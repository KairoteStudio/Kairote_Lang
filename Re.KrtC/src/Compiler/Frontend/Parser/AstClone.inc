/* Cached trees are immutable; every compilation receives its own arena copy. */
static void* clone_ast_data(KrtArena* arena, const void* data, size_t size) {
    if (!data || !size) {
        return NULL;
    }
    void* copy = KrtArenaAlloc(arena, size);
    if (copy) {
        memcpy(copy, data, size);
    }
    return copy;
}

#define KRT_CLONE_STRING(field)                                                                                        \
    do {                                                                                                               \
        if (node->data.field) {                                                                                        \
            copy->data.field = clone_ast_data(arena, node->data.field, strlen(node->data.field) + 1);                  \
            if (!copy->data.field) {                                                                                   \
                return NULL;                                                                                           \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)
#define KRT_CLONE_NODE(field)                                                                                          \
    do {                                                                                                               \
        if (node->data.field) {                                                                                        \
            copy->data.field = KrtAstClone(arena, node->data.field);                                                   \
            if (!copy->data.field) {                                                                                   \
                return NULL;                                                                                           \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)
#define KRT_CLONE_DATA(field, count)                                                                                   \
    do {                                                                                                               \
        if (node->data.field && node->data.count > 0) {                                                                \
            copy->data.field =                                                                                         \
                clone_ast_data(arena, node->data.field, sizeof(*node->data.field) * (size_t)node->data.count);         \
            if (!copy->data.field) {                                                                                   \
                return NULL;                                                                                           \
            }                                                                                                          \
        } else {                                                                                                       \
            copy->data.field = NULL;                                                                                   \
        }                                                                                                              \
    } while (0)
#define KRT_CLONE_NODES(field, count)                                                                                  \
    do {                                                                                                               \
        KRT_CLONE_DATA(field, count);                                                                                  \
        for (int i = 0; copy->data.field && i < node->data.count; i++) {                                               \
            if (node->data.field[i]) {                                                                                 \
                copy->data.field[i] = KrtAstClone(arena, node->data.field[i]);                                         \
                if (!copy->data.field[i]) {                                                                            \
                    return NULL;                                                                                       \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)
#define KRT_CLONE_STRINGS(field, count)                                                                                \
    do {                                                                                                               \
        KRT_CLONE_DATA(field, count);                                                                                  \
        for (int i = 0; copy->data.field && i < node->data.count; i++) {                                               \
            if (node->data.field[i]) {                                                                                 \
                copy->data.field[i] = clone_ast_data(arena, node->data.field[i], strlen(node->data.field[i]) + 1);     \
                if (!copy->data.field[i]) {                                                                            \
                    return NULL;                                                                                       \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

static bool clone_source_type(KrtArena* arena, KrtSourceType type, KrtSourceType* copy) {
    *copy = type;
    if (type.type_name) {
        copy->type_name = clone_ast_data(arena, type.type_name, strlen(type.type_name) + 1);
        if (!copy->type_name) {
            return false;
        }
    }
    if (!type.function) {
        return true;
    }
    KrtFunctionType* function = clone_ast_data(arena, type.function, sizeof(*function));
    if (!function || function->parameter_count < 0 || !clone_source_type(arena, function->result, &function->result)) {
        return false;
    }
    function->parameters =
        clone_ast_data(arena, function->parameters, sizeof(*function->parameters) * (size_t)function->parameter_count);
    if (function->parameter_count && !function->parameters) {
        return false;
    }
    for (int i = 0; i < function->parameter_count; i++) {
        if (!clone_source_type(arena, function->parameters[i], &function->parameters[i])) {
            return false;
        }
    }
    copy->function = function;
    return true;
}

KrtSourceType KrtSourceTypeClone(KrtArena* arena, KrtSourceType type) {
    KrtSourceType copy;
    if (!clone_source_type(arena, type, &copy)) {
        KRT_COMPILE_ERROR("Out of memory while cloning source type");
    }
    return copy;
}

ASTNode* KrtAstClone(KrtArena* arena, const ASTNode* node) {
    if (!arena || !node) {
        return NULL;
    }
    ASTNode* copy = clone_ast_data(arena, node, sizeof(*node));
    if (!copy) {
        return NULL;
    }
    copy->is_arena_allocated = 1;
    if (node->lexical_namespace) {
        copy->lexical_namespace = clone_ast_data(arena, node->lexical_namespace, strlen(node->lexical_namespace) + 1);
        if (!copy->lexical_namespace) {
            return NULL;
        }
    }
    KrtSourceType function;
    if (!clone_source_type(arena, node->declared_type, &copy->declared_type) ||
        !clone_source_type(arena, node->resolved_type, &copy->resolved_type) ||
        !clone_source_type(arena, (KrtSourceType){.function = node->function_type}, &function)) {
        return NULL;
    }
    copy->function_type = function.function;
    switch (node->type) {
    case AST_NUMBER:
    case AST_CHAR_LITERAL:
    case AST_BOOLEAN:
    case AST_THIS:
    case AST_NULL:
    case AST_YIELD_BREAK:
        break;
    case AST_STRING:
        KRT_CLONE_STRING(string_value);
        break;
    case AST_IDENTIFIER:
        KRT_CLONE_STRING(identifier_name);
        break;
    case AST_FUNCTION_DECLARATION:
    case AST_ASYNC_FUNCTION:
        KRT_CLONE_STRING(function_decl.name);
        KRT_CLONE_STRINGS(function_decl.parameters, function_decl.parameter_count);
        KRT_CLONE_DATA(function_decl.parameter_types, function_decl.parameter_count);
        KRT_CLONE_DATA(function_decl.parameter_is_params, function_decl.parameter_count);
        KRT_CLONE_DATA(function_decl.parameter_is_nullable, function_decl.parameter_count);
        KRT_CLONE_DATA(function_decl.parameter_is_array, function_decl.parameter_count);
        KRT_CLONE_NODES(function_decl.parameter_default_values, function_decl.parameter_count);
        KRT_CLONE_NODE(function_decl.body);
        break;
    case AST_STATIC_FUNCTION_DECLARATION:
        KRT_CLONE_STRING(static_function_decl.name);
        KRT_CLONE_STRINGS(static_function_decl.parameters, static_function_decl.parameter_count);
        KRT_CLONE_DATA(static_function_decl.parameter_types, static_function_decl.parameter_count);
        KRT_CLONE_DATA(static_function_decl.parameter_is_params, static_function_decl.parameter_count);
        KRT_CLONE_DATA(static_function_decl.parameter_is_nullable, static_function_decl.parameter_count);
        KRT_CLONE_DATA(static_function_decl.parameter_is_array, static_function_decl.parameter_count);
        KRT_CLONE_NODES(static_function_decl.parameter_default_values, static_function_decl.parameter_count);
        KRT_CLONE_NODE(static_function_decl.body);
        break;
    case AST_VARIABLE_DECLARATION:
        KRT_CLONE_STRING(variable_decl.name);
        KRT_CLONE_NODE(variable_decl.value);
        KRT_CLONE_NODE(variable_decl.array_size);
        KRT_CLONE_STRING(variable_decl.template_instantiation_type);
        break;
    case AST_STATIC_VARIABLE_DECLARATION:
        KRT_CLONE_STRING(static_variable_decl.name);
        KRT_CLONE_NODE(static_variable_decl.value);
        break;
    case AST_ASSIGNMENT:
        KRT_CLONE_STRING(assignment.name);
        KRT_CLONE_NODE(assignment.value);
        break;
    case AST_ARRAY_ASSIGNMENT:
        KRT_CLONE_NODE(array_assignment.array);
        KRT_CLONE_NODE(array_assignment.index);
        KRT_CLONE_NODE(array_assignment.value);
        break;
    case AST_COMPOUND_ASSIGNMENT:
        KRT_CLONE_STRING(compound_assignment.name);
        KRT_CLONE_NODE(compound_assignment.value);
        break;
    case AST_ARRAY_COMPOUND_ASSIGNMENT:
        KRT_CLONE_NODE(array_compound_assignment.array);
        KRT_CLONE_NODE(array_compound_assignment.index);
        KRT_CLONE_NODE(array_compound_assignment.value);
        break;
    case AST_IF_STATEMENT:
        KRT_CLONE_NODE(if_stmt.condition);
        KRT_CLONE_NODE(if_stmt.then_branch);
        KRT_CLONE_NODE(if_stmt.else_branch);
        break;
    case AST_WHILE_STATEMENT:
        KRT_CLONE_NODE(while_stmt.condition);
        KRT_CLONE_NODE(while_stmt.body);
        break;
    case AST_DO_WHILE_STATEMENT:
        KRT_CLONE_NODE(do_while_stmt.body);
        KRT_CLONE_NODE(do_while_stmt.condition);
        break;
    case AST_FOR_STATEMENT:
        KRT_CLONE_NODE(for_stmt.init);
        KRT_CLONE_NODE(for_stmt.condition);
        KRT_CLONE_NODE(for_stmt.increment);
        KRT_CLONE_NODE(for_stmt.body);
        break;
    case AST_FOREACH_STATEMENT:
        KRT_CLONE_STRING(foreach_stmt.var_name);
        KRT_CLONE_NODE(foreach_stmt.iterable);
        KRT_CLONE_NODE(foreach_stmt.body);
        break;
    case AST_RETURN_STATEMENT:
        KRT_CLONE_NODE(return_stmt.value);
        break;
    case AST_PRINT_STATEMENT:
        KRT_CLONE_NODES(print_stmt.values, print_stmt.value_count);
        break;
    case AST_BINARY_OPERATION:
        KRT_CLONE_NODE(binary_op.left);
        KRT_CLONE_NODE(binary_op.right);
        break;
    case AST_UNARY_OPERATION:
        KRT_CLONE_NODE(unary_op.operand);
        break;
    case AST_TERNARY_OPERATION:
        KRT_CLONE_NODE(ternary_op.condition);
        KRT_CLONE_NODE(ternary_op.true_value);
        KRT_CLONE_NODE(ternary_op.false_value);
        break;
    case AST_CALL:
        KRT_CLONE_STRING(call.name);
        KRT_CLONE_NODES(call.arguments, call.argument_count);
        KRT_CLONE_STRINGS(call.argument_names, call.argument_count);
        KRT_CLONE_NODE(call.object);
        KRT_CLONE_STRING(call.resolved_class_name);
        KRT_CLONE_STRING(call.resolved_mangled_name);
        break;
    case AST_PROGRAM:
    case AST_BLOCK:
        KRT_CLONE_NODES(block.statements, block.statement_count);
        break;
    case AST_STATIC_METHOD_CALL:
        KRT_CLONE_STRING(static_call.class_name);
        KRT_CLONE_STRING(static_call.method_name);
        KRT_CLONE_NODES(static_call.arguments, static_call.argument_count);
        KRT_CLONE_STRING(static_call.resolved_mangled_name);
        break;
    case AST_ARRAY_ACCESS:
        KRT_CLONE_NODE(array_access.array);
        KRT_CLONE_NODE(array_access.index);
        break;
    case AST_ARRAY_LITERAL:
        KRT_CLONE_NODES(array_literal.elements, array_literal.element_count);
        break;
    case AST_NEW_EXPRESSION:
        KRT_CLONE_STRING(new_expr.class_name);
        KRT_CLONE_NODES(new_expr.arguments, new_expr.argument_count);
        KRT_CLONE_STRINGS(new_expr.argument_names, new_expr.argument_count);
        break;
    case AST_NEW_ARRAY_EXPRESSION:
        KRT_CLONE_STRING(new_array_expr.element_type);
        KRT_CLONE_NODE(new_array_expr.size);
        break;
    case AST_NAMESPACE_DECLARATION:
        KRT_CLONE_STRING(namespace_decl.name);
        KRT_CLONE_NODE(namespace_decl.body);
        break;
    case AST_CLASS_DECLARATION:
        KRT_CLONE_STRING(class_decl.name);
        KRT_CLONE_NODE(class_decl.body);
        KRT_CLONE_NODE(class_decl.base_class);
        KRT_CLONE_STRINGS(class_decl.template_params, class_decl.template_param_count);
        KRT_CLONE_NODES(class_decl.constraints, class_decl.constraint_count);
        break;
    case AST_MEMBER_ACCESS:
        KRT_CLONE_NODE(member_access.object);
        KRT_CLONE_STRING(member_access.member_name);
        KRT_CLONE_STRING(member_access.resolved_class_name);
        KRT_CLONE_STRING(member_access.resolved_mangled_name);
        break;
    case AST_ACCESS_MODIFIER:
        KRT_CLONE_NODE(access_modifier.member);
        break;
    case AST_CONSTRUCTOR_DECLARATION:
        KRT_CLONE_STRINGS(constructor_decl.parameters, constructor_decl.parameter_count);
        KRT_CLONE_DATA(constructor_decl.parameter_types, constructor_decl.parameter_count);
        KRT_CLONE_NODE(constructor_decl.body);
        KRT_CLONE_NODES(constructor_decl.base_arguments, constructor_decl.base_argument_count);
        break;
    case AST_DESTRUCTOR_DECLARATION:
        KRT_CLONE_STRING(destructor_decl.class_name);
        KRT_CLONE_NODE(destructor_decl.body);
        break;
    case AST_TRY_STATEMENT:
        KRT_CLONE_NODE(try_stmt.try_block);
        KRT_CLONE_NODES(try_stmt.catch_clauses, try_stmt.catch_clause_count);
        KRT_CLONE_NODE(try_stmt.finally_clause);
        break;
    case AST_CATCH_CLAUSE:
        KRT_CLONE_STRING(catch_clause.exception_type);
        KRT_CLONE_STRING(catch_clause.exception_var);
        KRT_CLONE_NODE(catch_clause.catch_block);
        break;
    case AST_FINALLY_CLAUSE:
        KRT_CLONE_NODE(finally_clause.finally_block);
        break;
    case AST_THROW_STATEMENT:
        KRT_CLONE_NODE(throw_stmt.exception_expr);
        break;
    case AST_TEMPLATE_DECLARATION:
        KRT_CLONE_NODES(template_decl.parameters, template_decl.parameter_count);
        KRT_CLONE_NODE(template_decl.declaration);
        KRT_CLONE_NODES(template_decl.constraints, template_decl.constraint_count);
        break;
    case AST_TEMPLATE_PARAMETER:
        KRT_CLONE_STRING(template_param.param_name);
        break;
    case AST_TEMPLATE_INSTANTIATION:
        KRT_CLONE_STRING(template_instantiation.name);
        KRT_CLONE_STRINGS(template_instantiation.type_args, template_instantiation.type_arg_count);
        KRT_CLONE_NODES(template_instantiation.args, template_instantiation.arg_count);
        break;
    case AST_GENERIC_TYPE:
        KRT_CLONE_STRING(generic_type.type_name);
        break;
    case AST_GENERIC_CONSTRAINT:
        KRT_CLONE_STRING(generic_constraint.param_name);
        KRT_CLONE_STRING(generic_constraint.constraint_type);
        KRT_CLONE_NODE(generic_constraint.interface_constraint);
        break;
    case AST_SWITCH_STATEMENT:
        KRT_CLONE_NODE(switch_stmt.expression);
        KRT_CLONE_NODES(switch_stmt.cases, switch_stmt.case_count);
        KRT_CLONE_NODE(switch_stmt.default_case);
        break;
    case AST_CASE_CLAUSE:
        KRT_CLONE_NODE(case_clause.value);
        KRT_CLONE_NODES(case_clause.statements, case_clause.statement_count);
        break;
    case AST_DEFAULT_CLAUSE:
        KRT_CLONE_NODES(default_clause.statements, default_clause.statement_count);
        break;
    case AST_BREAK_STATEMENT:
        KRT_CLONE_NODE(break_stmt.value);
        KRT_CLONE_STRING(break_stmt.resolved_class_name);
        break;
    case AST_CONTINUE_STATEMENT:
        KRT_CLONE_NODE(continue_stmt.value);
        break;
    case AST_DELETE_STATEMENT:
        KRT_CLONE_NODE(delete_stmt.value);
        KRT_CLONE_STRING(delete_stmt.resolved_class_name);
        break;
    case AST_USING_STATEMENT:
        KRT_CLONE_NODE(using_stmt.resource);
        KRT_CLONE_NODE(using_stmt.body);
        break;
    case AST_USING_DIRECTIVE:
        KRT_CLONE_STRING(using_directive.alias);
        KRT_CLONE_STRINGS(using_directive.namespace_path, using_directive.path_length);
        break;
    case AST_NAMESPACE_IMPORT:
        KRT_CLONE_STRING(namespace_import.namespace_name);
        break;
    case AST_QUALIFIED_NAME:
        KRT_CLONE_STRINGS(qualified_name.parts, qualified_name.part_count);
        break;
    case AST_PROPERTY_DECLARATION:
        KRT_CLONE_STRING(property_decl.name);
        KRT_CLONE_NODE(property_decl.getter);
        KRT_CLONE_NODE(property_decl.setter);
        KRT_CLONE_NODE(property_decl.initial_value);
        KRT_CLONE_NODES(property_decl.attributes, property_decl.attribute_count);
        KRT_CLONE_STRING(property_decl.backing_field_name);
        break;
    case AST_PROPERTY_GETTER:
        KRT_CLONE_NODE(property_getter.body);
        break;
    case AST_PROPERTY_SETTER:
        KRT_CLONE_STRING(property_setter.value_param_name);
        KRT_CLONE_NODE(property_setter.body);
        break;
    case AST_LAMBDA_EXPRESSION:
        KRT_CLONE_STRINGS(lambda_expr.parameters, lambda_expr.parameter_count);
        KRT_CLONE_NODE(lambda_expr.body);
        KRT_CLONE_NODE(lambda_expr.expression);
        break;
    case AST_LINQ_QUERY:
        KRT_CLONE_NODE(linq_query.from_clause);
        KRT_CLONE_NODES(linq_query.clauses, linq_query.clause_count);
        KRT_CLONE_NODE(linq_query.select_clause);
        break;
    case AST_LINQ_FROM:
        KRT_CLONE_STRING(linq_from.var_name);
        KRT_CLONE_NODE(linq_from.source);
        KRT_CLONE_NODE(linq_from.type);
        break;
    case AST_LINQ_WHERE:
        KRT_CLONE_NODE(linq_where.condition);
        break;
    case AST_LINQ_SELECT:
        KRT_CLONE_NODE(linq_select.expression);
        KRT_CLONE_NODE(linq_select.key_selector);
        break;
    case AST_LINQ_ORDERBY:
        KRT_CLONE_NODE(linq_orderby.expression);
        break;
    case AST_LINQ_JOIN:
        KRT_CLONE_STRING(linq_join.var_name);
        KRT_CLONE_NODE(linq_join.source);
        KRT_CLONE_STRING(linq_join.join_var_name);
        KRT_CLONE_NODE(linq_join.join_source);
        KRT_CLONE_NODE(linq_join.left_key);
        KRT_CLONE_NODE(linq_join.right_key);
        KRT_CLONE_STRING(linq_join.into_var_name);
        break;
    case AST_LINQ_LET:
        KRT_CLONE_STRING(linq_let.var_name);
        KRT_CLONE_NODE(linq_let.expression);
        break;
    case AST_LINQ_GROUP:
        KRT_CLONE_NODE(linq_group.key_expression);
        KRT_CLONE_NODE(linq_group.element_expression);
        KRT_CLONE_STRING(linq_group.into_var_name);
        break;
    case AST_ATTRIBUTE:
        KRT_CLONE_STRING(attribute.name);
        KRT_CLONE_NODES(attribute.arguments, attribute.argument_count);
        KRT_CLONE_NODE(attribute.named_arguments);
        break;
    case AST_ATTRIBUTE_LIST:
        KRT_CLONE_NODES(attribute_list.attributes, attribute_list.attribute_count);
        KRT_CLONE_NODE(attribute_list.target);
        break;
    case AST_UNSAFE_CALL:
        KRT_CLONE_NODE(unsafe_call.expression);
        KRT_CLONE_STRINGS(unsafe_call.permissions, unsafe_call.permission_count);
        break;
    case AST_POINT_BLOCK:
        KRT_CLONE_NODE(point_block.body);
        break;
    case AST_DEFAULT_EXPRESSION:
        KRT_CLONE_STRING(default_expr.type_name);
        KRT_CLONE_NODE(default_expr.type_expr);
        break;
    case AST_IS_EXPRESSION:
        KRT_CLONE_STRING(is_expr.binding_name);
        KRT_CLONE_NODE(is_expr.expression);
        KRT_CLONE_STRING(is_expr.type_name);
        KRT_CLONE_NODE(is_expr.type_expr);
        break;
    case AST_AS_EXPRESSION:
        KRT_CLONE_NODE(as_expr.expression);
        KRT_CLONE_STRING(as_expr.type_name);
        KRT_CLONE_NODE(as_expr.type_expr);
        break;
    case AST_SIZEOF_EXPRESSION:
        KRT_CLONE_STRING(sizeof_expr.type_name);
        break;
    case AST_STACKALLOC_EXPRESSION:
        KRT_CLONE_STRING(stackalloc_expr.type_name);
        KRT_CLONE_NODE(stackalloc_expr.count_expr);
        break;
    case AST_INTERPOLATED_STRING:
        KRT_CLONE_STRINGS(interpolated_string.string_parts, interpolated_string.part_count);
        KRT_CLONE_NODES(interpolated_string.expressions, interpolated_string.expression_count);
        break;
    case AST_TUPLE_TYPE:
        KRT_CLONE_DATA(tuple_type.element_types, tuple_type.element_count);
        break;
    case AST_TUPLE_EXPRESSION:
        KRT_CLONE_NODES(tuple_expr.elements, tuple_expr.element_count);
        break;
    case AST_TUPLE_ELEMENT_ACCESS:
        KRT_CLONE_NODE(tuple_element_access.tuple);
        break;
    case AST_MATCH_EXPRESSION:
        KRT_CLONE_NODE(match_expr.expression);
        KRT_CLONE_NODES(match_expr.cases, match_expr.case_count);
        break;
    case AST_PATTERN_CASE:
        KRT_CLONE_NODE(pattern_case.pattern);
        KRT_CLONE_NODE(pattern_case.when_clause);
        KRT_CLONE_NODE(pattern_case.body);
        break;
    case AST_PATTERN_WILDCARD:
        break;
    case AST_PATTERN_LITERAL:
        KRT_CLONE_NODE(pattern_literal.value);
        break;
    case AST_PATTERN_VARIABLE:
        KRT_CLONE_STRING(pattern_variable.name);
        break;
    case AST_PATTERN_TUPLE:
        KRT_CLONE_NODES(pattern_tuple.elements, pattern_tuple.element_count);
        break;
    case AST_POINTER_TYPE:
        break;
    case AST_POINTER_ASSIGNMENT:
        KRT_CLONE_NODE(pointer_assignment.pointer);
        KRT_CLONE_NODE(pointer_assignment.value);
        break;
    case AST_POINTER_DEREFERENCE:
        KRT_CLONE_NODE(pointer_deref.pointer);
        break;
    case AST_ADDRESS_OF:
        KRT_CLONE_NODE(address_of.operand);
        break;
    case AST_FIXED_STATEMENT:
        KRT_CLONE_STRING(fixed_statement.variable_name);
        KRT_CLONE_NODE(fixed_statement.expression);
        KRT_CLONE_NODE(fixed_statement.body);
        break;
    case AST_YIELD_RETURN:
        KRT_CLONE_NODE(yield_return.value);
        break;
    case AST_AWAIT_EXPRESSION:
        KRT_CLONE_NODE(await_expr.expression);
        break;
    case AST_LOCK_STATEMENT:
        KRT_CLONE_NODE(lock_stmt.lock_object);
        KRT_CLONE_NODE(lock_stmt.body);
        break;
    case AST_OPERATOR_OVERLOAD:
        KRT_CLONE_STRING(operator_overload.operator_name);
        KRT_CLONE_NODE(operator_overload.body);
        break;
    case AST_DELEGATE_DECLARATION:
    case AST_DELEGATE_TYPE:
        KRT_CLONE_STRING(delegate_decl.name);
        KRT_CLONE_STRINGS(delegate_decl.parameters, delegate_decl.parameter_count);
        KRT_CLONE_DATA(delegate_decl.parameter_types, delegate_decl.parameter_count);
        break;
    case AST_NULLABLE_TYPE:
        KRT_CLONE_NODE(nullable_type.type_expr);
        break;
    case AST_NULL_COALESCING:
        KRT_CLONE_NODE(null_coalescing.left);
        KRT_CLONE_NODE(null_coalescing.right);
        break;
    case AST_NULL_CONDITIONAL:
        KRT_CLONE_NODE(null_conditional.expression);
        KRT_CLONE_STRING(null_conditional.member_name);
        KRT_CLONE_NODE(null_conditional.arguments);
        break;
    case AST_CAST_EXPRESSION:
        KRT_CLONE_NODE(cast_expr.expression);
        break;
    default:
        return NULL;
    }
    return copy;
}

#undef KRT_CLONE_STRING
#undef KRT_CLONE_NODE
#undef KRT_CLONE_DATA
#undef KRT_CLONE_NODES
#undef KRT_CLONE_STRINGS
