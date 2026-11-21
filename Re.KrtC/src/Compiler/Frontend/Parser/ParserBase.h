#ifndef KRT_PARSER_BASE_H
#define KRT_PARSER_BASE_H

#include "Parser.h"
#include <limits.h>
#include "../../../Core/Utils/KrtCommon.h"

/** @brief Allocate parser-owned storage; allocation failure is a fatal compiler error. */
static inline void* KrtParserAlloc(Parser* parser, size_t size) {
    void* data = KrtArenaAlloc(parser->arena, size ? size : 1);
    if (!data) {
        KRT_COMPILE_ERROR("Out of memory while parsing source");
    }
    return data;
}

/** @brief Copy text into the parser arena; NULL input remains NULL, OOM is fatal. */
static inline char* KrtParserStrdup(Parser* parser, const char* text) {
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char* copy = KrtParserAlloc(parser, size);
    memcpy(copy, text, size);
    return copy;
}

/** @brief Create a parser-owned AST node, reporting allocation failure as fatal. */
static inline ASTNode* KrtParserCreateNode(Parser* parser, ASTNodeType type, int line, int column) {
    ASTNode* node = ast_create_node_arena(type, line, column, parser->arena);
    if (!node) {
        KRT_COMPILE_ERROR("Out of memory while creating syntax node");
    }
    return node;
}

/** @brief Report a parse error and return NULL (convenience for expression parsers). */
static inline ASTNode* KrtParserFail(Parser* parser, int line, int col, const char* message) {
    parser_report_error(parser, line, col, "%s", message);
    return NULL;
}

/* Growable parser-owned lists.  They live in the parser arena, so they are
 * reclaimed with the AST and must not be passed to KRT_FREE/KRT_REALLOC. */
static inline bool KrtParserReserveNodeArray(Parser* parser, ASTNode*** items, int* capacity, int needed, int line,
                                             int col) {
    if (needed <= *capacity) {
        return true;
    }

    int new_capacity = *capacity > 0 ? *capacity : 8;
    while (new_capacity < needed) {
        if (new_capacity > INT_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }

    ASTNode** resized = (ASTNode**)KrtArenaRealloc(parser->arena, *items, (size_t)(*capacity) * sizeof(ASTNode*),
                                                   (size_t)new_capacity * sizeof(ASTNode*));
    if (!resized) {
        parser_report_error(parser, line, col, "out of memory while building a statement list");
        return false;
    }

    *items = resized;
    *capacity = new_capacity;
    return true;
}

static inline bool KrtParserReserveStringArray(Parser* parser, char*** items, int* capacity, int needed, int line,
                                               int col) {
    if (needed <= *capacity) {
        return true;
    }

    int new_capacity = *capacity > 0 ? *capacity : 8;
    while (new_capacity < needed) {
        if (new_capacity > INT_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }

    char** resized = (char**)KrtArenaRealloc(parser->arena, *items, (size_t)(*capacity) * sizeof(char*),
                                             (size_t)new_capacity * sizeof(char*));
    if (!resized) {
        parser_report_error(parser, line, col, "out of memory while building a qualified name");
        return false;
    }

    *items = resized;
    *capacity = new_capacity;
    return true;
}

void parser_add_declared_function(Parser* parser, const char* func_name);
int parser_is_type_keyword(KrtTokenType type);
void parser_advance(Parser* parser);
/** @brief Parse a complete source type into type; report syntax errors and return false. */
bool KrtParseSourceType(Parser* parser, KrtSourceType* type);
/** @brief Parse an arena-owned qualified/generic type name; return NULL on invalid syntax. */
char* KrtParseNamedType(Parser* parser);
int parser_parse_parameter_list(Parser* parser, char*** parameters, KrtTokenType** parameter_types,
                                int** parameter_is_params, int** parameter_is_array,
                                ASTNode*** parameter_default_values, int* parameter_count);
void parser_free_parameter_list(char** parameters, KrtTokenType* parameter_types, int* parameter_is_params,
                                int* parameter_is_array, ASTNode** parameter_default_values, int parameter_count);

#endif
