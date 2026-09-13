#ifndef ARKLINK_BACKEND_H
#define ARKLINK_BACKEND_H

#include "Context.h"
#include "Resolver.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ArkOutputType { ARK_OUTPUT_EXECUTABLE, ARK_OUTPUT_SHARED_LIB, ARK_OUTPUT_OBJECT } ArkOutputType;

typedef enum ArkRelocFieldSize {
    ARK_RELOC_FIELD_8 = 1,
    ARK_RELOC_FIELD_16 = 2,
    ARK_RELOC_FIELD_32 = 4,
    ARK_RELOC_FIELD_64 = 8
} ArkRelocFieldSize;

typedef struct ArkBuffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
    uint32_t base_rva;
} ArkBuffer;

/** @brief Create an owned byte buffer, or return NULL on allocation failure. */
ArkBuffer* ark_buffer_create(size_t initial_capacity, uint32_t base_rva);
/** @brief Release buf and its byte storage, accepting NULL. */
void ark_buffer_destroy(ArkBuffer* buf);
/** @brief Append bytes, returning their offset or SIZE_MAX on failure; source may alias buf. */
size_t ark_buffer_append(ArkBuffer* buf, const void* data, size_t len);
/** @brief Append zeroed bytes, returning their offset or SIZE_MAX on failure. */
size_t ark_buffer_append_zero(ArkBuffer* buf, size_t len);
/** @brief Append a terminated string, returning its offset or UINT32_MAX on failure. */
uint32_t ark_buffer_add_string(ArkBuffer* buf, const char* str);
/** @brief Return a borrowed character pointer at an in-range offset, or NULL if invalid. */
char* ark_buffer_get_string(ArkBuffer* buf, uint32_t offset);
/** @brief Convert a buffer offset to a relative virtual address. */
uint32_t ark_buffer_get_rva(ArkBuffer* buf, size_t offset);
/** @brief Append zero padding to align buf size. */
void ark_buffer_align(ArkBuffer* buf, size_t alignment);

typedef enum ArkSegmentType {
    ARK_SEGMENT_CODE,
    ARK_SEGMENT_DATA,
    ARK_SEGMENT_RODATA,
    ARK_SEGMENT_BSS,
    ARK_SEGMENT_TLS
} ArkSegmentType;

typedef struct ArkSectionLayout {
    uint32_t section_index;
    uint64_t virtual_address;
    uint64_t file_offset;
    uint64_t virtual_size;
    uint64_t file_size;
    uint32_t alignment;
    ArkSegmentType segment_type;
    uint32_t flags;
} ArkSectionLayout;

typedef struct ArkImageLayout {
    ArkSectionLayout* sections;
    size_t section_count;

    uint64_t image_base;
    uint64_t image_size;
    uint64_t file_size;
    uint64_t headers_size;

    uint64_t code_segment_start;
    uint64_t code_segment_end;
    uint64_t data_segment_start;
    uint64_t data_segment_end;

    uint32_t section_alignment;
    uint32_t file_alignment;
} ArkImageLayout;

/** @brief Plan aligned input sections, returning an owned layout or NULL on failure. */
ArkImageLayout* ark_layout_create(const ArkBackendInput* input, uint32_t section_alignment, uint32_t file_alignment);
/** @brief Release layout and its section records, accepting NULL. */
void ark_layout_destroy(ArkImageLayout* layout);
/** @brief Return a borrowed section layout, or NULL for an invalid index. */
const ArkSectionLayout* ark_layout_get_section(const ArkImageLayout* layout, size_t index);
/** @brief Return the planned image extent, optionally including BSS. */
uint64_t ark_layout_calc_total_size(const ArkImageLayout* layout, int include_bss);

typedef enum ArkRelocAction {
    ARK_RELOC_APPLY_ABSOLUTE,
    ARK_RELOC_APPLY_RELATIVE,
    ARK_RELOC_APPLY_GOT,
    ARK_RELOC_APPLY_SECREL,
    ARK_RELOC_GENERATE_BASE_REL
} ArkRelocAction;

typedef struct ArkRelocProcessor {
    ArkRelocAction action;
    ArkRelocFieldSize field_size;
    int is_pc_relative;
    uint32_t target_section;
    uint32_t offset;
    uint64_t symbol_value;
    int64_t addend;
} ArkRelocProcessor;

typedef void (*ArkRelocApplyFn)(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr);
typedef int (*ArkRelocShouldProcessFn)(const ArkResolverReloc* reloc, void* user_data);

/** @brief Apply relocations accepted by filter_fn using apply_fn and layout. */
void ark_reloc_process_all(ArkResolverReloc* relocs, size_t count, ArkRelocApplyFn apply_fn,
                           ArkRelocShouldProcessFn filter_fn, void* user_data, uint8_t* section_data,
                           size_t section_size, const ArkImageLayout* layout);

/** @brief Apply one bounded ELF relocation field at its virtual address. */
void ark_reloc_apply_elf(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr);
/** @brief Apply one bounded PE base relocation field. */
void ark_reloc_apply_pe_base(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr);

typedef struct ArkImportEntry {
    const char* module;
    const char* symbol;
    uint32_t iat_rva;
    int is_function;
} ArkImportEntry;

typedef struct ArkExportEntry {
    const char* name;
    uint32_t ordinal;
    uint32_t section_index;
    uint32_t offset;
    uint64_t value;
    int is_function;
} ArkExportEntry;

typedef struct ArkModuleImports {
    const char* module_name;
    const char** symbols;
    size_t symbol_count;
} ArkModuleImports;

typedef struct ArkImportGroup {
    ArkModuleImports* modules;
    size_t module_count;
} ArkImportGroup;

/** @brief Group imports by module, returning owned lists or NULL on allocation failure. */
ArkImportGroup* ark_import_group_create(const ArkImportEntry* imports, size_t count);
/** @brief Release grouped import lists, accepting NULL. */
void ark_import_group_destroy(ArkImportGroup* group);

typedef struct ArkBackendInput {
    ArkSectionBuffer* sections;
    size_t section_count;
    uint64_t entry_point;
    uint32_t entry_section;
    uint32_t entry_offset;
    ArkImportEntry* imports;
    size_t import_count;
    ArkExportEntry* exports;
    size_t export_count;
    const char* export_name;
    uint32_t export_ordinal_base;
    ArkResolverReloc* relocs;
    size_t reloc_count;
    uint64_t image_base;
    uint64_t stack_size;
    uint32_t subsystem;
    ArkOutputType output_type;
} ArkBackendInput;

typedef struct ArkSectionRvaMap {
    uint32_t rva;
    uint32_t size;
    uint32_t file_offset;
    uint32_t flags;
} ArkSectionRvaMap;

typedef struct ArkBackendOutput {
    uint8_t* data;
    size_t size;
    ArkSectionRvaMap* section_maps;
    size_t section_count;
    uint64_t image_base;
} ArkBackendOutput;

typedef ArkLinkResult (*ArkBackendLinkFn)(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output);

/** @brief Build a PE image in output; the caller owns successful image and section-map allocations. */
ArkLinkResult ark_backend_pe_link(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output);
/** @brief Build an ELF image in output; the caller owns successful image and section-map allocations. */
ArkLinkResult ark_backend_elf_link(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output);

/** @brief Return the address immediately after a relocation field. */
uint64_t ark_backend_calc_pc_relative(uint64_t p_vaddr, ArkRelocFieldSize field_size);
/** @brief Return whether input requires dynamic ELF metadata. */
int ark_backend_should_use_dynamic_elf(const ArkBackendInput* input);
/** @brief Round value up to a power-of-two alignment. */
uint64_t ark_backend_align_up(uint64_t value, uint64_t alignment);
/** @brief Round a 32-bit value up to a power-of-two alignment. */
uint32_t ark_backend_align_up_32(uint32_t value, uint32_t alignment);
/** @brief Return whether value is a nonzero power of two. */
int ark_backend_is_power_of_2(uint64_t value);

#ifdef __cplusplus
}
#endif

#endif
