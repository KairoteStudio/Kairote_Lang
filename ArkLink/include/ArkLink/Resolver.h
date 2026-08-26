#ifndef ARKLINK_RESOLVER_H
#define ARKLINK_RESOLVER_H

#include "Context.h"
#include "Loader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArkBackendInput ArkBackendInput;

typedef struct ArkResolverSymbol {
    const char* name;
    ArkSymbolBinding binding;
    ArkSymbolVisibility visibility;
    ArkSectionBuffer* section;
    uint32_t section_index;
    uint32_t value;
    uint32_t size;
    int32_t import_id;
    int is_export;
    const char* import_module;
    uint32_t iat_entry_rva;
    uint8_t type;
} ArkResolverSymbol;

typedef struct ArkResolverReloc {
    ArkSectionBuffer* section;
    uint32_t section_index;
    uint32_t original_section_index;
    uint32_t offset;
    uint32_t type;
    const ArkResolverSymbol* symbol;
    int64_t addend;
    uint32_t target_rva;
    uint32_t symbol_rva;
} ArkResolverReloc;

typedef struct ArkImportModule {
    const char* name;
    uint32_t name_rva;
    uint32_t first_thunk_rva;
    uint32_t orig_first_thunk_rva;
    uint32_t symbol_count;
    uint32_t symbol_start;
} ArkImportModule;

typedef struct ArkImportBinding {
    const char* module;
    const char* symbol;
    uint32_t slot;
    uint32_t iat_entry_rva;
    uint32_t thunk_index;
} ArkImportBinding;

typedef struct ArkExportBinding {
    const char* name;
    uint32_t symbol_index;
    uint32_t ordinal;
} ArkExportBinding;

typedef struct ArkResolverPlan {
    ArkBackendInput* backend_input;
    ArkResolverSymbol* symbols;
    size_t symbol_count;
    ArkResolverReloc* relocs;
    size_t reloc_count;
    ArkImportModule* import_modules;
    size_t import_module_count;
    ArkImportBinding* imports;
    size_t import_count;
    ArkExportBinding* exports;
    size_t export_count;
    uint32_t entry_symbol;
    uint32_t entry_section;
    uint32_t entry_offset;
} ArkResolverPlan;

ArkLinkResult ark_resolver_resolve(ArkLinkContext* ctx, ArkLinkUnit* const* units, size_t unit_count, ArkResolverPlan* out_plan);
void ark_resolver_plan_destroy(ArkLinkContext* ctx, ArkResolverPlan* plan);

#ifdef __cplusplus
}
#endif

#endif