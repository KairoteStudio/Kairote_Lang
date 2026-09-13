/* Exercise file-format helpers and validate their serialized output directly. */
#include "../../ArkLink/src/Backend/BackendPe.c"
#include "Tools/KroWriter.h"
#include <assert.h>

static void check_buffer(void) {
    assert(ark_backend_align_up(17, 16) == 32);
    assert(ark_backend_align_up(17, 3) == 18);
    assert(ark_backend_align_up(18, 3) == 18);
    assert(ark_backend_align_up(UINT64_MAX - 2, 8) == UINT64_MAX);
    assert(ark_backend_align_up_32(UINT32_MAX - 2, 8) == UINT32_MAX);
    assert(ark_backend_align_up(UINT64_MAX, 1) == UINT64_MAX);
    ArkBuffer* buffer = ark_buffer_create(4, 0x1000);
    assert(buffer);
    assert(ark_buffer_append(buffer, "abcd", 4) == 0);
    assert(ark_buffer_append(buffer, buffer->data, 4) == 4);
    assert(buffer->size == 8 && !memcmp(buffer->data, "abcdabcd", 8));
    assert(ark_buffer_append(buffer, buffer->data + 2, 7) == SIZE_MAX);
    assert(ark_buffer_append_zero(buffer, SIZE_MAX) == SIZE_MAX);
    assert(buffer->size == 8);
    assert(ark_buffer_get_rva(buffer, UINT32_MAX) == UINT32_MAX);
    assert(ark_buffer_get_rva(buffer, 8) == 0x1008);
    ark_buffer_align(buffer, 16);
    assert(buffer->size == 16);
    for (size_t i = 8; i < 16; i++) {
        assert(!buffer->data[i]);
    }
    ark_buffer_destroy(buffer);
}

static void check_imports(void) {
    enum { COUNT = 1200, MODULES = 3 };
    const char* modules[MODULES] = {"a.dll", "even.dll", "third.dll"};
    ArkImportEntry* imports = calloc(COUNT, sizeof(*imports));
    char (*names)[192] = calloc(COUNT, sizeof(*names));
    assert(imports && names);
    for (int i = 0; i < COUNT; i++) {
        snprintf(names[i], sizeof(names[i]), "Function_%04d_%0160d", i, i);
        imports[i].module = modules[i % MODULES];
        imports[i].symbol = names[i];
        imports[i].is_function = i % 2;
    }
    ArkBackendInput input = {.imports = imports, .import_count = COUNT};
    size_t size, entries;
    uint32_t iat;
    uint32_t base = 0x2000;
    uint8_t* data = generate_import_table(&input, base, &size, &iat, &entries);
    assert(data && size > 200000 && entries == COUNT + MODULES && iat % 8 == 0);
    size_t cursor = iat - base;
    for (int m = 0; m < MODULES; m++) {
        PE_IMPORT_DIRECTORY_ENTRY directory;
        memcpy(&directory, data + m * sizeof(directory), sizeof(directory));
        assert(!strcmp((char*)data + directory.NameRVA - base, modules[m]));
        assert(directory.ImportLookupTableRVA % 8 == 0);
        assert(directory.ImportAddressTableRVA - base == cursor);
        for (int j = 0; j <= COUNT / MODULES; j++) {
            uint64_t lookup, address;
            memcpy(&lookup, data + directory.ImportLookupTableRVA - base + j * 8, 8);
            memcpy(&address, data + cursor + j * 8, 8);
            assert(lookup == address);
            if (j == COUNT / MODULES) {
                assert(!lookup);
            } else {
                assert(lookup >= base && lookup - base + 2 < size);
                assert(!strcmp((char*)data + lookup - base + 2, names[j * MODULES + m]));
            }
        }
        cursor += (COUNT / MODULES + 1) * 8;
    }
    assert(cursor - (iat - base) == entries * 8);
    free(data);
    free(names);
    free(imports);
}

static void check_kro(const char* filename) {
    KROWriter* writer = kro_writer_create();
    assert(writer);
    uint8_t byte = 0xcc;
    kro_write_code(writer, &byte, 1);
    kro_write_data(writer, &byte, 1);
    assert(kro_write_code_aligned(writer, &byte, 1, 256) == 256);
    assert(kro_write_data_aligned(writer, &byte, 1, 32) == 32);
    assert(kro_write_code(writer, &byte, UINT32_MAX) == 0);
    assert(kro_get_code_offset(writer) == 257);
    kro_set_code_offset(writer, 1);
    kro_write_code(writer, &byte, 1);
    kro_set_code_offset(writer, 257);
    assert(kro_get_code_offset(writer) == 257);
    assert(kro_write_data_aligned(writer, &byte, 1, 3) == 0);
    assert(kro_get_data_offset(writer) == 33);
    kro_set_code_offset(writer, UINT32_MAX);
    assert(kro_get_code_offset(writer) == 257);
    assert(kro_reserve_bss(writer, 1, 0));
    assert(kro_reserve_bss(writer, 1, 6));
    assert(!kro_reserve_bss(writer, 1, 32));
    assert(!kro_reserve_bss(writer, UINT32_MAX, 0));
    assert(kro_write_file(writer, filename));
    FILE* file = fopen(filename, "rb");
    assert(file);
    KROHeader header;
    assert(fread(&header, sizeof(header), 1, file) == 1);
    assert(header.bss_align == 64 && header.bss_size == 65);
    assert(fgetc(file) == byte);
    assert(fgetc(file) == byte);
    for (int i = 2; i < 256; i++) {
        assert(fgetc(file) == 0x90);
    }
    assert(fgetc(file) == byte);
    fclose(file);
    kro_writer_destroy(writer);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    check_buffer();
    check_imports();
    check_kro(argv[1]);
    puts("buffer reuse, PE import tables and KRO alignment checks passed");
    return 0;
}
