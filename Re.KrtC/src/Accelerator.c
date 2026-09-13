#include "Accelerator.h"
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "Core/Utils/KrtCommon.h"

size_t accelerator_strlen(const char* str) {
    if (!str) {
        return 0;
    }

    const char* s = str;
    while (*s) {
        s++;
    }
    return (size_t)(s - str);
}

int accelerator_strcmp(const char* str1, const char* str2) {
    if (!str1 && !str2) {
        return 0;
    }
    if (!str1) {
        return -1;
    }
    if (!str2) {
        return 1;
    }

    while (*str1 && *str2 && *str1 == *str2) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

double accelerator_add_double(double a, double b) {
    return a + b;
}

double accelerator_sub_double(double a, double b) {
    return a - b;
}

double accelerator_mul_double(double a, double b) {
    return a * b;
}

double accelerator_div_double(double a, double b) {
    if (b == 0) {
        KrtError("Division by zero in accelerator_div_double");
        return 0;
    }
    return a / b;
}

void* accelerator_memcpy(void* dest, const void* src, size_t n) {
    if (!dest || !src || n == 0) {
        return dest;
    }

    char* d = (char*)dest;
    const char* s = (const char*)src;

    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void* accelerator_memset(void* s, int c, size_t n) {
    if (!s || n == 0) {
        return s;
    }

    char* p = (char*)s;
    while (n--) {
        *p++ = (char)c;
    }
    return s;
}

bool accelerator_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool accelerator_is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool accelerator_is_octal_digit(char c) {
    return c >= '0' && c <= '7';
}

bool accelerator_is_hex_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int accelerator_hex_to_int(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

int encode_utf8(char* buffer, int codepoint, int max_length) {
    if (codepoint < 0x80) {
        if (max_length < 1) {
            return 0;
        }
        buffer[0] = (char)codepoint;
        return 1;
    } else if (codepoint < 0x800) {
        if (max_length < 2) {
            return 0;
        }
        buffer[0] = (char)(0xC0 | (codepoint >> 6));
        buffer[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    } else if (codepoint < 0x10000) {
        if (max_length < 3) {
            return 0;
        }
        buffer[0] = (char)(0xE0 | (codepoint >> 12));
        buffer[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    } else {
        if (max_length < 4) {
            return 0;
        }
        buffer[0] = (char)(0xF0 | (codepoint >> 18));
        buffer[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buffer[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buffer[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
}

bool accelerator_is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

uint32_t accelerator_hash_string(const char* str) {
    if (!str) {
        return 0;
    }

    uint32_t hash = 5381;
    char c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}
