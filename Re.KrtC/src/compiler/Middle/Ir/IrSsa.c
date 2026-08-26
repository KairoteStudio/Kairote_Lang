#include "IrSsa.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SSA_MAX_BLOCKS 4096
#define SSA_HASH_BUCKETS 256
#define SSA_DOM_INITIAL -1

static unsigned int hash_string(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

KrtIRVarTable* KrtIrVarTableCreate(KrtIRMemoryArena* arena, int bucket_count) {
    if (!arena) return NULL;
    
    KrtIRVarTable* table = (KrtIRVarTable*)KrtIrArenaAlloc(arena, sizeof(KrtIRVarTable));
    if (!table) return NULL;
    
    table->buckets = (KrtIRVarVersion**)KrtIrArenaAlloc(arena, bucket_count * sizeof(KrtIRVarVersion*));
    if (!table->buckets) return NULL;
    
    memset(table->buckets, 0, bucket_count * sizeof(KrtIRVarVersion*));
    table->bucket_count = bucket_count;
    table->var_count = 0;
    
    return table;
}

void KrtIrVarTableDestroy(KrtIRVarTable* table) {
    (void)table;
}

static KrtIRVarVersion* var_table_find(KrtIRVarTable* table, const char* name) {
    if (!table || !name) return NULL;
    
    unsigned int hash = hash_string(name);
    int index = hash % table->bucket_count;
    
    KrtIRVarVersion* var = table->buckets[index];
    while (var) {
        if (strcmp(var->name, name) == 0) {
            return var;
        }
        var = var->next;
    }
    
    return NULL;
}

static void var_table_add(KrtIRVarTable* table, KrtIRVarVersion* var) {
    if (!table || !var || !var->name) return;
    
    unsigned int hash = hash_string(var->name);
    int index = hash % table->bucket_count;
    
    var->next = table->buckets[index];
    table->buckets[index] = var;
    table->var_count++;
}

KrtIRVarVersion* KrtIrVarGetVersion(KrtIRVarTable* table, const char* name) {
    return var_table_find(table, name);
}

KrtIRVarVersion* KrtIrVarNewVersion(KrtIRSSABuilder* ssa_builder, const char* name, KrtIRType* type,
                                       KrtIRBasicBlock* block, KrtIRInst* def) {
    if (!ssa_builder || !name) return NULL;
    
    KrtIRVarVersion* existing = var_table_find(ssa_builder->var_table, name);
    int new_version = existing ? (existing->version + 1) : 0;
    
    KrtIRVarVersion* var = (KrtIRVarVersion*)KrtIrArenaAlloc(ssa_builder->arena, sizeof(KrtIRVarVersion));
    if (!var) return NULL;
    
    memset(var, 0, sizeof(KrtIRVarVersion));
    var->name = KrtIrArenaStrdup(ssa_builder->arena, name);
    var->version = new_version;
    var->type = type;
    var->block = block;
    var->def = def;
    
    var_table_add(ssa_builder->var_table, var);
    
    return var;
}

KrtIRVarVersion* KrtIrVarFindVersion(KrtIRVarTable* table, const char* name, KrtIRBasicBlock* block) {
    (void)block;
    return var_table_find(table, name);
}

char* KrtIrVarVersionedName(KrtIRMemoryArena* arena, const char* name, int version) {
    if (!arena || !name) return NULL;
    
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s_%d", name, version);
    return KrtIrArenaStrdup(arena, buffer);
}

KrtIRPhi* KrtIrPhiCreate(KrtIRMemoryArena* arena, const char* var_name, KrtIRType* type,
                           int pred_count, KrtIRBasicBlock* parent) {
    if (!arena || !var_name) return NULL;
    
    KrtIRPhi* phi = (KrtIRPhi*)KrtIrArenaAlloc(arena, sizeof(KrtIRPhi));
    if (!phi) return NULL;
    
    memset(phi, 0, sizeof(KrtIRPhi));
    phi->var_name = KrtIrArenaStrdup(arena, var_name);
    phi->type = type;
    phi->pred_count = pred_count;
    phi->parent_block = parent;
    
    if (pred_count > 0) {
        phi->blocks = (KrtIRBasicBlock**)KrtIrArenaAlloc(arena, pred_count * sizeof(KrtIRBasicBlock*));
        phi->values = (KrtIRValue*)KrtIrArenaAlloc(arena, pred_count * sizeof(KrtIRValue));
        
        if (phi->blocks) memset(phi->blocks, 0, pred_count * sizeof(KrtIRBasicBlock*));
        if (phi->values) memset(phi->values, 0, pred_count * sizeof(KrtIRValue));
    } else {
        phi->blocks = NULL;
        phi->values = NULL;
    }
    
    return phi;
}

void KrtIrPhiAddOperand(KrtIRPhi* phi, KrtIRBasicBlock* block, KrtIRValue value, int index) {
    if (!phi || index < 0 || index >= phi->pred_count) return;
    
    phi->blocks[index] = block;
    phi->values[index] = value;
}

KrtIRPhi* KrtIrPhiFind(KrtIRBasicBlock* block, const char* var_name) {
    if (!block || !var_name) return NULL;
    
    KrtIRBlockPhiList* phi_list = block->phi_list;
    if (!phi_list) return NULL;
    
    KrtIRPhi* phi = phi_list->head;
    while (phi) {
        if (strcmp(phi->var_name, var_name) == 0) {
            return phi;
        }
        phi = phi->next;
    }
    return NULL;
}

void KrtIrBlockAddPhi(KrtIRBasicBlock* block, KrtIRPhi* phi) {
    if (!block || !phi) return;
    
    KrtIRBlockPhiList* phi_list = block->phi_list;
    if (!phi_list) {
        phi_list = (KrtIRBlockPhiList*)calloc(1, sizeof(KrtIRBlockPhiList));
        block->phi_list = phi_list;
    }
    
    if (!phi_list->head) {
        phi_list->head = phi;
        phi_list->tail = phi;
    } else {
        phi_list->tail->next = phi;
        phi_list->tail = phi;
    }
}

KrtIRBlockPhiList* KrtIrBlockGetPhiList(KrtIRBasicBlock* block) {
    if (!block) return NULL;
    return block->phi_list;
}

KrtIRSSABuilder* KrtIrSsaBuilderCreate(KrtIRBuilder* builder) {
    if (!builder) return NULL;
    
    KrtIRSSABuilder* ssa = (KrtIRSSABuilder*)KRT_CALLOC(1, sizeof(KrtIRSSABuilder));
    if (!ssa) return NULL;
    
    ssa->builder = builder;
    ssa->arena = builder->arena;
    ssa->var_table = KrtIrVarTableCreate(ssa->arena, SSA_HASH_BUCKETS);
    ssa->version_counters = NULL;
    ssa->var_capacity = 0;
    ssa->current_var_stack = NULL;
    ssa->stack_capacity = 0;
    
    return ssa;
}

void KrtIrSsaBuilderDestroy(KrtIRSSABuilder* ssa_builder) {
    if (!ssa_builder) return;
    
    if (ssa_builder->var_table) {
        KrtIrVarTableDestroy(ssa_builder->var_table);
    }
    
    KRT_FREE(ssa_builder->current_var_stack);
    KRT_FREE(ssa_builder);
}

typedef struct {
    char** names;
    int count;
    int capacity;
} VarNameList;

static void add_var_name(VarNameList* list, const char* name) {
    if (!list || !name) return;
    
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->names[i], name) == 0) {
            return;
        }
    }
    
    if (list->count >= list->capacity) {
        list->capacity = list->capacity > 0 ? list->capacity * 2 : 16;
        char** new_names = (char**)KRT_REALLOC(list->names, list->capacity * sizeof(char*));
        if (!new_names) return;
        list->names = new_names;
    }
    
    list->names[list->count++] = KRT_STRDUP(name);
}

char** KrtIrSsaCollectVars(KrtIRMemoryArena* arena, KrtIRFunction* func, int* count) {
    (void)arena;
    if (!func || !count) return NULL;
    
    VarNameList list = {0};
    
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (!inst) continue;
            
            if ((inst->opcode == KRT_IR_STORE || inst->opcode == KRT_IR_COPY) && inst->operand_count > 0) {
                if (inst->opcode == KRT_IR_COPY && inst->result.type == KRT_IR_VALUE_VAR && inst->result.data.name) {
                    add_var_name(&list, inst->result.data.name);
                } else if (inst->opcode == KRT_IR_STORE) {
                    KrtIRValue* val = &inst->operands[0];
                    if (val->type == KRT_IR_VALUE_VAR && val->data.name) {
                        add_var_name(&list, val->data.name);
                    }
                }
            }
            
            if (inst->opcode == KRT_IR_LOAD && inst->operand_count > 0) {
                KrtIRValue* val = &inst->operands[0];
                if (val->type == KRT_IR_VALUE_VAR && val->data.name) {
                    add_var_name(&list, val->data.name);
                }
            }
            
            for (int j = 0; j < inst->operand_count; j++) {
                if (inst->operands[j].type == KRT_IR_VALUE_VAR && inst->operands[j].data.name) {
                    add_var_name(&list, inst->operands[j].data.name);
                }
            }
        }
        block = block->next;
    }
    
    *count = list.count;
    return list.names;
}

static int count_blocks(KrtIRFunction* func) {
    int count = 0;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        count++;
        block = block->next;
    }
    return count;
}

static void collect_blocks(KrtIRFunction* func, KrtIRBasicBlock** blocks, int* block_to_index, int* count) {
    int idx = 0;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        blocks[idx] = block;
        block_to_index[block->id] = idx;
        idx++;
        block = block->next;
    }
    *count = idx;
}

int KrtIrComputePostorder(KrtIRFunction* func, int* order, int max_blocks) {
    if (!func || !order) return 0;
    
    KrtIRBasicBlock** blocks = (KrtIRBasicBlock**)calloc(max_blocks > 0 ? max_blocks : 64, sizeof(KrtIRBasicBlock*));
    if (!blocks) return 0;
    
    int* visited = (int*)calloc(max_blocks > 0 ? max_blocks : 64, sizeof(int));
    if (!visited) {
        free(blocks);
        return 0;
    }
    
    int block_count = 0;
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        blocks[block_count++] = block;
        block = block->next;
    }
    
    int* stack = (int*)calloc(block_count > 0 ? block_count : 1, sizeof(int));
    int* result = (int*)calloc(block_count > 0 ? block_count : 1, sizeof(int));
    if (!stack || !result) {
        free(blocks);
        free(visited);
        free(stack);
        free(result);
        return 0;
    }
    
    int stack_top = 0;
    int result_top = 0;
    
    int entry_idx = 0;
    for (int i = 0; i < block_count; i++) {
        if (blocks[i] == func->entry_block) {
            entry_idx = i;
            break;
        }
    }
    
    stack[stack_top++] = entry_idx;
    
    while (stack_top > 0) {
        int current = stack[--stack_top];
        
        if (visited[current]) continue;
        visited[current] = 1;
        
        KrtIRBasicBlock* blk = blocks[current];
        for (int i = 0; i < blk->succ_count; i++) {
            KrtIRBasicBlock* succ = blk->succs[i];
            for (int j = 0; j < block_count; j++) {
                if (blocks[j] == succ && !visited[j]) {
                    stack[stack_top++] = j;
                    break;
                }
            }
        }
        
        result[result_top++] = current;
    }
    
    for (int i = 0; i < result_top && i < max_blocks; i++) {
        order[i] = result[result_top - 1 - i];
    }
    
    free(blocks);
    free(visited);
    free(stack);
    free(result);
    
    return result_top;
}

KrtIRDominanceInfo* KrtIrComputeDominance(KrtIRFunction* func, KrtIRMemoryArena* arena) {
    if (!func || !arena) return NULL;
    
    int max_blocks = count_blocks(func);
    if (max_blocks == 0) return NULL;
    
    KrtIRDominanceInfo* info = (KrtIRDominanceInfo*)KrtIrArenaAlloc(arena, sizeof(KrtIRDominanceInfo));
    if (!info) return NULL;
    memset(info, 0, sizeof(KrtIRDominanceInfo));
    
    info->block_count = max_blocks;
    info->dom = (int*)KrtIrArenaAlloc(arena, max_blocks * max_blocks * sizeof(int));
    info->idom = (int*)KrtIrArenaAlloc(arena, max_blocks * sizeof(int));
    info->df = (int**)KrtIrArenaAlloc(arena, max_blocks * sizeof(int*));
    info->df_count = (int*)KrtIrArenaAlloc(arena, max_blocks * sizeof(int));
    info->postorder = (int*)KrtIrArenaAlloc(arena, max_blocks * sizeof(int));
    info->block_to_index = (int*)KrtIrArenaAlloc(arena, (max_blocks + 1024) * sizeof(int));
    
    if (!info->dom || !info->idom || !info->df || !info->df_count || !info->postorder || !info->block_to_index) {
        return NULL;
    }
    
    memset(info->idom, -1, max_blocks * sizeof(int));
    memset(info->df_count, 0, max_blocks * sizeof(int));
    memset(info->block_to_index, -1, (max_blocks + 1024) * sizeof(int));
    
    KrtIRBasicBlock** blocks = (KrtIRBasicBlock**)KrtIrArenaAlloc(arena, max_blocks * sizeof(KrtIRBasicBlock*));
    if (!blocks) return NULL;
    collect_blocks(func, blocks, info->block_to_index, &info->block_count);
    
    for (int i = 0; i < info->block_count; i++) {
        info->df[i] = (int*)KrtIrArenaAlloc(arena, info->block_count * sizeof(int));
        if (info->df[i]) {
            memset(info->df[i], -1, info->block_count * sizeof(int));
        }
    }
    
    info->postorder_count = KrtIrComputePostorder(func, info->postorder, max_blocks);
    
    for (int i = 0; i < info->block_count; i++) {
        for (int j = 0; j < info->block_count; j++) {
            info->dom[i * info->block_count + j] = (i == j) ? 1 : 0;
        }
    }
    
    int entry_idx = -1;
    for (int i = 0; i < info->block_count; i++) {
        if (blocks[i] == func->entry_block) {
            entry_idx = i;
            break;
        }
    }
    
    if (entry_idx < 0) {
        return info;
    }
    
    bool changed = true;
    int iterations = 0;
    while (changed && iterations < 100) {
        changed = false;
        iterations++;
        
        for (int i = info->postorder_count - 1; i >= 0; i--) {
            int b = info->postorder[i];
            if (b == entry_idx) continue;
            
            KrtIRBasicBlock* block = blocks[b];
            if (block->pred_count == 0) continue;
            
            for (int j = 0; j < info->block_count; j++) {
                info->dom[b * info->block_count + j] = 1;
            }
            
            int first_pred = -1;
            for (int p = 0; p < block->pred_count; p++) {
                for (int k = 0; k < info->block_count; k++) {
                    if (blocks[k] == block->preds[p]) {
                        first_pred = k;
                        break;
                    }
                }
                if (first_pred >= 0) break;
            }
            
            if (first_pred < 0) continue;
            
            for (int p = 0; p < block->pred_count; p++) {
                int pred_idx = -1;
                for (int k = 0; k < info->block_count; k++) {
                    if (blocks[k] == block->preds[p]) {
                        pred_idx = k;
                        break;
                    }
                }
                if (pred_idx < 0) continue;
                
                for (int j = 0; j < info->block_count; j++) {
                    info->dom[b * info->block_count + j] &= info->dom[pred_idx * info->block_count + j];
                }
            }
            
            info->dom[b * info->block_count + b] = 1;
        }
    }
    
    for (int i = 0; i < info->block_count; i++) {
        if (i == entry_idx) {
            info->idom[i] = -1;
            continue;
        }
        
        KrtIRBasicBlock* block = blocks[i];
        if (block->pred_count == 0) {
            info->idom[i] = -1;
            continue;
        }
        
        int idom = -1;
        for (int p = 0; p < block->pred_count; p++) {
            int pred_idx = -1;
            for (int k = 0; k < info->block_count; k++) {
                if (blocks[k] == block->preds[p]) {
                    pred_idx = k;
                    break;
                }
            }
            if (pred_idx < 0) continue;
            
            if (pred_idx != i && info->dom[pred_idx * info->block_count + i]) {
                if (idom == -1) {
                    idom = pred_idx;
                } else {
                    if (info->dom[idom * info->block_count + pred_idx]) {
                        idom = pred_idx;
                    }
                }
            }
        }
        
        info->idom[i] = idom;
    }
    
    for (int x = 0; x < info->block_count; x++) {
        for (int y = 0; y < info->block_count; y++) {
            KrtIRBasicBlock* y_block = blocks[y];
            if (y_block->pred_count < 2) continue;
            
            for (int p = 0; p < y_block->pred_count; p++) {
                int pred_idx = -1;
                for (int k = 0; k < info->block_count; k++) {
                    if (blocks[k] == y_block->preds[p]) {
                        pred_idx = k;
                        break;
                    }
                }
                if (pred_idx < 0) continue;
                
                if (info->dom[x * info->block_count + pred_idx] && 
                    x != y && !info->dom[x * info->block_count + y]) {
                    bool found = false;
                    for (int d = 0; d < info->df_count[x]; d++) {
                        if (info->df[x][d] == y) {
                            found = true;
                            break;
                        }
                    }
                    if (!found && info->df_count[x] < info->block_count) {
                        info->df[x][info->df_count[x]++] = y;
                    }
                }
            }
        }
    }
    
    return info;
}

void KrtIrDominanceDestroy(KrtIRDominanceInfo* info, KrtIRMemoryArena* arena) {
    (void)info;
    (void)arena;
}

bool KrtIrSsaDominates(KrtIRDominanceInfo* dom, int b1, int b2) {
    if (!dom || b1 < 0 || b2 < 0 || b1 >= dom->block_count || b2 >= dom->block_count) {
        return false;
    }
    return dom->dom[b1 * dom->block_count + b2] != 0;
}

void KrtIrSsaInsertPhisForVar(KrtIRSSABuilder* ssa_builder, KrtIRFunction* func, const char* var_name,
                               KrtIRDominanceInfo* dom_info) {
    if (!ssa_builder || !func || !var_name || !dom_info) return;
    
    int max_blocks = dom_info->block_count;
    KrtIRBasicBlock** blocks = (KrtIRBasicBlock**)KrtIrArenaAlloc(ssa_builder->arena, max_blocks * sizeof(KrtIRBasicBlock*));
    int* block_to_index = (int*)KrtIrArenaAlloc(ssa_builder->arena, (max_blocks + 1024) * sizeof(int));
    if (!blocks || !block_to_index) return;
    
    int actual_count = 0;
    collect_blocks(func, blocks, block_to_index, &actual_count);
    
    bool* has_store = (bool*)KrtIrArenaAlloc(ssa_builder->arena, max_blocks * sizeof(bool));
    bool* has_phi = (bool*)KrtIrArenaAlloc(ssa_builder->arena, max_blocks * sizeof(bool));
    bool* worklist = (bool*)KrtIrArenaAlloc(ssa_builder->arena, max_blocks * sizeof(bool));
    if (!has_store || !has_phi || !worklist) return;
    
    memset(has_store, 0, max_blocks * sizeof(bool));
    memset(has_phi, 0, max_blocks * sizeof(bool));
    memset(worklist, 0, max_blocks * sizeof(bool));
    
    for (int i = 0; i < actual_count; i++) {
        KrtIRBasicBlock* block = blocks[i];
        for (int j = 0; j < block->inst_count; j++) {
            KrtIRInst* inst = block->insts[j];
            if (inst->opcode == KRT_IR_STORE && inst->operand_count > 0) {
                KrtIRValue* val = &inst->operands[0];
                if (val->type == KRT_IR_VALUE_VAR && val->data.name && strcmp(val->data.name, var_name) == 0) {
                    has_store[i] = true;
                    worklist[i] = true;
                    break;
                }
            }
            if (inst->opcode == KRT_IR_COPY && inst->result.type == KRT_IR_VALUE_VAR && inst->result.data.name) {
                if (strcmp(inst->result.data.name, var_name) == 0) {
                    has_store[i] = true;
                    worklist[i] = true;
                    break;
                }
            }
        }
    }
    
    int worklist_queue[SSA_MAX_BLOCKS];
    int worklist_front = 0;
    int worklist_back = 0;
    
    for (int i = 0; i < actual_count; i++) {
        if (worklist[i]) {
            worklist_queue[worklist_back++] = i;
        }
    }
    
    while (worklist_front < worklist_back) {
        int b = worklist_queue[worklist_front++];
        
        for (int d = 0; d < dom_info->df_count[b]; d++) {
            int df_block = dom_info->df[b][d];
            if (df_block < 0 || df_block >= actual_count) continue;
            
            if (!has_phi[df_block]) {
                has_phi[df_block] = true;
                
                KrtIRBasicBlock* target_block = blocks[df_block];
                int pred_count = target_block->pred_count;
                
                KrtIRPhi* phi = KrtIrPhiCreate(ssa_builder->arena, var_name, NULL, pred_count, target_block);
                if (phi) {
                    KrtIrBlockAddPhi(target_block, phi);
                    
                    for (int p = 0; p < pred_count; p++) {
                        int pred_idx = -1;
                        for (int k = 0; k < actual_count; k++) {
                            if (blocks[k] == target_block->preds[p]) {
                                pred_idx = k;
                                break;
                            }
                        }
                        
                        KrtIRValue undef_val = {0};
                        undef_val.type = KRT_IR_VALUE_VOID;
                        KrtIrPhiAddOperand(phi, target_block->preds[p], undef_val, p);
                        
                        if (!has_phi[pred_idx] && !has_store[pred_idx]) {
                            has_phi[pred_idx] = true;
                            worklist_queue[worklist_back++] = pred_idx;
                        }
                    }
                }
            }
        }
    }
}

void KrtIrSsaInsertPhis(KrtIRSSABuilder* ssa_builder, KrtIRFunction* func) {
    if (!ssa_builder || !func) return;
    
    KrtIRDominanceInfo* dom_info = KrtIrComputeDominance(func, ssa_builder->arena);
    if (!dom_info) return;
    
    int var_count = 0;
    char** vars = KrtIrSsaCollectVars(ssa_builder->arena, func, &var_count);
    
    for (int i = 0; i < var_count; i++) {
        KrtIrSsaInsertPhisForVar(ssa_builder, func, vars[i], dom_info);
        KRT_FREE(vars[i]);
    }
    
    KRT_FREE(vars);
}

static KrtIRValue lookup_var(KrtIRSSABuilder* ssa_builder, const char* name, KrtIRBasicBlock* block,
                              KrtIRDominanceInfo* dom_info, KrtIRBasicBlock** blocks, int* block_to_index) {
    KrtIRVarVersion* version = var_table_find(ssa_builder->var_table, name);
    if (version) {
        char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, name, version->version);
        KrtIRValue val = {0};
        val.type = KRT_IR_VALUE_VAR;
        val.data.name = versioned_name;
        return val;
    }
    
    (void)block;
    (void)dom_info;
    (void)blocks;
    (void)block_to_index;
    
    KrtIRValue val = {0};
    val.type = KRT_IR_VALUE_IMM;
    val.data.imm = 0;
    return val;
}

#define RENAME_STACK_MAX 4096

typedef struct {
    KrtIRBasicBlock* block;
    int succ_idx;
} RenameStackFrame;

static void rename_block(KrtIRSSABuilder* ssa_builder, KrtIRBasicBlock* block,
                          KrtIRDominanceInfo* dom_info, KrtIRBasicBlock** blocks, int* block_to_index,
                          int actual_count) {
    if (!block) return;
    
    KrtIRBlockPhiList* phi_list = KrtIrBlockGetPhiList(block);
    KrtIRPhi* phi = phi_list ? phi_list->head : NULL;
    while (phi) {
        KrtIRVarVersion* version = KrtIrVarNewVersion(ssa_builder, phi->var_name, phi->type, block, NULL);
        if (version) {
            char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, phi->var_name, version->version);
            
            KrtIRInst* phi_inst = (KrtIRInst*)KrtIrArenaAlloc(ssa_builder->arena, sizeof(KrtIRInst));
            if (phi_inst) {
                memset(phi_inst, 0, sizeof(KrtIRInst));
                phi_inst->opcode = KRT_IR_NOP;
                phi_inst->result.type = KRT_IR_VALUE_VAR;
                phi_inst->result.data.name = versioned_name;
                phi->inst = phi_inst;
            }
        }
        phi = phi->next;
    }
    
    for (int i = 0; i < block->inst_count; i++) {
        KrtIRInst* inst = block->insts[i];
        if (!inst) continue;

        if (inst->opcode == KRT_IR_ALLOC && inst->operand_count > 0) {
            KrtIRValue* var_val = &inst->operands[0];
            if (var_val->type == KRT_IR_VALUE_VAR && var_val->data.name) {
                KrtIRVarVersion* version = KrtIrVarNewVersion(ssa_builder, var_val->data.name, NULL, block, inst);
                if (version) {
                    char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, var_val->data.name, version->version);
                    var_val->data.name = versioned_name;
                }
            }
        }

        if (inst->opcode == KRT_IR_STORE && inst->operand_count > 0) {
            KrtIRValue* var_val = &inst->operands[0];
            if (var_val->type == KRT_IR_VALUE_VAR && var_val->data.name) {
                KrtIRVarVersion* current_version = var_table_find(ssa_builder->var_table, var_val->data.name);
                if (current_version) {
                    char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, var_val->data.name, current_version->version);
                    var_val->data.name = versioned_name;
                }
            }
        }

        for (int j = 0; j < inst->operand_count; j++) {
            if ((inst->opcode == KRT_IR_STORE || inst->opcode == KRT_IR_ALLOC) && j == 0) {
                continue;
            }

            if (inst->operands[j].type == KRT_IR_VALUE_VAR && inst->operands[j].data.name) {
                KrtIRValue new_val = lookup_var(ssa_builder, inst->operands[j].data.name, block,
                                                 dom_info, blocks, block_to_index);
                inst->operands[j] = new_val;
            }
        }
    }

    for (int s = 0; s < block->succ_count; s++) {
        rename_block(ssa_builder, block->succs[s], dom_info, blocks, block_to_index, actual_count);
    }
}

static void rename_block_iterative(KrtIRSSABuilder* ssa_builder, KrtIRBasicBlock* entry_block,
                                    KrtIRDominanceInfo* dom_info, KrtIRBasicBlock** blocks, int* block_to_index,
                                    int actual_count) {
    if (!entry_block) return;
    
    bool* visited = (bool*)calloc(actual_count, sizeof(bool));
    if (!visited) return;
    
    RenameStackFrame stack[RENAME_STACK_MAX];
    int stack_top = 0;

    stack[stack_top].block = entry_block;
    stack[stack_top].succ_idx = 0;
    stack_top++;
    
    int iterations = 0;
    while (stack_top > 0) {
        iterations++;
        if (iterations > actual_count * 10 + 100) {
            break;
        }
        
        RenameStackFrame* frame = &stack[stack_top - 1];
        KrtIRBasicBlock* block = frame->block;
        
        if (!block) {
            stack_top--;
            continue;
        }
        
        if (frame->succ_idx == 0) {
            int block_idx = block_to_index[block->id];
            if (block_idx >= 0 && block_idx < actual_count) {
                visited[block_idx] = true;
            }
            
            KrtIRBlockPhiList* phi_list = KrtIrBlockGetPhiList(block);
            KrtIRPhi* phi = phi_list ? phi_list->head : NULL;
            while (phi) {
                KrtIRVarVersion* version = KrtIrVarNewVersion(ssa_builder, phi->var_name, phi->type, block, NULL);
                if (version) {
                    char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, phi->var_name, version->version);
                    
                    KrtIRInst* phi_inst = (KrtIRInst*)KrtIrArenaAlloc(ssa_builder->arena, sizeof(KrtIRInst));
                    if (phi_inst) {
                        memset(phi_inst, 0, sizeof(KrtIRInst));
                        phi_inst->opcode = KRT_IR_NOP;
                        phi_inst->result.type = KRT_IR_VALUE_VAR;
                        phi_inst->result.data.name = versioned_name;
                        phi->inst = phi_inst;
                    }
                }
                phi = phi->next;
            }
            
            for (int i = 0; i < block->inst_count; i++) {
                KrtIRInst* inst = block->insts[i];
                if (!inst) continue;

                if (inst->opcode == KRT_IR_ALLOC && inst->operand_count > 0) {
                    KrtIRValue* var_val = &inst->operands[0];
                    if (var_val->type == KRT_IR_VALUE_VAR && var_val->data.name) {
                        KrtIRVarVersion* version = KrtIrVarNewVersion(ssa_builder, var_val->data.name, NULL, block, inst);
                        if (version) {
                            char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, var_val->data.name, version->version);
                            var_val->data.name = versioned_name;
                        }
                    }
                }

                if (inst->opcode == KRT_IR_STORE && inst->operand_count > 0) {
                    KrtIRValue* var_val = &inst->operands[0];
                    if (var_val->type == KRT_IR_VALUE_VAR && var_val->data.name) {
                        KrtIRVarVersion* current_version = var_table_find(ssa_builder->var_table, var_val->data.name);
                        if (current_version) {
                            char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, var_val->data.name, current_version->version);
                            var_val->data.name = versioned_name;
                        }
                    }
                }

                if (inst->opcode == KRT_IR_COPY && inst->result.type == KRT_IR_VALUE_VAR && inst->result.data.name) {
                    KrtIRVarVersion* version = KrtIrVarNewVersion(ssa_builder, inst->result.data.name, NULL, block, inst);
                    if (version) {
                        char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, inst->result.data.name, version->version);
                        inst->result.data.name = versioned_name;
                    }
                }

                for (int j = 0; j < inst->operand_count; j++) {
                    if ((inst->opcode == KRT_IR_STORE || inst->opcode == KRT_IR_ALLOC) && j == 0) {
                        continue;
                    }

                    if (inst->operands[j].type == KRT_IR_VALUE_VAR && inst->operands[j].data.name) {
                        KrtIRValue new_val = lookup_var(ssa_builder, inst->operands[j].data.name, block,
                                                         dom_info, blocks, block_to_index);
                        inst->operands[j] = new_val;
                    }
                }
            }

            for (int s = 0; s < block->succ_count; s++) {
                KrtIRBasicBlock* succ = block->succs[s];
                
                int succ_idx = -1;
                for (int k = 0; k < actual_count; k++) {
                    if (blocks[k] == succ) {
                        succ_idx = k;
                        break;
                    }
                }
                if (succ_idx < 0) continue;
                
                KrtIRBlockPhiList* succ_phi_list = KrtIrBlockGetPhiList(succ);
                if (!succ_phi_list) continue;
                
                int pred_idx_in_succ = -1;
                for (int p = 0; p < succ->pred_count; p++) {
                    if (succ->preds[p] == block) {
                        pred_idx_in_succ = p;
                        break;
                    }
                }
                if (pred_idx_in_succ < 0) continue;
                
                KrtIRPhi* succ_phi = succ_phi_list->head;
                while (succ_phi) {
                    KrtIRVarVersion* version = var_table_find(ssa_builder->var_table, succ_phi->var_name);
                    if (version && pred_idx_in_succ < succ_phi->pred_count) {
                        char* versioned_name = KrtIrVarVersionedName(ssa_builder->arena, succ_phi->var_name, version->version);
                        KrtIRValue val = {0};
                        val.type = KRT_IR_VALUE_VAR;
                        val.data.name = versioned_name;
                        succ_phi->values[pred_idx_in_succ] = val;
                    }
                    succ_phi = succ_phi->next;
                }
            }
        }

        if (frame->succ_idx < block->succ_count) {
            KrtIRBasicBlock* succ = block->succs[frame->succ_idx];
            frame->succ_idx++;
            
            int succ_idx = succ ? block_to_index[succ->id] : -1;
            if (succ_idx >= 0 && succ_idx < actual_count && visited[succ_idx]) {
                continue;
            }
            
            if (stack_top < RENAME_STACK_MAX) {
                stack[stack_top].block = succ;
                stack[stack_top].succ_idx = 0;
                stack_top++;
            } else {
                rename_block(ssa_builder, succ, dom_info, blocks, block_to_index, actual_count);
            }
        } else {
            stack_top--;
        }
    }
    
    free(visited);
}

void KrtIrSsaConstruct(KrtIRSSABuilder* ssa_builder, KrtIRFunction* func) {
    if (!ssa_builder || !func) return;

    if (func->name && strcmp(func->name, "main") == 0) {
        KrtIRBasicBlock* b = func->entry_block;
        int count = 0;
        KrtIRInst* inst = b->first_inst;
        while (inst) {
            inst = inst->next;
        }
    }

    KrtIrSsaInsertPhis(ssa_builder, func);

    KrtIRDominanceInfo* dom_info = KrtIrComputeDominance(func, ssa_builder->arena);
    if (!dom_info) {
        return;
    }

    int max_blocks = count_blocks(func);
    KrtIRBasicBlock** blocks = (KrtIRBasicBlock**)KrtIrArenaAlloc(ssa_builder->arena, max_blocks * sizeof(KrtIRBasicBlock*));
    int* block_to_index = (int*)KrtIrArenaAlloc(ssa_builder->arena, (max_blocks + 1024) * sizeof(int));
    if (!blocks || !block_to_index) return;

    int actual_count = 0;
    collect_blocks(func, blocks, block_to_index, &actual_count);

    rename_block_iterative(ssa_builder, func->entry_block, dom_info, blocks, block_to_index, actual_count);

    if (func->name && strcmp(func->name, "main") == 0) {
        KrtIRBasicBlock* b = func->entry_block;
        int count = 0;
        KrtIRInst* inst = b->first_inst;
        while (inst) {
            inst = inst->next;
        }
    }
}

void KrtIrSsaReplaceVarUses(KrtIRFunction* func, KrtIRMemoryArena* arena) {
    if (!func || !arena) return;
    
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (!inst) continue;
            
            for (int j = 0; j < inst->operand_count; j++) {
                if (inst->operands[j].type == KRT_IR_VALUE_VAR && inst->operands[j].data.name) {
                    const char* name = inst->operands[j].data.name;
                    const char* underscore = strchr(name, '_');

                    if (underscore && underscore != name) {
                        int base_len = (int)(underscore - name);
                        if (base_len > 0) {
                            const char* p = underscore + 1;
                            int is_version = 1;
                            while (*p) {
                                if (*p < '0' || *p > '9') {
                                    is_version = 0;
                                    break;
                                }
                                p++;
                            }
                            
                            if (is_version) {
                                KrtIRValue new_val = {0};
                                new_val.type = KRT_IR_VALUE_VAR;
                                
                                char* base_name = (char*)KrtIrArenaAlloc(arena, base_len + 1);
                                if (base_name) {
                                    strncpy(base_name, name, base_len);
                                    base_name[base_len] = '\0';
                                    new_val.data.name = base_name;
                                    inst->operands[j] = new_val;
                                }
                            }
                        }
                    }
                }
            }
            
            if (inst->result.type == KRT_IR_VALUE_VAR && inst->result.data.name) {
                const char* name = inst->result.data.name;
                const char* underscore = strchr(name, '_');
                
                if (underscore && underscore != name) {
                    int base_len = (int)(underscore - name);
                    if (base_len > 0) {
                        const char* p = underscore + 1;
                        int is_version = 1;
                        while (*p) {
                            if (*p < '0' || *p > '9') {
                                is_version = 0;
                                break;
                            }
                            p++;
                        }
                        
                        if (is_version) {
                            char* base_name = (char*)KrtIrArenaAlloc(arena, base_len + 1);
                            if (base_name) {
                                strncpy(base_name, name, base_len);
                                base_name[base_len] = '\0';
                                inst->result.data.name = base_name;
                            }
                        }
                    }
                }
            }
        }
        block = block->next;
    }
}

void KrtIrSsaRemoveStoreInsts(KrtIRFunction* func) {
    if (!func) return;
    
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        int write_idx = 0;
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (inst && inst->opcode != KRT_IR_STORE) {
                if (write_idx != i) {
                    block->insts[write_idx] = inst;
                }
                write_idx++;
            }
        }
        
        for (int i = write_idx; i < block->inst_count; i++) {
            block->insts[i] = NULL;
        }
        block->inst_count = write_idx;
        
        block = block->next;
    }
}

void KrtIrSsaLowerPhis(KrtIRFunction* func, KrtIRMemoryArena* arena) {
    if (!func || !arena) return;

    if (func->name && strcmp(func->name, "main") == 0) {
        KrtIRBasicBlock* b = func->entry_block;
        int count = 0;
        KrtIRInst* inst = b->first_inst;
        while (inst) {
            fprintf(stderr, "[LowerPhis] Before: main block inst[%d] opcode=%d\n", count++, inst->opcode);
            inst = inst->next;
        }
    }

    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        KrtIRBlockPhiList* phi_list = KrtIrBlockGetPhiList(block);
        if (phi_list) {
            KrtIRPhi* phi = phi_list->head;
            while (phi) {
                if (!phi->inst) {
                    phi = phi->next;
                    continue;
                }
                
                for (int i = 0; i < phi->pred_count; i++) {
                    KrtIRBasicBlock* pred = phi->blocks[i];
                    if (!pred) continue;
                    
                    KrtIRValue* val = &phi->values[i];
                    if (val->type == KRT_IR_VALUE_VOID) {
                        continue;
                    }
                    
                    if (pred->inst_count >= pred->inst_capacity) {
                        pred->inst_capacity = pred->inst_capacity ? pred->inst_capacity * 2 : 8;
                        pred->insts = (KrtIRInst**)KRT_REALLOC(pred->insts, pred->inst_capacity * sizeof(KrtIRInst*));
                        if (!pred->insts) continue;
                    }
                    
                    KrtIRInst* move_inst = (KrtIRInst*)KrtIrArenaAlloc(arena, sizeof(KrtIRInst));
                    if (!move_inst) continue;
                    
                    memset(move_inst, 0, sizeof(KrtIRInst));
                    move_inst->opcode = KRT_IR_COPY;
                    move_inst->operand_count = 1;
                    move_inst->operand_capacity = 4;
                    move_inst->operands = (KrtIRValue*)KrtIrArenaAlloc(arena, 4 * sizeof(KrtIRValue));
                    if (!move_inst->operands) continue;
                    
                    move_inst->operands[0] = *val;
                    move_inst->result = phi->inst->result;

                    int insert_at = pred->inst_count;
                    KrtIRInst* terminator = NULL;
                    if (pred->inst_count > 0) {
                        KrtIRInst* last = pred->insts[pred->inst_count - 1];
                        if (last && (last->opcode == KRT_IR_JUMP ||
                                     last->opcode == KRT_IR_BRANCH ||
                                     last->opcode == KRT_IR_RETURN)) {
                            insert_at = pred->inst_count - 1;
                            terminator = last;
                        }
                    }

                    for (int j = pred->inst_count; j > insert_at; j--) {
                        pred->insts[j] = pred->insts[j - 1];
                    }
                    pred->insts[insert_at] = move_inst;
                    pred->inst_count++;

                    if (terminator) {
                        KrtIRInst* prev = NULL;
                        KrtIRInst* current = pred->first_inst;
                        while (current && current != terminator) {
                            prev = current;
                            current = current->next;
                        }
                        move_inst->next = terminator;
                        if (prev) prev->next = move_inst;
                        else pred->first_inst = move_inst;
                    } else {
                        move_inst->next = NULL;
                        if (pred->last_inst) pred->last_inst->next = move_inst;
                        else pred->first_inst = move_inst;
                        pred->last_inst = move_inst;
                    }
                }
                phi = phi->next;
            }
        }
        block = block->next;
    }
}

bool KrtIrSsaVerify(KrtIRFunction* func) {
    if (!func) return false;
    
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (!inst) continue;
            
            if (inst->opcode == KRT_IR_STORE) {
                return false;
            }
        }
        block = block->next;
    }
    
    return true;
}

static void ssa_eliminate_dead_code(KrtIRFunction* func) {
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        int new_count = 0;
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (!inst) continue;
            
            int is_used = 0;
            if (inst->result.type == KRT_IR_VALUE_TEMP) {
                int temp_idx = inst->result.data.index;
                
                KrtIRBasicBlock* check_block = func->entry_block;
                while (check_block && !is_used) {
                    for (int j = 0; j < check_block->inst_count && !is_used; j++) {
                        KrtIRInst* check_inst = check_block->insts[j];
                        if (!check_inst) continue;
                        
                        for (int k = 0; k < check_inst->operand_count && !is_used; k++) {
                            if (check_inst->operands[k].type == KRT_IR_VALUE_TEMP &&
                                check_inst->operands[k].data.index == temp_idx) {
                                is_used = 1;
                            }
                        }
                    }
                    check_block = check_block->next;
                }
            } else {
                is_used = 1;
            }
            
            if (is_used || inst->opcode == KRT_IR_RETURN ||
                inst->opcode == KRT_IR_CALL || inst->opcode == KRT_IR_SYSCALL ||
                inst->opcode == KRT_IR_BRANCH ||
                inst->opcode == KRT_IR_JUMP || inst->opcode == KRT_IR_STORE ||
                inst->opcode == KRT_IR_ALLOC || inst->opcode == KRT_IR_LOAD) {
                block->insts[new_count++] = inst;
            }
        }
        block->inst_count = new_count;
        block = block->next;
    }
}

static void ssa_copy_propagation(KrtIRFunction* func) {
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (!inst) continue;
            
            if (inst->opcode == KRT_IR_COPY && inst->operand_count == 1 &&
                inst->operands[0].type == KRT_IR_VALUE_TEMP &&
                inst->result.type == KRT_IR_VALUE_TEMP) {
                
                int src_temp = inst->operands[0].data.index;
                int dst_temp = inst->result.data.index;
                
                for (int j = i + 1; j < block->inst_count; j++) {
                    KrtIRInst* later_inst = block->insts[j];
                    if (!later_inst) continue;
                    
                    int redefined = 0;
                    if (later_inst->result.type == KRT_IR_VALUE_TEMP &&
                        (later_inst->result.data.index == src_temp ||
                         later_inst->result.data.index == dst_temp)) {
                        redefined = 1;
                    }
                    
                    if (redefined) break;
                    
                    for (int k = 0; k < later_inst->operand_count; k++) {
                        if (later_inst->operands[k].type == KRT_IR_VALUE_TEMP &&
                            later_inst->operands[k].data.index == src_temp) {
                            later_inst->operands[k].data.index = dst_temp;
                        }
                    }
                }
            }
        }
        block = block->next;
    }
}

static void ssa_constant_propagation(KrtIRFunction* func) {
    KrtIRBasicBlock* block = func->entry_block;
    while (block) {
        for (int i = 0; i < block->inst_count; i++) {
            KrtIRInst* inst = block->insts[i];
            if (!inst) continue;
            
            if (inst->opcode == KRT_IR_COPY && inst->operand_count == 1 &&
                inst->operands[0].type == KRT_IR_VALUE_IMM &&
                inst->result.type == KRT_IR_VALUE_TEMP) {
                
                double const_val = inst->operands[0].data.imm;
                int dst_temp = inst->result.data.index;
                
                for (int j = i + 1; j < block->inst_count; j++) {
                    KrtIRInst* later_inst = block->insts[j];
                    if (!later_inst) continue;
                    
                    if (later_inst->result.type == KRT_IR_VALUE_TEMP &&
                        later_inst->result.data.index == dst_temp) {
                        break;
                    }
                    
                    for (int k = 0; k < later_inst->operand_count; k++) {
                        if (later_inst->operands[k].type == KRT_IR_VALUE_TEMP &&
                            later_inst->operands[k].data.index == dst_temp) {
                            later_inst->operands[k].type = KRT_IR_VALUE_IMM;
                            later_inst->operands[k].data.imm = const_val;
                        }
                    }
                }
            }
        }
        block = block->next;
    }
}

static void ssa_merge_blocks(KrtIRFunction* func) {
    if (!func || !func->entry_block) return;
    
    KrtIRBasicBlock* block = func->entry_block;
    while (block && block->next) {
        if (block->inst_count > 0) {
            KrtIRInst* last_inst = block->insts[block->inst_count - 1];
            if (last_inst && last_inst->opcode == KRT_IR_JUMP &&
                block->succ_count == 1 && block->next->pred_count == 1) {
                
                KrtIRBasicBlock* next_block = block->succs[0];
                if (next_block && next_block->pred_count == 1) {
                    int new_capacity = block->inst_count + next_block->inst_count;
                    if (new_capacity > block->inst_capacity) {
                        block->inst_capacity = new_capacity;
                        block->insts = KRT_REALLOC(block->insts, new_capacity * sizeof(KrtIRInst*));
                    }
                    
                    for (int i = 0; i < next_block->inst_count; i++) {
                        block->insts[block->inst_count++] = next_block->insts[i];
                    }
                    
                    block->succ_count = next_block->succ_count;
                    for (int i = 0; i < next_block->succ_count; i++) {
                        block->succs[i] = next_block->succs[i];
                        if (block->succs[i]) {
                            for (int j = 0; j < block->succs[i]->pred_count; j++) {
                                if (block->succs[i]->preds[j] == next_block) {
                                    block->succs[i]->preds[j] = block;
                                }
                            }
                        }
                    }
                    
                    block->label = next_block->label;
                }
            }
        }
        block = block->next;
    }

    if (func->name && strcmp(func->name, "main") == 0) {
        KrtIRBasicBlock* b = func->entry_block;
        int count = 0;
        KrtIRInst* inst = b->first_inst;
        while (inst) {
            fprintf(stderr, "[LowerPhis] After: main block inst[%d] opcode=%d\n", count++, inst->opcode);
            inst = inst->next;
        }
    }
}

void KrtIrSsaOptimize(KrtIRFunction* func) {
    if (!func) return;

    ssa_constant_propagation(func);
    ssa_copy_propagation(func);
    ssa_eliminate_dead_code(func);
    
    ssa_constant_propagation(func);
    ssa_eliminate_dead_code(func);
}

void KrtIrSsaFullOptimize(KrtIRFunction* func) {
    if (!func) return;
    
    ssa_constant_propagation(func);
    ssa_copy_propagation(func);
    ssa_eliminate_dead_code(func);
    
    ssa_merge_blocks(func);
    
    ssa_constant_propagation(func);
    ssa_copy_propagation(func);
    ssa_eliminate_dead_code(func);
}
