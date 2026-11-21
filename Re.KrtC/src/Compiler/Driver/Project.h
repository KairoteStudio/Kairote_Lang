#ifndef KRT_PROJ_H
#define KRT_PROJ_H

#include "../../Core/Utils/KrtCommon.h"

typedef enum { KRT_PROJ_TYPE_CONSOLE, KRT_PROJ_TYPE_LIBRARY, KRT_PROJ_TYPE_WEB, KRT_PROJ_TYPE_SYSTEM } KrtProjectType;

typedef enum { KRT_PROJ_CONFIG_DEBUG, KRT_PROJ_CONFIG_RELEASE, KRT_PROJ_CONFIG_CUSTOM } KrtProjectConfig;

typedef struct KrtProjectDependency {
    char* name;
    char* version;
    char* path;
    struct KrtProjectDependency* next;
} KrtProjectDependency;

typedef struct KrtProjectItem {
    char* file_path;
    char* item_type;
    struct KrtProjectItem* next;
} KrtProjectItem;

typedef struct KrtProjectPropertyGroup {
    KrtProjectConfig config;
    char* output_path;
    char* intermediate_path;
    char* target_name;
    char* defines;
    char* include_paths;
    int optimize;
    int debug_symbols;
    struct KrtProjectPropertyGroup* next;
} KrtProjectPropertyGroup;

typedef struct KrtProject {
    char* name;
    char* version;
    KrtProjectType type;
    char* root_namespace;
    char* description;

    KrtProjectItem* items;
    KrtProjectDependency* dependencies;
    KrtProjectPropertyGroup* property_groups;

    char* sdk_version;
    char* created_date;
    char* modified_date;

    char* project_root;
} KrtProject;

KrtProject* KrtProjCreate(const char* name, KrtProjectType type);
void KrtProjDestroy(KrtProject* project);

int KrtProjAddFile(KrtProject* project, const char* file_path, const char* item_type);
void KrtProjRemoveFile(KrtProject* project, const char* file_path);
int KrtProjAddDependency(KrtProject* project, const char* name, const char* version, const char* path);

char** KrtProjGetSourceFiles(KrtProject* project, int* count);
/* The returned path is owned by the project; callers must not free or modify it. */
char* KrtProjGetOutputPath(KrtProject* project, KrtProjectConfig config);
/* The returned path is owned by the project; callers must not free or modify it. */
char* KrtProjGetIntermediatePath(KrtProject* project, KrtProjectConfig config);

/*
 * A generated project.krt always has the form
 *     function Configure(Project p) { p.Name(...); p.Type(...); ... }
 * The `Project p` parameter is accepted for C#-style familiarity; the loader
 * evaluates the call names and does not bind the argument.
 */
int KrtProjCreateTemplate(KrtProject* project, const char* output_dir);
char** KrtProjGetTemplates(int* count);

#endif
