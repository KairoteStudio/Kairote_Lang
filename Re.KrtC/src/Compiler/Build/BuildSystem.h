#ifndef KRT_BUILD_SYSTEM_H
#define KRT_BUILD_SYSTEM_H

#include "../Driver/ConfigManager.h"
#include "../Platform/PlatformAbstraction.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    KrtConfig* config;
    KrtPlatform* platform;
    char error_message[256];
} KrtBuildContext;

KrtBuildContext* KrtBuildContextCreate(KrtConfig* config, KrtPlatform* platform);
void KrtBuildContextDestroy(KrtBuildContext* ctx);
int KrtBuildExecute(KrtBuildContext* ctx, const char* input_file, const char* output_file);
const char* KrtBuildGetError(KrtBuildContext* ctx);

#ifdef __cplusplus
}
#endif

#endif
