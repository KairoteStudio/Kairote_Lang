#ifndef KRT_NAME_MANGLING_H
#define KRT_NAME_MANGLING_H

#include "../Parser/Ast.h"

typedef struct {
    char* mangled_name;
    int success;
    size_t total_length;
} NameManglingResult;

int name_mangling_init(void);

void name_mangling_cleanup(void);

/** @brief Return the immutable ABI encoding of a primitive type token. */
const char* KrtMangleTypeName(KrtTokenType type);

char* name_mangle_simple(const char* class_name, const char* member_name);

char* name_mangle_constructor(const char* class_name, int parameter_count);

char* name_mangle_names(const char** namespaces, const char* name);

char* name_mangle_from_ast(ASTNode* namespace_node, const char* name);

char* name_mangle_function(const char** namespaces, const char* function_name, KrtTokenType* param_types,
                           int param_count);

char* name_mangle_class_member(const char** namespaces, const char* class_name, const char* member_name);

int name_demangle(const char* mangled_name, char*** out_names, int* out_count);

void name_demangle_free(char** names, int count);

#endif
