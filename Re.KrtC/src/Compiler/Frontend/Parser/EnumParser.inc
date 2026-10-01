/* Enum initializers are checked integer constant expressions, never runtime code. */
static bool enum_constant(ASTNode* expr, ASTNode* body, int64_t* result) {
    if (!expr) {
        return false;
    }
    int64_t a, b;
    if (expr->type == AST_NUMBER && expr->is_integer_literal && expr->integer_value <= INT64_MAX) {
        *result = (int64_t)expr->integer_value;
        return true;
    }
    if (expr->type == AST_IDENTIFIER) {
        for (int i = 0; i < body->data.block.statement_count; i++) {
            ASTNode* item = body->data.block.statements[i];
            if (!strcmp(item->data.static_variable_decl.name, expr->data.identifier_name)) {
                *result = (int32_t)item->data.static_variable_decl.value->integer_value;
                return true;
            }
        }
    }
    if (expr->type == AST_UNARY_OPERATION && enum_constant(expr->data.unary_op.operand, body, &a)) {
        if (expr->data.unary_op.operator== TOKEN_MINUS) {
            *result = -a;
        } else if (expr->data.unary_op.operator== TOKEN_TILDE) {
            *result = ~a;
        } else if (expr->data.unary_op.operator== TOKEN_PLUS) {
            *result = a;
        } else {
            return false;
        }
        return *result >= INT32_MIN && *result <= INT32_MAX;
    }
    if (expr->type != AST_BINARY_OPERATION || !enum_constant(expr->data.binary_op.left, body, &a) ||
        !enum_constant(expr->data.binary_op.right, body, &b) || a < INT32_MIN || a > INT32_MAX || b < INT32_MIN ||
        b > INT32_MAX) {
        return false;
    }
    switch (expr->data.binary_op.operator) {
    case TOKEN_PLUS:
        *result = a + b;
        break;
    case TOKEN_MINUS:
        *result = a - b;
        break;
    case TOKEN_MULTIPLY:
        *result = a * b;
        break;
    case TOKEN_DIVIDE:
        if (!b) {
            return false;
        }
        *result = a / b;
        break;
    case TOKEN_MODULO:
        if (!b) {
            return false;
        }
        *result = a % b;
        break;
    case TOKEN_PIPE:
    case TOKEN_BITWISE_OR:
        *result = a | b;
        break;
    case TOKEN_BITWISE_AND:
        *result = a & b;
        break;
    case TOKEN_BITWISE_XOR:
        *result = a ^ b;
        break;
    case TOKEN_LSHIFT:
        if (b < 0 || b > 31) {
            return false;
        }
        *result = a * (INT64_C(1) << b);
        break;
    case TOKEN_RSHIFT:
        if (b < 0 || b > 31) {
            return false;
        }
        *result = a >> b;
        break;
    default:
        return false;
    }
    return *result >= INT32_MIN && *result <= INT32_MAX;
}

static ASTNode* parser_parse_enum(Parser* parser) {
    int line = parser->current_token.line, col = parser->current_token.column;
    ASTNode* node = KRT_PARSER_CREATE_NODE(AST_CLASS_DECLARATION, line, col);
    node->is_enum = true;
    parser_advance(parser);
    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        goto invalid;
    }
    node->data.class_decl.name = KRT_PARSER_STRDUP(parser->current_token.value);
    parser_advance(parser);
    if (parser->current_token.type != TOKEN_LEFT_BRACE) {
        goto invalid;
    }
    parser_advance(parser);
    ASTNode* body = KRT_PARSER_CREATE_NODE(AST_BLOCK, line, col);
    node->data.class_decl.body = body;
    int capacity = 8;
    body->data.block.statements = KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
    int64_t next = 0;
    while (parser->current_token.type != TOKEN_RIGHT_BRACE) {
        if (parser->current_token.type != TOKEN_IDENTIFIER) {
            goto invalid;
        }
        ASTNode* item = KRT_PARSER_CREATE_NODE(AST_STATIC_VARIABLE_DECLARATION, parser->current_token.line,
                                               parser->current_token.column);
        item->data.static_variable_decl.name = KRT_PARSER_STRDUP(parser->current_token.value);
        item->data.static_variable_decl.type = TOKEN_INT32;
        for (int i = 0; i < body->data.block.statement_count; i++) {
            if (!strcmp(body->data.block.statements[i]->data.static_variable_decl.name,
                        item->data.static_variable_decl.name)) {
                goto invalid;
            }
        }
        parser_advance(parser);
        if (parser->current_token.type == TOKEN_ASSIGN) {
            parser_advance(parser);
            if (!enum_constant(parser_parse_expression(parser), body, &next)) {
                goto invalid;
            }
        }
        if (next < INT32_MIN || next > INT32_MAX) {
            goto invalid;
        }
        ASTNode* value = KRT_PARSER_CREATE_NODE(AST_NUMBER, item->line, item->col);
        value->is_integer_literal = true;
        value->integer_value = (KrtUInt128)next;
        value->data.number_value = (double)next++;
        value->declared_type.token = TOKEN_INT32;
        item->data.static_variable_decl.value = value;
        if (body->data.block.statement_count == capacity) {
            if (capacity > INT_MAX / 2) {
                KRT_COMPILE_ERROR("Parser collection exceeds supported size");
            }
            capacity *= 2;
            ASTNode** items = KRT_PARSER_ALLOC_FROM_ARENA(parser, capacity * sizeof(ASTNode*));
            memcpy(items, body->data.block.statements, body->data.block.statement_count * sizeof(ASTNode*));
            body->data.block.statements = items;
        }
        body->data.block.statements[body->data.block.statement_count++] = item;
        if (parser->current_token.type != TOKEN_COMMA) {
            break;
        }
        parser_advance(parser);
    }
    if (parser->current_token.type != TOKEN_RIGHT_BRACE) {
        goto invalid;
    }
    parser_advance(parser);
    if (parser->current_token.type == TOKEN_SEMICOLON) {
        parser_advance(parser);
    }
    return node;
invalid:
    parser_report_error(parser, line, col,
                        "Invalid enum: require unique members and int32 constant values without overflow");
    return NULL;
}
