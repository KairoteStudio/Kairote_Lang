#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "CompilerPipeline.h"
#include "../Frontend/Semantic/NameMangling.h"
#include "../Driver/ConsoleUtils.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#define KRT_PATH_SEP '\\'
#define KRT_PATH_SEP_STR "\\"
#else
#define KRT_PATH_SEP '/'
#define KRT_PATH_SEP_STR "/"
#endif
static const char* KrtCompileStageNames[] = {"读取源文件", "预处理",   "词法分析", "语法分析",
                                             "语义分析",   "类型检查", "代码生成", "编译完成"};

static const char* KrtCompileStageNamesEnglish[] = {"ReadSource", "Preprocess", "Lex",     "Parse",
                                                    "Semantic",   "TypeCheck",  "CodeGen", "Complete"};

static void load_standard_macros(Preprocessor* preprocessor) {
    if (!preprocessor) {
        return;
    }

    if (!PreprocessorAddMacro(preprocessor, "println", "Console.WriteLine")) {
        KrtError("Preprocessor: failed to register standard macro 'println'");
    }
    if (!PreprocessorAddMacro(preprocessor, "print", "Console.Write")) {
        KrtError("Preprocessor: failed to register standard macro 'print'");
    }
    if (!PreprocessorAddMacro(preprocessor, "print_int", "Console.WriteInt")) {
        KrtError("Preprocessor: failed to register standard macro 'print_int'");
    }
}

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static int scan_stdlib_directory(const char* dir_path, char** file_list, int max_files, int* file_count) {
#ifdef _WIN32
    char search_path[1024];
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

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir_path, find_data.cFileName);

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_stdlib_directory(full_path, file_list, max_files, file_count);
        } else {
            int len = strlen(find_data.cFileName);
            if (len > 4 && strcmp(find_data.cFileName + len - 4, ".krt") == 0) {
                if (*file_count < max_files) {
                    file_list[*file_count] = KRT_STRDUP(full_path);
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

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_stdlib_directory(full_path, file_list, max_files, file_count);
            } else {
                int len = strlen(entry->d_name);
                if (len > 4 && strcmp(entry->d_name + len - 4, ".krt") == 0) {
                    if (*file_count < max_files) {
                        file_list[*file_count] = KRT_STRDUP(full_path);
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

#include "StdlibCache.inc"

static void record_stage_result(KrtCompilePipeline* pipeline, KrtCompileStage stage, KrtCompileResult result,
                                double duration, const char* file_name) {
    if (!pipeline) {
        return;
    }

    if (stage < 0 || stage >= (int)(sizeof(KrtCompileStageNames) / sizeof(KrtCompileStageNames[0]))) {
        KrtError("internal error: invalid compile stage %d", (int)stage);
        return;
    }

    if (pipeline->stage_count >= (int)(sizeof(pipeline->stage_results) / sizeof(pipeline->stage_results[0]))) {
        KrtError("compile stage result dropped: stage table full");
        return;
    }

    KrtCompileStageResult* stage_result = &pipeline->stage_results[pipeline->stage_count++];
    stage_result->stage = stage;
    stage_result->result = result;
    stage_result->duration = duration;
    stage_result->file_name = file_name;

    pipeline->current_stage = stage;

    if (result == KRT_RESULT_FAILED) {
        KRT_STRNCPY_SAFE(pipeline->failed_stage_name, KrtCompileStageNames[stage]);
    }
}

KrtCompilePipeline* KrtCompilePipelineCreate(KrtConfig* config, KrtPlatform* platform) {
    KrtCompilePipeline* pipeline = (KrtCompilePipeline*)KRT_MALLOC(sizeof(KrtCompilePipeline));
    if (!pipeline) {
        return NULL;
    }

    memset(pipeline, 0, sizeof(KrtCompilePipeline));
    pipeline->config = config;
    pipeline->platform = platform;
    pipeline->success = 1;

    name_mangling_init();

    return pipeline;
}

void KrtCompilePipelineReset(KrtCompilePipeline* pipeline) {
    if (!pipeline) {
        return;
    }

    if (pipeline->compiler) {
        KrtCompilerDestroy(pipeline->compiler);
    }
    if (pipeline->semantic_analyzer) {
        if (pipeline->owns_semantic_analyzer) {
            semantic_analyzer_destroy(pipeline->semantic_analyzer);
        }
        pipeline->semantic_analyzer = NULL;
    }
    if (pipeline->semantic_result) {
        if (pipeline->semantic_result->error_messages) {
            KRT_FREE(pipeline->semantic_result->error_messages);
        }
        KRT_FREE(pipeline->semantic_result);
    }
    if (pipeline->ast) {
        ast_destroy_node(pipeline->ast);
    }
    if (pipeline->parser) {
        parser_destroy(pipeline->parser);
    }
    if (pipeline->lexer) {
        lexer_destroy(pipeline->lexer);
    }
    if (pipeline->processed_source) {
        KRT_FREE(pipeline->processed_source);
    }
    if (pipeline->source_code) {
        KRT_FREE(pipeline->source_code);
    }

    if (pipeline->imported_files) {
        for (int i = 0; i < pipeline->imported_file_count; i++) {
            if (pipeline->imported_files[i]) {
                KRT_FREE(pipeline->imported_files[i]);
            }
        }
        KRT_FREE(pipeline->imported_files);
    }

    KrtConfig* config = pipeline->config;
    KrtPlatform* platform = pipeline->platform;
    memset(pipeline, 0, sizeof(*pipeline));
    pipeline->config = config;
    pipeline->platform = platform;
    pipeline->success = 1;
}

void KrtCompilePipelineDestroy(KrtCompilePipeline* pipeline) {
    if (!pipeline) {
        return;
    }
    KrtCompilePipelineReset(pipeline);
    name_mangling_cleanup();
    KRT_FREE(pipeline);
}

int KrtCompilePipelineReadSource(KrtCompilePipeline* pipeline) {
    if (!pipeline || !pipeline->input_file) {
        return 0;
    }

    clock_t start = clock();

    FILE* fp = fopen(pipeline->input_file, "r");
    if (!fp) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "无法打开源文件: %s", pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_READ_SOURCE, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "无法读取源文件大小: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_READ_SOURCE, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "无法获取源文件大小: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_READ_SOURCE, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }

    rewind(fp);
    pipeline->source_code = (char*)KRT_MALLOC((size_t)file_size + 1);
    if (!pipeline->source_code) {
        fclose(fp);
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "内存分配失败，无法读取源文件: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_READ_SOURCE, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }

    size_t read_bytes = fread(pipeline->source_code, 1, (size_t)file_size, fp);
    fclose(fp);
    if (read_bytes != (size_t)file_size) {
        KRT_FREE(pipeline->source_code);
        pipeline->source_code = NULL;
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "无法完整读取源文件: %s (%zu/%ld 字节)",
                 pipeline->input_file, read_bytes, file_size);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_READ_SOURCE, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }
    pipeline->source_code[read_bytes] = '\0';

    record_stage_result(pipeline, KRT_STAGE_READ_SOURCE, KRT_RESULT_SUCCESS, (double)(clock() - start) / CLOCKS_PER_SEC,
                        pipeline->input_file);
    return 1;
}

int KrtCompilePipelinePreprocess(KrtCompilePipeline* pipeline) {
    if (!pipeline || !pipeline->source_code) {
        return 0;
    }

    clock_t start = clock();

    Preprocessor* preprocessor = PreprocessorCreate();
    if (!preprocessor) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "预处理器创建失败: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PREPROCESS, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }

    load_standard_macros(preprocessor);
    pipeline->processed_source = PreprocessorProcess(preprocessor, pipeline->source_code);
    PreprocessorDestroy(preprocessor);

    if (!pipeline->processed_source) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "预处理失败: %s", pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PREPROCESS, KRT_RESULT_FAILED,
                            (double)(clock() - start) / CLOCKS_PER_SEC, pipeline->input_file);
        return 0;
    }

    record_stage_result(pipeline, KRT_STAGE_PREPROCESS, KRT_RESULT_SUCCESS, (double)(clock() - start) / CLOCKS_PER_SEC,
                        pipeline->input_file);
    return 1;
}

static void apply_lexer_error(KrtCompilePipeline* pipeline) {
    if (pipeline->lexer->error_message[0] != '\0') {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "%s near line %d",
                 pipeline->lexer->error_message, pipeline->lexer->error_line);
    } else {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "lexical error near line %d",
                 pipeline->lexer->error_line);
    }
}

int KrtCompilePipelineLex(KrtCompilePipeline* pipeline) {
    if (!pipeline || !pipeline->processed_source) {
        return 0;
    }

    clock_t start = clock();

    pipeline->lexer = lexer_create(pipeline->processed_source);
    if (!pipeline->lexer) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "词法分析器创建失败: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_LEX, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    if (pipeline->lexer->error_count > 0) {
        apply_lexer_error(pipeline);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_LEX, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    record_stage_result(pipeline, KRT_STAGE_LEX, KRT_RESULT_SUCCESS, (double)(clock() - start) / CLOCKS_PER_SEC,
                        pipeline->input_file);
    return 1;
}

int KrtCompilePipelineParse(KrtCompilePipeline* pipeline) {
    if (!pipeline || !pipeline->lexer) {
        return 0;
    }

    clock_t start = clock();

    pipeline->parser = parser_create(pipeline->lexer);
    if (!pipeline->parser) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "语法分析器创建失败: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PARSE, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    pipeline->parser->source_name = pipeline->input_file ? pipeline->input_file : NULL;
    if (pipeline->lexer->error_count > 0) {
        apply_lexer_error(pipeline);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PARSE, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }
    pipeline->ast = parser_parse(pipeline->parser);

    if (pipeline->lexer->error_count > 0) {
        apply_lexer_error(pipeline);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PARSE, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    if (pipeline->ast && pipeline->parser->error_count > 0) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "语法错误 %d 处: %s",
                 pipeline->parser->error_count, pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PARSE, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    if (!pipeline->ast) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "语法分析失败: %s", pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_PARSE, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    record_stage_result(pipeline, KRT_STAGE_PARSE, KRT_RESULT_SUCCESS, (double)(clock() - start) / CLOCKS_PER_SEC,
                        pipeline->input_file);
    return 1;
}

int KrtCompilePipelineSemantic(KrtCompilePipeline* pipeline) {
    if (!pipeline || !pipeline->ast) {
        return 0;
    }

    clock_t start = clock();

    /* M1 修复: 多文件模式由驱动层预设共享分析器(已 collect_exports),
     * 此处不得无条件重建顶掉它 —— 否则跨文件符号全部失踪 */
    if (!pipeline->semantic_analyzer) {
        pipeline->semantic_analyzer = semantic_analyzer_create();
        pipeline->owns_semantic_analyzer = 1;
    }
    if (!pipeline->semantic_analyzer) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "语义分析器创建失败: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_SEMANTIC, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    semantic_analyzer_set_input_file(pipeline->semantic_analyzer, pipeline->input_file);
    {
        const char* of = pipeline->output_file;
        size_t ol = of ? strlen(of) : 0;
        if (ol >= 4 && strcmp(of + ol - 4, ".kro") == 0) {
            pipeline->semantic_analyzer->require_entry_point = false;
        }
    }
    semantic_analyzer_set_pipeline(pipeline->semantic_analyzer, pipeline);

    pipeline->semantic_result = semantic_analyzer_analyze(pipeline->semantic_analyzer, pipeline->ast);
    if (!pipeline->semantic_result || !pipeline->semantic_result->success) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "语义分析失败，发现 %d 个错误",
                 pipeline->semantic_result ? pipeline->semantic_result->error_count : 0);
        pipeline->success = 0;

        if (pipeline->semantic_result && pipeline->semantic_result->error_report) {
            KrtErrorReportSetSourceCode(pipeline->semantic_result->error_report, pipeline->source_code);
            KrtErrorReportSetFilePath(pipeline->semantic_result->error_report, pipeline->input_file);
        }

        record_stage_result(pipeline, KRT_STAGE_SEMANTIC, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    record_stage_result(pipeline, KRT_STAGE_SEMANTIC, KRT_RESULT_SUCCESS, (double)(clock() - start) / CLOCKS_PER_SEC,
                        pipeline->input_file);
    return 1;
}

int KrtCompilePipelineCodegen(KrtCompilePipeline* pipeline) {
    if (!pipeline || !pipeline->ast || !pipeline->semantic_analyzer) {
        return 0;
    }

    clock_t start = clock();

    if (!pipeline->config) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "编译配置缺失: %s", pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_CODEGEN, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    KrtTargetPlatform target;
    switch (pipeline->config->target_type) {
    case KRT_TARGET_ASM:
        target = KRT_TARGET_X86_ASM;
        break;
    case KRT_TARGET_IR:
        target = KRT_TARGET_IR_TEXT;
        break;
    case KRT_TARGET_KRO:
        target = KRT_TARGET_KRO_OBJ;
        break;
    case KRT_TARGET_ARM64:
        target = KRT_TARGET_ARM64_OBJ;
        break;
    case KRT_TARGET_EXE:
        target = KRT_TARGET_EXE_PLATFORM;
        break;
    default:
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "未知目标类型: %d",
                 (int)pipeline->config->target_type);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_CODEGEN, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    pipeline->compiler = KrtCompilerCreate(pipeline->output_file, target);
    if (!pipeline->compiler) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "编译器创建失败: %s", pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_CODEGEN, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    pipeline->compiler->optimization_level = pipeline->config->optimization_level;
    pipeline->compiler->show_ir = pipeline->config->show_ir;
    if (!pipeline->semantic_result || !pipeline->semantic_result->success) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "语义分析未完成或失败: %s",
                 pipeline->input_file);
        pipeline->success = 0;
        record_stage_result(pipeline, KRT_STAGE_CODEGEN, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    if (!KrtCompilerCompile(pipeline->compiler, pipeline->ast,
                            semantic_analyzer_get_symbol_table(pipeline->semantic_analyzer))) {
        pipeline->success = 0;
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "Code generation failed: %s",
                 pipeline->input_file);
        record_stage_result(pipeline, KRT_STAGE_CODEGEN, KRT_RESULT_FAILED, (double)(clock() - start) / CLOCKS_PER_SEC,
                            pipeline->input_file);
        return 0;
    }

    record_stage_result(pipeline, KRT_STAGE_CODEGEN, KRT_RESULT_SUCCESS, (double)(clock() - start) / CLOCKS_PER_SEC,
                        pipeline->input_file);
    return 1;
}

void KrtCompilePipelineSetMergedAst(KrtCompilePipeline* pipeline, ASTNode* merged_ast) {
    if (!pipeline) {
        return;
    }
    pipeline->ast = merged_ast;
}

void KrtCompilePipelineSetSemanticAnalyzer(KrtCompilePipeline* pipeline, SemanticAnalyzer* analyzer) {
    if (!pipeline) {
        return;
    }
    pipeline->semantic_analyzer = analyzer;
    pipeline->owns_semantic_analyzer = 0; /* 外部所有: 销毁责任在调用方 */
}

int KrtCompilePipelineExecute(KrtCompilePipeline* pipeline, const char* input_file, const char* output_file) {
    if (!pipeline || !input_file || !output_file) {
        return 0;
    }
    if (!pipeline->config) {
        snprintf(pipeline->error_message, sizeof(pipeline->error_message), "编译流水线缺少配置");
        pipeline->success = 0;
        return 0;
    }

    clock_t total_start = clock();

    pipeline->input_file = input_file;
    pipeline->output_file = output_file;

    if (pipeline->ast) {
        if (!KrtCompilePipelineSemantic(pipeline)) {
            return 0;
        }

        if (!KrtCompilePipelineCodegen(pipeline)) {
            return 0;
        }

        pipeline->total_duration = (double)(clock() - total_start) / CLOCKS_PER_SEC;
        record_stage_result(pipeline, KRT_STAGE_COMPLETE, KRT_RESULT_SUCCESS, pipeline->total_duration, input_file);

        return 1;
    }

    if (!KrtCompilePipelineReadSource(pipeline)) {
        return 0;
    }

    if (!KrtCompilePipelinePreprocess(pipeline)) {
        return 0;
    }

    if (!KrtCompilePipelineLex(pipeline)) {
        return 0;
    }

    if (!KrtCompilePipelineParse(pipeline)) {
        return 0;
    }

    if (!KrtCompilePipelineSemantic(pipeline)) {
        return 0;
    }

    if (!KrtCompilePipelineCodegen(pipeline)) {
        return 0;
    }

    pipeline->total_duration = (double)(clock() - total_start) / CLOCKS_PER_SEC;
    record_stage_result(pipeline, KRT_STAGE_COMPLETE, KRT_RESULT_SUCCESS, pipeline->total_duration, input_file);

    return 1;
}

int KrtCompilePipelineGetSuccess(KrtCompilePipeline* pipeline) {
    return pipeline ? pipeline->success : 0;
}

const char* KrtCompilePipelineGetError(KrtCompilePipeline* pipeline) {
    return pipeline ? pipeline->error_message : "Unknown error";
}

KrtCompileStageResult* KrtCompilePipelineGetStageResults(KrtCompilePipeline* pipeline, int* count) {
    if (!pipeline || !count) {
        return NULL;
    }
    *count = pipeline->stage_count;
    return pipeline->stage_results;
}

double KrtCompilePipelineGetTotalDuration(KrtCompilePipeline* pipeline) {
    return pipeline ? pipeline->total_duration : 0.0;
}

const char* KrtCompilePipelineGetFailedStageName(KrtCompilePipeline* pipeline) {
    if (!pipeline || pipeline->success) {
        return NULL;
    }
    return pipeline->failed_stage_name;
}

void KrtCompilePipelinePrintErrorReport(KrtCompilePipeline* pipeline) {
    if (!pipeline || pipeline->success) {
        return;
    }

    if (pipeline->semantic_result && pipeline->semantic_result->error_report) {
        KrtErrorReportPrint(pipeline->semantic_result->error_report);
        return;
    }

    const char* red = KrtColor(KRT_COL_RED);
    const char* bold = KrtColor(KRT_COL_BOLD);
    const char* cyan = KrtColor(KRT_COL_CYAN);
    const char* yellow = KrtColor(KRT_COL_YELLOW);
    const char* gray = KrtColor(KRT_COL_GRAY);
    const char* green = KrtColor(KRT_COL_GREEN);
    const char* reset = KrtColor(KRT_COL_RESET);

    fprintf(stderr, "\n");
    fprintf(stderr, "%s%s╔══════════════════════════════════════════════════════════════╗%s\n", bold, red, reset);
    fprintf(stderr, "%s%s║                    编 译 失 误 报 告                        ║%s\n", bold, red, reset);
    fprintf(stderr, "%s%s╚══════════════════════════════════════════════════════════════╝%s\n", bold, red, reset);
    fprintf(stderr, "\n");

    if (pipeline->input_file) {
        fprintf(stderr, "%s  源文件:%s %s%s%s\n", cyan, reset, yellow, pipeline->input_file, reset);
    }

    if (pipeline->failed_stage_name[0] != '\0') {
        fprintf(stderr, "%s  失败阶段:%s %s%s%s (%s)\n", cyan, reset, red, pipeline->failed_stage_name,
                KrtCompileStageNamesEnglish[pipeline->current_stage], reset);
    }

    if (pipeline->error_line > 0) {
        fprintf(stderr, "%s  错误位置:%s 第 %s%d%s 行", cyan, reset, yellow, pipeline->error_line, reset);
        if (pipeline->error_column > 0) {
            fprintf(stderr, ", 第 %s%d%s 列", yellow, pipeline->error_column, reset);
        }
        fprintf(stderr, "\n");
    }

    if (pipeline->error_message[0] != '\0') {
        fprintf(stderr, "%s  错误详情:%s %s%s%s\n", cyan, reset, yellow, pipeline->error_message, reset);
    }

    if (pipeline->error_hint[0] != '\0') {
        fprintf(stderr, "%s  修复建议:%s %s%s%s\n", cyan, reset, green, pipeline->error_hint, reset);
    }

    if (pipeline->source_code && pipeline->error_line > 0) {
        fprintf(stderr, "\n%s  源代码上下文:%s\n", bold, reset);

        const char* line_start = pipeline->source_code;
        int current_line = 1;

        while (line_start && current_line < pipeline->error_line) {
            const char* next = strchr(line_start, '\n');
            if (!next) {
                break;
            }
            line_start = next + 1;
            current_line++;
        }

        if (line_start) {
            const char* line_end = strchr(line_start, '\n');
            int line_len = line_end ? (int)(line_end - line_start) : (int)strlen(line_start);

            fprintf(stderr, "  %s──────────────────────────────────────────────────────────────%s\n", gray, reset);

            fprintf(stderr, "  %s%4d |%s ", gray, pipeline->error_line, reset);
            fprintf(stderr, "%.*s\n", line_len, line_start);

            if (pipeline->error_column > 0) {
                fprintf(stderr, "       %s|%s", gray, reset);
                int spaces = pipeline->error_column - 1;
                for (int i = 0; i < spaces && i < 80; i++) {
                    fprintf(stderr, " ");
                }
                fprintf(stderr, "%s^ 错误在这里%s\n", red, reset);
            }

            fprintf(stderr, "  %s──────────────────────────────────────────────────────────────%s\n", gray, reset);
        }
    }

    fprintf(stderr, "\n");

    fprintf(stderr, "%s  编译阶段进度:%s\n", bold, reset);
    fprintf(stderr, "  %s──────────────────────────────────────────────────────────────%s\n", gray, reset);

    for (int i = 0; i < pipeline->stage_count; i++) {
        KrtCompileStageResult* stage = &pipeline->stage_results[i];
        const char* status_icon;
        const char* status_color;

        if (stage->result == KRT_RESULT_SUCCESS) {
            status_icon = "✓";
            status_color = green;
        } else if (stage->result == KRT_RESULT_FAILED) {
            status_icon = "✗";
            status_color = red;
        } else {
            status_icon = "○";
            status_color = gray;
        }

        fprintf(stderr, "    %s%s%s  %s%-12s%s  %s  %.4f秒\n", status_color, status_icon, reset, gray,
                KrtCompileStageNamesEnglish[stage->stage], reset, KrtCompileStageNames[stage->stage], stage->duration);
    }

    fprintf(stderr, "  %s──────────────────────────────────────────────────────────────%s\n", gray, reset);
    fprintf(stderr, "\n");

    fprintf(stderr, "%s  总耗时:%s %.4f秒\n", cyan, reset, pipeline->total_duration);
    fprintf(stderr, "\n");
    fprintf(stderr, "%s%s════════════════════════════════════════════════════════════════%s\n", bold, red, reset);
    fprintf(stderr, "%s  编译已停止，请修复错误后重试。%s\n", yellow, reset);
    fprintf(stderr, "%s%s════════════════════════════════════════════════════════════════%s\n\n", bold, red, reset);

    fflush(stderr);
}

bool KrtCompilePipelineAddImportedFile(KrtCompilePipeline* pipeline, const char* file_path) {
    if (!pipeline || !file_path) {
        return false;
    }

    for (int i = 0; i < pipeline->imported_file_count; i++) {
        if (strcmp(pipeline->imported_files[i], file_path) == 0) {
            return true;
        }
    }

    if (pipeline->imported_file_count >= pipeline->imported_file_capacity) {
        if (pipeline->imported_file_capacity > INT_MAX / 2 ||
            (size_t)(pipeline->imported_file_capacity == 0 ? 8 : pipeline->imported_file_capacity * 2) >
                SIZE_MAX / sizeof(*pipeline->imported_files)) {
            return false;
        }
        int new_capacity = pipeline->imported_file_capacity == 0 ? 8 : pipeline->imported_file_capacity * 2;
        char** new_files = (char**)KRT_REALLOC(pipeline->imported_files, new_capacity * sizeof(char*));
        if (!new_files) {
            return false;
        }
        pipeline->imported_files = new_files;
        pipeline->imported_file_capacity = new_capacity;
    }

    char* copy = KRT_STRDUP(file_path);
    if (!copy) {
        return false;
    }
    pipeline->imported_files[pipeline->imported_file_count++] = copy;
    return true;
}

char** KrtCompilePipelineGetImportedFiles(KrtCompilePipeline* pipeline, int* count) {
    if (!pipeline || !count) {
        return NULL;
    }
    *count = pipeline->imported_file_count;
    return pipeline->imported_files;
}
