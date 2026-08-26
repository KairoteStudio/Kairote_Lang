#include "SymbolTable.h"
#include "Core/Utils/OutputCache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define DEBUG_SYMBOL_TABLE 1

static unsigned int hash_string(const char* str, int table_size) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % table_size;
}

SymbolTable* symbol_table_create(void) {
    SymbolTable* table = KRT_CALLOC(1, sizeof(SymbolTable));
    if (!table) return NULL;

    table->hash_size = 256;
    table->hash_table = KRT_CALLOC(table->hash_size, sizeof(SymbolEntry*));
    if (!table->hash_table) {
        KRT_FREE(table);
        return NULL;
    }

    table->global_scope = KRT_CALLOC(1, sizeof(SymbolScope));
    if (!table->global_scope) {
        KRT_FREE(table->hash_table);
        KRT_FREE(table);
        return NULL;
    }

    table->global_scope->scope_level = 0;
    table->current_scope = table->global_scope;

    table->forward_ref_capacity = 64;
    table->forward_refs = KRT_CALLOC(table->forward_ref_capacity, sizeof(SymbolEntry*));
    if (!table->forward_refs) {
        KRT_FREE(table->global_scope);
        KRT_FREE(table->hash_table);
        KRT_FREE(table);
        return NULL;
    }

    table->parent_table = NULL;

    return table;
}

void symbol_table_destroy(SymbolTable* table) {
    if (!table) return;

    SymbolScope* scope = table->global_scope;
    while (scope) {
        SymbolScope* next_scope = scope->parent;

        SymbolEntry* sym = scope->symbols;
        while (sym) {
            SymbolEntry* next_sym = sym->scope_next;
            if (sym->nested_table) {
                if (sym->nested_table != table) {
                    symbol_table_destroy(sym->nested_table);
                }
                sym->nested_table = NULL;
            }
            KRT_FREE(sym->name);
            KRT_FREE(sym);
            sym = next_sym;
        }

        KRT_FREE(scope);
        scope = next_scope;
    }

    KRT_FREE(table->hash_table);
    KRT_FREE(table->forward_refs);
    KRT_FREE(table);
}

void symbol_table_push_scope(SymbolTable* table) {
    if (!table) return;

    SymbolScope* new_scope = KRT_CALLOC(1, sizeof(SymbolScope));
    if (!new_scope) return;

    new_scope->parent = table->current_scope;
    new_scope->scope_level = table->current_scope->scope_level + 1;
    table->current_scope = new_scope;
}

void symbol_table_pop_scope(SymbolTable* table) {
    if (!table || !table->current_scope || table->current_scope == table->global_scope) {
        return;
    }

    SymbolScope* old_scope = table->current_scope;
    table->current_scope = old_scope->parent;

    SymbolEntry* sym = old_scope->symbols;
    while (sym) {
        SymbolEntry* next_sym = sym->scope_next;
        
        unsigned int hash = hash_string(sym->name, table->hash_size);
        SymbolEntry* entry = table->hash_table[hash];
        SymbolEntry* prev = NULL;
        
        while (entry) {
            if (entry == sym) {
                if (prev) {
                    prev->next = entry->next;
                } else {
                    table->hash_table[hash] = entry->next;
                }
                KRT_FREE(sym->name);
                KRT_FREE(sym);
                break;
            }
            prev = entry;
            entry = entry->next;
        }
        
        sym = next_sym;
    }

    old_scope->symbols = NULL;
    KRT_FREE(old_scope);
}

SymbolEntry* symbol_table_declare(SymbolTable* table, const char* name,
                                SymbolType type, int line) {
    if (!table || !name) return NULL;

    SymbolEntry* existing = symbol_table_lookup_current_scope(table, name);
    if (existing) {
        if (existing->state == SYMBOL_DECLARED && existing->type == type) {

            return existing;
        } else {
            symbol_table_add_error(table, "符号 '%s' 重复声明", name);
            return NULL;
        }
    }

    SymbolEntry* sym = KRT_CALLOC(1, sizeof(SymbolEntry));
    if (!sym) return NULL;

    sym->name = KRT_STRDUP(name);
    sym->type = type;
    sym->state = SYMBOL_DECLARED;
    sym->declaration_line = line;
    sym->is_array = false; 

    unsigned int hash = hash_string(name, table->hash_size);
    sym->next = table->hash_table[hash];
    table->hash_table[hash] = sym;

    sym->scope_next = table->current_scope->symbols;
    table->current_scope->symbols = sym;

    table->symbol_count++;
    
    return sym;
}

SymbolEntry* symbol_table_define(SymbolTable* table, const char* name,
                               SymbolType type, int line, void* data) {
    if (!table || !name) return NULL;

    SymbolEntry* sym = symbol_table_lookup_current_scope(table, name);

    if (sym) {
        if (sym->state == SYMBOL_DEFINED) {
            symbol_table_add_error(table, "Symbol '%s' already defined in current scope", name);
            return NULL;
        }

        sym->state = SYMBOL_DEFINED;
        sym->definition_line = line;

        switch (type) {
            case SYMBOL_VARIABLE:
                if (data) sym->ir_value = (KrtIRValue*)data;
                sym->is_array = false; 
                break;
            case SYMBOL_FUNCTION:
                if (data) sym->ir_function = (KrtIRFunction*)data;
                sym->is_array = false; 
                break;
            default:
                sym->is_array = false; 
                break;
        }

        return sym;
    } else {
        sym = symbol_table_declare(table, name, type, line);
        if (sym) {
            sym->state = SYMBOL_DEFINED;
            sym->definition_line = line;

            switch (type) {
                case SYMBOL_VARIABLE:
                    if (data) sym->ir_value = (KrtIRValue*)data;
                    sym->is_array = false; 
                    break;
                case SYMBOL_FUNCTION:
                    if (data) sym->ir_function = (KrtIRFunction*)data;
                    sym->is_array = false; 
                    break;
                default:
                    sym->is_array = false; 
                    break;
            }
        }
        return sym;
    }
}

SymbolEntry* symbol_table_lookup(SymbolTable* table, const char* name) {
    if (!table || !name) return NULL;

    unsigned int hash = hash_string(name, table->hash_size);
    SymbolEntry* sym = table->hash_table[hash];
    
    while (sym) {
        if (sym->name && strcmp(sym->name, name) == 0) {
            return sym;
        }
        sym = sym->next;
    }
    
    return NULL;
}

SymbolEntry* symbol_table_lookup_current_scope(SymbolTable* table, const char* name) {
    if (!table || !table->current_scope || !name) return NULL;

    SymbolEntry* sym = table->current_scope->symbols;
    while (sym) {
        if (sym->name && strcmp(sym->name, name) == 0) {
            return sym;
        }
        sym = sym->scope_next;
    }

    return NULL;
}

SymbolEntry* symbol_table_lookup_scope_chain(SymbolTable* table, const char* name) {
    if (!table || !name) return NULL;
    
    SymbolScope* current_scope = table->current_scope;
    while (current_scope) {
        SymbolEntry* sym = current_scope->symbols;
        while (sym) {
            if (sym->name && strcmp(sym->name, name) == 0) {
                return sym;
            }
            sym = sym->scope_next;
        }
        current_scope = current_scope->parent;
    }
    
    return symbol_table_lookup(table, name);
}

bool symbol_table_query_var_type(SymbolTable* table, const char* name,
                                 KrtTokenType* out_value_type, bool* out_is_array) {
    SymbolEntry* e = table ? symbol_table_lookup_scope_chain(table, name) : NULL;
    if (!e) return false;
    if (out_value_type) *out_value_type = e->value_type;
    if (out_is_array)   *out_is_array   = e->is_array;
    return true;
}

void symbol_table_mark_defined(SymbolTable* table, const char* name, int line) {
    if (!table || !name) return;

    SymbolEntry* sym = symbol_table_lookup(table, name);
    if (sym) {
        sym->state = SYMBOL_DEFINED;
        sym->definition_line = line;
    }
}

void symbol_table_check_undefined(SymbolTable* table) {
    if (!table) return;

    for (int i = 0; i < table->hash_size; i++) {
        SymbolEntry* sym = table->hash_table[i];
        while (sym) {
            if (sym->state == SYMBOL_DECLARED) {
                symbol_table_add_error(table, "符号 '%s' 已声明但未定义", sym->name);
            }
            sym = sym->next;
        }
    }
}

bool symbol_table_check_entry_point_conflict(SymbolTable* table) {
    if (!table) return false;

    int entry_count = 0;

    for (int i = 0; i < table->hash_size; i++) {
        SymbolEntry* sym = table->hash_table[i];
        while (sym) {
            if (sym->is_entry_point) {
                entry_count++;
            }
            sym = sym->next;
        }
    }

    if (entry_count > 1) {
        symbol_table_add_error(table, "发现多个入口点（main函数）");
        return true;
    }

    return false;
}

SymbolEntry* symbol_table_get_entry_point(SymbolTable* table) {
    if (!table) return NULL;

    for (int i = 0; i < table->hash_size; i++) {
        SymbolEntry* sym = table->hash_table[i];
        while (sym) {
            if (sym->is_entry_point && strcmp(sym->name, "main") == 0) {
                return sym;
            }
            sym = sym->next;
        }
    }

    return NULL;
}

void symbol_table_set_entry_point(SymbolTable* table, const char* name) {
    if (!table || !name) return;

    SymbolEntry* sym = symbol_table_lookup(table, name);
    if (sym && sym->type == SYMBOL_FUNCTION) {
        sym->is_entry_point = true;
    }
}

const char* symbol_type_to_string(SymbolType type) {
    switch (type) {
        case SYMBOL_VARIABLE: return "变量";
        case SYMBOL_FUNCTION: return "函数";
        case SYMBOL_TYPE: return "类型";
        case SYMBOL_LABEL: return "标签";
        case SYMBOL_FIELD: return "字段";
        case SYMBOL_STATIC_FIELD: return "静态字段";
        case SYMBOL_CLASS: return "类";
        case SYMBOL_NAMESPACE: return "命名空间";
        default: return "未知";
    }
}

const char* symbol_state_to_string(SymbolState state) {
    switch (state) {
        case SYMBOL_DECLARED: return "已声明";
        case SYMBOL_DEFINED: return "已定义";
        case SYMBOL_FORWARD_REF: return "前向引用";
        default: return "未知";
    }
}

void symbol_table_print_stats(SymbolTable* table) {
    if (!table) return;

    printf("符号表统计信息:\n");
    printf("  总符号数: %d\n", table->symbol_count);
    printf("  错误数: %d\n", table->error_count);
    printf("  前向引用数: %d\n", table->forward_ref_count);
    printf("  当前作用域层级: %d\n", table->current_scope->scope_level);

    int var_count = 0, func_count = 0, type_count = 0, label_count = 0;
    for (int i = 0; i < table->hash_size; i++) {
        SymbolEntry* sym = table->hash_table[i];
        while (sym) {
            switch (sym->type) {
                case SYMBOL_VARIABLE: var_count++; break;
                case SYMBOL_FUNCTION: func_count++; break;
                case SYMBOL_TYPE: type_count++; break;
                case SYMBOL_LABEL: label_count++; break;
                default: break;
            }
            sym = sym->next;
        }
    }

    printf("  变量: %d, 函数: %d, 类型: %d, 标签: %d\n",
           var_count, func_count, type_count, label_count);
}

void symbol_table_add_error(SymbolTable* table, const char* format, ...) {
    if (!table || !format) return;

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    KrtError("符号表错误: %s", buffer);
    table->error_count++;
}

int symbol_table_get_error_count(SymbolTable* table) {
    return table ? table->error_count : 0;
}

void symbol_table_define_namespace(SymbolTable* table, const char* namespace_name) {
    if (!table || !namespace_name) return;

    SymbolEntry* existing = symbol_table_lookup(table, namespace_name);
    if (existing) {
        symbol_table_add_error(table, "Namespace '%s' already defined", namespace_name);
        return;
    }

    SymbolEntry* ns_entry = symbol_table_define(table, namespace_name, SYMBOL_NAMESPACE, 0, NULL);
    if (ns_entry) {
        ns_entry->nested_table = symbol_table_create();
    }
}

SymbolEntry* symbol_table_lookup_in_namespace(SymbolTable* table, const char* namespace_name, const char* symbol_name) {
    if (!table || !namespace_name || !symbol_name) return NULL;

    SymbolEntry* namespace_entry = symbol_table_lookup(table, namespace_name);
    if (!namespace_entry) {
        symbol_table_add_error(table, "Namespace or class '%s' not found", namespace_name);
        return NULL;
    }

    if (namespace_entry->type != SYMBOL_NAMESPACE && namespace_entry->type != SYMBOL_CLASS) {
        symbol_table_add_error(table, "'%s' is not a namespace or class", namespace_name);
        return NULL;
    }

    if (namespace_entry->nested_table) {
        SymbolEntry* found = symbol_table_lookup(namespace_entry->nested_table, symbol_name);
        if (found) return found;
    }

    SymbolEntry* global_found = symbol_table_lookup(table, symbol_name);
    return global_found;
}

struct SymbolScope* symbol_table_enter_nested_scope(SymbolTable* table, SymbolEntry* entry) {
    if (!table || !entry) return NULL;

    if (!entry->nested_table) {
        entry->nested_table = symbol_table_create();
    }

    if (!entry->nested_table->current_scope) {
        symbol_table_push_scope(entry->nested_table);
    }

    SymbolScope* previous = table->current_scope;
    table->current_scope = entry->nested_table->current_scope;
    return previous;
}

void symbol_table_exit_nested_scope(SymbolTable* table, SymbolScope* previous_scope) {
    if (!table) return;
    if (previous_scope) {
        table->current_scope = previous_scope;
    }
}