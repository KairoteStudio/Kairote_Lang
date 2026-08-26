#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "../../Middle/Ir/Ir.h"
#include <stdbool.h>

typedef enum {
    SYMBOL_VARIABLE,
    SYMBOL_FUNCTION,
    SYMBOL_TYPE,
    SYMBOL_LABEL,
    SYMBOL_CLASS,
    SYMBOL_FIELD,
    SYMBOL_STATIC_FIELD,
    SYMBOL_NAMESPACE
} SymbolType;

typedef enum {
    SYMBOL_DECLARED,
    SYMBOL_DEFINED,
    SYMBOL_FORWARD_REF
} SymbolState;

typedef struct SymbolEntry {
    char* name;
    SymbolType type;
    SymbolState state;
    KrtIRValue* ir_value;
    KrtIRFunction* ir_function;
    int stack_offset;
    struct SymbolEntry* next;
    struct SymbolEntry* scope_next;
    int declaration_line;
    int definition_line;
    bool is_entry_point;
    bool is_array;
    struct SymbolTable* nested_table;
    KrtTokenType value_type;
    char* class_type_name;
} SymbolEntry;

typedef struct SymbolScope {
    struct SymbolScope* parent;
    SymbolEntry* symbols;
    int scope_level;
} SymbolScope;

typedef struct SymbolTable {
    SymbolScope* global_scope;
    SymbolScope* current_scope;
    SymbolEntry** hash_table;
    int hash_size;
    int symbol_count;
    int error_count;
    SymbolEntry** forward_refs;
    int forward_ref_count;
    int forward_ref_capacity;
    struct SymbolTable* parent_table;
} SymbolTable;
typedef void (*ForwardRefCallback)(SymbolEntry* symbol, void* context);
SymbolTable* symbol_table_create(void);
void symbol_table_destroy(SymbolTable* table);
void symbol_table_push_scope(SymbolTable* table);
void symbol_table_pop_scope(SymbolTable* table);
SymbolEntry* symbol_table_declare(SymbolTable* table, const char* name,
                                  SymbolType type, int line);
SymbolEntry* symbol_table_define(SymbolTable* table, const char* name,
                                SymbolType type, int line, void* data);
SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name);
SymbolEntry* symbol_table_lookup_current_scope(SymbolTable* table, const char* name);
SymbolEntry* symbol_table_lookup_scope_chain(SymbolTable* table, const char* name);
bool symbol_table_query_var_type(SymbolTable* table, const char* name,
                                 KrtTokenType* out_value_type, bool* out_is_array);
void symbol_table_mark_defined(SymbolTable* table, const char* name, int line);
void symbol_table_check_undefined(SymbolTable* table);
void symbol_table_register_forward_ref(SymbolTable* table, const char* name,
                                     ForwardRefCallback callback, void* context);
void symbol_table_resolve_forward_refs(SymbolTable* table);
bool symbol_table_check_entry_point_conflict(SymbolTable* table);
SymbolEntry* symbol_table_get_entry_point(SymbolTable* table);
void symbol_table_set_entry_point(SymbolTable* table, const char* name);
const char* symbol_type_to_string(SymbolType type);
const char* symbol_state_to_string(SymbolState state);
void symbol_table_print_stats(SymbolTable* table);
void symbol_table_add_error(SymbolTable* table, const char* format, ...);
int symbol_table_get_error_count(SymbolTable* table);
void symbol_table_define_namespace(SymbolTable* table, const char* namespace_name);
SymbolEntry* symbol_table_lookup_in_namespace(SymbolTable* table, const char* namespace_name, const char* symbol_name);
struct SymbolScope* symbol_table_enter_nested_scope(struct SymbolTable* table, struct SymbolEntry* entry);
void symbol_table_exit_nested_scope(struct SymbolTable* table, struct SymbolScope* previous_scope);

#endif