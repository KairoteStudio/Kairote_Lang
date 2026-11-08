#include "Preprocessor.h"
#include <string.h>
#include <ctype.h>

static bool is_identifier_char(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 128;
}

static unsigned macro_hash(const char* name, size_t length) {
    unsigned hash = 2166136261u;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ (unsigned char)name[i]) * 16777619u;
    }
    return hash;
}

Preprocessor* PreprocessorCreate(void) {
    return (Preprocessor*)KRT_CALLOC(1, sizeof(Preprocessor));
}

void PreprocessorDestroy(Preprocessor* preprocessor) {
    if (!preprocessor) {
        return;
    }
    for (int i = 0; i < preprocessor->macro_count; i++) {
        KRT_FREE(preprocessor->macros[i].name);
        KRT_FREE(preprocessor->macros[i].replacement);
    }
    KRT_FREE(preprocessor->macros);
    KRT_FREE(preprocessor->buckets);
    KRT_FREE(preprocessor);
}

static int find_macro(Preprocessor* preprocessor, const char* name, size_t length, unsigned hash) {
    if (!preprocessor->bucket_count) {
        return -1;
    }
    unsigned bucket = hash & (preprocessor->bucket_count - 1);
    while (preprocessor->buckets[bucket]) {
        int index = preprocessor->buckets[bucket] - 1;
        Macro* macro = &preprocessor->macros[index];
        if (macro->hash == hash && macro->name_length == length && memcmp(macro->name, name, length) == 0) {
            return index;
        }
        bucket = (bucket + 1) & (preprocessor->bucket_count - 1);
    }
    return -1;
}

static bool grow_macro_index(Preprocessor* preprocessor) {
    int count = preprocessor->bucket_count ? preprocessor->bucket_count * 2 : 16;
    int* buckets = (int*)KRT_CALLOC(count, sizeof(int));
    if (!buckets) {
        return false;
    }
    for (int i = 0; i < preprocessor->macro_count; i++) {
        unsigned bucket = preprocessor->macros[i].hash & (count - 1);
        while (buckets[bucket]) {
            bucket = (bucket + 1) & (count - 1);
        }
        buckets[bucket] = i + 1;
    }
    KRT_FREE(preprocessor->buckets);
    preprocessor->buckets = buckets;
    preprocessor->bucket_count = count;
    return true;
}

bool PreprocessorAddMacro(Preprocessor* preprocessor, const char* name, const char* replacement) {
    if (!preprocessor || !name || !replacement || !*name || isdigit((unsigned char)*name)) {
        return false;
    }
    size_t length = strlen(name);
    for (size_t i = 0; i < length; i++) {
        if (!is_identifier_char((unsigned char)name[i])) {
            return false;
        }
    }
    unsigned hash = macro_hash(name, length);
    int existing = find_macro(preprocessor, name, length, hash);
    char* copy = KRT_STRDUP(replacement);
    if (!copy) {
        return false;
    }
    if (existing >= 0) {
        KRT_FREE(preprocessor->macros[existing].replacement);
        preprocessor->macros[existing].replacement = copy;
        preprocessor->macros[existing].replacement_length = strlen(replacement);
        return true;
    }
    if ((preprocessor->macro_count + 1) * 4 >= preprocessor->bucket_count * 3 && !grow_macro_index(preprocessor)) {
        KRT_FREE(copy);
        return false;
    }
    if (preprocessor->macro_count == preprocessor->macro_capacity) {
        int capacity = preprocessor->macro_capacity ? preprocessor->macro_capacity * 2 : 8;
        Macro* macros = (Macro*)KRT_REALLOC(preprocessor->macros, capacity * sizeof(Macro));
        if (!macros) {
            KRT_FREE(copy);
            return false;
        }
        preprocessor->macros = macros;
        preprocessor->macro_capacity = capacity;
    }
    char* name_copy = KRT_STRDUP(name);
    if (!name_copy) {
        KRT_FREE(copy);
        return false;
    }
    int index = preprocessor->macro_count++;
    preprocessor->macros[index] = (Macro){name_copy, copy, length, strlen(replacement), hash};
    unsigned bucket = hash & (preprocessor->bucket_count - 1);
    while (preprocessor->buckets[bucket]) {
        bucket = (bucket + 1) & (preprocessor->bucket_count - 1);
    }
    preprocessor->buckets[bucket] = index + 1;
    return true;
}

char* PreprocessorProcess(Preprocessor* preprocessor, const char* source) {
    if (!preprocessor || !source) {
        return NULL;
    }
    if (!preprocessor->macro_count) {
        return KRT_STRDUP(source);
    }
    size_t capacity = strlen(source) + 1, used = 0;
    char* result = (char*)KRT_MALLOC(capacity);
    if (!result) {
        return NULL;
    }
    const char* cursor = source;
    while (*cursor) {
        const char* chunk = cursor;
        size_t length;
        if (*cursor == '"' || *cursor == '\'') {
            char quote = *cursor++;
            while (*cursor && *cursor != quote) {
                if (*cursor == '\\' && cursor[1]) {
                    cursor++;
                }
                cursor++;
            }
            if (*cursor) {
                cursor++;
            }
        } else if (cursor[0] == '/' && cursor[1] == '/') {
            while (*cursor && *cursor != '\n') {
                cursor++;
            }
        } else if (cursor[0] == '/' && cursor[1] == '*') {
            cursor += 2;
            while (*cursor && !(cursor[0] == '*' && cursor[1] == '/')) {
                cursor++;
            }
            if (*cursor) {
                cursor += 2;
            }
        } else if (is_identifier_char((unsigned char)*cursor)) {
            unsigned hash = 2166136261u;
            do {
                hash = (hash ^ (unsigned char)*cursor++) * 16777619u;
            } while (is_identifier_char((unsigned char)*cursor));
            length = cursor - chunk;
            int index = find_macro(preprocessor, chunk, length, hash);
            if (index >= 0) {
                chunk = preprocessor->macros[index].replacement;
                length = preprocessor->macros[index].replacement_length;
                goto append;
            }
        } else {
            cursor++;
        }
        length = cursor - chunk;
    append:
        if (used + length + 1 > capacity) {
            size_t next = (used + length + 1) * 2;
            char* resized = (char*)KRT_REALLOC(result, next);
            if (!resized) {
                KRT_FREE(result);
                return NULL;
            }
            result = resized;
            capacity = next;
        }
        memcpy(result + used, chunk, length);
        used += length;
    }
    result[used] = '\0';
    return result;
}
