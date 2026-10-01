/* Read a qualified name and retain its complete generic argument spelling. */
char* KrtParseNamedType(Parser* parser) {
    char text[4096];
    size_t length = 0;
    int depth = 0;
    for (;;) {
        Token* token = &parser->current_token;
        const char* part = token->value;
        if (!part || token->type == TOKEN_EOF) {
            goto invalid;
        }
        size_t size = strlen(part);
        if (length + size >= sizeof(text)) {
            goto invalid;
        }
        memcpy(text + length, part, size);
        length += size;
        if (token->type == TOKEN_LESS) {
            depth++;
        }
        if (token->type == TOKEN_GREATER) {
            depth--;
        }
        if (token->type == TOKEN_RSHIFT) {
            depth -= 2;
        }
        if (depth < 0 || depth > 32) {
            goto invalid;
        }
        parser_advance(parser);
        if (depth == 0 && parser->current_token.type != TOKEN_DOT && parser->current_token.type != TOKEN_LESS) {
            break;
        }
        if (depth == 0 && parser->current_token.type == TOKEN_DOT) {
            if (length + 1 >= sizeof(text)) {
                goto invalid;
            }
            text[length++] = '.';
            parser_advance(parser);
            if (parser->current_token.type != TOKEN_IDENTIFIER) {
                goto invalid;
            }
        }
        if (depth &&
            (parser->current_token.type == TOKEN_SEMICOLON || parser->current_token.type == TOKEN_LEFT_BRACE)) {
            goto invalid;
        }
    }
    text[length] = 0;
    char* result = KrtParserAlloc(parser, length + 1);
    memcpy(result, text, length + 1);
    return result;
invalid:
    parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                        "Invalid or excessively nested generic type name");
    return NULL;
}

/* Source types retain pointee and callable information until IR lowering. */
bool KrtParseSourceType(Parser* parser, KrtSourceType* type) {
    memset(type, 0, sizeof(*type));
    if (parser->current_token.type == TOKEN_IDENTIFIER && !strcmp(parser->current_token.value, "ref")) {
        type->is_ref = true;
        parser_advance(parser);
    }
    type->token = parser->current_token.type;
    if (type->token == TOKEN_FN) {
        KrtFunctionType* fn = KrtParserAlloc(parser, sizeof(*fn));
        if (!fn) {
            return false;
        }
        memset(fn, 0, sizeof(*fn));
        type->function = fn;
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_LEFT_PAREN) {
            goto invalid;
        }
        parser_advance(parser);
        int capacity = 8;
        fn->parameters = KrtParserAlloc(parser, capacity * sizeof(KrtSourceType));
        while (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            if (fn->parameter_count == capacity) {
                if (capacity > INT_MAX / 2) {
                    KRT_COMPILE_ERROR("Parser collection exceeds supported size");
                }
                capacity *= 2;
                KrtSourceType* params = KrtParserAlloc(parser, capacity * sizeof(*params));
                memcpy(params, fn->parameters, fn->parameter_count * sizeof(*params));
                fn->parameters = params;
            }
            if (!KrtParseSourceType(parser, &fn->parameters[fn->parameter_count++])) {
                return false;
            }
            if (parser->current_token.type != TOKEN_COMMA) {
                break;
            }
            parser_advance(parser);
        }
        if (parser->current_token.type != TOKEN_RIGHT_PAREN) {
            goto invalid;
        }
        parser_advance(parser);
        if (parser->current_token.type != TOKEN_ARROW) {
            goto invalid;
        }
        parser_advance(parser);
        if (!KrtParseSourceType(parser, &fn->result)) {
            return false;
        }
    } else {
        if (!parser_is_type_keyword(type->token) && type->token != TOKEN_IDENTIFIER) {
            goto invalid;
        }
        if (type->token == TOKEN_IDENTIFIER) {
            type->type_name = KrtParseNamedType(parser);
            if (!type->type_name) {
                return false;
            }
        } else {
            parser_advance(parser);
        }
    }
    for (;;) {
        if (parser->current_token.type == TOKEN_MULTIPLY || parser->current_token.type == TOKEN_POWER ||
            parser->current_token.type == TOKEN_MUL_ASSIGN) {
            unsigned levels = parser->current_token.type == TOKEN_POWER ? 2 : 1;
            if (type->pointer_depth + levels > 64) {
                parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                                    "Pointer nesting exceeds 64 levels");
                return false;
            }
            type->pointee_nullable = ((type->pointee_nullable << 1) | type->nullable) << (levels - 1);
            type->nullable = false;
            type->pointer_depth += levels;
            if (parser->current_token.type == TOKEN_MUL_ASSIGN) {
                parser->current_token.type = TOKEN_ASSIGN;
                break;
            }
            parser_advance(parser);
        } else if (parser->current_token.type == TOKEN_QUESTION && !type->nullable) {
            type->nullable = true;
            parser_advance(parser);
        } else {
            break;
        }
    }
    return true;
invalid:
    parser_report_error(parser, parser->current_token.line, parser->current_token.column,
                        "expected a type, pointer type, or fn(T, ...) -> T");
    return false;
}
