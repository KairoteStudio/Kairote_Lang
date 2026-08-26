#include "ArkLink/BackendElf.h"
#include "ArkLink/Loader.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#pragma pack(push, 1)

typedef struct {
    uint8_t  e_ident[16];
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
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Rela;

typedef struct {
    int64_t  d_tag;
    uint64_t d_val;
} Elf64_Dyn;

#pragma pack(pop)

#define ELFCLASS64     2
#define ELFDATA2LSB    1
#define EV_CURRENT     1
#define ELFOSABI_NONE  0
#define ET_EXEC        2
#define ET_DYN         3
#define EM_X86_64      0x3E

#define PT_LOAD        1
#define PT_DYNAMIC     2
#define PT_INTERP      3
#define PT_TLS         7
#define PT_GNU_RELRO   0x6474e552
#define PT_GNU_STACK   0x6474e551
#define PF_X           1
#define PF_W           2
#define PF_R           4

#define DT_NULL        0
#define DT_NEEDED      1
#define DT_STRTAB      5
#define DT_SYMTAB      6
#define DT_STRSZ       10
#define DT_SYMENT      11
#define DT_RELA        7
#define DT_RELASZ      8
#define DT_RELAENT     9
#define DT_DEBUG       21

#define SHT_NULL       0
#define SHT_PROGBITS   1
#define SHT_SYMTAB     2
#define SHT_STRTAB     3
#define SHT_RELA       4
#define SHT_DYNAMIC    6
#define SHT_NOBITS     8
#define SHT_DYNSYM     11
#define SHT_INIT_ARRAY 14
#define SHT_FINI_ARRAY 15
#define SHT_TLS        17

#define SHF_WRITE      0x1
#define SHF_ALLOC      0x2
#define SHF_EXECINSTR  0x4
#define SHF_TLS        0x400

#define STB_LOCAL      0
#define STB_GLOBAL     1
#define STB_WEAK       2
#define STT_NOTYPE     0
#define STT_OBJECT     1
#define STT_FUNC       2
#define STT_SECTION    3
#define STT_FILE       4

#define SHN_UNDEF      0
#define SHN_ABS        0xfff1

#define R_X86_64_64        1
#define R_X86_64_PC32      2
#define R_X86_64_32        10
#define R_X86_64_GOTPC32   29
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_GLOB_DAT 6

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_STRSZ        10
#define DT_SYMENT       11
#define DT_INIT         12
#define DT_FINI         13
#define DT_SONAME       14
#define DT_RPATH        15
#define DT_SYMBOLIC     16
#define DT_REL          17
#define DT_RELSZ        18
#define DT_RELENT       19
#define DT_PLTREL       20
#define DT_DEBUG        21
#define DT_TEXTREL      22
#define DT_JMPREL       23
#define DT_BIND_NOW     24
#define DT_INIT_ARRAY   25
#define DT_INIT_ARRAYSZ 26
#define DT_FINI_ARRAY   27
#define DT_FINI_ARRAYSZ 28

#define ELF_ST_BIND(i)   ((uint8_t)((i) >> 4))
#define ELF_ST_TYPE(i)   ((uint8_t)((i) & 0xf))
#define ELF_ST_INFO(b,t) ((uint8_t)(((b) << 4) | ((t) & 0xf)))
#define ELF_R_SYM(i)     ((uint32_t)((i) >> 32))
#define ELF_R_TYPE(i)    ((uint32_t)((i) & 0xffffffff))
#define ELF_R_INFO(s,t)  (((uint64_t)(s) << 32) | (uint64_t)(t))

typedef struct {
    ArkBuffer* buffer;
} ElfStrBuilder;

static int sb_init(ElfStrBuilder* sb) {
    sb->buffer = ark_buffer_create(64, 0);
    if (!sb->buffer) return 0;
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
    if (!sb || !sb->buffer) return (uint32_t)-1;
    return ark_buffer_add_string(sb->buffer, s);
}

static int is_tls_kind(ArkSectionKind kind);

static const char* kind_to_canonical(ArkSectionKind kind) {
    switch (kind) {
        case ARK_SECTION_CODE:   return ".text";
        case ARK_SECTION_DATA:   return ".data";
        case ARK_SECTION_RODATA: return ".rodata";
        case ARK_SECTION_BSS:    return ".bss";
        case ARK_SECTION_TDATA:  return ".tdata";
        case ARK_SECTION_TBSS:   return ".tbss";
        default:                 return ".data";
    }
}

static uint32_t kind_to_sh_type(void) {
    return SHT_PROGBITS;
}

static uint64_t kind_to_sh_flags(ArkSectionKind kind, uint32_t input_flags) {
    uint64_t f = SHF_ALLOC;
    if (kind == ARK_SECTION_CODE)   f |= SHF_EXECINSTR;
    if (kind == ARK_SECTION_BSS)    f |= SHF_WRITE;
    if (kind == ARK_SECTION_DATA)   f |= SHF_WRITE;
    if (is_tls_kind(kind))          f |= SHF_WRITE | SHF_TLS;
    if (input_flags & ARK_SECTION_EXEC)  f |= SHF_EXECINSTR;
    if (input_flags & ARK_SECTION_WRITE) f |= SHF_WRITE;
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
    snprintf(buf, sizeof(buf), "%s.%zu", kind_to_canonical(kind), occurrence);
    return sb_add(shstrtab, buf);
}

static uint32_t get_rela_name(ElfStrBuilder* shstrtab, const char* target_name) {
    char buf[80];
    snprintf(buf, sizeof(buf), ".rela%s", target_name);
    return sb_add(shstrtab, buf);
}

typedef struct {
    const char** names;
    uint32_t*    indices;
    size_t       count;
    size_t       capacity;
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
            if (out_idx) *out_idx = m->indices[i];
            return 1;
        }
    }
    return 0;
}

static int nim_add(NameIndexMap* m, const char* name, uint32_t idx) {
    if (nim_lookup(m, name, &idx)) return 1;
    if (m->count == m->capacity) {
        size_t new_cap = m->capacity ? m->capacity * 2 : 16;
        const char** nn = (const char**)realloc(m->names, new_cap * sizeof(const char*));
        uint32_t* ni = (uint32_t*)realloc(m->indices, new_cap * sizeof(uint32_t));
        if (!nn || !ni) {
            free(nn); free(ni);
            return 0;
        }
        m->names = nn;
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

ArkLinkResult ark_backend_elf_link(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output) {
    (void)ctx;
    if (!input || !output) return ARK_LINK_ERR_INVALID_ARGUMENT;
    memset(output, 0, sizeof(ArkBackendOutput));

    const uint64_t page_size    = 0x1000;
    uint64_t image_base = input->image_base ? input->image_base : 0x400000;
    size_t ns = input->section_count;

    ArkImageLayout* layout = ark_layout_create(input, (uint32_t)page_size, (uint32_t)page_size);
    if (!layout) {
        return ARK_LINK_ERR_MEMORY;
    }
    image_base = layout->image_base;

    Elf64_Rela* rela_dyn_data = NULL;
    size_t rela_dyn_count = 0;
    Elf64_Rela* rela_plt_data = NULL;
    size_t rela_plt_count = 0;
    size_t func_import_count = 0;
    Elf64_Sym*  symtab = NULL;
    Elf64_Sym*  dynsym = NULL;
    Elf64_Dyn*  dyntab = NULL;
    ElfStrBuilder dynstr = {0};
    uint32_t*   sec_name_off = NULL;
    size_t*     relocs_per_sec = NULL;
    ElfStrBuilder strtab = {0};
    ElfStrBuilder shstrtab = {0};
    Elf64_Rela** rela_arrays = NULL;

    relocs_per_sec = (size_t*)calloc(ns ? ns : 1, sizeof(size_t));
    if (!relocs_per_sec) { ark_layout_destroy(layout); return ARK_LINK_ERR_MEMORY; }
    for (size_t i = 0; i < input->reloc_count; i++) {
        uint32_t s = input->relocs[i].section_index;
        if (s < ns) relocs_per_sec[s]++;
    }
    size_t rela_sections = 0;
    for (size_t i = 0; i < ns; i++) {
        if (relocs_per_sec[i] > 0) rela_sections++;
    }

    int has_dynamic = (input->import_count > 0) ? 1 : 0;
    int has_tls = 0;
    for (size_t i = 0; i < ns; i++) {
        if (is_tls_kind((ArkSectionKind)input->sections[i].kind)) { has_tls = 1; break; }
    }

    uint64_t dynstr_actual_size = 1;
    if (has_dynamic) {
        const char** seen_names = (const char**)calloc(input->import_count * 2 + 2, sizeof(const char*));
        size_t seen_count = 0;
        for (size_t i = 0; i < input->import_count; i++) {
            const char* sym = input->imports[i].symbol;
            const char* mod = input->imports[i].module;
            int sym_seen = 0, mod_seen = 0;
            for (size_t k = 0; k < seen_count; k++) {
                if (!sym_seen && seen_names[k] && strcmp(seen_names[k], sym) == 0) sym_seen = 1;
                if (!mod_seen && seen_names[k] && strcmp(seen_names[k], mod) == 0) mod_seen = 1;
            }
            if (sym && !sym_seen) {
                dynstr_actual_size += (uint64_t)strlen(sym) + 1;
                seen_names[seen_count++] = sym;
            }
            if (mod && !mod_seen) {
                dynstr_actual_size += (uint64_t)strlen(mod) + 1;
                seen_names[seen_count++] = mod;
            }
        }
        free(seen_names);
    }
    const size_t dyn_extra = (size_t)(has_dynamic ? 10 : 0);
    size_t elf_shnum        = 1 + ns + rela_sections + 3 + dyn_extra;
    size_t rela_shidx_base  = 1 + ns;
    size_t symtab_shidx     = rela_shidx_base + rela_sections;
    size_t strtab_shidx     = symtab_shidx + 1;
    size_t shstrtab_shidx   = strtab_shidx + 1;
    size_t interp_shidx     = has_dynamic ? (shstrtab_shidx + 1) : 0;
    size_t dynsym_shidx     = has_dynamic ? (shstrtab_shidx + 2) : 0;
    size_t dynstr_shidx     = has_dynamic ? (shstrtab_shidx + 3) : 0;
    size_t dynamic_shidx    = has_dynamic ? (shstrtab_shidx + 4) : 0;
    size_t hash_shidx       = has_dynamic ? (shstrtab_shidx + 5) : 0;
    size_t init_array_shidx = has_dynamic ? (shstrtab_shidx + 6) : 0;
    size_t fini_array_shidx = has_dynamic ? (shstrtab_shidx + 7) : 0;
    size_t reladyn_shidx    = has_dynamic ? (shstrtab_shidx + 8) : 0;
    size_t plt_shidx        = has_dynamic ? (shstrtab_shidx + 9) : 0;
    size_t gotplt_shidx     = has_dynamic ? (shstrtab_shidx + 10) : 0;

    if (!sb_init(&shstrtab)) { free(relocs_per_sec); ark_layout_destroy(layout); return ARK_LINK_ERR_MEMORY; }
    sec_name_off = (uint32_t*)calloc(elf_shnum, sizeof(uint32_t));
    if (!sec_name_off) { sb_free(&shstrtab); free(relocs_per_sec); ark_layout_destroy(layout); return ARK_LINK_ERR_MEMORY; }
    sec_name_off[0] = 0;
    size_t occ[6] = {0, 0, 0, 0, 0, 0};
    for (size_t i = 0; i < ns; i++) {
        ArkSectionKind k = (ArkSectionKind)input->sections[i].kind;
        size_t kidx = (k == ARK_SECTION_CODE)   ? 0 :
                      (k == ARK_SECTION_DATA)   ? 1 :
                      (k == ARK_SECTION_RODATA) ? 2 :
                      (k == ARK_SECTION_BSS)    ? 3 :
                      (k == ARK_SECTION_TDATA)  ? 4 : 5;
        sec_name_off[1 + i] = get_unique_name(&shstrtab, k, occ[kidx]++);
    }
    size_t rela_idx = 0;
    for (size_t i = 0; i < ns; i++) {
        if (relocs_per_sec[i] == 0) continue;
        const char* target_name = (const char*)shstrtab.buffer->data + sec_name_off[1 + i];
        sec_name_off[rela_shidx_base + rela_idx] = get_rela_name(&shstrtab, target_name);
        rela_idx++;
    }
    sec_name_off[symtab_shidx]   = sb_add(&shstrtab, ".symtab");
    sec_name_off[strtab_shidx]   = sb_add(&shstrtab, ".strtab");
    sec_name_off[shstrtab_shidx] = sb_add(&shstrtab, ".shstrtab");
    if (has_dynamic) {
        sec_name_off[interp_shidx]     = sb_add(&shstrtab, ".interp");
        sec_name_off[dynsym_shidx]     = sb_add(&shstrtab, ".dynsym");
        sec_name_off[dynstr_shidx]     = sb_add(&shstrtab, ".dynstr");
        sec_name_off[dynamic_shidx]    = sb_add(&shstrtab, ".dynamic");
        sec_name_off[hash_shidx]       = sb_add(&shstrtab, ".hash");
        sec_name_off[init_array_shidx] = sb_add(&shstrtab, ".init_array");
        sec_name_off[fini_array_shidx] = sb_add(&shstrtab, ".fini_array");
        sec_name_off[reladyn_shidx]    = sb_add(&shstrtab, ".rela.dyn");
        sec_name_off[plt_shidx]        = sb_add(&shstrtab, ".plt");
        sec_name_off[gotplt_shidx]     = sb_add(&shstrtab, ".got.plt");
    }
    if (sec_name_off[symtab_shidx] == (uint32_t)-1 || sec_name_off[strtab_shidx] == (uint32_t)-1
        || sec_name_off[shstrtab_shidx] == (uint32_t)-1) {
        sb_free(&shstrtab); free(sec_name_off); free(relocs_per_sec); ark_layout_destroy(layout);
        return ARK_LINK_ERR_MEMORY;
    }

    if (!sb_init(&strtab)) {
        sb_free(&shstrtab); free(sec_name_off); free(relocs_per_sec); ark_layout_destroy(layout);
        return ARK_LINK_ERR_MEMORY;
    }
    sb_add(&strtab, "");
    NameIndexMap sym_map = {0};
    size_t next_sym_idx = 1 + ns;

    #define INTERN_SYM(name_ptr, out_idx) do {                                  \
        if (!nim_lookup(&sym_map, (name_ptr), (out_idx))) {                     \
            uint32_t no = sb_add(&strtab, (name_ptr));                          \
            if (no == (uint32_t)-1) goto oom;                                   \
            if (!nim_add(&sym_map, (name_ptr), (uint32_t)next_sym_idx)) goto oom;\
            *(out_idx) = (uint32_t)next_sym_idx++;                              \
        }                                                                       \
    } while (0)

    for (size_t i = 0; i < input->export_count; i++) {
        const char* n = input->exports[i].name;
        if (!n) continue;
        uint32_t idx;
        INTERN_SYM(n, &idx);
    }
    for (size_t i = 0; i < input->import_count; i++) {
        const char* sym = input->imports[i].symbol;
        if (!sym) continue;
        uint32_t idx;
        INTERN_SYM(sym, &idx);
    }
    for (size_t i = 0; i < input->reloc_count; i++) {
        const ArkResolverSymbol* s = input->relocs[i].symbol;
        if (!s || !s->name) continue;
        uint32_t idx;
        INTERN_SYM(s->name, &idx);
    }
    #undef INTERN_SYM

    size_t total_syms = next_sym_idx;
    nim_free(&sym_map);

    for (size_t i = 0; i < input->reloc_count; i++) {
        ArkResolverReloc* reloc = &input->relocs[i];
        if (reloc->symbol && reloc->symbol->section_index >= 0 && reloc->symbol->section_index <= ns) {
            uint64_t sec_vaddr = ark_layout_get_section(layout, reloc->symbol->section_index) ?
                                 ark_layout_get_section(layout, reloc->symbol->section_index)->virtual_address : 0;
            reloc->symbol_rva = (uint32_t)(sec_vaddr + reloc->symbol->value);
#ifdef ARK_DEBUG
#endif
        }
    }


    for (size_t i = 0; i < ns; i++) {
        ArkSectionBuffer* target = &input->sections[i];
        if (!target->data) continue;
        if (is_bss_kind((ArkSectionKind)target->kind)) continue;

        uint32_t target_sec_idx = (uint32_t)i;
        ark_reloc_process_all(input->relocs, input->reloc_count,
                              ark_reloc_apply_elf,
                              reloc_filter_by_section,
                              &target_sec_idx,
                              target->data,
                              target->size,
                              layout);
    }

    #define SEC_VADDR(sec_idx)   (ark_layout_get_section(layout, (sec_idx)) ? ark_layout_get_section(layout, (sec_idx))->virtual_address : 0)
    #define SEC_OFFSET(sec_idx)  (ark_layout_get_section(layout, (sec_idx)) ? ark_layout_get_section(layout, (sec_idx))->file_offset : 0)
    #define SEC_FSIZE(sec_idx)   (ark_layout_get_section(layout, (sec_idx)) ? ark_layout_get_section(layout, (sec_idx))->file_size : 0)
    #define SEC_VSIZE(sec_idx)   (ark_layout_get_section(layout, (sec_idx)) ? ark_layout_get_section(layout, (sec_idx))->virtual_size : 0)

    typedef struct {
        uint64_t vaddr;
        uint64_t offset;
        uint64_t size;
    } ElfMetaSection;

    ElfMetaSection* meta_secs = NULL;
    if (elf_shnum > 0) {
        meta_secs = (ElfMetaSection*)calloc(elf_shnum, sizeof(ElfMetaSection));
        if (!meta_secs) { free(relocs_per_sec); ark_layout_destroy(layout); return ARK_LINK_ERR_MEMORY; }
    }

    #define META_VADDR(idx)   (meta_secs ? meta_secs[(idx)].vaddr : 0)
    #define META_OFFSET(idx)  (meta_secs ? meta_secs[(idx)].offset : 0)
    #define META_SIZE(idx)    (meta_secs ? meta_secs[(idx)].size : 0)

    uint64_t file_size = layout->file_size;

    uint64_t cur_offset = file_size;
    uint64_t rx_end = cur_offset;

    if (has_dynamic) {
        static const char interp_path[] = "/lib64/ld-linux-x86-64.so.2";
        size_t interp_size = sizeof(interp_path);
        meta_secs[interp_shidx].offset = cur_offset;
        meta_secs[interp_shidx].vaddr = image_base + cur_offset;
        meta_secs[interp_shidx].size  = (uint64_t)interp_size;
        cur_offset += interp_size;
    }

    rela_idx = 0;
    for (size_t i = 0; i < ns; i++) {
        if (relocs_per_sec[i] == 0) continue;
        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        meta_secs[rela_shidx_base + rela_idx].offset = cur_offset;
        meta_secs[rela_shidx_base + rela_idx].vaddr = has_dynamic ? (image_base + cur_offset) : 0;
        meta_secs[rela_shidx_base + rela_idx].size  = (uint64_t)(relocs_per_sec[i] * sizeof(Elf64_Rela));
        cur_offset += meta_secs[rela_shidx_base + rela_idx].size;
        rela_idx++;
    }

    cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
    meta_secs[symtab_shidx].offset = cur_offset;
    meta_secs[symtab_shidx].vaddr = 0;
    meta_secs[symtab_shidx].size  = 0;
    cur_offset += (uint64_t)(total_syms * sizeof(Elf64_Sym));

    cur_offset = ark_backend_align_up(cur_offset, 1);
    meta_secs[strtab_shidx].offset = cur_offset;
    meta_secs[strtab_shidx].vaddr = 0;
    meta_secs[strtab_shidx].size  = 0;
    cur_offset += strtab.buffer->size;

    cur_offset = ark_backend_align_up(cur_offset, 1);
    meta_secs[shstrtab_shidx].offset = cur_offset;
    meta_secs[shstrtab_shidx].vaddr = 0;
    meta_secs[shstrtab_shidx].size  = shstrtab.buffer->size;
    cur_offset += shstrtab.buffer->size;

    if (has_dynamic) {
        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        meta_secs[dynsym_shidx].offset = cur_offset;
        meta_secs[dynsym_shidx].vaddr = image_base + cur_offset;
        meta_secs[dynsym_shidx].size  = 0;
        cur_offset += (uint64_t)((1 + input->import_count) * sizeof(Elf64_Sym));

        cur_offset = ark_backend_align_up(cur_offset, 1);
        meta_secs[dynstr_shidx].offset = cur_offset;
        meta_secs[dynstr_shidx].vaddr = image_base + cur_offset;
        meta_secs[dynstr_shidx].size  = dynstr_actual_size;
        cur_offset += dynstr_actual_size;

        {
            size_t hash_nbuckets = 1;
            size_t hash_export_func_count = 0;
            for (size_t i = 0; i < input->export_count; i++) {
                if (input->exports[i].is_function) hash_export_func_count++;
            }
            size_t hash_nchain = 1 + input->import_count + hash_export_func_count + 1;
            size_t hash_size = (2 + hash_nbuckets + hash_nchain) * sizeof(uint32_t);
            cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
            meta_secs[hash_shidx].offset = cur_offset;
            meta_secs[hash_shidx].vaddr = image_base + cur_offset;
            meta_secs[hash_shidx].size  = (uint64_t)hash_size;
            cur_offset += hash_size;
        }

        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        meta_secs[reladyn_shidx].offset = cur_offset;
        meta_secs[reladyn_shidx].vaddr = image_base + cur_offset;
        meta_secs[reladyn_shidx].size  = (uint64_t)(input->import_count * sizeof(Elf64_Rela));
        cur_offset += meta_secs[reladyn_shidx].size;

        cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
        meta_secs[init_array_shidx].offset = cur_offset;
        meta_secs[init_array_shidx].vaddr = image_base + cur_offset;
        meta_secs[init_array_shidx].size  = 0;
        meta_secs[fini_array_shidx].offset = cur_offset;
        meta_secs[fini_array_shidx].vaddr = image_base + cur_offset;
        meta_secs[fini_array_shidx].size  = 0;

        func_import_count = 0;
        for (size_t i = 0; i < input->import_count; i++) {

            if (input->imports[i].is_function) {
                func_import_count++;
            }
        }

        if (func_import_count > 0) {
            size_t plt_entry_size = 16;
            size_t plt_size = (1 + func_import_count) * plt_entry_size;

            cur_offset = ark_backend_align_up(cur_offset, 16);
            meta_secs[plt_shidx].offset = cur_offset;
            meta_secs[plt_shidx].vaddr = image_base + cur_offset;
            meta_secs[plt_shidx].size  = plt_size;
            cur_offset += plt_size;

            size_t gotplt_entries = 3 + func_import_count;
            size_t gotplt_size = gotplt_entries * sizeof(uint64_t);

            cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
            meta_secs[gotplt_shidx].offset = cur_offset;
            meta_secs[gotplt_shidx].vaddr = image_base + cur_offset;
            meta_secs[gotplt_shidx].size  = gotplt_size;
            cur_offset += gotplt_size;

        } else {
            meta_secs[plt_shidx].offset = 0;
            meta_secs[plt_shidx].vaddr = 0;
            meta_secs[plt_shidx].size  = 0;
            meta_secs[gotplt_shidx].offset = 0;
            meta_secs[gotplt_shidx].vaddr = 0;
            meta_secs[gotplt_shidx].size  = 0;
        }
    }

    cur_offset = ark_backend_align_up(cur_offset, sizeof(uint64_t));
    uint64_t shdr_offset = cur_offset;
    uint64_t shdr_size = (uint64_t)(elf_shnum * sizeof(Elf64_Shdr));
    cur_offset += shdr_size;

    rx_end = ark_backend_align_up(cur_offset, page_size);

    uint64_t rw_start = rx_end;
    uint64_t rw_cur = rx_end;
    uint64_t tls_offset = 0, tls_vaddr = 0;
    uint64_t tls_filesz = 0, tls_memsz = 0, tls_align = 1;

    if (has_dynamic) {
        rw_cur = ark_backend_align_up(rw_cur, sizeof(uint64_t));
        meta_secs[dynamic_shidx].offset = rw_cur;
        meta_secs[dynamic_shidx].vaddr = image_base + rw_cur;
        size_t dynamic_upper = 10 + input->import_count + (rela_sections > 0 ? 3 : 0);
        meta_secs[dynamic_shidx].size  = (uint64_t)(dynamic_upper * sizeof(Elf64_Dyn));
        rw_cur += meta_secs[dynamic_shidx].size;
    }

    file_size = cur_offset;
    if (rw_cur > file_size) file_size = rw_cur;
    if (layout->file_size > file_size) file_size = layout->file_size;

    uint64_t code_file_end = 0;
    if (has_dynamic && interp_shidx > 0) {
        code_file_end = meta_secs[interp_shidx].offset;
    } else {
        for (size_t i = 0; i < ns; i++) {
            if ((ArkSectionKind)input->sections[i].kind == ARK_SECTION_CODE) {
                code_file_end = meta_secs[i + 1].offset + meta_secs[i + 1].size;
                break;
            }
        }
        if (code_file_end == 0) code_file_end = cur_offset;
    }

    uint64_t seg1_filesz = 0, seg1_memsz = 0;
    uint64_t seg2_offset = 0, seg2_vaddr = 0;
    uint64_t rw_filesz = 0, rw_memsz = 0;
    static Elf64_Phdr static_loads[48];
    int n_static_loads = 0;
    size_t phdr_load_count = 0;
    if (!has_dynamic && !has_tls) {
        uint32_t cur_flags = 0;
        for (size_t i = 0; i < ns; i++) {
            ArkSectionKind k = (ArkSectionKind)input->sections[i].kind;
            if (k != ARK_SECTION_CODE && k != ARK_SECTION_DATA &&
                k != ARK_SECTION_RODATA && k != ARK_SECTION_BSS &&
                k != ARK_SECTION_TDATA) continue;
            ArkSectionLayout* lsec = ark_layout_get_section(layout, i);
            uint64_t va = SEC_VADDR(i);
            uint64_t vsz = SEC_VSIZE(i);
            uint64_t off = lsec ? lsec->file_offset : meta_secs[i + 1].offset;
            uint32_t f = (k == ARK_SECTION_CODE) ? (PF_R | PF_X)
                       : (k == ARK_SECTION_RODATA) ? PF_R
                       : (PF_R | PF_W);
            uint64_t filesz_add = (k == ARK_SECTION_BSS) ? 0 : vsz;

            if (n_static_loads > 0 && cur_flags == f) {
                Elf64_Phdr* c = &static_loads[n_static_loads - 1];
                uint64_t fend = off + filesz_add;
                uint64_t vend = va + vsz;
                if (fend > c->p_offset + c->p_filesz) { /* 同权限但文件上不相邻则开新段,这个{}为空是正常的 */ }
                if ((uint64_t)(c->p_offset + c->p_filesz) >= off || fend <= (uint64_t)(c->p_offset + c->p_filesz)) {
                    if (fend > (uint64_t)(c->p_offset + c->p_filesz)) c->p_filesz = fend - c->p_offset;
                    if (vend > (uint64_t)(c->p_vaddr + c->p_memsz)) c->p_memsz = vend - c->p_vaddr;
                    continue;
                }
            }
            if (n_static_loads >= 48) break;
            Elf64_Phdr* seg = &static_loads[n_static_loads++];
            memset(seg, 0, sizeof(*seg));
            seg->p_type   = PT_LOAD;
            seg->p_flags  = f;
            seg->p_offset = off;
            seg->p_vaddr  = va;
            seg->p_paddr  = va;
            seg->p_filesz = filesz_add;
            seg->p_memsz  = vsz;
            seg->p_align  = page_size;
            cur_flags = f;
        }
        if (n_static_loads > 0) {
            seg1_filesz = static_loads[0].p_filesz;
            seg1_memsz  = static_loads[0].p_memsz;
        } else {
            seg1_filesz = ark_backend_align_up(code_file_end, page_size);
            seg1_memsz  = seg1_filesz;
        }
    } else {
        seg1_filesz = ark_backend_align_up(code_file_end, page_size);
        seg1_memsz  = seg1_filesz;

        uint64_t data_start = has_dynamic ? meta_secs[interp_shidx].offset : rw_start;
        seg2_offset = data_start;
        seg2_vaddr  = image_base + data_start;

        uint64_t data_end = file_size;
        if (data_end < data_start) data_end = data_start;
        rw_filesz = data_end - data_start;
        rw_memsz = rw_filesz;
    }

    symtab = (Elf64_Sym*)calloc(total_syms ? total_syms : 1, sizeof(Elf64_Sym));
    if (!symtab) goto oom;
    for (size_t i = 0; i < ns; i++) {
        Elf64_Sym* s = &symtab[1 + i];
        s->st_info  = ELF_ST_INFO(STB_LOCAL, STT_SECTION);
        s->st_other = 0;
        s->st_shndx = (uint16_t)(1 + i);
        s->st_value = SEC_VADDR(i);
        s->st_size  = SEC_VSIZE(i);
    }
    next_sym_idx = 1 + ns;
    NameIndexMap sym_map2 = {0};
    for (size_t i = 0; i < input->export_count; i++) {
        const char* n = input->exports[i].name;
        if (!n) continue;
        uint32_t idx;
        if (!nim_lookup(&sym_map2, n, &idx)) {
            idx = (uint32_t)next_sym_idx++;
            Elf64_Sym* s = &symtab[idx];
            s->st_info  = ELF_ST_INFO(STB_GLOBAL, input->exports[i].is_function ? STT_FUNC : STT_OBJECT);
            s->st_other = 0;
            if (input->exports[i].section_index >= 0 && input->exports[i].section_index <= ns) {
                s->st_shndx = (uint16_t)input->exports[i].section_index;
                s->st_value = SEC_VADDR(input->exports[i].section_index) + input->exports[i].offset;
                s->st_size  = 0;
            } else {
                s->st_shndx = SHN_ABS;
                s->st_value = input->exports[i].value;
                s->st_size  = 0;
            }
            nim_add(&sym_map2, n, idx);
        }
    }
    for (size_t i = 0; i < input->import_count; i++) {
        const char* sym = input->imports[i].symbol;
        if (!sym) continue;
        uint32_t idx;
        if (!nim_lookup(&sym_map2, sym, &idx)) {
            idx = (uint32_t)next_sym_idx++;
            Elf64_Sym* s = &symtab[idx];
            s->st_info  = ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            s->st_other = 0;
            s->st_shndx = SHN_UNDEF;
            s->st_value = 0;
            s->st_size  = 0;
            nim_add(&sym_map2, sym, idx);
        }
    }
    for (size_t i = 0; i < input->reloc_count; i++) {
        const ArkResolverSymbol* ss = input->relocs[i].symbol;
        if (!ss || !ss->name) continue;
        uint32_t idx;
        if (!nim_lookup(&sym_map2, ss->name, &idx)) {
            idx = (uint32_t)next_sym_idx++;
            Elf64_Sym* s = &symtab[idx];
            uint8_t bind = (ss->binding == ARK_BIND_WEAK) ? STB_WEAK : STB_GLOBAL;
            uint8_t type = STT_NOTYPE;
            s->st_info  = ELF_ST_INFO(bind, type);
            s->st_other = (ss->visibility == ARK_VISIBILITY_HIDDEN) ? 2 : 0;
            if (ss->section_index < ns) {
                s->st_shndx = (uint16_t)(1 + ss->section_index);
                s->st_value = SEC_VADDR(ss->section_index) + ss->value;
            } else {
                s->st_shndx = SHN_UNDEF;
                s->st_value = 0;
            }
            s->st_size  = ss->size;
            nim_add(&sym_map2, ss->name, idx);
        }
    }
    nim_free(&sym_map2);
    free(strtab.buffer->data);
    sb_init(&strtab);
    sb_add(&strtab, "");
    NameIndexMap name_off_map = {0};
    size_t next_global_idx = (size_t)(1 + ns);
    for (size_t i = 0; i < input->export_count; i++) {
        const char* n = input->exports[i].name;
        if (!n) continue;
        uint32_t lookup;
        if (nim_lookup(&name_off_map, n, &lookup)) { continue; }
        uint32_t off = sb_add(&strtab, n);
        if (off == (uint32_t)-1) goto oom;

        Elf64_Sym* s = &symtab[next_global_idx];
        s->st_name  = off;
        uint64_t export_sec_idx = input->exports[i].section_index;
#ifdef ARK_DEBUG
        if (layout && export_sec_idx < layout->section_count) {
        } else if (!layout) {
        } else {
        }
#endif
        uint64_t sec_vaddr = SEC_VADDR(export_sec_idx);
        s->st_value = sec_vaddr + input->exports[i].value;
        s->st_size  = 0;
        s->st_info  = ELF_ST_INFO(STB_GLOBAL,
                                  input->exports[i].is_function ? STT_FUNC : STT_OBJECT);
        s->st_other = 0;
        s->st_shndx = input->exports[i].section_index >= 0 ? (uint16_t)(input->exports[i].section_index + 1) : SHN_ABS;


        nim_add(&name_off_map, n, off);
        next_global_idx++;
    }
    for (size_t i = 0; i < input->import_count; i++) {
        const char* sym = input->imports[i].symbol;
        if (!sym) continue;
        uint32_t lookup;
        if (nim_lookup(&name_off_map, sym, &lookup)) { continue; }
        uint32_t off = sb_add(&strtab, sym);
        if (off == (uint32_t)-1) goto oom;
        symtab[next_global_idx].st_name = off;
        nim_add(&name_off_map, sym, off);
        next_global_idx++;
    }
    for (size_t i = 0; i < input->reloc_count; i++) {
        const ArkResolverSymbol* ss = input->relocs[i].symbol;
        if (!ss || !ss->name) continue;
        uint32_t lookup;
        if (nim_lookup(&name_off_map, ss->name, &lookup)) { continue; }
        uint32_t off = sb_add(&strtab, ss->name);
        if (off == (uint32_t)-1) goto oom;
        symtab[next_global_idx].st_name = off;
        nim_add(&name_off_map, ss->name, off);
        next_global_idx++;
    }
    nim_free(&name_off_map);
    rela_arrays = (Elf64_Rela**)calloc(rela_sections ? rela_sections : 1, sizeof(Elf64_Rela*));
    if (!rela_arrays) goto oom;
    rela_idx = 0;
    for (size_t i = 0; i < ns; i++) {
        if (relocs_per_sec[i] == 0) continue;
        rela_arrays[rela_idx] = (Elf64_Rela*)calloc(relocs_per_sec[i], sizeof(Elf64_Rela));
        if (!rela_arrays[rela_idx]) {
            for (size_t k = 0; k < rela_idx; k++) free(rela_arrays[k]);
            free(rela_arrays);
            goto oom;
        }
        size_t cur = 0;
        for (size_t j = 0; j < input->reloc_count; j++) {
            const ArkResolverReloc* r = &input->relocs[j];
            if (r->section_index != i) continue;
            uint32_t sym_idx = 0;
            if (r->symbol && r->symbol->name) {
                for (uint32_t k = 1 + (uint32_t)ns; k < total_syms; k++) {
                    if (symtab[k].st_name < strtab.buffer->size
                        && strcmp((const char*)strtab.buffer->data + symtab[k].st_name, r->symbol->name) == 0) {
                        sym_idx = k;
                        break;
                    }
                }
            }
            uint32_t r_type = 0;
            switch (r->type) {
                case 1: r_type = R_X86_64_64;      break;
                case 2: r_type = R_X86_64_32;      break;
                case 3: r_type = R_X86_64_PC32;    break;
                case 4: r_type = R_X86_64_GOTPC32; break;
                case 5: r_type = R_X86_64_32;      break;
                default: r_type = R_X86_64_64;     break;
            }
            if (r->symbol && r->symbol->section_index == 0 && r->symbol->import_module != NULL) {
                continue;
            }
            rela_arrays[rela_idx][cur].r_offset = SEC_VADDR(r->section_index) + r->offset;
            rela_arrays[rela_idx][cur].r_info   = ELF_R_INFO(sym_idx, r_type);
            rela_arrays[rela_idx][cur].r_addend = r->addend;
            cur++;
        }
        rela_idx++;
    }

    dynsym = NULL;
    memset(&dynstr, 0, sizeof(dynstr));
    dyntab = NULL;
    size_t dyntab_count = 0;

    #define FREE_RELA_ARRAYS() do { \
        for (size_t k = 0; k < rela_sections; k++) free(rela_arrays[k]); \
        free(rela_arrays); \
    } while (0)
    if (has_dynamic) {
        if (!sb_init(&dynstr)) { FREE_RELA_ARRAYS(); goto oom; }
        NameIndexMap module_off = {0};
        NameIndexMap symbol_off = {0};
        size_t unique_modules = 0;
        for (size_t i = 0; i < input->import_count; i++) {
            const char* sym = input->imports[i].symbol;
            const char* mod = input->imports[i].module;
            if (sym && !nim_lookup(&symbol_off, sym, NULL)) {
                uint32_t off = sb_add(&dynstr, sym);
                if (off == (uint32_t)-1) {
                    nim_free(&module_off); nim_free(&symbol_off); sb_free(&dynstr);
                    FREE_RELA_ARRAYS(); goto oom;
                }
                nim_add(&symbol_off, sym, off);
            }
            if (mod && !nim_lookup(&module_off, mod, NULL)) {
                uint32_t off = sb_add(&dynstr, mod);
                if (off == (uint32_t)-1) {
                    nim_free(&module_off); nim_free(&symbol_off); sb_free(&dynstr);
                    FREE_RELA_ARRAYS(); goto oom;
                }
                nim_add(&module_off, mod, off);
                unique_modules++;
            }
        }


        size_t export_sym_count = 0;
        for (size_t i = 0; i < input->export_count; i++) {
            if (input->exports[i].is_function) export_sym_count++;
        }

        size_t dynsym_n = 1 + input->import_count + export_sym_count;
        dynsym = (Elf64_Sym*)calloc(dynsym_n, sizeof(Elf64_Sym));
        if (!dynsym) { nim_free(&module_off); nim_free(&symbol_off); sb_free(&dynstr);
            FREE_RELA_ARRAYS(); goto oom; }

        size_t export_dynsym_idx = 1 + input->import_count;
        for (size_t i = 0; i < input->export_count; i++) {
            if (!input->exports[i].is_function) continue;

            const char* sym = input->exports[i].name;
            Elf64_Sym* s = &dynsym[export_dynsym_idx];
            s->st_info  = ELF_ST_INFO(STB_GLOBAL, STT_FUNC);
            s->st_other = 0;
            s->st_shndx = input->exports[i].section_index >= 0 ? (uint16_t)(input->exports[i].section_index + 1) : SHN_ABS;
            s->st_value = input->exports[i].offset;
            s->st_size  = 0;
            uint32_t name_off = 0;
            if (sym && !nim_lookup(&symbol_off, sym, &name_off)) {
                name_off = sb_add(&dynstr, sym);
                if (name_off != (uint32_t)-1) {
                    nim_add(&symbol_off, sym, name_off);
                }
            }
            s->st_name = name_off;
            export_dynsym_idx++;
        }

        for (size_t i = 0; i < input->import_count; i++) {
            const char* sym = input->imports[i].symbol;
            Elf64_Sym* s = &dynsym[1 + i];
            s->st_info  = ELF_ST_INFO(STB_GLOBAL, STT_NOTYPE);
            s->st_other = 0;
            s->st_shndx = SHN_UNDEF;
            s->st_value = 0;
            s->st_size  = 0;
            s->st_name  = (sym && nim_lookup(&symbol_off, sym, &s->st_name)) ? s->st_name : 0;
        }


        size_t rela_count_for_dyn = rela_sections;
        dyntab_count = 5 + unique_modules + 4 + (rela_count_for_dyn > 0 ? 3 : 0) + 1;
        dyntab = (Elf64_Dyn*)calloc(dyntab_count, sizeof(Elf64_Dyn));
        if (!dyntab) { free(dynsym); nim_free(&module_off); nim_free(&symbol_off); sb_free(&dynstr);
            FREE_RELA_ARRAYS(); goto oom; }
        size_t di = 0;
        dyntab[di].d_tag = DT_STRTAB; dyntab[di].d_val = META_VADDR(dynstr_shidx); di++;
        dyntab[di].d_tag = DT_SYMTAB; dyntab[di].d_val = META_VADDR(dynsym_shidx); di++;
        dyntab[di].d_tag = DT_STRSZ;  dyntab[di].d_val = 0; di++;
        dyntab[di].d_tag = DT_SYMENT; dyntab[di].d_val = sizeof(Elf64_Sym); di++;
        dyntab[di].d_tag = DT_HASH;   dyntab[di].d_val = META_VADDR(hash_shidx); di++;

        NameIndexMap module_emitted = {0};
        for (size_t i = 0; i < input->import_count; i++) {
            const char* mod = input->imports[i].module;
            if (!mod) continue;
            if (nim_lookup(&module_emitted, mod, NULL)) continue;
            uint32_t name_off = 0;
            if (!nim_lookup(&module_off, mod, &name_off)) continue;
            dyntab[di].d_tag = DT_NEEDED;
            dyntab[di].d_val = name_off;
            di++;
            nim_add(&module_emitted, mod, 1);
        }
        nim_free(&module_emitted);
        
        dyntab[di].d_tag = DT_INIT_ARRAY;    dyntab[di].d_val = META_VADDR(init_array_shidx); di++;
        dyntab[di].d_tag = DT_INIT_ARRAYSZ;  dyntab[di].d_val = 0; di++;

        dyntab[di].d_tag = DT_FINI_ARRAY;    dyntab[di].d_val = META_VADDR(fini_array_shidx); di++;
        dyntab[di].d_tag = DT_FINI_ARRAYSZ;  dyntab[di].d_val = 0; di++;
        
        if (rela_count_for_dyn > 0) {
            
            uint64_t first_rela_vaddr = 0;
            uint64_t total_rela_size = 0;
            rela_idx = 0;
            for (size_t i = 0; i < ns; i++) {
                if (relocs_per_sec[i] == 0) continue;
                if (first_rela_vaddr == 0) {
                    first_rela_vaddr = META_VADDR(rela_shidx_base + rela_idx);
                }
                total_rela_size += META_SIZE(rela_shidx_base + rela_idx);
                rela_idx++;
            }
            dyntab[di].d_tag = DT_RELA;      dyntab[di].d_val = first_rela_vaddr; di++;
            dyntab[di].d_tag = DT_RELASZ;    dyntab[di].d_val = total_rela_size; di++;
            dyntab[di].d_tag = DT_RELAENT;   dyntab[di].d_val = sizeof(Elf64_Rela); di++;
        }
        dyntab[di].d_tag = DT_NULL; dyntab[di].d_val = 0; di++;

        for (size_t i = 0; i < di && i < 15; i++) {
            const char* tag_name = "UNKNOWN";
            switch (dyntab[i].d_tag) {
                case 0: tag_name = "DT_NULL"; break;
                case 1: tag_name = "DT_NEEDED"; break;
                case 5: tag_name = "DT_STRTAB"; break;
                case 6: tag_name = "DT_SYMTAB"; break;
                case 10: tag_name = "DT_STRSZ"; break;
                case 11: tag_name = "DT_SYMENT"; break;
                case 25: tag_name = "DT_INIT_ARRAY"; break;
                case 26: tag_name = "DT_INIT_ARRAYSZ"; break;
                case 27: tag_name = "DT_FINI_ARRAY"; break;
                case 28: tag_name = "DT_FINI_ARRAYSZ"; break;
            }
            fprintf(stderr, "  [%zu] d_tag=%d (%s), d_val=0x%lx\n",
                    i, (int)dyntab[i].d_tag, tag_name, (unsigned long)dyntab[i].d_val);
        }

        
        meta_secs[dynsym_shidx].size  = (uint64_t)(dynsym_n * sizeof(Elf64_Sym));
        meta_secs[dynstr_shidx].size  = (uint64_t)dynstr.buffer->size;
        meta_secs[dynamic_shidx].size = (uint64_t)(dyntab_count * sizeof(Elf64_Dyn));

        fprintf(stderr, "  dynsym_shidx=%zu, size=%lu\n", dynsym_shidx, (unsigned long)meta_secs[dynsym_shidx].size);
        fprintf(stderr, "  dynstr_shidx=%zu, size=%lu\n", dynstr_shidx, (unsigned long)meta_secs[dynstr_shidx].size);
        fprintf(stderr, "  dynamic_shidx=%zu, size=%lu\n", dynamic_shidx, (unsigned long)meta_secs[dynamic_shidx].size);
        fprintf(stderr, "  init_array_shidx=%zu, size=%lu\n", init_array_shidx, (unsigned long)meta_secs[init_array_shidx].size);
        fprintf(stderr, "  fini_array_shidx=%zu, size=%lu\n", fini_array_shidx, (unsigned long)meta_secs[fini_array_shidx].size);
        
        for (size_t i = 0; i < dyntab_count; i++) {
            if (dyntab[i].d_tag == DT_STRSZ) { dyntab[i].d_val = dynstr.buffer->size; break; }
        }


        size_t func_import_count = 0;
        for (size_t i = 0; i < input->import_count; i++) {
            if (input->imports[i].is_function) {
                func_import_count++;
            }
        }

        
        size_t jump_slot_count = 0;    
        size_t glob_dat_count = 0;     
        size_t abs64_import_count = 0;

        for (size_t i = 0; i < input->reloc_count; i++) {
            const ArkResolverReloc* r = &input->relocs[i];
            if (!r->symbol || r->symbol->section_index != 0) continue;


            int is_func = 0;
            for (size_t j = 0; j < input->import_count; j++) {
                if (input->imports[j].is_function &&
                    strcmp(input->imports[j].symbol, r->symbol->name) == 0) {
                    is_func = 1;
                    break;
                }
            }

            if (is_func && func_import_count > 0) {
                jump_slot_count++;
            } else if (!is_func) {
                glob_dat_count++;    
            } else {
                abs64_import_count++;
            }
        }

        
        rela_dyn_count = glob_dat_count + abs64_import_count;
        rela_plt_count = jump_slot_count;



        Elf64_Rela* rela_plt_data = NULL;
        if (rela_dyn_count > 0) {
            meta_secs[reladyn_shidx].size = (uint64_t)(rela_dyn_count * sizeof(Elf64_Rela));
            rela_dyn_data = (Elf64_Rela*)calloc(rela_dyn_count, sizeof(Elf64_Rela));
            if (!rela_dyn_data) { free(dynsym); free(dyntab); sb_free(&dynstr); FREE_RELA_ARRAYS(); goto oom; }
        } else {
            meta_secs[reladyn_shidx].size = 0;
        }

        
        if (rela_plt_count > 0) {
            rela_plt_data = (Elf64_Rela*)calloc(rela_plt_count, sizeof(Elf64_Rela));
            if (!rela_plt_data) { free(rela_dyn_data); free(dynsym); free(dyntab); sb_free(&dynstr); FREE_RELA_ARRAYS(); goto oom; }
        }


        NameIndexMap dynsym_idx_map = {0};
        NameIndexMap plt_idx_map = {0};
        size_t current_plt_idx = 1;

        for (size_t i = 0; i < input->import_count; i++) {
            const char* sym = input->imports[i].symbol;
            nim_add(&dynsym_idx_map, sym, (uint32_t)(1 + i));

            
            if (input->imports[i].is_function) {
                nim_add(&plt_idx_map, sym, (uint32_t)current_plt_idx++);
            }
        }

        size_t dr_idx = 0;
        size_t pr_idx = 0;

        for (size_t i = 0; i < input->reloc_count; i++) {
            const ArkResolverReloc* r = &input->relocs[i];
            if (!r->symbol || r->symbol->section_index != 0) continue;

            const char* sym_name = r->symbol->name;
            uint32_t dyn_sym_idx = 0;
            if (!nim_lookup(&dynsym_idx_map, sym_name, &dyn_sym_idx)) {
                continue;
            }

            uint32_t plt_entry = 0;
            int is_func_with_plt = (nim_lookup(&plt_idx_map, sym_name, &plt_entry) && func_import_count > 0);

            if (is_func_with_plt) {
                uint64_t gotplt_entry_vaddr = META_VADDR(gotplt_shidx) + (3 + (plt_entry - 1)) * sizeof(uint64_t);

                rela_plt_data[pr_idx].r_offset = gotplt_entry_vaddr;
                rela_plt_data[pr_idx].r_info   = ELF_R_INFO(dyn_sym_idx, R_X86_64_JUMP_SLOT);
                rela_plt_data[pr_idx].r_addend = 0;
                pr_idx++;

            } else {
                uint64_t target_vaddr = SEC_VADDR(r->section_index) + r->offset;

                if (glob_dat_count > 0) {
                    rela_dyn_data[dr_idx].r_offset = target_vaddr;
                    rela_dyn_data[dr_idx].r_info   = ELF_R_INFO(dyn_sym_idx, R_X86_64_GLOB_DAT);
                    rela_dyn_data[dr_idx].r_addend = 0;
                } else {
                    rela_dyn_data[dr_idx].r_offset = target_vaddr;
                    rela_dyn_data[dr_idx].r_info   = ELF_R_INFO(dyn_sym_idx, R_X86_64_64);
                    rela_dyn_data[dr_idx].r_addend = r->addend;
                }
                dr_idx++;
            }
        }
        nim_free(&dynsym_idx_map);
        nim_free(&plt_idx_map);

        for (size_t i = 0; i < dyntab_count; i++) {
            if (dyntab[i].d_tag == DT_RELA) {
                dyntab[i].d_val = META_VADDR(reladyn_shidx);
            } else if (dyntab[i].d_tag == DT_RELASZ) {
                dyntab[i].d_val = META_SIZE(reladyn_shidx);
            }
        }

        if (func_import_count > 0 && rela_plt_count > 0) {
            size_t null_idx = 0;
            for (size_t i = 0; i < dyntab_count; i++) {
                if (dyntab[i].d_tag == DT_NULL) {
                    null_idx = i;
                    break;
                }
            }

            if (null_idx >= 5) {
                dyntab[null_idx - 5].d_tag = DT_JMPREL;
                dyntab[null_idx - 5].d_val = META_VADDR(reladyn_shidx) + META_SIZE(reladyn_shidx);
                if (rela_plt_count == 0 || rela_plt_count > UINT32_MAX / sizeof(Elf64_Rela)) {
                    rela_plt_count = func_import_count > 0 ? func_import_count : 0;
                }
                dyntab[null_idx - 4].d_tag = DT_PLTRELSZ;
                dyntab[null_idx - 4].d_val = (uint64_t)(rela_plt_count * sizeof(Elf64_Rela));
                dyntab[null_idx - 3].d_tag = DT_PLTGOT;
                dyntab[null_idx - 3].d_val = META_VADDR(gotplt_shidx);
                dyntab[null_idx - 2].d_tag = DT_PLTREL;
                dyntab[null_idx - 2].d_val = sizeof(Elf64_Rela);

                int use_lazy_binding = 1;
                if (!use_lazy_binding) {
                    dyntab[null_idx - 1].d_tag = DT_BIND_NOW;
                    dyntab[null_idx - 1].d_val = 0;

                } else {
                }


                (void)META_OFFSET(reladyn_shidx);
            }
        }

        nim_free(&module_off);
        nim_free(&symbol_off);
    }


    uint8_t* out_buf = (uint8_t*)calloc(1, file_size);
    if (!out_buf) {
        FREE_RELA_ARRAYS();
        if (has_dynamic) { free(dynsym); free(dyntab); sb_free(&dynstr); }
        goto oom;
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

    if (ark_backend_should_use_dynamic_elf(input)) {
        ehdr.e_type = ET_DYN;
    } else {
        ehdr.e_type = ET_EXEC;
    }
    ehdr.e_machine   = EM_X86_64;
    ehdr.e_version   = EV_CURRENT;
    ehdr.e_entry     = image_base;
    if (input->entry_section >= 0 && input->entry_section <= ns) {
        ehdr.e_entry = SEC_VADDR(input->entry_section) + input->entry_offset;
    } else {
        // 留空,别动
    }
    ehdr.e_phoff     = sizeof(Elf64_Ehdr);
    ehdr.e_shoff     = shdr_offset;
    ehdr.e_flags     = 0;
    ehdr.e_ehsize    = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);

    int has_relro = has_dynamic && func_import_count > 0;
    int needs_gnu_stack = 1;  
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum     = (uint16_t)elf_shnum;
    ehdr.e_shstrndx  = (uint16_t)shstrtab_shidx;
    if (!has_dynamic && !has_tls) {
        for (int li = 0; li < n_static_loads; li++) {
            memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_load_count * sizeof(Elf64_Phdr),
                   &static_loads[li], sizeof(Elf64_Phdr));
            phdr_load_count++;
        }
    } else {
        Elf64_Phdr phdr0 = {0};
        phdr0.p_type   = PT_LOAD;
        phdr0.p_flags  = PF_R | PF_X;
        phdr0.p_offset = 0;
        phdr0.p_vaddr  = image_base;
        phdr0.p_paddr  = image_base;
        phdr0.p_filesz = seg1_filesz;
        phdr0.p_memsz  = seg1_memsz;
        phdr0.p_align  = page_size;
        memcpy(out_buf + sizeof(Elf64_Ehdr), &phdr0, sizeof(phdr0));
        phdr_load_count++;

        Elf64_Phdr phdr1 = {0};
        phdr1.p_type   = PT_LOAD;
        phdr1.p_flags  = PF_R | PF_W;
        phdr1.p_offset = seg2_offset;
        phdr1.p_vaddr  = seg2_vaddr;
        phdr1.p_paddr  = seg2_vaddr;
        phdr1.p_filesz = rw_filesz;
        phdr1.p_memsz  = rw_memsz;
        phdr1.p_align  = page_size;
        memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_load_count * sizeof(Elf64_Phdr), &phdr1, sizeof(phdr1));
        phdr_load_count++;
    }

    ehdr.e_phnum   = (uint16_t)(phdr_load_count + (has_tls ? 1 : 0) + (has_relro ? 1 : 0) + (needs_gnu_stack ? 1 : 0));
    memcpy(out_buf, &ehdr, sizeof(ehdr));

    Elf64_Phdr phdr2 = {0}, phdr3 = {0}, phdr_tls = {0};
    size_t phdr_write_idx = phdr_load_count;
    if (has_dynamic) {
        phdr2.p_type   = PT_INTERP;
        phdr2.p_offset = META_OFFSET(interp_shidx);
        phdr2.p_vaddr  = META_VADDR(interp_shidx);
        phdr2.p_paddr  = META_VADDR(interp_shidx);
        phdr2.p_filesz = META_SIZE(interp_shidx);
        phdr2.p_memsz  = META_SIZE(interp_shidx);
        phdr2.p_align  = 1;
        memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr2, sizeof(phdr2));
        phdr_write_idx++;

        phdr3.p_type   = PT_DYNAMIC;
        phdr3.p_offset = META_OFFSET(dynamic_shidx);
        phdr3.p_vaddr  = META_VADDR(dynamic_shidx);
        phdr3.p_paddr  = META_VADDR(dynamic_shidx);
        phdr3.p_filesz = META_SIZE(dynamic_shidx);
        phdr3.p_memsz  = META_SIZE(dynamic_shidx);
        phdr3.p_align  = sizeof(uint64_t);
        memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr), &phdr3, sizeof(phdr3));
        phdr_write_idx++;
    }
    if (has_tls) {
        phdr_tls.p_type   = PT_TLS;
        phdr_tls.p_offset = tls_offset;
        phdr_tls.p_vaddr  = tls_vaddr;
        phdr_tls.p_paddr  = tls_vaddr;
        phdr_tls.p_filesz = tls_filesz;
        phdr_tls.p_memsz  = tls_memsz;
        phdr_tls.p_flags  = PF_R | PF_W;
        phdr_tls.p_align  = tls_align;
        memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr),
               &phdr_tls, sizeof(phdr_tls));
        phdr_write_idx++;
    }

    if (has_relro) {
        Elf64_Phdr phdr_relro = {0};
        phdr_relro.p_type   = PT_GNU_RELRO;
        phdr_relro.p_flags  = PF_R;

        uint64_t relro_start = META_VADDR(gotplt_shidx);
        uint64_t relro_end   = META_VADDR(dynamic_shidx) + META_SIZE(dynamic_shidx);

        phdr_relro.p_offset = META_OFFSET(gotplt_shidx);
        phdr_relro.p_vaddr  = relro_start;
        phdr_relro.p_paddr  = relro_start;
        phdr_relro.p_filesz = (uint64_t)(relro_end - relro_start);
        phdr_relro.p_memsz  = (uint64_t)(relro_end - relro_start);
        phdr_relro.p_align  = page_size;

        memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr),
               &phdr_relro, sizeof(phdr_relro));

    }

    if (needs_gnu_stack) {
        Elf64_Phdr phdr_stack = {0};
        phdr_stack.p_type   = PT_GNU_STACK;
        phdr_stack.p_flags  = PF_R | PF_W;  // RW: readable writable, non-executable stack
        phdr_stack.p_offset = 0;
        phdr_stack.p_vaddr  = 0;
        phdr_stack.p_paddr  = 0;
        phdr_stack.p_filesz = 0;
        phdr_stack.p_memsz  = 0;
        phdr_stack.p_align  = 16;

        memcpy(out_buf + sizeof(Elf64_Ehdr) + phdr_write_idx * sizeof(Elf64_Phdr),
               &phdr_stack, sizeof(phdr_stack));

    }

    for (size_t i = 0; i < ns; i++) {
        if (is_bss_kind((ArkSectionKind)input->sections[i].kind)) continue;
        if (input->sections[i].data && input->sections[i].size > 0) {
            memcpy(out_buf + SEC_OFFSET(i), input->sections[i].data, input->sections[i].size);
        }
    }
    rela_idx = 0;
    for (size_t i = 0; i < ns; i++) {
        if (relocs_per_sec[i] == 0) continue;
        if (rela_arrays[rela_idx]) {
            memcpy(out_buf + META_OFFSET(rela_shidx_base + rela_idx),
                   rela_arrays[rela_idx],
                   relocs_per_sec[i] * sizeof(Elf64_Rela));
        }
        free(rela_arrays[rela_idx]);
        rela_idx++;
    }
    free(rela_arrays);
    memcpy(out_buf + META_OFFSET(symtab_shidx), symtab, total_syms * sizeof(Elf64_Sym));
    memcpy(out_buf + META_OFFSET(strtab_shidx), strtab.buffer->data, strtab.buffer->size);
    memcpy(out_buf + META_OFFSET(shstrtab_shidx), shstrtab.buffer->data, shstrtab.buffer->size);
    if (has_dynamic) {
        static const char interp_path[] = "/lib64/ld-linux-x86-64.so.2";
        fprintf(stderr, "  META_OFFSET(interp_shidx)=0x%lx\n", (unsigned long)META_OFFSET(interp_shidx));
        fprintf(stderr, "  META_OFFSET(dynsym_shidx)=0x%lx\n", (unsigned long)META_OFFSET(dynsym_shidx));
        fprintf(stderr, "  META_OFFSET(dynstr_shidx)=0x%lx\n", (unsigned long)META_OFFSET(dynstr_shidx));
        fprintf(stderr, "  META_OFFSET(dynamic_shidx)=0x%lx\n", (unsigned long)META_OFFSET(dynamic_shidx));
        fprintf(stderr, "  META_SIZE(dynamic_shidx)=%lu (should be %zu * %lu = %lu)\n",
                (unsigned long)META_SIZE(dynamic_shidx),
                dyntab_count, sizeof(Elf64_Dyn),
                (unsigned long)(dyntab_count * sizeof(Elf64_Dyn)));

        memcpy(out_buf + META_OFFSET(interp_shidx), interp_path, sizeof(interp_path));
        memcpy(out_buf + META_OFFSET(dynsym_shidx), dynsym, META_SIZE(dynsym_shidx));
        memcpy(out_buf + META_OFFSET(dynstr_shidx), dynstr.buffer->data, META_SIZE(dynstr_shidx));

        if (META_OFFSET(hash_shidx) > 0 && META_SIZE(hash_shidx) > 0) {
            size_t hash_nbuckets = 1;
            size_t hash_nchain = 1 + input->import_count + 1;
            uint32_t* hash_data = (uint32_t*)(out_buf + META_OFFSET(hash_shidx));
            hash_data[0] = (uint32_t)hash_nbuckets;
            hash_data[1] = (uint32_t)hash_nchain;
            hash_data[2] = 0;
            for (size_t i = 0; i < hash_nchain; i++) {
                hash_data[3 + i] = (uint32_t)((i + 1) % hash_nchain);
            }
        }

        fprintf(stderr, "  Destination: out_buf + 0x%lx\n", (unsigned long)META_OFFSET(dynamic_shidx));
        fprintf(stderr, "  Source size: %lu bytes\n", (unsigned long)META_SIZE(dynamic_shidx));

        memcpy(out_buf + META_OFFSET(dynamic_shidx), dyntab, META_SIZE(dynamic_shidx));

        Elf64_Dyn* written_dyn = (Elf64_Dyn*)(out_buf + META_OFFSET(dynamic_shidx));
        for (size_t i = 0; i < 10 && i < dyntab_count; i++) {
            const char* tag_name = "UNKNOWN";
            switch (written_dyn[i].d_tag) {
                case 0: tag_name = "DT_NULL"; break;
                case 1: tag_name = "DT_NEEDED"; break;
                case 5: tag_name = "DT_STRTAB"; break;
                case 6: tag_name = "DT_SYMTAB"; break;
                case 10: tag_name = "DT_STRSZ"; break;
                case 11: tag_name = "DT_SYMENT"; break;
                case 25: tag_name = "DT_INIT_ARRAY"; break;
                case 26: tag_name = "DT_INIT_ARRAYSZ"; break;
                case 27: tag_name = "DT_FINI_ARRAY"; break;
                case 28: tag_name = "DT_FINI_ARRAYSZ"; break;
            }
            fprintf(stderr, "  [%zu] d_tag=%d (%s), d_val=0x%lx\n",
                    i, (int)written_dyn[i].d_tag, tag_name, (unsigned long)written_dyn[i].d_val);
        }
        if (rela_dyn_data && rela_dyn_count > 0) {
            memcpy(out_buf + META_OFFSET(reladyn_shidx), rela_dyn_data, META_SIZE(reladyn_shidx));
            free(rela_dyn_data);
            rela_dyn_data = NULL;
        }

        if (rela_plt_data && rela_plt_count > 0) {
            uint64_t rela_plt_offset = META_OFFSET(reladyn_shidx) + META_SIZE(reladyn_shidx);
            memcpy(out_buf + rela_plt_offset, rela_plt_data, rela_plt_count * sizeof(Elf64_Rela));
            free(rela_plt_data);
            rela_plt_data = NULL;
        }

        if (func_import_count > 0 && META_SIZE(plt_shidx) > 0 && META_SIZE(gotplt_shidx) > 0) {
            fprintf(stderr, "  META_OFFSET(plt_shidx)=0x%lx\n", (unsigned long)META_OFFSET(plt_shidx));
            fprintf(stderr, "  META_OFFSET(gotplt_shidx)=0x%lx\n", (unsigned long)META_OFFSET(gotplt_shidx));
            fprintf(stderr, "  file_size=0x%lx\n", (unsigned long)file_size);

            if (META_OFFSET(plt_shidx) == 0 || META_OFFSET(gotplt_shidx) == 0) {
                func_import_count = 0;
            } else {
                uint8_t* plt_base = out_buf + META_OFFSET(plt_shidx);
            uint64_t plt_vaddr = META_VADDR(plt_shidx);
            uint64_t gotplt_vaddr = META_VADDR(gotplt_shidx);

            size_t expected_plt_size = (1 + func_import_count) * 16;
            if (META_SIZE(plt_shidx) < expected_plt_size) {
                func_import_count = (META_SIZE(plt_shidx) / 16) - 1;
            }
            if (META_SIZE(gotplt_shidx) < (3 + func_import_count) * sizeof(uint64_t)) {
            }

            plt_base[0]  = 0xff; plt_base[1]  = 0x35;
            int32_t disp_to_got1 = (int32_t)((gotplt_vaddr + 1 * sizeof(uint64_t)) - (plt_vaddr + 6));
            memcpy(plt_base + 2, &disp_to_got1, 4);

            plt_base[6]  = 0xff; plt_base[7]  = 0x25;
            int32_t disp_to_got2 = (int32_t)((gotplt_vaddr + 2 * sizeof(uint64_t)) - (plt_vaddr + 12));
            memcpy(plt_base + 8, &disp_to_got2, 4);

            plt_base[12] = 0x0f; plt_base[13] = 0x1f;
            plt_base[14] = 0x40; plt_base[15] = 0x00;

            for (size_t i = 0; i < func_import_count; i++) {
                uint8_t* entry = plt_base + (1 + i) * 16;
                uint64_t entry_vaddr = plt_vaddr + (1 + i) * 16;
                uint64_t got_entry_vaddr = gotplt_vaddr + (3 + i) * sizeof(uint64_t);

                entry[0] = 0xff; entry[1] = 0x25;
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


        if (func_import_count > 0 && META_SIZE(gotplt_shidx) > 0) {
            uint8_t* gotplt_base = out_buf + META_OFFSET(gotplt_shidx);
            uint64_t plt_vaddr = META_VADDR(plt_shidx);


            size_t required_got_entries = 3 + func_import_count;
            if (META_SIZE(gotplt_shidx) < required_got_entries * sizeof(uint64_t)) {
                func_import_count = (META_SIZE(gotplt_shidx) / sizeof(uint64_t)) - 3;
                if ((int64_t)func_import_count < 0) func_import_count = 0;
            }


            uint64_t dynamic_addr = META_VADDR(dynamic_shidx);
            memcpy(gotplt_base + 0 * sizeof(uint64_t), &dynamic_addr, sizeof(uint64_t));


            uint64_t zero = 0;
            memcpy(gotplt_base + 1 * sizeof(uint64_t), &zero, sizeof(uint64_t));


            memcpy(gotplt_base + 2 * sizeof(uint64_t), &zero, sizeof(uint64_t));


            for (size_t i = 0; i < func_import_count; i++) {

                uint64_t push_instr_addr = plt_vaddr + (1 + i) * 16 + 6;
                memcpy(gotplt_base + (3 + i) * sizeof(uint64_t), &push_instr_addr, sizeof(uint64_t));

            }
        }
    }


    Elf64_Shdr* shdrs = (Elf64_Shdr*)calloc(elf_shnum, sizeof(Elf64_Shdr));
    if (!shdrs) { free(out_buf); goto oom; }


    for (size_t i = 0; i < ns; i++) {
        Elf64_Shdr* sh = &shdrs[1 + i];
        const ArkSectionLayout* sl = ark_layout_get_section(layout, i);
        sh->sh_name      = sec_name_off[1 + i];
        sh->sh_type      = is_bss_kind((ArkSectionKind)input->sections[i].kind) ? SHT_NOBITS
                                                                            : kind_to_sh_type();
        sh->sh_flags     = kind_to_sh_flags((ArkSectionKind)input->sections[i].kind, input->sections[i].flags);
        sh->sh_addr      = SEC_VADDR(i);
        sh->sh_offset    = SEC_OFFSET(i);
        sh->sh_size      = SEC_VSIZE(i);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = sl ? sl->alignment : input->sections[i].alignment;
        sh->sh_entsize   = 0;
    }

    rela_idx = 0;
    for (size_t i = 0; i < ns; i++) {
        if (relocs_per_sec[i] == 0) continue;
        Elf64_Shdr* sh = &shdrs[rela_shidx_base + rela_idx];
        sh->sh_name      = sec_name_off[rela_shidx_base + rela_idx];
        sh->sh_type      = SHT_RELA;
        sh->sh_flags     = 0;
        sh->sh_addr      = META_VADDR(rela_shidx_base + rela_idx);
        sh->sh_offset    = META_OFFSET(rela_shidx_base + rela_idx);
        sh->sh_size      = META_SIZE(rela_shidx_base + rela_idx);
        sh->sh_link      = (uint32_t)symtab_shidx;
        sh->sh_info      = (uint32_t)(1 + i);
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(Elf64_Rela);
        rela_idx++;
    }

    {
        Elf64_Shdr* sh = &shdrs[symtab_shidx];
        sh->sh_name      = sec_name_off[symtab_shidx];
        sh->sh_type      = SHT_SYMTAB;
        sh->sh_flags     = 0;
        sh->sh_addr      = META_VADDR(symtab_shidx);
        sh->sh_offset    = META_OFFSET(symtab_shidx);
        sh->sh_size      = (uint64_t)(total_syms * sizeof(Elf64_Sym));
        sh->sh_link      = (uint32_t)strtab_shidx;
        sh->sh_info      = (uint32_t)(1 + ns);
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(Elf64_Sym);
    }

    {
        Elf64_Shdr* sh = &shdrs[strtab_shidx];
        sh->sh_name      = sec_name_off[strtab_shidx];
        sh->sh_type      = SHT_STRTAB;
        sh->sh_flags     = 0;
        sh->sh_addr      = META_VADDR(strtab_shidx);
        sh->sh_offset    = META_OFFSET(strtab_shidx);
        sh->sh_size      = strtab.buffer->size;
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize   = 0;
    }
    {
        Elf64_Shdr* sh = &shdrs[shstrtab_shidx];
        sh->sh_name      = sec_name_off[shstrtab_shidx];
        sh->sh_type      = SHT_STRTAB;
        sh->sh_flags     = 0;
        sh->sh_addr      = META_VADDR(shstrtab_shidx);
        sh->sh_offset    = META_OFFSET(shstrtab_shidx);
        sh->sh_size      = shstrtab.buffer->size;
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize   = 0;
    }
    if (has_dynamic) {
        Elf64_Shdr* sh;
        sh = &shdrs[interp_shidx];
        sh->sh_name      = sec_name_off[interp_shidx];
        sh->sh_type      = SHT_PROGBITS;
        sh->sh_flags     = SHF_ALLOC;
        sh->sh_addr      = META_VADDR(interp_shidx);
        sh->sh_offset    = META_OFFSET(interp_shidx);
        sh->sh_size      = META_SIZE(interp_shidx);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize   = 0;

        sh = &shdrs[dynsym_shidx];
        sh->sh_name      = sec_name_off[dynsym_shidx];
        sh->sh_type      = SHT_DYNSYM;
        sh->sh_flags     = SHF_ALLOC;
        sh->sh_addr      = META_VADDR(dynsym_shidx);
        sh->sh_offset    = META_OFFSET(dynsym_shidx);
        sh->sh_size      = META_SIZE(dynsym_shidx);
        sh->sh_link      = (uint32_t)dynstr_shidx;
        sh->sh_info      = 1;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(Elf64_Sym);

        sh = &shdrs[dynstr_shidx];
        sh->sh_name      = sec_name_off[dynstr_shidx];
        sh->sh_type      = SHT_STRTAB;
        sh->sh_flags     = SHF_ALLOC;
        sh->sh_addr      = META_VADDR(dynstr_shidx);
        sh->sh_offset    = META_OFFSET(dynstr_shidx);
        sh->sh_size      = META_SIZE(dynstr_shidx);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = 1;
        sh->sh_entsize   = 0;

        sh = &shdrs[dynamic_shidx];
        sh->sh_name      = sec_name_off[dynamic_shidx];
        sh->sh_type      = SHT_DYNAMIC;
        sh->sh_flags     = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr      = META_VADDR(dynamic_shidx);
        sh->sh_offset    = META_OFFSET(dynamic_shidx);
        sh->sh_size      = META_SIZE(dynamic_shidx);
        sh->sh_link      = (uint32_t)dynstr_shidx;
        sh->sh_info      = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(Elf64_Dyn);

        sh = &shdrs[init_array_shidx];
        sh->sh_name      = sec_name_off[init_array_shidx];
        sh->sh_type      = SHT_INIT_ARRAY;
        sh->sh_flags     = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr      = META_VADDR(init_array_shidx);
        sh->sh_offset    = META_OFFSET(init_array_shidx);
        sh->sh_size      = META_SIZE(init_array_shidx);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(uint64_t);

        sh = &shdrs[fini_array_shidx];
        sh->sh_name      = sec_name_off[fini_array_shidx];
        sh->sh_type      = SHT_FINI_ARRAY;
        sh->sh_flags     = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr      = META_VADDR(fini_array_shidx);
        sh->sh_offset    = META_OFFSET(fini_array_shidx);
        sh->sh_size      = META_SIZE(fini_array_shidx);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(uint64_t);

        sh = &shdrs[reladyn_shidx];
        sh->sh_name      = sec_name_off[reladyn_shidx];
        sh->sh_type      = SHT_RELA;
        sh->sh_flags     = SHF_ALLOC;
        sh->sh_addr      = META_VADDR(reladyn_shidx);
        sh->sh_offset    = META_OFFSET(reladyn_shidx);
        sh->sh_size      = META_SIZE(reladyn_shidx);
        sh->sh_link      = (uint32_t)dynsym_shidx;
        sh->sh_info      = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(Elf64_Rela);

        sh = &shdrs[plt_shidx];
        sh->sh_name      = sec_name_off[plt_shidx];
        sh->sh_type      = SHT_PROGBITS;
        sh->sh_flags     = SHF_ALLOC | SHF_EXECINSTR;
        sh->sh_addr      = META_VADDR(plt_shidx);
        sh->sh_offset    = META_OFFSET(plt_shidx);
        sh->sh_size      = META_SIZE(plt_shidx);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = 16;
        sh->sh_entsize   = 16;

        sh = &shdrs[gotplt_shidx];
        sh->sh_name      = sec_name_off[gotplt_shidx];
        sh->sh_type      = SHT_PROGBITS;
        sh->sh_flags     = SHF_ALLOC | SHF_WRITE;
        sh->sh_addr      = META_VADDR(gotplt_shidx);
        sh->sh_offset    = META_OFFSET(gotplt_shidx);
        sh->sh_size      = META_SIZE(gotplt_shidx);
        sh->sh_link      = 0;
        sh->sh_info      = 0;
        sh->sh_addralign = sizeof(uint64_t);
        sh->sh_entsize   = sizeof(uint64_t);
    }
    memcpy(out_buf + shdr_offset, shdrs, elf_shnum * sizeof(Elf64_Shdr));
    free(shdrs);

    if (ns > 0) {
        ArkSectionRvaMap* maps = (ArkSectionRvaMap*)calloc(ns, sizeof(ArkSectionRvaMap));
        if (!maps) { free(out_buf); goto oom; }
        for (size_t i = 0; i < ns; i++) {
            maps[i].rva        = (uint32_t)(SEC_VADDR(i) - image_base);
            maps[i].size       = (uint32_t)SEC_VSIZE(i);
            maps[i].file_offset = (uint32_t)SEC_OFFSET(i);
            maps[i].flags      = input->sections[i].flags;
        }
        output->section_maps = maps;
        output->section_count = ns;
    }
    output->data        = out_buf;
    output->size        = file_size;
    output->image_base  = image_base;

    if (has_dynamic) {
        Elf64_Dyn* final_dyn = (Elf64_Dyn*)(out_buf + META_OFFSET(dynamic_shidx));
        for (size_t i = 0; i < 10 && i < dyntab_count; i++) {
            const char* tag_name = "UNKNOWN";
            switch (final_dyn[i].d_tag) {
                case 0: tag_name = "DT_NULL"; break;
                case 1: tag_name = "DT_NEEDED"; break;
                case 5: tag_name = "DT_STRTAB"; break;
                case 6: tag_name = "DT_SYMTAB"; break;
                case 10: tag_name = "DT_STRSZ"; break;
                case 11: tag_name = "DT_SYMENT"; break;
                case 25: tag_name = "DT_INIT_ARRAY"; break;
                case 26: tag_name = "DT_INIT_ARRAYSZ"; break;
                case 27: tag_name = "DT_FINI_ARRAY"; break;
                case 28: tag_name = "DT_FINI_ARRAYSZ"; break;
            }
            fprintf(stderr, "  [%zu] d_tag=%d (%s), d_val=0x%lx\n",
                    i, (int)final_dyn[i].d_tag, tag_name, (unsigned long)final_dyn[i].d_val);
        }
    }

    if (has_dynamic) { free(dynsym); free(dyntab); sb_free(&dynstr); }
    free(symtab);
    free(meta_secs);
    free(sec_name_off);
    free(relocs_per_sec);
    sb_free(&strtab);
    sb_free(&shstrtab);
    ark_layout_destroy(layout);
    return ARK_LINK_OK;

oom:
    if (has_dynamic) { free(dynsym); free(dyntab); sb_free(&dynstr); free(rela_dyn_data); }
    free(symtab);
    free(meta_secs);
    free(sec_name_off);
    free(relocs_per_sec);
    sb_free(&strtab);
    sb_free(&shstrtab);
    ark_layout_destroy(layout);
    return ARK_LINK_ERR_MEMORY;
}