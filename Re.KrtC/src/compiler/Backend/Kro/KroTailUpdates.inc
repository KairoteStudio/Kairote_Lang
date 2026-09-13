/* Parallel parameter updates can run in place if every reader of an old
 * parameter runs before its writer. Cycles retain the ordinary snapshots. */
#define KRT_KRO_TAIL_UPDATE_LIMIT 5
#define KRT_KRO_TAIL_PREFIX_LIMIT 64
#define KRT_KRO_TAIL_DEFINITION_LIMIT 24

typedef struct {
    KrtIROpcode opcode;
    KrtIRValue left;
    KrtIRValue right;
    unsigned reads;
} KROTailUpdate;

typedef struct {
    KROTailUpdate updates[KRT_KRO_TAIL_UPDATE_LIMIT];
    int order[KRT_KRO_TAIL_UPDATE_LIMIT];
    int update_count;
    KrtIRInst* definitions[KRT_KRO_TAIL_DEFINITION_LIMIT];
    int definition_count;
    KrtIRInst* prefix[KRT_KRO_TAIL_PREFIX_LIMIT];
    int prefix_count;
} KROTailUpdatePlan;

static bool tail_identity_instruction(KrtIRInst* inst) {
    return inst->opcode == KRT_IR_COPY || inst->opcode == KRT_IR_CAST || inst->opcode == KRT_IR_IMM;
}

static KrtIRInst* tail_update_definition(KROCodegenContext* ctx, KROTailUpdatePlan* plan, KrtIRValue* value) {
    if (!single_use_temporary(ctx, value) || KrtTokenIntegerBits(value->value_type) != 64) {
        return NULL;
    }
    for (int i = 0; i < plan->prefix_count; i++) {
        KrtIRInst* inst = plan->prefix[i];
        if (inst->result.type != KRT_IR_VALUE_TEMP || inst->result.data.index != value->data.index) {
            continue;
        }
        if (KrtTokenIntegerBits(inst->result.value_type) != 64 ||
            plan->definition_count == KRT_KRO_TAIL_DEFINITION_LIMIT) {
            return NULL;
        }
        /* A single use makes these expression trees, without shared producers. */
        for (int j = 0; j < plan->definition_count; j++) {
            if (plan->definitions[j] == inst) {
                return NULL;
            }
        }
        plan->definitions[plan->definition_count++] = inst;
        return inst;
    }
    return NULL;
}

static bool tail_update_leaf(KROCodegenContext* ctx, KROTailUpdatePlan* plan, KrtIRValue value, KrtIRValue* leaf) {
    while (value.type == KRT_IR_VALUE_TEMP) {
        KrtIRInst* definition = tail_update_definition(ctx, plan, &value);
        if (!definition || !tail_identity_instruction(definition) || definition->operand_count < 1) {
            return false;
        }
        value = definition->operands[0];
    }
    int width = KrtTokenIntegerBits(value.value_type);
    if (value.type == KRT_IR_VALUE_INTEGER && width > 0 && width <= 64) {
        *leaf = value;
        return true;
    }
    if (value.type != KRT_IR_VALUE_ARG || width != 64 || value.data.index < 0 ||
        value.data.index >= ctx->current_param_count || ctx->arg_address_taken[value.data.index]) {
        return false;
    }
    KROPackedField* field = &ctx->arg_packed[value.data.index];
    if (field->bit_width != 64 || field->bit_offset) {
        return false;
    }
    *leaf = value;
    return true;
}

static bool build_tail_update_plan(KROCodegenContext* ctx, KrtIRBasicBlock* block, KrtIRInst* call,
                                   KROTailUpdatePlan* plan) {
    KrtIRFunction* function = ctx->current_function;
    if (ctx->optimization_level < 3 || ctx->tail_acc_opcode != KRT_IR_NOP || function->param_count < 2 ||
        function->param_count > KRT_KRO_TAIL_UPDATE_LIMIT || call->opcode != KRT_IR_CALL ||
        call->result.type != KRT_IR_VALUE_TEMP) {
        return false;
    }
    KROTempSlot* tail = find_temp_location(ctx, call->result.data.index);
    if (!tail || !tail->tail_return) {
        return false;
    }
    for (int i = 0; i < function->param_count; i++) {
        if (function->params[i].is_array || KrtTokenIntegerBits(function->params[i].type) != 64 ||
            ctx->arg_address_taken[i] || ctx->arg_packed[i].bit_width != 64 || ctx->arg_packed[i].bit_offset) {
            return false;
        }
    }
    memset(plan, 0, sizeof(*plan));
    KrtIRInst* prefix = block->first_inst;
    for (; prefix && prefix != call; prefix = prefix->next) {
        if (prefix->opcode == KRT_IR_NOP) {
            continue;
        }
        int operation;
        uint8_t opcode;
        if (plan->prefix_count == KRT_KRO_TAIL_PREFIX_LIMIT ||
            (!tail_identity_instruction(prefix) && !scalar_binary_encoding(prefix->opcode, &operation, &opcode)) ||
            prefix->result.type != KRT_IR_VALUE_TEMP || !KrtTokenIntegerBits(prefix->result.value_type)) {
            return false;
        }
        plan->prefix[plan->prefix_count++] = prefix;
    }
    if (prefix != call) {
        return false;
    }
    unsigned remaining = 0;
    for (int i = 0; i < function->param_count; i++) {
        KROTailUpdate* update = &plan->updates[i];
        KrtIRValue value = call->operands[i + 1];
        update->opcode = KRT_IR_COPY;
        while (value.type == KRT_IR_VALUE_TEMP) {
            KrtIRInst* definition = tail_update_definition(ctx, plan, &value);
            if (!definition) {
                return false;
            }
            if (tail_identity_instruction(definition)) {
                if (definition->operand_count < 1) {
                    return false;
                }
                value = definition->operands[0];
                continue;
            }
            int operation;
            uint8_t opcode;
            if (definition->operand_count != 2 || !scalar_binary_encoding(definition->opcode, &operation, &opcode) ||
                !tail_update_leaf(ctx, plan, definition->operands[0], &update->left) ||
                !tail_update_leaf(ctx, plan, definition->operands[1], &update->right)) {
                return false;
            }
            update->opcode = definition->opcode;
            break;
        }
        if (update->opcode == KRT_IR_COPY && !tail_update_leaf(ctx, plan, value, &update->left)) {
            return false;
        }
        if (update->left.type == KRT_IR_VALUE_ARG) {
            update->reads |= 1u << update->left.data.index;
        }
        if (update->opcode != KRT_IR_COPY && update->right.type == KRT_IR_VALUE_ARG) {
            update->reads |= 1u << update->right.data.index;
        }
        if (update->opcode != KRT_IR_COPY || update->left.type != KRT_IR_VALUE_ARG || update->left.data.index != i) {
            remaining |= 1u << i;
        }
    }
    while (remaining) {
        int selected = -1;
        for (int i = 0; i < function->param_count && selected < 0; i++) {
            if (!(remaining & (1u << i))) {
                continue;
            }
            bool has_reader = false;
            for (int j = 0; j < function->param_count; j++) {
                if (j != i && (remaining & (1u << j)) && (plan->updates[j].reads & (1u << i))) {
                    has_reader = true;
                }
            }
            if (!has_reader) {
                selected = i;
            }
        }
        if (selected < 0) {
            return false;
        }
        plan->order[plan->update_count++] = selected;
        remaining &= ~(1u << selected);
    }
    return true;
}

static void plan_tail_register_updates(KROCodegenContext* ctx, KrtIRFunction* func) {
    if (ctx->optimization_level < 3 || !ctx->tail_site_count || ctx->tail_acc_opcode != KRT_IR_NOP) {
        return;
    }
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* call = block->first_inst; call; call = call->next) {
            if (call->opcode != KRT_IR_CALL) {
                continue;
            }
            KROTailUpdatePlan plan;
            if (!build_tail_update_plan(ctx, block, call, &plan)) {
                continue;
            }
            for (int i = 0; i < plan.definition_count; i++) {
                KROTempSlot* slot = find_temp_location(ctx, plan.definitions[i]->result.data.index);
                slot->deferred_tail_argument = true;
                slot->no_storage = true;
            }
        }
    }
}

static bool is_deferred_tail_argument(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (inst->result.type != KRT_IR_VALUE_TEMP) {
        return false;
    }
    KROTempSlot* slot = find_temp_location(ctx, inst->result.data.index);
    return slot && slot->deferred_tail_argument;
}

static void emit_tail_update(KROCodegenContext* ctx, KROTailUpdate* update, int parameter) {
    int target = ctx->arg_packed[parameter].reg;
    KrtIRValue* left = &update->left;
    KrtIRValue* right = &update->right;
    if (update->opcode == KRT_IR_COPY) {
        emit_load_value_to_reg(ctx, left, target);
        return;
    }
    int left_reg = scalar_register64(ctx, left);
    int right_reg = scalar_register64(ctx, right);
    if (update->opcode != KRT_IR_SUB && right_reg == target) {
        KrtIRValue* swap = left;
        left = right;
        right = swap;
        right_reg = left_reg;
        left_reg = target;
    }
    /* For c - old_parameter, loading c into the destination would erase the
     * second operand. RAX is outside the permanent parameter register set. */
    int result_reg = right_reg == target && left_reg != target ? KRT_REG_RAX : target;
    emit_load_value_to_reg(ctx, left, result_reg);
    int operation;
    uint8_t opcode;
    if (!scalar_binary_encoding(update->opcode, &operation, &opcode)) {
        return;
    }
    if (right->type == KRT_IR_VALUE_INTEGER && operation >= 0 && fits_signed_imm32(scalar_integer_constant(right))) {
        emit_imm_op64(ctx, operation, result_reg, scalar_integer_constant(right));
    } else {
        if (right_reg < 0) {
            right_reg = result_reg == KRT_REG_RAX ? KRT_REG_RCX : KRT_REG_RAX;
            emit_load_value_to_reg(ctx, right, right_reg);
        }
        if (operation < 0) {
            emit_byte(ctx, 0x48 | (result_reg >= 8 ? 4 : 0) | (right_reg >= 8 ? 1 : 0));
            emit_bytes(ctx, (const uint8_t*)"\x0F\xAF", 2);
            emit_byte(ctx, 0xC0 | ((result_reg & 7) << 3) | (right_reg & 7));
        } else {
            emit_reg_op(ctx, opcode, result_reg, right_reg);
        }
    }
    emit_move_reg_to_reg(ctx, result_reg, target);
}

static bool emit_scheduled_tail_updates(KROCodegenContext* ctx, KrtIRInst* call) {
    KROTailUpdatePlan plan;
    if (!build_tail_update_plan(ctx, ctx->current_block, call, &plan)) {
        return false;
    }
    for (int i = 0; i < plan.update_count; i++) {
        int parameter = plan.order[i];
        emit_tail_update(ctx, &plan.updates[parameter], parameter);
    }
    emit_tail_backedge(ctx);
    return true;
}
