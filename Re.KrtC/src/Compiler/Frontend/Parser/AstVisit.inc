/* Shared traversal keeps specialization consistent with cached AST cloning. */
void KrtAstVisit(ASTNode* node, bool (*visitor)(ASTNode*, void*), void* context) {
    if (!node || !visitor(node, context)) {
        return;
    }
    switch (node->type) {
    case AST_NUMBER:
    case AST_CHAR_LITERAL:
    case AST_BOOLEAN:
    case AST_THIS:
    case AST_NULL:
    case AST_YIELD_BREAK:
        break;
    case AST_STRING:
        break;
    case AST_IDENTIFIER:
        break;
    case AST_FUNCTION_DECLARATION:
    case AST_ASYNC_FUNCTION:
        for (int i = 0;
             node->data.function_decl.parameter_default_values && i < node->data.function_decl.parameter_count; i++) {
            KrtAstVisit(node->data.function_decl.parameter_default_values[i], visitor, context);
        }
        KrtAstVisit(node->data.function_decl.body, visitor, context);
        break;
    case AST_STATIC_FUNCTION_DECLARATION:
        for (int i = 0; node->data.static_function_decl.parameter_default_values &&
                        i < node->data.static_function_decl.parameter_count;
             i++) {
            KrtAstVisit(node->data.static_function_decl.parameter_default_values[i], visitor, context);
        }
        KrtAstVisit(node->data.static_function_decl.body, visitor, context);
        break;
    case AST_VARIABLE_DECLARATION:
        KrtAstVisit(node->data.variable_decl.value, visitor, context);
        KrtAstVisit(node->data.variable_decl.array_size, visitor, context);
        break;
    case AST_STATIC_VARIABLE_DECLARATION:
        KrtAstVisit(node->data.static_variable_decl.value, visitor, context);
        break;
    case AST_ASSIGNMENT:
        KrtAstVisit(node->data.assignment.value, visitor, context);
        break;
    case AST_ARRAY_ASSIGNMENT:
        KrtAstVisit(node->data.array_assignment.array, visitor, context);
        KrtAstVisit(node->data.array_assignment.index, visitor, context);
        KrtAstVisit(node->data.array_assignment.value, visitor, context);
        break;
    case AST_COMPOUND_ASSIGNMENT:
        KrtAstVisit(node->data.compound_assignment.value, visitor, context);
        break;
    case AST_ARRAY_COMPOUND_ASSIGNMENT:
        KrtAstVisit(node->data.array_compound_assignment.array, visitor, context);
        KrtAstVisit(node->data.array_compound_assignment.index, visitor, context);
        KrtAstVisit(node->data.array_compound_assignment.value, visitor, context);
        break;
    case AST_IF_STATEMENT:
        KrtAstVisit(node->data.if_stmt.condition, visitor, context);
        KrtAstVisit(node->data.if_stmt.then_branch, visitor, context);
        KrtAstVisit(node->data.if_stmt.else_branch, visitor, context);
        break;
    case AST_WHILE_STATEMENT:
        KrtAstVisit(node->data.while_stmt.condition, visitor, context);
        KrtAstVisit(node->data.while_stmt.body, visitor, context);
        break;
    case AST_DO_WHILE_STATEMENT:
        KrtAstVisit(node->data.do_while_stmt.body, visitor, context);
        KrtAstVisit(node->data.do_while_stmt.condition, visitor, context);
        break;
    case AST_FOR_STATEMENT:
        KrtAstVisit(node->data.for_stmt.init, visitor, context);
        KrtAstVisit(node->data.for_stmt.condition, visitor, context);
        KrtAstVisit(node->data.for_stmt.increment, visitor, context);
        KrtAstVisit(node->data.for_stmt.body, visitor, context);
        break;
    case AST_FOREACH_STATEMENT:
        KrtAstVisit(node->data.foreach_stmt.iterable, visitor, context);
        KrtAstVisit(node->data.foreach_stmt.body, visitor, context);
        break;
    case AST_RETURN_STATEMENT:
        KrtAstVisit(node->data.return_stmt.value, visitor, context);
        break;
    case AST_PRINT_STATEMENT:
        for (int i = 0; node->data.print_stmt.values && i < node->data.print_stmt.value_count; i++) {
            KrtAstVisit(node->data.print_stmt.values[i], visitor, context);
        }
        break;
    case AST_BINARY_OPERATION:
        KrtAstVisit(node->data.binary_op.left, visitor, context);
        KrtAstVisit(node->data.binary_op.right, visitor, context);
        break;
    case AST_UNARY_OPERATION:
        KrtAstVisit(node->data.unary_op.operand, visitor, context);
        break;
    case AST_TERNARY_OPERATION:
        KrtAstVisit(node->data.ternary_op.condition, visitor, context);
        KrtAstVisit(node->data.ternary_op.true_value, visitor, context);
        KrtAstVisit(node->data.ternary_op.false_value, visitor, context);
        break;
    case AST_CALL:
        for (int i = 0; node->data.call.arguments && i < node->data.call.argument_count; i++) {
            KrtAstVisit(node->data.call.arguments[i], visitor, context);
        }
        KrtAstVisit(node->data.call.object, visitor, context);
        break;
    case AST_PROGRAM:
    case AST_BLOCK:
        for (int i = 0; node->data.block.statements && i < node->data.block.statement_count; i++) {
            KrtAstVisit(node->data.block.statements[i], visitor, context);
        }
        break;
    case AST_STATIC_METHOD_CALL:
        for (int i = 0; node->data.static_call.arguments && i < node->data.static_call.argument_count; i++) {
            KrtAstVisit(node->data.static_call.arguments[i], visitor, context);
        }
        break;
    case AST_ARRAY_ACCESS:
        KrtAstVisit(node->data.array_access.array, visitor, context);
        KrtAstVisit(node->data.array_access.index, visitor, context);
        break;
    case AST_ARRAY_LITERAL:
        for (int i = 0; node->data.array_literal.elements && i < node->data.array_literal.element_count; i++) {
            KrtAstVisit(node->data.array_literal.elements[i], visitor, context);
        }
        break;
    case AST_NEW_EXPRESSION:
        for (int i = 0; node->data.new_expr.arguments && i < node->data.new_expr.argument_count; i++) {
            KrtAstVisit(node->data.new_expr.arguments[i], visitor, context);
        }
        break;
    case AST_NEW_ARRAY_EXPRESSION:
        KrtAstVisit(node->data.new_array_expr.size, visitor, context);
        break;
    case AST_NAMESPACE_DECLARATION:
        KrtAstVisit(node->data.namespace_decl.body, visitor, context);
        break;
    case AST_CLASS_DECLARATION:
        KrtAstVisit(node->data.class_decl.body, visitor, context);
        KrtAstVisit(node->data.class_decl.base_class, visitor, context);
        for (int i = 0; node->data.class_decl.constraints && i < node->data.class_decl.constraint_count; i++) {
            KrtAstVisit(node->data.class_decl.constraints[i], visitor, context);
        }
        break;
    case AST_MEMBER_ACCESS:
        KrtAstVisit(node->data.member_access.object, visitor, context);
        break;
    case AST_ACCESS_MODIFIER:
        KrtAstVisit(node->data.access_modifier.member, visitor, context);
        break;
    case AST_CONSTRUCTOR_DECLARATION:
        KrtAstVisit(node->data.constructor_decl.body, visitor, context);
        for (int i = 0;
             node->data.constructor_decl.base_arguments && i < node->data.constructor_decl.base_argument_count; i++) {
            KrtAstVisit(node->data.constructor_decl.base_arguments[i], visitor, context);
        }
        break;
    case AST_DESTRUCTOR_DECLARATION:
        KrtAstVisit(node->data.destructor_decl.body, visitor, context);
        break;
    case AST_TRY_STATEMENT:
        KrtAstVisit(node->data.try_stmt.try_block, visitor, context);
        for (int i = 0; node->data.try_stmt.catch_clauses && i < node->data.try_stmt.catch_clause_count; i++) {
            KrtAstVisit(node->data.try_stmt.catch_clauses[i], visitor, context);
        }
        KrtAstVisit(node->data.try_stmt.finally_clause, visitor, context);
        break;
    case AST_CATCH_CLAUSE:
        KrtAstVisit(node->data.catch_clause.catch_block, visitor, context);
        break;
    case AST_FINALLY_CLAUSE:
        KrtAstVisit(node->data.finally_clause.finally_block, visitor, context);
        break;
    case AST_THROW_STATEMENT:
        KrtAstVisit(node->data.throw_stmt.exception_expr, visitor, context);
        break;
    case AST_TEMPLATE_DECLARATION:
        for (int i = 0; node->data.template_decl.parameters && i < node->data.template_decl.parameter_count; i++) {
            KrtAstVisit(node->data.template_decl.parameters[i], visitor, context);
        }
        KrtAstVisit(node->data.template_decl.declaration, visitor, context);
        for (int i = 0; node->data.template_decl.constraints && i < node->data.template_decl.constraint_count; i++) {
            KrtAstVisit(node->data.template_decl.constraints[i], visitor, context);
        }
        break;
    case AST_TEMPLATE_PARAMETER:
        break;
    case AST_TEMPLATE_INSTANTIATION:
        for (int i = 0; node->data.template_instantiation.args && i < node->data.template_instantiation.arg_count;
             i++) {
            KrtAstVisit(node->data.template_instantiation.args[i], visitor, context);
        }
        break;
    case AST_GENERIC_TYPE:
        break;
    case AST_GENERIC_CONSTRAINT:
        KrtAstVisit(node->data.generic_constraint.interface_constraint, visitor, context);
        break;
    case AST_SWITCH_STATEMENT:
        KrtAstVisit(node->data.switch_stmt.expression, visitor, context);
        for (int i = 0; node->data.switch_stmt.cases && i < node->data.switch_stmt.case_count; i++) {
            KrtAstVisit(node->data.switch_stmt.cases[i], visitor, context);
        }
        KrtAstVisit(node->data.switch_stmt.default_case, visitor, context);
        break;
    case AST_CASE_CLAUSE:
        KrtAstVisit(node->data.case_clause.value, visitor, context);
        for (int i = 0; node->data.case_clause.statements && i < node->data.case_clause.statement_count; i++) {
            KrtAstVisit(node->data.case_clause.statements[i], visitor, context);
        }
        break;
    case AST_DEFAULT_CLAUSE:
        for (int i = 0; node->data.default_clause.statements && i < node->data.default_clause.statement_count; i++) {
            KrtAstVisit(node->data.default_clause.statements[i], visitor, context);
        }
        break;
    case AST_BREAK_STATEMENT:
        KrtAstVisit(node->data.break_stmt.value, visitor, context);
        break;
    case AST_CONTINUE_STATEMENT:
        KrtAstVisit(node->data.continue_stmt.value, visitor, context);
        break;
    case AST_DELETE_STATEMENT:
        KrtAstVisit(node->data.delete_stmt.value, visitor, context);
        break;
    case AST_USING_STATEMENT:
        KrtAstVisit(node->data.using_stmt.resource, visitor, context);
        KrtAstVisit(node->data.using_stmt.body, visitor, context);
        break;
    case AST_USING_DIRECTIVE:
        break;
    case AST_NAMESPACE_IMPORT:
        break;
    case AST_QUALIFIED_NAME:
        break;
    case AST_PROPERTY_DECLARATION:
        KrtAstVisit(node->data.property_decl.getter, visitor, context);
        KrtAstVisit(node->data.property_decl.setter, visitor, context);
        KrtAstVisit(node->data.property_decl.initial_value, visitor, context);
        for (int i = 0; node->data.property_decl.attributes && i < node->data.property_decl.attribute_count; i++) {
            KrtAstVisit(node->data.property_decl.attributes[i], visitor, context);
        }
        break;
    case AST_PROPERTY_GETTER:
        KrtAstVisit(node->data.property_getter.body, visitor, context);
        break;
    case AST_PROPERTY_SETTER:
        KrtAstVisit(node->data.property_setter.body, visitor, context);
        break;
    case AST_LAMBDA_EXPRESSION:
        KrtAstVisit(node->data.lambda_expr.body, visitor, context);
        KrtAstVisit(node->data.lambda_expr.expression, visitor, context);
        break;
    case AST_LINQ_QUERY:
        KrtAstVisit(node->data.linq_query.from_clause, visitor, context);
        for (int i = 0; node->data.linq_query.clauses && i < node->data.linq_query.clause_count; i++) {
            KrtAstVisit(node->data.linq_query.clauses[i], visitor, context);
        }
        KrtAstVisit(node->data.linq_query.select_clause, visitor, context);
        break;
    case AST_LINQ_FROM:
        KrtAstVisit(node->data.linq_from.source, visitor, context);
        KrtAstVisit(node->data.linq_from.type, visitor, context);
        break;
    case AST_LINQ_WHERE:
        KrtAstVisit(node->data.linq_where.condition, visitor, context);
        break;
    case AST_LINQ_SELECT:
        KrtAstVisit(node->data.linq_select.expression, visitor, context);
        KrtAstVisit(node->data.linq_select.key_selector, visitor, context);
        break;
    case AST_LINQ_ORDERBY:
        KrtAstVisit(node->data.linq_orderby.expression, visitor, context);
        break;
    case AST_LINQ_JOIN:
        KrtAstVisit(node->data.linq_join.source, visitor, context);
        KrtAstVisit(node->data.linq_join.join_source, visitor, context);
        KrtAstVisit(node->data.linq_join.left_key, visitor, context);
        KrtAstVisit(node->data.linq_join.right_key, visitor, context);
        break;
    case AST_LINQ_LET:
        KrtAstVisit(node->data.linq_let.expression, visitor, context);
        break;
    case AST_LINQ_GROUP:
        KrtAstVisit(node->data.linq_group.key_expression, visitor, context);
        KrtAstVisit(node->data.linq_group.element_expression, visitor, context);
        break;
    case AST_ATTRIBUTE:
        for (int i = 0; node->data.attribute.arguments && i < node->data.attribute.argument_count; i++) {
            KrtAstVisit(node->data.attribute.arguments[i], visitor, context);
        }
        KrtAstVisit(node->data.attribute.named_arguments, visitor, context);
        break;
    case AST_ATTRIBUTE_LIST:
        for (int i = 0; node->data.attribute_list.attributes && i < node->data.attribute_list.attribute_count; i++) {
            KrtAstVisit(node->data.attribute_list.attributes[i], visitor, context);
        }
        KrtAstVisit(node->data.attribute_list.target, visitor, context);
        break;
    case AST_UNSAFE_CALL:
        KrtAstVisit(node->data.unsafe_call.expression, visitor, context);
        break;
    case AST_POINT_BLOCK:
        KrtAstVisit(node->data.point_block.body, visitor, context);
        break;
    case AST_DEFAULT_EXPRESSION:
        KrtAstVisit(node->data.default_expr.type_expr, visitor, context);
        break;
    case AST_IS_EXPRESSION:
        KrtAstVisit(node->data.is_expr.expression, visitor, context);
        KrtAstVisit(node->data.is_expr.type_expr, visitor, context);
        break;
    case AST_AS_EXPRESSION:
        KrtAstVisit(node->data.as_expr.expression, visitor, context);
        KrtAstVisit(node->data.as_expr.type_expr, visitor, context);
        break;
    case AST_SIZEOF_EXPRESSION:
        break;
    case AST_STACKALLOC_EXPRESSION:
        KrtAstVisit(node->data.stackalloc_expr.count_expr, visitor, context);
        break;
    case AST_INTERPOLATED_STRING:
        for (int i = 0;
             node->data.interpolated_string.expressions && i < node->data.interpolated_string.expression_count; i++) {
            KrtAstVisit(node->data.interpolated_string.expressions[i], visitor, context);
        }
        break;
    case AST_TUPLE_TYPE:
        break;
    case AST_TUPLE_EXPRESSION:
        for (int i = 0; node->data.tuple_expr.elements && i < node->data.tuple_expr.element_count; i++) {
            KrtAstVisit(node->data.tuple_expr.elements[i], visitor, context);
        }
        break;
    case AST_TUPLE_ELEMENT_ACCESS:
        KrtAstVisit(node->data.tuple_element_access.tuple, visitor, context);
        break;
    case AST_MATCH_EXPRESSION:
        KrtAstVisit(node->data.match_expr.expression, visitor, context);
        for (int i = 0; node->data.match_expr.cases && i < node->data.match_expr.case_count; i++) {
            KrtAstVisit(node->data.match_expr.cases[i], visitor, context);
        }
        break;
    case AST_PATTERN_CASE:
        KrtAstVisit(node->data.pattern_case.pattern, visitor, context);
        KrtAstVisit(node->data.pattern_case.when_clause, visitor, context);
        KrtAstVisit(node->data.pattern_case.body, visitor, context);
        break;
    case AST_PATTERN_WILDCARD:
        break;
    case AST_PATTERN_LITERAL:
        KrtAstVisit(node->data.pattern_literal.value, visitor, context);
        break;
    case AST_PATTERN_VARIABLE:
        break;
    case AST_PATTERN_TUPLE:
        for (int i = 0; node->data.pattern_tuple.elements && i < node->data.pattern_tuple.element_count; i++) {
            KrtAstVisit(node->data.pattern_tuple.elements[i], visitor, context);
        }
        break;
    case AST_POINTER_TYPE:
        break;
    case AST_POINTER_ASSIGNMENT:
        KrtAstVisit(node->data.pointer_assignment.pointer, visitor, context);
        KrtAstVisit(node->data.pointer_assignment.value, visitor, context);
        break;
    case AST_POINTER_DEREFERENCE:
        KrtAstVisit(node->data.pointer_deref.pointer, visitor, context);
        break;
    case AST_ADDRESS_OF:
        KrtAstVisit(node->data.address_of.operand, visitor, context);
        break;
    case AST_FIXED_STATEMENT:
        KrtAstVisit(node->data.fixed_statement.expression, visitor, context);
        KrtAstVisit(node->data.fixed_statement.body, visitor, context);
        break;
    case AST_YIELD_RETURN:
        KrtAstVisit(node->data.yield_return.value, visitor, context);
        break;
    case AST_AWAIT_EXPRESSION:
        KrtAstVisit(node->data.await_expr.expression, visitor, context);
        break;
    case AST_LOCK_STATEMENT:
        KrtAstVisit(node->data.lock_stmt.lock_object, visitor, context);
        KrtAstVisit(node->data.lock_stmt.body, visitor, context);
        break;
    case AST_OPERATOR_OVERLOAD:
        KrtAstVisit(node->data.operator_overload.body, visitor, context);
        break;
    case AST_DELEGATE_DECLARATION:
    case AST_DELEGATE_TYPE:
        break;
    case AST_NULLABLE_TYPE:
        KrtAstVisit(node->data.nullable_type.type_expr, visitor, context);
        break;
    case AST_NULL_COALESCING:
        KrtAstVisit(node->data.null_coalescing.left, visitor, context);
        KrtAstVisit(node->data.null_coalescing.right, visitor, context);
        break;
    case AST_NULL_CONDITIONAL:
        KrtAstVisit(node->data.null_conditional.expression, visitor, context);
        KrtAstVisit(node->data.null_conditional.arguments, visitor, context);
        break;
    case AST_CAST_EXPRESSION:
        KrtAstVisit(node->data.cast_expr.expression, visitor, context);
        break;
    default:
        break;
    }
}
