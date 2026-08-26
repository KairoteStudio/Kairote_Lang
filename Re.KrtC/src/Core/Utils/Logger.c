#include "OutputCache.h"
#include "KrtCommon.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static int KrtFormatAndOutput(FILE* stream, const char* format, va_list args) {
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
        if (!KrtOutputCacheIsEnabled() && stream) {
            va_list args_direct;
            va_copy(args_direct, args);
            int result = vfprintf(stream, format, args_direct);
            va_end(args_direct);
            return result;
        }
        return -1;
    }

    int written = vsnprintf(buffer, (size_t)size, format, args);
    if (written < 0) {
        KRT_FREE(buffer);
        return -1;
    }

    int result = written;
    if (KrtOutputCacheIsEnabled()) {
        if (stream == stdout) {
            KrtOutputCacheAdd("%s", buffer);
        } else if (stream == stderr) {
            KrtOutputCacheAddError("%s", buffer);
        } else {
            if (fputs(buffer, stream) == EOF) {
                result = -1;
            }
        }
    } else {
        if (stream) {
            if (fputs(buffer, stream) == EOF) {
                result = -1;
            }
        } else {
            if (fputs(buffer, stdout) == EOF) {
                result = -1;
            }
        }
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

    int result;
    if (KrtOutputCacheIsEnabled()) {
        va_list args_copy;
        va_copy(args_copy, args);
        int size = vsnprintf(NULL, 0, format, args_copy) + 1;
        va_end(args_copy);

        if (size <= 0) {
            va_end(args);
            return -1;
        }

        char* buffer = KRT_MALLOC((size_t)size);
        if (!buffer) {
            va_end(args);
            return -1;
        }

        int written = vsnprintf(buffer, (size_t)size, format, args);
        if (written >= 0) {
            result = fputs(buffer, stdout);
            fflush(stdout);
        } else {
            result = -1;
        }

        KRT_FREE(buffer);
    } else {
        result = vfprintf(stdout, format, args);
        fflush(stdout);
    }

    va_end(args);
    return result;
}

int KrtFprintf(FILE* stream, const char* format, ...) {
    if (!format) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    int result = KrtFormatAndOutput(stream, format, args);
    va_end(args);

    if (!KrtOutputCacheIsEnabled() && stream) {
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