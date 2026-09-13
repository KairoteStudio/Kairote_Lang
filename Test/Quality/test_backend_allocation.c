#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../Re.KrtC/src/Core/Memory/Allocator.h"
#include "../../Re.KrtC/src/Core/Utils/KrtCommon.h"

static size_t allocation_index;
static size_t failure_index;
static size_t live_allocations;

static void* failing_malloc(size_t size) {
    if (++allocation_index == failure_index) {
        return NULL;
    }
    void* result = malloc(size);
    live_allocations += result != NULL;
    return result;
}

static void* failing_calloc(size_t count, size_t size) {
    if (++allocation_index == failure_index) {
        return NULL;
    }
    void* result = calloc(count, size);
    live_allocations += result != NULL;
    return result;
}

static void* failing_realloc(void* previous, size_t size) {
    if (++allocation_index == failure_index) {
        return NULL;
    }
    bool is_new = previous == NULL;
    void* result = realloc(previous, size);
    live_allocations += is_new && result != NULL;
    return result;
}

static void tracking_free(void* allocation) {
    if (allocation) {
        assert(live_allocations > 0);
        live_allocations--;
    }
    free(allocation);
}

#undef KRT_MALLOC
#undef KRT_CALLOC
#undef KRT_REALLOC
#undef KRT_FREE
#define KRT_MALLOC(size) failing_malloc(size)
#define KRT_CALLOC(count, size) failing_calloc(count, size)
#define KRT_REALLOC(previous, size) failing_realloc(previous, size)
#define KRT_FREE(allocation) tracking_free(allocation)
#include "../../Re.KrtC/src/compiler/Backend/X86/X86RegAlloc.c"

#define malloc failing_malloc
#define calloc failing_calloc
#define realloc failing_realloc
#define free tracking_free
#include "../../ArkLink/src/Backend/BackendCommon.c"
#include "../../ArkLink/src/Backend/BackendPe.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

static void check_register_pressure(void) {
    ConflictGraph* graph = conflict_graph_create();
    for (int left = 0; left < 30; left++) {
        for (int right = left + 1; right < 30; right++) {
            conflict_graph_add_edge(graph, left, right);
        }
    }
    RegAllocResult* result = regalloc_allocate_with_capacity(graph, 30, NULL);
    assert(result->num_spilled == 30 - X86_NUM_ALLOCABLE_REGS);
    for (int left = 0; left < 30; left++) {
        for (int right = left + 1; right < 30; right++) {
            assert(result->temp_to_reg[left] < 0 || result->temp_to_reg[right] < 0 ||
                   result->temp_to_reg[left] != result->temp_to_reg[right]);
        }
    }
    regalloc_result_destroy(result);
    conflict_graph_destroy(graph);
    assert(live_allocations == 0);
}

static void check_fatal_allocation(int kind) {
    int diagnostics[2];
    assert(pipe(diagnostics) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(diagnostics[0]);
        assert(dup2(diagnostics[1], STDERR_FILENO) >= 0);
        close(diagnostics[1]);
        allocation_index = 0;
        failure_index = 1;
        if (kind == 0) {
            backend_allocate(32);
        } else if (kind == 1) {
            backend_allocate_zeroed(4, 8);
        } else {
            backend_reallocate(NULL, 32);
        }
        _exit(99);
    }
    close(diagnostics[1]);
    char message[256] = {0};
    assert(read(diagnostics[0], message, sizeof(message) - 1) > 0);
    close(diagnostics[0]);
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    assert(strstr(message, "[KRT_COMPILE_ERROR] Backend") != NULL);
}

static void check_pe_allocation_failures(void) {
    uint8_t code[16] = {0xc3};
    ArkSectionBuffer section = {.data = code,
                                .size = sizeof(code),
                                .capacity = sizeof(code),
                                .kind = ARK_SECTION_CODE,
                                .flags = ARK_SECTION_READ | ARK_SECTION_EXEC,
                                .alignment = 16};
    ArkImportEntry import = {.module = "kernel32.dll", .symbol = "ExitProcess", .is_function = 1};
    ArkExportEntry export = {.name = "entry", .ordinal = 1, .section_index = 0, .is_function = 1};
    ArkResolverSymbol symbol = {.section_index = 0, .value = 0};
    ArkResolverReloc reloc = {.section_index = 0, .offset = 8, .type = ARK_RELOC_ABS64, .symbol = &symbol};
    char context_placeholder;
    size_t allocation_count = 0;
    for (size_t index = 0; index <= allocation_count; index++) {
        allocation_index = 0;
        failure_index = index;
        ArkBackendInput input = {.sections = &section,
                                 .section_count = 1,
                                 .entry_section = 0,
                                 .imports = &import,
                                 .import_count = 1,
                                 .exports = &export,
                                 .export_count = 1,
                                 .export_ordinal_base = 1,
                                 .relocs = &reloc,
                                 .reloc_count = 1,
                                 .image_base = 0x140000000,
                                 .output_type = ARK_OUTPUT_EXECUTABLE};
        ArkBackendOutput output = {0};
        ArkLinkResult result = ark_backend_pe_link((ArkLinkContext*)&context_placeholder, &input, &output);
        if (index == 0) {
            assert(result == ARK_LINK_OK);
            assert(output.size > 2 && memcmp(output.data, "MZ", 2) == 0);
            allocation_count = allocation_index;
        } else {
            assert(result == ARK_LINK_ERR_MEMORY);
            assert(output.data == NULL && output.section_maps == NULL && output.size == 0);
        }
        tracking_free(output.data);
        tracking_free(output.section_maps);
        assert(live_allocations == 0);
    }
    failure_index = 0;
    printf("PE allocation failures checked: %zu\n", allocation_count);
}

int main(void) {
    check_register_pressure();
    for (int kind = 0; kind < 3; kind++) {
        check_fatal_allocation(kind);
    }
    check_pe_allocation_failures();
    puts("Backend allocation checks passed");
    return 0;
}
