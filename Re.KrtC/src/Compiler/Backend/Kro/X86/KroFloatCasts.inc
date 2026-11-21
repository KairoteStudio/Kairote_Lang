/* Casts operate on the seed ABI's RDX:RAX pair and double expression bits. */
static void emit_cast_save(KROCodegenContext* ctx, int low, int high, bool pair) {
    emit_bytes(ctx, (const uint8_t*)"\x50\x51\x52\x41\x50\x41\x51", 7);
    if (pair) {
        if (low >= 8) { emit_byte(ctx, 0x41); }
        emit_byte(ctx, 0x50 + (low & 7));
        if (high >= 8) { emit_byte(ctx, 0x41); }
        emit_byte(ctx, 0x50 + (high & 7));
        emit_bytes(ctx, (const uint8_t*)"\x5A\x58", 2);
    } else {
        emit_move_reg_to_reg(ctx, low, KRT_REG_RAX);
    }
}

static int cast_saved_offset(int reg) {
    switch (reg) {
    case KRT_REG_RAX: return 32;
    case KRT_REG_RCX: return 24;
    case KRT_REG_RDX: return 16;
    case KRT_REG_R8: return 8;
    default: return 0;
    }
}

static void emit_cast_restore(KROCodegenContext* ctx, int low, int high, bool pair) {
    emit_bytes(ctx, (const uint8_t*)"\x48\x89\x44\x24", 4);
    emit_byte(ctx, (uint8_t)cast_saved_offset(low));
    if (pair) {
        emit_bytes(ctx, (const uint8_t*)"\x48\x89\x54\x24", 4);
        emit_byte(ctx, (uint8_t)cast_saved_offset(high));
    }
    emit_bytes(ctx, (const uint8_t*)"\x41\x59\x41\x58\x5A\x59\x58", 7);
}

/* Shift the full magnitude to p bits, separating guard from all preceding
 * discarded bits. Round once to nearest/even, then assemble IEEE payloads.
 * The loop intentionally avoids x86's shift-count modulo-64 behavior. */
static const uint8_t wide_float4_core[] = {
    0x48, 0x89, 0xC1, 0x48, 0x09, 0xD1, 0x0F, 0x84, 0x80, 0x00, 0x00, 0x00,
    0x48, 0x85, 0xD2, 0x74, 0x09, 0x48, 0x0F, 0xBD, 0xCA, 0x83, 0xC1, 0x40,
    0xEB, 0x04, 0x48, 0x0F, 0xBD, 0xC8, 0x51, 0x83, 0xE9, 0x17, 0x7E, 0x61,
    0x45, 0x31, 0xC9, 0xFF, 0xC9, 0x74, 0x0E, 0x48, 0xD1, 0xEA, 0x48, 0xD1,
    0xD8, 0x41, 0x83, 0xD1, 0x00, 0xFF, 0xC9, 0x75, 0xF2, 0x48, 0xD1, 0xEA,
    0x48, 0xD1, 0xD8, 0x73, 0x0C, 0x45, 0x85, 0xC9, 0x75, 0x04, 0xA8, 0x01,
    0x74, 0x03, 0x48, 0xFF, 0xC0, 0x48, 0xC7, 0xC2, 0x00, 0x00, 0x00, 0x01,
    0x48, 0x39, 0xD0, 0x75, 0x07, 0x48, 0xD1, 0xE8, 0x48, 0xFF, 0x04, 0x24,
    0x59, 0x83, 0xC1, 0x7F, 0x81, 0xF9, 0xFF, 0x00, 0x00, 0x00, 0x7D, 0x0F,
    0xC1, 0xE1, 0x17, 0x25, 0xFF, 0xFF, 0x7F, 0x00, 0x09, 0xC8, 0x44, 0x09,
    0xC0, 0xEB, 0x13, 0xB8, 0x00, 0x00, 0x80, 0x7F, 0x44, 0x09, 0xC0, 0xEB,
    0x09, 0xF7, 0xD9, 0x48, 0xD3, 0xE0, 0xEB, 0xD4, 0x31, 0xC0,
};

static const uint8_t wide_float8_core[] = {
    0x48, 0x89, 0xC1, 0x48, 0x09, 0xD1, 0x74, 0x7E, 0x48, 0x85, 0xD2, 0x74,
    0x09, 0x48, 0x0F, 0xBD, 0xCA, 0x83, 0xC1, 0x40, 0xEB, 0x04, 0x48, 0x0F,
    0xBD, 0xC8, 0x51, 0x83, 0xE9, 0x34, 0x7E, 0x5F, 0x45, 0x31, 0xC9, 0xFF,
    0xC9, 0x74, 0x0E, 0x48, 0xD1, 0xEA, 0x48, 0xD1, 0xD8, 0x41, 0x83, 0xD1,
    0x00, 0xFF, 0xC9, 0x75, 0xF2, 0x48, 0xD1, 0xEA, 0x48, 0xD1, 0xD8, 0x73,
    0x0C, 0x45, 0x85, 0xC9, 0x75, 0x04, 0xA8, 0x01, 0x74, 0x03, 0x48, 0xFF,
    0xC0, 0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x48,
    0x39, 0xD0, 0x75, 0x07, 0x48, 0xD1, 0xE8, 0x48, 0xFF, 0x04, 0x24, 0x59,
    0x81, 0xC1, 0xFF, 0x03, 0x00, 0x00, 0x48, 0xC1, 0xE1, 0x34, 0x48, 0xBA,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x48, 0x21, 0xD0, 0x48,
    0x09, 0xC8, 0x4C, 0x09, 0xC0, 0xEB, 0x09, 0xF7, 0xD9, 0x48, 0xD3, 0xE0,
    0xEB, 0xD9, 0x31, 0xC0,
};

static void emit_integer_float_cast(KROCodegenContext* ctx, int low, int high, bool is_unsigned, int width) {
    emit_cast_save(ctx, low, high, true);
    emit_bytes(ctx, (const uint8_t*)"\x45\x31\xC0", 3);
    if (!is_unsigned) {
        emit_bytes(ctx, (const uint8_t*)"\x48\x85\xD2", 3);
        uint32_t positive = emit_local_jump(ctx, 0x89);
        emit_load_imm64_to_reg(ctx, width == 4 ? UINT64_C(0x80000000) : UINT64_C(0x8000000000000000), KRT_REG_R8);
        emit_bytes(ctx, (const uint8_t*)"\x48\xF7\xD8\x48\x83\xD2\x00\x48\xF7\xDA", 10);
        patch_local_jump(ctx, positive, kro_get_code_offset(ctx->writer));
    }
    if (width == 4) {
        emit_bytes(ctx, wide_float4_core, sizeof(wide_float4_core));
        /* Seed float32 expression values are exact widened binary32 bits. */
        emit_float32_conversion(ctx, KRT_REG_RAX, false);
    } else {
        emit_bytes(ctx, wide_float8_core, sizeof(wide_float8_core));
    }
    emit_cast_restore(ctx, low, high, false);
}

static void emit_float_wide_cast(KROCodegenContext* ctx, int low, int high) {
    emit_cast_save(ctx, low, high, false);
    emit_bytes(ctx, (const uint8_t*)"\x49\x89\xC1\x49\xC1\xE9\x3F\x48\x89\xC1\x48\xC1\xE9\x34\x81\xE1\xFF\x07\x00\x00\x81\xE9\xFF\x03\x00\x00\x31\xD2\x85\xC9", 30);
    uint32_t zero_small = emit_local_jump(ctx, 0x88);
    emit_bytes(ctx, (const uint8_t*)"\x83\xF9\x7F", 3);
    uint32_t zero_large = emit_local_jump(ctx, 0x8F);
    emit_load_imm64_to_reg(ctx, UINT64_C(0x000FFFFFFFFFFFFF), KRT_REG_RDX);
    emit_bytes(ctx, (const uint8_t*)"\x48\x21\xD0", 3);
    emit_load_imm64_to_reg(ctx, UINT64_C(0x0010000000000000), KRT_REG_RDX);
    emit_bytes(ctx, (const uint8_t*)"\x48\x09\xD0\x31\xD2\x83\xE9\x34", 8);
    uint32_t left = emit_local_jump(ctx, 0x89);
    emit_bytes(ctx, (const uint8_t*)"\xF7\xD9\x48\xD3\xE8", 5);
    uint32_t sign_right = emit_local_jump(ctx, 0);
    patch_local_jump(ctx, left, kro_get_code_offset(ctx->writer));
    emit_bytes(ctx, (const uint8_t*)"\x83\xF9\x40", 3);
    uint32_t upper = emit_local_jump(ctx, 0x8D);
    emit_bytes(ctx, (const uint8_t*)"\x48\x0F\xA5\xC2\x48\xD3\xE0", 7);
    uint32_t sign_left = emit_local_jump(ctx, 0);
    patch_local_jump(ctx, upper, kro_get_code_offset(ctx->writer));
    emit_bytes(ctx, (const uint8_t*)"\x83\xE9\x40\x48\x89\xC2\x48\xD3\xE2\x31\xC0", 11);
    uint32_t sign = kro_get_code_offset(ctx->writer);
    patch_local_jump(ctx, sign_right, sign);
    patch_local_jump(ctx, sign_left, sign);
    emit_bytes(ctx, (const uint8_t*)"\x45\x85\xC9", 3);
    uint32_t ready_positive = emit_local_jump(ctx, 0x84);
    emit_bytes(ctx, (const uint8_t*)"\x48\xF7\xD8\x48\x83\xD2\x00\x48\xF7\xDA", 10);
    uint32_t ready_negative = emit_local_jump(ctx, 0);
    uint32_t zero = kro_get_code_offset(ctx->writer);
    patch_local_jump(ctx, zero_small, zero);
    patch_local_jump(ctx, zero_large, zero);
    emit_bytes(ctx, (const uint8_t*)"\x31\xC0\x31\xD2", 4);
    uint32_t ready = kro_get_code_offset(ctx->writer);
    patch_local_jump(ctx, ready_positive, ready);
    patch_local_jump(ctx, ready_negative, ready);
    emit_cast_restore(ctx, low, high, true);
}
