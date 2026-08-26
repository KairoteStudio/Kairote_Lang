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

#define PARSER_LIKELY(x) (x)
#define PARSER_UNLIKELY(x) (x)
#define PARSER_ARGUMENT_CAPACITY_INIT 8

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

static KrtTokenType token_type_to_krt_type(KrtTokenType token_type) {
    switch (token_type) {
        case TOKEN_VOID: return TOKEN_VOID;
        case TOKEN_INT8: return TOKEN_INT8;
        case TOKEN_INT16: return TOKEN_INT16;
        case TOKEN_INT32: return TOKEN_INT32;
        case TOKEN_INT64: return TOKEN_INT64;
        case TOKEN_FLOAT32: return TOKEN_FLOAT32;
        case TOKEN_FLOAT64: return TOKEN_FLOAT64;
        case TOKEN_BOOL: return TOKEN_BOOL;
        case TOKEN_TYPE_STRING: return TOKEN_TYPE_STRING;
        case TOKEN_CHAR: return TOKEN_CHAR;
        default: return TOKEN_VOID;
    }
}

static ASTNode* parser_parse_function_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    
    KrtTokenType return_type = token_type_to_krt_type(parser->current_token.type);
    parser_advance(parser);

    while (parser->current_token.type == TOKEN_MULTIPLY) {
        parser_advance(parser);
    }
    
    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        return NULL;
    }
    
    char* func_name = arena_strdup(parser->arena, parser->current_token.value);
    parser_advance(parser);
    
    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        KRT_FREE(func_name);
        return NULL;
    }
    parser_advance(parser);
    
    char** param_names = NULL;
    KrtTokenType* param_types = NULL;
    int* param_is_params = NULL;
    int* param_is_array = NULL;
    int param_count = 0;
    int param_capacity = 8;
    
    param_names = (char**)KrtArenaAlloc(parser->arena, param_capacity * sizeof(char*));
    param_types = (KrtTokenType*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(KrtTokenType));
    param_is_params = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
    param_is_array = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
    
    while (parser->current_token.type != TOKEN_RIGHT_PAREN && 
           parser->current_token.type != TOKEN_EOF) {
        if (param_count >= param_capacity) {
            param_capacity *= 2;
            char** new_names = (char**)KrtArenaAlloc(parser->arena, param_capacity * sizeof(char*));
            KrtTokenType* new_types = (KrtTokenType*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(KrtTokenType));
            int* new_is_params = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
            int* new_is_array = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
            memcpy(new_names, param_names, param_count * sizeof(char*));
            memcpy(new_types, param_types, param_count * sizeof(KrtTokenType));
            memcpy(new_is_params, param_is_params, param_count * sizeof(int));
            memcpy(new_is_array, param_is_array, param_count * sizeof(int));
            param_names = new_names;
            param_types = new_types;
            param_is_params = new_is_params;
            param_is_array = new_is_array;
        }
        
        KrtTokenType ptype = token_type_to_krt_type(parser->current_token.type);
        parser_advance(parser);

        while (parser->current_token.type == TOKEN_MULTIPLY) {
            parser_advance(parser);
        }

        int is_array_param = 0;
        if (parser->current_token.type == TOKEN_LEFT_BRACKET) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
                KRT_FREE(func_name);
                return NULL;
            }
            parser_advance(parser);
            is_array_param = 1;
        }
        
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            KRT_FREE(func_name);
            return NULL;
        }
        
        param_names[param_count] = arena_strdup(parser->arena, parser->current_token.value);
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
        KRT_FREE(func_name);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* body = NULL;
    if (parser->current_token.type == TOKEN_LEFT_BRACE) {
        body = parser_parse_block(parser);
    } else {
        KRT_FREE(func_name);
        return NULL;
    }
    
    if (!body) {
        KRT_FREE(func_name);
        return NULL;
    }
    
    ASTNode* node = ast_create_node_arena(AST_FUNCTION_DECLARATION, line, col, parser->arena);
    if (!node) {
        KRT_FREE(func_name);
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
    
    return node;
}

static ASTNode* parser_parse_declaration_with_modifiers(Parser* parser) {
    int is_static = 0;
    int is_const = 0;
    while (parser->current_token.type == TOKEN_PUBLIC ||
           parser->current_token.type == TOKEN_PRIVATE ||
           parser->current_token.type == TOKEN_PROTECTED ||
           parser->current_token.type == TOKEN_STATIC ||
           parser->current_token.type == TOKEN_CONST ||
           parser->current_token.type == TOKEN_EXTERN) {
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
    } else if ((is_static || is_const) && declaration &&
               declaration->type == AST_VARIABLE_DECLARATION) {
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
        
        while (parser->current_token.type == TOKEN_MULTIPLY) {
            pointer_depth++;
            parser_advance(parser);
        }
    } else if (parser->current_token.type == TOKEN_IDENTIFIER) {
        type_token = TOKEN_IDENTIFIER;
        type_name = KRT_STRDUP(parser->current_token.value);
        if (!type_name) return NULL;
        parser_advance(parser);
        
        while (parser->current_token.type == TOKEN_MULTIPLY) {
            pointer_depth++;
            parser_advance(parser);
        }
    } else {
        return NULL;
    }

    char* name = NULL;
    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        name = KRT_STRDUP(parser->current_token.value);
        parser_advance(parser);
    } else if (parser->current_token.type >= TOKEN_OP_ADDITION &&
               parser->current_token.type <= TOKEN_OP_EXPLICIT) {
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
            if (!array_size) { KRT_FREE(name); KRT_FREE(type_name); return NULL; }
        }
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) ast_destroy_node(array_size);
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
                parser_report_error(parser, parser->current_token.line,
                                    parser->current_token.column,
                                    "expected expression after '=' in declaration of '%s'",
                                    name ? name : "?");
            }
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) ast_destroy_node(array_size);
            return NULL;
        }
    }

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        if (declared_by_keyword) {
            parser_report_error(parser, parser->current_token.line,
                                parser->current_token.column,
                                "expected ';' after declaration of '%s'",
                                name ? name : "?");
        }
        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) ast_destroy_node(value);
        if (array_size) ast_destroy_node(array_size);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = ast_create_node_arena(AST_VARIABLE_DECLARATION, line, col, parser->arena);
    if (!node) {
        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) ast_destroy_node(value);
        if (array_size) ast_destroy_node(array_size);
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

    return node;
}

static ASTNode* parser_parse_variable_declaration_with_type(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    KrtTokenType type_token = token_type_to_krt_type(parser->current_token.type);
    char* type_name = NULL;

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        type_token = TOKEN_IDENTIFIER;
        type_name = arena_strdup(parser->arena, parser->current_token.value);
        if (!type_name) return NULL;
    }
    parser_advance(parser);

    int pointer_depth = 0;
    while (parser->current_token.type == TOKEN_MULTIPLY) {
        pointer_depth++;
        parser_advance(parser);
    }

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
        name = arena_strdup(parser->arena, parser->current_token.value);
        parser_advance(parser);
    } else if (parser->current_token.type >= TOKEN_OP_ADDITION &&
               parser->current_token.type <= TOKEN_OP_EXPLICIT) {
        const char* op_name = token_type_to_string(parser->current_token.type);
        name = arena_strdup(parser->arena, op_name ? op_name : "op_Unknown");
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
            if (!array_size) { KRT_FREE(name); KRT_FREE(type_name); return NULL; }
        }
        if (parser->current_token.type != TOKEN_RIGHT_BRACKET) {
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) ast_destroy_node(array_size);
            return NULL;
        }
        parser_advance(parser);
    }

    ASTNode* value = NULL;
    if (parser->current_token.type == TOKEN_ASSIGN) {
        parser_advance(parser);
        value = parser_parse_expression(parser);
        if (!value) {
            parser_report_error(parser, parser->current_token.line,
                                parser->current_token.column,
                                "expected expression after '=' in declaration");
            KRT_FREE(name);
            KRT_FREE(type_name);
            if (array_size) ast_destroy_node(array_size);
            return NULL;
        }
    }

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected ';' after declaration");

        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) ast_destroy_node(value);
        if (array_size) ast_destroy_node(array_size);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = ast_create_node_arena(AST_VARIABLE_DECLARATION, line, col, parser->arena);
    if (!node) {
        KRT_FREE(name);
        KRT_FREE(type_name);
        if (value) ast_destroy_node(value);
        if (array_size) ast_destroy_node(array_size);
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

    return node;
}

static ASTNode* parser_parse_type_inferred_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    parser_advance(parser);

    char* name = NULL;
    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        name = arena_strdup(parser->arena, parser->current_token.value);
        parser_advance(parser);
    } else {
        return NULL;
    }

    if (!name) return NULL;

    if (parser->current_token.type != TOKEN_ASSIGN) {
        KRT_FREE(name);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* value = parser_parse_expression(parser);
    if (!value) {
        KRT_FREE(name);
        return NULL;
    }

    if (parser->current_token.type != TOKEN_SEMICOLON) {
        KRT_FREE(name);
        ast_destroy_node(value);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* node = ast_create_node_arena(AST_VARIABLE_DECLARATION, line, col, parser->arena);
    if (!node) {
        KRT_FREE(name);
        ast_destroy_node(value);
        return NULL;
    }

    node->data.variable_decl.name = name;
    node->data.variable_decl.value = value;
    node->data.variable_decl.type = TOKEN_AUTO;
    node->data.variable_decl.template_instantiation_type = NULL;
    node->data.variable_decl.array_size = NULL;
    node->data.variable_decl.is_array = false;
    node->data.variable_decl.is_let = 0;

    return node;
}

static ASTNode* parser_parse_assignment_from_left(Parser* parser, ASTNode* left) {
    if (!left) return NULL;

    KrtTokenType operator = parser->current_token.type;
    if (operator != TOKEN_ASSIGN && operator != TOKEN_PLUS_ASSIGN &&
        operator != TOKEN_MINUS_ASSIGN && operator != TOKEN_MUL_ASSIGN &&
        operator != TOKEN_DIV_ASSIGN && operator != TOKEN_MOD_ASSIGN) {
        return left;
    }

    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    ASTNode* value = parser_parse_expression(parser);
    if (!value) { ast_destroy_node(left); return NULL; }

    if (left->type == AST_ARRAY_ACCESS) {
        ASTNode* node = (operator == TOKEN_ASSIGN)
            ? PARSER_CREATE_NODE(AST_ARRAY_ASSIGNMENT, line, col)
            : PARSER_CREATE_NODE(AST_ARRAY_COMPOUND_ASSIGNMENT, line, col);
        if (!node) { ast_destroy_node(left); ast_destroy_node(value); return NULL; }

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
        ASTNode* node = (operator == TOKEN_ASSIGN) ?
            ast_create_node_arena(AST_ASSIGNMENT, line, col, parser->arena) :
            ast_create_node_arena(AST_COMPOUND_ASSIGNMENT, line, col, parser->arena);
        if (!node) { ast_destroy_node(left); ast_destroy_node(value); return NULL; }

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
            ASTNode* node = ast_create_node_arena(AST_BINARY_OPERATION, line, col, parser->arena);
            if (!node) { ast_destroy_node(left); ast_destroy_node(value); return NULL; }
            node->data.binary_op.left = left;
            node->data.binary_op.operator = operator;
            node->data.binary_op.right = value;
            return node;
        }
    }

    ast_destroy_node(left);
    ast_destroy_node(value);
    return NULL;
}

ASTNode* parser_parse_return_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    
    ASTNode* value = NULL;
    if (parser->current_token.type != TOKEN_SEMICOLON && 
        parser->current_token.type != TOKEN_RIGHT_BRACE &&
        parser->current_token.type != TOKEN_EOF) {
        value = parser_parse_expression(parser);
    }
    
    ASTNode* node = ast_create_node_arena(AST_RETURN_STATEMENT, line, col, parser->arena);
    if (!node) { if (value) ast_destroy_node(value); return NULL; }
    node->data.return_stmt.value = value;
    return node;
}

ASTNode* parser_parse_print_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    
    if (parser->current_token.type != TOKEN_LEFT_PAREN) { return NULL; }
    parser_advance(parser);
    
    ASTNode** values = (ASTNode**)PARSER_ALLOC_FROM_ARENA(parser, sizeof(ASTNode*) * PARSER_ARGUMENT_CAPACITY_INIT);
    int count = 0, capacity = PARSER_ARGUMENT_CAPACITY_INIT;
    
    while (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        if (count >= capacity) {
            capacity *= 2;
            values = (ASTNode**)KRT_REALLOC(values, capacity * sizeof(ASTNode*));
            if (!values) return NULL;
        }
        
        values[count++] = parser_parse_expression(parser);
        if (!values[count - 1]) break;
        
        if (parser->current_token.type == TOKEN_COMMA) {
            parser_advance(parser);
        } else if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            break;
        }
    }
    
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        for (int i = 0; i < count; i++) ast_destroy_node(values[i]);
        KRT_FREE(values);
        return NULL;
    }
    parser_advance(parser);
    
    bool has_newline = true;
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        for (int i = 0; i < count; i++) ast_destroy_node(values[i]);
        KRT_FREE(values);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* node = ast_create_node_arena(AST_PRINT_STATEMENT, line, col, parser->arena);
    if (!node) { 
        for (int i = 0; i < count; i++) ast_destroy_node(values[i]); 
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
    if (!stmt) return NULL;

    ASTNode** statements = (ASTNode**)PARSER_MALLOC(sizeof(ASTNode*) * 1);
    if (!statements) { ast_destroy_node(stmt); return NULL; }
    statements[0] = stmt;

    ASTNode* node = ast_create_node_arena(AST_BLOCK, line, col, parser->arena);
    if (!node) { ast_destroy_node(statements[0]); KRT_FREE(statements); return NULL; }
    node->data.block.statements = statements;
    node->data.block.statement_count = 1;
    return node;
}

ASTNode* parser_parse_if_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    
    if (parser->current_token.type != TOKEN_LEFT_PAREN) return NULL;
    parser_advance(parser);
    
    ASTNode* condition = parser_parse_expression(parser);
    if (!condition) return NULL;
    
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        ast_destroy_node(condition);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* then_branch = parser_parse_block_or_statement(parser);
    if (!then_branch) { ast_destroy_node(condition); return NULL; }
    
    ASTNode* else_branch = NULL;
    if (parser->current_token.type == TOKEN_ELSE) {
        parser_advance(parser);
        if (parser->current_token.type == TOKEN_IF) {
            else_branch = parser_parse_if_statement(parser);
        } else {
            else_branch = parser_parse_block_or_statement(parser);
        }
        if (!else_branch) { ast_destroy_node(condition); ast_destroy_node(then_branch); return NULL; }
    }
    
    ASTNode* node = ast_create_node_arena(AST_IF_STATEMENT, line, col, parser->arena);
    if (!node) { 
        ast_destroy_node(condition); 
        ast_destroy_node(then_branch); 
        if (else_branch) ast_destroy_node(else_branch); 
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
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected '(' after 'switch'");
        return NULL;
    }
    parser_advance(parser);

    ASTNode* expr = parser_parse_expression(parser);
    if (!expr) return NULL;

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected ')' after switch expression");
        ast_destroy_node(expr);
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected '{' to open switch body");
        ast_destroy_node(expr);
        return NULL;
    }
    parser_advance(parser);

    ASTNode** cases = NULL;
    int case_count = 0, cap = 0;
    ASTNode* default_case = NULL;

    while (parser->current_token.type != TOKEN_RIGHT_BRACE &&
           parser->current_token.type != TOKEN_EOF) {
        if (parser->current_token.type == TOKEN_CASE) {
            int cline = parser->current_token.line, ccol = parser->current_token.column;
            parser_advance(parser);
            ASTNode* value = parser_parse_expression(parser);
            if (!value) goto cleanup;
            if (parser->current_token.type != TOKEN_COLON) {
                parser_report_error(parser, parser->current_token.line,
                                    parser->current_token.column,
                                    "expected ':' after case value");
                ast_destroy_node(value); goto cleanup;
            }
            parser_advance(parser);

            ASTNode** stmts = NULL; int sc = 0, scap = 0;
            while (parser->current_token.type != TOKEN_CASE &&
                   parser->current_token.type != TOKEN_DEFAULT &&
                   parser->current_token.type != TOKEN_RIGHT_BRACE &&
                   parser->current_token.type != TOKEN_EOF) {
                ASTNode* st = parser_parse_statement(parser);
                if (!st) {
                    if (parser->current_token.type != TOKEN_EOF &&
                        parser->current_token.type != TOKEN_RIGHT_BRACE)
                        parser_advance(parser);
                    continue;
                }
                if (sc >= scap) { scap = scap ? scap * 2 : 4;
                    stmts = (ASTNode**)KRT_REALLOC(stmts, scap * sizeof(ASTNode*)); }
                stmts[sc++] = st;
            }
            ASTNode* clause = ast_create_node_arena(AST_CASE_CLAUSE, cline, ccol, parser->arena);
            if (!clause) { ast_destroy_node(value);
                for (int k = 0; k < sc; k++) ast_destroy_node(stmts[k]);
                KRT_FREE(stmts); goto cleanup; }
            clause->data.case_clause.value = value;
            clause->data.case_clause.statements = stmts;
            clause->data.case_clause.statement_count = sc;
            if (case_count >= cap) { cap = cap ? cap * 2 : 4;
                cases = (ASTNode**)KRT_REALLOC(cases, cap * sizeof(ASTNode*)); }
            cases[case_count++] = clause;
        } else if (parser->current_token.type == TOKEN_DEFAULT) {
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_COLON) {
                parser_report_error(parser, parser->current_token.line,
                                    parser->current_token.column,
                                    "expected ':' after 'default'");
                goto cleanup;
            }
            parser_advance(parser);

            ASTNode** stmts = NULL; int sc = 0, scap = 0;
            while (parser->current_token.type != TOKEN_CASE &&
                   parser->current_token.type != TOKEN_DEFAULT &&
                   parser->current_token.type != TOKEN_RIGHT_BRACE &&
                   parser->current_token.type != TOKEN_EOF) {
                ASTNode* st = parser_parse_statement(parser);
                if (!st) {
                    if (parser->current_token.type != TOKEN_EOF &&
                        parser->current_token.type != TOKEN_RIGHT_BRACE)
                        parser_advance(parser);
                    continue;
                }
                if (sc >= scap) { scap = scap ? scap * 2 : 4;
                    stmts = (ASTNode**)KRT_REALLOC(stmts, scap * sizeof(ASTNode*)); }
                stmts[sc++] = st;
            }
            ASTNode* blk = ast_create_node_arena(AST_BLOCK, line, col, parser->arena);
            if (!blk) { for (int k = 0; k < sc; k++) ast_destroy_node(stmts[k]);
                KRT_FREE(stmts); goto cleanup; }
            blk->data.block.statements = stmts;
            blk->data.block.statement_count = sc;
            default_case = blk;
        } else {
            parser_advance(parser);
        }
    }

    if (parser->current_token.type != TOKEN_RIGHT_BRACE) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected '}' to close switch body");
        goto cleanup;
    }
    parser_advance(parser);

    ASTNode* node = ast_create_node_arena(AST_SWITCH_STATEMENT, line, col, parser->arena);
    if (!node) goto cleanup;
    node->data.switch_stmt.expression = expr;
    node->data.switch_stmt.cases = cases;
    node->data.switch_stmt.case_count = case_count;
    node->data.switch_stmt.default_case = default_case;
    return node;

cleanup:
    ast_destroy_node(expr);
    for (int k = 0; k < case_count; k++) ast_destroy_node(cases[k]);
    if (cases) KRT_FREE(cases);
    ast_destroy_node(default_case);
    return NULL;
}

ASTNode* parser_parse_do_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    ASTNode* body = parser_parse_block_or_statement(parser);
    if (!body) return NULL;

    if (parser->current_token.type != TOKEN_WHILE) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected 'while' after 'do' body");
        ast_destroy_node(body);
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_LEFT_PAREN) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected '(' after 'while' in do-while");
        ast_destroy_node(body);
        return NULL;
    }
    parser_advance(parser);

    ASTNode* condition = parser_parse_expression(parser);
    if (!condition) { ast_destroy_node(body); return NULL; }

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        parser_report_error(parser, parser->current_token.line,
                            parser->current_token.column,
                            "expected ')' after do-while condition");
        ast_destroy_node(condition);
        ast_destroy_node(body);
        return NULL;
    }
    parser_advance(parser);

    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);
    }

    ASTNode* node = ast_create_node_arena(AST_DO_WHILE_STATEMENT, line, col, parser->arena);
    if (!node) { ast_destroy_node(condition); ast_destroy_node(body); return NULL; }
    node->data.do_while_stmt.body = body;
    node->data.do_while_stmt.condition = condition;
    return node;
}

ASTNode* parser_parse_while_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    
    if (parser->current_token.type != TOKEN_LEFT_PAREN) return NULL;
    parser_advance(parser);
    
    ASTNode* condition = parser_parse_expression(parser);
    if (!condition) return NULL;
    
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        ast_destroy_node(condition);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* body = parser_parse_block_or_statement(parser);
    if (!body) { ast_destroy_node(condition); return NULL; }
    
    ASTNode* node = ast_create_node_arena(AST_WHILE_STATEMENT, line, col, parser->arena);
    if (!node) { ast_destroy_node(condition); ast_destroy_node(body); return NULL; }
    node->data.while_stmt.condition = condition;
    node->data.while_stmt.body = body;
    return node;
}

ASTNode* parser_parse_for_statement(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);
    
    if (parser->current_token.type != TOKEN_LEFT_PAREN) return NULL;
    parser_advance(parser);
    
    ASTNode* init = NULL;
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        KrtTokenType t = parser->current_token.type;
        bool looks_decl = (t >= TOKEN_INT8 && t <= TOKEN_VOID) || t == TOKEN_TYPE_STRING;
        if (looks_decl) {
            init = parser_parse_variable_declaration_with_type(parser);
            if (!init) return NULL;
        } else {
            init = parser_parse_statement(parser);
            if (!init) return NULL;
        }
    }
    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);
    }
    
    ASTNode* condition = NULL;
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        condition = parser_parse_expression(parser);
        if (!condition) { if (init) ast_destroy_node(init); return NULL; }
    }
    if (parser->current_token.type != TOKEN_SEMICOLON) {
        if (init) ast_destroy_node(init);
        if (condition) ast_destroy_node(condition);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* increment = NULL;
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        increment = parser_parse_expression(parser);
        if (!increment) {
            if (init) ast_destroy_node(init);
            if (condition) ast_destroy_node(condition);
            return NULL;
        }
        if (parser->current_token.type == TOKEN_ASSIGN ||
            parser->current_token.type == TOKEN_PLUS_ASSIGN ||
            parser->current_token.type == TOKEN_MINUS_ASSIGN ||
            parser->current_token.type == TOKEN_MUL_ASSIGN ||
            parser->current_token.type == TOKEN_DIV_ASSIGN ||
            parser->current_token.type == TOKEN_MOD_ASSIGN) {
            ASTNode* asg = parser_parse_assignment_from_left(parser, increment);
            if (!asg) {
                ast_destroy_node(increment);
                if (init) ast_destroy_node(init);
                if (condition) ast_destroy_node(condition);
                return NULL;
            }
            increment = asg;
        }
    }
    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        if (init) ast_destroy_node(init);
        if (condition) ast_destroy_node(condition);
        if (increment) ast_destroy_node(increment);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* body = parser_parse_block(parser);
    if (!body) { 
        if (init) ast_destroy_node(init); 
        if (condition) ast_destroy_node(condition); 
        if (increment) ast_destroy_node(increment); 
        return NULL; 
    }
    
    ASTNode* node = ast_create_node_arena(AST_FOR_STATEMENT, line, col, parser->arena);
    if (!node) { 
        if (init) ast_destroy_node(init); 
        if (condition) ast_destroy_node(condition); 
        if (increment) ast_destroy_node(increment); 
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

    if (parser->current_token.type != TOKEN_LEFT_PAREN) return NULL;
    parser_advance(parser);

    KrtTokenType t = parser->current_token.type;
    if ((t >= TOKEN_INT8 && t <= TOKEN_VOID) || t == TOKEN_TYPE_STRING ||
        (t >= TOKEN_UINT8 && t <= TOKEN_UINT64)) {
        parser_advance(parser);
    }

    if (parser->current_token.type != TOKEN_IDENTIFIER) return NULL;
    char* var_name = arena_strdup(parser->arena, parser->current_token.value);
    if (!var_name) return NULL;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_IN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* iterable = parser_parse_expression(parser);
    if (!iterable) return NULL;

    if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    parser_advance(parser);

    ASTNode* body = parser_parse_block_or_statement(parser);
    if (!body) { return NULL; }

    ASTNode* node = ast_create_node_arena(AST_FOREACH_STATEMENT, line, col, parser->arena);
    if (!node) return NULL;
    node->data.foreach_stmt.var_name = var_name;
    node->data.foreach_stmt.iterable = iterable;
    node->data.foreach_stmt.body = body;
    return node;
}

ASTNode* parser_parse_block(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;

    if (parser->current_token.type != TOKEN_LEFT_BRACE) return NULL;
    parser_advance(parser);

    ASTNode** statements = (ASTNode**)PARSER_MALLOC(sizeof(ASTNode*) * 16);
    int count = 0, capacity = 16;

    int consecutive_errors = 0;

    while (parser->current_token.type != TOKEN_RIGHT_BRACE &&
           parser->current_token.type != TOKEN_EOF) {

        if (parser->current_token.type == TOKEN_SEMICOLON) {
            parser_advance(parser);
            consecutive_errors = 0;
            continue;
        }

        if (count >= capacity) {
            capacity *= 2;
            statements = (ASTNode**)KRT_REALLOC(statements, capacity * sizeof(ASTNode*));
            if (!statements) return NULL;
        }

        KrtTokenType current_token_before_parse = parser->current_token.type;

        ASTNode* stmt = parser_parse_statement(parser);
        if (stmt) {
            statements[count++] = stmt;
            consecutive_errors = 0;
        } else {
            bool token_advanced = (parser->current_token.type != current_token_before_parse);

            if (!token_advanced) {
                if (parser->current_token.type != TOKEN_EOF &&
                    parser->current_token.type != TOKEN_RIGHT_BRACE) {
                    parser_advance(parser);  // 强制跳过,后续解决一下
                }

                consecutive_errors++;
            } else {
                consecutive_errors = 0;
            }

            if (consecutive_errors > 10) {
                for (int i = 0; i < count; i++) ast_destroy_node(statements[i]);
                KRT_FREE(statements);
                return NULL;
            }
        }
    }
    
    if (parser->current_token.type != TOKEN_RIGHT_BRACE) {
        for (int i = 0; i < count; i++) ast_destroy_node(statements[i]);
        KRT_FREE(statements);
        return NULL;
    }
    parser_advance(parser);
    
    ASTNode* node = ast_create_node_arena(AST_BLOCK, line, col, parser->arena);
    if (!node) { 
        for (int i = 0; i < count; i++) ast_destroy_node(statements[i]); 
        KRT_FREE(statements); 
        return NULL; 
    }
    node->data.block.statements = statements;
    node->data.block.statement_count = count;
    return node;
}

static ASTNode* parser_parse_keyword_function_declaration(Parser* parser) {
    int line = parser->current_token.line;
    int col = parser->current_token.column;
    parser_advance(parser);

    if (parser->current_token.type != TOKEN_IDENTIFIER) return NULL;
    char* func_name = arena_strdup(parser->arena, parser->current_token.value);
    parser_advance(parser);

    char** param_names = NULL;
    KrtTokenType* param_types = NULL;
    int* param_is_params = NULL;
    int* param_is_array = NULL;
    int param_count = 0;

    if (parser->current_token.type == TOKEN_LEFT_PAREN) {
        parser_advance(parser);
        int param_capacity = PARSER_ARGUMENT_CAPACITY_INIT;
        param_names = (char**)KrtArenaAlloc(parser->arena, param_capacity * sizeof(char*));
        param_types = (KrtTokenType*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(KrtTokenType));
        param_is_params = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
        param_is_array = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));

        while (parser->current_token.type != TOKEN_RIGHT_PAREN &&
               parser->current_token.type != TOKEN_EOF) {
            if (param_count >= param_capacity) {
                param_capacity *= 2;
                char** n_names = (char**)KrtArenaAlloc(parser->arena, param_capacity * sizeof(char*));
                KrtTokenType* n_types = (KrtTokenType*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(KrtTokenType));
                int* n_is_params = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
                int* n_is_array = (int*)KrtArenaAlloc(parser->arena, param_capacity * sizeof(int));
                memcpy(n_names, param_names, param_count * sizeof(char*));
                memcpy(n_types, param_types, param_count * sizeof(KrtTokenType));
                memcpy(n_is_params, param_is_params, param_count * sizeof(int));
                memcpy(n_is_array, param_is_array, param_count * sizeof(int));
                param_names = n_names; param_types = n_types;
                param_is_params = n_is_params; param_is_array = n_is_array;
            }

            KrtTokenType ptype = TOKEN_AUTO;
            char* pname = NULL;

            if (parser_is_type_keyword(parser->current_token.type)) {
                ptype = parser->current_token.type;
                parser_advance(parser);
                if (parser->current_token.type != TOKEN_IDENTIFIER) return NULL;
                pname = arena_strdup(parser->arena, parser->current_token.value);
                parser_advance(parser);
            } else if (parser->current_token.type == TOKEN_IDENTIFIER) {
                Token next = lexer_peek_token(parser->lexer);
                KrtTokenType next_type = next.type;
                token_free(&next);
                if (next_type == TOKEN_IDENTIFIER) {
                    ptype = TOKEN_IDENTIFIER;
                    parser_advance(parser);
                    if (parser->current_token.type != TOKEN_IDENTIFIER) return NULL;
                    pname = arena_strdup(parser->arena, parser->current_token.value);
                    parser_advance(parser);
                } else {
                    pname = arena_strdup(parser->arena, parser->current_token.value);
                    parser_advance(parser);
                }
            } else {
                return NULL;
            }

            param_names[param_count] = pname;
            param_types[param_count] = ptype;
            param_is_params[param_count] = 1;
            param_is_array[param_count] = 0;
            param_count++;

            if (parser->current_token.type == TOKEN_COMMA) parser_advance(parser);
        }

        if (parser->current_token.type != TOKEN_RIGHT_PAREN) return NULL;
        parser_advance(parser);
    }

    ASTNode* body = NULL;
    if (parser->current_token.type == TOKEN_LEFT_BRACE) {
        body = parser_parse_block(parser);
    }
    if (!body) return NULL;

    ASTNode* node = ast_create_node_arena(AST_FUNCTION_DECLARATION, line, col, parser->arena);
    if (!node) return NULL;
    node->data.function_decl.name = func_name;
    node->data.function_decl.parameters = param_names;
    node->data.function_decl.parameter_count = param_count;
    node->data.function_decl.parameter_types = param_types;
    node->data.function_decl.parameter_is_params = param_is_params;
    node->data.function_decl.parameter_is_nullable = NULL;
    node->data.function_decl.parameter_is_array = param_is_array;
    node->data.function_decl.parameter_default_values = NULL;
    node->data.function_decl.body = body;
    node->data.function_decl.return_type = TOKEN_VOID;
    node->data.function_decl.is_async = 0;
    return node;
}

ASTNode* parser_parse_statement(Parser* parser) {
    Token token = parser->current_token;

    switch (token.type) {
        case TOKEN_POINT: {
            int line = token.line;
            int col = token.column;
            parser_advance(parser);

            ASTNode* body = parser_parse_block(parser);
            if (!body) return NULL;

            ASTNode* node = ast_create_node_arena(AST_POINT_BLOCK, line, col, parser->arena);
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
            Token v2 = (v1.type == TOKEN_IDENTIFIER)
                ? lexer_peek_nth_token(parser->lexer, 2) : (Token){0};
            int is_inferred = (v1.type == TOKEN_IDENTIFIER) &&
                (v2.type == TOKEN_ASSIGN || v2.type == TOKEN_SEMICOLON || v2.type == TOKEN_LEFT_BRACKET);
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

            ASTNode* node = ast_create_node_arena(AST_BREAK_STATEMENT, line, col, parser->arena);
            if (!node) return NULL;
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

            ASTNode* node = ast_create_node_arena(AST_CONTINUE_STATEMENT, line, col, parser->arena);
            if (!node) return NULL;
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

            if (parser->current_token.type == TOKEN_IDENTIFIER)
            {
                path_parts[path_count++] = KRT_STRDUP(parser->current_token.value);
                parser_advance(parser);

                while (parser->current_token.type == TOKEN_DOT)
                {
                    parser_advance(parser);
                    if (parser->current_token.type == TOKEN_IDENTIFIER)
                    {
                        if (path_count >= capacity)
                        {
                            capacity *= 2;
                            path_parts = (char**)KRT_REALLOC(path_parts, capacity * sizeof(char*));
                        }
                        path_parts[path_count++] = KRT_STRDUP(parser->current_token.value);
                        parser_advance(parser);
                    }
                    else if (parser->current_token.type == TOKEN_MULTIPLY)
                    {
                        parser_advance(parser);
                        char* wildcard = KRT_STRDUP("*");
                        if (path_count >= capacity)
                        {
                            capacity *= 2;
                            path_parts = (char**)KRT_REALLOC(path_parts, capacity * sizeof(char*));
                        }
                        path_parts[path_count++] = wildcard;
                        break;
                    }
                    else
                    {
                        for (int i = 0; i < path_count; i++) KRT_FREE(path_parts[i]);
                        KRT_FREE(path_parts);
                        return NULL;
                    }
                }

                if (parser->current_token.type == TOKEN_SEMICOLON)
                {
                    parser_advance(parser);
                }

                ASTNode* node = ast_create_node_arena(AST_USING_DIRECTIVE, line, col, parser->arena);
                if (node)
                {
                    node->data.using_directive.alias = alias;
                    node->data.using_directive.namespace_path = path_parts;
                    node->data.using_directive.path_length = path_count;
                    node->data.using_directive.is_alias = 0;
                }
                return node;
            }
            else
            {
                KRT_FREE(path_parts);
                return NULL;
            }
        }

        case TOKEN_VOID:
        case TOKEN_INT8: case TOKEN_INT16: case TOKEN_INT32: case TOKEN_INT64:
        case TOKEN_UINT8: case TOKEN_UINT16: case TOKEN_UINT32: case TOKEN_UINT64:
        case TOKEN_FLOAT32: case TOKEN_FLOAT64:
        case TOKEN_BOOL:
        case TOKEN_TYPE_STRING:
        case TOKEN_CHAR:
        case TOKEN_IDENTIFIER: {
            Token next_token = lexer_peek_token(parser->lexer);
            Token third_token = {0};

            if (next_token.type == TOKEN_IDENTIFIER) {
                third_token = lexer_peek_nth_token(parser->lexer, 2);

                if (third_token.type == TOKEN_LEFT_PAREN) {
                    token_free(&next_token);
                    token_free(&third_token);
                    return parser_parse_function_declaration(parser);
                }
                else {
                    token_free(&next_token);
                    token_free(&third_token);
                    return parser_parse_variable_declaration_with_type(parser);
                }
            } else if (next_token.type == TOKEN_LEFT_BRACKET) {
                Token bracket_token = lexer_peek_nth_token(parser->lexer, 2);
                bool is_array_type_declaration = token.type != TOKEN_IDENTIFIER ||
                                                 bracket_token.type == TOKEN_RIGHT_BRACKET;
                token_free(&next_token);
                token_free(&bracket_token);
                if (is_array_type_declaration) {
                    return parser_parse_variable_declaration_with_type(parser);
                }

                ASTNode* expr = parser_parse_expression(parser);
                if (!expr) return NULL;
                ASTNode* stmt = parser_parse_assignment_from_left(parser, expr);
                if (parser->current_token.type != TOKEN_SEMICOLON) {
                    ast_destroy_node(stmt);
                    return NULL;
                }
                parser_advance(parser);
                return stmt;
            } else if (next_token.type == TOKEN_MULTIPLY) {
                Token name_token = lexer_peek_nth_token(parser->lexer, 2);
                Token after_name = lexer_peek_nth_token(parser->lexer, 3);
                bool is_function = name_token.type == TOKEN_IDENTIFIER &&
                                   after_name.type == TOKEN_LEFT_PAREN;
                token_free(&next_token);
                token_free(&name_token);
                token_free(&after_name);
                return is_function ? parser_parse_function_declaration(parser)
                                   : parser_parse_variable_declaration_with_type(parser);
            } else {
                token_free(&next_token);
                token_free(&third_token);
                ASTNode* expr = parser_parse_expression(parser);
                if (!expr) return NULL;
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
            Token f_n2 = (f_next.type == TOKEN_IDENTIFIER)
                ? lexer_peek_nth_token(parser->lexer, 2) : (Token){0};
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
            if (parser->current_token.type == TOKEN_EOF ||
                parser->current_token.type == TOKEN_UNKNOWN) {
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
