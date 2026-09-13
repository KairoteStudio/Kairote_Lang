#include "BytecodeGenerator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define KRT_REALLOC(ptr, size) realloc(ptr, size)

struct KrtBytecodeGenerator {
    int dummy;
};

KrtBytecodeGenerator* KrtBytecodeGeneratorCreate(void) {
    KrtBytecodeGenerator* generator = malloc(sizeof(KrtBytecodeGenerator));
    if (!generator) {
        return NULL;
    }
    return generator;
}

void KrtBytecodeGeneratorDestroy(KrtBytecodeGenerator* generator) {
    free(generator);
}

void KrtBytecodeGeneratorInitChunk(KrtChunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->constants.count = 0;
    chunk->constants.capacity = 0;
    chunk->constants.values = NULL;
}

void KrtBytecodeGeneratorWriteByte(KrtChunk* chunk, uint8_t byte, int line) {
    if (chunk->capacity < chunk->count + 1) {
        int old_capacity = chunk->capacity;
        chunk->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        chunk->code = KRT_REALLOC(chunk->code, chunk->capacity);
        chunk->lines = KRT_REALLOC(chunk->lines, chunk->capacity * sizeof(int));
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->count++;
}

void KrtBytecodeGeneratorWriteShort(KrtChunk* chunk, uint16_t value) {
    KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)(value & 0xFF), 0);
    KrtBytecodeGeneratorWriteByte(chunk, (uint8_t)((value >> 8) & 0xFF), 0);
}

int KrtBytecodeGeneratorAddConstant(KrtChunk* chunk, KrtValue value) {
    if (chunk->constants.capacity < chunk->constants.count + 1) {
        int old_capacity = chunk->constants.capacity;
        chunk->constants.capacity = old_capacity < 8 ? 8 : old_capacity * 2;
        chunk->constants.values = KRT_REALLOC(chunk->constants.values, chunk->constants.capacity * sizeof(KrtValue));
    }

    chunk->constants.values[chunk->constants.count] = value;
    return chunk->constants.count++;
}

int KrtBytecodeGeneratorAddStringConstant(KrtChunk* chunk, const char* string) {
    KrtValue value;
    value.type = VAL_STRING_LITERAL;
    value.as.string_literal = string;
    return KrtBytecodeGeneratorAddConstant(chunk, value);
}

void KrtBytecodeGeneratorFreeChunk(KrtChunk* chunk) {
    free(chunk->code);
    free(chunk->lines);
    free(chunk->constants.values);
    KrtBytecodeGeneratorInitChunk(chunk);
}

bool KrtBytecodeGeneratorSerializeToFile(KrtChunk* chunk, const char* filename) {
    FILE* file = fopen(filename, "wb");
    if (!file) {
        return false;
    }

    uint32_t magic = 0x45534243;
    uint16_t version = 1;
    fwrite(&magic, sizeof(uint32_t), 1, file);
    fwrite(&version, sizeof(uint16_t), 1, file);

    uint32_t code_count = chunk->count;
    fwrite(&code_count, sizeof(uint32_t), 1, file);
    if (code_count > 0) {
        fwrite(chunk->code, sizeof(uint8_t), code_count, file);
        fwrite(chunk->lines, sizeof(int), code_count, file);
    }

    uint32_t constant_count = chunk->constants.count;
    fwrite(&constant_count, sizeof(uint32_t), 1, file);
    for (uint32_t i = 0; i < constant_count; i++) {
        KrtValue value = chunk->constants.values[i];
        fwrite(&value.type, sizeof(KrtValueType), 1, file);

        switch (value.type) {
        case VAL_BOOL:
            fwrite(&value.as.boolean, sizeof(bool), 1, file);
            break;
        case VAL_NUMBER:
            fwrite(&value.as.number, sizeof(double), 1, file);
            break;
        case VAL_STRING_LITERAL: {
            uint16_t length = strlen(value.as.string_literal);
            fwrite(&length, sizeof(uint16_t), 1, file);
            fwrite(value.as.string_literal, sizeof(char), length, file);
            break;
        }
        case VAL_NULL:
        case VAL_OBJ:
            break;
        }
    }

    fclose(file);
    return true;
}
