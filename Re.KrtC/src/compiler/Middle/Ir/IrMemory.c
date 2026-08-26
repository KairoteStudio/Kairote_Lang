#include "IrSsa.h"
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#define MIN_ALIGNMENT 8

static size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

static KrtIRMemoryPool* create_pool(size_t size) {
    KrtIRMemoryPool* pool = (KrtIRMemoryPool*)KRT_MALLOC(sizeof(KrtIRMemoryPool));
    if (!pool) return NULL;
    
    pool->buffer = (char*)KRT_MALLOC(size);
    if (!pool->buffer) {
        KRT_FREE(pool);
        return NULL;
    }
    
    pool->size = size;
    pool->used = 0;
    pool->next = NULL;
    
    return pool;
}

static void destroy_pool(KrtIRMemoryPool* pool) {
    if (!pool) return;
    
    if (pool->buffer) {
        KRT_FREE(pool->buffer);
    }
    KRT_FREE(pool);
}

KrtIRMemoryArena* KrtIrArenaCreate(size_t pool_size) {
    KrtIRMemoryArena* arena = (KrtIRMemoryArena*)KRT_MALLOC(sizeof(KrtIRMemoryArena));
    if (!arena) return NULL;
    
    if (pool_size == 0) {
        pool_size = DEFAULT_POOL_SIZE;
    }
    
    arena->current_pool = create_pool(pool_size);
    if (!arena->current_pool) {
        KRT_FREE(arena);
        return NULL;
    }
    
    arena->pool_size = pool_size;
    arena->total_allocated = 0;
    arena->pool_count = 1;
    
    return arena;
}

void KrtIrArenaDestroy(KrtIRMemoryArena* arena) {
    if (!arena) return;
    
    KrtIRMemoryPool* pool = arena->current_pool;
    while (pool) {
        KrtIRMemoryPool* next = pool->next;
        destroy_pool(pool);
        pool = next;
    }
    
    KRT_FREE(arena);
}

void* KrtIrArenaAlloc(KrtIRMemoryArena* arena, size_t size) {
    if (!arena || size == 0) return NULL;
    
    size = align_size(size, MIN_ALIGNMENT);
    
    KrtIRMemoryPool* pool = arena->current_pool;
    
    if (pool->used + size > pool->size) {
        KrtIRMemoryPool* new_pool = create_pool(arena->pool_size > size ? arena->pool_size : size * 2);
        if (!new_pool) return NULL;
        
        new_pool->next = pool;
        arena->current_pool = new_pool;
        arena->pool_count++;
        pool = new_pool;
    }
    
    void* result = pool->buffer + pool->used;
    pool->used += size;
    arena->total_allocated += size;
    
    return result;
}

char* KrtIrArenaStrdup(KrtIRMemoryArena* arena, const char* str) {
    if (!arena || !str) return NULL;
    
    size_t len = strlen(str);
    char* result = (char*)KrtIrArenaAlloc(arena, len + 1);
    if (!result) return NULL;
    
    memcpy(result, str, len + 1);
    return result;
}

void KrtIrArenaReset(KrtIRMemoryArena* arena) {
    if (!arena) return;
    
    KrtIRMemoryPool* pool = arena->current_pool;
    while (pool) {
        KrtIRMemoryPool* next = pool->next;
        if (pool != arena->current_pool) {
            destroy_pool(pool);
        } else {
            pool->used = 0;
            pool->next = NULL;
        }
        pool = next;
    }
    
    arena->pool_count = 1;
    arena->total_allocated = 0;
}

void KrtIrArenaGetStats(KrtIRMemoryArena* arena, size_t* total_allocated, size_t* pool_count) {
    if (!arena) return;
    
    if (total_allocated) {
        *total_allocated = arena->total_allocated;
    }
    
    if (pool_count) {
        *pool_count = arena->pool_count;
    }
}

const size_t g_object_sizes[KRT_POOL_COUNT] = {
    sizeof(KrtIRInst),         
    sizeof(KrtIRBasicBlock),   
    sizeof(KrtIRValue),        
    sizeof(KrtIRType),         
    sizeof(KrtIRVarVersion),   
    sizeof(KrtIRPhi),          
};

const char* g_pool_names[KRT_POOL_COUNT] = {
    "Instruction ",
    "BasicBlock  ",
    "Value       ",
    "Type        ",
    "VarVersion  ",
    "Phi         ",
};

static void pool_init(KrtIRObjectPool* pool, const char* name, size_t object_size, size_t block_size) {
    pool->name = name;
    pool->object_size = object_size;
    pool->block_size = block_size;
    pool->free_list = NULL;
    pool->blocks = NULL;
    pool->alloc_count = 0;
    pool->free_count = 0;
    pool->hit_count = 0;
    pool->miss_count = 0;
}

static __attribute__((unused)) void* pool_alloc(KrtIRObjectPool* pool) {
    if (!pool) return NULL;
    
    pool->alloc_count++;
    
    if (pool->free_list) {
        KrtPoolNode* node = pool->free_list;
        pool->free_list = node->next;
        pool->hit_count++;
        
        memset(node->data, 0, pool->object_size);
        return node->data;
    }
    
    pool->miss_count++;
    return NULL;
}

static void pool_grow(KrtIRObjectPool* pool, KrtIRMemoryArena* arena) {
    if (!pool || !arena) return;
    
    size_t node_size = sizeof(KrtPoolNode) + pool->object_size;
    size_t block_mem_size = pool->block_size * node_size;
    char* block_mem = (char*)KrtIrArenaAlloc(arena, block_mem_size);
    if (!block_mem) return;
    
    for (size_t i = 0; i < pool->block_size; i++) {
        KrtPoolNode* node = (KrtPoolNode*)(block_mem + i * node_size);
        node->next = pool->free_list;
        pool->free_list = node;
    }
}

void KrtIrPoolManagerInit(KrtIRPoolManager* manager, KrtIRMemoryArena* arena) {
    if (!manager) return;
    
    memset(manager, 0, sizeof(KrtIRPoolManager));
    manager->arena = arena;
    
    for (int i = 0; i < KRT_POOL_COUNT; i++) {
        pool_init(&manager->pools[i], g_pool_names[i], g_object_sizes[i], 64);
    }
}

void KrtIrPoolManagerDestroy(KrtIRPoolManager* manager) {
    if (!manager) return;
    
    for (int i = 0; i < KRT_POOL_COUNT; i++) {
        manager->pools[i].free_list = NULL;
        manager->pools[i].blocks = NULL;
    }
}

void* KrtIrPoolAlloc(KrtIRPoolManager* manager, KrtPoolObjectType type) {
    if (!manager || type < 0 || type >= KRT_POOL_COUNT) return NULL;
    
    KrtIRObjectPool* pool = &manager->pools[type];
    pool->alloc_count++;
    
    if (pool->free_list) {
        KrtPoolNode* node = pool->free_list;
        pool->free_list = node->next;
        pool->hit_count++;
        
        memset(node->data, 0, pool->object_size);
        return node->data;
    }
    
    pool->miss_count++;
    pool_grow(pool, manager->arena);
    
    if (pool->free_list) {
        KrtPoolNode* node = pool->free_list;
        pool->free_list = node->next;
        memset(node->data, 0, pool->object_size);
        return node->data;
    }
    
    return NULL;
}

KrtIRInst* KrtIrPoolAllocInst(KrtIRPoolManager* manager) {
    return (KrtIRInst*)KrtIrPoolAlloc(manager, KRT_POOL_INST);
}

KrtIRBasicBlock* KrtIrPoolAllocBlock(KrtIRPoolManager* manager) {
    return (KrtIRBasicBlock*)KrtIrPoolAlloc(manager, KRT_POOL_BLOCK);
}

KrtIRType* KrtIrPoolAllocType(KrtIRPoolManager* manager) {
    return (KrtIRType*)KrtIrPoolAlloc(manager, KRT_POOL_TYPE);
}

KrtIRVarVersion* KrtIrPoolAllocVarVersion(KrtIRPoolManager* manager) {
    return (KrtIRVarVersion*)KrtIrPoolAlloc(manager, KRT_POOL_VAR_VERSION);
}

KrtIRPhi* KrtIrPoolAllocPhi(KrtIRPoolManager* manager) {
    return (KrtIRPhi*)KrtIrPoolAlloc(manager, KRT_POOL_PHI);
}

void KrtIrPoolFree(KrtIRPoolManager* manager, KrtPoolObjectType type, void* obj) {
    if (!manager || !obj || type < 0 || type >= KRT_POOL_COUNT) return;
    
    KrtIRObjectPool* pool = &manager->pools[type];
    pool->free_count++;
    
    KrtPoolNode* node = (KrtPoolNode*)((char*)obj - sizeof(KrtPoolNode));
    
    node->next = pool->free_list;
    pool->free_list = node;
}

void KrtIrPoolFreeInst(KrtIRPoolManager* manager, KrtIRInst* inst) {
    KrtIrPoolFree(manager, KRT_POOL_INST, inst);
}

void KrtIrPoolFreeBlock(KrtIRPoolManager* manager, KrtIRBasicBlock* block) {
    KrtIrPoolFree(manager, KRT_POOL_BLOCK, block);
}

void KrtIrPoolFreeType(KrtIRPoolManager* manager, KrtIRType* type) {
    KrtIrPoolFree(manager, KRT_POOL_TYPE, type);
}

void KrtIrPoolFreeVarVersion(KrtIRPoolManager* manager, KrtIRVarVersion* var) {
    KrtIrPoolFree(manager, KRT_POOL_VAR_VERSION, var);
}

void KrtIrPoolFreePhi(KrtIRPoolManager* manager, KrtIRPhi* phi) {
    KrtIrPoolFree(manager, KRT_POOL_PHI, phi);
}

void KrtIrPoolClear(KrtIRPoolManager* manager, KrtPoolObjectType type) {
    if (!manager || type < 0 || type >= KRT_POOL_COUNT) return;
    
    KrtIRObjectPool* pool = &manager->pools[type];
    pool->free_list = NULL;
}

void KrtIrPoolClearAll(KrtIRPoolManager* manager) {
    if (!manager) return;
    
    for (int i = 0; i < KRT_POOL_COUNT; i++) {
        KrtIrPoolClear(manager, (KrtPoolObjectType)i);
    }
}

static int count_pool_objects(KrtIRObjectPool* pool) {
    int count = 0;
    KrtPoolNode* node = pool->free_list;
    while (node) {
        count++;
        node = node->next;
    }
    return count;
}

void KrtIrPoolDefrag(KrtIRPoolManager* manager, KrtPoolObjectType type) {
    if (!manager || type < 0 || type >= KRT_POOL_COUNT) return;
    
    KrtIRObjectPool* pool = &manager->pools[type];
    
    int free_count = count_pool_objects(pool);
    if (free_count < 2) return;  
    
    (void)free_count;
}

void KrtIrPoolDefragAll(KrtIRPoolManager* manager) {
    if (!manager) return;
    
    for (int i = 0; i < KRT_POOL_COUNT; i++) {
        KrtIrPoolDefrag(manager, (KrtPoolObjectType)i);
    }
}

int KrtIrPoolFragmentationRate(KrtIRPoolManager* manager, KrtPoolObjectType type) {
    if (!manager || type < 0 || type >= KRT_POOL_COUNT) return 0;
    
    KrtIRObjectPool* pool = &manager->pools[type];
    
    int free_count = count_pool_objects(pool);
    int total_allocated = pool->alloc_count;
    
    if (total_allocated == 0) return 0;
    
    return (free_count * 100) / total_allocated;
}

int KrtIrPoolGetHitRate(KrtIRPoolManager* manager, KrtPoolObjectType type) {
    if (!manager || type < 0 || type >= KRT_POOL_COUNT) return 0;
    
    KrtIRObjectPool* pool = &manager->pools[type];
    int total = pool->hit_count + pool->miss_count;
    
    if (total == 0) return 0;
    return (pool->hit_count * 100) / total;
}

void KrtIrPoolPrintStats(KrtIRPoolManager* manager) {
    if (!manager) return;
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              Object Pool Statistics                      ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Type         Allocs   Frees   Hit%%   Frag%%   Hit/Miss   ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    
    int total_allocs = 0;
    int total_frees = 0;
    int total_hits = 0;
    int total_misses = 0;
    
    for (int i = 0; i < KRT_POOL_COUNT; i++) {
        KrtIRObjectPool* pool = &manager->pools[i];
        if (pool->alloc_count > 0) {
            int hit_rate = KrtIrPoolGetHitRate(manager, (KrtPoolObjectType)i);
            int frag_rate = KrtIrPoolFragmentationRate(manager, (KrtPoolObjectType)i);
            printf("║ %s  %6d  %6d   %3d%%   %3d%%  %5d/%5d  ║\n",
                   pool->name,
                   pool->alloc_count,
                   pool->free_count,
                   hit_rate,
                   frag_rate,
                   pool->hit_count,
                   pool->miss_count);
            
            total_allocs += pool->alloc_count;
            total_frees += pool->free_count;
            total_hits += pool->hit_count;
            total_misses += pool->miss_count;
        }
    }
    
    printf("╠══════════════════════════════════════════════════════════╣\n");
    int total_rate = (total_hits + total_misses > 0) 
        ? (total_hits * 100) / (total_hits + total_misses) 
        : 0;
    printf("║ TOTAL        %6d  %6d   %3d%%                      ║\n",
           total_allocs, total_frees, total_rate);
    printf("╚══════════════════════════════════════════════════════════╝\n");
}

static KrtLazyOp* lazy_create_op(KrtLazyAllocManager* manager) {
    if (!manager) return NULL;
    
    if (manager->free_list) {
        KrtLazyOp* op = manager->free_list;
        manager->free_list = op->next;
        memset(op, 0, sizeof(KrtLazyOp));
        return op;
    }
    
    return (KrtLazyOp*)KRT_CALLOC(1, sizeof(KrtLazyOp));
}

static void lazy_recycle_op(KrtLazyAllocManager* manager, KrtLazyOp* op) {
    if (!manager || !op) return;
    
    op->next = manager->free_list;
    manager->free_list = op;
}

void KrtIrLazyInit(KrtLazyAllocManager* manager) {
    if (!manager) return;
    
    memset(manager, 0, sizeof(KrtLazyAllocManager));
    manager->batch_size = 32;      
    manager->enable_coalesce = 1;  
}

void KrtIrLazyDestroy(KrtLazyAllocManager* manager) {
    if (!manager) return;
    
    KrtIrLazyFlush(manager);
    
    KrtLazyOp* op = manager->pending;
    while (op) {
        KrtLazyOp* next = op->next;
        KRT_FREE(op);
        op = next;
    }
    
    op = manager->free_list;
    while (op) {
        KrtLazyOp* next = op->next;
        KRT_FREE(op);
        op = next;
    }
    
    memset(manager, 0, sizeof(KrtLazyAllocManager));
}

void KrtIrLazyRecordAlloc(KrtLazyAllocManager* manager, void** target, size_t size) {
    if (!manager || !target) return;
    
    KrtLazyOp* op = lazy_create_op(manager);
    if (!op) return;
    
    op->type = KRT_LAZY_OP_ALLOC;
    op->target = target;
    op->size = size;
    op->processed = 0;
    
    op->next = manager->pending;
    manager->pending = op;
    manager->pending_count++;
}

void KrtIrLazyRecordCopy(KrtLazyAllocManager* manager, void** target, void* source, size_t size) {
    if (!manager || !target) return;
    
    KrtLazyOp* op = lazy_create_op(manager);
    if (!op) return;
    
    op->type = KRT_LAZY_OP_COPY;
    op->target = target;
    op->source = source;
    op->size = size;
    op->processed = 0;
    
    op->next = manager->pending;
    manager->pending = op;
    manager->pending_count++;
}

void KrtIrLazyRecordInit(KrtLazyAllocManager* manager, void* target, size_t size) {
    if (!manager || !target) return;
    
    KrtLazyOp* op = lazy_create_op(manager);
    if (!op) return;
    
    op->type = KRT_LAZY_OP_INIT;
    op->target = target;
    op->size = size;
    op->processed = 0;
    
    op->next = manager->pending;
    manager->pending = op;
    manager->pending_count++;
}

static void lazy_execute_op(KrtLazyOp* op) {
    if (!op || op->processed) return;
    
    switch (op->type) {
        case KRT_LAZY_OP_ALLOC: {
            void** target = (void**)op->target;
            if (target && *target == NULL) {
                *target = KRT_MALLOC(op->size);
                if (*target) {
                    memset(*target, 0, op->size);
                }
            }
            break;
        }
        
        case KRT_LAZY_OP_COPY: {
            void** target = (void**)op->target;
            if (target && op->source && op->size > 0) {
                if (*target == NULL) {
                    *target = KRT_MALLOC(op->size);
                }
                if (*target) {
                    memcpy(*target, op->source, op->size);
                }
            }
            break;
        }
        
        case KRT_LAZY_OP_INIT: {
            void* target = op->target;
            if (target && op->size > 0) {
                memset(target, 0, op->size);
            }
            break;
        }
        
        default:
            break;
    }
    
    op->processed = 1;
}

void KrtIrLazyFlush(KrtLazyAllocManager* manager) {
    if (!manager) return;
    
    if (manager->enable_coalesce) {
        KrtIrLazyCoalesceAllocs(manager);
    }
    
    KrtLazyOp* op = manager->pending;
    KrtLazyOp* prev = NULL;
    
    while (op) {
        KrtLazyOp* next = op->next;
        
        if (!op->processed) {
            lazy_execute_op(op);
            manager->processed_count++;
        }
        
        if (op->processed) {
            if (prev) {
                prev->next = next;
            } else {
                manager->pending = next;
            }
            lazy_recycle_op(manager, op);
            manager->pending_count--;
            op = next;
        } else {
            prev = op;
            op = next;
        }
    }
}

void KrtIrLazyFlushBatch(KrtLazyAllocManager* manager, int max_ops) {
    if (!manager || max_ops <= 0) return;
    
    int count = 0;
    KrtLazyOp* op = manager->pending;
    
    while (op && count < max_ops) {
        if (!op->processed) {
            lazy_execute_op(op);
            manager->processed_count++;
            count++;
        }
        op = op->next;
    }
}

void KrtIrLazyCoalesceAllocs(KrtLazyAllocManager* manager) {
    if (!manager || !manager->enable_coalesce) return;
    
    size_t size_histogram[8] = {0};  
    
    KrtLazyOp* op = manager->pending;
    while (op) {
        if (op->type == KRT_LAZY_OP_ALLOC && !op->processed) {
            int bucket = 0;
            size_t size = op->size;
            while (size > 8 && bucket < 7) {
                size >>= 1;
                bucket++;
            }
            size_histogram[bucket]++;
        }
        op = op->next;
    }
    
    int coalesced = 0;
    for (int i = 0; i < 8; i++) {
        if (size_histogram[i] > 4) {
            coalesced++;
        }
    }
    
    manager->coalesced_count += coalesced;
}

bool KrtIrLazyHasPending(KrtLazyAllocManager* manager) {
    if (!manager) return false;
    
    KrtLazyOp* op = manager->pending;
    while (op) {
        if (!op->processed) return true;
        op = op->next;
    }
    return false;
}

int KrtIrLazyPendingCount(KrtLazyAllocManager* manager) {
    if (!manager) return 0;
    return manager->pending_count;
}

void KrtIrLazyPrintStats(KrtLazyAllocManager* manager) {
    if (!manager) return;
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         Lazy Allocation Statistics               ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Pending Operations:     %10d               ║\n", manager->pending_count);
    printf("║ Processed Operations:   %10d               ║\n", manager->processed_count);
    printf("║ Coalesced Groups:       %10d               ║\n", manager->coalesced_count);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Batch Size:             %10d               ║\n", manager->batch_size);
    printf("║ Coalesce Enabled:       %10s               ║\n", 
           manager->enable_coalesce ? "Yes" : "No");
    printf("╚══════════════════════════════════════════════════╝\n");
}

KrtIRProfiler g_ir_profiler = {0};
KrtIRMemoryStats g_ir_memory_stats = {0};
KrtIRPerformanceCounters g_ir_counters = {0};

void KrtIrProfilerInit(void) {
    memset(&g_ir_profiler, 0, sizeof(g_ir_profiler));
}

void KrtIrProfilerReset(void) {
    KrtIrProfilerInit();
}

static const char* profile_phase_name(KrtProfilePhase phase) {
    switch (phase) {
        case KRT_PROFILE_LEXER:    return "Lexer     ";
        case KRT_PROFILE_PARSER:   return "Parser    ";
        case KRT_PROFILE_SEMANTIC: return "Semantic  ";
        case KRT_PROFILE_IR_GEN:   return "IR Gen    ";
        case KRT_PROFILE_IR_OPT:   return "IR Opt    ";
        case KRT_PROFILE_CODEGEN:  return "Codegen   ";
        case KRT_PROFILE_LINKING:  return "Linking   ";
        case KRT_PROFILE_TOTAL:    return "TOTAL     ";
        default:                  return "Unknown   ";
    }
}

void KrtIrProfilerPrint(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║            IR Compilation Profile                ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Phase          Time (ms)    Calls   Avg (ms)     ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    
    double total = 0;
    for (int i = 0; i < KRT_PROFILE_PHASE_COUNT; i++) {
        if (g_ir_profiler.times[i] > 0) {
            double avg = g_ir_profiler.counts[i] > 0 
                ? g_ir_profiler.times[i] / g_ir_profiler.counts[i] 
                : 0;
            printf("║ %s  %8.3f    %5d   %8.3f    ║\n",
                   profile_phase_name(i),
                   g_ir_profiler.times[i],
                   g_ir_profiler.counts[i],
                   avg);
            total += g_ir_profiler.times[i];
        }
    }
    
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ %s  %8.3f                        ║\n", 
           profile_phase_name(KRT_PROFILE_TOTAL), total);
    printf("╚══════════════════════════════════════════════════╝\n");
}

void KrtIrProfileBegin(KrtProfilePhase phase) {
    if (phase < 0 || phase >= KRT_PROFILE_PHASE_COUNT) return;
    if (g_ir_profiler.active[phase]) return;
    
    g_ir_profiler.start_times[phase] = clock();
    g_ir_profiler.active[phase] = 1;
    g_ir_profiler.counts[phase]++;
}

void KrtIrProfileEnd(KrtProfilePhase phase) {
    if (phase < 0 || phase >= KRT_PROFILE_PHASE_COUNT) return;
    if (!g_ir_profiler.active[phase]) return;
    
    clock_t end = clock();
    double elapsed = ((double)(end - g_ir_profiler.start_times[phase]))
                     * 1000.0 / CLOCKS_PER_SEC;
    g_ir_profiler.times[phase] += elapsed;
    g_ir_profiler.active[phase] = 0;
}

void KrtIrMemoryTrackAlloc(size_t size) {
    g_ir_memory_stats.total_allocated += size;
    g_ir_memory_stats.current_used += size;
    g_ir_memory_stats.allocation_count++;
    
    if (g_ir_memory_stats.current_used > g_ir_memory_stats.peak_used) {
        g_ir_memory_stats.peak_used = g_ir_memory_stats.current_used;
    }
}

void KrtIrMemoryTrackFree(size_t size) {
    g_ir_memory_stats.total_freed += size;
    g_ir_memory_stats.current_used -= size;
    g_ir_memory_stats.free_count++;
}

void KrtIrMemoryPrint(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║            IR Memory Statistics                  ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Total Allocated:    %10zu bytes            ║\n", g_ir_memory_stats.total_allocated);
    printf("║ Total Freed:        %10zu bytes            ║\n", g_ir_memory_stats.total_freed);
    printf("║ Current Used:       %10zu bytes            ║\n", g_ir_memory_stats.current_used);
    printf("║ Peak Used:          %10zu bytes            ║\n", g_ir_memory_stats.peak_used);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Allocation Count:   %10zu                 ║\n", g_ir_memory_stats.allocation_count);
    printf("║ Free Count:         %10zu                 ║\n", g_ir_memory_stats.free_count);
    printf("╚══════════════════════════════════════════════════╝\n");
}

void KrtIrCountersReset(void) {
    memset(&g_ir_counters, 0, sizeof(g_ir_counters));
}

void KrtIrCountersPrint(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║         IR Performance Counters                  ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Instructions Generated:  %10d             ║\n", g_ir_counters.inst_count);
    printf("║ Basic Blocks:            %10d             ║\n", g_ir_counters.block_count);
    printf("║ Functions:               %10d             ║\n", g_ir_counters.function_count);
    printf("║ Variables:               %10d             ║\n", g_ir_counters.var_count);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Optimization Passes:     %10d             ║\n", g_ir_counters.opt_passes);
    printf("║ Optimizations Applied:   %10d             ║\n", g_ir_counters.opt_applied);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Arena Allocations:       %10d             ║\n", g_ir_counters.arena_allocs);
    printf("║ Malloc Allocations:      %10d             ║\n", g_ir_counters.malloc_allocs);
    printf("║ Arena Bytes:             %10zu bytes        ║\n", g_ir_counters.arena_bytes);
    printf("╚══════════════════════════════════════════════════╝\n");
}