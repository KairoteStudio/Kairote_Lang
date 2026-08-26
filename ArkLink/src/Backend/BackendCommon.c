#include "ArkLink/Backend.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


uint64_t ark_backend_calc_pc_relative(uint64_t p_vaddr, ArkRelocFieldSize field_size) {
    switch (field_size) {
        case ARK_RELOC_FIELD_8:   return p_vaddr + 1;
        case ARK_RELOC_FIELD_16:  return p_vaddr + 2;
        case ARK_RELOC_FIELD_32:  return p_vaddr + 4;
        case ARK_RELOC_FIELD_64:  return p_vaddr + 8;
        default:                   return p_vaddr + 4;
    }
}

uint64_t ark_backend_align_up(uint64_t value, uint64_t alignment) {
    if (alignment == 0) return value;
    if (!ark_backend_is_power_of_2(alignment)) {
        return ((value + alignment - 1) / alignment) * alignment;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t ark_backend_align_up_32(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    if (!ark_backend_is_power_of_2(alignment)) {
        return ((value + alignment - 1) / alignment) * alignment;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

int ark_backend_should_use_dynamic_elf(const ArkBackendInput* input) {
    if (!input) return 0;

    switch (input->output_type) {
        case ARK_OUTPUT_SHARED_LIB:  return 1;
        case ARK_OUTPUT_OBJECT:      return 0;
        case ARK_OUTPUT_EXECUTABLE:  break;
    }

    return (input->import_count > 0) ? 1 : 0;
}

int ark_backend_is_power_of_2(uint64_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}


ArkBuffer* ark_buffer_create(size_t initial_capacity, uint32_t base_rva) {
    ArkBuffer* buf = (ArkBuffer*)calloc(1, sizeof(ArkBuffer));
    if (!buf) return NULL;

    if (initial_capacity == 0) initial_capacity = 64;

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
    if (!buf) return;
    free(buf->data);
    free(buf);
}

static int ark_buffer_ensure_capacity(ArkBuffer* buf, size_t needed) {
    if (!buf || !buf->data) return 0;
    
    if (needed <= buf->capacity) return 1;

    size_t new_cap = buf->capacity;
    while (new_cap < needed) {
        new_cap *= 2;
        if (new_cap < needed) new_cap = needed + needed / 2;
    }

    uint8_t* new_data = (uint8_t*)realloc(buf->data, new_cap);
    if (!new_data) return 0;

    buf->data = new_data;
    buf->capacity = new_cap;
    return 1;
}

size_t ark_buffer_append(ArkBuffer* buf, const void* data, size_t len) {
    if (!buf || len == 0) return (size_t)-1;
    
    if (!ark_buffer_ensure_capacity(buf, buf->size + len)) {
        return (size_t)-1;
    }

    size_t offset = buf->size;
    if (data) {
        memcpy(buf->data + offset, data, len);
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
    if (!buf || !str) return (uint32_t)-1;

    size_t len = strlen(str) + 1;
    uint32_t offset = (uint32_t)buf->size;
    
    if (ark_buffer_append(buf, str, len) == (size_t)-1) {
        return (uint32_t)-1;
    }
    
    return offset;
}

char* ark_buffer_get_string(ArkBuffer* buf, uint32_t offset) {
    if (!buf || !buf->data || offset >= buf->size) return NULL;
    return (char*)(buf->data + offset);
}

uint32_t ark_buffer_get_rva(ArkBuffer* buf, size_t offset) {
    if (!buf) return 0;
    return buf->base_rva + (uint32_t)offset;
}

void ark_buffer_align(ArkBuffer* buf, size_t alignment) {
    if (!buf || alignment <= 1) return;
    
    size_t current = buf->size;
    size_t aligned = ark_backend_align_up(current, alignment);
    
    if (aligned > current) {
        ark_buffer_append_zero(buf, aligned - current);
    }
}

static ArkSegmentType classify_section_kind(ArkSectionKind kind) {
    switch (kind) {
        case ARK_SECTION_CODE:   return ARK_SEGMENT_CODE;
        case ARK_SECTION_RODATA: return ARK_SEGMENT_RODATA;
        case ARK_SECTION_DATA:   return ARK_SEGMENT_DATA;
        case ARK_SECTION_BSS:    return ARK_SEGMENT_BSS;
        case ARK_SECTION_TDATA:  return ARK_SEGMENT_TLS;
        case ARK_SECTION_TBSS:   return ARK_SEGMENT_TLS;
        default:                 return ARK_SEGMENT_DATA;
    }
}

ArkImageLayout* ark_layout_create(const ArkBackendInput* input,
                                   uint32_t section_alignment,
                                   uint32_t file_alignment) {
    if (!input) return NULL;

    ArkImageLayout* layout = (ArkImageLayout*)calloc(1, sizeof(ArkImageLayout));
    if (!layout) return NULL;

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

    for (size_t i = 0; i < input->section_count; i++) {
        ArkSectionLayout* sec = &layout->sections[i];
        const ArkSectionBuffer* input_sec = &input->sections[i];

        sec->section_index = (uint32_t)i;
        sec->segment_type = classify_section_kind(input_sec->kind);
        sec->alignment = input_sec->alignment ? input_sec->alignment : section_alignment;
        current_va = ark_backend_align_up(current_va, sec->alignment);
        current_file_offset = ark_backend_align_up(current_file_offset, layout->file_alignment);
        sec->virtual_address = current_va;
        sec->file_offset = current_file_offset;
        sec->virtual_size = input_sec->size;
        int is_bss = (sec->segment_type == ARK_SEGMENT_BSS ||
                      (sec->segment_type == ARK_SEGMENT_TLS &&
                       input_sec->kind == ARK_SECTION_TBSS));
        sec->file_size = is_bss ? 0 : input_sec->size;
        sec->flags = 0;

        uint64_t aligned_size = ark_backend_align_up(input_sec->size, section_alignment);
        current_va += aligned_size;
        
        if (!is_bss) {
            current_file_offset += ark_backend_align_up(input_sec->size, layout->file_alignment);
        }
    }

    layout->code_segment_start = layout->image_base + layout->headers_size;
    layout->code_segment_end = layout->code_segment_start;
    layout->data_segment_start = current_va;
    layout->data_segment_end = layout->data_segment_start;

    for (size_t i = 0; i < input->section_count; i++) {
        const ArkSectionLayout* sec = &layout->sections[i];
        uint64_t sec_end = sec->virtual_address + ark_backend_align_up(sec->virtual_size, section_alignment);

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
        }
    }

    if (layout->data_segment_start >= layout->data_segment_end) {
        layout->data_segment_start = layout->code_segment_end;
        layout->data_segment_end = layout->data_segment_start;
    }

    layout->image_size = layout->data_segment_end - layout->image_base;
    layout->file_size = current_file_offset;

    return layout;
}

void ark_layout_destroy(ArkImageLayout* layout) {
    if (!layout) return;
    free(layout->sections);
    free(layout);
}

const ArkSectionLayout* ark_layout_get_section(const ArkImageLayout* layout, size_t index) {
    if (!layout || !layout->sections || index >= layout->section_count) return NULL;
    return &layout->sections[index];
}

uint64_t ark_layout_calc_total_size(const ArkImageLayout* layout, int include_bss) {
    if (!layout) return 0;
    return include_bss ? layout->image_size : layout->file_size;
}

void ark_reloc_apply_elf(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr) {
    if (!data || !proc || proc->offset >= size) return;

    uint64_t result = 0;

#ifdef ARK_DEBUG
#endif

    switch (proc->action) {
        case ARK_RELOC_APPLY_ABSOLUTE:
            result = proc->symbol_value + (uint64_t)proc->addend;
            break;

        case ARK_RELOC_APPLY_RELATIVE:
        case ARK_RELOC_APPLY_GOT: {
            uint64_t pc_addr = ark_backend_calc_pc_relative(p_vaddr, proc->field_size);
#ifdef ARK_DEBUG
#endif
            result = proc->symbol_value + (uint64_t)proc->addend - pc_addr;
            break;
        }
        
        case ARK_RELOC_APPLY_SECREL:
            result = proc->symbol_value + (uint64_t)proc->addend;
            break;
        
        default:
            return;
    }

    switch (proc->field_size) {
        case ARK_RELOC_FIELD_8:
            if (proc->offset < size) {
                data[proc->offset] = (uint8_t)(result & 0xFF);
            }
            break;
        
        case ARK_RELOC_FIELD_16:
            if (proc->offset + 2 <= size) {
                uint16_t val = (uint16_t)(result & 0xFFFF);
                memcpy(data + proc->offset, &val, 2);
            }
            break;
        
        case ARK_RELOC_FIELD_32:
            if (proc->offset + 4 <= size) {
                uint32_t val = (uint32_t)(result & 0xFFFFFFFF);
                memcpy(data + proc->offset, &val, 4);
            }
            break;
        
        case ARK_RELOC_FIELD_64:
            if (proc->offset + 8 <= size) {
                uint64_t val = result;
                memcpy(data + proc->offset, &val, 8);
            }
            break;
    }
}

void ark_reloc_apply_pe_base(uint8_t* data, size_t size, const ArkRelocProcessor* proc, uint64_t p_vaddr) {
    if (!data || !proc || !size) return;

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

        case ARK_RELOC_GENERATE_BASE_REL:
            return;

        default:
            return;
    }

    switch (proc->field_size) {
        case ARK_RELOC_FIELD_32:
            if (size >= 4) {
                uint32_t val = (uint32_t)(result & 0xFFFFFFFF);
                memcpy(data, &val, 4);
            }
            break;

        case ARK_RELOC_FIELD_64:
            if (size >= 8) {
                memcpy(data, &result, 8);
            }
            break;

        default:
            break;
    }
}

void ark_reloc_process_all(ArkResolverReloc* relocs, size_t count,
                           ArkRelocApplyFn apply_fn,
                           ArkRelocShouldProcessFn filter_fn,
                           void* user_data,
                           uint8_t* section_data,
                           size_t section_size,
                           const ArkImageLayout* layout) {
    if (!relocs || count == 0 || !apply_fn || !section_data) return;

    for (size_t i = 0; i < count; i++) {
        ArkResolverReloc* reloc = &relocs[i];

        if (filter_fn && !filter_fn(reloc, user_data)) {
            continue;
        }

        ArkRelocProcessor proc = {0};
        proc.target_section = reloc->section_index;
        proc.offset = reloc->offset;
        proc.symbol_value = reloc->symbol_rva ? reloc->symbol_rva : (reloc->symbol ? reloc->symbol->value : 0);
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
            
            default:
                continue;
        }

        uint64_t p_vaddr = 0;
        if (layout) {
            const ArkSectionLayout* sec = ark_layout_get_section(layout, reloc->section_index);
            if (sec) {
                p_vaddr = sec->virtual_address + reloc->offset;
            }
        }

        apply_fn(section_data, section_size, &proc, p_vaddr);
    }
}

ArkImportGroup* ark_import_group_create(const ArkImportEntry* imports, size_t count) {
    if (!imports || count == 0) return NULL;

    ArkImportGroup* group = (ArkImportGroup*)calloc(1, sizeof(ArkImportGroup));
    if (!group) return NULL;
    const char** unique_modules = (const char**)calloc(count, sizeof(char*));
    size_t unique_count = 0;
    
    for (size_t i = 0; i < count; i++) {
        const char* mod = imports[i].module;
        if (!mod) continue;
        
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
            if (strcmp(imports[i].module, unique_modules[m]) == 0) {
                sym_count++;
            }
        }

        group->modules[m].symbols = (const char**)calloc(sym_count, sizeof(char*));
        if (!group->modules[m].symbols) continue;
        
        group->modules[m].symbol_count = 0;
        for (size_t i = 0; i < count; i++) {
            if (strcmp(imports[i].module, unique_modules[m]) == 0 &&
                imports[i].symbol) {
                group->modules[m].symbols[group->modules[m].symbol_count++] = imports[i].symbol;
            }
        }
    }

    free(unique_modules);
    return group;
}

void ark_import_group_destroy(ArkImportGroup* group) {
    if (!group) return;

    if (group->modules) {
        for (size_t i = 0; i < group->module_count; i++) {
            free(group->modules[i].symbols);
        }
        free(group->modules);
    }
    
    free(group);
}