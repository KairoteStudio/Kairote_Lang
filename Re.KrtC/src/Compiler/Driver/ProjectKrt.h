#ifndef KRT_PROJECT_KRT_H
#define KRT_PROJECT_KRT_H

#include "../../Core/Utils/KrtCommon.h"
#include "Project.h"

typedef struct KrtProjectKrtConfig {
    char* project_name;
    char* output_name;
    KrtProjectType type;
    char** sources;
    int source_count;
    char** libraries;
    int library_count;
    char* base_dir;
} KrtProjectKrtConfig;

KrtProjectKrtConfig* KrtProjectKrtLoad(const char* path);
void KrtProjectKrtDestroy(KrtProjectKrtConfig* config);
int KrtProjectKrtSave(const KrtProjectKrtConfig* config, const char* path);
char* KrtProjectKrtResolveProjectFile(const char* base_dir);
char* KrtProjectKrtFindLibrary(const char* base_dir, const char* lib_name);

#endif
