#include "ArkLink/Loader.h"
#include <stdint.h>
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
} Elf64_Ehdr_Loader;

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
} Elf64_Shdr_Loader;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym_Loader;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela_Loader;

#pragma pack(pop)

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1

#define ET_REL 1
#define EM_X86_64 62

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8

#define SHN_UNDEF 0
#define SHN_ABS 0xfff1

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_PLT32 4
#define R_X86_64_GOTPC32 29
#define R_X86_64_REX_GOTP 42

#define ELF64_ST_BIND(i) ((uint8_t)((i) >> 4))
#define ELF64_ST_TYPE(i) ((uint8_t)((i) & 0xf))
#define ELF64_R_SYM(i) ((uint32_t)((i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)((i) & 0xffffffff))

typedef struct {
    uint8_t* file_data;
    size_t file_size;
    Elf64_Ehdr_Loader* ehdr;
    Elf64_Shdr_Loader* shdrs;
    uint32_t* elf_sec_to_local;
    char* shstrtab;
    size_t shstrtab_size;
} ElfContext;

static int is_elf(const uint8_t* data, size_t size) {
    if (size < 4) {
        return 0;
    }
    return data[0] == ELFMAG0 && data[1] == ELFMAG1 && data[2] == ELFMAG2 && data[3] == ELFMAG3;
}

static ElfContext* elf_context_create(const uint8_t* data, size_t size) {
    ElfContext* ctx = (ElfContext*)calloc(1, sizeof(ElfContext));
    if (!ctx) {
        return NULL;
    }

    ctx->file_data = (uint8_t*)malloc(size);
    if (!ctx->file_data) {
        free(ctx);
        return NULL;
    }
    memcpy(ctx->file_data, data, size);
    ctx->file_size = size;

    ctx->ehdr = (Elf64_Ehdr_Loader*)ctx->file_data;
    if (ctx->ehdr->e_shoff + (uint64_t)ctx->ehdr->e_shnum * sizeof(Elf64_Shdr_Loader) > size) {
        free(ctx->file_data);
        free(ctx);
        return NULL;
    }

    ctx->shdrs = (Elf64_Shdr_Loader*)(ctx->file_data + ctx->ehdr->e_shoff);

    if (ctx->ehdr->e_shstrndx != 0 && ctx->ehdr->e_shstrndx < ctx->ehdr->e_shnum) {
        Elf64_Shdr_Loader* shstr_shdr = &ctx->shdrs[ctx->ehdr->e_shstrndx];
        if (shstr_shdr->sh_offset + shstr_shdr->sh_size <= size) {
            ctx->shstrtab = (char*)(ctx->file_data + shstr_shdr->sh_offset);
            ctx->shstrtab_size = (size_t)shstr_shdr->sh_size;
        }
    }

    ctx->elf_sec_to_local = (uint32_t*)calloc(ctx->ehdr->e_shnum, sizeof(uint32_t));
    if (!ctx->elf_sec_to_local) {
        free(ctx->file_data);
        free(ctx);
        return NULL;
    }
    for (int i = 0; i < ctx->ehdr->e_shnum; i++) {
        ctx->elf_sec_to_local[i] = (uint32_t)-1;
    }

    return ctx;
}

static void elf_context_destroy(ElfContext* ctx) {
    if (!ctx) {
        return;
    }
    if (ctx->file_data) {
        free(ctx->file_data);
    }
    if (ctx->elf_sec_to_local) {
        free(ctx->elf_sec_to_local);
    }
    free(ctx);
}

static const char* elf_section_name(ElfContext* ctx, uint32_t sh_name) {
    if (!ctx->shstrtab || sh_name >= ctx->shstrtab_size) {
        return "";
    }
    return ctx->shstrtab + sh_name;
}

static ArkSectionKind elf_section_kind(const char* name, uint32_t sh_flags) {
    (void)sh_flags;
    if (strcmp(name, ".text") == 0) {
        return ARK_SECTION_CODE;
    }
    if (strcmp(name, ".data") == 0) {
        return ARK_SECTION_DATA;
    }
    if (strcmp(name, ".rodata") == 0 || strncmp(name, ".rodata.", 7) == 0) {
        return ARK_SECTION_RODATA;
    }
    if (strcmp(name, ".bss") == 0 || strncmp(name, ".bss.", 5) == 0) {
        return ARK_SECTION_BSS;
    }
    return ARK_SECTION_UNKNOWN;
}

static uint32_t elf_section_flags(uint32_t sh_flags, ArkSectionKind kind) {
    uint32_t flags = 0;
    if (sh_flags & 0x1) {
        flags |= ARK_SECTION_EXEC;
    }
    if (sh_flags & 0x2) {
        flags |= ARK_SECTION_WRITE;
    }
    flags |= ARK_SECTION_READ;
    if (kind == ARK_SECTION_CODE && !(flags & ARK_SECTION_EXEC)) {
        flags |= ARK_SECTION_EXEC;
    }
    if (kind == ARK_SECTION_DATA) {
        flags |= ARK_SECTION_WRITE;
    }
    if (kind == ARK_SECTION_BSS) {
        flags |= ARK_SECTION_WRITE;
    }
    return flags;
}

static int elf_is_relevant_section(const char* name, uint32_t sh_type) {
    if (sh_type != SHT_PROGBITS && sh_type != SHT_NOBITS) {
        return 0;
    }
    if (strcmp(name, ".text") == 0 || strcmp(name, ".data") == 0 || strcmp(name, ".rodata") == 0 ||
        strncmp(name, ".rodata.", 7) == 0 || strcmp(name, ".bss") == 0 || strncmp(name, ".bss.", 5) == 0) {
        return 1;
    }
    return 0;
}

ArkLinkResult ark_link_load_elf(const char* path, ArkLinkUnit** unit) {
    if (!path || !unit) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        return ARK_LINK_ERR_NOT_FOUND;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return ARK_LINK_ERR_IO;
    }

    uint8_t* raw_data = (uint8_t*)malloc((size_t)file_size);
    if (!raw_data) {
        fclose(file);
        return ARK_LINK_ERR_MEMORY;
    }

    if (fread(raw_data, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(raw_data);
        fclose(file);
        return ARK_LINK_ERR_IO;
    }
    fclose(file);

    if (!is_elf(raw_data, (size_t)file_size)) {
        free(raw_data);
        return ARK_LINK_ERR_FORMAT;
    }

    ElfContext* ctx = elf_context_create(raw_data, (size_t)file_size);
    free(raw_data);
    if (!ctx) {
        return ARK_LINK_ERR_MEMORY;
    }

    Elf64_Ehdr_Loader* ehdr = ctx->ehdr;
    if (ehdr->e_type != ET_REL) {
        elf_context_destroy(ctx);
        return ARK_LINK_ERR_UNSUPPORTED;
    }
    if (ehdr->e_machine != EM_X86_64) {
        elf_context_destroy(ctx);
        return ARK_LINK_ERR_UNSUPPORTED;
    }

    if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB) {
        elf_context_destroy(ctx);
        return ARK_LINK_ERR_FORMAT;
    }

    ArkLinkUnit* new_unit = ark_link_unit_create(path);
    if (!new_unit) {
        elf_context_destroy(ctx);
        return ARK_LINK_ERR_MEMORY;
    }

    for (int i = 0; i < ehdr->e_shnum; i++) {
        Elf64_Shdr_Loader* sh = &ctx->shdrs[i];
        const char* name = elf_section_name(ctx, sh->sh_name);
        if (elf_is_relevant_section(name, sh->sh_type)) {
            ArkSectionDesc desc = {0};
            desc.name = name;
            desc.size = (size_t)sh->sh_size;
            desc.alignment = (size_t)sh->sh_addralign;
            if (desc.alignment == 0) {
                desc.alignment = 1;
            }
            desc.flags = elf_section_flags(sh->sh_flags, elf_section_kind(name, sh->sh_flags));

            if (sh->sh_type != SHT_NOBITS && sh->sh_size > 0) {
                if (sh->sh_offset + sh->sh_size > ctx->file_size) {
                    ark_link_unit_destroy(new_unit);
                    elf_context_destroy(ctx);
                    return ARK_LINK_ERR_FORMAT;
                }
                desc.data = (uint8_t*)malloc((size_t)sh->sh_size);
                if (!desc.data) {
                    ark_link_unit_destroy(new_unit);
                    elf_context_destroy(ctx);
                    return ARK_LINK_ERR_MEMORY;
                }
                memcpy((void*)desc.data, ctx->file_data + sh->sh_offset, (size_t)sh->sh_size);
            } else {
                desc.data = NULL;
            }

            ArkLinkSection* sec = ark_link_unit_add_section(new_unit, &desc);
            if (!sec) {
                free((void*)desc.data);
                ark_link_unit_destroy(new_unit);
                elf_context_destroy(ctx);
                return ARK_LINK_ERR_MEMORY;
            }
            sec->kind = elf_section_kind(name, sh->sh_flags);
            ctx->elf_sec_to_local[i] = (uint32_t)(new_unit->section_count - 1);
        }
    }

    Elf64_Sym_Loader* symtab = NULL;
    size_t sym_count = 0;
    const char* strtab = NULL;
    size_t strtab_size = 0;

    for (int i = 0; i < ehdr->e_shnum; i++) {
        Elf64_Shdr_Loader* sh = &ctx->shdrs[i];
        if (sh->sh_type == SHT_SYMTAB) {
            if (sh->sh_offset + sh->sh_size > ctx->file_size) {
                continue;
            }
            symtab = (Elf64_Sym_Loader*)(ctx->file_data + sh->sh_offset);
            sym_count = (size_t)(sh->sh_size / sizeof(Elf64_Sym_Loader));
            if (sh->sh_link < (uint32_t)ehdr->e_shnum) {
                Elf64_Shdr_Loader* str_sh = &ctx->shdrs[sh->sh_link];
                if (str_sh->sh_offset + str_sh->sh_size <= ctx->file_size) {
                    strtab = (const char*)(ctx->file_data + str_sh->sh_offset);
                    strtab_size = (size_t)str_sh->sh_size;
                }
            }
            break;
        }
    }

    if (symtab) {
        for (size_t i = 1; i < sym_count; i++) {
            Elf64_Sym_Loader* sym = &symtab[i];
            ArkSymbolDesc desc = {0};

            const char* sym_name = NULL;
            if (sym->st_name < strtab_size && strtab) {
                const char* name = strtab + sym->st_name;
                if (name[0] != '\0') {
                    sym_name = name;
                }
            }

            uint16_t shndx = sym->st_shndx;
            uint8_t type = ELF64_ST_TYPE(sym->st_info);

            if (!sym_name && type == STT_SECTION && shndx < (uint16_t)ehdr->e_shnum) {
                sym_name = elf_section_name(ctx, ctx->shdrs[shndx].sh_name);
            }

            if (!sym_name && type == STT_FILE) {
                char file_sym[64];
                snprintf(file_sym, sizeof(file_sym), ".file.%zu", i);
                desc.name = strdup(file_sym);
            } else if (sym_name) {
                desc.name = strdup(sym_name);
            } else {
                char anon_name[64];
                snprintf(anon_name, sizeof(anon_name), ".anon.%zu", i);
                desc.name = strdup(anon_name);
            }

            desc.value = (uint64_t)sym->st_value;
            desc.size = (uint32_t)sym->st_size;

            if (shndx == SHN_UNDEF) {
                desc.section_index = 0;
            } else if (shndx < (uint16_t)ehdr->e_shnum) {
                uint32_t local = ctx->elf_sec_to_local[shndx];
                if (local != (uint32_t)-1) {
                    desc.section_index = local + 1;
                } else {
                    desc.section_index = 0;
                }
            } else {
                desc.section_index = 0;
            }

            uint8_t bind = ELF64_ST_BIND(sym->st_info);
            switch (bind) {
            case STB_LOCAL:
                desc.binding = ARK_BIND_LOCAL;
                break;
            case STB_WEAK:
                desc.binding = ARK_BIND_WEAK;
                break;
            default:
                desc.binding = ARK_BIND_GLOBAL;
                break;
            }
            desc.type = type == STT_FUNC ? ARK_SYM_FUNC : type == STT_OBJECT ? ARK_SYM_OBJECT : ARK_SYM_NOTYPE;

            ark_link_unit_add_symbol(new_unit, &desc);
        }
    }

    for (int i = 0; i < ehdr->e_shnum; i++) {
        Elf64_Shdr_Loader* sh = &ctx->shdrs[i];
        if (sh->sh_type != SHT_RELA) {
            continue;
        }
        if (sh->sh_offset + sh->sh_size > ctx->file_size) {
            continue;
        }

        uint32_t target_elf_idx = (uint32_t)sh->sh_info;
        if (target_elf_idx >= (uint32_t)ehdr->e_shnum) {
            continue;
        }
        uint32_t local = ctx->elf_sec_to_local[target_elf_idx];
        if (local == (uint32_t)-1) {
            continue;
        }
        if (local >= new_unit->section_count) {
            continue;
        }
        ArkLinkSection* target_sec = &new_unit->sections[local];

        size_t rela_count = (size_t)(sh->sh_size / sizeof(Elf64_Rela_Loader));
        Elf64_Rela_Loader* relas = (Elf64_Rela_Loader*)(ctx->file_data + sh->sh_offset);

        for (size_t j = 0; j < rela_count; j++) {
            Elf64_Rela_Loader* r = &relas[j];
            uint32_t elf_sym_idx = ELF64_R_SYM(r->r_info);
            if (elf_sym_idx == 0) {
                continue;
            }
            elf_sym_idx--;
            uint32_t r_type = ELF64_R_TYPE(r->r_info);

            uint16_t ark_type = 0;
            switch (r_type) {
            case R_X86_64_64:
                ark_type = ARK_RELOC_ABS64;
                break;
            case R_X86_64_32:
                ark_type = ARK_RELOC_ADDR32;
                break;
            case R_X86_64_32S:
                ark_type = ARK_RELOC_ADDR32;
                break;
            case R_X86_64_PC32:
                ark_type = ARK_RELOC_PC32;
                break;
            case R_X86_64_PLT32:
                ark_type = ARK_RELOC_PC32;
                break;
            case R_X86_64_GOTPC32:
                ark_type = ARK_RELOC_GOTPC32;
                break;
            default:
                ark_type = ARK_RELOC_ABS64;
                break;
            }

            ArkRelocationDesc reloc = {0};
            reloc.offset = (uint64_t)r->r_offset;
            reloc.sym_idx = (uint32_t)elf_sym_idx;
            reloc.section_index = local;
            reloc.type = ark_type;
            reloc.addend = r->r_addend;

            ark_link_section_add_reloc(target_sec, &reloc);
        }
    }

    elf_context_destroy(ctx);

    new_unit->file_size = 0;
    *unit = new_unit;
    return ARK_LINK_OK;
}
