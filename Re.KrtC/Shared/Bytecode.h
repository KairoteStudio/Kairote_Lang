#ifndef KRT_BYTECODE_H
#define KRT_BYTECODE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef enum {
    OP_CONSTANT,
    OP_NULL,
    OP_TRUE,
    OP_FALSE,

    OP_POP,

    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_DEFINE_GLOBAL,
    OP_SET_GLOBAL,

    OP_EQUAL,
    OP_GREATER,
    OP_LESS,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_NOT,
    OP_NEGATE,

    OP_PRINT,

    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_LOOP,

    OP_CALL,
    OP_RETURN,

    OP_STK_ADJ,

    OP_INT_TO_STRING,

    OP_HALT
} KrtOpCode;

typedef enum {
    VAL_BOOL,
    VAL_NULL,
    VAL_NUMBER,
    VAL_OBJ,
    VAL_STRING_LITERAL,
} KrtValueType;

typedef struct KrtValue {
    KrtValueType type;
    union {
        bool boolean;
        double number;
        void* obj;
        const char* string_literal;
    } as;
} KrtValue;

typedef struct {
    int count;
    int capacity;
    KrtValue* values;
} KrtValueArray;

typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    KrtValueArray constants;
} KrtChunk;

#endif
