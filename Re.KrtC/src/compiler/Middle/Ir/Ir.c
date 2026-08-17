#include "Ir.h"
#include "IrMemory.h"
#include "Core/Memory/Allocator.h"
#include "Core/Utils/KrtString.h"
#include "Core/Core.h"
#include <string.h>
#include <stdio.h>

#define VAR_TABLE_SIZE 256

typedef struct KrtIRVarEntry {
    char* name;
    int current_version;
    struct KrtIRVarEntry* next;
} KrtIRVarEntry;

typedef struct {
    KrtIRVarEntry* buckets[VAR_TABLE_SIZE];
} KrtIRVarTable;

static unsigned int hash_var_name(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

static KrtIRVarTable* var_table_create(KrtIRMemoryArena* arena) {
    KrtIRVarTable* table = (KrtIRVarTable*)KrtIrArenaAlloc(arena, sizeof(KrtIRVarTable));
    if (table) {
        memset(table->buckets, 0, sizeof(table->buckets));
    }
    return table;
}

static int var_table_get_version(KrtIRVarTable* table, const char* name) {
    if (!table || !name) return 0;
    unsigned int idx = hash_var_name(name) % VAR_TABLE_SIZE;
    KrtIRVarEntry* entry = table->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->current_version;
        }
        entry = entry->next;
    }
    return 0;
}

static int var_table_next_version(KrtIRVarTable* table, const char* name, KrtIRMemoryArena* arena) {
    if (!table || !name) return 0;
    unsigned int idx = hash_var_name(name) % VAR_TABLE_SIZE;
    KrtIRVarEntry* entry = table->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return ++entry->current_version;
        }
        entry = entry->next;
    }
    entry = (KrtIRVarEntry*)KrtIrArenaAlloc(arena, sizeof(KrtIRVarEntry));
    entry->name = KrtIrArenaStrdup(arena, name);
    entry->current_version = 1;
    entry->next = table->buckets[idx];
    table->buckets[idx] = entry;
    return 1;
}

static char* make_versioned_name(KrtIRMemoryArena* arena, const char* name, int version) {
    char buffer[256];
    if (version == 0) {
        return KrtIrArenaStrdup(arena, name);
    }
    snprintf(buffer, sizeof(buffer), "%s_%d", name, version);
    return KrtIrArenaStrdup(arena, buffer);
}

KrtIRModule* KrtIrModuleCreate(void) {
    KrtIRModule* module = (KrtIRModule*)KRT_CALLOC(1, sizeof(KrtIRModule));
    if (module) {
        memset(module, 0, sizeof(KrtIRModule));
    }
    return module;
}

void KrtIrModuleDestroy(KrtIRModule* module) {
    if (!module) return;
    
    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRFunction* next = func->next;
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRBasicBlock* block_next = block->next;
            KRT_FREE(block);
            block = block_next;
        }
        KRT_FREE(func);
        func = next;
    }
    
    for (int i = 0; i < module->string_const_count; i++) {
        KRT_FREE(module->string_constants[i]);
    }
    KRT_FREE(module->string_constants);
    
    for (int i = 0; i < module->global_count; i++) {
        KRT_FREE(module->globals[i].name);
    }
    KRT_FREE(module->globals);
    
    KRT_FREE(module);
}

KrtIRFunction* KrtIrFunctionCreate(KrtIRBuilder* builder, const char* name, KrtIRParam* params, int param_count, KrtTokenType return_type) {
    if (!builder || !name) return NULL;
    
    KrtIRFunction* func = (KrtIRFunction*)KRT_CALLOC(1, sizeof(KrtIRFunction));
    if (!func) return NULL;
    
    func->name = KRT_STRDUP(name);
    func->return_type = return_type;
    func->param_count = param_count;
    
    if (param_count > 0 && params) {
        func->params = (KrtIRParam*)KRT_MALLOC(param_count * sizeof(KrtIRParam));
        if (func->params) {
            for (int i = 0; i < param_count; i++) {
                func->params[i].name = KRT_STRDUP(params[i].name);
                func->params[i].type = params[i].type;
                func->params[i].is_params = params[i].is_params;
            }
        }
    }
    
    if (builder->module) {
        KrtIRFunction* prev = NULL;
        KrtIRFunction* curr = builder->module->functions;
        while (curr) {
            prev = curr;
            curr = curr->next;
        }
        if (prev) {
            prev->next = func;
        } else {
            builder->module->functions = func;
        }
    }
    
    return func;
}

void KrtIrFunctionSetEntry(KrtIRBuilder* builder, KrtIRFunction* func) {
    if (!builder || !func) return;
    builder->current_function = func;
    
    KrtIRBasicBlock* entry = KrtIrBlockCreate(builder, "entry");
    func->entry_block = entry;
    KrtIrBlockSetCurrent(builder, entry);
}

KrtIRBasicBlock* KrtIrBlockCreate(KrtIRBuilder* builder, const char* label) {
    if (!builder || !label) return NULL;
    
    KrtIRBasicBlock* block = (KrtIRBasicBlock*)KRT_CALLOC(1, sizeof(KrtIRBasicBlock));
    if (!block) return NULL;
    
    block->label = KRT_STRDUP(label);
    block->id = builder->block_id_counter++;
    
    block->inst_capacity = 32;
    block->insts = (KrtIRInst**)KRT_MALLOC(block->inst_capacity * sizeof(KrtIRInst*));
    if (!block->insts) {
        KRT_FREE(block->label);
        KRT_FREE(block);
        return NULL;
    }
    
    block->pred_capacity = 4;
    block->preds = (KrtIRBasicBlock**)KRT_MALLOC(block->pred_capacity * sizeof(KrtIRBasicBlock*));
    block->succ_capacity = 4;
    block->succs = (KrtIRBasicBlock**)KRT_MALLOC(block->succ_capacity * sizeof(KrtIRBasicBlock*));
    
    if (builder->current_function) {
        KrtIRBasicBlock* prev = NULL;
        KrtIRBasicBlock* curr = builder->current_function->entry_block;
        while (curr) {
            prev = curr;
            curr = curr->next;
        }
        if (prev) {
            prev->next = block;
        } else {
            builder->current_function->entry_block = block;
        }
    }
    
    return block;
}

void KrtIrBlockSetCurrent(KrtIRBuilder* builder, KrtIRBasicBlock* block) {
    if (!builder) return;
    builder->current_block = block;
    if (block && builder->current_function && !builder->current_function->entry_block) {
        builder->current_function->entry_block = block;
    }
}

int KrtIrBlockGetInstCount(KrtIRBasicBlock* block) {
    return block ? block->inst_count : 0;
}

KrtIRInst* KrtIrBlockGetInst(KrtIRBasicBlock* block, int index) {
    if (!block || index < 0 || index >= block->inst_count) return NULL;
    return block->insts[index];
}

KrtIRInst* KrtIrBlockGetFirstInst(KrtIRBasicBlock* block) {
    return (block && block->inst_count > 0) ? block->insts[0] : NULL;
}

KrtIRInst* KrtIrBlockGetLastInst(KrtIRBasicBlock* block) {
    return (block && block->inst_count > 0) ? block->insts[block->inst_count - 1] : NULL;
}

void KrtIrBlockAddPred(KrtIRBuilder* builder, KrtIRBasicBlock* block, KrtIRBasicBlock* pred) {
    (void)builder;
    if (!block || !pred) return;
    if (block->pred_count >= block->pred_capacity) {
        block->pred_capacity *= 2;
        block->preds = (KrtIRBasicBlock**)KRT_REALLOC(block->preds, block->pred_capacity * sizeof(KrtIRBasicBlock*));
    }
    block->preds[block->pred_count++] = pred;
}

void KrtIrBlockAddSucc(KrtIRBuilder* builder, KrtIRBasicBlock* block, KrtIRBasicBlock* succ) {
    (void)builder;
    if (!block || !succ) return;
    if (block->succ_count >= block->succ_capacity) {
        block->succ_capacity *= 2;
        block->succs = (KrtIRBasicBlock**)KRT_REALLOC(block->succs, block->succ_capacity * sizeof(KrtIRBasicBlock*));
    }
    block->succs[block->succ_count++] = succ;
}

int KrtIrBlockGetPredCount(KrtIRBasicBlock* block) {
    return block ? block->pred_count : 0;
}

int KrtIrBlockGetSuccCount(KrtIRBasicBlock* block) {
    return block ? block->succ_count : 0;
}

KrtIRBasicBlock* KrtIrBlockGetPred(KrtIRBasicBlock* block, int index) {
    if (!block || index < 0 || index >= block->pred_count) return NULL;
    return block->preds[index];
}

KrtIRBasicBlock* KrtIrBlockGetSucc(KrtIRBasicBlock* block, int index) {
    if (!block || index < 0 || index >= block->succ_count) return NULL;
    return block->succs[index];
}

void KrtIrBlockInvalidateCache(KrtIRBasicBlock* block) {
    (void)block;
}

KrtIRInst* KrtIrBlockFindCachedInst(KrtIRBasicBlock* block, KrtIROpcode opcode) {
    if (!block) return NULL;
    for (int i = 0; i < block->inst_count; i++) {
        if (block->insts[i] && block->insts[i]->opcode == opcode) {
            return block->insts[i];
        }
    }
    return NULL;
}

static KrtIRInst* ir_create_inst(KrtIRBuilder* builder, KrtIROpcode opcode) {
    if (!builder || !builder->current_block) return NULL;

    KrtIRBasicBlock* block = builder->current_block;

    if (builder->current_function && builder->current_function->name && strcmp(builder->current_function->name, "main") == 0) {
        fprintf(stderr, "[Ir] create_inst: opcode=%d, block=%p, block->label=%s, inst_count=%d\n",
                opcode, (void*)block, block->label, block->inst_count);
    }
    
    if (block->inst_count >= block->inst_capacity) {
        block->inst_capacity *= 2;
        block->insts = (KrtIRInst**)KRT_REALLOC(block->insts, block->inst_capacity * sizeof(KrtIRInst*));
        if (!block->insts) return NULL;
    }
    
    KrtIRInst* inst = (KrtIRInst*)KRT_CALLOC(1, sizeof(KrtIRInst));
    if (!inst) return NULL;
    
    inst->opcode = opcode;
    inst->operand_capacity = 4;
    inst->operands = (KrtIRValue*)KRT_MALLOC(inst->operand_capacity * sizeof(KrtIRValue));
    if (!inst->operands) {
        KRT_FREE(inst);
        return NULL;
    }
    
    inst->result.type = KRT_IR_VALUE_TEMP;
    inst->result.data.index = builder->temp_counter++;
    
    block->insts[block->inst_count++] = inst;

    if (block->last_inst) {
        block->last_inst->next = inst;
    } else {
        block->first_inst = inst;
    }
    block->last_inst = inst;

    return inst;
}

static KrtIRValue ir_add_operand(KrtIRInst* inst, KrtIRValue val) {
    if (!inst) return val;
    if (inst->operand_count >= inst->operand_capacity) {
        inst->operand_capacity *= 2;
        inst->operands = (KrtIRValue*)KRT_REALLOC(inst->operands, inst->operand_capacity * sizeof(KrtIRValue));
    }
    inst->operands[inst->operand_count++] = val;
    return val;
}

void KrtIrStore(KrtIRBuilder* builder, const char* name, KrtIRValue value) {
    if (!builder || !name || !builder->current_block) return;

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_COPY);
    if (inst) {
        inst->result.type = KRT_IR_VALUE_VAR;
        inst->result.data.name = KrtIrArenaStrdup(builder->arena, name);
        ir_add_operand(inst, value);
    }
}

KrtIRValue KrtIrLoad(KrtIRBuilder* builder, const char* name) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_IMM;
    result.data.imm = 0;

    if (!builder || !name) return result;

    result.type = KRT_IR_VALUE_VAR;
    result.data.name = KrtIrArenaStrdup(builder->arena, name);
    return result;
}

void KrtIrAlloc(KrtIRBuilder* builder, const char* name) {
    if (!builder || !name) return;
    
    KrtIRVarTable* table = (KrtIRVarTable*)builder->extensions;
    unsigned int idx = hash_var_name(name) % VAR_TABLE_SIZE;
    KrtIRVarEntry* entry = table->buckets[idx];
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return;
        }
        entry = entry->next;
    }
    
    entry = (KrtIRVarEntry*)KrtIrArenaAlloc(builder->arena, sizeof(KrtIRVarEntry));
    entry->name = KrtIrArenaStrdup(builder->arena, name);
    entry->current_version = 0;
    entry->next = table->buckets[idx];
    table->buckets[idx] = entry;
}

KrtIRValue KrtIrAdd(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_ADD);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrSub(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_SUB);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrMul(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_MUL);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrDiv(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_DIV);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
        if (builder->current_function) {
            builder->current_function->uses_division = true;
        }
    }
    return result;
}

KrtIRValue KrtIrMod(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_MOD);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
        if (builder->current_function) {
            builder->current_function->uses_modulo = true;
        }
    }
    return result;
}

KrtIRValue KrtIrAnd(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_AND);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrOr(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_OR);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrXor(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_XOR);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrLshift(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_LSHIFT);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrRshift(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_RSHIFT);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrPow(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_POW);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrCompare(KrtIRBuilder* builder, KrtIROpcode op, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, op);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrPhi(KrtIRBuilder* builder, KrtIRValue* values, KrtIRBasicBlock** blocks, int count) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block || !values || !blocks || count <= 0) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_PHI);
    if (inst) {
        for (int i = 0; i < count; i++) {
            ir_add_operand(inst, values[i]);
        }
        result.data.index = inst->result.data.index;
    }
    return result;
}

void KrtIrJump(KrtIRBuilder* builder, KrtIRBasicBlock* target) {
    if (!builder || !builder->current_block || !target) return;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_JUMP);
    if (inst) {
        KrtIrBlockAddSucc(builder, builder->current_block, target);
        KrtIrBlockAddPred(builder, target, builder->current_block);
    }
}

void KrtIrBranch(KrtIRBuilder* builder, KrtIRValue cond, KrtIRBasicBlock* true_block, KrtIRBasicBlock* false_block) {
    if (!builder || !builder->current_block || !true_block || !false_block) return;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_BRANCH);
    if (inst) {
        ir_add_operand(inst, cond);
        KrtIrBlockAddSucc(builder, builder->current_block, true_block);
        KrtIrBlockAddSucc(builder, builder->current_block, false_block);
        KrtIrBlockAddPred(builder, true_block, builder->current_block);
        KrtIrBlockAddPred(builder, false_block, builder->current_block);
    }
}

void KrtIrReturn(KrtIRBuilder* builder, KrtIRValue value) {
    if (!builder || !builder->current_block) return;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_RETURN);
    if (inst) {
        ir_add_operand(inst, value);
    }
}

void KrtIrLabel(KrtIRBuilder* builder, const char* label) {
    if (!builder || !label) return;
    
    KrtIRBasicBlock* block = KrtIrBlockCreate(builder, label);
    KrtIrBlockSetCurrent(builder, block);
}

void KrtIrNop(KrtIRBuilder* builder) {
    if (!builder || !builder->current_block) return;
    
    ir_create_inst(builder, KRT_IR_NOP);
}

KrtIRValue KrtIrCall(KrtIRBuilder* builder, const char* func_name, KrtIRValue* args, int arg_count) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !func_name || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_CALL);
    if (inst) {
        KrtIRValue name_val;
        name_val.type = KRT_IR_VALUE_FUNCTION;
        name_val.data.function_name = KRT_STRDUP(func_name);
        ir_add_operand(inst, name_val);
        
        for (int i = 0; i < arg_count; i++) {
            ir_add_operand(inst, args[i]);
        }
        
        result.data.index = inst->result.data.index;
        
        if (builder->current_function) {
            builder->current_function->has_calls = 1;
        }
    }
    return result;
}

KrtIRValue KrtIrSyscall(KrtIRBuilder* builder, KrtIRValue syscall_num, KrtIRValue* args, int arg_count) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) return result;

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_SYSCALL);
    if (inst) {
        ir_add_operand(inst, syscall_num);
        for (int i = 0; i < arg_count && i < 6; i++) {
            ir_add_operand(inst, args[i]);
        }
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrImm(KrtIRBuilder* builder, double value) {
    (void)builder;
    KrtIRValue result;
    result.type = KRT_IR_VALUE_IMM;
    result.data.imm = value;
    return result;
}

KrtIRValue KrtIrVar(KrtIRBuilder* builder, const char* name) {
    if (builder && name) {
        return KrtIrLoad(builder, name);
    }
    KrtIRValue result;
    result.type = KRT_IR_VALUE_VAR;
    result.data.name = (char*)name;
    return result;
}

KrtIRValue KrtIrTemp(KrtIRBuilder* builder) {
    KrtIRValue result;
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    return result;
}

KrtIRValue KrtIrArg(KrtIRBuilder* builder, int index) {
    (void)builder;
    KrtIRValue result;
    result.type = KRT_IR_VALUE_ARG;
    result.data.index = index;
    return result;
}

KrtIRValue KrtIrStringConst(KrtIRBuilder* builder, const char* str) {
    if (!builder || !builder->module || !str) {
        KrtIRValue result;
        result.type = KRT_IR_VALUE_STRING_CONST;
        result.data.string_const_id = -1;
        return result;
    }
    
    if (builder->module->string_const_count >= builder->module->string_const_capacity) {
        builder->module->string_const_capacity = builder->module->string_const_capacity == 0 ? 16 : builder->module->string_const_capacity * 2;
        builder->module->string_constants = (char**)KRT_REALLOC(builder->module->string_constants, builder->module->string_const_capacity * sizeof(char*));
    }
    
    int id = builder->module->string_const_count;
    builder->module->string_constants[id] = KRT_STRDUP(str);
    builder->module->string_const_count++;
    
    KrtIRValue result;
    result.type = KRT_IR_VALUE_STRING_CONST;
    result.data.string_const_id = id;
    return result;
}

KrtIRValue KrtIrLoadPtr(KrtIRBuilder* builder, KrtIRValue base, int offset) {
    return KrtIrLoadPtrSized(builder, base, offset, 8);
}

KrtIRValue KrtIrLoadPtrSized(KrtIRBuilder* builder, KrtIRValue base, int offset, int size) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_LOADPTR);
    if (inst) {
        ir_add_operand(inst, base);
        KrtIRValue offset_val;
        offset_val.type = KRT_IR_VALUE_IMM;
        offset_val.data.imm = (double)offset;
        ir_add_operand(inst, offset_val);
        KrtIRValue size_val;
        size_val.type = KRT_IR_VALUE_IMM;
        size_val.data.imm = (double)size;
        ir_add_operand(inst, size_val);
        result.data.index = inst->result.data.index;
    }
    return result;
}

void KrtIrStorePtr(KrtIRBuilder* builder, KrtIRValue base, int offset, KrtIRValue value) {
    KrtIrStorePtrSized(builder, base, offset, value, 8);
}

void KrtIrStorePtrSized(KrtIRBuilder* builder, KrtIRValue base, int offset, KrtIRValue value, int size) {
    if (!builder || !builder->current_block) return;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_STOREPTR);
    if (inst) {
        ir_add_operand(inst, base);
        KrtIRValue offset_val;
        offset_val.type = KRT_IR_VALUE_IMM;
        offset_val.data.imm = (double)offset;
        ir_add_operand(inst, offset_val);
        ir_add_operand(inst, value);
        KrtIRValue size_val;
        size_val.type = KRT_IR_VALUE_IMM;
        size_val.data.imm = (double)size;
        ir_add_operand(inst, size_val);
    }
}

void KrtIrArrayStore(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index, KrtIRValue value) {
    KrtIrArrayStoreSized(builder, array, index, value, 8);
}

void KrtIrArrayStoreSized(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index, KrtIRValue value, int element_size) {
    if (!builder || !builder->current_block) return;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_ARRAY_STORE);
    if (inst) {
        ir_add_operand(inst, array);
        ir_add_operand(inst, index);
        ir_add_operand(inst, value);
        KrtIRValue size_val;
        size_val.type = KRT_IR_VALUE_IMM;
        size_val.data.imm = (double)element_size;
        ir_add_operand(inst, size_val);
    }
}

KrtIRValue KrtIrStrcat(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_STRCAT);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrIntToString(KrtIRBuilder* builder, KrtIRValue value) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_INT_TO_STRING);
    if (inst) {
        ir_add_operand(inst, value);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrDoubleToString(KrtIRBuilder* builder, KrtIRValue value) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_DOUBLE_TO_STRING);
    if (inst) {
        ir_add_operand(inst, value);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRValue KrtIrCast(KrtIRBuilder* builder, KrtIRValue value, KrtTokenType target_type) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    
    if (!builder || !builder->current_block) return result;
    
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_CAST);
    if (inst) {
        ir_add_operand(inst, value);
        KrtIRValue type_val;
        type_val.type = KRT_IR_VALUE_IMM;
        type_val.data.imm = (double)target_type;
        ir_add_operand(inst, type_val);
        result.data.index = inst->result.data.index;
    }
    return result;
}

KrtIRGlobal* KrtIrModuleAddGlobal(KrtIRBuilder* builder, const char* name, KrtTokenType type) {
    if (!builder || !builder->module || !name) return NULL;
    
    if (builder->module->global_count >= builder->module->global_capacity) {
        builder->module->global_capacity = builder->module->global_capacity == 0 ? 16 : builder->module->global_capacity * 2;
        builder->module->globals = (KrtIRGlobal*)KRT_REALLOC(builder->module->globals, builder->module->global_capacity * sizeof(KrtIRGlobal));
    }
    
    int idx = builder->module->global_count++;
    builder->module->globals[idx].name = KRT_STRDUP(name);
    builder->module->globals[idx].type = type;
    builder->module->globals[idx].has_initializer = 0;
    builder->module->globals[idx].init_number = 0.0;
    
    return &builder->module->globals[idx];
}

KrtIRGlobal* KrtIrModuleFindGlobal(KrtIRModule* module, const char* name) {
    if (!module || !name) return NULL;
    
    for (int i = 0; i < module->global_count; i++) {
        if (strcmp(module->globals[i].name, name) == 0) {
            return &module->globals[i];
        }
    }
    return NULL;
}

void KrtIrModuleSetGlobalNumberInitializer(KrtIRGlobal* global, double value) {
    if (!global) return;
    global->has_initializer = 1;
    global->init_number = value;
}

void KrtIrPushLoopContext(KrtIRBuilder* builder, KrtIRBasicBlock* continue_block, KrtIRBasicBlock* break_block) {
    if (!builder) return;
    
    if (builder->loop_stack_size >= builder->loop_stack_capacity) {
        builder->loop_stack_capacity = builder->loop_stack_capacity == 0 ? 8 : builder->loop_stack_capacity * 2;
        builder->loop_continue_blocks = (KrtIRBasicBlock**)KRT_REALLOC(builder->loop_continue_blocks, builder->loop_stack_capacity * sizeof(KrtIRBasicBlock*));
        builder->loop_break_blocks = (KrtIRBasicBlock**)KRT_REALLOC(builder->loop_break_blocks, builder->loop_stack_capacity * sizeof(KrtIRBasicBlock*));
    }
    
    builder->loop_continue_blocks[builder->loop_stack_size] = continue_block;
    builder->loop_break_blocks[builder->loop_stack_size] = break_block;
    builder->loop_stack_size++;
}

void KrtIrPopLoopContext(KrtIRBuilder* builder) {
    if (!builder || builder->loop_stack_size <= 0) return;
    builder->loop_stack_size--;
}

KrtIRBasicBlock* KrtIrGetCurrentContinueBlock(KrtIRBuilder* builder) {
    if (!builder || builder->loop_stack_size <= 0) return NULL;
    return builder->loop_continue_blocks[builder->loop_stack_size - 1];
}

KrtIRBasicBlock* KrtIrGetCurrentBreakBlock(KrtIRBuilder* builder) {
    if (!builder || builder->loop_stack_size <= 0) return NULL;
    return builder->loop_break_blocks[builder->loop_stack_size - 1];
}

KrtIRBuilder* KrtIrBuilderCreate(void) {
    KrtIRBuilder* builder = (KrtIRBuilder*)KRT_CALLOC(1, sizeof(KrtIRBuilder));
    if (!builder) return NULL;
    
    builder->arena = KrtIrArenaCreate(0);
    if (!builder->arena) {
        KRT_FREE(builder);
        return NULL;
    }
    
    builder->module = KrtIrModuleCreate();
    if (!builder->module) {
        KrtIrArenaDestroy(builder->arena);
        KRT_FREE(builder);
        return NULL;
    }
    
    builder->temp_counter = 0;
    builder->label_counter = 0;
    builder->block_id_counter = 0;
    
    builder->extensions = var_table_create(builder->arena);
    
    return builder;
}

void KrtIrBuilderDestroy(KrtIRBuilder* builder) {
    if (!builder) return;
    
    if (builder->arena) {
        KrtIrArenaDestroy(builder->arena);
    }
    if (builder->module) {
        KrtIrModuleDestroy(builder->module);
    }
    
    if (builder->loop_continue_blocks) KRT_FREE(builder->loop_continue_blocks);
    if (builder->loop_break_blocks) KRT_FREE(builder->loop_break_blocks);
    if (builder->class_name_stack) {
        for (int i = 0; i < builder->class_stack_size; i++) {
            KRT_FREE(builder->class_name_stack[i]);
        }
        KRT_FREE(builder->class_name_stack);
    }
    if (builder->namespace_stack) {
        for (int i = 0; i < builder->namespace_stack_size; i++) {
            KRT_FREE(builder->namespace_stack[i]);
        }
        KRT_FREE(builder->namespace_stack);
    }
    
    KRT_FREE(builder);
}

void KrtIrRegisterClassLayout(KrtIRBuilder* builder, const char* class_name, ASTNode* class_body) {
    if (!builder || !class_name) return;
    
    if (builder->layout_count >= builder->layout_capacity) {
        builder->layout_capacity = builder->layout_capacity == 0 ? 8 : builder->layout_capacity * 2;
        builder->layouts = (KrtIRClassLayout*)KRT_REALLOC(builder->layouts, builder->layout_capacity * sizeof(KrtIRClassLayout));
    }
    
    int idx = builder->layout_count++;
    builder->layouts[idx].class_name = KRT_STRDUP(class_name);
    builder->layouts[idx].field_count = 0;
    builder->layouts[idx].field_capacity = 8;
    builder->layouts[idx].fields = (KrtIRFieldOffset*)KRT_MALLOC(builder->layouts[idx].field_capacity * sizeof(KrtIRFieldOffset));
    
    if (class_body && class_body->type == AST_BLOCK) {
        for (int i = 0; i < class_body->data.block.statement_count; i++) {
            ASTNode* stmt = class_body->data.block.statements[i];
            if (!stmt) continue;
            
            if (stmt->type == AST_ACCESS_MODIFIER) {
                stmt = stmt->data.access_modifier.member;
                if (!stmt) continue;
            }
            
            if (stmt->type == AST_VARIABLE_DECLARATION) {
                if (builder->layouts[idx].field_count >= builder->layouts[idx].field_capacity) {
                    builder->layouts[idx].field_capacity *= 2;
                    builder->layouts[idx].fields = (KrtIRFieldOffset*)KRT_REALLOC(builder->layouts[idx].fields, builder->layouts[idx].field_capacity * sizeof(KrtIRFieldOffset));
                }
                
                int fidx = builder->layouts[idx].field_count++;
                builder->layouts[idx].fields[fidx].name = KRT_STRDUP(stmt->data.variable_decl.name);
                builder->layouts[idx].fields[fidx].offset = fidx * 8;
            }
        }
    }
}

int KrtIrLayoutGetOffset(KrtIRBuilder* builder, const char* class_name, const char* field_name) {
    if (!builder || !class_name || !field_name) return -1;
    
    for (int i = 0; i < builder->layout_count; i++) {
        if (strcmp(builder->layouts[i].class_name, class_name) == 0) {
            for (int j = 0; j < builder->layouts[i].field_count; j++) {
                if (strcmp(builder->layouts[i].fields[j].name, field_name) == 0) {
                    return builder->layouts[i].fields[j].offset;
                }
            }
        }
    }
    return -1;
}

int KrtIrLayoutGetSize(KrtIRBuilder* builder, const char* class_name) {
    if (!builder || !class_name) return 0;
    
    for (int i = 0; i < builder->layout_count; i++) {
        if (strcmp(builder->layouts[i].class_name, class_name) == 0) {
            return builder->layouts[i].field_count * 8;
        }
    }
    return 0;
}

static const char* ir_opcode_names[] = {
    "load", "store", "alloc", "imm", "add", "sub", "mul", "div", "mod",
    "and", "or", "xor", "lshift", "rshift", "pow",
    "lt", "gt", "eq", "le", "ge", "ne",
    "jump", "branch", "call", "return", "label",
    "strcat", "cast", "loadptr", "storeptr", "array_store",
    "int_to_string", "double_to_string", "copy", "syscall", "phi", "nop"
};

static void print_value(FILE* out, KrtIRValue* val) {
    if (!val) {
        fprintf(out, "?");
        return;
    }
    switch (val->type) {
        case KRT_IR_VALUE_VOID: fprintf(out, "void"); break;
        case KRT_IR_VALUE_IMM: fprintf(out, "%.0f", val->data.imm); break;
        case KRT_IR_VALUE_VAR: fprintf(out, "%s", val->data.name ? val->data.name : "?"); break;
        case KRT_IR_VALUE_TEMP: fprintf(out, "t%d", val->data.index); break;
        case KRT_IR_VALUE_ARG: fprintf(out, "arg%d", val->data.index); break;
        case KRT_IR_VALUE_STRING_CONST: fprintf(out, "str%d", val->data.string_const_id); break;
        case KRT_IR_VALUE_FUNCTION: fprintf(out, "%s", val->data.function_name ? val->data.function_name : "?"); break;
        default: fprintf(out, "?"); break;
    }
}

void KrtIrPrint(KrtIRModule* module, FILE* output) {
    if (!module) return;
    FILE* out = output ? output : stdout;
    
    for (int i = 0; i < module->global_count; i++) {
        fprintf(out, "global %s\n", module->globals[i].name);
    }
    
    for (int i = 0; i < module->string_const_count; i++) {
        fprintf(out, "str%d = \"%s\"\n", i, module->string_constants[i]);
    }
    
    KrtIRFunction* func = module->functions;
    while (func) {
        fprintf(out, "\nfunction %s(", func->name);
        for (int i = 0; i < func->param_count; i++) {
            if (i > 0) fprintf(out, ", ");
            fprintf(out, "%s", func->params[i].name);
        }
        fprintf(out, ")\n");
        
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            fprintf(out, "  %s:\n", block->label);
            for (int i = 0; i < block->inst_count; i++) {
                KrtIRInst* inst = block->insts[i];
                if (!inst) continue;
                
                if (inst->opcode >= 0 && inst->opcode <= KRT_IR_NOP) {
                    fprintf(out, "    ");
                    print_value(out, &inst->result);
                    fprintf(out, " = %s", ir_opcode_names[inst->opcode]);
                    for (int j = 0; j < inst->operand_count; j++) {
                        fprintf(out, " ");
                        print_value(out, &inst->operands[j]);
                    }
                    fprintf(out, "\n");
                }
            }
            block = block->next;
        }
        func = func->next;
    }
}
