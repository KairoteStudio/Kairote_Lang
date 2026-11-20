# 第九十四批：ArkLink/StubInclude/后端/Build 头文件

## 覆盖范围

本批全量读取并审计以下 13 个头文件（read 工具整文件读取；声明—定义—调用一致性仅用 grep 特定符号核对，未整文件通读其它源文件）：

| 文件 | 行数 | 核对方式 |
| --- | --- | --- |
| `ArkLink/include/ArkLink/Arklink.h` | 88 | 13 个 `arklink_session_*` 符号在 `ArkLink/src/Core/Arklink.c`、`LinkerNative.c` 全部有定义；`ARK_LINK_FLAG_*` 全仓库无引用 |
| `ArkLink/include/ArkLink/BackendElf.h` | 17 | `ark_backend_elf_link` 定义于 `ArkLink/src/Backend/BackendElf.c:2055`；与 `Backend.h:195` 重复声明 |
| `ArkLink/include/ArkLink/BackendPe.h` | 18 | `ark_backend_pe_link` 定义于 `ArkLink/src/Backend/BackendPe.c:665`；与 `Backend.h:193` 重复声明 |
| `ArkLink/include/ArkLink/Context.h` | 30 | `ark_context_create/destroy` 定义于 `ArkLink/src/Core/Context.c:10/20`；`ArkSectionBuffer` 被 Loader/Resolver/Backend 使用 |
| `ArkLink/include/ArkLink/LinkerNative.h` | 17 | `arklink_session_link_native` 定义于 `ArkLink/src/Core/LinkerNative.c:33` |
| `ArkLink/include/ArkLink/NativeAbi.h` | 7 | `ark_native_abi_validate` 定义于 `ArkLink/src/Core/NativeAbi.c:259`，签名一致；无 `extern "C"` |
| `Re.KrtC/StubInclude/ArkLink/Arklink.h` | 75 | 与真实头逐条比对：枚举值/函数签名全部一致，仅缺 12 条 `@brief` 注释，无语义分叉；包含守卫与真实头撞名 |
| `Re.KrtC/src/Compiler/Backend/BackendAllocation.h` | 31 | 3 个 `static inline` 包装在 X86RegAlloc/X86Codegen/VmCodegen 中有调用 |
| `Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h` | 90 | `KrtKrtGenerate` 定义于 `KroCodegen.c:1470`；4 个宏均被使用；2 个字段无引用 |
| `Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.h` | 11 | `KrtVmCodegenGenerate` 定义于 `VmCodegen.c:296`，被 `Compiler.c:225` 调用 |
| `Re.KrtC/src/Compiler/Backend/X86/X86CodeOpt.h` | 18 | 4 个符号均定义于 `X86CodeOpt.c`，但公开入口 `x86_optimize_peephole` 全仓库无调用 |
| `Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h` | 88 | `KrtX86Generate` 定义于 `X86Codegen.c:1053`；12 个 `codegen_*` 中 2 个无调用；1 个宏无引用 |
| `Re.KrtC/src/Compiler/Build/BuildSystem.h` | 26 | 4 个 `KrtBuild*` 定义于 `BuildSystem.c:6/20/27/73`，被 `Main.c:483-493` 调用；返回约定与调用方相反 |

结论概览：致命 1 条、高 1 条、中 5 条、低 10 条，共 17 条。

## 问题条目

### [致命] 94-1｜`KrtBuildExecute` 返回约定未定义，且实现与调用方判断相反
- 位置：`Re.KrtC/src/Compiler/Build/BuildSystem.h:19`
- 类别：1 契约破坏（错误码约定缺失）
- 触发条件：任何一次成功或失败的 native 构建。
- 后果：头文件只写 `int KrtBuildExecute(...)`，无任何返回值约定。实现 `BuildSystem.c` 采用 `1=成功 / 0=失败`（`BuildSystem.c:29,34,41,47,53,59,66` 全部 `return 0;`，成功路径 `:70` `return 1;`）；而唯一调用方 `Main.c:485` 写成 `if (KrtBuildExecute(build_ctx, temp_eo, output_file)) { KrtError("构建失败：%s", KrtBuildGetError(build_ctx)); ... }`，即把非 0 当失败。于是「构建成功」被判定为失败并打印「构建失败」，「构建失败」（返回 0）反而被当作成功继续往下走，错误信息只在成功路径被读取。
- 最小修复：统一约定为 `0=成功，非 0=失败`（或 `1=成功`），同步修正 `BuildSystem.c` 与 `Main.c:485` 之一，并在 `BuildSystem.h:19` 注释中写明返回值与 `KrtBuildGetError` 的配套关系。
- 备注：本条为头文件契约缺失直接导致的调用方语义反转，非单纯文档问题。

### [高] 94-2｜`NativeAbi.h` 缺少 `extern "C"` 包裹，C++ 调用方符号不匹配
- 位置：`ArkLink/include/ArkLink/NativeAbi.h:6`
- 类别：1 契约破坏（语言链接约定缺失）
- 触发条件：任何 C++ 翻译单元 `#include "ArkLink/NativeAbi.h"` 并调用 `ark_native_abi_validate`。
- 后果：该头没有 `#ifdef __cplusplus extern "C" { #endif`，声明按 C++ 链接处理（名字修饰），而 `ArkLink/src/Core/NativeAbi.c:259` 以 C 链接定义该符号，链接期报未定义符号。同目录 `Arklink.h:7-9`、`Context.h:6-8`、`LinkerNative.h:6-8`、`BackendElf.h:7-9`、`BackendPe.h:7-9` 全部带该包裹，唯 `NativeAbi.h` 遗漏。
- 最小修复：在 `#include "Loader.h"` 后补 `#ifdef __cplusplus extern "C" { #endif`，文件末尾补 `#ifdef __cplusplus } #endif`，与同目录头保持一致。
- 备注：该头是 `include/ArkLink/` 下的公开 API 头，且被 `Resolver.c` 使用；当前工程为纯 C，故为潜在链接破坏而非现网崩溃。

### [中] 94-3｜StubInclude 与真实 Arklink.h 共用同一包含守卫 `ARKLINK_ARKLINK_H`
- 位置：`Re.KrtC/StubInclude/ArkLink/Arklink.h:1-2`、`ArkLink/include/ArkLink/Arklink.h:1-2`
- 类别：5 包含守卫撞名
- 触发条件：同一翻译单元同时包含这两个路径（编译器前端的 StubInclude 搜索路径与真实头搜索路径同时生效时即可能发生）。
- 后果：两个不同文件使用完全相同的守卫宏，后包含者被静默跳过，编译器不会报重定义，也不会提示缺失声明；一旦 stub 与真实头发生漂移，前端解析到的声明与实际链接的符号不一致且无任何编译期信号。
- 最小修复：为 stub 使用独立守卫（如 `ARKLINK_STUB_ARKLINK_H`），或让 stub 直接 include 真实头；至少在两文件头注释中互相指明守卫必须同步。
- 备注：本批逐条比对确认两者当前枚举值、函数签名完全一致（stub 仅缺 12 条 `@brief` 注释），故当前无语义分叉，属潜在撞名风险。

### [中] 94-4｜`KrtBuildContext` 的 `config`/`platform` 所有权未约定
- 位置：`Re.KrtC/src/Compiler/Build/BuildSystem.h:11-15`
- 类别：1 契约破坏（所有权约定缺失）
- 触发条件：调用方按「`KrtBuildContextCreate` 接管 config/platform」或「`KrtBuildContextDestroy` 会释放它们」的假设编写代码。
- 后果：结构体把 `KrtConfig* config; KrtPlatform* platform;` 直接暴露为公开字段，但头文件没有任何所有权说明；实现 `BuildSystem.c:20-25` 的 `KrtBuildContextDestroy` 只 `KRT_FREE(ctx)`，不释放 `config`/`platform`。调用方若自行释放即双重释放，若依赖 destroy 释放则泄漏。
- 最小修复：在结构体或 `KrtBuildContextCreate` 声明处注释「ctx 不拥有 config/platform，生命周期由调用方维护」。
- 备注：`KrtBuildContextCreate` 对 `config`/`platform` 也未做 NULL 校验（`BuildSystem.c:6-18` 直接存入），头文件未声明该前置条件。

### [中] 94-5｜`x86_optimize_peephole` 定义但全仓库无调用，整个 peephole 阶段为死代码
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86CodeOpt.h:14`
- 类别：2 死代码（定义无调用）
- 触发条件：编译任何走 X86 后端的目标时。
- 后果：`x86_optimize_peephole` 在 `X86CodeOpt.c:428` 实现（内部还按 `optimization_level` 决定 3/5 趟），但全仓库（含 Test/）没有任何调用点，X86 代码生成流程从不执行 peephole 优化；`optimization_level` 在该路径上完全无效。`KrtX86Generate`（`X86Codegen.c:1053`）也未引用优化器。
- 最小修复：在 `KrtX86Generate` 的合适阶段调用 `x86_optimize_peephole(module, optimization_level)`，或删除该阶段及其头文件声明。
- 备注：`x86_optimize_peephole` 内部调用的 `x86_peephole_optimizer_create/destroy`（`X86CodeOpt.c:433/457`）只服务于这条死路径。

### [中] 94-6｜`codegen_alloc_register`/`codegen_free_register` 无调用，寄存器跟踪子系统失效
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:73`、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:75`
- 类别：2 死代码（定义无调用）
- 触发条件：任何 X86 代码生成。
- 后果：两函数在 `X86Codegen.c:201/223` 有完整实现，但全仓库无调用点；依赖它们的 `RegisterState registers[X86_MAX_REGISTERS]`（`X86Codegen.h:47`）、`RegisterState.content` 字段因此成为死数据。寄存器分配实际由 `X86RegAlloc.c` 的 `regalloc_allocate_with_capacity`/`x86_allocate_registers` 完成，两套机制并存且头文件把死 API 作为公开接口暴露，易被误用。
- 最小修复：将 `codegen_alloc_register/free_register` 接入 `X86Codegen.c` 的寄存器分配路径，或删除声明、定义及 `registers`/`RegisterState`。
- 备注：`codegen_get_temp_location`/`codegen_set_temp_*`/`codegen_emit_*` 均有调用，仅这两个寄存器管理函数孤立。

### [中] 94-7｜`ark_backend_elf_link` 声明缺少所有权/错误契约，与 BackendPe.h 不对称
- 位置：`ArkLink/include/ArkLink/BackendElf.h:11`
- 类别：1 契约破坏（所有权约定缺失）/ 3 注释与实现脱节
- 触发条件：调用方以 `BackendPe.h:11` 的约定（"the caller owns successful image and section-map allocations"）使用 ELF 后端时。
- 后果：`BackendPe.h:11` 明确写出成功时调用方拥有 image 与 section-map 分配，`BackendElf.h:11` 对同名同构 API 无任何说明；`ArkBackendOutput` 的所有权、失败时是否已释放、`ctx` 是否为 NULL 容忍等前置条件均未文档化。两个后端头一个有一个没有，调用方无法据此编写正确的清理代码。
- 最小修复：在 `BackendElf.h:11` 前补与 `BackendPe.h:11` 相同的 `@brief`，并补充 NULL/错误码约定。
- 备注：实现位于 `ArkLink/src/Backend/BackendElf.c:2055`；`BackendElf.h` 与 `Backend.h:195` 存在重复声明（签名一致，无冲突）。

### [低] 94-8｜`ARK_LINK_FLAG_VERBOSE`/`ARK_LINK_FLAG_QUIET` 无任何引用且无 setter
- 位置：`ArkLink/include/ArkLink/Arklink.h:11-14`
- 类别：2 死代码（声明无使用）
- 触发条件：任何需要控制 ArkLink 日志级别的调用方。
- 后果：匿名枚举定义了 `ARK_LINK_FLAG_VERBOSE = 1u << 0`、`ARK_LINK_FLAG_QUIET = 1u << 1`，但全仓库（含 `ArkLink/src/Main.c`、`Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.c`）无任何引用；`Arklink.h` 也没有 `arklink_session_set_flags` 之类的接口，`arklink_session_set_logger` 无级别过滤参数。两个公开枚举值不可达。
- 最小修复：实现 flag setter 并在 `Main.c` 解析 `-v/-q`，或删除这两个枚举值。
- 备注：`Main.c` 的 `logger_callback` 对所有级别一律打印。

### [低] 94-9｜`X86_MAX_LABEL_LEN` 定义后无使用
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:13`
- 类别：2 死代码（定义无使用）
- 触发条件：—
- 后果：宏 `X86_MAX_LABEL_LEN 256` 全仓库仅在此处出现，`X86Codegen.c` 生成标签时未使用该上限，标签缓冲区长度无约束。
- 最小修复：删除该宏，或用于标签名缓冲区上限。
- 备注：同文件 `X86_MAX_VAR_NAME_LEN`、`X86_MAX_TEMP_SPACE` 等均有使用，仅此宏孤立。

### [低] 94-10｜`X86_MAX_REGISTERS` 与 `X86_REGISTER_COUNT` 同值重复定义（配置漂移）
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:8`、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:12`
- 类别：8 配置漂移
- 触发条件：维护者只改动其中一个宏的值时。
- 后果：两个宏都等于 14，但分别用于不同位置——`registers[X86_MAX_REGISTERS]`（`X86Codegen.h:47`）与 `g_register_count = X86_REGISTER_COUNT`（`X86Codegen.c:9`）。任一处单独调整即造成数组容量与寄存器计数不一致，引发越界或寄存器漏用，且无任何编译期关联约束。
- 最小修复：`#define X86_REGISTER_COUNT X86_MAX_REGISTERS`，或合并为单一宏。
- 备注：当前值相同，属潜在漂移而非现网错误。

### [低] 94-11｜`x86_peephole_get_optimization_count` 定义但无调用
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86CodeOpt.h:16`
- 类别：2 死代码（定义无调用）
- 触发条件：—
- 后果：该访问器在 `X86CodeOpt.c:460` 有定义，全仓库无调用点；与 94-5 同属已废弃的 peephole 公开 API。
- 最小修复：随 94-5 一并接入或删除。
- 备注：—

### [低] 94-12｜`KROCodegenContext.current_phi_inst`、`phi_operand_index` 字段无任何读写
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h:83`、`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h:84`
- 类别：2 死代码（死字段）
- 触发条件：—
- 后果：两个字段在全仓库（含 `KroCodegen.c` 与全部 `.inc`）仅出现在头文件声明处，无任何赋值或读取；每个 `KROCodegenContext` 实例浪费 16 字节，并暗示存在未实现的 phi 处理路径，误导维护者。
- 最小修复：删除这两个字段；若确有 phi 支持计划，应补注释说明并加 `(void)` 使用点。
- 备注：同结构体其余字段（`tail_combine`、`tail_return`、`rotated_tail_leaf` 等）均有使用。

### [低] 94-13｜`backend_allocate` 系列在 `KRT_COMPILE_ERROR` 后的 `return` 不可达
- 位置：`Re.KrtC/src/Compiler/Backend/BackendAllocation.h:12`、`Re.KrtC/src/Compiler/Backend/BackendAllocation.h:20`、`Re.KrtC/src/Compiler/Backend/BackendAllocation.h:28`
- 类别：2 死代码（不可达语句）/ 3 注释与实现脱节
- 触发条件：任何分配失败路径。
- 后果：`KRT_COMPILE_ERROR` 在 `Core/Utils/KrtCommon.h:60-64` 展开为 `fprintf(...); exit(1);`（`:64` 为 `exit(1)`），因此 `return allocation;` 永不执行；`if (!allocation)` 分支本身可达（`KrtSafeMalloc` 失败会返回 NULL，见 `Core/Memory/Allocator.c:161` 等），但「返回 NULL 给调用方」的设计意图与宏的退出语义矛盾。
- 最小修复：删除不可达的 `return allocation;`，或改用可返回错误码的宏使头文件契约自洽。
- 备注：`backend_allocate_zeroed` 的 `(count ? count : 1, size ? size : 1)` 与 `KRT_CALLOC(count, size)` 参数序一致，无错误。

### [低] 94-14｜`arklink_session_link_native` 注释所称 "native platform linker" 与实现不符
- 位置：`ArkLink/include/ArkLink/LinkerNative.h:10`
- 类别：3 注释与实现脱节
- 触发条件：调用方依据注释理解该函数行为时。
- 后果：注释写 "Link session inputs using the native platform linker"，但实现 `ArkLink/src/Core/LinkerNative.c:33-175` 并不调用系统链接器（ld/link.exe），而是自行 `calloc` 单元、调用 `ark_loader_load_unit`、`ark_resolver_resolve`，再分派到 ArkLink 自带的 `ark_backend_pe_link`/`ark_backend_elf_link`（`:106/109`）。函数名 `link_native` 与注释共同误导 API 使用者。
- 最小修复：注释改为 "using ArkLink's PE/ELF backends"，或更名函数。
- 备注：函数签名与实现一致，非链接错误。

### [低] 94-15｜`KrtKrtGenerate` 命名前缀重复，疑似 `KrtKroGenerate` 笔误
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h:88`
- 类别：9 其它静态审计（命名一致性）
- 触发条件：按名称检索/调用 KRO 后端入口时。
- 后果：项目约定为 `Krt<模块>Generate`（`KrtX86Generate`、`KrtVmCodegenGenerate`），此处为 `Krt` + `Krt` + `Generate`，与 KRO 后端（KroCodegen.c:1470）不符，易被误认为另一 API，也给自动化检索/重构带来噪音。
- 最小修复：重命名为 `KrtKroGenerate`，同步 `KroCodegen.c:1470` 与 `Compiler.c:243,247` 两个调用点。
- 备注：声明与定义一致，当前无链接错误。

### [低] 94-16｜`KrtBuildContext.error_message` 为公开可变固定缓冲，`KrtBuildGetError` 返回其裸指针
- 位置：`Re.KrtC/src/Compiler/Build/BuildSystem.h:14`、`Re.KrtC/src/Compiler/Build/BuildSystem.h:20`
- 类别：9 其它静态审计（封装/契约）
- 触发条件：调用方在 `KrtBuildExecute` 返回后读取错误消息，或自行写入 `ctx->error_message`。
- 后果：结构体公开 `char error_message[256]`，调用方可直接改写 `KrtBuildGetError` 将要返回的内容；`BuildSystem.c:33,39` 等用 `snprintf` 写入时超长消息被静默截断，头文件未给出长度上限或截断约定。
- 最小修复：把 `error_message` 移入不透明结构体（`BuildSystem.c` 内定义），或注释明确「调用方不得修改，缓冲 256 字节，超长截断」。
- 备注：`KrtBuildGetError` 对空消息返回字面量 `"Build failed"`（`BuildSystem.c:80`），头文件未说明。

### [低] 94-17｜`VmCodegen.h` 以裸文件名包含两个分处异地的头，隐式依赖外部 `-I`
- 位置：`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.h:5`、`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.h:6`
- 类别：6 阶段不对齐 / 9 其它静态审计（包含路径脆弱）
- 触发条件：构建系统未把 `Re.KrtC/src/Bytecode` 与 `Re.KrtC/Shared` 同时加入包含路径时。
- 后果：`VmCodegen.h` 位于 `Compiler/Backend/Vm/`，却 `#include "Bytecode.h"`（实际在 `Re.KrtC/src/Bytecode/Bytecode.h`）与 `#include "BytecodeGenerator.h"`（实际在 `Re.KrtC/Shared/BytecodeGenerator.h`）。两个被依赖头都不在包含文件所在目录，必须依赖外部 `-I` 才能解析；`Vm/` 目录内仅有 `VmCodegen.c/.h`。一旦构建包含路径调整即编译失败，且从头部本身无法看出依赖位置。
- 最小修复：改用相对路径（如 `"../../../../Bytecode/Bytecode.h"`、`"../../../../Shared/BytecodeGenerator.h"`），或把两个头统一到同一目录。
- 备注：`Bytecode.h` 定义 `KrtChunk`（`:45`）、`BytecodeGenerator.h` 声明 `KrtBytecodeGeneratorWriteByte`（`:9`），均为 `KrtVmCodegenGenerate` 的必需类型。
