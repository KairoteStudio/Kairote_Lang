
#ifndef KRT_X86_REGALLOC_H
#define KRT_X86_REGALLOC_H

#include "X86Codegen.h"

#define X86_MAX_INSTRUCTIONS 10000
#define X86_LIVESET_BITS 256
#define X86_LIVESET_BYTES (X86_LIVESET_BITS / 8)
#define X86_MAX_CONFLICT_NODES 256
#define X86_NUM_ALLOCABLE_REGS 14
#define X86_CALLER_SAVED_REGS 9
#define X86_CALLEE_SAVED_REGS 5
#define X86_MAX_TEMP_INITIAL 256
#define X86_MAX_TEMP_GROWTH_FACTOR 2

typedef struct {
    unsigned char bits[X86_LIVESET_BYTES];
} LiveSet;

typedef struct {
    LiveSet use;
    LiveSet def;
    LiveSet live_in;
    LiveSet live_out;
} BlockLiveInfo;

typedef struct {
    BlockLiveInfo* block_info;
    int block_count;
    LiveSet* inst_live;
    int inst_count;
} LivenessAnalysis;

/** @brief Allocate empty liveness storage for every block and instruction in func. */
LivenessAnalysis* liveness_analysis_create(KrtIRFunction* func);
/** @brief Release an analysis, accepting NULL. */
void liveness_analysis_destroy(LivenessAnalysis* analysis);
/** @brief Compute temporary live ranges for func into analysis. */
void liveness_analysis_run(LivenessAnalysis* analysis, KrtIRFunction* func);
/** @brief Return whether temp_idx belongs to set. */
int liveness_is_live(LiveSet* set, int temp_idx);
/** @brief Add an in-range temporary to set. */
void liveness_add(LiveSet* set, int temp_idx);
/** @brief Remove an in-range temporary from set. */
void liveness_remove(LiveSet* set, int temp_idx);
/** @brief Store the union of a and b in result. */
void liveness_union(LiveSet* result, LiveSet* a, LiveSet* b);

struct ConflictNodeStruct;
typedef struct ConflictNodeStruct ConflictNode;

struct ConflictNodeStruct {
    int temp_idx;
    int degree;
    int color;
    int is_spilled;
    int stack_offset;
    ConflictNode** neighbors;
    int neighbor_count;
    int neighbor_capacity;
};

typedef struct {
    ConflictNode* nodes[X86_MAX_CONFLICT_NODES];
    int node_count;
} ConflictGraph;

/** @brief Allocate an empty register interference graph. */
ConflictGraph* conflict_graph_create(void);
/** @brief Release graph and its adjacency lists, accepting NULL. */
void conflict_graph_destroy(ConflictGraph* graph);
/** @brief Build temporary interference edges from func and its liveness. */
void conflict_graph_build(ConflictGraph* graph, LivenessAnalysis* liveness, KrtIRFunction* func);
/** @brief Add a symmetric interference edge between two temporaries. */
void conflict_graph_add_edge(ConflictGraph* graph, int temp_a, int temp_b);
/** @brief Return whether temp_a and temp_b interfere. */
int conflict_graph_are_conflicting(ConflictGraph* graph, int temp_a, int temp_b);

extern const char* g_allocable_regs[X86_NUM_ALLOCABLE_REGS];

typedef struct {
    int* temp_to_reg;
    int* temp_to_stack;
    int capacity;
    int num_spilled;
    int stack_space_needed;
} RegAllocResult;

/** @brief Allocate registers for graph. */
RegAllocResult* regalloc_allocate(ConflictGraph* graph);
/** @brief Color graph and record register or spill locations for capacity temporaries. */
RegAllocResult* regalloc_allocate_with_capacity(ConflictGraph* graph, int capacity, LivenessAnalysis* liveness);
/** @brief Release register and stack assignment tables, accepting NULL. */
void regalloc_result_destroy(RegAllocResult* result);
/** @brief Return the assigned register name, or NULL for a spilled or invalid temporary. */
const char* regalloc_get_reg_name(RegAllocResult* result, int temp_idx);
/** @brief Return the assigned spill offset, or -1 when none exists. */
int regalloc_get_stack_offset(RegAllocResult* result, int temp_idx);

/** @brief Analyze func and return its register and spill assignments. */
RegAllocResult* x86_allocate_registers(KrtIRFunction* func);
/** @brief Release an allocation result, accepting NULL. */
void x86_regalloc_destroy(RegAllocResult* result);

#endif
