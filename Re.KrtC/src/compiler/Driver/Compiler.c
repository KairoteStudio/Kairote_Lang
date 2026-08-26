#include "Compiler.h"
#include "../Middle/Ir/IrSsa.h"
#include "../Middle/Ir/IrOptimizer.h"
#include "../Backend/X86/X86Codegen.h"
#include "../Backend/Vm/VmCodegen.h"
#include "../Backend/Kro/KroCodegen.h"

KrtCompiler* KrtCompilerCreate(const char* output_filename, KrtTargetPlatform target) {
    KrtCompiler* compiler = (KrtCompiler*)KRT_MALLOC(sizeof(KrtCompiler));
    if (!compiler) return NULL;

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
    KrtBytecodeGeneratorInitChunk(&compiler->last_chunk);

    return compiler;
}

void KrtCompilerDestroy(KrtCompiler* compiler) {
    if (!compiler) return;

    if (compiler->output_file) {
        fclose(compiler->output_file);
        compiler->output_file = NULL;
    }
    KrtBytecodeGeneratorFreeChunk(&compiler->last_chunk);
    KRT_FREE(compiler);
}

void KrtCompilerCompile(KrtCompiler* compiler, ASTNode* ast, void* semantic_analyzer) {
    if (!compiler || !ast) {
        return;
    }

    KrtIRBuilder* ir_builder = KrtIrBuilderCreate();
    if (!ir_builder) {
        return;
    }

    KrtIrGenerateFromAst(ir_builder, ast, semantic_analyzer);

    {
        KrtIRFunction* f = ir_builder->module->functions;
        while (f) {
            if (f->name && strcmp(f->name, "main") == 0) {
                KrtIRBasicBlock* b = f->entry_block;
                int count = 0;
                KrtIRInst* inst = b->first_inst;
                while (inst) {
                    inst = inst->next;
                }
            }
            f = f->next;
        }
    }


    IROptimizer* optimizer = ir_optimizer_create();
    if (optimizer) {
        OptimizationFlags opt_flags = OPT_CONSTANT_FOLDING | OPT_DEAD_CODE_ELIMINATION |
                                     OPT_STRENGTH_REDUCTION | OPT_LOOP_INVARIANT_CODE_MOTION;
        ir_optimize_module(optimizer, ir_builder->module, opt_flags);
        ir_optimizer_destroy(optimizer);
    }

    if (getenv("KRT_DUMP_IR")) KrtIrPrint(ir_builder->module, stderr);

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
}
