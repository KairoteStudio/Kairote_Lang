#ifndef KRT_INTEGER_H
#define KRT_INTEGER_H

#include <stdbool.h>
#include <stdint.h>

/* AST and IR arenas preserve max_align_t alignment, including native 128-bit values. */
typedef unsigned __int128 KrtUInt128;

/** Return the low-bit mask for an integer width in the range 1 through 128. */
static inline KrtUInt128 KrtIntegerMask(int bits) {
    return bits >= 128 ? ~(KrtUInt128)0 : (((KrtUInt128)1 << bits) - 1);
}

/** Truncate to bits and sign-extend signed integers; nonpositive bits leaves value unchanged. */
static inline KrtUInt128 KrtIntegerNormalize(KrtUInt128 value, int bits, bool is_unsigned) {
    if (bits <= 0) {
        return value;
    }
    KrtUInt128 mask = KrtIntegerMask(bits);
    value &= mask;
    if (!is_unsigned && (value & ((KrtUInt128)1 << (bits - 1)))) {
        value |= ~mask;
    }
    return value;
}

/** Parse decimal, 0x hexadecimal or 0b binary text; return false on malformed input or overflow. */
static inline bool KrtIntegerParse(const char* text, KrtUInt128* result) {
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    } else if (text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
        base = 2;
        text += 2;
    }
    if (!*text) {
        return false;
    }
    KrtUInt128 value = 0, limit = ~(KrtUInt128)0;
    for (; *text; text++) {
        unsigned digit;
        if (*text >= '0' && *text <= '9') {
            digit = *text - '0';
        } else if (*text >= 'a' && *text <= 'f') {
            digit = *text - 'a' + 10;
        } else if (*text >= 'A' && *text <= 'F') {
            digit = *text - 'A' + 10;
        } else {
            return false;
        }
        if (digit >= (unsigned)base || value > (limit - digit) / base) {
            return false;
        }
        value = value * base + digit;
    }
    *result = value;
    return true;
}

/** Write a terminated decimal representation to the caller's buffer of at least 42 bytes. */
static inline void KrtIntegerFormat(KrtUInt128 value, bool is_unsigned, char buffer[42]) {
    char digits[40];
    int count = 0, position = 0;
    bool negative = !is_unsigned && (value >> 127);
    if (negative) {
        value = -value;
    }
    do {
        digits[count++] = '0' + (unsigned)(value % 10);
        value /= 10;
    } while (value);
    if (negative) {
        buffer[position++] = '-';
    }
    while (count) {
        buffer[position++] = digits[--count];
    }
    buffer[position] = '\0';
}

#endif
