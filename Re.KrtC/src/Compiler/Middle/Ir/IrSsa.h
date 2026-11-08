#ifndef KRT_IR_SSA_H
#define KRT_IR_SSA_H

#include "Ir.h"
#include "IrType.h"

typedef struct KrtIRVarVersion {
    char* name;
    int version;
    KrtIRType* type;
    KrtIRBasicBlock* block;
    KrtIRInst* def;
    struct KrtIRVarVersion* next;
} KrtIRVarVersion;

typedef struct {
    KrtIRVarVersion** buckets;
    int bucket_count;
    int var_count;
} KrtIRVarTable;

typedef struct KrtIRPhi {
    char* var_name;
    int version;
    KrtIRType* type;

    KrtIRBasicBlock** blocks;
    KrtIRValue* values;
    int pred_count;

    KrtIRBasicBlock* parent_block;
    KrtIRInst* inst;

    struct KrtIRPhi* next;
} KrtIRPhi;

typedef struct KrtIRBlockPhiList {
    KrtIRPhi* head;
    KrtIRPhi* tail;
} KrtIRBlockPhiList;

typedef struct {
    KrtIRBuilder* builder;
    KrtIRVarTable* var_table;
    KrtIRMemoryArena* arena;

    int* version_counters;
    int var_capacity;

    char** current_var_stack;
    int stack_capacity;
} KrtIRSSABuilder;

typedef struct {
    int* dom;
    int* idom;
    int** df;
    int* df_count;
    int* postorder;
    int postorder_count;
    int* block_to_index;
    int block_count;
} KrtIRDominanceInfo;

KrtIRVarTable* KrtIrVarTableCreate(KrtIRMemoryArena* arena, int bucket_count);
void KrtIrVarTableDestroy(KrtIRVarTable* table);

KrtIRVarVersion* KrtIrVarGetVersion(KrtIRVarTable* table, const char* name);
KrtIRVarVersion* KrtIrVarNewVersion(KrtIRSSABuilder* ssa_builder, const char* name, KrtIRType* type,
                                    KrtIRBasicBlock* block, KrtIRInst* def);

KrtIRVarVersion* KrtIrVarFindVersion(KrtIRVarTable* table, const char* name, KrtIRBasicBlock* block);

char* KrtIrVarVersionedName(KrtIRMemoryArena* arena, const char* name, int version);

KrtIRPhi* KrtIrPhiCreate(KrtIRMemoryArena* arena, const char* var_name, KrtIRType* type, int pred_count,
                         KrtIRBasicBlock* parent);

void KrtIrPhiAddOperand(KrtIRPhi* phi, KrtIRBasicBlock* block, KrtIRValue value, int index);

KrtIRPhi* KrtIrPhiFind(KrtIRBasicBlock* block, const char* var_name);

void KrtIrBlockAddPhi(KrtIRBasicBlock* block, KrtIRPhi* phi);

KrtIRSSABuilder* KrtIrSsaBuilderCreate(KrtIRBuilder* builder);
void KrtIrSsaBuilderDestroy(KrtIRSSABuilder* ssa_builder);

void KrtIrSsaConstruct(KrtIRSSABuilder* ssa_builder, KrtIRFunction* func);

void KrtIrSsaInsertPhis(KrtIRSSABuilder* ssa_builder, KrtIRFunction* func);

void KrtIrSsaRenameVars(KrtIRSSABuilder* ssa_builder, KrtIRBasicBlock* block);

KrtIRDominanceInfo* KrtIrComputeDominance(KrtIRFunction* func, KrtIRMemoryArena* arena);
void KrtIrDominanceDestroy(KrtIRDominanceInfo* info, KrtIRMemoryArena* arena);

int KrtIrComputePostorder(KrtIRFunction* func, int* order, int max_blocks);
void KrtIrComputeIDF(KrtIRDominanceInfo* dom_info, KrtIRFunction* func);

bool KrtIrSsaDominates(KrtIRDominanceInfo* dom, int b1, int b2);

bool KrtIrSsaVerify(KrtIRFunction* func);

char** KrtIrSsaCollectVars(KrtIRMemoryArena* arena, KrtIRFunction* func, int* count);

void KrtIrSsaReplaceVarUses(KrtIRFunction* func, KrtIRMemoryArena* arena);

void KrtIrSsaRemoveStoreInsts(KrtIRFunction* func);

void KrtIrSsaLowerPhis(KrtIRFunction* func, KrtIRMemoryArena* arena);

KrtIRBlockPhiList* KrtIrBlockGetPhiList(KrtIRBasicBlock* block);

void KrtIrSsaOptimize(KrtIRFunction* func);

void KrtIrSsaFullOptimize(KrtIRFunction* func);

#endif
