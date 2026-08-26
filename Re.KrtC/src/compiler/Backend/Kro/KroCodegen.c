#include "KroCodegen.h"
#include "../../Middle/Ir/IrSsa.h"
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

// #define KRO_DEBUG  //如果要调试一定要打开

#define X86_MOV_R64_IMM64   0x48B8
#define X86_MOV_R64_R64     0x4889
#define X86_PUSH_R64        0x50
#define X86_POP_R64         0x58
#define X86_RET             0xC3
#define X86_CALL_REL32      0xE8
#define X86_JMP_REL32       0xE9
#define X86_ADD_R64_IMM32   0x4881
#define X86_SUB_R64_IMM32   0x4881
#define X86_XOR_R64_R64     0x4831
#define KRO_MAX_LABEL_LEN   256
#define KRO_MIN_STACK_SIZE  48

#define REG_RAX 0
#define REG_RCX 1
#define REG_RDX 2
#define REG_RBX 3
#define REG_RSP 4
#define REG_RBP 5
#define REG_RSI 6
#define REG_RDI 7
#define REG_R8  8
#define REG_R9  9
#define REG_R10 10
#define REG_R11 11
#define REG_R12 12
#define REG_R13 13
#define REG_R14 14
#define REG_R15 15

#ifdef __linux__
static const int g_arg_regs[] = { REG_RDI, REG_RSI, REG_RDX, REG_RCX, REG_R8, REG_R9 };
static const int g_syscall_arg_regs[] = { REG_RDI, REG_RSI, REG_RDX, REG_R10, REG_R8, REG_R9 };
static const int g_arg_reg_count = 6;
static const int g_syscall_arg_reg_count = 6;
#define KRO_SHADOW_SPACE_SIZE 0
#else
static const int g_arg_regs[] = { REG_RCX, REG_RDX, REG_R8, REG_R9 };
static const int g_syscall_arg_regs[] = { REG_RCX, REG_RDX, REG_R8, REG_R9 };
static const int g_arg_reg_count = 4;
static const int g_syscall_arg_reg_count = 4;
#define KRO_SHADOW_SPACE_SIZE 32
#endif

static void sanitize_label_name(const char* name, char* out, size_t out_size) {
    if (!name || !out || out_size == 0) return;
    size_t j = 0;
    for (size_t i = 0; name[i] && j < out_size - 1; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '.') {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

static bool kro_module_has_mangled_main(KrtIRModule* module) {
    if (!module) return false;
    KrtIRFunction* f = module->functions;
    while (f) {
        if (f->name && strcmp(f->name, "_KrtMainEntry") == 0) return true;
        f = f->next;
    }
    return false;
}

static bool is_entry_point_function(const char* name, bool has_mangled_main) {
    if (!name) return false;
    if (strcmp(name, "_KrtMainEntry") == 0) return true;
    if (strcmp(name, "main") == 0 && !has_mangled_main) return true;
    return false;
}

static void emit_byte(KROCodegenContext* ctx, uint8_t byte);
static void emit_u32(KROCodegenContext* ctx, uint32_t value);
static int alloc_temp_slot(KROCodegenContext* ctx, int temp_index);

static KROLocalVar* find_or_alloc_local_var(KROCodegenContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;

    for (int i = 0; i < KRO_MAX_LOCAL_VARS && i < ctx->local_var_count; i++) {
        if (strcmp(ctx->local_vars[i].name, name) == 0) {
            return &ctx->local_vars[i];
        }
    }

    if (ctx->local_var_count >= KRO_MAX_LOCAL_VARS) return NULL;

    KROLocalVar* var = &ctx->local_vars[ctx->local_var_count];
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->name[sizeof(var->name) - 1] = '\0';
    ctx->current_stack_offset += 8;
    var->stack_offset = ctx->current_stack_offset;
    var->allocated = 1;
    ctx->local_var_count++;

#ifdef KRO_DEBUG
#endif

    return var;
}

static KROLocalVar* find_local_var(KROCodegenContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;

    for (int i = 0; i < KRO_MAX_LOCAL_VARS && i < ctx->local_var_count; i++) {
        if (strcmp(ctx->local_vars[i].name, name) == 0) {
            return &ctx->local_vars[i];
        }
    }
    return NULL;
}

static void emit_store_to_stack(KROCodegenContext* ctx, int offset, int src_reg) {
    emit_byte(ctx, 0x48 | (src_reg >= 8 ? 0x04 : 0));
    emit_byte(ctx, 0x89);
    emit_byte(ctx, 0x85 | ((src_reg & 0x7) << 3));
    emit_u32(ctx, (uint32_t)(-offset));
}

static void emit_load_from_stack(KROCodegenContext* ctx, int offset, int dst_reg) {
    emit_byte(ctx, 0x48 | (dst_reg >= 8 ? 0x04 : 0));
    emit_byte(ctx, 0x8B);
    emit_byte(ctx, 0x85 | ((dst_reg & 0x7) << 3));
    emit_u32(ctx, (uint32_t)(-offset));
}

static void emit_byte(KROCodegenContext* ctx, uint8_t byte) {
    uint8_t data = byte;
    uint32_t before = kro_get_code_offset(ctx->writer);
    kro_write_code(ctx->writer, &data, 1);
    if (kro_get_code_offset(ctx->writer) == before) {
        fprintf(stderr, "[emit_byte] WARNING: offset not updated! byte=0x%02x\n", byte);
    }
}

static void emit_bytes(KROCodegenContext* ctx, const uint8_t* data, uint32_t size) {
    kro_write_code(ctx->writer, data, size);
}

static void emit_u32(KROCodegenContext* ctx, uint32_t value) {
    kro_write_code(ctx->writer, &value, 4);
}

static void emit_u64(KROCodegenContext* ctx, uint64_t value) {
    kro_write_code(ctx->writer, &value, 8);
}

static void emit_load_imm64_to_reg(KROCodegenContext* ctx, uint64_t value, int reg) {
    if (reg < 8) {
        
        emit_byte(ctx, 0x48);
        emit_byte(ctx, 0xB8 + reg);
        emit_u64(ctx, value);
    } else {
        
        emit_byte(ctx, 0x49);
        emit_byte(ctx, 0xB8 + (reg - 8));
        emit_u64(ctx, value);
    }
}

static void emit_load_string_addr_to_reg(KROCodegenContext* ctx, int32_t sym_idx, int reg) {
    
    if (reg < 8) {
        emit_byte(ctx, 0x48);
        emit_byte(ctx, 0xB8 + reg);
    } else {
        emit_byte(ctx, 0x49);
        emit_byte(ctx, 0xB8 + (reg - 8));
    }
    
    uint32_t offset = kro_get_code_offset(ctx->writer);
    emit_u64(ctx, 0); 
    
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, offset, sym_idx, KRO_RELOC_ABS64, 0);
}

static void emit_move_reg_to_reg(KROCodegenContext* ctx, int src_reg, int dst_reg) {
    if (src_reg == dst_reg) return;
    emit_byte(ctx, 0x48 | (src_reg >= 8 ? 0x04 : 0) | (dst_reg >= 8 ? 0x01 : 0));
    emit_byte(ctx, 0x89);
    emit_byte(ctx, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

static void emit_store_temp_result(KROCodegenContext* ctx, KrtIRInst* inst, int reg) {
    if (!ctx || !inst || inst->result.type != KRT_IR_VALUE_TEMP) return;
    int slot = alloc_temp_slot(ctx, inst->result.data.index);
    if (slot > 0) emit_store_to_stack(ctx, slot, reg);
}

static void make_block_symbol_name(KROCodegenContext* ctx, KrtIRBasicBlock* block,
                                   char* buffer, size_t buffer_size) {
    uint32_t function_hash = 2166136261u;
    const char* function_name = ctx ? ctx->current_function_name : NULL;
    while (function_name && *function_name) {
        function_hash ^= (uint8_t)*function_name++;
        function_hash *= 16777619u;
    }
    snprintf(buffer, buffer_size, "__krt_bb_%08x_%d",
             function_hash, block ? block->id : -1);
}

static int find_block_symbol(KROCodegenContext* ctx, KrtIRBasicBlock* block) {
    char name[64];
    make_block_symbol_name(ctx, block, name, sizeof(name));
    return kro_find_symbol(ctx->writer, name);
}

static void emit_jump_to_block(KROCodegenContext* ctx, KrtIRBasicBlock* target) {
    int sym_idx = find_block_symbol(ctx, target);
    if (sym_idx < 0) return;
    emit_byte(ctx, 0xE9);
    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
    emit_u32(ctx, 0);
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
}

static void emit_cond_jump_to_block(KROCodegenContext* ctx, uint8_t condition,
                                    KrtIRBasicBlock* target) {
    int sym_idx = find_block_symbol(ctx, target);
    if (sym_idx < 0) return;
    emit_byte(ctx, 0x0F);
    emit_byte(ctx, condition);
    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
    emit_u32(ctx, 0);
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
}

static int instruction_value_size(KrtIRInst* inst, int operand_index, int default_size) {
    if (!inst || operand_index < 0 || operand_index >= inst->operand_count) return default_size;
    KrtIRValue* size = &inst->operands[operand_index];
    if (size->type != KRT_IR_VALUE_IMM) return default_size;
    int value = (int)size->data.imm;
    return value == 1 || value == 2 || value == 4 || value == 8 ? value : default_size;
}

static void emit_load_indirect(KROCodegenContext* ctx, int address_reg, int target_reg, int size) {
    if (address_reg != REG_RAX || target_reg != REG_RAX) {
        emit_move_reg_to_reg(ctx, address_reg, REG_RAX);
    }
    switch (size) {
        case 1: emit_bytes(ctx, (const uint8_t*)"\x0F\xB6\x00", 3); break;
        case 2: emit_bytes(ctx, (const uint8_t*)"\x0F\xB7\x00", 3); break;
        case 4: emit_bytes(ctx, (const uint8_t*)"\x8B\x00", 2); break;
        default: emit_bytes(ctx, (const uint8_t*)"\x48\x8B\x00", 3); break;
    }
    if (target_reg != REG_RAX) emit_move_reg_to_reg(ctx, REG_RAX, target_reg);
}

static void emit_store_indirect(KROCodegenContext* ctx, int address_reg, int value_reg, int size) {
    if (address_reg != REG_RAX) emit_move_reg_to_reg(ctx, address_reg, REG_RAX);
    if (value_reg != REG_RDX) emit_move_reg_to_reg(ctx, value_reg, REG_RDX);
    switch (size) {
        case 1: emit_bytes(ctx, (const uint8_t*)"\x88\x10", 2); break;
        case 2: emit_bytes(ctx, (const uint8_t*)"\x66\x89\x10", 3); break;
        case 4: emit_bytes(ctx, (const uint8_t*)"\x89\x10", 2); break;
        default: emit_bytes(ctx, (const uint8_t*)"\x48\x89\x10", 3); break;
    }
}

static void emit_function_prologue(KROCodegenContext* ctx, int stack_size) {
    
    emit_byte(ctx, 0x55);
    
    emit_bytes(ctx, (const uint8_t*)"\x48\x89\xE5", 3);
    
    if (stack_size > 0) {
        emit_bytes(ctx, (const uint8_t*)"\x48\x81\xEC", 3);
        emit_u32(ctx, (uint32_t)stack_size);
    }
}

static void emit_function_epilogue(KROCodegenContext* ctx) {

    emit_bytes(ctx, (const uint8_t*)"\x48\x89\xEC", 3);

    emit_byte(ctx, 0x5D);

    if (ctx && ctx->is_main_func) {
#ifdef KRO_DEBUG
#endif

#ifdef _WIN32
        emit_bytes(ctx, (const uint8_t*)"\x48\x89\xC1", 3);
        emit_call_external(ctx, "ExitProcess");
#else
        emit_byte(ctx, 0x48);
        emit_byte(ctx, 0x89);
        emit_byte(ctx, 0xC7);

        emit_byte(ctx, 0x48);
        emit_byte(ctx, 0xC7);
        emit_byte(ctx, 0xC0);
        emit_u32(ctx, 60);

        emit_byte(ctx, 0x0F);
        emit_byte(ctx, 0x05);
#endif
    } else {
        emit_byte(ctx, X86_RET);
    }
}

static void emit_load_imm64(KROCodegenContext* ctx, uint64_t value) {
    
    emit_byte(ctx, 0x48);
    emit_byte(ctx, 0xB8);
    emit_u64(ctx, value);
}

static uint64_t encode_integer_immediate(double value) {
    int64_t signed_value = (int64_t)value;
    uint64_t encoded = 0;
    memcpy(&encoded, &signed_value, sizeof(encoded));
    return encoded;
}

static void emit_call_external(KROCodegenContext* ctx, const char* func_name) {
    if (!ctx || !func_name) return;

    int32_t sym_idx = kro_find_symbol(ctx->writer, func_name);
    if (sym_idx < 0) {
#ifdef _WIN32
        if (strcmp(func_name, "puts") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "puts", "msvcrt.dll");
        } else if (strcmp(func_name, "Console__Write") == 0 ||
                   strcmp(func_name, "Console__WriteLine") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "printf", "msvcrt.dll");
        } else if (strcmp(func_name, "Console__ReadLine") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "fgets", "msvcrt.dll");
        } else if (strcmp(func_name, "malloc") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "malloc", "msvcrt.dll");
        } else if (strcmp(func_name, "free") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "free", "msvcrt.dll");
        } else if (strcmp(func_name, "memcpy") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "memcpy", "msvcrt.dll");
        } else if (strcmp(func_name, "ExitProcess") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "ExitProcess", "kernel32.dll");
        } else {
            sym_idx = kro_add_undefined_symbol(ctx->writer, func_name);
        }
#else
        if (strcmp(func_name, "puts") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "puts", "libc.so.6");
        } else if (strcmp(func_name, "Console__Write") == 0 ||
                   strcmp(func_name, "Console__WriteLine") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "printf", "libc.so.6");
        } else if (strcmp(func_name, "Console__ReadLine") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "fgets", "libc.so.6");
        } else if (strcmp(func_name, "malloc") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "malloc", "libc.so.6");
        } else if (strcmp(func_name, "free") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "free", "libc.so.6");
        } else if (strcmp(func_name, "memcpy") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "memcpy", "libc.so.6");
        } else {
            sym_idx = kro_add_undefined_symbol(ctx->writer, func_name);
        }
#endif
    }

    if (sym_idx < 0) return;

    emit_byte(ctx, 0x48);
    emit_byte(ctx, 0xB8);
    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
    emit_u64(ctx, 0);
    emit_byte(ctx, 0xFF);
    emit_byte(ctx, 0xD0);

    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_ABS64, 0);
}

static void emit_call_local(KROCodegenContext* ctx, const char* func_name) {
    if (!ctx || !func_name) return;

#ifdef KRO_DEBUG
#endif

    int32_t sym_idx = kro_find_symbol(ctx->writer, func_name);

#ifdef KRO_DEBUG
#endif

    if (sym_idx < 0) {
#ifdef KRO_DEBUG
#endif
        sym_idx = kro_add_undefined_symbol(ctx->writer, func_name);

#ifdef KRO_DEBUG
#endif
    }

    if (sym_idx < 0) return;

    uint32_t call_offset = kro_get_code_offset(ctx->writer);

    emit_byte(ctx, 0xE8);
    emit_u32(ctx, 0);

    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, call_offset + 1, sym_idx, KRO_RELOC_PC32, 0);
}

static void emit_binary_op(KROCodegenContext* ctx, int op) {
    if (!ctx) return;

    switch (op) {
        case 0:
            emit_bytes(ctx, (const uint8_t*)"\x48\x01\xD8", 3);
            break;
        case 1:
            emit_bytes(ctx, (const uint8_t*)"\x48\x29\xD8", 3);
            break;
        case 2:
            emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xAF\xC3", 4);
            break;
        case 3:
            emit_bytes(ctx, (const uint8_t*)"\x48\x99", 2);
            emit_bytes(ctx, (const uint8_t*)"\x48\xF7\xFB", 3);
            break;
        default:
            break;
    }
}

static int find_temp_slot(KROCodegenContext* ctx, int temp_index) {
    if (!ctx) return -1;
    for (int i = 0; i < ctx->temp_slot_count; i++) {
        if (ctx->temp_slots[i].valid && ctx->temp_slots[i].temp_index == temp_index) {
            return ctx->temp_slots[i].stack_offset;
        }
    }
    return -1;
}

static int alloc_temp_slot(KROCodegenContext* ctx, int temp_index) {
    if (!ctx || ctx->temp_slot_count >= KRO_MAX_TEMP_REGS) return -1;
    for (int i = 0; i < ctx->temp_slot_count; i++) {
        if (ctx->temp_slots[i].valid && ctx->temp_slots[i].temp_index == temp_index) {
            return ctx->temp_slots[i].stack_offset;
        }
    }
    ctx->current_stack_offset += 8;
    ctx->temp_slots[ctx->temp_slot_count].temp_index = temp_index;
    ctx->temp_slots[ctx->temp_slot_count].stack_offset = ctx->current_stack_offset;
    ctx->temp_slots[ctx->temp_slot_count].valid = 1;
    ctx->temp_slot_count++;
    return ctx->current_stack_offset;
}

static void emit_load_value_to_reg(KROCodegenContext* ctx, KrtIRValue* value, int target_reg) {
    if (!ctx || !value) return;

    switch (value->type) {
        case KRT_IR_VALUE_VOID:
            emit_load_imm64_to_reg(ctx, 0, target_reg);
            break;
        case KRT_IR_VALUE_IMM:
            emit_load_imm64_to_reg(ctx, encode_integer_immediate(value->data.imm), target_reg);
            break;
        case KRT_IR_VALUE_VAR: {
            KROLocalVar* local_var = find_local_var(ctx, value->data.name);
            if (local_var && local_var->allocated) {
                emit_load_from_stack(ctx, local_var->stack_offset, target_reg);
            } else {
                int sym_idx = kro_find_symbol(ctx->writer, value->data.name);
                if (sym_idx >= 0) {
                    emit_byte(ctx, 0x48 | (target_reg >= 8 ? 0x04 : 0));
                    emit_byte(ctx, 0x8B);
                    emit_byte(ctx, 0x05 + ((target_reg & 0x7) << 3));
                    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
                    emit_u32(ctx, 0);
                    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
                } else {
                    local_var = find_or_alloc_local_var(ctx, value->data.name);
                    if (local_var) {
                        emit_load_from_stack(ctx, local_var->stack_offset, target_reg);
                    }
                }
            }
            break;
        }
        case KRT_IR_VALUE_TEMP: {
            int stack_offset = find_temp_slot(ctx, value->data.index);
            if (stack_offset > 0) {
                emit_load_from_stack(ctx, stack_offset, target_reg);
            }
            break;
        }
        case KRT_IR_VALUE_ARG: {
            int arg_idx = value->data.index;
            if (arg_idx >= 0 && arg_idx < ctx->current_param_count &&
                ctx->arg_stack_offsets[arg_idx] > 0) {
                emit_load_from_stack(ctx, ctx->arg_stack_offsets[arg_idx], target_reg);
            } else if (arg_idx >= 0 && arg_idx < g_arg_reg_count) {
                emit_move_reg_to_reg(ctx, g_arg_regs[arg_idx], target_reg);
            }
            break;
        }
        case KRT_IR_VALUE_STRING_CONST: {
            int32_t sym_idx = -1;
            if (value->data.string_const_id >= 0 &&
                ctx->string_const_sym_indices &&
                value->data.string_const_id < ctx->string_const_count) {
                sym_idx = ctx->string_const_sym_indices[value->data.string_const_id];
            }
            if (sym_idx >= 0) {
                emit_load_string_addr_to_reg(ctx, sym_idx, target_reg);
            }
            break;
        }
        default:
            break;
    }
}

static void KroGenerateInstruction(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (!ctx || !inst) return;

#ifdef KRO_DEBUG
#endif

    switch (inst->opcode) {
        case KRT_IR_IMM: {
            if (inst->operand_count >= 1 && inst->operands[0].type == KRT_IR_VALUE_IMM) {
                uint64_t value = encode_integer_immediate(inst->operands[0].data.imm);
                emit_load_imm64(ctx, value);
                emit_store_temp_result(ctx, inst, REG_RAX);
            }
            break;
        }

        case KRT_IR_ADD:
        case KRT_IR_SUB:
        case KRT_IR_MUL:
        case KRT_IR_DIV:
        case KRT_IR_MOD:
        case KRT_IR_AND:
        case KRT_IR_OR:
        case KRT_IR_XOR:
        case KRT_IR_LSHIFT:
        case KRT_IR_RSHIFT: {
            if (inst->operand_count < 2) break;

            KrtIRValue* lhs = &inst->operands[0];
            KrtIRValue* rhs = &inst->operands[1];

            emit_load_value_to_reg(ctx, lhs, REG_RAX);
            emit_load_value_to_reg(ctx, rhs, REG_RCX);

            switch (inst->opcode) {
                case KRT_IR_ADD: emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3); break;
                case KRT_IR_SUB: emit_bytes(ctx, (const uint8_t*)"\x48\x29\xC8", 3); break;
                case KRT_IR_MUL: emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xAF\xC1", 4); break;
                case KRT_IR_DIV:
                case KRT_IR_MOD:
                    emit_bytes(ctx, (const uint8_t*)"\x48\x99\x48\xF7\xF9", 5);
                    if (inst->opcode == KRT_IR_MOD) emit_move_reg_to_reg(ctx, REG_RDX, REG_RAX);
                    break;
                case KRT_IR_AND: emit_bytes(ctx, (const uint8_t*)"\x48\x21\xC8", 3); break;
                case KRT_IR_OR: emit_bytes(ctx, (const uint8_t*)"\x48\x09\xC8", 3); break;
                case KRT_IR_XOR: emit_bytes(ctx, (const uint8_t*)"\x48\x31\xC8", 3); break;
                case KRT_IR_LSHIFT: emit_bytes(ctx, (const uint8_t*)"\x48\xD3\xE0", 3); break;
                case KRT_IR_RSHIFT: emit_bytes(ctx, (const uint8_t*)"\x48\xD3\xF8", 3); break;
                default: break;
            }
            emit_store_temp_result(ctx, inst, REG_RAX);
            break;
        }

        case KRT_IR_LT:
        case KRT_IR_GT:
        case KRT_IR_EQ:
        case KRT_IR_LE:
        case KRT_IR_GE:
        case KRT_IR_NE: {
            if (inst->operand_count < 2) break;
            emit_load_value_to_reg(ctx, &inst->operands[0], REG_RAX);
            emit_load_value_to_reg(ctx, &inst->operands[1], REG_RCX);
            emit_bytes(ctx, (const uint8_t*)"\x48\x39\xC8", 3);
            emit_byte(ctx, 0x0F);
            switch (inst->opcode) {
                case KRT_IR_LT: emit_byte(ctx, 0x9C); break;
                case KRT_IR_GT: emit_byte(ctx, 0x9F); break;
                case KRT_IR_EQ: emit_byte(ctx, 0x94); break;
                case KRT_IR_LE: emit_byte(ctx, 0x9E); break;
                case KRT_IR_GE: emit_byte(ctx, 0x9D); break;
                case KRT_IR_NE: emit_byte(ctx, 0x95); break;
                default: break;
            }
            emit_byte(ctx, 0xC0);
            emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xB6\xC0", 4);
            emit_store_temp_result(ctx, inst, REG_RAX);
            break;
        }

        case KRT_IR_CAST: {
            if (inst->operand_count < 1) break;
            emit_load_value_to_reg(ctx, &inst->operands[0], REG_RAX);
            emit_store_temp_result(ctx, inst, REG_RAX);
            break;
        }

        case KRT_IR_JUMP: {
            if (ctx->current_block && ctx->current_block->succ_count > 0) {
                emit_jump_to_block(ctx, ctx->current_block->succs[0]);
            }
            break;
        }

        case KRT_IR_BRANCH: {
            if (inst->operand_count < 1 || !ctx->current_block ||
                ctx->current_block->succ_count < 2) break;
            emit_load_value_to_reg(ctx, &inst->operands[0], REG_RAX);
            emit_bytes(ctx, (const uint8_t*)"\x48\x85\xC0", 3);
            emit_cond_jump_to_block(ctx, 0x85, ctx->current_block->succs[0]);
            emit_jump_to_block(ctx, ctx->current_block->succs[1]);
            break;
        }

        case KRT_IR_CALL: {
            if (inst->operand_count < 1 || inst->operands[0].type != KRT_IR_VALUE_FUNCTION) break;

            const char* func_name = inst->operands[0].data.function_name;
            if (!func_name) break;

            int arg_count = inst->operand_count - 1;
            int capped_arg_count = arg_count < g_arg_reg_count ? arg_count : g_arg_reg_count;

            for (int i = 0; i < capped_arg_count; i++) {
                KrtIRValue* arg = &inst->operands[i + 1];
                int target_reg = g_arg_regs[i];

                emit_load_value_to_reg(ctx, arg, target_reg);
            }

            if (strcmp(func_name, "KrtStorePtr") == 0 && inst->operand_count >= 4) {
                emit_load_value_to_reg(ctx, &inst->operands[1], REG_RAX); /* ptr */
                emit_load_value_to_reg(ctx, &inst->operands[2], REG_RCX); /* offset */
                if (!(inst->operands[2].type == KRT_IR_VALUE_IMM &&
                      inst->operands[2].data.imm == 0)) {
                    emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);   /* add rax, rcx */
                }
                emit_load_value_to_reg(ctx, &inst->operands[3], REG_RDX); /* value */
                emit_bytes(ctx, (const uint8_t*)"\x48\x89\x10", 3);       /* mov [rax], rdx */
                break;
            }
            if (strcmp(func_name, "KrtLoadPtr") == 0 && inst->operand_count >= 3) {
                emit_load_value_to_reg(ctx, &inst->operands[1], REG_RAX); /* ptr */
                emit_load_value_to_reg(ctx, &inst->operands[2], REG_RCX); /* offset */
                if (!(inst->operands[2].type == KRT_IR_VALUE_IMM &&
                      inst->operands[2].data.imm == 0)) {
                    emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);   /* add rax, rcx */
                }
                emit_bytes(ctx, (const uint8_t*)"\x48\x8B\x00", 3);       /* mov rax, [rax] */
                emit_store_temp_result(ctx, inst, REG_RAX);
                break;
            }

            int32_t local_sym_idx = kro_find_symbol(ctx->writer, func_name);
            if (local_sym_idx >= 0) {
                emit_call_local(ctx, func_name);
            } else {
                emit_call_external(ctx, func_name);
            }
            if (inst->result.type == KRT_IR_VALUE_TEMP) {
                int slot = alloc_temp_slot(ctx, inst->result.data.index);
                if (slot > 0) emit_store_to_stack(ctx, slot, REG_RAX);
            }
            break;
        }

        case KRT_IR_RETURN: {
#ifdef KRO_DEBUG
#endif
            if (inst->operand_count >= 1 && inst->operands[0].type != KRT_IR_VALUE_VOID) {
                KrtIRValue* ret_val = &inst->operands[0];
#ifdef KRO_DEBUG
#endif
                emit_load_value_to_reg(ctx, ret_val, REG_RAX);
            } else {
                emit_load_imm64(ctx, 0);
            }
            emit_function_epilogue(ctx);
            break;
        }

        case KRT_IR_STORE: {
            if (inst->operand_count < 2) break;

            KrtIRValue* dest = &inst->operands[0];
            KrtIRValue* value = &inst->operands[1];

#ifdef KRO_DEBUG
            if (dest->type == KRT_IR_VALUE_VAR) fprintf(stderr, ", name=%s", dest->data.name);
            fprintf(stderr, ", value.type=%d", value->type);
            if (value->type == KRT_IR_VALUE_IMM) fprintf(stderr, ", imm=%ld", value->data.imm);
            fprintf(stderr, "\n");
#endif

            if (dest->type == KRT_IR_VALUE_VAR) {
                KROLocalVar* local_var = find_local_var(ctx, dest->data.name);

#ifdef KRO_DEBUG
                if (local_var) fprintf(stderr, ", allocated=%d, stack_offset=-%d",
                        local_var->allocated, local_var->stack_offset);
                fprintf(stderr, "\n");
#endif

                if (local_var && local_var->allocated) {
#ifdef KRO_DEBUG
#endif
                    emit_load_value_to_reg(ctx, value, REG_RAX);
#ifdef KRO_DEBUG
#endif
                    emit_store_to_stack(ctx, local_var->stack_offset, REG_RAX);
                } else {
                    int sym_idx = kro_find_symbol(ctx->writer, dest->data.name);
                    if (sym_idx >= 0) {
                        emit_load_value_to_reg(ctx, value, REG_RAX);
                        emit_byte(ctx, 0x48);
                        emit_byte(ctx, 0x89);
                        emit_byte(ctx, 0x05);
                        uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
                        emit_u32(ctx, 0);
                        kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
                    }
                }
            }
            break;
        }

        case KRT_IR_LOADPTR: {
            if (inst->operand_count < 1) break;
            emit_load_value_to_reg(ctx, &inst->operands[0], REG_RAX);
            if (inst->operand_count >= 2) {
                emit_load_value_to_reg(ctx, &inst->operands[1], REG_RCX);
                emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);
            }
            int size = instruction_value_size(inst, 2, 8);
            emit_load_indirect(ctx, REG_RAX, REG_RAX, size);
            emit_store_temp_result(ctx, inst, REG_RAX);
            break;
        }

        case KRT_IR_STOREPTR: {
            if (inst->operand_count < 3) break;
            emit_load_value_to_reg(ctx, &inst->operands[0], REG_RAX);
            emit_load_value_to_reg(ctx, &inst->operands[1], REG_RCX);
            emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);
            emit_load_value_to_reg(ctx, &inst->operands[2], REG_RDX);
            emit_store_indirect(ctx, REG_RAX, REG_RDX,
                                instruction_value_size(inst, 3, 8));
            break;
        }

        case KRT_IR_ARRAY_STORE: {
            if (inst->operand_count < 3) break;
            int size = instruction_value_size(inst, 3, 8);
            emit_load_value_to_reg(ctx, &inst->operands[0], REG_RAX);
            emit_load_value_to_reg(ctx, &inst->operands[1], REG_RCX);
            if (size != 1) {
                emit_bytes(ctx, (const uint8_t*)"\x48\x6B\xC9", 3);
                emit_byte(ctx, (uint8_t)size);
            }
            emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);
            emit_load_value_to_reg(ctx, &inst->operands[2], REG_RDX);
            emit_store_indirect(ctx, REG_RAX, REG_RDX, size);
            break;
        }

        case KRT_IR_LOAD: {
            if (inst->operand_count < 1) break;

            KrtIRValue* src = &inst->operands[0];

            if (src->type == KRT_IR_VALUE_VAR) {
                KROLocalVar* local_var = find_local_var(ctx, src->data.name);

                if (local_var && local_var->allocated) {
#ifdef KRO_DEBUG
#endif
                    emit_load_from_stack(ctx, local_var->stack_offset, REG_RAX);
                } else {
                    int sym_idx = kro_find_symbol(ctx->writer, src->data.name);
                    if (sym_idx >= 0) {
                        emit_byte(ctx, 0x48);
                        emit_byte(ctx, 0x8B);
                        emit_byte(ctx, 0x05);
                        uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
                        emit_u32(ctx, 0);
                        kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
                    }
                }
            }
            if (inst->result.type == KRT_IR_VALUE_TEMP) {
                int slot = alloc_temp_slot(ctx, inst->result.data.index);
                if (slot > 0) {
                    emit_store_to_stack(ctx, slot, REG_RAX);
#ifdef KRO_DEBUG
#endif
                }
            }
            break;
        }

        case KRT_IR_ALLOC: {
            if (inst->operand_count < 1) break;

            KrtIRValue* var = &inst->operands[0];
#ifdef KRO_DEBUG
            if (var->type == KRT_IR_VALUE_VAR && var->data.name) {
                fprintf(stderr, ", name=%s", var->data.name);
            }
            fprintf(stderr, "\n");
#endif
            if (var->type == KRT_IR_VALUE_VAR) {
#ifdef KRO_DEBUG
#endif
                find_or_alloc_local_var(ctx, var->data.name);

#ifdef KRO_DEBUG
                for (int i = 0; i < ctx->local_var_count; i++) {
                    fprintf(stderr, "     [%d] name='%s', offset=-%d\n",
                            i, ctx->local_vars[i].name, ctx->local_vars[i].stack_offset);
                }
#endif
            }
            break;
        }

        case KRT_IR_COPY: {
            if (inst->operand_count < 1) break;

            KrtIRValue* src = &inst->operands[0];
            KrtIRValue* dst = &inst->result;

#ifdef KRO_DEBUG
            if (src->type == KRT_IR_VALUE_TEMP) {
            }
            if (dst->type == KRT_IR_VALUE_TEMP) {
            }
#endif

            emit_load_value_to_reg(ctx, src, REG_RAX);

            if (dst->type == KRT_IR_VALUE_TEMP) {
                int slot = alloc_temp_slot(ctx, dst->data.index);
                if (slot > 0) {
                    emit_store_to_stack(ctx, slot, REG_RAX);
#ifdef KRO_DEBUG
#endif
                }
            } else if (dst->type == KRT_IR_VALUE_VAR) {
                KROLocalVar* local_var = find_local_var(ctx, dst->data.name);
                if (local_var && local_var->allocated) {
                    emit_store_to_stack(ctx, local_var->stack_offset, REG_RAX);
                } else {
                    int sym_idx = kro_find_symbol(ctx->writer, dst->data.name);
                    if (sym_idx >= 0) {
                        emit_byte(ctx, 0x48);
                        emit_byte(ctx, 0x89);
                        emit_byte(ctx, 0x05);
                        uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
                        emit_u32(ctx, 0);
                        kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
                    } else {
                        local_var = find_or_alloc_local_var(ctx, dst->data.name);
                        if (local_var && local_var->allocated) {
                            emit_store_to_stack(ctx, local_var->stack_offset, REG_RAX);
                        }
                    }
                }
            }
            break;
        }

        case KRT_IR_PHI: {
            if (inst->result.type == KRT_IR_VALUE_TEMP) {
                int slot = alloc_temp_slot(ctx, inst->result.data.index);
#ifdef KRO_DEBUG
#endif
            }
            break;
        }

        case KRT_IR_SYSCALL: {
#ifdef KRO_DEBUG
            for (int i = 0; i < inst->operand_count && i < 7; i++) {
                KrtIRValue* op = &inst->operands[i];
                if (op->type == KRT_IR_VALUE_IMM) fprintf(stderr, ", imm=%f", op->data.imm);
                if (op->type == KRT_IR_VALUE_TEMP) fprintf(stderr, ", temp_idx=%d", op->data.index);
                if (op->type == KRT_IR_VALUE_VAR) fprintf(stderr, ", var=%s", op->data.name ? op->data.name : "(null)");
                fprintf(stderr, "\n");
            }
#endif
            if (inst->operand_count < 1) break;
            KrtIRValue* syscall_num = &inst->operands[0];
            int arg_count = inst->operand_count - 1;
            if (arg_count > g_syscall_arg_reg_count) arg_count = g_syscall_arg_reg_count;

            for (int i = arg_count - 1; i >= 0; i--) {
                KrtIRValue* arg = &inst->operands[i + 1];
                int target_reg = g_syscall_arg_regs[i];
                emit_load_value_to_reg(ctx, arg, target_reg);
            }

            emit_load_value_to_reg(ctx, syscall_num, REG_RAX);

            emit_byte(ctx, 0x0F);
            emit_byte(ctx, 0x05);

            if (inst->result.type == KRT_IR_VALUE_TEMP) {
                int slot = alloc_temp_slot(ctx, inst->result.data.index);
                if (slot > 0) {
                    emit_store_to_stack(ctx, slot, REG_RAX);
                }
            }
            break;
        }

        default:
#ifdef KRO_DEBUG
#endif
            break;
    }
}

static void KroGenerateBlock(KROCodegenContext* ctx, KrtIRBasicBlock* block) {
    if (!block) return;

    ctx->current_block = block;
    int block_sym_idx = find_block_symbol(ctx, block);
    if (block_sym_idx >= 0) {
        kro_update_symbol_value(ctx->writer, block_sym_idx, kro_get_code_offset(ctx->writer));
    }

    KrtIRInst* inst = block->first_inst;
    while (inst) {
        KroGenerateInstruction(ctx, inst);
        inst = inst->next;
    }
}

static int calculate_function_stack_size(KrtIRFunction* func) {
    int slot_count = 8;

    if (func) {
        slot_count += func->param_count;
        KrtIRBasicBlock* block = func->entry_block;

#ifdef KRO_DEBUG
#endif

        while (block) {
            for (int i = 0; i < block->inst_count; i++) {
                KrtIRInst* inst = block->insts[i];
                if (!inst) continue;
#ifdef KRO_DEBUG
                fprintf(stderr, "   opcode=%d", inst->opcode);
                if (inst->opcode == KRT_IR_ALLOC && inst->operand_count >= 1 &&
                    inst->operands[0].type == KRT_IR_VALUE_VAR) {
                    fprintf(stderr, " [ALLOC: %s]", inst->operands[0].data.name);
                }
                if (inst->opcode == KRT_IR_STORE && inst->operand_count >= 1 &&
                    inst->operands[0].type == KRT_IR_VALUE_VAR) {
                    fprintf(stderr, " [STORE: %s]", inst->operands[0].data.name);
                }
                fprintf(stderr, "\n");
#endif

                slot_count++;
            }
            block = block->next;
        }
    }

    int stack_size = KRO_SHADOW_SPACE_SIZE + slot_count * 8;
    stack_size = (stack_size + 15) & ~15;
    if (stack_size < KRO_MIN_STACK_SIZE) stack_size = KRO_MIN_STACK_SIZE;
    return stack_size;
}

static void KroGenerateFunction(KROCodegenContext* ctx, KrtIRFunction* func, KrtIRModule* module) {
    if (!func) return;

    // KrtIrSsaOptimize(func);  //禁用SSA优化,除了Airs_td以外的人不要打开它,SSA不稳定

    bool has_mangled_main = kro_module_has_mangled_main(module);
    int is_entry_point = is_entry_point_function(func->name, has_mangled_main);
    int is_main = (strcmp(func->name, "main") == 0);

    ctx->is_main_func = is_entry_point;
    ctx->local_var_count = 0;
    ctx->current_stack_offset = 8;
    memset(ctx->local_vars, 0, sizeof(ctx->local_vars));
    memset(ctx->temp_slots, 0, sizeof(ctx->temp_slots));
    ctx->temp_slot_count = 0;
    ctx->func_index++;
    ctx->current_function_id = ctx->func_index;
    ctx->current_function_name = func->name;
    ctx->current_block = NULL;
    ctx->current_param_count = func->param_count < KRO_MAX_ARGS ? func->param_count : KRO_MAX_ARGS;
    memset(ctx->arg_stack_offsets, 0, sizeof(ctx->arg_stack_offsets));

    int stack_size = calculate_function_stack_size(func);

    emit_function_prologue(ctx, stack_size);

    for (int i = 0; i < ctx->current_param_count && i < g_arg_reg_count; i++) {
        ctx->arg_stack_offsets[i] = ctx->current_stack_offset;
        ctx->current_stack_offset += 8;
        emit_store_to_stack(ctx, ctx->arg_stack_offsets[i], g_arg_regs[i]);
    }

    KrtIRBasicBlock* symbol_block = func->entry_block;
    while (symbol_block) {
        char block_symbol[64];
        make_block_symbol_name(ctx, symbol_block, block_symbol, sizeof(block_symbol));
        if (kro_find_symbol(ctx->writer, block_symbol) < 0) {
            kro_add_symbol(ctx->writer, block_symbol, KRO_SYM_NOTYPE, KRO_BIND_LOCAL,
                           KRO_SEC_TEXT, 0);
        }
        symbol_block = symbol_block->next;
    }

    bool has_explicit_return = false;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        KrtIRInst* scan_inst = block->first_inst;
        while (scan_inst) {
            if (scan_inst->opcode == KRT_IR_RETURN) {
                has_explicit_return = true;
                break;
            }
            scan_inst = scan_inst->next;
        }
        KroGenerateBlock(ctx, block);
        block = block->next;
    }

    if (is_entry_point) {

        int called_mangled_main = 0;
        if (is_main && !has_explicit_return && module) {

            KrtIRFunction* mangled_main = module->functions;
            while (mangled_main) {
                if (strcmp(mangled_main->name, "_KrtMainEntry") == 0) {

                    emit_call_local(ctx, "_KrtMainEntry");
                    called_mangled_main = 1;
                    break;
                }
                mangled_main = mangled_main->next;
            }
        }

        if (!has_explicit_return && !called_mangled_main) {
            emit_load_imm64(ctx, 0);
        }

        emit_function_epilogue(ctx);
    } else {
        if (!has_explicit_return) {
            emit_load_imm64(ctx, 0);
        }
        emit_function_epilogue(ctx);
    }
}

static void KroGenerateDataSection(KROCodegenContext* ctx, KrtIRModule* module) {
    if (!module || !ctx || !ctx->writer) return;

    if (module->string_const_count > 0 && module->string_constants) {
        ctx->string_const_sym_indices = (int32_t*)KRT_MALLOC(module->string_const_count * sizeof(int32_t));
        if (!ctx->string_const_sym_indices) return;
        ctx->string_const_count = module->string_const_count;

        for (int i = 0; i < module->string_const_count; i++) {
            const char* str = module->string_constants[i];
            if (!str) continue;
            uint32_t rodata_offset = kro_get_rodata_offset(ctx->writer);

            char escaped[4096];
            uint32_t escaped_len = 0;
            if (str) {
                size_t len = strlen(str);
                for (size_t s = 0; s < len && escaped_len < sizeof(escaped) - 1; s++) {
                    if (str[s] == '\\' && s + 1 < len) {
                        s++;
                        switch (str[s]) {
                            case 'n':  escaped[escaped_len++] = '\n'; break;
                            case 'r':  escaped[escaped_len++] = '\r'; break;
                            case 't':  escaped[escaped_len++] = '\t'; break;
                            case '\\': escaped[escaped_len++] = '\\'; break;
                            case '"':  escaped[escaped_len++] = '"';  break;
                            case '0':  escaped[escaped_len++] = '\0'; break;
                            default:   escaped[escaped_len++] = '\\';
                                       if (escaped_len < sizeof(escaped) - 1)
                                           escaped[escaped_len++] = str[s];
                                       break;
                        }
                    } else {
                        escaped[escaped_len++] = str[s];
                    }
                }
            }
            escaped[escaped_len] = '\0';
            escaped_len++;
            kro_write_rodata(ctx->writer, escaped, escaped_len);

            char sym_name[64];
            snprintf(sym_name, sizeof(sym_name), "str_const_%d", i);
            ctx->string_const_sym_indices[i] = kro_add_symbol(ctx->writer, sym_name, KRO_SYM_OBJECT, KRO_BIND_LOCAL, KRO_SEC_RODATA, rodata_offset);
        }
    }

    for (int i = 0; i < module->global_count; i++) {
        KrtIRGlobal* global = &module->globals[i];
        if (!global || !global->name) continue;

        uint32_t data_offset = kro_get_data_offset(ctx->writer);

        int64_t init_value = 0;
        if (global->has_initializer) {
            init_value = (int64_t)global->init_number;
        }

        kro_write_data(ctx->writer, &init_value, 8);

        int sym_idx = kro_add_symbol(ctx->writer, global->name, KRO_SYM_OBJECT, KRO_BIND_GLOBAL, KRO_SEC_DATA, data_offset);
        if (sym_idx < 0) {
#ifdef KRO_DEBUG
#endif
        }
    }
}

void KrtKrtGenerate(FILE* output_file, const char* output_filename, KrtIRModule* module) {
    if (!output_file || !module) {
        return;
    }

#ifdef KRO_DEBUG
    KrtIRFunction* check_func = module->functions;
    while (check_func) {
        fprintf(stderr, "  Function: %s, entry_block=%p\n", check_func->name, (void*)check_func->entry_block);
        if (check_func->entry_block && check_func->entry_block->first_inst) {
            int count = 0;
            KrtIRInst* inst = check_func->entry_block->first_inst;
            while (inst) {
                fprintf(stderr, "    [%d] opcode=%d, addr=%p", count++, inst->opcode, (void*)inst);
                if (inst->opcode == KRT_IR_STORE && inst->operand_count >= 2) {
                    fprintf(stderr, ", dest.type=%d, value.type=%d",
                            inst->operands[0].type, inst->operands[1].type);
                    if (inst->operands[1].type == KRT_IR_VALUE_IMM) {
                        fprintf(stderr, ", imm=%f", inst->operands[1].data.imm);
                    }
                }
                fprintf(stderr, "\n");
                inst = inst->next;
            }
            fprintf(stderr, "  Total in linked list: %d, inst_count=%d\n", count, check_func->entry_block->inst_count);
        }
        check_func = check_func->next;
    }
#endif

    KROCodegenContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.output_file = output_file;
    if (output_filename) {
        size_t len = strlen(output_filename);
        if (len >= sizeof(ctx.output_filename)) len = sizeof(ctx.output_filename) - 1;
        memcpy(ctx.output_filename, output_filename, len);
        ctx.output_filename[len] = '\0';
    } else {
        memcpy(ctx.output_filename, "output.kro", 11);
    }
    ctx.writer = kro_writer_create();

    if (!ctx.writer) {
        return;
    }

    KroGenerateDataSection(&ctx, module);

    int func_count = 0;
    KrtIRFunction* temp_func = module->functions;
    while (temp_func) {
        func_count++;
        temp_func = temp_func->next;
    }

    if (func_count == 0) {
        kro_writer_destroy(ctx.writer);
        if (ctx.string_const_sym_indices) {
            KRT_FREE(ctx.string_const_sym_indices);
        }
        return;
    }

    int* sym_indices = (int*)KRT_MALLOC(func_count * sizeof(int));
    if (!sym_indices) {
        kro_writer_destroy(ctx.writer);
        if (ctx.string_const_sym_indices) {
            KRT_FREE(ctx.string_const_sym_indices);
        }
        return;
    }

    int func_idx = 0;
     KrtIRFunction* dbg_func = module->functions;
     while (dbg_func) {
         dbg_func = dbg_func->next;
     }

     bool has_mangled_main = kro_module_has_mangled_main(module);

     KrtIRFunction* func = module->functions;
     while (func) {
         uint32_t func_offset = kro_get_code_offset(ctx.writer);
        int sym_idx = kro_add_symbol(ctx.writer, func->name, KRO_SYM_FUNC, KRO_BIND_GLOBAL, KRO_SEC_TEXT, func_offset);
        sym_indices[func_idx] = sym_idx;

        if (is_entry_point_function(func->name, has_mangled_main)) {
            kro_set_entry_point(ctx.writer, func_offset);
        }

        func = func->next;
        func_idx++;
    }

    kro_set_code_offset(ctx.writer, 0);

    func_idx = 0;
    func = module->functions;
    while (func) {
        uint32_t actual_offset = kro_get_code_offset(ctx.writer);

        if (sym_indices[func_idx] >= 0) {
            kro_update_symbol_value(ctx.writer, sym_indices[func_idx], actual_offset);
#ifdef KRO_DEBUG
#endif
        }

        if (is_entry_point_function(func->name, has_mangled_main)) {
            kro_set_entry_point(ctx.writer, actual_offset);
        }

        KroGenerateFunction(&ctx, func, module);
        func = func->next;
        func_idx++;
    }

    KRT_FREE(sym_indices);

    {
        const char* main_name = "_KrtMainEntry";
        int main_sym = kro_find_symbol(ctx.writer, main_name);
        if (main_sym < 0) {
            main_name = "main";
            main_sym = kro_find_symbol(ctx.writer, main_name);
        }
        if (main_sym < 0) {
            main_name = "_ZN4MainEv";
            main_sym = kro_find_symbol(ctx.writer, main_name);
        }

        if (main_sym >= 0) {
            uint32_t start_offset = kro_get_code_offset(ctx.writer);
            kro_add_symbol(ctx.writer, "_start", KRO_SYM_FUNC, KRO_BIND_GLOBAL, KRO_SEC_TEXT, start_offset);
            kro_set_entry_point(ctx.writer, start_offset);

            uint8_t call_op = 0xE8;
            emit_byte(&ctx, call_op);
            uint32_t reloc_offset = kro_get_code_offset(ctx.writer);
            emit_u32(&ctx, 0);
            kro_add_reloc(ctx.writer, KRO_SEC_TEXT, reloc_offset, main_sym, KRO_RELOC_PC32, 0);

            /* mov rdi, rax */
            emit_bytes(&ctx, (const uint8_t*)"\x48\x89\xc7", 3);

            /* mov rax, 60 (sys_exit) */
            emit_bytes(&ctx, (const uint8_t*)"\x48\xc7\xc0\x3c\x00\x00\x00", 7);

            /* syscall */
            emit_bytes(&ctx, (const uint8_t*)"\x0f\x05", 2);

        }
    }

    if (!kro_write_file(ctx.writer, ctx.output_filename)) {
#ifdef KRO_DEBUG
#endif
    }

    if (ctx.string_const_sym_indices) {
        KRT_FREE(ctx.string_const_sym_indices);
    }
    kro_writer_destroy(ctx.writer);
}
