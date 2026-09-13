#ifndef ARKLINK_LINKER_NATIVE_H
#define ARKLINK_LINKER_NATIVE_H

#include "Arklink.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Link session inputs using the native platform linker. */
ArkLinkResult arklink_session_link_native(ArkLinkSession* session);

#ifdef __cplusplus
}
#endif

#endif
