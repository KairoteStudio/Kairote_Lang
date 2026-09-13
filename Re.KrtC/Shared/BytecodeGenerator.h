#ifndef KRT_BYTECODE_GENERATOR_H
#define KRT_BYTECODE_GENERATOR_H
#include "Bytecode.h"
#include <stdbool.h>
typedef struct KrtBytecodeGenerator KrtBytecodeGenerator;
KrtBytecodeGenerator* KrtBytecodeGeneratorCreate(void);
void KrtBytecodeGeneratorDestroy(KrtBytecodeGenerator* generator);
void KrtBytecodeGeneratorInitChunk(KrtChunk* chunk);
void KrtBytecodeGeneratorWriteByte(KrtChunk* chunk, uint8_t byte, int line);
void KrtBytecodeGeneratorWriteShort(KrtChunk* chunk, uint16_t value);
int KrtBytecodeGeneratorAddConstant(KrtChunk* chunk, KrtValue value);
int KrtBytecodeGeneratorAddStringConstant(KrtChunk* chunk, const char* string);
void KrtBytecodeGeneratorFreeChunk(KrtChunk* chunk);
bool KrtBytecodeGeneratorSerializeToFile(KrtChunk* chunk, const char* filename);
#endif
