# Re.KrtC 开发者指南

本文说明 C 版编译器 `Re.KrtC` 的实际源码布局和常用修改路径。SelfHost 编译器使用另一套 Kairote 源码，见[对应文档](../../../../SelfHost/README.md)。

## 构建

仓库使用 CMake 构建 C 编译器和 ArkLink：

```sh
cmake -S . -B build
cmake --build build --target KrtC ArkLink -j4
```

`Re.KrtC/CMakeLists.txt` 从 `Re.KrtC/build.zig` 读取 C 源文件清单，以 C11 构建 `KrtC`。Zig 构建描述仍在仓库中，但上述工作流不要求先用 Zig 生成编译器。

## 源码路径

| 职责 | 主要位置 |
| --- | --- |
| 命令行和项目驱动 | `Re.KrtC/src/Main.c`、`Re.KrtC/src/Compiler/Driver/` |
| 词法分析 | `Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c` |
| 语法树和解析器 | `Re.KrtC/src/Compiler/Frontend/Parser/` |
| 名称、类型和指针语义 | `Re.KrtC/src/Compiler/Frontend/Semantic/` |
| AST 到 IR 的生成 | `Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c` |
| IR、内存操作和优化 | `Re.KrtC/src/Compiler/Middle/Ir/` |
| KRO 原生后端 | `Re.KrtC/src/Compiler/Backend/Kro/` |
| 运行时 Kairote 源码 | `Re.KrtC/src/Runtime/Krt/` |
| 独立库源码 | `libs/System/` |
| KRO 链接器 | `ArkLink/` |

实际编译流程由 `Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c` 连接各阶段。文件存在不等于某条语法或后端已经完整支持；应从可达入口和执行结果确认。

## 修改语言行为

数组访问的实现路径可作为典型例子：

1. `SemanticAnalyzer.c` 检查数组表达式和整数索引。
2. `IrGen.c` 识别托管数组，生成索引检查、地址计算及读写指令。
3. `Ir.c` 定义 IR 操作，KRO 后端负责把它们转换为本机代码。
4. `Test/SelfHost/test_seed_array_bounds.py` 对 C 编译器运行合法、越界和宽整数索引回归。

新增语法时，按解析器、AST、语义、IR 和后端的实际依赖逐层修改；只增加 Token 或 AST 枚举不能证明该语法已经可用。修改运行时或库 API 时，同时检查调用方、所有权约定和 `Test/StandardLibrary/` 的契约。

## 原生回归

C 编译器数组检查测试：

```sh
python3 -m unittest Test.SelfHost.test_seed_array_bounds
```

构建器也可通过原生自举验证 SelfHost；这与 Re.KrtC 的测试是不同验收项：

```sh
python3 Test/SelfHost/Bootstrap.py
```

VM 不属于本文推荐的编译器验证路径。

## 文档维护

`Docs/KrtC/Doc/` 描述 C 编译器；`Docs/SelfHost/` 描述自举编译器。修改时保持实现边界清楚，并使用 `Re.KrtC/src/`、`SelfHost/` 和 `libs/` 中的当前源码核实命令、类型和 API。
