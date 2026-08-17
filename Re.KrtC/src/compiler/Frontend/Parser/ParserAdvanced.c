#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "Core/Utils/KrtCommon.h"
#include "Core/Memory/Arena.h"
#include "Parser.h"
#include "ParserBase.h"
#include "ParserExpression.h"
#include "ParserStatement.h"
#include "../Lexer/Tokenizer.h"

#define PARSER_LIKELY(x) (x)
#define PARSER_UNLIKELY(x) (x)

#define PARSER_MALLOC(size) KRT_MALLOC(size)
#define PARSER_REALLOC(ptr, size) KRT_REALLOC(ptr, size)
#define PARSER_FREE(ptr) KRT_FREE(ptr)

#define PARSER_ALLOC_FROM_ARENA(parser, size) KrtArenaAlloc((parser)->arena, size)

#define PARSER_CREATE_NODE(type, line, col) ast_create_node_arena(type, line, col, parser->arena)
#define PARSER_STRDUP(s) arena_strdup(parser->arena, s)

#undef ast_create_node
#define ast_create_node(type, line, col) ast_create_node_arena(type, line, col, parser->arena)

static __attribute__((unused)) char* arena_strdup(KrtArena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* result = (char*)KrtArenaAlloc(arena, len);
    if (result) memcpy(result, str, len);
    return result;
}

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

    if (!name) return NULL;

    ASTNode** template_params = NULL;
    int template_param_count = 0;
    if (parser->current_token.type == TOKEN_LESS) {
        parser_advance(parser);

        int capacity = 4;
        template_params = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
        if (!template_params) { KRT_FREE(name); return NULL; }

        while (parser->current_token.type != TOKEN_GREATER) {
            if (template_param_count >= capacity) {
                capacity *= 2;
                ASTNode** new_params = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
                if (!new_params) { KRT_FREE(name); for (int i=0; i<template_param_count; i++) ast_destroy_node(template_params[i]); return NULL; }
                memcpy(new_params, template_params, template_param_count * sizeof(ASTNode*));
                template_params = new_params;
            }

            if (parser->current_token.type != TOKEN_IDENTIFIER) break;

            ASTNode* param = ast_create_node(AST_TEMPLATE_PARAMETER,
                                           parser->current_token.line,
                                           parser->current_token.column);
            if (!param) { KRT_FREE(name); for (int i=0; i<template_param_count; i++) ast_destroy_node(template_params[i]); return NULL; }
            param->data.template_param.param_name = KRT_STRDUP(parser->current_token.value);
            template_params[template_param_count++] = param;
            parser_advance(parser);

            if (parser->current_token.type == TOKEN_COMMA) {
                parser_advance(parser);
            } else {
                break;
            }
        }

        if (parser->current_token.type != TOKEN_GREATER) {
            KRT_FREE(name);
            for (int i=0; i<template_param_count; i++) ast_destroy_node(template_params[i]);
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
        if (base_class) ast_destroy_node(base_class);
        for (int i=0; i<template_param_count; i++) ast_destroy_node(template_params[i]);
        return NULL;
    }

    char* saved_class = parser->current_class;
    parser->current_class = name;

    ASTNode* body = parser_parse_block(parser);
    if (!body) {
        parser->current_class = saved_class;
        KRT_FREE(name);
        if (base_class) ast_destroy_node(base_class);
        for (int i=0; i<template_param_count; i++) ast_destroy_node(template_params[i]);
        return NULL;
    }

    parser->current_class = saved_class;

    ASTNode* node = ast_create_node(AST_CLASS_DECLARATION, line, col);
    if (!node) {
        ast_destroy_node(body);
        KRT_FREE(name);
        if (base_class) ast_destroy_node(base_class);
        for (int i=0; i<template_param_count; i++) ast_destroy_node(template_params[i]);
        return NULL;
    }

    node->data.class_decl.name = name;
    node->data.class_decl.body = body;
    node->data.class_decl.base_class = base_class;
    node->data.class_decl.template_params = (char**)template_params;
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

    parts = (char**)PARSER_MALLOC(capacity * sizeof(char*));
    if (!parts) return NULL;

    do {
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            for (int i=0; i<part_count; i++) KRT_FREE(parts[i]);
            KRT_FREE(parts);
            return NULL;
        }

        if (part_count >= capacity) {
            capacity *= 2;
            char** new_parts = (char**)PARSER_REALLOC(parts, capacity * sizeof(char*));
            if (!new_parts) { for (int i=0; i<part_count; i++) KRT_FREE(parts[i]); KRT_FREE(parts); return NULL; }
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

    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);

        ASTNode* node = ast_create_node(AST_NAMESPACE_DECLARATION, line, col);
        if (!node) {
            for (int i=0; i<part_count; i++) KRT_FREE(parts[i]);
            KRT_FREE(parts);
            return NULL;
        }

        node->data.namespace_decl.name = (parts && part_count > 0) ? parts[0] : NULL;
        node->data.namespace_decl.body = NULL;

        for (int i=1; i<part_count; i++) KRT_FREE(parts[i]);
        KRT_FREE(parts);

        return node;
    }

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        for (int i=0; i<part_count; i++) KRT_FREE(parts[i]);
        KRT_FREE(parts);
        return NULL;
    }

    ASTNode* body = parser_parse_block(parser);
    if (!body) {
        for (int i=0; i<part_count; i++) KRT_FREE(parts[i]);
        KRT_FREE(parts);
        return NULL;
    }

    ASTNode* node = ast_create_node(AST_NAMESPACE_DECLARATION, line, col);
    if (!node) {
        ast_destroy_node(body);
        for (int i=0; i<part_count; i++) KRT_FREE(parts[i]);
        KRT_FREE(parts);
        return NULL;
    }

    node->data.namespace_decl.name = (parts && part_count > 0) ? parts[0] : NULL;
    node->data.namespace_decl.body = body;

    for (int i=1; i<part_count; i++) KRT_FREE(parts[i]);
    KRT_FREE(parts);

    return node;
}
