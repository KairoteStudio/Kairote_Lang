#include "IrType.h"
#include <string.h>
#include <stdio.h>

void KrtIrTypePoolInit(KrtIRTypePool* pool) {
    if (!pool) {
        return;
    }
    pool->types = NULL;
    pool->count = 0;
}

void KrtIrTypePoolDestroy(KrtIRTypePool* pool) {
    if (!pool) {
        return;
    }

    KrtIRType* type = pool->types;
    while (type) {
        KrtIRType* next = type->next;

        if (type->kind == KRT_IR_TYPE_FUNCTION) {
            if (type->data.function.params) {
                KRT_FREE(type->data.function.params);
            }
        } else if (type->kind == KRT_IR_TYPE_STRUCT || type->kind == KRT_IR_TYPE_CLASS) {
            if (type->data.compound.name) {
                KRT_FREE(type->data.compound.name);
            }
            if (type->data.compound.fields) {
                KRT_FREE(type->data.compound.fields);
            }
            if (type->data.compound.field_names) {
                for (int i = 0; i < type->data.compound.field_count; i++) {
                    KRT_FREE(type->data.compound.field_names[i]);
                }
                KRT_FREE(type->data.compound.field_names);
            }
        }

        KRT_FREE(type);
        type = next;
    }

    pool->types = NULL;
    pool->count = 0;
}

static KrtIRType* create_type(KrtIRTypePool* pool, KrtIRTypeKind kind, int size, int align) {
    if (!pool) {
        return NULL;
    }

    KrtIRType* type = (KrtIRType*)KRT_CALLOC(1, sizeof(KrtIRType));
    if (!type) {
        return NULL;
    }

    type->kind = kind;
    type->modifiers = KRT_IR_TYPE_MOD_NONE;
    type->size = size;
    type->align = align;

    type->next = pool->types;
    pool->types = type;
    pool->count++;

    return type;
}

KrtIRType* KrtIrTypeVoid(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_VOID, 0, 1);
}

KrtIRType* KrtIrTypeInteger(KrtIRTypePool* pool, int bits, bool is_unsigned) {
    KrtTokenType token = KrtTokenIntegerType(bits, is_unsigned);
    if (!token) {
        return NULL;
    }
    int size = KrtTokenIntegerSize(token);
    KrtIRTypeKind kind = (KrtIRTypeKind)((is_unsigned ? KRT_IR_TYPE_UINT2 : KRT_IR_TYPE_INT2) + bits / 2 - 1);
    KrtIRType* type = create_type(pool, kind, size, size);
    if (type) {
        type->bit_width = bits;
    }
    return type;
}

#define KRT_INTEGER_WIDTH(bits)                                                                                        \
    KrtIRType* KrtIrTypeInt##bits(KrtIRTypePool* pool) {                                                               \
        return KrtIrTypeInteger(pool, bits, false);                                                                    \
    }                                                                                                                  \
    KrtIRType* KrtIrTypeUint##bits(KrtIRTypePool* pool) {                                                              \
        return KrtIrTypeInteger(pool, bits, true);                                                                     \
    }
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH

KrtIRType* KrtIrTypeFloat32(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_FLOAT32, 4, 4);
}

KrtIRType* KrtIrTypeFloat64(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_FLOAT64, 8, 8);
}

KrtIRType* KrtIrTypeBool(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_BOOL, 1, 1);
}

KrtIRType* KrtIrTypeChar(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_CHAR, 1, 1);
}

KrtIRType* KrtIrTypeString(KrtIRTypePool* pool) {

    return create_type(pool, KRT_IR_TYPE_STRING, 8, 8);
}

KrtIRType* KrtIrTypeAny(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_ANY, 8, 8);
}

KrtIRType* KrtIrTypeUnknown(KrtIRTypePool* pool) {
    return create_type(pool, KRT_IR_TYPE_UNKNOWN, 0, 1);
}

KrtIRType* KrtIrTypePointer(KrtIRTypePool* pool, KrtIRType* pointee) {
    if (!pool || !pointee) {
        return NULL;
    }

    KrtIRType* type = create_type(pool, KRT_IR_TYPE_POINTER, 8, 8);
    if (!type) {
        return NULL;
    }

    type->data.pointer.pointee = pointee;
    return type;
}

KrtIRType* KrtIrTypeArray(KrtIRTypePool* pool, KrtIRType* element, int size) {
    if (!pool || !element) {
        return NULL;
    }

    KrtIRType* type = create_type(pool, KRT_IR_TYPE_ARRAY, element->size * size, element->align);
    if (!type) {
        return NULL;
    }

    type->data.array.element = element;
    type->data.array.size = size;
    return type;
}

KrtIRType* KrtIrTypeFunction(KrtIRTypePool* pool, KrtIRType** params, int param_count, KrtIRType* ret) {
    if (!pool) {
        return NULL;
    }

    KrtIRType* type = create_type(pool, KRT_IR_TYPE_FUNCTION, 8, 8);
    if (!type) {
        return NULL;
    }

    type->data.function.ret = ret ? ret : KrtIrTypeVoid(pool);
    type->data.function.param_count = param_count;

    if (param_count > 0 && params) {
        type->data.function.params = (KrtIRType**)KRT_MALLOC(param_count * sizeof(KrtIRType*));
        if (type->data.function.params) {
            memcpy(type->data.function.params, params, param_count * sizeof(KrtIRType*));
        }
    } else {
        type->data.function.params = NULL;
    }

    return type;
}

KrtIRType* KrtIrTypeStruct(KrtIRTypePool* pool, const char* name) {
    if (!pool) {
        return NULL;
    }

    KrtIRType* type = create_type(pool, KRT_IR_TYPE_STRUCT, 0, 1);
    if (!type) {
        return NULL;
    }

    if (name) {
        type->data.compound.name = KRT_STRDUP(name);
    } else {
        type->data.compound.name = NULL;
    }
    type->data.compound.fields = NULL;
    type->data.compound.field_names = NULL;
    type->data.compound.field_count = 0;

    return type;
}

KrtIRType* KrtIrTypeFromToken(KrtIRTypePool* pool, KrtTokenType token_type) {
    if (!pool) {
        return NULL;
    }

    switch (token_type) {
    case TOKEN_VOID:
        return KrtIrTypeVoid(pool);
#define KRT_INTEGER_WIDTH(bits)                                                                                        \
    case TOKEN_INT##bits:                                                                                              \
        return KrtIrTypeInteger(pool, bits, false);                                                                    \
    case TOKEN_UINT##bits:                                                                                             \
        return KrtIrTypeInteger(pool, bits, true);
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
    case TOKEN_FLOAT32:
        return KrtIrTypeFloat32(pool);
    case TOKEN_FLOAT64:
        return KrtIrTypeFloat64(pool);
    case TOKEN_BOOL:
        return KrtIrTypeBool(pool);
    case TOKEN_CHAR:
        return KrtIrTypeChar(pool);
    case TOKEN_STRING:
    case TOKEN_TYPE_STRING:
        return KrtIrTypeString(pool);
    default:
        return KrtIrTypeUnknown(pool);
    }
}

int KrtIrTypeSize(KrtIRType* type) {
    if (!type) {
        return 0;
    }
    return type->size;
}

int KrtIrTypeAlign(KrtIRType* type) {
    if (!type) {
        return 1;
    }
    return type->align;
}

const char* KrtIrTypeToString(KrtIRType* type) {
    if (!type) {
        return "null";
    }

    switch (type->kind) {
    case KRT_IR_TYPE_VOID:
        return "void";
#define KRT_INTEGER_WIDTH(bits)                                                                                        \
    case KRT_IR_TYPE_INT##bits:                                                                                        \
        return "int" #bits;                                                                                            \
    case KRT_IR_TYPE_UINT##bits:                                                                                       \
        return "uint" #bits;
#include "../../../Core/Utils/IntegerWidths.def"
#undef KRT_INTEGER_WIDTH
    case KRT_IR_TYPE_FLOAT32:
        return "float32";
    case KRT_IR_TYPE_FLOAT64:
        return "float64";
    case KRT_IR_TYPE_BOOL:
        return "bool";
    case KRT_IR_TYPE_CHAR:
        return "char";
    case KRT_IR_TYPE_STRING:
        return "string";
    case KRT_IR_TYPE_POINTER:
        return "pointer";
    case KRT_IR_TYPE_ARRAY:
        return "array";
    case KRT_IR_TYPE_FUNCTION:
        return "function";
    case KRT_IR_TYPE_STRUCT:
        return type->data.compound.name ? type->data.compound.name : "struct";
    case KRT_IR_TYPE_CLASS:
        return type->data.compound.name ? type->data.compound.name : "class";
    case KRT_IR_TYPE_ANY:
        return "any";
    case KRT_IR_TYPE_UNKNOWN:
        return "unknown";
    default:
        return "invalid";
    }
}

bool KrtIrTypeEqual(KrtIRType* a, KrtIRType* b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    if (a->kind != b->kind) {
        return false;
    }

    if (a->kind == KRT_IR_TYPE_POINTER) {
        return KrtIrTypeEqual(a->data.pointer.pointee, b->data.pointer.pointee);
    }
    if (a->kind == KRT_IR_TYPE_ARRAY) {
        return KrtIrTypeEqual(a->data.array.element, b->data.array.element) && a->data.array.size == b->data.array.size;
    }

    return true;
}

bool KrtIrTypeCompatible(KrtIRType* src, KrtIRType* dst) {
    if (!src || !dst) {
        return false;
    }
    if (KrtIrTypeEqual(src, dst)) {
        return true;
    }

    if (dst->kind == KRT_IR_TYPE_ANY) {
        return true;
    }

    if (KrtIrTypeIsNumeric(src) && KrtIrTypeIsNumeric(dst)) {
        return true;
    }

    if (KrtIrTypeIsPointer(src) && KrtIrTypeIsPointer(dst)) {
        return true;
    }

    return false;
}

bool KrtIrTypeIsInteger(KrtIRType* type) {
    if (!type) {
        return false;
    }
    return type->kind >= KRT_IR_TYPE_INT2 && type->kind <= KRT_IR_TYPE_UINT128;
}

bool KrtIrTypeIsUnsigned(KrtIRType* type) {
    if (!type) {
        return false;
    }
    return type->kind >= KRT_IR_TYPE_UINT2 && type->kind <= KRT_IR_TYPE_UINT128;
}

bool KrtIrTypeIsFloat(KrtIRType* type) {
    if (!type) {
        return false;
    }
    return type->kind == KRT_IR_TYPE_FLOAT32 || type->kind == KRT_IR_TYPE_FLOAT64;
}

bool KrtIrTypeIsNumeric(KrtIRType* type) {
    return KrtIrTypeIsInteger(type) || KrtIrTypeIsFloat(type);
}

bool KrtIrTypeIsPointer(KrtIRType* type) {
    if (!type) {
        return false;
    }
    return type->kind == KRT_IR_TYPE_POINTER || type->kind == KRT_IR_TYPE_STRING;
}

KrtIRType* KrtIrTypePointee(KrtIRType* type) {
    if (!KrtIrTypeIsPointer(type)) {
        return NULL;
    }
    if (type->kind == KRT_IR_TYPE_POINTER) {
        return type->data.pointer.pointee;
    }
    return NULL;
}

KrtIRType* KrtIrTypeBinaryResult(KrtIRTypePool* pool, KrtIRType* lhs, KrtIRType* rhs, KrtIROpcode op) {
    if (!pool || !lhs || !rhs) {
        return KrtIrTypeUnknown(pool);
    }

    if (op >= KRT_IR_LT && op <= KRT_IR_NE) {
        return KrtIrTypeBool(pool);
    }

    if (!KrtIrTypeIsNumeric(lhs) || !KrtIrTypeIsNumeric(rhs)) {
        return KrtIrTypeUnknown(pool);
    }

    if ((op == KRT_IR_LSHIFT || op == KRT_IR_RSHIFT) && KrtIrTypeIsInteger(lhs) && KrtIrTypeIsInteger(rhs)) {
        return lhs;
    }

    if (lhs->kind == KRT_IR_TYPE_FLOAT64 || rhs->kind == KRT_IR_TYPE_FLOAT64) {
        return KrtIrTypeFloat64(pool);
    }

    if (lhs->kind == KRT_IR_TYPE_FLOAT32 || rhs->kind == KRT_IR_TYPE_FLOAT32) {
        return KrtIrTypeFloat32(pool);
    }

    int lhs_bits = lhs->bit_width;
    int rhs_bits = rhs->bit_width;

    if (lhs_bits > rhs_bits || (lhs_bits == rhs_bits && KrtIrTypeIsUnsigned(lhs))) {
        return lhs;
    } else {
        return rhs;
    }
}

KrtIRType* KrtIrTypeCompareResult(KrtIRTypePool* pool) {
    return pool ? KrtIrTypeBool(pool) : NULL;
}

KrtIRType* KrtIrTypePromote(KrtIRTypePool* pool, KrtIRType* type) {
    if (!pool || !type) {
        return NULL;
    }

    if (KrtIrTypeIsInteger(type) && type->size < 4) {
        if (KrtIrTypeIsUnsigned(type)) {
            return KrtIrTypeUint32(pool);
        } else {
            return KrtIrTypeInt32(pool);
        }
    }

    return type;
}

KrtIRType* KrtIrTypeCommon(KrtIRTypePool* pool, KrtIRType* a, KrtIRType* b) {
    if (!pool) {
        return NULL;
    }
    if (!a) {
        return b;
    }
    if (!b) {
        return a;
    }

    if (KrtIrTypeEqual(a, b)) {
        return a;
    }

    if (KrtIrTypeIsNumeric(a) && KrtIrTypeIsNumeric(b)) {

        if (KrtIrTypeIsFloat(a) || KrtIrTypeIsFloat(b)) {
            if (a->kind == KRT_IR_TYPE_FLOAT64 || b->kind == KRT_IR_TYPE_FLOAT64) {
                return KrtIrTypeFloat64(pool);
            }
            return KrtIrTypeFloat32(pool);
        }

        if (a->bit_width > b->bit_width || (a->bit_width == b->bit_width && KrtIrTypeIsUnsigned(a))) {
            return a;
        }
        return b;
    }

    return KrtIrTypeAny(pool);
}

bool KrtIrTypeCanAssign(KrtIRType* src, KrtIRType* dst) {
    return KrtIrTypeCompatible(src, dst);
}

bool KrtIrTypeCanCast(KrtIRType* src, KrtIRType* dst) {
    if (!src || !dst) {
        return false;
    }

    if (KrtIrTypeEqual(src, dst)) {
        return true;
    }

    if (KrtIrTypeIsNumeric(src) && KrtIrTypeIsNumeric(dst)) {
        return true;
    }

    if (KrtIrTypeIsPointer(src) && KrtIrTypeIsInteger(dst)) {
        return true;
    }
    if (KrtIrTypeIsInteger(src) && KrtIrTypeIsPointer(dst)) {
        return true;
    }

    if (KrtIrTypeIsPointer(src) && KrtIrTypeIsPointer(dst)) {
        return true;
    }

    return false;
}

bool KrtIrTypeSupportsOp(KrtIRType* type, KrtIROpcode op) {
    if (!type) {
        return false;
    }

    switch (op) {
    case KRT_IR_ADD:
    case KRT_IR_SUB:
    case KRT_IR_MUL:
    case KRT_IR_DIV:
    case KRT_IR_MOD:
        return KrtIrTypeIsNumeric(type);

    case KRT_IR_AND:
    case KRT_IR_OR:
    case KRT_IR_XOR:
    case KRT_IR_LSHIFT:
    case KRT_IR_RSHIFT:
        return KrtIrTypeIsInteger(type);

    case KRT_IR_LT:
    case KRT_IR_GT:
    case KRT_IR_LE:
    case KRT_IR_GE:
        return KrtIrTypeIsNumeric(type);

    case KRT_IR_EQ:
    case KRT_IR_NE:
        return true;

    default:
        return true;
    }
}

KrtIRValue KrtIrTypeDefaultValue(KrtIRType* type) {
    KrtIRValue value = {0};
    value.type = KRT_IR_VALUE_IMM;
    value.data.imm = 0;

    if (!type) {
        return value;
    }

    switch (type->kind) {
    case KRT_IR_TYPE_FLOAT32:
    case KRT_IR_TYPE_FLOAT64:
        value.data.imm = 0.0;
        break;
    case KRT_IR_TYPE_BOOL:
        value.data.imm = 0;
        break;
    case KRT_IR_TYPE_POINTER:
    case KRT_IR_TYPE_STRING:

        value.data.imm = 0;
        break;
    default:
        value.data.imm = 0;
        break;
    }

    return value;
}
