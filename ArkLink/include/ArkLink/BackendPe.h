#ifndef ARKLINK_BACKEND_PE_H
#define ARKLINK_BACKEND_PE_H

#include "Context.h"
#include "Backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Build a PE image in output; the caller owns successful image and section-map allocations. */
ArkLinkResult ark_backend_pe_link(ArkLinkContext* ctx, ArkBackendInput* input, ArkBackendOutput* output);

#ifdef __cplusplus
}
#endif

#endif
