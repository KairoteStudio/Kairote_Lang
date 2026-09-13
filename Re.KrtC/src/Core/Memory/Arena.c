#include "Arena.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "Size.h"

KrtArena* KrtArenaCreateLocal(size_t block_size) {
    KrtArena* arena = (KrtArena*)KRT_CALLOC(1, sizeof(KrtArena));
    if (!arena) {
        return NULL;
    }
    arena->block_size = block_size ? block_size : KRT_ARENA_DEFAULT_BLOCK_SIZE;
    return arena;
}

KrtArena* KrtArenaCreate(size_t block_size) {
    KrtArena* arena = KrtArenaCreateLocal(block_size);
    if (!arena) {
        return NULL;
    }
#ifdef _WIN32
    InitializeCriticalSection(&arena->mutex);
#else
    if (pthread_mutex_init(&arena->mutex, NULL) != 0) {
        KRT_FREE(arena);
        return NULL;
    }
#endif
    arena->has_mutex = 1;
    return arena;
}

void KrtArenaDestroy(KrtArena* arena) {
    if (!arena) {
        return;
    }

    KrtArenaBlock* current = arena->blocks;
    while (current) {
        KrtArenaBlock* next = current->next;
        KRT_FREE(current);
        current = next;
    }

    if (arena->has_mutex) {
#ifdef _WIN32
        DeleteCriticalSection(&arena->mutex);
#else
        pthread_mutex_destroy(&arena->mutex);
#endif
    }

    KRT_FREE(arena);
}

static KrtArenaBlock* arena_create_block(KrtArena* arena, size_t min_size) {
    size_t block_size = min_size > arena->block_size ? min_size : arena->block_size;
    if (block_size > SIZE_MAX - sizeof(KrtArenaBlock)) {
        return NULL;
    }
    size_t total_size = sizeof(KrtArenaBlock) + block_size;
    if (total_size > SIZE_MAX - arena->total_allocated) {
        return NULL;
    }
    KrtArenaBlock* block = (KrtArenaBlock*)KRT_MALLOC(total_size);
    if (!block) {
        return NULL;
    }
    block->next = NULL;
    block->size = block_size;
    block->used = 0;
    arena->total_allocated += total_size;
    return block;
}

void* KrtArenaAlloc(KrtArena* arena, size_t size) {
    size_t aligned_size;
    if (!arena || !size || !KrtSizeAlign(size, KRT_ARENA_ALIGNMENT, &aligned_size)) {
        return NULL;
    }
    KRT_ARENA_LOCK(arena);
    if (aligned_size > SIZE_MAX - arena->total_used) {
        KRT_ARENA_UNLOCK(arena);
        return NULL;
    }
    KrtArenaBlock* block = arena->current;
    /* Walk retained blocks only once per reset; full blocks never get rescanned. */
    while (block && aligned_size > block->size - block->used && block->next) {
        block = block->next;
    }
    if (!block || aligned_size > block->size - block->used) {
        KrtArenaBlock* next = arena_create_block(arena, aligned_size);
        if (!next) {
            KRT_ARENA_UNLOCK(arena);
            return NULL;
        }
        if (block) {
            block->next = next;
        } else {
            arena->blocks = next;
        }
        block = next;
    }
    arena->current = block;
    void* ptr = block->data + block->used;
    block->used += aligned_size;
    arena->total_used += aligned_size;
    arena->allocation_count++;
    KRT_ARENA_UNLOCK(arena);
    return ptr;
}

void* KrtArenaCalloc(KrtArena* arena, size_t count, size_t size) {
    size_t total_size;
    if (!KrtSizeMultiply(count, size, &total_size)) {
        return NULL;
    }
    void* ptr = KrtArenaAlloc(arena, total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

char* KrtArenaStrdup(KrtArena* arena, const char* str) {
    if (!str) {
        return NULL;
    }
    size_t size = strlen(str) + 1;
    char* result = KrtArenaAlloc(arena, size);
    if (result) {
        memcpy(result, str, size);
    }
    return result;
}

void* KrtArenaRealloc(KrtArena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (!arena) {
        return NULL;
    }
    if (!ptr) {
        return KrtArenaAlloc(arena, new_size);
    }
    if (new_size <= old_size) {
        return ptr;
    }
    size_t old_aligned, new_aligned;
    if (!KrtSizeAlign(old_size, KRT_ARENA_ALIGNMENT, &old_aligned) ||
        !KrtSizeAlign(new_size, KRT_ARENA_ALIGNMENT, &new_aligned)) {
        return NULL;
    }
    KRT_ARENA_LOCK(arena);
    KrtArenaBlock* block = arena->current;
    size_t extra = new_aligned - old_aligned;
    if (block && old_size && old_aligned <= block->used && ptr == block->data + block->used - old_aligned &&
        extra <= block->size - block->used && extra <= SIZE_MAX - arena->total_used) {
        block->used += extra;
        arena->total_used += extra;
        KRT_ARENA_UNLOCK(arena);
        return ptr;
    }
    KRT_ARENA_UNLOCK(arena);
    void* result = KrtArenaAlloc(arena, new_size);
    if (result) {
        memcpy(result, ptr, old_size);
    }
    return result;
}

void KrtArenaReset(KrtArena* arena) {
    if (!arena) {
        return;
    }

    KRT_ARENA_LOCK(arena);

    KrtArenaBlock* current = arena->blocks;
    while (current) {
        current->used = 0;
        current = current->next;
    }

    arena->current = arena->blocks;
    arena->total_used = 0;
    arena->allocation_count = 0;

    KRT_ARENA_UNLOCK(arena);
}

void KrtArenaStats(KrtArena* arena) {
    if (!arena) {
        return;
    }

    KRT_ARENA_LOCK(arena);

    size_t block_count = 0;
    KrtArenaBlock* current = arena->blocks;
    while (current) {
        block_count++;
        current = current->next;
    }

    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║              Arena Memory Statistics             ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Total Allocated:    %10zu bytes            ║\n", arena->total_allocated);
    printf("║ Total Used:         %10zu bytes            ║\n", arena->total_used);
    printf("║ Allocation Count:   %10zu                 ║\n", arena->allocation_count);
    printf("║ Block Count:        %10zu                 ║\n", block_count);
    printf("║ Block Size:         %10zu bytes            ║\n", arena->block_size);
    printf("║ Utilization:        %10.2f %%               ║\n",
           arena->total_allocated > 0 ? (double)arena->total_used / arena->total_allocated * 100.0 : 0.0);
    printf("╚══════════════════════════════════════════════════╝\n");

    KRT_ARENA_UNLOCK(arena);
}

size_t KrtArenaGetUsage(KrtArena* arena) {
    if (!arena) {
        return 0;
    }

    KRT_ARENA_LOCK(arena);
    size_t usage = arena->total_used;
    KRT_ARENA_UNLOCK(arena);

    return usage;
}

size_t KrtArenaGetBlockCount(KrtArena* arena) {
    if (!arena) {
        return 0;
    }

    KRT_ARENA_LOCK(arena);

    size_t block_count = 0;
    KrtArenaBlock* current = arena->blocks;
    while (current) {
        block_count++;
        current = current->next;
    }

    KRT_ARENA_UNLOCK(arena);

    return block_count;
}
