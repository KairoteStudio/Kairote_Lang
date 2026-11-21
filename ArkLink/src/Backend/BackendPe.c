#include "ArkLink/BackendPe.h"
#include "ArkLink/Loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma pack(push, 1)

typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} PE_DOS_HEADER;

#define PE_SIGNATURE 0x00004550

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} PE_COFF_HEADER;

#define PE_OPT_HDR_MAGIC_PE32 0x10b
#define PE_OPT_HDR_MAGIC_PE32_PLUS 0x20b

typedef struct {
    uint32_t VirtualAddress;
    uint32_t Size;
} PE_DATA_DIRECTORY;

typedef struct {
    uint16_t Magic;
    uint8_t MajorLinkerVersion;
    uint8_t MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    PE_DATA_DIRECTORY DataDirectory[16];
} PE_OPTIONAL_HEADER_64;

typedef struct {
    uint8_t Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} PE_SECTION_HEADER;

#define PE_MACHINE_AMD64 0x8664
#define PE_CHAR_EXECUTABLE_IMAGE 0x0002
#define PE_CHAR_LARGE_ADDRESS_AWARE 0x0020
#define PE_CHAR_DEBUG_STRIPPED 0x0200
#define PE_CHAR_DLL 0x2000
#define PE_SCN_CNT_CODE 0x00000020
#define PE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define PE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define PE_SCN_MEM_EXECUTE 0x20000000
#define PE_SCN_MEM_READ 0x40000000
#define PE_SCN_MEM_WRITE 0x80000000
#define PE_SUBSYSTEM_WINDOWS_CUI 3
#define PE_DD_EXPORT 0
#define PE_DD_IMPORT 1
#define PE_DD_RESOURCE 2
#define PE_DD_EXCEPTION 3
#define PE_DD_CERTIFICATE 4
#define PE_DD_BASE_RELOCATION 5
#define PE_DD_DEBUG 6
#define PE_DD_ARCHITECTURE 7
#define PE_DD_GLOBAL_PTR 8
#define PE_DD_TLS 9
#define PE_DD_LOAD_CONFIG 10
#define PE_DD_BOUND_IMPORT 11
#define PE_DD_IAT 12
#define PE_DD_DELAY_IMPORT 13
#define PE_DD_COM_DESCRIPTOR 14
#define PE_REL_BASED_ABS 0
#define PE_REL_BASED_HIGH 1
#define PE_REL_BASED_LOW 2
#define PE_REL_BASED_HIGHLOW 3
#define PE_REL_BASED_HIGHADJ 4
#define PE_REL_BASED_DIR64 10

typedef struct {
    uint32_t ImportLookupTableRVA;
    uint32_t TimeDateStamp;
    uint32_t ForwarderChain;
    uint32_t NameRVA;
    uint32_t ImportAddressTableRVA;
} PE_IMPORT_DIRECTORY_ENTRY;

typedef struct {
    uint64_t Value;
} PE_IMPORT_LOOKUP_ENTRY;

typedef struct {
    uint16_t Hint;
    char Name[1];
} PE_HINT_NAME_ENTRY;

typedef struct {
    uint32_t PageRVA;
    uint32_t BlockSize;

} PE_BASE_RELOCATION_BLOCK;

typedef struct {
    uint32_t ExportFlags;
    uint32_t TimeDateStamp;
    uint16_t MajorVersion;
    uint16_t MinorVersion;
    uint32_t NameRVA;
    uint32_t OrdinalBase;
    uint32_t AddressTableEntries;
    uint32_t NumberOfNamePointers;
    uint32_t ExportAddressTableRVA;
    uint32_t NamePointerRVA;
    uint32_t OrdinalTableRVA;
} PE_EXPORT_DIRECTORY_ENTRY;

#pragma pack(pop)

/* PE 标准对齐固定；ArkBackendInput 目前不提供覆盖字段。 */
#define PE_SECTION_ALIGNMENT 0x1000
#define PE_FILE_ALIGNMENT 0x200

static int pe_safe_mul(size_t count, size_t elem_size, size_t* out) {
    if (elem_size == 0 || count > SIZE_MAX / elem_size) {
        return -1;
    }
    *out = count * elem_size;
    return 0;
}

static int pe_module_name_equal(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

static int pe_calc_header_size(size_t total_sections, uint32_t file_alignment, uint32_t* out) {
    uint64_t table_size = (uint64_t)total_sections * sizeof(PE_SECTION_HEADER);
    uint64_t total =
        (uint64_t)0x80 + 4 + sizeof(PE_COFF_HEADER) + sizeof(PE_OPTIONAL_HEADER_64) + table_size;
    uint64_t aligned = ark_backend_align_up(total, file_alignment);
    if (aligned == UINT64_MAX || aligned > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)aligned;
    return 0;
}

static int pe_align_u32(size_t value, uint32_t alignment, uint32_t* out) {
    uint64_t aligned = ark_backend_align_up(value, alignment);
    if (aligned == UINT64_MAX || aligned > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)aligned;
    return 0;
}

static int pe_add_size(size_t* total, uint64_t delta) {
    if (delta == UINT64_MAX || delta > SIZE_MAX || *total > SIZE_MAX - (size_t)delta) {
        return -1;
    }
    *total += (size_t)delta;
    return 0;
}

static int pe_add_aligned(size_t* total, size_t value, uint32_t alignment) {
    return pe_add_size(total, ark_backend_align_up(value, alignment));
}

static void write_dos_header(uint8_t* data) {
    PE_DOS_HEADER* dos = (PE_DOS_HEADER*)data;

    dos->e_magic = 0x5A4D;
    dos->e_cblp = 0x0090;
    dos->e_cp = 0x0003;
    dos->e_cparhdr = 0x0004;
    dos->e_minalloc = 0x0000;
    dos->e_maxalloc = 0xFFFF;
    dos->e_ss = 0x0000;
    dos->e_sp = 0x00B8;
    dos->e_csum = 0x0000;
    dos->e_ip = 0x0000;
    dos->e_cs = 0x0000;
    dos->e_lfarlc = 0x0040;
    dos->e_ovno = 0x0000;
    dos->e_lfanew = 0x00000080;

    static const uint8_t dos_stub[] = {0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD,
                                       0x21, 0x54, 0x68, 0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D,
                                       0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F, 0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75,
                                       0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20, 0x6D, 0x6F, 0x64, 0x65,
                                       0x2E, 0x0D, 0x0D, 0x0A, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(data + sizeof(PE_DOS_HEADER), dos_stub, sizeof(dos_stub));
}

static void write_section_header(uint8_t* data, size_t index, const char* name, uint32_t virtual_size,
                                 uint32_t virtual_address, uint32_t size_of_raw_data, uint32_t pointer_to_raw_data,
                                 uint32_t characteristics) {
    uint32_t pe_offset = 0x80;
    uint32_t section_table_offset = pe_offset + 4 + sizeof(PE_COFF_HEADER) + sizeof(PE_OPTIONAL_HEADER_64);

    PE_SECTION_HEADER* sec = (PE_SECTION_HEADER*)(data + section_table_offset + index * sizeof(PE_SECTION_HEADER));

    memset(sec->Name, 0, 8);
    size_t name_len = strlen(name);
    if (name_len > 8) {
        fprintf(stderr, "ArkLink: PE section name '%s' truncated to 8 characters\n", name);
        name_len = 8;
    }
    memcpy(sec->Name, name, name_len);

    sec->VirtualSize = virtual_size;
    sec->VirtualAddress = virtual_address;
    sec->SizeOfRawData = size_of_raw_data;
    sec->PointerToRawData = pointer_to_raw_data;
    sec->PointerToRelocations = 0;
    sec->PointerToLinenumbers = 0;
    sec->NumberOfRelocations = 0;
    sec->NumberOfLinenumbers = 0;
    sec->Characteristics = characteristics;
}

static uint8_t* generate_relocation_table(ArkBackendInput* input, ArkSectionRvaMap* section_maps, size_t* out_size) {
    *out_size = 0;
    if (!input->relocs || input->reloc_count == 0) {
        return NULL;
    }
    if (input->reloc_count > SIZE_MAX / sizeof(uint32_t)) {
        return NULL;
    }

#define KRT_PE_PAGE_SIZE 0x1000

    uint32_t* page_rvas = (uint32_t*)malloc(input->reloc_count * sizeof(uint32_t));
    if (!page_rvas) {
        return NULL;
    }

    size_t page_count = 0;
    for (size_t i = 0; i < input->reloc_count; i++) {
        ArkResolverReloc* reloc = &input->relocs[i];
        if (!reloc->symbol || reloc->type == ARK_RELOC_PC32) {
            continue;
        }
        if (reloc->section_index >= input->section_count) {
            fprintf(stderr, "ArkLink: relocation section index %u out of range\n", reloc->section_index);
            continue;
        }
        uint64_t rva64 = (uint64_t)section_maps[reloc->section_index].rva + reloc->offset;
        if (rva64 > UINT32_MAX) {
            fprintf(stderr, "ArkLink: relocation RVA overflow in section %u\n", reloc->section_index);
            continue;
        }
        uint32_t reloc_rva = (uint32_t)rva64;
        uint32_t page_rva = reloc_rva & ~(KRT_PE_PAGE_SIZE - 1);

        int found = 0;
        for (size_t j = 0; j < page_count; j++) {
            if (page_rvas[j] == page_rva) {
                found = 1;
                break;
            }
        }
        if (!found) {
            page_rvas[page_count++] = page_rva;
        }
    }

    if (page_count == 0) {
        free(page_rvas);
        return NULL;
    }

    size_t total_size = 0;
    for (size_t p = 0; p < page_count; p++) {
        size_t count = 0;
        for (size_t i = 0; i < input->reloc_count; i++) {
            ArkResolverReloc* reloc = &input->relocs[i];
            if (!reloc->symbol || reloc->type == ARK_RELOC_PC32) {
                continue;
            }
            if (reloc->section_index >= input->section_count) {
                continue;
            }
            uint64_t rva64 = (uint64_t)section_maps[reloc->section_index].rva + reloc->offset;
            if (rva64 > UINT32_MAX) {
                continue;
            }
            uint32_t page_rva = (uint32_t)rva64 & ~(KRT_PE_PAGE_SIZE - 1);
            if (page_rva == page_rvas[p]) {
                count++;
            }
        }
        if (count > (SIZE_MAX - 8) / 2) {
            free(page_rvas);
            return NULL;
        }
        size_t block_size = 8 + ((count + 1) & ~1) * 2;
        if (total_size > SIZE_MAX - block_size) {
            free(page_rvas);
            return NULL;
        }
        total_size += block_size;
    }

    uint8_t* reloc_data = (uint8_t*)calloc(1, total_size);
    if (!reloc_data) {
        free(page_rvas);
        return NULL;
    }

    size_t offset = 0;
    for (size_t p = 0; p < page_count; p++) {
        PE_BASE_RELOCATION_BLOCK* block = (PE_BASE_RELOCATION_BLOCK*)(reloc_data + offset);
        block->PageRVA = page_rvas[p];

        uint8_t* type_offset = reloc_data + offset + 8;
        size_t count = 0;
        for (size_t i = 0; i < input->reloc_count; i++) {
            ArkResolverReloc* reloc = &input->relocs[i];
            if (!reloc->symbol || reloc->type == ARK_RELOC_PC32) {
                continue;
            }
            if (reloc->section_index >= input->section_count) {
                continue;
            }
            uint64_t rva64 = (uint64_t)section_maps[reloc->section_index].rva + reloc->offset;
            if (rva64 > UINT32_MAX) {
                continue;
            }
            uint32_t reloc_rva = (uint32_t)rva64;
            uint32_t page_rva = reloc_rva & ~(KRT_PE_PAGE_SIZE - 1);
            if (page_rva == page_rvas[p]) {
                uint16_t offset_in_page = reloc_rva - page_rva;
                uint16_t type = PE_REL_BASED_DIR64;
                if (reloc->type == ARK_RELOC_ADDR32) {
                    type = PE_REL_BASED_HIGHLOW;
                }
                uint16_t entry = (type << 12) | (offset_in_page & 0xFFF);
                memcpy(type_offset + count++ * sizeof(entry), &entry, sizeof(entry));
            }
        }

        size_t block_size = 8 + ((count + 1) & ~1) * 2;
        block->BlockSize = (uint32_t)block_size;
        offset += block_size;
    }

    free(page_rvas);
    *out_size = total_size;
    return reloc_data;
}

typedef ArkBuffer ImportTableBuilder;

static ImportTableBuilder* import_builder_create(size_t initial_capacity, uint32_t rva) {
    return ark_buffer_create(initial_capacity, rva);
}

static void import_builder_free(ImportTableBuilder* builder) {
    ark_buffer_destroy(builder);
}

static size_t import_builder_append(ImportTableBuilder* builder, const void* data, size_t len) {
    return ark_buffer_append(builder, data, len);
}

static int import_builder_get_rva(ImportTableBuilder* builder, size_t offset, uint32_t* out_rva) {
    uint32_t rva;
    if (!out_rva) {
        return -1;
    }
    rva = ark_buffer_get_rva(builder, offset);
    if (rva == UINT32_MAX) {
        return -1;
    }
    *out_rva = rva;
    return 0;
}

static uint8_t* generate_import_table(ArkBackendInput* input, uint32_t idata_rva, size_t* out_size,
                                      uint32_t* out_iat_rva, size_t* out_iat_total_entries) {
    if (!input->imports || input->import_count == 0) {
        *out_size = 0;
        *out_iat_rva = 0;
        if (out_iat_total_entries) {
            *out_iat_total_entries = 0;
        }
        return NULL;
    }

    ImportTableBuilder* builder = NULL;
    size_t module_count = 0;
    const char** modules = NULL;

    if (input->import_count > SIZE_MAX / sizeof(char*)) {
        return NULL;
    }
    modules = (const char**)calloc(input->import_count, sizeof(char*));
    if (!modules) {
        return NULL;
    }

    for (size_t i = 0; i < input->import_count; i++) {
        const char* mod = input->imports[i].module;
        if (!mod) {
            fprintf(stderr, "ArkLink: import %zu has no module name\n", i);
            continue;
        }
        int found = 0;
        for (size_t j = 0; j < module_count; j++) {
            if (pe_module_name_equal(modules[j], mod)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            modules[module_count++] = mod;
        }
    }

    if (module_count == 0) {
        fprintf(stderr, "ArkLink: no usable import modules\n");
        free(modules);
        return NULL;
    }

    builder = import_builder_create(4096, idata_rva);
    if (!builder) {
        free(modules);
        return NULL;
    }

    if (module_count > SIZE_MAX / sizeof(PE_IMPORT_DIRECTORY_ENTRY) - 1) {
        goto fail;
    }
    size_t idt_offset = import_builder_append(builder, NULL, (module_count + 1) * sizeof(PE_IMPORT_DIRECTORY_ENTRY));
    if (idt_offset == (size_t)-1) {
        goto fail;
    }

    if (input->import_count > SIZE_MAX - module_count) {
        goto fail;
    }
    size_t total_entries = input->import_count + module_count;
    if (total_entries > SIZE_MAX / sizeof(PE_IMPORT_LOOKUP_ENTRY)) {
        goto fail;
    }
    size_t table_size = total_entries * sizeof(PE_IMPORT_LOOKUP_ENTRY);
    size_t padding = (8 - builder->size % 8) % 8;
    if (padding && import_builder_append(builder, NULL, padding) == (size_t)-1) {
        goto fail;
    }
    size_t all_ilt_offset = import_builder_append(builder, NULL, table_size);
    if (all_ilt_offset == (size_t)-1) {
        goto fail;
    }
    size_t all_iat_offset = import_builder_append(builder, NULL, table_size);
    if (all_iat_offset == (size_t)-1) {
        goto fail;
    }
    size_t table_index = 0;
    PE_IMPORT_DIRECTORY_ENTRY descriptor;

    for (size_t m = 0; m < module_count; m++) {
        const char* module_name = modules[m];

        size_t sym_count = 0;
        for (size_t i = 0; i < input->import_count; i++) {
            if (pe_module_name_equal(input->imports[i].module, module_name)) {
                sym_count++;
            }
        }

        size_t* sym_indices = (size_t*)malloc(sym_count * sizeof(size_t));
        if (!sym_indices) {
            goto fail;
        }

        size_t sym_idx = 0;
        for (size_t i = 0; i < input->import_count; i++) {
            if (pe_module_name_equal(input->imports[i].module, module_name)) {
                sym_indices[sym_idx++] = i;
            }
        }

        size_t name_len = strlen(module_name) + 1;
        size_t ilt_offset = all_ilt_offset + table_index * sizeof(PE_IMPORT_LOOKUP_ENTRY);
        size_t iat_offset = all_iat_offset + table_index * sizeof(PE_IMPORT_LOOKUP_ENTRY);
        table_index += sym_count + 1;

        size_t name_offset = builder->size;

        if (import_builder_append(builder, module_name, name_len) == (size_t)-1) {
            free(sym_indices);
            goto fail;
        }

        if (name_len % 2 != 0) {
            uint8_t pad = 0;
            if (import_builder_append(builder, &pad, 1) == (size_t)-1) {
                free(sym_indices);
                goto fail;
            }
        }

        for (size_t i = 0; i < sym_count; i++) {
            size_t import_idx = sym_indices[i];

            size_t hint_name_offset = builder->size;
            uint16_t hint = 0;
            if (import_builder_append(builder, &hint, sizeof(hint)) == (size_t)-1) {
                free(sym_indices);
                goto fail;
            }

            const char* sym_name = input->imports[import_idx].symbol;
            if (!sym_name) {
                fprintf(stderr, "ArkLink: import %zu has no symbol name\n", import_idx);
                free(sym_indices);
                goto fail;
            }
            size_t sym_name_len = strlen(sym_name) + 1;
            if (import_builder_append(builder, sym_name, sym_name_len) == (size_t)-1) {
                free(sym_indices);
                goto fail;
            }

            if (sym_name_len % 2 != 0) {
                uint8_t pad = 0;
                if (import_builder_append(builder, &pad, 1) == (size_t)-1) {
                    free(sym_indices);
                    goto fail;
                }
            }

            uint32_t entry_rva = 0;
            if (import_builder_get_rva(builder, hint_name_offset, &entry_rva) != 0) {
                free(sym_indices);
                goto fail;
            }
            uint64_t entry_value = entry_rva;

            memcpy(builder->data + ilt_offset + i * sizeof(entry_value), &entry_value, sizeof(entry_value));

            memcpy(builder->data + iat_offset + i * sizeof(entry_value), &entry_value, sizeof(entry_value));
        }

        free(sym_indices);

        uint32_t ilt_rva = 0;
        uint32_t name_rva = 0;
        uint32_t iat_rva = 0;
        if (import_builder_get_rva(builder, ilt_offset, &ilt_rva) != 0 ||
            import_builder_get_rva(builder, name_offset, &name_rva) != 0 ||
            import_builder_get_rva(builder, iat_offset, &iat_rva) != 0) {
            goto fail;
        }
        descriptor.ImportLookupTableRVA = ilt_rva;
        descriptor.TimeDateStamp = 0;
        descriptor.ForwarderChain = 0;
        descriptor.NameRVA = name_rva;
        descriptor.ImportAddressTableRVA = iat_rva;
        memcpy(builder->data + idt_offset + m * sizeof(descriptor), &descriptor, sizeof(descriptor));
    }

    memcpy(&descriptor, builder->data + idt_offset, sizeof(descriptor));

    *out_size = builder->size;
    *out_iat_rva = descriptor.ImportAddressTableRVA;

    if (out_iat_total_entries) {
        *out_iat_total_entries = total_entries;
    }

    uint8_t* result = (uint8_t*)malloc(builder->size);
    if (result) {
        memcpy(result, builder->data, builder->size);
    }

    import_builder_free(builder);
    free(modules);
    return result;

fail:
    import_builder_free(builder);
    free(modules);
    return NULL;
}

typedef ArkBuffer ExportTableBuilder;

static ExportTableBuilder* export_builder_create(size_t initial_capacity, uint32_t base_rva) {
    return ark_buffer_create(initial_capacity, base_rva);
}

static void export_builder_free(ExportTableBuilder* builder) {
    ark_buffer_destroy(builder);
}

static size_t export_builder_append(ExportTableBuilder* builder, const void* data, size_t size) {
    return ark_buffer_append(builder, data, size);
}

static int export_builder_get_rva(ExportTableBuilder* builder, size_t offset, uint32_t* out_rva) {
    uint32_t rva;
    if (!out_rva) {
        return -1;
    }
    rva = ark_buffer_get_rva(builder, offset);
    if (rva == UINT32_MAX) {
        return -1;
    }
    *out_rva = rva;
    return 0;
}

static int compare_export_entries(const void* a, const void* b) {
    const ArkExportEntry* ea = (const ArkExportEntry*)a;
    const ArkExportEntry* eb = (const ArkExportEntry*)b;
    return strcmp(ea->name, eb->name);
}

static uint8_t* generate_export_table(ArkBackendInput* input, uint32_t edata_rva, size_t* out_size,
                                      ArkSectionRvaMap* section_maps) {
    if (!input->exports || input->export_count == 0) {
        *out_size = 0;
        return NULL;
    }

    size_t export_size = 0;
    if (pe_safe_mul(input->export_count, sizeof(ArkExportEntry), &export_size) != 0) {
        return NULL;
    }
    for (size_t i = 0; i < input->export_count; i++) {
        if (!input->exports[i].name) {
            fprintf(stderr, "ArkLink: export %zu has no name\n", i);
            *out_size = 0;
            return NULL;
        }
    }

    ExportTableBuilder* builder = export_builder_create(4096, edata_rva);
    if (!builder) {
        return NULL;
    }

    ArkExportEntry* sorted_exports = NULL;
    sorted_exports = (ArkExportEntry*)malloc(export_size);
    if (!sorted_exports) {
        goto fail;
    }
    memcpy(sorted_exports, input->exports, export_size);
    qsort(sorted_exports, input->export_count, sizeof(ArkExportEntry), compare_export_entries);

    size_t count = input->export_count;
    size_t edt_size = sizeof(PE_EXPORT_DIRECTORY_ENTRY);
    size_t eat_size = 0;
    size_t ent_size = 0;
    size_t eot_size = 0;
    if (pe_safe_mul(count, sizeof(uint32_t), &eat_size) != 0 || pe_safe_mul(count, sizeof(uint32_t), &ent_size) != 0 ||
        pe_safe_mul(count, sizeof(uint16_t), &eot_size) != 0) {
        goto fail;
    }

    const char* dll_name = input->export_name ? input->export_name : "exported.dll";
    size_t dll_name_len = strlen(dll_name) + 1;

    size_t edt_offset = export_builder_append(builder, NULL, edt_size);
    size_t eat_offset = export_builder_append(builder, NULL, eat_size);
    size_t ent_offset = export_builder_append(builder, NULL, ent_size);
    size_t eot_offset = export_builder_append(builder, NULL, eot_size);
    size_t dll_name_offset = export_builder_append(builder, dll_name, dll_name_len);

    if (edt_offset == (size_t)-1 || eat_offset == (size_t)-1 || ent_offset == (size_t)-1 || eot_offset == (size_t)-1 ||
        dll_name_offset == (size_t)-1) {
        goto fail;
    }

    size_t name_lens_size = 0;
    size_t name_offsets_size = 0;
    if (pe_safe_mul(count, sizeof(size_t), &name_lens_size) != 0 ||
        pe_safe_mul(count, sizeof(size_t), &name_offsets_size) != 0) {
        goto fail;
    }

    size_t* name_lens = (size_t*)malloc(name_lens_size);
    if (!name_lens) {
        goto fail;
    }
    for (size_t i = 0; i < count; i++) {
        name_lens[i] = strlen(sorted_exports[i].name) + 1;
    }

    size_t* name_offsets = (size_t*)malloc(name_offsets_size);
    if (!name_offsets) {
        free(name_lens);
        goto fail;
    }
    for (size_t i = 0; i < count; i++) {
        name_offsets[i] = export_builder_append(builder, NULL, name_lens[i]);
        if (name_offsets[i] == (size_t)-1) {
            free(name_offsets);
            free(name_lens);
            goto fail;
        }
    }

    uint8_t* eat = builder->data + eat_offset;
    uint8_t* ent = builder->data + ent_offset;
    uint8_t* eot = builder->data + eot_offset;

    for (size_t i = 0; i < count; i++) {
        ArkExportEntry* exp = &sorted_exports[i];
        memcpy(builder->data + name_offsets[i], exp->name, name_lens[i]);

        if (exp->ordinal < input->export_ordinal_base) {
            fprintf(stderr, "ArkLink: export ordinal %u below base %u\n", exp->ordinal, input->export_ordinal_base);
            free(name_lens);
            free(name_offsets);
            goto fail;
        }
        uint64_t ordinal_idx = (uint64_t)exp->ordinal - input->export_ordinal_base;
        if (ordinal_idx >= count) {
            fprintf(stderr, "ArkLink: export ordinal index %llu out of range\n", (unsigned long long)ordinal_idx);
            free(name_lens);
            free(name_offsets);
            goto fail;
        }
        uint32_t name_rva = 0;
        if (export_builder_get_rva(builder, name_offsets[i], &name_rva) != 0) {
            free(name_lens);
            free(name_offsets);
            goto fail;
        }
        uint16_t ordinal = (uint16_t)ordinal_idx;
        memcpy(ent + i * sizeof(name_rva), &name_rva, sizeof(name_rva));
        memcpy(eot + i * sizeof(ordinal), &ordinal, sizeof(ordinal));
        uint32_t address;

        if (exp->section_index < input->section_count) {
            uint64_t address64 = (uint64_t)section_maps[exp->section_index].rva + exp->offset;
            if (address64 > UINT32_MAX) {
                fprintf(stderr, "ArkLink: export address overflow\n");
                free(name_lens);
                free(name_offsets);
                goto fail;
            }
            address = (uint32_t)address64;
        } else {
            if (exp->value > UINT32_MAX) {
                fprintf(stderr, "ArkLink: export value %llu exceeds 32 bits\n", (unsigned long long)exp->value);
                free(name_lens);
                free(name_offsets);
                goto fail;
            }
            address = (uint32_t)exp->value;
        }
        memcpy(eat + ordinal_idx * sizeof(address), &address, sizeof(address));
    }

    free(name_lens);
    free(name_offsets);

    PE_EXPORT_DIRECTORY_ENTRY* edt = (PE_EXPORT_DIRECTORY_ENTRY*)(builder->data + edt_offset);
    edt->ExportFlags = 0;
    edt->TimeDateStamp = 0;
    edt->MajorVersion = 0;
    edt->MinorVersion = 0;
    uint32_t dll_name_rva = 0;
    uint32_t eat_rva = 0;
    uint32_t ent_rva = 0;
    uint32_t eot_rva = 0;
    if (export_builder_get_rva(builder, dll_name_offset, &dll_name_rva) != 0 ||
        export_builder_get_rva(builder, eat_offset, &eat_rva) != 0 ||
        export_builder_get_rva(builder, ent_offset, &ent_rva) != 0 ||
        export_builder_get_rva(builder, eot_offset, &eot_rva) != 0) {
        goto fail;
    }
    edt->NameRVA = dll_name_rva;
    edt->OrdinalBase = input->export_ordinal_base;
    if (input->export_count > UINT32_MAX) {
        fprintf(stderr, "ArkLink: export count exceeds 32 bits\n");
        goto fail;
    }
    edt->AddressTableEntries = (uint32_t)input->export_count;
    edt->NumberOfNamePointers = (uint32_t)input->export_count;
    edt->ExportAddressTableRVA = eat_rva;
    edt->NamePointerRVA = ent_rva;
    edt->OrdinalTableRVA = eot_rva;

    *out_size = builder->size;

    uint8_t* result = (uint8_t*)malloc(builder->size);
    if (result) {
        memcpy(result, builder->data, builder->size);
    }

    free(sorted_exports);
    export_builder_free(builder);
    return result;

fail:
    free(sorted_exports);
    export_builder_free(builder);
    return NULL;
}

ArkLinkResult ark_backend_pe_link(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output) {
    if (!ctx || !input || !output) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    memset(output, 0, sizeof(ArkBackendOutput));

    const uint32_t dos_header_size = 0x80;
    const uint32_t pe_signature_size = 4;
    const uint32_t section_alignment = PE_SECTION_ALIGNMENT;
    const uint32_t file_alignment = PE_FILE_ALIGNMENT;

    ArkImageLayout* layout = ark_layout_create(input, section_alignment, file_alignment);
    if (!layout) {
        return ARK_LINK_ERR_MEMORY;
    }
    if (layout->image_size > UINT32_MAX) {
        fprintf(stderr, "ArkLink: PE image exceeds 4 GiB\n");
        ark_layout_destroy(layout);
        return ARK_LINK_ERR_FORMAT;
    }

    size_t reloc_data_size = 0;
    uint8_t* reloc_data = NULL;

    size_t temp_maps_size = 0;
    if (pe_safe_mul(input->section_count, sizeof(ArkSectionRvaMap), &temp_maps_size) != 0) {
        ark_layout_destroy(layout);
        return ARK_LINK_ERR_FORMAT;
    }
    ArkSectionRvaMap* temp_maps = (ArkSectionRvaMap*)calloc(temp_maps_size, 1);
    if (!temp_maps) {
        ark_layout_destroy(layout);
        return ARK_LINK_ERR_MEMORY;
    }

    for (size_t i = 0; i < input->section_count; i++) {
        const ArkSectionLayout* sec = ark_layout_get_section(layout, i);
        if (sec) {
            if (sec->virtual_size > UINT32_MAX || sec->virtual_address - layout->image_base > UINT32_MAX) {
                fprintf(stderr, "ArkLink: section %zu address or size exceeds 32 bits\n", i);
                free(temp_maps);
                ark_layout_destroy(layout);
                return ARK_LINK_ERR_FORMAT;
            }
            temp_maps[i].rva = (uint32_t)(sec->virtual_address - layout->image_base);
            temp_maps[i].size = (uint32_t)sec->virtual_size;
        }
    }

    reloc_data = generate_relocation_table(input, temp_maps, &reloc_data_size);
    if (!reloc_data && reloc_data_size != 0) {
        free(temp_maps);
        ark_layout_destroy(layout);
        return ARK_LINK_ERR_MEMORY;
    }

    size_t import_data_size = 0;
    uint32_t import_iat_rva = 0;
    size_t import_iat_total_entries = 0;
    uint8_t* import_data = NULL;

    uint64_t idata_rva64 = layout->data_segment_end - layout->image_base;
    if (idata_rva64 > UINT32_MAX) {
        fprintf(stderr, "ArkLink: import directory RVA exceeds 32 bits\n");
        free(reloc_data);
        free(temp_maps);
        ark_layout_destroy(layout);
        return ARK_LINK_ERR_FORMAT;
    }
    uint32_t idata_rva = (uint32_t)idata_rva64;
    if (input->imports && input->import_count > 0) {
        import_data =
            generate_import_table(input, idata_rva, &import_data_size, &import_iat_rva, &import_iat_total_entries);
        if (!import_data) {
            free(reloc_data);
            free(temp_maps);
            ark_layout_destroy(layout);
            return ARK_LINK_ERR_MEMORY;
        }
    }

    size_t export_data_size = 0;
    uint8_t* export_data = NULL;

    uint64_t edata_aligned = ark_backend_align_up(import_data_size, section_alignment);
    if (edata_aligned == UINT64_MAX || idata_rva64 > UINT32_MAX - edata_aligned) {
        fprintf(stderr, "ArkLink: export directory RVA overflow\n");
        free(reloc_data);
        free(import_data);
        free(temp_maps);
        ark_layout_destroy(layout);
        return ARK_LINK_ERR_FORMAT;
    }
    uint32_t edata_rva = (uint32_t)(idata_rva64 + edata_aligned);
    if (input->exports && input->export_count > 0) {
        export_data = generate_export_table(input, edata_rva, &export_data_size, temp_maps);
        if (!export_data) {
            free(import_data);
            free(reloc_data);
            free(temp_maps);
            ark_layout_destroy(layout);
            return ARK_LINK_ERR_MEMORY;
        }
    }

    int has_reloc_section = (reloc_data_size > 0);
    int has_idata_section = (import_data_size > 0);
    int has_edata_section = (export_data_size > 0);
    size_t total_sections =
        input->section_count + (has_idata_section ? 1 : 0) + (has_edata_section ? 1 : 0) + (has_reloc_section ? 1 : 0);

    uint32_t headers_size = 0;
    if (pe_calc_header_size(total_sections, file_alignment, &headers_size) != 0) {
        free(temp_maps);
        ark_layout_destroy(layout);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }
    uint32_t image_size = (uint32_t)ark_layout_calc_total_size(layout, 1);

    size_t total_file_size = headers_size;
    for (size_t i = 0; i < input->section_count; i++) {
        const ArkSectionLayout* sec = ark_layout_get_section(layout, i);
        if (sec && sec->segment_type != ARK_SEGMENT_BSS) {
            if (pe_add_aligned(&total_file_size, sec->file_size, file_alignment) != 0) {
                fprintf(stderr, "ArkLink: PE file size overflow\n");
                free(temp_maps);
                ark_layout_destroy(layout);
                free(reloc_data);
                free(import_data);
                free(export_data);
                return ARK_LINK_ERR_FORMAT;
            }
        }
    }
    if (has_idata_section && pe_add_aligned(&total_file_size, import_data_size, file_alignment) != 0) {
        fprintf(stderr, "ArkLink: PE file size overflow\n");
        free(temp_maps);
        ark_layout_destroy(layout);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }
    if (has_edata_section && pe_add_aligned(&total_file_size, export_data_size, file_alignment) != 0) {
        fprintf(stderr, "ArkLink: PE file size overflow\n");
        free(temp_maps);
        ark_layout_destroy(layout);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }
    if (has_reloc_section && pe_add_aligned(&total_file_size, reloc_data_size, file_alignment) != 0) {
        fprintf(stderr, "ArkLink: PE file size overflow\n");
        free(temp_maps);
        ark_layout_destroy(layout);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }

    output->data = (uint8_t*)calloc(1, total_file_size);
    if (!output->data) {
        free(temp_maps);
        ark_layout_destroy(layout);
        if (reloc_data) {
            free(reloc_data);
        }
        if (import_data) {
            free(import_data);
        }
        if (export_data) {
            free(export_data);
        }
        return ARK_LINK_ERR_MEMORY;
    }

    output->size = total_file_size;
    output->image_base = layout->image_base;

    size_t section_maps_size = 0;
    if (pe_safe_mul(total_sections, sizeof(ArkSectionRvaMap), &section_maps_size) != 0) {
        free(output->data);
        output->data = NULL;
        free(temp_maps);
        ark_layout_destroy(layout);
        free(reloc_data);
        free(import_data);
        free(export_data);
        memset(output, 0, sizeof(*output));
        return ARK_LINK_ERR_FORMAT;
    }
    output->section_maps = (ArkSectionRvaMap*)calloc(section_maps_size, 1);
    if (!output->section_maps) {
        free(output->data);
        free(temp_maps);
        ark_layout_destroy(layout);
        if (reloc_data) {
            free(reloc_data);
        }
        if (import_data) {
            free(import_data);
        }
        free(export_data);
        memset(output, 0, sizeof(*output));
        return ARK_LINK_ERR_MEMORY;
    }

    output->section_count = total_sections;

    write_dos_header(output->data);

    if (input->entry_section >= input->section_count) {
        fprintf(stderr, "ArkLink: entry section %u out of range\n", input->entry_section);
        free(output->data);
        free(output->section_maps);
        memset(output, 0, sizeof(*output));
        ark_layout_destroy(layout);
        free(temp_maps);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }
    const ArkSectionLayout* entry_sec = ark_layout_get_section(layout, input->entry_section);
    if (!entry_sec) {
        fprintf(stderr, "ArkLink: entry section %u has no layout\n", input->entry_section);
        free(output->data);
        free(output->section_maps);
        memset(output, 0, sizeof(*output));
        ark_layout_destroy(layout);
        free(temp_maps);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }
    uint64_t entry_rva64 = (uint64_t)(entry_sec->virtual_address - layout->image_base) + input->entry_offset;
    if (entry_rva64 > UINT32_MAX) {
        fprintf(stderr, "ArkLink: entry point RVA exceeds 32 bits\n");
        free(output->data);
        free(output->section_maps);
        memset(output, 0, sizeof(*output));
        ark_layout_destroy(layout);
        free(temp_maps);
        free(reloc_data);
        free(import_data);
        free(export_data);
        return ARK_LINK_ERR_FORMAT;
    }
    uint32_t entry_point_rva = (uint32_t)entry_rva64;

    uint32_t reloc_rva = 0;
    if (has_reloc_section) {
        uint64_t reloc_aligned = ark_backend_align_up(export_data_size, section_alignment);
        if (reloc_aligned == UINT64_MAX || (uint64_t)edata_rva > UINT32_MAX - reloc_aligned) {
            fprintf(stderr, "ArkLink: relocation directory RVA overflow\n");
            free(output->data);
            free(output->section_maps);
            memset(output, 0, sizeof(*output));
            ark_layout_destroy(layout);
            free(temp_maps);
            free(reloc_data);
            free(import_data);
            free(export_data);
            return ARK_LINK_ERR_FORMAT;
        }
        reloc_rva = (uint32_t)((uint64_t)edata_rva + reloc_aligned);
    }

    uint32_t idata_virtual_size = 0;
    uint32_t idata_raw_size = 0;
    uint32_t edata_virtual_size = 0;
    uint32_t edata_raw_size = 0;
    uint32_t reloc_raw_size = 0;

    if (has_idata_section) {
        if (pe_align_u32(import_data_size, section_alignment, &idata_virtual_size) != 0 ||
            pe_align_u32(import_data_size, file_alignment, &idata_raw_size) != 0) {
            fprintf(stderr, "ArkLink: import section size overflow\n");
            goto pe_link_fail_output;
        }
    }

    if (has_edata_section) {
        if (pe_align_u32(export_data_size, section_alignment, &edata_virtual_size) != 0 ||
            pe_align_u32(export_data_size, file_alignment, &edata_raw_size) != 0) {
            fprintf(stderr, "ArkLink: export section size overflow\n");
            goto pe_link_fail_output;
        }
    }

    if (has_reloc_section) {
        if (pe_align_u32(reloc_data_size, file_alignment, &reloc_raw_size) != 0) {
            fprintf(stderr, "ArkLink: relocation section size overflow\n");
            goto pe_link_fail_output;
        }
    }

    uint32_t pe_offset = 0x80;

    uint8_t* pe_sig_ptr = output->data + pe_offset;
    const uint32_t signature = PE_SIGNATURE;
    memcpy(pe_sig_ptr, &signature, sizeof(signature));

    PE_COFF_HEADER* coff = (PE_COFF_HEADER*)(output->data + pe_offset + 4);
    coff->Machine = PE_MACHINE_AMD64;
    if (total_sections > UINT16_MAX) {
        fprintf(stderr, "ArkLink: PE section count %zu exceeds 16 bits\n", total_sections);
        goto pe_link_fail_output;
    }
    coff->NumberOfSections = (uint16_t)total_sections;
    coff->TimeDateStamp = 0;
    coff->PointerToSymbolTable = 0;
    coff->NumberOfSymbols = 0;
    coff->SizeOfOptionalHeader = sizeof(PE_OPTIONAL_HEADER_64);
    uint32_t characteristics = PE_CHAR_EXECUTABLE_IMAGE | PE_CHAR_LARGE_ADDRESS_AWARE | PE_CHAR_DEBUG_STRIPPED;
    if (input->output_type == ARK_OUTPUT_SHARED_LIB) {
        characteristics |= PE_CHAR_DLL;
    }
    coff->Characteristics = characteristics;

    size_t opt_offset = pe_offset + 4 + sizeof(PE_COFF_HEADER);
    if (opt_offset + sizeof(PE_OPTIONAL_HEADER_64) > total_file_size) {
        fprintf(stderr, "ArkLink: PE optional header exceeds image\n");
        goto pe_link_fail_output;
    }
    uint8_t* opt_ptr = output->data + opt_offset;
    PE_OPTIONAL_HEADER_64* opt = (PE_OPTIONAL_HEADER_64*)opt_ptr;
    opt->Magic = PE_OPT_HDR_MAGIC_PE32_PLUS;
    opt->MajorLinkerVersion = 1;
    opt->MinorLinkerVersion = 0;

    uint64_t size_of_code = 0;
    for (size_t i = 0; i < input->section_count; i++) {
        if (input->sections[i].kind == ARK_SECTION_CODE) {
            uint64_t aligned_size = ark_backend_align_up(input->sections[i].size, section_alignment);
            if (aligned_size == UINT64_MAX || size_of_code > UINT32_MAX - aligned_size) {
                fprintf(stderr, "ArkLink: SizeOfCode overflow\n");
                goto pe_link_fail_output;
            }
            size_of_code += aligned_size;
        }
    }
    opt->SizeOfCode = (uint32_t)size_of_code;

    uint64_t size_of_initialized_data = 0;
    for (size_t i = 0; i < input->section_count; i++) {
        if (input->sections[i].kind == ARK_SECTION_DATA) {
            uint64_t aligned_size = ark_backend_align_up(input->sections[i].size, section_alignment);
            if (aligned_size == UINT64_MAX || size_of_initialized_data > UINT32_MAX - aligned_size) {
                fprintf(stderr, "ArkLink: SizeOfInitializedData overflow\n");
                goto pe_link_fail_output;
            }
            size_of_initialized_data += aligned_size;
        }
    }

    if (has_idata_section) {
        if (size_of_initialized_data > UINT32_MAX - idata_virtual_size) {
            fprintf(stderr, "ArkLink: SizeOfInitializedData overflow\n");
            goto pe_link_fail_output;
        }
        size_of_initialized_data += idata_virtual_size;
    }

    if (has_edata_section) {
        if (size_of_initialized_data > UINT32_MAX - edata_virtual_size) {
            fprintf(stderr, "ArkLink: SizeOfInitializedData overflow\n");
            goto pe_link_fail_output;
        }
        size_of_initialized_data += edata_virtual_size;
    }
    opt->SizeOfInitializedData = (uint32_t)size_of_initialized_data;

    opt->SizeOfUninitializedData = 0;
    opt->AddressOfEntryPoint = entry_point_rva;
    uint32_t first_section_rva = 0;
    if (pe_align_u32(headers_size, section_alignment, &first_section_rva) != 0) {
        fprintf(stderr, "ArkLink: first section RVA overflow\n");
        goto pe_link_fail_output;
    }
    opt->BaseOfCode = first_section_rva;
    opt->ImageBase = output->image_base;
    opt->SectionAlignment = section_alignment;
    opt->FileAlignment = file_alignment;
    opt->MajorOperatingSystemVersion = 6;
    opt->MinorOperatingSystemVersion = 0;
    opt->MajorImageVersion = 0;
    opt->MinorImageVersion = 0;
    opt->MajorSubsystemVersion = 6;
    opt->MinorSubsystemVersion = 0;
    opt->Win32VersionValue = 0;
    opt->SizeOfImage = image_size;

    opt->SizeOfHeaders = headers_size;
    opt->CheckSum = 0;
    opt->Subsystem = input->subsystem ? input->subsystem : PE_SUBSYSTEM_WINDOWS_CUI;

    opt->DllCharacteristics = 0x0120;
    opt->SizeOfStackReserve = input->stack_size ? input->stack_size : 0x100000;
    opt->SizeOfStackCommit = 0x1000;
    opt->SizeOfHeapReserve = 0x100000;
    opt->SizeOfHeapCommit = 0x1000;
    opt->LoaderFlags = 0;
    opt->NumberOfRvaAndSizes = 16;

    for (int i = 0; i < 16; i++) {
        opt->DataDirectory[i].VirtualAddress = 0;
        opt->DataDirectory[i].Size = 0;
    }

    if (has_edata_section) {
        opt->DataDirectory[PE_DD_EXPORT].VirtualAddress = edata_rva;
        opt->DataDirectory[PE_DD_EXPORT].Size = (uint32_t)export_data_size;
    }

    if (has_idata_section) {
        opt->DataDirectory[PE_DD_IMPORT].VirtualAddress = idata_rva;
        opt->DataDirectory[PE_DD_IMPORT].Size = (uint32_t)import_data_size;

        /* The import builder reserves one IAT slot per import plus one
         * terminator per module; the data directory describes that array. */
        uint64_t iat_size = import_iat_total_entries * sizeof(PE_IMPORT_LOOKUP_ENTRY);
        if (iat_size > UINT32_MAX) {
            fprintf(stderr, "ArkLink: IAT directory size exceeds 32 bits\n");
            goto pe_link_fail_output;
        }
        opt->DataDirectory[PE_DD_IAT].VirtualAddress = import_iat_rva;
        opt->DataDirectory[PE_DD_IAT].Size = (uint32_t)iat_size;
    }

    if (has_reloc_section) {
        opt->DataDirectory[PE_DD_BASE_RELOCATION].VirtualAddress = reloc_rva;
        opt->DataDirectory[PE_DD_BASE_RELOCATION].Size = (uint32_t)reloc_data_size;
    }

    uint32_t current_rva = first_section_rva;
    uint32_t current_file_offset = headers_size;

    for (size_t i = 0; i < input->section_count; i++) {
        ArkSectionBuffer* sec = &input->sections[i];

        if (sec->size > UINT32_MAX) {
            fprintf(stderr, "ArkLink: section %zu size exceeds 32 bits\n", i);
            goto pe_link_fail_output;
        }
        uint32_t virtual_size = (uint32_t)sec->size;
        uint32_t virtual_address = current_rva;
        uint64_t raw_aligned = ark_backend_align_up(sec->size, file_alignment);
        if (raw_aligned == UINT64_MAX || raw_aligned > UINT32_MAX) {
            fprintf(stderr, "ArkLink: section %zu raw size overflow\n", i);
            goto pe_link_fail_output;
        }
        uint32_t raw_size = (uint32_t)raw_aligned;
        uint32_t raw_offset = current_file_offset;

        uint32_t characteristics = PE_SCN_MEM_READ;
        if (sec->flags & ARK_SECTION_EXEC) {
            characteristics |= PE_SCN_CNT_CODE | PE_SCN_MEM_EXECUTE;
        }
        if (sec->flags & ARK_SECTION_WRITE) {
            characteristics |= PE_SCN_MEM_WRITE;
        }
        if (!(sec->flags & ARK_SECTION_EXEC) && (sec->flags & ARK_SECTION_READ)) {
            characteristics |= PE_SCN_CNT_INITIALIZED_DATA;
        }

        char section_name[9] = {0};
        if (sec->kind == ARK_SECTION_CODE) {
            strcpy(section_name, ".text");
        } else if (sec->kind == ARK_SECTION_DATA) {
            strcpy(section_name, ".data");
        } else if (sec->kind == ARK_SECTION_RODATA) {
            strcpy(section_name, ".rdata");
        } else {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            snprintf(section_name, sizeof(section_name), "sect%lu", (unsigned long)i);
#pragma GCC diagnostic pop
        }
        write_section_header(output->data, i, section_name, virtual_size, virtual_address, raw_size, raw_offset,
                             characteristics);

        output->section_maps[i].rva = virtual_address;
        output->section_maps[i].size = virtual_size;
        output->section_maps[i].file_offset = raw_offset;
        output->section_maps[i].flags = sec->flags;

        if (sec->size > 0 && !sec->data) {
            fprintf(stderr, "ArkLink: section %zu has no data\n", i);
            goto pe_link_fail_output;
        }
        if (sec->data && sec->size > 0) {
            if ((uint64_t)raw_offset + sec->size > output->size) {
                fprintf(stderr, "ArkLink: section %zu data exceeds image\n", i);
                goto pe_link_fail_output;
            }
            memcpy(output->data + raw_offset, sec->data, sec->size);
        }

        uint64_t next_va = (uint64_t)current_rva + ark_backend_align_up(sec->size, section_alignment);
        if (next_va > UINT32_MAX) {
            fprintf(stderr, "ArkLink: section %zu RVA overflow\n", i);
            goto pe_link_fail_output;
        }
        current_rva = (uint32_t)next_va;
        if ((uint64_t)current_file_offset + raw_size > output->size) {
            fprintf(stderr, "ArkLink: section %zu file offset exceeds image\n", i);
            goto pe_link_fail_output;
        }
        current_file_offset += raw_size;
    }

    if (has_idata_section) {
        uint32_t idata_section_idx = (uint32_t)input->section_count;
        uint32_t idata_file_offset = current_file_offset;

        write_section_header(output->data, idata_section_idx, ".idata", (uint32_t)import_data_size, idata_rva,
                             idata_raw_size, idata_file_offset, PE_SCN_CNT_INITIALIZED_DATA | PE_SCN_MEM_READ);

        output->section_maps[idata_section_idx].rva = idata_rva;
        output->section_maps[idata_section_idx].size = (uint32_t)import_data_size;
        output->section_maps[idata_section_idx].file_offset = idata_file_offset;
        output->section_maps[idata_section_idx].flags = ARK_SECTION_READ;

        if (import_data) {
            if ((uint64_t)idata_file_offset + import_data_size > output->size) {
                fprintf(stderr, "ArkLink: import data exceeds image\n");
                goto pe_link_fail_output;
            }
            memcpy(output->data + idata_file_offset, import_data, import_data_size);
        }

        current_file_offset += idata_raw_size;
    }

    if (has_edata_section) {
        uint32_t edata_section_idx = (uint32_t)(input->section_count + (has_idata_section ? 1 : 0));
        uint32_t edata_file_offset = current_file_offset;

        write_section_header(output->data, edata_section_idx, ".edata", (uint32_t)export_data_size, edata_rva,
                             edata_raw_size, edata_file_offset, PE_SCN_CNT_INITIALIZED_DATA | PE_SCN_MEM_READ);

        output->section_maps[edata_section_idx].rva = edata_rva;
        output->section_maps[edata_section_idx].size = (uint32_t)export_data_size;
        output->section_maps[edata_section_idx].file_offset = edata_file_offset;
        output->section_maps[edata_section_idx].flags = ARK_SECTION_READ;

        if (export_data) {
            if ((uint64_t)edata_file_offset + export_data_size > output->size) {
                fprintf(stderr, "ArkLink: export data exceeds image\n");
                goto pe_link_fail_output;
            }
            memcpy(output->data + edata_file_offset, export_data, export_data_size);
        }

        current_file_offset += edata_raw_size;
    }

    if (has_reloc_section) {
        uint32_t reloc_section_idx =
            (uint32_t)(input->section_count + (has_idata_section ? 1 : 0) + (has_edata_section ? 1 : 0));
        uint32_t reloc_file_offset = current_file_offset;

        write_section_header(output->data, reloc_section_idx, ".reloc", (uint32_t)reloc_data_size, reloc_rva,
                             reloc_raw_size, reloc_file_offset, PE_SCN_CNT_INITIALIZED_DATA | PE_SCN_MEM_READ);

        output->section_maps[reloc_section_idx].rva = reloc_rva;
        output->section_maps[reloc_section_idx].size = (uint32_t)reloc_data_size;
        output->section_maps[reloc_section_idx].file_offset = reloc_file_offset;
        output->section_maps[reloc_section_idx].flags = ARK_SECTION_READ;

        if (reloc_data) {
            if ((uint64_t)reloc_file_offset + reloc_data_size > output->size) {
                fprintf(stderr, "ArkLink: relocation data exceeds image\n");
                goto pe_link_fail_output;
            }
            memcpy(output->data + reloc_file_offset, reloc_data, reloc_data_size);
        }
    }

    if (input->relocs && input->reloc_count > 0) {
        for (size_t i = 0; i < input->reloc_count; i++) {
            ArkResolverReloc* reloc = &input->relocs[i];
            if (!reloc->symbol) {
                continue;
            }

            if (reloc->section_index >= output->section_count) {
                fprintf(stderr, "ArkLink: relocation %zu section index %u out of range\n", i, reloc->section_index);
                goto pe_link_fail_output;
            }

            uint64_t symbol_addr;
            if (reloc->symbol->import_module != NULL) {

                uint32_t import_idx = 0;
                int found = 0;
                for (size_t j = 0; j < input->import_count; j++) {
                    const char* imp_sym = input->imports[j].symbol;
                    if (imp_sym && reloc->symbol->name && strcmp(imp_sym, reloc->symbol->name) == 0) {
                        import_idx = (uint32_t)j;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    fprintf(stderr, "ArkLink: relocation %zu import symbol not found: %s\n", i,
                            reloc->symbol->name ? reloc->symbol->name : "(null)");
                    goto pe_link_fail_output;
                }
                symbol_addr = output->image_base + import_iat_rva + (uint64_t)import_idx * sizeof(PE_IMPORT_LOOKUP_ENTRY);
            } else {

                if (reloc->symbol->section_index >= output->section_count) {
                    fprintf(stderr, "ArkLink: relocation %zu symbol section index %u out of range\n", i,
                            reloc->symbol->section_index);
                    goto pe_link_fail_output;
                }
                symbol_addr =
                    output->image_base + output->section_maps[reloc->symbol->section_index].rva + reloc->symbol->value;
            }

            uint64_t file_offset64 = (uint64_t)output->section_maps[reloc->section_index].file_offset + reloc->offset;
            if (file_offset64 > output->size) {
                fprintf(stderr, "ArkLink: relocation %zu file offset exceeds image\n", i);
                goto pe_link_fail_output;
            }
            uint32_t reloc_file_offset = (uint32_t)file_offset64;

            ArkRelocProcessor proc = {0};

            switch (reloc->type) {
            case ARK_RELOC_ABS64:
                proc.action = ARK_RELOC_APPLY_ABSOLUTE;
                proc.field_size = ARK_RELOC_FIELD_64;
                break;
            case ARK_RELOC_ADDR32:
                proc.action = ARK_RELOC_APPLY_ABSOLUTE;
                proc.field_size = ARK_RELOC_FIELD_32;
                break;
            case ARK_RELOC_PC32:
                proc.action = ARK_RELOC_APPLY_RELATIVE;
                proc.field_size = ARK_RELOC_FIELD_32;
                proc.is_pc_relative = 1;
                break;
            default:
                fprintf(stderr, "ArkLink: unsupported PE relocation type %d at index %zu\n", (int)reloc->type, i);
                goto pe_link_fail_output;
            }

            proc.target_section = reloc->section_index;
            proc.offset = reloc_file_offset;
            proc.symbol_value = symbol_addr;
            proc.addend = reloc->addend;

            uint64_t p_vaddr = 0;
            if (proc.is_pc_relative) {
                p_vaddr = output->image_base + output->section_maps[reloc->section_index].rva + reloc->offset;
            }

            if (ark_reloc_apply_pe_base(output->data, output->size, &proc, p_vaddr) != 0) {
                fprintf(stderr, "ArkLink: failed to apply PE relocation %zu\n", i);
                goto pe_link_fail_output;
            }
        }
    }

    ark_layout_destroy(layout);
    free(temp_maps);
    if (reloc_data) {
        free(reloc_data);
    }
    if (import_data) {
        free(import_data);
    }
    if (export_data) {
        free(export_data);
    }

    return ARK_LINK_OK;

pe_link_fail_output:
    free(output->data);
    free(output->section_maps);
    memset(output, 0, sizeof(*output));
    ark_layout_destroy(layout);
    free(temp_maps);
    free(reloc_data);
    free(import_data);
    free(export_data);
    return ARK_LINK_ERR_FORMAT;
}
