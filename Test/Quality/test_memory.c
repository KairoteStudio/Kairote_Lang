#include "Core/Memory/Arena.h"
#include "compiler/Middle/Ir/IrSsa.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_alignment(void* ptr) {
    assert(ptr && (uintptr_t)ptr % _Alignof(max_align_t) == 0);
    *(long double*)ptr = 1.25L;
    assert(*(long double*)ptr == 1.25L);
    *(unsigned __int128*)ptr = (unsigned __int128)1 << 100;
    assert(*(unsigned __int128*)ptr == (unsigned __int128)1 << 100);
}

static void check_allocations(void) {
    for (size_t size = sizeof(max_align_t); size < 256; size++) {
        void* ptr = KRT_MALLOC(size);
        check_alignment(ptr);
        assert(KrtMemoryPtrIsValid(ptr));
        assert(!KrtMemoryPtrIsValid((char*)ptr + 1));
        void* grown = KRT_REALLOC(ptr, size + 33);
        check_alignment(grown);
        KRT_FREE(grown);
    }
    assert(!KRT_MALLOC(SIZE_MAX));
    assert(!KRT_CALLOC(SIZE_MAX / 2 + 1, 2));
    char buffer[16];
    assert(KrtBufferCheck(buffer, 8, 8, sizeof(buffer)));
    assert(!KrtBufferCheck(buffer, SIZE_MAX - 3, 8, sizeof(buffer)));
    assert(!KrtBufferCheck(buffer, 8, 9, sizeof(buffer)));
    assert(!KrtPtrGetBlock((void*)(uintptr_t)1));
    assert(!KrtMemoryGetBlockCount());
    KrtMemorySafetyCleanup();
    void* ptr = KRT_MALLOC(32);
    check_alignment(ptr);
    KRT_FREE(ptr);
}

static void check_arena(void) {
    KrtArena* arena = KrtArenaCreate(256);
    assert(arena);
    void* first = NULL;
    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < 1000; i++) {
            assert(KrtArenaAlloc(arena, (size_t)i % 17 + 1));
            void* ptr = KrtArenaAlloc(arena, sizeof(max_align_t));
            check_alignment(ptr);
            if (!pass && !i) {
                first = ptr;
            }
            if (pass && !i) {
                assert(ptr == first);
            }
        }
        size_t blocks = KrtArenaGetBlockCount(arena);
        assert(blocks > 1);
        assert(!KrtArenaAlloc(arena, SIZE_MAX));
        assert(!KrtArenaCalloc(arena, SIZE_MAX / 2 + 1, 2));
        KrtArenaReset(arena);
        assert(!KrtArenaGetUsage(arena));
        assert(KrtArenaGetBlockCount(arena) == blocks);
    }
    char* text = KrtArenaStrdup(arena, "alignment");
    char* grown = KrtArenaRealloc(arena, text, 10, 40);
    assert(grown == text && !strcmp(grown, "alignment"));
    assert(!KrtArenaRealloc(arena, grown, 40, SIZE_MAX));
    assert(!strcmp(grown, "alignment"));
    KrtArenaDestroy(arena);
}

static void check_ir_pool(void) {
    KrtIRMemoryArena* arena = KrtIrArenaCreate(256);
    assert(arena);
    void* first = NULL;
    size_t count = 0;
    for (int pass = 0; pass < 4; pass++) {
        for (int i = 0; i < 1000; i++) {
            assert(KrtIrArenaAlloc(arena, (size_t)i % 17 + 1));
            void* ptr = KrtIrArenaAlloc(arena, sizeof(max_align_t));
            check_alignment(ptr);
            if (!pass && !i) {
                first = ptr;
            }
            if (pass && !i) {
                assert(ptr == first);
            }
        }
        if (!pass) {
            count = arena->pool_count;
        }
        assert(arena->pool_count == count);
        assert(!KrtIrArenaAlloc(arena, SIZE_MAX));
        KrtIrArenaReset(arena);
        assert(arena->pool_count == count && !arena->total_allocated);
    }
    KrtIRPoolManager manager;
    KrtIrPoolManagerInit(&manager, arena);
    for (int type = 0; type < KRT_POOL_COUNT; type++) {
        void* objects[200];
        for (int i = 0; i < 200; i++) {
            objects[i] = KrtIrPoolAlloc(&manager, (KrtPoolObjectType)type);
            assert(objects[i] && (uintptr_t)objects[i] % _Alignof(max_align_t) == 0);
            memset(objects[i], 0x5a, manager.pools[type].object_size);
        }
        for (int i = 0; i < 200; i++) {
            KrtIrPoolFree(&manager, (KrtPoolObjectType)type, objects[i]);
        }
        for (int i = 199; i >= 0; i--) {
            unsigned char* obj = KrtIrPoolAlloc(&manager, (KrtPoolObjectType)type);
            assert(obj == objects[i]);
            for (size_t j = 0; j < manager.pools[type].object_size; j++) {
                assert(!obj[j]);
            }
        }
    }
    KrtIrPoolManagerDestroy(&manager);
    KrtIrArenaDestroy(arena);
}

static void* allocate_thread(void* argument) {
    KrtArena* arena = argument;
    for (int i = 0; i < 3000; i++) {
        void* ptr = KRT_MALLOC(31);
        check_alignment(ptr);
        assert(KrtPtrGetBlock(ptr));
        check_alignment(KrtArenaAlloc(arena, 31));
        KRT_FREE(ptr);
    }
    return NULL;
}

static void check_threads(void) {
    KrtArena* arena = KrtArenaCreate(4096);
    assert(arena);
    pthread_t threads[8];
    for (size_t i = 0; i < 8; i++) {
        assert(!pthread_create(&threads[i], NULL, allocate_thread, arena));
    }
    for (size_t i = 0; i < 8; i++) {
        assert(!pthread_join(threads[i], NULL));
    }
    assert(arena->allocation_count == 24000);
    KrtArenaDestroy(arena);
}

int main(void) {
    check_allocations();
    check_arena();
    check_ir_pool();
    check_threads();
    assert(!KrtMemoryGetTotalUsage() && !KrtMemoryGetBlockCount());
    KrtMemorySafetyCleanup();
    puts("allocation, overflow, alignment, reuse and concurrency checks passed");
    return 0;
}
