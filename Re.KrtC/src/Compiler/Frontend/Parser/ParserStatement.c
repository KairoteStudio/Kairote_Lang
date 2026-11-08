#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Accelerator.h"
#include "ParserBase.h"
#include "ParserExpression.h"

ASTNode* parser_parse_statement(Parser* parser);
ASTNode* parser_parse_block(Parser* parser);
ASTNode* parser_parse_namespace_declaration(Parser* parser);
ASTNode* parser_parse_class_declaration(Parser* parser);

#define KRT_PARSER_ARGUMENT_CAPACITY_INIT 8

#define KRT_PARSER_MALLOC(size) KRT_MALLOC(size)

#define KRT_PARSER_ALLOC_FROM_ARENA(parser, size) KrtParserAlloc(parser, size)

#define KRT_PARSER_CREATE_NODE(type, line, col) KrtParserCreateNode(parser, type, line, col)
#define KRT_PARSER_STRDUP(s) KrtParserStrdup(parser, s)

#include "EnumParser.inc"

static ASTNode* parser_parse_function_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    KrtSourceType return_source;
    if (!KrtParseSourceType(parser, &return_source)) {
        return NULL;
    }
    KrtTokenType return_type = KrtSourceStorage(return_source);

    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    char* func_name = KrtParserStrdup(parser, parser->current_token.value);
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    char** param_names = NULL;
    KrtTokenType* param_types = NULL;
    int* param_is_params = NULL;
    int* param_is_array = NULL;
    int param_count = 0;
    int param_capacity = 8;
    KrtSourceType* source_params = KrtParserAlloc(parser, param_capacity * sizeof(*source_params));

    param_names = (char**)KrtParserAlloc(parser, param_capacity * sizeof(char*));
    param_types = (KrtTokenType*)KrtParserAlloc(parser, param_capacity * sizeof(KrtTokenType));
    param_is_params = (int*)KrtParserAlloc(parser, param_capacity * sizeof(int));
    param_is_array = (int*)KrtParserAlloc(parser, param_capacity * sizeof(int));

    while (parser->current_token.type != TOKEN_RIGHT_PAREN && parser->current_token.type != TOKEN_EOF) {
        if (param_count >= param_capacity) {
            if (param_capacity > INT_MAX / 2) {
                KRT_COMPILE_ERROR("Parser collection exceeds supported size");
            }
            param_capacity *= 2;
            KrtSourceType* new_source = KrtParserAlloc(parser, param_capacity * sizeof(*new_source));
            memcpy(new_source, source_params, param_count * sizeof(*new_source));
            source_params = new_source;
            char** new_names = (char**)KrtParserAlloc(parser, param_capacity * sizeof(char*));
            KrtTokenType* new_types = (KrtTokenType*)KrtParserAlloc(parser, param_capacity * sizeof(KrtTokenType));
            int* new_is_params = (int*)KrtParserAlloc(parser, param_capacity * sizeof(int));
            int* new_is_array = (int*)KrtParserAlloc(parser, param_capacity * sizeof(int));
            memcpy(new_names, param_names, param_count * sizeof(char*));
            memcpy(new_types, param_types, param_count * sizeof(KrtTokenType));
            memcpy(new_is_params, param_is_params, param_count * sizeof(int));
            memcpy(new_is_array, param_is_array, param_count * sizeof(int));
            param_names = new_names;
            param_types = new_types;
            param_is_params = new_is_params;
            param_is_array = new_is_array;
        }

        if (!KrtParseSourceType(parser, &source_params[param_count])) {
            return NULL;
        }
        KrtTokenType ptype = KrtSourceStorage(source_params[param_count]);

        int is_array_param = 0;
        if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                return NULL;
            }
            parser_advance(parser);
            is_array_param = 1;
        }

        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            return NULL;
        }

        param_names[param_count] = KrtParserStrdup(parser, parser->current_token.value);
        param_types[param_count] = ptype;
        param_is_params[param_count] = 1;
        param_is_array[param_count] = is_array_param;
        param_count++;

        parser_advance(parser);

        if (parser->current_token.type == TOKEN_COMMA) {
            parser_advance(parser);
        }
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* body = NULL;
    if (parser->current_token.type == TOKEN_LEFT_BRACE) {
        body = parser_parse_block(parser);
    } else {
        return NULL;
    }

    if (!body) {
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_FUNCTION_DECLARATION, line, col);
    if (!node) {
        return NULL;
    }

    node->data.function_decl.name = func_name;
    node->data.function_decl.parameters = param_names;
    node->data.function_decl.parameter_count = param_count;
    node->data.function_decl.parameter_types = param_types;
    node->data.function_decl.parameter_is_params = param_is_params;
    node->data.function_decl.parameter_is_nullable = NULL;
    node->data.function_decl.parameter_is_array = param_is_array;
    node->data.function_decl.parameter_default_values = NULL;
    node->data.function_decl.body = body;
    node->data.function_decl.return_type = return_type;
    node->data.function_decl.is_async = 0;
    node->function_type = KrtParserAlloc(parser, sizeof(*node->function_type));
    *node->function_type = (KrtFunctionType){return_source, source_params, param_count};

    return node;
}

static ASTNode* parser_parse_declaration_with_modifiers(Parser* parser) {
    int is_static = 0;
    int is_const = 0;
    while (parser->current_token.type == TOKEN_PUBLIC || parser->current_token.type == TOKEN_PRIVATE ||
           parser->current_token.type == TOKEN_PROTECTED || parser->current_token.type == TOKEN_STATIC ||
           parser->current_token.type == TOKEN_CONST || parser->current_token.type == TOKEN_EXTERN) {
        if (parser->current_token.type == TOKEN_STATIC) {
            is_static = 1;
        } else if (parser->current_token.type == TOKEN_CONST) {
            is_const = 1;
        }
        parser_advance(parser);
    }

    if (parser->current_token.type == TOKEN_CLASS) {
        return parser_parse_class_declaration(parser);
    }

    ASTNode* declaration = parser_parse_statement(parser);
    if (is_static && declaration && declaration->type == AST_FUNCTION_DECLARATION) {
        declaration->type = AST_STATIC_FUNCTION_DECLARATION;
    } else if ((is_static || is_const) && declaration && declaration->type == AST_VARIABLE_DECLARATION) {
        declaration->type = AST_STATIC_VARIABLE_DECLARATION;
    }
    return declaration;
}

static ASTNode* parser_parse_variable_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    int is_let = (parser->current_token.type == TOKEN_LET) ? 1 : 0;
    parser_advance(parser);

    KrtTokenType type_token = TOKEN_UNKNOWN;
    char* type_name = NULL;

    int pointer_depth = 0;

    int declared_by_keyword = 0;
    if (parser_is_type_keyword(parser->current_token.type)) {
        type_token = parser->current_token.type;
        declared_by_keyword = 1;
        parser_advance(parser);

        while ((parser->current_token.type == TOKEN_MULTIPLY || parser->current_token.type == TOKEN_POWER)) {
            pointer_depth += parser->current_token.type == TOKEN_POWER ? 2 : 1;
            parser_advance(parser);
        }
    } else if (parser->current_token.type == TOKEN_IDENTIFIER) {
        type_token = TOKEN_IDENTIFIER;
        type_name = KRT_STRDUP(parser->current_token.value);
        if (!type_name) {
            return NULL;
        }
        parser_advance(parser);

        while ((parser->current_token.type == TOKEN_MULTIPLY || parser->current_token.type == TOKEN_POWER)) {
            pointer_depth += parser->current_token.type == TOKEN_POWER ? 2 : 1;
            parser_advance(parser);
        }
    } else {
        return NULL;
    }

    char* name = NULL;
    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        name = KRT_STRDUP(parser->current_token.value);
        parser_advance(parser);
    } else if (parser->current_token.type >= TOKEN_OP_ADDITION && parser->current_token.type <= TOKEN_OP_EXPLICIT) {
        const char* op_name = token_type_to_string(parser->current_token.type);
        name = KRT_STRDUP(op_name ? op_name : "op_Unknown");
        parser_advance(parser);
    } else {
        KRT_FREE(type_name);
        return NULL;
    }

    if (!name) {
        KRT_FREE(type_name);
        return NULL;
    }

    ASTNode* array_size = NULL;
    bool is_array = false;

    if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
        parser_advance(parser);
        is_array = true;
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            array_size = parser_parse_expression(parser);
            if (!array_size) {
                KRT_FREE(name);
                KRT_FREE(type_name);
                return NULL;
            }
        }
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) {
                ast_destroy_node(array_size);
            }
            return NULL;
        }
        parser_advance(parser);
    }

    ASTNode* value = NULL;
    if (parser->current_token.type == TOKEN_ASSIGN) {
        parser_advance(parser);
        value = parser_parse_expression(parser);
        if (!value) {
            if (declared_by_keyword) {
                parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                    "expected expression after '=' in declaration of '%s'", name ? name : "?");
            }
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) {
                ast_destroy_node(array_size);
            }
            return NULL;
        }
    }

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        if (declared_by_keyword) {
            parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                "expected ';' after declaration of '%s'", name ? name : "?");
        }
        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) {
            ast_destroy_node(value);
        }
        if (array_size) {
            ast_destroy_node(array_size);
        }
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = KrtParserCreateNode(parser, AST_VARIABLE_DECLARATION, line, col);
    if (!node) {
        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) {
            ast_destroy_node(value);
        }
        if (array_size) {
            ast_destroy_node(array_size);
        }
        return NULL;
    }

    node->data.variable_decl.name = name;
    node->data.variable_decl.value = value;
    node->data.variable_decl.type = type_token;
    node->data.variable_decl.template_instantiation_type = type_name;
    node->data.variable_decl.array_size = array_size;
    node->data.variable_decl.is_array = is_array;
    node->data.variable_decl.is_let = is_let;
    node->data.variable_decl.pointer_depth = pointer_depth;
    node->declared_type =
        (KrtSourceType){.token = node->data.variable_decl.type, .pointer_depth = pointer_depth, .type_name = type_name};
    if (pointer_depth) {
        node->data.variable_decl.type = TOKEN_UINT64;
    }

    return node;
}

static ASTNode* parser_parse_variable_declaration_with_type(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    KrtSourceType source;
    if (!KrtParseSourceType(parser, &source)) {
        return NULL;
    }
    KrtTokenType type_token = KrtSourceStorage(source);
    char* type_name = source.type_name;
    int pointer_depth = source.pointer_depth;

    bool is_array = false;
    if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            KRT_FREE(type_name);
            return NULL;
        }
        parser_advance(parser);
        is_array = true;
    }

    char* name = NULL;
    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        name = KrtParserStrdup(parser, parser->current_token.value);
        parser_advance(parser);
    } else if (parser->current_token.type >= TOKEN_OP_ADDITION && parser->current_token.type <= TOKEN_OP_EXPLICIT) {
        const char* op_name = token_type_to_string(parser->current_token.type);
        name = KrtParserStrdup(parser, op_name ? op_name : "op_Unknown");
        parser_advance(parser);
    } else {
        KRT_FREE(type_name);
        return NULL;
    }

    if (!name) {
        KRT_FREE(type_name);
        return NULL;
    }

    ASTNode* array_size = NULL;

    if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
        parser_advance(parser);
        is_array = true;
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            array_size = parser_parse_expression(parser);
            if (!array_size) {
                KRT_FREE(name);
                KRT_FREE(type_name);
                return NULL;
            }
        }
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) {
                ast_destroy_node(array_size);
            }
            return NULL;
        }
        parser_advance(parser);
    }

    ASTNode* value = NULL;
    if (parser->current_token.type == TOKEN_ASSIGN) {
        parser_advance(parser);
        value = parser_parse_expression(parser);
        if (!value) {
            parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                "expected expression after '=' in declaration");
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) {
                ast_destroy_node(array_size);
            }
            return NULL;
        }
    }

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected ';' after declaration");

        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) {
            ast_destroy_node(value);
        }
        if (array_size) {
            ast_destroy_node(array_size);
        }
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = KrtParserCreateNode(parser, AST_VARIABLE_DECLARATION, line, col);
    if (!node) {
        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) {
            ast_destroy_node(value);
        }
        if (array_size) {
            ast_destroy_node(array_size);
        }
        return NULL;
    }

    node->data.variable_decl.name = name;
    node->data.variable_decl.value = value;
    node->data.variable_decl.type = type_token;
    node->data.variable_decl.template_instantiation_type = type_name;
    node->data.variable_decl.array_size = array_size;
    node->data.variable_decl.is_array = is_array;
    node->data.variable_decl.is_let = 0;
    node->data.variable_decl.pointer_depth = pointer_depth;
    node->declared_type = source;
    if (pointer_depth) {
        node->data.variable_decl.type = TOKEN_UINT64;
    }

    return node;
}

static ASTNode* parser_parse_type_inferred_declaration(Parser* parser) {
    ASTNode* node =
        KRT_PARSER_CREATE_NODE(AST_VARIABLE_DECLARATION, parser->current_token.line, parser->current_token.column);
    node->data.variable_decl.is_let = parser->current_token.type == TOKEN_LET;
    parser_advance(parser);
    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        return NULL;
    }
    node->data.variable_decl.name = KRT_PARSER_STRDUP(parser->current_token.value);
    node->data.variable_decl.type = TOKEN_AUTO;
    parser_advance(parser);
    if (parser->current_token.type == TOKEN_COLON) {
        parser_advance(parser);
        if (!KrtParseSourceType(parser, &node->declared_type)) {
            return NULL;
        }
        node->data.variable_decl.type = KrtSourceStorage(node->declared_type);
        node->data.variable_decl.pointer_depth = node->declared_type.pointer_depth;
        node->data.variable_decl.is_nullable = node->declared_type.nullable;
    }
    if (parser->current_token.type == TOKEN_ASSIGN) {
        parser_advance(parser);
        node->data.variable_decl.value = parser_parse_expression(parser);
        if (!node->data.variable_decl.value) {
            goto invalid;
        }
    } else if (node->data.variable_decl.type == TOKEN_AUTO || node->data.variable_decl.is_let) {
        goto invalid;
    }
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        goto invalid;
    }
    parser_advance(parser);
    return node;
invalid:
    parser_report_error(parser, node->line, node->col, "expected variable initializer and ';'");
    return NULL;
}

static ASTNode* parser_parse_assignment_from_left(Parser* parser, ASTNode* left) {
    if (!left) {
        return NULL;
    }

    KrtTokenType operator = parser->current_token.type;
    if (operator != TOKEN_ASSIGN && !KrtTokenIsCompoundAssignment(operator)) {
        return left;
    }

    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    ASTNode* value = parser_parse_expression(parser);
    if (!value) {
        parser_report_error(parser, line, col, "Expected an assignment value");
        ast_destroy_node(left);
        return NULL;
    }

    if (left->type == AST_POINTER_DEREFERENCE) {
        ASTNode* node = KRT_PARSER_CREATE_NODE(AST_POINTER_ASSIGNMENT, line, col);
        node->data.pointer_assignment.pointer = left->data.pointer_deref.pointer;
        node->data.pointer_assignment.value = value;
        node->data.pointer_assignment.operator = operator;
        return node;
    }
    if (left->type == AST_ARRAY_ACCESS) {
        ASTNode* node = (operator == TOKEN_ASSIGN) ? KRT_PARSER_CREATE_NODE(AST_ARRAY_ASSIGNMENT, line, col)
                                                   : KRT_PARSER_CREATE_NODE(AST_ARRAY_COMPOUND_ASSIGNMENT, line, col);
        if (!node) {
            ast_destroy_node(left);
            ast_destroy_node(value);
            return NULL;
        }

        if (operator == TOKEN_ASSIGN) {
            node->data.array_assignment.array = left->data.array_access.array;
            node->data.array_assignment.index = left->data.array_access.index;
            node->data.array_assignment.value = value;
        } else {
            node->data.array_compound_assignment.array = left->data.array_access.array;
            node->data.array_compound_assignment.index = left->data.array_access.index;
            node->data.array_compound_assignment.value = value;
            node->data.array_compound_assignment.operator = operator;
        }
        KRT_FREE(left);
        return node;
    } else if (left->type == AST_IDENTIFIER) {
        ASTNode* node = (operator == TOKEN_ASSIGN) ? KrtParserCreateNode(parser, AST_ASSIGNMENT, line, col)
                                                   : KrtParserCreateNode(parser, AST_COMPOUND_ASSIGNMENT, line, col);
        if (!node) {
            ast_destroy_node(left);
            ast_destroy_node(value);
            return NULL;
        }

        if (operator == TOKEN_ASSIGN) {
            node->data.assignment.name = KRT_STRDUP(left->data.identifier_name);
            node->data.assignment.value = value;
        } else {
            node->data.compound_assignment.name = KRT_STRDUP(left->data.identifier_name);
            node->data.compound_assignment.value = value;
            node->data.compound_assignment.operator = operator;
        }
        ast_destroy_node(left);
        return node;
    } else if (left->type == AST_MEMBER_ACCESS) {
        if (operator == TOKEN_ASSIGN) {
            ASTNode* node = KrtParserCreateNode(parser, AST_BINARY_OPERATION, line, col);
            if (!node) {
                ast_destroy_node(left);
                ast_destroy_node(value);
                return NULL;
            }
            node->data.binary_op.left = left;
            node->data.binary_op.operator = operator;
            node->data.binary_op.right = value;
            return node;
        }
    }

    parser_report_error(parser, line, col, "Unsupported assignment target");
    ast_destroy_node(left);
    ast_destroy_node(value);
    return NULL;
}

ASTNode* parser_parse_return_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    ASTNode* value = NULL;
    if (parser->current_token.type != TOKEN_SEMICOLON && parser->current_token.type != TOKEN_RIGHT_BRACE &&
        parser->current_token.type != TOKEN_EOF) {
        value = parser_parse_expression(parser);
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_RETURN_STATEMENT, line, col);
    if (!node) {
        if (value) {
            ast_destroy_node(value);
        }
        return NULL;
    }
    node->data.return_stmt.value = value;
    return node;
}

ASTNode* parser_parse_print_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode** values =
        (ASTNode**)KRT_PARSER_ALLOC_FROM_ARENA(parser, sizeof(ASTNode*) * KRT_PARSER_ARGUMENT_CAPACITY_INIT);
    int count = 0, capacity = KRT_PARSER_ARGUMENT_CAPACITY_INIT;

    while (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        if (count >= capacity) {
            capacity *= 2;
            values = (ASTNode**)KRT_REALLOC(values, capacity * sizeof(ASTNode*));
            if (!values) {
                return NULL;
            }
        }

        values[count++] = parser_parse_expression(parser);
        if (!values[count - 1]) {
            break;
        }

        if (parser->current_token.type == TOKEN_COMMA) {
            parser_advance(parser);
        } else if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            break;
        }
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        for (int i = 0; i < count; i++) {
            ast_destroy_node(values[i]);
        }
        KRT_FREE(values);
        return NULL;
    }
    parser_advance(parser);

    bool has_newline = true;
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        for (int i = 0; i < count; i++) {
            ast_destroy_node(values[i]);
        }
        KRT_FREE(values);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = KrtParserCreateNode(parser, AST_PRINT_STATEMENT, line, col);
    if (!node) {
        for (int i = 0; i < count; i++) {
            ast_destroy_node(values[i]);
        }
        KRT_FREE(values);
        return NULL;
    }
    node->data.print_stmt.values = values;
    node->data.print_stmt.value_count = count;
    node->data.print_stmt.has_newline = has_newline;
    return node;
}

static ASTNode* parser_parse_block_or_statement(Parser* parser) {
    if (parser->current_token.type == TOKEN_LEFT_BRACE) {
        return parser_parse_block(parser);
    }
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    ASTNode* stmt = parser_parse_statement(parser);
    if (!stmt) {
        return NULL;
    }

    ASTNode** statements = (ASTNode**)KRT_PARSER_MALLOC(sizeof(ASTNode*) * 1);
    if (!statements) {
        ast_destroy_node(stmt);
        return NULL;
    }
    statements[0] = stmt;

    ASTNode* node = KrtParserCreateNode(parser, AST_BLOCK, line, col);
    if (!node) {
        ast_destroy_node(statements[0]);
        KRT_FREE(statements);
        return NULL;
    }
    node->data.block.statements = statements;
    node->data.block.statement_count = 1;
    return node;
}

ASTNode* parser_parse_if_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* condition = parser_parse_expression(parser);
    if (!condition) {
        return NULL;
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        ast_destroy_node(condition);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* then_branch = parser_parse_block_or_statement(parser);
    if (!then_branch) {
        ast_destroy_node(condition);
        return NULL;
    }

    ASTNode* else_branch = NULL;
    if (parser->current_token.type == TOKEN_ELSE) {
        parser_advance(parser);
        if (parser->current_token.type == TOKEN_IF) {
            else_branch = parser_parse_if_statement(parser);
        } else {
            else_branch = parser_parse_block_or_statement(parser);
        }
        if (!else_branch) {
            ast_destroy_node(condition);
            ast_destroy_node(then_branch);
            return NULL;
        }
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_IF_STATEMENT, line, col);
    if (!node) {
        ast_destroy_node(condition);
        ast_destroy_node(then_branch);
        if (else_branch) {
            ast_destroy_node(else_branch);
        }
        return NULL;
    }
    node->data.if_stmt.condition = condition;
    node->data.if_stmt.then_branch = then_branch;
    node->data.if_stmt.else_branch = else_branch;
    return node;
}

ASTNode* parser_parse_switch_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected '(' after 'switch'");
        return NULL;
    }
    parser_advance(parser);

    ASTNode* expr = parser_parse_expression(parser);
    if (!expr) {
        return NULL;
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected ')' after switch expression");
        ast_destroy_node(expr);
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected '{' to open switch body");
        ast_destroy_node(expr);
        return NULL;
    }
    parser_advance(parser);

    ASTNode** cases = NULL;
    int case_count = 0, cap = 0;
    ASTNode* default_case = NULL;

    while (parser->current_token.type != TOKEN_RIGHT_BRACE && parser->current_token.type != TOKEN_EOF) {
        if (parser->current_token.type == TOKEN_CASE) {
            int cline = parser->current_token.line, ccol = parser->current_token.column;
            parser_advance(parser);
            ASTNode* value = parser_parse_expression(parser);
            if (!value) {
                goto cleanup;
            }
            if (parser->current_token.type != TOKEN_COLON) {
                parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                    "expected ':' after case value");
                ast_destroy_node(value);
                goto cleanup;
            }
            parser_advance(parser);

            ASTNode** stmts = NULL;
            int sc = 0, scap = 0;
            while (parser->current_token.type != TOKEN_CASE && parser->current_token.type != TOKEN_DEFAULT &&
                   parser->current_token.type != TOKEN_RIGHT_BRACE && parser->current_token.type != TOKEN_EOF) {
                ASTNode* st = parser_parse_statement(parser);
                if (!st) {
                    if (parser->current_token.type != TOKEN_EOF && parser->current_token.type != TOKEN_RIGHT_BRACE) {
                        parser_advance(parser);
                    }
                    continue;
                }
                if (sc >= scap) {
                    scap = scap ? scap * 2 : 4;
                    stmts = (ASTNode**)KRT_REALLOC(stmts, scap * sizeof(ASTNode*));
                }
                stmts[sc++] = st;
            }
            ASTNode* clause = KrtParserCreateNode(parser, AST_CASE_CLAUSE, cline, ccol);
            if (!clause) {
                ast_destroy_node(value);
                for (int k = 0; k < sc; k++) {
                    ast_destroy_node(stmts[k]);
                }
                KRT_FREE(stmts);
                goto cleanup;
            }
            clause->data.case_clause.value = value;
            clause->data.case_clause.statements = stmts;
            clause->data.case_clause.statement_count = sc;
            if (case_count >= cap) {
                cap = cap ? cap * 2 : 4;
                cases = (ASTNode**)KRT_REALLOC(cases, cap * sizeof(ASTNode*));
            }
            cases[case_count++] = clause;
        } else if (parser->current_token.type == TOKEN_DEFAULT) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_COLON) {
                parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                    "expected ':' after 'default'");
                goto cleanup;
            }
            parser_advance(parser);

            ASTNode** stmts = NULL;
            int sc = 0, scap = 0;
            while (parser->current_token.type != TOKEN_CASE && parser->current_token.type != TOKEN_DEFAULT &&
                   parser->current_token.type != TOKEN_RIGHT_BRACE && parser->current_token.type != TOKEN_EOF) {
                ASTNode* st = parser_parse_statement(parser);
                if (!st) {
                    if (parser->current_token.type != TOKEN_EOF && parser->current_token.type != TOKEN_RIGHT_BRACE) {
                        parser_advance(parser);
                    }
                    continue;
                }
                if (sc >= scap) {
                    scap = scap ? scap * 2 : 4;
                    stmts = (ASTNode**)KRT_REALLOC(stmts, scap * sizeof(ASTNode*));
                }
                stmts[sc++] = st;
            }
            ASTNode* blk = KrtParserCreateNode(parser, AST_BLOCK, line, col);
            if (!blk) {
                for (int k = 0; k < sc; k++) {
                    ast_destroy_node(stmts[k]);
                }
                KRT_FREE(stmts);
                goto cleanup;
            }
            blk->data.block.statements = stmts;
            blk->data.block.statement_count = sc;
            default_case = blk;
        } else {
            parser_advance(parser);
        }
    }

    if (parser->current_token.type != TOKEN_RIGHT_BRACE) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected '}' to close switch body");
        goto cleanup;
    }
    parser_advance(parser);

    ASTNode* node = KrtParserCreateNode(parser, AST_SWITCH_STATEMENT, line, col);
    if (!node) {
        goto cleanup;
    }
    node->data.switch_stmt.expression = expr;
    node->data.switch_stmt.cases = cases;
    node->data.switch_stmt.case_count = case_count;
    node->data.switch_stmt.default_case = default_case;
    return node;

cleanup:
    ast_destroy_node(expr);
    for (int k = 0; k < case_count; k++) {
        ast_destroy_node(cases[k]);
    }
    if (cases) {
        KRT_FREE(cases);
    }
    ast_destroy_node(default_case);
    return NULL;
}

ASTNode* parser_parse_do_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    ASTNode* body = parser_parse_block_or_statement(parser);
    if (!body) {
        return NULL;
    }

    if (parser->current_token.type != TOKEN_WHILE) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected 'while' after 'do' body");
        ast_destroy_node(body);
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected '(' after 'while' in do-while");
        ast_destroy_node(body);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* condition = parser_parse_expression(parser);
    if (!condition) {
        ast_destroy_node(body);
        return NULL;
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                            "expected ')' after do-while condition");
        ast_destroy_node(condition);
        ast_destroy_node(body);
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_DO_WHILE_STATEMENT, line, col);
    if (!node) {
        ast_destroy_node(condition);
        ast_destroy_node(body);
        return NULL;
    }
    node->data.do_while_stmt.body = body;
    node->data.do_while_stmt.condition = condition;
    return node;
}

ASTNode* parser_parse_while_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* condition = parser_parse_expression(parser);
    if (!condition) {
        return NULL;
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        ast_destroy_node(condition);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* body = parser_parse_block_or_statement(parser);
    if (!body) {
        ast_destroy_node(condition);
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_WHILE_STATEMENT, line, col);
    if (!node) {
        ast_destroy_node(condition);
        ast_destroy_node(body);
        return NULL;
    }
    node->data.while_stmt.condition = condition;
    node->data.while_stmt.body = body;
    return node;
}

ASTNode* parser_parse_for_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* init = NULL;
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        KrtTokenType t = parser->current_token.type;
        bool looks_decl = (t >= TOKEN_INT2 && t <= TOKEN_VOID) || t == TOKEN_TYPE_STRING;
        if (looks_decl) {
            init = parser_parse_variable_declaration_with_type(parser);
            if (!init) {
                return NULL;
            }
        } else {
            init = parser_parse_statement(parser);
            if (!init) {
                return NULL;
            }
        }
    }
    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);
    }

    ASTNode* condition = NULL;
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        condition = parser_parse_expression(parser);
        if (!condition) {
            if (init) {
                ast_destroy_node(init);
            }
            return NULL;
        }
    }
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        if (init) {
            ast_destroy_node(init);
        }
        if (condition) {
            ast_destroy_node(condition);
        }
        return NULL;
    }
    parser_advance(parser);

    ASTNode* increment = NULL;
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        increment = parser_parse_expression(parser);
        if (!increment) {
            if (init) {
                ast_destroy_node(init);
            }
            if (condition) {
                ast_destroy_node(condition);
            }
            return NULL;
        }
        if (parser->current_token.type == TOKEN_ASSIGN || KrtTokenIsCompoundAssignment(parser->current_token.type)) {
            ASTNode* asg = parser_parse_assignment_from_left(parser, increment);
            if (!asg) {
                ast_destroy_node(increment);
                if (init) {
                    ast_destroy_node(init);
                }
                if (condition) {
                    ast_destroy_node(condition);
                }
                return NULL;
            }
            increment = asg;
        }
    }
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        if (init) {
            ast_destroy_node(init);
        }
        if (condition) {
            ast_destroy_node(condition);
        }
        if (increment) {
            ast_destroy_node(increment);
        }
        return NULL;
    }
    parser_advance(parser);

    ASTNode* body = parser_parse_block(parser);
    if (!body) {
        if (init) {
            ast_destroy_node(init);
        }
        if (condition) {
            ast_destroy_node(condition);
        }
        if (increment) {
            ast_destroy_node(increment);
        }
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_FOR_STATEMENT, line, col);
    if (!node) {
        if (init) {
            ast_destroy_node(init);
        }
        if (condition) {
            ast_destroy_node(condition);
        }
        if (increment) {
            ast_destroy_node(increment);
        }
        ast_destroy_node(body);
        return NULL;
    }
    node->data.for_stmt.init = init;
    node->data.for_stmt.condition = condition;
    node->data.for_stmt.increment = increment;
    node->data.for_stmt.body = body;
    return node;
}

ASTNode* parser_parse_foreach_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    KrtTokenType t = parser->current_token.type;
    if ((t >= TOKEN_INT2 && t <= TOKEN_VOID) || t == TOKEN_TYPE_STRING || (t >= TOKEN_UINT2 && t <= TOKEN_UINT128)) {
        parser_advance(parser);
    }

    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        return NULL;
    }
    char* var_name = KrtParserStrdup(parser, parser->current_token.value);
    if (!var_name) {
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_IN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* iterable = parser_parse_expression(parser);
    if (!iterable) {
        return NULL;
    }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* body = parser_parse_block_or_statement(parser);
    if (!body) {
        return NULL;
    }

    ASTNode* node = KrtParserCreateNode(parser, AST_FOREACH_STATEMENT, line, col);
    if (!node) {
        return NULL;
    }
    node->data.foreach_stmt.var_name = var_name;
    node->data.foreach_stmt.iterable = iterable;
    node->data.foreach_stmt.body = body;
    return node;
}

ASTNode* parser_parse_block(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode** statements = (ASTNode**)KRT_PARSER_MALLOC(sizeof(ASTNode*) * 16);
    int count = 0, capacity = 16;

    int consecutive_errors = 0;

    while (parser->current_token.type != TOKEN_RIGHT_BRACE && parser->current_token.type != TOKEN_EOF) {

        if (parser->current_token.type == TOKEN_SEMICOLON) {
            parser_advance(parser);
            consecutive_errors = 0;
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            statements = (ASTNode**)KRT_REALLOC(statements, capacity * sizeof(ASTNode*));
            if (!statements) {
                return NULL;
            }
        }

        KrtTokenType current_token_before_parse = parser->current_token.type;

        ASTNode* stmt = parser_parse_statement(parser);
        if (stmt) {
            statements[count++] = stmt;
            consecutive_errors = 0;
        } else {
            bool token_advanced = (parser->current_token.type != current_token_before_parse);

            if (!token_advanced) {
                if (parser->current_token.type != TOKEN_EOF && parser->current_token.type != TOKEN_RIGHT_BRACE) {
                    parser_advance(parser); // 强制跳过,后续解决一下
                }

                consecutive_errors++;
            } else {
                consecutive_errors = 0;
            }

            if (consecutive_errors > 10) {
                for (int i = 0; i < count; i++) {
                    ast_destroy_node(statements[i]);
                }
                KRT_FREE(statements);
                return NULL;
            }
        }
    }

    if (parser->current_token.type != TOKEN_RIGHT_BRACE) {
        for (int i = 0; i < count; i++) {
            ast_destroy_node(statements[i]);
        }
        KRT_FREE(statements);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = KrtParserCreateNode(parser, AST_BLOCK, line, col);
    if (!node) {
        for (int i = 0; i < count; i++) {
            ast_destroy_node(statements[i]);
        }
        KRT_FREE(statements);
        return NULL;
    }
    node->data.block.statements = statements;
    node->data.block.statement_count = count;
    return node;
}

static ASTNode* parser_parse_keyword_function_declaration(Parser* parser) {
    /* The keyword supplies a void result; the remaining grammar is identical. */
    parser->current_token.type = TOKEN_VOID;
    return parser_parse_function_declaration(parser);
}

ASTNode* parser_parse_statement(Parser* parser) {
    Token token = parser->current_token;

    switch (token.type) {
    case TOKEN_UNSAFE: {
        ASTNode* node = KRT_PARSER_CREATE_NODE(AST_UNSAFE_CALL, token.line, token.column);
        node->data.unsafe_call.is_block = 1;
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_LEFT_PAREN) {
            goto unsafe_error;
        }
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_USING) {
            goto unsafe_error;
        }
        parser_advance(parser);
        int capacity = 4;
        node->data.unsafe_call.permissions = KrtParserAlloc(parser, capacity * sizeof(char*));
        for (;;) {
            char permission[1024];
            size_t length = 0;
            for (;;) {
                if (parser->current_token.type != TOKEN_IDENTIFIER) {
                    goto unsafe_error;
                }
                size_t part = strlen(parser->current_token.value);
                if (length + part + 2 > sizeof(permission)) {
                    goto unsafe_error;
                }
                memcpy(permission + length, parser->current_token.value, part);
                length += part;
                parser_advance(parser);
                if (parser->current_token.type != TOKEN_DOT) {
                    break;
                }
                permission[length++] = '.';
                parser_advance(parser);
            }
            permission[length] = 0;
            int n = node->data.unsafe_call.permission_count;
            if (n == capacity) {
                capacity *= 2;
                char** names = KrtParserAlloc(parser, capacity * sizeof(char*));
                memcpy(names, node->data.unsafe_call.permissions, n * sizeof(char*));
                node->data.unsafe_call.permissions = names;
            }
            node->data.unsafe_call.permissions[n] = KRT_PARSER_STRDUP(permission);
            node->data.unsafe_call.permission_count++;
            if (parser->current_token.type != TOKEN_COMMA) {
                break;
            }
            parser_advance(parser);
        }
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            goto unsafe_error;
        }
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            goto unsafe_error;
        }
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_LEFT_BRACE) {
            goto unsafe_error;
        }
        node->data.unsafe_call.expression = parser_parse_block(parser);
        return node;
    unsafe_error:
        parser_report_error(parser, token.line, token.column, "expected unsafe(using namespace, ...;) { ... }");
        return NULL;
    }
    case TOKEN_POINT: {
        int line = token.line;
        int col = token.column;
        parser_advance(parser);

        ASTNode* body = parser_parse_block(parser);
        if (!body) {
            return NULL;
        }

        ASTNode* node = KrtParserCreateNode(parser, AST_POINT_BLOCK, line, col);
        if (!node) {
            ast_destroy_node(body);
            return NULL;
        }
        node->data.point_block.body = body;
        return node;
    }

    case TOKEN_VAR:
    case TOKEN_LET: {
        Token v1 = lexer_peek_token(parser->lexer);
        Token v2 = (v1.type == TOKEN_IDENTIFIER) ? lexer_peek_nth_token(parser->lexer, 2) : (Token){0};
        int is_inferred =
            (v1.type == TOKEN_IDENTIFIER) && (v2.type == TOKEN_ASSIGN || v2.type == TOKEN_COLON ||
                                              v2.type == TOKEN_SEMICOLON || v2.type == TOKEN_LEFT_BRACKET);
        token_free(&v1);
        token_free(&v2);
        if (is_inferred) {
            return parser_parse_type_inferred_declaration(parser);
        }
        return parser_parse_variable_declaration(parser);
    }

    case TOKEN_IF:
        return parser_parse_if_statement(parser);

    case TOKEN_WHILE:
        return parser_parse_while_statement(parser);

    case TOKEN_DO:
        return parser_parse_do_statement(parser);

    case TOKEN_SWITCH:
        return parser_parse_switch_statement(parser);

    case TOKEN_FOR:
        return parser_parse_for_statement(parser);

    case TOKEN_FOREACH:
        return parser_parse_foreach_statement(parser);

    case TOKEN_RETURN:
        return parser_parse_return_statement(parser);

    case TOKEN_BREAK: {
        int line = parser->current_token.line;
        int col = parser->current_token.column;
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            return NULL;
        }
        parser_advance(parser);

        ASTNode* node = KrtParserCreateNode(parser, AST_BREAK_STATEMENT, line, col);
        if (!node) {
            return NULL;
        }
        return node;
    }

    case TOKEN_CONTINUE: {
        int line = parser->current_token.line;
        int col = parser->current_token.column;
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            return NULL;
        }
        parser_advance(parser);

        ASTNode* node = KrtParserCreateNode(parser, AST_CONTINUE_STATEMENT, line, col);
        if (!node) {
            return NULL;
        }
        return node;
    }

    case TOKEN_PRINT:
        return parser_parse_print_statement(parser);

    case TOKEN_LEFT_BRACE:
        return parser_parse_block(parser);

    case TOKEN_NAMESPACE: {
        ASTNode* ns = parser_parse_namespace_declaration(parser);
        return ns;
    }

    case TOKEN_ENUM:
        return parser_parse_enum(parser);

    case TOKEN_CLASS:
        return parser_parse_class_declaration(parser);

    case TOKEN_PUBLIC:
    case TOKEN_PRIVATE:
    case TOKEN_PROTECTED:
    case TOKEN_STATIC:
    case TOKEN_CONST:
    case TOKEN_EXTERN:
        return parser_parse_declaration_with_modifiers(parser);

    case TOKEN_USING: {
        int line = parser->current_token.line;
        int col = parser->current_token.column;
        parser_advance(parser);

        char** path_parts = (char**)KRT_MALLOC(16 * sizeof(char*));
        int path_count = 0;
        int capacity = 16;
        char* alias = NULL;

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            path_parts[path_count++] = KRT_STRDUP(parser->current_token.value);
            parser_advance(parser);

            while (parser->current_token.type == TOKEN_DOT) {
                parser_advance(parser);
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    if (path_count >= capacity) {
                        capacity *= 2;
                        path_parts = (char**)KRT_REALLOC(path_parts, capacity * sizeof(char*));
                    }
                    path_parts[path_count++] = KRT_STRDUP(parser->current_token.value);
                    parser_advance(parser);
                } else if ((parser->current_token.type == TOKEN_MULTIPLY ||
                            parser->current_token.type == TOKEN_POWER)) {
                    parser_advance(parser);
                    char* wildcard = KRT_STRDUP("*");
                    if (path_count >= capacity) {
                        capacity *= 2;
                        path_parts = (char**)KRT_REALLOC(path_parts, capacity * sizeof(char*));
                    }
                    path_parts[path_count++] = wildcard;
                    break;
                } else {
                    for (int i = 0; i < path_count; i++) {
                        KRT_FREE(path_parts[i]);
                    }
                    KRT_FREE(path_parts);
                    return NULL;
                }
            }

            if (parser->current_token.type == TOKEN_SEMICOLON) {
                parser_advance(parser);
            }

            ASTNode* node = KrtParserCreateNode(parser, AST_USING_DIRECTIVE, line, col);
            if (node) {
                node->data.using_directive.alias = alias;
                node->data.using_directive.namespace_path = path_parts;
                node->data.using_directive.path_length = path_count;
                node->data.using_directive.is_alias = 0;
            }
            return node;
        } else {
            KRT_FREE(path_parts);
            return NULL;
        }
    }

    case TOKEN_VOID:
#define KRT_INTEGER_WIDTH(bits)                                                                                        \
    case TOKEN_INT##bits:                                                                                              \
    case TOKEN_UINT##bits:
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
    case TOKEN_FLOAT32:
    case TOKEN_FLOAT64:
    case TOKEN_BOOL:
    case TOKEN_TYPE_STRING:
    case TOKEN_CHAR:
    case TOKEN_IDENTIFIER: {
        Token next_token = lexer_peek_token(parser->lexer);
        Token third_token = {0};

        if (token.type == TOKEN_IDENTIFIER && (next_token.type == TOKEN_LESS || next_token.type == TOKEN_DOT)) {
            int depth = 0;
            for (int look = 1; look < 256; look++) {
                Token scan = lexer_peek_nth_token(parser->lexer, look);
                KrtTokenType kind = scan.type;
                token_free(&scan);
                if (kind == TOKEN_LESS) {
                    depth++;
                } else if (kind == TOKEN_GREATER) {
                    depth--;
                } else if (kind == TOKEN_RSHIFT) {
                    depth -= 2;
                }
                if (depth < 0 || kind == TOKEN_EOF || kind == TOKEN_SEMICOLON || kind == TOKEN_ASSIGN ||
                    kind == TOKEN_LEFT_PAREN) {
                    break;
                }
                if (depth == 0 && kind == TOKEN_IDENTIFIER) {
                    Token after = lexer_peek_nth_token(parser->lexer, look + 1);
                    KrtTokenType after_type = after.type;
                    token_free(&after);
                    if (after_type == TOKEN_IDENTIFIER) {
                        Token tail = lexer_peek_nth_token(parser->lexer, look + 2);
                        bool function = tail.type == TOKEN_LEFT_PAREN;
                        token_free(&tail);
                        token_free(&next_token);
                        return function ? parser_parse_function_declaration(parser)
                                        : parser_parse_variable_declaration_with_type(parser);
                    }
                }
                if (depth == 0 && (kind == TOKEN_GREATER || kind == TOKEN_RSHIFT)) {
                    Token tail = lexer_peek_nth_token(parser->lexer, look + 2);
                    bool function = tail.type == TOKEN_LEFT_PAREN;
                    token_free(&tail);
                    token_free(&next_token);
                    return function ? parser_parse_function_declaration(parser)
                                    : parser_parse_variable_declaration_with_type(parser);
                }
            }
        }

        if (next_token.type == TOKEN_IDENTIFIER) {
            third_token = lexer_peek_nth_token(parser->lexer, 2);

            if (third_token.type == TOKEN_LEFT_PAREN) {
                token_free(&next_token);
                token_free(&third_token);
                return parser_parse_function_declaration(parser);
            } else {
                token_free(&next_token);
                token_free(&third_token);
                return parser_parse_variable_declaration_with_type(parser);
            }
        } else if (next_token.type == TOKEN_LEFT_BRACKET) {
            Token bracket_token = lexer_peek_nth_token(parser->lexer, 2);
            bool is_array_type_declaration =
                token.type != TOKEN_IDENTIFIER || bracket_token.type == TOKEN_RIGHT_BRACKET;
            token_free(&next_token);
            token_free(&bracket_token);
            if (is_array_type_declaration) {
                return parser_parse_variable_declaration_with_type(parser);
            }

            ASTNode* expr = parser_parse_expression(parser);
            if (!expr) {
                return NULL;
            }
            ASTNode* stmt = parser_parse_assignment_from_left(parser, expr);
            if (parser->current_token.type != TOKEN_SEMICOLON) {
                ast_destroy_node(stmt);
                return NULL;
            }
            parser_advance(parser);
            return stmt;
        } else if ((next_token.type == TOKEN_MULTIPLY || next_token.type == TOKEN_POWER) &&
                   token.type != TOKEN_IDENTIFIER) {
            int look = 2;
            Token name_token = lexer_peek_nth_token(parser->lexer, look);
            while (name_token.type == TOKEN_MULTIPLY || name_token.type == TOKEN_POWER ||
                   name_token.type == TOKEN_QUESTION) {
                token_free(&name_token);
                name_token = lexer_peek_nth_token(parser->lexer, ++look);
            }
            Token after_name = lexer_peek_nth_token(parser->lexer, look + 1);
            bool is_function = name_token.type == TOKEN_IDENTIFIER && after_name.type == TOKEN_LEFT_PAREN;
            token_free(&next_token);
            token_free(&name_token);
            token_free(&after_name);
            return is_function ? parser_parse_function_declaration(parser)
                               : parser_parse_variable_declaration_with_type(parser);
        } else {
            token_free(&next_token);
            token_free(&third_token);
            ASTNode* expr = parser_parse_expression(parser);
            if (!expr) {
                return NULL;
            }
            ASTNode* stmt = parser_parse_assignment_from_left(parser, expr);
            if (parser->current_token.type != TOKEN_SEMICOLON) {
                ast_destroy_node(stmt);
                return NULL;
            }
            parser_advance(parser);
            return stmt;
        }
    }

    case TOKEN_FUNCTION: {
        Token f_next = lexer_peek_token(parser->lexer);
        Token f_n2 = (f_next.type == TOKEN_IDENTIFIER) ? lexer_peek_nth_token(parser->lexer, 2) : (Token){0};
        int is_keyword_fn = f_next.type == TOKEN_IDENTIFIER && f_n2.type == TOKEN_LEFT_PAREN;
        token_free(&f_next);
        token_free(&f_n2);
        if (is_keyword_fn) {
            return parser_parse_keyword_function_declaration(parser);
        }
        return parser_parse_type_inferred_declaration(parser);
    }

    default: {

        // 如果是 EOF 或未知 token，会跳过,后续解决
        if (parser->current_token.type == TOKEN_EOF || parser->current_token.type == TOKEN_UNKNOWN) {
            return NULL;
        }

        ASTNode* expr = parser_parse_expression(parser);
        if (!expr) {
            if (parser->current_token.type != TOKEN_EOF) {
                parser_advance(parser);
            }
            return NULL;
        }
        ASTNode* stmt = parser_parse_assignment_from_left(parser, expr);
        if (parser->current_token.type != TOKEN_SEMICOLON) {
            ast_destroy_node(stmt);
            return NULL;
        }
        parser_advance(parser);
        return stmt;
    }
    }
}
