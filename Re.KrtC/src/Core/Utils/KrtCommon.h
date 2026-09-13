#ifndef KRT_COMMON_H
#define KRT_COMMON_H

#define KRT_VERSION_MAJOR 1
#define KRT_VERSION_MINOR 0
#define KRT_VERSION_PATCH 0
#define KRT_VERSION_STRING "1.0.0"

#ifdef DEBUG
#define KRT_DEBUG_ENABLED 1
#else
#define KRT_DEBUG_ENABLED 0
#endif
#define KrtError(fmt, ...)                                                                                             \
    do {                                                                                                               \
        extern int KrtOutputCacheIsEnabled(void);                                                                      \
        extern void KrtOutputCacheAddError(const char* format, ...);                                                   \
        if (KrtOutputCacheIsEnabled()) {                                                                               \
            KrtOutputCacheAddError("[KrtError] " fmt "\n", ##__VA_ARGS__);                                             \
        } else {                                                                                                       \
            fprintf(stderr, "[KrtError] " fmt "\n", ##__VA_ARGS__);                                                    \
            fflush(stderr);                                                                                            \
        }                                                                                                              \
    } while (0)

#define KrtError_LOC(file, line, fmt, ...)                                                                             \
    do {                                                                                                               \
        extern int KrtOutputCacheIsEnabled(void);                                                                      \
        extern void KrtOutputCacheAddError(const char* format, ...);                                                   \
        if (KrtOutputCacheIsEnabled()) {                                                                               \
            KrtOutputCacheAddError("[KrtError] %s:%d: " fmt "\n", file, line, ##__VA_ARGS__);                          \
        } else {                                                                                                       \
            fprintf(stderr, "[KrtError] %s:%d: " fmt "\n", file, line, ##__VA_ARGS__);                                 \
            fflush(stderr);                                                                                            \
        }                                                                                                              \
    } while (0)
#define KRT_WARNING(fmt, ...)                                                                                          \
    do {                                                                                                               \
        extern int KrtOutputCacheIsEnabled(void);                                                                      \
        extern void KrtOutputCacheAddError(const char* format, ...);                                                   \
        if (KrtOutputCacheIsEnabled()) {                                                                               \
            KrtOutputCacheAddError("[KRT_WARNING] " fmt "\n", ##__VA_ARGS__);                                          \
        } else {                                                                                                       \
            fprintf(stderr, "[KRT_WARNING] " fmt "\n", ##__VA_ARGS__);                                                 \
            fflush(stderr);                                                                                            \
        }                                                                                                              \
    } while (0)
#define KRT_WARNING_LOC(file, line, fmt, ...)                                                                          \
    do {                                                                                                               \
        extern int KrtOutputCacheIsEnabled(void);                                                                      \
        extern void KrtOutputCacheAddError(const char* format, ...);                                                   \
        if (KrtOutputCacheIsEnabled()) {                                                                               \
            KrtOutputCacheAddError("[KRT_WARNING] %s:%d: " fmt "\n", file, line, ##__VA_ARGS__);                       \
        } else {                                                                                                       \
            fprintf(stderr, "[KRT_WARNING] %s:%d: " fmt "\n", file, line, ##__VA_ARGS__);                              \
            fflush(stderr);                                                                                            \
        }                                                                                                              \
    } while (0)

#define KRT_COMPILE_ERROR(fmt, ...)                                                                                    \
    do {                                                                                                               \
        fprintf(stderr, "[KRT_COMPILE_ERROR] " fmt "\n", ##__VA_ARGS__);                                               \
        fflush(stderr);                                                                                                \
        exit(1);                                                                                                       \
    } while (0)

#define KRT_COMPILE_ERROR_LOC(file, line, fmt, ...)                                                                    \
    do {                                                                                                               \
        fprintf(stderr, "[KRT_COMPILE_ERROR] %s:%d: " fmt "\n", file, line, ##__VA_ARGS__);                            \
        fflush(stderr);                                                                                                \
        exit(1);                                                                                                       \
    } while (0)

#include "../Memory/Allocator.h"
#include "../Memory/SmartPtr.h"

#define KRT_MALLOC(size) KrtSafeMalloc(size, __FILE__, __LINE__)
#define KRT_FREE(ptr) KrtSafeFree(ptr, __FILE__, __LINE__)
#define KRT_CALLOC(count, size) KrtSafeCalloc(count, size, __FILE__, __LINE__)
#define KRT_REALLOC(ptr, size) KrtSafeRealloc(ptr, size, __FILE__, __LINE__)
#define KRT_STRDUP(str) KrtSafeStrdup(str, __FILE__, __LINE__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stddef.h>
#include "KrtString.h"
#include <time.h>
#include <stdarg.h>

double KrtGetTime(void);

void* KrtMallocChecked(size_t size, const char* file, int line);
void* KrtCallocChecked(size_t count, size_t size, const char* file, int line);
void* KrtReallocChecked(void* ptr, size_t size, const char* file, int line);
char* KrtStrdupChecked(const char* str, const char* file, int line);
void KrtFreeChecked(void* ptr, const char* file, int line);

#define KRT_STRLEN(str) strlen(str)
#define KRT_STRCPY(dest, src)                                                                                          \
    do {                                                                                                               \
        size_t src_len = strlen(src);                                                                                  \
        if (src_len >= sizeof(dest)) {                                                                                 \
            fprintf(stderr, "[KrtError] Buffer overflow in KRT_STRCPY at %s:%d\n", __FILE__, __LINE__);                \
            fflush(stderr);                                                                                            \
            exit(1);                                                                                                   \
        }                                                                                                              \
        KRT_STRCPY_S(dest, sizeof(dest), src);                                                                         \
    } while (0)
#define KRT_STRNCPY(dest, src, n)                                                                                      \
    do {                                                                                                               \
        strncpy((dest), (src), (n));                                                                                   \
        (dest)[(n) - 1] = '\0';                                                                                        \
    } while (0)
#define KRT_STRNCPY_SAFE(dest, src) KRT_STRNCPY(dest, src, sizeof(dest))

#define KRT_ASSERT(condition) ((void)0)

#ifdef _WIN32
#define KRT_API_EXPORT __declspec(dllexport)
#define KRT_API_IMPORT __declspec(dllimport)
#else

#define KRT_API_EXPORT
#define KRT_API_IMPORT
#endif

#ifdef KRT_COMPILING_DLL
#define KRT_API_EXPORT_IMPORT KRT_API_EXPORT
#else
#define KRT_API_EXPORT_IMPORT KRT_API_IMPORT
#endif

#if defined(_WIN32) || defined(_WIN64)
#define KRT_PLATFORM_WINDOWS 1
#define KRT_PLATFORM_NAME "Windows"
#elif defined(__APPLE__)
#define KRT_PLATFORM_MACOS 1
#define KRT_PLATFORM_NAME "macOS"
#elif defined(__linux__)
#define KRT_PLATFORM_LINUX 1
#define KRT_PLATFORM_NAME "Linux"
#else
#define KRT_PLATFORM_UNKNOWN 1
#define KRT_PLATFORM_NAME "Unknown"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define KRT_ARCH_X64 1
#define KRT_ARCH_NAME "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define KRT_ARCH_X86 1
#define KRT_ARCH_NAME "x86"
#elif defined(__arm__) || defined(_M_ARM)
#define KRT_ARCH_ARM 1
#define KRT_ARCH_NAME "ARM"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define KRT_ARCH_ARM64 1
#define KRT_ARCH_NAME "ARM64"
#else
#define KRT_ARCH_UNKNOWN 1
#define KRT_ARCH_NAME "Unknown"
#endif

#define KRT_LEAK_DETECTION_ENABLED 0

typedef struct {
    void* ptr;
    size_t size;
    const char* file;
    int line;
    const char* function;
    uint64_t timestamp;
    const char* type;
} MemoryAllocationInfo;

typedef struct {
    MemoryAllocationInfo* allocations;
    size_t capacity;
    size_t count;
    size_t total_allocated;
    size_t total_freed;
    uint64_t start_time;
    int enabled;
} MemoryLeakDetector;

extern MemoryLeakDetector g_memory_leak_detector;

void KrtMemoryLeakDetectorInit(void);
void KrtMemoryLeakDetectorCleanup(void);
void KrtRecordAllocation(void* ptr, size_t size, const char* file, int line, const char* function, const char* type);
void KrtRecordDeallocation(void* ptr, const char* file, int line, const char* function);
void KrtReportMemoryLeaks(void);
void KrtGetMemoryStats(size_t* current_usage, size_t* peak_usage, size_t* total_allocations, size_t* total_leaks);

#ifdef KRT_LEAK_DETECTION_ENABLED
#define KRT_MALLOC_LEAK(size) KrtMallocLeakDetected(size, __FILE__, __LINE__)
#define KRT_CALLOC_LEAK(count, size) KrtCallocLeakDetected(count, size, __FILE__, __LINE__)
#define KRT_REALLOC_LEAK(ptr, size) KrtReallocLeakDetected(ptr, size, __FILE__, __LINE__)
#define KRT_STRDUP_LEAK(str) KrtStrdupLeakDetected(str, __FILE__, __LINE__)
#define KrtFree_LEAK(ptr) KrtFreeLeakDetected(ptr, __FILE__, __LINE__)
#else
#define KRT_MALLOC_LEAK(size) KRT_MALLOC(size)
#define KRT_CALLOC_LEAK(count, size) KRT_CALLOC(count, size)
#define KRT_REALLOC_LEAK(ptr, size) KRT_REALLOC(ptr, size)
#define KRT_STRDUP_LEAK(str) KRT_STRDUP(str)
#define KrtFree_LEAK(ptr) KrtFree(ptr)
#endif

void* KrtMallocLeakDetected(size_t size, const char* file, int line);
void* KrtCallocLeakDetected(size_t count, size_t size, const char* file, int line);
void* KrtReallocLeakDetected(void* ptr, size_t size, const char* file, int line);
char* KrtStrdupLeakDetected(const char* str, const char* file, int line);
void KrtFreeLeakDetected(void* ptr, const char* file, int line);

double KrtTimeNowSeconds(void);
int KrtPathExists(const char* path);
int KrtPathGetFilename(const char* path, char* filename, size_t size);
int KrtPathRemoveExtension(const char* path, char* result, size_t size);
int KrtPathJoin(char* result, size_t size, const char* path1, const char* path2);
int KrtEnsureDirectoryRecursive(const char* path);

#define KRT_COMPILER_INFO (KRT_PLATFORM_NAME "-" KRT_ARCH_NAME " " KRT_VERSION_STRING)

#endif
