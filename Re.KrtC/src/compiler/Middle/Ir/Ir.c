#include "Ir.h"
#include "Core/Core.h"
#include <string.h>
#include <stdio.h>

#define KRT_VAR_TABLE_SIZE 256

typedef struct KrtIRVarEntry {
    char* name;
    int current_version;
    struct KrtIRVarEntry* next;
} KrtIRVarEntry;

typedef struct {
    KrtIRVarEntry* buckets[KRT_VAR_TABLE_SIZE];
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

KrtIRModule* KrtIrModuleCreate(void) {
    KrtIRModule* module = (KrtIRModule*)KRT_CALLOC(1, sizeof(KrtIRModule));
    if (module) {
        module->optimization_level = 2;
    }
    return module;
}

void KrtIrModuleDestroy(KrtIRModule* module) {
    if (!module) {
        return;
    }

    KrtIRFunction* func = module->functions;
    while (func) {
        KrtIRFunction* next = func->next;
        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            KrtIRBasicBlock* block_next = block->next;
            KrtIRInst* inst = block->first_inst;
            while (inst) {
                KrtIRInst* next_inst = inst->next;
                KRT_FREE(inst->operands);
                KRT_FREE(inst);
                inst = next_inst;
            }
            KRT_FREE(block->label);
            KRT_FREE(block->insts);
            KRT_FREE(block->preds);
            KRT_FREE(block->succs);
            KRT_FREE(block->phi_list);
            KRT_FREE(block);
            block = block_next;
        }
        for (int i = 0; func->params && i < func->param_count; i++) {
            KRT_FREE(func->params[i].name);
        }
        KRT_FREE(func->params);
        KRT_FREE(func->name);
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

KrtIRFunction* KrtIrFunctionCreate(KrtIRBuilder* builder, const char* name, KrtIRParam* params, int param_count,
                                   KrtTokenType return_type) {
    if (!builder || !builder->module || !name || (param_count > 0 && !params)) {
        return NULL;
    }
    KrtIRFunction* func = NULL;
    for (KrtIRFunction* declared = builder->module->functions; declared; declared = declared->next) {
        if (!declared->entry_block && strcmp(declared->name, name) == 0 && declared->param_count == param_count) {
            return declared;
        }
        if (!declared->entry_block && !strcmp(declared->name, name) && declared->param_count < 0 && param_count >= 0) {
            func = declared;
            break;
        }
    }
    bool already_declared = func != NULL;
    if (!func) {
        func = (KrtIRFunction*)KRT_CALLOC(1, sizeof(KrtIRFunction));
    }
    if (!func) {
        return NULL;
    }

    KrtIRParam* copied_params = NULL;
    int copied_count = 0;
    if (param_count > 0) {
        copied_params = (KrtIRParam*)KRT_MALLOC((size_t)param_count * sizeof(KrtIRParam));
        if (!copied_params) {
            goto allocation_failed;
        }
        for (int i = 0; i < param_count; i++) {
            if (!params[i].name) {
                goto allocation_failed;
            }
            copied_params[i] = params[i];
            copied_params[i].name = KRT_STRDUP(params[i].name);
            if (!copied_params[i].name) {
                goto allocation_failed;
            }
            copied_count++;
        }
    }
    if (!func->name) {
        func->name = KRT_STRDUP(name);
        if (!func->name) {
            goto allocation_failed;
        }
    }
    func->return_type = return_type;
    func->param_count = param_count;
    func->params = copied_params;

    if (!already_declared) {
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

allocation_failed:
    for (int i = 0; i < copied_count; i++) {
        KRT_FREE(copied_params[i].name);
    }
    KRT_FREE(copied_params);
    if (!already_declared) {
        KRT_FREE(func);
    }
    return NULL;
}

void KrtIrFunctionSetEntry(KrtIRBuilder* builder, KrtIRFunction* func) {
    if (!builder || !func) {
        return;
    }
    builder->current_function = func;

    KrtIRBasicBlock* entry = KrtIrBlockCreate(builder, "entry");
    func->entry_block = entry;
    KrtIrBlockSetCurrent(builder, entry);
}

KrtIRBasicBlock* KrtIrBlockCreate(KrtIRBuilder* builder, const char* label) {
    if (!builder || !label) {
        return NULL;
    }

    KrtIRBasicBlock* block = (KrtIRBasicBlock*)KRT_CALLOC(1, sizeof(KrtIRBasicBlock));
    if (!block) {
        return NULL;
    }

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
    if (!builder) {
        return;
    }
    builder->current_block = block;
    if (block && builder->current_function && !builder->current_function->entry_block) {
        builder->current_function->entry_block = block;
    }
}

int KrtIrBlockGetInstCount(KrtIRBasicBlock* block) {
    return block ? block->inst_count : 0;
}

KrtIRInst* KrtIrBlockGetInst(KrtIRBasicBlock* block, int index) {
    if (!block || index < 0 || index >= block->inst_count) {
        return NULL;
    }
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
    if (!block || !pred) {
        return;
    }
    if (block->pred_count >= block->pred_capacity) {
        block->pred_capacity *= 2;
        block->preds = (KrtIRBasicBlock**)KRT_REALLOC(block->preds, block->pred_capacity * sizeof(KrtIRBasicBlock*));
    }
    block->preds[block->pred_count++] = pred;
}

void KrtIrBlockAddSucc(KrtIRBuilder* builder, KrtIRBasicBlock* block, KrtIRBasicBlock* succ) {
    (void)builder;
    if (!block || !succ) {
        return;
    }
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
    if (!block || index < 0 || index >= block->pred_count) {
        return NULL;
    }
    return block->preds[index];
}

KrtIRBasicBlock* KrtIrBlockGetSucc(KrtIRBasicBlock* block, int index) {
    if (!block || index < 0 || index >= block->succ_count) {
        return NULL;
    }
    return block->succs[index];
}

void KrtIrBlockInvalidateCache(KrtIRBasicBlock* block) {
    (void)block;
}

KrtIRInst* KrtIrBlockFindCachedInst(KrtIRBasicBlock* block, KrtIROpcode opcode) {
    if (!block) {
        return NULL;
    }
    for (int i = 0; i < block->inst_count; i++) {
        if (block->insts[i] && block->insts[i]->opcode == opcode) {
            return block->insts[i];
        }
    }
    return NULL;
}

static KrtIRInst* ir_create_inst(KrtIRBuilder* builder, KrtIROpcode opcode) {
    if (!builder || !builder->current_block) {
        return NULL;
    }

    KrtIRBasicBlock* block = builder->current_block;

    if (block->inst_count >= block->inst_capacity) {
        int capacity = block->inst_capacity ? block->inst_capacity * 2 : 32;
        KrtIRInst** insts = (KrtIRInst**)KRT_REALLOC(block->insts, (size_t)capacity * sizeof(KrtIRInst*));
        if (!insts) {
            return NULL;
        }
        block->insts = insts;
        block->inst_capacity = capacity;
    }

    KrtIRInst* inst = (KrtIRInst*)KRT_CALLOC(1, sizeof(KrtIRInst));
    if (!inst) {
        return NULL;
    }

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
    if (!inst) {
        return val;
    }
    if (inst->operand_count >= inst->operand_capacity) {
        int capacity = inst->operand_capacity ? inst->operand_capacity * 2 : 4;
        KrtIRValue* operands = (KrtIRValue*)KRT_REALLOC(inst->operands, (size_t)capacity * sizeof(KrtIRValue));
        if (!operands) {
            KRT_COMPILE_ERROR("Unable to allocate IR instruction operands");
        }
        inst->operands = operands;
        inst->operand_capacity = capacity;
    }
    inst->operands[inst->operand_count++] = val;
    if (inst->operand_count == 2 && inst->opcode >= KRT_IR_ADD && inst->opcode <= KRT_IR_NE) {
        inst->result.value_type =
            (inst->opcode >= KRT_IR_LT) ? TOKEN_BOOL
            : (inst->opcode == KRT_IR_LSHIFT || inst->opcode == KRT_IR_RSHIFT)
                ? inst->operands[0].value_type
                : KrtTokenIntegerCommon(inst->operands[0].value_type, inst->operands[1].value_type);
    }
    return val;
}

static KrtTokenType ir_variable_type(KrtIRBuilder* builder, const char* name) {
    for (int i = builder->var_type_count - 1; i >= 0; i--) {
        struct KrtIrVarType* entry = &builder->var_types[i];
        if (strcmp(entry->ir_name, name) == 0) {
            return entry->is_array ? TOKEN_EOF : (KrtTokenType)entry->token;
        }
    }
    KrtIRGlobal* global = KrtIrModuleFindGlobal(builder->module, name);
    return global ? global->type : TOKEN_EOF;
}

KrtIRValue KrtIrTyped(KrtIRBuilder* builder, KrtIRValue value, KrtTokenType type) {
    value.value_type = type;
    if (builder && builder->current_block && value.type == KRT_IR_VALUE_TEMP) {
        KrtIRInst* inst = builder->current_block->last_inst;
        if (inst && inst->result.type == value.type && inst->result.data.index == value.data.index) {
            inst->result.value_type = type;
        }
    }
    return value;
}

KrtIRValue KrtIrInteger(KrtUInt128 integer, KrtTokenType type) {
    KrtIRValue value = {0};
    value.type = KRT_IR_VALUE_INTEGER;
    value.value_type = type;
    value.data.integer = KrtIntegerNormalize(integer, KrtTokenIntegerBits(type), KrtTokenIsUnsigned(type));
    return value;
}

bool KrtIrFoldInteger(KrtIROpcode op, KrtIRValue lhs, KrtIRValue rhs, KrtIRValue* result) {
    if (!result || (lhs.type != KRT_IR_VALUE_INTEGER && lhs.type != KRT_IR_VALUE_IMM) ||
        (rhs.type != KRT_IR_VALUE_INTEGER && rhs.type != KRT_IR_VALUE_IMM)) {
        return false;
    }
    KrtTokenType type = (op == KRT_IR_LSHIFT || op == KRT_IR_RSHIFT)
                            ? lhs.value_type
                            : KrtTokenIntegerCommon(lhs.value_type, rhs.value_type);
    if (!KrtTokenIntegerBits(type)) {
        type = TOKEN_INT64;
    }
    int bits = KrtTokenIntegerBits(type);
    bool is_unsigned = KrtTokenIsUnsigned(type);
    KrtUInt128 a = lhs.type == KRT_IR_VALUE_INTEGER ? lhs.data.integer : (KrtUInt128)(int64_t)lhs.data.imm;
    KrtUInt128 b = rhs.type == KRT_IR_VALUE_INTEGER ? rhs.data.integer : (KrtUInt128)(int64_t)rhs.data.imm;
    a = KrtIntegerNormalize(a, bits, is_unsigned);
    if (op != KRT_IR_LSHIFT && op != KRT_IR_RSHIFT) {
        b = KrtIntegerNormalize(b, bits, is_unsigned);
    }
    KrtUInt128 value = 0, sign = (KrtUInt128)1 << 127;
    bool negative_a = !is_unsigned && (a & sign);
    bool negative_b = !is_unsigned && (b & sign);
    KrtUInt128 ordered_a = is_unsigned ? a : a ^ sign;
    KrtUInt128 ordered_b = is_unsigned ? b : b ^ sign;
    switch (op) {
    case KRT_IR_ADD:
        value = a + b;
        break;
    case KRT_IR_SUB:
        value = a - b;
        break;
    case KRT_IR_MUL:
        value = a * b;
        break;
    case KRT_IR_DIV:
    case KRT_IR_MOD: {
        if (!b) {
            return false;
        }
        KrtUInt128 magnitude_a = negative_a ? -a : a;
        KrtUInt128 magnitude_b = negative_b ? -b : b;
        if (op == KRT_IR_DIV) {
            value = magnitude_a / magnitude_b;
            if (negative_a != negative_b) {
                value = -value;
            }
        } else {
            value = magnitude_a % magnitude_b;
            if (negative_a) {
                value = -value;
            }
        }
        break;
    }
    case KRT_IR_AND:
        value = a & b;
        break;
    case KRT_IR_OR:
        value = a | b;
        break;
    case KRT_IR_XOR:
        value = a ^ b;
        break;
    case KRT_IR_LSHIFT:
        value = b >= (unsigned)bits ? 0 : a << (unsigned)b;
        break;
    case KRT_IR_RSHIFT:
        value = b >= (unsigned)bits ? (negative_a ? ~(KrtUInt128)0 : 0)
                : negative_a        ? ~(~a >> (unsigned)b)
                                    : a >> (unsigned)b;
        break;
    case KRT_IR_LT:
        value = ordered_a < ordered_b;
        break;
    case KRT_IR_GT:
        value = ordered_a > ordered_b;
        break;
    case KRT_IR_LE:
        value = ordered_a <= ordered_b;
        break;
    case KRT_IR_GE:
        value = ordered_a >= ordered_b;
        break;
    case KRT_IR_EQ:
        value = a == b;
        break;
    case KRT_IR_NE:
        value = a != b;
        break;
    default:
        return false;
    }
    *result = KrtIrInteger(value, op >= KRT_IR_LT && op <= KRT_IR_NE ? TOKEN_BOOL : type);
    return true;
}

void KrtIrStore(KrtIRBuilder* builder, const char* name, KrtIRValue value) {
    if (!builder || !name || !builder->current_block) {
        return;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_COPY);
    if (inst) {
        inst->result.type = KRT_IR_VALUE_VAR;
        inst->result.data.name = KrtIrArenaStrdup(builder->arena, name);
        inst->result.value_type = ir_variable_type(builder, name);
        if (builder->current_function) {
            for (int i = 0; i < builder->current_function->param_count; i++) {
                if (!strcmp(builder->current_function->params[i].name, name)) {
                    inst->result = KrtIrArg(builder, i);
                }
            }
        }
        if (inst->result.value_type == TOKEN_EOF && value.value_type != TOKEN_EOF) {
            inst->result.value_type = value.value_type;
            int token = 0, is_array = 0;
            if (!KrtIrVarTypeFind(builder, name, &token, &is_array)) {
                KrtIrVarTypePush(builder, name, value.value_type, 0);
            }
        }
        ir_add_operand(inst, value);
    }
}

KrtIRValue KrtIrLoad(KrtIRBuilder* builder, const char* name) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_IMM;
    result.data.imm = 0;

    if (!builder || !name) {
        return result;
    }
    if (builder->current_function) {
        for (int i = 0; i < builder->current_function->param_count; i++) {
            if (!strcmp(builder->current_function->params[i].name, name)) {
                return KrtIrArg(builder, i);
            }
        }
    }

    result.type = KRT_IR_VALUE_VAR;
    result.data.name = KrtIrArenaStrdup(builder->arena, name);
    result.value_type = ir_variable_type(builder, name);
    return result;
}

void KrtIrAlloc(KrtIRBuilder* builder, const char* name) {
    if (!builder || !name) {
        return;
    }

    KrtIRVarTable* table = (KrtIRVarTable*)builder->extensions;
    unsigned int idx = hash_var_name(name) % KRT_VAR_TABLE_SIZE;
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

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_ADD);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

/* 双精度立即数: 物化时按位型装入(区别于整数编码的 IMM) */
KrtIRValue KrtIrImmF(KrtIRBuilder* builder, double value) {
    (void)builder;
    KrtIRValue result = {0};
    memset(&result, 0, sizeof(result));
    result.type = KRT_IR_VALUE_IMM_F;
    result.value_type = TOKEN_FLOAT64;
    result.data.imm = value;
    return result;
}

/* 浮点二元运算通用构造(FADD..FGE), 骨架同 KrtIrAdd */
KrtIRValue KrtIrFloatBinary(KrtIRBuilder* builder, KrtIROpcode op, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, op);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        inst->result.value_type = TOKEN_FLOAT64;
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrSub(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_SUB);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrMul(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_MUL);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrDiv(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_DIV);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
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

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_MOD);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
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

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_AND);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrOr(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_OR);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrXor(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_XOR);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrLshift(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_LSHIFT);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrRshift(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_RSHIFT);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrPow(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_POW);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrCompare(KrtIRBuilder* builder, KrtIROpcode op, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, op);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        inst->result.value_type = TOKEN_BOOL;
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrPhi(KrtIRBuilder* builder, KrtIRValue* values, KrtIRBasicBlock** blocks, int count) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block || !values || !blocks || count <= 0) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_PHI);
    if (inst) {
        for (int i = 0; i < count; i++) {
            ir_add_operand(inst, values[i]);
        }
        result = inst->result;
    }
    return result;
}

void KrtIrJump(KrtIRBuilder* builder, KrtIRBasicBlock* target) {
    if (!builder || !builder->current_block || !target) {
        return;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_JUMP);
    if (inst) {
        KrtIrBlockAddSucc(builder, builder->current_block, target);
        KrtIrBlockAddPred(builder, target, builder->current_block);
    }
}

void KrtIrBranch(KrtIRBuilder* builder, KrtIRValue cond, KrtIRBasicBlock* true_block, KrtIRBasicBlock* false_block) {
    if (!builder || !builder->current_block || !true_block || !false_block) {
        return;
    }

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
    if (!builder || !builder->current_block) {
        return;
    }

    if (builder->current_function && KrtTokenIntegerBits(builder->current_function->return_type)) {
        value = KrtIrCast(builder, value, builder->current_function->return_type);
    }
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_RETURN);
    if (inst) {
        ir_add_operand(inst, value);
    }
}

void KrtIrLabel(KrtIRBuilder* builder, const char* label) {
    if (!builder || !label) {
        return;
    }

    KrtIRBasicBlock* block = KrtIrBlockCreate(builder, label);
    KrtIrBlockSetCurrent(builder, block);
}

void KrtIrNop(KrtIRBuilder* builder) {
    if (!builder || !builder->current_block) {
        return;
    }

    ir_create_inst(builder, KRT_IR_NOP);
}

KrtIRValue KrtIrCall(KrtIRBuilder* builder, const char* func_name, KrtIRValue* args, int arg_count) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !func_name || !builder->current_block) {
        return result;
    }

    KrtIRFunction* callee = builder->module->functions;
    while (callee && strcmp(callee->name, func_name) != 0) {
        callee = callee->next;
    }
    if (!callee && strncmp(func_name, "_ZN", 3) == 0) {
        const char* end = func_name + 3;
        while (*end >= '0' && *end <= '9') {
            char* next = NULL;
            long length = strtol(end, &next, 10);
            if (length <= 0 || (size_t)length > strlen(next)) {
                break;
            }
            end = next + length;
        }
        if (*end == 'E') {
            size_t prefix = (size_t)(end - func_name) + 1;
            for (KrtIRFunction* candidate = builder->module->functions; candidate; candidate = candidate->next) {
                if (candidate->param_count == arg_count && strncmp(candidate->name, func_name, prefix) == 0) {
                    callee = candidate;
                    func_name = candidate->name;
                    break;
                }
            }
        }
    }
    if (callee) {
        for (int i = 0; i < arg_count && i < callee->param_count; i++) {
            if (!callee->params[i].is_array && KrtTokenIntegerBits(callee->params[i].type)) {
                args[i] = KrtIrCast(builder, args[i], callee->params[i].type);
            }
        }
    }
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_CALL);
    if (inst) {
        inst->result.value_type = callee ? callee->return_type : TOKEN_EOF;
        KrtIRValue name_val = {0};
        name_val.type = KRT_IR_VALUE_FUNCTION;
        name_val.data.function_name = KrtIrArenaStrdup(builder->arena, func_name);
        ir_add_operand(inst, name_val);

        for (int i = 0; i < arg_count; i++) {
            ir_add_operand(inst, args[i]);
        }

        result = inst->result;

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

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_SYSCALL);
    if (inst) {
        ir_add_operand(inst, syscall_num);
        for (int i = 0; i < arg_count && i < 6; i++) {
            ir_add_operand(inst, args[i]);
        }
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrImm(KrtIRBuilder* builder, double value) {
    (void)builder;
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_IMM;
    result.data.imm = value;
    return result;
}

KrtIRValue KrtIrVar(KrtIRBuilder* builder, const char* name) {
    if (builder && name) {
        return KrtIrLoad(builder, name);
    }
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_VAR;
    result.data.name = (char*)name;
    return result;
}

KrtIRValue KrtIrAddressOf(KrtIRBuilder* builder, KrtIRValue storage) {
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_ADDRESS_OF);
    if (!inst) {
        return (KrtIRValue){.type = KRT_IR_VALUE_VOID};
    }
    ir_add_operand(inst, storage);
    inst->result.value_type = TOKEN_UINT64;
    return inst->result;
}

KrtIRValue KrtIrStackAlloc(KrtIRBuilder* builder, KrtIRValue count, int element_size) {
    /* Keep the high word until the backend has checked allocation overflow. */
    count = KrtIrCast(builder, count, KrtTokenIntegerBits(count.value_type) > 64 ? TOKEN_UINT128 : TOKEN_UINT64);
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_STACKALLOC);
    if (!inst) {
        return (KrtIRValue){.type = KRT_IR_VALUE_VOID};
    }
    ir_add_operand(inst, count);
    ir_add_operand(inst, KrtIrInteger(element_size, TOKEN_UINT64));
    inst->result.value_type = TOKEN_UINT64;
    return inst->result;
}

KrtIRValue KrtIrCallIndirect(KrtIRBuilder* builder, KrtIRValue callee, KrtIRValue* args, int count,
                             KrtFunctionType* signature) {
    if (!builder || !builder->current_function || !signature || count < 0 || count != signature->parameter_count ||
        (count && (!args || !signature->parameters))) {
        return (KrtIRValue){.type = KRT_IR_VALUE_VOID};
    }
    for (int i = 0; i < count; i++) {
        args[i] = KrtIrCast(builder, args[i], KrtSourceAbiStorage(signature->parameters[i]));
    }
    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_CALL_INDIRECT);
    if (!inst) {
        return (KrtIRValue){.type = KRT_IR_VALUE_VOID};
    }
    ir_add_operand(inst, callee);
    for (int i = 0; i < count; i++) {
        ir_add_operand(inst, args[i]);
    }
    inst->result.value_type = KrtSourceStorage(signature->result);
    builder->current_function->has_calls = 1;
    return inst->result;
}

KrtIRValue KrtIrTemp(KrtIRBuilder* builder) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;
    return result;
}

KrtIRValue KrtIrArg(KrtIRBuilder* builder, int index) {
    (void)builder;
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_ARG;
    result.data.index = index;
    if (builder && builder->current_function && index >= 0 && index < builder->current_function->param_count) {
        result.value_type = builder->current_function->params[index].is_array
                                ? TOKEN_EOF
                                : builder->current_function->params[index].type;
    }
    return result;
}

KrtIRValue KrtIrStringConst(KrtIRBuilder* builder, const char* str) {
    if (!builder || !builder->module || !str) {
        KrtIRValue result = {0};
        result.type = KRT_IR_VALUE_STRING_CONST;
        result.data.string_const_id = -1;
        return result;
    }

    if (builder->module->string_const_count >= builder->module->string_const_capacity) {
        builder->module->string_const_capacity =
            builder->module->string_const_capacity == 0 ? 16 : builder->module->string_const_capacity * 2;
        builder->module->string_constants = (char**)KRT_REALLOC(builder->module->string_constants,
                                                                builder->module->string_const_capacity * sizeof(char*));
    }

    int id = builder->module->string_const_count;
    builder->module->string_constants[id] = KRT_STRDUP(str);
    builder->module->string_const_count++;

    KrtIRValue result = {0};
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

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_LOADPTR);
    if (inst) {
        ir_add_operand(inst, base);
        KrtIRValue offset_val = {0};
        offset_val.type = KRT_IR_VALUE_IMM;
        offset_val.data.imm = (double)offset;
        ir_add_operand(inst, offset_val);
        KrtIRValue size_val = {0};
        size_val.type = KRT_IR_VALUE_IMM;
        size_val.data.imm = (double)size;
        ir_add_operand(inst, size_val);
        result = inst->result;
    }
    return result;
}

void KrtIrStorePtr(KrtIRBuilder* builder, KrtIRValue base, int offset, KrtIRValue value) {
    KrtIrStorePtrSized(builder, base, offset, value, 8);
}

void KrtIrStorePtrSized(KrtIRBuilder* builder, KrtIRValue base, int offset, KrtIRValue value, int size) {
    if (!builder || !builder->current_block) {
        return;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_STOREPTR);
    if (inst) {
        ir_add_operand(inst, base);
        KrtIRValue offset_val = {0};
        offset_val.type = KRT_IR_VALUE_IMM;
        offset_val.data.imm = (double)offset;
        ir_add_operand(inst, offset_val);
        ir_add_operand(inst, value);
        KrtIRValue size_val = {0};
        size_val.type = KRT_IR_VALUE_IMM;
        size_val.data.imm = (double)size;
        ir_add_operand(inst, size_val);
    }
}

void KrtIrArrayStore(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index, KrtIRValue value) {
    KrtIrArrayStoreSized(builder, array, index, value, 8);
}

void KrtIrArrayStoreSized(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index, KrtIRValue value,
                          int element_size) {
    if (!builder || !builder->current_block) {
        return;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_ARRAY_STORE);
    if (inst) {
        ir_add_operand(inst, array);
        ir_add_operand(inst, index);
        ir_add_operand(inst, value);
        KrtIRValue size_val = {0};
        size_val.type = KRT_IR_VALUE_IMM;
        size_val.data.imm = (double)element_size;
        ir_add_operand(inst, size_val);
    }
}

KrtIRValue KrtIrStrcat(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_STRCAT);
    if (inst) {
        ir_add_operand(inst, lhs);
        ir_add_operand(inst, rhs);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrIntToString(KrtIRBuilder* builder, KrtIRValue value) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_INT_TO_STRING);
    if (inst) {
        ir_add_operand(inst, value);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrDoubleToString(KrtIRBuilder* builder, KrtIRValue value) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_DOUBLE_TO_STRING);
    if (inst) {
        ir_add_operand(inst, value);
        result = inst->result;
    }
    return result;
}

KrtIRValue KrtIrCast(KrtIRBuilder* builder, KrtIRValue value, KrtTokenType target_type) {
    /* Mutable variables/arguments still need a snapshot at the evaluation point. */
    if (KrtTokenIntegerBits(target_type) && value.value_type == target_type &&
        (value.type == KRT_IR_VALUE_TEMP || value.type == KRT_IR_VALUE_INTEGER)) {
        return value;
    }
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = builder ? builder->temp_counter++ : 0;

    if (!builder || !builder->current_block) {
        return result;
    }

    KrtIRInst* inst = ir_create_inst(builder, KRT_IR_CAST);
    if (inst) {
        ir_add_operand(inst, value);
        KrtIRValue type_val = {0};
        type_val.type = KRT_IR_VALUE_IMM;
        type_val.data.imm = (double)target_type;
        ir_add_operand(inst, type_val);
        inst->result.value_type = target_type;
        result = inst->result;
    }
    return result;
}

KrtIRGlobal* KrtIrModuleAddGlobal(KrtIRBuilder* builder, const char* name, KrtTokenType type) {
    if (!builder || !builder->module || !name) {
        return NULL;
    }

    if (builder->module->global_count >= builder->module->global_capacity) {
        builder->module->global_capacity =
            builder->module->global_capacity == 0 ? 16 : builder->module->global_capacity * 2;
        builder->module->globals =
            (KrtIRGlobal*)KRT_REALLOC(builder->module->globals, builder->module->global_capacity * sizeof(KrtIRGlobal));
    }

    int idx = builder->module->global_count++;
    builder->module->globals[idx].name = KRT_STRDUP(name);
    builder->module->globals[idx].type = type;
    builder->module->globals[idx].has_initializer = 0;
    builder->module->globals[idx].init_number = 0.0;
    builder->module->globals[idx].init_integer = 0;
    builder->module->globals[idx].is_integer_initializer = false;

    return &builder->module->globals[idx];
}

void KrtIrVarTypePush(KrtIRBuilder* builder, const char* name, int token, int is_array) {
    if (!builder || !name) {
        return;
    }
    if (builder->var_type_count >= builder->var_type_capacity) {
        builder->var_type_capacity = builder->var_type_capacity ? builder->var_type_capacity * 2 : 16;
        builder->var_types = (struct KrtIrVarType*)KRT_REALLOC(builder->var_types, builder->var_type_capacity *
                                                                                       sizeof(struct KrtIrVarType));
    }
    struct KrtIrVarType* v = &builder->var_types[builder->var_type_count++];
    v->name = KrtIrArenaStrdup(builder->arena, name);
    v->token = token;
    v->is_array = is_array;
    int collisions = 0;
    for (int i = 0; i < builder->var_type_count - 1; i++) {
        if (strcmp(builder->var_types[i].name, name) == 0) {
            collisions++;
        }
    }
    if (collisions == 0) {
        v->ir_name = v->name;
    } else {
        size_t ln = strlen(name) + 12;
        char* buf = (char*)KRT_MALLOC(ln);
        if (buf) {
            snprintf(buf, ln, "%s#%d", name, collisions + 1);
            v->ir_name = KrtIrArenaStrdup(builder->arena, buf);
            KRT_FREE(buf);
        } else {
            v->ir_name = v->name;
        }
    }
}

const char* KrtIrVarTypeResolveIRName(KrtIRBuilder* builder, const char* name) {
    if (!builder || !name) {
        return name;
    }
    for (int i = builder->var_type_count - 1; i >= 0; i--) {
        if (strcmp(builder->var_types[i].name, name) == 0) {
            return builder->var_types[i].ir_name ? builder->var_types[i].ir_name : name;
        }
    }
    return name;
}

int KrtIrVarTypeFind(KrtIRBuilder* builder, const char* name, int* out_token, int* out_is_array) {
    if (!builder || !name) {
        return 0;
    }
    for (int i = builder->var_type_count - 1; i >= 0; i--) {
        if (strcmp(builder->var_types[i].name, name) == 0) {
            if (out_token) {
                *out_token = builder->var_types[i].token;
            }
            if (out_is_array) {
                *out_is_array = builder->var_types[i].is_array;
            }
            return 1;
        }
    }
    return 0;
}

void KrtIrVarTypeTruncate(KrtIRBuilder* builder, int count) {
    if (builder && count >= 0 && count <= builder->var_type_count) {
        builder->var_type_count = count;
    }
}

KrtIRGlobal* KrtIrModuleFindGlobal(KrtIRModule* module, const char* name) {
    if (!module || !name) {
        return NULL;
    }

    for (int i = 0; i < module->global_count; i++) {
        if (strcmp(module->globals[i].name, name) == 0) {
            return &module->globals[i];
        }
    }
    return NULL;
}

void KrtIrModuleSetGlobalNumberInitializer(KrtIRGlobal* global, double value) {
    if (!global) {
        return;
    }
    global->has_initializer = 1;
    global->init_number = value;
}

void KrtIrPushLoopContext(KrtIRBuilder* builder, KrtIRBasicBlock* continue_block, KrtIRBasicBlock* break_block) {
    if (!builder) {
        return;
    }

    if (builder->loop_stack_size >= builder->loop_stack_capacity) {
        builder->loop_stack_capacity = builder->loop_stack_capacity == 0 ? 8 : builder->loop_stack_capacity * 2;
        builder->loop_continue_blocks = (KrtIRBasicBlock**)KRT_REALLOC(
            builder->loop_continue_blocks, builder->loop_stack_capacity * sizeof(KrtIRBasicBlock*));
        builder->loop_break_blocks = (KrtIRBasicBlock**)KRT_REALLOC(
            builder->loop_break_blocks, builder->loop_stack_capacity * sizeof(KrtIRBasicBlock*));
    }

    builder->loop_continue_blocks[builder->loop_stack_size] = continue_block;
    builder->loop_break_blocks[builder->loop_stack_size] = break_block;
    builder->loop_stack_size++;
}

void KrtIrPopLoopContext(KrtIRBuilder* builder) {
    if (!builder || builder->loop_stack_size <= 0) {
        return;
    }
    builder->loop_stack_size--;
}

KrtIRBasicBlock* KrtIrGetCurrentContinueBlock(KrtIRBuilder* builder) {
    if (!builder || builder->loop_stack_size <= 0) {
        return NULL;
    }
    return builder->loop_continue_blocks[builder->loop_stack_size - 1];
}

KrtIRBasicBlock* KrtIrGetCurrentBreakBlock(KrtIRBuilder* builder) {
    if (!builder || builder->loop_stack_size <= 0) {
        return NULL;
    }
    return builder->loop_break_blocks[builder->loop_stack_size - 1];
}

KrtIRBuilder* KrtIrBuilderCreate(void) {
    KrtIRBuilder* builder = (KrtIRBuilder*)KRT_CALLOC(1, sizeof(KrtIRBuilder));
    if (!builder) {
        return NULL;
    }

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

static void ir_class_layout_dispose(KrtIRClassLayout* layout) {
    for (int i = 0; i < layout->field_count; i++) {
        KRT_FREE(layout->fields[i].name);
    }
    KRT_FREE(layout->fields);
    KRT_FREE(layout->class_name);
}

void KrtIrBuilderDestroy(KrtIRBuilder* builder) {
    if (!builder) {
        return;
    }

    if (builder->module) {
        KrtIrModuleDestroy(builder->module);
    }
    if (builder->arena) {
        KrtIrArenaDestroy(builder->arena);
    }
    for (int i = 0; i < builder->layout_count; i++) {
        ir_class_layout_dispose(&builder->layouts[i]);
    }
    KRT_FREE(builder->layouts);
    KRT_FREE(builder->var_types);

    if (builder->loop_continue_blocks) {
        KRT_FREE(builder->loop_continue_blocks);
    }
    if (builder->loop_break_blocks) {
        KRT_FREE(builder->loop_break_blocks);
    }
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
    if (!builder || !class_name) {
        return;
    }
    for (int i = 0; i < builder->layout_count; i++) {
        if (!strcmp(builder->layouts[i].class_name, class_name)) {
            return;
        }
    }

    KrtIRClassLayout layout = {0};
    layout.class_name = KRT_STRDUP(class_name);
    layout.field_capacity = 8;
    layout.fields = (KrtIRFieldOffset*)KRT_MALLOC((size_t)layout.field_capacity * sizeof(KrtIRFieldOffset));
    if (!layout.class_name || !layout.fields) {
        goto allocation_failed;
    }

    if (class_body && class_body->type == AST_BLOCK) {
        for (int i = 0; i < class_body->data.block.statement_count; i++) {
            ASTNode* stmt = class_body->data.block.statements[i];
            if (!stmt) {
                continue;
            }

            if (stmt->type == AST_ACCESS_MODIFIER) {
                stmt = stmt->data.access_modifier.member;
                if (!stmt) {
                    continue;
                }
            }

            if (stmt->type == AST_VARIABLE_DECLARATION) {
                if (layout.field_count >= layout.field_capacity) {
                    int capacity = layout.field_capacity * 2;
                    KrtIRFieldOffset* fields =
                        (KrtIRFieldOffset*)KRT_REALLOC(layout.fields, (size_t)capacity * sizeof(KrtIRFieldOffset));
                    if (!fields) {
                        goto allocation_failed;
                    }
                    layout.fields = fields;
                    layout.field_capacity = capacity;
                }

                KrtIRFieldOffset* field = &layout.fields[layout.field_count];
                field->name = KRT_STRDUP(stmt->data.variable_decl.name);
                if (!field->name) {
                    goto allocation_failed;
                }
                layout.field_count++;
                field->type = stmt->data.variable_decl.is_array ? TOKEN_EOF : stmt->data.variable_decl.type;
                int size = KrtTokenIntegerBits(field->type) > 64 ? 16 : 8;
                layout.size = (layout.size + size - 1) & ~(size - 1);
                field->offset = layout.size;
                layout.size += size;
            }
        }
    }
    if (builder->layout_count >= builder->layout_capacity) {
        int capacity = builder->layout_capacity == 0 ? 8 : builder->layout_capacity * 2;
        KrtIRClassLayout* layouts =
            (KrtIRClassLayout*)KRT_REALLOC(builder->layouts, (size_t)capacity * sizeof(KrtIRClassLayout));
        if (!layouts) {
            goto allocation_failed;
        }
        builder->layouts = layouts;
        builder->layout_capacity = capacity;
    }
    builder->layouts[builder->layout_count++] = layout;
    return;

allocation_failed:
    ir_class_layout_dispose(&layout);
    KRT_COMPILE_ERROR("Unable to allocate IR class layout for '%s'", class_name);
}

int KrtIrLayoutGetOffset(KrtIRBuilder* builder, const char* class_name, const char* field_name) {
    if (!builder || !class_name || !field_name) {
        return -1;
    }

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

KrtTokenType KrtIrLayoutGetType(KrtIRBuilder* builder, const char* class_name, const char* field_name) {
    if (!builder || !class_name || !field_name) {
        return TOKEN_EOF;
    }
    for (int i = 0; i < builder->layout_count; i++) {
        if (strcmp(builder->layouts[i].class_name, class_name) != 0) {
            continue;
        }
        for (int j = 0; j < builder->layouts[i].field_count; j++) {
            KrtIRFieldOffset* field = &builder->layouts[i].fields[j];
            if (strcmp(field->name, field_name) == 0) {
                return field->type;
            }
        }
    }
    return TOKEN_EOF;
}

int KrtIrLayoutGetSize(KrtIRBuilder* builder, const char* class_name) {
    if (!builder || !class_name) {
        return 0;
    }

    for (int i = 0; i < builder->layout_count; i++) {
        if (strcmp(builder->layouts[i].class_name, class_name) == 0) {
            return builder->layouts[i].size;
        }
    }
    return 0;
}

static const char* ir_opcode_names[] = {"load",
                                        "store",
                                        "alloc",
                                        "imm",
                                        "add",
                                        "sub",
                                        "mul",
                                        "div",
                                        "mod",
                                        "and",
                                        "or",
                                        "xor",
                                        "lshift",
                                        "rshift",
                                        "pow",
                                        "lt",
                                        "gt",
                                        "eq",
                                        "le",
                                        "ge",
                                        "ne",
                                        "jump",
                                        "branch",
                                        "call",
                                        "return",
                                        "label",
                                        "strcat",
                                        "cast",
                                        "loadptr",
                                        "storeptr",
                                        "array_store",
                                        "int_to_string",
                                        "double_to_string",
                                        "copy",
                                        "syscall",
                                        "phi",
                                        "nop",
                                        "fadd",
                                        "fsub",
                                        "fmul",
                                        "fdiv",
                                        "feq",
                                        "fne",
                                        "flt",
                                        "fle",
                                        "fgt",
                                        "fge"};

static void print_value(FILE* out, KrtIRValue* val) {
    if (!val) {
        fprintf(out, "?");
        return;
    }
    switch (val->type) {
    case KRT_IR_VALUE_VOID:
        fprintf(out, "void");
        break;
    case KRT_IR_VALUE_IMM:
        fprintf(out, "%.0f", val->data.imm);
        break;
    case KRT_IR_VALUE_IMM_F:
        fprintf(out, "%.17g", val->data.imm);
        break;
    case KRT_IR_VALUE_INTEGER: {
        char buffer[42];
        KrtIntegerFormat(val->data.integer, KrtTokenIsUnsigned(val->value_type), buffer);
        fprintf(out, "%s", buffer);
        break;
    }
    case KRT_IR_VALUE_VAR:
        fprintf(out, "%s", val->data.name ? val->data.name : "?");
        break;
    case KRT_IR_VALUE_TEMP:
        fprintf(out, "t%d", val->data.index);
        break;
    case KRT_IR_VALUE_ARG:
        fprintf(out, "arg%d", val->data.index);
        break;
    case KRT_IR_VALUE_STRING_CONST:
        fprintf(out, "str%d", val->data.string_const_id);
        break;
    case KRT_IR_VALUE_FUNCTION:
        fprintf(out, "%s", val->data.function_name ? val->data.function_name : "?");
        break;
    default:
        fprintf(out, "?");
        break;
    }
}

void KrtIrPrint(KrtIRModule* module, FILE* output) {
    if (!module) {
        return;
    }
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
            if (i > 0) {
                fprintf(out, ", ");
            }
            fprintf(out, "%s", func->params[i].name);
        }
        fprintf(out, ")\n");

        KrtIRBasicBlock* block = func->entry_block;
        while (block) {
            fprintf(out, "  %s:\n", block->label);
            for (int i = 0; i < block->inst_count; i++) {
                KrtIRInst* inst = block->insts[i];
                if (!inst) {
                    continue;
                }

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
