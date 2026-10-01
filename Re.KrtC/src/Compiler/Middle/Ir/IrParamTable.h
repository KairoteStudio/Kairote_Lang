#ifndef KRT_IR_PARAM_TABLE_H
#define KRT_IR_PARAM_TABLE_H

#include "IrMemory.h"
#include "../../Frontend/Parser/Ast.h"

typedef struct KrtIRParamNode {
    char* name;
    KrtTokenType type;
    int index;
    struct KrtIRParamNode* next;
} KrtIRParamNode;

typedef struct KrtIRParamTable {
    KrtIRParamNode** buckets;
    int bucket_count;
    int param_count;
    KrtIRMemoryArena* arena;
} KrtIRParamTable;

KrtIRParamTable* KrtIrParamTableCreate(KrtIRMemoryArena* arena, int initial_bucket_count);

void KrtIrParamTableDestroy(KrtIRParamTable* table);

bool KrtIrParamTableAdd(KrtIRParamTable* table, const char* name, KrtTokenType type, int index);

KrtIRParamNode* KrtIrParamTableFind(KrtIRParamTable* table, const char* name);

int KrtIrParamTableCount(KrtIRParamTable* table);

void KrtIrParamTableForeach(KrtIRParamTable* table, void (*callback)(KrtIRParamNode* node, void* userdata),
                            void* userdata);

KrtIRParamNode* KrtIrFunctionFindParam(KrtIRFunction* func, const char* name);

int KrtIrFunctionGetParamIndex(KrtIRFunction* func, const char* name);

#endif
