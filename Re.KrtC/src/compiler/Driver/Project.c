#include "Project.h"
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define KRT_MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#define KRT_MKDIR(path) mkdir(path, 0775)
#endif

#ifndef KRT_MAX_PATH
#define KRT_MAX_PATH 1024
#endif

static char KrtPathSeparator(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

static int KrtPathIsSeparator(char c) {
    return (c == '/') || (c == '\\');
}

static int KrtCopyDirectoryRecursive(const char* src_dir, const char* dst_dir) {
#ifdef _WIN32
    
    KRT_MKDIR(dst_dir);
    
    char search_path[KRT_MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", src_dir);
    
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA(search_path, &find_data);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char src_path[KRT_MAX_PATH];
        char dst_path[KRT_MAX_PATH];
        snprintf(src_path, sizeof(src_path), "%s\\%s", src_dir, find_data.cFileName);
        snprintf(dst_path, sizeof(dst_path), "%s\\%s", dst_dir, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            
            KrtCopyDirectoryRecursive(src_path, dst_path);
        } else {
            
            FILE* src_fp = fopen(src_path, "rb");
            if (src_fp) {
                FILE* dst_fp = fopen(dst_path, "wb");
                if (dst_fp) {
                    char buffer[4096];
                    size_t bytes_read;
                    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
                        fwrite(buffer, 1, bytes_read, dst_fp);
                    }
                    fclose(dst_fp);
                }
                fclose(src_fp);
            }
        }
    } while (FindNextFileA(hFind, &find_data));
    
    FindClose(hFind);
#else
    
    mkdir(dst_dir, 0775);
    
    DIR* dir = opendir(src_dir);
    if (!dir) {
        return -1;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char src_path[KRT_MAX_PATH];
        char dst_path[KRT_MAX_PATH];
        snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, entry->d_name);
        
        struct stat st;
        if (stat(src_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                
                KrtCopyDirectoryRecursive(src_path, dst_path);
            } else {
                
                FILE* src_fp = fopen(src_path, "rb");
                if (src_fp) {
                    FILE* dst_fp = fopen(dst_path, "wb");
                    if (dst_fp) {
                        char buffer[4096];
                        size_t bytes_read;
                        while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
                            fwrite(buffer, 1, bytes_read, dst_fp);
                        }
                        fclose(dst_fp);
                    }
                    fclose(src_fp);
                }
            }
        }
    }
    
    closedir(dir);
#endif
    return 0;
}

static void KrtJoinPath(char* buffer, size_t buffer_size, const char* base, const char* part) {
    if (!buffer || buffer_size == 0) return;
    if (!base || base[0] == '\0') {
        snprintf(buffer, buffer_size, "%s", part ? part : "");
        return;
    }
    if (!part || part[0] == '\0') {
        snprintf(buffer, buffer_size, "%s", base);
        return;
    }

    char sep = KrtPathSeparator();
    size_t base_len = strlen(base);
    int need_sep = base_len > 0 && !KrtPathIsSeparator(base[base_len - 1]);

    if (!need_sep) {
        snprintf(buffer, buffer_size, "%s%s", base, part);
    } else {
        snprintf(buffer, buffer_size, "%s%c%s", base, sep, part);
    }
}

static int KrtCreateDirectoryRecursive(const char* path) {
    if (!path || path[0] == '\0') return 0;

    char temp[KRT_MAX_PATH];
    snprintf(temp, sizeof(temp), "%s", path);
    size_t len = strlen(temp);
    if (len == 0) return 0;

    for (size_t i = 0; i < len; ++i) {
        if (KrtPathIsSeparator(temp[i])) {
            char old = temp[i];
            temp[i] = '\0';
            if (temp[0] != '\0') {
                if (KRT_MKDIR(temp) != 0 && errno != EEXIST) {
                    temp[i] = old;
                    return -1;
                }
            }
            temp[i] = old;
        }
    }
    if (KRT_MKDIR(temp) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

static void KrtWriteFileIfPossible(const char* path, const char* content) {
    if (!path || !content) return;
    FILE* fp = fopen(path, "w");
    if (!fp) return;
    fputs(content, fp);
    fclose(fp);
}

KrtProject* KrtProjCreate(const char* name, KrtProjectType type) {
    KrtProject* project = KRT_CALLOC(1, sizeof(KrtProject));
    if (!project) return NULL;

    project->name = KRT_STRDUP(name);
    project->version = KRT_STRDUP("1.0.0");
    project->type = type;
    project->output_type = KRT_STRDUP(type == KRT_PROJ_TYPE_LIBRARY ? "dll" : "exe");
    project->root_namespace = KRT_STRDUP(name);
    project->description = KRT_STRDUP("");

    project->sdk_version = KRT_STRDUP("1.0.0");

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    project->created_date = KRT_STRDUP(time_str);
    project->modified_date = KRT_STRDUP(time_str);

    return project;
}

void KrtProjDestroy(KrtProject* project) {
    if (!project) return;

    KRT_FREE(project->name);
    KRT_FREE(project->version);
    KRT_FREE(project->output_type);
    KRT_FREE(project->root_namespace);
    KRT_FREE(project->description);
    KRT_FREE(project->sdk_version);
    KRT_FREE(project->created_date);
    KRT_FREE(project->modified_date);
    KRT_FREE(project->project_root);

    KrtProjectItem* item = project->items;
    while (item) {
        KrtProjectItem* next = item->next;
        KRT_FREE(item->file_path);
        KRT_FREE(item->item_type);
        KRT_FREE(item);
        item = next;
    }

    KrtProjectDependency* dep = project->dependencies;
    while (dep) {
        KrtProjectDependency* next = dep->next;
        KRT_FREE(dep->name);
        KRT_FREE(dep->version);
        KRT_FREE(dep->path);
        KRT_FREE(dep);
        dep = next;
    }

    KrtProjectPropertyGroup* prop = project->property_groups;
    while (prop) {
        KrtProjectPropertyGroup* next = prop->next;
        KRT_FREE(prop->output_path);
        KRT_FREE(prop->intermediate_path);
        KRT_FREE(prop->target_name);
        KRT_FREE(prop->defines);
        KRT_FREE(prop->include_paths);
        KRT_FREE(prop);
        prop = next;
    }

    KRT_FREE(project);
}

void KrtProjAddFile(KrtProject* project, const char* file_path, const char* item_type) {
    if (!project || !file_path) return;

    KrtProjectItem* item = KRT_CALLOC(1, sizeof(KrtProjectItem));
    if (!item) return;

    item->file_path = KRT_STRDUP(file_path);
    item->item_type = KRT_STRDUP(item_type ? item_type : "Compile");

    item->next = project->items;
    project->items = item;

    time_t now = time(NULL);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    KRT_FREE(project->modified_date);
    project->modified_date = KRT_STRDUP(time_str);
}

void KrtProjAddDependency(KrtProject* project, const char* name, const char* version, const char* path) {
    if (!project || !name) return;

    KrtProjectDependency* dep = KRT_CALLOC(1, sizeof(KrtProjectDependency));
    if (!dep) return;

    dep->name = KRT_STRDUP(name);
    dep->version = KRT_STRDUP(version ? version : "*");
    dep->path = path ? KRT_STRDUP(path) : NULL;

    dep->next = project->dependencies;
    project->dependencies = dep;
}

char** KrtProjGetSourceFiles(KrtProject* project, int* count) {
    if (!project || !count) return NULL;

    int file_count = 0;
    KrtProjectItem* item = project->items;
    while (item) {
        if (strcmp(item->item_type, "Compile") == 0) {
            file_count++;
        }
        item = item->next;
    }

    if (file_count == 0) {
        *count = 0;
        return NULL;
    }

    char** files = KRT_CALLOC(file_count, sizeof(char*));
    if (!files) {
        *count = 0;
        return NULL;
    }

    int index = 0;
    item = project->items;
    while (item && index < file_count) {
        if (strcmp(item->item_type, "Compile") == 0) {
            files[index++] = KRT_STRDUP(item->file_path);
        }
        item = item->next;
    }

    *count = file_count;
    return files;
}

int KrtProjCreateTemplate(KrtProject* project, const char* output_dir) {
    if (!project || !output_dir) return 0;

    if (output_dir[0] == '\0') {
        output_dir = project->name ? project->name : "MyProject";
    }

    if (!project->project_root) {
        project->project_root = KRT_STRDUP(output_dir);
    }

    typedef struct {
        const char* name;
        const char* content;
    } TemplateFile;

    static const TemplateFile console_templates[] = {
        {
            "main.krt",
            "function main() {\n"
            "    print(\"Hello, Kairote Lang!\");\n"
            "}\n"
        }
    };

    static const TemplateFile library_templates[] = {
        {
            "library.krt",
            "function add(int32 a, int32 b) {\n"
            "    return a + b;\n"
            "}\n"
        },
        {
            "exports.krt",
            "function export_symbols() {\n"
            "}\n"
        }
    };

    static const TemplateFile web_templates[] = {
        {
            "app.krt",
            "function handle_request() {\n"
            "    print(\"HTTP/1.1 200 OK\\r\\n\");\n"
            "    print(\"Content-Type: text/html\\r\\n\\r\\n\");\n"
            "    print(\"<h1>Hello from Kairote Lang Web!</h1>\");\n"
            "}\n"
        },
        {
            "routes.krt",
            "function configure_routes() {\n"
            "}\n"
        }
    };

    static const TemplateFile system_templates[] = {
        {
            "kernel.krt",
            "function void _start() asm {\n"
            "    mov rax, 1\n"
            "    mov rdi, 1\n"
            "    mov rsi, msg\n"
            "    mov rdx, 13\n"
            "    syscall\n"
            "    mov rax, 60\n"
            "    xor rdi, rdi\n"
            "    syscall\n"
            "}\n\n"
            "section .data\n"
            "    msg: db \"System.Everything is ready\", 10\n\n"
        },
        {
            "drivers.krt",
            "function init_drivers() {\n"
            "}\n"
        }
    };

    const TemplateFile* templates = console_templates;
    size_t template_count = sizeof(console_templates) / sizeof(console_templates[0]);

    switch (project->type) {
        case KRT_PROJ_TYPE_LIBRARY:
            templates = library_templates;
            template_count = sizeof(library_templates) / sizeof(library_templates[0]);
            break;
        case KRT_PROJ_TYPE_WEB:
            templates = web_templates;
            template_count = sizeof(web_templates) / sizeof(web_templates[0]);
            break;
        case KRT_PROJ_TYPE_SYSTEM:
            templates = system_templates;
            template_count = sizeof(system_templates) / sizeof(system_templates[0]);
            break;
        case KRT_PROJ_TYPE_CONSOLE:
        default:
            break;
    }

    if (KrtCreateDirectoryRecursive(output_dir) != 0) {
        return 0;
    }

    char sources_buf[512] = "";
    for (size_t i = 0; i < template_count; ++i) {
        const TemplateFile* tpl = &templates[i];
        KrtProjAddFile(project, tpl->name, "Compile");

        char file_path[KRT_MAX_PATH];
        KrtJoinPath(file_path, sizeof(file_path), output_dir, tpl->name);
        KrtWriteFileIfPossible(file_path, tpl->content);

        if (strlen(sources_buf) + strlen(tpl->name) + 1 < sizeof(sources_buf)) {
            strncat(sources_buf, tpl->name, sizeof(sources_buf) - strlen(sources_buf) - 1);
            strncat(sources_buf, " ", sizeof(sources_buf) - strlen(sources_buf) - 1);
        }
    }

    char project_krt_path[KRT_MAX_PATH];
    KrtJoinPath(project_krt_path, sizeof(project_krt_path), output_dir, "project.krt");
    {
        FILE* pk = fopen(project_krt_path, "w");
        if (pk) {
            const char* proj_name = project->name ? project->name : output_dir;
            const char* type_str = "console";
            switch (project->type) {
                case KRT_PROJ_TYPE_LIBRARY: type_str = "library"; break;
                case KRT_PROJ_TYPE_WEB: type_str = "web"; break;
                case KRT_PROJ_TYPE_SYSTEM: type_str = "system"; break;
                default: break;
            }
            
            fprintf(pk, "// Kairote Lang project\n");
            fprintf(pk, "function Configure(Project p) {\n");
            fprintf(pk, "    p.Name(\"%s\");\n", proj_name);
            fprintf(pk, "    p.Type(\"%s\");\n", type_str);
            fprintf(pk, "    p.Output(\"%s\");\n", proj_name);
            if (sources_buf[0] != '\0') {
                size_t slen = strlen(sources_buf);
                while (slen > 0 && (sources_buf[slen - 1] == ' ' || sources_buf[slen - 1] == '\t')) {
                    sources_buf[--slen] = '\0';
                }
                fprintf(pk, "    p.Sources(");
                char* save = NULL;
                char* tok = strtok_r(sources_buf, " \t", &save);
                int first = 1;
                while (tok) {
                    if (!first) fprintf(pk, ", ");
                    fprintf(pk, "\"%s\"", tok);
                    first = 0;
                    tok = strtok_r(NULL, " \t", &save);
                }
                fprintf(pk, ");\n");
            }
            fprintf(pk, "}\n");
            fclose(pk);
        }
    }

    char exe_dir[KRT_MAX_PATH - 32];
    char stdlib_src[KRT_MAX_PATH];
    char stdlib_dst[KRT_MAX_PATH];

#ifdef _WIN32
    GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
    exe_dir[sizeof(exe_dir) - 1] = '\0';
    char* last_sep = strrchr(exe_dir, '\\');
    if (last_sep) {
        *last_sep = '\0';
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len != -1) {
        exe_dir[len] = '\0';
        char* last_sep = strrchr(exe_dir, '/');
        if (last_sep) {
            *last_sep = '\0';
        }
    } else {
        strcpy(exe_dir, ".");
    }
#endif

    snprintf(stdlib_src, sizeof(stdlib_src), "%s%c..%cstdlib", exe_dir, KrtPathSeparator(), KrtPathSeparator());
    struct stat stdlib_stat;
    if (stat(stdlib_src, &stdlib_stat) != 0 || !(stdlib_stat.st_mode & S_IFDIR)) {

        snprintf(stdlib_src, sizeof(stdlib_src), "%s%cstdlib", exe_dir, KrtPathSeparator());
    }

    if (stat(stdlib_src, &stdlib_stat) == 0 && (stdlib_stat.st_mode & S_IFDIR)) {
        snprintf(stdlib_dst, sizeof(stdlib_dst), "%s%cstdlib", output_dir, KrtPathSeparator());
        KrtCopyDirectoryRecursive(stdlib_src, stdlib_dst);
    }

    return 1;
}

char** KrtProjGetTemplates(int* count) {
    if (!count) return NULL;

    static char* templates[] = {
        "console",
        "library",
        "web",
        "system"
    };

    *count = sizeof(templates) / sizeof(templates[0]);
    return templates;
}

char* KrtProjGetOutputPath(KrtProject* project, KrtProjectConfig config) {
    if (!project) return NULL;
    
    KrtProjectPropertyGroup* prop = project->property_groups;
    while (prop) {
        if (prop->config == config && prop->output_path) {
            return prop->output_path;
        }
        prop = prop->next;
    }
    
    return NULL;
}

char* KrtProjGetIntermediatePath(KrtProject* project, KrtProjectConfig config) {
    if (!project) return NULL;
    
    KrtProjectPropertyGroup* prop = project->property_groups;
    while (prop) {
        if (prop->config == config && prop->intermediate_path) {
            return prop->intermediate_path;
        }
        prop = prop->next;
    }
    
    return NULL;
}
