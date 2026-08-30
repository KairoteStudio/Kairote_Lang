# Kairote 开发标准文档

## 0. 项目架构概述

Kairote编译器采用经典的三段式架构设计：

### 0.1 编译器架构层次
```
源代码 (.krt) 
    ↓
前端 (Frontend) - 词法分析、语法分析、语义分析
    ↓
中端 (Middle) - 中间表示(IR)生成与优化
    ↓
后端 (Backend) - 目标代码生成 (当前支持x86)
    ↓
机器码 / 可执行文件
```

### 0.2 核心模块划分
- **compiler/frontend/**: 前端处理模块
  - `Lexer/`: 词法分析器 (Tokenizer.c)
  - `Parser/`: 语法分析器 (Parser.c, Ast.c)
  - `Semantic/`: 语义分析器 (SemanticAnalyzer.c, SymbolTable.c, Generics.c)
- **compiler/middle/**: 中端处理模块
  - `ir/`: 中间表示定义与基础操作 (ir.c, ir_optimizer.c)
  - `codegen/`: IR生成与优化 (ir_gen.c, optimizer.c)
- **compiler/backend/**: 后端处理模块
  - `x86/`: x86架构代码生成 (x86_codegen.c)
- **compiler/driver/**: 驱动与项目管理
  - `compiler.c`: 主编译器驱动
  - `project.c`: 项目管理
  - `preprocessor.c`: 预处理
  - `parallel_compiler.c`: 并行编译支持
- **compiler/pipeline/**: 编译流程协调
  - `compiler_pipeline.c`: 编译流程控制
- **core/**: 基础工具库
  - `memory/`: 内存管理 (allocator.c, smart_ptr.c, leak_detector.c)
  - `utils/`: 工具函数 (error.c, logger.c, string.c, path.c)
  - `platform/`: 平台抽象 (thread_pool.c)
- **runtime/**: 运行时支持
  - `runtime.c`: 运行时库
  - `interpreter.c`: 简单解释器（后期将被AOT替代）

### 0.3 当前开发重点
- **Kro目标文件后端**

## 1. 代码风格规范

### 1.1 基本格式
- **缩进**: 使用4个空格进行缩进，禁止使用制表符(Tab)
- **括号风格**: 严格遵循K&R风格
  - 函数左大括号放在行末
  - 控制语句左大括号放在行末
  - 右大括号独占一行，除非后面紧跟else/while等

```c
// 正确的K&R风格
int function_name(int param) {
    if (condition) {
        do_something();
    } else {
        do_something_else();
    }
    return 0;
}

// 错误的风格
int function_name(int param) 
{
    if (condition) 
    {
        do_something();
    }
    else 
    {
        do_something_else();
    }
    return 0;
}
```

### 1.2 命名规范

#### 1.2.1 公共接口命名（对外暴露的 API）
- **函数命名**: 使用大驼峰命名法 (PascalCase)，以 `Krt` 为前缀
  - 正确: `KrtParseExpression()`, `KrtCompileSource()`
  - 错误: `parse_expression()`, `krt_parse_expression()`
- **类型命名**: 使用大驼峰命名法，以 `Krt` 为前缀
  - 正确: `KrtAstNode`, `KrtTokenType`
  - 错误: `ast_node`, `krt_ast_node_t`

#### 1.2.2 内部实现命名（模块内部使用）
- **函数命名**: 使用下划线分隔的小写字母
  - 正确: `parser_parse_expression()`, `semantic_analyze()`
  - 错误: `parserParseExpression()`, `ParserParseExpression()`
- **变量命名**: 使用下划线分隔的小写字母
  - 正确: `token_type`, `current_position`
  - 错误: `tokenType`, `currentPosition`
- **类型命名**: 使用大驼峰命名法 (PascalCase)
  - 正确: `KrtAstNode`, `KrtToken`
  - 错误: `ast_node_t`, `token_t`

#### 1.2.3 宏命名
- **宏命名**: 使用全大写字母和下划线，以 `KRT_` 为前缀
  - 正确: `KRT_MAX_TOKEN_LENGTH`, `KRT_TOKEN_TYPE_KEYWORD`
  - 错误: `MAX_TOKEN_LENGTH`, `krt_max_token_length`

#### 1.2.4 文件和文件夹命名（PascalCase 强制要求）
- **文件夹命名**: **强制使用 PascalCase 命名法**，所有单词首字母大写
  - 正确: `Frontend/`, `Parser/`, `SemanticAnalyzer/`, `CodeGen/`
  - 错误: `frontend/`, `parser/`, `semantic_analyzer/`, `codegen/`
  - 错误: `FRONTEND/`, `PARSER/` (全大写)
- **源文件命名**: **强制使用 PascalCase 命名法**，所有单词首字母大写
  - 正确: `Parser.c`, `Lexer.c`, `AstNode.c`, `SemanticAnalyzer.c`, `IrGenerator.c`
  - 错误: `parser.c`, `lexer.c` (示例仅供参考，当前代码库使用 PascalCase)
- **头文件命名**: **强制使用 PascalCase 命名法**，与对应源文件保持一致
  - 正确: `Parser.h`, `Lexer.h`, `AstNode.h`, `SemanticAnalyzer.h`
  - 错误: `parser.h`, `lexer.h`, `ast_node.h`, `semantic_analyzer.h`
- **特殊文件命名**: 配置文件、文档等也遵循 PascalCase
  - 正确: `CMakeLists.txt`, `ReadMe.md`, `DevStand.md`, `Version.json`
  - 错误: `cmakelists.txt`, `readme.md`, `devstand.md`, `version.json`
- **复合词处理**: 缩略词整体视为一个单词或每个字母都大写
  - 正确: `XmlParser.c`, `HtmlRenderer.c`, `IoManager.c`, `KrtCompiler.c`
  - 推荐: `JSONParser.c`, `HTMLRenderer.c`, `IOManager.c`
- **禁止使用的命名方式**:
  - ❌ 禁止使用 snake_case (下划线分隔): `my_file.c`
  - ❌ 禁止使用 kebab-case (连字符分隔): `my-file.c`
  - ❌ 禁止使用全小写: `myfile.c`
  - ❌ 禁止使用全大写 (缩略词除外): `MYFILE.C`
- **迁移计划**: 现有不符合规范的文件应在重构时逐步重命名为 PascalCase

### 1.3 注释规范
- **最小化注释**: 代码应该自解释，只在必要时添加注释（如：复杂算法添加注释等）
- **函数注释**: 只在复杂算法或关键函数前添加简短注释
- **行注释**: 避免行末注释，除非绝对必要
- **临时注释**: 调试用的临时注释在提交前必须删除

### 1.4 代码组织
- **函数长度**: 单个函数不超过500行，超过的需要在提交时报备
- **文件长度**: 单个文件不超过5000行
- **头文件**: 必须包含防止重复包含的宏
- **源文件**: 按照功能模块组织，相关函数放在一起

## 2. 文件组织规范

### 2.1 测试文件规范
- **位置**: 所有测试文件必须放在 `Test/` 目录下
- **命名**: 使用 `test_` 前缀，描述性名称
  - 正确: `Test/test_console_simple.krt`
  - 错误: `test_console_simple.krt` (放在根目录)
- **分类**: 按功能分类放置
  - 语法测试: `Test/syntax/`
  - 功能测试: `Test/features/`
  - 集成测试: `Test/integration/`

### 2.2 禁止行为
- 禁止在根目录创建测试文件
- 禁止在src目录中创建测试文件
- 禁止将临时文件提交到版本库

## 3. 编译器开发规范

### 3.1 语法扩展原则
- **向后兼容**: 新语法必须保持对旧语法的兼容
- **渐进式**: 优先实现最常用的C#语法特性
- **一致性**: 新语法必须与现有语法风格保持一致

### 3.2 错误处理规范

#### 3.2.1 错误码定义
```c
// 运行时错误码 (src/runtime/runtime.h)
typedef enum {
    KRT_ERR_NONE = 0,
    KRT_ERR_OUT_OF_MEMORY,
    KRT_ERR_INVALID_ARGUMENT,
    KRT_ERR_FILE_NOT_FOUND,
    KRT_ERR_PERMISSION_DENIED,
    KRT_ERR_IO_ERROR,
    KRT_ERR_NETWORK_ERROR,
    KRT_ERR_INDEX_OUT_OF_BOUNDS,
    KRT_ERR_DIVISION_BY_ZERO,
    KRT_ERR_NULL_POINTER,
    KRT_ERR_BUFFER_OVERFLOW,
    KRT_ERR_UNKNOWN
} KrtErrorCode;
```

#### 3.2.2 编译时错误处理
- **错误位置**: 必须包含文件名、行号、列号
- **错误级别**: 分为ERROR、WARNING、NOTE三级
- **错误恢复**: 语法错误后尝试跳过到下一个语句边界
- **错误累积**: 收集多个错误后统一报告，不中断编译流程

#### 3.2.3 错误信息格式
Kairote编译器使用自定义的错误输出格式，不是传统的C语言编译器格式：

**基本错误格式**:
```
[KrtError] 错误描述信息
```

**带位置的错误格式**:
```
[KrtError] 文件名:行号: 错误描述信息
```

**编译错误格式**（致命错误，会终止编译）:
```
[KRT_COMPILE_ERROR] 错误描述信息
```

**带位置的编译错误格式**:
```
[KRT_COMPILE_ERROR] 文件名:行号: 错误描述信息
```

**警告信息格式**:
```
[KRT_WARNING] 警告描述信息
[KRT_WARNING] 文件名:行号: 警告描述信息
```

#### 3.2.4 错误报告宏使用

错误报告宏定义在 `src/core/utils/krt_common.h` 中，使用方法如下：

```c
// 基础错误报告
KrtError("错误描述信息");
KrtError("变量 %s 未定义", var_name);

// 带位置的错误报告
KrtError_LOC(file, line, "语法错误: 意外的token");

// 编译错误（致命错误，会终止程序）
KRT_COMPILE_ERROR("无法打开文件: %s", filename);
KRT_COMPILE_ERROR_LOC(file, line, "内部编译器错误");

// 警告信息
KRT_WARNING("未使用的变量: %s", var_name);
KRT_WARNING_LOC(file, line, "隐式类型转换");
```

#### 3.2.5 运行时错误处理
```c
// 运行时错误码 (src/runtime/runtime.h)
typedef enum {
    KRT_ERR_NONE = 0,
    KRT_ERR_OUT_OF_MEMORY,
    KRT_ERR_INVALID_ARGUMENT,
    KRT_ERR_FILE_NOT_FOUND,
    KRT_ERR_PERMISSION_DENIED,
    KRT_ERR_IO_ERROR,
    KRT_ERR_NETWORK_ERROR,
    KRT_ERR_INDEX_OUT_OF_BOUNDS,
    KRT_ERR_DIVISION_BY_ZERO,
    KRT_ERR_NULL_POINTER,
    KRT_ERR_BUFFER_OVERFLOW,
    KRT_ERR_UNKNOWN
} KrtErrorCode;

// 运行时错误函数
void KrtError(KrtErrorCode code);
const char* KrtStrerror(KrtErrorCode code);
```

#### 3.2.6 内存错误处理
- **分配失败**: 所有内存分配必须检查返回值
- **内存泄漏**: 使用泄漏检测器 (src/core/memory/leak_detector.c)
- **双重释放**: 使用智能指针防止 (src/core/memory/smart_ptr.c)

#### 3.2.7 错误处理最佳实践
1. **编译期错误**: 使用 `KrtError` 或 `KrtError_LOC` 报告非致命错误
2. **致命错误**: 使用 `KRT_COMPILE_ERROR` 或 `KRT_COMPILE_ERROR_LOC` 报告致命错误并终止编译
3. **警告信息**: 使用 `KRT_WARNING` 或 `KRT_WARNING_LOC` 报告警告信息
4. **运行时错误**: 使用 `KrtError()` 设置运行时错误码，用 `KrtStrerror()` 获取错误描述

### 3.3 AST和中间表示规范

#### 3.3.1 AST节点定义
```c
// AST节点类型枚举 (src/compiler/frontend/parser/ast.h)
typedef enum {
    KRT_AST_PROGRAM,
    KRT_AST_FUNCTION_DECLARATION,
    KRT_AST_VARIABLE_DECLARATION,
    KRT_AST_ASSIGNMENT,
    KRT_AST_BINARY_OPERATION,
    KRT_AST_UNARY_OPERATION,
    KRT_AST_IF_STATEMENT,
    KRT_AST_WHILE_STATEMENT,
    KRT_AST_FOR_STATEMENT,
    KRT_AST_RETURN_STATEMENT,
    KRT_AST_CALL,
    KRT_AST_IDENTIFIER,
    KRT_AST_NUMBER,
    KRT_AST_STRING,
    // ... 更多节点类型
} KrtAstNodeType;
```

#### 3.3.2 AST节点结构
```c
typedef struct KrtAstNode {
    KrtAstNodeType type;
    int line;
    int column;
    union {
        // 根据节点类型存储具体数据
        struct { /* 函数声明数据 */ } function;
        struct { /* 二元运算数据 */ } binary_op;
        // ... 其他节点数据
    } data;
    struct KrtAstNode* parent;
    struct KrtAstNode** children;
    size_t child_count;
} KrtAstNode;
```

#### 3.3.3 中间表示(IR)规范
```c
// IR指令类型 (src/compiler/middle/ir/ir.h)
typedef enum {
    // 内存操作
    KRT_IR_LOAD,
    KRT_IR_STORE,
    KRT_IR_ALLOC,
    
    // 算术运算
    KRT_IR_ADD,
    KRT_IR_SUB,
    KRT_IR_MUL,
    KRT_IR_DIV,
    
    // 控制流
    KRT_IR_JUMP,
    KRT_IR_BRANCH,
    KRT_IR_RETURN,
    
    // 函数调用
    KRT_IR_CALL,
    KRT_IR_PARAM,
    
    // ... 更多IR指令
} KrtIROpcode;
```

#### 3.3.4 IR值类型
```c
typedef enum {
    KRT_IR_VALUE_VOID,
    KRT_IR_VALUE_IMM,          // 立即数
    KRT_IR_VALUE_VAR,          // 变量
    KRT_IR_VALUE_TEMP,         // 临时变量
    KRT_IR_VALUE_ARG,          // 函数参数
    KRT_IR_VALUE_STRING_CONST, // 字符串常量
} KrtIRValueType;
```

#### 3.3.5 IR优化规则
- **常量折叠**: 编译期计算常量表达式
- **死代码消除**: 移除不可达代码
- **公共子表达式消除**: 避免重复计算
- **循环优化**: 循环不变量外提等

### 3.4 内存管理规范
- **统一分配**: 使用统一的内存分配器 (src/core/memory/allocator.c)
- **智能指针**: 使用智能指针防止内存泄漏 (src/core/memory/smart_ptr.c)
- **泄漏检测**: 启用内存泄漏检测 (src/core/memory/leak_detector.c)
- **错误检查**: 内存分配失败必须检查并处理
- **及时释放**: 及时释放不再使用的内存，避免内存碎片

## 4. 测试和调试规范

### 4.1 测试流程
1. 编写测试文件到 `Test/` 目录
2. 运行编译器进行测试
3. 验证输出结果
4. 检查错误信息质量
5. 确保不引入回归问题

### 4.2 调试规范
- **调试输出**: 使用专门的调试输出函数
- **断言**: 使用断言检查内部一致性
- **日志级别**: 区分不同级别的调试信息

### 4.3 性能考虑
- **编译速度**: 优化编译速度，避免不必要的重复工作
- **内存使用**: 控制内存使用量
- **代码生成**: 生成高效的机器码

### 4.4 编译器本体构建
- **编译命令**: `cd Re.KrtC && zig build`
- **依赖库**: 无外部依赖，仅依赖标准库

## 5. 提交规范

### 5.1 提交前检查
- [ ] 代码符合K&R风格
- [ ] 测试文件在正确位置
- [ ] 没有临时文件和调试输出
- [ ] 编译器本身能正常编译
- [ ] 基本功能测试通过

### 5.2 提交信息
- 使用简洁明了的提交信息
- 说明修改的主要内容和原因
- 如果修复了特定问题，说明问题症状

## 6. 常见错误提醒

### 6.1 风格错误
- 不要在函数名中使用驼峰命名
- 不要在行末添加多余空格
- 不要混用空格和制表符

### 6.2 组织错误
- 测试文件必须放在Test目录
- 不要在根目录创建临时文件
- 及时清理调试用的临时文件

### 6.3 语法实现错误
- 保持语法的一致性
- 确保新语法不会破坏现有功能
- 正确处理边界情况

## 7. 代码审查规范

### 7.1 审查流程
1. **创建PR**: 在GitHub创建Pull Request
2. **自我审查**: 提交前进行自我代码审查
3. **同行审查**: 至少1名其他开发者审查
4. **自动化检查**: CI/CD自动运行代码检查和测试
5. **合并**: 审查通过后由维护者合并

### 7.2 审查要点
- [ ] **代码风格**: 符合K&R风格和命名规范
- [ ] **架构设计**: 符合编译器架构层次
- [ ] **错误处理**: 完善的错误检查和报告
- [ ] **内存管理**: 无内存泄漏，使用智能指针
- [ ] **测试覆盖**: 新增功能有对应测试用例
- [ ] **性能影响**: 不引入明显的性能退化
- [ ] **文档更新**: 相关文档已同步更新

### 7.3 审查标准
- **必须修复**: 内存泄漏、崩溃、安全漏洞
- **建议修复**: 代码风格、性能优化、可读性改进
- **可选修复**: 重构建议、未来改进方向

## 8. 文档规范

### 8.1 代码文档
- **API文档**: 公共接口必须有文档注释
- **实现文档**: 复杂算法需要实现说明
- **架构文档**: 重要模块需要架构设计文档

### 8.2 文档格式
```c
/**
 * @brief 函数简要说明
 * @param param1 参数1说明
 * @param param2 参数2说明
 * @return 返回值说明
 * @note 注意事项
 * @warning 警告信息
 */
int function_name(int param1, const char* param2);
```

### 8.3 文档位置
- **API文档**: 头文件中，接口声明上方
- **实现文档**: 源文件中，复杂函数上方
- **架构文档**: `docs/DEV-DOC/` 目录下

## 9. 性能基准

### 9.1 编译性能目标
- **编译速度**: 1000行/秒以上（中等复杂度代码）
- **内存使用**: 编译过程中峰值内存 < 500MB
- **代码生成**: 生成代码性能达到GCC -O1水平

### 9.2 优化级别
- **O0**: 无优化，最快编译速度，用于调试
- **O1**: 基础优化，平衡编译速度和运行性能
- **O2**: 标准优化，重点优化运行性能
- **O3**: 激进优化，可能增加编译时间

### 9.3 性能测试
- **基准测试**: 使用标准测试集定期测试
- **回归测试**: 确保优化不引入性能退化
- **对比测试**: 与GCC/Clang生成的代码性能对比

## 10. 开发流程

1. **需求分析**: 明确要实现的功能和语法特性
2. **架构设计**: 设计对应的AST节点和IR指令
3. **接口定义**: 定义模块间的接口和数据结构
4. **实现**: 按照规范编写代码，遵循单一职责原则
5. **单元测试**: 编写针对性的单元测试
6. **集成测试**: 确保与现有功能兼容
7. **性能测试**: 验证性能符合基准要求
8. **代码审查**: 提交PR进行同行审查
9. **文档更新**: 更新相关文档和注释
10. **合并发布**: 审查通过后合并到主分支

---

**注意**: 本文档会根据项目发展持续更新，请定期查看最新版本。
**当前版本**: v1.2.1 (2026年更新，更新小细节问题)
