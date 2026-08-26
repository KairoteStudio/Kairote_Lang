#include "ArkLink/Loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KRO_DEBUG

#pragma pack(push, 1)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t entry_point;
    uint32_t text_size;
    uint32_t rodata_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t text_reloc_count;
    uint32_t rodata_reloc_count;
    uint32_t data_reloc_count;
    uint32_t bss_align;
    uint32_t sym_count;
    uint32_t strtab_size;
    uint32_t total_reloc_count;
    uint32_t reserved;
} KroHeader;

#define KRO_VERSION_V2 2u

typedef struct {
    uint32_t name_offset;
    uint32_t value;
    uint32_t size;
    uint32_t section;
    uint32_t binding;
    uint32_t type;
    uint32_t flags;
    uint32_t reserved;
} KroSymbol;

typedef struct {
    uint32_t offset;
    uint32_t sym_idx;
    uint32_t type;
    int32_t  addend;
} KroReloc;

#pragma pack(pop)

#define KRO_MAGIC 0x004F524B

static const char* kro_string_at(const char* string_table, size_t table_size, uint32_t offset) {
    if (!string_table || offset >= (uint32_t)table_size) return NULL;
    size_t max_len = table_size - offset;
    if (!memchr(string_table + offset, '\0', max_len)) return NULL;
    return string_table + offset;
}

ArkLinkResult ark_link_load_kro(const char* path, ArkLinkUnit** unit) {
#ifdef KRO_DEBUG
#endif
    if (!path || !unit) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return ARK_LINK_ERR_NOT_FOUND;
    }

    KroHeader header;
    if (fread(&header, sizeof(header), 1, file) != 1) {
        fclose(file);
        return ARK_LINK_ERR_FORMAT;
    }

    if (header.magic != KRO_MAGIC) {
        fclose(file);
        return ARK_LINK_ERR_FORMAT;
    }
    if (header.version != KRO_VERSION_V2) {
        fclose(file);
        return ARK_LINK_ERR_FORMAT;
    }

    ArkLinkUnit* new_unit = ark_link_unit_create(path);
    if (!new_unit) {
        fclose(file);
        return ARK_LINK_ERR_MEMORY;
    }
    new_unit->entry_point = header.entry_point;

    size_t header_size = sizeof(KroHeader);
    size_t text_offset   = header_size;
    size_t rodata_offset = text_offset   + header.text_size;
    size_t data_offset   = rodata_offset + header.rodata_size;
    size_t sym_offset    = data_offset   + header.data_size;
    size_t text_reloc_offset  = sym_offset + header.sym_count * sizeof(KroSymbol);
    size_t rodata_reloc_offset = text_reloc_offset + header.text_reloc_count * sizeof(KroReloc);
    size_t data_reloc_offset   = rodata_reloc_offset + header.rodata_reloc_count * sizeof(KroReloc);
    size_t strtab_offset = data_reloc_offset + header.data_reloc_count * sizeof(KroReloc);

    if (header.text_size > 0) {
        uint8_t* code_data = (uint8_t*)malloc((size_t)header.text_size);
        if (!code_data) {
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }

        fseek(file, (long)text_offset, SEEK_SET);
        if (fread(code_data, 1, (size_t)header.text_size, file) != (size_t)header.text_size) {
            free(code_data);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_IO;
        }

        ArkSectionDesc desc = {0};
        desc.name = ".text";
        desc.data = code_data;
        desc.size = (size_t)header.text_size;
        desc.alignment = 16;
        desc.flags = ARK_SECTION_READ | ARK_SECTION_EXEC;

        ArkLinkSection* sec = ark_link_unit_add_section(new_unit, &desc);
        if (!sec) {
            free(code_data);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }
        sec->kind = ARK_SECTION_CODE;
    }

    if (header.rodata_size > 0) {
        uint8_t* rodata_buf = (uint8_t*)malloc((size_t)header.rodata_size);
        if (!rodata_buf) {
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }

        fseek(file, (long)rodata_offset, SEEK_SET);
        if (fread(rodata_buf, 1, (size_t)header.rodata_size, file) != (size_t)header.rodata_size) {
            free(rodata_buf);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_IO;
        }

        ArkSectionDesc desc = {0};
        desc.name = ".rodata";
        desc.data = rodata_buf;
        desc.size = (size_t)header.rodata_size;
        desc.alignment = 8;
        desc.flags = ARK_SECTION_READ;

        ArkLinkSection* sec = ark_link_unit_add_section(new_unit, &desc);
        if (!sec) {
            free(rodata_buf);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }
        sec->kind = ARK_SECTION_RODATA;
    }

    if (header.data_size > 0) {
        uint8_t* data_buf = (uint8_t*)malloc((size_t)header.data_size);
        if (!data_buf) {
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }

        fseek(file, (long)data_offset, SEEK_SET);
        if (fread(data_buf, 1, (size_t)header.data_size, file) != (size_t)header.data_size) {
            free(data_buf);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_IO;
        }

        ArkSectionDesc desc = {0};
        desc.name = ".data";
        desc.data = data_buf;
        desc.size = (size_t)header.data_size;
        desc.alignment = 8;
        desc.flags = ARK_SECTION_READ | ARK_SECTION_WRITE;

        ArkLinkSection* sec = ark_link_unit_add_section(new_unit, &desc);
        if (!sec) {
            free(data_buf);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }
        sec->kind = ARK_SECTION_DATA;
    }

#ifdef KRO_DEBUG
#endif
    if (header.sym_count > 0) {
        KroSymbol* kro_syms = (KroSymbol*)malloc(header.sym_count * sizeof(KroSymbol));
        if (!kro_syms) {
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_MEMORY;
        }

        fseek(file, (long)sym_offset, SEEK_SET);
        if (fread(kro_syms, sizeof(KroSymbol), (size_t)header.sym_count, file) != (size_t)header.sym_count) {
            free(kro_syms);
            ark_link_unit_destroy(new_unit);
            fclose(file);
            return ARK_LINK_ERR_IO;
        }

        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        long string_table_size = file_size - (long)strtab_offset;

        char* string_table = NULL;
        if (string_table_size > 0 && header.strtab_size > 0) {
            string_table = (char*)malloc((size_t)string_table_size);
            if (string_table) {
                fseek(file, (long)strtab_offset, SEEK_SET);
                fread(string_table, 1, (size_t)string_table_size, file);
            }
        }

        uint32_t kro_to_actual[5] = {0};
#ifdef KRO_DEBUG
#endif
        for (size_t s = 0; s < new_unit->section_count; s++) {
            ArkLinkSection* sec = &new_unit->sections[s];
#ifdef KRO_DEBUG
#endif
            if (strcmp(sec->name, ".text") == 0) kro_to_actual[1] = (uint32_t)(s + 1);
            else if (strcmp(sec->name, ".rodata") == 0) kro_to_actual[2] = (uint32_t)(s + 1);
            else if (strcmp(sec->name, ".data") == 0) kro_to_actual[3] = (uint32_t)(s + 1);
        }

        kro_to_actual[4] = 0;

        for (size_t i = 0; i < (size_t)header.sym_count; i++) {
            const char* sym_name = kro_string_at(string_table, (size_t)string_table_size, kro_syms[i].name_offset);

            uint32_t actual_section = 0;
            if (kro_syms[i].section > 0 && kro_syms[i].section <= 4) {
                actual_section = kro_to_actual[kro_syms[i].section];
            }

#ifdef KRO_DEBUG
#endif

            ArkSymbolDesc sym_desc = {0};
            sym_desc.name = sym_name ? strdup(sym_name) : NULL;
            sym_desc.section_index = actual_section;
            sym_desc.value = kro_syms[i].value;
            sym_desc.size = kro_syms[i].size;
            sym_desc.binding = (ArkSymbolBinding)kro_syms[i].binding;
            sym_desc.type = (ArkSymbolType)kro_syms[i].type;
            sym_desc.visibility = (ArkSymbolVisibility)kro_syms[i].flags;

            ark_link_unit_add_symbol(new_unit, &sym_desc);
        }

        free(kro_syms);
        free(string_table);

        struct {
            uint32_t count;
            uint32_t kro_sec_idx;
        } reloc_sections[] = {
            { header.text_reloc_count,   1 },
            { header.rodata_reloc_count, 2 },
            { header.data_reloc_count,   3 },
        };

        size_t reloc_offsets[] = {
            text_reloc_offset,
            rodata_reloc_offset,
            data_reloc_offset,
        };

        for (int r = 0; r < 3; r++) {
            if (reloc_sections[r].count == 0) continue;

            uint32_t actual_sec_idx = kro_to_actual[reloc_sections[r].kro_sec_idx];
            if (actual_sec_idx == 0 || actual_sec_idx > new_unit->section_count) continue;

            KroReloc* kro_relocs = (KroReloc*)malloc(reloc_sections[r].count * sizeof(KroReloc));
            if (!kro_relocs) continue;

            fseek(file, (long)reloc_offsets[r], SEEK_SET);
            if (fread(kro_relocs, sizeof(KroReloc), reloc_sections[r].count, file) == reloc_sections[r].count) {
                for (size_t i = 0; i < reloc_sections[r].count; i++) {
                    ArkRelocationDesc reloc_desc = {0};
                    reloc_desc.offset = kro_relocs[i].offset;
                    reloc_desc.type = kro_relocs[i].type;
                    reloc_desc.sym_idx = kro_relocs[i].sym_idx;
                    reloc_desc.addend = kro_relocs[i].addend;
                    reloc_desc.section_index = actual_sec_idx - 1;
                    ark_link_section_add_reloc(&new_unit->sections[actual_sec_idx - 1], &reloc_desc);
                }
            }
            free(kro_relocs);
        }
    }

    fclose(file);
    *unit = new_unit;
    return ARK_LINK_OK;
}
