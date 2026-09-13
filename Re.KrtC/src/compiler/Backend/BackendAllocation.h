#ifndef KRT_BACKEND_ALLOCATION_H
#define KRT_BACKEND_ALLOCATION_H

#include "../../Core/Memory/Allocator.h"
#include "../../Core/Utils/KrtCommon.h"

static inline void* backend_allocate(size_t size) {
    void* allocation = KRT_MALLOC(size ? size : 1);
    if (!allocation) {
        KRT_COMPILE_ERROR("Backend allocation failed (%zu bytes)", size);
    }
    return allocation;
}

static inline void* backend_allocate_zeroed(size_t count, size_t size) {
    void* allocation = KRT_CALLOC(count ? count : 1, size ? size : 1);
    if (!allocation) {
        KRT_COMPILE_ERROR("Backend allocation failed (%zu elements of %zu bytes)", count, size);
    }
    return allocation;
}

static inline void* backend_reallocate(void* previous, size_t size) {
    void* allocation = KRT_REALLOC(previous, size ? size : 1);
    if (!allocation) {
        KRT_COMPILE_ERROR("Backend reallocation failed (%zu bytes)", size);
    }
    return allocation;
}

#endif
