#ifndef KRT_VM_CODEGEN_H
#define KRT_VM_CODEGEN_H

#include "../../Middle/Ir/Ir.h"
#include "Bytecode.h"
#include "BytecodeGenerator.h"

/** @brief Initialize chunk and emit the module main function as VM bytecode. */
void KrtVmCodegenGenerate(KrtIRModule* ir_module, KrtChunk* chunk);

#endif
