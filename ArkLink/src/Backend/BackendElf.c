#include "ArkLink/BackendElf.h"
#include "ArkLink/Loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma pack(push, 1)

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;

typedef struct {
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;

#pragma pack(pop)

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ELFOSABI_NONE 0
#define ET_EXEC 2
#define ET_DYN 3
#define EM_X86_64 0x3E
#define EM_AARCH64 0xB7
#define KRO_ARCH_ARM64 0xAA64u

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_RELRO 0x6474e552
#define PT_GNU_STACK 0x6474e551
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_DYNAMIC 6
#define SHT_NOBITS 8
#define SHT_DYNSYM 11
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHT_TLS 17

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_TLS 0x400

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1

#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_32 10
#define R_X86_64_GOTPC32 29
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_GLOB_DAT 6

#define R_AARCH64_ABS64 257
#define R_AARCH64_ABS32 258
#define R_AARCH64_ADR_PREL_PG_HI21 275
#define R_AARCH64_ADD_ABS_LO12_NC 277
#define R_AARCH64_LDR_ABS_LO12_NC 278
#define R_AARCH64_CALL26 283

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_RPATH 15
#define DT_SYMBOLIC 16
#define DT_REL 17
#define DT_RELSZ 18
#define DT_RELENT 19
#define DT_PLTREL 20
#define DT_DEBUG 21
#define DT_TEXTREL 22
#define DT_JMPREL 23
#define DT_BIND_NOW 24
#define DT_INIT_ARRAY 25
#define DT_INIT_ARRAYSZ 26
#define DT_FINI_ARRAY 27
#define DT_FINI_ARRAYSZ 28

#define ELF_ST_BIND(i) ((uint8_t)((i) >> 4))
#define ELF_ST_TYPE(i) ((uint8_t)((i) & 0xf))
#define ELF_ST_INFO(b, t) ((uint8_t)(((b) << 4) | ((t) & 0xf)))
#define ELF_R_SYM(i) ((uint32_t)((i) >> 32))
#define ELF_R_TYPE(i) ((uint32_t)((i) & 0xffffffff))
#define ELF_R_INFO(s, t) (((uint64_t)(s) << 32) | (uint64_t)(t))

typedef struct {
    ArkBuffer* buffer;
} ElfStrBuilder;

static int sb_init(ElfStrBuilder* sb) {
    sb->buffer = ark_buffer_create(64, 0);
    if (!sb->buffer) {
        return 0;
    }
    sb->buffer->data[0] = 0;
    sb->buffer->size = 1;
    return 1;
}

static void sb_free(ElfStrBuilder* sb) {
    if (sb && sb->buffer) {
        ark_buffer_destroy(sb->buffer);
        sb->buffer = NULL;
    }
}

static uint32_t sb_add(ElfStrBuilder* sb, const char* s) {
    if (!sb || !sb->buffer) {
        return (uint32_t)-1;
    }
    return ark_buffer_add_string(sb->buffer, s);
}

static int is_tls_kind(ArkSectionKind kind);

static const char* kind_to_canonical(ArkSectionKind kind) {
    switch (kind) {
    case ARK_SECTION_CODE:
        return ".text";
    case ARK_SECTION_DATA:
        return ".data";
    case ARK_SECTION_RODATA:
        return ".rodata";
    case ARK_SECTION_BSS:
        return ".bss";
    case ARK_SECTION_TDATA:
        return ".tdata";
    case ARK_SECTION_TBSS:
        return ".tbss";
    default:
        return ".data";
    }
}

static uint32_t kind_to_sh_type(ArkSectionKind kind) {
    /* All non-BSS sections are PROGBITS here; BSS is chosen by the caller. */
    (void)kind;
    return SHT_PROGBITS;
}

static uint64_t kind_to_sh_flags(ArkSectionKind kind, uint32_t input_flags) {
    uint64_t f = SHF_ALLOC;
    if (kind == ARK_SECTION_CODE) {
        f |= SHF_EXECINSTR;
    }
    if (kind == ARK_SECTION_BSS) {
        f |= SHF_WRITE;
    }
    if (kind == ARK_SECTION_DATA) {
        f |= SHF_WRITE;
    }
    if (is_tls_kind(kind)) {
        f |= SHF_WRITE | SHF_TLS;
    }
    if (input_flags & ARK_SECTION_EXEC) {
        f |= SHF_EXECINSTR;
    }
    if (input_flags & ARK_SECTION_WRITE) {
        f |= SHF_WRITE;
    }
    return f;
}

static int is_bss_kind(ArkSectionKind kind) {
    return kind == ARK_SECTION_BSS || kind == ARK_SECTION_TBSS;
}

static int is_tls_kind(ArkSectionKind kind) {
    return kind == ARK_SECTION_TDATA || kind == ARK_SECTION_TBSS;
}

static uint32_t get_unique_name(ElfStrBuilder* shstrtab, ArkSectionKind kind, size_t occurrence) {
    char buf[64];
    if (occurrence == 0) {
        return sb_add(shstrtab, kind_to_canonical(kind));
    }
    int n = snprintf(buf, sizeof(buf), "%s.%zu", kind_to_canonical(kind), occurrence);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return (uint32_t)-1;
    }
    return sb_add(shstrtab, buf);
}

static uint32_t get_rela_name(ElfStrBuilder* shstrtab, const char* target_name) {
    char buf[80];
    int n = snprintf(buf, sizeof(buf), ".rela%s", target_name);
    if (n < 0 || (size_t)n >= sizeof(buf)) {
        return (uint32_t)-1;
    }
    return sb_add(shstrtab, buf);
}

typedef struct {
    const char** names;
    uint32_t* indices;
    size_t count;
    size_t capacity;
} NameIndexMap;

static void nim_free(NameIndexMap* m) {
    free(m->names);
    free(m->indices);
    m->names = NULL;
    m->indices = NULL;
    m->count = m->capacity = 0;
}

static int nim_lookup(NameIndexMap* m, const char* name, uint32_t* out_idx) {
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->names[i], name) == 0) {
            if (out_idx) {
                *out_idx = m->indices[i];
            }
            return 1;
        }
    }
    return 0;
}

static int nim_add(NameIndexMap* m, const char* name, uint32_t idx) {
    if (nim_lookup(m, name, &idx)) {
        return 1;
    }
    if (m->count == m->capacity) {
        size_t new_cap = m->capacity ? m->capacity * 2 : 16;
        const char** nn = (const char**)realloc(m->names, new_cap * sizeof(const char*));
        if (!nn) {
            return 0;
        }
        m->names = nn;
        uint32_t* ni = (uint32_t*)realloc(m->indices, new_cap * sizeof(uint32_t));
        if (!ni) {
            return 0;
        }
        m->indices = ni;
        m->capacity = new_cap;
    }
    m->names[m->count] = name;
    m->indices[m->count] = idx;
    m->count++;
    return 1;
}

static int reloc_filter_by_section(const ArkResolverReloc* reloc, void* user_data) {
    uint32_t target_sec_idx = *(const uint32_t*)user_data;
    return reloc->section_index == target_sec_idx;
}

static int elf_safe_mul(size_t a, size_t b, size_t* out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return -1;
    }
    *out = a * b;
    return 0;
}

typedef struct {
    uint64_t vaddr;
    uint64_t offset;
    uint64_t size;
} ElfMetaSection;

/* Owns temporary linking resources until elf_publish transfers the image. */
typedef struct {
    ArkBackendInput* input;
    ArkImageLayout* layout;
    ElfMetaSection* meta_secs;
    Elf64_Rela* rela_dyn_data;
    Elf64_Rela* rela_plt_data;
    Elf64_Sym* symtab;
    Elf64_Sym* dynsym;
    Elf64_Dyn* dyntab;
    uint32_t* sec_name_off;
    size_t* relocs_per_sec;
    Elf64_Rela** rela_arrays;
    uint8_t* out_buf;
    Elf64_Shdr* shdrs;
    uint64_t page_size;
    uint64_t image_base;
    uint64_t dynstr_actual_size;
    uint64_t file_size;
    uint64_t shdr_offset;
    uint64_t rx_end;
    uint64_t rw_start;
    uint64_t tls_offset;
    uint64_t tls_vaddr;
    uint64_t tls_filesz;
    uint64_t tls_memsz;
    uint64_t tls_align;
    uint64_t seg1_filesz;
    uint64_t seg1_memsz;
    uint64_t seg2_offset;
    uint64_t seg2_vaddr;
    uint64_t rw_filesz;
    uint64_t rw_memsz;
    size_t ns;
    size_t rela_dyn_count;
    size_t rela_plt_count;
    size_t func_import_count;
    size_t rela_sections;
    size_t elf_shnum;
    size_t rela_shidx_base;
    size_t symtab_shidx;
    size_t strtab_shidx;
    size_t shstrtab_shidx;
    size_t interp_shidx;
    size_t dynsym_shidx;
    size_t dynstr_shidx;
    size_t dynamic_shidx;
    size_t hash_shidx;
    size_t init_array_shidx;
    size_t fini_array_shidx;
    size_t reladyn_shidx;
    size_t plt_shidx;
    size_t gotplt_shidx;
    size_t rela_idx;
    size_t next_sym_idx;
    size_t total_syms;
    size_t dyntab_count;
    size_t phdr_load_count;
    int has_dynamic;
    int has_tls;
    int n_static_loads;
    NameIndexMap sym_map;
    NameIndexMap sym_map2;
    NameIndexMap name_off_map;
    NameIndexMap module_off;
    NameIndexMap symbol_off;
    NameIndexMap module_emitted;
    NameIndexMap dynsym_idx_map;
    NameIndexMap plt_idx_map;
    ElfStrBuilder dynstr;
    ElfStrBuilder strtab;
    ElfStrBuilder shstrtab;
    Elf64_Phdr static_loads[48];
} ElfLinkState;

static uint64_t elf_section_vaddr(ElfLinkState* state, size_t index) {
    const ArkSectionLayout* section = ark_layout_get_section(state->layout, index);
    if (!section) {
        fprintf(stderr, "ArkLink: section %zu has no layout\n", index);
        return 0;
    }
    return section->virtual_address;
}

static uint64_t elf_section_offset(ElfLinkState* state, size_t index) {
    const ArkSectionLayout* section = ark_layout_get_section(state->layout, index);
    if (!section) {
        fprintf(stderr, "ArkLink: section %zu has no layout\n", index);
        return 0;
    }
    return section->file_offset;
}

static uint64_t elf_section_vsize(ElfLinkState* state, size_t index) {
    const ArkSectionLayout* section = ark_layout_get_section(state->layout, index);
    if (!section) {
        fprintf(stderr, "ArkLink: section %zu has no layout\n", index);
        return 0;
    }
    return section->virtual_size;
}
static uint64_t elf_meta_vaddr(ElfLinkState* state, size_t index) {
    return state->meta_secs ? state->meta_secs[index].vaddr : 0;
}

static uint64_t elf_meta_offset(ElfLinkState* state, size_t index) {
    return state->meta_secs ? state->meta_secs[index].offset : 0;
}

static uint64_t elf_meta_size(ElfLinkState* state, size_t index) {
    return state->meta_secs ? state->meta_secs[index].size : 0;
}

static void elf_free_relocations(ElfLinkState* state) {
    if (state->rela_arrays) {
        for (size_t i = 0; i < state->rela_sections; i++) {
            free(state->rela_arrays[i]);
        }
        free(state->rela_arrays);
        state->rela_arrays = NULL;
    }
}

static void elf_cleanup(ElfLinkState* state) {
    elf_free_relocations(state);
    free(state->rela_dyn_data);
    free(state->rela_plt_data);
    free(state->symtab);
    free(state->dynsym);
    free(state->dyntab);
    free(state->meta_secs);
    free(state->sec_name_off);
    free(state->relocs_per_sec);
    free(state->out_buf);
    free(state->shdrs);
    sb_free(&state->strtab);
    sb_free(&state->shstrtab);
    sb_free(&state->dynstr);
    nim_free(&state->sym_map);
    nim_free(&state->sym_map2);
    nim_free(&state->name_off_map);
    nim_free(&state->module_off);
    nim_free(&state->symbol_off);
    nim_free(&state->module_emitted);
    nim_free(&state->dynsym_idx_map);
    nim_free(&state->plt_idx_map);
    ark_layout_destroy(state->layout);
}

static int elf_intern_symbol(ElfLinkState* state, const char* name) {
    uint32_t index;
    if (nim_lookup(&state->sym_map, name, &index)) {
        return 1;
    }
    if (sb_add(&state->strtab, name) == UINT32_MAX || !nim_add(&state->sym_map, name, (uint32_t)state->next_sym_idx)) {
        return 0;
    }
    state->next_sym_idx++;
    return 1;
}

static ArkLinkResult elf_prepare(ElfLinkState* state) {
    /* ELF stores section indices and counts in 16 bits; reject oversized inputs
     * up front so every later count/size stays representable. */
    if (state->input->section_count > 65534 || state->input->import_count > (1u << 20) ||
        state->input->export_count > (1u << 20) || state->input->reloc_count > (1u << 24)) {
        return ARK_LINK_ERR_FORMAT;
    }

    state->page_size = 0x1000;
    state->image_base = state->input->image_base ? state->input->image_base : 0x400000;
    state->ns = state->input->section_count;

    state->layout = ark_layout_create(state->input, (uint32_t)state->page_size, (uint32_t)state->page_size);
    if (!state->layout) {
        return ARK_LINK_ERR_MEMORY;
    }
    state->image_base = state->layout->image_base;

    state->relocs_per_sec = (size_t*)calloc(state->ns ? state->ns : 1, sizeof(size_t));
    if (!state->relocs_per_sec) {
        return ARK_LINK_ERR_MEMORY;
    }
    for (size_t i = 0; i < state->input->reloc_count; i++) {
        uint32_t s = state->input->relocs[i].section_index;
        if (s < state->ns) {
            state->relocs_per_sec[s]++;
        }
    }
    state->rela_sections = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (state->relocs_per_sec[i] > 0) {
            state->rela_sections++;
        }
    }

    state->has_dynamic = (state->input->import_count > 0) ? 1 : 0;
    state->has_tls = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (is_tls_kind((ArkSectionKind)state->input->sections[i].kind)) {
            state->has_tls = 1;
            break;
        }
    }

    state->dynstr_actual_size = 1;
    if (state->has_dynamic) {
        size_t seen_capacity = 0;
        if (elf_safe_mul(state->input->import_count, 2, &seen_capacity) != 0 ||
            seen_capacity > SIZE_MAX - 2) {
            return ARK_LINK_ERR_FORMAT;
        }
        seen_capacity += 2;
        const char** seen_names = (const char**)calloc(seen_capacity, sizeof(const char*));
        if (!seen_names) {
            return ARK_LINK_ERR_MEMORY;
        }
        size_t seen_count = 0;
        for (size_t i = 0; i < state->input->import_count; i++) {
            const char* sym = state->input->imports[i].symbol;
            const char* mod = state->input->imports[i].module;
            int sym_seen = 0, mod_seen = 0;
            for (size_t k = 0; k < seen_count; k++) {
                if (sym && !sym_seen && seen_names[k] && strcmp(seen_names[k], sym) == 0) {
                    sym_seen = 1;
                }
                if (mod && !mod_seen && seen_names[k] && strcmp(seen_names[k], mod) == 0) {
                    mod_seen = 1;
                }
            }
            if (sym && !sym_seen) {
                size_t len = strlen(sym);
                if ((uint64_t)len + 1 > UINT64_MAX - state->dynstr_actual_size) {
                    free(seen_names);
                    return ARK_LINK_ERR_FORMAT;
                }
                state->dynstr_actual_size += (uint64_t)len + 1;
                seen_names[seen_count++] = sym;
            }
            if (mod && !mod_seen) {
                size_t len = strlen(mod);
                if ((uint64_t)len + 1 > UINT64_MAX - state->dynstr_actual_size) {
                    free(seen_names);
                    return ARK_LINK_ERR_FORMAT;
                }
                state->dynstr_actual_size += (uint64_t)len + 1;
                seen_names[seen_count++] = mod;
            }
        }
        free(seen_names);
    }
    const size_t dyn_extra = (size_t)(state->has_dynamic ? 10 : 0);
    state->elf_shnum = 1 + state->ns + state->rela_sections + 3 + dyn_extra;
    if (state->elf_shnum > 65535) {
        fprintf(stderr, "ArkLink: ELF section count %zu exceeds 16 bits\n", state->elf_shnum);
        return ARK_LINK_ERR_FORMAT;
    }
    state->rela_shidx_base = 1 + state->ns;
    state->symtab_shidx = state->rela_shidx_base + state->rela_sections;
    state->strtab_shidx = state->symtab_shidx + 1;
    state->shstrtab_shidx = state->strtab_shidx + 1;
    state->interp_shidx = state->has_dynamic ? (state->shstrtab_shidx + 1) : 0;
    state->dynsym_shidx = state->has_dynamic ? (state->shstrtab_shidx + 2) : 0;
    state->dynstr_shidx = state->has_dynamic ? (state->shstrtab_shidx + 3) : 0;
    state->dynamic_shidx = state->has_dynamic ? (state->shstrtab_shidx + 4) : 0;
    state->hash_shidx = state->has_dynamic ? (state->shstrtab_shidx + 5) : 0;
    state->init_array_shidx = state->has_dynamic ? (state->shstrtab_shidx + 6) : 0;
    state->fini_array_shidx = state->has_dynamic ? (state->shstrtab_shidx + 7) : 0;
    state->reladyn_shidx = state->has_dynamic ? (state->shstrtab_shidx + 8) : 0;
    state->plt_shidx = state->has_dynamic ? (state->shstrtab_shidx + 9) : 0;
    state->gotplt_shidx = state->has_dynamic ? (state->shstrtab_shidx + 10) : 0;

    if (!sb_init(&state->shstrtab)) {
        return ARK_LINK_ERR_MEMORY;
    }
    state->sec_name_off = (uint32_t*)calloc(state->elf_shnum, sizeof(uint32_t));
    if (!state->sec_name_off) {
        return ARK_LINK_ERR_MEMORY;
    }
    state->sec_name_off[0] = 0;
    size_t occ[6] = {0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < state->ns; i++) {
        ArkSectionKind k = (ArkSectionKind)state->input->sections[i].kind;
        size_t kidx = (k == ARK_SECTION_CODE)     ? 0
                      : (k == ARK_SECTION_DATA)   ? 1
                      : (k == ARK_SECTION_RODATA) ? 2
                      : (k == ARK_SECTION_BSS)    ? 3
                      : (k == ARK_SECTION_TDATA)  ? 4
                                                  : 5;
        state->sec_name_off[1 + i] = get_unique_name(&state->shstrtab, k, occ[kidx]++);
    }
    state->rela_idx = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (state->relocs_per_sec[i] == 0) {
            continue;
        }
        const char* target_name = (const char*)state->shstrtab.buffer->data + state->sec_name_off[1 + i];
        state->sec_name_off[state->rela_shidx_base + state->rela_idx] = get_rela_name(&state->shstrtab, target_name);
        state->rela_idx++;
    }
    state->sec_name_off[state->symtab_shidx] = sb_add(&state->shstrtab, ".symtab");
    state->sec_name_off[state->strtab_shidx] = sb_add(&state->shstrtab, ".strtab");
    state->sec_name_off[state->shstrtab_shidx] = sb_add(&state->shstrtab, ".shstrtab");
    if (state->has_dynamic) {
        state->sec_name_off[state->interp_shidx] = sb_add(&state->shstrtab, ".interp");
        state->sec_name_off[state->dynsym_shidx] = sb_add(&state->shstrtab, ".dynsym");
        state->sec_name_off[state->dynstr_shidx] = sb_add(&state->shstrtab, ".dynstr");
        state->sec_name_off[state->dynamic_shidx] = sb_add(&state->shstrtab, ".dynamic");
        state->sec_name_off[state->hash_shidx] = sb_add(&state->shstrtab, ".hash");
        state->sec_name_off[state->init_array_shidx] = sb_add(&state->shstrtab, ".init_array");
        state->sec_name_off[state->fini_array_shidx] = sb_add(&state->shstrtab, ".fini_array");
        state->sec_name_off[state->reladyn_shidx] = sb_add(&state->shstrtab, ".rela.dyn");
        state->sec_name_off[state->plt_shidx] = sb_add(&state->shstrtab, ".plt");
        state->sec_name_off[state->gotplt_shidx] = sb_add(&state->shstrtab, ".got.plt");
    }
    for (size_t i = 1; i < state->elf_shnum; i++) {
        if (state->sec_name_off[i] == (uint32_t)-1) {
            fprintf(stderr, "ArkLink: failed to append section name %zu\n", i);
            return ARK_LINK_ERR_MEMORY;
        }
    }

    if (!sb_init(&state->strtab)) {
        return ARK_LINK_ERR_MEMORY;
    }
    if (sb_add(&state->strtab, "") == UINT32_MAX) {
        return ARK_LINK_ERR_MEMORY;
    }

    state->next_sym_idx = 1 + state->ns;

    for (size_t i = 0; i < state->input->export_count; i++) {
        const char* n = state->input->exports[i].name;
        if (!n) {
            continue;
        }
        if (state->input->exports[i].section_index < state->ns &&
            state->input->exports[i].offset != state->input->exports[i].value) {
            fprintf(stderr, "ArkLink: export '%s' has inconsistent offset/value\n", n);
            return ARK_LINK_ERR_FORMAT;
        }
        if (!elf_intern_symbol(state, n)) {
            return ARK_LINK_ERR_MEMORY;
        }
    }
    for (size_t i = 0; i < state->input->import_count; i++) {
        const char* sym = state->input->imports[i].symbol;
        if (!sym) {
            continue;
        }
        if (!elf_intern_symbol(state, sym)) {
            return ARK_LINK_ERR_MEMORY;
        }
    }
    for (size_t i = 0; i < state->input->reloc_count; i++) {
        const ArkResolverSymbol* s = state->input->relocs[i].symbol;
        if (!s || !s->name) {
            continue;
        }
        if (!elf_intern_symbol(state, s->name)) {
            return ARK_LINK_ERR_MEMORY;
        }
    }

    state->total_syms = state->next_sym_idx;
    if (state->total_syms > (1u << 25)) {
        fprintf(stderr, "ArkLink: symbol count %zu too large\n", state->total_syms);
        return ARK_LINK_ERR_FORMAT;
    }
    nim_free(&state->sym_map);

    return ARK_LINK_OK;
}

static ArkLinkResult elf_plan_layout(ElfLinkState* state) {
    for (size_t i = 0; i < state->input->reloc_count; i++) {
        ArkResolverReloc* reloc = &state->input->relocs[i];
        if (reloc->symbol && reloc->symbol->section_index < state->ns) {
            uint64_t sec_vaddr =
                ark_layout_get_section(state->layout, reloc->symbol->section_index)
                    ? ark_layout_get_section(state->layout, reloc->symbol->section_index)->virtual_address
                    : 0;
            if (reloc->symbol->value > UINT64_MAX - sec_vaddr) {
                return ARK_LINK_ERR_FORMAT;
            }
            reloc->symbol_address = sec_vaddr + reloc->symbol->value;
            reloc->symbol_address_resolved = 1;
        }
    }

    for (size_t i = 0; i < state->ns; i++) {
        ArkSectionBuffer* target = &state->input->sections[i];
        if (!target->data) {
            if (target->size > 0) {
                fprintf(stderr, "ArkLink: section %zu has size %zu but no data\n", i, target->size);
                return ARK_LINK_ERR_FORMAT;
            }
            continue;
        }
        if (is_bss_kind((ArkSectionKind)target->kind)) {
            continue;
        }

        uint32_t target_sec_idx = (uint32_t)i;
        int reloc_failures = ark_reloc_process_all(state->input->relocs, state->input->reloc_count, ark_reloc_apply_elf,
                                                   reloc_filter_by_section, &target_sec_idx, target->data,
                                                   target->size, state->layout);
        if (reloc_failures > 0) {
            return ARK_LINK_ERR_FORMAT;
        }
    }

    if (state->elf_shnum > 0) {
        state->meta_secs = (ElfMetaSection*)calloc(state->elf_shnum, sizeof(ElfMetaSection));
        if (!state->meta_secs) {
            return ARK_LINK_ERR_MEMORY;
        }
    }

    state->file_size = state->layout->file_size;

    uint64_t cur_offset = state->file_size;
    state->rx_end = cur_offset;

    if (state->has_dynamic) {
        static const char interp_path[] = "/lib64/ld-linux-x86-64.so.2";
        size_t interp_size = sizeof(interp_path);
        state->meta_secs[state->interp_shidx].offset = cur_offset;
        state->meta_secs[state->interp_shidx].vaddr = state->image_base + cur_offset;
        state->meta_secs[state->interp_shidx].size = (uint64_t)interp_size;
        cur_offset += interp_size;
    }

    state->rela_idx = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (state->relocs_per_sec[i] == 0) {
            continue;
        }
        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        state->meta_secs[state->rela_shidx_base + state->rela_idx].offset = cur_offset;
        state->meta_secs[state->rela_shidx_base + state->rela_idx].vaddr =
            state->has_dynamic ? (state->image_base + cur_offset) : 0;
        state->meta_secs[state->rela_shidx_base + state->rela_idx].size =
            (uint64_t)(state->relocs_per_sec[i] * sizeof(Elf64_Rela));
        cur_offset += state->meta_secs[state->rela_shidx_base + state->rela_idx].size;
        state->rela_idx++;
    }

    cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
    state->meta_secs[state->symtab_shidx].offset = cur_offset;
    state->meta_secs[state->symtab_shidx].vaddr = 0;
    state->meta_secs[state->symtab_shidx].size = 0;
    cur_offset += (uint64_t)(state->total_syms * sizeof(Elf64_Sym));

    cur_offset = ark_backend_align_up(cur_offset, 1);
    state->meta_secs[state->strtab_shidx].offset = cur_offset;
    state->meta_secs[state->strtab_shidx].vaddr = 0;
    state->meta_secs[state->strtab_shidx].size = 0;
    cur_offset += state->strtab.buffer->size;

    cur_offset = ark_backend_align_up(cur_offset, 1);
    state->meta_secs[state->shstrtab_shidx].offset = cur_offset;
    state->meta_secs[state->shstrtab_shidx].vaddr = 0;
    state->meta_secs[state->shstrtab_shidx].size = state->shstrtab.buffer->size;
    cur_offset += state->shstrtab.buffer->size;

    if (state->has_dynamic) {
        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        state->meta_secs[state->dynsym_shidx].offset = cur_offset;
        state->meta_secs[state->dynsym_shidx].vaddr = state->image_base + cur_offset;
        state->meta_secs[state->dynsym_shidx].size = 0;
        cur_offset += (uint64_t)((1 + state->input->import_count) * sizeof(Elf64_Sym));

        cur_offset = ark_backend_align_up(cur_offset, 1);
        state->meta_secs[state->dynstr_shidx].offset = cur_offset;
        state->meta_secs[state->dynstr_shidx].vaddr = state->image_base + cur_offset;
        state->meta_secs[state->dynstr_shidx].size = state->dynstr_actual_size;
        cur_offset += state->dynstr_actual_size;

        {
            size_t hash_nbuckets = 1;
            size_t hash_export_func_count = 0;
            for (size_t i = 0; i < state->input->export_count; i++) {
                if (state->input->exports[i].is_function) {
                    hash_export_func_count++;
                }
            }
            size_t hash_nchain = 1 + state->input->import_count + hash_export_func_count;
            size_t hash_size = (2 + hash_nbuckets + hash_nchain) * sizeof(uint32_t);
            cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
            state->meta_secs[state->hash_shidx].offset = cur_offset;
            state->meta_secs[state->hash_shidx].vaddr = state->image_base + cur_offset;
            state->meta_secs[state->hash_shidx].size = (uint64_t)hash_size;
            cur_offset += hash_size;
        }

        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        state->meta_secs[state->reladyn_shidx].offset = cur_offset;
        state->meta_secs[state->reladyn_shidx].vaddr = state->image_base + cur_offset;
        state->meta_secs[state->reladyn_shidx].size = (uint64_t)(state->input->import_count * sizeof(Elf64_Rela));
        cur_offset += state->meta_secs[state->reladyn_shidx].size;

        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        state->meta_secs[state->init_array_shidx].offset = cur_offset;
        state->meta_secs[state->init_array_shidx].vaddr = state->image_base + cur_offset;
        state->meta_secs[state->init_array_shidx].size = 0;
        state->meta_secs[state->fini_array_shidx].offset = cur_offset;
        state->meta_secs[state->fini_array_shidx].vaddr = state->image_base + cur_offset;
        state->meta_secs[state->fini_array_shidx].size = 0;

        state->func_import_count = 0;
        for (size_t i = 0; i < state->input->import_count; i++) {

            if (state->input->imports[i].is_function) {
                state->func_import_count++;
            }
        }

        if (state->func_import_count > 0) {
            size_t plt_entry_size = 16;
            size_t plt_size = (1 + state->func_import_count) * plt_entry_size;

            cur_offset = ark_backend_align_up(cur_offset, 16);
            state->meta_secs[state->plt_shidx].offset = cur_offset;
            state->meta_secs[state->plt_shidx].vaddr = state->image_base + cur_offset;
            state->meta_secs[state->plt_shidx].size = plt_size;
            cur_offset += plt_size;

            size_t gotplt_entries = 3 + state->func_import_count;
            size_t gotplt_size = gotplt_entries * sizeof(uint64_t);

            cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
            state->meta_secs[state->gotplt_shidx].offset = cur_offset;
            state->meta_secs[state->gotplt_shidx].vaddr = state->image_base + cur_offset;
            state->meta_secs[state->gotplt_shidx].size = gotplt_size;
            cur_offset += gotplt_size;

        } else {
            state->meta_secs[state->plt_shidx].offset = 0;
            state->meta_secs[state->plt_shidx].vaddr = 0;
            state->meta_secs[state->plt_shidx].size = 0;
            state->meta_secs[state->gotplt_shidx].offset = 0;
            state->meta_secs[state->gotplt_shidx].vaddr = 0;
            state->meta_secs[state->gotplt_shidx].size = 0;
        }
    }

    cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
    state->shdr_offset = cur_offset;
    uint64_t shdr_size = (uint64_t)(state->elf_shnum * sizeof(Elf64_Shdr));
    cur_offset += shdr_size;

    state->rx_end = ark_backend_align_up(cur_offset, state->page_size);

    state->rw_start = state->rx_end;
    uint64_t rw_cur = state->rx_end;
    state->tls_offset = 0;
    state->tls_vaddr = 0;
    state->tls_filesz = 0;
    state->tls_memsz = 0;
    state->tls_align = 1;

    if (state->has_dynamic) {
        rw_cur = ark_backend_align_up(rw_cur, sizeof(uint64_t));
        state->meta_secs[state->dynamic_shidx].offset = rw_cur;
        state->meta_secs[state->dynamic_shidx].vaddr = state->image_base + rw_cur;
        /* 14 base slots: STRTAB/SYMTAB/STRSZ/SYMENT/HASH + INIT_ARRAY/SZ +
         * FINI_ARRAY/SZ + 4 PLT tags (JMPREL/PLTRELSZ/PLTGOT/PLTREL). */
        size_t dynamic_upper = 14 + state->input->import_count + (state->rela_sections > 0 ? 3 : 0);
        state->meta_secs[state->dynamic_shidx].size = (uint64_t)(dynamic_upper * sizeof(Elf64_Dyn));
        rw_cur += state->meta_secs[state->dynamic_shidx].size;
    }

    state->file_size = cur_offset;
    if (rw_cur > state->file_size) {
        state->file_size = rw_cur;
    }
    if (state->layout->file_size > state->file_size) {
        state->file_size = state->layout->file_size;
    }

    uint64_t code_file_end = 0;
    if (state->has_dynamic && state->interp_shidx > 0) {
        code_file_end = state->meta_secs[state->interp_shidx].offset;
    } else {
        for (size_t i = 0; i < state->ns; i++) {
            if ((ArkSectionKind)state->input->sections[i].kind == ARK_SECTION_CODE) {
                code_file_end = state->meta_secs[i + 1].offset + state->meta_secs[i + 1].size;
                break;
            }
        }
        if (code_file_end == 0) {
            code_file_end = cur_offset;
        }
    }

    state->seg1_filesz = 0;
    state->seg1_memsz = 0;
    state->seg2_offset = 0;
    state->seg2_vaddr = 0;
    state->rw_filesz = 0;
    state->rw_memsz = 0;
    state->n_static_loads = 0;
    state->phdr_load_count = 0;
    if (!state->has_dynamic && !state->has_tls) {
        uint32_t cur_flags = 0;
        /*
         * Sections are laid out grouped by permission class, so their virtual
         * addresses are no longer in input order.  Walk them by address:
         * building the PT_LOADs in input order makes them overlap, and the
         * later mapping silently wins (which is how .rodata used to be
         *clobbered by a text segment).
         */
        size_t* sec_order = (size_t*)malloc(state->ns * sizeof(size_t));
        if (!sec_order) {
            return ARK_LINK_ERR_MEMORY;
        }
        for (size_t i = 0; i < state->ns; i++) {
            sec_order[i] = i;
        }
        for (size_t i = 1; i < state->ns; i++) {
            size_t key = sec_order[i];
            uint64_t key_va = ark_layout_get_section(state->layout, key)->virtual_address;
            size_t j = i;
            while (j > 0 &&
                   ark_layout_get_section(state->layout, sec_order[j - 1])->virtual_address > key_va) {
                sec_order[j] = sec_order[j - 1];
                j--;
            }
            sec_order[j] = key;
        }
        for (size_t oi = 0; oi < state->ns; oi++) {
            size_t i = sec_order[oi];
            ArkSectionKind k = (ArkSectionKind)state->input->sections[i].kind;
            if (k != ARK_SECTION_CODE && k != ARK_SECTION_DATA && k != ARK_SECTION_RODATA && k != ARK_SECTION_BSS &&
                k != ARK_SECTION_TDATA) {
                fprintf(stderr, "ArkLink: unsupported section kind %d\n", (int)k);
                free(sec_order);
                return ARK_LINK_ERR_FORMAT;
            }
            const ArkSectionLayout* lsec = ark_layout_get_section(state->layout, i);
            uint64_t va = elf_section_vaddr(state, i);
            uint64_t vsz = elf_section_vsize(state, i);
            uint64_t off = lsec ? lsec->file_offset : state->meta_secs[i + 1].offset;
            uint32_t f = (k == ARK_SECTION_CODE) ? (PF_R | PF_X) : (k == ARK_SECTION_RODATA) ? PF_R : (PF_R | PF_W);
            uint64_t filesz_add = (k == ARK_SECTION_BSS) ? 0 : vsz;

            /*
             * Merge neighbouring sections into one PT_LOAD.  Two rules:
             *  - the combined flags must not become RWX (keep the image as
             *    safe as the section classes allow);
             *  - page-alignment gaps between sections are spanned, which is
             *    what keeps the segment count low enough for loaders with a
             *    small PT_LOAD budget (proot refuses >=17).
             * `va - off` is constant across sections (both advance by the same
             * aligned size), so spanning a gap keeps p_vaddr == p_offset
             * (mod p_align) intact.
             */
            uint32_t merged_flags = cur_flags | f;
            int runnable_and_writable = (merged_flags & PF_X) && (merged_flags & PF_W);
            if (state->n_static_loads > 0 && !runnable_and_writable) {
                Elf64_Phdr* c = &state->static_loads[state->n_static_loads - 1];
                uint64_t fend = off + filesz_add;
                uint64_t vend = va + vsz;
                if (fend > (uint64_t)(c->p_offset + c->p_filesz)) {
                    c->p_filesz = fend - c->p_offset;
                }
                if (vend > (uint64_t)(c->p_vaddr + c->p_memsz)) {
                    c->p_memsz = vend - c->p_vaddr;
                }
                cur_flags = merged_flags;
                continue;
            }
            if (state->n_static_loads >= 48) {
                fprintf(stderr, "ArkLink: static PT_LOAD count exceeds 48\n");
                free(sec_order);
                return ARK_LINK_ERR_FORMAT;
            }
            Elf64_Phdr* seg = &state->static_loads[state->n_static_loads++];
            memset(seg, 0, sizeof(*seg));
            seg->p_type = PT_LOAD;
            seg->p_flags = f;
            seg->p_offset = off;
            seg->p_vaddr = va;
            seg->p_paddr = va;
            seg->p_filesz = filesz_add;
            seg->p_memsz = vsz;
            seg->p_align = state->page_size;
            cur_flags = f;
        }
        free(sec_order);
        if (state->n_static_loads > 0) {
            state->seg1_filesz = state->static_loads[0].p_filesz;
            state->seg1_memsz = state->static_loads[0].p_memsz;
        } else {
            state->seg1_filesz = ark_backend_align_up(code_file_end, state->page_size);
            state->seg1_memsz = state->seg1_filesz;
        }
    } else {
        state->seg1_filesz = ark_backend_align_up(code_file_end, state->page_size);
        state->seg1_memsz = state->seg1_filesz;

        uint64_t data_start = state->has_dynamic ? state->meta_secs[state->interp_shidx].offset : state->rw_start;
        state->seg2_offset = data_start;
        state->seg2_vaddr = state->image_base + data_start;

        uint64_t data_end = state->file_size;
        if (data_end < data_start) {
            data_end = data_start;
        }
        state->rw_filesz = data_end - data_start;
        state->rw_memsz = state->rw_filesz;
    }

    return ARK_LINK_OK;
}

static ArkLinkResult elf_build_symbols(ElfLinkState* state) {
    state->symtab = (Elf64_Sym*)calloc(state->total_syms ? state->total_syms : 1, sizeof(Elf64_Sym));
    if (!state->symtab) {
        return ARK_LINK_ERR_MEMORY;
    }
    for (size_t i = 0; i < state->ns; i++) {
        Elf64_Sym* s = &state->symtab[1 + i];
        s->st_info = ELF_ST_INFO(STB_LOCAL, STT_SECTION);
        s->st_other = 0;
        s->st_shndx = (uint16_t)(1 + i);
        s->st_value = elf_section_vaddr(state, i);
        s->st_size = elf_section_vsize(state, i);
    }
    state->next_sym_idx = 1 + state->ns;

    for (size_t i = 0; i < state->input->export_count; i++) {
        const char* n = state->input->exports[i].name;
        if (!n) {
            continue;
        }
        uint32_t idx;
        if (!nim_lookup(&state->sym_map2, n, &idx)) {
            idx = (uint32_t)state->next_sym_idx++;
            Elf64_Sym* s = &state->symtab[idx];
            s->st_info = ELF_ST_INFO(STB_GLOBAL, state->input->exports[i].is_function ? STT_FUNC : STT_OBJECT);
            s->st_other = 0;
            if (state->input->exports[i].section_index < state->ns) {
                s->st_shndx = (uint16_t)state->input->exports[i].section_index;
                s->st_value =
                    elf_section_vaddr(state, state->input->exports[i].section_index) + state->input->exports[i].value;
                s->st_size = 0;
            } else {
                s->st_shndx = SHN_ABS;
                s->st_value = state->input->exports[i].value;
                s->st_size = 0;
            }
            if (!nim_add(&state->sym_map2, n, idx)) {
                return ARK_LINK_ERR_MEMORY;
            }
        }
    }
    for (size_t i = 0; i < state->input->import_count; i++) {
        const char* sym = state->input->imports[i].symbol;
        if (!sym) {
            continue;
        }
        uint32_t idx;
        if (!nim_lookup(&state->sym_map2, sym, &idx)) {
            idx = (uint32_t)state->next_sym_idx++;
            Elf64_Sym* s = &state->symtab[idx];
            s->st_info = ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            s->st_other = 0;
            s->st_shndx = SHN_UNDEF;
            s->st_value = 0;
            s->st_size = 0;
            if (!nim_add(&state->sym_map2, sym, idx)) {
                return ARK_LINK_ERR_MEMORY;
            }
        }
    }
    for (size_t i = 0; i < state->input->reloc_count; i++) {
        const ArkResolverSymbol* ss = state->input->relocs[i].symbol;
        if (!ss || !ss->name) {
            continue;
        }
        uint32_t idx;
        if (!nim_lookup(&state->sym_map2, ss->name, &idx)) {
            idx = (uint32_t)state->next_sym_idx++;
            Elf64_Sym* s = &state->symtab[idx];
            uint8_t bind = (ss->binding == ARK_BIND_WEAK) ? STB_WEAK : STB_GLOBAL;
            uint8_t type = STT_NOTYPE;
            s->st_info = ELF_ST_INFO(bind, type);
            s->st_other = (ss->visibility == ARK_VISIBILITY_HIDDEN) ? 2 : 0;
            if (ss->section_index < state->ns) {
                s->st_shndx = (uint16_t)(1 + ss->section_index);
                s->st_value = elf_section_vaddr(state, ss->section_index) + ss->value;
            } else {
                s->st_shndx = SHN_UNDEF;
                s->st_value = 0;
            }
            s->st_size = ss->size;
            if (!nim_add(&state->sym_map2, ss->name, idx)) {
                return ARK_LINK_ERR_MEMORY;
            }
        }
    }
    sb_free(&state->strtab);
    if (!sb_init(&state->strtab)) {
        return ARK_LINK_ERR_MEMORY;
    }
    sb_add(&state->strtab, "");

    size_t next_global_idx = (size_t)(1 + state->ns);
    for (size_t i = 0; i < state->input->export_count; i++) {
        const char* n = state->input->exports[i].name;
        if (!n) {
            continue;
        }
        uint32_t lookup;
        if (nim_lookup(&state->name_off_map, n, &lookup)) {
            continue;
        }
        uint32_t off = sb_add(&state->strtab, n);
        if (off == (uint32_t)-1) {
            return ARK_LINK_ERR_MEMORY;
        }

        Elf64_Sym* s = &state->symtab[next_global_idx];
        s->st_name = off;
        uint64_t export_sec_idx = state->input->exports[i].section_index;
        uint64_t sec_vaddr = elf_section_vaddr(state, export_sec_idx);
        s->st_value = sec_vaddr + state->input->exports[i].value;
        s->st_size = 0;
        s->st_info = ELF_ST_INFO(STB_GLOBAL, state->input->exports[i].is_function ? STT_FUNC : STT_OBJECT);
        s->st_other = 0;
        s->st_shndx = state->input->exports[i].section_index < state->ns
                          ? (uint16_t)(state->input->exports[i].section_index + 1)
                          : SHN_ABS;

        if (!nim_add(&state->name_off_map, n, off)) {
            return ARK_LINK_ERR_MEMORY;
        }
        next_global_idx++;
    }
    for (size_t i = 0; i < state->input->import_count; i++) {
        const char* sym = state->input->imports[i].symbol;
        if (!sym) {
            continue;
        }
        uint32_t lookup;
        if (nim_lookup(&state->name_off_map, sym, &lookup)) {
            continue;
        }
        uint32_t off = sb_add(&state->strtab, sym);
        if (off == (uint32_t)-1) {
            return ARK_LINK_ERR_MEMORY;
        }
        state->symtab[next_global_idx].st_name = off;
        if (!nim_add(&state->name_off_map, sym, off)) {
            return ARK_LINK_ERR_MEMORY;
        }
        next_global_idx++;
    }
    for (size_t i = 0; i < state->input->reloc_count; i++) {
        const ArkResolverSymbol* ss = state->input->relocs[i].symbol;
        if (!ss || !ss->name) {
            continue;
        }
        uint32_t lookup;
        if (nim_lookup(&state->name_off_map, ss->name, &lookup)) {
            continue;
        }
        uint32_t off = sb_add(&state->strtab, ss->name);
        if (off == (uint32_t)-1) {
            return ARK_LINK_ERR_MEMORY;
        }
        state->symtab[next_global_idx].st_name = off;
        if (!nim_add(&state->name_off_map, ss->name, off)) {
            return ARK_LINK_ERR_MEMORY;
        }
        next_global_idx++;
    }
    nim_free(&state->name_off_map);
    return ARK_LINK_OK;
}

static ArkLinkResult elf_build_relocations(ElfLinkState* state) {
    state->rela_arrays = (Elf64_Rela**)calloc(state->rela_sections ? state->rela_sections : 1, sizeof(Elf64_Rela*));
    if (!state->rela_arrays) {
        return ARK_LINK_ERR_MEMORY;
    }
    state->rela_idx = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (state->relocs_per_sec[i] == 0) {
            continue;
        }
        state->rela_arrays[state->rela_idx] = (Elf64_Rela*)calloc(state->relocs_per_sec[i], sizeof(Elf64_Rela));
        if (!state->rela_arrays[state->rela_idx]) {
            return ARK_LINK_ERR_MEMORY;
        }
        size_t cur = 0;
        for (size_t j = 0; j < state->input->reloc_count; j++) {
            const ArkResolverReloc* r = &state->input->relocs[j];
            if (r->section_index != i) {
                continue;
            }
            uint32_t sym_idx = 0;
            if (r->symbol && r->symbol->name) {
                if (!nim_lookup(&state->sym_map2, r->symbol->name, &sym_idx)) {
                    fprintf(stderr, "ArkLink: relocation symbol '%s' not in symtab\n", r->symbol->name);
                    return ARK_LINK_ERR_FORMAT;
                }
            }
            uint32_t r_type = 0;
            switch (r->type) {
            case ARK_RELOC_ABS64:
                r_type = R_X86_64_64;
                break;
            case ARK_RELOC_ADDR32:
            case ARK_RELOC_SECREL32:
                r_type = R_X86_64_32;
                break;
            case ARK_RELOC_PC32:
                r_type = R_X86_64_PC32;
                break;
            case ARK_RELOC_GOTPC32:
                r_type = R_X86_64_GOTPC32;
                break;
            case ARK_RELOC_AARCH64_ADR_PREL_PG_HI21:
                r_type = R_AARCH64_ADR_PREL_PG_HI21;
                break;
            case ARK_RELOC_AARCH64_ADD_ABS_LO12_NC:
                r_type = R_AARCH64_ADD_ABS_LO12_NC;
                break;
            case ARK_RELOC_AARCH64_LDR_ABS_LO12_NC:
                r_type = R_AARCH64_LDR_ABS_LO12_NC;
                break;
            case ARK_RELOC_AARCH64_CALL26:
                r_type = R_AARCH64_CALL26;
                break;
            case ARK_RELOC_AARCH64_ABS32:
                r_type = R_AARCH64_ABS32;
                break;
            default:
                fprintf(stderr, "ArkLink: unsupported ELF relocation type %d\n", (int)r->type);
                return ARK_LINK_ERR_FORMAT;
            }
            if (r->symbol && r->symbol->section_index == 0 && r->symbol->import_module != NULL) {
                if (!state->has_dynamic) {
                    fprintf(stderr, "ArkLink: import relocation without dynamic linking\n");
                    return ARK_LINK_ERR_FORMAT;
                }
                continue;
            }
            state->rela_arrays[state->rela_idx][cur].r_offset = elf_section_vaddr(state, r->section_index) + r->offset;
            state->rela_arrays[state->rela_idx][cur].r_info = ELF_R_INFO(sym_idx, r_type);
            state->rela_arrays[state->rela_idx][cur].r_addend = r->addend;
            cur++;
        }
        state->rela_idx++;
    }

    nim_free(&state->sym_map2);

    state->dynsym = NULL;
    memset(&state->dynstr, 0, sizeof(state->dynstr));
    state->dyntab = NULL;
    state->dyntab_count = 0;

    return ARK_LINK_OK;
}

static ArkLinkResult elf_build_dynamic(ElfLinkState* state) {
    if (state->has_dynamic) {
        if (!sb_init(&state->dynstr)) {
            return ARK_LINK_ERR_MEMORY;
        }

        size_t unique_modules = 0;
        for (size_t i = 0; i < state->input->import_count; i++) {
            const char* sym = state->input->imports[i].symbol;
            const char* mod = state->input->imports[i].module;
            if (sym && !nim_lookup(&state->symbol_off, sym, NULL)) {
                uint32_t off = sb_add(&state->dynstr, sym);
                if (off == (uint32_t)-1) {
                    return ARK_LINK_ERR_MEMORY;
                }
                if (!nim_add(&state->symbol_off, sym, off)) {
                    return ARK_LINK_ERR_MEMORY;
                }
            }
            if (mod && !nim_lookup(&state->module_off, mod, NULL)) {
                uint32_t off = sb_add(&state->dynstr, mod);
                if (off == (uint32_t)-1) {
                    return ARK_LINK_ERR_MEMORY;
                }
                if (!nim_add(&state->module_off, mod, off)) {
                    return ARK_LINK_ERR_MEMORY;
                }
                unique_modules++;
            }
        }

        size_t export_sym_count = 0;
        for (size_t i = 0; i < state->input->export_count; i++) {
            if (state->input->exports[i].is_function) {
                export_sym_count++;
            }
        }

        size_t dynsym_n = 1 + state->input->import_count + export_sym_count;
        state->dynsym = (Elf64_Sym*)calloc(dynsym_n, sizeof(Elf64_Sym));
        if (!state->dynsym) {
            return ARK_LINK_ERR_MEMORY;
        }

        size_t export_dynsym_idx = 1 + state->input->import_count;
        for (size_t i = 0; i < state->input->export_count; i++) {
            if (!state->input->exports[i].is_function) {
                continue;
            }

            const char* sym = state->input->exports[i].name;
            Elf64_Sym* s = &state->dynsym[export_dynsym_idx];
            s->st_info = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
            s->st_other = 0;
            s->st_shndx = state->input->exports[i].section_index < state->ns
                              ? (uint16_t)(state->input->exports[i].section_index + 1)
                              : SHN_ABS;
            if (state->input->exports[i].section_index < state->ns) {
                s->st_value = elf_section_vaddr(state, state->input->exports[i].section_index) +
                              state->input->exports[i].offset;
            } else {
                s->st_value = state->input->exports[i].value;
            }
            s->st_size = 0;
            uint32_t name_off = 0;
            if (sym && !nim_lookup(&state->symbol_off, sym, &name_off)) {
                name_off = sb_add(&state->dynstr, sym);
                if (name_off == (uint32_t)-1) {
                    return ARK_LINK_ERR_MEMORY;
                } else {
                    if (!nim_add(&state->symbol_off, sym, name_off)) {
                        return ARK_LINK_ERR_MEMORY;
                    }
                }
            }
            s->st_name = name_off;
            export_dynsym_idx++;
        }

        for (size_t i = 0; i < state->input->import_count; i++) {
            const char* sym = state->input->imports[i].symbol;
            Elf64_Sym* s = &state->dynsym[1 + i];
            s->st_info = ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            s->st_other = 0;
            s->st_shndx = SHN_UNDEF;
            s->st_value = 0;
            s->st_size = 0;
            s->st_name = (sym && nim_lookup(&state->symbol_off, sym, &s->st_name)) ? s->st_name : 0;
        }

        size_t rela_count_for_dyn = state->rela_sections;
        /* 14 base slots (STRTAB/SYMTAB/STRSZ/SYMENT/HASH, INIT_ARRAY/SZ,
         * FINI_ARRAY/SZ, 4 PLT tags, DT_NULL) plus one DT_NEEDED per module. */
        state->dyntab_count = 14 + unique_modules + (rela_count_for_dyn > 0 ? 3 : 0);
        state->dyntab = (Elf64_Dyn*)calloc(state->dyntab_count, sizeof(Elf64_Dyn));
        if (!state->dyntab) {
            return ARK_LINK_ERR_MEMORY;
        }
        size_t di = 0;
        state->dyntab[di].d_tag = DT_STRTAB;
        state->dyntab[di].d_val = elf_meta_vaddr(state, state->dynstr_shidx);
        di++;
        state->dyntab[di].d_tag = DT_SYMTAB;
        state->dyntab[di].d_val = elf_meta_vaddr(state, state->dynsym_shidx);
        di++;
        state->dyntab[di].d_tag = DT_STRSZ;
        state->dyntab[di].d_val = 0;
        di++;
        state->dyntab[di].d_tag = DT_SYMENT;
        state->dyntab[di].d_val = sizeof(Elf64_Sym);
        di++;
        state->dyntab[di].d_tag = DT_HASH;
        state->dyntab[di].d_val = elf_meta_vaddr(state, state->hash_shidx);
        di++;

        for (size_t i = 0; i < state->input->import_count; i++) {
            const char* mod = state->input->imports[i].module;
            if (!mod) {
                continue;
            }
            if (nim_lookup(&state->module_emitted, mod, NULL)) {
                continue;
            }
            uint32_t name_off = 0;
            if (!nim_lookup(&state->module_off, mod, &name_off)) {
                continue;
            }
            state->dyntab[di].d_tag = DT_NEEDED;
            state->dyntab[di].d_val = name_off;
            di++;
            if (!nim_add(&state->module_emitted, mod, 1)) {
                return ARK_LINK_ERR_MEMORY;
            }
        }
        nim_free(&state->module_emitted);

        state->dyntab[di].d_tag = DT_INIT_ARRAY;
        state->dyntab[di].d_val = elf_meta_vaddr(state, state->init_array_shidx);
        di++;
        state->dyntab[di].d_tag = DT_INIT_ARRAYSZ;
        state->dyntab[di].d_val = 0;
        di++;

        state->dyntab[di].d_tag = DT_FINI_ARRAY;
        state->dyntab[di].d_val = elf_meta_vaddr(state, state->fini_array_shidx);
        di++;
        state->dyntab[di].d_tag = DT_FINI_ARRAYSZ;
        state->dyntab[di].d_val = 0;
        di++;

        if (rela_count_for_dyn > 0) {

            uint64_t first_rela_vaddr = 0;
            uint64_t total_rela_size = 0;
            state->rela_idx = 0;
            for (size_t i = 0; i < state->ns; i++) {
                if (state->relocs_per_sec[i] == 0) {
                    continue;
                }
                if (first_rela_vaddr == 0) {
                    first_rela_vaddr = elf_meta_vaddr(state, state->rela_shidx_base + state->rela_idx);
                }
                total_rela_size += elf_meta_size(state, state->rela_shidx_base + state->rela_idx);
                state->rela_idx++;
            }
            state->dyntab[di].d_tag = DT_RELA;
            state->dyntab[di].d_val = first_rela_vaddr;
            di++;
            state->dyntab[di].d_tag = DT_RELASZ;
            state->dyntab[di].d_val = total_rela_size;
            di++;
            state->dyntab[di].d_tag = DT_RELAENT;
            state->dyntab[di].d_val = sizeof(Elf64_Rela);
            di++;
        }

        state->meta_secs[state->dynsym_shidx].size = (uint64_t)(dynsym_n * sizeof(Elf64_Sym));
        state->meta_secs[state->dynstr_shidx].size = (uint64_t)state->dynstr.buffer->size;
        state->meta_secs[state->dynamic_shidx].size = (uint64_t)(state->dyntab_count * sizeof(Elf64_Dyn));

        for (size_t i = 0; i < state->dyntab_count; i++) {
            if (state->dyntab[i].d_tag == DT_STRSZ) {
                state->dyntab[i].d_val = state->dynstr.buffer->size;
                break;
            }
        }

        state->func_import_count = 0;
        for (size_t i = 0; i < state->input->import_count; i++) {
            if (state->input->imports[i].is_function) {
                state->func_import_count++;
            }
        }

        size_t jump_slot_count = 0;
        size_t glob_dat_count = 0;
        size_t abs64_import_count = 0;

        for (size_t i = 0; i < state->input->reloc_count; i++) {
            const ArkResolverReloc* r = &state->input->relocs[i];
            if (!r->symbol || r->symbol->section_index != 0) {
                continue;
            }

            int is_func = 0;
            for (size_t j = 0; j < state->input->import_count; j++) {
                if (state->input->imports[j].is_function &&
                    strcmp(state->input->imports[j].symbol, r->symbol->name) == 0) {
                    is_func = 1;
                    break;
                }
            }

            if (is_func && state->func_import_count > 0) {
                jump_slot_count++;
            } else if (!is_func) {
                glob_dat_count++;
            } else {
                abs64_import_count++;
            }
        }

        state->rela_dyn_count = glob_dat_count + abs64_import_count;
        state->rela_plt_count = jump_slot_count;

        if (state->rela_dyn_count > 0) {
            state->meta_secs[state->reladyn_shidx].size = (uint64_t)(state->rela_dyn_count * sizeof(Elf64_Rela));
            state->rela_dyn_data = (Elf64_Rela*)calloc(state->rela_dyn_count, sizeof(Elf64_Rela));
            if (!state->rela_dyn_data) {
                return ARK_LINK_ERR_MEMORY;
            }
        } else {
            state->meta_secs[state->reladyn_shidx].size = 0;
        }

        if (state->rela_plt_count > 0) {
            state->rela_plt_data = (Elf64_Rela*)calloc(state->rela_plt_count, sizeof(Elf64_Rela));
            if (!state->rela_plt_data) {
                return ARK_LINK_ERR_MEMORY;
            }
        }

        if (state->func_import_count > 0 && state->rela_plt_count > 0) {
            if (state->rela_plt_count > UINT32_MAX / sizeof(Elf64_Rela)) {
                fprintf(stderr, "ArkLink: PLT relocation count overflow\n");
                return ARK_LINK_ERR_FORMAT;
            }
            state->dyntab[di].d_tag = DT_JMPREL;
            state->dyntab[di].d_val =
                elf_meta_vaddr(state, state->reladyn_shidx) + elf_meta_size(state, state->reladyn_shidx);
            di++;
            state->dyntab[di].d_tag = DT_PLTRELSZ;
            state->dyntab[di].d_val = (uint64_t)(state->rela_plt_count * sizeof(Elf64_Rela));
            di++;
            state->dyntab[di].d_tag = DT_PLTGOT;
            state->dyntab[di].d_val = elf_meta_vaddr(state, state->gotplt_shidx);
            di++;
            state->dyntab[di].d_tag = DT_PLTREL;
            state->dyntab[di].d_val = sizeof(Elf64_Rela);
            di++;
        }
        state->dyntab[di].d_tag = DT_NULL;
        state->dyntab[di].d_val = 0;
        di++;
        if (di > state->dyntab_count) {
            fprintf(stderr, "ArkLink: dynamic table overflow\n");
            return ARK_LINK_ERR_FORMAT;
        }

        size_t current_plt_idx = 1;

        for (size_t i = 0; i < state->input->import_count; i++) {
            const char* sym = state->input->imports[i].symbol;
            if (!nim_add(&state->dynsym_idx_map, sym, (uint32_t)(1 + i))) {
                return ARK_LINK_ERR_MEMORY;
            }

            if (state->input->imports[i].is_function) {
                if (!nim_add(&state->plt_idx_map, sym, (uint32_t)current_plt_idx++)) {
                    return ARK_LINK_ERR_MEMORY;
                }
            }
        }

        size_t dr_idx = 0;
        size_t pr_idx = 0;

        for (size_t i = 0; i < state->input->reloc_count; i++) {
            const ArkResolverReloc* r = &state->input->relocs[i];
            if (!r->symbol || r->symbol->section_index != 0) {
                continue;
            }

            const char* sym_name = r->symbol->name;
            uint32_t dyn_sym_idx = 0;
            if (!nim_lookup(&state->dynsym_idx_map, sym_name, &dyn_sym_idx)) {
                continue;
            }

            uint32_t plt_entry = 0;
            int is_func_with_plt =
                (nim_lookup(&state->plt_idx_map, sym_name, &plt_entry) && state->func_import_count > 0);

            if (is_func_with_plt) {
                uint64_t gotplt_entry_vaddr =
                    elf_meta_vaddr(state, state->gotplt_shidx) + (3 + (plt_entry - 1)) * sizeof(uint64_t);

                state->rela_plt_data[pr_idx].r_offset = gotplt_entry_vaddr;
                state->rela_plt_data[pr_idx].r_info = ELF_R_INFO(dyn_sym_idx, R_X86_64_JUMP_SLOT);
                state->rela_plt_data[pr_idx].r_addend = 0;
                pr_idx++;

            } else {
                uint64_t target_vaddr = elf_section_vaddr(state, r->section_index) + r->offset;

                if (glob_dat_count > 0) {
                    state->rela_dyn_data[dr_idx].r_offset = target_vaddr;
                    state->rela_dyn_data[dr_idx].r_info = ELF_R_INFO(dyn_sym_idx, R_X86_64_GLOB_DAT);
                    state->rela_dyn_data[dr_idx].r_addend = 0;
                } else {
                    state->rela_dyn_data[dr_idx].r_offset = target_vaddr;
                    state->rela_dyn_data[dr_idx].r_info = ELF_R_INFO(dyn_sym_idx, R_X86_64_64);
                    state->rela_dyn_data[dr_idx].r_addend = r->addend;
                }
                dr_idx++;
            }
        }
        nim_free(&state->dynsym_idx_map);
        nim_free(&state->plt_idx_map);

        for (size_t i = 0; i < state->dyntab_count; i++) {
            if (state->dyntab[i].d_tag == DT_RELA) {
                state->dyntab[i].d_val = elf_meta_vaddr(state, state->reladyn_shidx);
            } else if (state->dyntab[i].d_tag == DT_RELASZ) {
                state->dyntab[i].d_val = elf_meta_size(state, state->reladyn_shidx);
            }
        }

        nim_free(&state->module_off);
        nim_free(&state->symbol_off);
    }

    return ARK_LINK_OK;
}

static ArkLinkResult elf_write_headers(ElfLinkState* state) {
    state->out_buf = (uint8_t*)calloc(1, state->file_size);
    if (!state->out_buf) {
        return ARK_LINK_ERR_MEMORY;
    }

    Elf64_Ehdr ehdr = {0};
    ehdr.e_ident[0] = 0x7f;
    ehdr.e_ident[1] = 'E';
    ehdr.e_ident[2] = 'L';
    ehdr.e_ident[3] = 'F';
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_ident[7] = ELFOSABI_NONE;

    if (ark_backend_should_use_dynamic_elf(state->input)) {
        ehdr.e_type = ET_DYN;
    } else {
        ehdr.e_type = ET_EXEC;
    }
    ehdr.e_machine = (state->input->arch == KRO_ARCH_ARM64) ? EM_AARCH64 : EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    if (state->input->entry_section >= state->ns) {
        fprintf(stderr, "ArkLink: entry section %u out of range (%zu sections)\n", state->input->entry_section,
                state->ns);
        return ARK_LINK_ERR_FORMAT;
    }
    ehdr.e_entry = elf_section_vaddr(state, state->input->entry_section) + state->input->entry_offset;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = state->shdr_offset;
    ehdr.e_flags = 0;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);

    int has_relro = state->has_dynamic && state->func_import_count > 0;
    int needs_gnu_stack = 1;

    /* PT_PHDR must come first: some loaders (notably proot) use it to locate
     * the program header table of the tracee. */
    size_t phdr_load_total = 1 + (state->has_dynamic ? 2 : (size_t)state->n_static_loads) +
                             (state->has_dynamic ? 2 : 0) + (state->has_tls ? 1 : 0) +
                             (has_relro ? 1 : 0) + (needs_gnu_stack ? 1 : 0);
    Elf64_Phdr phdr_phdr = {0};
    phdr_phdr.p_type = PT_PHDR;
    phdr_phdr.p_flags = PF_R;
    phdr_phdr.p_offset = sizeof(Elf64_Ehdr);
    phdr_phdr.p_vaddr = state->image_base + sizeof(Elf64_Ehdr);
    phdr_phdr.p_paddr = phdr_phdr.p_vaddr;
    phdr_phdr.p_filesz = (uint64_t)(phdr_load_total * sizeof(Elf64_Phdr));
    phdr_phdr.p_memsz = phdr_phdr.p_filesz;
    phdr_phdr.p_align = sizeof(uint64_t);
    memcpy(state->out_buf + sizeof(Elf64_Ehdr), &phdr_phdr, sizeof(phdr_phdr));
    state->phdr_load_count = 1;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = (uint16_t)state->elf_shnum;
    ehdr.e_shstrndx = (uint16_t)state->shstrtab_shidx;
    if (!state->has_dynamic && !state->has_tls) {
        for (int li = 0; li < state->n_static_loads; li++) {
            memcpy(state->out_buf + sizeof(Elf64_Ehdr) + state->phdr_load_count * sizeof(Elf64_Phdr),
                   &state->static_loads[li], sizeof(Elf64_Phdr));
            state->phdr_load_count++;
        }
    } else {
        Elf64_Phdr phdr0 = {0};
        phdr0.p_type = PT_LOAD;
        phdr0.p_flags = PF_R | PF_X;
        phdr0.p_offset = 0;
        phdr0.p_vaddr = state->image_base;
        phdr0.p_paddr = state->image_base;
        phdr0.p_filesz = state->seg1_filesz;
        phdr0.p_memsz = state->seg1_memsz;
        phdr0.p_align = state->page_size;
        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + state->phdr_load_count * sizeof(Elf64_Phdr), &phdr0,
               sizeof(phdr0));
        state->phdr_load_count++;

        Elf64_Phdr phdr1 = {0};
        phdr1.p_type = PT_LOAD;
        phdr1.p_flags = PF_R | PF_W;
        phdr1.p_offset = state->seg2_offset;
        phdr1.p_vaddr = state->seg2_vaddr;
        phdr1.p_paddr = state->seg2_vaddr;
        phdr1.p_filesz = state->rw_filesz;
        phdr1.p_memsz = state->rw_memsz;
        phdr1.p_align = state->page_size;
        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + state->phdr_load_count * sizeof(Elf64_Phdr), &phdr1,
               sizeof(phdr1));
        state->phdr_load_count++;
    }

    ehdr.e_phnum = (uint16_t)phdr_load_total;
    memcpy(state->out_buf, &ehdr, sizeof(ehdr));

    Elf64_Phdr phdr2 = {0}, phdr3 = {0}, phdr_tls = {0};
    size_t phdr_write_idx = state->phdr_load_count;
    if (state->has_dynamic) {
        phdr2.p_type = PT_INTERP;
        phdr2.p_offset = elf_meta_offset(state, state->interp_shidx);
        phdr2.p_vaddr = elf_meta_vaddr(state, state->interp_shidx);
        phdr2.p_paddr = elf_meta_vaddr(state, state->interp_shidx);
        phdr2.p_filesz = elf_meta_size(state, state->interp_shidx);
        phdr2.p_memsz = elf_meta_size(state, state->interp_shidx);
        phdr2.p_align = 1;
        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr2, sizeof(phdr2));
        phdr_write_idx++;

        phdr3.p_type = PT_DYNAMIC;
        phdr3.p_offset = elf_meta_offset(state, state->dynamic_shidx);
        phdr3.p_vaddr = elf_meta_vaddr(state, state->dynamic_shidx);
        phdr3.p_paddr = elf_meta_vaddr(state, state->dynamic_shidx);
        phdr3.p_filesz = elf_meta_size(state, state->dynamic_shidx);
        phdr3.p_memsz = elf_meta_size(state, state->dynamic_shidx);
        phdr3.p_align = sizeof(uint64_t);
        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr3, sizeof(phdr3));
        phdr_write_idx++;
    }
    if (state->has_tls) {
        phdr_tls.p_type = PT_TLS;
        phdr_tls.p_offset = state->tls_offset;
        phdr_tls.p_vaddr = state->tls_vaddr;
        phdr_tls.p_paddr = state->tls_vaddr;
        phdr_tls.p_filesz = state->tls_filesz;
        phdr_tls.p_memsz = state->tls_memsz;
        phdr_tls.p_flags = PF_R | PF_W;
        phdr_tls.p_align = state->tls_align;
        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr_tls, sizeof(phdr_tls));
        phdr_write_idx++;
    }

    if (has_relro) {
        Elf64_Phdr phdr_relro = {0};
        phdr_relro.p_type = PT_GNU_RELRO;
        phdr_relro.p_flags = PF_R;

        uint64_t relro_start = elf_meta_vaddr(state, state->gotplt_shidx);
        uint64_t relro_end = elf_meta_vaddr(state, state->dynamic_shidx) + elf_meta_size(state, state->dynamic_shidx);

        phdr_relro.p_offset = elf_meta_offset(state, state->gotplt_shidx);
        phdr_relro.p_vaddr = relro_start;
        phdr_relro.p_paddr = relro_start;
        phdr_relro.p_filesz = (uint64_t)(relro_end - relro_start);
        phdr_relro.p_memsz = (uint64_t)(relro_end - relro_start);
        phdr_relro.p_align = state->page_size;

        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr_relro,
               sizeof(phdr_relro));
        phdr_write_idx++;
    }

    if (needs_gnu_stack) {
        Elf64_Phdr phdr_stack = {0};
        phdr_stack.p_type = PT_GNU_STACK;
        phdr_stack.p_flags = PF_R | PF_W; // RW: readable writable, non-executable stack
        phdr_stack.p_offset = 0;
        phdr_stack.p_vaddr = 0;
        phdr_stack.p_paddr = 0;
        phdr_stack.p_filesz = 0;
        phdr_stack.p_memsz = 0;
        phdr_stack.p_align = 16;

        memcpy(state->out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr_stack,
               sizeof(phdr_stack));
        phdr_write_idx++;
    }

    if (phdr_write_idx != phdr_load_total) {
        fprintf(stderr, "ArkLink: program header count mismatch (%zu written vs %zu planned)\n", phdr_write_idx,
                phdr_load_total);
        return ARK_LINK_ERR_FORMAT;
    }

    return ARK_LINK_OK;
}

static ArkLinkResult elf_write_contents(ElfLinkState* state) {
    for (size_t i = 0; i < state->ns; i++) {
        if (is_bss_kind((ArkSectionKind)state->input->sections[i].kind)) {
            continue;
        }
        if (state->input->sections[i].data && state->input->sections[i].size > 0) {
            memcpy(state->out_buf + elf_section_offset(state, i), state->input->sections[i].data,
                   state->input->sections[i].size);
        }
    }
    state->rela_idx = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (state->relocs_per_sec[i] == 0) {
            continue;
        }
        if (state->rela_arrays[state->rela_idx]) {
            memcpy(state->out_buf + elf_meta_offset(state, state->rela_shidx_base + state->rela_idx),
                   state->rela_arrays[state->rela_idx], state->relocs_per_sec[i] * sizeof(Elf64_Rela));
        }
        free(state->rela_arrays[state->rela_idx]);
        state->rela_idx++;
    }
    free(state->rela_arrays);
    state->rela_arrays = NULL;
    memcpy(state->out_buf + elf_meta_offset(state, state->symtab_shidx), state->symtab,
           state->total_syms * sizeof(Elf64_Sym));
    memcpy(state->out_buf + elf_meta_offset(state, state->strtab_shidx), state->strtab.buffer->data,
           state->strtab.buffer->size);
    memcpy(state->out_buf + elf_meta_offset(state, state->shstrtab_shidx), state->shstrtab.buffer->data,
           state->shstrtab.buffer->size);
    if (state->has_dynamic) {
        static const char interp_path[] = "/lib64/ld-linux-x86-64.so.2";

        memcpy(state->out_buf + elf_meta_offset(state, state->interp_shidx), interp_path, sizeof(interp_path));
        memcpy(state->out_buf + elf_meta_offset(state, state->dynsym_shidx), state->dynsym,
               elf_meta_size(state, state->dynsym_shidx));
        memcpy(state->out_buf + elf_meta_offset(state, state->dynstr_shidx), state->dynstr.buffer->data,
               elf_meta_size(state, state->dynstr_shidx));

        if (elf_meta_offset(state, state->hash_shidx) > 0 && elf_meta_size(state, state->hash_shidx) > 0) {
            size_t hash_nbuckets = 1;
            size_t hash_export_func_count = 0;
            for (size_t i = 0; i < state->input->export_count; i++) {
                if (state->input->exports[i].is_function) {
                    hash_export_func_count++;
                }
            }
            size_t hash_nchain = 1 + state->input->import_count + hash_export_func_count;
            uint8_t* hash_data = state->out_buf + elf_meta_offset(state, state->hash_shidx);
            /* Layout: nbucket, nchain, bucket[nbucket], chain[nchain]. With one
             * bucket the chain must start at the first real dynsym (index 1);
             * a zero head would make every lookup miss. */
            uint32_t hash_head = hash_nchain > 1 ? 1 : 0;
            uint32_t hash_header[3] = {(uint32_t)hash_nbuckets, (uint32_t)hash_nchain, hash_head};
            memcpy(hash_data, hash_header, sizeof(hash_header));
            for (size_t i = 0; i < hash_nchain; i++) {
                uint32_t chain = (uint32_t)((i + 1) % hash_nchain);
                memcpy(hash_data + (3 + i) * sizeof(chain), &chain, sizeof(chain));
            }
        }

        memcpy(state->out_buf + elf_meta_offset(state, state->dynamic_shidx), state->dyntab,
               elf_meta_size(state, state->dynamic_shidx));

        if (state->rela_dyn_data && state->rela_dyn_count > 0) {
            memcpy(state->out_buf + elf_meta_offset(state, state->reladyn_shidx), state->rela_dyn_data,
                   elf_meta_size(state, state->reladyn_shidx));
            free(state->rela_dyn_data);
            state->rela_dyn_data = NULL;
        }

        if (state->rela_plt_data && state->rela_plt_count > 0) {
            uint64_t rela_plt_offset =
                elf_meta_offset(state, state->reladyn_shidx) + elf_meta_size(state, state->reladyn_shidx);
            memcpy(state->out_buf + rela_plt_offset, state->rela_plt_data, state->rela_plt_count * sizeof(Elf64_Rela));
            free(state->rela_plt_data);
            state->rela_plt_data = NULL;
        }

        if (state->func_import_count > 0 && elf_meta_size(state, state->plt_shidx) > 0 &&
            elf_meta_size(state, state->gotplt_shidx) > 0) {

            if (elf_meta_offset(state, state->plt_shidx) == 0 || elf_meta_offset(state, state->gotplt_shidx) == 0) {
                state->func_import_count = 0;
            } else {
                uint8_t* plt_base = state->out_buf + elf_meta_offset(state, state->plt_shidx);
                uint64_t plt_vaddr = elf_meta_vaddr(state, state->plt_shidx);
                uint64_t gotplt_vaddr = elf_meta_vaddr(state, state->gotplt_shidx);

                size_t expected_plt_size = (1 + state->func_import_count) * 16;
                if (elf_meta_size(state, state->plt_shidx) < expected_plt_size) {
                    fprintf(stderr, "ArkLink: PLT size mismatch\n");
                    return ARK_LINK_ERR_FORMAT;
                }

                plt_base[0] = 0xff;
                plt_base[1] = 0x35;
                int32_t disp_to_got1 = (int32_t)((gotplt_vaddr + 1 * sizeof(uint64_t)) - (plt_vaddr + 6));
                memcpy(plt_base + 2, &disp_to_got1, 4);

                plt_base[6] = 0xff;
                plt_base[7] = 0x25;
                int32_t disp_to_got2 = (int32_t)((gotplt_vaddr + 2 * sizeof(uint64_t)) - (plt_vaddr + 12));
                memcpy(plt_base + 8, &disp_to_got2, 4);

                plt_base[12] = 0x0f;
                plt_base[13] = 0x1f;
                plt_base[14] = 0x40;
                plt_base[15] = 0x00;

                for (size_t i = 0; i < state->func_import_count; i++) {
                    uint8_t* entry = plt_base + (1 + i) * 16;
                    uint64_t entry_vaddr = plt_vaddr + (1 + i) * 16;
                    uint64_t got_entry_vaddr = gotplt_vaddr + (3 + i) * sizeof(uint64_t);

                    entry[0] = 0xff;
                    entry[1] = 0x25;
                    int32_t disp_to_got = (int32_t)(got_entry_vaddr - (entry_vaddr + 6));
                    memcpy(entry + 2, &disp_to_got, 4);

                    entry[6] = 0x68;
                    uint32_t reloc_idx = (uint32_t)i;
                    memcpy(entry + 7, &reloc_idx, 4);

                    entry[11] = 0xe9;
                    int32_t disp_to_plt0 = (int32_t)(plt_vaddr - (entry_vaddr + 15));
                    memcpy(entry + 12, &disp_to_plt0, 4);
                }
            }
        }

        if (state->func_import_count > 0 && elf_meta_size(state, state->gotplt_shidx) > 0) {
            uint8_t* gotplt_base = state->out_buf + elf_meta_offset(state, state->gotplt_shidx);
            uint64_t plt_vaddr = elf_meta_vaddr(state, state->plt_shidx);

            size_t required_got_entries = 3 + state->func_import_count;
            if (elf_meta_size(state, state->gotplt_shidx) < required_got_entries * sizeof(uint64_t)) {
                fprintf(stderr, "ArkLink: GOT.plt size mismatch\n");
                return ARK_LINK_ERR_FORMAT;
            }

            uint64_t dynamic_addr = elf_meta_vaddr(state, state->dynamic_shidx);
            memcpy(gotplt_base + 0 * sizeof(uint64_t), &dynamic_addr, sizeof(uint64_t));

            uint64_t zero = 0;
            memcpy(gotplt_base + 1 * sizeof(uint64_t), &zero, sizeof(uint64_t));

            memcpy(gotplt_base + 2 * sizeof(uint64_t), &zero, sizeof(uint64_t));

            for (size_t i = 0; i < state->func_import_count; i++) {

                uint64_t push_instr_addr = plt_vaddr + (1 + i) * 16 + 6;
                memcpy(gotplt_base + (3 + i) * sizeof(uint64_t), &push_instr_addr, sizeof(uint64_t));
            }
        }
    }

    return ARK_LINK_OK;
}

static ArkLinkResult elf_write_section_headers(ElfLinkState* state) {
    state->shdrs = (Elf64_Shdr*)calloc(state->elf_shnum, sizeof(Elf64_Shdr));
    if (!state->shdrs) {
        return ARK_LINK_ERR_MEMORY;
    }

    for (size_t i = 0; i < state->ns; i++) {
        Elf64_Shdr* sh = &state->shdrs[1 + i];
        const ArkSectionLayout* sl = ark_layout_get_section(state->layout, i);
        sh->sh_name = state->sec_name_off[1 + i];
        sh->sh_type = is_bss_kind((ArkSectionKind)state->input->sections[i].kind)
                          ? SHT_NOBITS
                          : kind_to_sh_type((ArkSectionKind)state->input->sections[i].kind);
        sh->sh_flags =
            kind_to_sh_flags((ArkSectionKind)state->input->sections[i].kind, state->input->sections[i].flags);
        sh->sh_addr = elf_section_vaddr(state, i);
        sh->sh_offset = elf_section_offset(state, i);
        sh->sh_size = elf_section_vsize(state, i);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = sl ? sl->alignment : state->input->sections[i].alignment;
        sh->sh_entsize = 0;
    }

    state->rela_idx = 0;
    for (size_t i = 0; i < state->ns; i++) {
        if (state->relocs_per_sec[i] == 0) {
            continue;
        }
        Elf64_Shdr* sh = &state->shdrs[state->rela_shidx_base + state->rela_idx];
        sh->sh_name = state->sec_name_off[state->rela_shidx_base + state->rela_idx];
        sh->sh_type = SHT_RELA;
        sh->sh_flags = 0;
        sh->sh_addr = elf_meta_vaddr(state, state->rela_shidx_base + state->rela_idx);
        sh->sh_offset = elf_meta_offset(state, state->rela_shidx_base + state->rela_idx);
        sh->sh_size = elf_meta_size(state, state->rela_shidx_base + state->rela_idx);
        sh->sh_link = (uint32_t)state->symtab_shidx;
        sh->sh_info = (uint32_t)(1 + i);
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(Elf64_Rela);
        state->rela_idx++;
    }

    {
        Elf64_Shdr* sh = &state->shdrs[state->symtab_shidx];
        sh->sh_name = state->sec_name_off[state->symtab_shidx];
        sh->sh_type = SHT_SYMTAB;
        sh->sh_flags = 0;
        sh->sh_addr = elf_meta_vaddr(state, state->symtab_shidx);
        sh->sh_offset = elf_meta_offset(state, state->symtab_shidx);
        sh->sh_size = (uint64_t)(state->total_syms * sizeof(Elf64_Sym));
        sh->sh_link = (uint32_t)state->strtab_shidx;
        sh->sh_info = (uint32_t)(1 + state->ns);
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(Elf64_Sym);
    }

    {
        Elf64_Shdr* sh = &state->shdrs[state->strtab_shidx];
        sh->sh_name = state->sec_name_off[state->strtab_shidx];
        sh->sh_type = SHT_STRTAB;
        sh->sh_flags = 0;
        sh->sh_addr = elf_meta_vaddr(state, state->strtab_shidx);
        sh->sh_offset = elf_meta_offset(state, state->strtab_shidx);
        sh->sh_size = state->strtab.buffer->size;
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize = 0;
    }
    {
        Elf64_Shdr* sh = &state->shdrs[state->shstrtab_shidx];
        sh->sh_name = state->sec_name_off[state->shstrtab_shidx];
        sh->sh_type = SHT_STRTAB;
        sh->sh_flags = 0;
        sh->sh_addr = elf_meta_vaddr(state, state->shstrtab_shidx);
        sh->sh_offset = elf_meta_offset(state, state->shstrtab_shidx);
        sh->sh_size = state->shstrtab.buffer->size;
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize = 0;
    }
    if (state->has_dynamic) {
        Elf64_Shdr* sh;
        sh = &state->shdrs[state->interp_shidx];
        sh->sh_name = state->sec_name_off[state->interp_shidx];
        sh->sh_type = SHT_PROGBITS;
        sh->sh_flags = SHF_ALLOC;
        sh->sh_addr = elf_meta_vaddr(state, state->interp_shidx);
        sh->sh_offset = elf_meta_offset(state, state->interp_shidx);
        sh->sh_size = elf_meta_size(state, state->interp_shidx);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize = 0;

        sh = &state->shdrs[state->dynsym_shidx];
        sh->sh_name = state->sec_name_off[state->dynsym_shidx];
        sh->sh_type = SHT_DYNSYM;
        sh->sh_flags = SHF_ALLOC;
        sh->sh_addr = elf_meta_vaddr(state, state->dynsym_shidx);
        sh->sh_offset = elf_meta_offset(state, state->dynsym_shidx);
        sh->sh_size = elf_meta_size(state, state->dynsym_shidx);
        sh->sh_link = (uint32_t)state->dynstr_shidx;
        sh->sh_info = 1;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(Elf64_Sym);

        sh = &state->shdrs[state->dynstr_shidx];
        sh->sh_name = state->sec_name_off[state->dynstr_shidx];
        sh->sh_type = SHT_STRTAB;
        sh->sh_flags = SHF_ALLOC;
        sh->sh_addr = elf_meta_vaddr(state, state->dynstr_shidx);
        sh->sh_offset = elf_meta_offset(state, state->dynstr_shidx);
        sh->sh_size = elf_meta_size(state, state->dynstr_shidx);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize = 0;

        sh = &state->shdrs[state->dynamic_shidx];
        sh->sh_name = state->sec_name_off[state->dynamic_shidx];
        sh->sh_type = SHT_DYNAMIC;
        sh->sh_flags = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr = elf_meta_vaddr(state, state->dynamic_shidx);
        sh->sh_offset = elf_meta_offset(state, state->dynamic_shidx);
        sh->sh_size = elf_meta_size(state, state->dynamic_shidx);
        sh->sh_link = (uint32_t)state->dynstr_shidx;
        sh->sh_info = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(Elf64_Dyn);

        sh = &state->shdrs[state->init_array_shidx];
        sh->sh_name = state->sec_name_off[state->init_array_shidx];
        sh->sh_type = SHT_INIT_ARRAY;
        sh->sh_flags = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr = elf_meta_vaddr(state, state->init_array_shidx);
        sh->sh_offset = elf_meta_offset(state, state->init_array_shidx);
        sh->sh_size = elf_meta_size(state, state->init_array_shidx);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(uint64_t);

        sh = &state->shdrs[state->fini_array_shidx];
        sh->sh_name = state->sec_name_off[state->fini_array_shidx];
        sh->sh_type = SHT_FINI_ARRAY;
        sh->sh_flags = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr = elf_meta_vaddr(state, state->fini_array_shidx);
        sh->sh_offset = elf_meta_offset(state, state->fini_array_shidx);
        sh->sh_size = elf_meta_size(state, state->fini_array_shidx);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(uint64_t);

        sh = &state->shdrs[state->reladyn_shidx];
        sh->sh_name = state->sec_name_off[state->reladyn_shidx];
        sh->sh_type = SHT_RELA;
        sh->sh_flags = SHF_ALLOC;
        sh->sh_addr = elf_meta_vaddr(state, state->reladyn_shidx);
        sh->sh_offset = elf_meta_offset(state, state->reladyn_shidx);
        sh->sh_size = elf_meta_size(state, state->reladyn_shidx);
        sh->sh_link = (uint32_t)state->dynsym_shidx;
        sh->sh_info = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(Elf64_Rela);

        sh = &state->shdrs[state->plt_shidx];
        sh->sh_name = state->sec_name_off[state->plt_shidx];
        sh->sh_type = SHT_PROGBITS;
        sh->sh_flags = SHF_ALLOC | SHF_EXECINSTR;
        sh->sh_addr = elf_meta_vaddr(state, state->plt_shidx);
        sh->sh_offset = elf_meta_offset(state, state->plt_shidx);
        sh->sh_size = elf_meta_size(state, state->plt_shidx);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = 16;
        sh->sh_entsize = 16;

        sh = &state->shdrs[state->gotplt_shidx];
        sh->sh_name = state->sec_name_off[state->gotplt_shidx];
        sh->sh_type = SHT_PROGBITS;
        sh->sh_flags = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr = elf_meta_vaddr(state, state->gotplt_shidx);
        sh->sh_offset = elf_meta_offset(state, state->gotplt_shidx);
        sh->sh_size = elf_meta_size(state, state->gotplt_shidx);
        sh->sh_link = 0;
        sh->sh_info = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize = sizeof(uint64_t);
    }
    memcpy(state->out_buf + state->shdr_offset, state->shdrs, state->elf_shnum * sizeof(Elf64_Shdr));

    return ARK_LINK_OK;
}

static ArkLinkResult elf_publish(ElfLinkState* state, ArkBackendOutput* output) {
    if (state->ns > 0) {
        ArkSectionRvaMap* maps = (ArkSectionRvaMap*)calloc(state->ns, sizeof(ArkSectionRvaMap));
        if (!maps) {
            return ARK_LINK_ERR_MEMORY;
        }
        for (size_t i = 0; i < state->ns; i++) {
            uint64_t rva = elf_section_vaddr(state, i) - state->image_base;
            uint64_t vsize = elf_section_vsize(state, i);
            uint64_t offset = elf_section_offset(state, i);
            if (rva > UINT32_MAX || vsize > UINT32_MAX || offset > UINT32_MAX) {
                fprintf(stderr, "ArkLink: section %zu address or size exceeds 32 bits\n", i);
                free(maps);
                return ARK_LINK_ERR_FORMAT;
            }
            maps[i].rva = (uint32_t)rva;
            maps[i].size = (uint32_t)vsize;
            maps[i].file_offset = (uint32_t)offset;
            maps[i].flags = state->input->sections[i].flags;
        }
        output->section_maps = maps;
        output->section_count = state->ns;
    } else {
        output->section_maps = NULL;
        output->section_count = 0;
    }
    output->data = state->out_buf;
    output->size = state->file_size;
    output->image_base = state->image_base;

    state->out_buf = NULL;
    return ARK_LINK_OK;
}

ArkLinkResult ark_backend_elf_link(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output) {
    (void)ctx;
    if (!input || !output) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    memset(output, 0, sizeof(*output));
    ElfLinkState state = {.input = input};
    ArkLinkResult (*const stages[])(ElfLinkState*) = {
        elf_prepare,       elf_plan_layout,   elf_build_symbols,  elf_build_relocations,
        elf_build_dynamic, elf_write_headers, elf_write_contents, elf_write_section_headers};
    ArkLinkResult result = ARK_LINK_OK;
    for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); i++) {
        result = stages[i](&state);
        if (result != ARK_LINK_OK) {
            break;
        }
    }
    if (result == ARK_LINK_OK) {
        result = elf_publish(&state, output);
    }
    elf_cleanup(&state);
    return result;
}
