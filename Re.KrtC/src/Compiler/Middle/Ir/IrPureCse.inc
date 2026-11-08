/* Local value numbering with transitively memory-free integer calls.
 * Never speculate a call or remove its first execution: it may trap or loop.
 * The reverse call graph permits pure recursive SCCs without quadratic scans. */
#define KRT_PURE_FUNCTION_LIMIT 65536
#define KRT_PURE_CALL_LIMIT 1048576
#define KRT_CSE_BLOCK_LIMIT 2048

typedef struct {
    KrtIRFunction* function;
    int callers;
    bool pure;
} KrtPureFunction;

typedef struct {
    int caller;
    int next;
} KrtPureEdge;

typedef struct {
    unsigned generation;
    int id;
    KrtIRValue value;
} KrtCseAlias;

typedef struct {
    unsigned generation;
    KrtIRInst* instruction;
} KrtCseExpression;

static bool cse_integer(KrtTokenType type) {
    return KrtTokenIntegerBits(type) || type == TOKEN_BOOL;
}

static bool cse_scalar(KrtIRValue value) {
    return cse_integer(value.value_type) && (value.type == KRT_IR_VALUE_INTEGER || value.type == KRT_IR_VALUE_IMM ||
                                             value.type == KRT_IR_VALUE_TEMP || value.type == KRT_IR_VALUE_ARG);
}

static int compare_pure_functions(const void* left, const void* right) {
    return strcmp(((const KrtPureFunction*)left)->function->name, ((const KrtPureFunction*)right)->function->name);
}

static int find_pure_function(KrtPureFunction* functions, int count, const char* name) {
    if (!name) {
        return -1;
    }
    int start = 0;
    while (start < count) {
        int middle = start + (count - start) / 2;
        int order = strcmp(name, functions[middle].function->name);
        if (!order) {
            return middle;
        }
        if (order < 0) {
            count = middle;
        } else {
            start = middle + 1;
        }
    }
    return -1;
}

static bool cse_scalar_operands(KrtIRInst* inst) {
    if (inst->operand_count < 1 || inst->operand_count > 2 || inst->result.type != KRT_IR_VALUE_TEMP ||
        !cse_integer(inst->result.value_type)) {
        return false;
    }
    for (int i = 0; i < inst->operand_count; i++) {
        if (inst->opcode == KRT_IR_CAST && i == 1) {
            if (inst->operands[i].type != KRT_IR_VALUE_IMM ||
                inst->operands[i].data.imm != (double)inst->result.value_type) {
                return false;
            }
        } else if (!cse_scalar(inst->operands[i])) {
            return false;
        }
    }
    return true;
}

static bool cse_integer_instruction(KrtIRInst* inst) {
    return (is_pure_operation(inst->opcode) || inst->opcode == KRT_IR_COPY || inst->opcode == KRT_IR_CAST) &&
           cse_scalar_operands(inst);
}

static int cse_direct_callee(KrtPureFunction* functions, int count, KrtIRInst* call) {
    if (call->opcode != KRT_IR_CALL || call->operand_count < 1 || call->operands[0].type != KRT_IR_VALUE_FUNCTION) {
        return -1;
    }
    int index = find_pure_function(functions, count, call->operands[0].data.function_name);
    if (index < 0) {
        return -1;
    }
    KrtIRFunction* callee = functions[index].function;
    if (call->operand_count != callee->param_count + 1 || call->result.type != KRT_IR_VALUE_TEMP ||
        call->result.value_type != callee->return_type) {
        return -1;
    }
    for (int i = 0; i < callee->param_count; i++) {
        if (callee->params[i].is_array || !cse_scalar(call->operands[i + 1]) ||
            call->operands[i + 1].value_type != callee->params[i].type) {
            return -1;
        }
    }
    return index;
}

static KrtPureFunction* find_memory_free_functions(KrtIRModule* module, int* function_count) {
    int count = 0, call_count = 0;
    for (KrtIRFunction* f = module->functions; f; f = f->next) {
        if (!f->name || ++count > KRT_PURE_FUNCTION_LIMIT) {
            return NULL;
        }
        for (KrtIRBasicBlock* b = f->entry_block; b; b = b->next) {
            for (KrtIRInst* i = b->first_inst; i; i = i->next) {
                if (i->opcode == KRT_IR_CALL && ++call_count > KRT_PURE_CALL_LIMIT) {
                    return NULL;
                }
            }
        }
    }
    if (!count) {
        return NULL;
    }
    KrtPureFunction* functions = KRT_CALLOC((size_t)count, sizeof(*functions));
    KrtPureEdge* edges = KRT_MALLOC((size_t)(call_count ? call_count : 1) * sizeof(*edges));
    int* queue = KRT_MALLOC((size_t)count * sizeof(*queue));
    if (!functions || !edges || !queue) {
        KRT_FREE(functions);
        KRT_FREE(edges);
        KRT_FREE(queue);
        return NULL;
    }
    int index = 0;
    for (KrtIRFunction* f = module->functions; f; f = f->next) {
        functions[index++] = (KrtPureFunction){f, -1, true};
    }
    qsort(functions, (size_t)count, sizeof(*functions), compare_pure_functions);
    int edge_count = 0, queued = 0;
    for (int j = 0; j < count; j++) {
        KrtIRFunction* f = functions[j].function;
        bool pure = f->entry_block && f->param_count >= 0 && cse_integer(f->return_type);
        for (int n = 0; n < f->param_count; n++) {
            if (f->params[n].is_array || !cse_integer(f->params[n].type)) {
                pure = false;
            }
        }
        for (KrtIRBasicBlock* b = f->entry_block; b; b = b->next) {
            for (KrtIRInst* i = b->first_inst; i; i = i->next) {
                if (i->opcode == KRT_IR_CALL) {
                    int callee = cse_direct_callee(functions, count, i);
                    if (callee < 0) {
                        pure = false;
                    } else {
                        edges[edge_count] = (KrtPureEdge){j, functions[callee].callers};
                        functions[callee].callers = edge_count++;
                    }
                } else if (i->opcode == KRT_IR_RETURN || i->opcode == KRT_IR_BRANCH) {
                    if (i->operand_count != 1 || !cse_scalar(i->operands[0])) {
                        pure = false;
                    }
                } else if (i->opcode != KRT_IR_JUMP && i->opcode != KRT_IR_NOP && !cse_integer_instruction(i)) {
                    pure = false;
                }
            }
        }
        functions[j].pure = pure;
        if (!pure) {
            queue[queued++] = j;
        }
    }
    for (int cursor = 0; cursor < queued; cursor++) {
        for (int e = functions[queue[cursor]].callers; e >= 0; e = edges[e].next) {
            int caller = edges[e].caller;
            if (!functions[caller].pure) {
                continue;
            }
            functions[caller].pure = false;
            queue[queued++] = caller;
        }
    }
    KRT_FREE(edges);
    KRT_FREE(queue);
    *function_count = count;
    return functions;
}

static KrtCseAlias* cse_alias_slot(KrtCseAlias* aliases, unsigned generation, unsigned mask, int id) {
    unsigned index = (unsigned)id * 2654435761u & mask;
    while (aliases[index].generation == generation && aliases[index].id != id) {
        index = (index + 1) & mask;
    }
    return &aliases[index];
}

static uint64_t cse_mix(uint64_t hash, uint64_t word) {
    return (hash ^ word) * UINT64_C(1099511628211);
}

static uint64_t cse_hash_value(uint64_t hash, KrtIRValue value) {
    hash = cse_mix(cse_mix(hash, value.type), value.value_type);
    switch (value.type) {
    case KRT_IR_VALUE_INTEGER:
        return cse_mix(cse_mix(hash, (uint64_t)value.data.integer), (uint64_t)(value.data.integer >> 64));
    case KRT_IR_VALUE_IMM: {
        uint64_t bits;
        memcpy(&bits, &value.data.imm, sizeof(bits));
        return cse_mix(hash, bits);
    }
    case KRT_IR_VALUE_FUNCTION:
        for (const unsigned char* c = (const unsigned char*)value.data.function_name; *c; c++) {
            hash = cse_mix(hash, *c);
        }
        return hash;
    default:
        return cse_mix(hash, (unsigned)value.data.index);
    }
}

static bool cse_equal_value(KrtIRValue left, KrtIRValue right) {
    if (left.type != right.type || left.value_type != right.value_type) {
        return false;
    }
    if (left.type == KRT_IR_VALUE_FUNCTION) {
        return strcmp(left.data.function_name, right.data.function_name) == 0;
    }
    if (left.type == KRT_IR_VALUE_INTEGER) {
        return left.data.integer == right.data.integer;
    }
    if (left.type == KRT_IR_VALUE_IMM) {
        return memcmp(&left.data.imm, &right.data.imm, sizeof(double)) == 0;
    }
    return left.data.index == right.data.index;
}

static bool cse_equal_expression(KrtIRInst* left, KrtIRInst* right) {
    if (left->opcode != right->opcode || left->result.value_type != right->result.value_type ||
        left->operand_count != right->operand_count) {
        return false;
    }
    for (int i = 0; i < left->operand_count; i++) {
        if (!cse_equal_value(left->operands[i], right->operands[i])) {
            return false;
        }
    }
    return true;
}

static int number_pure_block(KrtIRBasicBlock* block, KrtPureFunction* functions, int function_count) {
    int count = 0;
    for (KrtIRInst* i = block->first_inst; i; i = i->next) {
        if (++count > KRT_CSE_BLOCK_LIMIT) {
            return 0;
        }
    }
    unsigned capacity = 8;
    while (capacity < (unsigned)count * 2) {
        capacity *= 2;
    }
    KrtCseAlias* aliases = KRT_CALLOC(capacity, sizeof(*aliases));
    KrtCseExpression* expressions = KRT_CALLOC(capacity, sizeof(*expressions));
    if (!aliases || !expressions) {
        KRT_FREE(aliases);
        KRT_FREE(expressions);
        return 0;
    }
    unsigned generation = 1, mask = capacity - 1;
    int changed = 0;
    for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
        bool result_temp = inst->result.type == KRT_IR_VALUE_TEMP;
        if (result_temp &&
            cse_alias_slot(aliases, generation, mask, inst->result.data.index)->generation == generation) {
            generation++;
        }
        /* Stores bind their first operand; it is not an expression use. PHIs
         * refer to predecessor values and are outside this local analysis. */
        if (inst->opcode != KRT_IR_PHI && inst->opcode != KRT_IR_ALLOC && inst->opcode != KRT_IR_ADDRESS_OF) {
            for (int n = inst->opcode == KRT_IR_STORE ? 1 : 0; n < inst->operand_count; n++) {
                KrtIRValue* value = &inst->operands[n];
                if (value->type != KRT_IR_VALUE_TEMP) {
                    continue;
                }
                KrtCseAlias* alias = cse_alias_slot(aliases, generation, mask, value->data.index);
                if (alias->generation == generation && alias->value.value_type == value->value_type &&
                    !cse_equal_value(*value, alias->value)) {
                    *value = alias->value;
                    changed++;
                }
            }
        }
        int callee = cse_direct_callee(functions, function_count, inst);
        bool pure_call = callee >= 0 && functions[callee].pure;
        bool integer = cse_integer_instruction(inst);
        if (integer && inst->opcode == KRT_IR_ADD && inst->operand_count == 2 &&
            KrtTokenIntegerBits(inst->result.value_type) && inst->operands[0].value_type == inst->result.value_type &&
            cse_equal_value(inst->operands[0], inst->operands[1])) {
            /* x+x == x*2 modulo 2^N, including the signed two-bit type. */
            inst->opcode = KRT_IR_MUL;
            inst->operands[1] = KrtIrInteger(2, inst->result.value_type);
            changed++;
        }
        KrtIRValue canonical = inst->result;
        /* A bool-to-bool cast canonicalizes memory representations (e.g. 3)
         * to 0/1 and therefore is not an identity, even with equal types. */
        if (integer &&
            (inst->opcode == KRT_IR_COPY || inst->opcode == KRT_IR_IMM ||
             (inst->opcode == KRT_IR_CAST && inst->result.value_type != TOKEN_BOOL)) &&
            inst->operands[0].value_type == inst->result.value_type) {
            canonical = inst->operands[0];
        } else if (integer || pure_call) {
            uint64_t hash = cse_mix(cse_mix(UINT64_C(1469598103934665603), inst->opcode), inst->result.value_type);
            for (int n = 0; n < inst->operand_count; n++) {
                hash = cse_hash_value(hash, inst->operands[n]);
            }
            unsigned slot = (unsigned)hash & mask;
            while (expressions[slot].generation == generation &&
                   !cse_equal_expression(inst, expressions[slot].instruction)) {
                slot = (slot + 1) & mask;
            }
            if (expressions[slot].generation == generation) {
                canonical = expressions[slot].instruction->result;
                inst->opcode = KRT_IR_COPY;
                inst->operand_count = 1;
                inst->operands[0] = canonical;
                changed++;
            } else {
                expressions[slot] = (KrtCseExpression){generation, inst};
            }
        }
        if (!integer && !pure_call && inst->opcode != KRT_IR_NOP) {
            generation++;
        }
        if (result_temp) {
            KrtCseAlias* alias = cse_alias_slot(aliases, generation, mask, inst->result.data.index);
            *alias = (KrtCseAlias){generation, inst->result.data.index, canonical};
        }
        if (inst->opcode == KRT_IR_RETURN || inst->opcode == KRT_IR_BRANCH || inst->opcode == KRT_IR_JUMP) {
            break;
        }
    }
    KRT_FREE(aliases);
    KRT_FREE(expressions);
    return changed;
}

static bool eliminate_pure_expressions(KrtIRModule* module, IROptimizer* stats) {
    int function_count = 0;
    KrtPureFunction* functions = find_memory_free_functions(module, &function_count);
    if (!functions) {
        return false;
    }
    int changed = 0;
    for (KrtIRFunction* f = module->functions; f; f = f->next) {
        for (KrtIRBasicBlock* b = f->entry_block; b; b = b->next) {
            changed += number_pure_block(b, functions, function_count);
        }
    }
    KRT_FREE(functions);
    if (stats) {
        stats->cse_count += changed;
        stats->optimization_count += changed;
    }
    return changed != 0;
}

typedef struct {
    KrtIRInst* definition;
    int id;
    unsigned uses;
    bool multiple;
} KrtUnusedInteger;

static KrtUnusedInteger* unused_integer_slot(KrtUnusedInteger* values, unsigned mask, int id) {
    unsigned index = (unsigned)id * 2654435761u & mask;
    while (values[index].definition && values[index].id != id) {
        index = (index + 1) & mask;
    }
    return &values[index];
}

static bool removable_integer(KrtUnusedInteger* value) {
    KrtIRInst* inst = value->definition;
    return inst && !value->multiple && !value->uses && cse_integer_instruction(inst) && inst->opcode != KRT_IR_DIV &&
           inst->opcode != KRT_IR_MOD;
}

/* Reusing a value leaves obsolete transfers and argument computations. Delete
 * only unused, nontrapping integer definitions; retain every first call and
 * division even when its result is unused. A worklist visits each use once. */
static bool eliminate_unused_integers(KrtIRModule* module) {
    bool changed = false;
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        unsigned count = 0;
        for (KrtIRBasicBlock* b = function->entry_block; b; b = b->next) {
            for (KrtIRInst* i = b->first_inst; i; i = i->next) {
                if (count <= 262144) {
                    count++;
                }
            }
        }
        if (!count || count > 262144) {
            continue;
        }
        unsigned capacity = 8;
        while (capacity < count * 2) {
            capacity *= 2;
        }
        KrtUnusedInteger* values = KRT_CALLOC(capacity, sizeof(*values));
        KrtUnusedInteger** queue = KRT_MALLOC((size_t)count * sizeof(*queue));
        if (!values || !queue) {
            KRT_FREE(values);
            KRT_FREE(queue);
            continue;
        }
        unsigned mask = capacity - 1, queued = 0;
        for (KrtIRBasicBlock* b = function->entry_block; b; b = b->next) {
            for (KrtIRInst* i = b->first_inst; i; i = i->next) {
                if (i->result.type != KRT_IR_VALUE_TEMP) {
                    continue;
                }
                KrtUnusedInteger* value = unused_integer_slot(values, mask, i->result.data.index);
                if (value->definition) {
                    value->multiple = true;
                } else {
                    value->id = i->result.data.index;
                    value->definition = i;
                }
            }
        }
        for (KrtIRBasicBlock* b = function->entry_block; b; b = b->next) {
            for (KrtIRInst* i = b->first_inst; i; i = i->next) {
                for (int n = 0; n < i->operand_count; n++) {
                    if (i->operands[n].type != KRT_IR_VALUE_TEMP) {
                        continue;
                    }
                    KrtUnusedInteger* value = unused_integer_slot(values, mask, i->operands[n].data.index);
                    if (value->definition && value->uses < UINT_MAX) {
                        value->uses++;
                    }
                }
            }
        }
        for (unsigned i = 0; i < capacity; i++) {
            if (removable_integer(&values[i])) {
                queue[queued++] = &values[i];
            }
        }
        for (unsigned cursor = 0; cursor < queued; cursor++) {
            KrtIRInst* inst = queue[cursor]->definition;
            inst->opcode = KRT_IR_NOP;
            changed = true;
            for (int n = 0; n < inst->operand_count; n++) {
                if (inst->operands[n].type != KRT_IR_VALUE_TEMP) {
                    continue;
                }
                KrtUnusedInteger* value = unused_integer_slot(values, mask, inst->operands[n].data.index);
                if (value->definition && value->uses && value->uses != UINT_MAX && !--value->uses &&
                    removable_integer(value)) {
                    queue[queued++] = value;
                }
            }
        }
        KRT_FREE(values);
        KRT_FREE(queue);
    }
    return changed;
}
