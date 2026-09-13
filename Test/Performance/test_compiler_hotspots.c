#include "compiler/Driver/Preprocessor.h"
#include "compiler/Frontend/Semantic/SymbolTable.h"
#include "compiler/Pipeline/CompilerPipeline.h"
#include "compiler/Driver/ParallelCompiler.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value)) {                                                                                                \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #value);                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static double now_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

static int write_source(const char* path, const char* source) {
    FILE* file = fopen(path, "wb");
    if (!file) {
        return 0;
    }
    size_t length = strlen(source);
    int ok = fwrite(source, 1, length, file) == length;
    return fclose(file) == 0 && ok;
}

static int preprocessor_test(int benchmark) {
    Preprocessor* pp = PreprocessorCreate();
    CHECK(pp);
    CHECK(PreprocessorAddMacro(pp, "print", "Write"));
    CHECK(PreprocessorAddMacro(pp, "println", "WriteLine"));
    for (int i = 0; i < 256; i++) {
        char name[32];
        snprintf(name, sizeof(name), "M%d", i);
        CHECK(PreprocessorAddMacro(pp, name, "123"));
    }
    CHECK(PreprocessorAddMacro(pp, "M255", "999"));
    if (!benchmark) {
        const char* input = "print println footprint print_int M255 M25 M255x\n\"print M255\" 'M255' // print M255\n/* "
                            "println */ print";
        const char* expected = "Write WriteLine footprint print_int 999 123 M255x\n\"print M255\" 'M255' // print "
                               "M255\n/* println */ Write";
        char* actual = PreprocessorProcess(pp, input);
        CHECK(actual && strcmp(actual, expected) == 0);
        KRT_FREE(actual);
        CHECK(PreprocessorAddMacro(pp, "M0", ""));
        actual = PreprocessorProcess(pp, "M0 M1");
        CHECK(actual && strcmp(actual, " 123") == 0);
        KRT_FREE(actual);
    } else {
        const char* line = "ordinary_identifier + M63 + ordinary_identifier;\n";
        size_t width = strlen(line), count = 18000;
        char* input = malloc(width * count + 1);
        CHECK(input);
        for (size_t i = 0; i < count; i++) {
            memcpy(input + i * width, line, width);
        }
        input[width * count] = 0;
        double start = now_seconds();
        char* result = PreprocessorProcess(pp, input);
        double duration = now_seconds() - start;
        CHECK(result && strstr(result, "+ 123 +"));
        printf("{\"preprocess_seconds\":%.9f,\"input_bytes\":%zu,\"macros\":%d}\n", duration, width * count,
               pp->macro_count);
        KRT_FREE(result);
        free(input);
    }
    PreprocessorDestroy(pp);
    return 0;
}

static int symbols_test(int benchmark) {
    SymbolTable* table = symbol_table_create();
    CHECK(table);
    SymbolEntry* outer = symbol_table_define(table, "shadow", SYMBOL_VARIABLE, 1, NULL);
    CHECK(outer);
    for (int i = 0; i < 4000; i++) {
        char name[32];
        snprintf(name, sizeof(name), "symbol_%d", i);
        CHECK(symbol_table_define(table, name, SYMBOL_VARIABLE, i + 2, NULL));
    }
    symbol_table_push_scope(table);
    SymbolEntry* inner = symbol_table_define(table, "shadow", SYMBOL_VARIABLE, 3, NULL);
    CHECK(inner && inner != outer);
    CHECK(symbol_table_lookup_scope_chain(table, "shadow") == inner);
    CHECK(symbol_table_lookup_current_scope(table, "symbol_0") == NULL);
    CHECK(symbol_table_lookup_scope_chain(table, "symbol_0") != NULL);
    symbol_table_pop_scope(table);
    CHECK(symbol_table_lookup_scope_chain(table, "shadow") == outer);
    if (benchmark) {
        double start = now_seconds();
        unsigned long long sum = 0;
        for (int i = 0; i < 160000; i++) {
            char name[32];
            snprintf(name, sizeof(name), "symbol_%d", i % 4000);
            SymbolEntry* entry = symbol_table_lookup_scope_chain(table, name);
            CHECK(entry);
            sum += entry->declaration_line;
        }
        printf("{\"symbol_seconds\":%.9f,\"lookups\":160000,\"checksum\":%llu}\n", now_seconds() - start, sum);
    }
    SymbolEntry* ns = symbol_table_define(table, "Nested", SYMBOL_NAMESPACE, 0, NULL);
    CHECK(ns);
    SymbolScope* saved = symbol_table_enter_nested_scope(table, ns);
    CHECK(symbol_table_define(table, "member", SYMBOL_VARIABLE, 0, NULL));
    CHECK(symbol_table_lookup_scope_chain(ns->nested_table, "member"));
    symbol_table_exit_nested_scope(table, saved);
    CHECK(symbol_table_lookup_current_scope(table, "member") == NULL);
    symbol_table_destroy(table);
    return 0;
}

static int parse_main(KrtCompilePipeline* pipeline, const char* input) {
    KrtCompilePipelineReset(pipeline);
    pipeline->input_file = input;
    return KrtCompilePipelineReadSource(pipeline) && KrtCompilePipelinePreprocess(pipeline) &&
           KrtCompilePipelineLex(pipeline) && KrtCompilePipelineParse(pipeline);
}

typedef struct {
    const char* input;
    const char* directory;
    int result;
} CacheWorker;

static void* cache_worker(void* pointer) {
    CacheWorker* worker = pointer;
    KrtConfig config = {0};
    KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(&config, NULL);
    worker->result = 1;
    for (int i = 0; i < 12; i++) {
        if (!pipeline || !parse_main(pipeline, worker->input) ||
            !KrtCompilePipelineLoadStandardLibrary(pipeline, worker->directory) ||
            pipeline->ast->data.block.statement_count != 2) {
            worker->result = 0;
            break;
        }
    }
    KrtCompilePipelineDestroy(pipeline);
    return NULL;
}

static int cache_test(const char* directory, int benchmark) {
    char library[1024], input[1024], output[1024];
    snprintf(library, sizeof(library), "%s/Library.krt", directory);
    snprintf(input, sizeof(input), "%s/../Main.krt", directory);
    snprintf(output, sizeof(output), "%s/../Main.ir", directory);
    CHECK(write_source(input, "int32 main() { return 0; }"));
    CHECK(write_source(library, "int32 CachedValue(int32 input) { return 7; }"));
    KrtConfig config = {0};
    config.target_type = KRT_TARGET_IR;
    KrtCompilePipeline* pipeline = KrtCompilePipelineCreate(&config, NULL);
    CHECK(pipeline);
    KrtStdlibCacheClear();
    if (benchmark) {
        FILE* file = fopen(library, "wb");
        CHECK(file);
        for (int i = 0; i < 1200; i++) {
            fprintf(file, "int32 Cached%d(int32 v) { return (v + %d) * 3; }\n", i, i);
        }
        fclose(file);
        CHECK(parse_main(pipeline, input));
        double start = now_seconds();
        CHECK(KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
        double cold = now_seconds() - start;
        double warm = 0;
        for (int i = 0; i < 8; i++) {
            CHECK(parse_main(pipeline, input));
            start = now_seconds();
            CHECK(KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
            warm += now_seconds() - start;
        }
        printf("{\"stdlib_cold_seconds\":%.9f,\"stdlib_warm_seconds\":%.9f}\n", cold, warm / 8);
    } else {
        CHECK(parse_main(pipeline, input));
        CHECK(KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
        CHECK(pipeline->ast->data.block.statement_count == 2);
        ASTNode* function = pipeline->ast->data.block.statements[1];
        CHECK(function->type == AST_FUNCTION_DECLARATION);
        CHECK(strcmp(function->data.function_decl.name, "CachedValue") == 0);
        CHECK(function->data.function_decl.parameter_count == 1);
        function->data.function_decl.name[0] = 'X';
        function->data.function_decl.parameters[0][0] = 'X';
        function->data.function_decl.parameter_types[0] = TOKEN_UINT64;
        function->data.function_decl.body->data.block.statements[0]->data.return_stmt.value->integer_value = 99;
        CHECK(parse_main(pipeline, input));
        CHECK(KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
        function = pipeline->ast->data.block.statements[1];
        CHECK(strcmp(function->data.function_decl.name, "CachedValue") == 0);
        CHECK(strcmp(function->data.function_decl.parameters[0], "input") == 0);
        CHECK(function->data.function_decl.parameter_types[0] == TOKEN_INT32);
        CHECK(function->data.function_decl.body->data.block.statements[0]->data.return_stmt.value->integer_value == 7);
        pipeline->output_file = output;
        CHECK(KrtCompilePipelineSemantic(pipeline));
        CHECK(KrtCompilePipelineCodegen(pipeline));
        CHECK(write_source(library, "int32 CachedValue(int32 input) { return 8; }"));
        CHECK(parse_main(pipeline, input));
        CHECK(KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
        function = pipeline->ast->data.block.statements[1];
        CHECK(function->data.function_decl.body->data.block.statements[0]->data.return_stmt.value->integer_value == 8);
        pthread_t threads[4];
        CacheWorker workers[4];
        for (int i = 0; i < 4; i++) {
            workers[i] = (CacheWorker){input, directory, 0};
            CHECK(pthread_create(&threads[i], NULL, cache_worker, &workers[i]) == 0);
        }
        for (int i = 0; i < 4; i++) {
            pthread_join(threads[i], NULL);
            CHECK(workers[i].result);
        }
        unsigned long long hits, misses;
        KrtStdlibCacheGetStats(&hits, &misses);
        CHECK(hits >= 49 && misses == 2);
        KrtArena* raw_arena = KrtArenaCreate(KRT_ARENA_DEFAULT_BLOCK_SIZE);
        CHECK(raw_arena);
        CHECK(KrtStdlibCacheClone(library, raw_arena, false));
        CHECK(KrtStdlibCacheClone(library, raw_arena, false));
        KrtStdlibCacheGetStats(&hits, &misses);
        CHECK(hits >= 50 && misses == 3);
        KrtArenaDestroy(raw_arena);
        CHECK(write_source(library, "int*? CachedPointer(fn(int*) -> int*? callback, int* p) { "
                                    "unsafe(using krt.mem;) { let q: int*? = callback(p); "
                                    "if (q is int* hit) { *hit = 7; } } return p; }"));
        raw_arena = KrtArenaCreate(KRT_ARENA_DEFAULT_BLOCK_SIZE);
        CHECK(raw_arena);
        ASTNode* pointer_ast = KrtStdlibCacheClone(library, raw_arena, false);
        CHECK(pointer_ast);
        ASTNode* pointer_fn = pointer_ast->data.block.statements[0];
        CHECK(pointer_fn->function_type->result.pointer_depth == 1);
        CHECK(pointer_fn->function_type->parameters[0].function->result.nullable);
        pointer_fn->function_type->parameters[0].function->result.pointer_depth = 9;
        ASTNode* unsafe_node = pointer_fn->data.function_decl.body->data.block.statements[0];
        unsafe_node->data.unsafe_call.permissions[0][0] = 'X';
        ASTNode* pattern = unsafe_node->data.unsafe_call.expression->data.block.statements[1]->data.if_stmt.condition;
        pattern->data.is_expr.binding_name[0] = 'X';
        KrtArenaDestroy(raw_arena);
        raw_arena = KrtArenaCreate(KRT_ARENA_DEFAULT_BLOCK_SIZE);
        CHECK(raw_arena);
        pointer_ast = KrtStdlibCacheClone(library, raw_arena, false);
        CHECK(pointer_ast);
        pointer_fn = pointer_ast->data.block.statements[0];
        CHECK(pointer_fn->function_type->parameters[0].function->result.pointer_depth == 1);
        unsafe_node = pointer_fn->data.function_decl.body->data.block.statements[0];
        CHECK(strcmp(unsafe_node->data.unsafe_call.permissions[0], "krt.mem") == 0);
        pattern = unsafe_node->data.unsafe_call.expression->data.block.statements[1]->data.if_stmt.condition;
        CHECK(strcmp(pattern->data.is_expr.binding_name, "hit") == 0);
        KrtArenaDestroy(raw_arena);
        CHECK(
            write_source(library, "int32 Broken() { uint128 n = 340282366920938463463374607431768211456; return 0; }"));
        CHECK(parse_main(pipeline, input));
        CHECK(!KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
        CHECK(unlink(library) == 0);
        CHECK(parse_main(pipeline, input));
        CHECK(KrtCompilePipelineLoadStandardLibrary(pipeline, directory));
        CHECK(pipeline->ast->data.block.statement_count == 1);
    }
    KrtCompilePipelineDestroy(pipeline);
    KrtStdlibCacheClear();
    return 0;
}

static int parallel_test(const char* directory, int large) {
    KrtConfig config = {0};
    config.target_type = KRT_TARGET_IR;
    config.platform = KRT_CONFIG_PLATFORM_LINUX;
    ParallelCompiler* compiler = ParallelCompilerCreate(4, &config);
    CHECK(compiler && !compiler->thread_pool);
    for (int i = 0; i < 8; i++) {
        char input[1024], output[1024];
        snprintf(input, sizeof(input), "%s/input%d.krt", directory, i);
        snprintf(output, sizeof(output), "%s/output%d.ir", directory, i);
        FILE* file = fopen(input, "wb");
        CHECK(file);
        fprintf(file, "int32 main() { return %d; }\n", i + 40);
        if (large) {
            for (int j = 0; j < 2200; j++) {
                fputs("// padding makes this job large enough to schedule in parallel\n", file);
            }
        }
        fclose(file);
        CHECK(ParallelCompilerAddFile(compiler, input, output, NULL, KRT_TARGET_IR, 0) == 0);
    }
    CHECK(ParallelCompilerExecute(compiler) == 0);
    CHECK(compiler->stats.succeeded == 8 && compiler->stats.failed == 0);
    CHECK((compiler->thread_pool != NULL) == (large != 0));
    for (int i = 0; i < 8; i++) {
        char path[1024], expected[64], data[4096];
        snprintf(path, sizeof(path), "%s/output%d.ir", directory, i);
        snprintf(expected, sizeof(expected), "return %d\n", i + 40);
        FILE* file = fopen(path, "rb");
        CHECK(file);
        size_t length = fread(data, 1, sizeof(data) - 1, file);
        data[length] = 0;
        fclose(file);
        CHECK(strstr(data, expected));
    }
    ParallelCompilerDestroy(compiler);
    return 0;
}

static int parallel_benchmark(const char* directory) {
    char inputs[64][1024], outputs[64][1024];
    for (int i = 0; i < 64; i++) {
        snprintf(inputs[i], sizeof(inputs[i]), "%s/input%d.krt", directory, i);
        snprintf(outputs[i], sizeof(outputs[i]), "%s/output%d.ir", directory, i);
        CHECK(write_source(inputs[i], "int32 main() { return 42; }\n"));
    }
    KrtConfig config = {0};
    config.target_type = KRT_TARGET_IR;
    config.platform = KRT_CONFIG_PLATFORM_LINUX;
    double start = now_seconds();
    ParallelCompiler* compiler = ParallelCompilerCreate(4, &config);
    CHECK(compiler);
    for (int i = 0; i < 64; i++) {
        CHECK(ParallelCompilerAddFile(compiler, inputs[i], outputs[i], NULL, KRT_TARGET_IR, 0) == 0);
    }
    CHECK(ParallelCompilerExecute(compiler) == 0);
    CHECK(compiler->stats.succeeded == 64);
    ParallelCompilerDestroy(compiler);
    printf("{\"batch_seconds\":%.9f,\"files\":64}\n", now_seconds() - start);
    return 0;
}

int main(int argc, char** argv) {
    KrtMemorySafetyInit();
    if (argc < 2) {
        return 2;
    }
    if (strcmp(argv[1], "preprocessor") == 0) {
        return preprocessor_test(0);
    }
    if (strcmp(argv[1], "symbols") == 0) {
        return symbols_test(0);
    }
    if (strcmp(argv[1], "bench_preprocessor") == 0) {
        return preprocessor_test(1);
    }
    if (strcmp(argv[1], "bench_symbols") == 0) {
        return symbols_test(1);
    }
    if (argc < 3) {
        return 2;
    }
    if (strcmp(argv[1], "cache") == 0) {
        return cache_test(argv[2], 0);
    }
    if (strcmp(argv[1], "bench_cache") == 0) {
        return cache_test(argv[2], 1);
    }
    if (strcmp(argv[1], "bench_parallel") == 0) {
        return parallel_benchmark(argv[2]);
    }
    if (strcmp(argv[1], "parallel_small") == 0) {
        return parallel_test(argv[2], 0);
    }
    if (strcmp(argv[1], "parallel_large") == 0) {
        return parallel_test(argv[2], 1);
    }
    return 2;
}
