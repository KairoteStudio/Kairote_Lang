#include "X86CodeOpt.h"
#include <string.h>
#include <stdlib.h>

typedef struct {
    char* pattern;
    char* replacement;
    int size;
} PeepholePattern;

#define PEEPHOLE_WINDOW_SIZE 4

typedef struct X86PeepholeOptimizer {
    PeepholePattern* patterns;
    int pattern_count;
    int pattern_capacity;
    int optimization_count;
    int window_size;
} X86PeepholeOptimizer;

X86PeepholeOptimizer* x86_peephole_optimizer_create(void) {
    X86PeepholeOptimizer* optimizer = (X86PeepholeOptimizer*)KRT_CALLOC(1, sizeof(X86PeepholeOptimizer));
    if (!optimizer) return NULL;

    optimizer->pattern_capacity = 32;
    optimizer->patterns = (PeepholePattern*)KRT_CALLOC(optimizer->pattern_capacity, sizeof(PeepholePattern));
    if (!optimizer->patterns) {
        KRT_FREE(optimizer);
        return NULL;
    }

    optimizer->window_size = PEEPHOLE_WINDOW_SIZE;
    return optimizer;
}

void x86_peephole_optimizer_destroy(X86PeepholeOptimizer* optimizer) {
    if (!optimizer) return;

    for (int i = 0; i < optimizer->pattern_count; i++) {
        KRT_FREE(optimizer->patterns[i].pattern);
        KRT_FREE(optimizer->patterns[i].replacement);
    }
    KRT_FREE(optimizer->patterns);
    KRT_FREE(optimizer);
}

typedef struct {
    KrtIROpcode opcode;
    KrtIRValue operands[3];
    int operand_count;
    KrtIRValue result;
} InstructionPattern;

static bool match_mov_mov(KrtIRInst* inst1, KrtIRInst* inst2) {
    if (!inst1 || !inst2) return false;
    
    if (inst1->opcode == KRT_IR_COPY && inst2->opcode == KRT_IR_COPY) {
        if (inst1->operand_count == 1 && inst2->operand_count == 1) {
            if (inst1->result.type == KRT_IR_VALUE_TEMP &&
                inst2->operands[0].type == KRT_IR_VALUE_TEMP &&
                inst1->result.data.index == inst2->operands[0].data.index) {
                return true;
            }
        }
    }
    return false;
}

static bool match_redundant_load_store(KrtIRInst* inst1, KrtIRInst* inst2) {
    if (!inst1 || !inst2) return false;
    
    if (inst1->opcode == KRT_IR_STORE && inst2->opcode == KRT_IR_LOAD) {
        if (inst1->operand_count >= 2 && inst2->operand_count >= 1) {
            if (inst1->operands[0].type == KRT_IR_VALUE_VAR &&
                inst2->operands[0].type == KRT_IR_VALUE_VAR &&
                strcmp(inst1->operands[0].data.name, inst2->operands[0].data.name) == 0) {
                return true;
            }
        }
    }
    return false;
}

static bool match_algebraic_identity(KrtIRInst* inst) {
    if (!inst) return false;
    
    switch (inst->opcode) {
        case KRT_IR_ADD:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 0) {
                return true;
            }
            break;
            
        case KRT_IR_MUL:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 1) {
                return true;
            }
            break;
            
        case KRT_IR_SUB:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 0) {
                return true;
            }
            break;
            
        case KRT_IR_DIV:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 1) {
                return true;
            }
            break;
            
        case KRT_IR_AND:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == -1) {
                return true;
            }
            break;
            
        case KRT_IR_OR:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 0) {
                return true;
            }
            break;
            
        case KRT_IR_XOR:
            if (inst->operand_count == 2 &&
                inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 0) {
                return true;
            }
            break;
            
        default:
            break;
    }
    
    return false;
}

static bool match_algebraic_zero_result(KrtIRInst* inst) {
    if (!inst || inst->operand_count < 2) return false;
    
    switch (inst->opcode) {
        case KRT_IR_MUL:
            if (inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 0) {
                return true;
            }
            break;
            
        case KRT_IR_AND:
            if (inst->operands[1].type == KRT_IR_VALUE_IMM &&
                inst->operands[1].data.imm == 0) {
                return true;
            }
            break;
            
        case KRT_IR_SUB: {
            bool same_operand = false;
            if (inst->operands[0].type == KRT_IR_VALUE_TEMP &&
                inst->operands[1].type == KRT_IR_VALUE_TEMP &&
                inst->operands[0].data.index == inst->operands[1].data.index) {
                same_operand = true;
            } else if (inst->operands[0].type == KRT_IR_VALUE_VAR &&
                       inst->operands[1].type == KRT_IR_VALUE_VAR &&
                       strcmp(inst->operands[0].data.name, inst->operands[1].data.name) == 0) {
                same_operand = true;
            }
            if (same_operand) return true;
            break;
        }
        
        case KRT_IR_XOR: {
            bool same_operand = false;
            if (inst->operands[0].type == KRT_IR_VALUE_TEMP &&
                inst->operands[1].type == KRT_IR_VALUE_TEMP &&
                inst->operands[0].data.index == inst->operands[1].data.index) {
                same_operand = true;
            } else if (inst->operands[0].type == KRT_IR_VALUE_VAR &&
                       inst->operands[1].type == KRT_IR_VALUE_VAR &&
                       strcmp(inst->operands[0].data.name, inst->operands[1].data.name) == 0) {
                same_operand = true;
            }
            if (same_operand) return true;
            break;
        }
        
        default:
            break;
    }
    
    return false;
}

static bool match_negation_pattern(KrtIRInst* inst) {
    if (!inst || inst->opcode != KRT_IR_MUL || inst->operand_count < 2) return false;
    
    if (inst->operands[1].type == KRT_IR_VALUE_IMM &&
        inst->operands[1].data.imm == -1) {
        return true;
    }
    
    return false;
}

static bool match_strength_reduction_candidate(KrtIRInst* inst) {
    if (!inst) return false;
    
    if (inst->opcode == KRT_IR_MUL && inst->operand_count == 2) {
        if (inst->operands[1].type == KRT_IR_VALUE_IMM) {
            double val = inst->operands[1].data.imm;
            if (val == 2 || val == 4 || val == 8 || val == 16 ||
                val == 32 || val == 64 || val == 128) {
                return true;
            }
        }
    }
    
    return false;
}

static bool apply_mov_mov_elimination(KrtIRBasicBlock* block, KrtIRInst* inst1, KrtIRInst* inst2) {
    (void)block;
    if (!match_mov_mov(inst1, inst2)) return false;

    inst2->operands[0] = inst1->operands[0];
    return true;
}

static bool apply_redundant_load_store_elimination(KrtIRBasicBlock* block, KrtIRInst* inst1, KrtIRInst* inst2) {
    (void)block;
    if (!match_redundant_load_store(inst1, inst2)) return false;

    inst2->opcode = KRT_IR_COPY;
    inst2->operands[0] = inst1->operands[1];
    inst2->operand_count = 1;
    return true;
}

static bool apply_algebraic_identity(KrtIRInst* inst) {
    if (!match_algebraic_identity(inst)) return false;
    
    switch (inst->opcode) {
        case KRT_IR_ADD:
        case KRT_IR_SUB:
        case KRT_IR_MUL:
        case KRT_IR_DIV:
        case KRT_IR_AND:
        case KRT_IR_OR:
        case KRT_IR_XOR:
            inst->opcode = KRT_IR_COPY;
            inst->operand_count = 1;
            return true;
            
        default:
            return false;
    }
}

static bool apply_algebraic_zero_result(KrtIRInst* inst) {
    if (!match_algebraic_zero_result(inst)) return false;
    
    inst->opcode = KRT_IR_COPY;
    inst->operand_count = 1;
    inst->operands[0].type = KRT_IR_VALUE_IMM;
    inst->operands[0].data.imm = 0;
    return true;
}

static bool apply_negation(KrtIRInst* inst) {
    if (!match_negation_pattern(inst)) return false;
    
    inst->opcode = KRT_IR_SUB;
    KrtIRValue zero = {0};
    zero.type = KRT_IR_VALUE_IMM;
    zero.data.imm = 0;
    inst->operands[1] = inst->operands[0];
    inst->operands[0] = zero;
    return true;
}

static bool apply_strength_reduction(KrtIRInst* inst) {
    if (!match_strength_reduction_candidate(inst)) return false;
    
    double val = inst->operands[1].data.imm;
    int shift_amount = 0;
    
    switch ((int)val) {
        case 2: shift_amount = 1; break;
        case 4: shift_amount = 2; break;
        case 8: shift_amount = 3; break;
        case 16: shift_amount = 4; break;
        case 32: shift_amount = 5; break;
        case 64: shift_amount = 6; break;
        case 128: shift_amount = 7; break;
        default: return false;
    }
    
    inst->opcode = KRT_IR_LSHIFT;
    inst->operands[1].data.imm = shift_amount;
    return true;
}

static bool match_mov_mov_chain(KrtIRInst* inst1, KrtIRInst* inst2, KrtIRInst* inst3) {
    if (!inst1 || !inst2 || !inst3) return false;
    
    if (inst1->opcode == KRT_IR_COPY && 
        inst2->opcode == KRT_IR_COPY &&
        inst3->opcode == KRT_IR_COPY) {
        if (inst1->operand_count == 1 && 
            inst2->operand_count == 1 && 
            inst3->operand_count == 1) {
            if (inst1->result.type == KRT_IR_VALUE_TEMP &&
                inst2->operands[0].type == KRT_IR_VALUE_TEMP &&
                inst1->result.data.index == inst2->operands[0].data.index &&
                inst2->result.type == KRT_IR_VALUE_TEMP &&
                inst3->operands[0].type == KRT_IR_VALUE_TEMP &&
                inst2->result.data.index == inst3->operands[0].data.index) {
                return true;
            }
        }
    }
    return false;
}

static bool apply_mov_mov_chain_elimination(KrtIRBasicBlock* block, KrtIRInst* inst1, KrtIRInst* inst2, KrtIRInst* inst3) {
    (void)block;
    if (!match_mov_mov_chain(inst1, inst2, inst3)) return false;

    inst3->operands[0] = inst1->operands[0];
    inst2->opcode = KRT_IR_NOP;
    inst2->operand_count = 0;
    return true;
}

static bool optimize_basic_block(X86PeepholeOptimizer* optimizer, KrtIRBasicBlock* block) {
    if (!block || !block->first_inst) return false;
    
    bool changed = false;
    int window = optimizer->window_size;
    KrtIRInst* inst = block->first_inst;
    
    while (inst) {
        KrtIRInst* window_insts[PEEPHOLE_WINDOW_SIZE] = {0};
        int count = 0;
        KrtIRInst* current = inst;
        
        while (current && count < window) {
            window_insts[count++] = current;
            current = current->next;
        }
        
        if (count >= 2) {
            if (apply_mov_mov_elimination(block, window_insts[0], window_insts[1])) {
                optimizer->optimization_count++;
                changed = true;
            }
            else if (apply_redundant_load_store_elimination(block, window_insts[0], window_insts[1])) {
                optimizer->optimization_count++;
                changed = true;
            }
        }
        
        if (count >= 3 && !changed) {
            if (apply_mov_mov_chain_elimination(block, window_insts[0], window_insts[1], window_insts[2])) {
                optimizer->optimization_count++;
                changed = true;
            }
        }
        
        if (window_insts[0]) {
            if (apply_algebraic_identity(window_insts[0])) {
                optimizer->optimization_count++;
                changed = true;
            }
            else if (apply_algebraic_zero_result(window_insts[0])) {
                optimizer->optimization_count++;
                changed = true;
            }
            else if (apply_negation(window_insts[0])) {
                optimizer->optimization_count++;
                changed = true;
            }
            else if (apply_strength_reduction(window_insts[0])) {
                optimizer->optimization_count++;
                changed = true;
            }
        }
        
        inst = inst ? inst->next : NULL;
    }
    
    return changed;
}

void x86_optimize_peephole(KrtIRModule* module, int optimization_level) {
    if (!module || optimization_level <= 0) return;
    
    X86PeepholeOptimizer* optimizer = x86_peephole_optimizer_create();
    if (!optimizer) return;
    
    int max_passes = (optimization_level >= 2) ? 5 : 3;
    int pass = 0;
    bool changed = true;
    
    while (changed && pass < max_passes) {
        changed = false;
        pass++;
        
        KrtIRFunction* func = module->functions;
        while (func) {
            KrtIRBasicBlock* block = func->entry_block;
            while (block) {
                changed |= optimize_basic_block(optimizer, block);
                block = block->next;
            }
            func = func->next;
        }
    }
    
    x86_peephole_optimizer_destroy(optimizer);
}

int x86_peephole_get_optimization_count(X86PeepholeOptimizer* optimizer) {
    return optimizer ? optimizer->optimization_count : 0;
}