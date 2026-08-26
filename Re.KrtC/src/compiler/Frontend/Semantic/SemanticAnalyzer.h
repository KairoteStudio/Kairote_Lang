#ifndef SEMANTIC_ANALYZER_H
#define SEMANTIC_ANALYZER_H

#include "SymbolTable.h"
#include "../Parser/Ast.h"
#include "../../Middle/Ir/Ir.h"
#include "Generics.h"
#include "../CompilerError.h"

typedef struct UsingDirective {
    char* alias;  
    char** namespace_path;  
    int path_length;
    int is_alias;  
} UsingDirective;

typedef struct SemanticAnalyzer {
    SymbolTable* symbol_table;
    SymbolTable* global_symbol_table;  
    KrtIRBuilder* ir_builder;
    int error_count;
    int warning_count;
    bool has_entry_point;
    bool require_entry_point;
    char* entry_point_name;
    char** class_name_stack;
    int class_stack_size;
    int class_stack_capacity;
    GenericRegistry* generic_registry;
    bool generic_registry_shared;
    int is_unsafe_mode;
    int point_depth;
    UsingDirective** using_directives;
    int using_count;
    int using_capacity;
    char* input_file_path;
    char* libs_path;
    void* pipeline;
    KrtErrorReport* error_report;
} SemanticAnalyzer;

typedef struct SemanticAnalysisResult {
    bool success;
    int error_count;
    int warning_count;
    SymbolTable* symbol_table;
    char* error_messages;
    KrtErrorReport* error_report;`
} SemanticAnalysisResult;

SemanticAnalyzer* semantic_analyzer_create(void);
void semantic_analyzer_destroy(SemanticAnalyzer* analyzer);
SymbolTable* semantic_analyzer_get_symbol_table(SemanticAnalyzer* analyzer);
SemanticAnalysisResult* semantic_analyzer_analyze(SemanticAnalyzer* analyzer,
                                                 ASTNode* ast);
typedef struct FunctionAnalysisResult {
    bool success;
    int param_count;
    bool has_return_statement;
    SymbolEntry* function_symbol;
} FunctionAnalysisResult;
FunctionAnalysisResult semantic_analyzer_analyze_function(SemanticAnalyzer* analyzer,
                                                         ASTNode* function_node);
bool semantic_analyzer_analyze_statement(SemanticAnalyzer* analyzer, ASTNode* stmt);
bool semantic_analyzer_analyze_expression(SemanticAnalyzer* analyzer, ASTNode* expr);
bool semantic_analyzer_analyze_variable_decl(SemanticAnalyzer* analyzer, ASTNode* var_decl);
bool semantic_analyzer_analyze_generic_decl(SemanticAnalyzer* analyzer, ASTNode* generic_decl);
bool semantic_analyzer_validate_constraints(SemanticAnalyzer* analyzer, ASTNode* class_decl, const char** type_args, int arg_count);
bool semantic_analyzer_analyze_generic_instantiation(SemanticAnalyzer* analyzer, const char* base_type, const char** type_args, int arg_count);
bool semantic_analyzer_analyze_function_call(SemanticAnalyzer* analyzer, ASTNode* call_expr);
bool semantic_analyzer_analyze_variable_use(SemanticAnalyzer* analyzer, const char* var_name);
bool semantic_analyzer_check_entry_point(SemanticAnalyzer* analyzer);
bool semantic_analyzer_analyze_dependencies(SemanticAnalyzer* analyzer);
bool semantic_analyzer_resolve_forward_references(SemanticAnalyzer* analyzer);
SemanticAnalysisResult* semantic_analyzer_get_result(SemanticAnalyzer* analyzer);
void semantic_analyzer_add_error(SemanticAnalyzer* analyzer, const char* format, ...);
void semantic_analyzer_add_error_at(SemanticAnalyzer* analyzer, ASTNode* node,
                                    const char* format, ...);
void semantic_analyzer_add_error_ex(SemanticAnalyzer* analyzer, int line, int column,
                                    const char* message, const char* hint);
void semantic_analyzer_add_error_ex_at(SemanticAnalyzer* analyzer, ASTNode* node,
                                       KrtErrorStage stage, const char* message,
                                       const char* hint);
void semantic_analyzer_add_warning(SemanticAnalyzer* analyzer, const char* format, ...);
const char* semantic_analyzer_get_symbol_type_name(SymbolType type);
bool semantic_analyzer_is_valid_symbol_name(const char* name);
void semantic_analyzer_push_class_context(SemanticAnalyzer* analyzer, const char* class_name);
void semantic_analyzer_pop_class_context(SemanticAnalyzer* analyzer);
void semantic_analyzer_set_input_file(SemanticAnalyzer* analyzer, const char* file_path);
void semantic_analyzer_set_pipeline(SemanticAnalyzer* analyzer, void* pipeline);
void semantic_analyzer_register_imported_file(SemanticAnalyzer* analyzer, const char* file_path);
SymbolEntry* semantic_analyzer_lookup_qualified_name(SemanticAnalyzer* analyzer,
                                                     char** parts,
                                                     int part_count);
const char* semantic_analyzer_get_current_class_context(SemanticAnalyzer* analyzer);
void semantic_analyzer_collect_exports(SemanticAnalyzer* analyzer, ASTNode* ast);

#endif
