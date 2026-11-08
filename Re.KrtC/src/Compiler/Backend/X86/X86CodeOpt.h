
#ifndef KRT_X86_CODEOPT_H
#define KRT_X86_CODEOPT_H

#include "../../Middle/Ir/Ir.h"

typedef struct X86PeepholeOptimizer X86PeepholeOptimizer;

/** @brief Allocate an empty peephole optimizer, or return NULL on allocation failure. */
X86PeepholeOptimizer* x86_peephole_optimizer_create(void);
/** @brief Release optimizer, accepting NULL. */
void x86_peephole_optimizer_destroy(X86PeepholeOptimizer* optimizer);
/** @brief Apply enabled peephole transformations to module. */
void x86_optimize_peephole(KrtIRModule* module, int optimization_level);
/** @brief Return the number of transformations performed by optimizer. */
int x86_peephole_get_optimization_count(X86PeepholeOptimizer* optimizer);

#endif
