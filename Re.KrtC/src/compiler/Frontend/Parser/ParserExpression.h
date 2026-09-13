#ifndef KRT_PARSER_EXPRESSION_H
#define KRT_PARSER_EXPRESSION_H

#include "Parser.h"

ASTNode* parser_parse_call(Parser* parser, ASTNode* callee);
ASTNode* parser_parse_primary(Parser* parser);
int get_operator_precedence(KrtTokenType type);
ASTNode* parser_parse_binary_operation(Parser* parser, int precedence);
ASTNode* parser_parse_postfix_expression(Parser* parser);
ASTNode* parser_parse_ternary_operation(Parser* parser);
ASTNode* parser_parse_expression(Parser* parser);

#endif
