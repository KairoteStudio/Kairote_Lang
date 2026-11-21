#include "Arm64Codegen.h"
#include "../../Frontend/Lexer/Tokenizer.h"
#include <stdlib.h>
#include <string.h>

/*
 * AArch64 (little endian) native backend, stage 2.
 *
 * Values live in 8-byte stack slots addressed off sp and are shuttled through
 * x0/x1, so no register allocator is needed yet.  Control flow is emitted with
 * a fixup list: block starts are recorded as they are laid out and jumps are
 * patched once every block has an offset.
 */

#define ARM64_MAX_LOCALS 1024
#define ARM64_MAX_TEMPS 8192
#define ARM64_MAX_BLOCKS 4096
#define ARM64_MAX_FIXUPS 8192
#define ARM64_MAX_FUNCS 4096
#define ARM64_SLOT_SIZE 8
#define ARM64_NUM_ARG_REGS 8
#define ARM64_NOP 0xD503201Fu

/* condition codes */
#define COND_EQ 0
#define COND_NE 1
#define COND_HS 2
#define COND_LO 3
#define COND_MI 4
#define COND_HI 8
#define COND_LS 9
#define COND_GE 10
#define COND_LT 11
#define COND_GT 12
#define COND_LE 13

typedef struct {
    char* name;
    int slot;
} Arm64Local;

typedef struct {
    uint32_t code_offset;
    uint32_t kind; /* 0 b, 1 b.cond, 2 cbz, 3 cbnz, 4 bl */
    uint32_t cond;
    int block_id;
    const char* func_name;
} Arm64Fixup;

typedef struct {
    KROWriter* writer;
    char output_filename[256];
    KrtIRModule* module;
    KrtIRFunction* current_function;
    KrtIRBasicBlock* current_block;
    int optimization_level;
    int slot_count;
    int frame_slots;
    Arm64Local locals[ARM64_MAX_LOCALS];
    int local_count;
    int temp_slot_of[ARM64_MAX_TEMPS];
    int temp_count;
    int stackalloc_of[ARM64_MAX_TEMPS];
    uint32_t block_offset[ARM64_MAX_BLOCKS];
    Arm64Fixup fixups[ARM64_MAX_FIXUPS];
    int fixup_count;
    struct {
        const char* name;
        uint32_t offset;
    } funcs[ARM64_MAX_FUNCS];
    int func_count;
    uint32_t entry_func_offset;
    bool has_entry_func;
    int32_t* string_const_sym_indices;
    int string_const_count;
} Arm64Context;

/* ------------------------------------------------------------------ */
/* AArch64 encoders                                                    */
/* ------------------------------------------------------------------ */

static void emit_word(Arm64Context* ctx, uint32_t word) {
    if (ctx && ctx->writer) {
        kro_write_code(ctx->writer, &word, 4);
    }
}

static uint32_t encode_movz(int rd, uint16_t imm, int shift, bool is_64) {
    uint32_t op = is_64 ? 0xD2800000u : 0x52800000u;
    return op | ((uint32_t)(shift & 0x3) << 21) | ((uint32_t)imm << 5) | (uint32_t)(rd & 0x1F);
}

static uint32_t encode_movk(int rd, uint16_t imm, int shift, bool is_64) {
    uint32_t op = is_64 ? 0xF2800000u : 0x72800000u;
    return op | ((uint32_t)(shift & 0x3) << 21) | ((uint32_t)imm << 5) | (uint32_t)(rd & 0x1F);
}

static void emit_load_imm(Arm64Context* ctx, int reg, uint64_t value, bool is_64) {
    int chunks = is_64 ? 4 : 2;
    uint64_t masked = is_64 ? value : (value & 0xFFFFFFFFu);
    bool emitted = false;
    for (int i = 0; i < chunks; i++) {
        uint16_t half = (uint16_t)((masked >> (i * 16)) & 0xFFFFu);
        if (!emitted || half != 0) {
            emit_word(ctx, emitted ? encode_movk(reg, half, i, is_64) : encode_movz(reg, half, i, is_64));
            emitted = true;
        }
    }
    if (!emitted) {
        emit_word(ctx, encode_movz(reg, 0, 0, is_64));
    }
}

/* Slot addressing is sp-relative; see the audit list (A64-1) for why dynamic
 * stackalloc still needs a switch to x29. */
#define ARM64_SLOT_BASE 31u

static void emit_load_from_slot(Arm64Context* ctx, int reg, int slot, bool is_64) {
    uint32_t op = is_64 ? 0xF9400000u : 0xB9400000u;
    uint32_t scale = is_64 ? 8u : 4u;
    uint32_t imm = (uint32_t)((slot * ARM64_SLOT_SIZE) / scale) & 0xFFFu;
    emit_word(ctx, op | (imm << 10) | (ARM64_SLOT_BASE << 5) | (uint32_t)(reg & 0x1F));
}

static void emit_store_to_slot(Arm64Context* ctx, int reg, int slot, bool is_64) {
    uint32_t op = is_64 ? 0xF9000000u : 0xB9000000u;
    uint32_t scale = is_64 ? 8u : 4u;
    uint32_t imm = (uint32_t)((slot * ARM64_SLOT_SIZE) / scale) & 0xFFFu;
    emit_word(ctx, op | (imm << 10) | (ARM64_SLOT_BASE << 5) | (uint32_t)(reg & 0x1F));
}

static void emit_binary_reg(Arm64Context* ctx, uint32_t base, int rd, int rn, int rm) {
    emit_word(ctx, base | ((uint32_t)(rm & 0x1F) << 16) | ((uint32_t)(rn & 0x1F) << 5) | (uint32_t)(rd & 0x1F));
}

static void emit_sp_adjust(Arm64Context* ctx, uint32_t delta, bool subtract) {
    while (delta > 0) {
        uint32_t step = delta > 4095 ? 4095u : delta;
        uint32_t base = subtract ? 0xD1000000u : 0x91000000u;
        emit_word(ctx, base | (step << 10) | (31u << 5) | 31u);
        delta -= step;
    }
}

static void emit_extend(Arm64Context* ctx, int bits, bool is_unsigned, bool is_64, int src, int dst) {
    uint32_t base;
    if (bits <= 8) {
        base = is_unsigned ? 0x53001C00u : 0x13001C00u; /* uxtb / sxtb */
    } else if (bits <= 16) {
        base = is_unsigned ? 0x53003C00u : 0x13003C00u; /* uxth / sxth */
    } else if (bits <= 32) {
        if (is_unsigned || !is_64) {
            return; /* writing Wd already zero-extends */
        }
        base = 0x93407C00u; /* sxtw */
    } else {
        return;
    }
    emit_word(ctx, base | ((uint32_t)(src & 0x1F) << 5) | (uint32_t)(dst & 0x1F));
}

/*
 * The language's runtime and the libraries embed Linux/x86-64 syscall numbers
 * (the frontend emits them straight into the IR).  AArch64 uses a different
 * numbering, so translate the ones whose signatures match.  Numbers without an
 * entry are passed through unchanged.
 */
static const uint32_t g_syscall_map[][2] = {
    {0, 63},   /* read */
    {1, 64},   /* write */
    {3, 57},   /* close */
    {5, 80},   /* fstat */
    {8, 62},   /* lseek */
    {9, 222},  /* mmap */
    {10, 226}, /* mprotect */
    {11, 215}, /* munmap */
    {12, 214}, /* brk */
    {16, 29},  /* ioctl */
    {60, 93},  /* exit */
    {231, 94}, /* exit_group */
};

#define SYSCALL_MAP_COUNT (sizeof(g_syscall_map) / sizeof(g_syscall_map[0]))

static uint32_t arm64_syscall_number(uint32_t x86_number) {
    for (size_t i = 0; i < SYSCALL_MAP_COUNT; i++) {
        if (g_syscall_map[i][0] == x86_number) {
            return g_syscall_map[i][1];
        }
    }
    return x86_number;
}

static void emit_load_indirect(Arm64Context* ctx, int address_reg, int size) {
    uint32_t base;
    switch (size) {
    case 1:
        base = 0x39400000u; /* ldrb wt, [xn] */
        break;
    case 2:
        base = 0x79400000u; /* ldrh wt, [xn] */
        break;
    case 4:
        base = 0xB9400000u; /* ldr wt, [xn] */
        break;
    default:
        base = 0xF9400000u; /* ldr xt, [xn] */
        break;
    }
    emit_word(ctx, base | ((uint32_t)(address_reg & 0x1F) << 5));
}

static void emit_store_indirect(Arm64Context* ctx, int address_reg, int value_reg, int size) {
    uint32_t base;
    switch (size) {
    case 1:
        base = 0x39000000u; /* strb wt, [xn] */
        break;
    case 2:
        base = 0x79000000u; /* strh wt, [xn] */
        break;
    case 4:
        base = 0xB9000000u; /* str wt, [xn] */
        break;
    default:
        base = 0xF9000000u; /* str xt, [xn] */
        break;
    }
    emit_word(ctx, base | ((uint32_t)(address_reg & 0x1F) << 5) | (uint32_t)(value_reg & 0x1F));
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

/* ------------------------------------------------------------------ */
/* Frame planning                                                      */
/* ------------------------------------------------------------------ */

static bool value_is_const_int(KrtIRValue* value) {
    return value && (value->type == KRT_IR_VALUE_IMM || value->type == KRT_IR_VALUE_INTEGER);
}

static uint64_t value_const_int(KrtIRValue* value) {
    if (!value) {
        return 0;
    }
    return value->type == KRT_IR_VALUE_INTEGER ? (uint64_t)value->data.integer
                                               : (uint64_t)(int64_t)value->data.imm;
}

/*
 * KrtIrStackAlloc casts its count to uint64, so the operand is normally a TEMP
 * produced by `cast <const>`.  Look through that so constant-sized requests can
 * still be placed statically inside the frame.
 */
static bool resolve_const_count(KrtIRFunction* func, KrtIRValue* value, uint64_t* out) {
    if (!value) {
        return false;
    }
    if (value_is_const_int(value)) {
        *out = value_const_int(value);
        return true;
    }
    if (value->type != KRT_IR_VALUE_TEMP) {
        return false;
    }
    int index = value->data.index;
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->result.type != KRT_IR_VALUE_TEMP || inst->result.data.index != index) {
                continue;
            }
            if ((inst->opcode == KRT_IR_CAST || inst->opcode == KRT_IR_IMM || inst->opcode == KRT_IR_COPY) &&
                inst->operand_count >= 1 && value_is_const_int(&inst->operands[0])) {
                *out = value_const_int(&inst->operands[0]);
                return true;
            }
            return false;
        }
    }
    return false;
}

static int alloc_slot(Arm64Context* ctx) {
    return ctx->slot_count++;
}

/* Reserve a byte range in the frame; returns its first slot index. */
static int alloc_slot_for_bytes(Arm64Context* ctx, uint32_t bytes) {
    int base = ctx->slot_count;
    ctx->slot_count += (bytes + ARM64_SLOT_SIZE - 1) / ARM64_SLOT_SIZE;
    return base;
}

static int local_slot(Arm64Context* ctx, const char* name) {
    if (!name) {
        return -1;
    }
    /* Module-level objects are addressed through their symbol, not a frame
     * slot; otherwise every function would get a private copy. */
    if (kro_find_symbol(ctx->writer, name) >= 0) {
        return -1;
    }
    for (int i = 0; i < ctx->local_count; i++) {
        if (ctx->locals[i].name && strcmp(ctx->locals[i].name, name) == 0) {
            return ctx->locals[i].slot;
        }
    }
    if (ctx->local_count >= ARM64_MAX_LOCALS) {
        return -1;
    }
    Arm64Local* local = &ctx->locals[ctx->local_count++];
    local->name = (char*)name;
    local->slot = alloc_slot(ctx);
    return local->slot;
}

static int temp_slot(Arm64Context* ctx, int index) {
    if (index < 0 || index >= ARM64_MAX_TEMPS) {
        return -1;
    }
    if (ctx->temp_slot_of[index] < 0) {
        ctx->temp_slot_of[index] = alloc_slot(ctx);
        ctx->temp_count++;
    }
    return ctx->temp_slot_of[index];
}

static int value_slot(Arm64Context* ctx, KrtIRValue* value) {
    if (!value) {
        return -1;
    }
    switch (value->type) {
    case KRT_IR_VALUE_VAR:
        return local_slot(ctx, value->data.name);
    case KRT_IR_VALUE_TEMP:
        return temp_slot(ctx, value->data.index);
    case KRT_IR_VALUE_ARG:
        return value->data.index;
    default:
        return -1;
    }
}

static void plan_frame(Arm64Context* ctx, KrtIRFunction* func) {
    ctx->slot_count = 0;
    ctx->local_count = 0;
    ctx->temp_count = 0;
    for (int i = 0; i < ARM64_MAX_TEMPS; i++) {
        ctx->temp_slot_of[i] = -1;
        ctx->stackalloc_of[i] = -1;
    }
    for (int i = 0; i < func->param_count; i++) {
        alloc_slot(ctx);
    }
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            for (int i = 0; i < inst->operand_count; i++) {
                value_slot(ctx, &inst->operands[i]);
            }
            value_slot(ctx, &inst->result);
            if (inst->opcode == KRT_IR_STACKALLOC && inst->operand_count >= 2 &&
                value_is_const_int(&inst->operands[1])) {
                uint64_t count = 0;
                uint64_t element = value_const_int(&inst->operands[1]);
                if (resolve_const_count(func, &inst->operands[0], &count)) {
                    uint64_t bytes = count * element;
                    if (bytes > 0 && bytes <= (uint64_t)(ARM64_MAX_TEMPS - ctx->slot_count) * ARM64_SLOT_SIZE) {
                        if (inst->result.type == KRT_IR_VALUE_TEMP) {
                            ctx->stackalloc_of[inst->result.data.index] = alloc_slot_for_bytes(ctx, (uint32_t)bytes);
                        }
                    }
                }
            } else if (inst->opcode == KRT_IR_LOADPTR && inst->operand_count >= 3 &&
                       instruction_value_size(inst, 2, 8) == 16 && inst->result.type == KRT_IR_VALUE_TEMP) {
                /* A 128-bit value occupies two frame slots. */
                temp_slot(ctx, inst->result.data.index);
                alloc_slot(ctx);
            }
        }
    }
    ctx->frame_slots = ctx->slot_count;
}

static int frame_size(Arm64Context* ctx) {
    int bytes = ctx->frame_slots * ARM64_SLOT_SIZE;
    return (bytes + 15) & ~15;
}

/* ------------------------------------------------------------------ */
/* Block labels and fixups                                             */
/* ------------------------------------------------------------------ */

static void record_block_start(Arm64Context* ctx, KrtIRBasicBlock* block) {
    if (!block || block->id < 0 || block->id >= ARM64_MAX_BLOCKS) {
        return;
    }
    ctx->block_offset[block->id] = kro_get_code_offset(ctx->writer);
}

static void add_fixup(Arm64Context* ctx, uint32_t kind, uint32_t cond, int block_id, const char* func_name) {
    if (ctx->fixup_count >= ARM64_MAX_FIXUPS) {
        return;
    }
    Arm64Fixup* fix = &ctx->fixups[ctx->fixup_count++];
    fix->code_offset = kro_get_code_offset(ctx->writer);
    fix->kind = kind;
    fix->cond = cond;
    fix->block_id = block_id;
    fix->func_name = func_name;
}

static uint32_t find_func_offset(Arm64Context* ctx, const char* name) {
    for (int i = 0; i < ctx->func_count; i++) {
        if (ctx->funcs[i].name && strcmp(ctx->funcs[i].name, name) == 0) {
            return ctx->funcs[i].offset;
        }
    }
    return 0xFFFFFFFFu;
}

static void patch_fixups(Arm64Context* ctx) {
    for (int i = 0; i < ctx->fixup_count; i++) {
        Arm64Fixup* fix = &ctx->fixups[i];
        uint32_t target;
        if (fix->func_name) {
            target = find_func_offset(ctx, fix->func_name);
        } else if (fix->block_id >= 0 && fix->block_id < ARM64_MAX_BLOCKS) {
            target = ctx->block_offset[fix->block_id];
        } else {
            continue;
        }
        int64_t distance = (int64_t)target - (int64_t)fix->code_offset;
        uint32_t word;
        switch (fix->kind) {
        case 0: /* b */
            word = 0x14000000u | ((uint32_t)((distance >> 2) & 0x03FFFFFF));
            break;
        case 1: /* b.cond */
            word = 0x54000000u | ((uint32_t)((distance >> 2) & 0x7FFFF) << 5) | (fix->cond & 0xF);
            break;
        case 2: /* cbz */
            word = 0x34000000u | ((uint32_t)((distance >> 2) & 0x7FFFF) << 5);
            break;
        case 3: /* cbnz */
            word = 0x35000000u | ((uint32_t)((distance >> 2) & 0x7FFFF) << 5);
            break;
        default: /* bl */
            word = 0x94000000u | ((uint32_t)((distance >> 2) & 0x03FFFFFF));
            break;
        }
        kro_patch_code(ctx->writer, fix->code_offset, &word, 4);
    }
}

/* ------------------------------------------------------------------ */
/* Value access                                                        */
/* ------------------------------------------------------------------ */

static bool value_is_64(KrtIRValue* value) {
    if (!value) {
        return true;
    }
    if (value->value_type == TOKEN_FLOAT32) {
        return false;
    }
    if (value->value_type == TOKEN_FLOAT64) {
        return true;
    }
    int bits = KrtTokenIntegerBits(value->value_type);
    return bits == 0 || bits > 32;
}

static void emit_load_value(Arm64Context* ctx, KrtIRValue* value, int reg);

static bool value_is_float64(KrtIRValue* value) {
    return value && value->value_type == TOKEN_FLOAT64;
}

static void emit_gp_to_fp(Arm64Context* ctx, int gp, int fp, bool is64) {
    emit_word(ctx, (is64 ? 0x9E670000u : 0x1E270000u) | ((uint32_t)(gp & 0x1F) << 5) | (uint32_t)(fp & 0x1F));
}

static void emit_fp_to_gp(Arm64Context* ctx, int fp, int gp, bool is64) {
    emit_word(ctx, (is64 ? 0x9E660000u : 0x1E260000u) | ((uint32_t)(fp & 0x1F) << 5) | (uint32_t)(gp & 0x1F));
}

static void emit_load_float(Arm64Context* ctx, KrtIRValue* value, int fp_reg) {
    bool is64 = value_is_float64(value);
    int slot = value_slot(ctx, value);
    if (slot >= 0) {
        emit_load_from_slot(ctx, 0, slot, is64);
    } else if (value->type == KRT_IR_VALUE_IMM_F) {
        if (is64) {
            uint64_t bits = 0;
            memcpy(&bits, &value->data.imm, sizeof(bits));
            emit_load_imm(ctx, 0, bits, true);
        } else {
            float narrowed = (float)value->data.imm;
            uint32_t bits = 0;
            memcpy(&bits, &narrowed, sizeof(bits));
            emit_load_imm(ctx, 0, bits, false);
        }
    } else {
        emit_load_value(ctx, value, 0);
    }
    emit_gp_to_fp(ctx, 0, fp_reg, is64);
}

static void emit_store_float_result(Arm64Context* ctx, KrtIRInst* inst, int fp_reg) {
    bool is64 = value_is_float64(&inst->result);
    emit_fp_to_gp(ctx, fp_reg, 0, is64);
    int slot = value_slot(ctx, &inst->result);
    if (slot >= 0) {
        emit_store_to_slot(ctx, 0, slot, is64);
    }
}

static void emit_symbol_address(Arm64Context* ctx, const char* name, int reg);

static void emit_load_value(Arm64Context* ctx, KrtIRValue* value, int reg) {
    if (!value) {
        return;
    }
    bool is_64 = value_is_64(value);
    switch (value->type) {
    case KRT_IR_VALUE_VOID:
        emit_load_imm(ctx, reg, 0, is_64);
        break;
    case KRT_IR_VALUE_INTEGER:
        emit_load_imm(ctx, reg, (uint64_t)value->data.integer, is_64);
        break;
    case KRT_IR_VALUE_IMM:
        emit_load_imm(ctx, reg, (uint64_t)(int64_t)value->data.imm, is_64);
        break;
    case KRT_IR_VALUE_IMM_F: {
        uint64_t bits = 0;
        memcpy(&bits, &value->data.imm, sizeof(bits));
        emit_load_imm(ctx, reg, bits, is_64);
        break;
    }
    case KRT_IR_VALUE_VAR: {
        if (value->data.name && kro_find_symbol(ctx->writer, value->data.name) >= 0) {
            emit_symbol_address(ctx, value->data.name, reg);
            emit_word(ctx, 0xF9400000u | ((uint32_t)(reg & 0x1F) << 5) | (uint32_t)(reg & 0x1F));
            break;
        }
        int slot = local_slot(ctx, value->data.name);
        if (slot >= 0) {
            emit_load_from_slot(ctx, reg, slot, is_64);
        }
        break;
    }
    case KRT_IR_VALUE_TEMP: {
        int slot = temp_slot(ctx, value->data.index);
        if (slot >= 0) {
            emit_load_from_slot(ctx, reg, slot, is_64);
        }
        break;
    }
    case KRT_IR_VALUE_ARG: {
        int slot = value->data.index;
        if (slot >= 0) {
            emit_load_from_slot(ctx, reg, slot, is_64);
        }
        break;
    }
    case KRT_IR_VALUE_STRING_CONST: {
        int id = value->data.string_const_id;
        if (id >= 0 && id < ctx->string_const_count && ctx->string_const_sym_indices &&
            ctx->string_const_sym_indices[id] >= 0) {
            char name[64];
            snprintf(name, sizeof(name), "str_const_%d", id);
            emit_symbol_address(ctx, name, reg);
        } else {
            emit_load_imm(ctx, reg, 0, true);
        }
        break;
    }
    default:
        emit_load_imm(ctx, reg, 0, is_64);
        break;
    }
}

static void emit_store_result(Arm64Context* ctx, KrtIRInst* inst, int reg) {
    if (inst->result.type == KRT_IR_VALUE_VAR && inst->result.data.name &&
        kro_find_symbol(ctx->writer, inst->result.data.name) >= 0) {
        int scratch = reg == 9 ? 10 : 9;
        emit_symbol_address(ctx, inst->result.data.name, scratch);
        emit_word(ctx, 0xF9000000u | ((uint32_t)(scratch & 0x1F) << 5) | (uint32_t)(reg & 0x1F));
        return;
    }

    int slot = value_slot(ctx, &inst->result);
    if (slot >= 0) {
        emit_store_to_slot(ctx, reg, slot, value_is_64(&inst->result));
    }
}

/* Load the absolute address of a symbol: ADRP (page) + ADD (low 12 bits). */
static void emit_symbol_address(Arm64Context* ctx, const char* name, int reg) {
    int sym = name ? kro_find_symbol(ctx->writer, name) : -1;
    if (sym < 0) {
        emit_load_imm(ctx, reg, 0, true);
        return;
    }
    uint32_t adrp_offset = kro_get_code_offset(ctx->writer);
    emit_word(ctx, 0x90000000u | (uint32_t)(reg & 0x1F));
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, adrp_offset, sym, KRO_RELOC_AARCH64_ADR_PREL_PG_HI21, 0);
    uint32_t add_offset = kro_get_code_offset(ctx->writer);
    emit_word(ctx, 0x91000000u | ((uint32_t)(reg & 0x1F) << 5) | (uint32_t)(reg & 0x1F));
    kro_add_reloc(ctx->writer, KRO_SEC_TEXT, add_offset, sym, KRO_RELOC_AARCH64_ADD_ABS_LO12_NC, 0);
}

/* ------------------------------------------------------------------ */
/* Instruction selection                                               */
/* ------------------------------------------------------------------ */

static void emit_return_sequence(Arm64Context* ctx) {
    int size = frame_size(ctx);
    if (size > 0) {
        emit_sp_adjust(ctx, (uint32_t)size, false);
    }
    emit_word(ctx, 0xA8C17BFDu); /* ldp x29, x30, [sp], #16 */
    emit_word(ctx, 0xD65F03C0u); /* ret */
}

static uint32_t comparison_condition(KrtIROpcode opcode, bool is_unsigned) {
    switch (opcode) {
    case KRT_IR_EQ:
        return COND_EQ;
    case KRT_IR_NE:
        return COND_NE;
    case KRT_IR_LT:
        return is_unsigned ? COND_LO : COND_LT;
    case KRT_IR_GT:
        return is_unsigned ? COND_HI : COND_GT;
    case KRT_IR_LE:
        return is_unsigned ? COND_LS : COND_LE;
    case KRT_IR_GE:
        return is_unsigned ? COND_HS : COND_GE;
    default:
        return COND_EQ;
    }
}

static void emit_comparison(Arm64Context* ctx, KrtIRInst* inst) {
    bool is_unsigned = KrtTokenIsUnsigned(inst->result.value_type);
    uint32_t cond = comparison_condition(inst->opcode, is_unsigned);
    bool is_64 = inst->operand_count >= 1 && value_is_64(&inst->operands[0]);
    if (inst->operand_count >= 2) {
        emit_load_value(ctx, &inst->operands[0], 0);
        emit_load_value(ctx, &inst->operands[1], 1);
        emit_word(ctx, (is_64 ? 0xEB00001Fu : 0x6B00001Fu) | (1u << 16));
    } else {
        emit_load_imm(ctx, 0, 0, true);
    }
    /* cset rd, cond  (the encoding stores cond ^ 1) */
    emit_word(ctx, (is_64 ? 0x9A9F07E0u : 0x1A9F07E0u) | (((cond ^ 1) & 0xF) << 12));
    emit_store_result(ctx, inst, 0);
}

static void emit_call(Arm64Context* ctx, KrtIRInst* inst) {
    if (inst->operand_count < 1 || inst->operands[0].type != KRT_IR_VALUE_FUNCTION) {
        return;
    }
    const char* name = inst->operands[0].data.function_name;
    if (!name) {
        return;
    }
    int args = inst->operand_count - 1;
    if (args > ARM64_NUM_ARG_REGS) {
        KrtError("ARM64 backend: %s takes %d arguments; only %d register arguments are supported", name, args,
                 ARM64_NUM_ARG_REGS);
        return;
    }
    for (int i = 0; i < args; i++) {
        KrtIRValue* arg = &inst->operands[i + 1];
        bool arg_float = arg->value_type == TOKEN_FLOAT32 || arg->value_type == TOKEN_FLOAT64;
        if (arg_float) {
            emit_load_float(ctx, arg, i); /* AAPCS64: v0-v7 */
        } else {
            emit_load_value(ctx, arg, i);
        }
    }
    if (find_func_offset(ctx, name) != 0xFFFFFFFFu) {
        add_fixup(ctx, 4, 0, -1, name);
        emit_word(ctx, 0x94000000u); /* bl (patched) */
    } else {
        /* Defined in another object: leave a CALL26 relocation for the linker. */
        int sym = kro_find_symbol(ctx->writer, name);
        if (sym < 0) {
            sym = kro_add_undefined_symbol(ctx->writer, name);
        }
        uint32_t reloc_offset = kro_get_code_offset(ctx->writer);
        emit_word(ctx, 0x94000000u);
        kro_add_reloc(ctx->writer, KRO_SEC_TEXT, reloc_offset, sym, KRO_RELOC_AARCH64_CALL26, 0);
    }
    emit_store_result(ctx, inst, 0);
}

static void emit_function_body(Arm64Context* ctx, KrtIRFunction* func) {
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
        ctx->current_block = block;
        record_block_start(ctx, block);
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            bool is_64 = value_is_64(&inst->result);
            switch (inst->opcode) {
            case KRT_IR_NOP:
                break;
            case KRT_IR_COPY:
            case KRT_IR_IMM:
                if (inst->operand_count >= 1) {
                    bool src_float = inst->operands[0].value_type == TOKEN_FLOAT32 ||
                                     inst->operands[0].value_type == TOKEN_FLOAT64;
                    bool dst_float = inst->result.value_type == TOKEN_FLOAT32 ||
                                     inst->result.value_type == TOKEN_FLOAT64;
                    if (src_float && dst_float) {
                        bool src64 = inst->operands[0].value_type == TOKEN_FLOAT64;
                        bool dst64 = inst->result.value_type == TOKEN_FLOAT64;
                        emit_load_float(ctx, &inst->operands[0], 0);
                        if (src64 != dst64) {
                            emit_word(ctx, src64 ? 0x1E624000u : 0x1E22C000u); /* fcvt */
                        }
                        emit_store_float_result(ctx, inst, 0);
                        break;
                    }
                    emit_load_value(ctx, &inst->operands[0], 0);
                    emit_store_result(ctx, inst, 0);
                }
                break;
            case KRT_IR_CAST: {
                if (inst->operand_count >= 1) {
                    bool src_float = inst->operands[0].value_type == TOKEN_FLOAT32 ||
                                     inst->operands[0].value_type == TOKEN_FLOAT64;
                    bool dst_float = inst->result.value_type == TOKEN_FLOAT32 ||
                                     inst->result.value_type == TOKEN_FLOAT64;
                    if (src_float && !dst_float) {
                        /* float -> integer truncate */
                        emit_load_float(ctx, &inst->operands[0], 0);
                        emit_word(ctx, (inst->operands[0].value_type == TOKEN_FLOAT64) ? 0x9E780000u
                                                                                        : 0x1E380000u);
                        emit_store_result(ctx, inst, 0);
                        break;
                    }
                    if (!src_float && dst_float) {
                        /* integer -> float */
                        emit_load_value(ctx, &inst->operands[0], 0);
                        emit_word(ctx, (inst->result.value_type == TOKEN_FLOAT64) ? 0x9E620000u
                                                                                   : 0x1E220000u);
                        emit_store_float_result(ctx, inst, 0);
                        break;
                    }
                    emit_load_value(ctx, &inst->operands[0], 0);
                    int bits = KrtTokenIntegerBits(inst->result.value_type);
                    if (bits > 0 && bits < 64) {
                        emit_extend(ctx, bits, KrtTokenIsUnsigned(inst->result.value_type), is_64, 0, 0);
                    }
                    emit_store_result(ctx, inst, 0);
                }
                break;
            }
            case KRT_IR_ADD:
            case KRT_IR_SUB:
            case KRT_IR_MUL:
            case KRT_IR_DIV:
            case KRT_IR_MOD: {
                if (inst->operand_count < 2) {
                    break;
                }
                emit_load_value(ctx, &inst->operands[0], 0);
                emit_load_value(ctx, &inst->operands[1], 1);
                if (inst->opcode == KRT_IR_ADD) {
                    emit_binary_reg(ctx, is_64 ? 0x8B000000u : 0x0B000000u, 0, 0, 1);
                } else if (inst->opcode == KRT_IR_SUB) {
                    emit_binary_reg(ctx, is_64 ? 0xCB000000u : 0x4B000000u, 0, 0, 1);
                } else if (inst->opcode == KRT_IR_MUL) {
                    emit_binary_reg(ctx, is_64 ? 0x9B007C00u : 0x1B007C00u, 0, 0, 1);
                } else {
                    uint32_t div = KrtTokenIsUnsigned(inst->result.value_type)
                                       ? (is_64 ? 0x9AC00800u : 0x1AC00800u)
                                       : (is_64 ? 0x9AC00C00u : 0x1AC00C00u);
                    emit_binary_reg(ctx, div, 2, 0, 1);
                    if (inst->opcode == KRT_IR_MOD) {
                        emit_binary_reg(ctx, is_64 ? 0x9B007C00u : 0x1B007C00u, 2, 1, 2);
                        emit_binary_reg(ctx, is_64 ? 0xCB000000u : 0x4B000000u, 0, 0, 2);
                    } else {
                        emit_word(ctx, 0xAA0003E0u | ((uint32_t)2 << 16)); /* mov x0, x2 */
                    }
                }
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_AND:
            case KRT_IR_OR:
            case KRT_IR_XOR: {
                if (inst->operand_count < 2) {
                    break;
                }
                emit_load_value(ctx, &inst->operands[0], 0);
                emit_load_value(ctx, &inst->operands[1], 1);
                uint32_t base;
                if (inst->opcode == KRT_IR_AND) {
                    base = is_64 ? 0x8A000000u : 0x0A000000u;
                } else if (inst->opcode == KRT_IR_OR) {
                    base = is_64 ? 0xAA000000u : 0x2A000000u;
                } else {
                    base = is_64 ? 0xCA000000u : 0x4A000000u;
                }
                emit_binary_reg(ctx, base, 0, 0, 1);
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_LSHIFT:
            case KRT_IR_RSHIFT: {
                if (inst->operand_count < 2) {
                    break;
                }
                emit_load_value(ctx, &inst->operands[0], 0);
                emit_load_value(ctx, &inst->operands[1], 1);
                bool arithmetic = inst->opcode == KRT_IR_RSHIFT && !KrtTokenIsUnsigned(inst->result.value_type);
                uint32_t base;
                if (inst->opcode == KRT_IR_LSHIFT) {
                    base = is_64 ? 0x9AC02000u : 0x1AC02000u;
                } else {
                    base = arithmetic ? (is_64 ? 0x9AC02800u : 0x1AC02800u) : (is_64 ? 0x9AC02400u : 0x1AC02400u);
                }
                emit_binary_reg(ctx, base, 0, 0, 1);
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_LT:
            case KRT_IR_GT:
            case KRT_IR_EQ:
            case KRT_IR_LE:
            case KRT_IR_GE:
            case KRT_IR_NE:
                emit_comparison(ctx, inst);
                break;
            case KRT_IR_FADD:
            case KRT_IR_FSUB:
            case KRT_IR_FMUL:
            case KRT_IR_FDIV: {
                if (inst->operand_count < 2) {
                    break;
                }
                bool is64 = value_is_float64(&inst->operands[0]) || value_is_float64(&inst->operands[1]);
                emit_load_float(ctx, &inst->operands[0], 0);
                if (value_is_float64(&inst->operands[0]) != is64) {
                    emit_word(ctx, (is64 ? 0x1E22C000u : 0x1E624000u) | (0u << 5) | 0u); /* fcvt s0/d0 */
                }
                emit_load_float(ctx, &inst->operands[1], 1);
                if (value_is_float64(&inst->operands[1]) != is64) {
                    emit_word(ctx, (is64 ? 0x1E22C000u : 0x1E624000u) | (1u << 5) | 1u); /* fcvt s1/d1 */
                }
                uint32_t base;
                if (inst->opcode == KRT_IR_FADD) {
                    base = is64 ? 0x1E602800u : 0x1E202800u;
                } else if (inst->opcode == KRT_IR_FSUB) {
                    base = is64 ? 0x1E603800u : 0x1E203800u;
                } else if (inst->opcode == KRT_IR_FMUL) {
                    base = is64 ? 0x1E600800u : 0x1E200800u;
                } else {
                    base = is64 ? 0x1E601800u : 0x1E201800u;
                }
                emit_word(ctx, base | (1u << 16)); /* sd0 = sd0 <op> sd1 */
                if (is64 != value_is_float64(&inst->result)) {
                    /* convert the operation precision to the result precision */
                    emit_word(ctx, is64 ? 0x1E624000u : 0x1E22C000u); /* fcvt */
                }
                emit_store_float_result(ctx, inst, 0);
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
                bool is64 = value_is_float64(&inst->operands[0]);
                emit_load_float(ctx, &inst->operands[0], 0);
                emit_load_float(ctx, &inst->operands[1], 1);
                emit_word(ctx, (is64 ? 0x1E602000u : 0x1E202000u) | (1u << 16)); /* fcmp sd0, sd1 */
                uint32_t cond;
                switch (inst->opcode) {
                case KRT_IR_FEQ:
                    cond = COND_EQ;
                    break;
                case KRT_IR_FNE:
                    cond = COND_NE;
                    break;
                case KRT_IR_FLT:
                    cond = COND_MI;
                    break;
                case KRT_IR_FLE:
                    cond = COND_LS;
                    break;
                case KRT_IR_FGT:
                    cond = COND_GT;
                    break;
                default:
                    cond = COND_GE;
                    break;
                }
                emit_word(ctx, 0x1A9F07E0u | (((cond ^ 1) & 0xF) << 12)); /* cset w0, cond */
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_JUMP: {
                if (block->succ_count > 0 && block->succs[0] != block->next) {
                    add_fixup(ctx, 0, 0, block->succs[0]->id, NULL);
                    emit_word(ctx, 0x14000000u); /* b */
                }
                break;
            }
            case KRT_IR_BRANCH: {
                if (inst->operand_count < 1 || block->succ_count < 2) {
                    break;
                }
                emit_load_value(ctx, &inst->operands[0], 0);
                if (block->succs[0] == block->next) {
                    add_fixup(ctx, 2, 0, block->succs[1]->id, NULL);
                    emit_word(ctx, 0x34000000u); /* cbz w0 */
                } else {
                    add_fixup(ctx, 3, 0, block->succs[0]->id, NULL);
                    emit_word(ctx, 0x35000000u); /* cbnz w0 */
                    if (block->succs[1] != block->next) {
                        add_fixup(ctx, 0, 0, block->succs[1]->id, NULL);
                        emit_word(ctx, 0x14000000u); /* b */
                    }
                }
                break;
            }
            case KRT_IR_CALL:
            case KRT_IR_CALL_INDIRECT:
                emit_call(ctx, inst);
                break;
            case KRT_IR_STACKALLOC: {
                int base = -1;
                if (inst->result.type == KRT_IR_VALUE_TEMP) {
                    base = ctx->stackalloc_of[inst->result.data.index];
                }
                if (base < 0) {
                    KrtError("ARM64 backend: dynamic stackalloc is not supported");
                    break;
                }
                uint32_t offset = (uint32_t)base * ARM64_SLOT_SIZE;
                if (offset <= 4095) {
                    emit_word(ctx, 0x91000000u | (offset << 10) | (31u << 5)); /* add x0, sp, #offset */
                } else {
                    emit_load_imm(ctx, 0, offset, true);
                    emit_word(ctx, 0x8B000000u | (31u << 5)); /* add x0, sp, x0 */
                }
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_RETURN:
                if (inst->operand_count >= 1) {
                    emit_load_value(ctx, &inst->operands[0], 0);
                } else {
                    emit_load_imm(ctx, 0, 0, true);
                }
                emit_return_sequence(ctx);
                break;
            case KRT_IR_ADDRESS_OF: {
                if (inst->operand_count < 1) {
                    break;
                }
                int slot = -1;
                if (inst->operands[0].type == KRT_IR_VALUE_VAR) {
                    slot = local_slot(ctx, inst->operands[0].data.name);
                } else if (inst->operands[0].type == KRT_IR_VALUE_ARG) {
                    slot = inst->operands[0].data.index;
                }
                if (slot < 0) {
                    break;
                }
                uint32_t offset = (uint32_t)slot * ARM64_SLOT_SIZE;
                if (offset <= 4095) {
                    emit_word(ctx, 0x91000000u | (offset << 10) | (31u << 5)); /* add x0, sp, #offset */
                } else {
                    emit_load_imm(ctx, 0, offset, true);
                    emit_word(ctx, 0x8B000000u | (31u << 5) | 0u); /* add x0, sp, x0 */
                }
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_LOADPTR: {
                if (inst->operand_count < 1) {
                    break;
                }
                emit_load_value(ctx, &inst->operands[0], 0);
                if (inst->operand_count >= 2) {
                    emit_load_value(ctx, &inst->operands[1], 1);
                    emit_word(ctx, 0x8B010000u); /* add x0, x0, x1 */
                }
                int size = instruction_value_size(inst, 2, 8);
                if (size == 16) {
                    /* Split a 128-bit indirect load into two 64-bit halves. */
                    emit_word(ctx, 0xF9400000u | ((uint32_t)(0 & 0x1F) << 5)); /* ldr x0, [x0] */
                    emit_word(ctx, 0xF9400000u | (1u << 10) | (0u << 5) | 1u);  /* ldr x1, [x0, #8] */
                    int slot = value_slot(ctx, &inst->result);
                    if (slot >= 0) {
                        emit_store_to_slot(ctx, 0, slot, true);
                        emit_store_to_slot(ctx, 1, slot + 1, true);
                    }
                    break;
                }
                emit_load_indirect(ctx, 0, size);
                emit_store_result(ctx, inst, 0);
                break;
            }
            case KRT_IR_STOREPTR: {
                if (inst->operand_count < 3) {
                    break;
                }
                emit_load_value(ctx, &inst->operands[0], 0);
                emit_load_value(ctx, &inst->operands[1], 1);
                emit_word(ctx, 0x8B010000u); /* add x0, x0, x1 */
                emit_load_value(ctx, &inst->operands[2], 2);
                int size = instruction_value_size(inst, 3, 8);
                if (size == 16) {
                    KrtError("ARM64 backend: 128-bit indirect stores are not supported");
                    break;
                }
                emit_store_indirect(ctx, 0, 2, size);
                break;
            }
            case KRT_IR_ARRAY_STORE: {
                if (inst->operand_count < 3) {
                    break;
                }
                int size = instruction_value_size(inst, 3, 8);
                emit_load_value(ctx, &inst->operands[0], 0);
                emit_load_value(ctx, &inst->operands[1], 1);
                if (size > 1 && size <= 8) {
                    emit_load_imm(ctx, 2, (uint32_t)size, true);
                    emit_binary_reg(ctx, 0x9B007C00u, 1, 2, 1); /* x1 = x1 * size */
                }
                emit_word(ctx, 0x8B010000u); /* add x0, x0, x1 */
                emit_load_value(ctx, &inst->operands[2], 2);
                emit_store_indirect(ctx, 0, 2, size == 16 ? 8 : size);
                break;
            }
            case KRT_IR_SYSCALL: {
                if (inst->operand_count < 1) {
                    break;
                }
                uint32_t number = 0;
                uint64_t resolved = 0;
                bool has_number = false;
                if (inst->operands[0].type == KRT_IR_VALUE_IMM) {
                    number = arm64_syscall_number((uint32_t)(int64_t)inst->operands[0].data.imm);
                    has_number = true;
                } else if (inst->operands[0].type == KRT_IR_VALUE_INTEGER) {
                    number = arm64_syscall_number((uint32_t)inst->operands[0].data.integer);
                    has_number = true;
                } else if (ctx->current_function &&
                           resolve_const_count(ctx->current_function, &inst->operands[0], &resolved)) {
                    number = arm64_syscall_number((uint32_t)resolved);
                    has_number = true;
                }
                if (has_number) {
                    emit_load_imm(ctx, 8, number, true);
                } else {
                    emit_load_value(ctx, &inst->operands[0], 8);
                    /* The number is a runtime value (e.g. the stdlib wraps it in
                     * a parameter), so translate it inline. */
                    for (size_t i = 0; i < SYSCALL_MAP_COUNT; i++) {
                        uint32_t from = g_syscall_map[i][0];
                        uint32_t to = g_syscall_map[i][1];
                        emit_word(ctx, 0xF100001Fu | ((uint32_t)from << 10) | (8u << 5)); /* cmp x8, #from */
                        emit_load_imm(ctx, 9, to, true);                                   /* movz/movk x9, #to */
                        emit_word(ctx, 0x9A800000u | (8u << 16) | (9u << 5) | 8u);        /* csel x8, x9, x8, eq */
                    }
                }
                for (int i = 1; i < inst->operand_count && i <= ARM64_NUM_ARG_REGS; i++) {
                    emit_load_value(ctx, &inst->operands[i], i - 1);
                }
                emit_word(ctx, 0xD4000001u); /* svc #0 */
                emit_store_result(ctx, inst, 0);
                break;
            }
            default:
                KrtError("ARM64 backend: unsupported opcode %d in %s", (int)inst->opcode,
                         ctx->current_function ? ctx->current_function->name : "?");
                break;
            }
        }
    }
}

/* Exception support: minimal setjmp/longjmp stubs emitted directly by the
 * backend (the target is a static, libc-free ELF).  The buffer layout must
 * match KRT_JMP_BUF_* in IrGen.c. */
#define ARM64_JMP_BUF_REG_COUNT 12 /* x19..x30 */
#define ARM64_JMP_BUF_FP_OFFSET 112

static void emit_arm64_setjmp(Arm64Context* ctx) {
    /* x0 = buf; x30 = return address */
    emit_word(ctx, 0xA9005013u); /* stp x19, x20, [x0, #0] */
    emit_word(ctx, 0xA9015815u); /* stp x21, x22, [x0, #16] */
    emit_word(ctx, 0xA9026017u); /* stp x23, x24, [x0, #32] */
    emit_word(ctx, 0xA9036819u); /* stp x25, x26, [x0, #48] */
    emit_word(ctx, 0xA904701Bu); /* stp x27, x28, [x0, #64] */
    emit_word(ctx, 0xA905781Du); /* stp x29, x30, [x0, #80] */
    emit_word(ctx, 0x910003E9u); /* mov x9, sp */
    emit_word(ctx, 0xF9003009u); /* str x9, [x0, #96] */
    emit_word(ctx, 0xAD03A408u); /* stp q8, q9, [x0, #112] */
    emit_word(ctx, 0xAD04AC0Au); /* stp q10, q11, [x0, #144] */
    emit_word(ctx, 0xAD05B40Cu); /* stp q12, q13, [x0, #176] */
    emit_word(ctx, 0xAD06BC0Eu); /* stp q14, q15, [x0, #208] */
    emit_word(ctx, 0x52800000u); /* mov x0, #0 */
    emit_word(ctx, 0xD65F03C0u); /* ret */
}

static void emit_arm64_longjmp(Arm64Context* ctx) {
    /* x0 = buf, x1 = value */
    emit_word(ctx, 0xA9405013u); /* ldp x19, x20, [x0, #0] */
    emit_word(ctx, 0xA9415815u); /* ldp x21, x22, [x0, #16] */
    emit_word(ctx, 0xA9426017u); /* ldp x23, x24, [x0, #32] */
    emit_word(ctx, 0xA9436819u); /* ldp x25, x26, [x0, #48] */
    emit_word(ctx, 0xA944701Bu); /* ldp x27, x28, [x0, #64] */
    emit_word(ctx, 0xA945781Du); /* ldp x29, x30, [x0, #80] */
    emit_word(ctx, 0xF9403009u); /* ldr x9, [x0, #96] */
    emit_word(ctx, 0x9100013Fu); /* mov sp, x9 */
    emit_word(ctx, 0xAD43A408u); /* ldp q8, q9, [x0, #112] */
    emit_word(ctx, 0xAD44AC0Au); /* ldp q10, q11, [x0, #144] */
    emit_word(ctx, 0xAD45B40Cu); /* ldp q12, q13, [x0, #176] */
    emit_word(ctx, 0xAD46BC0Eu); /* ldp q14, q15, [x0, #208] */
    emit_word(ctx, 0x52800020u); /* mov x0, #1 */
    emit_word(ctx, 0xF100003Fu); /* cmp x1, #0 */
    emit_word(ctx, 0x9A801020u); /* csel x0, x1, x0, ne */
    emit_word(ctx, 0xD61F03C0u); /* br x30 */
}

static void emit_function(Arm64Context* ctx, KrtIRFunction* func) {
    if (func && func->name && strcmp(func->name, "__krt_setjmp") == 0) {
        emit_arm64_setjmp(ctx);
        return;
    }
    if (func && func->name && strcmp(func->name, "__krt_longjmp") == 0) {
        emit_arm64_longjmp(ctx);
        return;
    }

    ctx->current_function = func;
    ctx->current_block = NULL;
    ctx->fixup_count = 0;
    plan_frame(ctx, func);
    int size = frame_size(ctx);

    emit_word(ctx, 0xA9BF7BFDu); /* stp x29, x30, [sp, #-16]! */
    emit_word(ctx, 0x910003FDu); /* mov x29, sp */
    if (size > 0) {
        emit_sp_adjust(ctx, (uint32_t)size, true);
    }

    for (int i = 0; i < func->param_count && i < ARM64_NUM_ARG_REGS; i++) {
        bool param_float = func->params[i].type == TOKEN_FLOAT32 || func->params[i].type == TOKEN_FLOAT64;
        if (param_float) {
            /* Spill the AAPCS64 FP argument register into its stack slot. */
            emit_fp_to_gp(ctx, i, 0, func->params[i].type == TOKEN_FLOAT64);
            emit_store_to_slot(ctx, 0, i, func->params[i].type == TOKEN_FLOAT64);
        } else {
            emit_store_to_slot(ctx, i, i, true);
        }
    }

    emit_function_body(ctx, func);
    patch_fixups(ctx);

    emit_load_imm(ctx, 0, 0, true);
    emit_return_sequence(ctx);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void KrtArm64Generate(FILE* output_file, const char* output_filename, KrtIRModule* module) {
    (void)output_file;
    if (!module) {
        return;
    }

    Arm64Context ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.module = module;
    ctx.optimization_level = module->optimization_level;
    if (output_filename) {
        snprintf(ctx.output_filename, sizeof(ctx.output_filename), "%s", output_filename);
    } else {
        snprintf(ctx.output_filename, sizeof(ctx.output_filename), "output.kro");
    }
    ctx.writer = kro_writer_create(KRO_ARCH_ARM64);
    if (!ctx.writer) {
        return;
    }

    /* Materialise module globals into .data so loads/stores can address them
     * through their symbol. */
    for (int i = 0; i < module->global_count; i++) {
        KrtIRGlobal* global = &module->globals[i];
        if (!global || !global->name) {
            continue;
        }

        uint32_t data_offset = kro_get_data_offset(ctx.writer);
        KrtUInt128 init_value = global->is_integer_initializer
                                    ? global->init_integer
                                    : (global->has_initializer ? (KrtUInt128)(int64_t)global->init_number : 0);
        init_value = KrtIntegerNormalize(init_value, KrtTokenIntegerBits(global->type), KrtTokenIsUnsigned(global->type));
        if (global->type == TOKEN_FLOAT32) {
            float narrowed = (float)global->init_number;
            init_value = 0;
            memcpy(&init_value, &narrowed, sizeof(narrowed));
        } else if (global->type == TOKEN_FLOAT64) {
            memcpy(&init_value, &global->init_number, sizeof(double));
        }
        kro_write_data(ctx.writer, &init_value, KrtTokenIntegerBits(global->type) > 64 ? 16 : 8);
        kro_add_symbol(ctx.writer, global->name, KRO_SYM_OBJECT, KRO_BIND_GLOBAL, KRO_SEC_DATA, data_offset);
    }

    int func_count = 0;
    for (KrtIRFunction* func = module->functions; func; func = func->next) {
        func_count++;
    }

    /* Materialise string constants into .rodata and give each one a symbol. */
    if (module->string_const_count > 0 && module->string_constants) {
        ctx.string_const_sym_indices = (int32_t*)KRT_MALLOC(module->string_const_count * sizeof(int32_t));
        if (!ctx.string_const_sym_indices) {
            kro_writer_destroy(ctx.writer);
            return;
        }
        ctx.string_const_count = module->string_const_count;
        for (int i = 0; i < module->string_const_count; i++) {
            const char* text = module->string_constants[i];
            ctx.string_const_sym_indices[i] = -1;
            if (!text) {
                continue;
            }
            uint32_t offset = kro_get_rodata_offset(ctx.writer);
            kro_write_rodata(ctx.writer, text, (uint32_t)strlen(text) + 1);
            char name[64];
            snprintf(name, sizeof(name), "str_const_%d", i);
            ctx.string_const_sym_indices[i] =
                kro_add_symbol(ctx.writer, name, KRO_SYM_OBJECT, KRO_BIND_LOCAL, KRO_SEC_RODATA, offset);
        }
    }

    /* Materialise global/const storage in .data and give each one a symbol. */
    if (module->global_count > 0 && module->globals) {
        for (int i = 0; i < module->global_count; i++) {
            KrtIRGlobal* global = &module->globals[i];
            if (!global || !global->name) {
                continue;
            }
            uint32_t offset = kro_get_data_offset(ctx.writer);
            int bits = KrtTokenIntegerBits(global->type);
            int size = bits > 0 ? bits / 8 : (int)sizeof(KrtUInt128);
            uint8_t buffer[16];
            memset(buffer, 0, sizeof(buffer));
            if (global->type == TOKEN_FLOAT32) {
                float v = (float)(global->has_initializer ? global->init_number : 0.0);
                memcpy(buffer, &v, sizeof(v));
            } else if (global->type == TOKEN_FLOAT64) {
                double v = global->has_initializer ? global->init_number : 0.0;
                memcpy(buffer, &v, sizeof(v));
            } else {
                KrtUInt128 v = global->is_integer_initializer
                                    ? global->init_integer
                                    : (global->has_initializer ? (KrtUInt128)(int64_t)global->init_number : 0);
                v = KrtIntegerNormalize(v, bits, KrtTokenIsUnsigned(global->type));
                memcpy(buffer, &v, sizeof(v));
            }
            if (size > 0) {
                kro_write_data(ctx.writer, buffer, (uint32_t)size);
            }
            kro_add_symbol(ctx.writer, global->name, KRO_SYM_OBJECT, KRO_BIND_GLOBAL, KRO_SEC_DATA, offset);
        }
    }

    if (func_count == 0) {
        kro_write_file(ctx.writer, ctx.output_filename);
        KRT_FREE(ctx.string_const_sym_indices);
        kro_writer_destroy(ctx.writer);
        return;
    }

    for (KrtIRFunction* func = module->functions; func; func = func->next) {
        kro_add_symbol(ctx.writer, func->name, KRO_SYM_FUNC, KRO_BIND_GLOBAL, KRO_SEC_TEXT,
                       kro_get_code_offset(ctx.writer));
    }

    kro_set_code_offset(ctx.writer, 0);

    for (KrtIRFunction* func = module->functions; func; func = func->next) {
        while (kro_get_code_offset(ctx.writer) & 15) {
            emit_word(&ctx, ARM64_NOP);
        }
        uint32_t actual = kro_get_code_offset(ctx.writer);
        int sym = kro_find_symbol(ctx.writer, func->name);
        if (sym >= 0) {
            kro_update_symbol_value(ctx.writer, sym, actual);
        }
        if (ctx.func_count < ARM64_MAX_FUNCS) {
            ctx.funcs[ctx.func_count].name = func->name;
            ctx.funcs[ctx.func_count].offset = actual;
            ctx.func_count++;
        }
        if (strcmp(func->name, "main") == 0 || strcmp(func->name, "_KrtMainEntry") == 0) {
            ctx.entry_func_offset = actual;
            ctx.has_entry_func = true;
        }
        emit_function(&ctx, func);
    }

    if (ctx.has_entry_func) {
        while (kro_get_code_offset(ctx.writer) & 3) {
            emit_word(&ctx, ARM64_NOP);
        }
        uint32_t start_offset = kro_get_code_offset(ctx.writer);
        kro_add_symbol(ctx.writer, "_start", KRO_SYM_FUNC, KRO_BIND_GLOBAL, KRO_SEC_TEXT, start_offset);
        kro_set_entry_point(ctx.writer, start_offset);

        uint32_t bl_pc = kro_get_code_offset(ctx.writer);
        int64_t distance = (int64_t)ctx.entry_func_offset - (int64_t)bl_pc;
        emit_word(&ctx, 0x94000000u | ((uint32_t)((distance >> 2) & 0x03FFFFFFu)));
        emit_load_imm(&ctx, 8, 93, true);
        emit_word(&ctx, 0xD4000001u);
    }

    kro_write_file(ctx.writer, ctx.output_filename);
    KRT_FREE(ctx.string_const_sym_indices);
    kro_writer_destroy(ctx.writer);
}
