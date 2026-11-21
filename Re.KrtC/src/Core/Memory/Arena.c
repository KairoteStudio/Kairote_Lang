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

void* KrtArenaRealloc(KrtArena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (!arena) {
        return NULL;
    }
    if (!ptr) {
        return KrtArenaAlloc(arena, new_size);
    }
    /* Shrinking is a no-op by design: arena blocks are never split, so the
     * caller keeps the original (larger) allocation. */
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
