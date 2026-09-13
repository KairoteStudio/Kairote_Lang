/* Self-tail calls reuse the current frame. Integer associative returns can carry
 * an accumulator because fixed-width addition/multiplication wrap modulo 2^N.
 * Eligibility depends on IR types, uses and frame lifetime, not benchmark inputs. */
static bool same_temporary(KrtIRValue* left, KrtIRValue* right) {
    return left->type == KRT_IR_VALUE_TEMP && right->type == KRT_IR_VALUE_TEMP && left->data.index == right->data.index;
}

static bool single_use_temporary(KROCodegenContext* ctx, KrtIRValue* value) {
    if (value->type != KRT_IR_VALUE_TEMP) {
        return false;
    }
    KROTempSlot* slot = find_temp_location(ctx, value->data.index);
    return slot && slot->use_count == 1 && !slot->cross_block && !slot->skip_register;
}

static bool associative_integer_opcode(KrtIROpcode opcode) {
    return opcode == KRT_IR_ADD || opcode == KRT_IR_MUL || opcode == KRT_IR_AND || opcode == KRT_IR_OR ||
           opcode == KRT_IR_XOR;
}

static KrtIRInst* skip_tail_copies(KROCodegenContext* ctx, KrtIRInst* inst, KrtIRValue** value) {
    while (inst && (inst->opcode == KRT_IR_CAST || inst->opcode == KRT_IR_COPY) && inst->operand_count >= 1 &&
           same_temporary(&inst->operands[0], *value) && KrtTokenIntegerBits((*value)->value_type) > 0 &&
           inst->result.value_type == (*value)->value_type && single_use_temporary(ctx, &inst->result)) {
        *value = &inst->result;
        inst = inst->next;
    }
    return inst;
}

static bool match_tail_return(KROCodegenContext* ctx, KrtIRInst* call, KrtIRInst** combine, KrtIRInst** ret) {
    KrtIRFunction* func = ctx->current_function;
    if (call->opcode != KRT_IR_CALL || call->operand_count != func->param_count + 1 ||
        call->operands[0].type != KRT_IR_VALUE_FUNCTION || !call->operands[0].data.function_name ||
        strcmp(call->operands[0].data.function_name, func->name) != 0 || call->result.value_type != func->return_type ||
        !single_use_temporary(ctx, &call->result)) {
        return false;
    }
    for (int i = 0; i < func->param_count; i++) {
        if (call->operands[i + 1].value_type != func->params[i].type) {
            return false;
        }
    }
    KrtIRValue* value = &call->result;
    KrtIRInst* inst = skip_tail_copies(ctx, call->next, &value);
    *combine = NULL;
    if (inst && associative_integer_opcode(inst->opcode) && inst->operand_count == 2 &&
        KrtTokenIntegerBits(func->return_type) > 0 && inst->result.value_type == func->return_type &&
        single_use_temporary(ctx, &inst->result)) {
        int recursive_operand =
            same_temporary(&inst->operands[0], value) ? 0 : (same_temporary(&inst->operands[1], value) ? 1 : -1);
        if (recursive_operand < 0) {
            return false;
        }
        KrtIRValue* other = &inst->operands[1 - recursive_operand];
        if (other->value_type != func->return_type ||
            (other->type != KRT_IR_VALUE_INTEGER && other->type != KRT_IR_VALUE_ARG &&
             other->type != KRT_IR_VALUE_TEMP)) {
            return false;
        }
        if (other->type == KRT_IR_VALUE_TEMP) {
            KROTempSlot* source = find_temp_location(ctx, other->data.index);
            KROTempSlot* result = find_temp_location(ctx, call->result.data.index);
            if (!source || source->skip_register || source->cross_block || source->first_position < 0 ||
                source->first_position >= result->first_position) {
                return false;
            }
        }
        *combine = inst;
        value = &inst->result;
        inst = skip_tail_copies(ctx, inst->next, &value);
    }
    if (!inst || inst->opcode != KRT_IR_RETURN || inst->operand_count != 1 ||
        !same_temporary(&inst->operands[0], value)) {
        return false;
    }
    *ret = inst;
    return true;
}

static void plan_tail_calls(KROCodegenContext* ctx, KrtIRFunction* func) {
    if (ctx->optimization_level < 2) {
        return;
    }
    /* Keep frame lifetime observable through addresses, stackalloc and locals.
     * Array metadata and PHI lowering need separate lifetime/parallel-copy work. */
    if (ctx->local_var_count || func->param_count != ctx->current_param_count) {
        return;
    }
    for (int i = 0; i < func->param_count; i++) {
        if (func->params[i].is_array) {
            return;
        }
    }
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->opcode == KRT_IR_ADDRESS_OF || inst->opcode == KRT_IR_STACKALLOC || inst->opcode == KRT_IR_PHI) {
                return;
            }
        }
    }

    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            KrtIRInst* combine;
            KrtIRInst* ret;
            if (!match_tail_return(ctx, inst, &combine, &ret)) {
                continue;
            }
            if (combine) {
                if (ctx->tail_acc_opcode != KRT_IR_NOP && ctx->tail_acc_opcode != combine->opcode) {
                    continue;
                }
                ctx->tail_acc_opcode = combine->opcode;
            }
            KROTempSlot* slot = find_temp_location(ctx, inst->result.data.index);
            slot->tail_combine = combine;
            slot->tail_return = ret;
            for (KrtIRInst* omitted = inst; omitted != ret; omitted = omitted->next) {
                KROTempSlot* unused = find_temp_location(ctx, omitted->result.data.index);
                if (unused) {
                    unused->no_storage = true;
                }
            }
            ctx->tail_site_count++;
        }
    }
    if (!ctx->tail_site_count) {
        return;
    }
    if (ctx->optimization_level >= 3) {
        /* Move a single-use call result straight into the hidden accumulator.
         * Its remaining use is an omitted associative combine in this block. */
        for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
            for (KrtIRInst* call = block->first_inst; call; call = call->next) {
                if (call->opcode != KRT_IR_CALL || call->result.type != KRT_IR_VALUE_TEMP) {
                    continue;
                }
                KROTempSlot* tail = find_temp_location(ctx, call->result.data.index);
                if (!tail || !tail->tail_combine) {
                    continue;
                }
                KrtIRInst* combine = tail->tail_combine;
                for (KrtIRInst* producer = block->first_inst; producer && producer != call; producer = producer->next) {
                    if (producer->opcode == KRT_IR_RETURN || producer->opcode == KRT_IR_JUMP ||
                        producer->opcode == KRT_IR_BRANCH) {
                        break;
                    }
                    if (producer->opcode != KRT_IR_CALL || !single_use_temporary(ctx, &producer->result) ||
                        producer->result.value_type != func->return_type ||
                        (!same_temporary(&producer->result, &combine->operands[0]) &&
                         !same_temporary(&producer->result, &combine->operands[1]))) {
                        continue;
                    }
                    KROTempSlot* contribution = find_temp_location(ctx, producer->result.data.index);
                    contribution->early_tail_accumulate = true;
                    contribution->no_storage = true;
                    tail->tail_combine = NULL;
                    break;
                }
            }
        }
    }
    int width = KrtTokenIntegerBits(func->return_type);
    if (ctx->tail_acc_opcode != KRT_IR_NOP) {
        ctx->packed_reg_bits[KRT_KRO_PACKED_REG_COUNT - 1] = 64;
        if (width > 64) {
            ctx->packed_reg_bits[KRT_KRO_PACKED_REG_COUNT - 2] = 64;
        }
    }
    ctx->tail_register_argument = func->param_count == 1 && KrtTokenIntegerBits(func->params[0].type) > 0 &&
                                  KrtTokenIntegerBits(func->params[0].type) <= 64 &&
                                  (ctx->tail_acc_opcode == KRT_IR_NOP || width <= 64);
}

static void emit_tail_accumulator_init(KROCodegenContext* ctx) {
    if (ctx->tail_acc_opcode == KRT_IR_NOP) {
        return;
    }
    uint64_t identity = ctx->tail_acc_opcode == KRT_IR_MUL ? 1 : (ctx->tail_acc_opcode == KRT_IR_AND ? UINT64_MAX : 0);
    emit_load_imm64_to_reg(ctx, identity, KRT_REG_R15);
    if (KrtTokenIntegerBits(ctx->current_function->return_type) > 64) {
        emit_load_imm64_to_reg(ctx, ctx->tail_acc_opcode == KRT_IR_AND ? UINT64_MAX : 0, KRT_REG_R14);
    }
}

/* The operand is in RDX:RAX. Only the low N bits are observable at the return;
 * internal accumulation can use the entire register/pair until then. */
static void emit_tail_accumulate(KROCodegenContext* ctx, bool update, int operand_reg) {
    if (ctx->tail_acc_opcode == KRT_IR_NOP) {
        return;
    }
    KrtTokenType type = ctx->current_function->return_type;
    int width = KrtTokenIntegerBits(type);
    uint8_t opcode =
        ctx->tail_acc_opcode == KRT_IR_ADD
            ? 0x01
            : (ctx->tail_acc_opcode == KRT_IR_AND ? 0x21 : (ctx->tail_acc_opcode == KRT_IR_OR ? 0x09 : 0x31));
    if (width <= 64) {
        int dst = update ? KRT_REG_R15 : KRT_REG_RAX;
        int src = update ? operand_reg : KRT_REG_R15;
        if (ctx->tail_acc_opcode == KRT_IR_MUL) {
            emit_byte(ctx, (width == 32 ? 0x40 : 0x48) | (dst >= 8 ? 4 : 0) | (src >= 8 ? 1 : 0));
            emit_bytes(ctx, (const uint8_t*)"\x0F\xAF", 2);
            emit_byte(ctx, 0xC0 | ((dst & 7) << 3) | (src & 7));
        } else if (width == 32) {
            emit_reg_op32(ctx, opcode, dst, src);
        } else {
            emit_reg_op(ctx, opcode, dst, src);
        }
        if (!update) {
            emit_extend_scalar(ctx, width, KrtTokenIsUnsigned(type), KRT_REG_RAX, KRT_REG_RAX);
        }
    } else {
        emit_move_reg_to_reg(ctx, KRT_REG_R15, KRT_REG_RCX);
        emit_move_reg_to_reg(ctx, KRT_REG_R14, KRT_REG_R8);
        if (ctx->tail_acc_opcode == KRT_IR_MUL) {
            emit_multiply_pair(ctx);
        } else {
            emit_reg_op(ctx, opcode, KRT_REG_RAX, KRT_REG_RCX);
            emit_reg_op(ctx, ctx->tail_acc_opcode == KRT_IR_ADD ? 0x11 : opcode, KRT_REG_RDX, KRT_REG_R8);
        }
        if (update) {
            emit_move_reg_to_reg(ctx, KRT_REG_RAX, KRT_REG_R15);
            emit_move_reg_to_reg(ctx, KRT_REG_RDX, KRT_REG_R14);
        } else {
            emit_normalize_pair(ctx, type, KRT_REG_RAX, KRT_REG_RDX);
        }
    }
}

/* Returning an associative identity requires only the accumulated value.
 * Normalize at the boundary exactly as the ordinary combine would. */
static bool emit_tail_identity_return(KROCodegenContext* ctx, KrtIRValue* value) {
    if (ctx->optimization_level < 3 || ctx->tail_acc_opcode == KRT_IR_NOP || value->type != KRT_IR_VALUE_INTEGER ||
        value->value_type != ctx->current_function->return_type) {
        return false;
    }
    KrtTokenType type = value->value_type;
    int width = KrtTokenIntegerBits(type);
    if (!width) {
        return false;
    }
    bool is_unsigned = KrtTokenIsUnsigned(type);
    KrtUInt128 identity =
        ctx->tail_acc_opcode == KRT_IR_MUL ? 1 : (ctx->tail_acc_opcode == KRT_IR_AND ? ~(KrtUInt128)0 : 0);
    if (KrtIntegerNormalize(value->data.integer, width, is_unsigned) !=
        KrtIntegerNormalize(identity, width, is_unsigned)) {
        return false;
    }
    if (width <= 64) {
        emit_extend_scalar(ctx, width, is_unsigned, KRT_REG_R15, KRT_REG_RAX);
    } else {
        emit_move_reg_to_reg(ctx, KRT_REG_R15, KRT_REG_RAX);
        emit_move_reg_to_reg(ctx, KRT_REG_R14, KRT_REG_RDX);
        emit_normalize_pair(ctx, type, KRT_REG_RAX, KRT_REG_RDX);
    }
    return true;
}

#include "KroTailUpdates.inc"

static bool emit_tail_register_copies(KROCodegenContext* ctx, KrtIRInst* call) {
    KrtIRFunction* func = ctx->current_function;
    if (ctx->optimization_level < 3 || ctx->tail_acc_opcode != KRT_IR_NOP || func->param_count < 2 ||
        func->param_count > 5) {
        return false;
    }
    const int scratch[] = {KRT_REG_RAX, KRT_REG_RCX, KRT_REG_RDX, KRT_REG_R8, KRT_REG_R9};
    unsigned occupied = 0;
    for (int i = 0; i < func->param_count; i++) {
        int width = KrtTokenIntegerBits(func->params[i].type);
        if (width <= 0 || width > 64) {
            return false;
        }
        int reg = scalar_low_register(ctx, &call->operands[i + 1]);
        if (reg >= 0) {
            occupied |= 1u << reg;
        }
    }
    int targets[5], count = 0;
    for (int i = 0; i < 5; i++) {
        if (!(occupied & (1u << scratch[i]))) {
            targets[count++] = scratch[i];
        }
    }
    if (count < func->param_count) {
        return false;
    }
    /* Destination scratch registers cannot destroy an unread argument. Packed
     * parameter stores use R10/R11, outside this set, so cycles remain safe. */
    for (int i = 0; i < func->param_count; i++) {
        emit_load_value_to_reg(ctx, &call->operands[i + 1], targets[i]);
    }
    for (int i = 0; i < func->param_count; i++) {
        emit_store_parameter_word(ctx, func, i, 0, targets[i]);
    }
    emit_tail_backedge(ctx);
    return true;
}

static bool emit_tail_call(KROCodegenContext* ctx, KrtIRInst* call) {
    if (!ctx->tail_site_count || call->opcode != KRT_IR_CALL || call->result.type != KRT_IR_VALUE_TEMP) {
        return false;
    }
    KROTempSlot* slot = find_temp_location(ctx, call->result.data.index);
    if (!slot || !slot->tail_return) {
        return false;
    }
    if (emit_scheduled_tail_updates(ctx, call)) {
        return true;
    }
    if (emit_tail_register_copies(ctx, call)) {
        return true;
    }
    KrtIRFunction* func = ctx->current_function;
    KrtIRValue* contribution = NULL;
    int contribution_reg = -1;
    if (slot->tail_combine) {
        KrtIRInst* combine = slot->tail_combine;
        KrtIRInst* previous = call;
        while (previous->next != combine) {
            previous = previous->next;
        }
        contribution = &combine->operands[same_temporary(&combine->operands[0], &previous->result) ? 1 : 0];
        contribution_reg = scalar_low_register(ctx, contribution);
    }
    int argument_reg =
        contribution && (contribution_reg < 0 || contribution_reg == KRT_REG_RAX) ? KRT_REG_R9 : KRT_REG_RAX;
    if (ctx->tail_register_argument) {
        emit_load_value_to_reg(ctx, &call->operands[1], argument_reg);
    } else {
        /* Snapshot every argument before writing any parameter (including cycles
         * and packed neighbours). The storage belongs to this frame, not RSP. */
        for (int i = 0; i < func->param_count; i++) {
            emit_load_pair(ctx, &call->operands[i + 1], KRT_REG_RAX, KRT_REG_RDX);
            emit_store_to_stack(ctx, ctx->tail_arg_offsets[i], KRT_REG_RAX);
            if (KrtTokenIntegerBits(func->params[i].type) > 64) {
                emit_store_to_stack(ctx, ctx->tail_arg_offsets[i] - 8, KRT_REG_RDX);
            }
        }
    }
    if (contribution) {
        if (contribution_reg < 0) {
            emit_load_for_type(ctx, contribution, func->return_type, KRT_REG_RAX, KRT_REG_RDX);
            contribution_reg = KRT_REG_RAX;
        }
        emit_tail_accumulate(ctx, true, contribution_reg);
    }
    if (ctx->tail_register_argument) {
        emit_store_parameter_word(ctx, func, 0, 0, argument_reg);
    } else {
        for (int i = 0; i < func->param_count; i++) {
            emit_load_from_stack(ctx, ctx->tail_arg_offsets[i], KRT_REG_RAX);
            emit_store_parameter_word(ctx, func, i, 0, KRT_REG_RAX);
            if (KrtTokenIntegerBits(func->params[i].type) > 64) {
                emit_load_from_stack(ctx, ctx->tail_arg_offsets[i] - 8, KRT_REG_RAX);
                emit_store_parameter_word(ctx, func, i, 1, KRT_REG_RAX);
            }
        }
    }
    emit_tail_backedge(ctx);
    return true;
}
