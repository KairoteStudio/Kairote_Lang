#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static size_t allocation_index, failure_index;
static void* failing_calloc(size_t count, size_t size) {
    return ++allocation_index == failure_index ? NULL : calloc(count, size);
}
static void* failing_realloc(void* ptr, size_t size) {
    return ++allocation_index == failure_index ? NULL : realloc(ptr, size);
}

#define calloc failing_calloc
#define realloc failing_realloc
#include "../../ArkLink/src/Backend/BackendElf.c"
#undef calloc
#undef realloc

int main(void) {
    uint8_t code[] = {0x31, 0xc0, 0xc3};
    ArkSectionBuffer section = {
        .data = code, .size = sizeof(code), .capacity = sizeof(code), .kind = ARK_SECTION_CODE, .alignment = 16};
    ArkImportEntry import = {.module = "libc.so.6", .symbol = "puts", .is_function = 1};
    for (int dynamic = 0; dynamic < 2; dynamic++) {
        size_t count = 1;
        for (size_t index = 0; index <= count; index++) {
            allocation_index = 0;
            failure_index = index;
            ArkBackendInput input = {.sections = &section,
                                     .section_count = 1,
                                     .entry_section = 0,
                                     .output_type = ARK_OUTPUT_EXECUTABLE,
                                     .imports = dynamic ? &import : NULL,
                                     .import_count = (size_t)dynamic};
            ArkBackendOutput output = {0};
            ArkLinkResult result = ark_backend_elf_link(NULL, &input, &output);
            if (!index) {
                assert(result == ARK_LINK_OK);
                count = allocation_index;
            }
            assert(result == ARK_LINK_OK || result == ARK_LINK_ERR_MEMORY);
            if (result == ARK_LINK_OK) {
                assert(output.size > 4 && !memcmp(output.data, "\177ELF", 4));
            }
            free(output.data);
        }
        printf("ELF %s allocation failures checked: %zu\n", dynamic ? "dynamic" : "static", count);
    }
    return 0;
}
