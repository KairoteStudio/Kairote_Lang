#include <string.h>

#include "../../Core/Utils/Path.h"
#include "../Platform/PlatformAbstraction.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define KRT_PLATFORM_DEFAULT KRT_CONFIG_PLATFORM_WINDOWS
#else
#include <unistd.h>
#define KRT_PLATFORM_DEFAULT KRT_CONFIG_PLATFORM_LINUX
#endif

static const KrtLinkerConfig g_linker_configs[] = {
    {"mainCRTStartup"},
    {"_start"},
};

KrtConfig* KrtConfigCreate(void) {
    KrtConfig* config = (KrtConfig*)KRT_MALLOC(sizeof(KrtConfig));
    if (!config) {
        KrtError("无法分配编译配置");
        return NULL;
    }

    memset(config, 0, sizeof(KrtConfig));
    config->target_type = KRT_TARGET_ASM;
    config->optimization_level = 2;
    config->platform = KrtConfigDetectPlatform();
    config->keep_temp_files = 0;
    config->max_threads = 8;

    return config;
}

void KrtConfigDestroy(KrtConfig* config) {
    if (!config) {
        return;
    }

    if (config->imported_files) {
        for (int i = 0; i < config->imported_file_count; i++) {
            if (config->imported_files[i]) {
                KRT_FREE(config->imported_files[i]);
            }
        }
        KRT_FREE(config->imported_files);
    }

    KRT_FREE(config);
}

void KrtConfigAddImportedFile(KrtConfig* config, const char* file_path) {
    if (!config || !file_path) {
        return;
    }
    if (!krt_string_list_add(&config->imported_files, &config->imported_file_count, &config->imported_file_capacity,
                             file_path)) {
        KrtError("无法记录导入文件: %s", file_path);
    }
}

KrtPlatformType KrtConfigDetectPlatform(void) {
#ifdef _WIN32
    return KRT_CONFIG_PLATFORM_WINDOWS;
#else
    return KRT_CONFIG_PLATFORM_LINUX;
#endif
}

const KrtLinkerConfig* KrtConfigGetLinkerConfig(KrtPlatformType platform) {
    switch (platform) {
    case KRT_CONFIG_PLATFORM_WINDOWS:
        return &g_linker_configs[0];
    case KRT_CONFIG_PLATFORM_LINUX:
        return &g_linker_configs[1];
    default:
        KrtError("未知的链接平台配置");
        return NULL;
    }
}

const char* KrtConfigGetDefaultOutput(KrtConfig* config) {
    if (!config) {
        return "output";
    }

    switch (config->target_type) {
    case KRT_TARGET_EXE:
        return (config->platform == KRT_CONFIG_PLATFORM_WINDOWS) ? "a.exe" : "a";
    case KRT_TARGET_KRO:
        return "output.kro";
    case KRT_TARGET_ARM64:
        return "output.arm64";
    case KRT_TARGET_IR:
        return "output.ir";
    case KRT_TARGET_ASM:
    default:
        return "output.asm";
    }
}

int KrtConfigNeedsLinking(KrtConfig* config) {
    return config && (config->target_type == KRT_TARGET_EXE || config->target_type == KRT_TARGET_KRO);
}

int KrtConfigValidate(KrtConfig* config) {
    if (!config) {
        return 0;
    }

    if (!config->create_project && !config->input_file) {
        KrtError("Input file not specified");
        return 0;
    }

    return 1;
}
