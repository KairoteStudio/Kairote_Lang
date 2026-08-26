#include "../Utils/KrtCommon.h"
#include <string.h>
#include <assert.h>
#include <stdlib.h>

static struct {
    atomic_size_t total_shared;
    atomic_size_t total_unique;
    atomic_size_t total_weak;
    atomic_size_t total_allocations;
    atomic_size_t total_deallocations;
} g_smart_ptr_stats = {0};

SmartPtr* KrtMakeShared(const char* type_name, size_t size) {
    if (size == 0) return NULL;

    SharedControlBlock* control = KRT_MALLOC(sizeof(SharedControlBlock));
    if (!control) return NULL;

    void* data = KRT_MALLOC(size);
    if (!data) {
        KRT_FREE(control);
        return NULL;
    }

    atomic_init(&control->ref_count, 1);
    atomic_init(&control->weak_count, 0);
    control->deleter = NULL;
    control->data = data;
    control->is_array = false;

    SmartPtr* ptr = KRT_MALLOC(sizeof(SmartPtr));
    if (!ptr) {
        KRT_FREE(data);
        KRT_FREE(control);
        return NULL;
    }

    ptr->ptr = data;
    ptr->control = control;
    ptr->type = PTR_SHARED;
    ptr->type_name = type_name;

    atomic_fetch_add(&g_smart_ptr_stats.total_shared, 1);
    atomic_fetch_add(&g_smart_ptr_stats.total_allocations, 1);

    return ptr;
}

SmartPtr* KrtMakeSharedArray(const char* type_name, size_t count, size_t element_size) {
    if (count == 0 || element_size == 0) return NULL;

    SmartPtr* ptr = KrtMakeShared(type_name, count * element_size);
    if (ptr && ptr->control) {
        ptr->control->is_array = true;
    }
    return ptr;
}

SmartPtr* KrtMakeUnique(const char* type_name, size_t size) {
    if (size == 0) return NULL;

    void* data = KRT_MALLOC(size);
    if (!data) return NULL;

    SmartPtr* ptr = KRT_MALLOC(sizeof(SmartPtr));
    if (!ptr) {
        KRT_FREE(data);
        return NULL;
    }

    ptr->ptr = data;
    ptr->control = NULL;
    ptr->type = PTR_UNIQUE;
    ptr->type_name = type_name;

    atomic_fetch_add(&g_smart_ptr_stats.total_unique, 1);
    atomic_fetch_add(&g_smart_ptr_stats.total_allocations, 1);

    return ptr;
}

SmartPtr* KrtMakeUniqueArray(const char* type_name, size_t count, size_t element_size) {
    if (count == 0 || element_size == 0) return NULL;
    return KrtMakeUnique(type_name, count * element_size);
}

void KrtSharedAcquire(SmartPtr* ptr) {
    if (!ptr || ptr->type != PTR_SHARED || !ptr->control) return;

    atomic_fetch_add(&ptr->control->ref_count, 1);
}

void KrtSharedRelease(SmartPtr* ptr) {
    if (!ptr || ptr->type != PTR_SHARED || !ptr->control) return;

    int old_count = atomic_fetch_sub(&ptr->control->ref_count, 1);

    if (old_count == 1) {

        void* data = ptr->control->data;

        if (ptr->control->deleter) {
            ptr->control->deleter(data);
        } else {
            KRT_FREE(data);
        }

        if (atomic_load(&ptr->control->weak_count) > 0) {
            ptr->control->data = NULL;
        } else {
            KRT_FREE(ptr->control);
            ptr->control = NULL;
        }

        atomic_fetch_sub(&g_smart_ptr_stats.total_shared, 1);
        atomic_fetch_add(&g_smart_ptr_stats.total_deallocations, 1);
    }
}

int KrtSharedCount(const SmartPtr* ptr) {
    if (!ptr || ptr->type != PTR_SHARED || !ptr->control) return 0;
    return atomic_load(&ptr->control->ref_count);
}

void* KrtPtrGet(const SmartPtr* ptr) {
    if (!ptr || !KrtPtrIsValid(ptr)) return NULL;
    return ptr->ptr;
}

void* KrtPtrGetChecked(const SmartPtr* ptr, const char* file, int line) {
    if (!ptr) {
        KrtError("%s:%d: Null smart pointer\n", file, line);
        return NULL;
    }

    if (!KrtPtrIsValid(ptr)) {
        KrtError("%s:%d: Invalid smart pointer (type: %d, name: %s)\n",
                file, line, ptr->type, ptr->type_name ? ptr->type_name : "unknown");
        return NULL;
    }

    return ptr->ptr;
}

bool KrtPtrIsValid(const SmartPtr* ptr) {
    if (!ptr) return false;

    switch (ptr->type) {
        case PTR_SHARED:
            return ptr->control && ptr->control->data &&
                   atomic_load(&ptr->control->ref_count) > 0;

        case PTR_UNIQUE:
            return ptr->ptr != NULL;

        case PTR_WEAK:
            return ptr->control &&
                   atomic_load(&ptr->control->ref_count) > 0;

        default:
            return false;
    }
}

bool KrtPtrIsNull(const SmartPtr* ptr) {
    return !ptr || !KrtPtrIsValid(ptr) || ptr->ptr == NULL;
}

static void KrtPtrCleanupInternal(SmartPtr* ptr) {
    if (!ptr) return;

    switch (ptr->type) {
        case PTR_SHARED:
            KrtSharedRelease(ptr);
            break;

        case PTR_UNIQUE:
            if (ptr->ptr) {
                KRT_FREE(ptr->ptr);
                atomic_fetch_sub(&g_smart_ptr_stats.total_unique, 1);
                atomic_fetch_add(&g_smart_ptr_stats.total_deallocations, 1);
            }
            break;

        case PTR_WEAK:
            if (ptr->control) {
                atomic_fetch_sub(&ptr->control->weak_count, 1);

                if (atomic_load(&ptr->control->weak_count) == 0 &&
                    atomic_load(&ptr->control->ref_count) == 0) {
                    KRT_FREE(ptr->control);
                }
            }
            atomic_fetch_sub(&g_smart_ptr_stats.total_weak, 1);
            break;
    }
}

void KrtPtrDestroy(SmartPtr* ptr) {
    if (!ptr) return;
    KrtPtrCleanupInternal(ptr);
    KRT_FREE(ptr);
}

void KrtPtrReset(SmartPtr* ptr, void* new_ptr, size_t size __attribute__((unused))) {
    if (!ptr) return;

    KrtPtrCleanupInternal(ptr);

    ptr->ptr = new_ptr;
    ptr->control = NULL;
    ptr->type = PTR_UNIQUE;
    ptr->type_name = "reset";
}

SmartPtr* KrtMakeWeak(const SmartPtr* shared_ptr) {
    if (!shared_ptr || shared_ptr->type != PTR_SHARED || !shared_ptr->control) {
        return NULL;
    }

    SmartPtr* weak = KRT_MALLOC(sizeof(SmartPtr));
    if (!weak) return NULL;

    weak->ptr = NULL;
    weak->control = shared_ptr->control;
    weak->type = PTR_WEAK;
    weak->type_name = shared_ptr->type_name;

    atomic_fetch_add(&weak->control->weak_count, 1);
    atomic_fetch_add(&g_smart_ptr_stats.total_weak, 1);

    return weak;
}

SmartPtr* KrtWeakLock(const SmartPtr* weak_ptr) {
    if (!weak_ptr || weak_ptr->type != PTR_WEAK || !weak_ptr->control) {
        return NULL;
    }

    int current_count = atomic_load(&weak_ptr->control->ref_count);
    while (current_count > 0) {
        if (atomic_compare_exchange_weak(&weak_ptr->control->ref_count,
                                        &current_count, current_count + 1)) {

            SmartPtr* shared = KRT_MALLOC(sizeof(SmartPtr));
            if (!shared) {
                atomic_fetch_sub(&weak_ptr->control->ref_count, 1);
                return NULL;
            }

            shared->ptr = weak_ptr->control->data;
            shared->control = weak_ptr->control;
            shared->type = PTR_SHARED;
            shared->type_name = weak_ptr->type_name;

            atomic_fetch_add(&g_smart_ptr_stats.total_shared, 1);
            return shared;
        }
    }

    return NULL;
}

bool KrtWeakExpired(const SmartPtr* weak_ptr) {
    if (!weak_ptr || weak_ptr->type != PTR_WEAK || !weak_ptr->control) {
        return true;
    }

    return atomic_load(&weak_ptr->control->ref_count) == 0;
}

void KrtPtrDumpStats(void) {
    printf("=== Smart Pointer Statistics ===\n");
    printf("Active Shared Pointers: %zu\n", g_smart_ptr_stats.total_shared);
    printf("Active Unique Pointers: %zu\n", g_smart_ptr_stats.total_unique);
    printf("Active Weak Pointers: %zu\n", g_smart_ptr_stats.total_weak);
    printf("Total Allocations: %zu\n", g_smart_ptr_stats.total_allocations);
    printf("Total Deallocations: %zu\n", g_smart_ptr_stats.total_deallocations);
    printf("Potential Leaks: %zu\n",
           g_smart_ptr_stats.total_allocations - g_smart_ptr_stats.total_deallocations);
    printf("================================\n");
}

size_t KrtPtrGetTotalSharedCount(void) {
    return g_smart_ptr_stats.total_shared;
}

size_t KrtPtrGetTotalUniqueCount(void) {
    return g_smart_ptr_stats.total_unique;
}
