#include "ArkLink/Loader.h"
#include <stdio.h>

ArkLinkResult ark_link_load_coff(const char* path, ArkLinkUnit** unit) {
    if (!path || !unit) {
        return ARK_LINK_ERR_INVALID_ARGUMENT;
    }
    (void)path;
    (void)unit;
    fprintf(stderr, "ArkLink: COFF object loading is not supported\n");
    return ARK_LINK_ERR_UNSUPPORTED;
}
