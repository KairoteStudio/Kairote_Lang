#ifndef KRT_KRO_CODEGEN_H
#define KRT_KRO_CODEGEN_H

#include "../../Middle/Ir/Ir.h"
#include "../../../Tools/KroWriter.h"
#include <stdio.h>

#define KRT_KRO_MAX_LOCAL_VARS 1024
#define KRT_KRO_MAX_TEMP_REGS 4096
#define KRT_KRO_TEMP_INDEX_SIZE (KRT_KRO_MAX_TEMP_REGS * 2)
#define KRT_KRO_MAX_ARGS 128
#define KRT_KRO_PACKED_REG_COUNT 5

typedef struct {
    int reg;
    int bit_offset;
    int bit_width;
} KROPackedField;

typedef struct {
    const char* name;
    int stack_offset;
    int allocated;
    bool address_taken;
    KrtTokenType type;
    KROPackedField packed;
} KROLocalVar;

typedef struct {
    int temp_index;
    int stack_offset;
    int valid;
    int reg_low;
    int reg_high;
    int first_position;
    int last_position;
    int use_count;
    int width;
    int cross_block;
    int skip_register;
    bool no_storage;
    bool low_word_only;
    bool early_tail_accumulate;
    bool deferred_tail_argument;
    KrtIRInst* tail_combine;
    KrtIRInst* tail_return;
    KrtIRBasicBlock* block;
} KROTempSlot;

typedef struct {
    KROWriter* writer;
    int optimization_level;
    FILE* output_file;
    char output_filename[256];
    int32_t* string_const_sym_indices;
    int string_const_count;
    int is_main_func;
    int epilogue_symbol;
    int local_var_count;
    int func_index;
    KROLocalVar local_vars[KRT_KRO_MAX_LOCAL_VARS];
    int current_stack_offset;
    KROTempSlot temp_slots[KRT_KRO_MAX_TEMP_REGS];
    int temp_slot_count;
    int temp_index[KRT_KRO_TEMP_INDEX_SIZE];
    int arg_stack_offsets[KRT_KRO_MAX_ARGS];
    bool arg_address_taken[KRT_KRO_MAX_ARGS];
    KROPackedField arg_packed[KRT_KRO_MAX_ARGS];
    int packed_reg_bits[KRT_KRO_PACKED_REG_COUNT];
    int packed_reg_save_offsets[KRT_KRO_PACKED_REG_COUNT];
    bool packed_reg_needs_zero[KRT_KRO_PACKED_REG_COUNT];
    int tail_site_count;
    KrtIRBasicBlock* rotated_tail_leaf;
    KrtIROpcode tail_acc_opcode;
    bool tail_register_argument;
    int tail_arg_offsets[KRT_KRO_MAX_ARGS];
    KrtIRFunction* current_function;
    int current_param_count;
    int current_function_id;
    const char* current_function_name;
    KrtIRBasicBlock* current_block;
    KrtIRInst* current_phi_inst;
    int phi_operand_index;
} KROCodegenContext;

/** @brief Emit a KRO object for module into output_file; output_filename names diagnostics. */
void KrtKrtGenerate(FILE* output_file, const char* output_filename, KrtIRModule* module);

#endif
