#ifndef KAIROTE_TARGET_H
#define KAIROTE_TARGET_H

#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#define KRT_STACK_ALIGNMENT 16
#define KRT_SHADOW_SPACE_SIZE 32
#define KRT_PAGE_SIZE 4096
#define KRT_SIZE_INT8 1
#define KRT_SIZE_INT16 2
#define KRT_SIZE_INT32 4
#define KRT_SIZE_INT64 8
#define KRT_SIZE_PTR 8
#define KRT_ALIGN_UP(val, align) (((val) + (align) - 1) & ~((align) - 1))
#define KRT_IS_ALIGNED(val, align) (((val) & ((align) - 1)) == 0)
#ifdef _WIN32
#define KRT_ALIGNED_ALLOC(align, size) _aligned_malloc(size, align)
#define KRT_ALIGNED_FREE(ptr) _aligned_free(ptr)
#else
#define KRT_ALIGNED_ALLOC(align, size) aligned_alloc(align, size)
#define KRT_ALIGNED_FREE(ptr) free(ptr)
#endif

static_assert(sizeof(size_t) == 8, "KairoteLang requires 64-bit architecture");
static_assert(sizeof(void*) == 8, "KairoteLang requires 64-bit pointers");

typedef uint64_t KrtFileOffset;
typedef uint64_t KrtMemSize;
typedef uint64_t KrtBufferSize;

#define KRT_ASSERT_ALIGNED(ptr, type) assert(((uintptr_t)(ptr) % alignof(type)) == 0)

#endif
