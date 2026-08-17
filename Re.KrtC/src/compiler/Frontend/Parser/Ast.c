#include "Ast.h"
#include "Accelerator.h"
#include "Core/Utils/KrtCommon.h"
#include "Core/Utils/OutputCache.h"
#include "Core/Memory/Arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

ASTNode* ast_create_node(ASTNodeType type, int line, int col) {
    return ast_create_node_arena(type, line, col, NULL);
}

ASTNode* ast_create_node_arena(ASTNodeType type, int line, int col, KrtArena* arena) {
    ASTNode* node;
    if (arena) {
        node = (ASTNode*)KrtArenaAlloc(arena, sizeof(ASTNode));
    } else {
        node = (ASTNode*)KRT_MALLOC(sizeof(ASTNode));
    }


    node->type = type;
    node->line = line;
    node->col = col;
    node->is_arena_allocated = (arena != NULL) ? 1 : 0;
    memset(&node->data, 0, sizeof(node->data));

    return node;
}

static void free_string_array(char** arr, int count) {
    if (!arr) return;
    for (int i = 0; i < count; i++) {
        KRT_FREE(arr[i]);
    }
    KRT_FREE(arr);
}

static void free_ast_node_array(ASTNode** nodes, int count) {
    if (!nodes) return;
    for (int i = 0; i < count; i++) {
        ast_destroy_node(nodes[i]);
    }
    KRT_FREE(nodes);
}

static void free_function_decl_data(
    const char* name,
    char** parameters, int param_count,
    KrtTokenType* param_types,
    int* param_is_params,
    ASTNode* body
) {
    if (name) KRT_FREE((void*)name);
    free_string_array(parameters, param_count);
    if (param_types) KRT_FREE(param_types);
    if (param_is_params) KRT_FREE(param_is_params);
    if (body) ast_destroy_node(body);
}

void ast_destroy_node(ASTNode* node) {
    if (!node) return;

    if (node->is_arena_allocated) return;

    switch (node->type) {
        case AST_STRING:
            if (node->data.string_value) KRT_FREE(node->data.string_value);
            break;
        case AST_IDENTIFIER:
            if (node->data.identifier_name) KRT_FREE(node->data.identifier_name);
            break;
        case AST_FUNCTION_DECLARATION:
            free_function_decl_data(
                node->data.function_decl.name,
                node->data.function_decl.parameters, node->data.function_decl.parameter_count,
                node->data.function_decl.parameter_types,
                node->data.function_decl.parameter_is_params,
                node->data.function_decl.body
            );
            break;
        case AST_STATIC_FUNCTION_DECLARATION:
            free_function_decl_data(
                node->data.static_function_decl.name,
                node->data.static_function_decl.parameters, node->data.static_function_decl.parameter_count,
                node->data.static_function_decl.parameter_types,
                node->data.static_function_decl.parameter_is_params,
                node->data.static_function_decl.body
            );
            break;
        case AST_VARIABLE_DECLARATION:
            if (node->data.variable_decl.name) KRT_FREE(node->data.variable_decl.name);
            if (node->data.variable_decl.value) ast_destroy_node(node->data.variable_decl.value);
            if (node->data.variable_decl.array_size) ast_destroy_node(node->data.variable_decl.array_size);
            if (node->data.variable_decl.template_instantiation_type) KRT_FREE(node->data.variable_decl.template_instantiation_type);
            break;
        case AST_STATIC_VARIABLE_DECLARATION:
            if (node->data.static_variable_decl.name) KRT_FREE(node->data.static_variable_decl.name);
            if (node->data.static_variable_decl.value) ast_destroy_node(node->data.static_variable_decl.value);
            break;
        case AST_ASSIGNMENT:
            if (node->data.assignment.name) KRT_FREE(node->data.assignment.name);
            ast_destroy_node(node->data.assignment.value);
            break;
        case AST_ARRAY_ASSIGNMENT:
            ast_destroy_node(node->data.array_assignment.array);
            ast_destroy_node(node->data.array_assignment.index);
            ast_destroy_node(node->data.array_assignment.value);
            break;
        case AST_COMPOUND_ASSIGNMENT:
            if (node->data.compound_assignment.name) KRT_FREE(node->data.compound_assignment.name);
            ast_destroy_node(node->data.compound_assignment.value);
            break;
        case AST_ARRAY_COMPOUND_ASSIGNMENT:
            ast_destroy_node(node->data.array_compound_assignment.array);
            ast_destroy_node(node->data.array_compound_assignment.index);
            ast_destroy_node(node->data.array_compound_assignment.value);
            break;
        case AST_IF_STATEMENT:
            ast_destroy_node(node->data.if_stmt.condition);
            ast_destroy_node(node->data.if_stmt.then_branch);
            ast_destroy_node(node->data.if_stmt.else_branch);
            break;
        case AST_WHILE_STATEMENT:
            ast_destroy_node(node->data.while_stmt.condition);
            ast_destroy_node(node->data.while_stmt.body);
            break;
        case AST_FOR_STATEMENT:
            ast_destroy_node(node->data.for_stmt.init);
            ast_destroy_node(node->data.for_stmt.condition);
            ast_destroy_node(node->data.for_stmt.increment);
            ast_destroy_node(node->data.for_stmt.body);
            break;
        case AST_FOREACH_STATEMENT:
            if (node->data.foreach_stmt.var_name) KRT_FREE(node->data.foreach_stmt.var_name);
            ast_destroy_node(node->data.foreach_stmt.iterable);
            ast_destroy_node(node->data.foreach_stmt.body);
            break;
        case AST_RETURN_STATEMENT:
            ast_destroy_node(node->data.return_stmt.value);
            break;
        case AST_PRINT_STATEMENT:
            free_ast_node_array(node->data.print_stmt.values, node->data.print_stmt.value_count);
            break;
        case AST_BINARY_OPERATION:
            ast_destroy_node(node->data.binary_op.left);
            ast_destroy_node(node->data.binary_op.right);
            break;
        case AST_UNARY_OPERATION:
            ast_destroy_node(node->data.unary_op.operand);
            break;
        case AST_CALL:
            if (node->data.call.name) KRT_FREE(node->data.call.name);
            free_ast_node_array(node->data.call.arguments, node->data.call.argument_count);
            free_string_array(node->data.call.argument_names, node->data.call.argument_count);
            if (node->data.call.object) {
                ast_destroy_node(node->data.call.object);
            }
            if (node->data.call.resolved_class_name) KRT_FREE(node->data.call.resolved_class_name);
            if (node->data.call.resolved_mangled_name) KRT_FREE(node->data.call.resolved_mangled_name);
            break;
        case AST_BLOCK:
            free_ast_node_array(node->data.block.statements, node->data.block.statement_count);
            break;
        case AST_ARRAY_LITERAL:
            free_ast_node_array(node->data.array_literal.elements, node->data.array_literal.element_count);
            break;
        case AST_THIS:

            break;
        case AST_MEMBER_ACCESS:
            ast_destroy_node(node->data.member_access.object);
            if (node->data.member_access.member_name) KRT_FREE(node->data.member_access.member_name);
            if (node->data.member_access.resolved_class_name) KRT_FREE(node->data.member_access.resolved_class_name);
            if (node->data.member_access.resolved_mangled_name) KRT_FREE(node->data.member_access.resolved_mangled_name);
            break;
        case AST_STATIC_METHOD_CALL:
            if (node->data.static_call.class_name) KRT_FREE(node->data.static_call.class_name);
            if (node->data.static_call.method_name) KRT_FREE(node->data.static_call.method_name);
            free_ast_node_array(node->data.static_call.arguments, node->data.static_call.argument_count);
            if (node->data.static_call.resolved_mangled_name) KRT_FREE(node->data.static_call.resolved_mangled_name);
            break;
        case AST_ARRAY_ACCESS:
            ast_destroy_node(node->data.array_access.array);
            ast_destroy_node(node->data.array_access.index);
            break;
        case AST_ACCESS_MODIFIER:
            ast_destroy_node(node->data.access_modifier.member);
            break;
        case AST_CONSTRUCTOR_DECLARATION:
            free_string_array(node->data.constructor_decl.parameters, node->data.constructor_decl.parameter_count);
            if (node->data.constructor_decl.parameter_types) KRT_FREE(node->data.constructor_decl.parameter_types);
            ast_destroy_node(node->data.constructor_decl.body);
            break;
        case AST_DESTRUCTOR_DECLARATION:
            if (node->data.destructor_decl.class_name) KRT_FREE(node->data.destructor_decl.class_name);
            ast_destroy_node(node->data.destructor_decl.body);
            break;
        case AST_CLASS_DECLARATION:
            if (node->data.class_decl.name) KRT_FREE(node->data.class_decl.name);
            ast_destroy_node(node->data.class_decl.body);
            if (node->data.class_decl.base_class) ast_destroy_node(node->data.class_decl.base_class);
            free_string_array(node->data.class_decl.template_params, node->data.class_decl.template_param_count);
            free_ast_node_array(node->data.class_decl.constraints, node->data.class_decl.constraint_count);
            break;

        case AST_TRY_STATEMENT:
            ast_destroy_node(node->data.try_stmt.try_block);
            free_ast_node_array(node->data.try_stmt.catch_clauses, node->data.try_stmt.catch_clause_count);
            ast_destroy_node(node->data.try_stmt.finally_clause);
            break;
        case AST_CATCH_CLAUSE:
            if (node->data.catch_clause.exception_type) KRT_FREE(node->data.catch_clause.exception_type);
            if (node->data.catch_clause.exception_var) KRT_FREE(node->data.catch_clause.exception_var);
            ast_destroy_node(node->data.catch_clause.catch_block);
            break;
        case AST_FINALLY_CLAUSE:
            ast_destroy_node(node->data.finally_clause.finally_block);
            break;
        case AST_THROW_STATEMENT:
            ast_destroy_node(node->data.throw_stmt.exception_expr);
            break;

        case AST_TEMPLATE_DECLARATION:
            free_ast_node_array(node->data.template_decl.parameters, node->data.template_decl.parameter_count);
            free_ast_node_array(node->data.template_decl.constraints, node->data.template_decl.constraint_count);
            ast_destroy_node(node->data.template_decl.declaration);
            break;
        case AST_TEMPLATE_PARAMETER:
            if (node->data.template_param.param_name) KRT_FREE(node->data.template_param.param_name);
            break;
        case AST_TEMPLATE_INSTANTIATION:
            if (node->data.template_instantiation.name) KRT_FREE(node->data.template_instantiation.name);
            free_string_array(node->data.template_instantiation.type_args, node->data.template_instantiation.type_arg_count);
            free_ast_node_array(node->data.template_instantiation.args, node->data.template_instantiation.arg_count);
            break;
        case AST_GENERIC_TYPE:
            if (node->data.generic_type.type_name) KRT_FREE(node->data.generic_type.type_name);
            break;
        case AST_GENERIC_CONSTRAINT:
            if (node->data.generic_constraint.param_name) KRT_FREE(node->data.generic_constraint.param_name);
            if (node->data.generic_constraint.constraint_type) KRT_FREE(node->data.generic_constraint.constraint_type);
            if (node->data.generic_constraint.interface_constraint) ast_destroy_node(node->data.generic_constraint.interface_constraint);
            break;
        case AST_USING_STATEMENT:
            if (node->data.using_stmt.resource) ast_destroy_node(node->data.using_stmt.resource);
            if (node->data.using_stmt.body) ast_destroy_node(node->data.using_stmt.body);
            break;
        case AST_USING_DIRECTIVE:
            if (node->data.using_directive.alias) KRT_FREE(node->data.using_directive.alias);
            free_string_array(node->data.using_directive.namespace_path, node->data.using_directive.path_length);
            break;
        case AST_QUALIFIED_NAME:
            free_string_array(node->data.qualified_name.parts, node->data.qualified_name.part_count);
            break;
        case AST_NAMESPACE_IMPORT:
            if (node->data.namespace_import.namespace_name) KRT_FREE(node->data.namespace_import.namespace_name);
            break;
        
        case AST_PROPERTY_DECLARATION:
            if (node->data.property_decl.name) KRT_FREE(node->data.property_decl.name);
            if (node->data.property_decl.getter) ast_destroy_node(node->data.property_decl.getter);
            if (node->data.property_decl.setter) ast_destroy_node(node->data.property_decl.setter);
            if (node->data.property_decl.initial_value) ast_destroy_node(node->data.property_decl.initial_value);
            free_ast_node_array(node->data.property_decl.attributes, node->data.property_decl.attribute_count);
            break;
        case AST_PROPERTY_GETTER:
            if (node->data.property_getter.body) ast_destroy_node(node->data.property_getter.body);
            break;
        case AST_PROPERTY_SETTER:
            if (node->data.property_setter.value_param_name) KRT_FREE(node->data.property_setter.value_param_name);
            if (node->data.property_setter.body) ast_destroy_node(node->data.property_setter.body);
            break;
        case AST_LAMBDA_EXPRESSION:
            free_string_array(node->data.lambda_expr.parameters, node->data.lambda_expr.parameter_count);
            if (node->data.lambda_expr.body) ast_destroy_node(node->data.lambda_expr.body);
            if (node->data.lambda_expr.expression) ast_destroy_node(node->data.lambda_expr.expression);
            break;
        case AST_LINQ_QUERY:
            if (node->data.linq_query.from_clause) ast_destroy_node(node->data.linq_query.from_clause);
            free_ast_node_array(node->data.linq_query.clauses, node->data.linq_query.clause_count);
            if (node->data.linq_query.select_clause) ast_destroy_node(node->data.linq_query.select_clause);
            break;
        case AST_LINQ_FROM:
            if (node->data.linq_from.var_name) KRT_FREE(node->data.linq_from.var_name);
            if (node->data.linq_from.source) ast_destroy_node(node->data.linq_from.source);
            if (node->data.linq_from.type) ast_destroy_node(node->data.linq_from.type);
            break;
        case AST_LINQ_WHERE:
            if (node->data.linq_where.condition) ast_destroy_node(node->data.linq_where.condition);
            break;
        case AST_LINQ_SELECT:
            if (node->data.linq_select.expression) ast_destroy_node(node->data.linq_select.expression);
            if (node->data.linq_select.key_selector) ast_destroy_node(node->data.linq_select.key_selector);
            break;
        case AST_LINQ_ORDERBY:
            if (node->data.linq_orderby.expression) ast_destroy_node(node->data.linq_orderby.expression);
            break;
        case AST_LINQ_JOIN:
            if (node->data.linq_join.var_name) KRT_FREE(node->data.linq_join.var_name);
            if (node->data.linq_join.source) ast_destroy_node(node->data.linq_join.source);
            if (node->data.linq_join.join_var_name) KRT_FREE(node->data.linq_join.join_var_name);
            if (node->data.linq_join.join_source) ast_destroy_node(node->data.linq_join.join_source);
            if (node->data.linq_join.left_key) ast_destroy_node(node->data.linq_join.left_key);
            if (node->data.linq_join.right_key) ast_destroy_node(node->data.linq_join.right_key);
            if (node->data.linq_join.into_var_name) KRT_FREE(node->data.linq_join.into_var_name);
            break;
        case AST_ATTRIBUTE:
            if (node->data.attribute.name) KRT_FREE(node->data.attribute.name);
            free_ast_node_array(node->data.attribute.arguments, node->data.attribute.argument_count);
            if (node->data.attribute.named_arguments) ast_destroy_node(node->data.attribute.named_arguments);
            break;
        case AST_ATTRIBUTE_LIST:
            if (node->data.attribute_list.attributes) {
                for (int i = 0; i < node->data.attribute_list.attribute_count; i++) {
                    ast_destroy_node(node->data.attribute_list.attributes[i]);
                }
                KRT_FREE(node->data.attribute_list.attributes);
            }
            if (node->data.attribute_list.target) ast_destroy_node(node->data.attribute_list.target);
            break;
        case AST_UNSAFE_CALL:
            if (node->data.unsafe_call.expression) ast_destroy_node(node->data.unsafe_call.expression);
            if (node->data.unsafe_call.permissions) {
                for (int i = 0; i < node->data.unsafe_call.permission_count; i++) {
                    KRT_FREE(node->data.unsafe_call.permissions[i]);
                }
                KRT_FREE(node->data.unsafe_call.permissions);
            }
            break;
        case AST_DEFAULT_EXPRESSION:
            if (node->data.default_expr.type_name) KRT_FREE(node->data.default_expr.type_name);
            if (node->data.default_expr.type_expr) ast_destroy_node(node->data.default_expr.type_expr);
            break;
        case AST_IS_EXPRESSION:
            if (node->data.is_expr.expression) ast_destroy_node(node->data.is_expr.expression);
            if (node->data.is_expr.type_name) KRT_FREE(node->data.is_expr.type_name);
            if (node->data.is_expr.type_expr) ast_destroy_node(node->data.is_expr.type_expr);
            break;
        case AST_AS_EXPRESSION:
            if (node->data.as_expr.expression) ast_destroy_node(node->data.as_expr.expression);
            if (node->data.as_expr.type_name) KRT_FREE(node->data.as_expr.type_name);
            if (node->data.as_expr.type_expr) ast_destroy_node(node->data.as_expr.type_expr);
            break;
        case AST_YIELD_RETURN:
            if (node->data.yield_return.value) ast_destroy_node(node->data.yield_return.value);
            break;
        case AST_LOCK_STATEMENT:
            if (node->data.lock_stmt.lock_object) ast_destroy_node(node->data.lock_stmt.lock_object);
            if (node->data.lock_stmt.body) ast_destroy_node(node->data.lock_stmt.body);
            break;
        case AST_OPERATOR_OVERLOAD:
            if (node->data.operator_overload.operator_name) KRT_FREE(node->data.operator_overload.operator_name);
            if (node->data.operator_overload.body) ast_destroy_node(node->data.operator_overload.body);
            break;
        case AST_DELEGATE_DECLARATION:
            if (node->data.delegate_decl.name) KRT_FREE(node->data.delegate_decl.name);
            if (node->data.delegate_decl.parameters) {
                for (int i = 0; i < node->data.delegate_decl.parameter_count; i++) {
                    KRT_FREE(node->data.delegate_decl.parameters[i]);
                }
                KRT_FREE(node->data.delegate_decl.parameters);
            }
            if (node->data.delegate_decl.parameter_types) {
                KRT_FREE(node->data.delegate_decl.parameter_types);
            }
            break;
        default:
            break;
    }

    KRT_FREE(node);
}