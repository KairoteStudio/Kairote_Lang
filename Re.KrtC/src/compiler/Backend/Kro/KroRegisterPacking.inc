/* Each field keeps a fixed bit range for the entire function, including loops. */
static void allocate_packed_field(KROCodegenContext* ctx, KrtTokenType type, KROPackedField* field) {
    int bits = KrtTokenIntegerBits(type);
    int fragment = bits == 64 ? 64 : bits % 64;
    if (!fragment) {
        return;
    }

    int best = -1;
    for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
        if (ctx->packed_reg_bits[i] + fragment > 64) {
            continue;
        }
        if (best < 0 || ctx->packed_reg_bits[i] > ctx->packed_reg_bits[best]) {
            best = i;
        }
    }
    if (best < 0) {
        return;
    }

    field->reg = g_packed_regs[best];
    field->bit_offset = ctx->packed_reg_bits[best];
    field->bit_width = fragment;
    ctx->packed_reg_bits[best] += fragment;
}

static void emit_load_packed_field(KROCodegenContext* ctx, const KROPackedField* field, bool is_unsigned,
                                   int target_reg) {
    if (!field->bit_offset) {
        emit_extend_scalar(ctx, field->bit_width, is_unsigned, field->reg, target_reg);
        return;
    }
    emit_move_reg_to_reg(ctx, field->reg, target_reg);
    if (field->bit_width == 64) {
        return;
    }
    int left = 64 - field->bit_offset - field->bit_width;
    if (left) {
        emit_shift_imm(ctx, target_reg, 4, left);
    }
    emit_shift_imm(ctx, target_reg, is_unsigned ? 5 : 7, 64 - field->bit_width);
}

static void emit_store_packed_field(KROCodegenContext* ctx, const KROPackedField* field, int source_reg) {
    if (field->bit_width == 64) {
        emit_move_reg_to_reg(ctx, source_reg, field->reg);
        return;
    }
    if (!field->bit_offset) {
        for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
            if (g_packed_regs[i] == field->reg && ctx->packed_reg_bits[i] == field->bit_width) {
                emit_move_reg_to_reg(ctx, source_reg, field->reg);
                return;
            }
        }
    }
    uint64_t mask = ((UINT64_C(1) << field->bit_width) - 1) << field->bit_offset;
    /* R10/R11 are scratch registers; incoming arguments and RDX:RAX stay intact. */
    emit_move_reg_to_reg(ctx, source_reg, KRT_REG_R10);
    emit_shift_imm(ctx, KRT_REG_R10, 4, 64 - field->bit_width);
    emit_shift_imm(ctx, KRT_REG_R10, 5, 64 - field->bit_width);
    if (field->bit_offset) {
        emit_shift_imm(ctx, KRT_REG_R10, 4, field->bit_offset);
    }
    emit_load_imm64_to_reg(ctx, ~mask, KRT_REG_R11);
    emit_reg_op(ctx, 0x21, field->reg, KRT_REG_R11);
    emit_reg_op(ctx, 0x09, field->reg, KRT_REG_R10);
}

static void collect_local_storage(KROCodegenContext* ctx, KrtIRValue* value) {
    if (value->type != KRT_IR_VALUE_VAR || !value->data.name || kro_find_symbol(ctx->writer, value->data.name) >= 0) {
        return;
    }
    KROLocalVar* var = find_or_alloc_local_var(ctx, value->data.name);
    if (!var || value->value_type == TOKEN_EOF) {
        return;
    }
    if (var->type == TOKEN_EOF) {
        var->type = value->value_type;
    } else if (var->type != value->value_type) {
        var->type = TOKEN_VOID;
    }
}

static int allocate_value_stack(KROCodegenContext* ctx, KrtTokenType type, const KROPackedField* field) {
    if (field->bit_width && KrtTokenIntegerBits(type) <= 64) {
        return 0;
    }
    ctx->current_stack_offset += field->bit_width ? 8 : 16;
    return ctx->current_stack_offset;
}

static KROTempSlot* record_temporary(KROCodegenContext* ctx, KrtIRValue* value, KrtIRBasicBlock* block, int position,
                                     bool definition) {
    if (value->type != KRT_IR_VALUE_TEMP) {
        return NULL;
    }
    KROTempSlot* slot = create_temp_location(ctx, value->data.index);
    if (!slot) {
        return NULL;
    }
    if (!slot->block) {
        slot->block = block;
    } else if (slot->block != block) {
        slot->cross_block = 1;
    }
    int width = KrtTokenIntegerBits(value->value_type);
    if (!width) {
        width = 64;
    }
    if (width > slot->width) {
        slot->width = width;
    }
    if (definition) {
        if (slot->first_position >= 0) {
            slot->skip_register = 1;
        } else {
            slot->first_position = position;
        }
    } else {
        slot->last_position = position;
        slot->use_count++;
    }
    return slot;
}

static bool can_fuse_integer_branch(KROCodegenContext* ctx, KrtIRBasicBlock* block, KrtIRInst* inst) {
    KrtIRInst* branch = inst->next;
    if (inst->opcode < KRT_IR_LT || inst->opcode > KRT_IR_NE || inst->operand_count < 2 || !branch ||
        branch->opcode != KRT_IR_BRANCH || branch->operand_count < 1 || inst->result.type != KRT_IR_VALUE_TEMP ||
        branch->operands[0].type != KRT_IR_VALUE_TEMP || inst->result.data.index != branch->operands[0].data.index ||
        block->succ_count < 2) {
        return false;
    }
    KROTempSlot* slot = find_temp_location(ctx, inst->result.data.index);
    int width = KrtTokenIntegerBits(KrtTokenIntegerCommon(inst->operands[0].value_type, inst->operands[1].value_type));
    return slot && slot->use_count == 1 && width > 0 && width <= 64;
}

/* A single adjacent consumer can read a scratch register before it is clobbered.
 * Calls with several arguments, indirect calls and wide operations keep the
 * ordinary allocator: their argument setup can overwrite RAX/RCX. */
static int forwarded_result_register(KrtIRInst* inst) {
    KrtIRInst* next = inst->next;
    int width = KrtTokenIntegerBits(inst->result.value_type);
    if (!next || width <= 0 || width > 64) {
        return -1;
    }
    if (next->opcode == KRT_IR_RETURN && next->operand_count == 1 && next->operands[0].type == KRT_IR_VALUE_TEMP &&
        next->operands[0].data.index == inst->result.data.index) {
        return KRT_REG_RAX;
    }
    if (next->opcode == KRT_IR_CALL && next->operand_count == 2 && next->operands[0].type == KRT_IR_VALUE_FUNCTION &&
        next->operands[1].type == KRT_IR_VALUE_TEMP && next->operands[1].data.index == inst->result.data.index) {
        return KRT_REG_RAX;
    }
    bool binary = next->opcode == KRT_IR_ADD || next->opcode == KRT_IR_SUB || next->opcode == KRT_IR_MUL ||
                  next->opcode == KRT_IR_AND || next->opcode == KRT_IR_OR || next->opcode == KRT_IR_XOR ||
                  (next->opcode >= KRT_IR_LT && next->opcode <= KRT_IR_NE);
    if (!binary || next->operand_count != 2) {
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        int bits = KrtTokenIntegerBits(next->operands[i].value_type);
        if (bits <= 0 || bits > 64) {
            return -1;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (next->operands[i].type == KRT_IR_VALUE_TEMP && next->operands[i].data.index == inst->result.data.index) {
            if (i && next->opcode != KRT_IR_SUB && can_emit_binary32(next) &&
                KrtTokenIntegerBits(next->operands[i].value_type) == 32) {
                return KRT_REG_RAX;
            }
            return i ? KRT_REG_RCX : KRT_REG_RAX;
        }
    }
    return -1;
}

static void mark_low_word_operands(KROCodegenContext* ctx, KrtIRInst* inst) {
    int start = 0, end = 2;
    bool compare =
        inst->opcode >= KRT_IR_LT && inst->opcode <= KRT_IR_NE && has_scalar32_operands(inst) &&
        KrtTokenIntegerBits(KrtTokenIntegerCommon(inst->operands[0].value_type, inst->operands[1].value_type)) == 32;
    if (!compare && !can_emit_binary32(inst)) {
        if (inst->opcode != KRT_IR_CALL || inst->operand_count != 2 ||
            inst->operands[0].type != KRT_IR_VALUE_FUNCTION) {
            return;
        }
        start = 1;
    }
    for (int i = start; i < end; i++) {
        KrtIRValue* value = &inst->operands[i];
        if (value->type != KRT_IR_VALUE_TEMP || KrtTokenIntegerBits(value->value_type) != 32) {
            continue;
        }
        KROTempSlot* slot = find_temp_location(ctx, value->data.index);
        /* Other uses, redefinitions and promotions may observe the upper word. */
        if (slot && slot->use_count == 1 && slot->width == 32 && !slot->skip_register) {
            slot->low_word_only = true;
        }
    }
}

static void record_function_temporaries(KROCodegenContext* ctx, KrtIRFunction* func) {
    int position = 0;
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next, position++) {
            for (int i = 0; i < inst->operand_count; i++) {
                record_temporary(ctx, &inst->operands[i], block, position, false);
            }
            KROTempSlot* slot = record_temporary(ctx, &inst->result, block, position, true);
            if (slot && inst->opcode == KRT_IR_PHI) {
                slot->skip_register = 1;
            }
        }
    }
}

/* A full-width local or parameter owns its callee-saved register throughout the
 * function. Its copies can share that register until the next explicit write.
 * Addressable storage, packed neighbours, promotions and non-SSA temporaries
 * deliberately retain their snapshots. The backwards next-write scan is linear
 * in the instruction count, including blocks with many long-lived copies. */
static int full_storage_register(KROCodegenContext* ctx, KrtIRValue* value, KROLocalVar* const* locals) {
    KROPackedField* field = NULL;
    if (value->type == KRT_IR_VALUE_ARG && value->data.index >= 0 && value->data.index < ctx->current_param_count &&
        !ctx->arg_address_taken[value->data.index]) {
        field = &ctx->arg_packed[value->data.index];
    } else if (value->type == KRT_IR_VALUE_VAR) {
        for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
            if (locals[i] && value->data.name && strcmp(locals[i]->name, value->data.name) == 0) {
                field = &locals[i]->packed;
                break;
            }
        }
    }
    return field && field->bit_width == 64 && !field->bit_offset ? field->reg : -1;
}

static void coalesce_storage_copies(KROCodegenContext* ctx, KrtIRFunction* func) {
    if (ctx->optimization_level < 3) {
        return;
    }
    KROLocalVar* locals[KRT_KRO_PACKED_REG_COUNT] = {0};
    for (int i = 0; i < ctx->local_var_count; i++) {
        KROLocalVar* var = &ctx->local_vars[i];
        if (var->address_taken || var->packed.bit_width != 64 || var->packed.bit_offset) {
            continue;
        }
        for (int j = 0; j < KRT_KRO_PACKED_REG_COUNT; j++) {
            if (var->packed.reg == g_packed_regs[j]) {
                locals[j] = var;
            }
        }
    }
    int position = 0;
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        int next_write[16];
        for (int i = 0; i < 16; i++) {
            next_write[i] = -1;
        }
        position += block->inst_count;
        for (int index = block->inst_count - 1; index >= 0; index--) {
            KrtIRInst* inst = block->insts[index];
            int here = position - block->inst_count + index;
            int written = full_storage_register(ctx, &inst->result, locals);
            if (written >= 0) {
                next_write[written] = here;
            }
            if ((inst->opcode != KRT_IR_COPY && inst->opcode != KRT_IR_CAST) || inst->operand_count < 1 ||
                inst->result.type != KRT_IR_VALUE_TEMP || KrtTokenIntegerBits(inst->result.value_type) != 64 ||
                inst->operands[0].value_type != inst->result.value_type) {
                continue;
            }
            int source = full_storage_register(ctx, &inst->operands[0], locals);
            if (source < 0) {
                continue;
            }
            KROTempSlot* slot = find_temp_location(ctx, inst->result.data.index);
            if (!slot || slot->no_storage || slot->cross_block || slot->skip_register || slot->width != 64 ||
                slot->first_position != here || slot->last_position < here ||
                (next_write[source] >= 0 && next_write[source] < slot->last_position)) {
                continue;
            }
            slot->reg_low = source;
            slot->no_storage = true;
        }
    }
}

static bool is_coalesced_register_copy(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (inst->result.type != KRT_IR_VALUE_TEMP) {
        return false;
    }
    KROTempSlot* slot = find_temp_location(ctx, inst->result.data.index);
    return slot && slot->no_storage && slot->reg_low >= 0;
}

#define KRT_KRO_ACYCLIC_BLOCK_LIMIT 256
#define KRT_KRO_DOMINATOR_WORDS (KRT_KRO_ACYCLIC_BLOCK_LIMIT / 64)

static int storage_block_index(KrtIRBasicBlock* const* blocks, int count, KrtIRBasicBlock* target) {
    for (int i = 0; i < count; i++) {
        if (blocks[i] == target) {
            return i;
        }
    }
    return -1;
}

/* Preserve a temporary across forward CFG edges only after proving that its
 * sole definition dominates every use. Dominators follow the effective machine
 * edges, including implicit fallthrough, rather than trusting predecessor
 * metadata for instructions after an early return. Explicit loops and large
 * graphs retain the block-local allocator. */
static bool plan_cross_block_temporaries(KROCodegenContext* ctx, KrtIRFunction* func, bool* eligible) {
    if (ctx->optimization_level < 3) {
        return false;
    }
    bool candidates = false;
    for (int i = 0; i < ctx->temp_slot_count; i++) {
        KROTempSlot* slot = &ctx->temp_slots[i];
        eligible[i] = slot->cross_block && !slot->skip_register && !slot->no_storage && slot->first_position >= 0 &&
                      slot->last_position > slot->first_position;
        candidates |= eligible[i];
    }
    if (!candidates) {
        return false;
    }
    KrtIRBasicBlock* blocks[KRT_KRO_ACYCLIC_BLOCK_LIMIT];
    int starts[KRT_KRO_ACYCLIC_BLOCK_LIMIT], count = 0, position = 0;
    uint64_t dominators[KRT_KRO_ACYCLIC_BLOCK_LIMIT][KRT_KRO_DOMINATOR_WORDS] = {{0}};
    bool has_predecessor[KRT_KRO_ACYCLIC_BLOCK_LIMIT] = {false};
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        if (count == KRT_KRO_ACYCLIC_BLOCK_LIMIT) {
            return false;
        }
        blocks[count] = block;
        starts[count++] = position;
        position += block->inst_count;
    }
    if (!count) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        dominators[i][i / 64] |= UINT64_C(1) << (i % 64);
        KrtIRInst* terminator = blocks[i]->first_inst;
        while (terminator && terminator->opcode != KRT_IR_RETURN && terminator->opcode != KRT_IR_JUMP &&
               terminator->opcode != KRT_IR_BRANCH) {
            terminator = terminator->next;
        }
        KrtIRBasicBlock* successors[2];
        int successor_count = 0;
        if (terminator && terminator->opcode == KRT_IR_BRANCH) {
            if (terminator->operand_count < 1 || blocks[i]->succ_count != 2) {
                return false;
            }
            successors[successor_count++] = blocks[i]->succs[0];
            successors[successor_count++] = blocks[i]->succs[1];
        } else if (terminator && terminator->opcode == KRT_IR_JUMP && blocks[i]->succ_count) {
            if (blocks[i]->succ_count != 1) {
                return false;
            }
            successors[successor_count++] = blocks[i]->succs[0];
        } else if ((!terminator || terminator->opcode != KRT_IR_RETURN) && i + 1 < count) {
            successors[successor_count++] = blocks[i + 1];
        }
        for (int edge = 0; edge < successor_count; edge++) {
            int target = storage_block_index(blocks, count, successors[edge]);
            if (target <= i) {
                return false;
            }
            for (int word = 0; word < KRT_KRO_DOMINATOR_WORDS; word++) {
                if (has_predecessor[target]) {
                    dominators[target][word] &= dominators[i][word];
                } else {
                    dominators[target][word] = dominators[i][word];
                }
            }
            has_predecessor[target] = true;
        }
    }
    int definition_block[KRT_KRO_MAX_TEMP_REGS];
    for (int i = 0; i < ctx->temp_slot_count; i++) {
        if (!eligible[i]) {
            continue;
        }
        KROTempSlot* slot = &ctx->temp_slots[i];
        int low = 0, high = count;
        while (low + 1 < high) {
            int middle = low + (high - low) / 2;
            if (starts[middle] <= slot->first_position) {
                low = middle;
            } else {
                high = middle;
            }
        }
        int offset = slot->first_position - starts[low];
        if (offset < 0 || offset >= blocks[low]->inst_count ||
            !KrtTokenIntegerBits(blocks[low]->insts[offset]->result.value_type) || (ctx->tail_site_count && low == 0)) {
            eligible[i] = false;
            continue;
        }
        definition_block[i] = low;
    }
    position = 0;
    for (int block = 0; block < count; block++) {
        for (KrtIRInst* inst = blocks[block]->first_inst; inst; inst = inst->next, position++) {
            for (int operand = 0; operand < inst->operand_count; operand++) {
                KrtIRValue* value = &inst->operands[operand];
                if (value->type != KRT_IR_VALUE_TEMP) {
                    continue;
                }
                KROTempSlot* slot = find_temp_location(ctx, value->data.index);
                if (!slot) {
                    continue;
                }
                int index = (int)(slot - ctx->temp_slots);
                if (!eligible[index]) {
                    continue;
                }
                int definition = definition_block[index];
                if (slot->first_position >= position ||
                    !(dominators[block][definition / 64] & (UINT64_C(1) << (definition % 64)))) {
                    eligible[index] = false;
                }
            }
        }
    }
    /* TRE adds backedges after this analysis. It re-executes each dominating
     * definition on the next iteration; entry definitions are excluded because
     * the guarded tail-loop rotation can bypass that block. Parameter updates
     * use permanently reserved registers, outside the temporary owner set. */
    for (int i = 0; i < ctx->temp_slot_count; i++) {
        if (eligible[i]) {
            return true;
        }
    }
    return false;
}

/* Block-local SSA values reuse registers. Proven acyclic cross-block values use
 * the same intervals, retaining owners across blocks until their last use. */
static void allocate_temporary_registers(KROCodegenContext* ctx, KrtIRFunction* func) {
    unsigned permanent = 0;
    for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
        if (ctx->packed_reg_bits[i]) {
            permanent |= 1u << i;
        }
    }
    bool cross_block[KRT_KRO_MAX_TEMP_REGS];
    bool global_intervals = plan_cross_block_temporaries(ctx, func, cross_block);
    KROTempSlot* owners[KRT_KRO_PACKED_REG_COUNT] = {0};
    int position = 0;
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        if (!global_intervals) {
            memset(owners, 0, sizeof(owners));
        }
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next, position++) {
            mark_low_word_operands(ctx, inst);
            for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
                if (owners[i] && owners[i]->last_position <= position) {
                    owners[i] = NULL;
                }
            }
            if (inst->result.type != KRT_IR_VALUE_TEMP) {
                continue;
            }
            KROTempSlot* slot = find_temp_location(ctx, inst->result.data.index);
            if (!slot || slot->no_storage || slot->skip_register ||
                (slot->cross_block && (!global_intervals || !cross_block[slot - ctx->temp_slots])) ||
                slot->first_position != position || slot->last_position < position) {
                continue;
            }
            if (can_fuse_integer_branch(ctx, block, inst)) {
                slot->no_storage = true;
                continue;
            }
            if (!slot->cross_block && slot->use_count == 1 && slot->width <= 64) {
                int reg = forwarded_result_register(inst);
                if (reg >= 0) {
                    slot->reg_low = reg;
                    continue;
                }
            }
            int available[2], count = 0, needed = slot->width > 64 ? 2 : 1;
            for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT && count < needed; i++) {
                if (!(permanent & (1u << i)) && !owners[i]) {
                    available[count++] = i;
                }
            }
            if (count < needed) {
                continue;
            }
            slot->reg_low = g_packed_regs[available[0]];
            if (needed == 2) {
                slot->reg_high = g_packed_regs[available[1]];
            }
            for (int i = 0; i < needed; i++) {
                owners[available[i]] = slot;
                ctx->packed_reg_bits[available[i]] = 64;
            }
        }
    }
}

static void plan_tail_register_updates(KROCodegenContext* ctx, KrtIRFunction* func);

static void plan_function_storage(KROCodegenContext* ctx, KrtIRFunction* func) {
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            collect_local_storage(ctx, &inst->result);
            for (int i = 0; i < inst->operand_count; i++) {
                collect_local_storage(ctx, &inst->operands[i]);
            }
        }
    }

    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->opcode != KRT_IR_ADDRESS_OF || !inst->operand_count) {
                continue;
            }
            KrtIRValue* storage = &inst->operands[0];
            if (storage->type == KRT_IR_VALUE_VAR) {
                KROLocalVar* var = find_local_var(ctx, storage->data.name);
                if (var) {
                    var->address_taken = true;
                }
            } else if (storage->type == KRT_IR_VALUE_ARG && storage->data.index >= 0 &&
                       storage->data.index < KRT_KRO_MAX_ARGS) {
                ctx->arg_address_taken[storage->data.index] = true;
            }
        }
    }
    record_function_temporaries(ctx, func);
    plan_tail_calls(ctx, func);
    for (int i = 0; i < ctx->current_param_count; i++) {
        if (!ctx->arg_address_taken[i] && !func->params[i].is_array) {
            allocate_packed_field(ctx, func->params[i].type, &ctx->arg_packed[i]);
        }
    }
    for (int i = 0; i < ctx->local_var_count; i++) {
        KROLocalVar* var = &ctx->local_vars[i];
        if (!var->address_taken) {
            allocate_packed_field(ctx, var->type, &var->packed);
        }
        if (var->packed.bit_width) {
            for (int j = 0; j < KRT_KRO_PACKED_REG_COUNT; j++) {
                if (var->packed.reg == g_packed_regs[j]) {
                    ctx->packed_reg_needs_zero[j] = true;
                }
            }
        }
    }

    plan_tail_register_updates(ctx, func);
    coalesce_storage_copies(ctx, func);
    allocate_temporary_registers(ctx, func);
    ctx->current_stack_offset = 0;
    for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
        if (!ctx->packed_reg_bits[i]) {
            continue;
        }
        ctx->current_stack_offset += 8;
        ctx->packed_reg_save_offsets[i] = ctx->current_stack_offset;
    }
    for (int i = 0; i < ctx->current_param_count; i++) {
        ctx->arg_stack_offsets[i] = allocate_value_stack(ctx, func->params[i].type, &ctx->arg_packed[i]);
    }
    for (int i = 0; i < ctx->local_var_count; i++) {
        KROLocalVar* var = &ctx->local_vars[i];
        var->stack_offset = allocate_value_stack(ctx, var->type, &var->packed);
    }
    for (int i = 0; i < ctx->temp_slot_count; i++) {
        KROTempSlot* slot = &ctx->temp_slots[i];
        if (slot->reg_low < 0 && slot->use_count && !slot->no_storage) {
            ctx->current_stack_offset += 16;
            slot->stack_offset = ctx->current_stack_offset;
        }
    }
    if (ctx->tail_site_count && !ctx->tail_register_argument) {
        for (int i = 0; i < ctx->current_param_count; i++) {
            ctx->current_stack_offset += 16;
            ctx->tail_arg_offsets[i] = ctx->current_stack_offset;
        }
    }
}

static void emit_store_parameter_word(KROCodegenContext* ctx, KrtIRFunction* func, int index, int word,
                                      int source_reg) {
    if (func->params[index].type == TOKEN_FLOAT32) {
        emit_float32_conversion(ctx, source_reg, true);
    }
    KROPackedField* field = &ctx->arg_packed[index];
    int packed_word = KrtTokenIntegerBits(func->params[index].type) > 64 ? 1 : 0;
    if (field->bit_width && word == packed_word) {
        emit_store_packed_field(ctx, field, source_reg);
    } else {
        emit_store_to_stack(ctx, ctx->arg_stack_offsets[index] - word * 8, source_reg);
    }
}
