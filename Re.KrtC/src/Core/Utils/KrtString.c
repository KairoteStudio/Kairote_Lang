#include "Attributes.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "KrtString.h"

#ifndef _WIN32

KRT_PRINTF_FORMAT(1, 2) void KrtError(const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

#endif

char* KrtStrcpySafe(char* dest, size_t dest_size, const char* src) {
    if (!dest || dest_size == 0) {
        return NULL;
    }
    if (!src) {
        dest[0] = '\0';
        return dest;
    }

    size_t i;
    for (i = 0; i < dest_size - 1 && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';

    if (src[i] != '\0') {

        KrtError("[SAFE_STRING_WARNING] String truncation in strcpy_safe\n");
    }

    return dest;
}

char* KrtStrcatSafe(char* dest, size_t dest_size, const char* src) {
    if (!dest || dest_size == 0) {
        return NULL;
    }
    if (!src) {
        return dest;
    }

    size_t dest_len = strlen(dest);
    if (dest_len >= dest_size - 1) {
        return dest;
    }

    size_t i;
    for (i = 0; i < dest_size - dest_len - 1 && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';

    if (src[i] != '\0') {

        KrtError("[SAFE_STRING_WARNING] String truncation in strcat_safe\n");
    }

    return dest;
}

KRT_PRINTF_FORMAT(3, 4) int KrtSprintfSafe(char* buffer, size_t buffer_size, const char* format, ...) {
    va_list args;
    va_start(args, format);

    int result = vsnprintf(buffer, buffer_size, format, args);

    va_end(args);

    if (result >= (int)buffer_size) {

        KrtError("[SAFE_STRING_WARNING] String truncation in sprintf_safe\n");
    }

    return result;
}

size_t KrtStrlcpy(char* dest, const char* src, size_t dest_size) {
    if (!dest || dest_size == 0) {
        return 0;
    }
    if (!src) {
        dest[0] = '\0';
        return 0;
    }

    size_t src_len = strlen(src);
    size_t copy_len = (src_len < dest_size - 1) ? src_len : dest_size - 1;

    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';

    return src_len;
}

size_t KrtStrlcat(char* dest, const char* src, size_t dest_size) {
    if (!dest || dest_size == 0) {
        return 0;
    }
    if (!src) {
        return strlen(dest);
    }

    size_t dest_len = strlen(dest);
    if (dest_len >= dest_size - 1) {
        return dest_len + strlen(src);
    }

    size_t src_len = strlen(src);
    size_t remaining = dest_size - dest_len - 1;
    size_t copy_len = (src_len < remaining) ? src_len : remaining;

    memcpy(dest + dest_len, src, copy_len);
    dest[dest_len + copy_len] = '\0';

    return dest_len + src_len;
}

size_t KrtCalculateConcatenatedSize(const char* base, const char** parts, int part_count) {
    size_t total_size = strlen(base) + 1;

    for (int i = 0; i < part_count; i++) {
        if (parts[i]) {
            total_size += strlen(parts[i]);
        }
    }

    return total_size;
}
