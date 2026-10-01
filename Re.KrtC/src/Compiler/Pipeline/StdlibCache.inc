#include <pthread.h>

#define KRT_STDLIB_CACHE_FILES 256
#define KRT_STDLIB_CACHE_BYTES (64u * 1024u * 1024u)

typedef struct {
    char* path;
    struct stat version;
    char* source;
    Lexer* lexer;
    Parser* parser;
    ASTNode* ast;
    size_t bytes;
    unsigned long long used;
    bool expand_macros;
} KrtStdlibCacheEntry;

static KrtStdlibCacheEntry g_stdlib_cache[KRT_STDLIB_CACHE_FILES];
static pthread_mutex_t g_stdlib_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_stdlib_cache_once = PTHREAD_ONCE_INIT;
static size_t g_stdlib_cache_bytes;
static unsigned long long g_stdlib_cache_clock, g_stdlib_cache_hits, g_stdlib_cache_misses;

static void free_stdlib_entry(KrtStdlibCacheEntry* entry) {
    if (entry->parser) {
        parser_destroy(entry->parser);
    }
    if (entry->lexer) {
        lexer_destroy(entry->lexer);
    }
    KRT_FREE(entry->source);
    KRT_FREE(entry->path);
    memset(entry, 0, sizeof(*entry));
}

void KrtStdlibCacheClear(void) {
    pthread_mutex_lock(&g_stdlib_cache_mutex);
    for (int i = 0; i < KRT_STDLIB_CACHE_FILES; i++) {
        free_stdlib_entry(&g_stdlib_cache[i]);
    }
    g_stdlib_cache_bytes = 0;
    g_stdlib_cache_hits = g_stdlib_cache_misses = g_stdlib_cache_clock = 0;
    pthread_mutex_unlock(&g_stdlib_cache_mutex);
}

void KrtStdlibCacheGetStats(unsigned long long* hits, unsigned long long* misses) {
    pthread_mutex_lock(&g_stdlib_cache_mutex);
    if (hits) {
        *hits = g_stdlib_cache_hits;
    }
    if (misses) {
        *misses = g_stdlib_cache_misses;
    }
    pthread_mutex_unlock(&g_stdlib_cache_mutex);
}

static void initialize_stdlib_cache(void) {
    atexit(KrtStdlibCacheClear);
}

static bool same_stdlib_version(const struct stat* a, const struct stat* b) {
    if (a->st_size != b->st_size || a->st_mtime != b->st_mtime || a->st_ctime != b->st_ctime ||
        a->st_ino != b->st_ino || a->st_dev != b->st_dev) {
        return false;
    }
#ifdef __linux__
    if (a->st_mtim.tv_nsec != b->st_mtim.tv_nsec || a->st_ctim.tv_nsec != b->st_ctim.tv_nsec) {
        return false;
    }
#endif
    return true;
}

static bool parse_stdlib_entry(KrtStdlibCacheEntry* entry, const char* path, const struct stat* version,
                               bool expand_macros) {
    if (version->st_size < 0) {
        return false;
    }
    FILE* file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    size_t length = (size_t)version->st_size;
    char* source = KRT_MALLOC(length + 1);
    if (!source) {
        fclose(file);
        return false;
    }
    size_t read = fread(source, 1, length, file);
    bool ok = read == length && !ferror(file);
    fclose(file);
    source[read] = '\0';
    if (!ok) {
        KRT_FREE(source);
        return false;
    }
    if (expand_macros) {
        Preprocessor* preprocessor = PreprocessorCreate();
        if (!preprocessor) {
            KRT_FREE(source);
            return false;
        }
        load_standard_macros(preprocessor);
        entry->source = PreprocessorProcess(preprocessor, source);
        PreprocessorDestroy(preprocessor);
        KRT_FREE(source);
    } else {
        entry->source = source;
    }
    entry->expand_macros = expand_macros;
    if (!entry->source) {
        return false;
    }
    entry->path = KRT_STRDUP(path);
    if (!entry->path) {
        return false;
    }
    entry->lexer = lexer_create(entry->source);
    if (!entry->lexer || entry->lexer->error_count) {
        return false;
    }
    entry->parser = parser_create(entry->lexer);
    if (!entry->parser) {
        return false;
    }
    entry->parser->source_name = entry->path;
    entry->ast = parser_parse(entry->parser);
    if (!entry->ast || entry->parser->error_count || entry->lexer->error_count) {
        return false;
    }
    entry->version = *version;
    entry->bytes = strlen(entry->source) + 1 + entry->parser->arena->total_allocated +
                   (entry->lexer->arena ? entry->lexer->arena->total_allocated : 0);
    return true;
}

ASTNode* KrtStdlibCacheClone(const char* path, KrtArena* arena, bool expand_macros) {
    if (!path || !arena) {
        return NULL;
    }
    pthread_once(&g_stdlib_cache_once, initialize_stdlib_cache);
    pthread_mutex_lock(&g_stdlib_cache_mutex);
    struct stat version;
    if (stat(path, &version) != 0) {
        pthread_mutex_unlock(&g_stdlib_cache_mutex);
        return NULL;
    }
    int slot = -1;
    for (int i = 0; i < KRT_STDLIB_CACHE_FILES; i++) {
        KrtStdlibCacheEntry* entry = &g_stdlib_cache[i];
        if (entry->path && entry->expand_macros == expand_macros && strcmp(entry->path, path) == 0) {
            slot = i;
            if (same_stdlib_version(&entry->version, &version)) {
                entry->used = ++g_stdlib_cache_clock;
                g_stdlib_cache_hits++;
                ASTNode* copy = KrtAstClone(arena, entry->ast);
                pthread_mutex_unlock(&g_stdlib_cache_mutex);
                return copy;
            }
            break;
        }
        if (!entry->path && slot < 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < KRT_STDLIB_CACHE_FILES; i++) {
            if (g_stdlib_cache[i].used < g_stdlib_cache[slot].used) {
                slot = i;
            }
        }
    }
    KrtStdlibCacheEntry* entry = &g_stdlib_cache[slot];
    g_stdlib_cache_bytes -= entry->bytes;
    free_stdlib_entry(entry);
    g_stdlib_cache_misses++;
    if (!parse_stdlib_entry(entry, path, &version, expand_macros)) {
        free_stdlib_entry(entry);
        pthread_mutex_unlock(&g_stdlib_cache_mutex);
        return NULL;
    }
    entry->used = ++g_stdlib_cache_clock;
    g_stdlib_cache_bytes += entry->bytes;
    ASTNode* copy = KrtAstClone(arena, entry->ast);
    while (g_stdlib_cache_bytes > KRT_STDLIB_CACHE_BYTES) {
        int oldest = -1;
        for (int i = 0; i < KRT_STDLIB_CACHE_FILES; i++) {
            if (g_stdlib_cache[i].path && (oldest < 0 || g_stdlib_cache[i].used < g_stdlib_cache[oldest].used)) {
                oldest = i;
            }
        }
        if (oldest < 0) {
            break;
        }
        g_stdlib_cache_bytes -= g_stdlib_cache[oldest].bytes;
        free_stdlib_entry(&g_stdlib_cache[oldest]);
    }
    pthread_mutex_unlock(&g_stdlib_cache_mutex);
    return copy;
}

static int compare_stdlib_paths(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

int KrtCompilePipelineLoadStandardLibrary(KrtCompilePipeline* pipeline, const char* directory) {
    if (!pipeline || !pipeline->ast || !pipeline->parser || !pipeline->parser->arena) {
        return 0;
    }
    char* paths[KRT_STDLIB_CACHE_FILES];
    ASTNode* roots[KRT_STDLIB_CACHE_FILES];
    int count = 0;
    if (!scan_stdlib_directory(directory, paths, KRT_STDLIB_CACHE_FILES, &count)) {
        return 1;
    }
    qsort(paths, count, sizeof(char*), compare_stdlib_paths);
    int total = pipeline->ast->data.block.statement_count;
    bool ok = true;
    for (int i = 0; i < count; i++) {
        roots[i] = KrtStdlibCacheClone(paths[i], pipeline->parser->arena, true);
        if (!roots[i] || roots[i]->type != AST_PROGRAM) {
            ok = false;
            snprintf(pipeline->error_message, sizeof(pipeline->error_message), "Failed to parse standard library: %s",
                     paths[i]);
        } else {
            total += roots[i]->data.block.statement_count;
        }
        KRT_FREE(paths[i]);
    }
    if (!ok) {
        pipeline->success = 0;
        return 0;
    }
    if (!count) {
        return 1;
    }
    ASTNode** statements = KrtArenaAlloc(pipeline->parser->arena, sizeof(ASTNode*) * (size_t)total);
    if (!statements && total) {
        return 0;
    }
    int used = pipeline->ast->data.block.statement_count;
    if (used) {
        memcpy(statements, pipeline->ast->data.block.statements, sizeof(ASTNode*) * (size_t)used);
    }
    for (int i = 0; i < count; i++) {
        int length = roots[i]->data.block.statement_count;
        if (length) {
            memcpy(statements + used, roots[i]->data.block.statements, sizeof(ASTNode*) * (size_t)length);
        }
        used += length;
    }
    pipeline->ast->data.block.statements = statements;
    pipeline->ast->data.block.statement_count = total;
    return 1;
}
