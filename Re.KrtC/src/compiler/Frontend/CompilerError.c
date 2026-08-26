#include "CompilerError.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KRT_ERROR_INITIAL_CAPACITY 16

#define KRT_COL_RED     "\033[31m"
#define KRT_COL_GREEN   "\033[32m"
#define KRT_COL_YELLOW  "\033[33m"
#define KRT_COL_BLUE    "\033[34m"
#define KRT_COL_MAGENTA "\033[35m"
#define KRT_COL_CYAN    "\033[36m"
#define KRT_COL_WHITE   "\033[37m"
#define KRT_COL_GRAY    "\033[90m"
#define KRT_COL_BOLD    "\033[1m"
#define KRT_COL_RESET   "\033[0m"

const char* KrtErrorStageName(KrtErrorStage stage) {
    switch (stage) {
        case KRT_ERROR_STAGE_LEX:        return "lex";
        case KRT_ERROR_STAGE_PARSE:      return "parse";
        case KRT_ERROR_STAGE_SEMANTIC:   return "semantic";
        case KRT_ERROR_STAGE_TYPE_CHECK: return "typecheck";
        case KRT_ERROR_STAGE_CODEGEN:    return "codegen";
        case KRT_ERROR_STAGE_SSA:        return "ssa";
        default:                         return "unknown";
    }
}

const char* KrtErrorSeverityName(KrtErrorSeverity severity) {
    switch (severity) {
        case KRT_ERROR_NOTE:    return "note";
        case KRT_ERROR_WARNING: return "warning";
        case KRT_ERROR_ERROR:   return "error";
        case KRT_ERROR_FATAL:   return "fatal";
        default:                return "unknown";
    }
}

KrtErrorReport* KrtErrorReportCreate(void) {
    KrtErrorReport* report = (KrtErrorReport*)malloc(sizeof(KrtErrorReport));
    if (!report) return NULL;
    
    report->errors = (KrtCompileError*)malloc(KRT_ERROR_INITIAL_CAPACITY * sizeof(KrtCompileError));
    if (!report->errors) {
        free(report);
        return NULL;
    }
    
    report->error_count = 0;
    report->error_capacity = KRT_ERROR_INITIAL_CAPACITY;
    report->warning_count = 0;
    report->source_code = NULL;
    report->file_path[0] = '\0';
    
    return report;
}

void KrtErrorReportDestroy(KrtErrorReport* report) {
    if (!report) return;
    if (report->errors) {
        free(report->errors);
    }
    if (report->source_code) {
        free(report->source_code);
    }
    free(report);
}

void KrtErrorReportSetSourceCode(KrtErrorReport* report, const char* source_code) {
    if (!report) return;
    
    if (report->source_code) {
        free(report->source_code);
        report->source_code = NULL;
    }
    
    if (source_code) {
        report->source_code = strdup(source_code);
    }
}

void KrtErrorReportSetFilePath(KrtErrorReport* report, const char* file_path) {
    if (!report) return;
    
    if (file_path) {
        strncpy(report->file_path, file_path, sizeof(report->file_path) - 1);
        report->file_path[sizeof(report->file_path) - 1] = '\0';
    } else {
        report->file_path[0] = '\0';
    }
}

static const char* get_severity_color(KrtErrorSeverity severity) {
    switch (severity) {
        case KRT_ERROR_NOTE:    return KRT_COL_CYAN;
        case KRT_ERROR_WARNING: return KRT_COL_YELLOW;
        case KRT_ERROR_ERROR:   return KRT_COL_RED;
        case KRT_ERROR_FATAL:   return KRT_COL_MAGENTA;
        default:                return KRT_COL_WHITE;
    }
}

static void init_error(KrtCompileError* error) {
    memset(error, 0, sizeof(KrtCompileError));
}

void KrtErrorReportAdd(KrtErrorReport* report, KrtErrorSeverity severity,
                       KrtErrorStage stage, int line, int column,
                       const char* message, const char* hint) {
    KrtErrorReportAddEx(report, severity, stage, line, column, line, column, "", message, hint);
}

void KrtErrorReportAddEx(KrtErrorReport* report, KrtErrorSeverity severity,
                          KrtErrorStage stage, int line, int column,
                          int end_line, int end_column,
                          const char* error_code, const char* message, 
                          const char* hint) {
    if (!report || !message) return;
    
    if (report->error_count >= report->error_capacity) {
        int new_capacity = report->error_capacity * 2;
        KrtCompileError* new_errors = (KrtCompileError*)realloc(report->errors, new_capacity * sizeof(KrtCompileError));
        if (!new_errors) return;
        report->errors = new_errors;
        report->error_capacity = new_capacity;
    }
    
    KrtCompileError* error = &report->errors[report->error_count++];
    init_error(error);
    
    error->severity = severity;
    error->stage = stage;
    error->line = line;
    error->column = column;
    error->end_line = end_line;
    error->end_column = end_column;
    
    strncpy(error->message, message, sizeof(error->message) - 1);
    error->message[sizeof(error->message) - 1] = '\0';
    
    if (hint) {
        strncpy(error->hint, hint, sizeof(error->hint) - 1);
        error->hint[sizeof(error->hint) - 1] = '\0';
    }
    
    if (error_code) {
        strncpy(error->error_code, error_code, sizeof(error->error_code) - 1);
        error->error_code[sizeof(error->error_code) - 1] = '\0';
    }
    
    if (report->file_path[0] != '\0') {
        strncpy(error->file_path, report->file_path, sizeof(error->file_path) - 1);
        error->file_path[sizeof(error->file_path) - 1] = '\0';
    }
    
    if (severity == KRT_ERROR_WARNING) {
        report->warning_count++;
    }
}

static const char* get_source_line(KrtErrorReport* report, int line, char* buf, int buf_size) {
    if (!report || !report->source_code || line <= 0) return NULL;
    
    const char* pos = report->source_code;
    int current_line = 1;
    
    while (pos && current_line < line) {
        const char* next = strchr(pos, '\n');
        if (!next) return NULL;
        pos = next + 1;
        current_line++;
    }
    
    if (!pos) return NULL;
    
    const char* line_end = strchr(pos, '\n');
    int line_len = line_end ? (int)(line_end - pos) : (int)strlen(pos);
    if (line_len >= buf_size) line_len = buf_size - 1;
    
    strncpy(buf, pos, line_len);
    buf[line_len] = '\0';
    
    return buf;
}

static const char* get_filename(const char* path) {
    if (!path) return "unknown";
    
    const char* filename = strrchr(path, '/');
    if (filename) {
        return filename + 1;
    }
    
    filename = strrchr(path, '\\');
    if (filename) {
        return filename + 1;
    }
    
    return path;
}

void KrtErrorReportPrint(KrtErrorReport* report) {
    if (!report || report->error_count == 0) return;
    
    int error_count = 0;
    int warning_count = report->warning_count;
    for (int i = 0; i < report->error_count; i++) {
        if (report->errors[i].severity >= KRT_ERROR_ERROR) {
            error_count++;
        }
    }
    
    fprintf(stderr, "%s%d%s %s, %s%d%s %s\n",
            KRT_COL_RED, error_count, KRT_COL_RESET,
            error_count == 1 ? "error" : "errors",
            KRT_COL_YELLOW, warning_count, KRT_COL_RESET,
            warning_count == 1 ? "warning" : "warnings");
    
    for (int i = 0; i < report->error_count; i++) {
        KrtCompileError* error = &report->errors[i];
        const char* sev_color = get_severity_color(error->severity);
        const char* sev_name = KrtErrorSeverityName(error->severity);
        
        const char* filename = get_filename(error->file_path[0] ? error->file_path : report->file_path);
        
        if (error->line > 0) {
            if (error->end_line > 0 && error->end_column > 0 && 
                (error->end_line != error->line || error->end_column != error->column)) {
                fprintf(stderr, "  %s(%d,%d,%d,%d): %s%s: %s%s\n",
                        filename,
                        error->line, error->column,
                        error->end_line, error->end_column,
                        sev_color, sev_name,
                        error->message, KRT_COL_RESET);
            } else {
                fprintf(stderr, "  %s(%d,%d): %s%s: %s%s\n",
                        filename,
                        error->line, error->column,
                        sev_color, sev_name,
                        error->message, KRT_COL_RESET);
            }
        } else {
            fprintf(stderr, "  %s: %s%s: %s%s\n",
                    filename,
                    sev_color, sev_name,
                    error->message, KRT_COL_RESET);
        }
        
        if (error->hint[0] != '\0') {
            fprintf(stderr, "    %s->%s %s\n", KRT_COL_CYAN, KRT_COL_RESET, error->hint);
        }
        
        char line_buf[256] = {0};
        const char* source_line = error->source_line;
        
        if (source_line[0] == '\0' && report->source_code && error->line > 0) {
            source_line = get_source_line(report, error->line, line_buf, sizeof(line_buf));
        }
        
        if (source_line && source_line[0] != '\0') {
            fprintf(stderr, "   %s%4d |%s %s\n", KRT_COL_GRAY, error->line, KRT_COL_RESET, source_line);
            
            if (error->column > 0) {
                fprintf(stderr, "     %s|%s ", KRT_COL_GRAY, KRT_COL_RESET);
                int spaces = error->column - 1;
                if (spaces > 60) spaces = 60;
                for (int j = 0; j < spaces; j++) {
                    fprintf(stderr, " ");
                }
                fprintf(stderr, "%s^%s\n", KRT_COL_RED, KRT_COL_RESET);
            }
        }
        
        if (i < report->error_count - 1) {
            fprintf(stderr, "\n");
        }
    }
    
    fflush(stderr);
}
