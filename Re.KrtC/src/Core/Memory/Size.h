#ifndef KRT_MEMORY_SIZE_H
#define KRT_MEMORY_SIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Round size up to a power-of-two alignment; leave result unchanged on overflow. */
static inline bool KrtSizeAlign(size_t size, size_t alignment, size_t* result) {
    if (!alignment || (alignment & (alignment - 1)) || size > SIZE_MAX - (alignment - 1)) {
        return false;
    }
    *result = (size + alignment - 1) & ~(alignment - 1);
    return true;
}

/** Store count * size in result, or return false without writing on overflow. */
static inline bool KrtSizeMultiply(size_t count, size_t size, size_t* result) {
    if (size && count > SIZE_MAX / size) {
        return false;
    }
    *result = count * size;
    return true;
}

#endif
