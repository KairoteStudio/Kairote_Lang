#ifndef KRT_COMPILER_H
#define KRT_COMPILER_H

#include "../../Core/Utils/KrtCommon.h"
#include <stdio.h>
#include "../Frontend/Parser/Ast.h"
#include "../Middle/Ir/Ir.h"

#define KRT_DEBUG_IR(fmt, ...) KRT_DEBUG_LOG("IR", fmt, ##__VA_ARGS__)

typedef enum {
    KRT_TARGET_X86_ASM,
    KRT_TARGET_WASM,
    KRT_TARGET_IR_TEXT,
    KRT_TARGET_VM_BYTECODE,
    KRT_TARGET_KRO_OBJ,
    KRT_TARGET_EXE_PLATFORM     
} KrtTargetPlatform;

#include "Bytecode.h"

typedef struct {
    FILE* output_file;
    KrtTargetPlatform target;
    KrtChunk last_chunk;
    char output_filename[256]; 
} KrtCompiler;

KrtCompiler* KrtCompilerCreate(const char* output_filename, KrtTargetPlatform target);
void KrtCompilerDestroy(KrtCompiler* compiler);

void KrtCompilerCompile(KrtCompiler* compiler, ASTNode* ast, void* semantic_analyzer);

#endif
