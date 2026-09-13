#ifndef KRT_STACK_CALCULATOR_H
#define KRT_STACK_CALCULATOR_H

#include <stddef.h>
#include <stdint.h>

#define KRT_STACK_ALIGNMENT 16
#define KRT_MIN_STACK_SIZE 128
#define KRT_MAX_STACK_DEPTH 1000

typedef enum {
    KRT_STACK_TYPE_LOCAL_VAR = 0,
    KRT_STACK_TYPE_TEMP_VALUE,
    KRT_STACK_TYPE_SAVED_REG,
    KRT_STACK_TYPE_CALL_FRAME,
    KRT_STACK_TYPE_ALIGNMENT_PAD
} KrtStackUsageType;

typedef struct {
    size_t offset;
    size_t size;
    KrtStackUsageType type;
    const char* description;
    const char* file;
    int line;
} KrtStackUsage;

typedef struct {
    size_t total_size;
    size_t used_size;
    size_t max_usage;
    KrtStackUsage* usages;
    size_t usage_count;
    size_t usage_capacity;
} KrtStackFrame;

typedef struct {
    KrtStackFrame* frames;
    size_t frame_count;
    size_t frame_capacity;
    size_t current_depth;
    size_t max_depth;
} KrtStackAnalyzer;

KrtStackAnalyzer* KrtStackAnalyzerInit(void);

void KrtStackAnalyzerDestroy(KrtStackAnalyzer* analyzer);

KrtStackFrame* KrtStackAnalyzerBeginFunction(KrtStackAnalyzer* analyzer, const char* function_name);

void KrtStackAnalyzerEndFunction(KrtStackAnalyzer* analyzer);

void KrtStackFrameAddUsage(KrtStackFrame* frame, size_t size, KrtStackUsageType type, const char* description,
                           const char* file, int line);

void KrtStackFrameOptimizeLayout(KrtStackFrame* frame);

size_t KrtStackFrameGetTotalSize(const KrtStackFrame* frame);

void KrtStackFrameGenerateReport(const KrtStackFrame* frame);

int KrtStackFrameCheckOverflow(const KrtStackFrame* frame, size_t stack_limit);

size_t KrtCalculateDynamicStackSize(const char* ir_code);

size_t KrtPredictStackUsage(const char* function_signature, size_t param_count, size_t local_var_count);

KrtStackAnalyzer* KrtGetGlobalStackAnalyzer(void);

void KrtCleanupGlobalStackAnalyzer(void);

#endif
