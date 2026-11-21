#ifndef KRT_ARM64_CODEGEN_H
#define KRT_ARM64_CODEGEN_H

#include "../../../Middle/Ir/Ir.h"
#include "../../../../Tools/KroWriter.h"
#include <stdio.h>

/** @brief Emit an AArch64 KRO object for module into output_file. */
void KrtArm64Generate(FILE* output_file, const char* output_filename, KrtIRModule* module);

#endif
