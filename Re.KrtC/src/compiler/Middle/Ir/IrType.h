#ifndef KRT_IR_TYPE_H
#define KRT_IR_TYPE_H

#include "Ir.h"

typedef enum {
    KRT_IR_TYPE_VOID,
#define KRT_INTEGER_WIDTH(bits) KRT_IR_TYPE_INT##bits,
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
#define KRT_INTEGER_WIDTH(bits) KRT_IR_TYPE_UINT##bits,
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
    KRT_IR_TYPE_FLOAT32,
    KRT_IR_TYPE_FLOAT64,
    KRT_IR_TYPE_BOOL,
    KRT_IR_TYPE_CHAR,
    KRT_IR_TYPE_STRING,
    KRT_IR_TYPE_POINTER,
    KRT_IR_TYPE_ARRAY,
    KRT_IR_TYPE_FUNCTION,
    KRT_IR_TYPE_STRUCT,
    KRT_IR_TYPE_CLASS,
    KRT_IR_TYPE_ANY,
    KRT_IR_TYPE_UNKNOWN,
} KrtIRTypeKind;

typedef enum {
    KRT_IR_TYPE_MOD_NONE = 0,
    KRT_IR_TYPE_MOD_CONST = 1 << 0,
    KRT_IR_TYPE_MOD_VOLATILE = 1 << 1,
    KRT_IR_TYPE_MOD_REFERENCE = 1 << 2,
} KrtIRTypeModifier;

struct KrtIRType;
typedef struct KrtIRType KrtIRType;

struct KrtIRType {
    KrtIRTypeKind kind;
    int modifiers;
    int size;
    int bit_width;
    int align;

    union {

        struct {
            KrtIRType* pointee;
        } pointer;

        struct {
            KrtIRType* element;
            int size;
        } array;

        struct {
            KrtIRType** params;
            int param_count;
            KrtIRType* ret;
        } function;

        struct {
            char* name;
            KrtIRType** fields;
            char** field_names;
            int field_count;
        } compound;
    } data;

    KrtIRType* next;
};

typedef struct {
    KrtIRType* types;
    int count;
} KrtIRTypePool;

void KrtIrTypePoolInit(KrtIRTypePool* pool);
void KrtIrTypePoolDestroy(KrtIRTypePool* pool);

/**
 * @brief Get or create a pool-owned integer type with an even width from 2 through 128 bits.
 * @return The type, or NULL for an unsupported width or allocation failure.
 */
KrtIRType* KrtIrTypeInteger(KrtIRTypePool* pool, int bits, bool is_unsigned);

KrtIRType* KrtIrTypeVoid(KrtIRTypePool* pool);
#define KRT_INTEGER_WIDTH(bits)                                                                                        \
    KrtIRType* KrtIrTypeInt##bits(KrtIRTypePool* pool);                                                                \
    KrtIRType* KrtIrTypeUint##bits(KrtIRTypePool* pool);
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
KrtIRType* KrtIrTypeFloat32(KrtIRTypePool* pool);
KrtIRType* KrtIrTypeFloat64(KrtIRTypePool* pool);
KrtIRType* KrtIrTypeBool(KrtIRTypePool* pool);
KrtIRType* KrtIrTypeChar(KrtIRTypePool* pool);
KrtIRType* KrtIrTypeString(KrtIRTypePool* pool);
KrtIRType* KrtIrTypeAny(KrtIRTypePool* pool);
KrtIRType* KrtIrTypeUnknown(KrtIRTypePool* pool);

KrtIRType* KrtIrTypePointer(KrtIRTypePool* pool, KrtIRType* pointee);
KrtIRType* KrtIrTypeArray(KrtIRTypePool* pool, KrtIRType* element, int size);
KrtIRType* KrtIrTypeFunction(KrtIRTypePool* pool, KrtIRType** params, int param_count, KrtIRType* ret);

KrtIRType* KrtIrTypeStruct(KrtIRTypePool* pool, const char* name);

KrtIRType* KrtIrTypeFromToken(KrtIRTypePool* pool, KrtTokenType token_type);

int KrtIrTypeSize(KrtIRType* type);

int KrtIrTypeAlign(KrtIRType* type);

const char* KrtIrTypeToString(KrtIRType* type);

bool KrtIrTypeEqual(KrtIRType* a, KrtIRType* b);

bool KrtIrTypeCompatible(KrtIRType* src, KrtIRType* dst);

bool KrtIrTypeIsInteger(KrtIRType* type);

bool KrtIrTypeIsUnsigned(KrtIRType* type);

bool KrtIrTypeIsFloat(KrtIRType* type);

bool KrtIrTypeIsNumeric(KrtIRType* type);

bool KrtIrTypeIsPointer(KrtIRType* type);

KrtIRType* KrtIrTypePointee(KrtIRType* type);

KrtIRType* KrtIrTypeBinaryResult(KrtIRTypePool* pool, KrtIRType* lhs, KrtIRType* rhs, KrtIROpcode op);

KrtIRType* KrtIrTypeCompareResult(KrtIRTypePool* pool);

KrtIRType* KrtIrTypePromote(KrtIRTypePool* pool, KrtIRType* type);

KrtIRType* KrtIrTypeCommon(KrtIRTypePool* pool, KrtIRType* a, KrtIRType* b);

bool KrtIrTypeCanAssign(KrtIRType* src, KrtIRType* dst);

bool KrtIrTypeCanCast(KrtIRType* src, KrtIRType* dst);

bool KrtIrTypeSupportsOp(KrtIRType* type, KrtIROpcode op);

KrtIRValue KrtIrTypeDefaultValue(KrtIRType* type);

#endif
