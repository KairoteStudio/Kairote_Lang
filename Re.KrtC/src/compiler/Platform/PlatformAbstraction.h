#ifndef KRT_PLATFORM_ABSTRACTION_H
#define KRT_PLATFORM_ABSTRACTION_H

#include "../../Core/Utils/KrtCommon.h"
#include "../Driver/ConfigManager.h"

typedef struct KrtPlatform {
    KrtPlatformType type;
    const char* name;

    char path_separator;
    int (*is_absolute_path)(const char* path);
    int (*path_exists)(const char* path);
    int (*is_directory)(const char* path);

    int (*supports_color)(void);
    void (*set_utf8)(void);

    int (*execute_command)(const char* command);
} KrtPlatform;

KrtPlatform* KrtPlatformCreate(KrtPlatformType type);
void KrtPlatformDestroy(KrtPlatform* platform);

char KrtPlatformGetSeparator(KrtPlatform* platform);
int KrtPlatformPathIsAbsolute(KrtPlatform* platform, const char* path);
int KrtPlatformPathExists(KrtPlatform* platform, const char* path);
int KrtPlatformPathIsDirectory(KrtPlatform* platform, const char* path);
void KrtPlatformPathJoin(KrtPlatform* platform, char* buffer, size_t buffer_size, const char* base, const char* part);

int KrtPlatformConsoleSupportsColor(KrtPlatform* platform);
void KrtPlatformConsoleSetUtf8(KrtPlatform* platform);

int KrtPlatformExecuteCommand(KrtPlatform* platform, const char* command);

KrtPlatform* KrtPlatformGetCurrent(void);

#endif
