#include "ParallelCompiler.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef _WIN32
#include <process.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif
#include "../../Core/Utils/Path.h"
#include "ArkLinkIntegration.h"

#include "../Pipeline/CompilerPipeline.h"

#ifndef KRT_USE_ARKLINK
#define KRT_USE_ARKLINK 1
#endif

static void collect_compile_error(CompileTask* task, const char* error_msg) {
    if (!task || !error_msg) {
        return;
    }

    if (task->error_message) {
        KRT_FREE(task->error_message);
    }

    task->error_message = KRT_STRDUP(error_msg);
}

static int run_nasm(const char* format, const char* input, const char* output) {
    char* arguments[] = {"nasm", "-f", (char*)format, (char*)input, "-o", (char*)output, NULL};
#ifdef _WIN32
    intptr_t result = _spawnvp(_P_WAIT, "nasm", (const char* const*)arguments);
    return result == 0 ? 0 : -1;
#else
    pid_t child = 0;
    int spawn_error = posix_spawnp(&child, "nasm", NULL, NULL, arguments, environ);
    if (spawn_error != 0) {
        return -1;
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(status)) {
        return -1;
    }
    return WEXITSTATUS(status);
#endif
}

static void compile_task(CompileTask* task, KrtCompilePipeline* pipeline) {
    double start_time = KrtTimeNowSeconds();
    task->has_object_code = false;

    ParallelCompiler* compiler = (ParallelCompiler*)task->compiler_context;
    int result;

    if (!pipeline) {
        result = -1;
    } else {

        result = KrtCompilePipelineExecute(pipeline, task->input_file, task->output_file);
        task->has_object_code = result == 1 && pipeline->compiler && pipeline->compiler->has_object_code;

        if (result == 1 && task->has_object_code && task->obj_file) {
            KrtPlatformType platform = compiler->config ? compiler->config->platform : KRT_CONFIG_PLATFORM_WINDOWS;
            int assemble_res = run_nasm(platform == KRT_CONFIG_PLATFORM_WINDOWS ? "win64" : "elf64",
                                        task->output_file, task->obj_file);
            if (assemble_res != 0) {
                result = -1;
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), "汇编失败: %s -> %s (退出码 %d)", task->output_file,
                         task->obj_file, assemble_res);
                collect_compile_error(task, error_msg);
            }
        }
    }

    task->duration = KrtTimeNowSeconds() - start_time;

    if (result != 1) {
        if (!task->error_message) {
            char error_msg[512];
            snprintf(error_msg, sizeof(error_msg), "编译失败: %s (耗时: %.2fs)", task->input_file, task->duration);
            collect_compile_error(task, error_msg);
        }
        task->result = -1;
    } else {
        task->result = 0;
    }
}

static void compile_batch_worker(void* arg) {
    ParallelCompiler* compiler = (ParallelCompiler*)arg;
    KrtConfig config = compiler->config ? *compiler->config : (KrtConfig){0};
    config.imported_files = NULL;
    config.imported_file_count = config.imported_file_capacity = 0;
    KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(&config, NULL);
    for (;;) {
        pthread_mutex_lock(&compiler->result_mutex);
        int index = compiler->next_task++;
        pthread_mutex_unlock(&compiler->result_mutex);
        if (index >= compiler->task_count) {
            break;
        }
        CompileTask* task = compiler->tasks[index];
        KRT_FREE(task->error_message);
        task->error_message = NULL;
        config.target_type = (KrtTargetType)task->target_type;
        config.show_ir = task->show_ir;
        compile_task(task, pipeline);
        KrtCompilePipelineReset(pipeline);
    }
    KrtCompilePipelineDestroy(pipeline);
    for (int i = 0; i < config.imported_file_count; i++) {
        KRT_FREE(config.imported_files[i]);
    }
    KRT_FREE(config.imported_files);
}

ParallelCompiler* ParallelCompilerCreate(int max_threads, KrtConfig* config) {
    if (max_threads <= 0) {
        max_threads = 4;
    }

    ParallelCompiler* compiler = (ParallelCompiler*)KRT_MALLOC(sizeof(ParallelCompiler));
    if (!compiler) {
        KrtError("无法分配并行编译器上下文");
        return NULL;
    }

    compiler->thread_pool = NULL;

    compiler->tasks = NULL;
    compiler->task_count = 0;
    compiler->max_threads = max_threads;
    compiler->next_task = 0;
    compiler->any_failed = 0;
    compiler->config = config;
    memset(&compiler->stats, 0, sizeof(compiler->stats));

    compiler->shared_generic_registry = generics_create_registry();
    if (!compiler->shared_generic_registry) {
        KrtError("无法创建泛型注册表");
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
    if (!compiler) {
        return;
    }

    for (int i = 0; i < compiler->task_count; i++) {
        if (compiler->tasks[i]) {
            KRT_FREE(compiler->tasks[i]->input_file);
            KRT_FREE(compiler->tasks[i]->output_file);
            if (compiler->tasks[i]->obj_file) {
                KRT_FREE(compiler->tasks[i]->obj_file);
            }
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

int ParallelCompilerAddFile(ParallelCompiler* compiler, const char* input_file, const char* output_file,
                            const char* obj_file, int target_type, int show_ir) {
    if (!compiler || !input_file || !output_file) {
        return -1;
    }

    CompileTask* task = (CompileTask*)KRT_MALLOC(sizeof(CompileTask));
    if (!task) {
        KrtError("无法分配编译任务: %s", input_file);
        return -1;
    }

    task->input_file = KRT_STRDUP(input_file);
    task->output_file = KRT_STRDUP(output_file);
    task->obj_file = obj_file ? KRT_STRDUP(obj_file) : NULL;
    if (!task->input_file || !task->output_file || (obj_file && !task->obj_file)) {
        KRT_FREE(task->input_file);
        KRT_FREE(task->output_file);
        KRT_FREE(task->obj_file);
        KRT_FREE(task);
        return -1;
    }
    task->target_type = target_type;
    task->show_ir = show_ir;
    task->result = -1;
    task->has_object_code = false;
    task->error_message = NULL;
    task->duration = 0.0;
    task->compiler_context = compiler;

    if ((size_t)compiler->task_count >= (size_t)-1 / sizeof(*compiler->tasks)) {
        KRT_FREE(task->input_file);
        KRT_FREE(task->output_file);
        KRT_FREE(task->obj_file);
        KRT_FREE(task);
        return -1;
    }

    CompileTask** expanded_tasks =
        (CompileTask**)KRT_REALLOC(compiler->tasks, (size_t)(compiler->task_count + 1) * sizeof(*compiler->tasks));
    if (!expanded_tasks) {
        KrtError("无法扩展编译任务表");
        KRT_FREE(task->input_file);
        KRT_FREE(task->output_file);
        KRT_FREE(task->obj_file);
        KRT_FREE(task);
        return -1;
    }

    compiler->tasks = expanded_tasks;
    compiler->tasks[compiler->task_count] = task;
    compiler->task_count++;

    return 0;
}

int ParallelCompilerExecute(ParallelCompiler* compiler) {
    if (!compiler || compiler->task_count == 0) {
        return 0;
    }

    compiler->stats.total_files = compiler->task_count;
    compiler->stats.succeeded = 0;
    compiler->stats.failed = 0;
    compiler->any_failed = 0;

    double total_start_time = KrtTimeNowSeconds();

    compiler->next_task = 0;
    int workers = compiler->max_threads;
    if (workers > compiler->task_count) {
        workers = compiler->task_count;
    }
    if (workers < 1) {
        workers = 1;
    }
    if (workers < 2) {
        compile_batch_worker(compiler);
    } else {
        if (!compiler->thread_pool) {
            compiler->thread_pool = thread_pool_create(compiler->max_threads);
        }
        if (!compiler->thread_pool) {
            compile_batch_worker(compiler);
        } else {
            for (int i = 0; i < workers; i++) {
                thread_pool_submit(compiler->thread_pool, compile_batch_worker, compiler);
            }
            thread_pool_wait(compiler->thread_pool);
        }
    }

    compiler->stats.total_time = KrtTimeNowSeconds() - total_start_time;

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
        if (current_dir) {
            KRT_FREE(current_dir);
        }
        return;
    }

    const char* subdirs[] = {"obj" KRT_PATH_SEPARATOR_STR "runtime",
                             "obj" KRT_PATH_SEPARATOR_STR "core" KRT_PATH_SEPARATOR_STR "utils",
                             "obj" KRT_PATH_SEPARATOR_STR "core" KRT_PATH_SEPARATOR_STR "memory",
                             "obj" KRT_PATH_SEPARATOR_STR "compiler",
                             "obj" KRT_PATH_SEPARATOR_STR "common",
                             "build"};
    for (size_t i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
        char temp[KRT_MAX_PATH];
        KrtPathJoin(temp, sizeof(temp), subdirs[i], obj_name);
        if (KrtPathExists(temp)) {
            KRT_STRCPY_S(result, size, temp);
            if (current_dir) {
                KRT_FREE(current_dir);
            }
            return;
        }
    }

    if (current_dir) {
        char parent_obj[KRT_MAX_PATH];
        KrtPathJoin(parent_obj, sizeof(parent_obj), current_dir, "..");

        for (size_t i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
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
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR
            "core" KRT_PATH_SEPARATOR_STR "utils",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR
            "core" KRT_PATH_SEPARATOR_STR "memory",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "compiler",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "obj" KRT_PATH_SEPARATOR_STR "common",
            ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "build"};
        for (size_t i = 0; i < sizeof(exe_rel_base) / sizeof(exe_rel_base[0]); i++) {
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

    result[0] = '\0';
}

void ParallelCompilerGetStats(ParallelCompiler* compiler, int* total, int* succeeded, int* failed) {
    if (!compiler) {
        return;
    }

    if (total) {
        *total = compiler->stats.total_files;
    }
    if (succeeded) {
        *succeeded = compiler->stats.succeeded;
    }
    if (failed) {
        *failed = compiler->stats.failed;
    }
}

/* Blank out comments and string/char literal contents so the generic-type
 * scan cannot mistake text inside them for a declaration. */
static char* mask_noncode(const char* source, size_t length) {
    char* masked = (char*)KRT_MALLOC(length + 1);
    if (!masked) {
        return NULL;
    }
    memcpy(masked, source, length + 1);

    size_t i = 0;
    while (i < length) {
        char current = masked[i];
        if (current == '/' && i + 1 < length && masked[i + 1] == '/') {
            while (i < length && masked[i] != '\n') {
                masked[i++] = ' ';
            }
        } else if (current == '/' && i + 1 < length && masked[i + 1] == '*') {
            masked[i++] = ' ';
            masked[i++] = ' ';
            while (i < length && !(masked[i] == '*' && i + 1 < length && masked[i + 1] == '/')) {
                masked[i++] = ' ';
            }
            if (i < length) {
                masked[i++] = ' ';
                masked[i++] = ' ';
            }
        } else if (current == '"' || current == '\'') {
            char quote = current;
            masked[i++] = ' ';
            while (i < length && masked[i] != quote) {
                if (masked[i] == '\\' && i + 1 < length) {
                    masked[i++] = ' ';
                }
                masked[i++] = ' ';
            }
            if (i < length) {
                masked[i++] = ' ';
            }
        } else {
            i++;
        }
    }
    return masked;
}

int ParallelCompilerCollectGenericTypes(ParallelCompiler* compiler) {
    if (!compiler || !compiler->shared_generic_registry) {
        return -1;
    }

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

        char* masked = mask_noncode(source, read_bytes);
        if (!masked) {
            KrtError("内存分配失败: 泛型扫描缓冲区: %s", task->input_file);
            KRT_FREE(source);
            continue;
        }

        char* pos = masked;
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
            while (*name_end && (*name_end != '{' && *name_end != '<' && *name_end != ' ' && *name_end != '\n' &&
                                 *name_end != '\t' && *name_end != ',')) {
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
                if (!type_name) {
                    KrtError("内存分配失败: 泛型类型名");
                    KRT_FREE(source);
                    KRT_FREE(masked);
                    return -1;
                }
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
                            if (*p == ',') {
                                param_count++;
                            }
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

        KRT_FREE(masked);
        KRT_FREE(source);
    }

    return 0;
}
