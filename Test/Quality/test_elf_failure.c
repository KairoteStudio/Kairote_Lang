#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

static size_t allocation_index, failure_index, live_allocations;
static void* tracked_allocations[4096];

static void track_allocation(void* ptr) {
    if (!ptr) {
        return;
    }
    for (size_t i = 0; i < sizeof(tracked_allocations) / sizeof(tracked_allocations[0]); i++) {
        if (!tracked_allocations[i]) {
            tracked_allocations[i] = ptr;
            live_allocations++;
            return;
        }
    }
    assert(0 && "allocation tracker exhausted");
}

static void forget_allocation(void* ptr) {
    if (!ptr) {
        return;
    }
    for (size_t i = 0; i < sizeof(tracked_allocations) / sizeof(tracked_allocations[0]); i++) {
        if (tracked_allocations[i] == ptr) {
            tracked_allocations[i] = NULL;
            live_allocations--;
            return;
        }
    }
}

static void* failing_calloc(size_t count, size_t size) {
    if (++allocation_index == failure_index) {
        return NULL;
    }
    void* ptr = calloc(count, size);
    track_allocation(ptr);
    return ptr;
}

static void* failing_realloc(void* ptr, size_t size) {
    if (++allocation_index == failure_index) {
        return NULL;
    }
    /* Failed realloc preserves the original tracked allocation. */
    size_t slot = sizeof(tracked_allocations) / sizeof(tracked_allocations[0]);
    for (size_t i = 0; i < slot; i++) {
        if (ptr && tracked_allocations[i] == ptr) {
            slot = i;
            break;
        }
    }
    void* replacement = realloc(ptr, size);
    if (replacement) {
        if (slot < sizeof(tracked_allocations) / sizeof(tracked_allocations[0])) {
            tracked_allocations[slot] = replacement;
        } else {
            track_allocation(replacement);
        }
    }
    return replacement;
}

static void tracked_free(void* ptr) {
    forget_allocation(ptr);
    free(ptr);
}

#define calloc failing_calloc
#define realloc failing_realloc
#define free tracked_free
#ifndef ARK_ELF_TEST_SOURCE
#define ARK_ELF_TEST_SOURCE "../../ArkLink/src/Backend/BackendElf.c"
#endif
#include ARK_ELF_TEST_SOURCE
#undef calloc
#undef realloc
#undef free

static void snapshot(const char* directory, const char* name, const ArkBackendOutput* output) {
    char path[1024];
    assert(snprintf(path, sizeof(path), "%s/%s.elf", directory, name) < (int)sizeof(path));
    FILE* file = fopen(path, "wb");
    assert(file);
    assert(fwrite(output->data, 1, output->size, file) == output->size);
    assert(fclose(file) == 0);
    assert(snprintf(path, sizeof(path), "%s/%s.maps", directory, name) < (int)sizeof(path));
    file = fopen(path, "wb");
    assert(file);
    assert(fwrite(output->section_maps, sizeof(*output->section_maps), output->section_count, file) ==
           output->section_count);
    assert(fclose(file) == 0);
}

static void check_case(const char* name, ArkBackendInput* input, const char* directory) {
    size_t count = 1;
    for (size_t index = 0; index <= count; index++) {
        allocation_index = 0;
        failure_index = index;
        ArkBackendOutput output = {0};
        ArkLinkResult result = ark_backend_elf_link(NULL, input, &output);
        if (!index) {
            assert(result == ARK_LINK_OK);
            count = directory ? 0 : allocation_index;
            if (directory) {
                snapshot(directory, name, &output);
            }
            assert(output.size > 4 && !memcmp(output.data, "\177ELF", 4));
            assert(output.section_count == input->section_count);
        } else {
            assert(result == ARK_LINK_ERR_MEMORY);
            assert(!output.data && !output.section_maps && !output.size && !output.section_count);
        }
        tracked_free(output.data);
        tracked_free(output.section_maps);
        assert(live_allocations == 0);
    }
    printf("ELF %s allocation failures checked: %zu\n", name, count);
}

int main(int argc, char** argv) {
    const char* directory = argc > 1 ? argv[1] : NULL;
    uint8_t code[32] = {0x31, 0xc0, 0xc3};
    uint8_t data[16] = {1, 2, 3, 4};
    ArkSectionBuffer sections[] = {
        {.data = code, .size = sizeof(code), .kind = ARK_SECTION_CODE, .alignment = 16},
        {.data = data, .size = sizeof(data), .kind = ARK_SECTION_DATA, .alignment = 8},
        {.size = 32, .kind = ARK_SECTION_BSS, .alignment = 16},
        {.data = data, .size = sizeof(data), .kind = ARK_SECTION_RODATA, .alignment = 8},
        {.data = data, .size = sizeof(data), .kind = ARK_SECTION_TDATA, .alignment = 8},
        {.size = 32, .kind = ARK_SECTION_TBSS, .alignment = 16},
    };
    ArkBackendInput input = {.sections = sections, .section_count = 1, .output_type = ARK_OUTPUT_EXECUTABLE};
    check_case("static", &input, directory);
    ArkImportEntry imports[40] = {{.module = "libc.so.6", .symbol = "puts", .is_function = 1}};
    input.imports = imports;
    input.import_count = 1;
    check_case("dynamic", &input, directory);

    imports[1] = (ArkImportEntry){.module = "libc.so.6", .symbol = "environ"};
    ArkResolverSymbol imported_symbols[] = {
        {.name = "puts", .import_module = "libc.so.6"},
        {.name = "environ", .import_module = "libc.so.6"},
    };
    ArkResolverReloc imported_relocations[] = {
        {.symbol = &imported_symbols[0], .section_index = 0, .offset = 8, .type = 1},
        {.symbol = &imported_symbols[1], .section_index = 0, .offset = 16, .type = 1},
    };
    input.import_count = 2;
    input.relocs = imported_relocations;
    input.reloc_count = 2;
    check_case("dynamic_relocations", &input, directory);
    input.relocs = NULL;
    input.reloc_count = 0;

    char names[40][32];
    ArkExportEntry exports[40] = {0};
    for (size_t i = 0; i < 40; i++) {
        snprintf(names[i], sizeof(names[i]), "symbol_%zu", i);
        exports[i].name = names[i];
        exports[i].section_index = 0;
        exports[i].is_function = 1;
        imports[i].module = "libc.so.6";
        imports[i].symbol = names[i];
        imports[i].is_function = (int)(i % 2);
    }
    input.import_count = 0;
    input.exports = exports;
    input.export_count = 40;
    check_case("export_growth", &input, directory);
    input.export_count = 0;
    input.import_count = 40;
    check_case("import_growth", &input, directory);

    input.import_count = 0;
    input.section_count = 4;
    ArkResolverSymbol symbol = {.name = "data_symbol", .section_index = 1, .value = 8};
    ArkResolverReloc relocation = {.symbol = &symbol, .section_index = 0, .offset = 8, .type = 1};
    input.relocs = &relocation;
    input.reloc_count = 1;
    check_case("sections_relocation", &input, directory);
    input.section_count = 6;
    check_case("tls", &input, directory);
    return 0;
}
