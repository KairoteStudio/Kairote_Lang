#include <stddef.h>

extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern char *strstr(const char *haystack, const char *needle);

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include "../Ir/Ir.h"
#include "../../Compiler.h"
#include "../../../Core/Utils/OutputCache.h"
#include "../../../Core/Utils/KrtCommon.h"
#include "../../Frontend/Semantic/TypeChecker.h"
#include "../../Frontend/Semantic/NameMangling.h"

static KrtIRValue KrtIrGenerateExpression(KrtIRBuilder* builder, ASTNode* expr);
static void KrtIrGenerateStatement(KrtIRBuilder* builder, ASTNode* stmt);
static void KrtIrGenerateBlock(KrtIRBuilder* builder, ASTNode* block);
static KrtIRValue void_val_return(void);
static void KrtIrGeneratePrint(KrtIRBuilder* builder, ASTNode** values, int count, bool has_newline);
static int try_extract_constant(ASTNode* node, double* out_value);
static KrtIRValue convert_to_string_if_needed(KrtIRBuilder* builder, ASTNode* expr_node, KrtIRValue value);
static int KrtIrCheckHasReturn(ASTNode* node);
static void KrtIrEnsureMainEntry(KrtIRBuilder* builder);
static void KrtIrPushClassContext(KrtIRBuilder* builder, const char* class_name);
static void KrtIrPopClassContext(KrtIRBuilder* builder);

KrtIRValue KrtIrSyscall(KrtIRBuilder* builder, KrtIRValue syscall_num, KrtIRValue* args, int arg_count);
static const char* KrtIrCurrentClassContext(KrtIRBuilder* builder);
static char* KrtIrMangleStaticMember(const char* class_name, const char* member_name);
static char* KrtIrMangleGenericStaticMember(const char* class_name, const char* member_name, 
                                            const char** type_args, int type_arg_count);
static __attribute__((unused)) int KrtIrEvaluateNumericConstant(ASTNode* expr, double* out_value);
static void KrtIrPushNamespaceContext(KrtIRBuilder* builder, const char* namespace_name);
static void KrtIrPopNamespaceContext(KrtIRBuilder* builder);
static const char** KrtIrGetNamespacePath(KrtIRBuilder* builder, int* out_count);

static int irgen_type_size(TypeKind kind) {
    switch (kind) {
        case TYPE_INT8:
        case TYPE_UINT8:
        case TYPE_BOOL:
            return 1;
        case TYPE_INT16:
        case TYPE_UINT16:
            return 2;
        case TYPE_INT32:
        case TYPE_UINT32:
        case TYPE_FLOAT32:
            return 4;
        default:
            return 8;
    }
}

static int irgen_array_element_size(KrtIRBuilder* builder, ASTNode* array_expr) {
    if (!builder || !builder->type_context || !array_expr) return 8;

    Type* array_type = type_check_expression(builder->type_context, array_expr);
    if (!array_type) return 8;

    int size = 8;
    if (array_type->kind == TYPE_ARRAY && array_type->data.array.element_type) {
        size = irgen_type_size(array_type->data.array.element_type->kind);
    }
    type_destroy(array_type);
    return size;
}

static int irgen_is_syscall_method(const char* class_name, const char* method_name) {
    return class_name && method_name &&
           strcmp(class_name, "Sys") == 0 && strcmp(method_name, "syscall") == 0;
}

typedef struct {
    char** type_args;
    int type_arg_count;
} KrtGenericContext;

static KrtGenericContext g_generic_context = {NULL, 0};

#define IRGEN_LABEL_BUFFER_SIZE 64
#define IRGEN_NAME_BUFFER_SIZE 256
#define IRGEN_MAX_MANGLE_PARAMS 32
#define IRGEN_POINTER_SIZE 8
#define IRGEN_DEFAULT_ARRAY_CAPACITY 10
#define IRGEN_NAMESPACE_STACK_INIT 8
#define IRGEN_CLASS_STACK_INIT 8

#define KRT_IMM_ZERO(builder) KrtIrImm(builder, 0)
#define KRT_IMM_ONE(builder) KrtIrImm(builder, 1)
#define KRT_IMM_PTR_SIZE(builder) KrtIrImm(builder, IRGEN_POINTER_SIZE)
#define KRT_IMM_FALSE(builder) KRT_IMM_ZERO(builder)
#define KRT_IMM_TRUE(builder) KRT_IMM_ONE(builder)

#define IRGEN_ALLOC_ARGS(count) ((KrtIRValue*)KRT_MALLOC((count) * sizeof(KrtIRValue)))
#define IRGEN_SAFE_FREE(ptr) do { if (ptr) { KRT_FREE(ptr); ptr = NULL; } } while(0)
#define IRGEN_FREE(ptr) IRGEN_SAFE_FREE(ptr)

#define IRGEN_SET_ARG(args, index, value) do { \
    if (args && (index) >= 0) { \
        args[(index)] = (value); \
    } \
} while(0)

#define IRGEN_CHECK_ALLOC(ptr, error_return) do { \
    if (!(ptr)) { \
        return (error_return); \
    } \
} while(0)

typedef enum {
    IRGEN_FUNC_MALLOC = 0,
    IRGEN_FUNC_FREE,
    IRGEN_FUNC_STORE_PTR,
    IRGEN_FUNC_LOAD_PTR,
    IRGEN_FUNC_STRING_CONCAT,
    IRGEN_FUNC_INT32_TO_STRING,
    IRGEN_FUNC_IS_INSTANCE,
    IRGEN_FUNC_AS_INSTANCE,
    IRGEN_FUNC_LINQ_WHERE,
    IRGEN_FUNC_LINQ_ORDERBY,
    IRGEN_FUNC_LINQ_GROUPBY,
    IRGEN_FUNC_LINQ_SELECT,
    IRGEN_FUNC_STACK_ALLOC,
    IRGEN_FUNC_AWAIT_TASK,
    IRGEN_FUNC_NULL_COALESCE,
    IRGEN_FUNC_NULL_CONDITIONAL,
    IRGEN_FUNC_MONITOR_ENTER,
    IRGEN_FUNC_MONITOR_EXIT,
    IRGEN_FUNC_PIN_OBJECT,
    IRGEN_FUNC_UNPIN_OBJECT,
    IRGEN_FUNC_DISPOSE,
    IRGEN_FUNC_RETHROW_EXCEPTION,
    IRGEN_FUNC_THROW_EXCEPTION,
    IRGEN_FUNC_GET_VARIABLE_ADDRESS,
    IRGEN_FUNC_SIZE_OF_POINTER,
    IRGEN_FUNC_ARRAY_GET,
    IRGEN_FUNC_ARRAY_SIZE,
    IRGEN_FUNC_COUNT
} IrGenBuiltinFunction;

static const char* g_builtin_function_names[IRGEN_FUNC_COUNT] = {
    "KrtMalloc",
    "KrtFree",
    "KrtStorePtr",
    "KrtLoadPtr",
    "KrtStringConcat",
    "KrtInt32ToString",
    "KrtIsInstance",
    "KrtAsInstance",
    "KrtLinqWhere",
    "KrtLinqOrderBy",
    "KrtLinqGroupBy",
    "KrtLinqSelect",
    "KrtStackAlloc",
    "KrtAwaitTask",
    "KrtNullCoalesce",
    "KrtNullConditional",
    "Monitor_Enter",
    "Monitor_Exit",
    "KrtPinObject",
    "KrtUnpinObject",
    "Dispose",
    "KrtRethrowException",
    "KrtThrowException",
    "KrtGetVariableAddress",
    "KrtSizeOfPointer",
    "array_get",
    "array_size"
};

static const char* irgen_get_builtin_func(IrGenBuiltinFunction func_id) {
    if (func_id >= 0 && func_id < IRGEN_FUNC_COUNT) {
        return g_builtin_function_names[func_id];
    }
    return NULL;
}

static KrtIRValue irgen_call_builtin_0args(KrtIRBuilder* builder, IrGenBuiltinFunction func_id) {
    return KrtIrCall(builder, irgen_get_builtin_func(func_id), NULL, 0);
}

static KrtIRValue irgen_call_builtin_1arg(KrtIRBuilder* builder, IrGenBuiltinFunction func_id, 
                                          KrtIRValue arg0) {
    KrtIRValue args[1] = { arg0 };
    return KrtIrCall(builder, irgen_get_builtin_func(func_id), args, 1);
}

static KrtIRValue irgen_call_builtin_2args(KrtIRBuilder* builder, IrGenBuiltinFunction func_id, 
                                           KrtIRValue arg0, KrtIRValue arg1) {
    KrtIRValue args[2] = { arg0, arg1 };
    return KrtIrCall(builder, irgen_get_builtin_func(func_id), args, 2);
}

static KrtIRValue irgen_call_builtin_3args(KrtIRBuilder* builder, IrGenBuiltinFunction func_id, 
                                           KrtIRValue arg0, KrtIRValue arg1, KrtIRValue arg2) {
    KrtIRValue args[3] = { arg0, arg1, arg2 };
    return KrtIrCall(builder, irgen_get_builtin_func(func_id), args, 3);
}

static KrtIRValue irgen_call_builtin_4args(KrtIRBuilder* builder, IrGenBuiltinFunction func_id, 
                                           KrtIRValue arg0, KrtIRValue arg1, 
                                           KrtIRValue arg2, KrtIRValue arg3) {
    KrtIRValue args[4] = { arg0, arg1, arg2, arg3 };
    return KrtIrCall(builder, irgen_get_builtin_func(func_id), args, 4);
}

static KrtIRValue irgen_allocate_memory(KrtIRBuilder* builder, KrtIRValue size_val) {
    const int header_size = IRGEN_POINTER_SIZE;
    char result_name[64];
    char success_label[64];
    char failure_label[64];
    char end_label[64];
    int label_id = builder->label_counter++;
    snprintf(result_name, sizeof(result_name), "__krt_malloc_result_%d", label_id);
    snprintf(success_label, sizeof(success_label), "malloc_success_%d", label_id);
    snprintf(failure_label, sizeof(failure_label), "malloc_failure_%d", label_id);
    snprintf(end_label, sizeof(end_label), "malloc_end_%d", label_id);

    KrtIrAlloc(builder, result_name);
    KrtIrStore(builder, result_name, KrtIrImm(builder, 0));

    KrtIRValue total_size = KrtIrAdd(builder, size_val, KrtIrImm(builder, header_size));
    KrtIRValue mmap_args[6] = {
        KrtIrImm(builder, 0),
        total_size,
        KrtIrImm(builder, 3),
        KrtIrImm(builder, 0x22),
        KrtIrImm(builder, -1),
        KrtIrImm(builder, 0)
    };
    KrtIRValue allocation = KrtIrSyscall(builder, KrtIrImm(builder, 9), mmap_args, 6);

    KrtIRBasicBlock* source_block = builder->current_block;
    KrtIRBasicBlock* success_block = KrtIrBlockCreate(builder, success_label);
    KrtIRBasicBlock* failure_block = KrtIrBlockCreate(builder, failure_label);
    KrtIRBasicBlock* end_block = KrtIrBlockCreate(builder, end_label);
    KrtIrBranch(builder,
                KrtIrCompare(builder, KRT_IR_LT, allocation, KrtIrImm(builder, 0)),
                failure_block, success_block);
    if (source_block) source_block->next = failure_block;
    failure_block->next = success_block;
    success_block->next = end_block;

    KrtIrBlockSetCurrent(builder, failure_block);
    KrtIrJump(builder, end_block);

    KrtIrBlockSetCurrent(builder, success_block);
    KrtIrStorePtr(builder, allocation, 0, total_size);
    KrtIrStore(builder, result_name,
               KrtIrAdd(builder, allocation, KrtIrImm(builder, header_size)));
    KrtIrJump(builder, end_block);

    KrtIrBlockSetCurrent(builder, end_block);
    return KrtIrLoad(builder, result_name);
}

static void irgen_free_memory(KrtIRBuilder* builder, KrtIRValue ptr_val) {
    const int header_size = IRGEN_POINTER_SIZE;

    char free_label[64];
    char end_label[64];
    int label_id = builder->label_counter++;
    snprintf(free_label, sizeof(free_label), "free_release_%d", label_id);
    snprintf(end_label, sizeof(end_label), "free_end_%d", label_id);

    KrtIRBasicBlock* source_block = builder->current_block;
    KrtIRBasicBlock* free_block = KrtIrBlockCreate(builder, free_label);
    KrtIRBasicBlock* end_block = KrtIrBlockCreate(builder, end_label);
    KrtIrBranch(builder,
                KrtIrCompare(builder, KRT_IR_EQ, ptr_val, KrtIrImm(builder, 0)),
                end_block, free_block);
    if (source_block) source_block->next = free_block;
    free_block->next = end_block;

    KrtIrBlockSetCurrent(builder, free_block);
    KrtIRValue allocation = KrtIrSub(builder, ptr_val, KrtIrImm(builder, header_size));
    KrtIRValue total_size = KrtIrLoadPtr(builder, allocation, 0);
    KrtIRValue munmap_args[2] = { allocation, total_size };
    KrtIrSyscall(builder, KrtIrImm(builder, 11), munmap_args, 2);
    KrtIrJump(builder, end_block);

    KrtIrBlockSetCurrent(builder, end_block);
}

static KrtIRValue irgen_store_pointer(KrtIRBuilder* builder, KrtIRValue ptr, 
                                       KrtIRValue offset, KrtIRValue value) {
    return irgen_call_builtin_3args(builder, IRGEN_FUNC_STORE_PTR, ptr, offset, value);
}

static KrtIRValue irgen_load_pointer(KrtIRBuilder* builder, KrtIRValue ptr, KrtIRValue offset) {
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_LOAD_PTR, ptr, offset);
}

static KrtIRValue irgen_concat_strings(KrtIRBuilder* builder, KrtIRValue left, KrtIRValue right) {
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_STRING_CONCAT, left, right);
}

static KrtIRValue irgen_int32_to_string(KrtIRBuilder* builder, KrtIRValue value) {
    return irgen_call_builtin_1arg(builder, IRGEN_FUNC_INT32_TO_STRING, value);
}

static KrtIRValue irgen_is_instance(KrtIRBuilder* builder, KrtIRValue obj, const char* type_name) {
    KrtIRValue type_str = type_name ? KrtIrStringConst(builder, type_name) : KRT_IMM_ZERO(builder);
    if (!type_name) {
        type_str.type = KRT_IR_VALUE_STRING_CONST;
        type_str.data.string_const_id = -1;
    }
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_IS_INSTANCE, obj, type_str);
}

static KrtIRValue irgen_as_instance(KrtIRBuilder* builder, KrtIRValue obj, const char* type_name) {
    KrtIRValue type_str = type_name ? KrtIrStringConst(builder, type_name) : KRT_IMM_ZERO(builder);
    if (!type_name) {
        type_str.type = KRT_IR_VALUE_STRING_CONST;
        type_str.data.string_const_id = -1;
    }
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_AS_INSTANCE, obj, type_str);
}

static KrtIRValue irgen_linq_where(KrtIRBuilder* builder, KrtIRValue source, KrtIRValue predicate) {
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_LINQ_WHERE, source, predicate);
}

static KrtIRValue irgen_linq_orderby(KrtIRBuilder* builder, KrtIRValue source, int ascending) {
    return irgen_call_builtin_3args(builder, IRGEN_FUNC_LINQ_ORDERBY, source, 
                                     KrtIrImm(builder, ascending ? 1 : 0), KRT_IMM_ZERO(builder));
}

static KrtIRValue irgen_linq_groupby(KrtIRBuilder* builder, KrtIRValue source, 
                                      KrtIRValue key_expr, KrtIRValue elem_expr, 
                                      const char* into_var) {
    KrtIRValue into_str = into_var ? KrtIrStringConst(builder, into_var) : KrtIrStringConst(builder, "");
    return irgen_call_builtin_4args(builder, IRGEN_FUNC_LINQ_GROUPBY, source, key_expr, elem_expr, into_str);
}

static KrtIRValue irgen_linq_select(KrtIRBuilder* builder, KrtIRValue source, KrtIRValue selector) {
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_LINQ_SELECT, source, selector);
}

static KrtIRValue irgen_stack_alloc(KrtIRBuilder* builder, KrtIRValue size_val) {
    return irgen_call_builtin_1arg(builder, IRGEN_FUNC_STACK_ALLOC, size_val);
}

static KrtIRValue irgen_await_task(KrtIRBuilder* builder, KrtIRValue task) {
    return irgen_call_builtin_1arg(builder, IRGEN_FUNC_AWAIT_TASK, task);
}

static KrtIRValue irgen_null_coalesce(KrtIRBuilder* builder, KrtIRValue left, KrtIRValue right) {
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_NULL_COALESCE, left, right);
}

static KrtIRValue irgen_null_conditional(KrtIRBuilder* builder, KrtIRValue obj, const char* member) {
    KrtIRValue member_str = member ? KrtIrStringConst(builder, member) : KRT_IMM_ZERO(builder);
    if (!member) {
        member_str.type = KRT_IR_VALUE_STRING_CONST;
        member_str.data.string_const_id = -1;
    }
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_NULL_CONDITIONAL, obj, member_str);
}

static void irgen_monitor_enter(KrtIRBuilder* builder, KrtIRValue lock_obj) {
    irgen_call_builtin_1arg(builder, IRGEN_FUNC_MONITOR_ENTER, lock_obj);
}

static void irgen_monitor_exit(KrtIRBuilder* builder, KrtIRValue lock_obj) {
    irgen_call_builtin_1arg(builder, IRGEN_FUNC_MONITOR_EXIT, lock_obj);
}

static void irgen_pin_object(KrtIRBuilder* builder, KrtIRValue obj) {
    irgen_call_builtin_1arg(builder, IRGEN_FUNC_PIN_OBJECT, obj);
}

static void irgen_unpin_object(KrtIRBuilder* builder, KrtIRValue obj) {
    irgen_call_builtin_1arg(builder, IRGEN_FUNC_UNPIN_OBJECT, obj);
}

static void irgen_dispose(KrtIRBuilder* builder, KrtIRValue resource) {
    irgen_call_builtin_1arg(builder, IRGEN_FUNC_DISPOSE, resource);
}

static void irgen_rethrow_exception(KrtIRBuilder* builder) {
    irgen_call_builtin_0args(builder, IRGEN_FUNC_RETHROW_EXCEPTION);
}

static void irgen_throw_exception(KrtIRBuilder* builder) {
    irgen_call_builtin_0args(builder, IRGEN_FUNC_THROW_EXCEPTION);
}

static KrtIRValue irgen_get_variable_address(KrtIRBuilder* builder, const char* var_name) {
    return irgen_call_builtin_1arg(builder, IRGEN_FUNC_GET_VARIABLE_ADDRESS, 
                                    KrtIrStringConst(builder, var_name));
}

static KrtIRValue irgen_size_of_pointer(KrtIRBuilder* builder) {
    return irgen_call_builtin_0args(builder, IRGEN_FUNC_SIZE_OF_POINTER);
}

static KrtIRValue irgen_array_get(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index) {
    return irgen_call_builtin_2args(builder, IRGEN_FUNC_ARRAY_GET, array, index);
}

static KrtIRValue irgen_array_size(KrtIRBuilder* builder, KrtIRValue array) {
    return irgen_call_builtin_1arg(builder, IRGEN_FUNC_ARRAY_SIZE, array);
}

static KrtTokenType KrtIrInferMangleType(KrtIRBuilder* builder, ASTNode* arg) {
    if (!arg) return TOKEN_INT32;

    switch (arg->type) {
        case AST_STRING:
            return TOKEN_TYPE_STRING;
        case AST_BOOLEAN:
            return TOKEN_BOOL;
        case AST_CHAR_LITERAL:
            return TOKEN_CHAR;
        case AST_NUMBER:
            return TOKEN_INT32;
        case AST_CAST_EXPRESSION:
            return arg->data.cast_expr.target_type;
        case AST_UNARY_OPERATION:
            return KrtIrInferMangleType(builder, arg->data.unary_op.operand);
        case AST_IDENTIFIER:
            if (builder && builder->current_function) {
                for (int i = 0; i < builder->current_function->param_count; i++) {
                    if (strcmp(builder->current_function->params[i].name,
                               arg->data.identifier_name) == 0) {
                        return builder->current_function->params[i].type;
                    }
                }
            }
            return TOKEN_INT32;
        default:
            return TOKEN_INT32;
    }
}

static void KrtIrFillParamTypesForMangle(KrtIRBuilder* builder, ASTNode** args,
                                         int count, KrtTokenType* out) {
    for (int i = 0; i < count; i++) {
        out[i] = KrtIrInferMangleType(builder, args ? args[i] : NULL);
    }
}

static __attribute__((unused)) char* KrtIrMangleCallFunctionName(KrtIRBuilder* builder,
                                         const char** ns_path, const char* base_name,
                                         ASTNode** call_args, int arg_count) {
    if (!base_name) {
        return NULL;
    }
    KrtTokenType buf[IRGEN_MAX_MANGLE_PARAMS];
    int n = arg_count;
    if (n > IRGEN_MAX_MANGLE_PARAMS) {
        n = IRGEN_MAX_MANGLE_PARAMS;
    }
    if (n > 0 && call_args) {
        KrtIrFillParamTypesForMangle(builder, call_args, n, buf);
        return name_mangle_function(ns_path, base_name, buf, n);
    }
    return name_mangle_function(ns_path, base_name, NULL, 0);
}

static char* KrtIrMangleClassMethodName(const char* class_name, const char* method_name,
                                         KrtTokenType* param_types, int param_count) {
    if (!class_name || !method_name) {
        return NULL;
    }
    const char* ns_path[2] = { class_name, NULL };
    return name_mangle_function(ns_path, method_name, param_types, param_count);
}

static char* KrtIrMangleStaticMember(const char* class_name, const char* member_name) {
    if (g_generic_context.type_arg_count > 0 && g_generic_context.type_args) {
        return KrtIrMangleGenericStaticMember(class_name, member_name, 
                                               (const char**)g_generic_context.type_args, 
                                               g_generic_context.type_arg_count);
    }
    return name_mangle_simple(class_name, member_name);
}

static char* KrtIrMangleGenericStaticMember(const char* class_name, const char* member_name, 
                                            const char** type_args, int type_arg_count) {
    if (!class_name || !member_name) {
        return NULL;
    }
    
    size_t total_len = strlen(class_name) + strlen(member_name) + 16;
    for (int i = 0; i < type_arg_count; i++) {
        if (type_args[i]) {
            total_len += strlen(type_args[i]) + 1;
        }
    }
    
    char* result = (char*)KRT_MALLOC(total_len);
    if (!result) {
        return NULL;
    }
    
    strcpy(result, class_name);
    strcat(result, "__");
    
    for (int i = 0; i < type_arg_count; i++) {
        if (type_args[i]) {
            if (i > 0) {
                strcat(result, "_");
            }
            strcat(result, type_args[i]);
        }
    }
    
    strcat(result, "__");
    strcat(result, member_name);
    
    return result;
}

static void KrtIrPushNamespaceContext(KrtIRBuilder* builder, const char* namespace_name) {
    if (!builder || !namespace_name) {
        return;
    }
    if (builder->namespace_stack_size >= builder->namespace_stack_capacity) {
        int new_cap = builder->namespace_stack_capacity == 0 ? IRGEN_NAMESPACE_STACK_INIT : builder->namespace_stack_capacity * 2;
        char** new_stack = (char**)KRT_REALLOC(builder->namespace_stack, new_cap * sizeof(char*));
        if (!new_stack) {
            return;
        }
        builder->namespace_stack = new_stack;
        builder->namespace_stack_capacity = new_cap;
    }
    builder->namespace_stack[builder->namespace_stack_size] = KRT_STRDUP(namespace_name);
    builder->namespace_stack_size++;
}

static void KrtIrPopNamespaceContext(KrtIRBuilder* builder) {
    if (!builder || builder->namespace_stack_size <= 0) {
        return;
    }
    builder->namespace_stack_size--;
    if (builder->namespace_stack[builder->namespace_stack_size]) {
        KRT_FREE(builder->namespace_stack[builder->namespace_stack_size]);
        builder->namespace_stack[builder->namespace_stack_size] = NULL;
    }
}

static const char** KrtIrGetNamespacePath(KrtIRBuilder* builder, int* out_count) {
    if (!builder || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    if (builder->namespace_stack_size <= 0) {
        *out_count = 0;
        return NULL;
    }
    
    const char** path = (const char**)KRT_MALLOC(sizeof(const char*) * (size_t)(builder->namespace_stack_size + 1));
    if (!path) {
        *out_count = 0;
        return NULL;
    }
    
    for (int i = 0; i < builder->namespace_stack_size; i++) {
        path[i] = builder->namespace_stack[i];
    }
    path[builder->namespace_stack_size] = NULL;
    *out_count = builder->namespace_stack_size;
    
    return path;
}

static bool is_string_expression(KrtIRBuilder* builder, ASTNode* expr) {
    if (!expr) {
        return false;
    }
    if (expr->type == AST_STRING) {
        return true;
    }
    if (expr->type == AST_IDENTIFIER) {
        if (builder->type_context && builder->type_context->current_scope) {
            TypeCheckSymbolTable* scope = builder->type_context->current_scope;
            while (scope) {
                TypeCheckSymbol* symbol = type_check_symbol_table_lookup(scope, expr->data.identifier_name);
                if (symbol && symbol->type) {
                    return symbol->type->kind == TYPE_STRING;
                }
                scope = scope->parent;
            }
        }
        return false;
    }
    if (expr->type == AST_BINARY_OPERATION && expr->data.binary_op.operator == TOKEN_PLUS) {
        return is_string_expression(builder, expr->data.binary_op.left) ||
               is_string_expression(builder, expr->data.binary_op.right);
    }
    return false;
}

static void KrtIrEnsureMainEntry(KrtIRBuilder* builder) {
    if (!builder || !builder->module) return;
    KrtIRFunction* main_func = builder->module->functions;
    while (main_func) {
        if (main_func->name && strcmp(main_func->name, "main") == 0) {
            builder->module->main_function = main_func;
            return;
        }
        main_func = main_func->next;
    }
}

static void KrtIrPushClassContext(KrtIRBuilder* builder, const char* class_name) {
    if (!builder || !class_name) {
        return;
    }
    if (builder->class_stack_size >= builder->class_stack_capacity) {
        int new_cap = builder->class_stack_capacity == 0 ? IRGEN_CLASS_STACK_INIT : builder->class_stack_capacity * 2;
        char** new_stack = (char**)KRT_REALLOC(builder->class_name_stack, new_cap * sizeof(char*));
        if (!new_stack) {
            return;
        }
        builder->class_name_stack = new_stack;
        builder->class_stack_capacity = new_cap;
    }
    builder->class_name_stack[builder->class_stack_size] = KRT_STRDUP(class_name);
    builder->class_stack_size++;
}

static void KrtIrPopClassContext(KrtIRBuilder* builder) {
    if (!builder || builder->class_stack_size <= 0) {
        return;
    }
    builder->class_stack_size--;
    if (builder->class_name_stack[builder->class_stack_size]) {
        KRT_FREE(builder->class_name_stack[builder->class_stack_size]);
        builder->class_name_stack[builder->class_stack_size] = NULL;
    }
}

static const char* KrtIrCurrentClassContext(KrtIRBuilder* builder) {
    if (!builder || builder->class_stack_size <= 0) {
        return NULL;
    }
    return builder->class_name_stack[builder->class_stack_size - 1];
}

static __attribute__((unused)) int KrtIrEvaluateNumericConstant(ASTNode* expr, double* out_value) {
    if (!expr || !out_value) {
        return 0;
    }
    switch (expr->type) {
        case AST_NUMBER:
            *out_value = expr->data.number_value;
            return 1;
        case AST_BOOLEAN:
            *out_value = expr->data.boolean_value ? 1 : 0;
            return 1;
        case AST_BINARY_OPERATION: {
            double left_val, right_val;
            if (KrtIrEvaluateNumericConstant(expr->data.binary_op.left, &left_val) &&
                KrtIrEvaluateNumericConstant(expr->data.binary_op.right, &right_val)) {
                switch (expr->data.binary_op.operator) {
                    case TOKEN_PLUS: *out_value = left_val + right_val; return 1;
                    case TOKEN_MINUS: *out_value = left_val - right_val; return 1;
                    case TOKEN_MULTIPLY: *out_value = left_val * right_val; return 1;
                    case TOKEN_DIVIDE: 
                        if (right_val != 0) {
                            *out_value = left_val / right_val;
                            return 1;
                        }
                        return 0;
                    default:
                        return 0;
                }
            }
            return 0;
        }
        case AST_UNARY_OPERATION: {
            double val;
            if (KrtIrEvaluateNumericConstant(expr->data.unary_op.operand, &val)) {
                switch (expr->data.unary_op.operator) {
                    case TOKEN_MINUS: *out_value = -val; return 1;
                    default: return 0;
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

static KrtIRValue KrtIrGenerateExpression(KrtIRBuilder* builder, ASTNode* expr) {
    if (!builder || !expr) {
        fprintf(stderr, "[IrGenExpr] NULL builder or expr (type=%d)\n", expr ? (int)expr->type : -1);
        return KRT_IMM_ZERO(builder);
    }

    switch (expr->type) {
        case AST_IDENTIFIER: {
            const char* var_name = expr->data.identifier_name;

            if (builder->current_function && builder->current_function->params) {
                for (int i = 0; i < builder->current_function->param_count; i++) {
                    if (strcmp(builder->current_function->params[i].name, var_name) == 0) {
                        return KrtIrArg(builder, i);
                    }
                }
            }

            const char* current_class = KrtIrCurrentClassContext(builder);
            if (current_class && var_name) {
                int field_offset = KrtIrLayoutGetOffset(builder, current_class, var_name);
                if (field_offset >= 0) {
                    KrtIRValue this_val = KrtIrArg(builder, 0);
                    return KrtIrLoadPtr(builder, this_val, field_offset);
                }
                char* mangled = KrtIrMangleStaticMember(current_class, var_name);
                if (mangled) {
                    KrtIRGlobal* global = KrtIrModuleFindGlobal(builder->module, mangled);
                    if (global) {
                        KrtIRValue v = KrtIrLoad(builder, mangled);
                        IRGEN_FREE(mangled);
                        return v;
                    }
                    IRGEN_FREE(mangled);
                }
            }

            return KrtIrLoad(builder, var_name);
        }

        case AST_NUMBER: {
            return KrtIrImm(builder, expr->data.number_value);
        }

        case AST_BOOLEAN: {
            return KrtIrImm(builder, expr->data.boolean_value ? 1 : 0);
        }

        case AST_STRING: {
            return KrtIrStringConst(builder, expr->data.string_value);
        }

        case AST_CHAR_LITERAL: {
            return KrtIrImm(builder, (double)(unsigned char)expr->data.char_value);
        }

        case AST_NEW_EXPRESSION: {
            const char* class_name = expr->data.new_expr.class_name;
            int layout_size = KrtIrLayoutGetSize(builder, class_name);
            double alloc_size = (double)layout_size;
            
            KrtIRValue obj = irgen_allocate_memory(builder, KrtIrImm(builder, alloc_size));
            
            int ac = expr->data.new_expr.argument_count;
            char* ctor_name = name_mangle_constructor(class_name, ac);
            if (ctor_name) {
                bool exists = false;
                KrtIRFunction* f = builder->module->functions;
                while (f) {
                    if (strcmp(f->name, ctor_name) == 0) {
                        exists = true;
                        break;
                    }
                    f = f->next;
                }
                if (!exists) {
                    KrtIrFunctionCreate(builder, ctor_name, NULL, -1, TOKEN_VOID);
                }
                
                KrtIRValue* cargs = IRGEN_ALLOC_ARGS(ac + 1);
                IRGEN_CHECK_ALLOC(cargs, obj);
                
                IRGEN_SET_ARG(cargs, 0, obj);
                for (int i = 0; i < ac; i++) {
                    IRGEN_SET_ARG(cargs, i + 1, KrtIrGenerateExpression(builder, expr->data.new_expr.arguments[i]));
                }
                KrtIrCall(builder, ctor_name, cargs, ac + 1);
                IRGEN_FREE(cargs);
                IRGEN_FREE(ctor_name);
            }
            return obj;
        }

        case AST_BINARY_OPERATION: {
            KrtIRValue lhs = KrtIrGenerateExpression(builder, expr->data.binary_op.left);
            KrtIRValue rhs = KrtIrGenerateExpression(builder, expr->data.binary_op.right);
            switch (expr->data.binary_op.operator) {
                case TOKEN_ASSIGN: {
                    if (expr->data.binary_op.left->type == AST_IDENTIFIER) {
                        const char* name = expr->data.binary_op.left->data.identifier_name;
                        const char* current_class = KrtIrCurrentClassContext(builder);
                        if (current_class) {
                            int field_offset = KrtIrLayoutGetOffset(builder, current_class, name);
                            if (field_offset >= 0) {
                                KrtIRValue this_val = KrtIrArg(builder, 0);
                                irgen_store_pointer(builder, this_val, KrtIrImm(builder, field_offset), rhs);
                                return rhs;
                            }
                        }
                        KrtIrStore(builder, name, rhs);
                        return rhs;
                    }
                    if (expr->data.binary_op.left->type == AST_MEMBER_ACCESS) {
                        ASTNode* objexpr = expr->data.binary_op.left->data.member_access.object;
                        const char* member_name = expr->data.binary_op.left->data.member_access.member_name;
                        if (objexpr->type == AST_THIS) {
                            const char* current_class = KrtIrCurrentClassContext(builder);
                            int offset = KrtIrLayoutGetOffset(builder, current_class, member_name);
                            KrtIRValue this_val = KrtIrArg(builder, 0);
                            irgen_store_pointer(builder, this_val, KrtIrImm(builder, offset >= 0 ? offset : 0), rhs);
                            return rhs;
                        }
                        if (objexpr->type == AST_IDENTIFIER) {
                            const char* class_name = objexpr->data.identifier_name;
                            char* mangled = KrtIrMangleStaticMember(class_name, member_name);
                            if (mangled) {
                                KrtIrStore(builder, mangled, rhs);
                                IRGEN_FREE(mangled);
                                return rhs;
                            }
                        }
                        if (expr->data.binary_op.left->data.member_access.resolved_class_name) {
                            int offset = KrtIrLayoutGetOffset(builder, expr->data.binary_op.left->data.member_access.resolved_class_name, member_name);
                            KrtIRValue base = KrtIrGenerateExpression(builder, objexpr);
                            irgen_store_pointer(builder, base, KrtIrImm(builder, offset >= 0 ? offset : 0), rhs);
                            return rhs;
                        }
                    }
                    return rhs;
                }
                case TOKEN_PLUS: {
                    bool left_is_string = is_string_expression(builder, expr->data.binary_op.left);
                    bool right_is_string = is_string_expression(builder, expr->data.binary_op.right);
                    if (left_is_string || right_is_string) {
                        KrtIRValue string_lhs = convert_to_string_if_needed(builder, expr->data.binary_op.left, lhs);
                        KrtIRValue string_rhs = convert_to_string_if_needed(builder, expr->data.binary_op.right, rhs);
                        return KrtIrStrcat(builder, string_lhs, string_rhs);
                    }
                    return KrtIrAdd(builder, lhs, rhs);
                }
                case TOKEN_MINUS:
                    return KrtIrSub(builder, lhs, rhs);
                case TOKEN_MULTIPLY:
                    return KrtIrMul(builder, lhs, rhs);
                case TOKEN_DIVIDE:
                    return KrtIrDiv(builder, lhs, rhs);
                case TOKEN_MODULO:
                    return KrtIrMod(builder, lhs, rhs);
                case TOKEN_BITWISE_AND:
                    return KrtIrAnd(builder, lhs, rhs);
                case TOKEN_BITWISE_OR:
                    return KrtIrOr(builder, lhs, rhs);
                case TOKEN_BITWISE_XOR:
                    return KrtIrXor(builder, lhs, rhs);
                case TOKEN_LSHIFT:
                    return KrtIrLshift(builder, lhs, rhs);
                case TOKEN_RSHIFT:
                    return KrtIrRshift(builder, lhs, rhs);
                case TOKEN_POWER:
                    return KrtIrPow(builder, lhs, rhs);
                case TOKEN_LESS:
                    return KrtIrCompare(builder, KRT_IR_LT, lhs, rhs);
                case TOKEN_GREATER:
                    return KrtIrCompare(builder, KRT_IR_GT, lhs, rhs);
                case TOKEN_EQUAL:
                    return KrtIrCompare(builder, KRT_IR_EQ, lhs, rhs);
                case TOKEN_LESS_EQUAL:
                    return KrtIrCompare(builder, KRT_IR_LE, lhs, rhs);
                case TOKEN_GREATER_EQUAL:
                    return KrtIrCompare(builder, KRT_IR_GE, lhs, rhs);
                case TOKEN_NOT_EQUAL:
                    return KrtIrCompare(builder, KRT_IR_NE, lhs, rhs);
                case TOKEN_AND: {
                    static int and_label_id = 0;
                    char rhs_label[IRGEN_LABEL_BUFFER_SIZE], end_label[IRGEN_LABEL_BUFFER_SIZE];
                    snprintf(rhs_label, sizeof(rhs_label), "and_rhs_%d", and_label_id);
                    snprintf(end_label, sizeof(end_label), "and_end_%d", and_label_id++);
                    
                    KrtIRBasicBlock* rhs_block = KrtIrBlockCreate(builder, rhs_label);
                    KrtIRBasicBlock* end_block = KrtIrBlockCreate(builder, end_label);
                    KrtIRBasicBlock* current_block = builder->current_block;
                    
                    if (current_block) {
                        KrtIrBranch(builder, lhs, rhs_block, end_block);
                        current_block->next = rhs_block;
                    }
                    rhs_block->next = end_block;
                    
                    KrtIrBlockSetCurrent(builder, rhs_block);
                    KrtIrJump(builder, end_block);
                    
                    KrtIrBlockSetCurrent(builder, end_block);
                    KrtIRValue phi_values[2] = { KRT_IMM_FALSE(builder), rhs };
                    KrtIRBasicBlock* phi_blocks[2] = { current_block, rhs_block };
                    return KrtIrPhi(builder, phi_values, phi_blocks, 2);
                }
                case TOKEN_OR: {
                    static int or_label_id = 0;
                    char rhs_label[IRGEN_LABEL_BUFFER_SIZE], end_label[IRGEN_LABEL_BUFFER_SIZE];
                    snprintf(rhs_label, sizeof(rhs_label), "or_rhs_%d", or_label_id);
                    snprintf(end_label, sizeof(end_label), "or_end_%d", or_label_id++);
                    
                    KrtIRBasicBlock* rhs_block = KrtIrBlockCreate(builder, rhs_label);
                    KrtIRBasicBlock* end_block = KrtIrBlockCreate(builder, end_label);
                    KrtIRBasicBlock* current_block = builder->current_block;
                    
                    if (current_block) {
                        KrtIrBranch(builder, lhs, end_block, rhs_block);
                        current_block->next = rhs_block;
                    }
                    rhs_block->next = end_block;
                    
                    KrtIrBlockSetCurrent(builder, rhs_block);
                    KrtIrJump(builder, end_block);
                    
                    KrtIrBlockSetCurrent(builder, end_block);
                    KrtIRValue phi_values[2] = { KRT_IMM_TRUE(builder), rhs };
                    KrtIRBasicBlock* phi_blocks[2] = { current_block, rhs_block };
                    return KrtIrPhi(builder, phi_values, phi_blocks, 2);
                }
                case TOKEN_NULL_COALESCING:
                    return irgen_null_coalesce(builder, lhs, rhs);
                default:
                    return lhs;
            }
        }

        case AST_UNARY_OPERATION: {
            KrtIRValue operand = KrtIrGenerateExpression(builder, expr->data.unary_op.operand);
            switch (expr->data.unary_op.operator) {
                case TOKEN_MINUS: return KrtIrSub(builder, KRT_IMM_ZERO(builder), operand);
                case TOKEN_NOT: return KrtIrCompare(builder, KRT_IR_EQ, operand, KRT_IMM_ZERO(builder));
                default: return operand;
            }
        }

        case AST_MEMBER_ACCESS: {
            fprintf(stderr, "[IrGen] MEMBER_ACCESS: object_type=%d, member=%s\n",
                    expr->data.member_access.object ? (int)expr->data.member_access.object->type : -1,
                    expr->data.member_access.member_name);
            if (expr->data.member_access.object &&
                expr->data.member_access.object->type == AST_IDENTIFIER) {
                const char* class_name = expr->data.member_access.object->data.identifier_name;
                const char* member_name = expr->data.member_access.member_name;
                const char* mangled_name = expr->data.member_access.resolved_mangled_name;
                if (mangled_name) {
                    KrtIRGlobal* global = KrtIrModuleFindGlobal(builder->module, mangled_name);
                    if (global) {
                        return KrtIrLoad(builder, mangled_name);
                    }
                }
                char* fallback_mangled = KrtIrMangleStaticMember(class_name, member_name);
                if (fallback_mangled) {
                    KrtIRGlobal* global = KrtIrModuleFindGlobal(builder->module, fallback_mangled);
                    if (global) {
                        KrtIRValue value = KrtIrLoad(builder, fallback_mangled);
                        IRGEN_FREE(fallback_mangled);
                        return value;
                    }
                    IRGEN_FREE(fallback_mangled);
                }
            }

            if (expr->data.member_access.object &&
                expr->data.member_access.object->type == AST_THIS) {
                const char* current_class = KrtIrCurrentClassContext(builder);
                const char* field_name = expr->data.member_access.member_name;
                int offset = KrtIrLayoutGetOffset(builder, current_class, field_name);
                KrtIRValue this_val = KrtIrArg(builder, 0);
                return irgen_load_pointer(builder, this_val, KrtIrImm(builder, offset >= 0 ? offset : 0));
            }

            if (expr->data.member_access.resolved_class_name) {
                const char* field_name = expr->data.member_access.member_name;
                int offset = KrtIrLayoutGetOffset(builder, expr->data.member_access.resolved_class_name, field_name);
                KrtIRValue base = KrtIrGenerateExpression(builder, expr->data.member_access.object);
                return irgen_load_pointer(builder, base, KrtIrImm(builder, offset >= 0 ? offset : 0));
            }

            return void_val_return();
        }

        case AST_CALL: {
            const char* func_name = expr->data.call.name;

            fprintf(stderr, "[IrGen] AST_CALL: func_name=%s, object=%p, object_type=%d\n",
                    func_name,
                    (void*)expr->data.call.object,
                    expr->data.call.object ? (int)expr->data.call.object->type : -1);

            if (!expr->data.call.object &&
                strcmp(func_name, irgen_get_builtin_func(IRGEN_FUNC_MALLOC)) == 0 &&
                expr->data.call.argument_count == 1) {
                KrtIRValue size = KrtIrGenerateExpression(builder, expr->data.call.arguments[0]);
                return irgen_allocate_memory(builder, size);
            }

            if (!expr->data.call.object &&
                strcmp(func_name, irgen_get_builtin_func(IRGEN_FUNC_FREE)) == 0 &&
                expr->data.call.argument_count == 1) {
                KrtIRValue ptr = KrtIrGenerateExpression(builder, expr->data.call.arguments[0]);
                irgen_free_memory(builder, ptr);
                return void_val_return();
            }

            if (expr->data.call.object) {
                if (expr->data.call.object->type == AST_IDENTIFIER) {
                    const char* class_name = expr->data.call.object->data.identifier_name;

                    KrtIRValue* args = NULL;
                    if (expr->data.call.argument_count > 0) {
                        args = IRGEN_ALLOC_ARGS(expr->data.call.argument_count);
                        IRGEN_CHECK_ALLOC(args, void_val_return());
                        for (int i = 0; i < expr->data.call.argument_count; i++) {
                            IRGEN_SET_ARG(args, i, KrtIrGenerateExpression(builder, expr->data.call.arguments[i]));
                        }
                    }

                    char* fallback_mangled = NULL;
                    const char* call_func_name = expr->data.call.resolved_mangled_name;
                    if (!call_func_name) {
                        const char* ns_path[2] = { class_name, NULL };
                        fallback_mangled = KrtIrMangleCallFunctionName(
                            builder, ns_path, func_name, expr->data.call.arguments,
                            expr->data.call.argument_count);
                        call_func_name = fallback_mangled ? fallback_mangled : func_name;
                    }

                    fprintf(stderr, "[IrGen] Static call: %s -> %s\n", func_name, call_func_name);
                    KrtIRValue result;
                    if (irgen_is_syscall_method(class_name, func_name) &&
                        expr->data.call.argument_count > 0) {
                        result = KrtIrSyscall(builder, args[0], args + 1,
                                              expr->data.call.argument_count - 1);
                    } else {
                        result = KrtIrCall(builder, call_func_name, args,
                                           expr->data.call.argument_count);
                    }
                    IRGEN_SAFE_FREE(args);
                    IRGEN_SAFE_FREE(fallback_mangled);
                    return result;
                }
            }

            if (strcmp(func_name, "delete") == 0 && expr->data.call.object && expr->data.call.object->type == AST_IDENTIFIER) {
                const char* class_name = expr->data.call.object->data.identifier_name;
                char* dtor_name = KrtIrMangleStaticMember(class_name, "destructor");
                if (dtor_name) {
                    if (expr->data.call.argument_count > 0) {
                        KrtIRValue obj = KrtIrGenerateExpression(builder, expr->data.call.arguments[0]);
                        irgen_free_memory(builder, obj);
                    }
                    IRGEN_FREE(dtor_name);
                }
                return void_val_return();
            }

            char* call_mangled = NULL;
            const char* call_func_name = expr->data.call.resolved_mangled_name;
            if (!call_func_name) {
                KrtTokenType* param_types = NULL;
                int arg_count = expr->data.call.argument_count;
                if (arg_count > 0) {
                    param_types = (KrtTokenType*)KRT_MALLOC(arg_count * sizeof(KrtTokenType));
                    if (param_types) {
                        KrtIrFillParamTypesForMangle(builder, expr->data.call.arguments,
                                                     arg_count, param_types);
                    }
                }

                call_mangled = name_mangle_function(NULL, func_name, param_types, arg_count);
                if (param_types) KRT_FREE(param_types);
                call_func_name = call_mangled ? call_mangled : func_name;
            }

            KrtIRValue* args = NULL;
            if (expr->data.call.argument_count > 0) {
                args = IRGEN_ALLOC_ARGS(expr->data.call.argument_count);
                IRGEN_CHECK_ALLOC(args, void_val_return());
                for (int i = 0; i < expr->data.call.argument_count; i++) {
                    IRGEN_SET_ARG(args, i, KrtIrGenerateExpression(builder, expr->data.call.arguments[i]));
                }
            }
            
            KrtIRValue result;
            if (strcmp(func_name, "syscall") == 0 &&
                expr->data.call.argument_count > 0) {
                result = KrtIrSyscall(builder, args[0], args + 1,
                                      expr->data.call.argument_count - 1);
            } else {
                result = KrtIrCall(builder, call_func_name, args, expr->data.call.argument_count);
            }
            IRGEN_SAFE_FREE(args);
            IRGEN_SAFE_FREE(call_mangled);
            return result;
        }

        case AST_STATIC_METHOD_CALL: {
            const char* class_name = expr->data.static_call.class_name;
            const char* method_name = expr->data.static_call.method_name;

            fprintf(stderr, "[IrGen] AST_STATIC_METHOD_CALL expr: %s.%s\n", class_name, method_name);

            char* mangled = NULL;
            const char* call_func_name = expr->data.static_call.resolved_mangled_name;
            if (!call_func_name) {
                const char* ns_path[2] = { class_name, NULL };
                mangled = KrtIrMangleCallFunctionName(builder, ns_path, method_name,
                                                      expr->data.static_call.arguments,
                                                      expr->data.static_call.argument_count);
                call_func_name = mangled ? mangled : method_name;
            }

            KrtIRValue* args = NULL;
            if (expr->data.static_call.argument_count > 0) {
                args = IRGEN_ALLOC_ARGS(expr->data.static_call.argument_count);
                IRGEN_CHECK_ALLOC(args, void_val_return());
                for (int i = 0; i < expr->data.static_call.argument_count; i++) {
                    IRGEN_SET_ARG(args, i, KrtIrGenerateExpression(builder, expr->data.static_call.arguments[i]));
                }
            }

            KrtIRValue result;
            if (irgen_is_syscall_method(class_name, method_name) &&
                expr->data.static_call.argument_count > 0) {
                result = KrtIrSyscall(builder, args[0], args + 1,
                                      expr->data.static_call.argument_count - 1);
            } else {
                result = KrtIrCall(builder, call_func_name, args,
                                   expr->data.static_call.argument_count);
            }
            IRGEN_SAFE_FREE(args);
            IRGEN_SAFE_FREE(mangled);
            return result;
        }

        case AST_TERNARY_OPERATION: {
            KrtIRValue cond = KrtIrGenerateExpression(builder, expr->data.ternary_op.condition);

            char true_label[32], false_label[32], end_label[32];
            snprintf(true_label, sizeof(true_label), "ternary_true_%d", builder->label_counter++);
            snprintf(false_label, sizeof(false_label), "ternary_false_%d", builder->label_counter++);
            snprintf(end_label, sizeof(end_label), "ternary_end_%d", builder->label_counter++);

            KrtIRBasicBlock* true_block = KrtIrBlockCreate(builder, true_label);
            KrtIRBasicBlock* false_block = KrtIrBlockCreate(builder, false_label);
            KrtIRBasicBlock* end_block = KrtIrBlockCreate(builder, end_label);
            KrtIRBasicBlock* saved_current_block = builder->current_block;

            KrtIrBranch(builder, cond, true_block, false_block);
            if (saved_current_block) saved_current_block->next = true_block;
            true_block->next = false_block;
            false_block->next = end_block;

            KrtIrBlockSetCurrent(builder, true_block);
            KrtIRValue true_value = KrtIrGenerateExpression(builder, expr->data.ternary_op.true_value);
            KrtIrJump(builder, end_block);

            KrtIrBlockSetCurrent(builder, false_block);
            KrtIRValue false_value = KrtIrGenerateExpression(builder, expr->data.ternary_op.false_value);
            KrtIrJump(builder, end_block);

            KrtIrBlockSetCurrent(builder, end_block);
            KrtIRValue phi_values[2] = { true_value, false_value };
            KrtIRBasicBlock* phi_blocks[2] = { true_block, false_block };
            return KrtIrPhi(builder, phi_values, phi_blocks, 2);
        }

        case AST_ARRAY_LITERAL: {
            int element_count = expr->data.array_literal.element_count;
            if (element_count == 0) return irgen_allocate_memory(builder, KRT_IMM_PTR_SIZE(builder));

            KrtIRValue total_size = KrtIrMul(builder, KrtIrImm(builder, element_count), KRT_IMM_PTR_SIZE(builder));
            KrtIRValue array_ptr = irgen_allocate_memory(builder, total_size);

            for (int i = 0; i < element_count; i++) {
                KrtIRValue elem_val = KrtIrGenerateExpression(builder, expr->data.array_literal.elements[i]);
                KrtIrArrayStore(builder, array_ptr, KrtIrImm(builder, i), elem_val);
            }
            return array_ptr;
        }

        case AST_ARRAY_ACCESS: {
            int element_size = irgen_array_element_size(builder, expr->data.array_access.array);
            KrtIRValue base = KrtIrGenerateExpression(builder, expr->data.array_access.array);
            KrtIRValue index = KrtIrGenerateExpression(builder, expr->data.array_access.index);
            KrtIRValue offset = index;
            if (element_size != 1) {
                offset = KrtIrMul(builder, index, KrtIrImm(builder, element_size));
            }
            KrtIRValue address = KrtIrAdd(builder, base, offset);
            return KrtIrLoadPtrSized(builder, address, 0, element_size);
        }

        case AST_LAMBDA_EXPRESSION: {
            static int lambda_counter = 0;
            char lambda_name[256];
            snprintf(lambda_name, sizeof(lambda_name), "__lambda_%d", lambda_counter++);

            int param_count = expr->data.lambda_expr.parameter_count;
            KrtIRParam* params = NULL;
            if (param_count > 0) {
                params = (KrtIRParam*)KRT_MALLOC(param_count * sizeof(KrtIRParam));
                IRGEN_CHECK_ALLOC(params, void_val_return());
                for (int i = 0; i < param_count; i++) {
                    params[i].name = KRT_STRDUP(expr->data.lambda_expr.parameters[i]);
                    params[i].type = TOKEN_INT32;
                }
            }

            KrtIRFunction* lambda_func = KrtIrFunctionCreate(builder, lambda_name, params, param_count, TOKEN_INT32);
            if (params) { for (int i = 0; i < param_count; i++) KRT_FREE(params[i].name); IRGEN_FREE(params); }
            if (!lambda_func) return void_val_return();

            KrtIRFunction* saved_func = builder->current_function;
            KrtIRBasicBlock* saved_block = builder->current_block;
            builder->current_function = lambda_func;
            KrtIRBasicBlock* lambda_entry = KrtIrBlockCreate(builder, "entry");
            KrtIrBlockSetCurrent(builder, lambda_entry);

            if (expr->data.lambda_expr.expression) {
                KrtIrReturn(builder, KrtIrGenerateExpression(builder, expr->data.lambda_expr.expression));
            } else if (expr->data.lambda_expr.body) {
                KrtIrGenerateStatement(builder, expr->data.lambda_expr.body);
                if (!KrtIrCheckHasReturn(expr->data.lambda_expr.body)) KrtIrReturn(builder, KRT_IMM_ZERO(builder));
            }

            builder->current_function = saved_func;
            builder->current_block = saved_block;
            return KRT_IMM_ZERO(builder);
        }

        case AST_LINQ_QUERY: {
            ASTNode* from_clause = expr->data.linq_query.from_clause;
            if (!from_clause || from_clause->type != AST_LINQ_FROM) return void_val_return();
            
            KrtIRValue source = KrtIrGenerateExpression(builder, from_clause->data.linq_from.source);
            
            for (int i = 0; i < expr->data.linq_query.clause_count; i++) {
                ASTNode* clause = expr->data.linq_query.clauses[i];
                if (!clause) continue;
                
                switch (clause->type) {
                    case AST_LINQ_WHERE:
                        source = irgen_linq_where(builder, source, KRT_IMM_ZERO(builder)); break;
                    case AST_LINQ_ORDERBY:
                        source = irgen_linq_orderby(builder, source, clause->data.linq_orderby.ascending ? 1 : 0); break;
                    case AST_LINQ_LET: {
                        KrtIRValue val = KrtIrGenerateExpression(builder, clause->data.linq_let.expression);
                        char var[64]; snprintf(var, sizeof(var), "__let_%s", clause->data.linq_let.var_name);
                        KrtIrAlloc(builder, var); KrtIrStore(builder, var, val);
                        break;
                    }
                    case AST_LINQ_GROUP:
                        source = irgen_linq_groupby(builder, source,
                            KrtIrGenerateExpression(builder, clause->data.linq_group.key_expression),
                            KrtIrGenerateExpression(builder, clause->data.linq_group.element_expression),
                            clause->data.linq_group.into_var_name);
                        break;
                    default: break;
                }
            }
            
            if (expr->data.linq_query.select_clause && expr->data.linq_query.select_clause->type == AST_LINQ_SELECT) {
                KrtIRValue sel = expr->data.linq_query.select_clause->data.linq_select.expression ?
                    KrtIrGenerateExpression(builder, expr->data.linq_query.select_clause->data.linq_select.expression) :
                    KRT_IMM_ZERO(builder);
                source = irgen_linq_select(builder, source, sel);
            }
            return source;
        }

        case AST_UNSAFE_CALL: {
            if (expr->data.unsafe_call.is_block) {
                KrtIrGenerateStatement(builder, expr->data.unsafe_call.expression);
                return void_val_return();
            }
            return KrtIrGenerateExpression(builder, expr->data.unsafe_call.expression);
        }

        case AST_DEFAULT_EXPRESSION: return KRT_IMM_ZERO(builder);

        case AST_IS_EXPRESSION:
            return irgen_is_instance(builder,
                KrtIrGenerateExpression(builder, expr->data.is_expr.expression),
                expr->data.is_expr.type_name);

        case AST_AS_EXPRESSION:
            return irgen_as_instance(builder,
                KrtIrGenerateExpression(builder, expr->data.as_expr.expression),
                expr->data.as_expr.type_name);

        case AST_SIZEOF_EXPRESSION:
            return irgen_size_of_pointer(builder);

        case AST_INTERPOLATED_STRING: {
            int part_count = expr->data.interpolated_string.part_count;
            int expr_count = expr->data.interpolated_string.expression_count;
            if (part_count == 0 && expr_count == 0) return KrtIrStringConst(builder, "");
            
            KrtIRValue result = (part_count > 0 && expr->data.interpolated_string.string_parts[0]) ?
                KrtIrStringConst(builder, expr->data.interpolated_string.string_parts[0]) :
                KrtIrStringConst(builder, "");
            
            int part_idx = (part_count > 0) ? 1 : 0, expr_idx = 0;
            
            while (expr_idx < expr_count || part_idx < part_count) {
                if (expr_idx < expr_count && expr->data.interpolated_string.expressions[expr_idx]) {
                    KrtIRValue val = KrtIrGenerateExpression(builder, expr->data.interpolated_string.expressions[expr_idx]);
                    result = irgen_concat_strings(builder, result, irgen_int32_to_string(builder, val));
                    expr_idx++;
                }
                if (part_idx < part_count && expr->data.interpolated_string.string_parts[part_idx]) {
                    result = irgen_concat_strings(builder, result, KrtIrStringConst(builder, expr->data.interpolated_string.string_parts[part_idx]));
                    part_idx++;
                }
            }
            return result;
        }

        case AST_TUPLE_EXPRESSION: {
            int count = expr->data.tuple_expr.element_count;
            KrtIRValue tuple_ptr = irgen_allocate_memory(builder, KrtIrImm(builder, count * IRGEN_POINTER_SIZE));
            for (int i = 0; i < count; i++) {
                irgen_store_pointer(builder, tuple_ptr, KrtIrImm(builder, i * IRGEN_POINTER_SIZE),
                    KrtIrGenerateExpression(builder, expr->data.tuple_expr.elements[i]));
            }
            return tuple_ptr;
        }

        case AST_TUPLE_ELEMENT_ACCESS:
            return irgen_load_pointer(builder,
                KrtIrGenerateExpression(builder, expr->data.tuple_element_access.tuple),
                KrtIrImm(builder, expr->data.tuple_element_access.index * IRGEN_POINTER_SIZE));

        case AST_CAST_EXPRESSION: {
            KrtIRValue val = KrtIrGenerateExpression(builder, expr->data.cast_expr.expression);
            switch (expr->data.cast_expr.target_type) {
                case TOKEN_INT32: case TOKEN_INT64: case TOKEN_FLOAT32:
                case TOKEN_FLOAT64: case TOKEN_BOOL: case TOKEN_TYPE_STRING:
                    return KrtIrCast(builder, val, expr->data.cast_expr.target_type);
                default: return val;
            }
        }

        case AST_DELEGATE_DECLARATION:
        case AST_DELEGATE_TYPE:
            return KRT_IMM_ZERO(builder);

        case AST_POINTER_TYPE: return irgen_size_of_pointer(builder);

        case AST_POINTER_DEREFERENCE:
            return irgen_load_pointer(builder,
                KrtIrGenerateExpression(builder, expr->data.pointer_deref.pointer),
                KRT_IMM_ZERO(builder));

        case AST_ADDRESS_OF: {
            if (expr->data.address_of.operand && expr->data.address_of.operand->type == AST_IDENTIFIER)
                return irgen_get_variable_address(builder, expr->data.address_of.operand->data.identifier_name);
            return KRT_IMM_ZERO(builder);
        }

        case AST_STACKALLOC_EXPRESSION: {
            int elem_size = IRGEN_POINTER_SIZE;
            switch (expr->data.stackalloc_expr.type_token) {
                case TOKEN_INT8: case TOKEN_UINT8: elem_size = 1; break;
                case TOKEN_INT16: case TOKEN_UINT16: elem_size = 2; break;
                case TOKEN_INT32: case TOKEN_UINT32: case TOKEN_FLOAT32: elem_size = 4; break;
                default: break;
            }
            return irgen_stack_alloc(builder,
                KrtIrMul(builder, KrtIrGenerateExpression(builder, expr->data.stackalloc_expr.count_expr),
                          KrtIrImm(builder, elem_size)));
        }

        case AST_AWAIT_EXPRESSION:
            return irgen_await_task(builder, KrtIrGenerateExpression(builder, expr->data.await_expr.expression));

        case AST_NULL_COALESCING:
            return irgen_null_coalesce(builder,
                KrtIrGenerateExpression(builder, expr->data.null_coalescing.left),
                KrtIrGenerateExpression(builder, expr->data.null_coalescing.right));

        case AST_NULL_CONDITIONAL:
            return irgen_null_conditional(builder,
                KrtIrGenerateExpression(builder, expr->data.null_conditional.expression),
                expr->data.null_conditional.member_name);

        default:
            return void_val_return();
    }
}

static KrtIRValue void_val_return(void) {
    KrtIRValue v = {0};
    v.type = KRT_IR_VALUE_VOID;
    return v;
}

static void KrtIrGenerateStatement(KrtIRBuilder* builder, ASTNode* stmt) {
    if (!stmt) return;

    switch (stmt->type) {
        case AST_ASSIGNMENT: {
            KrtIRValue value = KrtIrGenerateExpression(builder, stmt->data.assignment.value);
            const char* target_name = stmt->data.assignment.name;
            const char* current_class = KrtIrCurrentClassContext(builder);
            int stored = 0;
            
            if (current_class && target_name) {
                int field_offset = KrtIrLayoutGetOffset(builder, current_class, target_name);
                if (field_offset >= 0) {
                    irgen_store_pointer(builder, KrtIrArg(builder, 0), KrtIrImm(builder, field_offset), value);
                    stored = 1;
                }
            }
            
            if (!stored && current_class && target_name) {
                char* mangled = KrtIrMangleStaticMember(current_class, target_name);
                if (mangled) {
                    KrtIRGlobal* global = KrtIrModuleFindGlobal(builder->module, mangled);
                    if (global) { KrtIrStore(builder, mangled, value); stored = 1; }
                    IRGEN_FREE(mangled);
                }
            }
            
            if (!stored) KrtIrStore(builder, target_name, value);
            break;
        }

        case AST_COMPOUND_ASSIGNMENT: {
            KrtIRValue current_val = KrtIrLoad(builder, stmt->data.compound_assignment.name);
            KrtIRValue rhs = KrtIrGenerateExpression(builder, stmt->data.compound_assignment.value);
            KrtIRValue result;
            
            switch (stmt->data.compound_assignment.operator) {
                case TOKEN_PLUS_ASSIGN: result = KrtIrAdd(builder, current_val, rhs); break;
                case TOKEN_MINUS_ASSIGN: result = KrtIrSub(builder, current_val, rhs); break;
                case TOKEN_MUL_ASSIGN: result = KrtIrMul(builder, current_val, rhs); break;
                case TOKEN_DIV_ASSIGN: result = KrtIrDiv(builder, current_val, rhs); break;
                default: result = rhs; break;
            }
            KrtIrStore(builder, stmt->data.compound_assignment.name, result);
            break;
        }

        case AST_ARRAY_ASSIGNMENT: {
            int element_size = irgen_array_element_size(builder, stmt->data.array_assignment.array);
            KrtIrArrayStoreSized(builder,
                KrtIrGenerateExpression(builder, stmt->data.array_assignment.array),
                KrtIrGenerateExpression(builder, stmt->data.array_assignment.index),
                KrtIrGenerateExpression(builder, stmt->data.array_assignment.value),
                element_size);
            break;
        }

        case AST_RETURN_STATEMENT:
            KrtIrReturn(builder, KrtIrGenerateExpression(builder, stmt->data.return_stmt.value));
            break;

        case AST_IF_STATEMENT: {
            KrtIRValue cond = KrtIrGenerateExpression(builder, stmt->data.if_stmt.condition);
            char labels[3][32];
            for (int i = 0; i < 3; i++) snprintf(labels[i], sizeof(labels[i]), "%s_%d",
                i == 0 ? "if_true" : i == 1 ? "if_false" : "if_end", builder->label_counter++);

            KrtIRBasicBlock* blocks[3] = {
                KrtIrBlockCreate(builder, labels[0]),
                KrtIrBlockCreate(builder, labels[1]),
                KrtIrBlockCreate(builder, labels[2])
            };

            KrtIrBranch(builder, cond, blocks[0], blocks[1]);

            KrtIrBlockSetCurrent(builder, blocks[0]);
            KrtIrGenerateStatement(builder, stmt->data.if_stmt.then_branch);
            if (!KrtIrCheckHasReturn(stmt->data.if_stmt.then_branch)) KrtIrJump(builder, blocks[2]);

            KrtIrBlockSetCurrent(builder, blocks[1]);
            if (stmt->data.if_stmt.else_branch) {
                KrtIrGenerateStatement(builder, stmt->data.if_stmt.else_branch);
                if (!KrtIrCheckHasReturn(stmt->data.if_stmt.else_branch)) KrtIrJump(builder, blocks[2]);
            } else {
                KrtIrJump(builder, blocks[2]);
            }

            KrtIrBlockSetCurrent(builder, blocks[2]);
            break;
        }

        case AST_WHILE_STATEMENT: {
            char labels[3][32];
            for (int i = 0; i < 3; i++) snprintf(labels[i], sizeof(labels[i]), "while_%s_%d",
                i == 0 ? "cond" : i == 1 ? "body" : "end", builder->label_counter++);

            KrtIRBasicBlock* blocks[3] = {
                KrtIrBlockCreate(builder, labels[0]),
                KrtIrBlockCreate(builder, labels[1]),
                KrtIrBlockCreate(builder, labels[2])
            };

            KrtIrPushLoopContext(builder, blocks[0], blocks[2]);
            KrtIrJump(builder, blocks[0]);

            KrtIrBlockSetCurrent(builder, blocks[0]);
            KrtIrBranch(builder, KrtIrGenerateExpression(builder, stmt->data.while_stmt.condition), blocks[1], blocks[2]);

            KrtIrBlockSetCurrent(builder, blocks[1]);
            KrtIrGenerateStatement(builder, stmt->data.while_stmt.body);
            KrtIrJump(builder, blocks[0]);

            KrtIrBlockSetCurrent(builder, blocks[2]);
            KrtIrPopLoopContext(builder);
            break;
        }

        case AST_FOR_STATEMENT: {
            if (stmt->data.for_stmt.init) {
                switch (stmt->data.for_stmt.init->type) {
                    case AST_ASSIGNMENT: case AST_COMPOUND_ASSIGNMENT: case AST_ARRAY_ASSIGNMENT:
                    case AST_VARIABLE_DECLARATION: case AST_BLOCK:
                        KrtIrGenerateStatement(builder, stmt->data.for_stmt.init);
                        break;
                    default:
                        KrtIrGenerateExpression(builder, stmt->data.for_stmt.init);
                        break;
                }
            }

            char labels[4][32];
            for (int i = 0; i < 4; i++) snprintf(labels[i], sizeof(labels[i]), "for_%s_%d",
                i == 0 ? "cond" : i == 1 ? "body" : i == 2 ? "incr" : "end", builder->label_counter++);

            KrtIRBasicBlock* blocks[4] = {
                KrtIrBlockCreate(builder, labels[0]),
                KrtIrBlockCreate(builder, labels[1]),
                KrtIrBlockCreate(builder, labels[2]),
                KrtIrBlockCreate(builder, labels[3])
            };

            KrtIrPushLoopContext(builder, blocks[2], blocks[3]);
            KrtIrJump(builder, blocks[0]);

            KrtIrBlockSetCurrent(builder, blocks[0]);
            KrtIRValue cond_val = stmt->data.for_stmt.condition ?
                KrtIrGenerateExpression(builder, stmt->data.for_stmt.condition) : KRT_IMM_ONE(builder);
            if (cond_val.type == KRT_IR_VALUE_VOID) cond_val = KRT_IMM_ONE(builder);
            KrtIrBranch(builder, cond_val, blocks[1], blocks[3]);

            KrtIrBlockSetCurrent(builder, blocks[1]);
            KrtIrGenerateStatement(builder, stmt->data.for_stmt.body);
            KrtIrJump(builder, blocks[2]);

            KrtIrBlockSetCurrent(builder, blocks[2]);
            if (stmt->data.for_stmt.increment) {
                switch (stmt->data.for_stmt.increment->type) {
                    case AST_ASSIGNMENT: case AST_COMPOUND_ASSIGNMENT: case AST_ARRAY_ASSIGNMENT:
                    case AST_VARIABLE_DECLARATION: case AST_BLOCK:
                        KrtIrGenerateStatement(builder, stmt->data.for_stmt.increment);
                        break;
                    default:
                        KrtIrGenerateExpression(builder, stmt->data.for_stmt.increment);
                        break;
                }
            }
            KrtIrJump(builder, blocks[0]);

            KrtIrBlockSetCurrent(builder, blocks[3]);
            KrtIrPopLoopContext(builder);
            break;
        }

        case AST_FOREACH_STATEMENT: {
            char* var_name = stmt->data.foreach_stmt.var_name;
            KrtIRValue iterable_value = KrtIrGenerateExpression(builder, stmt->data.foreach_stmt.iterable);

            char index_var[256];
            snprintf(index_var, sizeof(index_var), "%s__idx", var_name);
            KrtIrAlloc(builder, index_var);
            KrtIrStore(builder, index_var, KRT_IMM_ZERO(builder));

            char loop_labels[3][32];
            for (int i = 0; i < 3; i++) snprintf(loop_labels[i], sizeof(loop_labels[i]), "foreach_%s_%d",
                i == 0 ? "cond" : i == 1 ? "body" : "end", builder->label_counter++);

            KrtIRBasicBlock* loop_blocks[3] = {
                KrtIrBlockCreate(builder, loop_labels[0]),
                KrtIrBlockCreate(builder, loop_labels[1]),
                KrtIrBlockCreate(builder, loop_labels[2])
            };

            KrtIrPushLoopContext(builder, loop_blocks[2], loop_blocks[2]);
            KrtIrJump(builder, loop_blocks[0]);

            KrtIrBlockSetCurrent(builder, loop_blocks[0]);
            KrtIRValue index_value = KrtIrLoad(builder, index_var);
            KrtIRValue array_len = irgen_array_size(builder, iterable_value);
            KrtIRValue cond = KrtIrCompare(builder, KRT_IR_LT, index_value, array_len);
            KrtIrBranch(builder, cond, loop_blocks[1], loop_blocks[2]);

            KrtIrBlockSetCurrent(builder, loop_blocks[1]);
            KrtIRValue current_index = KrtIrLoad(builder, index_var);
            KrtIRValue element_ptr = irgen_array_get(builder, iterable_value, current_index);
            KrtIRValue element_value = KrtIrLoadPtr(builder, element_ptr, 0);

            KrtIrAlloc(builder, var_name);
            KrtIrStore(builder, var_name, element_value);

            TypeCheckSymbolTable* foreach_scope = NULL;
            TypeCheckSymbolTable* saved_scope = NULL;
            if (builder->type_context) {
                saved_scope = builder->type_context->current_scope;
                foreach_scope = type_check_symbol_table_create(saved_scope);
                builder->type_context->current_scope = foreach_scope;
                
                TypeCheckSymbol symbol = {0};
                symbol.name = var_name;
                symbol.type = type_create_basic(TYPE_INT64);
                type_check_symbol_table_add(builder->type_context->current_scope, symbol);
            }

            KrtIrGenerateStatement(builder, stmt->data.foreach_stmt.body);

            KrtIrStore(builder, index_var, KrtIrAdd(builder, KrtIrLoad(builder, index_var), KRT_IMM_ONE(builder)));
            KrtIrJump(builder, loop_blocks[0]);

            KrtIrBlockSetCurrent(builder, loop_blocks[2]);

            if (builder->type_context) {
                builder->type_context->current_scope = saved_scope;
                type_check_symbol_table_unref(foreach_scope);
            }

            KrtIrPopLoopContext(builder);
            break;
        }

        case AST_PRINT_STATEMENT:
            KrtIrGeneratePrint(builder, stmt->data.print_stmt.values, stmt->data.print_stmt.value_count, stmt->data.print_stmt.has_newline);
            break;

        case AST_CALL:
            KrtIrGenerateExpression(builder, stmt);
            break;

        case AST_STATIC_METHOD_CALL:
            fprintf(stderr, "[IrGen] Processing AST_STATIC_METHOD_CALL statement\n");
            KrtIrGenerateExpression(builder, stmt);
            fprintf(stderr, "[IrGen] After AST_STATIC_METHOD_CALL: main block first_inst=%p\n",
                    builder->current_block ? (void*)builder->current_block->first_inst : NULL);
            break;

        case AST_BLOCK:
            KrtIrGenerateBlock(builder, stmt);
            break;

        case AST_POINT_BLOCK:
            KrtIrGenerateBlock(builder, stmt->data.point_block.body);
            break;

        case AST_SWITCH_STATEMENT: {
            KrtIRValue cond = KrtIrGenerateExpression(builder, stmt->data.switch_stmt.expression);
            char end_label[32];
            int end_label_id = builder->label_counter++;
            snprintf(end_label, sizeof(end_label), "end_%d", end_label_id);
            KrtIRBasicBlock* end_block = KrtIrBlockCreate(builder, end_label);
            KrtIrPushLoopContext(builder, NULL, end_block);

            char** case_labels = (char**)KRT_MALLOC(stmt->data.switch_stmt.case_count * sizeof(char*));
            KrtIRBasicBlock** case_blocks = (KrtIRBasicBlock**)KRT_MALLOC(stmt->data.switch_stmt.case_count * sizeof(KrtIRBasicBlock*));
            if (case_labels && case_blocks) {
                for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
                    case_labels[i] = (char*)KRT_MALLOC(32);
                    if (case_labels[i]) {
                        int case_label_id = builder->label_counter++;
                        snprintf(case_labels[i], 32, "case_%d_%d", case_label_id, i + 1);
                        case_blocks[i] = KrtIrBlockCreate(builder, case_labels[i]);
                    }
                }
            }

            KrtIRBasicBlock* default_block = NULL;
            char* default_label = NULL;
            if (stmt->data.switch_stmt.default_case) {
                default_label = (char*)KRT_MALLOC(32);
                if (default_label) {
                    int default_label_id = builder->label_counter++;
                    snprintf(default_label, 32, "default_%d", default_label_id);
                    default_block = KrtIrBlockCreate(builder, default_label);
                }
            }

            for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
                KrtIRValue case_value = KrtIrGenerateExpression(builder, stmt->data.switch_stmt.cases[i]->data.case_clause.value);
                KrtIRValue cmp_result = KrtIrCompare(builder, KRT_IR_EQ, cond, case_value);
                KrtIrBranch(builder, cmp_result, case_blocks[i],
                            (i < stmt->data.switch_stmt.case_count - 1) ? case_blocks[i + 1] :
                            (default_block ? default_block : end_block));
            }

            if (stmt->data.switch_stmt.default_case) {
                KrtIrJump(builder, default_block);
            } else if (stmt->data.switch_stmt.case_count > 0) {
                KrtIrJump(builder, end_block);
            }

            for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
                KrtIrBlockSetCurrent(builder, case_blocks[i]);
                KrtIrGenerateStatement(builder, stmt->data.switch_stmt.cases[i]);
                KrtIrJump(builder, end_block);
            }

            if (stmt->data.switch_stmt.default_case) {
                KrtIrBlockSetCurrent(builder, default_block);
                KrtIrGenerateStatement(builder, stmt->data.switch_stmt.default_case);
                KrtIrJump(builder, end_block);
            }

            KrtIrBlockSetCurrent(builder, end_block);
            KrtIrNop(builder);
            KrtIrPopLoopContext(builder);

            if (case_labels) {
                for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
                    IRGEN_FREE(case_labels[i]);
                }
                IRGEN_FREE(case_labels);
            }
            IRGEN_FREE(case_blocks);
            IRGEN_FREE(default_label);
            break;
        }

        case AST_BREAK_STATEMENT: {
            KrtIRBasicBlock* break_block = KrtIrGetCurrentBreakBlock(builder);
            if (break_block) KrtIrJump(builder, break_block);
            break;
        }

        case AST_CONTINUE_STATEMENT: {
            KrtIRBasicBlock* continue_block = KrtIrGetCurrentContinueBlock(builder);
            if (continue_block) KrtIrJump(builder, continue_block);
            break;
        }

        case AST_DELETE_STATEMENT: {
            KrtIRValue obj = KrtIrGenerateExpression(builder, stmt->data.delete_stmt.value);
            const char* class_name = stmt->data.delete_stmt.resolved_class_name;
            if (class_name) {
                char* dtor_name = KrtIrMangleStaticMember(class_name, "destructor");
                if (dtor_name) {
                    bool exists = false;
                    KrtIRFunction* f = builder->module->functions;
                    while (f) { if (strcmp(f->name, dtor_name) == 0) { exists = true; break; } f = f->next; }
                    if (!exists) KrtIrFunctionCreate(builder, dtor_name, NULL, -1, TOKEN_VOID);
                    
                    if (stmt->data.delete_stmt.value) {
                        KrtIRValue* dtor_args = IRGEN_ALLOC_ARGS(1);
                        if (dtor_args) {
                            IRGEN_SET_ARG(dtor_args, 0, obj);
                            KrtIrCall(builder, dtor_name, dtor_args, 1);
                            IRGEN_FREE(dtor_args);
                        }
                    }

                    irgen_free_memory(builder, obj);
                    IRGEN_FREE(dtor_name);
                }
            }
            break;
        }

        case AST_LOCK_STATEMENT: {
            KrtIRValue lock_obj = KrtIrGenerateExpression(builder, stmt->data.lock_stmt.lock_object);
            irgen_monitor_enter(builder, lock_obj);
            KrtIrGenerateStatement(builder, stmt->data.lock_stmt.body);
            irgen_monitor_exit(builder, lock_obj);
            break;
        }

        case AST_FIXED_STATEMENT: {
            KrtIRValue fixed_expr = KrtIrGenerateExpression(builder, stmt->data.fixed_statement.expression);
            const char* var_name = stmt->data.fixed_statement.variable_name;
            if (var_name) { KrtIrAlloc(builder, var_name); KrtIrStore(builder, var_name, fixed_expr); }
            
            irgen_pin_object(builder, fixed_expr);
            KrtIrGenerateStatement(builder, stmt->data.fixed_statement.body);
            irgen_unpin_object(builder, fixed_expr);
            break;
        }

        case AST_USING_STATEMENT: {
            KrtIRValue resource = KrtIrGenerateExpression(builder, stmt->data.using_stmt.resource);
            KrtIrGenerateStatement(builder, stmt->data.using_stmt.body);
            irgen_dispose(builder, resource);
            break;
        }

        case AST_THROW_STATEMENT: {
            if (stmt->data.throw_stmt.is_rethrow) {
                irgen_rethrow_exception(builder);
            } else {
                irgen_throw_exception(builder);
            }
            break;
        }

        case AST_TRY_STATEMENT: {
            if (stmt->data.try_stmt.try_block) KrtIrGenerateBlock(builder, stmt->data.try_stmt.try_block);
            if (stmt->data.try_stmt.catch_clauses) {
                for (int i = 0; i < stmt->data.try_stmt.catch_clause_count; i++) {
                    if (stmt->data.try_stmt.catch_clauses[i] && stmt->data.try_stmt.catch_clauses[i]->data.catch_clause.catch_block)
                        KrtIrGenerateBlock(builder, stmt->data.try_stmt.catch_clauses[i]->data.catch_clause.catch_block);
                }
            }
            if (stmt->data.try_stmt.finally_clause) KrtIrGenerateBlock(builder, stmt->data.try_stmt.finally_clause);
            break;
        }

        case AST_VARIABLE_DECLARATION: {
            if (builder->type_context && builder->type_context->current_scope) {
                TypeCheckSymbol symbol = {0};
                symbol.name = (char*)stmt->data.variable_decl.name;
                Type* element_type = type_create_from_token(stmt->data.variable_decl.type);
                symbol.type = stmt->data.variable_decl.is_array
                    ? type_create_array(element_type, 0)
                    : element_type;
                if (symbol.type) type_check_symbol_table_add(builder->type_context->current_scope, symbol);
            }

            bool is_c_style_array = (stmt->data.variable_decl.array_size != NULL);
            bool is_array_literal = (stmt->data.variable_decl.value &&
                                      stmt->data.variable_decl.value->type == AST_ARRAY_LITERAL);
            (void)is_array_literal;

            if (is_c_style_array) {
                int array_size = 10;
                double size_value;
                if (try_extract_constant(stmt->data.variable_decl.array_size, &size_value))
                    array_size = (size_value > 0) ? (int)size_value : 10;

                Type* element_type = type_create_from_token(stmt->data.variable_decl.type);
                int element_size = element_type ? irgen_type_size(element_type->kind) : IRGEN_POINTER_SIZE;
                type_destroy(element_type);
                int total_bytes = array_size * element_size;
                KrtIrAlloc(builder, stmt->data.variable_decl.name);
                KrtIRValue ptr = irgen_allocate_memory(builder, KrtIrImm(builder, total_bytes));
                KrtIrStore(builder, stmt->data.variable_decl.name, ptr);
            } else {
                KrtIrAlloc(builder, stmt->data.variable_decl.name);

                if (stmt->data.variable_decl.value) {
                    KrtIRValue init_val = KrtIrGenerateExpression(builder, stmt->data.variable_decl.value);
                    KrtIrStore(builder, stmt->data.variable_decl.name, init_val);
                }
            }
            break;
        }

        case AST_STATIC_VARIABLE_DECLARATION: {
            const char* name = stmt->data.static_variable_decl.name;
            const char* current_class = KrtIrCurrentClassContext(builder);
            char* mangled_name = current_class
                ? KrtIrMangleStaticMember(current_class, name)
                : NULL;
            const char* global_name = mangled_name ? mangled_name : name;

            KrtIRGlobal* global = KrtIrModuleFindGlobal(builder->module, global_name);
            if (!global) {
                global = KrtIrModuleAddGlobal(builder, global_name,
                                              stmt->data.static_variable_decl.type);
            }

            if (global && stmt->data.static_variable_decl.value) {
                double value = 0.0;
                if (KrtIrEvaluateNumericConstant(stmt->data.static_variable_decl.value, &value)) {
                    KrtIrModuleSetGlobalNumberInitializer(global, value);
                }
            }

            IRGEN_SAFE_FREE(mangled_name);
            break;
        }

        case AST_FUNCTION_DECLARATION: {
            if (!stmt->data.function_decl.body) break;
            
            KrtIRParam* params = NULL;
            int pc = stmt->data.function_decl.parameter_count;
            if (pc > 0) {
                params = (KrtIRParam*)KRT_MALLOC(pc * sizeof(KrtIRParam));
                if (params) {
                    for (int i = 0; i < pc; i++) {
                        params[i].name = stmt->data.function_decl.parameters[i];
                        params[i].type = stmt->data.function_decl.parameter_types[i];
                    }
                }
            }

            int ns_count = 0;
            const char** ns_path = KrtIrGetNamespacePath(builder, &ns_count);
            char* mangled_name = NULL;

            if (strcmp(stmt->data.function_decl.name, "main") == 0 ||
                strcmp(stmt->data.function_decl.name, "_start") == 0 ||
                strcmp(stmt->data.function_decl.name, "_KrtMainEntry") == 0) {
                mangled_name = NULL;
            } else {
                mangled_name = name_mangle_function(ns_path, stmt->data.function_decl.name,
                                                       stmt->data.function_decl.parameter_types,
                                                       stmt->data.function_decl.parameter_count);
            }
            IRGEN_SAFE_FREE(ns_path);

            const char* func_name = mangled_name ? mangled_name : stmt->data.function_decl.name;
            KrtIRFunction* func = KrtIrFunctionCreate(builder, func_name, params,
                                                       stmt->data.function_decl.parameter_count,
                                                       stmt->data.function_decl.return_type);
            
            if (strcmp(stmt->data.function_decl.name, "main") == 0) {
                builder->module->main_function = func;
            }
            
            KrtIRFunction* saved_func = builder->current_function;
            builder->current_function = func;

            TypeCheckSymbolTable* saved_scope = NULL;
            if (builder->type_context) {
                saved_scope = builder->type_context->current_scope;
                TypeCheckSymbol* func_sym = type_check_symbol_table_lookup(saved_scope, stmt->data.function_decl.name);
                if (func_sym && func_sym->type && func_sym->type->kind == TYPE_FUNCTION &&
                    func_sym->type->data.function.function_scope)
                    builder->type_context->current_scope = func_sym->type->data.function.function_scope;
            }

            KrtIrFunctionSetEntry(builder, func);
            IRGEN_SAFE_FREE(params);

            KrtIRBasicBlock* entry = func->entry_block;
            if (!entry || entry->first_inst || entry->next) {
                entry = KrtIrBlockCreate(builder, "entry");
                KrtIrBlockSetCurrent(builder, entry);
            } else {
                builder->current_block = entry;
            }

            KrtIrGenerateBlock(builder, stmt->data.function_decl.body);
            if (!KrtIrCheckHasReturn(stmt->data.function_decl.body))
                KrtIrReturn(builder, KRT_IMM_ZERO(builder));

            builder->current_function = saved_func;
            if (builder->type_context) builder->type_context->current_scope = saved_scope;
            IRGEN_SAFE_FREE(mangled_name);
            break;
        }

        case AST_CLASS_DECLARATION: {
            if (!stmt->data.class_decl.name) break;

            ASTNode** constraints = stmt->data.class_decl.constraints;
            int constraint_count = stmt->data.class_decl.constraint_count;

            if (constraint_count > 0 && constraints) {
                for (int i = 0; i < constraint_count; i++) {
                    ASTNode* constraint = constraints[i];
                    if (!constraint || constraint->type != AST_GENERIC_CONSTRAINT) continue;

                    const char* param_name = constraint->data.generic_constraint.param_name;
                    const char* constraint_type = constraint->data.generic_constraint.constraint_type;

                    if (param_name && constraint_type) {
                        char check_func_name[128];
                        snprintf(check_func_name, sizeof(check_func_name),
                                 "__check_constraint_%s_%s_%s",
                                 stmt->data.class_decl.name, param_name, constraint_type);

                        KrtIRFunction* check_func = KrtIrFunctionCreate(
                            builder, check_func_name, NULL, 0, TOKEN_VOID);
                        KrtIrFunctionSetEntry(builder, check_func);

                        KrtIRBasicBlock* entry = KrtIrBlockCreate(builder, "entry");
                        KrtIrBlockSetCurrent(builder, entry);
                        KrtIrReturn(builder, KRT_IMM_ZERO(builder));
                    }
                }
            }

            KrtIrPushClassContext(builder, stmt->data.class_decl.name);
            KrtIrRegisterClassLayout(builder, stmt->data.class_decl.name, stmt->data.class_decl.body);

            ASTNode* body = stmt->data.class_decl.body;
            if (body) {
                if (body->type == AST_BLOCK) {
                    for (int i = 0; i < body->data.block.statement_count; i++) {
                        ASTNode* member = body->data.block.statements[i];
                        if (!member) continue;
                        if (member->type == AST_ACCESS_MODIFIER) {
                            member = member->data.access_modifier.member;
                            if (!member) continue;
                        }
                        if (member->type == AST_STATIC_FUNCTION_DECLARATION) {
                            KrtIrGenerateStatement(builder, member);
                        } else if (member->type == AST_CONSTRUCTOR_DECLARATION) {
                            KrtIRParam* params = NULL;
                            int pc = member->data.constructor_decl.parameter_count;
                            if (pc >= 0) {
                                params = (KrtIRParam*)KRT_MALLOC((pc + 1) * sizeof(KrtIRParam));
                                if (params) {
                                    params[0].name = "this";
                                    params[0].type = TOKEN_UINT64;
                                    for (int j = 0; j < pc; j++) {
                                        params[j + 1].name = member->data.constructor_decl.parameters[j];
                                        params[j + 1].type = member->data.constructor_decl.parameter_types[j];
                                    }
                                }
                            }
                            char* mangled = name_mangle_constructor(stmt->data.class_decl.name, pc);
                            KrtIRFunction* func = KrtIrFunctionCreate(builder, mangled, params, pc + 1, TOKEN_VOID);
                            KrtIrFunctionSetEntry(builder, func);
                            IRGEN_FREE(params);

                            KrtIRBasicBlock* entry_block = func->entry_block;
                            if (!entry_block || entry_block->first_inst || entry_block->next) {
                                entry_block = KrtIrBlockCreate(builder, "entry");
                                KrtIrBlockSetCurrent(builder, entry_block);
                            } else {
                                builder->current_block = entry_block;
                            }

                            if (stmt->data.class_decl.base_class &&
                                stmt->data.class_decl.base_class->type == AST_IDENTIFIER &&
                                member->data.constructor_decl.has_base_call) {
                                const char* base_class_name = stmt->data.class_decl.base_class->data.identifier_name;
                                int base_argc = member->data.constructor_decl.base_argument_count;
                                KrtIRValue* cargs = IRGEN_ALLOC_ARGS(base_argc + 1);
                                if (cargs) {
                                    IRGEN_SET_ARG(cargs, 0, KrtIrArg(builder, 0));
                                    for (int j = 0; j < base_argc; j++) {
                                        IRGEN_SET_ARG(cargs, j + 1, KrtIrGenerateExpression(builder, member->data.constructor_decl.base_arguments[j]));
                                    }
                                    char* base_ctor = name_mangle_constructor(base_class_name, base_argc);
                                    if (base_ctor) {
                                        KrtIrCall(builder, base_ctor, cargs, base_argc + 1);
                                        IRGEN_FREE(base_ctor);
                                    }
                                    IRGEN_FREE(cargs);
                                }
                            }

                            KrtIrGenerateBlock(builder, member->data.constructor_decl.body);
                            KrtIrReturn(builder, KRT_IMM_ZERO(builder));
                            IRGEN_FREE(mangled);
                        } else if (member->type == AST_DESTRUCTOR_DECLARATION) {
                            KrtIRParam* params = (KrtIRParam*)KRT_MALLOC(sizeof(KrtIRParam));
                            if (params) {
                                params[0].name = "this";
                                params[0].type = TOKEN_UINT64;
                            }
                            char* mangled = KrtIrMangleStaticMember(stmt->data.class_decl.name, "destructor");
                            KrtIRFunction* func = KrtIrFunctionCreate(builder, mangled, params, 1, TOKEN_VOID);
                            KrtIrFunctionSetEntry(builder, func);
                            IRGEN_FREE(params);

                            KrtIRBasicBlock* entry_block = func->entry_block;
                            if (!entry_block || entry_block->first_inst || entry_block->next) {
                                entry_block = KrtIrBlockCreate(builder, "entry");
                                KrtIrBlockSetCurrent(builder, entry_block);
                            } else {
                                builder->current_block = entry_block;
                            }

                            KrtIrGenerateBlock(builder, member->data.destructor_decl.body);
                            KrtIrReturn(builder, KRT_IMM_ZERO(builder));
                            IRGEN_FREE(mangled);
                        } else if (member->type == AST_FUNCTION_DECLARATION) {
                            KrtIRParam* params = NULL;
                            int pc = member->data.function_decl.parameter_count;
                            if (pc >= 0) {
                                params = (KrtIRParam*)KRT_MALLOC((pc + 1) * sizeof(KrtIRParam));
                                if (params) {
                                    params[0].name = "this";
                                    params[0].type = TOKEN_UINT64;
                                    for (int j = 0; j < pc; j++) {
                                        params[j + 1].name = member->data.function_decl.parameters[j];
                                        params[j + 1].type = member->data.function_decl.parameter_types[j];
                                    }
                                }
                            }
                            const char* base_name = member->data.function_decl.name;
                            char* mangled = KrtIrMangleClassMethodName(stmt->data.class_decl.name, base_name,
                                                                       member->data.function_decl.parameter_types, pc);
                            KrtIRFunction* func = KrtIrFunctionCreate(builder, mangled, params, pc + 1, member->data.function_decl.return_type);
                            KrtIrFunctionSetEntry(builder, func);
                            IRGEN_FREE(params);

                            KrtIRBasicBlock* entry_block = func->entry_block;
                            if (!entry_block || entry_block->first_inst || entry_block->next) {
                                entry_block = KrtIrBlockCreate(builder, "entry");
                                KrtIrBlockSetCurrent(builder, entry_block);
                            } else {
                                builder->current_block = entry_block;
                            }

                            KrtIrGenerateBlock(builder, member->data.function_decl.body);
                            int has_return = KrtIrCheckHasReturn(member->data.function_decl.body);
                            if (!has_return) {
                                KrtIrReturn(builder, KRT_IMM_ZERO(builder));
                            }
                            IRGEN_FREE(mangled);
                        } else if (member->type == AST_STATIC_VARIABLE_DECLARATION) {
                            KrtIrGenerateStatement(builder, member);
                        } else if (member->type == AST_CLASS_DECLARATION) {
                            KrtIrGenerateStatement(builder, member);
                        } else if (member->type == AST_PROPERTY_DECLARATION) {
                            if (member->data.property_decl.is_auto_property) {
                                const char* class_name = stmt->data.class_decl.name;
                                const char* prop_name = member->data.property_decl.name;
                                const char* field_name = member->data.property_decl.backing_field_name;
                                KrtTokenType prop_type = member->data.property_decl.type;

                                if (member->data.property_decl.getter) {
                                    KrtIRParam* params = (KrtIRParam*)KRT_MALLOC(sizeof(KrtIRParam));
                                    if (params) {
                                        params[0].name = "this";
                                        params[0].type = TOKEN_UINT64;

                                        char getter_name[256];
                                        snprintf(getter_name, sizeof(getter_name), "%s_get_%s", class_name, prop_name);

                                        KrtIRFunction* func = KrtIrFunctionCreate(builder, getter_name, params, 1, prop_type);
                                        KrtIrFunctionSetEntry(builder, func);

                                        KrtIRBasicBlock* entry_block = KrtIrBlockCreate(builder, "entry");
                                        KrtIrBlockSetCurrent(builder, entry_block);

                                        if (field_name) {
                                            KrtIRValue field_val = KrtIrLoad(builder, field_name);
                                            KrtIrReturn(builder, field_val);
                                        } else {
                                            KrtIrReturn(builder, KRT_IMM_ZERO(builder));
                                        }

                                        IRGEN_FREE(params);
                                    }
                                }

                                if (member->data.property_decl.setter) {
                                    KrtIRParam* params = (KrtIRParam*)KRT_MALLOC(2 * sizeof(KrtIRParam));
                                    if (params) {
                                        params[0].name = "this";
                                        params[0].type = TOKEN_UINT64;
                                        params[1].name = "value";
                                        params[1].type = prop_type;

                                        char setter_name[256];
                                        snprintf(setter_name, sizeof(setter_name), "%s_set_%s", class_name, prop_name);

                                        KrtIRFunction* func = KrtIrFunctionCreate(builder, setter_name, params, 2, TOKEN_VOID);
                                        KrtIrFunctionSetEntry(builder, func);

                                        KrtIRBasicBlock* entry_block = KrtIrBlockCreate(builder, "entry");
                                        KrtIrBlockSetCurrent(builder, entry_block);

                                        if (field_name) {
                                            KrtIRValue value_val = KrtIrArg(builder, 1);
                                            KrtIrStore(builder, field_name, value_val);
                                        }
                                        KrtIrReturn(builder, KRT_IMM_ZERO(builder));

                                        IRGEN_FREE(params);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    ASTNode* member = body;
                    if (member->type == AST_ACCESS_MODIFIER) {
                        member = member->data.access_modifier.member;
                    }
                    if (member && member->type == AST_STATIC_FUNCTION_DECLARATION) {
                        KrtIrGenerateStatement(builder, member);
                    } else if (member && member->type == AST_FUNCTION_DECLARATION) {

                    } else if (member && member->type == AST_STATIC_VARIABLE_DECLARATION) {
                        KrtIrGenerateStatement(builder, member);
                    } else if (member && member->type == AST_PROPERTY_DECLARATION) {
                        if (member->data.property_decl.is_auto_property) {
                            const char* class_name = stmt->data.class_decl.name;
                            const char* prop_name = member->data.property_decl.name;
                            const char* field_name = member->data.property_decl.backing_field_name;
                            KrtTokenType prop_type = member->data.property_decl.type;

                            if (member->data.property_decl.getter) {
                                KrtIRParam* params = (KrtIRParam*)KRT_MALLOC(sizeof(KrtIRParam));
                                if (params) {
                                    params[0].name = "this";
                                    params[0].type = TOKEN_UINT64;

                                    char getter_name[256];
                                    snprintf(getter_name, sizeof(getter_name), "%s_get_%s", class_name, prop_name);

                                    KrtIRFunction* func = KrtIrFunctionCreate(builder, getter_name, params, 1, prop_type);
                                    KrtIrFunctionSetEntry(builder, func);

                                    KrtIRBasicBlock* entry_block = KrtIrBlockCreate(builder, "entry");
                                    KrtIrBlockSetCurrent(builder, entry_block);

                                    if (field_name) {
                                        KrtIRValue field_val = KrtIrLoad(builder, field_name);
                                        KrtIrReturn(builder, field_val);
                                    } else {
                                        KrtIrReturn(builder, KRT_IMM_ZERO(builder));
                                    }

                                    IRGEN_FREE(params);
                                }
                            }

                            if (member->data.property_decl.setter) {
                                KrtIRParam* params = (KrtIRParam*)KRT_MALLOC(2 * sizeof(KrtIRParam));
                                if (params) {
                                    params[0].name = "this";
                                    params[0].type = TOKEN_UINT64;
                                    params[1].name = "value";
                                    params[1].type = prop_type;

                                    char setter_name[256];
                                    snprintf(setter_name, sizeof(setter_name), "%s_set_%s", class_name, prop_name);

                                    KrtIRFunction* func = KrtIrFunctionCreate(builder, setter_name, params, 2, TOKEN_VOID);
                                    KrtIrFunctionSetEntry(builder, func);

                                    KrtIRBasicBlock* entry_block = KrtIrBlockCreate(builder, "entry");
                                    KrtIrBlockSetCurrent(builder, entry_block);

                                    if (field_name) {
                                        KrtIRValue value_val = KrtIrArg(builder, 1);
                                        KrtIrStore(builder, field_name, value_val);
                                    }
                                    KrtIrReturn(builder, KRT_IMM_ZERO(builder));

                                    IRGEN_FREE(params);
                                }
                            }
                        }
                    }
                }
            }

            KrtIrPopClassContext(builder);
            break;
        }

        case AST_NAMESPACE_DECLARATION: {
            const char* namespace_name = stmt->data.namespace_decl.name;
            if (namespace_name) {
                KrtIrPushNamespaceContext(builder, namespace_name);
                if (stmt->data.namespace_decl.body) {
                    if (stmt->data.namespace_decl.body->type == AST_BLOCK) {
                        for (int i = 0; i < stmt->data.namespace_decl.body->data.block.statement_count; i++) {
                            ASTNode* s = stmt->data.namespace_decl.body->data.block.statements[i];
                            KrtIrGenerateStatement(builder, s);
                        }
                    } else {
                        KrtIrGenerateStatement(builder, stmt->data.namespace_decl.body);
                    }
                }
                KrtIrPopNamespaceContext(builder);
            }
            break;
        }

        case AST_STATIC_FUNCTION_DECLARATION: {
            KrtIRParam* params = NULL;
            int pc = stmt->data.static_function_decl.parameter_count;
            if (pc > 0) {
                params = (KrtIRParam*)KRT_MALLOC(pc * sizeof(KrtIRParam));
                if (params) {
                    for (int i = 0; i < pc; i++) {
                        params[i].name = stmt->data.static_function_decl.parameters[i];
                        params[i].type = stmt->data.static_function_decl.parameter_types[i];
                    }
                }
            }

            const char* current_class = KrtIrCurrentClassContext(builder);
            char* mangled_name = NULL;
            const char* function_name = stmt->data.static_function_decl.name;

            if (current_class) {
                mangled_name = KrtIrMangleClassMethodName(current_class, function_name,
                                                           stmt->data.static_function_decl.parameter_types,
                                                           stmt->data.static_function_decl.parameter_count);
                if (mangled_name) function_name = mangled_name;
            } else {
                KrtTokenType* param_types = NULL;
                int pc = stmt->data.static_function_decl.parameter_count;
                if (pc > 0) {
                    param_types = (KrtTokenType*)KRT_MALLOC(pc * sizeof(KrtTokenType));
                    if (param_types) {
                        for (int i = 0; i < pc; i++) {
                            param_types[i] = stmt->data.static_function_decl.parameter_types[i];
                        }
                    }
                }
                mangled_name = name_mangle_function(NULL, function_name, param_types, pc);
                if (param_types) KRT_FREE(param_types);
                if (mangled_name) function_name = mangled_name;
            }

            KrtIRFunction* func = KrtIrFunctionCreate(builder, function_name, params,
                                                       stmt->data.static_function_decl.parameter_count,
                                                       stmt->data.static_function_decl.return_type);
            KrtIrFunctionSetEntry(builder, func);
            IRGEN_SAFE_FREE(params);

            KrtIRBasicBlock* entry = func->entry_block;
            if (!entry || entry->first_inst || entry->next) {
                entry = KrtIrBlockCreate(builder, "entry");
                KrtIrBlockSetCurrent(builder, entry);
            } else {
                builder->current_block = entry;
            }

            KrtIrGenerateBlock(builder, stmt->data.static_function_decl.body);
            if (!KrtIrCheckHasReturn(stmt->data.static_function_decl.body))
                KrtIrReturn(builder, KRT_IMM_ZERO(builder));
            
            IRGEN_SAFE_FREE(mangled_name);
            break;
        }

        default:
            KrtIrGenerateExpression(builder, stmt);
            return;
    }
}

static int KrtIrCheckHasReturn(ASTNode* node) {
    if (!node) return 0;
    
    switch (node->type) {
        case AST_RETURN_STATEMENT: return 1;
        case AST_BLOCK:
            for (int i = 0; i < node->data.block.statement_count; i++)
                if (KrtIrCheckHasReturn(node->data.block.statements[i])) return 1;
            return 0;
        case AST_POINT_BLOCK:
            return KrtIrCheckHasReturn(node->data.point_block.body);
        case AST_IF_STATEMENT:
            return KrtIrCheckHasReturn(node->data.if_stmt.then_branch) &&
                   (!node->data.if_stmt.else_branch || KrtIrCheckHasReturn(node->data.if_stmt.else_branch));
        case AST_WHILE_STATEMENT:
        case AST_FOR_STATEMENT:
        case AST_FOREACH_STATEMENT:
            return 0;
        default:
            return 0;
    }
}

static void KrtIrGenerateBlock(KrtIRBuilder* builder, ASTNode* block) {
    if (!block || block->type != AST_BLOCK) return;
    fprintf(stderr, "[IrGen] Block: %d statements\n", block->data.block.statement_count);
    for (int i = 0; i < block->data.block.statement_count; i++) {
        ASTNode* stmt = block->data.block.statements[i];
        if (stmt) {
            fprintf(stderr, "[IrGen]   Statement[%d]: type=%d\n", i, (int)stmt->type);
            KrtIrGenerateStatement(builder, stmt);
        }
    }
}

void KrtIrGenerateFromAst(KrtIRBuilder* builder, ASTNode* ast, TypeCheckContext* type_context) {
    if (!builder || !ast) return;
    
    builder->type_context = type_context;
    
    if (ast->type == AST_BLOCK || ast->type == AST_PROGRAM) {
        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* stmt = ast->data.block.statements ? ast->data.block.statements[i] : NULL;
            if (!stmt) continue;
            if (stmt->type == AST_BLOCK) {
                KrtIrGenerateFromAst(builder, stmt, type_context);
            } else if (stmt->type == AST_NAMESPACE_DECLARATION) {
                KrtIrGenerateStatement(builder, stmt);
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* stmt = ast->data.block.statements[i];
            if (stmt->type == AST_BLOCK) continue;
            if (stmt->type == AST_FUNCTION_DECLARATION ||
                stmt->type == AST_STATIC_FUNCTION_DECLARATION ||
                stmt->type == AST_CLASS_DECLARATION)
                KrtIrGenerateStatement(builder, stmt);
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* stmt = ast->data.block.statements[i];
            if (stmt->type == AST_BLOCK) continue;
            if (stmt->type != AST_FUNCTION_DECLARATION &&
                stmt->type != AST_STATIC_FUNCTION_DECLARATION &&
                stmt->type != AST_CLASS_DECLARATION &&
                stmt->type != AST_NAMESPACE_DECLARATION) {
                KrtIrEnsureMainEntry(builder);
                KrtIrGenerateStatement(builder, stmt);
            }
        }
    } else {
        KrtIrEnsureMainEntry(builder);
        KrtIrGenerateStatement(builder, ast);
    }

    if (builder->current_function) {
        int has_main_code = 0;
        if (ast->type == AST_BLOCK || ast->type == AST_PROGRAM) {
            for (int i = 0; i < ast->data.block.statement_count; i++) {
                ASTNode* s = ast->data.block.statements[i];
                if (s->type != AST_FUNCTION_DECLARATION && s->type != AST_STATIC_FUNCTION_DECLARATION &&
                    s->type != AST_CLASS_DECLARATION && s->type != AST_NAMESPACE_DECLARATION) {
                    has_main_code = 1; break;
                }
            }
        } else {
            has_main_code = 1;
        }

        if (has_main_code) {
            int has_return = 0;
            if (ast->type == AST_BLOCK || ast->type == AST_PROGRAM) {
                for (int i = 0; i < ast->data.block.statement_count; i++) {
                    ASTNode* s = ast->data.block.statements[i];
                    if (s->type != AST_FUNCTION_DECLARATION && s->type != AST_STATIC_FUNCTION_DECLARATION &&
                        s->type != AST_CLASS_DECLARATION && s->type != AST_NAMESPACE_DECLARATION) {
                        if (KrtIrCheckHasReturn(s)) { has_return = 1; break; }
                    }
                }
            }
            if (!has_return && ast->type != AST_RETURN_STATEMENT)
                KrtIrReturn(builder, KRT_IMM_ZERO(builder));
        }
    }
}

static void KrtIrGeneratePrint(KrtIRBuilder* builder, ASTNode** values, int count, bool has_newline) {
    if (!values || count <= 0) return;
    
    for (int i = 0; i < count; i++) {
        if (!values[i]) continue;
        KrtIRValue value = KrtIrGenerateExpression(builder, values[i]);

        char print_func[64];
        snprintf(print_func, sizeof(print_func), "KrtPrint%s", has_newline && i == count - 1 ? "Line" : "");

        KrtIRValue* print_args = IRGEN_ALLOC_ARGS(1);
        if (print_args) {
            IRGEN_SET_ARG(print_args, 0, value);
            KrtIrCall(builder, print_func, print_args, 1);
            IRGEN_FREE(print_args);
        }
    }
}

static int try_extract_constant(ASTNode* node, double* out_value) {
    if (!node || !out_value) return 0;

    switch (node->type) {
        case AST_NUMBER:
            *out_value = node->data.number_value;
            return 1;
        case AST_BOOLEAN:
            *out_value = node->data.boolean_value ? 1.0 : 0.0;
            return 1;
        default:
            return 0;
    }
}

static KrtIRValue convert_to_string_if_needed(KrtIRBuilder* builder, ASTNode* expr_node, KrtIRValue value) {
    if (!expr_node || !builder) return value;
    
    if (expr_node->type == AST_STRING || 
        (expr_node->type == AST_BINARY_OPERATION && 
         expr_node->data.binary_op.operator == TOKEN_OP_ADDITION)) {
        return KrtIrDoubleToString(builder, value);
    }
    
    return value;
}
