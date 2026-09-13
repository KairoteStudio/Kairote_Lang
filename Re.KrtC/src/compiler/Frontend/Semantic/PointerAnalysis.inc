static bool semantic_expression_impl(SemanticAnalyzer* analyzer, ASTNode* expr);

static bool source_type_equal(KrtSourceType a, KrtSourceType b) {
    if (a.is_ref != b.is_ref) {
        return false;
    }
    if (a.token == TOKEN_IDENTIFIER && b.token == TOKEN_IDENTIFIER && a.type_name && b.type_name &&
        strcmp(a.type_name, b.type_name)) {
        return false;
    }
    if (a.pointer_depth != b.pointer_depth || a.nullable != b.nullable || a.pointee_nullable != b.pointee_nullable ||
        !!a.function != !!b.function) {
        return false;
    }
    if (!a.function) {
        return a.token == b.token;
    }
    if (a.function->parameter_count != b.function->parameter_count ||
        !source_type_equal(a.function->result, b.function->result)) {
        return false;
    }
    for (int i = 0; i < a.function->parameter_count; i++) {
        if (!source_type_equal(a.function->parameters[i], b.function->parameters[i])) {
            return false;
        }
    }
    return true;
}

static bool pointer_error(SemanticAnalyzer* analyzer, ASTNode* node, const char* message) {
    semantic_analyzer_add_error_at(analyzer, node, "%s", message);
    return false;
}

static bool require_memory_permission(SemanticAnalyzer* analyzer, ASTNode* node) {
    return analyzer->unsafe_mem || analyzer->point_depth ||
           pointer_error(analyzer, node, "Pointer operation requires unsafe(using krt.mem;) { ... }");
}

static bool pointer_assignable(SemanticAnalyzer* analyzer, ASTNode* node, KrtSourceType target, KrtSourceType value) {
    target.is_ref = value.is_ref = false;
    if (!KrtSourceIsPointer(target) && !KrtSourceIsPointer(value)) {
        if (target.type_name && value.type_name && (strchr(target.type_name, '<') || strchr(value.type_name, '<')) &&
            strcmp(target.type_name, value.type_name)) {
            return pointer_error(analyzer, node, "Incompatible generic instance types");
        }
        return true;
    }
    if (value.token == TOKEN_NULL && !KrtSourceIsPointer(value)) {
        return target.nullable || pointer_error(analyzer, node, "null requires a nullable pointer type");
    }
    if (value.nullable && !target.nullable) {
        return pointer_error(analyzer, node, "Nullable pointer must be narrowed before use");
    }
    if (value.pointee_nullable != target.pointee_nullable) {
        return pointer_error(analyzer, node, "Incompatible pointee nullability");
    }
    target.nullable = value.nullable = false;
    if (source_type_equal(target, value)) {
        return true;
    }
    if (!target.function && !value.function && target.token == TOKEN_VOID && target.pointer_depth == 1 &&
        value.pointer_depth) {
        return true;
    }
    return pointer_error(analyzer, node, "Incompatible pointer or function pointer types; use an explicit cast");
}

static bool call_argument_assignable(SemanticAnalyzer* analyzer, ASTNode* arg, KrtSourceType parameter) {
    if (!parameter.is_ref) {
        if (arg->is_ref_argument) {
            return pointer_error(analyzer, arg, "ref argument supplied to a value parameter");
        }
        return pointer_assignable(analyzer, arg, parameter, arg->resolved_type);
    }
    parameter.is_ref = false;
    if ((arg->type != AST_IDENTIFIER && arg->type != AST_ARRAY_ACCESS && arg->type != AST_POINTER_DEREFERENCE) ||
        arg->is_function_reference) {
        return pointer_error(analyzer, arg, "ref argument requires a writable variable or memory lvalue");
    }
    if (arg->type == AST_IDENTIFIER) {
        SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, arg->data.identifier_name);
        if (!symbol || symbol->is_array) {
            return pointer_error(analyzer, arg, "ref requires a scalar slot; pass an array element instead");
        }
    }
    if (!source_type_equal(parameter, arg->resolved_type)) {
        return pointer_error(analyzer, arg, "ref argument type must match exactly, including pointer nullability");
    }
    arg->is_ref_argument = true;
    return true;
}

static bool pointer_pointee(SemanticAnalyzer* analyzer, ASTNode* node, KrtSourceType pointer, KrtSourceType* element) {
    if (!require_memory_permission(analyzer, node)) {
        return false;
    }
    if (pointer.nullable) {
        return pointer_error(analyzer, node, "Nullable pointer must be narrowed before dereference or arithmetic");
    }
    if (!pointer.pointer_depth) {
        return pointer_error(analyzer, node, "Expected a data pointer");
    }
    *element = pointer;
    element->pointer_depth--;
    element->nullable = pointer.pointee_nullable & 1;
    element->pointee_nullable >>= 1;
    if (!element->pointer_depth && !element->function && element->token == TOKEN_VOID) {
        return pointer_error(analyzer, node, "void* has no element size; cast to a typed pointer first");
    }
    return true;
}

static bool pointer_update_assignable(SemanticAnalyzer* analyzer, ASTNode* node, KrtSourceType target,
                                      KrtSourceType value, KrtTokenType op) {
    if (op == TOKEN_ASSIGN) {
        return pointer_assignable(analyzer, node, target, value);
    }
    if (KrtSourceIsPointer(target)) {
        KrtSourceType element;
        if ((op != TOKEN_PLUS_ASSIGN && op != TOKEN_MINUS_ASSIGN) || KrtSourceIsPointer(value) ||
            !KrtTokenIntegerBits(value.token)) {
            return pointer_error(analyzer, node, "Pointer compound assignment requires += or -= integer");
        }
        return pointer_pointee(analyzer, node, target, &element);
    }
    if ((target.token == TOKEN_FLOAT32 || target.token == TOKEN_FLOAT64) && op == TOKEN_MOD_ASSIGN) {
        return pointer_error(analyzer, node, "Floating remainder assignment is not supported");
    }
    if (KrtTokenIsBitwiseAssignment(op) &&
        (!KrtTokenIntegerBits(target.token) || !KrtTokenIntegerBits(value.token) || KrtSourceIsPointer(value))) {
        return pointer_error(analyzer, node, "Bitwise compound assignment requires integer operands");
    }
    return pointer_assignable(analyzer, node, target, value);
}

static bool analyze_pointer_index(SemanticAnalyzer* analyzer, ASTNode* node, ASTNode* array, ASTNode* index,
                                  ASTNode* value) {
    KrtSourceType element;
    if (!pointer_pointee(analyzer, node, array->resolved_type, &element)) {
        return false;
    }
    if (!KrtTokenIntegerBits(KrtSourceStorage(index->resolved_type)) || KrtSourceIsPointer(index->resolved_type)) {
        return pointer_error(analyzer, node, "Pointer index must be an integer");
    }
    node->resolved_type = element;
    return !value || pointer_assignable(analyzer, node, element, value->resolved_type);
}

bool semantic_analyzer_analyze_expression(SemanticAnalyzer* analyzer, ASTNode* expr) {
    if (!analyzer || !expr) {
        return false;
    }
    switch (expr->type) {
    case AST_ADDRESS_OF: {
        ASTNode* operand = expr->data.address_of.operand;
        if (!require_memory_permission(analyzer, expr) || !semantic_analyzer_analyze_expression(analyzer, operand)) {
            return false;
        }
        if (operand->type != AST_IDENTIFIER && operand->type != AST_POINTER_DEREFERENCE &&
            operand->type != AST_ARRAY_ACCESS) {
            return pointer_error(analyzer, expr, "Address operand must be a variable, element, or function");
        }
        expr->resolved_type = operand->resolved_type;
        bool function_symbol = false;
        if (operand->type == AST_IDENTIFIER) {
            SymbolEntry* symbol =
                symbol_table_lookup_scope_chain(analyzer->symbol_table, operand->data.identifier_name);
            function_symbol = symbol && symbol->type == SYMBOL_FUNCTION;
        }
        if (!function_symbol) {
            if (expr->resolved_type.pointer_depth >= 64) {
                return pointer_error(analyzer, expr, "Pointer nesting exceeds 64 levels");
            }
            expr->resolved_type.pointee_nullable =
                (expr->resolved_type.pointee_nullable << 1) | expr->resolved_type.nullable;
            expr->resolved_type.nullable = false;
            expr->resolved_type.pointer_depth++;
        }
        return true;
    }
    case AST_POINTER_DEREFERENCE:
        return semantic_analyzer_analyze_expression(analyzer, expr->data.pointer_deref.pointer) &&
               pointer_pointee(analyzer, expr, expr->data.pointer_deref.pointer->resolved_type, &expr->resolved_type);
    case AST_POINTER_ASSIGNMENT: {
        ASTNode* pointer = expr->data.pointer_assignment.pointer;
        ASTNode* value = expr->data.pointer_assignment.value;
        return semantic_analyzer_analyze_expression(analyzer, pointer) &&
               semantic_analyzer_analyze_expression(analyzer, value) &&
               pointer_pointee(analyzer, expr, pointer->resolved_type, &expr->resolved_type) &&
               pointer_update_assignable(analyzer, expr, expr->resolved_type, value->resolved_type,
                                         expr->data.pointer_assignment.operator);
    }
    case AST_STACKALLOC_EXPRESSION: {
        ASTNode* count = expr->data.stackalloc_expr.count_expr;
        if (!require_memory_permission(analyzer, expr) || !semantic_analyzer_analyze_expression(analyzer, count)) {
            return false;
        }
        if (!KrtTokenIntegerBits(KrtSourceStorage(count->resolved_type)) || KrtSourceIsPointer(count->resolved_type)) {
            return pointer_error(analyzer, expr, "stackalloc count must be an integer");
        }
        if ((count->type == AST_UNARY_OPERATION && count->data.unary_op.operator== TOKEN_MINUS &&
                                                   count->data.unary_op.operand->type == AST_NUMBER &&
                                                   count->data.unary_op.operand->data.number_value> 0) ||
            (!KrtSourceIsPointer(expr->declared_type) && !KrtTokenIntegerBits(expr->declared_type.token) &&
             expr->declared_type.token != TOKEN_FLOAT32 && expr->declared_type.token != TOKEN_FLOAT64 &&
             expr->declared_type.token != TOKEN_BOOL && expr->declared_type.token != TOKEN_CHAR)) {
            return pointer_error(analyzer, expr, "stackalloc requires a sized type and a nonnegative count");
        }
        expr->resolved_type = expr->declared_type;
        if (expr->resolved_type.pointer_depth >= 64) {
            return pointer_error(analyzer, expr, "Pointer nesting exceeds 64 levels");
        }
        expr->resolved_type.pointee_nullable =
            (expr->resolved_type.pointee_nullable << 1) | expr->resolved_type.nullable;
        expr->resolved_type.nullable = false;
        expr->resolved_type.pointer_depth++;
        return true;
    }
    case AST_IS_EXPRESSION: {
        ASTNode* value = expr->data.is_expr.expression;
        if (!semantic_analyzer_analyze_expression(analyzer, value)) {
            return false;
        }
        KrtSourceType source = value->resolved_type, target = expr->declared_type;
        source.nullable = false;
        if (!KrtSourceIsPointer(target) || target.nullable || !source_type_equal(source, target)) {
            return pointer_error(analyzer, expr, "Pointer pattern must name the same non-nullable pointer type");
        }
        expr->resolved_type = (KrtSourceType){.token = TOKEN_BOOL};
        return true;
    }
    case AST_CALL: {
        SymbolEntry* symbol = !expr->data.call.object
                                  ? symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.call.name)
                                  : NULL;
        KrtSourceType callee = symbol ? symbol->source_type : (KrtSourceType){0};
        if (expr->data.call.object && !strcmp(expr->data.call.name, "__expr_call__")) {
            if (!semantic_analyzer_analyze_expression(analyzer, expr->data.call.object)) {
                return false;
            }
            callee = expr->data.call.object->resolved_type;
        }
        if (callee.function && !callee.pointer_depth) {
            expr->is_indirect_call = !symbol || symbol->type != SYMBOL_FUNCTION;
            if (expr->is_indirect_call && (!require_memory_permission(analyzer, expr) || callee.nullable)) {
                return pointer_error(analyzer, expr,
                                     "Function pointer call requires a non-null pointer in unsafe code");
            }
            KrtFunctionType* fn = callee.function;
            if (expr->data.call.argument_count != fn->parameter_count) {
                return pointer_error(analyzer, expr, "Function pointer signature argument count mismatch");
            }
            for (int i = 0; i < fn->parameter_count; i++) {
                ASTNode* arg = expr->data.call.arguments[i];
                if (!semantic_analyzer_analyze_expression(analyzer, arg) ||
                    !call_argument_assignable(analyzer, arg, fn->parameters[i])) {
                    return false;
                }
            }
            expr->function_type = fn;
            expr->resolved_type = fn->result;
            return true;
        }
        if (callee.pointer_depth) {
            return pointer_error(analyzer, expr, "A data pointer is not a callable fn value");
        }
        break;
    }
    default:
        break;
    }
    if (!semantic_expression_impl(analyzer, expr)) {
        return false;
    }
    expr->resolved_type = (KrtSourceType){.token = semantic_analyzer_infer_expression_type(analyzer, expr)};
    switch (expr->type) {
    case AST_NEW_EXPRESSION:
        expr->resolved_type = (KrtSourceType){.token = TOKEN_IDENTIFIER, .type_name = expr->data.new_expr.class_name};
        break;
    case AST_NUMBER:
        if (expr->declared_type.token) {
            expr->resolved_type = expr->declared_type;
        }
        break;
    case AST_NULL:
        expr->resolved_type.token = TOKEN_NULL;
        break;
    case AST_ARRAY_LITERAL: {
        KrtSourceType element = {0};
        bool nullable = false;
        for (int i = 0; i < expr->data.array_literal.element_count; i++) {
            KrtSourceType type = expr->data.array_literal.elements[i]->resolved_type;
            nullable |= type.nullable || type.token == TOKEN_NULL;
            if (KrtSourceIsPointer(type) && !element.token) {
                element = type;
            }
        }
        if (element.token) {
            element.nullable |= nullable;
            for (int i = 0; i < expr->data.array_literal.element_count; i++) {
                if (!pointer_assignable(analyzer, expr, element, expr->data.array_literal.elements[i]->resolved_type)) {
                    return false;
                }
            }
            if (element.pointer_depth >= 64) {
                return pointer_error(analyzer, expr, "Pointer nesting exceeds 64 levels");
            }
            expr->resolved_type = element;
            expr->resolved_type.pointer_depth++;
            expr->resolved_type.pointee_nullable = (element.pointee_nullable << 1) | element.nullable;
            expr->resolved_type.nullable = false;
        }
        break;
    }
    case AST_CALL: {
        const char* owner = expr->data.call.resolved_class_name;
        const char* key = expr->data.call.resolved_mangled_name;
        SymbolEntry* cls = owner ? semantic_analyzer_lookup_class(analyzer, owner) : NULL;
        SymbolEntry* method =
            cls && cls->nested_table && key ? symbol_table_lookup_scope_chain(cls->nested_table, key) : NULL;
        if (!method && key) {
            method = symbol_table_lookup_scope_chain(analyzer->symbol_table, key);
        }
        if (method && method->source_type.function) {
            KrtFunctionType* fn = method->source_type.function;
            if (fn->parameter_count == expr->data.call.argument_count) {
                for (int i = 0; i < fn->parameter_count; i++) {
                    if (!call_argument_assignable(analyzer, expr->data.call.arguments[i], fn->parameters[i])) {
                        return false;
                    }
                }
                expr->function_type = fn;
                expr->resolved_type = fn->result;
            }
        }
        break;
    }
    case AST_IDENTIFIER: {
        SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.identifier_name);
        if (symbol && symbol->source_type.token) {
            expr->resolved_type = symbol->source_type;
        }
        expr->is_ref_binding = expr->resolved_type.is_ref;
        expr->resolved_type.is_ref = false;
        expr->is_function_reference = symbol && symbol->type == SYMBOL_FUNCTION;
        break;
    }
    case AST_CAST_EXPRESSION: {
        KrtSourceType value = expr->data.cast_expr.expression->resolved_type;
        if (expr->declared_type.token) {
            expr->resolved_type = expr->declared_type;
        }
        if (KrtSourceIsPointer(expr->resolved_type) || KrtSourceIsPointer(value)) {
            if (!require_memory_permission(analyzer, expr)) {
                return false;
            }
            KrtSourceType scalar = KrtSourceIsPointer(value) ? expr->resolved_type : value;
            if (!KrtSourceIsPointer(scalar) && scalar.token != TOKEN_NULL && !KrtTokenIntegerBits(scalar.token)) {
                return pointer_error(analyzer, expr, "Pointer casts require pointers or integers");
            }
            if ((value.nullable || value.token == TOKEN_NULL) && !expr->resolved_type.nullable) {
                return pointer_error(analyzer, expr, "A cast cannot remove pointer nullability; use a pattern");
            }
            if (value.pointee_nullable & ~expr->resolved_type.pointee_nullable) {
                return pointer_error(analyzer, expr, "A cast cannot remove pointee nullability");
            }
        }
        break;
    }
    case AST_ARRAY_ACCESS:
        if (expr->data.array_access.array->type == AST_IDENTIFIER) {
            SymbolEntry* array = symbol_table_lookup_scope_chain(analyzer->symbol_table,
                                                                 expr->data.array_access.array->data.identifier_name);
            if (array && array->is_array) {
                expr->resolved_type = (KrtSourceType){.token = array->value_type};
            }
        }
        if (KrtSourceIsPointer(expr->data.array_access.array->resolved_type)) {
            return analyze_pointer_index(analyzer, expr, expr->data.array_access.array, expr->data.array_access.index,
                                         NULL);
        }
        break;
    case AST_UNARY_OPERATION:
        if (expr->data.unary_op.operator== TOKEN_INCREMENT || expr->data.unary_op.operator== TOKEN_DECREMENT) {
            ASTNode* target = expr->data.unary_op.operand;
            if (target->type != AST_IDENTIFIER && target->type != AST_ARRAY_ACCESS &&
                target->type != AST_POINTER_DEREFERENCE) {
                return pointer_error(analyzer, expr, "Increment/decrement requires a variable or memory lvalue");
            }
            if (target->is_function_reference) {
                return pointer_error(analyzer, expr, "A function symbol is not a writable lvalue");
            }
        }
        if (KrtSourceIsPointer(expr->data.unary_op.operand->resolved_type)) {
            expr->resolved_type = expr->data.unary_op.operand->resolved_type;
            KrtSourceType element;
            KrtTokenType op = expr->data.unary_op.operator;
            if (op != TOKEN_INCREMENT && op != TOKEN_DECREMENT) {
                return pointer_error(analyzer, expr, "Invalid unary pointer operator");
            }
            return pointer_pointee(analyzer, expr, expr->resolved_type, &element);
        }
        break;
    case AST_BINARY_OPERATION: {
        KrtSourceType a = expr->data.binary_op.left->resolved_type, b = expr->data.binary_op.right->resolved_type;
        bool ap = KrtSourceIsPointer(a), bp = KrtSourceIsPointer(b);
        if (!ap && !bp) {
            break;
        }
        if (!require_memory_permission(analyzer, expr)) {
            return false;
        }
        KrtTokenType op = expr->data.binary_op.operator;
        bool equality = op == TOKEN_EQUAL || op == TOKEN_NOT_EQUAL;
        if (equality && (a.token == TOKEN_NULL || b.token == TOKEN_NULL)) {
            expr->resolved_type.token = TOKEN_BOOL;
            break;
        }
        if (equality && ap && bp) {
            a.nullable = b.nullable = false;
            if (!source_type_equal(a, b)) {
                return pointer_error(analyzer, expr, "Pointer operands must have matching types");
            }
            expr->resolved_type.token = TOKEN_BOOL;
            break;
        }
        if (a.nullable || b.nullable) {
            return pointer_error(analyzer, expr, "Nullable pointer must be narrowed before arithmetic or comparison");
        }
        if (ap && bp) {
            if (!source_type_equal(a, b)) {
                return pointer_error(analyzer, expr, "Pointer operands must have matching types");
            }
            if (op == TOKEN_MINUS) {
                KrtSourceType element;
                if (!pointer_pointee(analyzer, expr, a, &element)) {
                    return false;
                }
                expr->resolved_type.token = TOKEN_UINT64;
            } else if (equality || op == TOKEN_LESS || op == TOKEN_GREATER || op == TOKEN_LESS_EQUAL ||
                       op == TOKEN_GREATER_EQUAL) {
                if (a.function && !a.pointer_depth && !equality) {
                    return pointer_error(analyzer, expr, "Function pointers only support equality comparisons");
                }
                expr->resolved_type.token = TOKEN_BOOL;
            } else {
                return pointer_error(analyzer, expr, "Invalid pointer operator");
            }
        } else {
            KrtSourceType element, pointer = ap ? a : b, integer = ap ? b : a;
            if ((op != TOKEN_PLUS && !(op == TOKEN_MINUS && ap)) || !KrtTokenIntegerBits(integer.token)) {
                return pointer_error(analyzer, expr, "Pointer arithmetic requires pointer +/- integer");
            }
            if (!pointer_pointee(analyzer, expr, pointer, &element)) {
                return false;
            }
            expr->resolved_type = pointer;
        }
        break;
    }
    case AST_TERNARY_OPERATION: {
        KrtSourceType a = expr->data.ternary_op.true_value->resolved_type;
        KrtSourceType b = expr->data.ternary_op.false_value->resolved_type;
        if (KrtSourceIsPointer(a) || KrtSourceIsPointer(b)) {
            if (a.token == TOKEN_NULL) {
                a = b;
                a.nullable = true;
            }
            if (b.token == TOKEN_NULL) {
                b = a;
                b.nullable = true;
            }
            a.nullable |= b.nullable;
            if (!pointer_assignable(analyzer, expr, a, b)) {
                return false;
            }
            expr->resolved_type = a;
        }
        break;
    }
    default:
        break;
    }
    return true;
}
