#ifndef ARKLINK_LOADER_H
#define ARKLINK_LOADER_H

#include "Context.h"

struct ArkArchive;
typedef struct ArkArchive ArkArchive;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArkLoaderDiagnostics {
    const char* file;
    uint32_t line;
    ArkLinkResult code;
    const char* message;
} ArkLoaderDiagnostics;

typedef struct ArkLoaderOptions {
    int enable_strict_checks;
} ArkLoaderOptions;

typedef enum ArkSymbolBinding {
    ARK_BIND_LOCAL,
    ARK_BIND_GLOBAL,
    ARK_BIND_WEAK,
} ArkSymbolBinding;

typedef enum ArkSymbolVisibility {
    ARK_VISIBILITY_DEFAULT,
    ARK_VISIBILITY_HIDDEN,
} ArkSymbolVisibility;

typedef enum ArkSymbolType {
    ARK_SYM_NOTYPE,
    ARK_SYM_FUNC,
    ARK_SYM_OBJECT,
} ArkSymbolType;

typedef enum ArkRelocationType {
    ARK_RELOC_ABS64 = 1,   
    ARK_RELOC_ADDR32 = 2,  
    ARK_RELOC_PC32 = 3,    
    ARK_RELOC_GOTPC32 = 4,
    ARK_RELOC_SECREL32 = 5,
} ArkRelocationType;

#define ARK_SECTION_READ  0x01
#define ARK_SECTION_WRITE 0x02
#define ARK_SECTION_EXEC  0x04

typedef enum ArkSectionKind {
    ARK_SECTION_UNKNOWN = 0,
    ARK_SECTION_CODE = 1,
    ARK_SECTION_DATA = 2,
    ARK_SECTION_RODATA = 3,
    ARK_SECTION_BSS = 4,
    ARK_SECTION_TDATA = 5,
    ARK_SECTION_TBSS  = 6,
} ArkSectionKind;

typedef struct ArkSectionRange {
    uint32_t section_index;
    uint32_t offset;
    uint32_t size;
} ArkSectionRange;

typedef struct ArkRelocationDesc {
    uint64_t offset;
    uint32_t sym_idx;
    uint32_t section_index;
    uint16_t type;
    int64_t addend;
} ArkRelocationDesc;

typedef struct ArkSymbolDesc {
    const char* name;
    uint64_t value;
    uint32_t size;
    uint16_t section_index;
    uint8_t type;
    uint8_t binding;
    uint8_t visibility;
    const char* import_module;
} ArkSymbolDesc;

typedef struct ArkLinkSectionDesc {
    ArkSectionBuffer buffer;
    const char* name;
    uint32_t id;
} ArkLinkSectionDesc;

typedef struct ArkLinkRelocList {
    ArkRelocationDesc* relocs;
    size_t count;
} ArkLinkRelocList;

struct ArkSectionBuffer;

typedef struct ArkLinkSection {
    char name[32];
    const uint8_t* data;
    size_t size;
    size_t alignment;
    uint32_t flags;
    ArkSectionKind kind;

    ArkRelocationDesc* relocs;
    size_t reloc_count;
    size_t reloc_capacity;

    struct ArkSectionBuffer* buffer;
} ArkLinkSection;

typedef struct ArkSectionDesc {
    const char* name;
    const uint8_t* data;
    size_t size;
    size_t alignment;
    uint32_t flags;
} ArkSectionDesc;

typedef struct ArkLinkUnit {
    const char* path;
    ArkLinkSection* sections;
    size_t section_count;
    ArkSymbolDesc* symbols;
    size_t symbol_count;
    ArkLinkRelocList relocations;
    uint64_t entry_point;
    
    uint8_t* file_data;
    size_t file_size;
} ArkLinkUnit;

ArkLinkUnit* ark_link_unit_create(const char* path);
void ark_link_unit_destroy(ArkLinkUnit* unit);
ArkLinkSection* ark_link_unit_add_section(ArkLinkUnit* unit, const ArkSectionDesc* desc);
int ark_link_unit_add_symbol(ArkLinkUnit* unit, const ArkSymbolDesc* desc);

ArkLinkSection* ark_link_section_create(const char* name, const uint8_t* data, size_t size);
int ark_link_section_add_reloc(ArkLinkSection* section, const ArkRelocationDesc* desc);

ArkLinkResult ark_link_load_kro(const char* path, ArkLinkUnit** unit);
ArkLinkResult ark_link_load_coff(const char* path, ArkLinkUnit** unit);
ArkLinkResult ark_link_load_elf(const char* path, ArkLinkUnit** unit);

int ark_link_unit_add_reloc(ArkLinkUnit* unit, const ArkRelocationDesc* desc);

ArkLinkResult ark_loader_load_unit(ArkLinkContext* ctx, const char* path, const ArkLoaderOptions* opts, ArkLinkUnit** out_unit, ArkLoaderDiagnostics* diag);

#ifdef __cplusplus
}
#endif

#endif 