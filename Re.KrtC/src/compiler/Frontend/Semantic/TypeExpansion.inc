/* Resolve declaration names and materialize generic classes before symbol registration. */
#include "../Parser/ParserBase.h"
#include <ctype.h>
#include <limits.h>
#include "Core/Utils/KrtCommon.h"

typedef struct {
    SemanticAnalyzer* analyzer;
    ASTNode** nodes;
    int count;
    int capacity;
    int instances;
} TypeExpansion;

static void* expansion_alloc(TypeExpansion* context, size_t size) {
    void* data = KrtArenaAlloc(context->analyzer->type_arena, size ? size : 1);
    if (!data) {
        KRT_COMPILE_ERROR("Out of memory while expanding source types");
    }
    return data;
}

static char* expansion_string(TypeExpansion* context, const char* text) {
    size_t size = strlen(text) + 1;
    char* result = expansion_alloc(context, size);
    memcpy(result, text, size);
    return result;
}

static char* expansion_join(TypeExpansion* context, const char* scope, const char* name) {
    if (!scope || !*scope) {
        return expansion_string(context, name);
    }
    size_t size = strlen(scope) + strlen(name) + 2;
    char* result = expansion_alloc(context, size);
    snprintf(result, size, "%s.%s", scope, name);
    return result;
}

static void expansion_append(TypeExpansion* context, ASTNode* node) {
    if (context->count == context->capacity) {
        if (context->capacity > INT_MAX / 2) {
            KRT_COMPILE_ERROR("Too many declarations while expanding source types");
        }
        int capacity = context->capacity ? context->capacity * 2 : 32;
        ASTNode** nodes = KRT_REALLOC(context->nodes, (size_t)capacity * sizeof(*nodes));
        if (!nodes) {
            KRT_COMPILE_ERROR("Out of memory while expanding declarations");
        }
        context->nodes = nodes;
        context->capacity = capacity;
    }
    context->nodes[context->count++] = node;
}

static bool expansion_set_scope(ASTNode* node, void* scope) {
    node->lexical_namespace = scope;
    return true;
}

static const char* expansion_decl_name(ASTNode* node) {
    if (node->type == AST_CLASS_DECLARATION) {
        return node->data.class_decl.name;
    }
    if (node->type == AST_FUNCTION_DECLARATION || node->type == AST_STATIC_FUNCTION_DECLARATION) {
        return node->data.function_decl.name;
    }
    return NULL;
}

static ASTNode* expansion_find(TypeExpansion* context, const char* name) {
    for (int i = 0; i < context->count; i++) {
        const char* key = expansion_decl_name(context->nodes[i]);
        if (key && !strcmp(key, name)) {
            return context->nodes[i];
        }
    }
    return NULL;
}

static char* expansion_resolve(TypeExpansion* context, const char* scope, const char* name) {
    char* parent = expansion_string(context, scope ? scope : "");
    while (*parent) {
        char* candidate = expansion_join(context, parent, name);
        if (expansion_find(context, candidate) ||
            generics_lookup_type(context->analyzer->generic_registry, candidate)) {
            return candidate;
        }
        char* dot = strrchr(parent, '.');
        if (!dot) {
            break;
        }
        *dot = 0;
    }
    return expansion_string(context, name);
}

static void expansion_collect(TypeExpansion* context, ASTNode* node, const char* scope) {
    if (!node) {
        return;
    }
    if (node->type == AST_PROGRAM) {
        for (int i = 0; i < node->data.block.statement_count; i++) {
            expansion_collect(context, node->data.block.statements[i], scope);
        }
        return;
    }
    if (node->type == AST_NAMESPACE_DECLARATION && node->data.namespace_decl.body) {
        ASTNode* body = node->data.namespace_decl.body;
        char* nested = expansion_join(context, scope, node->data.namespace_decl.name);
        for (int i = 0; i < body->data.block.statement_count; i++) {
            expansion_collect(context, body->data.block.statements[i], nested);
        }
        return;
    }
    if (scope && *scope) {
        KrtAstVisit(node, expansion_set_scope, (void*)scope);
        if (node->type == AST_CLASS_DECLARATION) {
            node->data.class_decl.name = expansion_join(context, scope, node->data.class_decl.name);
        } else if (node->type == AST_FUNCTION_DECLARATION || node->type == AST_STATIC_FUNCTION_DECLARATION) {
            node->data.function_decl.name = expansion_join(context, scope, node->data.function_decl.name);
        }
    }
    if (node->type == AST_CLASS_DECLARATION && node->data.class_decl.template_param_count) {
        GenericParameter* parameters = NULL;
        GenericParameter** tail = &parameters;
        for (int i = 0; i < node->data.class_decl.template_param_count; i++) {
            GenericParameter* parameter = generics_create_parameter(node->data.class_decl.template_params[i]);
            if (!parameter) {
                generics_free_parameters(parameters);
                KRT_COMPILE_ERROR("Out of memory while collecting generic parameters");
            }
            *tail = parameter;
            tail = &parameter->next;
        }
        GenericType* old = generics_lookup_type(context->analyzer->generic_registry, node->data.class_decl.name);
        if (old) {
            semantic_analyzer_add_error_at(context->analyzer, node, "Duplicate generic class: %s",
                                           node->data.class_decl.name);
            generics_free_parameters(parameters);
        } else {
            ASTNode* definition = KrtAstClone(context->analyzer->type_arena, node);
            if (!definition ||
                !generics_register_type(context->analyzer->generic_registry, node->data.class_decl.name, parameters,
                                        node->data.class_decl.template_param_count, definition)) {
                generics_free_parameters(parameters);
                KRT_COMPILE_ERROR("Unable to allocate generic declaration");
            }
        }
        return;
    }
    expansion_append(context, node);
}

typedef struct {
    TypeExpansion* context;
    ASTNode* definition;
    KrtSourceType* types;
    char** names;
} TypeSubstitution;

static char* expansion_substitute_name(TypeSubstitution* mapping, const char* text) {
    if (!text) {
        return NULL;
    }
    size_t capacity = strlen(text) + 1;
    for (int i = 0; i < mapping->definition->data.class_decl.template_param_count; i++) {
        capacity += strlen(text) * strlen(mapping->names[i]);
    }
    char* result = expansion_alloc(mapping->context, capacity);
    char* out = result;
    while (*text) {
        if (!isalpha((unsigned char)*text) && *text != '_') {
            *out++ = *text++;
            continue;
        }
        const char* start = text++;
        while (isalnum((unsigned char)*text) || *text == '_') {
            text++;
        }
        size_t size = text - start;
        const char* replacement = start;
        for (int i = 0; i < mapping->definition->data.class_decl.template_param_count; i++) {
            const char* key = mapping->definition->data.class_decl.template_params[i];
            if (strlen(key) == size && !strncmp(key, start, size)) {
                replacement = mapping->names[i];
                size = strlen(replacement);
                break;
            }
        }
        memcpy(out, replacement, size);
        out += size;
    }
    *out = 0;
    return result;
}

static void expansion_substitute_type(TypeSubstitution* mapping, KrtSourceType* type) {
    if (type->function) {
        expansion_substitute_type(mapping, &type->function->result);
        for (int i = 0; i < type->function->parameter_count; i++) {
            expansion_substitute_type(mapping, &type->function->parameters[i]);
        }
    }
    if (!type->type_name) {
        return;
    }
    for (int i = 0; i < mapping->definition->data.class_decl.template_param_count; i++) {
        if (strcmp(type->type_name, mapping->definition->data.class_decl.template_params[i])) {
            continue;
        }
        KrtSourceType actual = mapping->types[i];
        if (actual.pointer_depth + type->pointer_depth > 64) {
            semantic_analyzer_add_error(mapping->context->analyzer, "Generic pointer nesting exceeds 64 levels");
            return;
        }
        if (type->pointer_depth) {
            uint64_t inner = (actual.pointee_nullable << 1) | actual.nullable;
            actual.pointee_nullable = type->pointer_depth == 64
                                          ? type->pointee_nullable
                                          : (inner << (type->pointer_depth - 1)) | type->pointee_nullable;
            actual.nullable = type->nullable;
        } else {
            actual.nullable |= type->nullable;
        }
        actual.pointer_depth += type->pointer_depth;
        actual.is_ref = type->is_ref;
        *type = actual;
        return;
    }
    type->type_name = expansion_substitute_name(mapping, type->type_name);
}

static void expansion_sync_types(ASTNode* node) {
    if (node->function_type &&
        (node->type == AST_FUNCTION_DECLARATION || node->type == AST_STATIC_FUNCTION_DECLARATION)) {
        node->data.function_decl.return_type = KrtSourceStorage(node->function_type->result);
        for (int i = 0; i < node->function_type->parameter_count; i++) {
            node->data.function_decl.parameter_types[i] = KrtSourceStorage(node->function_type->parameters[i]);
        }
    }
    if (node->type == AST_VARIABLE_DECLARATION && node->declared_type.token) {
        node->data.variable_decl.type = KrtSourceStorage(node->declared_type);
        node->data.variable_decl.pointer_depth = node->declared_type.pointer_depth;
        node->data.variable_decl.template_instantiation_type = node->declared_type.type_name;
    }
}

static bool expansion_substitute_node(ASTNode* node, void* data) {
    TypeSubstitution* mapping = data;
    expansion_substitute_type(mapping, &node->declared_type);
    KrtSourceType function = {.function = node->function_type};
    expansion_substitute_type(mapping, &function);
    if (node->type == AST_NEW_EXPRESSION) {
        node->data.new_expr.class_name = expansion_substitute_name(mapping, node->data.new_expr.class_name);
    }
    expansion_sync_types(node);
    return true;
}

static char* expansion_instantiate(TypeExpansion* context, ASTNode* site, const char* name) {
    const char* bracket = strchr(name, '<');
    if (!bracket) {
        return expansion_resolve(context, site->lexical_namespace, name);
    }
    char* base = expansion_string(context, name);
    base[bracket - name] = 0;
    base = expansion_resolve(context, site->lexical_namespace, base);
    size_t size = strlen(base) + strlen(bracket) + 1;
    char* full = expansion_alloc(context, size);
    snprintf(full, size, "%s%s", base, bracket);
    if (expansion_find(context, full)) {
        return full;
    }
    GenericType* generic = generics_lookup_type(context->analyzer->generic_registry, base);
    if (!generic || !generic->definition || ++context->instances > 256) {
        goto invalid;
    }
    int count = generic->parameter_count;
    KrtSourceType* types = expansion_alloc(context, count * sizeof(*types));
    char** names = expansion_alloc(context, count * sizeof(*names));
    const char* start = bracket + 1;
    for (int i = 0; i < count; i++) {
        const char* end = start;
        int depth = 0;
        while (*end) {
            if ((*end == ',' || *end == '>') && !depth) {
                break;
            }
            if (*end == '<' || *end == '(') {
                depth++;
            }
            if (*end == '>' || *end == ')') {
                depth--;
            }
            end++;
        }
        if (end == start || (i + 1 < count ? *end != ',' : *end != '>' || end[1])) {
            goto invalid;
        }
        names[i] = expansion_string(context, start);
        names[i][end - start] = 0;
        Lexer* lexer = lexer_create(names[i]);
        Parser* parser = lexer ? parser_create(lexer) : NULL;
        if (!parser) {
            lexer_destroy(lexer);
            KRT_COMPILE_ERROR("Out of memory while parsing generic argument");
        }
        KrtSourceType type;
        bool valid = KrtParseSourceType(parser, &type) && parser->current_token.type == TOKEN_EOF &&
                     (type.token != TOKEN_VOID || KrtSourceIsPointer(type)) && !type.is_ref;
        if (valid) {
            types[i] = KrtSourceTypeClone(context->analyzer->type_arena, type);
        }
        parser_destroy(parser);
        lexer_destroy(lexer);
        if (!valid) {
            goto invalid;
        }
        if (types[i].token == TOKEN_IDENTIFIER && types[i].type_name) {
            types[i].type_name = expansion_instantiate(context, site, types[i].type_name);
            if (!expansion_find(context, types[i].type_name) &&
                !semantic_analyzer_lookup_class(context->analyzer, types[i].type_name)) {
                goto invalid;
            }
        }
        start = end + 1;
    }
    ASTNode* instance = KrtAstClone(context->analyzer->type_arena, generic->definition);
    if (!instance) {
        KRT_COMPILE_ERROR("Unable to allocate generic instance");
    }
    TypeSubstitution mapping = {context, generic->definition, types, names};
    KrtAstVisit(instance, expansion_substitute_node, &mapping);
    instance->data.class_decl.name = full;
    const char* constructor = strrchr(base, '.');
    constructor = constructor ? constructor + 1 : base;
    ASTNode* body = instance->data.class_decl.body;
    for (int i = 0; body && i < body->data.block.statement_count; i++) {
        ASTNode* member = body->data.block.statements[i];
        if (member->type == AST_FUNCTION_DECLARATION && !strcmp(member->data.function_decl.name, constructor)) {
            member->data.function_decl.name = full;
        }
    }
    instance->data.class_decl.template_params = NULL;
    instance->data.class_decl.template_param_count = 0;
    expansion_append(context, instance);
    return full;
invalid:
    semantic_analyzer_add_error_at(
        context->analyzer, site,
        "Invalid generic instantiation: %s (check type arguments, arity, and expansion limit 256)", name);
    return full;
}

static void expansion_resolve_type(TypeExpansion* context, ASTNode* node, KrtSourceType* type) {
    if (type->function) {
        expansion_resolve_type(context, node, &type->function->result);
        for (int i = 0; i < type->function->parameter_count; i++) {
            expansion_resolve_type(context, node, &type->function->parameters[i]);
        }
    }
    if (type->token != TOKEN_IDENTIFIER || !type->type_name) {
        return;
    }
    type->type_name = expansion_instantiate(context, node, type->type_name);
    ASTNode* declaration = expansion_find(context, type->type_name);
    if (declaration && declaration->is_enum) {
        type->token = TOKEN_INT32;
    }
}

static char* expansion_expression_name(TypeExpansion* context, ASTNode* node) {
    if (!node) {
        return NULL;
    }
    if (node->type == AST_IDENTIFIER) {
        return node->data.identifier_name;
    }
    if (node->type != AST_MEMBER_ACCESS) {
        return NULL;
    }
    char* prefix = expansion_expression_name(context, node->data.member_access.object);
    return prefix ? expansion_join(context, prefix, node->data.member_access.member_name) : NULL;
}

static bool expansion_resolve_node(ASTNode* node, void* data) {
    TypeExpansion* context = data;
    expansion_resolve_type(context, node, &node->declared_type);
    KrtSourceType function = {.function = node->function_type};
    expansion_resolve_type(context, node, &function);
    expansion_sync_types(node);
    if (node->type == AST_NEW_EXPRESSION) {
        node->data.new_expr.class_name = expansion_instantiate(context, node, node->data.new_expr.class_name);
    }
    if (node->type == AST_CALL || node->type == AST_MEMBER_ACCESS) {
        ASTNode* object = node->type == AST_CALL ? node->data.call.object : node->data.member_access.object;
        char* path = expansion_expression_name(context, object);
        if (path) {
            path = expansion_resolve(context, node->lexical_namespace, path);
            ASTNode* declaration = expansion_find(context, path);
            if (declaration && declaration->type == AST_CLASS_DECLARATION) {
                if (declaration->is_enum && node->type == AST_MEMBER_ACCESS) {
                    ASTNode* body = declaration->data.class_decl.body;
                    for (int i = 0; i < body->data.block.statement_count; i++) {
                        ASTNode* field = body->data.block.statements[i];
                        if (!strcmp(field->data.static_variable_decl.name, node->data.member_access.member_name)) {
                            ASTNode* value = field->data.static_variable_decl.value;
                            node->type = AST_NUMBER;
                            node->is_integer_literal = true;
                            node->integer_value = value->integer_value;
                            node->data.number_value = value->data.number_value;
                            node->declared_type.token = TOKEN_INT32;
                            return false;
                        }
                    }
                    semantic_analyzer_add_error_at(context->analyzer, node, "Unknown enum member: %s",
                                                   node->data.member_access.member_name);
                }
                object->type = AST_IDENTIFIER;
                object->data.identifier_name = path;
            } else if (node->type == AST_CALL) {
                char* qualified = expansion_join(context, path, node->data.call.name);
                declaration = expansion_find(context, qualified);
                if (declaration && declaration->type == AST_FUNCTION_DECLARATION) {
                    node->data.call.object = NULL;
                    node->data.call.name = qualified;
                }
            }
        } else if (node->type == AST_CALL && !object) {
            node->data.call.name = expansion_resolve(context, node->lexical_namespace, node->data.call.name);
        }
    }
    return true;
}

static bool semantic_expand_types(SemanticAnalyzer* analyzer, ASTNode* ast) {
    if (ast->type != AST_PROGRAM && ast->type != AST_BLOCK) {
        return true;
    }
    TypeExpansion context = {.analyzer = analyzer};
    for (int i = 0; i < ast->data.block.statement_count; i++) {
        expansion_collect(&context, ast->data.block.statements[i], "");
    }
    for (int i = 0; i < context.count && !analyzer->error_count; i++) {
        KrtAstVisit(context.nodes[i], expansion_resolve_node, &context);
    }
    ast->data.block.statement_count = context.count;
    ast->data.block.statements = expansion_alloc(&context, context.count * sizeof(*context.nodes));
    if (context.count) {
        memcpy(ast->data.block.statements, context.nodes, context.count * sizeof(*context.nodes));
    }
    KRT_FREE(context.nodes);
    return !analyzer->error_count;
}
