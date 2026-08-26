#include "ParallelCompiler.h"
#include "ArkLinkIntegration.h"
#include "../../Core/Utils/Path.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

extern void FindRuntimeObj(const char* obj_name, char* result, size_t size);

int ParallelCompilerLinkResults(ParallelCompiler* compiler, const char* final_output) {
    if (!compiler || !final_output) return -1;

    if (compiler->any_failed) {
        return -1;
    }

    KrtArkLinkContext* ark_ctx = KrtArkLinkContextCreate(compiler->config);
    if (!ark_ctx) {
        KrtError("Failed to create ArkLink context");
        return -1;
    }

    int success_count = 0;
    for (int i = 0; i < compiler->task_count; i++) {
        CompileTask* task = compiler->tasks[i];
        if (task->result == 0) {
            const char* file_to_link = task->obj_file ? task->obj_file : task->output_file;
            if (file_to_link) {
                if (KrtArkLinkAddObjectFile(ark_ctx, file_to_link) == 0) {
                    success_count++;
                }
            }
        }
    }

    if (success_count == 0) {
        KrtError("No object files to link");
        KrtArkLinkContextDestroy(ark_ctx);
        return -1;
    }

    char runtime_obj[KRT_MAX_PATH];
    char cache_obj[KRT_MAX_PATH];
    char allocator_obj[KRT_MAX_PATH];
    char KrtStringObj[KRT_MAX_PATH];

    FindRuntimeObj("runtime.o", runtime_obj, sizeof(runtime_obj));
    FindRuntimeObj("output_cache.o", cache_obj, sizeof(cache_obj));
    FindRuntimeObj("allocator.o", allocator_obj, sizeof(allocator_obj));
    FindRuntimeObj("KrtString.o", KrtStringObj, sizeof(KrtStringObj));

    KrtArkLinkAddObjectFile(ark_ctx, runtime_obj);
    KrtArkLinkAddObjectFile(ark_ctx, cache_obj);
    KrtArkLinkAddObjectFile(ark_ctx, allocator_obj);
    KrtArkLinkAddObjectFile(ark_ctx, KrtStringObj);

    if (KrtArkLinkLoadProjectLibraries(ark_ctx, compiler->config) != 0) {
        KrtArkLinkContextDestroy(ark_ctx);
        return -1;
    }

    if (KrtArkLinkSetOutput(ark_ctx, final_output) != 0) {
        KrtError("Failed to set output path");
        KrtArkLinkContextDestroy(ark_ctx);
        return -1;
    }

    int link_result = KrtArkLinkLink(ark_ctx);

    KrtArkLinkContextDestroy(ark_ctx);

    if (link_result != 0) {
        KrtError("ArkLink linking failed");
        return -1;
    }

    return 0;
}
