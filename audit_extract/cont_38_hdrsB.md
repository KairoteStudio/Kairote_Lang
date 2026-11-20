# 第九十五批：Driver/Frontend/Core/Tools 头文件

## 覆盖范围

本批补审原审计在第 57 批后中断、从未完整覆盖的 17 个头文件（全量读取）：

- `Re.KrtC/src/Compiler/Compiler.h`
- `Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.h`
- `Re.KrtC/src/Compiler/Driver/Compiler.h`
- `Re.KrtC/src/Compiler/Driver/ConsoleUtils.h`
- `Re.KrtC/src/Compiler/Driver/ParallelCompiler.h`
- `Re.KrtC/src/Compiler/Driver/ProjectKrt.h`
- `Re.KrtC/src/Compiler/Frontend/CompilerError.h`
- `Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.h`
- `Re.KrtC/src/Compiler/Frontend/Parser/Parser.h`
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.h`
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.h`
- `Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.h`
- `Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.h`
- `Re.KrtC/src/Core/Utils/Logger.h`
- `Re.KrtC/src/Core/Utils/OutputCache.h`
- `Re.KrtC/src/Tools/KroWriter.h`
- `Re.KrtC/src/Version.h`

核对方式：仅对特定符号做 grep（定义/调用/包含守卫），未整文件通读其他源文件。

## 问题条目

### [致命] 95-1｜聚合头与 Driver 头共用包含守卫 KRT_COMPILER_H，导致 Compiler.h 被整体吞掉
- 位置：`Re.KrtC/src/Compiler/Compiler.h:1-2` 与 `Re.KrtC/src/Compiler/Driver/Compiler.h:1-2`（聚合头在第 13 行包含 Driver 头）
- 类别：包含守卫撞名 / 契约破坏
- 触发条件：任一翻译单元先包含 `Compiler/Driver/Compiler.h` 再包含 `Compiler/Compiler.h`（或反之），第二次包含因守卫已定义而被预处理器整体跳过。
- 后果：`Main.c:28-29` 正是先 Driver 后聚合，聚合头 `Compiler.h` 的全部内容（含对 `Driver/Compiler.h` 的包含）被静默丢弃；`KrtCompiler`、`KrtTargetPlatform`、`KrtCompilerCreate/Destroy/Compile` 在聚合头路径下完全不可见。当前 Main.c 因逐个头显式包含而未暴露，但任何只包含聚合头的翻译单元会直接编译失败或退化为隐式声明，聚合头作为"总入口"的契约被破坏。
- 最小修复：把 `Compiler/Driver/Compiler.h` 的守卫改为 `KRT_DRIVER_COMPILER_H`（或把聚合头改为 `KRT_COMPILER_AGGREGATE_H`），二者不得同名。
- 备注：全仓库 grep `KRT_COMPILER_H` 仅命中这两个头，无第三方占用，改名安全。

### [高] 95-2｜Parser.h 中 13 个 parser_parse_* 声明在全仓库无定义、无调用
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/Parser.h:32-47`
- 类别：死代码（声明无定义）/ 契约破坏
- 触发条件：任何外部代码按头文件声明调用这些函数；或审计/工具按声明生成绑定。
- 后果：`parser_parse_try_statement`、`parser_parse_catch_clause`、`parser_parse_finally_clause`、`parser_parse_throw_statement`、`parser_parse_lock_statement`、`parser_parse_yield_statement`、`parser_parse_using_statement`、`parser_parse_template_declaration`、`parser_parse_template_parameter`、`parser_parse_case_clause`、`parser_parse_default_clause`、`parser_parse_break_statement`、`parser_parse_continue_statement` 全部只有声明，仓库内无定义（`break`/`continue`/`case` 实际在 `parser_parse_statement` 内联处理）。一旦被调用即链接错误；同时声明与实现脱节，掩盖了这些语句并未走独立函数的事实。
- 最小修复：删除未实现声明；若确为规划中的 API，则补定义并加 `#ifdef` 标注未完成状态。
- 备注：全仓库 grep 这 13 个符号，除 Parser.h 外零命中。

### [高] 95-3｜ParserStatement.h 声明的两个函数与实现不符：一个被 static 隐藏，一个根本不存在
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.h:6-7`；实现见 `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:180`（`static ASTNode* parser_parse_variable_declaration`）、`ParserStatement.c:490`（`static ... parser_parse_assignment_from_left`）
- 类别：契约破坏（声明与实现不符）/ 死声明
- 触发条件：其他翻译单元按头文件调用 `parser_parse_variable_declaration` 或 `parser_parse_assignment`。
- 后果：`parser_parse_variable_declaration` 在头文件中是外部可见符号，实现却是 `static`，外部调用会链接失败；`parser_parse_assignment` 只有声明、全仓库无定义（实际实现是 `parser_parse_assignment_from_left`），调用必然未定义符号。
- 最小修复：把 `parser_parse_variable_declaration` 的 `static` 去掉并同步签名；删除 `parser_parse_assignment` 声明或补上实现。
- 备注：`ParserStatement.c` 内部大量调用的是 `parser_parse_variable_declaration_with_type` / `parser_parse_assignment_from_left`，均未在头部暴露。

### [高] 95-4｜SemanticAnalyzer.h 中 6 个声明在全仓库无定义、无调用
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.h:85,87,88,95,99,100`
- 类别：死代码（声明无定义）/ 契约破坏
- 触发条件：按声明调用 `semantic_analyzer_check_entry_point`、`semantic_analyzer_resolve_forward_references`、`semantic_analyzer_get_result`、`semantic_analyzer_add_error_ex_at`、`semantic_analyzer_get_symbol_type_name`、`semantic_analyzer_is_valid_symbol_name`。
- 后果：六个公开 API 只有声明没有定义，调用即链接错误。其中 `semantic_analyzer_get_result` 与 `semantic_analyzer_analyze` 同为返回 `SemanticAnalysisResult*`，功能重叠，暗示一次未完成的重构；`semantic_analyzer_add_error_ex_at` 带 `KrtErrorStage` 参数，本应是错误阶段上报的入口，缺失后阶段信息只能走 `add_error_at` 的默认 SEMANTIC。
- 最小修复：删除未实现声明；若需要则补定义（尤其 `add_error_ex_at` 与阶段枚举配套）。
- 备注：全仓库 grep 这 6 个符号仅命中头文件本身。

### [高] 95-5｜`#include "Bytecode.h"` 同名同守卫、不同布局，KrtChunk 定义跨翻译单元不一致
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.h:20`（触发点；`Compiler.h:28` 的 `KrtChunk last_chunk` 字段）
- 类别：包含守卫撞名 / 语义分叉 / ODR 违反
- 触发条件：`src/Bytecode/Bytecode.h` 与 `Shared/Bytecode.h` 均以 `KRT_BYTECODE_H` 为守卫（分别见 `src/Bytecode/Bytecode.h:1-2`、`Shared/Bytecode.h:1-2`），且都定义 `KrtChunk`；`CMakeLists.txt:20` 的包含顺序为 `src src/Core src/Tools src/Bytecode Shared ...`，`src/Bytecode` 先于 `Shared`。
- 后果：`Compiler.c`/`VmCodegen.c` 经 `-I src/Bytecode` 取到 `src/Bytecode/Bytecode.h`（其 `KrtValueArray` 字段顺序为 `capacity,count,values`，见 `src/Bytecode/Value.h:41-44`），而 `Shared/BytecodeGenerator.c` 经相对包含取到 `Shared/Bytecode.h`（`KrtValueArray` 为 `count,capacity,values`，见 `Shared/Bytecode.h:66-70`）。两处 `KrtChunk.constants` 的 `count`/`capacity` 语义相反，跨 TU 操作同一对象时容量判断错位，存在堆越界/损坏风险；且因守卫同名，同一 TU 内后包含者被静默丢弃，问题不会以宏重定义告警的形式暴露。
- 最小修复：统一 `KrtValueArray`/`KrtChunk` 定义到唯一头文件；两个 `Bytecode.h` 使用不同守卫；包含改为可唯一解析的路径（如 `Bytecode/Bytecode.h`）并固定包含顺序。
- 备注：`src/Bytecode/Bytecode.h` 声明的 `KrtChunkInit/Write/Free/AddConstant/AddStringConstant` 全仓库无调用者，只有结构体被使用，说明该头基本被 Shared 版本取代但未清理。

### [中] 95-6｜KRT_DEBUG_IR 宏引用了全仓库未定义的 KRT_DEBUG_LOG
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.h:9`
- 类别：注释与实现脱节 / 死代码
- 触发条件：任何翻译单元使用 `KRT_DEBUG_IR(...)`。
- 后果：`KRT_DEBUG_LOG` 在仓库中无定义（grep 仅命中该宏体自身），展开即编译错误；该宏目前是死宏， Driver 层缺少可用的 IR 调试输出通道。
- 最小修复：定义 `KRT_DEBUG_LOG(tag, fmt, ...)`（可复用 `KrtPrintf`/`KrtOutputCacheAdd`），或删除 `KRT_DEBUG_IR`。
- 备注：`Compiler.c` 中实际未使用该宏。

### [中] 95-7｜版本号双源漂移：Version.h 与 KrtCommon.h 数值不一致
- 位置：`Re.KrtC/src/Version.h:4`（`KRT_COMPILER_VERSION "0.00.0006cc"`）；对照 `Re.KrtC/src/Core/Utils/KrtCommon.h:4-7`（`KRT_VERSION_MAJOR 1` / `MINOR 0` / `PATCH 0` / `KRT_VERSION_STRING "1.0.0"`）
- 类别：配置漂移
- 触发条件：CLI 打印版本（`Main.c:134`、`Main.c:1389`）或任何使用 `KRT_COMPILER_INFO` 的运行时信息。
- 后果：同一编译器的版本标识出现 `0.00.0006cc` 与 `1.0.0` 两套值，构建产物/日志/用户可见版本不一致，无法据版本号追溯产物。
- 最小修复：确立单一版本源（建议 KrtCommon.h），Version.h 由该源派生或删除；发布流程统一 bump。
- 备注：`KRT_COMPILER_INFO` 在仓库中无使用者，`KRT_VERSION_STRING` 目前仅服务于它。

### [中] 95-8｜KrtErrorStage 阶段枚举与实现脱节：仅 SEMANTIC 会被产出
- 位置：`Re.KrtC/src/Compiler/Frontend/CompilerError.h:9-15`；唯一产出点 `Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.c:846,872`
- 类别：阶段不对齐 / 死枚举
- 触发条件：词法、语法、类型检查、代码生成、SSA 阶段产生错误时。
- 后果：`KRT_ERROR_STAGE_LEX`、`PARSE`、`TYPE_CHECK`、`CODEGEN`、`SSA`、`UNKNOWN` 在仓库中只出现于 `CompilerError.c` 的 switch 分支，没有任何生产者传入；所有语义错误一律上报为 `KRT_ERROR_STAGE_SEMANTIC`。错误分类、阶段统计与按阶段过滤全部失真。
- 最小修复：在 Lexer/Parser/Semantic/Codegen 各层调用 `KrtErrorReportAdd(AddEx)` 时传真实 stage；无法区分的阶段删除枚举值。
- 备注：`CompilerError.c:19-36` 的 `KrtErrorStageName` 已为这些值准备了名字，说明设计意图与实现脱节。

### [中] 95-9｜KrtErrorReportAddEx 的扩展能力无生产者：end 位置与 error_code 永远为空
- 位置：`Re.KrtC/src/Compiler/Frontend/CompilerError.h:47-48`；唯一调用点 `Re.KrtC/src/Compiler/Frontend/CompilerError.c:136`
- 类别：死代码 / 契约破坏
- 触发条件：任何需要精确错误区间或错误码的调用方。
- 后果：`KrtErrorReportAdd` 调用 `AddEx` 时把 `end_line/end_column` 填成与 `line/column` 相同、`error_code` 填 `""`，仓库中再无其他 `AddEx` 调用者。于是 `KrtCompileError.end_line/end_column/error_code` 成为永不生效的死字段，错误定位退化为单点，`AddEx` 名不副实。
- 最小修复：让 SemanticAnalyzer 在 `add_error_at` 系列里传递真实 end 位置与错误码并走 `AddEx`；否则删除 `AddEx` 及相关字段。
- 备注：`CompilerError.c:267` 已按 `end_line/end_column` 打印，说明消费端就绪、生产端缺失。

### [中] 95-10｜NameMangling.h 的 NameManglingResult 类型与 4 个公开函数无使用者
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.h:6-10`（`NameManglingResult`）、`:25`（`name_mangle_from_ast`）、`:30`（`name_mangle_class_member`）、`:32,34`（`name_demangle`/`name_demangle_free`）
- 类别：死代码 / 语义分叉
- 触发条件：任何外部模块使用该反混淆/成员名拼接 API。
- 后果：`NameManglingResult` 结构体全仓库零引用；`name_mangle_from_ast`、`name_mangle_class_member` 除自身文件内的相互调用外无外部调用者；`name_demangle`/`name_demangle_free` 仅在其自身错误清理路径被调用，未形成对外的反混淆能力。公开头承诺的 API 实际不可用/未接线。
- 最小修复：删除未用声明与类型；若反混淆是规划功能，应在符号表/错误显示处接线。
- 备注：`name_mangle_names` 虽在头文件公开，实际只被同文件其他 mangle 函数调用，属"半内部"API。

### [中] 95-11｜项目/库配置三处重复建模，所有权类型不一致
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.h:7-17`（`KrtProjectKrtConfig`：`libraries/library_count` 为 `char**`）；`Re.KrtC/src/Compiler/Driver/ConfigManager.h:22-23`（`KrtConfig`：`libraries` 为 `const char* const*`、`library_count`）；`Re.KrtC/src/Compiler/Driver/Project.h:43-45`（`KrtProject`：`items/dependencies`）
- 类别：配置漂移 / 语义分叉
- 触发条件：任一需要读取项目库列表的模块（如 ArkLinkIntegration、ParallelCompiler）。
- 后果：同一"项目—库"信息存在三套表示，字段名相同而所有权/常量性不同（`char**` vs `const char* const*`），无单一事实来源，转换时易漏释放或悬垂；也是 95-12 中 `KrtProjectKrtSave` 无调用者的背景原因。
- 最小修复：确立唯一项目配置结构，其余层通过访问函数或 `const` 视图引用，禁止重复字段。
- 备注：`Main.c` 同时使用 `KrtProjCreate`（`Main.c:76`）与 `KrtProjectKrtLoad`（`Main.c:923,1156`），印证双模型并存。

### [低] 95-12｜KrtProjectKrtSave 定义但全仓库无调用者
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.h:21`；定义 `Re.KrtC/src/Compiler/Driver/ProjectKrt.c:797`
- 类别：死代码（定义无调用）
- 触发条件：需要把 `KrtProjectKrtConfig` 写回 `.krt` 文件时。
- 后果：`.krt` 工程文件只能读不能写，保存/更新项目配置的功能缺失；`ProjectKrt.c` 中为此实现的序列化逻辑成为死代码。
- 最小修复：接入保存命令（如 `krt build --save`）或删除该函数及其实现。
- 备注：全仓库 grep `KrtProjectKrtSave` 仅命中头与定义。

### [低] 95-13｜KrtArkLinkSetEntryPoint 定义但无调用者，入口点实际由 linker_config 传递
- 位置：`Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.h:18`；定义 `Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.c:167`；实际入口点设置点 `ArkLinkIntegration.c:65-66`
- 类别：死代码（定义无调用）
- 触发条件：调用方试图用 `KrtArkLinkSetEntryPoint` 覆盖入口点。
- 后果：公开 API 无调用者，入口点改由 `KrtLinkerConfig::entry_point` 在 `KrtArkLinkContextCreate` 内设置；调用者若误以为该函数必须调用，会写出无效代码，且 `KrtConfig::entry_point` 字段实际无人读取。
- 最小修复：删除该函数，或在 `KrtArkLinkLinkObjects` 中按 `config->entry_point` 调用它。
- 备注：全仓库 grep 仅命中头与定义两处。

### [低] 95-14｜Logger.h 与 OutputCache.h 重复声明同一打印 API，且 Logger.h 缺格式属性
- 位置：`Re.KrtC/src/Core/Utils/Logger.h:7-9`；`Re.KrtC/src/Core/Utils/OutputCache.h:39-41`
- 类别：契约重复 / 配置漂移
- 触发条件：同时包含两个头的翻译单元（如经 `Core/Core.h` 与显式包含）。
- 后果：`KrtPrintf/KrtPrintFormat/KrtFprintf` 两处声明，`OutputCache.h` 带 `KRT_PRINTF_FORMAT` 而 `Logger.h` 不带；格式串检查只在包含 OutputCache.h 的路径生效，含 Logger.h 的路径失去编译期校验。两处需人工保持同步。
- 最小修复：Logger.h 的声明补齐 `KRT_PRINTF_FORMAT` 并包含 `Attributes.h`，或改为直接包含 OutputCache.h 并删除重复声明。
- 备注：实现只在 `Core/Utils/Logger.c:68,112,129`。

### [低] 95-15｜ConsoleUtils.h 的 ANSI_SAVE_CURSOR / ANSI_RESTORE_CURSOR 宏无使用者
- 位置：`Re.KrtC/src/Compiler/Driver/ConsoleUtils.h:16-17`
- 类别：死代码
- 触发条件：需要保存/恢复光标的终端输出。
- 后果：两个宏在全仓库仅定义处出现，无调用者；`ANSI_CURSOR_UP`/`ANSI_CLEAR_LINE` 则被 TaskManager.c 使用，说明光标控制只实现了一半。
- 最小修复：删除未用宏，或在 TaskManager 的光标控制中补齐使用。
- 备注：全仓库 grep 仅命中定义行。

### [低] 95-16｜Tokenizer.h 存在空的 `#ifdef _WIN32 ... #endif` 块
- 位置：`Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.h:8-10`
- 类别：死代码 / 配置漂移
- 触发条件：在 `_WIN32` 下编译时。
- 后果：平台分支内无任何声明，属被清空但未清理的占位；同时 `<stdio.h>`/`<stdlib.h>` 紧随其后，读者会误以为存在平台相关声明。
- 最小修复：删除该空块，或补齐曾经存在的平台相关声明。
- 备注：`Lexer` 结构体本身无平台差异。

### [低] 95-17｜KrtOutputCacheSetEnabled 定义但无调用者，缓存开关不可达
- 位置：`Re.KrtC/src/Core/Utils/OutputCache.h:37`；定义 `Re.KrtC/src/Core/Utils/OutputCache.c:199`
- 类别：死代码（定义无调用）
- 触发条件：需要开启/关闭输出缓存时。
- 后果：`g_output_cache.enabled` 仅在 `KrtOutputCacheInit`（`OutputCache.c:11-13`）中被置 0，运行期无法改变；缓存功能实际恒定关闭，`SetEnabled` 成为不可达 API。
- 最小修复：在 CLI/配置层接入开关，或删除该函数。
- 备注：全仓库 grep `KrtOutputCacheSetEnabled` 仅命中头与定义。

### [低] 95-18｜KrtCompilerCompile 以 void* 传递 semantic_analyzer，类型擦除
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.h:36`
- 类别：契约破坏（类型不安全）
- 触发条件：调用方传入非 `SemanticAnalyzer*` 的指针。
- 后果：编译期无法发现误传；实现 `Compiler.c:163` 直接把该 `void*` 转交 `KrtIrGenerateFromAst`，错误类型会延迟到运行期。注释所称 "borrowed analyzer" 的所有权约定也无编译期保障。
- 最小修复：在头文件中前向声明 `typedef struct SemanticAnalyzer SemanticAnalyzer;`，参数改为 `SemanticAnalyzer*`。
- 备注：`KrtCompiler` 结构体未回指该 analyzer，borrow 关系仅靠注释表达。

### [低] 95-19｜NameMangling 各函数返回 char* 但未约定所有权与释放责任
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.h:19-34`
- 类别：契约破坏（所有权约定缺失）
- 触发条件：调用方使用 `name_mangle_simple/constructor/names/from_ast/function/class_member` 的返回值。
- 后果：头文件未注明返回值是否需 `free`、由谁释放；实现返回堆分配串（`NameMangling.c` 各 `return KRT_STRDUP/分配串`），调用方（IrGen.c、SemanticAnalyzer.c）只能靠约定 `KRT_FREE`，一旦新调用方遗漏即泄漏，误用 `free` 于静态串则崩溃。
- 最小修复：为每个返回 `char*` 的函数补充 `@brief 返回堆分配串，调用方负责 KRT_FREE` 注释，或改为调用方传入缓冲区。
- 备注：`KrtMangleTypeName` 已正确标注为不可变 ABI 编码，可作为注释范式。

### [低] 95-20｜KroWriter 写函数无失败通道，reloc 字段宽度与结构体不一致
- 位置：`Re.KrtC/src/Tools/KroWriter.h:79-84`（`kro_write_code/data/rodata(_aligned)` 返回 `uint32_t`）、`:94-95`（`kro_add_reloc` 的 `uint16_t type, int16_t addend`）；结构体字段见 `:65-70`（`uint32_t type; int32_t addend`）
- 类别：契约破坏（错误码约定缺失）/ 语义分叉
- 触发条件：写入过程中发生分配失败，或新增超出 16 位的重定位类型/加数。
- 后果：`kro_write_*` 用 `uint32_t` 同时表达偏移与错误，无文档说明失败返回值，调用方（KroCodegen.c:182,190,194,1435,1464）无法区分成功偏移与失败；`kro_add_reloc` 把类型/加数收窄为 16 位，与写入结构体的 32 位字段不匹配，未来扩展会被静默截断。
- 最小修复：写函数改为 `bool` + 出参偏移，或定义 `KRO_WRITE_FAILED` 常量并注释；reloc 形参宽度与 `KRORelocation` 对齐。
- 备注：`kro_write_file` 已用 `bool` 表达成功，可作为范式。
