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

static void emit_load_from_slot(Arm64Context* ctx, int reg, int slot, bool is_64) {
    uint32_t op = is_64 ? 0xF9400000u : 0xB9400000u;
    uint32_t scale = is_64 ? 8u : 4u;
    uint32_t imm = (uint32_t)((slot * ARM64_SLOT_SIZE) / scale) & 0xFFFu;
    emit_word(ctx, op | (imm << 10) | (31u << 5) | (uint32_t)(reg & 0x1F));
}

static void emit_store_to_slot(Arm64Context* ctx, int reg, int slot, bool is_64) {
    uint32_t op = is_64 ? 0xF9000000u : 0xB9000000u;
    uint32_t scale = is_64 ? 8u : 4u;
    uint32_t imm = (uint32_t)((slot * ARM64_SLOT_SIZE) / scale) & 0xFFFu;
    emit_word(ctx, op | (imm << 10) | (31u << 5) | (uint32_t)(reg & 0x1F));
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
static uint32_t arm64_syscall_number(uint32_t x86_number) {
    switch (x86_number) {
    case 0:
        return 63; /* read */
    case 1:
        return 64; /* write */
    case 3:
        return 57; /* close */
    case 5:
        return 80; /* fstat */
    case 8:
        return 62; /* lseek */
    case 9:
        return 222; /* mmap */
    case 10:
        return 226; /* mprotect */
    case 11:
        return 215; /* munmap */
    case 12:
        return 214; /* brk */
    case 16:
        return 29; /* ioctl */
    case 60:
        return 93; /* exit */
    case 231:
        return 94; /* exit_group */
    default:
        return x86_number;
    }
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

static int alloc_slot(Arm64Context* ctx) {
    return ctx->slot_count++;
}

static int local_slot(Arm64Context* ctx, const char* name) {
    if (!name) {
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
    int bits = KrtTokenIntegerBits(value->value_type);
    return bits == 0 || bits > 32;
}

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
    default:
        emit_load_imm(ctx, reg, 0, is_64);
        break;
    }
}

static void emit_store_result(Arm64Context* ctx, KrtIRInst* inst, int reg) {
    int slot = value_slot(ctx, &inst->result);
    if (slot >= 0) {
        emit_store_to_slot(ctx, reg, slot, value_is_64(&inst->result));
    }
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
        emit_load_value(ctx, arg, i);
    }
    add_fixup(ctx, 4, 0, -1, name);
    emit_word(ctx, 0x94000000u); /* bl (patched) */
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
                    emit_load_value(ctx, &inst->operands[0], 0);
                    emit_store_result(ctx, inst, 0);
                }
                break;
            case KRT_IR_CAST: {
                if (inst->operand_count >= 1) {
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
                    KrtError("ARM64 backend: 128-bit indirect loads are not supported");
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
                if (inst->operands[0].type == KRT_IR_VALUE_IMM) {
                    number = arm64_syscall_number((uint32_t)(int64_t)inst->operands[0].data.imm);
                }
                if (inst->operands[0].type == KRT_IR_VALUE_IMM) {
                    emit_load_imm(ctx, 8, number, true);
                } else {
                    emit_load_value(ctx, &inst->operands[0], 8);
                    /* dynamic number: translate at runtime via a small stub is not
                     * available yet, so use it verbatim */
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

static void emit_function(Arm64Context* ctx, KrtIRFunction* func) {
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
        emit_store_to_slot(ctx, i, i, true);
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

    int func_count = 0;
    for (KrtIRFunction* func = module->functions; func; func = func->next) {
        func_count++;
    }
    if (func_count == 0) {
        kro_write_file(ctx.writer, ctx.output_filename);
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
    kro_writer_destroy(ctx.writer);
}
