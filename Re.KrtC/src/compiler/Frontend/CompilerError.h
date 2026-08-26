#ifndef KRT_COMPILER_ERROR_H
#define KRT_COMPILER_ERROR_H

#include <stdbool.h>

typedef enum {
    KRT_ERROR_NOTE,
    KRT_ERROR_WARNING,
    KRT_ERROR_ERROR,
    KRT_ERROR_FATAL
} KrtErrorSeverity;

typedef enum {
    KRT_ERROR_STAGE_UNKNOWN,
    KRT_ERROR_STAGE_LEX,
    KRT_ERROR_STAGE_PARSE,
    KRT_ERROR_STAGE_SEMANTIC,
    KRT_ERROR_STAGE_TYPE_CHECK,
    KRT_ERROR_STAGE_CODEGEN,
    KRT_ERROR_STAGE_SSA
} KrtErrorStage;

typedef struct {
    KrtErrorSeverity severity;
    KrtErrorStage stage;
    int line;
    int column;
    int end_line;
    int end_column;
    char message[512];
    char hint[256];
    char source_line[256];
    char file_path[256];
    char error_code[32];
} KrtCompileError;

typedef struct {
    KrtCompileError* errors;
    int error_count;
    int error_capacity;
    int warning_count;
    char* source_code;
    char file_path[256];
} KrtErrorReport;

KrtErrorReport* KrtErrorReportCreate(void);
void KrtErrorReportDestroy(KrtErrorReport* report);
void KrtErrorReportSetSourceCode(KrtErrorReport* report, const char* source_code);
void KrtErrorReportSetFilePath(KrtErrorReport* report, const char* file_path);
void KrtErrorReportAdd(KrtErrorReport* report, KrtErrorSeverity severity,
                       KrtErrorStage stage, int line, int column,
                       const char* message, const char* hint);
void KrtErrorReportAddEx(KrtErrorReport* report, KrtErrorSeverity severity,
                          KrtErrorStage stage, int line, int column,
                          int end_line, int end_column,
                          const char* error_code, const char* message, 
                          const char* hint);
void KrtErrorReportPrint(KrtErrorReport* report);
const char* KrtErrorStageName(KrtErrorStage stage);
const char* KrtErrorSeverityName(KrtErrorSeverity severity);

#endif
