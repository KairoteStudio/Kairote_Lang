#ifndef KRT_IR_OPTIMIZER_H
#define KRT_IR_OPTIMIZER_H

#include "Ir.h"
#include <stdbool.h>

enum {
    OPT_CONSTANT_FOLDING = 1 << 0,
    OPT_CONSTANT_PROPAGATION = 1 << 1,
    OPT_DEAD_CODE_ELIMINATION = 1 << 2,
    OPT_COMMON_SUBEXPRESSION_ELIMINATION = 1 << 3,
    OPT_COPY_PROPAGATION = 1 << 4,
    OPT_LOOP_INVARIANT_CODE_MOTION = 1 << 5,
    OPT_STRENGTH_REDUCTION = 1 << 6,
    OPT_FUNCTION_INLINING = 1 << 7,
    OPT_CONTROL_FLOW_OPTIMIZATION = 1 << 8,
    OPT_ESCAPE_ANALYSIS = 1 << 9,
    OPT_ALL = 0xFFFFFFFF
};

typedef unsigned int OptimizationFlags;

typedef struct Loop {
    int header_id;
    int* body_blocks;
    int body_count;
} Loop;

typedef struct {
    Loop* loops;
    int count;
} LoopInfo;

typedef struct {
    int optimization_count;
    double time_spent;
    int constant_fold_count;
    int strength_reduce_count;
    int dead_code_count;
    int cse_count;
    int loop_invariant_count;
    int function_inline_count;
    int control_flow_count;
    int escape_analysis_count;
} IROptimizer;

IROptimizer* ir_optimizer_create(void);
void ir_optimizer_destroy(IROptimizer* optimizer);

void ir_optimize_module(IROptimizer* optimizer, KrtIRModule* module, OptimizationFlags flags);

bool ir_optimize_constant_folding(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_constant_propagation(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_dead_code_elimination(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_common_subexpression_elimination(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_copy_propagation(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_loop_invariant_code_motion(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_strength_reduction(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_function_inlining(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_control_flow(KrtIRModule* module, IROptimizer* stats);
bool ir_optimize_escape_analysis(KrtIRModule* module, IROptimizer* stats);

void ir_optimizer_print_stats(IROptimizer* optimizer);
int ir_optimizer_get_total_optimizations(IROptimizer* optimizer);
double ir_optimizer_get_time_spent(IROptimizer* optimizer);

int ir_optimizer_get_constant_fold_count(IROptimizer* optimizer);
int ir_optimizer_get_strength_reduce_count(IROptimizer* optimizer);
int ir_optimizer_get_dead_code_count(IROptimizer* optimizer);
int ir_optimizer_get_cse_count(IROptimizer* optimizer);
int ir_optimizer_get_loop_invariant_count(IROptimizer* optimizer);
int ir_optimizer_get_function_inline_count(IROptimizer* optimizer);
int ir_optimizer_get_control_flow_count(IROptimizer* optimizer);
int ir_optimizer_get_escape_analysis_count(IROptimizer* optimizer);

#endif
