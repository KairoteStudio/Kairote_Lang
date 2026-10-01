#ifndef KRT_GENERICS_H
#define KRT_GENERICS_H

#include "SymbolTable.h"
#include "../Parser/Ast.h"
#include <stdbool.h>
#include <pthread.h>

typedef struct GenericParameter {
    char* name;
    struct GenericParameter* next;
} GenericParameter;

typedef struct GenericType {
    char* name;
    GenericParameter* parameters;
    int parameter_count;
    ASTNode* definition;
    SymbolTable* specialized_symbols;
    struct GenericType* next;
} GenericType;

typedef struct GenericRegistry {
    GenericType* types;
    int type_count;
    pthread_mutex_t mutex;
} GenericRegistry;

typedef struct GenericTypeInfo {
    const char* name;
    GenericParameter* parameters;
    int parameter_count;
    ASTNode* declaration;
} GenericTypeInfo;

GenericRegistry* generics_create_registry(void);
void generics_destroy_registry(GenericRegistry* registry);

bool generics_register_type(GenericRegistry* registry, const char* name, GenericParameter* params, int param_count,
                            ASTNode* definition);

GenericType* generics_lookup_type(GenericRegistry* registry, const char* name);

bool generics_instantiate_type(GenericRegistry* registry, const char* type_name, const char** type_args, int arg_count,
                               SymbolTable* target_table);

GenericParameter* generics_create_parameter(const char* name);
void generics_add_parameter(GenericParameter** list, const char* name);
void generics_free_parameters(GenericParameter* params);

bool generics_is_generic_type(GenericRegistry* registry, const char* type_name);
bool generics_validate_instantiation(GenericType* generic_type, const char** type_args, int arg_count);

const char* generics_build_instantiated_name(const char* base_name, const char** type_args, int arg_count);

GenericTypeInfo* generics_get_type_info(GenericRegistry* registry, const char* type_name);

#endif
