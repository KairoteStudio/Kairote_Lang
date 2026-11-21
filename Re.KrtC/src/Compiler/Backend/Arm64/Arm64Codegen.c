#include "Arm64Codegen.h"
#include "../../Frontend/Lexer/Tokenizer.h"
#include <stdlib.h>
#include <string.h>

/*
 * AArch64 (little endian) native backend, stage 1.
 *
 * Values live in 8-byte stack slots addressed off sp, and are shuttled through
 * x0/x1.  That keeps instruction selection and the AAPCS64 frame layout correct
 * without a register allocator; a later stage can replace the slot traffic with
 * real register allocation.
 */

#define ARM64_MAX_LOCALS 1024
#define ARM64_MAX_TEMPS 8192
#define ARM64_SLOT_SIZE 8
#define ARM64_NUM_ARG_REGS 8
#define ARM64_NOP 0xD503201Fu

typedef struct {
    char* name;
    int slot;
} Arm64Local;

typedef struct {
    KROWriter* writer;
    char output_filename[256];
    KrtIRModule* module;
    KrtIRFunction* current_function;
    int optimization_level;
    int slot_count;
    int frame_slots;
    Arm64Local locals[ARM64_MAX_LOCALS];
    int local_count;
    int temp_slot_of[ARM64_MAX_TEMPS];
    int temp_count;
    uint32_t entry_func_offset;
    bool has_entry_func;
} Arm64Context;

/* ------------------------------------------------------------------ */
/* AArch64 encoders                                                    */
/* ------------------------------------------------------------------ */

static void emit_word(Arm64Context* ctx, uint32_t word) {
    if (!ctx || !ctx->writer) {
        return;
    }
    kro_write_code(ctx->writer, &word, 4);
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
    /* Parameters occupy the first slots so the prologue can spill x0..x7. */
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

static void emit_sp_adjust(Arm64Context* ctx, uint32_t delta, bool subtract) {
    /* ADD/SUB (immediate) takes an unscaled 12-bit immediate, so a frame larger
     * than 4095 bytes has to be applied in several steps. */
    while (delta > 0) {
        uint32_t step = delta > 4095 ? 4095u : delta;
        uint32_t base = subtract ? 0xD1000000u : 0x91000000u;
        emit_word(ctx, base | (step << 10) | (31u << 5) | 31u);
        delta -= step;
    }
}

static void emit_return_sequence(Arm64Context* ctx) {
    int size = frame_size(ctx);
    if (size > 0) {
        emit_sp_adjust(ctx, (uint32_t)size, false);
    }
    emit_word(ctx, 0xA8C17BFDu); /* ldp x29, x30, [sp], #16 */
    emit_word(ctx, 0xD65F03C0u); /* ret */
}

static void emit_function_body(Arm64Context* ctx, KrtIRFunction* func) {
    for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
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
                    uint32_t div = KrtTokenIsUnsigned(inst->result.value_type) ? (is_64 ? 0x9AC00800u : 0x1AC00800u)
                                                                             : (is_64 ? 0x9AC00C00u : 0x1AC00C00u);
                    emit_binary_reg(ctx, div, 2, 0, 1); /* x2 = x0 / x1 */
                    if (inst->opcode == KRT_IR_MOD) {
                        emit_binary_reg(ctx, is_64 ? 0x9B007C00u : 0x1B007C00u, 2, 1, 2); /* x2 = x1 * x2 */
                        emit_binary_reg(ctx, is_64 ? 0xCB000000u : 0x4B000000u, 0, 0, 2); /* x0 = x0 - x2 */
                    } else {
                        emit_word(ctx, 0xAA0003E0u | ((uint32_t)2 << 16)); /* mov x0, x2 */
                    }
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
        int32_t distance = (int32_t)(ctx.entry_func_offset - bl_pc) >> 2;
        emit_word(&ctx, 0x94000000u | ((uint32_t)distance & 0x03FFFFFFu)); /* bl main */
        emit_load_imm(&ctx, 8, 93, true);                                     /* mov x8, #__NR_exit */
        emit_word(&ctx, 0xD4000001u);                                         /* svc #0 */
    }

    kro_write_file(ctx.writer, ctx.output_filename);
    kro_writer_destroy(ctx.writer);
}
