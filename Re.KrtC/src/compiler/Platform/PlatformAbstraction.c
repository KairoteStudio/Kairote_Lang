#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "PlatformAbstraction.h"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#define KRT_PLATFORM_DEFAULT KRT_CONFIG_PLATFORM_WINDOWS
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#define KRT_PLATFORM_DEFAULT KRT_CONFIG_PLATFORM_LINUX
#endif

static int win32_is_absolute_path(const char* path) {
    if (!path || path[0] == '\0') return 0;
    if (path[0] == '/' || path[0] == '\\') return 1;
    return strlen(path) >= 2 && path[1] == ':';
}

static int win32_path_exists(const char* path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

static int win32_is_directory(const char* path) {
    if (!path || path[0] == '\0') return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static int win32_supports_color(void) {
#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) return 0;
    
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return 0;
    
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return 0;
    
    if (!(mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    return 1;
#else
    return 0; 
#endif
}

static void win32_set_utf8(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

static int win32_execute_command(const char* command) {
    return system(command);
}

static int linux_is_absolute_path(const char* path) {
    return path && path[0] == '/';
}

static int linux_path_exists(const char* path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

static int linux_is_directory(const char* path) {
    if (!path || path[0] == '\0') return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int linux_supports_color(void) {
    return isatty(fileno(stdout));
}

static void linux_set_utf8(void) {
    
}

static int linux_execute_command(const char* command) {
    return system(command);
}

KrtPlatform* KrtPlatformCreate(KrtPlatformType type) {
    KrtPlatform* platform = (KrtPlatform*)KRT_MALLOC(sizeof(KrtPlatform));
    if (!platform) return NULL;
    
    memset(platform, 0, sizeof(KrtPlatform));
    platform->type = type;
    
    switch (type) {
        case KRT_CONFIG_PLATFORM_WINDOWS:
            platform->name = "Windows";
            platform->path_separator = '\\';
            platform->is_absolute_path = win32_is_absolute_path;
            platform->path_exists = win32_path_exists;
            platform->is_directory = win32_is_directory;
            platform->supports_color = win32_supports_color;
            platform->set_utf8 = win32_set_utf8;
            platform->execute_command = win32_execute_command;
            break;
            
        case KRT_CONFIG_PLATFORM_LINUX:
            platform->name = "Linux";
            platform->path_separator = '/';
            platform->is_absolute_path = linux_is_absolute_path;
            platform->path_exists = linux_path_exists;
            platform->is_directory = linux_is_directory;
            platform->supports_color = linux_supports_color;
            platform->set_utf8 = linux_set_utf8;
            platform->execute_command = linux_execute_command;
            break;
            
        default:
            KrtError("不支持的平台类型: %d", type);
            KRT_FREE(platform);
            return NULL;
    }
    
    return platform;
}

void KrtPlatformDestroy(KrtPlatform* platform) {
    if (!platform) return;
    KRT_FREE(platform);
}

char KrtPlatformGetSeparator(KrtPlatform* platform) {
    return platform ? platform->path_separator : '/';
}

int KrtPlatformPathIsAbsolute(KrtPlatform* platform, const char* path) {
    return platform && platform->is_absolute_path ? platform->is_absolute_path(path) : 0;
}

int KrtPlatformPathExists(KrtPlatform* platform, const char* path) {
    return platform && platform->path_exists ? platform->path_exists(path) : 0;
}

int KrtPlatformPathIsDirectory(KrtPlatform* platform, const char* path) {
    return platform && platform->is_directory ? platform->is_directory(path) : 0;
}

void KrtPlatformPathJoin(KrtPlatform* platform, char* buffer, size_t buffer_size, const char* base, const char* part) {
    if (!buffer || buffer_size == 0) return;
    if (!platform) {
        snprintf(buffer, buffer_size, "%s", part ? part : "");
        return;
    }
    
    if (!base || base[0] == '\0') {
        snprintf(buffer, buffer_size, "%s", part ? part : "");
        return;
    }
    if (!part || part[0] == '\0') {
        snprintf(buffer, buffer_size, "%s", base);
        return;
    }

    size_t base_len = strlen(base);
    int need_sep = base_len > 0 && base[base_len - 1] != '/' && base[base_len - 1] != '\\';
    if (need_sep) {
        snprintf(buffer, buffer_size, "%s%c%s", base, platform->path_separator, part);
    } else {
        snprintf(buffer, buffer_size, "%s%s", base, part);
    }
}

int KrtPlatformConsoleSupportsColor(KrtPlatform* platform) {
    return platform && platform->supports_color ? platform->supports_color() : 0;
}

void KrtPlatformConsoleSetUtf8(KrtPlatform* platform) {
    if (platform && platform->set_utf8) platform->set_utf8();
}

int KrtPlatformExecuteCommand(KrtPlatform* platform, const char* command) {
    return platform && platform->execute_command ? platform->execute_command(command) : -1;
}

KrtPlatform* KrtPlatformGetCurrent(void) {
    static KrtPlatform* current_platform = NULL;
    if (!current_platform) {
        current_platform = KrtPlatformCreate(KRT_PLATFORM_DEFAULT);
    }
    return current_platform;
}