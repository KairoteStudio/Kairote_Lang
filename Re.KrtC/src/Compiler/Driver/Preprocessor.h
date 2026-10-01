#ifndef KRT_PREPROCESSOR_H
#define KRT_PREPROCESSOR_H

#include "../../Core/Utils/KrtCommon.h"

typedef struct {
    char* name;
    char* replacement;
    size_t name_length;
    size_t replacement_length;
    unsigned hash;
} Macro;

typedef struct {
    Macro* macros;
    int macro_count;
    int macro_capacity;
    int* buckets;
    int bucket_count;
} Preprocessor;

Preprocessor* PreprocessorCreate(void);
void PreprocessorDestroy(Preprocessor* preprocessor);
bool PreprocessorAddMacro(Preprocessor* preprocessor, const char* name, const char* replacement);
char* PreprocessorProcess(Preprocessor* preprocessor, const char* source);

#endif
