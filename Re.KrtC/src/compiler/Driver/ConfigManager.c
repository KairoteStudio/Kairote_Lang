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

static const char* g_win_object_files[] = {
    NULL
};

static const char* g_win_end_objects[] = {
    NULL
};

static const char* g_win_library_paths[] = {
    NULL
};

static const char* g_win_libraries[] = {
    NULL
};

static const char* g_linux_libraries[] = {
    NULL
};

static const KrtLinkerConfig g_linker_configs[] = {
    {
        "Windows",
        g_win_object_files, sizeof(g_win_object_files) / sizeof(char*),
        g_win_end_objects, sizeof(g_win_end_objects) / sizeof(char*),
        g_win_library_paths, sizeof(g_win_library_paths) / sizeof(char*),
        g_win_libraries, sizeof(g_win_libraries) / sizeof(char*),
        "mainCRTStartup", "console", NULL
    },
    {
        "Linux",
        NULL, 0,
        NULL, 0,
        NULL, 0,
        g_linux_libraries, sizeof(g_linux_libraries) / sizeof(char*),
        "_start", NULL, NULL
    }
};

KrtConfig* KrtConfigCreate(void) {
    KrtConfig* config = (KrtConfig*)KRT_MALLOC(sizeof(KrtConfig));
    if (!config) return NULL;
    
    memset(config, 0, sizeof(KrtConfig));
    config->target_type = KRT_TARGET_ASM;
    config->platform = KrtConfigDetectPlatform();
    config->linker_config = KrtConfigGetLinkerConfig(config->platform);
    config->keep_temp_files = 1;
    
    return config;
}

void KrtConfigDestroy(KrtConfig* config) {
    if (!config) return;

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

void KrtConfigAddImportedFile(KrtConfig* config, const char* file_path)
{
    if (!config || !file_path) return;

    for (int i = 0; i < config->imported_file_count; i++)
    {
        if (strcmp(config->imported_files[i], file_path) == 0) return;
    }

    if (config->imported_file_count >= config->imported_file_capacity)
    {
        int new_capacity = config->imported_file_capacity == 0 ? 8 : config->imported_file_capacity * 2;
        char** new_files = (char**)KRT_REALLOC(config->imported_files, new_capacity * sizeof(char*));
        if (!new_files) return;
        config->imported_files = new_files;
        config->imported_file_capacity = new_capacity;
    }

    config->imported_files[config->imported_file_count++] = KRT_STRDUP(file_path);
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
            return &g_linker_configs[1]; 
    }
}

const char* KrtConfigProjectNameFromType(const char* project_type) {
    if (!project_type) return "MyProject";
    if (strcmp(project_type, "console") == 0) return "ConsoleApp";
    if (strcmp(project_type, "library") == 0) return "Library";
    if (strcmp(project_type, "web") == 0) return "WebApp";
    if (strcmp(project_type, "system") == 0) return "SystemProject";
    return project_type;
}

const char* KrtConfigGetDefaultOutput(KrtConfig* config) {
    if (!config) return "output";
    
    switch (config->target_type) {
        case KRT_TARGET_EXE:
            return (config->platform == KRT_CONFIG_PLATFORM_WINDOWS) ? "a.exe" : "a";
        case KRT_TARGET_IR:
            return "output.ir";
        case KRT_TARGET_ASM:
        default:
            return "output.asm";
    }
}

int KrtConfigNeedsLinking(KrtConfig* config) {
    return config && config->target_type == KRT_TARGET_EXE;
}

int KrtConfigValidate(KrtConfig* config) {
    if (!config) return 0;
    
    if (!config->create_project && !config->input_file) {
        KrtError("Input file not specified");
        return 0;
    }
    
    return 1;
}

int KrtConfigCreateProject(const char* project_name, const char* project_type) {
    KrtPlatform* platform = KrtPlatformGetCurrent();
    if (!platform) {
        KrtError("Failed to create platform abstraction");
        return 1;
    }

    if (KrtEnsureDirectoryRecursive(project_name) != 0) {
        KrtPlatformDestroy(platform);
        return 1;
    }

    char project_file[KRT_MAX_PATH];
    snprintf(project_file, sizeof(project_file), "%s/project.esproj", project_name);

    FILE* fp = fopen(project_file, "w");
    if (!fp) {
        KrtError("无法创建项目文件: %s", project_file);
        KrtPlatformDestroy(platform);
        return 1;
    }

    fprintf(fp, "<Project>\n");
    fprintf(fp, "  <PropertyGroup>\n");
    fprintf(fp, "    <OutputType>%s</OutputType>\n", project_type);
    fprintf(fp, "    <TargetFramework>es1.0.0</TargetFramework>\n");
    fprintf(fp, "  </PropertyGroup>\n");
    fprintf(fp, "  <ItemGroup>\n");
    fprintf(fp, "    <Compile Include=\"main.es\" />\n");
    fprintf(fp, "  </ItemGroup>\n");
    fprintf(fp, "</Project>\n");
    fclose(fp);

    char main_file[KRT_MAX_PATH];
    snprintf(main_file, sizeof(main_file), "%s/main.es", project_name);

    fp = fopen(main_file, "w");
    if (!fp) {
        KrtError("无法创建主文件: %s", main_file);
        KrtPlatformDestroy(platform);
        return 1;
    }

    if (strcmp(project_type, "console") == 0) {
        fprintf(fp, "function int main() {\n");
        fprintf(fp, "    println(\"Hello, World!\");\n");
        fprintf(fp, "    return 0;\n");
        fprintf(fp, "}\n");
    } else if (strcmp(project_type, "system") == 0) {
        fprintf(fp, "function int main() {\n");
        fprintf(fp, "    return 0;\n");
        fprintf(fp, "}\n");
    } else if (strcmp(project_type, "library") == 0) {
        fprintf(fp, "function int add(int a, int b) {\n");
        fprintf(fp, "    return a + b;\n");
        fprintf(fp, "}\n");
    } else if (strcmp(project_type, "web") == 0) {
        fprintf(fp, "function void handleRequest() {\n");
        fprintf(fp, "    // None\n");
        fprintf(fp, "}\n");
    }

    fclose(fp);
    KrtPlatformDestroy(platform);

    return 0;
}
