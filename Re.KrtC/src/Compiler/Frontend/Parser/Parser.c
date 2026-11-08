#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"
#include "ParserStatement.h"

#define KRT_PARSER_CREATE_NODE(type, line, col) KrtParserCreateNode(parser, type, line, col)

extern ASTNode* parser_parse_statement(Parser* parser);

ASTNode* parser_parse(Parser* parser) {
    if (!parser) {
        return NULL;
    }

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

    ASTNode* program = KRT_PARSER_CREATE_NODE(AST_PROGRAM, line, col);
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
