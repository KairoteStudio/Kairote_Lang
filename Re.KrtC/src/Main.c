#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef S_IFDIR
#define S_IFDIR _S_IFDIR
#endif
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#endif
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include "Core/Core.h"
#include "compiler/Driver/Compiler.h"
#include "compiler/Compiler.h"
#include "compiler/Driver/ConfigManager.h"
#include "compiler/Platform/PlatformAbstraction.h"
#include "compiler/Build/BuildSystem.h"
#include "compiler/Pipeline/CompilerPipeline.h"
#include "compiler/Driver/TaskManager.h"
#include "compiler/Driver/ConsoleUtils.h"
#include "Version.h"
#include "compiler/Frontend/Parser/Parser.h"
#include "compiler/Frontend/Lexer/Tokenizer.h"
#include "compiler/Driver/Project.h"
#include "compiler/Driver/ProjectKrt.h"
#include "compiler/Driver/ParallelCompiler.h"
#include "compiler/Frontend/Semantic/SemanticAnalyzer.h"
#include "compiler/Driver/Preprocessor.h"
#include "compiler/Frontend/Semantic/Generics.h"
#include "compiler/Driver/ArkLinkIntegration.h"
#include "Core/Utils/OutputCache.h"

static const char* project_name_from_type(const char* type) {
    if (!type) return "console";
    if (strcmp(type, "console") == 0) return "console";
    if (strcmp(type, "lib") == 0) return "library";
    if (strcmp(type, "library") == 0) return "library";
    if (strcmp(type, "web") == 0) return "web";
    return "console";
}

static void KrtCreateProject(const char* name, const char* type) {
    KrtProjectType ptype = KRT_PROJ_TYPE_CONSOLE;
    if (type && (strcmp(type, "lib") == 0 || strcmp(type, "library") == 0 || strcmp(type, "dll") == 0)) {
        ptype = KRT_PROJ_TYPE_LIBRARY;
    } else if (type && strcmp(type, "web") == 0) {
        ptype = KRT_PROJ_TYPE_WEB;
    } else if (type && strcmp(type, "system") == 0) {
        ptype = KRT_PROJ_TYPE_SYSTEM;
    }
    KrtProject* project = KrtProjCreate(name, ptype);
    if (project) {
        KrtProjCreateTemplate(project, name);
        KrtProjDestroy(project);
    } else {
        KrtError("项目创建失败!");
    }
}

typedef enum {
    KRT_TARGET_CMD_ASM = 0,
    KRT_TARGET_CMD_IR = 3,
    KRT_TARGET_CMD_EXE = 4,
    KRT_TARGET_CMD_VM = 5,
    KRT_TARGET_CMD_BUILD = 6,
    KRT_TARGET_CMD_CLEAN = 7,
    KRT_TARGET_CMD_CHECK = 8,
    KRT_TARGET_CMD_VERSION = 9,
    KRT_TARGET_CMD_KRO = 10    
} KrtCommandTargetType;

#define KRT_MAX_INPUT_FILES 32

typedef struct {
    const char* input_file;
    const char* input_files[KRT_MAX_INPUT_FILES];
    int input_file_count;
    const char* output_file;
    KrtCommandTargetType target_type;
    int show_ir;
    int show_help;
    int create_project;
    int keep_temp_files;
    const char* project_type;
    int output_file_set;
    int target_type_set;
} KrtCommandLineOptions;

static void KrtPrintUsage(const char* program_name) {
    (void)program_name;
    const char* blue = KrtColor(KRT_COL_BLUE);
    const char* gray = KrtColor(KRT_COL_GRAY);
    const char* reset = KrtColor(KRT_COL_RESET);
    
    KrtPrintf("%s||Kairote Lang 使用说明%s\n", blue, reset);
    KrtPrintf("%sbuild%s:%s构建%s  clean%s:%s清理\n", blue, gray, blue, blue, gray, blue);
    KrtPrintf("%scheck%s:%s检查%s  help%s:%s帮助信息\n", blue, gray, blue, blue, gray, blue);
    KrtPrintf("%s--keep-temp%s:%s保留临时文件 (.kro, .eo)\n", blue, gray, blue);
    KrtPrintf("\n%s=========== %s其他 %s===========\n", gray, gray, gray);
    KrtPrintf("%starget%s:%s输出类型 %s<%sir%s/%sasm%s/%sexe%s/%svm%s/%seo%s>\n", 
              blue, gray, blue, gray, blue, gray, blue, gray, blue, gray, blue, gray, blue, gray);
    KrtPrintf("%snew%s:%s创建项目 %s<%s类型%s> <%s项目名%s>\n", blue, gray, blue, gray, blue, gray, blue, gray);
    KrtPrintf("%sversion%s:%s %s\n", blue, gray, blue, KRT_COMPILER_VERSION);
    KrtPrintf("%s", reset);
}

static KrtCommandLineOptions KrtParseCommandLine(int argc, char* argv[]) {
    KrtCommandLineOptions options;
    memset(&options, 0, sizeof(options));
    options.target_type = KRT_TARGET_CMD_EXE;

    if (argc < 2) {
        options.show_help = 1;
        return options;
    }

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        
        if (strcmp(arg, "help") == 0) {
            options.show_help = 1;
        } else if (strcmp(arg, "--keep-temp") == 0) {
            options.keep_temp_files = 1;
        } else if (strcmp(arg, "new") == 0) {
            if (i + 2 >= argc) {
                KrtError("缺少参数: new <类型> <项目名>");
                exit(1);
            }
            options.create_project = 1;
            options.project_type = argv[++i];
            options.input_file = argv[++i];
        } else if (strcmp(arg, "build") == 0) {
            if (i + 1 < argc) {
                options.target_type = KRT_TARGET_CMD_BUILD;
                options.input_file = argv[++i];
                if (options.input_file_count < KRT_MAX_INPUT_FILES) {
                    options.input_files[options.input_file_count++] = options.input_file;
                }
            } else {
                options.target_type = KRT_TARGET_CMD_BUILD;
                if (KrtPathExists("project.krt")) {
                    if (options.input_file_count < KRT_MAX_INPUT_FILES) {
                        options.input_file = "project.krt";
                        options.input_files[options.input_file_count++] = options.input_file;
                    }
                } else {
                    KrtError("未找到 project.krt，请指定输入文件");
                    exit(1);
                }
            }
        } else if (strcmp(arg, "clean") == 0) {
            options.target_type = KRT_TARGET_CMD_CLEAN;
        } else if (strcmp(arg, "check") == 0) {
            options.target_type = KRT_TARGET_CMD_CHECK;
        } else if (strcmp(arg, "target") == 0) {
            if (i + 1 >= argc) {
                KrtError("缺少参数: target <类型>");
                exit(1);
            }
            options.target_type_set = 1;
            const char* target_type = argv[++i];
            if (strcmp(target_type, "asm") == 0) {
                options.target_type = KRT_TARGET_CMD_ASM;
            } else if (strcmp(target_type, "exe") == 0) {
                options.target_type = KRT_TARGET_CMD_EXE;
            } else if (strcmp(target_type, "ir") == 0) {
                options.target_type = KRT_TARGET_CMD_IR;
            } else if (strcmp(target_type, "vm") == 0) {
                options.target_type = KRT_TARGET_CMD_VM;
            } else if (strcmp(target_type, "eo") == 0) {
                options.target_type = KRT_TARGET_CMD_KRO;
            } else {
                KrtError("未知的目标类型 '%s'", target_type);
                exit(1);
            }
        } else if (strcmp(arg, "version") == 0) {
            options.target_type = KRT_TARGET_CMD_VERSION;
        } else if (strcmp(arg, "output") == 0) {
            if (i + 1 >= argc) {
                KrtError("缺少参数: output <输出文件>");
                exit(1);
            }
            options.output_file = argv[++i];
            options.output_file_set = 1;
        } else if (strcmp(arg, "--show-ir") == 0 || strcmp(arg, "--ir") == 0) {
            options.show_ir = 1;
         } else if (options.target_type != KRT_TARGET_CMD_CLEAN &&
                   options.target_type != KRT_TARGET_CMD_CHECK && options.target_type != KRT_TARGET_CMD_VERSION) {
            
            if (options.input_file_count < KRT_MAX_INPUT_FILES) {
                options.input_files[options.input_file_count++] = arg;
                if (options.input_file == NULL) {
                    options.input_file = arg; 
                }
            } else {
                KrtError("Too many input files (max %d)", KRT_MAX_INPUT_FILES);
                exit(1);
            }
        } else {
            KrtError("未知命令或参数: %s", arg);
            options.show_help = 1;
        }
    }

    return options;
}

static void KrtGetExecutableDir(char* buffer, int buffer_size);
static int KrtScanStdlibDir(const char* dir_path, char** file_list, int max_files, int* file_count);
static void KrtGetKroFilename(const char* krt_file, char* kro_file, int kro_file_size);
static int KrtNeedsRecompile(const char* krt_file, const char* kro_file);
static int KrtCompileStdlib(KrtConfig* config, KrtPlatform* platform,
                             char** stdlib_krt_files, int stdlib_count,
                             char** stdlib_kro_files, int max_kro_files);

static int KrtLinkKroExecutable(KrtConfig* config, KrtPlatform* platform,
                                const char* kro_file, const char* exe_output) {
    if (!KrtPlatformPathExists(platform, kro_file)) {
        KrtError("KRO 文件不存在: %s", kro_file);
        return 0;
    }

    char exe_dir[KRT_MAX_PATH];
    KrtGetExecutableDir(exe_dir, sizeof(exe_dir));

    char stdlib_dir[KRT_MAX_PATH];
    char exe_dir_truncated[1010];
    strncpy(exe_dir_truncated, exe_dir, sizeof(exe_dir_truncated) - 1);
    exe_dir_truncated[sizeof(exe_dir_truncated) - 1] = '\0';
    snprintf(stdlib_dir, sizeof(stdlib_dir), "%s" KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "stdlib", exe_dir_truncated);

    char** stdlib_krt_files = NULL;
    char** stdlib_kro_files = NULL;
    int stdlib_count = 0;
    int stdlib_kro_count = 0;
    struct stat stdlib_stat;

    if (stat(stdlib_dir, &stdlib_stat) == 0 && (stdlib_stat.st_mode & S_IFDIR)) {
        stdlib_krt_files = (char**)malloc(256 * sizeof(char*));
        stdlib_kro_files = (char**)malloc(256 * sizeof(char*));

        if (stdlib_krt_files && stdlib_kro_files) {
            KrtScanStdlibDir(stdlib_dir, stdlib_krt_files, 256, &stdlib_count);

            if (stdlib_count > 0) {
                stdlib_kro_count = KrtCompileStdlib(config, platform,
                                                     stdlib_krt_files, stdlib_count,
                                                     stdlib_kro_files, 256);
            }
        }
    }

    int import_kro_count = 0;
    int import_kro_capacity = config->imported_file_count > 0
        ? config->imported_file_count
        : 8;
    char** import_kro_files = NULL;
    int imports_ok = 1;

    if (config->imported_file_count > 0)
    {
        import_kro_files = (char**)malloc(import_kro_capacity * sizeof(char*));
        if (import_kro_files)
        {
            for (int i = 0; i < config->imported_file_count; i++)
            {
                if (!config->imported_files[i]) continue;

                const char* src_path = config->imported_files[i];
                char kro_path[KRT_MAX_PATH];
                KrtGetKroFilename(src_path, kro_path, sizeof(kro_path));


                KrtCompilePipeline* import_pipeline = KrtCompilePipelineCreate(config, platform);
                if (import_pipeline)
                {
                    int import_result = KrtCompilePipelineExecute(import_pipeline, src_path, kro_path);
                    if (import_result && import_pipeline->compiler)
                    {
                        if (import_kro_count >= import_kro_capacity) {
                            import_kro_capacity *= 2;
                            char** expanded = (char**)realloc(import_kro_files,
                                import_kro_capacity * sizeof(char*));
                            if (!expanded) {
                                imports_ok = 0;
                                KrtCompilePipelineDestroy(import_pipeline);
                                break;
                            }
                            import_kro_files = expanded;
                        }
                        import_kro_files[import_kro_count++] = strdup(kro_path);

                        int nested_count = 0;
                        char** nested_files = KrtCompilePipelineGetImportedFiles(import_pipeline,
                                                                                  &nested_count);
                        for (int j = 0; j < nested_count; j++) {
                            KrtConfigAddImportedFile(config, nested_files[j]);
                        }
                    }
                    else
                    {
                        imports_ok = 0;
                    }
                    KrtCompilePipelineDestroy(import_pipeline);
                }
                else
                {
                    imports_ok = 0;
                }
            }
        }
        else
        {
            imports_ok = 0;
        }
    }

    int total_obj_count = 1 + stdlib_kro_count + import_kro_count;
    const char** obj_files = (const char**)malloc(total_obj_count * sizeof(const char*));
    int result = 0;
    if (!obj_files) {
        KrtError("Failed to allocate memory for object files");
    } else {
        int obj_count = 0;
        obj_files[obj_count++] = kro_file;

        for (int i = 0; i < stdlib_kro_count; i++) {
            if (stdlib_kro_files[i] && KrtPlatformPathExists(platform, stdlib_kro_files[i])) {
                obj_files[obj_count++] = stdlib_kro_files[i];
            }
        }

        for (int i = 0; i < import_kro_count; i++) {
            if (import_kro_files[i] && KrtPlatformPathExists(platform, import_kro_files[i])) {
                obj_files[obj_count++] = import_kro_files[i];
            }
        }

        if (!imports_ok) {
            KrtError("依赖编译失败");
            KrtTaskReport("link", kro_file, KRT_TASK_RESULT_FAILED, 0.0, KrtGetGlobalTaskStats());
            result = 0;
        } else if (KrtArkLinkLinkObjects(obj_files, obj_count, exe_output, config) == 0) {
            KrtTaskReport("link", kro_file, KRT_TASK_RESULT_EXECUTED, 0.0, KrtGetGlobalTaskStats());
            result = 1;
        } else {
            KrtError("ArkLink 链接失败");
            KrtTaskReport("link", kro_file, KRT_TASK_RESULT_FAILED, 0.0, KrtGetGlobalTaskStats());
            result = 0;
        }

        free(obj_files);
    }

    if (import_kro_files) {
        for (int i = 0; i < import_kro_count; i++) {
            if (import_kro_files[i]) {
                free(import_kro_files[i]);
            }
        }
        free(import_kro_files);
    }

    if (stdlib_krt_files) {
        for (int i = 0; i < stdlib_count; i++) {
            if (stdlib_krt_files[i]) free(stdlib_krt_files[i]);
        }
        free(stdlib_krt_files);
    }
    if (stdlib_kro_files) {
        for (int i = 0; i < stdlib_kro_count; i++) {
            if (stdlib_kro_files[i]) {
                free(stdlib_kro_files[i]);
            }
        }
        free(stdlib_kro_files);
    }

    return result;
}

static int KrtCompileSingleFile(const char* input_file, const char* output_file, KrtCommandTargetType target_type, int show_ir, int keep_temp) {
    if (!input_file) {
        KrtError("Input file is empty");
        return 1;
    }
    
    char default_output[KRT_MAX_PATH];
    if (!output_file || output_file[0] == '\0') {
        if (target_type == KRT_TARGET_CMD_KRO) {
            strcpy(default_output, "output.exe");
            output_file = default_output;
        } else {
            KrtError("Output file is empty");
            return 1;
        }
    }
    KrtConfig* config = KrtConfigCreate();
    if (!config) {
        KrtError("Failed to create config manager");
        return 1;
    }
    config->keep_temp_files = keep_temp;
    switch (target_type) {
        case KRT_TARGET_CMD_ASM:
            config->target_type = KRT_TARGET_ASM;
            break;
        case KRT_TARGET_CMD_IR:
            config->target_type = KRT_TARGET_IR;
            break;
        case KRT_TARGET_CMD_EXE:
            config->target_type = KRT_TARGET_EXE;
            break;
        case KRT_TARGET_CMD_VM:
            config->target_type = KRT_TARGET_VM;
            break;
        case KRT_TARGET_CMD_KRO:
            config->target_type = KRT_TARGET_KRO;
            break;
        case KRT_TARGET_CMD_BUILD:
            config->target_type = KRT_TARGET_EXE;
            break;
        case KRT_TARGET_CMD_CLEAN:
            break;
        case KRT_TARGET_CMD_CHECK:
            break;
        case KRT_TARGET_CMD_VERSION:
            break;
    }
    
    config->show_ir = show_ir;
    
    KrtPlatform* platform = KrtPlatformGetCurrent();
    if (!platform) {
        KrtError("Failed to create platform abstraction");
        KrtConfigDestroy(config);
        return 1;
    }
    
    KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(config, platform);
    if (!pipeline) {
        KrtError("Failed to create compile pipeline");
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    const char* compile_output = output_file;
#ifndef __linux__
    char temp_eo[KRT_MAX_PATH];
#endif
    char temp_kro[KRT_MAX_PATH];
    if (target_type == KRT_TARGET_CMD_EXE || target_type == KRT_TARGET_CMD_BUILD) {
        char file_base[KRT_MAX_PATH];
        KrtPathGetFilename(input_file, file_base, sizeof(file_base));
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
        char file_base_truncated[1010];
        strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
        file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
#ifndef __linux__
        snprintf(temp_eo, sizeof(temp_eo), "%s.eo", file_base_truncated);
        compile_output = temp_eo;
#else
        snprintf(temp_kro, sizeof(temp_kro), "%s.kro", file_base_truncated);
        compile_output = temp_kro;
#endif
    } else if (target_type == KRT_TARGET_CMD_KRO) {
        
        char file_base[KRT_MAX_PATH];
        KrtPathGetFilename(input_file, file_base, sizeof(file_base));
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
        char file_base_truncated[1010];
        strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
        file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
        snprintf(temp_kro, sizeof(temp_kro), "%s.kro", file_base_truncated);
        compile_output = temp_kro;
    }

    int result = KrtCompilePipelineExecute(pipeline, input_file, compile_output);
    double total_duration = pipeline->total_duration;

    if (result) {
        if (target_type == KRT_TARGET_CMD_EXE || target_type == KRT_TARGET_CMD_BUILD) {
            if (pipeline->compiler) {
                KrtCompilerDestroy(pipeline->compiler);
                pipeline->compiler = NULL;
            }

#ifdef __linux__
            {
                int import_count = 0;
                char** import_files = KrtCompilePipelineGetImportedFiles(pipeline, &import_count);
                for (int i = 0; i < import_count; i++) {
                    KrtConfigAddImportedFile(config, import_files[i]);
                }
            }

            if (KrtLinkKroExecutable(config, platform, compile_output, output_file)) {
                KrtTaskReport("build", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());
            } else {
                KrtTaskReport("build", input_file, KRT_TASK_RESULT_FAILED, total_duration, KrtGetGlobalTaskStats());
                result = 0;
            }
#else
            KrtBuildContext* build_ctx = KrtBuildContextCreate(config, platform);
            if (build_ctx) {
                if (KrtBuildExecute(build_ctx, temp_eo, output_file)) {
                    KrtTaskReport("build", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());
                } else {
                    KrtError("构建失败：%s", KrtBuildGetError(build_ctx));
                    KrtTaskReport("build", input_file, KRT_TASK_RESULT_FAILED, total_duration, KrtGetGlobalTaskStats());
                    result = 0;
                }
                KrtBuildContextDestroy(build_ctx);
            } else {
                KrtError("Failed to create build context");
                result = 0;
            }
#endif
        } else if (target_type == KRT_TARGET_CMD_KRO) {
            if (pipeline->compiler) {
                KrtCompilerDestroy(pipeline->compiler);
                pipeline->compiler = NULL;
            }
            KrtTaskReport("compile", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());

            const char* kro_file = compile_output;

            char default_exe_output[KRT_MAX_PATH];
            if (!output_file || output_file[0] == '\0') {
                strncpy(default_exe_output, kro_file, sizeof(default_exe_output) - 1);
                default_exe_output[sizeof(default_exe_output) - 1] = '\0';

                char* dot = strrchr(default_exe_output, '.');
                if (dot) {
                    strcpy(dot, ".exe");
                } else {
                    strncat(default_exe_output, ".exe", sizeof(default_exe_output) - strlen(default_exe_output) - 1);
                }
                output_file = default_exe_output;
            }
            const char* exe_output = output_file;

            {
                int import_count = 0;
                char** import_files = KrtCompilePipelineGetImportedFiles(pipeline, &import_count);
                for (int i = 0; i < import_count; i++) {
                    KrtConfigAddImportedFile(config, import_files[i]);
                }
            }

            if (!KrtLinkKroExecutable(config, platform, kro_file, exe_output)) {
                result = 0;
            }
        } else if (target_type == KRT_TARGET_CMD_VM) {
            KrtTaskReport("compile", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());
        } else {
            KrtTaskReport("compile", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());
        }
    } else {
        KrtCompilePipelinePrintErrorReport(pipeline);
        KrtTaskReport("compile", input_file, KRT_TASK_RESULT_FAILED, total_duration, KrtGetGlobalTaskStats());
    }
    
    KrtCompilePipelineDestroy(pipeline);
    KrtPlatformDestroy(platform);
    KrtConfigDestroy(config);
    
    return result ? 0 : 1;
}

static void KrtGetExecutableDir(char* buffer, int buffer_size) {
#ifdef _WIN32
    GetModuleFileNameA(NULL, buffer, buffer_size);
    char* last_sep = strrchr(buffer, '\\');
    if (last_sep) {
        *last_sep = '\0';
    }
#else
    ssize_t len = readlink("/proc/self/exe", buffer, buffer_size - 1);
    if (len != -1) {
        buffer[len] = '\0';
        char* last_sep = strrchr(buffer, '/');
        if (last_sep) {
            *last_sep = '\0';
        }
    } else {
        strcpy(buffer, ".");
    }
#endif
}

static int KrtScanStdlibDir(const char* dir_path, char** file_list, int max_files, int* file_count) {
#ifdef _WIN32
    char search_path[KRT_MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", dir_path);
    
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[KRT_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            
            KrtScanStdlibDir(full_path, file_list, max_files, file_count);
        } else {
            
            int len = strlen(find_data.cFileName);
            if (len > 4 && strcmp(find_data.cFileName + len - 4, ".krt") == 0) {
                if (*file_count < max_files) {
                    file_list[*file_count] = strdup(full_path);
                    (*file_count)++;
                }
            }
        }
    } while (FindNextFileA(hFind, &find_data));
    
    FindClose(hFind);
#else
    DIR* dir = opendir(dir_path);
    if (!dir) {
        return 0;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[KRT_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                
                KrtScanStdlibDir(full_path, file_list, max_files, file_count);
            } else {
                
                int len = strlen(entry->d_name);
                if (len > 4 && strcmp(entry->d_name + len - 4, ".krt") == 0) {
                    if (*file_count < max_files) {
                        file_list[*file_count] = strdup(full_path);
                        (*file_count)++;
                    }
                }
            }
        }
    }
    
    closedir(dir);
#endif
    return 1;
}

static void KrtGetKroFilename(const char* krt_file, char* kro_file, int kro_file_size) {
    strncpy(kro_file, krt_file, kro_file_size - 1);
    kro_file[kro_file_size - 1] = '\0';
    
    int len = strlen(kro_file);
    if (len > 4) {
        strcpy(kro_file + len - 4, ".kro");
    }
}

static int KrtNeedsRecompile(const char* krt_file, const char* kro_file) {
    struct stat krt_stat, kro_stat;
    
    if (stat(krt_file, &krt_stat) != 0) {
        return 0;  
    }
    
    if (stat(kro_file, &kro_stat) != 0) {
        return 1;  
    }
    
#ifdef _WIN32
    return kro_stat.st_mtime < krt_stat.st_mtime;
#else
    return kro_stat.st_mtime < krt_stat.st_mtime;
#endif
}

static int KrtCompileStdlib(KrtConfig* config, KrtPlatform* platform, 
                             char** stdlib_krt_files, int stdlib_count,
                             char** stdlib_kro_files, int max_kro_files) {
    int compiled_count = 0;
    
    for (int i = 0; i < stdlib_count && compiled_count < max_kro_files; i++) {
        char kro_file[KRT_MAX_PATH];
        KrtGetKroFilename(stdlib_krt_files[i], kro_file, sizeof(kro_file));
        
        if (!KrtNeedsRecompile(stdlib_krt_files[i], kro_file)) {
            stdlib_kro_files[compiled_count] = strdup(kro_file);
            compiled_count++;
            continue;
        }

        KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(config, platform);
        if (!pipeline) {
            KrtError("Failed to create compile pipeline for %s", stdlib_krt_files[i]);
            continue;
        }

        int compile_result = KrtCompilePipelineExecute(pipeline, stdlib_krt_files[i], kro_file);

        if (compile_result && pipeline->compiler) {
            stdlib_kro_files[compiled_count] = strdup(kro_file);
            compiled_count++;
        } else {
            KrtError("Failed to compile stdlib %s: %s", stdlib_krt_files[i], pipeline->error_message);
        }

        KrtCompilePipelineDestroy(pipeline);
    }
    
    return compiled_count;
}

typedef struct {
    const char* file_path;
    ASTNode* ast;
    Lexer* lexer;
    Parser* parser;
    char* source;
} ParsedFile;

static int KrtCompileMultipleFiles(const char** input_files, int input_count, const char* output_file, 
                                    KrtCommandTargetType target_type, int show_ir, int keep_temp) {
    if (!input_files || input_count <= 0) {
        KrtError("No input files specified");
        return 1;
    }
    
    if (input_count == 1) {
        return KrtCompileSingleFile(input_files[0], output_file, target_type, show_ir, keep_temp);
    }
    
    KrtConfig* config = KrtConfigCreate();
    if (!config) {
        KrtError("Failed to create config");
        return 1;
    }
    config->keep_temp_files = keep_temp;
    config->target_type = KRT_TARGET_KRO;
    config->show_ir = show_ir;
    
    KrtPlatform* platform = KrtPlatformGetCurrent();
    if (!platform) {
        KrtError("Failed to create platform abstraction");
        KrtConfigDestroy(config);
        return 1;
    }
    
    char default_output[KRT_MAX_PATH];
    if (!output_file || output_file[0] == '\0') {
        char file_base[KRT_MAX_PATH];
        KrtPathGetFilename(input_files[0], file_base, sizeof(file_base));
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
        char file_base_truncated[1010];
        strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
        file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
        snprintf(default_output, sizeof(default_output), "%s.kro", file_base_truncated);
        output_file = default_output;
    }
    
    char** kro_files = (char**)calloc(input_count, sizeof(char*));
    if (!kro_files) {
        KrtError("Failed to allocate memory");
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    ParsedFile* parsed_files = (ParsedFile*)calloc(input_count, sizeof(ParsedFile));
    if (!parsed_files) {
        KrtError("Failed to allocate memory");
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    int parsed_count = 0;
    for (int i = 0; i < input_count; i++) {
        FILE* fp = fopen(input_files[i], "r");
        if (!fp) {
            KrtError("Cannot open file: %s", input_files[i]);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        rewind(fp);

        char* source = (char*)malloc(file_size + 1);
        if (!source) {
            fclose(fp);
            continue;
        }

        fread(source, 1, file_size, fp);
        source[file_size] = '\0';
        fclose(fp);

        Lexer* lexer = lexer_create(source);
        if (!lexer) {
            free(source);
            continue;
        }

        Parser* parser = parser_create(lexer);
        if (!parser) {
            free(source);
            lexer_destroy(lexer);
            continue;
        }

        ASTNode* ast = parser_parse(parser);
        if (!ast) {
            free(source);
            lexer_destroy(lexer);
            parser_destroy(parser);
            continue;
        }

        parsed_files[parsed_count].file_path = input_files[i];
        parsed_files[parsed_count].ast = ast;
        parsed_files[parsed_count].lexer = lexer;
        parsed_files[parsed_count].parser = parser;
        parsed_files[parsed_count].source = source;
        parsed_count++;
    }

    if (parsed_count == 0) {
        KrtError("All files failed to parse");
        free(parsed_files);
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    SemanticAnalyzer* shared_analyzer = semantic_analyzer_create();
    if (!shared_analyzer) {
        for (int i = 0; i < parsed_count; i++) {
            free(parsed_files[i].source);
            lexer_destroy(parsed_files[i].lexer);
            parser_destroy(parsed_files[i].parser);
        }
        free(parsed_files);
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    for (int i = 0; i < parsed_count; i++) {
        semantic_analyzer_collect_exports(shared_analyzer, parsed_files[i].ast);
    }

    int success_count = 0;

    for (int i = 0; i < input_count; i++) {
        const char* input_file = input_files[i];

        char file_dir[KRT_MAX_PATH];
        strncpy(file_dir, input_file, sizeof(file_dir) - 1);
        file_dir[sizeof(file_dir) - 1] = '\0';
        char* last_slash = strrchr(file_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
        } else {
            strcpy(file_dir, ".");
        }

        char file_base[KRT_MAX_PATH];
        KrtPathGetFilename(input_file, file_base, sizeof(file_base));
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));

        kro_files[i] = (char*)malloc(KRT_MAX_PATH);
        if (!kro_files[i]) continue;

        char file_base_truncated[1010];
        strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
        file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
        snprintf(kro_files[i], KRT_MAX_PATH, "%s/%s.kro", file_dir, file_base_truncated);

        KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(config, platform);
        if (!pipeline) {
            free(kro_files[i]);
            kro_files[i] = NULL;
            continue;
        }

        semantic_analyzer_set_input_file(shared_analyzer, input_file);
        KrtCompilePipelineSetSemanticAnalyzer(pipeline, shared_analyzer);

        int result = KrtCompilePipelineExecute(pipeline, input_file, kro_files[i]);
        KrtCompilePipelineDestroy(pipeline);

        if (result) {
            success_count++;
        } else {
            KrtError("Failed to compile: %s", input_file);
            free(kro_files[i]);
            kro_files[i] = NULL;
        }
    }

    semantic_analyzer_destroy(shared_analyzer);
    for (int i = 0; i < parsed_count; i++) {
        free(parsed_files[i].source);
        lexer_destroy(parsed_files[i].lexer);
        parser_destroy(parsed_files[i].parser);
    }
    free(parsed_files);
    
    if (success_count == 0) {
        KrtError("All files failed to compile");
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    int total_obj_count = success_count;
    const char** obj_files = (const char**)malloc(total_obj_count * sizeof(const char*));
    if (!obj_files) {
        for (int i = 0; i < input_count; i++) {
            if (kro_files[i]) free(kro_files[i]);
        }
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    int obj_idx = 0;
    for (int i = 0; i < input_count; i++) {
        if (kro_files[i] && KrtPlatformPathExists(platform, kro_files[i])) {
            obj_files[obj_idx++] = kro_files[i];
        }
    }
    
    char exe_output[KRT_MAX_PATH];
    char out_file_base[KRT_MAX_PATH];
    KrtPathGetFilename(output_file, out_file_base, sizeof(out_file_base));
    KrtPathRemoveExtension(out_file_base, out_file_base, sizeof(out_file_base));
    char out_base_truncated[1010];
    strncpy(out_base_truncated, out_file_base, sizeof(out_base_truncated) - 1);
    out_base_truncated[sizeof(out_base_truncated) - 1] = '\0';
    snprintf(exe_output, sizeof(exe_output), "%s.exe", out_base_truncated);
    
    int link_result = 0;
    if (obj_idx > 0) {
        link_result = KrtArkLinkLinkObjects(obj_files, obj_idx, exe_output, config);
    }
    
    for (int i = 0; i < input_count; i++) {
        if (kro_files[i]) {
            if (!keep_temp) {
                KrtDeleteFile(kro_files[i]);
            }
            free(kro_files[i]);
        }
    }
    free(kro_files);
    free(obj_files);
    KrtConfigDestroy(config);
    KrtPlatformDestroy(platform);
    
    if (link_result != 0) {
        KrtError("Linking failed");
        return 1;
    }
    
    fprintf(stderr, "BUILD SUCCESSFUL: %s -> %s (%d files compiled, %d linked)\n", 
            input_files[0], exe_output, success_count, obj_idx);
    return 0;
}

static int KrtBuildProject(const char* project_file, const char* output_path __attribute__((unused)), int keep_temp) {
    
    KrtPlatform* platform = KrtPlatformGetCurrent();
    if (!platform) {
        KrtError("Failed to create platform abstraction");
        return 1;
    }
    
    KrtProjectKrtConfig* project = KrtProjectKrtLoad(project_file);
    if (!project) {
        KrtError("无法加载项目文件: %s", project_file);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    const char* project_root = (project->base_dir && project->base_dir[0] != '\0')
                               ? project->base_dir : ".";

    int max_threads = 8;
    KrtConfig* config = KrtConfigCreate();
    if (config) {
        config->keep_temp_files = keep_temp;
        config->input_file = project_file;
    }
    if (!config) {
        KrtError("Failed to create config");
        KrtProjectKrtDestroy(project);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    if (project->source_count == 0) {
        KrtError("项目 '%s' 中没有 sources 定义",
                 project->project_name ? project->project_name : project_file);
        KrtProjectKrtDestroy(project);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    ParallelCompiler* parallel_compiler = ParallelCompilerCreate(max_threads, config);
    if (!parallel_compiler) {
        KrtError("无法创建并行编译器");
        KrtProjectKrtDestroy(project);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    char obj_dir[KRT_MAX_PATH];
    KrtPlatformPathJoin(platform, obj_dir, sizeof(obj_dir), project_root, "obj");
    KrtEnsureDirectoryRecursive(obj_dir);

    for (int i = 0; i < project->source_count; i++) {
        const char* src = project->sources[i];
        if (!src || src[0] == '\0') continue;

        char source_path[KRT_MAX_PATH];
        KrtPlatformPathJoin(platform, source_path, sizeof(source_path), project_root, src);
        
        if (!KrtPlatformPathExists(platform, source_path)) {
            KrtError("源文件不存在: %s", source_path);
            ParallelCompilerDestroy(parallel_compiler);
            KrtProjectKrtDestroy(project);
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }
        
        char file_name[KRT_MAX_PATH];
        KrtPathGetFilename(source_path, file_name, sizeof(file_name));
        char file_name_no_ext[KRT_MAX_PATH];
        KrtPathRemoveExtension(file_name, file_name_no_ext, sizeof(file_name_no_ext));
        
        char intermediate_asm[KRT_MAX_PATH];
        char intermediate_obj[KRT_MAX_PATH];
        
        char obj_dir_truncated[500];
        strncpy(obj_dir_truncated, obj_dir, sizeof(obj_dir_truncated) - 1);
        obj_dir_truncated[sizeof(obj_dir_truncated) - 1] = '\0';
        char file_name_truncated[500];
        strncpy(file_name_truncated, file_name_no_ext, sizeof(file_name_truncated) - 1);
        file_name_truncated[sizeof(file_name_truncated) - 1] = '\0';
        snprintf(intermediate_asm, sizeof(intermediate_asm), "%s%c%s.asm",
                 obj_dir_truncated, KrtPlatformGetSeparator(platform), file_name_truncated);
        snprintf(intermediate_obj, sizeof(intermediate_obj), "%s%c%s.obj",
                 obj_dir_truncated, KrtPlatformGetSeparator(platform), file_name_truncated);
        
        if (ParallelCompilerAddFile(parallel_compiler, source_path, intermediate_asm, intermediate_obj, 
                                     KRT_TARGET_ASM, 0) != 0) {
            KrtError("添加编译任务失败: %s", source_path);
            ParallelCompilerDestroy(parallel_compiler);
            KrtProjectKrtDestroy(project);
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }
    }
    
    ParallelCompilerCollectGenericTypes(parallel_compiler);

    int compile_result = ParallelCompilerExecute(parallel_compiler);
    
    int total_files = 0, succeeded_files = 0, failed_files = 0;
    ParallelCompilerGetStats(parallel_compiler, &total_files, &succeeded_files, &failed_files);
    
    if (compile_result != 0 || failed_files > 0) {
        KrtError("并行编译失败：%d 个文件编译失败", failed_files);
        ParallelCompilerDestroy(parallel_compiler);
        KrtProjectKrtDestroy(project);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    
    const char* out_name = (project->output_name && project->output_name[0] != '\0')
                           ? project->output_name : project->project_name;

    if (total_files > 0) {
        char bin_dir[KRT_MAX_PATH];
        char platform_name[32];
        
        strncpy(platform_name, platform->name, sizeof(platform_name));
        for (int i = 0; platform_name[i]; i++) {
            platform_name[i] = tolower(platform_name[i]);
        }
        
        char bin_subdir[KRT_MAX_PATH];
        snprintf(bin_subdir, sizeof(bin_subdir), "bin%c%s", 
                KrtPlatformGetSeparator(platform), platform_name);
        
        KrtPlatformPathJoin(platform, bin_dir, sizeof(bin_dir), project_root, bin_subdir);
        
        KrtEnsureDirectoryRecursive(bin_dir);
        
        char project_output_path[KRT_MAX_PATH];
        char project_filename[KRT_MAX_PATH];
        if (platform->type == KRT_CONFIG_PLATFORM_WINDOWS) {
            char truncated[1010];
            strncpy(truncated, out_name ? out_name : "output", sizeof(truncated) - 1);
            truncated[sizeof(truncated) - 1] = '\0';
            snprintf(project_filename, sizeof(project_filename), "%s.exe", truncated);
        } else {
            snprintf(project_filename, sizeof(project_filename), "%s",
                     out_name ? out_name : "output");
        }
        
        KrtPlatformPathJoin(platform, project_output_path, sizeof(project_output_path), bin_dir, project_filename);

        if (ParallelCompilerLinkResults(parallel_compiler, project_output_path) != 0) {
            KrtError("链接失败");
            ParallelCompilerDestroy(parallel_compiler);
            KrtProjectKrtDestroy(project);
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }
    }
    
    KrtPlatformDestroy(platform);
    ParallelCompilerDestroy(parallel_compiler);
    KrtProjectKrtDestroy(project);
    KrtConfigDestroy(config);
    
    return 0;
}

static int KrtRunCompiler(const char** input_files, int input_count, const char* output_file, KrtCommandTargetType target_type, int show_ir, int keep_temp) {
    if (!input_files || input_count <= 0) {
        KrtError("未指定输入文件");
        return 1;
    }
    
    int is_project = 0;
    const char* slash = strrchr(input_files[0], '/');
    if (!slash) slash = strrchr(input_files[0], '\\');
    const char* base = slash ? slash + 1 : input_files[0];
    if (strcmp(base, "project.krt") == 0) {
        is_project = 1;
    } else {
        const char* ext = strrchr(input_files[0], '.');
        if (ext && (strcmp(ext, ".esproj") == 0 || strcmp(ext, ".json") == 0)) {
            is_project = 1;
        }
    }
    
    char default_output[KRT_MAX_PATH];
    const char* final_output = output_file ? output_file : default_output;
    
    memset(default_output, 0, sizeof(default_output));
    if (is_project) {
        char project_name[KRT_MAX_PATH];
        KrtPathGetFilename(input_files[0], project_name, sizeof(project_name));
        KrtPathRemoveExtension(project_name, project_name, sizeof(project_name));
#ifdef _WIN32
        char project_name_truncated[1010];
        strncpy(project_name_truncated, project_name, sizeof(project_name_truncated) - 1);
        project_name_truncated[sizeof(project_name_truncated) - 1] = '\0';
        snprintf(default_output, sizeof(default_output), "%s.exe", project_name_truncated);
#else
        snprintf(default_output, sizeof(default_output), "%s", project_name);
#endif
    } else {
        switch (target_type) {
            case KRT_TARGET_CMD_EXE: {
                char file_base[KRT_MAX_PATH];
                KrtPathGetFilename(input_files[0], file_base, sizeof(file_base));
                KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
#ifdef _WIN32
                char file_base_truncated[1010];
                strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
                file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
                snprintf(default_output, sizeof(default_output), "%s.exe", file_base_truncated);
#else
                snprintf(default_output, sizeof(default_output), "%s", file_base);
#endif
                break;
            }
            case KRT_TARGET_CMD_IR:
                strncpy(default_output, "output.ir", sizeof(default_output) - 1);
                break;
            case KRT_TARGET_CMD_BUILD: {
                char file_base[KRT_MAX_PATH];
                KrtPathGetFilename(input_files[0], file_base, sizeof(file_base));
                KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
#ifdef _WIN32
                char file_base_truncated[1010];
                strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
                file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
                snprintf(default_output, sizeof(default_output), "%s.exe", file_base_truncated);
#else
                snprintf(default_output, sizeof(default_output), "%s", file_base);
#endif
                break;
            }
            case KRT_TARGET_CMD_VM:
                strncpy(default_output, "output.ebc", sizeof(default_output) - 1);
                break;
            case KRT_TARGET_CMD_KRO:
                strncpy(default_output, "output.exe", sizeof(default_output) - 1);
                break;
            default:
                strncpy(default_output, "output.asm", sizeof(default_output) - 1);
                break;
        }
    }
    
    if (is_project) {
        return KrtBuildProject(input_files[0], final_output, keep_temp);
    } else if (input_count > 1) {
        
        return KrtCompileMultipleFiles(input_files, input_count, final_output, target_type, show_ir, keep_temp);
    } else {
        
        return KrtCompileSingleFile(input_files[0], final_output, target_type, show_ir, keep_temp);
    }
}

int main(int argc, char* argv[]) {
    fflush(stdout);
    KrtConsoleSetColorEnabled(KrtConsoleSupportsColor());
    KrtBuildSummaryReset();
    
    double total_start = KrtGetTime();
    
    KrtCommandLineOptions options = KrtParseCommandLine(argc, argv);
    
    if (options.show_help) {
        KrtPrintUsage(argv[0]);
        KrtOutputCacheCleanup();
        return 0;
    }
    
    if (options.create_project) {
        const char* project_name = options.input_file ? options.input_file
                                                      : project_name_from_type(options.project_type);
        KrtCreateProject(project_name, options.project_type);
        KrtBuildSummarySetDuration(KrtGetTime() - total_start);
        KrtPrintBuildSummary();
        KrtOutputCacheCleanup();
        return 0;
    }
    
    if (options.target_type == KRT_TARGET_CMD_VERSION) {
        const char *blue  = KrtColor(KRT_COL_BLUE);
        const char *gray  = KrtColor(KRT_COL_GRAY);
        const char *reset = KrtColor(KRT_COL_RESET);

        KrtPrintf("%sKairote Lang Compiler Version%s: %s %s (%s %s)\n",
                  blue, reset,
                  KRT_COMPILER_VERSION,
                  gray, __DATE__, __TIME__);
        KrtOutputCacheCleanup();
        return 0;
    }

    int result = KrtRunCompiler(options.input_files, options.input_file_count, options.output_file, options.target_type, options.show_ir, options.keep_temp_files);

    KrtBuildSummarySetDuration(KrtGetTime() - total_start);
    if (result != 0) {
        KrtBuildSummarySetFailed(1);
    }
    KrtPrintBuildSummary();
    
    KrtOutputCacheCleanup();
    return result;
}
