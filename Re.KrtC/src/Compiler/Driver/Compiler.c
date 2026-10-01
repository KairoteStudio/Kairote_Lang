#include "Compiler.h"
#include "../Middle/Ir/IrSsa.h"
#include "../Middle/Ir/IrOptimizer.h"
#include "../Backend/X86/X86Codegen.h"
#include "../Backend/Vm/VmCodegen.h"
#include "../Backend/Kro/KroCodegen.h"

KrtCompiler* KrtCompilerCreate(const char* output_filename, KrtTargetPlatform target) {
    KrtCompiler* compiler = (KrtCompiler*)KRT_MALLOC(sizeof(KrtCompiler));
    if (!compiler) {
        return NULL;
    }

    KRT_STRNCPY_SAFE(compiler->output_filename, output_filename);

    if (target == KRT_TARGET_VM_BYTECODE || target == KRT_TARGET_KRO_OBJ) {
        compiler->output_file = fopen(output_filename, "wb");
    } else {
        compiler->output_file = fopen(output_filename, "w");
    }
    if (!compiler->output_file) {
        KrtError("Failed to open output file: %s", output_filename);
        KRT_FREE(compiler);
        return NULL;
    }

    compiler->target = target;
    compiler->optimization_level = 2;
    compiler->has_object_code = false;
    KrtBytecodeGeneratorInitChunk(&compiler->last_chunk);

    return compiler;
}

void KrtCompilerDestroy(KrtCompiler* compiler) {
    if (!compiler) {
        return;
    }

    if (compiler->output_file) {
        fclose(compiler->output_file);
        compiler->output_file = NULL;
    }
    KrtBytecodeGeneratorFreeChunk(&compiler->last_chunk);
    KRT_FREE(compiler);
}

static bool extended_integer_type(KrtTokenType type) {
    int bits = KrtTokenIntegerBits(type);
    return bits && bits != 8 && bits != 16 && bits != 32 && bits != 64;
}

static bool module_has_extended_integers(KrtIRModule* module) {
    for (int i = 0; i < module->global_count; i++) {
        if (extended_integer_type(module->globals[i].type)) {
            return true;
        }
    }
    for (KrtIRFunction* func = module->functions; func; func = func->next) {
        if (extended_integer_type(func->return_type)) {
            return true;
        }
        for (int i = 0; i < func->param_count; i++) {
            if (extended_integer_type(func->params[i].type)) {
                return true;
            }
        }
        for (KrtIRBasicBlock* block = func->entry_block; block; block = block->next) {
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                if (extended_integer_type(inst->result.value_type)) {
                    return true;
                }
                for (int i = 0; i < inst->operand_count; i++) {
                    if (extended_integer_type(inst->operands[i].value_type)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static bool module_has_native_pointers(KrtIRModule* module) {
    for (KrtIRFunction* fn = module->functions; fn; fn = fn->next) {
        for (KrtIRBasicBlock* block = fn->entry_block; block; block = block->next) {
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                if (inst->opcode == KRT_IR_ADDRESS_OF || inst->opcode == KRT_IR_STACKALLOC ||
                    inst->opcode == KRT_IR_CALL_INDIRECT) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool module_fits_kro_limits(KrtIRModule* module) {
    for (KrtIRFunction* fn = module->functions; fn; fn = fn->next) {
        if (fn->param_count > KRT_KRO_MAX_ARGS) {
            KrtError("KRO function %s exceeds the %d parameter limit", fn->name, KRT_KRO_MAX_ARGS);
            return false;
        }
        int locals = 0, temporaries = 0;
        const char* names[KRT_KRO_MAX_LOCAL_VARS * 2] = {0};
        for (KrtIRBasicBlock* block = fn->entry_block; block; block = block->next) {
            for (KrtIRInst* inst = block->first_inst; inst; inst = inst->next) {
                temporaries += inst->result.type == KRT_IR_VALUE_TEMP;
                for (int i = 0; i < inst->operand_count; i++) {
                    KrtIRValue* value = &inst->operands[i];
                    if (value->type != KRT_IR_VALUE_VAR || !value->data.name ||
                        KrtIrModuleFindGlobal(module, value->data.name)) {
                        continue;
                    }
                    unsigned hash = 2166136261u;
                    for (const unsigned char* p = (const unsigned char*)value->data.name; *p; p++) {
                        hash = (hash ^ *p) * 16777619u;
                    }
                    unsigned slot = hash % (KRT_KRO_MAX_LOCAL_VARS * 2);
                    while (names[slot] && strcmp(names[slot], value->data.name)) {
                        slot = (slot + 1) % (KRT_KRO_MAX_LOCAL_VARS * 2);
                    }
                    if (!names[slot]) {
                        names[slot] = value->data.name;
                        locals++;
                    }
                    if (locals > KRT_KRO_MAX_LOCAL_VARS) {
                        goto storage_limit;
                    }
                }
            }
        }
        if (locals > KRT_KRO_MAX_LOCAL_VARS || temporaries > KRT_KRO_MAX_TEMP_REGS) {
        storage_limit:
            KrtError("KRO function %s exceeds the %d local or %d temporary storage limit", fn->name,
                     KRT_KRO_MAX_LOCAL_VARS, KRT_KRO_MAX_TEMP_REGS);
            return false;
        }
    }
    return true;
}

bool KrtCompilerCompile(KrtCompiler* compiler, ASTNode* ast, void* semantic_analyzer) {
    if (!compiler || !ast) {
        return false;
    }
    compiler->has_object_code = false;

    KrtIRBuilder* ir_builder = KrtIrBuilderCreate();
    if (!ir_builder) {
        return false;
    }

    KrtIrGenerateFromAst(ir_builder, ast, semantic_analyzer);
    if ((compiler->target == KRT_TARGET_X86_ASM || compiler->target == KRT_TARGET_VM_BYTECODE) &&
        module_has_native_pointers(ir_builder->module)) {
        KrtError("unsafe pointers require the KRO/native executable backend");
        KrtIrBuilderDestroy(ir_builder);
        return false;
    }
    if ((compiler->target == KRT_TARGET_X86_ASM || compiler->target == KRT_TARGET_VM_BYTECODE) &&
        module_has_extended_integers(ir_builder->module)) {
        KrtError("extended integer widths require the KRO/native executable backend");
        KrtIrBuilderDestroy(ir_builder);
        return false;
    }

    {
        KrtIRFunction* f = ir_builder->module->functions;
        while (f) {
            if (f->name && strcmp(f->name, "main") == 0) {
                KrtIRBasicBlock* b = f->entry_block;
                KrtIRInst* inst = b->first_inst;
                while (inst) {
                    inst = inst->next;
                }
            }
            f = f->next;
        }
    }

    ir_builder->module->optimization_level = compiler->optimization_level;
    IROptimizer* optimizer = ir_optimizer_create();
    if (optimizer) {
        OptimizationFlags opt_flags =
            OPT_CONSTANT_FOLDING | OPT_DEAD_CODE_ELIMINATION | OPT_STRENGTH_REDUCTION | OPT_LOOP_INVARIANT_CODE_MOTION;
        if (compiler->optimization_level == 0) {
            opt_flags = 0;
        }
        if (compiler->optimization_level >= 3) {
            opt_flags |= OPT_FUNCTION_INLINING;
        }
        ir_optimize_module(optimizer, ir_builder->module, opt_flags);
        ir_optimizer_destroy(optimizer);
    }

    if ((compiler->target == KRT_TARGET_KRO_OBJ || compiler->target == KRT_TARGET_EXE_PLATFORM) &&
        !module_fits_kro_limits(ir_builder->module)) {
        KrtIrBuilderDestroy(ir_builder);
        return false;
    }
    compiler->has_object_code = ir_builder->module->functions != NULL || ir_builder->module->global_count > 0 ||
                                ir_builder->module->string_const_count > 0;
    switch (compiler->target) {
    case KRT_TARGET_IR_TEXT:
        KrtIrPrint(ir_builder->module, compiler->output_file);
        break;
    case KRT_TARGET_X86_ASM:
        KrtX86Generate(compiler->output_file, ir_builder->module);
        break;
    case KRT_TARGET_WASM:
        fprintf(compiler->output_file, "; WASAS 生成尚未实现\n");
        break;
    case KRT_TARGET_VM_BYTECODE: {
        KrtVmCodegenGenerate(ir_builder->module, &compiler->last_chunk);

        fclose(compiler->output_file);
        compiler->output_file = NULL;

        char output_filename[256];
        KRT_STRNCPY(output_filename, compiler->output_filename, sizeof(output_filename));
        char* dot = strrchr(output_filename, '.');
        if (dot) {
            strcpy(dot, ".ebc");
        } else {
            strcat(output_filename, ".ebc");
        }

        KrtBytecodeGeneratorSerializeToFile(&compiler->last_chunk, output_filename);
        break;
    }
    case KRT_TARGET_KRO_OBJ: {
        KrtKrtGenerate(compiler->output_file, compiler->output_filename, ir_builder->module);
        break;
    }
    case KRT_TARGET_EXE_PLATFORM: {
        KrtKrtGenerate(compiler->output_file, compiler->output_filename, ir_builder->module);
        break;
    }
    default:
        KrtIrPrint(ir_builder->module, compiler->output_file);
        break;
    }

    KrtIrBuilderDestroy(ir_builder);
    return true;
}
