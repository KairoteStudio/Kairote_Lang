/* Hoist a pure entry guard and its trivial return ahead of the stack frame.
 * Neither removed block may have another incoming edge. */
static int incoming_scalar_register(KrtIRFunction* func, KrtIRValue* value) {
    if (value->type != KRT_IR_VALUE_ARG || value->data.index < 0 || value->data.index >= func->param_count) {
        return -1;
    }
    int reg_index = 0;
    for (int i = 0; i <= value->data.index; i++) {
        int words = !func->params[i].is_array && KrtTokenIntegerBits(func->params[i].type) > 64 ? 2 : 1;
        bool in_registers = reg_index + words <= g_arg_reg_count;
        if (i == value->data.index) {
            return in_registers && words == 1 && !func->params[i].is_array ? g_arg_regs[reg_index] : -1;
        }
        if (in_registers) {
            reg_index += words;
        }
    }
    return -1;
}

static bool is_entry_scalar(KrtIRFunction* func, KrtIRValue* value) {
    int width = KrtTokenIntegerBits(value->value_type);
    return width > 0 && width <= 64 &&
           (value->type == KRT_IR_VALUE_INTEGER || incoming_scalar_register(func, value) >= 0);
}

static bool trivial_leaf_value(KROCodegenContext* ctx, KrtIRFunction* func, KrtIRBasicBlock* block, KrtIRValue* value) {
    if (!block) {
        return false;
    }
    KrtIRInst* inst = block->first_inst;
    bool transfer =
        inst && (inst->opcode == KRT_IR_CAST ||
                 (ctx->optimization_level >= 3 && (inst->opcode == KRT_IR_COPY || inst->opcode == KRT_IR_IMM)));
    if (transfer && inst->operand_count >= 1 && inst->result.type == KRT_IR_VALUE_TEMP && inst->next &&
        inst->next->opcode == KRT_IR_RETURN && inst->next->operand_count == 1) {
        KrtIRValue returned = inst->next->operands[0];
        if (returned.type == KRT_IR_VALUE_TEMP && returned.data.index == inst->result.data.index) {
            *value = inst->operands[0];
        } else {
            /* Value numbering can forward the original argument into RETURN,
             * leaving an unused integer transfer before it. */
            if (ctx->optimization_level < 3 || !KrtTokenIntegerBits(inst->result.value_type) ||
                !KrtTokenIntegerBits(inst->operands[0].value_type) ||
                (inst->operands[0].type != KRT_IR_VALUE_INTEGER && inst->operands[0].type != KRT_IR_VALUE_ARG)) {
                return false;
            }
            *value = returned;
            return value->value_type == func->return_type && is_entry_scalar(func, value);
        }
        if (value->value_type != inst->result.value_type) {
            int from = KrtTokenIntegerBits(value->value_type);
            int to = KrtTokenIntegerBits(inst->result.value_type);
            if (ctx->optimization_level < 3 || value->type != KRT_IR_VALUE_INTEGER || !from || !to) {
                return false;
            }
            value->data.integer = KrtIntegerNormalize(value->data.integer, from, KrtTokenIsUnsigned(value->value_type));
            value->data.integer =
                KrtIntegerNormalize(value->data.integer, to, KrtTokenIsUnsigned(inst->result.value_type));
            value->value_type = inst->result.value_type;
        }
    } else if (inst && inst->opcode == KRT_IR_RETURN && inst->operand_count == 1) {
        *value = inst->operands[0];
    } else {
        return false;
    }
    return value->value_type == func->return_type && is_entry_scalar(func, value);
}

static void emit_entry_scalar(KROCodegenContext* ctx, KrtIRFunction* func, KrtIRValue* value, int target) {
    if (value->type == KRT_IR_VALUE_INTEGER) {
        emit_load_imm64_to_reg(ctx, (uint64_t)value->data.integer, target);
    } else {
        emit_extend_scalar(ctx, KrtTokenIntegerBits(value->value_type), KrtTokenIsUnsigned(value->value_type),
                           incoming_scalar_register(func, value), target);
    }
}

static bool entry_leaf_return(KROCodegenContext* ctx, KrtIRFunction* func, int* selected_leaf, KrtIRValue* value) {
    if (!ctx->optimization_level) {
        return false;
    }
    KrtIRBasicBlock* entry = func->entry_block;
    KrtIRInst* compare = entry ? entry->first_inst : NULL;
    if (!compare || entry->pred_count || !can_fuse_integer_branch(ctx, entry, compare) ||
        entry->succs[0] == entry->succs[1] || !is_entry_scalar(func, &compare->operands[0]) ||
        !is_entry_scalar(func, &compare->operands[1])) {
        return false;
    }
    int leaf_index = 0;
    if (!trivial_leaf_value(ctx, func, entry->succs[0], value)) {
        leaf_index = 1;
        if (!trivial_leaf_value(ctx, func, entry->succs[1], value)) {
            return false;
        }
    }
    KrtIRBasicBlock* leaf = entry->succs[leaf_index];
    if (leaf->pred_count != 1 || leaf->preds[0] != entry) {
        return false;
    }
    *selected_leaf = leaf_index;
    return true;
}

static uint8_t emit_entry_compare(KROCodegenContext* ctx, KrtIRFunction* func, KrtIRInst* compare) {
    KrtTokenType type = KrtTokenIntegerCommon(compare->operands[0].value_type, compare->operands[1].value_type);
    int width = KrtTokenIntegerBits(type);
    int source = incoming_scalar_register(func, &compare->operands[0]);
    if (width == 32 && KrtTokenIntegerBits(compare->operands[0].value_type) == 32 && source >= 0 &&
        compare->operands[1].type == KRT_IR_VALUE_INTEGER) {
        emit_imm_op32(ctx, 7, source, (uint32_t)compare->operands[1].data.integer);
    } else if (ctx->optimization_level >= 3 && width == 64 &&
               KrtTokenIntegerBits(compare->operands[0].value_type) == 64 && source >= 0 &&
               compare->operands[1].type == KRT_IR_VALUE_INTEGER &&
               fits_signed_imm32(scalar_integer_constant(&compare->operands[1]))) {
        emit_imm_op64(ctx, 7, source, scalar_integer_constant(&compare->operands[1]));
    } else {
        /* These scratch registers never overlap incoming argument registers. */
        emit_entry_scalar(ctx, func, &compare->operands[0], KRT_REG_R10);
        emit_extend_scalar(ctx, width, KrtTokenIsUnsigned(type), KRT_REG_R10, KRT_REG_R10);
        emit_entry_scalar(ctx, func, &compare->operands[1], KRT_REG_R11);
        emit_extend_scalar(ctx, width, KrtTokenIsUnsigned(type), KRT_REG_R11, KRT_REG_R11);
        emit_reg_op(ctx, 0x39, KRT_REG_R10, KRT_REG_R11);
    }
    return integer_condition(compare->opcode, KrtTokenIsUnsigned(type)) - 0x20;
}

static KrtIRBasicBlock* emit_leaf_return_guard(KROCodegenContext* ctx, KrtIRFunction* func) {
    int leaf_index;
    KrtIRValue value;
    if (!entry_leaf_return(ctx, func, &leaf_index, &value)) {
        return NULL;
    }
    KrtIRBasicBlock* entry = func->entry_block;
    KrtIRInst* compare = entry->first_inst;
    KrtIRBasicBlock* leaf = entry->succs[leaf_index];
    uint8_t condition = emit_entry_compare(ctx, func, compare);
    emit_byte(ctx, leaf_index ? condition : condition ^ 1);
    uint32_t displacement = kro_get_code_offset(ctx->writer);
    emit_byte(ctx, 0);
    emit_entry_scalar(ctx, func, &value, KRT_REG_RAX);
    emit_byte(ctx, KRT_X86_RET);
    uint32_t end = kro_get_code_offset(ctx->writer);
    kro_set_code_offset(ctx->writer, displacement);
    emit_byte(ctx, (uint8_t)(end - displacement - 1));
    kro_set_code_offset(ctx->writer, end);
    /* Rotate bounded tail loops: the backedge tests the guard directly and
     * continues at the body, saving the unconditional jump through the header. */
    if (ctx->optimization_level >= 3 && ctx->tail_site_count && ctx->tail_site_count <= 16) {
        ctx->rotated_tail_leaf = leaf;
    }
    return ctx->tail_site_count && !ctx->rotated_tail_leaf ? NULL : leaf;
}

static void emit_tail_backedge(KROCodegenContext* ctx) {
    KrtIRFunction* func = ctx->current_function;
    if (!ctx->rotated_tail_leaf) {
        emit_jump_to_block(ctx, func->entry_block);
        return;
    }
    KrtIRBasicBlock* entry = func->entry_block;
    KrtIRInst* compare = entry->first_inst;
    int leaf_index = entry->succs[0] == ctx->rotated_tail_leaf ? 0 : 1;
    KrtTokenType type = KrtTokenIntegerCommon(compare->operands[0].value_type, compare->operands[1].value_type);
    emit_scalar_compare_flags(ctx, compare, type);
    uint8_t condition = integer_condition(compare->opcode, KrtTokenIsUnsigned(type)) - 0x10;
    emit_cond_jump_to_block(ctx, leaf_index ? condition : condition ^ 1, entry->succs[1 - leaf_index]);
    KrtIRValue value;
    if (!trivial_leaf_value(ctx, func, ctx->rotated_tail_leaf, &value)) {
        return;
    }
    if (!emit_tail_identity_return(ctx, &value)) {
        emit_load_for_type(ctx, &value, func->return_type, KRT_REG_RAX, KRT_REG_RDX);
        emit_tail_accumulate(ctx, false, KRT_REG_RAX);
    }
    if (ctx->current_block->next) {
        emit_byte(ctx, 0xE9);
        uint32_t offset = kro_get_code_offset(ctx->writer);
        emit_u32(ctx, 0);
        kro_add_reloc(ctx->writer, KRO_SEC_TEXT, offset, ctx->epilogue_symbol, KRO_RELOC_PC32, 0);
    }
}
