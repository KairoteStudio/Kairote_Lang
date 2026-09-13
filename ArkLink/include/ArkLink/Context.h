#ifndef ARKLINK_CONTEXT_H
#define ARKLINK_CONTEXT_H

#include "Arklink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArkSectionBuffer {
    uint8_t* data;
    size_t size;
    size_t capacity;
    int kind;
    uint32_t flags;
    uint32_t alignment;
} ArkSectionBuffer;

typedef struct ArkLinkContext ArkLinkContext;

/** @brief Create an owned link context for target, or return NULL on failure. */
ArkLinkContext* ark_context_create(ArkLinkTarget target);
/** @brief Release ctx and its owned data, accepting NULL. */
void ark_context_destroy(ArkLinkContext* ctx);

#ifdef __cplusplus
}
#endif

#endif
