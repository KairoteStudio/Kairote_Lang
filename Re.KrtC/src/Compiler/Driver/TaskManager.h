#ifndef TASK_MANAGER_H
#define TASK_MANAGER_H

#include <stddef.h>

typedef enum {
    KRT_TASK_RESULT_EXECUTED = 0,
    KRT_TASK_RESULT_UP_TO_DATE,
    KRT_TASK_RESULT_SKIPPED,
    KRT_TASK_RESULT_FAILED
} KrtTaskResult;

typedef struct {
    int executed;
    int up_to_date;
    int skipped;
    int failed;
} KrtTaskStats;

typedef struct {
    KrtTaskStats stats;
    double total_duration;
    int failed;
} KrtBuildSummary;

void KrtTaskReport(const char* stage, const char* file, KrtTaskResult result, double duration, KrtTaskStats* stats);
void KrtBuildSummaryReset(void);
void KrtBuildSummaryAccumulate(const KrtTaskStats* stats, double duration, int failed);
void KrtPrintBuildSummary(void);
void KrtBuildSummarySetDuration(double duration);
void KrtBuildSummarySetFailed(int failed);
KrtTaskStats* KrtGetGlobalTaskStats(void);

#endif
