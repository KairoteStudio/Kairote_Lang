#include "SemanticAnalyzer.h"
#include "../../Pipeline/CompilerPipeline.h"
#include "NameMangling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
static bool import_krt_file(SemanticAnalyzer* analyzer, const char* file_path);
static bool semantic_try_lazy_import_class(SemanticAnalyzer* analyzer, const char* class_name);

#include "Core/Utils/KrtCommon.h"
#include "Accelerator.h"
#include "../Lexer/Tokenizer.h"
#include "../Parser/Parser.h"

/* True when `capacity` can be doubled (or started at 8) without overflowing
 * size_t for an array of `element_size` bytes. */
static inline bool krt_capacity_can_grow(int capacity, size_t element_size) {
    if (capacity < 0 || element_size == 0) {
        return false;
    }
    size_t doubled = capacity == 0 ? 8u : (size_t)capacity * 2u;
    return doubled <= SIZE_MAX / element_size;
}

static bool semantic_analyzer_register_builtins(SemanticAnalyzer* analyzer) {
    if (!analyzer || !analyzer->symbol_table) {
        return false;
    }
    if (symbol_table_lookup(analyzer->symbol_table, "timer_start")) {
        return true;
    }
    static const char* const names[] = {"timer_start",       "timer_start_int", "timer_elapsed",
                                        "timer_elapsed_int", "timer_current",   "timer_current_int"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (!symbol_table_define(analyzer->symbol_table, names[i], SYMBOL_FUNCTION, 0, NULL)) {
            return false;
        }
    }
    return true;
}

SemanticAnalyzer* semantic_analyzer_create(void) {
    SemanticAnalyzer* analyzer = KRT_CALLOC(1, sizeof(*analyzer));
    if (!analyzer) {
        return NULL;
    }
    analyzer->symbol_table = symbol_table_create();
    if (!analyzer->symbol_table) {
        goto allocation_failed;
    }
    analyzer->global_symbol_table = analyzer->symbol_table;
    analyzer->require_entry_point = true;
    if (!semantic_analyzer_register_builtins(analyzer)) {
        goto allocation_failed;
    }

    analyzer->class_stack_capacity = 8;
    analyzer->class_name_stack = KRT_CALLOC(analyzer->class_stack_capacity, sizeof(*analyzer->class_name_stack));
    if (!analyzer->class_name_stack) {
        goto allocation_failed;
    }
    analyzer->generic_registry = generics_create_registry();
    if (!analyzer->generic_registry) {
        goto allocation_failed;
    }
    analyzer->type_arena = KrtArenaCreateLocal(KRT_ARENA_DEFAULT_BLOCK_SIZE);
    if (!analyzer->type_arena) {
        goto allocation_failed;
    }
    analyzer->using_capacity = 16;
    analyzer->using_directives = KRT_CALLOC(analyzer->using_capacity, sizeof(*analyzer->using_directives));
    if (!analyzer->using_directives) {
        goto allocation_failed;
    }
    analyzer->error_report = KrtErrorReportCreate();
    if (!analyzer->error_report) {
        goto allocation_failed;
    }
    return analyzer;

allocation_failed:
    semantic_analyzer_destroy(analyzer);
    return NULL;
}

void semantic_analyzer_destroy(SemanticAnalyzer* analyzer) {
    if (!analyzer) {
        return;
    }

    KrtArenaDestroy(analyzer->type_arena);
    if (analyzer->symbol_table) {
        symbol_table_destroy(analyzer->symbol_table);
        analyzer->symbol_table = NULL;
    }

    if (analyzer->class_name_stack) {
        for (int i = 0; i < analyzer->class_stack_size; i++) {
            if (analyzer->class_name_stack[i]) {
                char* name = analyzer->class_name_stack[i];
                analyzer->class_name_stack[i] = NULL;
                KRT_FREE(name);
            }
        }
        char** stack = analyzer->class_name_stack;
        analyzer->class_name_stack = NULL;
        KRT_FREE(stack);
    }

    if (analyzer->generic_registry && !analyzer->generic_registry_shared) {
        generics_destroy_registry(analyzer->generic_registry);
        analyzer->generic_registry = NULL;
    }

    if (analyzer->using_directives) {
        for (int i = 0; i < analyzer->using_count; i++) {
            UsingDirective* directive = analyzer->using_directives[i];
            if (directive) {
                if (directive->alias) {
                    KRT_FREE(directive->alias);
                }
                if (directive->namespace_path) {
                    for (int j = 0; j < directive->path_length; j++) {
                        KRT_FREE(directive->namespace_path[j]);
                    }
                    KRT_FREE(directive->namespace_path);
                }
                KRT_FREE(directive);
            }
        }
        KRT_FREE(analyzer->using_directives);
    }

    for (int i = 0; i < analyzer->project_namespace_count; i++) {
        KRT_FREE(analyzer->project_namespace_names[i]);
    }
    KRT_FREE(analyzer->project_namespace_names);
    for (int i = 0; i < analyzer->imported_source_count; i++) {
        KRT_FREE(analyzer->imported_source_paths[i]);
    }
    KRT_FREE(analyzer->imported_source_paths);
    KRT_FREE(analyzer->imported_source_failed);

    if (analyzer->error_report) {
        KrtErrorReportDestroy(analyzer->error_report);
        analyzer->error_report = NULL;
    }

    if (analyzer->input_file_path) {
        KRT_FREE(analyzer->input_file_path);
        analyzer->input_file_path = NULL;
    }

    if (analyzer->libs_path) {
        KRT_FREE(analyzer->libs_path);
        analyzer->libs_path = NULL;
    }

    KRT_FREE(analyzer->entry_point_name);
    analyzer->entry_point_name = NULL;
    KRT_FREE(analyzer);
}

void semantic_analyzer_set_pipeline(SemanticAnalyzer* analyzer, void* pipeline) {
    if (!analyzer) {
        return;
    }
    analyzer->pipeline = pipeline;
}

bool semantic_analyzer_register_imported_file(SemanticAnalyzer* analyzer, const char* file_path) {
    if (!analyzer || !file_path || !analyzer->pipeline) {
        return false;
    }

    return KrtCompilePipelineAddImportedFile(analyzer->pipeline, file_path);
}

static void collect_exports_from_node(SemanticAnalyzer* analyzer, ASTNode* node) {
    if (!analyzer || !node) {
        return;
    }

    switch (node->type) {
    case AST_FUNCTION_DECLARATION:
    case AST_STATIC_FUNCTION_DECLARATION: {
        const char* name = (node->type == AST_FUNCTION_DECLARATION) ? node->data.function_decl.name
                                                                   : node->data.static_function_decl.name;
        if (name) {
            SymbolEntry* existing = symbol_table_lookup(analyzer->global_symbol_table, name);
            if (!existing) {
                SymbolEntry* sym = symbol_table_define(analyzer->global_symbol_table, name, SYMBOL_FUNCTION, 0, NULL);
                /* M1: 预登记只算占位(FORWARD_REF), 不能是 DEFINED ——
                     * 否则本体文件预登记时撞 "already defined"; 且跨文件
                     * 引用方也能凭此条目通过查找 */
                if (sym) {
                    sym->state = SYMBOL_FORWARD_REF;
                    sym->source_type = KrtSourceTypeClone(
                        analyzer->type_arena, (KrtSourceType){.token = TOKEN_FN, .function = node->function_type});
                    sym->value_type = node->data.function_decl.return_type;
                }
            } else if (existing->type == SYMBOL_FUNCTION && existing->source_type.function &&
                       node->function_type) {
                KrtFunctionType* a = existing->source_type.function;
                KrtFunctionType* b = node->function_type;
                bool same = a->parameter_count == b->parameter_count;
                if (same) {
                    for (int k = 0; k < a->parameter_count; k++) {
                        if (KrtSourceStorage(a->parameters[k]) != KrtSourceStorage(b->parameters[k])) {
                            same = false;
                            break;
                        }
                    }
                }
                if (!same) {
                    semantic_analyzer_add_error(analyzer, "Conflicting cross-file definition of '%s'", name);
                }
            }
        }
        break;
    }
    case AST_CLASS_DECLARATION: {
        /* Project precollection is deliberately limited to free-function
         * signatures.  Classes may follow a semicolon namespace declaration
         * while still appearing as top-level AST nodes, so only their owning
         * namespace analysis can select the correct symbol table. */
        break;
    }
    case AST_NAMESPACE_DECLARATION: {
        ASTNode* body = node->data.namespace_decl.body;
        if (body && body->type == AST_BLOCK) {
            for (int i = 0; i < body->data.block.statement_count; i++) {
                ASTNode* child = body->data.block.statements[i];
                /* Namespace classes belong to the namespace's nested symbol
                 * table.  Registering them here in the global table makes
                 * their real declaration look like a duplicate.  Functions
                 * are safe forward exports, and nested namespaces recurse. */
                if (child && (child->type == AST_FUNCTION_DECLARATION ||
                              child->type == AST_STATIC_FUNCTION_DECLARATION ||
                              child->type == AST_NAMESPACE_DECLARATION)) {
                    collect_exports_from_node(analyzer, child);
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

static void collect_project_namespaces_recursive(SemanticAnalyzer* analyzer, ASTNode* node, int depth) {
    if (!analyzer || !node) {
        return;
    }
    if (depth > 64) {
        analyzer->project_namespace_collection_failed = true;
        return;
    }
    if (node->type == AST_PROGRAM || node->type == AST_BLOCK) {
        for (int i = 0; i < node->data.block.statement_count; i++) {
            collect_project_namespaces_recursive(analyzer, node->data.block.statements[i], depth + 1);
        }
        return;
    }
    if (node->type != AST_NAMESPACE_DECLARATION) {
        return;
    }

    const char* name = node->data.namespace_decl.name;
    if (name) {
        for (int i = 0; i < analyzer->project_namespace_count; i++) {
            if (strcmp(analyzer->project_namespace_names[i], name) == 0) {
                goto collect_children;
            }
        }
        if (analyzer->project_namespace_count == analyzer->project_namespace_capacity) {
            if (!krt_capacity_can_grow(analyzer->project_namespace_capacity,
                                       sizeof(*analyzer->project_namespace_names))) {
                analyzer->project_namespace_collection_failed = true;
                return;
            }
            int capacity = analyzer->project_namespace_capacity == 0 ? 8 : analyzer->project_namespace_capacity * 2;
            char** names = KRT_REALLOC(analyzer->project_namespace_names, (size_t)capacity * sizeof(*names));
            if (!names) {
                analyzer->project_namespace_collection_failed = true;
                return;
            }
            analyzer->project_namespace_names = names;
            analyzer->project_namespace_capacity = capacity;
        }
        char* copy = KRT_STRDUP(name);
        if (!copy) {
            analyzer->project_namespace_collection_failed = true;
            return;
        }
        analyzer->project_namespace_names[analyzer->project_namespace_count++] = copy;
    }

collect_children:
    collect_project_namespaces_recursive(analyzer, node->data.namespace_decl.body, depth + 1);
}

void semantic_analyzer_collect_project_namespaces(SemanticAnalyzer* analyzer, ASTNode* node) {
    collect_project_namespaces_recursive(analyzer, node, 0);
}

void semantic_analyzer_collect_exports(SemanticAnalyzer* analyzer, ASTNode* ast) {
    if (!analyzer || !ast) {
        return;
    }

    semantic_analyzer_collect_project_namespaces(analyzer, ast);

    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->data.block.statement_count; i++) {
            collect_exports_from_node(analyzer, ast->data.block.statements[i]);
        }
    } else {
        collect_exports_from_node(analyzer, ast);
    }
}

/* Single source of truth for the project root that owns a source file: walk
 * up to the nearest project.krt, otherwise fall back to the library root that
 * contains the file. */
static bool semantic_analyzer_project_root(const char* source_path, char* root, size_t root_size) {
    if (!source_path || !root || root_size == 0) {
        return false;
    }
    if (snprintf(root, root_size, "%s", source_path) >= (int)root_size) {
        return false;
    }
    char* file_name = strrchr(root, '/');
    if (!file_name) {
        snprintf(root, root_size, ".");
    } else if (file_name == root) {
        root[1] = '\0';
    } else {
        *file_name = '\0';
    }

    char search[PATH_MAX];
    snprintf(search, sizeof(search), "%s", root);
    bool found_project_manifest = false;
    for (;;) {
        char manifest[PATH_MAX];
        int manifest_length = snprintf(manifest, sizeof(manifest), "%s/project.krt", search);
        struct stat st;
        if (manifest_length >= 0 && (size_t)manifest_length < sizeof(manifest) && stat(manifest, &st) == 0 &&
            S_ISREG(st.st_mode)) {
            snprintf(root, root_size, "%s", search);
            found_project_manifest = true;
            break;
        }

        if (strcmp(search, "/") == 0) {
            break;
        }
        char* parent = strrchr(search, '/');
        if (!parent) {
            break;
        }
        if (parent == search) {
            search[1] = '\0';
        } else {
            *parent = '\0';
        }
    }

    /* Imported sources do not normally carry a project.krt of their own.  When
     * compiling such a source, retain the library root that contains it rather
     * than treating (for example) libs/System as a new project root. */
    if (!found_project_manifest) {
        char* library_component = NULL;
        for (char* component = strstr(root, "/libs/"); component != NULL;
             component = strstr(component + 1, "/libs/")) {
            library_component = component;
        }
        if (library_component) {
            *library_component = '\0';
        } else {
            size_t root_length = strlen(root);
            if (root_length >= 5 && strcmp(root + root_length - 5, "/libs") == 0) {
                if (root_length == 5) {
                    root[1] = '\0';
                } else {
                    root[root_length - 5] = '\0';
                }
            }
        }
    }
    return true;
}

static void semantic_analyzer_set_project_libs_path(SemanticAnalyzer* analyzer) {
    if (!analyzer || !analyzer->input_file_path) {
        return;
    }

    char project_root[PATH_MAX];
    if (!semantic_analyzer_project_root(analyzer->input_file_path, project_root, sizeof(project_root))) {
        KrtError("源文件路径过长: %s", analyzer->input_file_path);
        return;
    }

    char libs_path[PATH_MAX];
    int libs_length = strcmp(project_root, "/") == 0
                          ? snprintf(libs_path, sizeof(libs_path), "/libs")
                          : snprintf(libs_path, sizeof(libs_path), "%s/libs", project_root);
    if (libs_length >= 0 && (size_t)libs_length < sizeof(libs_path)) {
        analyzer->libs_path = KRT_STRDUP(libs_path);
    }
}

void semantic_analyzer_set_input_file(SemanticAnalyzer* analyzer, const char* file_path) {
    if (!analyzer) {
        return;
    }

    if (analyzer->input_file_path) {
        KRT_FREE(analyzer->input_file_path);
        analyzer->input_file_path = NULL;
    }

    if (analyzer->libs_path) {
        KRT_FREE(analyzer->libs_path);
        analyzer->libs_path = NULL;
    }

    if (file_path) {
        char abs_path[PATH_MAX];
#ifdef _WIN32
        if (file_path[0] == '/' || file_path[0] == '\\') {
#else
        if (file_path[0] == '/') {
#endif
            analyzer->input_file_path = KRT_STRDUP(file_path);
        } else if (realpath(file_path, abs_path) != NULL) {
            analyzer->input_file_path = KRT_STRDUP(abs_path);
        } else {
            KrtError("无法解析源文件路径: %s", file_path);
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                int length = snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, file_path);
                analyzer->input_file_path =
                    KRT_STRDUP(length >= 0 && (size_t)length < sizeof(abs_path) ? abs_path : file_path);
            } else {
                analyzer->input_file_path = KRT_STRDUP(file_path);
            }
        }

        semantic_analyzer_set_project_libs_path(analyzer);
    }
}

bool semantic_analyzer_push_class_context(SemanticAnalyzer* analyzer, const char* class_name) {
    if (!analyzer || !class_name) {
        return false;
    }

    if (analyzer->class_stack_size >= analyzer->class_stack_capacity) {
        if (!krt_capacity_can_grow(analyzer->class_stack_capacity, sizeof(*analyzer->class_name_stack))) {
            KrtError("类上下文栈容量溢出");
            return false;
        }
        int new_capacity = analyzer->class_stack_capacity * 2;
        char** new_stack = (char**)KRT_REALLOC(analyzer->class_name_stack, sizeof(char*) * (size_t)new_capacity);
        if (!new_stack) {
            KrtError("类上下文栈分配失败");
            return false;
        }
        analyzer->class_name_stack = new_stack;
        analyzer->class_stack_capacity = new_capacity;
    }

    char* copy = KRT_STRDUP(class_name);
    if (!copy) {
        KrtError("类上下文名称分配失败");
        return false;
    }
    analyzer->class_name_stack[analyzer->class_stack_size++] = copy;
    return true;
}

void semantic_analyzer_pop_class_context(SemanticAnalyzer* analyzer) {
    if (!analyzer || analyzer->class_stack_size <= 0) {
        return;
    }

    analyzer->class_stack_size--;
    KRT_FREE(analyzer->class_name_stack[analyzer->class_stack_size]);
    analyzer->class_name_stack[analyzer->class_stack_size] = NULL;
}

static void semantic_analyzer_add_using_directive(SemanticAnalyzer* analyzer, ASTNode* using_node) {
    if (!analyzer || !using_node) {
        return;
    }
    for (int i = 0; i < analyzer->using_count; i++) {
        UsingDirective* existing = analyzer->using_directives[i];
        if (existing->is_alias != using_node->data.using_directive.is_alias ||
            existing->path_length != using_node->data.using_directive.path_length) {
            continue;
        }
        const char* alias = using_node->data.using_directive.alias;
        if ((existing->alias == NULL) != (alias == NULL) || (alias && strcmp(existing->alias, alias) != 0)) {
            continue;
        }
        bool same_path = true;
        for (int j = 0; j < existing->path_length; j++) {
            if (strcmp(existing->namespace_path[j], using_node->data.using_directive.namespace_path[j]) != 0) {
                same_path = false;
                break;
            }
        }
        if (same_path) {
            return;
        }
    }
    if (analyzer->using_count >= analyzer->using_capacity) {
        if (analyzer->using_capacity > INT_MAX / 2) {
            semantic_analyzer_add_error_at(analyzer, using_node, "Too many using directives");
            return;
        }
        int capacity = analyzer->using_capacity * 2;
        UsingDirective** directives = KRT_REALLOC(analyzer->using_directives, (size_t)capacity * sizeof(*directives));
        if (!directives) {
            semantic_analyzer_add_error_at(analyzer, using_node, "Out of memory while collecting using directives");
            return;
        }
        analyzer->using_directives = directives;
        analyzer->using_capacity = capacity;
    }
    UsingDirective* directive = KRT_CALLOC(1, sizeof(*directive));
    if (!directive) {
        semantic_analyzer_add_error_at(analyzer, using_node, "Out of memory while collecting using directive");
        return;
    }
    if (using_node->data.using_directive.alias) {
        directive->alias = KRT_STRDUP(using_node->data.using_directive.alias);
        if (!directive->alias) {
            goto allocation_failed;
        }
    }
    directive->is_alias = using_node->data.using_directive.is_alias;
    directive->path_length = using_node->data.using_directive.path_length;
    if (using_node->data.using_directive.namespace_path && directive->path_length > 0) {
        directive->namespace_path = KRT_CALLOC(directive->path_length, sizeof(*directive->namespace_path));
        if (!directive->namespace_path) {
            goto allocation_failed;
        }
        for (int i = 0; i < directive->path_length; i++) {
            directive->namespace_path[i] = KRT_STRDUP(using_node->data.using_directive.namespace_path[i]);
            if (!directive->namespace_path[i]) {
                goto allocation_failed;
            }
        }
    }
    analyzer->using_directives[analyzer->using_count++] = directive;
    return;

allocation_failed:
    if (directive->namespace_path) {
        for (int i = 0; i < directive->path_length; i++) {
            KRT_FREE(directive->namespace_path[i]);
        }
        KRT_FREE(directive->namespace_path);
    }
    KRT_FREE(directive->alias);
    KRT_FREE(directive);
    semantic_analyzer_add_error_at(analyzer, using_node, "Out of memory while copying using directive");
}

static SymbolEntry* semantic_analyzer_lookup_namespace(SemanticAnalyzer* analyzer, char** namespace_path,
                                                       int path_length) {
    if (!analyzer || !namespace_path || path_length <= 0) {
        return NULL;
    }

    SymbolEntry* current = NULL;

    SymbolTable* current_table = analyzer->global_symbol_table ? analyzer->global_symbol_table : analyzer->symbol_table;

    for (int i = 0; i < path_length; i++) {
        if (!namespace_path[i]) {
            return NULL;
        }
        current = symbol_table_lookup(current_table, namespace_path[i]);

        if (!current) {
            return NULL;
        }

        if (i < path_length - 1) {
            if (current->type != SYMBOL_NAMESPACE) {
                return NULL;
            }
            if (!current->nested_table) {
                return NULL;
            }

            current_table = current->nested_table;
        }
    }

    if (current && current->type != SYMBOL_NAMESPACE) {
        return NULL;
    }

    return current;
}

SymbolEntry* semantic_analyzer_lookup_qualified_name(SemanticAnalyzer* analyzer, char** parts, int part_count) {
    if (!analyzer || !parts || part_count <= 0) {
        return NULL;
    }

    SymbolEntry* result = semantic_analyzer_lookup_namespace(analyzer, parts, part_count);
    if (result) {
        return result;
    }

    if (part_count >= 2) {
        if (!parts[part_count - 1]) {
            return NULL;
        }
        SymbolEntry* ns = semantic_analyzer_lookup_namespace(analyzer, parts, part_count - 1);
        if (ns && ns->nested_table) {
            result = symbol_table_lookup(ns->nested_table, parts[part_count - 1]);
            if (result) {
                return result;
            }
        }
    }

    return NULL;
}

static SymbolEntry* semantic_analyzer_lookup_with_usings(SemanticAnalyzer* analyzer, const char* name) {
    if (!analyzer || !name) {
        return NULL;
    }

    SymbolEntry* result = symbol_table_lookup_scope_chain(analyzer->symbol_table, name);
    if (result) {
        return result;
    }

    for (int i = 0; i < analyzer->using_count; i++) {
        UsingDirective* directive = analyzer->using_directives[i];
        if (!directive || directive->path_length <= 0) {
            continue;
        }

        SymbolEntry* ns =
            semantic_analyzer_lookup_namespace(analyzer, directive->namespace_path, directive->path_length);
        if (ns && ns->nested_table) {
            SymbolEntry* found = symbol_table_lookup(ns->nested_table, name);
            if (found) {
                if (result && found != result) {
                    semantic_analyzer_add_error(
                        analyzer, "Ambiguous name '%s' imported by multiple using directives", name);
                    return NULL;
                }
                result = found;
            }
        }
    }

    return result;
}

const char* semantic_analyzer_get_current_class_context(SemanticAnalyzer* analyzer) {
    if (!analyzer || analyzer->class_stack_size <= 0) {
        return NULL;
    }
    return analyzer->class_name_stack[analyzer->class_stack_size - 1];
}

static bool semantic_class_has_members(SymbolEntry* cls) {
    if (!cls || !cls->nested_table) {
        return false;
    }
    SymbolTable* nt = cls->nested_table;
    for (SymbolScope* sc = nt->current_scope; sc; sc = sc->parent) {
        if (sc->symbols) {
            return true;
        }
    }
    if (nt->hash_table) {
        for (int b = 0; b < nt->hash_size; b++) {
            if (nt->hash_table[b]) {
                return true;
            }
        }
    }
    return false;
}

static SymbolEntry* semantic_lookup_method_by_arity(SemanticAnalyzer* analyzer, SymbolTable* nested,
                                                    const char* class_name, const char* method_name, int argc,
                                                    ASTNode* const* args);
static SymbolEntry* semantic_lookup_method_in_global_class(SemanticAnalyzer* analyzer, const char* class_name,
                                                           const char* method_name, int argc, ASTNode* const* args) {
    if (!analyzer || !analyzer->global_symbol_table || !class_name || !method_name) {
        return NULL;
    }
    SymbolEntry* gcls = symbol_table_lookup(analyzer->global_symbol_table, class_name);
    if (!gcls || gcls->type != SYMBOL_CLASS || !gcls->nested_table) {
        return NULL;
    }
    return semantic_lookup_method_by_arity(analyzer, gcls->nested_table, class_name, method_name, argc, args);
}

static SymbolEntry* semantic_analyzer_lookup_class(SemanticAnalyzer* analyzer, const char* class_name) {
    if (!analyzer || !class_name) {
        return NULL;
    }

    /* A class is only usable once it owns a scope; a bare declaration is not
     * a definition and must not be handed to callers as one. */
    SymbolEntry* fallback = NULL;
    SymbolTable* table = analyzer->symbol_table;
    while (table) {
        SymbolEntry* result = symbol_table_lookup(table, class_name);
        if (result && result->type == SYMBOL_CLASS) {
            if (semantic_class_has_members(result)) {
                return result;
            }
            if (!fallback && result->nested_table) {
                fallback = result;
            }
        }
        table = table->parent_table;
    }

    if (analyzer->global_symbol_table && analyzer->global_symbol_table != analyzer->symbol_table) {
        SymbolEntry* result = symbol_table_lookup(analyzer->global_symbol_table, class_name);
        if (result && result->type == SYMBOL_CLASS) {
            if (semantic_class_has_members(result)) {
                return result;
            }
            if (!fallback && result->nested_table) {
                fallback = result;
            }
        }
    }

    return fallback;
}

/* Single source of truth for the type of `c ? a : b`. */
static KrtTokenType semantic_ternary_result_type(KrtTokenType left, KrtTokenType right) {
    if (left == TOKEN_UNKNOWN || right == TOKEN_UNKNOWN) {
        return TOKEN_UNKNOWN;
    }
    if (left == right) {
        return left;
    }
    if ((left == TOKEN_STRING && right == TOKEN_NULL) || (right == TOKEN_STRING && left == TOKEN_NULL)) {
        return TOKEN_STRING;
    }
    if (left == TOKEN_FLOAT64 || right == TOKEN_FLOAT64) {
        return TOKEN_FLOAT64;
    }
    if (left == TOKEN_FLOAT32 || right == TOKEN_FLOAT32) {
        return TOKEN_FLOAT32;
    }
    return KrtTokenIntegerCommon(left, right);
}

/* Element type of an array expression.  TOKEN_EOF is the "not an array, or
 * element type unknown" sentinel; callers must treat it as "no stride"
 * rather than as a byte count. */
static KrtTokenType semantic_array_element_type_depth(SemanticAnalyzer* analyzer, ASTNode* expr, int depth) {
    if (!expr) {
        return TOKEN_EOF;
    }
    if (depth > 64) {
        return TOKEN_EOF;
    }
    if (expr->type == AST_IDENTIFIER) {
        SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.identifier_name);
        return symbol && symbol->is_array ? symbol->value_type : TOKEN_EOF;
    }
    if (expr->type == AST_NEW_ARRAY_EXPRESSION) {
        return expr->data.new_array_expr.type_token;
    }
    if (expr->type == AST_MEMBER_ACCESS && expr->data.member_access.resolved_is_array) {
        return expr->resolved_type.token;
    }
    if (expr->type == AST_TERNARY_OPERATION) {
        if (expr->data.ternary_op.true_value->type == AST_NULL) {
            return semantic_array_element_type_depth(analyzer, expr->data.ternary_op.false_value, depth + 1);
        }
        if (expr->data.ternary_op.false_value->type == AST_NULL) {
            return semantic_array_element_type_depth(analyzer, expr->data.ternary_op.true_value, depth + 1);
        }
        KrtTokenType left = semantic_array_element_type_depth(analyzer, expr->data.ternary_op.true_value, depth + 1);
        KrtTokenType right = semantic_array_element_type_depth(analyzer, expr->data.ternary_op.false_value, depth + 1);
        if (left == TOKEN_EOF || right == TOKEN_EOF) {
            return TOKEN_EOF;
        }
        KrtTokenType merged = semantic_ternary_result_type(left, right);
        return merged == TOKEN_UNKNOWN ? TOKEN_EOF : merged;
    }
    return TOKEN_EOF;
}

static KrtTokenType semantic_array_element_type(SemanticAnalyzer* analyzer, ASTNode* expr) {
    return semantic_array_element_type_depth(analyzer, expr, 0);
}

/* Value of a compile-time integer constant, or -1 when it is not one. */
static long semantic_constant_int(SemanticAnalyzer* analyzer, ASTNode* expr) {
    if (!expr) {
        return -1;
    }
    if (expr->type == AST_NUMBER && expr->is_integer_literal) {
        return (long)(int64_t)expr->integer_value;
    }
    if (expr->type == AST_UNARY_OPERATION && expr->data.unary_op.operator == TOKEN_MINUS) {
        long v = semantic_constant_int(analyzer, expr->data.unary_op.operand);
        return v == -1 ? -1 : -v;
    }
    if (expr->type == AST_BINARY_OPERATION) {
        long a = semantic_constant_int(analyzer, expr->data.binary_op.left);
        long b = semantic_constant_int(analyzer, expr->data.binary_op.right);
        if (a == -1 || b == -1) {
            return -1;
        }
        switch (expr->data.binary_op.operator) {
        case TOKEN_PLUS:
            return a + b;
        case TOKEN_MINUS:
            return a - b;
        case TOKEN_MULTIPLY:
            return a * b;
        default:
            return -1;
        }
    }
    return -1;
}

static KrtTokenType semantic_analyzer_infer_expression_type(SemanticAnalyzer* analyzer, ASTNode* expr) {
    if (!expr) {
        return TOKEN_UNKNOWN;
    }
    /* Array overloads are named by their element token, while the ABI passes an address. */
    KrtTokenType element_type = semantic_array_element_type(analyzer, expr);
    if (element_type != TOKEN_EOF) {
        return element_type;
    }
    if (expr->resolved_type.token) {
        return KrtSourceStorage(expr->resolved_type);
    }

    switch (expr->type) {
    case AST_NUMBER: {
        if (!expr->is_integer_literal) {
            return TOKEN_FLOAT64;
        }
        if (expr->integer_value <= INT32_MAX) {
            return TOKEN_INT32;
        }
        if (expr->integer_value <= INT64_MAX) {
            return TOKEN_INT64;
        }
        if (expr->integer_value <= KrtIntegerMask(127)) {
            return TOKEN_INT128;
        }
        return TOKEN_UINT128;
    }
    case AST_STRING:
        return TOKEN_STRING;
    case AST_BOOLEAN:
        return TOKEN_BOOL;
    case AST_CHAR_LITERAL:
        return TOKEN_CHAR;
    case AST_IDENTIFIER: {
        SymbolEntry* sym = symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.identifier_name);
        if (sym && sym->value_type != TOKEN_EOF) {
            return sym->value_type;
        }
        return TOKEN_UNKNOWN;
    }
    case AST_CAST_EXPRESSION:
        return expr->data.cast_expr.target_type;
    case AST_CALL: {
        SymbolEntry* callee = NULL;
        if (expr->data.call.resolved_mangled_name) {
            callee = symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.call.resolved_mangled_name);
        }
        if (!callee && expr->data.call.name) {
            callee = symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.call.name);
            if (!callee && expr->data.call.resolved_class_name) {
                char* legacy = name_mangle_simple(expr->data.call.resolved_class_name, expr->data.call.name);
                if (legacy) {
                    callee = symbol_table_lookup_scope_chain(analyzer->symbol_table, legacy);
                    KRT_FREE(legacy);
                }
            }
        }
        if (callee && callee->value_type != TOKEN_EOF) {
            return callee->value_type;
        }
        return TOKEN_UNKNOWN;
    }
    case AST_UNARY_OPERATION: {
        if (expr->data.unary_op.operator == TOKEN_NOT) {
            return TOKEN_BOOL;
        }
        KrtTokenType operand = semantic_analyzer_infer_expression_type(analyzer, expr->data.unary_op.operand);
        return operand == TOKEN_UNKNOWN ? TOKEN_UNKNOWN : operand;
    }
    case AST_TERNARY_OPERATION: {
        KrtTokenType left = semantic_analyzer_infer_expression_type(analyzer, expr->data.ternary_op.true_value);
        KrtTokenType right = semantic_analyzer_infer_expression_type(analyzer, expr->data.ternary_op.false_value);
        return semantic_ternary_result_type(left, right);
    }
    case AST_BINARY_OPERATION: {
        KrtTokenType op = expr->data.binary_op.operator;
        if (op == TOKEN_EQUAL || op == TOKEN_NOT_EQUAL || op == TOKEN_LESS || op == TOKEN_GREATER ||
            op == TOKEN_LESS_EQUAL || op == TOKEN_GREATER_EQUAL || op == TOKEN_AND || op == TOKEN_OR) {
            return TOKEN_BOOL;
        }
        KrtTokenType left = semantic_analyzer_infer_expression_type(analyzer, expr->data.binary_op.left);
        KrtTokenType right = semantic_analyzer_infer_expression_type(analyzer, expr->data.binary_op.right);
        if (left == TOKEN_UNKNOWN || right == TOKEN_UNKNOWN) {
            return TOKEN_UNKNOWN;
        }
        if (op == TOKEN_ASSIGN) {
            return left;
        }
        if (left == TOKEN_FLOAT64 || right == TOKEN_FLOAT64) {
            return TOKEN_FLOAT64;
        }
        if (left == TOKEN_FLOAT32 || right == TOKEN_FLOAT32) {
            return TOKEN_FLOAT32;
        }
        if (left == TOKEN_STRING || right == TOKEN_STRING) {
            /* Only string + string is a valid operation; anything else is
             * diagnosed by the expression analyser. */
            return (op == TOKEN_PLUS && left == TOKEN_STRING && right == TOKEN_STRING) ? TOKEN_STRING
                                                                                        : TOKEN_UNKNOWN;
        }
        if (op == TOKEN_LSHIFT || op == TOKEN_RSHIFT) {
            return left;
        }
        return KrtTokenIntegerCommon(left, right);
    }
    default:
        return TOKEN_UNKNOWN;
    }
}

/* Replace *slot with a copy of text, releasing the previous value.
 * Returns false when the copy could not be allocated. */
static bool semantic_set_resolved_name(char** slot, const char* text) {
    if (!text) {
        return true;
    }
    char* copy = KRT_STRDUP(text);
    if (!copy) {
        return false;
    }
    KRT_FREE(*slot);
    *slot = copy;
    return true;
}

void semantic_analyzer_add_error(SemanticAnalyzer* analyzer, const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (written < 0) {
        message[0] = '\0';
    }
    semantic_analyzer_add_error_ex(analyzer, 0, 0, message, NULL);
}

void semantic_analyzer_add_error_ex(SemanticAnalyzer* analyzer, int line, int column, const char* message,
                                    const char* hint) {
    if (!analyzer) {
        // 9-11: never swallow a diagnostic just because the analyzer is gone.
        if (message) {
            fprintf(stderr, "Error: %s\n", message);
            fflush(stderr);
        }
        return;
    }

    analyzer->error_count++;

    if (analyzer->error_report) {
        KrtErrorReportAdd(analyzer->error_report, KRT_ERROR_ERROR, KRT_ERROR_STAGE_SEMANTIC, line, column, message,
                          hint ? hint : "");
        return;
    }

    if (line > 0) {
        fprintf(stderr, "Error at %d:%d: %s\n", line, column, message);
    } else {
        fprintf(stderr, "Error: %s\n", message);
    }
    fflush(stderr);
}

void semantic_analyzer_add_error_at(SemanticAnalyzer* analyzer, ASTNode* node, const char* format, ...) {
    if (!analyzer) {
        return;
    }

    analyzer->error_count++;

    char message[512];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (written < 0) {
        snprintf(message, sizeof(message), "%s", "internal error while formatting a diagnostic");
    }

    int line = node ? node->line : 0;
    int column = node ? node->col : 0;

    if (analyzer->error_report) {
        KrtErrorReportAdd(analyzer->error_report, KRT_ERROR_ERROR, KRT_ERROR_STAGE_SEMANTIC, line, column, message, "");
    } else {
        if (line > 0) {
            fprintf(stderr, "Error at %d:%d: %s\n", line, column, message);
        } else {
            fprintf(stderr, "Error: %s\n", message);
        }
        fflush(stderr);
    }
}

void semantic_analyzer_add_warning(SemanticAnalyzer* analyzer, const char* format, ...) {
    if (!analyzer) {
        return;
    }

    analyzer->warning_count++;

    va_list args;
    va_start(args, format);

    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), format, args);

    fprintf(stderr, "[SEMANTIC WARNING] %s\n", buffer);
    fflush(stderr);

    va_end(args);
}

static SymbolEntry* semantic_lookup_exact_in_class(SymbolTable* nested, const char* name) {
    return symbol_table_lookup_scope_chain(nested, name);
}

typedef struct {
    int worst_conversion;
    int conversion_count;
    uint64_t width_distance;
} KrtMethodScore;

static bool source_type_equal(KrtSourceType a, KrtSourceType b);

static KrtTokenType semantic_mangled_parameter_type(const char* suffix, int length) {
    if (length > 1) {
        long bits = 0;
        for (int i = 1; i < length - 1; i++) {
            if (suffix[i] < '0' || suffix[i] > '9') {
                return TOKEN_EOF;
            }
            bits = bits * 10 + (suffix[i] - '0');
            if (bits == 0 || bits > 128) {
                return TOKEN_EOF;
            }
        }
        return KrtTokenIntegerType((int)bits, suffix[0] == 'u');
    }
    switch (*suffix) {
    case 'c':
        return TOKEN_INT8;
    case 's':
        return TOKEN_INT16;
    case 'i':
        return TOKEN_INT32;
    case 'l':
        return TOKEN_INT64;
    case 'C':
        return TOKEN_UINT8;
    case 'S':
        return TOKEN_UINT16;
    case 'I':
        return TOKEN_UINT32;
    case 'L':
        return TOKEN_UINT64;
    case 'f':
        return TOKEN_FLOAT32;
    case 'd':
        return TOKEN_FLOAT64;
    case 'b':
        return TOKEN_BOOL;
    case 'r': /* legacy code, kept so old objects still demangle */
    case 't':
        return TOKEN_STRING;
    case 'v':
        return TOKEN_VOID;
    default:
        return TOKEN_EOF;
    }
}

static bool semantic_parameter_score(SemanticAnalyzer* analyzer, const SymbolEntry* method, const char* suffix,
                                     int argc, ASTNode* const* args, KrtMethodScore* score) {
    *score = (KrtMethodScore){0};
    if (argc == 0) {
        return !*suffix || strcmp(suffix, "v") == 0;
    }
    for (int k = 0; k < argc; k++) {
        if (!*suffix || !args || !args[k]) {
            return false;
        }
        KrtTokenType source = semantic_analyzer_infer_expression_type(analyzer, args[k]);
        KrtFunctionType* signature = method->source_type.function;
        KrtSourceType parameter = (signature && signature->parameters && signature->parameter_count == argc)
                                      ? signature->parameters[k]
                                      : (KrtSourceType){0};
        if (parameter.is_ref) {
            parameter.is_ref = false;
            if (!source_type_equal(parameter, args[k]->resolved_type)) {
                return false;
            }
        } else if (args[k]->is_ref_argument) {
            return false;
        }
        const char* expected = KrtMangleTypeName(source);
        int length = 1;
        if ((suffix[0] == 'i' || suffix[0] == 'u') && suffix[1] >= '0' && suffix[1] <= '9') {
            while (suffix[length] >= '0' && suffix[length] <= '9') {
                length++;
            }
            if (suffix[length] != '_') {
                return false;
            }
            length++;
        }
        if (strlen(expected) != (size_t)length || strncmp(suffix, expected, length) != 0) {
            KrtTokenType target = semantic_mangled_parameter_type(suffix, length);
            bool source_string = source == TOKEN_STRING || source == TOKEN_TYPE_STRING;
            bool target_string = target == TOKEN_STRING || target == TOKEN_TYPE_STRING;
            bool null_to_string = source == TOKEN_NULL && target_string;
            if (source_string != target_string && !null_to_string) {
                return false;
            }
            if (semantic_array_element_type(analyzer, args[k]) != TOKEN_EOF && !KrtSourceIsPointer(parameter)) {
                /* Changing an array element type would change the callee's indexing stride. */
                return false;
            }
            int source_bits = KrtTokenIntegerBits(source), target_bits = KrtTokenIntegerBits(target);
            int conversion = null_to_string ? 1 : 3;
            if (source == TOKEN_CHAR || source == TOKEN_BOOL) {
                source_bits = source == TOKEN_BOOL ? 1 : 8;
            }
            if (source_bits && target_bits) {
                bool source_unsigned = KrtTokenIsUnsigned(source) || source == TOKEN_CHAR || source == TOKEN_BOOL;
                bool target_unsigned = KrtTokenIsUnsigned(target);
                if ((source_unsigned == target_unsigned && target_bits >= source_bits) ||
                    (source_unsigned && !target_unsigned && target_bits > source_bits)) {
                    conversion = 1;
                }
                score->width_distance += (uint64_t)abs(target_bits - source_bits);
            } else if (source == TOKEN_FLOAT32 && target == TOKEN_FLOAT64) {
                conversion = 1;
            } else if (source_bits && (target == TOKEN_FLOAT32 || target == TOKEN_FLOAT64)) {
                conversion = 2;
            }
            if (conversion > score->worst_conversion) {
                score->worst_conversion = conversion;
            }
            score->conversion_count++;
        }
        suffix += length;
    }
    return !*suffix;
}

static bool semantic_method_score_better(KrtMethodScore score, KrtMethodScore best) {
    if (score.worst_conversion != best.worst_conversion) {
        return score.worst_conversion < best.worst_conversion;
    }
    if (score.conversion_count != best.conversion_count) {
        return score.conversion_count < best.conversion_count;
    }
    return score.width_distance < best.width_distance;
}

static SymbolEntry* semantic_lookup_method_by_arity(SemanticAnalyzer* analyzer, SymbolTable* nested,
                                                    const char* class_name, const char* method_name, int argc,
                                                    ASTNode* const* args) {
    if (!nested || !class_name || !method_name) {
        return NULL;
    }
    char* prefix = name_mangle_class_member(NULL, class_name, method_name);
    if (!prefix) {
        return NULL;
    }
    size_t plen = strlen(prefix);

    SymbolEntry* best = NULL;
    KrtMethodScore best_score = {INT_MAX, INT_MAX, UINT64_MAX};
    bool ambiguous = false;

    for (SymbolScope* sc = nested->current_scope; sc; sc = sc->parent) {
        for (SymbolEntry* e = sc->symbols; e; e = e->scope_next) {
            if (!e || !e->name || e == best) {
                continue;
            }
            if (strncmp(e->name, prefix, plen) != 0) {
                continue;
            }
            KrtMethodScore score;
            if (!semantic_parameter_score(analyzer, e, e->name + plen, argc, args, &score)) {
                continue;
            }
            if (semantic_method_score_better(score, best_score)) {
                best_score = score;
                best = e;
            } else if (best && score.worst_conversion == best_score.worst_conversion &&
                       score.conversion_count == best_score.conversion_count &&
                       score.width_distance == best_score.width_distance) {
                ambiguous = true;
            }
        }
    }
    if (nested->hash_table) {
        for (int b = 0; b < nested->hash_size; b++) {
            for (SymbolEntry* e = nested->hash_table[b]; e; e = e->next) {
                if (!e || !e->name || e == best) {
                    continue;
                }
                if (strncmp(e->name, prefix, plen) != 0) {
                    continue;
                }
                KrtMethodScore score;
                if (!semantic_parameter_score(analyzer, e, e->name + plen, argc, args, &score)) {
                    continue;
                }
                if (semantic_method_score_better(score, best_score)) {
                    best_score = score;
                    best = e;
                } else if (best && score.worst_conversion == best_score.worst_conversion &&
                           score.conversion_count == best_score.conversion_count &&
                           score.width_distance == best_score.width_distance) {
                    ambiguous = true;
                }
            }
        }
    }

    KRT_FREE(prefix);
    if (ambiguous && best) {
        semantic_analyzer_add_error(analyzer, "Ambiguous overload: %s::%s", class_name, method_name);
    }
    return best;
}

#include "PointerAnalysis.inc"

static bool semantic_check_assignable(SemanticAnalyzer* analyzer, ASTNode* node, const char* name,
                                      SymbolEntry* symbol) {
    if (!symbol) {
        return true;
    }
    if (symbol->is_const) {
        semantic_analyzer_add_error_at(analyzer, node, "Cannot assign to constant: %s", name);
        return false;
    }
    if (symbol->type != SYMBOL_VARIABLE && symbol->type != SYMBOL_FIELD &&
        symbol->type != SYMBOL_STATIC_FIELD) {
        semantic_analyzer_add_error_at(analyzer, node, "'%s' is not an assignable variable", name);
        return false;
    }
    return true;
}

/*
 * C#-style condition: only bool is accepted.  Without this, `if (5)` and
 * `while ("x")` compile silently and the run-time test is just "non-zero".
 */
static bool semantic_require_bool_condition(SemanticAnalyzer* analyzer, ASTNode* node, ASTNode* cond) {
    if (!cond) {
        return true;
    }
    KrtTokenType type = semantic_analyzer_infer_expression_type(analyzer, cond);
    if (type != TOKEN_BOOL && type != TOKEN_EOF && type != TOKEN_NULL && type != TOKEN_UNKNOWN) {
        semantic_analyzer_add_error_at(analyzer, node,
                                       "Condition must be of type bool (got token %d)", (int)type);
        return false;
    }
    return true;
}

/* Single rule for array indices: an integer expression, present. */
static bool semantic_check_array_index(SemanticAnalyzer* analyzer, ASTNode* container, ASTNode* index_expr) {
    if (!index_expr) {
        semantic_analyzer_add_error_at(analyzer, container, "Array access is missing its index");
        return false;
    }
    KrtTokenType index_type = semantic_analyzer_infer_expression_type(analyzer, index_expr);
    if (index_type != TOKEN_EOF && index_type != TOKEN_UNKNOWN && KrtTokenIntegerBits(index_type) == 0) {
        semantic_analyzer_add_error_at(analyzer, index_expr, "Array index must be an integer");
        return false;
    }
    return true;
}

static bool semantic_expression_impl(SemanticAnalyzer* analyzer, ASTNode* expr) {
    if (!analyzer || !expr) {
        return false;
    }

    switch (expr->type) {
    case AST_NUMBER:
        expr->resolved_type.token = semantic_analyzer_infer_expression_type(analyzer, expr);
        return true;
    case AST_BOOLEAN:
        expr->resolved_type.token = TOKEN_BOOL;
        return true;
    case AST_STRING:
        expr->resolved_type.token = TOKEN_STRING;
        return true;
    case AST_CHAR_LITERAL:
        expr->resolved_type.token = TOKEN_CHAR;
        return true;

    case AST_IDENTIFIER: {
        const char* var_name = expr->data.identifier_name;
        if (!var_name) {
            semantic_analyzer_add_error_at(analyzer, expr, "Identifier is missing its name");
            return false;
        }

        if (!analyzer->is_unsafe_mode && var_name[0] == '_' && strncmp(var_name, "__lambda_", 9) != 0) {
            semantic_analyzer_add_error_at(
                analyzer, expr, "Direct access to internal symbol '%s' is restricted. Use unsafe(\"%s\", ...) instead.",
                var_name, var_name);
            return false;
        }

        SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, var_name);
        if (!symbol) {
            semantic_analyzer_add_error_at(analyzer, expr, "Undefined identifier: %s", var_name);
            return false;
        }
        symbol->was_read = 1;

        return true;
    }

    case AST_UNSAFE_CALL: {
        int old_mode = analyzer->is_unsafe_mode;
        int old_mem = analyzer->unsafe_mem;
        analyzer->is_unsafe_mode = 1;
        for (int i = 0; i < expr->data.unsafe_call.permission_count; i++) {
            if (expr->data.unsafe_call.permissions[i] &&
                strcmp(expr->data.unsafe_call.permissions[i], "krt.mem") == 0) {
                analyzer->unsafe_mem = 1;
            }
        }
        bool result = semantic_analyzer_analyze_expression(analyzer, expr->data.unsafe_call.expression);
        analyzer->is_unsafe_mode = old_mode;
        analyzer->unsafe_mem = old_mem;
        return result;
    }

    case AST_BINARY_OPERATION: {
        ASTNode* left = expr->data.binary_op.left;
        if (!left || !expr->data.binary_op.right) {
            semantic_analyzer_add_error_at(analyzer, expr, "Binary operation is missing an operand");
            return false;
        }
        if (expr->data.binary_op.operator == TOKEN_ASSIGN && left->type != AST_IDENTIFIER &&
            left->type != AST_ARRAY_ACCESS && left->type != AST_MEMBER_ACCESS &&
            left->type != AST_POINTER_DEREFERENCE) {
            return pointer_error(analyzer, expr, "Assignment requires a writable lvalue");
        }
        bool left_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.binary_op.left);
        KrtTokenType left_type = semantic_analyzer_infer_expression_type(analyzer, left);
        KrtTokenType right_type = semantic_analyzer_infer_expression_type(analyzer, expr->data.binary_op.right);
        bool left_string = left_type == TOKEN_STRING || left_type == TOKEN_TYPE_STRING;
        bool right_string = right_type == TOKEN_STRING || right_type == TOKEN_TYPE_STRING;
        KrtTokenType op = expr->data.binary_op.operator;
        bool arithmetic_op = op == TOKEN_PLUS || op == TOKEN_MINUS || op == TOKEN_MULTIPLY ||
                             op == TOKEN_DIVIDE || op == TOKEN_MODULO;
        if (left_ok && arithmetic_op && (left_string || right_string) &&
            (op != TOKEN_PLUS || !(left_string && right_string))) {
            semantic_analyzer_add_error_at(analyzer, expr,
                                           "String values only support concatenation with another string");
            return false;
        }
        if (left_ok && op != TOKEN_ASSIGN &&
            (left_type == TOKEN_VOID || right_type == TOKEN_VOID)) {
            semantic_analyzer_add_error_at(analyzer, expr, "A void value cannot be used here");
            return false;
        }
        if (left_ok && (op == TOKEN_DIVIDE || op == TOKEN_MODULO)) {
            ASTNode* divisor = expr->data.binary_op.right;
            if (divisor && divisor->type == AST_NUMBER && divisor->is_integer_literal &&
                divisor->integer_value == 0) {
                semantic_analyzer_add_error_at(analyzer, expr, "Division by zero");
                return false;
            }
        }
        if (left_ok && expr->data.binary_op.operator == TOKEN_ASSIGN &&
            left->type == AST_MEMBER_ACCESS &&
            left->data.member_access.resolved_builtin_member != KRT_BUILTIN_MEMBER_NONE) {
            semantic_analyzer_add_error_at(analyzer, expr,
                                           "Cannot assign to read-only member '%s'",
                                           left->data.member_access.member_name);
            return false;
        }
        /* Assigning through a member/array marks the base object initialized:
         * `Vec v; v.x = 1; use(v);` must not be reported as read-before-assign. */
        if (left_ok && expr->data.binary_op.operator == TOKEN_ASSIGN) {
            ASTNode* base = NULL;
            if (left->type == AST_MEMBER_ACCESS) {
                base = left->data.member_access.object;
            } else if (left->type == AST_ARRAY_ACCESS) {
                base = left->data.array_access.array;
            }
            if (base && base->type == AST_IDENTIFIER) {
                SymbolEntry* base_symbol =
                    symbol_table_lookup_scope_chain(analyzer->symbol_table, base->data.identifier_name);
                if (base_symbol && base_symbol->type == SYMBOL_VARIABLE) {
                    base_symbol->is_initialized = 1;
                }
            }
        }
        bool right_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.binary_op.right);
        return left_ok && right_ok;
    }

    case AST_UNARY_OPERATION: {
        ASTNode* operand = expr->data.unary_op.operand;
        if (!operand) {
            semantic_analyzer_add_error_at(analyzer, expr, "Unary operation is missing its operand");
            return false;
        }
        KrtTokenType op = expr->data.unary_op.operator;
        KrtTokenType operand_type = semantic_analyzer_infer_expression_type(analyzer, operand);
        if ((op == TOKEN_MINUS || op == TOKEN_PLUS) &&
            (operand_type == TOKEN_STRING || operand_type == TOKEN_TYPE_STRING || operand_type == TOKEN_VOID)) {
            semantic_analyzer_add_error_at(analyzer, expr, "Arithmetic unary operator cannot be applied here");
            return false;
        }
        return semantic_analyzer_analyze_expression(analyzer, operand);
    }

    case AST_ARRAY_LITERAL: {
        bool ok = true;
        bool seen_string = false;
        bool seen_other = false;
        for (int i = 0; i < expr->data.array_literal.element_count; i++) {
            if (!semantic_analyzer_analyze_expression(analyzer, expr->data.array_literal.elements[i])) {
                ok = false;
                continue;
            }
            KrtTokenType element = expr->data.array_literal.elements[i]->resolved_type.token;
            if (element == TOKEN_STRING || element == TOKEN_TYPE_STRING) {
                seen_string = true;
            } else if (element != TOKEN_EOF && element != TOKEN_NULL && element != TOKEN_UNKNOWN) {
                seen_other = true;
            }
        }
        if (seen_string && seen_other) {
            semantic_analyzer_add_error_at(analyzer, expr,
                                           "Array literal mixes string and non-string elements");
            return false;
        }
        return ok;
    }

    case AST_TUPLE_EXPRESSION:
        for (int i = 0; i < expr->data.tuple_expr.element_count; i++) {
            if (!semantic_analyzer_analyze_expression(analyzer, expr->data.tuple_expr.elements[i])) {
                return false;
            }
        }
        return true;

    case AST_CALL:
        /* Stage0 emits existing Linux/Windows platforms only. Recognize the
         * native compiler's target query without an unresolved runtime call. */
        if (!expr->data.call.object && expr->data.call.name &&
            !strcmp(expr->data.call.name, "__krt_target_minios")) {
            if (expr->data.call.argument_count != 0) {
                semantic_analyzer_add_error_at(analyzer, expr, "__krt_target_minios expects no arguments");
                return false;
            }
            expr->type = AST_BOOLEAN;
            expr->data.boolean_value = 0;
            expr->resolved_type = (KrtSourceType){.token = TOKEN_BOOL};
            return true;
        }
        return semantic_analyzer_analyze_function_call(analyzer, expr);

    case AST_ASSIGNMENT: {
        const char* var_name = expr->data.assignment.name;
        SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, var_name);
        if (!symbol) {
            semantic_analyzer_add_error_at(analyzer, expr, "Undefined variable: %s", var_name);
            return false;
        }
        if (!semantic_check_assignable(analyzer, expr, var_name, symbol)) {
            return false;
        }
        symbol->is_initialized = 1;
        return semantic_analyzer_analyze_expression(analyzer, expr->data.assignment.value);
    }

    case AST_COMPOUND_ASSIGNMENT: {
        const char* var_name = expr->data.compound_assignment.name;
        SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, var_name);
        if (!symbol) {
            semantic_analyzer_add_error_at(analyzer, expr, "Undefined variable: %s", var_name);
            return false;
        }
        if (!semantic_check_assignable(analyzer, expr, var_name, symbol)) {
            return false;
        }
        symbol->is_initialized = 1;
        return semantic_analyzer_analyze_expression(analyzer, expr->data.compound_assignment.value);
    }

    case AST_TERNARY_OPERATION: {
        bool cond_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.ternary_op.condition);
        bool true_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.ternary_op.true_value);
        bool false_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.ternary_op.false_value);
        KrtTokenType left = semantic_array_element_type(analyzer, expr->data.ternary_op.true_value);
        KrtTokenType right = semantic_array_element_type(analyzer, expr->data.ternary_op.false_value);
        if (left != right && (left != TOKEN_EOF || right != TOKEN_EOF) &&
            expr->data.ternary_op.true_value->type != AST_NULL && expr->data.ternary_op.false_value->type != AST_NULL) {
            semantic_analyzer_add_error_at(analyzer, expr,
                                           "Conditional array operands must have the same element type");
            return false;
        }
        return cond_ok && true_ok && false_ok;
    }

    case AST_NEW_EXPRESSION: {

        if (!expr || !expr->data.new_expr.class_name) {
            semantic_analyzer_add_error(analyzer, "Invalid new expression node");
            return false;
        }

        KrtTokenType type_token = expr->data.new_expr.type_token;
        const char* class_name = expr->data.new_expr.class_name;

        if (type_token == TOKEN_INT32) {

            if (expr->data.new_expr.argument_count > 0 && !expr->data.new_expr.arguments) {
                return true;
            }

            for (int i = 0; i < expr->data.new_expr.argument_count; i++) {
                if (expr->data.new_expr.arguments[i] &&
                    !semantic_analyzer_analyze_expression(analyzer, expr->data.new_expr.arguments[i])) {
                    return false;
                }
            }
            return true;
        }

        if (!class_name || strlen(class_name) == 0) {
            semantic_analyzer_add_error(analyzer, "Invalid class name for new expression");
            return false;
        }

        SymbolEntry* class_symbol = semantic_analyzer_lookup_with_usings(analyzer, class_name);
        if (!class_symbol) {
            semantic_analyzer_add_error(analyzer, "Undefined class: %s", class_name);
            return false;
        }

        if (class_symbol->type != SYMBOL_CLASS) {
            semantic_analyzer_add_error(analyzer, "%s is not a class", class_name);
            return false;
        }

        for (int i = 0; i < expr->data.new_expr.argument_count; i++) {
            if (!semantic_analyzer_analyze_expression(analyzer, expr->data.new_expr.arguments[i])) {
                return false;
            }
        }

        SymbolEntry* constructor =
            semantic_lookup_method_by_arity(analyzer, class_symbol->nested_table, class_name, class_name,
                                            expr->data.new_expr.argument_count, expr->data.new_expr.arguments);
        if (constructor && constructor->source_type.function) {
            expr->function_type = constructor->source_type.function;
            for (int i = 0; i < expr->data.new_expr.argument_count; i++) {
                if (!call_argument_assignable(analyzer, expr->data.new_expr.arguments[i],
                                              expr->function_type->parameters[i])) {
                    return false;
                }
            }
        }
        return true;
    }

    case AST_NEW_ARRAY_EXPRESSION: {

        KrtTokenType type_token = expr->data.new_array_expr.type_token;

        bool known_type = (type_token >= TOKEN_INT2 && type_token < TOKEN_VOID) || type_token == TOKEN_TYPE_STRING;
        if (type_token == TOKEN_IDENTIFIER) {
            const char* name = expr->data.new_array_expr.element_type;
            known_type = semantic_analyzer_lookup_class(analyzer, name) != NULL;
            if (!known_type) {
                semantic_try_lazy_import_class(analyzer, name);
                known_type = semantic_analyzer_lookup_class(analyzer, name) != NULL;
            }
        }
        if (known_type) {

            if (!semantic_analyzer_analyze_expression(analyzer, expr->data.new_array_expr.size)) {
                return false;
            }
            long size = semantic_constant_int(analyzer, expr->data.new_array_expr.size);
            if (size <= 0) {
                semantic_analyzer_add_error_at(analyzer, expr->data.new_array_expr.size,
                                               "Array size must be greater than zero");
                return false;
            }
            return true;
        }

        semantic_analyzer_add_error(analyzer, "Unknown or invalid array element type: %s",
                                    expr->data.new_array_expr.element_type);
        return false;
    }

    case AST_STATIC_METHOD_CALL: {
        const char* class_name = expr->data.static_call.class_name;
        const char* method_name = expr->data.static_call.method_name;

        SymbolEntry* class_symbol = semantic_analyzer_lookup_class(analyzer, class_name);
        if (!class_symbol) {
            semantic_try_lazy_import_class(analyzer, class_name);
            class_symbol = semantic_analyzer_lookup_class(analyzer, class_name);
        }
        if (!class_symbol || class_symbol->type != SYMBOL_CLASS) {
            semantic_analyzer_add_error(analyzer, "Undefined class: %s", class_name);
            return false;
        }

        for (int i = 0; i < expr->data.static_call.argument_count; i++) {
            if (!semantic_analyzer_analyze_expression(analyzer, expr->data.static_call.arguments[i])) {
                return false;
            }
        }

        SymbolEntry* method_symbol = NULL;
        char* mangled_name = NULL;
        if (class_symbol->nested_table) {
            KrtTokenType* arg_types = NULL;
            if (expr->data.static_call.argument_count > 0) {
                arg_types = (KrtTokenType*)KRT_MALLOC(sizeof(KrtTokenType) * expr->data.static_call.argument_count);
                if (!arg_types) {
                    semantic_analyzer_add_error(analyzer, "Memory allocation failed");
                    return false;
                }
                for (int i = 0; i < expr->data.static_call.argument_count; i++) {
                    arg_types[i] =
                        semantic_analyzer_infer_expression_type(analyzer, expr->data.static_call.arguments[i]);
                }
            }
            const char* ns_path[2] = {class_name, NULL};
            mangled_name = name_mangle_function(ns_path, method_name, arg_types, expr->data.static_call.argument_count);
            if (arg_types) {
                KRT_FREE(arg_types);
            }

            if (mangled_name) {
                method_symbol = semantic_lookup_exact_in_class(class_symbol->nested_table, mangled_name);
            }
            if (!method_symbol) {
                method_symbol = semantic_lookup_method_by_arity(analyzer, class_symbol->nested_table, class_name,
                                                                method_name, expr->data.static_call.argument_count,
                                                                expr->data.static_call.arguments);
            }
        }

        if (!method_symbol) {
            method_symbol = symbol_table_lookup_in_namespace(analyzer->symbol_table, class_name, method_name);
            if (!method_symbol) {
                method_symbol = semantic_lookup_method_by_arity(analyzer, analyzer->symbol_table, class_name,
                                                                method_name, expr->data.static_call.argument_count,
                                                                expr->data.static_call.arguments);
            }
            if (!method_symbol) {
                method_symbol = semantic_lookup_method_in_global_class(analyzer, class_name, method_name,
                                                                       expr->data.static_call.argument_count,
                                                                       expr->data.static_call.arguments);
            }
            if (!method_symbol) {
                semantic_analyzer_add_error(analyzer, "Undefined static method: %s::%s", class_name, method_name);
                if (mangled_name) {
                    KRT_FREE(mangled_name);
                }
                return false;
            }
        }

        if (method_symbol->type != SYMBOL_FUNCTION) {
            semantic_analyzer_add_error(analyzer, "%s::%s is not a static method", class_name, method_name);
            if (mangled_name) {
                KRT_FREE(mangled_name);
            }
            return false;
        }

        if (method_symbol->name && method_symbol->name[0] == '_' && method_symbol->name[1] == 'Z' &&
            (!mangled_name || strcmp(method_symbol->name, mangled_name) != 0)) {
            if (mangled_name) {
                KRT_FREE(mangled_name);
            }
            mangled_name = KRT_STRDUP(method_symbol->name);
        }

        if (mangled_name) {
            if (expr->data.static_call.resolved_mangled_name) {
                KRT_FREE(expr->data.static_call.resolved_mangled_name);
            }
            expr->data.static_call.resolved_mangled_name = mangled_name;
        }

        return true;
    }

    case AST_MEMBER_ACCESS: {
        bool object_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.member_access.object);
        if (!object_ok) {
            return false;
        }

        ASTNode* object = expr->data.member_access.object;
        const char* member_name = expr->data.member_access.member_name;

        /* Instance fields must retain their owner for IR address calculation.
         * Treating parameter.field as a static name silently creates a different
         * storage location instead of loading/storing through the parameter. */
        /* One source of truth: the variable's recorded class type wins, and
         * the expression's resolved type name is only a fallback. */
        const char* instance_name = NULL;
        if (object && object->type == AST_IDENTIFIER) {
            SymbolEntry* variable = symbol_table_lookup(analyzer->symbol_table, object->data.identifier_name);
            if (variable && variable->type != SYMBOL_CLASS && variable->class_type_name) {
                instance_name = variable->class_type_name;
            }
        }
        if (!instance_name && object) {
            instance_name = object->resolved_type.type_name;
        }
        if (instance_name && object && object->type != AST_THIS) {
            SymbolEntry* owner = semantic_analyzer_lookup_class(analyzer, instance_name);
            SymbolEntry* field =
                owner && owner->nested_table ? symbol_table_lookup(owner->nested_table, member_name) : NULL;
            if (field && field->type == SYMBOL_FIELD) {
                if (!semantic_set_resolved_name(&expr->data.member_access.resolved_class_name, instance_name)) {
                    semantic_analyzer_add_error(analyzer, "Out of memory while resolving a member name");
                    return false;
                }
                expr->data.member_access.resolved_is_array = field->is_array;
                expr->resolved_type = field->source_type;
                if (!expr->resolved_type.token) {
                    expr->resolved_type.token = field->value_type;
                }
                return true;
            }
        }

        if (object && (object->type == AST_THIS ||
                       (object->type == AST_IDENTIFIER && strcmp(object->data.identifier_name, "this") == 0))) {
            const char* current_class = semantic_analyzer_get_current_class_context(analyzer);
            if (current_class) {
                SymbolEntry* member = symbol_table_lookup_scope_chain(analyzer->symbol_table, member_name);
                if (member && (member->type == SYMBOL_FIELD || member->type == SYMBOL_FUNCTION ||
                               member->type == SYMBOL_STATIC_FIELD || member->type == SYMBOL_VARIABLE)) {
                    if (!semantic_set_resolved_name(&expr->data.member_access.resolved_class_name, current_class)) {
                        semantic_analyzer_add_error(analyzer, "Out of memory while resolving a member name");
                        return false;
                    }
                    expr->data.member_access.resolved_is_array = member->is_array;
                    expr->resolved_type = member->source_type;
                    if (!expr->resolved_type.token) {
                        expr->resolved_type.token = member->value_type;
                    }
                    return true;
                }
                semantic_analyzer_add_error(analyzer, "Undefined member '%s' in class '%s'", member_name,
                                            current_class);
                return false;
            }
        }

        if (object && object->type == AST_IDENTIFIER) {
            const char* class_name = object->data.identifier_name;
            SymbolEntry* class_symbol = semantic_analyzer_lookup_class(analyzer, class_name);
            if (class_symbol && class_symbol->type == SYMBOL_CLASS) {
                char* mangled_name = name_mangle_simple(class_name, member_name);
                SymbolEntry* member = NULL;
                if (class_symbol->nested_table && mangled_name) {
                    member = symbol_table_lookup(class_symbol->nested_table, mangled_name);
                }
                if (!member && class_symbol->nested_table) {
                    member = symbol_table_lookup(class_symbol->nested_table, member_name);
                }
                if (member && (member->type == SYMBOL_STATIC_FIELD || member->type == SYMBOL_FUNCTION)) {
                    const char* from_class = semantic_analyzer_get_current_class_context(analyzer);
                    if (member->access == TOKEN_PRIVATE &&
                        (!from_class || !class_name || strcmp(from_class, class_name) != 0)) {
                        semantic_analyzer_add_error_at(analyzer, expr, "Cannot access private member '%s' of %s",
                                                       member_name, class_name);
                        if (mangled_name) {
                            KRT_FREE(mangled_name);
                        }
                        return false;
                    }
                    if (expr->data.member_access.resolved_class_name) {
                        KRT_FREE(expr->data.member_access.resolved_class_name);
                    }
                    if (expr->data.member_access.resolved_mangled_name) {
                        KRT_FREE(expr->data.member_access.resolved_mangled_name);
                    }
                    expr->data.member_access.resolved_class_name = KRT_STRDUP(class_name);
                    expr->data.member_access.resolved_mangled_name = mangled_name;
                    expr->data.member_access.resolved_is_array = member->is_array;
                    expr->resolved_type = member->source_type;
                    if (!expr->resolved_type.token) {
                        expr->resolved_type.token = member->value_type;
                    }
                    return true;
                }
                if (mangled_name) {
                    KRT_FREE(mangled_name);
                }
            }
        }

        /* Resolve built-in Length properties here, leaving code generation an
         * explicit semantic tag instead of inferring behavior from the name.
         * A user-declared member of that name always wins. */
        if (member_name && strcmp(member_name, "Length") == 0 &&
            !symbol_table_lookup_scope_chain(analyzer->symbol_table, "Length")) {
            KrtTokenType element_type = semantic_array_element_type(analyzer, object);
            if (element_type != TOKEN_EOF) {
                expr->data.member_access.resolved_builtin_member = KRT_BUILTIN_MEMBER_ARRAY_LENGTH;
                expr->resolved_type = (KrtSourceType){.token = TOKEN_INT32};
                return true;
            }
            KrtTokenType object_type = semantic_analyzer_infer_expression_type(analyzer, object);
            if (object_type == TOKEN_TYPE_STRING || object_type == TOKEN_STRING) {
                expr->data.member_access.resolved_builtin_member = KRT_BUILTIN_MEMBER_STRING_LENGTH;
                expr->resolved_type = (KrtSourceType){.token = TOKEN_INT32};
                return true;
            }
        }

        /* Nothing above claimed this member: it does not exist.  Falling through
         * used to accept `(5).Length`, `null.Length` and unknown members. */
        if (object_ok) {
            semantic_analyzer_add_error_at(analyzer, expr, "Undefined member '%s'", member_name);
            return false;
        }
        return object_ok;
    }

    case AST_ARRAY_ACCESS: {
        bool array_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.array_access.array);
        bool index_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.array_access.index);
        if (index_ok && !semantic_check_array_index(analyzer, expr, expr->data.array_access.index)) {
            return false;
        }

        if (array_ok && expr->data.array_access.array->type == AST_IDENTIFIER) {
            const char* array_name = expr->data.array_access.array->data.identifier_name;
            SymbolEntry* symbol = symbol_table_lookup(analyzer->symbol_table, array_name);
            if (symbol && !symbol->is_array && !KrtSourceIsPointer(symbol->source_type)) {
                semantic_analyzer_add_error(analyzer, "Attempting to access non-array as array");
                return false;
            }
            long size = symbol ? symbol->const_array_size : -1;
            long index = semantic_constant_int(analyzer, expr->data.array_access.index);
            if (size >= 0 && index >= size) {
                semantic_analyzer_add_error_at(analyzer, expr->data.array_access.index,
                                               "Array index %ld is out of bounds (size %ld)", index, size);
                return false;
            }
        }

        return array_ok && index_ok;
    }

    case AST_LAMBDA_EXPRESSION: {

        symbol_table_push_scope(analyzer->symbol_table);

        for (int i = 0; i < expr->data.lambda_expr.parameter_count; i++) {
            const char* param_name = expr->data.lambda_expr.parameters[i];
            if (!param_name) {
                semantic_analyzer_add_error(analyzer, "Lambda parameter %d has no name", i);
                symbol_table_pop_scope(analyzer->symbol_table);
                return false;
            }
            SymbolEntry* param = symbol_table_define(analyzer->symbol_table, param_name, SYMBOL_VARIABLE, 0, NULL);
            if (!param) {
                semantic_analyzer_add_error(analyzer, "Failed to define lambda parameter: %s", param_name);
                symbol_table_pop_scope(analyzer->symbol_table);
                return false;
            }
        }

        bool body_ok = false;
        if (expr->data.lambda_expr.expression) {
            body_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.lambda_expr.expression);
        } else if (expr->data.lambda_expr.body) {
            body_ok = semantic_analyzer_analyze_statement(analyzer, expr->data.lambda_expr.body);
        } else {
            semantic_analyzer_add_error_at(analyzer, expr, "Lambda has no body");
            symbol_table_pop_scope(analyzer->symbol_table);
            return false;
        }

        symbol_table_pop_scope(analyzer->symbol_table);

        return body_ok;
    }

    case AST_LINQ_QUERY: {
        bool ok = true;

        if (expr->data.linq_query.from_clause) {
            bool sub = semantic_analyzer_analyze_expression(
                analyzer, expr->data.linq_query.from_clause->data.linq_from.source);
            ok = sub && ok;
        }

        for (int i = 0; i < expr->data.linq_query.clause_count; i++) {
            ASTNode* clause = expr->data.linq_query.clauses[i];
            if (!clause) {
                semantic_analyzer_add_error_at(analyzer, expr, "LINQ query has a null clause");
                ok = false;
                continue;
            }
            if (clause->type == AST_LINQ_WHERE) {
                bool sub = semantic_analyzer_analyze_expression(analyzer, clause->data.linq_where.condition);
                ok = sub && ok;
            } else if (clause->type == AST_LINQ_ORDERBY) {
                bool sub = semantic_analyzer_analyze_expression(analyzer, clause->data.linq_orderby.expression);
                ok = sub && ok;
            } else {
                semantic_analyzer_add_error_at(analyzer, clause, "Unsupported LINQ clause");
                ok = false;
            }
        }

        if (expr->data.linq_query.select_clause) {
            bool sub = semantic_analyzer_analyze_expression(
                analyzer, expr->data.linq_query.select_clause->data.linq_select.expression);
            ok = sub && ok;
            if (sub && expr->data.linq_query.select_clause->data.linq_select.expression->resolved_type.token) {
                expr->resolved_type = expr->data.linq_query.select_clause->data.linq_select.expression->resolved_type;
            }
        }

        return ok;
    }

    case AST_NULL_COALESCING: {
        bool left_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.null_coalescing.left);
        bool right_ok = semantic_analyzer_analyze_expression(analyzer, expr->data.null_coalescing.right);
        if (left_ok && right_ok) {
            KrtTokenType left = semantic_analyzer_infer_expression_type(analyzer, expr->data.null_coalescing.left);
            KrtTokenType right = semantic_analyzer_infer_expression_type(analyzer, expr->data.null_coalescing.right);
            expr->resolved_type.token = left != TOKEN_NULL ? left : right;
        }
        return left_ok && right_ok;
    }

    case AST_CAST_EXPRESSION: {
        if (!expr->data.cast_expr.expression) {
            semantic_analyzer_add_error_at(analyzer, expr, "Cast expression has no operand");
            return false;
        }
        return semantic_analyzer_analyze_expression(analyzer, expr->data.cast_expr.expression);
    }

    case AST_THIS:
    case AST_NULL:
        return true;

    case AST_SIZEOF_EXPRESSION:
        /* sizeof yields a machine word, not an int32; IR emits a 64-bit one. */
        expr->resolved_type.token = KrtTokenIntegerType(64, true);
        return true;

    default:
        semantic_analyzer_add_error_at(analyzer, expr, "Unsupported expression node kind %d", (int)expr->type);
        return false;
    }
}

/* Checks a call against a known function signature: arity always, argument
 * types only when both sides are known scalars. */
static bool semantic_check_call_signature(SemanticAnalyzer* analyzer, ASTNode* call_expr, SymbolEntry* function) {
    if (!analyzer || !call_expr || !function) {
        return true;
    }
    KrtFunctionType* fn = function->source_type.function;
    if (!fn) {
        return true;
    }
    if (fn->parameter_count != call_expr->data.call.argument_count) {
        semantic_analyzer_add_error_at(analyzer, call_expr, "Function '%s' expects %d argument(s), got %d",
                                       call_expr->data.call.name, fn->parameter_count,
                                       call_expr->data.call.argument_count);
        return false;
    }
    for (int i = 0; i < fn->parameter_count; i++) {
        KrtSourceType parameter = fn->parameters[i];
        if (parameter.token == TOKEN_EOF) {
            continue;
        }
        KrtTokenType arg_type =
            semantic_analyzer_infer_expression_type(analyzer, call_expr->data.call.arguments[i]);
        if (arg_type == TOKEN_UNKNOWN || arg_type == TOKEN_EOF || arg_type == TOKEN_NULL) {
            continue;
        }
        bool parameter_string = parameter.token == TOKEN_STRING || parameter.token == TOKEN_TYPE_STRING;
        bool arg_string = arg_type == TOKEN_STRING || arg_type == TOKEN_TYPE_STRING;
        if (parameter_string != arg_string) {
            semantic_analyzer_add_error_at(analyzer, call_expr,
                                           "Argument %d of '%s' cannot be passed to a %s parameter", i + 1,
                                           call_expr->data.call.name, parameter_string ? "string" : "non-string");
            return false;
        }
    }
    return true;
}

bool semantic_analyzer_analyze_function_call(SemanticAnalyzer* analyzer, ASTNode* call_expr) {
    if (!analyzer || !call_expr || call_expr->type != AST_CALL) {
        return false;
    }

    if (!call_expr->data.call.object &&
        (strcmp(call_expr->data.call.name, "KrtMalloc") == 0 || strcmp(call_expr->data.call.name, "KrtFree") == 0) &&
        analyzer->point_depth == 0) {
        semantic_analyzer_add_error_at(analyzer, call_expr, "%s can only be used inside a Point block",
                                       call_expr->data.call.name);
        return false;
    }

    char* func_name = NULL;

    if (strncmp(call_expr->data.call.name, "global::", 8) == 0) {
        call_expr->data.call.is_global_scope = 1;
        call_expr->data.call.name = call_expr->data.call.name + 8;
    }

    if (strcmp(call_expr->data.call.name, "syscall") == 0) {
        for (int i = 0; i < call_expr->data.call.argument_count; i++) {
            if (!semantic_analyzer_analyze_expression(analyzer, call_expr->data.call.arguments[i])) {
                return false;
            }
        }
        if (call_expr->data.call.resolved_mangled_name) {
            KRT_FREE(call_expr->data.call.resolved_mangled_name);
            call_expr->data.call.resolved_mangled_name = NULL;
        }
        return true;
    }

    if (!call_expr->data.call.object && !call_expr->data.call.is_global_scope) {
        const char* current_class = semantic_analyzer_get_current_class_context(analyzer);
        if (current_class) {
            for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                if (!semantic_analyzer_analyze_expression(analyzer, call_expr->data.call.arguments[i])) {
                    return false;
                }
            }

            KrtTokenType* arg_types = NULL;
            if (call_expr->data.call.argument_count > 0) {
                arg_types = (KrtTokenType*)KRT_MALLOC(sizeof(KrtTokenType) * call_expr->data.call.argument_count);
                if (!arg_types) {
                    semantic_analyzer_add_error(analyzer, "Memory allocation failed");
                    return false;
                }
                for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                    arg_types[i] = semantic_analyzer_infer_expression_type(analyzer, call_expr->data.call.arguments[i]);
                }
            }

            const char* ns_path[2] = {current_class, NULL};
            char* mangled_name = name_mangle_function(ns_path, call_expr->data.call.name, arg_types,
                                                      call_expr->data.call.argument_count);
            KRT_FREE(arg_types);
            if (!mangled_name) {
                semantic_analyzer_add_error(analyzer, "Failed to mangle method %s", call_expr->data.call.name);
                return false;
            }

            {
                SymbolEntry* owner_class = semantic_analyzer_lookup_class(analyzer, current_class);
                SymbolEntry* method_symbol =
                    (owner_class && owner_class->nested_table)
                        ? semantic_lookup_exact_in_class(owner_class->nested_table, mangled_name)
                        : NULL;
                if (!method_symbol && owner_class && owner_class->nested_table) {
                    method_symbol = semantic_lookup_method_by_arity(
                        analyzer, owner_class->nested_table, current_class, call_expr->data.call.name,
                        call_expr->data.call.argument_count, call_expr->data.call.arguments);
                }
                if (!method_symbol) {
                    /*
                     * Not a member of the current class.  That is legitimate
                     * for a namespace-level function (IrGen mangles those
                     * without a class qualifier), so look there before
                     * complaining; otherwise an unresolved name only surfaces
                     * as an "Undefined symbol" at link time.
                     */
                    KrtTokenType* gtypes = NULL;
                    int gargc = call_expr->data.call.argument_count;
                    if (gargc > 0) {
                        gtypes = (KrtTokenType*)KRT_MALLOC(sizeof(KrtTokenType) * (size_t)gargc);
                        if (gtypes) {
                            for (int i = 0; i < gargc; i++) {
                                gtypes[i] = semantic_analyzer_infer_expression_type(analyzer,
                                                                                    call_expr->data.call.arguments[i]);
                            }
                        }
                    }
                    char* global_name =
                        name_mangle_function(NULL, call_expr->data.call.name, gtypes, gargc);
                    KRT_FREE(gtypes);
                    SymbolTable* gtable =
                        analyzer->global_symbol_table ? analyzer->global_symbol_table : analyzer->symbol_table;
                    SymbolEntry* global = symbol_table_lookup_scope_chain(gtable, call_expr->data.call.name);
                    if (!global && global_name) {
                        global = symbol_table_lookup_scope_chain(gtable, global_name);
                    }
                    if (!global) {
                        semantic_analyzer_add_error_at(analyzer, call_expr, "Undefined function: %s",
                                                       call_expr->data.call.name);
                        KRT_FREE(global_name);
                        KRT_FREE(mangled_name);
                        return false;
                    }
                    KRT_FREE(global_name);
                    KRT_FREE(mangled_name);
                    return true;
                }
                if (mangled_name && strcmp(method_symbol->name, mangled_name) != 0) {
                    KRT_FREE(mangled_name);
                    mangled_name = KRT_STRDUP(method_symbol->name);
                }
            }

            KRT_FREE(call_expr->data.call.resolved_mangled_name);
            call_expr->data.call.resolved_mangled_name = mangled_name;
            return true;
        }
    }

    if (call_expr->data.call.object && call_expr->data.call.object->type == AST_IDENTIFIER) {
        const char* method_name = call_expr->data.call.name;
        if (!method_name) {
            semantic_analyzer_add_error_at(analyzer, call_expr, "Method call is missing its name");
            return false;
        }

        const char* obj_name = call_expr->data.call.object->data.identifier_name;
        SymbolEntry* var_sym = symbol_table_lookup_scope_chain(analyzer->symbol_table, obj_name);
        if (var_sym && var_sym->class_type_name) {
            const char* inst_class = var_sym->class_type_name;
            SymbolEntry* cls = semantic_analyzer_lookup_class(analyzer, inst_class);
            if (!cls || !cls->nested_table) {
                semantic_analyzer_add_error_at(analyzer, call_expr, "Undefined class: %s", inst_class);
                return false;
            }

            for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                if (!semantic_analyzer_analyze_expression(analyzer, call_expr->data.call.arguments[i])) {
                    return false;
                }
            }

            KrtTokenType* arg_types = NULL;
            if (call_expr->data.call.argument_count > 0) {
                arg_types = (KrtTokenType*)KRT_MALLOC(sizeof(KrtTokenType) * call_expr->data.call.argument_count);
                if (!arg_types) {
                    semantic_analyzer_add_error(analyzer, "Memory allocation failed");
                    return false;
                }
                for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                    arg_types[i] = semantic_analyzer_infer_expression_type(analyzer, call_expr->data.call.arguments[i]);
                }
            }

            const char* ns_path[2] = {inst_class, NULL};
            char* mangled_name =
                name_mangle_function(ns_path, method_name, arg_types, call_expr->data.call.argument_count);
            KRT_FREE(arg_types);

            SymbolEntry* method_symbol =
                mangled_name ? semantic_lookup_exact_in_class(cls->nested_table, mangled_name) : NULL;
            if (!method_symbol) {
                method_symbol = semantic_lookup_method_by_arity(analyzer, cls->nested_table, inst_class, method_name,
                                                                call_expr->data.call.argument_count,
                                                                call_expr->data.call.arguments);
            }
            if (!method_symbol) {
                method_symbol = semantic_lookup_method_in_global_class(analyzer, inst_class, method_name,
                                                                       call_expr->data.call.argument_count,
                                                                       call_expr->data.call.arguments);
            }
            if (!method_symbol || method_symbol->type != SYMBOL_FUNCTION) {
                semantic_analyzer_add_error_at(analyzer, call_expr, "Undefined method: %s.%s", inst_class, method_name);
                KRT_FREE(mangled_name);
                return false;
            }
            if (mangled_name && strcmp(method_symbol->name, mangled_name) != 0) {
                KRT_FREE(mangled_name);
                mangled_name = KRT_STRDUP(method_symbol->name);
            }

            KRT_FREE(call_expr->data.call.resolved_class_name);
            KRT_FREE(call_expr->data.call.resolved_mangled_name);
            call_expr->data.call.resolved_class_name = KRT_STRDUP(inst_class);
            call_expr->data.call.resolved_mangled_name = mangled_name;
            call_expr->data.call.is_instance_call = 1;
            return true;
        }

        const char* class_name = call_expr->data.call.object->data.identifier_name;
        if (!class_name) {
            semantic_analyzer_add_error_at(analyzer, call_expr, "Class name is missing");
            return false;
        }
        if (strncmp(class_name, "global::", 8) == 0) {
            class_name += 8;
        }
        SymbolEntry* class_symbol = semantic_analyzer_lookup_class(analyzer, class_name);
        if (!class_symbol) {
            semantic_try_lazy_import_class(analyzer, class_name);
            class_symbol = semantic_analyzer_lookup_class(analyzer, class_name);
        }
        if (!class_symbol && !var_sym) {
            /* Neither an object nor a class: report the real problem. */
            semantic_analyzer_add_error_at(analyzer, call_expr, "Undefined variable: %s", obj_name);
            return false;
        }
        if (class_symbol) {
            for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                if (!semantic_analyzer_analyze_expression(analyzer, call_expr->data.call.arguments[i])) {
                    return false;
                }
            }

            KrtTokenType* arg_types = NULL;
            if (call_expr->data.call.argument_count > 0) {
                arg_types = (KrtTokenType*)KRT_MALLOC(sizeof(KrtTokenType) * call_expr->data.call.argument_count);
                if (!arg_types) {
                    semantic_analyzer_add_error(analyzer, "Memory allocation failed");
                    return false;
                }
                for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                    arg_types[i] = semantic_analyzer_infer_expression_type(analyzer, call_expr->data.call.arguments[i]);
                }
            }

            const char* ns_path[2] = {class_name, NULL};
            char* mangled_name =
                name_mangle_function(ns_path, method_name, arg_types, call_expr->data.call.argument_count);
            KRT_FREE(arg_types);

            SymbolEntry* method_symbol = NULL;
            if (mangled_name && class_symbol->nested_table) {
                method_symbol = semantic_lookup_exact_in_class(class_symbol->nested_table, mangled_name);
                if (!method_symbol) {
                    method_symbol = semantic_lookup_method_by_arity(analyzer, class_symbol->nested_table, class_name,
                                                                    method_name, call_expr->data.call.argument_count,
                                                                    call_expr->data.call.arguments);
                }
            }
            if (!method_symbol) {
                SymbolTable* gtable =
                    analyzer->global_symbol_table ? analyzer->global_symbol_table : analyzer->symbol_table;
                method_symbol = semantic_lookup_method_by_arity(analyzer, gtable, class_name, method_name,
                                                                call_expr->data.call.argument_count,
                                                                call_expr->data.call.arguments);
            }
            if (!method_symbol) {
                method_symbol = semantic_lookup_method_in_global_class(analyzer, class_name, method_name,
                                                                       call_expr->data.call.argument_count,
                                                                       call_expr->data.call.arguments);
            }
            if (method_symbol && method_symbol->type == SYMBOL_FUNCTION &&
                method_symbol->access == TOKEN_PRIVATE) {
                const char* from_class = semantic_analyzer_get_current_class_context(analyzer);
                if (!from_class || strcmp(from_class, class_name) != 0) {
                    semantic_analyzer_add_error_at(analyzer, call_expr,
                                                   "Cannot access private method '%s' of %s", method_name, class_name);
                    KRT_FREE(mangled_name);
                    return false;
                }
            }
            if (method_symbol && method_symbol->type == SYMBOL_FUNCTION && method_symbol->name &&
                method_symbol->name[0] == '_' && method_symbol->name[1] == 'Z') {
                if (mangled_name) {
                    KRT_FREE(mangled_name);
                }
                mangled_name = KRT_STRDUP(method_symbol->name);

                KRT_FREE(call_expr->data.call.resolved_class_name);
                KRT_FREE(call_expr->data.call.resolved_mangled_name);
                call_expr->data.call.resolved_class_name = KRT_STRDUP(class_name);
                call_expr->data.call.resolved_mangled_name = mangled_name;
                return true;
            }
            if (mangled_name) {
                KRT_FREE(mangled_name);
            }
            semantic_analyzer_add_error_at(analyzer, call_expr, "Undefined method: %s.%s (names are case-sensitive)",
                                           class_name, method_name);
            return false;
        }

        if (strstr(method_name, "__") != NULL) {

            func_name = KRT_STRDUP(method_name);
        } else {

            func_name = name_mangle_simple(class_name, method_name);
            if (func_name) {
                if (call_expr->data.call.resolved_class_name) {
                    KRT_FREE(call_expr->data.call.resolved_class_name);
                }
                call_expr->data.call.resolved_class_name = KRT_STRDUP(class_name);
            }
        }
    } else {

        func_name = KRT_STRDUP(call_expr->data.call.name);
    }

    if (!func_name) {
        semantic_analyzer_add_error(analyzer, "Memory allocation failed for function name");
        return false;
    }

    SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, func_name);

    if (!symbol) {
        semantic_analyzer_add_error_at(analyzer, call_expr, "Undefined function: %s", func_name);
        KRT_FREE(func_name);
        return false;
    }

    if (symbol->type != SYMBOL_FUNCTION) {

        if (symbol->type == SYMBOL_VARIABLE) {
            if (symbol->source_type.token != TOKEN_FN) {
                semantic_analyzer_add_error_at(analyzer, call_expr, "'%s' is not a function", func_name);
                KRT_FREE(func_name);
                return false;
            }
            KRT_FREE(func_name);

            for (int i = 0; i < call_expr->data.call.argument_count; i++) {
                if (!semantic_analyzer_analyze_expression(analyzer, call_expr->data.call.arguments[i])) {
                    return false;
                }
            }
            return true;
        }

        semantic_analyzer_add_error_at(analyzer, call_expr, "%s is not a function", func_name);
        KRT_FREE(func_name);
        return false;
    }

    if (!call_expr->data.call.object) {
        char* old_name = call_expr->data.call.name;
        call_expr->data.call.name = func_name;
        KRT_FREE(old_name);
    } else {
        KRT_FREE(func_name);
    }

    for (int i = 0; i < call_expr->data.call.argument_count; i++) {
        if (!semantic_analyzer_analyze_expression(analyzer, call_expr->data.call.arguments[i])) {
            return false;
        }
    }

    bool signature_ok = semantic_check_call_signature(analyzer, call_expr, symbol);
    KRT_FREE(func_name);
    return signature_ok;
}

bool semantic_analyzer_analyze_variable_decl(SemanticAnalyzer* analyzer, ASTNode* var_decl) {
    if (!analyzer || !var_decl) {
        return false;
    }

    const char* var_name = var_decl->data.variable_decl.name;
    int is_let = var_decl->data.variable_decl.is_let;

    if (!var_name) {
        semantic_analyzer_add_error_at(analyzer, var_decl, "Variable declaration is missing its name");
        return false;
    }

    if (var_decl->data.variable_decl.pointer_depth > 0 && !analyzer->unsafe_mem && analyzer->point_depth == 0) {
        semantic_analyzer_add_error_at(
            analyzer, var_decl, "Pointer variable '%s' requires unsafe(using krt.mem;) or a Point block", var_name);
        return false;
    }

    if (is_let) {

        if (!analyzer->symbol_table->current_scope) {
            semantic_analyzer_add_error_at(analyzer, var_decl, "let is used outside any scope");
            return false;
        }
        if (analyzer->symbol_table->current_scope->scope_level == KRT_GLOBAL_SCOPE_LEVEL + 1) {
            semantic_analyzer_add_error_at(analyzer, var_decl, "let cannot be used at global scope, use var instead");
            return false;
        }
    }

    SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, var_name);
    if (existing) {
        semantic_analyzer_add_error_at(analyzer, var_decl, "Variable %s already declared", var_name);
        return false;
    }

    if (var_decl->data.variable_decl.template_instantiation_type) {

        char* template_type = var_decl->data.variable_decl.template_instantiation_type;

        char* bracket_pos = strchr(template_type, '<');
        if (bracket_pos && !semantic_analyzer_lookup_class(analyzer, template_type)) {

            int base_type_len = bracket_pos - template_type;
            char* base_type = KRT_MALLOC(base_type_len + 1);
            if (base_type) {
                strncpy(base_type, template_type, base_type_len);
                base_type[base_type_len] = '\0';

                char* args_start = bracket_pos + 1;
                char* args_end = strrchr(args_start, '>');
                if (args_end) {
                    int args_len = args_end - args_start;
                    char* args_str = KRT_MALLOC(args_len + 1);
                    if (args_str) {
                        strncpy(args_str, args_start, args_len);
                        args_str[args_len] = '\0';

                        char** type_args = KRT_MALLOC(sizeof(char*));
                        int arg_count = 0;

                        if (args_str && strlen(args_str) > 0) {
                            type_args[0] = KRT_STRDUP(args_str);
                            arg_count = 1;
                        }

                        bool valid = semantic_analyzer_analyze_generic_instantiation(
                            analyzer, base_type, (const char**)type_args, arg_count);

                        for (int i = 0; i < arg_count; i++) {
                            if (type_args[i]) {
                                KRT_FREE(type_args[i]);
                            }
                        }
                        KRT_FREE(type_args);
                        KRT_FREE(args_str);
                        KRT_FREE(base_type);

                        if (!valid) {
                            semantic_analyzer_add_error(analyzer, "Invalid template instantiation: %s", template_type);
                            return false;
                        }
                    } else {
                        semantic_analyzer_add_error(analyzer, "Out of memory while parsing template arguments");
                        KRT_FREE(base_type);
                        return false;
                    }
                } else {
                    semantic_analyzer_add_error_at(analyzer, var_decl,
                                                   "Unterminated template instantiation: %s", template_type);
                    KRT_FREE(base_type);
                }
            } else {
                semantic_analyzer_add_error(analyzer, "Out of memory while parsing template type");
                return false;
            }
        }
    }

    const char* current_class = semantic_analyzer_get_current_class_context(analyzer);
    /* Only a declaration directly in the class body is a field; a local
     * inside a method must stay a variable. */
    SymbolType symbol_type = SYMBOL_VARIABLE;
    if (current_class && analyzer->symbol_table->current_scope &&
        analyzer->symbol_table->current_scope->scope_level == KRT_GLOBAL_SCOPE_LEVEL) {
        symbol_type = SYMBOL_FIELD;
    }

    SymbolEntry* symbol = symbol_table_define(analyzer->symbol_table, var_name, symbol_type, 0, NULL);
    if (!symbol) {
        semantic_analyzer_add_error(analyzer, "Failed to declare variable %s", var_name);
        return false;
    }

    symbol->value_type = var_decl->data.variable_decl.type;
    symbol->access = var_decl->data.variable_decl.access;
    symbol->const_array_size = -1;
    symbol->is_initialized = var_decl->data.variable_decl.value != NULL;
    symbol->was_read = 0;

    if (var_decl->data.variable_decl.type == TOKEN_IDENTIFIER &&
        var_decl->data.variable_decl.template_instantiation_type) {
        SymbolEntry* cls_sym =
            semantic_analyzer_lookup_class(analyzer, var_decl->data.variable_decl.template_instantiation_type);
        if (cls_sym && cls_sym->type == SYMBOL_CLASS) {
            symbol->class_type_name = KRT_STRDUP(var_decl->data.variable_decl.template_instantiation_type);
            if (!symbol->class_type_name) {
                semantic_analyzer_add_error(analyzer, "Out of memory while naming class type");
                return false;
            }
        }
    }

    if (var_decl->data.variable_decl.is_array) {
        symbol->is_array = 1;

        if (var_decl->data.variable_decl.array_size) {
            if (!semantic_analyzer_analyze_expression(analyzer, var_decl->data.variable_decl.array_size)) {
                return false;
            }
            long size = semantic_constant_int(analyzer, var_decl->data.variable_decl.array_size);
            if (size >= 0) {
                symbol->const_array_size = size;
            } else {
                semantic_analyzer_add_warning(analyzer,
                                              "Array size is not a constant expression; length tracking disabled");
            }
        }
    } else if (var_decl->data.variable_decl.value && var_decl->data.variable_decl.value->type == AST_ARRAY_LITERAL) {
        symbol->is_array = 1;
    }

    /* int32[] a = new int32[3]; */
    if (var_decl->data.variable_decl.value &&
        var_decl->data.variable_decl.value->type == AST_NEW_ARRAY_EXPRESSION) {
        ASTNode* size = var_decl->data.variable_decl.value->data.new_array_expr.size;
        long n = semantic_constant_int(analyzer, size);
        if (n >= 0) {
            symbol->is_array = 1;
            symbol->const_array_size = n;
        }
    }

    KrtSourceType type = var_decl->declared_type;
    if (!type.token) {
        type.token = var_decl->data.variable_decl.type;
    }
    /* `void*` is a valid pointer type; only a plain void variable is invalid. */
    if ((type.token == TOKEN_VOID || var_decl->data.variable_decl.type == TOKEN_VOID) &&
        type.pointer_depth == 0) {
        semantic_analyzer_add_error_at(analyzer, var_decl, "Cannot declare a variable of type void");
        return false;
    }
    if (var_decl->data.variable_decl.value) {
        ASTNode* value = var_decl->data.variable_decl.value;
        if (!semantic_analyzer_analyze_expression(analyzer, value)) {
            return false;
        }
        if (type.token == TOKEN_AUTO) {
            type = value->resolved_type;
        } else {
            int bits = KrtTokenIntegerBits(type.token);
            if (value->type == AST_NUMBER && value->is_integer_literal && bits > 0) {
                KrtUInt128 max_value = KrtIntegerMask(bits);
                if (!KrtTokenIsUnsigned(type.token)) {
                    max_value >>= 1;
                }
                if (value->integer_value > max_value) {
                    semantic_analyzer_add_error_at(analyzer, value,
                                                   "Integer literal is outside the range of the declared type");
                    return false;
                }
            }
            if (!pointer_assignable(analyzer, var_decl, type, value->resolved_type)) {
                return false;
            }
        }
    } else if (KrtSourceIsPointer(type) && !type.nullable) {
        return pointer_error(analyzer, var_decl, "Non-nullable pointer requires an initializer");
    }
    if (KrtSourceIsPointer(type) && !require_memory_permission(analyzer, var_decl)) {
        return false;
    }
    if (type.token == TOKEN_IDENTIFIER && type.type_name && !KrtSourceIsPointer(type)) {
        /* `NoSuchType x;` used to compile silently. */
        if (!semantic_analyzer_lookup_class(analyzer, type.type_name)) {
            semantic_analyzer_add_error_at(analyzer, var_decl, "Undefined type: %s", type.type_name);
            return false;
        }
        if (!symbol->class_type_name) {
            symbol->class_type_name = KRT_STRDUP(type.type_name);
            if (!symbol->class_type_name) {
                semantic_analyzer_add_error(analyzer, "Out of memory while naming class type");
                return false;
            }
        }
        var_decl->data.variable_decl.type = TOKEN_IDENTIFIER;
    }
    var_decl->resolved_type = symbol->source_type = type;
    /* The AST's declared type is left untouched; IrGen reads resolved_type
     * first and falls back to the declared token. */
    symbol->value_type = KrtSourceStorage(type);
    return true;
}

bool semantic_analyzer_analyze_static_variable_decl(SemanticAnalyzer* analyzer, ASTNode* var_decl) {
    if (!analyzer || !var_decl) {
        return false;
    }

    const char* var_name = var_decl->data.static_variable_decl.name;
    SymbolEntry* symbol = symbol_table_define(analyzer->symbol_table, var_name, SYMBOL_STATIC_FIELD, 0, NULL);

    if (!symbol) {
        semantic_analyzer_add_error(analyzer, "Failed to declare static variable %s", var_name);
        return false;
    }
    symbol->value_type = var_decl->data.static_variable_decl.type;
    symbol->is_const = var_decl->data.static_variable_decl.is_const;
    symbol->access = var_decl->data.variable_decl.access;
    symbol->source_type =
        var_decl->declared_type.token ? var_decl->declared_type : (KrtSourceType){.token = symbol->value_type};

    if (var_decl->data.static_variable_decl.value) {
        if (!semantic_analyzer_analyze_expression(analyzer, var_decl->data.static_variable_decl.value)) {
            return false;
        }
        if (!pointer_assignable(analyzer, var_decl, symbol->source_type,
                                var_decl->data.static_variable_decl.value->resolved_type)) {
            return false;
        }
        var_decl->resolved_type = symbol->source_type;
        return true;
    }

    return true;
}

static bool semantic_analyze_stmt_assignment(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* var_name = stmt->data.assignment.name;
    if (!var_name) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Assignment target is missing its name");
        return false;
    }
    SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, var_name);
    if (!symbol) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Undefined variable: %s", var_name);
        return false;
    }
    if (!semantic_check_assignable(analyzer, stmt, var_name, symbol)) {
        return false;
    }
    symbol->is_initialized = 1;
    stmt->is_ref_binding = symbol->source_type.is_ref;
    stmt->resolved_type = symbol->source_type;
    stmt->resolved_type.is_ref = false;
    return semantic_analyzer_analyze_expression(analyzer, stmt->data.assignment.value) &&
           pointer_assignable(analyzer, stmt, symbol->source_type, stmt->data.assignment.value->resolved_type);
}
static bool semantic_analyze_stmt_compound_assignment(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* var_name = stmt->data.compound_assignment.name;
    if (!var_name) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Assignment target is missing its name");
        return false;
    }
    SymbolEntry* symbol = symbol_table_lookup_scope_chain(analyzer->symbol_table, var_name);
    if (!symbol) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Undefined variable: %s", var_name);
        return false;
    }
    if (!semantic_check_assignable(analyzer, stmt, var_name, symbol)) {
        return false;
    }
    if (!semantic_analyzer_analyze_expression(analyzer, stmt->data.compound_assignment.value)) {
        return false;
    }
    symbol->is_initialized = 1;
    stmt->is_ref_binding = symbol->source_type.is_ref;
    stmt->resolved_type = symbol->source_type;
    stmt->resolved_type.is_ref = false;
    if (symbol->is_array) {
        return pointer_error(analyzer, stmt, "Compound assignment requires a scalar or array element");
    }
    return pointer_update_assignable(analyzer, stmt, stmt->resolved_type,
                                     stmt->data.compound_assignment.value->resolved_type,
                                     stmt->data.compound_assignment.operator);
}
static bool semantic_analyze_stmt_array_assignment(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool array_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.array_assignment.array);
    bool index_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.array_assignment.index);
    bool value_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.array_assignment.value);

    if (index_ok && !semantic_check_array_index(analyzer, stmt, stmt->data.array_assignment.index)) {
        return false;
    }

    if (array_ok && stmt->data.array_assignment.array->type == AST_IDENTIFIER) {
        const char* array_name = stmt->data.array_assignment.array->data.identifier_name;
        SymbolEntry* symbol = symbol_table_lookup(analyzer->symbol_table, array_name);
        if (symbol && !symbol->is_array && !KrtSourceIsPointer(symbol->source_type)) {
            semantic_analyzer_add_error(analyzer, "Attempting to assign to non-array as array");
            return false;
        }
        long size = symbol ? symbol->const_array_size : -1;
        long index = semantic_constant_int(analyzer, stmt->data.array_assignment.index);
        if (size >= 0 && index >= size) {
            semantic_analyzer_add_error_at(analyzer, stmt->data.array_assignment.index,
                                           "Array index %ld is out of bounds (size %ld)", index, size);
            return false;
        }
    }

    if (array_ok && index_ok && value_ok && KrtSourceIsPointer(stmt->data.array_assignment.array->resolved_type)) {
        return analyze_pointer_index(analyzer, stmt, stmt->data.array_assignment.array,
                                     stmt->data.array_assignment.index, stmt->data.array_assignment.value);
    }
    return array_ok && index_ok && value_ok;
}
static bool semantic_analyze_stmt_array_compound_assignment(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool array_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.array_compound_assignment.array);
    bool index_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.array_compound_assignment.index);
    bool value_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.array_compound_assignment.value);

    if (array_ok && stmt->data.array_compound_assignment.array->type == AST_IDENTIFIER) {
        const char* array_name = stmt->data.array_compound_assignment.array->data.identifier_name;
        SymbolEntry* symbol = symbol_table_lookup(analyzer->symbol_table, array_name);
        if (symbol && !symbol->is_array && !KrtSourceIsPointer(symbol->source_type)) {
            semantic_analyzer_add_error(analyzer, "Attempting to compound assign to non-array as array");
            return false;
        }
    }

    if (!array_ok || !index_ok || !value_ok) {
        return false;
    }
    stmt->resolved_type = stmt->data.array_compound_assignment.array->resolved_type;
    if (KrtSourceIsPointer(stmt->data.array_compound_assignment.array->resolved_type)) {
        return analyze_pointer_index(analyzer, stmt, stmt->data.array_compound_assignment.array,
                                     stmt->data.array_compound_assignment.index, NULL) &&
               pointer_update_assignable(analyzer, stmt, stmt->resolved_type,
                                         stmt->data.array_compound_assignment.value->resolved_type,
                                         stmt->data.array_compound_assignment.operator);
    }
    return pointer_update_assignable(analyzer, stmt, stmt->resolved_type,
                                     stmt->data.array_compound_assignment.value->resolved_type,
                                     stmt->data.array_compound_assignment.operator);
}
static bool semantic_analyze_stmt_return_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool returns_void = analyzer->current_return_type.token == TOKEN_VOID &&
                        analyzer->current_return_type.pointer_depth == 0;
    if (stmt->data.return_stmt.value) {
        ASTNode* value = stmt->data.return_stmt.value;
        if (returns_void) {
            semantic_analyzer_add_error_at(analyzer, stmt, "Void function cannot return a value");
            return false;
        }
        return semantic_analyzer_analyze_expression(analyzer, value) &&
               pointer_assignable(analyzer, stmt, analyzer->current_return_type, value->resolved_type);
    }
    return returns_void || pointer_error(analyzer, stmt, "Non-void function must return a value");
}
static bool semantic_statement_guarantees_return(ASTNode* stmt) {
    if (!stmt) {
        return false;
    }
    if (stmt->type == AST_RETURN_STATEMENT) {
        return true;
    }
    if (stmt->type == AST_UNSAFE_CALL) {
        return semantic_statement_guarantees_return(stmt->data.unsafe_call.expression);
    }
    if (stmt->type == AST_POINT_BLOCK) {
        return semantic_statement_guarantees_return(stmt->data.point_block.body);
    }
    if (stmt->type == AST_BLOCK) {
        for (int i = 0; i < stmt->data.block.statement_count; ++i) {
            if (semantic_statement_guarantees_return(stmt->data.block.statements[i])) {
                return true;
            }
        }
    }
    if (stmt->type == AST_IF_STATEMENT && stmt->data.if_stmt.else_branch) {
        return semantic_statement_guarantees_return(stmt->data.if_stmt.then_branch) &&
               semantic_statement_guarantees_return(stmt->data.if_stmt.else_branch);
    }
    if (stmt->type == AST_SWITCH_STATEMENT) {
        /* Every case, plus a default, must return. */
        if (!stmt->data.switch_stmt.default_case) {
            return false;
        }
        for (int i = 0; i < stmt->data.switch_stmt.case_count; i++) {
            ASTNode* clause = stmt->data.switch_stmt.cases[i];
            if (!clause) {
                return false;
            }
            bool clause_returns = false;
            for (int k = 0; k < clause->data.case_clause.statement_count; k++) {
                if (semantic_statement_guarantees_return(clause->data.case_clause.statements[k])) {
                    clause_returns = true;
                    break;
                }
            }
            if (!clause_returns) {
                return false;
            }
        }
        ASTNode* default_block = stmt->data.switch_stmt.default_case;
        for (int k = 0; k < default_block->data.block.statement_count; k++) {
            if (semantic_statement_guarantees_return(default_block->data.block.statements[k])) {
                return true;
            }
        }
        return false;
    }
    if (stmt->type == AST_TRY_STATEMENT) {
        return semantic_statement_guarantees_return(stmt->data.try_stmt.try_block);
    }
    return false;
}
static bool semantic_analyze_stmt_if_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool cond_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.if_stmt.condition);
    if (cond_ok && !semantic_require_bool_condition(analyzer, stmt, stmt->data.if_stmt.condition)) {
        return false;
    }
    symbol_table_push_scope(analyzer->symbol_table);
    ASTNode* pattern = stmt->data.if_stmt.condition;
    if (cond_ok && pattern->type == AST_IS_EXPRESSION && pattern->data.is_expr.binding_name) {
        SymbolEntry* hit =
            symbol_table_define(analyzer->symbol_table, pattern->data.is_expr.binding_name, SYMBOL_VARIABLE, 0, NULL);
        if (hit) {
            hit->source_type = pattern->declared_type;
            hit->value_type = KrtSourceStorage(hit->source_type);
        }
    }
    bool then_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.if_stmt.then_branch);
    symbol_table_pop_scope(analyzer->symbol_table);
    bool else_ok = true;
    if (stmt->data.if_stmt.else_branch) {
        symbol_table_push_scope(analyzer->symbol_table);
        else_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.if_stmt.else_branch);
        symbol_table_pop_scope(analyzer->symbol_table);
    }
    return cond_ok && then_ok && else_ok;
}
static bool semantic_analyze_stmt_while_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool cond_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.while_stmt.condition);
    if (cond_ok && !semantic_require_bool_condition(analyzer, stmt, stmt->data.while_stmt.condition)) {
        return false;
    }
    symbol_table_push_scope(analyzer->symbol_table);
    analyzer->breakable_depth++;
    analyzer->loop_depth++;
    bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.while_stmt.body);
    analyzer->loop_depth--;
    analyzer->breakable_depth--;
    symbol_table_pop_scope(analyzer->symbol_table);
    return cond_ok && body_ok;
}
static bool semantic_analyze_stmt_for_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    symbol_table_push_scope(analyzer->symbol_table);
    bool init_ok = true;
    if (stmt->data.for_stmt.init) {
        init_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.for_stmt.init);
    }
    bool cond_ok = true;
    if (stmt->data.for_stmt.condition) {
        cond_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.for_stmt.condition);
        if (cond_ok && !semantic_require_bool_condition(analyzer, stmt, stmt->data.for_stmt.condition)) {
            cond_ok = false;
        }
    }
    bool increment_ok = true;
    if (stmt->data.for_stmt.increment) {
        if (stmt->data.for_stmt.increment->type == AST_ASSIGNMENT ||
            stmt->data.for_stmt.increment->type == AST_COMPOUND_ASSIGNMENT ||
            stmt->data.for_stmt.increment->type == AST_VARIABLE_DECLARATION ||
            stmt->data.for_stmt.increment->type == AST_STATIC_VARIABLE_DECLARATION ||
            stmt->data.for_stmt.increment->type == AST_BLOCK) {
            increment_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.for_stmt.increment);
        } else {
            increment_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.for_stmt.increment);
        }
    }
    analyzer->breakable_depth++;
    analyzer->loop_depth++;
    bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.for_stmt.body);
    analyzer->loop_depth--;
    analyzer->breakable_depth--;
    symbol_table_pop_scope(analyzer->symbol_table);
    return init_ok && cond_ok && increment_ok && body_ok;
}
static bool semantic_analyze_stmt_foreach_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    if (!stmt->data.foreach_stmt.var_name) {
        semantic_analyzer_add_error(analyzer, "Foreach loop variable is missing its name");
        return false;
    }
    symbol_table_push_scope(analyzer->symbol_table);

    SymbolEntry* loop_var =
        symbol_table_define(analyzer->symbol_table, stmt->data.foreach_stmt.var_name, SYMBOL_VARIABLE, 0, NULL);
    if (!loop_var) {
        semantic_analyzer_add_error(analyzer, "Failed to define loop variable: %s", stmt->data.foreach_stmt.var_name);
        symbol_table_pop_scope(analyzer->symbol_table);
        return false;
    }
    bool iterable_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.foreach_stmt.iterable);
    if (iterable_ok && stmt->data.foreach_stmt.iterable) {
        /* The loop variable takes the iterable's element type; without this
         * every use inside the body fell back to a zero/INT32 type. */
        KrtTokenType element = semantic_array_element_type(analyzer, stmt->data.foreach_stmt.iterable);
        if (element == TOKEN_EOF) {
            element = stmt->data.foreach_stmt.iterable->resolved_type.token;
        }
        if (element != TOKEN_EOF) {
            loop_var->value_type = element;
            loop_var->source_type.token = element;
        }
    }
    analyzer->breakable_depth++;
    analyzer->loop_depth++;
    bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.foreach_stmt.body);
    analyzer->loop_depth--;
    analyzer->breakable_depth--;
    symbol_table_pop_scope(analyzer->symbol_table);
    return iterable_ok && body_ok;
}
static bool semantic_analyze_stmt_using_directive(SemanticAnalyzer* analyzer, ASTNode* stmt) {

    if (stmt->data.using_directive.path_length == 0) {
        semantic_analyzer_add_error(analyzer, "Using directive must have at least one namespace part");
        return false;
    }

    if (stmt->data.using_directive.is_alias) {
        if (!stmt->data.using_directive.alias) {
            semantic_analyzer_add_error(analyzer, "Using alias must have an alias name");
            return false;
        }
    }

    semantic_analyzer_add_using_directive(analyzer, stmt);

    return true;
}
static bool semantic_analyze_stmt_namespace_declaration(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* namespace_name = stmt->data.namespace_decl.name;

    SymbolEntry* ns_entry = symbol_table_lookup_current_scope(analyzer->symbol_table, namespace_name);
    SymbolTable* prev_table = analyzer->symbol_table;
    if (!ns_entry) {
        ns_entry = symbol_table_define(analyzer->symbol_table, namespace_name, SYMBOL_NAMESPACE, 0, NULL);
        if (ns_entry && !ns_entry->nested_table) {
            ns_entry->nested_table = symbol_table_create();
            if (ns_entry->nested_table) {
                ns_entry->nested_table->parent_table = prev_table;
            } else {
                semantic_analyzer_add_error(analyzer, "Failed to create namespace scope: %s", namespace_name);
                return false;
            }
        }
    } else if (ns_entry->type != SYMBOL_NAMESPACE) {
        semantic_analyzer_add_error(analyzer, "'%s' is already declared as a different symbol type", namespace_name);
        return false;
    }

    SymbolScope* prev_scope = analyzer->symbol_table->current_scope;
    if (ns_entry && ns_entry->nested_table) {

        analyzer->symbol_table = ns_entry->nested_table;
        analyzer->symbol_table->current_scope = ns_entry->nested_table->global_scope;
    }

    bool body_ok = true;
    ASTNode* body_node = stmt->data.namespace_decl.body;

    if (body_node) {
        if (body_node->type == AST_BLOCK) {
            for (int k = 0; k < body_node->data.block.statement_count; k++) {
                ASTNode* decl = body_node->data.block.statements[k];
                if (decl && decl->type == AST_ACCESS_MODIFIER) {
                    decl = decl->data.access_modifier.member;
                }
                if (decl && decl->type == AST_CLASS_DECLARATION) {
                    const char* class_name = decl->data.class_decl.name;
                    SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, class_name);
                    if (!existing) {
                        SymbolEntry* class_sym =
                            symbol_table_declare(analyzer->symbol_table, class_name, SYMBOL_CLASS, 0);
                        if (class_sym && !class_sym->nested_table) {
                            class_sym->nested_table = symbol_table_create();
                        }
                    }
                }
            }
            /* Reopening an existing namespace must still analyse its body;
             * only the predeclaration pass above is idempotent. */
            for (int k = 0; k < body_node->data.block.statement_count; k++) {
                ASTNode* decl = body_node->data.block.statements[k];
                if (!decl) {
                    continue;
                }
                if (!semantic_analyzer_analyze_statement(analyzer, decl)) {
                    body_ok = false;
                }
            }
        } else if (body_node->type == AST_ACCESS_MODIFIER) {
            ASTNode* inner_decl = body_node->data.access_modifier.member;
            if (inner_decl && inner_decl->type == AST_CLASS_DECLARATION) {
                const char* class_name = inner_decl->data.class_decl.name;
                SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, class_name);
                if (!existing) {
                    SymbolEntry* class_sym =
                        symbol_table_declare(analyzer->symbol_table, class_name, SYMBOL_CLASS, 0);
                    if (class_sym && !class_sym->nested_table) {
                        class_sym->nested_table = symbol_table_create();
                    }
                }
            }
            body_ok = semantic_analyzer_analyze_statement(analyzer, body_node);
        } else {
            body_ok = semantic_analyzer_analyze_statement(analyzer, body_node);
        }
    }

    analyzer->symbol_table = prev_table;
    analyzer->symbol_table->current_scope = prev_scope;

    return body_ok;
}
static bool semantic_analyze_stmt_class_declaration(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* class_name = stmt->data.class_decl.name;

    SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, class_name);
    /* The predeclare pass legitimately leaves a SYMBOL_FORWARD_REF behind;
     * anything more than that is a real duplicate. */
    if (existing && existing->state != SYMBOL_FORWARD_REF) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Class %s already declared", class_name);
        return false;
    }

    if (stmt->data.class_decl.template_params && stmt->data.class_decl.template_param_count > 0) {

        GenericParameter* params_list = NULL;
        for (int i = 0; i < stmt->data.class_decl.template_param_count; i++) {
            const char* tparam = stmt->data.class_decl.template_params[i];
            if (!tparam || !generics_add_parameter(&params_list, tparam)) {
                semantic_analyzer_add_error_at(analyzer, stmt, "Failed to add template parameter");
                generics_free_parameters(params_list);
                return false;
            }
        }

        if (!params_list) {
            semantic_analyzer_add_error_at(analyzer, stmt, "Failed to add any template parameter for %s", class_name);
            return false;
        }

        if (params_list) {

            bool registered = generics_register_type(analyzer->generic_registry, class_name, params_list,
                                                     stmt->data.class_decl.template_param_count, stmt);

            if (!registered) {
                generics_free_parameters(params_list);
                semantic_analyzer_add_error_at(analyzer, stmt, "Failed to register template class %s", class_name);
                return false;
            }
        }
    }

    SymbolEntry* class_sym = symbol_table_define(analyzer->symbol_table, class_name, SYMBOL_CLASS, 0, NULL);
    if (!class_sym) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Failed to define class %s", class_name);
        return false;
    }
    SymbolTable* prev_table = analyzer->symbol_table;
    if (!class_sym->nested_table) {
        class_sym->nested_table = symbol_table_create();
    }
    if (!class_sym->nested_table) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Failed to create class scope");
        return false;
    }
    class_sym->nested_table->parent_table = prev_table;

    analyzer->symbol_table = class_sym->nested_table;

    if (!analyzer->symbol_table->current_scope) {
        symbol_table_push_scope(analyzer->symbol_table);
    }

    SymbolEntry* self_class_symbol = symbol_table_define(analyzer->symbol_table, class_name, SYMBOL_CLASS, 0, NULL);
    if (self_class_symbol) {
        self_class_symbol->nested_table = class_sym->nested_table;
    }

    if (!semantic_analyzer_push_class_context(analyzer, class_name)) {
        semantic_analyzer_add_error_at(analyzer, stmt, "类上下文入栈失败: %s", class_name);
        analyzer->symbol_table = prev_table;
        return false;
    }

    bool body_ok = true;
    ASTNode* body_node = stmt->data.class_decl.body;

    if (body_node && body_node->type == AST_BLOCK) {
        for (int i = 0; i < body_node->data.block.statement_count; i++) {
            ASTNode* member = body_node->data.block.statements[i];
            if (!member) {
                continue;
            }
            KrtTokenType member_access = TOKEN_PUBLIC;
            if (member->type == AST_ACCESS_MODIFIER) {
                member_access = member->data.access_modifier.access_modifier;
                member = member->data.access_modifier.member;
                if (!member) {
                    continue;
                }
            } else if (member->type == AST_STATIC_FUNCTION_DECLARATION) {
                member_access = member->data.static_function_decl.access;
            } else if (member->type == AST_FUNCTION_DECLARATION) {
                member_access = member->data.function_decl.access;
            }
            const char* mname = NULL;
            KrtTokenType* ptypes = NULL;
            int pcount = 0;
            if (member->type == AST_STATIC_FUNCTION_DECLARATION) {
                mname = member->data.static_function_decl.name;
                ptypes = member->data.static_function_decl.parameter_types;
                pcount = member->data.static_function_decl.parameter_count;
            } else if (member->type == AST_FUNCTION_DECLARATION) {
                mname = member->data.function_decl.name;
                ptypes = member->data.function_decl.parameter_types;
                pcount = member->data.function_decl.parameter_count;
            }
            if (!mname) {
                continue;
            }

            const char* ns_path[2] = {class_name, NULL};
            char* key = name_mangle_function(ns_path, mname, ptypes, pcount);
            if (!key) {
                semantic_analyzer_add_error_at(analyzer, member, "Failed to mangle method %s", mname);
                body_ok = false;
                continue;
            }
            SymbolEntry* existing_m = symbol_table_lookup_current_scope(analyzer->symbol_table, key);
            if (!existing_m) {
                SymbolEntry* m_sym = symbol_table_define(analyzer->symbol_table, key, SYMBOL_FUNCTION, 0, NULL);
                if (m_sym) {
                    m_sym->state = SYMBOL_DECLARED;
                    m_sym->access = member_access;
                    m_sym->value_type = member->data.function_decl.return_type;
                    m_sym->source_type = KrtSourceTypeClone(
                        analyzer->type_arena, (KrtSourceType){.token = TOKEN_FN, .function = member->function_type});
                }
            }
            KRT_FREE(key);
        }
    }

    if (body_node && body_node->type == AST_BLOCK) {
        for (int i = 0; i < body_node->data.block.statement_count; i++) {
            ASTNode* member = body_node->data.block.statements[i];

            if (!member) {
                continue;
            }

            if (member->type == AST_ACCESS_MODIFIER) {
                member = member->data.access_modifier.member;
                if (!member) {
                    continue;
                }
            }

            bool member_ok = semantic_analyzer_analyze_statement(analyzer, member);
            body_ok = member_ok && body_ok;
        }
    } else if (body_node) {
        body_ok = semantic_analyzer_analyze_statement(analyzer, body_node);
    }

    semantic_analyzer_pop_class_context(analyzer);

    analyzer->symbol_table = prev_table;

    return body_ok;
}
static bool semantic_expression_is_constant(SemanticAnalyzer* analyzer, ASTNode* expr) {
    if (!expr) {
        return false;
    }
    if (expr->type == AST_NUMBER) {
        return true;
    }
    if (expr->type == AST_UNARY_OPERATION &&
        (expr->data.unary_op.operator == TOKEN_MINUS || expr->data.unary_op.operator == TOKEN_PLUS)) {
        return semantic_expression_is_constant(analyzer, expr->data.unary_op.operand);
    }
    if (expr->type == AST_IDENTIFIER) {
        SymbolEntry* sym = symbol_table_lookup_scope_chain(analyzer->symbol_table, expr->data.identifier_name);
        return sym && sym->is_const;
    }
    return false;
}

static bool semantic_analyze_stmt_switch_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool cond_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.switch_stmt.expression);
    bool cases_ok = true;
    analyzer->breakable_depth++;

    for (int i = 0; i < stmt->data.switch_stmt.case_count && cases_ok; i++) {
        ASTNode* clause = stmt->data.switch_stmt.cases[i];
        if (!clause) {
            cases_ok = false;
            continue;
        }
        if (!semantic_expression_is_constant(analyzer, clause->data.case_clause.value)) {
            semantic_analyzer_add_error_at(analyzer, clause->data.case_clause.value,
                                           "Switch case value must be a constant expression");
            cases_ok = false;
            continue;
        }
        long value = semantic_constant_int(analyzer, clause->data.case_clause.value);
        for (int j = 0; j < i; j++) {
            ASTNode* other = stmt->data.switch_stmt.cases[j];
            if (other && semantic_constant_int(analyzer, other->data.case_clause.value) == value) {
                semantic_analyzer_add_error_at(analyzer, clause->data.case_clause.value,
                                               "Duplicate switch case value");
                cases_ok = false;
                break;
            }
        }
        if (cases_ok) {
            cases_ok = semantic_analyzer_analyze_statement(analyzer, clause);
        }
    }

    bool default_ok = true;
    if (stmt->data.switch_stmt.default_case) {
        default_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.switch_stmt.default_case);
    }
    analyzer->breakable_depth--;

    return cond_ok && cases_ok && default_ok;
}
static bool semantic_analyze_stmt_case_clause(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool value_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.case_clause.value);
    bool stmts_ok = true;

    for (int i = 0; i < stmt->data.case_clause.statement_count && stmts_ok; i++) {
        stmts_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.case_clause.statements[i]);
    }

    return value_ok && stmts_ok;
}
static bool semantic_analyze_stmt_default_clause(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool stmts_ok = true;

    for (int i = 0; i < stmt->data.default_clause.statement_count && stmts_ok; i++) {
        stmts_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.default_clause.statements[i]);
    }

    return stmts_ok;
}
static bool semantic_analyze_stmt_print_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    bool all_ok = true;
    for (int i = 0; i < stmt->data.print_stmt.value_count; i++) {
        if (!semantic_analyzer_analyze_expression(analyzer, stmt->data.print_stmt.values[i])) {
            all_ok = false;
        }
    }
    return all_ok;
}
static bool semantic_analyze_stmt_unsafe_call(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    int old_mode = analyzer->is_unsafe_mode, old_mem = analyzer->unsafe_mem;
    analyzer->is_unsafe_mode = 1;
    for (int i = 0; i < stmt->data.unsafe_call.permission_count; i++) {
        if (stmt->data.unsafe_call.permissions[i] &&
            strcmp(stmt->data.unsafe_call.permissions[i], "krt.mem") == 0) {
            analyzer->unsafe_mem = 1;
        }
    }
    bool result = stmt->data.unsafe_call.is_block
                      ? semantic_analyzer_analyze_statement(analyzer, stmt->data.unsafe_call.expression)
                      : semantic_analyzer_analyze_expression(analyzer, stmt->data.unsafe_call.expression);
    analyzer->is_unsafe_mode = old_mode;
    analyzer->unsafe_mem = old_mem;
    return result;
}
static bool semantic_analyze_stmt_access_modifier(SemanticAnalyzer* analyzer, ASTNode* stmt) {

    if (stmt->data.access_modifier.member) {
        return semantic_analyzer_analyze_statement(analyzer, stmt->data.access_modifier.member);
    }
    semantic_analyzer_add_error_at(analyzer, stmt, "Access modifier without a member");
    return false;
}
static bool semantic_analyze_stmt_function_declaration(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* func_name = stmt->data.function_decl.name;
    const char* current_class = semantic_analyzer_get_current_class_context(analyzer);
    char* mangled_name = NULL;
    const char* symbol_name = func_name;

    if (current_class) {
        const char* ns_path[2] = {current_class, NULL};
        mangled_name = name_mangle_function(ns_path, func_name, stmt->data.function_decl.parameter_types,
                                            stmt->data.function_decl.parameter_count);
        if (!mangled_name) {
            semantic_analyzer_add_error_at(analyzer, stmt, "Failed to mangle method %s", func_name);
            return false;
        }
        symbol_name = mangled_name;
    }

    SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, symbol_name);
    if (existing && existing->state == SYMBOL_DEFINED) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Function %s already defined", func_name);
        KRT_FREE(mangled_name);
        return false;
    }

    SymbolEntry* func_sym = symbol_table_define(analyzer->symbol_table, symbol_name, SYMBOL_FUNCTION, 0, NULL);
    if (func_sym) {
        func_sym->value_type = stmt->data.function_decl.return_type;
        func_sym->source_type = KrtSourceTypeClone(analyzer->type_arena,
                                                   (KrtSourceType){.token = TOKEN_FN, .function = stmt->function_type});
    }
    if (!func_sym) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Failed to define function %s", func_name);
        KRT_FREE(mangled_name);
        return false;
    }
    func_sym->state = SYMBOL_DEFINED;

    if (!current_class && analyzer->symbol_table == analyzer->global_symbol_table &&
        strcmp(func_name, "main") == 0) {
        if (analyzer->entry_point_name) {
            semantic_analyzer_add_error_at(analyzer, stmt, "Multiple entry points named 'main'");
            KRT_FREE(mangled_name);
            return false;
        }
        analyzer->has_entry_point = true;
        analyzer->entry_point_name = KRT_STRDUP(func_name);
        func_sym->is_entry_point = true;
    }

    if (stmt->data.function_decl.body) {
        symbol_table_push_scope(analyzer->symbol_table);
        for (int i = 0; i < stmt->data.function_decl.parameter_count; i++) {
            SymbolEntry* param = symbol_table_define(analyzer->symbol_table, stmt->data.function_decl.parameters[i],
                                                     SYMBOL_VARIABLE, 0, NULL);
            if (param) {
                param->value_type = stmt->data.function_decl.parameter_types[i];
                if (stmt->function_type && i < stmt->function_type->parameter_count) {
                    param->source_type = stmt->function_type->parameters[i];
                }
                if (param->source_type.token == TOKEN_IDENTIFIER && param->source_type.type_name) {
                    param->class_type_name = KRT_STRDUP(param->source_type.type_name);
                }
                param->is_array =
                    stmt->data.function_decl.parameter_is_array && stmt->data.function_decl.parameter_is_array[i];
            }
        }
        KrtSourceType saved_return = analyzer->current_return_type;
        analyzer->current_return_type = stmt->function_type
                                            ? stmt->function_type->result
                                            : (KrtSourceType){.token = stmt->data.function_decl.return_type};
        bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.function_decl.body);
        if (body_ok && !(analyzer->current_return_type.token == TOKEN_VOID &&
                         analyzer->current_return_type.pointer_depth == 0) &&
            !semantic_statement_guarantees_return(stmt->data.function_decl.body)) {
            semantic_analyzer_add_error_at(analyzer, stmt, "Not all control paths return a value");
            body_ok = false;
        }
        analyzer->current_return_type = saved_return;
        symbol_table_pop_scope(analyzer->symbol_table);
        KRT_FREE(mangled_name);
        return body_ok;
    }
    KRT_FREE(mangled_name);
    return true;
}
static bool semantic_analyze_stmt_static_function_declaration(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* method_name = stmt->data.static_function_decl.name;

    const char* current_class = semantic_analyzer_get_current_class_context(analyzer);

    char* mangled_name = NULL;
    const char* function_name = method_name;
    if (current_class) {
        const char* ns_path[2] = {current_class, NULL};
        mangled_name = name_mangle_function(ns_path, method_name, stmt->data.static_function_decl.parameter_types,
                                            stmt->data.static_function_decl.parameter_count);
        if (!mangled_name) {
            semantic_analyzer_add_error_at(analyzer, stmt, "Failed to mangle static method %s", method_name);
            return false;
        }
        function_name = mangled_name;
    }

    SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, function_name);
    if (existing && existing->state == SYMBOL_DEFINED) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Static method %s already defined", method_name);
        if (mangled_name) {
            KRT_FREE(mangled_name);
        }
        return false;
    }

    SymbolEntry* func_sym =
        symbol_table_define(analyzer->symbol_table, function_name, SYMBOL_FUNCTION, 0, NULL);
    if (!func_sym) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Failed to define static method %s", method_name);
        if (mangled_name) {
            KRT_FREE(mangled_name);
        }
        return false;
    }
    func_sym->state = SYMBOL_DEFINED;
    func_sym->value_type = stmt->data.static_function_decl.return_type;
    func_sym->source_type =
        KrtSourceTypeClone(analyzer->type_arena, (KrtSourceType){.token = TOKEN_FN, .function = stmt->function_type});

    /* A static method without a body is a legitimate forward declaration. */
    if (!stmt->data.static_function_decl.body) {
        if (mangled_name) {
            KRT_FREE(mangled_name);
        }
        return true;
    }

    symbol_table_push_scope(analyzer->symbol_table);

    for (int i = 0; i < stmt->data.static_function_decl.parameter_count; i++) {
        const char* param_name = stmt->data.static_function_decl.parameters[i];

        SymbolEntry* param = symbol_table_define(analyzer->symbol_table, param_name, SYMBOL_VARIABLE, 0, NULL);
        if (param) {
            param->value_type = stmt->data.static_function_decl.parameter_types[i];
            if (stmt->function_type && i < stmt->function_type->parameter_count) {
                param->source_type = stmt->function_type->parameters[i];
            }
            if (param->source_type.token == TOKEN_IDENTIFIER && param->source_type.type_name) {
                param->class_type_name = KRT_STRDUP(param->source_type.type_name);
            }
            param->is_array = stmt->data.static_function_decl.parameter_is_array &&
                              stmt->data.static_function_decl.parameter_is_array[i];
        }
    }

    KrtSourceType saved_return = analyzer->current_return_type;
    analyzer->current_return_type = stmt->function_type
                                        ? stmt->function_type->result
                                        : (KrtSourceType){.token = stmt->data.static_function_decl.return_type};
    bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.static_function_decl.body);
    if (body_ok && !(analyzer->current_return_type.token == TOKEN_VOID &&
                     analyzer->current_return_type.pointer_depth == 0) &&
        !semantic_statement_guarantees_return(stmt->data.static_function_decl.body)) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Not all control paths return a value");
        body_ok = false;
    }
    analyzer->current_return_type = saved_return;

    symbol_table_pop_scope(analyzer->symbol_table);

    if (mangled_name) {
        KRT_FREE(mangled_name);
    }
    return body_ok;
}
static bool semantic_analyze_stmt_constructor_declaration(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    const char* current_class = semantic_analyzer_get_current_class_context(analyzer);
    if (!current_class) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Constructor declaration outside of a class");
        return false;
    }

    int parameter_count = stmt->data.constructor_decl.parameter_count;
    char* mangled_name = name_mangle_constructor(current_class, parameter_count);
    if (!mangled_name) {
        semantic_analyzer_add_error_at(analyzer, stmt, "Failed to mangle constructor %s", current_class);
        return false;
    }

    SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, mangled_name);
    if (existing && existing->state == SYMBOL_DEFINED) {
        semantic_analyzer_add_error(analyzer, "Constructor %s already defined", current_class);
        KRT_FREE(mangled_name);
        return false;
    }

    SymbolEntry* ctor_sym = symbol_table_define(analyzer->symbol_table, mangled_name, SYMBOL_FUNCTION, 0, NULL);
    if (!ctor_sym) {
        semantic_analyzer_add_error(analyzer, "Failed to define constructor %s", current_class);
        KRT_FREE(mangled_name);
        return false;
    }
    ctor_sym->state = SYMBOL_DEFINED;
    ctor_sym->value_type = TOKEN_VOID;
    ctor_sym->source_type =
        KrtSourceTypeClone(analyzer->type_arena, (KrtSourceType){.token = TOKEN_FN, .function = stmt->function_type});
    KRT_FREE(mangled_name);

    if (stmt->data.constructor_decl.body) {
        symbol_table_push_scope(analyzer->symbol_table);
        for (int i = 0; i < stmt->data.constructor_decl.parameter_count; i++) {
            SymbolEntry* param = symbol_table_define(analyzer->symbol_table, stmt->data.constructor_decl.parameters[i],
                                                     SYMBOL_VARIABLE, 0, NULL);
            if (param) {
                param->value_type = stmt->data.constructor_decl.parameter_types[i];
            }
        }

        bool base_ok = true;
        for (int i = 0; i < stmt->data.constructor_decl.base_argument_count; i++) {
            bool arg_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.constructor_decl.base_arguments[i]);
            base_ok = arg_ok && base_ok;
        }

        bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.constructor_decl.body);
        symbol_table_pop_scope(analyzer->symbol_table);
        return base_ok && body_ok;
    }
    return true;
}
/* Like guarantees_return, but also covers break/continue. */
static bool semantic_statement_terminates(ASTNode* stmt) {
    if (!stmt) {
        return false;
    }
    if (stmt->type == AST_BREAK_STATEMENT || stmt->type == AST_CONTINUE_STATEMENT) {
        return true;
    }
    return semantic_statement_guarantees_return(stmt);
}

static bool semantic_check_uninitialized_locals(SemanticAnalyzer* analyzer, SymbolScope* scope,
                                                ASTNode* node) {
    bool ok = true;
    for (SymbolEntry* local = scope ? scope->symbols : NULL; local; local = local->scope_next) {
        if (local->type == SYMBOL_VARIABLE && !local->is_initialized && local->was_read) {
            semantic_analyzer_add_error_at(analyzer, node,
                                           "Variable '%s' is read before it is assigned", local->name);
            ok = false;
        }
    }
    return ok;
}

static bool semantic_analyze_stmt_block(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    symbol_table_push_scope(analyzer->symbol_table);
    bool overall_result = true;
    for (int i = 0; i < stmt->data.block.statement_count; i++) {
        ASTNode* current_stmt = stmt->data.block.statements[i];
        bool stmt_result = semantic_analyzer_analyze_statement(analyzer, current_stmt);
        if (!stmt_result) {
            overall_result = false;
        }
        if (i + 1 < stmt->data.block.statement_count && semantic_statement_terminates(current_stmt)) {
            semantic_analyzer_add_error_at(analyzer, stmt->data.block.statements[i + 1],
                                           "Unreachable code after a statement that always exits");
            overall_result = false;
        }
    }
    /* Locals of this block disappear at the pop, so check them here. */
    if (!semantic_check_uninitialized_locals(analyzer, analyzer->symbol_table->current_scope, stmt)) {
        overall_result = false;
    }
    symbol_table_pop_scope(analyzer->symbol_table);
    return overall_result;
}
static bool semantic_analyze_stmt_point_block(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    analyzer->point_depth++;
    bool result = semantic_analyzer_analyze_statement(analyzer, stmt->data.point_block.body);
    analyzer->point_depth--;
    return result;
}

bool semantic_analyzer_analyze_statement(SemanticAnalyzer* analyzer, ASTNode* stmt) {
    if (!analyzer || !stmt) {
        return false;
    }

    switch (stmt->type) {
    case AST_VARIABLE_DECLARATION:
        return semantic_analyzer_analyze_variable_decl(analyzer, stmt);

    case AST_STATIC_VARIABLE_DECLARATION:
        return semantic_analyzer_analyze_static_variable_decl(analyzer, stmt);

    case AST_ASSIGNMENT:
        return semantic_analyze_stmt_assignment(analyzer, stmt);
    case AST_COMPOUND_ASSIGNMENT:
        return semantic_analyze_stmt_compound_assignment(analyzer, stmt);
    case AST_ARRAY_ASSIGNMENT:
        return semantic_analyze_stmt_array_assignment(analyzer, stmt);
    case AST_ARRAY_COMPOUND_ASSIGNMENT:
        return semantic_analyze_stmt_array_compound_assignment(analyzer, stmt);
    case AST_RETURN_STATEMENT:
        return semantic_analyze_stmt_return_statement(analyzer, stmt);
    case AST_IF_STATEMENT:
        return semantic_analyze_stmt_if_statement(analyzer, stmt);
    case AST_DO_WHILE_STATEMENT: {
        analyzer->breakable_depth++;
        analyzer->loop_depth++;
        bool body_ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.do_while_stmt.body);
        analyzer->loop_depth--;
        analyzer->breakable_depth--;
        bool condition_ok = semantic_analyzer_analyze_expression(analyzer, stmt->data.do_while_stmt.condition);
        return body_ok && condition_ok;
    }
    case AST_WHILE_STATEMENT:
        return semantic_analyze_stmt_while_statement(analyzer, stmt);
    case AST_FOR_STATEMENT:
        return semantic_analyze_stmt_for_statement(analyzer, stmt);
    case AST_FOREACH_STATEMENT:
        return semantic_analyze_stmt_foreach_statement(analyzer, stmt);
    case AST_USING_DIRECTIVE:
        return semantic_analyze_stmt_using_directive(analyzer, stmt);
    case AST_NAMESPACE_DECLARATION:
        return semantic_analyze_stmt_namespace_declaration(analyzer, stmt);
    case AST_CLASS_DECLARATION:
        return semantic_analyze_stmt_class_declaration(analyzer, stmt);
    case AST_TEMPLATE_DECLARATION:

        return semantic_analyzer_analyze_generic_decl(analyzer, stmt);

    case AST_SWITCH_STATEMENT:
        return semantic_analyze_stmt_switch_statement(analyzer, stmt);
    case AST_CASE_CLAUSE:
        return semantic_analyze_stmt_case_clause(analyzer, stmt);
    case AST_DEFAULT_CLAUSE:
        return semantic_analyze_stmt_default_clause(analyzer, stmt);
    case AST_BREAK_STATEMENT:
        if (!analyzer->breakable_depth) {
            semantic_analyzer_add_error_at(analyzer, stmt, "break is only valid inside a loop or switch");
            return false;
        }
        return true;
    case AST_CONTINUE_STATEMENT:
        if (!analyzer->loop_depth) {
            semantic_analyzer_add_error_at(analyzer, stmt, "continue is only valid inside a loop");
            return false;
        }
        return true;

    case AST_PRINT_STATEMENT:
        return semantic_analyze_stmt_print_statement(analyzer, stmt);
    case AST_UNSAFE_CALL:
        return semantic_analyze_stmt_unsafe_call(analyzer, stmt);
    case AST_ACCESS_MODIFIER:
        return semantic_analyze_stmt_access_modifier(analyzer, stmt);
    case AST_FUNCTION_DECLARATION:
        return semantic_analyze_stmt_function_declaration(analyzer, stmt);
    case AST_STATIC_FUNCTION_DECLARATION:
        return semantic_analyze_stmt_static_function_declaration(analyzer, stmt);
    case AST_CONSTRUCTOR_DECLARATION:
        return semantic_analyze_stmt_constructor_declaration(analyzer, stmt);
    case AST_BLOCK:
        return semantic_analyze_stmt_block(analyzer, stmt);
    case AST_POINT_BLOCK:
        return semantic_analyze_stmt_point_block(analyzer, stmt);
    case AST_POINTER_ASSIGNMENT:
    case AST_POINTER_DEREFERENCE:
    case AST_ADDRESS_OF:
    case AST_STACKALLOC_EXPRESSION:
    case AST_BINARY_OPERATION:
    case AST_UNARY_OPERATION:
    case AST_TERNARY_OPERATION:
    case AST_CALL:
    case AST_IDENTIFIER:
    case AST_NUMBER:
    case AST_STRING:
    case AST_BOOLEAN:

        return semantic_analyzer_analyze_expression(analyzer, stmt);

    case AST_TRY_STATEMENT: {
        bool ok = true;
        if (stmt->data.try_stmt.try_block) {
            ok = semantic_analyzer_analyze_statement(analyzer, stmt->data.try_stmt.try_block);
        }
        for (int i = 0; i < stmt->data.try_stmt.catch_clause_count; i++) {
            ASTNode* clause = stmt->data.try_stmt.catch_clauses ? stmt->data.try_stmt.catch_clauses[i] : NULL;
            if (!clause) {
                continue;
            }
            symbol_table_push_scope(analyzer->symbol_table);
            if (clause->data.catch_clause.exception_var) {
                SymbolEntry* catch_var = symbol_table_define(analyzer->symbol_table,
                                                             clause->data.catch_clause.exception_var, SYMBOL_VARIABLE, 0,
                                                             NULL);
                if (!catch_var) {
                    semantic_analyzer_add_error(analyzer, "Failed to define catch variable: %s",
                                                clause->data.catch_clause.exception_var);
                    symbol_table_pop_scope(analyzer->symbol_table);
                    ok = false;
                    continue;
                }
            }
            if (clause->data.catch_clause.catch_block) {
                ok = semantic_analyzer_analyze_statement(analyzer, clause->data.catch_clause.catch_block) && ok;
            }
            symbol_table_pop_scope(analyzer->symbol_table);
        }
        if (stmt->data.try_stmt.finally_clause &&
            stmt->data.try_stmt.finally_clause->data.finally_clause.finally_block) {
            ok = semantic_analyzer_analyze_statement(
                     analyzer, stmt->data.try_stmt.finally_clause->data.finally_clause.finally_block) &&
                 ok;
        }
        return ok;
    }
    case AST_THROW_STATEMENT:
        if (stmt->data.throw_stmt.exception_expr) {
            return semantic_analyzer_analyze_expression(analyzer, stmt->data.throw_stmt.exception_expr);
        }
        return true;

    default:
        semantic_analyzer_add_error_at(analyzer, stmt, "Unsupported statement type %d", (int)stmt->type);
        return false;
    }
}

FunctionAnalysisResult semantic_analyzer_analyze_function(SemanticAnalyzer* analyzer, ASTNode* function_node) {
    FunctionAnalysisResult result = {0};
    result.success = false;

    if (!analyzer || !function_node ||
        (function_node->type != AST_FUNCTION_DECLARATION && function_node->type != AST_STATIC_FUNCTION_DECLARATION)) {
        if (analyzer) {
            semantic_analyzer_add_error(analyzer, "Invalid function node for analysis");
        }
        return result;
    }

    const char* func_name = (function_node->type == AST_FUNCTION_DECLARATION)
                                ? function_node->data.function_decl.name
                                : function_node->data.static_function_decl.name;

    SymbolEntry* existing = symbol_table_lookup(analyzer->symbol_table, func_name);
    if (existing && existing->state == SYMBOL_DEFINED) {
        semantic_analyzer_add_error_at(analyzer, function_node, "Function %s already defined", func_name);
        return result;
    }

    SymbolEntry* func_symbol;
    /* M1: 复用任何未定义的既有函数条目 —— 覆盖两类来源:
     * 前向引用(本文件后置定义) 与 collect_exports 预登记(跨文件) */
    if (existing && existing->type == SYMBOL_FUNCTION && existing->state != SYMBOL_DEFINED) {
        func_symbol = existing;
    } else {
        func_symbol = symbol_table_define(analyzer->symbol_table, func_name, SYMBOL_FUNCTION, 0, NULL);
        if (!func_symbol) {
            semantic_analyzer_add_error_at(analyzer, function_node, "Failed to define function %s", func_name);
            return result;
        }
    }
    func_symbol->state = SYMBOL_DEFINED;
    func_symbol->access = (function_node->type == AST_FUNCTION_DECLARATION)
                              ? function_node->data.function_decl.access
                              : function_node->data.static_function_decl.access;
    func_symbol->source_type = KrtSourceTypeClone(
        analyzer->type_arena, (KrtSourceType){.token = TOKEN_FN, .function = function_node->function_type});
    func_symbol->value_type = function_node->data.function_decl.return_type;

    if (analyzer->symbol_table == analyzer->global_symbol_table &&
        !semantic_analyzer_get_current_class_context(analyzer) && strcmp(func_name, "main") == 0) {
        if (analyzer->entry_point_name) {
            semantic_analyzer_add_error_at(analyzer, function_node, "Multiple entry points named 'main'");
            return result;
        }
        analyzer->has_entry_point = true;
        analyzer->entry_point_name = KRT_STRDUP(func_name);
        func_symbol->is_entry_point = true;
    }

    symbol_table_push_scope(analyzer->symbol_table);

    int param_count = (function_node->type == AST_FUNCTION_DECLARATION)
                          ? function_node->data.function_decl.parameter_count
                          : function_node->data.static_function_decl.parameter_count;
    for (int i = 0; i < param_count; i++) {
        const char* param_name = (function_node->type == AST_FUNCTION_DECLARATION)
                                     ? function_node->data.function_decl.parameters[i]
                                     : function_node->data.static_function_decl.parameters[i];
        KrtTokenType param_type = (function_node->type == AST_FUNCTION_DECLARATION)
                                      ? function_node->data.function_decl.parameter_types[i]
                                      : function_node->data.static_function_decl.parameter_types[i];
        SymbolEntry* param_symbol = symbol_table_define(analyzer->symbol_table, param_name, SYMBOL_VARIABLE, 0, NULL);
        if (param_symbol) {
            param_symbol->value_type = param_type;
            if (function_node->function_type && i < function_node->function_type->parameter_count) {
                param_symbol->source_type = function_node->function_type->parameters[i];
            } else {
                param_symbol->source_type = (KrtSourceType){.token = param_type};
            }
            if (param_symbol->source_type.token == TOKEN_IDENTIFIER && param_symbol->source_type.type_name) {
                param_symbol->class_type_name = KRT_STRDUP(param_symbol->source_type.type_name);
            }
            int is_array = (function_node->type == AST_FUNCTION_DECLARATION)
                               ? (function_node->data.function_decl.parameter_is_array
                                      ? function_node->data.function_decl.parameter_is_array[i]
                                      : 0)
                               : (function_node->data.static_function_decl.parameter_is_array
                                      ? function_node->data.static_function_decl.parameter_is_array[i]
                                      : 0);
            if (is_array) {
                param_symbol->is_array = 1;
            }
        }
    }

    ASTNode* body = (function_node->type == AST_FUNCTION_DECLARATION) ? function_node->data.function_decl.body
                                                                      : function_node->data.static_function_decl.body;

    KrtSourceType saved_return = analyzer->current_return_type;
    KrtTokenType declared_return = (function_node->type == AST_FUNCTION_DECLARATION)
                                       ? function_node->data.function_decl.return_type
                                       : function_node->data.static_function_decl.return_type;
    analyzer->current_return_type = function_node->function_type
                                        ? function_node->function_type->result
                                        : (KrtSourceType){.token = declared_return};
    if (!body) {
        /* Pure declaration: nothing to analyse, no control-flow requirement. */
        analyzer->current_return_type = saved_return;
        symbol_table_pop_scope(analyzer->symbol_table);
        result.success = true;
        result.param_count = param_count;
        result.function_symbol = func_symbol;
        return result;
    }
    bool body_ok = semantic_analyzer_analyze_statement(analyzer, body);
    if (body_ok) {
        /*
         * Conservative "read before assignment": a local declared without a
         * value and never assigned anywhere in this function.  Branches are
         * deliberately ignored - `if (c) x = 1; use(x);` stays valid.
         */
        for (SymbolEntry* local = analyzer->symbol_table->current_scope->symbols; local;
             local = local->scope_next) {
            if (local->type == SYMBOL_VARIABLE && !local->is_initialized && local->was_read) {
                semantic_analyzer_add_error_at(analyzer, function_node,
                                               "Variable '%s' is read before it is assigned", local->name);
                body_ok = false;
            }
        }
    }
    if (body_ok && !(analyzer->current_return_type.token == TOKEN_VOID &&
                     analyzer->current_return_type.pointer_depth == 0) &&
        !semantic_statement_guarantees_return(body)) {
        semantic_analyzer_add_error_at(analyzer, function_node, "Not all control paths return a value");
        body_ok = false;
    }
    analyzer->current_return_type = saved_return;

    symbol_table_pop_scope(analyzer->symbol_table);

    result.success = body_ok;
    result.param_count = param_count;
    result.function_symbol = func_symbol;

    return result;
}

bool semantic_analyzer_analyze_generic_decl(SemanticAnalyzer* analyzer, ASTNode* generic_decl) {
    if (!analyzer || !generic_decl || generic_decl->type != AST_TEMPLATE_DECLARATION) {
        return false;
    }

    ASTNode* declaration = generic_decl->data.template_decl.declaration;
    if (!declaration) {
        semantic_analyzer_add_error(analyzer, "Template declaration missing declaration");
        return false;
    }

    const char* type_name = NULL;
    if (declaration->type == AST_CLASS_DECLARATION) {
        type_name = declaration->data.class_decl.name;
    } else if (declaration->type == AST_FUNCTION_DECLARATION) {
        type_name = declaration->data.function_decl.name;
    }

    if (!type_name) {
        semantic_analyzer_add_error(analyzer, "Template declaration missing name");
        return false;
    }

    GenericParameter* params = NULL;
    int param_count = 0;

    if (generic_decl->data.template_decl.parameters) {

        for (int i = 0; i < generic_decl->data.template_decl.parameter_count; i++) {
            ASTNode* param_node = generic_decl->data.template_decl.parameters[i];
            if (!param_node || param_node->type != AST_TEMPLATE_PARAMETER) {
                semantic_analyzer_add_error(analyzer, "Invalid template parameter #%d for %s", i, type_name);
                generics_free_parameters(params);
                return false;
            }
            const char* param_name = param_node->data.template_param.param_name;
            if (!param_name || !generics_add_parameter(&params, param_name)) {
                semantic_analyzer_add_error(analyzer, "Failed to add template parameter #%d", i);
                generics_free_parameters(params);
                return false;
            }
            param_count++;
        }
    }

    bool success = generics_register_type(analyzer->generic_registry, type_name, params, param_count, declaration);

    if (!success) {
        semantic_analyzer_add_error(analyzer, "Failed to register generic type: %s", type_name);
        generics_free_parameters(params);
        return false;
    }

    return true;
}

bool semantic_analyzer_analyze_generic_instantiation(SemanticAnalyzer* analyzer, const char* base_type,
                                                     const char** type_args, int arg_count) {
    if (!analyzer || !base_type || !type_args || arg_count <= 0) {
        return false;
    }

    if (!generics_is_generic_type(analyzer->generic_registry, base_type)) {
        semantic_analyzer_add_error(analyzer, "Unknown generic type: %s", base_type);
        return false;
    }

    GenericTypeInfo* type_info = generics_get_type_info(analyzer->generic_registry, base_type);

    if (!type_info) {
        semantic_analyzer_add_error(analyzer, "Unknown generic type: %s", base_type);
        return false;
    }
    if (!type_info->declaration) {
        semantic_analyzer_add_error(analyzer, "Generic type '%s' has no declaration", base_type);
        KRT_FREE(type_info);
        return false;
    }
    ASTNode* declaration = type_info->declaration;
    if (declaration->type != AST_CLASS_DECLARATION) {
        semantic_analyzer_add_error(analyzer, "Invalid template declaration for: %s", base_type);
        if (type_info) {
            KRT_FREE(type_info);
        }
        return false;
    }

    if (!semantic_analyzer_validate_constraints(analyzer, declaration, type_args, arg_count)) {
        semantic_analyzer_add_error(analyzer, "Constraint validation failed for generic type: %s", base_type);
        KRT_FREE(type_info);
        return false;
    }

    KRT_FREE(type_info);

    bool success =
        generics_instantiate_type(analyzer->generic_registry, base_type, type_args, arg_count, analyzer->symbol_table);

    if (!success) {
        semantic_analyzer_add_error(analyzer, "Failed to instantiate generic type: %s", base_type);
        return false;
    }

    return true;
}

bool semantic_analyzer_validate_constraints(SemanticAnalyzer* analyzer, ASTNode* class_decl, const char** type_args,
                                            int arg_count) {
    if (!analyzer || !class_decl || class_decl->type != AST_CLASS_DECLARATION) {
        return false;
    }

    if (class_decl->data.class_decl.constraint_count == 0) {
        return true;
    }

    char** template_params = class_decl->data.class_decl.template_params;
    int template_param_count = class_decl->data.class_decl.template_param_count;

    if (!template_params || template_param_count == 0) {
        semantic_analyzer_add_error(analyzer, "Class has constraints but no template parameters");
        return false;
    }

    if (arg_count != template_param_count) {
        semantic_analyzer_add_error(analyzer, "Wrong number of type arguments: expected %d, got %d",
                                    template_param_count, arg_count);
        return false;
    }

    for (int param_idx = 0; param_idx < template_param_count; param_idx++) {
        const char* param_name = template_params[param_idx];
        bool has_class_constraint = false;
        bool has_struct_constraint = false;

        for (int i = 0; i < class_decl->data.class_decl.constraint_count; i++) {
            ASTNode* constraint = class_decl->data.class_decl.constraints[i];
            if (!constraint || constraint->type != AST_GENERIC_CONSTRAINT) {
                continue;
            }

            const char* c_param = constraint->data.generic_constraint.param_name;
            const char* c_type = constraint->data.generic_constraint.constraint_type;
            if (!c_param || !c_type) {
                semantic_analyzer_add_error(analyzer, "Invalid constraint: missing parameter or type");
                return false;
            }
            if (strcmp(c_param, param_name) == 0) {
                const char* constraint_type = c_type;
                if (strcmp(constraint_type, "class") == 0) {
                    has_class_constraint = true;
                } else if (strcmp(constraint_type, "struct") == 0) {
                    has_struct_constraint = true;
                }
            }
        }

        if (has_class_constraint && has_struct_constraint) {
            semantic_analyzer_add_error(analyzer, "Parameter '%s' has conflicting class and struct constraints",
                                        param_name);
            return false;
        }
    }

    for (int i = 0; i < class_decl->data.class_decl.constraint_count; i++) {
        ASTNode* constraint = class_decl->data.class_decl.constraints[i];
        if (!constraint || constraint->type != AST_GENERIC_CONSTRAINT) {
            continue;
        }

        const char* param_name = constraint->data.generic_constraint.param_name;
        const char* constraint_type = constraint->data.generic_constraint.constraint_type;

        if (!param_name || !constraint_type) {
            semantic_analyzer_add_error(analyzer, "Invalid constraint: missing parameter or type");
            return false;
        }

        int param_index = -1;
        for (int j = 0; j < template_param_count; j++) {
            if (template_params[j] && strcmp(template_params[j], param_name) == 0) {
                param_index = j;
                break;
            }
        }

        if (param_index == -1) {
            semantic_analyzer_add_error(analyzer, "Constraint parameter '%s' not found in template parameters",
                                        param_name);
            return false;
        }

        const char* type_arg = type_args[param_index];
        if (!type_arg) {
            semantic_analyzer_add_error(analyzer, "Missing type argument for parameter '%s'", param_name);
            return false;
        }

        if (strcmp(constraint_type, "class") == 0) {

            if (strcmp(type_arg, "int") == 0 || strcmp(type_arg, "float") == 0 || strcmp(type_arg, "string") == 0 ||
                strcmp(type_arg, "bool") == 0 || strcmp(type_arg, "char") == 0) {
                semantic_analyzer_add_error(
                    analyzer,
                    "Type argument '%s' for parameter '%s' violates class constraint (must be reference type)",
                    type_arg, param_name);
                return false;
            }
        } else if (strcmp(constraint_type, "struct") == 0) {

            if (strcmp(type_arg, "int") != 0 && strcmp(type_arg, "float") != 0 && strcmp(type_arg, "bool") != 0 &&
                strcmp(type_arg, "char") != 0) {
                semantic_analyzer_add_error(
                    analyzer, "Type argument '%s' for parameter '%s' violates struct constraint (must be value type)",
                    type_arg, param_name);
                return false;
            }
        } else if (strcmp(constraint_type, "new()") == 0) {
            SymbolEntry* arg_class = semantic_analyzer_lookup_class(analyzer, type_arg);
            char* default_ctor =
                (arg_class && arg_class->nested_table) ? name_mangle_constructor(type_arg, 0) : NULL;
            bool has_default_ctor = default_ctor && arg_class && arg_class->nested_table &&
                                   symbol_table_lookup(arg_class->nested_table, default_ctor);
            KRT_FREE(default_ctor);
            if (!has_default_ctor) {
                semantic_analyzer_add_error(analyzer,
                                            "Type argument '%s' has no default constructor for the new() constraint",
                                            type_arg);
                return false;
            }
            continue;
        } else if (strcmp(constraint_type, "IDisposable") == 0) {
            if (strcmp(type_arg, "string") == 0) {
                semantic_analyzer_add_error(analyzer,
                                            "Type argument '%s' for parameter '%s' does not implement interface '%s'",
                                            type_arg, param_name, constraint_type);
                return false;
            }
        } else {
            semantic_analyzer_add_error(analyzer, "Unknown constraint type '%s'", constraint_type);
            return false;
        }
    }

    return true;
}

SymbolTable* semantic_analyzer_get_symbol_table(SemanticAnalyzer* analyzer) {
    return analyzer ? analyzer->symbol_table : NULL;
}

#include "TypeExpansion.inc"

SemanticAnalysisResult* semantic_analyzer_analyze(SemanticAnalyzer* analyzer, ASTNode* ast) {
    if (!analyzer || !ast) {
        return NULL;
    }

    SemanticAnalysisResult* result = (SemanticAnalysisResult*)KRT_MALLOC(sizeof(SemanticAnalysisResult));
    if (!result) {
        return NULL;
    }

    result->success = true;
    result->error_count = 0;
    result->warning_count = 0;
    result->symbol_table = analyzer->symbol_table;
    result->error_messages = NULL;

    analyzer->error_count = 0;
    analyzer->warning_count = 0;
    semantic_analyzer_collect_project_namespaces(analyzer, ast);
    if (analyzer->project_namespace_collection_failed) {
        semantic_analyzer_add_error(analyzer, "Out of memory while collecting project namespaces");
        result->success = false;
    }
    if (!semantic_expand_types(analyzer, ast)) {
        result->success = false;
        return result;
    }

    if (ast->type == AST_PROGRAM || ast->type == AST_BLOCK) {
        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && decl->type == AST_USING_DIRECTIVE) {
                if (!semantic_analyzer_analyze_statement(analyzer, decl)) {
                    result->success = false;
                }
            }
        }

        if (!semantic_analyzer_analyze_dependencies(analyzer)) {
            result->success = false;
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && decl->type == AST_NAMESPACE_DECLARATION) {
                if (!semantic_analyzer_analyze_statement(analyzer, decl)) {
                    result->success = false;
                }
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && decl->type == AST_CLASS_DECLARATION) {
                const char* class_name = decl->data.class_decl.name;
                SymbolEntry* existing = symbol_table_lookup_current_scope(analyzer->symbol_table, class_name);
                if (!existing) {
                    SymbolEntry* class_sym = symbol_table_declare(analyzer->symbol_table, class_name, SYMBOL_CLASS, 0);
                    if (class_sym) {
                        class_sym->state = SYMBOL_FORWARD_REF;
                        if (!class_sym->nested_table) {
                            class_sym->nested_table = symbol_table_create();
                        }
                    }
                } else if (existing->state == SYMBOL_FORWARD_REF) {
                    semantic_analyzer_add_error(analyzer, "Class %s already declared", class_name);
                    result->success = false;
                }
            }
        }

        /* Class bodies must see global signatures before their calls are checked. */
        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && (decl->type == AST_FUNCTION_DECLARATION || decl->type == AST_STATIC_FUNCTION_DECLARATION)) {
                const char* fn = (decl->type == AST_FUNCTION_DECLARATION) ? decl->data.function_decl.name
                                                                          : decl->data.static_function_decl.name;
                if (!symbol_table_lookup(analyzer->symbol_table, fn)) {
                    SymbolEntry* pre = symbol_table_define(analyzer->symbol_table, fn, SYMBOL_FUNCTION, 0, NULL);
                    if (pre) {
                        pre->state = SYMBOL_FORWARD_REF;
                        pre->source_type = KrtSourceTypeClone(
                            analyzer->type_arena, (KrtSourceType){.token = TOKEN_FN, .function = decl->function_type});
                        pre->value_type = (decl->type == AST_FUNCTION_DECLARATION)
                                              ? decl->data.function_decl.return_type
                                              : decl->data.static_function_decl.return_type;
                    }
                }
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && (decl->type == AST_CLASS_DECLARATION || decl->type == AST_TEMPLATE_DECLARATION)) {
                if (!semantic_analyzer_analyze_statement(analyzer, decl)) {
                    result->success = false;
                }
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && decl->type == AST_STATIC_VARIABLE_DECLARATION) {
                if (!semantic_analyzer_analyze_static_variable_decl(analyzer, decl)) {
                    result->success = false;
                }
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && decl->type == AST_VARIABLE_DECLARATION) {
                semantic_analyzer_add_error_at(
                    analyzer, decl,
                    "Top-level variable '%s' must be declared 'static' (file-scope variables require 'static')",
                    decl->data.variable_decl.name);
                result->success = false;
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* decl = ast->data.block.statements[i];
            if (decl && (decl->type == AST_FUNCTION_DECLARATION || decl->type == AST_STATIC_FUNCTION_DECLARATION)) {
                FunctionAnalysisResult func_result = semantic_analyzer_analyze_function(analyzer, decl);
                if (!func_result.success) {
                    result->success = false;
                }
            }
        }

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* stmt = ast->data.block.statements[i];
            if (!stmt) {
                continue;
            }
            if (stmt->type == AST_VARIABLE_DECLARATION) {
                /* Top-level non-static variables were already reported above. */
                continue;
            }
            if (stmt->type != AST_FUNCTION_DECLARATION && stmt->type != AST_STATIC_FUNCTION_DECLARATION &&
                stmt->type != AST_CLASS_DECLARATION && stmt->type != AST_TEMPLATE_DECLARATION &&
                stmt->type != AST_STATIC_VARIABLE_DECLARATION && stmt->type != AST_NAMESPACE_DECLARATION &&
                stmt->type != AST_USING_DIRECTIVE) {
                if (!semantic_analyzer_analyze_statement(analyzer, stmt)) {
                    result->success = false;
                }
            }
        }

        if (analyzer->global_symbol_table) {
            analyzer->symbol_table = analyzer->global_symbol_table;
        }
    } else if (ast->type == AST_FUNCTION_DECLARATION || ast->type == AST_STATIC_FUNCTION_DECLARATION) {

        FunctionAnalysisResult func_result = semantic_analyzer_analyze_function(analyzer, ast);
        if (!func_result.success) {
            result->success = false;
        }
    } else if (ast->type == AST_BLOCK) {

        for (int i = 0; i < ast->data.block.statement_count; i++) {
            ASTNode* stmt = ast->data.block.statements[i];
            if (stmt && !semantic_analyzer_analyze_statement(analyzer, stmt)) {
                result->success = false;
            }
        }
    } else {

        if (!semantic_analyzer_analyze_statement(analyzer, ast)) {
            result->success = false;
        }
    }

    if (!symbol_table_check_undefined(analyzer->symbol_table)) {
        result->success = false;
    }

    if (!analyzer->has_entry_point) {
        SymbolEntry* main_sym = symbol_table_lookup_current_scope(analyzer->symbol_table, "main");
        if (main_sym && main_sym->type == SYMBOL_FUNCTION) {
            analyzer->has_entry_point = true;
        }
    }

    if (!analyzer->has_entry_point && analyzer->require_entry_point) {
        semantic_analyzer_add_warning(
            analyzer,
            "No entry point (main function) defined. The program will not have a valid starting point for execution.");
    }

    result->error_count = analyzer->error_count;
    result->warning_count = analyzer->warning_count;
    result->error_report = analyzer->error_report;

    return result;
}

static char* resolve_using_path(char** path_parts, int path_length, bool* is_wildcard) {
    if (!path_parts || path_length <= 0) {
        return NULL;
    }

    *is_wildcard = false;

    int capacity = 256;
    char* resolved = (char*)KRT_MALLOC(capacity);
    if (!resolved) {
        return NULL;
    }
    int len = 0;
    resolved[0] = '\0';

    for (int i = 0; i < path_length; i++) {
        if (!path_parts[i]) {
            KRT_FREE(resolved);
            return NULL;
        }
        if (strcmp(path_parts[i], "*") == 0) {
            *is_wildcard = true;
            break;
        }

        size_t part_length = strlen(path_parts[i]);
        if (part_length + (size_t)len + 2 > (size_t)INT_MAX) {
            KRT_FREE(resolved);
            return NULL;
        }
        int part_len = (int)part_length;
        if (len + part_len + 2 > capacity) {
            if (len + part_len + 2 > INT_MAX / 2) {
                KRT_FREE(resolved);
                return NULL;
            }
            int new_capacity = (len + part_len + 2) * 2;
            char* grown = (char*)KRT_REALLOC(resolved, new_capacity);
            if (!grown) {
                KRT_FREE(resolved);
                return NULL;
            }
            resolved = grown;
            capacity = new_capacity;
        }

        if (i > 0) {
            resolved[len++] = '.';
            resolved[len] = '\0';
        }

        strcat(resolved + len, path_parts[i]);
        len += part_len;
    }

    /* Namespaces are dotted internally; only the filesystem path uses '/'. */
    for (int i = 0; i < len; i++) {
        if (resolved[i] == '.') {
            resolved[i] = '/';
        }
    }
    return resolved;
}

static bool namespace_matches_using(const char* namespace_name, const UsingDirective* directive) {
    if (!namespace_name || !directive || !directive->namespace_path || directive->path_length <= 0) {
        return false;
    }

    const char* current = namespace_name;
    for (int i = 0; i < directive->path_length; i++) {
        const char* part = directive->namespace_path[i];
        if (!part || strcmp(part, "*") == 0) {
            return false;
        }
        size_t length = strlen(part);
        if (strncmp(current, part, length) != 0) {
            return false;
        }
        current += length;
        if (i + 1 < directive->path_length) {
            if (*current != '.') {
                return false;
            }
            current++;
        }
    }
    return *current == '\0' || *current == '.';
}

static bool semantic_analyzer_using_is_project_namespace(const SemanticAnalyzer* analyzer,
                                                          const UsingDirective* directive) {
    if (!analyzer || !directive) {
        return false;
    }
    for (int i = 0; i < analyzer->project_namespace_count; i++) {
        if (namespace_matches_using(analyzer->project_namespace_names[i], directive)) {
            return true;
        }
    }
    return false;
}

static char* try_file_path(const char* path) {
    if (!path) {
        return NULL;
    }

    FILE* fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return KRT_STRDUP(path);
    }
    return NULL;
}

static bool semantic_try_lazy_import_class(SemanticAnalyzer* analyzer, const char* class_name) {
    if (!analyzer || !class_name || !analyzer->libs_path) {
        return false;
    }

    char path[1024];
    int length = snprintf(path, sizeof(path), "%s/%s.krt", analyzer->libs_path, class_name);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        semantic_analyzer_add_error(analyzer, "Using: library path for '%s' is too long", class_name);
        return false;
    }
    FILE* fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return import_krt_file(analyzer, path);
    }

    DIR* dir = opendir(analyzer->libs_path);
    if (!dir) {
        return false;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char sub[1024];
        if (snprintf(sub, sizeof(sub), "%s/%s", analyzer->libs_path, ent->d_name) >= (int)sizeof(sub)) {
            continue;
        }
        struct stat st;
        if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) {
            continue;
        }
        int sub_path_length = snprintf(path, sizeof(path), "%s/%s.krt", sub, class_name);
        if (sub_path_length < 0 || (size_t)sub_path_length >= sizeof(path)) {
            continue;
        }
        fp = fopen(path, "r");
        if (fp) {
            fclose(fp);
            bool imported = import_krt_file(analyzer, path);
            closedir(dir);
            return imported;
        }
        DIR* sub_dir = opendir(sub);
        if (!sub_dir) {
            continue;
        }
        struct dirent* sub_ent;
        while ((sub_ent = readdir(sub_dir)) != NULL) {
            if (sub_ent->d_name[0] == '.') {
                continue;
            }
            char deep[1280];
            snprintf(deep, sizeof(deep), "%s/%s", sub, sub_ent->d_name);
            if (stat(deep, &st) != 0 || !S_ISDIR(st.st_mode)) {
                continue;
            }
            length = snprintf(path, sizeof(path), "%s/%s.krt", deep, class_name);
            if (length < 0 || (size_t)length >= sizeof(path)) {
                continue;
            }
            fp = fopen(path, "r");
            if (fp) {
                fclose(fp);
                bool imported = import_krt_file(analyzer, path);
                closedir(sub_dir);
                closedir(dir);
                return imported;
            }
        }
        closedir(sub_dir);
    }
    closedir(dir);
    return false;
}

static int semantic_analyzer_find_imported_file(SemanticAnalyzer* analyzer, const char* file_path) {
    if (!analyzer || !file_path) {
        return -1;
    }
    for (int i = 0; i < analyzer->imported_source_count; i++) {
        if (strcmp(analyzer->imported_source_paths[i], file_path) == 0) {
            return i;
        }
    }
    return -1;
}

static bool semantic_analyzer_has_imported_file(SemanticAnalyzer* analyzer, const char* file_path) {
    return semantic_analyzer_find_imported_file(analyzer, file_path) >= 0;
}

static bool semantic_analyzer_remember_imported_file(SemanticAnalyzer* analyzer, const char* file_path) {
    if (semantic_analyzer_has_imported_file(analyzer, file_path)) {
        return true;
    }
    if (analyzer->imported_source_count == analyzer->imported_source_capacity) {
        if (!krt_capacity_can_grow(analyzer->imported_source_capacity, sizeof(*analyzer->imported_source_paths))) {
            return false;
        }
        int capacity = analyzer->imported_source_capacity == 0 ? 8 : analyzer->imported_source_capacity * 2;
        char** paths = KRT_MALLOC((size_t)capacity * sizeof(*paths));
        bool* failures = KRT_MALLOC((size_t)capacity * sizeof(*failures));
        if (!paths || !failures) {
            KRT_FREE(paths);
            KRT_FREE(failures);
            return false;
        }
        for (int i = 0; i < analyzer->imported_source_count; i++) {
            paths[i] = analyzer->imported_source_paths[i];
            failures[i] = analyzer->imported_source_failed[i];
        }
        KRT_FREE(analyzer->imported_source_paths);
        KRT_FREE(analyzer->imported_source_failed);
        analyzer->imported_source_paths = paths;
        analyzer->imported_source_failed = failures;
        analyzer->imported_source_capacity = capacity;
    }
    char* copy = KRT_STRDUP(file_path);
    if (!copy) {
        return false;
    }
    analyzer->imported_source_paths[analyzer->imported_source_count] = copy;
    analyzer->imported_source_failed[analyzer->imported_source_count] = false;
    analyzer->imported_source_count++;
    return true;
}

static bool import_krt_file(SemanticAnalyzer* analyzer, const char* file_path) {
    if (!analyzer || !file_path) {
        return false;
    }

    if (analyzer->input_file_path && strcmp(analyzer->input_file_path, file_path) == 0) {
        return true;
    }
    int imported_index = semantic_analyzer_find_imported_file(analyzer, file_path);
    if (imported_index >= 0) {
        if (!semantic_analyzer_register_imported_file(analyzer, file_path)) {
            semantic_analyzer_add_error(analyzer, "Using: unable to record imported source '%s'", file_path);
            return false;
        }
        if (analyzer->imported_source_failed[imported_index]) {
            semantic_analyzer_add_error(analyzer, "Using: imported source '%s' previously failed", file_path);
            return false;
        }
        return true;
    }

    KrtArena* import_arena = KrtArenaCreateLocal(KRT_ARENA_DEFAULT_BLOCK_SIZE);
    if (!import_arena) {
        semantic_analyzer_add_error(analyzer, "Using: unable to allocate memory while importing '%s'", file_path);
        return false;
    }
    ASTNode* imported_ast = KrtStdlibCacheClone(file_path, import_arena, false);
    if (!imported_ast) {
        KrtArenaDestroy(import_arena);
        semantic_analyzer_add_error(analyzer, "Using: unable to read or parse imported source '%s' (%s)", file_path,
                                    KrtStdlibCacheLastError() ? KrtStdlibCacheLastError() : "unknown reason");
        return false;
    }
    if (!semantic_analyzer_register_imported_file(analyzer, file_path)) {
        KrtArenaDestroy(import_arena);
        semantic_analyzer_add_error(analyzer, "Using: unable to record imported source '%s'", file_path);
        return false;
    }
    if (!semantic_analyzer_remember_imported_file(analyzer, file_path)) {
        KrtArenaDestroy(import_arena);
        semantic_analyzer_add_error(analyzer, "Using: unable to retain imported source '%s'", file_path);
        return false;
    }

    int initial_errors = analyzer->error_count;
    if (imported_ast->type == AST_PROGRAM || imported_ast->type == AST_BLOCK) {
        for (int j = 0; j < imported_ast->data.block.statement_count; j++) {
            ASTNode* stmt = imported_ast->data.block.statements[j];
            if (!stmt) {
                continue;
            }
            if (stmt->type == AST_USING_DIRECTIVE) {
                /* Queue copied dependencies; the outer scan handles transitive imports without recursion. */
                semantic_analyzer_add_using_directive(analyzer, stmt);
                continue;
            }

            if (stmt->type == AST_FUNCTION_DECLARATION) {
                const char* func_name = stmt->data.function_decl.name;
                if (!func_name) {
                    semantic_analyzer_add_error(analyzer, "Imported function declaration has no name");
                    continue;
                }
                /* The entry point is registered by the file that defines it,
                 * not by the importing translation unit. */
                if (strcmp(func_name, "main") == 0) {
                    semantic_analyzer_add_warning(
                        analyzer, "Imported source '%s' declares main; the entry point comes from the main source",
                        file_path);
                    continue;
                }
                if (strcmp(func_name, "main") != 0) {
                    SymbolEntry* existing = symbol_table_lookup(analyzer->global_symbol_table, func_name);
                    if (!existing) {
                        existing =
                            symbol_table_define(analyzer->global_symbol_table, func_name, SYMBOL_FUNCTION, 0, NULL);
                        if (existing) {
                            existing->source_type =
                                KrtSourceTypeClone(analyzer->type_arena,
                                                   (KrtSourceType){.token = TOKEN_FN, .function = stmt->function_type});
                            existing->value_type = stmt->data.function_decl.return_type;
                        }
                    }
                }
            } else if (stmt->type == AST_CLASS_DECLARATION) {
                const char* class_name = stmt->data.class_decl.name;
                if (class_name) {
                    SymbolEntry* existing = symbol_table_lookup(analyzer->global_symbol_table, class_name);
                    if (!existing) {
                        existing =
                            symbol_table_define(analyzer->global_symbol_table, class_name, SYMBOL_CLASS, 0, NULL);
                    }

                    ASTNode* body = stmt->data.class_decl.body;
                    if (existing && body && body->type == AST_BLOCK) {
                        struct SymbolScope* prev_scope =
                            symbol_table_enter_nested_scope(analyzer->global_symbol_table, existing);
                        for (int k = 0; k < body->data.block.statement_count; k++) {
                            ASTNode* member = body->data.block.statements[k];
                            if (!member) {
                                continue;
                            }
                            if (member->type == AST_ACCESS_MODIFIER) {
                                member = member->data.access_modifier.member;
                                if (!member) {
                                    continue;
                                }
                            }
                            const char* mname = NULL;
                            KrtTokenType* ptypes = NULL;
                            int pcount = 0;
                            if (member->type == AST_STATIC_FUNCTION_DECLARATION) {
                                mname = member->data.static_function_decl.name;
                                ptypes = member->data.static_function_decl.parameter_types;
                                pcount = member->data.static_function_decl.parameter_count;
                            } else if (member->type == AST_FUNCTION_DECLARATION) {
                                mname = member->data.function_decl.name;
                                ptypes = member->data.function_decl.parameter_types;
                                pcount = member->data.function_decl.parameter_count;
                            } else if (member->type == AST_CONSTRUCTOR_DECLARATION) {
                                mname = class_name;
                                ptypes = member->data.constructor_decl.parameter_types;
                                pcount = member->data.constructor_decl.parameter_count;
                            }
                            if (!mname) {
                                continue;
                            }

                            KrtTokenType ret_type = TOKEN_INT32;
                            if (member->type == AST_STATIC_FUNCTION_DECLARATION) {
                                ret_type = member->data.static_function_decl.return_type;
                            } else if (member->type == AST_FUNCTION_DECLARATION) {
                                ret_type = member->data.function_decl.return_type;
                            } else if (member->type == AST_CONSTRUCTOR_DECLARATION) {
                                ret_type = TOKEN_VOID;
                            }

                            const char* ns_path[2] = {class_name, NULL};
                            char* key = name_mangle_function(ns_path, mname, ptypes, pcount);
                            if (key) {
                                SymbolEntry* fsym =
                                    symbol_table_lookup_current_scope(analyzer->global_symbol_table, key);
                                if (!fsym) {
                                    fsym = symbol_table_define(analyzer->global_symbol_table, key, SYMBOL_FUNCTION, 0,
                                                               NULL);
                                }
                                if (fsym) {
                                    fsym->value_type = ret_type;
                                    fsym->source_type = KrtSourceTypeClone(
                                        analyzer->type_arena,
                                        (KrtSourceType){.token = TOKEN_FN, .function = member->function_type});
                                }
                                KRT_FREE(key);
                            }
                            char* full_name = name_mangle_simple(class_name, mname);
                            SymbolEntry* alias_sym = full_name
                                                          ? symbol_table_lookup_current_scope(
                                                                analyzer->global_symbol_table, full_name)
                                                          : NULL;
                            bool alias_is_new = false;
                            if (full_name && !alias_sym) {
                                alias_sym = symbol_table_define(analyzer->global_symbol_table, full_name,
                                                                SYMBOL_FUNCTION, 0, NULL);
                                alias_is_new = alias_sym != NULL;
                            }
                            /* Keep the first definition: a later overload must not
                             * overwrite the name's type (13-2). */
                            if (alias_sym && alias_is_new) {
                                alias_sym->value_type = ret_type;
                                alias_sym->source_type = KrtSourceTypeClone(
                                    analyzer->type_arena,
                                    (KrtSourceType){.token = TOKEN_FN, .function = member->function_type});
                            }
                            KRT_FREE(full_name);
                            SymbolEntry* bare_sym =
                                symbol_table_lookup_current_scope(analyzer->global_symbol_table, mname);
                            bool bare_is_new = false;
                            if (!bare_sym) {
                                bare_sym =
                                    symbol_table_define(analyzer->global_symbol_table, mname, SYMBOL_FUNCTION, 0, NULL);
                                bare_is_new = bare_sym != NULL;
                            }
                            if (bare_sym && bare_is_new) {
                                bare_sym->value_type = ret_type;
                                bare_sym->source_type = KrtSourceTypeClone(
                                    analyzer->type_arena,
                                    (KrtSourceType){.token = TOKEN_FN, .function = member->function_type});
                            }
                        }
                        symbol_table_exit_nested_scope(analyzer->global_symbol_table, prev_scope);
                    }
                }
            }
        }
    }

    KrtArenaDestroy(import_arena);
    bool success = analyzer->error_count == initial_errors;
    imported_index = semantic_analyzer_find_imported_file(analyzer, file_path);
    if (imported_index >= 0) {
        analyzer->imported_source_failed[imported_index] = !success;
    } else if (!success) {
        semantic_analyzer_add_error(analyzer, "Using: imported source '%s' is not registered", file_path);
    }
    return success;
}

static int compare_import_paths(const void* left, const void* right) {
    const char* const* a = (const char* const*)left;
    const char* const* b = (const char* const*)right;
    return strcmp(*a, *b);
}

static bool import_namespace_folder(SemanticAnalyzer* analyzer, const char* folder_path) {
    if (!analyzer || !folder_path) {
        return false;
    }

    int initial_errors = analyzer->error_count;
    DIR* directory = opendir(folder_path);
    if (!directory) {
        semantic_analyzer_add_error(analyzer, "Using: cannot open namespace directory '%s'", folder_path);
        return false;
    }

    char** paths = NULL;
    int count = 0;
    int capacity = 0;
    bool success = true;
    struct dirent* entry;
    for (;;) {
        errno = 0;
        entry = readdir(directory);
        if (!entry) {
            if (errno != 0) {
                success = false;
            }
            break;
        }
        const char* name = entry->d_name;
        size_t length = strlen(name);
        if (name[0] == '.' || length <= 4 || strcmp(name + length - 4, ".krt") != 0) {
            continue;
        }

        char path[PATH_MAX];
        int path_length = snprintf(path, sizeof(path), "%s/%s", folder_path, name);
        struct stat st;
        if (path_length < 0 || (size_t)path_length >= sizeof(path) || stat(path, &st) != 0) {
            /* Skip the bad entry and keep importing the rest of the folder. */
            success = false;
            continue;
        }
        if (!S_ISREG(st.st_mode)) {
            continue;
        }
        if (count == capacity) {
            if (!krt_capacity_can_grow(capacity, sizeof(*paths))) {
                success = false;
                break;
            }
            int new_capacity = capacity == 0 ? 8 : capacity * 2;
            char** new_paths = KRT_REALLOC(paths, (size_t)new_capacity * sizeof(*new_paths));
            if (!new_paths) {
                success = false;
                break;
            }
            paths = new_paths;
            capacity = new_capacity;
        }
        paths[count] = KRT_STRDUP(path);
        if (!paths[count]) {
            success = false;
            break;
        }
        count++;
    }

    if (closedir(directory) != 0) {
        success = false;
    }
    if (success && count == 0) {
        semantic_analyzer_add_error(analyzer, "Using: namespace directory '%s' contains no .krt files", folder_path);
        success = false;
    }
    if (success) {
        qsort(paths, (size_t)count, sizeof(*paths), compare_import_paths);
        for (int i = 0; i < count; i++) {
            if (!import_krt_file(analyzer, paths[i])) {
                success = false;
            }
        }
    }
    if (!success && analyzer->error_count == initial_errors) {
        semantic_analyzer_add_error(analyzer, "Using: failed to enumerate namespace directory '%s'", folder_path);
    }
    for (int i = 0; i < count; i++) {
        KRT_FREE(paths[i]);
    }
    KRT_FREE(paths);
    return success;
}

bool semantic_analyzer_analyze_dependencies(SemanticAnalyzer* analyzer) {
    if (!analyzer || analyzer->using_count == 0) {
        return true;
    }

    bool success = true;

    for (int i = 0; i < analyzer->using_count; i++) {
        UsingDirective* directive = analyzer->using_directives[i];
        if (!directive || !directive->namespace_path || directive->path_length == 0) {
            semantic_analyzer_add_error(analyzer, "Using: malformed using directive");
            success = false;
            continue;
        }
        if (semantic_analyzer_using_is_project_namespace(analyzer, directive)) {
            continue;
        }
        bool is_wildcard = false;
        char* relative_path = resolve_using_path(directive->namespace_path, directive->path_length, &is_wildcard);
        if (!relative_path) {
            semantic_analyzer_add_error(analyzer, "Using: unable to resolve import path");
            success = false;
            continue;
        }

        const char* libs_dir = analyzer->libs_path ? analyzer->libs_path : "./libs";
        char folder_path_resolved[1024];
        int folder_length = snprintf(folder_path_resolved, sizeof(folder_path_resolved), "%s/%s", libs_dir, relative_path);
        if (folder_length < 0 || (size_t)folder_length >= sizeof(folder_path_resolved)) {
            semantic_analyzer_add_error(analyzer, "Using: import path is too long");
            KRT_FREE(relative_path);
            success = false;
            continue;
        }

        if (is_wildcard) {
            if (!import_namespace_folder(analyzer, folder_path_resolved)) {
                success = false;
            }
            KRT_FREE(relative_path);
            continue;
        }

        struct stat st;
        if (stat(folder_path_resolved, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!import_namespace_folder(analyzer, folder_path_resolved)) {
                success = false;
            }
            KRT_FREE(relative_path);
            continue;
        }

        char file_path_with_ext[1024];
        int file_length = snprintf(file_path_with_ext, sizeof(file_path_with_ext), "%s/%s.krt", libs_dir, relative_path);
        if (file_length < 0 || (size_t)file_length >= sizeof(file_path_with_ext)) {
            semantic_analyzer_add_error(analyzer, "Using: import path is too long");
            KRT_FREE(relative_path);
            success = false;
            continue;
        }
        char* file_path = try_file_path(file_path_with_ext);

        if (!file_path) {
            semantic_analyzer_add_error(analyzer, "Using: cannot find '%s' in project libraries '%s'", relative_path,
                                        libs_dir);
            KRT_FREE(relative_path);
            success = false;
            continue;
        }

        if (!import_krt_file(analyzer, file_path)) {
            success = false;
        }

        KRT_FREE(relative_path);
        KRT_FREE(file_path);
    }

    return success;
}
