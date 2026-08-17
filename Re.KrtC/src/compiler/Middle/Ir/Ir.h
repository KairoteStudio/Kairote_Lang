#ifndef KRT_IR_H
#define KRT_IR_H
#include "../../../Core/Utils/KrtCommon.h"
#include "../../Frontend/Parser/Ast.h"
#include "IrMemory.h"
#include "IrParamTable.h"

#ifndef KRT_IR_INLINE
    #ifdef _MSC_VER
        #define KRT_IR_INLINE __forceinline
    #else
        #define KRT_IR_INLINE static inline __attribute__((always_inline))
    #endif
#endif

#ifndef KRT_IR_LIKELY
    #ifdef __GNUC__
        #define KRT_IR_LIKELY(x) __builtin_expect(!!(x), 1)
        #define KRT_IR_UNLIKELY(x) __builtin_expect(!!(x), 0)
    #else
        #define KRT_IR_LIKELY(x) (x)
        #define KRT_IR_UNLIKELY(x) (x)
    #endif
#endif

#ifndef KRT_IR_ASSERT_ENABLED
    #ifdef DEBUG
        #define KRT_IR_ASSERT_ENABLED 1
    #else
        #define KRT_IR_ASSERT_ENABLED 0
    #endif
#endif

#ifndef KRT_IR_BOUNDS_CHECK_ENABLED
    #ifdef DEBUG
        #define KRT_IR_BOUNDS_CHECK_ENABLED 1
    #else
        #define KRT_IR_BOUNDS_CHECK_ENABLED 0
    #endif
#endif

#if KRT_IR_ASSERT_ENABLED
    #define KRT_IR_ASSERT(cond) do { \
        if (!(cond)) { \
            KrtError("IR_ASSERT failed: %s at %s:%d", #cond, __FILE__, __LINE__); \
            assert(cond); \
        } \
    } while(0)
    
    #define KRT_IR_ASSERT_MSG(cond, msg) do { \
        if (!(cond)) { \
            KrtError("IR_ASSERT failed: %s - %s at %s:%d", #cond, msg, __FILE__, __LINE__); \
            assert(cond); \
        } \
    } while(0)
    
    #define KRT_IR_ASSERT_NOT_NULL(ptr) KRT_IR_ASSERT_MSG((ptr) != NULL, #ptr " must not be NULL")
    
    #define KRT_IR_ASSERT_VALID_BUILDER(builder) do { \
        KRT_IR_ASSERT_NOT_NULL(builder); \
        KRT_IR_ASSERT_NOT_NULL((builder)->arena); \
        KRT_IR_ASSERT_NOT_NULL((builder)->module); \
    } while(0)
    
    #define KRT_IR_ASSERT_VALID_FUNCTION(func) do { \
        KRT_IR_ASSERT_NOT_NULL(func); \
        KRT_IR_ASSERT_NOT_NULL((func)->name); \
        KRT_IR_ASSERT((func)->param_count >= 0); \
    } while(0)
    
    #define KRT_IR_ASSERT_VALID_BLOCK(block) do { \
        KRT_IR_ASSERT_NOT_NULL(block); \
        KRT_IR_ASSERT_NOT_NULL((block)->label); \
    } while(0)
    
    #define KRT_IR_ASSERT_VALID_INST(inst) do { \
        KRT_IR_ASSERT_NOT_NULL(inst); \
        KRT_IR_ASSERT((inst)->opcode >= KRT_IR_LOAD && (inst)->opcode <= KRT_IR_NOP); \
        KRT_IR_ASSERT((inst)->operand_count >= 0); \
        KRT_IR_ASSERT((inst)->operand_count <= (inst)->operand_capacity); \
    } while(0)
    
    #define KRT_IR_ASSERT_VALID_VALUE(val) do { \
        KRT_IR_ASSERT((val).type >= KRT_IR_VALUE_VOID && (val).type <= KRT_IR_VALUE_FUNCTION); \
    } while(0)
    
    #define KRT_IR_ASSERT_VALID_ARENA(arena) do { \
        KRT_IR_ASSERT_NOT_NULL(arena); \
        KRT_IR_ASSERT_NOT_NULL((arena)->current_pool); \
        KRT_IR_ASSERT((arena)->pool_size > 0); \
    } while(0)
#else
    #define KRT_IR_ASSERT(cond) ((void)0)
    #define KRT_IR_ASSERT_MSG(cond, msg) ((void)0)
    #define KRT_IR_ASSERT_NOT_NULL(ptr) ((void)0)
    #define KRT_IR_ASSERT_VALID_BUILDER(builder) ((void)0)
    #define KRT_IR_ASSERT_VALID_FUNCTION(func) ((void)0)
    #define KRT_IR_ASSERT_VALID_BLOCK(block) ((void)0)
    #define KRT_IR_ASSERT_VALID_INST(inst) ((void)0)
    #define KRT_IR_ASSERT_VALID_VALUE(val) ((void)0)
    #define KRT_IR_ASSERT_VALID_ARENA(arena) ((void)0)
#endif

#if KRT_IR_BOUNDS_CHECK_ENABLED
    #define KRT_IR_BOUNDS_CHECK(index, count) do { \
        if ((index) < 0 || (index) >= (count)) { \
            KrtError("IR_BOUNDS_CHECK failed: index=%d, count=%d at %s:%d", \
                     (int)(index), (int)(count), __FILE__, __LINE__); \
            assert((index) >= 0 && (index) < (count)); \
        } \
    } while(0)
    
    #define KRT_IR_BOUNDS_CHECK_ARRAY(ptr, index, count) do { \
        KRT_IR_ASSERT_NOT_NULL(ptr); \
        KRT_IR_BOUNDS_CHECK(index, count); \
    } while(0)
    
    #define KRT_IR_CAPACITY_CHECK(current, capacity) do { \
        if ((current) >= (capacity)) { \
            KrtError("IR_CAPACITY_CHECK failed: current=%d, capacity=%d at %s:%d", \
                     (int)(current), (int)(capacity), __FILE__, __LINE__); \
            assert((current) < (capacity)); \
        } \
    } while(0)
#else
    #define KRT_IR_BOUNDS_CHECK(index, count) ((void)0)
    #define KRT_IR_BOUNDS_CHECK_ARRAY(ptr, index, count) ((void)0)
    #define KRT_IR_CAPACITY_CHECK(current, capacity) ((void)0)
#endif

#define KRT_IR_ALLOC_FAIL_ABORT 0
#define KRT_IR_ALLOC_FAIL_RETURN_NULL 1
#define KRT_IR_ALLOC_FAIL_LOG_AND_RETURN 2

#ifndef KRT_IR_ALLOC_FAIL_POLICY
    #define KRT_IR_ALLOC_FAIL_POLICY KRT_IR_ALLOC_FAIL_LOG_AND_RETURN
#endif

#if KRT_IR_ALLOC_FAIL_POLICY == KRT_IR_ALLOC_FAIL_ABORT
    #define KRT_IR_HANDLE_ALLOC_FAIL(msg) do { \
        KrtError("IR_ALLOC_FAIL: %s at %s:%d", msg, __FILE__, __LINE__); \
        abort(); \
    } while(0)
#elif KRT_IR_ALLOC_FAIL_POLICY == KRT_IR_ALLOC_FAIL_RETURN_NULL
    #define KRT_IR_HANDLE_ALLOC_FAIL(msg) do { \
        return NULL; \
    } while(0)
#else 
    #define KRT_IR_HANDLE_ALLOC_FAIL(msg) do { \
        KrtError("IR_ALLOC_FAIL: %s at %s:%d", msg, __FILE__, __LINE__); \
        return NULL; \
    } while(0)
#endif

#define KRT_IR_ALLOC_CHECKED(ptr, type, arena) do { \
    (ptr) = (type*)KrtIrArenaAlloc((arena), sizeof(type)); \
    if (KRT_IR_UNLIKELY(!(ptr))) { \
        KRT_IR_HANDLE_ALLOC_FAIL("Failed to allocate " #type); \
    } \
} while(0)

#define KRT_IR_ALLOC_ARRAY_CHECKED(ptr, type, count, arena) do { \
    (ptr) = (type*)KrtIrArenaAlloc((arena), sizeof(type) * (count)); \
    if (KRT_IR_UNLIKELY(!(ptr))) { \
        KRT_IR_HANDLE_ALLOC_FAIL("Failed to allocate array of " #type); \
    } \
} while(0)

#define KRT_IR_STRDUP_CHECKED(ptr, str, arena) do { \
    (ptr) = KrtIrArenaStrdup((arena), (str)); \
    if (KRT_IR_UNLIKELY(!(ptr))) { \
        KRT_IR_HANDLE_ALLOC_FAIL("Failed to duplicate string"); \
    } \
} while(0)
typedef struct KrtIRModule KrtIRModule;
typedef struct KrtIRFunction KrtIRFunction;
typedef struct KrtIRBasicBlock KrtIRBasicBlock;
typedef struct KrtIRInst KrtIRInst;
typedef struct KrtIRBuilder KrtIRBuilder;

typedef enum {
    KRT_IR_VALUE_VOID,
    KRT_IR_VALUE_IMM,
    KRT_IR_VALUE_VAR,
    KRT_IR_VALUE_TEMP,
    KRT_IR_VALUE_ARG,
    KRT_IR_VALUE_STRING_CONST,
    KRT_IR_VALUE_FUNCTION,
} KrtIRValueType;
#define KRT_IR_TYPE_I64 KRT_IR_VALUE_IMM

typedef struct {
    KrtIRValueType type;
    union {
        double imm;
        char* name;
        int index;
        int string_const_id;
        char* function_name;
    } data;
} KrtIRValue;

typedef enum {
    KRT_IR_LOAD,
    KRT_IR_STORE,
    KRT_IR_ALLOC,
    KRT_IR_IMM,    
    KRT_IR_ADD,
    KRT_IR_SUB,
    KRT_IR_MUL,
    KRT_IR_DIV,
    KRT_IR_MOD,
    KRT_IR_AND,
    KRT_IR_OR,
    KRT_IR_XOR,
    KRT_IR_LSHIFT,
    KRT_IR_RSHIFT,
    KRT_IR_POW,
    KRT_IR_LT,
    KRT_IR_GT,
    KRT_IR_EQ,
    KRT_IR_LE,
    KRT_IR_GE,
    KRT_IR_NE,
    KRT_IR_JUMP,
    KRT_IR_BRANCH,
    KRT_IR_CALL,
    KRT_IR_RETURN,
    KRT_IR_LABEL,
    KRT_IR_STRCAT,
    KRT_IR_CAST,
    KRT_IR_LOADPTR,
    KRT_IR_STOREPTR,
    KRT_IR_ARRAY_STORE,
    KRT_IR_INT_TO_STRING,
    KRT_IR_DOUBLE_TO_STRING,
    KRT_IR_COPY,
    KRT_IR_SYSCALL,
    KRT_IR_PHI,
    KRT_IR_NOP,
} KrtIROpcode;

#define KRT_IR_CACHE_LINE_SIZE 64

typedef struct KrtIRInst {
    
    KrtIROpcode opcode;           
    int operand_count;           
    int operand_capacity;        
    bool is_int_result;          
    char _padding[3];            
    
    KrtIRValue* operands;         
    KrtIRValue result;            
    
    KrtIRInst* next;              
} KrtIRInst;

#define KRT_IR_BLOCK_CACHE_SIZE 4

typedef struct KrtIRBasicBlock {
    
    char* label;                 
    int id;                      
    int inst_count;              
    int inst_capacity;           
    int cache_count;             
    
    KrtIRInst** insts;            
    KrtIRInst* cache[KRT_IR_BLOCK_CACHE_SIZE];  
    
    KrtIRInst* first_inst;        
    KrtIRInst* last_inst;         
    
    struct KrtIRBasicBlock** preds;  
    int pred_count;
    int pred_capacity;
    struct KrtIRBasicBlock** succs;  
    int succ_count;
    int succ_capacity;

    struct KrtIRBlockPhiList* phi_list;
    
    struct KrtIRBasicBlock* next;  
} KrtIRBasicBlock;

typedef struct {
    char* name;
    KrtTokenType type;
    int is_params;
} KrtIRParam;

typedef struct {
    char* name;
    KrtTokenType type;
    int has_initializer;
    double init_number;
} KrtIRGlobal;

typedef struct KrtIRFunction {
    char* name;
    KrtIRParam* params;              
    KrtIRParamTable* param_table;    
    int param_count;
    KrtTokenType return_type;
    KrtIRBasicBlock* entry_block;
    KrtIRBasicBlock* exit_block;
    int stack_size;
    int has_calls;
    bool uses_division;
    bool uses_modulo;
    struct KrtIRFunction* next;
} KrtIRFunction;

typedef struct KrtIRModule {
    KrtIRFunction* functions;
    KrtIRFunction* main_function;
    char** string_constants;
    int string_const_count;
    int string_const_capacity;
    KrtIRGlobal* globals;
    int global_count;
    int global_capacity;
} KrtIRModule;

struct KrtIRBuilder {
    KrtIRModule* module;
    KrtIRFunction* current_function;
    KrtIRBasicBlock* current_block;
    int temp_counter;
    int label_counter;
    int block_id_counter;
    KrtIRBasicBlock** loop_continue_blocks;
    KrtIRBasicBlock** loop_break_blocks;
    int loop_stack_size;
    int loop_stack_capacity;
    char** class_name_stack;
    int class_stack_size;
    int class_stack_capacity;
    char** namespace_stack;
    int namespace_stack_size;
    int namespace_stack_capacity;
    struct KrtIRClassLayout* layouts;
    int layout_count;
    int layout_capacity;
    struct TypeCheckContext* type_context;
    KrtIRMemoryArena* arena;

    int use_object_pool;
    int use_lazy_alloc;

    void* extensions;
};
KrtIRBuilder* KrtIrBuilderCreate(void);
void KrtIrBuilderDestroy(KrtIRBuilder* builder);
KrtIRModule* KrtIrModuleCreate(void);
void KrtIrModuleDestroy(KrtIRModule* module);
KrtIRFunction* KrtIrFunctionCreate(KrtIRBuilder* builder, const char* name, KrtIRParam* params, int param_count, KrtTokenType return_type);
void KrtIrFunctionSetEntry(KrtIRBuilder* builder, KrtIRFunction* func);
KrtIRBasicBlock* KrtIrBlockCreate(KrtIRBuilder* builder, const char* label);
void KrtIrBlockSetCurrent(KrtIRBuilder* builder, KrtIRBasicBlock* block);

int KrtIrBlockGetInstCount(KrtIRBasicBlock* block);
KrtIRInst* KrtIrBlockGetInst(KrtIRBasicBlock* block, int index);
KrtIRInst* KrtIrBlockGetFirstInst(KrtIRBasicBlock* block);
KrtIRInst* KrtIrBlockGetLastInst(KrtIRBasicBlock* block);

void KrtIrBlockAddPred(KrtIRBuilder* builder, KrtIRBasicBlock* block, KrtIRBasicBlock* pred);
void KrtIrBlockAddSucc(KrtIRBuilder* builder, KrtIRBasicBlock* block, KrtIRBasicBlock* succ);
int KrtIrBlockGetPredCount(KrtIRBasicBlock* block);
int KrtIrBlockGetSuccCount(KrtIRBasicBlock* block);
KrtIRBasicBlock* KrtIrBlockGetPred(KrtIRBasicBlock* block, int index);
KrtIRBasicBlock* KrtIrBlockGetSucc(KrtIRBasicBlock* block, int index);

void KrtIrBlockInvalidateCache(KrtIRBasicBlock* block);
KrtIRInst* KrtIrBlockFindCachedInst(KrtIRBasicBlock* block, KrtIROpcode opcode);
KrtIRValue KrtIrLoad(KrtIRBuilder* builder, const char* name);
void KrtIrStore(KrtIRBuilder* builder, const char* name, KrtIRValue value);
void KrtIrAlloc(KrtIRBuilder* builder, const char* name);
KrtIRValue KrtIrLoadPtr(KrtIRBuilder* builder, KrtIRValue base, int offset);
KrtIRValue KrtIrLoadPtrSized(KrtIRBuilder* builder, KrtIRValue base, int offset, int size);
void KrtIrStorePtr(KrtIRBuilder* builder, KrtIRValue base, int offset, KrtIRValue value);
void KrtIrStorePtrSized(KrtIRBuilder* builder, KrtIRValue base, int offset, KrtIRValue value, int size);
void KrtIrArrayStore(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index, KrtIRValue value);
void KrtIrArrayStoreSized(KrtIRBuilder* builder, KrtIRValue array, KrtIRValue index, KrtIRValue value, int element_size);
KrtIRValue KrtIrAdd(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrSub(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrMul(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrDiv(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrMod(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrAnd(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrOr(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrXor(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrLshift(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrRshift(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrPow(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrStrcat(KrtIRBuilder* builder, KrtIRValue lhs, KrtIRValue rhs);
KrtIRValue KrtIrIntToString(KrtIRBuilder* builder, KrtIRValue value);
KrtIRValue KrtIrDoubleToString(KrtIRBuilder* builder, KrtIRValue value);
KrtIRValue KrtIrCast(KrtIRBuilder* builder, KrtIRValue value, KrtTokenType target_type);
KrtIRValue KrtIrCompare(KrtIRBuilder* builder, KrtIROpcode op, KrtIRValue lhs, KrtIRValue rhs);
void KrtIrJump(KrtIRBuilder* builder, KrtIRBasicBlock* target);
void KrtIrBranch(KrtIRBuilder* builder, KrtIRValue cond, KrtIRBasicBlock* true_block, KrtIRBasicBlock* false_block);
KrtIRValue KrtIrCall(KrtIRBuilder* builder, const char* func_name, KrtIRValue* args, int arg_count);
KrtIRValue KrtIrSyscall(KrtIRBuilder* builder, KrtIRValue syscall_num, KrtIRValue* args, int arg_count);
void KrtIrReturn(KrtIRBuilder* builder, KrtIRValue value);
void KrtIrLabel(KrtIRBuilder* builder, const char* label);
void KrtIrNop(KrtIRBuilder* builder);
void KrtIrPushLoopContext(KrtIRBuilder* builder, KrtIRBasicBlock* continue_block, KrtIRBasicBlock* break_block);
void KrtIrPopLoopContext(KrtIRBuilder* builder);
KrtIRBasicBlock* KrtIrGetCurrentContinueBlock(KrtIRBuilder* builder);
KrtIRBasicBlock* KrtIrGetCurrentBreakBlock(KrtIRBuilder* builder);
KrtIRValue KrtIrImm(KrtIRBuilder* builder, double value);
KrtIRValue KrtIrVar(KrtIRBuilder* builder, const char* name);
KrtIRValue KrtIrTemp(KrtIRBuilder* builder);
KrtIRValue KrtIrArg(KrtIRBuilder* builder, int index);
KrtIRValue KrtIrStringConst(KrtIRBuilder* builder, const char* str);
KrtIRValue KrtIrPhi(KrtIRBuilder* builder, KrtIRValue* values, KrtIRBasicBlock** blocks, int count);
KrtIRGlobal* KrtIrModuleAddGlobal(KrtIRBuilder* builder, const char* name, KrtTokenType type);
KrtIRGlobal* KrtIrModuleFindGlobal(KrtIRModule* module, const char* name);
void KrtIrModuleSetGlobalNumberInitializer(KrtIRGlobal* global, double value);
void KrtIrGenerateFromAst(KrtIRBuilder* builder, ASTNode* ast, struct TypeCheckContext* type_context);
void KrtIrPrint(KrtIRModule* module, FILE* output);

typedef struct KrtIRFieldOffset {
    char* name;
    int offset;
} KrtIRFieldOffset;

typedef struct KrtIRClassLayout {
    char* class_name;
    KrtIRFieldOffset* fields;
    int field_count;
    int field_capacity;
} KrtIRClassLayout;
void KrtIrRegisterClassLayout(KrtIRBuilder* builder, const char* class_name, ASTNode* class_body);
int KrtIrLayoutGetOffset(KrtIRBuilder* builder, const char* class_name, const char* field_name);
int KrtIrLayoutGetSize(KrtIRBuilder* builder, const char* class_name);

KrtIRParamNode* KrtIrFunctionFindParam(KrtIRFunction* func, const char* name);
int KrtIrFunctionGetParamIndex(KrtIRFunction* func, const char* name);

KRT_IR_INLINE KrtIRValue KrtIrImmFast(double value) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_IMM;
    result.data.imm = value;
    return result;
}

KRT_IR_INLINE KrtIRValue KrtIrTempFast(int* counter) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_TEMP;
    result.data.index = (*counter)++;
    return result;
}

KRT_IR_INLINE KrtIRValue KrtIrArgFast(int index) {
    KrtIRValue result = {0};
    result.type = KRT_IR_VALUE_ARG;
    result.data.index = index;
    return result;
}

KRT_IR_INLINE int KrtIrBlockInstCountFast(KrtIRBasicBlock* block) {
    return KRT_IR_LIKELY(block != NULL) ? block->inst_count : 0;
}

KRT_IR_INLINE KrtIRInst* KrtIrBlockInstFast(KrtIRBasicBlock* block, int index) {
    if (KRT_IR_UNLIKELY(block == NULL || index < 0 || index >= block->inst_count)) {
        return NULL;
    }
    return block->insts[index];
}

KRT_IR_INLINE KrtIROpcode KrtIrInstOpcodeFast(KrtIRInst* inst) {
    return KRT_IR_LIKELY(inst != NULL) ? inst->opcode : KRT_IR_NOP;
}

KRT_IR_INLINE int KrtIrInstOperandCountFast(KrtIRInst* inst) {
    return KRT_IR_LIKELY(inst != NULL) ? inst->operand_count : 0;
}

KRT_IR_INLINE KrtIRValue* KrtIrInstOperandsFast(KrtIRInst* inst) {
    return KRT_IR_LIKELY(inst != NULL) ? inst->operands : NULL;
}

KRT_IR_INLINE bool KrtIrBuilderIsValid(KrtIRBuilder* builder) {
    return builder && builder->arena && builder->module;
}

KRT_IR_INLINE bool KrtIrFunctionIsValid(KrtIRFunction* func) {
    return func && func->name && func->param_count >= 0;
}

KRT_IR_INLINE bool KrtIrBlockIsValid(KrtIRBasicBlock* block) {
    return block && block->label;
}

KRT_IR_INLINE bool KrtIrInstIsValid(KrtIRInst* inst) {
    return inst && inst->opcode >= KRT_IR_LOAD && inst->opcode <= KRT_IR_NOP
        && inst->operand_count >= 0 && inst->operand_count <= inst->operand_capacity;
}

KRT_IR_INLINE bool KrtIrValueIsValid(KrtIRValue* value) {
    return value && value->type >= KRT_IR_VALUE_VOID && value->type <= KRT_IR_VALUE_FUNCTION;
}

KRT_IR_INLINE bool KrtIrArenaIsValid(KrtIRMemoryArena* arena) {
    return arena && arena->current_pool && arena->pool_size > 0;
}

KRT_IR_INLINE bool KrtIrOperandIndexIsValid(KrtIRInst* inst, int index) {
    return inst && index >= 0 && index < inst->operand_count;
}

KRT_IR_INLINE bool KrtIrParamIndexIsValid(KrtIRFunction* func, int index) {
    return func && index >= 0 && index < func->param_count;
}

#endif
