/* Bounded self-recursive expansion. A return continuation is copied into each
 * exit, keeping the caller's final call/combine/return in one basic block. This
 * avoids PHIs and preserves the backend's accumulator tail-call conversion.
 * Frame-observable operations and nonlocal effects are deliberately excluded. */
#define KRT_RECURSIVE_BODY_LIMIT 48
#define KRT_RECURSIVE_BLOCK_LIMIT 12
#define KRT_RECURSIVE_GROWTH_LIMIT 384
#define KRT_RECURSIVE_DEPTH_LIMIT 3
#define KRT_RECURSIVE_PARAM_LIMIT 8

typedef struct {
    int temp_id;
    KrtIRValue argument;
    KrtUInt128 offset;
    KrtIRBasicBlock* block;
} KrtRecursiveAffine;

typedef struct {
    int old_id;
    KrtIRValue value;
} KrtRecursiveValue;

typedef struct {
    KrtIRValue arguments[KRT_RECURSIVE_PARAM_LIMIT];
    KrtRecursiveValue values[KRT_RECURSIVE_BODY_LIMIT];
    int count;
} KrtRecursiveMap;

typedef struct KrtRecursiveContinuation {
    KrtIRInst* first;
    KrtRecursiveMap map;
    int result_id;
    int depth;
    const struct KrtRecursiveContinuation* next;
} KrtRecursiveContinuation;

typedef struct {
    KrtIRFunction* function;
    KrtIRBasicBlock* first;
    KrtIRBasicBlock* last;
    int next_temp;
    int next_block;
    int instruction_count;
    int inline_count;
    int cse_count;
    bool failed;
    KrtRecursiveAffine affine[KRT_RECURSIVE_GROWTH_LIMIT];
    int affine_count;
} KrtRecursiveExpansion;

static void recursive_destroy_blocks(KrtIRBasicBlock* block) {
    while (block) {
        KrtIRBasicBlock* next = block->next;
        KrtIRInst* inst = block->first_inst;
        while (inst) {
            KrtIRInst* following = inst->next;
            KRT_FREE(inst->operands);
            KRT_FREE(inst);
            inst = following;
        }
        KRT_FREE(block->label);
        KRT_FREE(block->insts);
        KRT_FREE(block->preds);
        KRT_FREE(block->succs);
        KRT_FREE(block->phi_list);
        KRT_FREE(block);
        block = next;
    }
}

static bool recursive_self_call(KrtIRFunction* function, KrtIRInst* inst) {
    return inst->opcode == KRT_IR_CALL && inst->operand_count == function->param_count + 1 &&
           inst->operands[0].type == KRT_IR_VALUE_FUNCTION && inst->operands[0].data.function_name &&
           !strcmp(inst->operands[0].data.function_name, function->name) && inst->result.type == KRT_IR_VALUE_TEMP &&
           inst->result.value_type == function->return_type;
}

/* Every reachable path must terminate without loops or cross-block SSA uses.
 * Copies introduced by earlier optimizations are accepted; named slots are not. */
static bool recursive_check_block(KrtIRFunction* function, KrtIRBasicBlock* block, KrtIRBasicBlock** path, int depth,
                                  int* calls, int* size) {
    if (!block || depth == KRT_RECURSIVE_BLOCK_LIMIT) {
        return false;
    }
    for (int i = 0; i < depth; i++) {
        if (path[i] == block) {
            return false;
        }
    }
    path[depth] = block;
    int ids[KRT_RECURSIVE_BODY_LIMIT], count = 0;
    for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
        if (++*size > KRT_RECURSIVE_BODY_LIMIT) {
            return false;
        }
        bool call = recursive_self_call(function, inst);
        bool terminal = inst->opcode == KRT_IR_RETURN || inst->opcode == KRT_IR_JUMP || inst->opcode == KRT_IR_BRANCH;
        if (!call && !terminal && !is_pure_operation(inst->opcode) && inst->opcode != KRT_IR_COPY &&
            inst->opcode != KRT_IR_CAST) {
            return false;
        }
        if (!terminal && (inst->result.type != KRT_IR_VALUE_TEMP || !inline_scalar(inst->result))) {
            return false;
        }
        if (call) {
            (*calls)++;
            for (int i = 0; i < function->param_count; i++) {
                if (inst->operands[i + 1].value_type != function->params[i].type) {
                    return false;
                }
            }
        }
        for (int i = 0; i < inst->operand_count; i++) {
            if (call && !i) {
                continue;
            }
            KrtIRValue value = inst->operands[i];
            if (inst->opcode == KRT_IR_CAST && i == 1) {
                if (value.type != KRT_IR_VALUE_IMM || value.data.imm != (double)inst->result.value_type) {
                    return false;
                }
                continue;
            }
            if (!inline_scalar(value)) {
                return false;
            }
            if (value.type == KRT_IR_VALUE_ARG) {
                if (value.data.index < 0 || value.data.index >= function->param_count) {
                    return false;
                }
            } else if (value.type == KRT_IR_VALUE_TEMP) {
                bool found = false;
                for (int j = 0; j < count; j++) {
                    if (ids[j] == value.data.index) {
                        found = true;
                    }
                }
                if (!found) {
                    return false;
                }
            } else if (value.type != KRT_IR_VALUE_INTEGER && value.type != KRT_IR_VALUE_IMM) {
                return false;
            }
        }
        if (terminal) {
            if (inst->opcode == KRT_IR_RETURN) {
                return inst->operand_count == 1 && inst->operands[0].value_type == function->return_type;
            }
            int successors = inst->opcode == KRT_IR_BRANCH ? 2 : 1;
            if (block->succ_count != successors || inst->operand_count != successors - 1) {
                return false;
            }
            for (int i = 0; i < successors; i++) {
                if (!recursive_check_block(function, block->succs[i], path, depth + 1, calls, size)) {
                    return false;
                }
            }
            return true;
        }
        if (count == KRT_RECURSIVE_BODY_LIMIT) {
            return false;
        }
        for (int i = 0; i < count; i++) {
            if (ids[i] == inst->result.data.index) {
                return false;
            }
        }
        ids[count++] = inst->result.data.index;
    }
    return false;
}

static KrtIRValue recursive_value(KrtRecursiveMap* map, KrtIRValue value) {
    if (value.type == KRT_IR_VALUE_ARG) {
        return map->arguments[value.data.index];
    }
    if (value.type == KRT_IR_VALUE_TEMP) {
        for (int i = map->count - 1; i >= 0; i--) {
            if (map->values[i].old_id == value.data.index) {
                return map->values[i].value;
            }
        }
    }
    return value;
}

static bool recursive_map_result(KrtRecursiveExpansion* expansion, KrtRecursiveMap* map, int old_id, KrtIRValue value) {
    if (map->count == KRT_RECURSIVE_BODY_LIMIT) {
        expansion->failed = true;
        return false;
    }
    map->values[map->count++] = (KrtRecursiveValue){old_id, value};
    return true;
}

static KrtIRBasicBlock* recursive_new_block(KrtRecursiveExpansion* expansion) {
    if (expansion->failed || expansion->next_block == INT_MAX) {
        expansion->failed = true;
        return NULL;
    }
    KrtIRBasicBlock* block = KRT_CALLOC(1, sizeof(*block));
    if (!block) {
        expansion->failed = true;
        return NULL;
    }
    block->id = ++expansion->next_block;
    char label[48];
    snprintf(label, sizeof(label), "recursive_inline_%d", block->id);
    block->label = KRT_STRDUP(label);
    block->inst_capacity = KRT_RECURSIVE_GROWTH_LIMIT;
    block->insts = KRT_MALLOC((size_t)block->inst_capacity * sizeof(*block->insts));
    block->pred_capacity = 1;
    block->preds = KRT_MALLOC(sizeof(*block->preds));
    block->succ_capacity = 2;
    block->succs = KRT_MALLOC(2 * sizeof(*block->succs));
    if (expansion->last) {
        expansion->last->next = block;
    } else {
        expansion->first = block;
    }
    expansion->last = block;
    if (!block->label || !block->insts || !block->preds || !block->succs) {
        expansion->failed = true;
    }
    return expansion->failed ? NULL : block;
}

/* Cloned scalar arguments often cross a newly introduced branch. Preserve them
 * as same-width ARG + constant expressions instead of creating live stack slots.
 * Modular arithmetic makes this valid for signed and unsigned integers alike. */
static bool recursive_affine_value(KrtRecursiveExpansion* expansion, KrtIRValue value, KrtRecursiveAffine* affine) {
    if (value.type == KRT_IR_VALUE_ARG && KrtTokenIntegerBits(value.value_type)) {
        *affine = (KrtRecursiveAffine){-1, value, 0, NULL};
        return true;
    }
    if (value.type == KRT_IR_VALUE_TEMP) {
        for (int i = expansion->affine_count - 1; i >= 0; i--) {
            if (expansion->affine[i].temp_id == value.data.index) {
                *affine = expansion->affine[i];
                return true;
            }
        }
    }
    return false;
}

static bool recursive_affine_result(KrtRecursiveExpansion* expansion, KrtIRInst* inst, KrtRecursiveAffine* affine) {
    int bits = KrtTokenIntegerBits(inst->result.value_type);
    if (!bits || inst->result.type != KRT_IR_VALUE_TEMP || inst->operand_count < 1) {
        return false;
    }
    if (inst->opcode == KRT_IR_COPY || inst->opcode == KRT_IR_CAST) {
        if (!recursive_affine_value(expansion, inst->operands[0], affine)) {
            return false;
        }
    } else if ((inst->opcode == KRT_IR_ADD || inst->opcode == KRT_IR_SUB) && inst->operand_count == 2) {
        int constant = inst->operands[1].type == KRT_IR_VALUE_INTEGER
                           ? 1
                           : (inst->opcode == KRT_IR_ADD && inst->operands[0].type == KRT_IR_VALUE_INTEGER ? 0 : -1);
        if (constant < 0 || !recursive_affine_value(expansion, inst->operands[1 - constant], affine)) {
            return false;
        }
        KrtIRValue immediate = inst->operands[constant];
        int immediate_bits = KrtTokenIntegerBits(immediate.value_type);
        if (!immediate_bits) {
            return false;
        }
        KrtUInt128 offset = immediate.data.integer;
        if (immediate_bits < 128) {
            KrtUInt128 mask = ((KrtUInt128)1 << immediate_bits) - 1;
            offset &= mask;
            if (!KrtTokenIsUnsigned(immediate.value_type) && (offset & ((KrtUInt128)1 << (immediate_bits - 1)))) {
                offset |= ~mask;
            }
        }
        affine->offset = inst->opcode == KRT_IR_SUB ? affine->offset - offset : affine->offset + offset;
    } else {
        return false;
    }
    if (affine->argument.value_type != inst->result.value_type) {
        return false;
    }
    if (bits < 128) {
        affine->offset &= ((KrtUInt128)1 << bits) - 1;
    }
    return true;
}

static bool recursive_set_affine_inst(KrtIRInst* inst, KrtRecursiveAffine affine) {
    if (inst->operand_capacity < 2) {
        KrtIRValue* grown = KRT_REALLOC(inst->operands, 2 * sizeof(*grown));
        if (!grown) {
            return false;
        }
        inst->operands = grown;
        inst->operand_capacity = 2;
    }
    inst->opcode = affine.offset ? KRT_IR_ADD : KRT_IR_COPY;
    inst->operand_count = affine.offset ? 2 : 1;
    inst->operands[0] = affine.argument;
    inst->operands[1] = (KrtIRValue){
        .type = KRT_IR_VALUE_INTEGER, .value_type = affine.argument.value_type, .data.integer = affine.offset};
    return true;
}

static void recursive_append_inst(KrtIRBasicBlock* block, KrtIRInst* inst) {
    if (block->last_inst) {
        block->last_inst->next = inst;
    } else {
        block->first_inst = inst;
    }
    block->last_inst = inst;
    block->insts[block->inst_count++] = inst;
}

static KrtIRInst* recursive_copy_inst(KrtRecursiveExpansion* expansion, KrtIRBasicBlock* block, KrtIRInst* source,
                                      KrtRecursiveMap* map) {
    if (expansion->failed || ++expansion->instruction_count > KRT_RECURSIVE_GROWTH_LIMIT ||
        expansion->next_temp == INT_MAX) {
        expansion->failed = true;
        return NULL;
    }
    KrtIRInst* copy = KRT_CALLOC(1, sizeof(*copy));
    if (!copy) {
        expansion->failed = true;
        return NULL;
    }
    *copy = *source;
    copy->next = NULL;
    copy->operands = NULL;
    if (copy->operand_count) {
        copy->operands = KRT_MALLOC((size_t)copy->operand_count * sizeof(*copy->operands));
        if (!copy->operands) {
            KRT_FREE(copy);
            expansion->failed = true;
            return NULL;
        }
        for (int i = 0; i < copy->operand_count; i++) {
            copy->operands[i] = recursive_value(map, source->operands[i]);
        }
    }
    copy->operand_capacity = copy->operand_count;
    if (copy->result.type == KRT_IR_VALUE_TEMP) {
        copy->result.data.index = ++expansion->next_temp;
        recursive_map_result(expansion, map, source->result.data.index, copy->result);
    }
    KrtRecursiveAffine affine;
    if (recursive_affine_result(expansion, copy, &affine)) {
        if (!recursive_set_affine_inst(copy, affine)) {
            expansion->failed = true;
        }
        affine.temp_id = copy->result.data.index;
        affine.block = block;
        expansion->affine[expansion->affine_count++] = affine;
    } else {
        for (int i = 0; i < copy->operand_count && !expansion->failed; i++) {
            if (copy->operands[i].type != KRT_IR_VALUE_TEMP ||
                !recursive_affine_value(expansion, copy->operands[i], &affine) || affine.block == block) {
                continue;
            }
            if (++expansion->instruction_count >= KRT_RECURSIVE_GROWTH_LIMIT || expansion->next_temp == INT_MAX) {
                expansion->failed = true;
                break;
            }
            KrtIRInst* rematerialized = KRT_CALLOC(1, sizeof(*rematerialized));
            if (!rematerialized) {
                expansion->failed = true;
                break;
            }
            rematerialized->result = copy->operands[i];
            rematerialized->result.data.index = ++expansion->next_temp;
            rematerialized->is_int_result = true;
            if (!recursive_set_affine_inst(rematerialized, affine)) {
                KRT_FREE(rematerialized);
                expansion->failed = true;
                break;
            }
            copy->operands[i] = rematerialized->result;
            recursive_append_inst(block, rematerialized);
        }
    }
    recursive_append_inst(block, copy);
    return expansion->failed ? NULL : copy;
}

static bool recursive_has_later_call(KrtIRFunction* function, KrtIRInst* inst) {
    for (inst = inst->next; inst; inst = inst->next) {
        if (recursive_self_call(function, inst)) {
            return true;
        }
        if (inst->opcode == KRT_IR_RETURN || inst->opcode == KRT_IR_JUMP || inst->opcode == KRT_IR_BRANCH) {
            break;
        }
    }
    return false;
}

static void recursive_emit_path(KrtRecursiveExpansion* expansion, KrtIRBasicBlock* output,
                                KrtIRBasicBlock* source_block, KrtIRInst* first, KrtRecursiveMap map, int depth,
                                const KrtRecursiveContinuation* continuation) {
    for (KrtIRInst* inst = first; inst && !expansion->failed; inst = inst->next) {
        if (inst->opcode == KRT_IR_RETURN && continuation) {
            KrtRecursiveMap resumed = continuation->map;
            if (!recursive_map_result(expansion, &resumed, continuation->result_id,
                                      recursive_value(&map, inst->operands[0]))) {
                return;
            }
            recursive_emit_path(expansion, output, NULL, continuation->first, resumed, continuation->depth,
                                continuation->next);
            return;
        }
        if (depth && recursive_self_call(expansion->function, inst) &&
            recursive_has_later_call(expansion->function, inst)) {
            KrtRecursiveMap arguments = {0};
            for (int i = 0; i < expansion->function->param_count; i++) {
                arguments.arguments[i] = recursive_value(&map, inst->operands[i + 1]);
            }
            KrtRecursiveContinuation next = {inst->next, map, inst->result.data.index, depth, continuation};
            expansion->inline_count++;
            recursive_emit_path(expansion, output, expansion->function->entry_block,
                                expansion->function->entry_block->first_inst, arguments, depth - 1, &next);
            return;
        }
        if (!recursive_copy_inst(expansion, output, inst, &map)) {
            return;
        }
        if (inst->opcode == KRT_IR_RETURN) {
            return;
        }
        if (inst->opcode == KRT_IR_JUMP || inst->opcode == KRT_IR_BRANCH) {
            if (!source_block) {
                expansion->failed = true;
                return;
            }
            int count = inst->opcode == KRT_IR_BRANCH ? 2 : 1;
            for (int i = 0; i < count; i++) {
                KrtIRBasicBlock* child = recursive_new_block(expansion);
                if (!child) {
                    return;
                }
                output->succs[output->succ_count++] = child;
                child->preds[child->pred_count++] = output;
                KrtRecursiveMap child_map = map;
                child_map.count = 0;
                recursive_emit_path(expansion, child, source_block->succs[i], source_block->succs[i]->first_inst,
                                    child_map, depth, continuation);
            }
            return;
        }
    }
}

static bool inline_recursive_functions(KrtIRModule* module, IROptimizer* stats) {
    if (!module) {
        return false;
    }
    int next_temp = 0, next_block = 0, expanded = 0, module_size = 0;
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        for (KrtIRBasicBlock* block = function->entry_block; block; block = block->next) {
            if (block->id > next_block) {
                next_block = block->id;
            }
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                if (module_size < INT_MAX) {
                    module_size++;
                }
                if (inst->result.type == KRT_IR_VALUE_TEMP && inst->result.data.index > next_temp) {
                    next_temp = inst->result.data.index;
                }
            }
        }
    }
    /* Small modules can afford one useful expansion. Large units have a 25%
     * growth budget, preventing a file full of eligible helpers from ballooning. */
    int remaining_growth = module_size / 4;
    if (remaining_growth < KRT_RECURSIVE_GROWTH_LIMIT) {
        remaining_growth = KRT_RECURSIVE_GROWTH_LIMIT;
    }
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        if (!function->entry_block || !KrtTokenIntegerBits(function->return_type) || function->param_count < 1 ||
            function->param_count > KRT_RECURSIVE_PARAM_LIMIT) {
            continue;
        }
        bool eligible = true;
        for (int i = 0; i < function->param_count; i++) {
            if (function->params[i].is_array || !KrtTokenIntegerBits(function->params[i].type)) {
                eligible = false;
            }
        }
        KrtIRBasicBlock* path[KRT_RECURSIVE_BLOCK_LIMIT];
        int calls = 0, size = 0;
        if (!eligible || !recursive_check_block(function, function->entry_block, path, 0, &calls, &size) || calls < 2 ||
            calls > 4 || remaining_growth < size) {
            continue;
        }
        int original_size = 0;
        for (KrtIRBasicBlock* block = function->entry_block; block; block = block->next) {
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                if (original_size < INT_MAX) {
                    original_size++;
                }
            }
        }
        /* Stop at the first depth exposing a reusable recursive result. Deeper
         * expansion can create more distinct recursive subproblems again, so
         * a larger body is not automatically better. Without reuse, keep the
         * deepest candidate within the same small, bounded expansion budget. */
        KrtRecursiveExpansion selected = {0};
        for (int depth = 1; depth <= KRT_RECURSIVE_DEPTH_LIMIT; depth++) {
            KrtRecursiveExpansion expansion = {0};
            expansion.function = function;
            expansion.next_temp = next_temp;
            expansion.next_block = next_block;
            KrtIRBasicBlock* entry = recursive_new_block(&expansion);
            if (entry) {
                KrtRecursiveMap map = {0};
                for (int i = 0; i < function->param_count; i++) {
                    map.arguments[i].type = KRT_IR_VALUE_ARG;
                    map.arguments[i].value_type = function->params[i].type;
                    map.arguments[i].data.index = i;
                }
                recursive_emit_path(&expansion, entry, function->entry_block, function->entry_block->first_inst, map,
                                    depth, NULL);
            }
            int growth = expansion.instruction_count - original_size;
            if (expansion.failed || !expansion.inline_count || growth > remaining_growth) {
                recursive_destroy_blocks(expansion.first);
                break;
            }

            int before_calls = 0, after_calls = 0;
            for (KrtIRBasicBlock* block = expansion.first; block; block = block->next) {
                for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (recursive_self_call(function, inst)) {
                        before_calls++;
                    }
                }
            }
            /* Eligibility proved that the candidate only calls itself. A
             * temporary one-function module can therefore use the existing
             * purity/value-numbering passes without touching the source CFG. */
            KrtIRFunction candidate = *function;
            candidate.entry_block = expansion.first;
            candidate.exit_block = expansion.last;
            candidate.next = NULL;
            KrtIRModule probe = {.optimization_level = 3, .functions = &candidate};
            IROptimizer probe_stats = {0};
            eliminate_pure_expressions(&probe, &probe_stats);
            eliminate_unused_integers(&probe);
            expansion.cse_count = probe_stats.cse_count;
            for (KrtIRBasicBlock* block = expansion.first; block; block = block->next) {
                for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                    if (recursive_self_call(function, inst)) {
                        after_calls++;
                    }
                }
            }
            recursive_destroy_blocks(selected.first);
            selected = expansion;
            if (after_calls < before_calls) {
                break;
            }
        }
        KrtRecursiveExpansion expansion = selected;
        if (!expansion.first) {
            continue;
        }
        int growth = expansion.instruction_count - original_size;
        if (stats) {
            stats->cse_count += expansion.cse_count;
            stats->optimization_count += expansion.cse_count;
        }
        recursive_destroy_blocks(function->entry_block);
        function->entry_block = expansion.first;
        function->exit_block = expansion.last;
        next_temp = expansion.next_temp;
        next_block = expansion.next_block;
        if (growth > 0) {
            remaining_growth -= growth;
        }
        expanded += expansion.inline_count;
    }
    if (stats) {
        stats->function_inline_count += expanded;
        stats->optimization_count += expanded;
    }
    return expanded > 0;
}
