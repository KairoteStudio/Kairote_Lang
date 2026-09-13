#include "ArkLink/Loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ArkLinkResult ark_link_load_coff(const char* path, ArkLinkUnit** unit) {
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

    if (file_size < 0) {
        fclose(file);
        return ARK_LINK_ERR_IO;
    }

    ArkLinkUnit* new_unit = ark_link_unit_create(path);
    if (!new_unit) {
        fclose(file);
        return ARK_LINK_ERR_MEMORY;
    }

    new_unit->file_data = (uint8_t*)malloc((size_t)file_size);
    if (!new_unit->file_data) {
        ark_link_unit_destroy(new_unit);
        fclose(file);
        return ARK_LINK_ERR_MEMORY;
    }

    size_t read_size = fread(new_unit->file_data, 1, (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        ark_link_unit_destroy(new_unit);
        return ARK_LINK_ERR_IO;
    }

    new_unit->file_size = (size_t)file_size;

    *unit = new_unit;
    return ARK_LINK_OK;
}
