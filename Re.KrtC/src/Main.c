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
#include <limits.h>
#include <stdint.h>
#include <time.h>
#include <stdarg.h>
#include "Core/Core.h"
#include "Compiler/Driver/Compiler.h"
#include "Compiler/Compiler.h"
#include "Compiler/Driver/ConfigManager.h"
#include "Compiler/Platform/PlatformAbstraction.h"
#include "Compiler/Build/BuildSystem.h"
#include "Compiler/Pipeline/CompilerPipeline.h"
#include "Compiler/Driver/TaskManager.h"
#include "Compiler/Driver/ConsoleUtils.h"
#include "Version.h"
#include "Compiler/Frontend/Parser/Parser.h"
#include "Compiler/Frontend/Lexer/Tokenizer.h"
#include "Compiler/Driver/Project.h"
#include "Compiler/Driver/ProjectKrt.h"
#include "Compiler/Driver/ParallelCompiler.h"
#include "Compiler/Frontend/Semantic/SemanticAnalyzer.h"
#include "Compiler/Driver/Preprocessor.h"
#include "Compiler/Frontend/Semantic/Generics.h"
#include "Compiler/Driver/ArkLinkIntegration.h"
#include "Core/Utils/OutputCache.h"

static const char* project_name_from_type(const char* type) {
    if (!type) {
        return "console";
    }
    if (strcmp(type, "console") == 0) {
        return "console";
    }
    if (strcmp(type, "lib") == 0) {
        return "library";
    }
    if (strcmp(type, "dll") == 0) {
        return "library";
    }
    if (strcmp(type, "library") == 0) {
        return "library";
    }
    if (strcmp(type, "web") == 0) {
        return "web";
    }
    return "console";
}

static int create_project(const char* name, const char* type) {
    if (type && strcmp(type, "console") != 0 && strcmp(type, "lib") != 0 && strcmp(type, "library") != 0 &&
        strcmp(type, "dll") != 0 && strcmp(type, "web") != 0 && strcmp(type, "system") != 0) {
        KrtError("未知项目类型: %s", type ? type : "(null)");
        return 0;
    }

    KrtProjectType ptype = KRT_PROJ_TYPE_CONSOLE;
    if (type && (strcmp(type, "lib") == 0 || strcmp(type, "library") == 0 || strcmp(type, "dll") == 0)) {
        ptype = KRT_PROJ_TYPE_LIBRARY;
    } else if (type && strcmp(type, "web") == 0) {
        ptype = KRT_PROJ_TYPE_WEB;
    } else if (type && strcmp(type, "system") == 0) {
        ptype = KRT_PROJ_TYPE_SYSTEM;
    }
    KrtProject* project = KrtProjCreate(name, ptype);
    if (!project) {
        KrtError("项目创建失败!");
        return 0;
    }
    int created = KrtProjCreateTemplate(project, name);
    KrtProjDestroy(project);
    if (!created) {
        KrtError("项目创建失败!");
    }
    return created;
}

typedef enum {
    KRT_TARGET_CMD_ASM = 0,
    KRT_TARGET_CMD_IR = 3,
    KRT_TARGET_CMD_EXE = 4,
    KRT_TARGET_CMD_BUILD = 6,
    KRT_TARGET_CMD_CLEAN = 7,
    KRT_TARGET_CMD_CHECK = 8,
    KRT_TARGET_CMD_VERSION = 9,
    KRT_TARGET_CMD_KRO = 10,
    KRT_TARGET_CMD_ARM64 = 11
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
    int optimization_level;
    int max_threads;
} KrtCommandLineOptions;

static void print_usage(const char* program_name) {
    const char* blue = KrtColor(KRT_COL_BLUE);
    const char* gray = KrtColor(KRT_COL_GRAY);
    const char* reset = KrtColor(KRT_COL_RESET);

    KrtPrintf("%s||Kairote Lang 使用说明%s\n", blue, reset);
    KrtPrintf("用法: %s <命令> [参数]\n", program_name ? program_name : "krtc");
    KrtPrintf("%sbuild%s:%s构建%s  clean%s:%s清理\n", blue, gray, blue, blue, gray, blue);
    KrtPrintf("%scheck%s:%s检查%s  help%s:%s帮助信息\n", blue, gray, blue, blue, gray, blue);
    KrtPrintf("%s--keep-temp%s:%s保留临时文件 (.kro)\n", blue, gray, blue);
    KrtPrintf("%s--show-ir%s:%s打印生成的 IR\n", blue, gray, blue);
    KrtPrintf("%s--threads%s:%s项目编译线程数，默认 8\n", blue, gray, blue);
    KrtPrintf("-O0 / -O1 / -O2 / -O3: 优化级别，默认 -O2\n");
    KrtPrintf("\n%s=========== %s其他 %s===========\n", gray, gray, gray);
    KrtPrintf("%starget%s:%s输出类型 %s<%sir%s/%sasm%s/%sexe%s/%skro%s/%sarm64%s>\n", blue, gray, blue, gray, blue, gray,
              blue, gray, blue, gray, blue, gray, blue, gray);
    KrtPrintf("%snew%s:%s创建项目 %s<%s类型%s> <%s项目名%s>\n", blue, gray, blue, gray, blue, gray, blue, gray);
    KrtPrintf("%sversion%s:%s %s\n", blue, gray, blue, KRT_COMPILER_VERSION);
    KrtPrintf("%s", reset);
}

static KrtCommandLineOptions parse_command_line(int argc, char* argv[]) {
    KrtCommandLineOptions options;
    memset(&options, 0, sizeof(options));
    options.target_type = KRT_TARGET_CMD_EXE;
    options.optimization_level = 2;
    options.max_threads = 8;

    if (argc < 2) {
        options.show_help = 1;
        return options;
    }

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (arg[0] == '-' && arg[1] == 'O') {
            if (strlen(arg) != 3 || arg[2] < '0' || arg[2] > '3') {
                KrtError("优化级别必须为 -O0、-O1、-O2 或 -O3");
                exit(1);
            }
            options.optimization_level = arg[2] - '0';
        } else if (strcmp(arg, "help") == 0) {
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
            const char* target_type = argv[++i];
            if (strcmp(target_type, "asm") == 0) {
                options.target_type = KRT_TARGET_CMD_ASM;
            } else if (strcmp(target_type, "exe") == 0) {
                options.target_type = KRT_TARGET_CMD_EXE;
            } else if (strcmp(target_type, "ir") == 0) {
                options.target_type = KRT_TARGET_CMD_IR;
            } else if (strcmp(target_type, "kro") == 0) {
                options.target_type = KRT_TARGET_CMD_KRO;
            } else if (strcmp(target_type, "arm64") == 0) {
                options.target_type = KRT_TARGET_CMD_ARM64;
            } else {
                KrtError("未知的目标类型 '%s'", target_type);
                exit(1);
            }
        } else if (strcmp(arg, "version") == 0) {
            options.target_type = KRT_TARGET_CMD_VERSION;
        } else if (strcmp(arg, "output") == 0 || strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                KrtError("缺少参数: output <输出文件>");
                exit(1);
            }
            options.output_file = argv[++i];
        } else if (strcmp(arg, "--show-ir") == 0 || strcmp(arg, "--ir") == 0) {
            options.show_ir = 1;
        } else if (strcmp(arg, "--threads") == 0 || strcmp(arg, "-j") == 0) {
            if (i + 1 >= argc) {
                KrtError("缺少参数: threads <数量>");
                exit(1);
            }
            int threads = atoi(argv[++i]);
            if (threads <= 0) {
                KrtError("线程数必须为正整数");
                exit(1);
            }
            options.max_threads = threads;
        } else if (options.target_type != KRT_TARGET_CMD_CLEAN && options.target_type != KRT_TARGET_CMD_CHECK &&
                   options.target_type != KRT_TARGET_CMD_VERSION) {

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

/**
 * @brief Derive the .kro path for a .krt source path.
 * @param kro_file_size Size of the kro_file buffer; must be at least KRT_MAX_PATH.
 * @note The output is silently truncated if the buffer is too small; callers
 *       must pass a buffer of KRT_MAX_PATH bytes.
 */
static void get_kro_filename(const char* krt_file, char* kro_file, int kro_file_size);
static int link_kro_executable(KrtConfig* config, KrtPlatform* platform, const char* kro_file, const char* exe_output) {
    if (!KrtPlatformPathExists(platform, kro_file)) {
        KrtError("KRO 文件不存在: %s", kro_file);
        return 0;
    }

    /* Automatic stdlib bodies already belong to this optimized IR module. */
    int import_kro_count = 0;
    int import_kro_capacity = config->imported_file_count > 0 ? config->imported_file_count : 8;
    char** import_kro_files = NULL;
    int imports_ok = 1;

    if (config->imported_file_count > 0) {
        import_kro_files = (char**)malloc(import_kro_capacity * sizeof(char*));
        if (import_kro_files) {
            for (int i = 0; i < config->imported_file_count; i++) {
                if (!config->imported_files[i]) {
                    continue;
                }

                const char* src_path = config->imported_files[i];
                char kro_path[KRT_MAX_PATH];
                get_kro_filename(src_path, kro_path, sizeof(kro_path));

                KrtCompilePipeline* import_pipeline = KrtCompilePipelineCreate(config, platform);
                if (import_pipeline) {
                    int import_result = KrtCompilePipelineExecute(import_pipeline, src_path, kro_path);
                    if (import_result && import_pipeline->compiler) {
                        if (import_pipeline->compiler->has_object_code) {
                            if (import_kro_count >= import_kro_capacity) {
                                if (import_kro_capacity > INT_MAX / 2) {
                                    imports_ok = 0;
                                    KrtError("导入文件数量超出上限");
                                    KrtCompilePipelineDestroy(import_pipeline);
                                    break;
                                }
                                import_kro_capacity *= 2;
                                char** expanded =
                                    (char**)realloc(import_kro_files, import_kro_capacity * sizeof(char*));
                                if (!expanded) {
                                    imports_ok = 0;
                                    KrtCompilePipelineDestroy(import_pipeline);
                                    break;
                                }
                                import_kro_files = expanded;
                            }
                            char* imported_kro = strdup(kro_path);
                            if (!imported_kro) {
                                imports_ok = 0;
                                KrtCompilePipelineDestroy(import_pipeline);
                                break;
                            }
                            import_kro_files[import_kro_count++] = imported_kro;
                        }

                        int nested_count = 0;
                        const char* const* nested_files = KrtCompilePipelineGetImportedFiles(import_pipeline, &nested_count);
                        for (int j = 0; j < nested_count; j++) {
                            KrtConfigAddImportedFile(config, nested_files[j]);
                        }
                    } else {
                        KrtCompilePipelinePrintErrorReport(import_pipeline);
                        imports_ok = 0;
                    }
                    KrtCompilePipelineDestroy(import_pipeline);
                } else {
                    imports_ok = 0;
                }
            }
        } else {
            imports_ok = 0;
        }
    }

    int total_obj_count = 1 + import_kro_count;
    const char** obj_files = (const char**)malloc(total_obj_count * sizeof(const char*));
    int result = 0;
    if (!obj_files) {
        KrtError("Failed to allocate memory for object files");
    } else {
        int obj_count = 0;
        obj_files[obj_count++] = kro_file;

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

    return result;
}

static int compile_single_file(const char* input_file, const char* output_file, KrtCommandTargetType target_type,
                               int show_ir, int keep_temp, int optimization_level) {
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
    config->optimization_level = optimization_level;
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
    case KRT_TARGET_CMD_KRO:
        config->target_type = KRT_TARGET_KRO;
        break;
    case KRT_TARGET_CMD_ARM64:
        config->target_type = KRT_TARGET_ARM64;
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
    char temp_kro[KRT_MAX_PATH];
    if (target_type == KRT_TARGET_CMD_EXE || target_type == KRT_TARGET_CMD_BUILD) {
        char file_base[KRT_MAX_PATH];
        if (KrtPathGetFilename(input_file, file_base, sizeof(file_base)) != 0) {
            KrtCompilePipelineDestroy(pipeline);
            KrtPlatformDestroy(platform);
            KrtConfigDestroy(config);
            return 1;
        }
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
        char file_base_truncated[KRT_MAX_PATH];
        strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
        file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
        snprintf(temp_kro, sizeof(temp_kro), "%s.kro", file_base_truncated);
        compile_output = temp_kro;
    } else if (target_type == KRT_TARGET_CMD_KRO) {

        char file_base[KRT_MAX_PATH];
        if (KrtPathGetFilename(input_file, file_base, sizeof(file_base)) != 0) {
            KrtCompilePipelineDestroy(pipeline);
            KrtPlatformDestroy(platform);
            KrtConfigDestroy(config);
            return 1;
        }
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
        char file_base_truncated[KRT_MAX_PATH];
        strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
        file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
        snprintf(temp_kro, sizeof(temp_kro), "%s.kro", file_base_truncated);
        compile_output = temp_kro;
    } else if (target_type == KRT_TARGET_CMD_ARM64) {
        char file_base[KRT_MAX_PATH];
        if (KrtPathGetFilename(input_file, file_base, sizeof(file_base)) != 0) {
            KrtCompilePipelineDestroy(pipeline);
            KrtPlatformDestroy(platform);
            KrtConfigDestroy(config);
            return 1;
        }
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
        char file_base_truncated[KRT_MAX_PATH];
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
                const char* const* import_files = KrtCompilePipelineGetImportedFiles(pipeline, &import_count);
                for (int i = 0; i < import_count; i++) {
                    KrtConfigAddImportedFile(config, import_files[i]);
                }
            }

            if (link_kro_executable(config, platform, compile_output, output_file)) {
                KrtTaskReport("build", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());
            } else {
                KrtTaskReport("build", input_file, KRT_TASK_RESULT_FAILED, total_duration, KrtGetGlobalTaskStats());
                result = 0;
            }
#else
            KrtBuildContext* build_ctx = KrtBuildContextCreate(config, platform);
            if (build_ctx) {
                if (KrtBuildExecute(build_ctx, temp_kro, output_file)) {
                    KrtTaskReport("build", input_file, KRT_TASK_RESULT_EXECUTED, total_duration,
                                  KrtGetGlobalTaskStats());
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
        } else if (target_type == KRT_TARGET_CMD_KRO || target_type == KRT_TARGET_CMD_ARM64) {
            if (pipeline->compiler) {
                KrtCompilerDestroy(pipeline->compiler);
                pipeline->compiler = NULL;
            }
            KrtTaskReport("compile", input_file, KRT_TASK_RESULT_EXECUTED, total_duration, KrtGetGlobalTaskStats());

            const char* kro_file = compile_output;
            const char* exe_output = output_file;

            {
                int import_count = 0;
                const char* const* import_files = KrtCompilePipelineGetImportedFiles(pipeline, &import_count);
                for (int i = 0; i < import_count; i++) {
                    KrtConfigAddImportedFile(config, import_files[i]);
                }
            }

            if (!link_kro_executable(config, platform, kro_file, exe_output)) {
                result = 0;
            }
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

static void get_kro_filename(const char* krt_file, char* kro_file, int kro_file_size) {
    strncpy(kro_file, krt_file, kro_file_size - 1);
    kro_file[kro_file_size - 1] = '\0';

    size_t len = strlen(kro_file);
    if (len >= 4 && strcmp(kro_file + len - 4, ".krt") == 0) {
        memcpy(kro_file + len - 4, ".kro", 4);
    } else if (len + 4 < (size_t)kro_file_size) {
        snprintf(kro_file + len, (size_t)kro_file_size - len, ".kro");
    }
}

typedef struct {
    const char* file_path;
    ASTNode* ast;
    Lexer* lexer;
    Parser* parser;
    char* source;
} ParsedFile;

static int compile_multiple_files(const char** input_files, int input_count, const char* output_file,
                                  KrtCommandTargetType target_type, int show_ir, int keep_temp,
                                  int optimization_level) {
    if (!input_files || input_count <= 0) {
        KrtError("No input files specified");
        return 1;
    }

    if (input_count == 1) {
        return compile_single_file(input_files[0], output_file, target_type, show_ir, keep_temp, optimization_level);
    }

    /* This path links one KRO module per input.  Do not silently turn an
     * unsupported request such as `target ir` into an executable build. */
    if (target_type != KRT_TARGET_CMD_EXE && target_type != KRT_TARGET_CMD_BUILD &&
        target_type != KRT_TARGET_CMD_KRO && target_type != KRT_TARGET_CMD_ARM64) {
        KrtError("Multiple source files support only executable, kro or arm64 targets");
        return 1;
    }

    KrtConfig* config = KrtConfigCreate();
    if (!config) {
        KrtError("Failed to create config");
        return 1;
    }
    config->keep_temp_files = keep_temp;
    config->optimization_level = optimization_level;
    config->target_type = (target_type == KRT_TARGET_CMD_ARM64) ? KRT_TARGET_ARM64 : KRT_TARGET_KRO;
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
        if (KrtPathGetFilename(input_files[0], file_base, sizeof(file_base)) != 0) {
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
#ifdef _WIN32
        int output_length = snprintf(default_output, sizeof(default_output), "%s.exe", file_base);
#else
        int output_length = snprintf(default_output, sizeof(default_output), "%s", file_base);
#endif
        if (output_length < 0 || (size_t)output_length >= sizeof(default_output)) {
            KrtError("Default output path is too long");
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }
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

        if (fseek(fp, 0, SEEK_END) != 0) {
            KrtError("Cannot seek in file: %s", input_files[i]);
            fclose(fp);
            continue;
        }
        long file_size = ftell(fp);
        if (file_size < 0 || (uintmax_t)file_size >= (uintmax_t)SIZE_MAX || fseek(fp, 0, SEEK_SET) != 0) {
            KrtError("Cannot determine file size: %s", input_files[i]);
            fclose(fp);
            continue;
        }

        char* source = (char*)malloc((size_t)file_size + 1);
        if (!source) {
            KrtError("Cannot allocate memory for source file: %s", input_files[i]);
            fclose(fp);
            continue;
        }

        size_t bytes_read = fread(source, 1, (size_t)file_size, fp);
        if (bytes_read != (size_t)file_size || ferror(fp)) {
            KrtError("Cannot read complete file: %s", input_files[i]);
            free(source);
            fclose(fp);
            continue;
        }
        source[(size_t)file_size] = '\0';
        fclose(fp);

        Lexer* lexer = lexer_create(source);
        if (!lexer) {
            KrtError("Cannot create lexer for file: %s", input_files[i]);
            free(source);
            continue;
        }

        Parser* parser = parser_create(lexer);
        if (!parser) {
            KrtError("Cannot create parser for file: %s", input_files[i]);
            free(source);
            lexer_destroy(lexer);
            continue;
        }

        ASTNode* ast = parser_parse(parser);
        if (!ast) {
            KrtError("Cannot parse file: %s", input_files[i]);
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

    if (parsed_count != input_count) {
        for (int i = 0; i < parsed_count; i++) {
            free(parsed_files[i].source);
            lexer_destroy(parsed_files[i].lexer);
            parser_destroy(parsed_files[i].parser);
        }
        KrtError("Build preparation failed: %d of %d source files parsed", parsed_count, input_count);
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
        /* Make project-local signatures visible independently of command-line
         * file order.  Entries are registered as forward references and are
         * completed when their owning file is analyzed. */
        semantic_analyzer_collect_exports(shared_analyzer, parsed_files[i].ast);
    }

    int success_count = 0;

    for (int i = 0; i < input_count; i++) {
        const char* input_file = input_files[i];

        char file_dir[KRT_MAX_PATH];
        int directory_length = snprintf(file_dir, sizeof(file_dir), "%s", input_file);
        if (directory_length < 0 || (size_t)directory_length >= sizeof(file_dir)) {
            KrtError("Input path is too long: %s", input_file);
            continue;
        }
        char* last_slash = strrchr(file_dir, '/');
        if (last_slash) {
            *last_slash = '\0';
        } else {
            strcpy(file_dir, ".");
        }

        char file_base[KRT_MAX_PATH];
        if (KrtPathGetFilename(input_file, file_base, sizeof(file_base)) != 0) {
            continue;
        }
        KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));

        kro_files[i] = (char*)malloc(KRT_MAX_PATH);
        if (!kro_files[i]) {
            KrtError("Cannot allocate intermediate output path for: %s", input_file);
            continue;
        }

        int output_length = snprintf(kro_files[i], KRT_MAX_PATH, "%s/%s.kro", file_dir, file_base);
        if (output_length < 0 || (size_t)output_length >= KRT_MAX_PATH) {
            KrtError("Intermediate output path is too long for: %s", input_file);
            free(kro_files[i]);
            kro_files[i] = NULL;
            continue;
        }

        KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(config, platform);
        if (!pipeline) {
            KrtError("Failed to create compiler pipeline for: %s", input_file);
            free(kro_files[i]);
            kro_files[i] = NULL;
            continue;
        }

        semantic_analyzer_set_input_file(shared_analyzer, input_file);
        KrtCompilePipelineSetSemanticAnalyzer(pipeline, shared_analyzer);

        int result = KrtCompilePipelineExecute(pipeline, input_file, kro_files[i]);
        if (!result) {
            KrtCompilePipelinePrintErrorReport(pipeline);
        }
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

    if (success_count != input_count || parsed_count != input_count) {
        KrtError("Build failed: %d of %d files compiled; linking cancelled", success_count, input_count);
        for (int i = 0; i < input_count; i++) {
            free(kro_files[i]);
        }
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    int total_obj_count = success_count;
    if ((size_t)total_obj_count > SIZE_MAX / sizeof(const char*)) {
        KrtError("Too many object files to link");
        for (int i = 0; i < input_count; i++) {
            free(kro_files[i]);
        }
        free(kro_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }
    const char** obj_files = (const char**)malloc((size_t)total_obj_count * sizeof(const char*));
    if (!obj_files) {
        for (int i = 0; i < input_count; i++) {
            if (kro_files[i]) {
                free(kro_files[i]);
            }
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
    if (obj_idx != success_count) {
        KrtError("Build failed: one or more compiled object files are missing");
        for (int i = 0; i < input_count; i++) {
            if (kro_files[i]) {
                free(kro_files[i]);
            }
        }
        free(kro_files);
        free(obj_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    char exe_output[KRT_MAX_PATH];
    /* `-o` is a complete output path, not a basename.  In particular, do
     * not discard its directory or invent a platform extension here. */
    int exe_length = snprintf(exe_output, sizeof(exe_output), "%s", output_file);
    if (exe_length < 0 || (size_t)exe_length >= sizeof(exe_output)) {
        KrtError("Executable output path is too long");
        for (int i = 0; i < input_count; i++) {
            free(kro_files[i]);
        }
        free(kro_files);
        free(obj_files);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    int link_result = obj_idx == success_count ? 0 : -1;
    if (link_result == 0 && obj_idx > 0) {
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
        KrtError("Linking failed (result %d)", link_result);
        return 1;
    }

    fprintf(stderr, "BUILD SUCCESSFUL: %s -> %s (%d files compiled, %d linked)\n", input_files[0], exe_output,
            success_count, obj_idx);
    return 0;
}

static int build_project_independent(const char* project_file, const char* output_path, int keep_temp,
                                     int optimization_level, int max_threads) {

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

    const char* project_root = (project->base_dir && project->base_dir[0] != '\0') ? project->base_dir : ".";

    KrtConfig* config = KrtConfigCreate();
    if (config) {
        config->keep_temp_files = keep_temp;
        config->optimization_level = optimization_level;
        config->input_file = project_file;
        config->target_type = KRT_TARGET_KRO;
        config->max_threads = max_threads > 0 ? max_threads : 8;
    }
    if (!config) {
        KrtError("Failed to create config");
        KrtProjectKrtDestroy(project);
        KrtPlatformDestroy(platform);
        return 1;
    }

    if (project->source_count == 0) {
        KrtError("项目 '%s' 中没有 sources 定义", project->project_name ? project->project_name : project_file);
        KrtProjectKrtDestroy(project);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    ParallelCompiler* parallel_compiler = ParallelCompilerCreate(config->max_threads, config);
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
        if (!src || src[0] == '\0') {
            KrtError("项目源列表第 %d 项为空", i);
            ParallelCompilerDestroy(parallel_compiler);
            KrtProjectKrtDestroy(project);
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }

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
        if (KrtPathGetFilename(source_path, file_name, sizeof(file_name)) != 0) {
            ParallelCompilerDestroy(parallel_compiler);
            KrtProjectKrtDestroy(project);
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }
        char file_name_no_ext[KRT_MAX_PATH];
        KrtPathRemoveExtension(file_name, file_name_no_ext, sizeof(file_name_no_ext));

        char intermediate_obj[KRT_MAX_PATH];

        if (strlen(obj_dir) >= 500 || strlen(file_name_no_ext) >= 500) {
            KrtError("项目对象路径过长: %s", obj_dir);
            ParallelCompilerDestroy(parallel_compiler);
            KrtProjectKrtDestroy(project);
            KrtConfigDestroy(config);
            KrtPlatformDestroy(platform);
            return 1;
        }

        char obj_dir_truncated[500];
        strncpy(obj_dir_truncated, obj_dir, sizeof(obj_dir_truncated) - 1);
        obj_dir_truncated[sizeof(obj_dir_truncated) - 1] = '\0';
        char file_name_truncated[500];
        strncpy(file_name_truncated, file_name_no_ext, sizeof(file_name_truncated) - 1);
        file_name_truncated[sizeof(file_name_truncated) - 1] = '\0';
        snprintf(intermediate_obj, sizeof(intermediate_obj), "%s%c%s.kro", obj_dir_truncated,
                 KrtPlatformGetSeparator(platform), file_name_truncated);

        if (ParallelCompilerAddFile(parallel_compiler, source_path, intermediate_obj, NULL, KRT_TARGET_KRO, 0) != 0) {
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
        KrtError("并行编译失败：result=%d，%d/%d 个文件编译失败", compile_result, failed_files, total_files);
        ParallelCompilerDestroy(parallel_compiler);
        KrtProjectKrtDestroy(project);
        KrtConfigDestroy(config);
        KrtPlatformDestroy(platform);
        return 1;
    }

    const char* out_name =
        (project->output_name && project->output_name[0] != '\0') ? project->output_name : project->project_name;

    if (total_files > 0) {
        char bin_dir[KRT_MAX_PATH];
        char platform_name[32];

        KrtStrlcpy(platform_name, platform->name, sizeof(platform_name));
        for (int i = 0; platform_name[i]; i++) {
            platform_name[i] = tolower((unsigned char)platform_name[i]);
        }

        char bin_subdir[KRT_MAX_PATH];
        snprintf(bin_subdir, sizeof(bin_subdir), "bin%c%s", KrtPlatformGetSeparator(platform), platform_name);

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
            snprintf(project_filename, sizeof(project_filename), "%s", out_name ? out_name : "output");
        }

        if (output_path && output_path[0] != '\0') {
            /* An explicit -o wins over bin/<platform>/<name>. */
            snprintf(project_output_path, sizeof(project_output_path), "%s", output_path);
        } else {
            KrtPlatformPathJoin(platform, project_output_path, sizeof(project_output_path), bin_dir, project_filename);
        }

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


static int compile_project_sources_together(const char** sources, int source_count, const char* object_dir,
                                           const char* output, int keep_temp, int optimization_level) {
#ifdef _WIN32
    long process_id = (long)GetCurrentProcessId();
#else
    long process_id = (long)getpid();
#endif
    char aggregate_path[KRT_MAX_PATH];
    char aggregate_kro[KRT_MAX_PATH];
    if (snprintf(aggregate_path, sizeof(aggregate_path), "%s%ckrt_project_aggregate_%ld.krt", object_dir,
                 KRT_PATH_SEPARATOR, process_id) >= (int)sizeof(aggregate_path)) {
        KrtError("合并项目源码路径过长: %s", object_dir);
        return 1;
    }
    if (snprintf(aggregate_kro, sizeof(aggregate_kro), "krt_project_aggregate_%ld.kro", process_id) >=
        (int)sizeof(aggregate_kro)) {
        KrtError("合并项目产物路径过长");
        return 1;
    }

    FILE* aggregate = fopen(aggregate_path, "wb");
    if (!aggregate) {
        KrtError("无法创建合并项目源码: %s", aggregate_path);
        return 1;
    }

    int write_failed = 0;
    char buffer[64 * 1024];
    for (int i = 0; i < source_count && !write_failed; i++) {
        FILE* input = fopen(sources[i], "rb");
        if (!input) {
            KrtError("无法读取项目源文件: %s", sources[i]);
            write_failed = 1;
            break;
        }
        if (fprintf(aggregate, "\n// project source: %s\n", sources[i]) < 0) {
            write_failed = 1;
        }
        size_t count;
        while (!write_failed && (count = fread(buffer, 1, sizeof(buffer), input)) != 0) {
            if (fwrite(buffer, 1, count, aggregate) != count) {
                write_failed = 1;
            }
        }
        if (ferror(input)) {
            KrtError("读取项目源文件失败: %s", sources[i]);
            write_failed = 1;
        }
        if (fclose(input) != 0) {
            write_failed = 1;
        }
    }
    if (fclose(aggregate) != 0) {
        write_failed = 1;
    }
    if (write_failed) {
        KrtError("合并项目源码失败");
        KrtDeleteFile(aggregate_path);
        return 1;
    }

    int result = compile_single_file(aggregate_path, output, KRT_TARGET_CMD_EXE, 0, keep_temp,
                                    optimization_level);
    KrtDeleteFile(aggregate_path);
    if (!keep_temp) {
        KrtDeleteFile(aggregate_kro);
    }
    return result;
}

static int compare_project_source_paths(const void* left, const void* right) {
    const char* const* a = (const char* const*)left;
    const char* const* b = (const char* const*)right;
    return strcmp(*a, *b);
}

static int build_project(const char* project_file, const char* output_path, int keep_temp, int optimization_level,
                         int max_threads) {
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

    const char* project_root = (project->base_dir && project->base_dir[0] != '\0') ? project->base_dir : ".";

    if (project->source_count == 0) {
        KrtError("项目 '%s' 中没有 sources 定义", project->project_name ? project->project_name : project_file);
        KrtProjectKrtDestroy(project);
        KrtPlatformDestroy(platform);
        return 1;
    }

    if (!project->compile_together) {
        KrtProjectKrtDestroy(project);
        KrtPlatformDestroy(platform);
        return build_project_independent(project_file, output_path, keep_temp, optimization_level, max_threads);
    }

    const char** source_paths = (const char**)calloc((size_t)project->source_count, sizeof(const char*));
    if (!source_paths) {
        KrtError("无法分配项目源文件列表");
        KrtProjectKrtDestroy(project);
        KrtPlatformDestroy(platform);
        return 1;
    }

    for (int i = 0; i < project->source_count; i++) {
        const char* src = project->sources[i];
        if (!src || src[0] == '\0') {
            KrtError("项目源列表第 %d 项为空", i);
            for (int j = 0; j < i; j++) {
                free((void*)source_paths[j]);
            }
            free(source_paths);
            KrtProjectKrtDestroy(project);
            KrtPlatformDestroy(platform);
            return 1;
        }

        char* source_path = (char*)malloc(KRT_MAX_PATH);
        if (!source_path) {
            KrtError("无法分配源文件路径");
            for (int j = 0; j < i; j++) {
                free((void*)source_paths[j]);
            }
            free(source_paths);
            KrtProjectKrtDestroy(project);
            KrtPlatformDestroy(platform);
            return 1;
        }
        KrtPlatformPathJoin(platform, source_path, KRT_MAX_PATH, project_root, src);
        if (!KrtPlatformPathExists(platform, source_path)) {
            KrtError("源文件不存在: %s", source_path);
            free(source_path);
            for (int j = 0; j < i; j++) {
                free((void*)source_paths[j]);
            }
            free(source_paths);
            KrtProjectKrtDestroy(project);
            KrtPlatformDestroy(platform);
            return 1;
        }
        source_paths[i] = source_path;
    }
    qsort(source_paths, (size_t)project->source_count, sizeof(*source_paths), compare_project_source_paths);

    const char* out_name =
        (project->output_name && project->output_name[0] != '\0') ? project->output_name : project->project_name;
    char bin_dir[KRT_MAX_PATH];
    char platform_name[32];
    char bin_subdir[KRT_MAX_PATH];
    char project_output_path[KRT_MAX_PATH];
    char project_filename[KRT_MAX_PATH];
    char obj_dir[KRT_MAX_PATH];
    KrtStrlcpy(platform_name, platform->name, sizeof(platform_name));
    for (int i = 0; platform_name[i]; i++) {
        platform_name[i] = (char)tolower((unsigned char)platform_name[i]);
    }
    snprintf(bin_subdir, sizeof(bin_subdir), "bin%c%s", KrtPlatformGetSeparator(platform), platform_name);
    KrtPlatformPathJoin(platform, bin_dir, sizeof(bin_dir), project_root, bin_subdir);
    KrtEnsureDirectoryRecursive(bin_dir);
    if (platform->type == KRT_CONFIG_PLATFORM_WINDOWS) {
        snprintf(project_filename, sizeof(project_filename), "%s.exe", out_name ? out_name : "output");
    } else {
        KrtStrlcpy(project_filename, out_name ? out_name : "output", sizeof(project_filename));
    }
    KrtPlatformPathJoin(platform, project_output_path, sizeof(project_output_path), bin_dir, project_filename);
    if (output_path && output_path[0] != '\0') {
        KrtStrlcpy(project_output_path, output_path, sizeof(project_output_path));
    }
    KrtPlatformPathJoin(platform, obj_dir, sizeof(obj_dir), project_root, "obj");
    if (KrtEnsureDirectoryRecursive(obj_dir) != 0) {
        KrtError("无法创建项目对象目录: %s", obj_dir);
        for (int i = 0; i < project->source_count; i++) {
            free((void*)source_paths[i]);
        }
        free(source_paths);
        KrtProjectKrtDestroy(project);
        KrtPlatformDestroy(platform);
        return 1;
    }

    int result = compile_project_sources_together(source_paths, project->source_count, obj_dir,
                                                  project_output_path, keep_temp, optimization_level);
    for (int i = 0; i < project->source_count; i++) {
        free((void*)source_paths[i]);
    }
    free(source_paths);

    KrtPlatformDestroy(platform);
    KrtProjectKrtDestroy(project);
    return result;
}

static int run_compiler(const char** input_files, int input_count, const char* output_file,
                        KrtCommandTargetType target_type, int show_ir, int keep_temp, int optimization_level,
                        int max_threads) {
    if (!input_files || input_count <= 0) {
        KrtError("未指定输入文件");
        return 1;
    }

    int is_project = 0;
    for (int i = 0; i < input_count; i++) {
        const char* slash = strrchr(input_files[i], '/');
        if (!slash) {
            slash = strrchr(input_files[i], '\\');
        }
        const char* base = slash ? slash + 1 : input_files[i];
        if (strcmp(base, "project.krt") == 0) {
            is_project = 1;
            break;
        }
        const char* ext = strrchr(input_files[i], '.');
        if (ext && (strcmp(ext, ".esproj") == 0 || strcmp(ext, ".json") == 0)) {
            is_project = 1;
            break;
        }
    }

    char default_output[KRT_MAX_PATH];
    const char* final_output = output_file ? output_file : default_output;

    memset(default_output, 0, sizeof(default_output));
    if (is_project) {
        char project_name[KRT_MAX_PATH];
        if (KrtPathGetFilename(input_files[0], project_name, sizeof(project_name)) != 0) {
            return 1;
        }
        KrtPathRemoveExtension(project_name, project_name, sizeof(project_name));
#ifdef _WIN32
        char project_name_truncated[KRT_MAX_PATH];
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
            if (KrtPathGetFilename(input_files[0], file_base, sizeof(file_base)) != 0) {
                return 1;
            }
            KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
#ifdef _WIN32
            char file_base_truncated[KRT_MAX_PATH];
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
            if (KrtPathGetFilename(input_files[0], file_base, sizeof(file_base)) != 0) {
                return 1;
            }
            KrtPathRemoveExtension(file_base, file_base, sizeof(file_base));
#ifdef _WIN32
            char file_base_truncated[KRT_MAX_PATH];
            strncpy(file_base_truncated, file_base, sizeof(file_base_truncated) - 1);
            file_base_truncated[sizeof(file_base_truncated) - 1] = '\0';
            snprintf(default_output, sizeof(default_output), "%s.exe", file_base_truncated);
#else
            snprintf(default_output, sizeof(default_output), "%s", file_base);
#endif
            break;
        }
        case KRT_TARGET_CMD_KRO:
            strncpy(default_output, "output.exe", sizeof(default_output) - 1);
            break;
        case KRT_TARGET_CMD_ARM64:
            strncpy(default_output, "output.kro", sizeof(default_output) - 1);
            break;
        default:
            strncpy(default_output, "output.asm", sizeof(default_output) - 1);
            break;
        }
    }

    if (is_project) {
        return build_project(input_files[0], final_output, keep_temp, optimization_level, max_threads);
    } else if (input_count > 1) {

        return compile_multiple_files(input_files, input_count, final_output, target_type, show_ir, keep_temp,
                                      optimization_level);
    } else {

        return compile_single_file(input_files[0], final_output, target_type, show_ir, keep_temp, optimization_level);
    }
}

int main(int argc, char* argv[]) {
    fflush(stdout);
    KrtConsoleSetColorEnabled(KrtConsoleSupportsColor());
    KrtBuildSummaryReset();

    double total_start = KrtGetTime();

    KrtCommandLineOptions options = parse_command_line(argc, argv);

    if (options.show_help) {
        print_usage(argv[0]);
        KrtOutputCacheCleanup();
        return 0;
    }

    if (options.create_project) {
        const char* project_name =
            options.input_file ? options.input_file : project_name_from_type(options.project_type);
        int created = create_project(project_name, options.project_type);
        KrtBuildSummarySetDuration(KrtGetTime() - total_start);
        KrtPrintBuildSummary();
        KrtOutputCacheCleanup();
        return created ? 0 : 1;
    }

    if (options.target_type == KRT_TARGET_CMD_VERSION) {
        const char* blue = KrtColor(KRT_COL_BLUE);
        const char* gray = KrtColor(KRT_COL_GRAY);
        const char* reset = KrtColor(KRT_COL_RESET);

        KrtPrintf("%sKairote Lang Compiler Version%s: %s %s (%s %s)\n", blue, reset, KRT_COMPILER_VERSION, gray,
                  __DATE__, __TIME__);
        KrtOutputCacheCleanup();
        return 0;
    }

    int result = run_compiler(options.input_files, options.input_file_count, options.output_file, options.target_type,
                              options.show_ir, options.keep_temp_files, options.optimization_level, options.max_threads);

    KrtBuildSummarySetDuration(KrtGetTime() - total_start);
    if (result != 0) {
        KrtBuildSummarySetFailed(1);
    }
    KrtPrintBuildSummary();

    KrtOutputCacheCleanup();
    return result;
}
