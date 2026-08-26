#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"
#include "ParserStatement.h"

#define PARSER_MALLOC(size) KRT_MALLOC(size)
#define PARSER_REALLOC(ptr, size) KRT_REALLOC(ptr, size)
#define PARSER_FREE(ptr) KRT_FREE(ptr)

#define PARSER_ALLOC_FROM_ARENA(parser, size) KrtArenaAlloc((parser)->arena, size)

#define PARSER_CREATE_NODE(type, line, col) ast_create_node_arena(type, line, col, parser->arena)
#define PARSER_STRDUP(s) arena_strdup(parser->arena, s)

static __attribute__((unused)) char* arena_strdup(KrtArena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* result = (char*)KrtArenaAlloc(arena, len);
    if (result) memcpy(result, str, len);
    return result;
}

extern ASTNode* parser_parse_statement(Parser* parser);

ASTNode* parser_parse(Parser* parser) {
    if (!parser) return NULL;

    int line = parser->current_token.line;
    int col = parser->current_token.column;
    ASTNode** statements = NULL;
    int statement_count = 0;
    
    while (parser->current_token.type != TOKEN_EOF) {
        ASTNode* stmt = parser_parse_statement(parser);
        if (!stmt) {
            if (parser->current_token.type != TOKEN_EOF) {
                parser_advance(parser);
            }
            continue;
        }
        
        ASTNode** new_statements = KRT_REALLOC(statements, (statement_count + 1) * sizeof(ASTNode*));
        if (!new_statements) {
            for (int i = 0; i < statement_count; i++) {
                ast_destroy_node(statements[i]);
            }
            KRT_FREE(statements);
            ast_destroy_node(stmt);
            return NULL;
        }
        
        statements = new_statements;
        statements[statement_count] = stmt;
        statement_count++;
    }
    
    ASTNode* program = PARSER_CREATE_NODE(AST_PROGRAM, line, col);
    if (!program) {
        for (int i = 0; i < statement_count; i++) {
            ast_destroy_node(statements[i]);
        }
        KRT_FREE(statements);
        return NULL;
    }
    
    program->data.block.statements = statements;
    program->data.block.statement_count = statement_count;
    
    return program;
}