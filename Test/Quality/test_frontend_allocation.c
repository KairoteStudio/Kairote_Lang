#include "compiler/Frontend/Parser/ParserBase.h"
#include "compiler/Frontend/Semantic/SemanticAnalyzer.h"
#include <assert.h>
#include <stdio.h>

static int fail_after = -1;
static int calls;
static bool fail_heap;
static bool fail_arena;

static bool allocation_fails(void) {
    calls++;
    if (fail_after < 0) {
        return false;
    }
    if (fail_after-- == 0) {
        fail_after = -1;
        return true;
    }
    return false;
}
void* __real_KrtSafeMalloc(size_t, const char*, int);
void* __real_KrtSafeCalloc(size_t, size_t, const char*, int);
void* __real_KrtSafeRealloc(void*, size_t, const char*, int);
char* __real_KrtSafeStrdup(const char*, const char*, int);
void* __real_KrtArenaAlloc(KrtArena*, size_t);
void* __wrap_KrtSafeMalloc(size_t size, const char* file, int line) {
    return fail_heap && allocation_fails() ? NULL : __real_KrtSafeMalloc(size, file, line);
}
void* __wrap_KrtSafeCalloc(size_t count, size_t size, const char* file, int line) {
    return fail_heap && allocation_fails() ? NULL : __real_KrtSafeCalloc(count, size, file, line);
}
void* __wrap_KrtSafeRealloc(void* pointer, size_t size, const char* file, int line) {
    return fail_heap && allocation_fails() ? NULL : __real_KrtSafeRealloc(pointer, size, file, line);
}
char* __wrap_KrtSafeStrdup(const char* text, const char* file, int line) {
    return fail_heap && allocation_fails() ? NULL : __real_KrtSafeStrdup(text, file, line);
}
void* __wrap_KrtArenaAlloc(KrtArena* arena, size_t size) {
    return fail_arena && allocation_fails() ? NULL : __real_KrtArenaAlloc(arena, size);
}

static void check_analyzer_construction(void) {
    size_t baseline = KrtMemoryGetBlockCount();
    fail_heap = true;
    calls = 0;
    SemanticAnalyzer* analyzer = semantic_analyzer_create();
    assert(analyzer);
    int count = calls;
    semantic_analyzer_destroy(analyzer);
    assert(KrtMemoryGetBlockCount() == baseline);
    for (int i = 0; i < count; i++) {
        fail_after = i;
        analyzer = semantic_analyzer_create();
        assert(!analyzer);
        assert(KrtMemoryGetBlockCount() == baseline);
    }
    fail_heap = false;
    printf("Analyzer allocation failures checked: %d\n", count);
}

static void check_ast_cloning(void) {
    KrtSourceType parameter = {.token = TOKEN_IDENTIFIER, .type_name = "Value"};
    KrtFunctionType function = {.result = parameter, .parameters = &parameter, .parameter_count = 1};
    ASTNode node = {.type = AST_NUMBER,
                    .lexical_namespace = "Acme",
                    .function_type = &function,
                    .declared_type = {.token = TOKEN_FN, .function = &function}};
    size_t baseline = KrtMemoryGetBlockCount();
    fail_after = -1;
    KrtArena* arena = KrtArenaCreateLocal(128);
    assert(arena);
    fail_arena = true;
    calls = 0;
    ASTNode* copy = KrtAstClone(arena, &node);
    assert(copy && copy->declared_type.function != &function);
    int count = calls;
    fail_arena = false;
    KrtArenaDestroy(arena);
    assert(KrtMemoryGetBlockCount() == baseline);
    for (int i = 0; i < count; i++) {
        arena = KrtArenaCreateLocal(128);
        assert(arena);
        fail_arena = true;
        fail_after = i;
        assert(!KrtAstClone(arena, &node));
        fail_arena = false;
        KrtArenaDestroy(arena);
        assert(KrtMemoryGetBlockCount() == baseline);
    }
    printf("AST allocation failures checked: %d\n", count);
}
int main(void) {
    KrtMemorySafetyInit();
    check_analyzer_construction();
    check_ast_cloning();
    assert(!KrtMemoryGetBlockCount());
    KrtMemorySafetyCleanup();
    return 0;
}
