#ifndef KRT_ARKLINK_INTEGRATION_H
#define KRT_ARKLINK_INTEGRATION_H

#include <ArkLink/Arklink.h>
#include "ConfigManager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KrtArkLinkContext KrtArkLinkContext;

KrtArkLinkContext* KrtArkLinkContextCreate(KrtConfig* config);
void KrtArkLinkContextDestroy(KrtArkLinkContext* ctx);
int KrtArkLinkAddObjectFile(KrtArkLinkContext* ctx, const char* obj_path);
int KrtArkLinkAddRuntimeObjects(KrtArkLinkContext* ctx, const char* runtime_dir);
int KrtArkLinkSetOutput(KrtArkLinkContext* ctx, const char* output_path);
int KrtArkLinkSetEntryPoint(KrtArkLinkContext* ctx, const char* entry_point);
int KrtArkLinkLink(KrtArkLinkContext* ctx);
int KrtArkLinkLoadProjectLibraries(KrtArkLinkContext* ctx, KrtConfig* config);
int KrtArkLinkLinkObjects(const char** obj_files, int obj_count, 
                            const char* output_path, KrtConfig* config);

#ifdef __cplusplus
}
#endif

#endif
