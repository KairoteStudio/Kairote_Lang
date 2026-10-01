#ifndef KRT_MEMORY_LEAK_DETECTOR_H
#define KRT_MEMORY_LEAK_DETECTOR_H

#include "../Utils/Logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define KRT_LEAK_DETECTION_ENABLED 0

void KrtMemoryLeakDetectorInit(void);

void KrtMemoryLeakDetectorCleanup(void);

void KrtRecordAllocation(void* ptr, size_t size, const char* file, int line, const char* function, const char* type);

void KrtRecordDeallocation(void* ptr, const char* file, int line, const char* function);

void KrtReportMemoryLeaks(void);

void KrtGetMemoryStats(size_t* current_usage, size_t* peak_usage, size_t* total_allocations, size_t* total_leaks);

void* KrtMallocLeakDetected(size_t size, const char* file, int line);
void* KrtCallocLeakDetected(size_t count, size_t size, const char* file, int line);
void* KrtReallocLeakDetected(void* ptr, size_t size, const char* file, int line);
char* KrtStrdupLeakDetected(const char* str, const char* file, int line);
void KrtFreeLeakDetected(void* ptr, const char* file, int line);

#if KRT_LEAK_DETECTION_ENABLED

#define KRT_LEAK_DETECTOR_INIT() KrtMemoryLeakDetectorInit()
#define KRT_LEAK_DETECTOR_CLEANUP() KrtMemoryLeakDetectorCleanup()
#define KRT_REPORT_LEAKS() KrtReportMemoryLeaks()

#define KRT_GET_MEMORY_STATS(current, peak, allocations, leaks) KrtGetMemoryStats(current, peak, allocations, leaks)
#else
#define KRT_LEAK_DETECTOR_INIT() ((void)0)
#define KRT_LEAK_DETECTOR_CLEANUP() ((void)0)
#define KRT_REPORT_LEAKS() ((void)0)
#define KRT_GET_MEMORY_STATS(current, peak, allocations, leaks)                                                        \
    do {                                                                                                               \
        if (current)                                                                                                   \
            *current = 0;                                                                                              \
        if (peak)                                                                                                      \
            *peak = 0;                                                                                                 \
        if (allocations)                                                                                               \
            *allocations = 0;                                                                                          \
        if (leaks)                                                                                                     \
            *leaks = 0;                                                                                                \
    } while (0)
#endif

#endif
