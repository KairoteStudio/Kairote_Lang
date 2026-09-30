# Kairote Lang 开发者指南

## 目录

1. [概述](#概述)
2. [编译器架构](#编译器架构)
3. [构建系统](#构建系统)
4. [开发环境设置](#开发环境设置)
5. [代码结构](#代码结构)
6. [编译流程](#编译流程)
7. [扩展和定制](#扩展和定制)
8. [调试和测试](#调试和测试)
9. [贡献指南](#贡献指南)

## 概述

Kairote Lang 是一个现代的、高性能的编程语言，本指南面向希望了解 Kairote Lang 编译器内部工作原理、参与开发或扩展功能的开发者。

### 主要组件

- **前端**：词法分析、语法分析、语义分析
- **中端**：IR（中间表示）生成和优化
- **后端**：代码生成（x86汇编、字节码等）
- **运行时**：内存管理、系统调用、标准库

## 编译器架构

### 整体架构图

```
源代码 → 词法分析器 → 语法分析器 → 语义分析器 → IR生成器 → IR优化器 → 代码生成器 → 目标代码
```

### 前端组件

#### 词法分析器 (Lexer)

位置：`src/compiler/frontend/lexer/`

主要文件：
- `tokenizer.c` - 词法分析器实现
- `tokenizer.h` - 词法分析器接口

功能：
- 将源代码转换为标记（Token）流
- 处理关键字、标识符、字面量、运算符等

#### 语法分析器 (Parser)

位置：`src/compiler/frontend/parser/`

主要文件：
- `parser.c` - 语法分析器实现
- `ast.c` - AST节点操作
- `ast.h` - AST节点定义

功能：
- 将标记流转换为抽象语法树（AST）
- 处理表达式、语句、声明等语法结构

#### 语义分析器 (Semantic Analyzer)

位置：`src/compiler/frontend/semantic/`

主要文件：
- `semantic_analyzer.c` - 语义分析器实现
- `symbol_table.c` - 符号表管理
- `generics.c` - 泛型处理

功能：
- 类型检查
- 作用域分析
- 符号解析
- 错误检测

### 中端组件

#### IR生成器

位置：`src/compiler/middle/codegen/`

主要文件：
- `ir_gen.c` - IR生成实现
- `type_checker.c` - 类型检查器

功能：
- 将AST转换为中间表示（IR）
- 类型检查和转换
- 生成类型信息

#### IR优化器

位置：`src/compiler/middle/ir/`

主要文件：
- `ir.c` - IR操作
- `ir_optimizer.c` - IR优化实现
- `ir_definitions.h` - IR定义

功能：
- 常量折叠
- 死代码消除
- 循环优化
- 内联优化

### 后端组件

#### x86代码生成器

位置：`src/compiler/backend/x86/`

主要文件：
- `x86_codegen.c` - x86代码生成
- `x86_codeopt.c` - x86代码优化

功能：
- 将IR转换为x86汇编代码
- 寄存器分配
- 指令选择

#### 字节码生成器

位置：`src/compiler/backend/vm/`

主要文件：
- `vm_codegen.c` - 字节码生成
- `vm.h` - 虚拟机定义

功能：
- 将IR转换为虚拟机字节码
- 生成字节码常量池

### 运行时系统

位置：`src/runtime/`

主要文件：
- `runtime.c` - 运行时函数实现
- `runtime.h` - 运行时接口

功能：
- 内存管理
- 系统调用封装
- 标准库函数

## 构建系统

### 构建脚本

主构建工具：`zig`（作为 C 编译器和构建系统）

```bash
# 基本构建命令
cd Re.KrtC && zig build

# 发布构建
zig build -Doptimize=ReleaseSafe

# 运行编译器
zig build run --
```

## 开发环境设置

### 必需工具

1. **编译器**
   - Zig 0.17.0+

2. **调试工具**
   - LLVM/GDB

## 代码结构

### 命名约定
见../../../DevStand.md

## 编译流程

### 编译阶段详解

#### 1. 词法分析

输入：源代码字符串
输出：Token流

```c
// 创建词法分析器
Lexer* lexer = lexer_create(source_code);

// 获取下一个Token
Token token = lexer_next_token(lexer);

// 销毁词法分析器
lexer_destroy(lexer);
```

#### 2. 语法分析

输入：Token流
输出：AST

```c
// 创建语法分析器
Parser* parser = parser_create(lexer);

// 解析源代码
ASTNode* ast = parser_parse(parser);

// 销毁语法分析器
parser_destroy(parser);
```

#### 3. 语义分析

输入：AST
输出：带类型信息的AST

```c
// 创建语义分析器
SemanticAnalyzer* analyzer = semantic_analyzer_create();

// 执行语义分析
semantic_analyzer_analyze(analyzer, ast);

// 销毁语义分析器
semantic_analyzer_destroy(analyzer);
```

#### 4. IR生成

输入：带类型信息的AST
输出：IR

```c
// 创建IR构建器
KrtIRBuilder* ir_builder = KrtIrBuilderCreate();

// 生成IR
KrtIrGenerateFromAst(ir_builder, ast, type_context);

// 销毁IR构建器
KrtIrBuilderDestroy(ir_builder);
```

#### 5. 代码生成

输入：IR
输出：目标代码

```c
// 生成x86汇编
KrtX86Generate(output_file, ir_builder->module);

// 或生成字节码
KrtVmCodegenGenerate(ir_builder->module, &chunk);
```

### 编译器驱动

位置：`src/compiler/driver/compiler.c`

主要函数：
- `KrtCompilerCreate` - 创建编译器实例
- `KrtCompilerCompile` - 执行编译
- `KrtCompilerDestroy` - 销毁编译器实例

```c
// 创建编译器
KrtCompiler* compiler = KrtCompilerCreate("output.asm", KRT_TARGET_X86_ASM);

// 编译AST
KrtCompilerCompile(compiler, ast, type_context);

// 销毁编译器
KrtCompilerDestroy(compiler);
```

## 扩展和定制

### 添加新的语言特性

#### 1. 扩展词法分析器

在 `src/compiler/frontend/lexer/tokenizer.h` 中添加新的Token类型：

```c
typedef enum {
    // 现有Token...
    TOKEN_NEW_FEATURE,  // 新特性Token
    // ...
} KrtTokenType;
```

在 `src/compiler/frontend/lexer/tokenizer.c` 中实现词法规则：

```c
case 'n':
    if (strncmp(source + position, "new_feature", 11) == 0) {
        return create_token(TOKEN_NEW_FEATURE);
    }
    break;
```

#### 2. 扩展语法分析器

在 `src/compiler/frontend/parser/ast.h` 中添加新的AST节点类型：

```c
typedef enum {
    // 现有AST节点...
    AST_NEW_FEATURE,  // 新特性AST节点
    // ...
} ASTNodeType;
```

在 `src/compiler/frontend/parser/parser.c` 中添加解析逻辑：

```c
static ASTNode* parse_new_feature(Parser* parser) {
    ASTNode* node = ast_create_node(AST_NEW_FEATURE);
    // 解析新特性的语法结构
    return node;
}
```

#### 3. 扩展语义分析器

在 `src/compiler/frontend/semantic/semantic_analyzer.c` 中添加语义检查：

```c
static void analyze_new_feature(SemanticAnalyzer* analyzer, ASTNode* node) {
    // 执行类型检查
    // 执行作用域分析
    // 执行错误检查
}
```

#### 4. 扩展代码生成器

在 `src/compiler/backend/x86/x86_codegen.c` 中添加代码生成逻辑：

```c
static void generate_new_feature(X86CodeGenerator* generator, ASTNode* node) {
    // 生成新特性的机器码
}
```

### 添加新的目标平台

#### 1. 创建后端目录

```
src/compiler/backend/new_platform/
├── new_platform_codegen.c
├── new_platform_codegen.h
├── new_platform_codeopt.c
└── new_platform_codeopt.h
```

#### 2. 实现代码生成器

```c
// new_platform_codegen.h
typedef struct {
    // 平台特定的代码生成器状态
} NewPlatformCodeGenerator;

// new_platform_codegen.c
void new_platform_generate(FILE* output, KrtIRModule* module) {
    // 实现平台特定的代码生成
}
```

#### 3. 集成到编译器

在 `src/compiler/driver/compiler.c` 中添加新的目标平台：

```c
case KRT_TARGET_NEW_PLATFORM:
    new_platform_generate(compiler->output_file, ir_builder->module);
    break;
```

### 添加新的优化

#### 1. 创建优化器

在 `src/compiler/middle/ir/ir_optimizer.c` 中添加新的优化函数：

```c
static void optimize_new_feature(IROptimizer* optimizer, KrtIRModule* module) {
    // 实现新的优化算法
}
```

#### 2. 注册优化

在优化器初始化函数中注册新的优化：

```c
IROptimizer* ir_optimizer_create(void) {
    IROptimizer* optimizer = KRT_MALLOC(sizeof(IROptimizer));
    // ...
    optimizer->optimize_new_feature = optimize_new_feature;
    return optimizer;
}
```

## 测试

#### 1. 测试

请根据Test/Readme.md引导进行测试

## 贡献指南

### 代码提交规范

#### 1. 分支命名

- 功能分支：`feature/功能名称`
- 修复分支：`fix/问题描述`
- 优化分支：`optimize/优化内容`

#### 2. 提交信息

格式：
```
类型(范围): 简短描述

详细描述（可选）

相关问题：#issue编号
```

类型：
- `Feat`: 新功能
- `Fix`: 修复bug
- `Docs`: 文档更新
- `Style`: 代码格式调整
- `Refactor`: 代码重构
- `Test`: 测试相关
- `Chore`: 构建过程或辅助工具的变动

示例：
```
Feat(Lexer): 添加对字符串插值的支持

实现了词法分析器对字符串插值语法的支持，允许在字符串中嵌入表达式。

相关问题：#123
```

#### 3. 代码审查

所有代码更改都需要经过代码审查：

1. 创建Pull Request
2. 至少需要一位维护者审查
3. 通过所有自动化测试(或通过允许的不完全通过测试)
4. 解决审查意见

### 发布流程

#### 1. 版本号规范

遵循语义化版本控制（SemVer）：
- 主版本号：不兼容的API修改
- 次版本号：向下兼容的功能性新增
- 修订号：向下兼容的问题修正

#### 2. 发布检查清单

- [ ] 所有测试通过
- [ ] 文档更新
- [ ] 版本号更新
- [ ] 更新日志编写
- [ ] 性能测试通过
- [ ] 安全审查完成

--- 
