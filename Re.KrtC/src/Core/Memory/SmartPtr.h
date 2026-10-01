#ifndef KRT_SMART_PTR_H
#define KRT_SMART_PTR_H

#include "../Utils/Logger.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum { PTR_UNIQUE, PTR_SHARED, PTR_WEAK } SmartPtrType;

typedef struct SharedControlBlock {
    atomic_int ref_count;
    atomic_int weak_count;
    void (*deleter)(void*);
    void* data;
    bool is_array;
} SharedControlBlock;

typedef struct SmartPtr {
    void* ptr;
    SharedControlBlock* control;
    SmartPtrType type;
    const char* type_name;
} SmartPtr;

SmartPtr* KrtMakeShared(const char* type_name, size_t size);
SmartPtr* KrtMakeSharedArray(const char* type_name, size_t count, size_t element_size);
SmartPtr* KrtMakeUnique(const char* type_name, size_t size);
SmartPtr* KrtMakeUniqueArray(const char* type_name, size_t count, size_t element_size);

void KrtSharedAcquire(SmartPtr* ptr);
void KrtSharedRelease(SmartPtr* ptr);
int KrtSharedCount(const SmartPtr* ptr);

void* KrtPtrGet(const SmartPtr* ptr);
void* KrtPtrGetChecked(const SmartPtr* ptr, const char* file, int line);

bool KrtPtrIsValid(const SmartPtr* ptr);
bool KrtPtrIsNull(const SmartPtr* ptr);

void KrtPtrDestroy(SmartPtr* ptr);
void KrtPtrReset(SmartPtr* ptr, void* new_ptr, size_t size);

SmartPtr* KrtMakeWeak(const SmartPtr* shared_ptr);
SmartPtr* KrtWeakLock(const SmartPtr* weak_ptr);
bool KrtWeakExpired(const SmartPtr* weak_ptr);

SmartPtr* KrtMakeSharedPooled(const char* type_name, size_t size, const char* pool_name);

void KrtPtrDumpStats(void);
size_t KrtPtrGetTotalSharedCount(void);
size_t KrtPtrGetTotalUniqueCount(void);

#define KRT_MAKE_SHARED(type) KrtMakeShared(#type, sizeof(type))
#define KRT_MAKE_SHARED_ARRAY(type, count) KrtMakeSharedArray(#type, count, sizeof(type))
#define KRT_MAKE_UNIQUE(type) KrtMakeUnique(#type, sizeof(type))
#define KRT_MAKE_UNIQUE_ARRAY(type, count) KrtMakeUniqueArray(#type, count, sizeof(type))

#define KRT_PTR_GET(ptr) KrtPtrGetChecked(ptr, __FILE__, __LINE__)
#define KRT_PTR_VALID(ptr) (KrtPtrIsValid(ptr) && !KrtPtrIsNull(ptr))

#define KRT_PTR_AUTO_DESTROY __attribute__((cleanup(KrtPtrDestroy)))

#endif
