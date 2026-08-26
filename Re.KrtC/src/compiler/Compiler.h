#ifndef KRT_COMPILER_H
#define KRT_COMPILER_H

#include "Frontend/Lexer/Tokenizer.h"
#include "Frontend/Parser/Parser.h"
#include "Frontend/Parser/Ast.h"
#include "Frontend/Semantic/SemanticAnalyzer.h"
#include "Frontend/Semantic/SymbolTable.h"
#include "Frontend/Semantic/Generics.h"
#include "Frontend/Semantic/NameMangling.h"
#include "Middle/Ir/Ir.h"
#include "Backend/X86/X86Codegen.h"
#include "Driver/Compiler.h"
#include "Driver/ParallelCompiler.h"
#include "Driver/Preprocessor.h"
#include "Driver/Project.h"

#endif