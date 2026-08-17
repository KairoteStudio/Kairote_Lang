#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stdlib.h>
#include "../../../Core/Memory/Arena.h"

#ifdef _WIN32

#endif

typedef enum {
    TOKEN_EOF,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_CHAR_LITERAL,
    TOKEN_TYPE_STRING,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_MULTIPLY,
    TOKEN_DIVIDE,
    TOKEN_MODULO,
    TOKEN_ASSIGN,
    TOKEN_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER_EQUAL,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_DOUBLE_COLON,
    TOKEN_DOT,
    TOKEN_ARROW,
    TOKEN_PIPE,
    TOKEN_DOLLAR,
    TOKEN_FUNCTION,
    TOKEN_VAR,
    TOKEN_LET,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_FOREACH,
    TOKEN_IN,
    TOKEN_RETURN,
    TOKEN_PRINT,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
    TOKEN_UNDERSCORE,
    TOKEN_FIXED,
    TOKEN_ASYNC,
    TOKEN_AWAIT,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_NEW,
    TOKEN_DELETE,
    TOKEN_CLASS,
    TOKEN_STRUCT,
    TOKEN_INTERFACE,
    TOKEN_ENUM,
    TOKEN_NAMESPACE,
    TOKEN_THIS,
    TOKEN_BASE,
    TOKEN_PUBLIC,
    TOKEN_PRIVATE,
    TOKEN_PROTECTED,
    TOKEN_STATIC,
    TOKEN_VIRTUAL,
    TOKEN_ABSTRACT,
    TOKEN_OVERRIDE,
    TOKEN_USING,
    TOKEN_CONSOLE,
    TOKEN_INCREMENT,
    TOKEN_DECREMENT,
    TOKEN_PLUS_ASSIGN,
    TOKEN_MINUS_ASSIGN,
    TOKEN_MUL_ASSIGN,
    TOKEN_DIV_ASSIGN,
    TOKEN_MOD_ASSIGN,
    TOKEN_INT8,
    TOKEN_INT16,
    TOKEN_INT32,
    TOKEN_INT64,
    TOKEN_UINT8,
    TOKEN_UINT16,
    TOKEN_UINT32,
    TOKEN_UINT64,
    TOKEN_FLOAT32,
    TOKEN_FLOAT64,
    TOKEN_BOOL,
    TOKEN_CHAR,
    TOKEN_VOID,
    TOKEN_TILDE,
    TOKEN_BITWISE_AND,
    TOKEN_BITWISE_OR,
    TOKEN_BITWISE_XOR,
    TOKEN_LSHIFT,
    TOKEN_RSHIFT,
    TOKEN_UNSAFE,
    TOKEN_POINT,
    TOKEN_POWER,

    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_FINALLY,
    TOKEN_THROW,
    TOKEN_EXCEPTION,

    TOKEN_TEMPLATE,
    TOKEN_TYPENAME,
    TOKEN_WHERE,
    TOKEN_QUESTION,
    TOKEN_QUESTION_COLON,
    TOKEN_NULL_COALESCING,
    TOKEN_QUESTION_DOT,

    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_DEFAULT,

    TOKEN_GET,           
    TOKEN_SET,           
    TOKEN_FROM,          
    TOKEN_SELECT,        
    
    TOKEN_ORDERBY,       
    TOKEN_GROUP,         
    TOKEN_BY,            
    TOKEN_JOIN,          
    TOKEN_ON,            
    TOKEN_EQUALS,        
    TOKEN_INTO,          
    TOKEN_LAMBDA,        
    TOKEN_ATTRIBUTE,     

    TOKEN_IS,
    TOKEN_AS,
    TOKEN_CONST,
    TOKEN_EXTERN,
    TOKEN_YIELD,
    TOKEN_LOCK,
    TOKEN_MATCH,
    TOKEN_WITH,
    TOKEN_WHEN,
    TOKEN_PARAMS,
    TOKEN_OPERATOR,
    TOKEN_DELEGATE,
    TOKEN_SIZEOF,
    TOKEN_STACKALLOC,

    TOKEN_OP_ADDITION,
    TOKEN_OP_SUBTRACTION,
    TOKEN_OP_MULTIPLY,
    TOKEN_OP_DIVISION,
    TOKEN_OP_MODULO,
    TOKEN_OP_EQUALITY,
    TOKEN_OP_INEQUALITY,
    TOKEN_OP_LESS,
    TOKEN_OP_GREATER,
    TOKEN_OP_LESS_EQUAL,
    TOKEN_OP_GREATER_EQUAL,
    TOKEN_OP_BITWISE_AND,
    TOKEN_OP_BITWISE_OR,
    TOKEN_OP_BITWISE_XOR,
    TOKEN_OP_LEFT_SHIFT,
    TOKEN_OP_RIGHT_SHIFT,
    TOKEN_OP_UNARY_PLUS,
    TOKEN_OP_UNARY_MINUS,
    TOKEN_OP_LOGICAL_NOT,
    TOKEN_OP_BITWISE_NOT,
    TOKEN_OP_INCREMENT,
    TOKEN_OP_DECREMENT,
    TOKEN_OP_TRUE,
    TOKEN_OP_FALSE,
    TOKEN_OP_IMPLICIT,
    TOKEN_OP_EXPLICIT,

    TOKEN_AUTO,

    TOKEN_UNKNOWN
} KrtTokenType;

typedef struct {
    KrtTokenType type;
    char* value;
    int line;
    int column;
} Token;

typedef struct {
    const char* source;
    char* source_owned;
    int position;
    int line;
    int column;
    Token current_token;
    KrtArena* arena;  
} Lexer;

Lexer* lexer_create(const char* source);
void lexer_destroy(Lexer* lexer);
void lexer_reset(Lexer* lexer);
Token lexer_next_token(Lexer* lexer);
Token lexer_peek_token(Lexer* lexer);
Token lexer_peek_nth_token(Lexer* lexer, int n);
void token_free(Token* token);
const char* token_type_to_string(KrtTokenType type);

#endif
