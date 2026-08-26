#include "Preprocessor.h"
#include <string.h>
#include <ctype.h>

Preprocessor* PreprocessorCreate(void) {
    Preprocessor* preprocessor = (Preprocessor*)KRT_MALLOC(sizeof(Preprocessor));
    if (!preprocessor) return NULL;
    
    preprocessor->macros = NULL;
    preprocessor->macro_count = 0;
    preprocessor->macro_capacity = 0;
    
    return preprocessor;
}

void PreprocessorDestroy(Preprocessor* preprocessor) {
    if (!preprocessor) return;
    
    for (int i = 0; i < preprocessor->macro_count; i++) {
        KRT_FREE(preprocessor->macros[i].name);
        KRT_FREE(preprocessor->macros[i].replacement);
    }
    KRT_FREE(preprocessor->macros);
    KRT_FREE(preprocessor);
}

bool PreprocessorAddMacro(Preprocessor* preprocessor, const char* name, const char* replacement) {
    if (!preprocessor || !name || !replacement) return false;
    
    for (int i = 0; i < preprocessor->macro_count; i++) {
        if (strcmp(preprocessor->macros[i].name, name) == 0) {
            
            KRT_FREE(preprocessor->macros[i].replacement);
            preprocessor->macros[i].replacement = KRT_STRDUP(replacement);
            return true;
        }
    }
    
    if (preprocessor->macro_count >= preprocessor->macro_capacity) {
        int new_capacity = preprocessor->macro_capacity == 0 ? 8 : preprocessor->macro_capacity * 2;
        Macro* new_macros = (Macro*)KRT_REALLOC(preprocessor->macros, new_capacity * sizeof(Macro));
        if (!new_macros) return false;
        preprocessor->macros = new_macros;
        preprocessor->macro_capacity = new_capacity;
    }
    
    preprocessor->macros[preprocessor->macro_count].name = KRT_STRDUP(name);
    preprocessor->macros[preprocessor->macro_count].replacement = KRT_STRDUP(replacement);
    preprocessor->macro_count++;
    
    return true;
}

static bool IsIdentifierChar(char c) {
    return isalnum(c) || c == '_';
}

char* PreprocessorProcess(Preprocessor* preprocessor, const char* source) {
    if (!preprocessor || !source) return NULL;
    
    size_t source_len = strlen(source);
    size_t capacity = source_len * 2 + 1;
    char* result = (char*)KRT_MALLOC(capacity);
    if (!result) return NULL;
    
    size_t result_pos = 0;
    const char* src_ptr = source;
    
    while (*src_ptr) {
        bool replaced = false;
        
        for (int i = 0; i < preprocessor->macro_count; i++) {
            if (!preprocessor->macros[i].name) continue;
            size_t name_len = strlen(preprocessor->macros[i].name);
            if (name_len == 0) continue;
            
            if (strncmp(src_ptr, preprocessor->macros[i].name, name_len) == 0) {
                
                if (!IsIdentifierChar(src_ptr[name_len])) {
                    
                    const char* replacement = preprocessor->macros[i].replacement;
                    if (!replacement) replacement = "";
                    size_t replacement_len = strlen(replacement);
                    
                    if (result_pos + replacement_len >= capacity - 1) {
                        size_t new_capacity = (result_pos + replacement_len) * 2 + 1;
                        char* new_result = (char*)KRT_REALLOC(result, new_capacity);
                        if (!new_result) {
                            KRT_FREE(result);
                            return NULL;
                        }
                        result = new_result;
                        capacity = new_capacity;
                    }
                    
                    KRT_STRCPY_S(result + result_pos, capacity - result_pos, replacement);
                    result_pos += replacement_len;
                    src_ptr += name_len;
                    replaced = true;
                    break;
                }
            }
        }
        
        if (!replaced) {
            
            if (result_pos + 1 >= capacity - 1) {
                size_t new_capacity = capacity * 2;
                char* new_result = (char*)KRT_REALLOC(result, new_capacity);
                if (!new_result) {
                    KRT_FREE(result);
                    return NULL;
                }
                result = new_result;
                capacity = new_capacity;
            }
            result[result_pos++] = *src_ptr++;
        }
    }
    
    result[result_pos] = '\0';
    return result;
}
