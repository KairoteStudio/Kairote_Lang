#include "TaskManager.h"
#include "ConsoleUtils.h"
#include "../../Core/Utils/KrtCommon.h"
#include "../../Core/Utils/OutputCache.h"
#include <stdio.h>
#include <string.h>

static KrtBuildSummary g_build_summary = {0};
static int g_current_task_line_active = 0;

void KrtTaskReport(const char* stage, const char* file, KrtTaskResult result, double duration, KrtTaskStats* stats) {
    if (!stage || !stats) {
        return;
    }

    const char* status = "";
    const char* color = KRT_COL_RESET;
    switch (result) {
    case KRT_TASK_RESULT_EXECUTED:
        stats->executed++;
        break;
    case KRT_TASK_RESULT_UP_TO_DATE:
        stats->up_to_date++;
        status = " UP-TO-DATE";
        color = KRT_COL_YELLOW;
        break;
    case KRT_TASK_RESULT_SKIPPED:
        stats->skipped++;
        status = " SKIPPED";
        color = KRT_COL_GRAY;
        break;
    case KRT_TASK_RESULT_FAILED:
        stats->executed++;
        stats->failed++;
        break;
    }

    if (g_current_task_line_active) {
        KrtPrintf(ANSI_CURSOR_UP ANSI_CLEAR_LINE);
    }

    if (result == KRT_TASK_RESULT_EXECUTED || result == KRT_TASK_RESULT_UP_TO_DATE ||
        result == KRT_TASK_RESULT_SKIPPED) {
        KrtPrintf("%s>%s %s", KrtColor(KRT_COL_CYAN), KrtColor(KRT_COL_RESET), stage);
        if (file && file[0] != '\0') {
            KrtPrintf(" %s%s%s", KrtColor(KRT_COL_GRAY), file, KrtColor(KRT_COL_RESET));
        }
        if (status[0] != '\0') {
            KrtPrintf(" %s%s%s", KrtColor(color), status, KrtColor(KRT_COL_RESET));
        }
        if (duration >= 0.0 && duration > 0.01) {
            KrtPrintf(" %s%.1fs%s", KrtColor(KRT_COL_GRAY), duration, KrtColor(KRT_COL_RESET));
        }
        KrtPrintf("\n");
        g_current_task_line_active = 1;
    } else if (result == KRT_TASK_RESULT_FAILED) {
        KrtPrintf("%s>%s %s", KrtColor(KRT_COL_CYAN), KrtColor(KRT_COL_RESET), stage);
        if (file && file[0] != '\0') {
            KrtPrintf(" %s%s%s", KrtColor(KRT_COL_GRAY), file, KrtColor(KRT_COL_RESET));
        }
        KrtPrintf(" %sFAILED%s\n", KrtColor(KRT_COL_RED), KrtColor(KRT_COL_RESET));
        g_current_task_line_active = 0;
    }

    if (result == KRT_TASK_RESULT_FAILED) {
        KrtOutputCacheFlush();
        g_current_task_line_active = 0;
    }
}

static void show_idle_status(void) {
    if (g_current_task_line_active) {
        KrtPrintf(ANSI_CURSOR_UP ANSI_CLEAR_LINE);
        KrtPrintf("%s>%s %sIDLE%s\n", KrtColor(KRT_COL_CYAN), KrtColor(KRT_COL_RESET), KrtColor(KRT_COL_GRAY),
                  KrtColor(KRT_COL_RESET));
        g_current_task_line_active = 0;
    }
}

void KrtBuildSummaryReset(void) {
    memset(&g_build_summary, 0, sizeof(g_build_summary));
    g_current_task_line_active = 0;
}

void KrtBuildSummaryAccumulate(const KrtTaskStats* stats, double duration, int failed) {
    if (!stats) {
        return;
    }
    g_build_summary.stats.executed += stats->executed;
    g_build_summary.stats.up_to_date += stats->up_to_date;
    g_build_summary.stats.skipped += stats->skipped;
    g_build_summary.stats.failed += stats->failed;
    g_build_summary.total_duration += duration;
    if (failed) {
        g_build_summary.failed = 1;
    }
}

void KrtPrintBuildSummary(void) {
    int total_tasks = g_build_summary.stats.executed + g_build_summary.stats.up_to_date + g_build_summary.stats.skipped;
    if (total_tasks <= 0) {
        return;
    }

    show_idle_status();
    KrtOutputCacheFlush();

    const char* status_text = g_build_summary.failed ? "FAILED" : "SUCCESSFUL";
    const char* status_color = g_build_summary.failed ? KRT_COL_RED : KRT_COL_GREEN;

    KrtPrintf("\n%s%sBUILD %s%s in %.1fs\n", KrtColor(KRT_COL_BOLD), KrtColor(status_color), status_text,
              KrtColor(KRT_COL_RESET), g_build_summary.total_duration);

    int executed = g_build_summary.stats.executed;
    int up_to_date = g_build_summary.stats.up_to_date;
    int skipped = g_build_summary.stats.skipped;
    int failed = g_build_summary.stats.failed;

    if (total_tasks > 0) {
        KrtPrintf("%s%d tasks: %s", KrtColor(KRT_COL_GRAY), total_tasks, KrtColor(KRT_COL_RESET));

        int first = 1;
        if (executed > 0) {
            KrtPrintf("%s%d executed%s", KrtColor(KRT_COL_GREEN), executed, KrtColor(KRT_COL_RESET));
            first = 0;
        }
        if (up_to_date > 0) {
            if (!first) {
                KrtPrintf(", ");
            }
            KrtPrintf("%s%d up-to-date%s", KrtColor(KRT_COL_YELLOW), up_to_date, KrtColor(KRT_COL_RESET));
            first = 0;
        }
        if (skipped > 0) {
            if (!first) {
                KrtPrintf(", ");
            }
            KrtPrintf("%s%d skipped%s", KrtColor(KRT_COL_GRAY), skipped, KrtColor(KRT_COL_RESET));
            first = 0;
        }
        if (failed > 0) {
            if (!first) {
                KrtPrintf(", ");
            }
            KrtPrintf("%s%d failed%s", KrtColor(KRT_COL_RED), failed, KrtColor(KRT_COL_RESET));
            first = 0;
        }
        KrtPrintf("\n");
    }
}

void KrtBuildSummarySetDuration(double duration) {
    g_build_summary.total_duration = duration;
}

void KrtBuildSummarySetFailed(int failed) {
    g_build_summary.failed = failed;
}

KrtTaskStats* KrtGetGlobalTaskStats(void) {
    return &g_build_summary.stats;
}
