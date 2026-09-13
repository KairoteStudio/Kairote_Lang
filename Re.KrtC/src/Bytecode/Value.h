#ifndef KRT_VM_VALUE_H
#define KRT_VM_VALUE_H

#include "Core/Utils/KrtCommon.h"

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

#define BOOL_VAL(value) ((KrtValue){VAL_BOOL, {.boolean = value}})
#define NULL_VAL ((KrtValue){VAL_NULL, {.number = 0}})
#define NUMBER_VAL(value) ((KrtValue){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object) ((KrtValue){VAL_OBJ, {.obj = (void*)object}})
#define STRING_VAL(chars) ((KrtValue){VAL_STRING_LITERAL, {.string_literal = chars}})

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NULL(value) ((value).type == VAL_NULL)
#define IS_NUMBER(value) ((value).type == VAL_NUMBER)
#define IS_OBJ(value) ((value).type == VAL_OBJ)
#define IS_STRING_LIT(value) ((value).type == VAL_STRING_LITERAL)

#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)
#define AS_OBJ(value) ((value).as.obj)
#define AS_STRING_LIT(value) ((value).as.string_literal)

typedef struct {
    int capacity;
    int count;
    KrtValue* values;
} KrtValueArray;

void KrtValueArrayInit(KrtValueArray* array);
void KrtValueArrayWrite(KrtValueArray* array, KrtValue value);
void KrtValueArrayFree(KrtValueArray* array);
void KrtValuePrint(KrtValue value);

#endif
