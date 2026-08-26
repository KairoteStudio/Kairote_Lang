#include "KroWriter.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 1024
#define STRING_INITIAL_CAPACITY 256

#define DEFAULT_CODE_ALIGN 16
#define DEFAULT_DATA_ALIGN 8
#define DEFAULT_RODATA_ALIGN 8
#define DEFAULT_BSS_ALIGN 8

typedef struct {
    KRORelocation* relocs;
    uint32_t count;
    uint32_t capacity;
} RelocList;

typedef struct KROWriter {
    uint8_t* text;
    uint8_t* data;
    uint8_t* rodata;

    uint32_t text_capacity;
    uint32_t data_capacity;
    uint32_t rodata_capacity;

    uint32_t text_offset;
    uint32_t data_offset;
    uint32_t rodata_offset;

    uint8_t text_align;
    uint8_t data_align;
    uint8_t rodata_align;
    uint8_t bss_align;

    uint32_t bss_size;

    KROSymbol* symbols;
    uint32_t sym_capacity;
    uint32_t sym_count;

    RelocList text_relocs;
    RelocList data_relocs;
    RelocList rodata_relocs;
    RelocList bss_relocs;

    char* strings;
    uint32_t string_capacity;
    uint32_t string_size;

    uint64_t entry_point;
    bool has_entry;

    uint32_t flags;
} KROWriter;

static bool allocate_writer_buffers(KROWriter* writer) {
    writer->text_capacity = INITIAL_CAPACITY;
    writer->text = (uint8_t*)malloc(writer->text_capacity);
    if (!writer->text) return false;

    writer->data_capacity = INITIAL_CAPACITY;
    writer->data = (uint8_t*)malloc(writer->data_capacity);
    if (!writer->data) return false;

    writer->rodata_capacity = INITIAL_CAPACITY;
    writer->rodata = (uint8_t*)malloc(writer->rodata_capacity);
    if (!writer->rodata) return false;

    writer->sym_capacity = 64;
    writer->symbols = (KROSymbol*)calloc(writer->sym_capacity, sizeof(KROSymbol));
    if (!writer->symbols) return false;

    writer->text_relocs.capacity = 16;
    writer->text_relocs.relocs = (KRORelocation*)calloc(writer->text_relocs.capacity, sizeof(KRORelocation));
    if (!writer->text_relocs.relocs) return false;

    writer->data_relocs.capacity = 16;
    writer->data_relocs.relocs = (KRORelocation*)calloc(writer->data_relocs.capacity, sizeof(KRORelocation));
    if (!writer->data_relocs.relocs) return false;

    writer->rodata_relocs.capacity = 16;
    writer->rodata_relocs.relocs = (KRORelocation*)calloc(writer->rodata_relocs.capacity, sizeof(KRORelocation));
    if (!writer->rodata_relocs.relocs) return false;

    writer->bss_relocs.capacity = 16;
    writer->bss_relocs.relocs = (KRORelocation*)calloc(writer->bss_relocs.capacity, sizeof(KRORelocation));
    if (!writer->bss_relocs.relocs) return false;

    writer->string_capacity = STRING_INITIAL_CAPACITY;
    writer->strings = (char*)malloc(writer->string_capacity);
    if (!writer->strings) return false;
    writer->strings[0] = '\0';
    writer->string_size = 1;

    return true;
}

KROWriter* kro_writer_create(void) {
    KROWriter* writer = (KROWriter*)calloc(1, sizeof(KROWriter));
    if (!writer) return NULL;

    if (!allocate_writer_buffers(writer)) {
        kro_writer_destroy(writer);
        return NULL;
    }

    writer->text_align = DEFAULT_CODE_ALIGN;
    writer->data_align = DEFAULT_DATA_ALIGN;
    writer->rodata_align = DEFAULT_RODATA_ALIGN;
    writer->bss_align = DEFAULT_BSS_ALIGN;

    return writer;
}

void kro_writer_destroy(KROWriter* writer) {
    if (!writer) return;

    free(writer->text);
    free(writer->data);
    free(writer->rodata);
    free(writer->symbols);
    free(writer->text_relocs.relocs);
    free(writer->data_relocs.relocs);
    free(writer->rodata_relocs.relocs);
    free(writer->bss_relocs.relocs);
    free(writer->strings);
    free(writer);
}

static bool ensure_capacity(uint8_t** buffer, uint32_t* capacity, uint32_t required) {
    if (required <= *capacity) return true;

    uint32_t new_capacity = *capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    uint8_t* new_buffer = (uint8_t*)realloc(*buffer, new_capacity);
    if (!new_buffer) return false;

    *buffer = new_buffer;
    *capacity = new_capacity;
    return true;
}

uint32_t kro_write_code(KROWriter* writer, const void* data, uint32_t size) {
    if (!writer || !data || size == 0) return writer ? writer->text_offset : 0;

    uint32_t offset = writer->text_offset;
    uint32_t new_size = offset + size;

    if (!ensure_capacity(&writer->text, &writer->text_capacity, new_size)) {
        return 0;
    }

    memcpy(writer->text + offset, data, size);
    writer->text_offset = new_size;
    return offset;
}

uint32_t kro_write_data(KROWriter* writer, const void* data, uint32_t size) {
    if (!writer || !data || size == 0) return writer ? writer->data_offset : 0;

    uint32_t offset = writer->data_offset;
    uint32_t new_size = offset + size;

    if (!ensure_capacity(&writer->data, &writer->data_capacity, new_size)) {
        return 0;
    }

    memcpy(writer->data + offset, data, size);
    writer->data_offset = new_size;
    return offset;
}

uint32_t kro_write_rodata(KROWriter* writer, const void* data, uint32_t size) {
    if (!writer || !data || size == 0) return writer ? writer->rodata_offset : 0;

    uint32_t offset = writer->rodata_offset;
    uint32_t new_size = offset + size;

    if (!ensure_capacity(&writer->rodata, &writer->rodata_capacity, new_size)) {
        return 0;
    }

    memcpy(writer->rodata + offset, data, size);
    writer->rodata_offset = new_size;
    return offset;
}

static uint32_t align_up(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t kro_write_code_aligned(KROWriter* writer, const void* data, uint32_t size, uint32_t align) {
    if (!writer || align == 0) return 0;

    uint32_t aligned_offset = align_up(writer->text_offset, align);
    uint32_t padding = aligned_offset - writer->text_offset;

    if (padding > 0) {
        static const uint8_t nops[16] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
                                          0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
        for (uint32_t i = 0; i < padding; i++) {
            kro_write_code(writer, &nops[i % 16], 1);
        }
    }

    return kro_write_code(writer, data, size);
}

uint32_t kro_write_data_aligned(KROWriter* writer, const void* data, uint32_t size, uint32_t align) {
    if (!writer || align == 0) return 0;

    uint32_t aligned_offset = align_up(writer->data_offset, align);
    uint32_t padding = aligned_offset - writer->data_offset;

    if (padding > 0) {
        uint8_t zeros[16] = {0};
        for (uint32_t i = 0; i < padding; i += 16) {
            uint32_t to_write = (padding - i < 16) ? (padding - i) : 16;
            kro_write_data(writer, zeros, to_write);
        }
    }

    return kro_write_data(writer, data, size);
}

static uint32_t add_string(KROWriter* writer, const char* str) {
    if (!writer || !str) return 0;

    uint32_t len = (uint32_t)strlen(str) + 1;
    uint32_t new_size = writer->string_size + len;

    if (new_size > writer->string_capacity) {
        uint32_t new_capacity = writer->string_capacity;
        while (new_capacity < new_size) {
            new_capacity *= 2;
        }

        char* new_strings = (char*)realloc(writer->strings, new_capacity);
        if (!new_strings) return 0;

        writer->strings = new_strings;
        writer->string_capacity = new_capacity;
    }

    uint32_t offset = writer->string_size;
    memcpy(writer->strings + offset, str, len);
    writer->string_size = new_size;

    return offset;
}

int kro_add_symbol(KROWriter* writer, const char* name, uint8_t type, uint8_t bind,
                  uint32_t sec_idx, uint64_t value) {
    if (!writer || !name) return -1;

    if (writer->sym_count >= writer->sym_capacity) {
        uint32_t new_capacity = writer->sym_capacity * 2;
        KROSymbol* new_symbols = (KROSymbol*)realloc(writer->symbols,
                                                    new_capacity * sizeof(KROSymbol));
        if (!new_symbols) return -1;

        writer->symbols = new_symbols;
        writer->sym_capacity = new_capacity;
    }

    uint32_t index = writer->sym_count++;
    KROSymbol* sym = &writer->symbols[index];

    memset(sym, 0, sizeof(KROSymbol));

    sym->name_offset = add_string(writer, name);
    sym->binding = bind;
    sym->section = sec_idx;
    sym->value = (uint32_t)value;
    sym->size = 0;
    sym->type = type;

    return (int)index;
}

int kro_add_undefined_symbol(KROWriter* writer, const char* name) {
    return kro_add_symbol(writer, name, KRO_SYM_NOTYPE, KRO_BIND_GLOBAL, 0, 0);
}

int kro_add_import_symbol(KROWriter* writer, const char* name, const char* module) {
    if (!writer || !name || !module) return -1;

    if (writer->sym_count >= writer->sym_capacity) {
        uint32_t new_capacity = writer->sym_capacity * 2;
        KROSymbol* new_symbols = (KROSymbol*)realloc(writer->symbols,
                                                    new_capacity * sizeof(KROSymbol));
        if (!new_symbols) return -1;

        writer->symbols = new_symbols;
        writer->sym_capacity = new_capacity;
    }

    uint32_t index = writer->sym_count++;
    KROSymbol* sym = &writer->symbols[index];

    memset(sym, 0, sizeof(KROSymbol));

    sym->name_offset = add_string(writer, name);
    sym->binding = KRO_BIND_GLOBAL;
    sym->section = 0;
    sym->value = 0;
    sym->flags = add_string(writer, module);

    return (int)index;
}

int kro_find_symbol(KROWriter* writer, const char* name) {
    if (!writer || !name) return -1;

    for (uint32_t i = 0; i < writer->sym_count; i++) {
        if (writer->symbols[i].name_offset < writer->string_size) {
            const char* sym_name = writer->strings + writer->symbols[i].name_offset;
            if (strcmp(sym_name, name) == 0) {
                return (int)i;
            }
        }
    }

    return -1;
}

void kro_update_symbol_value(KROWriter* writer, int sym_idx, uint64_t value) {
    if (!writer || sym_idx < 0 || sym_idx >= (int)writer->sym_count) return;
    writer->symbols[sym_idx].value = (uint32_t)value;
}

static bool add_reloc_to_list(RelocList* list, const KRORelocation* reloc) {
    if (list->count >= list->capacity) {
        uint32_t new_capacity = list->capacity * 2;
        KRORelocation* new_relocs = (KRORelocation*)realloc(list->relocs,
                                                            new_capacity * sizeof(KRORelocation));
        if (!new_relocs) return false;

        list->relocs = new_relocs;
        list->capacity = new_capacity;
    }

    list->relocs[list->count++] = *reloc;
    return true;
}

void kro_add_reloc(KROWriter* writer, uint32_t sec_idx, uint64_t offset,
                   uint32_t sym_idx, uint16_t type, int16_t addend) {
    if (!writer) return;


    KRORelocation reloc = {
        .offset    = (uint32_t)offset,
        .sym_idx   = sym_idx,
        .type      = type,
        .addend    = addend,
    };

    RelocList* list = NULL;
    switch (sec_idx) {
        case KRO_SEC_TEXT:   list = &writer->text_relocs; break;
        case KRO_SEC_DATA:   list = &writer->data_relocs; break;
        case KRO_SEC_RODATA: list = &writer->rodata_relocs; break;
        case KRO_SEC_BSS:    list = &writer->bss_relocs; break;
        default: return;
    }

    add_reloc_to_list(list, &reloc);
}

bool kro_set_entry_point(KROWriter* writer, uint64_t offset) {
    if (!writer) return false;
    writer->entry_point = offset;
    writer->has_entry = true;
    return true;
}

bool kro_reserve_bss(KROWriter* writer, uint32_t size, uint8_t align_log2) {
    if (!writer) return false;
    (void)align_log2;

    writer->bss_size += size;
    return true;
}

uint32_t kro_get_code_offset(KROWriter* writer) {
    return writer ? writer->text_offset : 0;
}

uint32_t kro_get_data_offset(KROWriter* writer) {
    return writer ? writer->data_offset : 0;
}

uint32_t kro_get_rodata_offset(KROWriter* writer) {
    return writer ? writer->rodata_offset : 0;
}

void kro_set_code_offset(KROWriter* writer, uint32_t offset) {
    if (writer) {
        writer->text_offset = offset;
    }
}

bool kro_write_file(KROWriter* writer, const char* filename) {
    if (!writer || !filename) return false;

    FILE* fp = fopen(filename, "wb");
    if (!fp) return false;
    uint32_t total_reloc_count = writer->text_relocs.count +
                                  writer->rodata_relocs.count +
                                  writer->data_relocs.count;
    KROHeader header;
    memset(&header, 0, sizeof(header));
    header.magic = KRO_MAGIC;
    header.version = KRO_VERSION;
    header.flags = writer->flags;
    header.entry_point = writer->has_entry ? (uint32_t)writer->entry_point : 0;
    header.text_size = writer->text_offset;
    header.rodata_size = writer->rodata_offset;
    header.data_size = writer->data_offset;
    header.bss_size = writer->bss_size;
    header.text_reloc_count = writer->text_relocs.count;
    header.rodata_reloc_count = writer->rodata_relocs.count;
    header.data_reloc_count = writer->data_relocs.count;
    header.bss_align = 8;
    header.sym_count = writer->sym_count;
    header.strtab_size = writer->string_size;
    header.total_reloc_count = total_reloc_count;

    if (fwrite(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return false;
    }

    if (writer->text_offset > 0) {
        if (fwrite(writer->text, 1, writer->text_offset, fp) != writer->text_offset) {
            fclose(fp);
            return false;
        }
    }

    if (writer->rodata_offset > 0) {
        if (fwrite(writer->rodata, 1, writer->rodata_offset, fp) != writer->rodata_offset) {
            fclose(fp);
            return false;
        }
    }

    if (writer->data_offset > 0) {
        if (fwrite(writer->data, 1, writer->data_offset, fp) != writer->data_offset) {
            fclose(fp);
            return false;
        }
    }

    if (writer->sym_count > 0) {
        if (fwrite(writer->symbols, sizeof(KROSymbol), writer->sym_count, fp) != writer->sym_count) {
            fclose(fp);
            return false;
        }
    }

    if (writer->text_relocs.count > 0) {
        if (fwrite(writer->text_relocs.relocs, sizeof(KRORelocation),
                   writer->text_relocs.count, fp) != writer->text_relocs.count) {
            fclose(fp);
            return false;
        }
    }
    if (writer->rodata_relocs.count > 0) {
        if (fwrite(writer->rodata_relocs.relocs, sizeof(KRORelocation),
                   writer->rodata_relocs.count, fp) != writer->rodata_relocs.count) {
            fclose(fp);
            return false;
        }
    }
    if (writer->data_relocs.count > 0) {
        if (fwrite(writer->data_relocs.relocs, sizeof(KRORelocation),
                   writer->data_relocs.count, fp) != writer->data_relocs.count) {
            fclose(fp);
            return false;
        }
    }

    if (writer->string_size > 1) {
        if (fwrite(writer->strings, 1, writer->string_size, fp) != writer->string_size) {
            fclose(fp);
            return false;
        }
    }

    fclose(fp);
    return true;
}
