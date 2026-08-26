#ifndef KRT_KRO_WRITER_H
#define KRT_KRO_WRITER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define KRO_MAGIC       0x004F524B
#define KRO_VERSION     2
#define KRO_ARCH_X86_64  0x8664
#define KRO_ARCH_ARM64   0xAA64

typedef enum {
    KRO_SEC_UNDEF  = 0,
    KRO_SEC_TEXT   = 1,
    KRO_SEC_RODATA = 2,
    KRO_SEC_DATA   = 3,
    KRO_SEC_BSS    = 4,
} KROSectionIndex;

#define KRO_BIND_LOCAL   0
#define KRO_BIND_GLOBAL  1
#define KRO_BIND_WEAK    2

#define KRO_SYM_NOTYPE   0
#define KRO_SYM_FUNC     1
#define KRO_SYM_OBJECT   2

#define KRO_RELOC_ABS64  1
#define KRO_RELOC_ADDR32 2
#define KRO_RELOC_PC32   3

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
} KROHeader;

typedef struct {
    uint32_t name_offset;
    uint32_t value;
    uint32_t size;
    uint32_t section;
    uint32_t binding;
    uint32_t type;
    uint32_t flags;
    uint32_t reserved;
} KROSymbol;

typedef struct {
    uint32_t offset;
    uint32_t sym_idx;
    uint32_t type;
    int32_t  addend;
} KRORelocation;

#pragma pack(pop)

typedef struct KROWriter KROWriter;

KROWriter* kro_writer_create(void);
void kro_writer_destroy(KROWriter* writer);

uint32_t kro_write_code(KROWriter* writer, const void* data, uint32_t size);
uint32_t kro_write_data(KROWriter* writer, const void* data, uint32_t size);
uint32_t kro_write_rodata(KROWriter* writer, const void* data, uint32_t size);

uint32_t kro_write_code_aligned(KROWriter* writer, const void* data, uint32_t size, uint32_t align);
uint32_t kro_write_data_aligned(KROWriter* writer, const void* data, uint32_t size, uint32_t align);

bool kro_reserve_bss(KROWriter* writer, uint32_t size, uint8_t align_log2);

int kro_add_symbol(KROWriter* writer, const char* name, uint8_t type, uint8_t bind,
                  uint32_t sec_idx, uint64_t value);
int kro_add_undefined_symbol(KROWriter* writer, const char* name);
int kro_add_import_symbol(KROWriter* writer, const char* name, const char* module);
int kro_find_symbol(KROWriter* writer, const char* name);
void kro_update_symbol_value(KROWriter* writer, int sym_idx, uint64_t value);

void kro_add_reloc(KROWriter* writer, uint32_t sec_idx, uint64_t offset,
                   uint32_t sym_idx, uint16_t type, int16_t addend);

bool kro_set_entry_point(KROWriter* writer, uint64_t offset);

uint32_t kro_get_code_offset(KROWriter* writer);
uint32_t kro_get_data_offset(KROWriter* writer);
uint32_t kro_get_rodata_offset(KROWriter* writer);
void kro_set_code_offset(KROWriter* writer, uint32_t offset);

bool kro_write_file(KROWriter* writer, const char* filename);

#endif
