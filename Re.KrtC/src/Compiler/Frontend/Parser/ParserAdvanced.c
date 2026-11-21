#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"
#include "ParserStatement.h"

ASTNode* parser_parse_class_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    KrtTokenType class_type = parser->current_token.type;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        parser_report_error(parser, line, col, "expected class name");
        return NULL;
    }
    char* name = KrtParserStrdup(parser, parser->current_token.value);
    parser_advance(parser);

    char** template_params = NULL;
    int template_param_count = 0;
    int template_capacity = 0;
    if (parser->current_token.type == TOKEN_LESS) {
        parser_advance(parser);

        while (parser->current_token.type != TOKEN_GREATER) {
            if (!KrtParserReserveStringArray(parser, &template_params, &template_capacity, template_param_count + 1,
                                             line, col)) {
                return NULL;
            }
            if (parser->current_token.type != TOKEN_IDENTIFIER) {
                parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                    "expected generic parameter name");
                return NULL;
            }

            for (int i = 0; i < template_param_count; i++) {
                if (template_params[i] && strcmp(template_params[i], parser->current_token.value) == 0) {
                    parser_report_error(parser, line, col, "Duplicate generic parameter");
                    return NULL;
                }
            }
            template_params[template_param_count++] = KrtParserStrdup(parser, parser->current_token.value);
            parser_advance(parser);

            if (parser->current_token.type == TOKEN_COMMA) {
                parser_advance(parser);
            } else {
                break;
            }
        }

        if (parser->current_token.type != TOKEN_GREATER) {
            parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                "expected '>' after generic parameters");
            return NULL;
        }
        parser_advance(parser);
    }

    ASTNode* base_class = NULL;
    if (parser->current_token.type == TOKEN_COLON) {
        parser_advance(parser);
        base_class = parser_parse_expression(parser);
        if (!base_class) {
            return NULL;
        }
    }

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected '{' after class declaration");
        return NULL;
    }

    char* saved_class = parser->current_class;
    parser->current_class = name;

    ASTNode* body = parser_parse_block(parser);
    parser->current_class = saved_class;
    if (!body) {
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_CLASS_DECLARATION, line, col);
    node->data.class_decl.name = name;
    node->data.class_decl.body = body;
    node->data.class_decl.base_class = base_class;
    node->data.class_decl.template_params = template_params;
    node->data.class_decl.template_param_count = template_param_count;
    node->data.class_decl.declaration_kind =
        class_type == TOKEN_STRUCT ? 1 : (class_type == TOKEN_INTERFACE ? 2 : 0);
    return node;
}

ASTNode* parser_parse_namespace_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    char** parts = NULL;
    int part_count = 0;
    int capacity = 0;

    do {
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                "expected namespace identifier");
            return NULL;
        }
        if (!KrtParserReserveStringArray(parser, &parts, &capacity, part_count + 1, line, col)) {
            return NULL;
        }
        parts[part_count++] = KrtParserStrdup(parser, parser->current_token.value);
        parser_advance(parser);

        if (parser->current_token.type == TOKEN_DOT) {
            parser_advance(parser);
        } else {
            break;
        }
    } while (1);

    size_t name_size = 1;
    for (int i = 0; i < part_count; i++) {
        name_size += strlen(parts[i]) + 1;
    }
    char* full_name = KrtParserAlloc(parser, name_size);
    full_name[0] = 0;
    for (int i = 0; i < part_count; i++) {
        if (i) {
            strcat(full_name, ".");
        }
        strcat(full_name, parts[i]);
    }

    ASTNode* body = NULL;
    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);
    } else if (parser->current_token.type == TOKEN_LEFT_BRACE) {
        body = parser_parse_block(parser);
        if (!body) {
            return NULL;
        }
    } else {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected '{' or ';' after namespace");
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_NAMESPACE_DECLARATION, line, col);
    node->data.namespace_decl.name = full_name;
    node->data.namespace_decl.body = body;
    return node;
}
