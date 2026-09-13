/* Wide integers use RDX:RAX, with RCX/R8 for the right operand. */
static void emit_reg_op(KROCodegenContext* ctx, uint8_t opcode, int dst, int src) {
    emit_byte(ctx, 0x48 | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0));
    emit_byte(ctx, opcode);
    emit_byte(ctx, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

static void emit_shift_imm(KROCodegenContext* ctx, int reg, int operation, int count) {
    emit_byte(ctx, 0x48 | (reg >= 8 ? 1 : 0));
    emit_byte(ctx, 0xC1);
    emit_byte(ctx, 0xC0 | (operation << 3) | (reg & 7));
    emit_byte(ctx, (uint8_t)count);
}

static uint32_t emit_local_jump(KROCodegenContext* ctx, int condition) {
    if (condition) {
        emit_byte(ctx, 0x0F);
        emit_byte(ctx, (uint8_t)condition);
    } else {
        emit_byte(ctx, 0xE9);
    }
    uint32_t offset = kro_get_code_offset(ctx->writer);
    emit_u32(ctx, 0);
    return offset;
}

static void patch_local_jump(KROCodegenContext* ctx, uint32_t offset, uint32_t target) {
    uint32_t end = kro_get_code_offset(ctx->writer);
    kro_set_code_offset(ctx->writer, offset);
    emit_u32(ctx, target - offset - 4);
    kro_set_code_offset(ctx->writer, end);
}

static void emit_extend_scalar(KROCodegenContext* ctx, int bits, bool is_unsigned, int source, int target) {
    uint8_t rex = 0x48 | (target >= 8 ? 4 : 0) | (source >= 8 ? 1 : 0);
    if (bits == 32) {
        if (is_unsigned) {
            if (rex != 0x48) {
                emit_byte(ctx, rex & ~8);
            }
            emit_byte(ctx, 0x8B);
        } else {
            emit_byte(ctx, rex);
            emit_byte(ctx, 0x63);
        }
        emit_byte(ctx, 0xC0 | ((target & 7) << 3) | (source & 7));
    } else if (bits == 8 || bits == 16) {
        emit_byte(ctx, rex);
        emit_byte(ctx, 0x0F);
        emit_byte(ctx, (is_unsigned ? 0xB6 : 0xBE) + (bits == 16));
        emit_byte(ctx, 0xC0 | ((target & 7) << 3) | (source & 7));
    } else {
        emit_move_reg_to_reg(ctx, source, target);
        if (bits > 0 && bits < 64) {
            emit_shift_imm(ctx, target, 4, 64 - bits);
            emit_shift_imm(ctx, target, is_unsigned ? 5 : 7, 64 - bits);
        }
    }
}

static void emit_normalize_pair(KROCodegenContext* ctx, KrtTokenType type, int low, int high) {
    int bits = KrtTokenIntegerBits(type);
    bool is_unsigned = KrtTokenIsUnsigned(type);
    if (!bits) {
        return;
    }
    if (bits < 64) {
        emit_extend_scalar(ctx, bits, is_unsigned, low, low);
    } else if (bits > 64 && bits < 128) {
        emit_extend_scalar(ctx, bits - 64, is_unsigned, high, high);
    }
    if (bits <= 64) {
        if (is_unsigned) {
            emit_load_imm64_to_reg(ctx, 0, high);
        } else {
            emit_move_reg_to_reg(ctx, low, high);
            emit_shift_imm(ctx, high, 7, 63);
        }
    }
}

static void emit_load_high(KROCodegenContext* ctx, KrtIRValue* value, int low, int high) {
    if (KrtTokenIntegerBits(value->value_type) <= 64) {
        if (KrtTokenIsUnsigned(value->value_type) || value->type == KRT_IR_VALUE_STRING_CONST) {
            emit_load_imm64_to_reg(ctx, 0, high);
        } else {
            emit_move_reg_to_reg(ctx, low, high);
            emit_shift_imm(ctx, high, 7, 63);
        }
        return;
    }
    int slot = -1;
    bool addressable = false;
    switch (value->type) {
    case KRT_IR_VALUE_INTEGER:
        emit_load_imm64_to_reg(ctx, (uint64_t)(value->data.integer >> 64), high);
        return;
    case KRT_IR_VALUE_TEMP: {
        KROTempSlot* temp = find_temp_location(ctx, value->data.index);
        if (temp && temp->reg_high >= 0) {
            emit_move_reg_to_reg(ctx, temp->reg_high, high);
            return;
        }
        slot = find_temp_slot(ctx, value->data.index);
        break;
    }
    case KRT_IR_VALUE_ARG:
        if (value->data.index >= 0 && value->data.index < ctx->current_param_count) {
            KROPackedField* field = &ctx->arg_packed[value->data.index];
            if (field->bit_width) {
                emit_load_packed_field(ctx, field, KrtTokenIsUnsigned(value->value_type), high);
                return;
            }
            slot = ctx->arg_stack_offsets[value->data.index];
            addressable = ctx->arg_address_taken[value->data.index];
        }
        break;
    case KRT_IR_VALUE_VAR: {
        KROLocalVar* var = find_local_var(ctx, value->data.name);
        if (var) {
            if (var->packed.bit_width) {
                emit_load_packed_field(ctx, &var->packed, KrtTokenIsUnsigned(var->type), high);
                return;
            }
            slot = var->stack_offset;
            addressable = var->address_taken;
        } else {
            int symbol = kro_find_symbol(ctx->writer, value->data.name);
            if (symbol >= 0) {
                emit_byte(ctx, 0x48 | (high >= 8 ? 4 : 0));
                emit_byte(ctx, 0x8B);
                emit_byte(ctx, 0x05 | ((high & 7) << 3));
                uint32_t offset = kro_get_code_offset(ctx->writer);
                emit_u32(ctx, 0);
                kro_add_reloc(ctx->writer, KRO_SEC_TEXT, offset, symbol, KRO_RELOC_PC32, 8);
                int fragment = KrtTokenIntegerBits(value->value_type) - 64;
                if (fragment > 0 && fragment < 64) {
                    emit_extend_scalar(ctx, fragment, KrtTokenIsUnsigned(value->value_type), high, high);
                }
                return;
            }
        }
        break;
    }
    default:
        break;
    }
    if (slot > 0) {
        emit_load_from_stack(ctx, slot - 8, high);
    } else {
        emit_load_imm64_to_reg(ctx, 0, high);
    }
    int fragment = KrtTokenIntegerBits(value->value_type) - 64;
    if (addressable && fragment > 0 && fragment < 64) {
        emit_extend_scalar(ctx, fragment, KrtTokenIsUnsigned(value->value_type), high, high);
    }
}

static void emit_load_pair(KROCodegenContext* ctx, KrtIRValue* value, int low, int high) {
    emit_load_value_to_reg(ctx, value, low);
    emit_load_high(ctx, value, low, high);
}

static void emit_load_for_type(KROCodegenContext* ctx, KrtIRValue* value, KrtTokenType type, int low, int high) {
    emit_load_value_to_reg(ctx, value, low);
    bool source_float = value->value_type == TOKEN_FLOAT32 || value->value_type == TOKEN_FLOAT64;
    bool target_float = type == TOKEN_FLOAT32 || type == TOKEN_FLOAT64;
    if (target_float && !source_float) {
        emit_byte(ctx, 0xF2);
        emit_byte(ctx, 0x48 | (low >= 8 ? 1 : 0));
        emit_bytes(ctx, (const uint8_t*)"\x0F\x2A", 2);
        emit_byte(ctx, 0xC0 | (low & 7));
        emit_byte(ctx, 0x66);
        emit_byte(ctx, 0x48 | (low >= 8 ? 1 : 0));
        emit_bytes(ctx, (const uint8_t*)"\x0F\x7E", 2);
        emit_byte(ctx, 0xC0 | (low & 7));
    } else if (source_float && KrtTokenIntegerBits(type)) {
        emit_byte(ctx, 0x66);
        emit_byte(ctx, 0x48 | (low >= 8 ? 1 : 0));
        emit_bytes(ctx, (const uint8_t*)"\x0F\x6E", 2);
        emit_byte(ctx, 0xC0 | (low & 7));
        emit_byte(ctx, 0xF2);
        emit_byte(ctx, 0x48 | (low >= 8 ? 4 : 0));
        emit_bytes(ctx, (const uint8_t*)"\x0F\x2C", 2);
        emit_byte(ctx, 0xC0 | ((low & 7) << 3));
    }
    if (type == TOKEN_FLOAT32) {
        emit_float32_conversion(ctx, low, true);
        emit_float32_conversion(ctx, low, false);
    }
    if (KrtTokenIntegerBits(type) > 64) {
        emit_load_high(ctx, value, low, high);
    }
}

static void emit_load_condition(KROCodegenContext* ctx, KrtIRValue* value) {
    emit_load_for_type(ctx, value, value->value_type, KRT_REG_RAX, KRT_REG_RDX);
    if (KrtTokenIntegerBits(value->value_type) > 64) {
        emit_reg_op(ctx, 0x09, KRT_REG_RAX, KRT_REG_RDX);
    } else {
        emit_reg_op(ctx, 0x85, KRT_REG_RAX, KRT_REG_RAX);
    }
}

static void emit_store_value(KROCodegenContext* ctx, KrtIRValue* dest) {
    KROTempSlot* temp = NULL;
    if (dest->type == KRT_IR_VALUE_TEMP) {
        temp = find_temp_location(ctx, dest->data.index);
        if (temp && (!temp->use_count || temp->no_storage)) {
            return;
        }
    }
    if (dest->value_type == TOKEN_FLOAT32 && dest->type != KRT_IR_VALUE_TEMP) {
        emit_float32_conversion(ctx, KRT_REG_RAX, true);
    }
    bool wide = KrtTokenIntegerBits(dest->value_type) > 64;
    if (wide) {
        emit_normalize_pair(ctx, dest->value_type, KRT_REG_RAX, KRT_REG_RDX);
    } else if (!temp || !temp->low_word_only) {
        emit_extend_scalar(ctx, KrtTokenIntegerBits(dest->value_type), KrtTokenIsUnsigned(dest->value_type),
                           KRT_REG_RAX, KRT_REG_RAX);
    }
    int slot = -1;
    if (dest->type == KRT_IR_VALUE_TEMP) {
        if (temp && temp->reg_low >= 0) {
            emit_move_reg_to_reg(ctx, KRT_REG_RAX, temp->reg_low);
            if (temp->reg_high >= 0) {
                if (!wide) {
                    emit_load_high(ctx, dest, KRT_REG_RAX, KRT_REG_RDX);
                }
                emit_move_reg_to_reg(ctx, KRT_REG_RDX, temp->reg_high);
            }
            return;
        }
        slot = alloc_temp_slot(ctx, dest->data.index);
    } else if (dest->type == KRT_IR_VALUE_ARG) {
        int index = dest->data.index;
        KROPackedField* field = &ctx->arg_packed[index];
        if (field->bit_width) {
            if (wide) {
                emit_store_to_stack(ctx, ctx->arg_stack_offsets[index], KRT_REG_RAX);
            }
            emit_store_packed_field(ctx, field, wide ? KRT_REG_RDX : KRT_REG_RAX);
            return;
        }
        slot = ctx->arg_stack_offsets[index];
    } else if (dest->type == KRT_IR_VALUE_VAR) {
        KROLocalVar* var = find_local_var(ctx, dest->data.name);
        if (var && var->packed.bit_width) {
            if (wide) {
                emit_store_to_stack(ctx, var->stack_offset, KRT_REG_RAX);
            }
            emit_store_packed_field(ctx, &var->packed, wide ? KRT_REG_RDX : KRT_REG_RAX);
            return;
        }
        int symbol = var ? -1 : kro_find_symbol(ctx->writer, dest->data.name);
        if (symbol >= 0) {
            for (int i = 0; i < (wide ? 2 : 1); i++) {
                emit_bytes(ctx, (const uint8_t*)"\x48\x89", 2);
                emit_byte(ctx, i ? 0x15 : 0x05);
                uint32_t offset = kro_get_code_offset(ctx->writer);
                emit_u32(ctx, 0);
                kro_add_reloc(ctx->writer, KRO_SEC_TEXT, offset, symbol, KRO_RELOC_PC32, i * 8);
            }
            return;
        }
        if (!var) {
            var = find_or_alloc_local_var(ctx, dest->data.name);
        }
        if (var) {
            slot = var->stack_offset;
        }
    }
    if (slot > 0) {
        emit_store_to_stack(ctx, slot, KRT_REG_RAX);
        if (wide) {
            emit_store_to_stack(ctx, slot - 8, KRT_REG_RDX);
        }
    }
}

static void emit_negate_pair_by_sign(KROCodegenContext* ctx, int low, int high, int sign) {
    emit_reg_op(ctx, 0x31, low, sign);
    emit_reg_op(ctx, 0x31, high, sign);
    emit_reg_op(ctx, 0x29, low, sign);
    emit_reg_op(ctx, 0x19, high, sign);
}

static void emit_wide_division(KROCodegenContext* ctx, bool is_unsigned, bool remainder) {
    /* Save RSI/RDI also on targets where they are callee-saved. */
    emit_bytes(ctx, (const uint8_t*)"\x56\x57", 2);
    emit_move_reg_to_reg(ctx, KRT_REG_RCX, KRT_REG_R11);
    emit_reg_op(ctx, 0x09, KRT_REG_R11, KRT_REG_R8);
    uint32_t nonzero = emit_local_jump(ctx, 0x85);
    emit_bytes(ctx, (const uint8_t*)"\x0F\x0B", 2);
    patch_local_jump(ctx, nonzero, kro_get_code_offset(ctx->writer));
    if (!is_unsigned) {
        emit_move_reg_to_reg(ctx, KRT_REG_RDX, KRT_REG_R9);
        emit_shift_imm(ctx, KRT_REG_R9, 7, 63);
        emit_move_reg_to_reg(ctx, KRT_REG_R8, KRT_REG_R10);
        emit_shift_imm(ctx, KRT_REG_R10, 7, 63);
        emit_negate_pair_by_sign(ctx, KRT_REG_RAX, KRT_REG_RDX, KRT_REG_R9);
        emit_negate_pair_by_sign(ctx, KRT_REG_RCX, KRT_REG_R8, KRT_REG_R10);
        emit_reg_op(ctx, 0x31, KRT_REG_R10, KRT_REG_R9);
    }
    emit_load_imm64_to_reg(ctx, 0, KRT_REG_RSI);
    emit_load_imm64_to_reg(ctx, 0, KRT_REG_RDI);
    emit_load_imm64_to_reg(ctx, 128, KRT_REG_R11);
    uint32_t loop = kro_get_code_offset(ctx->writer);
    emit_bytes(ctx, (const uint8_t*)"\x48\xD1\xE0\x48\xD1\xD2\x48\xD1\xD6\x48\xD1\xD7", 12);
    uint32_t overflow = emit_local_jump(ctx, 0x82);
    emit_reg_op(ctx, 0x39, KRT_REG_RDI, KRT_REG_R8);
    uint32_t above = emit_local_jump(ctx, 0x87);
    uint32_t below = emit_local_jump(ctx, 0x82);
    emit_reg_op(ctx, 0x39, KRT_REG_RSI, KRT_REG_RCX);
    uint32_t low_below = emit_local_jump(ctx, 0x82);
    uint32_t subtract = kro_get_code_offset(ctx->writer);
    patch_local_jump(ctx, overflow, subtract);
    patch_local_jump(ctx, above, subtract);
    emit_reg_op(ctx, 0x29, KRT_REG_RSI, KRT_REG_RCX);
    emit_reg_op(ctx, 0x19, KRT_REG_RDI, KRT_REG_R8);
    emit_bytes(ctx, (const uint8_t*)"\x48\x83\xC8\x01", 4);
    uint32_t next = kro_get_code_offset(ctx->writer);
    patch_local_jump(ctx, below, next);
    patch_local_jump(ctx, low_below, next);
    emit_bytes(ctx, (const uint8_t*)"\x49\xFF\xCB", 3);
    uint32_t again = emit_local_jump(ctx, 0x85);
    patch_local_jump(ctx, again, loop);
    if (remainder) {
        emit_move_reg_to_reg(ctx, KRT_REG_RSI, KRT_REG_RAX);
        emit_move_reg_to_reg(ctx, KRT_REG_RDI, KRT_REG_RDX);
    }
    if (!is_unsigned) {
        emit_negate_pair_by_sign(ctx, KRT_REG_RAX, KRT_REG_RDX, remainder ? KRT_REG_R9 : KRT_REG_R10);
    }
    emit_bytes(ctx, (const uint8_t*)"\x5F\x5E", 2);
}

static void emit_integer_shift(KROCodegenContext* ctx, bool wide, bool right, bool is_unsigned) {
    emit_reg_op(ctx, 0x85, KRT_REG_R8, KRT_REG_R8);
    uint32_t high_count = emit_local_jump(ctx, 0x85);
    emit_bytes(ctx, (const uint8_t*)"\x48\x81\xF9", 3);
    emit_u32(ctx, wide ? 128 : 64);
    uint32_t outside = emit_local_jump(ctx, 0x83);
    uint32_t done_small = 0;
    if (wide) {
        emit_bytes(ctx, (const uint8_t*)"\x48\x83\xF9\x40", 4);
        uint32_t large = emit_local_jump(ctx, 0x83);
        if (right) {
            emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xAD\xD0", 4);
            emit_bytes(ctx, (const uint8_t*)(is_unsigned ? "\x48\xD3\xEA" : "\x48\xD3\xFA"), 3);
        } else {
            emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xA5\xC2\x48\xD3\xE0", 7);
        }
        done_small = emit_local_jump(ctx, 0);
        patch_local_jump(ctx, large, kro_get_code_offset(ctx->writer));
        if (right) {
            emit_move_reg_to_reg(ctx, KRT_REG_RDX, KRT_REG_RAX);
            emit_bytes(ctx, (const uint8_t*)(is_unsigned ? "\x48\xD3\xE8" : "\x48\xD3\xF8"), 3);
            if (is_unsigned) {
                emit_load_imm64_to_reg(ctx, 0, KRT_REG_RDX);
            } else {
                emit_shift_imm(ctx, KRT_REG_RDX, 7, 63);
            }
        } else {
            emit_move_reg_to_reg(ctx, KRT_REG_RAX, KRT_REG_RDX);
            emit_bytes(ctx, (const uint8_t*)"\x48\xD3\xE2", 3);
            emit_load_imm64_to_reg(ctx, 0, KRT_REG_RAX);
        }
    } else {
        emit_bytes(ctx, (const uint8_t*)(right ? (is_unsigned ? "\x48\xD3\xE8" : "\x48\xD3\xF8") : "\x48\xD3\xE0"), 3);
    }
    uint32_t done = emit_local_jump(ctx, 0);
    patch_local_jump(ctx, outside, kro_get_code_offset(ctx->writer));
    patch_local_jump(ctx, high_count, kro_get_code_offset(ctx->writer));
    if (right && !is_unsigned) {
        if (wide) {
            emit_move_reg_to_reg(ctx, KRT_REG_RDX, KRT_REG_RAX);
        }
        emit_shift_imm(ctx, KRT_REG_RAX, 7, 63);
        emit_move_reg_to_reg(ctx, KRT_REG_RAX, KRT_REG_RDX);
    } else {
        emit_load_imm64_to_reg(ctx, 0, KRT_REG_RAX);
        emit_load_imm64_to_reg(ctx, 0, KRT_REG_RDX);
    }
    uint32_t end = kro_get_code_offset(ctx->writer);
    patch_local_jump(ctx, done, end);
    if (done_small) {
        patch_local_jump(ctx, done_small, end);
    }
}

static void emit_reg_op32(KROCodegenContext* ctx, uint8_t opcode, int dst, int src) {
    uint8_t rex = 0x40 | (src >= 8 ? 4 : 0) | (dst >= 8 ? 1 : 0);
    if (rex != 0x40) {
        emit_byte(ctx, rex);
    }
    emit_byte(ctx, opcode);
    emit_byte(ctx, 0xC0 | ((src & 7) << 3) | (dst & 7));
}

static void emit_imm_op32(KROCodegenContext* ctx, int operation, int reg, uint32_t value) {
    bool small = value <= 127 || value >= UINT32_C(0xffffff80);
    if (reg >= 8) {
        emit_byte(ctx, 0x41);
    }
    emit_byte(ctx, small ? 0x83 : 0x81);
    emit_byte(ctx, 0xC0 | (operation << 3) | (reg & 7));
    if (small) {
        emit_byte(ctx, (uint8_t)value);
    } else {
        emit_u32(ctx, value);
    }
}

static int scalar_low_register(KROCodegenContext* ctx, KrtIRValue* value) {
    int width = KrtTokenIntegerBits(value->value_type);
    if (width <= 0 || width > 64) {
        return -1;
    }
    if (value->type == KRT_IR_VALUE_TEMP) {
        KROTempSlot* slot = find_temp_location(ctx, value->data.index);
        return slot ? slot->reg_low : -1;
    }
    KROPackedField* field = NULL;
    if (value->type == KRT_IR_VALUE_ARG && value->data.index >= 0 && value->data.index < ctx->current_param_count) {
        field = &ctx->arg_packed[value->data.index];
    } else if (value->type == KRT_IR_VALUE_VAR) {
        KROLocalVar* var = find_local_var(ctx, value->data.name);
        if (var) {
            field = &var->packed;
        }
    }
    return field && field->bit_width == width && !field->bit_offset ? field->reg : -1;
}

static int scalar_register32(KROCodegenContext* ctx, KrtIRValue* value) {
    return KrtTokenIntegerBits(value->value_type) == 32 ? scalar_low_register(ctx, value) : -1;
}

/* A 32-bit consumer ignores upper bits. Narrower operands still need their own
 * signed/unsigned extension before promotion; packed neighbours stay untouched. */
static void emit_load_scalar32(KROCodegenContext* ctx, KrtIRValue* value, int target) {
    int source = scalar_register32(ctx, value);
    if (source >= 0) {
        if (source != target) {
            emit_reg_op32(ctx, 0x89, target, source);
        }
    } else {
        emit_load_value_to_reg(ctx, value, target);
    }
}

static bool has_scalar32_operands(KrtIRInst* inst) {
    if (inst->operand_count != 2) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        int width = KrtTokenIntegerBits(inst->operands[i].value_type);
        if (width <= 0 || width > 32) {
            return false;
        }
    }
    return true;
}

static bool can_emit_binary32(KrtIRInst* inst) {
    if (KrtTokenIntegerBits(inst->result.value_type) != 32 || !has_scalar32_operands(inst)) {
        return false;
    }
    return inst->opcode == KRT_IR_ADD || inst->opcode == KRT_IR_SUB || inst->opcode == KRT_IR_MUL ||
           inst->opcode == KRT_IR_AND || inst->opcode == KRT_IR_OR || inst->opcode == KRT_IR_XOR;
}

static bool fits_signed_imm32(uint64_t value) {
    return value <= INT32_MAX || value >= UINT64_C(0xffffffff80000000);
}

static uint64_t scalar_integer_constant(KrtIRValue* value) {
    return (uint64_t)KrtIntegerNormalize(value->data.integer, KrtTokenIntegerBits(value->value_type),
                                         KrtTokenIsUnsigned(value->value_type));
}

static void emit_imm_op64(KROCodegenContext* ctx, int operation, int reg, uint64_t value) {
    bool small = value <= 127 || value >= UINT64_C(0xffffffffffffff80);
    emit_byte(ctx, 0x48 | (reg >= 8 ? 1 : 0));
    emit_byte(ctx, small ? 0x83 : 0x81);
    emit_byte(ctx, 0xC0 | (operation << 3) | (reg & 7));
    if (small) {
        emit_byte(ctx, (uint8_t)value);
    } else {
        emit_u32(ctx, (uint32_t)value);
    }
}

static int scalar_register64(KROCodegenContext* ctx, KrtIRValue* value) {
    return KrtTokenIntegerBits(value->value_type) == 64 ? scalar_low_register(ctx, value) : -1;
}

static bool scalar_binary_encoding(KrtIROpcode ir_opcode, int* operation, uint8_t* opcode) {
    switch (ir_opcode) {
    case KRT_IR_ADD:
        *operation = 0;
        *opcode = 0x01;
        return true;
    case KRT_IR_SUB:
        *operation = 5;
        *opcode = 0x29;
        return true;
    case KRT_IR_AND:
        *operation = 4;
        *opcode = 0x21;
        return true;
    case KRT_IR_OR:
        *operation = 1;
        *opcode = 0x09;
        return true;
    case KRT_IR_XOR:
        *operation = 6;
        *opcode = 0x31;
        return true;
    case KRT_IR_MUL:
        *operation = -1;
        *opcode = 0;
        return true;
    default:
        return false;
    }
}

static bool emit_binary64(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (ctx->optimization_level < 3 || KrtTokenIntegerBits(inst->result.value_type) != 64 || inst->operand_count != 2) {
        return false;
    }
    for (int i = 0; i < 2; i++) {
        int width = KrtTokenIntegerBits(inst->operands[i].value_type);
        if (width <= 0 || width > 64) {
            return false;
        }
    }
    int operation;
    uint8_t opcode;
    if (!scalar_binary_encoding(inst->opcode, &operation, &opcode)) {
        return false;
    }
    KrtIRValue* left = &inst->operands[0];
    KrtIRValue* right = &inst->operands[1];
    if (inst->opcode != KRT_IR_SUB && scalar_register64(ctx, right) == KRT_REG_RAX) {
        left = &inst->operands[1];
        right = &inst->operands[0];
    }
    bool immediate = right->type == KRT_IR_VALUE_INTEGER;
    uint64_t value = immediate ? scalar_integer_constant(right) : 0;
    uint64_t displacement = operation == 5 ? 0 - value : value;
    int source = scalar_register64(ctx, left);
    if (source >= 0 && immediate && (operation == 0 || operation == 5) && fits_signed_imm32(displacement)) {
        bool small = displacement <= 127 || displacement >= UINT64_C(0xffffffffffffff80);
        emit_byte(ctx, 0x48 | (source >= 8 ? 1 : 0));
        emit_byte(ctx, 0x8D);
        emit_byte(ctx, (small ? 0x40 : 0x80) | (source & 7));
        if ((source & 7) == 4) {
            emit_byte(ctx, 0x24);
        }
        if (small) {
            emit_byte(ctx, (uint8_t)displacement);
        } else {
            emit_u32(ctx, (uint32_t)displacement);
        }
    } else {
        emit_load_value_to_reg(ctx, left, KRT_REG_RAX);
        if (immediate && operation >= 0 && fits_signed_imm32(value)) {
            emit_imm_op64(ctx, operation, KRT_REG_RAX, value);
        } else {
            int reg = scalar_register64(ctx, right);
            if (reg < 0) {
                reg = KRT_REG_RCX;
                emit_load_value_to_reg(ctx, right, reg);
            }
            if (operation < 0) {
                emit_byte(ctx, 0x48 | (reg >= 8 ? 1 : 0));
                emit_bytes(ctx, (const uint8_t*)"\x0F\xAF", 2);
                emit_byte(ctx, 0xC0 | (reg & 7));
            } else {
                emit_reg_op(ctx, opcode, KRT_REG_RAX, reg);
            }
        }
    }
    emit_store_value(ctx, &inst->result);
    return true;
}

static bool emit_binary32(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (!can_emit_binary32(inst)) {
        return false;
    }
    int operation;
    uint8_t opcode;
    if (!scalar_binary_encoding(inst->opcode, &operation, &opcode)) {
        return false;
    }
    KrtIRValue* left = &inst->operands[0];
    KrtIRValue* right = &inst->operands[1];
    if (inst->opcode != KRT_IR_SUB && scalar_register32(ctx, right) == KRT_REG_RAX) {
        left = &inst->operands[1];
        right = &inst->operands[0];
    }
    int source = scalar_register32(ctx, left);
    bool immediate = right->type == KRT_IR_VALUE_INTEGER;
    uint32_t value = immediate ? (uint32_t)right->data.integer : 0;
    if (source >= 0 && immediate && (operation == 0 || operation == 5)) {
        /* LEA computes the low word directly, including defined integer wrap. */
        uint32_t displacement = operation == 5 ? 0u - value : value;
        bool small = displacement <= 127 || displacement >= UINT32_C(0xffffff80);
        if (source >= 8) {
            emit_byte(ctx, 0x41);
        }
        emit_byte(ctx, 0x8D);
        emit_byte(ctx, (small ? 0x40 : 0x80) | (source & 7));
        if ((source & 7) == 4) {
            emit_byte(ctx, 0x24);
        }
        if (small) {
            emit_byte(ctx, (uint8_t)displacement);
        } else {
            emit_u32(ctx, displacement);
        }
    } else {
        emit_load_scalar32(ctx, left, KRT_REG_RAX);
        if (immediate && operation >= 0) {
            emit_imm_op32(ctx, operation, KRT_REG_RAX, value);
        } else {
            int reg = scalar_register32(ctx, right);
            if (reg < 0) {
                reg = KRT_REG_RCX;
                emit_load_scalar32(ctx, right, reg);
            }
            if (operation < 0) {
                if (reg >= 8) {
                    emit_byte(ctx, 0x41);
                }
                emit_bytes(ctx, (const uint8_t*)"\x0F\xAF", 2);
                emit_byte(ctx, 0xC0 | (reg & 7));
            } else {
                emit_reg_op32(ctx, opcode, KRT_REG_RAX, reg);
            }
        }
    }
    emit_store_value(ctx, &inst->result);
    return true;
}

static void emit_scalar_compare_flags(KROCodegenContext* ctx, KrtIRInst* inst, KrtTokenType type) {
    int width = KrtTokenIntegerBits(type);
    if (ctx->optimization_level >= 3 && (width == 32 || width == 64) &&
        KrtTokenIntegerBits(inst->operands[0].value_type) == width &&
        KrtTokenIntegerBits(inst->operands[1].value_type) == width) {
        int left = scalar_low_register(ctx, &inst->operands[0]);
        int right = scalar_low_register(ctx, &inst->operands[1]);
        if (left >= 0 && right >= 0) {
            if (width == 32) {
                emit_reg_op32(ctx, 0x39, left, right);
            } else {
                emit_reg_op(ctx, 0x39, left, right);
            }
            return;
        }
    }
    if (ctx->optimization_level >= 3 && (width == 32 || width == 64) &&
        KrtTokenIntegerBits(inst->operands[0].value_type) == width && inst->operands[1].type == KRT_IR_VALUE_INTEGER) {
        int reg = scalar_low_register(ctx, &inst->operands[0]);
        uint64_t value = scalar_integer_constant(&inst->operands[1]);
        if (reg >= 0 && (width == 32 || fits_signed_imm32(value))) {
            if (width == 32) {
                emit_imm_op32(ctx, 7, reg, (uint32_t)value);
            } else {
                emit_imm_op64(ctx, 7, reg, value);
            }
            return;
        }
    }
    if (width == 32 && has_scalar32_operands(inst)) {
        emit_load_scalar32(ctx, &inst->operands[0], KRT_REG_RAX);
        if (inst->operands[1].type == KRT_IR_VALUE_INTEGER) {
            emit_imm_op32(ctx, 7, KRT_REG_RAX, (uint32_t)inst->operands[1].data.integer);
        } else {
            emit_load_scalar32(ctx, &inst->operands[1], KRT_REG_RCX);
            emit_reg_op32(ctx, 0x39, KRT_REG_RAX, KRT_REG_RCX);
        }
        return;
    }
    emit_load_for_type(ctx, &inst->operands[0], type, KRT_REG_RAX, KRT_REG_RDX);
    emit_extend_scalar(ctx, width, KrtTokenIsUnsigned(type), KRT_REG_RAX, KRT_REG_RAX);
    emit_load_for_type(ctx, &inst->operands[1], type, KRT_REG_RCX, KRT_REG_R8);
    emit_extend_scalar(ctx, width, KrtTokenIsUnsigned(type), KRT_REG_RCX, KRT_REG_RCX);
    emit_reg_op(ctx, 0x39, KRT_REG_RAX, KRT_REG_RCX);
}

static void emit_multiply_pair(KROCodegenContext* ctx) {
    emit_bytes(ctx, (const uint8_t*)"\x49\x89\xD1\x4C\x0F\xAF\xC9\x4C\x0F\xAF\xC0\x4D\x01\xC1\x48\xF7\xE1\x4C\x01\xCA",
               20);
}

static void emit_integer_binary(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (emit_binary32(ctx, inst) || emit_binary64(ctx, inst)) {
        return;
    }
    KrtTokenType type = inst->result.value_type;
    bool wide = KrtTokenIntegerBits(type) > 64;
    bool is_unsigned = KrtTokenIsUnsigned(type);
    bool shift = inst->opcode == KRT_IR_LSHIFT || inst->opcode == KRT_IR_RSHIFT;
    emit_load_for_type(ctx, &inst->operands[0], type, KRT_REG_RAX, KRT_REG_RDX);
    if (wide) {
        emit_normalize_pair(ctx, type, KRT_REG_RAX, KRT_REG_RDX);
    } else {
        emit_extend_scalar(ctx, KrtTokenIntegerBits(type), is_unsigned, KRT_REG_RAX, KRT_REG_RAX);
    }
    /* A shift count can itself be wider than the shifted value. */
    if (shift) {
        emit_load_pair(ctx, &inst->operands[1], KRT_REG_RCX, KRT_REG_R8);
    } else {
        emit_load_for_type(ctx, &inst->operands[1], type, KRT_REG_RCX, KRT_REG_R8);
        if (wide) {
            emit_normalize_pair(ctx, type, KRT_REG_RCX, KRT_REG_R8);
        } else {
            emit_extend_scalar(ctx, KrtTokenIntegerBits(type), is_unsigned, KRT_REG_RCX, KRT_REG_RCX);
        }
    }
    switch (inst->opcode) {
    case KRT_IR_ADD:
    case KRT_IR_SUB:
        emit_reg_op(ctx, inst->opcode == KRT_IR_ADD ? 0x01 : 0x29, KRT_REG_RAX, KRT_REG_RCX);
        if (wide) {
            emit_reg_op(ctx, inst->opcode == KRT_IR_ADD ? 0x11 : 0x19, KRT_REG_RDX, KRT_REG_R8);
        }
        break;
    case KRT_IR_MUL:
        if (wide) {
            emit_multiply_pair(ctx);
        } else {
            emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xAF\xC1", 4);
        }
        break;
    case KRT_IR_DIV:
    case KRT_IR_MOD:
        if (wide) {
            emit_wide_division(ctx, is_unsigned, inst->opcode == KRT_IR_MOD);
        } else {
            uint32_t overflow_done = 0;
            if (!is_unsigned) {
                emit_bytes(ctx, (const uint8_t*)"\x48\x83\xF9\xFF", 4);
                uint32_t ordinary_divisor = emit_local_jump(ctx, 0x85);
                emit_load_imm64_to_reg(ctx, UINT64_C(0x8000000000000000), KRT_REG_R9);
                emit_reg_op(ctx, 0x39, KRT_REG_RAX, KRT_REG_R9);
                uint32_t ordinary_dividend = emit_local_jump(ctx, 0x85);
                if (inst->opcode == KRT_IR_MOD) {
                    emit_load_imm64_to_reg(ctx, 0, KRT_REG_RAX);
                }
                overflow_done = emit_local_jump(ctx, 0);
                uint32_t ordinary = kro_get_code_offset(ctx->writer);
                patch_local_jump(ctx, ordinary_divisor, ordinary);
                patch_local_jump(ctx, ordinary_dividend, ordinary);
            }
            if (is_unsigned) {
                emit_load_imm64_to_reg(ctx, 0, KRT_REG_RDX);
            } else {
                emit_bytes(ctx, (const uint8_t*)"\x48\x99", 2);
            }
            emit_bytes(ctx, (const uint8_t*)(is_unsigned ? "\x48\xF7\xF1" : "\x48\xF7\xF9"), 3);
            if (inst->opcode == KRT_IR_MOD) {
                emit_move_reg_to_reg(ctx, KRT_REG_RDX, KRT_REG_RAX);
            }
            if (overflow_done) {
                patch_local_jump(ctx, overflow_done, kro_get_code_offset(ctx->writer));
            }
        }
        break;
    case KRT_IR_AND:
    case KRT_IR_OR:
    case KRT_IR_XOR: {
        uint8_t opcode = inst->opcode == KRT_IR_AND ? 0x21 : inst->opcode == KRT_IR_OR ? 0x09 : 0x31;
        emit_reg_op(ctx, opcode, KRT_REG_RAX, KRT_REG_RCX);
        if (wide) {
            emit_reg_op(ctx, opcode, KRT_REG_RDX, KRT_REG_R8);
        }
        break;
    }
    case KRT_IR_LSHIFT:
    case KRT_IR_RSHIFT:
        emit_integer_shift(ctx, wide, inst->opcode == KRT_IR_RSHIFT, is_unsigned);
        break;
    default:
        break;
    }
    emit_store_value(ctx, &inst->result);
}

static uint8_t integer_condition(KrtIROpcode op, bool is_unsigned) {
    switch (op) {
    case KRT_IR_LT:
        return is_unsigned ? 0x92 : 0x9C;
    case KRT_IR_GT:
        return is_unsigned ? 0x97 : 0x9F;
    case KRT_IR_LE:
        return is_unsigned ? 0x96 : 0x9E;
    case KRT_IR_GE:
        return is_unsigned ? 0x93 : 0x9D;
    case KRT_IR_EQ:
        return 0x94;
    default:
        return 0x95;
    }
}

static void emit_integer_compare(KROCodegenContext* ctx, KrtIRInst* inst) {
    KrtTokenType type = KrtTokenIntegerCommon(inst->operands[0].value_type, inst->operands[1].value_type);
    bool wide = KrtTokenIntegerBits(type) > 64;
    if (!wide) {
        emit_scalar_compare_flags(ctx, inst, type);
        emit_byte(ctx, 0x0F);
        emit_byte(ctx, integer_condition(inst->opcode, KrtTokenIsUnsigned(type)));
        emit_byte(ctx, 0xC0);
        emit_bytes(ctx, (const uint8_t*)"\x0F\xB6\xC0", 3);
        emit_store_value(ctx, &inst->result);
        return;
    }
    emit_load_for_type(ctx, &inst->operands[0], type, KRT_REG_RAX, KRT_REG_RDX);
    if (wide) {
        emit_normalize_pair(ctx, type, KRT_REG_RAX, KRT_REG_RDX);
    } else {
        emit_extend_scalar(ctx, KrtTokenIntegerBits(type), KrtTokenIsUnsigned(type), KRT_REG_RAX, KRT_REG_RAX);
    }
    emit_load_for_type(ctx, &inst->operands[1], type, KRT_REG_RCX, KRT_REG_R8);
    if (wide) {
        emit_normalize_pair(ctx, type, KRT_REG_RCX, KRT_REG_R8);
    } else {
        emit_extend_scalar(ctx, KrtTokenIntegerBits(type), KrtTokenIsUnsigned(type), KRT_REG_RCX, KRT_REG_RCX);
    }
    uint32_t high_diff = 0, low_done = 0;
    if (KrtTokenIntegerBits(type) > 64) {
        emit_reg_op(ctx, 0x39, KRT_REG_RDX, KRT_REG_R8);
        high_diff = emit_local_jump(ctx, 0x85);
        emit_reg_op(ctx, 0x39, KRT_REG_RAX, KRT_REG_RCX);
        emit_byte(ctx, 0x0F);
        emit_byte(ctx, integer_condition(inst->opcode, true));
        emit_byte(ctx, 0xC0);
        low_done = emit_local_jump(ctx, 0);
        patch_local_jump(ctx, high_diff, kro_get_code_offset(ctx->writer));
    } else {
        emit_reg_op(ctx, 0x39, KRT_REG_RAX, KRT_REG_RCX);
    }
    emit_byte(ctx, 0x0F);
    emit_byte(ctx, integer_condition(inst->opcode, KrtTokenIsUnsigned(type)));
    emit_byte(ctx, 0xC0);
    if (low_done) {
        patch_local_jump(ctx, low_done, kro_get_code_offset(ctx->writer));
    }
    emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xB6\xC0", 4);
    emit_store_value(ctx, &inst->result);
}
