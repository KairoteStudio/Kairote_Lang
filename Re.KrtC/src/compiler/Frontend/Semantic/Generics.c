#include "Generics.h"
#include "Accelerator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GenericRegistry* generics_create_registry(void) {
    GenericRegistry* registry = (GenericRegistry*)KRT_MALLOC(sizeof(GenericRegistry));
    if (!registry) return NULL;
    
    registry->types = NULL;
    registry->type_count = 0;
    
    if (pthread_mutex_init(&registry->mutex, NULL) != 0) {
        KRT_FREE(registry);
        return NULL;
    }
    
    return registry;
}

void generics_destroy_registry(GenericRegistry* registry) {
    if (!registry) return;
    
    GenericType* current = registry->types;
    while (current) {
        GenericType* next = current->next;
        KRT_FREE(current->name);
        generics_free_parameters(current->parameters);
        if (current->specialized_symbols) {
            symbol_table_destroy(current->specialized_symbols);
        }
        KRT_FREE(current);
        current = next;
    }
    
    pthread_mutex_destroy(&registry->mutex);
    KRT_FREE(registry);
}

bool generics_register_type(GenericRegistry* registry, const char* name, 
                           GenericParameter* params, int param_count, ASTNode* definition) {
    if (!registry || !name || param_count < 0) return false;
    
    pthread_mutex_lock(&registry->mutex);
    
    GenericType* existing = registry->types;
    while (existing) {
        if (strcmp(existing->name, name) == 0) {
            
            if (definition && !existing->definition) {
                existing->definition = definition;
            }
            
            if (params && !existing->parameters) {
                existing->parameters = params;
                existing->parameter_count = param_count;
            }
            
            pthread_mutex_unlock(&registry->mutex);
            return true;
        }
        existing = existing->next;
    }
    
    GenericType* new_type = (GenericType*)KRT_MALLOC(sizeof(GenericType));
    if (!new_type) {
        pthread_mutex_unlock(&registry->mutex);
        return false;
    }
    
    new_type->name = KRT_STRDUP(name);
    if (!new_type->name) {
        KRT_FREE(new_type);
        pthread_mutex_unlock(&registry->mutex);
        return false;
    }
    
    new_type->parameters = params;
    new_type->parameter_count = param_count;
    new_type->definition = definition;
    new_type->specialized_symbols = NULL;
    new_type->next = registry->types;
    
    registry->types = new_type;
    registry->type_count++;
    
    pthread_mutex_unlock(&registry->mutex);
    return true;
}

GenericType* generics_lookup_type(GenericRegistry* registry, const char* name) {
    if (!registry || !name) return NULL;
    
    pthread_mutex_lock(&registry->mutex);
    GenericType* current = registry->types;
    while (current) {
        if (strcmp(current->name, name) == 0) {
            pthread_mutex_unlock(&registry->mutex);
            return current;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&registry->mutex);
    return NULL;
}

bool generics_is_generic_type(GenericRegistry* registry, const char* type_name) {
    GenericType* result = generics_lookup_type(registry, type_name);
    return result != NULL;
}

bool generics_validate_instantiation(GenericType* generic_type, const char** type_args, int arg_count) {
    (void)type_args;
    if (!generic_type) return false;
    return arg_count == generic_type->parameter_count;
}

bool generics_instantiate_type(GenericRegistry* registry, const char* type_name,
                              const char** type_args, int arg_count,
                              SymbolTable* target_table) {
    if (!registry || !type_name || !type_args || arg_count <= 0 || !target_table) {
        return false;
    }
    
    GenericType* generic_type = generics_lookup_type(registry, type_name);
    if (!generic_type) return false;
    
    if (!generics_validate_instantiation(generic_type, type_args, arg_count)) {
        return false;
    }
    
    size_t total_size = strlen(type_name) + 2; 
    for (int i = 0; i < arg_count; i++) {
        if (type_args[i]) {
            total_size += strlen(type_args[i]);
            if (i > 0) total_size += 1; 
        }
    }
    total_size += 1; 
    
    char* instantiated_name = (char*)KRT_MALLOC(total_size);
    if (!instantiated_name) return false;
    
    KRT_STRCPY_S(instantiated_name, total_size, type_name);
    KRT_STRCAT_S(instantiated_name, total_size, "<");
    for (int i = 0; i < arg_count; i++) {
        if (i > 0) KRT_STRCAT_S(instantiated_name, total_size, ",");
        if (type_args[i]) KRT_STRCAT_S(instantiated_name, total_size, type_args[i]);
    }
    KRT_STRCAT_S(instantiated_name, total_size, ">");
    
    if (symbol_table_lookup(target_table, instantiated_name)) {
        KRT_FREE(instantiated_name);
        return true; 
    }
    
    if (!generic_type->specialized_symbols) {
        generic_type->specialized_symbols = symbol_table_create();
        if (!generic_type->specialized_symbols) {
            KRT_FREE(instantiated_name);
            return false;
        }
    }
    
    SymbolEntry* type_symbol = symbol_table_define(target_table, instantiated_name, 
                                                  SYMBOL_TYPE, 0, NULL);
    if (!type_symbol) {
        KRT_FREE(instantiated_name);
        return false;
    }
    
    KRT_FREE(instantiated_name);
    return true;
}

GenericParameter* generics_create_parameter(const char* name) {
    if (!name) return NULL;
    
    GenericParameter* param = (GenericParameter*)KRT_MALLOC(sizeof(GenericParameter));
    if (!param) return NULL;
    
    param->name = KRT_STRDUP(name);
    if (!param->name) {
        KRT_FREE(param);
        return NULL;
    }
    
    param->next = NULL;
    return param;
}

void generics_add_parameter(GenericParameter** list, const char* name) {
    if (!list || !name) return;
    
    GenericParameter* new_param = generics_create_parameter(name);
    if (!new_param) return;
    
    if (!*list) {
        *list = new_param;
    } else {
        GenericParameter* current = *list;
        while (current->next) {
            current = current->next;
        }
        current->next = new_param;
    }
}

void generics_free_parameters(GenericParameter* params) {
    while (params) {
        GenericParameter* next = params->next;
        KRT_FREE(params->name);
        KRT_FREE(params);
        params = next;
    }
}

const char* generics_build_instantiated_name(const char* base_name, const char** type_args, int arg_count) {
    if (!base_name || !type_args || arg_count <= 0) return NULL;
    
    size_t total_size = strlen(base_name) + 2; 
    for (int i = 0; i < arg_count; i++) {
        total_size += strlen(type_args[i]) + 1; 
    }
    
    char* result = (char*)KRT_MALLOC(total_size);
    if (!result) return NULL;
    
    KRT_STRCPY_S(result, total_size, base_name);
    KRT_STRCAT_S(result, total_size, "<");
    for (int i = 0; i < arg_count; i++) {
        if (i > 0) KRT_STRCAT_S(result, total_size, ",");
        KRT_STRCAT_S(result, total_size, type_args[i]);
    }
    KRT_STRCAT_S(result, total_size, ">");
    
    return result;
}

GenericTypeInfo* generics_get_type_info(GenericRegistry* registry, const char* type_name) {
    if (!registry || !type_name) return NULL;
    
    GenericType* type = generics_lookup_type(registry, type_name);
    if (!type) return NULL;
    
    GenericTypeInfo* info = (GenericTypeInfo*)KRT_MALLOC(sizeof(GenericTypeInfo));
    if (!info) return NULL;
    
    info->name = type->name;
    info->parameters = type->parameters;
    info->parameter_count = type->parameter_count;
    info->declaration = type->definition;
    
    return info;
}