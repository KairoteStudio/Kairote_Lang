#include "Ir.h"
#include <string.h>

static size_t hash_string(const char* str) {
    size_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

KrtIRParamTable* KrtIrParamTableCreate(KrtIRMemoryArena* arena, int initial_bucket_count) {
    if (!arena || initial_bucket_count <= 0) return NULL;
    
    int bucket_count = 1;
    while (bucket_count < initial_bucket_count) {
        bucket_count <<= 1;
    }
    
    KrtIRParamTable* table = (KrtIRParamTable*)KrtIrArenaAlloc(arena, sizeof(KrtIRParamTable));
    if (!table) return NULL;
    
    table->buckets = (KrtIRParamNode**)KrtIrArenaAlloc(arena, bucket_count * sizeof(KrtIRParamNode*));
    if (!table->buckets) return NULL;
    
    memset(table->buckets, 0, bucket_count * sizeof(KrtIRParamNode*));
    
    table->bucket_count = bucket_count;
    table->param_count = 0;
    table->arena = arena;
    
    return table;
}

bool KrtIrParamTableAdd(KrtIRParamTable* table, const char* name, KrtTokenType type, int index) {
    if (!table || !name) return false;
    
    if (KrtIrParamTableFind(table, name)) {
        return false; 
    }
    
    size_t hash = hash_string(name);
    int bucket_index = hash & (table->bucket_count - 1);
    
    KrtIRParamNode* node = (KrtIRParamNode*)KrtIrArenaAlloc(table->arena, sizeof(KrtIRParamNode));
    if (!node) return false;
    
    node->name = KrtIrArenaStrdup(table->arena, name);
    if (!node->name) return false;
    
    node->type = type;
    node->index = index;
    node->next = table->buckets[bucket_index];
    
    table->buckets[bucket_index] = node;
    table->param_count++;
    
    return true;
}

KrtIRParamNode* KrtIrParamTableFind(KrtIRParamTable* table, const char* name) {
    if (!table || !name) return NULL;
    
    size_t hash = hash_string(name);
    int bucket_index = hash & (table->bucket_count - 1);
    
    KrtIRParamNode* node = table->buckets[bucket_index];
    while (node) {
        if (strcmp(node->name, name) == 0) {
            return node;
        }
        node = node->next;
    }
    
    return NULL;
}

int KrtIrParamTableCount(KrtIRParamTable* table) {
    return table ? table->param_count : 0;
}

void KrtIrParamTableForeach(KrtIRParamTable* table, void (*callback)(KrtIRParamNode* node, void* userdata), void* userdata) {
    if (!table || !callback) return;
    
    for (int i = 0; i < table->bucket_count; i++) {
        KrtIRParamNode* node = table->buckets[i];
        while (node) {
            callback(node, userdata);
            node = node->next;
        }
    }
}

KrtIRParamNode* KrtIrFunctionFindParam(KrtIRFunction* func, const char* name) {
    if (!func || !name) return NULL;
    
    if (func->param_table) {
        return KrtIrParamTableFind(func->param_table, name);
    }
    
    for (int i = 0; i < func->param_count; i++) {
        if (func->params[i].name && strcmp(func->params[i].name, name) == 0) {
            KrtIRParamNode* temp_node = (KrtIRParamNode*)KRT_CALLOC(1, sizeof(KrtIRParamNode));
            if (!temp_node) return NULL;
            temp_node->name = func->params[i].name;
            temp_node->type = func->params[i].type;
            temp_node->index = i;
            temp_node->next = NULL;
            return temp_node;
        }
    }
    
    return NULL;
}

int KrtIrFunctionGetParamIndex(KrtIRFunction* func, const char* name) {
    KrtIRParamNode* param = KrtIrFunctionFindParam(func, name);
    if (!param) return -1;
    int index = param->index;
    KRT_FREE(param);
    return index;
}