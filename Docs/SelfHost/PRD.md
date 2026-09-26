# Kairote 真正自举编译器 PRD

## 1. 背景与问题

当前仓库的 `Re.KrtC` 是用 C 实现的宿主编译器。此前的 Kairote→C 转译方案只能证明“现有编译器可以编译一个转译器”，不能证明 Kairote 编译器能够编译自身，也不能形成独立的 Kairote 工具链。

本项目要建立一条真正的自举链：第一代由现有 C 编译器提供种子，之后的编译器由 Kairote 编译器自身编译，并直接生成 Kro 对象文件，由 ArkLink 链接为可执行文件。自第二代开始，构建过程不得调用 C 编译器，也不得把 Kairote 源码转译为 C。

## 2. 产品目标

交付一个结构清晰、可测试、可重复构建的 Kairote 编译器，首个目标平台为 Linux x86-64，目标文件格式为 Kro。

编译器必须具备以下能力：

1. 读取一个或多个 `.krt` 源文件并建立模块单元。
2. 完成词法分析、语法分析、AST 构建、名称解析、类型检查和控制流检查。
3. 将经过检查的 AST 降低到稳定的、与前端解耦的 SSA 风格 IR。
4. 对 IR 执行确定性的基础优化，并保留 `-O0` 到 `-O3` 的行为契约。
5. 直接生成 Kro 对象文件，包括代码、只读数据、数据、BSS、符号和重定位信息。
6. 调用 ArkLink 完成链接，生成可执行文件。
7. 编译自身，并验证连续两代的编译器产物可重复、行为一致。

## 3. 非目标

第一阶段不实现以下内容：

- Windows、ARM64 等额外目标平台。
- 完整 C# 兼容性、宏系统、包管理器和 IDE 服务。
- 依赖垃圾回收的运行时。
- 将 C 编译器作为第二代或后续代的隐藏后端。

## 4. 自举定义与代际约束

自举分为三个阶段：

```text
Stage 0: 现有 C KrtC  →  Kairote 编译器 Stage 1
Stage 1: Kairote 编译器 → Kairote 编译器 Stage 2
Stage 2: Kairote 编译器 → Kairote 编译器 Stage 3
```

Stage 0 只用于生成种子编译器。Stage 1 及以后必须满足：

- 编译器源码由 Kairote 编写；
- 输出为 Kro 对象或最终可执行文件；
- 不产生 C 源码作为中间产物；
- 不启动 `cc`、`clang`、`gcc` 或其他外部编译器；
- 链接只通过 ArkLink 和仓库内运行时完成。

每一代编译器都必须能编译下一代源码。对于固定源码、固定目标和固定优化级别，生成的 Kro 文件应具有稳定哈希；若包含时间戳或路径，必须从产物中移除或规范化。

## 5. 目标架构

### 5.1 Driver 层

负责命令行、项目文件、输入文件集合、目标平台、优化级别、诊断输出和构建缓存。Driver 不参与语义判断和机器码生成，只编排各阶段。

建议接口：

```text
KrtCompileProject(Project, CompileOptions) -> CompileResult
KrtCompileModule(Source, ModuleOptions) -> ModuleArtifact
KrtLink(Objects, LinkOptions) -> Executable
```

### 5.2 Frontend 层

目录划分：

```text
Frontend/Lexer
Frontend/Parser
Frontend/Ast
Frontend/Semantic
Frontend/Diagnostics
```

Lexer 只负责 Token 和源码位置；Parser 只负责 AST；Semantic 负责符号表、类型推导、名称解析、可变性、返回路径和控制流检查。各阶段不得直接调用 Kro writer。

### 5.3 Middle 层

IR 必须拥有明确的数据模型：模块、函数、基本块、值、类型、指令和源位置。第一版 IR 至少支持：

- 整数和布尔值；
- 栈槽、加载、存储；
- 算术、比较、位运算；
- 条件跳转、无条件跳转和返回；
- 函数调用；
- 全局数据和字符串常量。

IR 验证器在每次优化前后运行，检查 SSA 定义唯一性、基本块终结指令、类型匹配和支配关系。优化必须是可关闭、确定性且可单独测试的。

### 5.4 Backend/Kro 层

后端只消费验证过的 IR，并通过 `KroWriter` 写出 Kro 对象。后端负责：

- 调用约定和寄存器分配；
- 栈帧布局；
- x86-64 指令编码；
- 本地符号、外部符号和重定位；
- 入口点生成。

后端不得重新解析源代码或依赖 AST。Kro 文件格式以 `Re.KrtC/src/Tools/KroWriter.h` 的定义为契约，必要的格式扩展必须同步 ArkLink loader 和测试。

### 5.5 Runtime 层

运行时提供最小系统调用、内存、字符串和进程入口支持。运行时 API 必须有稳定的 ABI 声明；编译器本体不得把宿主 C 函数当作隐式内建函数使用。

## 6. 源码布局

目标布局如下：

```text
SelfHost/
├── Driver/
├── Frontend/
│   ├── Lexer/
│   ├── Parser/
│   └── Semantic/
├── Middle/
│   ├── Ir/
│   └── Optimize/
├── Backend/
│   └── Kro/
├── Runtime/
└── Main.krt
```

模块间只通过显式类型和接口通信。禁止重新引入单文件转译器、字符串拼接式代码生成器或依赖 C 语法作为后端协议。

## 7. 分阶段交付

### M0：格式和接口冻结

- 冻结 Token、AST、IR、Kro writer 的最小接口。
- 增加 Kro 对象结构测试和 ArkLink 端到端测试。
- 定义编译错误格式和稳定哈希规则。

### M1：Kairote 前端

- 用 Kairote 实现 Lexer、Parser、AST 和诊断。
- 由 Stage 0 编译为 Stage 1。
- 用语法审计用例验证前端，不生成可执行文件要求。

### M2：语义和 IR

- 实现符号表、类型检查、控制流检查和 IR 验证器。
- 让 Stage 1 能编译自己的前端和 IR 模块。
- 增加 IR 文本快照和验证失败测试。

### M3：Kro 后端

- 先支持整数、分支、循环、调用和返回。
- 直接输出 Kro 并由 ArkLink 链接。
- 编译并运行最小 `main`、递归、全局数据和字符串程序。

### M4：首次自举

- Stage 1 编译完整编译器源码生成 Stage 2。
- Stage 2 再编译同一源码生成 Stage 3。
- Stage 2 和 Stage 3 对固定输入产生相同规范化哈希。

### M5：扩展和替换

- 扩展数组、指针、模块导入和标准库支持。
- 将 Stage 2 设为默认自举编译器。
- C KrtC 退回种子和交叉验证工具，不再是日常构建依赖。

## 8. 验收标准

必须同时满足以下条件才算完成真正自举：

1. `cmake --build build --target KrtC` 能构建 Stage 0。
2. Stage 0 能生成可运行的 Stage 1 编译器。
3. Stage 1 能在不调用外部 C 编译器的情况下生成 Kro 并链接一个可运行程序。
4. Stage 1 能编译完整的 Kairote 编译器源码生成 Stage 2。
5. Stage 2 能再次编译自身生成 Stage 3。
6. Stage 2 和 Stage 3 的规范化输出哈希一致，或差异有可解释且经过测试的重定位原因。
7. 前端错误、类型错误、未定义符号和链接错误均有稳定诊断。
8. `Test/` 下的语法、标准库、指针、质量和自举回归全部通过。
9. 后续代际构建日志中不存在 `cc`、`gcc`、`clang` 等外部编译器调用。

## 9. 风险与应对

- **语言子集过大**：先冻结可自举子集，使用能力表逐项扩展。
- **Kro 格式不足**：优先补齐 writer/loader 的契约测试，再扩展后端。
- **代际漂移**：每次自举都保存规范化哈希和编译日志。
- **宿主依赖泄漏**：在 Stage 1+ 构建中隔离 PATH，并扫描进程调用记录。
- **前后端耦合**：IR 验证器和独立 IR 快照测试作为合并门槛。

## 10. 成功定义

成功不是“某个 Kairote 文件可以运行”，而是 Kairote 编译器能够从自己的源码生成下一代 Kairote 编译器，生成过程经过可审计的 Frontend → IR → Kro → ArkLink 链路，并且后续代际不再需要 C 编译器。
