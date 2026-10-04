#ifndef ARKLINK_NATIVE_ABI_H
#define ARKLINK_NATIVE_ABI_H
#include "Loader.h"

/* Exact native contracts are checked before symbol resolution or output. */
ArkLinkResult ark_native_abi_validate(ArkLinkUnit* const* units, size_t count);
#endif
