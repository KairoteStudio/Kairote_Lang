#include "NameMangling.h"
#include "Core/Utils/KrtCommon.h"
#include "Accelerator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int digit_count(int n) {
    if (n == 0) return 1;
    int count = 0;
    while (n > 0) {
        count++;
        n /= 10;
    }
    return count;
}

static char mangle_param_type_char(KrtTokenType type) {
    switch (type) {
        case TOKEN_INT8:  return 'c';
        case TOKEN_INT16: return 's';
        case TOKEN_INT32: return 'i';
        case TOKEN_INT64: return 'l';
        case TOKEN_UINT8:  return 'C';
        case TOKEN_UINT16: return 'S';
        case TOKEN_UINT32: return 'I';
        case TOKEN_UINT64: return 'L';
        case TOKEN_FLOAT32: return 'f';
        case TOKEN_FLOAT64: return 'd';
        case TOKEN_BOOL:   return 'b';
        case TOKEN_STRING:
        case TOKEN_TYPE_STRING: return 'r';
        case TOKEN_VOID:   return 'v';
        default:           return 'x';
    }
}

static int write_number(char* buffer, int n) {
    if (n == 0) {
        buffer[0] = '0';
        return 1;
    }
    
    char temp[16];
    int pos = 0;
    while (n > 0) {
        temp[pos++] = '0' + (n % 10);
        n /= 10;
    }
    
    for (int i = 0; i < pos; i++) {
        buffer[i] = temp[pos - 1 - i];
    }
    buffer[pos] = '\0';
    return pos;
}

static size_t calculate_namespace_length(const char** namespaces) {
    if (!namespaces) return 0;
    
    size_t total = 0;
    for (int i = 0; namespaces[i] != NULL; i++) {
        total += digit_count((int)strlen(namespaces[i]));
        total += strlen(namespaces[i]);
    }
    return total;
}

char* name_mangle_simple(const char* class_name, const char* member_name) {
    if (!class_name || !member_name) {
        return NULL;
    }

    size_t class_len = strlen(class_name);
    size_t member_len = strlen(member_name);
    size_t total_len = class_len + member_len + 3;

    char* mangled = (char*)KRT_MALLOC(total_len);
    if (!mangled) {
        return NULL;
    }

    snprintf(mangled, total_len, "%s__%s", class_name, member_name);
    return mangled;
}

char* name_mangle_constructor(const char* class_name, int parameter_count) {
    if (!class_name) {
        return NULL;
    }

    size_t class_len = strlen(class_name);
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d", parameter_count);
    size_t count_len = strlen(count_str);
    size_t total_len = class_len + strlen("__constructor_") + count_len + 1;

    char* mangled = (char*)KRT_MALLOC(total_len);
    if (!mangled) {
        return NULL;
    }

    snprintf(mangled, total_len, "%s__constructor_%s", class_name, count_str);
    return mangled;
}

char* name_mangle_names(const char** namespaces, const char* name) {
    if (!name) {
        return NULL;
    }
    
    size_t name_len = strlen(name);
    size_t ns_length = calculate_namespace_length(namespaces);
    
    size_t total_len = 2 + 1 + ns_length + digit_count((int)name_len) + name_len + 1 + 1;
    
    char* mangled = (char*)KRT_MALLOC(total_len);
    if (!mangled) {
        return NULL;
    }
    
    char* ptr = mangled;
    
    *ptr++ = '_';
    *ptr++ = 'Z';
    
    *ptr++ = 'N';
    
    if (namespaces) {
        for (int i = 0; namespaces[i] != NULL; i++) {
            size_t len = strlen(namespaces[i]);
            
            ptr += write_number(ptr, (int)len);
            
            memcpy(ptr, namespaces[i], len);
            ptr += len;
        }
    }
    
    size_t final_len = strlen(name);
    ptr += write_number(ptr, (int)final_len);
    memcpy(ptr, name, final_len);
    ptr += final_len;
    
    *ptr++ = 'E';
    *ptr = '\0';
    
    return mangled;
}

char* name_mangle_from_ast(ASTNode* namespace_node, const char* name) {
    if (!name) {
        return NULL;
    }
    
    if (!namespace_node) {
        return name_mangle_names(NULL, name);
    }
    
    const char** namespaces = NULL;
    int ns_count = 0;
    int ns_capacity = 8;
    
    namespaces = (const char**)KRT_MALLOC(sizeof(const char*) * (size_t)(ns_capacity + 1));
    if (!namespaces) {
        return NULL;
    }
    
    ASTNode* current = namespace_node;
    while (current && current->type == AST_NAMESPACE_DECLARATION) {
        if (ns_count >= ns_capacity) {
            ns_capacity *= 2;
            namespaces = (const char**)KRT_REALLOC(namespaces, sizeof(const char*) * (size_t)(ns_capacity + 1));
            if (!namespaces) {
                return NULL;
            }
        }
        
        namespaces[ns_count++] = current->data.namespace_decl.name;
        current = current->data.namespace_decl.body;
    }
    
    if (ns_count > 0) {
        namespaces[ns_count] = NULL;
        char* result = name_mangle_names(namespaces, name);
        KRT_FREE(namespaces);
        return result;
    } else {
        KRT_FREE(namespaces);
        return name_mangle_names(NULL, name);
    }
}

char* name_mangle_function(const char** namespaces, 
                           const char* function_name,
                           KrtTokenType* param_types,
                           int param_count) {
    
    char* base_mangled = name_mangle_names(namespaces, function_name);
    if (!base_mangled) {
        return NULL;
    }
    
    if (!param_types || param_count <= 0) {
        
        size_t base_len = strlen(base_mangled);
        char* result = (char*)KRT_MALLOC(base_len + 2);
        if (!result) {
            KRT_FREE(base_mangled);
            return NULL;
        }
        memcpy(result, base_mangled, base_len);
        result[base_len] = 'v';
        result[base_len + 1] = '\0';
        KRT_FREE(base_mangled);
        return result;
    }
    
    size_t param_encoding_len = 0;
    for (int i = 0; i < param_count; i++) {
        (void)mangle_param_type_char(param_types[i]);
        param_encoding_len += 1;
    }
    
    size_t base_len = strlen(base_mangled);
    size_t total_len = base_len + param_encoding_len + 1;
    char* result = (char*)KRT_MALLOC(total_len);
    if (!result) {
        KRT_FREE(base_mangled);
        return NULL;
    }
    
    memcpy(result, base_mangled, base_len);
    KRT_FREE(base_mangled);
    
    char* ptr = result + base_len;
    for (int i = 0; i < param_count; i++) {
        *ptr++ = mangle_param_type_char(param_types[i]);
    }
    *ptr = '\0';
    
    return result;
}

char* name_mangle_class_member(const char** namespaces,
                               const char* class_name,
                               const char* member_name) {
    if (!class_name || !member_name) {
        return NULL;
    }
    
    int ns_count = 0;
    if (namespaces) {
        while (namespaces[ns_count] != NULL) {
            ns_count++;
        }
    }
    
    const char** full_path = (const char**)KRT_MALLOC(sizeof(const char*) * (size_t)(ns_count + 2));
    if (!full_path) {
        return NULL;
    }
    
    for (int i = 0; i < ns_count; i++) {
        full_path[i] = namespaces[i];
    }
    full_path[ns_count] = class_name;
    full_path[ns_count + 1] = NULL;
    
    char* result = name_mangle_names(full_path, member_name);
    KRT_FREE(full_path);
    return result;
}

int name_demangle(const char* mangled_name, char*** out_names, int* out_count) {
    if (!mangled_name || !out_names || !out_count) {
        return 0;
    }
    
    if (strlen(mangled_name) < 2 || mangled_name[0] != '_' || mangled_name[1] != 'Z') {
        return 0;
    }
    
    const char* ptr = mangled_name + 2;
    *out_count = 0;
    *out_names = NULL;
    
    int capacity = 8;
    *out_names = (char**)KRT_MALLOC(sizeof(char*) * (size_t)capacity);
    if (!*out_names) {
        return 0;
    }
    
    if (*ptr == 'N') {
        ptr++;
    }
    
    while (*ptr && *ptr != 'E') {
        
        int length = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            length = length * 10 + (*ptr - '0');
            ptr++;
        }
        
        if (length <= 0 || !*ptr) {
            
            name_demangle_free(*out_names, *out_count);
            *out_names = NULL;
            *out_count = 0;
            return 0;
        }
        
        if (*out_count >= capacity) {
            capacity *= 2;
            *out_names = (char**)KRT_REALLOC(*out_names, sizeof(char*) * (size_t)capacity);
            if (!*out_names) {
                return 0;
            }
        }
        
        char* name = (char*)KRT_MALLOC((size_t)(length + 1));
        if (!name) {
            name_demangle_free(*out_names, *out_count);
            *out_names = NULL;
            *out_count = 0;
            return 0;
        }
        
        memcpy(name, ptr, (size_t)length);
        name[length] = '\0';
        (*out_names)[(*out_count)++] = name;
        ptr += length;
    }
    
    if (*ptr == 'E') {
        ptr++;
    }
    
    return 1;
}

void name_demangle_free(char** names, int count) {
    if (!names) return;
    
    for (int i = 0; i < count; i++) {
        if (names[i]) {
            KRT_FREE(names[i]);
        }
    }
    KRT_FREE(names);
}

int name_mangling_init(void) {
    return 1;
}

void name_mangling_cleanup(void) {
    
}
