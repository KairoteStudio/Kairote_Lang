#include "VmCodegen.h"
#include "../../Middle/Ir/IrSsa.h"
#include <string.h>
#include <stdio.h>

#define NUMBER_VAL(value) ((KrtValue){VAL_NUMBER, {.number = value}})

typedef struct {
    const char* name;
    int index;
} LocalMapping;

typedef struct {
    const char* name;
    int address;
} LabelMapping;

typedef struct {
    const char* label_name;
    int jump_inst_address;
} PendingJump;

typedef struct {
    KrtIRModule* module;
    KrtIRFunction* current_function;
    LocalMapping* locals;
    int local_count;
    int local_capacity;
    LabelMapping* labels;
    int label_count;
    int label_capacity;
    PendingJump* pending_jumps;
    int pending_jump_count;
    int pending_jump_capacity;
    int next_temp_index;
} CodegenContext;

static void init_context(CodegenContext* ctx, KrtIRModule* module) {
    ctx->module = module;
    ctx->current_function = NULL;
    ctx->locals = NULL;
    ctx->local_count = 0;
    ctx->local_capacity = 0;
    ctx->labels = NULL;
    ctx->label_count = 0;
    ctx->label_capacity = 0;
    ctx->pending_jumps = NULL;
    ctx->pending_jump_count = 0;
    ctx->pending_jump_capacity = 0;
    ctx->next_temp_index = 1000; 
}

static void free_context(CodegenContext* ctx) {
    KRT_FREE(ctx->locals);
    KRT_FREE(ctx->labels);
    KRT_FREE(ctx->pending_jumps);
}

static int get_local_index(CodegenContext* ctx, const char* name) {
    for (int i = 0; i < ctx->local_count; i++) {
        if (strcmp(ctx->locals[i].name, name) == 0) {
            return ctx->locals[i].index;
        }
    }
    
    if (ctx->local_count >= ctx->local_capacity) {
        int old_capacity = ctx->local_capacity;
        ctx->local_capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        ctx->locals = KRT_REALLOC(ctx->locals, ctx->local_capacity * sizeof(LocalMapping));
    }
    
    int index = ctx->local_count;
    ctx->locals[ctx->local_count].name = name;
    ctx->locals[ctx->local_count].index = index;
    ctx->local_count++;
    
    return index;
}

static int get_temp_index(CodegenContext* ctx, int temp_id) {
    printf("get_temp_index: temp_id=%d, local_count=%d\n", temp_id, ctx->local_count);
    
    return ctx->local_count + temp_id;
}

static void emit_value_push(CodegenContext* ctx, KrtChunk* chunk, KrtIRValue val) {
    printf("emit_value_push: val.type=%d\n", val.type);
    switch (val.type) {
        case KRT_IR_VALUE_IMM: {
            printf("emit_value_push: KRT_IR_VALUE_IMM, imm=%f\n", val.data.imm);
            int constant = KrtBytecodeGeneratorAddConstant(chunk, NUMBER_VAL(val.data.imm));
            KrtBytecodeGeneratorWriteByte(chunk, OP_CONSTANT, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)constant, 0);
            break;
        }
        case KRT_IR_VALUE_VAR: {
            printf("emit_value_push: KRT_IR_VALUE_VAR, name=%s\n", val.data.name);
            int index = get_local_index(ctx, val.data.name);
            KrtBytecodeGeneratorWriteByte(chunk, OP_GET_LOCAL, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)index, 0);
            break;
        }
        case KRT_IR_VALUE_TEMP: {
            printf("emit_value_push: KRT_IR_VALUE_TEMP, index=%d\n", val.data.index);
            
            int index = get_temp_index(ctx, val.data.index);
            KrtBytecodeGeneratorWriteByte(chunk, OP_GET_LOCAL, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)index, 0);
            break;
        }
        case KRT_IR_VALUE_STRING_CONST: {
            printf("emit_value_push: KRT_IR_VALUE_STRING_CONST, string_const_id=%d\n", val.data.string_const_id);
            if (!ctx->module->string_constants || val.data.string_const_id < 0 || val.data.string_const_id >= ctx->module->string_const_count) {
                break;
            }
            const char* str = ctx->module->string_constants[val.data.string_const_id];
            int constant = KrtBytecodeGeneratorAddStringConstant(chunk, str);
            KrtBytecodeGeneratorWriteByte(chunk, OP_CONSTANT, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)constant, 0);
            break;
        }
        case KRT_IR_VALUE_FUNCTION: {
            printf("emit_value_push: KRT_IR_VALUE_FUNCTION, function_name=%s\n", val.data.function_name ? val.data.function_name : "?");
            const char* func_name = val.data.function_name ? val.data.function_name : "";
            int constant = KrtBytecodeGeneratorAddStringConstant(chunk, func_name);
            KrtBytecodeGeneratorWriteByte(chunk, OP_CONSTANT, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)constant, 0);
            break;
        }
        case KRT_IR_VALUE_ARG: {
            
            KrtBytecodeGeneratorWriteByte(chunk, OP_GET_LOCAL, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)val.data.index, 0);
            break;
        }
        default:
            KrtBytecodeGeneratorWriteByte(chunk, OP_NULL, 0);
            break;
    }
}

static void emit_binary_op(CodegenContext* ctx, KrtChunk* chunk, KrtIRInst* inst, KrtOpCode op) {
    printf("emit_binary_op: starting\n");
    
    printf("emit_binary_op: writing operation opcode\n");
    KrtBytecodeGeneratorWriteByte(chunk, op, 0);
    
    if (inst->result.type != KRT_IR_VALUE_VOID) {
        printf("emit_binary_op: storing result, result.type=%d\n", inst->result.type);
        int index;
        if (inst->result.type == KRT_IR_VALUE_TEMP) {
            index = get_temp_index(ctx, inst->result.data.index);
        } else {
            index = get_local_index(ctx, inst->result.data.name);
        }
        KrtBytecodeGeneratorWriteByte(chunk, OP_SET_LOCAL, 0);
        KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)index, 0);
        KrtBytecodeGeneratorWriteByte(chunk, OP_POP, 0); 
    }
    printf("emit_binary_op: done\n");
}

static void emit_assignment(CodegenContext* ctx, KrtChunk* chunk, KrtIRInst* inst) {
    
    emit_value_push(ctx, chunk, inst->operands[0]);
    
    if (inst->result.type == KRT_IR_VALUE_VAR) {
        int dest_index = get_local_index(ctx, inst->result.data.name);
        KrtBytecodeGeneratorWriteByte(chunk, OP_SET_LOCAL, 0);
        KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)dest_index, 0);
        KrtBytecodeGeneratorWriteByte(chunk, OP_POP, 0);
    } else if (inst->result.type == KRT_IR_VALUE_TEMP) {
        int dest_index = get_temp_index(ctx, inst->result.data.index);
        KrtBytecodeGeneratorWriteByte(chunk, OP_SET_LOCAL, 0);
        KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)dest_index, 0);
        KrtBytecodeGeneratorWriteByte(chunk, OP_POP, 0);
    }
}

static void emit_function_call(CodegenContext* ctx, KrtChunk* chunk, KrtIRInst* inst) {
    
    for (int i = inst->operand_count - 1; i >= 0; i--) {
        emit_value_push(ctx, chunk, inst->operands[i]);
    }
    
    KrtBytecodeGeneratorWriteByte(chunk, OP_CALL, 0);
    KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)(inst->operand_count - 1), 0);
    
    if (inst->result.type != KRT_IR_VALUE_VOID) {
        if (inst->result.type == KRT_IR_VALUE_VAR) {
            int dest_index = get_local_index(ctx, inst->result.data.name);
            KrtBytecodeGeneratorWriteByte(chunk, OP_SET_LOCAL, 0);
            KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)dest_index, 0);
            KrtBytecodeGeneratorWriteByte(chunk, OP_POP, 0); 
        }
        KrtBytecodeGeneratorWriteByte(chunk, OP_POP, 0);
    }
}

static void emit_return(CodegenContext* ctx, KrtChunk* chunk, KrtIRInst* inst) {
    if (inst->operand_count > 0) {
        emit_value_push(ctx, chunk, inst->operands[0]);
    } else {
        KrtBytecodeGeneratorWriteByte(chunk, OP_NULL, 0);
    }
    
    KrtBytecodeGeneratorWriteByte(chunk, OP_RETURN, 0);
}

static void generate_function(CodegenContext* ctx, KrtIRFunction* func, KrtChunk* chunk) {
    printf("generate_function: starting\n");
    ctx->current_function = func;
    
    KrtIrSsaOptimize(func);
    
    printf("generate_function: registering %d parameters\n", func->param_count);
    for (int i = 0; i < func->param_count; i++) {
        printf("generate_function: registering parameter %d: %s\n", i, func->params[i].name);
        get_local_index(ctx, func->params[i].name);
    }
    
    printf("generate_function: generating function body\n");
    KrtIRBasicBlock* block = func->entry_block;
    int block_count = 0;
    while (block) {
        printf("generate_function: processing block %d\n", block_count++);
        KrtIRInst* inst = block->first_inst;
        int inst_count = 0;
        while (inst) {
            printf("generate_function: processing instruction %d: opcode=%d\n", inst_count++, inst->opcode);
            switch (inst->opcode) {
                case KRT_IR_ADD:
                    emit_binary_op(ctx, chunk, inst, OP_ADD);
                    break;
                case KRT_IR_SUB:
                    emit_binary_op(ctx, chunk, inst, OP_SUB);
                    break;
                case KRT_IR_MUL:
                    emit_binary_op(ctx, chunk, inst, OP_MUL);
                    break;
                case KRT_IR_DIV:
                    emit_binary_op(ctx, chunk, inst, OP_DIV);
                    break;
                case KRT_IR_LT:
                    emit_binary_op(ctx, chunk, inst, OP_LESS);
                    break;
                case KRT_IR_GT:
                    emit_binary_op(ctx, chunk, inst, OP_GREATER);
                    break;
                case KRT_IR_EQ:
                    emit_binary_op(ctx, chunk, inst, OP_EQUAL);
                    break;
                case KRT_IR_STORE:
                    emit_assignment(ctx, chunk, inst);
                    break;
                case KRT_IR_CALL:
                    emit_function_call(ctx, chunk, inst);
                    break;
                case KRT_IR_RETURN:
                    emit_return(ctx, chunk, inst);
                    break;
                case KRT_IR_INT_TO_STRING:
                    emit_value_push(ctx, chunk, inst->operands[0]);
                    KrtBytecodeGeneratorWriteByte(chunk, OP_INT_TO_STRING, 0);
                    
                    if (inst->result.type != KRT_IR_VALUE_VOID) {
                        int index = get_local_index(ctx, inst->result.data.name);
                        KrtBytecodeGeneratorWriteByte(chunk, OP_SET_LOCAL, 0);
                        KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)index, 0);
                        KrtBytecodeGeneratorWriteByte(chunk, OP_POP, 0); 
                    }
                    break;
                case KRT_IR_ALLOC:
                    
                    break;
                case KRT_IR_LOAD:
                    
                    emit_value_push(ctx, chunk, inst->operands[0]);
                    break;
                case KRT_IR_IMM:
                    
                    emit_value_push(ctx, chunk, inst->operands[0]);
                    break;
                default:
                    
                    break;
            }
            inst = inst->next;
        }
        block = block->next;
    }
}

void KrtVmCodegenGenerate(KrtIRModule* ir_module, KrtChunk* chunk) {
    printf("KrtVmCodegenGenerate: starting\n");
    
    CodegenContext ctx;
    printf("KrtVmCodegenGenerate: initializing context\n");
    init_context(&ctx, ir_module);
    
    printf("KrtVmCodegenGenerate: initializing chunk\n");
    KrtBytecodeGeneratorInitChunk(chunk);
    printf("KrtVmCodegenGenerate: chunk initialized\n");
    
    if (ir_module->main_function) {
        printf("KrtVmCodegenGenerate: found main function\n");
        KrtIRFunction* main_func = ir_module->main_function;
        
        int local_count = main_func->param_count + 10; 
        printf("KrtVmCodegenGenerate: writing STK_ADJ with local_count=%d\n", local_count);
        KrtBytecodeGeneratorWriteByte(chunk, OP_STK_ADJ, 0);
        KrtBytecodeGeneratorWriteByte(chunk, local_count, 0);
        
        printf("KrtVmCodegenGenerate: generating function body\n");
        generate_function(&ctx, main_func, chunk);
        printf("KrtVmCodegenGenerate: function body generated\n");
    }
    
    printf("KrtVmCodegenGenerate: writing HALT\n");
    KrtBytecodeGeneratorWriteByte(chunk, OP_HALT, 0);
    printf("KrtVmCodegenGenerate: HALT written\n");
    
    printf("KrtVmCodegenGenerate: freeing context\n");
    free_context(&ctx);
    printf("KrtVmCodegenGenerate: done\n");
}