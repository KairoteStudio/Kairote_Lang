#include "X86Codegen.h"
#include "X86RegAlloc.h"
#include "../../../Core/Utils/KrtCommon.h"
#include "../../Middle/Ir/IrSsa.h"
#include <string.h>

static const char* g_register_names[] = {
    "rax", "rbx", "rcx", "rdx",
    "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"
};

static int g_register_count = X86_REGISTER_COUNT;

#ifdef __linux__
static const char* g_arg_regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static const int g_arg_reg_count = 6;
#else
static const char* g_arg_regs[] = {"rcx", "rdx", "r8", "r9"};
static const int g_arg_reg_count = 4;
#endif

static int calculate_stack_size(KrtIRFunction* func) {
    int stack_size = X86_SHADOW_SPACE;
    int max_temp_idx = 0;

    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        KrtIRInst* inst = block->first_inst;
        while (inst) {
            for (int i = 0; i < inst->operand_count; i++) {
                if (inst->operands[i].type == KRT_IR_VALUE_TEMP) {
                    int idx = inst->operands[i].data.index;
                    if (idx > max_temp_idx) max_temp_idx = idx;
                }
            }
            if (inst->result.type == KRT_IR_VALUE_TEMP) {
                int idx = inst->result.data.index;
                if (idx > max_temp_idx) max_temp_idx = idx;
            }
            inst = inst->next;
        }
        block = block->next;
    }

    int temp_space = (max_temp_idx + 1) * 8;
    if (temp_space > X86_MAX_TEMP_SPACE) temp_space = X86_MAX_TEMP_SPACE;

    stack_size += func->param_count * 8 + temp_space;
    stack_size = (stack_size + (X86_STACK_ALIGNMENT - 1)) & ~(X86_STACK_ALIGNMENT - 1);

    if (stack_size < X86_MIN_STACK_SIZE) stack_size = X86_MIN_STACK_SIZE;

    return stack_size;
}

static void temp_array_init(TempLocationArray* arr, int initial_capacity) {
    arr->locations = (TempLocation*)calloc(initial_capacity, sizeof(TempLocation));
    arr->usage = (int*)calloc(initial_capacity, sizeof(int));
    arr->count = 0;
    arr->capacity = initial_capacity;
}

static void temp_array_ensure_capacity(TempLocationArray* arr, int needed) {
    if (needed < arr->capacity) return;
    int new_capacity = arr->capacity * X86_TEMP_GROWTH_FACTOR;
    while (new_capacity <= needed) {
        new_capacity *= X86_TEMP_GROWTH_FACTOR;
    }
    TempLocation* new_locs = (TempLocation*)realloc(arr->locations, new_capacity * sizeof(TempLocation));
    int* new_usage = (int*)realloc(arr->usage, new_capacity * sizeof(int));
    if (new_locs) {
        memset(new_locs + arr->capacity, 0, (new_capacity - arr->capacity) * sizeof(TempLocation));
    }
    if (new_usage) {
        memset(new_usage + arr->capacity, 0, (new_capacity - arr->capacity) * sizeof(int));
    }
    arr->locations = new_locs;
    arr->usage = new_usage;
    arr->capacity = new_capacity;
}

static void temp_array_destroy(TempLocationArray* arr) {
    free(arr->locations);
    free(arr->usage);
    arr->locations = NULL;
    arr->usage = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

void codegen_context_init(CodegenContext* ctx, KrtIRFunction* func, KrtIRModule* module) {
    if (!ctx || !func) {
        return;
    }

    memset(ctx, 0, sizeof(CodegenContext));
    ctx->current_func = func;
    ctx->current_module = module;

    temp_array_init(&ctx->temp_locations, X86_TEMP_INITIAL_CAPACITY);

    for (int i = 0; i < g_register_count; i++) {
        ctx->registers[i].name = g_register_names[i];
        ctx->registers[i].is_free = 1;
        ctx->registers[i].content = NULL;
    }

    ctx->registers[0].is_free = 0;

    RegAllocResult* regalloc = x86_allocate_registers(func);
    ctx->regalloc = regalloc;

    if (regalloc) {
        int reg_count = 0;
        int stack_count = 0;
        int max_temp = 0;

        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                for (int i = 0; i < inst->operand_count; i++) {
                    if (inst->operands[i].type == KRT_IR_VALUE_TEMP) {
                        int idx = inst->operands[i].data.index;
                        if (idx > max_temp) max_temp = idx;
                    }
                }
                if (inst->result.type == KRT_IR_VALUE_TEMP) {
                    int idx = inst->result.data.index;
                    if (idx > max_temp) max_temp = idx;
                }
                inst = inst->next;
            }
            block = block->next;
        }

        temp_array_ensure_capacity(&ctx->temp_locations, max_temp + 1);

        for (int i = 0; i <= max_temp; i++) {
            const char* reg = regalloc_get_reg_name(regalloc, i);
            if (reg) {
                codegen_set_temp_in_register(ctx, i, reg);
                reg_count++;
            } else {
                int stack_offset = regalloc_get_stack_offset(regalloc, i);
                if (stack_offset > 0) {
                    codegen_set_temp_on_stack(ctx, i, stack_offset);
                    stack_count++;
                }
            }
        }
    }

    ctx->stack_size = calculate_stack_size(func);
    ctx->temp_stack_base = func->param_count * 8 + 32;  
}

void codegen_context_destroy(CodegenContext* ctx) {
    if (!ctx) return;
    temp_array_destroy(&ctx->temp_locations);
    if (ctx->regalloc) {
        x86_regalloc_destroy((RegAllocResult*)ctx->regalloc);
        ctx->regalloc = NULL;
    }
}

TempLocation* codegen_get_temp_location(CodegenContext* ctx, int temp_idx) {
    if (temp_idx < 0 || temp_idx >= ctx->temp_locations.capacity) return NULL;
    return &ctx->temp_locations.locations[temp_idx];
}

void codegen_set_temp_in_register(CodegenContext* ctx, int temp_idx, const char* reg) {
    if (temp_idx < 0 || temp_idx >= ctx->temp_locations.capacity) return;
    ctx->temp_locations.locations[temp_idx].type = TEMP_LOC_REGISTER;
    ctx->temp_locations.locations[temp_idx].reg = reg;
}

void codegen_set_temp_on_stack(CodegenContext* ctx, int temp_idx, int offset) {
    if (temp_idx < 0 || temp_idx >= ctx->temp_locations.capacity) return;
    ctx->temp_locations.locations[temp_idx].type = TEMP_LOC_STACK;
    ctx->temp_locations.locations[temp_idx].offset = offset;
}

const char* codegen_alloc_register(CodegenContext* ctx) {
    if (!ctx) return NULL;
    
    for (int i = 12; i < g_register_count; i++) {
        if (ctx->registers[i].is_free) {
            ctx->registers[i].is_free = 0;
            return ctx->registers[i].name;
        }
    }
    
    for (int i = 10; i <= 11; i++) {
        if (ctx->registers[i].is_free) {
            ctx->registers[i].is_free = 0;
            return ctx->registers[i].name;
        }
    }
    
    return NULL;
}

void codegen_free_register(CodegenContext* ctx, const char* reg) {
    for (int i = 0; i < g_register_count; i++) {
        if (strcmp(ctx->registers[i].name, reg) == 0) {
            ctx->registers[i].is_free = 1;
            ctx->registers[i].content = NULL;
            return;
        }
    }
}

int codegen_get_var_offset(CodegenContext* ctx, const char* var_name) {
    if (!ctx || !var_name) return 0;
    
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->var_offsets[i].name, var_name) == 0) {
            return ctx->var_offsets[i].offset;
        }
    }
    
    if (ctx->var_count < X86_MAX_VAR_OFFSETS) {
        size_t len = strlen(var_name);
        if (len >= X86_MAX_VAR_NAME_LEN) len = X86_MAX_VAR_NAME_LEN - 1;
        memcpy(ctx->var_offsets[ctx->var_count].name, var_name, len);
        ctx->var_offsets[ctx->var_count].name[len] = '\0';
        ctx->next_var_offset += 8;
        ctx->var_offsets[ctx->var_count].offset = ctx->next_var_offset;
        return ctx->var_offsets[ctx->var_count++].offset;
    }
    
    return 0;
}

void codegen_emit_load_temp(FILE* output, CodegenContext* ctx, int temp_idx, const char* target_reg) {
    TempLocation* loc = codegen_get_temp_location(ctx, temp_idx);
    if (!loc) return;
    
    switch (loc->type) {
        case TEMP_LOC_REGISTER:
            if (strcmp(loc->reg, target_reg) != 0) {
                fprintf(output, "    mov %s, %s\n", target_reg, loc->reg);
            }
            break;
        case TEMP_LOC_STACK:
            fprintf(output, "    mov %s, [rbp - %d]\n", target_reg, loc->offset);
            break;
        case TEMP_LOC_NONE:
        default:
            break;
    }
}

void codegen_emit_store_temp(FILE* output, CodegenContext* ctx, int temp_idx, const char* source_reg) {
    TempLocation* loc = codegen_get_temp_location(ctx, temp_idx);
    if (!loc) return;
    
    switch (loc->type) {
        case TEMP_LOC_REGISTER:
            if (strcmp(loc->reg, source_reg) != 0) {
                fprintf(output, "    mov %s, %s\n", loc->reg, source_reg);
            }
            break;
        case TEMP_LOC_STACK:
            fprintf(output, "    mov [rbp - %d], %s\n", loc->offset, source_reg);
            break;
        case TEMP_LOC_NONE:
            
            codegen_set_temp_on_stack(ctx, temp_idx, ctx->temp_stack_base + temp_idx * 8);
            fprintf(output, "    mov [rbp - %d], %s\n", ctx->temp_stack_base + temp_idx * 8, source_reg);
            break;
    }
}

void codegen_emit_load_var(FILE* output, CodegenContext* ctx, const char* var_name, const char* target_reg) {
    int offset = codegen_get_var_offset(ctx, var_name);
    fprintf(output, "    mov %s, [rbp - %d]\n", target_reg, offset);
}

void codegen_emit_store_var(FILE* output, CodegenContext* ctx, const char* var_name, const char* source_reg) {
    int offset = codegen_get_var_offset(ctx, var_name);
    fprintf(output, "    mov [rbp - %d], %s\n", offset, source_reg);
}

typedef struct {
    FILE* output;
    CodegenContext* ctx;
} Emitter;

static int spill_rcx_if_used(Emitter* em) {
    if (!em || !em->ctx) return 0;

    for (int i = 0; i < em->ctx->temp_locations.capacity; i++) {
        TempLocation* loc = &em->ctx->temp_locations.locations[i];
        if (loc->type == TEMP_LOC_REGISTER && loc->reg && strcmp(loc->reg, "rcx") == 0) {
            fprintf(em->output, "    push rcx\n");
            return 1;
        }
    }
    return 0;
}

static void restore_rcx_if_needed(Emitter* em, int was_spilled) {
    if (!em || !was_spilled) return;
    fprintf(em->output, "    pop rcx\n");
}

static void emit_load_value(Emitter* em, KrtIRValue* value, const char* target_reg) {
    switch (value->type) {
        case KRT_IR_VALUE_IMM: {
            long long imm = (long long)value->data.imm;
            if (imm == 0) {
                fprintf(em->output, "    xor %s, %s\n", target_reg, target_reg);
            } else {
                fprintf(em->output, "    mov %s, %lld\n", target_reg, imm);
            }
            break;
        }
        case KRT_IR_VALUE_VAR:
            codegen_emit_load_var(em->output, em->ctx, value->data.name, target_reg);
            break;
        case KRT_IR_VALUE_TEMP:
            codegen_emit_load_temp(em->output, em->ctx, value->data.index, target_reg);
            break;
        case KRT_IR_VALUE_ARG: {
            
            int arg_idx = value->data.index;
            if (arg_idx >= 0 && arg_idx < em->ctx->current_func->param_count &&
                em->ctx->current_func->params && em->ctx->current_func->params[arg_idx].name) {
                const char* param_name = em->ctx->current_func->params[arg_idx].name;
                int offset = codegen_get_var_offset(em->ctx, param_name);
                fprintf(em->output, "    mov %s, [rbp - %d]\n", target_reg, offset);
            }
            break;
        }
        case KRT_IR_VALUE_STRING_CONST: {
            int str_id = value->data.string_const_id;
            fprintf(em->output, "    lea %s, [rel string_const_%d]\n", target_reg, str_id);
            break;
        }
        default:
            break;
    }
}

static void emit_store_result(Emitter* em, KrtIRValue* result, const char* source_reg) {
    switch (result->type) {
        case KRT_IR_VALUE_VAR:
            codegen_emit_store_var(em->output, em->ctx, result->data.name, source_reg);
            break;
        case KRT_IR_VALUE_TEMP:
            
            codegen_emit_store_temp(em->output, em->ctx, result->data.index, source_reg);
            break;
        default:
            break;
    }
}

static void sanitize_label_name(const char* name, char* out, size_t out_size) {
    if (!name || !out || out_size == 0) return;

    size_t j = 0;
    for (size_t i = 0; name[i] && j < out_size - 1; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '$' || c == '.') {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

static void emit_binary_op(Emitter* em, KrtIROpcode op, KrtIRValue* lhs, KrtIRValue* rhs, KrtIRValue* result) {
    if (!em || !lhs || !rhs || !result) return;

    emit_load_value(em, lhs, "rax");
    emit_load_value(em, rhs, "r8");

    char safe_name[256];
    sanitize_label_name(em->ctx->current_func->name, safe_name, sizeof(safe_name));

    switch (op) {
        case KRT_IR_ADD: fprintf(em->output, "    add rax, r8\n"); break;
        case KRT_IR_SUB: fprintf(em->output, "    sub rax, r8\n"); break;
        case KRT_IR_MUL: fprintf(em->output, "    imul rax, r8\n"); break;
        case KRT_IR_DIV:
            fprintf(em->output, "    test r8, r8\n");
            fprintf(em->output, "    jz .Ldiv_by_zero_%s\n", safe_name);
            fprintf(em->output, "    xor rdx, rdx\n");
            fprintf(em->output, "    idiv r8\n");
            break;
        case KRT_IR_MOD:
            fprintf(em->output, "    test r8, r8\n");
            fprintf(em->output, "    jz .Ldiv_by_zero_%s\n", safe_name);
            fprintf(em->output, "    xor rdx, rdx\n");
            fprintf(em->output, "    idiv r8\n");
            fprintf(em->output, "    mov rax, rdx\n");
            break;
        case KRT_IR_AND: fprintf(em->output, "    and rax, r8\n"); break;
        case KRT_IR_OR:  fprintf(em->output, "    or rax, r8\n"); break;
        case KRT_IR_XOR: fprintf(em->output, "    xor rax, r8\n"); break;
        default:
            break;
    }

    emit_store_result(em, result, "rax");
}

static void emit_compare(Emitter* em, KrtIROpcode op, KrtIRValue* lhs, KrtIRValue* rhs, KrtIRValue* result) {
    if (!em || !lhs || !rhs || !result) return;

    emit_load_value(em, lhs, "rax");
    emit_load_value(em, rhs, "r9");

    fprintf(em->output, "    cmp rax, r9\n");
    
    switch (op) {
        case KRT_IR_EQ: fprintf(em->output, "    sete al\n"); break;
        case KRT_IR_NE: fprintf(em->output, "    setne al\n"); break;
        case KRT_IR_LT: fprintf(em->output, "    setl al\n"); break;
        case KRT_IR_LE: fprintf(em->output, "    setle al\n"); break;
        case KRT_IR_GT: fprintf(em->output, "    setg al\n"); break;
        case KRT_IR_GE: fprintf(em->output, "    setge al\n"); break;
        default:
            break;
    }

    fprintf(em->output, "    movzx rax, al\n");
    
    emit_store_result(em, result, "rax");
}

static void emit_load(Emitter* em, KrtIRValue* var, KrtIRValue* result) {
    if (var->type != KRT_IR_VALUE_VAR) return;
    
    codegen_emit_load_var(em->output, em->ctx, var->data.name, "rax");
    emit_store_result(em, result, "rax");
}

static void emit_store(Emitter* em, KrtIRValue* var, KrtIRValue* value) {
    if (var->type != KRT_IR_VALUE_VAR) return;
    
    emit_load_value(em, value, "rax");
    codegen_emit_store_var(em->output, em->ctx, var->data.name, "rax");
}

static void emit_branch(Emitter* em, KrtIRValue* cond, const char* true_label, const char* false_label) {
    emit_load_value(em, cond, "rax");
    fprintf(em->output, "    test rax, rax\n");
    fprintf(em->output, "    jnz %s\n", true_label);
    fprintf(em->output, "    jmp %s\n", false_label);
}

static void emit_jump(Emitter* em, const char* label) {
    fprintf(em->output, "    jmp %s\n", label);
}

static void emit_call(Emitter* em, const char* func_name, KrtIRValue* args, int arg_count, KrtIRValue* result) {
    if (!em || !func_name || !args) return;
    if (arg_count < 0) arg_count = 0;

    int capped_arg_count = arg_count < g_arg_reg_count ? arg_count : g_arg_reg_count;
    int save_reg_indices[14] = {0};
    int save_count = 0;
    
    for (int i = 0; i < em->ctx->temp_locations.capacity; i++) {
        TempLocation* loc = &em->ctx->temp_locations.locations[i];
        if (loc->type == TEMP_LOC_REGISTER && loc->reg != NULL) {
            const char* reg_name = loc->reg;
            int is_arg_reg = 0;
            for (int j = 0; j < capped_arg_count; j++) {
                if (strcmp(reg_name, g_arg_regs[j]) == 0) {
                    is_arg_reg = 1;
                    break;
                }
            }
            if (strcmp(reg_name, "rax") == 0) continue;
            if (!is_arg_reg && 
                (strcmp(reg_name, "rcx") == 0 || strcmp(reg_name, "rdx") == 0 ||
                 strcmp(reg_name, "rsi") == 0 || strcmp(reg_name, "rdi") == 0 ||
                 strcmp(reg_name, "r8") == 0 || strcmp(reg_name, "r9") == 0 ||
                 strcmp(reg_name, "r10") == 0 || strcmp(reg_name, "r11") == 0)) {

                int already_saved = 0;
                for (int k = 0; k < save_count; k++) {
                    if (strcmp(reg_name, g_register_names[save_reg_indices[k]]) == 0) {
                        already_saved = 1;
                        break;
                    }
                }
                
                if (!already_saved) {
                    for (int k = 0; k < g_register_count; k++) {
                        if (strcmp(g_register_names[k], reg_name) == 0) {
                            save_reg_indices[save_count++] = k;
                            break;
                        }
                    }
                }
            }
        }
    }

    for (int i = save_count - 1; i >= 0; i--) {
        fprintf(em->output, "    push %s\n", g_register_names[save_reg_indices[i]]);
    }

    for (int i = 0; i < capped_arg_count; i++) {
        emit_load_value(em, &args[i], g_arg_regs[i]);
    }

    const char* actual_func_name = func_name;
    if (strcmp(func_name, "print_string") == 0) {
        actual_func_name = "_print_string";
    } else if (strcmp(func_name, "println_string") == 0) {
        actual_func_name = "_println_string";
    }
    fprintf(em->output, "    call %s\n", actual_func_name);

    for (int i = 0; i < save_count; i++) {
        fprintf(em->output, "    pop %s\n", g_register_names[save_reg_indices[i]]);
    }
    
    if (result && result->type != KRT_IR_VALUE_VOID) {
        emit_store_result(em, result, "rax");
    }
}

static void emit_return(Emitter* em, KrtIRValue* value) {
    if (em->ctx->current_func && 
        em->ctx->current_func->name && 
        strcmp(em->ctx->current_func->name, "main") == 0) {
        
        printf("[X86CodeGen] Generating sys_exit for main function\n");
        
        if (value && value->type != KRT_IR_VALUE_VOID) {
            emit_load_value(em, value, "rdi");
        } else {
            fprintf(em->output, "    xor rdi, rdi\n");
        }
        
        fprintf(em->output, "    mov rax, 60\n");
        fprintf(em->output, "    syscall\n");
        return;
    }
    
    if (value && value->type != KRT_IR_VALUE_VOID) {
        emit_load_value(em, value, "rax");
    }

    char safe_name[256];
    sanitize_label_name(em->ctx->current_func->name, safe_name, sizeof(safe_name));
    fprintf(em->output, "    jmp %s_epilogue\n", safe_name);
}

static void emit_array_store(Emitter* em, KrtIRInst* inst) {
    if (inst->operand_count < 3) return;

    KrtIRValue* arr = &inst->operands[0];
    KrtIRValue* index = &inst->operands[1];
    KrtIRValue* value = &inst->operands[2];

    emit_load_value(em, arr, "rcx");

    emit_load_value(em, index, "rdx");

    emit_load_value(em, value, "rax");

    fprintf(em->output, "    mov [rcx + rdx*8], rax\n");
}

static void emit_instruction(Emitter* em, KrtIRInst* inst) {
    switch (inst->opcode) {
        case KRT_IR_ADD:
        case KRT_IR_SUB:
        case KRT_IR_MUL:
        case KRT_IR_DIV:
        case KRT_IR_MOD:
        case KRT_IR_AND:
        case KRT_IR_OR:
        case KRT_IR_XOR:
            if (inst->operand_count >= 2) {
                emit_binary_op(em, inst->opcode, &inst->operands[0], &inst->operands[1], &inst->result);
            }
            break;
            
        case KRT_IR_LT:
        case KRT_IR_GT:
        case KRT_IR_EQ:
        case KRT_IR_LE:
        case KRT_IR_GE:
        case KRT_IR_NE:
            if (inst->operand_count >= 2) {
                emit_compare(em, inst->opcode, &inst->operands[0], &inst->operands[1], &inst->result);
            }
            break;
            
        case KRT_IR_LOAD:
            if (inst->operand_count >= 1) {
                emit_load(em, &inst->operands[0], &inst->result);
            }
            break;
            
        case KRT_IR_STORE:
            if (inst->operand_count >= 2) {
                emit_store(em, &inst->operands[0], &inst->operands[1]);
            }
            break;
            
        case KRT_IR_BRANCH:
            if (inst->operand_count >= 3) {
                char safe_name[256];
                sanitize_label_name(em->ctx->current_func->name, safe_name, sizeof(safe_name));
                char true_label[X86_MAX_LABEL_LEN], false_label[X86_MAX_LABEL_LEN];
                snprintf(true_label, sizeof(true_label), "%s_%s",
                         safe_name, inst->operands[1].data.name);
                true_label[sizeof(true_label) - 1] = '\0';
                snprintf(false_label, sizeof(false_label), "%s_%s",
                         safe_name, inst->operands[2].data.name);
                false_label[sizeof(false_label) - 1] = '\0';
                emit_branch(em, &inst->operands[0], true_label, false_label);
            }
            break;

        case KRT_IR_JUMP:
            if (inst->operand_count >= 1) {
                char safe_name[256];
                sanitize_label_name(em->ctx->current_func->name, safe_name, sizeof(safe_name));
                char label[X86_MAX_LABEL_LEN];
                snprintf(label, sizeof(label), "%s_%s",
                         safe_name, inst->operands[0].data.name);
                label[sizeof(label) - 1] = '\0';
                emit_jump(em, label);
            }
            break;
            
        case KRT_IR_CALL:
            if (inst->operand_count >= 1) {
                const char* func_name = inst->operands[0].data.function_name;
                emit_call(em, func_name, &inst->operands[1], inst->operand_count - 1, &inst->result);
            }
            break;
            
        case KRT_IR_RETURN:
            if (inst->operand_count >= 1) {
                emit_return(em, &inst->operands[0]);
            } else {
                emit_return(em, NULL);
            }
            break;
            
        case KRT_IR_IMM:
            if (inst->operand_count >= 1) {
                emit_load_value(em, &inst->operands[0], "rax");
                emit_store_result(em, &inst->result, "rax");
            }
            break;

        case KRT_IR_ALLOC:
        case KRT_IR_NOP:

            break;
            
        case KRT_IR_ARRAY_STORE:
            emit_array_store(em, inst);
            break;
            
        case KRT_IR_LOADPTR:
            if (inst->operand_count >= 2) {
                emit_load_value(em, &inst->operands[0], "rcx");
                int offset = (int)inst->operands[1].data.imm;
                fprintf(em->output, "    mov rax, [rcx + %d]\n", offset);
                emit_store_result(em, &inst->result, "rax");
            } else if (inst->operand_count >= 1) {
                emit_load_value(em, &inst->operands[0], "rcx");
                fprintf(em->output, "    mov rax, [rcx]\n");
                emit_store_result(em, &inst->result, "rax");
            }
            break;

        case KRT_IR_STOREPTR:
            if (inst->operand_count >= 3) {
                emit_load_value(em, &inst->operands[0], "rcx");
                emit_load_value(em, &inst->operands[2], "rax");
                int offset = (int)inst->operands[1].data.imm;
                fprintf(em->output, "    mov [rcx + %d], rax\n", offset);
            }
            break;
            
        case KRT_IR_STRCAT:

            if (inst->operand_count >= 2) {
#ifdef __linux__
                emit_load_value(em, &inst->operands[0], "rdi");
                emit_load_value(em, &inst->operands[1], "rsi");
#else
                emit_load_value(em, &inst->operands[0], "rcx");
                emit_load_value(em, &inst->operands[1], "rdx");
#endif
                fprintf(em->output, "    call KrtStringConcat\n");
                emit_store_result(em, &inst->result, "rax");
            }
            break;

        case KRT_IR_INT_TO_STRING:

            if (inst->operand_count >= 1) {
#ifdef __linux__
                emit_load_value(em, &inst->operands[0], "rdi");
#else
                emit_load_value(em, &inst->operands[0], "rcx");
#endif
                fprintf(em->output, "    call KrtIntToString\n");
                emit_store_result(em, &inst->result, "rax");
            }
            break;

        case KRT_IR_DOUBLE_TO_STRING:
            if (inst->operand_count >= 1) {
#ifdef __linux__
                emit_load_value(em, &inst->operands[0], "rdi");
#else
                emit_load_value(em, &inst->operands[0], "rcx");
#endif
                fprintf(em->output, "    call KrtDoubleToString\n");
                emit_store_result(em, &inst->result, "rax");
            }
            break;
            
        case KRT_IR_LSHIFT:

            if (inst->operand_count >= 2) {
                int rcx_spilled = spill_rcx_if_used(em);
                emit_load_value(em, &inst->operands[0], "rax");
                emit_load_value(em, &inst->operands[1], "rcx");
                fprintf(em->output, "    shl rax, cl\n");
                emit_store_result(em, &inst->result, "rax");
                restore_rcx_if_needed(em, rcx_spilled);
            }
            break;

        case KRT_IR_RSHIFT:

            if (inst->operand_count >= 2) {
                int rcx_spilled = spill_rcx_if_used(em);
                emit_load_value(em, &inst->operands[0], "rax");
                emit_load_value(em, &inst->operands[1], "rcx");
                fprintf(em->output, "    sar rax, cl\n");
                emit_store_result(em, &inst->result, "rax");
                restore_rcx_if_needed(em, rcx_spilled);
            }
            break;
            
        case KRT_IR_POW:
            
            if (inst->operand_count >= 2) {
                emit_load_value(em, &inst->operands[0], "rcx");  
                emit_load_value(em, &inst->operands[1], "rdx");  
                fprintf(em->output, "    call KrtPow\n");
                emit_store_result(em, &inst->result, "rax");
            }
            break;
            
        case KRT_IR_CAST:
            if (inst->operand_count >= 1) {
                emit_load_value(em, &inst->operands[0], "rax");
                emit_store_result(em, &inst->result, "rax");
            }
            break;
            
        case KRT_IR_COPY:
            if (inst->operand_count >= 1) {
                emit_load_value(em, &inst->operands[0], "rax");
                emit_store_result(em, &inst->result, "rax");
            }
            break;
            
        case KRT_IR_LABEL:
            break;
            
        default:
            break;
    }
}

static void emit_basic_block(Emitter* em, KrtIRBasicBlock* block) {
    if (!block) return;

    char safe_name[256];
    sanitize_label_name(em->ctx->current_func->name, safe_name, sizeof(safe_name));

    fprintf(em->output, "%s_%s:\n", safe_name, block->label);

    KrtIRInst* inst = block->first_inst;
    while (inst) {
        emit_instruction(em, inst);
        inst = inst->next;
    }
}

static const char* callee_saved_regs[] = {"rbx", "r12", "r13", "r14", "r15"};
#define NUM_CALLEE_SAVED_REGS 5

static int collect_used_callee_saved_regs(CodegenContext* ctx, const char** used_regs) {
    int count = 0;
    for (int j = 0; j < NUM_CALLEE_SAVED_REGS; j++) {
        for (int i = 0; i < ctx->temp_locations.capacity; i++) {
            TempLocation* loc = &ctx->temp_locations.locations[i];
            if (loc->type == TEMP_LOC_REGISTER && strcmp(loc->reg, callee_saved_regs[j]) == 0) {
                used_regs[count++] = callee_saved_regs[j];
                break;
            }
        }
    }
    return count;
}

static void emit_function_prologue(FILE* output, CodegenContext* ctx) {
    if (!output || !ctx) return;

    const char* used_callee_saved[NUM_CALLEE_SAVED_REGS];
    int num_used = collect_used_callee_saved_regs(ctx, used_callee_saved);

    fprintf(output, "    push rbp\n");
    fprintf(output, "    mov rbp, rsp\n");

    for (int i = 0; i < num_used; i++) {
        fprintf(output, "    push %s\n", used_callee_saved[i]);
    }

    int total_push_bytes = 8 + num_used * 8;
    int adjusted_stack_size = ctx->stack_size;
    int remainder = (adjusted_stack_size + total_push_bytes) % 16;
    if (remainder != 0) {
        adjusted_stack_size += (16 - remainder);
    }
    
    if (adjusted_stack_size > 0) {
        fprintf(output, "    sub rsp, %d\n", adjusted_stack_size);
    }

    for (int i = 0; i < ctx->current_func->param_count && i < g_arg_reg_count; i++) {
        if (ctx->current_func->params && ctx->current_func->params[i].name) {
            const char* param_name = ctx->current_func->params[i].name;
            int offset = codegen_get_var_offset(ctx, param_name);
            fprintf(output, "    mov [rbp - %d], %s\n", offset, g_arg_regs[i]);
        }
    }
}

static void emit_function_epilogue(FILE* output, CodegenContext* ctx) {
    if (!output || !ctx) return;

    const char* used_callee_saved[NUM_CALLEE_SAVED_REGS];
    int num_used = collect_used_callee_saved_regs(ctx, used_callee_saved);

    char safe_name[256];
    sanitize_label_name(ctx->current_func->name, safe_name, sizeof(safe_name));

    fprintf(output, "%s_epilogue:\n", safe_name);
    fprintf(output, "    mov rsp, rbp\n");

    for (int i = num_used - 1; i >= 0; i--) {
        fprintf(output, "    pop %s\n", used_callee_saved[i]);
    }

    fprintf(output, "    pop rbp\n");
    fprintf(output, "    ret\n");
}

static void emit_function(FILE* output, KrtIRFunction* func, KrtIRModule* module) {
    if (!func || !func->name || !output) {
        return;
    }

    KrtIrSsaOptimize(func);

    CodegenContext ctx;
    codegen_context_init(&ctx, func, module);

    Emitter em = {output, &ctx};

    fprintf(output, "\nglobal %s\n", func->name);
    fprintf(output, "; Function: %s\n", func->name);
    fprintf(output, "%s:\n", func->name);

    emit_function_prologue(output, &ctx);

    KrtIRBasicBlock* block = func->entry_block;
    int block_count = 0;
    while (block) {
        emit_basic_block(&em, block);
        block = block->next;
        block_count++;
    }

    emit_function_epilogue(output, &ctx);

    char safe_name[256];
    sanitize_label_name(func->name, safe_name, sizeof(safe_name));

    if (func->uses_division || func->uses_modulo) {
        fprintf(output, ".Ldiv_by_zero_%s:\n", safe_name);
        fprintf(output, "    mov rax, -1\n");
        fprintf(output, "    jmp %s_epilogue\n", safe_name);
    }

    codegen_context_destroy(&ctx);
}

void KrtX86Generate(FILE* output, KrtIRModule* module) {
    if (!output || !module) {
        return;
    }
    
    fprintf(output, "section .data\n\n");
    fprintf(output, "section .rodata\n");

    for (int i = 0; i < module->string_const_count && module->string_constants; i++) {
        const char* str = module->string_constants[i];
        if (!str) continue;
        fprintf(output, "string_const_%d: db ", i);

        int need_comma = 0;
        for (const char* p = str; *p; p++) {
            if (need_comma) {
                fprintf(output, ", ");
            }
            need_comma = 1;
            
            switch (*p) {
                case '\n': fprintf(output, "10"); break;  
                case '\r': fprintf(output, "13"); break;  
                case '\t': fprintf(output, "9"); break;   
                case '\\': fprintf(output, "'\\\\'"); break;
                case '"': fprintf(output, "'\\\"'"); break;
                case '\0': fprintf(output, "0"); break;
                default:
                    if (*p >= 32 && *p < 127) {
                        fprintf(output, "'%c'", *p);
                    } else {
                        
                        fprintf(output, "%d", (unsigned char)*p);
                    }
            }
        }
        fprintf(output, ", 0\n");
    }

    if (module->string_const_count == 0) {
        fprintf(output, "string_const_empty: db 0\n");
    }
    fprintf(output, "\n");

    fprintf(output, "section .text\n\n");

    fprintf(output, "extern exit\n");
    fprintf(output, "extern _print_int\n");
    fprintf(output, "extern _print_int64\n");
    fprintf(output, "extern _print_float\n");
    fprintf(output, "extern _print_string\n");
    fprintf(output, "extern _println_string\n");
    fprintf(output, "extern Console__WriteLine\n");
    fprintf(output, "extern Console__Write\n");
    fprintf(output, "extern Console__WriteInt\n");
    fprintf(output, "extern KrtMalloc\n");
    fprintf(output, "extern KrtFree\n");
    fprintf(output, "extern KrtRealloc\n");
    fprintf(output, "extern KrtStrcat\n");
    fprintf(output, "extern KrtIntToString\n");
    fprintf(output, "extern KrtDoubleToString\n");
    fprintf(output, "extern KrtPow\n");
    fprintf(output, "extern timer_start\n");
    fprintf(output, "extern timer_start_int\n");
    fprintf(output, "extern timer_elapsed\n");
    fprintf(output, "extern timer_elapsed_int\n");
    fprintf(output, "extern timer_current\n");
    fprintf(output, "extern timer_current_int\n");
    fprintf(output, "extern KrtIsInstance\n");
    fprintf(output, "extern KrtAsInstance\n");
    fprintf(output, "extern KrtNullCoalesce\n");
    fprintf(output, "extern KrtNullConditional\n");
    fprintf(output, "extern KrtCastToInt32\n");
    fprintf(output, "extern KrtCastToInt64\n");
    fprintf(output, "extern KrtCastToFloat32\n");
    fprintf(output, "extern KrtCastToFloat64\n");
    fprintf(output, "extern KrtCastToBool\n");
    fprintf(output, "extern KrtCastToString\n");
    fprintf(output, "extern KrtSizeOfInt32\n");
    fprintf(output, "extern KrtSizeOfInt64\n");
    fprintf(output, "extern KrtSizeOfFloat32\n");
    fprintf(output, "extern KrtSizeOfFloat64\n");
    fprintf(output, "extern KrtSizeOfPointer\n");
    fprintf(output, "extern KrtStringConcat\n");
    fprintf(output, "extern KrtInt32ToString\n");
    fprintf(output, "extern KrtInt64ToString\n");
    fprintf(output, "extern KrtFloat32ToString\n");
    fprintf(output, "extern KrtFloat64ToString\n");
    fprintf(output, "extern KrtBoolToString\n");
    fprintf(output, "extern KrtCreateTuple\n");
    fprintf(output, "extern KrtTupleSetElement\n");
    fprintf(output, "extern KrtTupleGetElement\n");
    fprintf(output, "extern KrtLoadPtr\n");
    fprintf(output, "extern KrtStorePtr\n");
    fprintf(output, "extern KrtStackAlloc\n");
    fprintf(output, "extern KrtPinObject\n");
    fprintf(output, "extern KrtUnpinObject\n");
    fprintf(output, "extern KrtAwaitTask\n");
    fprintf(output, "extern KrtCreateTask\n");
    fprintf(output, "extern KrtCompleteTask\n");
    fprintf(output, "extern KrtLinqWhere\n");
    fprintf(output, "extern KrtLinqOrderBy\n");
    fprintf(output, "extern KrtLinqGroupBy\n");
    fprintf(output, "extern KrtLinqSelect\n");
    fprintf(output, "extern KrtCreateDelegate\n");
    fprintf(output, "extern KrtInvokeDelegate\n");
    fprintf(output, "extern Monitor_Enter\n");
    fprintf(output, "extern Monitor_Exit\n");
    fprintf(output, "extern KrtThrowException\n");
    fprintf(output, "extern KrtRethrowException\n");
    fprintf(output, "extern KrtGetGenericStaticField\n");
    fprintf(output, "extern KrtSetGenericStaticField\n");
    fprintf(output, "extern KrtClearGenericStaticFields\n");
    fprintf(output, "extern KrtIsClass\n");
    fprintf(output, "extern KrtIsStruct\n");
    fprintf(output, "extern KrtImplementsInterface\n");
    fprintf(output, "extern KrtInheritsFrom\n");
    fprintf(output, "extern KrtCheckGenericConstraint\n\n");

    KrtIRFunction* func = module->functions;
    int func_count = 0;
    while (func) {
        emit_function(output, func, module);
        func = func->next;
        func_count++;
    }
    
    if (module->main_function && strcmp(module->main_function->name, "main") != 0) {
        fprintf(output, "\nglobal main\n");
        fprintf(output, "; Wrapper: main -> %s\n", module->main_function->name);
        fprintf(output, "main:\n");
        fprintf(output, "    push rbp\n");
        fprintf(output, "    mov rbp, rsp\n");
        fprintf(output, "    call %s\n", module->main_function->name);
        fprintf(output, "    mov rdi, rax\n");
        fprintf(output, "    mov rax, 60\n");
        fprintf(output, "    syscall\n");
    }
}