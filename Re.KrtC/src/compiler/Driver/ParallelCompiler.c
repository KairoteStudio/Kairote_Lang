#include "ParallelCompiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../../Core/Utils/Path.h"
#include "ArkLinkIntegration.h"

extern void *memset(void *s, int c, size_t n);
extern size_t strlen(const char *s);
extern char *strstr(const char *haystack, const char *needle);
extern char *strncpy(char *dest, const char *src, size_t n);

#include "../Pipeline/CompilerPipeline.h"

#ifndef KRT_USE_ARKLINK
#define KRT_USE_ARKLINK 1
#endif

static void CollectCompileError(CompileTask* task, const char* error_msg) {
    if (!task || !error_msg) return;

    if (task->error_message) {
        KRT_FREE(task->error_message);
    }

    task->error_message = KRT_STRDUP(error_msg);
}

static void CompileTaskWorker(void* arg) {
    CompileTask* task = (CompileTask*)arg;
    double start_time = KrtTimeNowSeconds();

    ParallelCompiler* compiler = (ParallelCompiler*)task->compiler_context;
    int result;
    
    KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(compiler->config, NULL);
    if (!pipeline) {
        result = -1;
    } else {
        
        result = KrtCompilePipelineExecute(pipeline, task->input_file, task->output_file);
        
        KrtCompilePipelineDestroy(pipeline);
        
        if (result == 1 && task->obj_file) {
            char assemble_cmd[1024];
            const char* format = NULL;
            KrtPlatformType platform = compiler->config ? compiler->config->platform : KRT_CONFIG_PLATFORM_WINDOWS;
            
            if (platform == KRT_CONFIG_PLATFORM_WINDOWS) {
                format = "nasm -f win64 %s -o %s";
            } else {
                format = "nasm -f elf64 %s -o %s";
            }
            
            snprintf(assemble_cmd, sizeof(assemble_cmd), format, task->output_file, task->obj_file);
            int assemble_res = system(assemble_cmd);
            if (assemble_res != 0) {
                result = -1;
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), "汇编失败: %s -> %s", task->output_file, task->obj_file);
                CollectCompileError(task, error_msg);
            } else {
                
            }
        }
        
    }
    
    task->duration = KrtTimeNowSeconds() - start_time;

    if (result != 1) { 
        if (!task->error_message) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "编译失败: %s (耗时: %.2fs)",
                     task->input_file, task->duration);
            CollectCompileError(task, error_msg);
        }
        task->result = -1;
    } else {
        task->result = 0;
    }
}

ParallelCompiler* ParallelCompilerCreate(int max_threads, KrtConfig* config) {
    if (max_threads <= 0) {
        max_threads = 4;
    }

    ParallelCompiler* compiler = (ParallelCompiler*)KRT_MALLOC(sizeof(ParallelCompiler));
    if (!compiler) return NULL;

    compiler->thread_pool = thread_pool_create(max_threads);
    if (!compiler->thread_pool) {
        KRT_FREE(compiler);
        return NULL;
    }

    compiler->tasks = NULL;
    compiler->task_count = 0;
    compiler->max_threads = max_threads;
    compiler->any_failed = 0;
    compiler->config = config;
    memset(&compiler->stats, 0, sizeof(compiler->stats));

    compiler->shared_generic_registry = generics_create_registry();
    if (!compiler->shared_generic_registry) {
        thread_pool_destroy(compiler->thread_pool);
        KRT_FREE(compiler);
        return NULL;
    }

    if (pthread_mutex_init(&compiler->result_mutex, NULL) != 0) {
        thread_pool_destroy(compiler->thread_pool);
        generics_destroy_registry(compiler->shared_generic_registry);
        KRT_FREE(compiler);
        return NULL;
    }

    if (pthread_mutex_init(&compiler->registry_mutex, NULL) != 0) {
        pthread_mutex_destroy(&compiler->result_mutex);
        thread_pool_destroy(compiler->thread_pool);
        generics_destroy_registry(compiler->shared_generic_registry);
        KRT_FREE(compiler);
        return NULL;
    }

    return compiler;
}

void ParallelCompilerDestroy(ParallelCompiler* compiler) {
    if (!compiler) return;

    for (int i = 0; i < compiler->task_count; i++) {
        if (compiler->tasks[i]) {
            KRT_FREE(compiler->tasks[i]->input_file);
            KRT_FREE(compiler->tasks[i]->output_file);
            if (compiler->tasks[i]->obj_file) KRT_FREE(compiler->tasks[i]->obj_file);
            KRT_FREE(compiler->tasks[i]->error_message);
            KRT_FREE(compiler->tasks[i]);
        }
    }
    KRT_FREE(compiler->tasks);

    pthread_mutex_destroy(&compiler->result_mutex);
    pthread_mutex_destroy(&compiler->registry_mutex);
    thread_pool_destroy(compiler->thread_pool);
    
    if (compiler->shared_generic_registry) {
        generics_destroy_registry(compiler->shared_generic_registry);
    }
    
    KRT_FREE(compiler);
}

int ParallelCompilerAddFile(ParallelCompiler* compiler, const char* input_file, const char* output_file, const char* obj_file, int target_type, int show_ir) {
    if (!compiler || !input_file || !output_file) return -1;

    CompileTask* task = (CompileTask*)KRT_MALLOC(sizeof(CompileTask));
    if (!task) return -1;

    task->input_file = KRT_STRDUP(input_file);
    task->output_file = KRT_STRDUP(output_file);
    task->obj_file = obj_file ? KRT_STRDUP(obj_file) : NULL;
    task->target_type = target_type;
    task->show_ir = show_ir;
    task->result = -1;
    task->error_message = NULL;
    task->duration = 0.0;
    task->compiler_context = compiler;  

    compiler->tasks = (CompileTask**)KRT_REALLOC(compiler->tasks, (compiler->task_count + 1) * sizeof(CompileTask*));
    if (!compiler->tasks) {
        KRT_FREE(task->input_file);
        KRT_FREE(task->output_file);
        KRT_FREE(task);
        return -1;
    }

    compiler->tasks[compiler->task_count] = task;
    compiler->task_count++;

    return 0;
}

int ParallelCompilerExecute(ParallelCompiler* compiler) {
    if (!compiler || compiler->task_count == 0) return 0;

    compiler->stats.total_files = compiler->task_count;
    compiler->stats.succeeded = 0;
    compiler->stats.failed = 0;
    compiler->any_failed = 0;

    double total_start_time = KrtTimeNowSeconds();

    for (int i = 0; i < compiler->task_count; i++) {
        thread_pool_submit(compiler->thread_pool, CompileTaskWorker, compiler->tasks[i]);
    }

    thread_pool_wait(compiler->thread_pool);

    (void)(KrtTimeNowSeconds() - total_start_time);

    pthread_mutex_lock(&compiler->result_mutex);
    for (int i = 0; i < compiler->task_count; i++) {
        CompileTask* task = compiler->tasks[i];
        if (task->result == 0) {
            compiler->stats.succeeded++;
        } else {
            compiler->stats.failed++;
            compiler->any_failed = 1;

            if (task->error_message) {
                KrtError("错误: %s", task->error_message);
            }
        }
    }
    pthread_mutex_unlock(&compiler->result_mutex);

    return compiler->any_failed ? -1 : 0;
}

void FindRuntimeObj(const char* obj_name, char* result, size_t size) {
    char* current_dir = KrtGetCurrentDirectory();

    if (KrtPathExists(obj_name)) {
        KRT_STRCPY_S(result, size, obj_name);
        if (current_dir) KRT_FREE(current_dir);
        return;
    }

    const char* subdirs[] = {
        "obj" KRT_PATH_SEPARATOR_STR "runtime",
        "obj" KRT_PATH_SEPARATOR_STR "core" KRT_PATH_SEPARATOR_STR "utils",
        "obj" KRT_PATH_SEPARATOR_STR "core" KRT_PATH_SEPARATOR_STR "memory",
        "obj" KRT_PATH_SEPARATOR_STR "compiler",
        "obj" KRT_PATH_SEPARATOR_STR "common",
        "build"
    };
    for (size_t i = 0; i < sizeof(subdirs)/sizeof(subdirs[0]); i++) {
        char temp[KRT_MAX_PATH];
        KrtPathJoin(temp, sizeof(temp), subdirs[i], obj_name);
        if (KrtPathExists(temp)) {
            KRT_STRCPY_S(result, size, temp);
            if (current_dir) KRT_FREE(current_dir);
            return;
        }
    }

    if (current_dir) {
        char parent_obj[KRT_MAX_PATH];
        KrtPathJoin(parent_obj, sizeof(parent_obj), current_dir, "..");

        for (size_t i = 0; i < sizeof(subdirs)/sizeof(subdirs[0]); i++) {
            char full_path[KRT_MAX_PATH];
            char temp_path[KRT_MAX_PATH];
            KrtPathJoin(full_path, sizeof(full_path), parent_obj, subdirs[i]);
            KrtPathJoin(temp_path, sizeof(temp_path), full_path, obj_name);
            if (KrtPathExists(temp_path)) {
                KRT_STRCPY_S(result, size, temp_path);
                KRT_FREE(current_dir);
                return;
            }
        }
        KRT_FREE(current_dir);
    }

    char exe_dir[KRT_MAX_PATH];
    if (KrtGetExecutableDirectory(exe_dir, sizeof(exe_dir)) == 0) {
        
        char temp[KRT_MAX_PATH];
        KrtPathJoin(temp, sizeof(temp), exe_dir, obj_name);
        if (KrtPathExists(temp)) {
            KRT_STRCPY_S(result, size, temp);
            return;
        }

        const char* exe_rel_base[] = {
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "runtime",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "core" KRT_PATH_SEPARATOR_STR "utils",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "core" KRT_PATH_SEPARATOR_STR "memory",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "compiler",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "common",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "build"
        };
        for (size_t i = 0; i < sizeof(exe_rel_base)/sizeof(exe_rel_base[0]); i++) {
            char rel_path[KRT_MAX_PATH];
            char temp2[KRT_MAX_PATH];
            KrtPathJoin(rel_path, sizeof(rel_path), exe_dir, exe_rel_base[i]);
            KrtPathJoin(temp2, sizeof(temp2), rel_path, obj_name);
            if (KrtPathExists(temp2)) {
                KRT_STRCPY_S(result, size, temp2);
                return;
            }
        }
    }

    KRT_STRCPY_S(result, size, obj_name);
}

void ParallelCompilerGetStats(ParallelCompiler* compiler, int* total, int* succeeded, int* failed) {
    if (!compiler) return;

    if (total) *total = compiler->stats.total_files;
    if (succeeded) *succeeded = compiler->stats.succeeded;
    if (failed) *failed = compiler->stats.failed;
}

int ParallelCompilerCollectGenericTypes(ParallelCompiler* compiler) {
    if (!compiler || !compiler->shared_generic_registry) return -1;
    
    for (int i = 0; i < compiler->task_count; i++) {
        CompileTask* task = compiler->tasks[i];
        
        FILE* fp = fopen(task->input_file, "r");
        if (!fp) {
            continue;
        }
        
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            continue;
        }
        
        long file_size = ftell(fp);
        if (file_size <= 0) {
            fclose(fp);
            continue;
        }
        
        rewind(fp);
        char* source = (char*)KRT_MALLOC(file_size + 1);
        if (!source) {
            KrtError("内存分配失败: %s", task->input_file);
            fclose(fp);
            continue;
        }
        
        size_t read_bytes = fread(source, 1, file_size, fp);
        fclose(fp);
        source[read_bytes] = '\0';
        
        char* pos = source;
        while ((pos = strstr(pos, "template")) != NULL) {
            
            char* search_pos = pos + 8; 
            char* class_pos = NULL;
            
            while ((class_pos = strstr(search_pos, "class")) != NULL) {
                
                char* where_pos = strstr(search_pos, "where");
                char* brace_pos = strstr(search_pos, "{");
                
                if (where_pos && where_pos < class_pos && brace_pos && class_pos < brace_pos) {
                    search_pos = class_pos + 5;
                    continue; 
                }
                
                break;
            }
            
            if (!class_pos) {
                pos++;
                continue;
            }
            
            char* name_start = class_pos + 5;
            while (*name_start && (*name_start == ' ' || *name_start == '\t')) {
                name_start++;
            }
            
            char* name_end = name_start;
            while (*name_end && 
                   (*name_end != '{' && *name_end != '<' && *name_end != ' ' && 
                    *name_end != '\n' && *name_end != '\t' && *name_end != ',')) {
                name_end++;
            }
            
            if (name_end > name_start) {
                char* test = name_start;
                bool valid_name = true;
                
                if (!((*test >= 'a' && *test <= 'z') || (*test >= 'A' && *test <= 'Z') || *test == '_')) {
                    valid_name = false;
                } else {
                    
                    test++;
                    while (test < name_end) {
                        if (!((*test >= 'a' && *test <= 'z') || (*test >= 'A' && *test <= 'Z') || 
                              (*test >= '0' && *test <= '9') || *test == '_')) {
                            valid_name = false;
                            break;
                        }
                        test++;
                    }
                }
                
                if (!valid_name) {
                    pos = name_end;
                    continue;
                }
            }
            
            if (name_end > name_start) {
                int name_len = name_end - name_start;
                char* type_name = (char*)KRT_MALLOC(name_len + 1);
                strncpy(type_name, name_start, name_len);
                type_name[name_len] = '\0';
                
                int param_count = 1; 
                char* template_decl_start = strstr(pos, "<");
                if (template_decl_start && template_decl_start < class_pos) {
                    char* template_decl_end = strstr(template_decl_start, ">");
                    if (template_decl_end && template_decl_end < class_pos) {
                        
                        param_count = 1;
                        char* p = template_decl_start + 1;
                        while (p < template_decl_end) {
                            if (*p == ',') param_count++;
                            p++;
                        }
                    }
                }
                
                pthread_mutex_lock(&compiler->registry_mutex);
                if (!generics_lookup_type(compiler->shared_generic_registry, type_name)) {
                    
                    generics_register_type(compiler->shared_generic_registry, type_name, NULL, param_count, NULL);
                }
                pthread_mutex_unlock(&compiler->registry_mutex);
                
                KRT_FREE(type_name);
            }
            
            pos = name_end; 
        }
        
        KRT_FREE(source);
    }
    
    return 0;
}
