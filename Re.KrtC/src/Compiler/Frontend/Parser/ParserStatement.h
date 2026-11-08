#ifndef KRT_PARSER_STATEMENT_H
#define KRT_PARSER_STATEMENT_H

#include "Parser.h"

ASTNode* parser_parse_variable_declaration(Parser* parser);
ASTNode* parser_parse_assignment(Parser* parser);
ASTNode* parser_parse_print_statement(Parser* parser);
ASTNode* parser_parse_return_statement(Parser* parser);
ASTNode* parser_parse_if_statement(Parser* parser);
ASTNode* parser_parse_while_statement(Parser* parser);
ASTNode* parser_parse_for_statement(Parser* parser);
ASTNode* parser_parse_block(Parser* parser);
ASTNode* parser_parse_statement(Parser* parser);

#endif
