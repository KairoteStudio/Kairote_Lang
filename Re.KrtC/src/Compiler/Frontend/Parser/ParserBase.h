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
