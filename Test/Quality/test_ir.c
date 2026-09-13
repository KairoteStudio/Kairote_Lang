#include "compiler/Middle/Ir/IrOptimizer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static KrtIRFunction* create_integer_function(KrtIRBuilder* builder, const char* name) {
    KrtIRParam parameter = {.name = "n", .type = TOKEN_INT32};
    KrtIRFunction* function = KrtIrFunctionCreate(builder, name, &parameter, 1, TOKEN_INT32);
    assert(function);
    KrtIrFunctionSetEntry(builder, function);
    return function;
}

static void assert_instruction_index(KrtIRModule* module) {
    for (KrtIRFunction* function = module->functions; function; function = function->next) {
        for (KrtIRBasicBlock* block = function->entry_block; block; block = block->next) {
            int count = 0;
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                assert(count < block->inst_count && block->insts[count] == inst);
                assert(KrtIrBlockGetInst(block, count++) == inst);
                assert(inst->opcode != KRT_IR_NOP);
                assert(inst->operand_count >= 0 && inst->operand_count <= inst->operand_capacity);
            }
            assert(count == block->inst_count && KrtIrBlockGetInstCount(block) == count);
            assert(block->last_inst == (count ? block->insts[count - 1] : NULL));
            assert(!block->last_inst || !block->last_inst->next);
        }
    }
}

static int count_opcode(KrtIRFunction* function, KrtIROpcode opcode) {
    int count = 0;
    for (KrtIRBasicBlock* block = function->entry_block; block; block = block->next) {
        for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
            if (inst->opcode == opcode) {
                count++;
            }
        }
    }
    return count;
}

static void check_cse_and_dead_chains(void) {
    size_t baseline = KrtMemoryGetBlockCount();
    KrtIRBuilder* builder = KrtIrBuilderCreate();
    assert(builder);
    KrtIRFunction* duplicate = create_integer_function(builder, "Duplicates");
    KrtIRValue argument = KrtIrArg(builder, 0);
    KrtIRValue first = KrtIrAdd(builder, argument, KrtIrInteger(1, TOKEN_INT32));
    KrtIRValue second = KrtIrAdd(builder, argument, KrtIrInteger(1, TOKEN_INT32));
    KrtIrReturn(builder, KrtIrMul(builder, first, second));

    KrtIRFunction* chain = create_integer_function(builder, "UnusedChain");
    argument = KrtIrArg(builder, 0);
    first = KrtIrAdd(builder, argument, KrtIrInteger(1, TOKEN_INT32));
    /* The dead chain crosses a CFG edge; a block-local use count is insufficient. */
    KrtIRBasicBlock* finish = KrtIrBlockCreate(builder, "finish");
    assert(finish);
    KrtIrJump(builder, finish);
    KrtIrBlockSetCurrent(builder, finish);
    second = KrtIrMul(builder, first, KrtIrInteger(3, TOKEN_INT32));
    KrtIrSub(builder, second, argument);
    KrtIrReturn(builder, argument);

    IROptimizer stats = {0};
    assert(ir_optimize_function_inlining(builder->module, &stats));
    assert(stats.cse_count > 0);
    /* CSE forwards both uses to the first ADD; its obsolete COPY is then gone. */
    assert(duplicate->entry_block->inst_count == 3);
    assert(count_opcode(duplicate, KRT_IR_ADD) == 1);
    assert(count_opcode(duplicate, KRT_IR_COPY) == 0);
    KrtIRInst* multiply = duplicate->entry_block->insts[1];
    assert(multiply->opcode == KRT_IR_MUL);
    assert(multiply->operands[0].type == KRT_IR_VALUE_TEMP);
    assert(multiply->operands[0].data.index == multiply->operands[1].data.index);
    assert(multiply->operands[0].data.index == duplicate->entry_block->first_inst->result.data.index);
    /* All three unused arithmetic nodes and the forwarded return CAST disappear. */
    assert(chain->entry_block->inst_count == 1 && chain->entry_block->first_inst->opcode == KRT_IR_JUMP);
    assert(finish->inst_count == 1 && finish->first_inst->opcode == KRT_IR_RETURN);
    assert(finish->first_inst->operands[0].type == KRT_IR_VALUE_ARG);
    assert_instruction_index(builder->module);
    assert(!ir_optimize_function_inlining(builder->module, NULL));
    KrtIrBuilderDestroy(builder);
    assert(KrtMemoryGetBlockCount() == baseline);
}

static void check_unused_traps_and_calls(void) {
    size_t baseline = KrtMemoryGetBlockCount();
    KrtIRBuilder* builder = KrtIrBuilderCreate();
    assert(builder);
    KrtIRFunction* pure = create_integer_function(builder, "GuardedPure");
    KrtIRValue argument = KrtIrArg(builder, 0);
    KrtIRValue zero = KrtIrInteger(0, TOKEN_INT32);
    KrtIRValue condition = KrtIrCompare(builder, KRT_IR_EQ, argument, zero);
    KrtIRBasicBlock* yes = KrtIrBlockCreate(builder, "zero");
    KrtIRBasicBlock* no = KrtIrBlockCreate(builder, "nonzero");
    assert(yes && no);
    KrtIrBranch(builder, condition, yes, no);
    KrtIrBlockSetCurrent(builder, yes);
    KrtIrReturn(builder, zero);
    KrtIrBlockSetCurrent(builder, no);
    KrtIrReturn(builder, argument);

    KrtIRFunction* function = create_integer_function(builder, "DiscardedResults");
    argument = KrtIrArg(builder, 0);
    KrtIrDiv(builder, argument, zero);
    KrtIrDiv(builder, argument, zero);
    KrtIrMod(builder, argument, zero);
    KrtIrMod(builder, argument, zero);
    KrtIRValue arguments[] = {argument};
    KrtIrCall(builder, "GuardedPure", arguments, 1);
    KrtIrCall(builder, "GuardedPure", arguments, 1);
    arguments[0] = argument;
    KrtIrCall(builder, "UnknownBridge", arguments, 1);
    KrtIrReturn(builder, zero);

    assert(ir_optimize_function_inlining(builder->module, NULL));
    /* Duplicate evaluations may be removed, but the first trap/call must stay. */
    assert(count_opcode(function, KRT_IR_DIV) == 1);
    assert(count_opcode(function, KRT_IR_MOD) == 1);
    assert(count_opcode(function, KRT_IR_CALL) == 2);
    assert(count_opcode(function, KRT_IR_COPY) == 0);
    assert(count_opcode(function, KRT_IR_CAST) == 0);
    assert(function->entry_block->inst_count == 5);
    KrtIRInst* call = function->entry_block->insts[2];
    assert(call->opcode == KRT_IR_CALL && !strcmp(call->operands[0].data.function_name, pure->name));
    call = function->entry_block->insts[3];
    assert(call->opcode == KRT_IR_CALL && !strcmp(call->operands[0].data.function_name, "UnknownBridge"));
    assert_instruction_index(builder->module);
    assert(!ir_optimize_function_inlining(builder->module, NULL));
    KrtIrBuilderDestroy(builder);
    assert(KrtMemoryGetBlockCount() == baseline);
}

static void check_multiple_definitions(void) {
    size_t baseline = KrtMemoryGetBlockCount();
    KrtIRBuilder* builder = KrtIrBuilderCreate();
    assert(builder);
    KrtIRFunction* unused = create_integer_function(builder, "RepeatedDefinition");
    KrtIRValue argument = KrtIrArg(builder, 0);
    KrtIRValue shared = KrtIrAdd(builder, argument, KrtIrInteger(1, TOKEN_INT32));
    KrtIrSub(builder, argument, KrtIrInteger(2, TOKEN_INT32));
    /* Non-SSA input must not be treated as having a unique defining node. */
    unused->entry_block->last_inst->result = shared;
    KrtIrReturn(builder, KrtIrInteger(0, TOKEN_INT32));

    KrtIRFunction* diamond = create_integer_function(builder, "Diamond");
    argument = KrtIrArg(builder, 0);
    KrtIRValue condition = KrtIrCompare(builder, KRT_IR_LT, argument, KrtIrInteger(0, TOKEN_INT32));
    KrtIRBasicBlock* yes = KrtIrBlockCreate(builder, "negative");
    KrtIRBasicBlock* no = KrtIrBlockCreate(builder, "positive");
    KrtIRBasicBlock* join = KrtIrBlockCreate(builder, "join");
    assert(yes && no && join);
    KrtIrBranch(builder, condition, yes, no);
    KrtIrBlockSetCurrent(builder, yes);
    shared = KrtIrAdd(builder, argument, KrtIrInteger(3, TOKEN_INT32));
    KrtIrJump(builder, join);
    KrtIrBlockSetCurrent(builder, no);
    KrtIrSub(builder, argument, KrtIrInteger(4, TOKEN_INT32));
    no->last_inst->result = shared;
    KrtIrJump(builder, join);
    KrtIrBlockSetCurrent(builder, join);
    KrtIrReturn(builder, shared);

    ir_optimize_function_inlining(builder->module, NULL);
    assert(unused->entry_block->inst_count == 3);
    assert(unused->entry_block->insts[0]->result.data.index == unused->entry_block->insts[1]->result.data.index);
    assert(unused->entry_block->insts[0]->opcode == KRT_IR_ADD);
    assert(unused->entry_block->insts[1]->opcode == KRT_IR_SUB);
    /* Either branch may define the value consumed by the common return. */
    assert(count_opcode(diamond, KRT_IR_ADD) == 1 && count_opcode(diamond, KRT_IR_SUB) == 1);
    assert(yes->first_inst->result.data.index == shared.data.index);
    assert(no->first_inst->result.data.index == shared.data.index);
    assert(join->first_inst->opcode == KRT_IR_RETURN);
    assert(join->first_inst->operands[0].type == KRT_IR_VALUE_TEMP);
    assert(join->first_inst->operands[0].data.index == shared.data.index);
    assert_instruction_index(builder->module);
    KrtIrBuilderDestroy(builder);
    assert(KrtMemoryGetBlockCount() == baseline);
}

static void check_invalid_pointer_builders(void) {
    size_t baseline = KrtMemoryGetBlockCount();
    KrtIRValue value = KrtIrInteger(1, TOKEN_INT32);
    assert(KrtIrAddressOf(NULL, value).type == KRT_IR_VALUE_VOID);
    assert(KrtIrStackAlloc(NULL, value, 4).type == KRT_IR_VALUE_VOID);
    assert(KrtIrCallIndirect(NULL, value, NULL, 0, NULL).type == KRT_IR_VALUE_VOID);
    assert(!KrtIrFoldInteger(KRT_IR_ADD, value, value, NULL));

    KrtIRBuilder* builder = KrtIrBuilderCreate();
    assert(builder);
    assert(KrtIrAddressOf(builder, value).type == KRT_IR_VALUE_VOID);
    assert(KrtIrStackAlloc(builder, value, 4).type == KRT_IR_VALUE_VOID);
    KrtIRFunction* function = create_integer_function(builder, "InvalidPointers");
    KrtSourceType parameter = {.token = TOKEN_INT32};
    KrtFunctionType signature = {.result = parameter, .parameters = &parameter, .parameter_count = 1};
    int instructions = function->entry_block->inst_count;
    assert(KrtIrCallIndirect(builder, value, &value, 1, NULL).type == KRT_IR_VALUE_VOID);
    assert(KrtIrCallIndirect(builder, value, NULL, 1, &signature).type == KRT_IR_VALUE_VOID);
    assert(KrtIrCallIndirect(builder, value, &value, -1, &signature).type == KRT_IR_VALUE_VOID);
    assert(KrtIrCallIndirect(builder, value, &value, 0, &signature).type == KRT_IR_VALUE_VOID);
    signature.parameters = NULL;
    assert(KrtIrCallIndirect(builder, value, &value, 1, &signature).type == KRT_IR_VALUE_VOID);
    assert(function->entry_block->inst_count == instructions);
    assert(!function->has_calls);
    KrtIrBuilderDestroy(builder);
    assert(KrtMemoryGetBlockCount() == baseline);
}

int main(void) {
    check_invalid_pointer_builders();
    size_t baseline = KrtMemoryGetBlockCount();
    for (int pass = 0; pass < 20; pass++) {
        KrtIRBuilder* builder = KrtIrBuilderCreate();
        assert(builder);
        for (int f = 0; f < 50; f++) {
            char name[32];
            snprintf(name, sizeof(name), "Function%d", f);
            KrtIRParam parameter = {.name = "n", .type = TOKEN_INT32};
            KrtIRFunction* function = KrtIrFunctionCreate(builder, name, &parameter, 1, TOKEN_INT32);
            assert(function);
            KrtIrFunctionSetEntry(builder, function);
            KrtIrNop(builder);
            KrtIRValue value = KrtIrAdd(builder, KrtIrArg(builder, 0), KrtIrInteger(1, TOKEN_INT32));
            KrtIrNop(builder);
            KrtIrReturn(builder, value);
        }
        assert(ir_optimize_dead_code_elimination(builder->module, NULL));
        assert(!ir_optimize_dead_code_elimination(builder->module, NULL));
        KrtIRParam parameter = {.name = "n", .type = TOKEN_INT32};
        KrtIRFunction* caller = KrtIrFunctionCreate(builder, "Caller", &parameter, 1, TOKEN_INT32);
        KrtIrFunctionSetEntry(builder, caller);
        KrtIRValue argument = KrtIrArg(builder, 0);
        KrtIRValue called = KrtIrCall(builder, "Function0", &argument, 1);
        KrtIrReturn(builder, called);
        /* Populate the cached instruction view before changing its linked list. */
        assert(KrtIrBlockGetInstCount(caller->entry_block) == caller->entry_block->inst_count);
        assert(ir_optimize_function_inlining(builder->module, NULL));
        assert(!ir_optimize_function_inlining(builder->module, NULL));
        for (KrtIRFunction* function = builder->module->functions; function; function = function->next) {
            KrtIRBasicBlock* block = function->entry_block;
            int count = 0;
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                assert(count < block->inst_count && block->insts[count++] == inst);
                assert(inst->opcode != KRT_IR_NOP);
                assert(inst->opcode != KRT_IR_CALL);
                assert(KrtIrBlockGetInst(block, count - 1) == inst);
            }
            assert(count == block->inst_count && block->last_inst == block->insts[count - 1]);
        }
        KrtIrBuilderDestroy(builder);
        assert(KrtMemoryGetBlockCount() == baseline);
        check_cse_and_dead_chains();
        check_unused_traps_and_calls();
        check_multiple_definitions();
    }
    KrtMemorySafetyCleanup();
    puts("IR ownership and instruction index checks passed");
    return 0;
}
