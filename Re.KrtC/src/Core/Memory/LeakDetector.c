#include "LeakDetector.h"
#include "../Utils/KrtCommon.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern char *strrchr(const char *s, int c);
extern size_t strlen(const char *s);

MemoryLeakDetector g_memory_leak_detector = {0};

void KrtMemoryLeakDetectorInit(void) {
    if (!KRT_LEAK_DETECTION_ENABLED) return;

    g_memory_leak_detector.capacity = 1024;
    g_memory_leak_detector.allocations = KRT_MALLOC(sizeof(MemoryAllocationInfo) * g_memory_leak_detector.capacity);
    g_memory_leak_detector.count = 0;
    g_memory_leak_detector.total_allocated = 0;
    g_memory_leak_detector.total_freed = 0;
    g_memory_leak_detector.start_time = (uint64_t)time(NULL);
    g_memory_leak_detector.enabled = 1;

    if (!g_memory_leak_detector.allocations) {
        g_memory_leak_detector.enabled = 0;
        return;
    }

}

void KrtMemoryLeakDetectorCleanup(void) {
    if (!KRT_LEAK_DETECTION_ENABLED || !g_memory_leak_detector.enabled) return;

    KrtReportMemoryLeaks();

    if (g_memory_leak_detector.allocations) {
        KRT_FREE(g_memory_leak_detector.allocations);
        g_memory_leak_detector.allocations = NULL;
    }

    g_memory_leak_detector.capacity = 0;
    g_memory_leak_detector.count = 0;
    g_memory_leak_detector.enabled = 0;

}

static void expand_allocation_array(void) {
    size_t new_capacity = g_memory_leak_detector.capacity * 2;
    MemoryAllocationInfo* new_allocations = KRT_REALLOC(g_memory_leak_detector.allocations,
                                                    sizeof(MemoryAllocationInfo) * new_capacity);

    if (!new_allocations) {
        return;
    }

    g_memory_leak_detector.allocations = new_allocations;
    g_memory_leak_detector.capacity = new_capacity;

}

void KrtRecordAllocation(void* ptr, size_t size, const char* file, int line,
                         const char* function, const char* type) {
    if (!KRT_LEAK_DETECTION_ENABLED || !g_memory_leak_detector.enabled || !ptr) return;

    if (g_memory_leak_detector.count >= g_memory_leak_detector.capacity) {
        expand_allocation_array();
    }

    MemoryAllocationInfo* info = &g_memory_leak_detector.allocations[g_memory_leak_detector.count];
    info->ptr = ptr;
    info->size = size;
    info->file = file;
    info->line = line;
    info->function = function;
    info->timestamp = (uint64_t)time(NULL);
    info->type = type;

    g_memory_leak_detector.count++;
    g_memory_leak_detector.total_allocated += size;

}

void KrtRecordDeallocation(void* ptr, const char* file, int line, const char* function) {
    (void)file;
    (void)line;
    (void)function;
    if (!KRT_LEAK_DETECTION_ENABLED || !g_memory_leak_detector.enabled || !ptr) return;

    for (size_t i = 0; i < g_memory_leak_detector.count; i++) {
        if (g_memory_leak_detector.allocations[i].ptr == ptr) {

            g_memory_leak_detector.total_freed += g_memory_leak_detector.allocations[i].size;

            if (i < g_memory_leak_detector.count - 1) {
                g_memory_leak_detector.allocations[i] =
                    g_memory_leak_detector.allocations[g_memory_leak_detector.count - 1];
            }

            g_memory_leak_detector.count--;

            return;
        }
    }

}

void KrtReportMemoryLeaks(void) {
    if (!KRT_LEAK_DETECTION_ENABLED || !g_memory_leak_detector.enabled) return;
    if (g_memory_leak_detector.count == 0) return;

    size_t total_leaked = 0;
    for (size_t i = 0; i < g_memory_leak_detector.count; i++) {
        total_leaked += g_memory_leak_detector.allocations[i].size;
    }

    KrtError("[LEAK] blk=%zu, bytes=%zu, alloc=%zu, free=%zu\n",
              g_memory_leak_detector.count, total_leaked,
              g_memory_leak_detector.total_allocated,
              g_memory_leak_detector.total_freed);
}

void KrtGetMemoryStats(size_t* current_usage, size_t* peak_usage,
                        size_t* total_allocations, size_t* total_leaks) {
    if (!KRT_LEAK_DETECTION_ENABLED || !g_memory_leak_detector.enabled) {
        if (current_usage) *current_usage = 0;
        if (peak_usage) *peak_usage = 0;
        if (total_allocations) *total_allocations = 0;
        if (total_leaks) *total_leaks = 0;
        return;
    }

    if (current_usage) *current_usage = g_memory_leak_detector.total_allocated - g_memory_leak_detector.total_freed;
    if (peak_usage) *peak_usage = g_memory_leak_detector.total_allocated;
    if (total_allocations) *total_allocations = g_memory_leak_detector.total_allocated;
    if (total_leaks) *total_leaks = g_memory_leak_detector.count;
}

void* KrtMallocLeakDetected(size_t size, const char* file, int line) {
    void* ptr = KRT_MALLOC(size);
    if (ptr) {
        KrtRecordAllocation(ptr, size, file, line, __func__, "malloc");
    }
    return ptr;
}

void* KrtCallocLeakDetected(size_t count, size_t size, const char* file, int line) {
    void* ptr = KRT_CALLOC(count, size);
    if (ptr) {
        KrtRecordAllocation(ptr, count * size, file, line, __func__, "calloc");
    }
    return ptr;
}

void* KrtReallocLeakDetected(void* ptr, size_t size, const char* file, int line) {

    if (ptr) {
        KrtRecordDeallocation(ptr, file, line, __func__);
    }

    void* new_ptr = KRT_REALLOC(ptr, size);
    if (new_ptr) {
        KrtRecordAllocation(new_ptr, size, file, line, __func__, "realloc");
    }

    return new_ptr;
}

char* KrtStrdupLeakDetected(const char* str, const char* file, int line) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char* new_str = KRT_MALLOC(len + 1);
    if (new_str) {
        KRT_STRCPY_S(new_str, len + 1, str);
        KrtRecordAllocation(new_str, len + 1, file, line, __func__, "strdup");
    }
    return new_str;
}

void KRT_FREELeakDetected(void* ptr, const char* file, int line) {
    if (ptr) {
        KrtRecordDeallocation(ptr, file, line, __func__);
    }
    KRT_FREE(ptr);
}
