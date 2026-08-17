#ifndef KRT_AST_NODES_H
#define KRT_AST_NODES_H

#include <stdbool.h>
#include "../Lexer/Tokenizer.h"
#include "../../../Core/Memory/Arena.h"

typedef enum {
    AST_PROGRAM,
    AST_FUNCTION_DECLARATION,
    AST_VARIABLE_DECLARATION,
    AST_STATIC_VARIABLE_DECLARATION,
    AST_ASSIGNMENT,
    AST_ARRAY_ASSIGNMENT,
    AST_IF_STATEMENT,
    AST_WHILE_STATEMENT,
    AST_FOR_STATEMENT,
    AST_FOREACH_STATEMENT,
    AST_RETURN_STATEMENT,
    AST_PRINT_STATEMENT,
    AST_BINARY_OPERATION,
    AST_UNARY_OPERATION,
    AST_TERNARY_OPERATION,
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_STRING,
    AST_CHAR_LITERAL,
    AST_BOOLEAN,
    AST_CALL,
    AST_BLOCK,
    AST_ARRAY_LITERAL,
    AST_NEW_EXPRESSION,
    AST_NEW_ARRAY_EXPRESSION,
    AST_NAMESPACE_DECLARATION,
    AST_CLASS_DECLARATION,
    AST_THIS,
    AST_MEMBER_ACCESS,
    AST_ACCESS_MODIFIER,
    AST_CONSTRUCTOR_DECLARATION,
    AST_DESTRUCTOR_DECLARATION,
    AST_STATIC_FUNCTION_DECLARATION,

    AST_TRY_STATEMENT,
    AST_CATCH_CLAUSE,
    AST_FINALLY_CLAUSE,
    AST_THROW_STATEMENT,

    AST_TEMPLATE_DECLARATION,
    AST_TEMPLATE_PARAMETER,
    AST_TEMPLATE_INSTANTIATION,
    AST_GENERIC_TYPE,
    AST_GENERIC_CONSTRAINT,

    AST_STATIC_METHOD_CALL,
    AST_ARRAY_ACCESS,

    AST_COMPOUND_ASSIGNMENT,
    AST_ARRAY_COMPOUND_ASSIGNMENT,

    AST_SWITCH_STATEMENT,
    AST_CASE_CLAUSE,
    AST_DEFAULT_CLAUSE,
    AST_BREAK_STATEMENT,
    AST_CONTINUE_STATEMENT,
    AST_DELETE_STATEMENT,
    AST_USING_STATEMENT,
    AST_USING_DIRECTIVE,
    AST_NAMESPACE_IMPORT,
    AST_QUALIFIED_NAME,

    AST_PROPERTY_DECLARATION,    
    AST_PROPERTY_GETTER,         
    AST_PROPERTY_SETTER,         
    AST_LAMBDA_EXPRESSION,       
    AST_LINQ_QUERY,              
    AST_LINQ_FROM,               
    AST_LINQ_WHERE,              
    AST_LINQ_SELECT,             
    AST_LINQ_ORDERBY,            
    AST_LINQ_JOIN,               
    AST_LINQ_LET,                
    AST_LINQ_GROUP,              
    AST_ATTRIBUTE,               
    AST_ATTRIBUTE_LIST,
    AST_UNSAFE_CALL,
    AST_POINT_BLOCK,
    AST_DEFAULT_EXPRESSION,
    AST_IS_EXPRESSION,
    AST_AS_EXPRESSION,
    AST_SIZEOF_EXPRESSION,
    AST_STACKALLOC_EXPRESSION,
    AST_INTERPOLATED_STRING,
    AST_TUPLE_TYPE,
    AST_TUPLE_EXPRESSION,
    AST_TUPLE_ELEMENT_ACCESS,
    AST_MATCH_EXPRESSION,
    AST_PATTERN_CASE,
    AST_PATTERN_WILDCARD,
    AST_PATTERN_LITERAL,
    AST_PATTERN_VARIABLE,
    AST_PATTERN_TUPLE,
    AST_POINTER_TYPE,
    AST_POINTER_DEREFERENCE,
    AST_ADDRESS_OF,
    AST_FIXED_STATEMENT,
    AST_YIELD_RETURN,
    AST_YIELD_BREAK,
    AST_ASYNC_FUNCTION,
    AST_AWAIT_EXPRESSION,
    AST_LOCK_STATEMENT,
    AST_OPERATOR_OVERLOAD,
    AST_DELEGATE_DECLARATION,
    AST_DELEGATE_TYPE,
    AST_NULLABLE_TYPE,
    AST_NULL_COALESCING,
    AST_NULL_CONDITIONAL,
    AST_CAST_EXPRESSION,
    AST_NULL
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    int line;
    int col;
    KrtTokenType inferred_type;
    union {
        double number_value;
        char* string_value;
        char char_value;
        char* identifier_name;
        int boolean_value;
        struct {
            char* name;
            char** parameters;
            int parameter_count;
            KrtTokenType* parameter_types;
            int* parameter_is_params;
            int* parameter_is_nullable;
            int* parameter_is_array;
            struct ASTNode** parameter_default_values;
            struct ASTNode* body;
            KrtTokenType return_type;
            int is_async;
        } function_decl;
        struct {
            char* name;
            char** parameters;
            int parameter_count;
            KrtTokenType* parameter_types;
            int* parameter_is_params;
            int* parameter_is_nullable;
            int* parameter_is_array;
            struct ASTNode** parameter_default_values;
            struct ASTNode* body;
            KrtTokenType return_type;
        } static_function_decl;
        struct {
            char* name;
            struct ASTNode* value;
            KrtTokenType type;
            struct ASTNode* array_size;
            int is_array;
            int is_let;
            char* template_instantiation_type;
            int is_nullable;
            int pointer_depth;
        } variable_decl;
        struct {
            char* name;
            struct ASTNode* value;
            KrtTokenType type;
        } static_variable_decl;
        struct {
            char* name;
            struct ASTNode* value;
        } assignment;
        struct {
            struct ASTNode* array;
            struct ASTNode* index;
            struct ASTNode* value;
        } array_assignment;
        struct {
            char* name;
            struct ASTNode* value;
            KrtTokenType operator;
        } compound_assignment;
        struct {
            struct ASTNode* array;
            struct ASTNode* index;
            struct ASTNode* value;
            KrtTokenType operator;
        } array_compound_assignment;
        struct {
            struct ASTNode* condition;
            struct ASTNode* then_branch;
            struct ASTNode* else_branch;
        } if_stmt;
        struct {
            struct ASTNode* condition;
            struct ASTNode* body;
        } while_stmt;
        struct {
            struct ASTNode* init;
            struct ASTNode* condition;
            struct ASTNode* increment;
            struct ASTNode* body;
        } for_stmt;
        struct {
            char* var_name;
            struct ASTNode* iterable;
            struct ASTNode* body;
        } foreach_stmt;
        struct {
            struct ASTNode* value;
        } return_stmt;
        struct {
            struct ASTNode** values;
            int value_count;
            bool has_newline;
        } print_stmt;
        struct {
            struct ASTNode* left;
            KrtTokenType operator;
            struct ASTNode* right;
        } binary_op;
        struct {
            KrtTokenType operator;
            struct ASTNode* operand;
            int is_postfix;
        } unary_op;
        struct {
            struct ASTNode* condition;
            struct ASTNode* true_value;
            struct ASTNode* false_value;
        } ternary_op;
        struct {
            char* name;
            struct ASTNode** arguments;
            int argument_count;
            char** argument_names;
            struct ASTNode* object;
            char* resolved_class_name;
            char* resolved_mangled_name;
        } call;
        struct {
            struct ASTNode** statements;
            int statement_count;
        } block;
        struct {
            char* class_name;
            char* method_name;
            struct ASTNode** arguments;
            int argument_count;
            char* resolved_mangled_name;
        } static_call;
        struct {
            struct ASTNode* array;
            struct ASTNode* index;
        } array_access;

        struct {
            struct ASTNode** elements;
            int element_count;
        } array_literal;
        struct {
            KrtTokenType type_token;
            char* class_name;
            struct ASTNode** arguments;
            int argument_count;
            char** argument_names;
        } new_expr;
        struct {
            KrtTokenType type_token;
            char* element_type;
            struct ASTNode* size;
        } new_array_expr;
        struct {
            char* name;
            struct ASTNode* body;
        } namespace_decl;
        struct {
            char* name;
            struct ASTNode* body;
            struct ASTNode* base_class;
            char** template_params;
            int template_param_count;
            struct ASTNode** constraints;
            int constraint_count;
        } class_decl;
        struct {
            struct ASTNode* object;
            char* member_name;
            char* resolved_class_name;
            char* resolved_mangled_name;
        } member_access;
        struct {
            KrtTokenType access_modifier;
            struct ASTNode* member;
            int is_virtual;
        } access_modifier;
        struct {
            char** parameters;
            int parameter_count;
            KrtTokenType* parameter_types;
            struct ASTNode* body;
            struct ASTNode** base_arguments;
            int base_argument_count;
            int has_base_call;
        } constructor_decl;
        struct {
            char* class_name;
            struct ASTNode* body;
        } destructor_decl;

        struct {
            struct ASTNode* try_block;
            struct ASTNode** catch_clauses;
            int catch_clause_count;
            struct ASTNode* finally_clause;
        } try_stmt;
        struct {
            char* exception_type;
            char* exception_var;
            struct ASTNode* catch_block;
        } catch_clause;
        struct {
            struct ASTNode* finally_block;
        } finally_clause;
        struct {
            struct ASTNode* exception_expr;
            int is_rethrow;
        } throw_stmt;

        struct {
            struct ASTNode** parameters;
            int parameter_count;
            struct ASTNode* declaration;
            struct ASTNode** constraints;  
            int constraint_count;
        } template_decl;
        struct {
            char* param_name;
        } template_param;
        struct {
            char* name;
            char** type_args;
            int type_arg_count;
            struct ASTNode** args;
            int arg_count;
        } template_instantiation;
        struct {
            char* type_name;
        } generic_type;
        struct {
            char* param_name;           
            char* constraint_type;       
            struct ASTNode* interface_constraint; 
        } generic_constraint;

        struct {
            struct ASTNode* expression;
            struct ASTNode** cases;
            int case_count;
            struct ASTNode* default_case;
        } switch_stmt;
        struct {
            struct ASTNode* value;
            struct ASTNode** statements;
            int statement_count;
        } case_clause;
        struct {
            struct ASTNode** statements;
            int statement_count;
        } default_clause;
        struct {
            struct ASTNode* value;
            char* resolved_class_name;
        } break_stmt;
        struct {
            struct ASTNode* value;
        } continue_stmt;
        struct {
            struct ASTNode* value;
            char* resolved_class_name;
            int is_array;
        } delete_stmt;
        struct {
            struct ASTNode* resource;
            struct ASTNode* body;
        } using_stmt;
        struct {
            char* alias;  
            char** namespace_path;  
            int path_length;  
            int is_alias;  
        } using_directive;
        struct {
            char* namespace_name;
        } namespace_import;
        struct {
            char** parts;  
            int part_count;  
        } qualified_name;
        
        struct {
            char* name;
            KrtTokenType type;
            struct ASTNode* getter;
            struct ASTNode* setter;
            struct ASTNode* initial_value;
            struct ASTNode** attributes;
            int attribute_count;
            char* backing_field_name;
            int is_auto_property;
        } property_decl;
        struct {
            struct ASTNode* body;
            int is_auto;
        } property_getter;
        struct {
            char* value_param_name;  
            struct ASTNode* body;
            int is_auto;
        } property_setter;
        struct {
            char** parameters;
            int parameter_count;
            struct ASTNode* body;
            struct ASTNode* expression;  
        } lambda_expr;
        struct {
            struct ASTNode* from_clause;
            struct ASTNode** clauses;
            int clause_count;
            struct ASTNode* select_clause;
        } linq_query;
        struct {
            char* var_name;
            struct ASTNode* source;
            struct ASTNode* type;  
        } linq_from;
        struct {
            struct ASTNode* condition;
        } linq_where;
        struct {
            struct ASTNode* expression;
            struct ASTNode* key_selector;  
            bool ascending;
        } linq_select;
        struct {
            struct ASTNode* expression;
            bool ascending;
        } linq_orderby;
        struct {
            char* var_name;
            struct ASTNode* source;
            char* join_var_name;
            struct ASTNode* join_source;
            struct ASTNode* left_key;
            struct ASTNode* right_key;
            char* into_var_name;  
        } linq_join;
        struct {
            char* var_name;
            struct ASTNode* expression;
        } linq_let;
        struct {
            struct ASTNode* key_expression;
            struct ASTNode* element_expression;
            char* into_var_name;
        } linq_group;
        struct {
            char* name;
            struct ASTNode** arguments;
            int argument_count;
            struct ASTNode* named_arguments;  
        } attribute;
        struct {
            struct ASTNode** attributes;
            int attribute_count;
            struct ASTNode* target;  
        } attribute_list;
        struct {
            struct ASTNode* expression; 
            char** permissions;       
            int permission_count;
            int is_block;             
        } unsafe_call;
        struct {
            struct ASTNode* body;
        } point_block;
        struct {
            char* type_name;
            struct ASTNode* type_expr;
        } default_expr;
        struct {
            struct ASTNode* expression;
            char* type_name;
            struct ASTNode* type_expr;
        } is_expr;
        struct {
            struct ASTNode* expression;
            char* type_name;
            struct ASTNode* type_expr;
        } as_expr;
        struct {
            KrtTokenType type_token;
            char* type_name;
        } sizeof_expr;
        struct {
            KrtTokenType type_token;
            char* type_name;
            struct ASTNode* count_expr;
        } stackalloc_expr;
        struct {
            char** string_parts;
            int part_count;
            struct ASTNode** expressions;
            int expression_count;
        } interpolated_string;
        struct {
            KrtTokenType* element_types;
            int element_count;
        } tuple_type;
        struct {
            struct ASTNode** elements;
            int element_count;
        } tuple_expr;
        struct {
            struct ASTNode* tuple;
            int index;
        } tuple_element_access;
        struct {
            struct ASTNode* expression;
            struct ASTNode** cases;
            int case_count;
        } match_expr;
        struct {
            struct ASTNode* pattern;
            struct ASTNode* when_clause;
            struct ASTNode* body;
        } pattern_case;
        struct {
        } pattern_wildcard;
        struct {
            struct ASTNode* value;
        } pattern_literal;
        struct {
            char* name;
        } pattern_variable;
        struct {
            struct ASTNode** elements;
            int element_count;
        } pattern_tuple;
        struct {
            KrtTokenType base_type;
            int pointer_level;
        } pointer_type;
        struct {
            struct ASTNode* pointer;
        } pointer_deref;
        struct {
            struct ASTNode* operand;
        } address_of;
        struct {
            char* variable_name;
            struct ASTNode* expression;
            struct ASTNode* body;
        } fixed_statement;
        struct {
            struct ASTNode* value;
        } yield_return;
        struct {
            struct ASTNode* expression;
        } await_expr;
        struct {
            struct ASTNode* lock_object;
            struct ASTNode* body;
        } lock_stmt;
        struct {
            KrtTokenType operator_type;
            char* operator_name;
            struct ASTNode* body;
        } operator_overload;
        struct {
            char* name;
            KrtTokenType return_type;
            char** parameters;
            KrtTokenType* parameter_types;
            int parameter_count;
        } delegate_decl;
        struct {
            KrtTokenType base_type;
            struct ASTNode* type_expr;
        } nullable_type;
        struct {
            struct ASTNode* left;
            struct ASTNode* right;
        } null_coalescing;
        struct {
            struct ASTNode* expression;
            char* member_name;
            struct ASTNode* arguments;
            int is_method_call;
        } null_conditional;
        struct {
            KrtTokenType target_type;
            struct ASTNode* expression;
        } cast_expr;
    } data;
    int is_arena_allocated;  
} ASTNode;

ASTNode* ast_create_node(ASTNodeType type, int line, int col);
ASTNode* ast_create_node_arena(ASTNodeType type, int line, int col, KrtArena* arena);
void ast_destroy_node(ASTNode* node);
void ast_print(ASTNode* node, int indent);

#endif
