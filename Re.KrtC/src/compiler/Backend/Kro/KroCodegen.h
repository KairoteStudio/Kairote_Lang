#ifndef KRT_KRO_CODEGEN_H
#define KRT_KRO_CODEGEN_H

#include "../../Middle/Ir/Ir.h"
#include "../../../Tools/KroWriter.h"
#include <stdio.h>

#define KRO_MAX_LOCAL_VARS 256
#define KRO_MAX_TEMP_REGS 256
#define KRO_MAX_ARGS 16

typedef struct {
    char name[64];
    int stack_offset;
    int allocated;
} KROLocalVar;

typedef struct {
    int temp_index;
    int stack_offset;
    int valid;
} KROTempSlot;

typedef struct {
    KROWriter* writer;
    FILE* output_file;
    char output_filename[256];
    int32_t* string_const_sym_indices;
    int string_const_count;
    int is_main_func;
    int local_var_count;
    int func_index;
    KROLocalVar local_vars[KRO_MAX_LOCAL_VARS];
    int current_stack_offset;
    KROTempSlot temp_slots[KRO_MAX_TEMP_REGS];
    int temp_slot_count;
    int arg_stack_offsets[KRO_MAX_ARGS];
    int current_param_count;
    int current_function_id;
    const char* current_function_name;
    KrtIRBasicBlock* current_block;
    KrtIRInst* current_phi_inst;
    int phi_operand_index;
} KROCodegenContext;

void KrtKrtGenerate(FILE* output_file, const char* output_filename, KrtIRModule* module);

#endif
