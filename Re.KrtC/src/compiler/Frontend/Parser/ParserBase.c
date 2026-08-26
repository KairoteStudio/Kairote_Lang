#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "../../../Core/Utils/OutputCache.h"

#define PARSER_DEFAULT_ARENA_SIZE (128 * 1024)
#define PARSER_FUNCTION_CAPACITY_INIT 16
#define PARSER_ATTRIBUTE_CAPACITY_INIT 4
#define PARSER_PARAMETER_CAPACITY_INIT 8
#define PARSER_ARGUMENT_CAPACITY_INIT 8
#define PARSER_ELEMENT_CAPACITY_INIT 8
#define PARSER_MEMBER_CAPACITY_INIT 8

#define PARSER_LIKELY(x) (x)
#define PARSER_UNLIKELY(x) (x)

#define PARSER_MALLOC(size) KRT_MALLOC(size)
#define PARSER_REALLOC(ptr, size) KRT_REALLOC(ptr, size)
#define PARSER_FREE(ptr) KRT_FREE(ptr)

#define PARSER_ALLOC_FROM_ARENA(parser, size) KrtArenaAlloc((parser)->arena, size)

#define PARSER_CLEANUP_AND_RETURN_NULL(cleanup_expr) \
    do {                                               \
        cleanup_expr;                                  \
        return NULL;                                   \
    } while (0)

#define PARSER_FREE_STRING_ARRAY(arr, count)           \
    do {                                               \
        if (arr) {                                     \
            for (int i = 0; i < count; i++) {          \
                if (arr[i]) {                          \
                    KRT_FREE(arr[i]);                  \
                }                                      \
            }                                          \
            KRT_FREE(arr);                             \
        }                                              \
    } while (0)

#define PARSER_FREE_AST_NODE_ARRAY(arr, count)         \
    do {                                               \
        if (arr) {                                     \
            for (int i = 0; i < count; i++) {          \
                if (arr[i]) {                          \
                    ast_destroy_node(arr[i]);          \
                }                                      \
            }                                          \
            KRT_FREE(arr);                             \
        }                                              \
    } while (0)

#define PARSER_CREATE_NODE(type, line, col) ast_create_node_arena(type, line, col, parser->arena)
#define PARSER_STRDUP(s) arena_strdup(parser->arena, s)

static __attribute__((unused)) char* arena_strdup(KrtArena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* result = (char*)KrtArenaAlloc(arena, len);
    if (result) {
        memcpy(result, str, len);
    }
    return result;
}

void parser_advance(Parser* parser) {
    if (parser) {
        int i = parser->hist_len < 8 ? parser->hist_len : 7;
        if (parser->hist_len < 8) parser->hist_len++;
        else { for (int k = 0; k < 7; k++) { parser->hist_type[k] = parser->hist_type[k+1]; parser->hist_line[k] = parser->hist_line[k+1]; } }
        parser->hist_type[i] = (int)parser->current_token.type;
        parser->hist_line[i] = parser->current_token.line;
    }
    token_free(&parser->current_token);
    parser->current_token = lexer_next_token(parser->lexer);
}

void parser_add_declared_function(Parser* parser, const char* func_name) {
    if (!parser || !func_name) {
        return;
    }
    for (int i = 0; i < parser->declared_function_count; i++) {
        if (strcmp(parser->declared_functions[i], func_name) == 0) {
            return;
        }
    }
    if (parser->declared_function_count >= parser->declared_function_capacity) {
        int new_capacity = parser->declared_function_capacity == 0 ? PARSER_FUNCTION_CAPACITY_INIT : parser->declared_function_capacity * 2;
        char** new_functions = (char**)KRT_REALLOC(parser->declared_functions, new_capacity * sizeof(char*));
        if (!new_functions) {
            return;
        }
        parser->declared_functions = new_functions;
        parser->declared_function_capacity = new_capacity;
    }
    parser->declared_functions[parser->declared_function_count++] = KRT_STRDUP(func_name);
}

void parser_report_error(Parser* parser, int line, int col, const char* format, ...) {
    if (!parser) return;
    parser->error_count++;
    char prefix[64];
    if (parser->source_name) {
        snprintf(prefix, sizeof(prefix), "%s(%d,%d): ", parser->source_name, line, col);
    } else {
        snprintf(prefix, sizeof(prefix), "(%d,%d): ", line, col);
    }
    char body[512];
    va_list args;
    va_start(args, format);
    vsnprintf(body, sizeof(body), format, args);
    va_end(args);
    KrtOutputCacheAddError("%s\033[31merror:\033[0m %s\n", prefix, body);
}

Parser* parser_create(Lexer* lexer) {
    return parser_create_with_arena(lexer, PARSER_DEFAULT_ARENA_SIZE);
}

Parser* parser_create_with_arena(Lexer* lexer, size_t arena_size) {
    if (!lexer) {
        return NULL;
    }
    Parser* parser = (Parser*)KRT_MALLOC(sizeof(Parser));
    if (!parser) {
        return NULL;
    }
    parser->lexer = lexer;
    parser->current_token = lexer_next_token(lexer);
    parser->declared_functions = NULL;
    parser->declared_function_count = 0;
    parser->declared_function_capacity = 0;
    parser->is_unsafe_mode = 0;
    parser->current_class = NULL;
    parser->error_count = 0;
    parser->source_name = NULL;
    parser->hist_len = 0;

    if (arena_size > 0) {
        parser->arena = KrtArenaCreate(arena_size);
        if (!parser->arena) {
            KRT_FREE(parser);
            return NULL;
        }
    } else {
        parser->arena = NULL;
    }

    return parser;
}

void parser_destroy(Parser* parser) {
    if (!parser) {
        return;
    }
    if (parser->declared_functions) {
        for (int i = 0; i < parser->declared_function_count; i++) {
            KRT_FREE(parser->declared_functions[i]);
        }
        KRT_FREE(parser->declared_functions);
    }
    if (parser->arena) {
        KrtArenaDestroy(parser->arena);
    }
    KRT_FREE(parser);
}

int parser_is_type_keyword(KrtTokenType type) {
    switch (type) {
        case TOKEN_INT8:
        case TOKEN_INT16:
        case TOKEN_INT32:
        case TOKEN_INT64:
        case TOKEN_UINT8:
        case TOKEN_UINT16:
        case TOKEN_UINT32:
        case TOKEN_UINT64:
        case TOKEN_FLOAT32:
        case TOKEN_FLOAT64:
        case TOKEN_BOOL:
        case TOKEN_TYPE_STRING:
        case TOKEN_CHAR:
        case TOKEN_VOID:
            return 1;
        default:
            return 0;
    }
}