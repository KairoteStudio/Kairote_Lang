#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"
#include "ParserStatement.h"

#define KRT_PARSER_MALLOC(size) KRT_MALLOC(size)
#define KRT_PARSER_REALLOC(ptr, size) KRT_REALLOC(ptr, size)

#define KRT_PARSER_ALLOC_FROM_ARENA(parser, size) KrtParserAlloc(parser, size)

ASTNode* parser_parse_class_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    KrtTokenType class_type = parser->current_token.type;
    (void)class_type;
    parser_advance(parser);

    char* name = NULL;
    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        name = KRT_STRDUP(parser->current_token.value);
        parser_advance(parser);
    } else {
        return NULL;
    }

    if (!name) {
        return NULL;
    }

    char** template_params = NULL;
    int template_param_count = 0;
    if (parser->current_token.type == TOKEN_LESS) {
        parser_advance(parser);

        int capacity = 4;
        template_params = (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(char*));
        if (!template_params) {
            KRT_FREE(name);
            return NULL;
        }

        while (parser->current_token.type != TOKEN_GREATER) {
            if (template_param_count >= capacity) {
                capacity *= 2;
                char** new_params = (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(char*));
                if (!new_params) {
                    KRT_FREE(name);
                    for (int i = 0; i < template_param_count; i++) {
                        KRT_FREE(template_params[i]);
                    }
                    return NULL;
                }
                memcpy(new_params, template_params, template_param_count * sizeof(char*));
                template_params = new_params;
            }

            if (parser->current_token.type != TOKEN_IDENTIFIER) {
                break;
            }

            for (int i = 0; i < template_param_count; i++) {
                if (!strcmp(template_params[i], parser->current_token.value)) {
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
            KRT_FREE(name);
            for (int i = 0; i < template_param_count; i++) {
                KRT_FREE(template_params[i]);
            }
            return NULL;
        }
        parser_advance(parser);
    }

    ASTNode* base_class = NULL;
    if (parser->current_token.type == TOKEN_COLON) {
        parser_advance(parser);
        base_class = parser_parse_expression(parser);
    }

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        KRT_FREE(name);
        if (base_class) {
            ast_destroy_node(base_class);
        }
        for (int i = 0; i < template_param_count; i++) {
            KRT_FREE(template_params[i]);
        }
        return NULL;
    }

    char* saved_class = parser->current_class;
    parser->current_class = name;

    ASTNode* body = parser_parse_block(parser);
    if (!body) {
        parser->current_class = saved_class;
        KRT_FREE(name);
        if (base_class) {
            ast_destroy_node(base_class);
        }
        for (int i = 0; i < template_param_count; i++) {
            KRT_FREE(template_params[i]);
        }
        return NULL;
    }

    parser->current_class = saved_class;

    ASTNode* node = KrtParserCreateNode(parser, AST_CLASS_DECLARATION, line, col);
    if (!node) {
        ast_destroy_node(body);
        KRT_FREE(name);
        if (base_class) {
            ast_destroy_node(base_class);
        }
        for (int i = 0; i < template_param_count; i++) {
            KRT_FREE(template_params[i]);
        }
        return NULL;
    }

    node->data.class_decl.name = name;
    node->data.class_decl.body = body;
    node->data.class_decl.base_class = base_class;
    node->data.class_decl.template_params = template_params;
    node->data.class_decl.template_param_count = template_param_count;
    return node;
}

ASTNode* parser_parse_namespace_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    char** parts = NULL;
    int part_count = 0;
    int capacity = 8;

    parts = (char**)KRT_PARSER_MALLOC(capacity * sizeof(char*));
    if (!parts) {
        return NULL;
    }

    do {
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            for (int i = 0; i < part_count; i++) {
                KRT_FREE(parts[i]);
            }
            KRT_FREE(parts);
            return NULL;
        }

        if (part_count >= capacity) {
            capacity *= 2;
            char** new_parts = (char**)KRT_PARSER_REALLOC(parts, capacity * sizeof(char*));
            if (!new_parts) {
                for (int i = 0; i < part_count; i++) {
                    KRT_FREE(parts[i]);
                }
                KRT_FREE(parts);
                return NULL;
            }
            parts = new_parts;
        }

        parts[part_count++] = KRT_STRDUP(parser->current_token.value);
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
    char* full_name = KRT_PARSER_ALLOC_FROM_ARENA(parser, name_size);
    full_name[0] = 0;
    for (int i = 0; i < part_count; i++) {
        if (i) {
            strcat(full_name, ".");
        }
        strcat(full_name, parts[i]);
    }

    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);

        ASTNode* node = KrtParserCreateNode(parser, AST_NAMESPACE_DECLARATION, line, col);
        if (!node) {
            for (int i = 0; i < part_count; i++) {
                KRT_FREE(parts[i]);
            }
            KRT_FREE(parts);
            return NULL;
        }

        node->data.namespace_decl.name = full_name;
        node->data.namespace_decl.body = NULL;

        for (int i = 0; i < part_count; i++) {
            KRT_FREE(parts[i]);
        }
        KRT_FREE(parts);

        return node;
    }

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        for (int i = 0; i < part_count; i++) {
            KRT_FREE(parts[i]);
        }
        KRT_FREE(parts);
        return NULL;
    }

    ASTNode* body = parser_parse_block(parser);
    if (!body) {
        for (int i = 0; i < part_count; i++) {
            KRT_FREE(parts[i]);
        }
        KRT_FREE(parts);
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_NAMESPACE_DECLARATION, line, col);
    if (!node) {
        ast_destroy_node(body);
        for (int i = 0; i < part_count; i++) {
            KRT_FREE(parts[i]);
        }
        KRT_FREE(parts);
        return NULL;
    }

    node->data.namespace_decl.name = full_name;
    node->data.namespace_decl.body = body;

    for (int i = 0; i < part_count; i++) {
        KRT_FREE(parts[i]);
    }
    KRT_FREE(parts);

    return node;
}
