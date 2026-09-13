/* White-box CFG cases that well-formed source programs cannot construct. */
#include <assert.h>
#include <stdlib.h>
#include "../../Re.KrtC/src/compiler/Backend/Kro/KroCodegen.c"

typedef struct {
    KROCodegenContext* context;
    KrtIRFunction function;
    KrtIRBasicBlock blocks[4];
    KrtIRBasicBlock* successors[4][2];
    KrtIRInst instructions[16];
    KrtIRInst* block_instructions[4][8];
    KrtIRValue operands[16][2];
    int instruction_count;
} RegisterFixture;

static KrtIRInst* test_append(RegisterFixture* fixture, int block, KrtIROpcode opcode, int result, int source) {
    int index = fixture->instruction_count++;
    assert(index < 16);
    KrtIRInst* inst = &fixture->instructions[index];
    inst->opcode = opcode;
    inst->result.type = result >= 0 ? KRT_IR_VALUE_TEMP : KRT_IR_VALUE_VOID;
    inst->result.value_type = TOKEN_UINT64;
    inst->result.data.index = result;
    inst->operands = fixture->operands[index];
    if (source >= 0) {
        inst->operand_count = 1;
        inst->operands[0].type = KRT_IR_VALUE_TEMP;
        inst->operands[0].value_type = TOKEN_UINT64;
        inst->operands[0].data.index = source;
    } else if (opcode == KRT_IR_IMM || opcode == KRT_IR_BRANCH || opcode == KRT_IR_RETURN) {
        inst->operand_count = 1;
        inst->operands[0].type = KRT_IR_VALUE_INTEGER;
        inst->operands[0].value_type = TOKEN_UINT64;
        inst->operands[0].data.integer = 1;
    }
    KrtIRBasicBlock* target = &fixture->blocks[block];
    if (target->last_inst) {
        target->last_inst->next = inst;
    } else {
        target->first_inst = inst;
    }
    target->last_inst = inst;
    target->insts[target->inst_count++] = inst;
    return inst;
}

static void test_initialize(RegisterFixture* fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->context = calloc(1, sizeof(*fixture->context));
    assert(fixture->context);
    fixture->context->optimization_level = 3;
    fixture->context->current_function = &fixture->function;
    fixture->function.entry_block = fixture->blocks;
    fixture->function.return_type = TOKEN_UINT64;
    for (int i = 0; i < 4; i++) {
        fixture->blocks[i].insts = fixture->block_instructions[i];
        fixture->blocks[i].succs = fixture->successors[i];
        fixture->blocks[i].next = i < 3 ? &fixture->blocks[i + 1] : NULL;
    }
    fixture->blocks[0].succ_count = 2;
    fixture->successors[0][0] = &fixture->blocks[1];
    fixture->successors[0][1] = &fixture->blocks[2];
    for (int i = 1; i < 3; i++) {
        fixture->blocks[i].succ_count = 1;
        fixture->successors[i][0] = &fixture->blocks[3];
    }
}

static void test_diamond(RegisterFixture* fixture, bool entry_definition, bool second_definition,
                         bool before_definition) {
    test_initialize(fixture);
    if (before_definition) {
        test_append(fixture, 0, KRT_IR_COPY, 2, 1);
    }
    if (entry_definition) {
        test_append(fixture, 0, KRT_IR_IMM, 1, -1);
    }
    test_append(fixture, 0, KRT_IR_BRANCH, -1, -1);
    if (!entry_definition) {
        test_append(fixture, 1, KRT_IR_IMM, 1, -1);
    }
    test_append(fixture, 1, KRT_IR_JUMP, -1, -1);
    if (second_definition) {
        test_append(fixture, 2, KRT_IR_IMM, 1, -1);
    }
    test_append(fixture, 2, KRT_IR_JUMP, -1, -1);
    test_append(fixture, 3, KRT_IR_RETURN, -1, 1);
}

static bool test_eligible(RegisterFixture* fixture) {
    record_function_temporaries(fixture->context, &fixture->function);
    bool eligible[KRT_KRO_MAX_TEMP_REGS];
    if (!plan_cross_block_temporaries(fixture->context, &fixture->function, eligible)) {
        return false;
    }
    KROTempSlot* slot = find_temp_location(fixture->context, 1);
    return eligible[slot - fixture->context->temp_slots];
}

int main(void) {
    RegisterFixture fixture;
    test_diamond(&fixture, true, false, false);
    assert(test_eligible(&fixture));
    allocate_temporary_registers(fixture.context, &fixture.function);
    assert(find_temp_location(fixture.context, 1)->reg_low >= 0);
    free(fixture.context);

    test_diamond(&fixture, false, false, false);
    assert(!test_eligible(&fixture)); /* The right predecessor bypasses the definition. */
    allocate_temporary_registers(fixture.context, &fixture.function);
    assert(find_temp_location(fixture.context, 1)->reg_low < 0);
    free(fixture.context);

    test_diamond(&fixture, false, true, false);
    assert(!test_eligible(&fixture)); /* Multiple definitions are not SSA. */
    free(fixture.context);

    test_diamond(&fixture, true, false, true);
    assert(!test_eligible(&fixture)); /* A use precedes the sole definition. */
    free(fixture.context);

    test_diamond(&fixture, true, false, false);
    fixture.blocks[0].first_inst->opcode = KRT_IR_PHI;
    assert(!test_eligible(&fixture));
    free(fixture.context);

    test_diamond(&fixture, true, false, false);
    fixture.successors[1][0] = &fixture.blocks[0];
    assert(!test_eligible(&fixture)); /* Explicit backwards edges require loop liveness. */
    free(fixture.context);

    test_diamond(&fixture, true, false, false);
    fixture.context->tail_site_count = 1;
    assert(!test_eligible(&fixture)); /* Rotation may bypass entry definitions. */
    free(fixture.context);

    test_diamond(&fixture, false, false, false);
    fixture.blocks[2].first_inst->opcode = KRT_IR_RETURN;
    fixture.blocks[2].succ_count = 0;
    fixture.context->tail_site_count = 1;
    assert(test_eligible(&fixture)); /* Every tail iteration re-executes the body definition. */
    free(fixture.context);

    test_diamond(&fixture, true, false, false);
    fixture.blocks[0].first_inst->result.value_type = TOKEN_UINT128;
    fixture.blocks[0].first_inst->operands[0].value_type = TOKEN_UINT128;
    fixture.blocks[3].first_inst->operands[0].value_type = TOKEN_UINT128;
    assert(test_eligible(&fixture));
    allocate_temporary_registers(fixture.context, &fixture.function);
    assert(find_temp_location(fixture.context, 1)->reg_low >= 0);
    assert(find_temp_location(fixture.context, 1)->reg_high >= 0);
    free(fixture.context);

    test_diamond(&fixture, true, false, false);
    assert(test_eligible(&fixture));
    for (int i = 0; i < KRT_KRO_PACKED_REG_COUNT; i++) {
        fixture.context->packed_reg_bits[i] = 64;
    }
    allocate_temporary_registers(fixture.context, &fixture.function);
    assert(find_temp_location(fixture.context, 1)->reg_low < 0);
    free(fixture.context);
    return 0;
}
