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

#define KRT_X86_MOV_R64_IMM64 0x48B8
#define KRT_X86_MOV_R64_R64 0x4889
#define KRT_X86_PUSH_R64 0x50
#define KRT_X86_POP_R64 0x58
#define KRT_X86_RET 0xC3
#define KRT_X86_CALL_REL32 0xE8
#define KRT_X86_JMP_REL32 0xE9
#define KRT_X86_ADD_R64_IMM32 0x4881
#define KRT_X86_SUB_R64_IMM32 0x4881
#define KRT_X86_XOR_R64_R64 0x4831
#define KRT_KRO_MAX_LABEL_LEN 256

#define KRT_REG_RAX 0
#define KRT_REG_RCX 1
#define KRT_REG_RDX 2
#define KRT_REG_RBX 3
#define KRT_REG_RSP 4
#define KRT_REG_RBP 5
#define KRT_REG_RSI 6
#define KRT_REG_RDI 7
#define KRT_REG_R8 8
#define KRT_REG_R9 9
#define KRT_REG_R10 10
#define KRT_REG_R11 11
#define KRT_REG_R12 12
#define KRT_REG_R13 13
#define KRT_REG_R14 14
#define KRT_REG_R15 15

static const int g_packed_regs[KRT_KRO_PACKED_REG_COUNT] = {KRT_REG_RBX, KRT_REG_R12, KRT_REG_R13, KRT_REG_R14,
                                                            KRT_REG_R15};

#ifdef __linux__
static const int g_arg_regs[] = {KRT_REG_RDI, KRT_REG_RSI, KRT_REG_RDX, KRT_REG_RCX, KRT_REG_R8, KRT_REG_R9};
static const int g_syscall_arg_regs[] = {KRT_REG_RDI, KRT_REG_RSI, KRT_REG_RDX, KRT_REG_R10, KRT_REG_R8, KRT_REG_R9};
static const int g_arg_reg_count = 6;
static const int g_syscall_arg_reg_count = 6;
#define KRT_KRO_SHADOW_SPACE_SIZE 0
#else
static const int g_arg_regs[] = {KRT_REG_RCX, KRT_REG_RDX, KRT_REG_R8, KRT_REG_R9};
static const int g_syscall_arg_regs[] = {KRT_REG_RCX, KRT_REG_RDX, KRT_REG_R8, KRT_REG_R9};
static const int g_arg_reg_count = 4;
static const int g_syscall_arg_reg_count = 4;
#define KRT_KRO_SHADOW_SPACE_SIZE 32
#endif

static bool kro_module_has_mangled_main(KrtIRModule* module) {
    if (!module) {
        return false;
    }
    KrtIRFunction* f = module->functions;
    while (f) {
        if (f->name && strcmp(f->name, "_KrtMainEntry") == 0) {
            return true;
        }
        f = f->next;
    }
    return false;
}

static bool is_entry_point_function(const char* name, bool has_mangled_main) {
    if (!name) {
        return false;
    }
    if (strcmp(name, "_KrtMainEntry") == 0) {
        return true;
    }
    if (strcmp(name, "main") == 0 && !has_mangled_main) {
        return true;
    }
    return false;
}

static void emit_byte(KROCodegenContext* ctx, uint8_t byte);
static void emit_u32(KROCodegenContext* ctx, uint32_t value);
static int alloc_temp_slot(KROCodegenContext* ctx, int temp_index);
static void emit_normalize_pair(KROCodegenContext* ctx, KrtTokenType type, int low, int high);
static void emit_store_value(KROCodegenContext* ctx, KrtIRValue* dest);
static void emit_reg_op(KROCodegenContext* ctx, uint8_t opcode, int dst, int src);
static void emit_load_packed_field(KROCodegenContext* ctx, const KROPackedField* field, bool is_unsigned,
                                   int target_reg);
static void emit_store_packed_field(KROCodegenContext* ctx, const KROPackedField* field, int source_reg);
static void emit_extend_scalar(KROCodegenContext* ctx, int bits, bool is_unsigned, int src, int dst);
static void plan_tail_calls(KROCodegenContext* ctx, KrtIRFunction* func);

static KROLocalVar* find_or_alloc_local_var(KROCodegenContext* ctx, const char* name) {
    if (!ctx || !name) {
        return NULL;
    }

    for (int i = 0; i < KRT_KRO_MAX_LOCAL_VARS && i < ctx->local_var_count; i++) {
        if (strcmp(ctx->local_vars[i].name, name) == 0) {
            return &ctx->local_vars[i];
        }
    }

    if (ctx->local_var_count >= KRT_KRO_MAX_LOCAL_VARS) {
        return NULL;
    }

    KROLocalVar* var = &ctx->local_vars[ctx->local_var_count];
    memset(var, 0, sizeof(*var));
    var->name = name;
    ctx->current_stack_offset += 16;
    var->stack_offset = ctx->current_stack_offset;
    var->allocated = 1;
    ctx->local_var_count++;

    return var;
}

static KROLocalVar* find_local_var(KROCodegenContext* ctx, const char* name) {
    if (!ctx || !name) {
        return NULL;
    }

    for (int i = 0; i < KRT_KRO_MAX_LOCAL_VARS && i < ctx->local_var_count; i++) {
        if (strcmp(ctx->local_vars[i].name, name) == 0) {
            return &ctx->local_vars[i];
        }
    }
    return NULL;
}

static void emit_store_to_stack(KROCodegenContext* ctx, int offset, int src_reg) {
    emit_byte(ctx, 0x48 | (src_reg >= 8 ? 0x04 : 0));
    emit_byte(ctx, 0x89);
    bool compact = offset >= -127 && offset <= 128;
    emit_byte(ctx, (compact ? 0x45 : 0x85) | ((src_reg & 0x7) << 3));
    if (compact) {
        emit_byte(ctx, (uint8_t)(-offset));
    } else {
        emit_u32(ctx, (uint32_t)(-offset));
    }
}

static void emit_load_from_stack(KROCodegenContext* ctx, int offset, int dst_reg) {
    emit_byte(ctx, 0x48 | (dst_reg >= 8 ? 0x04 : 0));
    emit_byte(ctx, 0x8B);
    bool compact = offset >= -127 && offset <= 128;
    emit_byte(ctx, (compact ? 0x45 : 0x85) | ((dst_reg & 0x7) << 3));
    if (compact) {
        emit_byte(ctx, (uint8_t)(-offset));
    } else {
        emit_u32(ctx, (uint32_t)(-offset));
    }
}

static void emit_byte(KROCodegenContext* ctx, uint8_t byte) {
    uint8_t data = byte;
    kro_write_code(ctx->writer, &data, 1);
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
    if (value == 0) {
        if (reg >= 8) {
            emit_byte(ctx, 0x45);
        }
        emit_byte(ctx, 0x31);
        emit_byte(ctx, 0xC0 | ((reg & 7) << 3) | (reg & 7));
        return;
    }
    if (value <= UINT32_MAX) {
        if (reg >= 8) {
            emit_byte(ctx, 0x41);
        }
        emit_byte(ctx, 0xB8 + (reg & 7));
        emit_u32(ctx, (uint32_t)value);
        return;
    }
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
    if (src_reg == dst_reg) {
        return;
    }
    emit_byte(ctx, 0x48 | (src_reg >= 8 ? 0x04 : 0) | (dst_reg >= 8 ? 0x01 : 0));
    emit_byte(ctx, 0x89);
    emit_byte(ctx, 0xC0 | ((src_reg & 0x7) << 3) | (dst_reg & 0x7));
}

static void emit_store_temp_result(KROCodegenContext* ctx, KrtIRInst* inst, int reg) {
    if (!ctx || !inst || inst->result.type != KRT_IR_VALUE_TEMP) {
        return;
    }
    emit_move_reg_to_reg(ctx, reg, KRT_REG_RAX);
    emit_store_value(ctx, &inst->result);
}

static void make_block_symbol_name(KROCodegenContext* ctx, KrtIRBasicBlock* block, char* buffer, size_t buffer_size) {
    uint32_t function_hash = 2166136261u;
    const char* function_name = ctx ? ctx->current_function_name : NULL;
    while (function_name && *function_name) {
        function_hash ^= (uint8_t)*function_name++;
        function_hash *= 16777619u;
    }
    snprintf(buffer, buffer_size, "__krt_bb_%08x_%d", function_hash, block ? block->id : -1);
}

static int find_block_symbol(KROCodegenContext* ctx, KrtIRBasicBlock* block) {
    char name[64];
    make_block_symbol_name(ctx, block, name, sizeof(name));
    return kro_find_symbol(ctx->writer, name);
}

static void emit_jump_to_block(KROCodegenContext* ctx, KrtIRBasicBlock* target) {
    int sym_idx = find_block_symbol(ctx, target);
    if (sym_idx < 0) {
        return;
    }
    emit_byte(ctx, 0xE9);
    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
    emit_u32(ctx, 0);
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
}

static void emit_cond_jump_to_block(KROCodegenContext* ctx, uint8_t condition, KrtIRBasicBlock* target) {
    int sym_idx = find_block_symbol(ctx, target);
    if (sym_idx < 0) {
        return;
    }
    emit_byte(ctx, 0x0F);
    emit_byte(ctx, condition);
    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
    emit_u32(ctx, 0);
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
}

static int instruction_value_size(KrtIRInst* inst, int operand_index, int default_size) {
    if (!inst || operand_index < 0 || operand_index >= inst->operand_count) {
        return default_size;
    }
    KrtIRValue* size = &inst->operands[operand_index];
    if (size->type != KRT_IR_VALUE_IMM) {
        return default_size;
    }
    int value = (int)size->data.imm;
    return value == 1 || value == 2 || value == 4 || value == 8 || value == 16 ? value : default_size;
}

static void emit_load_indirect(KROCodegenContext* ctx, int address_reg, int target_reg, int size) {
    if (address_reg != KRT_REG_RAX || target_reg != KRT_REG_RAX) {
        emit_move_reg_to_reg(ctx, address_reg, KRT_REG_RAX);
    }
    switch (size) {
    case 1:
        emit_bytes(ctx, (const uint8_t*)"\x0F\xB6\x00", 3);
        break;
    case 2:
        emit_bytes(ctx, (const uint8_t*)"\x0F\xB7\x00", 3);
        break;
    case 4:
        emit_bytes(ctx, (const uint8_t*)"\x8B\x00", 2);
        break;
    default:
        emit_bytes(ctx, (const uint8_t*)"\x48\x8B\x00", 3);
        break;
    }
    if (target_reg != KRT_REG_RAX) {
        emit_move_reg_to_reg(ctx, KRT_REG_RAX, target_reg);
    }
}

static void emit_store_indirect(KROCodegenContext* ctx, int address_reg, int value_reg, int size) {
    if (address_reg != KRT_REG_RAX) {
        emit_move_reg_to_reg(ctx, address_reg, KRT_REG_RAX);
    }
    if (value_reg != KRT_REG_RDX) {
        emit_move_reg_to_reg(ctx, value_reg, KRT_REG_RDX);
    }
    switch (size) {
    case 1:
        emit_bytes(ctx, (const uint8_t*)"\x88\x10", 2);
        break;
    case 2:
        emit_bytes(ctx, (const uint8_t*)"\x66\x89\x10", 3);
        break;
    case 4:
        emit_bytes(ctx, (const uint8_t*)"\x89\x10", 2);
        break;
    default:
        emit_bytes(ctx, (const uint8_t*)"\x48\x89\x10", 3);
        break;
    }
}

static void emit_function_prologue(KROCodegenContext* ctx, int stack_size) {

    emit_byte(ctx, 0x55);

    emit_bytes(ctx, (const uint8_t*)"\x48\x89\xE5", 3);

    if (stack_size > 0 && stack_size <= 127) {
        emit_bytes(ctx, (const uint8_t*)"\x48\x83\xEC", 3);
        emit_byte(ctx, (uint8_t)stack_size);
    } else if (stack_size > 0) {
        emit_bytes(ctx, (const uint8_t*)"\x48\x81\xEC", 3);
        emit_u32(ctx, (uint32_t)stack_size);
    }
    for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
        if (!ctx->packed_reg_bits[i]) {
            continue;
        }
        emit_store_to_stack(ctx, ctx->packed_reg_save_offsets[i], g_packed_regs[i]);
        if (ctx->packed_reg_needs_zero[i]) {
            emit_reg_op(ctx, 0x31, g_packed_regs[i], g_packed_regs[i]);
        }
    }
}

static void emit_function_epilogue(KROCodegenContext* ctx) {
    for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
        if (ctx->packed_reg_bits[i]) {
            emit_load_from_stack(ctx, ctx->packed_reg_save_offsets[i], g_packed_regs[i]);
        }
    }
    emit_bytes(ctx, (const uint8_t*)"\x48\x89\xEC", 3);

    emit_byte(ctx, 0x5D);

    emit_byte(ctx, KRT_X86_RET);
}

static void emit_load_imm64(KROCodegenContext* ctx, uint64_t value) {
    emit_load_imm64_to_reg(ctx, value, KRT_REG_RAX);
}

static uint64_t encode_integer_immediate(double value) {
    int64_t signed_value = (int64_t)value;
    uint64_t encoded = 0;
    memcpy(&encoded, &signed_value, sizeof(encoded));
    return encoded;
}

static void emit_call_external(KROCodegenContext* ctx, const char* func_name) {
    if (!ctx || !func_name) {
        return;
    }

    int32_t sym_idx = kro_find_symbol(ctx->writer, func_name);
    if (sym_idx < 0) {
#ifdef _WIN32
        if (strcmp(func_name, "puts") == 0) {
            sym_idx = kro_add_import_symbol(ctx->writer, "puts", "msvcrt.dll");
        } else if (strcmp(func_name, "Console__Write") == 0 || strcmp(func_name, "Console__WriteLine") == 0) {
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
        } else if (strcmp(func_name, "Console__Write") == 0 || strcmp(func_name, "Console__WriteLine") == 0) {
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

    if (sym_idx < 0) {
        return;
    }

    emit_byte(ctx, 0x48);
    emit_byte(ctx, 0xB8);
    uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
    emit_u64(ctx, 0);
    emit_byte(ctx, 0xFF);
    emit_byte(ctx, 0xD0);

    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_ABS64, 0);
}

static void emit_call_local(KROCodegenContext* ctx, const char* func_name) {
    if (!ctx || !func_name) {
        return;
    }

    int32_t sym_idx = kro_find_symbol(ctx->writer, func_name);

    if (sym_idx < 0) {
        sym_idx = kro_add_undefined_symbol(ctx->writer, func_name);
    }

    if (sym_idx < 0) {
        return;
    }

    uint32_t call_offset = kro_get_code_offset(ctx->writer);

    emit_byte(ctx, 0xE8);
    emit_u32(ctx, 0);

    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, call_offset + 1, sym_idx, KRO_RELOC_PC32, 0);
}

static KROTempSlot* find_temp_location(KROCodegenContext* ctx, int temp_index) {
    unsigned bucket = (unsigned)temp_index * 2654435761u & (KRT_KRO_TEMP_INDEX_SIZE - 1);
    while (ctx->temp_index[bucket]) {
        KROTempSlot* slot = &ctx->temp_slots[ctx->temp_index[bucket] - 1];
        if (slot->temp_index == temp_index) {
            return slot;
        }
        bucket = (bucket + 1) & (KRT_KRO_TEMP_INDEX_SIZE - 1);
    }
    return NULL;
}

static KROTempSlot* create_temp_location(KROCodegenContext* ctx, int temp_index) {
    KROTempSlot* slot = find_temp_location(ctx, temp_index);
    if (slot) {
        return slot;
    }
    if (ctx->temp_slot_count >= KRT_KRO_MAX_TEMP_REGS) {
        return NULL;
    }
    int index = ctx->temp_slot_count++;
    slot = &ctx->temp_slots[index];
    memset(slot, 0, sizeof(*slot));
    slot->temp_index = temp_index;
    slot->valid = 1;
    slot->reg_low = slot->reg_high = -1;
    slot->first_position = -1;
    slot->last_position = -1;
    unsigned bucket = (unsigned)temp_index * 2654435761u & (KRT_KRO_TEMP_INDEX_SIZE - 1);
    while (ctx->temp_index[bucket]) {
        bucket = (bucket + 1) & (KRT_KRO_TEMP_INDEX_SIZE - 1);
    }
    ctx->temp_index[bucket] = index + 1;
    return slot;
}

static int find_temp_slot(KROCodegenContext* ctx, int temp_index) {
    KROTempSlot* slot = find_temp_location(ctx, temp_index);
    return slot ? slot->stack_offset : -1;
}

static int alloc_temp_slot(KROCodegenContext* ctx, int temp_index) {
    KROTempSlot* slot = create_temp_location(ctx, temp_index);
    if (!slot) {
        return -1;
    }
    if (!slot->stack_offset && slot->reg_low < 0) {
        ctx->current_stack_offset += 16;
        slot->stack_offset = ctx->current_stack_offset;
    }
    return slot->stack_offset;
}

/* Expression floats use doubles; float32 addressable storage uses IEEE binary32. */
static void emit_float32_conversion(KROCodegenContext* ctx, int reg, bool encode) {
    emit_byte(ctx, 0x66);
    emit_byte(ctx, 0x48 | (reg >= 8 ? 1 : 0));
    emit_bytes(ctx, (const uint8_t*)"\x0F\x6E", 2);
    emit_byte(ctx, 0xC0 | (reg & 7));
    emit_byte(ctx, encode ? 0xF2 : 0xF3);
    emit_bytes(ctx, (const uint8_t*)"\x0F\x5A\xC0", 3);
    emit_byte(ctx, 0x66);
    emit_byte(ctx, 0x48 | (reg >= 8 ? 1 : 0));
    emit_bytes(ctx, (const uint8_t*)"\x0F\x7E", 2);
    emit_byte(ctx, 0xC0 | (reg & 7));
}

static void emit_load_value_to_reg(KROCodegenContext* ctx, KrtIRValue* value, int target_reg) {
    if (!ctx || !value) {
        return;
    }

    switch (value->type) {
    case KRT_IR_VALUE_VOID:
        emit_load_imm64_to_reg(ctx, 0, target_reg);
        break;
    case KRT_IR_VALUE_INTEGER:
        emit_load_imm64_to_reg(ctx, (uint64_t)value->data.integer, target_reg);
        break;
    case KRT_IR_VALUE_IMM:
        emit_load_imm64_to_reg(ctx, encode_integer_immediate(value->data.imm), target_reg);
        break;
    case KRT_IR_VALUE_IMM_F: {
        /* 双精度立即数: 按 IEEE754 位型装入 GP 寄存器, 供 SSE 指令按 xmm 解释 */
        uint64_t bits;
        memcpy(&bits, &value->data.imm, sizeof(bits));
        emit_load_imm64_to_reg(ctx, (int64_t)bits, target_reg);
        break;
    }
    case KRT_IR_VALUE_VAR: {
        KROLocalVar* local_var = find_local_var(ctx, value->data.name);
        if (local_var && local_var->allocated) {
            if (local_var->packed.bit_width && KrtTokenIntegerBits(local_var->type) <= 64) {
                emit_load_packed_field(ctx, &local_var->packed, KrtTokenIsUnsigned(local_var->type), target_reg);
            } else {
                emit_load_from_stack(ctx, local_var->stack_offset, target_reg);
                if (local_var->type == TOKEN_FLOAT32) {
                    emit_float32_conversion(ctx, target_reg, false);
                }
                int bits = KrtTokenIntegerBits(local_var->type);
                if (local_var->address_taken && bits && bits < 64) {
                    emit_extend_scalar(ctx, bits, KrtTokenIsUnsigned(local_var->type), target_reg, target_reg);
                }
            }
        } else {
            int sym_idx = kro_find_symbol(ctx->writer, value->data.name);
            if (sym_idx >= 0) {
                emit_byte(ctx, 0x48 | (target_reg >= 8 ? 0x04 : 0));
                emit_byte(ctx, 0x8B);
                emit_byte(ctx, 0x05 + ((target_reg & 0x7) << 3));
                uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
                emit_u32(ctx, 0);
                kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym_idx, KRO_RELOC_PC32, 0);
                if (value->value_type == TOKEN_FLOAT32) {
                    emit_float32_conversion(ctx, target_reg, false);
                }
                int bits = KrtTokenIntegerBits(value->value_type);
                if (bits && bits < 64) {
                    emit_extend_scalar(ctx, bits, KrtTokenIsUnsigned(value->value_type), target_reg, target_reg);
                }
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
        KROTempSlot* slot = find_temp_location(ctx, value->data.index);
        if (slot && slot->reg_low >= 0) {
            emit_move_reg_to_reg(ctx, slot->reg_low, target_reg);
        } else if (slot && slot->stack_offset > 0) {
            emit_load_from_stack(ctx, slot->stack_offset, target_reg);
        }
        break;
    }
    case KRT_IR_VALUE_ARG: {
        int arg_idx = value->data.index;
        if (arg_idx >= 0 && arg_idx < ctx->current_param_count && ctx->arg_packed[arg_idx].bit_width &&
            KrtTokenIntegerBits(value->value_type) <= 64) {
            emit_load_packed_field(ctx, &ctx->arg_packed[arg_idx], KrtTokenIsUnsigned(value->value_type), target_reg);
        } else if (arg_idx >= 0 && arg_idx < ctx->current_param_count && ctx->arg_stack_offsets[arg_idx] > 0) {
            emit_load_from_stack(ctx, ctx->arg_stack_offsets[arg_idx], target_reg);
            if (value->value_type == TOKEN_FLOAT32) {
                emit_float32_conversion(ctx, target_reg, false);
            }
            int bits = KrtTokenIntegerBits(value->value_type);
            if (ctx->arg_address_taken[arg_idx] && bits && bits < 64) {
                emit_extend_scalar(ctx, bits, KrtTokenIsUnsigned(value->value_type), target_reg, target_reg);
            }
        } else if (arg_idx >= 0 && arg_idx < g_arg_reg_count) {
            emit_move_reg_to_reg(ctx, g_arg_regs[arg_idx], target_reg);
        }
        break;
    }
    case KRT_IR_VALUE_FUNCTION: {
        int symbol = kro_find_symbol(ctx->writer, value->data.function_name);
        if (symbol < 0) {
            symbol =
                kro_add_symbol(ctx->writer, value->data.function_name, KRO_SYM_FUNC, KRO_BIND_GLOBAL, KRO_SEC_UNDEF, 0);
        }
        emit_load_string_addr_to_reg(ctx, symbol, target_reg);
        break;
    }
    case KRT_IR_VALUE_STRING_CONST: {
        int32_t sym_idx = -1;
        if (value->data.string_const_id >= 0 && ctx->string_const_sym_indices &&
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

/* Sized integer loads also handle values modified through byte-pointer aliases. */
#include "KroInteger.inc"
#include "KroRegisterPacking.inc"
static void emit_tail_backedge(KROCodegenContext* ctx);
#include "KroTailCalls.inc"
#include "KroLeafReturn.inc"

static void emit_branch_targets(KROCodegenContext* ctx, uint8_t condition) {
    KrtIRBasicBlock* block = ctx->current_block;
    if (block->succs[0] == block->next) {
        emit_cond_jump_to_block(ctx, condition ^ 1, block->succs[1]);
    } else {
        emit_cond_jump_to_block(ctx, condition, block->succs[0]);
        if (block->succs[1] != block->next) {
            emit_jump_to_block(ctx, block->succs[1]);
        }
    }
}

static void generate_instruction(KROCodegenContext* ctx, KrtIRInst* inst) {
    if (!ctx || !inst) {
        return;
    }
    if (ctx->optimization_level >= 3 && ctx->tail_site_count && ctx->tail_acc_opcode == KRT_IR_NOP &&
        is_deferred_tail_argument(ctx, inst)) {
        return;
    }

    switch (inst->opcode) {
    case KRT_IR_IMM:
        if (inst->operand_count >= 1) {
            emit_load_for_type(ctx, &inst->operands[0], inst->result.value_type, KRT_REG_RAX, KRT_REG_RDX);
            emit_store_value(ctx, &inst->result);
        }
        break;

    case KRT_IR_ADD:
    case KRT_IR_SUB:
    case KRT_IR_MUL:
    case KRT_IR_DIV:
    case KRT_IR_MOD:
    case KRT_IR_AND:
    case KRT_IR_OR:
    case KRT_IR_XOR:
    case KRT_IR_LSHIFT:
    case KRT_IR_RSHIFT:
        if (inst->operand_count >= 2) {
            emit_integer_binary(ctx, inst);
        }
        break;

    case KRT_IR_LT:
    case KRT_IR_GT:
    case KRT_IR_EQ:
    case KRT_IR_LE:
    case KRT_IR_GE:
    case KRT_IR_NE:
        if (inst->operand_count >= 2) {
            emit_integer_compare(ctx, inst);
        }
        break;

    /* ---- SSE2 双精度浮点(C24) ----
         * 桥接策略: 操作数经既有机制装入 GP 寄存器, movq 进 xmm0/xmm1,
         * 标量双精度运算后 movq 回 RAX 写结果槽 —— 无需完整 SSE 分配器 */
    case KRT_IR_FADD:
    case KRT_IR_FSUB:
    case KRT_IR_FMUL:
    case KRT_IR_FDIV: {
        if (inst->operand_count < 2) {
            break;
        }
        emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_RAX);
        emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RCX);
        emit_bytes(ctx, (const uint8_t*)"\x66\x48\x0F\x6E\xC0", 5); /* movq xmm0,rax */
        emit_bytes(ctx, (const uint8_t*)"\x66\x48\x0F\x6E\xC9", 5); /* movq xmm1,rcx */
        switch (inst->opcode) {
        case KRT_IR_FADD:
            emit_bytes(ctx, (const uint8_t*)"\xF2\x0F\x58\xC1", 4);
            break; /* addsd */
        case KRT_IR_FSUB:
            emit_bytes(ctx, (const uint8_t*)"\xF2\x0F\x5C\xC1", 4);
            break; /* subsd */
        case KRT_IR_FMUL:
            emit_bytes(ctx, (const uint8_t*)"\xF2\x0F\x59\xC1", 4);
            break; /* mulsd */
        case KRT_IR_FDIV:
            emit_bytes(ctx, (const uint8_t*)"\xF2\x0F\x5E\xC1", 4);
            break; /* divsd */
        default:
            break;
        }
        emit_bytes(ctx, (const uint8_t*)"\x66\x48\x0F\x7E\xC0", 5); /* movq rax,xmm0 */
        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }

    case KRT_IR_FEQ:
    case KRT_IR_FNE:
    case KRT_IR_FLT:
    case KRT_IR_FLE:
    case KRT_IR_FGT:
    case KRT_IR_FGE: {
        if (inst->operand_count < 2) {
            break;
        }
        emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_RAX);
        emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RCX);
        emit_bytes(ctx, (const uint8_t*)"\x66\x48\x0F\x6E\xC0", 5); /* movq xmm0,rax */
        emit_bytes(ctx, (const uint8_t*)"\x66\x48\x0F\x6E\xC9", 5); /* movq xmm1,rcx */
        if (inst->opcode == KRT_IR_FGT || inst->opcode == KRT_IR_FGE) {
            /* GT/GE 无直接 comisd 条件: 翻转操作数后按 LT/LE 取标志 */
            emit_bytes(ctx, (const uint8_t*)"\x66\x0F\x2F\xC8", 4); /* comisd xmm1,xmm0 */
        } else {
            emit_bytes(ctx, (const uint8_t*)"\x66\x0F\x2F\xC1", 4); /* comisd xmm0,xmm1 */
        }
        /* 条件置字节 + 有序保护(NaN 时 PF=1 -> 强制假): 结果 = cc 且 !PF */
        emit_byte(ctx, 0x0F);
        switch (inst->opcode) {
        case KRT_IR_FEQ:
            emit_byte(ctx, 0x94);
            break; /* sete  (ZF)      */
        case KRT_IR_FNE:
            emit_byte(ctx, 0x95);
            break; /* setne (!ZF)     */
        case KRT_IR_FLT:
            emit_byte(ctx, 0x92);
            break; /* setb  (CF)      */
        case KRT_IR_FLE:
            emit_byte(ctx, 0x96);
            break; /* setbe (CF|ZF)   */
        case KRT_IR_FGT:
            emit_byte(ctx, 0x92);
            break; /* 翻转后 setb     */
        case KRT_IR_FGE:
            emit_byte(ctx, 0x96);
            break; /* 翻转后 setbe    */
        default:
            break;
        }
        emit_byte(ctx, 0xC0);
        if (inst->opcode == KRT_IR_FNE) {
            emit_bytes(ctx, (const uint8_t*)"\x0F\x9A\xC2\x08\xD0", 5); /* unordered is unequal */
        } else {
            emit_bytes(ctx, (const uint8_t*)"\x0F\x9B\xC2\x22\xC2", 5);
        }
        emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xB6\xC0", 4); /* movzx rax,al */
        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }

    case KRT_IR_CAST: {
        if (is_coalesced_register_copy(ctx, inst)) {
            break;
        }
        if (inst->operand_count < 1) {
            break;
        }
        if (inst->result.value_type == TOKEN_BOOL) {
            emit_load_condition(ctx, &inst->operands[0]);
            emit_bytes(ctx, (const uint8_t*)"\x0F\x95\xC0\x48\x0F\xB6\xC0", 7);
        } else {
            emit_load_for_type(ctx, &inst->operands[0], inst->result.value_type, KRT_REG_RAX, KRT_REG_RDX);
        }
        emit_store_value(ctx, &inst->result);
        break;
    }

    case KRT_IR_JUMP: {
        if (ctx->current_block && ctx->current_block->succ_count > 0) {
            if (ctx->current_block->succs[0] != ctx->current_block->next) {
                emit_jump_to_block(ctx, ctx->current_block->succs[0]);
            }
        }
        break;
    }

    case KRT_IR_BRANCH: {
        if (inst->operand_count < 1 || !ctx->current_block || ctx->current_block->succ_count < 2) {
            break;
        }
        emit_load_condition(ctx, &inst->operands[0]);
        emit_branch_targets(ctx, 0x85);
        break;
    }

    case KRT_IR_CALL_INDIRECT:
    case KRT_IR_CALL: {
        bool indirect = inst->opcode == KRT_IR_CALL_INDIRECT;
        if (inst->operand_count < 1 || (!indirect && inst->operands[0].type != KRT_IR_VALUE_FUNCTION)) {
            break;
        }

        const char* func_name = indirect ? "" : inst->operands[0].data.function_name;
        if (!func_name) {
            break;
        }

        if (strcmp(func_name, "KrtStorePtr") == 0 && inst->operand_count >= 4) {
            emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RAX); /* ptr */
            emit_load_value_to_reg(ctx, &inst->operands[2], KRT_REG_RCX); /* offset */
            if (!(inst->operands[2].type == KRT_IR_VALUE_IMM && inst->operands[2].data.imm == 0)) {
                emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3); /* add rax, rcx */
            }
            emit_load_value_to_reg(ctx, &inst->operands[3], KRT_REG_RDX); /* value */
            emit_bytes(ctx, (const uint8_t*)"\x48\x89\x10", 3);           /* mov [rax], rdx */
            break;
        }
        if (strcmp(func_name, "KrtLoadPtr") == 0 && inst->operand_count >= 3) {
            emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RAX); /* ptr */
            emit_load_value_to_reg(ctx, &inst->operands[2], KRT_REG_RCX); /* offset */
            if (!(inst->operands[2].type == KRT_IR_VALUE_IMM && inst->operands[2].data.imm == 0)) {
                emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3); /* add rax, rcx */
            }
            emit_bytes(ctx, (const uint8_t*)"\x48\x8B\x00", 3); /* mov rax, [rax] */
            emit_store_temp_result(ctx, inst, KRT_REG_RAX);
            break;
        }

        int arg_count = inst->operand_count - 1;
        int reg_index = 0, stack_words = 0;
        for (int i = 0; i < arg_count; i++) {
            int words = KrtTokenIntegerBits(inst->operands[i + 1].value_type) > 64 ? 2 : 1;
            if (reg_index + words <= g_arg_reg_count) {
                reg_index += words;
            } else {
                stack_words += words;
            }
        }
        int outgoing = (stack_words * 8 + KRT_KRO_SHADOW_SPACE_SIZE + 15) & ~15;
        if (outgoing) {
            emit_bytes(ctx, (const uint8_t*)"\x48\x81\xEC", 3);
            emit_u32(ctx, outgoing);
        }
        reg_index = 0;
        int stack_offset = KRT_KRO_SHADOW_SPACE_SIZE;
        for (int i = 0; i < arg_count; i++) {
            KrtIRValue* arg = &inst->operands[i + 1];
            int words = KrtTokenIntegerBits(arg->value_type) > 64 ? 2 : 1;
            if (reg_index + words <= g_arg_reg_count) {
                reg_index += words;
                continue;
            }
            emit_load_pair(ctx, arg, KRT_REG_RAX, KRT_REG_RDX);
            emit_bytes(ctx, (const uint8_t*)"\x48\x89\x84\x24", 4);
            emit_u32(ctx, stack_offset);
            if (words == 2) {
                emit_bytes(ctx, (const uint8_t*)"\x48\x89\x94\x24", 4);
                emit_u32(ctx, stack_offset + 8);
            }
            stack_offset += words * 8;
        }
        reg_index = 0;
        for (int i = 0; i < arg_count; i++) {
            KrtIRValue* arg = &inst->operands[i + 1];
            int words = KrtTokenIntegerBits(arg->value_type) > 64 ? 2 : 1;
            if (reg_index + words > g_arg_reg_count) {
                continue;
            }
            if (words == 2) {
                emit_load_pair(ctx, arg, g_arg_regs[reg_index], g_arg_regs[reg_index + 1]);
            } else {
                emit_load_value_to_reg(ctx, arg, g_arg_regs[reg_index]);
            }
            reg_index += words;
        }

        int32_t local_sym_idx = kro_find_symbol(ctx->writer, func_name);
        if (indirect) {
            emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_R11);
            emit_bytes(ctx, (const uint8_t*)"\x41\xFF\xD3", 3); /* call r11 */
        } else if (local_sym_idx >= 0) {
            emit_call_local(ctx, func_name);
        } else {
            emit_call_external(ctx, func_name);
        }
        if (outgoing) {
            emit_bytes(ctx, (const uint8_t*)"\x48\x81\xC4", 3);
            emit_u32(ctx, outgoing);
        }
        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }

    case KRT_IR_RETURN: {
        bool identity = inst->operand_count >= 1 && emit_tail_identity_return(ctx, &inst->operands[0]);
        if (!identity && inst->operand_count >= 1 && inst->operands[0].type != KRT_IR_VALUE_VOID) {
            KrtIRValue* ret_val = &inst->operands[0];
            emit_load_for_type(ctx, ret_val, ret_val->value_type, KRT_REG_RAX, KRT_REG_RDX);
        } else if (!identity) {
            emit_load_imm64(ctx, 0);
        }
        if (!identity) {
            emit_tail_accumulate(ctx, false, KRT_REG_RAX);
        }
        if (!ctx->current_block || ctx->current_block->next) {
            emit_byte(ctx, 0xE9);
            uint32_t offset = kro_get_code_offset(ctx->writer);
            emit_u32(ctx, 0);
            kro_add_reloc(ctx->writer, KRO_SEC_TEXT, offset, ctx->epilogue_symbol, KRO_RELOC_PC32, 0);
        }
        break;
    }

    case KRT_IR_STORE:
        if (inst->operand_count >= 2) {
            emit_load_for_type(ctx, &inst->operands[1], inst->operands[0].value_type, KRT_REG_RAX, KRT_REG_RDX);
            emit_store_value(ctx, &inst->operands[0]);
        }
        break;

    case KRT_IR_LOADPTR: {
        if (inst->operand_count < 1) {
            break;
        }
        emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_RAX);
        if (inst->operand_count >= 2) {
            emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RCX);
            emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);
        }
        int size = instruction_value_size(inst, 2, 8);
        if (size == 16) {
            emit_bytes(ctx, (const uint8_t*)"\x48\x8B\x50\x08", 4);
        }
        emit_load_indirect(ctx, KRT_REG_RAX, KRT_REG_RAX, size);
        if (inst->result.value_type == TOKEN_FLOAT32) {
            emit_float32_conversion(ctx, KRT_REG_RAX, false);
        }
        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }

    case KRT_IR_STOREPTR: {
        if (inst->operand_count < 3) {
            break;
        }
        emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_RAX);
        emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RCX);
        emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);
        emit_load_value_to_reg(ctx, &inst->operands[2], KRT_REG_RDX);
        int size = instruction_value_size(inst, 3, 8);
        if (inst->operands[2].value_type == TOKEN_FLOAT32) {
            emit_float32_conversion(ctx, KRT_REG_RDX, true);
        }
        if (size == 16) {
            emit_load_high(ctx, &inst->operands[2], KRT_REG_RDX, KRT_REG_R8);
            emit_bytes(ctx, (const uint8_t*)"\x4C\x89\x40\x08", 4);
        }
        emit_store_indirect(ctx, KRT_REG_RAX, KRT_REG_RDX, size);
        break;
    }

    case KRT_IR_ARRAY_STORE: {
        if (inst->operand_count < 3) {
            break;
        }
        int size = instruction_value_size(inst, 3, 8);
        emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_RAX);
        emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RCX);
        if (size != 1) {
            emit_bytes(ctx, (const uint8_t*)"\x48\x6B\xC9", 3);
            emit_byte(ctx, (uint8_t)size);
        }
        emit_bytes(ctx, (const uint8_t*)"\x48\x01\xC8", 3);
        emit_load_value_to_reg(ctx, &inst->operands[2], KRT_REG_RDX);
        if (inst->operands[2].value_type == TOKEN_FLOAT32) {
            emit_float32_conversion(ctx, KRT_REG_RDX, true);
        }
        if (size == 16) {
            emit_load_high(ctx, &inst->operands[2], KRT_REG_RDX, KRT_REG_R8);
            emit_bytes(ctx, (const uint8_t*)"\x4C\x89\x40\x08", 4);
        }
        emit_store_indirect(ctx, KRT_REG_RAX, KRT_REG_RDX, size);
        break;
    }

    case KRT_IR_LOAD:
        if (inst->operand_count >= 1) {
            emit_load_for_type(ctx, &inst->operands[0], inst->result.value_type, KRT_REG_RAX, KRT_REG_RDX);
            emit_store_value(ctx, &inst->result);
        }
        break;

    case KRT_IR_ADDRESS_OF: {
        KrtIRValue* storage = &inst->operands[0];
        int offset = 0;
        if (storage->type == KRT_IR_VALUE_ARG) {
            offset = ctx->arg_stack_offsets[storage->data.index];
        } else if (storage->type == KRT_IR_VALUE_VAR) {
            KROLocalVar* var = find_local_var(ctx, storage->data.name);
            if (var) {
                offset = var->stack_offset;
            } else {
                int symbol = kro_find_symbol(ctx->writer, storage->data.name);
                emit_load_string_addr_to_reg(ctx, symbol, KRT_REG_RAX);
            }
        }
        if (offset) {
            emit_bytes(ctx, (const uint8_t*)"\x48\x8D\x85", 3); /* lea rax,[rbp-offset] */
            emit_u32(ctx, (uint32_t)-offset);
        }
        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }
    case KRT_IR_STACKALLOC: {
        emit_load_value_to_reg(ctx, &inst->operands[0], KRT_REG_RAX);
        if (KrtTokenIntegerBits(inst->operands[0].value_type) > 64) {
            emit_load_high(ctx, &inst->operands[0], KRT_REG_RAX, KRT_REG_RDX);
            emit_bytes(ctx, (const uint8_t*)"\x48\x85\xD2\x74\x02\x0F\x0B", 7);
        }
        emit_load_value_to_reg(ctx, &inst->operands[1], KRT_REG_RCX);
        /* Checked byte size and alignment; reject negative/overflowing sizes. */
        emit_bytes(ctx, (const uint8_t*)"\x48\xF7\xE1\x48\x85\xD2\x74\x02\x0F\x0B", 10);
        emit_bytes(ctx, (const uint8_t*)"\x48\x83\xC0\x0F\x73\x02\x0F\x0B", 8);
        emit_bytes(ctx, (const uint8_t*)"\x48\x85\xC0\x79\x02\x0F\x0B", 7);
        emit_bytes(ctx, (const uint8_t*)"\x48\x83\xE0\xF0", 4);
        /* Probe every stack page. All permanent frame slots stay relative to rbp. */
        emit_bytes(ctx, (const uint8_t*)"\x48\x3D\x00\x10\x00\x00\x72\x13", 8);
        emit_bytes(ctx, (const uint8_t*)"\x48\x81\xEC\x00\x10\x00\x00\xF6\x04\x24\x00", 11);
        emit_bytes(ctx, (const uint8_t*)"\x48\x2D\x00\x10\x00\x00\xEB\xE5", 8);
        emit_bytes(ctx, (const uint8_t*)"\x48\x29\xC4\x48\x89\xE0", 6);
        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }
    case KRT_IR_ALLOC: {
        if (inst->operand_count < 1) {
            break;
        }

        KrtIRValue* var = &inst->operands[0];
        if (var->type == KRT_IR_VALUE_VAR) {
            find_or_alloc_local_var(ctx, var->data.name);
        }
        break;
    }

    case KRT_IR_COPY:
        if (is_coalesced_register_copy(ctx, inst)) {
            break;
        }
        if (inst->operand_count >= 1) {
            emit_load_for_type(ctx, &inst->operands[0], inst->result.value_type, KRT_REG_RAX, KRT_REG_RDX);
            emit_store_value(ctx, &inst->result);
        }
        break;

    case KRT_IR_PHI: {
        if (inst->result.type == KRT_IR_VALUE_TEMP) {
            alloc_temp_slot(ctx, inst->result.data.index);
        }
        break;
    }

    case KRT_IR_SYSCALL: {
        if (inst->operand_count < 1) {
            break;
        }
        KrtIRValue* syscall_num = &inst->operands[0];
        int arg_count = inst->operand_count - 1;
        if (arg_count > g_syscall_arg_reg_count) {
            arg_count = g_syscall_arg_reg_count;
        }

        for (int i = arg_count - 1; i >= 0; i--) {
            KrtIRValue* arg = &inst->operands[i + 1];
            int target_reg = g_syscall_arg_regs[i];
            emit_load_value_to_reg(ctx, arg, target_reg);
        }

        emit_load_value_to_reg(ctx, syscall_num, KRT_REG_RAX);

        emit_byte(ctx, 0x0F);
        emit_byte(ctx, 0x05);

        emit_store_temp_result(ctx, inst, KRT_REG_RAX);
        break;
    }

    default:
        break;
    }
}

static void generate_block(KROCodegenContext* ctx, KrtIRBasicBlock* block) {
    if (!block) {
        return;
    }

    ctx->current_block = block;
    int block_sym_idx = find_block_symbol(ctx, block);
    if (block_sym_idx >= 0) {
        kro_update_symbol_value(ctx->writer, block_sym_idx, kro_get_code_offset(ctx->writer));
    }

    KrtIRInst* inst = block->first_inst;
    while (inst) {
        if (emit_tail_call(ctx, inst)) {
            break;
        }
        if (can_fuse_integer_branch(ctx, block, inst)) {
            KrtTokenType type = KrtTokenIntegerCommon(inst->operands[0].value_type, inst->operands[1].value_type);
            emit_scalar_compare_flags(ctx, inst, type);
            emit_branch_targets(ctx, integer_condition(inst->opcode, KrtTokenIsUnsigned(type)) - 0x10);
            break;
        }
        generate_instruction(ctx, inst);
        if (inst->result.type == KRT_IR_VALUE_TEMP) {
            KROTempSlot* result = find_temp_location(ctx, inst->result.data.index);
            if (result && result->early_tail_accumulate) {
                emit_tail_accumulate(ctx, true, KRT_REG_RAX);
            }
        }
        if (inst->opcode == KRT_IR_RETURN || inst->opcode == KRT_IR_JUMP || inst->opcode == KRT_IR_BRANCH) {
            break;
        }
        inst = inst->next;
    }
}

static int calculate_function_stack_size(KROCodegenContext* ctx) {
    int stack_size = KRT_KRO_SHADOW_SPACE_SIZE + ctx->current_stack_offset;
    stack_size = (stack_size + 15) & ~15;
    return stack_size;
}

static void generate_function(KROCodegenContext* ctx, KrtIRFunction* func, KrtIRModule* module) {
    if (!func) {
        return;
    }

    // KrtIrSsaOptimize(func);  //禁用SSA优化,除了Airs_td以外的人不要打开它,SSA不稳定

    bool has_mangled_main = kro_module_has_mangled_main(module);
    int is_entry_point = is_entry_point_function(func->name, has_mangled_main);
    int is_main = (strcmp(func->name, "main") == 0);

    ctx->is_main_func = is_entry_point;
    ctx->local_var_count = 0;
    ctx->current_stack_offset = 0;
    /* Slots are initialized when allocated; unused capacity needs no clearing. */
    memset(ctx->temp_index, 0, sizeof(ctx->temp_index));
    ctx->temp_slot_count = 0;
    ctx->func_index++;
    ctx->current_function_id = ctx->func_index;
    ctx->current_function_name = func->name;
    ctx->current_function = func;
    ctx->tail_site_count = 0;
    ctx->rotated_tail_leaf = NULL;
    ctx->tail_acc_opcode = KRT_IR_NOP;
    ctx->tail_register_argument = false;
    memset(ctx->tail_arg_offsets, 0, sizeof(ctx->tail_arg_offsets));
    ctx->current_block = NULL;
    ctx->current_param_count = func->param_count < KRT_KRO_MAX_ARGS ? func->param_count : KRT_KRO_MAX_ARGS;
    memset(ctx->arg_stack_offsets, 0, sizeof(ctx->arg_stack_offsets));
    memset(ctx->arg_address_taken, 0, sizeof(ctx->arg_address_taken));
    memset(ctx->arg_packed, 0, sizeof(ctx->arg_packed));
    memset(ctx->packed_reg_bits, 0, sizeof(ctx->packed_reg_bits));
    memset(ctx->packed_reg_save_offsets, 0, sizeof(ctx->packed_reg_save_offsets));
    memset(ctx->packed_reg_needs_zero, 0, sizeof(ctx->packed_reg_needs_zero));

    char epilogue_name[64];
    KrtIRBasicBlock epilogue_block = {0};
    epilogue_block.id = -1;
    make_block_symbol_name(ctx, &epilogue_block, epilogue_name, sizeof(epilogue_name));
    ctx->epilogue_symbol = kro_add_symbol(ctx->writer, epilogue_name, KRO_SYM_NOTYPE, KRO_BIND_LOCAL, KRO_SEC_TEXT, 0);

    plan_function_storage(ctx, func);
    int stack_size = calculate_function_stack_size(ctx);

    KrtIRBasicBlock* fast_leaf = emit_leaf_return_guard(ctx, func);
    emit_function_prologue(ctx, stack_size);

    int reg_index = 0, incoming_stack = 16 + KRT_KRO_SHADOW_SPACE_SIZE;
    for (int i = 0; i < ctx->current_param_count; i++) {
        int words = !func->params[i].is_array && KrtTokenIntegerBits(func->params[i].type) > 64 ? 2 : 1;
        if (reg_index + words <= g_arg_reg_count) {
            emit_store_parameter_word(ctx, func, i, 0, g_arg_regs[reg_index++]);
            if (words == 2) {
                emit_store_parameter_word(ctx, func, i, 1, g_arg_regs[reg_index++]);
            }
        } else {
            emit_load_from_stack(ctx, -incoming_stack, KRT_REG_RAX);
            emit_store_parameter_word(ctx, func, i, 0, KRT_REG_RAX);
            if (words == 2) {
                emit_load_from_stack(ctx, -incoming_stack - 8, KRT_REG_RAX);
                emit_store_parameter_word(ctx, func, i, 1, KRT_REG_RAX);
            }
            incoming_stack += words * 8;
        }
    }

    emit_tail_accumulator_init(ctx);
    KrtIRBasicBlock* symbol_block = func->entry_block;
    while (symbol_block) {
        if (fast_leaf && (symbol_block == func->entry_block || symbol_block == fast_leaf)) {
            symbol_block = symbol_block->next;
            continue;
        }
        char block_symbol[64];
        make_block_symbol_name(ctx, symbol_block, block_symbol, sizeof(block_symbol));
        if (kro_find_symbol(ctx->writer, block_symbol) < 0) {
            kro_add_symbol(ctx->writer, block_symbol, KRO_SYM_NOTYPE, KRO_BIND_LOCAL, KRO_SEC_TEXT, 0);
        }
        symbol_block = symbol_block->next;
    }

    if (fast_leaf) {
        KrtIRBasicBlock* nonleaf = func->entry_block->succs[func->entry_block->succs[0] == fast_leaf ? 1 : 0];
        KrtIRBasicBlock* first = func->entry_block->next;
        if (first == fast_leaf) {
            first = first->next;
        }
        if (first != nonleaf) {
            emit_jump_to_block(ctx, nonleaf);
        }
    }
    KrtIRBasicBlock* last_block = NULL;
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        if (fast_leaf && (block == func->entry_block || block == fast_leaf)) {
            continue;
        }
        generate_block(ctx, block);
        last_block = block;
    }
    KrtIRInst* last = last_block ? last_block->first_inst : NULL;
    while (last && last->next && last->opcode != KRT_IR_RETURN && last->opcode != KRT_IR_JUMP &&
           last->opcode != KRT_IR_BRANCH) {
        last = last->next;
    }
    bool falls_through =
        !last || (last->opcode != KRT_IR_RETURN && last->opcode != KRT_IR_JUMP && last->opcode != KRT_IR_BRANCH);
    if (falls_through) {
        bool called_entry = false;
        if (is_main && has_mangled_main && !is_entry_point) {
            emit_call_local(ctx, "_KrtMainEntry");
            called_entry = true;
        }
        if (!called_entry) {
            emit_load_imm64(ctx, 0);
        }
        emit_tail_accumulate(ctx, false, KRT_REG_RAX);
    }
    kro_update_symbol_value(ctx->writer, ctx->epilogue_symbol, kro_get_code_offset(ctx->writer));
    emit_function_epilogue(ctx);
}

static void generate_data_section(KROCodegenContext* ctx, KrtIRModule* module) {
    if (!module || !ctx || !ctx->writer) {
        return;
    }

    if (module->string_const_count > 0 && module->string_constants) {
        ctx->string_const_sym_indices = (int32_t*)KRT_MALLOC(module->string_const_count * sizeof(int32_t));
        if (!ctx->string_const_sym_indices) {
            return;
        }
        ctx->string_const_count = module->string_const_count;

        for (int i = 0; i < module->string_const_count; i++) {
            const char* str = module->string_constants[i];
            if (!str) {
                continue;
            }
            uint32_t rodata_offset = kro_get_rodata_offset(ctx->writer);

            char escaped[4096];
            uint32_t escaped_len = 0;
            if (str) {
                size_t len = strlen(str);
                for (size_t s = 0; s < len && escaped_len < sizeof(escaped) - 1; s++) {
                    if (str[s] == '\\' && s + 1 < len) {
                        s++;
                        switch (str[s]) {
                        case 'n':
                            escaped[escaped_len++] = '\n';
                            break;
                        case 'r':
                            escaped[escaped_len++] = '\r';
                            break;
                        case 't':
                            escaped[escaped_len++] = '\t';
                            break;
                        case '\\':
                            escaped[escaped_len++] = '\\';
                            break;
                        case '"':
                            escaped[escaped_len++] = '"';
                            break;
                        case '0':
                            escaped[escaped_len++] = '\0';
                            break;
                        default:
                            escaped[escaped_len++] = '\\';
                            if (escaped_len < sizeof(escaped) - 1) {
                                escaped[escaped_len++] = str[s];
                            }
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
            ctx->string_const_sym_indices[i] =
                kro_add_symbol(ctx->writer, sym_name, KRO_SYM_OBJECT, KRO_BIND_LOCAL, KRO_SEC_RODATA, rodata_offset);
        }
    }

    for (int i = 0; i < module->global_count; i++) {
        KrtIRGlobal* global = &module->globals[i];
        if (!global || !global->name) {
            continue;
        }

        uint32_t data_offset = kro_get_data_offset(ctx->writer);

        KrtUInt128 init_value = global->is_integer_initializer
                                    ? global->init_integer
                                    : (global->has_initializer ? (KrtUInt128)(int64_t)global->init_number : 0);
        init_value =
            KrtIntegerNormalize(init_value, KrtTokenIntegerBits(global->type), KrtTokenIsUnsigned(global->type));
        if (global->type == TOKEN_FLOAT32) {
            float value = (float)global->init_number;
            init_value = 0;
            memcpy(&init_value, &value, sizeof(value));
        } else if (global->type == TOKEN_FLOAT64) {
            memcpy(&init_value, &global->init_number, sizeof(double));
        }
        kro_write_data(ctx->writer, &init_value, KrtTokenIntegerBits(global->type) > 64 ? 16 : 8);

        kro_add_symbol(ctx->writer, global->name, KRO_SYM_OBJECT, KRO_BIND_GLOBAL, KRO_SEC_DATA, data_offset);
    }
}

void KrtKrtGenerate(FILE* output_file, const char* output_filename, KrtIRModule* module) {
    if (!output_file || !module) {
        return;
    }

    KROCodegenContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.optimization_level = module->optimization_level;
    ctx.output_file = output_file;
    if (output_filename) {
        size_t len = strlen(output_filename);
        if (len >= sizeof(ctx.output_filename)) {
            len = sizeof(ctx.output_filename) - 1;
        }
        memcpy(ctx.output_filename, output_filename, len);
        ctx.output_filename[len] = '\0';
    } else {
        memcpy(ctx.output_filename, "output.kro", 11);
    }
    ctx.writer = kro_writer_create();

    if (!ctx.writer) {
        return;
    }

    generate_data_section(&ctx, module);

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
        while (kro_get_code_offset(ctx.writer) & 15) {
            emit_byte(&ctx, 0x90);
        }
        uint32_t actual_offset = kro_get_code_offset(ctx.writer);

        if (sym_indices[func_idx] >= 0) {
            kro_update_symbol_value(ctx.writer, sym_indices[func_idx], actual_offset);
        }

        if (is_entry_point_function(func->name, has_mangled_main)) {
            kro_set_entry_point(ctx.writer, actual_offset);
        }

        generate_function(&ctx, func, module);
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

#ifdef _WIN32
            emit_bytes(&ctx, (const uint8_t*)"\x48\x83\xEC\x28", 4);
#endif
            uint8_t call_op = 0xE8;
            emit_byte(&ctx, call_op);
            uint32_t reloc_offset = kro_get_code_offset(ctx.writer);
            emit_u32(&ctx, 0);
            kro_add_reloc(ctx.writer, KRO_SEC_TEXT, reloc_offset, main_sym, KRO_RELOC_PC32, 0);

#ifdef _WIN32
            emit_move_reg_to_reg(&ctx, KRT_REG_RAX, KRT_REG_RCX);
            emit_call_external(&ctx, "ExitProcess");
#else
            /* Only the process entry point exits; main returns like any function. */
            emit_bytes(&ctx, (const uint8_t*)"\x48\x89\xc7", 3);

            /* mov rax, 60 (sys_exit) */
            emit_bytes(&ctx, (const uint8_t*)"\x48\xc7\xc0\x3c\x00\x00\x00", 7);

            /* syscall */
            emit_bytes(&ctx, (const uint8_t*)"\x0f\x05", 2);
#endif
        }
    }

    if (!kro_write_file(ctx.writer, ctx.output_filename)) {
    }

    if (ctx.string_const_sym_indices) {
        KRT_FREE(ctx.string_const_sym_indices);
    }
    kro_writer_destroy(ctx.writer);
}
