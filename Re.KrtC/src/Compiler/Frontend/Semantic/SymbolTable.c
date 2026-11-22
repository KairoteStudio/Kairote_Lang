#include "SymbolTable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>

static unsigned int hash_string(const char* str, int table_size) {
    if (table_size <= 0) {
        return 0;
    }
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % (unsigned)table_size;
}

static bool grow_scope_index(SymbolScope* scope) {
    if (scope->hash_size > INT_MAX / 2) {
        return false;
    }
    int size = scope->hash_size ? scope->hash_size * 2 : 16;
    SymbolEntry** buckets = KRT_CALLOC(size, sizeof(SymbolEntry*));
    if (!buckets) {
        return false;
    }
    for (SymbolEntry* entry = scope->symbols; entry; entry = entry->scope_next) {
        unsigned bucket = hash_string(entry->name, size);
        entry->scope_hash_next = buckets[bucket];
        buckets[bucket] = entry;
    }
    KRT_FREE(scope->hash_table);
    scope->hash_table = buckets;
    scope->hash_size = size;
    return true;
}

static bool grow_table_index(SymbolTable* table) {
    if (!table || table->hash_size > INT_MAX / 2) {
        return false;
    }
    int size = table->hash_size ? table->hash_size * 2 : 16;
    SymbolEntry** buckets = KRT_CALLOC(size, sizeof(SymbolEntry*));
    if (!buckets) {
        return false;
    }
    for (int i = 0; i < table->hash_size; i++) {
        SymbolEntry* entry = table->hash_table[i];
        while (entry) {
            SymbolEntry* next = entry->next;
            unsigned bucket = hash_string(entry->name, size);
            entry->next = buckets[bucket];
            buckets[bucket] = entry;
            entry = next;
        }
    }
    /* Doubling splits old buckets; reverse each chain to retain shadowing order. */
    for (int i = 0; i < size; i++) {
        SymbolEntry* entry = buckets[i];
        buckets[i] = NULL;
        while (entry) {
            SymbolEntry* next = entry->next;
            entry->next = buckets[i];
            buckets[i] = entry;
            entry = next;
        }
    }
    KRT_FREE(table->hash_table);
    table->hash_table = buckets;
    table->hash_size = size;
    return true;
}

static SymbolEntry* lookup_in_scope(SymbolScope* scope, const char* name) {
    if (!scope || !scope->hash_size) {
        return NULL;
    }
    unsigned bucket = hash_string(name, scope->hash_size);
    for (SymbolEntry* entry = scope->hash_table[bucket]; entry; entry = entry->scope_hash_next) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

SymbolTable* symbol_table_create(void) {
    SymbolTable* table = KRT_CALLOC(1, sizeof(SymbolTable));
    if (!table) {
        return NULL;
    }

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

    table->parent_table = NULL;

    return table;
}

void symbol_table_destroy(SymbolTable* table) {
    if (!table) {
        return;
    }

    SymbolScope* scope = table->current_scope ? table->current_scope : table->global_scope;
    while (scope) {
        SymbolScope* next_scope = scope->parent;

        SymbolEntry* sym = scope->symbols;
        while (sym) {
            SymbolEntry* next_sym = sym->scope_next;
            if (sym->nested_table && sym->nested_table != table) {
                SymbolTable* nested = sym->nested_table;
                sym->nested_table = NULL;
                /* Several entries can alias one nested table; only the first
                 * destroys it. */
                if (!nested->destroyed) {
                    nested->destroyed = true;
                    symbol_table_destroy(nested);
                }
            } else {
                sym->nested_table = NULL;
            }
            KRT_FREE(sym->name);
            KRT_FREE(sym);
            sym = next_sym;
        }

        KRT_FREE(scope->hash_table);
        KRT_FREE(scope);
        scope = next_scope;
    }

    /* enter_nested_scope may have left current_scope pointing into a nested
     * table, in which case the global scope is not on the chain above. */
    if (table->global_scope) {
        int reachable = 0;
        for (SymbolScope* s = table->current_scope; s; s = s->parent) {
            if (s == table->global_scope) {
                reachable = 1;
                break;
            }
        }
        if (!reachable) {
            SymbolEntry* sym = table->global_scope->symbols;
            while (sym) {
                SymbolEntry* next_sym = sym->scope_next;
                KRT_FREE(sym->name);
                KRT_FREE(sym);
                sym = next_sym;
            }
            KRT_FREE(table->global_scope->hash_table);
            KRT_FREE(table->global_scope);
        }
    }

    KRT_FREE(table->hash_table);
    KRT_FREE(table);
}

void symbol_table_push_scope(SymbolTable* table) {
    if (!table) {
        return;
    }

    SymbolScope* new_scope = KRT_CALLOC(1, sizeof(SymbolScope));
    if (!new_scope) {
        return;
    }

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

        bool found = false;
        while (entry) {
            if (entry == sym) {
                if (prev) {
                    prev->next = entry->next;
                } else {
                    table->hash_table[hash] = entry->next;
                }
                found = true;
                break;
            }
            prev = entry;
            entry = entry->next;
        }

        if (!found) {
            /* Not in the table-level index (scope-only entry): release it. */
            table->symbol_count--;
        }
        KRT_FREE(sym->name);
        KRT_FREE(sym);

        sym = next_sym;
    }

    old_scope->symbols = NULL;
    KRT_FREE(old_scope->hash_table);
    KRT_FREE(old_scope);
}

SymbolEntry* symbol_table_declare(SymbolTable* table, const char* name, SymbolType type, int line) {
    if (!table || !name) {
        return NULL;
    }

    SymbolEntry* existing = symbol_table_lookup_current_scope(table, name);
    if (existing) {
        if (existing->state == SYMBOL_DECLARED && existing->type == type) {

            return existing;
        } else {
            symbol_table_add_error(table, "符号 '%s' 重复声明（第 %d 行）", name, line);
            return NULL;
        }
    }

    SymbolScope* scope = table->current_scope;
    if (((size_t)scope->symbol_count + 1) * 4 >= (size_t)scope->hash_size * 3 && !grow_scope_index(scope)) {
        return NULL;
    }
    if (((size_t)table->symbol_count + 1) * 4 >= (size_t)table->hash_size * 3 && !grow_table_index(table)) {
        return NULL;
    }
    SymbolEntry* sym = KRT_CALLOC(1, sizeof(SymbolEntry));
    if (!sym) {
        return NULL;
    }
    sym->is_initialized = 1; /* only locals declared without a value clear this */
    sym->const_array_size = -1;

    sym->name = KRT_STRDUP(name);
    if (!sym->name) {
        KRT_FREE(sym);
        return NULL;
    }
    sym->type = type;
    sym->state = SYMBOL_DECLARED;
    sym->declaration_line = line;
    sym->is_array = false;

    unsigned int hash = hash_string(name, table->hash_size);
    sym->next = table->hash_table[hash];
    table->hash_table[hash] = sym;

    sym->scope_next = table->current_scope->symbols;
    table->current_scope->symbols = sym;
    unsigned scope_hash = hash_string(name, scope->hash_size);
    sym->scope_hash_next = scope->hash_table[scope_hash];
    scope->hash_table[scope_hash] = sym;
    scope->symbol_count++;

    table->symbol_count++;

    return sym;
}

SymbolEntry* symbol_table_define(SymbolTable* table, const char* name, SymbolType type, int line, void* data) {
    if (!table || !name) {
        return NULL;
    }

    SymbolEntry* sym = symbol_table_lookup_current_scope(table, name);

    if (sym) {
        if (sym->state == SYMBOL_DEFINED) {
            symbol_table_add_error(table, "Symbol '%s' already defined in current scope (line %d)", name, line);
            return NULL;
        }
        if (sym->type != type) {
            symbol_table_add_error(table, "符号 '%s' 的声明与定义类型不一致（第 %d 行）", name, line);
            return NULL;
        }

        sym->state = SYMBOL_DEFINED;
        sym->definition_line = line;

        switch (type) {
        case SYMBOL_VARIABLE:
            if (data) {
                sym->ir_value = (KrtIRValue*)data;
            }
            sym->is_array = false;
            break;
        case SYMBOL_FUNCTION:
            if (data) {
                sym->ir_function = (KrtIRFunction*)data;
            }
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
                if (data) {
                    sym->ir_value = (KrtIRValue*)data;
                }
                sym->is_array = false;
                break;
            case SYMBOL_FUNCTION:
                if (data) {
                    sym->ir_function = (KrtIRFunction*)data;
                }
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
    if (!table || !name) {
        return NULL;
    }

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
    if (!table || !table->current_scope || !name) {
        return NULL;
    }

    return lookup_in_scope(table->current_scope, name);
}

SymbolEntry* symbol_table_lookup_scope_chain(SymbolTable* table, const char* name) {
    if (!table || !name) {
        return NULL;
    }

    int depth = 0;
    for (SymbolTable* t = table; t && depth < 64; t = t->parent_table, depth++) {
        SymbolScope* current_scope = t->current_scope;
        while (current_scope) {
            SymbolEntry* sym = lookup_in_scope(current_scope, name);
            if (sym) {
                return sym;
            }
            current_scope = current_scope->parent;
        }
    }

    /* No whole-table hash fallback: a name must be found on the lexical scope
     * chain, otherwise sibling scopes would shadow each other.  The lexical
     * parent table is walked, so a class body sees the globals around it. */
    return NULL;
}

bool symbol_table_query_var_type(SymbolTable* table, const char* name, KrtTokenType* out_value_type,
                                 bool* out_is_array) {
    SymbolEntry* e = table ? symbol_table_lookup_scope_chain(table, name) : NULL;
    if (!e) {
        return false;
    }
    if (out_value_type) {
        *out_value_type = e->value_type;
    }
    if (out_is_array) {
        *out_is_array = e->is_array;
    }
    return true;
}






const char* symbol_type_to_string(SymbolType type) {
    switch (type) {
    case SYMBOL_VARIABLE:
        return "变量";
    case SYMBOL_FUNCTION:
        return "函数";
    case SYMBOL_TYPE:
        return "类型";
    case SYMBOL_LABEL:
        return "标签";
    case SYMBOL_FIELD:
        return "字段";
    case SYMBOL_STATIC_FIELD:
        return "静态字段";
    case SYMBOL_CLASS:
        return "类";
    case SYMBOL_NAMESPACE:
        return "命名空间";
    default:
        return "未知";
    }
}

const char* symbol_state_to_string(SymbolState state) {
    switch (state) {
    case SYMBOL_DECLARED:
        return "已声明";
    case SYMBOL_DEFINED:
        return "已定义";
    case SYMBOL_FORWARD_REF:
        return "前向引用";
    default:
        return "未知";
    }
}


void symbol_table_add_error(SymbolTable* table, const char* format, ...) {
    if (!table || !format) {
        return;
    }

    va_list args;
    va_start(args, format);

    char buffer[1024];
    int written = vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);
    if (written < 0) {
        buffer[0] = '\0';
    }

    KrtError("符号表错误: %s", buffer);
    table->error_count++;
}




struct SymbolScope* symbol_table_enter_nested_scope(SymbolTable* table, SymbolEntry* entry) {
    if (!table || !entry) {
        return NULL;
    }

    if (!entry->nested_table) {
        entry->nested_table = symbol_table_create();
        if (!entry->nested_table) {
            return NULL;
        }
    }
    if (!entry->nested_table->current_scope) {
        symbol_table_push_scope(entry->nested_table);
    }
    if (!entry->nested_table->current_scope) {
        return NULL;
    }

    SymbolScope* previous = table->current_scope;
    table->current_scope = entry->nested_table->current_scope;
    return previous;
}

void symbol_table_exit_nested_scope(SymbolTable* table, SymbolScope* previous_scope) {
    if (!table) {
        return;
    }
    if (previous_scope) {
        table->current_scope = previous_scope;
    }
}

bool symbol_table_check_undefined(SymbolTable* table) {
    if (!table) {
        return false;
    }
    bool all_defined = true;
    for (int i = 0; i < table->hash_size; i++) {
        SymbolEntry* sym = table->hash_table[i];
        while (sym) {
            if (sym->state == SYMBOL_DECLARED) {
                symbol_table_add_error(table, "符号 '%s' 已声明但未定义", sym->name);
                all_defined = false;
            }
            sym = sym->next;
        }
    }
    return all_defined;
}

SymbolEntry* symbol_table_lookup_in_namespace(SymbolTable* table, const char* namespace_name, const char* symbol_name) {
    if (!table || !namespace_name || !symbol_name) {
        return NULL;
    }

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
        if (found) {
            return found;
        }
    }
    return symbol_table_lookup(table, symbol_name);
}
