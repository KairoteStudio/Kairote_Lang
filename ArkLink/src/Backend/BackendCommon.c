#include "ArkLink/Backend.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t ark_backend_calc_pc_relative(uint64_t p_vaddr, ArkRelocFieldSize field_size) {
    switch (field_size) {
    case ARK_RELOC_FIELD_8:
        return p_vaddr + 1;
    case ARK_RELOC_FIELD_16:
        return p_vaddr + 2;
    case ARK_RELOC_FIELD_32:
        return p_vaddr + 4;
    case ARK_RELOC_FIELD_64:
        return p_vaddr + 8;
    default:
        return 0;
    }
}

uint64_t ark_backend_align_up(uint64_t value, uint64_t alignment) {
    /* alignment is a power-of-two hint; callers must pass a nonzero value. */
    if (alignment == 0) {
        fprintf(stderr, "ArkLink: align_up called with zero alignment\n");
        return value;
    }
    uint64_t remainder = ark_backend_is_power_of_2(alignment) ? value & (alignment - 1) : value % alignment;
    uint64_t padding = remainder ? alignment - remainder : 0;
    return value > UINT64_MAX - padding ? UINT64_MAX : value + padding;
}

int ark_backend_align_up_32(uint32_t value, uint32_t alignment, uint32_t* out) {
    uint64_t aligned;
    if (!out || alignment == 0) {
        return -1;
    }
    aligned = ark_backend_align_up(value, alignment);
    if (aligned > UINT32_MAX) {
        return -1;
    }
    *out = (uint32_t)aligned;
    return 0;
}

int ark_backend_should_use_dynamic_elf(const ArkBackendInput* input) {
    if (!input) {
        return 0;
    }

    switch (input->output_type) {
    case ARK_OUTPUT_SHARED_LIB:
        return 1;
    case ARK_OUTPUT_OBJECT:
        return 0;
    case ARK_OUTPUT_EXECUTABLE:
        break;
    }

    return (input->import_count > 0) ? 1 : 0;
}

int ark_backend_is_power_of_2(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

ArkBuffer* ark_buffer_create(size_t initial_capacity, uint32_t base_rva) {
    ArkBuffer* buf = (ArkBuffer*)calloc(1, sizeof(ArkBuffer));
    if (!buf) {
        return NULL;
    }

    if (initial_capacity == 0) {
        initial_capacity = 64;
    }

    buf->data = (uint8_t*)malloc(initial_capacity);
    if (!buf->data) {
        free(buf);
        return NULL;
    }

    buf->size = 0;
    buf->capacity = initial_capacity;
    buf->base_rva = base_rva;

    return buf;
}

void ark_buffer_destroy(ArkBuffer* buf) {
    if (!buf) {
        return;
    }
    free(buf->data);
    free(buf);
}

static int ark_buffer_ensure_capacity(ArkBuffer* buf, size_t needed) {
    if (!buf || !buf->data) {
        return 0;
    }

    if (needed <= buf->capacity) {
        return 1;
    }

    size_t new_cap = buf->capacity <= SIZE_MAX / 2 ? buf->capacity * 2 : needed;

    uint8_t* new_data = (uint8_t*)realloc(buf->data, new_cap);
    if (!new_data) {
        return 0;
    }

    buf->data = new_data;
    buf->capacity = new_cap;
    return 1;
}

size_t ark_buffer_append(ArkBuffer* buf, const void* data, size_t len) {
    if (!buf || len == 0 || len > SIZE_MAX - buf->size) {
        return (size_t)-1;
    }

    /* Appending an existing slice must survive realloc and overlapping ranges. */
    size_t source_offset = 0;
    int internal = data && (uintptr_t)data >= (uintptr_t)buf->data &&
                   (uintptr_t)data - (uintptr_t)buf->data < buf->capacity;
    if (internal) {
        source_offset = (uintptr_t)data - (uintptr_t)buf->data;
        if (len > buf->size - source_offset) {
            return (size_t)-1;
        }
    }

    if (!ark_buffer_ensure_capacity(buf, buf->size + len)) {
        return (size_t)-1;
    }

    size_t offset = buf->size;
    if (data) {
        if (internal) {
            data = buf->data + source_offset;
        }
        memmove(buf->data + offset, data, len);
    } else {
        memset(buf->data + offset, 0, len);
    }
    buf->size += len;

    return offset;
}

size_t ark_buffer_append_zero(ArkBuffer* buf, size_t len) {
    return ark_buffer_append(buf, NULL, len);
}

uint32_t ark_buffer_add_string(ArkBuffer* buf, const char* str) {
    if (!buf || !str) {
        return (uint32_t)-1;
    }

    size_t len = strlen(str) + 1;
    if (buf->size > UINT32_MAX || len > UINT32_MAX - buf->size) {
        return UINT32_MAX;
    }
    uint32_t offset = (uint32_t)buf->size;

    if (ark_buffer_append(buf, str, len) == (size_t)-1) {
        return (uint32_t)-1;
    }

    return offset;
}

char* ark_buffer_get_string(ArkBuffer* buf, uint32_t offset) {
    if (!buf || !buf->data || offset >= buf->size) {
        return NULL;
    }
    return (char*)(buf->data + offset);
}

uint32_t ark_buffer_get_rva(ArkBuffer* buf, size_t offset) {
    if (!buf) {
        return UINT32_MAX;
    }
    if (offset > UINT32_MAX - buf->base_rva) {
        return UINT32_MAX;
    }
    return buf->base_rva + (uint32_t)offset;
}

int ark_buffer_align(ArkBuffer* buf, size_t alignment) {
    if (!buf || alignment <= 1) {
        return 0;
    }

    size_t current = buf->size;
    size_t aligned = ark_backend_align_up(current, alignment);

    if (aligned == UINT64_MAX) {
        return -1;
    }
    if (aligned > current) {
        if (ark_buffer_append_zero(buf, aligned - current) == (size_t)-1) {
            return -1;
        }
    }
    return 0;
}

/* Permission-class rank used to keep same-class sections contiguous. */
static int layout_section_class(ArkSectionKind kind) {
    switch (kind) {
    case ARK_SECTION_CODE:
        return 0;
    case ARK_SECTION_RODATA:
        return 1;
    case ARK_SECTION_DATA:
        return 2;
    case ARK_SECTION_BSS:
        return 3;
    case ARK_SECTION_TDATA:
    case ARK_SECTION_TBSS:
        return 4;
    default:
        return 5;
    }
}

static ArkSegmentType classify_section_kind(ArkSectionKind kind) {
    switch (kind) {
    case ARK_SECTION_CODE:
        return ARK_SEGMENT_CODE;
    case ARK_SECTION_RODATA:
        return ARK_SEGMENT_RODATA;
    case ARK_SECTION_DATA:
        return ARK_SEGMENT_DATA;
    case ARK_SECTION_BSS:
        return ARK_SEGMENT_BSS;
    case ARK_SECTION_TDATA:
        return ARK_SEGMENT_TLS;
    case ARK_SECTION_TBSS:
        return ARK_SEGMENT_TLS;
    default:
        return ARK_SEGMENT_UNKNOWN;
    }
}

ArkImageLayout* ark_layout_create(const ArkBackendInput* input, uint32_t section_alignment, uint32_t file_alignment) {
    if (!input || (input->section_count && !input->sections)) {
        return NULL;
    }

    ArkImageLayout* layout = (ArkImageLayout*)calloc(1, sizeof(ArkImageLayout));
    if (!layout) {
        return NULL;
    }

    layout->section_count = input->section_count;
    layout->section_alignment = section_alignment ? section_alignment : 0x1000;
    layout->file_alignment = file_alignment ? file_alignment : 0x200;
    layout->image_base = input->image_base ? input->image_base : 0x400000;

    if (input->section_count == 0) {
        layout->sections = NULL;
        layout->headers_size = section_alignment;
        layout->file_size = section_alignment;
        layout->image_size = section_alignment;
        return layout;
    }

    layout->sections = (ArkSectionLayout*)calloc(input->section_count, sizeof(ArkSectionLayout));
    if (!layout->sections) {
        free(layout);
        return NULL;
    }

    layout->headers_size = section_alignment;
    uint64_t current_va = layout->image_base + layout->headers_size;
    uint64_t current_file_offset = layout->headers_size;

    /*
     * Assign addresses grouped by permission class (code, rodata, data, bss,
     * everything else).  Without this the sections of a multi-object image
     * interleave (text, data, text, rodata, ...) and every section ends up in
     * its own PT_LOAD; loaders with a small segment budget (proot refuses
     * >=17) then fail to map the image at all.
     */
    size_t* order = (size_t*)malloc(input->section_count * sizeof(size_t));
    if (!order) {
        free(layout->sections);
        free(layout);
        return NULL;
    }
    for (size_t i = 0; i < input->section_count; i++) {
        order[i] = i;
    }
    for (size_t i = 1; i < input->section_count; i++) {
        size_t key = order[i];
        size_t cls = (size_t)layout_section_class(input->sections[key].kind);
        size_t j = i;
        while (j > 0 && (size_t)layout_section_class(input->sections[order[j - 1]].kind) > cls) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }

    for (size_t oi = 0; oi < input->section_count; oi++) {
        size_t i = order[oi];
        ArkSectionLayout* sec = &layout->sections[i];
        const ArkSectionBuffer* input_sec = &input->sections[i];

        sec->section_index = (uint32_t)i;
        sec->segment_type = classify_section_kind(input_sec->kind);
        if (sec->segment_type == ARK_SEGMENT_UNKNOWN) {
            free(order);
            free(layout->sections);
            free(layout);
            return NULL;
        }
        sec->alignment = input_sec->alignment ? input_sec->alignment : section_alignment;
        current_va = ark_backend_align_up(current_va, sec->alignment);
        current_file_offset = ark_backend_align_up(current_file_offset, layout->file_alignment);
        sec->virtual_address = current_va;
        sec->file_offset = current_file_offset;
        sec->virtual_size = input_sec->size;
        int is_bss = (sec->segment_type == ARK_SEGMENT_BSS ||
                      (sec->segment_type == ARK_SEGMENT_TLS && input_sec->kind == ARK_SECTION_TBSS));
        sec->file_size = is_bss ? 0 : input_sec->size;
        sec->flags = 0;

        uint64_t aligned_size = ark_backend_align_up(input_sec->size, section_alignment);
        if (aligned_size == UINT64_MAX || current_va > UINT64_MAX - aligned_size) {
            free(order);
            free(layout->sections);
            free(layout);
            return NULL;
        }
        current_va += aligned_size;

        if (!is_bss) {
            uint64_t file_advance = ark_backend_align_up(input_sec->size, layout->file_alignment);
            if (file_advance == UINT64_MAX || current_file_offset > UINT64_MAX - file_advance) {
                free(order);
                free(layout->sections);
                free(layout);
                return NULL;
            }
            current_file_offset += file_advance;
        }
    }

    layout->code_segment_start = layout->image_base + layout->headers_size;
    layout->code_segment_end = layout->code_segment_start;
    layout->data_segment_start = current_va;
    layout->data_segment_end = layout->data_segment_start;
    free(order);

    for (size_t i = 0; i < input->section_count; i++) {
        const ArkSectionLayout* sec = &layout->sections[i];
        uint64_t aligned_vsize = ark_backend_align_up(sec->virtual_size, section_alignment);
        if (aligned_vsize == UINT64_MAX || sec->virtual_address > UINT64_MAX - aligned_vsize) {
            free(layout->sections);
            free(layout);
            return NULL;
        }
        uint64_t sec_end = sec->virtual_address + aligned_vsize;

        switch (sec->segment_type) {
        case ARK_SEGMENT_CODE:
        case ARK_SEGMENT_RODATA:
            if (sec_end > layout->code_segment_end) {
                layout->code_segment_end = sec_end;
            }
            break;

        case ARK_SEGMENT_DATA:
        case ARK_SEGMENT_BSS:
            if (sec->virtual_address < layout->data_segment_start) {
                layout->data_segment_start = sec->virtual_address;
            }
            if (sec_end > layout->data_segment_end) {
                layout->data_segment_end = sec_end;
            }
            break;

        case ARK_SEGMENT_TLS:
            if (sec->virtual_address < layout->data_segment_start) {
                layout->data_segment_start = sec->virtual_address;
            }
            if (sec_end > layout->data_segment_end) {
                layout->data_segment_end = sec_end;
            }
            break;

        default:
            break;
        }
    }

    if (layout->data_segment_start >= layout->data_segment_end) {
        layout->data_segment_start = layout->code_segment_end;
        layout->data_segment_end = layout->data_segment_start;
    }

    /*
     * image_size spans the loaded image (including BSS), while file_size is the
     * on-disk extent (excluding BSS).  They intentionally use different bases;
     * do not collapse them into one formula.
     */
    if (layout->data_segment_end < layout->image_base) {
        free(layout->sections);
        free(layout);
        return NULL;
    }
    layout->image_size = layout->data_segment_end - layout->image_base;
    layout->file_size = current_file_offset;

    return layout;
}

void ark_layout_destroy(ArkImageLayout* layout) {
    if (!layout) {
        return;
    }
    free(layout->sections);
    free(layout);
}

const ArkSectionLayout* ark_layout_get_section(const ArkImageLayout* layout, size_t index) {
    if (!layout || !layout->sections || index >= layout->section_count) {
        return NULL;
    }
    return &layout->sections[index];
}

uint64_t ark_layout_calc_total_size(const ArkImageLayout* layout, int include_bss) {
    if (!layout) {
        return 0;
    }
    return include_bss ? layout->image_size : layout->file_size;
}

static int ark_reloc_field_width(ArkRelocFieldSize field_size, size_t* width) {
    switch (field_size) {
    case ARK_RELOC_FIELD_8:
        *width = 1;
        return 0;
    case ARK_RELOC_FIELD_16:
        *width = 2;
        return 0;
    case ARK_RELOC_FIELD_32:
        *width = 4;
        return 0;
    case ARK_RELOC_FIELD_64:
        *width = 8;
        return 0;
    default:
        return -1;
    }
}

/* Bounds-checked field access; the offset+width sum is never formed, so it cannot wrap. */
static int ark_reloc_read_u32(const uint8_t* data, size_t size, uint32_t offset, uint32_t* out) {
    if (!data || !out || offset > size || 4 > size - offset) {
        return -1;
    }
    uint32_t val = 0;
    memcpy(&val, data + offset, 4);
    *out = val;
    return 0;
}

static int ark_reloc_write_field(uint8_t* data, size_t size, uint32_t offset, uint64_t value,
                                 ArkRelocFieldSize field_size) {
    size_t width = 0;
    if (ark_reloc_field_width(field_size, &width) != 0) {
        return -1;
    }
    if (offset > size || width > size - offset) {
        return -1;
    }

    switch (field_size) {
    case ARK_RELOC_FIELD_8:
        data[offset] = (uint8_t)(value & 0xFF);
        break;
    case ARK_RELOC_FIELD_16: {
        uint16_t val = (uint16_t)(value & 0xFFFF);
        memcpy(data + offset, &val, 2);
        break;
    }
    case ARK_RELOC_FIELD_32: {
        uint32_t val = (uint32_t)(value & 0xFFFFFFFF);
        memcpy(data + offset, &val, 4);
        break;
    }
    case ARK_RELOC_FIELD_64:
        memcpy(data + offset, &value, 8);
        break;
    }
    return 0;
}

int ark_reloc_apply_elf(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr) {
    size_t width = 0;
    if (!data || !proc || ark_reloc_field_width(proc->field_size, &width) != 0) {
        return -1;
    }

    uint64_t result = 0;

    switch (proc->action) {
    case ARK_RELOC_APPLY_ABSOLUTE:
    case ARK_RELOC_APPLY_SECREL:
        result = proc->symbol_value + (uint64_t)proc->addend;
        break;

    case ARK_RELOC_APPLY_RELATIVE:
    case ARK_RELOC_APPLY_GOT: {
        uint64_t pc_addr = ark_backend_calc_pc_relative(p_vaddr, proc->field_size);
        result = proc->symbol_value + (uint64_t)proc->addend - pc_addr;
        break;
    }

    case ARK_RELOC_APPLY_AARCH64_CALL26: {
        /* AArch64 branch PC is the instruction address itself. */
        int64_t delta = (int64_t)(proc->symbol_value + (uint64_t)proc->addend) - (int64_t)p_vaddr;
        uint32_t insn = 0;
        if (ark_reloc_read_u32(data, size, proc->offset, &insn) != 0) {
            return -1;
        }
        insn &= ~0x03FFFFFFu;
        insn |= (uint32_t)((delta >> 2) & 0x03FFFFFFu);
        return ark_reloc_write_field(data, size, proc->offset, insn, ARK_RELOC_FIELD_32);
    }

    case ARK_RELOC_APPLY_AARCH64_ADRP: {
        uint64_t target_page = (proc->symbol_value + (uint64_t)proc->addend) & ~0xFFFu;
        uint64_t pc_page = p_vaddr & ~0xFFFu;
        int64_t imm = ((int64_t)target_page - (int64_t)pc_page) >> 12;
        uint32_t insn = 0;
        if (ark_reloc_read_u32(data, size, proc->offset, &insn) != 0) {
            return -1;
        }
        uint32_t immlo = (uint32_t)(imm & 0x3u);
        uint32_t immhi = (uint32_t)((imm >> 2) & 0x7FFFFu);
        insn &= ~((0x7FFFFu << 5) | (0x3u << 29));
        insn |= (immhi << 5) | (immlo << 29);
        return ark_reloc_write_field(data, size, proc->offset, insn, ARK_RELOC_FIELD_32);
    }

    case ARK_RELOC_APPLY_AARCH64_LO12: {
        uint32_t lo12 = (uint32_t)((proc->symbol_value + (uint64_t)proc->addend) & 0xFFFu);
        uint32_t insn = 0;
        if (ark_reloc_read_u32(data, size, proc->offset, &insn) != 0) {
            return -1;
        }
        insn &= ~(0xFFFu << 10);
        insn |= lo12 << 10;
        return ark_reloc_write_field(data, size, proc->offset, insn, ARK_RELOC_FIELD_32);
    }

    default:
        return -1;
    }

    return ark_reloc_write_field(data, size, proc->offset, result, proc->field_size);
}

int ark_reloc_apply_pe_base(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr) {
    size_t width = 0;
    if (!data || !proc || ark_reloc_field_width(proc->field_size, &width) != 0) {
        return -1;
    }

    uint64_t result = 0;

    switch (proc->action) {
    case ARK_RELOC_APPLY_ABSOLUTE:
        result = proc->symbol_value + (uint64_t)proc->addend;
        break;

    case ARK_RELOC_APPLY_RELATIVE: {
        uint64_t pc_addr = ark_backend_calc_pc_relative(p_vaddr, proc->field_size);
        result = proc->symbol_value + (uint64_t)proc->addend - pc_addr;
        break;
    }

    default:
        return -1;
    }

    return ark_reloc_write_field(data, size, proc->offset, result, proc->field_size);
}

int ark_reloc_process_all(ArkResolverReloc* relocs, size_t count, ArkRelocApplyFn apply_fn,
                          ArkRelocShouldProcessFn filter_fn, void* user_data, uint8_t* section_data,
                          size_t section_size, const ArkImageLayout* layout) {
    if (!relocs || count == 0 || !apply_fn || !section_data) {
        return 0;
    }

    int failures = 0;

    for (size_t i = 0; i < count; i++) {
        ArkResolverReloc* reloc = &relocs[i];

        if (filter_fn && !filter_fn(reloc, user_data)) {
            continue;
        }

        ArkRelocProcessor proc = {0};
        proc.target_section = reloc->section_index;
        proc.offset = reloc->offset;
        proc.symbol_value = reloc->symbol_address_resolved ? reloc->symbol_address :
            (reloc->symbol_rva ? reloc->symbol_rva : (reloc->symbol ? reloc->symbol->value : 0));
        proc.addend = reloc->addend;

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
        case ARK_RELOC_GOTPC32:
            proc.action = ARK_RELOC_APPLY_RELATIVE;
            proc.field_size = ARK_RELOC_FIELD_32;
            proc.is_pc_relative = 1;
            break;

        case ARK_RELOC_SECREL32:
            proc.action = ARK_RELOC_APPLY_SECREL;
            proc.field_size = ARK_RELOC_FIELD_32;
            break;

        case ARK_RELOC_AARCH64_CALL26:
            proc.action = ARK_RELOC_APPLY_AARCH64_CALL26;
            proc.field_size = ARK_RELOC_FIELD_32;
            proc.is_pc_relative = 1;
            break;

        case ARK_RELOC_AARCH64_ADR_PREL_PG_HI21:
            proc.action = ARK_RELOC_APPLY_AARCH64_ADRP;
            proc.field_size = ARK_RELOC_FIELD_32;
            proc.is_pc_relative = 1;
            break;

        case ARK_RELOC_AARCH64_ADD_ABS_LO12_NC:
        case ARK_RELOC_AARCH64_LDR_ABS_LO12_NC:
            proc.action = ARK_RELOC_APPLY_AARCH64_LO12;
            proc.field_size = ARK_RELOC_FIELD_32;
            break;

        case ARK_RELOC_AARCH64_ABS32:
            proc.action = ARK_RELOC_APPLY_ABSOLUTE;
            proc.field_size = ARK_RELOC_FIELD_32;
            break;

        default:
            fprintf(stderr, "ArkLink: unsupported relocation type %d at section %u offset %u\n", (int)reloc->type,
                    reloc->section_index, reloc->offset);
            failures++;
            continue;
        }

        if (!layout) {
            fprintf(stderr, "ArkLink: no layout for relocation in section %u\n", reloc->section_index);
            failures++;
            continue;
        }

        const ArkSectionLayout* sec = ark_layout_get_section(layout, reloc->section_index);
        if (!sec) {
            fprintf(stderr, "ArkLink: relocation section index %u out of range\n", reloc->section_index);
            failures++;
            continue;
        }
        if (sec->virtual_address > UINT64_MAX - reloc->offset) {
            fprintf(stderr, "ArkLink: relocation virtual address overflow in section %u\n", reloc->section_index);
            failures++;
            continue;
        }
        uint64_t p_vaddr = sec->virtual_address + reloc->offset;

        if (apply_fn(section_data, section_size, &proc, p_vaddr) != 0) {
            fprintf(stderr, "ArkLink: failed to apply relocation type %d in section %u at offset %u\n", (int)reloc->type,
                    reloc->section_index, reloc->offset);
            failures++;
        }
    }

    return failures;
}

static int ark_import_module_equal(const char* a, const char* b) {
    return a && b && strcmp(a, b) == 0;
}

ArkImportGroup* ark_import_group_create(const ArkImportEntry* imports, size_t count) {
    if (!imports || count == 0) {
        return NULL;
    }

    ArkImportGroup* group = (ArkImportGroup*)calloc(1, sizeof(ArkImportGroup));
    if (!group) {
        return NULL;
    }
    const char** unique_modules = (const char**)calloc(count, sizeof(char*));
    if (!unique_modules) {
        free(group);
        return NULL;
    }
    size_t unique_count = 0;

    for (size_t i = 0; i < count; i++) {
        const char* mod = imports[i].module;
        if (!mod) {
            continue;
        }

        int found = 0;
        for (size_t j = 0; j < unique_count; j++) {
            if (strcmp(unique_modules[j], mod) == 0) {
                found = 1;
                break;
            }
        }

        if (!found) {
            unique_modules[unique_count++] = mod;
        }
    }

    if (unique_count == 0) {
        free(unique_modules);
        free(group);
        return NULL;
    }

    group->modules = (ArkModuleImports*)calloc(unique_count, sizeof(ArkModuleImports));
    if (!group->modules) {
        free(unique_modules);
        free(group);
        return NULL;
    }
    group->module_count = unique_count;

    for (size_t m = 0; m < unique_count; m++) {
        group->modules[m].module_name = unique_modules[m];

        size_t sym_count = 0;
        for (size_t i = 0; i < count; i++) {
            if (ark_import_module_equal(imports[i].module, unique_modules[m])) {
                sym_count++;
            }
        }

        group->modules[m].symbols = (const char**)calloc(sym_count, sizeof(char*));
        if (!group->modules[m].symbols) {
            for (size_t k = 0; k < m; k++) {
                free(group->modules[k].symbols);
            }
            free(group->modules);
            free(unique_modules);
            free(group);
            return NULL;
        }

        group->modules[m].symbol_count = 0;
        for (size_t i = 0; i < count; i++) {
            if (ark_import_module_equal(imports[i].module, unique_modules[m]) && imports[i].symbol) {
                group->modules[m].symbols[group->modules[m].symbol_count++] = imports[i].symbol;
            }
        }
    }

    free(unique_modules);
    return group;
}

void ark_import_group_destroy(ArkImportGroup* group) {
    if (!group) {
        return;
    }

    if (group->modules) {
        for (size_t i = 0; i < group->module_count; i++) {
            free(group->modules[i].symbols);
        }
        free(group->modules);
    }

    free(group);
}
