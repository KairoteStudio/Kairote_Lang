#include "ArkLink/Loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ArkLinkUnit* ark_link_unit_create(const char* path) {
    ArkLinkUnit* unit = (ArkLinkUnit*)calloc(1, sizeof(ArkLinkUnit));
    if (!unit) {
        return NULL;
    }

    if (path) {
        unit->path = strdup(path);
        if (!unit->path) {
            free(unit);
            return NULL;
        }
    }

    return unit;
}

void ark_link_unit_destroy(ArkLinkUnit* unit) {
    if (!unit) {
        return;
    }

    if (unit->path) {
        free((char*)unit->path);
    }

    if (unit->sections) {
        for (size_t i = 0; i < unit->section_count; i++) {
            ArkLinkSection* sec = &unit->sections[i];
            if (sec->relocs) {
                free(sec->relocs);
            }

            if (sec->data) {
                free((void*)sec->data);
            }
        }
        free(unit->sections);
    }

    if (unit->symbols) {
        for (size_t i = 0; i < unit->symbol_count; i++) {
            ArkSymbolDesc* sym = &unit->symbols[i];
            if (sym->name) {
                free((char*)sym->name);
            }
            if (sym->import_module) {
                free((void*)sym->import_module);
            }
        }
        free(unit->symbols);
    }

    if (unit->relocations.relocs) {
        free(unit->relocations.relocs);
    }

    if (unit->file_data) {
        free(unit->file_data);
    }

    free(unit);
}

ArkLinkSection* ark_link_unit_add_section(ArkLinkUnit* unit, const ArkSectionDesc* desc) {
    if (!unit || !desc || !desc->name) {
        return NULL;
    }

    ArkLinkSection* new_sections =
        (ArkLinkSection*)realloc(unit->sections, (unit->section_count + 1) * sizeof(ArkLinkSection));
    if (!new_sections) {
        return NULL;
    }

    unit->sections = new_sections;
    ArkLinkSection* sec = &unit->sections[unit->section_count];
    memset(sec, 0, sizeof(ArkLinkSection));

    strncpy(sec->name, desc->name, sizeof(sec->name) - 1);
    sec->data = desc->data;
    sec->size = desc->size;
    sec->alignment = desc->alignment;
    sec->flags = desc->flags;

    unit->section_count++;
    return sec;
}

int ark_link_unit_add_symbol(ArkLinkUnit* unit, const ArkSymbolDesc* desc) {
    if (!unit || !desc) {
        return 0;
    }

    ArkSymbolDesc* new_symbols =
        (ArkSymbolDesc*)realloc(unit->symbols, (unit->symbol_count + 1) * sizeof(ArkSymbolDesc));
    if (!new_symbols) {
        return 0;
    }

    unit->symbols = new_symbols;
    unit->symbols[unit->symbol_count] = *desc;
    unit->symbol_count++;
    return 1;
}

ArkLinkSection* ark_link_section_create(const char* name, const uint8_t* data, size_t size) {
    if (!name) {
        return NULL;
    }

    ArkLinkSection* sec = (ArkLinkSection*)calloc(1, sizeof(ArkLinkSection));
    if (!sec) {
        return NULL;
    }

    strncpy(sec->name, name, sizeof(sec->name) - 1);
    sec->data = data;
    sec->size = size;

    return sec;
}

int ark_link_section_add_reloc(ArkLinkSection* section, const ArkRelocationDesc* desc) {
    if (!section || !desc) {
        return 0;
    }

    if (section->reloc_count >= section->reloc_capacity) {
        size_t new_capacity = section->reloc_capacity ? section->reloc_capacity * 2 : 8;
        ArkRelocationDesc* new_relocs =
            (ArkRelocationDesc*)realloc(section->relocs, new_capacity * sizeof(ArkRelocationDesc));
        if (!new_relocs) {
            return 0;
        }

        section->relocs = new_relocs;
        section->reloc_capacity = new_capacity;
    }

    section->relocs[section->reloc_count++] = *desc;
    return 1;
}

int ark_link_unit_add_reloc(ArkLinkUnit* unit, const ArkRelocationDesc* desc) {
    if (!unit || !desc) {
        return 0;
    }

    ArkRelocationDesc* new_relocs = (ArkRelocationDesc*)realloc(
        unit->relocations.relocs, (unit->relocations.count + 1) * sizeof(ArkRelocationDesc));
    if (!new_relocs) {
        return 0;
    }

    unit->relocations.relocs = new_relocs;
    unit->relocations.relocs[unit->relocations.count++] = *desc;
    return 1;
}

ArkLinkResult ark_loader_load_unit(ArkLinkContext* ctx, const char* path, const ArkLoaderOptions* opts,
                                   ArkLinkUnit** out_unit, ArkLoaderDiagnostics* diag) {
    if (!path || !out_unit) {
        if (diag) {
            diag->code = ARK_LINK_ERR_INVALID_ARGUMENT;
            diag->message = "Invalid arguments";
        }
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }

    (void)ctx;
    (void)opts;

    const char* ext = strrchr(path, '.');
    if (!ext) {
        if (diag) {
            diag->code = ARK_LINK_ERR_FORMAT;
            diag->message = "File has no extension";
        }
        return ARK_LINK_ERR_FORMAT;
    }

    ArkLinkResult result;
    ArkLinkUnit* unit = NULL;

    if (strcmp(ext, ".kro") == 0) {
        result = ark_link_load_kro(path, &unit);
    } else if (strcmp(ext, ".obj") == 0 || strcmp(ext, ".o") == 0) {
        FILE* magic_file = fopen(path, "rb");
        if (magic_file) {
            unsigned char magic[4] = {0};
            size_t magic_read = fread(magic, 1, 4, magic_file);
            fclose(magic_file);
            if (magic_read < 4) {
                result = ARK_LINK_ERR_FORMAT;
            } else if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F') {
                result = ark_link_load_elf(path, &unit);
            } else {
                result = ark_link_load_coff(path, &unit);
            }
        } else {
            result = ARK_LINK_ERR_NOT_FOUND;
        }
    } else {
        if (diag) {
            diag->code = ARK_LINK_ERR_UNSUPPORTED;
            diag->message = "Unsupported file format";
        }
        return ARK_LINK_ERR_UNSUPPORTED;
    }

    if (result != ARK_LINK_OK) {
        if (diag) {
            diag->code = result;
            diag->message = "Failed to load file";
        }
        return result;
    }

    *out_unit = unit;
    return ARK_LINK_OK;
}
