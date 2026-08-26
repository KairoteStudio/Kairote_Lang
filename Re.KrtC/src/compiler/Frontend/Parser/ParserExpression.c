#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"

ASTNode* parser_parse_expression(Parser* parser);

#define PARSER_ARGUMENT_CAPACITY_INIT 8

#define PARSER_LIKELY(x) (x)
#define PARSER_UNLIKELY(x) (x)

#define PARSER_MALLOC(size) KRT_MALLOC(size)
#define PARSER_REALLOC(ptr, size) KRT_REALLOC(ptr, size)
#define PARSER_FREE(ptr) KRT_FREE(ptr)

#define PARSER_ALLOC_FROM_ARENA(parser, size) KrtArenaAlloc((parser)->arena, size)

#define PARSER_CREATE_NODE(type, line, col) ast_create_node_arena(type, line, col, parser->arena)
#define PARSER_STRDUP(s) arena_strdup(parser->arena, s)

static char* arena_strdup(KrtArena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* result = (char*)KrtArenaAlloc(arena, len);
    if (result) memcpy(result, str, len);
    return result;
}

ASTNode* parser_parse_call(Parser* parser, ASTNode* callee) {
    parser_advance(parser);

    ASTNode** arguments = NULL;
    char** argument_names = NULL;
    int argument_count = 0;
    int capacity = 0;

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        capacity = PARSER_ARGUMENT_CAPACITY_INIT;
        arguments = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
        argument_names = (char**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(char*));
        if (PARSER_UNLIKELY(!arguments || !argument_names)) {
            ast_destroy_node(callee);
            return NULL;
        }

        while (1) {
            char* param_name = NULL;
            ASTNode* arg = NULL;

            if (parser->current_token.type == TOKEN_RIGHT_PAREN) break;

            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                Token next = lexer_peek_token(parser->lexer);
                KrtTokenType next_type = next.type;
                token_free(&next);

                if (next_type == TOKEN_COLON) {
                    param_name = PARSER_STRDUP(parser->current_token.value);
                    parser_advance(parser);
                    parser_advance(parser);
                    arg = parser_parse_expression(parser);
                    if (PARSER_UNLIKELY(!arg)) {
                        KRT_FREE(param_name);
                        goto cleanup_error;
                    }
                } else {
                    arg = parser_parse_expression(parser);
                    if (PARSER_UNLIKELY(!arg)) {
                        goto cleanup_error;
                    }
                }
            } else {
                arg = parser_parse_expression(parser);
                if (PARSER_UNLIKELY(!arg)) {
                    goto cleanup_error;
                }
            }

            if (argument_count >= capacity) {
                capacity *= 2;
                ASTNode** new_arguments = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
                char** new_argument_names = (char**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(char*));
                if (PARSER_UNLIKELY(!new_arguments || !new_argument_names)) {
                    KRT_FREE(param_name);
                    ast_destroy_node(arg);
                    goto cleanup_error;
                }
                memcpy(new_arguments, arguments, argument_count * sizeof(ASTNode*));
                memcpy(new_argument_names, argument_names, argument_count * sizeof(char*));
                arguments = new_arguments;
                argument_names = new_argument_names;
            }

            arguments[argument_count] = arg;
            argument_names[argument_count] = param_name;
            argument_count++;

            if (parser->current_token.type == TOKEN_RIGHT_PAREN) break;
            if (PARSER_UNLIKELY(parser->current_token.type != TOKEN_COMMA)) {
                goto cleanup_error;
            }
            parser_advance(parser);
        }
    }

    if (PARSER_UNLIKELY(parser->current_token.type != TOKEN_RIGHT_PAREN)) {
        goto cleanup_error;
    }
    parser_advance(parser);

    ASTNode* node = ast_create_node_arena(AST_CALL, parser->current_token.line, parser->current_token.column, parser->arena);
    if (PARSER_UNLIKELY(!node)) {
        goto cleanup_error;
    }

    if (callee->type == AST_IDENTIFIER) {
        node->data.call.name = PARSER_STRDUP(callee->data.identifier_name);
        node->data.call.object = NULL;
        ast_destroy_node(callee);
    } else if (callee->type == AST_MEMBER_ACCESS) {
        node->data.call.name = PARSER_STRDUP(callee->data.member_access.member_name);
        ASTNode* object = callee->data.member_access.object;
        callee->data.member_access.object = NULL;
        node->data.call.object = object;
        ast_destroy_node(callee);
    } else {
        node->data.call.name = PARSER_STRDUP("__expr_call__");
        node->data.call.object = callee;
    }
    node->data.call.arguments = arguments;
    node->data.call.argument_count = argument_count;
    node->data.call.argument_names = argument_names;
    return node;

cleanup_error:
    for (int i = 0; i < argument_count; i++) {
        ast_destroy_node(arguments[i]);
        if (argument_names && argument_names[i]) {
            KRT_FREE(argument_names[i]);
        }
    }
    ast_destroy_node(callee);
    return NULL;
}

ASTNode* parser_parse_primary(Parser* parser) {
    Token token = parser->current_token;

    switch (token.type) {
        case TOKEN_NUMBER: {
            parser_advance(parser);
            ASTNode* node = ast_create_node_arena(AST_NUMBER, token.line, token.column, parser->arena);
            if (!node) return NULL;
            if (token.value[0] == '0' && (token.value[1] == 'b' || token.value[1] == 'B')) {
                double bin = 0.0;
                for (const char* bp = token.value + 2; *bp == '0' || *bp == '1'; bp++) {
                    bin = bin * 2.0 + (double)(*bp - '0');
                }
                node->data.number_value = bin;
            } else {
                node->data.number_value = atof(token.value);
            }
            return node;
        }
        case TOKEN_FUNCTION: {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_LEFT_PAREN) return NULL;
            parser_advance(parser);

            char** params = NULL;
            int param_count = 0;
            int cap = 0;
            while (parser->current_token.type != TOKEN_RIGHT_PAREN &&
                   parser->current_token.type != TOKEN_EOF) {
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    if (param_count == cap) {
                        int ncap = cap ? cap * 2 : 4;
                        char** np = (char**)KrtArenaAlloc(parser->arena, sizeof(char*) * ncap);
                        if (!np) { params = NULL; break; }
                        for (int k = 0; k < param_count; k++) np[k] = params[k];
                        params = np; cap = ncap;
                    }
                    params[param_count++] = parser->current_token.value;
                }
                parser_advance(parser);
                if (parser->current_token.type == TOKEN_COMMA) {
                    parser_advance(parser);
                }
            }
            if (parser->current_token.type != TOKEN_RIGHT_PAREN) return NULL;
            parser_advance(parser);

            if (parser->current_token.type != TOKEN_LAMBDA) return NULL;
            parser_advance(parser);

            ASTNode* node = ast_create_node_arena(AST_LAMBDA_EXPRESSION, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.lambda_expr.parameters = params;
            node->data.lambda_expr.parameter_count = param_count;
            node->data.lambda_expr.expression = parser_parse_expression(parser);
            node->data.lambda_expr.body = NULL;
            if (!node->data.lambda_expr.expression) return NULL;
            return node;
        }
        case TOKEN_STRING: {
            parser_advance(parser);
            ASTNode* node = ast_create_node_arena(AST_STRING, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.string_value = PARSER_STRDUP(token.value);
            return node;
        }
        case TOKEN_CHAR_LITERAL: {
            parser_advance(parser);
            ASTNode* node = ast_create_node_arena(AST_CHAR_LITERAL, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.char_value = token.value ? token.value[0] : '\0';
            return node;
        }
        case TOKEN_IDENTIFIER: {
            parser_advance(parser);
            ASTNode* node = ast_create_node_arena(AST_IDENTIFIER, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.identifier_name = PARSER_STRDUP(token.value);
            return node;
        }
        case TOKEN_TRUE: {
            parser_advance(parser);
            ASTNode* node = ast_create_node_arena(AST_BOOLEAN, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.boolean_value = 1;
            return node;
        }
        case TOKEN_FALSE: {
            parser_advance(parser);
            ASTNode* node = ast_create_node_arena(AST_BOOLEAN, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.boolean_value = 0;
            return node;
        }
        case TOKEN_NULL: {
            parser_advance(parser);
            return ast_create_node_arena(AST_NULL, token.line, token.column, parser->arena);
        }
        case TOKEN_THIS: {
            parser_advance(parser);
            return ast_create_node_arena(AST_THIS, token.line, token.column, parser->arena);
        }
        case TOKEN_LEFT_PAREN: {
            Token next = lexer_peek_token(parser->lexer);
            bool is_cast = false;
            bool is_array_cast = false;

            if ((next.type >= TOKEN_INT8 && next.type <= TOKEN_VOID) ||
                next.type == TOKEN_TYPE_STRING) {
                Token third = lexer_peek_nth_token(parser->lexer, 2);
                if (third.type == TOKEN_RIGHT_PAREN) {
                    is_cast = true;
                } else if (third.type == TOKEN_MULTIPLY) {
                    int idx = 3;
                    Token scan = lexer_peek_nth_token(parser->lexer, idx);
                    while (scan.type == TOKEN_MULTIPLY) {
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
                while (parser->current_token.type == TOKEN_MULTIPLY) {
                    parser_advance(parser);
                }
                parser_advance(parser);
                ASTNode* expr = parser_parse_expression(parser);
                if (!expr) { token_free(&next); return NULL; }
                ASTNode* node = ast_create_node_arena(AST_CAST_EXPRESSION, token.line, token.column, parser->arena);
                if (!node) { ast_destroy_node(expr); token_free(&next); return NULL; }
                node->data.cast_expr.target_type = cast_type;
                node->data.cast_expr.expression = expr;
                token_free(&next);
                return node;
            }
            token_free(&next);

            parser_advance(parser);
            ASTNode* expr = parser_parse_expression(parser);
            if (!expr) return NULL;
            if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
                ast_destroy_node(expr);
                return NULL;
            }
            parser_advance(parser);
            return expr;
        }
        case TOKEN_LEFT_BRACKET: {
            parser_advance(parser);
            ASTNode** elements = NULL;
            int element_count = 0;
            int capacity = 8;

            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                elements = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
                if (!elements) return NULL;

                while (1) {
                    ASTNode* elem = parser_parse_expression(parser);
                    if (!elem) goto array_cleanup;

                    if (element_count >= capacity) {
                        capacity *= 2;
                        ASTNode** new_elems = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
                        if (!new_elems) { ast_destroy_node(elem); goto array_cleanup; }
                        memcpy(new_elems, elements, element_count * sizeof(ASTNode*));
                        elements = new_elems;
                    }
                    elements[element_count++] = elem;

                    if (parser->current_token.type == TOKEN_RIGHT_BRACKET) break;
                    if (parser->current_token.type != TOKEN_COMMA) goto array_cleanup;
                    parser_advance(parser);
                }
            }

            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) goto array_cleanup;
            parser_advance(parser);

            ASTNode* node = ast_create_node_arena(AST_ARRAY_LITERAL, token.line, token.column, parser->arena);
            if (!node) goto array_cleanup;
            node->data.array_literal.elements = elements;
            node->data.array_literal.element_count = element_count;
            return node;

array_cleanup:
            if (elements) {
                for (int i = 0; i < element_count; i++) ast_destroy_node(elements[i]);
            }
            return NULL;
        }
        case TOKEN_INT8: case TOKEN_INT16: case TOKEN_INT32: case TOKEN_INT64:
        case TOKEN_UINT8: case TOKEN_UINT16: case TOKEN_UINT32: case TOKEN_UINT64:
        case TOKEN_FLOAT32: case TOKEN_FLOAT64: case TOKEN_TYPE_STRING:
        case TOKEN_CHAR: case TOKEN_BOOL: case TOKEN_VOID: {
            KrtTokenType type = token.type;
            parser_advance(parser);

            if (parser->current_token.type == TOKEN_LEFT_PAREN) {
                parser_advance(parser);
                ASTNode* expr = parser_parse_expression(parser);
                if (!expr) return NULL;
                if (parser->current_token.type != TOKEN_RIGHT_PAREN) { ast_destroy_node(expr); return NULL; }
                parser_advance(parser);

                ASTNode* node = ast_create_node_arena(AST_CAST_EXPRESSION, token.line, token.column, parser->arena);
                if (!node) { ast_destroy_node(expr); return NULL; }
                node->data.cast_expr.target_type = type;
                node->data.cast_expr.expression = expr;
                return node;
            }

            ASTNode* node = ast_create_node_arena(AST_GENERIC_TYPE, token.line, token.column, parser->arena);
            if (!node) return NULL;
            node->data.generic_type.type_name = PARSER_STRDUP(token.value);
            return node;
        }
        case TOKEN_NEW: {
            parser_advance(parser);

            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                char* class_name = PARSER_STRDUP(parser->current_token.value);
                parser_advance(parser);
                if (parser->current_token.type == TOKEN_LESS) {
                    int depth = 0;
                    do {
                        if (parser->current_token.type == TOKEN_LESS) depth++;
                        else if (parser->current_token.type == TOKEN_GREATER) depth--;
                        parser_advance(parser);
                    } while (depth > 0 && parser->current_token.type != TOKEN_EOF);
                }

                ASTNode** args = NULL;
                int arg_count = 0;
                int arg_capacity = 8;

                if (parser->current_token.type == TOKEN_LEFT_PAREN) {
                    parser_advance(parser);
                    args = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, sizeof(ASTNode*) * arg_capacity);
                    if (!args) return NULL;

                    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
                        while (1) {
                            ASTNode* arg = parser_parse_expression(parser);
                            if (!arg) { return NULL; }
                            if (arg_count >= arg_capacity) {
                                arg_capacity *= 2;
                                ASTNode** new_args = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, sizeof(ASTNode*) * arg_capacity);
                                if (!new_args) { return NULL; }
                                memcpy(new_args, args, arg_count * sizeof(ASTNode*));
                                args = new_args;
                            }
                            args[arg_count++] = arg;
                            if (parser->current_token.type == TOKEN_COMMA) { parser_advance(parser); continue; }
                            break;
                        }
                    }
                    if (parser->current_token.type != TOKEN_RIGHT_PAREN) { return NULL; }
                    parser_advance(parser);
                }

                ASTNode* node = ast_create_node_arena(AST_NEW_EXPRESSION, token.line, token.column, parser->arena);
                if (!node) return NULL;
                node->data.new_expr.class_name = class_name;
                node->data.new_expr.arguments = args;
                node->data.new_expr.argument_count = arg_count;
                node->data.new_expr.argument_names = NULL;
                node->data.new_expr.type_token = TOKEN_IDENTIFIER;
                return node;
            }

            if (parser_is_type_keyword(parser->current_token.type)) {
                KrtTokenType elem_type = parser->current_token.type;
                char* elem_name = PARSER_STRDUP(parser->current_token.value ? parser->current_token.value : "");
                parser_advance(parser);

                if (parser->current_token.type != TOKEN_LEFT_BRACKET) return NULL;
                parser_advance(parser);
                ASTNode* size = parser_parse_expression(parser);
                if (!size) return NULL;
                if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                    ast_destroy_node(size);
                    return NULL;
                }
                parser_advance(parser);

                ASTNode* node = ast_create_node_arena(AST_NEW_ARRAY_EXPRESSION, token.line, token.column, parser->arena);
                if (!node) { ast_destroy_node(size); return NULL; }
                node->data.new_array_expr.type_token = elem_type;
                node->data.new_array_expr.element_type = elem_name;
                node->data.new_array_expr.size = size;
                return node;
            }

            return NULL;
        }
        default:
            return NULL;
    }
}

int get_operator_precedence(KrtTokenType type) {
    switch (type) {
        case TOKEN_OR: return 1;
        case TOKEN_AND: return 2;
        case TOKEN_BITWISE_OR:
        case TOKEN_PIPE: return 3;
        case TOKEN_BITWISE_XOR: return 4;
        case TOKEN_BITWISE_AND: return 5;
        case TOKEN_EQUAL: case TOKEN_NOT_EQUAL: return 6;
        case TOKEN_LESS: case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL: case TOKEN_GREATER_EQUAL: return 7;
        case TOKEN_LSHIFT: case TOKEN_RSHIFT: return 8;
        case TOKEN_PLUS: case TOKEN_MINUS: return 9;
        case TOKEN_MULTIPLY: case TOKEN_DIVIDE: case TOKEN_MODULO: return 10;
        case TOKEN_NOT: return 11;
        default: return 0;
    }
}

static ASTNode* parser_parse_unary_prefix(Parser* parser) {
    KrtTokenType op = parser->current_token.type;
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    ASTNode* operand = parser_parse_binary_operation(parser, 11);
    if (!operand) return NULL;
    ASTNode* node = ast_create_node_arena(AST_UNARY_OPERATION, line, col, parser->arena);
    if (!node) { ast_destroy_node(operand); return NULL; }
    node->data.unary_op.operator = op;
    node->data.unary_op.operand = operand;
    return node;
}

ASTNode* parser_parse_binary_operation(Parser* parser, int precedence) {
    ASTNode* left;
    if (parser->current_token.type == TOKEN_MINUS ||
        parser->current_token.type == TOKEN_NOT) {
        left = parser_parse_unary_prefix(parser);
    } else {
        left = parser_parse_postfix_expression(parser);
    }
    if (!left) return NULL;

    int prec = get_operator_precedence(parser->current_token.type);
    while (prec > 0 && prec >= precedence) {
        KrtTokenType op = parser->current_token.type;
        int op_line = parser->current_token.line;
        int op_col = parser->current_token.column;
        parser_advance(parser);

        ASTNode* right;
        if (parser->current_token.type == TOKEN_MINUS ||
            parser->current_token.type == TOKEN_NOT) {
            right = parser_parse_unary_prefix(parser);
        } else {
            right = parser_parse_binary_operation(parser, get_operator_precedence(op) + 1);
        }
        if (!right) { ast_destroy_node(left); return NULL; }

        ASTNode* node = ast_create_node_arena(AST_BINARY_OPERATION, op_line, op_col, parser->arena);
        if (!node) { ast_destroy_node(left); ast_destroy_node(right); return NULL; }
        node->data.binary_op.left = left;
        node->data.binary_op.right = right;
        node->data.binary_op.operator = op;
        left = node;
        
        prec = get_operator_precedence(parser->current_token.type);
    }

    return left;
}

ASTNode* parser_parse_postfix_expression(Parser* parser) {
    ASTNode* primary = parser_parse_primary(parser);
    if (!primary) return NULL;

    while (1) {
        if (parser->current_token.type == TOKEN_DOUBLE_COLON &&
            primary->type == AST_IDENTIFIER &&
            strcmp(primary->data.identifier_name, "global") == 0) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_IDENTIFIER) {
                parser_report_error(parser,
                                    parser->current_token.line,
                                    parser->current_token.column,
                                    "expected identifier after 'global::'");
                ast_destroy_node(primary);
                return NULL;
            }
            char tmp[512];
            snprintf(tmp, sizeof(tmp), "global::%s", parser->current_token.value);
            primary->data.identifier_name = PARSER_STRDUP(tmp);
            parser_advance(parser);
        } else if (parser->current_token.type == TOKEN_DOUBLE_COLON) {
            parser_report_error(parser,
                                parser->current_token.line,
                                parser->current_token.column,
                                "'::' is only valid as 'global::' root-scope escape; "
                                "use '.' for member access");
            ast_destroy_node(primary);
            return NULL;
        } else if (parser->current_token.type == TOKEN_DOT) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_IDENTIFIER) { ast_destroy_node(primary); return NULL; }

            ASTNode* member = ast_create_node_arena(AST_MEMBER_ACCESS,
                                             primary->line, primary->col, parser->arena);
            if (!member) { ast_destroy_node(primary); return NULL; }
            member->data.member_access.object = primary;
            member->data.member_access.member_name = PARSER_STRDUP(parser->current_token.value);
            primary = member;
            parser_advance(parser);
        } else if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
            parser_advance(parser);
            ASTNode* index = parser_parse_expression(parser);
            if (!index) { ast_destroy_node(primary); return NULL; }
            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                ast_destroy_node(index); ast_destroy_node(primary); return NULL;
            }
            parser_advance(parser);

            ASTNode* access = ast_create_node_arena(AST_ARRAY_ACCESS,
                                              primary->line, primary->col, parser->arena);
            if (!access) { ast_destroy_node(index); ast_destroy_node(primary); return NULL; }
            access->data.array_access.array = primary;
            access->data.array_access.index = index;
            primary = access;
        } else if (parser->current_token.type == TOKEN_LEFT_PAREN) {
            ASTNode* call = parser_parse_call(parser, primary);
            if (!call) return NULL;
            primary = call;
        } else {
            break;
        }
    }

    return primary;
}

ASTNode* parser_parse_ternary_operation(Parser* parser) {
    ASTNode* condition = parser_parse_binary_operation(parser, 0);
    if (!condition) return NULL;

    if (parser->current_token.type != TOKEN_QUESTION) return condition;
    parser_advance(parser);

    ASTNode* then_expr = parser_parse_expression(parser);
    if (!then_expr) { ast_destroy_node(condition); return NULL; }

    if (parser->current_token.type != TOKEN_COLON) {
        ast_destroy_node(condition); ast_destroy_node(then_expr); return NULL;
    }
    parser_advance(parser);

    ASTNode* else_expr = parser_parse_ternary_operation(parser);
    if (!else_expr) { ast_destroy_node(condition); ast_destroy_node(then_expr); return NULL; }

    ASTNode* node = ast_create_node_arena(AST_TERNARY_OPERATION,
                                   condition->line, condition->col, parser->arena);
    if (!node) { ast_destroy_node(condition); ast_destroy_node(then_expr); ast_destroy_node(else_expr); return NULL; }
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