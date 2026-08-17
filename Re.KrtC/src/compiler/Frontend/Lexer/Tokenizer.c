#include "Core/Utils/KrtCommon.h"
#include "Core/Memory/Arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include "Tokenizer.h"
#include "Accelerator.h"

typedef struct {
    const char* word;
    KrtTokenType token;
} Keyword;

#define KEYWORD_TABLE_SIZE 256

static Keyword keywords[] = {
    {"function", TOKEN_FUNCTION},
    {"func", TOKEN_FUNCTION},
    {"var", TOKEN_VAR},
    {"let", TOKEN_LET},
    {"if", TOKEN_IF},
    {"else", TOKEN_ELSE},
    {"while", TOKEN_WHILE},
    {"for", TOKEN_FOR},
    {"foreach", TOKEN_FOREACH},
    {"in", TOKEN_IN},
    {"return", TOKEN_RETURN},
    
    {"true", TOKEN_TRUE},
    {"false", TOKEN_FALSE},
    {"null", TOKEN_NULL},
    {"_", TOKEN_UNDERSCORE},
    {"fixed", TOKEN_FIXED},
    {"async", TOKEN_ASYNC},
    {"await", TOKEN_AWAIT},
    {"unsafe", TOKEN_UNSAFE},
    {"Point", TOKEN_POINT},
    {"point", TOKEN_POINT},
    {"class", TOKEN_CLASS},
    {"struct", TOKEN_STRUCT},
    {"interface", TOKEN_INTERFACE},
    {"enum", TOKEN_ENUM},
    {"new", TOKEN_NEW},
    {"delete", TOKEN_DELETE},
    {"this", TOKEN_THIS},
    {"public", TOKEN_PUBLIC},
    {"private", TOKEN_PRIVATE},
    {"protected", TOKEN_PROTECTED},
    {"static", TOKEN_STATIC},
    {"virtual", TOKEN_VIRTUAL},
    {"abstract", TOKEN_ABSTRACT},
    {"override", TOKEN_OVERRIDE},
    {"base", TOKEN_BASE},
    {"using", TOKEN_USING},
    {"namespace", TOKEN_NAMESPACE},
    {"try", TOKEN_TRY},
    {"catch", TOKEN_CATCH},
    {"finally", TOKEN_FINALLY},
    {"throw", TOKEN_THROW},
    {"exception", TOKEN_EXCEPTION},
    {"template", TOKEN_TEMPLATE},
    {"typename", TOKEN_TYPENAME},
    {"where", TOKEN_WHERE},
    {"switch", TOKEN_SWITCH},
    {"case", TOKEN_CASE},
    {"break", TOKEN_BREAK},
    {"continue", TOKEN_CONTINUE},
    {"default", TOKEN_DEFAULT},
    {"int", TOKEN_INT32},
    {"long", TOKEN_INT64},
    {"float", TOKEN_FLOAT32},
    {"double", TOKEN_FLOAT64},
    {"int8", TOKEN_INT8},
    {"int16", TOKEN_INT16},
    {"int32", TOKEN_INT32},
    {"int64", TOKEN_INT64},
    {"uint8", TOKEN_UINT8},
    {"uint16", TOKEN_UINT16},
    {"uint32", TOKEN_UINT32},
    {"uint64", TOKEN_UINT64},
    {"float32", TOKEN_FLOAT32},
    {"float64", TOKEN_FLOAT64},
    {"bool", TOKEN_BOOL},
    {"char", TOKEN_CHAR},
    {"void", TOKEN_VOID},
    {"string", TOKEN_TYPE_STRING},
    {"console", TOKEN_CONSOLE},
    {"get", TOKEN_GET},
    {"set", TOKEN_SET},
    {"from", TOKEN_FROM},
    {"select", TOKEN_SELECT},
    {"orderby", TOKEN_ORDERBY},
    {"group", TOKEN_GROUP},
    {"by", TOKEN_BY},
    {"join", TOKEN_JOIN},
    {"on", TOKEN_ON},
    {"equals", TOKEN_EQUALS},
    {"into", TOKEN_INTO},
    {"is", TOKEN_IS},
    {"as", TOKEN_AS},
    {"yield", TOKEN_YIELD},
    {"lock", TOKEN_LOCK},
    {"match", TOKEN_MATCH},
    {"with", TOKEN_WITH},
    {"when", TOKEN_WHEN},
    {"params", TOKEN_PARAMS},
    {"operator", TOKEN_OPERATOR},
    {"delegate", TOKEN_DELEGATE},
    {"sizeof", TOKEN_SIZEOF},
    {"stackalloc", TOKEN_STACKALLOC},
    {"op_Addition", TOKEN_OP_ADDITION},
    {"op_Subtraction", TOKEN_OP_SUBTRACTION},
    {"op_Multiply", TOKEN_OP_MULTIPLY},
    {"op_Division", TOKEN_OP_DIVISION},
    {"op_Modulo", TOKEN_OP_MODULO},
    {"op_Equality", TOKEN_OP_EQUALITY},
    {"op_Inequality", TOKEN_OP_INEQUALITY},
    {"op_LessThan", TOKEN_OP_LESS},
    {"op_GreaterThan", TOKEN_OP_GREATER},
    {"op_LessThanOrEqual", TOKEN_OP_LESS_EQUAL},
    {"op_GreaterThanOrEqual", TOKEN_OP_GREATER_EQUAL},
    {"op_BitwiseAnd", TOKEN_OP_BITWISE_AND},
    {"op_BitwiseOr", TOKEN_OP_BITWISE_OR},
    {"op_BitwiseXor", TOKEN_OP_BITWISE_XOR},
    {"op_LeftShift", TOKEN_OP_LEFT_SHIFT},
    {"op_RightShift", TOKEN_OP_RIGHT_SHIFT},
    {"op_UnaryPlus", TOKEN_OP_UNARY_PLUS},
    {"op_UnaryMinus", TOKEN_OP_UNARY_MINUS},
    {"op_LogicalNot", TOKEN_OP_LOGICAL_NOT},
    {"op_BitwiseNot", TOKEN_OP_BITWISE_NOT},
    {"op_Increment", TOKEN_OP_INCREMENT},
    {"op_Decrement", TOKEN_OP_DECREMENT},
    {"op_True", TOKEN_OP_TRUE},
    {"op_False", TOKEN_OP_FALSE},
    {"op_Implicit", TOKEN_OP_IMPLICIT},
    {"op_Explicit", TOKEN_OP_EXPLICIT},
    {"const", TOKEN_CONST},
    {"extern", TOKEN_EXTERN},
    {NULL, 0}
};

static Keyword* keyword_table[KEYWORD_TABLE_SIZE] = {NULL};
static int keyword_table_initialized = 0;

static void init_keyword_table(void) {
    if (keyword_table_initialized) return;
    
    for (int i = 0; keywords[i].word != NULL; i++) {
        uint32_t hash = accelerator_hash_string(keywords[i].word);
        int index = hash % KEYWORD_TABLE_SIZE;
        
        while (keyword_table[index] != NULL) {
            index = (index + 1) % KEYWORD_TABLE_SIZE;
        }
        
        keyword_table[index] = &keywords[i];
    }
    
    keyword_table_initialized = 1;
}

static KrtTokenType lookup_keyword(const char* word) {
    if (!word || !keyword_table_initialized) return TOKEN_IDENTIFIER;
    
    uint32_t hash = accelerator_hash_string(word);
    int index = hash % KEYWORD_TABLE_SIZE;
    
    for (int i = 0; i < KEYWORD_TABLE_SIZE; i++) {
        Keyword* entry = keyword_table[index];
        if (!entry) break;
        
        if (strcmp(entry->word, word) == 0) {
            return entry->token;
        }
        
        index = (index + 1) % KEYWORD_TABLE_SIZE;
    }
    
    return TOKEN_IDENTIFIER;
}

static void lexer_advance(Lexer* lexer) {
    if (lexer->source[lexer->position] == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    lexer->position++;
}

static char lexer_current_char(Lexer* lexer) {
    return lexer->source[lexer->position];
}

static void lexer_skip_whitespace(Lexer* lexer) {
    while (lexer->source[lexer->position] != '\0') {
        if (lexer->source[lexer->position] == ' ' ||
            lexer->source[lexer->position] == '\t' ||
            lexer->source[lexer->position] == '\n' ||
            lexer->source[lexer->position] == '\r') {
            lexer_advance(lexer);
        } else if (lexer->source[lexer->position] == '/' &&
                 lexer->source[lexer->position + 1] == '/') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            while (lexer->source[lexer->position] != '\0' &&
                   lexer->source[lexer->position] != '\n') {
                lexer_advance(lexer);
            }
        } else if (lexer->source[lexer->position] == '/' &&
                 lexer->source[lexer->position + 1] == '*') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            while (lexer->source[lexer->position] != '\0') {
                if (lexer->source[lexer->position] == '*' &&
                    lexer->source[lexer->position + 1] == '/') {
                    lexer_advance(lexer);
                    lexer_advance(lexer);
                    break;
                }
                lexer_advance(lexer);
            }
        } else {
            break;
        }
    }
}

static Token lexer_create_token(KrtTokenType type, char* value, int line, int column) {
    Token token;
    token.type = type;
    token.value = value;
    token.line = line;
    token.column = column;
    return token;
}

static Token lexer_read_number(Lexer* lexer) {
    int line = lexer->line;
    int column = lexer->column;

    int start = lexer->position;

    if (lexer->source[lexer->position] == '0' && 
        (lexer->source[lexer->position + 1] == 'x' || lexer->source[lexer->position + 1] == 'X')) {
        lexer_advance(lexer);
        lexer_advance(lexer);
        while ((accelerator_is_digit(lexer->source[lexer->position]) ||
                (lexer->source[lexer->position] >= 'a' && lexer->source[lexer->position] <= 'f') ||
                (lexer->source[lexer->position] >= 'A' && lexer->source[lexer->position] <= 'F'))) {
            lexer_advance(lexer);
        }
    } else if (lexer->source[lexer->position] == '0' &&
               (lexer->source[lexer->position + 1] == 'b' || lexer->source[lexer->position + 1] == 'B')) {
        lexer_advance(lexer);
        lexer_advance(lexer);
        while (lexer->source[lexer->position] == '0' || lexer->source[lexer->position] == '1') {
            lexer_advance(lexer);
        }
    } else if (lexer->source[lexer->position] == '0' &&
               accelerator_is_digit(lexer->source[lexer->position + 1]) &&
               lexer->source[lexer->position + 1] != '.' &&
               lexer->source[lexer->position + 1] != 'e' &&
               lexer->source[lexer->position + 1] != 'E') {
        while (accelerator_is_octal_digit(lexer->source[lexer->position])) {
            lexer_advance(lexer);
        }
    } else {
        while (accelerator_is_digit(lexer->source[lexer->position])) {
            lexer_advance(lexer);
        }
    }

    if (lexer->source[lexer->position] == '.') {
        bool is_float_already = false;
        for (int i = start; i < lexer->position; i++) {
            if (lexer->source[i] == '.') { is_float_already = true; break; }
        }
        if (!is_float_already) {
            lexer_advance(lexer);
            while (accelerator_is_digit(lexer->source[lexer->position])) {
                lexer_advance(lexer);
            }
        }
    }

    if (lexer->source[lexer->position] == 'e' || lexer->source[lexer->position] == 'E') {
        lexer_advance(lexer);
        if (lexer->source[lexer->position] == '+' || lexer->source[lexer->position] == '-') {
            lexer_advance(lexer);
        }
        while (accelerator_is_digit(lexer->source[lexer->position])) {
            lexer_advance(lexer);
        }
    }

    int length = lexer->position - start;
    char* value = (char*)KrtArenaAlloc(lexer->arena, length + 1);
    if (!value) {
        return lexer_create_token(TOKEN_EOF, "", line, column);
    }
    strncpy(value, lexer->source + start, length);
    value[length] = '\0';

    return lexer_create_token(TOKEN_NUMBER, value, line, column);
}

static Token lexer_read_identifier(Lexer* lexer) {
    int line = lexer->line;
    int column = lexer->column;

    int start = lexer->position;
    while (accelerator_is_alpha(lexer->source[lexer->position]) ||
           accelerator_is_digit(lexer->source[lexer->position]) ||
           lexer->source[lexer->position] == '_') {
        lexer_advance(lexer);
    }

    int length = lexer->position - start;
    char* value = (char*)KrtArenaAlloc(lexer->arena, length + 1);
    if (!value) {
        return lexer_create_token(TOKEN_EOF, "", line, column);
    }
    strncpy(value, lexer->source + start, length);
    value[length] = '\0';

    init_keyword_table();
    
    KrtTokenType type = lookup_keyword(value);
    if (type != TOKEN_IDENTIFIER) {
        return lexer_create_token(type, value, line, column);
    }

    return lexer_create_token(TOKEN_IDENTIFIER, value, line, column);
}

static Token lexer_read_char(Lexer* lexer) {
    int line = lexer->line;
    int column = lexer->column;

    lexer_advance(lexer);

    char value[2] = {0, 0};
    
    if (lexer->source[lexer->position] == '\\') {
        lexer_advance(lexer);
        char escape = lexer->source[lexer->position];
        switch (escape) {
            case 'n': value[0] = '\n'; break;
            case 't': value[0] = '\t'; break;
            case 'r': value[0] = '\r'; break;
            case '\\': value[0] = '\\'; break;
            case '\'': value[0] = '\''; break;
            case '0': value[0] = '\0'; break;
            case 'b': value[0] = '\b'; break;
            case 'f': value[0] = '\f'; break;
            case 'a': value[0] = '\a'; break;
            default:
                if (escape == 'x' || escape == 'X') {
                    lexer_advance(lexer);
                    int hex_val = 0;
                    for (int i = 0; i < 2 && accelerator_is_hex_digit(lexer->source[lexer->position]); i++) {
                        hex_val = hex_val * 16 + accelerator_hex_to_int(lexer->source[lexer->position]);
                        lexer_advance(lexer);
                    }
                    value[0] = (char)hex_val;
                } else {
                    value[0] = escape;
                }
                break;
        }
        lexer_advance(lexer);
    } else if (lexer->source[lexer->position] != '\'' && lexer->source[lexer->position] != '\0') {
        value[0] = lexer->source[lexer->position];
        lexer_advance(lexer);
    }

    if (lexer->source[lexer->position] != '\'') {
        return lexer_create_token(TOKEN_EOF, "", line, column);
    }
    lexer_advance(lexer);

    char* char_str = (char*)KrtArenaAlloc(lexer->arena, 2);
    if (char_str) {
        char_str[0] = value[0];
        char_str[1] = '\0';
    }
    
    return lexer_create_token(TOKEN_CHAR_LITERAL, char_str ? char_str : value, line, column);
}

static Token lexer_read_string(Lexer* lexer) {
    int line = lexer->line;
    int column = lexer->column;

    lexer_advance(lexer);

    int capacity = 32;
    int length = 0;
    char* value = (char*)KrtArenaAlloc(lexer->arena, capacity);
    if (!value) return lexer_create_token(TOKEN_EOF, "", line, column);

    while (lexer->source[lexer->position] != '"' && lexer->source[lexer->position] != '\0') {
        if (length + 2 >= capacity) {
            capacity *= 2;
            char* new_value = (char*)KrtArenaRealloc(lexer->arena, value, capacity / 2, capacity);
            if (!new_value) {
                return lexer_create_token(TOKEN_EOF, "", line, column);
            }
            value = new_value;
        }

        if (lexer->source[lexer->position] == '\\') {
            lexer_advance(lexer);
            char escape = lexer->source[lexer->position];
            switch (escape) {
                case 'n': value[length++] = '\n'; break;
                case 't': value[length++] = '\t'; break;
                case 'r': value[length++] = '\r'; break;
                case '\\': value[length++] = '\\'; break;
                case '"': value[length++] = '"'; break;
                case '\'': value[length++] = '\''; break;
                case '0': value[length++] = '\0'; break;
                case 'b': value[length++] = '\b'; break;
                case 'f': value[length++] = '\f'; break;
                case 'a': value[length++] = '\a'; break;
                default:
                    if (escape == 'x' || escape == 'X') {
                        lexer_advance(lexer);
                        int hex_val = 0;
                        for (int i = 0; i < 2 && accelerator_is_hex_digit(lexer->source[lexer->position]); i++) {
                            hex_val = hex_val * 16 + accelerator_hex_to_int(lexer->source[lexer->position]);
                            lexer_advance(lexer);
                        }
                        value[length++] = (char)hex_val;
                        continue;
                    } else if (escape == 'u') {
                        lexer_advance(lexer);
                        int unicode_val = 0;
                        for (int i = 0; i < 4 && accelerator_is_hex_digit(lexer->source[lexer->position]); i++) {
                            unicode_val = unicode_val * 16 + accelerator_hex_to_int(lexer->source[lexer->position]);
                            lexer_advance(lexer);
                        }
                        length += encode_utf8(value + length, unicode_val, capacity - length);
                        continue;
                    } else if (escape == 'U') {
                        lexer_advance(lexer);
                        int unicode_val = 0;
                        for (int i = 0; i < 8 && accelerator_is_hex_digit(lexer->source[lexer->position]); i++) {
                            unicode_val = unicode_val * 16 + accelerator_hex_to_int(lexer->source[lexer->position]);
                            lexer_advance(lexer);
                        }
                        length += encode_utf8(value + length, unicode_val, capacity - length);
                        continue;
                    } else {
                        value[length++] = '\\';
                        value[length++] = escape;
                    }
                    break;
            }
            lexer_advance(lexer);
        } else {
            value[length++] = lexer->source[lexer->position];
            lexer_advance(lexer);
        }
    }

    if (lexer->source[lexer->position] == '\0') {
        return lexer_create_token(TOKEN_EOF, "", line, column);
    }

    value[length] = '\0';
    lexer_advance(lexer);

    return lexer_create_token(TOKEN_STRING, value, line, column);
}

Lexer* lexer_create(const char* source) {
    Lexer* lexer = (Lexer*)KRT_MALLOC(sizeof(Lexer));
    if (!lexer) {
        return NULL;
    }

    lexer->source_owned = KRT_STRDUP(source);
    if (!lexer->source_owned) {
        KRT_FREE(lexer);
        return NULL;
    }

    lexer->source = lexer->source_owned;
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
    lexer->arena = KrtArenaCreate(64 * 1024);  
    
    if (!lexer->arena) {
        KRT_FREE(lexer->source_owned);
        KRT_FREE(lexer);
        return NULL;
    }

    return lexer;
}

void lexer_destroy(Lexer* lexer) {
    if (lexer) {
        if (lexer->arena) {
            KrtArenaDestroy(lexer->arena);
        }
        if (lexer->source_owned) {
            KRT_FREE(lexer->source_owned);
        }
        KRT_FREE(lexer);
    }
}

void lexer_reset(Lexer* lexer) {
    lexer->position = 0;
    lexer->line = 1;
    lexer->column = 1;
}

Token lexer_next_token(Lexer* lexer) {
    lexer_skip_whitespace(lexer);

    if (lexer->source[lexer->position] == '\0') {
        return lexer_create_token(TOKEN_EOF, "", lexer->line, lexer->column);
    }

    char c = lexer_current_char(lexer);
    int line = lexer->line;
    int column = lexer->column;

    if (accelerator_is_alpha(c) || c == '_') {
        return lexer_read_identifier(lexer);
    }

    if (accelerator_is_digit(c)) {
        return lexer_read_number(lexer);
    }

    if (c == '"') {
        return lexer_read_string(lexer);
    }

    if (c == '\'') {
        return lexer_read_char(lexer);
    }

    switch (c) {
        case '+': {
            lexer_advance(lexer);
            char next = lexer_current_char(lexer);
            if (next == '+') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_INCREMENT, "++", line, column);
            }
            if (next == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_PLUS_ASSIGN, "+=", line, column);
            }
            return lexer_create_token(TOKEN_PLUS, "+", line, column);
        }
        case '-': {
            lexer_advance(lexer);
            char next = lexer_current_char(lexer);
            if (next == '-') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_DECREMENT, "--", line, column);
            }
            if (next == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_MINUS_ASSIGN, "-=", line, column);
            }
            if (next == '>') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_ARROW, "->", line, column);
            }
            return lexer_create_token(TOKEN_MINUS, "-", line, column);
        }
        case '*': {
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_MUL_ASSIGN, "*=", line, column);
            }
            if (lexer_current_char(lexer) == '*') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_POWER, "**", line, column);
            }
            return lexer_create_token(TOKEN_MULTIPLY, "*", line, column);
        }
        case '/': {
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_DIV_ASSIGN, "/=", line, column);
            }
            return lexer_create_token(TOKEN_DIVIDE, "/", line, column);
        }
        case '%': {
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_MOD_ASSIGN, "%=", line, column);
            }
            return lexer_create_token(TOKEN_MODULO, "%", line, column);
        }
        case '=':
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_EQUAL, "==", line, column);
            }
            if (lexer_current_char(lexer) == '>') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_LAMBDA, "=>", line, column);
            }
            return lexer_create_token(TOKEN_ASSIGN, "=", line, column);
        case '!':
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_NOT_EQUAL, "!=", line, column);
            }
            return lexer_create_token(TOKEN_NOT, "!", line, column);
        case '<':
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_LESS_EQUAL, "<=", line, column);
            }
            if (lexer_current_char(lexer) == '<') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_LSHIFT, "<<", line, column);
            }
            return lexer_create_token(TOKEN_LESS, "<", line, column);
        case '>':
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '=') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_GREATER_EQUAL, ">=", line, column);
            }
            if (lexer_current_char(lexer) == '>') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_RSHIFT, ">>", line, column);
            }
            return lexer_create_token(TOKEN_GREATER, ">", line, column);
        case '&':
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '&') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_AND, "&&", line, column);
            }
            return lexer_create_token(TOKEN_BITWISE_AND, "&", line, column);
        case '|':
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == '|') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_OR, "||", line, column);
            }
            return lexer_create_token(TOKEN_PIPE, "|", line, column);
        case '^':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_BITWISE_XOR, "^", line, column);
        case '(':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_LEFT_PAREN, "(", line, column);
        case ')':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_RIGHT_PAREN, ")", line, column);
        case '$':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_DOLLAR, "$", line, column);
        case '{':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_LEFT_BRACE, "{", line, column);
        case '}':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_RIGHT_BRACE, "}", line, column);
        case '[':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_LEFT_BRACKET, "[", line, column);
        case ']':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_RIGHT_BRACKET, "]", line, column);
        case ',':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_COMMA, ",", line, column);
        case ';':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_SEMICOLON, ";", line, column);
        case ':': {
            lexer_advance(lexer);
            if (lexer_current_char(lexer) == ':') {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_DOUBLE_COLON, "::", line, column);
            }
            return lexer_create_token(TOKEN_COLON, ":", line, column);
        }
        case '.':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_DOT, ".", line, column);
        case '?': {
            int line = lexer->line;
            int column = lexer->column;
            if (lexer->source[lexer->position + 1] == ':') {
                lexer_advance(lexer);
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_QUESTION_COLON, "?:", line, column);
            } else if (lexer->source[lexer->position + 1] == '?') {
                lexer_advance(lexer);
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_NULL_COALESCING, "??", line, column);
            } else if (lexer->source[lexer->position + 1] == '.') {
                lexer_advance(lexer);
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_QUESTION_DOT, "?.", line, column);
            } else {
                lexer_advance(lexer);
                return lexer_create_token(TOKEN_QUESTION, "?", line, column);
            }
        }
        case '~':
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_TILDE, "~", line, column);
        default:
            if ((unsigned char)c >= 0xC0) {
                int bytes_remaining;
                if ((unsigned char)c <= 0xDF) {
                    bytes_remaining = 1;
                } else if ((unsigned char)c <= 0xEF) {
                    bytes_remaining = 2;
                } else if ((unsigned char)c <= 0xF7) {
                    bytes_remaining = 3;
                } else {
                    break;
                }

                while (bytes_remaining > 0 && lexer->source[lexer->position] != '\0') {
                    unsigned char next_byte = (unsigned char)lexer->source[lexer->position];
                    if (next_byte >= 0x80 && next_byte <= 0xBF) {
                        lexer_advance(lexer);
                        bytes_remaining--;
                    } else {
                        break;
                    }
                }
                return lexer_next_token(lexer);
            }
            lexer_advance(lexer);
            return lexer_create_token(TOKEN_UNKNOWN, "", line, column);
            break;
    }
    
    return lexer_create_token(TOKEN_EOF, "", lexer->line, lexer->column);
}

Token lexer_peek_token(Lexer* lexer) {
    int saved_position = lexer->position;
    int saved_line = lexer->line;
    int saved_column = lexer->column;

    Token token = lexer_next_token(lexer);

    lexer->position = saved_position;
    lexer->line = saved_line;
    lexer->column = saved_column;

    return token;
}

Token lexer_peek_nth_token(Lexer* lexer, int n) {
    int saved_position = lexer->position;
    int saved_line = lexer->line;
    int saved_column = lexer->column;

    Token token = {0};
    token.type = -1;  // 🔧 修复：初始化为 -1 而不是 0 (TOKEN_EOF)，确保循环能执行

    for (int i = 0; i < n && token.type != TOKEN_EOF; i++) {
        if (i > 0) token_free(&token);
        token = lexer_next_token(lexer);
    }

    lexer->position = saved_position;
    lexer->line = saved_line;
    lexer->column = saved_column;

    return token;
}

void token_free(Token* token) {
    if (!token) return;
    (void)token;
}

const char* token_type_to_string(KrtTokenType type) {
    switch (type) {
        case TOKEN_EOF:
            return "EOF";
        case TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TOKEN_NUMBER:
            return "NUMBER";
        case TOKEN_STRING:
            return "STRING_LITERAL";
        case TOKEN_CHAR_LITERAL:
            return "CHAR_LITERAL";
        case TOKEN_TYPE_STRING:
            return "STRING";
        case TOKEN_PLUS:
            return "PLUS";
        case TOKEN_MINUS:
            return "MINUS";
        case TOKEN_MULTIPLY:
            return "MULTIPLY";
        case TOKEN_DIVIDE:
            return "DIVIDE";
        case TOKEN_ASSIGN:
            return "ASSIGN";
        case TOKEN_EQUAL:
            return "EQUAL";
        case TOKEN_NOT_EQUAL:
            return "NOT_EQUAL";
        case TOKEN_LESS:
            return "LESS";
        case TOKEN_GREATER:
            return "GREATER";
        case TOKEN_LESS_EQUAL:
            return "LESS_EQUAL";
        case TOKEN_GREATER_EQUAL:
            return "GREATER_EQUAL";
        case TOKEN_LEFT_PAREN:
            return "LEFT_PAREN";
        case TOKEN_RIGHT_PAREN:
            return "RIGHT_PAREN";
        case TOKEN_LEFT_BRACE:
            return "LEFT_BRACE";
        case TOKEN_RIGHT_BRACE:
            return "RIGHT_BRACE";
        case TOKEN_COMMA:
            return "COMMA";
        case TOKEN_SEMICOLON:
            return "SEMICOLON";
        case TOKEN_FUNCTION:
            return "FUNCTION";
        case TOKEN_VAR:
            return "VAR";
        case TOKEN_LET:
            return "LET";
        case TOKEN_IF:
            return "IF";
        case TOKEN_ELSE:
            return "ELSE";
        case TOKEN_WHILE:
            return "WHILE";
        case TOKEN_FOR:
            return "FOR";
        case TOKEN_RETURN:
            return "RETURN";
        case TOKEN_PRINT:
            return "PRINT";
        case TOKEN_TRUE:
            return "TRUE";
        case TOKEN_FALSE:
            return "FALSE";
        case TOKEN_NULL:
            return "NULL";
        case TOKEN_UNDERSCORE:
            return "UNDERSCORE";
        case TOKEN_FIXED:
            return "FIXED";
        case TOKEN_ASYNC:
            return "ASYNC";
        case TOKEN_AWAIT:
            return "AWAIT";
        case TOKEN_AND:
            return "AND";
        case TOKEN_OR:
            return "OR";
        case TOKEN_NOT:
            return "NOT";
        case TOKEN_UNSAFE:
            return "UNSAFE";
        case TOKEN_POINT:
            return "POINT";
        case TOKEN_CLASS:
            return "CLASS";
        case TOKEN_NEW:
            return "NEW";
        case TOKEN_DELETE:
            return "DELETE";
        case TOKEN_THIS:
            return "THIS";
        case TOKEN_PUBLIC:
            return "PUBLIC";
        case TOKEN_PRIVATE:
            return "PRIVATE";
        case TOKEN_PROTECTED:
            return "PROTECTED";
        case TOKEN_STATIC:
            return "STATIC";
        case TOKEN_VIRTUAL:
            return "VIRTUAL";
        case TOKEN_ABSTRACT:
            return "ABSTRACT";
        case TOKEN_OVERRIDE:
            return "OVERRIDE";
        case TOKEN_TILDE:
            return "TILDE";
        case TOKEN_COLON:
            return "COLON";
        case TOKEN_DOUBLE_COLON:
            return "DOUBLE_COLON";
        case TOKEN_DOT:
            return "DOT";
        case TOKEN_LEFT_BRACKET:
            return "LEFT_BRACKET";
        case TOKEN_RIGHT_BRACKET:
            return "RIGHT_BRACKET";
        case TOKEN_CONSOLE:
            return "CONSOLE";
        case TOKEN_TRY:
            return "TRY";
        case TOKEN_CATCH:
            return "CATCH";
        case TOKEN_FINALLY:
            return "FINALLY";
        case TOKEN_THROW:
            return "THROW";
        case TOKEN_EXCEPTION:
            return "EXCEPTION";
        case TOKEN_TEMPLATE:
            return "TEMPLATE";
        case TOKEN_TYPENAME:
            return "TYPENAME";
        case TOKEN_QUESTION:
            return "QUESTION";
        case TOKEN_QUESTION_COLON:
            return "QUESTION_COLON";
        case TOKEN_INT8:
            return "INT8";
        case TOKEN_INT16:
            return "INT16";
        case TOKEN_INT32:
            return "INT32";
        case TOKEN_INT64:
            return "INT64";
        case TOKEN_UINT8:
            return "UINT8";
        case TOKEN_UINT16:
            return "UINT16";
        case TOKEN_UINT32:
            return "UINT32";
        case TOKEN_UINT64:
            return "UINT64";
        case TOKEN_FLOAT32:
            return "FLOAT32";
        case TOKEN_FLOAT64:
            return "FLOAT64";
        case TOKEN_BOOL:
            return "BOOL";
        case TOKEN_VOID:
            return "VOID";
        case TOKEN_BASE:
            return "BASE";
        case TOKEN_USING:
            return "USING";

        case TOKEN_INCREMENT:
            return "INCREMENT";
        case TOKEN_DECREMENT:
            return "DECREMENT";
        case TOKEN_PLUS_ASSIGN:
            return "PLUS_ASSIGN";
        case TOKEN_MINUS_ASSIGN:
            return "MINUS_ASSIGN";
        case TOKEN_MUL_ASSIGN:
            return "MUL_ASSIGN";
        case TOKEN_DIV_ASSIGN:
            return "DIV_ASSIGN";
        case TOKEN_MOD_ASSIGN:
            return "MOD_ASSIGN";
        case TOKEN_ARROW:
            return "ARROW";
        case TOKEN_PIPE:
            return "PIPE";
        case TOKEN_DOLLAR:
            return "DOLLAR";
        case TOKEN_GET:
            return "GET";
        case TOKEN_SET:
            return "SET";
        case TOKEN_FROM:
            return "FROM";
        case TOKEN_SELECT:
            return "SELECT";
        case TOKEN_ORDERBY:
            return "ORDERBY";
        case TOKEN_GROUP:
            return "GROUP";
        case TOKEN_BY:
            return "BY";
        case TOKEN_JOIN:
            return "JOIN";
        case TOKEN_ON:
            return "ON";
        case TOKEN_EQUALS:
            return "EQUALS";
        case TOKEN_INTO:
            return "INTO";
        case TOKEN_LAMBDA:
            return "LAMBDA";
        case TOKEN_IS:
            return "IS";
        case TOKEN_AS:
            return "AS";
        case TOKEN_CONST:
            return "CONST";
        case TOKEN_EXTERN:
            return "EXTERN";
        case TOKEN_YIELD:
            return "YIELD";
        case TOKEN_LOCK:
            return "LOCK";
        case TOKEN_MATCH:
            return "MATCH";
        case TOKEN_WITH:
            return "WITH";
        case TOKEN_WHEN:
            return "WHEN";
        case TOKEN_PARAMS:
            return "PARAMS";
        case TOKEN_WHERE:
            return "WHERE";
        case TOKEN_OPERATOR:
            return "OPERATOR";
        case TOKEN_DELEGATE:
            return "DELEGATE";
        case TOKEN_SIZEOF:
            return "SIZEOF";
        case TOKEN_STACKALLOC:
            return "STACKALLOC";
        case TOKEN_OP_ADDITION:
            return "op_Addition";
        case TOKEN_OP_SUBTRACTION:
            return "op_Subtraction";
        case TOKEN_OP_MULTIPLY:
            return "op_Multiply";
        case TOKEN_OP_DIVISION:
            return "op_Division";
        case TOKEN_OP_MODULO:
            return "op_Modulo";
        case TOKEN_OP_EQUALITY:
            return "op_Equality";
        case TOKEN_OP_INEQUALITY:
            return "op_Inequality";
        case TOKEN_OP_LESS:
            return "op_LessThan";
        case TOKEN_OP_GREATER:
            return "op_GreaterThan";
        case TOKEN_OP_LESS_EQUAL:
            return "op_LessThanOrEqual";
        case TOKEN_OP_GREATER_EQUAL:
            return "op_GreaterThanOrEqual";
        case TOKEN_OP_BITWISE_AND:
            return "op_BitwiseAnd";
        case TOKEN_OP_BITWISE_OR:
            return "op_BitwiseOr";
        case TOKEN_OP_BITWISE_XOR:
            return "op_BitwiseXor";
        case TOKEN_OP_LEFT_SHIFT:
            return "op_LeftShift";
        case TOKEN_OP_RIGHT_SHIFT:
            return "op_RightShift";
        case TOKEN_OP_UNARY_PLUS:
            return "op_UnaryPlus";
        case TOKEN_OP_UNARY_MINUS:
            return "op_UnaryMinus";
        case TOKEN_OP_LOGICAL_NOT:
            return "op_LogicalNot";
        case TOKEN_OP_BITWISE_NOT:
            return "op_BitwiseNot";
        case TOKEN_OP_INCREMENT:
            return "op_Increment";
        case TOKEN_OP_DECREMENT:
            return "op_Decrement";
        case TOKEN_OP_TRUE:
            return "op_True";
        case TOKEN_OP_FALSE:
            return "op_False";
        case TOKEN_OP_IMPLICIT:
            return "op_Implicit";
        case TOKEN_OP_EXPLICIT:
            return "op_Explicit";
        default:
            return "UNKNOWN";
    }
}
