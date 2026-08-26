
#include "X86RegAlloc.h"
#include <string.h>

static __attribute__((unused)) const char* g_caller_saved_regs[] = {
    "r10", "r11", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9"
};
static const int g_caller_saved_count = 9;

static __attribute__((unused)) const char* g_callee_saved_regs[] = {
    "rbx", "r12", "r13", "r14", "r15"
};
static const int g_callee_saved_count = 5;

const char* g_allocable_regs[X86_NUM_ALLOCABLE_REGS] = {
    "r10", "r11", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9",
    "rbx", "r12", "r13", "r14", "r15"
};

static int find_max_temp_index(KrtIRFunction* func) {
    int max_temp = 0;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        KrtIRInst* inst = block->first_inst;
        while (inst) {
            for (int i = 0; i < inst->operand_count; i++) {
                if (inst->operands[i].type == KRT_IR_VALUE_TEMP ||
                    inst->operands[i].type == KRT_IR_VALUE_ARG) {
                    int idx = inst->operands[i].data.index;
                    if (idx > max_temp) max_temp = idx;
                }
            }
            if (inst->result.type == KRT_IR_VALUE_TEMP ||
                inst->result.type == KRT_IR_VALUE_ARG) {
                int idx = inst->result.data.index;
                if (idx > max_temp) max_temp = idx;
            }
            inst = inst->next;
        }
        block = block->next;
    }
    return max_temp + 1;
}

void liveness_init_set(LiveSet* set) {
    memset(set->bits, 0, sizeof(set->bits));
}

int liveness_is_live(LiveSet* set, int temp_idx) {
    if (temp_idx < 0 || temp_idx >= X86_LIVESET_BITS) return 0;
    int byte_idx = temp_idx / 8;
    int bit_idx = temp_idx % 8;
    return (set->bits[byte_idx] >> bit_idx) & 1;
}

void liveness_add(LiveSet* set, int temp_idx) {
    if (temp_idx < 0 || temp_idx >= X86_LIVESET_BITS) return;
    int byte_idx = temp_idx / 8;
    int bit_idx = temp_idx % 8;
    set->bits[byte_idx] |= (1 << bit_idx);
}

void liveness_remove(LiveSet* set, int temp_idx) {
    if (temp_idx < 0 || temp_idx >= X86_LIVESET_BITS) return;
    int byte_idx = temp_idx / 8;
    int bit_idx = temp_idx % 8;
    set->bits[byte_idx] &= ~(1 << bit_idx);
}

void liveness_union(LiveSet* result, LiveSet* a, LiveSet* b) {
    for (int i = 0; i < 32; i++) {
        result->bits[i] = a->bits[i] | b->bits[i];
    }
}

void liveness_copy(LiveSet* dst, LiveSet* src) {
    memcpy(dst->bits, src->bits, sizeof(dst->bits));
}

int liveness_equal(LiveSet* a, LiveSet* b) {
    return memcmp(a->bits, b->bits, sizeof(a->bits)) == 0;
}

static void collect_temp_indices(KrtIRInst* inst, int* temps, int* count) {
    *count = 0;
    
    for (int i = 0; i < inst->operand_count; i++) {
        if (inst->operands[i].type == KRT_IR_VALUE_TEMP) {
            int idx = inst->operands[i].data.index;
            
            int found = 0;
            for (int j = 0; j < *count; j++) {
                if (temps[j] == idx) { found = 1; break; }
            }
            if (!found && *count < 16) {
                temps[(*count)++] = idx;
            }
        }
    }
    
    if (inst->result.type == KRT_IR_VALUE_TEMP) {
        int idx = inst->result.data.index;
        int found = 0;
        for (int j = 0; j < *count; j++) {
            if (temps[j] == idx) { found = 1; break; }
        }
        if (!found && *count < 16) {
            temps[(*count)++] = idx;
        }
    }
}

static int inst_uses_temp(KrtIRInst* inst, int temp_idx) {
    for (int i = 0; i < inst->operand_count; i++) {
        if (inst->operands[i].type == KRT_IR_VALUE_TEMP &&
            inst->operands[i].data.index == temp_idx) {
            return 1;
        }
    }
    return 0;
}

static int inst_defines_temp(KrtIRInst* inst, int temp_idx) {
    return inst->result.type == KRT_IR_VALUE_TEMP &&
           inst->result.data.index == temp_idx;
}

LivenessAnalysis* liveness_analysis_create(KrtIRFunction* func) {
    LivenessAnalysis* analysis = KRT_CALLOC(1, sizeof(LivenessAnalysis));
    
    int block_count = 0;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        block_count++;
        block = block->next;
    }
    analysis->block_count = block_count;
    analysis->block_info = KRT_CALLOC(block_count, sizeof(BlockLiveInfo));
    
    int inst_count = 0;
    block = func->entry_block;
    while (block) {
        KrtIRInst* inst = block->first_inst;
        while (inst) {
            inst_count++;
            inst = inst->next;
        }
        block = block->next;
    }
    analysis->inst_count = inst_count;
    analysis->inst_live = KRT_CALLOC(inst_count, sizeof(LiveSet));
    
    return analysis;
}

void liveness_analysis_destroy(LivenessAnalysis* analysis) {
    if (analysis) {
        KRT_FREE(analysis->block_info);
        KRT_FREE(analysis->inst_live);
        KRT_FREE(analysis);
    }
}

void liveness_analysis_run(LivenessAnalysis* analysis, KrtIRFunction* func) {
    
    int block_idx = 0;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        BlockLiveInfo* info = &analysis->block_info[block_idx];
        liveness_init_set(&info->use);
        liveness_init_set(&info->def);
        liveness_init_set(&info->live_in);
        liveness_init_set(&info->live_out);
        
        KrtIRInst* inst = block->first_inst;
        while (inst) {
            
            int temps[16], temp_count;
            collect_temp_indices(inst, temps, &temp_count);
            
            for (int i = 0; i < temp_count; i++) {
                int temp_idx = temps[i];
                
                if (!liveness_is_live(&info->def, temp_idx)) {
                    if (inst_uses_temp(inst, temp_idx)) {
                        liveness_add(&info->use, temp_idx);
                    }
                }
                
                if (inst_defines_temp(inst, temp_idx)) {
                    liveness_add(&info->def, temp_idx);
                }
            }
            
            inst = inst->next;
        }
        
        block_idx++;
        block = block->next;
    }
    
    if (analysis->block_count > 0) {
        
        int max_temp = find_max_temp_index(func);
        int temp_capacity = max_temp > X86_LIVESET_BITS ? max_temp : X86_LIVESET_BITS;
        
        int* def_pos = (int*)malloc(temp_capacity * sizeof(int));
        int* last_use_pos = (int*)malloc(temp_capacity * sizeof(int));
        
        memset(def_pos, -1, temp_capacity * sizeof(int));
        memset(last_use_pos, -1, temp_capacity * sizeof(int));
        
        KrtIRBasicBlock* scan_block = func->entry_block;
        int global_inst_idx = 0;
        
        while (scan_block) {
            KrtIRInst* inst = scan_block->first_inst;
            while (inst) {
                
                if (inst->result.type == KRT_IR_VALUE_TEMP || inst->result.type == KRT_IR_VALUE_ARG) {
                    int temp_idx = inst->result.data.index;
                    if (temp_idx >= 0 && temp_idx < temp_capacity) {
                        def_pos[temp_idx] = global_inst_idx;
                        if (last_use_pos[temp_idx] < 0) {
                            last_use_pos[temp_idx] = global_inst_idx;
                        }
                    }
                }
                
                for (int i = 0; i < inst->operand_count; i++) {
                    if (inst->operands[i].type == KRT_IR_VALUE_TEMP || 
                        inst->operands[i].type == KRT_IR_VALUE_ARG) {
                        int temp_idx = inst->operands[i].data.index;
                        if (temp_idx >= 0 && temp_idx < temp_capacity) {
                            last_use_pos[temp_idx] = global_inst_idx;
                        }
                    }
                }
                inst = inst->next;
                global_inst_idx++;
            }
            scan_block = scan_block->next;
        }
        
        for (int i = 0; i < global_inst_idx && i < X86_MAX_INSTRUCTIONS; i++) {
            liveness_init_set(&analysis->inst_live[i]);
            
            for (int temp_idx = 0; temp_idx < temp_capacity; temp_idx++) {
                int dpos = def_pos[temp_idx];
                int lupos = last_use_pos[temp_idx];
                
                if (dpos < 0 || lupos < 0) continue;
                
                if (dpos <= i && i <= lupos) {
                    liveness_add(&analysis->inst_live[i], temp_idx);
                }
            }
        }
        
        free(def_pos);
        free(last_use_pos);
    }
}

ConflictGraph* conflict_graph_create(void) {
    ConflictGraph* graph = KRT_CALLOC(1, sizeof(ConflictGraph));
    return graph;
}

void conflict_graph_destroy(ConflictGraph* graph) {
    if (!graph) return;
    
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i]) {
            KRT_FREE(graph->nodes[i]->neighbors);
            KRT_FREE(graph->nodes[i]);
        }
    }
    KRT_FREE(graph);
}

static ConflictNode* conflict_graph_get_or_create_node(ConflictGraph* graph, int temp_idx) {
    
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i] && graph->nodes[i]->temp_idx == temp_idx) {
            return graph->nodes[i];
        }
    }
    
    if (graph->node_count >= 256) return NULL;
    
    ConflictNode* node = KRT_CALLOC(1, sizeof(ConflictNode));
    node->temp_idx = temp_idx;
    node->color = -1;
    node->is_spilled = 0;
    node->neighbors = KRT_CALLOC(8, sizeof(ConflictNode*));
    node->neighbor_capacity = 8;
    node->neighbor_count = 0;
    
    graph->nodes[graph->node_count++] = node;
    return node;
}

void conflict_graph_add_edge(ConflictGraph* graph, int temp_a, int temp_b) {
    if (temp_a == temp_b) return;
    
    ConflictNode* node_a = conflict_graph_get_or_create_node(graph, temp_a);
    ConflictNode* node_b = conflict_graph_get_or_create_node(graph, temp_b);
    
    if (!node_a || !node_b) return;
    
    for (int i = 0; i < node_a->neighbor_count; i++) {
        if (node_a->neighbors[i] == node_b) return;
    }
    
    if (node_a->neighbor_count >= node_a->neighbor_capacity) {
        node_a->neighbor_capacity *= 2;
        node_a->neighbors = KRT_REALLOC(node_a->neighbors, 
                                       node_a->neighbor_capacity * sizeof(ConflictNode*));
    }
    node_a->neighbors[node_a->neighbor_count++] = node_b;
    node_a->degree++;
    
    if (node_b->neighbor_count >= node_b->neighbor_capacity) {
        node_b->neighbor_capacity *= 2;
        node_b->neighbors = KRT_REALLOC(node_b->neighbors,
                                       node_b->neighbor_capacity * sizeof(ConflictNode*));
    }
    node_b->neighbors[node_b->neighbor_count++] = node_a;
    node_b->degree++;
}

int conflict_graph_are_conflicting(ConflictGraph* graph, int temp_a, int temp_b) {
    ConflictNode* node_a = NULL;
    for (int i = 0; i < graph->node_count; i++) {
        if (graph->nodes[i] && graph->nodes[i]->temp_idx == temp_a) {
            node_a = graph->nodes[i];
            break;
        }
    }
    
    if (!node_a) return 0;
    
    for (int i = 0; i < node_a->neighbor_count; i++) {
        if (node_a->neighbors[i] && node_a->neighbors[i]->temp_idx == temp_b) {
            return 1;
        }
    }
    return 0;
}

void conflict_graph_build(ConflictGraph* graph, LivenessAnalysis* liveness, KrtIRFunction* func) {
    
    KrtIRBasicBlock* block = func->entry_block;
    int inst_idx = 0;
    int def_count = 0;
    int edge_count = 0;
    
    while (block) {
        KrtIRInst* inst = block->first_inst;
        while (inst) {
            
            LiveSet* live_after = &liveness->inst_live[inst_idx];
            
            if (inst->result.type == KRT_IR_VALUE_TEMP || inst->result.type == KRT_IR_VALUE_ARG) {
                int def_temp = inst->result.data.index;
                def_count++;
                
                conflict_graph_get_or_create_node(graph, def_temp);
                
                for (int i = 0; i < X86_LIVESET_BITS; i++) {
                    if (liveness_is_live(live_after, i) && i != def_temp) {
                        conflict_graph_add_edge(graph, def_temp, i);
                        edge_count++;
                    }
                }
            }
            
            inst = inst->next;
            inst_idx++;
        }
        block = block->next;
    }
    
}

typedef struct {
    ConflictNode* node;
    int original_degree;
} NodeWithDegree;

typedef struct {
    ConflictNode* node;
    double spill_cost;
} NodeWithSpillCost;

static int compare_by_spill_cost_asc(const void* a, const void* b) {
    NodeWithSpillCost* na = (NodeWithSpillCost*)a;
    NodeWithSpillCost* nb = (NodeWithSpillCost*)b;
    if (na->spill_cost < nb->spill_cost) return -1;
    if (na->spill_cost > nb->spill_cost) return 1;
    return 0;
}

static double calculate_spill_cost(ConflictNode* node, LivenessAnalysis* liveness) {
    
    double cost = 1.0;
    
    if (!node) return cost;
    
    cost += node->degree * 2.0;
    
    if (liveness && liveness->inst_count > 0) {
        int temp_idx = node->temp_idx;
        
        int use_count = 0;
        for (int i = 0; i < liveness->inst_count && i < X86_MAX_INSTRUCTIONS; i++) {
            if (liveness_is_live(&liveness->inst_live[i], temp_idx)) {
                use_count++;
            }
        }
        
        cost += use_count * 5.0;
        
        int first_def = -1, last_use = -1;
        (void)first_def;
        (void)last_use;
    }
    
    if (node->degree >= X86_NUM_ALLOCABLE_REGS) {
        cost *= 3.0;
    }
    
    return cost;
}

RegAllocResult* regalloc_allocate_with_capacity(ConflictGraph* graph, int capacity, LivenessAnalysis* liveness) {
    RegAllocResult* result = KRT_CALLOC(1, sizeof(RegAllocResult));
    result->temp_to_reg = (int*)malloc(capacity * sizeof(int));
    result->temp_to_stack = (int*)malloc(capacity * sizeof(int));
    result->capacity = capacity;
    
    for (int i = 0; i < capacity; i++) {
        result->temp_to_reg[i] = -1;
        result->temp_to_stack[i] = -1;
    }
    
    if (graph->node_count == 0) {
        return result;
    }

    NodeWithSpillCost* nodes = KRT_CALLOC(graph->node_count, sizeof(NodeWithSpillCost));
    for (int i = 0; i < graph->node_count; i++) {
        nodes[i].node = graph->nodes[i];
        nodes[i].spill_cost = calculate_spill_cost(graph->nodes[i], liveness);
    }
    
    qsort(nodes, graph->node_count, sizeof(NodeWithSpillCost), compare_by_spill_cost_asc);

    int used_colors[X86_NUM_ALLOCABLE_REGS] = {0};
    int num_colored = 0;

    int* uncolored_indices = (int*)malloc(graph->node_count * sizeof(int));
    int uncolored_count = 0;
    
    for (int pass = 0; pass < 2; pass++) {
        int start_idx = (pass == 0) ? graph->node_count - 1 : 0;
        int end_idx = (pass == 0) ? -1 : graph->node_count;
        int step = (pass == 0) ? -1 : 1;
        
        for (int i = start_idx; i != end_idx; i += step) {
            ConflictNode* node = nodes[i].node;
            if (node->color >= 0) continue;

            int temp_idx = node->temp_idx;
            memset(used_colors, 0, sizeof(used_colors));
            
            for (int j = 0; j < node->neighbor_count; j++) {
                ConflictNode* neighbor = node->neighbors[j];
                if (neighbor->color >= 0 && neighbor->color < X86_NUM_ALLOCABLE_REGS) {
                    used_colors[neighbor->color] = 1;
                }
            }

            int assigned = 0;
            
            for (int c = 0; c < g_caller_saved_count; c++) {
                if (!used_colors[c]) {
                    node->color = c;
                    if (temp_idx >= 0 && temp_idx < result->capacity) {
                        result->temp_to_reg[temp_idx] = c;
                    }
                    assigned = 1;
                    num_colored++;
                    break;
                }
            }
            
            if (!assigned) {
                for (int c = 0; c < g_callee_saved_count; c++) {
                    int reg_idx = g_caller_saved_count + c;
                    if (!used_colors[reg_idx]) {
                        node->color = reg_idx;
                        if (temp_idx >= 0 && temp_idx < result->capacity) {
                            result->temp_to_reg[temp_idx] = reg_idx;
                        }
                        assigned = 1;
                        num_colored++;
                        break;
                    }
                }
            }
            
            if (!assigned) {
                uncolored_indices[uncolored_count++] = i;
            }
        }
    }
    
    if (uncolored_count > 0) {
        NodeWithSpillCost* uncolored = KRT_CALLOC(uncolored_count, sizeof(NodeWithSpillCost));
        for (int i = 0; i < uncolored_count; i++) {
            uncolored[i] = nodes[uncolored_indices[i]];
        }
        qsort(uncolored, uncolored_count, sizeof(NodeWithSpillCost), compare_by_spill_cost_asc);
        
        for (int u = 0; u < uncolored_count; u++) {
            ConflictNode* node = uncolored[u].node;
            int temp_idx = node->temp_idx;
            
            memset(used_colors, 0, sizeof(used_colors));
            for (int j = 0; j < node->neighbor_count; j++) {
                ConflictNode* neighbor = node->neighbors[j];
                if (neighbor->color >= 0 && neighbor->color < X86_NUM_ALLOCABLE_REGS) {
                    used_colors[neighbor->color] = 1;
                }
            }

            int assigned = 0;
            for (int c = 0; c < g_caller_saved_count; c++) {
                if (!used_colors[c]) {
                    node->color = c;
                    if (temp_idx >= 0 && temp_idx < result->capacity) {
                        result->temp_to_reg[temp_idx] = c;
                    }
                    assigned = 1;
                    break;
                }
            }
            
            if (!assigned) {
                for (int c = 0; c < g_callee_saved_count; c++) {
                    int reg_idx = g_caller_saved_count + c;
                    if (!used_colors[reg_idx]) {
                        node->color = reg_idx;
                        if (temp_idx >= 0 && temp_idx < result->capacity) {
                            result->temp_to_reg[temp_idx] = reg_idx;
                        }
                        assigned = 1;
                        break;
                    }
                }
            }
            
            if (!assigned) {
                node->is_spilled = 1;
                result->num_spilled++;
            }
        }
        
        KRT_FREE(uncolored);
        free(uncolored_indices);
    } else {
        free(uncolored_indices);
    }
    
    int stack_offset = 0;
    for (int i = 0; i < graph->node_count; i++) {
        ConflictNode* node = graph->nodes[i];
        if (node->is_spilled) {
            stack_offset += 8;
            node->stack_offset = stack_offset;
            if (node->temp_idx >= 0 && node->temp_idx < result->capacity) {
                result->temp_to_stack[node->temp_idx] = stack_offset;
            }
        }
    }
    result->stack_space_needed = stack_offset;
    
    KRT_FREE(nodes);
    return result;
}

void regalloc_result_destroy(RegAllocResult* result) {
    if (!result) return;
    free(result->temp_to_reg);
    free(result->temp_to_stack);
    KRT_FREE(result);
}

const char* regalloc_get_reg_name(RegAllocResult* result, int temp_idx) {
    if (temp_idx < 0 || temp_idx >= result->capacity) return NULL;

    int reg_idx = result->temp_to_reg[temp_idx];
    if (reg_idx < 0 || reg_idx >= X86_NUM_ALLOCABLE_REGS) return NULL;
    
    return g_allocable_regs[reg_idx];
}

int regalloc_get_stack_offset(RegAllocResult* result, int temp_idx) {
    if (temp_idx < 0 || temp_idx >= result->capacity) return -1;
    return result->temp_to_stack[temp_idx];
}

RegAllocResult* x86_allocate_registers(KrtIRFunction* func) {
    
    LivenessAnalysis* liveness = liveness_analysis_create(func);
    liveness_analysis_run(liveness, func);

    ConflictGraph* graph = conflict_graph_create();
    conflict_graph_build(graph, liveness, func);

    int max_temp = find_max_temp_index(func);
    int capacity = max_temp > X86_TEMP_INITIAL_CAPACITY ? max_temp : X86_TEMP_INITIAL_CAPACITY;
    RegAllocResult* result = regalloc_allocate_with_capacity(graph, capacity, liveness);

    int reg_count = 0, spill_count = 0;
    for (int i = 0; i < result->capacity; i++) {
        if (result->temp_to_reg[i] >= 0) reg_count++;
        if (result->temp_to_stack[i] >= 0) spill_count++;
    }

    conflict_graph_destroy(graph);
    liveness_analysis_destroy(liveness);

    return result;
}

void x86_regalloc_destroy(RegAllocResult* result) {
    regalloc_result_destroy(result);
}