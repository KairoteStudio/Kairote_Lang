#include "ArkLink/Context.h"
#include <stdlib.h>
#include <string.h>

typedef struct ArkLinkContext {
    ArkLinkTarget target;
    void* backend_data;
} ArkLinkContext;

ArkLinkContext* ark_context_create(ArkLinkTarget target) {
    ArkLinkContext* ctx = (ArkLinkContext*)calloc(1, sizeof(ArkLinkContext));
    if (!ctx) {
        return NULL;
    }

    ctx->target = target;
    return ctx;
}

void ark_context_destroy(ArkLinkContext* ctx) {
    if (!ctx) {
        return;
    }
    free(ctx);
}
