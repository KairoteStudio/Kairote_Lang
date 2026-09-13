#ifndef KRT_CONFIG_MANAGER_H
#define KRT_CONFIG_MANAGER_H

#include "../../Core/Utils/KrtCommon.h"

#ifndef KRT_MAX_PATH
#define KRT_MAX_PATH 1024
#endif

typedef enum { KRT_TARGET_ASM, KRT_TARGET_IR, KRT_TARGET_EXE, KRT_TARGET_VM, KRT_TARGET_KRO } KrtTargetType;

typedef enum { KRT_CONFIG_PLATFORM_WINDOWS, KRT_CONFIG_PLATFORM_LINUX, KRT_CONFIG_PLATFORM_UNKNOWN } KrtPlatformType;

typedef struct {
    const char* platform_name;
    const char* const* object_files;
    int object_count;
    const char* const* end_objects;
    int end_object_count;
    const char* const* library_paths;
    int library_path_count;
    const char* const* libraries;
    int library_count;
    const char* entry_point;
    const char* subsystem;
    const char* dynamic_linker;
} KrtLinkerConfig;

typedef struct {

    KrtTargetType target_type;
    int optimization_level;
    KrtPlatformType platform;
    const char* input_file;
    const char* output_file;

    int show_ir;
    int show_help;
    int create_project;
    int keep_temp_files;
    const char* project_type;

    const KrtLinkerConfig* linker_config;

    int color_enabled;
    char temp_asm_file[KRT_MAX_PATH];
    char temp_obj_file[KRT_MAX_PATH];

    char** imported_files;
    int imported_file_count;
    int imported_file_capacity;
} KrtConfig;

KrtConfig* KrtConfigCreate(void);
void KrtConfigDestroy(KrtConfig* config);
void KrtConfigAddImportedFile(KrtConfig* config, const char* file_path);

KrtConfig* KrtConfigParseCommandLine(int argc, char* argv[]);
int KrtConfigValidate(KrtConfig* config);

KrtPlatformType KrtConfigDetectPlatform(void);
const KrtLinkerConfig* KrtConfigGetLinkerConfig(KrtPlatformType platform);

const char* KrtConfigGetDefaultOutput(KrtConfig* config);
int KrtConfigNeedsLinking(KrtConfig* config);

const char* KrtConfigProjectNameFromType(const char* project_type);
int KrtConfigCreateProject(const char* project_name, const char* project_type);

#endif
