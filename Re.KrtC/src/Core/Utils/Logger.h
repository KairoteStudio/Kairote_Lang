#ifndef KRT_LOGGER_H
#define KRT_LOGGER_H

#include <stdio.h>
#include <stdarg.h>

int KrtPrintf(const char* format, ...);
int KrtPrintFormat(const char* format, ...);
int KrtFprintf(FILE* stream, const char* format, ...);
#endif
