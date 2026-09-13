#include "StackCalculator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "KrtCommon.h"

static KrtStackAnalyzer* g_stack_analyzer = NULL;

KrtStackAnalyzer* KrtStackAnalyzerInit(void) {
    KrtStackAnalyzer* analyzer = KRT_MALLOC(sizeof(KrtStackAnalyzer));
    if (!analyzer) {
        KrtError("Failed to allocate stack analyzer");
        return NULL;
    }

    analyzer->frame_capacity = 16;
    analyzer->frames = KRT_MALLOC(sizeof(KrtStackFrame) * analyzer->frame_capacity);
    if (!analyzer->frames) {
        KrtError("Failed to allocate stack frames");
        KRT_FREE(analyzer);
        return NULL;
    }

    analyzer->frame_count = 0;
    analyzer->current_depth = 0;
    analyzer->max_depth = 0;

    return analyzer;
}

void KrtStackAnalyzerDestroy(KrtStackAnalyzer* analyzer) {
    if (!analyzer) {
        return;
    }

    for (size_t i = 0; i < analyzer->frame_count; i++) {
        KrtStackFrame* frame = &analyzer->frames[i];
        if (frame->usages) {
            KRT_FREE(frame->usages);
        }
    }

    if (analyzer->frames) {
        KRT_FREE(analyzer->frames);
    }

    KRT_FREE(analyzer);
}

KrtStackFrame* KrtStackAnalyzerBeginFunction(KrtStackAnalyzer* analyzer, const char* function_name) {
    if (!analyzer || !function_name) {
        KrtError("Invalid parameters for stack analyzer begin function");
        return NULL;
    }

    if (analyzer->frame_count >= analyzer->frame_capacity) {
        size_t new_capacity = analyzer->frame_capacity * 2;
        KrtStackFrame* new_frames = KRT_REALLOC(analyzer->frames, sizeof(KrtStackFrame) * new_capacity);
        if (!new_frames) {
            KrtError("Failed to expand stack frames array");
            return NULL;
        }
        analyzer->frames = new_frames;
        analyzer->frame_capacity = new_capacity;
    }

    KrtStackFrame* frame = &analyzer->frames[analyzer->frame_count++];
    frame->total_size = 0;
    frame->used_size = 0;
    frame->max_usage = 0;
    frame->usage_count = 0;
    frame->usage_capacity = 16;
    frame->usages = KRT_MALLOC(sizeof(KrtStackUsage) * frame->usage_capacity);

    if (!frame->usages) {
        KrtError("Failed to allocate stack usage array");
        return NULL;
    }

    analyzer->current_depth++;
    if (analyzer->current_depth > analyzer->max_depth) {
        analyzer->max_depth = analyzer->current_depth;
    }
    return frame;
}

void KrtStackAnalyzerEndFunction(KrtStackAnalyzer* analyzer) {
    if (!analyzer || analyzer->current_depth == 0) {
        KrtError("Invalid stack analyzer state for end function");
        return;
    }
    analyzer->current_depth--;
}

void KrtStackFrameAddUsage(KrtStackFrame* frame, size_t size, KrtStackUsageType type, const char* description,
                           const char* file, int line) {
    if (!frame || size == 0) {
        KrtError("Invalid parameters for stack frame add usage");
        return;
    }

    if (frame->usage_count >= frame->usage_capacity) {
        size_t new_capacity = frame->usage_capacity * 2;
        KrtStackUsage* new_usages = KRT_REALLOC(frame->usages, sizeof(KrtStackUsage) * new_capacity);
        if (!new_usages) {
            KrtError("Failed to expand stack usage array");
            return;
        }
        frame->usages = new_usages;
        frame->usage_capacity = new_capacity;
    }

    KrtStackUsage* usage = &frame->usages[frame->usage_count++];
    usage->offset = frame->used_size;
    usage->size = size;
    usage->type = type;
    usage->description = description ? description : "unknown";
    usage->file = file ? file : "unknown";
    usage->line = line;

    frame->used_size += size;
    if (frame->used_size > frame->max_usage) {
        frame->max_usage = frame->used_size;
    }
}

void KrtStackFrameOptimizeLayout(KrtStackFrame* frame) {
    if (!frame) {
        return;
    }

    size_t aligned_size = frame->used_size;
    if (aligned_size % KRT_STACK_ALIGNMENT != 0) {
        aligned_size = ((aligned_size + KRT_STACK_ALIGNMENT - 1) / KRT_STACK_ALIGNMENT) * KRT_STACK_ALIGNMENT;
    }

    if (aligned_size < KRT_MIN_STACK_SIZE) {
        aligned_size = KRT_MIN_STACK_SIZE;
    }

    frame->total_size = aligned_size;
}

size_t KrtStackFrameGetTotalSize(const KrtStackFrame* frame) {
    if (!frame) {
        return KRT_MIN_STACK_SIZE;
    }

    if (frame->total_size == 0) {
        size_t aligned_size = frame->used_size;
        if (aligned_size % KRT_STACK_ALIGNMENT != 0) {
            aligned_size = ((aligned_size + KRT_STACK_ALIGNMENT - 1) / KRT_STACK_ALIGNMENT) * KRT_STACK_ALIGNMENT;
        }
        return aligned_size < KRT_MIN_STACK_SIZE ? KRT_MIN_STACK_SIZE : aligned_size;
    }

    return frame->total_size;
}

void KrtStackFrameGenerateReport(const KrtStackFrame* frame) {
    if (!frame) {
        return;
    }
}

int KrtStackFrameCheckOverflow(const KrtStackFrame* frame, size_t stack_limit) {
    if (!frame) {
        return 0;
    }

    size_t required_size = KrtStackFrameGetTotalSize(frame);

    if (required_size > stack_limit) {
        return 1;
    }

    double usage_ratio = (double)frame->max_usage / stack_limit;
    (void)usage_ratio;

    return 0;
}

size_t KrtCalculateDynamicStackSize(const char* ir_code) {

    if (!ir_code) {
        return KRT_MIN_STACK_SIZE;
    }

    size_t instruction_count = 0;
    const char* ptr = ir_code;
    while (*ptr) {
        if (*ptr == '\n') {
            instruction_count++;
        }
        ptr++;
    }

    size_t base_size = KRT_MIN_STACK_SIZE;
    size_t dynamic_size = (instruction_count / 10) * 64;

    size_t total_size = base_size + dynamic_size;

    if (total_size % KRT_STACK_ALIGNMENT != 0) {
        total_size = ((total_size + KRT_STACK_ALIGNMENT - 1) / KRT_STACK_ALIGNMENT) * KRT_STACK_ALIGNMENT;
    }

    return total_size;
}

size_t KrtPredictStackUsage(const char* function_signature __attribute__((unused)), size_t param_count,
                            size_t local_var_count) {

    size_t base_size = 16;
    size_t param_size = param_count > 6 ? (param_count - 6) * 8 : 0;
    size_t local_size = local_var_count * 8;
    size_t register_size = 5 * 8;

    size_t total_size = base_size + param_size + local_size + register_size;

    if (total_size < KRT_MIN_STACK_SIZE) {
        total_size = KRT_MIN_STACK_SIZE;
    }

    if (total_size % KRT_STACK_ALIGNMENT != 0) {
        total_size = ((total_size + KRT_STACK_ALIGNMENT - 1) / KRT_STACK_ALIGNMENT) * KRT_STACK_ALIGNMENT;
    }

    return total_size;
}

KrtStackAnalyzer* KrtGetGlobalStackAnalyzer(void) {
    if (!g_stack_analyzer) {
        g_stack_analyzer = KrtStackAnalyzerInit();
    }
    return g_stack_analyzer;
}

void KrtCleanupGlobalStackAnalyzer(void) {
    if (g_stack_analyzer) {
        KrtStackAnalyzerDestroy(g_stack_analyzer);
        g_stack_analyzer = NULL;
    }
}
