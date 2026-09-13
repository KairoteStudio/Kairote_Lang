#ifndef KRT_PARALLEL_COMPILER_H
#define KRT_PARALLEL_COMPILER_H

#include "../../Core/Utils/KrtCommon.h"
#include "Project.h"
#include "../../Core/Platform/ThreadPool.h"
#include "../Frontend/Semantic/Generics.h"
#include "ConfigManager.h"
#include <pthread.h>

typedef struct CompileTask {
    char* input_file;
    char* output_file;
    char* obj_file;
    int target_type;
    int show_ir;
    int result;
    char* error_message;
    double duration;
    void* compiler_context;
} CompileTask;

typedef struct ParallelCompiler {
    ThreadPool* thread_pool;
    CompileTask** tasks;
    int task_count;
    int max_threads;
    int next_task;
    pthread_mutex_t result_mutex;
    pthread_mutex_t registry_mutex;
    int any_failed;
    KrtConfig* config;
    struct {
        int total_files;
        int succeeded;
        int failed;
        double total_time;
    } stats;
    GenericRegistry* shared_generic_registry;
} ParallelCompiler;

ParallelCompiler* ParallelCompilerCreate(int max_threads, KrtConfig* config);
void ParallelCompilerDestroy(ParallelCompiler* compiler);
int ParallelCompilerAddFile(ParallelCompiler* compiler, const char* input_file, const char* output_file,
                            const char* obj_file, int target_type, int show_ir);
int ParallelCompilerExecute(ParallelCompiler* compiler);

void ParallelCompilerGetStats(ParallelCompiler* compiler, int* total, int* succeeded, int* failed);

int ParallelCompilerLinkResults(ParallelCompiler* compiler, const char* final_output);

int ParallelCompilerCollectGenericTypes(ParallelCompiler* compiler);

void FindRuntimeObj(const char* obj_name, char* result, size_t size);

#endif
