#include "Attributes.h"
#include "KrtCommon.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static KRT_PRINTF_FORMAT(2, 0) int format_and_output(FILE* stream, const char* format, va_list args) {
    if (!format) {
        return -1;
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int size = vsnprintf(NULL, 0, format, args_copy) + 1;
    va_end(args_copy);

    if (size <= 0) {
        return -1;
    }

    char* buffer = KRT_MALLOC((size_t)size);
    if (!buffer) {
        if (stream) {
            return vfprintf(stream, format, args);
        }
        return -1;
    }

    int written = vsnprintf(buffer, (size_t)size, format, args);
    if (written < 0) {
        KRT_FREE(buffer);
        return -1;
    }

    int result = written;
    if (fputs(buffer, stream ? stream : stdout) == EOF) {
        result = -1;
    }
    KRT_FREE(buffer);
    return result;
}

int KrtPrintFormat(const char* format, ...) {
    if (!format) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    int result = vfprintf(stdout, format, args);
    va_end(args);
    fflush(stdout);
    return result;
}

int KrtFprintf(FILE* stream, const char* format, ...) {
    if (!format) {
        return -1;
    }
    va_list args;
    va_start(args, format);
    int result = format_and_output(stream, format, args);
    va_end(args);
    if (stream) {
        fflush(stream);
    }
    return result;
}

int KrtPrintf(const char* format, ...) {
    if (!format) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    int result = vfprintf(stdout, format, args);
    va_end(args);
    fflush(stdout);
    return result;
}
