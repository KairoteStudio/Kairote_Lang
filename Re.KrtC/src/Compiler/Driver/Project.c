#include "Project.h"
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define KRT_MKDIR(path) _mkdir(path)
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#endif
#else
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#define KRT_MKDIR(path) mkdir(path, 0775)
#endif

#ifndef KRT_MAX_PATH
#define KRT_MAX_PATH 1024
#endif

static char path_separator(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

static int path_is_separator(char c) {
    return (c == '/') || (c == '\\');
}

static int ensure_directory_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
#ifdef _WIN32
        return (st.st_mode & _S_IFDIR) != 0 ? 0 : -1;
#else
        return S_ISDIR(st.st_mode) ? 0 : -1;
#endif
    }
    if (KRT_MKDIR(path) == 0) {
        return 0;
    }
    if (errno == EEXIST && stat(path, &st) == 0) {
#ifdef _WIN32
        return (st.st_mode & _S_IFDIR) != 0 ? 0 : -1;
#else
        return S_ISDIR(st.st_mode) ? 0 : -1;
#endif
    }
    return -1;
}

static int copy_file_checked(const char* source, const char* destination) {
    FILE* src = fopen(source, "rb");
    if (!src) {
        return -1;
    }
    FILE* dst = fopen(destination, "wb");
    if (!dst) {
        fclose(src);
        return -1;
    }

    int result = 0;
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), src)) != 0) {
        if (fwrite(buffer, 1, count, dst) != count) {
            result = -1;
            break;
        }
    }
    if (ferror(src)) {
        result = -1;
    }
    if (fclose(src) != 0) {
        result = -1;
    }
    if (fclose(dst) != 0) {
        result = -1;
    }
    return result;
}

static int copy_directory_recursive(const char* src_dir, const char* dst_dir, int depth) {
    if (depth > 64) {
        KrtError("复制目录层级过深: %s", src_dir);
        return -1;
    }
    if (ensure_directory_exists(dst_dir) != 0) {
        return -1;
    }
#ifdef _WIN32
    char search_path[KRT_MAX_PATH];
    int search_length = snprintf(search_path, sizeof(search_path), "%s\\*", src_dir);
    if (search_length < 0 || (size_t)search_length >= sizeof(search_path)) {
        return -1;
    }

    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileA(search_path, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) {
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    }

    int result = 0;
    do {
        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }

        char src_path[KRT_MAX_PATH];
        char dst_path[KRT_MAX_PATH];
        int src_length = snprintf(src_path, sizeof(src_path), "%s\\%s", src_dir, find_data.cFileName);
        int dst_length = snprintf(dst_path, sizeof(dst_path), "%s\\%s", dst_dir, find_data.cFileName);
        if (src_length < 0 || (size_t)src_length >= sizeof(src_path) || dst_length < 0 ||
            (size_t)dst_length >= sizeof(dst_path)) {
            result = -1;
            break;
        }

        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
            continue;
        }
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (copy_directory_recursive(src_path, dst_path, depth + 1) != 0) {
                result = -1;
                break;
            }
        } else {
            if (copy_file_checked(src_path, dst_path) != 0) {
                result = -1;
                break;
            }
        }
    } while (FindNextFileA(find_handle, &find_data));

    if (result == 0 && GetLastError() != ERROR_NO_MORE_FILES) {
        result = -1;
    }
    FindClose(find_handle);
    return result;
#else
    DIR* dir = opendir(src_dir);
    if (!dir) {
        return -1;
    }

    int result = 0;
    struct dirent* entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) {
            if (errno != 0) {
                result = -1;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char src_path[KRT_MAX_PATH];
        char dst_path[KRT_MAX_PATH];
        int src_length = snprintf(src_path, sizeof(src_path), "%s/%s", src_dir, entry->d_name);
        int dst_length = snprintf(dst_path, sizeof(dst_path), "%s/%s", dst_dir, entry->d_name);
        if (src_length < 0 || (size_t)src_length >= sizeof(src_path) || dst_length < 0 ||
            (size_t)dst_length >= sizeof(dst_path)) {
            result = -1;
            break;
        }

        struct stat st;
        if (lstat(src_path, &st) != 0) {
            result = -1;
            break;
        }
        if (S_ISLNK(st.st_mode)) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_directory_recursive(src_path, dst_path, depth + 1) != 0) {
                result = -1;
                break;
            }
        } else if (copy_file_checked(src_path, dst_path) != 0) {
            result = -1;
            break;
        }
    }

    if (closedir(dir) != 0) {
        result = -1;
    }
    return result;
#endif
}

static int join_path(char* buffer, size_t buffer_size, const char* base, const char* part) {
    if (!buffer || buffer_size == 0) {
        return 0;
    }
    if (!base || base[0] == '\0') {
        int length = snprintf(buffer, buffer_size, "%s", part ? part : "");
        return length >= 0 && (size_t)length < buffer_size;
    }
    if (!part || part[0] == '\0') {
        int length = snprintf(buffer, buffer_size, "%s", base);
        return length >= 0 && (size_t)length < buffer_size;
    }

    char sep = path_separator();
    size_t base_len = strlen(base);
    int need_sep = base_len > 0 && !path_is_separator(base[base_len - 1]);

    if (!need_sep) {
        int length = snprintf(buffer, buffer_size, "%s%s", base, part);
        return length >= 0 && (size_t)length < buffer_size;
    } else {
        int length = snprintf(buffer, buffer_size, "%s%c%s", base, sep, part);
        return length >= 0 && (size_t)length < buffer_size;
    }
}

static int create_directory_recursive(const char* path) {
    if (!path || path[0] == '\0') {
        return 0;
    }

    char temp[KRT_MAX_PATH];
    int copied = snprintf(temp, sizeof(temp), "%s", path);
    if (copied < 0 || (size_t)copied >= sizeof(temp)) {
        return -1;
    }
    size_t len = strlen(temp);
    if (len == 0) {
        return 0;
    }

    for (size_t i = 0; i < len; ++i) {
        if (path_is_separator(temp[i])) {
            char old = temp[i];
            temp[i] = '\0';
            if (temp[0] != '\0') {
                if (ensure_directory_exists(temp) != 0) {
                    temp[i] = old;
                    return -1;
                }
            }
            temp[i] = old;
        }
    }
    if (ensure_directory_exists(temp) != 0) {
        return -1;
    }
    return 0;
}

static int write_file_checked(const char* path, const char* content) {
    if (!path || !content) {
        return -1;
    }
    FILE* fp = fopen(path, "w");
    if (!fp) {
        return -1;
    }
    int result = fputs(content, fp) == EOF ? -1 : 0;
    if (fclose(fp) != 0) {
        result = -1;
    }
    return result;
}

static int write_krt_string_literal(FILE* fp, const char* value) {
    if (!fp || !value || fputc('"', fp) == EOF) {
        return -1;
    }
    for (const unsigned char* current = (const unsigned char*)value; *current != '\0'; current++) {
        if (*current == '"' || *current == '\\') {
            if (fputc('\\', fp) == EOF || fputc(*current, fp) == EOF) {
                return -1;
            }
        } else if (*current == '\n' || *current == '\r' || *current == '\t') {
            const char* escape = *current == '\n' ? "\\n" : (*current == '\r' ? "\\r" : "\\t");
            if (fputs(escape, fp) == EOF) {
                return -1;
            }
        } else if (*current < 32 || *current == 127 || fputc(*current, fp) == EOF) {
            return -1;
        }
    }
    return fputc('"', fp) == EOF ? -1 : 0;
}

static int resolve_bundled_stdlib(char* result, size_t result_size) {
    if (!result || result_size == 0) {
        return 0;
    }

    char exe_dir[KRT_MAX_PATH - 32];
#ifdef _WIN32
    GetModuleFileNameA(NULL, exe_dir, sizeof(exe_dir));
    exe_dir[sizeof(exe_dir) - 1] = '\0';
    char* last_sep = strrchr(exe_dir, '\\');
    if (last_sep) {
        *last_sep = '\0';
    }
#else
    ssize_t len = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
    if (len < 0) {
        KrtError("无法解析可执行文件路径: %s", strerror(errno));
        return 0;
    }
    exe_dir[len] = '\0';
    char* last_sep = strrchr(exe_dir, '/');
    if (last_sep) {
        *last_sep = '\0';
    }
#endif

    const char* candidates[] = {
        ".." KRT_PATH_SEPARATOR_STR "stdlib",
        "stdlib",
        ".." KRT_PATH_SEPARATOR_STR ".." KRT_PATH_SEPARATOR_STR "libs" KRT_PATH_SEPARATOR_STR "System",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char candidate[KRT_MAX_PATH];
        if (!join_path(candidate, sizeof(candidate), exe_dir, candidates[i])) {
            continue;
        }
        struct stat st;
        if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
            int length = snprintf(result, result_size, "%s", candidate);
            return length >= 0 && (size_t)length < result_size;
        }
    }
    return 0;
}

KrtProject* KrtProjCreate(const char* name, KrtProjectType type) {
    if (!name) {
        return NULL;
    }
    KrtProject* project = KRT_CALLOC(1, sizeof(KrtProject));
    if (!project) {
        return NULL;
    }

    project->name = KRT_STRDUP(name);
    project->version = KRT_STRDUP("1.0.0");
    project->type = type;
    project->root_namespace = KRT_STRDUP(name);
    project->description = KRT_STRDUP("");

    project->sdk_version = KRT_STRDUP("1.0.0");

    time_t now = time(NULL);
    char time_str[64];
    struct tm local_tm;
    struct tm* local_time = localtime_r(&now, &local_tm);
    if (!local_time || strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time) == 0) {
        KrtProjDestroy(project);
        return NULL;
    }
    project->created_date = KRT_STRDUP(time_str);
    project->modified_date = KRT_STRDUP(time_str);

    if (!project->name || !project->version || !project->root_namespace ||
        !project->description || !project->sdk_version || !project->created_date || !project->modified_date) {
        KrtProjDestroy(project);
        return NULL;
    }

    return project;
}

void KrtProjDestroy(KrtProject* project) {
    if (!project) {
        return;
    }

    KRT_FREE(project->name);
    KRT_FREE(project->version);
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

int KrtProjAddFile(KrtProject* project, const char* file_path, const char* item_type) {
    if (!project || !file_path) {
        return 0;
    }

    KrtProjectItem* item = KRT_CALLOC(1, sizeof(KrtProjectItem));
    if (!item) {
        return 0;
    }

    item->file_path = KRT_STRDUP(file_path);
    item->item_type = KRT_STRDUP(item_type ? item_type : "Compile");
    if (!item->file_path || !item->item_type) {
        KRT_FREE(item->file_path);
        KRT_FREE(item->item_type);
        KRT_FREE(item);
        return 0;
    }

    time_t now = time(NULL);
    char time_str[64];
    struct tm local_tm;
    struct tm* local_time = localtime_r(&now, &local_tm);
    if (!local_time || strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", local_time) == 0) {
        KRT_FREE(item->file_path);
        KRT_FREE(item->item_type);
        KRT_FREE(item);
        return 0;
    }
    char* modified_date = KRT_STRDUP(time_str);
    if (!modified_date) {
        KRT_FREE(item->file_path);
        KRT_FREE(item->item_type);
        KRT_FREE(item);
        return 0;
    }

    if (!project->items) {
        project->items = item;
    } else {
        KrtProjectItem* tail = project->items;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = item;
    }
    KRT_FREE(project->modified_date);
    project->modified_date = modified_date;
    return 1;
}

void KrtProjRemoveFile(KrtProject* project, const char* file_path) {
    if (!project || !file_path) {
        return;
    }

    KrtProjectItem** link = &project->items;
    while (*link) {
        KrtProjectItem* item = *link;
        if (item->file_path && strcmp(item->file_path, file_path) == 0) {
            *link = item->next;
            KRT_FREE(item->file_path);
            KRT_FREE(item->item_type);
            KRT_FREE(item);
            return;
        }
        link = &item->next;
    }
}

int KrtProjAddDependency(KrtProject* project, const char* name, const char* version, const char* path) {
    if (!project || !name) {
        return 0;
    }

    KrtProjectDependency* dep = KRT_CALLOC(1, sizeof(KrtProjectDependency));
    if (!dep) {
        return 0;
    }

    dep->name = KRT_STRDUP(name);
    dep->version = KRT_STRDUP(version ? version : "*");
    dep->path = path ? KRT_STRDUP(path) : NULL;
    if (!dep->name || !dep->version || (path && !dep->path)) {
        KRT_FREE(dep->name);
        KRT_FREE(dep->version);
        KRT_FREE(dep->path);
        KRT_FREE(dep);
        return 0;
    }

    dep->next = project->dependencies;
    project->dependencies = dep;
    return 1;
}

char** KrtProjGetSourceFiles(KrtProject* project, int* count) {
    if (!project || !count) {
        return NULL;
    }

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
            files[index] = KRT_STRDUP(item->file_path);
            if (!files[index]) {
                for (int i = 0; i < index; i++) {
                    KRT_FREE(files[i]);
                }
                KRT_FREE(files);
                *count = 0;
                KrtError("无法复制项目源文件列表");
                return NULL;
            }
            index++;
        }
        item = item->next;
    }

    *count = file_count;
    return files;
}

int KrtProjCreateTemplate(KrtProject* project, const char* output_dir) {
    if (!project || !output_dir) {
        KrtError("项目模板需要有效的项目和输出路径");
        return 0;
    }

    if (output_dir[0] == '\0') {
        output_dir = project->name ? project->name : "MyProject";
    }

    if (strlen(output_dir) >= KRT_MAX_PATH) {
        KrtError("项目输出路径过长");
        return 0;
    }

    char created_files[8][KRT_MAX_PATH];
    int created_count = 0;
    char stdlib_dst[KRT_MAX_PATH] = {0};
    char project_libs[KRT_MAX_PATH] = {0};

    if (!project->project_root) {
        project->project_root = KRT_STRDUP(output_dir);
        if (!project->project_root) {
            KrtError("无法分配项目根目录路径");
            goto fail;
        }
    }

    char stdlib_src[KRT_MAX_PATH];
    if (!resolve_bundled_stdlib(stdlib_src, sizeof(stdlib_src))) {
        KrtError("无法找到捆绑标准库；期望安装目录同级的 stdlib/");
        goto fail;
    }

    typedef struct {
        const char* name;
        const char* content;
    } TemplateFile;

    static const TemplateFile console_templates[] = {{"main.krt", "using System;\n\n"
                                                                  "function main() {\n"
                                                                  "    print(\"Hello, Kairote Lang!\");\n"
                                                                  "}\n"}};

    static const TemplateFile library_templates[] = {{"library.krt", "function add(int32 a, int32 b) {\n"
                                                                     "    return a + b;\n"
                                                                     "}\n"},
                                                     {"exports.krt", "function export_symbols() {\n"
                                                                     "}\n"}};

    static const TemplateFile web_templates[] = {{"app.krt", "using System;\n\n"
                                                             "function handle_request() {\n"
                                                             "    print(\"HTTP/1.1 200 OK\\r\\n\");\n"
                                                             "    print(\"Content-Type: text/html\\r\\n\\r\\n\");\n"
                                                             "    print(\"<h1>Hello from Kairote Lang Web!</h1>\");\n"
                                                             "}\n"},
                                                 {"routes.krt", "function configure_routes() {\n"
                                                                "}\n"}};

    static const TemplateFile system_templates[] = {{"kernel.krt",
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
                                                     "    msg: db \"System.Everything is ready\", 10\n\n"},
                                                    {"drivers.krt", "function init_drivers() {\n"
                                                                    "}\n"}};

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

    if (create_directory_recursive(output_dir) != 0) {
        KrtError("无法创建项目目录: %s", output_dir);
        goto fail;
    }

    char sources_buf[512] = "";
    for (size_t i = 0; i < template_count; ++i) {
        const TemplateFile* tpl = &templates[i];
        if (!KrtProjAddFile(project, tpl->name, "Compile")) {
            KrtError("无法记录项目模板源文件: %s", tpl->name);
            goto fail;
        }

        char file_path[KRT_MAX_PATH];
        if (!join_path(file_path, sizeof(file_path), output_dir, tpl->name)) {
            KrtError("项目模板文件路径过长: %s", tpl->name);
            goto fail;
        }
        if (write_file_checked(file_path, tpl->content) != 0) {
            KrtError("无法写入项目模板文件: %s", file_path);
            goto fail;
        }
        if (created_count < 8) {
            KRT_STRNCPY(created_files[created_count], file_path, KRT_MAX_PATH);
            created_count++;
        }

        size_t sources_length = strlen(sources_buf);
        size_t template_name_length = strlen(tpl->name);
        if (sources_length + template_name_length + 2 > sizeof(sources_buf)) {
            KrtError("项目模板源文件列表过长");
            goto fail;
        }
        memcpy(sources_buf + sources_length, tpl->name, template_name_length);
        sources_buf[sources_length + template_name_length] = ' ';
        sources_buf[sources_length + template_name_length + 1] = '\0';
    }

    char project_krt_path[KRT_MAX_PATH];
    if (!join_path(project_krt_path, sizeof(project_krt_path), output_dir, "project.krt")) {
        KrtError("项目配置文件路径过长");
        goto fail;
    }
    {
        FILE* pk = fopen(project_krt_path, "w");
        if (!pk) {
            KrtError("无法创建项目配置文件: %s", project_krt_path);
            goto fail;
        }

        const char* proj_name = project->name ? project->name : output_dir;
        const char* type_str = "console";
        switch (project->type) {
        case KRT_PROJ_TYPE_LIBRARY:
            type_str = "library";
            break;
        case KRT_PROJ_TYPE_WEB:
            type_str = "web";
            break;
        case KRT_PROJ_TYPE_SYSTEM:
            type_str = "system";
            break;
        default:
            break;
        }

        int project_file_ok = 1;
        project_file_ok &= fprintf(pk, "// Kairote Lang project\n") >= 0;
        project_file_ok &= fprintf(pk, "function Configure(Project p) {\n") >= 0;
        project_file_ok &= fprintf(pk, "    p.Name(") >= 0;
        project_file_ok &= write_krt_string_literal(pk, proj_name) == 0;
        project_file_ok &= fprintf(pk, ");\n") >= 0;
        project_file_ok &= fprintf(pk, "    p.Type(\"%s\");\n", type_str) >= 0;
        project_file_ok &= fprintf(pk, "    p.Output(") >= 0;
        project_file_ok &= write_krt_string_literal(pk, proj_name) == 0;
        project_file_ok &= fprintf(pk, ");\n") >= 0;
        project_file_ok &= fprintf(pk, "    p.CompileTogether(true);\n") >= 0;
        if (sources_buf[0] != '\0') {
            size_t slen = strlen(sources_buf);
            while (slen > 0 && (sources_buf[slen - 1] == ' ' || sources_buf[slen - 1] == '\t')) {
                sources_buf[--slen] = '\0';
            }
            project_file_ok &= fprintf(pk, "    p.Sources(") >= 0;
            char* save = NULL;
            char* tok = strtok_r(sources_buf, " \t", &save);
            int first = 1;
            while (tok) {
                if (!first) {
                    project_file_ok &= fprintf(pk, ", ") >= 0;
                }
                project_file_ok &= write_krt_string_literal(pk, tok) == 0;
                first = 0;
                tok = strtok_r(NULL, " \t", &save);
            }
            project_file_ok &= fprintf(pk, ");\n") >= 0;
        }
        project_file_ok &= fprintf(pk, "}\n") >= 0;
        if (fclose(pk) != 0) {
            project_file_ok = 0;
        }
        if (!project_file_ok) {
            KrtError("写入项目配置文件失败: %s", project_krt_path);
            goto fail;
        }
        if (created_count < 8) {
            KRT_STRNCPY(created_files[created_count], project_krt_path, KRT_MAX_PATH);
            created_count++;
        }
    }

    if (!join_path(project_libs, sizeof(project_libs), output_dir, "libs") ||
        !join_path(stdlib_dst, sizeof(stdlib_dst), project_libs, "System")) {
        KrtError("标准库目标路径过长");
        goto fail;
    }
    if (create_directory_recursive(stdlib_dst) != 0 || copy_directory_recursive(stdlib_src, stdlib_dst, 0) != 0) {
        KrtError("无法复制标准库到项目目录: %s", stdlib_dst);
        goto fail;
    }

    return 1;

fail:
    for (int i = 0; i < created_count; i++) {
        KrtDeleteFile(created_files[i]);
    }
    KrtRemoveDirectory(stdlib_dst);
    KrtRemoveDirectory(project_libs);
    KrtRemoveDirectory(output_dir);
    return 0;
}

char** KrtProjGetTemplates(int* count) {
    if (!count) {
        return NULL;
    }

    static char* templates[] = {"console", "library", "web", "system"};

    *count = sizeof(templates) / sizeof(templates[0]);
    return templates;
}

char* KrtProjGetOutputPath(KrtProject* project, KrtProjectConfig config) {
    if (!project) {
        return NULL;
    }

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
    if (!project) {
        return NULL;
    }

    KrtProjectPropertyGroup* prop = project->property_groups;
    while (prop) {
        if (prop->config == config && prop->intermediate_path) {
            return prop->intermediate_path;
        }
        prop = prop->next;
    }

    return NULL;
}
