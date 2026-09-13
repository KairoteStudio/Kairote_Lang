#ifndef KRT_X86_BACKEND_H
#define KRT_X86_BACKEND_H

#include <stdio.h>
#include "../../../Core/Target/KairoteTarget.h"
#include "../../Middle/Ir/Ir.h"

#define X86_MAX_REGISTERS 14
#define X86_MAX_VAR_OFFSETS 100
#define X86_MAX_VAR_NAME_LEN 64
#define X86_MAX_TEMP_SPACE 256
#define X86_REGISTER_COUNT 14
#define X86_MAX_LABEL_LEN 256
#define X86_STACK_ALIGNMENT KRT_STACK_ALIGNMENT
#define X86_SHADOW_SPACE KRT_SHADOW_SPACE_SIZE
#define X86_MIN_STACK_SIZE 48
#define X86_TEMP_INITIAL_CAPACITY 256
#define X86_TEMP_GROWTH_FACTOR 2

typedef enum { TEMP_LOC_NONE, TEMP_LOC_REGISTER, TEMP_LOC_STACK } TempLocationType;

typedef struct {
    TempLocationType type;
    union {
        const char* reg;
        int offset;
    };
} TempLocation;

typedef struct {
    const char* name;
    int is_free;
    const char* content;
} RegisterState;

typedef struct {
    TempLocation* locations;
    int count;
    int capacity;
    int* usage;
} TempLocationArray;

typedef struct {
    KrtIRFunction* current_func;
    KrtIRModule* current_module;
    TempLocationArray temp_locations;
    RegisterState registers[X86_MAX_REGISTERS];
    int stack_size;
    int temp_stack_base;
    struct {
        char name[X86_MAX_VAR_NAME_LEN];
        int offset;
    } var_offsets[X86_MAX_VAR_OFFSETS];
    int var_count;
    int next_var_offset;
    void* regalloc;
} CodegenContext;

/** @brief Emit x86 assembly for module into output. */
void KrtX86Generate(FILE* output, KrtIRModule* module);

/** @brief Initialize register and stack bookkeeping for func. */
void codegen_context_init(CodegenContext* ctx, KrtIRFunction* func, KrtIRModule* module);
/** @brief Release storage owned by ctx. */
void codegen_context_destroy(CodegenContext* ctx);
/** @brief Return mutable storage information for temp_idx. */
TempLocation* codegen_get_temp_location(CodegenContext* ctx, int temp_idx);
/** @brief Record that temp_idx is available in reg. */
void codegen_set_temp_in_register(CodegenContext* ctx, int temp_idx, const char* reg);
/** @brief Record the frame offset containing temp_idx. */
void codegen_set_temp_on_stack(CodegenContext* ctx, int temp_idx, int offset);
/** @brief Reserve a free register, or return NULL when all are occupied. */
const char* codegen_alloc_register(CodegenContext* ctx);
/** @brief Mark reg as available for subsequent allocation. */
void codegen_free_register(CodegenContext* ctx, const char* reg);
/** @brief Find or assign a frame offset for var_name. */
int codegen_get_var_offset(CodegenContext* ctx, const char* var_name);

/** @brief Emit a load of temp_idx into target_reg. */
void codegen_emit_load_temp(FILE* output, CodegenContext* ctx, int temp_idx, const char* target_reg);
/** @brief Emit storage of source_reg as temp_idx. */
void codegen_emit_store_temp(FILE* output, CodegenContext* ctx, int temp_idx, const char* source_reg);
/** @brief Emit a load of var_name into target_reg. */
void codegen_emit_load_var(FILE* output, CodegenContext* ctx, const char* var_name, const char* target_reg);
/** @brief Emit storage of source_reg into var_name. */
void codegen_emit_store_var(FILE* output, CodegenContext* ctx, const char* var_name, const char* source_reg);

#endif
