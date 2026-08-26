#include "ProjectKrt.h"
#include "ConfigManager.h"
#include "../../Core/Utils/Path.h"
#include "compiler/Frontend/Lexer/Tokenizer.h"
#include "compiler/Frontend/Parser/Parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef _WIN32
#include <dirent.h>
#endif

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t' || *e == '\n' || *e == '\r')) *e-- = '\0';
    return s;
}

static char* trim_copy(const char* s) {
    if (!s) return KRT_STRDUP("");
    char* copy = KRT_STRDUP(s);
    if (!copy) return NULL;
    return trim(copy);
}

static int tokenize_list(const char* value, char*** list, int* count, int* capacity) {
    if (!value) return *count;

    char* copy = KRT_STRDUP(value);
    if (!copy) return *count;
    char* p = copy;

    while (*p) {
        if (*p == ',' || *p == ';') {
            *p = ' ';
        }
        p++;
    }

    char* save = NULL;
    char* token = strtok_r(copy, " \t", &save);
    while (token) {
        char* trimmed = trim(token);
        if (*trimmed) {
            if (*count >= *capacity) {
                int new_cap = (*capacity == 0) ? 4 : (*capacity) * 2;
                char** new_list = (char**)KRT_REALLOC(*list, sizeof(char*) * new_cap);
                if (!new_list) {
                    KRT_FREE(copy);
                    return -1;
                }
                *list = new_list;
                *capacity = new_cap;
            }
            (*list)[(*count)++] = KRT_STRDUP(trimmed);
        }
        token = strtok_r(NULL, " \t", &save);
    }
    KRT_FREE(copy);
    return *count;
}

static int parse_project_type(const char* value, KrtProjectType* type) {
    if (strcmp(value, "console") == 0 || strcmp(value, "exe") == 0) {
        *type = KRT_PROJ_TYPE_CONSOLE;
        return 1;
    }
    if (strcmp(value, "library") == 0 || strcmp(value, "lib") == 0 || strcmp(value, "dll") == 0) {
        *type = KRT_PROJ_TYPE_LIBRARY;
        return 1;
    }
    if (strcmp(value, "web") == 0) { *type = KRT_PROJ_TYPE_WEB; return 1; }
    if (strcmp(value, "system") == 0) { *type = KRT_PROJ_TYPE_SYSTEM; return 1; }
    return 0;
}

static void extract_base_dir(KrtProjectKrtConfig* config, const char* path) {
    if (!config || !path) return;
    char* dir = KrtGetDirectory(path);
    if (dir && strlen(dir) > 0 && strcmp(dir, ".") != 0) {
        config->base_dir = dir;
    } else {
        config->base_dir = KRT_STRDUP("");
        KRT_FREE(dir);
    }
}

static void set_string(char** field, char* value) {
    if (!field) return;
    if (*field) KRT_FREE(*field);
    *field = value;
}

typedef struct {
    char* name;
    char* value;
} KrtProjVar;

typedef struct {
    KrtProjVar* vars;
    int var_count;
    int var_cap;
    KrtProjectKrtConfig* cfg;
} KrtProjEval;

static const char* krt_proj_builtin(const char* name, char* out, size_t out_size) {
    if (!name || !out) return NULL;
    if (strcmp(name, "os") == 0) {
#if defined(_WIN32)
        snprintf(out, out_size, "win32");
#elif defined(__APPLE__)
        snprintf(out, out_size, "darwin");
#else
        snprintf(out, out_size, "linux");
#endif
        return out;
    }
    if (strcmp(name, "platform") == 0) return krt_proj_builtin("os", out, out_size);
    if (strcmp(name, "config") == 0) {
        const char* cfg = getenv("KRT_CONFIG");
        snprintf(out, out_size, "%s", (cfg && cfg[0]) ? cfg : "release");
        return out;
    }
    return NULL;
}

static const char* krt_proj_lookup(KrtProjEval* ev, const char* name, char* out, size_t out_size) {
    for (int i = 0; i < ev->var_count; i++) {
        if (strcmp(ev->vars[i].name, name) == 0) return ev->vars[i].value;
    }
    return krt_proj_builtin(name, out, out_size);
}

static void krt_proj_set_var(KrtProjEval* ev, const char* name, const char* value) {
    for (int i = 0; i < ev->var_count; i++) {
        if (strcmp(ev->vars[i].name, name) == 0) {
            KRT_FREE(ev->vars[i].value);
            ev->vars[i].value = KRT_STRDUP(value ? value : "");
            return;
        }
    }
    if (ev->var_count >= ev->var_cap) {
        int cap = ev->var_cap == 0 ? 4 : ev->var_cap * 2;
        KrtProjVar* nv = (KrtProjVar*)KRT_REALLOC(ev->vars, sizeof(KrtProjVar) * (size_t)cap);
        if (!nv) return;
        ev->vars = nv; ev->var_cap = cap;
    }
    ev->vars[ev->var_count].name = KRT_STRDUP(name);
    ev->vars[ev->var_count].value = KRT_STRDUP(value ? value : "");
    ev->var_count++;
}

static char* krt_proj_expand(KrtProjEval* ev, const char* s) {
    size_t cap = strlen(s) + 32, len = 0;
    char* buf = (char*)KRT_MALLOC(cap);
    if (!buf) return NULL;
    const char* p = s;
    while (p && *p) {
        if (p[0] == '$' && p[1] == '{') {
            const char* close = strchr(p + 2, '}');
            if (!close) { buf[len++] = *p++; continue; }
            char namebuf[512];
            size_t nlen = (size_t)(close - (p + 2));
            if (nlen >= sizeof(namebuf)) goto copy_literal;
            memcpy(namebuf, p + 2, nlen); namebuf[nlen] = '\0';

            const char* val = NULL;
            char varbuf[512];
            if (strncmp(namebuf, "env:", 4) == 0) {
                val = getenv(namebuf + 4);
                if (!val) val = "";
            } else {
                val = krt_proj_lookup(ev, namebuf, varbuf, sizeof(varbuf));
                if (!val) {
                    KRT_WARNING("project.krt: unknown variable '${%s}'", namebuf);
                    val = "";
                }
            }
            size_t vlen = strlen(val);
            if (len + vlen + 1 > cap) {
                cap = (len + vlen + 1) * 2 + 16;
                char* nb = (char*)KRT_REALLOC(buf, cap);
                if (!nb) { KRT_FREE(buf); return NULL; }
                buf = nb;
            }
            memcpy(buf + len, val, vlen); len += vlen;
            p = close + 1;
            continue;
        }
copy_literal:
        if (len + 2 > cap) {
            cap = cap * 2 + 16;
            char* nb = (char*)KRT_REALLOC(buf, cap);
            if (!nb) { KRT_FREE(buf); return NULL; }
            buf = nb;
        }
        buf[len++] = *p++;
    }
    buf[len] = '\0';
    return buf;
}

static int krt_glob_match(const char* pat, const char* str) {
    const char *p = pat, *s = str, *star = NULL, *ss = NULL;
    while (*s) {
        if (*p == '?') { p++; s++; }
        else if (*p == '*') { star = ++p; ss = s; }
        else if (*p == *s) { p++; s++; }
        else if (star) { p = star; s = ++ss; }
        else return 0;
    }
    while (*p == '*') p++;
    return *p == '\0';
}

static int krt_list_add(char*** list, int* count, int* cap, const char* value) {
    if (!value || !*value) return *count;
    if (*count >= *cap) {
        int nc = (*cap == 0) ? 4 : (*cap) * 2;
        char** nl = (char**)KRT_REALLOC(*list, sizeof(char*) * (size_t)nc);
        if (!nl) return -1;
        *list = nl; *cap = nc;
    }
    (*list)[(*count)++] = KRT_STRDUP(value);
    return *count;
}

static int krt_glob_append(const char* base_dir, const char* pattern,
                           char*** list, int* count, int* cap) {
    if (!pattern) return *count;
    if (!strchr(pattern, '*') && !strchr(pattern, '?')) {
        return krt_list_add(list, count, cap, pattern);
    }

    char dirbuf[KRT_MAX_PATH];
    const char* slash = strrchr(pattern, '/');
#if defined(_WIN32)
    const char* bslash = strrchr(pattern, '\\');
    if (!slash || (bslash && bslash > slash)) slash = bslash;
#endif
    const char* filepat;
    if (slash) {
        size_t dlen = (size_t)(slash - pattern);
        if (dlen >= sizeof(dirbuf)) dlen = sizeof(dirbuf) - 1;
        memcpy(dirbuf, pattern, dlen); dirbuf[dlen] = '\0';
        filepat = slash + 1;
    } else {
        dirbuf[0] = '\0';
        filepat = pattern;
    }

    char dirpath[KRT_MAX_PATH];
    if (base_dir && base_dir[0] && dirbuf[0])
        snprintf(dirpath, sizeof(dirpath), "%s%c%s", base_dir, KRT_PATH_SEPARATOR, dirbuf);
    else if (base_dir && base_dir[0])
        snprintf(dirpath, sizeof(dirpath), "%s", base_dir);
    else if (dirbuf[0])
        snprintf(dirpath, sizeof(dirpath), "%s", dirbuf);
    else
        snprintf(dirpath, sizeof(dirpath), "%s", ".");

#if defined(_WIN32)
    char full_pat[KRT_MAX_PATH];
    snprintf(full_pat, sizeof(full_pat), "%s%c%s", dirpath, KRT_PATH_SEPARATOR, filepat);
    struct _finddata_t fd;
    intptr_t h = _findfirst(full_pat, &fd);
    if (h == -1) return *count;
    do {
        if (strcmp(fd.name, ".") == 0 || strcmp(fd.name, "..") == 0) continue;
        char rel[KRT_MAX_PATH];
        if (dirbuf[0]) snprintf(rel, sizeof(rel), "%s%c%s", dirbuf, KRT_PATH_SEPARATOR, fd.name);
        else snprintf(rel, sizeof(rel), "%s", fd.name);
        if (krt_list_add(list, count, cap, rel) < 0) { _findclose(h); return -1; }
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR* d = opendir(dirpath);
    if (!d) return *count;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        if (!krt_glob_match(filepat, e->d_name)) continue;
        char rel[KRT_MAX_PATH];
        if (dirbuf[0]) snprintf(rel, sizeof(rel), "%s%c%s", dirbuf, KRT_PATH_SEPARATOR, e->d_name);
        else snprintf(rel, sizeof(rel), "%s", e->d_name);
        if (krt_list_add(list, count, cap, rel) < 0) { closedir(d); return -1; }
    }
    closedir(d);
#endif
    return *count;
}

static int krt_proj_add_sources(KrtProjEval* ev, char*** list, int* count, int* cap, const char* spec) {
    if (!spec) return *count;
    char* copy = KRT_STRDUP(spec);
    char* p = copy;
    while (*p) { if (*p == ',' || *p == ';') *p = ' '; p++; }
    char* save = NULL;
    char* tok = strtok_r(copy, " \t", &save);
    while (tok) {
        char* item = trim(tok);
        if (*item) {
            char* exp = krt_proj_expand(ev, item);
            if (exp) {
                if (krt_glob_append(ev->cfg->base_dir, exp, list, count, cap) < 0) {
                    KRT_FREE(exp); KRT_FREE(copy); return -1;
                }
                KRT_FREE(exp);
            }
        }
        tok = strtok_r(NULL, " \t", &save);
    }
    KRT_FREE(copy);
    return *count;
}

static int krt_proj_add_libraries(KrtProjEval* ev, char*** list, int* count, int* cap, const char* spec) {
    if (!spec) return *count;
    char* copy = KRT_STRDUP(spec);
    char* p = copy;
    while (*p) { if (*p == ',' || *p == ';') *p = ' '; p++; }
    char* save = NULL;
    char* tok = strtok_r(copy, " \t", &save);
    while (tok) {
        char* item = trim(tok);
        if (*item) {
            char* exp = krt_proj_expand(ev, item);
            if (exp) {
                if (krt_list_add(list, count, cap, exp) < 0) {
                    KRT_FREE(exp); KRT_FREE(copy); return -1;
                }
                KRT_FREE(exp);
            }
        }
        tok = strtok_r(NULL, " \t", &save);
    }
    KRT_FREE(copy);
    return *count;
}

static void krt_proj_eval_destroy(KrtProjEval* ev) {
    if (!ev) return;
    for (int i = 0; i < ev->var_count; i++) {
        KRT_FREE(ev->vars[i].name);
        KRT_FREE(ev->vars[i].value);
    }
    KRT_FREE(ev->vars);
    ev->vars = NULL; ev->var_count = 0; ev->var_cap = 0;
}

static const char* krt_proj_str_arg(KrtProjEval* ev, ASTNode* arg, char** out) {
    if (!arg) return NULL;
    if (arg->type == AST_STRING) {
        *out = krt_proj_expand(ev, arg->data.string_value);
        return *out;
    }
    if (arg->type == AST_IDENTIFIER) {
        char buf[512];
        const char* v = krt_proj_lookup(ev, arg->data.identifier_name, buf, sizeof(buf));
        if (!v) return NULL;
        *out = KRT_STRDUP(v);
        return *out;
    }
    return NULL;
}

static int krt_proj_eval_block(KrtProjEval* ev, ASTNode* b);

static int krt_proj_eval_condition(KrtProjEval* ev, ASTNode* cond) {
    if (!cond) return 0;
    if (cond->type == AST_BINARY_OPERATION &&
        (cond->data.binary_op.operator == TOKEN_EQUAL ||
         cond->data.binary_op.operator == TOKEN_NOT_EQUAL)) {
        ASTNode* l = cond->data.binary_op.left;
        ASTNode* r = cond->data.binary_op.right;
        char lbuf[512], rbuf[512];
        const char* lval = NULL, *rval = NULL;
        if (l && l->type == AST_IDENTIFIER) lval = krt_proj_lookup(ev, l->data.identifier_name, lbuf, sizeof(lbuf));
        else if (l && l->type == AST_STRING) lval = l->data.string_value;
        if (r && r->type == AST_IDENTIFIER) rval = krt_proj_lookup(ev, r->data.identifier_name, rbuf, sizeof(rbuf));
        else if (r && r->type == AST_STRING) rval = r->data.string_value;
        int eq = strcmp(lval ? lval : "", rval ? rval : "") == 0;
        return cond->data.binary_op.operator == TOKEN_EQUAL ? eq : !eq;
    }
    if (cond->type == AST_IDENTIFIER) {
        char buf[64];
        const char* v = krt_proj_lookup(ev, cond->data.identifier_name, buf, sizeof(buf));
        if (v) return strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
    }
    return 0;
}

static int krt_proj_eval_call(KrtProjEval* ev, ASTNode* c) {
    if (!c || c->type != AST_CALL) return 0;
    const char* m = c->data.call.name;
    if (!m) return 0;

    if (strcmp(m, "Name") == 0 || strcmp(m, "name") == 0) {
        if (c->data.call.argument_count >= 1) {
            char* alloc = NULL;
            const char* v = krt_proj_str_arg(ev, c->data.call.arguments[0], &alloc);
            if (v) set_string(&ev->cfg->project_name, KRT_STRDUP(v));
            if (alloc) KRT_FREE(alloc);
        }
        return 0;
    }
    if (strcmp(m, "Type") == 0 || strcmp(m, "type") == 0) {
        if (c->data.call.argument_count >= 1) {
            char* alloc = NULL;
            const char* v = krt_proj_str_arg(ev, c->data.call.arguments[0], &alloc);
            if (v) parse_project_type(v, &ev->cfg->type);
            if (alloc) KRT_FREE(alloc);
        }
        return 0;
    }
    if (strcmp(m, "Output") == 0 || strcmp(m, "output") == 0) {
        if (c->data.call.argument_count >= 1) {
            char* alloc = NULL;
            const char* v = krt_proj_str_arg(ev, c->data.call.arguments[0], &alloc);
            if (v) set_string(&ev->cfg->output_name, KRT_STRDUP(v));
            if (alloc) KRT_FREE(alloc);
        }
        return 0;
    }
    if (strcmp(m, "Sources") == 0 || strcmp(m, "sources") == 0) {
        int cap = ev->cfg->source_count == 0 ? 0 : ev->cfg->source_count;
        for (int i = 0; i < c->data.call.argument_count; i++) {
            char* alloc = NULL;
            const char* v = krt_proj_str_arg(ev, c->data.call.arguments[i], &alloc);
            if (v && krt_proj_add_sources(ev, &ev->cfg->sources, &ev->cfg->source_count, &cap, v) < 0) {
                if (alloc) KRT_FREE(alloc);
                return -1;
            }
            if (alloc) KRT_FREE(alloc);
        }
        return 0;
    }
    if (strcmp(m, "Libraries") == 0 || strcmp(m, "libraries") == 0 ||
        strcmp(m, "Using") == 0 || strcmp(m, "using") == 0) {
        int cap = ev->cfg->library_count == 0 ? 0 : ev->cfg->library_count;
        for (int i = 0; i < c->data.call.argument_count; i++) {
            char* alloc = NULL;
            const char* v = krt_proj_str_arg(ev, c->data.call.arguments[i], &alloc);
            if (v && krt_proj_add_libraries(ev, &ev->cfg->libraries, &ev->cfg->library_count, &cap, v) < 0) {
                if (alloc) KRT_FREE(alloc);
                return -1;
            }
            if (alloc) KRT_FREE(alloc);
        }
        return 0;
    }
    return 0;
}

static int krt_proj_eval_stmt(KrtProjEval* ev, ASTNode* s) {
    if (!s) return 0;
    switch (s->type) {
        case AST_VARIABLE_DECLARATION: {
            const char* vname = s->data.variable_decl.name;
            ASTNode* value = s->data.variable_decl.value;
            char* alloc = NULL;
            const char* v = NULL;
            if (value && value->type == AST_STRING) {
                alloc = krt_proj_expand(ev, value->data.string_value);
                v = alloc;
            }
            if (vname) krt_proj_set_var(ev, vname, v ? v : "");
            if (alloc) KRT_FREE(alloc);
            return 0;
        }
        case AST_CALL:
            return krt_proj_eval_call(ev, s);
        case AST_IF_STATEMENT:
            if (krt_proj_eval_condition(ev, s->data.if_stmt.condition)) {
                return krt_proj_eval_block(ev, s->data.if_stmt.then_branch);
            } else if (s->data.if_stmt.else_branch) {
                return krt_proj_eval_stmt(ev, s->data.if_stmt.else_branch);
            }
            return 0;
        case AST_BLOCK:
            return krt_proj_eval_block(ev, s);
        default:
            return 0;
    }
}

static int krt_proj_eval_block(KrtProjEval* ev, ASTNode* b) {
    if (!b || b->type != AST_BLOCK) return 0;
    for (int i = 0; i < b->data.block.statement_count; i++) {
        if (krt_proj_eval_stmt(ev, b->data.block.statements[i]) != 0) return -1;
    }
    return 0;
}

static void krt_proj_eval_top_level_vars(KrtProjEval* ev, ASTNode* ast) {
    if (!ev || !ast) return;
    ASTNode* listnode = ast;
    if (ast->type == AST_NAMESPACE_DECLARATION) {
        listnode = ast->data.namespace_decl.body;
    }
    if (!listnode || listnode->type != AST_PROGRAM) return;
    for (int i = 0; i < listnode->data.block.statement_count; i++) {
        ASTNode* s = listnode->data.block.statements[i];
        if (s && s->type == AST_VARIABLE_DECLARATION) {
            krt_proj_eval_stmt(ev, s);
        }
    }
}

static ASTNode* krt_proj_find_configure(ASTNode* ast) {
    if (!ast) return NULL;
    if (ast->type == AST_PROGRAM) {
        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* f = krt_proj_find_configure(ast->data.block.statements[i]);
            if (f) return f;
        }
    } else if (ast->type == AST_NAMESPACE_DECLARATION) {
        return krt_proj_find_configure(ast->data.namespace_decl.body);
    } else if (ast->type == AST_FUNCTION_DECLARATION || ast->type == AST_STATIC_FUNCTION_DECLARATION) {
        const char* nm = (ast->type == AST_FUNCTION_DECLARATION)
            ? ast->data.function_decl.name : ast->data.static_function_decl.name;
        if (nm && strcmp(nm, "Configure") == 0) return ast;
    }
    return NULL;
}

KrtProjectKrtConfig* KrtProjectKrtLoad(const char* path) {
    if (!path) return NULL;

    FILE* fp = fopen(path, "r");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    rewind(fp);
    if (fsize < 0) { fclose(fp); return NULL; }

    char* source = (char*)KRT_MALLOC((size_t)fsize + 1);
    if (!source) { fclose(fp); return NULL; }
    size_t read = fread(source, 1, (size_t)fsize, fp);
    source[read] = '\0';
    fclose(fp);

    KrtProjectKrtConfig* config = (KrtProjectKrtConfig*)KRT_CALLOC(1, sizeof(KrtProjectKrtConfig));
    if (!config) { KRT_FREE(source); return NULL; }
    config->type = KRT_PROJ_TYPE_CONSOLE;
    extract_base_dir(config, path);

    KrtProjEval ev;
    memset(&ev, 0, sizeof(ev));
    ev.cfg = config;

    Lexer* lexer = lexer_create(source);
    Parser* parser = lexer ? parser_create(lexer) : NULL;
    if (!parser) {
        KRT_FREE(source);
        KrtProjectKrtDestroy(config);
        KrtError("project.krt: 无法解析项目文件 '%s'", path);
        return NULL;
    }

    ASTNode* ast = parser_parse(parser);
    ASTNode* configure = ast ? krt_proj_find_configure(ast) : NULL;
    if (!configure) {
        parser_destroy(parser);
        lexer_destroy(lexer);
        KRT_FREE(source);
        KrtProjectKrtDestroy(config);
        KrtError("project.krt: 未找到 Configure 函数: %s", path);
        return NULL;
    }

    ASTNode* body = (configure->type == AST_FUNCTION_DECLARATION)
        ? configure->data.function_decl.body : configure->data.static_function_decl.body;

    krt_proj_eval_top_level_vars(&ev, ast);

    int rc = krt_proj_eval_block(&ev, body);

    krt_proj_eval_destroy(&ev);
    parser_destroy(parser);
    lexer_destroy(lexer);
    KRT_FREE(source);

    if (rc != 0) {
        KrtProjectKrtDestroy(config);
        return NULL;
    }

    if (!config->project_name) config->project_name = KRT_STRDUP("Untitled");
    if (!config->output_name) {
        config->output_name = KRT_STRDUP(config->project_name ? config->project_name : "output");
    }

    fprintf(stderr, "[KrtProj.debug] name='%s' out='%s' type=%d\n", config->project_name, config->output_name, (int)config->type);
    for (int i = 0; i < config->source_count; i++) fprintf(stderr, "[KrtProj.debug]   src[%d]=%s\n", i, config->sources[i]);
    for (int i = 0; i < config->library_count; i++) fprintf(stderr, "[KrtProj.debug]   lib[%d]=%s\n", i, config->libraries[i]);

    return config;
}

void KrtProjectKrtDestroy(KrtProjectKrtConfig* config) {
    if (!config) return;
    set_string(&config->project_name, NULL);
    set_string(&config->output_name, NULL);
    set_string(&config->base_dir, NULL);
    for (int i = 0; i < config->source_count; i++) {
        if (config->sources[i]) KRT_FREE(config->sources[i]);
    }
    KRT_FREE(config->sources);
    for (int i = 0; i < config->library_count; i++) {
        if (config->libraries[i]) KRT_FREE(config->libraries[i]);
    }
    KRT_FREE(config->libraries);
    KRT_FREE(config);
}

int KrtProjectKrtSave(const KrtProjectKrtConfig* config, const char* path) {
    if (!config || !path) return -1;

    FILE* fp = fopen(path, "w");
    if (!fp) return -1;

    fprintf(fp, "# Kairote Lang project file (.csproj equivalent)\n");
    fprintf(fp, "project: %s\n", config->project_name ? config->project_name : "Untitled");
    fprintf(fp, "type: console\n");
    switch (config->type) {
        case KRT_PROJ_TYPE_LIBRARY: fprintf(fp, "type: library\n"); break;
        case KRT_PROJ_TYPE_WEB:     fprintf(fp, "type: web\n"); break;
        case KRT_PROJ_TYPE_SYSTEM:  fprintf(fp, "type: system\n"); break;
        default: break;
    }
    fprintf(fp, "output: %s\n", config->output_name ? config->output_name : "output");

    if (config->source_count > 0) {
        fprintf(fp, "sources: ");
        for (int i = 0; i < config->source_count; i++) {
            if (config->sources[i]) fprintf(fp, "%s ", config->sources[i]);
        }
        fprintf(fp, "\n");
    }

    if (config->library_count > 0) {
        fprintf(fp, "libraries: ");
        for (int i = 0; i < config->library_count; i++) {
            if (config->libraries[i]) fprintf(fp, "%s ", config->libraries[i]);
        }
        fprintf(fp, "\n");
    } else {
        fprintf(fp, "# libraries: Sys\n");
    }

    fclose(fp);
    return 0;
}

char* KrtProjectKrtResolveProjectFile(const char* base_dir) {
    char path[KRT_MAX_PATH];
    if (base_dir && base_dir[0] != '\0') {
        snprintf(path, sizeof(path), "%s%cproject.krt", base_dir, KRT_PATH_SEPARATOR);
    } else {
        snprintf(path, sizeof(path), "project.krt");
    }
    if (KrtPathExists(path)) {
        return KRT_STRDUP(path);
    }
    return NULL;
}

static int probe_path(const char* base, const char* rel, char* out, size_t out_size) {
    if (base && base[0] != '\0') {
        snprintf(out, out_size, "%s%c%s%c%s.kro", base, KRT_PATH_SEPARATOR, rel, KRT_PATH_SEPARATOR, rel);
        if (KrtPathExists(out)) return 1;
    } else {
        snprintf(out, out_size, "%s%c%s.kro", rel, KRT_PATH_SEPARATOR, rel);
        if (KrtPathExists(out)) return 1;
    }
    return 0;
}

static int probe_flat(const char* base, const char* prefix, const char* lib_name, char* out, size_t out_size) {
    if (base && base[0] != '\0') {
        if (prefix && prefix[0] != '\0') {
            snprintf(out, out_size, "%s%c%s%c%s.kro", base, KRT_PATH_SEPARATOR, prefix, KRT_PATH_SEPARATOR, lib_name);
        } else {
            snprintf(out, out_size, "%s%c%s.kro", base, KRT_PATH_SEPARATOR, lib_name);
        }
    } else {
        if (prefix && prefix[0] != '\0') {
            snprintf(out, out_size, "%s%c%s.kro", prefix, KRT_PATH_SEPARATOR, lib_name);
        } else {
            snprintf(out, out_size, "%s.kro", lib_name);
        }
    }
    return KrtPathExists(out);
}

static void collect_search_roots_unused(void) {}

char* KrtProjectKrtFindLibrary(const char* base_dir, const char* lib_name) {
    if (!lib_name || lib_name[0] == '\0') return NULL;

    char out[KRT_MAX_PATH];

    if (strchr(lib_name, '/') || strchr(lib_name, '\\'))
    {
        snprintf(out, sizeof(out), "%s", lib_name);
        if (KrtPathExists(out)) return KRT_STRDUP(out);
        if (base_dir && base_dir[0] != '\0') {
            snprintf(out, sizeof(out), "%s%c%s", base_dir, KRT_PATH_SEPARATOR, lib_name);
            if (KrtPathExists(out)) return KRT_STRDUP(out);
        }
    }

    if (probe_flat(base_dir, NULL, lib_name, out, sizeof(out))) return KRT_STRDUP(out);
    if (probe_flat(base_dir, "libs", lib_name, out, sizeof(out))) return KRT_STRDUP(out);
    if (probe_flat(base_dir, "libs/System", lib_name, out, sizeof(out))) return KRT_STRDUP(out);
    if (probe_path(base_dir, "libs", out, sizeof(out))) return KRT_STRDUP(out);

    if (probe_flat(".", NULL, lib_name, out, sizeof(out))) return KRT_STRDUP(out);
    if (probe_flat(".", "libs", lib_name, out, sizeof(out))) return KRT_STRDUP(out);
    if (probe_flat(".", "libs/System", lib_name, out, sizeof(out))) return KRT_STRDUP(out);

    char exe_dir[KRT_MAX_PATH];
    if (KrtGetExecutableDirectory(exe_dir, sizeof(exe_dir)) == 0) {
        char installed_lib[KRT_MAX_PATH];
        snprintf(installed_lib, sizeof(installed_lib), "%s%c..%clibs%cSystem%c%s.kro",
                 exe_dir, KRT_PATH_SEPARATOR, KRT_PATH_SEPARATOR,
                 KRT_PATH_SEPARATOR, KRT_PATH_SEPARATOR, lib_name);
        if (KrtPathExists(installed_lib)) return KRT_STRDUP(installed_lib);

        snprintf(installed_lib, sizeof(installed_lib), "%s%clibs%cSystem%c%s.kro",
                 exe_dir, KRT_PATH_SEPARATOR, KRT_PATH_SEPARATOR,
                 KRT_PATH_SEPARATOR, lib_name);
        if (KrtPathExists(installed_lib)) return KRT_STRDUP(installed_lib);

        snprintf(installed_lib, sizeof(installed_lib), "%s%c..%clibs%c%s.kro",
                 exe_dir, KRT_PATH_SEPARATOR, KRT_PATH_SEPARATOR,
                 KRT_PATH_SEPARATOR, lib_name);
        if (KrtPathExists(installed_lib)) return KRT_STRDUP(installed_lib);
    }

    (void)collect_search_roots_unused;
    return NULL;
}