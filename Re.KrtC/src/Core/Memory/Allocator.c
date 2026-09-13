#include "../Utils/KrtCommon.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "Size.h"

#define KRT_MEMORY_MAGIC 0xDEADBEEF
#define KRT_CANARY_VALUE 0xCAFEBABE
#define KRT_POISON_VALUE 0xDD
#define KRT_CANARY_PREFIX _Alignof(max_align_t)

#ifdef _WIN32
static MemorySafetyManager memory_manager = {.poison_enabled = true, .canary_enabled = true};
static INIT_ONCE memory_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK initialize_memory(PINIT_ONCE once, PVOID parameter, PVOID* context) {
    (void)once;
    (void)parameter;
    (void)context;
    InitializeCriticalSection(&memory_manager.mutex);
    return TRUE;
}
#else
static MemorySafetyManager memory_manager = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .poison_enabled = true,
    .canary_enabled = true,
};
#endif
MemorySafetyManager* g_memory_safety = &memory_manager;

void KrtMemorySafetyInit(void) {
#ifdef _WIN32
    InitOnceExecuteOnce(&memory_once, initialize_memory, NULL, NULL);
#endif
}

static void memory_lock(void) {
    KrtMemorySafetyInit();
#ifdef _WIN32
    EnterCriticalSection(&g_memory_safety->mutex);
#else
    pthread_mutex_lock(&g_memory_safety->mutex);
#endif
}

static void memory_unlock(void) {
#ifdef _WIN32
    LeaveCriticalSection(&g_memory_safety->mutex);
#else
    pthread_mutex_unlock(&g_memory_safety->mutex);
#endif
}

static size_t pointer_hash(const void* ptr, size_t capacity) {
    uintptr_t value = (uintptr_t)ptr / _Alignof(max_align_t);
    value ^= value >> 16;
    value *= (uintptr_t)0x9e3779b1U;
    value ^= value >> 16;
    return (size_t)value & (capacity - 1);
}

/* Caller holds the manager lock; growth never invalidates MemoryBlock pointers. */
static bool grow_index(void) {
    size_t old_capacity = g_memory_safety->bucket_count;
    if (old_capacity && g_memory_safety->block_count < old_capacity - old_capacity / 4) {
        return true;
    }
    if (old_capacity > SIZE_MAX / 2 / sizeof(MemoryBlock*)) {
        return false;
    }
    size_t capacity = old_capacity ? old_capacity * 2 : 1024;
    MemoryBlock** buckets = __builtin_calloc(capacity, sizeof(*buckets));
    if (!buckets) {
        return false;
    }
    for (MemoryBlock* block = g_memory_safety->blocks; block; block = block->next) {
        size_t index = pointer_hash(block->user_ptr, capacity);
        block->hash_next = buckets[index];
        buckets[index] = block;
    }
    __builtin_free(g_memory_safety->buckets);
    g_memory_safety->buckets = buckets;
    g_memory_safety->bucket_count = capacity;
    return true;
}

static MemoryBlock* find_block(const void* ptr) {
    if (!g_memory_safety->bucket_count) {
        return NULL;
    }
    size_t index = pointer_hash(ptr, g_memory_safety->bucket_count);
    for (MemoryBlock* block = g_memory_safety->buckets[index]; block; block = block->hash_next) {
        if (block->user_ptr == ptr) {
            return block;
        }
    }
    return NULL;
}

static bool add_memory_block(MemoryBlock* block) {
    memory_lock();
    if (block->user_size > SIZE_MAX - g_memory_safety->total_used || !grow_index()) {
        memory_unlock();
        return false;
    }
    block->next = g_memory_safety->blocks;
    block->prev = NULL;
    if (block->next) {
        block->next->prev = block;
    }
    g_memory_safety->blocks = block;
    size_t index = pointer_hash(block->user_ptr, g_memory_safety->bucket_count);
    block->hash_next = g_memory_safety->buckets[index];
    g_memory_safety->buckets[index] = block;
    g_memory_safety->block_count++;
    g_memory_safety->total_used += block->user_size;
    memory_unlock();
    return true;
}

static void remove_memory_block(MemoryBlock* block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        g_memory_safety->blocks = block->next;
    }
    if (block->next) {
        block->next->prev = block->prev;
    }
    size_t index = pointer_hash(block->user_ptr, g_memory_safety->bucket_count);
    MemoryBlock** entry = &g_memory_safety->buckets[index];
    while (*entry != block) {
        entry = &(*entry)->hash_next;
    }
    *entry = block->hash_next;
    g_memory_safety->block_count--;
    g_memory_safety->total_used -= block->user_size;
}

/* Call after all users have stopped. The static manager can be reused afterwards. */
void KrtMemorySafetyCleanup(void) {
    memory_lock();
    MemoryBlock* current = g_memory_safety->blocks;
    while (current) {
        MemoryBlock* next = current->next;
        __builtin_free(current->actual_ptr);
        __builtin_free(current);
        current = next;
    }
    __builtin_free(g_memory_safety->buckets);
    g_memory_safety->buckets = NULL;
    g_memory_safety->bucket_count = 0;
    g_memory_safety->blocks = NULL;
    g_memory_safety->block_count = 0;
    g_memory_safety->total_used = 0;
    memory_unlock();
}

void* KrtSafeMalloc(size_t size, const char* file, int line) {
    if (size == 0) {
        return NULL;
    }

    size_t actual_size = size;
    if (g_memory_safety->canary_enabled) {
        if (size > SIZE_MAX - KRT_CANARY_PREFIX - sizeof(uint32_t)) {
            return NULL;
        }
        actual_size += KRT_CANARY_PREFIX + sizeof(uint32_t);
    }

    void* actual_ptr = __builtin_malloc(actual_size);
    if (!actual_ptr) {
        KrtMemoryReportError("MALLOC_FAILED", NULL, file, line, "Failed to allocate %zu bytes", size);
        return NULL;
    }

    MemoryBlock* block = __builtin_malloc(sizeof(MemoryBlock));
    if (!block) {
        __builtin_free(actual_ptr);
        return NULL;
    }

    void* user_ptr = actual_ptr;
    if (g_memory_safety->canary_enabled) {
        user_ptr = (char*)actual_ptr + KRT_CANARY_PREFIX;
    }

    block->actual_ptr = actual_ptr;
    block->user_ptr = user_ptr;
    block->user_size = size;
    block->actual_size = actual_size;
    block->magic = KRT_MEMORY_MAGIC;
    block->protection = MEM_PROTECT_READ | MEM_PROTECT_WRITE;
    block->is_freed = false;
    block->has_canary = g_memory_safety->canary_enabled;
    block->file = file;
    block->line = line;

    if (g_memory_safety->canary_enabled) {
        KrtSetCanary(block);
    }

    if (!add_memory_block(block)) {
        __builtin_free(actual_ptr);
        __builtin_free(block);
        return NULL;
    }

    return user_ptr;
}

void* KrtSafeCalloc(size_t count, size_t size, const char* file, int line) {
    if (count == 0 || size == 0) {
        return NULL;
    }

    size_t total_size;
    if (!KrtSizeMultiply(count, size, &total_size)) {
        return NULL;
    }
    void* ptr = KrtSafeMalloc(total_size, file, line);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

void* KrtSafeRealloc(void* old_ptr, size_t new_size, const char* file, int line) {
    if (!old_ptr) {
        return KrtSafeMalloc(new_size, file, line);
    }

    if (new_size == 0) {
        KrtSafeFree(old_ptr, file, line);
        return NULL;
    }

    MemoryBlock* old_block = KrtPtrGetBlock(old_ptr);
    if (!old_block) {
        KrtMemoryReportError("INVALID_REALLOC", old_ptr, file, line, "Attempt to realloc untracked pointer %p",
                             old_ptr);
        return NULL;
    }

    void* new_ptr = KrtSafeMalloc(new_size, file, line);
    if (!new_ptr) {
        return NULL;
    }

    size_t copy_size = (old_block->user_size < new_size) ? old_block->user_size : new_size;
    memcpy(new_ptr, old_ptr, copy_size);

    if (new_size > copy_size) {
        memset((char*)new_ptr + copy_size, 0, new_size - copy_size);
    }

    KrtSafeFree(old_ptr, file, line);

    return new_ptr;
}

char* KrtSafeStrdup(const char* str, const char* file, int line) {
    if (!str) {
        return NULL;
    }

    size_t len = strlen(str);
    char* copy = (char*)KrtSafeMalloc(len + 1, file, line);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

void KrtSafeFree(void* ptr, const char* file, int line) {
    if (!ptr) {
        return;
    }

    memory_lock();
    MemoryBlock* block = find_block(ptr);
    if (!block) {
        memory_unlock();
        KrtMemoryReportError("INVALID_FREE", ptr, file, line, "Attempt to free untracked pointer %p", ptr);
        return;
    }
    remove_memory_block(block);
    memory_unlock();

    if (!KrtCheckCanary(block)) {
        KrtMemoryReportError("CANARY_CORRUPTED", ptr, file, line, "Memory canary corrupted for block %p", ptr);
    }

    if (KrtMemoryIsPoisoned(ptr, block->user_size)) {
        KrtMemoryReportError("USE_AFTER_FREE", ptr, file, line, "Use after free detected for block %p", ptr);
    }

    block->is_freed = true;

    if (g_memory_safety->poison_enabled) {
        KrtMemoryPoison(ptr, block->user_size);
    }

    __builtin_free(block->actual_ptr);
    __builtin_free(block);
}

bool KrtBoundsCheck(const void* array, size_t index, size_t element_size, size_t array_size) {
    if (!array || element_size == 0) {
        return false;
    }

    if (index >= array_size) {
        return false;
    }

    return true;
}

bool KrtBufferCheck(const void* buffer, size_t offset, size_t size, size_t buffer_size) {
    if (!buffer || size == 0) {
        return false;
    }

    if (offset > buffer_size || size > buffer_size - offset) {
        return false;
    }

    return true;
}

bool KrtPtrCheckDoubleFree(const void* ptr, const char* file, int line) {
    if (!ptr) {
        return false;
    }

    MemoryBlock* block = KrtPtrGetBlock(ptr);
    if (block && block->is_freed) {
        KrtMemoryReportError("DOUBLE_FREE", ptr, file, line, "Double free detected for block %p", ptr);
        return true;
    }

    return false;
}

MemoryBlock* KrtPtrGetBlock(const void* ptr) {
    if (!ptr) {
        return NULL;
    }
    memory_lock();
    MemoryBlock* block = find_block(ptr);
    memory_unlock();
    return block;
}

bool KrtMemoryPtrIsValid(const void* ptr) {
    if (!ptr) {
        return false;
    }

    MemoryBlock* block = KrtPtrGetBlock(ptr);
    if (!block) {
        return false;
    }

    if (block->magic != KRT_MEMORY_MAGIC) {
        return false;
    }
    if (block->is_freed) {
        return false;
    }
    if (!KrtCheckCanary(block)) {
        return false;
    }

    return true;
}

void KrtSetCanary(MemoryBlock* block) {
    if (!block || !block->has_canary) {
        return;
    }

    uint32_t* front_canary = (uint32_t*)block->actual_ptr;
    *front_canary = KRT_CANARY_VALUE;

    uint32_t back_canary_value = KRT_CANARY_VALUE;
    memcpy((char*)block->user_ptr + block->user_size, &back_canary_value, sizeof(uint32_t));
}

bool KrtCheckCanary(const MemoryBlock* block) {
    if (!block || !block->has_canary) {
        return true;
    }

    uint32_t front_canary = *(uint32_t*)block->actual_ptr;
    uint32_t back_canary;
    memcpy(&back_canary, (char*)block->user_ptr + block->user_size, sizeof(uint32_t));

    return front_canary == KRT_CANARY_VALUE && back_canary == KRT_CANARY_VALUE;
}

void KrtMemoryPoison(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return;
    }
    memset(ptr, KRT_POISON_VALUE, size);
}

bool KrtMemoryIsPoisoned(const void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return false;
    }

    const unsigned char* bytes = (const unsigned char*)ptr;
    for (size_t i = 0; i < size; i++) {
        if (bytes[i] != KRT_POISON_VALUE) {
            return false;
        }
    }
    return true;
}

void KrtMemoryReportError(const char* error_type, const void* ptr, const char* file, int line, const char* format,
                          ...) {
    (void)error_type;
    (void)ptr;
    (void)file;
    (void)line;
    (void)format;
#ifdef KRT_DEBUG
    __debugbreak();
#endif
}

void KrtMemoryDumpBlocks(void) {
    if (!g_memory_safety) {
        return;
    }

#ifdef _WIN32
    EnterCriticalSection(&g_memory_safety->mutex);
#else
    pthread_mutex_lock(&g_memory_safety->mutex);
#endif

    MemoryBlock* current = g_memory_safety->blocks;
    while (current) {
        current = current->next;
    }

#ifdef _WIN32
    LeaveCriticalSection(&g_memory_safety->mutex);
#else
    pthread_mutex_unlock(&g_memory_safety->mutex);
#endif
}

size_t KrtMemoryGetTotalUsage(void) {
    memory_lock();
    size_t total = g_memory_safety->total_used;
    memory_unlock();
    return total;
}

size_t KrtMemoryGetBlockCount(void) {
    memory_lock();
    size_t count = g_memory_safety->block_count;
    memory_unlock();
    return count;
}

void KrtMemoryDumpStats(void) {
}
