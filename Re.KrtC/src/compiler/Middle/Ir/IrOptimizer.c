#include "IrOptimizer.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define MAX_OPTIMIZATION_PASSES 10

typedef struct {
    KrtIRValue* values;
    int count;
    int capacity;
} ValueTable;

typedef struct {
    int temp_id;
    KrtIRValue constant_value;
    bool is_constant;
} ConstantEntry;

typedef struct {
    ConstantEntry* entries;
    int count;
    int capacity;
} ConstantTable;

static double KrtGetTimeSec(void) {
    return (double)clock() / CLOCKS_PER_SEC;
}

IROptimizer* ir_optimizer_create(void) {
    IROptimizer* optimizer = (IROptimizer*)KRT_CALLOC(1, sizeof(IROptimizer));
    return optimizer;
}

void ir_optimizer_destroy(IROptimizer* optimizer) {
    if (!optimizer) return;
    KRT_FREE(optimizer);
}

static bool is_constant_value(KrtIRValue* value) {
    return value && value->type == KRT_IR_VALUE_IMM;
}

static bool values_equal(KrtIRValue* a, KrtIRValue* b) {
    if (a->type != b->type) return false;
    switch (a->type) {
        case KRT_IR_VALUE_IMM:
            return a->data.imm == b->data.imm;
        case KRT_IR_VALUE_VAR:
        case KRT_IR_VALUE_ARG:
            return strcmp(a->data.name, b->data.name) == 0;
        case KRT_IR_VALUE_TEMP:
            return a->data.index == b->data.index;
        default:
            return false;
    }
}

static bool is_pure_operation(KrtIROpcode opcode) {
    switch (opcode) {
        case KRT_IR_ADD:
        case KRT_IR_SUB:
        case KRT_IR_MUL:
        case KRT_IR_DIV:
        case KRT_IR_MOD:
        case KRT_IR_AND:
        case KRT_IR_OR:
        case KRT_IR_XOR:
        case KRT_IR_LSHIFT:
        case KRT_IR_RSHIFT:
        case KRT_IR_LT:
        case KRT_IR_GT:
        case KRT_IR_EQ:
        case KRT_IR_LE:
        case KRT_IR_GE:
        case KRT_IR_NE:
        case KRT_IR_IMM:
            return true;
        default:
            return false;
    }
}

static bool is_side_effect_free(KrtIROpcode opcode) {
    switch (opcode) {
        case KRT_IR_LOAD:
        case KRT_IR_STORE:
        case KRT_IR_ALLOC:
        case KRT_IR_CALL:
        case KRT_IR_SYSCALL:
        case KRT_IR_STOREPTR:
        case KRT_IR_ARRAY_STORE:
        case KRT_IR_RETURN:
        case KRT_IR_BRANCH:
        case KRT_IR_JUMP:
            return false;
        default:
            return true;
    }
}

static double fold_binary_op(KrtIROpcode op, double left, double right) {
    switch (op) {
        case KRT_IR_ADD: return left + right;
        case KRT_IR_SUB: return left - right;
        case KRT_IR_MUL: return left * right;
        case KRT_IR_DIV: return right != 0 ? left / right : 0;
        case KRT_IR_MOD: return right != 0 ? fmod(left, right) : 0;
        case KRT_IR_AND: return (int64_t)left & (int64_t)right;
        case KRT_IR_OR:  return (int64_t)left | (int64_t)right;
        case KRT_IR_XOR: return (int64_t)left ^ (int64_t)right;
        case KRT_IR_LSHIFT: return (int64_t)left << (int)right;
        case KRT_IR_RSHIFT: return (int64_t)left >> (int)right;
        case KRT_IR_LT: return left < right ? 1 : 0;
        case KRT_IR_GT: return left > right ? 1 : 0;
        case KRT_IR_EQ: return left == right ? 1 : 0;
        case KRT_IR_LE: return left <= right ? 1 : 0;
        case KRT_IR_GE: return left >= right ? 1 : 0;
        case KRT_IR_NE: return left != right ? 1 : 0;
        default: return 0;
    }
}

static bool fold_constant_unary(KrtIROpcode op, double operand, double* result) {
    switch (op) {
        case KRT_IR_SUB:
            *result = -operand;
            return true;
        default:
            return false;
    }
}

static bool fold_constant_binary(KrtIROpcode op, double left, double right, double* result) {
    if (op == KRT_IR_DIV && right == 0) return false;
    if (op == KRT_IR_MOD && right == 0) return false;
    *result = fold_binary_op(op, left, right);
    return true;
}

bool ir_optimize_constant_folding(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int fold_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                KrtIRInst* next = inst->next;
                
                if (inst->operand_count == 2 &&
                    is_pure_operation(inst->opcode) &&
                    is_constant_value(&inst->operands[0]) &&
                    is_constant_value(&inst->operands[1])) {

                    double left = inst->operands[0].data.imm;
                    double right = inst->operands[1].data.imm;
                    double result;

                    if (fold_constant_binary(inst->opcode, left, right, &result)) {
                        inst->opcode = KRT_IR_IMM;
                        inst->operands[0].data.imm = result;
                        inst->operand_count = 1;
                        changed = true;
                        fold_count++;
                    }
                }
                else if (inst->operand_count == 1 &&
                         inst->opcode == KRT_IR_SUB &&
                         is_constant_value(&inst->operands[0])) {
                    double result;
                    if (fold_constant_unary(inst->opcode, inst->operands[0].data.imm, &result)) {
                        inst->opcode = KRT_IR_IMM;
                        inst->operands[0].data.imm = result;
                        changed = true;
                        fold_count++;
                    }
                }
                
                inst = next;
            }
            block = block->next;
        }
        func = func->next;
    }
    
    if (stats) {
        stats->constant_fold_count += fold_count;
        stats->optimization_count += fold_count;
    }
    
    return changed;
}

bool ir_optimize_constant_propagation(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int prop_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        ConstantTable constants = {0};
        constants.count = 0;
        constants.capacity = 256;
        constants.entries = (ConstantEntry*)KRT_CALLOC(constants.capacity, sizeof(ConstantEntry));
        if (!constants.entries) {
            func = func->next;
            continue;
        }
        
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                if (inst->opcode == KRT_IR_IMM && inst->result.type == KRT_IR_VALUE_TEMP) {
                    int found = 0;
                    for (int i = 0; i < constants.count; i++) {
                        if (constants.entries[i].temp_id == inst->result.data.index) {
                            constants.entries[i].constant_value = inst->operands[0];
                            constants.entries[i].is_constant = true;
                            found = 1;
                            break;
                        }
                    }
                    if (!found && constants.count < constants.capacity) {
                        constants.entries[constants.count].temp_id = inst->result.data.index;
                        constants.entries[constants.count].constant_value = inst->operands[0];
                        constants.entries[constants.count].is_constant = true;
                        constants.count++;
                    }
                }
                
                for (int i = 0; i < inst->operand_count; i++) {
                    if (inst->operands[i].type == KRT_IR_VALUE_TEMP) {
                        for (int j = 0; j < constants.count; j++) {
                            if (constants.entries[j].temp_id == inst->operands[i].data.index &&
                                constants.entries[j].is_constant) {
                                inst->operands[i] = constants.entries[j].constant_value;
                                changed = true;
                                prop_count++;
                                break;
                            }
                        }
                    }
                }
                
                inst = inst->next;
            }
            block = block->next;
        }
        
        KRT_FREE(constants.entries);
        func = func->next;
    }
    
    if (stats) {
        stats->optimization_count += prop_count;
    }
    
    return changed;
}

bool ir_optimize_dead_code_elimination(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int elim_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            KrtIRInst* prev = NULL;
            
            while (inst) {
                KrtIRInst* next = inst->next;
                bool can_remove = false;

                if (inst->opcode == KRT_IR_CALL)
                {
                    const char* fn = inst->operand_count > 0 ? inst->operands[0].data.function_name : "(null)";
                }

                if (is_side_effect_free(inst->opcode) && inst->result.type == KRT_IR_VALUE_VOID) {
                    can_remove = true;
                }
                
                if (inst->opcode == KRT_IR_NOP) {
                    can_remove = true;
                }
                
                if (can_remove) {
                    if (prev) {
                        prev->next = next;
                    } else {
                        block->first_inst = next;
                    }
                    if (block->last_inst == inst) {
                        block->last_inst = prev;
                    }
                    block->inst_count--;
                    inst = next;
                    changed = true;
                    elim_count++;
                } else {
                    prev = inst;
                    inst = next;
                }
            }
            block = block->next;
        }
        func = func->next;
    }
    
    if (stats) {
        stats->dead_code_count += elim_count;
        stats->optimization_count += elim_count;
    }
    
    return changed;
}

typedef struct {
    KrtIROpcode opcode;
    KrtIRValue left;
    KrtIRValue right;
    int temp_result;
} Expression;

bool ir_optimize_common_subexpression_elimination(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int cse_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        int expr_capacity = 256;
        Expression* exprs = (Expression*)KRT_CALLOC(expr_capacity, sizeof(Expression));
        if (!exprs) {
            func = func->next;
            continue;
        }
        int expr_count = 0;
        
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                if (is_pure_operation(inst->opcode) && inst->operand_count >= 2) {
                    bool found = false;
                    for (int i = 0; i < expr_count; i++) {
                        if (exprs[i].opcode == inst->opcode &&
                            values_equal(&exprs[i].left, &inst->operands[0]) &&
                            values_equal(&exprs[i].right, &inst->operands[1])) {
                            
                            inst->opcode = KRT_IR_COPY;
                            inst->operand_count = 1;
                            inst->operands[0].type = KRT_IR_VALUE_TEMP;
                            inst->operands[0].data.index = exprs[i].temp_result;
                            changed = true;
                            found = true;
                            cse_count++;
                            break;
                        }
                    }
                    
                    if (!found && expr_count < expr_capacity && inst->result.type == KRT_IR_VALUE_TEMP) {
                        exprs[expr_count].opcode = inst->opcode;
                        exprs[expr_count].left = inst->operands[0];
                        exprs[expr_count].right = inst->operands[1];
                        exprs[expr_count].temp_result = inst->result.data.index;
                        expr_count++;
                    }
                }
                
                inst = inst->next;
            }
            block = block->next;
        }
        
        KRT_FREE(exprs);
        func = func->next;
    }
    
    if (stats) {
        stats->cse_count += cse_count;
        stats->optimization_count += cse_count;
    }
    
    return changed;
}

bool ir_optimize_copy_propagation(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int copy_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                if (inst->opcode == KRT_IR_COPY && inst->operand_count == 1 &&
                    inst->result.type == KRT_IR_VALUE_TEMP) {
                    int copy_from = inst->operands[0].data.index;
                    int copy_to = inst->result.data.index;
                    
                    KrtIRInst* use = inst->next;
                    while (use) {
                        if (use->result.type == KRT_IR_VALUE_TEMP && use->result.data.index == copy_to) {
                            break;
                        }
                        for (int i = 0; i < use->operand_count; i++) {
                            if (use->operands[i].type == KRT_IR_VALUE_TEMP &&
                                use->operands[i].data.index == copy_to) {
                                use->operands[i].data.index = copy_from;
                                changed = true;
                                copy_count++;
                            }
                        }
                        use = use->next;
                    }
                }
                inst = inst->next;
            }
            block = block->next;
        }
        func = func->next;
    }
    
    if (stats) {
        stats->optimization_count += copy_count;
    }
    
    return changed;
}

bool ir_optimize_strength_reduction(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int reduce_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                if (inst->opcode == KRT_IR_MUL && inst->operand_count == 2) {
                    if (is_constant_value(&inst->operands[1])) {
                        double val = inst->operands[1].data.imm;
                        if (val == 0) {
                            inst->opcode = KRT_IR_IMM;
                            inst->operands[0].data.imm = 0;
                            inst->operand_count = 1;
                            changed = true;
                            reduce_count++;
                        } else if (val == 1) {
                            inst->opcode = KRT_IR_COPY;
                            inst->operand_count = 1;
                            inst->operands[0] = inst->operands[0];
                            changed = true;
                            reduce_count++;
                        } else if (val == 2) {
                            inst->opcode = KRT_IR_ADD;
                            inst->operands[1] = inst->operands[0];
                            changed = true;
                            reduce_count++;
                        }
                    }
                }
                else if (inst->opcode == KRT_IR_DIV && inst->operand_count == 2) {
                    if (is_constant_value(&inst->operands[1]) && inst->operands[1].data.imm == 1) {
                        inst->opcode = KRT_IR_COPY;
                        inst->operand_count = 1;
                        changed = true;
                        reduce_count++;
                    }
                }
                else if (inst->opcode == KRT_IR_POW && inst->operand_count == 2) {
                    if (is_constant_value(&inst->operands[1])) {
                        double exp = inst->operands[1].data.imm;
                        if (exp == 0) {
                            inst->opcode = KRT_IR_IMM;
                            inst->operands[0].data.imm = 1;
                            inst->operand_count = 1;
                            changed = true;
                            reduce_count++;
                        } else if (exp == 1) {
                            inst->opcode = KRT_IR_COPY;
                            inst->operand_count = 1;
                            changed = true;
                            reduce_count++;
                        } else if (exp == 2) {
                            inst->opcode = KRT_IR_MUL;
                            inst->operands[1] = inst->operands[0];
                            changed = true;
                            reduce_count++;
                        }
                    }
                }
                
                inst = inst->next;
            }
            block = block->next;
        }
        func = func->next;
    }
    
    if (stats) {
        stats->strength_reduce_count += reduce_count;
        stats->optimization_count += reduce_count;
    }
    
    return changed;
}

bool ir_optimize_control_flow(KrtIRModule* module, IROptimizer* stats) {
    if (!module) return false;
    
    bool changed = false;
    int cf_count = 0;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                if (inst->opcode == KRT_IR_BRANCH && inst->operand_count >= 3) {
                    if (is_constant_value(&inst->operands[0])) {
                        double cond = inst->operands[0].data.imm;
                        inst->opcode = KRT_IR_JUMP;
                        inst->operand_count = 1;
                        if (cond != 0) {
                            inst->operands[0] = inst->operands[1];
                        } else {
                            inst->operands[0] = inst->operands[2];
                        }
                        changed = true;
                        cf_count++;
                    }
                }
                inst = inst->next;
            }
            block = block->next;
        }
        func = func->next;
    }
    
    if (stats) {
        stats->control_flow_count += cf_count;
        stats->optimization_count += cf_count;
    }
    
    return changed;
}

bool ir_optimize_loop_invariant_code_motion(KrtIRModule* module, IROptimizer* stats) {
    (void)module;
    (void)stats;
    return false;
}

bool ir_optimize_function_inlining(KrtIRModule* module, IROptimizer* stats) {
    (void)module;
    (void)stats;
    return false;
}

bool ir_optimize_escape_analysis(KrtIRModule* module, IROptimizer* stats) {
    (void)module;
    (void)stats;
    return false;
}

void ir_optimize_module(IROptimizer* optimizer, KrtIRModule* module, OptimizationFlags flags) {
    if (!optimizer || !module) return;
    
    double start_time = KrtGetTimeSec();
    int pass = 0;
    bool changed = true;
    
    while (changed && pass < MAX_OPTIMIZATION_PASSES) {
        changed = false;
        pass++;
        
        if (flags & OPT_CONSTANT_FOLDING) {
            changed |= ir_optimize_constant_folding(module, optimizer);
        }
        
        if (flags & OPT_CONSTANT_PROPAGATION) {
            changed |= ir_optimize_constant_propagation(module, optimizer);
        }
        
        if (flags & OPT_COPY_PROPAGATION) {
            changed |= ir_optimize_copy_propagation(module, optimizer);
        }
        
        if (flags & OPT_COMMON_SUBEXPRESSION_ELIMINATION) {
            changed |= ir_optimize_common_subexpression_elimination(module, optimizer);
        }
        
        if (flags & OPT_STRENGTH_REDUCTION) {
            changed |= ir_optimize_strength_reduction(module, optimizer);
        }
        
        if (flags & OPT_DEAD_CODE_ELIMINATION) {
            changed |= ir_optimize_dead_code_elimination(module, optimizer);
        }
        
        if (flags & OPT_CONTROL_FLOW_OPTIMIZATION) {
            changed |= ir_optimize_control_flow(module, optimizer);
        }
    }
    
    optimizer->time_spent += KrtGetTimeSec() - start_time;
}

int ir_optimizer_get_total_optimizations(IROptimizer* optimizer) {
    return optimizer ? optimizer->optimization_count : 0;
}

double ir_optimizer_get_time_spent(IROptimizer* optimizer) {
    return optimizer ? optimizer->time_spent : 0.0;
}

void ir_optimizer_print_stats(IROptimizer* optimizer) {
    if (!optimizer) return;
    
    printf("IR Optimization Statistics:\n");
    printf("  Total optimizations: %d\n", optimizer->optimization_count);
    printf("  Time spent: %.3f seconds\n", optimizer->time_spent);
    printf("  Constant folding: %d\n", optimizer->constant_fold_count);
    printf("  Strength reduction: %d\n", optimizer->strength_reduce_count);
    printf("  Dead code elimination: %d\n", optimizer->dead_code_count);
    printf("  Common subexpression elimination: %d\n", optimizer->cse_count);
    printf("  Control flow optimization: %d\n", optimizer->control_flow_count);
}

int ir_optimizer_get_constant_fold_count(IROptimizer* optimizer) {
    return optimizer ? optimizer->constant_fold_count : 0;
}

int ir_optimizer_get_strength_reduce_count(IROptimizer* optimizer) {
    return optimizer ? optimizer->strength_reduce_count : 0;
}
