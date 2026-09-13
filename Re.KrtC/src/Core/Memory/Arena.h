#ifndef KRT_ARENA_H
#define KRT_ARENA_H

#include "../Utils/KrtCommon.h"
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#define KRT_ARENA_DEFAULT_BLOCK_SIZE (64 * 1024)
#define KRT_ARENA_ALIGNMENT _Alignof(max_align_t)

typedef struct KrtArenaBlock {
    struct KrtArenaBlock* next;
    size_t size;
    size_t used;
    _Alignas(max_align_t) uint8_t data[];
} KrtArenaBlock;

typedef struct {
    KrtArenaBlock* blocks;
    KrtArenaBlock* current;
    size_t block_size;
    size_t total_allocated;
    size_t total_used;
    size_t allocation_count;
#ifdef _WIN32
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
    int has_mutex;
} KrtArena;

KrtArena* KrtArenaCreate(size_t block_size);
/** Create a thread-confined arena without locking; return NULL if allocation fails. */
KrtArena* KrtArenaCreateLocal(size_t block_size);
void KrtArenaDestroy(KrtArena* arena);

void* KrtArenaAlloc(KrtArena* arena, size_t size);
void* KrtArenaCalloc(KrtArena* arena, size_t count, size_t size);
void* KrtArenaRealloc(KrtArena* arena, void* ptr, size_t old_size, size_t new_size);
/** Copy str into arena-owned storage; return NULL for invalid input or allocation failure. */
char* KrtArenaStrdup(KrtArena* arena, const char* str);

void KrtArenaReset(KrtArena* arena);
void KrtArenaStats(KrtArena* arena);

size_t KrtArenaGetUsage(KrtArena* arena);
size_t KrtArenaGetBlockCount(KrtArena* arena);

#ifdef _WIN32
#define KRT_ARENA_LOCK(arena)                                                                                          \
    do {                                                                                                               \
        if ((arena)->has_mutex)                                                                                        \
            EnterCriticalSection(&(arena)->mutex);                                                                     \
    } while (0)
#define KRT_ARENA_UNLOCK(arena)                                                                                        \
    do {                                                                                                               \
        if ((arena)->has_mutex)                                                                                        \
            LeaveCriticalSection(&(arena)->mutex);                                                                     \
    } while (0)
#else
#define KRT_ARENA_LOCK(arena)                                                                                          \
    do {                                                                                                               \
        if ((arena)->has_mutex)                                                                                        \
            pthread_mutex_lock(&(arena)->mutex);                                                                       \
    } while (0)
#define KRT_ARENA_UNLOCK(arena)                                                                                        \
    do {                                                                                                               \
        if ((arena)->has_mutex)                                                                                        \
            pthread_mutex_unlock(&(arena)->mutex);                                                                     \
    } while (0)
#endif

#endif
