#include "ParallelCompiler.h"
#include "ArkLinkIntegration.h"
#include "../../Core/Utils/Path.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

extern void FindRuntimeObj(const char* obj_name, char* result, size_t size);

int ParallelCompilerLinkResults(ParallelCompiler* compiler, const char* final_output) {
    if (!compiler || !final_output) {
        return -1;
    }

    if (compiler->any_failed) {
        return -1;
    }

    KrtArkLinkContext* ark_ctx = KrtArkLinkContextCreate(compiler->config);
    if (!ark_ctx) {
        KrtError("Failed to create ArkLink context");
        return -1;
    }

    int success_count = 0;
    int no_object_code = 0;
    int missing_file = 0;
    int add_failed = 0;
    for (int i = 0; i < compiler->task_count; i++) {
        CompileTask* task = compiler->tasks[i];
        if (task->result != 0 || !task->has_object_code) {
            no_object_code++;
            continue;
        }
        const char* file_to_link = task->obj_file ? task->obj_file : task->output_file;
        if (!file_to_link) {
            missing_file++;
            continue;
        }
        if (KrtArkLinkAddObjectFile(ark_ctx, file_to_link) == 0) {
            success_count++;
        } else {
            add_failed++;
            KrtError("Failed to add object file: %s", file_to_link);
        }
    }

    if (success_count == 0) {
        if (add_failed > 0) {
            KrtError("%d object file(s) were rejected by the linker", add_failed);
        } else if (missing_file > 0) {
            KrtError("No object files to link: %d task(s) had no output path", missing_file);
        } else {
            KrtError("No object files to link: %d task(s) produced no object code", no_object_code);
        }
        KrtArkLinkContextDestroy(ark_ctx);
        return -1;
    }

    if (compiler->config->target_type != KRT_TARGET_KRO) {
        char runtime_obj[KRT_MAX_PATH];
        char cache_obj[KRT_MAX_PATH];
        char allocator_obj[KRT_MAX_PATH];
        char KrtStringObj[KRT_MAX_PATH];

        FindRuntimeObj("runtime.o", runtime_obj, sizeof(runtime_obj));
        FindRuntimeObj("output_cache.o", cache_obj, sizeof(cache_obj));
        FindRuntimeObj("allocator.o", allocator_obj, sizeof(allocator_obj));
        FindRuntimeObj("KrtString.o", KrtStringObj, sizeof(KrtStringObj));

        const char* runtime_paths[] = {runtime_obj, cache_obj, allocator_obj, KrtStringObj};
        const char* runtime_names[] = {"runtime.o", "output_cache.o", "allocator.o", "KrtString.o"};
        for (int i = 0; i < 4; i++) {
            if (runtime_paths[i][0] == '\0') {
                KrtError("运行时对象缺失: %s", runtime_names[i]);
                continue;
            }
            if (KrtArkLinkAddObjectFile(ark_ctx, runtime_paths[i]) != 0) {
                KrtError("Failed to add runtime object %s: %s", runtime_names[i], runtime_paths[i]);
            }
        }
    }

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
#ifndef _WIN32
    if (compiler->config->target_type != KRT_TARGET_KRO && chmod(final_output, 0755) != 0) {
        KrtError("Failed to make output executable: %s", final_output);
        return -1;
    }
#endif

    return 0;
}
