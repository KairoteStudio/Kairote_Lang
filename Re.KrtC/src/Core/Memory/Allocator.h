#ifndef KRT_MEMORY_H
#define KRT_MEMORY_H

#include "../Utils/Logger.h"
#include "../Platform/Platform.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
typedef CRITICAL_SECTION KrtMutexT;
#else
#include <pthread.h>
typedef pthread_mutex_t KrtMutexT;
#endif

typedef enum {
    MEM_PROTECT_NONE = 0,
    MEM_PROTECT_READ = 1 << 0,
    MEM_PROTECT_WRITE = 1 << 1,
    MEM_PROTECT_EXECUTE = 1 << 2,
    MEM_PROTECT_ALL = MEM_PROTECT_READ | MEM_PROTECT_WRITE | MEM_PROTECT_EXECUTE
} MemoryProtectionFlags;

typedef struct MemoryBlock {
    void* user_ptr;
    void* actual_ptr;
    size_t user_size;
    size_t actual_size;
    uint32_t magic;
    MemoryProtectionFlags protection;
    bool is_freed;
    bool has_canary;
    const char* file;
    int line;
    struct MemoryBlock* next;
    struct MemoryBlock* prev;
    struct MemoryBlock* hash_next;
} MemoryBlock;

typedef struct MemorySafetyManager {
    MemoryBlock* blocks;
    size_t block_count;
    KrtMutexT mutex;
    bool poison_enabled;
    bool canary_enabled;
    MemoryBlock** buckets;
    size_t bucket_count;
    size_t total_used;
} MemorySafetyManager;

extern MemorySafetyManager* g_memory_safety;

void KrtMemorySafetyInit(void);
void KrtMemorySafetyCleanup(void);

void* KrtSafeMalloc(size_t size, const char* file, int line);
void* KrtSafeCalloc(size_t count, size_t size, const char* file, int line);
void* KrtSafeRealloc(void* ptr, size_t size, const char* file, int line);
char* KrtSafeStrdup(const char* str, const char* file, int line);

void KrtSafeFree(void* ptr, const char* file, int line);

bool KrtBoundsCheck(const void* array, size_t index, size_t element_size, size_t array_size);
bool KrtBufferCheck(const void* buffer, size_t offset, size_t size, size_t buffer_size);

bool KrtMemoryProtect(void* ptr, MemoryProtectionFlags flags);
bool KrtMemoryUnprotect(void* ptr);

void KrtMemoryPoison(void* ptr, size_t size);
bool KrtMemoryIsPoisoned(const void* ptr, size_t size);

void KrtSetCanary(MemoryBlock* block);
bool KrtCheckCanary(const MemoryBlock* block);

bool KrtMemoryPtrIsValid(const void* ptr);
bool KrtPtrIsFreed(const void* ptr);
MemoryBlock* KrtPtrGetBlock(const void* ptr);

bool KrtPtrIsDangling(const void* ptr);
void KrtPtrTrackFree(void* ptr);

bool KrtPtrCheckDoubleFree(const void* ptr, const char* file, int line);

void KrtMemoryDumpBlocks(void);
size_t KrtMemoryGetTotalUsage(void);
size_t KrtMemoryGetBlockCount(void);
void KrtMemoryDumpStats(void);

void KrtMemoryReportError(const char* error_type, const void* ptr, const char* file, int line, const char* format, ...);

bool KrtMemoryValidateHeap(void);
bool KrtMemoryCheckIntegrity(void);
void KrtMemoryScanCorruption(void);

#define KRT_SAFE_MALLOC(size) KrtSafeMalloc(size, __FILE__, __LINE__)
#define KRT_SAFE_CALLOC(count, size) KrtSafeCalloc(count, size, __FILE__, __LINE__)
#define KRT_SAFE_REALLOC(ptr, size) KrtSafeRealloc(ptr, size, __FILE__, __LINE__)
#define KRT_SAFE_STRDUP(str) KrtSafeStrdup(str, __FILE__, __LINE__)
#define KRT_SAFE_FREE(ptr) KrtSafeFree(ptr, __FILE__, __LINE__)

#define KRT_BOUNDS_CHECK(array, index, array_size) KrtBoundsCheck(array, index, sizeof(*(array)), array_size)

#define KRT_BUFFER_CHECK(buffer, offset, size, buffer_size) KrtBufferCheck(buffer, offset, size, buffer_size)

#define KRT_ARRAY_GET(array, index, array_size) (KRT_BOUNDS_CHECK(array, index, array_size) ? &((array)[index]) : NULL)

#define KRT_ARRAY_SET(array, index, value, array_size)                                                                 \
    do {                                                                                                               \
        if (KRT_BOUNDS_CHECK(array, index, array_size)) {                                                              \
            (array)[index] = (value);                                                                                  \
        } else {                                                                                                       \
            KrtMemoryReportError("ARRAY_BOUNDS", array, __FILE__, __LINE__, "Index %zu out of bounds [0, %zu)",        \
                                 (size_t)(index), (size_t)(array_size));                                               \
        }                                                                                                              \
    } while (0)

#define KRT_MEMORY_PTR_VALID(ptr) KrtMemoryPtrIsValid(ptr)
#define KRT_PTR_NOT_FREED(ptr) (!KrtPtrIsFreed(ptr))
#define KRT_PTR_NOT_DANGLING(ptr) (!KrtPtrIsDangling(ptr))

void KrtMemorySafetyEnablePoison(bool enable);
void KrtMemorySafetyEnableCanary(bool enable);
void KrtMemorySafetySetStrictMode(bool strict);

#endif
