#include "BuildSystem.h"
#include "../Driver/ArkLinkIntegration.h"
#include <string.h>
#include <stdio.h>

KrtBuildContext* KrtBuildContextCreate(KrtConfig* config, KrtPlatform* platform) {
    KrtBuildContext* ctx = (KrtBuildContext*)KRT_MALLOC(sizeof(KrtBuildContext));
    if (!ctx) return NULL;

    memset(ctx, 0, sizeof(KrtBuildContext));
    ctx->config = config;
    ctx->platform = platform;
    ctx->error_message[0] = '\0';

    return ctx;
}

void KrtBuildContextDestroy(KrtBuildContext* ctx) {
    if (!ctx) return;
    KRT_FREE(ctx);
}

int KrtBuildExecute(KrtBuildContext* ctx, const char* input_file, const char* output_file) {
    if (!ctx) {
        return 0;
    }

    if (!input_file || !input_file[0] || !output_file || !output_file[0]) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Invalid build arguments");
        return 0;
    }

    const char* ext = strrchr(input_file, '.');
    if (!ext || (strcmp(ext, ".kro") != 0 && strcmp(ext, ".eo") != 0)) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Unsupported input format for native link: %s", input_file);
        return 0;
    }

    KrtArkLinkContext* ark_ctx = KrtArkLinkContextCreate(ctx->config);
    if (!ark_ctx) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Failed to create ArkLink context");
        return 0;
    }

    if (KrtArkLinkAddObjectFile(ark_ctx, input_file) != 0) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Failed to add input file: %s", input_file);
        KrtArkLinkContextDestroy(ark_ctx);
        return 0;
    }

    if (KrtArkLinkSetOutput(ark_ctx, output_file) != 0) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "Failed to set output path: %s", output_file);
        KrtArkLinkContextDestroy(ark_ctx);
        return 0;
    }

    int result = KrtArkLinkLink(ark_ctx);
    if (result != 0) {
        snprintf(ctx->error_message, sizeof(ctx->error_message),
                 "ArkLink link failed");
        KrtArkLinkContextDestroy(ark_ctx);
        return 0;
    }

    KrtArkLinkContextDestroy(ark_ctx);
    return 1;
}

const char* KrtBuildGetError(KrtBuildContext* ctx) {
    if (!ctx) return "Unknown error";
    if (ctx->error_message[0]) {
        return ctx->error_message;
    }
    return "Build failed";
}