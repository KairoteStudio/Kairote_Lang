#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"

ASTNode* parser_parse_expression(Parser* parser);

#define KRT_PARSER_ARGUMENT_CAPACITY_INIT 8

#define KRT_PARSER_UNLIKELY(x) (x)

/* Guards against stack overflow on pathologically nested expressions. */
#define KRT_MAX_EXPRESSION_DEPTH 512

#define KRT_PARSER_ALLOC_FROM_ARENA(parser, size) KrtParserAlloc(parser, size)

#define KRT_PARSER_CREATE_NODE(type, line, col) KrtParserCreateNode(parser, type, line, col)
#define KRT_PARSER_STRDUP(s) KrtParserStrdup(parser, s)

ASTNode* parser_parse_call(Parser* parser, ASTNode* callee) {
    parser_advance(parser);

    ASTNode** arguments = NULL;
    char** argument_names = NULL;
    int argument_count = 0;
    int capacity = 0;

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        capacity = KRT_PARSER_ARGUMENT_CAPACITY_INIT;
        arguments = (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
        argument_names = (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(char*));
        if (KRT_PARSER_UNLIKELY(!arguments || !argument_names)) {
            ast_destroy_node(callee);
            return NULL;
        }

        while (1) {
            char* param_name = NULL;
            ASTNode* arg = NULL;

            if (parser->current_token.type == TOKEN_RIGHT_PAREN) {
                break;
            }
            bool explicit_ref =
                parser->current_token.type == TOKEN_IDENTIFIER && !strcmp(parser->current_token.value, "ref");
            if (explicit_ref) {
                parser_advance(parser);
            }

            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                Token next = lexer_peek_token(parser->lexer);
                KrtTokenType next_type = next.type;
                token_free(&next);

                if (next_type == TOKEN_COLON) {
                    param_name = KRT_PARSER_STRDUP(parser->current_token.value);
                    parser_advance(parser);
                    parser_advance(parser);
                    arg = parser_parse_expression(parser);
                    if (KRT_PARSER_UNLIKELY(!arg)) {
                        KRT_FREE(param_name);
                        goto cleanup_error;
                    }
                } else {
                    arg = parser_parse_expression(parser);
                    if (KRT_PARSER_UNLIKELY(!arg)) {
                        goto cleanup_error;
                    }
                }
            } else {
                arg = parser_parse_expression(parser);
                if (KRT_PARSER_UNLIKELY(!arg)) {
                    goto cleanup_error;
                }
            }

            if (argument_count >= capacity) {
                capacity *= 2;
                ASTNode** new_arguments = (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
                char** new_argument_names = (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(char*));
                if (KRT_PARSER_UNLIKELY(!new_arguments || !new_argument_names)) {
                    KRT_FREE(param_name);
                    ast_destroy_node(arg);
                    goto cleanup_error;
                }
                memcpy(new_arguments, arguments, argument_count * sizeof(ASTNode*));
                memcpy(new_argument_names, argument_names, argument_count * sizeof(char*));
                arguments = new_arguments;
                argument_names = new_argument_names;
            }

            arg->is_ref_argument = explicit_ref;
            arguments[argument_count] = arg;
            argument_names[argument_count] = param_name;
            argument_count++;

            if (parser->current_token.type == TOKEN_RIGHT_PAREN) {
                break;
            }
            if (KRT_PARSER_UNLIKELY(parser->current_token.type != TOKEN_COMMA)) {
                goto cleanup_error;
            }
            parser_advance(parser);
        }
    }

    if (KRT_PARSER_UNLIKELY(parser->current_token.type != TOKEN_RIGHT_PAREN)) {
        goto cleanup_error;
    }
    parser_advance(parser);

    ASTNode* node = KrtParserCreateNode(parser, AST_CALL, parser->current_token.line, parser->current_token.column);
    if (KRT_PARSER_UNLIKELY(!node)) {
        goto cleanup_error;
    }

    if (callee->type == AST_IDENTIFIER) {
        node->data.call.name = KRT_PARSER_STRDUP(callee->data.identifier_name);
        node->data.call.object = NULL;
        ast_destroy_node(callee);
    } else if (callee->type == AST_MEMBER_ACCESS) {
        node->data.call.name = KRT_PARSER_STRDUP(callee->data.member_access.member_name);
        ASTNode* object = callee->data.member_access.object;
        callee->data.member_access.object = NULL;
        node->data.call.object = object;
        ast_destroy_node(callee);
    } else {
        node->data.call.name = KRT_PARSER_STRDUP("__expr_call__");
        node->data.call.object = callee;
    }
    node->data.call.arguments = arguments;
    node->data.call.argument_count = argument_count;
    node->data.call.argument_names = argument_names;
    return node;

cleanup_error:
    KrtParserFail(parser, parser->current_token.line, parser->current_token.column, "malformed call argument list");
    for (int i = 0; i < argument_count; i++) {
        ast_destroy_node(arguments[i]);
        if (argument_names && argument_names[i]) {
            KRT_FREE(argument_names[i]);
        }
    }
    ast_destroy_node(callee);
    return NULL;
}

/* Parses the text of a $"..." literal into literal parts plus hole
 * expressions.  The lexer has already decoded escapes, so a '}' inside a hole
 * would end it; that matches the simple form of the feature. */
static ASTNode* parser_parse_interpolated_string(Parser* parser, int line, int col, const char* text) {
    ASTNode* node = KrtParserCreateNode(parser, AST_INTERPOLATED_STRING, line, col);
    if (!node) {
        return NULL;
    }
    int capacity = 8;
    node->data.interpolated_string.string_parts =
        (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)capacity * sizeof(char*));
    node->data.interpolated_string.expressions =
        (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)capacity * sizeof(ASTNode*));
    if (!node->data.interpolated_string.string_parts || !node->data.interpolated_string.expressions) {
        return NULL;
    }

    char part[2048];
    int part_len = 0;
    int count = 0;
    const char* cursor = text;

    while (*cursor) {
        if (*cursor == '{') {
            const char* close = strchr(cursor + 1, '}');
            if (!close) {
                KrtParserFail(parser, line, col, "unterminated interpolation in string");
                return NULL;
            }
            part[part_len] = '\0';
            if (count >= capacity) {
                int new_capacity = capacity * 2;
                char** new_parts =
                    (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)new_capacity * sizeof(char*));
                ASTNode** new_exprs =
                    (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)new_capacity * sizeof(ASTNode*));
                if (!new_parts || !new_exprs) {
                    return NULL;
                }
                memcpy(new_parts, node->data.interpolated_string.string_parts, (size_t)count * sizeof(char*));
                memcpy(new_exprs, node->data.interpolated_string.expressions, (size_t)count * sizeof(ASTNode*));
                node->data.interpolated_string.string_parts = new_parts;
                node->data.interpolated_string.expressions = new_exprs;
                capacity = new_capacity;
            }
            node->data.interpolated_string.string_parts[count] = KRT_PARSER_STRDUP(part);

            size_t hole_len = (size_t)(close - cursor - 1);
            char* hole = (char*)KRT_PARSER_ALLOC_FROM_ARENA(parser, hole_len + 1);
            if (!hole) {
                return NULL;
            }
            memcpy(hole, cursor + 1, hole_len);
            hole[hole_len] = '\0';

            Lexer* sub_lexer = lexer_create(hole);
            Parser* sub_parser = sub_lexer ? parser_create(sub_lexer) : NULL;
            ASTNode* hole_expr = sub_parser ? parser_parse_expression(sub_parser) : NULL;
            /* The sub-parser owns its arena, so move the tree into the main
             * parser's arena before tearing it down. */
            ASTNode* owned = hole_expr ? KrtAstClone(parser->arena, hole_expr) : NULL;
            if (sub_parser) {
                parser_destroy(sub_parser);
            }
            if (sub_lexer) {
                lexer_destroy(sub_lexer);
            }
            if (!owned) {
                KrtParserFail(parser, line, col, "invalid interpolation expression");
                return NULL;
            }
            node->data.interpolated_string.expressions[count] = owned;
            count++;
            part_len = 0;
            cursor = close + 1;
            continue;
        }
        if (part_len < (int)sizeof(part) - 1) {
            part[part_len++] = *cursor;
        }
        cursor++;
    }

    part[part_len] = '\0';
    if (count >= capacity) {
        int new_capacity = capacity * 2;
        char** new_parts = (char**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)new_capacity * sizeof(char*));
        ASTNode** new_exprs =
            (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)new_capacity * sizeof(ASTNode*));
        if (!new_parts || !new_exprs) {
            return NULL;
        }
        memcpy(new_parts, node->data.interpolated_string.string_parts, (size_t)count * sizeof(char*));
        memcpy(new_exprs, node->data.interpolated_string.expressions, (size_t)count * sizeof(ASTNode*));
        node->data.interpolated_string.string_parts = new_parts;
        node->data.interpolated_string.expressions = new_exprs;
    }
    node->data.interpolated_string.string_parts[count] = KRT_PARSER_STRDUP(part);
    node->data.interpolated_string.part_count = count + 1;
    node->data.interpolated_string.expression_count = count;
    return node;
}

ASTNode* parser_parse_primary(Parser* parser) {
    Token token = parser->current_token;

    switch (token.type) {
    case TOKEN_NUMBER: {
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_NUMBER, token.line, token.column);
        if (!node) {
            return NULL;
        }
        bool prefixed = token.value[0] == '0' && (token.value[1] == 'x' || token.value[1] == 'X' ||
                                                  token.value[1] == 'b' || token.value[1] == 'B');
        node->is_integer_literal =
            prefixed || (!strchr(token.value, '.') && !strchr(token.value, 'e') && !strchr(token.value, 'E'));
        if (node->is_integer_literal) {
            if (!KrtIntegerParse(token.value, &node->integer_value)) {
                parser_report_error(parser, token.line, token.column,
                                    "invalid integer literal or value exceeds uint128");
                return NULL;
            }
            node->data.number_value = (double)node->integer_value;
        } else {
            node->data.number_value = atof(token.value);
        }
        return node;
    }
    case TOKEN_STACKALLOC: {
        parser_advance(parser);
        ASTNode* node = KRT_PARSER_CREATE_NODE(AST_STACKALLOC_EXPRESSION, token.line, token.column);
        if (!KrtParseSourceType(parser, &node->declared_type)) {
            return NULL;
        }
        node->data.stackalloc_expr.type_token = KrtSourceStorage(node->declared_type);
        if (parser->current_token.type != TOKEN_LEFT_BRACKET) {
            goto stackalloc_error;
        }
        parser_advance(parser);
        node->data.stackalloc_expr.count_expr = parser_parse_expression(parser);
        if (!node->data.stackalloc_expr.count_expr || parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            goto stackalloc_error;
        }
        parser_advance(parser);
        return node;
    stackalloc_error:
        parser_report_error(parser, token.line, token.column, "expected stackalloc T[count]");
        return NULL;
    }
    case TOKEN_SIZEOF: {
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_LEFT_PAREN) {
            return KrtParserFail(parser, token.line, token.column, "expected '(' after 'sizeof'");
        }
        parser_advance(parser);
        if (!parser_is_type_keyword(parser->current_token.type)) {
            return KrtParserFail(parser, token.line, token.column, "expected a type name in 'sizeof'");
        }
        KrtTokenType type = parser->current_token.type;
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            return KrtParserFail(parser, token.line, token.column, "expected ')' after 'sizeof'");
        }
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_SIZEOF_EXPRESSION, token.line, token.column);
        if (node) {
            node->data.sizeof_expr.type_token = type;
        }
        return node;
    }
    case TOKEN_FUNCTION: {
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_LEFT_PAREN) {
            return NULL;
        }
        parser_advance(parser);

        char** params = NULL;
        int param_count = 0;
        int cap = 0;
        while (parser->current_token.type != TOKEN_RIGHT_PAREN && parser->current_token.type != TOKEN_EOF) {
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                if (param_count == cap) {
                    int ncap = cap ? cap * 2 : 4;
                    char** np = (char**)KrtParserAlloc(parser, sizeof(char*) * ncap);
                    if (!np) {
                        params = NULL;
                        break;
                    }
                    for (int k = 0; k < param_count; k++) {
                        np[k] = params[k];
                    }
                    params = np;
                    cap = ncap;
                }
                params[param_count++] = parser->current_token.value;
            }
            parser_advance(parser);
            if (parser->current_token.type == TOKEN_COMMA) {
                parser_advance(parser);
            }
        }
        if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            return KrtParserFail(parser, token.line, token.column, "expected ')' after lambda parameters");
        }
        parser_advance(parser);

        if (parser->current_token.type != TOKEN_LAMBDA) {
            return KrtParserFail(parser, token.line, token.column, "expected '=>' after lambda parameters");
        }
        parser_advance(parser);

        ASTNode* node = KrtParserCreateNode(parser, AST_LAMBDA_EXPRESSION, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.lambda_expr.parameters = params;
        node->data.lambda_expr.parameter_count = param_count;
        node->data.lambda_expr.expression = parser_parse_expression(parser);
        node->data.lambda_expr.body = NULL;
        if (!node->data.lambda_expr.expression) {
            return NULL;
        }
        return node;
    }
    case TOKEN_DOLLAR: {
        Token next = lexer_peek_token(parser->lexer);
        if (next.type != TOKEN_STRING) {
            token_free(&next);
            return KrtParserFail(parser, token.line, token.column, "'$' must be followed by a string literal");
        }
        token_free(&next);
        parser_advance(parser);
        Token text_token = parser->current_token;
        parser_advance(parser);
        return parser_parse_interpolated_string(parser, token.line, token.column, text_token.value);
    }
    case TOKEN_STRING: {
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_STRING, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.string_value = KRT_PARSER_STRDUP(token.value);
        return node;
    }
    case TOKEN_CHAR_LITERAL: {
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_CHAR_LITERAL, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.char_value = token.value ? token.value[0] : '\0';
        return node;
    }
    case TOKEN_IDENTIFIER: {
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_IDENTIFIER, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.identifier_name = KRT_PARSER_STRDUP(token.value);
        return node;
    }
    case TOKEN_TRUE: {
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_BOOLEAN, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.boolean_value = 1;
        return node;
    }
    case TOKEN_FALSE: {
        parser_advance(parser);
        ASTNode* node = KrtParserCreateNode(parser, AST_BOOLEAN, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.boolean_value = 0;
        return node;
    }
    case TOKEN_NULL: {
        parser_advance(parser);
        return KrtParserCreateNode(parser, AST_NULL, token.line, token.column);
    }
    case TOKEN_THIS: {
        parser_advance(parser);
        return KrtParserCreateNode(parser, AST_THIS, token.line, token.column);
    }
    case TOKEN_LEFT_PAREN: {
        Token next = lexer_peek_token(parser->lexer);
        bool is_cast = false;
        bool is_array_cast = false;
        bool is_pointer_cast = false;

        if ((next.type >= TOKEN_INT2 && next.type <= TOKEN_VOID) || next.type == TOKEN_TYPE_STRING) {
            Token third = lexer_peek_nth_token(parser->lexer, 2);
            if (third.type == TOKEN_RIGHT_PAREN) {
                is_cast = true;
            } else if ((third.type == TOKEN_MULTIPLY || third.type == TOKEN_POWER)) {
                is_pointer_cast = true;
                int idx = 3;
                Token scan = lexer_peek_nth_token(parser->lexer, idx);
                while ((scan.type == TOKEN_MULTIPLY || scan.type == TOKEN_POWER)) {
                    token_free(&scan);
                    idx++;
                    scan = lexer_peek_nth_token(parser->lexer, idx);
                }
                if (scan.type == TOKEN_RIGHT_PAREN) {
                    is_cast = true;
                }
                token_free(&scan);
            } else if (third.type == TOKEN_LEFT_BRACKET) {
                Token fifth = lexer_peek_nth_token(parser->lexer, 4);
                if (fifth.type == TOKEN_RIGHT_PAREN) {
                    is_cast = true;
                    is_array_cast = true;
                }
                token_free(&fifth);
            }
            token_free(&third);
        }

        if (is_cast) {
            KrtTokenType cast_type = next.type;
            parser_advance(parser);
            parser_advance(parser);
            if (is_array_cast) {
                parser_advance(parser);
                parser_advance(parser);
            }
            unsigned pointer_depth = 0;
            while ((parser->current_token.type == TOKEN_MULTIPLY || parser->current_token.type == TOKEN_POWER)) {
                pointer_depth += parser->current_token.type == TOKEN_POWER ? 2 : 1;
                parser_advance(parser);
            }
            parser_advance(parser);
            ASTNode* expr = parser_parse_binary_operation(parser, 11);
            if (!expr) {
                token_free(&next);
                return NULL;
            }
            ASTNode* node = KrtParserCreateNode(parser, AST_CAST_EXPRESSION, token.line, token.column);
            if (!node) {
                ast_destroy_node(expr);
                token_free(&next);
                return NULL;
            }
            node->data.cast_expr.target_type = cast_type;
            node->data.cast_expr.target_is_pointer = is_array_cast || is_pointer_cast;
            node->declared_type = (KrtSourceType){.token = cast_type, .pointer_depth = pointer_depth};
            node->data.cast_expr.expression = expr;
            token_free(&next);
            return node;
        }
        token_free(&next);

        parser_advance(parser);
        ASTNode* expr = parser_parse_expression(parser);
        if (!expr) {
            return NULL;
        }
        if (parser->current_token.type == TOKEN_COMMA) {
            int capacity = 4;
            int count = 0;
            ASTNode** elements = (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)capacity * sizeof(ASTNode*));
            if (!elements) {
                ast_destroy_node(expr);
                return NULL;
            }
            elements[count++] = expr;
            while (parser->current_token.type == TOKEN_COMMA) {
                parser_advance(parser);
                if (parser->current_token.type == TOKEN_RIGHT_PAREN) {
                    break;
                }
                ASTNode* element = parser_parse_expression(parser);
                if (!element) {
                    return NULL;
                }
                if (count >= capacity) {
                    int grown_capacity = capacity * 2;
                    ASTNode** grown =
                        (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, (size_t)grown_capacity * sizeof(ASTNode*));
                    if (!grown) {
                        return NULL;
                    }
                    memcpy(grown, elements, (size_t)count * sizeof(ASTNode*));
                    elements = grown;
                    capacity = grown_capacity;
                }
                elements[count++] = element;
            }
            if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
                return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                     "expected ')' after tuple");
            }
            parser_advance(parser);
            ASTNode* node = KrtParserCreateNode(parser, AST_TUPLE_EXPRESSION, token.line, token.column);
            if (!node) {
                return NULL;
            }
            node->data.tuple_expr.elements = elements;
            node->data.tuple_expr.element_count = count;
            return node;
        }
        if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            ast_destroy_node(expr);
            return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                 "expected ')' after expression");
        }
        parser_advance(parser);
        return expr;
    }
    case TOKEN_LEFT_BRACKET:
    case TOKEN_LEFT_BRACE: {
        KrtTokenType closing = token.type == TOKEN_LEFT_BRACE ? TOKEN_RIGHT_BRACE : TOKEN_RIGHT_BRACKET;
        parser_advance(parser);
        ASTNode** elements = NULL;
        int element_count = 0;
        int capacity = 8;

        if (parser->current_token.type != closing) {
            elements = (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
            if (!elements) {
                return NULL;
            }

            while (1) {
                ASTNode* elem = parser_parse_expression(parser);
                if (!elem) {
                    goto array_cleanup;
                }

                if (element_count >= capacity) {
                    capacity *= 2;
                    ASTNode** new_elems = (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
                    if (!new_elems) {
                        ast_destroy_node(elem);
                        goto array_cleanup;
                    }
                    memcpy(new_elems, elements, element_count * sizeof(ASTNode*));
                    elements = new_elems;
                }
                elements[element_count++] = elem;

                if (parser->current_token.type == closing) {
                    break;
                }
                if (parser->current_token.type != TOKEN_COMMA) {
                    goto array_cleanup;
                }
                parser_advance(parser);
            }
        }

        if (parser->current_token.type != closing) {
            goto array_cleanup;
        }
        parser_advance(parser);

        ASTNode* node = KrtParserCreateNode(parser, AST_ARRAY_LITERAL, token.line, token.column);
        if (!node) {
            goto array_cleanup;
        }
        node->data.array_literal.elements = elements;
        node->data.array_literal.element_count = element_count;
        return node;

    array_cleanup:
        KrtParserFail(parser, parser->current_token.line, parser->current_token.column, "malformed array literal");
        if (elements) {
            for (int i = 0; i < element_count; i++) {
                ast_destroy_node(elements[i]);
            }
        }
        return NULL;
    }
#define KRT_INTEGER_WIDTH(bits)                                                                                        \
    case TOKEN_INT##bits:                                                                                              \
    case TOKEN_UINT##bits:
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
    case TOKEN_FLOAT32:
    case TOKEN_FLOAT64:
    case TOKEN_TYPE_STRING:
    case TOKEN_CHAR:
    case TOKEN_BOOL:
    case TOKEN_VOID: {
        KrtTokenType type = token.type;
        parser_advance(parser);

        if (parser->current_token.type == TOKEN_LEFT_PAREN) {
            parser_advance(parser);
            ASTNode* expr = parser_parse_expression(parser);
            if (!expr) {
                return NULL;
            }
            if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
                ast_destroy_node(expr);
                return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                     "expected ')' after cast expression");
            }
            parser_advance(parser);

            ASTNode* node = KrtParserCreateNode(parser, AST_CAST_EXPRESSION, token.line, token.column);
            if (!node) {
                ast_destroy_node(expr);
                return NULL;
            }
            node->data.cast_expr.target_type = type;
            node->data.cast_expr.expression = expr;
            return node;
        }

        ASTNode* node = KrtParserCreateNode(parser, AST_GENERIC_TYPE, token.line, token.column);
        if (!node) {
            return NULL;
        }
        node->data.generic_type.type_name = KRT_PARSER_STRDUP(token.value);
        return node;
    }
    case TOKEN_NEW: {
        parser_advance(parser);

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            char* class_name = KrtParseNamedType(parser);
            if (!class_name) {
                return NULL;
            }

            if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
                parser_advance(parser);
                ASTNode* size = parser_parse_expression(parser);
                if (!size || parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                    return KrtParserFail(parser, token.line, token.column, "expected ']' after array size");
                }
                parser_advance(parser);
                ASTNode* node = KrtParserCreateNode(parser, AST_NEW_ARRAY_EXPRESSION, token.line, token.column);
                if (!node) {
                    return NULL;
                }
                node->data.new_array_expr.type_token = TOKEN_IDENTIFIER;
                node->data.new_array_expr.element_type = class_name;
                node->data.new_array_expr.size = size;
                return node;
            }

            ASTNode** args = NULL;
            int arg_count = 0;
            int arg_capacity = 8;

            if (parser->current_token.type == TOKEN_LEFT_PAREN) {
                parser_advance(parser);
                args = (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, sizeof(ASTNode*) * arg_capacity);
                if (!args) {
                    return NULL;
                }

                if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
                    while (1) {
                        ASTNode* arg = parser_parse_expression(parser);
                        if (!arg) {
                            return KrtParserFail(parser, token.line, token.column, "expected constructor argument");
                        }
                        if (arg_count >= arg_capacity) {
                            arg_capacity *= 2;
                            ASTNode** new_args =
                                (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, sizeof(ASTNode*) * arg_capacity);
                            if (!new_args) {
                                return NULL;
                            }
                            memcpy(new_args, args, arg_count * sizeof(ASTNode*));
                            args = new_args;
                        }
                        args[arg_count++] = arg;
                        if (parser->current_token.type == TOKEN_COMMA) {
                            parser_advance(parser);
                            continue;
                        }
                        break;
                    }
                }
                if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
                    return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                         "expected ')' after constructor arguments");
                }
                parser_advance(parser);
            }

            ASTNode* node = KrtParserCreateNode(parser, AST_NEW_EXPRESSION, token.line, token.column);
            if (!node) {
                return NULL;
            }
            node->data.new_expr.class_name = class_name;
            node->data.new_expr.arguments = args;
            node->data.new_expr.argument_count = arg_count;
            node->data.new_expr.argument_names = NULL;
            node->data.new_expr.type_token = TOKEN_IDENTIFIER;
            return node;
        }

        if (parser_is_type_keyword(parser->current_token.type)) {
            KrtTokenType elem_type = parser->current_token.type;
            char* elem_name = KRT_PARSER_STRDUP(parser->current_token.value ? parser->current_token.value : "");
            parser_advance(parser);

            if (parser->current_token.type != TOKEN_LEFT_BRACKET) {
                return KrtParserFail(parser, token.line, token.column, "expected '[' after array element type");
            }
            parser_advance(parser);
            ASTNode* size = parser_parse_expression(parser);
            if (!size) {
                return KrtParserFail(parser, token.line, token.column, "expected array size expression");
            }
            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                ast_destroy_node(size);
                return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                     "expected ']' after array size");
            }
            parser_advance(parser);

            ASTNode* node = KrtParserCreateNode(parser, AST_NEW_ARRAY_EXPRESSION, token.line, token.column);
            if (!node) {
                ast_destroy_node(size);
                return NULL;
            }
            node->data.new_array_expr.type_token = elem_type;
            node->data.new_array_expr.element_type = elem_name;
            node->data.new_array_expr.size = size;
            return node;
        }

        return KrtParserFail(parser, token.line, token.column, "expected a type after 'new'");
    }
    default:
        return KrtParserFail(parser, token.line, token.column, "expected an expression");
    }
}

int get_operator_precedence(KrtTokenType type) {
    switch (type) {
    case TOKEN_OR:
        return 1;
    case TOKEN_AND:
        return 2;
    case TOKEN_BITWISE_OR:
    case TOKEN_PIPE:
        return 3;
    case TOKEN_BITWISE_XOR:
        return 4;
    case TOKEN_BITWISE_AND:
        return 5;
    case TOKEN_EQUAL:
    case TOKEN_NOT_EQUAL:
        return 6;
    case TOKEN_IS:
    case TOKEN_AS:
    case TOKEN_LESS:
    case TOKEN_GREATER:
    case TOKEN_LESS_EQUAL:
    case TOKEN_GREATER_EQUAL:
        return 7;
    case TOKEN_LSHIFT:
    case TOKEN_RSHIFT:
        return 8;
    case TOKEN_PLUS:
    case TOKEN_MINUS:
        return 9;
    case TOKEN_MULTIPLY:
    case TOKEN_DIVIDE:
    case TOKEN_MODULO:
        return 10;
    default:
        return 0;
    }
}

static ASTNode* parser_parse_unary_prefix(Parser* parser) {
    KrtTokenType op = parser->current_token.type;
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    ASTNode* operand = parser_parse_binary_operation(parser, 11);
    if (!operand) {
        return NULL;
    }
    if (op == TOKEN_BITWISE_AND || op == TOKEN_MULTIPLY || op == TOKEN_POWER) {
        ASTNode* node =
            KRT_PARSER_CREATE_NODE(op == TOKEN_BITWISE_AND ? AST_ADDRESS_OF : AST_POINTER_DEREFERENCE, line, col);
        if (op == TOKEN_BITWISE_AND) {
            node->data.address_of.operand = operand;
        } else {
            if (op == TOKEN_POWER) {
                ASTNode* inner = KRT_PARSER_CREATE_NODE(AST_POINTER_DEREFERENCE, line, col);
                inner->data.pointer_deref.pointer = operand;
                operand = inner;
            }
            node->data.pointer_deref.pointer = operand;
        }
        return node;
    }
    if (op == TOKEN_AWAIT) {
        ASTNode* node = KRT_PARSER_CREATE_NODE(AST_AWAIT_EXPRESSION, line, col);
        if (!node) {
            ast_destroy_node(operand);
            return NULL;
        }
        node->data.await_expr.expression = operand;
        return node;
    }
    ASTNode* node = KrtParserCreateNode(parser, AST_UNARY_OPERATION, line, col);
    if (!node) {
        ast_destroy_node(operand);
        return NULL;
    }
    node->data.unary_op.operator = op;
    node->data.unary_op.operand = operand;
    return node;
}

static ASTNode* parser_parse_binary_operation_inner(Parser* parser, int precedence) {
    ASTNode* left;
    if (parser->current_token.type == TOKEN_POWER || parser->current_token.type == TOKEN_BITWISE_AND ||
        parser->current_token.type == TOKEN_MULTIPLY || parser->current_token.type == TOKEN_MINUS ||
        parser->current_token.type == TOKEN_NOT || parser->current_token.type == TOKEN_TILDE ||
        parser->current_token.type == TOKEN_PLUS || parser->current_token.type == TOKEN_AWAIT ||
        parser->current_token.type == TOKEN_INCREMENT || parser->current_token.type == TOKEN_DECREMENT) {
        left = parser_parse_unary_prefix(parser);
    } else {
        left = parser_parse_postfix_expression(parser);
    }
    if (!left) {
        return NULL;
    }

    int prec = get_operator_precedence(parser->current_token.type);
    while (prec > 0 && prec >= precedence) {
        KrtTokenType op = parser->current_token.type;
        int op_line = parser->current_token.line;
        int op_col = parser->current_token.column;
        parser_advance(parser);

        if (op == TOKEN_AS) {
            ASTNode* node = KRT_PARSER_CREATE_NODE(AST_AS_EXPRESSION, op_line, op_col);
            if (!node) {
                ast_destroy_node(left);
                return NULL;
            }
            node->data.as_expr.expression = left;
            KrtSourceType target = {0};
            if (!KrtParseSourceType(parser, &target)) {
                ast_destroy_node(node);
                return NULL;
            }
            node->declared_type = target;
            node->data.as_expr.type_name = target.type_name ? KRT_PARSER_STRDUP(target.type_name) : NULL;
            left = node;
            prec = get_operator_precedence(parser->current_token.type);
            continue;
        }
        if (op == TOKEN_IS) {
            ASTNode* node = KRT_PARSER_CREATE_NODE(AST_IS_EXPRESSION, op_line, op_col);
            node->data.is_expr.expression = left;
            if (!KrtParseSourceType(parser, &node->declared_type)) {
                return NULL;
            }
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                node->data.is_expr.binding_name = KRT_PARSER_STRDUP(parser->current_token.value);
                parser_advance(parser);
            }
            left = node;
            prec = get_operator_precedence(parser->current_token.type);
            continue;
        }
        ASTNode* right = parser_parse_binary_operation(parser, get_operator_precedence(op) + 1);
        if (!right) {
            parser_report_error(parser, op_line, op_col, "Expected right operand of binary operator");
            ast_destroy_node(left);
            return NULL;
        }

        ASTNode* node = KrtParserCreateNode(parser, AST_BINARY_OPERATION, op_line, op_col);
        if (!node) {
            ast_destroy_node(left);
            ast_destroy_node(right);
            return NULL;
        }
        node->data.binary_op.left = left;
        node->data.binary_op.right = right;
        node->data.binary_op.operator = op;
        left = node;

        prec = get_operator_precedence(parser->current_token.type);
    }

    return left;
}

ASTNode* parser_parse_binary_operation(Parser* parser, int precedence) {
    if (parser->expression_depth >= KRT_MAX_EXPRESSION_DEPTH) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expression is nested too deeply");
        return NULL;
    }

    parser->expression_depth++;
    ASTNode* result = parser_parse_binary_operation_inner(parser, precedence);
    parser->expression_depth--;
    return result;
}

ASTNode* parser_parse_postfix_expression(Parser* parser) {
    ASTNode* primary = parser_parse_primary(parser);
    if (!primary) {
        return NULL;
    }

    while (1) {
        if (parser->current_token.type == TOKEN_DOUBLE_COLON && primary->type == AST_IDENTIFIER &&
            strcmp(primary->data.identifier_name, "global") == 0) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_IDENTIFIER) {
                parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                    "expected identifier after 'global::'");
                ast_destroy_node(primary);
                return NULL;
            }
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "global::%s", parser->current_token.value);
            primary->data.identifier_name = KRT_PARSER_STRDUP(tmp);
            parser_advance(parser);
        } else if (parser->current_token.type == TOKEN_DOUBLE_COLON) {
            parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                "'::' is only valid as 'global::' root-scope escape; "
                                "use '.' for member access");
            ast_destroy_node(primary);
            return NULL;
        } else if (parser->current_token.type == TOKEN_DOT) {
            parser_advance(parser);
            if (parser->current_token.type == TOKEN_NUMBER && !strchr(parser->current_token.value, '.')) {
                ASTNode* element =
                    KrtParserCreateNode(parser, AST_TUPLE_ELEMENT_ACCESS, primary->line, primary->col);
                if (!element) {
                    ast_destroy_node(primary);
                    return NULL;
                }
                element->data.tuple_element_access.tuple = primary;
                element->data.tuple_element_access.index =
                    (int)strtoull(parser->current_token.value, NULL, 10);
                primary = element;
                parser_advance(parser);
            } else {
                if (parser->current_token.type != TOKEN_IDENTIFIER) {
                    ast_destroy_node(primary);
                    return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                         "expected member name after '.'");
                }

                ASTNode* member = KrtParserCreateNode(parser, AST_MEMBER_ACCESS, primary->line, primary->col);
                if (!member) {
                    ast_destroy_node(primary);
                    return NULL;
                }
                member->data.member_access.object = primary;
                member->data.member_access.member_name = KRT_PARSER_STRDUP(parser->current_token.value);
                primary = member;
                parser_advance(parser);
            }
        } else if (parser->current_token.type == TOKEN_QUESTION_DOT) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_IDENTIFIER) {
                ast_destroy_node(primary);
                return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                     "expected member name after '?.'");
            }
            ASTNode* member = KrtParserCreateNode(parser, AST_NULL_CONDITIONAL, primary->line, primary->col);
            if (!member) {
                ast_destroy_node(primary);
                return NULL;
            }
            member->data.null_conditional.expression = primary;
            member->data.null_conditional.member_name = KRT_PARSER_STRDUP(parser->current_token.value);
            member->data.null_conditional.is_method_call = 0;
            primary = member;
            parser_advance(parser);
        } else if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
            parser_advance(parser);
            ASTNode* index = parser_parse_expression(parser);
            if (!index) {
                ast_destroy_node(primary);
                return NULL;
            }
            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                ast_destroy_node(index);
                ast_destroy_node(primary);
                return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                                     "expected ']' after array index");
            }
            parser_advance(parser);

            ASTNode* access = KrtParserCreateNode(parser, AST_ARRAY_ACCESS, primary->line, primary->col);
            if (!access) {
                ast_destroy_node(index);
                ast_destroy_node(primary);
                return NULL;
            }
            access->data.array_access.array = primary;
            access->data.array_access.index = index;
            primary = access;
        } else if (parser->current_token.type == TOKEN_INCREMENT || parser->current_token.type == TOKEN_DECREMENT) {
            ASTNode* node = KrtParserCreateNode(parser, AST_UNARY_OPERATION, primary->line, primary->col);
            if (!node) {
                ast_destroy_node(primary);
                return NULL;
            }
            node->data.unary_op.operator = parser->current_token.type;
            node->data.unary_op.operand = primary;
            node->data.unary_op.is_postfix = 1;
            parser_advance(parser);
            primary = node;
        } else if (parser->current_token.type == TOKEN_LEFT_PAREN) {
            ASTNode* call = parser_parse_call(parser, primary);
            if (!call) {
                return NULL;
            }
            primary = call;
        } else {
            break;
        }
    }

    return primary;
}

ASTNode* parser_parse_ternary_operation(Parser* parser) {
    ASTNode* condition = parser_parse_binary_operation(parser, 0);
    if (!condition) {
        return NULL;
    }

    /* `??` binds looser than any binary operator but tighter than `?:`. */
    while (parser->current_token.type == TOKEN_NULL_COALESCING) {
        int op_line = parser->current_token.line;
        int op_col = parser->current_token.column;
        parser_advance(parser);
        ASTNode* right = parser_parse_binary_operation(parser, 1);
        if (!right) {
            ast_destroy_node(condition);
            return NULL;
        }
        ASTNode* node = KrtParserCreateNode(parser, AST_NULL_COALESCING, op_line, op_col);
        if (!node) {
            ast_destroy_node(condition);
            ast_destroy_node(right);
            return NULL;
        }
        node->data.null_coalescing.left = condition;
        node->data.null_coalescing.right = right;
        condition = node;
    }

    if (parser->current_token.type != TOKEN_QUESTION) {
        return condition;
    }
    parser_advance(parser);

    ASTNode* then_expr = parser_parse_expression(parser);
    if (!then_expr) {
        ast_destroy_node(condition);
        return NULL;
    }

    if (parser->current_token.type != TOKEN_COLON) {
        ast_destroy_node(condition);
        ast_destroy_node(then_expr);
        return KrtParserFail(parser, parser->current_token.line, parser->current_token.column,
                             "expected ':' in conditional expression");
    }
    parser_advance(parser);

    ASTNode* else_expr = parser_parse_ternary_operation(parser);
    if (!else_expr) {
        ast_destroy_node(condition);
        ast_destroy_node(then_expr);
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_TERNARY_OPERATION, condition->line, condition->col);
    if (!node) {
        ast_destroy_node(condition);
        ast_destroy_node(then_expr);
        ast_destroy_node(else_expr);
        return NULL;
    }
    node->data.ternary_op.condition = condition;
    node->data.ternary_op.true_value = then_expr;
    node->data.ternary_op.false_value = else_expr;
    return node;
}

static ASTNode* parser_parse_unary_operation(Parser* parser) {
    return parser_parse_ternary_operation(parser);
}

ASTNode* parser_parse_expression(Parser* parser) {
    return parser_parse_unary_operation(parser);
}
