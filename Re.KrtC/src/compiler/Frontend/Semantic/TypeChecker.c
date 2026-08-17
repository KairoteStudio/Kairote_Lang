#include "TypeChecker.h"
#include "SemanticAnalyzer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Core/Utils/KrtCommon.h"
#include "SymbolTable.h"
#include "Accelerator.h"

const char* type_get_name(Type* type);

static const struct {
    KrtTokenType token;
    TypeKind    kind;
} TOKEN_TYPE_MAP[] = {
    {TOKEN_INT8, TYPE_INT8}, {TOKEN_INT16, TYPE_INT16},
    {TOKEN_INT32, TYPE_INT32}, {TOKEN_INT64, TYPE_INT64},
    {TOKEN_UINT8, TYPE_UINT8}, {TOKEN_UINT16, TYPE_UINT16},
    {TOKEN_UINT32, TYPE_UINT32}, {TOKEN_UINT64, TYPE_UINT64},
    {TOKEN_FLOAT32, TYPE_FLOAT32}, {TOKEN_FLOAT64, TYPE_FLOAT64},
    {TOKEN_BOOL, TYPE_BOOL}, {TOKEN_CHAR, TYPE_INT8}, {TOKEN_STRING, TYPE_STRING},
    {TOKEN_VOID, TYPE_VOID},
    {0, TYPE_UNKNOWN}
};

static TypeKind token_to_type_kind(KrtTokenType token) {
    for (int i = 0; TOKEN_TYPE_MAP[i].token; i++)
        if (TOKEN_TYPE_MAP[i].token == token)
            return TOKEN_TYPE_MAP[i].kind;
    return TYPE_UNKNOWN;
}

#define CHECK_EXPR(ctx, node, var) \
    Type* var = type_check_expression(ctx, node); \
    if (!var) return NULL

#define CHECK_EXPR_STMT(ctx, node, var) \
    Type* var = type_check_expression(ctx, node); \
    if (!var) return 0

static const struct {
    const char* name;
    TypeKind    kind;
} BUILTIN_TYPES[] = {
    {"int8", TYPE_INT8}, {"int16", TYPE_INT16},
    {"int32", TYPE_INT32}, {"int64", TYPE_INT64},
    {"uint8", TYPE_UINT8}, {"uint16", TYPE_UINT16},
    {"uint32", TYPE_UINT32}, {"uint64", TYPE_UINT64},
    {"float32", TYPE_FLOAT32}, {"float64", TYPE_FLOAT64},
    {"bool", TYPE_BOOL}, {"string", TYPE_STRING},
    {"void", TYPE_VOID},
    {NULL, TYPE_UNKNOWN}
};

static const struct {
    const char* name;
    TypeKind    ret_kind;
    int         param_count;
    TypeKind    param_kinds[2];
} BUILTIN_FUNCS[] = {
    
    {"print_number", TYPE_VOID, 1, {TYPE_FLOAT64}},
    {"print_string", TYPE_VOID, 1, {TYPE_STRING}},
    {"print_float", TYPE_VOID, 1, {TYPE_FLOAT64}},
    {"print_int", TYPE_VOID, 1, {TYPE_INT64}},
    {"print_int64", TYPE_VOID, 1, {TYPE_INT64}},
    {"println_int", TYPE_VOID, 1, {TYPE_INT32}},
    {"println_string", TYPE_VOID, 1, {TYPE_STRING}},
    {"println_float", TYPE_VOID, 1, {TYPE_FLOAT64}},
    {"println", TYPE_VOID, 1, {TYPE_STRING}},
    {"print", TYPE_VOID, 1, {TYPE_STRING}},
    
    {"get_total_memory", TYPE_INT64, 0, {TYPE_UNKNOWN}},
    {"get_free_memory", TYPE_INT64, 0, {TYPE_UNKNOWN}},
    
    {"timer_start", TYPE_FLOAT64, 0, {TYPE_UNKNOWN}},
    {"timer_elapsed", TYPE_FLOAT64, 0, {TYPE_UNKNOWN}},
    {"timer_current", TYPE_FLOAT64, 0, {TYPE_UNKNOWN}},
    
    {"timer_start_int", TYPE_INT64, 0, {TYPE_UNKNOWN}},
    {"timer_elapsed_int", TYPE_INT64, 0, {TYPE_UNKNOWN}},
    {"timer_current_int", TYPE_INT64, 0, {TYPE_UNKNOWN}},
    {NULL, TYPE_UNKNOWN, 0, {TYPE_UNKNOWN}}
};

typedef int (*StatementChecker)(TypeCheckContext*, ASTNode*);
typedef Type* (*ExpressionChecker)(TypeCheckContext*, ASTNode*);

static int check_variable_declaration(TypeCheckContext* ctx, ASTNode* stmt);
static int check_assignment(TypeCheckContext* ctx, ASTNode* stmt);
static int check_array_assignment(TypeCheckContext* ctx, ASTNode* stmt);
static int check_return_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_if_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_while_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_for_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_foreach_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_block(TypeCheckContext* ctx, ASTNode* stmt);
static int check_function_declaration(TypeCheckContext* ctx, ASTNode* stmt);
static int check_class_declaration(TypeCheckContext* ctx, ASTNode* stmt);
static int check_try_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_throw_statement(TypeCheckContext* ctx, ASTNode* stmt);
static int check_print_statement(TypeCheckContext* ctx, ASTNode* stmt);

static int check_constructor_declaration(TypeCheckContext* ctx, ASTNode* stmt);
static int check_destructor_declaration(TypeCheckContext* ctx, ASTNode* stmt);
static int check_namespace_declaration(TypeCheckContext* ctx, ASTNode* stmt);
static int check_catch_clause(TypeCheckContext* ctx, ASTNode* stmt);
static int check_finally_clause(TypeCheckContext* ctx, ASTNode* stmt);
static int check_access_modifier(TypeCheckContext* ctx, ASTNode* stmt);

static Type* check_number_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_string_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_char_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_boolean_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_identifier(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_binary_operation(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_ternary_operation(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_unary_operation(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_array_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_member_access(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_new_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_this_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_static_method_call(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_call_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_array_access(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_assignment_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_lambda_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_linq_query(TypeCheckContext* ctx, ASTNode* expr);

Type* infer_return_type_from_statement(TypeCheckContext* context, ASTNode* statement);
Type* infer_function_return_type(TypeCheckContext* context, ASTNode* function_body);
static int type_check_class_body(TypeCheckContext* context, ClassInfo* class_info, ASTNode* body, AccessModifier default_access);
static int type_check_add_class_member(TypeCheckContext* context, ClassInfo* class_info,
                                       ASTNode* member, AccessModifier access, int is_static);
static void type_check_symbol_table_destroy(TypeCheckSymbolTable* table);
static void type_check_symbol_table_ref(TypeCheckSymbolTable* table);
void type_check_symbol_table_unref(TypeCheckSymbolTable* table);
TypeCheckSymbol* type_check_symbol_table_lookup(TypeCheckSymbolTable* table, const char* name);

Type* type_create_basic(TypeKind kind) {
    Type* type = (Type*)KRT_CALLOC(1, sizeof(Type));
    if (!type) {
        return NULL;
    }
    type->kind = kind;
    return type;
}

Type* type_create_pointer(Type* base_type) {
    Type* type = (Type*)KRT_CALLOC(1, sizeof(Type));
    if (!type) {
        return NULL;
    }
    type->kind = TYPE_POINTER;
    type->data.pointer_to = base_type;
    return type;
}

Type* type_create_array(Type* element_type, int size) {
    Type* type = (Type*)KRT_CALLOC(1, sizeof(Type));
    if (!type) {
        return NULL;
    }
    type->kind = TYPE_ARRAY;
    type->data.array.element_type = element_type;
    type->data.array.size = size;
    return type;
}

Type* type_create_function(Type* return_type, Type** param_types, int param_count) {
    Type* type = (Type*)KRT_CALLOC(1, sizeof(Type));
    if (!type) {
        return NULL;
    }
    type->kind = TYPE_FUNCTION;
    
    type->data.function.return_type = return_type ? type_copy(return_type) : NULL;
    
    if (param_count > 0 && param_types) {
        type->data.function.parameter_types = (Type**)KRT_MALLOC(sizeof(Type*) * param_count);
        if (!type->data.function.parameter_types) {
            type_destroy(type);
            return NULL;
        }
        for (int i = 0; i < param_count; i++) {
            type->data.function.parameter_types[i] = param_types[i] ? type_copy(param_types[i]) : NULL;
        }
    } else {
        type->data.function.parameter_types = NULL;
    }
    type->data.function.parameter_count = param_count;
    type->data.function.function_scope = NULL;
    return type;
}

Type* type_create_class(const char* class_name) {
    Type* type = (Type*)KRT_CALLOC(1, sizeof(Type));
    if (!type) {
        return NULL;
    }
    type->kind = TYPE_CLASS;
    type->data.class.class_name = KRT_STRDUP(class_name);
    type->data.class.field_types = NULL;
    type->data.class.field_names = NULL;
    type->data.class.field_count = 0;
    type->data.class.class_info = NULL;
    return type;
}

ClassInfo* class_info_create(const char* class_name) {
    ClassInfo* info = (ClassInfo*)KRT_MALLOC(sizeof(ClassInfo));
    if (!info) {
        return NULL;
    }
    info->class_name = KRT_STRDUP(class_name);
    info->base_class_name = NULL;
    info->members = NULL;
    info->member_count = 0;
    info->member_capacity = 0;
    info->member_scope = type_check_symbol_table_create(NULL);
    return info;
}

void class_info_destroy(ClassInfo* class_info) {
    if (!class_info) return;

    KRT_FREE(class_info->class_name);
    if (class_info->base_class_name) {
        KRT_FREE(class_info->base_class_name);
    }

    if (class_info->member_scope) {
        type_check_symbol_table_unref(class_info->member_scope);
        class_info->member_scope = NULL;
    }

    for (int i = 0; i < class_info->member_count; i++) {
        if (class_info->members[i].name) {
            KRT_FREE(class_info->members[i].name);
        }
        if (class_info->members[i].type) {
            type_destroy(class_info->members[i].type);
        }
    }

    if (class_info->members) {
        KRT_FREE(class_info->members);
    }

    KRT_FREE(class_info);
}

void class_info_add_member(ClassInfo* class_info, ClassMember member) {
    if (!class_info) return;

    if (class_info->member_count >= class_info->member_capacity) {
        int new_capacity = class_info->member_capacity == 0 ? 16 : class_info->member_capacity * 2;
        ClassMember* new_members = (ClassMember*)KRT_REALLOC(class_info->members,
                                                         new_capacity * sizeof(ClassMember));
        if (!new_members) {
            return;
        }
        class_info->members = new_members;
        class_info->member_capacity = new_capacity;
    }

    class_info->members[class_info->member_count] = member;
    class_info->member_count++;

}

ClassMember* class_info_find_member(ClassInfo* class_info, const char* member_name) {
    if (!class_info || !member_name) return NULL;

    for (int i = 0; i < class_info->member_count; i++) {
        if (strcmp(class_info->members[i].name, member_name) == 0) {
            return &class_info->members[i];
        }
    }

    return NULL;
}

AccessModifier token_to_access_modifier(KrtTokenType token) {
    switch (token) {
        case TOKEN_PUBLIC:
            return ACCESS_PUBLIC;
        case TOKEN_PRIVATE:
            return ACCESS_PRIVATE;
        case TOKEN_PROTECTED:
            return ACCESS_PROTECTED;
        default:
            return ACCESS_PUBLIC;
    }
}

void type_function_add_parameter(Type* func_type, Type* param_type) {
    if (!func_type || func_type->kind != TYPE_FUNCTION || !param_type) {
        return;
    }

    int new_count = func_type->data.function.parameter_count + 1;
    Type** new_params = (Type**)KRT_REALLOC(func_type->data.function.parameter_types,
                                         new_count * sizeof(Type*));
    if (!new_params) {
        return;
    }

    func_type->data.function.parameter_types = new_params;
    func_type->data.function.parameter_types[func_type->data.function.parameter_count] = param_type;
    func_type->data.function.parameter_count = new_count;
}

void type_destroy(Type* type) {
    if (!type) return;

    if (type->kind == TYPE_ARRAY) {
    }

    switch (type->kind) {
        case TYPE_POINTER:
            type_destroy(type->data.pointer_to);
            break;
        case TYPE_ARRAY:
            type_destroy(type->data.array.element_type);
            break;
        case TYPE_FUNCTION:
            type_destroy(type->data.function.return_type);
            for (int i = 0; i < type->data.function.parameter_count; i++) {
                type_destroy(type->data.function.parameter_types[i]);
            }
            KRT_FREE(type->data.function.parameter_types);
            break;
        case TYPE_CLASS:
            if (type->data.class.class_name) {
                KRT_FREE(type->data.class.class_name);
                type->data.class.class_name = NULL;
            }
            for (int i = 0; i < type->data.class.field_count; i++) {
                if (type->data.class.field_types[i]) {
                    type_destroy(type->data.class.field_types[i]);
                    type->data.class.field_types[i] = NULL;
                }
                if (type->data.class.field_names[i]) {
                    KRT_FREE(type->data.class.field_names[i]);
                    type->data.class.field_names[i] = NULL;
                }
            }
            KRT_FREE(type->data.class.field_types);
            type->data.class.field_types = NULL;
            KRT_FREE(type->data.class.field_names);
            type->data.class.field_names = NULL;

            break;
        default:
            break;
    }

    KRT_FREE(type);
}

Type* type_copy(Type* type) {
    if (!type) return NULL;

    Type* new_type = KRT_CALLOC(1, sizeof(Type));
    if (!new_type) return NULL;

    new_type->kind = type->kind;

    switch (type->kind) {
        case TYPE_POINTER:
            new_type->data.pointer_to = type_copy(type->data.pointer_to);
            break;

        case TYPE_ARRAY:
            new_type->data.array.element_type = type_copy(type->data.array.element_type);
            new_type->data.array.size = type->data.array.size;
            break;

        case TYPE_FUNCTION:

            new_type->data.function.return_type = type_copy(type->data.function.return_type);

            if (type->data.function.parameter_count > 0) {
                new_type->data.function.parameter_types = KRT_MALLOC(sizeof(Type*) * type->data.function.parameter_count);
                if (!new_type->data.function.parameter_types) {
                    type_destroy(new_type);
                    return NULL;
                }

                for (int i = 0; i < type->data.function.parameter_count; i++) {
                    new_type->data.function.parameter_types[i] = type_copy(type->data.function.parameter_types[i]);
                }
                new_type->data.function.parameter_count = type->data.function.parameter_count;
            } else {
                new_type->data.function.parameter_types = NULL;
                new_type->data.function.parameter_count = 0;
            }
            break;

        case TYPE_CLASS:

            if (type->data.class.class_name) {
                new_type->data.class.class_name = KRT_STRDUP(type->data.class.class_name);
            } else {
                new_type->data.class.class_name = NULL;
            }

            if (type->data.class.field_count > 0) {
                new_type->data.class.field_types = KRT_MALLOC(sizeof(Type*) * type->data.class.field_count);
                new_type->data.class.field_names = KRT_MALLOC(sizeof(char*) * type->data.class.field_count);

                if (!new_type->data.class.field_types || !new_type->data.class.field_names) {
                    if (new_type->data.class.field_types) KRT_FREE(new_type->data.class.field_types);
                    if (new_type->data.class.field_names) KRT_FREE(new_type->data.class.field_names);
                    type_destroy(new_type);
                    return NULL;
                }

                for (int i = 0; i < type->data.class.field_count; i++) {
                    new_type->data.class.field_types[i] = type_copy(type->data.class.field_types[i]);
                    if (type->data.class.field_names[i]) {
                        new_type->data.class.field_names[i] = KRT_STRDUP(type->data.class.field_names[i]);
                    } else {
                        new_type->data.class.field_names[i] = NULL;
                    }
                }
                new_type->data.class.field_count = type->data.class.field_count;
            } else {
                new_type->data.class.field_types = NULL;
                new_type->data.class.field_names = NULL;
                new_type->data.class.field_count = 0;
            }

            new_type->data.class.class_info = type->data.class.class_info;
            break;

        default:

            break;
    }

    return new_type;
}

TypeCheckSymbolTable* type_check_symbol_table_create(TypeCheckSymbolTable* parent) {
    TypeCheckSymbolTable* table = (TypeCheckSymbolTable*)KRT_MALLOC(sizeof(TypeCheckSymbolTable));
    if (!table) {
        return NULL;
    }

    table->symbols = NULL;
    table->symbol_count = 0;
    table->capacity = 0;
    table->parent = parent;
    table->ref_count = 1;

    if (parent) {
        type_check_symbol_table_ref(parent);
    }

    return table;
}

static void type_check_symbol_table_ref(TypeCheckSymbolTable* table) {
    if (table) {
        table->ref_count++;
    }
}

void type_check_symbol_table_unref(TypeCheckSymbolTable* table) {
    if (!table) return;

    table->ref_count--;
    if (table->ref_count <= 0) {
        type_check_symbol_table_destroy(table);
    }
}

static void type_check_symbol_table_destroy(TypeCheckSymbolTable* table) {
    if (!table) return;

    if (table->symbols && (uintptr_t)table->symbols >= 0x1000) {

        if (table->symbol_count > 0 && table->symbol_count <= table->capacity) {
            for (int i = 0; i < table->symbol_count; i++) {

                if (table->symbols[i].name && (uintptr_t)table->symbols[i].name >= 0x1000 &&
                    table->symbols[i].owns_name) {
                    KRT_FREE(table->symbols[i].name);
                    table->symbols[i].name = NULL;
                }

                if (table->symbols[i].type) {
                    type_destroy(table->symbols[i].type);
                    table->symbols[i].type = NULL;
                }
            }
        }
        
        KRT_FREE(table->symbols);
        table->symbols = NULL;
        table->symbol_count = 0;
        table->capacity = 0;
    }
    KRT_FREE(table);
}

int type_check_symbol_table_add(TypeCheckSymbolTable* table, TypeCheckSymbol symbol) {
    if (!table) return 0;

    for (int i = 0; i < table->symbol_count; i++) {
        if (strcmp(table->symbols[i].name, symbol.name) == 0) {
            return 0;
        }
    }

    if (table->symbol_count >= table->capacity) {
        int new_capacity = table->capacity == 0 ? 16 : table->capacity * 2;
        TypeCheckSymbol* new_symbols;
        if (table->symbols == NULL) {
            new_symbols = (TypeCheckSymbol*)KRT_MALLOC(new_capacity * sizeof(TypeCheckSymbol));
        } else {
            new_symbols = (TypeCheckSymbol*)KRT_REALLOC(table->symbols, new_capacity * sizeof(TypeCheckSymbol));
        }
        if (!new_symbols) {
            return 0;
        }
        table->symbols = new_symbols;
        table->capacity = new_capacity;
    }

    table->symbols[table->symbol_count] = symbol;
    table->symbol_count++;

    return 1;
}

TypeCheckSymbol* type_check_symbol_table_lookup(TypeCheckSymbolTable* table, const char* name) {
    TypeCheckSymbolTable* current = table;

    while (current) {
        for (int i = 0; i < current->symbol_count; i++) {
            if (strcmp(current->symbols[i].name, name) == 0) {
                return &current->symbols[i];
            }
        }
        current = current->parent;
    }

    return NULL;
}

TypeCheckContext* type_check_context_create(void* semantic_analyzer) {
    TypeCheckContext* context = (TypeCheckContext*)KRT_MALLOC(sizeof(TypeCheckContext));
    if (!context) {
        return NULL;
    }

    context->current_scope = type_check_symbol_table_create(NULL);
    context->error_count = 0;
    context->warning_count = 0;
    context->scope_ownership = 1;
    context->semantic_analyzer = semantic_analyzer;

    type_check_init_builtin_types(context);
    type_check_init_builtin_functions(context);

    return context;
}

void type_check_context_destroy(TypeCheckContext* context) {
    if (!context) return;

    if (context->scope_ownership && context->current_scope) {

        type_check_symbol_table_unref(context->current_scope);
    }

    KRT_FREE(context);
}

int type_is_compatible(Type* type1, Type* type2) {
    if (!type1 || !type2) return 0;

    if (type1->kind == TYPE_UNKNOWN || type2->kind == TYPE_UNKNOWN) {
        return 1;
    }

    if (type1->kind == type2->kind) {
        return 1;
    }

    if ((type1->kind >= TYPE_INT8 && type1->kind <= TYPE_FLOAT64) &&
        (type2->kind >= TYPE_INT8 && type2->kind <= TYPE_FLOAT64)) {
        return 1;
    }

    if (type1->kind == TYPE_POINTER && type2->kind == TYPE_POINTER) {
        return type_is_compatible(type1->data.pointer_to, type2->data.pointer_to);
    }

    if (type1->kind == TYPE_STRING && type2->kind == TYPE_ARRAY &&
        type2->data.array.element_type->kind == TYPE_INT8) {
        return 1;
    }

    if (type2->kind == TYPE_STRING && type1->kind == TYPE_ARRAY &&
        type1->data.array.element_type->kind == TYPE_INT8) {
        return 1;
    }

    if (type1->kind == TYPE_POINTER && type2->kind == TYPE_VOID) {
        return 1;
    }

    if (type2->kind == TYPE_POINTER && type1->kind == TYPE_VOID) {
        return 1;
    }

    if (type1->kind == TYPE_BOOL && type2->kind == TYPE_BOOL) {
        return 1;
    }

    return 0;
}

const char* type_get_name(Type* type) {
    if (!type) return "unknown";

    switch (type->kind) {
        case TYPE_VOID: return "void";
        case TYPE_INT8: return "int8";
        case TYPE_INT16: return "int16";
        case TYPE_INT32: return "int32";
        case TYPE_INT64: return "int64";
        case TYPE_UINT8: return "uint8";
        case TYPE_UINT16: return "uint16";
        case TYPE_UINT32: return "uint32";
        case TYPE_UINT64: return "uint64";
        case TYPE_FLOAT32: return "float32";
        case TYPE_FLOAT64: return "float64";
        case TYPE_BOOL: return "bool";
        case TYPE_STRING: return "string";
        case TYPE_POINTER: return "pointer";
        case TYPE_ARRAY: return "array";
        case TYPE_FUNCTION: return "function";
        case TYPE_CLASS: return "class";
        case TYPE_UNKNOWN: return "unknown";
        default: return "unknown";
    }
}

int type_is_assignable(Type* target, Type* source) {
    if (!target || !source) return 0;

    if (target->kind == source->kind) {
        return 1;
    }

    if ((target->kind >= TYPE_INT8 && target->kind <= TYPE_FLOAT64) &&
        (source->kind >= TYPE_INT8 && source->kind <= TYPE_FLOAT64)) {

        if (target->kind == TYPE_FLOAT64 || target->kind == TYPE_FLOAT32) {

            return 1;
        } else if (target->kind >= TYPE_INT32 && source->kind <= TYPE_INT32) {

            return 1;
        } else if (target->kind >= TYPE_UINT32 && source->kind <= TYPE_UINT32) {

            return 1;
        }

        return 0;
    }

    if (target->kind == TYPE_POINTER && source->kind == TYPE_POINTER) {
        return type_is_compatible(target->data.pointer_to, source->data.pointer_to);
    }

    if (target->kind == TYPE_POINTER && source->kind == TYPE_VOID) {
        return 1;
    }

    if (target->kind == TYPE_VOID && source->kind == TYPE_POINTER) {
        return 1;
    }

    if (target->kind == TYPE_POINTER && source->kind == TYPE_CLASS) {
        return 1;
    }

    if (target->kind == TYPE_CLASS && source->kind == TYPE_POINTER) {
        if (source->data.pointer_to && source->data.pointer_to->kind == TYPE_CLASS) {
            return 1;
        }
    }

    if (target->kind == TYPE_STRING && source->kind == TYPE_ARRAY &&
        source->data.array.element_type->kind == TYPE_INT8) {
        return 1;
    }

    if (target->kind == TYPE_ARRAY && source->kind == TYPE_STRING &&
        target->data.array.element_type->kind == TYPE_INT8) {
        return 1;
    }

    if (target->kind == TYPE_ARRAY && source->kind == TYPE_ARRAY) {

        Type* target_element = target->data.array.element_type;
        Type* source_element = source->data.array.element_type;
        return type_is_assignable(target_element, source_element);
    }

    return 0;
}

Type* type_create_from_token(KrtTokenType token_type) {
    
    TypeKind kind = token_to_type_kind(token_type);
    if (kind != TYPE_UNKNOWN) {
        return type_create_basic(kind);
    }
    
    if (token_type == TOKEN_TYPE_STRING) return type_create_basic(TYPE_STRING);
    if (token_type == TOKEN_UNKNOWN) return type_create_basic(TYPE_INT32);
    return type_create_basic(TYPE_UNKNOWN);
}

static Type* type_from_string(TypeCheckContext* context, const char* type_name) {
    if (!type_name) return type_create_basic(TYPE_UNKNOWN);

    if (strcmp(type_name, "int") == 0) return type_create_basic(TYPE_INT32);
    if (strcmp(type_name, "string") == 0) return type_create_basic(TYPE_STRING);
    if (strcmp(type_name, "bool") == 0) return type_create_basic(TYPE_BOOL);
    if (strcmp(type_name, "float") == 0) return type_create_basic(TYPE_FLOAT32);
    if (strcmp(type_name, "double") == 0) return type_create_basic(TYPE_FLOAT64);
    if (strcmp(type_name, "void") == 0) return type_create_basic(TYPE_VOID);

    TypeCheckSymbol* symbol = type_check_symbol_table_lookup(context->current_scope, type_name);
    if (symbol && symbol->type && symbol->type->kind == TYPE_CLASS) {
        return type_copy(symbol->type);
    }

    return NULL;
}

static int type_check_handle_function_decl(TypeCheckContext* context,
                                           const char* function_name,
                                           char** parameters,
                                           int parameter_count,
                                           KrtTokenType* parameter_types,
                                           int* parameter_is_array,
                                           ASTNode* body,
                                           KrtTokenType return_type_token) {

    TypeCheckSymbol* existing_symbol = type_check_symbol_table_lookup(context->current_scope, function_name);
    if (existing_symbol && existing_symbol->type && existing_symbol->type->kind == TYPE_FUNCTION) {

        if (!body) {
            return 1;
        }

        TypeCheckSymbolTable* old_scope = context->current_scope;
        TypeCheckSymbolTable* new_scope = type_check_symbol_table_create(old_scope);
        if (!new_scope) {
            return 0;
        }
        context->current_scope = new_scope;

        for (int i = 0; i < parameter_count; i++) {
            KrtTokenType token = parameter_types ? parameter_types[i] : TOKEN_UNKNOWN;
            Type* param_type = NULL;
            if (parameter_is_array && parameter_is_array[i]) {
                Type* element_type = type_create_from_token(token);
                if (!element_type) {
                    context->scope_ownership = 0;
                    context->current_scope = old_scope;
                    return 0;
                }
                param_type = type_create_array(element_type, 0);
                type_destroy(element_type);
            } else {
                param_type = type_create_from_token(token);
            }
            if (!param_type) {
                context->scope_ownership = 0;
                context->current_scope = old_scope;
                return 0;
            }

            TypeCheckSymbol param_symbol = {
                .name = KRT_STRDUP(parameters[i]),
                .type = param_type,
                .is_constant = 0,
                .line_number = 0,
                .owns_name = 1
            };

            if (!type_check_symbol_table_add(context->current_scope, param_symbol)) {
                type_destroy(param_type);
                KRT_FREE(param_symbol.name);
                context->scope_ownership = 0;
                context->current_scope = old_scope;
                return 0;
            }
        }

        if (existing_symbol->type->data.function.return_type->kind == TYPE_VOID || 
            existing_symbol->type->data.function.return_type->kind == TYPE_UNKNOWN) {
            Type* inferred_return_type = infer_function_return_type(context, body);
            if (inferred_return_type && inferred_return_type->kind != TYPE_VOID) {
                type_destroy(existing_symbol->type->data.function.return_type);
                existing_symbol->type->data.function.return_type = inferred_return_type;
            } else if (inferred_return_type) {
                type_destroy(inferred_return_type);
            }
        }

        if (!type_check_statement(context, body)) {
            context->scope_ownership = 0;
            context->current_scope = old_scope;
            return 0;
        }

        if (existing_symbol && existing_symbol->type && existing_symbol->type->kind == TYPE_FUNCTION) {
            existing_symbol->type->data.function.function_scope = new_scope;
        }

        context->scope_ownership = 0;
        context->current_scope = old_scope;
        return 1;
    }

    Type* func_type = type_create_function(NULL, NULL, 0);
    if (!func_type) {
        return 0;
    }

    for (int i = 0; i < parameter_count; i++) {
        KrtTokenType token = parameter_types ? parameter_types[i] : TOKEN_UNKNOWN;
        Type* param_type = NULL;
        if (parameter_is_array && parameter_is_array[i]) {
            Type* element_type = type_create_from_token(token);
            if (!element_type) {
                type_destroy(func_type);
                return 0;
            }
            param_type = type_create_array(element_type, 0);
            type_destroy(element_type);
        } else {
            param_type = type_create_from_token(token);
        }
        if (!param_type) {
            type_destroy(func_type);
            return 0;
        }
        type_function_add_parameter(func_type, param_type);
    }

    if (return_type_token != TOKEN_UNKNOWN) {
        Type* explicit_return_type = type_create_from_token(return_type_token);
        if (explicit_return_type) {
            func_type->data.function.return_type = explicit_return_type;
        } else {
            func_type->data.function.return_type = type_create_basic(TYPE_VOID);
        }
    } else {
        func_type->data.function.return_type = type_create_basic(TYPE_VOID);
    }

    TypeCheckSymbol symbol = {
        .name = KRT_STRDUP(function_name),
        .type = func_type,
        .is_constant = 1,
        .line_number = 0,
        .owns_name = 1
    };

    if (!type_check_symbol_table_add(context->current_scope, symbol)) {
        type_destroy(func_type);
        KRT_FREE(symbol.name);
        return 0;
    }

    if (!body) {
        return 1;
    }

    TypeCheckSymbolTable* old_scope = context->current_scope;
    TypeCheckSymbolTable* new_scope = type_check_symbol_table_create(old_scope);
    if (!new_scope) {
        context->current_scope = old_scope;
        return 0;
    }
    context->current_scope = new_scope;

    for (int i = 0; i < parameter_count; i++) {
        KrtTokenType token = parameter_types ? parameter_types[i] : TOKEN_UNKNOWN;
        Type* param_type = type_create_from_token(token);
        if (!param_type) {
            context->scope_ownership = 0;
            context->current_scope = old_scope;
            return 0;
        }

        TypeCheckSymbol param_symbol = {
            .name = KRT_STRDUP(parameters[i]),
            .type = param_type,
            .is_constant = 0,
            .line_number = 0,
            .owns_name = 1
        };

        if (!type_check_symbol_table_add(context->current_scope, param_symbol)) {
            type_destroy(param_type);
            KRT_FREE(param_symbol.name);
            context->scope_ownership = 0;
            context->current_scope = old_scope;
            return 0;
        }
    }

    if (return_type_token == TOKEN_UNKNOWN) {
        Type* inferred_return_type = infer_function_return_type(context, body);
        if (inferred_return_type && inferred_return_type->kind != TYPE_VOID) {
            type_destroy(func_type->data.function.return_type);
            func_type->data.function.return_type = inferred_return_type;
        } else if (inferred_return_type) {
            type_destroy(inferred_return_type);
        }
    }

    if (!type_check_statement(context, body)) {
        context->scope_ownership = 0;
        context->current_scope = old_scope;
        return 0;
    }

    context->scope_ownership = 0;
    context->current_scope = old_scope;

    return 1;
}

static int check_block(TypeCheckContext* context, ASTNode* statement) {
    TypeCheckSymbolTable* old_scope = context->current_scope;
    context->current_scope = type_check_symbol_table_create(old_scope);

    for (int i = 0; i < statement->data.block.statement_count; i++) {
        if (!type_check_statement(context, statement->data.block.statements[i])) {
            continue;
        }
    }

    type_check_symbol_table_destroy(context->current_scope);
    context->current_scope = old_scope;
    return 1;
}

static int check_print_statement(TypeCheckContext* context, ASTNode* statement) {
    for (int i = 0; i < statement->data.print_stmt.value_count; i++) {
        Type* value_type = type_check_expression(context, statement->data.print_stmt.values[i]);
        if (!value_type) {
            return 0;
        }
        type_destroy(value_type);
    }
    return 1;
}

static int check_function_declaration(TypeCheckContext* context, ASTNode* statement) {
    TypeCheckSymbol* existing_symbol = type_check_symbol_table_lookup(
        context->current_scope, statement->data.function_decl.name);
    if (existing_symbol && existing_symbol->type && existing_symbol->type->kind == TYPE_FUNCTION) {
        if (!type_check_handle_function_decl(context,
                                             statement->data.function_decl.name,
                                             statement->data.function_decl.parameters,
                                             statement->data.function_decl.parameter_count,
                                             statement->data.function_decl.parameter_types,
                                             statement->data.function_decl.parameter_is_array,
                                             statement->data.function_decl.body,
                                             statement->data.function_decl.return_type)) {
            return 0;
        }
    } else {
        if (!type_check_handle_function_decl(context,
                                             statement->data.function_decl.name,
                                             statement->data.function_decl.parameters,
                                             statement->data.function_decl.parameter_count,
                                             statement->data.function_decl.parameter_types,
                                             statement->data.function_decl.parameter_is_array,
                                             statement->data.function_decl.body,
                                             statement->data.function_decl.return_type)) {
            return 0;
        }
    }
    return 1;
}

static int check_static_function_declaration(TypeCheckContext* context, ASTNode* statement) {
    TypeCheckSymbol* existing_symbol = type_check_symbol_table_lookup(
        context->current_scope, statement->data.static_function_decl.name);
    if (existing_symbol && existing_symbol->type && existing_symbol->type->kind == TYPE_FUNCTION) {
        if (!type_check_handle_function_decl(context,
                                             statement->data.static_function_decl.name,
                                             statement->data.static_function_decl.parameters,
                                             statement->data.static_function_decl.parameter_count,
                                             statement->data.static_function_decl.parameter_types,
                                             statement->data.static_function_decl.parameter_is_array,
                                             statement->data.static_function_decl.body,
                                             statement->data.static_function_decl.return_type)) {
            return 0;
        }
    } else {
        if (!type_check_handle_function_decl(context,
                                             statement->data.static_function_decl.name,
                                             statement->data.static_function_decl.parameters,
                                             statement->data.static_function_decl.parameter_count,
                                             statement->data.static_function_decl.parameter_types,
                                             statement->data.static_function_decl.parameter_is_array,
                                             statement->data.static_function_decl.body,
                                             statement->data.static_function_decl.return_type)) {
            return 0;
        }
    }
    return 1;
}

static int check_try_statement(TypeCheckContext* context, ASTNode* statement) {
    if (!type_check_statement(context, statement->data.try_stmt.try_block)) {
        return 0;
    }

    for (int i = 0; i < statement->data.try_stmt.catch_clause_count; i++) {
        if (!type_check_statement(context, statement->data.try_stmt.catch_clauses[i])) {
            return 0;
        }
    }

    if (statement->data.try_stmt.finally_clause) {
        if (!type_check_statement(context, statement->data.try_stmt.finally_clause)) {
            return 0;
        }
    }
    return 1;
}

static int check_throw_statement(TypeCheckContext* context, ASTNode* statement) {
    if (statement->data.throw_stmt.exception_expr) {
        Type* value_type = type_check_expression(context, statement->data.throw_stmt.exception_expr);
        if (value_type) {
            type_destroy(value_type);
        }
    }
    return 1;
}

static int check_catch_clause(TypeCheckContext* context, ASTNode* statement) {
    TypeCheckSymbolTable* old_scope = context->current_scope;
    context->current_scope = type_check_symbol_table_create(old_scope);

    if (statement->data.catch_clause.exception_var) {
        Type* exception_type = NULL;

        if (statement->data.catch_clause.exception_type) {
            exception_type = type_from_string(context, statement->data.catch_clause.exception_type);
            if (!exception_type) {
                type_check_symbol_table_destroy(context->current_scope);
                context->current_scope = old_scope;
                return 0;
            }
        } else {
            exception_type = type_create_basic(TYPE_STRING);
        }

        TypeCheckSymbol exception_symbol = {
            .name = KRT_STRDUP(statement->data.catch_clause.exception_var),
            .type = exception_type,
            .is_constant = 0,
            .line_number = 0,
            .owns_name = 1
        };

        if (!type_check_symbol_table_add(context->current_scope, exception_symbol)) {
            type_destroy(exception_type);
            KRT_FREE(exception_symbol.name);
            type_check_symbol_table_destroy(context->current_scope);
            context->current_scope = old_scope;
            return 0;
        }
    }

    if (!type_check_statement(context, statement->data.catch_clause.catch_block)) {
        type_check_symbol_table_destroy(context->current_scope);
        context->current_scope = old_scope;
        return 0;
    }

    type_check_symbol_table_destroy(context->current_scope);
    context->current_scope = old_scope;
    return 1;
}

static int check_finally_clause(TypeCheckContext* context, ASTNode* statement) {
    if (!type_check_statement(context, statement->data.finally_clause.finally_block)) {
        return 0;
    }
    return 1;
}

static int check_namespace_declaration(TypeCheckContext* context, ASTNode* statement) {
    
    TypeCheckSymbol* existing_ns = type_check_symbol_table_lookup(context->current_scope, statement->data.namespace_decl.name);
    if (!existing_ns) {
        TypeCheckSymbol ns_symbol = {
            .name = KRT_STRDUP(statement->data.namespace_decl.name),
            .type = NULL,
            .is_constant = 1,
            .line_number = 0,
            .owns_name = 1
        };
        type_check_symbol_table_add(context->current_scope, ns_symbol);
    }

    TypeCheckSymbolTable* old_scope = context->current_scope;
    context->current_scope = type_check_symbol_table_create(old_scope);

    int body_ok = 1;
    if (statement->data.namespace_decl.body) {
        body_ok = type_check_statement(context, statement->data.namespace_decl.body);
    }

    type_check_symbol_table_destroy(context->current_scope);
    context->current_scope = old_scope;

    if (!body_ok) {
        return 0;
    }
    return 1;
}

static int check_class_declaration(TypeCheckContext* context, ASTNode* statement) {
    const char* class_name = statement->data.class_decl.name;

    TypeCheckSymbol* existing_symbol = type_check_symbol_table_lookup(context->current_scope, class_name);
    if (existing_symbol) {
        return 0;
    }

    ClassInfo* class_info = class_info_create(class_name);
    if (!class_info) {
        return 0;
    }

    if (statement->data.class_decl.base_class) {
        if (statement->data.class_decl.base_class->type == AST_IDENTIFIER) {
            class_info->base_class_name = KRT_STRDUP(statement->data.class_decl.base_class->data.identifier_name);
        }
    }

    Type* class_type = type_create_class(class_name);
    if (!class_type) {
        class_info_destroy(class_info);
        return 0;
    }

    class_type->data.class.class_info = class_info;

    TypeCheckSymbol class_symbol = {
        .name = KRT_STRDUP(class_name),
        .type = class_type,
        .is_constant = 1,
        .line_number = 0,
        .owns_name = 1
    };

    if (!type_check_symbol_table_add(context->current_scope, class_symbol)) {
        type_destroy(class_type);
        KRT_FREE(class_symbol.name);
        return 0;
    }

    char* old_class = context->current_class;
    ClassInfo* old_class_info = context->current_class_info;

    context->current_class = KRT_STRDUP(class_name);
    context->current_class_info = class_info;

    if (statement->data.class_decl.body) {
        AccessModifier default_access = ACCESS_PUBLIC;
        if (!type_check_class_body(context, class_info, statement->data.class_decl.body, default_access)) {
            KRT_FREE(context->current_class);
            context->current_class = old_class;
            context->current_class_info = old_class_info;
            return 0;
        }
    }

    KRT_FREE(context->current_class);
    context->current_class = old_class;
    context->current_class_info = old_class_info;
    return 1;
}

static int check_constructor_declaration(TypeCheckContext* context, ASTNode* statement) {
    if (!context->current_class_info) {
        return 0;
    }

    ClassMember constructor_member = {
        .name = KRT_STRDUP(context->current_class),
        .member_type = MEMBER_CONSTRUCTOR,
        .access = ACCESS_PUBLIC,
        .is_static = 0,
        .type = NULL
    };

    Type* constructor_type = type_create_function(type_create_basic(TYPE_VOID), NULL, 0);
    if (!constructor_type) {
        KRT_FREE(constructor_member.name);
        return 0;
    }

    for (int i = 0; i < statement->data.constructor_decl.parameter_count; i++) {
        KrtTokenType token = statement->data.constructor_decl.parameter_types[i];
        Type* param_type = type_create_from_token(token);
        type_function_add_parameter(constructor_type, param_type);
    }

    constructor_member.type = constructor_type;
    class_info_add_member(context->current_class_info, constructor_member);

    TypeCheckSymbolTable* old_scope = context->current_scope;
    context->current_scope = type_check_symbol_table_create(old_scope);

    for (int i = 0; i < statement->data.constructor_decl.parameter_count; i++) {
        Type* param_type = constructor_type->data.function.parameter_types[i];
        TypeCheckSymbol param_symbol = {
            .name = KRT_STRDUP(statement->data.constructor_decl.parameters[i]),
            .type = type_copy(param_type),
            .is_constant = 0,
            .line_number = 0,
            .owns_name = 1
        };
        type_check_symbol_table_add(context->current_scope, param_symbol);
    }

    if (statement->data.constructor_decl.body) {
        if (!type_check_statement(context, statement->data.constructor_decl.body)) {
            type_check_symbol_table_destroy(context->current_scope);
            context->current_scope = old_scope;
            return 0;
        }
    }

    type_check_symbol_table_destroy(context->current_scope);
    context->current_scope = old_scope;
    return 1;
}

static int check_destructor_declaration(TypeCheckContext* context, ASTNode* statement) {
    if (!context->current_class_info) {
        return 0;
    }

    char destructor_name[256];
    snprintf(destructor_name, sizeof(destructor_name), "~%s", context->current_class);

    ClassMember destructor_member = {
        .name = KRT_STRDUP(destructor_name),
        .member_type = MEMBER_DESTRUCTOR,
        .access = ACCESS_PUBLIC,
        .is_static = 0,
        .type = type_create_function(type_create_basic(TYPE_VOID), NULL, 0)
    };

    class_info_add_member(context->current_class_info, destructor_member);

    TypeCheckSymbolTable* old_scope = context->current_scope;
    context->current_scope = type_check_symbol_table_create(old_scope);

    if (statement->data.destructor_decl.body) {
        if (!type_check_statement(context, statement->data.destructor_decl.body)) {
            type_check_symbol_table_destroy(context->current_scope);
            context->current_scope = old_scope;
            return 0;
        }
    }

    type_check_symbol_table_destroy(context->current_scope);
    context->current_scope = old_scope;
    return 1;
}

static int check_access_modifier(TypeCheckContext* context, ASTNode* statement) {
    if (!context->current_class_info) {
        return 0;
    }

    AccessModifier access = token_to_access_modifier(statement->data.access_modifier.access_modifier);
    (void)access;

    ASTNode* member = statement->data.access_modifier.member;
    if (!member) {
        return 1;
    }

    return type_check_statement(context, member);
}

static int check_static_variable_declaration(TypeCheckContext* context, ASTNode* statement) {
    Type* initializer_type = NULL;
    if (statement->data.static_variable_decl.value) {
        initializer_type = type_check_expression(context, statement->data.static_variable_decl.value);
        if (!initializer_type) {
            return 0;
        }
    }

    KrtTokenType declared_token = statement->data.static_variable_decl.type;
    Type* declared_type = NULL;
    if (declared_token != TOKEN_UNKNOWN) {
        declared_type = type_create_from_token(declared_token);
        if (!declared_type) {
            if (initializer_type) {
                type_destroy(initializer_type);
            }
            return 0;
        }
    }

    Type* symbol_type = NULL;
    if (declared_type && initializer_type) {
        if (!type_is_assignable(declared_type, initializer_type)) {
            type_destroy(initializer_type);
            type_destroy(declared_type);
            return 0;
        }
        type_destroy(initializer_type);
        symbol_type = declared_type;
    } else if (declared_type) {
        symbol_type = declared_type;
        if (initializer_type) {
            type_destroy(initializer_type);
        }
    } else if (initializer_type) {
        symbol_type = type_copy(initializer_type);
        if (!symbol_type) {
            type_destroy(initializer_type);
            return 0;
        }
        type_destroy(initializer_type);
    } else {
        symbol_type = type_create_basic(TYPE_UNKNOWN);
        if (!symbol_type) {
            return 0;
        }
    }

    TypeCheckSymbol symbol = {
        .name = KRT_STRDUP(statement->data.static_variable_decl.name),
        .type = symbol_type,
        .is_constant = 0,
        .line_number = 0,
        .owns_name = 1
    };

    if (!type_check_symbol_table_add(context->current_scope, symbol)) {
        type_destroy(symbol_type);
        KRT_FREE(symbol.name);
        return 0;
    }
    return 1;
}

int type_check_program(TypeCheckContext* context, ASTNode* program) {
    if (!context || !program) {
        return 0;
    }

    context->error_count = 0;
    context->warning_count = 0;

    if (program->type != AST_PROGRAM) {
        return 0;
    }

    for (int i = 0; i < program->data.block.statement_count; i++) {
        ASTNode* statement = program->data.block.statements[i];
        if (!statement) continue;

        if (statement->type == AST_FUNCTION_DECLARATION) {
            if (!type_check_handle_function_decl(context,
                                                 statement->data.function_decl.name,
                                                 statement->data.function_decl.parameters,
                                                 statement->data.function_decl.parameter_count,
                                                 statement->data.function_decl.parameter_types,
                                                 statement->data.function_decl.parameter_is_array,
                                                 NULL,
                                                 statement->data.function_decl.return_type)) {
                continue;
            }
        } else if (statement->type == AST_STATIC_FUNCTION_DECLARATION) {
            if (!type_check_handle_function_decl(context,
                                                 statement->data.static_function_decl.name,
                                                 statement->data.static_function_decl.parameters,
                                                 statement->data.static_function_decl.parameter_count,
                                                 statement->data.static_function_decl.parameter_types,
                                                 statement->data.static_function_decl.parameter_is_array,
                                                 NULL,
                                                 statement->data.static_function_decl.return_type)) {
                continue;
            }
        } else if (statement->type == AST_NAMESPACE_DECLARATION) {
            
            ASTNode* body = statement->data.namespace_decl.body;
            if (body && body->type == AST_BLOCK) {
                for (int j = 0; j < body->data.block.statement_count; j++) {
                    ASTNode* ns_stmt = body->data.block.statements[j];
                    if (!ns_stmt) continue;
                    if (ns_stmt->type == AST_FUNCTION_DECLARATION) {
                        type_check_handle_function_decl(context,
                                                        ns_stmt->data.function_decl.name,
                                                        ns_stmt->data.function_decl.parameters,
                                                        ns_stmt->data.function_decl.parameter_count,
                                                        ns_stmt->data.function_decl.parameter_types,
                                                        ns_stmt->data.function_decl.parameter_is_array,
                                                        NULL,
                                                        ns_stmt->data.function_decl.return_type);
                    } else if (ns_stmt->type == AST_STATIC_FUNCTION_DECLARATION) {
                        type_check_handle_function_decl(context,
                                                        ns_stmt->data.static_function_decl.name,
                                                        ns_stmt->data.static_function_decl.parameters,
                                                        ns_stmt->data.static_function_decl.parameter_count,
                                                        ns_stmt->data.static_function_decl.parameter_types,
                                                        ns_stmt->data.static_function_decl.parameter_is_array,
                                                        NULL,
                                                        ns_stmt->data.static_function_decl.return_type);
                    }
                }
            }
        }
    }

    for (int i = 0; i < program->data.block.statement_count; i++) {
        if (!type_check_statement(context, program->data.block.statements[i])) {
            continue;
        }
    }

    return context->error_count == 0;
}

int type_check_statement(TypeCheckContext* context, ASTNode* statement) {
    if (!context || !statement) return 0;

    switch (statement->type) {
        case AST_VARIABLE_DECLARATION:
            return check_variable_declaration(context, statement);

        case AST_STATIC_VARIABLE_DECLARATION:
            return check_static_variable_declaration(context, statement);

        case AST_ASSIGNMENT:
        case AST_COMPOUND_ASSIGNMENT:
            return check_assignment(context, statement);

        case AST_ARRAY_ASSIGNMENT:
        case AST_ARRAY_COMPOUND_ASSIGNMENT:
            return check_array_assignment(context, statement);

        case AST_RETURN_STATEMENT:
            return check_return_statement(context, statement);

        case AST_IF_STATEMENT:
            return check_if_statement(context, statement);

        case AST_WHILE_STATEMENT:
            return check_while_statement(context, statement);

        case AST_FOR_STATEMENT:
            return check_for_statement(context, statement);

        case AST_FOREACH_STATEMENT:
            return check_foreach_statement(context, statement);

        case AST_BLOCK:
            return check_block(context, statement);

        case AST_PRINT_STATEMENT:
            return check_print_statement(context, statement);

        case AST_FUNCTION_DECLARATION:
            return check_function_declaration(context, statement);

        case AST_STATIC_FUNCTION_DECLARATION:
            return check_static_function_declaration(context, statement);

        case AST_TRY_STATEMENT:
            return check_try_statement(context, statement);

        case AST_THROW_STATEMENT:
            return check_throw_statement(context, statement);

        case AST_CATCH_CLAUSE:
            return check_catch_clause(context, statement);

        case AST_FINALLY_CLAUSE:
            return check_finally_clause(context, statement);

        case AST_NAMESPACE_DECLARATION:
            return check_namespace_declaration(context, statement);

        case AST_CLASS_DECLARATION:
            return check_class_declaration(context, statement);

        case AST_CONSTRUCTOR_DECLARATION:
            return check_constructor_declaration(context, statement);

        case AST_DESTRUCTOR_DECLARATION:
            return check_destructor_declaration(context, statement);

        case AST_ACCESS_MODIFIER:
            return check_access_modifier(context, statement);

        default:
            
            if (statement->type == AST_CALL || statement->type == AST_MEMBER_ACCESS ||
                statement->type == AST_ARRAY_ACCESS || statement->type == AST_NEW_EXPRESSION ||
                statement->type == AST_NEW_ARRAY_EXPRESSION || statement->type == AST_THIS ||
                statement->type == AST_STATIC_METHOD_CALL || statement->type == AST_LAMBDA_EXPRESSION ||
                statement->type == AST_LINQ_QUERY) {
                Type* expr_type = type_check_expression(context, statement);
                if (expr_type) {
                    type_destroy(expr_type);
                }
                return 1;
            }
            break;
    }
    return 1;
}

static Type* check_number_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_string_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_char_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_boolean_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_identifier(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_binary_operation(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_ternary_operation(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_unary_operation(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_array_literal(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_member_access(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_new_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_this_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_static_method_call(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_call_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_array_access(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_assignment_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_lambda_expression(TypeCheckContext* ctx, ASTNode* expr);
static Type* check_linq_query(TypeCheckContext* ctx, ASTNode* expr);

static Type* check_number_literal(TypeCheckContext* ctx, ASTNode* expr) {
    (void)ctx;
    (void)expr;
    return type_create_basic(TYPE_INT32);
}

static Type* check_string_literal(TypeCheckContext* ctx, ASTNode* expr) {
    (void)ctx;
    (void)expr;
    return type_create_basic(TYPE_STRING);
}

static Type* check_char_literal(TypeCheckContext* ctx, ASTNode* expr) {
    (void)ctx;
    (void)expr;
    return type_create_basic(TYPE_INT8);
}

static Type* check_boolean_literal(TypeCheckContext* ctx, ASTNode* expr) {
    (void)ctx;
    (void)expr;
    return type_create_basic(TYPE_BOOL);
}

static Type* check_identifier(TypeCheckContext* ctx, ASTNode* expr) {
    TypeCheckSymbol* symbol = type_check_symbol_table_lookup(ctx->current_scope, expr->data.identifier_name);
    if (!symbol) {
        if (ctx->semantic_analyzer) {
            SemanticAnalyzer* sem_analyzer = (SemanticAnalyzer*)ctx->semantic_analyzer;
            SymbolEntry* sem_symbol = symbol_table_lookup(sem_analyzer->symbol_table, expr->data.identifier_name);
            if (sem_symbol && sem_symbol->is_array) {

                Type* element_type = type_create_basic(TYPE_INT32);
                Type* array_type = type_create_array(element_type, 0);
                if (array_type) {
                    return array_type;
                }
            }

            if (sem_symbol && sem_symbol->type == SYMBOL_NAMESPACE) {
                return NULL;
            }

            Type* basic_type = type_create_basic(TYPE_INT32);
            if (basic_type) {
                return basic_type;
            }
        }

        return NULL;
    }

    Type* copied_type = type_copy(symbol->type);
    if (!copied_type) {
        return NULL;
    }
    return copied_type;
}

static Type* check_binary_operation(TypeCheckContext* ctx, ASTNode* expr) {
    Type* left_type = type_check_expression(ctx, expr->data.binary_op.left);
    Type* right_type = type_check_expression(ctx, expr->data.binary_op.right);

    if (!left_type || !right_type) {
        return NULL;
    }

    Type* result_type = NULL;
    switch (expr->data.binary_op.operator) {
        case TOKEN_PLUS:
            if (left_type->kind == TYPE_STRING && right_type->kind == TYPE_STRING) {
                result_type = type_create_basic(TYPE_STRING);
            } else if (left_type->kind == TYPE_STRING && (right_type->kind >= TYPE_INT8 && right_type->kind <= TYPE_FLOAT64)) {
                result_type = type_create_basic(TYPE_STRING);
            } else if ((left_type->kind >= TYPE_INT8 && left_type->kind <= TYPE_FLOAT64) && right_type->kind == TYPE_STRING) {
                result_type = type_create_basic(TYPE_STRING);
            } else if (!type_is_compatible(left_type, right_type)) {
                if (left_type->kind == TYPE_UNKNOWN || right_type->kind == TYPE_UNKNOWN) {
                    result_type = type_create_basic(TYPE_INT32);
                } else {
                    type_destroy(left_type);
                    type_destroy(right_type);
                    return NULL;
                }
            } else if ((left_type->kind >= TYPE_INT8 && left_type->kind <= TYPE_FLOAT64) &&
                       (right_type->kind >= TYPE_INT8 && right_type->kind <= TYPE_FLOAT64)) {
                if (left_type->kind == TYPE_FLOAT64 || right_type->kind == TYPE_FLOAT64) {
                    result_type = type_create_basic(TYPE_FLOAT64);
                } else if (left_type->kind == TYPE_FLOAT32 || right_type->kind == TYPE_FLOAT32) {
                    result_type = type_create_basic(TYPE_FLOAT32);
                } else {
                    result_type = type_create_basic(TYPE_INT32);
                }
            } else if (left_type->kind == TYPE_UNKNOWN || right_type->kind == TYPE_UNKNOWN) {
                result_type = type_create_basic(TYPE_INT32);
            }
            break;

        case TOKEN_MINUS:
        case TOKEN_MULTIPLY:
        case TOKEN_DIVIDE:
        case TOKEN_MODULO:
        case TOKEN_POWER:
            if ((left_type->kind >= TYPE_INT8 && left_type->kind <= TYPE_FLOAT64) &&
                (right_type->kind >= TYPE_INT8 && right_type->kind <= TYPE_FLOAT64)) {
                if (left_type->kind == TYPE_FLOAT64 || right_type->kind == TYPE_FLOAT64) {
                    result_type = type_create_basic(TYPE_FLOAT64);
                } else if (left_type->kind == TYPE_FLOAT32 || right_type->kind == TYPE_FLOAT32) {
                    result_type = type_create_basic(TYPE_FLOAT32);
                } else {
                    result_type = type_create_basic(TYPE_INT32);
                }
            }
            break;

        case TOKEN_AND:
        case TOKEN_OR:
            
            if (left_type->kind != TYPE_BOOL || right_type->kind != TYPE_BOOL) {
            }
            result_type = type_create_basic(TYPE_BOOL);
            break;

        case TOKEN_BITWISE_AND:
        case TOKEN_BITWISE_OR:
        case TOKEN_BITWISE_XOR:
        case TOKEN_LSHIFT:
        case TOKEN_RSHIFT:
            if ((left_type->kind >= TYPE_INT8 && left_type->kind <= TYPE_UINT64) &&
                (right_type->kind >= TYPE_INT8 && right_type->kind <= TYPE_UINT64)) {
                result_type = type_create_basic(TYPE_INT32);
            }
            break;

        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
            result_type = type_create_basic(TYPE_BOOL);
            break;

        case TOKEN_ASSIGN:
            if (!type_is_assignable(left_type, right_type)) {
                if (left_type->kind != TYPE_UNKNOWN && right_type->kind != TYPE_UNKNOWN) {
                    type_destroy(left_type);
                    type_destroy(right_type);
                    return NULL;
                }
            }
            result_type = type_copy(left_type);
            break;

        default:
            break;
    }

    type_destroy(left_type);
    type_destroy(right_type);
    return result_type;
}

static Type* check_ternary_operation(TypeCheckContext* ctx, ASTNode* expr) {
    if (!expr->data.ternary_op.condition || !expr->data.ternary_op.true_value || !expr->data.ternary_op.false_value) {
        return NULL;
    }

    Type* condition_type = type_check_expression(ctx, expr->data.ternary_op.condition);
    if (!condition_type) {
        return NULL;
    }

    if (condition_type->kind != TYPE_BOOL) {
        type_destroy(condition_type);
        return NULL;
    }
    type_destroy(condition_type);

    Type* true_type = type_check_expression(ctx, expr->data.ternary_op.true_value);
    if (!true_type) {
        return NULL;
    }

    Type* false_type = type_check_expression(ctx, expr->data.ternary_op.false_value);
    if (!false_type) {
        type_destroy(true_type);
        return NULL;
    }

    if (!type_is_compatible(true_type, false_type)) {
        type_destroy(true_type);
        type_destroy(false_type);
        return NULL;
    }

    type_destroy(false_type);
    return true_type;
}

static Type* check_unary_operation(TypeCheckContext* ctx, ASTNode* expr) {
    Type* operand_type = type_check_expression(ctx, expr->data.unary_op.operand);
    if (!operand_type) {
        return NULL;
    }

    Type* result_type = NULL;

    switch (expr->data.unary_op.operator) {
        case TOKEN_MINUS:
            if (operand_type->kind >= TYPE_INT8 && operand_type->kind <= TYPE_FLOAT64) {
                result_type = type_create_basic(operand_type->kind);
            }
            break;

        case TOKEN_NOT:
            if (operand_type->kind == TYPE_BOOL) {
                result_type = type_create_basic(TYPE_BOOL);
            }
            break;

        case TOKEN_INCREMENT:
        case TOKEN_DECREMENT:
            if (operand_type->kind >= TYPE_INT8 && operand_type->kind <= TYPE_FLOAT64) {
                result_type = type_create_basic(operand_type->kind);
            }
            break;

        case TOKEN_INT32:
        case TOKEN_INT64:
        case TOKEN_INT16:
        case TOKEN_INT8:
        case TOKEN_UINT32:
        case TOKEN_UINT64:
        case TOKEN_UINT16:
        case TOKEN_UINT8:
        case TOKEN_FLOAT32:
        case TOKEN_FLOAT64:
        case TOKEN_BOOL: {
            TypeKind target_kind = TYPE_UNKNOWN;
            switch (expr->data.unary_op.operator) {
                case TOKEN_INT32: target_kind = TYPE_INT32; break;
                case TOKEN_INT64: target_kind = TYPE_INT64; break;
                case TOKEN_INT16: target_kind = TYPE_INT16; break;
                case TOKEN_INT8: target_kind = TYPE_INT8; break;
                case TOKEN_UINT32: target_kind = TYPE_UINT32; break;
                case TOKEN_UINT64: target_kind = TYPE_UINT64; break;
                case TOKEN_UINT16: target_kind = TYPE_UINT16; break;
                case TOKEN_UINT8: target_kind = TYPE_UINT8; break;
                case TOKEN_FLOAT32: target_kind = TYPE_FLOAT32; break;
                case TOKEN_FLOAT64: target_kind = TYPE_FLOAT64; break;
                case TOKEN_BOOL: target_kind = TYPE_BOOL; break;
                default: break;
            }

            if (target_kind != TYPE_UNKNOWN) {
                if (operand_type->kind >= TYPE_INT8 && operand_type->kind <= TYPE_FLOAT64) {
                    result_type = type_create_basic(target_kind);
                } else {
                    Type* target_type = type_create_basic(target_kind);
                    type_destroy(target_type);
                }
            }
            break;
        }

        default:
            break;
    }

    type_destroy(operand_type);
    return result_type;
}

static Type* check_array_literal(TypeCheckContext* ctx, ASTNode* expr) {
    if (expr->data.array_literal.element_count == 0) {
        Type* unknown_type = type_create_basic(TYPE_UNKNOWN);
        Type* array_type = type_create_array(unknown_type, 0);
        return array_type;
    }

    Type* first_element_type = type_check_expression(ctx, expr->data.array_literal.elements[0]);
    if (!first_element_type) {
        return NULL;
    }

    for (int i = 1; i < expr->data.array_literal.element_count; i++) {
        Type* element_type = type_check_expression(ctx, expr->data.array_literal.elements[i]);
        if (!element_type) {
            type_destroy(first_element_type);
            return NULL;
        }

        if (!type_is_compatible(first_element_type, element_type)) {
            type_destroy(element_type);
            type_destroy(first_element_type);
            return NULL;
        }

        type_destroy(element_type);
    }

    Type* array_type = type_create_array(first_element_type, expr->data.array_literal.element_count);
    return array_type;
}

static Type* check_member_access(TypeCheckContext* ctx, ASTNode* expr) {
    Type* object_type = type_check_expression(ctx, expr->data.member_access.object);
    if (!object_type) {
        
        if (ctx->semantic_analyzer) {
            SemanticAnalyzer* sem_analyzer = (SemanticAnalyzer*)ctx->semantic_analyzer;
            
            char* parts[10];
            int part_count = 0;
            
            ASTNode* current = expr->data.member_access.object;
            while (current) {
                if (current->type == AST_MEMBER_ACCESS) {
                    if (part_count >= 9) break;
                    parts[part_count++] = current->data.member_access.member_name;
                    current = current->data.member_access.object;
                } else if (current->type == AST_IDENTIFIER) {
                    if (part_count >= 10) break;
                    parts[part_count++] = current->data.identifier_name;
                    break;
                } else {
                    break;
                }
            }
            
            for (int i = 0; i < part_count / 2; i++) {
                char* temp = parts[i];
                parts[i] = parts[part_count - 1 - i];
                parts[part_count - 1 - i] = temp;
            }
            
            if (part_count < 10) {
                parts[part_count++] = expr->data.member_access.member_name;
            }
            
            SymbolEntry* entry = semantic_analyzer_lookup_qualified_name(sem_analyzer, parts, part_count);
            if (entry) {
                
                if (entry->type == SYMBOL_FUNCTION) {
                    return type_create_basic(TYPE_INT32);  
                } else if (entry->type == SYMBOL_VARIABLE || entry->type == SYMBOL_FIELD) {
                    return type_create_basic(TYPE_INT32);  
                } else if (entry->type == SYMBOL_NAMESPACE) {
                    
                    return type_create_basic(TYPE_UNKNOWN);
                }
            }
        }

        return NULL;
    }

    const char* member_name = expr->data.member_access.member_name;
    Type* member_type = NULL;

    if (object_type->kind == TYPE_POINTER && object_type->data.pointer_to && object_type->data.pointer_to->kind == TYPE_CLASS) {
        object_type = object_type->data.pointer_to;
    }

    if (object_type->kind == TYPE_STRUCT || object_type->kind == TYPE_CLASS) {
        ClassInfo* class_info = object_type->data.class.class_info;

        if (class_info) {
            ClassMember* member = class_info_find_member(class_info, member_name);
            if (member) {
                if (member->access == ACCESS_PRIVATE) {
                    if (!ctx->current_class || strcmp(ctx->current_class, class_info->class_name) != 0) {
                    }
                } else if (member->access == ACCESS_PROTECTED) {
                    if (!ctx->current_class) {
                    }
                }

                int accessing_via_class = 0;
                if (expr->data.member_access.object->type == AST_IDENTIFIER) {
                    const char* obj_name = expr->data.member_access.object->data.identifier_name;
                    TypeCheckSymbol* obj_sym = type_check_symbol_table_lookup(ctx->current_scope, obj_name);
                    if (obj_sym && obj_sym->type && obj_sym->type->kind == TYPE_CLASS) {
                        accessing_via_class = 1;
                    }
                }
                if (accessing_via_class && !member->is_static) {
                }

                member_type = type_copy(member->type);
                if (!accessing_via_class) {
                    if (expr->data.member_access.resolved_class_name) {
                        KRT_FREE(expr->data.member_access.resolved_class_name);
                    }
                    expr->data.member_access.resolved_class_name = KRT_STRDUP(class_info->class_name);
                }
            } else {
                member_type = type_create_basic(TYPE_UNKNOWN);
            }
        } else {
            for (int i = 0; i < object_type->data.class.field_count; i++) {
                if (strcmp(object_type->data.class.field_names[i], member_name) == 0) {
                    member_type = type_copy(object_type->data.class.field_types[i]);
                    if (expr->data.member_access.resolved_class_name) {
                        KRT_FREE(expr->data.member_access.resolved_class_name);
                    }
                    expr->data.member_access.resolved_class_name = KRT_STRDUP(object_type->data.class.class_name);
                    break;
                }
            }

            if (!member_type) {
                member_type = type_create_basic(TYPE_UNKNOWN);
            }
        }
    } else if (object_type->kind == TYPE_ARRAY) {
        if (strcmp(member_name, "length") == 0) {
            member_type = type_create_basic(TYPE_INT32);
        } else {
            member_type = type_create_basic(TYPE_UNKNOWN);
        }
    } else {
        member_type = type_create_basic(TYPE_UNKNOWN);
    }

    type_destroy(object_type);
    return member_type;
}

static Type* check_new_expression(TypeCheckContext* ctx, ASTNode* expr) {
    const char* class_name = expr->data.new_expr.class_name;

    TypeCheckSymbol* class_symbol = type_check_symbol_table_lookup(ctx->current_scope, class_name);
    if (!class_symbol || class_symbol->type->kind != TYPE_CLASS) {
        return NULL;
    }

    Type* class_type = type_copy(class_symbol->type);
    if (!class_type) {
        return NULL;
    }

    if (expr->data.new_expr.arguments) {
        if (class_symbol->type->data.class.field_count > 0) {
            if (expr->data.new_expr.argument_count != class_symbol->type->data.class.field_count) {
                type_destroy(class_type);
                return NULL;
            }

            for (int i = 0; i < expr->data.new_expr.argument_count; i++) {
                Type* arg_type = type_check_expression(ctx, expr->data.new_expr.arguments[i]);
                if (!arg_type) {
                    type_destroy(class_type);
                    return NULL;
                }

                Type* expected_type = class_symbol->type->data.class.field_types[i];
                if (!type_is_compatible(expected_type, arg_type)) {
                }

                type_destroy(arg_type);
            }
        } else {
            if (expr->data.new_expr.argument_count > 0) {
            }
        }
    }

    return type_create_pointer(class_type);
}

static Type* check_this_expression(TypeCheckContext* ctx, ASTNode* expr) {
    (void)expr;

    if (!ctx->current_class) {
        return NULL;
    }

    if (ctx->current_function_is_static) {
        return NULL;
    }

    Type* this_type = type_create_class(ctx->current_class);
    return this_type;
}

static Type* check_static_method_call(TypeCheckContext* ctx, ASTNode* expr) {
    const char* class_name = expr->data.static_call.class_name;
    const char* method_name = expr->data.static_call.method_name;

    if (method_name && strcmp(method_name, "delete") == 0) {
        TypeCheckSymbol* class_symbol = type_check_symbol_table_lookup(ctx->current_scope, class_name);
        if (!class_symbol || class_symbol->type->kind != TYPE_CLASS) {
            return NULL;
        }
        ClassInfo* class_info = class_symbol->type->data.class.class_info;
        if (!class_info) {
            return type_create_basic(TYPE_VOID);
        }
        char dname[256];
        snprintf(dname, sizeof(dname), "~%s", class_name);
        ClassMember* dmember = class_info_find_member(class_info, dname);
        if (!dmember || dmember->member_type != MEMBER_DESTRUCTOR) {
            return NULL;
        }
        int actual_param_count = expr->data.static_call.argument_count;
        if (actual_param_count != 1) {
        }
        return type_create_basic(TYPE_VOID);
    }

    TypeCheckSymbol* class_symbol = type_check_symbol_table_lookup(ctx->current_scope, class_name);
    if (!class_symbol || class_symbol->type->kind != TYPE_CLASS) {
        return NULL;
        return NULL;
    }

    ClassInfo* class_info = class_symbol->type->data.class.class_info;
    if (!class_info) {
        return type_create_basic(TYPE_UNKNOWN);
    }

    ClassMember* member = class_info_find_member(class_info, method_name);
    if (!member) {
        return NULL;
    }

    if (member->member_type != MEMBER_METHOD || !member->is_static) {
        return NULL;
    }

    Type* func_type = member->type;
    if (!func_type || func_type->kind != TYPE_FUNCTION) {
        return NULL;
    }

    int expected_param_count = func_type->data.function.parameter_count;
    int actual_param_count = expr->data.static_call.argument_count;

    if (expected_param_count != actual_param_count) {
    }

    int param_to_check = actual_param_count < expected_param_count ? actual_param_count : expected_param_count;
    for (int i = 0; i < param_to_check; i++) {
        Type* arg_type = type_check_expression(ctx, expr->data.static_call.arguments[i]);
        if (!arg_type) {
            return NULL;
        }
        Type* expected_type = func_type->data.function.parameter_types[i];
        if (!type_is_compatible(expected_type, arg_type)) {
        }
        type_destroy(arg_type);
    }

    return type_copy(func_type->data.function.return_type);
}

static Type* check_call_expression(TypeCheckContext* ctx, ASTNode* expr) {
    if (expr->data.call.object && expr->data.call.object->type == AST_THIS) {
        if (!ctx->current_class_info) {
            return NULL;
        }
        ClassMember* member = class_info_find_member(ctx->current_class_info, expr->data.call.name);
        if (!member) {
            return NULL;
        }
        if (member->member_type != MEMBER_METHOD || member->is_static) {
            return NULL;
        }
        Type* func_type = member->type;
        int expected_param_count = func_type->data.function.parameter_count;
        int actual_param_count = expr->data.call.argument_count;
        if (expected_param_count != actual_param_count) {
        }
        int param_to_check = actual_param_count < expected_param_count ? actual_param_count : expected_param_count;
        for (int i = 0; i < param_to_check; i++) {
            Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
            if (!arg_type) {
                return NULL;
            }
            Type* expected_type = func_type->data.function.parameter_types[i];
            if (!type_is_compatible(expected_type, arg_type)) {
                
                bool is_console_write = false;
                if (ctx->current_class && 
                    strcmp(ctx->current_class, "Console") == 0 &&
                    (strcmp(expr->data.call.name, "Write") == 0 || 
                     strcmp(expr->data.call.name, "WriteLine") == 0) &&
                    arg_type->kind == TYPE_INT32) {
                    is_console_write = true;
                }
                
                if (!is_console_write) {
                }
            }
            type_destroy(arg_type);
        }
        return type_copy(func_type->data.function.return_type);
    }

    if (expr->data.call.object) {
        if (ctx->semantic_analyzer) {
            SemanticAnalyzer* sem_analyzer = (SemanticAnalyzer*)ctx->semantic_analyzer;
            
            char* parts[10];
            int part_count = 0;
            
            ASTNode* current = expr->data.call.object;
            while (current) {
                if (current->type == AST_MEMBER_ACCESS) {
                    if (part_count >= 9) break;
                    parts[part_count++] = current->data.member_access.member_name;
                    current = current->data.member_access.object;
                } else if (current->type == AST_IDENTIFIER) {
                    if (part_count >= 10) break;
                    parts[part_count++] = current->data.identifier_name;
                    break;
                } else {
                    break;
                }
            }
            
            for (int i = 0; i < part_count / 2; i++) {
                char* temp = parts[i];
                parts[i] = parts[part_count - 1 - i];
                parts[part_count - 1 - i] = temp;
            }
            
            if (part_count < 10) {
                parts[part_count++] = (char*)expr->data.call.name;
            }

            SymbolEntry* entry = semantic_analyzer_lookup_qualified_name(sem_analyzer, parts, part_count);

            if (entry && entry->type == SYMBOL_FUNCTION) {
                
                Type* return_type = type_create_basic(TYPE_INT32);
                
                for (int i = 0; i < expr->data.call.argument_count; i++) {
                    Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
                    if (arg_type) type_destroy(arg_type);
                }
                
                return return_type;
            }
        }
    }
    
    if (expr->data.call.object && expr->data.call.object->type == AST_IDENTIFIER) {
        const char* ns_name = expr->data.call.object->data.identifier_name;
        const char* func_name = expr->data.call.name;
        
        if (ctx->semantic_analyzer) {
            SemanticAnalyzer* sem_analyzer = (SemanticAnalyzer*)ctx->semantic_analyzer;
            
            char* parts[2] = {(char*)ns_name, (char*)func_name};
            SymbolEntry* entry = semantic_analyzer_lookup_qualified_name(sem_analyzer, parts, 2);
            
            if (entry && entry->type == SYMBOL_FUNCTION) {
                
                Type* return_type = type_create_basic(TYPE_INT32);
                int param_count = 0;
                
                if (entry->ir_function) {
                    param_count = entry->ir_function->param_count;
                    
                }
                
                if (param_count != expr->data.call.argument_count) {
                }
                
                for (int i = 0; i < expr->data.call.argument_count; i++) {
                    Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
                    if (arg_type) type_destroy(arg_type);
                }
                
                return return_type;
            }
            
            char* ns_parts[1] = {(char*)ns_name};
            SymbolEntry* ns_entry = semantic_analyzer_lookup_qualified_name(sem_analyzer, ns_parts, 1);
            if (ns_entry && ns_entry->type == SYMBOL_NAMESPACE && ns_entry->nested_table) {
                SymbolEntry* func_entry = symbol_table_lookup(ns_entry->nested_table, func_name);
                if (func_entry && func_entry->type == SYMBOL_FUNCTION) {
                    
                    Type* return_type = type_create_basic(TYPE_INT32);
                    
                    for (int i = 0; i < expr->data.call.argument_count; i++) {
                        Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
                        if (arg_type) type_destroy(arg_type);
                    }
                    
                    return return_type;
                }
            }
        }
    }
    
    if (expr->data.call.object) {
        Type* obj_type = type_check_expression(ctx, expr->data.call.object);
        int is_class_constant = 0;
        if (expr->data.call.object->type == AST_IDENTIFIER) {
            TypeCheckSymbol* sym = type_check_symbol_table_lookup(ctx->current_scope, expr->data.call.object->data.identifier_name);
            if (sym && sym->type && sym->type->kind == TYPE_CLASS && sym->is_constant) {
                is_class_constant = 1;
            }
        }
        const char* class_name_inst = NULL;
        if (obj_type && obj_type->kind == TYPE_POINTER && obj_type->data.pointer_to && obj_type->data.pointer_to->kind == TYPE_CLASS) {
            class_name_inst = obj_type->data.pointer_to->data.class.class_name;
        } else if (obj_type && obj_type->kind == TYPE_CLASS && !is_class_constant) {
            class_name_inst = obj_type->data.class.class_name;
        }
        if (class_name_inst) {
            TypeCheckSymbol* class_symbol = type_check_symbol_table_lookup(ctx->current_scope, class_name_inst);
            if (!class_symbol || class_symbol->type->kind != TYPE_CLASS) {
                if (obj_type) type_destroy(obj_type);
                return NULL;
            }
            ClassInfo* class_info = class_symbol->type->data.class.class_info;
            if (!class_info) {
                if (obj_type) type_destroy(obj_type);
                return type_create_basic(TYPE_UNKNOWN);
            }
            ClassMember* member = class_info_find_member(class_info, expr->data.call.name);
            if (!member) {
                if (obj_type) type_destroy(obj_type);
                return NULL;
            }
            
            if (class_info->member_scope && 
                strcmp(class_name_inst, "Console") == 0 &&
                (strcmp(expr->data.call.name, "Write") == 0 || 
                 strcmp(expr->data.call.name, "WriteLine") == 0) &&
                expr->data.call.argument_count == 1) {
                
                Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[0]);
                if (arg_type && arg_type->kind != TYPE_STRING) {
                    if (obj_type) type_destroy(obj_type);
                    if (arg_type) type_destroy(arg_type);
                    return NULL;
                }
                if (arg_type) type_destroy(arg_type);
            }
            
            if (member->member_type != MEMBER_METHOD || member->is_static) {
                if (obj_type) type_destroy(obj_type);
                return NULL;
            }
            if (expr->data.call.resolved_class_name) {
                KRT_FREE(expr->data.call.resolved_class_name);
            }
            expr->data.call.resolved_class_name = KRT_STRDUP(class_name_inst);
            Type* func_type = member->type;
            int expected_param_count = func_type->data.function.parameter_count;
            int actual_param_count = expr->data.call.argument_count;
            int param_to_check = actual_param_count < expected_param_count ? actual_param_count : expected_param_count;
            for (int i = 0; i < param_to_check; i++) {
                Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
                if (!arg_type) {
                    if (obj_type) type_destroy(obj_type);
                    return NULL;
                }
                Type* expected_type = func_type->data.function.parameter_types[i];
                if (!type_is_compatible(expected_type, arg_type)) {
                    
                }
                type_destroy(arg_type);
            }
            if (obj_type) type_destroy(obj_type);
            return type_copy(func_type->data.function.return_type);
        }
        if (obj_type) type_destroy(obj_type);
    }
    
    if (expr->data.call.object && expr->data.call.object->type == AST_IDENTIFIER) {
        const char* obj_name = expr->data.call.object->data.identifier_name;
        TypeCheckSymbol* obj_symbol = type_check_symbol_table_lookup(ctx->current_scope, obj_name);
        if (obj_symbol && obj_symbol->type && obj_symbol->type->kind == TYPE_CLASS) {
            ClassInfo* class_info = obj_symbol->type->data.class.class_info;
            if (!class_info) {
                return type_create_basic(TYPE_UNKNOWN);
            }
            ClassMember* member = class_info_find_member(class_info, expr->data.call.name);
            if (!member) {
                return NULL;
            }
            
            if (member->is_static) {
                
                if (expr->data.call.resolved_class_name) {
                    KRT_FREE(expr->data.call.resolved_class_name);
                }
                expr->data.call.resolved_class_name = KRT_STRDUP(class_info->class_name);
                Type* func_type = member->type;
                int expected_param_count = func_type->data.function.parameter_count;
                int actual_param_count = expr->data.call.argument_count;
                if (expected_param_count != actual_param_count) {
                }
                int param_to_check = actual_param_count < expected_param_count ? actual_param_count : expected_param_count;
                for (int i = 0; i < param_to_check; i++) {
                    Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
                    if (!arg_type) {
                        return NULL;
                    }
                    Type* expected_type = func_type->data.function.parameter_types[i];
                    if (!type_is_compatible(expected_type, arg_type)) {
                        
                        bool is_console_write = false;
                        if (strcmp(class_info->class_name, "Console") == 0 &&
                            (strcmp(expr->data.call.name, "Write") == 0 || 
                             strcmp(expr->data.call.name, "WriteLine") == 0) &&
                            arg_type->kind == TYPE_INT32) {
                            is_console_write = true;
                        }
                        
                        if (!is_console_write) {
                        }
                    }
                    type_destroy(arg_type);
                }
                return type_copy(func_type->data.function.return_type);
            }
            
            if (expr->data.call.resolved_class_name) {
                KRT_FREE(expr->data.call.resolved_class_name);
            }
            expr->data.call.resolved_class_name = KRT_STRDUP(class_info->class_name);
            Type* func_type = member->type;
            int expected_param_count = func_type->data.function.parameter_count;
            int actual_param_count = expr->data.call.argument_count;
            if (expected_param_count != actual_param_count) {
            }
            int param_to_check = actual_param_count < expected_param_count ? actual_param_count : expected_param_count;
            for (int i = 0; i < param_to_check; i++) {
                Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
                if (!arg_type) {
                    return NULL;
                }
                Type* expected_type = func_type->data.function.parameter_types[i];
                if (!type_is_compatible(expected_type, arg_type)) {
                }
                type_destroy(arg_type);
            }
            return type_copy(func_type->data.function.return_type);
        }
        
        const char* class_name = obj_name;
        if (strcmp(expr->data.call.name, "delete") == 0) {
            TypeCheckSymbol* class_symbol = type_check_symbol_table_lookup(ctx->current_scope, class_name);
            if (!class_symbol || class_symbol->type->kind != TYPE_CLASS) {
                return NULL;
            }
            ClassInfo* class_info = class_symbol->type->data.class.class_info;
            if (!class_info) {
                return type_create_basic(TYPE_VOID);
            }
            char dname[256];
            snprintf(dname, sizeof(dname), "~%s", class_name);
            ClassMember* dmember = class_info_find_member(class_info, dname);
            if (!dmember || dmember->member_type != MEMBER_DESTRUCTOR) {
                return NULL;
            }
            int actual_param_count = expr->data.call.argument_count;
            if (actual_param_count != 1) {
            }
            return type_create_basic(TYPE_VOID);
        }
        
        TypeCheckSymbol* class_symbol = type_check_symbol_table_lookup(ctx->current_scope, class_name);
        if (!class_symbol || class_symbol->type->kind != TYPE_CLASS) {
            return NULL;
        }
        ClassInfo* class_info = class_symbol->type->data.class.class_info;
        if (!class_info) {
            return type_create_basic(TYPE_UNKNOWN);
        }
        ClassMember* member = class_info_find_member(class_info, expr->data.call.name);
        if (!member) {
            return NULL;
        }
        if (member->member_type != MEMBER_METHOD || member->is_static) {
            return NULL;
        }
        Type* func_type = member->type;
        int expected_param_count = func_type->data.function.parameter_count;
        int actual_param_count = expr->data.call.argument_count;
        if (expected_param_count != actual_param_count) {
        }
        int param_to_check = actual_param_count < expected_param_count ? actual_param_count : expected_param_count;
        for (int i = 0; i < param_to_check; i++) {
            Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
            if (!arg_type) {
                return NULL;
            }
            Type* expected_type = func_type->data.function.parameter_types[i];
            if (!type_is_compatible(expected_type, arg_type)) {
            }
            type_destroy(arg_type);
        }
        return type_copy(func_type->data.function.return_type);
    }

    TypeCheckSymbol* func_symbol = type_check_symbol_table_lookup(ctx->current_scope, expr->data.call.name);
    if (!func_symbol) {
        return NULL;
    }
    
    if (func_symbol->type->kind != TYPE_FUNCTION &&
        func_symbol->type->kind != TYPE_POINTER &&
        func_symbol->type->kind != TYPE_ARRAY) {
        return NULL;
    }

    if (func_symbol->type->kind == TYPE_POINTER ||
        func_symbol->type->kind == TYPE_ARRAY) {

        for (int i = 0; i < expr->data.call.argument_count; i++) {
            Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
            if (arg_type) type_destroy(arg_type);
        }
        return type_create_basic(TYPE_INT32);
    }

    int expected_param_count = func_symbol->type->data.function.parameter_count;
    int actual_param_count = expr->data.call.argument_count;
    if (expected_param_count != actual_param_count) {
        return func_symbol->type->data.function.return_type;
    }
    
    for (int i = 0; i < actual_param_count; i++) {
        Type* arg_type = type_check_expression(ctx, expr->data.call.arguments[i]);
        Type* expected_type = func_symbol->type->data.function.parameter_types[i];
        if (!arg_type) {
            continue;
        }
        if (!type_is_compatible(expected_type, arg_type)) {
        }
        type_destroy(arg_type);
    }

    Type* return_type_copy = type_copy(func_symbol->type->data.function.return_type);
    if (!return_type_copy) {
        return NULL;
    }
    return return_type_copy;
}

static Type* check_array_access(TypeCheckContext* ctx, ASTNode* expr) {
    Type* array_type = type_check_expression(ctx, expr->data.array_access.array);
    if (!array_type) {
        return NULL;
    }

    if (array_type->kind != TYPE_ARRAY) {
        type_destroy(array_type);
        return NULL;
    }

    Type* index_type = type_check_expression(ctx, expr->data.array_access.index);
    if (!index_type) {
        type_destroy(array_type);
        return NULL;
    }

    if (index_type->kind < TYPE_INT8 || index_type->kind > TYPE_UINT64) {
        type_destroy(array_type);
        type_destroy(index_type);
        return NULL;
    }

    if (!array_type->data.array.element_type) {
        type_destroy(array_type);
        type_destroy(index_type);
        return NULL;
    }

    Type* element_type = type_copy(array_type->data.array.element_type);
    type_destroy(array_type);
    type_destroy(index_type);

    if (!element_type) {
        return NULL;
    }

    return element_type;
}

static Type* check_assignment_expression(TypeCheckContext* ctx, ASTNode* expr) {
    const char* var_name = (expr->type == AST_ASSIGNMENT) ? 
                           expr->data.assignment.name : 
                           expr->data.compound_assignment.name;
    ASTNode* value_node = (expr->type == AST_ASSIGNMENT) ? 
                           expr->data.assignment.value : 
                           expr->data.compound_assignment.value;

    TypeCheckSymbol* left_symbol = type_check_symbol_table_lookup(ctx->current_scope, var_name);
    if (!left_symbol) {
        return NULL;
    }

    if (value_node) {
        Type* value_type = type_check_expression(ctx, value_node);
        if (!value_type) {
            return NULL;
        }

        if (!type_is_assignable(left_symbol->type, value_type)) {
            type_destroy(value_type);
            return NULL;
        }

        Type* result_type = type_copy(left_symbol->type);
        type_destroy(value_type);
        return result_type;
    }

    return type_copy(left_symbol->type);
}

static Type* check_lambda_expression(TypeCheckContext* ctx, ASTNode* expr) {
    (void)ctx;
    
    Type* return_type = type_create_basic(TYPE_INT32);
    if (!return_type) {
        return NULL;
    }
    
    int param_count = expr->data.lambda_expr.parameter_count;
    
    Type** param_types = NULL;
    if (param_count > 0) {
        param_types = (Type**)KRT_MALLOC(param_count * sizeof(Type*));
        if (!param_types) {
            type_destroy(return_type);
            return NULL;
        }
        for (int i = 0; i < param_count; i++) {
            param_types[i] = type_create_basic(TYPE_INT32);
        }
    }
    
    Type* func_type = type_create_function(return_type, param_types, param_count);
    
    if (param_types) {
        for (int i = 0; i < param_count; i++) {
            if (param_types[i]) type_destroy(param_types[i]);
        }
        KRT_FREE(param_types);
    }
    if (return_type) type_destroy(return_type);
    
    return func_type;
}

static Type* check_linq_query(TypeCheckContext* ctx, ASTNode* expr) {
    (void)ctx;
    (void)expr;
    
    Type* element_type = type_create_basic(TYPE_UNKNOWN);
    Type* result_type = type_create_array(element_type, 0);
    type_destroy(element_type);
    return result_type;
}

Type* type_check_expression(TypeCheckContext* context, ASTNode* expression) {
    if (!context || !expression) return NULL;

    switch (expression->type) {
        case AST_NUMBER:
            return check_number_literal(context, expression);

        case AST_STRING:
            return check_string_literal(context, expression);

        case AST_CHAR_LITERAL:
            return check_char_literal(context, expression);

        case AST_BOOLEAN:
            return check_boolean_literal(context, expression);

        case AST_IDENTIFIER:
            return check_identifier(context, expression);

        case AST_BINARY_OPERATION:
            return check_binary_operation(context, expression);

        case AST_TERNARY_OPERATION:
            return check_ternary_operation(context, expression);

        case AST_UNARY_OPERATION:
            return check_unary_operation(context, expression);

        case AST_ARRAY_LITERAL:
            return check_array_literal(context, expression);

        case AST_MEMBER_ACCESS:
            return check_member_access(context, expression);

        case AST_NEW_EXPRESSION:
            return check_new_expression(context, expression);

        case AST_THIS:
            return check_this_expression(context, expression);

        case AST_STATIC_METHOD_CALL:
            return check_static_method_call(context, expression);

        case AST_CALL:
            return check_call_expression(context, expression);
        
        case AST_ARRAY_ACCESS:
            return check_array_access(context, expression);

        case AST_ASSIGNMENT:
        case AST_COMPOUND_ASSIGNMENT:
            return check_assignment_expression(context, expression);

        case AST_LAMBDA_EXPRESSION:
            return check_lambda_expression(context, expression);

        case AST_LINQ_QUERY:
            return check_linq_query(context, expression);

        case AST_CAST_EXPRESSION: {
            Type* inner_type = type_check_expression(context, expression->data.cast_expr.expression);
            if (!inner_type) {
                return NULL;
            }
            Type* target_type = type_create_from_token(expression->data.cast_expr.target_type);
            if (!target_type) {
                type_destroy(inner_type);
                return NULL;
            }
            type_destroy(inner_type);
            return target_type;
        }

        default:
            return NULL;
    }
}

void type_check_init_builtin_types(TypeCheckContext* context) {
    if (!context) return;

    for (int i = 0; BUILTIN_TYPES[i].name; i++) {
        TypeCheckSymbol symbol = {
            (char*)BUILTIN_TYPES[i].name,
            type_create_basic(BUILTIN_TYPES[i].kind),
            1, 0, 0
        };
        type_check_symbol_table_add(context->current_scope, symbol);
    }
}

void type_check_init_builtin_functions(TypeCheckContext* context) {
    if (!context) return;

    for (int i = 0; BUILTIN_FUNCS[i].name; i++) {
        const char* name = BUILTIN_FUNCS[i].name;
        TypeKind ret_kind = BUILTIN_FUNCS[i].ret_kind;
        int param_count = BUILTIN_FUNCS[i].param_count;

        Type** params = NULL;
        if (param_count > 0) {
            params = (Type**)KRT_MALLOC(sizeof(Type*) * param_count);
            for (int j = 0; j < param_count; j++) {
                params[j] = type_create_basic(BUILTIN_FUNCS[i].param_kinds[j]);
            }
        }

        Type* ret_type = type_create_basic(ret_kind);
        Type* func_type = type_create_function(ret_type, params, param_count);

        TypeCheckSymbol symbol = {(char*)name, func_type, 1, 0, 0};
        type_check_symbol_table_add(context->current_scope, symbol);

        type_destroy(ret_type);
        if (params) {
            for (int j = 0; j < param_count; j++) {
                type_destroy(params[j]);
            }
            KRT_FREE(params);
        }
    }

    Type* void_type = type_create_basic(TYPE_VOID);
    Type* int32_type = type_create_basic(TYPE_INT32);
    Type* pointer_to_void = type_create_pointer(type_copy(void_type));

    Type** malloc_params = (Type**)KRT_MALLOC(sizeof(Type*) * 1);
    malloc_params[0] = type_copy(int32_type);
    Type* malloc_type = type_create_function(type_copy(pointer_to_void), malloc_params, 1);
    TypeCheckSymbol malloc_symbol = {"KrtMalloc", malloc_type, 1, 0, 0};
    type_check_symbol_table_add(context->current_scope, malloc_symbol);
    KRT_FREE(malloc_params);

    Type** free_params = (Type**)KRT_MALLOC(sizeof(Type*) * 1);
    free_params[0] = type_copy(pointer_to_void);
    Type* free_type = type_create_function(type_copy(void_type), free_params, 1);
    TypeCheckSymbol free_symbol = {"KRT_FREE", free_type, 1, 0, 0};
    type_check_symbol_table_add(context->current_scope, free_symbol);
    KRT_FREE(free_params);

    type_destroy(void_type);
    type_destroy(int32_type);
    type_destroy(pointer_to_void);
}

Type* infer_function_return_type(TypeCheckContext* context, ASTNode* function_body) {
    if (!function_body) {
        return type_create_basic(TYPE_VOID);
    }

    return infer_return_type_from_statement(context, function_body);
}

Type* infer_return_type_from_statement(TypeCheckContext* context, ASTNode* statement) {
    if (!statement) {
        return NULL;
    }

    switch (statement->type) {
        case AST_RETURN_STATEMENT:

            if (statement->data.return_stmt.value) {

                TypeCheckContext silent_context = *context;
                silent_context.error_count = 0;
                silent_context.suppress_errors = 1;

                Type* return_type = type_check_expression(&silent_context, statement->data.return_stmt.value);

                if (return_type) {
                    return return_type;
                } else {

                    Type* direct_type = type_check_expression(context, statement->data.return_stmt.value);
                    if (direct_type) {
                        return direct_type;
                    }
                }
            } else {
                return type_create_basic(TYPE_VOID);
            }
            return NULL;

        case AST_IF_STATEMENT:
            {

                Type* then_type = infer_return_type_from_statement(context, statement->data.if_stmt.then_branch);
                Type* else_type = NULL;

                if (statement->data.if_stmt.else_branch) {
                    else_type = infer_return_type_from_statement(context, statement->data.if_stmt.else_branch);
                }

                if (then_type && else_type) {

                    type_destroy(else_type);
                    return then_type;
                } else if (then_type) {
                    return then_type;
                } else if (else_type) {
                    return else_type;
                }
                return NULL;
            }

        case AST_WHILE_STATEMENT:
            return infer_return_type_from_statement(context, statement->data.while_stmt.body);
        case AST_FOR_STATEMENT:
            return infer_return_type_from_statement(context, statement->data.for_stmt.body);
        case AST_FOREACH_STATEMENT:
            return infer_return_type_from_statement(context, statement->data.foreach_stmt.body);

        case AST_VARIABLE_DECLARATION:
        case AST_STATIC_VARIABLE_DECLARATION:
        case AST_ASSIGNMENT:
        case AST_COMPOUND_ASSIGNMENT:
        case AST_ARRAY_ASSIGNMENT:
        case AST_ARRAY_COMPOUND_ASSIGNMENT:
        case AST_PRINT_STATEMENT:
        case AST_BREAK_STATEMENT:
        case AST_CONTINUE_STATEMENT:
            return NULL;

        case AST_TERNARY_OPERATION:
            {
                
                TypeCheckContext silent_context = *context;
                silent_context.error_count = 0;
                silent_context.suppress_errors = 1;
                
                Type* true_type = type_check_expression(&silent_context, statement->data.ternary_op.true_value);
                Type* false_type = type_check_expression(&silent_context, statement->data.ternary_op.false_value);
                
                if (true_type) {
                    if (false_type) {
                        type_destroy(false_type);
                    }
                    return true_type;
                } else if (false_type) {
                    return false_type;
                }
                return NULL;
            }

        case AST_BLOCK:

            for (int i = 0; i < statement->data.block.statement_count; i++) {
                Type* stmt_type = infer_return_type_from_statement(context, statement->data.block.statements[i]);
                if (stmt_type) {
                    return stmt_type;
                }
            }
            return NULL;

        default:

            {
                TypeCheckContext silent_context = *context;
                silent_context.error_count = 0;
                silent_context.suppress_errors = 1;

                Type* expr_type = type_check_expression(&silent_context, statement);
                if (expr_type) {
                    return expr_type;
                }
            }
            return NULL;
    }
}

static int type_check_class_body(TypeCheckContext* context, ClassInfo* class_info,
                                 ASTNode* body, AccessModifier default_access) {
    if (!body || body->type != AST_BLOCK) {
        return 1;
    }

    for (int i = 0; i < body->data.block.statement_count; i++) {
        ASTNode* statement = body->data.block.statements[i];
        AccessModifier member_access = default_access;
        int is_static = 0;

        if (statement->type == AST_ACCESS_MODIFIER) {
            member_access = token_to_access_modifier(statement->data.access_modifier.access_modifier);
            statement = statement->data.access_modifier.member;
            if (!statement) continue;
        }

        if (statement->type == AST_STATIC_VARIABLE_DECLARATION ||
            statement->type == AST_STATIC_FUNCTION_DECLARATION) {
            is_static = 1;
        }

        switch (statement->type) {
            case AST_VARIABLE_DECLARATION:
            case AST_STATIC_VARIABLE_DECLARATION:

                if (!type_check_add_class_member(context, class_info, statement, member_access, is_static)) {
                    return 0;
                }
                break;

            case AST_FUNCTION_DECLARATION:
            case AST_STATIC_FUNCTION_DECLARATION:

                if (!type_check_add_class_member(context, class_info, statement, member_access, is_static)) {
                    return 0;
                }
                break;

            case AST_CONSTRUCTOR_DECLARATION:
            case AST_DESTRUCTOR_DECLARATION:

                if (!type_check_statement(context, statement)) {
                    return 0;
                }
                break;

            case AST_PROPERTY_DECLARATION:
                
                if (!type_check_add_class_member(context, class_info, statement, member_access, is_static)) {
                    return 0;
                }
                break;

            default:
                return 0;
        }
    }

    return 1;
}

static int type_check_add_class_member(TypeCheckContext* context, ClassInfo* class_info,
                                       ASTNode* member, AccessModifier access, int is_static) {
    if (!class_info || !member) return 0;

    switch (member->type) {
        case AST_VARIABLE_DECLARATION:
        case AST_STATIC_VARIABLE_DECLARATION: {

            const char* field_name = member->type == AST_VARIABLE_DECLARATION
                ? member->data.variable_decl.name
                : member->data.static_variable_decl.name;

            ClassMember* existing = class_info_find_member(class_info, field_name);
            if (existing) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Member '%s' already declared in class '%s'",
                         field_name, class_info->class_name);
                return 0;
            }

            Type* field_type = NULL;
            if (member->type == AST_VARIABLE_DECLARATION) {
                KrtTokenType type_token = member->data.variable_decl.type;
                switch (type_token) {
                    case TOKEN_INT8: field_type = type_create_basic(TYPE_INT8); break;
                    case TOKEN_INT16: field_type = type_create_basic(TYPE_INT16); break;
                    case TOKEN_INT32: field_type = type_create_basic(TYPE_INT32); break;
                    case TOKEN_INT64: field_type = type_create_basic(TYPE_INT64); break;
                    case TOKEN_UINT8: field_type = type_create_basic(TYPE_UINT8); break;
                    case TOKEN_UINT16: field_type = type_create_basic(TYPE_UINT16); break;
                    case TOKEN_UINT32: field_type = type_create_basic(TYPE_UINT32); break;
                    case TOKEN_UINT64: field_type = type_create_basic(TYPE_UINT64); break;
                    case TOKEN_FLOAT32: field_type = type_create_basic(TYPE_FLOAT32); break;
                    case TOKEN_FLOAT64: field_type = type_create_basic(TYPE_FLOAT64); break;
                    case TOKEN_BOOL: field_type = type_create_basic(TYPE_BOOL); break;
                    case TOKEN_STRING: field_type = type_create_basic(TYPE_STRING); break;
                    default: field_type = type_create_basic(TYPE_UNKNOWN); break;
                }
            } else {

                if (member->data.static_variable_decl.value) {
                    field_type = type_check_expression(context, member->data.static_variable_decl.value);
                } else {
                    field_type = type_create_basic(TYPE_UNKNOWN);
                }
            }

            if (!field_type) {
                return 0;
            }

            ClassMember class_member = {
                .name = KRT_STRDUP(field_name),
                .member_type = MEMBER_FIELD,
                .access = access,
                .is_static = is_static,
                .type = field_type
            };

            class_info_add_member(class_info, class_member);

            if (class_info->member_scope) {
                TypeCheckSymbol field_sym = {
                    .name = KRT_STRDUP(field_name),
                    .type = type_copy(field_type),
                    .is_constant = 0,
                    .line_number = 0
                };
                type_check_symbol_table_add(class_info->member_scope, field_sym);
            }
            break;
        }

        case AST_FUNCTION_DECLARATION:
        case AST_STATIC_FUNCTION_DECLARATION: {

            const char* method_name = member->type == AST_FUNCTION_DECLARATION
                ? member->data.function_decl.name
                : member->data.static_function_decl.name;

            ClassMember* existing = class_info_find_member(class_info, method_name);
            if (existing) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Member '%s' already declared in class '%s'",
                         method_name, class_info->class_name);
                return 0;
            }

            Type* method_type = type_create_function(NULL, NULL, 0);
            if (!method_type) {
                return 0;
            }

            int param_count = member->type == AST_FUNCTION_DECLARATION
                ? member->data.function_decl.parameter_count
                : member->data.static_function_decl.parameter_count;
            KrtTokenType* param_types = member->type == AST_FUNCTION_DECLARATION
                ? member->data.function_decl.parameter_types
                : member->data.static_function_decl.parameter_types;
            int* param_is_array = member->type == AST_FUNCTION_DECLARATION
                ? member->data.function_decl.parameter_is_array
                : member->data.static_function_decl.parameter_is_array;

            for (int i = 0; i < param_count; i++) {
                Type* param_type = NULL;
                switch (param_types[i]) {
                    case TOKEN_INT8: param_type = type_create_basic(TYPE_INT8); break;
                    case TOKEN_INT16: param_type = type_create_basic(TYPE_INT16); break;
                    case TOKEN_INT32: param_type = type_create_basic(TYPE_INT32); break;
                    case TOKEN_INT64: param_type = type_create_basic(TYPE_INT64); break;
                    case TOKEN_UINT8: param_type = type_create_basic(TYPE_UINT8); break;
                    case TOKEN_UINT16: param_type = type_create_basic(TYPE_UINT16); break;
                    case TOKEN_UINT32: param_type = type_create_basic(TYPE_UINT32); break;
                    case TOKEN_UINT64: param_type = type_create_basic(TYPE_UINT64); break;
                    case TOKEN_FLOAT32: param_type = type_create_basic(TYPE_FLOAT32); break;
                    case TOKEN_FLOAT64: param_type = type_create_basic(TYPE_FLOAT64); break;
                    case TOKEN_BOOL: param_type = type_create_basic(TYPE_BOOL); break;
                    case TOKEN_STRING: param_type = type_create_basic(TYPE_STRING); break;
                    default: param_type = type_create_basic(TYPE_UNKNOWN); break;
                }
                if (param_type && param_is_array && param_is_array[i]) {
                    Type* array_type = type_create_array(param_type, 0);
                    type_destroy(param_type);
                    param_type = array_type;
                }
                type_function_add_parameter(method_type, param_type);
            }

            KrtTokenType return_type_token;
            if (member->type == AST_FUNCTION_DECLARATION) {
                return_type_token = member->data.function_decl.return_type;
            } else {
                return_type_token = member->data.static_function_decl.return_type;
            }

            switch (return_type_token) {
                case TOKEN_INT8: method_type->data.function.return_type = type_create_basic(TYPE_INT8); break;
                case TOKEN_INT16: method_type->data.function.return_type = type_create_basic(TYPE_INT16); break;
                case TOKEN_INT32: method_type->data.function.return_type = type_create_basic(TYPE_INT32); break;
                case TOKEN_INT64: method_type->data.function.return_type = type_create_basic(TYPE_INT64); break;
                case TOKEN_UINT8: method_type->data.function.return_type = type_create_basic(TYPE_UINT8); break;
                case TOKEN_UINT16: method_type->data.function.return_type = type_create_basic(TYPE_UINT16); break;
                case TOKEN_UINT32: method_type->data.function.return_type = type_create_basic(TYPE_UINT32); break;
                case TOKEN_UINT64: method_type->data.function.return_type = type_create_basic(TYPE_UINT64); break;
                case TOKEN_FLOAT32: method_type->data.function.return_type = type_create_basic(TYPE_FLOAT32); break;
                case TOKEN_FLOAT64: method_type->data.function.return_type = type_create_basic(TYPE_FLOAT64); break;
                case TOKEN_BOOL: method_type->data.function.return_type = type_create_basic(TYPE_BOOL); break;
                case TOKEN_STRING: method_type->data.function.return_type = type_create_basic(TYPE_STRING); break;
                case TOKEN_VOID: method_type->data.function.return_type = type_create_basic(TYPE_VOID); break;
                default: method_type->data.function.return_type = type_create_basic(TYPE_UNKNOWN); break;
            }

            ASTNode* body = member->type == AST_FUNCTION_DECLARATION
                ? member->data.function_decl.body
                : member->data.static_function_decl.body;

            ClassMember class_member = {
                .name = KRT_STRDUP(method_name),
                .member_type = MEMBER_METHOD,
                .access = access,
                .is_static = is_static,
                .type = method_type
            };

            class_info_add_member(class_info, class_member);

            if (class_info->member_scope) {
                TypeCheckSymbol method_sym = {
                    .name = KRT_STRDUP(method_name),
                    .type = type_copy(method_type),
                    .is_constant = 1,
                    .line_number = 0
                };
                type_check_symbol_table_add(class_info->member_scope, method_sym);
            }

            TypeCheckSymbolTable* old_scope = context->current_scope;
            context->current_scope = class_info->member_scope;

            TypeCheckSymbolTable* method_scope = type_check_symbol_table_create(context->current_scope);
            context->current_scope = method_scope;

            int old_is_static = context->current_function_is_static;
            context->current_function_is_static = is_static;

            char** params = member->type == AST_FUNCTION_DECLARATION
                ? member->data.function_decl.parameters
                : member->data.static_function_decl.parameters;
            for (int i = 0; i < param_count; i++) {
                TypeCheckSymbol param_symbol = {
                    .name = KRT_STRDUP(params[i]),
                    .type = type_copy(method_type->data.function.parameter_types[i]),
                    .is_constant = 0,
                    .line_number = 0
                };
                type_check_symbol_table_add(context->current_scope, param_symbol);
            }

            if (return_type_token == TOKEN_UNKNOWN) {
                Type* inferred_return_type = infer_function_return_type(context, body);
                if (inferred_return_type) {
                    if (inferred_return_type->kind != TYPE_VOID) {
                        type_destroy(method_type->data.function.return_type);
                        method_type->data.function.return_type = inferred_return_type;

                        if (class_info->member_scope) {
                            TypeCheckSymbol* method_sym = type_check_symbol_table_lookup(class_info->member_scope, method_name);
                            if (method_sym && method_sym->type->kind == TYPE_FUNCTION) {
                                type_destroy(method_sym->type->data.function.return_type);
                                method_sym->type->data.function.return_type = type_copy(inferred_return_type);
                            }
                        }
                    } else {
                        type_destroy(inferred_return_type);
                    }
                } else {
                }
            } else {
            }

            if (body && !type_check_statement(context, body)) {
                type_check_symbol_table_destroy(context->current_scope);
                context->current_scope = old_scope;
                context->current_function_is_static = old_is_static;
                return 0;
            }

            type_check_symbol_table_destroy(context->current_scope);
            context->current_scope = old_scope;
            context->current_function_is_static = old_is_static;
            break;
        }

        case AST_PROPERTY_DECLARATION: {
            const char* prop_name = member->data.property_decl.name;

            ClassMember* existing = class_info_find_member(class_info, prop_name);
            if (existing) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                         "Property '%s' already declared in class '%s'",
                         prop_name, class_info->class_name);
                return 0;
            }

            KrtTokenType type_token = member->data.property_decl.type;
            Type* prop_type = NULL;
            switch (type_token) {
                case TOKEN_INT8: prop_type = type_create_basic(TYPE_INT8); break;
                case TOKEN_INT16: prop_type = type_create_basic(TYPE_INT16); break;
                case TOKEN_INT32: prop_type = type_create_basic(TYPE_INT32); break;
                case TOKEN_INT64: prop_type = type_create_basic(TYPE_INT64); break;
                case TOKEN_UINT8: prop_type = type_create_basic(TYPE_UINT8); break;
                case TOKEN_UINT16: prop_type = type_create_basic(TYPE_UINT16); break;
                case TOKEN_UINT32: prop_type = type_create_basic(TYPE_UINT32); break;
                case TOKEN_UINT64: prop_type = type_create_basic(TYPE_UINT64); break;
                case TOKEN_FLOAT32: prop_type = type_create_basic(TYPE_FLOAT32); break;
                case TOKEN_FLOAT64: prop_type = type_create_basic(TYPE_FLOAT64); break;
                case TOKEN_BOOL: prop_type = type_create_basic(TYPE_BOOL); break;
                case TOKEN_STRING: prop_type = type_create_basic(TYPE_STRING); break;
                default: prop_type = type_create_basic(TYPE_UNKNOWN); break;
            }

            ClassMember class_member = {
                .name = KRT_STRDUP(prop_name),
                .member_type = MEMBER_PROPERTY,
                .access = access,
                .is_static = is_static,
                .type = prop_type
            };

            class_info_add_member(class_info, class_member);

            if (class_info->member_scope) {
                TypeCheckSymbol prop_sym = {
                    .name = KRT_STRDUP(prop_name),
                    .type = type_copy(prop_type),
                    .is_constant = 0,
                    .line_number = 0
                };
                type_check_symbol_table_add(class_info->member_scope, prop_sym);
            }

            ASTNode* getter = member->data.property_decl.getter;
            ASTNode* setter = member->data.property_decl.setter;

            TypeCheckSymbolTable* old_scope = context->current_scope;
            context->current_scope = class_info->member_scope;

            if (getter && getter->data.property_getter.body) {
                if (!type_check_statement(context, getter->data.property_getter.body)) {
                    context->current_scope = old_scope;
                    return 0;
                }
            }

            if (setter && setter->data.property_setter.body) {
                
                TypeCheckSymbolTable* setter_scope = type_check_symbol_table_create(context->current_scope);
                context->current_scope = setter_scope;
                
                TypeCheckSymbol value_sym = {
                    .name = KRT_STRDUP("value"),
                    .type = type_copy(prop_type),
                    .is_constant = 0,
                    .line_number = 0
                };
                type_check_symbol_table_add(context->current_scope, value_sym);
                
                bool setter_ok = type_check_statement(context, setter->data.property_setter.body);
                
                type_check_symbol_table_destroy(context->current_scope);
                context->current_scope = old_scope;
                
                if (!setter_ok) {
                    return 0;
                }
            }

            context->current_scope = old_scope;
            break;
        }

        default:
            return 0;
    }

    return 1;
}

static int check_variable_declaration(TypeCheckContext* context, ASTNode* statement) {
    
    if (statement->data.variable_decl.is_array) {
        int array_size = 0;
        if (statement->data.variable_decl.array_size) {
            Type* size_type = type_check_expression(context, statement->data.variable_decl.array_size);
            if (!size_type) {
                return 0;
            }
            if (size_type->kind != TYPE_INT32) {
                type_destroy(size_type);
                return 0;
            }
            array_size = 10;
            type_destroy(size_type);
        }

        Type* initializer_type = NULL;
        if (statement->data.variable_decl.value) {
            initializer_type = type_check_expression(context, statement->data.variable_decl.value);
            if (!initializer_type) {
                return 0;
            }
            if (initializer_type->kind != TYPE_ARRAY) {
                type_destroy(initializer_type);
                return 0;
            }
            Type* symbol_type = type_copy(initializer_type);
            if (!symbol_type) {
                type_destroy(initializer_type);
                return 0;
            }
            TypeCheckSymbol symbol = {
                .name = KRT_STRDUP(statement->data.variable_decl.name),
                .type = symbol_type,
                .is_constant = 0,
                .line_number = 0,
                .owns_name = 1
            };
            if (!type_check_symbol_table_add(context->current_scope, symbol)) {
                type_destroy(symbol_type);
                KRT_FREE(symbol.name);
                type_destroy(initializer_type);
                return 0;
            }
            type_destroy(initializer_type);
            return 1;
        }

        Type* element_type = type_create_basic(TYPE_INT32);
        Type* array_type = type_create_array(element_type, array_size > 0 ? array_size : 10);
        if (!array_type) {
            return 0;
        }
        TypeCheckSymbol symbol = {
            .name = KRT_STRDUP(statement->data.variable_decl.name),
            .type = array_type,
            .is_constant = 0,
            .line_number = 0,
            .owns_name = 1
        };
        if (!type_check_symbol_table_add(context->current_scope, symbol)) {
            type_destroy(array_type);
            KRT_FREE(symbol.name);
            return 0;
        }
        return 1;
    }

    Type* initializer_type = NULL;
    if (statement->data.variable_decl.value) {
        initializer_type = type_check_expression(context, statement->data.variable_decl.value);
        if (!initializer_type) {
            return 0;
        }
    }

    KrtTokenType declared_token = statement->data.variable_decl.type;
    Type* declared_type = NULL;
    if (declared_token == TOKEN_IDENTIFIER && statement->data.variable_decl.template_instantiation_type) {
        declared_type = type_from_string(context, statement->data.variable_decl.template_instantiation_type);
    }
    if (!declared_type && declared_token != TOKEN_UNKNOWN) {
        declared_type = type_create_from_token(declared_token);
    }

    if (declared_type && statement->data.variable_decl.pointer_depth > 0) {
        Type* base_type = declared_type;
        for (int i = 0; i < statement->data.variable_decl.pointer_depth; i++) {
            declared_type = type_create_pointer(base_type);
            base_type = declared_type;
        }
    }

    Type* symbol_type = NULL;
    if (declared_type && initializer_type) {
        if (!type_is_assignable(declared_type, initializer_type)) {
            type_destroy(initializer_type);
            type_destroy(declared_type);
            return 0;
        }
        type_destroy(initializer_type);
        symbol_type = declared_type;
    } else if (declared_type) {
        symbol_type = declared_type;
        if (initializer_type) type_destroy(initializer_type);
    } else if (initializer_type) {
        symbol_type = type_copy(initializer_type);
        if (!symbol_type) {
            type_destroy(initializer_type);
            return 0;
        }
        type_destroy(initializer_type);
    } else {
        symbol_type = type_create_basic(TYPE_UNKNOWN);
        if (!symbol_type) {
            return 0;
        }
    }

    TypeCheckSymbol symbol = {
        .name = KRT_STRDUP(statement->data.variable_decl.name),
        .type = symbol_type,
        .is_constant = 0,
        .line_number = 0,
        .owns_name = 1
    };

    if (!type_check_symbol_table_add(context->current_scope, symbol)) {
        type_destroy(symbol_type);
        KRT_FREE(symbol.name);
        return 0;
    }
    return 1;
}

static int check_assignment(TypeCheckContext* context, ASTNode* statement) {
    const char* var_name = (statement->type == AST_ASSIGNMENT) ?
                           statement->data.assignment.name :
                           statement->data.compound_assignment.name;
    ASTNode* value_node = (statement->type == AST_ASSIGNMENT) ?
                           statement->data.assignment.value :
                           statement->data.compound_assignment.value;

    TypeCheckSymbol* left_symbol = type_check_symbol_table_lookup(context->current_scope, var_name);
    if (!left_symbol) {
        return 0;
    }

    if (value_node) {
        Type* value_type = type_check_expression(context, value_node);
        if (!value_type) {
            return 0;
        }

        if (!type_is_assignable(left_symbol->type, value_type)) {
            type_destroy(value_type);
            return 0;
        }
        type_destroy(value_type);
    }
    return 1;
}

static int check_array_assignment(TypeCheckContext* context, ASTNode* statement) {
    ASTNode* array_node = (statement->type == AST_ARRAY_ASSIGNMENT) ?
                           statement->data.array_assignment.array :
                           statement->data.array_compound_assignment.array;
    ASTNode* index_node = (statement->type == AST_ARRAY_ASSIGNMENT) ?
                           statement->data.array_assignment.index :
                           statement->data.array_compound_assignment.index;
    ASTNode* value_node = (statement->type == AST_ARRAY_ASSIGNMENT) ?
                           statement->data.array_assignment.value :
                           statement->data.array_compound_assignment.value;

    Type* array_type = type_check_expression(context, array_node);
    if (!array_type) {
        return 0;
    }

    if (array_type->kind != TYPE_ARRAY) {
        type_destroy(array_type);
        return 0;
    }

    Type* index_type = type_check_expression(context, index_node);
    if (!index_type) {
        type_destroy(array_type);
        return 0;
    }

    if (index_type->kind != TYPE_INT32) {
        type_destroy(array_type);
        type_destroy(index_type);
        return 0;
    }
    type_destroy(index_type);

    Type* value_type = type_check_expression(context, value_node);
    if (!value_type) {
        type_destroy(array_type);
        return 0;
    }

    Type* element_type = array_type->data.array.element_type;
    if (!type_is_assignable(element_type, value_type)) {
        type_destroy(array_type);
        type_destroy(value_type);
        return 0;
    }

    type_destroy(array_type);
    type_destroy(value_type);
    return 1;
}

static int check_return_statement(TypeCheckContext* context, ASTNode* statement) {
    if (statement->data.return_stmt.value) {
        Type* return_type = type_check_expression(context, statement->data.return_stmt.value);
        if (!return_type) {
            return 0;
        }
        type_destroy(return_type);
    }
    return 1;
}

static int check_if_statement(TypeCheckContext* context, ASTNode* statement) {
    Type* condition_type = type_check_expression(context, statement->data.if_stmt.condition);
    if (!condition_type) {
        return 0;
    }

    if (condition_type->kind != TYPE_BOOL) {
        type_destroy(condition_type);
        return 0;
    }
    type_destroy(condition_type);

    if (!type_check_statement(context, statement->data.if_stmt.then_branch)) {
        return 0;
    }

    if (statement->data.if_stmt.else_branch) {
        if (!type_check_statement(context, statement->data.if_stmt.else_branch)) {
            return 0;
        }
    }
    return 1;
}

static int check_while_statement(TypeCheckContext* context, ASTNode* statement) {
    Type* condition_type = type_check_expression(context, statement->data.while_stmt.condition);
    if (!condition_type) {
        return 0;
    }

    if (condition_type->kind != TYPE_BOOL) {
        type_destroy(condition_type);
        return 0;
    }
    type_destroy(condition_type);

    if (!type_check_statement(context, statement->data.while_stmt.body)) {
        return 0;
    }
    return 1;
}

static int check_for_statement(TypeCheckContext* context, ASTNode* statement) {
    if (statement->data.for_stmt.init) {
        if (!type_check_statement(context, statement->data.for_stmt.init)) {
            return 0;
        }
    }

    if (statement->data.for_stmt.condition) {
        Type* condition_type = type_check_expression(context, statement->data.for_stmt.condition);
        if (!condition_type) {
            return 0;
        }

        if (condition_type->kind != TYPE_BOOL) {
            type_destroy(condition_type);
            return 0;
        }
        type_destroy(condition_type);
    }

    if (statement->data.for_stmt.increment) {
        Type* increment_type = type_check_expression(context, statement->data.for_stmt.increment);
        if (!increment_type) {
            return 0;
        }
        type_destroy(increment_type);
    }

    if (!type_check_statement(context, statement->data.for_stmt.body)) {
        return 0;
    }
    return 1;
}

static int check_foreach_statement(TypeCheckContext* context, ASTNode* statement) {
    Type* iterable_type = type_check_expression(context, statement->data.foreach_stmt.iterable);
    if (!iterable_type) {
        return 0;
    }
    if (iterable_type->kind != TYPE_ARRAY) {
        type_destroy(iterable_type);
        return 0;
    }

    Type* element_type = type_copy(iterable_type->data.array.element_type);

    TypeCheckSymbolTable* old_scope = context->current_scope;
    context->current_scope = type_check_symbol_table_create(old_scope);

    TypeCheckSymbol foreach_var;
    foreach_var.name = statement->data.foreach_stmt.var_name;
    foreach_var.type = element_type;
    foreach_var.is_constant = 0;
    foreach_var.line_number = 0;
    foreach_var.owns_name = 0;
    int add_result = type_check_symbol_table_add(context->current_scope, foreach_var);
    if (!add_result) {
        type_destroy(iterable_type);
        type_check_symbol_table_destroy(context->current_scope);
        context->current_scope = old_scope;
        return 0;
    }

    int body_result = type_check_statement(context, statement->data.foreach_stmt.body);
    if (!body_result) {
        type_destroy(iterable_type);
        type_check_symbol_table_destroy(context->current_scope);
        context->current_scope = old_scope;
        return 0;
    }

    type_destroy(iterable_type);
    type_check_symbol_table_destroy(context->current_scope);
    context->current_scope = old_scope;
    return 1;
}
