#ifndef KRT_IR_MEMORY_H
#define KRT_IR_MEMORY_H

#include "../../../Core/Utils/KrtCommon.h"
#include <time.h>

#define KRT_DEFAULT_POOL_SIZE (4 * 1024)

typedef struct KrtIRMemoryPool {
    char* buffer;
    size_t size;
    size_t used;
    struct KrtIRMemoryPool* next;
} KrtIRMemoryPool;

typedef struct KrtIRMemoryArena {
    KrtIRMemoryPool* pools;
    KrtIRMemoryPool* current_pool;
    size_t pool_size;
    size_t total_allocated;
    size_t pool_count;
} KrtIRMemoryArena;

KrtIRMemoryArena* KrtIrArenaCreate(size_t pool_size);
void KrtIrArenaDestroy(KrtIRMemoryArena* arena);
void* KrtIrArenaAlloc(KrtIRMemoryArena* arena, size_t size);
char* KrtIrArenaStrdup(KrtIRMemoryArena* arena, const char* str);
void KrtIrArenaReset(KrtIRMemoryArena* arena);
void KrtIrArenaGetStats(KrtIRMemoryArena* arena, size_t* total_allocated, size_t* pool_count);

typedef struct KrtIRInst KrtIRInst;
typedef struct KrtIRBasicBlock KrtIRBasicBlock;
typedef struct KrtIRFunction KrtIRFunction;
typedef struct KrtIRType KrtIRType;
typedef struct KrtIRVarVersion KrtIRVarVersion;
typedef struct KrtIRPhi KrtIRPhi;

typedef enum {
    KRT_POOL_INST,
    KRT_POOL_BLOCK,
    KRT_POOL_VALUE,
    KRT_POOL_TYPE,
    KRT_POOL_VAR_VERSION,
    KRT_POOL_PHI,
    KRT_POOL_COUNT
} KrtPoolObjectType;

typedef struct KrtPoolNode {
    struct KrtPoolNode* next;
    _Alignas(max_align_t) char data[];
} KrtPoolNode;

typedef struct {
    const char* name;
    size_t object_size;
    size_t block_size;

    KrtPoolNode* free_list;
    KrtPoolNode* blocks;

    int alloc_count;
    int free_count;
    int hit_count;
    int miss_count;
} KrtIRObjectPool;

typedef struct {
    KrtIRObjectPool pools[KRT_POOL_COUNT];
    KrtIRMemoryArena* arena;
} KrtIRPoolManager;

void KrtIrPoolManagerInit(KrtIRPoolManager* manager, KrtIRMemoryArena* arena);
void KrtIrPoolManagerDestroy(KrtIRPoolManager* manager);
void* KrtIrPoolAlloc(KrtIRPoolManager* manager, KrtPoolObjectType type);
KrtIRInst* KrtIrPoolAllocInst(KrtIRPoolManager* manager);
KrtIRBasicBlock* KrtIrPoolAllocBlock(KrtIRPoolManager* manager);
KrtIRType* KrtIrPoolAllocType(KrtIRPoolManager* manager);
KrtIRVarVersion* KrtIrPoolAllocVarVersion(KrtIRPoolManager* manager);
KrtIRPhi* KrtIrPoolAllocPhi(KrtIRPoolManager* manager);
void KrtIrPoolFree(KrtIRPoolManager* manager, KrtPoolObjectType type, void* obj);
void KrtIrPoolFreeInst(KrtIRPoolManager* manager, KrtIRInst* inst);
void KrtIrPoolFreeBlock(KrtIRPoolManager* manager, KrtIRBasicBlock* block);
void KrtIrPoolFreeType(KrtIRPoolManager* manager, KrtIRType* type);
void KrtIrPoolFreeVarVersion(KrtIRPoolManager* manager, KrtIRVarVersion* var);
void KrtIrPoolFreePhi(KrtIRPoolManager* manager, KrtIRPhi* phi);
void KrtIrPoolClear(KrtIRPoolManager* manager, KrtPoolObjectType type);
void KrtIrPoolClearAll(KrtIRPoolManager* manager);
void KrtIrPoolDefrag(KrtIRPoolManager* manager, KrtPoolObjectType type);
void KrtIrPoolDefragAll(KrtIRPoolManager* manager);
int KrtIrPoolFragmentationRate(KrtIRPoolManager* manager, KrtPoolObjectType type);
void KrtIrPoolPrintStats(KrtIRPoolManager* manager);
int KrtIrPoolGetHitRate(KrtIRPoolManager* manager, KrtPoolObjectType type);

typedef enum { KRT_LAZY_OP_ALLOC, KRT_LAZY_OP_COPY, KRT_LAZY_OP_INIT, KRT_LAZY_OP_COUNT } KrtLazyOpType;

typedef struct KrtLazyOp {
    KrtLazyOpType type;
    void* target;
    void* source;
    size_t size;
    int processed;
    struct KrtLazyOp* next;
} KrtLazyOp;

typedef struct {
    KrtLazyOp* pending;
    KrtLazyOp* free_list;
    int pending_count;
    int processed_count;
    int coalesced_count;

    int batch_size;
    int enable_coalesce;
} KrtLazyAllocManager;

void KrtIrLazyInit(KrtLazyAllocManager* manager);
void KrtIrLazyDestroy(KrtLazyAllocManager* manager);
void KrtIrLazyRecordAlloc(KrtLazyAllocManager* manager, void** target, size_t size);
void KrtIrLazyRecordCopy(KrtLazyAllocManager* manager, void** target, void* source, size_t size);
void KrtIrLazyRecordInit(KrtLazyAllocManager* manager, void* target, size_t size);
void KrtIrLazyFlush(KrtLazyAllocManager* manager);
void KrtIrLazyFlushBatch(KrtLazyAllocManager* manager, int max_ops);
void KrtIrLazyCoalesceAllocs(KrtLazyAllocManager* manager);
bool KrtIrLazyHasPending(KrtLazyAllocManager* manager);
int KrtIrLazyPendingCount(KrtLazyAllocManager* manager);
void KrtIrLazyPrintStats(KrtLazyAllocManager* manager);

typedef enum {
    KRT_PROFILE_LEXER,
    KRT_PROFILE_PARSER,
    KRT_PROFILE_SEMANTIC,
    KRT_PROFILE_IR_GEN,
    KRT_PROFILE_IR_OPT,
    KRT_PROFILE_CODEGEN,
    KRT_PROFILE_LINKING,
    KRT_PROFILE_TOTAL,
    KRT_PROFILE_PHASE_COUNT
} KrtProfilePhase;

typedef struct {
    double times[KRT_PROFILE_PHASE_COUNT];
    int counts[KRT_PROFILE_PHASE_COUNT];
    clock_t start_times[KRT_PROFILE_PHASE_COUNT];
    int active[KRT_PROFILE_PHASE_COUNT];
} KrtIRProfiler;

typedef struct {
    size_t total_allocated;
    size_t total_freed;
    size_t current_used;
    size_t peak_used;
    size_t allocation_count;
    size_t free_count;
} KrtIRMemoryStats;

typedef struct {
    int inst_count;
    int block_count;
    int function_count;
    int var_count;

    int opt_passes;
    int opt_applied;

    int arena_allocs;
    int malloc_allocs;
    size_t arena_bytes;
} KrtIRPerformanceCounters;

extern KrtIRProfiler g_ir_profiler;
extern KrtIRMemoryStats g_ir_memory_stats;
extern KrtIRPerformanceCounters g_ir_counters;

void KrtIrProfilerInit(void);
void KrtIrProfilerReset(void);
void KrtIrProfilerPrint(void);
void KrtIrProfileBegin(KrtProfilePhase phase);
void KrtIrProfileEnd(KrtProfilePhase phase);
void KrtIrMemoryTrackAlloc(size_t size);
void KrtIrMemoryTrackFree(size_t size);
void KrtIrMemoryPrint(void);
void KrtIrCountersReset(void);
void KrtIrCountersPrint(void);

#ifdef ENABLE_IR_PROFILING
#define KRT_IR_PROFILE_BEGIN(phase) KrtIrProfileBegin(phase)
#define KRT_IR_PROFILE_END(phase) KrtIrProfileEnd(phase)
#define KRT_IR_TRACK_ALLOC(size) KrtIrMemoryTrackAlloc(size)
#define KRT_IR_TRACK_FREE(size) KrtIrMemoryTrackFree(size)
#else
#define KRT_IR_PROFILE_BEGIN(phase) ((void)0)
#define KRT_IR_PROFILE_END(phase) ((void)0)
#define KRT_IR_TRACK_ALLOC(size) ((void)0)
#define KRT_IR_TRACK_FREE(size) ((void)0)
#endif

#define KRT_IR_PROFILE_SCOPE(phase)                                                                                    \
    int _ir_profile_scope_done = (KrtIrProfileBegin(phase), 0);                                                        \
    for (; _ir_profile_scope_done == 0; KrtIrProfileEnd(phase), _ir_profile_scope_done = 1)

#endif
