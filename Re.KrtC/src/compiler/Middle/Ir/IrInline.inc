/* Inline small, straight-line integer helpers. Calls, memory, mutable slots and
 * control flow in the callee are excluded; arguments are evaluated by the caller. */
#define KRT_INLINE_BODY_LIMIT 32
#define KRT_INLINE_GROWTH_LIMIT 256
#define KRT_INLINE_CALLER_LIMIT 1024

typedef struct {
    KrtIRFunction* function;
    int count;
} KrtInlineCandidate;

static bool inline_scalar(KrtIRValue value) {
    return KrtTokenIntegerBits(value.value_type) || value.value_type == TOKEN_BOOL;
}

static int inline_body_size(KrtIRFunction* function) {
    if (!function->entry_block || function->entry_block->next || function->param_count > 8 ||
        function->param_count < 0 || !KrtTokenIntegerBits(function->return_type)) {
        return 0;
    }
    for (int i = 0; i < function->param_count; i++) {
        if (function->params[i].is_array || !KrtTokenIntegerBits(function->params[i].type)) {
            return 0;
        }
    }
    int ids[KRT_INLINE_BODY_LIMIT], count = 0;
    for (KrtIRInst* inst = function->entry_block->first_inst; inst; inst = inst->next) {
        bool terminal = inst->opcode == KRT_IR_RETURN;
        if (terminal) {
            if (inst->operand_count != 1 || inst->operands[0].value_type != function->return_type) {
                return 0;
            }
        } else if (!is_pure_operation(inst->opcode) && inst->opcode != KRT_IR_COPY && inst->opcode != KRT_IR_CAST) {
            return 0;
        }
        if ((!terminal && (inst->result.type != KRT_IR_VALUE_TEMP || !inline_scalar(inst->result))) ||
            inst->operand_count < 1 || inst->operand_count > 2) {
            return 0;
        }
        for (int i = 0; i < inst->operand_count; i++) {
            KrtIRValue value = inst->operands[i];
            if (inst->opcode == KRT_IR_CAST && i == 1) {
                if (value.type != KRT_IR_VALUE_IMM || value.data.imm != (double)inst->result.value_type) {
                    return 0;
                }
                continue;
            }
            if (!inline_scalar(value)) {
                return 0;
            }
            if (value.type == KRT_IR_VALUE_ARG) {
                if (value.data.index < 0 || value.data.index >= function->param_count) {
                    return 0;
                }
            } else if (value.type == KRT_IR_VALUE_TEMP) {
                bool defined = false;
                for (int j = 0; j < count; j++) {
                    if (ids[j] == value.data.index) {
                        defined = true;
                    }
                }
                if (!defined) {
                    return 0;
                }
            } else if (value.type != KRT_IR_VALUE_INTEGER && value.type != KRT_IR_VALUE_IMM) {
                return 0;
            }
        }
        if (terminal) {
            return count + 1;
        }
        if (count == KRT_INLINE_BODY_LIMIT - 1) {
            return 0;
        }
        for (int i = 0; i < count; i++) {
            if (ids[i] == inst->result.data.index) {
                return 0;
            }
        }
        ids[count++] = inst->result.data.index;
    }
    return 0;
}

static int compare_inline_candidates(const void* left, const void* right) {
    const KrtInlineCandidate* a = left;
    const KrtInlineCandidate* b = right;
    return strcmp(a->function->name, b->function->name);
}

static KrtInlineCandidate* find_inline_candidate(KrtInlineCandidate* candidates, int count, const char* name) {
    int start = 0;
    while (start < count) {
        int middle = start + (count - start) / 2;
        int order = strcmp(name, candidates[middle].function->name);
        if (!order) {
            return &candidates[middle];
        }
        if (order < 0) {
            count = middle;
        } else {
            start = middle + 1;
        }
    }
    return NULL;
}

static KrtIRValue inline_operand(KrtIRValue value, KrtIRInst* call, const int* old_ids, const int* new_ids, int count) {
    if (value.type == KRT_IR_VALUE_ARG) {
        return call->operands[value.data.index + 1];
    }
    if (value.type == KRT_IR_VALUE_TEMP) {
        for (int i = 0; i < count; i++) {
            if (old_ids[i] == value.data.index) {
                value.data.index = new_ids[i];
                break;
            }
        }
    }
    return value;
}

static bool inline_call(KrtIRBasicBlock* block, KrtIRInst* previous, KrtIRInst* call, KrtInlineCandidate* candidate,
                        int* next_temp) {
    KrtIRFunction* callee = candidate->function;
    if (call->operand_count != callee->param_count + 1 || call->result.type != KRT_IR_VALUE_TEMP ||
        call->result.value_type != callee->return_type) {
        return false;
    }
    for (int i = 0; i < callee->param_count; i++) {
        if (call->operands[i + 1].value_type != callee->params[i].type) {
            return false;
        }
    }
    if (*next_temp > INT_MAX - candidate->count) {
        return false;
    }
    int old_ids[KRT_INLINE_BODY_LIMIT], new_ids[KRT_INLINE_BODY_LIMIT], count = 0;
    KrtIRInst* first = NULL;
    KrtIRInst* last = NULL;
    KrtIRValue returned = {0};
    for (KrtIRInst* inst = callee->entry_block->first_inst; inst; inst = inst->next) {
        if (inst->opcode == KRT_IR_RETURN) {
            returned = inline_operand(inst->operands[0], call, old_ids, new_ids, count);
            break;
        }
        KrtIRInst* copy = (KrtIRInst*)KRT_CALLOC(1, sizeof(*copy));
        if (!copy) {
            goto failed;
        }
        copy->operands = (KrtIRValue*)KRT_MALLOC((size_t)inst->operand_count * sizeof(KrtIRValue));
        if (!copy->operands) {
            KRT_FREE(copy);
            goto failed;
        }
        copy->opcode = inst->opcode;
        copy->is_int_result = inst->is_int_result;
        copy->operand_count = copy->operand_capacity = inst->operand_count;
        for (int i = 0; i < inst->operand_count; i++) {
            copy->operands[i] = inline_operand(inst->operands[i], call, old_ids, new_ids, count);
        }
        old_ids[count] = inst->result.data.index;
        new_ids[count] = ++*next_temp;
        copy->result = inst->result;
        copy->result.data.index = new_ids[count++];
        if (last) {
            last->next = copy;
        } else {
            first = copy;
        }
        last = copy;
    }
    if (block->inst_count > INT_MAX - count) {
        goto failed;
    }
    int required = block->inst_count + count;
    if (required > block->inst_capacity) {
        KrtIRInst** grown = (KrtIRInst**)KRT_REALLOC(block->insts, (size_t)required * sizeof(KrtIRInst*));
        if (!grown) {
            goto failed;
        }
        block->insts = grown;
        block->inst_capacity = required;
    }
    if (last) {
        last->next = call;
        if (previous) {
            previous->next = first;
        } else {
            block->first_inst = first;
        }
    }
    call->opcode = KRT_IR_COPY;
    call->operand_count = 1;
    call->operands[0] = returned;
    block->inst_count = required;
    return true;
failed:
    while (first) {
        KrtIRInst* next = first->next;
        KRT_FREE(first->operands);
        KRT_FREE(first);
        first = next;
    }
    return false;
}

static bool inline_small_functions(KrtIRModule* module, IROptimizer* stats) {
    if (!module) {
        return false;
    }
    int function_count = 0, next_temp = 0;
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        function_count++;
        for (KrtIRBasicBlock* block = function->entry_block; block; block = block->next) {
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                if (inst->result.type == KRT_IR_VALUE_TEMP && inst->result.data.index > next_temp) {
                    next_temp = inst->result.data.index;
                }
                for (int i = 0; i < inst->operand_count; i++) {
                    if (inst->operands[i].type == KRT_IR_VALUE_TEMP && inst->operands[i].data.index > next_temp) {
                        next_temp = inst->operands[i].data.index;
                    }
                }
            }
        }
    }
    KrtInlineCandidate* candidates = (KrtInlineCandidate*)KRT_MALLOC((size_t)function_count * sizeof(*candidates));
    if (!candidates) {
        return false;
    }
    int count = 0;
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        int size = inline_body_size(function);
        if (size) {
            candidates[count++] = (KrtInlineCandidate){function, size};
        }
    }
    qsort(candidates, (size_t)count, sizeof(*candidates), compare_inline_candidates);
    int inlined = 0;
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        /* Keep large functions within their existing resource requirements. */
        int body_count = 0;
        for (KrtIRBasicBlock* block = function->entry_block; block && body_count <= KRT_INLINE_CALLER_LIMIT;
             block = block->next) {
            for (KrtIRInst* inst = block->first_inst; inst && body_count <= KRT_INLINE_CALLER_LIMIT;
                 inst = inst->next) {
                body_count++;
            }
        }
        if (body_count > KRT_INLINE_CALLER_LIMIT) {
            continue;
        }
        int growth = 0;
        for (KrtIRBasicBlock* block = function->entry_block; block; block = block->next) {
            KrtIRInst* previous = NULL;
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                if (inst->opcode == KRT_IR_CALL && inst->operand_count >= 1 &&
                    inst->operands[0].type == KRT_IR_VALUE_FUNCTION && inst->operands[0].data.function_name) {
                    KrtInlineCandidate* candidate =
                        find_inline_candidate(candidates, count, inst->operands[0].data.function_name);
                    if (candidate && growth + candidate->count <= KRT_INLINE_GROWTH_LIMIT &&
                        inline_call(block, previous, inst, candidate, &next_temp)) {
                        growth += candidate->count;
                        inlined++;
                    }
                }
                previous = inst;
            }
            int index = 0;
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                block->insts[index++] = inst;
            }
            KrtIrBlockInvalidateCache(block);
        }
    }
    KRT_FREE(candidates);
    if (stats) {
        stats->function_inline_count += inlined;
        stats->optimization_count += inlined;
    }
    return inlined > 0;
}
