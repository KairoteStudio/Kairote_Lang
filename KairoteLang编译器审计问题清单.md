# KairoteLang 编译器全量审计 · 问题总清单

> 来源：`Codex_Recovery_审查编译器全量代码_2026-11-09/审查编译器全量代码.transcript.md`（主会话可读转录；原始 JSONL 未读，避免上下文膨胀）
> 整理日期：2026-11-20。条目编号沿用原文（S/F/批内数字），每条含位置、类别、触发条件、后果、最小修复，疑似项单独标注。

## 1. 总体情况

- 主会话共交付 **57 批**，最后一批（第五十七批 Core/Memory：`Allocator.c` / `Arena.c` / `SmartPtr.c`）后被 **HTTP 402 Payment Required（LongCat Usage limit reached）** 中断；该批「高/中」之后未再产出。
- 后期 6 个子审计任务（Feynman / Helmholz / James / Curie / Faraday / Pasteur）**全部因同一 402 错误终止**，没有任何最终报告或编号问题条目，只有过程性排除项（见附录 A）。
- 其后已追加 **44 个补审切片（第 58–101 批）**，完成 Kro 后端剩余段、前端解析、IR 内联、Krt stdlib/Runtime、SelfHost 全部源文件、Accelerator/IrParamTable/平台层剩余文件、examples 与 Test 各目录（见第五部分）。
- 本清单共 **1936** 条问题条目（含疑似项）：致命 232、高 624、中 777、疑似/需验证 43、其他 260（其中第五部分为后续补审新增）。
- 部分条目被后续「复核更正 / 全局修订」改判或撤回，正文以【修订 Gx】/「已撤回 / 已作废」标注，请以标注为准。

## 2. 全局修订（影响此前的判定）

### G1　分配失败前提修正（推翻前 55 批的部分判定）

原审计前期假设 `KRT_MALLOC` / `KRT_CALLOC` / `KRT_REALLOC` / `KRT_STRDUP` 失败即 `exit(1)`，并据此把大量 `if (!ptr) return NULL;` 判成死代码。**该前提是错的**：

- `Core/Memory/Allocator.c:159-211` `KrtSafeMalloc`：`size == 0` → `return NULL`（:160-162）；`__builtin_malloc` 失败 → `KrtMemoryReportError("MALLOC_FAILED",...)` 后 **`return NULL`**（:174-176）；全函数无 `exit`/`abort`。
- `KrtCommon.h:77-81` 的 `KRT_MALLOC` 等宏**无任何 `#ifdef` 守卫**，直接展开为上述函数。
- `KrtMemoryReportError`（`Allocator.c:425-435`）是**空操作**（仅 `KRT_DEBUG` 下 `__debugbreak()`）。

**结论**：所有分配失败路径都是**活路径，且零诊断**；真正的缺陷从「死代码」变成「OOM 时 NULL 解引用 / 静默 NULL 字段向下传播」。已定位的未检查调用点抽样：`IrGen.c:743,981`、`ParserBase.c:49`、`SemanticAnalyzer.c:426,1396,1686,1751,1839,1847,1852,2019,2071,2677,2755`、`ProjectKrt.c:118,241,433,757,760`、`ConfigManager.c:88`、`ParallelCompiler.c:32`、`IrType.c:193`、`IrSsa.c:273,1460`、`Ir.c:1074`。

### G2　S28-1 作废

原第二十八批称「数组字面量元素步长语义分叉导致 `cs=['A','B','C']; cs[1]` → exit=1」，该实证使用了 `function int32 main()`——已被证明是非法/被误解析的语法，结论无效。**正确结论见 S31-1**：数组字面量在**声明上下文正确**，仅非声明上下文出错。

### G3　第三十八批降级

`KrtIrSsaOptimize`（`IrSsa.c:1497-1508`）**只调用** `ssa_constant_propagation` / `ssa_copy_propagation` / `ssa_eliminate_dead_code`，**从不调用** `KrtIrSsaConstruct` / `ReplaceVarUses` / `RemoveStoreInsts` / `LowerPhis`。因此 **S38-1～S38-15 全部位于零调用者死代码内**，严重度应从「致命/高」降为「高/中（死代码内潜在）」；**S38-11 的 prev 疑似项撤回**（`KrtIRInst` 无 `prev` 字段）。

## 3. 覆盖范围

- **ArkLink 全量**：Core（`Arklink.c`、`Context.c`、`Loader*.c`、`Resolver.c`、`NativeAbi.c`）、Backend（Common/Elf/Pe）、头文件契约面。
- **工具链**：`Main.c`（全量 1406 行）、`BuildSystem.c`、`BytecodeGenerator.c`、`PlatformAbstraction.c`、`CompilerError.c`、`Path.c`、`SmartPtr.c`、`ArkLinkIntegration.c`、`KroWriter.c`、`ConsoleUtils.c`。
- **前端**：`SemanticAnalyzer.c`（全量 4099 行）、`SymbolTable.c`、`NameMangling.c`、`Generics.c`、`PointerAnalysis.inc`、`TypeExpansion.inc`、`StdlibCache.inc`、`AstClone.inc`、Parser 系列、Tokenizer 相关。
- **IR 层**：`IrGen.c`、`Ir.c`/`Ir.h`、`IrType.c`、`IrMemory.c`、`IrSsa.c`、`IrOptimizer.c`、`IrPureCse.inc`、`StackCalculator.c`。
- **驱动 / 项目 / 并行**：`Compiler.c`、`ProjectKrt.c`、`Project.c`、`ParallelCompiler.c`、`CompilerPipeline.c`、`ConfigManager.c`、`Preprocessor.c`、`TaskManager.c`、`ParallelCompilerLink.c`。
- **Core/Utils**：`Integer.h`、`KrtCommon.h`、`Error.h/.c`、`Attributes.h`、`Path.h`、`OutputCache.c`、`KrtString.h`、`Logger.c`、`LeakDetector.c`、`KrtTime.c`。
- **Core/Memory**：`Allocator.c`、`Arena.c`、`SmartPtr.c`（第五十七批，审计在此中断）。
- **补审后仍未覆盖**：构建/驱动脚本（`CMakeLists.txt`、`build.zig`、Test 的 `.py`/`.sh` 驱动与 `.exp` 期望文件）、Docs/README、CI 配置等非源码支撑文件；源码文件已全部覆盖。

## 4. 分部分统计

| 部分 | 致命 | 高 | 中 | 疑似/需验证 | 其他 | 小计 |
| --- | --- | --- | --- | --- | --- | --- |
| 第一部分 | 87 | 112 | 72 | 10 | 0 | 281 |
| 第二部分 | 34 | 136 | 69 | 26 | 0 | 265 |
| 第三部分 | 19 | 78 | 95 | 7 | 2 | 201 |
| 第四部分 | 2 | 65 | 66 | 0 | 1 | 134 |
| 第五部分 | 90 | 233 | 475 | 0 | 257 | 1055 |
| **合计** | **232** | **624** | **777** | **43** | **260** | **1936** |
> 注：「其他」含已作废条目、原文自降级条目及低/轻微项。
> 另见 **§5 致命问题速览**（仅标题与位置，快速定位最严重问题）。

## 5. 致命问题速览（共 232 条，仅标题与位置；详情见正文）

| # | 严重度标记 | 标题 | 位置 |
| --- | --- | --- | --- |
| 1 | 致命 | 1｜emit_binary_op DIV/MOD 有符号除法未符号扩展 | `X86Codegen.c:434-444` |
| 2 | 致命 | 2｜emit_compare 无符号/浮点比较误用有符号 setcc | `X86Codegen.c:463-499` |
| 3 | 致命 | 3｜KRT_IR_POW 参数寄存器按 Windows ABI 硬编码 | `X86Codegen.c:820-858` |
| 4 | 致命 | 4｜未实现 opcode 被静默丢弃 | `X86Codegen.c:789-792`（`KRT_IR_ALLOC`/`NOP`）+ `emit_instruction` default |
| 5 | 致命 | 5｜KRT_IR_CAST 实现为空操作 | `X86Codegen.c:782-787` |
| 6 | 致命 | 6｜栈大小截断导致越界写 | `X86Codegen.c:19-59、163-165、271-273` |
| 7 | 致命 | 7｜未分配 temp 的 load/store 位置不一致 | `X86Codegen.c:259-301` |
| 8 | 致命 | 8｜X86_LIVESET_BITS=256 致高编号 temp 干扰边丢失 | `X86RegAlloc.h:7` + `X86RegAlloc.c:28-36、90-97` |
| 9 | 致命 | 9｜conflict graph 节点/加边失败静默丢失 | `X86RegAlloc.c:253-275` |
| 10 | 致命 | 10｜liveness 数组按无上限容量分配/填充不全 | `X86RegAlloc.c:200-250` |
| 11 | 致命 | 11｜mov-mov 链消除误删仍被使用的 temp 定义 | `X86CodeOpt.c:333-345`（`apply_mov_mov_chain_elimination`） |
| 12 | 致命 | 12｜peephole 优化器全项目死代码 | `X86CodeOpt.c:428`（`x86_optimize_peephole`） |
| 13 | 致命 | 13｜VM 目标只生成 main 函数 | `VmCodegen.c:280-300`（`KrtVmCodegenGenerate`） |
| 14 | 致命 | 14｜VM generate_function switch 大量 opcode 落入 default | `VmCodegen.c:200-280` |
| 15 | 致命 | 15｜VM 带返回值调用多弹一次栈 | `VmCodegen.c:160-180`（`emit_function_call`） |
| 16 | 致命 | 16｜VM temp 索引截断为 uint8 | `VmCodegen.c:40-55`（`emit_value_push` ARG/TEMP） |
| 17 | 致命 | 17｜VM 输出文件名 strcat 越界 | `Compiler.c:208-216` |
| 18 | 致命 | 18｜module_fits_kro_limits 只统计定义数不统计最大索引 | `Compiler.c:108-146` |
| 19 | 致命 | 19｜空遍历死循环 | `Compiler.c:148-160` |
| 20 | 致命 | 20｜module_has_native_pointers 覆盖不全 | `Compiler.c:100-107` |
| 21 | 致命 | 21｜AST→IR 失败返回值被忽略 | `Compiler.c:176`（`KrtIrGenerateFromAst`） |
| 22 | 致命 | 22｜KrtMemoryReportError 为空操作，内存错误全被吞 | `Core/Memory/Allocator.c:425-435`（原标 380-390） |
| 23 | 致命 | 23｜KRT_ASSERT 被编译为 ((void)0) | `Core/Utils/KrtCommon.h:90` |
| 24 | 致命 | 24｜后端改写共享 IR（SSA 优化） | `X86Codegen.c:1019`（`emit_function` 调用 `KrtIrSsaOptimize`） |
| 25 | 致命 | 25｜畸形 IR 操作数未检查 NULL/数量 | `X86Codegen.c:751-772` |
| 26 | 致命 | 26｜codegen_get_var_offset 失败返回 0 与合法 offset 冲突 | `X86Codegen.c:233-257` |
| 27 | 致命 | 27｜param_count>0 但 params==NULL 静默跳过保存 | `X86Codegen.c:900-930`（`emit_function_prologue`） |
| 28 | 致命 | 28｜emit_load_value 字符串常量/宽整数边界缺失 | `X86Codegen.c:340-379` |
| 29 | 致命 | 29｜128 位除零触发 SIGILL | `KroInteger.inc:288-336`（`emit_wide_division`） |
| 30 | 致命 | 30｜128 位移位只检查低 64 位 | `KroInteger.inc:338-390`（`emit_integer_shift`） |
| 31 | 致命 | 31｜未知条件码默认 JNE | `KroInteger.inc:836-851`（`integer_condition`） |
| 32 | 致命 | 32｜KroRegisterPacking 参数/寄存器数组越界 | `KroRegisterPacking.inc:227-292` |
| 33 | 致命 | 33｜ARG 索引≥32 移位 UB | `KroTailUpdates.inc:145-148` |
| 34 | 致命 | 34｜find_temp_location 返回 NULL 未检查即写 | `KroTailCalls.inc:116-118、149-156` |
| 35 | 致命 | 35｜call→ret 链表遍历无环检测 | `KroTailCalls.inc:119-124` |
| 36 | 致命 | 36｜KroLeafReturn 多处未检查操作数数量/NULL | `KroLeafReturn.inc:85-88、98、106-123、162-167` |
| 37 | 致命 | 37｜128 位/非常规宽度浮点转换未支持 | `KroFloatCasts.inc:68-86、88-123` |
| 38 | 致命 | 38｜allocate_packed_field bits=0/负数静默丢字段 | `KroRegisterPacking.inc:2-26` |
| 39 | 致命 | 39｜emit_store_value 未检查 dest->data.name/符号查找失败 | `KroInteger.inc:255-278` |
| 40 | 致命 | 40｜KroInteger 多处未检查 operand_count>=2 | `KroInteger.inc:651-695、739-766、853-863` |
| 41 | 致命 | 41｜definition_block 分配失败后状态未回滚 | `KroRegisterPacking.inc:386` |
| 42 | 致命 | 42｜KRO 后端静默截断超量参数 | `KroCodegen.c:1317` |
| 43 | 致命 | Resolver-1｜import_module 空串致 imports[] 堆溢出写 | `Resolver.c:487-511` |
| 44 | 致命 | BackendCommon-1｜unique_modules calloc 未判空即写入 | `ArkLink/src/Backend/BackendCommon.c:521,524-541` |
| 45 | 致命 | BackendCommon-2｜imports[i].module 为 NULL 时 strcmp 崩溃 | `ArkLink/src/Backend/BackendCommon.c:562` |
| 46 | 致命 | BackendCommon-3｜重定位 offset 加法溢出致越界写 | `ArkLink/src/Backend/BackendCommon.c:386,393,400` |
| 47 | 致命 | BackendCommon-4｜section_count>0 但 sections==NULL 仍取地址 | `ArkLink/src/Backend/BackendCommon.c:250-252` |
| 48 | 致命 | BackendCommon-5｜virtual_address+aligned_size 溢出致镜像布局错位 | `ArkLink/src/Backend/BackendCommon.c:282` |
| 49 | 致命 | BackendPe-1｜section_maps 索引无范围检查 | `ArkLink/src/Backend/BackendPe.c:246,279,311` |
| 50 | 致命 | BackendPe-2｜符号 section_index 越界读 | `ArkLink/src/Backend/BackendPe.c:1087` |
| 51 | 致命 | BackendPe-3｜file_offset 无范围检查且可溢出 | `ArkLink/src/Backend/BackendPe.c:1090` |
| 52 | 致命 | BackendPe-4｜reloc_file_offset 整数下溢导致堆越界写 | `ArkLink/src/Backend/BackendPe.c:1122` |
| 53 | 致命 | BackendPe-5｜导出序号下溢 / 越界写 | `ArkLink/src/Backend/BackendPe.c:629` |
| 54 | 致命 | BackendPe-6｜reloc_count 乘法溢出 | `ArkLink/src/Backend/BackendPe.c:230` |
| 55 | 致命 | BackendPe-7｜block_size/total_size 溢出 | `ArkLink/src/Backend/BackendPe.c:285-289,320` |
| 56 | 致命 | BackendPe-8｜import_count 乘法溢出 + NULL module | `ArkLink/src/Backend/BackendPe.c:364` |
| 57 | 致命 | BackendPe-9｜strlen(NULL) 崩溃 | `ArkLink/src/Backend/BackendPe.c:469-470` |
| 58 | 致命 | BackendPe-10｜export 相关乘法溢出 | `ArkLink/src/Backend/BackendPe.c:561,565,570-572,588,596` |
| 59 | 致命 | BackendPe-11｜section_count 乘法溢出 | `ArkLink/src/Backend/BackendPe.c:685,789` |
| 60 | 致命 | BackendPe-12｜头尺寸计算溢出/截断 | `ArkLink/src/Backend/BackendPe.c:743-748` |
| 61 | 致命 | BackendPe-13｜RVA 计算溢出 | `ArkLink/src/Backend/BackendPe.c:726-728,819-821` |
| 62 | 致命 | BackendPe-14｜SizeOfCode 等累加溢出 | `ArkLink/src/Backend/BackendPe.c:879-886,890-896` |
| 63 | 致命 | BackendPe-15｜section 文件偏移/RVA 回绕 | `ArkLink/src/Backend/BackendPe.c:967,1006,1026` |
| 64 | 致命 | BackendPe-16｜两套头尺寸计算分叉 | `ArkLink/src/Backend/BackendPe.c:922-923` |
| 65 | 致命 | BackendPe-17｜current_rva 初值与 layout 口径不一致 | `ArkLink/src/Backend/BackendPe.c:959-960` |
| 66 | 致命 | BackendPe-18｜section size 截断为 uint32_t | `ArkLink/src/Backend/BackendPe.c:962-1008` |
| 67 | 致命 | ELF-1｜imports 的 symbol/module 为 NULL 时 strcmp 崩溃 | `ArkLink/src/Backend/BackendElf.c:533,536` |
| 68 | 致命 | ELF-2｜static_loads 上限 48 静默丢段 | `ArkLink/src/Backend/BackendElf.c:907-909` |
| 69 | 致命 | ELF-3｜动态段 PLT 标签覆盖既有表项 | `ArkLink/src/Backend/BackendElf.c:1502-1514` |
| 70 | 致命 | ELF-4｜e_phnum 少算 2 | `ArkLink/src/Backend/BackendElf.c:1608-1609` |
| 71 | 致命 | ELF-5｜DT_HASH nchain ≠ dynsym 项数 | `ArkLink/src/Backend/BackendElf.c:1726-1731` |
| 72 | 致命 | ELF-6｜import_count 乘法溢出 → 堆溢出 | `ArkLink/src/Backend/BackendElf.c:523` |
| 73 | 致命 | ELF-7｜elf_shnum 溢出 / 截断 | `ArkLink/src/Backend/BackendElf.c:552,1573-1574` |
| 74 | 致命 | ELF-8｜段 data NULL / 重定位失败静默跳过 | `ArkLink/src/Backend/BackendElf.c:676-678,684-685` |
| 75 | 致命 | ELF-9｜shstrtab 14 个 sb_add 只检查 3 个 | `ArkLink/src/Backend/BackendElf.c:611-615` |
| 76 | 致命 | ELF-10｜layout 查询返回 NULL 时地址全为 0 | `ArkLink/src/Backend/BackendElf.c:410-434` |
| 77 | 致命 | B5-1｜build_project_independent 完全忽略 output_path | `Re.KrtC/src/Main.c:909-914` |
| 78 | 致命 | B5-2｜目标文件缺失只报错不中止 | `Re.KrtC/src/Main.c:856-860` |
| 79 | 致命 | B5-3｜WriteByte 的 KRT_REALLOC 失败未检查 | `Re.KrtC/src/Shared/BytecodeGenerator.c:120-121` |
| 80 | 致命 | B5-4｜chunk->count+1 / capacity*2 整数溢出 | `Re.KrtC/src/Shared/BytecodeGenerator.c:124-126` |
| 81 | 致命 | B6-1｜BSS 重定位从不写入文件且 total_reloc_count 漏算 bss | `Re.KrtC/src/Shared/KroWriter.c:537-557` |
| 82 | 致命 | B6-2｜写文件失败路径静默且 fclose 未检查 | `Re.KrtC/src/Shared/KroWriter.c:476-567` |
| 83 | 致命 | B6-3｜kro_add_reloc 丢弃 add_reloc_to_list 的失败 | `Re.KrtC/src/Shared/KroWriter.c:346` |
| 84 | 致命 | B6-4｜容量翻倍无溢出检查 | `Re.KrtC/src/Shared/KroWriter.c:236,289,361,154` |
| 85 | 致命 | B6-5｜count * element_size 乘法溢出 | `Re.KrtC/src/Shared/SmartPtr.c:59,97` |
| 86 | 致命 | B6-6｜KrtArkLinkAddObjectFile 返回值被忽略 | `Re.KrtC/src/Shared/ArkLinkIntegration.c:286` |
| 87 | 致命 | B6-7｜add_input 失败时已 strdup 的槽位泄漏 | `Re.KrtC/src/Shared/ArkLinkIntegration.c:123-127` |
| 88 | 致命 | 7-1｜类上下文入栈失败被静默丢弃，后续解析沿用错误上下文 | `SemanticAnalyzer.c:421-440` |
| 89 | 致命 | 7-2｜imported_file 注册：未注册却报告成功 | `SemanticAnalyzer.c:170-177` |
| 90 | 致命 | 9-1｜`infer_expression_type` 所有失败路径统一降级为 `TOKEN_INT32` | `SemanticAnalyzer.c:708-803` |
| 91 | 致命 | 9-2｜三种错误上报函数行为互不一致且 `add_error_at` 对用户不可见 | `SemanticAnalyzer.c:846-860`（只写 report）、`:805-816`（只写 OutputCache）、`:818-835`（两者都写但打印不带行列） |
| 92 | 致命 | 9-3｜legacy mangling 手工 `__` 拼接 + snprintf 截断 | `SemanticAnalyzer.c:730-745` |
| 93 | 致命 | 10-1｜参数个数不同的重载可能被判分并选中 | `SemanticAnalyzer.c:937-1010`（`semantic_parameter_score`） |
| 94 | 致命 | 10-2｜`_ZN...` 前缀 `snprintf` 截断无检查 → 可能匹配错误符号 | `SemanticAnalyzer.c:1012-1061` |
| 95 | 致命 | 10-3｜`TOKEN_POWER` 无优先级，`**` 永不被当作二元运算符 | `ParserExpression.c:620-657` |
| 96 | 致命 | 10-4｜`var_name[0]` 在 `var_name == NULL` 时解引用 | `SemanticAnalyzer.c:1096-1099` |
| 97 | 致命 | 11-1｜`**` 语义前后端冲突（前端双解引用 vs 后端 `KrtIrPow`） | `IrGen.c:1468-1469` ↔ `ParserExpression.c:620-657`、`ParserStatement.c:197-210` |
| 98 | 致命 | 11-2｜赋值名 `KRT_STRDUP` 返回值与源指针均未判空 | `ParserStatement.c:540-548` |
| 99 | 致命 | 11-3｜表达式形式 `AST_UNSAFE_CALL` 完全忽略 permissions | `SemanticAnalyzer.c:1124-1130` |
| 100 | 致命 | 12-1｜作用域链遍历起点错误导致子作用域全部泄漏 | `SymbolTable.c:123-153`（`destroy` 从 `global_scope` 起，而 `push_scope` 把 `new_scope->parent = current_scope`，global 是链尾 parent==NULL） |
| 101 | 致命 | 12-2｜符号查找只查表级哈希 + 作用域链失败回退全表哈希 | `SymbolTable.c:330-346`；`declare`（`:249-251`）无条件写表级哈希；`lookup_scope_chain`（`:354-368`）回退全表哈希 |
| 102 | 致命 | 12-3｜两个 SymbolEntry 共享同一 nested_table | `SemanticAnalyzer.c:2470-2471`（`self_class_symbol->nested_table = class_sym->nested_table`） |
| 103 | 致命 | 13-1｜SymbolTable 大量接口无人调用，连带 error_count 不影响成败 | `SymbolTable.h:79-82` + `SymbolTable.c:439-466` |
| 104 | 致命 | 13-2｜同名重载的第二个方法被静默跳过 | `SemanticAnalyzer.c:3894-3905` |
| 105 | 致命 | 13-3｜`enter_nested_scope` 永久改变 current_scope，错误路径脆弱 | `SemanticAnalyzer.c:3826-3909` |
| 106 | 致命 | F14-1｜`global::` 调用名被指针前移，AST 销毁时释放内部指针 | `SemanticAnalyzer.c:1621-1625`；`Ast.c:205-207`；`Allocator.c:276-290` |
| 107 | 致命 | F14-2｜`semantic_expression_impl` 的 `default: return true;` | `SemanticAnalyzer.c:1601-1602` |
| 108 | 致命 | F14-3｜解析器从不产出但 IrGen 实现了的节点类型 = 死代码 | `IrGen.c:2193/2198/2200-2201/2203`；枚举在 `Ast.h`，销毁分支在 `Ast.c` |
| 109 | 致命 | F15-1｜`strcmp(method_symbol->name, mangled_name)` 中 `mangled_name` 可为 NULL | `SemanticAnalyzer.c:1749`（同类模式 `:1816`） |
| 110 | 致命 | F16-1｜全局函数调用解析成功后覆盖 `call.name` 未释放旧指针 | `SemanticAnalyzer.c:1901-1905` |
| 111 | 致命 | F16-2｜泛型多参数被静默截断 + 缺 `>` 不报错 | `SemanticAnalyzer.c:1944-1998` |
| 112 | 致命 | F17-1｜复合赋值路径使用未初始化的 `stmt->resolved_type` | `SemanticAnalyzer.c:2180` |
| 113 | 致命 | F18-1｜重开已存在命名空间时函数体完全不分析 | `SemanticAnalyzer.c:2382-2393` |
| 114 | 致命 | F18-2｜命名空间体类型判断冗余致分支永不可达 | `SemanticAnalyzer.c:2362-2411` |
| 115 | 致命 | F20-1｜静态方法跨作用域链复用符号致原地污染 | `SemanticAnalyzer.c:2718` + `:2727` |
| 116 | 致命 | F21-1｜语句 dispatcher default 静默返回 true | `SemanticAnalyzer.c:2973-2974` |
| 117 | 致命 | F22-1｜type_info->declaration 为 NULL 时实例化静默成功 | `SemanticAnalyzer.c:3150-3160` |
| 118 | 致命 | F23-1｜函数声明两条并行分析路径语义分叉 | `SemanticAnalyzer.c:3431-3439` vs `:2620-2701` / `:3441-3458` |
| 119 | 致命 | S28-1｜数组字面量元素步长语义分叉【已作废（G2）】 | `Re.KrtC/src/Ir/IrGen.c:1841-1855`（表达式路径）vs `:2742-2760`（变量初始化路径） |
| 120 | 致命 | S29-1｜try/catch/finally/throw 完全无语义，catch 在正常路径也执行 | `Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2719-2736`（`irgen_stmt_try_statement`）、`:2711-2717`（`irgen_stmt_throw_statement`）、`:478-480`（`irgen_throw_exception`） |
| 121 | 致命 | S29-2｜switch 分派完全失效，仅 case 1/2 偶然正确 | `Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2561-2629`（`irgen_stmt_switch_statement`），缺陷核心 `:2596-2604` |
| 122 | 致命 | S30-1｜`function <类型> 名()` 被静默误路由，函数体按「顶层作用域」解析 | `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:1639-1648`（`case TOKEN_FUNCTION`）、`:451-458`（`parser_parse_type_inferred_declaration`）、`Re.KrtC/src/Compiler/Frontend/Parser/Parser.c:24-33` |
| 123 | 致命 | S30-2｜ArkLink：空模块时 `free` 后未置 NULL 即返回 OK → use-after-free + double-free → SIGSEGV | `ArkLink/src/Core/Resolver.c:314-319`（`ark_resolver_resolve` 的 `total_sections == 0` 分支）；触发点 `ArkLink/src/Core/LinkerNative.c:87-95`；崩溃点 `ArkLink/src/Core/Resolver.c:582`（`ark_resolver_plan_destroy`） |
| 124 | 致命 | S31-1｜数组字面量步长语义分叉：表达式路径硬编码 8 字节，声明路径用真实元素大小 | `Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:1841-1855`（`irgen_expr_array_literal`）对比 `:2745-2758`（`irgen_stmt_variable_declaration` 的 `is_array_literal` 分支）；底层硬编码在 `Re.KrtC/src/Compiler/Middle/Ir/Ir.c:1136-1139`（`KrtIrArrayStore` 恒 `element_size=8`） |
| 125 | 致命 | S32-1｜访问控制（`private`/`protected`）完全未实现，外部代码可读写私有成员 | `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:156-164`（`while (... TOKEN_PUBLIC || TOKEN_PRIVATE || TOKEN_PROTECTED ...)` 只 `parser_advance()` 吞掉修饰符，从不写入 AST）；`Re.KrtC/src/Compiler/Frontend/Parser/Ast.h:357` 的 `access_modifier` 字段无任何生产者；`SemanticAnalyzer.c` 全文无 `E_ACCESS` |
| 126 | 致命 | S32-2｜`base` 表达式与 `:base(...)` 构造初始化器未实现 | `Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（`base` 不作关键字处理，`AST_THIS` 仅在 `:321` 为 `this` 产生）；`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:30-130`（`parser_parse_function_declaration` 在 `)` 后直接要求 `{`，不识别 `:base(...)`） |
| 127 | 致命 | S32-3｜构造/析构/属性 AST 节点类型从不产生，IrGen 对应实现整体为死代码，基类构造链缺失 | `Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2941-2944`（`AST_ACCESS_MODIFIER`）、`:2946-2990`（`AST_CONSTRUCTOR_DECLARATION`）、`:2991-3017`（`AST_DESTRUCTOR_DECLARATION`）、`:3072-3130` 与 `:3145-3203`（`AST_PROPERTY_DECLARATION`）；生产者缺失见 `Re.KrtC/src/Compiler/Frontend/Parser/` 全目录（四类节点零次创建） |
| 128 | 致命 | S33-1｜`KrtIrGenerateFromAst` 后三个循环与尾部统计循环无 NULL 守卫 → SIGSEGV | `Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:3563-3564`、`:3574-3575`、`:3594-3595`、`:3609-3610` |
| 129 | 致命 | S34-1｜`ir_create_inst` 不检查块是否已有终结指令（S29-2 的 IR 层根因） | `Re.KrtC/src/Compiler/Middle/Ir/Ir.c:855-866`（`KrtIrJump`）、`:867-881`（`KrtIrBranch`）、`:882-894`（`KrtIrReturn`）、`:315-358`（`ir_create_inst`） |
| 130 | 致命 | S36-1｜死代码（整文件约 500 行零调用者）+ 语义分叉 | `IrMemory.c:160-852`（对象池 `pool_init`~`KrtIrPoolPrintStats`、惰性分配 `lazy_create_op`~`KrtIrLazyPrintStats`、性能统计 `KrtIrProfilerInit`~`KrtIrCountersPrint`）；声明于 `IrMemory.h:33-110` |
| 131 | 致命 | S37-1｜配置漂移 + 语义分叉（三条后端对同一 IR 跑不同的优化管线） | `Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c:1296`（`// KrtIrSsaOptimize(func);  //禁用SSA优化,除了Airs_td以外的人不要打开它,SSA不稳定`）、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.c:1019`（`KrtIrSsaOptimize(func);`）、`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.c:220`（`KrtIrSsaOptimize(func);`） |
| 132 | 致命 | S40-1｜DCE 的 `KRT_FREE(inst)` 与 arena 分配的 inst 冲突 → 堆破坏（启用 SSA 后触发） | `IrOptimizer.c:336-340`（`KRT_FREE(inst->operands); KRT_FREE(inst);`）；对照 `Ir.c:334`（`ir_create_inst` 用 `KRT_CALLOC`）与 `IrSsa.c:1229`（`move_inst` 用 `KrtIrArenaAlloc`） |
| 133 | 致命 | S41-1｜强度削减把 `x*0`/`x^0` 改成 IMM 时只写 `data.imm` 不改 `type` → 后端读 union 错误字段 | `IrOptimizer.c:511-514`（MUL val==0）、`:534-537`（POW exp==0） |
| 134 | 致命 | S43-1｜`generics_build_instantiated_name` 对 `type_args[i]` 无 NULL 检查 → 空指针崩溃 | `Generics.c:245`（`strlen(type_args[i])`）、`:257-259`（`KRT_STRCAT_S(result, total_size, type_args[i])`） |
| 135 | 致命 | S43-2｜`generics_instantiate_type` 的 `total_size` 低估 → `KRT_STRCAT_S` 缓冲区溢出 | `Generics.c:137-146`（计算）vs `:162-168`（拼接） |
| 136 | 致命 | S44-1｜未定义函数调用被语义分析静默接受，诊断与实际错误不符、无位置信息 | `Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:356-368`（`method` 在 `local_scope`/`global_scope` 都找不到、fallback 也找不到 → 直接 `break`，无任何错误）；`Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.c:75-78`（`if (!method_symbol) { KRT_FREE(mangled_name); return true; }` → 静默成功） |
| 137 | 致命 | S44-2｜赋值类型兼容性对非标量不检查 → 隐式截断/溢出无诊断 | `Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:40-63`（`pointer_assignable` 对非指针/非字符串/非 IDENTIFIER 的标量对一律 `return true`）；`SemanticAnalyzer.c:109-117`（AST_ASSIGNMENT 只查符号存在）；`SemanticAnalyzer.c:1135-1169`（二元 ASSIGN 只查 lvalue/字符串拼接/只读成员，不查类型） |
| 138 | 致命（原文自降级为疑似） | S47-1｜`link_kro_executable` 在 `import_kro_files` 分配失败时仍继续 | `Re.KrtC/src/Main.c:259-313`（:260-312 循环中 `imports_ok=0` 后 `break`，:315 `total_obj_count = 1 + import_kro_count` 仍按已完成的 `import_kro_count` 计算）；:316-328（`obj_files` 分配后只填 `import_kro_count` 个）；:330-341、:355；调用方 `compile_single_file:476` |
| 139 | 致命 | S47-2｜`compile_multiple_files` 在 `parsed_count != input_count` 时释放 `source` 但 `ast` 未释放 | `Re.KrtC/src/Main.c:720-732`（:721-725 只 `free(source)`、`lexer_destroy`、`parser_destroy`，但 `parsed_files[i].ast` 未 `ast_destroy`） |
| 140 | 致命 | S47-3｜`compile_multiple_files` 在 `semantic_analyzer_create` 失败时同样泄漏 AST | `Re.KrtC/src/Main.c:734-746` |
| 141 | 致命 | S55-1｜`Error.h` 的 include guard 撞名导致 `StackCalculator.h` 被跳过 | `Re.KrtC/src/Core/Utils/Error.h:1-2` + `StackCalculator.h:1-2` + `Core.h:13,15` |
| 142 | 致命 | S57-2｜分配失败系统性静默（零诊断） | `Re.KrtC/src/Core/Memory/Allocator.c:159-211,213-227,229-261,263-274` + `KrtCommon.h:77-81` |
| 143 | 致命 | 58-1｜`output_file` 参数完全未使用，写盘走的是 `output_filename` | `KroCodegen.c:1470`（签名）、`1478`（仅存 `ctx.output_file`）、实际写盘 `1506`、`1615`；对照 `KroCodegen.h:87` 注释 “Emit a KRO object for module into output_file; output_filename names diagnostics.” |
| 144 | 致命 | 58-2｜`kro_write_file` 失败被空 `if` 吞掉，且函数返回 void | `KroCodegen.c:1615-1616`（`if (!kro_write_file(...)) { }`），另 `1506` 直接忽略返回值；返回类型 `void` 见 `1470`。 |
| 145 | 致命 | 58-3｜`string_const_sym_indices[]` 存在未初始化元素 | `KroCodegen.c:1418`（`KRT_MALLOC`，非 CALLOC）、`1426-1428`（`if (!str) { continue; }`）、`1439-1440`（仅非空字符串才赋值）；消费点 `685-688`。 |
| 146 | 致命 | 60-1｜跨块临时寄存器分析中 KRT_MALLOC 失败被静默吞掉 | `Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:386-387` |
| 147 | 致命 | 60-2｜跨块布尔表 KRT_CALLOC 失败被静默吞掉 | `Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:456-457` |
| 148 | 致命 | 61-1｜float→宽整数转换越界/NaN/无符号负数静默出错 | `Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc:88-123`（关键：`:91-93`、`:111-118`） |
| 149 | 致命 | 62-1｜UTF-8 前导字节触发无限递归，栈溢出崩溃 | `Tokenizer.c:798-820`（关键 810-819） |
| 150 | 致命 | 62-2｜字符字面量 arena 分配失败时返回指向栈局部数组的悬垂指针 | `Tokenizer.c:409-415`（`char value[2]` 定义于 349） |
| 151 | 致命 | 62-3｜字符串字面量尾部反斜杠越界读（越过 NUL） | `Tokenizer.c:441-513`（关键 443、509、431） |
| 152 | 致命 | 62-4｜arena 分配失败静默伪装成 EOF（数字/标识符路径，G1） | `Tokenizer.c:305-308`（数字）、`Tokenizer.c:326-329`（标识符） |
| 153 | 致命 | 62-5｜arena 分配/realloc 失败静默伪装成 EOF（字符串路径，G1；含前人已指出 428/436） | `Tokenizer.c:426-429`（初始分配）、`Tokenizer.c:434-437`（扩容） |
| 154 | 致命 | 63-1｜name_demangle 长度字段无边界，memcpy 越界读堆 | `NameMangling.c:328-332`（长度累加）、`NameMangling.c:358`（`memcpy(name, ptr, (size_t)length)`） |
| 155 | 致命 | 63-2｜编解码不对称：函数参数编码被追加在 `E` 终止符之后，demangle 遇到 `E` 即停 | `NameMangling.c:263-270`（把参数码写在 `base_mangled` 尾部，而 `base_mangled` 已含 `E`）、`NameMangling.c:326,364`（读侧 `while (*ptr && *ptr != 'E')` 后 `ptr++` 直接返回） |
| 156 | 致命 | 63-3｜同一文件并存三套互不兼容的 mangling 方案，且只有 `_Z` 方案有解码器 | `NameMangling.c:99-115`（`class__member`）、`NameMangling.c:117-135`（`class__constructor_N`）、`NameMangling.c:137-179`（`_ZN...E`） |
| 157 | 致命 | 63-4｜类型编码碰撞：`int8` 与 `char` 同为 `"c"`，`string` 与 `type_string` 同为 `"r"` | `NameMangling.c:21-22`（`TOKEN_INT8 → "c"`）、`NameMangling.c:43-44`（`TOKEN_CHAR → "c"`）、`NameMangling.c:45-47`（`TOKEN_STRING/TOKEN_TYPE_STRING → "r"`） |
| 158 | 致命 | 63-5｜构造名与数据成员名碰撞：`A__constructor_2` | `NameMangling.c:126,133`（`%s__constructor_%s`）、`NameMangling.c:113`（`%s__%s`）；调用方 `IrGen.c:1207,2961,3040` 与 `IrGen.c:690` |
| 159 | 致命 | 64-1｜数组赋值对 arena AST 节点调用 KRT_FREE（非法释放） | `ParserStatement.c:537` |
| 160 | 致命 | 64-2｜print 参数数组 arena 分配后用 KRT_REALLOC/KRT_FREE（分配器混用） | `ParserStatement.c:612`、`618`、`640`、`650`、`660` |
| 161 | 致命 | 64-3｜switch 子句数组/语句数组 KRT_REALLOC 未检查 | `ParserStatement.c:821`、`839`、`864` |
| 162 | 致命 | 64-4｜using 指令 path_parts 分配/追加/扩容未检查 | `ParserStatement.c:1462`、`1468`、`1476`、`1478`、`1483`、`1486` |
| 163 | 致命 | 64-5｜block 语句数组 KRT_MALLOC 未检查 | `ParserStatement.c:1197` |
| 164 | 致命 | 64-7｜标识符赋值 KRT_STRDUP 未检查 | `ParserStatement.c:549`、`552` |
| 165 | 致命 | 65-1｜表达式解析失败普遍不报告诊断，error_count 不增长导致流水线静默继续 | `ParserExpression.c:100-102`、`107-109`、`185-192`、`196-208`、`247-254`、`396-399`、`438-447`、`483-489`、`521-523`、`570-572`、`593-604`、`618`、`786-789`、`807-811`、`860-869` |
| 166 | 致命 | 65-2｜`as` / `??` / `?.` / `?:` 有词法 token 但表达式语法完全不处理 | `ParserExpression.c:625-662`（优先级表无这些 token）、`758-846`（后缀无 `?.`）、`848-890`（三元无 `?:`） |
| 167 | 致命 | 65-3｜深嵌套表达式递归无深度上限 | `ParserExpression.c:392`（括号）、`418`（数组元素）、`520/548/597`（new）、`802`（下标）、`859/872`（三元）、`705→669→734`（二元/一元互递归） |
| 168 | 致命 | 65-4｜命名空间 parts 元素 `KRT_STRDUP` 未检查，后续 `strlen/strcat` 空指针崩溃 | `ParserAdvanced.c:184`、`196`、`204` |
| 169 | 致命 | 65-5｜类名 `KRT_STRDUP` 未检查且失败静默、token 已前进 | `ParserAdvanced.c:25`、`31-33` |
| 170 | 致命 | 66-1｜arena 节点提前返回，堆字符串/堆数组全部泄漏 | `Ast.c:77-79` |
| 171 | 致命 | 66-2｜destroy 与 clone/visit 覆盖不对齐（总纲） | `Ast.c:81-565`（destroy switch）对照 `Ast.h:9-120`（枚举）、`AstVisit.inc:6-408`、`AstClone.inc:128-549` |
| 172 | 致命 | 66-3｜destroy 缺 AST_ASYNC_FUNCTION | `Ast.c:81-565` 无此 case；字段见 `Ast.h:109,194-207`；visit/clone 按 function_decl 处理（`AstVisit.inc:19`、`AstClone.inc:142`） |
| 173 | 致命 | 66-4｜destroy 缺 AST_PROGRAM | `Ast.c` 无此 case；`Ast.h:9`；visit/clone 用 `data.block`（`AstVisit.inc:106-111`、`AstClone.inc:240-242`） |
| 174 | 致命 | 66-5｜destroy 缺 AST_TERNARY_OPERATION | `Ast.c` 无此 case；`Ast.h:24,287-291`；visit 见 `AstVisit.inc:95-99` |
| 175 | 致命 | 66-6｜destroy 缺 AST_NEW_EXPRESSION | `Ast.c` 无此 case；`Ast.h:33,325-328`；visit 见 `AstVisit.inc:126-130` |
| 176 | 致命 | 66-7｜destroy 缺 AST_NEW_ARRAY_EXPRESSION | `Ast.c` 无此 case；`Ast.h:34,332-333`；visit 见 `AstVisit.inc:131-133` |
| 177 | 致命 | 66-8｜destroy 缺 AST_NAMESPACE_DECLARATION | `Ast.c` 无此 case；`Ast.h:35,336-337`；visit 见 `AstVisit.inc:134-136` |
| 178 | 致命 | 66-9｜destroy 缺 AST_DEFAULT_CLAUSE | `Ast.c` 无此 case；`Ast.h:63,432-434`；visit 见 `AstVisit.inc:211-215` |
| 179 | 致命 | 66-10｜destroy 缺 AST_BREAK_STATEMENT | `Ast.c` 无此 case；`Ast.h:64,435-438`；visit 见 `AstVisit.inc:216-218` |
| 180 | 致命 | 66-11｜destroy 缺 AST_CONTINUE_STATEMENT | `Ast.c` 无此 case；`Ast.h:65,439-441`；visit 见 `AstVisit.inc:219-221` |
| 181 | 致命 | 66-12｜destroy 缺 AST_DELETE_STATEMENT | `Ast.c` 无此 case；`Ast.h:66,442-446`；visit 见 `AstVisit.inc:222-224` |
| 182 | 致命 | 66-13｜destroy 缺 AST_LINQ_LET | `Ast.c` 无此 case；`Ast.h:82,524-526`；visit 见 `AstVisit.inc:280-282` |
| 183 | 致命 | 66-14｜destroy 缺 AST_LINQ_GROUP | `Ast.c` 无此 case；`Ast.h:83,528-531`；visit 见 `AstVisit.inc:283-286` |
| 184 | 致命 | 66-15｜destroy 缺 AST_SIZEOF_EXPRESSION | `Ast.c` 无此 case；`Ast.h:91,568-570`；visit 见 `AstVisit.inc:316-317` |
| 185 | 致命 | 66-16｜destroy 缺 AST_STACKALLOC_EXPRESSION | `Ast.c` 无此 case；`Ast.h:92,572-575`；visit 见 `AstVisit.inc:318-320` |
| 186 | 致命 | 66-17｜destroy 缺 AST_INTERPOLATED_STRING | `Ast.c` 无此 case；`Ast.h:93,577-581`；visit 见 `AstVisit.inc:321-326` |
| 187 | 致命 | 66-18｜destroy 缺 AST_TUPLE_TYPE | `Ast.c` 无此 case；`Ast.h:94,583-585`；visit 见 `AstVisit.inc:327-328` |
| 188 | 致命 | 66-19｜destroy 缺 AST_TUPLE_EXPRESSION | `Ast.c` 无此 case；`Ast.h:95,587-589`；visit 见 `AstVisit.inc:329-333` |
| 189 | 致命 | 66-20｜destroy 缺 AST_TUPLE_ELEMENT_ACCESS | `Ast.c` 无此 case；`Ast.h:96,591-593`；visit 见 `AstVisit.inc:334-336` |
| 190 | 致命 | 66-21｜destroy 缺 AST_MATCH_EXPRESSION | `Ast.c` 无此 case；`Ast.h:97,595-598`；visit 见 `AstVisit.inc:337-342` |
| 191 | 致命 | 66-22｜destroy 缺 AST_PATTERN_CASE | `Ast.c` 无此 case；`Ast.h:98,600-603`；visit 见 `AstVisit.inc:343-347` |
| 192 | 致命 | 66-23｜destroy 缺 AST_PATTERN_LITERAL | `Ast.c` 无此 case；`Ast.h:100,607-608`；visit 见 `AstVisit.inc:350-352` |
| 193 | 致命 | 66-24｜destroy 缺 AST_PATTERN_VARIABLE | `Ast.c` 无此 case；`Ast.h:101,610-611`；visit 见 `AstVisit.inc:353-354` |
| 194 | 致命 | 66-25｜destroy 缺 AST_PATTERN_TUPLE | `Ast.c` 无此 case；`Ast.h:102,613-615`；visit 见 `AstVisit.inc:355-359` |
| 195 | 致命 | 66-26｜destroy 缺 AST_POINTER_DEREFERENCE | `Ast.c` 无此 case；`Ast.h:104,620-622`；visit 见 `AstVisit.inc:366-368` |
| 196 | 致命 | 66-27｜destroy 缺 AST_POINTER_ASSIGNMENT | `Ast.c` 无此 case；`Ast.h:120,623-627`；visit 见 `AstVisit.inc:362-365` |
| 197 | 致命 | 66-28｜destroy 缺 AST_ADDRESS_OF | `Ast.c` 无此 case；`Ast.h:105,628-630`；visit 见 `AstVisit.inc:369-371` |
| 198 | 致命 | 66-29｜destroy 缺 AST_FIXED_STATEMENT | `Ast.c` 无此 case；`Ast.h:106,631-635`；visit 见 `AstVisit.inc:372-375` |
| 199 | 致命 | 66-30｜destroy 缺 AST_AWAIT_EXPRESSION | `Ast.c` 无此 case；`Ast.h:110,639-641`；visit 见 `AstVisit.inc:379-381` |
| 200 | 致命 | 66-31｜destroy 缺 AST_NULLABLE_TYPE | `Ast.c` 无此 case；`Ast.h:115,658-661`；visit 见 `AstVisit.inc:392-394` |
| 201 | 致命 | 66-32｜destroy 缺 AST_NULL_COALESCING | `Ast.c` 无此 case；`Ast.h:116,662-665`；visit 见 `AstVisit.inc:395-398` |
| 202 | 致命 | 66-33｜destroy 缺 AST_NULL_CONDITIONAL | `Ast.c` 无此 case；`Ast.h:117,666-671`；visit 见 `AstVisit.inc:399-402` |
| 203 | 致命 | 66-34｜destroy 缺 AST_CAST_EXPRESSION | `Ast.c` 无此 case；`Ast.h:118,672-676`；visit 见 `AstVisit.inc:403-405` |
| 204 | 致命 | 67-1｜DIV/MOD 被当作纯操作，trap 语义被改变 | `IrRecursiveInline.inc:98`（配合 `IrOptimizer.c:67-90`；`IrInline.inc:33` 同因） |
| 205 | 致命 | 67-2｜recursive_value 未命中静默回退，提交时原块已被释放 | `IrRecursiveInline.inc:184`（`return value;` 原样返回）；`:573`（`recursive_destroy_blocks(function->entry_block)`） |
| 206 | 致命 | 68-1｜null 指针被折叠为长度 0，对 null 的 Clear/Copy/Fill/Reverse 静默返回 true | `Re.KrtC/stdlib/Array.krt:8-12`（`get_length` 对 `data == null` 返回 0）、`:70-74`、`:77-79`、`:95-97`、`:107-109`、`:126-128`、`:148-150` |
| 207 | 致命 | 68-2｜get_length 无数组头校验，对裸缓冲区/野指针读 data-8，静默错误长度或越界读崩溃 | `Re.KrtC/stdlib/Array.krt:13-27` |
| 208 | 致命 | 69-1｜同仓库存在三套 stdlib 副本，API 与实现语义分叉 | `Re.KrtC/src/Runtime/Krt/String.krt:8,13,19,45,88,146,183,223,256`；`Re.KrtC/src/Runtime/Krt/Math.krt:8,11,18,49,57,71,89,107,121`；`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:11,15,30,54,64,71,84,98,132,153,159,180,206`（对比 `Re.KrtC/stdlib/String.krt:9`、`Math.krt:5`、`StringBuilder.krt:12`） |
| 209 | 致命 | 69-2｜FreeBuffer 的容量由调用方回传，错配即堆损坏；负容量静默跳过释放 | `Re.KrtC/stdlib/StringBuilder.krt:33-39`（关键 35-37） |
| 210 | 致命 | 69-3｜Remove 无 buffer_size 形参，current_length 无上界校验，可越界读写 | `Re.KrtC/stdlib/StringBuilder.krt:174-192`（校验仅 176，写终止符 189） |
| 211 | 致命 | 70-9｜ToString 重载所有权不一致，统一释放会导致非法 munmap | `Re.KrtC/stdlib/Convert.krt:150-172` |
| 212 | 致命 | 71-1｜FromInt64 对 int64.MinValue 返回 "-" | `Re.KrtC/src/Runtime/Krt/String.krt:100-104`、`115-137` |
| 213 | 致命 | 71-12｜AppendLine 部分写入后返回 -1，状态与返回值不一致 | `Re.KrtC/src/Runtime/Krt/StringBuilder.krt:84-89` |
| 214 | 致命 | 71-24｜Write(char)/WriteLine(char) 输出十进制码点而非字符 | `Re.KrtC/src/Runtime/Krt/Console.krt:78-87` |
| 215 | 致命 | 71-32｜ToInt32/ToInt64 溢出静默回绕，无溢出检测 | `Re.KrtC/src/Runtime/Krt/Convert.krt:33`、`64` |
| 216 | 致命 | 82-1｜foreach 的 let/readonly 循环变量丢失 readonly 标志 | `SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt:1405-1419`（缺 `variable.native_readonly = original.native_readonly`）；对照 `Parser.krt:2236` |
| 217 | 致命 | 83-1｜整数溢出绕过 op 50/51/53/54 的 locals 边界校验，造成负偏移越界读写 | `Interpreter.krt:91-92`（校验）、`Interpreter.krt:806`（op 52 写）、`Interpreter.krt:810`（op 53/54 读） |
| 218 | 致命 | 84-1｜模块接口 original/transformed 别名被就地清空，重复导入误报冲突 | `SelfHost/Compiler/Driver/Driver.krt:544`（`file.original=data;file.transformed=data;`）、`:537`（比较 `previous.original`）、`:1107-1109`（清空 `transformed`） |
| 219 | 致命 | 88-1｜KrtCompileSource 对 null 的 unit 调用语义校验导致崩溃 | `SelfHost/Compiler/Pipeline/CompilerPipeline.krt:376`（关联 352-358、380-381） |
| 220 | 致命 | 90-1｜Abs(int32) 对 int32.MinValue 取负溢出，静默返回原负数 | `Re.KrtC/src/Runtime/Krt/Math.krt:13` |
| 221 | 致命 | 90-2｜InternalInt32ToString 对 int32.MinValue 生成字符串 `"-"`，静默错误 | `Re.KrtC/src/Runtime/Krt/Sys.krt:63-64`（叠加 74-80） |
| 222 | 致命 | 90-3｜Memory.Allocate 把 mmap 错误码当作指针返回，失败静默 | `Re.KrtC/src/Runtime/Krt/Memory.krt:16` |
| 223 | 致命 | 91-1｜encode_utf8 不校验码点合法域，静默产出非法 UTF-8 | `Re.KrtC/src/Accelerator.c:112-144`（关键分支 113-118、134-142）；声明 `Accelerator.h:24` |
| 224 | 致命 | 94-1｜`KrtBuildExecute` 返回约定未定义，且实现与调用方判断相反 | `Re.KrtC/src/Compiler/Build/BuildSystem.h:19` |
| 225 | 致命 | 95-1｜聚合头与 Driver 头共用包含守卫 KRT_COMPILER_H，导致 Compiler.h 被整体吞掉 | `Re.KrtC/src/Compiler/Compiler.h:1-2` 与 `Re.KrtC/src/Compiler/Driver/Compiler.h:1-2`（聚合头在第 13 行包含 Driver 头） |
| 226 | 致命 | 97-1｜空循环体可被优化器整段消除，断言无法发现，基准测量为零仍通过 | `Test/Performance/Runtime/test_empty_loop.krt:22-24` |
| 227 | 致命 | 99-1｜C72_lambda 用占位符 "?" 断言，lambda 与 Apply 从未被调用 | `cases/C72_lambda.krt:4-7` |
| 228 | 致命 | 99-2｜cases/ 下 76 个用例全为正向程序，语法错误路径零覆盖 | `cases/*.krt` 全部 76 个（无任何负向用例）；配套 `cases/*.exp` 存在但不在授权读取范围 |
| 229 | 致命 | 99-3｜返回码断言无映射且 stdout 为空，框架若只比 stdout 则整批静默通过 | `cases/C02_retcode.krt:3-4`；`cases/test_c81_pointer_increment.krt:6,9,11`（test_c80–c91 同型） |
| 230 | 致命 | 100-1｜f_inf.krt 无限循环无终止条件，运行审计会永久挂起 | `Test/SyntaxAudit/probes/f_inf.krt:6` |
| 231 | 致命 | 101-1｜x_c2 断言里残留 `%s` 模板占位符 | `Test/SyntaxAudit/probes/x_c2.krt:6` |
| 232 | 致命 | 101-2｜xn 字符串字面量转义错误且残留两处 `%s` | `Test/SyntaxAudit/probes/xn.krt:4`、`Test/SyntaxAudit/probes/xn.krt:6` |

## 第一部分　ArkLink 与链接层（含第一至六批）


#### [致命] 1｜emit_binary_op DIV/MOD 有符号除法未符号扩展
- 位置：`X86Codegen.c:434-444`
- 类别：1/4
- 触发条件：任何负数除法/取模
- 后果：静默错误结果或进程崩溃（`INT_MIN/-1` 直接 SIGFPE）
- 最小修复：有符号 `cqo+idiv`，无符号 `xor rdx,rdx+div`；除零生成显式诊断/返回值，并保证 `.Ldiv_by_zero_%s` 标签生成条件与指令生成一致（见第12条）
- 备注：—

#### [致命] 2｜emit_compare 无符号/浮点比较误用有符号 setcc
- 位置：`X86Codegen.c:463-499`
- 类别：4
- 触发条件：unsigned 或 float 比较
- 后果：条件分支静默错误
- 最小修复：按 IR 类型选 `setb/seta/setbe/setae`；浮点用 `ucomisd`+`seta/sete`
- 备注：—

#### [致命] 3｜KRT_IR_POW 参数寄存器按 Windows ABI 硬编码
- 位置：`X86Codegen.c:820-858`
- 类别：4/11
- 触发条件：Linux 下调用 `pow`
- 后果：参数错位（应 rdi/rsi，实际 rcx/rdx），结果错误或崩溃
- 最小修复：加 `#ifdef __linux__` 分支
- 备注：—

#### [致命] 4｜未实现 opcode 被静默丢弃
- 位置：`X86Codegen.c:789-792`（`KRT_IR_ALLOC`/`NOP`）+ `emit_instruction` default
- 类别：1/5
- 触发条件：IR 含 ALLOC、AND/OR/XOR/MOD/移位/LE/GE/NE/COPY/CAST/ARRAY_STORE/LOADPTR/STOREPTR 等未实现 opcode
- 后果：指令被静默丢弃，生成错误代码，无任何诊断
- 最小修复：实现或显式 `KrtError` 拒绝
- 备注：—

#### [致命] 5｜KRT_IR_CAST 实现为空操作
- 位置：`X86Codegen.c:782-787`
- 类别：13
- 触发条件：任何类型转换
- 后果：int/float/bool 转换全部静默无效
- 最小修复：按源/目标类型插入 `cvtsi2sd`/`cvttsd2si` 等
- 备注：—

#### [致命] 6｜栈大小截断导致越界写
- 位置：`X86Codegen.c:19-59、163-165、271-273`
- 类别：4/8
- 触发条件：temp 数 >32 或 spill 多
- 后果：`mov [rbp - offset]` 越界写，栈破坏（`temp_space` 上限截为 256 字节；regalloc 偏移无上限；`temp_stack_base` 硬编码）
- 最小修复：栈大小以 regalloc 的 `stack_space_needed` 为准，断言所有偏移 < `stack_size`
- 备注：—

#### [致命] 7｜未分配 temp 的 load/store 位置不一致
- 位置：`X86Codegen.c:259-301`
- 类别：1/4
- 触发条件：temp 未分配（TEMP_LOC_NONE）
- 后果：同一 temp 的 load/store 位置不一致，读到未初始化栈
- 最小修复：统一分配策略，load 也分配或报错
- 备注：—

#### [致命] 8｜X86_LIVESET_BITS=256 致高编号 temp 干扰边丢失
- 位置：`X86RegAlloc.h:7` + `X86RegAlloc.c:28-36、90-97`
- 类别：8
- 触发条件：函数 temp 数 >256
- 后果：高编号 temp 的干扰边全部丢失 → 寄存器分配冲突 → 静默错误结果
- 最小修复：动态位集/哈希，或显式限制并报错
- 备注：—

#### [致命] 9｜conflict graph 节点/加边失败静默丢失
- 位置：`X86RegAlloc.c:253-275`
- 类别：1/8
- 触发条件：并发 temp >256
- 后果：`conflict_graph_get_or_create_node` 返回 NULL、`add_edge` 静默 return、`build` 不检查 → 干扰边丢失 → 错误分配
- 最小修复：动态扩容或拒绝编译
- 备注：—

#### [致命] 10｜liveness 数组按无上限容量分配/填充不全
- 位置：`X86RegAlloc.c:200-250`
- 类别：8/4
- 触发条件：超大函数/超大 temp 索引
- 后果：`temp_capacity*sizeof(int)` 溢出堆；liveness 只填充 `i<10000` 但 build 遍历全部指令 → 读取零初始化 liveness → 干扰丢失 → 错误代码
- 最小修复：用 `KrtSizeMultiply` 检查溢出；指令数设上限报错；liveness 填充覆盖全部指令
- 备注：—

#### [致命] 11｜mov-mov 链消除误删仍被使用的 temp 定义
- 位置：`X86CodeOpt.c:333-345`（`apply_mov_mov_chain_elimination`）
- 类别：1
- 触发条件：COPY 链中间 temp 被后续指令使用
- 后果：静默错误结果
- 最小修复：检查 use-count 或保留 inst2
- 备注：—

#### [致命] 12｜peephole 优化器全项目死代码
- 位置：`X86CodeOpt.c:428`（`x86_optimize_peephole`）
- 类别：5
- 触发条件：全项目（grep 只有定义，无任何调用者）
- 后果：优化未生效；若将来接入会引入第11条错误
- 最小修复：接入流水线或删除
- 备注：—

#### [致命] 13｜VM 目标只生成 main 函数
- 位置：`VmCodegen.c:280-300`（`KrtVmCodegenGenerate`）
- 类别：5
- 触发条件：多函数模块选择 VM 目标
- 后果：其他函数完全不生成 → 调用未定义符号/功能缺失
- 最小修复：遍历 `module->functions` 生成全部函数
- 备注：—

#### [致命] 14｜VM generate_function switch 大量 opcode 落入 default
- 位置：`VmCodegen.c:200-280`
- 类别：1
- 触发条件：IR 含 AND/OR/XOR/MOD/移位/LE/GE/NE/BRANCH/JUMP/COPY/CAST/ARRAY_STORE/LOADPTR/STOREPTR/STRCAT/DOUBLE_TO_STRING 等
- 后果：静默错误代码（LOAD/IMM 只 push 操作数，不写 result）
- 最小修复：实现或报错
- 备注：—

#### [致命] 15｜VM 带返回值调用多弹一次栈
- 位置：`VmCodegen.c:160-180`（`emit_function_call`）
- 类别：8
- 触发条件：带返回值的调用赋给局部变量
- 后果：result 为 VAR 时 `SET_LOCAL+POP` 后又无条件 `POP` → VM 栈失衡/下溢
- 最小修复：确认 `OP_SET_LOCAL` 语义后移除多余 POP
- 备注：疑（`OP_SET_LOCAL` 是否弹栈待查 VM 实现）

#### [致命] 16｜VM temp 索引截断为 uint8
- 位置：`VmCodegen.c:40-55`（`emit_value_push` ARG/TEMP）
- 类别：8
- 触发条件：索引 >255
- 后果：`(uint8_t)index` 截断、无范围检查；`get_temp_index = local_count + temp_id` 可 >255 → 读写错误局部槽
- 最小修复：断言/报错
- 备注：—

#### [致命] 17｜VM 输出文件名 strcat 越界
- 位置：`Compiler.c:208-216`
- 类别：8
- 触发条件：输出路径长度接近 255 且无 `.`
- 后果：`char output_filename[256]; KRT_STRNCPY(...); strcat(...,".ebc")` 越界写栈
- 最小修复：`snprintf` 并检查长度
- 备注：—

#### [致命] 18｜module_fits_kro_limits 只统计定义数不统计最大索引
- 位置：`Compiler.c:108-146`
- 类别：4
- 触发条件：temp 索引跳号/稀疏
- 后果：绕过 `KRT_KRO_MAX_TEMP_STORAGE` 限制 → KRO 后端栈/表溢出
- 最小修复：跟踪最大索引+1
- 备注：—

#### [致命] 19｜空遍历死循环
- 位置：`Compiler.c:148-160`
- 类别：5
- 触发条件：总是
- 后果：无用代码
- 最小修复：删除
- 备注：—

#### [致命] 20｜module_has_native_pointers 覆盖不全
- 位置：`Compiler.c:100-107`
- 类别：4
- 触发条件：X86/VM 目标遇到 LOADPTR/STOREPTR/ARRAY_STORE/ARRAY_LOAD
- 后果：后端收到指针操作却生成错误代码，而非拒绝
- 最小修复：补齐指针类 opcode
- 备注：—

#### [致命] 21｜AST→IR 失败返回值被忽略
- 位置：`Compiler.c:176`（`KrtIrGenerateFromAst`）
- 类别：1
- 触发条件：AST→IR 失败
- 后果：仍继续优化和代码生成，对空/错误 module 继续，错误信息缺失
- 最小修复：检查返回值并报错
- 备注：—

#### [致命] 22｜KrtMemoryReportError 为空操作，内存错误全被吞
- 位置：`Core/Memory/Allocator.c:425-435`（原标 380-390）
- 类别：1/2 → 静默失败 / 未检查前置条件
- 触发条件：任何内存错误
- 后果：release 下 MALLOC_FAILED/INVALID_FREE/DOUBLE_FREE/CANARY_CORRUPTED/USE_AFTER_FREE 全部被静默吞掉，所有内存安全诊断丢失
- 最小修复：实现为打印/记录并设置全局错误标志
- 备注：【修订 G1】G1 确认该函数确为空操作；分配失败路径为活路径且零诊断

#### [致命] 23｜KRT_ASSERT 被编译为 ((void)0)
- 位置：`Core/Utils/KrtCommon.h:90`
- 类别：12
- 触发条件：任何依赖 assert 的不变量
- 后果：不变量破坏无检测
- 最小修复：改为真实 `assert` 或带诊断检查
- 备注：—

#### [致命] 24｜后端改写共享 IR（SSA 优化）
- 位置：`X86Codegen.c:1019`（`emit_function` 调用 `KrtIrSsaOptimize`）
- 类别：4/7
- 触发条件：同一 module 被多个后端/多次代码生成
- 后果：IR 被静默改写，结果不可预测（KroCodegen.c:1296 注释称 SSA 不稳定被禁用，X86/Vm 却启用）
- 最小修复：流水线层面统一优化一次，后端不得改 IR
- 备注：—

#### [致命] 25｜畸形 IR 操作数未检查 NULL/数量
- 位置：`X86Codegen.c:751-772`
- 类别：8
- 触发条件：BRANCH/JUMP/CALL 的 `operands[..].data.name/function_name` 为 NULL；CALL 在 `operand_count==1` 时构造 `&operands[1]` 越界指针
- 后果：崩溃/UB
- 最小修复：检查操作数数量和 NULL
- 备注：—

#### [致命] 26｜codegen_get_var_offset 失败返回 0 与合法 offset 冲突
- 位置：`X86Codegen.c:233-257`
- 类别：1/4
- 触发条件：>100 个变量或未登记变量
- 后果：变量别名/错误读写，静默丢变量
- 最小修复：返回 -1 并诊断
- 备注：—

#### [致命] 27｜param_count>0 但 params==NULL 静默跳过保存
- 位置：`X86Codegen.c:900-930`（`emit_function_prologue`）
- 类别：1
- 触发条件：`param_count>0` 且 `params==NULL`
- 后果：参数读到未初始化栈
- 最小修复：断言 params 非空或报错
- 备注：—

#### [致命] 28｜emit_load_value 字符串常量/宽整数边界缺失
- 位置：`X86Codegen.c:340-379`
- 类别：8
- 触发条件：越界字符串常量/宽整数
- 后果：STRING_CONST 不检查边界直接生成 `string_const_%d`；INTEGER 用 `%llu` 打印可能 128 位值 → 汇编错误或截断
- 最小修复：边界检查+按类型打印
- 备注：疑（`value->data.integer` 实际类型待查）

#### [致命] 29｜128 位除零触发 SIGILL
- 位置：`KroInteger.inc:288-336`（`emit_wide_division`）
- 类别：1/8
- 触发条件：128 位除零
- 后果：发 `0F 0B`（`ud2`）触发 SIGILL，而非返回错误/诊断；除零判定只比较部分位
- 最小修复：生成运行时检查/诊断
- 备注：—

#### [致命] 30｜128 位移位只检查低 64 位
- 位置：`KroInteger.inc:338-390`（`emit_integer_shift`）
- 类别：8
- 触发条件：128 位移位 count 高 64 位非零
- 后果：错误移位
- 最小修复：检查完整 128 位
- 备注：—

#### [致命] 31｜未知条件码默认 JNE
- 位置：`KroInteger.inc:836-851`（`integer_condition`）
- 类别：1
- 触发条件：未知条件码
- 后果：非法比较被静默当 NE
- 最小修复：报错
- 备注：—

#### [致命] 32｜KroRegisterPacking 参数/寄存器数组越界
- 位置：`KroRegisterPacking.inc:227-292`
- 类别：8
- 触发条件：`current_param_count>128` 或 reg 索引≥16
- 后果：`ctx->arg_packed` 大小 128；`next_write[16]` 硬编码 16 而 `g_packed_regs` 只有 5 → 越界写
- 最小修复：用 `KRT_KRO_PACKED_REG_COUNT` 并检查
- 备注：—

#### [致命] 33｜ARG 索引≥32 移位 UB
- 位置：`KroTailUpdates.inc:145-148`
- 类别：8
- 触发条件：ARG 索引≥32
- 后果：`1u << index` 移位 UB；`current_param_count` 可 >32
- 最小修复：用 64 位掩码或检查范围
- 备注：—

#### [致命] 34｜find_temp_location 返回 NULL 未检查即写
- 位置：`KroTailCalls.inc:116-118、149-156`
- 类别：8
- 触发条件：temp 未登记
- 后果：空指针写
- 最小修复：检查并报错
- 备注：—

#### [致命] 35｜call→ret 链表遍历无环检测
- 位置：`KroTailCalls.inc:119-124`
- 类别：8
- 触发条件：IR 链表被破坏
- 后果：死循环/越界
- 最小修复：加环检测/步数上限
- 备注：—

#### [致命] 36｜KroLeafReturn 多处未检查操作数数量/NULL
- 位置：`KroLeafReturn.inc:85-88、98、106-123、162-167`
- 类别：8/6
- 触发条件：畸形 IR
- 后果：`entry->succs[0/1]`、`compare->operands[0/1]`、`leaf->preds[0]`、`entry->first_inst` 未检查数量和 NULL → 崩溃
- 最小修复：断言/检查
- 备注：—

#### [致命] 37｜128 位/非常规宽度浮点转换未支持
- 位置：`KroFloatCasts.inc:68-86、88-123`
- 类别：4/6
- 触发条件：128 位/非常规宽度
- 后果：`emit_integer_float_cast` 只处理 width 4/8；`emit_float_wide_cast` 假定输入为 double 位模式、未校验类型/宽度 → 错误代码
- 最小修复：显式支持或拒绝
- 备注：—

#### [致命] 38｜allocate_packed_field bits=0/负数静默丢字段
- 位置：`KroRegisterPacking.inc:2-26`
- 类别：1/8
- 触发条件：bits=0/负数
- 后果：bits 未检查 ≤64/正数；bits==0 静默返回，字段无存储但调用方继续
- 最小修复：检查并报错
- 备注：—

#### [致命] 39｜emit_store_value 未检查 dest->data.name/符号查找失败
- 位置：`KroInteger.inc:255-278`
- 类别：8
- 触发条件：`dest->data.name==NULL` 或符号查找失败
- 后果：未检查 NULL；`find_or_alloc_local_var` 可能返回 NULL，slot 未初始化 → 崩溃
- 最小修复：检查
- 备注：—

#### [致命] 40｜KroInteger 多处未检查 operand_count>=2
- 位置：`KroInteger.inc:651-695、739-766、853-863`
- 类别：8/6
- 触发条件：畸形 IR
- 后果：多处未检查 `operand_count>=2` 即读 `operands[0/1]` → 越界读
- 最小修复：检查
- 备注：—

#### [致命] 41｜definition_block 分配失败后状态未回滚
- 位置：`KroRegisterPacking.inc:386`
- 类别：7/9 → 静默失败 / 未检查前置条件
- 触发条件：分配失败
- 后果：直接 return false，但 `eligible` 数组可能已部分修改，调用方未回滚 → 状态不一致
- 最小修复：回滚或清理
- 备注：【修订 G1】分配失败为活路径且零诊断（非死代码）；后果为状态不一致

#### [致命] 42｜KRO 后端静默截断超量参数
- 位置：`KroCodegen.c:1317`
- 类别：1/4
- 触发条件：`param_count > KRT_KRO_MAX_ARGS`
- 后果：`current_param_count` 被静默截断，超出参数丢弃 → 调用约定错位
- 最小修复：拒绝并报错（Compiler.c 已检查，后端再截断说明契约不一致）
- 备注：—

#### [高] 43｜emit_return 生产路径残留 printf 调试输出
- 位置：`X86Codegen.c:676-685`
- 类别：13/5
- 触发条件：生成 main 的 sys_exit 时
- 后果：污染编译器 stdout
- 最小修复：删除或 `KRT_DEBUG` 守卫
- 备注：—

#### [高] 44｜KrtX86Generate 未检查 module/函数名 NULL
- 位置：`X86Codegen.c:1053+`
- 类别：8
- 触发条件：空 module
- 后果：崩溃（`module`、`module->main_function->name` 未检查 NULL）
- 最小修复：检查
- 备注：—

#### [高] 45｜codegen_free_register 未检查 ctx/reg NULL
- 位置：`X86Codegen.c:201-231`
- 类别：8
- 触发条件：reg 为 NULL
- 后果：`strcmp` 崩溃
- 最小修复：检查
- 备注：—

#### [高] 46｜var_name NULL 生成 [rbp-0]
- 位置：`X86Codegen.c:223-231、303-311`
- 类别：8
- 触发条件：var_name 为 NULL
- 后果：生成 `[rbp-0]`
- 最小修复：检查
- 备注：—

#### [高] 47｜[rbp-offset] 无边界检查
- 位置：`X86Codegen.c:271-273`
- 类别：8
- 触发条件：offset 为负/超大
- 后果：内存破坏
- 最小修复：断言
- 备注：—

#### [高] 48｜temp_array_ensure_capacity 契约脆弱
- 位置：`X86Codegen.c:68-87`
- 类别：8
- 触发条件：needed 为负；capacity 初始 0 时 `while(0<=0)` 死循环（实际初始 256 不触发）
- 后果：契约脆弱
- 最小修复：检查 needed≤0
- 备注：—

#### [高] 49｜spill_rcx_if_used 只保存 rcx
- 位置：`X86Codegen.c:318-331`
- 类别：1/4
- 触发条件：emit_call 14 寄存器快照不存在时
- 后果：调用破坏寄存器未完整保存
- 最小修复：确认设计/补全保存
- 备注：疑（需确认设计）

#### [高] 50｜regalloc_get_reg_name/stack_offset 未检查 result NULL
- 位置：`X86RegAlloc.c:380-395`
- 类别：8
- 触发条件：调用方传 NULL
- 后果：崩溃
- 最小修复：检查
- 备注：—

#### [高] 51｜liveness_analysis_run 未检查 analysis/func NULL
- 位置：`X86RegAlloc.c:140-160`
- 类别：8
- 触发条件：NULL 传入
- 后果：崩溃
- 最小修复：检查
- 备注：—

#### [高] 52｜优化器对 .data.name 直接 strcmp
- 位置：`X86CodeOpt.c:60-70、110-120`
- 类别：8
- 触发条件：`.data.name` 为 NULL
- 后果：崩溃
- 最小修复：检查
- 备注：—

#### [高] 53｜module_has_extended_integers 覆盖不全
- 位置：`Compiler.c:176-190`
- 类别：4
- 触发条件：全局数组元素类型/string 常量
- 后果：与 `module_has_native_pointers` 一样覆盖不全
- 最小修复：补齐
- 备注：—

#### [高] 54｜VM 序列化返回值被忽略
- 位置：`Compiler.c:208-216`
- 类别：1
- 触发条件：`KrtBytecodeGeneratorSerializeToFile` 失败
- 后果：静默失败
- 最小修复：检查并报错
- 备注：—

#### [高] 55｜未识别 target 静默回退 KrtIrPrint
- 位置：`Compiler.c:230-240`
- 类别：1/11
- 触发条件：target 未识别
- 后果：用户请求的目标被静默替换
- 最小修复：报错
- 备注：—

#### [高] 56｜module_fits_kro_limits 栈数组/除零风险
- 位置：`Compiler.c:108-146`
- 类别：8
- 触发条件：常量较大时栈溢出；`hash % (KRT_KRO_MAX_LOCAL_VARS*2)` 在常量为 0 时除零
- 后果：栈溢出/除零
- 最小修复：检查常量值并防护
- 备注：疑（需确认常量值）

#### [高] 57｜CompilerPipeline Codegen 未检查 config NULL / target 静默回退
- 位置：`CompilerPipeline.c:320-330`
- 类别：8/1
- 触发条件：`pipeline->config` 为 NULL；target 未识别
- 后果：崩溃或静默错误目标（回退 X86_ASM）
- 最小修复：检查并报错
- 备注：—

#### [高] 58｜record_stage_result 静默丢弃/OOB 读
- 位置：`CompilerPipeline.c:19-25`
- 类别：1/8
- 触发条件：`stage_count>=8`；stage 越界
- 后果：阶段结果静默丢弃；`KrtCompileStageNames[stage]` OOB 读
- 最小修复：检查 stage 范围并扩容
- 备注：—

#### [高] 59｜ReadSource fread 短读未检测
- 位置：`CompilerPipeline.c:130-160`
- 类别：1
- 触发条件：I/O 错误
- 后果：源文件不完整仍继续编译（尾部未初始化）
- 最小修复：检查 `read_bytes==file_size`
- 备注：—

#### [高] 60｜Lex 词法错误诊断不具体
- 位置：`CompilerPipeline.c:170-200`
- 类别：2
- 触发条件：词法错误
- 后果：只报 "unterminated string or character literal near line %d"，不报具体类型；`error_line` 可能过期 → 诊断与实际错误不符
- 最小修复：透传 lexer 具体诊断
- 备注：—

#### [高] 61｜Execute 未检查 config / 预置 ast 模式假设
- 位置：`CompilerPipeline.c:520-560`
- 类别：6
- 触发条件：`pipeline->config` 为 NULL；ast 非空而 source_code 为空
- 后果：后续阶段假设可能不成立
- 最小修复：检查
- 备注：疑

#### [高] 62｜match_tail_return 未检查 params NULL
- 位置：`KroTailCalls.inc:33-43`
- 类别：8
- 触发条件：`func->params` 为 NULL 而 `param_count>0`（`IrFunctionCreate` 允许）
- 后果：崩溃
- 最小修复：检查
- 备注：—

#### [高] 63｜尾调用优化整段跳过与 plan_function_storage 顺序
- 位置：`KroTailCalls.inc:87`
- 类别：4
- 触发条件：`func->param_count != ctx->current_param_count`
- 后果：整段尾调用优化被跳过；设置顺序需确认
- 最小修复：确认阶段顺序
- 备注：疑（阶段不对齐）

#### [高] 64｜operands[i+1] 前置条件未显式断言
- 位置：`KroTailCalls.inc:265-299`、`KroTailUpdates.inc:117`
- 类别：6
- 触发条件：访问 `call->operands[i+1]` 依赖 `match_tail_return` 的 operand_count 前置
- 后果：契约未文档化，易被破坏
- 最小修复：加断言
- 备注：—

#### [高] 65｜KroTailUpdates find_temp_location NULL 未检查
- 位置：`KroTailUpdates.inc:192-195`
- 类别：8
- 触发条件：`find_temp_location` 返回 NULL
- 后果：崩溃
- 最小修复：检查
- 备注：—

#### [高] 66｜KroTailUpdates:228 分支可能把未加载寄存器当有效
- 位置：`KroTailUpdates.inc:228`
- 类别：1
- 触发条件：`left_reg==-1 && right_reg==target`
- 后果：`right_reg == target && left_reg != target` 分支选 RAX，把未加载的 left 当有效
- 最小修复：修正分支条件
- 备注：疑（需验证）

#### [高] 67｜KroLeafReturn:152 返回 NULL 语义可能相反
- 位置：`KroLeafReturn.inc:152`
- 类别：1
- 触发条件：调用方按函数名/语义解读返回值
- 后果：`return ctx->tail_site_count && !ctx->rotated_tail_leaf ? NULL : leaf;` 语义可能与函数名/调用方相反
- 最小修复：核对调用方是否把 NULL 当失败
- 备注：疑（需验证调用方）

#### [高] 68｜emit_load_packed_field 未检查 field/bit 参数
- 位置：`KroRegisterPacking.inc:28-40`
- 类别：8
- 触发条件：`field` 为 NULL；bit_offset/bit_width 非法
- 后果：bit_offset==0 且 bit_width==0 时 `emit_extend_scalar` 行为未定义
- 最小修复：检查
- 备注：—

#### [高] 69｜KroRegisterPacking params NULL / 宽度未校验
- 位置：`KroRegisterPacking.inc:544-547、574-579`
- 类别：8
- 触发条件：`func->params[i]` 为 NULL；params/local 类型宽度非法
- 后果：`allocate_value_stack` 可能分配错误大小
- 最小修复：检查
- 备注：—

#### [高] 70｜scalar_low_register arg_packed 越界/未知类型
- 位置：`KroInteger.inc:415-434`
- 类别：8
- 触发条件：同第32条越界；`value->value_type` 为 VOID/未知
- 后果：越界/未定义
- 最小修复：检查
- 备注：—

#### [高] 71｜emit_imm_op64 负数 imm32 无符号比较
- 位置：`KroInteger.inc:483-493`
- 类别：8
- 触发条件：`value >= 0xffffffffffffff80`
- 后果：无符号比较表示负数 imm32，`emit_u32` 截断语义需确认
- 最小修复：确认语义并检查
- 备注：疑

#### [高] 72｜emit_multiply_pair/constant_shift_count 符号未校验
- 位置：`KroInteger.inc:697-716`
- 类别：8
- 触发条件：`amount->value_type` 未校验
- 后果：未检查输入寄存器内容/符号；`value >= (KrtUInt128)width` 比较可能因符号扩展错误
- 最小修复：校验符号/类型
- 备注：疑

#### [高] 73｜INT_MIN/-1 的 MOD 未处理 RDX
- 位置：`KroInteger.inc:782-814`
- 类别：8
- 触发条件：INT_MIN/-1
- 后果：MOD 直接 RAX=0，未处理 RDX；128 位 INT_MIN 除 -1 的 ABI 未确认
- 最小修复：处理 RDX/确认 ABI
- 备注：疑

#### [高] 74｜emit_load_condition 对 float64 按整数 test
- 位置：`KroInteger.inc:195-202`
- 类别：4
- 触发条件：IR 未先把 float64 条件转 bool
- 后果：语义错误
- 最小修复：确认 IR 生成器契约并修正
- 备注：疑（需确认契约）

#### [高] 75｜KroCodegen.c:1296 注释与 X86/Vm 启用 SSA 矛盾
- 位置：`KroCodegen.c:1296`
- 类别：13/4
- 触发条件：阅读/维护时
- 后果：注释「禁用SSA优化…SSA不稳定」与 X86/Vm 启用 SSA 矛盾；注释含个人名字
- 最小修复：统一策略并清理注释
- 备注：—

#### [中] 76｜X86 runtime 符号映射与 Kro 后端分叉
- 位置：`X86Codegen.c:643-649`
- 类别：3
- 触发条件：`print_string/println_string` 改名
- 后果：其他 runtime 符号无映射；与 Kro 后端符号处理分叉
- 最小修复：统一符号映射表
- 备注：—

#### [中] 77｜call %s 未定义符号无诊断
- 位置：`X86Codegen.c:649`
- 类别：2
- 触发条件：调用未定义符号
- 后果：链接时才发现
- 最小修复：生成阶段检查符号表
- 备注：—

#### [中] 78｜LOADPTR/STOREPTR 忽略 size/类型
- 位置：`X86Codegen.c:798-818`
- 类别：3/4
- 触发条件：指针加载/存储
- 后果：与 Kro Sized 路径分叉
- 最小修复：统一 sized 访问
- 备注：—

#### [中] 79｜STRCAT/INT_TO_STRING/DOUBLE_TO_STRING 固定符号调用
- 位置：`X86Codegen.c:820-858`
- 类别：3
- 触发条件：这些 runtime 调用
- 后果：未检查参数类型/符号存在；与 Kro 后端分叉
- 最小修复：统一 runtime 调用约定
- 备注：—

#### [中] 80｜emit_call 无条件 push 14 寄存器/对齐公式可疑
- 位置：`X86Codegen.c:594-597、623-629、631-641、651-663`
- 类别：8
- 触发条件：函数调用
- 后果：无条件 push 全部 14 个寄存器（含 rax），save_count 逻辑冗余；outgoing 对齐公式可疑；恢复顺序需验证
- 最小修复：补测试/核对栈布局
- 备注：疑（栈布局可能正确但极复杂）

#### [中] 81｜比较指令重复实现整数 setcc
- 位置：`X86Codegen.c:728-737`
- 类别：4
- 触发条件：float/unsigned 比较
- 后果：同第2条，重复实现
- 最小修复：合并到 emit_compare
- 备注：—

#### [高] 82｜KrtSafeMalloc(0)/KrtSafeCalloc(0) 静默返回 NULL
- 位置：`Core/Memory/Allocator.c:159-162、213-216`（宏 `Core/Utils/KrtCommon.h:77-81`）
- 类别：1 → 静默失败 / 未检查前置条件
- 触发条件：`size==0` 或 malloc/calloc 失败
- 后果：调用方无法区分 size==0 与 OOM；所有分配失败路径为活路径且零诊断，NULL 静默传播/解引用
- 最小修复：返回非空或明确契约；调用点检查 NULL 并报错
- 备注：【修订 G1】原判「调用方可能误判 OOM」修正为「分配失败是活路径、零诊断」；严重度由中上调为高

#### [中] 83｜KrtMemoryDumpBlocks/DumpStats 死代码
- 位置：`Core/Memory/Allocator.c:340-350`
- 类别：5
- 触发条件：调用内存统计
- 后果：循环但不做事；空实现
- 最小修复：实现或删除
- 备注：—

#### [中] 84｜KrtArenaRealloc 不收缩
- 位置：`Core/Memory/Arena.c:155-165`
- 类别：6
- 触发条件：`new_size<=old_size`
- 后果：直接返回 ptr 不收缩；调用方若假设新大小则越界
- 最小修复：文档化或返回新大小
- 备注：—

#### [中] 85｜KrtArenaStats 生产路径打印统计
- 位置：`Core/Memory/Arena.c:190-210`
- 类别：13
- 触发条件：调用统计
- 后果：污染 stdout
- 最小修复：删除或调试守卫
- 备注：—

#### [中] 86｜KRT_STRNCPY 宏 n==0 越界
- 位置：`Core/Utils/KrtCommon.h:80-86`
- 类别：8
- 触发条件：n==0
- 后果：`dest[n-1]` 越界
- 最小修复：检查 n>0
- 备注：【修订 G1】宏无守卫（KrtCommon.h:77-81），分配类宏同样缺乏失败守卫

#### [中] 87｜KRO_OBJ 与 EXE_PLATFORM 同一语义两处实现
- 位置：`Compiler.c:108-146`
- 类别：3
- 触发条件：`KRT_TARGET_KRO_OBJ`/`KRT_TARGET_EXE_PLATFORM`
- 后果：都调用 `KrtKrtGenerate`，重复实现
- 最小修复：合并为一个入口
- 备注：—

#### [中] 88｜CompilerPipeline 重复包含头文件
- 位置：`CompilerPipeline.c:1-15`
- 类别：13
- 触发条件：编译
- 后果：`<string.h>`、`<sys/stat.h>` 重复包含
- 最小修复：清理
- 备注：—

#### [中] 89｜X86 优化器无测试覆盖且算法错误
- 位置：`X86CodeOpt.c:333-345`
- 类别：12
- 触发条件：—
- 后果：优化器无测试覆盖，且算法本身错误（第11条）
- 最小修复：补 use-count 断言和测试
- 备注：—

#### [中] 90｜Test/ 目录缺少关键回归测试
- 位置：`Test/`
- 类别：12
- 触发条件：—
- 后果：未覆盖除零、unsigned/float 比较、CAST、POW ABI、栈偏移溢出、>256 temp、VM 多函数/缺失 opcode、内存错误诊断等不变量
- 最小修复：为每个致命项加回归测试
- 备注：—

#### [疑] 疑似-1｜X86RegAlloc liveness 区间可能欠近似
- 位置：`X86RegAlloc.c:200-250`
- 类别：8
- 触发条件：循环中 use 在 def 之前
- 后果：`[def_pos, last_use_pos]` 欠近似 → 干扰边丢失 → 错误分配
- 最小修复：构造循环 IR 验证并修正
- 备注：疑似待验证

#### [疑] 疑似-2｜emit_call 栈布局/恢复顺序待验证
- 位置：`X86Codegen.c:594-663`
- 类别：8
- 触发条件：函数调用
- 后果：栈布局/恢复顺序是否完全正确未知
- 最小修复：反汇编生成的汇编并单步验证
- 备注：疑似待验证（与第80条相关）

#### [疑] 疑似-3｜value->data.integer 类型与 %llu 匹配性待查
- 位置：`X86Codegen.c:340-379`
- 类别：8
- 触发条件：打印整数字面量
- 后果：`value->data.integer` 是否 128 位与 `%llu` 是否匹配未知
- 最小修复：查 `KrtIRValue` 定义
- 备注：疑似待验证（与第28条相关）

#### [疑] 疑似-4｜KroTailUpdates:228 分支逻辑待验证
- 位置：`KroTailUpdates.inc:228`
- 类别：1
- 触发条件：`left_reg==-1 && right_reg==target`
- 后果：分支逻辑可能错误
- 最小修复：构造对应 IR 验证
- 备注：疑似待验证（同第66条）

#### [疑] 疑似-5｜KroLeafReturn:152 返回 NULL 语义待查
- 位置：`KroLeafReturn.inc:152`
- 类别：1
- 触发条件：调用方解读返回值
- 后果：返回 NULL 语义未知
- 最小修复：查调用方
- 备注：疑似待验证（同第67条）

#### [疑] 疑似-6｜KRT_KRO_MAX_LOCAL_VARS 实际值待确认
- 位置：`Compiler.c:114`
- 类别：8
- 触发条件：—
- 后果：判断栈数组是否溢出/除零
- 最小修复：确认常量值
- 备注：疑似待验证（与第56条相关）

#### [疑] 疑似-7｜OP_SET_LOCAL 是否弹栈待查
- 位置：`VmCodegen.c:160-180`
- 类别：8
- 触发条件：带返回值调用
- 后果：决定双 POP 是否为 bug
- 最小修复：查 VM 实现
- 备注：疑似待验证（与第15条相关）

#### [疑] 疑似-8｜KroInteger 符号/宽度边界待构造用例
- 位置：`KroInteger.inc:483-493、697-716、782-814`
- 类别：8
- 触发条件：128 位边界值
- 后果：符号/宽度边界行为未知
- 最小修复：构造 128 位用例验证
- 备注：疑似待验证（与第71/72/73条相关）

#### [中] 第一批-1｜target 有效性双重判断 + native default 不可达
- 位置：`ArkLink/src/Core/Arklink.c:184-191` + `LinkerNative.c:104-114`
- 类别：语义分叉 + 死代码
- 触发条件：target 校验
- 后果：session 层判一次、native 层再判一次；`LinkerNative.c:111-113` 的 `default` 永不可达；两处规则若漂移则一处失效
- 最小修复：统一到单层校验
- 备注：—

#### [中] 第一批-2｜符号解析失败丢弃具体诊断
- 位置：`ArkLink/src/Core/LinkerNative.c:76-88`
- 类别：诊断缺失
- 触发条件：符号解析失败
- 后果：只写 "Symbol resolution failed"，丢弃 resolver 的具体符号/位置信息；对比 loader 失败路径（行59）保留 `diag.message`，同层不一致
- 最小修复：透传 resolver 诊断
- 备注：—

#### [中] 第一批-3｜后端链接失败丢弃细节
- 位置：`ArkLink/src/Core/LinkerNative.c:116-127`
- 类别：诊断缺失
- 触发条件：后端链接失败
- 后果：只写 "Backend linking failed"，丢弃后端细节
- 最小修复：透传后端诊断
- 备注：—

#### [高] 第一批-4｜backend_input==NULL 仍调用后端 link
- 位置：`ArkLink/src/Core/LinkerNative.c:90-98`
- 类别：契约破坏
- 触发条件：`plan.backend_input == NULL`
- 后果：跳过 image_base 赋值后仍以 NULL 调用 `ark_backend_*_link`；后端是否处理 NULL 未断言
- 最小修复：检查 NULL 并报错
- 备注：—

#### [中] 第一批-5｜Main.c -o/-t 缺参数静默丢弃
- 位置：`ArkLink/src/Main.c:51,53`
- 类别：静默失败
- 触发条件：`-o`/`-t` 缺参数
- 后果：`i+1 < argc` 为假时该分支不匹配，`-o` 被静默丢弃，继续用默认输出名
- 最小修复：报错
- 备注：—

#### [中] 第一批-6｜Main.c 未知选项静默跳过
- 位置：`ArkLink/src/Main.c:63`
- 类别：静默失败
- 触发条件：未知选项（如 `-x`）
- 后果：以 `-` 开头直接跳过，无报错
- 最小修复：报错
- 备注：—

#### [高] 第一批-7｜inputs 槽位置 NULL 但计数未增
- 位置：`ArkLink/src/Core/Arklink.c:130-139`
- 类别：契约破坏
- 触发条件：`strdup` 失败
- 后果：`inputs` 已 realloc 扩容但 `input_count` 未增，槽位置 NULL；调用者见失败但 session 内部状态已变
- 最小修复：失败回滚
- 备注：—

#### [中] 第一批-8｜stack_size=0 未检查
- 位置：`ArkLink/src/Core/Arklink.c:117-123`
- 类别：未检查前置条件
- 触发条件：`stack_size = 0`
- 后果：直接接受，后端会生成零栈保留的程序
- 最小修复：范围校验
- 备注：—

#### [中] 第一批-9｜output_kind/subsystem 非法值未校验
- 位置：`ArkLink/src/Core/Arklink.c:78-84,101-107`
- 类别：未检查前置条件
- 触发条件：传非法值
- 后果：直接接受，无范围校验
- 最小修复：范围校验
- 备注：—

#### [中] 第一批-10｜error_buffer 只保留最后一次诊断
- 位置：`ArkLink/src/Core/Arklink.c:154-159`
- 类别：共享可变状态
- 触发条件：多次失败
- 后果：旧诊断被覆盖
- 最小修复：累积诊断
- 备注：—

#### [高] 第一批-11｜四个 session 配置 API 只写不读 + backend_data 未管理
- 位置：`ArkLink/src/Core/Arklink.c`（session 层）+ 各后端（`BackendPe.c:925`、`BackendElf.c:1669`）
- 类别：配置漂移 / 静默失败
- 触发条件：用户通过 `output_kind`、`subsystem`、`stack_size`、`entry_point` API 配置
- 后果：四字段全部只写不读（native 层结构体声明但从不使用，后端硬编码）；`backend_data` 从未被赋值或释放 → 用户配置被静默忽略
- 最小修复：把配置传到后端或使用统一字段；管理 backend_data 生命周期
- 备注：关键发现

#### [中] 第一批-12｜session 与后端枚举语义分叉
- 位置：`ArkLink/src/Core/Arklink.c`（`ArkLinkOutputKind`）vs 后端（`ArkOutputType`）
- 类别：语义分叉
- 触发条件：输出类型选择
- 后果：两套枚举描述同一概念，session 那套从未传到后端
- 最小修复：统一枚举并传递
- 备注：—

#### [高] Loader-1｜COFF loader 不解析任何结构即返回 OK
- 位置：`LoaderCoff.c:6-49`
- 类别：死代码 + 静默失败
- 触发条件：任意 `.obj`/`.o` 输入
- 后果：只读文件、不解析 COFF 结构、不建 section/symbol/reloc，返回 OK；真 COFF 目标文件被静默变成空单元，符号全丢但链接"成功"
- 最小修复：实现 COFF 解析或显式拒绝
- 备注：—

#### [高] Loader-2｜ELF sh_offset+sh_size 加法溢出绕过范围检查
- 位置：`LoaderElf.c:133,141-147,309,345,352,432,450`
- 类别：未检查前置条件
- 触发条件：恶意/损坏 ELF（`sh_offset` 接近 UINT64_MAX）
- 后果：加法溢出回绕绕过范围检查 → `file_data+sh_offset` 越界指针 → 崩溃/越界读
- 最小修复：减法形式检查并先校验 `offset < size`
- 备注：—

#### [中] Loader-3｜32 位 ELF 先用 64 位结构体解析
- 位置：`LoaderElf.c:268-287`
- 类别：阶段不对齐
- 触发条件：32 位 ELF
- 后果：`elf_context_create` 先用 64 位结构体解析并做范围检查，class/endianness 检查在其后；32 位文件可能用错布局解析
- 最小修复：先判 class/endianness 再解析
- 备注：—

#### [高] Loader-4｜r_info 符号索引越界未校验
- 位置：`LoaderElf.c:455-458,488`
- 类别：未检查前置条件
- 触发条件：`r_info` 符号索引 ≥ 符号表大小
- 后果：`elf_sym_idx--` 后仍越界，`reloc.sym_idx` 未校验 → 下游 resolver 越界访问符号表
- 最小修复：校验索引范围
- 备注：—

#### [高] Loader-5｜未知 r_type 默认映射 ABS64
- 位置：`LoaderElf.c:481-483`
- 类别：静默失败
- 触发条件：未知 `r_type`
- 后果：默认映射为 `ARK_RELOC_ABS64`，生成错误的重定位，运行时地址错乱
- 最小修复：报错或显式支持
- 备注：—

#### [高] Loader-6｜sh_entsize 不匹配仍按错误步长解析
- 位置：`LoaderElf.c:349,449`
- 类别：契约破坏
- 触发条件：`sh_entsize ≠ sizeof(Elf64_Sym_Loader)`
- 后果：符号/重定位表按错误步长解析，全部错位
- 最小修复：校验 sh_entsize
- 备注：—

#### [高] Loader-7｜reloc malloc 失败/fread 短读静默跳过
- 位置：`LoaderKro.c:309-312,315`
- 类别：静默失败 / 未检查前置条件
- 触发条件：reloc malloc 失败或 fread 短读
- 后果：`continue`/静默跳过，重定位全部丢失但加载成功
- 最小修复：检查返回值并报错
- 备注：【修订 G1】malloc 失败为活路径、零诊断

#### [中] Loader-8｜.bss 符号被当作未定义
- 位置：`LoaderKro.c:256,262`
- 类别：语义错误
- 触发条件：`.bss` 中的符号
- 后果：`kro_to_actual[4]=0` → bss 符号 `section_index=0`（未定义），链接报未解析
- 最小修复：修正映射
- 备注：—

#### [高] Loader-9｜desc->name 超长 strncpy 不写 NUL
- 位置：`Loader.c:85,122`
- 类别：未检查前置条件
- 触发条件：`desc->name` 长度 ≥ `sizeof(sec->name)`
- 后果：`strncpy` 不写 NUL，`strcmp`/`printf` 越界读
- 最小修复：检查长度并强制 NUL
- 备注：—

#### [高] Loader-10｜add_symbol 浅拷贝 name 致非法 free
- 位置：`Loader.c:95-110`（destroy 行50）
- 类别：契约破坏
- 触发条件：调用者传栈上/静态符号名
- 后果：`add_symbol` 浅拷贝 `name`，`destroy` 行50 `free(sym->name)` → free 非法指针崩溃；对比 `create` 用 strdup，同层不一致
- 最小修复：深拷贝 name
- 备注：—

#### [中] Loader-11｜LoaderKro 四处空 #ifdef KRO_DEBUG 块
- 位置：`LoaderKro.c:6,65-66,241-246,266-267`
- 类别：死代码
- 触发条件：定义 KRO_DEBUG
- 后果：4 处空 `#ifdef` 块
- 最小修复：删除或填入调试代码
- 备注：—

#### [高] Loader-12｜fread 短读致 string_table 未初始化
- 位置：`LoaderKro.c:232-238`
- 类别：未检查前置条件
- 触发条件：`fread` 短读
- 后果：返回值未检查，`string_table` 含未初始化字节，`kro_string_at` 越界读
- 最小修复：检查 fread 返回值
- 备注：—

#### [中] Loader-13｜SHN_ABS/SHN_COMMON 符号被当作外部符号
- 位置：`LoaderElf.c:398-404`
- 类别：语义错误
- 触发条件：`SHN_ABS`/`SHN_COMMON` 符号
- 后果：映射为 `section_index=0`（未定义），绝对符号被当作外部符号
- 最小修复：修正特殊节索引映射
- 备注：—

#### [中] Loader-14｜.text 无 EXEC 时强制标记可执行
- 位置：`LoaderElf.c:208-210`
- 类别：语义篡改
- 触发条件：`.text` 的 `sh_flags` 无 EXEC
- 后果：强制加 `ARK_SECTION_EXEC`，不可执行段被标记可执行
- 最小修复：按 sh_flags 设置
- 备注：—

#### [中] Loader-15｜.init_array/.comment/.note 等 PROGBITS 静默丢弃
- 位置：`LoaderElf.c:220-229`
- 类别：静默失败
- 触发条件：`.init_array`/`.comment`/`.note` 等 PROGBITS
- 后果：静默丢弃，不报错
- 最小修复：报错或支持
- 备注：—

#### [高] Loader-16｜add_symbol/add_reloc realloc 失败静默丢失
- 位置：`LoaderElf.c:423,493`、`LoaderKro.c:278,323`
- 类别：静默失败 / 未检查前置条件
- 触发条件：realloc 失败
- 后果：返回值未检查，符号/重定位静默丢失
- 最小修复：检查返回值并报错
- 备注：【修订 G1】realloc 失败为活路径、零诊断

#### [中] Loader-17｜STB_GNU_UNIQUE 等绑定静默降级为 GLOBAL
- 位置：`LoaderElf.c:417-419`
- 类别：静默降级
- 触发条件：`STB_GNU_UNIQUE` 等绑定
- 后果：默认映射 GLOBAL
- 最小修复：保留/显式处理绑定语义
- 备注：—

#### [致命] Resolver-1｜import_module 空串致 imports[] 堆溢出写
- 位置：`Resolver.c:487-511`
- 类别：未检查前置条件 → 堆溢出
- 触发条件：任一符号 `import_module != NULL` 但为空串（`LoaderKro.c:274` 允许任意 binding/flags 透传）
- 后果：统计用 `import_module[0]!='\0'`（行489），填充只判 `!=NULL`（行499）→ 填充数 > `import_count` → `imports[]` 堆溢出写
- 最小修复：统计与填充判定一致，并校验 idx < import_count
- 备注：本批唯一致命

#### [高] Resolver-2｜imports 数量字段与实际内容不符
- 位置：`Resolver.c:498-510`
- 类别：契约破坏
- 触发条件：同 Resolver-1
- 后果：行510 用统计值 `import_count` 而非实际 `idx`，数量字段与实际内容不符
- 最小修复：使用实际 idx
- 备注：—

#### [中] Resolver-3｜局部符号重命名泄漏 name
- 位置：`Resolver.c:257`
- 类别：资源泄漏
- 触发条件：局部符号重命名
- 后果：`strdup(renamed)` 赋给 `sym.name`，浅拷贝进 `sym_table`；`plan_destroy:588` 只 free 数组不 free name → 每次重命名泄漏一份
- 最小修复：销毁时释放 name
- 备注：—

#### [中] Resolver-4｜total_sections==0 提前返回未释放
- 位置：`Resolver.c:314-319`
- 类别：资源泄漏
- 触发条件：`total_sections==0`
- 后果：提前返回未 `free(unit_sec_start/unit_sym_start)`
- 最小修复：返回前释放
- 备注：—

#### [中] Resolver-5｜sections calloc 失败未释放前置分配
- 位置：`Resolver.c:321-327`
- 类别：资源泄漏
- 触发条件：`sections` calloc 失败
- 后果：未释放 `unit_sec_start/unit_sym_start`（对比行330-336 释放了，同层不一致）
- 最小修复：失败路径释放
- 备注：—

#### [中] Resolver-6｜reloc_table_add 失败未释放 sections/rename_table
- 位置：`Resolver.c:463-475`
- 类别：资源泄漏
- 触发条件：`reloc_table_add` 失败
- 后果：未释放 `sections[i].data`、未调用 `rename_table_free`
- 最小修复：失败路径清理
- 备注：—

#### [高] Resolver-7｜imports calloc 失败静默跳过全部导入
- 位置：`Resolver.c:495-511`
- 类别：静默失败 / 未检查前置条件
- 触发条件：`imports` calloc 失败
- 后果：`if (imports)` 为假时静默跳过全部导入处理 → 生成无导入表的 PE
- 最小修复：calloc 后检查 NULL 并报错
- 备注：【修订 G1】calloc 失败为活路径、零诊断

#### [高] Resolver-8｜exports calloc 失败静默跳过全部导出
- 位置：`Resolver.c:536-563`
- 类别：静默失败 / 未检查前置条件
- 触发条件：`exports` calloc 失败
- 后果：静默跳过全部导出
- 最小修复：calloc 后检查 NULL 并报错
- 备注：【修订 G1】calloc 失败为活路径、零诊断

#### [高] Resolver-9｜sym->name==NULL 时 strdup(NULL)
- 位置：`Resolver.c:551`
- 类别：未检查前置条件
- 触发条件：`sym->name == NULL`（`LoaderKro.c:270` 允许）
- 后果：`strdup(NULL)` 崩溃
- 最小修复：检查 NULL
- 备注：—

#### [高] Resolver-10｜rename_table_lookup 内 strcmp(local_name, NULL)
- 位置：`Resolver.c:439,177`
- 类别：未检查前置条件
- 触发条件：重定位符号名 NULL
- 后果：`strcmp(local_name, NULL)` 崩溃
- 最小修复：检查 NULL
- 备注：—

#### [中] Resolver-11｜未定义符号 fprintf 绕过 session logger
- 位置：`Resolver.c:448`
- 类别：错误处理不一致
- 触发条件：未定义符号
- 后果：直接 `fprintf(stderr,...)` 绕过 session logger；`LinkerNative.c:78` 又写 "Symbol resolution failed"，双重且不一致
- 最小修复：统一走 logger
- 备注：—

#### [中] Resolver-12｜注释与实现脱节（zero 是否有效定义）
- 位置：`Resolver.c:250` vs `Resolver.h:19`
- 类别：注释与实现脱节
- 触发条件：—
- 后果：头文件注释"zero is a valid definition"，实现 `defined = section_index > 0`（0=未定义）
- 最小修复：统一注释/实现
- 备注：—

#### [高] Resolver-13｜未定义 main 被硬编码强制定义
- 位置：`Resolver.c:268-277`
- 类别：语义错误
- 触发条件：`extern int main()` 未定义
- 后果：硬编码把名为 main 的未定义符号强制 `defined=1` 并指向 CODE 段
- 最小修复：移除硬编码，按正常解析
- 备注：—

#### [中] Resolver-14｜四处空块/空循环
- 位置：`Resolver.c:263-264,424-427,401-402,522-523`
- 类别：死代码
- 触发条件：—
- 后果：4 处空块/空循环
- 最小修复：删除或补实现
- 备注：—

#### [中] Resolver-15｜局部符号名+单元号超长 snprintf 截断
- 位置：`Resolver.c:255`
- 类别：未检查前置条件
- 触发条件：局部符号名 + 单元号 ≥ 512 字节
- 后果：`snprintf` 截断 → 重命名冲突，重定位解析错
- 最小修复：检查截断并报错
- 备注：—

#### [中] Resolver-16｜image_base 两处设置语义分叉
- 位置：`Resolver.c:485` vs `LinkerNative.c:91-97`
- 类别：语义分叉
- 触发条件：—
- 后果：`image_base` 两处设置（resolver 硬编码 0x140000000，native 按 target 覆盖）
- 最小修复：统一设置点
- 备注：—

#### [中] Resolver-17｜reloc_table.relocs 双所有者别名
- 位置：`Resolver.c:483-484,576`
- 类别：共享可变状态
- 触发条件：—
- 后果：`reloc_table.relocs` 同时赋给 `backend_input->relocs` 与 `plan->relocs`，两所有者别名
- 最小修复：明确单一所有权
- 备注：—

#### [致命] BackendCommon-1｜unique_modules calloc 未判空即写入
- 位置：`ArkLink/src/Backend/BackendCommon.c:521,524-541`
- 类别：未检查前置条件 / 静默失败
- 触发条件：`count` 较大或内存不足时 calloc 返回 NULL
- 后果：紧接着 `unique_modules[unique_count++] = mod` → NULL 解引用崩溃
- 最小修复：calloc 后 `if (!unique_modules) { free(group); return NULL; }`
- 备注：【修订 G1】calloc 失败为活路径、零诊断

#### [致命] BackendCommon-2｜imports[i].module 为 NULL 时 strcmp 崩溃
- 位置：`ArkLink/src/Backend/BackendCommon.c:562`
- 类别：未检查前置条件
- 触发条件：任一 import 的 module 为 NULL（第一遍 526 行只跳过统计，未从池中剔除）
- 后果：第二遍 `strcmp(imports[i].module, unique_modules[m])` → `strcmp(NULL, ...)` 崩溃
- 最小修复：第二遍同样 `if (!imports[i].module) continue;`，或第一遍整体排除
- 备注：—

#### [致命] BackendCommon-3｜重定位 offset 加法溢出致越界写
- 位置：`ArkLink/src/Backend/BackendCommon.c:386,393,400`
- 类别：整数溢出 / 未检查前置条件
- 触发条件：恶意/损坏 `.kro`/ELF 重定位 offset 接近 SIZE_MAX
- 后果：`proc->offset + 2/4/8 <= size` 加法回绕，检查通过，随后 `memcpy(data+offset,...)` 越界写
- 最小修复：改为 `proc->offset <= size - 2` 等减法形式，并先校验 `offset < size`
- 备注：—

#### [致命] BackendCommon-4｜section_count>0 但 sections==NULL 仍取地址
- 位置：`ArkLink/src/Backend/BackendCommon.c:250-252`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：上游构造 `ArkBackendInput` 时 section_count 非零而 sections 未分配
- 后果：直接 `&input->sections[i]` → NULL 解引用崩溃
- 最小修复：`if (input->section_count && !input->sections) return NULL;` 并在 `Backend.h` 文档化前置条件
- 备注：—

#### [致命] BackendCommon-5｜virtual_address+aligned_size 溢出致镜像布局错位
- 位置：`ArkLink/src/Backend/BackendCommon.c:282`
- 类别：整数溢出
- 触发条件：超大 section size
- 后果：`sec->virtual_address + ark_backend_align_up(sec->virtual_size, section_alignment)` 无溢出检查 → `sec_end` 回绕 → `code_segment_end`/`data_segment_end` 计算错误
- 最小修复：`__builtin_add_overflow` 或 `if (sec->virtual_address > UINT64_MAX - aligned) return NULL;`
- 备注：—

#### [高] BackendCommon-6｜重定位未应用静默返回
- 位置：`ArkLink/src/Backend/BackendCommon.c:346-349,374-375,405`
- 类别：静默失败 / 诊断缺失
- 触发条件：`ark_reloc_apply_elf` 中 `!data/!proc/offset>=size`；action `default`；未知 field_size
- 后果：直接 `return`/静默不写；调用方 `ark_reloc_process_all` 无法感知 → 生成错误目标文件但链接"成功"
- 最小修复：返回 `int` 错误码或至少 `fprintf(stderr,...)` 带 section/offset 诊断
- 备注：—

#### [高] BackendCommon-7｜PE 重定位写死偏移 0 忽略 proc->offset
- 位置：`ArkLink/src/Backend/BackendCommon.c:433-449`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：PE 后端处理任何重定位
- 后果：field_size 分支写死 `memcpy(data,...)`（偏移 0），完全忽略 `proc->offset`；ELF 版本（378-405）使用 `proc->offset` → 所有 PE 重定位写到 section 起始处，必然错位
- 最小修复：统一使用 `proc->offset` 并做边界检查；抽出唯一 `ark_reloc_write_field()`
- 备注：—

#### [高] BackendCommon-8｜未知 reloc 类型静默 continue
- 位置：`ArkLink/src/Backend/BackendCommon.c:496-497`
- 类别：静默失败
- 触发条件：Loader 映射出未知类型（如 `LoaderElf.c:481-483` 默认 ABS64 或未来新类型）
- 后果：重定位静默丢弃，生成错误镜像
- 最小修复：记录诊断并返回错误计数，或至少在 `ARK_DEBUG` 下输出
- 备注：—

#### [高] BackendCommon-9｜PE GENERATE_BASE_REL/action default 静默返回
- 位置：`ArkLink/src/Backend/BackendCommon.c:408-431`
- 类别：静默失败
- 触发条件：`ARK_RELOC_GENERATE_BASE_REL` 或未知 action
- 后果：静默返回，重定位未应用
- 最小修复：返回错误
- 备注：—

#### [高] BackendCommon-10｜symbols calloc 失败仍计入 module
- 位置：`ArkLink/src/Backend/BackendCommon.c:567-570`
- 类别：静默失败 / 契约破坏
- 触发条件：`symbols = calloc(sym_count, ...)` 失败
- 后果：`continue`，该 module 的 `symbol_count` 保持 0，但 `module_count` 已计入 → 下游看到存在但无符号的模块，可能生成空导入表项或误判
- 最小修复：失败时回滚已分配资源并返回 NULL
- 备注：【修订 G1】calloc 失败为活路径、零诊断

#### [高] BackendCommon-11｜ark_buffer_add_string 失败哨兵冲突
- 位置：`ArkLink/src/Backend/BackendCommon.c:150-166`
- 类别：契约破坏 / 诊断缺失
- 触发条件：追加失败或偏移超限
- 后果：失败返回 `(uint32_t)-1`（UINT32_MAX），与 156-158 行溢出时返回的 UINT32_MAX 哨兵冲突 → 调用方无法区分
- 最小修复：引入独立错误码或输出参数
- 备注：—

#### [高] BackendCommon-12｜buffer 错误信号三种不一致
- 位置：`ArkLink/src/Backend/BackendCommon.c:175-183`
- 类别：错误处理不一致
- 触发条件：`ark_buffer_get_rva`/`ark_buffer_get_string` 出错
- 后果：`!buf` 返回 0、溢出返回 UINT32_MAX；而 `ark_buffer_get_string`（168-173）用 NULL 表示错误 → 同层三种错误信号
- 最小修复：统一为返回 NULL/0 并加 `errno` 风格输出参数
- 备注：—

#### [高] BackendCommon-13｜ark_buffer_align 忽略 append_zero 失败
- 位置：`ArkLink/src/Backend/BackendCommon.c:185-196`
- 类别：静默失败
- 触发条件：`ark_buffer_append_zero` 返回 `(size_t)-1`
- 后果：对齐未完成但 size 未变 → 后续 section 偏移错位
- 最小修复：检查返回值并向上传播
- 备注：—

#### [高] BackendCommon-14｜layout_get_section 返回 NULL 时 VA 静默置 0
- 位置：`ArkLink/src/Backend/BackendCommon.c:500-506`
- 类别：静默失败
- 触发条件：`reloc->section_index >= section_count`
- 后果：`p_vaddr` 静默置 0，重定位按 VA=0 计算 → PC-relative 重定位算错
- 最小修复：检查 NULL 并记录诊断
- 备注：—

#### [高] BackendCommon-15｜unique_modules 借用指针所有权不明
- 位置：`ArkLink/src/Backend/BackendCommon.c:539`
- 类别：契约破坏
- 触发条件：调用方释放 imports 字符串
- 后果：`unique_modules[unique_count++] = mod` 借用 `imports[i].module` 指针（非常量所有权），`ark_import_group_destroy` 不释放 `module_name` → 悬垂
- 最小修复：在 `Backend.h` 明确所有权，或内部 `strdup`
- 备注：—

#### [高] BackendCommon-16｜两处空 #ifdef ARK_DEBUG 块
- 位置：`ArkLink/src/Backend/BackendCommon.c:353-354,364-365`
- 类别：死代码
- 触发条件：release 构建
- 后果：永久为空
- 最小修复：删除或填入实际调试代码
- 备注：—

#### [中] BackendCommon-17｜未知 field_size 默认按 32 位处理
- 位置：`ArkLink/src/Backend/BackendCommon.c:17-19`
- 类别：静默失败
- 触发条件：未知 `field_size`
- 后果：`ark_backend_calc_pc_relative` 默认 `p_vaddr + 4`，把非法枚举当 32 位
- 最小修复：`default: return 0;` 或断言
- 备注：—

#### [中] BackendCommon-18｜alignment==0 直接返回原值
- 位置：`ArkLink/src/Backend/BackendCommon.c:22-29`
- 类别：静默失败 / 契约破坏
- 触发条件：`alignment == 0` 或溢出
- 后果：非法对齐被接受；溢出静默 clamp 到 UINT64_MAX → 布局错误
- 最小修复：`alignment == 0` 返回错误或断言 1
- 备注：—

#### [中] BackendCommon-19｜align_up_32 溢出静默 clamp
- 位置：`ArkLink/src/Backend/BackendCommon.c:31-34`
- 类别：静默失败
- 触发条件：溢出
- 后果：静默 clamp 到 UINT32_MAX，调用方无感知
- 最小修复：提供 `int* ok` 输出或返回 0 并文档化
- 备注：—

#### [中] BackendCommon-20｜new_cap 分支后恒假判断
- 位置：`ArkLink/src/Backend/BackendCommon.c:97-100`
- 类别：死代码
- 触发条件：—
- 后果：`new_cap = capacity <= SIZE_MAX/2 ? capacity*2 : needed;` 之后 `if (new_cap < needed)` 恒假
- 最小修复：删除
- 备注：—

#### [中] BackendCommon-21｜未知 section kind 默认 DATA
- 位置：`ArkLink/src/Backend/BackendCommon.c:198-215`
- 类别：静默失败
- 触发条件：新增 section 类型未更新
- 后果：`classify_section_kind` 默认 `ARK_SEGMENT_DATA` → 段属性错误
- 最小修复：`default` 返回错误或 `ARK_SEGMENT_UNKNOWN` 并由调用方拒绝
- 备注：—

#### [中] BackendCommon-22｜internal 判定范围不含 capacity 区
- 位置：`ArkLink/src/Backend/BackendCommon.c:112-126`
- 类别：未检查前置条件
- 触发条件：`data` 指向 `[data+size, data+capacity)`（未初始化区）
- 后果：被判为外部，`memmove` 读取未初始化内存并追加
- 最小修复：判定范围改为 `< buf->capacity`，或拒绝非常规指针
- 备注：—

#### [中] BackendCommon-23｜VA/文件偏移累加无溢出检查
- 位置：`ArkLink/src/Backend/BackendCommon.c:246-248`
- 类别：整数溢出
- 触发条件：多 section 大尺寸
- 后果：`current_va`/`current_file_offset` 持续累加 `ark_backend_align_up(...)` 回绕 → 镜像布局错乱
- 最小修复：每次累加检查 `> UINT64_MAX - delta`
- 备注：—

#### [中] BackendCommon-24｜不可信 size 对齐后累加回绕
- 位置：`ArkLink/src/Backend/BackendCommon.c:267`
- 类别：整数溢出
- 触发条件：`input_sec->size` 被 clamp 成 UINT64_MAX
- 后果：`ark_backend_align_up(input_sec->size, section_alignment)` 后 `current_va += aligned_size` 回绕
- 最小修复：同上，加溢出检查
- 备注：—

#### [中] BackendCommon-25｜无数据段与全 BSS 共用 image_size 公式
- 位置：`ArkLink/src/Backend/BackendCommon.c:313-316`
- 类别：语义分叉 / 静默失败
- 触发条件：无数据段；或存在数据段但全部 BSS
- 后果：无数据段时 `data_segment_start/end` 强制设为 `code_segment_end` 使 `image_size` 恰好等于代码段尾；全 BSS 时 `data_segment_start` 来自 BSS VA、`file_size` 不含 BSS，二者语义不同却共用同一公式
- 最小修复：分离 `image_size`（含 BSS）与 `file_size` 计算路径并断言
- 备注：—

#### [中] BackendCommon-26｜import_group_destroy 不释放 module_name
- 位置：`ArkLink/src/Backend/BackendCommon.c:584-597`
- 类别：注释与契约
- 触发条件：调用方误以为拥有 module_name
- 后果：只释放 `symbols`/`modules`，不释放借用的 `module_name`；函数名暗示完整销毁 → 可能双重释放
- 最小修复：文档化或改为深拷贝
- 备注：—

#### [中] BackendCommon-27｜weak 标志用裸数字 5
- 位置：`ArkLink/src/Core/NativeAbi.c:157`
- 类别：魔法数字 / 未检查前置条件
- 触发条件：非法 flags 值（flags 来自 manifest）
- 后果：`record.weak = flags == 5;` 无枚举校验 → weak 语义错乱
- 最小修复：定义 `enum { ARK_ABI_FLAG_WEAK = 5 }` 并校验取值集合
- 备注：—

#### [中] BackendCommon-28｜expected_size 冗余算术易失配
- 位置：`ArkLink/src/Core/NativeAbi.c:148`
- 类别：可读性 / 潜在维护缺陷
- 触发条件：布局改动
- 后果：`expected_size = 6 + nominal_size - 6 + 2 + 16` 冗余算术易失配
- 最小修复：`nominal_size + 18` 并注释布局
- 备注：—


#### [致命] BackendPe-1｜section_maps 索引无范围检查
- 位置：`ArkLink/src/Backend/BackendPe.c:246,279,311`
- 类别：未检查前置条件
- 触发条件：`reloc->section_index >= input->section_count`（`temp_maps` 仅 `input->section_count` 项，见 :685）
- 后果：越界读，重定位表 RVA 错乱或崩溃
- 最小修复：`if (reloc->section_index >= input->section_count) continue/return ERR;`
- 备注：—

#### [致命] BackendPe-2｜符号 section_index 越界读
- 位置：`ArkLink/src/Backend/BackendPe.c:1087`
- 类别：未检查前置条件
- 触发条件：`reloc->symbol->section_index` 越界（`section_maps` 共 `total_sections` 项；如 LoaderElf 把 `SHN_ABS` 映射为 0、LoaderKro 把 `.bss` 映射为 0 之外的情况）
- 后果：`output->section_maps[...].rva` 越界读
- 最小修复：加 `section_index < output->section_count` 检查
- 备注：—

#### [致命] BackendPe-3｜file_offset 无范围检查且可溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:1090`
- 类别：未检查前置条件
- 触发条件：`output->section_maps[reloc->section_index].file_offset` 无范围检查；`+ reloc->offset` 可溢出
- 后果：越界指针，随后 :1122 写越界
- 最小修复：校验 `section_index` 与 `file_offset + offset <= output->size`
- 备注：—

#### [致命] BackendPe-4｜reloc_file_offset 整数下溢导致堆越界写
- 位置：`ArkLink/src/Backend/BackendPe.c:1122`
- 类别：整数下溢 / 未检查前置条件
- 触发条件：`reloc_file_offset`（uint32_t）> `output->size`（size_t），`output->size - reloc_file_offset` 下溢为巨大值，`ark_reloc_apply_pe_base` 的 `size >= 4/8` 检查通过
- 后果：堆越界写（损坏重定位 offset 触发）
- 最小修复：`if (reloc_file_offset >= output->size) continue/return ERR;`
- 备注：—

#### [致命] BackendPe-5｜导出序号下溢 / 越界写
- 位置：`ArkLink/src/Backend/BackendPe.c:629`
- 类别：未检查前置条件 / 整数下溢
- 触发条件：`eat + (exp->ordinal - input->export_ordinal_base) * sizeof(address)`，`ordinal` 与 `base` 均 uint32_t；`ordinal < base` 下溢为巨大 size_t，或 `ordinal - base >= export_count`
- 后果：堆越界写
- 最小修复：`uint64_t idx = (uint64_t)exp->ordinal - base; if (idx >= count) return ERR;`
- 备注：—

#### [致命] BackendPe-6｜reloc_count 乘法溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:230`
- 类别：整数溢出
- 触发条件：`reloc_count > SIZE_MAX/4`，`malloc(input->reloc_count * sizeof(uint32_t))` 乘法未检查
- 后果：分配不足，:257 `page_rvas[page_count++]` 堆溢出
- 最小修复：`if (reloc_count > SIZE_MAX/sizeof(uint32_t)) return NULL;`
- 备注：—

#### [致命] BackendPe-7｜block_size/total_size 溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:285-289,320`
- 类别：整数溢出
- 触发条件：`block_size = 8 + ((count + 1) & ~1) * 2` 与 `total_size += block_size` 未检查溢出；`calloc(1, total_size)` 后 :320 按 `count` 写 entry（单页重定位数量极大）
- 后果：堆溢出写
- 最小修复：每步检查 `count > (SIZE_MAX-8)/2`、`total_size > SIZE_MAX - block_size`
- 备注：—

#### [致命] BackendPe-8｜import_count 乘法溢出 + NULL module
- 位置：`ArkLink/src/Backend/BackendPe.c:364`
- 类别：整数溢出 + 未检查前置条件
- 触发条件：`calloc(input->import_count, sizeof(char*))` 乘法未检查；:369-380 把可能为 NULL 的 `input->imports[i].module` 直接 `strcmp`/`modules[module_count++]`
- 后果：崩溃或堆溢出
- 最小修复：检查乘法溢出；跳过 NULL module 并诊断
- 备注：—

#### [致命] BackendPe-9｜strlen(NULL) 崩溃
- 位置：`ArkLink/src/Backend/BackendPe.c:469-470`
- 类别：未检查前置条件
- 触发条件：`input->imports[import_idx].symbol` 为 NULL（`BackendCommon.c:574` 明确允许 symbol 为 NULL 跳过，PE 路径未同步该契约）
- 后果：`strlen(sym_name)` 解引用 NULL 崩溃
- 最小修复：`if (!sym_name) { free(sym_indices); goto fail; }` 或跳过
- 备注：—

#### [致命] BackendPe-10｜export 相关乘法溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:561,565,570-572,588,596`
- 类别：整数溢出
- 触发条件：`malloc(input->export_count * sizeof(ArkExportEntry))`、`memcpy(..., export_count * sizeof(...))`、`count * sizeof(uint32_t)`（三处）、`malloc(count*sizeof(size_t))`（两处）均未检查乘法溢出（超大 export_count）
- 后果：分配不足 → 堆溢出
- 最小修复：统一 `if (count > SIZE_MAX/sizeof(T)) goto fail;`
- 备注：—

#### [致命] BackendPe-11｜section_count 乘法溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:685,789`
- 类别：整数溢出
- 触发条件：`calloc(input->section_count, sizeof(ArkSectionRvaMap))`、`calloc(total_sections, sizeof(ArkSectionRvaMap))` 未检查
- 后果：分配不足 → 后续写溢出
- 最小修复：检查 `count > SIZE_MAX/sizeof(...)`
- 备注：—

#### [致命] BackendPe-12｜头尺寸计算溢出/截断
- 位置：`ArkLink/src/Backend/BackendPe.c:743-748`
- 类别：整数溢出 / 阶段不对齐
- 触发条件：`total_sections * sizeof(PE_SECTION_HEADER)` 赋给 uint32_t `section_table_size`，`total_header_size = dos_header_size + pe_header_size + section_table_size` 可溢出/截断
- 后果：`headers_size` 计算错误，PE 头越界或截断
- 最小修复：用 uint64_t 计算并检查 `<= UINT32_MAX`
- 备注：—

#### [致命] BackendPe-13｜RVA 计算溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:726-728,819-821`
- 类别：整数溢出
- 触发条件：`edata_rva = idata_rva + (uint32_t)((import_data_size + section_alignment - 1) & ~(section_alignment - 1))`；`import_data_size + 0xFFF` 可溢出，`idata_rva + ...` 可溢出；`reloc_rva` 同理
- 后果：段 RVA 回绕，数据目录指向错误位置
- 最小修复：用 uint64_t 计算并检查
- 备注：—

#### [致命] BackendPe-14｜SizeOfCode 等累加溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:879-886,890-896`
- 类别：整数溢出
- 触发条件：`(input->sections[i].size + section_alignment - 1)` 中 size 为 size_t 可溢出；`size_of_code += aligned_size` 累加溢出
- 后果：PE 头 SizeOfCode/SizeOfInitializedData 错误
- 最小修复：统一使用 `ark_backend_align_up`（已处理 clamp）而非手写表达式
- 备注：—

#### [致命] BackendPe-15｜section 文件偏移/RVA 回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:967,1006,1026`
- 类别：整数溢出 / 阶段不对齐
- 触发条件：`raw_size = (uint32_t)((sec->size + file_alignment - 1) & ~(file_alignment - 1))` 可溢出并截断；`current_rva += (virtual_size + section_alignment - 1) & ~(section_alignment - 1)` 中 `virtual_size` 为 uint32_t，`+0xFFF` 可溢出；`current_file_offset += raw_size` 累加溢出
- 后果：section 文件偏移/RVA 回绕，`memcpy` 越界
- 最小修复：全部改用 uint64_t 与 `ark_backend_align_up`，并检查 `<= UINT32_MAX`
- 备注：—

#### [致命] BackendPe-16｜两套头尺寸计算分叉
- 位置：`ArkLink/src/Backend/BackendPe.c:922-923`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：`headers_size_calc = 0x80 + 4 + sizeof(PE_COFF_HEADER) + sizeof(PE_OPTIONAL_HEADER_64) + total_sections * 40` 与 :750 `headers_size = ark_backend_align_up_32(total_header_size, file_alignment)` 是两套计算；:747 `section_table_size` 是 uint32_t 截断值，:922 用 `total_sections * 40`（size_t 不截断）
- 后果：section 数使 `total_sections*40 > UINT32_MAX` 时两者不一致，`SizeOfHeaders` 与实际头布局不符
- 最小修复：抽出 `ark_pe_calc_header_size()`，两处共用
- 备注：—

#### [致命] BackendPe-17｜current_rva 初值与 layout 口径不一致
- 位置：`ArkLink/src/Backend/BackendPe.c:959-960`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：`current_rva = section_alignment`（固定 0x1000），而 `ark_layout_create` 首段 VA 为 `image_base + ark_backend_align_up(headers_size, section_alignment)`；`headers_size > 0x1000`（section 数 > ~(0x1000-0x200)/40 ≈ 56）时 `align_up = 0x2000`，`temp_maps[0].rva = 0x2000` 但 `current_rva = 0x1000`
- 后果：`output->section_maps[].rva` 与 section 头 `VirtualAddress` 及后续重定位写位置全部错位，镜像损坏
- 最小修复：`current_rva` 初始化为 `ark_backend_align_up(headers_size, section_alignment)`，并与 layout 对齐口径统一
- 备注：—

#### [致命] BackendPe-18｜section size 截断为 uint32_t
- 位置：`ArkLink/src/Backend/BackendPe.c:962-1008`
- 类别：整数溢出 / 截断
- 触发条件：`virtual_size = (uint32_t)sec->size`、`raw_size = (uint32_t)...`、`virtual_address = current_rva` 均把 size_t 截断为 uint32_t，而 `total_file_size` 用 64 位 `sec->file_size` 累加（section size > 4GB）
- 后果：`memcpy(output->data + raw_offset, sec->data, sec->size)` 越界（raw_offset 按截断值计算，size 按原始值）
- 最小修复：入口拒绝 `sec->size > UINT32_MAX`
- 备注：—

#### [高] BackendPe-19｜strcmp 的 module 名可能为 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:373,422,434`
- 类别：未检查前置条件
- 触发条件：`strcmp(modules[j], mod)`、`strcmp(input->imports[i].module, module_name)` 中 `mod`/`module_name` 可能为 NULL（:369-380 未过滤 NULL module）
- 后果：崩溃
- 最小修复：统一过滤 NULL module
- 备注：—

#### [高] BackendPe-20｜重定位 symbol 名 strcmp 未判 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:1078`
- 类别：未检查前置条件
- 触发条件：`strcmp(input->imports[j].symbol, reloc->symbol->name)` 两者均可能为 NULL
- 后果：崩溃
- 最小修复：跳过 NULL 并诊断
- 备注：—

#### [高] BackendPe-21｜导入符号匹配失败静默用第一个 IAT 槽
- 位置：`ArkLink/src/Backend/BackendPe.c:1083`
- 类别：静默失败
- 触发条件：导入符号匹配失败
- 后果：`import_idx` 保持 0，静默使用第一个导入的 IAT 槽地址 → 符号地址错乱
- 最小修复：匹配失败时诊断并跳过/报错
- 备注：—

#### [高] BackendPe-22｜未知重定位类型静默丢弃
- 位置：`ArkLink/src/Backend/BackendPe.c:1108-1109`
- 类别：静默失败 / 诊断缺失
- 触发条件：未知 `reloc->type`，`default: continue`
- 后果：重定位静默丢弃，无诊断
- 最小修复：记录诊断并统计失败数
- 备注：—

#### [高] BackendPe-23｜段 data 为 NULL 静默留零
- 位置：`ArkLink/src/Backend/BackendPe.c:1002-1004`
- 类别：静默失败
- 触发条件：`sec->data == NULL && sec->size > 0`
- 后果：段内容丢失但链接成功
- 最小修复：返回 `ARK_LINK_ERR_INVALID_ARGUMENT` 或诊断
- 备注：—

#### [高] BackendPe-24｜入口段无效静默用 0x1000
- 位置：`ArkLink/src/Backend/BackendPe.c:809-815`
- 类别：静默失败
- 触发条件：`entry_section` 无效
- 后果：静默用 `entry_point_rva = 0x1000`（首段起点）→ 入口点错误
- 最小修复：无效时返回错误
- 备注：—

#### [高] BackendPe-25｜段名超 8 字符静默截断
- 位置：`ArkLink/src/Backend/BackendPe.c:205-207`
- 类别：静默失败
- 触发条件：section 名超 8 字符
- 后果：同名段冲突
- 最小修复：诊断或改用 `/42` 格式（COFF 长名）
- 备注：—

#### [高] BackendPe-26｜导出序号回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:619`
- 类别：整数下溢 / 静默失败
- 触发条件：`(uint16_t)(exp->ordinal - input->export_ordinal_base)`，`ordinal < base` 时回绕为大正数写入 `eot`
- 后果：导出序号错乱
- 最小修复：检查 `ordinal >= base`
- 备注：—

#### [高] BackendPe-27｜64 位 VA/size 多处截断
- 位置：`ArkLink/src/Backend/BackendPe.c:694-695,711,751,852,965-967,1014,1033,1053`
- 类别：整数截断（阶段不对齐）
- 触发条件：镜像 > 4GB，多处把 64 位 VA/size 截断为 uint32_t（`temp_maps[].rva`、`idata_rva`、`image_size`、`NumberOfSections`、`virtual_size/raw_size`、各段头 size）
- 后果：字段回绕，加载器误读
- 最小修复：入口检查 `<= UINT32_MAX`
- 备注：—

#### [高] BackendPe-28｜PE 可选头字段硬编码，忽略 input
- 位置：`ArkLink/src/Backend/BackendPe.c:925,928-931`
- 类别：配置漂移
- 触发条件：`Subsystem`、`SizeOfStackReserve/Commit`、`SizeOfHeapReserve/Commit` 全硬编码，忽略 `input->subsystem`、`input->stack_size`（`Backend.h:170-171` 已声明）
- 后果：CLI/session 设置无效
- 最小修复：读 `input` 字段，缺省再回退默认
- 备注：与 Header-7 同源

#### [高] BackendPe-29｜对齐值硬编码
- 位置：`ArkLink/src/Backend/BackendPe.c:674-675,909`
- 类别：配置漂移 / 魔法数字
- 触发条件：`section_alignment=0x1000`、`file_alignment=0x200` 硬编码，忽略 `input` 可能携带的对齐；`opt->BaseOfCode = section_alignment` 亦硬编码
- 后果：配置无效
- 最小修复：从 `input`/layout 读取并文档化
- 备注：—

#### [高] BackendPe-30｜SIZE_MAX 错误哨兵与 ark_buffer_append 契约冲突
- 位置：`ArkLink/src/Backend/BackendPe.c:222`
- 类别：契约破坏
- 触发条件：`*out_size = SIZE_MAX` 作为初始错误哨兵，但 `Backend.h:34` 规定 `ark_buffer_append` 失败返回 `SIZE_MAX`；调用方 :700 用 `reloc_data_size != 0` 判断
- 后果：语义混淆
- 最小修复：用独立 `int* ok` 或初始化 0
- 备注：—

#### [高] BackendPe-31｜import_builder_get_rva 失败值写入 IAT
- 位置：`ArkLink/src/Backend/BackendPe.c:484-488`
- 类别：静默失败
- 触发条件：`import_builder_get_rva` 失败返回 `UINT32_MAX`
- 后果：被直接写入 ILT/IAT 条目 → 导入地址错误
- 最小修复：检查返回值
- 备注：—

#### [高] BackendPe-32｜module_count==0 时 IAT RVA 静默为 0
- 位置：`ArkLink/src/Backend/BackendPe.c:501-504`
- 类别：静默失败
- 触发条件：`module_count == 0`（所有 import 的 module 均为 NULL）
- 后果：`descriptor` 从全零缓冲读出，`*out_iat_rva = 0` 静默 → IAT 数据目录指向 0
- 最小修复：`module_count == 0` 时返回错误
- 备注：—

#### [高] BackendPe-33｜export name 未判 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:543-547,566,593,616`
- 类别：未检查前置条件
- 触发条件：`compare_export_entries` 与 `strlen(sorted_exports[i].name)` 未判 `name == NULL`
- 后果：崩溃
- 最小修复：入口校验 export name 非 NULL
- 备注：—

#### [中] BackendPe-34｜sorted_exports 初始化时机（疑似）
- 位置：`ArkLink/src/Backend/BackendPe.c:659-662`
- 类别：资源与生命周期（疑似）
- 触发条件：`sorted_exports` 在 :561 赋值前未初始化；若 `malloc` 失败 `goto fail` 执行 `free(sorted_exports)`——此时值为 NULL（赋值已发生），安全；但若编译器/优化导致未赋值路径则未定义
- 后果：潜在未定义行为
- 最小修复：显式 `= NULL`
- 备注：疑似（原严重度：中）

#### [高] BackendPe-35｜错误码误报为 MEMORY
- 位置：`ArkLink/src/Backend/BackendPe.c:670,801,870`
- 类别：错误处理不一致
- 触发条件：成功路径返回 `ARK_LINK_OK`，失败统一 `ARK_LINK_ERR_MEMORY`，但 :860 `opt_offset + sizeof > total_file_size` 属格式错误而非内存不足
- 后果：错误码误报
- 最小修复：引入 `ARK_LINK_ERR_FORMAT`/`ERR_LAYOUT`
- 备注：—

#### [中] BackendPe-36｜段 kind 硬编码枚举
- 位置：`ArkLink/src/Backend/BackendPe.c:982-993`
- 类别：魔法数字 / 语义分叉
- 触发条件：`sec->kind == 1/2/3` 硬编码枚举值（虽与 `ArkSectionKind` 当前值一致），与 `BackendCommon.c:198-215` 的 `classify_section_kind` 是两套映射
- 后果：映射漂移风险
- 最小修复：复用 `classify_section_kind` 或改用枚举常量
- 备注：原编号 37

#### [中] BackendPe-37｜reloc_rva 偏移溢出回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:246,279,311`
- 类别：整数截断
- 触发条件：`section_maps[...].rva + reloc->offset`（uint32_t + size_t）赋给 uint32_t `reloc_rva`，偏移溢出时回绕
- 后果：RVA 错误
- 最小修复：用 uint64_t 并检查
- 备注：原编号 38

#### [中] BackendPe-38｜file_offset + offset 截断
- 位置：`ArkLink/src/Backend/BackendPe.c:1090`
- 类别：整数截断
- 触发条件：`file_offset + reloc->offset` 赋给 uint32_t
- 后果：偏移错误
- 最小修复：uint64_t
- 备注：原编号 39

#### [中] BackendPe-39｜导出 address 加法回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:624-628`
- 类别：整数溢出
- 触发条件：`section_maps[exp->section_index].rva + exp->offset`（uint32_t + size_t）赋给 uint32_t `address`，可回绕
- 后果：地址错误
- 最小修复：uint64_t 并检查
- 备注：原编号 40

#### [中] BackendPe-40｜export_count 截断
- 位置：`ArkLink/src/Backend/BackendPe.c:642-643`
- 类别：整数截断
- 触发条件：`(uint32_t)input->export_count`
- 后果：数量回绕
- 最小修复：检查 `<= UINT32_MAX`
- 备注：原编号 41

#### [中] BackendPe-41｜align_up 返回值截断
- 位置：`ArkLink/src/Backend/BackendPe.c:831-841`
- 类别：整数截断
- 触发条件：`ark_backend_align_up(...)` 返回 uint64_t 直接 cast uint32_t
- 后果：对齐值错误
- 最小修复：检查
- 备注：原编号 42

#### [中] BackendPe-42｜IAT 目录 Size 口径可能少算（疑似）
- 位置：`ArkLink/src/Backend/BackendPe.c:949-951`
- 类别：语义分叉
- 触发条件：`actual_iat_entries = import_iat_total_entries - 1` 假定最后一项是终止符，但 `total_entries = import_count + module_count` 中每个 module 的终止符占一项，实际有效项数应为 `import_count`；减 1 的口径与 IAT 布局不一致
- 后果：数据目录 Size 可能少算（需验证 Windows 加载器口径）
- 最小修复：验证后统一口径
- 备注：疑似（原编号 43，原严重度：中）

#### [中] BackendPe-43｜多 module 时全局 IAT RVA 只取第一个（疑似）
- 位置：`ArkLink/src/Backend/BackendPe.c:501`
- 类别：语义分叉
- 触发条件：`memcpy(&descriptor, builder->data + idt_offset, ...)` 取第一个 module 的 IAT RVA 作为全局 IAT RVA；多 module 时其余 module 的 IAT 位置未被数据目录引用（依赖 IAT 连续布局）
- 后果：多 module 下 IAT 引用可能不完整（需验证）
- 最小修复：验证后修正
- 备注：疑似（原编号 44，原严重度：中）

#### [中] BackendPe-44｜布局错误返回 MEMORY 且 free 未判 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:860-872`
- 类别：错误处理不一致
- 触发条件：失败路径返回 `ARK_LINK_ERR_MEMORY`，但 `opt_offset + sizeof(...) > total_file_size` 是布局错误；此处 `free(export_data)` 未判 NULL（`free(NULL)` 安全）
- 后果：错误码误报
- 最小修复：引入布局错误码
- 备注：原编号 45

#### [致命] ELF-1｜imports 的 symbol/module 为 NULL 时 strcmp 崩溃
- 位置：`ArkLink/src/Backend/BackendElf.c:533,536`
- 类别：未检查前置条件（NULL 解引用）
- 触发条件：`imports[i].symbol == NULL` 或 `imports[i].module == NULL`；可达路径：`Resolver.c:500-501` 的 `strdup` 返回值未检查（OOM 时字段为 NULL），或任何直接构造 `ArkBackendInput` 的调用方传 NULL
- 后果：`strcmp(seen_names[k], NULL)` → SIGSEGV，链接器崩溃，无诊断
- 最小修复：:532 循环内加 `sym &&` / `mod &&`，与 :540/544 的判空一致；或在 `elf_prepare` 入口拒绝 NULL
- 备注：—

#### [致命] ELF-2｜static_loads 上限 48 静默丢段
- 位置：`ArkLink/src/Backend/BackendElf.c:907-909`
- 类别：静默失败 + 死上限
- 触发条件：静态链接（`!has_dynamic && !has_tls`）且 section 数 > 48
- 后果：`static_loads[48]` 写满后直接 `break`，后续 section 的 PT_LOAD 被静默丢弃，镜像缺段 → 加载崩溃/数据错误，无任何诊断
- 最小修复：改为动态数组，或超限返回 `ARK_LINK_ERR_FORMAT` 并打印段索引
- 备注：—

#### [致命] ELF-3｜动态段 PLT 标签覆盖既有表项
- 位置：`ArkLink/src/Backend/BackendElf.c:1502-1514`
- 类别：阶段不对齐（动态段标签覆盖既有表项）
- 触发条件：`has_dynamic && func_import_count > 0 && rela_plt_count > 0`（任何带函数导入的动态链接）
- 后果：`dyntab_count`（:1272）未为 JMPREL/PLTRELSZ/PLTGOT/PLTREL 预留槽位，:1503-1514 把 4 个标签写到 `null_idx-5 .. null_idx-2`，覆盖 `DT_FINI_ARRAY`/`DT_FINI_ARRAYSZ`/`DT_RELA`/`DT_RELASZ` → `.rela.dyn` 地址/大小丢失、fini_array 丢失；ELF 能加载但运行结果错误
- 最小修复：`dyntab_count` 增加 4，把 4 个 PLT 标签放进 DT_NULL 前的独立槽位
- 备注：—

#### [致命] ELF-4｜e_phnum 少算 2
- 位置：`ArkLink/src/Backend/BackendElf.c:1608-1609`
- 类别：契约破坏
- 触发条件：`has_dynamic`（任何动态链接）
- 后果：实际写入 2×PT_LOAD + PT_INTERP + PT_DYNAMIC + (PT_TLS) + (PT_GNU_RELRO) + PT_GNU_STACK，但 `e_phnum` 只算 `phdr_load_count + tls + relro + stack`，漏掉 `has_dynamic ? 2 : 0`；加载器只读前 5 个 phdr，PT_GNU_RELRO/PT_GNU_STACK 被忽略 → RELRO 失效、栈属性回退默认
- 最小修复：补 `+ (has_dynamic ? 2 : 0)`，并断言 `phdr_write_idx == e_phnum`
- 备注：—

#### [致命] ELF-5｜DT_HASH nchain ≠ dynsym 项数
- 位置：`ArkLink/src/Backend/BackendElf.c:1726-1731`
- 类别：阶段不对齐
- 触发条件：`has_dynamic && export_sym_count > 1`
- 后果：规划期 :763 `hash_nchain = 1 + import_count + export_func_count + 1`，写入期 :1726 漏掉导出函数；dynsym 共 `1 + import_count + export_sym_count` 项，nchain 少 `export_sym_count - 1`。ELF 要求 nchain == dynsym 项数，不符时动态链接器越界读 chain 或无法解析导出符号 → 运行时符号查找失败
- 最小修复：写入期沿用规划期公式，并把导出函数纳入 chain
- 备注：—

#### [致命] ELF-6｜import_count 乘法溢出 → 堆溢出
- 位置：`ArkLink/src/Backend/BackendElf.c:523`
- 类别：整数溢出 → 堆溢出
- 触发条件：`import_count > SIZE_MAX/2`，`import_count * 2 + 2` 溢出
- 后果：分配极小缓冲 → :542/:546 `seen_names[seen_count++]` 越界写
- 最小修复：calloc 前检查 `import_count <= (SIZE_MAX - 2) / 2`
- 备注：—

#### [致命] ELF-7｜elf_shnum 溢出 / 截断
- 位置：`ArkLink/src/Backend/BackendElf.c:552,1573-1574`
- 类别：整数溢出 + 截断
- 触发条件：`ns + rela_sections` 极大
- 后果：`elf_shnum` 溢出 → :689 `calloc(elf_shnum, ...)` 分配不足 → 越界；或 `> 65535` 时 `e_shnum`/`e_shstrndx` 截断 → 段头表错乱
- 最小修复：计算前检查不溢出且 `<= 65535`，否则 `ARK_LINK_ERR_FORMAT`
- 备注：—

#### [致命] ELF-8｜段 data NULL / 重定位失败静默跳过
- 位置：`ArkLink/src/Backend/BackendElf.c:676-678,684-685`
- 类别：静默失败 / 诊断缺失
- 触发条件：section `size > 0` 但 `data == NULL`；或重定位应用失败
- 后果：`if (!target->data) continue;` 静默跳过该段全部重定位；`ark_reloc_process_all` 返回 void（`BackendCommon.c:452`），内部 `default: continue`（:496）吞掉未知类型 → 生成错误镜像，无诊断
- 最小修复：`data == NULL && size > 0` 返回 `ARK_LINK_ERR_FORMAT`；`ark_reloc_process_all` 改为返回错误码
- 备注：—

#### [致命] ELF-9｜shstrtab 14 个 sb_add 只检查 3 个
- 位置：`ArkLink/src/Backend/BackendElf.c:611-615`
- 类别：静默失败
- 触发条件：shstrtab 追加失败（OOM）
- 后果：:600-609 的 `.interp/.dynsym/.dynstr/.dynamic/.hash/.init_array/.fini_array/.rela.dyn/.plt/.got.plt` 返回 `UINT32_MAX` 时未检查，`sec_name_off` 记成 0xFFFFFFFF → `sh_name` 非法 → 段名解析失败
- 最小修复：抽 `assign_name(idx, s)` 统一检查
- 备注：—

#### [致命] ELF-10｜layout 查询返回 NULL 时地址全为 0
- 位置：`ArkLink/src/Backend/BackendElf.c:410-434`
- 类别：静默失败
- 触发条件：`ark_layout_get_section` 返回 NULL
- 后果：`elf_section_vaddr/offset/vsize`、`elf_meta_vaddr/offset/size` 一律返回 0，地址计算与 memcpy 目标全错，生成损坏镜像
- 最小修复：返回错误码或至少 `assert(index < ns)`
- 备注：—

#### [高] ELF-11｜多处乘法溢出未检查
- 位置：`ArkLink/src/Backend/BackendElf.c:949,728,747,763-764,775,1125,1467,1510,1702,1710`
- 类别：未检查前置条件（乘法溢出）
- 触发条件：极大 `total_syms` / `relocs_per_sec[i]` / `import_count` / `rela_plt_count`
- 后果：`calloc(n, sizeof(T))` / `memcpy(..., n*sizeof(T))` 前无 `n > SIZE_MAX/sizeof(T)` 检查 → 堆溢出或镜像截断
- 最小修复：统一安全乘法辅助函数；入口对 `total_syms`、`import_count` 设上限
- 备注：—

#### [高] ELF-12｜st_shndx 截断
- 位置：`ArkLink/src/Backend/BackendElf.c:957,975,1022,1064-1065`
- 类别：整数截断
- 触发条件：section 数 > 65534，`st_shndx` 为 uint16_t，`(uint16_t)(1 + i)` 回绕
- 后果：符号绑定到错误段
- 最小修复：入口断言 `ns <= 65534`
- 备注：—

#### [高] ELF-13｜同一导出符号三种取值口径
- 位置：`ArkLink/src/Backend/BackendElf.c:977,1060,1243`
- 类别：语义分叉
- 触发条件：任何导出符号。`.symtab` 第一阶段用 `section_vaddr + exports[i].offset`（:977），第二阶段同名符号被覆写为 `sec_vaddr + exports[i].value`（:1060），`.dynsym` 又用 `exports[i].offset`（:1243）。`ArkExportEntry` 同时有 `offset`(uint32_t) 和 `value`(uint64_t)；`Resolver.c:554-555` 目前两者同值所以暂时一致，但 `BackendPe.c:625/627` 也是 in-range 用 `.offset`、out-of-range 用 `.value`
- 后果：任一调用方只填一个字段，三处口径就不一致 → 符号地址错误
- 最小修复：统一为单一字段，或入口断言 `offset == value`
- 备注：—

#### [高] ELF-14｜未知重定位类型静默当成 64 位绝对重定位
- 位置：`ArkLink/src/Backend/BackendElf.c:1155-1164`
- 类别：静默失败 / 语义分叉（重定位类型映射）
- 触发条件：`r->type` 不在 1..5
- 后果：`default: r_type = R_X86_64_64;` 把未知类型静默当成 64 位绝对重定位；`case 2` 与 `case 5` 都映射 `R_X86_64_32`，语义重复
- 最小修复：`default` 返回 `ARK_LINK_ERR_FORMAT` 并报告 `r->type`；合并 case 2/5
- 备注：—

#### [高] ELF-15｜非动态链接下导入重定位静默丢弃
- 位置：`ArkLink/src/Backend/BackendElf.c:1163-1168`
- 类别：静默失败
- 触发条件：`r->symbol->section_index == 0 && import_module != NULL` 且 `!has_dynamic`
- 后果：`continue` 静默丢弃重定位；非动态链接下导入符号本就无法解析，应显式拒绝
- 最小修复：`!has_dynamic` 时返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

#### [高] ELF-16｜符号索引线性扫描 + 映射提前释放
- 位置：`ArkLink/src/Backend/BackendElf.c:1137-1145`
- 类别：语义分叉 / 性能
- 触发条件：任何重定位。符号索引靠 `strcmp` 线性扫描 `.symtab` + `strstr`，而 `elf_build_symbols` 已用 `NameIndexMap` 建好映射却在 :1028 提前 `nim_free`。同一语义两处实现
- 后果：性能差、实现分叉
- 最小修复：保留 `sym_map2` 到 `elf_build_relocations`，直接查表
- 备注：—

#### [高] ELF-17｜rela_plt_count 死条件 + 静默改值
- 位置：`ArkLink/src/Backend/BackendElf.c:1500-1501,1505-1507`
- 类别：死代码 / 静默降级
- 触发条件：`rela_plt_count > UINT32_MAX / sizeof(Elf64_Rela)`。:1505 的 `rela_plt_count == 0` 被外层 `> 0` 排除，是死条件；:1507 把 `rela_plt_count` 静默改成 `func_import_count`
- 后果：DT_PLTRELSZ 错误
- 最小修复：删死条件；溢出时返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

#### [高] ELF-18｜绑定模式不可配置（死分支）
- 位置：`ArkLink/src/Backend/BackendElf.c:1516-1520`
- 类别：死代码 / 配置漂移
- 触发条件：任何动态链接。`int use_lazy_binding = 1;` 局部常量，`if (!use_lazy_binding)` 永不执行，`DT_BIND_NOW` 永不写入，`else {}` 空
- 后果：绑定模式不可配置
- 最小修复：从 `ArkBackendInput` 读标志，或删除死分支并注释固定 lazy
- 备注：—

#### [高] ELF-19｜入口段无效时 e_entry 回退镜像起点
- 位置：`ArkLink/src/Backend/BackendElf.c:1558-1562`
- 类别：静默失败
- 触发条件：`entry_section >= ns`
- 后果：`e_entry = image_base`（镜像起点），入口错误 → 崩溃，无诊断
- 最小修复：返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

#### [高] ELF-20｜ArkSectionRvaMap 字段 4GB 截断
- 位置：`ArkLink/src/Backend/BackendElf.c:2039-2042`
- 类别：整数截断
- 触发条件：镜像 > 4GB。`ArkSectionRvaMap.rva/size/file_offset` 均为 uint32_t
- 后果：截断 → 调用方拿到错误 RVA
- 最小修复：入口断言镜像 <= 4GB，或字段加宽
- 备注：—

#### [高] ELF-21｜kind_to_sh_type 忽略 kind 参数语义
- 位置：`ArkLink/src/Backend/BackendElf.c:223-225,1846`
- 类别：死参数 / 语义分叉
- 触发条件：任何非 BSS section。`kind_to_sh_type(void)` 无参数、恒返回 `SHT_PROGBITS`，与 `kind_to_canonical`/`kind_to_sh_flags` 的 kind 相关语义分叉；函数签名暗示应按 kind 区分
- 后果：section 类型语义分叉
- 最小修复：改为 `kind_to_sh_type(ArkSectionKind)` 或删除该函数直接写常量
- 备注：—

#### [高] ELF-22｜DT_ 宏重复定义
- 位置：`ArkLink/src/Backend/BackendElf.c:90-100,137-165`
- 类别：重复宏定义
- 触发条件：编译期。`DT_NULL/DT_NEEDED/DT_STRTAB/DT_SYMTAB/DT_RELA/DT_RELASZ/DT_RELAENT/DT_STRSZ/DT_SYMENT/DT_DEBUG` 定义两次
- 后果：若值不一致会静默采用后者
- 最小修复：删第一组或加 `#ifndef`
- 备注：—

#### [高] ELF-23｜sb_add 返回值未检查 / 段名截断
- 位置：`ArkLink/src/Backend/BackendElf.c:617-620,261-270`
- 类别：未检查返回值 / 截断
- 触发条件：`sb_add(&state->strtab, "")` 失败；段名超过 63/79 字节
- 后果：返回值未检查；`snprintf(buf, 64/80, ...)` 静默截断 → 不同 section 得到相同名 → 段名冲突
- 最小修复：检查返回值；截断时报错
- 备注：—

#### [中] ELF-24｜PLT/GOT 尺寸不匹配时静默改小 func_import_count
- 位置：`ArkLink/src/Backend/BackendElf.c:1766,1811`
- 类别：静默降级
- 触发条件：`elf_meta_size(plt/gotplt)` 小于预期
- 后果：`func_import_count` 被静默改小 → PLT/GOT 与 dynsym/dyntab 不一致
- 最小修复：规划期与写入期统一公式，或返回错误
- 备注：—

#### [中] ELF-25｜未知 section kind 静默跳过
- 位置：`ArkLink/src/Backend/BackendElf.c:884`
- 类别：静默失败
- 触发条件：未知 section kind
- 后果：`continue` 跳过，不生成 PT_LOAD，无诊断
- 最小修复：返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

#### [中] ELF-26｜ns==0 时 section_count 未显式赋值
- 位置：`ArkLink/src/Backend/BackendElf.c:2050-2052`
- 类别：契约破坏
- 触发条件：`ns == 0` 时 `section_maps` 不分配、`section_count` 保持 0，但 `output->data` 仍发布
- 后果：输出契约不一致
- 最小修复：显式 `output->section_count = state->ns`
- 备注：—

#### [疑似] ELF-疑似-1｜段合并条件可能反向扩展（需验证）
- 位置：`ArkLink/src/Backend/BackendElf.c:893-905`
- 类别：语义分叉（疑似）
- 触发条件：段合并条件 `(c->p_offset + c->p_filesz) >= off || fend <= (c->p_offset + c->p_filesz)`；若 section 的 `file_offset` 非单调，会把前一段向后扩展
- 后果：段范围错误（需验证 `ark_layout_create` 是否保证按 `file_offset` 排序输出）
- 最小修复：验证布局排序保证
- 备注：原文未编号的「疑似」项

#### [疑似] ELF-疑似-2｜DT_HASH 单桶 chain（需验证）
- 位置：`ArkLink/src/Backend/BackendElf.c:1726-1731`
- 类别：语义分叉 / 性能（疑似）
- 触发条件：hash chain 值 `(i+1) % hash_nchain`；chain[nchain-1]=0 作为终止符合法，但单桶 O(n) 查找
- 后果：需确认目标动态链接器是否接受单桶 DT_HASH
- 最小修复：验证加载器兼容性
- 备注：原文未编号的「疑似」项

#### [高] HDR-1｜符号值 uint32_t 静默截断
- 位置：`ArkLink/include/ArkLink/Resolver.h:28`、`Loader.h:81`
- 类别：语义分叉 / 契约破坏
- 触发条件：`ArkResolverSymbol.value` 是 uint32_t，而 `ArkSymbolDesc.value` 是 uint64_t；`Resolver.c:279` 的 `sym.value = (uint32_t)sym_desc->value;` 静默截断，无诊断、无范围断言（任何 64 位符号值 ≥ 2^32）
- 后果：链接期符号值错误，PE/ELF 重定位写入错误地址，程序崩溃或静默数据损坏
- 最小修复：`ArkResolverSymbol.value` 改 uint64_t；若必须保留 32 位，则截断前显式检查 `> UINT32_MAX` 并返回 `ARK_RES_ERROR_*`
- 备注：—

#### [高] HDR-2｜section 指针与 section_index 双字段无不变量
- 位置：`ArkLink/include/ArkLink/Resolver.h:26-31`
- 类别：契约破坏（冗余双字段）
- 触发条件：任何手动构造或修改符号的路径。`ArkResolverSymbol` 同时有 `section`（指针）和 `section_index`（uint32_t），两者无不变量约束、无断言一致
- 后果：某路径只更新其一时产生悬垂/错引 → 节定位不一致 → 重定位写错节
- 最小修复：只保留 `section` 指针（`section_index` 反查或删除），或加 `ARK_ASSERT(section == NULL || section_index == section->index)`
- 备注：—

#### [高] HDR-3｜offset+width 32 位回绕绕过边界检查
- 位置：`ArkLink/include/ArkLink/Backend.h:105`
- 类别：未检查前置条件
- 触发条件：`ArkRelocProcessor.offset` 为 uint32_t，而 `BackendCommon.c:386/393/400` 计算 `offset+2/4/8` 做边界检查，32 位回绕可绕过（`offset=0xFFFFFFFE` 时 `offset+2==0`）
- 后果：越界写入重定位目标缓冲区
- 最小修复：用 uint64_t 计算 `offset + width`，或先检查 `offset > UINT32_MAX - width` 再拒绝
- 备注：—

#### [高] HDR-4｜ark_reloc_process_all 返回 void 吞掉未知类型
- 位置：`ArkLink/src/Backend/BackendCommon.c:452`
- 类别：静默失败 / 诊断缺失
- 触发条件：遇到 `ARK_RELOC_*` 未实现的动作。返回 void，内部 `default: continue`（:496）吞掉未知重定位类型，不报类型值、不报偏移、不返回错误码
- 后果：重定位静默丢失 → 生成可执行文件行为错误，无任何线索
- 最小修复：返回 `ArkLinkResult`，未知类型返回 `ARK_LINK_ERROR_UNSUPPORTED_RELOC` 并附 type/offset
- 备注：—

#### [高] HDR-5｜ark_reloc_apply_elf default 静默忽略
- 位置：`ArkLink/src/Backend/BackendCommon.c:385`
- 类别：静默失败
- 触发条件：未知 action
- 后果：`default: return;` 静默忽略，不写值、不报错
- 最小修复：同 HDR-4
- 备注：—

#### [高] HDR-6｜符号值解析链阶段耦合
- 位置：`ArkLink/src/Backend/BackendCommon.c:346-348`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：重定位符号值取值链 `symbol_address_resolved ? symbol_address : (symbol_rva ? symbol->value : 0)`；`symbol_address`（64 位）与 `symbol_rva`（32 位）分属不同阶段（链接布局 vs 对象解析），优先级隐式耦合在 `elf_plan_layout` 是否设置 `symbol_address_resolved` 上（任何路径未设 `symbol_address_resolved` 但设了 `symbol_rva`）
- 后果：ELF 与 PE 后端可能取到不同符号值口径
- 最小修复：把符号值解析集中到唯一函数（如 `ark_reloc_resolve_symbol_value`），两端共用
- 备注：—

#### [中] HDR-7｜PE 后端忽略 input 配置字段
- 位置：`ArkLink/include/ArkLink/Backend.h:170-172`、`ArkLink/src/Backend/BackendPe.c:925/928-931`
- 类别：配置漂移
- 触发条件：`ArkBackendInput` 声明 `stack_size`/`subsystem`/`output_type`，但 PE 后端硬编码值，配置字段被无视
- 后果：用户配置无效，行为不可预测
- 最小修复：PE 后端读取这些字段，缺失时用默认并报 warning
- 备注：与 BackendPe-28 同源

#### [中] HDR-8｜internal 别名判定基于 size 而非 capacity
- 位置：`ArkLink/src/Backend/BackendCommon.c:112-126`
- 类别：语义分叉
- 触发条件：`internal` 别名判定基于 `size`，但别名冲突实际取决于 `capacity`（写入可能越界到 capacity）
- 后果：判为 internal 的段可能重叠
- 最小修复：判定覆盖 `size..capacity`
- 备注：—

#### [致命] B5-1｜build_project_independent 完全忽略 output_path
- 位置：`Re.KrtC/src/Main.c:909-914`
- 类别：配置漂移 / 静默失败
- 触发条件：`krt build project.krt -o /path/to/foo`（或 `build_project` 传入任何 `-o`）；参数被 `__attribute__((unused))` 显式丢弃，函数体内从未引用
- 后果：用户指定的输出路径被静默忽略，产物固定落到 `<root>/bin/<platform>/<output_name>`；`-o` 形同虚设，且无任何警告
- 最小修复：把 `output_path` 传给 `ParallelCompilerLinkResults`；为空时再走默认 `bin/<platform>/<name>` 逻辑
- 备注：—

#### [致命] B5-2｜目标文件缺失只报错不中止
- 位置：`Re.KrtC/src/Main.c:856-860`
- 类别：静默失败 / 契约破坏
- 触发条件：`obj_idx != success_count`（某 .kro 未生成或 `KrtPlatformPathExists` 失败）
- 后果：只 `KrtError(...)` 后不 return，继续执行 `link_result = obj_idx == success_count ? 0 : -1;`，把「文件缺失」降级成「链接失败」，最终只报 `Linking failed`（:900）；原始「哪个文件缺失」信息被覆盖
- 最小修复：`obj_idx != success_count` 时立即释放资源并 `return 1`
- 备注：—

#### [致命] B5-3｜WriteByte 的 KRT_REALLOC 失败未检查
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:120-121`
- 类别：未检查前置条件 / 静默失败
- 触发条件：`realloc` 返回 NULL（OOM）
- 后果：`chunk->code`/`chunk->lines` 被赋 NULL，旧指针丢失（内存泄漏），随后 `chunk->code[chunk->count] = byte` 解引用 NULL → 崩溃。`AddConstant`（:138）同
- 最小修复：`void* p = realloc(...); if (!p) { /* 报告并终止/返回错误 */ } chunk->code = p;`
- 备注：G1 前提修正后结论不变（此处 `KRT_REALLOC` 为 `realloc` 宏，失败返回 NULL）

#### [致命] B5-4｜chunk->count+1 / capacity*2 整数溢出
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:124-126`
- 类别：未检查前置条件
- 触发条件：`count` 接近 `INT_MAX`
- 后果：`capacity < count+1` 为假 → 越界写入
- 最小修复：改用 `size_t` 或检查 `count > INT_MAX - 1`
- 备注：—

#### [高] B5-5｜get_kro_filename 盲目替换末 4 字符且不校验扩展名
- 位置：`Re.KrtC/src/Main.c:551-558`
- 类别：未检查前置条件 / 静默失败
- 触发条件：传入 `foo`（len=3）→ 不替换，输出仍 `foo`；传入 `a.bcd` → 变 `a.kro`；传入长度接近 `kro_file_size` 的路径 → `strcpy(kro_file+len-4, ".kro")` 写 5 字节可能越界
- 后果：路径错乱或缓冲区溢出；`link_kro_executable`（:269）用它生成 import 的 .kro 名，错误名会导致链接缺文件
- 最小修复：校验 `len >= 4 && strcmp(krt_file+len-4, ".krt")==0`，否则报错；并保证 `kro_file_size >= len+1`
- 备注：—

#### [高] B5-6｜import_kro_capacity 翻倍无溢出检查
- 位置：`Re.KrtC/src/Main.c:279`
- 类别：未检查前置条件
- 触发条件：import 数量使容量翻倍超过 `INT_MAX`
- 后果：容量变负 → `realloc` 尺寸异常/失败，`imports_ok=0`，依赖被静默丢弃，链接出残缺可执行文件
- 最小修复：翻倍前检查 `import_kro_capacity > INT_MAX/2`
- 备注：—

#### [高] B5-7｜链接失败只报「Linking failed」
- 位置：`Re.KrtC/src/Main.c:300-306,900`
- 类别：诊断缺失
- 触发条件：任何链接失败
- 后果：丢弃 `KrtArkLinkLinkObjects` 返回的具体错误码/原因，用户无法定位
- 最小修复：打印返回码及 ArkLink 上下文中记录的错误串
- 备注：—

#### [高] B5-8｜错误信息与实际不符
- 位置：`Re.KrtC/src/Main.c:1050-1055`
- 类别：诊断缺失（诊断与实际错误不符）
- 触发条件：`compile_result != 0` 但 `failed_files == 0`
- 后果：报「0 个文件编译失败」，与实际（并行编译整体失败）矛盾
- 最小修复：区分 `compile_result` 与 `failed_files`，分别报告
- 备注：—

#### [高] B5-9｜中间路径缓冲区静默截断
- 位置：`Re.KrtC/src/Main.c:990-995`
- 类别：静默失败
- 触发条件：`obj_dir` 或 `file_name_no_ext` 长度 ≥ 500
- 后果：中间 .kro 路径被截断，写到错误位置，且无诊断
- 最小修复：截断后检查 `strlen(src) >= sizeof(dst)` 并报错
- 备注：—

#### [高] B5-10｜两处 snprintf 未检查返回值
- 位置：`Re.KrtC/src/Main.c:1135-1140`
- 类别：未检查前置条件
- 触发条件：`object_dir` 过长
- 后果：`aggregate_path`/`aggregate_kro` 被截断，后续 `fopen` 失败仅报「无法创建合并项目源码」，不报路径被截断
- 最小修复：检查 `snprintf` 返回值 ≥ sizeof 时报错
- 备注：—

#### [高] B5-11｜WriteShort 写死行号 0
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:130-131`
- 类别：注释与实现脱节 / 语义分叉
- 触发条件：所有 short 常量写入
- 后果：字节码行号信息丢失，调试/栈回溯定位到错误行
- 最小修复：`WriteShort` 接收 `line` 参数并透传
- 备注：—

#### [高] B5-12｜SerializeToFile 写失败仍返回 true
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:159-203`
- 类别：静默失败
- 触发条件：磁盘满、`fwrite` 短写、`fclose` 失败
- 后果：函数返回 `true`，调用方认为序列化成功；文件内容残缺
- 最小修复：检查每个 `fwrite` 返回值与 `fclose`，失败返回 `false` 并报错
- 备注：—

#### [高] B5-13｜字符串常量长度截断为 uint16_t
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:191`
- 类别：未检查前置条件
- 触发条件：字符串常量长度 > 65535
- 后果：长度字段被截断，反序列化读越界/错乱
- 最小修复：检查 `strlen > UINT16_MAX` 并报错
- 备注：—

#### [高] B5-14｜VAL_OBJ 分支不写任何数据
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:196-198`
- 类别：契约破坏 / 静默失败
- 触发条件：常量池含 `VAL_OBJ`
- 后果：文件只写 `type` 字节，反序列化时按类型读后续字节会错位；且无诊断
- 最小修复：序列化时对 `VAL_OBJ` 报错或实现其编码
- 备注：—

#### [高] B5-15｜KrtBytecodeGeneratorDestroy 不检查 NULL
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:102-104`
- 类别：未检查前置条件
- 触发条件：传入 NULL
- 后果：`free(NULL)` 本身安全，但与 `KrtBuildContextDestroy`（:21 有 NULL 检查）风格不一致；若将来改为非 free 释放会崩
- 最小修复：统一 `if (!generator) return;`
- 备注：—

#### [高] B5-16｜KrtBuildContextCreate 的 NULL 检查为活路径而非死代码（G1 修订）
- 位置：`Re.KrtC/src/Build/BuildSystem.c:7-10`
- 类别：静默失败 / 未检查前置条件（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（KrtSafeMalloc 失败返回 NULL，不 exit）
- 后果：`if (!ctx) return NULL;` 是活路径，静默向上传播 NULL；`KrtMemoryReportError` 为空操作 → 零诊断。调用方 `Main.c:473` 按「可能返回 NULL」处理，与旧「失败即终止」假设形成双重标准
- 最小修复：保留 NULL 检查并补充 OOM 诊断；或统一分配器语义与文档
- 备注：【修订 G1】原文判为「死代码」基于「KRT_MALLOC 失败即 exit(1)」的错误前提；实际 `Core/Memory/Allocator.c:159-211` 失败返回 NULL、`KrtCommon.h:77-81` 宏无守卫。严重度按后果（静默失败 / 零诊断）重定为高

#### [高] B5-17｜链接失败丢弃 ArkLink 错误码
- 位置：`Re.KrtC/src/Build/BuildSystem.c:62-67`
- 类别：诊断缺失
- 触发条件：`KrtArkLinkLink` 返回非 0
- 后果：只写 `"ArkLink link failed"`，具体原因丢失
- 最小修复：把 ArkLink 结果码/错误串写入 `ctx->error_message`
- 备注：—

#### [中] B5-18｜未知项目类型静默返回 "console"
- 位置：`Re.KrtC/src/Main.c:48-60`
- 类别：静默失败
- 触发条件：`new <未知类型> <名>` 且未提供项目名
- 后果：用户笔误类型被吞掉，项目名变成 "console"
- 最小修复：未知类型报错退出
- 备注：—

#### [中] B5-19｜get_kro_filename 前向声明处未注释契约
- 位置：`Re.KrtC/src/Main.c:246`（与 :551 之间）
- 类别：契约破坏（轻微）
- 触发条件：调用方不知 `kro_file_size` 下界要求
- 后果：易误用
- 最小修复：声明处补 `@param kro_file_size` 下界要求
- 备注：—

#### [中] B5-20｜模板创建失败时部分产物未清理
- 位置：`Re.KrtC/src/Main.c:1376`
- 类别：资源与生命周期
- 触发条件：`KrtProjCreateTemplate` 中途失败
- 后果：残留空目录/文件，无回滚
- 最小修复：失败时删除已创建项
- 备注：—

#### [中] B5-21｜KRT_REALLOC 宏无括号保护
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:88`
- 类别：未检查前置条件
- 触发条件：传入带副作用或复杂表达式
- 后果：宏展开多次求值
- 最小修复：加括号 `(ptr)`/`(size)`，或改 `static inline`
- 备注：—

#### [中] B5-22｜容量检查逻辑重复实现
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:117`
- 类别：语义分叉
- 触发条件：`chunk->capacity < chunk->count + 1` 与 `AddConstant` 的 `capacity < count + 1` 重复实现
- 后果：逻辑分叉风险
- 最小修复：抽成唯一 `chunk_reserve` 函数
- 备注：—

#### [致命] B6-1｜BSS 重定位从不写入文件且 total_reloc_count 漏算 bss
- 位置：`Re.KrtC/src/Shared/KroWriter.c:537-557`
- 类别：静默失败 / 契约破坏
- 触发条件：任何写入 `KRO_SEC_BSS` 重定位的代码路径（`kro_add_reloc` 的 `case KRO_SEC_BSS`）
- 后果：`writer->bss_relocs` 被填充但 `kro_write_file` 只序列化 text/rodata/data 三组；`header.total_reloc_count`（:485）也不含 bss。BSS 符号地址在链接期全部丢失 → 链接产物行为错误或崩溃
- 最小修复：加 `header.bss_reloc_count` 并写入 `writer->bss_relocs`；`total_reloc_count` 计入 bss
- 备注：—

#### [致命] B6-2｜写文件失败路径静默且 fclose 未检查
- 位置：`Re.KrtC/src/Shared/KroWriter.c:476-567`
- 类别：静默失败
- 触发条件：磁盘满/IO 错误。`fwrite` 失败时 `fclose` 可能仍缓冲未刷数据；:566 成功路径的 `fclose` 返回值被忽略
- 后果：调用方（`KrtCompilePipelineExecute`）拿到 `true` 但文件残缺，后续链接基于坏 .kro
- 最小修复：检查 `fclose` 返回值；失败时删除半成品文件
- 备注：—

#### [致命] B6-3｜kro_add_reloc 丢弃 add_reloc_to_list 的失败
- 位置：`Re.KrtC/src/Shared/KroWriter.c:346`
- 类别：静默失败 / 错误处理不一致
- 触发条件：`realloc` 失败（OOM）
- 后果：重定位被静默丢弃，函数返回 void，无任何诊断
- 最小修复：`kro_add_reloc` 改返回 `bool`，调用方检查并中止
- 备注：—

#### [致命] B6-4｜容量翻倍无溢出检查
- 位置：`Re.KrtC/src/Shared/KroWriter.c:236,289,361,154`
- 类别：未检查前置条件
- 触发条件：`add_string`（:236 `while (new_capacity < new_size) new_capacity *= 2;`）、`kro_add_symbol`/`kro_add_import_symbol`/`add_reloc_to_list`（`* 2`）、`ensure_capacity`
- 后果：`uint32_t` 回绕 → `realloc` 小缓冲 → `memcpy` 堆溢出；`add_string` 的 while 可能死循环
- 最小修复：每次翻倍前检查 `> UINT32_MAX/2`，改为 `new_capacity = required`
- 备注：—

#### [致命] B6-5｜count * element_size 乘法溢出
- 位置：`Re.KrtC/src/Shared/SmartPtr.c:59,97`
- 类别：未检查前置条件
- 触发条件：`KrtMakeSharedArray(type, count, elem)` 且 `count*elem` 溢出 `size_t`
- 后果：分配极小缓冲，数组写入越界 → 堆损坏
- 最小修复：`if (count != 0 && element_size > SIZE_MAX / count) return NULL;`
- 备注：—

#### [致命] B6-6｜KrtArkLinkAddObjectFile 返回值被忽略
- 位置：`Re.KrtC/src/Shared/ArkLinkIntegration.c:286`
- 类别：静默失败 / 契约破坏
- 触发条件：`KrtArkLinkLinkObjects` 中添加任一 obj 失败（session_add_input 失败、strdup 失败、realloc 失败）
- 后果：链接继续进行，缺文件 → 链接失败或生成缺符号的可执行文件；无诊断
- 最小修复：检查返回值，失败立即销毁 ctx 并返回 -1
- 备注：—

#### [致命] B6-7｜add_input 失败时已 strdup 的槽位泄漏
- 位置：`Re.KrtC/src/Shared/ArkLinkIntegration.c:123-127`
- 类别：资源与生命周期
- 触发条件：`arklink_session_add_input` 返回非 OK
- 后果：`ctx->obj_files[obj_count]` 已分配但 `obj_count` 未增；`KrtArkLinkContextDestroy` 只释放 `i < obj_count` → 泄漏
- 最小修复：失败时 `KRT_FREE(ctx->obj_files[obj_count])` 并置 NULL
- 备注：—

## 第二部分　语义分析 / 符号表 / 解析（第七至二十九批）

### 第六批（续）· 高

#### [高] 6-8｜`ark_link_logger_callback` 吞掉所有 ArkLink 日志
- 位置：`ArkLinkIntegration.c:24-28`
- 类别：诊断缺失
- 触发：ArkLink 内部产生任何 warning/error/info
- 后果：`level`/`message` 全部 `(void)` 丢弃，链接期问题完全不可见
- 修复：按 level 转发到 `KrtError`/`KrtPrintf`

#### [高] 6-9｜`output_kind`/`stack_size` 硬编码，忽略配置
- 位置：`ArkLinkIntegration.c:62/79`
- 类别：配置漂移
- 触发：`config->target_type` 为库/静态库，或用户设置 stack size
- 后果：`KRT_TARGET_LIBRARY` 也生成 EXECUTABLE；`ArkBackendInput.stack_size` 被无视
- 修复：从 config 读取并下发

#### [高] 6-10｜`KrtArkLinkAddRuntimeObjects` 返回值被忽略
- 位置：`ArkLinkIntegration.c:275`
- 类别：静默失败
- 触发：runtime 对象添加失败
- 后果：链接缺 runtime → 缺符号，错误被推迟到链接期且信息不足
- 修复：检查返回值

#### [高] 6-11｜`chmod` 返回值未检查
- 位置：`ArkLinkIntegration.c:299`
- 类别：静默失败
- 触发：chmod 失败（如文件系统不支持）
- 后果：产物无执行权限，用户运行时报「Permission denied」，无任何提示
- 修复：失败时 `KrtError` 警告

#### [高] 6-12｜写函数用返回值 0 表示失败，但 0 是合法偏移
- 位置：`KroWriter.c:164/178/192/213/226`
- 类别：契约破坏
- 触发：`ensure_capacity` 失败或 offset 溢出
- 后果：调用方无法区分「失败」与「偏移 0」，静默生成错误代码
- 修复：返回 `int64_t`（-1 表失败）或用 out-param

#### [高] 6-13｜`uint64_t value/offset` 截断为 `uint32_t`
- 位置：`KroWriter.c:316/320/378/404`
- 类别：未检查前置条件 / 静默失败
- 触发：符号值或重定位偏移 > 4GiB
- 后果：符号地址/重定位位置被静默截断 → 链接错误
- 修复：截断前检查 `> UINT32_MAX` 并报错

#### [高] 6-14｜`add_string` 失败返回 0，被 `kro_add_symbol` 当合法 `name_offset`
- 位置：`KroWriter.c:296`
- 类别：契约破坏 / 静默失败
- 触发：字符串表 OOM
- 后果：符号名变成空串（offset 0 是保留空串），`kro_find_symbol` 可匹配到空名符号
- 修复：`add_string` 失败时 `kro_add_symbol` 返回 -1

#### [高] 6-15｜`kro_set_code_offset` 静默忽略越界 offset
- 位置：`KroWriter.c:470-474`
- 类别：契约破坏
- 触发：offset > text_high_water
- 后果：调用方以为已回退，实际未生效，后续写入位置错误
- 修复：返回 bool

#### [高] 6-16｜`KrtErrorReportAddEx` 的 `realloc` 失败静默 return
- 位置：`CompilerError.c:145-148`
- 类别：静默失败
- 触发：OOM
- 后果：错误被丢弃，`error_count` 不增，编译可能继续并产出错误结果；用户看不到本应报告的错误
- 修复：设置 `report->oom = true` 并在打印时提示；或终止

#### [高] 6-17｜`error_capacity * 2` 溢出
- 位置：`CompilerError.c:146`
- 类别：未检查前置条件
- 修复：检查 `> INT_MAX/2`

#### [高] 6-18｜`strdup` 失败未检查
- 位置：`CompilerError.c:98`
- 类别：静默失败
- 触发：`KrtErrorReportSetSourceCode` 且 OOM
- 后果：`source_code` 为 NULL，打印时退化为无源码片段，无提示
- 修复：返回 bool 或记录失败

#### [高] 6-19｜`error_code` 字段被收集但打印时从不输出
- 位置：`CompilerError.c:239-306`
- 类别：诊断缺失 / 死代码
- 触发：任何带 `error_code` 的错误
- 后果：错误码（如 `E0001`）被丢弃，用户无法按码检索文档
- 修复：打印 `error->error_code`

#### [高] 6-20｜使用 `malloc` 而非 `KRT_MALLOC`
- 位置：`CompilerError.c:54/59`
- 类别：错误处理不一致
- 触发：OOM
- 后果：与 `KRT_MALLOC`/`KrtSafeFree` 体系混用风险（裸 malloc 指针交给安全分配器会静默 no-op 泄漏/非法释放）
- 修复：统一分配器
- 备注：【修订 G1】原文「`KRT_MALLOC` 失败即 exit」前提已废；两分配器都不保证终止，混用与 NULL 传播问题更突出

#### [高] 6-21｜`KrtSharedRelease` 过度释放不检测
- 位置：`SmartPtr.c:113-134`
- 类别：未检查前置条件 / 共享可变状态
- 触发：对同一 `SmartPtr` 重复 `KrtSharedRelease`，或 `ref_count` 已为 0
- 后果：`ref_count` 变负，`old_count == 1` 不再触发，数据已释放但计数错乱；`KrtPtrIsValid` 用 `> 0` 判断，负数视为无效但控制块可能已释放 → 悬垂
- 修复：`old_count <= 0` 时报错并不减

#### [高] 6-22｜`KrtSharedAcquire` 可复活已过期对象
- 位置：`SmartPtr.c:100-106`
- 类别：共享可变状态 / 未检查前置条件
- 触发：`ref_count == 0` 时 acquire
- 后果：`data` 可能已释放（weak_count==0 时 `KRT_FREE(control)`），acquire 后 `ptr->control` 悬垂
- 修复：`ref_count == 0` 时拒绝 acquire

#### [高] 6-23｜`total_allocations - total_deallocations` 无符号下溢
- 位置：`SmartPtr.c:306`
- 类别：未检查前置条件
- 触发：过度释放使 deallocations > allocations
- 后果：打印巨大「Potential Leaks」数，误导
- 修复：用有符号差值并饱和

#### [高] 6-24｜`GetModuleFileNameA` 未检查 `len == MAX_PATH`（截断无 NUL）
- 位置：`Path.c:121`
- 类别：未检查前置条件
- 触发：Windows 可执行路径长度 ≥ MAX_PATH
- 后果：`path` 可能未 NUL 终止，后续 `strlen(path)`（:137）越界读
- 修复：`if (len == 0 || len >= MAX_PATH) return -1;` 或显式补 NUL

#### [高] 6-25｜`KrtEnsureDirectoryRecursive` Windows 分支对 `C:` 前缀相对路径处理错误
- 位置：`Path.c:279-330`
- 类别：语义分叉 / 未检查前置条件
- 触发：`path = "C:foo"`（盘符相对路径，len=5）
- 后果：`temp[1]==':'` 进入分支，`p=temp+2`，循环把 `C:` 当目录检查/创建，语义错误
- 修复：仅当 `temp[2]` 是 `/` 或 `\` 时才视为盘符绝对路径

#### [高] 6-26｜`KrtPathExists` 不检查 NULL
- 位置：`Path.c:36-39`
- 类别：未检查前置条件
- 触发：传入 NULL
- 后果：`stat(NULL,&st)` 返回 -1（errno EFAULT），函数返回 0，非法输入被静默吞掉
- 修复：`if (!path) return 0;`

#### [高] 6-27｜`last_sep = (slash > backslash) ? slash : backslash;` 两者均 NULL 时比较无效
- 位置：`Path.c:185`
- 类别：未检查前置条件（脆弱）
- 触发：路径不含 `/` 或 `\`
- 后果：实际结果恰好正确，但依赖实现定义行为，属脆弱写法
- 修复：显式 `last_sep = slash ? slash : backslash;`

#### [高] 6-28｜`win32_is_absolute_path` 把 `C:foo` 当绝对路径
- 位置：`PlatformAbstraction.c:27`
- 类别：语义分叉
- 触发：`path = "C:foo"`
- 后果：返回 1（绝对），实际是相对当前目录的盘符相对路径
- 修复：要求 `path[2] == '/' || path[2] == '\\'`

#### [高] 6-29｜`KrtPlatformPathJoin` 不检查截断，且 `part` 为绝对路径时结果错误
- 位置：`PlatformAbstraction.c:187-212`
- 类别：静默失败 / 未检查前置条件
- 触发：结果长度 ≥ buffer_size；或 `part` 以 `/` 开头
- 后果：静默截断；`base//abs` 拼接出无效路径
- 修复：检查 `snprintf` 返回值；`part` 绝对时直接返回 part

#### [高] 6-30｜`KrtPlatformCreate` 的 NULL 检查
- 位置：`PlatformAbstraction.c:124-127`
- 类别：静默失败 / 契约破坏
- 触发：OOM（`KRT_MALLOC` 失败）
- 后果：`if (!platform) return NULL` **并非死代码**——分配器失败返回 NULL 而不终止；调用方若按「必成功」处理则 NULL 传播崩溃
- 修复：统一分配器语义（要么全承认可失败并检查，要么提供 OR_DIE 宏）
- 备注：【修订 G1】原文「死代码」结论作废

#### [高] 6-31｜非 Windows 编译时 `win32_*` 三个函数为死代码
- 位置：`PlatformAbstraction.c:53-76`
- 类别：死代码
- 后果：维护负担；`win32_supports_color` 在 Linux 恒返回 0
- 修复：用 `#ifdef _WIN32` 包裹整组函数

### 第六批（续）· 中

#### [中] 6-32｜`header.entry_point` 截断
- 位置：`KroWriter.c:491`（同 6-13，入口 < 4GiB 无实际影响，降级）

#### [中] 6-33｜`fclose` 未检查
- 位置：`KroWriter.c:566`（同 6-12）

#### [中] 6-34｜`warning_count` 与 `error_count` 统计口径不一致
- 位置：`CompilerError.c:252-254`
- 后果：一个累计、一个实时重算；若 `AddEx` 被外部修改 `warning_count` 会不一致
- 修复：统一循环统计

#### [中] 6-35｜`KrtTimeNowSeconds` 跨平台语义分叉
- 位置：`Path.c:20-33`
- 触发：系统时间调整
- 后果：Linux 用 `CLOCK_MONOTONIC`，Windows 用 `GetSystemTimeAsFileTime`（墙钟）→ Windows 耗时跳变/为负
- 修复：Windows 也用 `QueryPerformanceCounter`

#### [中] 6-36｜`KrtEnsureDirectoryRecursive` 的 `temp[1024]` 固定尺寸
- 位置：`Path.c:271-273`
- 触发：`KRT_MAX_PATH` 被重定义为 > 1024
- 后果：`len >= sizeof(temp)` 漏检 → 溢出
- 修复：用 `sizeof(temp)` 常量并加 `static_assert`

#### [中] 6-37｜`KrtEnsureDirectoryRecursive(obj_dir)` 返回值被忽略
- 位置：`Main.c:986`（配合 6-25 的静默失败）
- 修复：检查并中止

#### [中] 6-38｜runtime 目录定位假设 exe 的父目录
- 位置：`ArkLinkIntegration.c:268-277`
- 触发：exe 在 `/usr/bin` 等目录
- 后果：`strrchr(runtime_dir, sep)` 取父目录 → runtime_dir 变 `/usr`，几乎必然找不到 runtime 对象
- 备注：疑似，需验证运行时部署约定

---

### 第七批 · `SemanticAnalyzer.c:1-440`

#### [致命] 7-1｜类上下文入栈失败被静默丢弃，后续解析沿用错误上下文
- 位置：`SemanticAnalyzer.c:421-440`
- 类别：资源与生命周期 / 静默失败
- 触发：`KRT_REALLOC` 失败
- 后果：`semantic_analyzer_push_class_context` 为 void 直接 return；类体内 `self`/字段/方法解析继续使用上一个类或全局上下文，产生错误符号绑定且无诊断
- 修复：改为返回 `bool` 并让调用方中止；同时检查 `class_stack_capacity * 2` 的 int 溢出

#### [致命] 7-2｜imported_file 注册：未注册却报告成功
- 位置：`SemanticAnalyzer.c:170-177`
- 类别：静默失败 / 诊断缺失
- 触发：`pipeline == NULL`（未调用 `set_pipeline`）但 analyzer/file_path 有效
- 后果：返回 true（未注册任何文件却成功）；而 `analyzer==NULL && file_path!=NULL` 返回 false，同一函数对「未注册」给出相反语义
- 修复：统一返回 false 并通过 `error_report` 记录原因

#### [高] 7-3｜`KrtArenaDestroy(analyzer->type_arena)` 未判空
- 位置：`SemanticAnalyzer.c:85-162`（`create` 失败路径 `:60-63,75-84`）
- 类别：未检查前置条件 / 契约破坏
- 触发：`type_arena` 分配失败
- 后果：依赖 `KrtArenaDestroy` 是否判空
- 修复：显式判空并定义契约
- 备注：后续核查发现 `Arena.c:34-36` 已显式判空，此条**降级为无问题**

#### [高] 7-4｜`snprintf(project_root,...)` 未检查返回值
- 位置：`SemanticAnalyzer.c:294-378`
- 类别：静默失败
- 触发：路径长度 ≥ `PATH_MAX`
- 后果：静默截断，`project.krt` 探测与 `libs_path` 拼接基于错误路径，`libs_path` 保持 NULL 而无诊断
- 修复：检查返回值，截断即报错拒绝

#### [高] 7-5｜`/libs/` 定位取最后一个匹配并就地截断
- 位置：`SemanticAnalyzer.c:326-355`
- 类别：语义分叉
- 触发：`a/libs/x/libs/y` 这类路径
- 后果：取错 project root；与 `import_krt_file`/`import_namespace_folder` 的路径构造各自独立实现
- 修复：抽出唯一 `project_root_from_source_path()` 供各阶段共用

#### [高] 7-6｜`realpath()` 失败静默降级 + 错误的绝对路径判定
- 位置：`SemanticAnalyzer.c:380-420`
- 类别：静默失败 / 阶段不对齐
- 触发：`realpath` 失败（文件不存在/权限不足）；或 `file_path[0]=='\\'`（Linux 下非绝对路径）
- 后果：降级为 `cwd + "/" + file_path`，整个导入解析阶段随之错位
- 修复：`realpath` 失败必须报错；绝对路径判定统一走平台抽象层

#### [高] 7-7｜跨文件同名导出符号先收集者胜
- 位置：`SemanticAnalyzer.c:191-224`
- 类别：静默失败 / 语义分叉
- 触发：两个文件导出同名函数（签名不同）
- 后果：不比较签名、不报冲突，后者被静默丢弃，跨文件引用可能绑定到错误签名
- 修复：命中已存在 `SYMBOL_FUNCTION` 时比较 mangled 签名，不一致报诊断

#### [高] 7-8｜命名空间收集无界递归 + 失败状态不上报
- 位置：`SemanticAnalyzer.c:238-292`
- 类别：未检查前置条件 / 静默失败
- 触发：深嵌套命名空间（栈溢出）
- 后果：`project_namespace_collection_failed` 置位后在本函数内不再上报
- 修复：加深度上限；确认调用方检查
- 备注：后续核查 `SemanticAnalyzer.c:3334` 有检查并转 `result->success=false`，**有检查**（递归深度问题仍需处理）

#### [中] 7-9｜`KRT_CALLOC` 失败后返回 NULL 的语义未统一
- 位置：`SemanticAnalyzer.c:41-84`
- 类别：错误处理不一致 / 契约破坏
- 触发：OOM
- 后果：`if (!analyzer)` 分支并非死代码（分配器失败返回 NULL）；但 `symbol_table_create`/`generics_create_registry`/`KrtArenaCreateLocal` 的失败语义未统一
- 修复：统一「分配失败返回 NULL + 上层检查」或「OR_DIE」约定
- 备注：【修订 G1】原文「死代码」结论作废

#### [中] 7-10｜容量增长两种标准（溢出检查不一致）
- 位置：`SemanticAnalyzer.c:421-440` 与 `:238-292`
- 类别：未检查前置条件 / 错误处理不一致
- 后果：`push_class_context` 用 `capacity * 2` 无溢出检查，`collect_project_namespaces` 做了 `INT_MAX/2`/`SIZE_MAX` 检查
- 修复：统一溢出检查

#### [疑似] 7-S1｜`project_namespace_collection_failed` 是否有调用方检查
- 位置：`SemanticAnalyzer.c:282-292`
- 需验证：grep 全部引用；若无 → 升级致命
- 备注：后续已核查有检查（`:3334`）

#### [疑似] 7-S2｜`KrtArenaDestroy(NULL)` 是否安全
- 位置：`SemanticAnalyzer.c:85`
- 需验证：查 `Arena.c`
- 备注：已核查 `Arena.c:34-36` 判空返回，安全

---

### 第八批 · `SemanticAnalyzer.c:440-700`

#### [高] 8-1｜符号查找两套实现（当前表 vs 作用域链）
- 位置：`SemanticAnalyzer.c:572-598`（lookup_with_usings）vs `:660`（array_element_type）
- 类别：语义分叉 / 阶段不对齐
- 触发：块内 shadowing
- 后果：`lookup_with_usings` 只用当前表，`semantic_array_element_type` 用 `lookup_scope_chain`，结果不同
- 修复：统一为 `symbol_table_lookup_scope_chain`

#### [高] 8-2｜多个 using 同名符号按声明顺序取第一个
- 位置：`SemanticAnalyzer.c:572-598`
- 类别：静默失败 / 诊断缺失
- 后果：歧义被静默消除，无诊断
- 修复：命中 ≥2 个不同符号时报 ambiguous

#### [高] 8-3｜找不到「有成员」的类时返回 fallback（无成员类）
- 位置：`SemanticAnalyzer.c:618-648`
- 类别：静默失败 / 契约破坏
- 后果：调用方拿到不完整类符号，后续成员解析必然失败或错误绑定，无诊断
- 修复：区分前向声明与已定义，返回 NULL 或明确诊断

#### [高] 8-4｜限定名两种解释先到先得 + `parts[...]` 未判空
- 位置：`SemanticAnalyzer.c:547-570`
- 类别：静默失败 / 未检查前置条件
- 触发：`A.B.C` 可整体当命名空间或「命名空间 A.B 中的符号 C」；或上游传 NULL 元素
- 后果：歧义静默裁决；NULL 崩溃
- 修复：判定歧义报错；对 parts 元素判空/断言

#### [高] 8-5｜未知类型统一返回 `TOKEN_EOF`
- 位置：`SemanticAnalyzer.c:650-670`
- 类别：阶段不对齐
- 后果：把「文件结束符」当「未知类型」哨兵；`AST_MEMBER_ACCESS` 依赖 `resolved_type` 就绪，时序早于解析完成时静默返回 EOF
- 修复：引入 `KRT_TYPE_UNKNOWN` 并在解析前给诊断

#### [中] 8-6｜`add_using_directive` 清理逻辑经核查基本正确
- 位置：`SemanticAnalyzer.c:440-513`
- 类别：未检查前置条件（待验证）
- 说明：循环内 `KRT_STRDUP` 失败时槽位由 CALLOC 保证为 NULL，释放遍历完整，`using_count` 未递增，无泄漏；真正待验证的是 `semantic_analyzer_add_error_at` 在 analyzer 有效但 using_node 为 NULL 时是否安全

#### [中] 8-7｜`semantic_class_has_members` 双遍历存储模型未确认
- 位置：`SemanticAnalyzer.c:600-616`
- 类别：疑似缺陷
- 需验证：`SymbolTable` 的 `symbols` 数组与哈希表是否独立存储；若独立则漏判/误判

#### [中] 8-8｜三元表达式递归无深度上限
- 位置：`SemanticAnalyzer.c:650-670`
- 类别：未检查前置条件
- 后果：深嵌套三元（Parser 可产生）栈溢出，与 `:238-292` 同族
- 修复：深度上限

---

### 第九批 · `SemanticAnalyzer.c:700-900`

#### [致命] 9-1｜`infer_expression_type` 所有失败路径统一降级为 `TOKEN_INT32`
- 位置：`SemanticAnalyzer.c:708-803`
- 类别：静默失败（全项目最大的汇聚点）
- 触发：`expr==NULL`、标识符查不到符号、调用查不到 callee、`default:` 分支
- 后果：未解析符号/未知表达式被静默赋予 int32 并流入 IR 生成，产出类型错误的机器码而非报错
- 修复：未知类型返回 `KRT_TYPE_UNKNOWN`，调用方中止分析并给带位置诊断

#### [致命] 9-2｜三种错误上报函数行为互不一致且 `add_error_at` 对用户不可见
- 位置：`SemanticAnalyzer.c:846-860`（只写 report）、`:805-816`（只写 OutputCache）、`:818-835`（两者都写但打印不带行列）
- 类别：诊断缺失 / 错误处理不一致
- 后果：`add_error_at` 的错误用户完全不可见（除非别处转储 report）
- 修复：合并为唯一 `semantic_analyzer_error(analyzer, node, fmt, ...)`，同时写 report 与输出流并总是带位置

#### [致命] 9-3｜legacy mangling 手工 `__` 拼接 + snprintf 截断
- 位置：`SemanticAnalyzer.c:730-745`
- 类别：语义分叉 / 静默失败
- 触发：超长类名/方法名
- 后果：`char legacy[256]` 截断无检查 → 错误 mangled name，查找失败落入 INT32 默认分支；该规则与 `NameMangling.c` 并行实现
- 修复：调用 `NameMangling.c` 唯一入口，删除此处拼接

#### [高] 9-4｜`AST_NUMBER` 只做上界检查（无下界）
- 位置：`SemanticAnalyzer.c:723-733`
- 类别：未检查前置条件
- 状态：**已撤回**。复核确认 `integer_value` 是 `unsigned __int128`（`Ast.h:168`），不存在负值下界问题

#### [高] 9-5｜三元类型推断两套逻辑
- 位置：`SemanticAnalyzer.c:757-777` vs `:700-707`
- 类别：语义分叉
- 后果：前者 `TOKEN_STRING && TOKEN_NULL` 组合判断并含 `left==right` 分支，后者用 `AST_NULL` 节点判断且无该分支
- 修复：抽成唯一 `semantic_ternary_result_type()`

#### [高] 9-6｜一元运算对所有运算符返回操作数类型
- 位置：`SemanticAnalyzer.c:748`
- 类别：静默失败
- 触发：逻辑非 `!x` 应返回 BOOL；若返回 INT 会使 `if (!x)` 条件类型错误
- 备注：需确认 `TOKEN_NOT`/`TOKEN_BITWISE_NOT` 语义划分

#### [高] 9-7｜二元运算见到 STRING 即返回 STRING，且不校验操作数
- 位置：`SemanticAnalyzer.c:780-786`
- 类别：静默失败
- 后果：`-`、`*` 等非法字符串运算被赋予 STRING 类型；`TOKEN_AND`/`TOKEN_OR` 直接返回 BOOL 而不校验是按位还是逻辑
- 修复：按运算符白名单校验

#### [高] 9-8｜错误上报缓冲区尺寸三种（1024/512）+ 无位置
- 位置：`SemanticAnalyzer.c:805-816`、`:862-873`、`:846-860`
- 类别：错误处理不一致 / 诊断缺失
- 后果：`add_error` 完全不带位置且绕过 `error_report`
- 修复：统一缓冲尺寸与通道

#### [高] 9-9｜`add_error_at` 中 `vsnprintf` 返回值未检查
- 位置：`SemanticAnalyzer.c:846-860`
- 类别：诊断缺失
- 后果：超 512 的诊断被静默截断，可能截掉关键路径/符号名
- 修复：检查返回值，超长做截断标记或动态缓冲

#### [高] 9-10｜`add_error_ex` 接收行列不校验且打印不含位置
- 位置：`SemanticAnalyzer.c:818-835`
- 类别：诊断缺失 / 阶段不对齐
- 后果：0/负值照传；终端看不到行列
- 修复：打印 `[stage:line:col]`

#### [中] 9-11｜`analyzer == NULL` 时静默返回，错误被吞
- 位置：`SemanticAnalyzer.c:805-816`
- 修复：至少 `fprintf(stderr,...)` 兜底

#### [中] 9-12｜「无位置」表示不一致（node?line:0 vs 调用方传 0）
- 位置：`SemanticAnalyzer.c:838`
- 类别：阶段不对齐
- 修复：统一表示

#### [疑似] 9-S1｜`integer_value` 符号性与 parser 负字面量处理（决定 9-4 是否成立）
- 备注：已撤回（见 9-4）

#### [疑似] 9-S2｜`TOKEN_NOT` 逻辑非还是按位非；`TOKEN_AND`/`TOKEN_OR` 逻辑还是按位（决定 9-6、9-7）

#### [疑似] 9-S3｜`symbol_table_lookup_scope_chain` 作用于 class 的 nested_table 时父链是否通向全局（决定 `semantic_lookup_exact_in_class` 是否名不副实）

#### [疑似] 9-S4｜`error_report` 是否在别处被转储（若无，9-2 升级更严重）

---

### 第十批 · `SemanticAnalyzer.c:900-1120` + `ParserExpression.c` 交叉验证

#### [致命] 10-1｜参数个数不同的重载可能被判分并选中
- 位置：`SemanticAnalyzer.c:937-1010`（`semantic_parameter_score`）
- 类别：未检查前置条件
- 触发：候选 `parameter_count != argc`
- 后果：不拒绝候选，而用全零伪参数继续评分 → 参数个数不同的重载可能选中，产生错误绑定
- 修复：`parameter_count != argc` 直接 `return false`

#### [致命] 10-2｜`_ZN...` 前缀 `snprintf` 截断无检查 → 可能匹配错误符号
- 位置：`SemanticAnalyzer.c:1012-1061`
- 类别：静默失败
- 触发：类名/方法名较长
- 后果：前缀截断后 `strncmp` 仍可能匹配到错误符号或全不匹配 → 静默选错重载或回退 INT32
- 修复：检查返回值，超长报诊断

#### [致命] 10-3｜`TOKEN_POWER` 无优先级，`**` 永不被当作二元运算符
- 位置：`ParserExpression.c:620-657`
- 状态：**已撤回**（见本节末复核更正）。全项目 `TOKEN_POWER` 只作双指针解引用，语言中不存在幂运算；`get_operator_precedence` 不含它是正确的。但 `IrGen.c:1468` 的 `case TOKEN_POWER` 与之冲突，见 11-1

#### [致命] 10-4｜`var_name[0]` 在 `var_name == NULL` 时解引用
- 位置：`SemanticAnalyzer.c:1096-1099`
- 类别：未检查前置条件
- 触发：parser 错误恢复产生 NULL 名字
- 后果：崩溃
- 修复：判空并报带位置错误

#### [高] 10-5｜`bits = bits * 10 + suffix[i] - '0'` 溢出检查在累加之前
- 位置：`SemanticAnalyzer.c:905-935`
- 类别：未检查前置条件 / 语义分叉
- 触发：超长数字串；或 `bits == 0`（`i0_`）
- 后果：int 溢出为负通过检查传给 `KrtTokenIntegerType(bits,...)`；与 `NameMangling.c` 并行实现
- 修复：累加后立即检查 `bits < 0 || bits > 128` 并拒绝 0

#### [高] 10-6｜同时遍历 scope 链与 hash_table 的重复评分/过期副本风险
- 位置：`SemanticAnalyzer.c:1012-1061`
- 类别：疑似缺陷
- 需验证：`SymbolTable.c` 存储模型；若哈希表持过期副本可能选中错误条目

#### [高] 10-7｜评分相等的多个重载先遍历者胜
- 位置：`SemanticAnalyzer.c:1012-1061`
- 类别：静默失败
- 后果：歧义静默裁决，无诊断
- 修复：相等时报 ambiguous overload

#### [高] 10-8｜`is_unsafe_mode` 作用域管理（降级）
- 位置：`SemanticAnalyzer.c:1087-1094`
- 类别：配置漂移 / 作用域错误
- 后果：analyzer 级全局开关；复核发现 `:1126-1129` 与 `:2599-2609` 均有 `old_mode` 保存/恢复（有作用域管理），但**恢复是否在所有出口（含提前 return）执行未验证**
- 修复：改为块作用域并验证所有出口

#### [高] 10-9｜mangled 名构造/解析/legacy 拼接分散三处
- 位置：`SemanticAnalyzer.c:1012-1061`、`:730-745`、`:905-935`
- 类别：语义分叉
- 修复：收敛到 `NameMangling.c` 唯一编解码入口

#### [中] 10-10｜`signature->parameters[k]` 未断言非 NULL
- 位置：`SemanticAnalyzer.c:937-1010`
- 触发：`parameter_count == argc` 但 `parameters == NULL`
- 后果：NULL 解引用
- 修复：契约断言

#### [中] 10-11｜`nested->hash_size` 与实际桶数一致性未断言
- 位置：`SemanticAnalyzer.c:1012-1061`
- 需验证：realloc 失败路径；否则越界
- 修复：查 `SymbolTable.c`

#### [中] 10-12｜运算符优先级表需与语言规范核对
- 位置：`ParserExpression.c:620-657`
- 说明：`TOKEN_PIPE` 与 `TOKEN_BITWISE_OR` 同为 3，`TOKEN_BITWISE_AND`(5) 低于 `TOKEN_EQUAL`(6)；C 语义下正确，若语言规范不同则为缺陷

#### [疑似] 10-S1｜`TOKEN_POWER` 到底是否仅为双解引用（10-3 已撤回，仍需确认 `IrGen.c:1468` 不是二元运算）

#### [疑似] 10-S2｜`is_unsafe_mode` 设置/复位点是否按块作用域管理（见 10-8）

#### [疑似] 10-S3｜`SymbolTable` 的 symbols 作用域链与 hash_table 是否同一批条目（决定 10-6、10-11）

#### 复核更正（紧接第十批之后）
- **撤回** 10-3（`TOKEN_POWER` 无优先级）：全项目仅作双指针解引用，无幂运算。
- **降级** 10-8（unsafe 全局开关）：有 `old_mode` 保存/恢复；但所有出口未验证。
- **新增线索**：`SymbolScope` 自带 `hash_table/hash_size`（`SymbolTable.h:47-53`），而 `semantic_lookup_method_by_arity` 只遍历 `sc->symbols` 链 + `nested->hash_table`，**从不遍历 `sc->hash_table`**；若 `symbol_table_define` 写 `scope->hash_table`，该类查找会漏掉整批方法（并入 12-2 处理）。

---

### 第十一批 · `SemanticAnalyzer.c:1120-1200,2590-2620` + `ParserStatement.c:540-580`

#### [致命] 11-1｜`**` 语义前后端冲突（前端双解引用 vs 后端 `KrtIrPow`）
- 位置：`IrGen.c:1468-1469` ↔ `ParserExpression.c:620-657`、`ParserStatement.c:197-210`
- 类别：语义分叉 / 死代码
- 触发：用户写 `a ** b`
- 后果：前端只把 `**` 当双指针解引用且优先级为 0，不产生二元 POWER 节点；后端 `case TOKEN_POWER: return KrtIrPow(...)` 的唯一调用点即此处 → `KrtIrPow`（`Ir.c:799`）是死代码
- 修复：统一——前端补 `TOKEN_POWER` 优先级/结合性，或删除 `KrtIrPow` 分支并文档化 `**` 仅为多级解引用

#### [致命] 11-2｜赋值名 `KRT_STRDUP` 返回值与源指针均未判空
- 位置：`ParserStatement.c:540-548`
- 类别：未检查前置条件
- 触发：strdup 失败或 `left->data.identifier_name == NULL`
- 后果：写入 NULL；`SemanticAnalyzer.c:1184` 把 `var_name` 直接交给 `lookup_scope_chain` → NULL 解引用崩溃
- 修复：两处判空并报带位置错误

#### [致命] 11-3｜表达式形式 `AST_UNSAFE_CALL` 完全忽略 permissions
- 位置：`SemanticAnalyzer.c:1124-1130`
- 类别：语义分叉 / 静默失效
- 触发：表达式写法 `unsafe("krt.mem", ...)`
- 后果：不设置 `unsafe_mem`（语句形式 `:2595-2610` 会设置）；两种形式共用字段却行为不同
- 状态：**降级**。复核确认 `unsafe_mem` 仅在 `SemanticAnalyzer.c:1924` 一处生效（影响面为指针深度声明检查），仍属语义分叉但范围有限
- 修复：抽出唯一 `semantic_analyzer_enter_unsafe(analyzer, permissions, count)`

#### [高] 11-4｜二元表达式左右操作数未判空
- 位置：`SemanticAnalyzer.c:1133-1162`
- 触发：`left == NULL`；`binary_op.right == NULL`
- 后果：解引用崩溃
- 修复：入口统一判空

#### [高] 11-5｜`TOKEN_ASSIGN` 不做类型兼容性检查
- 位置：`SemanticAnalyzer.c:1133-1162`
- 后果：`int_var = "hello"` 静默通过流入 IR
- 修复：比较 left/right 类型可转换性

#### [高] 11-6｜赋值语句不检查可变性与类型
- 位置：`SemanticAnalyzer.c:1182-1190`
- 后果：const/静态字段/只读成员可被赋值；类型不校验；`var_name` 未判空
- 修复：加可变性与类型校验

#### [高] 11-7｜一元运算无运算符相关校验
- 位置：`SemanticAnalyzer.c:1164-1165`
- 后果：`-` 作用字符串、`!` 作用 void 全放行
- 修复：按运算符白名单校验

#### [高] 11-8｜数组/元组字面量不检查元素类型一致性
- 位置：`SemanticAnalyzer.c:1170-1179`
- 后果：异构数组/元组静默通过
- 修复：比较元素类型

#### [高] 11-9｜INT32 兜底使 11-5/11-7 校验形同虚设
- 位置：`SemanticAnalyzer.c:1133-1162`
- 后果：存在未解析符号时校验基于假类型（依赖 9-1）
- 修复：随 9-1 一并解决

#### [中] 11-10｜数组赋值目标落入 "Unsupported assignment target"
- 位置：`ParserStatement.c:552-570` vs `SemanticAnalyzer.c:1136`
- 状态：**已撤回**。复核确认 `ParserStatement.c:512-537` 明确处理 `AST_ARRAY_ACCESS` → `AST_ARRAY_ASSIGNMENT`/`AST_ARRAY_COMPOUND_ASSIGNMENT`，阶段契约一致

#### [中] 11-11｜strdup 失败时销毁 AST 丢失标识符，诊断缺位置
- 位置：`ParserStatement.c:540-548`
- 后果：后续错误诊断丢失标识符
- 修复：失败时保留名字用于诊断

#### [疑似] 11-S1｜`unsafe_mem` 的实际强制点（决定 11-3 影响面；已知 `:1924`）

#### [疑似] 11-S2｜数组赋值是否真的无法解析（已撤回，见 11-10）

#### [疑似] 11-S3｜`SymbolTable` 的 `scope->hash_table` 是否被写入（决定方法查找漏项；并入 12-2）

#### 复核更正（紧接第十一批之后）
- **撤回** 11-10（数组赋值无法解析）：`ParserStatement.c:512-537` 已处理。
- **降级** 11-3（unsafe 表达式忽略 permissions）：影响面限 `:1924` 指针深度检查。
- **新增**：`symbol_table_lookup`（`SymbolTable.c:330-346`）只查表级哈希，而 `define/declare` 同时写表级哈希与 scope 链 → 需确认 `pop_scope` 清理完整性（并入 12-2/12-5）。

---

### 第十二批 · `SymbolTable.c` 全量 + `SemanticAnalyzer.c:2440-2480`

#### [致命] 12-1｜作用域链遍历起点错误导致子作用域全部泄漏
- 位置：`SymbolTable.c:123-153`（`destroy` 从 `global_scope` 起，而 `push_scope` 把 `new_scope->parent = current_scope`，global 是链尾 parent==NULL）
- 类别：资源与生命周期
- 触发：错误路径未配对 `pop_scope`
- 后果：残留子作用域的 `SymbolScope`、桶数组与符号全部泄漏
- 修复：从 `current_scope` 起向上遍历到 global_scope

#### [致命] 12-2｜符号查找只查表级哈希 + 作用域链失败回退全表哈希
- 位置：`SymbolTable.c:330-346`；`declare`（`:249-251`）无条件写表级哈希；`lookup_scope_chain`（`:354-368`）回退全表哈希
- 类别：语义分叉 / 作用域泄漏
- 触发：块内 shadowing；残留作用域
- 后果：`SemanticAnalyzer.c:573` 的 `lookup_with_usings` 直接用它，块内 shadowing 会取到外层/残留符号
- 修复：删除全表回退，或让 `symbol_table_lookup` 只查 `global_scope`

#### [致命] 12-3｜两个 SymbolEntry 共享同一 nested_table
- 位置：`SemanticAnalyzer.c:2470-2471`（`self_class_symbol->nested_table = class_sym->nested_table`）
- 类别：资源与生命周期 / 契约破坏
- 触发：任何新的共享（别名、命名空间合并、嵌套类引用）
- 后果：`symbol_table_destroy` 仅靠 `sym->nested_table != table` 防重入，新共享会 double free
- 修复：引入 `nested_table_owner` 字段，只有所有者负责销毁

#### [高] 12-4｜前向引用机制整体失效
- 位置：`SymbolTable.h:79-81` 声明 `register_forward_ref`/`resolve_forward_refs`，`SymbolTable.c` 无定义
- 类别：死代码 / 功能缺失
- 后果：`forward_refs` 数组是纯死代码；`SYMBOL_FORWARD_REF` 状态（`SemanticAnalyzer.c:191` 预登记）没有任何机制转为 DEFINED
- 修复：实现或删除

#### [高] 12-5｜`pop_scope` 哈希找不到时不释放符号
- 位置：`SymbolTable.c:169-208`
- 类别：资源与生命周期 / 契约破坏
- 触发：任何只写哈希不写 `scope->symbols` 的新路径
- 后果：`sym`/`sym->name` 泄漏，符号仍留在表级哈希（作用域泄漏）
- 修复：找不到时也释放并 `KRT_ASSERT`/报错

#### [高] 12-6｜`SYMBOL_DECLARED → SYMBOL_DEFINED` 不校验 type 一致性
- 位置：`SymbolTable.c:265-302`
- 后果：先 declare VARIABLE 再 define FUNCTION 会留下错误 type，无诊断
- 修复：类型不一致报错

#### [高] 12-7｜`grow_table_index` 失败静默继续
- 位置：`SymbolTable.c:228-232`
- 类别：错误处理不一致
- 后果：负载因子无上限增长
- 修复：改为返回 bool 并传播

#### [高] 12-8｜共享 nested_table 置空只对当前符号生效
- 位置：`SymbolTable.c:2471`
- 后果：另一方仍持悬垂指针（销毁顺序变化即 UAF），与 12-3 同源
- 修复：同 12-3

#### [中] 12-9｜`hash_size == 0` 无保护 → 除零崩溃
- 位置：`SymbolTable.c:330`（`lookup`）、`hash_string`（`:9-15`）
- 后果：`hash_string` 除零
- 修复：函数内统一判零

#### [中] 12-10｜`symbol_table_add_error` 是否带位置未确认
- 位置：`SymbolTable.c:542-557`
- 备注：`declare`/`define` 的重复错误只传名字无行列

#### [疑似] 12-S1｜`SYMBOL_FORWARD_REF` 是否有其他转换路径（决定 12-4 是「死接口」还是「功能缺失」）

#### [疑似] 12-S2｜`symbol_table_add_error` 是否打印行列

#### [疑似] 12-S3｜`enter/exit_nested_scope`（`:607-632`）在错误路径下是否泄漏 scope

---

### 第十三批 · `SymbolTable.c` 调用面统计 + `SemanticAnalyzer.c:3743-3924`

#### [致命] 13-1｜SymbolTable 大量接口无人调用，连带 error_count 不影响成败
- 位置：`SymbolTable.h:79-82` + `SymbolTable.c:439-466`
- 类别：死代码 / 静默失败
- 后果：`get/set_entry_point`、`check_entry_point_conflict`、`mark_defined`、`define_namespace`、`print_stats`、`get_error_count` 外部引用数 0；`error_count` 无消费者 → 符号表错误不影响编译成败判定；多入口点从不被检测；forward_refs 与 FORWARD_REF 状态无解析机制
- 修复：删除死接口，或接入 pipeline 并让 error_count 参与成败判定

#### [致命] 13-2｜同名重载的第二个方法被静默跳过
- 位置：`SemanticAnalyzer.c:3894-3905`
- 类别：静默失败
- 触发：同一类中定义同名重载
- 后果：裸名 `bare_sym` 已定义 → 第二个方法 `lookup_current_scope` 命中后跳过；两个重载共享同一 `SymbolEntry`，`value_type`/`source_type` 停留在第一个重载（`alias_sym` `:3879-3890` 同问题）
- 修复：裸名/别名只在无重载时定义，或定义为容器挂载所有重载

#### [致命] 13-3｜`enter_nested_scope` 永久改变 current_scope，错误路径脆弱
- 位置：`SemanticAnalyzer.c:3826-3909`
- 类别：契约破坏 / 脆弱状态管理
- 触发：将来在该分支内加入任何 `return false`/`goto`
- 后果：作用域永久错位（当前分支无提前 return，全靠 continue，暂安全）
- 修复：goto cleanup 模式统一恢复

#### [高] 13-4｜`symbol_table_enter_nested_scope` 对 create 返回 NULL 直接解引用
- 位置：`SymbolTable.c:607-623`
- 触发：`symbol_table_create()` 返回 NULL
- 后果：下一行 `entry->nested_table->current_scope` NULL 解引用（对比 `SemanticAnalyzer.c:2455-2460` 有显式检查）
- 修复：`if (!entry->nested_table || !entry->nested_table->current_scope)`

#### [高] 13-5｜`full_name[256]` 截断无检查
- 位置：`SemanticAnalyzer.c:3879-3882`
- 类别：静默失败
- 后果：长类名/方法名静默生成错误别名；与 `:730-745`、`:1012-1020` 三处独立 256/512 缓冲同族
- 修复：统一动态拼接并检查

#### [高] 13-6｜构造器返回类型被静默设为 INT32
- 位置：`SemanticAnalyzer.c:3866-3876`
- 类别：静默失败
- 修复：构造器显式设为类类型或 void

#### [高] 13-7｜`member->function_type` 未判空即克隆
- 位置：`SemanticAnalyzer.c:3869-3876`（`:3884-3886`、`:3896-3899`、`:3902-3904`）
- 需验证：`KrtSourceTypeClone` 是否判空

#### [高] 13-8｜未定义检查不覆盖 `SYMBOL_FORWARD_REF`
- 位置：`SymbolTable.c:398-412`
- 后果：跨文件预登记但未定义的函数永远不会被告警（`SemanticAnalyzer.c:195,3373,3393` 大量产生 FORWARD_REF）
- 修复：纳入未定义检查或明确区分

#### [高] 13-9｜入口点设置两套机制，其中一套是死代码
- 位置：`SymbolTable.c:457-466`
- 后果：`set_entry_point` 找不到/类型不符时静默返回无诊断；与 `SemanticAnalyzer.c:2659-2663,3016-3020` 直接操作 `is_entry_point` 并存
- 修复：统一入口点设置

#### [高] 13-10｜`get_entry_point` 硬编码 `"main"`，忽略可配置入口名
- 位置：`SymbolTable.c:447`
- 类别：配置漂移
- 后果：与 `analyzer->entry_point_name`（`SemanticAnalyzer.c:2662,3019`）不一致；死代码一旦启用即出错
- 修复：读取配置

#### [中] 13-11｜`symbol_table_add_error` `vsnprintf` 未检查、无位置
- 位置：`SymbolTable.c:542-557`

#### [中] 13-12｜`print_stats` 未判 `current_scope == NULL`
- 位置：`SymbolTable.c:504-537`

#### [中] 13-13｜预登记分支 `func_name == NULL` 静默跳过；硬编码 "main"
- 位置：`SemanticAnalyzer.c:3800-3808`
- 后果：配置漂移 + 诊断缺失

#### [中] 13-14｜`imported_index < 0` 时导入失败状态丢失
- 位置：`SemanticAnalyzer.c:3916-3919`
- 后果：`imported_source_failed` 不记录

#### [疑似] 13-S1｜`KrtSourceTypeClone` 对 `function == NULL` 是否安全（决定 13-7）

#### [疑似] 13-S2｜`symbol_table_check_undefined` 的唯一调用点是否真的依赖它做未定义检查

---

### 第十四批 · `SemanticAnalyzer.c:1400-1620` + `PointerAnalysis.inc` 全量

#### [致命] F14-1｜`global::` 调用名被指针前移，AST 销毁时释放内部指针
- 位置：`SemanticAnalyzer.c:1621-1625`；`Ast.c:205-207`；`Allocator.c:276-290`
- 类别：资源与生命周期 + 静默失败
- 触发：源码出现 `global::foo()`
- 后果：`call.name = call.name + 8` 前移；销毁时 `KRT_FREE` 释放块内偏移指针 → `find_block` 精确匹配失败 → `KrtMemoryReportError`（release 下空操作）→ 错误被吞、字符串永久泄漏、无诊断（debug 下 `__debugbreak`）
- 修复：不要原地改写指针，用局部变量 `name + 8` 或已有的 `is_global_scope` 字段

#### [致命] F14-2｜`semantic_expression_impl` 的 `default: return true;`
- 位置：`SemanticAnalyzer.c:1601-1602`
- 类别：静默失败
- 触发：未被 switch 覆盖的表达式节点（当前唯一实际可达：`AST_SIZEOF_EXPRESSION`）
- 后果：`sizeof(T)` 语义阶段不校验 `sizeof_expr.type_token`，`resolved_type` 被兜底改写成 INT32；新增节点忘加 case 无任何报错
- 修复：`default` 改为报错 return false；至少补 `AST_SIZEOF_EXPRESSION` case

#### [致命] F14-3｜解析器从不产出但 IrGen 实现了的节点类型 = 死代码
- 位置：`IrGen.c:2193/2198/2200-2201/2203`；枚举在 `Ast.h`，销毁分支在 `Ast.c`
- 类别：死代码 / 注释与实现脱节
- 后果：`as`、`??`、`?.`、字符串插值、模式匹配、元组访问、delegate 等特性在 IrGen 有完整实现却永不可达；语义分析对它们一律 return true，三层空转
- 修复：补齐语法产出，或删除死分支与枚举

#### [高] F14-4｜成员访问无法解析时返回 `object_ok`（true）
- 位置：`SemanticAnalyzer.c:1519`
- 类别：静默失败 / 诊断缺失
- 触发：未定义成员、类名拼写错误、fallback 类、静态成员查找失败
- 后果：错误成员访问进入 IR 生成，可能生成错误成员偏移
- 修复：报 "Undefined member" 并 return false

#### [高] F14-5｜LINQ 子句空指针 + 未处理子句静默跳过 + 结果类型不设置
- 位置：`SemanticAnalyzer.c:1581-1593`
- 类别：未检查前置条件 / 静默失败 / 阶段不对齐
- 触发：`from`/`select` 结构不完整；出现 group/join/let 子句
- 后果：`clause == NULL` 崩溃；group/join/let 被静默跳过；`resolved_type` 保持 0 下游被兜底 INT32
- 修复：判空；未支持子句报错；设置 resolved_type

#### [高] F14-6｜`ok = ok && analyze(...)` 短路吞掉后续错误
- 位置：`SemanticAnalyzer.c:1577-1578`、`:1584`、`:1586`、`:1591-1592`
- 类别：诊断缺失（多错误只报一个）
- 后果：用户只看到第一个错误
- 修复：`bool sub = analyze(...); ok = ok && sub;`（不用短路）

#### [高] F14-7｜lambda 参数名/参数节点未判空；空 body 无诊断
- 位置：`SemanticAnalyzer.c:1552`、`:1551`、`:1561-1570`
- 类别：未检查前置条件 / 诊断缺失
- 后果：`symbol_table_define(table, NULL, ...)` 行为未知；空 body 静默失败
- 修复：补判空报错；expression/body 皆空显式报错

#### [高] F14-8｜`PointerAnalysis.inc` AST_CALL 分支对 `name` 未判空即 strcmp
- 位置：`PointerAnalysis.inc:254`
- 类别：未检查前置条件
- 触发：`data.call.object != NULL` 且 `data.call.name == NULL`（`KrtParserStrdup` 对 NULL 入参返回 NULL）
- 后果：`strcmp(NULL,...)` 崩溃；同文件 `:250-251` 的 lookup 是判空的，属同层不一致
- 修复：`object && name && !strcmp(...)`

#### [高] F14-9｜内建 `Length` 按名字匹配，劫持用户同名成员
- 位置：`SemanticAnalyzer.c:1504`
- 类别：语义分叉 / 静默失败
- 触发：用户定义名为 `Length` 的成员且未被前面分支命中（如 SYMBOL_VARIABLE 或 fallback 类）
- 后果：用户成员永远不可见，落到 `:1519` 返回 true
- 修复：内建成员用独立命名空间/属性标记判定，或先确认用户表中不存在该名字

#### [高] F14-10｜实例类型来源双轨（`resolved_type.type_name` vs `class_type_name`）
- 位置：`SemanticAnalyzer.c:1421` 与 `:1425`
- 类别：阶段不对齐 / 语义分叉
- 后果：同一表达式在不同上下文可能解析到不同类；1421 先赋值后又被 1422-1427 覆盖，冗余
- 修复：只保留一个真源（建议 `variable->class_type_name`）

#### [高] F14-11｜非标识符数组访问不做非数组检查；索引检查两套口径
- 位置：`SemanticAnalyzer.c:1535-1541` vs `PointerAnalysis.inc` AST_ARRAY_ACCESS 分支
- 类别：静默失败 / 语义分叉
- 触发：`f()[0]`、`(a+b)[0]`、`this.items[0]`
- 后果：非数组表达式用下标完全跳过检查；整数校验逻辑分散两处
- 修复：抽成单一 helper 对所有表达式类型生效

#### [高] F14-12｜`AST_SIZEOF_EXPRESSION` 语义类型被兜底为 INT32
- 位置：`SemanticAnalyzer.c:1598-1599` + `:1694-1695`；`IrGen.c:2196`
- 类别：阶段不对齐
- 后果：语义阶段认为 sizeof 是 int32，IR 阶段生成 usize 常量；存在 `f(int)/f(usize)` 重载时错误绑定 `f(int)`
- 修复：显式设置 `TOKEN_USIZE`

#### [中] F14-13｜`AST_CAST_EXPRESSION` impl 只递归不校验
- 位置：`SemanticAnalyzer.c:1598-1599`
- 类别：契约破坏
- 后果：`cast_expr.expression == NULL` 时静默返回 false 不报错；`target_type` 合法性全链路无校验
- 修复：显式检查 target_type 与 expression

#### [中] F14-14｜错误信息可能传 NULL
- 位置：`SemanticAnalyzer.c:1462`
- 后果：`printf("%s", NULL)` 是 UB（glibc 打 `(null)`，其他 libc 崩溃）
- 修复：报错前判空

#### [中] F14-15｜`KRT_STRDUP` 返回值未检查
- 位置：`SemanticAnalyzer.c:1434`、`:1454`、`:1487`、`:1490` 附近
- 类别：静默失败（【修订 G1】）
- 后果：strdup 失败返回 NULL 且不 exit（G1）→ 静默 NULL 传播、无诊断；原文「死代码式防御」结论作废
- 修复：统一分配失败约定并真正检查/报错

#### [高] F14-16｜`PointerAnalysis.inc:294-296` 冗余二次推导，部分节点最终类型变 INT32
- 位置：`PointerAnalysis.inc:294-296`（post-switch 约 `:380`）
- 类别：语义分叉 / 阶段不对齐
- 触发：没有 post-switch 分支的节点类型（`AST_BOOLEAN`、`AST_STRING`、`AST_CHAR_LITERAL`、`AST_ASSIGNMENT`、`AST_COMPOUND_ASSIGNMENT`、`AST_UNSAFE_CALL`、`AST_TUPLE_EXPRESSION`、`AST_LAMBDA_EXPRESSION`、`AST_LINQ_QUERY`、`AST_STATIC_METHOD_CALL`）
- 后果：覆盖结果成为最终结果，`resolved_type` 全部变 INT32（F14-5/F14-12 根因之一）
- 修复：删除冗余覆盖，统一由 post-switch 推导
- 备注：本条是复核更正中「撤销上一轮『覆盖摧毁所有指针元数据』致命推断」后重新定级的真实缺陷；原推断经实证（14 个指针对用例全过）不成立

#### [疑似] F14-S1｜`call_argument_assignable` 对 `parameters == NULL` 的解引用
- 位置：`PointerAnalysis.inc:275`
- 需验证：`KrtSourceTypeClone` 与函数类型构造是否保证 `parameters != NULL`

#### [疑似] F14-S2｜`callee.function && !callee.pointer_depth` 优先级
- 位置：`PointerAnalysis.inc:262`
- 需验证：`KrtSourceTypeClone` 是否可能同时设置两者

#### [疑似] F14-S3｜用户自定义 `Length` 是否真被内建分支劫持（需构造测试用例）

---

### 第十五批 · `SemanticAnalyzer.c:1620-1900`（`analyze_function_call`）

#### [致命] F15-1｜`strcmp(method_symbol->name, mangled_name)` 中 `mangled_name` 可为 NULL
- 位置：`SemanticAnalyzer.c:1749`（同类模式 `:1816`）
- 类别：未检查前置条件
- 触发：`name_mangle_function` 返回 NULL → 兜底 `lookup_method_by_arity` 返回非 NULL → 类型检查通过 → `strcmp(..., NULL)`
- 后果：SIGSEGV（`if (!method_symbol)` 拦不住，因为兜底已填上）
- 修复：`if (mangled_name && strcmp(...) != 0)`，或 mangled_name 为 NULL 时直接复制

#### [高] F15-2｜变量被当函数调用时完全不检查参数
- 位置：`SemanticAnalyzer.c:1874`、`:1893`
- 类别：静默失败 / 契约破坏
- 触发：`let f = 3; f(1, 2);`
- 后果：语义放行，错误推迟到 IR；与 `PointerAnalysis.inc` AST_CALL post-switch 的检查强度不同（两套实现）
- 修复：变量调用走函数指针类型检查，否则报 "is not callable"

#### [高] F15-3｜`resolved_class_name` 覆盖赋值前未释放 → 泄漏
- 位置：`SemanticAnalyzer.c:1847`
- 类别：资源与生命周期
- 触发：同一 AST_CALL 被多次分析且第一次已设置该字段
- 后果：旧字符串永久泄漏；同文件 `:1754-1756`、`:1823-1826` 均先释放 → 同层三种写法不一致
- 修复：抽 `set_resolved_class_name()`，内部先 FREE 再 STRDUP

#### [高] F15-4｜未定义变量被当作类名，诊断与实际错误不符
- 位置：`SemanticAnalyzer.c:1700-1701`、`:1762`
- 触发：`undefinedVar.method()`
- 后果：报 "Undefined method/function" 而非 "Undefined variable"，误导用户
- 修复：`var_sym == NULL` 时显式报未定义变量

#### [高] F15-5｜`method_name`/`class_name` 未判空即 strstr/strlen
- 位置：`SemanticAnalyzer.c:1837`、`:1842`
- 后果：UB/崩溃
- 修复：进分支前统一判空

#### [高] F15-6｜完全相同查找重复执行（死代码）
- 位置：`SemanticAnalyzer.c:1860` 与 `:1864`
- 后果：同一表同一键查两遍，结果必然相同
- 修复：删除 `:1864` 重复查找

#### [高] F15-7｜`KRT_STRDUP(NULL)` 失败被误报为内存分配失败
- 位置：`SemanticAnalyzer.c:1852` + `:1855-1857`
- 类别：诊断与实际错误不符
- 后果：真实原因是节点缺名，却报 "Memory allocation failed"
- 修复：判空后报 "Call expression has no name"

#### [高] F15-8｜方法查找三段静默回退链
- 位置：`SemanticAnalyzer.c:1732-1743`、`:1797-1815`
- 类别：语义分叉 / 静默失败
- 触发：方法在类内不存在
- 后果：同名全局方法「接管」类内缺失调用，无歧义/缺失提示；两条实例调用路径回退策略不一致
- 修复：统一为单一解析函数，回退至少产生 warning

#### [中] F15-9｜legacy 手工 mangling 与 NameMangling.c 分叉
- 位置：`SemanticAnalyzer.c:1837-1849`
- 后果：`class__method` 两处实现，规则变化必然不同步
- 修复：改调 `name_mangle_simple(class_name, method_name)`

#### [中] F15-10｜`KRT_MALLOC` 失败检查为死代码但同层其他分支当真
- 位置：`SemanticAnalyzer.c:1744`、`:1844`
- 类别：错误处理不一致（【修订 G1】）
- 后果：G1 后这些检查是**活路径**（分配失败返回 NULL）；但 `:1855` 又为 `!func_name` 报错 → 同函数两种假设
- 修复：统一分配失败约定

#### [疑似] F15-S1｜手工识别 mangled 名 `name[0]=='_' && name[1]=='_Z'` 是第三种 mangling 约定判定
- 位置：`SemanticAnalyzer.c:1816-1817`
- 需验证：`NameMangling.c` 实际前缀；是否存在非 mangled 但下划线开头的用户方法被误判

#### [疑似] F15-S2｜1700 分支与 1762 分支 `is_instance_call` 设置不一致（1758 设置，1762 未见）
- 需验证：IR 生成是否依赖该标记区分实例/静态调用；若依赖则 1762 路径调用会被错误降级

---

### 第十六批 · `SemanticAnalyzer.c:1900-2010`

#### [致命] F16-1｜全局函数调用解析成功后覆盖 `call.name` 未释放旧指针
- 位置：`SemanticAnalyzer.c:1901-1905`
- 类别：资源与生命周期
- 触发：`call_expr->data.call.name = func_name;` 直接覆盖
- 后果：旧 name 永久泄漏；若旧 name 是 F14-1 的前移内部指针则加剧
- 修复：保存 old → 赋值 → `KRT_FREE(old)`

#### [致命] F16-2｜泛型多参数被静默截断 + 缺 `>` 不报错
- 位置：`SemanticAnalyzer.c:1944-1998`
- 类别：静默失败 / 未检查前置条件
- 触发 A：`type_args = KRT_MALLOC(sizeof(char*))` 只分配 1 元素，但循环按 `arg_count` 填充
- 后果 A：`Foo<T,U>` 静默截断为 1 个参数，实例化结果错误
- 触发 B：`args_end == NULL`（缺 `>`）走 else 静默跳过整个泛型校验
- 后果 B：`List<int` 被放行
- 修复：按 arg_count 分配；`args_end == NULL` 时报错 return false
- 备注：【修订 G1】「KRT_MALLOC 失败即 exit，if(base_type) 全死代码」的前提作废，但上述截断/缺 `>` 缺陷仍成立

#### [高] F16-3｜全局调用只分析实参就返回，不校验签名
- 位置：`SemanticAnalyzer.c:1907-1911`
- 类别：静默失败（F15-2 第三次同根出现：1874、1893、1907）
- 后果：`foo(1,2)` 对 `foo(int)` 静默通过
- 修复：抽 `semantic_check_call_signature(call_expr, func_symbol)` 三处统一调用

#### [高] F16-4｜`var_name`/`current_scope` 未判空 + 魔数 `scope_level == 1`
- 位置：`SemanticAnalyzer.c:1916-1936`
- 后果：空指针崩溃；嵌套层级 ≠ 1 时 let 语义判断错误
- 修复：判空；用 `GLOBAL_SCOPE_LEVEL` 常量替代魔数

#### [高] F16-5｜重复声明检查做两遍
- 位置：`SemanticAnalyzer.c:1938-1942` vs `symbol_table_define` 内部（SA:2006 调用）
- 类别：语义分叉 / 诊断不符
- 后果：一处报 duplicate，一处静默
- 修复：只保留 define 内部检查

#### [高] F16-6｜类体内局部变量被标记为 `SYMBOL_FIELD`
- 位置：`SemanticAnalyzer.c:2000-2004`
- 类别：阶段不对齐
- 后果：与 IR 生成对「局部 vs 字段」的假设不一致
- 修复：仅当 scope_level 为类体层级才设 FIELD

#### [中] F16-7｜泛型实例化失败用 `add_error`（只打印不写 report）
- 位置：`SemanticAnalyzer.c:1987`
- 类别：错误处理不一致
- 修复：统一上报通道

#### [中] F16-8｜若干 OOM 防御分支
- 位置：`SemanticAnalyzer.c:1944-1955`
- 类别：未检查前置条件（【修订 G1】）
- 后果：`if (base_type)`/`if (args_str)` 等在 G1 后是**活路径**（分配失败返回 NULL），但均无诊断、静默继续
- 修复：分配失败时报错或中止

---

### 第十七批 · `SemanticAnalyzer.c:2010-2300`（本范围截至 2056 行）

#### [致命] F17-1｜复合赋值路径使用未初始化的 `stmt->resolved_type`
- 位置：`SemanticAnalyzer.c:2180`
- 类别：未检查前置条件（使用未初始化字段）
- 触发：指针数组复合赋值 `pointer_update_assignable(analyzer, stmt, stmt->resolved_type, ...)`，该分支未赋值 `resolved_type`（非指针路径 `:2187` 才赋值）
- 后果：零值/残留类型当作赋值目标类型做兼容性判定 → 放行非法复合赋值或误报
- 修复：先赋值再传入

#### [高] F17-2｜赋值语句不检查左值类别
- 位置：`SemanticAnalyzer.c:2109-2121`
- 类别：静默失败
- 触发：`func_name = 1;` 或 `ClassName = x;`
- 后果：只要 `pointer_assignable` 恰好通过就放行
- 修复：限定 SYMBOL_VARIABLE/FIELD/PARAMETER

#### [高] F17-3｜静态变量初始化不检查类型兼容性、不设 resolved_type
- 位置：`SemanticAnalyzer.c:2091-2104`
- 类别：诊断缺失 / 阶段不对齐
- 触发：`static int x = "hello";`
- 后果：静默通过；下游 IR 拿到未设置的 `resolved_type`；与实例变量路径（`:2059`）不一致
- 修复：复用实例变量类型兼容性检查

#### [高] F17-4｜整数范围检查被一元负号绕过
- 位置：`SemanticAnalyzer.c:2048`
- 类别：静默失败
- 触发：`-9999999999`（AST 是 `AST_UNARY_OPERATION`）
- 后果：溢出被吞
- 修复：对 `-` 一元运算递归取绝对值做范围检查

#### [高] F17-5｜数组赋值路径不校验下标类型
- 位置：`SemanticAnalyzer.c:2142-2160`
- 类别：静默失败 / 语义分叉
- 后果：`arr[1.5] = x;`、`arr["s"] = x;` 静默通过；与 PointerAnalysis 索引校验强度不一致
- 修复：统一调用 `analyze_index_type()`

#### [高] F17-6｜return 保证判定漏 switch/try-catch → 假阳性
- 位置：`SemanticAnalyzer.c:2206-2231`
- 类别：诊断与实际错误不符
- 触发：函数体以「所有分支都 return 的 switch」结尾
- 后果：仍报 "Non-void function must return a value"
- 修复：补充 switch/try-catch 全分支 return 判定

#### [中] F17-7｜`array_size` 从不设置且不检查常量表达式
- 位置：`SemanticAnalyzer.c:2026-2030`
- 后果：`int arr[n];`（n 非常量）静默通过，符号表丢失数组长度信息
- 修复：常量折叠后写入，否则报错

#### [中] F17-8｜`class_type_name = KRT_STRDUP(...)` 失败无诊断
- 位置：`SemanticAnalyzer.c:2019`
- 后果：类名丢失，后续成员解析退化（【修订 G1】后 strdup 失败返回 NULL 是活路径）
- 修复：判空 return false

#### [中] F17-9｜AST 声明类型令牌被原地改写
- 位置：`SemanticAnalyzer.c:2076-2081`
- 类别：阶段不对齐
- 触发：`var_decl->data.variable_decl.type = KrtSourceStorage(type);` 把 `int` 改写为 `TOKEN_UINT64`
- 后果：AST 上声明信息被销毁；IrGen 若再读 `variable_decl.type` 会与 `resolved_type` 分叉
- 修复：不要把存储类型写回 AST


#### [致命] F18-1｜重开已存在命名空间时函数体完全不分析
- 位置：`SemanticAnalyzer.c:2382-2393`
- 类别：静默失败
- 触发条件：重新打开一个已存在的命名空间（`is_new_namespace == false`），整个语句分析循环被 `if (is_new_namespace)` 包住，函数体完全不分析，只跑了 2365-2381 的类预声明。
- 后果：`namespace N { fn f() { 非法代码 } }` 第二次出现时所有语义错误被吞，成员符号不进入符号表，后续引用报「未定义」或静默通过。
- 最小修复：去掉 `if (is_new_namespace)` 包裹（预声明循环可跳过，语句分析必须执行）。

#### [致命] F18-2｜命名空间体类型判断冗余致分支永不可达
- 位置：`SemanticAnalyzer.c:2362-2411`
- 类别：死代码
- 触发条件：外层 `if (body_node && body_node->type == AST_BLOCK)` 已保证类型，内部 `:2364` 再次判断 `AST_BLOCK` 恒真，其 `else if (AST_ACCESS_MODIFIER)`（:2394）与 `else if (is_new_namespace)`（:2409）永不可达。
- 后果：非 block 的命名空间体（单类声明）永远不会被分析；注释/实现脱节。
- 最小修复：把 `AST_BLOCK`/`AST_ACCESS_MODIFIER`/`else` 分支直接挂在外层 `if (body_node)` 上，删除 :2364 冗余判断。
- 备注：非分配失败类死代码，不适用 G1 重分类。

#### [高] F18-3｜nested_table 创建失败静默跳过且作用域不切换
- 位置：`SemanticAnalyzer.c:2340-2344` + `:2353`
- 类别：静默失败 + 契约破坏
- 触发条件：`ns_entry->nested_table = symbol_table_create();` 失败返回 NULL 时，`parent_table` 赋值被跳过且无诊断；`:2353` 判定为假，`analyzer->symbol_table` 不切换。
- 后果：命名空间成员被定义到父作用域，作用域语义破坏，错误被吞。
- 最小修复：创建失败时 `semantic_analyzer_add_error` + `return false`。
- 备注：【修订 G1】分配失败路径为活路径且零诊断（KRT_* 失败返回 NULL），应按后果重定严重度；当前已判高。

#### [高] F18-4｜foreach 循环变量类型从不推导
- 位置：`SemanticAnalyzer.c:2295-2313`
- 类别：阶段不对齐（类型推导缺失）
- 触发条件：`loop_var` 定义后从不从 `iterable->resolved_type` 推导元素类型，`loop_var->source_type` 保持零值。
- 后果：循环体内 `x` 类型为 TOKEN_NONE/INT32 兜底，成员访问、重载解析、指针深度全部错误；循环外类型不可知。
- 最小修复：从 `iterable->resolved_type` 推导元素类型（数组→元素类型，指针→pointee，`Length` 等）写入 `loop_var->source_type`。

#### [高] F18-5｜重复类检查可被 SYMBOL_DECLARED 绕过
- 位置：`SemanticAnalyzer.c:2423`
- 类别：静默失败
- 触发条件：重复类检查仅当 `existing->state == SYMBOL_DEFINED` 才报错；同名符号处于 `SYMBOL_DECLARED`（如 :2375 预声明）时检查被绕过。
- 后果：`symbol_table_define`（:2448）可能静默覆盖既有声明，符号表状态不一致。
- 最小修复：改为 `if (existing) { error; return false; }`，或明确区分 declared/defined 的合法重声明场景。

#### [中] F18-6｜模板参数全部添加失败时静默跳过注册
- 位置：`SemanticAnalyzer.c:2435`
- 类别：静默失败
- 触发条件：`template_param_count > 0` 但 `generics_add_parameter` 全部失败导致 `params_list == NULL` 时，静默跳过模板注册，无诊断。
- 后果：模板类未注册，后续实例化静默失败。
- 最小修复：`if (!params_list) { error; return false; }`。

#### [中] F18-7｜current_scope 判空分支为冗余防御
- 位置：`SemanticAnalyzer.c:2465-2467`
- 类别：死代码（疑似）
- 触发条件：`if (!analyzer->symbol_table->current_scope) { symbol_table_push_scope(...); }`——`symbol_table_create()` 已设置 `global_scope` 与 `current_scope`，此分支大概率为死。
- 后果：误导性防御代码。
- 最小修复：验证后删除，或改为断言。
- 备注：非分配失败类死代码，不适用 G1 重分类。

#### [高] F19-1｜命名空间内 main 劫持入口且多 main 静默覆盖
- 位置：`SemanticAnalyzer.c:2657-2663`
- 类别：静默失败 + 语义分叉
- 触发条件：入口点判定只用 `!current_class && strcmp(func_name, "main") == 0`；命名空间内 `main`（`current_class == NULL`）也被判为入口点；多个 `main` 时 `entry_point_name` 被后者静默覆盖（last-wins）。
- 后果：`namespace N { fn main() {} }` 劫持程序入口；多 main 冲突无诊断，链接/运行期行为不可预期。
- 最小修复：增加「当前作用域为全局作用域且不在任何命名空间内」的判定；检测到第二个 main 时报错。

#### [高] F19-2｜参数类型绑定无越界保护
- 位置：`SemanticAnalyzer.c:2674`
- 类别：未检查前置条件
- 触发条件：`stmt->function_type->parameters[i]` 与 `stmt->data.function_decl.parameters[i]` 均无越界/判空保护；`function_type->parameter_count < parameter_count`（上游解析不一致）即越界读。
- 后果：崩溃或读到垃圾 `KrtSourceType`，参数类型全部错误。
- 最小修复：`if (i < stmt->function_type->parameter_count) param->source_type = ...`，并断言两处 count 一致。
- 备注：【修订 G1】相邻调用点 `SemanticAnalyzer.c:2677` 为未检查分配调用（失败返回 NULL 且零诊断），属活路径，OOM 时 NULL 静默传播。

#### [高] F19-3｜类方法预声明可绕过重复定义检查并被静默覆盖
- 位置：`SemanticAnalyzer.c:2638` + `:2644`
- 类别：静默失败
- 触发条件：重复定义检查只看 `SYMBOL_DEFINED`；类方法在 `:2516` 被预声明为 `SYMBOL_DECLARED`，同名同签名方法因此绕过检查，`:2644` `symbol_table_define` 静默覆盖预声明符号。
- 后果：前向声明/预声明方法符号被覆盖，`source_type`/`value_type` 可能来自错误声明。
- 最小修复：`if (existing) { error; return false; }`，重声明走显式 `state` 转换路径。

#### [高] F19-4｜name_mangle_function 失败静默 continue
- 位置：`SemanticAnalyzer.c:2508-2511`
- 类别：静默失败
- 触发条件：`name_mangle_function` 返回 NULL 时直接 `continue`，成员符号不被定义，无诊断。
- 后果：该类所有方法在符号表中缺失 → 调用报「未定义成员」，错误位置指向调用点而非声明点。
- 最小修复：`if (!key) { semantic_analyzer_add_error_at(...); body_ok = false; continue; }`。

#### [高] F19-5｜类体首错后停止分析其余成员
- 位置：`SemanticAnalyzer.c:2527`
- 类别：诊断缺失（多错误只报一个）
- 触发条件：`for (...; ...; && body_ok; ...)` 在首个错误后停止分析类体其余成员。
- 后果：类体后续成员的所有错误被吞，用户只看到第一个。
- 最小修复：去掉 `&& body_ok`，用 `body_ok = semantic_analyzer_analyze_statement(...) && body_ok;` 累积。

#### [中] F19-6｜switch 不校验 case 常量/重复且 case 无独立作用域
- 位置：`SemanticAnalyzer.c:2553-2568`
- 类别：诊断缺失
- 触发条件：switch 不校验 case 值是否为常量表达式、不检测重复 case / 重复 default；case 体没有独立作用域（各 case 共享 switch 作用域）。
- 后果：非常量 case 静默通过；case 内声明变量泄漏到后续 case，造成符号遮蔽/重定义歧义。
- 最小修复：case 值做常量判定；每个 case 推入/弹出作用域；重复 case 报错。

#### [中] F19-7｜unsafe_call permissions 未判空即 strcmp
- 位置：`SemanticAnalyzer.c:2602`
- 类别：未检查前置条件
- 触发条件：`strcmp(stmt->data.unsafe_call.permissions[i], "krt.mem")` 未判空。
- 后果：权限项为 NULL 时崩溃。
- 最小修复：`if (stmt->data.unsafe_call.permissions[i] && !strcmp(...))`。

#### [中] F19-8｜空访问修饰符节点静默 return true
- 位置：`SemanticAnalyzer.c:2613-2619`
- 类别：静默失败
- 触发条件：`access_modifier.member == NULL` 时静默 `return true`。
- 后果：空的访问修饰符节点被当作合法语句吞掉。
- 最小修复：报「access modifier without member」并返回 false。

#### [中] F19-9｜未知类体成员类型静默 continue
- 位置：`SemanticAnalyzer.c:2503-2505`
- 类别：静默失败
- 触发条件：类体成员类型不是 `STATIC_FUNCTION_DECLARATION` / `FUNCTION_DECLARATION` 时静默 `continue`，不报错也不分析。
- 后果：未知成员节点被吞，符号表缺失。
- 最小修复：`semantic_analyzer_add_error_at(analyzer, member, "Unsupported class member type");`。

#### [致命] F20-1｜静态方法跨作用域链复用符号致原地污染
- 位置：`SemanticAnalyzer.c:2718` + `:2727`
- 类别：共享可变状态 + 语义分叉
- 触发条件：静态方法用 `symbol_table_lookup_scope_chain`（跨作用域链）查找，命中后 `func_sym = existing;` 直接复用该符号，随后 `:2738-2741` 在该符号所在作用域改写 `state/value_type/source_type`。
- 后果：外层作用域同名符号（函数/变量）被静默当作本类静态方法并原地污染；当前类嵌套表无该方法符号 → 后续调用解析失败。
- 最小修复：改用 `symbol_table_lookup_current_scope`；`existing` 仅用于重复定义判定，不得复用；始终在当前作用域 `symbol_table_define`。

#### [高] F20-2｜静态方法 mangle 失败静默回退裸名
- 位置：`SemanticAnalyzer.c:2709-2716`
- 类别：静默失败 + 语义分叉
- 触发条件：`name_mangle_function` 失败时 `if (mangled_name) function_name = mangled_name;` 静默回退为裸方法名；对比实例方法 `:2630-2633` 同场景是报错 + `return false`。
- 后果：同一语义两种处理；静态方法以裸名进入符号表，与实例方法 mangled 名并存 → 重载/调用解析分叉。
- 最小修复：与实例方法统一，mangle 失败即报错返回。

#### [高] F20-3｜静态方法无 body 保护致合法前向声明误报
- 位置：`SemanticAnalyzer.c:2766`
- 类别：诊断与实际错误不符
- 触发条件：静态方法无 `if (body)` 保护（对比 `function_declaration:2666` 有保护）。body 为 NULL（纯声明）时 `analyze_statement(NULL)` 返回 false → `:2767-2772` 再叠加 "Not all control paths return a value"。
- 后果：合法的静态方法前向声明被报成控制流错误；`body_ok` 被置 false。
- 最小修复：`if (!stmt->data.static_function_decl.body) { free; return true; }`，与实例方法对齐。

#### [高] F20-4｜构造器上下文错误/mangle 失败无诊断
- 位置：`SemanticAnalyzer.c:2784-2786` / `:2790-2792`
- 类别：诊断缺失
- 触发条件：构造器在类上下文之外（`!current_class`）或 `name_mangle_constructor` 返回 NULL 时直接 `return false`，无任何诊断。
- 后果：错误被上层当作「分析失败」吞掉，用户看不到原因。
- 最小修复：两处都补 `semantic_analyzer_add_error_at(analyzer, stmt, ...)`。

#### [高] F20-5｜构造器符号从不设置 value_type/source_type
- 位置：`SemanticAnalyzer.c:2807-2808`
- 类别：静默失败 + 阶段不对齐
- 触发条件：构造器符号只设置 `state = SYMBOL_DEFINED`，从不设置 `value_type` / `source_type`。
- 后果：构造器类型全零，调用点（`infer_expression_type`）拿到 TOKEN_NONE/INT32 兜底 → 参数类型检查、重载评分全部错误。
- 最小修复：从 `stmt->function_type`（或参数表）构造 `TOKEN_FN` 类型写入 `value_type`/`source_type`，与实例方法一致。

#### [高] F20-6｜静态方法参数绑定无越界保护（F19-2 同根第二处）
- 位置：`SemanticAnalyzer.c:2752`
- 类别：未检查前置条件
- 触发条件：`stmt->function_type->parameters[i]` 无越界保护（与 F19-2 同根）。
- 后果：`function_type->parameter_count < parameter_count` 时越界读。
- 最小修复：同 F19-2，并抽公共函数消除重复。
- 备注：【修订 G1】相邻调用点 `SemanticAnalyzer.c:2755` 为未检查分配调用（失败返回 NULL 且零诊断），属活路径，OOM 时 NULL 静默传播。

#### [中] F20-7｜dispatcher 中 return 后 break 永不可达
- 位置：`SemanticAnalyzer.c:2863-2900`
- 类别：死代码
- 触发条件：`case AST_XXX: return semantic_...; break;` 中 `break` 在 `return` 之后永不可达（该模式出现 10+ 处）。
- 后果：误导性代码，看起来有 fall-through 保护。
- 最小修复：删除 `return` 后的 `break;`。

#### [中] F20-8｜基类构造参数首错后停止分析
- 位置：`SemanticAnalyzer.c:2821`
- 类别：诊断缺失（多错误只报一个）
- 触发条件：`for (...; ... && base_ok; ...)` 在首个基类构造参数错误后停止。
- 后果：后续 base 参数错误被吞。
- 最小修复：`base_ok = semantic_analyzer_analyze_expression(...) && base_ok;`。

#### [致命] F21-1｜语句 dispatcher default 静默返回 true
- 位置：`SemanticAnalyzer.c:2973-2974`
- 类别：静默失败
- 触发条件：语句 dispatcher 的 `default: return true;`——所有未处理的语句节点类型静默通过（与 F14-2 同根，但在语句层，影响更大）。
- 后果：未实现/新增语句类型（如 AST_MATCH_EXPRESSION、AST_DEFER、AST_TRY_CATCH 等）全部被吞，不做任何分析。
- 最小修复：`default: semantic_analyzer_add_error_at(analyzer, stmt, "Unsupported statement type %d", stmt->type); return false;`。

#### [高] F21-2｜入口点判定第二处实现，类方法 main 也劫持入口
- 位置：`SemanticAnalyzer.c:3014-3021`
- 类别：语义分叉 + 静默失败
- 触发条件：入口点判定的第二处实现（第一处 :2657-2663），同样缺少 `!current_class` 判定，类方法 `main` 也会劫持入口；两处都覆盖 `analyzer->entry_point_name`，后执行者胜。
- 后果：同一语义两处实现且行为不一致（一处要求 `!current_class`，一处无条件）；`main` 冲突静默。
- 最小修复：合并为唯一的 `semantic_analyzer_mark_entry_point(analyzer, func_name, current_class)`，要求全局作用域且无类上下文，重复时报错。

#### [高] F21-3｜has_return_statement 硬编码为 true
- 位置：`SemanticAnalyzer.c:3077`
- 类别：契约破坏
- 触发条件：`result.has_return_statement = true;` 硬编码，与函数实际是否有 return 无关。
- 后果：调用方若依赖该字段判断返回行为，得到永远为真的错误契约（该字段实际是死值）。
- 最小修复：从 body 分析结果推导（如 `semantic_statement_guarantees_return(body)`），或删除该字段。

#### [高] F21-4｜静态函数返回类型误读 union 另一成员
- 位置：`SemanticAnalyzer.c:3062`
- 类别：契约破坏（union 成员误用）
- 触发条件：`analyzer->current_return_type = function_node->function_type ? ... : (KrtSourceType){.token = function_node->data.function_decl.return_type};`——对 `AST_STATIC_FUNCTION_DECLARATION`，当 `function_type == NULL` 时读的是 union 另一成员 `data.function_decl`。
- 后果：依赖 union 内存布局巧合；一旦两个成员偏移不同即读到垃圾返回类型。
- 最小修复：按 `function_node->type` 分支取 `data.static_function_decl.return_type`。

#### [高] F21-5｜纯声明 body 为 NULL 被误报控制流错误（第三处）
- 位置：`SemanticAnalyzer.c:3063-3069`
- 类别：诊断与实际错误不符
- 触发条件：body 为 NULL（纯声明）时 `analyze_statement(NULL)` 返回 false，随后被报成 "Not all control paths return a value"（与 F20-3 同根）。
- 后果：合法声明被误报控制流错误。
- 最小修复：`if (!body) { result.success = true; return result; }`。

#### [高] F21-6｜参数绑定无越界保护（第三处同根）
- 位置：`SemanticAnalyzer.c:3038`
- 类别：未检查前置条件
- 触发条件：`function_node->function_type->parameters[i]` 无越界保护（同根：:2674、:2752、:3038）。
- 后果：`function_type->parameter_count < param_count` 时越界读。
- 最小修复：抽 `semantic_analyzer_bind_parameters(analyzer, function_node, symbol_table)` 唯一实现，内部做边界断言。

#### [高] F21-7｜analyze_function 入参非法静默失败无诊断
- 位置：`SemanticAnalyzer.c:2982-2985`
- 类别：诊断缺失
- 触发条件：`analyze_function` 入参非法时静默 `return result`（success=false），无诊断。
- 后果：调用方只看到失败，无原因。
- 最小修复：补 `semantic_analyzer_add_error_at(analyzer, function_node, "Invalid function node for analysis");`。

#### [中] F21-8｜重复定义查找用全作用域链与 current_scope 不一致
- 位置：`SemanticAnalyzer.c:2991`
- 类别：语义分叉
- 触发条件：`symbol_table_lookup`（全作用域链）与 `function_declaration:2637` 的 `lookup_current_scope` 不一致。
- 后果：重复定义判定在不同路径上作用于不同作用域集合。
- 最小修复：统一为 current_scope（或明确记录 chain 查找的意图）。

#### [致命] F22-1｜type_info->declaration 为 NULL 时实例化静默成功
- 位置：`SemanticAnalyzer.c:3150-3160`
- 类别：静默失败 + 死代码
- 触发条件：`type_info != NULL` 但 `type_info->declaration == NULL` 时，走 `else { if (type_info) { KRT_FREE(type_info); return true; } ... }`——实例化什么都没做却返回成功；内层 `if (type_info)` 在 `else`（`type_info` 为 NULL）分支里是死代码（`KRT_FREE(NULL)` 永不执行）。
- 后果：泛型类型声明缺失时实例化静默成功，下游拿到未实例化的符号；错误被吞。
- 最小修复：`if (!type_info) { error; return false; } if (!type_info->declaration) { error; KRT_FREE(type_info); return false; }`。
- 备注：【修订 G1】内层空指针死代码属 G1 所指「空指针检查类」判定，G1 前提修正后需重估；主结论（NULL declaration 静默 return true）仍成立。`generics_get_type_info` 返回堆拷贝、内部字段为借用指针，`KRT_FREE(type_info)` 只释放外壳，所有权疑虑已解除。

#### [高] F22-2｜约束循环首处 strcmp 未判空
- 位置：`SemanticAnalyzer.c:3225` + `:3227`
- 类别：未检查前置条件 + 错误处理不一致
- 触发条件：第一个约束循环对 `constraint->data.generic_constraint.param_name` / `constraint_type` 未判空即 `strcmp`；同函数第二个循环 `:3251` 对同样字段做了判空。
- 后果：NULL 字段直接崩溃；同一函数内前置检查不一致。
- 最小修复：把判空逻辑抽到循环开头统一处理（两处共用）。

#### [高] F22-3｜type_args 未判空且 class 白名单漏 char
- 位置：`SemanticAnalyzer.c:3270-3285`
- 类别：未检查前置条件 + 语义分叉
- 触发条件：`type_args[param_index]` 未判空即 `strcmp`；`class` 约束白名单为 `{"int","float","string","bool"}`，漏掉 `"char"`，而 `struct` 白名单含 `"char"` 不含 `"string"`。
- 后果：`where T : class` 配 `char` 被误报违反约束；两处白名单硬编码不一致，且与 tokenizer 类型集合分叉。
- 最小修复：`type_arg` 判空；白名单改为单一 `KrtTypeIsValueLike()` 辅助函数，两处共用。

#### [高] F22-4｜未知约束类型静默忽略
- 位置：`SemanticAnalyzer.c:3293-3299`
- 类别：静默失败 + 诊断缺失
- 触发条件：`else` 分支对未知约束类型静默忽略，只对 `"IDisposable"` 做特判。
- 后果：拼写错误或新增约束（如 `IComparable`）静默失效，约束不生效且无诊断。
- 最小修复：`else { semantic_analyzer_add_error(analyzer, "Unknown constraint type '%s'", constraint_type); return false; }`。

#### [高] F22-5｜非法模板参数节点静默跳过且返回值被忽略
- 位置：`SemanticAnalyzer.c:3108-3120`
- 类别：静默失败
- 触发条件：模板参数节点为 NULL 或类型不是 `AST_TEMPLATE_PARAMETER` 时静默跳过，`param_count` 不增加；`generics_add_parameter` 返回值也被忽略。
- 后果：注册参数个数少于实际个数 → 后续 `validate_constraints` 的 `arg_count != template_param_count` 判定错误（误报或误放行）。
- 最小修复：遇到非法参数节点时报错返回；检查 `generics_add_parameter` 返回值。

#### [中] F22-6｜new() 约束不校验可构造性
- 位置：`SemanticAnalyzer.c:3291-3292`
- 类别：静默失败
- 触发条件：`new()` 约束仅 `continue`，不校验类型参数是否真的可构造。
- 后果：`where T : new()` 配结构体/无默认构造器的类型静默通过。
- 最小修复：查符号表确认存在默认构造器。

#### [中] F22-7｜template_params[j] 未判空即 strcmp
- 位置：`SemanticAnalyzer.c:3258`
- 类别：未检查前置条件
- 触发条件：`strcmp(template_params[j], param_name)` 中 `template_params[j]` 未判空。
- 后果：模板参数数组含 NULL 时崩溃。
- 最小修复：`if (template_params[j] && strcmp(...) == 0)`。

#### [致命] F23-1｜函数声明两条并行分析路径语义分叉
- 位置：`SemanticAnalyzer.c:3431-3439` vs `:2620-2701` / `:3441-3458`
- 类别：语义分叉（同一功能两套实现）
- 触发条件：顶层函数走 `semantic_analyzer_analyze_function`（:2978），类体内/经 dispatcher 的函数走 `semantic_analyze_stmt_function_declaration`（:2620）。二者在 mangle 失败处理（报错 vs 裸名回退）、入口点判定（有无 `current_class`）、参数绑定（越界保护）、返回类型来源（union 成员选择）上行为各不相同。
- 后果：同一份函数声明在不同位置被不同语义分析 → 类型/符号不一致；F19-1/F20-2/F21-2/F21-4 全部由此派生。
- 最小修复：合并为唯一 `semantic_analyzer_analyze_function_decl(analyzer, node)`，mangle/入口点/参数绑定只在其中实现一次；另一路径改为调用它。

#### [高] F23-2｜semantic_expand_types 失败后继续分析残缺 AST
- 位置：`SemanticAnalyzer.c:3338-3340`
- 类别：契约破坏（失败后继续）
- 触发条件：`semantic_expand_types` 失败时仅置 `result->success = false`，不提前返回，随后在部分展开的 AST 上跑完整个分析流程。
- 后果：后续阶段基于残缺类型推导，产生大量伪错误或漏报；错误定位指向错误位置。
- 最小修复：`if (!semantic_expand_types(analyzer, ast)) { result->success = false; return result; }`。

#### [高] F23-3｜symbol_table_check_undefined 返回值被丢弃
- 位置：`SemanticAnalyzer.c:3484`
- 类别：静默失败
- 触发条件：`symbol_table_check_undefined(analyzer->symbol_table);` 返回值被丢弃。
- 后果：未定义符号检查的发现不计入 `result->success`，符号表错误不影响编译成败（与批次十三「get_error_count 无人调用」叠加）。
- 最小修复：`if (!symbol_table_check_undefined(...)) result->success = false;`。

#### [高] F23-4｜静态函数返回类型误读 union 另一成员（第二处）
- 位置：`SemanticAnalyzer.c:3396`
- 类别：契约破坏（union 成员误用）
- 触发条件：`pre->value_type = decl->data.function_decl.return_type;` 对 `AST_STATIC_FUNCTION_DECLARATION` 读 union 另一成员 `data.function_decl`（同 F21-4 模式）。
- 后果：依赖 union 布局巧合；布局变化即读到垃圾返回类型。
- 最小修复：按 `decl->type` 分支取 `data.static_function_decl.return_type`。

#### [高] F23-5｜入口点兜底用跨作用域链查找 main
- 位置：`SemanticAnalyzer.c:3486-3491`
- 类别：语义分叉
- 触发条件：入口点兜底用 `symbol_table_lookup`（跨作用域链）找 `"main"`；命名空间内 main、类静态 main（若以裸名注册）都会命中。
- 后果：与 F19-1/F21-2 叠加 → 入口点可被非全局函数静默占用。
- 最小修复：改用 `symbol_table_lookup_current_scope` 在全局作用域查找。

#### [中] F23-6｜FORWARD_REF 报错后不停止继续分析
- 位置：`SemanticAnalyzer.c:3378-3380`
- 类别：错误处理不一致
- 触发条件：`existing->state == SYMBOL_FORWARD_REF` 时报 "Class already declared"，但不返回 false、不停止，后续仍按已存在类处理。
- 后果：重复类声明错误后继续分析，产生二次错误。
- 最小修复：报错误后 `continue`（跳过该类后续处理）或记录 `result->success = false`。

#### [中] F23-7｜顶层非 static 变量节点被重复分析
- 位置：`SemanticAnalyzer.c:3446-3449`
- 类别：重复处理
- 触发条件：顶层非 static 变量在 `:3420-3429` 已报错并置 `success = false`，此处再次调用 `semantic_analyzer_analyze_statement` 分析同一节点。
- 后果：同一节点两次分析，可能重复报错（诊断重复）。
- 最小修复：该分支改为只记录错误，不再分析。

#### [高] F24-1｜success 被无条件重算覆盖累加语义
- 位置：`SemanticAnalyzer.c:3503-3507`
- 类别：语义分叉（同一状态两种更新方式）
- 触发条件：`if (result->error_count > 0) result->success = false; else result->success = true;` 无条件重算并覆盖 `success`；前面 `:3336/:3339/:3353/:3360/:3406/:3415/:3427/:3436/:3455/:3467/:3474/:3480` 全部用累加式 `result->success = false`。
- 后果：任何未通过 `add_error*` 计数的失败（如 `semantic_expand_types` 失败、`symbol_table_check_undefined` 返回值、未来新增失败路径）被静默抹掉 → 编译器报成功。
- 最小修复：删除 `:3503-3507` 的重算，保持 `success` 累加语义；错误计数单独由 `error_count` 承担。

#### [高] F24-2｜path_parts[i] 未判空即 strcmp
- 位置：`SemanticAnalyzer.c:3528`
- 类别：未检查前置条件
- 触发条件：`strcmp(path_parts[i], "*")` 中 `path_parts[i]` 未判空。
- 后果：路径部分含 NULL 时崩溃。
- 最小修复：`if (!path_parts[i]) return NULL;`。

#### [高] F24-3｜类型展开循环首错即整体终止
- 位置：`TypeExpansion.inc:445`
- 类别：诊断缺失（多错误只报一个）
- 触发条件：`for (int i = 0; i < context.count && !analyzer->error_count; i++)`——首个错误使 `error_count > 0` 后整个展开循环终止。
- 后果：AST 只被部分展开，后续分析基于残缺 AST，其余错误全部被吞。
- 最小修复：循环内不依赖全局 error_count 作为终止条件，改为累积局部 `ok`。

#### [高] F24-4｜原地改写 AST 语句数组且契约未文档化
- 位置：`TypeExpansion.inc:448-452`
- 类别：契约破坏（阶段间假设未文档化）
- 触发条件：`ast->data.block.statement_count = context.count; ast->data.block.statements = expansion_alloc(...);` 原地改写 AST 的语句数量与指针，新 buffer 由独立分配器（`expansion_alloc`）给出。
- 后果：下游（IrGen/序列化/调试）若缓存旧 `statements` 指针或原 `statement_count` 即悬垂；该阶段契约完全未文档化、未断言。
- 最小修复：新增 `AST_PROGRAM.expanded` 标记或返回新 AST；至少加 `KRT_ASSERT(old_count == new_count)` 断言数量守恒。

#### [中] F24-5｜路径 snprintf 返回值未检查致静默截断
- 位置：`SemanticAnalyzer.c:3640` / `:3666`
- 类别：静默失败
- 触发条件：`snprintf(sub, sizeof(sub), "%s/%s", ...)` 与 `snprintf(deep, sizeof(deep), "%s/%s", ...)` 返回值未检查。
- 后果：路径超长被静默截断 → 后续 `stat`/`opendir` 作用在错误路径上，导入静默失败。
- 最小修复：检查返回值 `>= sizeof(buf)` 时跳过该条目。

#### [中] F24-6｜KRT_STRDUP 失败与文件不存在不可区分
- 位置：`SemanticAnalyzer.c:3608`
- 类别：静默失败
- 触发条件：`try_file_path` 中 `KRT_STRDUP(path)` 失败返回 NULL，调用方无法区分「文件不存在」与「内存不足」。
- 后果：OOM 被当作「文件不存在」处理。
- 最小修复：区分返回值语义或让调用方检查 `errno`。
- 备注：【修订 G1】KRT_STRDUP 失败返回 NULL 且零诊断，该 OOM 路径为活路径（非死代码），按后果维持「中」。

#### [中] F24-7｜path 分量 size_t→int 转换可致溢出检查失效
- 位置：`SemanticAnalyzer.c:3535`
- 类别：整数溢出（未检查前置条件）
- 触发条件：`part_len = (int)strlen(path_parts[i])`——超长路径分量使 `size_t→int` 转换可能为负值，导致 `len + part_len + 2 > INT_MAX / 2` 检查失效。
- 后果：`new_capacity` 为负 → `KRT_REALLOC` 收到巨大 `size_t` → 失败返回 NULL 走错误路径（不崩溃），但检查形同虚设。
- 最小修复：`if (part_len < 0 || (size_t)part_len > INT_MAX - len - 2) { ... }`。
- 备注：【修订 G1】`KRT_REALLOC` 失败返回 NULL 且零诊断，属活路径。

#### [高] F25-1｜静态函数名误读 union 另一成员（第三处同根）
- 位置：`SemanticAnalyzer.c:186`
- 类别：契约破坏（union 成员误用）
- 触发条件：`collect_exports_from_node` 的 `case AST_FUNCTION_DECLARATION: case AST_STATIC_FUNCTION_DECLARATION:` 分支中 `const char* name = node->data.function_decl.name;`——对静态函数读 union 另一成员。
- 后果：依赖 union 布局巧合（F21-4/F23-4 之后第三处同根）；布局变化即读到垃圾名字并注册错误符号。
- 最小修复：按 `node->type` 分支取 `data.static_function_decl.name`。

#### [高] F25-2｜StdlibCache 共享 AST 原地改写疑点（已撤销）
- 位置：`SemanticAnalyzer.c:3769`
- 类别：共享可变状态（疑似，需验证）
- 触发条件：`KrtStdlibCacheClone(file_path, import_arena, false)` 若返回缓存中的共享 AST（而非深拷贝），随后 `:3788-3796` 起的 `semantic_analyzer_analyze_statement` 会原地改写 `resolved_type`、`data.call.name`（F14-1 的 `+= 8`）、`variable_decl.type`（F17-9）。
- 后果：缓存被污染，后续编译单元/轮次拿到被改写的 AST → 类型与符号全错。
- 最小修复：确认 `KrtStdlibCacheClone` 深拷贝语义；若为共享，改为 `KrtAstClone` 或每单元独立解析。
- 备注：【修订：第二十七批实证撤销】`KrtStdlibCacheClone` 经 `KrtAstClone` 深拷贝（`clone_source_type` 递归克隆 `KrtFunctionType`/`parameters`/`result`；switch 覆盖全部 106/106 `ASTNodeType`，`default: return NULL` 排除浅拷贝兜底），缓存 AST 与调用方 AST 无共享可变状态，本条不成立。

#### [高] F25-3｜同一方法注册三次，裸名劫持全局符号
- 位置：`SemanticAnalyzer.c:3866-3900`
- 类别：语义分叉 + 静默覆盖
- 触发条件：同一方法被注册三次：mangled key（:3871）、`"%s__%s"` 别名（:3887）、裸方法名（:3899）。裸名注册到 `global_symbol_table` 会劫持全局命名空间同名函数（与 F14-9 `Length` 劫持同根）。
- 后果：全局函数 `write()` 与类方法 `write()` 冲突时后者覆盖前者；三份 `value_type`/`source_type` 重复设置，行为分叉。
- 最小修复：只保留 mangled key；别名/裸名改为 `SYMBOL_ALIAS` 引用同一符号，不重复定义。

#### [中] F25-4｜导入文件 main 未计入入口点且无诊断
- 位置：`SemanticAnalyzer.c:3801-3813`
- 类别：阶段不对齐
- 触发条件：导入文件中的 `main` 被显式排除（:3801），但类静态 `main`（mangled 名）未被排除也未计入 `has_entry_point`。
- 后果：入口点判定在导入边界处不一致——主文件 `main` 才算入口，导入文件 `main` 静默忽略，编译产物可能没有入口而无任何诊断。
- 最小修复：统一入口点规则，导入文件的 main 应报错或明确忽略并给出说明。

#### [中] F25-5｜full_name snprintf 返回值未检查致静默截断
- 位置：`SemanticAnalyzer.c:3883`
- 类别：静默失败
- 触发条件：`snprintf(full_name, sizeof(full_name), "%s__%s", class_name, mname)` 返回值未检查。
- 后果：类名+方法名超过 255 字节时静默截断，生成的别名可能与其他符号冲突，且错误地注册为合法别名。
- 最小修复：检查返回值 `>= sizeof(full_name)` 时跳过并诊断。

#### [中] F25-6｜容量检查冗余（第一处）
- 位置：`SemanticAnalyzer.c:3710-3715`
- 类别：死代码（冗余检查）
- 触发条件：`analyzer->imported_source_capacity > INT_MAX / 2 || (size_t)(...) > SIZE_MAX / sizeof(...)`——第一个条件被第二个条件完全覆盖。
- 后果：误导性防御代码。
- 最小修复：删除第一个条件，保留 `size_t` 版本。

#### [高] F26-1｜命名空间分隔符 `.` 与 `/` 不一致
- 位置：`SemanticAnalyzer.c:3561-3585` vs `:3512-3559`
- 类别：语义分叉（同一概念两种表示）
- 触发条件：`namespace_matches_using` 用 `.` 作分隔符匹配（:3578），而 `resolve_using_path` 用 `/` 拼接导入路径（:3550）。
- 后果：命名空间名含 `.` 时匹配判定与路径解析结果不一致 → 该 using 被错误判为项目命名空间（:4034）或解析到错误路径。
- 最小修复：统一分隔符（内部统一为 `.`，仅在文件系统拼接时转 `/`）。

#### [中] F26-2｜目录枚举坏条目致后续文件全部不导入
- 位置：`SemanticAnalyzer.c:3966-3969`
- 类别：静默失败（不完整导入）
- 触发条件：目录枚举循环中任一条目 `snprintf` 截断或 `stat` 失败即 `success = false; break;`。
- 后果：该目录后续所有 `.krt` 文件全部不被导入，只产生一条错误；导入结果不完整且无明确说明。
- 最小修复：改为 `continue`（跳过坏条目），最后汇总。

#### [中] F26-3｜首个 import_krt_file 失败即 break
- 位置：`SemanticAnalyzer.c:4005-4009`
- 类别：静默失败（不完整导入）
- 触发条件：`for` 循环中首个 `import_krt_file` 失败即 `break`。
- 后果：同目录下剩余文件静默不导入。
- 最小修复：累积 `success = false` 而不是 break。

#### [中] F26-4｜容量检查冗余（第三处同模式）
- 位置：`SemanticAnalyzer.c:3974-3975`
- 类别：死代码（冗余检查）
- 触发条件：`capacity > INT_MAX / 2 || (size_t)(capacity * 2) > SIZE_MAX / sizeof(*paths)`——第一个条件被第二个完全覆盖（第三处同模式：`:3710`、`:3974`、另 `TypeExpansion.inc`）。
- 后果：误导性防御代码。
- 最小修复：统一抽 `safe_grow_capacity()` 辅助函数，删除冗余分支。

#### [中] F26-5｜硬编码默认库路径 ./libs 与配置项并存
- 位置：`SemanticAnalyzer.c:4045`
- 类别：配置漂移
- 触发条件：`const char* libs_dir = analyzer->libs_path ? analyzer->libs_path : "./libs";`——硬编码默认库路径与 `libs_path` 配置项并存。
- 后果：未设置 `libs_path` 时静默使用 `./libs`，与用户预期「当前目录」可能不符；未文档化。
- 最小修复：默认值集中到配置模块，并在无 `libs_path` 时给出警告。

#### [中] F26-6｜非法 using 指令静默 continue
- 位置：`SemanticAnalyzer.c:4031-4033`
- 类别：诊断缺失
- 触发条件：`!directive || !directive->namespace_path || directive->path_length == 0` 时静默 `continue`。
- 后果：非法 using 指令被吞，无任何诊断。
- 最小修复：报 "Malformed using directive" 并 `success = false`。

#### [中] F26-7｜using X.* 展开到空目录即报错失败（疑似误报）
- 位置：`SemanticAnalyzer.c:3999-4001`
- 类别：诊断与实际错误不符（疑似误报）
- 触发条件：`using Foo.*` 展开到的目录中没有 `.krt` 文件时报 error 并失败。
- 后果：空子目录（合法存在）导致整个 using 失败。
- 最小修复：区分「目录不存在」（error）与「目录为空」（warning 或静默）。
- 备注：疑似；需验证 `using X.*` 的预期语义是否要求目录非空。

#### [高] S27-1｜KrtStdlibCacheGetStats 与命中/未命中计数器零调用者
- 位置：`StdlibCache.inc:46-55`（计数器 `:22/:155/:177`；声明 `CompilerPipeline.h:71`）
- 类别：死代码
- 触发条件：`KrtStdlibCacheGetStats` 零外部调用者；`g_stdlib_cache_hits/misses` 只写不读。
- 后果：无效统计代码。
- 最小修复：删除该函数与两个计数器，或接入诊断输出。

#### [高] S27-2｜全局缓存锁在锁内做 I/O 与 AST 克隆致完全串行
- 位置：`StdlibCache.inc:142,156,178,185`
- 类别：共享可变状态/并发
- 触发条件：并行编译多文件同时 `using` 同一 stdlib（`ParallelCompiler.c`）。
- 后果：全局 `g_stdlib_cache_mutex` 在锁内执行文件 I/O、词法/语法分析、万级节点 AST 克隆 → 所有导入完全串行化；一线程阻塞则全部编译线程停滞。
- 最小修复：锁内只做查找+指针交换，解析/克隆移到锁外；用 per-entry mutex 或引用计数。

#### [高] S27-3｜缓存键不感知编译器版本与宏集合
- 位置：`StdlibCache.inc:151-153`
- 类别：配置漂移
- 触发条件：升级 KrtC / 修改 `load_standard_macros` 后继续编译。
- 后果：缓存键仅 `(path, expand_macros)+stat`，不感知编译器版本与宏集合 → 命中旧 AST，跨版本语义分叉且无告警。
- 最小修复：缓存键加入编译器版本号+宏集合哈希，或提供 `--no-stdlib-cache`。

#### [中] S27-4｜stdlib 九个失败出口无法区分原因
- 位置：`StdlibCache.inc:74-135`
- 类别：诊断缺失
- 触发条件：stdlib 文件损坏/不可读。
- 后果：9 个失败出口（`:76/:80/:93/:99/:111/:115/:119/:123/:128`）全只返回 `bool`，SA:3772 只能报 "unable to read or parse imported source"，无法区分文件不存在 / fopen 失败 / fread 短读 / 预处理器失败 / 词法错误 / 语法错误。
- 最小修复：加 `const char** reason` 出参或在函数内带行号报错。

#### [中] S27-5｜parse_stdlib_entry 失败时部分初始化依赖唯一调用方兜底
- 位置：`StdlibCache.inc:104-130` + `:24-34`
- 类别：契约破坏
- 触发条件：未来出现第二个调用方。
- 后果：`parse_stdlib_entry` 失败时 `entry` 处于部分初始化状态（source/path/lexer/parser 部分填充），依赖唯一调用方 `:179` 的 `free_stdlib_entry` 兜底；`free_stdlib_entry` 又依赖 `memset` 零初始化（:33）。契约未文档化。
- 最小修复：函数入口 `memset(entry,0,...)`，失败时自行清理，或显式注释该契约。

#### [中] S27-6｜path==NULL 静默返回 NULL，调用方 printf("%s", NULL) 是 UB
- 位置：`StdlibCache.inc:138-140`
- 类别：未检查前置条件
- 触发条件：`path == NULL`（导出 API，`CompilerPipeline.h:73`）。
- 后果：静默返回 NULL；调用方 SA:3772 以 `'%s'` 打印 path → `printf("%s", NULL)` 是 UB（glibc 打 "(null)"，其他 libc 崩溃）。
- 最小修复：判空后用 `%s` 安全值，或 `KRT_COMPILE_ERROR`。

#### [中] S27-7｜parser_parse 返回非 PROGRAM/BLOCK 时整个导入静默成功
- 位置：`SemanticAnalyzer.c:3787`
- 类别：静默失败
- 触发条件：`parser_parse` 返回非 PROGRAM/BLOCK 根节点。
- 后果：无 `else` 分支 → 整个导入静默「成功」（`success` 仅比较 error_count），符号表零导入符号，编译继续。
- 最小修复：加 `else { semantic_analyzer_add_error(...); }`。

#### [疑似] S27-8｜缓存字节统计漏算 declared_functions 与预处理器产物
- 位置：`StdlibCache.inc:132-133`
- 类别：静默失败/统计失真（疑似，需验证）
- 触发条件：`entry->bytes` 只统计 `source + parser->arena + lexer->arena`，不含 `parser->declared_functions` 数组（KRT_MALLOC）与 Preprocessor 中间产物。
- 后果：64MB 上限可能系统性低估。
- 最小修复：补齐统计项。
- 备注：需验证——构造大 stdlib 文件，对比 `g_stdlib_cache_bytes` 与进程 RSS 增量。

#### [疑似] S27-9｜atexit 清理与并发 Clone 的数据竞争
- 位置：`StdlibCache.inc:58`
- 类别：资源生命周期/并发（疑似，需验证）
- 触发条件：`atexit(KrtStdlibCacheClear)` 在进程退出时调 `parser_destroy`，若此时仍有线程在 `KrtStdlibCacheClone` 锁内。
- 后果：数据竞争。
- 最小修复：退出前 join 所有编译线程。
- 备注：需验证——是否存在编译线程未 join 就退出的路径。

#### [致命] S28-1｜数组字面量元素步长语义分叉【已作废（G2）】
- 位置：`Re.KrtC/src/Ir/IrGen.c:1841-1855`（表达式路径）vs `:2742-2760`（变量初始化路径）
- 类别：语义分叉 + 静默错误数据
- 触发条件（原）：数组字面量出现在赋值右侧、函数实参、返回值等表达式上下文（dispatcher `:2166` 走 `:1841`）。
- 代码事实（原）：表达式路径 `:1848` `total_size = element_count * KRT_IMM_PTR_SIZE(builder)`（硬编码 8），`:1853` `KrtIrArrayStore(...)`（`Ir.c:1136-1138` 内部硬编码 size=8）；变量初始化路径 `:2751` 分配 `element_count * size`，`:2756` 用 `KrtIrArrayStoreSized(..., size)`。
- 后果（原）：表达式上下文数组字面量静默读错位置，产生错误数据，无编译错误/越界报警。
- 最小修复（原）：`:1848` 改为 `element_count * irgen_array_element_size(...)`；`:1853` 改用 `KrtIrArrayStoreSized(..., element_size)`；合并为唯一实现。
- 备注：【修订 G2】本条整体作废。原结论基于非法语法 `function int32 main()`，实证无效。正确结论见 S31-1：声明上下文正确，仅非声明上下文数组字面量步长错误。

#### [高] S28-2｜get/set 作函数名被静默误解析为属性访问器
- 位置：`IrGen.c:251-256` 相关解析入口（实证触发）
- 类别：诊断与实际错误不符 + 语义分叉
- 触发条件：`int32 get(int32 a) { return a; }` → 编译报 `error: Undefined identifier: a`；`set` 同理；`foo`/`value` 正常。
- 后果：解析器把签名当 getter/setter，参数不进作用域，且不报「get/set 是保留字」，报的是无关的「未定义标识符」，误导排查。
- 最小修复：保留字作函数名时在声明处直接报「保留字不可作函数名」，而非进入属性访问器解析。

#### [高] S28-3｜irgen_size_from_token 的 default: return 8 静默兜底
- 位置：`IrGen.c:54-56`
- 类别：静默失败 + 未检查前置条件
- 触发条件：`TOKEN_VOID`/`TOKEN_EOF`/`TOKEN_TYPE_STRING`/`TOKEN_FN` 全部静默当 8 字节。调用点 `:2197`（`sizeof`）、数组 stride `:1361/1367/1534/1858/2037/2233/2341/2506`。
- 后果：`sizeof(void)` 静默得 8；元素类型未解析时 stride 错误 → 后续读写越界。
- 最小修复：非数据 token 返回 0 并让调用方报「无法确定大小」，禁止默认 8。

#### [高] S28-4｜irgen_checked_array_index 元素类型为 TOKEN_EOF 时跳过越界检查
- 位置：`IrGen.c:126-128`
- 类别：静默失败 + 契约破坏
- 触发条件：任何元素类型解析失败（返回 `TOKEN_EOF`）的数组下标访问。
- 后果：安全特性静默失效，越界检查被直接跳过，只做 `KrtIrCast(index, TOKEN_UINT64)`。
- 最小修复：元素类型未知时报编译错误，而非跳过检查。

#### [高] S28-5｜new T[] 缺 size 静默返回 0 长度数组
- 位置：`IrGen.c:289-291`
- 类别：静默失败 + 诊断缺失
- 触发条件：`new T[]` 缺 size 表达式。
- 后果：`return KRT_IMM_ZERO(builder)` 当作「空数组」，下游按 0 长度处理，错误被吞。
- 最小修复：报「new 数组缺少长度」并终止。

#### [高] S28-6｜堆分配乘法溢出未检查
- 位置：`IrGen.c:295` `KrtIrMul(count, element_size)`
- 类别：未检查前置条件
- 触发条件：`new int64[0x2000000000000000]` → 乘积回绕到 0 或极小值。
- 后果：分配过小但 length 记大值 → 堆越界写。
- 最小修复：生成代码前或运行期检查 `count > UINT64_MAX/element_size`。

#### [高] S28-7｜irgen_free_memory 对非 mmap 指针调 munmap(ptr-16)
- 位置：`IrGen.c:353-374`，调用点 `:1661`（`KrtFree`）、`:1722`（`delete obj`）、`:2679`（delete 语句）
- 类别：未检查前置条件 + 资源生命周期
- 触发条件：对 `stackalloc` 缓冲区执行 `delete`/`KrtFree`。`irgen_expr_stackalloc_expression:2075-2076` 用 `KrtIrStackAlloc`，不走 `irgen_allocate_memory`，没有 16 字节头。
- 后果：读 `ptr-8` 当 total_size，对无效地址 `munmap` → 崩溃/破坏内存。
- 最小修复：引入所有权标记（指针头或元数据），释放时校验来源；编译期对 stackalloc 结果拒绝 delete。

#### [中] S28-8｜g_generic_context 为 _Thread_local 全局，嵌套泛型互相覆盖
- 位置：`IrGen.c:172`
- 类别：共享可变状态
- 触发条件：泛型函数内再调泛型函数（嵌套实例化）。
- 后果：外层 context 被覆盖且无法恢复，实例化错乱。
- 最小修复：改为按调用栈保存/恢复，或用参数显式传递。

#### [中] S28-9｜魔数散落（syscall 号、mmap flags）
- 位置：`:155-156`（exit 70 / syscall 60）、`:318`（mmap 9）、`:370`（munmap 11）、`:315-317`（flags 3 / 0x22）
- 类别：语义分叉 + 配置漂移
- 触发条件：同一 ABI 常量在多处硬编码。
- 后果：平台移植时漂移。
- 最小修复：集中到 `PlatformAbstraction`/`NativeAbi` 唯一常量表。

#### [中] S28-10｜KRT_IRGEN_CHECK_ALLOC 宏错误值契约不统一
- 位置：`IrGen.c:202-207`
- 类别：契约破坏 + 错误处理不一致
- 触发条件：用于返回 `KrtIRValue` 结构体的函数时用 `void_val_return()`（`:1888`）而非真 NULL。
- 后果：调用方无法用统一方式判断失败。
- 最小修复：统一返回 `NULL` 或统一返回带错误位的 value，二选一并全量替换。

#### [中] S28-11｜irgen_get_builtin_func 越界返回 NULL，调用方不检查
- 位置：`IrGen.c:251-256`
- 类别：静默失败 + 未检查前置条件
- 触发条件：builtin 名字不在表内。
- 后果：7 个 `irgen_call_builtin_*` 直接把可能为 NULL 的名字传给 `KrtIrCall`。
- 最小修复：越界时报错或返回静态错误节点。

#### [中] S28-12｜irgen_array_expression_element_type 的 TOKEN_EOF 契约未文档化
- 位置：`:7` 个调用点（`:126/499/1326/1824/2346/3342` 等）
- 类别：契约破坏
- 触发条件：`:126`（跳过检查）与 `:499`（布尔判断）对同一 `TOKEN_EOF` 语义解释不同。
- 后果：正是 S28-4 的根因。
- 最小修复：定义枚举（如 `ELEMENT_TYPE_UNKNOWN`）替代 `TOKEN_EOF`，并在头文件写明契约。

#### [疑似] S28-13｜`function int32 main(){...}` 不报 unexpected token
- 位置：解析器入口（`ParserStatement.c` 顶层声明函数）
- 类别：静默误解析（疑似，需验证）
- 触发条件：`function` 关键字 + 返回类型的形态不报「unexpected token」，静默误解析，函数体变顶层块 → 后续报 `Top-level variable must be static`。
- 后果：非法语法被误解析，诊断与实际错误不符。
- 最小修复：需先确认是语法定义有意支持还是分支遗漏，再决定报错或支持。
- 备注：疑似；该形态即 G2 所述非法语法，S28-1 的实证即建立在此形态上。

#### [致命] S29-1｜try/catch/finally/throw 完全无语义，catch 在正常路径也执行
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2719-2736`（`irgen_stmt_try_statement`）、`:2711-2717`（`irgen_stmt_throw_statement`）、`:478-480`（`irgen_throw_exception`）
- 类别：静默失败 + 阶段不对齐 + 契约破坏
- 触发条件：任何含 `try`/`catch`/`finally`/`throw` 的程序。
- 实证：`Test/SyntaxAudit/cases/C70_trycatch.krt`（`try{P("t")} throw new Exception("boom") catch{P("c")}`）→ 实际输出 `tc`；`try{P("a")} finally{P("f")} P("b")` → 实际输出 `afb`。`irgen_stmt_try_statement` 只顺序调用 `krt_ir_generate_block` 生成 try、catch、finally 三个块，既无异常表/landing pad，也无条件分支；`throw` 仅调用 `IRGEN_FUNC_THROW_EXCEPTION` builtin，不改变控制流。
- 后果：异常处理彻底失效。catch 块在正常路径也执行（`tc`），finally 在 throw 后不保证执行，`throw` 后代码继续跑；资源清理与错误传播语义全部错误。
- 最小修复：实现异常处理 IR（异常表 + landing pad + 从 throw 到匹配 catch 的条件分支），或在语义分析阶段直接拒绝 `try/catch/throw/finally` 并报错；二者必居其一，不能保留「静默顺序展开」。

#### [致命] S29-2｜switch 分派完全失效，仅 case 1/2 偶然正确
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2561-2629`（`irgen_stmt_switch_statement`），缺陷核心 `:2596-2604`
- 类别：未检查前置条件（IR 块多终止符）+ 死代码
- 触发条件：`switch` 含 ≥2 个 case，或含 `default`。
- 实证：IR entry 块中 `day = copy 2`；`t68 = eq day 1`；`t69 = branch t68`；`t71 = eq day 2`、`t72 = branch t71`、`t73 = jump` 均为死代码。运行实证：`day=3/5/9` 全部输出 `T`（case 2 的 body），而非 `F`/`D`/`D`；只有 `day=1`→`M`、`day=2`→`T` 正确。
- 根因：比较/分支循环（`:2596-2604`）在同一当前块连续追加多条 `KrtIrBranch`（`Ir.c:867` `KrtIrBranch` 直接 append，无「块已终止则另起新块」保护），后续分支全部不可达；`case_blocks[i]` 的 false 目标被硬编码为 `case_blocks[i+1]`，只有 i=0 时恰好等于正确链。
- 后果：所有 ≥3 分支的 switch 语义错误；default 永不执行；case 体内出现重复 `jump`。
- 最小修复：每个比较分支生成到独立前缀块（比较块），或用单条比较链 + 显式「比较块→下一比较块」的 fall-through 跳转；`KrtIrBranch` 在 `current_block` 已有终止符时应报错或自动切新块。

#### [高] S29-3｜测试套件把 S29-1 的错误行为固化为期望值
- 位置：`Test/SyntaxAudit/cases/C70_trycatch.exp:1`（内容为 `"tc"`）
- 类别：测试与断言不足（测试掩盖实现缺陷）
- 触发条件：运行 `Test/SyntaxAudit/test_language_regressions.py`。
- 后果：`catch` 在正常路径执行被断言为正确，缺陷被永久掩盖，后续修复会被测试判为回归。
- 最小修复：在 S29-1 修复后把 `.exp` 改为 `"t"`；修复前应删除该用例或标注为已知缺陷。

#### [疑似] S29-4｜using 语句的资源释放顺序/异常安全
- 位置：`IrGen.c:2705-2709`（`irgen_stmt_using_statement`）
- 类别：资源生命周期（疑似，需验证）
- 触发条件：`irgen_dispose`（`:470-472`）在 body 抛异常时是否仍执行；当前实现是顺序生成 body 后调用 dispose，若 S29-1 未修则无意义。
- 后果：异常路径下 dispose 可能不执行，资源泄漏。
- 最小修复：依赖 S29-1 的异常处理实现；需确认 `IRGEN_FUNC_DISPOSE` 对非 disposable 值的处理。
- 备注：疑似；需验证。

## 第三部分　IR / 优化 / 泛型 / 工具链（第三十至四十七批）


#### [致命] S30-1｜`function <类型> 名()` 被静默误路由，函数体按「顶层作用域」解析
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:1639-1648`（`case TOKEN_FUNCTION`）、`:451-458`（`parser_parse_type_inferred_declaration`）、`Re.KrtC/src/Compiler/Frontend/Parser/Parser.c:24-33`
- 类别：1 静默失败 + 2 诊断缺失 + 6 契约破坏
- 触发条件：源码写出 `function int32 main() { ... }`（`function` 后跟类型关键字）。
- 后果：`:1641-1644` 只把 `function 标识符 (` 判为关键字函数，此写法 `is_keyword_fn=false`；`:1648` 静默改走类型推断声明，`:455-458` 吃掉 `function` 后发现当前 token 是 `int32` 而非标识符，直接 `return NULL` 且无诊断；`Parser.c:29-31` 对 NULL 语句 `parser_advance()` 静默跳过一个 token，`int32` 被丢弃。随后 `main()` 被当顶层表达式丢弃、`{...}` 被当顶层块，块内声明落入全局作用域。实证：`function int32 main() { int32 y = 1; }` → 报 `Top-level variable 'y' must be declared 'static'`；`function int32 main() { int32[] a = new int32[3]; a[0]=5; }` → 报 `Top-level variable 'a'`；`function int32 main() { return 5; }` → 顶层 return 被丢弃、IR 为空（见 S30-2），编译器自身 SIGSEGV（exit=139）。对照 `int32 main()` 与 `function main()` 均正常。
- 最小修复：`:1641` 判定改为「`function` 后是类型关键字且再后是 `标识符 (`」时报错并 `parser_report_error` 指明不支持该写法，或直接按带返回类型的函数声明解析；`Parser.c:29-31` NULL 分支必须报告诊断而非静默跳过。
- 备注：无

#### [致命] S30-2｜ArkLink：空模块时 `free` 后未置 NULL 即返回 OK → use-after-free + double-free → SIGSEGV
- 位置：`ArkLink/src/Core/Resolver.c:314-319`（`ark_resolver_resolve` 的 `total_sections == 0` 分支）；触发点 `ArkLink/src/Core/LinkerNative.c:87-95`；崩溃点 `ArkLink/src/Core/Resolver.c:582`（`ark_resolver_plan_destroy`）
- 类别：10 资源与生命周期 + 1 静默失败 + 8 未检查前置条件
- 触发条件：链接一个没有任何 section 的 Kro 对象（由 S30-1 的空 IR 产生，或任何损坏的 Kro）。
- 后果：`:317` `free(out_plan->backend_input)` 后未置 NULL；`:319` `return ARK_LINK_OK` 把「无 section」当成功上报（应为错误）；`LinkerNative.c:88` `if (plan.backend_input)` 判真（悬垂非 NULL），`:90-94` 向已释放内存写 `image_base` → use-after-free；后端读已释放内存，`ark_resolver_plan_destroy` 再次 `free(plan->backend_input)` → double-free → SIGSEGV。实证 gdb 回溯 `ark_resolver_plan_destroy ← arklink_session_link_native ← KrtArkLinkLinkObjects ← link_kro_executable ← compile_single_file`；输入 `function int32 main() { return 5; }` 时 exit=139，`a3.kro` 仅 64 字节（只有文件头）。
- 最小修复：`:317` 改为 `free(...); out_plan->backend_input = NULL;`，`:319` 改为 `return ARK_LINK_ERR_INVALID_ARGUMENT;`；`ark_resolver_plan_destroy` 内所有 `free` 分支补 `= NULL`（`:586-590` 已置，`:603-608` 的 `backend_input` 分支在 `:637` 才置，重复调用仍会 double-free）。
- 备注：无

#### [高] S30-3｜C 风格数组长度为非常量表达式时，静默按 10 个元素分配
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2761-2766`
- 类别：1 静默失败 + 8 未检查前置条件
- 触发条件：`int32 a[n];`（`n` 为变量或任意非恒定表达式）。
- 后果：实证 `int32 n = 3; int32 a[n];` 编译运行均成功，IR 显示 `t10 = imm 56`（16 字节头 + 10×4），长度标记存 `10`，完全忽略 `n=3`；数组大小与源码语义不符，越界读写风险，且本应报错（动态长度在 C 风格数组中不合法）。
- 最小修复：`try_extract_constant` 失败时 `KRT_COMPILE_ERROR` 报出变量名与位置，不要回落到 `10`。
- 备注：无

#### [高] S30-4｜数组长度 ≤ 0 时静默变成 10
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2763-2765`（`(size_value > 0) ? (int)size_value : 10`）
- 类别：1 静默失败
- 触发条件：`int32 a[0];`、`int32 a[-1];`。
- 后果：实证 `int32 a[0]; a[0]=7;` 编译并运行成功（实际分配 10 元素），无任何诊断；非法长度被静默接受并改变语义，`a[-1]` 这类明显错误被吞掉。
- 最小修复：`size_value <= 0` 时报错并停止。
- 备注：无

#### [高] S30-5｜C 风格数组总字节数整数溢出未检查
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2768`（`int total_bytes = array_size * element_size;`）
- 类别：8 未检查前置条件
- 触发条件：`array_size` 较大（如 `int32 a[1000000000];`）→ `int` 溢出为负数或回绕。
- 后果：`irgen_allocate_memory` 收到错误的负/小尺寸，分配不足 → 堆越界写；或 `mmap` 长度异常。
- 最小修复：用 `int64`/`size_t` 计算并检查 `array_size > SIZE_MAX / element_size`，超限报错。
- 备注：无

#### [高] S30-6｜`parser_parse_function_declaration` 全部失败路径静默返回 NULL，无诊断
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:30`、`:35`、`:40`、`:105`、`:113`、`:120`、`:125`
- 类别：2 诊断缺失 + 1 静默失败
- 触发条件：函数声明缺少标识符、缺少 `(`、缺少 `)`、缺少 `{`、缺少函数体等。
- 后果：所有这些语法错误都不产生任何消息，直接被 `Parser.c:29-31` 的 NULL 分支静默吞掉，用户看到「成功」或无关错误。
- 最小修复：每个 `return NULL` 前调用 `parser_report_error` 并带上 `line/col` 与具体原因。
- 备注：无

#### [高] S30-7｜顶层语句解析失败时静默跳过一个 token
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/Parser.c:29-31`
- 类别：1 静默失败 + 2 诊断缺失
- 触发条件：任何 `parser_parse_statement` 返回 NULL 的情形（含 S30-1、S30-6）。
- 后果：错误被吞、token 被静默丢弃，后续解析基于错误状态继续，产生与真实原因无关的次生诊断（如 S30-1 的「Top-level variable」）。
- 最小修复：`!stmt` 分支必须 `parser_report_error` 并计入错误数，且不得静默 `advance`。
- 备注：无

#### [中] S30-8｜静态全局重复声明时复用旧符号且不校验类型
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2806-2812`（`KrtIrModuleFindGlobal` 命中即复用）
- 类别：3 语义分叉 + 1 静默失败
- 触发条件：同一模块内两次声明同名 `static` 变量且类型不同。
- 后果：`global->type` 保持首次类型，第二处声明的类型与初始化被静默忽略，前后不一致。
- 最小修复：命中已存在 global 时校验类型一致，否则报错。
- 备注：无

#### [中] S30-9｜`is_array_literal` 分支缺少 `KrtIrAlloc`
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2745-2758`（对比 `:2770` 的 `KrtIrAlloc(builder, ir_slot)`）
- 类别：7 共享可变状态（疑似）
- 触发条件：数组字面量路径未把变量登记进版本化变量表，后续同名变量的版本解析可能错位。
- 后果：同一作用域内先声明数组字面量、再用同名变量遮蔽时，`KrtIrLoad` 可能取到旧版本。
- 最小修复：确认后补 `KrtIrAlloc(builder, ir_slot);`。
- 备注：疑似，需验证（观察同名遮蔽下 `KrtIrLoad` 是否取到旧版本）。

#### [致命] S31-1｜数组字面量步长语义分叉：表达式路径硬编码 8 字节，声明路径用真实元素大小
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:1841-1855`（`irgen_expr_array_literal`）对比 `:2745-2758`（`irgen_stmt_variable_declaration` 的 `is_array_literal` 分支）；底层硬编码在 `Re.KrtC/src/Compiler/Middle/Ir/Ir.c:1136-1139`（`KrtIrArrayStore` 恒 `element_size=8`）
- 类别：3 语义分叉 + 4 阶段不对齐 + 1 静默失败
- 触发条件：数组字面量出现在任何非变量声明上下文——函数实参、返回值、三元表达式、赋值右侧等。
- 后果：表达式路径 `:1848` `total_size = element_count * KRT_IMM_PTR_SIZE`（恒 8 字节/元素），`:1853` 调 `KrtIrArrayStore` → `Ir.c:1138` 写死 `element_size=8`；声明路径 `:2749` 用 `irgen_size_from_token(type)`、`:2757` 调 `KrtIrArrayStoreSized`；读取侧 `irgen_expr_array_access`（`:1857` 起）用 `irgen_array_element_size`（`:120`）按声明类型算偏移，与表达式路径 8 字节写入步长不一致。实证（语法均合法）：`int32 get1(int32[] a){ return a[1]; } int32 main(){ return get1([11,22,33]); }` → exit=0（应为 22）；`int32 main(){ int32[] a = [11,22,33]; return a[1]; }` → exit=22（声明路径正确）。同一特性两阶段两种内存布局，静默得到 0 或垃圾，无诊断。
- 最小修复：删除 `irgen_expr_array_literal` 的 8 字节假设，改为从 `expr` 元素类型推导 `element_size`（复用 `irgen_array_expression_element_type`，`:60`）并调 `KrtIrArrayStoreSized`；进一步合并为同一 `irgen_emit_array_literal(builder, elements, element_size)`，删除 `KrtIrArrayStore` 的 8 字节默认值。
- 备注：本条即 S28-1 的正确结论（见下）。

#### [已作废（G2）] S28-1｜数组字面量元素步长语义分叉（原结论）
- 位置：原第二十八批（本范围外）；相关代码同 S31-1
- 类别：原判为语义分叉
- 触发条件：原实证 `cs=['A','B','C']; cs[1]` → exit=1
- 后果：原结论称声明上下文也出错。
- 最小修复：不适用。
- 备注：【已作废（G2）】原实证使用 `function int32 main()`，S30-1 已证明这是非法/被误解析的语法，结论无效。正确结论见 S31-1：声明上下文正确，仅非声明上下文（表达式路径）步长错误。

#### [疑似（需验证）] S31-2｜`params[i].type` 可能越界读 `function_type->parameters`
- 位置：`IrGen.c:2833`、`IrGen.c:3038`（`stmt->function_type ? KrtSourceAbiStorage(stmt->function_type->parameters[i]) : ...`）
- 类别：8 未检查前置条件（疑似）
- 触发条件：`function_type->parameter_count` 与 `function_decl.parameter_count` 不等时。
- 后果：`function_type` 来自 `AstClone.inc:126` 克隆、缓存/复用符号或克隆后参数被增删时，`parameters[i]` 越界读。
- 最小修复：加 `i < stmt->function_type->parameter_count` 断言或回退到 `function_decl.parameters[i]`。
- 备注：疑似，需验证（目前只找到 `ParserStatement.c:147-148` 一处赋值使用同一 `param_count`）。

#### [致命] S32-1｜访问控制（`private`/`protected`）完全未实现，外部代码可读写私有成员
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:156-164`（`while (... TOKEN_PUBLIC || TOKEN_PRIVATE || TOKEN_PROTECTED ...)` 只 `parser_advance()` 吞掉修饰符，从不写入 AST）；`Re.KrtC/src/Compiler/Frontend/Parser/Ast.h:357` 的 `access_modifier` 字段无任何生产者；`SemanticAnalyzer.c` 全文无 `E_ACCESS`
- 类别：1 静默失败 + 3 语义分叉 + 4 阶段不对齐
- 触发条件：任何 `private`/`protected` 成员被类外代码访问。
- 后果：实证 `class Box { private int32 secret = 7; } ... return box.secret;` 编译成功 exit=0（应拒绝）；`private int32 Read(){...}` 外部调用 exit=7（应拒绝）；对照 `build/selfhost/stage2/program` 同样代码被拒（E_ACCESS 只在 `SelfHost/Compiler/Frontend/Semantic/Types.krt:180,1521` 实现）。封装契约完全失效；`Test/SelfHost/test_access_control.py`、`test_symbol_index.py:134-136` 依赖的 `E_ACCESS` 在 C 编译器上永不触发。
- 最小修复：在 `ASTNode` 记录访问级别（或给 `function_decl`/字段节点加 `access`），由 `parser_parse_class_member` 写入，并在 `SemanticAnalyzer` 的字段访问/方法调用/静态引用处统一检查，缺失时报 `E_ACCESS` 并带位置。
- 备注：无

#### [致命] S32-2｜`base` 表达式与 `:base(...)` 构造初始化器未实现
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（`base` 不作关键字处理，`AST_THIS` 仅在 `:321` 为 `this` 产生）；`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:30-130`（`parser_parse_function_declaration` 在 `)` 后直接要求 `{`，不识别 `:base(...)`）
- 类别：1 静默失败 + 4 阶段不对齐
- 触发条件：派生类方法体内使用 `base.成员`，或构造函数写 `:base(args)`。
- 后果：实证 `class Child:Parent{ public int32 Total(){ return base.value + base.Add(2); } }` → 种子报 `t35.krt(2,64): error: Expected right operand of binary operator`，stage2 同代码 exit=36（正确）；`class B:A{ public function B(int32 v):base(v){...} }` → 种子报 `Undefined identifier: v`，stage2 编译并正确调用基类构造（`b.x==5`）。继承语义在种子编译器上不可表达。
- 最小修复：新增 `TOKEN_BASE` 并在表达式解析中产生 `AST_BASE`；`parser_parse_function_declaration` 在 `)` 后接受 `TOKEN_COLON` + `base(...)`，存入 `function_decl` 的基调用列表，由 IrGen 在方法体前发出基构造调用。
- 备注：无

#### [致命] S32-3｜构造/析构/属性 AST 节点类型从不产生，IrGen 对应实现整体为死代码，基类构造链缺失
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2941-2944`（`AST_ACCESS_MODIFIER`）、`:2946-2990`（`AST_CONSTRUCTOR_DECLARATION`）、`:2991-3017`（`AST_DESTRUCTOR_DECLARATION`）、`:3072-3130` 与 `:3145-3203`（`AST_PROPERTY_DECLARATION`）；生产者缺失见 `Re.KrtC/src/Compiler/Frontend/Parser/` 全目录（四类节点零次创建）
- 类别：5 死代码 + 3 语义分叉 + 1 静默失败
- 触发条件：任何类声明。
- 后果：构造函数实际由 `IrGen.c:3036-3044` 的 `is_ctor`（与类同名）名称比较处理，该路径无任何基类构造调用代码；`has_base_call` 全项目只被读、从未写为 1。实证 `class A{...A(int32 v){this.x=v;}} class B:A{...B(int32 v){this.y=v+1;}} ... return b.x;` → 编译成功 exit=0（基类构造未执行，`b.x` 为 0，应为 5）。继承场景下基类字段保持未初始化且无诊断；约 120 行构造/析构/属性代码永不执行。
- 最小修复：删除 `IrGen.c:2941-3017`、`:3072-3130`、`:3136-3203` 中不可达分支，把构造函数统一到 `:3020-3068` 的 `is_ctor` 路径并补齐基类构造调用（对齐 `SelfHost/Compiler/Frontend/Parser/Parser.krt:993-1010` 的 `KrtParserBaseConstructor` 语义）。
- 备注：无

#### [高] S32-4｜种子编译器无法编译本语言自带的示例与验收测试源码
- 位置：`Test/SelfHost/examples/language-project/src/Main.krt:7`（`public EmptyInput():base("empty input", 1) {}`）；`Test/SelfHost/test_inheritance.py:34,38,54,61,69-70`
- 类别：12 测试与断言不足 + 4 阶段不对齐
- 触发条件：用种子编译器编译依赖 `base`/构造初始化器的自带源码或跑对应验收测试。
- 后果：实证 `KrtC Test/SelfHost/examples/language-project/src/Main.krt -o m.bin` → 2 个语法错误、`BUILD FAILED`；`SELFHOST_COMPILER=build/Re.KrtC/KrtC python3 -m unittest ...NativeInheritanceTests.test_base_prefix_constructor_chain_and_upcasts` → `RuntimeError: Compiler produced no KRO`、FAILED。`Bootstrap.NATIVE_DRIVER_MODULES` 中 `test_inheritance` 在种子路径必然失败，发布的 example 项目不可构建。
- 最小修复：先实现 S32-2/S32-3，或在 Bootstrap 中把依赖这些特性的模块从种子验收集移除并显式标注，避免「绿灯假象」。
- 备注：无

#### [高] S32-5｜auto-property 代码两份完整重复，且所在分支整体不可达
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:3072-3130`（第一份）与 `:3145-3203`（第二份，逐字重复）；不可达原因：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:1188-1265` 的 `parser_parse_block` 只返回 `AST_BLOCK` 或 `NULL`，故 `IrGen.c:3135` 的 `else`（`body->type != AST_BLOCK`）永不进入
- 类别：3 语义分叉 + 5 死代码
- 触发条件：任何类声明（用于触发 `body->type` 判断）。
- 后果：同一属性 getter/setter 生成逻辑存在两份，未来修改必然只改一份；`IrGen.c:3141-3142` 的空 `AST_FUNCTION_DECLARATION` 分支是永远为空的陷阱（一旦 `else` 变可达即静默丢函数）。
- 最小修复：删除 `IrGen.c:3135-3205` 整个 `else` 分支；属性生成只保留一份并改为由 `AST_PROPERTY_DECLARATION` 真正驱动（当前该节点从不产生，属性功能实际缺失）。
- 备注：无

#### [高] S32-6｜C 种子与自托管编译器的语言特性集不一致（阶段不对齐）
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:156-164`（访问修饰符）、`ParserExpression.c`（`base`）、`ParserStatement.c:30-130`（构造初始化器）；对照 `SelfHost/Compiler/Frontend/Parser/Parser.krt:993-1010`、`SelfHost/Compiler/Frontend/Semantic/Types.krt:180,1521`
- 类别：4 阶段不对齐 + 3 语义分叉 + 11 配置漂移
- 触发条件：同一份 `.krt` 源码分别用 `build/Re.KrtC/KrtC` 与 `build/selfhost/stage2/program` 编译。
- 后果：实证 `t34.krt`（private 字段外部读）种子 exit=0 / stage2 拒绝；`t35.krt`（`base.Add`）种子编译失败 / stage2 exit=36；`t33.krt`（`:base(v)`）种子编译失败 / stage2 exit=5。stage0→stage1 自举虽能跑（因 `SelfHost/*.krt` 未用这些语法），但「同一语言两种语义」，测试结果依赖所选编译器，无法据种子结果推断自托管行为。
- 最小修复：把访问控制、`base`、构造初始化器三项特性在 C 种子中补齐，或在 `Bootstrap`/CI 中显式记录「种子不支持特性清单」并对相关测试加 `skipUnless`，禁止无标注的语义分叉。
- 备注：无

#### [中] S32-7｜解析错误前缀缓冲区过小，长路径下行号列号被截断丢失
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserBase.c:57`（`char prefix[64];`）、`:59`（`snprintf(prefix, sizeof(prefix), "%s(%d,%d): ", ...)`）
- 类别：2 诊断缺失
- 触发条件：源文件路径长度使 `prefix` 超过 63 字符。
- 后果：实证编译 `Test/SelfHost/examples/language-project/src/Main.krt`（路径 88 字符）输出 `.../language-project/src/languagerror: Expected right operand...`——路径被从中间截断、`(行,列)` 完全消失；诊断无位置信息，用户无法定位。
- 最小修复：`prefix` 改为动态长度（两次 `snprintf` 取长度后 `KRT_MALLOC`），或至少扩大到 512 并单独打印 `source_name` 与 `line:col`。
- 备注：无

#### [中] S32-8｜arena 分配的内存在 `ParserAdvanced.c` 被当作堆指针 `KRT_FREE`，静默 no-op
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c:41`、`:50`（`KRT_PARSER_ALLOC_FROM_ARENA`）、`:72`（`KrtParserStrdup`）分配；`:54`、`:85`、`:104`、`:120`、`:135`（`KRT_FREE(template_params[i])`）释放；`Re.KrtC/src/Core/Memory/Allocator.c:276-290`（`KrtSafeFree` 对未跟踪指针仅调 `KrtMemoryReportError`）、`:425-437`（该函数为空操作）
- 类别：10 资源与生命周期 + 1 静默失败
- 触发条件：泛型类声明解析失败（如 `class A<T> {` 后遇 EOF/语法错误）走错误返回路径。
- 后果：释放静默失败（既不崩溃也不报错），错误路径的清理形同虚设；若他日 `KrtMemoryReportError` 改为 `exit`，这些路径会立即崩溃。
- 最小修复：arena 内存不得 `KRT_FREE`——删除这些 `KRT_FREE(template_params[i])`（arena 生命周期由 parser 持有），或提供 `KrtArenaFree` 并统一使用。
- 备注：无

#### [致命] S33-1｜`KrtIrGenerateFromAst` 后三个循环与尾部统计循环无 NULL 守卫 → SIGSEGV
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:3563-3564`、`:3574-3575`、`:3594-3595`、`:3609-3610`
- 类别：8 未检查前置条件 + 1 静默失败 + 错误处理不一致
- 触发条件：解析器在错误恢复/部分失败时向 `statements` 写入 NULL 槽（`parser_parse_block` 的 `statements[i] = NULL` 路径），或 `statement_count > 0` 而 `statements == NULL`。
- 后果：四处循环直接 `ast->data.block.statements[i]` 后立即 `stmt->type`，无 `if (!stmt) continue;`，也无 `statements ? ... : NULL` 数组守卫。对照同函数 `:3549-3552`（pass1）、`:3560-3563`（pass2）与 `krt_ir_generate_block :3490-3492` 均有守卫——同一份代码四种写法。后果 SIGSEGV，编译器直接崩溃、无诊断，崩溃点取决于哪趟循环先撞到 NULL。
- 最小修复：在 `:3564`、`:3575`、`:3595`、`:3610` 四处循环体首行加 `if (!stmt) continue;`（尾部两处为 `if (!s) continue;`），并把 `statements` 的 NULL 守卫统一为循环前置条件，全函数只保留一种写法。
- 备注：无

#### [高] S33-2｜入口函数名集合三处实现、两种取值（mangle 不一致）
- 位置：`IrGen.c:2048`、`:2845-2847`、`:3515-3517`、`:959`
- 类别：3 语义分叉 + 4 阶段不对齐
- 触发条件：源中出现名为 `_start` 或 `_KrtMainEntry` 的函数（或模块入口不是 `main`），且存在对该函数的取地址/引用。
- 后果：`:2048`（`irgen_address` 函数引用路径）只对 `"main"` 跳过 mangle；`:2845-2847`（定义路径）与 `:3515-3517`（声明路径）对 `"main" || "_start" || "_KrtMainEntry"` 跳过；`:959`（`krt_ir_ensure_main_entry`）只匹配 `"main"`。引用点生成 mangled 名、定义点注册裸名 → 链接期未解析符号；`krt_ir_ensure_main_entry` 在入口为 `_start` 时找不到 `main_function`，模块入口指针为 NULL。
- 最小修复：提取 `static bool krt_ir_is_entry_name(const char* n)`（含 main/_start/_KrtMainEntry 三值），`:2048`、`:2845`、`:3515`、`:959` 四处统一调用，`krt_ir_ensure_main_entry` 同步使用。
- 备注：需验证——KRT 源是否允许用户声明 `_start`/`_KrtMainEntry`（若不允许，`:2845`/`:3515` 的两个额外分支为不可达死代码）。

#### [高] S33-3｜`krt_ir_check_has_return` 不识别 switch/try
- 位置：`IrGen.c:3450-3477`（switch 覆盖 `:3455-3476`；switch 与 try 语句均落入 `default: return 0;` `:3475-3476`）
- 类别：6 契约破坏 + 12 测试与断言不足
- 触发条件：函数体以 `switch`（含 default 且所有 case 都 return）或 `try/finally` 结尾。
- 后果：该判定器只识别 RETURN/BLOCK/POINT_BLOCK/IF/WHILE/DO/FOR/FOREACH，`AST_SWITCH_STATEMENT`、`AST_TRY_STATEMENT`、`AST_CASE_CLAUSE` 全落入 `default: return 0`，于是 `:2894` 与 `:3621` 追加一条不可达的 `KrtIrReturn(ZERO)`。当前因 S29-2（switch 分派失效）这条追加 return 反而成为唯一可达出口，把 switch 分派 bug 掩盖成「碰巧能跑」；一旦 switch 修复，同一条语句变成真实死代码。
- 最小修复：补 `AST_SWITCH_STATEMENT`（要求存在 default 且所有 case 子句都 return）与 `AST_TRY_STATEMENT`（try 体与 finally 体均 return）的递归分支。
- 备注：无

#### [中] S33-4｜栈上伪造 AST 节点传入生成链，靠「不缓存指针」未文档化约定保命（疑似）
- 位置：`IrGen.c:3322-3344`（ref binding 伪造节点）、`:3349-3363`（`AST_ARRAY_COMPOUND_ASSIGNMENT` 同构）
- 类别：6 契约破坏 + 资源与生命周期（疑似）
- 触发条件：任何 `is_ref_binding` 的赋值/复合赋值，或 `a[i] += x` 形式。
- 后果：在栈上伪造 `ASTNode target/address/store`，把 `&target`、`&address` 塞进 `address_of.operand` / `pointer_assignment.pointer`，再调 `krt_ir_generate_expression(builder, &store)`，靠被调方「不缓存指针」这一未文档化约定保命。若下游任一路径把节点指针存入 builder（延迟表达式、`KrtIrVarTypePush` 保存节点指针、诊断回调复制节点），即悬垂栈指针 → UB；当前未爆是因为下游恰好即时取值。
- 最小修复：改为从 `stmt` 直接派生（让 `irgen_expr_address_of`/`irgen_expr_pointer_assignment` 接受 `ASTNode*` 并即时取值），或把伪造节点改为 `KRT_IRGEN_ALLOC` 堆分配并在生成后释放。
- 备注：疑似，需验证——通读 `krt_ir_generate_expression` → `irgen_expr_pointer_assignment` → `irgen_expr_address_of` 全链，确认无节点指针缓存/延迟解引用。

#### [中] S33-5｜四趟发射顺序为隐式契约，无断言
- 位置：`IrGen.c:3547-3584`（pass1 静态变量 `:3549-3557`；pass2 嵌套 block/namespace `:3559-3571`；pass3 函数/类 `:3573-3583`；pass4 顶层语句 `:3593-3603`）
- 类别：6 契约破坏
- 触发条件：后续维护者调整循环顺序，或在 pass2 的嵌套 block 内引用 pass3 才声明的函数。
- 后果：顺序是隐式契约——pass3 先创建全部函数/类，pass4 才发射顶层语句，因此顶层语句可调用后声明的函数；但该顺序只靠代码排列维持，无注释、无 `KRT_ASSERT`（而 `KRT_ASSERT` 本身是 `((void)0)`）。一旦顺序被调整，函数尚未声明即被引用 → 符号解析失败或静默回退到未 mangle 名，且无断言保护。
- 最小修复：在 `KrtIrGenerateFromAst` 头写明四趟顺序契约并加阶段断言（如 `KRT_ASSERT(builder->module->functions)`，需换成有效断言）；或改为单趟 `irgen_collect_declarations` 先收集、再发射。
- 备注：无

#### [致命] S34-1｜`ir_create_inst` 不检查块是否已有终结指令（S29-2 的 IR 层根因）
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/Ir.c:855-866`（`KrtIrJump`）、`:867-881`（`KrtIrBranch`）、`:882-894`（`KrtIrReturn`）、`:315-358`（`ir_create_inst`）
- 类别：6 契约破坏 + 8 未检查前置条件
- 触发条件：任何在同一 `current_block` 上连续调用两次终结 API。已实证：`IrGen.c:2596-2604` 的 switch 分派连续 append 多条 `KrtIrBranch`（S29-2）。
- 后果：`ir_create_inst` 只把指令追加到 `block->insts[]` 尾部，从不检查 `block->last_inst` 是否已是终结指令；三个终结 API 也无条件下发。对照 `KrtIrBlockAddSucc`/`AddPred` 会维护前驱后继，说明 IR 层知道「块唯一终结」不变量但未强制。后果：一个基本块出现多条互相矛盾的终结指令，块语义不确定；后端按首条还是末条解释取决于实现 → switch 分派失效（S29-2）。
- 最小修复：在 `ir_create_inst` 中对 `KRT_IR_JUMP/KRT_IR_BRANCH/KRT_IR_RETURN` 加断言或报错（`KRT_IR_ASSERT` 目前是空操作，需换成 `KRT_COMPILE_ERROR`），或让终结 API 拒绝在已有终结指令的块上再次下发。
- 备注：无

#### [高] S34-2｜`KrtIrPrint` 静默丢弃浮点指令
- 位置：`Ir.c:1753`（守卫 `if (inst->opcode >= 0 && inst->opcode <= KRT_IR_NOP)`）、`Ir.c:1629-1676`（`ir_opcode_names[]` 共 47 项）、`Ir.h:262-273`（`KRT_IR_NOP`=37，`KRT_IR_FADD`=38 … `KRT_IR_FGE`=46）
- 类别：2 诊断缺失 + 注释/实现脱节
- 触发条件：`-target ir` 打印含浮点指令的 IR。
- 后果：守卫上界用 `KRT_IR_NOP`，把所有浮点指令（FADD/FSUB/FMUL/FDIV/FEQ/FNE/FLT/FLE/FGT/FGE）排除在打印之外，静默跳过、无提示。实证（`float64 a=1.5; float64 b=2.5; float64 c=a+b; if(c>3.0){return 1;} return 0;`）IR 文本中 `c = copy t3`（t3 从未定义，FADD 被丢弃）、`t7 = branch t6`（t6 从未定义，FGE 被丢弃）。`-target ir` 产出的 IR 不完整且引用未定义临时量，基于该文本的诊断、回归对比、教学验证全部失真。
- 最小修复：守卫改为 `inst->opcode >= KRT_IR_LOAD && inst->opcode < (int)(sizeof(ir_opcode_names)/sizeof(ir_opcode_names[0]))`，越界时打印 `unknown(%d)` 而非跳过。
- 备注：无

#### [高] S34-3｜IR 指令合法性判据漏掉浮点码
- 位置：`Ir.h:583-585`（`KrtIrInstIsValid`）、`Ir.h:81-87`（`KRT_IR_ASSERT_VALID_INST`）
- 类别：6 契约破坏 + 5 死代码
- 触发条件：DEBUG 构建下（`KRT_IR_ASSERT_ENABLED` 默认为 1，`Ir.h:26-30`）任何使用 `KRT_IR_ASSERT_VALID_INST` 的校验路径。
- 后果：判据 `inst->opcode >= KRT_IR_LOAD && inst->opcode <= KRT_IR_NOP` 把 9 个浮点 opcode 判为非法；`KrtIrInstIsValid` 全项目无调用点（死代码），但一旦有代码使用该宏即误报。契约与枚举定义不一致，浮点 IR 在任何校验路径上都是「非法指令」。
- 最小修复：上界统一改为 `KRT_IR_FGE`；同时给 `KrtIrInstIsValid` 加调用点或删除。
- 备注：无

#### [高] S34-4｜syscall 参数被静默截断
- 位置：`Ir.c:955-973`
- 类别：1 静默失败 + 8 未检查前置条件
- 触发条件：`IrGen.c:1706`、`:1764`、`:1799` 的 `KrtIrSyscall(builder, args[0], args + 1, argument_count - 1)`，当调用参数 ≥ 7 个时第 7 个起消失；或 `arg_count > 0 && args == NULL`。
- 后果：`for (int i = 0; i < arg_count && i < 6; i++)` 超过 6 个的实参被静默丢弃；且未检查 `args` 是否为 NULL（空指针解引用）。系统调用参数静默丢失，行为与源码不符，无诊断。
- 最小修复：入口加 `if (arg_count > 0 && !args) return result;`，并在 `arg_count > 6` 时 `KRT_COMPILE_ERROR("syscall takes at most 6 arguments")`。
- 备注：无

#### [中] S34-5｜`KrtIrAlloc` 无 arena/extensions 守卫
- 位置：`Ir.c:560-580`（`:565` 直接 `(KrtIRVarTable*)builder->extensions` 后 `table->buckets[idx]`）
- 类别：8 未检查前置条件 + 1 静默失败
- 触发条件：builder 未正常初始化（`KrtIrBuilderCreate` 之外构造），或 arena 耗尽。
- 后果：只检查 `builder` 和 `name`；`builder->extensions` 为 NULL 时空指针解引用；`KrtIrArenaStrdup` 返回 NULL 时静默插入 `name == NULL` 的表项，`KrtIrVarTypePush :1279` 的 `strcmp(history->name, key)` 随后解引用 NULL。后果 SIGSEGV 或静默坏表。
- 最小修复：入口加 `if (!builder || !name || !builder->arena || !builder->extensions) return;`；`entry->name` 判空后再插入。
- 备注：【修订 G1】`KrtIrArenaStrdup` 失败返回 NULL 而非 exit(1)，故「arena 耗尽 → NULL 传播」是活路径，OOM 时该崩溃/坏表真实可达。

#### [中] S34-6｜`KrtIrLoad` 把错误吞成常量
- 位置：`Ir.c:538-558`（`:543-547` 返回 `IMM 0`；`:556` 返回 `value_type = TOKEN_EOF` 的 VAR）
- 类别：1 静默失败 + 2 诊断缺失
- 触发条件：`builder`/`name` 为 NULL，或符号未在 var 表/global 中注册（上游检查被绕过，如新增语法路径、IR 层直调）。
- 后果：`builder`/`name` 为 NULL 时返回整数 0 常量；符号未注册时返回无类型 VAR，无任何诊断；`:555` `KrtIrArenaStrdup(builder->arena, name)` 未检查 arena。一旦上游检查被绕过，错误静默变成常量 0 → 计算结果错误且无法定位。已核查：语义分析层会拦截 KRT 源的未定义变量（前批实证），故当前不可从正常源码触发；但 `KrtIrLoad` 是公共 IR API，被 `KrtIrVar`（`:983-991`）等多处复用。
- 最小修复：`builder`/`name` 为空时 `KRT_COMPILE_ERROR`；符号未解析时同样报错而非返回 TOKEN_EOF。
- 备注：【修订 G1】`KrtIrArenaStrdup` 失败不再 exit，NULL 会静默进入表项/返回路径，OOM 场景从「不可达」变为活路径。

#### [中] S34-7｜`KrtIrStore` 自动补登记未声明变量
- 位置：`Ir.c:510-537`（`:531-536` `if (!KrtIrVarTypeFind(...)) KrtIrVarTypePush(...)`）
- 类别：1 静默失败
- 触发条件：任何名字首次被 store。
- 后果：未声明变量不报错，静默注册其类型；`inst->result.value_type == TOKEN_EOF` 且 `value.value_type == TOKEN_EOF` 时产生无类型 COPY 指令。拼写错误的变量名在 IR 层被静默接受为新建变量。
- 最小修复：store 前要求 `KrtIrVarTypeFind` 命中（参数/全局/已声明局部），否则 `KRT_COMPILE_ERROR`。
- 备注：无

#### [中] S34-8｜`KrtIrVarTypeTruncate` 非法 count 静默忽略
- 位置：`Ir.c:1364-1368`
- 类别：1 静默失败
- 触发条件：作用域回退时传入过期/错误的基线计数（异常路径、手工调用）。
- 后果：`count > builder->var_type_count` 或负数时直接 return，不报错、不断言；变量可见性静默泄漏到外层作用域，后续引用解析到错误类型/错误 IR 槽位。
- 最小修复：改为 `KRT_IR_ASSERT(count >= 0 && count <= builder->var_type_count)` 或 `KRT_COMPILE_ERROR`。
- 备注：无

#### [中] S34-9｜IR 文本字符串常量无转义
- 位置：`Ir.c:1730`（`fprintf(out, "str%d = \"%s\"\n", i, module->string_constants[i]);`）
- 类别：2 诊断缺失
- 触发条件：字符串常量含 `"`、`\n`、`\`，或 `string_constants[i]` 为 NULL。
- 后果：`%s` 直接嵌入引号内无转义，破坏 IR 文本格式；NULL 时向 `%s` 传 NULL。IR 文本不可靠，无法作为诊断/回归基线。
- 最小修复：改为输出长度 + 十六进制/转义序列，并对 NULL 加守卫。
- 备注：无

#### [中] S34-10｜global 表返回内部指针（潜在悬垂 + 重复符号）
- 位置：`Ir.c:1237-1258`（返回 `&builder->module->globals[idx]`，`:1249-1252` realloc 后固化下标）、`Ir.c:1370-1381`（`KrtIrModuleFindGlobal` 同样返回内部指针）
- 类别：7 共享可变状态 + 10 资源与生命周期
- 触发条件：持有返回的指针期间再次 `KrtIrModuleAddGlobal` 触发 realloc；或同名 global 重复入表。
- 后果：返回指向 `module->globals` 数组内部的指针，realloc 后悬垂；`AddGlobal` 不查重，同名 global 可重复入表。已核查：当前唯一调用点 `IrGen.c:2800-2803` 先用 `FindGlobal` 去重并立即使用，暂不可触发；后果为潜在悬垂 + 重复符号。
- 最小修复：`AddGlobal` 内部先查重（返回已有项）；对外返回 id/副本而非内部指针。
- 备注：无

#### [中] S34-11｜类布局的宽度与失败值约定
- 位置：`Ir.c:1505-1512`（同名 class 静默 return）、`:1554`（`int size = KrtTokenIntegerBits(field->type) > 64 ? 16 : 8;`）、`:1589-1603`（`KrtIrLayoutGetType` 对数组字段返回 TOKEN_EOF）、`:1616-1627`（`KrtIrLayoutGetSize` 未知类返回 0）
- 类别：1 静默失败 + 3 语义分叉 + 4 阶段不对齐
- 触发条件：任何 class 字段访问（`IrGen.c:1163`、`:1383`、`:2296` 用 offset；`:1201` 用 size 分配）。
- 后果：① 重复 class 定义静默丢弃第二份；② 所有 ≤64 位字段一律 8 字节开槽（int8/int16/int32/bool 同宽），与真实 ABI 布局不同；③ `GetType` 的「数组字段」与「字段不存在」都返回 TOKEN_EOF，不可区分；④ `GetSize` 的「空类」与「未知类」都返回 0，不可区分。字段偏移由该布局唯一决定，若与 KroCodegen/VM 后端的字段访问宽度假设不一致即产生读写错位；错误约定不可区分使「字段不存在」被静默当成「数组字段」。
- 最小修复：重复定义报错；`GetType`/`GetSize` 增加 `bool* found` 出参或返回带错误码的结构；字段宽度按真实类型大小（或明确注释这是 slot 布局并让后端同步）。
- 备注：需验证——KroCodegen/VM 后端是否按「8 字节槽」读取字段（若是，则②为有意设计，应写进注释并统一；若不是，则为高危偏移错位）。

#### [高] S34-12｜前驱/后继表扩容未判空即写入（G1：非死代码，OOM 时 NULL 解引用）
- 位置：`Ir.c:253-276`（`capacity *= 2` 后 `KRT_REALLOC` 未判空即写入）对照 `Ir.c:318-325`（判空返回 NULL）
- 类别：1 静默失败 + 8 未检查前置条件（【修订 G1】原判「与 KRT_* 失败即 exit 叠加时是死代码」不成立）
- 触发条件：IR 前驱/后继表扩容时 `KRT_REALLOC` 失败（OOM），或 `capacity` 接近 INT_MAX 时 `*= 2` 溢出为负/0。
- 后果：`KRT_REALLOC` 失败返回 NULL 而非 `exit(1)`（`Core/Memory/Allocator.c:159-211` 的 `KrtSafeMalloc`；`KrtCommon.h:77-81` 宏无守卫），未判空即写入 → NULL 解引用崩溃；`*= 2` 无溢出检查，容量接近 INT_MAX 时 realloc 0 字节。同一文件两种扩容写法本身也是维护陷阱。
- 最小修复：统一为 `ir_reserve` 辅助函数，判空 + 溢出检查（并改为 `KRT_COMPILE_ERROR` 上报，而非静默传播 NULL）。
- 备注：【修订 G1】严重度由「中」上调为「高」：分配失败是活路径，后果为崩溃。

#### [高] S35-1｜整个类型子系统无调用者（死代码）
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrType.c:1-588`（全文件）
- 类别：5 死代码
- 触发条件：无条件——该模块永远不被执行。
- 后果：`KrtIrTypePoolInit/Destroy`、`KrtIrTypeVoid/Integer/Int{bits}/Uint{bits}/Float32/Float64/Bool/Char/String/Any/Unknown/Pointer/Array/Function/Struct/FromToken`、`KrtIrTypeSize/Align/ToString/Equal/Compatible/IsInteger/IsUnsigned/IsFloat/IsNumeric/IsPointer/Pointee/BinaryResult/CompareResult/Promote/Common/CanAssign/CanCast/SupportsOp/DefaultValue` 逐一对 `Re.KrtC/src` 全量 grep，除 `IrType.c` 自身外零引用；`IrType.h` 仅被 `IrSsa.h:5` include，而 IrSsa 只用 `KrtIRType*` 结构体指针（`IrSsa.h:10`、`:25`），从不调用任何构造/判定函数。561 行死代码进入构建（`build/Re.KrtC/Makefile:1043-1065` 有 `IrType.o` 目标），其中 `KrtIrTypeEqual`、`KrtIrTypeBinaryResult`、`KrtIrTypeSupportsOp`、`KrtIrTypeFunction` 带真实缺陷（见 S35-2~S35-8），一旦将来有人「顺手接上」就会立即踩中。
- 最小修复：删除 `IrType.c`/`IrType.h`（保留 `IrSsa.h` 需要的结构体定义则拆出到独立头），或补齐调用点并修复下列缺陷。
- 备注：无

#### [中] S35-2｜`KrtIrTypeFunction` 参数表可空但计数非零
- 位置：`IrType.c:138-160`（`:150-156`：`if (param_count > 0 && params)` 才分配，否则 `params = NULL`，但 `type->data.function.param_count = param_count` 在 `:147` 已无条件写为传入值）
- 类别：8 未检查前置条件
- 触发条件：调用方传 `params = NULL` 或 `KRT_MALLOC` 分配失败。
- 后果：记录 `param_count > 0` 而 `params == NULL`；下游任何 `params[i]` 都是空指针偏移解引用（OOB 读）。
- 最小修复：分配失败/无 params 时把 `param_count` 一并归零，或返回 NULL。
- 备注：【修订 G1】`KRT_MALLOC` 失败返回 NULL 而非 `exit(1)`，故 OOM 时 `param_count > 0` + `params == NULL` 状态真实可达（本模块当前无调用者，属休眠缺陷）。

#### [高] S35-3｜`KrtIrTypeEqual` 对复合类型只比 kind
- 位置：`IrType.c:292-312`（`:310` `return true;` 是 FUNCTION/STRUCT/CLASS/ANY/UNKNOWN/... 的共同出口）
- 类别：3 语义分叉 + 8 未检查前置条件
- 触发条件：任何调用 `KrtIrTypeEqual` 的路径（当前无调用者，属休眠缺陷）。
- 后果：① 函数类型只比 `kind`，完全不比较 `param_count`/`params`/`ret` → `int32()` 与 `float64(float64)` 判定相等；结构体/类只比 kind → 任意两个类相等。② POINTER 分支 `KrtIrTypeEqual(a->pointee, b->pointee)` 无环检测，若 pointee 链成环（自指或互相指向）则无限递归 → 栈溢出。类型等价判定错误 → 重载解析/类型检查错乱；循环 pointee 直接崩溃。
- 最小修复：FUNCTION 比较参数个数与各参数类型及返回类型；STRUCT/CLASS 比较名字与字段；递归加深度上限或已访问集合。
- 备注：无

#### [中] S35-4｜`KrtIrTypeBinaryResult` 漏浮点比较码
- 位置：`IrType.c:374-406`（`:378` `if (op >= KRT_IR_LT && op <= KRT_IR_NE) return KrtIrTypeBool(pool);`）
- 类别：4 阶段不对齐 + 3 语义分叉
- 触发条件：对浮点操作数调用 `KrtIrTypeBinaryResult(..., KRT_IR_FEQ/FNE/FLE/FLT/FGT/FGE)`。
- 后果：布尔结果判定区间 `KRT_IR_LT..KRT_IR_NE` 不含浮点比较码（枚举中位于 `KRT_IR_NOP` 之后），这些 opcode 落到 `:382` 的数值分支，返回 FLOAT32/FLOAT64 而非 BOOL；比较表达式的类型被推成浮点，下游若按此类型分配寄存器/选择指令即出错。
- 最小修复：区间上界改为 `KRT_IR_FGE`，或显式列出 5 个浮点比较码。
- 备注：无

#### [中] S35-5｜`KrtIrTypeSupportsOp` 未列举的 opcode 一律返回 true
- 位置：`IrType.c:523-558`（`:556-557` `default: return true;`）
- 类别：1 静默失败
- 触发条件：任何对该函数的调用（当前无调用者）。
- 后果：JUMP/BRANCH/CALL/RETURN/PHI/SYSCALL/LOADPTR/STOREPTR/ARRAY_STORE/INT_TO_STRING/浮点运算/浮点比较等全部落入 `default`，被判为「支持」；新增 opcode 时不会被发现「未实现支持性判定」，静默放行。
- 最小修复：`default: return false;`，并显式列出支持的码。
- 备注：无

#### [中] S35-6｜两套并行的「可转换」规则
- 位置：`IrType.c:314-334`（`KrtIrTypeCompatible`）与 `:490-509`（`KrtIrTypeCanCast`），另 `:486-488` `KrtIrTypeCanAssign` 直接转发 `Compatible`
- 类别：3 语义分叉
- 触发条件：任何调用（当前无调用者）。
- 后果：`Compatible` 含 `dst == ANY` 与「任意指针互转」；`CanCast` 不含 ANY，但额外允许 pointer↔integer；三条 API 三套规则，且 `CanAssign == Compatible`。同一对类型在不同 API 下得到不同结论，语义分叉。
- 最小修复：合并为单一 `KrtIrTypeConvertible(src, dst, mode)`，mode 区分隐式/显式/赋值。
- 备注：无

#### [低] S35-7｜`KrtIrTypeDefaultValue` 的 switch 各分支等价
- 位置：`IrType.c:560-588`
- 类别：5 死代码 + 1 静默失败
- 触发条件：任何对该函数的调用（当前无调用者）。
- 后果：FLOAT32/FLOAT64、BOOL、POINTER/STRING、default 四个分支全部 `value.data.imm = 0;`，且 `value.value_type` 从未赋值（保持 0 = `TOKEN_EOF`）。switch 结构是纯噪音；返回值无类型信息。
- 最小修复：按 kind 设置 `value.value_type`；若默认值恒为 0，删除 switch。
- 备注：无

#### [中] S35-8｜`KrtIrTypeArray` 大小溢出
- 位置：`IrType.c:127-136`（`:131` `element->size * size`）
- 类别：8 未检查前置条件
- 触发条件：`element->size * size` 溢出，或 `size` 为负数。
- 后果：两个 int 相乘无溢出检查，`size` 为负也未拒绝；数组类型大小回绕，下游按错误大小分配/索引。
- 最小修复：`if (size < 0 || element->size > INT_MAX / size) return NULL;`
- 备注：无

#### [疑似（需验证）] 未编号｜`KrtIrTypeIsPointer` 与 `KrtIrTypePointee` 对 STRING 语义矛盾
- 位置：`IrType.c:354-360`（`KrtIrTypeIsPointer`）、`:362-370`（`KrtIrTypePointee`）
- 类别：3 语义分叉（疑似）
- 触发条件：字符串解引用/取址路径依赖 `Pointee` 时。
- 后果：`IsPointer` 把 `KRT_IR_TYPE_STRING` 也算指针，而 `Pointee` 对 STRING 返回 NULL；若存在依赖路径，字符串取 pointee 静默得到 NULL。
- 最小修复：确认后统一 STRING 的指针语义（要么不算指针，要么 `Pointee` 返回字符类型）。
- 备注：疑似，需验证——字符串解引用/取址路径是否依赖 `Pointee`。另需向作者确认 `IrType.c` 是「预留未接线」还是「应删除」。


#### [致命] S36-1｜死代码（整文件约 500 行零调用者）+ 语义分叉
- 位置：`IrMemory.c:160-852`（对象池 `pool_init`~`KrtIrPoolPrintStats`、惰性分配 `lazy_create_op`~`KrtIrLazyPrintStats`、性能统计 `KrtIrProfilerInit`~`KrtIrCountersPrint`）；声明于 `IrMemory.h:33-110`
- 类别：死代码 / 语义分叉
- 触发条件：任何编译流程。全库（排除 `IrMemory.c/h` 自身）grep `KrtIRPoolManager|KrtLazyAllocManager|KrtIRProfiler|KrtIRMemoryStats|KrtIRPerformanceCounters` 命中 0 次；池/惰性/统计全部 API 零外部调用者。
- 后果：① 项目 IR 节点分配实际只有 `Ir.c` 的 `ir_create_inst`→`KrtIrArenaAlloc` 一条路径，`IrMemory.c` 另有一套完整的「定长对象池 + free_list + block」分配器从未启用；arena 线性分配 vs 池化复用两套语义并存，未来接入即与 `Ir.c` 生命周期假设冲突。② 该文件 60% 以上代码（含全部诊断统计输出）不可达，`KrtIrPoolPrintStats` 的统计表永远打印不出来。
- 最小修复：删除 `IrMemory.c:160-852` 及 `IrMemory.h:33-110` 中全部池/惰性/统计 API；若保留对象池，则必须让 `Ir.c` 改走 `KrtIrPoolAlloc*` 并删除 arena 直配路径，只留一条分配语义。

#### [高] S36-2｜静默失败 + 资源与生命周期（`current_pool` 单调前移，旧池空间永久浪费）
- 位置：`IrMemory.c:87-101`（`:88` `while (size > pool->size - pool->used && pool->next)`；`:92-100` 新建池；`:101` `arena->current_pool = pool;`）
- 类别：静默失败 / 资源生命周期
- 触发条件：`current_pool` 剩余空间小于本次 `size` 时（`Ir.c:1436` 用 `KrtIrArenaCreate(0)` → `KRT_DEFAULT_POOL_SIZE = 4KB`，见 `IrMemory.h:7`，池很小，极易触发）。例如池 1 剩 100 字节时来一个 200 字节分配。
- 后果：池 1 的 100 字节永远不会被再用（无回绕、无复用、无统计、无告警）；4KB 默认池 + 37 处 `KrtIrArenaAlloc` 调用点 → 池数量随编译规模无界增长，内存放大；`KrtIrArenaReset`（`:125-135`）只清 `used`，不回收多余池。
- 最小修复：分配失败跳池后把 `current_pool` 重置回 `arena->pools` 重新扫描（或维护空闲池链）；至少对 `pool_count` 超阈值时告警。
- 备注：`:88` 的 `pool->size - pool->used` 是 `size_t` 减法，`used > size` 时下溢为巨大正数 → 条件恒假 → 静默跳过该池，当前不变量下无断言保护。

#### [高] S36-3｜静默失败（`pool_grow` 三条失败路径全部静默 `return`，`KrtIrPoolAlloc` 跟着静默返回 NULL）
- 位置：`IrMemory.c:172-193`（`:178-182` 溢出/对齐/乘法失败 → `return;`；`:183-186` arena 分配失败 → `return;`）、`IrMemory.c:219-247`（`:246` `return NULL;`）
- 类别：静默失败 / 诊断缺失
- 触发条件：`object_size > SIZE_MAX - sizeof(KrtPoolNode)`、`KrtSizeAlign`/`KrtSizeMultiply` 失败、arena 耗尽。
- 后果：池分配失败 → `KrtIrPoolAlloc` 返回 NULL → 调用方（若接入）直接解引用 → 空指针崩溃，崩溃点远离根因；或静默少分配导致 IR 缺失。全链无任何诊断。
- 最小修复：`pool_grow` 失败时 `KRT_COMPILE_ERROR("object pool grow failed: type=%s reason=...")`；`KrtIrPoolAlloc` 在 `:246` 前同样报错。
- 备注：【修订 G1】分配失败为活路径且零诊断（`KrtSafeMalloc` 失败返回 NULL，非 exit），本条结论不变、严重度维持高。

#### [高] S36-4｜未检查前置条件（`KrtIrPoolFree` 不做归属/对齐校验，可把任意指针挂入 free_list）
- 位置：`IrMemory.c:269-281`（`:277` `KrtPoolNode* node = (void*)((char*)obj - offsetof(KrtPoolNode, data));`）
- 类别：未检查前置条件 / 共享可变状态
- 触发条件：任何错误配对，如 `KrtIrPoolFreeInst(mgr, (KrtIRInst*)some_stack_obj)`、`KrtIrPoolFreeBlock` 与 `KrtIrPoolFreeInst` 混用、或对非本 pool 分配的指针对齐偏移。
- 后果：`obj` 未校验是否落在某个 block 内、是否按 `node_size` 对齐，直接 `(char*)obj - offsetof(...)` 得到假 node 写进 `free_list` → 后续 `KrtIrPoolAlloc` 返回该指针 → 类型混淆 / 堆破坏 / 静默数据损坏。
- 最小修复：`pool_grow` 时把每个 block 的 `[start,end)` 记入 `pool->blocks`（该字段当前恒为 NULL，见 S36-6），释放时校验 `obj` 落在某 block 内且 `(obj - start) % node_size == 0`。

#### [高] S36-5｜契约破坏 + 死代码（`KrtIrPoolManagerDestroy` 名为销毁，实为只丢指针；`blocks` 字段全程 NULL）
- 位置：`IrMemory.c:208-217`（仅置 `free_list=NULL`、`blocks=NULL`）、`IrMemory.c:165`（`pool->blocks = NULL`）、`IrMemory.h:59`（`KrtPoolNode* blocks;`）
- 类别：契约破坏 / 死代码
- 触发条件：调用 `KrtIrPoolManagerDestroy` 后再读 `manager->arena`，或任何读 `pool->blocks` 的代码。
- 后果：① `manager->arena` 未被清空，destroy 后 manager 处于「半失效」状态，复用即操作已失效状态；② `blocks` 字段从 `pool_init` 起恒为 NULL，全库无任何写入点（grep 只有 `IrMemory.c:165/215` 两处赋值）→ 死字段，且正是 S36-4 缺失的归属校验数据结构。
- 最小修复：`KrtIrPoolManagerDestroy` 增加 `manager->arena = NULL;`；删除 `blocks` 字段，或在 `pool_grow` 中真正维护它。

#### [中] S36-6｜静默失败 + 死代码（`KrtIrPoolDefrag`/`DefragAll` 是空操作）
- 位置：`IrMemory.c:332-355`（`:339-342` 计算 `free_count` 后 `if (free_count < 2) return;`，`:344` `(void)free_count;`）
- 类别：静默失败 / 死代码
- 触发条件：任何调用（当前全库零调用者，见 S36-1）。
- 后果：函数名与注释暗示「碎片整理」，实际计算完 `free_count` 后什么都不做，仅 `(void)free_count;` 消警。调用方若接入会误以为碎片已整理，而 `Frag%` 统计（`:371`）仍按未整理状态计算 → 诊断与实际不符。
- 最小修复：实现真正的整理（重建 block、合并空闲节点），或删除该 API 及 `IrMemory.h:88-89` 声明。

#### [中] S36-7｜未检查前置条件 + 整数溢出（统计计数器为 `int`，百分比计算可溢出为负）
- 位置：`IrMemory.c:357-386`（`:365` `int total_allocated = pool->alloc_count;`、`:371` `return (free_count * 100) / total_allocated;`、`:380` `int total = pool->hit_count + pool->miss_count;`、`:385` `(pool->hit_count * 100) / total`）、`IrMemory.h:60-63`（四个计数器均 `int`）
- 类别：未检查前置条件 / 静默失败
- 触发条件：大规模编译下 `free_count > INT_MAX/100`（约 2147 万）时 `free_count * 100` 溢出为负 → `KrtIrPoolFragmentationRate` 返回负百分比；`hit_count + miss_count` 亦可溢出。
- 后果：统计信息错误/为负，误导调优；整数除法还使 <1% 的比率恒显示 0%。
- 最小修复：计数器改 `uint64_t`；计算改 `(uint64_t)free_count * 100 / total_allocated`。

#### [中] S36-8｜未检查前置条件（`KrtIrArenaAlloc` 对 `arena->current_pool == NULL` 无守卫）
- 位置：`IrMemory.c:79-88`（`:80` 只查 `!arena || size == 0`；`:87` 取 `current_pool`；`:88` 直接 `pool->size`）
- 类别：未检查前置条件
- 触发条件：`current_pool` 为 NULL 时（`KrtIrArenaCreate` 失败时直接返回 NULL，不留下半初始化 arena，故当前不可达；但 `KrtIrArenaReset`（`:125-135`）在 `arena->pools == NULL` 时不会把 `current_pool` 置 NULL，反之若未来新增 `ArenaClose/Detach` 即触发）。
- 后果：`:88` 空指针解引用 → SIGSEGV。
- 最小修复：`if (!arena || !arena->current_pool || size == 0) return NULL;`

#### [中] S36-9｜契约破坏（`KrtIrArenaAlloc(arena, 0)` 返回 NULL，与「分配失败」不可区分）
- 位置：`IrMemory.c:79-82`（`size == 0` → NULL）对照 `IrMemory.c:110-123`（`KrtIrArenaStrdup` 用 `len + 1` 规避）
- 类别：契约破坏 / 诊断缺失
- 触发条件：任何直接 `KrtIrArenaAlloc(arena, 0)` 的调用。
- 后果：调用方无法区分「请求 0 字节（合法）」与「真 OOM」，易把正常空分配误判为内存耗尽而触发错误路径。`KrtIrArenaStrdup` 的 `len+1` 属正确绕行，但契约未文档化。
- 最小修复：在 `IrMemory.h:26` 声明处注释「size==0 返回 NULL 属正常语义」，或改为返回非 NULL 哨兵。

#### [中] S36-10｜契约破坏（`KrtLazyOp.target` 一个字段两种解释，无类型标记区分）
- 位置：`IrMemory.h:96-103`（`void* target;`）、`IrMemory.c:484-501`（`RecordAlloc` 存 `void**`）、`IrMemory.c:504-522`（`RecordCopy` 存 `void**`）、`IrMemory.c:525-543`（`RecordInit` 存 `void*`）、`IrMemory.c:545-588`（`lazy_execute_op` 按 `op->type` 分别当 `void**` / `void*` 解引用）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：调用方对 `KRT_LAZY_OP_INIT` 传入 `void**`（或反之），或 `RecordInit` 的 `target` 为指向指针的指针。
- 后果：`op->target` 被双重解引用语义混用，传错即静默写错地址（`memset` 到错误位置）或段错误；契约完全靠调用方自觉，无断言、无诊断。
- 最小修复：拆成 `void* target` + `void** slot` 两个字段，或改用 union 并在 DEBUG 下断言类型匹配。

#### [疑似] S36-X1｜`KrtIrLazyRecordInit` 的 `target` 类型与 Alloc/Copy 不一致
- 位置：`IrMemory.c:525-543`（`RecordInit` 存 `void*`）对照 `:484-501`/`:504-522`（Alloc/Copy 存 `void**`）；`IrMemory.h:96-103`（`KrtLazyOp.target` 声明为 `void*`）
- 类别：疑似 / 需验证
- 触发条件：是否存在某个调用点把 `&ptr` 传给 `RecordInit`（那样 `memset` 会清零指针本身而非指向的缓冲区）。
- 后果：若存在该用法则静默写错内存；当前全库零调用者，无法从代码推断意图。
- 最小修复：查设计文档或 git 历史确认该 API 预期用法后再定。
- 备注：**需验证**；本条与 S36-10 相关。

#### [疑似] S36-X2｜`KrtIrArenaAlloc` 的 `total_allocated` 溢出检查与 reset 的统计口径
- 位置：`IrMemory.c:84`（`size > SIZE_MAX - arena->total_allocated`）、`IrMemory.c:125-135`（`KrtIrArenaReset`）
- 类别：疑似 / 需验证
- 触发条件：`arena->total_allocated` 是跨池累计，而 `KrtIrArenaReset` 会清零；是否存在「reset 后 `total_allocated` 归零但 `pool_count` 不清零」导致统计口径不一致的调用序列。
- 后果：统计口径不一致（潜在），当前 `KrtIrArenaReset` 全库零调用者故不可达。
- 最小修复：**需验证** `KrtIrArenaReset` 的调用点。

#### [致命] S37-1｜配置漂移 + 语义分叉（三条后端对同一 IR 跑不同的优化管线）
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c:1296`（`// KrtIrSsaOptimize(func);  //禁用SSA优化,除了Airs_td以外的人不要打开它,SSA不稳定`）、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.c:1019`（`KrtIrSsaOptimize(func);`）、`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.c:220`（`KrtIrSsaOptimize(func);`）
- 类别：配置漂移 / 语义分叉
- 触发条件：同一份 `.krt` 源码分别用 Kro / X86 / Vm 后端编译。
- 后果：X86 与 Vm 在代码生成前会执行常量传播、复制传播、死代码删除（`IrSsa.c:1497-1508`），Kro 完全不执行。同一语义的程序在三条后端上得到不同的指令序列与不同的运行时行为（叠加 S37-2/S37-3 的有缺陷传播，等于三条后端三种语义）。注释还明确邀请「除了 Airs_td 以外的人不要打开它」——开关取值取决于开发者身份而非构建配置，属未文档化、未测试的配置漂移。
- 最小修复：把 SSA 优化与否提升为显式构建选项（`-O` 级别或 `--enable-ssa`），三条后端读同一配置；在修复 S37-2/S37-3 之前应统一关闭并在注释中记录原因与 issue 链接。

#### [高] S37-2｜阶段不对齐 + 静默失败（`ssa_eliminate_dead_code` 只压缩 `insts[]`，不动 `first_inst` 链，DCE 对 X86/Vm 完全无效）
- 位置：`IrSsa.c:1314-1358`（`:1354` `block->insts[new_count++] = inst;`、`:1357` `block->inst_count = new_count;`）；对照后端读取点 `VmCodegen.c:228`（`KrtIRInst* inst = block->first_inst;`）、`X86RegAlloc.c:17`、`KroCodegen.c:497`、`KroRegisterPacking.inc:210/412/464/517/526`
- 类别：阶段不对齐 / 契约破坏
- 触发条件：X86/Vm 后端调用 `KrtIrSsaOptimize` → `ssa_eliminate_dead_code`。
- 后果：`KrtIRBasicBlock` 同时维护 `insts[]` 数组与 `first_inst`/`last_inst` 链表两份指令表示（`Ir.h:289-300`）。DCE 只压实 `insts[]` 并减 `inst_count`，完全不修改链表。X86/Vm 走链表 → 被判定为「死」的指令仍全部发射，DCE 静默无效；而 `KroRegisterPacking.inc:267`（`block->insts[index]`）走数组 → 一旦 Kro 打开 S37-1 那行注释，`position`（`:265` 用 `block->inst_count` 累加）与 `insts[index]` 的对应关系被打破，读到已删除指令或越界。同一份 IR 两种遍历语义，且无一致性断言。
- 最小修复：DCE 删除指令时同步维护 `first_inst`/`last_inst` 链（或统一为单一表示，删除 `insts[]`）；在 `KrtIrSsaOptimize` 末尾加 `KRT_IR_ASSERT` 校验 `inst_count` 与链表长度一致。

#### [高] S37-3｜静默失败 / 语义错误（DCE 保留清单漏 `KRT_IR_CALL_INDIRECT`，间接调用被当死代码删除）
- 位置：`IrSsa.c:1346-1349`（保留条件只列 `RETURN/CALL/SYSCALL/BRANCH/JUMP/STORE/ALLOC/LOAD`）；产生点 `Ir.c:1016-1044`（`KrtIrCallIndirect` 返回 TEMP 结果）；调用点 `IrGen.c:1648`
- 类别：静默失败 / 未检查前置条件
- 触发条件：函数指针调用的返回值未被使用（如把调用当语句执行），且编译走 X86 或 Vm 后端。
- 后果：`KRT_IR_CALL_INDIRECT` 有副作用（调用外部代码）但不在保留清单里，其结果 temp 无人引用 → 被 DCE 删除 → 调用凭空消失，程序静默跳过函数指针调用。Kro 后端因 S37-1 未开 SSA 而侥幸躲过。
- 最小修复：保留清单加入 `KRT_IR_CALL_INDIRECT`（以及任何未来新增的 call 类 opcode）；更稳妥的做法是给 `KrtIROpcode` 增加「有副作用」属性表，DCE 查表而非手写枚举清单。

#### [高] S37-4｜死代码 + 阶段不对齐（SSA 构造入口零调用者，「SSA 优化」跑在非 SSA IR 上）
- 位置：`IrSsa.c:1048-1072`（`KrtIrSsaConstruct`）、`IrSsa.c:1291-1312`（`KrtIrSsaVerify`）；调用统计：`KrtIrSsaConstruct`/`KrtIrSsaVerify`/`KrtIrSsaBuilderCreate`/`KrtIrSsaInsertPhis`/`KrtIrComputeDominance`/`KrtIrSsaLowerPhis`/`KrtIrSsaReplaceVarUses`/`KrtIrSsaRemoveStoreInsts`/`KrtIrSsaCollectVars`/`KrtIrVarTableCreate`/`KrtIrVarNewVersion`/`KrtIrPhiCreate` 外部调用者均为 0
- 类别：死代码 / 阶段不对齐
- 触发条件：任何编译流程。
- 后果：① 项目里 IR 从未被转成 SSA（变量版本化、phi 插入、phi 下沉全部不执行），但 `KrtIrSsaOptimize` 的名字与注释宣称是「SSA 优化」，实际是在普通 IR 上跑 temp 级的常量/复制传播。② `KrtIrSsaVerify` 用「存在 STORE 即非 SSA」作判据（`:1305-1307`），从未被调用，因此 IR 是否满足 SSA 前置条件无人校验，`KrtIrSsaOptimize` 直接在未验证的 IR 上运行。
- 最小修复：要么接入 `KrtIrSsaConstruct` 并在 `KrtIrSsaOptimize` 前调用 `KrtIrSsaVerify`（失败即报错），要么把 `KrtIrSsaOptimize` 重命名为 `KrtIrLocalTempPropagate` 并删除全部死代码（约 700 行）。

#### [高] S37-5｜未检查前置条件（`KrtIrComputePostorder` 缓冲区越界 + 返回值导致调用方越界读）
- 位置：`IrSsa.c:345-424`（`:352-353` 按 `max_blocks` 分配 `blocks`/`visited`；`:366-370` `while (block) { blocks[block_count++] = block; ... }` 无上限；`:414` `return result_top;`；`:408-410` 只写 `i < max_blocks`）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：调用方传入的 `max_blocks` 小于 `func` 实际基本块数（唯一调用方 `KrtIrComputeDominance:487` 传的是 `count_blocks(func)`，当前恰好相等；但 `KrtIrComputePostorder` 是 `IrSsa.h` 公开 API）。
- 后果：`blocks[block_count++]` 与 `visited[current]` 越界写（堆破坏）；同时函数返回真实总数 `result_top`，调用方若按返回值遍历 `order[0..result_top-1]` 会读到未初始化/越界内存。返回值语义（「实际块数」还是「写入 order 的块数」）未文档化。
- 最小修复：`while (block && block_count < max_blocks)`；返回值改为实际写入 `order` 的数量，或返回 -1 表示 `max_blocks` 不足。

#### [高] S37-6｜未检查前置条件（`KrtIrSsaInsertPhisForVar` 定长栈数组无界 + `pred_idx == -1` 越界索引）
- 位置：`IrSsa.c:683`（`int worklist_queue[KRT_SSA_MAX_BLOCKS];` = 4096，`IrSsa.c:6`）、`IrSsa.c:695-700`（`worklist_queue[worklist_back++] = i;` 无上限）、`IrSsa.c:726-733`（`int pred_idx = -1; ... if (!has_phi[pred_idx] && !has_store[pred_idx])`）
- 类别：未检查前置条件
- 触发条件：① CFG 块数接近/超过 4096 时 worklist 溢出（栈缓冲区溢出）；② `target_block->preds[p]` 未被 `collect_blocks` 收进 `blocks[]`（前驱块从 entry 不可达，或 `collect_blocks` 因 `max_blocks` 截断）→ `pred_idx` 保持 -1 → `has_phi[-1]` / `has_store[-1]` 越界读并可能越界写 `worklist_queue[-1]`。
- 后果：栈溢出 / 越界写，静默内存破坏或崩溃。
- 最小修复：`worklist_queue` 改堆分配（`KrtIrArenaAlloc(arena, actual_count * sizeof(int))`）并加 `worklist_back < actual_count` 断言；`pred_idx < 0` 时 `continue`。

#### [中] S37-7｜未检查前置条件（`KrtIrVarTableCreate` 不校验 `bucket_count`）
- 位置：`IrSsa.c:19-39`（`:28-30` `bucket_count * sizeof(...)`；`:36` `hash % table->bucket_count`）
- 类别：未检查前置条件
- 触发条件：`bucket_count <= 0`。`KrtIrSsaBuilderCreate:228` 传常量 256 故当前不可达，但 `IrSsa.h:65` 是公开 API。
- 后果：`bucket_count == 0` → `:36` 除零（SIGFPE）；`bucket_count < 0` → `bucket_count * sizeof(ptr)` 溢出为巨大 `size_t` → 分配失败返回 NULL → `:36` 仍除零。
- 最小修复：`if (!arena || bucket_count <= 0) return NULL;`

#### [中] S37-8｜静默失败（`KrtIrVarNewVersion` strdup 失败后静默丢弃，版本号已递增）
- 位置：`IrSsa.c:81-105`（`:87` `int new_version = existing ? (existing->version + 1) : 0;`、`:95` `var->name = KrtIrArenaStrdup(...)` 未检查、`:103` `var_table_add`）
- 类别：静默失败 / 未检查前置条件
- 触发条件：arena 分配失败（`KrtIrArenaAlloc` 在对齐/溢出检查失败时返回 NULL，`IrMemory.c:84-86`，非 exit）。
- 后果：`var->name == NULL` → `var_table_add`（`:64-75`）因 `!var->name` 静默 `return`，但该 `KrtIRVarVersion` 已被 `var` 持有且 `new_version` 已消耗 → 后续同名变量取到旧版本或找不到，版本号出现空洞，SSA 重命名静默错乱。全链无诊断。
- 最小修复：`var->name` 为空时 `KRT_COMPILE_ERROR("var version name alloc failed")`。
- 备注：【修订 G1】分配失败为活路径且零诊断，本条结论不变。

#### [中] S37-9｜静默失败（`KrtIrVarVersionedName` 256 字节栈缓冲截断，可致不同变量同名）
- 位置：`IrSsa.c:112-120`（`:116` `char buffer[256];`、`:117` `snprintf(buffer, sizeof(buffer), "%s_%d", name, version);`）
- 类别：静默失败
- 触发条件：`strlen(name) + 数字 + 2 > 255`（变量名超过约 240 字符）。
- 后果：`snprintf` 静默截断 → 两个不同变量可能得到相同的版本名（`foo_1` 与 `fooXXXXX_1` 截断后相同）→ SSA 重命名把两个变量合并，静默数据错乱。无诊断。
- 最小修复：用 `KrtIrArenaAlloc` 按 `strlen(name) + 32` 动态分配，或截断时报错。

#### [中] S37-10｜契约破坏（`KrtIrVarFindVersion` 忽略 `block` 参数；`KrtIrSsaCollectVars` 的 `arena` 参数未使用）
- 位置：`IrSsa.c:107-110`（`(void)block; return var_table_find(table, name);`）、`IrSsa.c:276-278`（`(void)arena;`）
- 类别：契约破坏
- 触发条件：任何按「块内版本」语义调用 `KrtIrVarFindVersion` 的代码。
- 后果：函数签名宣称支持按块查找版本，实现只按名字查全局最新版本 → 调用方拿到跨块错误版本（静默）。`KrtIrSsaCollectVars` 要求 `arena` 却完全不用，属未文档化的契约冗余，且 `:316` `return list.names;` 把 `KRT_CALLOC` 的内存在 `IrSsa.c:751-753` 用 `KRT_FREE` 释放，与 arena 语义混用（当前配对正确，但两套分配器混用无注释）。
- 最小修复：实现块级查找（沿支配链回溯）或删除 `block` 参数；删除 `KrtIrSsaCollectVars` 的 `arena` 参数并注释内存所有权。

#### [中] S37-11｜静默失败 + 注释脱节（`lookup_var` 未定义变量返回 IMM 0）
- 位置：`IrSsa.c:763-790`（`:785-788` `val.type = KRT_IR_VALUE_IMM; val.data.imm = 0;`）
- 类别：静默失败
- 触发条件：SSA 重命名时遇到 `var_table` 里不存在的变量名（未声明变量、或前一步重命名漏登记）。
- 后果：未定义变量被静默替换为常量 0，且 `val.value_type` 未设置（默认 0 = `TOKEN_EOF`）→ 与 `Ir.c:538-558` `KrtIrLoad` 把未定义符号吞成 IMM 0 的既有行为叠加，错误被层层掩盖，最终在代码生成期变成错误数值而非编译错误。
- 最小修复：`KRT_COMPILE_ERROR("SSA rename: undefined variable '%s'", name)`。

#### [中] S37-12｜未检查前置条件 + 死代码（`ssa_merge_blocks` 合并后不摘链、不更新 `succ_capacity`，且调试打印错位）
- 位置：`IrSsa.c:1443-1495`（`:1460-1462` `block->succ_count = next_block->succ_count;` 直接写 `block->succs[i]` 但不校验 `block->succ_capacity`、不把 `next_block` 从 `func` 块链表摘除、不更新 `first_inst`/`last_inst`；`:1487-1494` `fprintf(stderr, "[LowerPhis] After: main block inst[%d] opcode=%d\n", ...)`）
- 类别：未检查前置条件 / 死代码
- 触发条件：仅 `KrtIrSsaFullOptimize:1510-1523` 调用，而 `KrtIrSsaFullOptimize` 外部零调用者 → 整段死代码。
- 后果：① 合并后 `next_block` 仍在 `func->entry_block` 链上，其 `preds` 仍指向 `block`，后续遍历会重复处理同一批指令；② `block->succs` 复用 `next_block->succs` 的计数但容量未同步，若 `next_block->succ_count > block->succ_capacity` 则越界写；③ `:1487` 的打印从 `KrtIrSsaLowerPhis`（`:1182-1290`）复制而来，与「合并块」无关，且无条件输出到 stderr（诊断噪音/注释脱节）。
- 最小修复：删除 `ssa_merge_blocks` 与 `KrtIrSsaFullOptimize`（无人调用），或实现完整的块摘链 + 容量检查 + 移除调试打印。

#### [中] S37-13｜未检查前置条件（`KrtIrPhiAddOperand` 不校验 `phi->blocks` 非空）
- 位置：`IrSsa.c:157-163`（`:158` 只校验 `!phi || index < 0 || index >= phi->pred_count`，随后 `:160` `phi->blocks[index] = block;`）；对照 `IrSsa.c:138-148`（`KrtIrPhiCreate` 在 arena 分配失败时把 `phi->blocks` 置 NULL 但 `phi->pred_count` 仍 > 0）
- 类别：未检查前置条件
- 触发条件：`KrtIrPhiCreate` 的 `KrtIrArenaAlloc` 返回 NULL（`IrMemory.c:84-86` 的对齐/溢出失败路径）。
- 后果：`phi->blocks[index] = block` 空指针解引用 → SIGSEGV。虽然概率低，但 `KrtIrPhiCreate` 自己用 `if (phi->blocks)` 保护了 memset（`:141-144`），说明作者认为该指针可能为空，而 `KrtIrPhiAddOperand` 未同步该假设 → 契约不一致。
- 最小修复：`if (!phi || !phi->blocks || !phi->values || index < 0 || index >= phi->pred_count) return;`
- 备注：【修订 G1】`KrtIrArenaAlloc` 失败返回 NULL 为活路径，本条触发条件成立、严重度维持中。

#### [中] S37-14｜契约破坏（`KrtIrBlockAddPhi` 未置 `phi->next = NULL`）
- 位置：`IrSsa.c:186-204`（`:197-202` 尾插时只改 `phi_list->tail->next = phi`，未清 `phi->next`）
- 类别：契约破坏
- 触发条件：复用一个已在别处链表中的 `KrtIRPhi`（例如从其他块迁移、或 `KrtIrPhiCreate` 后多次 `KrtIrBlockAddPhi` 到不同块）。
- 后果：`phi->next` 残留旧链表指针 → `KrtIrPhiFind`（`:166-184`）遍历 `phi_list->head` 时会越过尾节点读到别的块的 phi，比较 `var_name` 时可能命中错误 phi（静默错误），或走到野指针崩溃。
- 最小修复：`KrtIrBlockAddPhi` 中 `phi->next = NULL;`。

#### [中] S37-15｜语义分叉 / 静默失败（`ssa_constant_propagation` 只认 `KRT_IR_VALUE_IMM`，不认 `KRT_IR_VALUE_INTEGER`/`IMM_F`）
- 位置：`IrSsa.c:1404-1441`（`:1412` `inst->operands[0].type == KRT_IR_VALUE_IMM`、`:1413` `double const_val = inst->operands[0].data.imm;`、`:1430-1431` 写回 `type = KRT_IR_VALUE_IMM; data.imm = const_val`）；整数常量产生于 `Ir.c:406-412`（`KrtIrInteger` → `KRT_IR_VALUE_INTEGER` + `KrtUInt128`），浮点常量产生于 `Ir.c:601-608`（`KrtIrImmF` → `KRT_IR_VALUE_IMM_F`）
- 类别：语义分叉 / 静默失败
- 触发条件：X86/Vm 后端对 `int32 x = 5;` 这类整数常量拷贝做传播。
- 后果：① 所有 `KRT_IR_VALUE_INTEGER`（`KrtUInt128`，支持 128 位）与 `KRT_IR_VALUE_IMM_F` 的常量拷贝完全不会被传播，而 `KRT_IR_VALUE_IMM`（double）会被传播 → 同一「常量传播」语义在三种立即数类型上行为不一致（阶段不对齐）。② 更危险的是 `KRT_IR_VALUE_IMM` 在 `Ir.c:1103-1113`/`1127-1139` 里被当作偏移量/尺寸的小整数使用（`offset_val.data.imm = (double)offset`），`ssa_constant_propagation` 会把这种 double 位型的「整数」当成常量向下游传播，把整数值以 double 载体替换 `KRT_IR_VALUE_INTEGER` 操作数 → 与 `KrtIrInteger` 的 `KrtUInt128` 归一化语义分叉，128 位整数会被截断/精度丢失。
- 最小修复：传播前按 `inst->result.value_type` 分派，只对真正的浮点常量传播 `IMM_F`、只对整数常量传播 `INTEGER`，并保留 `value_type`；禁止把 `KRT_IR_VALUE_IMM` 用于整数偏移（在 `KrtIrLoadPtr`/`KrtIrStorePtrSized` 改用 `KrtIrInteger`）。

#### [中] S37-16｜静默失败 + 资源生命周期（`KrtIrVarTableDestroy` 是空操作，与 `KrtIrSsaBuilderDestroy` 的 `KRT_FREE` 混用两套分配器）
- 位置：`IrSsa.c:41-43`（`(void)table;`）、`IrSsa.c:234-251`（`KrtIrSsaBuilderDestroy` 中 `KRT_FREE(ssa_builder->current_var_stack); KRT_FREE(ssa_builder);`）
- 类别：静默失败 / 资源生命周期
- 触发条件：任何 `KrtIrSsaBuilderDestroy` 调用（当前零调用者）。
- 后果：`KrtIrVarTable` 及其 buckets 由 arena 分配（`:24-30`），`KrtIrSsaBuilder` 由 `KRT_CALLOC` 分配（`:217`），`current_var_stack` 由 `KRT_MALLOC`/类似分配。`KrtIrVarTableDestroy` 名为销毁实则空操作，实际依赖 arena 整体释放；但 `KrtIrSsaBuilderDestroy` 又对另外两个字段做 `KRT_FREE` —— 同一结构体内三种内存来源、两种销毁策略，无任何注释。若有人误对 `var_table` 调 `KRT_FREE` 即无效（指向 arena 内部），若误对 arena 内存二次 `KRT_FREE` 则堆破坏。
- 最小修复：统一为 arena 分配（`KrtIrSsaBuilder` 也走 arena），删除 `KRT_FREE`；或让 `KrtIrVarTableDestroy` 真正释放并文档化所有权。

#### [疑似] S37-X1｜`ssa_constant_propagation` 不跨块传播的保守性
- 位置：`IrSsa.c:1404-1441`
- 类别：疑似 / 需验证
- 触发条件：某 temp 定义在块 A、使用在块 B（不被传播）——这是保守而非错误。
- 后果：需验证是否存在「定义块 A 的 temp 在块 B 被使用，且传播后 `value_type` 与 `data.imm` 载体不匹配」的实际 IR 用例。
- 最小修复：**需验证**：用 `target ir` 导出含跨块 temp 的 IR，对比 X86 与 Kro 后端输出。

#### [疑似] S37-X2｜`worklist_queue` 溢出需 CFG 块数 > 4096
- 位置：`IrSsa.c:6`（`KRT_SSA_MAX_BLOCKS`）、`IrSsa.c:683`
- 类别：疑似 / 需验证
- 触发条件：`KRTIrSsaInsertPhisForVar` 的 `worklist_queue` 溢出需 CFG 块数 > 4096。
- 后果：需验证 `KRT_SSA_MAX_BLOCKS` 是否在其他地方被当作硬上限并提前拒绝。
- 最小修复：**需验证**：`grep -rn "KRT_SSA_MAX_BLOCKS"`。

#### [疑似] S37-X3｜`KrtIrSsaCollectVars` 中 `operands[j]` 是否可能为 NULL
- 位置：`IrSsa.c:253-274`（`add_var_name`）、`Ir.c:343-348`（`ir_create_inst`）、`Ir.c:1364-1368`（`KrtIrVarTypeTruncate`，前批 S34-8）
- 类别：疑似 / 需验证
- 触发条件：`inst->operands[j]` 在 `operand_count > 0` 时是否可能为 NULL。
- 后果：`ir_create_inst:343-348` 保证 `operands` 非空，但 `KrtIrVarTypeTruncate` 可能把 `operand_count` 改小而不重分配，方向是安全的。
- 最小修复：**需验证**：是否存在把 `operand_count` 增大而不重分配 `operands` 的路径。

#### [高（死代码内潜在）] S38-1｜`rename_block` 无访问标记递归 + 被 iterative 的栈溢出 fallback 调用 → 栈溢出
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrSsa.c:792-864`（递归点 :861-863）；fallback 调用点 `:1036-1038`
- 类别：未检查前置条件 / 死代码（两份实现）
- 触发条件：`rename_block_iterative` 的 `stack_top` 达到 `KRT_RENAME_STACK_MAX`（:791 = 4096）时，:1038 调用 `rename_block`。任何 CFG 嵌套深度 > 4096 的函数（编译器生成的 IR 块数通常远超此数）即触发。
- 后果：`rename_block` 无 `visited` 数组，对含环 CFG（循环）无限递归 → SIGSEGV；即使无环，剩余深度也会再次溢出。
- 最小修复：删除 `rename_block`（:792-864），把 iterative 的 fallback 改为 `break` 或返回错误码；若要保留递归版，必须加 `visited` 参数。
- 备注：【修订 G3】原文判「致命」；`KrtIrSsaOptimize`（:1497-1508）只调用 `ssa_constant_propagation`/`ssa_copy_propagation`/`ssa_eliminate_dead_code`，从不调用 `KrtIrSsaConstruct`/`ReplaceVarUses`/`RemoveStoreInsts`/`LowerPhis`，本条位于零调用者死代码内，严重度降为高（死代码内潜在）。

#### [高（死代码内潜在）] S38-2｜`rename_block_iterative` 的 `visited` 只标当前块，不标 phi 前驱回填 → phi 值静默丢失
- 位置：`IrSsa.c:876-885`（visited 数组）、`:1029-1041`（前驱回填）
- 类别：静默失败 / 契约破坏
- 触发条件：CFG 有环时，`visited[succ_idx]` 为 true 则 :1032-1034 `continue` 跳过该 succ 的 phi 前驱回填。
- 后果：回边前驱的 phi 操作数永远不被写入（保持 `KRT_IR_VALUE_VOID`），`KrtIrSsaLowerPhis:1206-1208` 因 `val->type == KRT_IR_VALUE_VOID` 直接 `continue` → 该前驱的 COPY 不生成 → phi 在该路径上无定义值。SSA 构造结果错误。
- 最小修复：phi 前驱回填必须在块首次访问时完成（标准 SSA 算法），或改为两趟：先全量回填 phi 值，再做重命名。
- 备注：【修订 G3】原文判「致命」；位于零调用者死代码内，严重度降为高（死代码内潜在）。

#### [中（死代码内潜在）] S38-3｜`rename_block`/`rename_block_iterative` 只维护 `insts[]` 数组，不维护 `first_inst`/`last_inst` 链 → 后端走链表看不到重命名
- 位置：`IrSsa.c:819-852`（数组遍历）、`:929-960`（数组遍历）
- 类别：阶段不对齐 / 契约破坏
- 触发条件：所有 SSA 重命名路径。
- 后果：`KrtIRBasicBlock` 双表示（`insts[]` + 链表）。重命名只改数组元素，链表里的 `inst` 指针虽相同（指向同一 `KrtIRInst`），但 `inst->result.data.name` 被改成版本名后，`KrtIrSsaLowerPhis:1247` 读 `phi->inst->result` 时可能读到已被 `KrtIrSsaReplaceVarUses` 改回的基名。后端（`VmCodegen.c:228`、`X86RegAlloc.c:17`）走链表，若链表顺序与数组不一致则结果不确定。
- 最小修复：统一表示，或文档化「链表与数组必须同步」，并在 `KrtIrSsaConstruct` 末尾断言两者一致。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

#### [中（死代码内潜在）] S38-4｜`rename_block` 的 phi 实现是伪造 `KrtIRInst`，从不加入块指令 → phi 定值无后端可见性
- 位置：`IrSsa.c:803-817`
- 类别：契约破坏 / 静默失败
- 触发条件：所有 phi 节点。
- 后果：`phi_inst` 是 arena 分配的 NOP 指令，`opcode = KRT_IR_NOP`，不加入 `block->insts[]` 也不加入链表。`KrtIrSsaLowerPhis:1247` 只读其 `result`，但 `KrtIrSsaReplaceVarUses:1124-1141` 会把 `result.data.name` 的版本后缀剥掉 → COPY 的 result 名与 operands 名相同 → 版本合并，SSA 破坏。
- 最小修复：phi 的 result 名不应被 `KrtIrSsaReplaceVarUses` 剥离（或该函数只处理 operands 不处理 result）。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

#### [中（死代码内潜在）] S38-5｜`KrtIrSsaLowerPhis` 的调试 `fprintf` 残留在生产代码，且只对 `main` 生效 → 配置漂移
- 位置：`IrSsa.c:1187-1193`
- 类别：死代码 / 配置漂移 / 注释脱节
- 触发条件：编译 `main` 函数时。
- 后果：每次编译 `main` 都向 stderr 打印 `[LowerPhis] Before: ...`，污染诊断输出；非 main 函数不打印，行为分叉。
- 最小修复：删除该 `fprintf` 块。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

#### [中（死代码内潜在）] S38-6｜`KrtIrSsaLowerPhis` 只改链表不保证数组一致（与 `KrtIrSsaRemoveStoreInsts` 同类）
- 位置：`IrSsa.c:1249-1285`
- 类别：阶段不对齐
- 触发条件：phi lowering 插入 COPY 时。
- 后果：:1249-1257 把 `move_inst` 插入 `pred->insts[]` 数组，:1258-1285 又把 `move_inst` 插入链表。但 `KrtIrSsaRemoveStoreInsts:1155-1180` 只压缩数组不动链表 → 若之前调用过 RemoveStoreInsts，数组与链表已经错位，此处插入后错位加剧。后端走链表，结果不确定。
- 最小修复：`KrtIrSsaRemoveStoreInsts` 同步维护链表，或废弃数组表示。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。本批「疑似但不确定」第 1 项（调用顺序）已由第三十八批重要修正解答：`KrtIrSsaOptimize` 从不调用本条涉及的任何函数。

#### [中（死代码内潜在）] S38-7｜`KrtIrSsaReplaceVarUses` 用 `strchr(name, '_')` 找第一个下划线 → 基名含下划线时语义破坏
- 位置：`IrSsa.c:1083-1087`、`:1124-1128`
- 类别：静默失败 / 语义分叉
- 触发条件：变量名含下划线（如 `my_var`），SSA 重命名后变为 `my_var_3`。
- 后果：`strchr` 找到第一个 `_`（`my_var_3` 中的 `my` 后），`base_len = 2`，`strncpy` 得到 `"my"`，`new_val.data.name = "my"` → 变量名被截断为 `my`，与原变量 `my_var` 不符。所有含下划线的变量在 SSA 后名字错误，可能与其他变量冲突。
- 最小修复：版本名格式改为不可能出现在标识符中的分隔符（如 `\x01`），或从末尾解析版本号。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

#### [中（死代码内潜在）] S38-8｜`KrtIrSsaConstruct` 不校验 `func->entry_block` 非 NULL → `actual_count` 可能为 0，静默返回
- 位置：`IrSsa.c:1048-1072`（:1065-1066 `KRT_CALLOC(actual_count, ...)`，:1071 调用 iterative）
- 类别：未检查前置条件 / 静默失败
- 触发条件：`func->entry_block == NULL`（如空函数体或构造失败）。
- 后果：`collect_blocks` 返回 0，`actual_count = 0`，`KRT_CALLOC(0, sizeof(bool))` 行为未定义；`rename_block_iterative` 里 `stack[0].block = NULL` → 空操作返回，SSA 构造静默失败。
- 最小修复：`KrtIrSsaConstruct` 入口断言 `func->entry_block != NULL`，否则返回错误码。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。【修订 G1】`KRT_CALLOC(0, ...)` 在 `size==0` 时返回 NULL 而非 exit，静默失败路径成立。本批「疑似但不确定」第 3 项（`collect_blocks` 对 `entry_block == NULL` 是否返回 0）与本条触发条件相关，仍需读 `IrSsa.c:230-260` 确认。

#### [中（死代码内潜在）] S38-9｜`rename_block_iterative` 的 `visited` 数组用 `actual_count` 分配，但 `block_to_index` 用 `max_blocks + 1024` 分配 → 索引不一致
- 位置：`IrSsa.c:1060-1063`（`block_to_index` 分配 `max_blocks + 1024`）、`:876`（`visited` 分配 `actual_count`）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：`collect_blocks` 返回的 `actual_count` 与 `count_blocks` 的 `max_blocks` 不一致（如 CFG 有不可达块）。
- 后果：`block_to_index` 的索引范围与 `visited` 数组大小不匹配，:1032 `visited[succ_idx]` 可能越界读/写。
- 最小修复：统一用 `actual_count` 分配所有数组，或在 `collect_blocks` 后断言 `actual_count <= max_blocks`。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

#### [中（死代码内潜在）] S38-10｜`rename_block_iterative` 的 `stack` 定长 4096，溢出时 fallback 到无 visited 的递归（见 S38-1）
- 位置：`IrSsa.c:791`（`KRT_RENAME_STACK_MAX 4096`）、`:878-885`（定长栈）、`:1036-1038`（fallback）
- 类别：未检查前置条件
- 触发条件：CFG 深度 > 4096。
- 后果：见 S38-1（栈溢出 / 无限递归）。
- 最小修复：fallback 改为返回错误码，或把栈改为动态分配。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

#### [中（死代码内潜在）] S38-11｜`KrtIrSsaLowerPhis` 扩容 `pred->insts` 用 `KRT_REALLOC`，但 `move_inst` 用 `KrtIrArenaAlloc` → 混合分配器 + 失败静默
- 位置：`IrSsa.c:1219-1227`（KRT_REALLOC）、`:1229-1247`（KrtIrArenaAlloc）
- 类别：资源与生命周期 / 静默失败
- 触发条件：`KrtIrArenaAlloc` 返回 NULL（arena 耗尽）。
- 后果：:1234-1236 `if (!move_inst) continue;` / :1240-1242 `if (!move_inst->operands) continue;` 静默跳过该 phi 操作数 → phi 在该前驱无 COPY → 该路径 phi 无定义值。SSA 破坏。
- 最小修复：arena 分配失败应返回错误码（`KrtIrSsaLowerPhis` 当前返回 void，需改为 int）。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。【修订 G1】arena 分配失败返回 NULL 为活路径且零诊断，`if (!move_inst) continue;` 属静默失败，结论成立。本批「疑似但不确定」第 2 项撤回：`KrtIRInst` 无 `prev` 字段（`Ir.h:281-289`），`prev` 疑似项不成立。

#### [中（死代码内潜在）] S38-12｜`rename_block` 与 `rename_block_iterative` 的 phi/ALLOC/STORE/COPY 处理代码几乎逐行重复
- 位置：`IrSsa.c:803-836` vs `:897-960`
- 类别：语义分叉
- 触发条件：任何维护时。
- 后果：两份实现已出现差异（iterative 多了 COPY 处理 :922-928，recursive 没有）→ 结果不一致。
- 最小修复：删除 `rename_block`，只保留 iterative 一份。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

#### [中（死代码内潜在）] S38-13｜`rename_block` 的 STORE 处理（:828-836）与通用操作数循环（:849-852）逻辑分散在三处
- 位置：`IrSsa.c:828-836`（STORE 特判）、`:849-852`（通用循环跳过 j==0）
- 类别：语义分叉
- 触发条件：STORE/ALLOC 指令。
- 后果：改写规则分散，易漏改。
- 最小修复：合并到唯一改写点。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

#### [中（死代码内潜在）] S38-14｜`KrtIrSsaRemoveStoreInsts` 只压缩数组不动链表（同 S38-6）
- 位置：`IrSsa.c:1155-1180`
- 类别：阶段不对齐
- 触发条件：调用 RemoveStoreInsts 后。
- 后果：链表仍含 STORE，后端仍执行 STORE → SSA lowering 后 STORE 未被移除。
- 最小修复：同步维护链表。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

#### [中（死代码内潜在）] S38-15｜`KrtIrSsaReplaceVarUses` 的 `is_version` 检查只验证后缀全为数字，不验证版本号范围
- 位置：`IrSsa.c:1090-1105`
- 类别：静默失败
- 触发条件：变量名形如 `foo_999999999999999999999`（超长数字）。
- 后果：`strncpy` 后 `base_name` 正确，但版本号溢出（如果后续有解析）。当前只改名不解析，影响有限。
- 最小修复：限制版本号长度。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

#### [高] S39-1｜`fold_binary_op` 移位操作未检查移位量 → 未定义行为
- 位置：`IrOptimizer.c:129-130`
- 类别：未检查前置条件
- 触发条件：`KRT_IR_LSHIFT`/`KRT_IR_RSHIFT` 的 `right` 操作数 >= 64 或为负。
- 后果：`(int64_t)left << (int)right` 在 C 标准中是未定义行为（移位量 >= 类型宽度）。x86 上实际取模 64，结果与源码语义不符。
- 最小修复：`if (right < 0 || right >= 64) return 0;`（或按语言语义报错）。

#### [高] S39-2｜`fold_binary_op` 把 `double` 强转 `int64_t` → 精度丢失
- 位置：`IrOptimizer.c:126-131`
- 类别：静默失败 / 阶段不对齐
- 触发条件：对 `KRT_IR_AND/OR/XOR/LSHIFT/RSHIFT` 的 double 操作数进行位运算。
- 后果：double 不能精确表示所有 int64（>2^53 丢失低位），位运算结果错误。
- 最小修复：整数运算应走 `KrtIrFoldInteger`（:184 路径），不应进入 double 折叠。

#### [高] S39-3｜`fold_binary_op` 的除零/取模保护是死代码，且与 `fold_constant_binary` 矛盾
- 位置：`IrOptimizer.c:122-125`（`fold_binary_op` 返回 0）、`:168-171`（`fold_constant_binary` 返回 false）
- 类别：死代码 / 语义分叉
- 触发条件：任何除零/模零。
- 后果：`fold_binary_op` 的 `right != 0 ? ... : 0` 永不被执行（调用方已拦截）；若未来有新调用方直接调 `fold_binary_op`，会得到静默的 0 而非错误。
- 最小修复：删除 `fold_binary_op` 中的除零分支，统一由 `fold_constant_binary` 处理。

#### [高] S39-4｜整数折叠把结果塞进 `KRT_IR_IMM` opcode → opcode 与 operand type 不匹配
- 位置：`IrOptimizer.c:184-190`
- 类别：阶段不对齐 / 契约破坏
- 触发条件：`KrtIrFoldInteger` 成功时。
- 后果：`inst->opcode = KRT_IR_IMM`（语义为 double 立即数），但 `inst->operands[0] = integer_result` 的 `type` 是 `KRT_IR_VALUE_INTEGER`（`KrtUInt128`）。后端按 `KRT_IR_IMM` 处理时读 `data.imm`（double 字段），而 `KrtUInt128` 在 union 中与 `double imm` 重叠 → 读取错误的位模式。
- 最小修复：整数折叠后 opcode 应为 `KRT_IR_INTEGER`（若存在）或保留原 opcode 只替换 operands。

#### [高] S39-5｜`ir_optimize_constant_folding` 覆盖 `operands[0].data.imm` 但不更新 `value_type`
- 位置：`IrOptimizer.c:204`
- 类别：阶段不对齐
- 触发条件：double 常量折叠。
- 后果：`inst->operands[0].value_type` 保留原操作数的 token 类型（如 `TOKEN_INT`），但值已是 double 结果 → 后续阶段按 `value_type` 解释时类型不符。
- 最小修复：同步设置 `value_type`。

#### [高] S39-6｜`ir_optimize_constant_propagation` 的 `constants.capacity = 256` 固定，超出静默丢弃
- 位置：`IrOptimizer.c:273`（`capacity = 256`）、`:281`（`if (!found && constants.count < constants.capacity)`）
- 类别：静默失败
- 触发条件：函数内不同 TEMP 常量数 > 256。
- 后果：超出部分不被传播，优化不完整且无任何诊断。
- 最小修复：动态扩容或报错。

#### [高] S39-7｜`values_equal` 的 `KRT_IR_VALUE_VAR` 分支 `strcmp` 无 NULL 检查
- 位置：`IrOptimizer.c:50`
- 类别：未检查前置条件
- 触发条件：`a->data.name` 或 `b->data.name` 为 NULL。
- 后果：空指针解引用 → SIGSEGV。
- 最小修复：`return a->data.name && b->data.name && strcmp(...) == 0;`

#### [高] S39-8｜`is_pure_operation` 不含浮点操作码 → 浮点表达式 CSE 不合并
- 位置：`IrOptimizer.c:60-82`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：浮点运算（FADD/FSUB/FMUL/FDIV/FLT/FGT/FEQ/FLE/FGE/FNE）。
- 后果：整数表达式被 CSE 合并，浮点表达式不合并 → 同层功能行为不一致。
- 最小修复：把浮点操作码加入 `is_pure_operation`。

#### [高] S39-9｜`is_side_effect_free` 的 `default: return true` → 未知 opcode 被误判为无副作用
- 位置：`IrOptimizer.c:84-99`
- 类别：静默失败
- 触发条件：未来新增有副作用的 opcode，或当前已有但未列出的 opcode（如 `KRT_IR_CALL_INDIRECT` 已列出，但 `KRT_IR_ATOMIC_*` 等未列出）。
- 后果：DCE 可能删除有副作用的指令。
- 最小修复：`default: return false;`（保守）。

#### [高] S39-10｜`is_constant_value` 只认 `KRT_IR_VALUE_IMM`，漏 `KRT_IR_VALUE_INTEGER`/`IMM_F`
- 位置：`IrOptimizer.c:37-39`
- 类别：阶段不对齐
- 触发条件：128 位整数常量或浮点立即数。
- 后果：这些常量不参与 double 折叠路径（虽然 :184 有整数路径，但 :198 的 double 路径漏掉 `IMM_F`）。
- 最小修复：补充 `KRT_IR_VALUE_IMM_F` 分支。

#### [高] S39-11｜`ir_optimize_constant_propagation` 的 `KRT_CALLOC` 失败时跳过该函数（静默）
- 位置：`IrOptimizer.c:265-269`
- 类别：静默失败 / 未检查前置条件（OOM 时静默 NULL 传播，零诊断）
- 触发条件：OOM（`KRT_CALLOC` 返回 NULL）。
- 后果：该函数常量传播被静默跳过，无任何诊断；优化结果不完整且不可观测。
- 最小修复：移除该分支或改为断言/报错；至少补 `KRT_COMPILE_ERROR`。
- 备注：【修订 G1】原文判「中」并称「KRT_CALLOC 失败实际会 exit(1)，此分支不可达（死代码）」——该前提错误：`KrtSafeCalloc`/`KrtSafeMalloc` 失败返回 NULL（size==0 或 malloc 失败均返回 NULL），`KrtMemoryReportError` 是空操作，故此分支是活路径，改标「静默失败 / 未检查前置条件」，严重度由中提升为高。

#### [中] S39-12｜`fold_constant_unary` 只处理 `KRT_IR_SUB`，不处理按位取反/逻辑非
- 位置：`IrOptimizer.c:155-163`
- 类别：死代码 / 语义分叉
- 触发条件：`KRT_IR_NOT` 等一元操作。
- 后果：一元常量折叠不完整。
- 最小修复：补充或删除该函数。

#### [致命] S40-1｜DCE 的 `KRT_FREE(inst)` 与 arena 分配的 inst 冲突 → 堆破坏（启用 SSA 后触发）
- 位置：`IrOptimizer.c:336-340`（`KRT_FREE(inst->operands); KRT_FREE(inst);`）；对照 `Ir.c:334`（`ir_create_inst` 用 `KRT_CALLOC`）与 `IrSsa.c:1229`（`move_inst` 用 `KrtIrArenaAlloc`）
- 类别：资源与生命周期 / 契约破坏
- 触发条件：`KrtIrSsaLowerPhis` 把 arena 分配的 `move_inst` 加入 `pred->first_inst` 链表（`IrSsa.c:1268-1272`）后，`ir_optimize_dead_code_elimination` 遍历到它并 `KRT_FREE(move_inst)`。当前 SSA 未启用（见 S37-4 死代码事实），故不可达；一旦启用 SSA 即触发。
- 后果：对 arena 内存调用 `free()` → 堆元数据破坏 / SIGSEGV / 未定义行为。
- 最小修复：inst 结构体加 `bool is_arena` 字段，DCE 只释放 `is_arena == false` 的；或统一用 arena 分配 inst。
- 备注：原文把「SSA 未启用」归因于 S37-1（配置漂移）；按 S37-4，根因是 SSA 构造入口零调用者。严重度维持致命（一旦启用即触发）。


#### [高] S40-2｜CSE 的 `expr_capacity = 256` 固定，超出静默丢弃
- 位置：`IrOptimizer.c:383`（`int expr_capacity = 256`）、`:421`（`if (!found && expr_count < expr_capacity)`）
- 类别：静默失败
- 触发条件：函数内纯表达式数 > 256。
- 后果：超出部分不参与 CSE，优化不完整且无诊断。
- 最小修复：动态扩容。

#### [高] S40-3｜CSE 把匹配 inst 改成 COPY 时只改 `type`/`index`，不改 `value_type`
- 位置：`IrOptimizer.c:401-403`
- 类别：阶段不对齐
- 触发条件：CSE 命中时。
- 后果：`inst->operands[0].value_type` 保留原操作数类型，但 `type` 已改为 `KRT_IR_VALUE_TEMP` → 后续阶段按 `value_type` 解释时类型不符。
- 最小修复：同步设置 `value_type`。

#### [高] S40-4｜`ir_optimize_copy_propagation` 假设 `operands[0].type == KRT_IR_VALUE_TEMP`，否则读 union 错误字段
- 位置：`IrOptimizer.c:445`（`int copy_from = inst->operands[0].data.index;`）
- 类别：未检查前置条件
- 触发条件：`KRT_IR_COPY` 指令的 `operands[0].type` 是 `IMM`/`VAR`/`INTEGER`（如 `KrtIrSsaLowerPhis:1245` 生成的 COPY，其 operands[0] 是 phi 值，可能是 VAR/IMM）。
- 后果：`data.index` 读取 union 的 `index` 字段，而 `IMM` 的 union 是 `double imm` → 把 double 位模式当 int → 垃圾 `copy_from` → 后续把垃圾索引写入其他指令 → **IR 破坏**。
- 最小修复：`if (inst->operands[0].type != KRT_IR_VALUE_TEMP) { inst = inst->next; continue; }`

#### [高] S40-5｜`ir_optimize_copy_propagation` 只遍历块内后续指令，不跨块 → 传播不完整
- 位置：`IrOptimizer.c:455`（`KrtIRInst* use = inst->next;`）
- 类别：静默失败 / 语义分叉
- 触发条件：COPY 的结果在后续基本块使用。
- 后果：跨块使用不被传播，与常量传播（同样只块内）一致但优化不完整。
- 最小修复：改为跨块数据流分析，或文档化"仅块内"契约。

#### [中] S40-6｜DCE 删除 inst 后 `block->insts[]` 尾部未清零 → 悬垂指针
- 位置：`IrOptimizer.c:347-348`（`block->insts[live_count++] = inst;` 后 `block->inst_count = live_count;`）
- 类别：资源与生命周期
- 触发条件：DCE 删除 inst 后。
- 后果：`insts[live_count..old_count-1]` 仍指向已 `KRT_FREE` 的 inst；若有代码按 `inst_capacity` 或旧 `inst_count` 遍历（如 `KroRegisterPacking.inc:267` 读 `insts[index]`），会读到悬垂指针。
- 最小修复：`for (int i = live_count; i < old_count; i++) block->insts[i] = NULL;`

#### [中] S40-7｜DCE 依赖 `is_side_effect_free` 的 `default: return true` → 有副作用指令可能被删
- 位置：`IrOptimizer.c:318`（`is_side_effect_free(inst->opcode)`）、`:99`（`default: return true`）
- 类别：静默失败（与 S39-9 同源）
- 触发条件：`result.type == VOID` 且 opcode 不在 `is_side_effect_free` 的"有副作用"列表中。
- 后果：若有未列出的有副作用 opcode，其 VOID 结果指令被删除 → 副作用丢失。
- 最小修复：`default: return false;`

#### [致命] S41-1｜强度削减把 `x*0`/`x^0` 改成 IMM 时只写 `data.imm` 不改 `type` → 后端读 union 错误字段
- 位置：`IrOptimizer.c:511-514`（MUL val==0）、`:534-537`（POW exp==0）
- 类别：未检查前置条件 / 阶段不对齐
- 触发条件：启用 `OPT_STRENGTH_REDUCTION` 且存在 `x * 0` 或 `x ^ 0`（`0` 为 `KRT_IR_VALUE_IMM`）。
- 后果：`inst->operands[0].data.imm = 0` 覆盖 union，但 `operands[0].type` 仍是原类型（`VAR`/`TEMP`）。后端按 `type == VAR` 读 `data.name`（即 double 0 的位模式当指针）→ **空指针解引用 / SIGSEGV**；或按 `TEMP` 读 `data.index` → 垃圾索引。
- 最小修复：`inst->operands[0].type = KRT_IR_VALUE_IMM; inst->operands[0].value_type = TOKEN_FLOAT;`（或对应 token 类型）。
- 备注：原文列为疑似——需确认 `OPT_STRENGTH_REDUCTION` 是否默认启用（若启用可立即触发）。

#### [高] S41-2｜`ir_optimize_loop_invariant_code_motion` 是空操作 → 死代码
- 位置：`IrOptimizer.c:618-622`
- 类别：死代码 / 注释脱节
- 触发条件：任何调用。
- 后果：函数存在但 `(void)module; (void)stats; return false;`，LICM 从未实现；若 `OptimizationFlags` 有对应位，启用后静默无效果。
- 最小修复：实现或删除，并在头文件标注"未实现"。

#### [高] S41-3｜`ir_optimize_escape_analysis` 是空操作 → 死代码
- 位置：`IrOptimizer.c:638-642`
- 类别：死代码
- 触发条件：任何调用。
- 后果：同 S41-2。
- 最小修复：同 S41-2（实现或删除并标注未实现）。

#### [中] S41-4｜`ir_optimize_function_inlining` 重复调用 `eliminate_pure_expressions`
- 位置：`IrOptimizer.c:626`、`:628`
- 类别：语义分叉 / 死代码
- 触发条件：启用 `OPT_FUNCTION_INLINING`。
- 后果：`eliminate_pure_expressions` 被调用两次，第二次在 `inline_recursive_functions` 后，可能有意（内联后再次消除）但无注释说明 → 语义不清。
- 最小修复：加注释说明为何调用两次，或合并。

#### [中] S41-5｜`ir_optimizer_print_stats` 用 `printf`（stdout）而非 stderr → 诊断流不一致
- 位置：`IrOptimizer.c:692-701`
- 类别：错误处理不一致
- 触发条件：调用该函数。
- 后果：统计信息污染 stdout（可能干扰 `-o` 输出或其他工具链）。
- 最小修复：改用 `fprintf(stderr, ...)`。

#### [中] S41-6｜`get_time_seconds` 用 `clock()`（CPU 时间）而非 wall time
- 位置：`IrOptimizer.c:27-29`
- 类别：注释脱节 / 阶段不对齐
- 触发条件：多线程/并行编译时。
- 后果：`time_spent` 统计 CPU 时间，与用户预期的 wall time 不符；并行场景下数值无意义。
- 最小修复：改用 `clock_gettime(CLOCK_MONOTONIC)`。

#### [中] S41-7｜`MUL val==2` 把 `operands[1] = operands[0]` 浅拷贝 → 两个操作数共享 `char* name`
- 位置：`IrOptimizer.c:520`
- 类别：资源与生命周期
- 触发条件：`x * 2` 且 `operands[0].type == KRT_IR_VALUE_VAR`。
- 后果：两个 operand 共享同一 `name` 指针；当前无释放逻辑，但一旦有 `KrtIRValue` 析构会双重释放。
- 最小修复：`KRT_STRDUP` 或文档化"operand 不拥有 name"。

#### [高] S42-1｜`find_memory_free_functions` 超上限或遇匿名函数时返回 NULL → 整个 CSE 静默失效
- 位置：`IrPureCse.inc:96-98`（`if (!f->name || ++count > KRT_PURE_FUNCTION_LIMIT) return NULL;`）、`:104-106`（call 超限 return NULL）
- 类别：静默失败
- 触发条件：模块函数数 > 65536，或 CALL 指令数 > 1048576，或存在匿名函数（`f->name == NULL`）。
- 后果：`eliminate_pure_expressions` 拿不到函数表，整个纯函数 CSE 静默跳过，无任何诊断。
- 最小修复：匿名函数跳过而非整体失败；超限时打印诊断。

#### [高] S42-2｜`cse_direct_callee` 不检查 `callee->params` 是否为 NULL → OOB 读
- 位置：`IrPureCse.inc:86`（`callee->params[i].is_array`）
- 类别：未检查前置条件
- 触发条件：`callee->param_count > 0` 但 `callee->params == NULL`（`KrtIrTypeFunction` :138-160 允许此情况，S35-2 已报告）。
- 后果：空指针解引用 → SIGSEGV。
- 最小修复：`if (!callee->params) return -1;`

#### [中] S42-3｜`cse_scalar` 漏 `KRT_IR_VALUE_IMM_F` → 浮点不参与局部值编号
- 位置：`IrPureCse.inc:27-30`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：浮点立即数操作数。
- 后果：整数/布尔参与 CSE，浮点不参与 → 同层功能行为不一致（与 S39-8 同源）。
- 最小修复：若浮点可纯，加入 `IMM_F`。

#### [中] S42-4｜`KRT_PURE_FUNCTION_LIMIT`/`KRT_PURE_CALL_LIMIT` 硬编码上限未文档化
- 位置：`IrPureCse.inc:4-6`
- 类别：契约破坏 / 配置漂移
- 触发条件：大模块。
- 后果：上限行为（整体失败）未在头文件文档化，调用方无法预知。
- 最小修复：在 `IrOptimizer.h` 文档化，或改为动态扩容。

#### [致命] S43-1｜`generics_build_instantiated_name` 对 `type_args[i]` 无 NULL 检查 → 空指针崩溃
- 位置：`Generics.c:245`（`strlen(type_args[i])`）、`:257-259`（`KRT_STRCAT_S(result, total_size, type_args[i])`）
- 类别：未检查前置条件
- 触发条件：任一 `type_args[i] == NULL`（`generics_instantiate_type` 允许 NULL arg，:138 `if (type_args[i])`）。
- 后果：`strlen(NULL)` → SIGSEGV；或 `strcat(NULL)` → SIGSEGV。
- 最小修复：拼接前跳过 NULL arg，并同步修正 `total_size`。

#### [致命] S43-2｜`generics_instantiate_type` 的 `total_size` 低估 → `KRT_STRCAT_S` 缓冲区溢出
- 位置：`Generics.c:137-146`（计算）vs `:162-168`（拼接）
- 类别：未检查前置条件
- 触发条件：`type_args[i] == NULL` 且 `i > 0`。
- 后果：计算时 NULL arg 不加逗号分隔符（:142 `if (i > 0) total_size += 1` 在 `if (type_args[i])` 内），但拼接时 `if (i > 0) KRT_STRCAT_S(",")` 无条件执行 → 实际写入超过 `total_size` → 堆溢出。
- 最小修复：拼接与计算用同一条件（分隔符逻辑放进 `if (type_args[i])` 内，或计算时无条件加）。

#### [高] S43-3｜`generics_lookup_type` 持锁返回内部指针 → 竞态
- 位置：`Generics.c:84-96`（:94 `pthread_mutex_unlock` 后 `return current`）
- 类别：共享可变状态
- 触发条件：多线程并发访问 registry。
- 后果：返回的 `GenericType*` 在锁外可能被其他线程修改/释放（`generics_destroy_registry`）→ 悬垂/竞态。
- 最小修复：返回副本，或文档化"调用方不得在锁外使用"。

#### [高] S43-4｜`generics_register_type` 重名时静默丢弃新 params/definition
- 位置：`Generics.c:49-59`
- 类别：静默失败
- 触发条件：同名泛型重复注册且 `existing->parameters` 已非空。
- 后果：新 `params` 被丢弃（且未释放 → 泄漏），`parameter_count` 不更新 → 实例化参数个数错误。
- 最小修复：重名时比较 `param_count`，不一致则报错；或释放旧 params 再替换。

#### [高] S43-5｜`generics_validate_instantiation` 只比数量不验证 type_args 内容
- 位置：`Generics.c:106-110`（`(void)type_args;`）
- 类别：静默失败 / 契约破坏
- 触发条件：任意类型参数。
- 后果：非法类型参数（如不存在的类型）被接受，实例化产出无效符号。
- 最小修复：验证每个 `type_args[i]` 是否为已注册类型。

#### [高] S43-6｜`generics_instantiate_type` 对已存在的实例化名直接返回 true，不验证兼容性
- 位置：`Generics.c:170-173`
- 类别：语义分叉
- 触发条件：同名实例化已存在但定义不同（如泛型定义被修改）。
- 后果：返回过期的特化符号。
- 最小修复：比较定义一致性。

#### [高] S43-7｜`specialized_symbols` 创建后从未使用 → 死代码/资源浪费
- 位置：`Generics.c:175-179`
- 类别：死代码
- 触发条件：每次首次实例化。
- 后果：`symbol_table_create()` 分配的符号表从未被读写（需 grep 确认无其他使用者），纯浪费。
- 最小修复：删除，或实现其用途。

#### [高] S43-8｜`generics_get_type_info` 返回内部指针的浅拷贝 → 契约破坏
- 位置：`Generics.c:280-292`
- 类别：资源与生命周期
- 触发条件：调用方修改/释放 `info->name`/`parameters`/`declaration`。
- 后果：破坏 registry 内部状态。
- 最小修复：深拷贝，或文档化"只读"。

#### [中] S43-9｜`generics_instantiate_type` 要求 `arg_count > 0` → 无参泛型无法实例化
- 位置：`Generics.c:118`
- 类别：静默失败 / 语义分叉
- 触发条件：`parameter_count == 0` 的泛型。
- 后果：`generics_validate_instantiation` 允许 `arg_count == 0`，但 `instantiate` 直接拒绝 → 不一致。
- 最小修复：改为 `arg_count < 0`。

#### [中] S43-10｜`generics_register_type` 不检查 `param_count > 0` 时 `params` 是否为 NULL
- 位置：`Generics.c:44`
- 类别：未检查前置条件
- 触发条件：`param_count > 0 && params == NULL`。
- 后果：`generics_free_parameters(NULL)` 安全，但 `params[i]` 访问会崩溃（若后续代码遍历）。
- 最小修复：`if (param_count > 0 && !params) return false;`

#### [致命] S44-1｜未定义函数调用被语义分析静默接受，诊断与实际错误不符、无位置信息
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:356-368`（`method` 在 `local_scope`/`global_scope` 都找不到、fallback 也找不到 → 直接 `break`，无任何错误）；`Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.c:75-78`（`if (!method_symbol) { KRT_FREE(mangled_name); return true; }` → 静默成功）
- 类别：静默失败 / 诊断缺失 / 阶段不对齐
- 触发条件：已实证 t1.krt：`class A { public function A() {} public function int32 F() { return Undefined(); } } int32 main() { return 0; }` → 语义分析阶段 0 错误。
- 后果：IrGen 发出调用伪符号 `_ZN9UndefinedEv`，直到链接期才报 `Undefined symbol: _ZN9UndefinedEv`，无源码位置、名字被 mangling，用户无法知道是 `A.F` 里的 `Undefined()`；对照 t2.krt（类外同名调用）能正确报 `t2.krt(1,34): error: Undefined function: Undefined`，两条路径诊断能力不一致。
- 最小修复：`PointerAnalysis.inc:368` 后若 `method == NULL` 且非 object 调用，调用 `semantic_analyzer_add_error_at(analyzer, expr, "Undefined function: %s", expr->data.call.name)` 并返回 false；`SemanticAnalyzer.c:75-78` 改为报错而非 `return true`。

#### [致命] S44-2｜赋值类型兼容性对非标量不检查 → 隐式截断/溢出无诊断
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:40-63`（`pointer_assignable` 对非指针/非字符串/非 IDENTIFIER 的标量对一律 `return true`）；`SemanticAnalyzer.c:109-117`（AST_ASSIGNMENT 只查符号存在）；`SemanticAnalyzer.c:1135-1169`（二元 ASSIGN 只查 lvalue/字符串拼接/只读成员，不查类型）
- 类别：静默失败 / 阶段不对齐
- 触发条件：已实证——`int32 main() { int32 x = 1; double d = 3.9; x = d; return x; }` 编译成功、运行 `exit=3`（double→int32 静默截断）；`x = 100000000000;` 编译成功、静默整数溢出。
- 后果：类型系统对赋值无约束，数值静默丢失/回绕，无任何诊断。
- 最小修复：在赋值路径增加标量转换规则（仅允许同类型或有文档记录的显式加宽），否则 `semantic_analyzer_add_error_at`。

#### [高（疑似）] S44-3｜`source_type_equal` 对 NULL `type_name` 的两个不同类判等
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:3-28`
- 类别：阶段不对齐 / 未检查前置条件
- 触发条件：`token==TOKEN_IDENTIFIER` 且至少一方 `type_name==NULL` 时只比较 token。
- 后果：两个不同的 NULL-type_name 类类型被判等 → 错误接受赋值/传参。
- 最小修复：（原文未给出）
- 备注：疑似——需验证 `type_name` 是否可能为 NULL（检查 `KrtParseSourceType`/`KrtSourceTypeClone` 构造路径）；若不可能为 NULL 则降级为防御性缺陷。

#### [高] S44-4｜`TypeExpansion.inc` 把 enum token 改成 INT32 但保留 enum `type_name` → 矛盾组合下传
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/TypeExpansion.inc:368-370`
- 类别：阶段不对齐
- 触发条件：枚举类型实例化/展开后，`token=TOKEN_INT32` 但 `type_name` 仍指向 enum 名。
- 后果：下游若先看 `type_name`（类查找/泛型实例化/IrGen 类型映射）会得到"INT32 token + 类名"的矛盾组合，行为取决于消费顺序。
- 最小修复：（原文未给出）
- 备注：需验证——grep IrGen/Semantic 中 `declared_type.type_name` 的消费顺序与优先级。

#### [中] S44-5｜`TypeExpansion.inc` 用 arena 替换堆数组，原数组泄漏且所有权不一致
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/TypeExpansion.inc:448-453`（`expansion_alloc`（type_arena）替换 `ast->data.block.statements`，原 `KRT_REALLOC` 数组无指针释放）；`context.count==0` 时 `expansion_alloc` 因 `size?size:1` 返回 1 字节非 NULL 指针
- 类别：资源与生命周期 / 契约破坏
- 触发条件：任何带泛型实例化的程序展开。
- 后果：`Parser.c:33` 的堆 statements 数组泄漏；后续若有人对 block statements 调 `KRT_FREE` 会 mixed-free。已确认 `ast_destroy_node`（`Ast.c:48`）对 arena 节点提前 return，故当前不会崩溃，但所有权契约未文档化。
- 最小修复：展开时释放原 statements 数组，或在契约中明确"展开后 block 由 arena 拥有、不得 free"。

#### [中] S44-6｜实例计数只增不复位，失败尝试污染成功路径
- 位置：`TypeExpansion.inc:276`（`++context->instances > 256` 对失败尝试也计数且从不复位）；`:320-325` 递归 `expansion_instantiate` 在验证前就可能已 `expansion_append` 实例（:347），失败路径 `invalid:` 仍把这些实例留在 `context.nodes`，最终被 :448-451 复制进 AST
- 类别：静默失败 / 共享可变状态
- 触发条件：一次失败的类型展开后跟一次成功展开。
- 后果：失败残留实例进入 AST；`instances` 计数被失败尝试耗尽，后续合法展开被误判超限。
- 最小修复：失败路径回滚已 append 实例；`instances` 只在成功时计数。

#### [中] S44-7｜`expansion_collect` 只处理顶层，嵌套类/限定名解析失败
- 位置：`TypeExpansion.inc:98-153`（只处理 AST_PROGRAM/AST_NAMESPACE_DECLARATION，不递归类体/不收集嵌套类）；`expansion_find`（:71-79）只匹配顶层收集名
- 类别：语义分叉 / 阶段不对齐
- 触发条件：嵌套类或跨作用域限定名泛型。
- 后果：与 SymbolTable 作用域链规则分叉，嵌套类解析失败。
- 最小修复：（原文未给出）
- 备注：需验证——用嵌套类/命名空间嵌套实证。

#### [中] S44-8｜表达式/成员链递归无深度限制
- 位置：`TypeExpansion.inc:383`（`expansion_expression_name`）；`PointerAnalysis.inc:199/204/238/255/274`；`SemanticAnalyzer.c:1141-1142`
- 类别：未检查前置条件
- 触发条件：深嵌套表达式/成员链。
- 后果：栈溢出崩溃。
- 最小修复：加深度上限并诊断。

#### [中] S44-9｜AST_CAST_EXPRESSION 不校验类型兼容性
- 位置：`PointerAnalysis.inc:394-414`（只校验非指针侧是整数/null/指针）
- 类别：静默失败
- 触发条件：`(int32)someObject` 之类。
- 后果：无诊断通过。
- 最小修复：（原文未给出）
- 备注：需验证——实证 `(int32)obj` 是否编译成功。

#### [中] S44-10｜第二段 AST_CALL：非函数符号/找不到符号均无错误
- 位置：`PointerAnalysis.inc:353-380`
- 类别：诊断缺失
- 触发条件：`method` 找到但 `source_type.function==NULL`，或 `method` 找不到。
- 后果：静默通过。
- 最小修复：（原文未给出）

#### [中] S44-11｜诊断与实际错误不符：未定义变量与数组整体 ref 合并
- 位置：`PointerAnalysis.inc:97-101`
- 类别：诊断缺失
- 触发条件：`!symbol`（未定义变量）与 `symbol->is_array` 走同一分支。
- 后果：报 "ref requires a scalar slot; pass an array element instead"，掩盖真实错误。
- 最小修复：（原文未给出）

#### [中] S44-12｜`global::` 前缀原地剥除，名字永久改变且不可恢复
- 位置：`SemanticAnalyzer.c:16-19`（`call_expr->data.call.name = call_expr->data.call.name + 8;`）
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：`global::X()` 调用。
- 后果：原串泄漏；AST 名字被永久改变，二次分析/诊断打印显示错误名字。
- 最小修复：用长度参数或拷贝，不改原指针。

#### [中（疑似）] S44-13｜字符串下标特例与指针分支覆盖关系
- 位置：`PointerAnalysis.inc:432-441`
- 类别：阶段不对齐
- 触发条件：字符串下标访问（原文未展开）。
- 后果：原文未展开，仅指出覆盖关系存疑。
- 最小修复：（原文未给出）
- 备注：疑似——需验证 `KrtSourceIsPointer` 对 TOKEN_TYPE_STRING 的判定（取决于 string 是否 pointer_depth>0），确认字符串下标是否被正确覆盖。

#### [中（疑似）] S44-14｜`new` 表达式 `resolved_type` pointer_depth=0 → 成员/方法调用走错分支
- 位置：`PointerAnalysis.inc:305-307`
- 类别：阶段不对齐
- 触发条件：`new` 表达式得到的 `resolved_type` pointer_depth=0。
- 后果：后续成员/方法调用走错分支。
- 最小修复：（原文未给出）
- 备注：疑似——需验证实证 `new Box(2)` 后 `b.Get()` 是否走错分支。

#### [高] S45-1｜`StackCalculator.c` 整套 244 行零调用者 = 死代码
- 位置：`Re.KrtC/src/Core/Utils/StackCalculator.c:1-244`（全部函数）
- 类别：死代码 / 无人调用的实现
- 触发条件：任何构建。
- 后果：栈帧分析、栈使用预测、溢出检查、全局 analyzer 全部从不执行；`Core.h:15` 仍 include 该头文件。
- 最小修复：删除该文件及 `Error.h`/`StackCalculator.h` 中相关声明，或接入实际调用点。

#### [高] S45-2｜`LeakDetector` 全套 API 零调用者，泄漏检测永久关闭
- 位置：`Re.KrtC/src/Core/Memory/LeakDetector.c:1-203`（全部函数）；`Re.KrtC/src/Core/Utils/KrtCommon.h:168`（`#define KRT_LEAK_DETECTION_ENABLED 0`）；`Re.KrtC/src/Core/Memory/LeakDetector.h:10`（重复定义 0）
- 类别：死代码 / 配置漂移
- 触发条件：任何构建。
- 后果：`KRT_MALLOC_LEAK`/`KRT_CALLOC_LEAK`/`KRT_REALLOC_LEAK`/`KRT_STRDUP_LEAK`/`KrtFree_LEAK` 宏在 `KrtCommon.h:206-210` 全部退化为普通 `KRT_MALLOC`/`KRT_FREE`；`KRT_LEAK_DETECTOR_INIT/CLEANUP/REPORT_LEAKS/GET_MEMORY_STATS`（`LeakDetector.h:38-51`）退化为 `((void)0)`/零值宏，泄漏检测功能整体不可用。
- 最小修复：删除死实现，或把开关改为构建期可配置并接入 `Main.c` 初始化/清理。

#### [高] S45-3｜`KRT_LEAK_DETECTION_ENABLED` 在两处独立定义为 0 → 配置漂移
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:168` 与 `Re.KrtC/src/Core/Memory/LeakDetector.h:10`
- 类别：配置漂移 / 语义分叉
- 触发条件：任一处改为 1 而另一处未改。
- 后果：`KrtCommon.h` 的宏走 `#ifdef`（恒真分支，因为宏被定义为 0 但 `#ifdef` 仍成立）→ 实际 `KRT_MALLOC_LEAK` 展开为 `KrtMallocLeakDetected`，而 `LeakDetector.h` 的 `#if KRT_LEAK_DETECTION_ENABLED` 为假 → 声明/宏不一致，编译期或链接期矛盾。
- 最小修复：只在 `LeakDetector.h` 定义一次，`KrtCommon.h` include 它。

#### [高] S45-4｜`KRT_STACK_ALIGNMENT`/`KRT_MIN_STACK_SIZE` 在三处独立定义
- 位置：`Re.KrtC/src/Core/Target/KairoteTarget.h:8`、`Re.KrtC/src/Core/Utils/StackCalculator.h:7-8`、`Re.KrtC/src/Core/Utils/Error.h:7-8`
- 类别：配置漂移 / 语义分叉
- 触发条件：任一处分值调整。
- 后果：栈布局计算与目标 ABI 常量可能不一致，且都是死代码路径（S45-1），掩盖了风险。
- 最小修复：统一到 `KairoteTarget.h`，其余 include。

#### [高] S45-5｜`KrtStackAnalyzerBeginFunction` 扩容失败时 `frame_count` 已递增但 frame 未初始化
- 位置：`Re.KrtC/src/Core/Utils/StackCalculator.c:57-79`（:68 `frame_count++` 在 :74 `usages` 分配之前；:76-79 失败直接 `return NULL`，但 `frame_count` 已加且 frame 字段未初始化）
- 类别：未检查前置条件 / 资源与生命周期
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 失败返回 NULL，活路径且零诊断）。
- 后果：`frame_count` 与有效 frame 数不一致；`KrtStackAnalyzerDestroy` 会读未初始化 `usages` 并 `KRT_FREE` 垃圾指针 → 堆破坏。
- 最小修复：先分配 `usages` 成功后再 `frame_count++`。
- 备注：【修订 G1】原「`KRT_MALLOC` 失败即 exit(1)，实际不可达，属防御性缺陷」作废；该文件同时是零调用者死代码（S45-1），按后果定高。

#### [中] S45-6｜`KrtStackFrameOptimizeLayout` 与 `KrtStackFrameGetTotalSize` 对齐逻辑重复实现
- 位置：`StackCalculator.c:133-143` 与 `:150-156`
- 类别：语义分叉
- 触发条件：任何调用（当前零调用）。
- 后果：同一"对齐到 KRT_STACK_ALIGNMENT 且下限 KRT_MIN_STACK_SIZE"规则两处实现，易漂移。
- 最小修复：`GetTotalSize` 调用 `OptimizeLayout` 或抽公共函数。

#### [中] S45-7｜`KrtStackFrameGenerateReport` 是空操作
- 位置：`StackCalculator.c:161-165`
- 类别：死代码 / 注释与实现脱节
- 触发条件：任何调用（当前零调用）。
- 后果：报告功能未实现。
- 最小修复：实现或删除。

#### [中] S45-8｜`KrtStackFrameCheckOverflow` 计算 `usage_ratio` 后丢弃
- 位置：`StackCalculator.c:178-179`（`double usage_ratio = ...; (void)usage_ratio;`）
- 类别：死代码
- 触发条件：任何调用（当前零调用）。
- 后果：利用率信息被丢弃。
- 最小修复：用于诊断或删除。

#### [中] S45-9｜`KrtStackAnalyzerEndFunction` 用 `KrtError` 报错但返回 void，调用方无法感知
- 位置：`StackCalculator.c:88-94`
- 类别：错误处理不一致 / 静默失败
- 触发条件：`current_depth == 0` 时调用。
- 后果：只打印不拒绝，栈深度状态已错。
- 最小修复：返回错误码或断言。

#### [中] S45-10｜`KrtStackFrameAddUsage` 对 `size==0` 报错但 `size` 来自外部
- 位置：`StackCalculator.c:96-101`
- 类别：错误处理不一致
- 触发条件：`size==0`。
- 后果：`KrtError` 只打印，frame 状态不变；调用方继续。
- 最小修复：返回错误码。

#### [中] S45-11｜`KrtStackAnalyzerInit` 的 `KRT_MALLOC` 失败分支（原判死代码）
- 位置：`StackCalculator.c:12-15`、`:19-23`、`:76-79`、`:106-109`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：返回 NULL 且无诊断，调用方若解引用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码（与全局 `KRT_MALLOC` 恒成功一致）」结论作废；该文件为零调用者死代码（S45-1），严重度按后果维持中。

#### [中] S45-12｜`KrtCalculateDynamicStackSize` 用 IR 文本行数估算栈大小，与真实帧布局无关
- 位置：`StackCalculator.c:184-209`
- 类别：契约破坏 / 静默失败
- 触发条件：任何调用（当前零调用）。
- 后果：估算与实际栈使用无对应关系，可能严重高估/低估。
- 最小修复：基于真实 IR 帧信息计算，或删除。

#### [中] S45-13｜`KrtPredictStackUsage` 忽略 `function_signature` 参数
- 位置：`StackCalculator.c:211-212`（`__attribute__((unused))`）
- 类别：死代码 / 契约破坏
- 触发条件：任何调用（当前零调用）。
- 后果：签名信息未用，参数个数/局部变量数由调用方硬编码。
- 最小修复：从签名解析或使用真实数据。

#### [中] S45-14｜`KrtOutputCacheInit` 先 `Clear` 再 `enabled=0`，`Clear` 不清 `enabled`
- 位置：`OutputCache.c:11-14`（`KrtOutputCacheClear` :24-35 不清 `enabled`）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：`enabled=1` 时调 `Init`。
- 后果：`Clear` 后 `enabled` 仍为 1，但 `Init` 紧接着置 0；若有人单独调 `Clear` 期望保留配置，行为不一致。
- 最小修复：明确 `Clear` 是否重置 `enabled`。

#### [中] S45-15｜`KrtOutputCacheCleanup` 先 `Flush` 再置空，但 `Flush` 已置空 → 二次置空冗余
- 位置：`OutputCache.c:16-22` 与 `:168-193`
- 类别：语义分叉（轻微）
- 触发条件：任何调用。
- 后果：无功能影响，但两处都在维护链表状态。
- 最小修复：`Cleanup` 只调 `Flush` + `enabled=0`。

#### [中] S45-16｜`KrtOutputCacheAddv`/`AddErrorv` 复制 `va_list` 后内部再复制一次
- 位置：`OutputCache.c:144-149`、`:161-166`、`:37-46`
- 类别：资源与生命周期（冗余）
- 触发条件：任何调用。
- 后果：双重 `va_copy`，无功能错误但冗余。
- 最小修复：去掉一层。

#### [中] S45-17｜`KrtOutputCacheAddInternal` 在 `enabled` 时把格式化串当 `%s` 参数，格式串注入风险
- 位置：`Logger.c:44`（`KrtOutputCacheAdd("%s", buffer)`）与 `OutputCache.c:89`（`fputs(buffer, target)`）
- 类别：契约破坏
- 触发条件：任何带格式串的日志。
- 后果：`buffer` 内容被当作数据而非格式串（这是正确的）；但 `OutputCache.c:134-142` 的 `KrtOutputCacheAdd` 直接把调用方 `format` 当格式串，若调用方传入用户数据作为 format 则有注入风险。
- 最小修复：（原文未给出）
- 备注：需验证——grep `KrtOutputCacheAdd(` 的调用方是否传入非字面量。

#### [中] S45-18｜`Logger.c` 与 `OutputCache.c` 各自实现一套 `format_and_output`/`output_cache_add_internal` 格式化逻辑
- 位置：`Logger.c:9-66` 与 `OutputCache.c:37-132`
- 类别：语义分叉
- 触发条件：任何日志输出。
- 后果：两套格式化+缓存逻辑并行，行为可能不一致（如 `Logger.c:42-51` 在 cache 启用时对 `stdout`/`stderr` 分流，而 `OutputCache.c` 内部也分流）。
- 最小修复：统一到一个实现。

#### [中] S45-19｜`KrtPrintFormat` 在 cache 启用时把结果写到 `stdout`，忽略 `stream` 语义
- 位置：`Logger.c:77-102`（:96 `fputs(buffer, stdout)`）
- 类别：阶段不对齐
- 触发条件：cache 启用时调 `KrtPrintFormat`。
- 后果：`KrtPrintFormat` 无 `stream` 参数，恒写 stdout，与 `KrtFprintf` 行为分叉。
- 最小修复：统一输出目标。

#### [中] S45-20｜`KrtPrintFormat` cache 启用分支 `KRT_MALLOC` 失败静默返回 -1
- 位置：`Logger.c:88-92`
- 类别：静默失败 / 未检查前置条件（OOM 时静默失败，零诊断）
- 触发条件：分配失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：无输出无诊断。
- 最小修复：降级到 `vfprintf`。
- 备注：【修订 G1】原「当前 `KRT_MALLOC` 恒成功，不可达」作废。

#### [中] S45-21｜`KrtFprintf` 对 `stream==NULL` 且 cache 启用时走 `fputs(buffer, stdout)`
- 位置：`Logger.c:52-62`（:57-60 `else` 分支写 stdout）
- 类别：静默失败 / 契约破坏
- 触发条件：`stream==NULL`。
- 后果：本应报错或写 stdout，实际写 stdout 但 `result` 为 `fputs` 返回值，调用方可能误判。
- 最小修复：显式文档化 `stream==NULL` 语义。

#### [中] S45-22｜`KrtPrintf` 与 `KrtFprintf` 在 cache 启用时行为不一致
- 位置：`Logger.c:129-140`（`KrtPrintf` 恒 `vfprintf(stdout)` + `fflush`，不查 cache）vs `:112-127`（`KrtFprintf` 查 cache）
- 类别：语义分叉 / 配置漂移
- 触发条件：cache 启用时。
- 后果：`KrtPrintf` 绕过缓存直接输出，`KrtFprintf` 走缓存 → 同一进程内输出顺序错乱。
- 最小修复：`KrtPrintf` 委托 `KrtFprintf(stdout, ...)`。

#### [中] S45-23｜`KrtStrcpySafe` 截断警告无位置信息
- 位置：`KrtString.c:33-36`
- 类别：诊断缺失
- 触发条件：`src` 长度 ≥ `dest_size-1`。
- 后果：只报 "String truncation"，无文件/行/目标缓冲区大小。
- 最小修复：加 `__FILE__`/`__LINE__` 参数。

#### [中] S45-24｜`KrtStrcatSafe` 在 `dest_len >= dest_size-1` 时静默返回，不警告
- 位置：`KrtString.c:50-52`
- 类别：静默失败 / 诊断缺失
- 触发条件：目标已满。
- 后果：静默丢弃 `src`，与 `KrtStrcpySafe` 的警告行为不一致（S45-23）。
- 最小修复：统一警告行为。

#### [中] S45-25｜`KrtStrcatSafe` 截断警告同样无位置信息
- 位置：`KrtString.c:60-63`
- 类别：诊断缺失
- 触发条件：`src` 剩余未拷贝。
- 后果：同 S45-23。
- 最小修复：同 S45-23（加 `__FILE__`/`__LINE__`）。

#### [中] S45-26｜`KrtSprintfSafe` 对 `buffer==NULL` 或 `buffer_size==0` 无保护
- 位置：`KrtString.c:68-82`（:72 `vsnprintf(buffer, buffer_size, ...)`）
- 类别：未检查前置条件
- 触发条件：`buffer==NULL`。
- 后果：`vsnprintf` 对 NULL 缓冲区是 UB/崩溃。
- 最小修复：加 `if (!buffer || buffer_size == 0) return -1;`。

#### [中] S45-27｜`KrtSprintfSafe` 截断检查用 `result >= buffer_size`，负返回值静默
- 位置：`KrtString.c:76-79`
- 类别：契约破坏（轻微）
- 触发条件：输出长度恰等于 `buffer_size-1`；或 `vsnprintf` 返回负值（编码错误）。
- 后果：`result == buffer_size-1` 不触发警告（正确）、`result == buffer_size` 触发（正确）；但 `result` 是 `int`、`buffer_size` 是 `size_t`，比较时 `result` 转 `size_t`，负值则比较为假 → 静默。
- 最小修复：先判 `result < 0`。
- 备注：原文标注需验证。

#### [中] S45-28｜`KrtStrlcpy` 在 `dest_size==0` 时返回 0，但 `dest` 可能为 NULL
- 位置：`KrtString.c:84-87`
- 类别：静默失败
- 触发条件：`dest_size==0`。
- 后果：无诊断。
- 最小修复：加 `KrtError`。

#### [中] S45-29｜`KrtStrlcat` 在 `dest_len >= dest_size-1` 时返回 `dest_len + strlen(src)`，可能溢出 `size_t`
- 位置：`KrtString.c:111-113`
- 类别：未检查前置条件
- 触发条件：极大字符串。
- 后果：返回值溢出。
- 最小修复：饱和或诊断。

#### [中] S45-30｜`KrtCalculateConcatenatedSize` 不检查 `parts==NULL` 且 `part_count<0`
- 位置：`KrtString.c:125-135`
- 类别：未检查前置条件
- 触发条件：`part_count<0` 时循环不执行（正确）；`parts==NULL` 且 `part_count>0` → 崩溃。
- 后果：空指针解引用。
- 最小修复：加 `if (!parts && part_count > 0) return strlen(base)+1;`。

#### [中] S45-31｜`KrtCalculateConcatenatedSize` 累加 `strlen` 可能溢出 `size_t`
- 位置：`KrtString.c:126-132`
- 类别：未检查前置条件
- 触发条件：多个超长字符串。
- 后果：溢出后分配不足 → 堆溢出。
- 最小修复：溢出检查。

#### [中] S45-32｜`KrtError` 在 `#ifndef _WIN32` 下定义，Windows 下无定义
- 位置：`KrtString.c:7-16`
- 类别：配置漂移 / 契约破坏
- 触发条件：Windows 构建。
- 后果：`KrtError` 未定义 → 链接错误；但 `KrtCommon.h` 可能另有定义。
- 最小修复：（原文未给出）
- 备注：需验证——Windows 下 `KrtError` 的定义位置。

#### [中] S45-33｜`KrtGetTime` 用 `CLOCK_MONOTONIC`，与 `IrOptimizer.c` 的 `clock()` 不一致
- 位置：`Re.KrtC/src/Core/Utils/KrtTime.c:4-8` vs `Re.KrtC/src/Compiler/Optimizer/IrOptimizer.c:27-29`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：任何计时。
- 后果：两处计时基准不同（monotonic vs CPU time），性能统计不可比。
- 最小修复：统一用 `KrtGetTime`。

#### [高] S46-1｜`KrtCompilerCompile` 中 177-189 行是纯死代码空循环
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.c:177-189`
- 类别：死代码
- 触发条件：任何编译。
- 后果：遍历 `main` 函数所有指令但什么都不做；`inst` 循环体为空，编译器可能警告但未报错。
- 最小修复：删除该块，或填入实际检查逻辑。

#### [高] S46-2｜`KrtCompilerCreate` 对 `KRT_TARGET_WASM` 返回 NULL，但 `KrtCompilerCompile` 仍有 WASM 分支
- 位置：`Compiler.c:13-16`（Create 拒绝 WASM）与 `:220-223`（Compile 处理 WASM）
- 类别：死代码 / 契约破坏
- 触发条件：任何路径。
- 后果：`KRT_TARGET_WASM` 永远无法通过 `KrtCompilerCreate` 到达 Compile 的 WASM 分支 → `:220-223` 不可达；若有人绕过 Create 直接构造 `KrtCompiler`，行为未定义。
- 最小修复：删除 Compile 的 WASM 分支，或让 Create 接受 WASM。

#### [高] S46-3｜`KrtCompilerCreate` 的 `KRT_MALLOC` 失败分支（原判死代码）
- 位置：`Compiler.c:19-21`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：返回 NULL 且无诊断，调用方解引用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码（`KRT_MALLOC` 恒成功）」结论作废。

#### [高] S46-4｜`KrtCompilerCompile` 的 `ir_builder` 创建失败分支（原判死代码）
- 位置：`Compiler.c:159-161`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：分配失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：`ir_builder` 为 NULL 且无诊断，后续使用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码」结论作废。

#### [高] S46-5｜`module_has_extended_integers` 只检查 `value_type`，不检查 `opcode` 隐含类型
- 位置：`Compiler.c:62-91`
- 类别：契约破坏 / 静默失败
- 触发条件：扩展整数通过 `KRT_IR_VALUE_INTEGER`（128 位）出现在 `data` 而非 `value_type` 字段。
- 后果：漏检 → 后端可能收到不支持的类型。
- 最小修复：（原文未给出）
- 备注：需验证——`KrtIRInst` 结构与 `value_type` 语义。

#### [高] S46-6｜`module_fits_kro_limits` 的 `names` 数组固定 `KRT_KRO_MAX_LOCAL_VARS * 2`，但 `locals` 计数可能超过
- 位置：`Compiler.c:114`（`const char* names[KRT_KRO_MAX_LOCAL_VARS * 2] = {0};`）与 `:117-142`
- 类别：未检查前置条件
- 触发条件：局部变量数超过 `KRT_KRO_MAX_LOCAL_VARS * 2`。
- 后果：`names` 数组越界写；:142 才检查 `locals > KRT_KRO_MAX_LOCAL_VARS`，而 `names` 容量是 `MAX*2`、阈值是 `MAX` → 若写入按 `locals` 索引且 `locals` 可达 `MAX*2-1`，则检查前已越界。
- 最小修复：把 `names` 容量与检查阈值对齐，或在写入前检查。
- 备注：需验证——读 :118-141 完整写入逻辑。

#### [高] S46-7｜`module_fits_kro_limits` 的 `goto storage_limit` 标签在循环内，跨迭代跳转
- 位置：`Compiler.c:142-147`（标签 `storage_limit:` 在 `for` 循环体内）
- 类别：可读性/维护风险（非缺陷）
- 触发条件：任何超限。
- 后果：标签在循环内，若后续有人在标签后加代码易出错；当前正确。
- 最小修复：抽成函数返回 false。

#### [高] S46-8｜`KrtCompilerCompile` 对 `KRT_TARGET_VM_BYTECODE` 先 `fclose` 再序列化，序列化失败无检查
- 位置：`Compiler.c:224-241`（:239 无返回值检查）
- 类别：静默失败 / 诊断缺失
- 触发条件：序列化失败（磁盘满/路径错误）。
- 后果：`KrtCompilerCompile` 返回 true，调用方以为成功。
- 最小修复：检查返回值并报错。

#### [高] S46-9｜`KRT_TARGET_KRO_OBJ` 与 `KRT_TARGET_EXE_PLATFORM` 分支完全相同
- 位置：`Compiler.c:242-249`
- 类别：语义分叉 / 配置漂移
- 触发条件：两种目标。
- 后果：同一 `KrtKrtGenerate` 调用，但 `KrtCompilerCreate` 对两者用同一 `fopen` 模式（:25-29 只区分 VM/KRO vs 其他），`KRT_TARGET_EXE_PLATFORM` 走 `fopen(..., "w")`（文本模式）→ 生成二进制对象用文本模式，Windows 下会损坏。
- 最小修复：区分 `KRT_TARGET_EXE_PLATFORM` 的二进制模式，或合并两个枚举。

#### [高] S46-10｜`KrtArkLinkAddObjectFile` 在 `arklink_session_add_input` 失败时已 `KRT_STRDUP` 但 `obj_count` 未递增 → 内存泄漏
- 位置：`Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.c:118-127`
- 类别：资源与生命周期
- 触发条件：`arklink_session_add_input` 失败。
- 后果：`ctx->obj_files[ctx->obj_count]` 已分配但未计入 `obj_count`，`KrtArkLinkContextDestroy` 不会释放它 → 泄漏。
- 最小修复：失败时 `KRT_FREE(ctx->obj_files[ctx->obj_count])`。

#### [高] S46-12｜`KrtArkLinkAddRuntimeObjects` 忽略 `KrtArkLinkAddObjectFile` 返回值
- 位置：`ArkLinkIntegration.c:141-148`
- 类别：静默失败 / 错误处理不一致
- 触发条件：runtime 对象添加失败。
- 后果：继续链接，可能生成缺运行时符号的可执行文件。
- 最小修复：检查返回值并报错。

#### [高] S46-13｜`KrtArkLinkContextCreate` 的 `KRT_MALLOC` 失败分支（原判死代码）
- 位置：`ArkLinkIntegration.c:36-38`、`:45-48`、`:51-55`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：返回 NULL 且无诊断，调用方解引用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码（`KRT_MALLOC` 恒成功）」结论作废。

#### [高] S46-14｜`ark_link_logger_callback` 是空操作，ArkLink 所有诊断被丢弃
- 位置：`ArkLinkIntegration.c:24-28`
- 类别：静默失败 / 诊断缺失
- 触发条件：任何 ArkLink 日志（警告/错误）。
- 后果：ArkLink 内部警告全部不可见；只有 `KrtArkLinkLink` 在最终失败时取 `arklink_session_get_error`。
- 最小修复：转发到 `KrtError`/`KrtPrintf`。

#### [高] S46-16｜`KrtArkLinkLoadProjectLibraries` 忽略 `ctx` 与 `config` 一致性
- 位置：`ArkLinkIntegration.c:197-212`（`config` 可为 NULL，但 `ctx->config` 已设）
- 类别：契约破坏
- 触发条件：`config==NULL`。
- 后果：`base_dir` 保持空，退化为当前目录；与 `ctx->config` 可能不一致。
- 最小修复：用 `ctx->config` 或报错。

#### [高] S46-18｜`KrtArkLinkLinkObjects` 的 `KrtArkLinkSetOutput` 与 `KrtArkLinkAddObjectFile` 顺序依赖
- 位置：`ArkLinkIntegration.c:252-295`
- 类别：契约破坏（顺序依赖）
- 触发条件：任何链接。
- 后果：`arklink_session_set_output` 必须在 `add_input` 之后？需确认 ArkLink API 契约。
- 最小修复：（原文未给出）
- 备注：需验证——ArkLink `set_output` 与 `add_input` 的顺序要求。

#### [高] S46-19｜`KrtConsoleSupportsColor` 在 Windows 下修改全局控制台 CP 与模式，有副作用
- 位置：`Re.KrtC/src/Compiler/Driver/ConsoleUtils.c:12-35`（:14 `SetConsoleOutputCP`、:27 `SetConsoleMode`）
- 类别：共享可变状态 / 契约破坏
- 触发条件：Windows 下调用。
- 后果：探测颜色支持时永久改变进程控制台状态，影响其他输出。
- 最小修复：文档化副作用或恢复原模式。

#### [高] S46-20｜`KrtConsoleSupportsColor` 返回值语义在 Windows/non-Windows 间分叉
- 位置：`ConsoleUtils.c:12-35`（Windows 返回 0/1；non-Windows 返回 `isatty` 的 0/1）
- 类别：语义分叉（轻微）
- 触发条件：任何调用。
- 后果：`isatty` 可能返回非 0/1 值（标准只保证非零），Windows 分支只返回 0/1；调用方若用 `== 1` 判断可能不一致。
- 最小修复：non-Windows 也 `return isatty(...) ? 1 : 0;`。

#### [高] S46-21｜`KrtColor` 在 `g_color_enabled` 时返回原串，否则返回空串，但调用方可能拼接后仍计算长度
- 位置：`ConsoleUtils.c:41-43`
- 类别：契约破坏（轻微）
- 触发条件：颜色禁用时。
- 后果：空串仍参与 `printf`，无功能错误。
- 最小修复：无需修复。

#### [高] S46-22｜`g_color_enabled` 是进程级全局，无锁
- 位置：`ConsoleUtils.c:10`、`:37-39`
- 类别：共享可变状态
- 触发条件：多线程编译（`ParallelCompiler.c` 存在）。
- 后果：颜色开关竞态。
- 最小修复：文档化单线程假设或加锁。

#### [致命（原文自降级为疑似）] S47-1｜`link_kro_executable` 在 `import_kro_files` 分配失败时仍继续
- 位置：`Re.KrtC/src/Main.c:259-313`（:260-312 循环中 `imports_ok=0` 后 `break`，:315 `total_obj_count = 1 + import_kro_count` 仍按已完成的 `import_kro_count` 计算）；:316-328（`obj_files` 分配后只填 `import_kro_count` 个）；:330-341、:355；调用方 `compile_single_file:476`
- 类别：未检查前置条件 / 静默失败
- 触发条件：`import_kro_files` 分配失败（:260 `malloc` 返回 NULL → :310-312 `imports_ok=0`，但 `import_kro_count=0`）。
- 后果：`obj_files` 只含 `kro_file`，链接继续；`imports_ok=0` 在 :330 才检查，而 :330 `if (!imports_ok)` 先命中 → `result=0`、:355 返回 0，调用方 `compile_single_file:476` 判断失败 → 实际正确（原文自我推翻）。
- 最小修复：`imports_ok=0` 时立即跳过链接。
- 备注：原结论被原文自身核对推翻，**降级为疑似**；需验证 `import_kro_files` 分配失败路径的最终返回值。

#### [致命] S47-2｜`compile_multiple_files` 在 `parsed_count != input_count` 时释放 `source` 但 `ast` 未释放
- 位置：`Re.KrtC/src/Main.c:720-732`（:721-725 只 `free(source)`、`lexer_destroy`、`parser_destroy`，但 `parsed_files[i].ast` 未 `ast_destroy`）
- 类别：资源与生命周期
- 触发条件：部分文件解析失败。
- 后果：AST 泄漏（每个失败文件的 AST 节点未释放）。
- 最小修复：补 `ast_destroy(parsed_files[i].ast)`。

#### [致命] S47-3｜`compile_multiple_files` 在 `semantic_analyzer_create` 失败时同样泄漏 AST
- 位置：`Re.KrtC/src/Main.c:734-746`
- 类别：资源与生命周期
- 触发条件：`semantic_analyzer_create` 失败。
- 后果：同 S47-2（AST 泄漏）。
- 最小修复：补 `ast_destroy`。

## 第四部分　驱动 / 项目 / 并行 / Core / Memory（第四十七至五十七批）


#### [高] S47-5｜`get_kro_filename` 对长度 ≤4 的输入静默不替换扩展名
- 位置：`Re.KrtC/src/Main.c:551-559`（:556 `if (len > 4)` 才替换）
- 类别：静默失败 / 契约破坏
- 触发条件：输入文件名长度 ≤4（如 `x`，长度 1）。
- 后果：不替换扩展名，`kro_file` 与 `krt_file` 同名 → 后续 `KrtCompilePipelineExecute` 输出路径与输入相同，可能覆盖源文件。
- 最小修复：无条件替换或追加 `.kro`。
- 备注：—

#### [高] S47-6｜`get_kro_filename` 用 `strcpy(kro_file + len - 4, ".kro")` 假设扩展名是 4 字符
- 位置：`Re.KrtC/src/Main.c:556-558`
- 类别：契约破坏 / 未检查前置条件
- 触发条件：输入扩展名不是 `.krt`（如 `.txt`）或文件名无扩展名但长度 >4（如 `abcd`）。
- 后果：`kro_file + len - 4` 指向错误位置，`strcpy` 可能越界写；`abcd` → `.kro`（丢失文件名）。
- 最小修复：用 `strrchr(kro_file, '.')` 找扩展名。
- 备注：—

#### [高] S47-8｜`compile_single_file` 截断后 `snprintf` 到 `temp_kro[KRT_MAX_PATH]`，但 `KRT_MAX_PATH` 可能 < 1010
- 位置：`Re.KrtC/src/Main.c:435-443`
- 类别：未检查前置条件
- 触发条件：`KRT_MAX_PATH < 1010`。
- 后果：`snprintf` 截断，`temp_kro` 路径不完整。
- 最小修复：截断到 `KRT_MAX_PATH`。
- 备注：疑似，需验证 `KRT_MAX_PATH` 的值。

#### [高] S47-9｜`KRT_TARGET_CMD_KRO` 且无 `-o` 时 :508-519 为死代码
- 位置：`Re.KrtC/src/Main.c:366-374` 与 `:508-519`
- 类别：语义分叉 / 死代码
- 触发条件：`KRT_TARGET_CMD_KRO` 且无 `-o`。
- 后果：:368 `default_output = "output.exe"` 非空 → :509 条件不成立 → :519 不执行，`default_exe_output` 覆盖逻辑死代码。
- 最小修复：删除 :508-519 或修正条件。
- 备注：—

#### [高] S47-10｜`KRT_TARGET_CMD_KRO` 成功路径可能双重销毁 `pipeline->compiler`
- 位置：`Re.KrtC/src/Main.c:500-503` 与 `:544`
- 类别：资源与生命周期 / 契约破坏
- 触发条件：`KRT_TARGET_CMD_KRO` 成功。
- 后果：:502 `pipeline->compiler = NULL`，若 `KrtCompilePipelineDestroy` 不检查 NULL 则双重释放。
- 最小修复：确认 `KrtCompilePipelineDestroy` 检查 `compiler==NULL`，否则补检查。
- 备注：疑似，需验证 `KrtCompilePipelineDestroy`。

#### [高] S47-11｜`KRT_TARGET_CMD_EXE` 成功路径同样的双重销毁问题
- 位置：`Re.KrtC/src/Main.c:462-465`
- 类别：同 S47-10（资源与生命周期 / 契约破坏）
- 触发条件：`KRT_TARGET_CMD_EXE` 成功。
- 后果：同 S47-10。
- 最小修复：同 S47-10。
- 备注：—

#### [高] S47-12｜`#ifdef __linux__` 与非 Linux 分支产生不同中间文件扩展名
- 位置：`Re.KrtC/src/Main.c:427-444`
- 类别：配置漂移 / 语义分叉
- 触发条件：Linux vs 非 Linux。
- 后果：同一命令在不同平台产出不同中间文件（`.kro` vs `.eo`），链接路径行为不同。
- 最小修复：统一中间格式或文档化。
- 备注：—

#### [高] S47-18｜`build_project` 的 `source_paths` 含 NULL 时 `qsort` 解引用 NULL
- 位置：`Re.KrtC/src/Main.c:1186-1216`（:1188-1190 `continue`）与 `:1217`（`qsort`）
- 类别：未检查前置条件
- 触发条件：`project->sources[i]` 为空字符串。
- 后果：`source_paths[i]` 保持 NULL，`qsort` 比较函数 `strcmp(*a, *b)` 对 NULL 解引用 → 崩溃。
- 最小修复：过滤空项或报错。
- 备注：—

#### [高] S47-20｜`build_project_independent` 的 `max_threads=8` 硬编码
- 位置：`Re.KrtC/src/Main.c:932`
- 类别：配置漂移
- 触发条件：任何项目构建。
- 后果：线程数不可配。
- 最小修复：从 config 读取。
- 备注：—

#### [高] S47-21｜`obj_dir_truncated[500]`/`file_name_truncated[500]` 截断后拼接路径可能不完整
- 位置：`Re.KrtC/src/Main.c:993-1000`
- 类别：未检查前置条件
- 触发条件：长路径。
- 后果：中间对象路径错误。
- 最小修复：检查截断。
- 备注：—

#### [高] S47-23｜`aggregate_kro` 用相对路径导致临时文件泄漏
- 位置：`Re.KrtC/src/Main.c:1090` 与 `:1137`
- 类别：阶段不对齐 / 资源与生命周期
- 触发条件：`object_dir` 不是当前目录。
- 后果：:1137 `KrtDeleteFile(aggregate_kro)` 删除相对路径，但实际 `.kro` 生成在 `object_dir` 下 → 临时文件泄漏。
- 最小修复：用绝对路径或在 `object_dir` 下删除。
- 备注：—

#### [高] S47-24｜`run_compiler` 的 `is_project` 只检查 `input_files[0]`
- 位置：`Re.KrtC/src/Main.c:1271-1284`
- 类别：契约破坏
- 触发条件：多文件时只检查第一个。
- 后果：`a.krt b.project.krt` 时 `is_project=0`，走多文件路径。
- 最小修复：文档化或检查所有文件。
- 备注：—

#### [高] S47-25｜项目构建时 `final_output` 完全无效（死代码）
- 位置：`Re.KrtC/src/Main.c:1290-1301` 与 `:1219-1220`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：项目构建。
- 后果：`build_project` 的 `output_path` 参数被 `__attribute__((unused))` 忽略（:1148），实际用 `project->output_name` → `run_compiler` 计算的 `final_output` 对项目构建完全无效。
- 最小修复：删除 :1286-1301 的项目分支，或让 `build_project` 使用 `output_path`。
- 备注：—

#### [高] S47-27｜`main` 调用 `KrtOutputCacheCleanup` 但 `KrtOutputCacheInit` 从未调用
- 位置：`Re.KrtC/src/Main.c:1360-1405`
- 类别：死代码 / 契约破坏
- 触发条件：任何执行。
- 后果：`g_output_cache` 静态零初始化 `enabled=0`，`Cleanup` 调 `Flush`（空链表）安全；`Init` 是死代码。
- 最小修复：调用 `Init` 或删除。
- 备注：与 S55-2 同源（OutputCache 整套未启用）。

#### [高] S47-28｜`KrtConsoleSetColorEnabled(KrtConsoleSupportsColor())` 在 Windows 下有副作用
- 位置：`Re.KrtC/src/Main.c:1361`
- 类别：共享可变状态
- 触发条件：Windows。
- 后果：同 S46-19。
- 最小修复：同 S46-19。
- 备注：—

#### [高] S47-31｜`build` 命令会覆盖此前已设置的 `input_file`
- 位置：`Re.KrtC/src/Main.c:170-188`
- 类别：语义分叉
- 触发条件：`build` 前有输入文件参数。
- 后果：`input_file` 被覆盖。
- 最小修复：文档化。
- 备注：—

#### [高] S47-32｜`options.target_type_set` 赋值后从未使用
- 位置：`Re.KrtC/src/Main.c:198`
- 类别：死代码
- 触发条件：任何 `target` 命令。
- 后果：`target_type_set` 字段无人读取。
- 最小修复：删除。
- 备注：—

#### [高] S47-33｜`options.output_file_set` 从未使用
- 位置：`Re.KrtC/src/Main.c:222`
- 类别：死代码
- 触发条件：任何 `-o`。
- 后果：无。
- 最小修复：删除。
- 备注：—

#### [高] S47-34｜`print_usage` 的 `program_name` 参数被 `(void)` 忽略
- 位置：`Re.KrtC/src/Main.c:119-120`
- 类别：死代码 / 契约破坏
- 触发条件：任何帮助。
- 后果：用法信息不含程序名。
- 最小修复：使用 `program_name` 或删除参数。
- 备注：—

#### [高] S47-35｜`project_name_from_type("dll")` 与 `create_project` 的 `ptype` 判断不一致
- 位置：`Re.KrtC/src/Main.c:48-65` 与 `:67-87`
- 类别：语义分叉
- 触发条件：`new dll name`。
- 后果：`project_name_from_type("dll")` 返回 `"console"`（默认），但 `ptype` 是 `LIBRARY`，名字与类型不一致。
- 最小修复：统一。
- 备注：—

#### [高] S47-37｜`CLEAN`/`CHECK`/`VERSION` 目标不设 `target_type`，可能产生无意义编译
- 位置：`Re.KrtC/src/Main.c:401-406`
- 类别：死代码
- 触发条件：`clean`/`check`/`version` 目标。
- 后果：`config->target_type` 保持默认值；`VERSION` 在 `main` 提前返回，`CLEAN`/`CHECK` 走 `compile_single_file` → 可能产生无意义编译。
- 最小修复：补齐目标处理。
- 备注：疑似，需验证 `run_compiler` 对 `CLEAN`/`CHECK` 的处理。

#### [高] S47-38｜`show_ir` 是否被 pipeline 使用未确认
- 位置：`Re.KrtC/src/Main.c:409`
- 类别：疑似（契约破坏）
- 触发条件：使用 `--show-ir`。
- 后果：`show_ir` 传入 `config->show_ir`，但 `KrtCompilePipelineExecute` 是否使用需验证。
- 最小修复：核实后在 pipeline 中生效或删除参数。
- 备注：疑似，需验证。

#### [高] S48-1｜`KrtProjectKrtSave` 产物无法被 `KrtProjectKrtLoad` 解析
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:807-830`
- 类别：契约破坏 + 语义分叉
- 触发条件：调用 `KrtProjectKrtSave` 生成 project.krt 后再用 `KrtProjectKrtLoad` 读取。
- 后果：Save 写键值行（`project:`/`type:`/`output:`/`sources:`），Load（:690-748）要求 AST 中存在 `Configure` 函数并以 `Name(...)`/`Type(...)`/`Sources(...)` 调用求值（:490-580），语法完全不重叠 → `krt_proj_find_configure` 返回 NULL，报「未找到 Configure 函数」，无法往返编辑项目文件。
- 最小修复：Save 输出 `function Configure() { Name("..."); Type("..."); Sources("..."); }`，或让 Load 同时支持键值行解析。
- 备注：—

#### [中] S48-2｜Save 无条件先写一行 `type: console`
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:808-820`
- 类别：注释与实现脱节 + 阶段不对齐
- 触发条件：非 console 项目（LIBRARY/WEB/SYSTEM）。
- 后果：写出两个 `type:` 行，第一个恒为 console；Load 若按行读取则取到错误类型。
- 最小修复：删除无条件 console 行，改为只在 `KRT_PROJ_TYPE_CONSOLE` 分支写。
- 备注：—

#### [中] S48-3｜`parser_create` 失败分支未销毁 `lexer`
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:721-724`
- 类别：资源与生命周期
- 触发条件：`lexer_create` 成功后 `parser_create` 失败。
- 后果：只 `KRT_FREE(source)` + `KrtProjectKrtDestroy`，未 `lexer_destroy(lexer)` → Lexer 结构体、`source_owned` 副本、64KB arena 全部泄漏。
- 最小修复：在 `if (!parser)` 分支补 `lexer_destroy(lexer);`。
- 备注：—

#### [中] S48-4｜项目文件 `fopen` 失败静默返回 NULL
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:685-687`
- 类别：诊断缺失（静默失败）
- 触发条件：项目文件缺失/权限不足。
- 后果：直接 `return NULL`，无 `KrtError`/`KRT_WARNING`，无路径与 errno → 上层只看到空指针，无法区分「文件不存在」与「解析失败」。
- 最小修复：失败时输出含 `path` 与 `strerror(errno)` 的诊断。
- 备注：—

#### [中] S48-5｜`${unknown}` 仅告警后替换为空串继续
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:164-170`
- 类别：静默失败
- 触发条件：`Sources("${typo}/**/*.krt")` 等未知变量。
- 后果：静默展开为空 → 项目编译 0 个源文件却「成功」。
- 最小修复：展开期未知变量应视为错误，`KrtProjectKrtLoad` 返回 NULL。
- 备注：—

#### [中] S48-6｜`trim("")` 构造数组起点之前的指针（UB）
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:18-19`
- 类别：未检查前置条件
- 触发条件：`project.krt` 中 `Sources("")` 或空 token。
- 后果：`e = s + strlen(s) - 1 == s - 1`，构造数组起点之前的指针（C 标准 UB）；`while (e > s)` 虽不解引用，但指针算术本身越界。
- 最小修复：`if (*s == '\0') return s;` 前置。
- 备注：—

#### [中] S48-7｜条件表达式只支持 `==`/`!=` 与裸标识符，其余恒为假
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:455-465`
- 类别：静默失败 + 语义分叉
- 触发条件：`if (config == "release" && os == "linux")` 等含 `>`/`>=`/`&&`/`||` 的条件。
- 后果：一律落到 `return 0`（:465），配置块被静默跳过。
- 最小修复：对不支持的运算符返回「未知」而非 false，并给出诊断。
- 备注：—

#### [中] S48-8｜未定义变量在条件比较中被当作空串
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:461`
- 类别：静默失败
- 触发条件：`${undefined} == ""`。
- 后果：`strcmp(lval ? lval : "", rval ? rval : "")` 使未定义变量判真。
- 最小修复：lval/rval 任一为 NULL 时判条件不成立并报错。
- 备注：—

#### [中] S48-9｜`Type("bogus")` 返回值被丢弃，类型静默回退 console
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:526-529`
- 类别：静默失败
- 触发条件：非法项目类型。
- 后果：`parse_project_type` 返回 0，返回值被丢弃，`cfg->type` 保持旧值，用户得不到任何提示。
- 最小修复：检查返回值并 `KrtError`。
- 备注：—

#### [中] S48-10｜`Sources` 与 `Libraries` 使用不同展开策略
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:554` vs `:572`
- 类别：语义分叉
- 触发条件：`Libraries("libs/*")`。
- 后果：`Sources` 走 `krt_proj_add_sources`（支持 `*`/`?` glob、递归展开目录），`Libraries` 走 `krt_proj_add_libraries`（仅 `krt_list_add`，字面量）→ glob 被当作字面文件名，链接期报「库不存在」。
- 最小修复：统一为同一展开函数，或在文档中明确区分并拒绝 glob。
- 备注：—

#### [中] S48-11｜变量声明只处理字符串，数字/表达式/布尔展开为空
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:595-600`
- 类别：静默失败
- 触发条件：`var n = 3;` 后在 `${n}` 处展开。
- 后果：`AST_VARIABLE_DECLARATION` 只处理 `value->type == AST_STRING`，其余一律被设为 `""`。
- 最小修复：至少支持 `AST_INTEGER`/`AST_BOOLEAN`，其余报诊断。
- 备注：—

#### [中] S48-12｜`opendir` 失败静默返回，项目 0 源文件却成功
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:311-313`
- 类别：静默失败
- 触发条件：`Sources("nonexistent/**/*.krt")`。
- 后果：静默产出 0 个源文件，项目「编译成功」但无输出。
- 最小修复：目录不存在时 `KrtError` 并报路径。
- 备注：—

#### [中] S48-13｜发布路径无条件打印 `[KrtProj.debug]` 到 stderr
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:762-770`
- 类别：死代码（调试残留）
- 触发条件：任何发布路径执行。
- 后果：污染 CLI 输出、泄露项目结构，且无法关闭。
- 最小修复：删除，或置于 `KRT_DEBUG` 宏内。
- 备注：—

#### [中] S48-14｜`krt_proj_eval_top_level_vars` 不递归，命名空间内变量展开为空
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:644-652`
- 类别：静默失败
- 触发条件：命名空间/块内声明 `var`。
- 后果：只扫描 `AST_PROGRAM` 顶层变量，嵌套 namespace/block 不递归 → 值静默为空。
- 最小修复：递归遍历或明确文档化仅支持顶层。
- 备注：—

#### [高] S48-15｜`KRT_REALLOC` 失败分支实为活路径（原判死代码）
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:131-134`、`:228-230`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）
- 触发条件：OOM（`KRT_REALLOC` 失败返回 NULL）。
- 后果：`krt_proj_set_var`/`krt_list_add` 的失败分支不再不可达；realloc 失败 → NULL 静默传播/字段为 NULL，零诊断。
- 最小修复：调用点检查 NULL 并报诊断，或改为断言。
- 备注：【修订 G1】原结论「`KRT_REALLOC` 失败即 `exit(1)`，分支不可达」作废，重定为高。

#### [轻微] S48-16｜`krt_proj_expand` 的 `close==NULL` 分支写入未做容量检查
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:213-215`
- 类别：未检查前置条件
- 触发条件：`${` 后无 `}`（`close==NULL`）。
- 后果：`buf[len++] = *p++` 未做容量检查（仅 `copy_literal` 分支检查）；当前输入下不会越界，但依赖隐式不变量。
- 最小修复：在该分支复用 `copy_literal` 的容量检查。
- 备注：—

#### [高] S49-1｜`KrtProjRemoveFile` 仅有声明、无实现
- 位置：`Re.KrtC/src/Compiler/Driver/Project.h:58`
- 类别：死代码 / 契约破坏
- 触发条件：调用 `KrtProjRemoveFile`。
- 后果：全项目无任何实现（`grep -rn` 仅命中头文件）→ 调用即链接失败；同时 `KrtProject.items` 链表只有头插（Project.c:455-457）无任何删除路径，项目模型无法移除文件。
- 最小修复：实现该函数（遍历链表、摘链、释放 `file_path`/`item_type`），或删除声明。
- 备注：—

#### [中] S49-2｜`project_root` 存原始空串导致路径拼接错误
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:554-556`
- 类别：阶段不对齐
- 触发条件：`KrtProjCreateTemplate` 且 `output_dir == ""`。
- 后果：局部 `output_dir` 被改写为 `project->name`，但 `project->project_root` 存的是原始空串 → 后续 `KrtProjGetOutputPath`/依赖 `project_root` 的代码看到空根目录。
- 最小修复：先归一化 `output_dir`，再写 `project_root`。
- 备注：—

#### [中] S49-3｜`KrtProjGetOutputPath`/`KrtProjGetIntermediatePath` 返回内部指针
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:758-781`
- 类别：共享可变状态 / 生命周期
- 触发条件：调用方 `KRT_FREE` 或修改返回值。
- 后果：与 `KrtProjDestroy` 的 `KRT_FREE(prop->output_path)`（Project.c:418）形成双重释放风险。
- 最小修复：返回 `KRT_STRDUP` 副本，或改为 `const char*` 并在头文件文档化「不可释放」。
- 备注：—

#### [中] S49-4｜`copy_directory_recursive` 无深度限制、不检测符号链接环
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:91-190`
- 类别：未检查前置条件（递归无界）
- 触发条件：stdlib 目录存在符号链接环或层级过深（`KrtProjCreateTemplate` 用它复制整个 stdlib 树，Project.c:741）。
- 后果：无限递归/栈溢出。
- 最小修复：加 depth 上限（如 64）与 `lstat`+`S_ISLNK` 跳过符号链接。
- 备注：—

#### [中] S49-5｜WEB/SYSTEM 项目输出类型被设为 `"exe"`
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:348`
- 类别：阶段不对齐
- 触发条件：`KRT_PROJ_TYPE_WEB`/`SYSTEM`。
- 后果：`KrtProjCreate` 仅按 `type == LIBRARY` 设 `"dll"`，否则一律 `"exe"` → 与实际后端不一致（配置漂移）。
- 最小修复：为 WEB/SYSTEM 分别设定输出类型，或删除该字段统一由后端决定。
- 备注：—

#### [中] S49-6｜`readlink("/proc/self/exe")` 失败静默返回 0
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:306-309`
- 类别：诊断缺失
- 触发条件：非 Linux 容器、proc 未挂载等。
- 后果：调用方只报「无法找到捆绑标准库」，丢失真实原因。
- 最小修复：失败时输出 `errno` 诊断。
- 备注：—

#### [中] S49-7｜生成 project.krt 时直接插值文件名，未走字符串字面量转义
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:714-718`
- 类别：静默失败
- 触发条件：文件名含 `"` 或 `\`。
- 后果：`fprintf(pk, "\"%s\"", tok)` 生成的 project.krt 语法被破坏，且无诊断。
- 最小修复：改用 `write_krt_string_literal`。
- 备注：—

#### [中] S49-8｜源文件顺序在两条路径下相反
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:455-457` + `:491-520`
- 类别：语义分叉
- 触发条件：`KrtProjAddFile` 与 `KrtProjectKrtLoad` 两条路径。
- 后果：`KrtProjAddFile` 头插、`KrtProjGetSourceFiles` 按链表顺序输出 → 顺序与添加顺序相反；`Sources` 按书写顺序保留 → 同一项目编译顺序不同，链接符号解析/初始化顺序可能不同。
- 最小修复：`KrtProjAddFile` 尾插，或 `KrtProjGetSourceFiles` 反转。
- 备注：—

#### [中] S49-9｜`localtime()` 非线程安全（疑似数据竞争）
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:356`、`:443`
- 类别：共享可变状态
- 触发条件：`ParallelCompiler` 并发调用 `KrtProjCreate`/`KrtProjAddFile`。
- 后果：`localtime()` 返回静态缓冲区指针，产生数据竞争。
- 最小修复：改用 `localtime_r`。
- 备注：疑似，需验证 `ParallelCompiler.c` 是否在同一进程内并发调用这两个函数。

#### [中] S49-10｜`Configure(Project p)` 形参在求值期完全无意义（契约未文档化）
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:686` vs `ProjectKrt.c:690-748`
- 类别：契约破坏
- 触发条件：使用 `KrtProjCreateTemplate` 生成的模板。
- 后果：`krt_proj_find_configure` 只匹配函数名、不校验参数，`krt_proj_eval_block` 从未绑定 `p`；无参 `Configure()` 同样被接受。已实证当前模板可正确加载（`p.Name(...)` 被解析为 call name `Name`），但契约未文档化。
- 最小修复：在 `krt_proj_find_configure` 中断言参数列表为空或忽略，并在文档中固定模板形式。
- 备注：疑似（当前可用，契约缺陷）。

#### [中] S50-1｜worker 丢弃 `config->imported_files` 可能泄漏/双重释放
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:100-104`
- 类别：资源与生命周期
- 触发条件：调用方在 `ParallelCompilerCreate` 前已向 `config->imported_files` 填入内容。
- 后果：worker 拷贝 config 后强制 `imported_files = NULL` 并清零计数，丢弃 `compiler->config` 已有数组指针；worker 只释放自己重建的数组（:120-123）→ 泄漏；原 `config` 仍持有该指针，可能与 worker 的释放形成双重释放。
- 最小修复：worker 内改用局部 `KrtConfig config = *compiler->config;` 后不清空，或明确文档化「config 的 imported_files 由 pipeline 接管」。
- 备注：疑似，需验证 `KrtCompilePipelineExecute` 是否写该字段、调用方是否预置。

#### [中] S50-2｜`run_nasm` 失败不捕获 nasm 的 stderr/退出码
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:76-80`
- 类别：诊断缺失
- 触发条件：nasm 汇编失败。
- 后果：只报「汇编失败: %s -> %s」，无法定位是语法错误、宏未定义还是 nasm 缺失。
- 最小修复：在 error_msg 中附带 `WEXITSTATUS(status)`，并将 nasm stderr 重定向到临时文件后回显。
- 备注：—

#### [中] S50-3｜`FindRuntimeObj` 未命中时返回未解析原始名
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:397`
- 类别：静默失败
- 触发条件：所有搜索路径都未命中。
- 后果：兜底 `KRT_STRCPY_S(result, size, obj_name)` 返回未解析的原始名 → 调用方据此链接，报错「找不到对象文件」而非「运行时库缺失」，诊断与实际原因不符。
- 最小修复：返回状态码或置 `result[0]='\0'` 并由调用方报错。
- 备注：与 S54-6 叠加。

#### [中] S50-4｜泛型类型发现用纯文本 `strstr` 扫描，不区分注释/字符串
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:453-467`
- 类别：语义分叉 / 误报
- 触发条件：注释或字符串里出现 `template class Foo`。
- 后果：注册不存在的泛型类型；与 `Generics.c` 的真正词法/语义注册（`generics_register_type`）形成两套发现机制。
- 最小修复：复用词法分析器或至少跳过注释/字符串区间。
- 备注：—

#### [中] S50-5｜`generics_register_type` 传 `params=NULL` 但 `param_count` 可能 >0
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:536-539`
- 类别：契约破坏
- 触发条件：`<...>` 逗号计数得出 `param_count > 0`。
- 后果：与 `Generics.c` 中 `param_count>0` 时访问 `params[i]` 的假设冲突（S43-10），实例化时越界。
- 最小修复：`params` 传 NULL 时强制 `param_count=0`，或在注册期校验。
- 备注：—

#### [中] S50-6｜worker 数由源码字节数估算，与 `max_threads` 无关
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:275-287`
- 类别：配置漂移（未文档化）
- 触发条件：任何并行编译。
- 后果：worker 数由 `source_bytes / 64KB` 估算，小于 64KB 的项目永远单线程；同一项目在不同机器/源文件大小时行为（耗时、日志顺序、错误顺序）不可预测。
- 最小修复：`workers` 至少为 1 且由 `max_threads` 直接控制，或文档化阈值。
- 备注：—

#### [高] S50-7｜`KRT_MALLOC`/`KRT_REALLOC` 失败分支实为活路径（原判死代码）
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:133-137`、`:190-193`、`:228-232`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）
- 触发条件：OOM（分配失败返回 NULL）。
- 后果：失败分支不再不可达；调用点若不检查即 NULL 解引用或静默 NULL 传播，零诊断。
- 最小修复：调用点检查 NULL 并报诊断，或改为断言。
- 备注：【修订 G1】原结论「失败即 `exit(1)`，分支不可达」作废，重定为高。

#### [中] S50-8｜运行时对象搜索顺序与库搜索顺序是两套硬编码列表
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:292-320` vs `ProjectKrt.c:896-953`
- 类别：语义分叉
- 触发条件：运行时/库路径维护时。
- 后果：`obj/runtime`、`obj/core/utils`… 与 `libs`、`libs/System`… 无共享常量，易漂移。
- 最小修复：提取共享的搜索路径表。
- 备注：—

#### [高] S51-1｜错误报告永不显示行列（`error_line`/`error_column` 从未赋值）
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:557-585`
- 类别：诊断缺失 + 死代码
- 触发条件：任何编译错误。
- 后果：`KrtCompilePipelinePrintErrorReport` 的「错误位置」分支读 `pipeline->error_line`/`error_column`，但全代码库这两个字段从未被赋值（`grep -rn "error_line\s*="` 仅命中 `Tokenizer.c` 的 `lexer->error_line`）→ 报告永远只显示「失败阶段」，`^ 错误在这里` 永不出现。
- 最小修复：在 `KrtCompilePipelineLex`/`Parse`/`Semantic` 失败时从 `lexer->error_line`、`parser->error_line` 写入，或删除该分支。
- 备注：—

#### [中] S51-2｜`lexer_create` 后检查 `lexer->error_count` 恒假
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:249-256`
- 类别：死代码（恒假条件）
- 触发条件：词法错误（未闭合字符串/字符）。
- 后果：`lexer_create`（Tokenizer.c:528-555）只初始化字段、不扫描，`error_count` 恒为 0 → 词法错误被放过，延后到 Parse 阶段才以「unterminated block comment or literal」报出，诊断与实际阶段不符。
- 最小修复：在 Lex 阶段触发一次完整扫描，或删除该恒假检查。
- 备注：—

#### [中] S51-3｜`parser_create` 后检查 `lexer->error_count` 同样恒假
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:281-288`、`:291-298`
- 类别：死代码（恒假条件）
- 触发条件：解析前。
- 后果：`parser_create` 不扫描，`lexer->error_count` 仍恒为 0 → 两处检查均不可达。
- 最小修复：合并为 `parser_parse` 之后的一次检查。
- 备注：—

#### [中] S51-4｜`require_entry_point` 由输出文件名后缀决定
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:345-349`
- 类别：契约破坏 / 配置漂移
- 触发条件：输出名不是 `.kro` 结尾（如 `foo.kro.bak`）。
- 后果：`ol >= 4 && strcmp(of + ol - 4, ".kro") == 0` 使改名即改变语义，且未文档化。
- 最小修复：改用 `pipeline->config->target_type == KRT_TARGET_KRO_OBJ` 显式判断。
- 备注：—

#### [中] S51-5｜`pipeline->input_file`/`output_file` 直接保存调用方指针
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:449-450`
- 类别：契约破坏（悬垂指针）
- 触发条件：调用方传栈上字符串（如局部缓冲）。
- 后果：不 `KRT_STRDUP`，`pipeline` 持有悬垂指针；`record_stage_result` 的 `file_name` 同样保存该指针。
- 最小修复：Create/Execute 时 `KRT_STRDUP`，Reset 时释放。
- 备注：—

#### [中] S51-6｜`record_stage_result` 在 `stage_count >= 8` 时静默丢弃
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:45-47`
- 类别：静默失败
- 触发条件：阶段数超过 8。
- 后果：直接 return，丢弃后续阶段结果，无诊断 → 阶段统计被静默截断。
- 最小修复：扩容或报错。
- 备注：—

#### [中] S51-7｜`GetStageResults`/`GetImportedFiles` 返回内部数组指针
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:505-512`、`:681-686`
- 类别：共享可变状态
- 触发条件：调用方修改或释放返回值。
- 后果：内部数组可被外部修改/释放。
- 最小修复：返回 `const` 或副本。
- 备注：—

#### [高] S51-8｜`config->target_type` 无 NULL 检查（G1 后确认为活路径）
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:383-389`
- 类别：未检查前置条件（原疑似）
- 触发条件：`KrtCompilePipelineCreate` 收到 NULL config（`KrtConfigCreate()` 失败返回 NULL）。
- 后果：`pipeline->config->target_type` 无 NULL 检查 → NULL 解引用崩溃，零诊断。
- 最小修复：Create 时断言 `config != NULL`，并检查 `Main.c` 三处 `KrtConfigCreate()` 返回值。
- 备注：【修订 G1】原备注「若内部用 `KRT_MALLOC` 则失败即 exit，不会返回 NULL」作废；G1 确认 `KRT_MALLOC` 失败返回 NULL，该路径为活路径，重定为高。

#### [高] S51-9｜`KRT_MALLOC` 失败分支实为活路径（原判死代码）
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:63-65`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）
- 触发条件：OOM。
- 后果：失败分支不再不可达；不检查即 NULL 解引用或静默 NULL 传播，零诊断。
- 最小修复：调用点检查 NULL 并报诊断，或改为断言。
- 备注：【修订 G1】原结论「失败即 `exit(1)`，分支不可达」作废，重定为高。

#### [高] S52-1｜`KrtConfigCreateProject` 零调用者且模板内容非法
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:162-218`
- 类别：死代码 + 语义分叉
- 触发条件：若被调用。
- 后果：生成 `.esproj` + `main.es` 模板，内容为 `function int main() { ... return 0; }`——该形式已被确认非法/被误解析（S30-1），正确形式为 `int32 main()` 或 `function main()`；与 `KrtProjCreateTemplate`（Project.c:539，生成 project.krt + main.krt）构成两套互不相通的项目模板系统，本路径产出的项目必然编译失败。
- 最小修复：删除 `KrtConfigCreateProject` 及 `.esproj` 相关代码，统一到 `KrtProjCreateTemplate`。
- 备注：—

#### [高] S52-2｜Windows 对象/库数组初始化为 `{NULL}` 导致计数为 1
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:21-26`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：按 `object_count`/`library_count` 遍历 `linker_config->object_files[i]`。
- 后果：四个 Windows 数组初始化为 `{NULL}`，`sizeof(...)/sizeof(char*)` 计数为 1，唯一元素是 NULL → 解引用 NULL。当前 `linker_config` 只被读取 `entry_point`（ArkLinkIntegration.c:64-66），尚未触发，但契约是错的。
- 最小修复：改为空数组 `{0}` 且计数 0，或在读取处判 NULL。
- 备注：—

#### [中] S52-3｜`KrtLinkerConfig` 六个字段全项目无读取
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.h:14-27` + `ConfigManager.c:27-34`
- 类别：死字段
- 触发条件：任何链接配置。
- 后果：`object_files`/`end_objects`/`library_paths`/`libraries`/`subsystem`/`dynamic_linker` 全项目无任何读取（`grep` 仅命中 `entry_point`）→ 链接配置形同虚设，Windows/Linux 行为无差异，维护者易误以为生效。
- 最小修复：删除未使用字段，或实现其语义。
- 备注：—

#### [中] S52-4｜`KrtConfigGetDefaultOutput` 未覆盖 VM/KRO 且零调用者
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:129-142`
- 类别：阶段不对齐
- 触发条件：`KRT_TARGET_VM`、`KRT_TARGET_KRO`。
- 后果：switch 只覆盖 EXE/IR/ASM，VM/KRO 落 `default` 返回 `"output.asm"`，默认输出名与实际产物不符；该函数零调用者（死代码）。
- 最小修复：补齐枚举分支并加 `default` 断言。
- 备注：—

#### [中] S52-5｜「是否需要链接」两处判断不一致
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:145-147`
- 类别：语义分叉
- 触发条件：KRO 目标。
- 后果：`KrtConfigNeedsLinking` 仅 `KRT_TARGET_EXE` 需要链接，而 `Main.c` 对 KRO 也走链接路径（`link_kro_executable`，Main.c:247）；该函数零调用者。
- 最小修复：统一为一个函数，或删除。
- 备注：—

#### [中] S52-6｜`KrtConfigGetLinkerConfig` 的 `default` 静默按 Linux 处理
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:99-107`
- 类别：静默降级
- 触发条件：平台探测失败（`KRT_CONFIG_PLATFORM_UNKNOWN`）。
- 后果：`default` 分支返回 Linux 配置，链接参数错误且无诊断。
- 最小修复：`default` 返回 NULL 并由调用方报错。
- 备注：—

#### [中] S52-7｜导入文件去重表有两套并行实现
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:67-93`
- 类别：语义分叉
- 触发条件：维护/排错时。
- 后果：与 `CompilerPipeline.c:658-686` 的 `KrtCompilePipelineAddImportedFile` 是两套实现：前者无 `INT_MAX/2` 与 `SIZE_MAX` 溢出检查，后者有；前者 `KRT_STRDUP` 失败返回 void，后者返回 false → 同一概念两套语义与错误处理。
- 最小修复：提取共享实现。
- 备注：—

#### [中] S52-8｜`imported_file_capacity * 2` 可能整数溢出
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:67-70`
- 类别：未检查前置条件
- 触发条件：容量翻倍时溢出。
- 后果：无上限检查 → 整数溢出。
- 最小修复：加入与 Pipeline 版本一致的溢出检查。
- 备注：—

#### [高] S52-9｜`KRT_MALLOC` 失败分支实为活路径；`keep_temp_files` 默认值与文档不符
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:42-44`、`:46-49`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）+ 配置漂移
- 触发条件：OOM；以及任何使用临时文件清理的构建。
- 后果：失败分支不再不可达 → OOM 时 NULL 传播，零诊断；另 `keep_temp_files = 1` 默认保留临时文件，与 CLI 文档（`--keep-temp` 显式开启）不一致。
- 最小修复：调用点检查 NULL 并报诊断/断言；默认值与文档对齐。
- 备注：【修订 G1】原结论「失败即 `exit(1)`，分支不可达」作废，重定为高。

#### [高] S53-1｜宏注册失败被忽略，错误推迟为「未定义符号」
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:69-78`、`CompilerPipeline.c:31-38`
- 类别：静默失败 + 诊断缺失
- 触发条件：宏名非法（空串、数字开头、含非标识符字符）。
- 后果：`PreprocessorAddMacro` 返回 `false`，但唯一调用方 `load_standard_macros` 三次调用全部忽略返回值，`PreprocessorProcess` 对未注册宏名静默原样透传 → 宏注册失败零诊断，错误推迟到词法/语义阶段表现为「未定义符号 `Console.WriteLine`」，诊断与实际错误不符。
- 最小修复：`load_standard_macros` 检查返回值并用 `KrtError` 报出宏名；或让 `PreprocessorAddMacro` 对非法名直接 `KRT_COMPILE_ERROR`。
- 备注：—

#### [高] S53-2｜宏展开只做一轮，不递归
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:159-170`
- 类别：语义分叉
- 触发条件：宏 A 的替换文本中出现宏 B 的名字（含 `PreprocessorAddMacro(pp,"Console","X")` 后使用 `println`）。
- 后果：展开只做一轮，替换结果不再参与宏查找 → 与标准预处理器递归展开语义分叉；当前 3 个标准宏替换文本中的 `Console`/`WriteLine` 恰好不是宏，暂不可触发，但 API 是 public（`Preprocessor.h:24`）。
- 最小修复：在 `append` 后把替换文本重新入队扫描；或显式文档化「不递归展开」。
- 备注：—

#### [高] S53-3｜`PreprocessorCreate` 无 NULL 检查，调用方 NULL 检查的契约不一致（G1）
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:17-19` vs `CompilerPipeline.c:205-207`
- 类别：静默失败 / 契约破坏（原判「死代码」）
- 触发条件：OOM（`KRT_CALLOC` 失败返回 NULL）。
- 后果：`PreprocessorCreate` 直接返回 `KRT_CALLOC` 结果、无 NULL 检查；调用方检查 `if (!preprocessor)` 不再是死代码，但失败时零诊断 → 静默 NULL 传播。
- 最小修复：统一为「分配失败即 `KRT_COMPILE_ERROR`」或统一返回 NULL 并由调用方处理；Create 内报诊断。
- 备注：【修订 G1】原结论「`KRT_CALLOC` 失败即 `exit(1)`，调用方检查为死代码」作废，重定为高。

#### [高] S53-4｜宏表扩容/`KRT_REALLOC`/`KRT_STRDUP` 失败仅 `return false` 无诊断
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:91-94`、`:98-101`、`:106-109`
- 类别：静默失败
- 触发条件：OOM。
- 后果：失败时仅 `return false`，无诊断，调用方忽略 → 宏注册/复制被静默吞掉，编译继续。
- 最小修复：加 `KrtError("Preprocessor: macro table grow failed")` 等诊断。
- 备注：【修订 G1】G1 确认失败返回 NULL 而非 exit，上述路径为活路径，重定为高。

#### [高] S53-5｜无宏时 `KRT_STRDUP(source)` 失败被误报为「预处理失败」
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:124-126` + `CompilerPipeline.c:216-227`
- 类别：静默失败 + 诊断与实际不符
- 触发条件：`macro_count == 0` 且 OOM。
- 后果：`KRT_STRDUP(source)` 失败返回 NULL，调用方报「预处理失败: <文件>」，丢失真实原因（内存分配失败）。
- 最小修复：`PreprocessorProcess` 用 out-param 区分「分配失败」与「无宏」，调用方在报文中带上阶段与原因。
- 备注：【修订 G1】G1 确认该路径为活路径，重定为高。

#### [中] S53-6｜扩容条件恒真且 `bucket_count * 2` 极端可溢出
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:91`
- 类别：死代码 / 恒真条件
- 触发条件：`bucket_count == 0`（首次）。
- 后果：`(macro_count + 1) * 4 >= bucket_count * 3` 恒真 → 首次必扩容，逻辑结果正确但把「空表」与「负载」混在一条表达式里；`bucket_count` 为 `int`，`:51` 的 `bucket_count * 2` 极端情况下可溢出（不可达）。
- 最小修复：`if (bucket_count == 0 || (macro_count + 1) * 4 >= bucket_count * 3)`。
- 备注：—

#### [中] S53-7｜`cursor[1]` 越界依赖隐式 NUL 结尾
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:139`
- 类别：未检查前置条件（轻微）
- 触发条件：`source` 非 NUL 结尾。
- 后果：`if (*cursor == '\\' && cursor[1])` 越界；实际由 `CompilerPipeline.c:191` 的 `source_code[read_bytes] = '\0'` 保证。
- 最小修复：`if (*cursor == '\\' && cursor[1] != '\0')`。
- 备注：—

#### [高] S54-1｜FAILED 分支重复计数导致构建摘要自相矛盾
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:32-35` + `:100`
- 类别：诊断与实际不符
- 触发条件：任何任务失败。
- 后果：FAILED 分支同时执行 `stats->executed++` 和 `stats->failed++`，而 `KrtPrintBuildSummary` 的 `total_tasks = executed + up_to_date + skipped` 不含 failed → 例如「2 tasks: 2 executed, 1 failed」。
- 最小修复：FAILED 分支去掉 `executed++`，或 `total_tasks` 加上 `failed`。
- 备注：—

#### [高] S54-2｜`KrtBuildSummaryAccumulate` 零调用者
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:85-97`
- 类别：死代码
- 触发条件：任何时刻。
- 后果：全代码库零调用者（仅 `TaskManager.h:28` 声明）。
- 最小修复：删除，或在 `Main.c` 中实际使用。
- 备注：—

#### [中] S54-3｜`if (total_tasks > 0)` 恒真
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:119`
- 类别：死代码
- 触发条件：任何时刻。
- 后果：在 `:101-103` 提前 return 之后恒为真。
- 最小修复：删除冗余判断。
- 备注：—

#### [中] S54-4｜`!stage || !stats` 静默丢弃整条报告
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:12-14`
- 类别：静默失败
- 触发条件：参数为 NULL。
- 后果：静默丢弃报告，无诊断；当前 `Main.c` 所有调用点传 `KrtGetGlobalTaskStats()`（非 NULL），但契约上无保护。
- 最小修复：参数非法时 `KrtError`。
- 备注：—

#### [中] S54-5｜对象文件添加失败不报文件名
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:29-39`
- 类别：静默失败 + 诊断缺失
- 触发条件：某任务 `obj_file`/`output_file` 不存在，或 ArkLink 拒绝该输入。
- 后果：`KrtArkLinkAddObjectFile` 返回非 0 时只不增加 `success_count`，不报出是哪个 .o 失败；`file_to_link == NULL` 时静默跳过 → 最终只报「No object files to link」，无法定位。
- 最小修复：失败时 `KrtError("Failed to add object file: %s", file_to_link)`。
- 备注：—

#### [中] S54-6｜运行时 .o 添加全部忽略返回值
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:34` vs `:58-61`
- 类别：错误处理不一致
- 触发条件：运行时 .o 缺失或 ArkLink 拒绝。
- 后果：`:34` 检查 `KrtArkLinkAddObjectFile` 返回值，而 `:58-61` 对 `runtime.o`/`output_cache.o`/`allocator.o`/`KrtString.o` 的添加全部忽略返回值 → 静默缺少运行时对象，最后只报「ArkLink linking failed」，不报「找不到 runtime.o」（与 S50-3 叠加）。
- 最小修复：统一检查四个返回值并报出文件名。
- 备注：—

#### [中] S54-7｜`KrtArkLinkLoadProjectLibraries` 失败无具体原因
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:64-67`
- 类别：诊断缺失
- 触发条件：库加载失败。
- 后果：直接 `return -1`，无具体原因；上层 `Main.c:1060` 只报「链接失败」→ 哪个库、什么错误全部丢失。
- 最小修复：此处 `KrtError` 报出 config 的库列表或 ArkLink 错误码。
- 备注：—

#### [中] S54-8｜ArkLink 全部诊断被空操作 logger 丢弃
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:79-82` + `ArkLinkIntegration.c:24-28`
- 类别：诊断缺失（根因）
- 触发条件：链接失败。
- 后果：`link_result != 0` 只报「ArkLink linking failed」，未取内部错误；`ark_link_logger_callback` 是空操作（`(void)message;`）→ 所有 ArkLink 诊断被丢弃（即 S46-14 的具体后果）。
- 最小修复：实现 logger 回调输出到 stderr，或提供 `arklink_session_get_last_error` 并在链接失败时打印。
- 备注：—

#### [中] S54-9｜对 KRO 字节码也执行 `chmod 0755`
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:84-87`
- 类别：语义不当
- 触发条件：`KRT_TARGET_KRO`。
- 后果：对字节码（非可执行）也 chmod。
- 最小修复：仅在 `target_type` 为 EXE/DLL 时 chmod。
- 备注：—

#### [中] S54-10｜`success_count == 0` 三种原因同一报文
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:41-45`
- 类别：诊断与实际不符
- 触发条件：所有任务 `has_object_code` 为假 / 所有 `file_to_link` 为 NULL / 所有添加失败。
- 后果：统一报「No object files to link」，三种原因无法区分。
- 最小修复：分别计数并给出对应报文。
- 备注：—

#### [致命] S55-1｜`Error.h` 的 include guard 撞名导致 `StackCalculator.h` 被跳过
- 位置：`Re.KrtC/src/Core/Utils/Error.h:1-2` + `StackCalculator.h:1-2` + `Core.h:13,15`
- 类别：契约破坏 / 静默失败
- 触发条件：任何经 `Core.h` 引用 `KrtStack*` 类型的代码。
- 后果：`Error.h` 的 guard 被写成 `#ifndef KRT_STACK_CALCULATOR_H`/`#define KRT_STACK_CALCULATOR_H`（复制粘贴自 StackCalculator.h）；`Core.h:13` 先包含 `Utils/Error.h`（占用 guard），`Core.h:15` 再包含 `Utils/StackCalculator.h` 时整个文件被跳过 → `KrtStackAnalyzer`/`KrtStackFrame` 类型与全部 API 声明在所有经 `Core.h` 的 TU 中不可见 → 编译期报错或（更糟）隐式声明；当前因 StackCalculator 整套零调用者而未暴露。
- 最小修复：把 `Error.h:1-2` 改为 `KRT_ERROR_H`；同时删除空的 Error.h/Error.c（见 S55-3）。
- 备注：—

#### [高] S55-2｜整套 OutputCache 是死功能
- 位置：`Re.KrtC/src/Core/Utils/OutputCache.c:103-131`、`:199-201`、`KrtCommon.h:14-58`
- 类别：死代码 + 配置漂移
- 触发条件：任何时刻。
- 后果：`KrtOutputCacheSetEnabled` 全代码库零调用者 → `g_output_cache.enabled` 恒为 0（`KrtOutputCacheInit` 也零调用者，静态零初始化）→ `output_cache_add_internal` 永远走 `:87-101` 直接打印分支，`:103-131` 链表队列路径永不执行；`KrtError`/`KRT_WARNING` 的 cache 分支、`KrtOutputCacheCleanup`/`Init`/`Clear` 全是死代码。`KRT_COMPILE_ERROR` 不经过 cache 因此「碰巧正确」，属巧合而非设计。
- 最小修复：接入 `KrtOutputCacheSetEnabled` 并测试缓冲语义，或删除整条 cache 路径。
- 备注：与 S47-27 同源。

#### [高] S55-3｜`Error.h`/`Error.c` 是空模块
- 位置：`Re.KrtC/src/Core/Utils/Error.h`（6 行，无任何声明）+ `Error.c`（6 行，只有 `#include`，无任何定义）
- 类别：死代码 / 注释与实现脱节
- 触发条件：任何时刻。
- 后果：空模块；`Error.c` 编译产出空目标文件；文件名 `Error` 暗示存在错误处理 API，实际不存在。
- 最小修复：删除两文件及 `Core.h:13` 的 include。
- 备注：与 S55-1 联动。

#### [高] S55-4｜`KRT_STRNCPY` 在 `n == 0` 时缓冲区下溢写
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:113-117`
- 类别：未检查前置条件（潜在 UB）
- 触发条件：以 `n = 0` 调用 `KRT_STRNCPY`（当前 4 个调用点均传 `sizeof(...)` 或 `sizeof(...)-1`，未触发）。
- 后果：`n == 0` 时 `(dest)[(n) - 1] = '\0'` → **缓冲区下溢写**（UB / 内存破坏）；宏也不校验 `n <= sizeof(dest)`。
- 最小修复：`if ((n) > 0) { strncpy(...); (dest)[(n)-1] = '\0'; }`，并用 `sizeof(dest)` 做静态断言。
- 备注：—

#### [高] S55-5｜两套并行 Path API，所有权模型相反
- 位置：`Re.KrtC/src/Core/Utils/Path.h:8-18` vs `KrtCommon.h:220-224`
- 类别：语义分叉
- 触发条件：开发者按名字猜测 API，选错一套。
- 后果：`Path.h` 声明 `KrtGetFilename`/`KrtJoinPath`（返回 `char*`，堆分配）；`KrtCommon.h` 声明 `KrtPathGetFilename`/`KrtPathJoin`（调用方缓冲）；两套名称近似、语义不同、内存所有权模型相反，且 `KrtPathExists`/`KrtTimeNowSeconds` 在两个头文件重复声明 → 所有权混淆，泄漏或双重释放。
- 最小修复：合并为唯一一套（建议保留 Path.h 的堆分配版），删除 `KrtCommon.h:220-224` 的重复声明。
- 备注：—

#### [高] S55-6｜`Path.h` 六个函数只有声明、无定义
- 位置：`Re.KrtC/src/Core/Utils/Path.h:11-17`
- 类别：死代码 / 未实现声明
- 触发条件：调用 `KrtGetFilename`、`KrtJoinPath`、`KrtResolvePath`、`KrtGetExtension`、`KrtReadFile`、`KrtWriteFile` 中任一个。
- 后果：全代码库无任何定义（`KrtGetDirectory`/`KrtGetCurrentDirectory`/`KrtGetExecutableDirectory` 有定义）→ **链接错误**。
- 最小修复：实现或删除；若保留 Path.h 为唯一 API，则必须实现。
- 备注：—

#### [高] S55-7｜`KrtCreateDirectory` 宏与函数同名冲突
- 位置：`Re.KrtC/src/Core/Utils/Path.h:9` + `Path.c:41` vs `PlatformCommon.h:148,164`
- 类别：死代码 / 宏与函数同名冲突
- 触发条件：任何同时包含 `Path.h` 与 `PlatformCommon.h` 的 TU。
- 后果：`PlatformCommon.h:148`（Windows）/`:164`（POSIX）`#define KrtCreateDirectory(path) CreateDirectory(path, NULL)` / `mkdir(path, 0755)`，使 `Path.h:9` 的声明被宏展开成 `int CreateDirectory(const char*, NULL);`（Win）或 `int mkdir(const char*, 0755);`（POSIX）→ 声明与定义不匹配。当前无 TU 同时包含两者（已核实），一旦新增就会编译失败或产生错误声明。
- 最小修复：把 `PlatformCommon.h` 的宏改名（如 `KrtPlatformCreateDirectory`），或删掉 `Path.c` 中的函数实现。
- 备注：—

#### [高] S55-8｜`KrtMallocChecked` 等「checked」分配 API 只有声明、无定义
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:96-100`
- 类别：死代码 / 未实现声明
- 触发条件：调用 `KrtMallocChecked`/`KrtCallocChecked`/`KrtReallocChecked`/`KrtStrdupChecked`/`KrtFreeChecked`。
- 后果：全代码库无定义、无调用 → 调用即链接错误；暗示存在「返回 NULL 而非 exit」的分配 API，实际不存在 → 声明与实现脱节。
- 最小修复：删除声明。
- 备注：【修订 G1】G1 确认分配失败返回 NULL 而非 `exit(1)`，因此调用方必须自行检查返回值；该「checked」API 并不存在，不能作为兜底。

#### [中] S55-9｜`KrtIntegerParse` 无 NULL 检查且格式支持不全
- 位置：`Re.KrtC/src/Core/Utils/Integer.h:29-31`
- 类别：未检查前置条件
- 触发条件：传 NULL → 崩溃；传 `" 123"`/`"-1"`/`"077"` → 静默返回 false。
- 后果：直接 `text[0]` 无 NULL 检查；不跳过前导空白、不支持 `+`/`-`、不支持八进制与前缀 `_`，可能不是调用方预期。
- 最小修复：加 `if (!text || !result) return false;`；文档化不支持的格式。
- 备注：—

#### [中] S55-10｜`KrtIntegerMask` 在 `bits < 0` 时为 UB
- 位置：`Re.KrtC/src/Core/Utils/Integer.h:11-13`
- 类别：未检查前置条件
- 触发条件：`bits < 0`（`KrtIntegerNormalize` 已用 `bits <= 0` 挡住，但 `KrtIntegerMask` 是 public inline）。
- 后果：`bits < 0` 时执行 `1 << bits`（UB）；`bits > 128` 时 `(1 << bits)` 亦 UB（当前只挡了 `>=128`）。
- 最小修复：`if (bits <= 0) return 0; if (bits >= 128) return ~(KrtUInt128)0;`。
- 备注：—

#### [中] S55-11｜`KrtIntegerFormat` 缓冲余量无静态断言
- 位置：`Re.KrtC/src/Core/Utils/Integer.h:63-81`
- 类别：契约破坏
- 触发条件：任何调用。
- 后果：`char digits[40]` 存放十进制位，UINT128 最大 39 位十进制数，加符号共 40 字符 + NUL = 41 ≤ 42，**当前安全**；但余量关系只靠注释说明，无静态断言。
- 最小修复：加 `_Static_assert` 或编译期检查。
- 备注：—

#### [中] S55-12｜`KRT_STRLEN` 是裸宏，同层错误处理不一致
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:102`
- 类别：语义分叉
- 触发条件：传 NULL。
- 后果：`#define KRT_STRLEN(str) strlen(str)` 与直接用 `strlen` 无差别且无 NULL 检查；同文件 `KRT_STRCPY` 做了溢出检查、`KRT_STRNCPY` 做了截断，唯独它是裸宏。
- 最小修复：删除该宏，或加 `if (!(str)) return 0;`（需改成 inline 函数）。
- 备注：—

#### [高] S56-1｜`KrtTimeNowSeconds` 两平台语义不同（墙钟 vs 单调钟）
- 位置：`Re.KrtC/src/Core/Utils/Path.c:20-34`
- 类别：阶段不对齐 / 配置漂移
- 触发条件：任何平台；调用方 `ParallelCompiler.c:59,85,265,298` 用它计算 duration。
- 后果：Windows 分支用 `GetSystemTimeAsFileTime`（墙钟），POSIX 分支用 `clock_gettime(CLOCK_MONOTONIC)`（单调钟）→ Windows 上 NTP 校时/闰秒可让 duration 为负或巨大，构建耗时统计失真；同一 API 两平台语义不同。
- 最小修复：Windows 改用 `QueryPerformanceCounter` 或 `GetTickCount64`。
- 备注：附带 `:31` `(void)ts;` 在 `:32` 使用 `ts` 之前是死语句。

#### [高] S56-2｜`KrtPathGetFilename` 超长返回 -1 无诊断，调用点忽略导致未初始化内存
- 位置：`Re.KrtC/src/Core/Utils/Path.c:53-82` + `Main.c:433,448,609,774,987,1292,1306,1323`
- 类别：静默失败 + 未检查前置条件（使用未初始化内存）
- 触发条件：文件名（含路径）长度 ≥ `KRT_MAX_PATH`(1024)。
- 后果：`KrtPathGetFilename` 在 `len >= size` 时 `return -1`（无诊断），Main.c 全部 8 个调用点忽略返回值；`char file_base[KRT_MAX_PATH];` 未初始化，失败后内容未定义，紧接着 `KrtPathRemoveExtension(file_base, ...)`（`Path.c:193-207`）读未初始化栈 → UB / 垃圾输出文件名，可能覆盖错误文件（与 S47-5 叠加）。
- 最小修复：调用点检查返回值并 `KrtError`；或 `KrtPathGetFilename` 内部对超限 `KRT_COMPILE_ERROR`。
- 备注：—

#### [高] S56-3｜`last_sep` 指针比较 UB（必然触发）
- 位置：`Re.KrtC/src/Core/Utils/Path.c:185`
- 类别：未检查前置条件（指针比较 UB）
- 触发条件：路径只含一种分隔符（Windows 含 `\`、POSIX 含 `/`，必然触发）。
- 后果：`const char* last_sep = (slash > backslash) ? slash : backslash;` —— 当 `slash` 与 `backslash` 恰有一个为 NULL 时，用 `>` 比较 NULL 与有效指针是 C 标准未定义行为；编译器可假设两指针同对象而优化掉比较 → `last_sep` 取值错误 → `KrtPathRemoveExtension` 对 `a.b/c`（应保留）或 `a/b.c`（应去扩展名）判断错误。
- 最小修复：`const char* last_sep = (slash && backslash) ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);`
- 备注：—

#### [高] S56-4｜`KRT_STRCPY_S` 超长静默截断
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:8-16`
- 类别：静默失败（截断）
- 触发条件：`src` 比 `dest` 长。
- 后果：`copy_len = (src_len < dest_size-1) ? src_len : dest_size-1;` 超长时静默截断，无诊断、无返回值指示。调用点 `Path.c:80,207,231,275`、`ParallelCompiler.c:323,340,358`、`LeakDetector.c:192` → 路径/对象名被静默截断，链接错文件、找不到运行时对象（与 S50-3/S54-6 叠加）。
- 最小修复：加返回值或让调用方在调用前检查长度（当前仅 `KRT_STRCPY` 做了）。
- 备注：—

#### [高] S56-5｜`KRT_STRCAT_S` 整数下溢导致 `memcpy` 越界写
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:18-30`
- 类别：静默失败 + 未检查前置条件（整数下溢）
- 触发条件：`dest` 不是以 NUL 结尾，或 `dest_len >= dest_size`。
- 后果：`size_t remaining = dest_size - dest_len - 1;` 下溢为巨大值 → `copy_len = src_len` → `memcpy` 越界写 → 内存破坏。
- 最小修复：`if (dest_len >= dest_size) break/return;` 先做前置断言。
- 备注：—

#### [中] S56-6｜`KrtPathExists` 无 NULL 检查
- 位置：`Re.KrtC/src/Core/Utils/Path.c:36-39`
- 类别：未检查前置条件
- 触发条件：传 NULL。
- 后果：`stat(path, &st)` 无 NULL 检查 → 崩溃。调用点 `Main.c:179` 传字面量、`Path.c:303,322,356,375` 传内部缓冲（非 NULL），但 API 是 public（`Path.h:8`、`KrtCommon.h:220`）。
- 最小修复：`if (!path) return 0;`。
- 备注：—

#### [中] S56-7｜`KrtGetExecutableDirectory` 路径超长静默截断
- 位置：`Re.KrtC/src/Core/Utils/Path.c:114-161`
- 类别：静默失败 + 未检查前置条件
- 触发条件：安装路径深度大（超 `MAX_PATH` / 1023）。
- 后果：Windows `GetModuleFileNameA(NULL, path, MAX_PATH)` 超长时静默截断，`len` 赋值后从未使用；POSIX `readlink("/proc/self/exe", path, 1023)` 超长时静默截断；若截断后串中无 `/`，`last_slash == NULL` → 不裁剪 → 把完整（截断的）可执行文件路径当作目录返回。`FindRuntimeObj`（`ParallelCompiler.c:367`、`ArkLinkIntegration.c:264`、`ProjectKrt.c:940`）找不到运行时 .o → 链接缺运行时对象（与 S50-3/S54-6 叠加）。
- 最小修复：检查 `len == MAX_PATH-1` / `readlink` 返回值是否等于缓冲上限，失败返回 -1 并报诊断。
- 备注：—

#### [中] S56-8｜`KrtPathJoin` 在 Windows 硬编码 `"/"`
- 位置：`Re.KrtC/src/Core/Utils/Path.c:233-235`
- 类别：平台语义分叉
- 触发条件：Windows 平台调用 `KrtPathJoin`。
- 后果：硬编码 `KRT_STRCAT_S(result, size, "/")`，Windows 下应使用 `KRT_PATH_SEPARATOR`（`'\\'`）→ 产出混合分隔符路径，部分 API 不接受。
- 最小修复：`#ifdef _WIN32` 选分隔符，或统一用 `KRT_PATH_SEPARATOR`。
- 备注：—

#### [中] S56-9｜`KrtEnsureDirectoryRecursive` 原地篡改调用方路径
- 位置：`Re.KrtC/src/Core/Utils/Path.c:314,367`
- 类别：静默失败 + 语义分叉
- 触发条件：路径含 `\` 分隔符（Windows）。
- 后果：`*slash = '\0'; ... *slash = '/';` 无论原分隔符是 `/` 还是 `\`，恢复时一律写 `'/'`，原地修改调用方路径；Windows 下 `C:\a\b` 在循环中变成 `C:\a/b`，后续 `KrtPathExists(temp)` 用的是被篡改的串。
- 最小修复：保存原字符 `char saved = *slash; ... *slash = saved;`。
- 备注：—

#### [中] S56-10｜`KrtEnsureDirectoryRecursive` 路径超长静默失败
- 位置：`Re.KrtC/src/Core/Utils/Path.c:266,271-273`
- 类别：静默失败
- 触发条件：路径 ≥ 1024。
- 后果：`char temp[1024]` 固定栈缓冲，`if (len >= sizeof(temp)) return -1;` 无诊断 → 目录创建静默失败，后续编译因缺目录报错，原因丢失。
- 最小修复：`KrtError("Path too long: %s", path)` 或动态分配。
- 备注：—

#### [中] S56-11｜`result[len] = '\0';` 连续写两遍
- 位置：`Re.KrtC/src/Core/Utils/Path.c:196-197`
- 类别：死代码 / 冗余
- 触发条件：任何调用。
- 后果：冗余写入。
- 最小修复：删一行。
- 备注：—

#### [中] S56-12｜`KRT_SPRINTF_S` 用 `__VA_ARGS__` 而非 `##__VA_ARGS__`
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:32`
- 类别：契约破坏 / 潜在编译错误
- 触发条件：格式串无额外参数时调用。
- 后果：`#define KRT_SPRINTF_S(buffer, buffer_size, format, ...) snprintf(buffer, buffer_size, format, __VA_ARGS__)` 用 `__VA_ARGS__` 而非 `##__VA_ARGS__` → 无额外参数时调用会语法错误；且忽略 `snprintf` 返回值（截断无指示）。
- 最小修复：改 `##__VA_ARGS__`，或改 inline 函数并返回截断标志。
- 备注：—

#### [中] S56-13｜`KRT_STRCAT_S` 在缓冲已满时静默无操作
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:18-30`
- 类别：静默失败
- 触发条件：`dest_len == dest_size - 1`（缓冲已满）。
- 后果：`remaining == 0` → 整个宏静默无操作，调用方以为拼接成功。
- 最小修复：返回剩余空间或截断标志。
- 备注：—

#### [高] S57-1｜裸 `malloc`/`strdup`/`realloc` 与 `KRT_FREE`/`KrtSafeFree` 混用风险
- 位置：`Re.KrtC/src/Compiler/Frontend/CompilerError.c:54,59,61,79,82,84,93,98,147`
- 类别：资源与生命周期 + 契约破坏（两套分配器混用）
- 触发条件：`KrtErrorReportCreate` 用裸 `malloc` 分配 `report`（:54）与 `report->errors`（:59），`KrtErrorReportSetSourceCode` 用裸 `strdup`（:98），`KrtErrorReportAddEx` 用裸 `realloc`（:147）；若这些指针被交给 `KRT_FREE`/`KrtSafeFree`，或反向把 `KRT_MALLOC` 的指针交给裸 `free`。
- 后果：`KrtSafeFree`（`Allocator.c:276-287`）查不到索引 → `KrtMemoryReportError` 空操作 → 静默 no-op → 泄漏；反向时 `user_ptr = actual_ptr + KRT_CANARY_PREFIX`（:186），裸 `free` 释放带前缀的中间指针 → glibc abort / 堆破坏。
- 最小修复：统一到 `KRT_MALLOC`/`KRT_FREE`；或在 `KrtErrorReport` 加 `allocator` 标记，`KrtSafeFree` 对未跟踪指针 `KRT_COMPILE_ERROR`。
- 备注：原列「致命」，现状核实已 grep 确认 `CompilerError.c` 内无 `KRT_FREE`、`KroWriter.c`/`Main.c` 内无 `KRT_FREE` → 当前无实际交叉释放，属潜伏地雷，故定级 **高**（非致命）。

#### [致命] S57-2｜分配失败系统性静默（零诊断）
- 位置：`Re.KrtC/src/Core/Memory/Allocator.c:159-211,213-227,229-261,263-274` + `KrtCommon.h:77-81`
- 类别：静默失败（系统性，零诊断）
- 触发条件：OOM；`size == 0` / `count == 0`（:160-162、:214-216）；`KrtSafeRealloc` 传入未跟踪指针（:239-244）。
- 后果：返回 NULL 而非终止，且诊断函数为空操作 → 调用点若不检查即 NULL 解引用（崩溃），或产生静默 NULL 字段向下传播。
- 最小修复：`KrtSafeMalloc` 失败时改用 `KrtError_LOC(file,line,...)` 并 `KRT_COMPILE_ERROR`；或提供 `KRT_MALLOC_OR_DIE` 宏并强制调用点检查。
- 备注：即 G1 前提修正对应的条目。


#### [高] S57-3｜死代码（整模块零调用者）
- 位置：`Core/Memory/SmartPtr.c` 全 316 行 + `SmartPtr.h`
- 类别：死代码
- 触发条件：任何调用（当前零调用者）
- 后果：约 316 行死代码，内含 S57-4~S57-8 多个未修复缺陷，一旦启用即炸
- 最小修复：删除整个模块，或补测试后启用

#### [高] S57-4｜契约破坏（cleanup 属性签名不匹配，潜伏致命）
- 位置：`SmartPtr.h:62` vs `SmartPtr.c:221`
- 类别：契约破坏
- 触发条件：任何 `SmartPtr* p __attribute__((cleanup(KrtPtrDestroy)))` 变量离开作用域
- 后果：GCC cleanup 传 `SmartPtr**`，而函数签名是 `SmartPtr*` → 函数内 `ptr->type` 读到栈垃圾 → 可能以垃圾 type 调用 `KrtSharedRelease` → 对垃圾地址 `atomic_fetch_sub` → SEGV；`KRT_FREE(ptr)` 释放栈地址（静默 no-op，泄漏）
- 最小修复：改 `void KrtPtrDestroy(SmartPtr** ptr)`，或加 wrapper
- 备注：当前零调用者 → 定级高（潜伏）

#### [高] S57-5｜未检查前置条件（整数溢出）
- 位置：`SmartPtr.c:54-64`、`SmartPtr.c:93-98`
- 类别：未检查前置条件
- 触发条件：`count * element_size` 溢出（如 `count=SIZE_MAX/4, element_size=8`）
- 后果：`KrtMakeShared(type_name, count*element_size)` 得到过小分配，调用方按 `count*element_size` 写入 → 堆缓冲区溢出
- 最小修复：用 `KrtSizeMultiply`（`Core/Utils/Size.h:18-24`）检查，溢出返回 NULL

#### [高] S57-6｜语义分叉 / 契约破坏
- 位置：`SmartPtr.c:186-188` + `SmartPtr.h:60`
- 类别：语义分叉 / 契约破坏
- 触发条件：对 `PTR_WEAK` 指针调用 `KRT_PTR_VALID`/`KrtPtrGet`
- 后果：`KrtPtrIsNull` 含 `ptr->ptr == NULL`，而 weak 指针的 `ptr` 恒为 NULL（`SmartPtr.c:252`）→ 任何有效 weak 指针都被判 invalid；`KrtPtrGet`（:144-149）对活着的 weak 返回 NULL，而 `KrtWeakLock` 能返回有效 shared → 同一语义两种结果
- 最小修复：`KrtPtrIsNull` 改为 `!ptr || !KrtPtrIsValid(ptr)`，或 `KrtPtrGet` 对 `PTR_WEAK` 走 `KrtWeakLock`

#### [高] S57-7｜静默失败 / 契约破坏
- 位置：`SmartPtr.c:229-240`
- 类别：静默失败 / 契约破坏
- 触发条件：传入非 `KRT_MALLOC` 分配的 `new_ptr`；或 `new_ptr == NULL`
- 后果：`ptr_cleanup_internal` 后直接 `ptr->ptr = new_ptr; ptr->type = PTR_UNIQUE`，不检查来源；之后 `KrtPtrDestroy` → `KRT_FREE(new_ptr)` → 未跟踪 → 静默 no-op 泄漏（叠加 S57-2）；`size` 形参被 `__attribute__((unused))` 丢弃
- 最小修复：文档化「`new_ptr` 必须来自 `KRT_MALLOC`」，或改签名为 `KrtPtrReset(SmartPtr*, SmartPtr*)`

#### [高] S57-8｜死代码（Arena 大半）
- 位置：`Arena.c:17-32`（`KrtArenaCreate` 互斥版）、`:114-124`（`KrtArenaCalloc`）、`:126-136`（`KrtArenaStrdup`）、`:171-189`（`KrtArenaReset`）、`:191-219`（`KrtArenaStats`）、`:221-231`（`KrtArenaGetUsage`）、`:233-250`（`KrtArenaGetBlockCount`）
- 类别：死代码
- 触发条件：任何调用（当前外部零调用者）
- 后果：约半个模块死代码
- 最小修复：删除（仅 `KrtArenaAlloc`/`KrtArenaCreateLocal`/`KrtArenaDestroy`/`KrtArenaRealloc` 有调用）

#### [高] S57-9｜死代码（11 个声明无定义）
- 位置：`Allocator.h:68,69,78,81,82,93,94,95,123,124,125`
- 类别：死代码 / 契约破坏
- 触发条件：任何调用
- 后果：`KrtMemoryProtect`/`KrtMemoryUnprotect`/`KrtPtrIsFreed`/`KrtPtrIsDangling`/`KrtPtrTrackFree`/`KrtMemoryValidateHeap`/`KrtMemoryCheckIntegrity`/`KrtMemoryScanCorruption`/`KrtMemorySafetyEnablePoison`/`KrtMemorySafetyEnableCanary`/`KrtMemorySafetySetStrictMode` 全项目 0 定义 0 调用；声明暗示存在内存保护/完整性校验/严格模式，实际不存在，调用即链接错误
- 最小修复：删除声明，或实现

#### [高] S57-10｜死代码 + 诊断不可达
- 位置：`Allocator.c:333-345`（`KrtPtrCheckDoubleFree`）
- 类别：死代码 / 诊断不可达
- 触发条件：任何调用（当前零调用者）
- 后果：`KrtSafeFree`（:276-310）在释放时已 `remove_memory_block` 并释放 `MemoryBlock`，第二次 `KrtPtrGetBlock` 必返回 NULL → `block && block->is_freed` 恒假 → 双重释放永远检测不出来；`is_freed` 字段（:205 写入）是死字段
- 最小修复：删除该 API 与 `is_freed`，或在 `remove_memory_block` 时 poison 标记

#### [高] S57-11｜死代码
- 位置：`Allocator.c:437-458`（`KrtMemoryDumpBlocks`，遍历链表但 `current = current->next` 无任何输出）、`:474-475`（`KrtMemoryDumpStats` 空函数体）、`:460-472`（`KrtMemoryGetTotalUsage`/`KrtMemoryGetBlockCount` 零调用者）
- 类别：死代码
- 最小修复：删除

#### [高] S57-12｜诊断逻辑反了（恒不触发）
- 位置：`Allocator.c:295-297`
- 类别：诊断不可达
- 触发条件：任何 `KrtSafeFree`
- 后果：`KrtMemoryIsPoisoned(ptr, block->user_size)` 在 free 中于 poison 之前调用，且要求全部用户字节都是 `0xDD` 才返回 true → 对活块几乎不可能成立 → "USE_AFTER_FREE" 诊断是死代码
- 最小修复：改为 free 前记录 hash，二次 free 时比对；或删除该诊断

#### [高] S57-13｜静默失败
- 位置：`Allocator.c:283-287`（未跟踪指针静默 no-op → 泄漏）、`:239-244`（`KrtSafeRealloc` 未跟踪返回 NULL 并泄漏 `old_ptr`）
- 类别：静默失败
- 最小修复：`KrtMemoryReportError` 改为 `KrtError_LOC`（真实诊断）并 `KRT_COMPILE_ERROR`

#### [高] S57-14｜静默失败（宏）
- 位置：`Allocator.h:107`（`KRT_ARRAY_GET` 越界返回 NULL 无诊断）、`:109-117`（`KRT_ARRAY_SET` 越界调用空操作 `KrtMemoryReportError`）
- 类别：静默失败
- 后果：越界写入被静默丢弃
- 最小修复：越界时 `KRT_COMPILE_ERROR`

#### [高] S57-15｜契约破坏 / 静默失败
- 位置：`Arena.c:77-112` + `Tokenizer.c:428,436`
- 类别：契约破坏 / 静默失败
- 触发条件：arena 分配失败（含 `KrtArenaAlloc` 对 `size==0` 返回 NULL）
- 后果：`Tokenizer.c:428`（及 :436）在 arena 分配失败时 `return lexer_create_token(TOKEN_EOF, ...)` → 把 OOM 伪装成文件结束，词法阶段静默截断输入，后续阶段得到残缺 token 流
- 最小修复：失败时 `KRT_COMPILE_ERROR` 报位置与原因；`KrtArenaAlloc` 对 `size == 0` 的行为需明确文档化（当前返回 NULL，调用方 `ParserBase.h:10`、`TypeExpansion.inc:16` 用 `size ? size : 1` 绕过，属未文档化前置条件）

#### [中] S57-16｜契约破坏（未文档化的 `old_size` 前置条件）
- 位置：`Arena.c:138-169`
- 类别：契约破坏
- 触发条件：调用方传入错误的 `old_size`（唯一调用点 `Tokenizer.c:434` 传 `capacity/2`，当前正确）
- 后果：`old_aligned` 与真实分配不符时，快速路径 `ptr == block->data + block->used - old_aligned` 误判 → 走慢路径 → 旧 arena 空间永久泄漏；且 `memcpy(result, ptr, old_size)` 可能多拷贝（`old_size` 大于真实分配）→ 越界读
- 最小修复：把 `old_size` 存进分配头，或文档化并加 `assert`
- 备注：已核查快速路径条件本身正确（`Arena.h:21` 对齐保证、`KRT_ARENA_ALIGNMENT` 与 `KrtSizeAlign` 一致）

#### [中] S57-17｜死代码 / 诊断不可达
- 位置：`SmartPtr.c:299-308`（`total_allocations - total_deallocations` 无符号下溢）、`:137-142`（`KrtSharedCount`）、`:242-261`（`KrtMakeWeak`）等零调用者
- 类别：死代码
- 最小修复：删模块（S57-3）

#### [中] S57-18｜死代码（宏别名级联）
- 位置：`KrtCommon.h:199-209`
- 类别：死代码
- 触发条件：任何 `KRT_MALLOC_LEAK` 系列调用
- 后果：`KRT_LEAK_DETECTION_ENABLED` 在 `LeakDetector.h:10` 与 `KrtCommon.h:168` 重复定义为 0（S45-2 已报，不重复计数）→ `KRT_MALLOC_LEAK` 系列成为 `KRT_MALLOC` 纯别名 → `LeakDetector.c:157-193` 四个 `*LeakDetected` 函数不可达
- 最小修复：随 S45-2 一并处理

## 第五部分　补充审计（第 58–101 批，2026-11-20）

> 原审计在第 57 批后被 HTTP 402 中断，本部分是随后补审的成果，覆盖：Kro 后端剩余段（KroCodegen.c:861-1622 与 Kro*.inc）、Lexer/Semantic/Parser 剩余文件、Ast/AstVisit、IR 内联两文件、Krt stdlib 与 Runtime、SelfHost 自举编译器全部源文件、Accelerator/IrParamTable/平台层剩余文件、examples 与 Test 各目录。
> 严重度已统一（严重→高、一般→中、轻→低）；各补审条目沿用原判定，部分切片对同类缺陷逐条拆分，口径可能与主清单略有差异。
> **G1 补充**：`KrtParserAlloc`/`KrtParserStrdup`/`KrtParserCreateNode` 失败走 `KRT_COMPILE_ERROR`→`exit(1)`（`ParserBase.h:9-35`），其 `if (!node)` 类分支是**死代码**；G1 的「分配失败返回 NULL、零诊断」只适用于 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC` 等宏（底层 `KrtSafeMalloc`/`KrtSafeStrdup`/`KrtSafeRealloc`）。


### 第五十八批：KroCodegen.c:861-1622

#### 覆盖范围
- 文件与行段：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c` 第 861-1622 行（`generate_instruction` 尾部、`generate_block`、`calculate_function_stack_size`、`generate_function`、`generate_data_section`、`KrtKrtGenerate`）。
- 为核对符号定义，另按需读取同文件相邻少量上下文（110-189、220-310、355-404、449-511、550-620、630-740），并 grep 了 `KroWriter.c`、`Ir.c`、`Ir.h`、`KroCodegen.h`、`KroRegisterPacking.inc`、`KroTailCalls.inc`、`Compiler.c` 中的特定符号行。未通读其他源文件。
- 前提 G1 已应用：`KRT_MALLOC/CALLOC/REALLOC/STRDUP` 失败返回 NULL，`KrtMemoryReportError` 为空操作，因此所有分配失败分支均按「活路径 + 零诊断」判定。


##### [致命] 58-1｜`output_file` 参数完全未使用，写盘走的是 `output_filename`
- 位置：`KroCodegen.c:1470`（签名）、`1478`（仅存 `ctx.output_file`）、实际写盘 `1506`、`1615`；对照 `KroCodegen.h:87` 注释 “Emit a KRO object for module into output_file; output_filename names diagnostics.”
- 类别：6 契约破坏；13 注释与实现脱节；5 死代码（`ctx.output_file` 为死字段）
- 触发条件：所有 `KRT_TARGET_KRO_OBJ` / `KRT_TARGET_EXE_PLATFORM` 编译（`Compiler.c:243`、`247` 传入 `compiler->output_file` 与 `compiler->output_filename`）。
- 后果：调用方打开的 `FILE*` 从未被写入；后端用 `output_filename` 再次 `fopen(...,"wb")`（`KroWriter.c:481`）。同一文件被两个句柄以 `"wb"` 打开（前者先截断、后者再截断写入）；若调用方传入的是管道 / 内存流 / `/dev/stdout` / 与文件名不同的目标，输出会丢失或落点错误；`output_filename == NULL` 时静默写当前目录下的 `output.kro`（`1487`）。
- 最小修复：为 `kro_write_file` 增加 `FILE*` 重载并真正写 `output_file`；或删除 `output_file` 参数，同步修正 `KroCodegen.h:87` 注释与 `Compiler.c` 调用约定。
- 备注：无。

##### [致命] 58-2｜`kro_write_file` 失败被空 `if` 吞掉，且函数返回 void
- 位置：`KroCodegen.c:1615-1616`（`if (!kro_write_file(...)) { }`），另 `1506` 直接忽略返回值；返回类型 `void` 见 `1470`。
- 类别：1 静默失败；2 诊断缺失；9 错误处理不一致
- 触发条件：磁盘满、配额、权限不足、目标路径不可写（`kro_write_file` 内 `fopen`/`fwrite` 失败，`KroWriter.c:481-567`）。
- 后果：目标文件残缺或根本不存在，`KrtKrtGenerate` 仍正常返回，`Compiler.c:256` 继续 `return true`，编译被上报为成功；调用方无法区分成功与失败。
- 最小修复：`KrtKrtGenerate` 返回 `bool`，写盘失败时 `KrtError` 并向 `Compiler.c` 传播；至少删掉空 `{}` 并记录诊断。
- 备注：无。

##### [致命] 58-3｜`string_const_sym_indices[]` 存在未初始化元素
- 位置：`KroCodegen.c:1418`（`KRT_MALLOC`，非 CALLOC）、`1426-1428`（`if (!str) { continue; }`）、`1439-1440`（仅非空字符串才赋值）；消费点 `685-688`。
- 类别：8 未检查前置条件（未初始化内存）；10 资源与生命周期
- 触发条件：`module->string_constants[i] == NULL`，即 `KrtIrStringConst` 中 `KRT_STRDUP` 失败（G1：返回 NULL，`Ir.c:1074`），或模块中本身存在空槽。
- 后果：`continue` 跳过赋值，`string_const_sym_indices[i]` 保持堆上垃圾值；随后 `emit_load_value_to_reg`（`685-688`）读出该垃圾 `sym_idx`，只要 `>= 0` 就调用 `emit_load_string_addr_to_reg` → `kro_add_reloc`（`240`）把非法符号索引写入 ABS64 重定位；`kro_add_reloc`（`KroWriter.c:397-428`）不校验 `sym_idx`，最终产出损坏的目标文件。同时该字符串未写 rodata、未建符号，`str_const_N` 命名与实际符号错位。
- 最小修复：改用 `KRT_CALLOC`；或在 `continue` 前显式写 `-1`，并让引用处对 `-1` 走诊断/回退路径。
- 备注：与 58-4 的 OOM 路径叠加。

##### [高] 58-4｜`generate_data_section` 分配失败静默 `return`，下游零代码发射
- 位置：`KroCodegen.c:1418-1421`；下游 `685-691`。
- 类别：1 静默失败（G1）
- 触发条件：`KRT_MALLOC(module->string_const_count * sizeof(int32_t))` 返回 NULL。
- 后果：`ctx.string_const_count` 保持 0、指针保持 NULL（`1422` 未执行），函数无诊断返回；`KrtKrtGenerate`（`1495`）忽略该返回继续生成全部函数。此后每个 `KRT_IR_VALUE_STRING_CONST` 在 `685-691` 因 `sym_idx < 0` 命中 `if (sym_idx >= 0)` 的否分支，**一条指令都不发射**，目标寄存器残留上一条指令的旧值 → 字符串地址静默错误。
- 最小修复：失败时 `KrtError` 并使整个代码生成失败（向上返回状态），不要在半初始化状态下继续。
- 备注：`685-691` 的静默跳过是 OOM 零诊断的直接放大器。

##### [高] 58-5｜字符串长度检查失败直接 `return`，跳过全部剩余字符串与全部 globals
- 位置：`KroCodegen.c:1434`（`if (len >= UINT32_MAX) { KrtError(...); return; }`）。
- 类别：1 静默失败；4 阶段不对齐
- 触发条件：某个字符串常量长度 ≥ `UINT32_MAX`（或后续 `kro_write_rodata` 无法容纳）。
- 后果：`generate_data_section` 提前返回，剩余字符串常量与**所有全局变量**的 rodata/data 写入（`1444-1467`）全部丢失；调用方无感知，继续生成引用这些符号的函数，目标文件出现未定义符号 / 错误重定位。
- 最小修复：`generate_data_section` 返回 `bool`，失败时中止整次 `KrtKrtGenerate`，不得只丢局部数据。
- 备注：有 `KrtError` 但无传播通道，诊断与中止语义脱节。

##### [高] 58-6｜syscall 实参过多被静默截断
- 位置：`KroCodegen.c:1224-1226`（`if (arg_count > g_syscall_arg_reg_count) { arg_count = g_syscall_arg_reg_count; }`）。
- 类别：1 静默失败；2 诊断缺失
- 触发条件：`KRT_IR_SYSCALL` 的实参数 > `g_syscall_arg_reg_count`（Linux 6、Windows 4，见 `50-57`）。
- 后果：多余实参被直接丢弃，syscall 以错误参数执行（例如 `write` 丢失 buf/count、`mmap` 丢失 flag），且无任何诊断；生成器继续产出“看起来正常”的目标文件。
- 最小修复：超限时报错并中止/跳过该指令，同时保证寄存器装载与实际参数一致。
- 备注：无。

##### [高] 58-7｜`KRT_IR_ADDRESS_OF` 对 ARG 索引无边界检查
- 位置：`KroCodegen.c:1151-1152`（`offset = ctx->arg_stack_offsets[storage->data.index];`）。
- 类别：8 未检查前置条件（越界）
- 触发条件：`ADDRESS_OF` 的操作数是索引 `< 0` 或 `>= KRT_KRO_MAX_ARGS(128)` 的 `KRT_IR_VALUE_ARG`。
- 后果：越界读 `arg_stack_offsets` 之后的相邻字段（`arg_address_taken`、`arg_packed`，见 `KroCodegen.h:67-69`），`offset` 变成垃圾值；随后 `lea rax,[rbp-offset]`（`1162-1165`）生成错误地址，且错误地址会被当作指针使用。
- 最小修复：读取前检查 `storage->data.index >= 0 && storage->data.index < ctx->current_param_count`，否则回退到符号路径并报错。
- 备注：疑似，需验证 IR 能否产生越界 ARG 索引（对照 `660`、`666` 与 `KroRegisterPacking.inc:536-537` 均做了 `>= 0 && < current_param_count/< KRT_KRO_MAX_ARGS` 检查，此处为遗漏点）。

##### [高] 58-8｜`ADDRESS_OF` 符号未找到时把 `-1` 当符号索引写重定位
- 位置：`KroCodegen.c:1158-1160`（`int symbol = kro_find_symbol(...); emit_load_string_addr_to_reg(ctx, symbol, KRT_REG_RAX);`），经 `227-241` 调用 `kro_add_reloc`。
- 类别：8 未检查前置条件；6 契约破坏
- 触发条件：`VAR` 既不在局部变量表（`find_local_var` 返回 NULL）也不在符号表中——例如未声明/未导出符号，或 `KRT_IR_ALLOC`（`1195-1197`）在局部变量数达到 `KRT_KRO_MAX_LOCAL_VARS` 后 `find_or_alloc_local_var` 返回 NULL 的路径。
- 后果：`kro_find_symbol` 返回 `-1`（`KroWriter.c:357-372`），`emit_load_string_addr_to_reg` 将其转为 `uint32_t` 的 `0xFFFFFFFF` 写入 ABS64 重定位；`kro_add_reloc`（`KroWriter.c:397-428`）不校验符号索引，目标文件含非法重定位，加载/链接阶段才暴露或越界访问。
- 最小修复：`symbol < 0` 时 `KrtError` 并跳过该地址计算（或写 0），不得把 `-1` 传入重定位。
- 备注：疑似，需验证是否存在“未声明 VAR 取地址”或“局部变量表溢出”的合法/可达路径。

##### [中] 58-9｜`KrtStorePtr` / `KrtLoadPtr` 操作数不足时静默回退为普通函数调用
- 位置：`KroCodegen.c:923`（`&& inst->operand_count >= 4`）、`933`（`&& inst->operand_count >= 3`），回退路径 `944-1045`。
- 类别：3 语义分叉；9 错误处理不一致；2 诊断缺失
- 触发条件：前端为这两个内建生成实参不足的调用（如 `KrtStorePtr` 仅 2 个实参，`operand_count == 3`）。
- 后果：条件不成立即落入通用调用流程，把 `"KrtStorePtr"` / `"KrtLoadPtr"` 当作外部函数符号调用（`1032-1040`）；若运行时不存在该函数，则生成未定义符号引用，且原本的内存存储/加载语义完全丢失，无任何诊断。
- 最小修复：内建操作数数量不匹配时 `KrtError` 并中止，不要静默改走通用调用。
- 备注：疑似，需验证前端/IR 优化是否可能生成实参不足的内建调用。

##### [中] 58-10｜入口参数为空与 writer 创建失败均静默 `return`
- 位置：`KroCodegen.c:1471-1473`（`if (!output_file || !module) return;`）、`1491-1493`（`if (!ctx.writer) return;`）。
- 类别：1 静默失败；2 诊断缺失
- 触发条件：调用方传入 NULL 的 `output_file`/`module`，或 `kro_writer_create()` 失败。
- 后果：直接返回，不写文件、不 `KrtError`；`Compiler.c:256` 仍返回成功，问题被延迟到加载/运行阶段才暴露。
- 最小修复：统一改为返回 `bool` 并在失败时 `KrtError`。
- 备注：与 58-2 构成同一条“失败不传播”链。

##### [中] 58-11｜`generate_function` 失败路径静默返回，不留下任何产物
- 位置：`KroCodegen.c:1559-1565`（`if (!generate_function(...)) { release_temporary_storage(...); KRT_FREE(...); kro_writer_destroy(...); return; }`）。
- 类别：1 静默失败；2 诊断缺失
- 触发条件：`generate_function` 返回 false（如 `1291-1294` 的临时存储分配失败；该分支自身有 `KrtError`，但调用点无额外处理）。
- 后果：目标文件完全不写，且此前已写入的 data/rodata 全部随 `kro_writer_destroy` 丢弃；调用方仍判定编译成功。错误处理与 58-2 的“写盘失败”不一致（一处有诊断、一处无）。
- 最小修复：失败时 `KrtError` 并向 `Compiler.c` 返回失败状态。
- 备注：无。

##### [中] 58-12｜`output_filename` 超长被静默截断
- 位置：`KroCodegen.c:1480-1485`（`if (len >= sizeof(ctx.output_filename)) { len = sizeof(...) - 1; }`）。
- 类别：11 配置漂移；1 静默失败
- 触发条件：调用方传入长度 ≥ 256 的 `output_filename`。
- 后果：文件名被静默截断，输出写到另一个路径，用户指定的目标丢失且无告警。
- 最小修复：超长时报错并失败，或动态分配并完整保存路径。
- 备注：无。

##### [中] 58-13｜全局符号添加失败 / 重名被静默接受
- 位置：`KroCodegen.c:1466`（`kro_add_symbol(...)` 返回值被丢弃），同类 `1587`（`_start` 符号）。
- 类别：1 静默失败；9 错误处理不一致
- 触发条件：`kro_add_symbol` 因容量/内存失败返回 `-1`（`KroWriter.c:297-302`），或 global 名称与函数/其他 global/ `_start` 重名。
- 后果：符号表出现 `-1` 或重复符号，后续重定位按名字取符号时行为不确定，链接期才报错；生成阶段零诊断。
- 最小修复：检查返回值；对重名做检测并报错。
- 备注：无。

##### [中] 58-14｜`RETURN` 无操作数时静默返回 0
- 位置：`KroCodegen.c:1054-1056`（`} else if (!identity) { emit_load_imm64(ctx, 0); }`）。
- 类别：2 诊断缺失；3 语义分叉
- 触发条件：`KRT_IR_RETURN` 的 `operand_count == 0` 且未被 `emit_tail_identity_return` 命中（如优化/SSA 改写后遗留的终止指令）。
- 后果：对非 void 返回类型的函数静默返回整数 0，不报诊断；调用方得到错误返回值。
- 最小修复：校验 `operand_count` 与 `func->return_type` 的一致性，缺失时 `KrtError`。
- 备注：疑似，需验证 RETURN 是否可能零操作数（`KrtIrReturn` 总是添加 1 个操作数，`Ir.c:882-894`；但优化/SSA 路径未逐一核对）。

##### [中] 58-15｜`default` 分支静默忽略未实现的 IR opcode
- 位置：`KroCodegen.c:1243-1244`（`default: break;`）。
- 类别：5 死代码；2 诊断缺失
- 触发条件：IR 中出现本 switch 未覆盖的 opcode（例如 `Ir.h` 中存在但此处未处理的指令）。
- 后果：该指令不发射任何代码，程序静默少做一步，且无告警；与 `1243` 同函数的其它分支（都带前置检查）形成错误处理不一致。
- 最小修复：对未实现 opcode `KrtError` 并中止。
- 备注：疑似，需对照 `Ir.h:226-275` 的 opcode 列表核对是否全部覆盖。

##### [低] 58-16｜`ADDRESS_OF` / `STACKALLOC` / `ALLOC` 未检查 `operand_count`
- 位置：`KroCodegen.c:1149`（`KrtIRValue* storage = &inst->operands[0];`）、`1170` 与 `1175`（`operands[0]`/`operands[1]`）、`1190`（`operands[0]`）。
- 类别：8 未检查前置条件（越界）
- 触发条件：对应指令 `operand_count` 小于代码读取的下标。
- 后果：越界读 `inst->operands` 数组，得到垃圾值后继续生成代码。
- 最小修复：进入各分支前检查 `operand_count`（与 `1077`、`1098`、`1118` 等分支的写法保持一致）。
- 备注：疑似，需验证 IR 构造器是否保证操作数个数（`KrtIrAddressOf`/`KrtIrStackAlloc` 在 `Ir.c:993-1014` 固定添加操作数）。

##### [低] 58-17｜`KRT_IR_PHI` 在栈帧大小固定后仍可能增长帧
- 位置：`KroCodegen.c:1213`（`alloc_temp_slot(ctx, inst->result.data.index);`），`alloc_temp_slot` 定义 `558-568`（会 `ctx->current_stack_offset += 16`），而 `stack_size` 已在 `1284-1288` / `1332` 计算并用于 prologue。
- 类别：4 阶段不对齐；8 未检查前置条件
- 触发条件：PHI 结果 temp 在代码生成阶段仍处于 `stack_offset == 0 && reg_low < 0`（例如被优化阶段标记 `no_storage`，`KroRegisterPacking.inc:289/481`、`KroTailCalls.inc:122/157`）。
- 后果：`alloc_temp_slot` 在 `stack_size` 固定之后追加 16 字节栈空间，超出已分配帧，破坏栈布局（与调用者参数区/红区重叠）。
- 最小修复：保证所有 temp 在 `plan_function_storage` 阶段完成分配；`alloc_temp_slot` 需要新增槽位时视为内部错误并中止。
- 备注：疑似，需验证 PHI temp 是否可能被标记 `no_storage`（`record_function_temporaries` 对 PHI 只设 `skip_register`，`KroRegisterPacking.inc:215-217`）。

##### [低] 58-18｜`if (!global)` 为恒不成立的死检查
- 位置：`KroCodegen.c:1446`（`KrtIRGlobal* global = &module->globals[i]; if (!global || !global->name)`）。
- 类别：5 死代码
- 触发条件：恒不成立——`global` 是对 `module->globals[i]` 取地址，永远非 NULL。
- 后果：给出“已做空指针防护”的错觉，真正未检查的是 `module->globals` 本身（`1444` 的循环上界来自 `module->global_count`，数组可能为 NULL 或更小）。
- 最小修复：删除 `!global`，改为检查 `module->globals` 非空且 `i < global_capacity`。
- 备注：无。

##### [低] 58-19｜直接调用时 `function_name == NULL` 静默 `break`
- 位置：`KroCodegen.c:918-921`（`const char* func_name = ...; if (!func_name) { break; }`）。
- 类别：2 诊断缺失；1 静默失败
- 触发条件：`KRT_IR_VALUE_FUNCTION` 值的 `data.function_name` 为 NULL（函数值构造失败或符号名为空）。
- 后果：整条调用指令被静默丢弃，不发射调用、不报错，程序行为缺失。
- 最小修复：`KrtError` 并中止，或回退到 `kro_add_undefined_symbol` 路径。
- 备注：无。

### 第五十九批：KroInteger.inc（全量）

#### 覆盖范围

- 审计对象：`Re.KrtC/src/Compiler/Backend/Kro/KroInteger.inc`，全量 898 行（分 1–300 / 301–600 / 601–898 三段读完）。
- 覆盖函数（按文件顺序）：`emit_reg_op`、`emit_shift_imm`、`emit_local_jump`、`patch_local_jump`、`emit_extend_scalar`、`emit_normalize_pair`、`emit_load_high`、`emit_load_pair`、`emit_load_for_type`（含 `#include "KroFloatCasts.inc"` 展开点）、`emit_load_condition`、`emit_store_value`、`emit_negate_pair_by_sign`、`emit_wide_division`、`emit_integer_shift`、`emit_reg_op32`、`emit_imm_op32`、`scalar_low_register`、`scalar_register32`、`emit_load_scalar32`、`has_scalar32_operands`、`can_emit_binary32`、`fits_signed_imm32`、`scalar_integer_constant`、`emit_imm_op64`、`scalar_register64`、`scalar_binary_encoding`、`emit_binary64`、`emit_binary32`、`emit_scalar_compare_flags`、`emit_multiply_pair`、`constant_shift_count`、`emit_shift_by_constant`、`emit_integer_binary`、`integer_condition`、`emit_integer_compare`。
- 核对方式：仅对调用方/契约做定向 grep 与窄窗口读取（`KroCodegen.c` 的 `emit_load_value_to_reg`/`emit_float32_conversion`/`emit_store_to_stack`/寄存器与栈帧约定、`KroRegisterPacking.inc` 的打包与临时寄存器分配、`KroWriter.c` 的重定位 addend 语义、`Integer.h` 的 `KrtIntegerNormalize`、`Tokenizer.c` 的 `KrtTokenIntegerBits/Common`、`X86Codegen.c`、`VmCodegen.c`）。
- 已确认无问题的部分：x86-64 编码（REX/ModRM/SIB、LEA、SHRD/SHLD、MUL/IMUL、IDIV/DIV、SETcc、重定位 addend）逐条核对正确；128 位乘加/移位/比较、宽除法恢复算法、`fits_signed_imm32` 与 `emit_imm_op*` 的符号扩展判断、`patch_local_jump` 的 rel32 计算均正确；`emit_extend_scalar` 在 bits=64/128 的 move 分支不会产生负移位（`bits < 64` 条件已挡住），故不列为问题。


##### [高] 59-1｜emit_store_value 对 ARG 下标缺少边界检查，可越界读/写
- 位置：`KroInteger.inc:236-245`（关键 237、245）
- 类别：8 未检查前置条件（越界）
- 触发条件：目的操作数为 `KRT_IR_VALUE_ARG` 且 `data.index < 0` 或 `>= ctx->current_param_count`（或超出 `KRT_KRO_MAX_ARGS`）。
- 后果：`ctx->arg_packed[index]`、`ctx->arg_stack_offsets[index]` 越界读，随后按越界 `slot` 调用 `emit_store_to_stack` 写栈，破坏栈帧；同一文件 `emit_load_high:108` 与 `KroCodegen.c:657/660` 都有 `index >= 0 && index < current_param_count` 守卫，唯独写路径没有。
- 最小修复：复用与 `emit_load_high` 相同的守卫；越界时回退到局部变量或报诊断并终止代码生成。
- 备注：同文件三种 ARG 访问三种口径（读低字有守卫、读高字有守卫、写无守卫）。

##### [高] 59-2｜窄整数除/模对除数 0 无检查，依赖 #DE，与宽路径及 X86 后端三重分叉
- 位置：`KroInteger.inc:787-814`（实际 `DIV/IDIV` 在 807）
- 类别：9 错误处理不一致 / 2 诊断缺失 / 3 语义分叉
- 触发条件：任意 ≤64 位整数 `KRT_IR_DIV`/`KRT_IR_MOD`，运行时除数为 0。
- 后果：直接执行 `48 F7 F1`（DIV）或 `48 F7 F9`（IDIV），触发硬件 #DE → SIGFPE 崩溃，无源码位置与诊断；X86 后端除零返回 -1（`X86Codegen.c:1044-1047`），本文件宽路径除零执行 UD2（`KroInteger.inc:293-295`），三处行为互不相同。
- 最小修复：统一除零策略（约定返回值或统一运行时错误例程），并让窄/宽路径一致；复用已有 `uses_division/uses_modulo` 标记。
- 备注：仅处理了 `INT64_MIN / -1` 溢出，未处理除零。

##### [高] 59-3｜宽整数除零仅执行 UD2，无诊断且与窄路径信号不同
- 位置：`KroInteger.inc:293-295`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：128 位 `KRT_IR_DIV`/`KRT_IR_MOD`，除数 RCX:R8 为 0。
- 后果：执行 `0F 0B`（UD2）→ SIGILL，无诊断；窄路径是 SIGFPE，X86 后端是返回 -1。
- 最小修复：与 59-2 统一为同一套除零处理。
- 备注：与 59-2 为不同代码点、不同信号，独立记录。

##### [高] 59-4｜emit_load_condition 直接测浮点位型，-0.0 被判为真
- 位置：`KroInteger.inc:195-202`（浮点加载分支 166-193）
- 类别：3 语义分叉
- 触发条件：浮点表达式直接作为条件（`IrGen.c:2405` 对 while 条件不做 bool 转换，语义分析也未限制条件类型）或 `CAST→BOOL`。
- 后果：`emit_load_for_type` 对 FLOAT32/FLOAT64 只保留/舍入位型，`emit_load_condition` 用 `TEST RAX,RAX` 判非零；-0.0（FLOAT32 `0x80000000`、FLOAT64 `0x8000000000000000`）被判为真，与“值 != 0”及 X86/Vm 的浮点比较语义不符。
- 最小修复：浮点源先与 0.0 比较（CMPSS/CMPSD，或按符号位+指数判零）再 SETcc；或在前端禁止浮点条件。
- 备注：NaN 判真与 C 一致，仅 -0.0 错误；`emit_load_condition` 也不区分 FLOAT32/FLOAT64 的零编码。

##### [高] 59-5｜emit_load_high 对未解析 VAR 静默把高字置 0，与低字回退口径不一致
- 位置：`KroInteger.inc:118-152`（148-152）
- 类别：1 静默失败 / 3 语义分叉
- 触发条件：`KRT_IR_VALUE_VAR` 既不在局部变量表（`find_local_var` 返回 NULL）、`kro_find_symbol` 也失败（符号缺失/声明缺失）。
- 后果：低字由 `emit_load_value_to_reg` 回退到 `find_or_alloc_local_var` 读未初始化栈（`KroCodegen.c:637-642`），而高字走 `slot=-1` 分支被置 0（`KroInteger.inc:151`），宽值静默错误；`kro_find_symbol` 的失败被完全吞掉，无任何诊断。
- 最小修复：与 `KroCodegen.c:637-642` 统一回退路径，或报告未解析符号并终止。
- 备注：`slot > 0` 的 else 分支同时承担“无效 slot”和“未解析”两种语义。

##### [中] 59-6｜emit_store_value 在 slot<=0 或局部变量表满时静默丢弃存储
- 位置：`KroInteger.inc:266-278`（273）
- 类别：1 静默失败
- 触发条件：`find_or_alloc_local_var` 返回 NULL（`KRT_KRO_MAX_LOCAL_VARS` 满，`KroCodegen.c:111-113`），或 `alloc_temp_slot` 返回 -1。
- 后果：`slot` 保持 -1/0，`if (slot > 0)` 直接跳过，赋值被静默丢弃，后续读到旧值或未初始化值，无诊断。
- 最小修复：失败时报告诊断并停止代码生成，不要静默跳过。
- 备注：G1 前提下 `KRT_MALLOC/KRT_CALLOC` 失败返回 NULL 且 `KrtMemoryReportError` 为空，同类失败均无信号。

##### [中] 59-7｜emit_load_high 的 ARG/VAR 打包字段分支缺少 <=64 守卫
- 位置：`KroInteger.inc:110-113`（ARG）、`121-124`（VAR）
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：`KrtTokenIntegerBits(value->value_type) > 64` 的值带有非零 `packed.bit_width`。
- 后果：把该字段当作高字加载（`emit_load_packed_field` 只取一个 64 位字段），宽值高字静默错误；低字路径 `KroCodegen.c:609/657` 均有 `KrtTokenIntegerBits(...) <= 64` 守卫，此处缺失。
- 最小修复：与 `KroCodegen.c:609/657` 一致，增加 `KrtTokenIntegerBits(value->value_type) <= 64` 条件。
- 备注：当前 `allocate_packed_field` 对 128 位返回 fragment=0，故为潜在/契约性缺陷；若将来允许宽值打包即触发。

##### [中] 59-8｜emit_shift_imm 不校验 count，静默截断为 uint8
- 位置：`KroInteger.inc:8-13`（12）
- 类别：8 未检查前置条件（移位）/ 1 静默失败
- 触发条件：调用方传入负数或 >255 的 `count`。
- 后果：`(uint8_t)count` 静默截断，x86 再按 6/5 位取模，实际移位次数与预期不符，生成错误码且无提示；对比 `emit_integer_shift:339-343` 对动态计数做了显式范围守卫。
- 最小修复：在辅助函数内断言 `0 <= count < 操作数宽度`，或返回失败并由调用方报告。
- 备注：当前所有调用方（`emit_extend_scalar`、`emit_normalize_pair`、`emit_integer_shift`、`emit_shift_by_constant`）传入值均在 0..63，属潜在缺陷。

##### [中] 59-9｜与 Vm 后端语义分叉：Vm 对 LSHIFT/RSHIFT（及 AND/OR/XOR/MOD 等）静默丢弃
- 位置：`KroInteger.inc:739-834`（Kro 正常生成移位/位运算），对照 `VmCodegen.c:233-289`（`default: break`）
- 类别：3 语义分叉 / 4 阶段不对齐
- 触发条件：同一份含 `KRT_IR_LSHIFT/RSHIFT`（以及 AND/OR/XOR/MOD/LE/GE/NE）的 IR 分别用 Kro 与 Vm 后端编译。
- 后果：Vm 后端这些指令落入 `default: break`，指令被静默丢弃，运算结果与 Kro 不一致（Kro 侧实现正确）。
- 最小修复：Vm 后端补齐 opcode 实现，或在驱动层禁止用 Vm 编译含这些指令的 IR 并给出诊断。
- 备注：缺陷在 Vm 后端；按“与 Vm 语义一致性”标准记录，供跨后端整改。

##### [中] 59-10｜与 X86 后端符号右移语义分叉：X86 恒用 SAR
- 位置：`KroInteger.inc:350`、`370`（无符号用 SHR），对照 `X86Codegen.c:878`（恒 `sar rax, cl`）
- 类别：3 语义分叉
- 触发条件：无符号整数的 `KRT_IR_RSHIFT`，且被移出位的最高有效位为 1。
- 后果：Kro 得逻辑移位（SHR），X86 得算术移位（SAR），同型同值结果不同；X86 后端全文无 `KrtTokenIsUnsigned` 处理。
- 最小修复：统一由类型符号性选择 `shr/sar`（X86 侧补齐无符号分支）。
- 备注：缺陷在 X86 侧；Kro 侧实现与类型系统一致。

### 第六十批：KroRegisterPacking.inc + KroLeafReturn.inc

#### 覆盖范围

- 全量读取（read 分段读完，无整文件通读其他源文件）：
  - `Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc`（608 行，实际 608 行）
  - `Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc`（实际 182 行，非任务描述的约 250 行）
- 仅为核对符号/调用方做了定向 grep 与小区间 read，确认了以下前置事实：
  - `KRT_KRO_PACKED_REG_COUNT = 5`，`g_packed_regs = {RBX=3, R12=12, R13=13, R14=14, R15=15}`（KroCodegen.h:11、KroCodegen.c:44-45）。
  - `KRT_KRO_MAX_ARGS = 128`，`ctx->current_param_count = min(param_count, 128)`（KroCodegen.h:10、KroCodegen.c:1317）。
  - `KRT_MALLOC -> KrtSafeMalloc`，失败 `return NULL`；`KrtMemoryReportError` 在 Allocator.c:425-435 为空操作（仅 `KRT_DEBUG` 断点），故分配失败零诊断。
  - `emit_extend_scalar`（KroInteger.inc:34-59）、`emit_shift_imm`（KroInteger.inc:8-13，4=SHL/5=SHR/7=SAR）、`integer_condition`（KroInteger.inc:836-851，返回 0x92..0x9F，减 0x20/0x10 后分别对应 short/near jcc）、`KrtIntegerNormalize`（Integer.h:16-26，符号扩展）。
  - 每函数重置：`arg_packed/arg_stack_offsets/arg_address_taken/packed_reg_bits/packed_reg_save_offsets/packed_reg_needs_zero` 均在 KroCodegen.c:1318-1323 清零；`prepare_temporary_storage` 失败会 `KrtError` 并中止（KroCodegen.c:1291-1294），故临时存储容量耗尽路径有诊断，不在本批条目内。
- 关键前提 G1 已应用：本批两条致命条目即 KRT_MALLOC/KRT_CALLOC 失败静默回退。


##### [致命] 60-1｜跨块临时寄存器分析中 KRT_MALLOC 失败被静默吞掉
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:386-387`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期（G1）
- 触发条件：`KRT_MALLOC((size_t)ctx->temp_slot_count * sizeof(*definition_block))` 返回 NULL（内存不足，或 `temp_slot_count` 很大导致请求过大）。
- 后果：`plan_cross_block_temporaries` 直接 `return false`，`allocate_temporary_registers` 中 `global_intervals` 置 false，跨块临时变量全部退化为块内分配；`KrtMemoryReportError` 为空操作，用户与调用方都收不到任何诊断，代码生成照常“成功”。这是 G1 描述的活路径零诊断。
- 最小修复：分配失败时记录错误（`KrtError`）并设置 ctx 失败标记，中止该函数代码生成；或至少让调用链把失败上报到 `KrtKrtGenerate`。
- 备注：同函数 456 行的 `KRT_CALLOC` 是同一模式，见 60-2。

##### [致命] 60-2｜跨块布尔表 KRT_CALLOC 失败被静默吞掉
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:456-457`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期（G1）
- 触发条件：`KRT_CALLOC(ctx->temp_slot_count ? ctx->temp_slot_count : 1, sizeof(*cross_block))` 返回 NULL。
- 后果：`cross_block = NULL`，`global_intervals = cross_block && ...` 静默变为 false，跨块临时寄存器分配被无声禁用；`KRT_FREE(cross_block)`（510 行）对 NULL 释放虽安全，但整条优化路径的失败不可观测，最终目标文件仍正常产出。
- 最小修复：分配失败时 `KrtError` 并中止代码生成，或显式回退并记录“跨块临时寄存器优化已禁用”。
- 备注：与 60-1 独立（不同分配点、不同数组），不可合并。

##### [高] 60-3｜allocate_packed_field 容量不足/128 位时静默返回，字段保持 0
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:3-7`、`18-20`
- 类别：静默失败 / 诊断缺失 / 契约破坏
- 触发条件：(a) 5 个打包寄存器剩余位宽都不足以容纳 `fragment`（`best < 0`）；(b) `type` 为 128 位，`bits % 64 == 0`，`fragment = 0`，第 5-7 行直接返回。
- 后果：`field->bit_width` 保持调用前的 0（`arg_packed` 每函数 memset、`KROLocalVar` 分配时 memset），`plan_function_storage`（546/552 行）据此静默改用栈分配。行为虽正确回退，但没有任何诊断，寄存器打包率下降不可观测；128 位走此路径时调用方 `allocate_value_stack` 按 `field->bit_width ? 8 : 16` 得 16 字节，与 128 位匹配，但注释与调用点均未说明“128 位必然不进打包”这一隐含契约。
- 最小修复：`allocate_packed_field` 返回 bool，失败/不支持时由调用方 `KrtError` 或至少记录；对 128 位直接走栈的约定写进注释。
- 备注：与 60-1/60-2 的分配失败不同，这里是容量/枚举范围导致的静默降级。

##### [高] 60-4｜plan_cross_block_temporaries 多处静默回退，无任何诊断
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:338-340`、`358-360`、`364-366`、`373-375`、`404-407`、`427-430`
- 类别：静默失败 / 诊断缺失 / 阶段不对齐
- 触发条件：函数块数达到 `KRT_KRO_ACYCLIC_BLOCK_LIMIT`（256）；BRANCH 的 `succ_count != 2`；JUMP 的 `succ_count != 1`；任一前向边 `target <= i`（回边/不可约图）；某 temp 定义块偏移越界或定义类型为非整数或位于 entry（`tail_site_count` 时）；任一使用点不满足 `first_position < position` 或不被定义块支配。
- 后果：统一 `return false` 或 `eligible[i] = false`，跨块临时寄存器优化被无声禁用或逐项丢弃，无任何诊断；用户无法区分“优化不适用”与“分析出错”。
- 最小修复：至少对“块数超限”“非拓扑边”这类异常输入 `KrtError`；对正常不适用路径可保留静默，但需与真正错误区分。
- 备注：`KRT_KRO_ACYCLIC_BLOCK_LIMIT` 为 256 的魔法上限（302 行），与 `KRT_KRO_TAIL_UPDATE_LIMIT`(5)、`KRT_KRO_TAIL_PREFIX_LIMIT`(64) 等上限无统一命名/配置来源。

##### [高] 60-5｜next_write[16] 为魔法上界，且以机器寄存器号而非打包索引索引
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:261`（定义）、`271`（`next_write[written] = here`）
- 类别：未检查前置条件 / 数组越界（潜在）/ 契约破坏
- 触发条件：`full_storage_register` 返回的 `field->reg`（机器寄存器号）>= 16。当前 `g_packed_regs` 最大为 `KRT_REG_R15 = 15`，故暂不触发；一旦 `g_packed_regs` 增加/改号（如加入 R16 或重排），`written` 即越界。
- 后果：`next_write[written]` 越界写，破坏栈上相邻对象（`locals[]`、`position` 等），且 `written >= 0` 检查（270 行）无法防御上界越界；静默内存破坏。
- 最小修复：把 `next_write` 改为按打包索引（0..KRT_KRO_PACKED_REG_COUNT-1）索引，或在写入前 `if ((unsigned)written >= sizeof(next_write)/sizeof(next_write[0])) return false;`。
- 备注：该数组还承担“寄存器改写位置”语义，用机器寄存器号做下标是隐式耦合，属共享可变状态与边界契约脆弱点。

##### [高] 60-6｜emit_store_parameter_word 对 index 无任何边界检查
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:596-607`（598 读 `func->params[index]`，601 读 `ctx->arg_packed[index]`，606 读 `ctx->arg_stack_offsets[index]`）
- 类别：未检查前置条件 / 数组越界（潜在）/ 契约破坏
- 触发条件：调用方传入 `index >= KRT_KRO_MAX_ARGS`(128) 或 `index >= func->param_count`。当前唯一潜在路径是 `KroTailCalls.inc:335`、`353` 的 `for (i = 0; i < func->param_count; i++)` 循环（未用 `current_param_count` 收敛），它依赖 `plan_tail_calls` 在 `KroTailCalls.inc:87` 的 `func->param_count != ctx->current_param_count` 提前返回来保证不越界。
- 后果：一旦该前置守卫被改动、绕过，或 `param_count` 在 plan 与 emit 之间变化，即对 `func->params`（IR 数组）、`ctx->arg_packed[128]`、`ctx->arg_stack_offsets[128]` 越界读写，静默生成错误代码或崩溃。
- 最小修复：函数入口加 `if (index < 0 || index >= ctx->current_param_count) { KrtError(...); return; }`；同时把 KroTailCalls 的循环上界改为 `ctx->current_param_count`。
- 备注：`emit_store_value`（KroInteger.inc:236-245）对 ARG 同样未做 `current_param_count` 检查，属同一契约缺口。

##### [高] 60-7｜emit_leaf_return_guard 先发射代码再决定是否采用，>16 时留下半激活冗余快路径
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:137-152`（发射在 137-146，判定在 149-152）
- 类别：阶段不对齐 / 死代码 / 配置漂移 / 共享可变状态
- 触发条件：`entry_leaf_return` 成功且 `ctx->tail_site_count > 16`（`ctx->optimization_level >= 3`）。
- 后果：比较 + 短跳转 + 内联 `mov RAX, imm/extend; ret` 序列已经写入代码缓冲，但 `rotated_tail_leaf` 保持 NULL、函数返回 NULL，于是 `fast_leaf == NULL`，entry 块与 leaf 块在 KroCodegen.c:1382-1388 仍被完整发射。结果是同一函数存在两条并行的叶返回路径（一条内联在 prologue 前，一条在 leaf 块），共享 R10/R11/RAX；`tail_site_count <= 16` 为无注释魔法上限，超限既无诊断也不影响正确性，属隐性分叉。
- 最小修复：把上限判定提到发射之前（超限直接 `return NULL`，不发射），或超限时仍设置 `rotated_tail_leaf` 并返回 leaf，保持单一快路径。
- 备注：`ctx->rotated_tail_leaf` 是跨函数复用的上下文状态，每函数在 KroCodegen.c:1312 清空，但本函数在未采用时仍会写它（150 行），语义易误读。

##### [高] 60-8｜emit_tail_backedge 在 trivial_leaf_value 失败时静默 return，丢弃返回路径
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:169-171`
- 类别：静默失败 / 诊断缺失 / 契约破坏
- 触发条件：`trivial_leaf_value(ctx, func, ctx->rotated_tail_leaf, &value)` 返回 false。当前因 `rotated_tail_leaf` 来自 `emit_leaf_return_guard` 中已验证成功的同一 leaf 而不可达，但函数自身无任何防御或断言。
- 后果：直接 `return`，此时比较与条件跳转（165-167 行）已发射，后续既无叶返回值、也无 `jmp epilogue`；控制流落入 `generate_block` 之后要发射的下一块或 epilogue，静默生成错误代码，且无诊断。
- 最小修复：失败时回退到 `emit_jump_to_block(ctx, func->entry_block)`（与 157-159 行一致）或 `KrtError` 后中止。
- 备注：与 60-7 同源（都依赖 rotated_tail_leaf 的事前验证），但失败点与后果独立，单独成条。

##### [中] 60-9｜allocate_value_stack 对任何未打包值一律分配 16 字节
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:86-92`
- 类别：资源与生命周期 / 契约破坏
- 触发条件：`field->bit_width == 0` 的所有参数/局部变量：8/16/32/64 位整数（未打包成功者）、`TOKEN_FLOAT32/FLOAT64`、指针/数组、`TOKEN_VOID`，以及 128 位整数。
- 后果：`current_stack_offset += 16`，但 `emit_store_value`/`emit_load_value_to_reg` 对 <=64 位只访问 8 字节（`emit_store_to_stack(slot)`、`emit_load_from_stack(slot)`），每个此类变量浪费 8 字节并在栈上留下 8 字节空洞；大函数栈帧无谓膨胀，`packed_reg_save_offsets` 之后的偏移全部顺延。
- 最小修复：按 `int bits = KrtTokenIntegerBits(type); ctx->current_stack_offset += (bits > 64) ? 16 : 8;`（浮点/指针按 8）。
- 备注：128 位与“未打包成功”共用 `field->bit_width == 0` 这一状态，无法区分，是尺寸判断退化的根因。

##### [中] 60-10｜emit_entry_compare 32 位与 64 位立即数路径归一化不一致
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:111`（原始 `data.integer` 截断）与 `116`（`scalar_integer_constant` 归一化）
- 类别：语义分叉 / 一致性
- 触发条件：`compare->operands[1]` 为 `KRT_IR_VALUE_INTEGER`，其 `data.integer` 未按自身 `value_type` 预先归一化（或宽度与 `type` 不一致）。
- 后果：32 位路径 `(uint32_t)compare->operands[1].data.integer` 直接截断，64 位路径 `scalar_integer_constant()` 走 `KrtIntegerNormalize`（符号扩展）；两条路径对同一语义常量可能得到不同位型，生成错误比较。当前依赖 IR 构造期已归一化（Ir.c:410）才未暴露。
- 最小修复：32 位路径统一改为 `(uint32_t)scalar_integer_constant(&compare->operands[1])`。
- 备注：`emit_scalar_compare_flags`（KroInteger.inc:683）也用原始 `data.integer`，但那是既有实现；本条只针对 KroLeafReturn.inc 内部两条路径的不一致。

##### [中] 60-11｜叶返回短跳转位移无范围检查即截断为 uint8_t
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:138-146`（145 行 `(uint8_t)(end - displacement - 1)`）
- 类别：未检查前置条件 / 溢出（潜在）
- 触发条件：内联叶返回序列（`emit_entry_scalar` 的立即数/扩展 + `RET`）字节数 > 127，使 `end - displacement - 1` 超出有符号 8 位范围。
- 后果：`uint8_t` 截断后短跳转到错误目标，静默误编译；当前序列约 11 字节故未触发，但 `emit_entry_scalar` 对 ARG 的扩展长度、立即数编码长度可变，无断言保护。
- 最小修复：写入前 `if (end - displacement - 1 > 127) { 回退到 near jump 或 KrtError }`。
- 备注：同文件 149 行的 `tail_site_count <= 16` 同属无检查魔法上限。

##### [低] 60-12｜packed_reg_needs_zero 只覆盖局部变量，不覆盖参数，初始化契约不对称
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:544-561`（参数打包在 544-548，needs_zero 仅在 549-561 的局部变量循环里置位）
- 类别：契约破坏 / 共享可变状态 / 注释与实现脱节
- 触发条件：某打包寄存器仅由参数占用（无同名局部变量触发 needs_zero），且该参数是偏移 0 的部分宽度字段（如 32 位）。
- 后果：prologue（KroCodegen.c:370-378）不会清零该寄存器；`emit_store_packed_field` 的快路径（52-55 行）执行 64 位 `mov`，把源寄存器高位垃圾写入该寄存器高位。当前 `emit_load_packed_field`/`scalar_low_register` 都按位域扩展/掩码，高位不可观测，但“寄存器初始为零”这一不变量在参数与局部变量之间不一致；一旦将来加入全宽读取（如把该寄存器当 64 位临时/累加器）会静默出错。
- 最小修复：对参数打包寄存器同样置 `packed_reg_needs_zero[j] = true`，或在 `emit_store_packed_field` 快路径改为按位域合并并注释该不变量。
- 备注：与 60-3 的“128 位不进打包”同属打包初始化契约的隐式约定，建议统一在头文件注释中固化。

### 第六十一批：KroTailCalls.inc + KroTailUpdates.inc + KroFloatCasts.inc

#### 覆盖范围

- 全量读取（分段 read，非通读其他文件）：
  - `Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc`（364 行，含 `#include "KroTailUpdates.inc"`）
  - `Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc`（264 行）
  - `Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc`（123 行）
- 调用方/契约核对仅限 grep 特定符号：`tail_site_count`、`tail_acc_opcode`、`tail_register_argument`、`tail_arg_offsets`、`early_tail_accumulate`、`deferred_tail_argument`、`no_storage`、`packed_reg_bits`、`arg_packed`、`find_temp_location`、`scalar_binary_encoding`、`scalar_register64`、`emit_load_for_type`、`emit_local_jump`、`allocate_packed_field`、`allocate_temporary_registers`、`forwarded_result_register`、`KRT_REG_*`、`KRT_MALLOC` 系列。
- G1（`KRT_MALLOC` 失败返回 NULL 而不 exit）：这三个被审文件内 **没有任何** `KRT_MALLOC/KRT_CALLOC/KRT_REALLOC/KRT_FREE` 调用，故 G1 的分配失败路径在本切片不直接触发。已按 G1 的“返回值可空、调用点零诊断”模式核查同类可空返回 `find_temp_location()`：`KroTailCalls.inc:116`、`:155`、`KroTailUpdates.inc:193` 三处均未显式判空，但其在调用前都已由 `single_use_temporary()`（内部 `find_temp_location` 判空）间接保证非空，当前不可达空指针，记为契约脆弱点（见 61-4 同类）。
- 关键寄存器事实（用于判定可达性）：`KRT_REG_RAX=0 … KRT_REG_R15=15`；`g_packed_regs = {RBX,R12,R13,R14,R15}`；`allocate_packed_field()` 只从 `g_packed_regs` 取寄存器；`plan_tail_calls()` 在累加器启用时把 `packed_reg_bits[4]（R15）`、宽度 >64 时再把 `[3]（R14）` 占满；`scalar_low_register()` 对宽度 >64 直接返回 -1。


##### [致命] 61-1｜float→宽整数转换越界/NaN/无符号负数静默出错
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc:88-123`（关键：`:91-93`、`:111-118`）
- 类别：语义分叉；未检查前置条件（浮点转换/饱和）；诊断缺失；静默失败
- 触发条件：`emit_load_for_type()` 走 `source_float && KrtTokenIntegerBits(type) > 64` 分支（`KroInteger.inc:173-175`），即 double → 128 位整数。具体触发：
  1. `|x| >= 2^128`（指数域 > 0x47E）→ `zero_large` 直接清零；
  2. `NaN / ±Inf`（指数域 0x7FF）→ 同样落到 `zero`；
  3. 目标类型为**无符号** 128 位且 `x < 0` → 仍执行 `neg rax; adc rdx,0; neg rdx`（`:113`），得到 `2^128-|x|` 的回绕值；
  4. 目标类型为**有符号** 128 位且 `x >= 2^127` → 保留原值，最高位置位后按有符号解释成负数（回绕）。
- 后果：越界与 NaN/Inf 全部静默变成 0；无符号目标遇到负数得到回绕巨值；有符号目标在 `[2^127, 2^128)` 回绕。没有任何诊断，也没有饱和/截断约定，与常见语言语义（饱和、截断或显式 UB 陷阱）均不一致，且 4 种情形行为互不相同（0 / 回绕 / 保留）。
- 最小修复：在 `emit_float_wide_cast()` 入口按 `KrtTokenIsUnsigned(type)` 与目标位宽显式定义语义：无符号目标负数 → 0 或 UINT128_MAX；有符号目标越界 → INT128_MIN/MAX 饱和；NaN/Inf → 明确值或 `KrtError` 诊断。至少把 `zero_small/zero_large` 的“清零”改为饱和，并区分符号。
- 备注：`emit_integer_float_cast()`（`:68-86`）方向相反，未发现同类问题；本条只针对 double→宽整数。

##### [中] 61-2｜`cast_saved_offset()` 对未知寄存器返回 0，恢复时写错寄存器
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc:15-23`（使用点 `:27`、`:30`）
- 类别：契约破坏；静默失败
- 触发条件：`emit_cast_save()/emit_cast_restore()` 的 `low` 或 `high` 不是 `{RAX,RCX,RDX,R8,R9}` 之一（例如 R10/R11/RBX/R12-R15）。`cast_saved_offset()` 走 `default: return 0`，而 0 号槽是 `emit_cast_save()` 压栈的 **R9** 保存槽（`:3` 的压栈顺序 RAX,RCX,RDX,R8,R9）。
- 后果：`emit_cast_restore()` 把转换结果写进 `[rsp+0]`，随后 `pop r9` 把它当作 R9 的旧值恢复——结果被送进 R9，而调用方期望的目标寄存器拿到的是旧值。整条转换静默算错。
- 最小修复：`cast_saved_offset()` 对未知寄存器返回 -1 并在 `emit_cast_save/restore` 中断言或回退；或把 `low/high` 限定为 `{RAX,RDX,RCX,R8}` 并加 `KrtError`。
- 备注：当前唯一调用链 `emit_load_for_type()` 只传 `RAX/RDX` 与 `RCX/R8`（`KroInteger.inc:692,760,871`），故为**潜伏契约破坏**，一旦新增调用即触发。

##### [中] 61-3｜`emit_tail_update()` 先发射指令后校验编码，失败留下半条机器码
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc:229-234`
- 类别：错误处理不一致；静默失败；资源与生命周期（代码缓冲区）
- 触发条件：`scalar_binary_encoding(update->opcode, &operation, &opcode)` 返回 false。`emit_tail_update()` 在 `:229` 已经执行 `emit_load_value_to_reg(ctx, left, result_reg)`，直到 `:232` 才校验，失败直接 `return`。
- 后果：代码缓冲区里留下一条没有对应操作的 `mov`，后续字节错位；函数返回后调用方 `emit_scheduled_tail_updates()` 仍继续 `emit_tail_backedge()`，把错误机器码当成成功路径，无任何诊断。
- 最小修复：把 `scalar_binary_encoding()` 校验移到 `:229` 之前；或让 `emit_tail_update()` 返回 bool，失败时向上传播并放弃本次尾调用优化。
- 备注：`build_tail_update_plan()` 在规划期已用同一函数校验过，故当前不可达；但规划期与发射期是两次独立重建（`plan_tail_register_updates()` 与 `emit_scheduled_tail_updates()`），契约上必须自洽。

##### [中] 61-4｜尾调用贡献寄存器在参数快照前捕获，快照会覆盖 RAX/RDX
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc:326`（捕获）、`:335-341`（快照覆盖 RAX/RDX）、`:329`（仅在 `tail_register_argument` 分支防御 RAX）、`:343-348`（使用）
- 类别：契约破坏；共享可变状态（寄存器）；未检查前置条件
- 触发条件：`emit_tail_call()` 走通用路径（`tail_register_argument == false`，即 `param_count >= 2`）且 `slot->tail_combine != NULL`。`:326` 先取 `contribution_reg = scalar_low_register(ctx, contribution)`；随后 `:335-341` 的快照循环用 `emit_load_pair(..., RAX, RDX)` 把 RAX/RDX 全部改写；`:343-348` 却在 `contribution_reg >= 0` 时直接拿旧寄存器做 `emit_tail_accumulate(ctx, true, contribution_reg)`。`:329` 只为 `tail_register_argument` 分支选了 R9 来避让 RAX，快照分支没有对应保护。
- 后果：一旦 `contribution_reg ∈ {RAX, RDX}`，累加器加到的是最后一个参数的快照值而非贡献值，尾调用返回结果静默错误。
- 最小修复：把贡献值的读取/加载移到快照之后（或在快照前把贡献溢出到栈/安全寄存器）；`contribution_reg == RAX` 的防御要对两条分支一致。
- 备注：**当前可达性受限**：`scalar_low_register()` 对宽度 >64 返回 -1；参数与临时寄存器都取自 `g_packed_regs={RBX,R12..R15}`，而 `plan_tail_calls()` 已把 R15（宽度 >64 时含 R14）占满，故 `contribution_reg` 实际只会是 RBX/R12/R13 或 -1，暂不会命中 RAX/RDX。但 `:329` 的存在说明作者认为 RAX 可达，IR/分配器一旦变化即变现为静默错算，属零容忍范围。

##### [低] 61-5｜`emit_tail_update()` 的 RCX 备用分支不可达（死代码）
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc:238-241`
- 类别：死代码；注释与实现脱节
- 触发条件：`right_reg < 0` 时执行 `right_reg = result_reg == KRT_REG_RAX ? KRT_REG_RCX : KRT_REG_RAX;`。
- 后果：`result_reg`（`:228`）只在 `right_reg == target && left_reg != target` 时才取 RAX；而 `right_reg < 0` 时 `right_reg == target` 恒不成立，故 `result_reg == target`。`target = ctx->arg_packed[parameter].reg` 只可能取 `g_packed_regs={RBX,R12..R15}`（`allocate_packed_field()` 仅从该集合分配），永远不是 RAX。因此 `KRT_REG_RCX` 分支恒不执行。
- 最小修复：删除 RCX 分支，或改为 `right_reg = KRT_REG_RAX;` 并同步删除 `:226-227` 中关于 RAX/参数寄存器集的过时注释。
- 备注：若他处把 `arg_packed` 改为可放入 RAX/RCX，该分支会立即变成真实的寄存器覆盖 bug。

##### [低] 61-6｜`tail_update_leaf()` 对 COPY/CAST 链无环检测，循环引用导致编译器死循环
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc:53-60`
- 类别：未检查前置条件；资源与生命周期（编译期挂死）
- 触发条件：`value` 沿 `definition->operands[0]` 下行时出现自引用（`t = copy t`）或互引用环（`t1 = copy t2; t2 = copy t1`），且两者 `use_count == 1`（`tail_update_definition()` 的要求）。`while (value.type == KRT_IR_VALUE_TEMP)` 没有访问深度上限或已访问集合。
- 后果：`plan_tail_register_updates()` → `build_tail_update_plan()` → `tail_update_leaf()` 无限循环，编译器挂死，无超时无诊断。
- 最小修复：加深度上限（如 `KRT_KRO_TAIL_PREFIX_LIMIT`）或 `definitions` 已收录集合判重，超限即返回 false（回退到普通快照路径）。
- 备注：`build_tail_update_plan()` 的 `plan->prefix` 本身有上限，但互引用环的两个节点都在 prefix 内，不会被前缀上限截断。

##### [低] 61-7｜头注释只声明加法/乘法，实现还包含 AND/OR/XOR
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc:1-3`（实现：`:16-19` `associative_integer_opcode()`）
- 类别：注释与实现脱节
- 触发条件：阅读 `:1-3` 的说明「Integer associative returns can carry an accumulator because fixed-width addition/multiplication wrap modulo 2^N」。
- 后果：注释把累加器限定为加法/乘法，实际 `associative_integer_opcode()` 还接受 `KRT_IR_AND/OR/XOR`，且 `emit_tail_accumulator_init()`（`:180`）为 AND 选 `UINT64_MAX`、OR/XOR 选 0。注释误导后续维护者对恒等元与溢出语义的判断。
- 最小修复：把注释改为「addition/multiplication/and/or/xor」，或显式列出恒等元表。
- 备注：语义本身（结合律 + 固定宽度回绕）对这四个算子成立，仅注释不准确。

### 第六十二批：Tokenizer.c（全量）

#### 覆盖范围

- 全量读取 `Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c`（1232 行，分三段读完）：关键字表、`lexer_advance/skip_whitespace`、数字/标识符/字符/字符串读取、`lexer_next_token` 全部运算符分支、`peek/peek_nth`、`token_free`、`token_type_to_string`、整数类型辅助函数。
- 契约核对（只读少量行/单符号 grep）：`Tokenizer.h`（Token/Lexer 结构、函数声明）、`Accelerator.h/.c`（`accelerator_is_alpha/digit/octal_digit/hex_digit`、`encode_utf8`）、`Arena.h/.c`（`KrtArenaAlloc/Realloc` 失败返回 NULL 语义）、`Allocator.c`（`KrtSafeStrdup`、`KrtMemoryReportError` 确为空操作）、`IntegerWidths.def`、`Core/Utils/KrtCommon.h`（`KRT_STRDUP`→`KrtSafeStrdup`）、调用方 `CompilerPipeline.c` 的 error_count 检查与 `ParserStatement/ParserExpression` 的 peek 用法。
- 已应用关键前提 G1：`KRT_MALLOC/KRT_STRDUP/KrtArenaAlloc` 失败返回 NULL 且不 exit，`KrtMemoryReportError` 为空操作，故所有分配失败路径均为“活路径 + 零诊断”。前人指出的 `Tokenizer.c:428/436` 已核对并纳入（见 62-5）。


##### [致命] 62-1｜UTF-8 前导字节触发无限递归，栈溢出崩溃
- 位置：`Tokenizer.c:798-820`（关键 810-819）
- 类别：8 未检查前置条件（递归无界）
- 触发条件：源码中任意非字符串/注释/字符字面量位置出现字节 `0xC0..0xF7`（即几乎任意 UTF-8 非 ASCII 字符，如 `var café = 1;`、中文标识符、emoji）。
- 后果：`default` 分支算出 `bytes_remaining` 后，`while` 从当前 lead byte 开始判断 `0x80..0xBF`，lead byte 本身不是 continuation，立即 `break`，随后 `return lexer_next_token(lexer)` 在 `position` 未前进的情况下递归；无限递归直到栈溢出，编译器进程崩溃。
- 最小修复：进入 `while` 前先 `lexer_advance(lexer)` 消费 lead byte，或从 `position+1` 开始校验 continuation；并给递归设深度上限。
- 备注：`accelerator_is_alpha` 仅 ASCII（`Accelerator.c:83-85`），因此非 ASCII 字节必然走到该分支。字节 `0x80..0xBF` 走 `TOKEN_UNKNOWN`，`0xF8..0xFF` 走 62-21。

##### [致命] 62-2｜字符字面量 arena 分配失败时返回指向栈局部数组的悬垂指针
- 位置：`Tokenizer.c:409-415`（`char value[2]` 定义于 349）
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：`KrtArenaAlloc(lexer->arena, 2)` 返回 NULL（OOM，G1）。
- 后果：`char_str ? char_str : value` 返回局部数组 `value` 的地址；函数返回后该数组生命周期结束，调用方拿到悬垂 `token.value`，后续 `strlen/strdup/strcmp` 为未定义行为；且此路径不增加 `error_count`，无任何诊断。
- 最小修复：分配失败时统一返回 `TOKEN_EOF` 并 `lexer->error_count++`，或把 `value` 改为调用方传入/堆分配；禁止把栈地址塞进 `Token`。
- 备注：与 62-4/62-5 同属 G1，但此路径不仅静默还产生悬垂指针。

##### [致命] 62-3｜字符串字面量尾部反斜杠越界读（越过 NUL）
- 位置：`Tokenizer.c:441-513`（关键 443、509、431）
- 类别：8 未检查前置条件（越界）
- 触发条件：字符串在遇到 closing `"` 前以 `\` 结尾并直接碰到源码 NUL，例如 `"abc\`（无闭合引号）。
- 后果：443 处 `escape` 取到 `'\0'`，走 `default` 的 504-505 写入 `'\\'` 与 `'\0'`，随后 509 无条件 `lexer_advance` 把 `position` 推到 NUL 之后（`strlen+1`）；431 的循环条件与后续写入在分配边界外读写堆内存，属未定义行为，可能崩溃或把相邻内存当字符串内容。
- 最小修复：在 441/509 前后检查 `lexer->source[lexer->position] == '\0'`，反斜杠后遇 NUL 直接判未终止并 `error_count++`；默认转义写两个字节前也要再确认容量。
- 备注：`source_owned` 是 `KRT_STRDUP` 的精确 `len+1` 分配，无多余字节可利用。

##### [致命] 62-4｜arena 分配失败静默伪装成 EOF（数字/标识符路径，G1）
- 位置：`Tokenizer.c:305-308`（数字）、`Tokenizer.c:326-329`（标识符）
- 类别：1 静默失败 / 10 资源与生命周期
- 触发条件：`KrtArenaAlloc(lexer->arena, length + 1)` 返回 NULL。
- 后果：返回 `TOKEN_EOF, ""`，不增加 `error_count`、不设 `error_line`；解析器把 OOM 当作正常文件结束，剩余源码被静默丢弃，流水线 `error_count` 检查（`CompilerPipeline.c:249/281`）也看不到任何错误。
- 最小修复：分配失败时返回带错误语义的 token（或置 `error_count`/`error_line` 并返回 `TOKEN_UNKNOWN`），绝不复用 `TOKEN_EOF`。
- 备注：G1 的直接体现；`KrtMemoryReportError` 为空操作（`Allocator.c:425-435`），上层无诊断。

##### [致命] 62-5｜arena 分配/realloc 失败静默伪装成 EOF（字符串路径，G1；含前人已指出 428/436）
- 位置：`Tokenizer.c:426-429`（初始分配）、`Tokenizer.c:434-437`（扩容）
- 类别：1 静默失败 / 10 资源与生命周期
- 触发条件：字符串初始 `KrtArenaAlloc(arena, 32)` 失败，或扩容 `KrtArenaRealloc(arena, value, capacity/2, capacity)` 返回 NULL。
- 后果：返回 `TOKEN_EOF, ""`，不计错误；长字符串解析中一旦 OOM，整个token流提前终止且零诊断。已核对前人指出的 428/436 确为此问题。
- 最小修复：同 62-4；另扩容失败时保留旧 `value` 并标记错误，避免丢失已读内容。
- 备注：`KrtArenaRealloc` 失败时旧指针仍有效（`Arena.c:138-168`），但代码直接返回 EOF 丢弃。

##### [高] 62-6｜字符字面量 `\xNN` 被多一次 advance 误判为“未终止”
- 位置：`Tokenizer.c:384-396`、`402`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：任意 `'\x41'`、`'\x4'`、`'\x'`（`\x` 后 0~2 个十六进制位）。
- 后果：`\x` 分支在 384 消费 `x` 后，循环 386-389 每读一位就 `advance`，结束时 `position` 已落在 closing `'` 上；随后 396 无条件再 `advance` 跳过该引号，402 检查的是引号之后的字节，必然 `!= '\''` → `error_count++` 并返回 `TOKEN_EOF`。即字符 `\x` 转义完全不可用。
- 最小修复：`\x` 分支结束后 `continue`/跳过 396（与字符串版 `Tokenizer.c:476-484` 的 `continue` 对齐），或把 396 的 advance 移入各简单转义分支。
- 备注：字符串版 `\x` 用 `continue` 正确，字符版缺失，属同一语义在两个字面量间的实现分叉。

##### [高] 62-7｜字符字面量尾部反斜杠越界读
- 位置：`Tokenizer.c:351-402`（关键 396、402）
- 类别：8 未检查前置条件（越界）
- 触发条件：字符字面量以 `\` 结尾并碰到源码 NUL，例如 `'\`（无闭合引号）。
- 后果：353 取到 `escape='\0'`，392 写入 `value[0]='\0'`，396 `advance` 把 `position` 推到 NUL 之后，402 在边界外读取 `source[position]`，未定义行为。
- 最小修复：反斜杠后先判断 NUL，直接走未终止错误路径；396 前加边界检查。
- 备注：与 62-3 同源（默认转义后无条件 advance）。

##### [高] 62-8｜未终止字符串/字符/块注释统一返回 TOKEN_EOF，与真实 EOF 混淆
- 位置：`Tokenizer.c:224-228`（块注释）、`402-406`（字符）、`516-520`（字符串）
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：`"abc`、`'a`、`/* abc` 等未终止构造。
- 后果：词法错误与文件结束共用 `TOKEN_EOF`，解析器无法区分，剩余 token 全部消失；只留一个 `error_count` 与起始行，后续阶段只能报笼统的 “unterminated … near line N”（`CompilerPipeline.c:251/283/293`）。
- 最小修复：引入 `TOKEN_LEX_ERROR`/`TOKEN_UNKNOWN` 之类可区分类型，或让调用方在 EOF 时强制检查 `error_count` 并携带列号与原因。
- 备注：`TOKEN_EOF` 还被 62-4/62-5 的 OOM 路径复用，进一步放大混淆。

##### [高] 62-9｜`\0` / `\x00` 在字符串中写入嵌入 NUL，导致静默截断
- 位置：`Tokenizer.c:463-465`（字符串 `\0`）、`476-484`（字符串 `\x`）、`370-372`（字符 `\0`）
- 类别：1 静默失败 / 3 语义分叉
- 触发条件：字符串含 `\0` 或 `\x00`（以及 `\x` 无数字）等产生 0x00 的转义。
- 后果：`Token.value` 是无长度字段的 `char*`，嵌入 NUL 后所有 `strlen/strdup/strcmp` 消费者只能看到 NUL 之前的内容，后半段字面量被静默丢弃；如 `"a\0b"` 实际变成 `"a"`。
- 最小修复：为 `Token` 增加长度字段，或拒绝在字符串/字符中产生嵌入 NUL 并给出诊断。
- 备注：与 62-18 的转义不一致叠加；字符 `\0` 还会得到空字符串。

##### [高] 62-10｜peek/peek_nth 不恢复 error_count/error_line，lookahead 污染词法错误状态
- 位置：`Tokenizer.c:829-841`、`843-863`
- 类别：7 共享可变状态 / 9 错误处理不一致
- 触发条件：对含未终止字面量或非法字符的区域做 `lexer_peek_token`/`lexer_peek_nth_token`；或重复 peek。
- 后果：peek 内部调用 `lexer_next_token` 会 `error_count++` 并覆盖 `error_line`，而函数只恢复 `position/line/column`；重复 peek 使计数虚增、行号被最后一次 peek 覆盖，流水线据此报告错误，与实际消费位置不一致。
- 最小修复：进入时保存 `error_count/error_line`（以及 arena 状态）并在返回前恢复；或把 peek 实现为无副作用的纯预览。
- 备注：`ParserStatement.c:1534` 的 `for (look=1; look<256; look++)` 会放大该问题。

##### [高] 62-11｜字符串 `\U` 转义在容量边界被静默丢弃
- 位置：`Tokenizer.c:432-439`、`494-501`
- 类别：1 静默失败 / 8 未检查前置条件（缓冲区容量）
- 触发条件：字符串已写 `capacity-3` 个字节时遇到 `\U0001F600` 等需要 4 字节 UTF-8 的转义（如 capacity=32、length=29）。
- 后果：432 的 `length + 2 >= capacity` 不触发扩容，`capacity-length==3`；`encode_utf8` 对 `codepoint >= 0x10000` 要求 `max_length >= 4`，返回 0（`Accelerator.c:134-137`），`length += 0` 后 `continue`，该转义被无声删除，字符串内容缺失且无任何诊断。
- 最小修复：把容量检查改为按最坏 4 字节预留（如 `length + 4 >= capacity`），或检查 `encode_utf8` 返回值并在 0 时报错。
- 备注：`encode_utf8` 的 0 返回值在 485-501 三处 `length +=` 调用点均未被检查。

##### [高] 62-12｜`\U` 八位十六进制解析有符号溢出 UB 且无 Unicode 范围校验
- 位置：`Tokenizer.c:497-499`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：`\U` 后跟 8 个十六进制位且值超过 `INT_MAX`（如 `\UFFFFFFFF`），或值超过 `0x10FFFF`（如 `\U00110000`）。
- 后果：`unicode_val = unicode_val * 16 + ...` 对 `int` 做乘法，8 位十六进制可溢出，属有符号溢出未定义行为；同时 `encode_utf8` 不拒绝 `> 0x10FFFF`，会产出非法 UTF-8 字节序列。
- 最小修复：用 `unsigned` 累积并检查 `<= 0x10FFFF`，越界时报词法错误。
- 备注：`\u`（4 位）无溢出，但仍有范围/代理项问题（62-13）。

##### [高] 62-13｜`\u`/`\U` 代理项码点被静默编码为非法 UTF-8
- 位置：`Tokenizer.c:485-501`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：`\uD800`、`\uDFFF`、`\U0000D800` 等代理项码点。
- 后果：`encode_utf8`（`Accelerator.c:126-133`）按 3 字节模板输出 `ED A0 80` 等，即 UTF-8 标准禁止的代理项编码；词法层不报错，非法字节流入 AST/后续阶段。
- 最小修复：在 `encode_utf8` 调用前拒绝 `0xD800..0xDFFF`，或让 `encode_utf8` 返回 0 并由调用方报错。
- 备注：与 62-11/62-12 同属 `\u`/`\U` 校验缺失。

##### [高] 62-14｜八进制字面量 `08`/`09`/`018` 被静默切分为两个 NUMBER
- 位置：`Tokenizer.c:266-271`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：`08`、`09`、`018`、`0789` 等以 0 开头且次字符是数字但含 8/9。
- 后果：进入八进制分支后 `while (is_octal_digit(...))` 只吃掉 `0`（以及后续 0-7），遇到 8/9 停止，得到 `NUMBER "0"`，下一个 token 是 `NUMBER "8"`；解析器看到两个相邻数字，报“期望 ;”之类与真实原因无关的错误，且位置指向第二个数字。
- 最小修复：八进制分支遇到 8/9 时判为词法错误（或按十进制整体读取），不要静默截断。
- 备注：`0x`/`0b` 分支无此问题；`0` 后跟 `.`/`e`/`E` 已被 266-268 排除。

##### [高] 62-15｜残缺指数/小数尾部被静默接受并由 atof 吞掉
- 位置：`Tokenizer.c:278-302`（关键 294-302）
- 类别：1 静默失败 / 3 语义分叉
- 触发条件：`1e`、`1E`、`1e+`、`1e-`、`1.5e` 等指数后无数字；`1.` 等。
- 后果：词法层照常产出 `TOKEN_NUMBER`，无诊断；`ParserExpression.c:169` 用 `atof` 转换，`atof("1e") == 1.0`，于是 `x = 1e;` 被静默当成 `1.0`，语言层面错误的字面量变成合法值。
- 最小修复：指数分支要求至少一位数字，否则 `error_count++` 并返回错误 token；`1.` 是否合法需按语言规范明确。
- 备注：`0x`、`0b` 无数字会在下游 `KrtIntegerParse` 报错（非静默），但 `1e` 因 atof 而静默，危害更大。

##### [高] 62-16｜`token_type_to_string` 漏掉 21 个词法可产出 token，诊断显示 UNKNOWN
- 位置：`Tokenizer.c:872-1174`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：解析器对以下 token 调用 `token_type_to_string`：`TOKEN_FN`、`TOKEN_DO`、`TOKEN_FOREACH`、`TOKEN_IN`、`TOKEN_STRUCT`、`TOKEN_INTERFACE`、`TOKEN_ENUM`、`TOKEN_NAMESPACE`、`TOKEN_CHAR`、`TOKEN_BITWISE_AND`、`TOKEN_BITWISE_OR`、`TOKEN_BITWISE_XOR`、`TOKEN_LSHIFT`、`TOKEN_RSHIFT`、`TOKEN_POWER`、`TOKEN_NULL_COALESCING`、`TOKEN_QUESTION_DOT`、`TOKEN_SWITCH`、`TOKEN_CASE`、`TOKEN_BREAK`、`TOKEN_CONTINUE`、`TOKEN_DEFAULT`。
- 后果：这些分支全部落到 `default: return "UNKNOWN"`，语法错误信息变成 “expected X, got UNKNOWN”，无法定位是哪个关键字/运算符；`TOKEN_ATTRIBUTE`、`TOKEN_AUTO`、`TOKEN_PRINT` 同样缺失（当前无关键字产出）。
- 最小修复：补齐所有 `KrtTokenType` 分支，并加 `_Static_assert`/单元测试保证枚举与 switch 同步。
- 备注：`TOKEN_TYPE_STRING` 返回 `"STRING"` 而 `TOKEN_STRING` 返回 `"STRING_LITERAL"`，命名也易误导。

##### [高] 62-17｜空字符字面量 `''` 被静默接受
- 位置：`Tokenizer.c:397-407`
- 类别：2 诊断缺失 / 3 语义分叉
- 触发条件：源码 `''`。
- 后果：397 的条件排除了 `'` 与 `\0`，直接落到 402 的引号检查并通过，返回 `TOKEN_CHAR_LITERAL` 且 `value[0]='\0'`（空字符串）；非法字面量无诊断。
- 最小修复：在 402 前判断是否读到有效字符，空字面量报词法错误。
- 备注：与 62-6 同属字符字面量校验缺失。

##### [高] 62-18｜字符与字符串转义语义不一致，字符未知转义静默丢弃反斜杠
- 位置：`Tokenizer.c:382-394`（字符）对照 `475-507`（字符串）
- 类别：3 语义分叉 / 13 注释与实现脱节
- 触发条件：未知转义如 `'\q'` 与 `"\q"`；或 `'\u0041'`、`'\U0041'`。
- 后果：字符串默认分支保留 `'\\'+escape` 两字符，字符默认分支只写 `escape`（丢掉反斜杠）；字符不支持 `\u`/`\U`，`'\u0041'` 会被当成普通字符 `u` 并因 402 检查失败而报未终止。同一转义语法在两个字面量中结果不同，且都无诊断。
- 最小修复：抽取统一转义解析函数，或明确规范并让字符版支持 `\u`/`\U`、未知转义统一报错。
- 备注：与 62-6、62-17 一起构成字符字面量处理的系统性缺口。

##### [高] 62-19｜`lexer_peek_nth_token` 不回收 arena 且 O(n²) 重复词法分析
- 位置：`Tokenizer.c:843-863`；放大点 `ParserStatement.c:1534-1566`
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：调用方在循环中递增 `n` 反复 peek（现有解析器 `for (look=1; look<256; look++)` 正是此模式）。
- 后果：每次 peek 都从保存位置重新产生前 `n` 个 token，并把每个 token 的 `value` 追加进 arena 且不回滚；`position` 恢复但 arena 分配保留，造成内存随 look 平方级膨胀，可把系统推向 OOM，进而触发 62-4/62-5 的“OOM 伪装 EOF”。
- 最小修复：peek 期间使用独立/可重置 arena，或缓存已产生 token；至少提供 `lexer_arena_save/restore`。
- 备注：`token_free` 是空操作（62-25），无法靠它回收。

##### [高] 62-20｜`lexer_peek_nth_token` 对 `n <= 0` 返回 `type = -1`、`value = NULL`
- 位置：`Tokenizer.c:843-856`
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：调用方传入 `n == 0` 或负数（公共 API 未限制）。
- 后果：`for` 循环不执行，返回未初始化的 `Token`（仅 `type=-1`），`value` 为 NULL；调用方若解引用 `value` 或把 `-1` 当枚举使用，行为未定义，且无任何断言/诊断。
- 最小修复：`n <= 0` 时返回 `TOKEN_EOF` 或 `TOKEN_UNKNOWN` 并 `error_count++`；头文件注明 `n >= 1`。
- 备注：当前调用点均传 `>=1`（`ParserStatement.c:1534/1608`），属潜在契约缺口。

##### [高] 62-21｜字节 `0xF8..0xFF` 静默返回 TOKEN_EOF
- 位置：`Tokenizer.c:804-807`、`826`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：源码中出现 `0xF8..0xFF` 字节（非法 UTF-8 首字节，如某些 Windows-1252 字节）。
- 后果：`else break` 跳出 `switch`，直接执行 826 返回 `TOKEN_EOF`，不计 `error_count`；词法流被当作正常结束，剩余源码全部丢失。
- 最小修复：该分支改为 `lexer_advance` 后返回 `TOKEN_UNKNOWN` 并 `error_count++`。
- 备注：与 62-1 同属 `default` 的 UTF-8 处理，但结果一个是崩溃、一个是静默 EOF。

##### [高] 62-22｜词法错误诊断只有计数+行号，无列号/原因，且 `error_line` 被覆盖
- 位置：`Tokenizer.c:225-226`、`403-404`、`517-518`
- 类别：2 诊断缺失
- 触发条件：任意多次词法错误。
- 后果：只累加 `error_count` 并写单个 `error_line`，没有列号、没有错误文本、不保留多条错误；后续错误覆盖前一条行号，调用方（`CompilerPipeline.c:251/283/293`）只能报 “near line N”。对 62-6 这类逻辑错误完全无法定位。
- 最小修复：引入错误数组（行/列/原因码），或至少提供列号与首/末错误位置。
- 备注：`error_count` 还被 62-10 的 peek 副作用污染。

##### [中] 62-23｜`lexer_reset` 不完全重置且无任何调用方（死代码）
- 位置：`Tokenizer.c:569-573`
- 类别：5 死代码 / 7 共享可变状态
- 触发条件：调用 `lexer_reset`（当前代码库无调用方，grep 仅命中定义与头声明）。
- 后果：只重置 `position/line/column`，不重置 `error_count/error_line`、不清空 arena、不重置 `current_token`；若将来用于二次词法分析，旧错误与旧 arena 内容会泄漏到新一次分析，且 `error_count` 继续累加。
- 最小修复：删除该函数，或补齐 `error_count/error_line` 重置、`KrtArenaReset` 与 `current_token` 清空。
- 备注：`KrtArenaReset` 存在于 `Arena.c:171`，可直接使用。

##### [中] 62-24｜`Lexer.current_token` 是从未初始化/读写的死字段
- 位置：`Tokenizer.h:215`；`Tokenizer.c:528-555`
- 类别：5 死代码 / 7 共享可变状态
- 触发条件：任何读取 `lexer->current_token` 的代码（当前词法器自身从不使用）。
- 后果：`lexer_create` 用 `KRT_MALLOC`（非 calloc）分配 `Lexer`，`current_token` 保持未初始化内存；字段既不设也不读，属死状态，一旦有调用方误用即未定义行为。
- 最小修复：删除该字段，或显式清零并让 `lexer_next_token` 维护它。
- 备注：解析器维护的是 `parser->current_token`，与此无关。

##### [中] 62-25｜`token_free` 为空操作，token 所有权契约缺失
- 位置：`Tokenizer.c:865-870`；声明 `Tokenizer.h:227`
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：调用方按名字期望 `token_free` 释放 `token.value`（`ParserStatement.c` 多处调用）。
- 后果：函数体只有 `(void)token`，不做任何事；`value` 实际由 lexer 的 arena 拥有，`lexer_destroy` 后全部悬垂，但头文件未说明“token 不得晚于 lexer 存活”，调用方无法安全复制/释放。
- 最小修复：在头文件明确所有权（arena 拥有、lexer 销毁后失效），或让 `token_free` 真正释放非 arena 来源的 value。
- 备注：`lexer_peek_nth_token` 内部的 `token_free(&token)` 因此只是噪音。

##### [中] 62-26｜关键字哈希表无满表保护，容量与 `IntegerWidths.def` 强耦合
- 位置：`Tokenizer.c:138-156`（关键 151-153）、`14`
- 类别：8 未检查前置条件 / 11 配置漂移
- 触发条件：`keywords[]` 条目数达到 `KRT_KEYWORD_TABLE_SIZE`（512）——当前约 238 条，若 `IntegerWidths.def` 扩充或继续加关键字即触发。
- 后果：`init_keyword_table` 的 `while (keyword_table[index] != NULL) index = (index + 1) % 512;` 在满表时无限循环，且发生在 `pthread_once` 首次标识符处，进程挂死；表容量是硬编码常量，与 def 文件无编译期一致性校验。
- 最小修复：插入前统计关键字数并断言小于表大小，或改用链地址法/动态表；添加 `_Static_assert(sizeof(keywords)/sizeof(keywords[0]) < KRT_KEYWORD_TABLE_SIZE)`。
- 备注：`keywords[]` 无显式长度，依赖 NULL 哨兵。

##### [中] 62-27｜公共 API 未检查 NULL 前置条件
- 位置：`Tokenizer.c:528-555`（`lexer_create`）、`569-573`（`lexer_reset`）、`575`（`lexer_next_token`）、`829`（`peek`）、`843`（`peek_nth`）
- 类别：8 未检查前置条件
- 触发条件：`lexer_create(NULL)`，或对 NULL `Lexer*` 调用 `lexer_reset/lexer_next_token/lexer_peek_*`。
- 后果：`lexer_create(NULL)` 经 `KrtSafeStrdup` 返回 NULL（`Allocator.c:263-266`）后静默返回 NULL，与真实 OOM 不可区分；其余函数直接解引用 NULL 崩溃，无断言无诊断。
- 最小修复：入口统一 `if (!lexer) return ...;`，`lexer_create` 对 NULL source 明确报错或约定。
- 备注：`lexer_destroy` 已做 NULL 检查，风格不一致。

##### [低] 62-28｜前导小数点 `.5` 被拆成 `DOT` + `NUMBER`
- 位置：`Tokenizer.c:590-592`（数字分发）、`773-775`（`.` 分支）
- 类别：3 语义分叉
- 触发条件：源码 `x = .5;`。
- 后果：`.` 走 `TOKEN_DOT`，随后 `5` 走 `TOKEN_NUMBER`，解析器无法把它当浮点字面量；若语言允许前导小数点，则属功能缺失，且错误信息会指向表达式开头而非词法层。
- 最小修复：确认语言规范；若支持 `.5`，在 `lexer_next_token` 的 `.` 分支向前看数字并走 `lexer_read_number`。
- 备注：需语言规范确认，故列为低；`1..2` 之类也会被拆成 `1.`、`.`、`2`。

### 第六十三批：NameMangling.c + EnumParser.inc + PointerTypes.inc

#### 覆盖范围

- 全量读取（read 工具）：
  - `Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.c`（389 行）
  - `Re.KrtC/src/Compiler/Frontend/Parser/EnumParser.inc`（163 行）
  - `Re.KrtC/src/Compiler/Frontend/Parser/PointerTypes.inc`（152 行）
- 仅以 grep/定点读取核对以下符号，未通读其它源文件：
  - `KRT_MALLOC/KRT_REALLOC` → `KrtCommon.h:77,80`（失败返回 NULL，不 exit，G1 适用）。
  - `KrtParserAlloc/KrtParserStrdup/KrtParserCreateNode` → `ParserBase.h:9-35`（OOM 走 `KRT_COMPILE_ERROR` 致命错误，不返回 NULL）。
  - `IntegerWidths.def`（偶数宽度 2..128）、`KrtSourceType`（`Ast.h:131-141`，`pointee_nullable` 为 `uint64_t`）、`KrtUInt128 = unsigned __int128`（`Integer.h:8`）。
  - 调用方：`SemanticAnalyzer.c:1342-1347,1656-1661,2789`、`IrGen.c:1740,2044-2048,2834,569-575`、`CompilerPipeline.c:73,136`、`TypeExpansion.inc:368-370,403-413`。
  - 测试：`Test/SyntaxAudit/test_language_regressions.py:36-57`、`Test/SelfHost/test_symbol_index.py`、`Test/Pointers/*`（全库无任何 `mangl/demangl` 关键字测试）。
- 已应用前提 G1：所有 `KRT_MALLOC/KRT_REALLOC` 失败路径按“返回 NULL 且零诊断”的活路径审查；`KRT_STRCAT_S` 在本切片未出现，改核 `snprintf/memcpy/write_number` 的尺寸链。


##### [致命] 63-1｜name_demangle 长度字段无边界，memcpy 越界读堆
- 位置：`NameMangling.c:328-332`（长度累加）、`NameMangling.c:358`（`memcpy(name, ptr, (size_t)length)`）
- 类别：未检查前置条件（越界读）
- 触发条件：任何形如 `_ZN9999E` 的名字（`length` 远大于 `ptr` 之后剩余字符）。外部符号名、对象文件符号、用户传入的 mangled 名均可触发。
- 后果：`memcpy` 从只有数字节长的堆块读出 `length` 字节，造成堆越界读、崩溃或地址信息泄露；`length = length * 10 + (*ptr - '0')` 本身也可有符号整数溢出（UB）。
- 最小修复：解析期用 `size_t` 并做 `if (length == 0 || (size_t)length > strlen(ptr)) goto fail;`，通过后再 `memcpy`。
- 备注：`name_demangle` 目前无任何调用方（见 63-13），属“未接入的高危代码”。

##### [致命] 63-2｜编解码不对称：函数参数编码被追加在 `E` 终止符之后，demangle 遇到 `E` 即停
- 位置：`NameMangling.c:263-270`（把参数码写在 `base_mangled` 尾部，而 `base_mangled` 已含 `E`）、`NameMangling.c:326,364`（读侧 `while (*ptr && *ptr != 'E')` 后 `ptr++` 直接返回）
- 类别：语义分叉 / 契约破坏（编解码不对称）
- 触发条件：任何带参函数，如 `f(int32)` 生成 `_ZN...Ei`。
- 后果：`name_demangle(name_mangle_function(...))` 只能还原命名空间与函数名，参数类型被永久丢弃；解码结果无法用于重载判别或 ABI 校验。
- 最小修复：参数编码写入 `E` 之前（如 `_ZN...iiE`），并让 `name_demangle` 解析尾部参数段。
- 备注：本文件没有第二个解码入口，`name_demangle` 是唯一解析器。

##### [致命] 63-3｜同一文件并存三套互不兼容的 mangling 方案，且只有 `_Z` 方案有解码器
- 位置：`NameMangling.c:99-115`（`class__member`）、`NameMangling.c:117-135`（`class__constructor_N`）、`NameMangling.c:137-179`（`_ZN...E`）
- 类别：语义分叉（mangled 名构造非唯一入口）
- 触发条件：数据成员走 `name_mangle_simple`（`IrGen.c:690`、`SemanticAnalyzer.c:1472`），构造走 `name_mangle_constructor`，函数走 `name_mangle_function`。
- 后果：三类实体分属不同命名空间，无法互相解析；`name_demangle` 对前两套一律返回 0，调用方拿到 0 无法区分“不是 mangled 名”与“解码失败”。
- 最小修复：统一为单一 Scheme（推荐 `_ZN...E`），成员/构造改用 `name_mangle_names` 体系；过渡期至少让 `name_demangle` 返回不同错误码。
- 备注：`name_mangle_class_member`（本可用于统一的成员方案）已实现但零调用。

##### [致命] 63-4｜类型编码碰撞：`int8` 与 `char` 同为 `"c"`，`string` 与 `type_string` 同为 `"r"`
- 位置：`NameMangling.c:21-22`（`TOKEN_INT8 → "c"`）、`NameMangling.c:43-44`（`TOKEN_CHAR → "c"`）、`NameMangling.c:45-47`（`TOKEN_STRING/TOKEN_TYPE_STRING → "r"`）
- 类别：语义分叉（碰撞）
- 触发条件：同一作用域重载 `f(int8)` 与 `f(char)`，或 `f(string)` 与 `f(type_string)`。
- 后果：两个不同类型得到完全相同的 mangled 名，符号表合并、调用错绑；解码端也无法区分。
- 最小修复：为 `char`/`int8`/`string`/`type_string` 分配互不相同的码（如 `char→"h"`、`int8→"i8_"`）。
- 备注：与 63-9 的宽度宏编码冲突是同一处根因。

##### [致命] 63-5｜构造名与数据成员名碰撞：`A__constructor_2`
- 位置：`NameMangling.c:126,133`（`%s__constructor_%s`）、`NameMangling.c:113`（`%s__%s`）；调用方 `IrGen.c:1207,2961,3040` 与 `IrGen.c:690`
- 类别：语义分叉（碰撞）
- 触发条件：类 `A` 同时声明成员 `constructor_2` 与 2 参构造函数 `A(int32,int32)`。
- 后果：数据成员与构造函数符号完全相同，链接期合并或互相覆盖，行为未定义。
- 最小修复：构造名使用不可能出现在标识符中的分隔（如 `_ZN...E` 体系或 `@ctor`），或在拼接前转义类/成员名中的下划线。
- 备注：`name_mangle_simple` 与 `name_mangle_constructor` 共用 `__` 分隔是碰撞根源。

##### [高] 63-6｜`name_mangle_simple` 对含 `__` 的名字不唯一
- 位置：`NameMangling.c:106,113`
- 类别：语义分叉（碰撞）
- 触发条件：`name_mangle_simple("a__b","c")` 与 `name_mangle_simple("a","b__c")` 均得 `a__b__c`。
- 后果：不同类/成员映射到同一符号，静态字段访问可能读到错误对象。
- 最小修复：改用长度前缀（`strlen:class:member`）或转义名字中的下划线。
- 备注：与 63-5 同属 `__` 分隔方案缺陷，但触发面更广（任何带双下划线的名字）。

##### [高] 63-7｜函数 mangling 丢失指针层级、可空性与 ref 修饰
- 位置：`NameMangling.c:247-250,264-269`（只对 `param_types[i]` 取 `KrtMangleTypeName`）；调用方 `IrGen.c:1740`（`KrtSourceStorage` 把任何指针/函数压成 `TOKEN_UINT64`）、`SemanticAnalyzer.c:1342-1343`（只推断基础 token）
- 类别：语义分叉 / 契约破坏
- 触发条件：重载 `f(int32*)`、`f(int32**)`、`f(uint64)`，或 `f(int32*)` 与 `f(int32*?)`。
- 后果：`f(int32*)` 与 `f(int32**)` 与 `f(uint64)` 同为 `...L`；`f(int32*)` 与 `f(int32)` 虽可分，但可空性完全丢失，重载解析歧义或错绑。
- 最小修复：参数编码加入 `pointer_depth`、`pointee_nullable`、`nullable`、`is_ref`（如 `L` 前加层级/可空标记）。
- 备注：`KrtSourceType` 已携带这些字段，mangle 侧主动丢弃。

##### [高] 63-8｜`f(void)` 与 `f()` 碰撞为同一个 `...Ev`
- 位置：`NameMangling.c:48-49`（`TOKEN_VOID → "v"`）、`NameMangling.c:232-245`（无参时追加 `"v"`）；`ParserBase.c:135`（`TOKEN_VOID` 是合法类型关键字）
- 类别：语义分叉（碰撞）
- 触发条件：声明 `f(void)` 与 `f()` 两个重载（C 风格空参列表）。
- 后果：两者 mangled 名相同，符号冲突。
- 最小修复：无参与 `void` 参数使用不同编码（如无参不追加、`void` 参数编码为 `"V"`），或在语法层禁止 `f(void)`。
- 备注：与 63-2 叠加时，`void` 参数信息在解码端同样丢失。

##### [高] 63-9｜未知类型静默编码为 `"x"`；前置 switch 遮蔽宽度宏导致 6 个 case 死代码
- 位置：`NameMangling.c:50-63`（`default: return "x"`）、`NameMangling.c:21-44`（先于宏捕获 `INT8/16/32/64`、`UINT8/16/32/64`）、`NameMangling.c:53-60`（宏生成的 `TOKEN_INT8` 等分支不可达）
- 类别：静默失败 + 死代码 + 语义分叉
- 触发条件：传入未支持/非法的 `KrtTokenType`（如 `TOKEN_EOF`、`0`）时静默返回 `"x"`；`int8` 实际编码 `"c"` 而 `int2` 编码 `"i2_"`。
- 后果：非法类型不报错而生成合法外观的 mangled 名，多个未知类型互相碰撞；`i8_/i16_/i32_/i64_/u8_/...` 分支永不执行，编码体系自相矛盾。
- 最小修复：删除前置 switch 中的 `INT8..UINT64` case（让宽度宏统一编码），并让未知类型返回 `NULL`/触发诊断而非 `"x"`。
- 备注：`IntegerWidths.def` 定义 2..128 偶数宽度，遮蔽只影响 8/16/32/64。

##### [高] 63-10｜name_demangle 对畸形输入静默成功
- 位置：`NameMangling.c:322-326`（无 `E` 也继续）、`NameMangling.c:364-368`（`E` 后尾随垃圾不校验）、`NameMangling.c:316-320,322`（`_ZN` 零组件也返回 1）
- 类别：静默失败 / 契约破坏
- 触发条件：`_ZN3abc`（缺 `E`）、`_ZN3abcEii`（尾部垃圾）、`_ZNE`（空组件表）。
- 后果：调用方无法区分合法解码与“勉强解析”，错误名字被当作有效符号；与 63-2 叠加使参数垃圾被静默接受。
- 最小修复：要求存在 `E`、`E` 后必须到串尾、至少解析出一个组件，否则返回 0。
- 备注：返回值只有 0/1，无错误原因。

##### [高] 63-11｜name_demangle 的 `KRT_REALLOC` 失败路径泄漏旧数组与已分配名字
- 位置：`NameMangling.c:342-348`（`*out_names = KRT_REALLOC(...)` 直接覆盖）
- 类别：资源与生命周期（G1 活路径）
- 触发条件：组件数超过初始容量 8 且 realloc 失败（`KRT_REALLOC` 失败返回 NULL，原块仍有效但指针被覆盖）。
- 后果：旧 `out_names` 数组与其中已 `KRT_MALLOC` 的每个名字全部泄漏；`*out_names` 被置 NULL，部分状态被掩盖。
- 最小修复：`char** tmp = KRT_REALLOC(*out_names, ...); if (!tmp) { name_demangle_free(*out_names, *out_count); *out_names=NULL; *out_count=0; return 0; } *out_names = tmp;`
- 备注：同函数内 `name` 分配失败路径（350-356）清理是正确的，可作对照。

##### [高] 63-12｜name_mangle_from_ast 的 `KRT_REALLOC` 失败泄漏旧数组且无任何诊断
- 位置：`NameMangling.c:201-207`
- 类别：资源与生命周期 / 静默失败
- 触发条件：命名空间嵌套深度超过初始容量 8 且 realloc 失败。
- 后果：旧 `namespaces` 数组泄漏；函数返回 NULL 而无诊断，调用方（若接入）只能静默失败。
- 最小修复：临时变量接收 realloc，失败时 `KRT_FREE` 旧数组并返回 NULL 且由调用方报错。
- 备注：该函数当前零调用（见 63-13），属未接入的泄漏路径。

##### [高] 63-13｜整组 API 为死代码且解码器零测试覆盖
- 位置：`NameMangling.c:181-222`（`name_mangle_from_ast`）、`NameMangling.c:275-301`（`name_mangle_class_member`）、`NameMangling.c:303-382`（`name_demangle/name_demangle_free`）
- 类别：死代码 / 测试不足
- 触发条件：全库 grep 仅命中定义与头文件声明，无任何调用方；`Test/` 下无 `mangl/demangl` 关键字。
- 后果：63-1 的越界读、63-10 的静默成功、63-11 的泄漏长期无人触发也无人发现；`name_mangle_class_member` 本可统一成员方案却闲置。
- 最小修复：要么接入（符号索引/诊断输出）并补单元测试，要么删除；至少为 `name_demangle` 增加畸形输入与 OOM 测试。
- 备注：`name_mangling_init/cleanup` 虽被 `CompilerPipeline.c:73,136` 调用，但函数体为空（见 63-16）。

##### [高] 63-14｜`size_t`→`int` 截断贯穿长度计算与写数字
- 位置：`NameMangling.c:7-17`（`digit_count(int)`）、`NameMangling.c:66-84`（`write_number` 无负数/上限保护）、`NameMangling.c:93-94,145,163,171`
- 类别：未检查前置条件（截断/溢出）
- 触发条件：命名空间或名字长度超过 `INT_MAX`（或 `strlen` 结果被强转 `int` 后为负）。
- 后果：`digit_count` 返回 0、`write_number` 只写一个 `'\0'` 并返回 0，mangled 名中间被插入 NUL、长度字段与实际内容不符，产生错误碰撞或解析错乱；分配尺寸也随之算错。
- 最小修复：`digit_count/write_number` 内部统一使用 `size_t`，删除 `(int)` 强转。
- 备注：常规源码不会触发，但属于“分配尺寸链”上的未检查前置条件（G1 关注点）。

##### [高] 63-15｜阶段不对齐：调用点用推断类型、定义端用声明类型构造同一函数的 mangled 名
- 位置：`NameMangling.c:224-273`（契约未要求类型规范化）；调用方 `SemanticAnalyzer.c:1342-1347,1656-1661`（`semantic_analyzer_infer_expression_type` 的结果）对比 `IrGen.c:2834`（`stmt->data.function_decl.parameter_types[i]`）、`IrGen.c:2044-2048`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：实参推断类型与形参声明类型不一致（如字面量推断 INT32 而形参为 INT64，或指针/枚举载体差异）。
- 后果：调用点生成的 mangled 名与定义端不同，符号查找失败或绑定到错误重载。
- 最小修复：`name_mangle_function` 只接受声明期规范化后的类型数组；调用点改用被调函数的声明参数类型，而非表达式推断类型。
- 备注：`name_mangle_function` 对入参无任何校验，无法自行发现该不一致。

##### [中] 63-16｜`name_mangling_init/cleanup` 为空壳却接入流水线
- 位置：`NameMangling.c:384-389`；调用方 `CompilerPipeline.c:73,136`
- 类别：死代码 / 契约
- 触发条件：每次编译。
- 后果：生命周期钩子无状态、无资源，误导调用方以为存在需初始化的全局状态；与 63-13 组合说明该模块从未真正接入。
- 最小修复：删除钩子，或填充实际状态并让 `name_mangling_init` 返回有意义的结果。
- 备注：`name_mangling_init` 恒返回 1，错误不可表达。

##### [高] 63-17｜负枚举值存入 `unsigned __int128`，后续按无符号推断为 UINT128
- 位置：`EnumParser.inc:132`（`value->integer_value = (KrtUInt128)next;`，`next` 为 int64 负值）；消费方 `TypeExpansion.inc:411`、`IrGen.c:569-575`
- 类别：语义分叉 / 未检查前置条件
- 触发条件：`enum E { A = -1 };` 之后使用 `E.A`（如作为实参参与重载/mangling）。
- 后果：`-1` 变为 `2^128-1`，`krt_ir_infer_mangle_type` 走 `> INT64_MAX` 分支返回 `TOKEN_UINT128`，而枚举底层类型应为 INT32；同一值在 `enum_constant`（int64 `-1`）与 AST（uint128 全 1）中两种表示。
- 最小修复：枚举值统一存 `int64_t`，或存时用 `KrtIntegerNormalize` 按 INT32 有符号语义规范化，并在读取处显式转回 int32。
- 备注：`Test/SyntaxAudit/test_language_regressions.py:38,43` 覆盖负值，但只断言截断后的返回值，未覆盖类型标注，故未暴露。

##### [高] 63-18｜枚举底层类型在三处硬编码 `TOKEN_INT32`，且解析器不接受底层类型说明符
- 位置：`EnumParser.inc:113`（`item->...type = TOKEN_INT32`）、`EnumParser.inc:134`（`value->declared_type.token`）、`EnumParser.inc:97`（名字后直接要求 `{`，`:` 说明符一律 invalid）；消费方 `TypeExpansion.inc:368-370`
- 类别：契约破坏 / 配置漂移
- 触发条件：源码写 `enum E : int64 { ... }` 或需要 64 位枚举值的程序。
- 后果：说明符被静默判为语法错误（错误消息归为通用 Invalid enum）；即使接受也会被强制按 INT32 存储，值域与 ABI 与声明不符。
- 最小修复：要么在语法层显式拒绝并给出专门诊断，要么把底层类型贯穿 `KrtSourceType`/`value->declared_type`，消除三处硬编码。
- 备注：`IntegerWidths.def` 已支持到 128 位，底层类型固化属于设计缺口。

##### [高] 63-19｜枚举错误诊断位置与原因错位：全部报在 `enum` 关键字行列且共用一条消息
- 位置：`EnumParser.inc:88`（捕获 `line/col`）、`EnumParser.inc:159-162`（唯一 `invalid:` 出口）
- 类别：诊断缺失
- 触发条件：缺名字、缺 `{`、成员重名、非常值表达式、溢出、缺 `}`、EOF 未闭合等 6 类以上失败。
- 后果：所有错误都指向 `enum` 关键字而非实际出错 token，且消息无法区分重名与溢出；`test_language_regressions.py:48-52` 只能断言通用 `Invalid enum`。
- 最小修复：在各失败点用 `parser->current_token.line/column` 报专门消息（重名/溢出/非恒值表达式分开）。
- 备注：`goto invalid` 结构使位置信息在跳转前已丢失。

##### [中] 63-20｜`enum_constant` 递归无深度上限
- 位置：`EnumParser.inc:20`（一元递归）、`EnumParser.inc:32`（二元递归）
- 类别：未检查前置条件（栈溢出）
- 触发条件：深度嵌套的枚举常量表达式（如数万个括号/运算符），解析器表达式阶段未限制深度时。
- 后果：C 栈溢出导致编译器崩溃，且无诊断。
- 最小修复：引入深度计数器，超过阈值即报 “expression too deeply nested”。
- 备注：与 `KrtParseNamedType` 的 `depth > 32` 限制形成对照，后者有防护。

##### [中] 63-21｜`enum_constant` 对数字字面量只判上界，`INT64_MIN` 字面量不可表达
- 位置：`EnumParser.inc:7-9`（`expr->integer_value <= INT64_MAX`）
- 类别：未检查前置条件 / 语义限制
- 触发条件：`-9223372036854775808`：操作数 `9223372036854775808 > INT64_MAX` 使内层字面量判负，外层一元减法拿不到值。
- 后果：合法整数常量表达式被误判为 Invalid enum。
- 最小修复：显式处理 `INT64_MIN`（或允许操作数上界到 `INT64_MAX+1` 并在取负后校验）。
- 备注：枚举最终值域是 int32，但中间表达式用 int64，边界处理不对称。

##### [中] 63-22｜标识符引用分支把 `KrtUInt128` 强转 `int32_t`，依赖实现定义截断
- 位置：`EnumParser.inc:15`（`(int32_t)item->data.static_variable_decl.value->integer_value`）
- 类别：未检查前置条件 / 可移植性
- 触发条件：引用先前列出的枚举成员（尤其是 63-17 的负值成员）。
- 后果：截断是实现定义行为；对负值成员靠回绕恰好得到正确 int32，但无任何范围校验，成员值若超出 int32（其它路径混入时）会静默回绕。
- 最小修复：改为从 `enum_constant` 的 int64 结果取值，或显式校验 `>= INT32_MIN && <= INT32_MAX` 后再转。
- 备注：与 63-17 是同一表示分叉的读取侧。

##### [高] 63-23｜`TOKEN_MUL_ASSIGN` 在类型解析中被当作指针星并原地改写为 `TOKEN_ASSIGN`
- 位置：`PointerTypes.inc:124-125`（把 `*=` 计入指针）、`PointerTypes.inc:135-138`（`parser->current_token.type = TOKEN_ASSIGN;`）
- 类别：共享可变状态 / 契约破坏
- 触发条件：任何在类型位置出现的 `*=`（如 `T* = v` 被 lexer 贪心成 `T` `*=`）。
- 后果：解析器直接篡改共享 token 流，`*=` 永久丢失；错误恢复、回溯或调用方再次查看该 token 时看到的是 `=`，行为依赖调用时序。
- 最小修复：让 lexer 把 `*` 与 `=` 分开产出，或解析器在栈上复制 token 而不写回 `parser->current_token`。
- 备注：该分支还 `break` 出指针循环，语义与 `*=` 作为复合赋值的本意无关，注释未说明。

##### [高] 63-24｜连续 `?` 被静默丢弃：`T??` 的第二个 `?` 直接终止解析
- 位置：`PointerTypes.inc:140-145`（`else if (... && !type->nullable)`，条件不成立时落入 `else break`）
- 类别：静默失败 / 诊断缺失
- 触发条件：`int32??`、`int32*??` 等重复可空标记。
- 后果：多余的 `?` 未报错也未消费，留在 token 流中，可能被后续解析成三元/可空访问，静默改变结构或产生级联错误。
- 最小修复：`?` 已置位时再遇 `?` 显式报 “duplicate nullable marker”。
- 备注：`type->nullable` 在 `*` 后清零（133 行），使 `T?*?*?` 只有部分可空性被记录。

##### [高] 63-25｜`KrtParseNamedType` 对泛型内部 token 不做任何校验
- 位置：`PointerTypes.inc:6-49`（尤其 `8-17` 无条件追加 `token->value`、`31-47` 只判 DOT/LESS）
- 类别：静默失败 / 未检查前置条件
- 触发条件：`Foo<123>`、`Foo<"x">`、`Foo<+>`、`Foo<Bar<Baz>>` 之外的畸形拼写。
- 后果：数字、字符串、运算符被原样拼进类型名并返回“成功”，下游按不存在的类型名处理，错误被推迟且位置丢失。
- 最小修复：在循环内限定可出现的 token 集合（标识符、类型关键字、逗号、`<`/`>`/`>>`、`.`），其余报专门诊断。
- 备注：`depth > 32` 只限制嵌套层数，不限制内容。

##### [中] 63-26｜`KrtParseNamedType` 遗漏 `<=`/`>=`/`>>=`/`<<` 的 depth 处理
- 位置：`PointerTypes.inc:18-26`（只处理 `TOKEN_LESS/TOKEN_GREATER/TOKEN_RSHIFT`）
- 类别：静默失败
- 触发条件：`Foo<=Bar`、`Foo>>=Bar`、`Foo<<Bar` 等拼写。
- 后果：depth 不更新，`<=` 被当作普通字符拼接后可能在 depth==0 提前 break，生成错误类型名而不报错。
- 最小修复：显式拒绝或正确更新 depth（`>>=` 视为两个 `>`）。
- 备注：`TOKEN_RSHIFT` 已处理，`TOKEN_RSHIFT_ASSIGN` 未处理，属同类遗漏。

##### [中] 63-27｜`ref` 以字符串比较识别：占用用户类型名且 `value==NULL` 时崩溃
- 位置：`PointerTypes.inc:62`（`parser->current_token.type == TOKEN_IDENTIFIER && !strcmp(parser->current_token.value, "ref")`）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：用户定义类型恰好叫 `ref`；或标识符 token 的 `value` 为 NULL（lexer 异常路径）。
- 后果：名为 `ref` 的类型被永久当作修饰符，无法声明/引用；`value` 为 NULL 时 `strcmp` 直接崩溃。
- 最小修复：把 `ref` 提升为 token 关键字，并在比较前判 `value`。
- 备注：`KrtParserStrdup` 对 NULL 输入返回 NULL，说明 NULL value 在接口上是被允许的。

##### [中] 63-28｜空参数列表仍分配并保留非 NULL `parameters`，与“无参数”不可区分
- 位置：`PointerTypes.inc:79-81`（先分配 capacity 8）、`PointerTypes.inc:99-101`（空列表直接跳出，`parameters` 非 NULL、`parameter_count==0`）
- 类别：契约破坏
- 触发条件：`fn()` 类型。
- 后果：下游若用 `parameters != NULL` 判断“有参数”会误判；`fn()` 与 `fn` 参数缺失的语义无法区分。
- 最小修复：空列表时把 `fn->parameters` 置 NULL，统一以 `parameter_count` 为唯一判据。
- 备注：`IrGen.c:2042` 已按 `parameter_count` 分配，说明判据应统一。

##### [中] 63-29｜`fn` 分支 OOM 返回 false 但不产生诊断，与其它路径不一致
- 位置：`PointerTypes.inc:68-71`（`if (!fn) return false;`）
- 类别：错误处理不一致
- 触发条件：`KrtParserAlloc` 失败时（当前实现走 `KRT_COMPILE_ERROR`，故实际不可达，但契约上该返回值存在）。
- 后果：若 arena 分配改为返回 NULL，该路径静默失败且不报错误位置；与 `invalid:` 出口（148-151）行为不一致。
- 最小修复：统一走 `invalid:` 或在使用 `KrtParserAlloc` 后直接依赖其致命语义并删除该分支。
- 备注：与 63-30 同属防御性代码与实际契约脱节。

##### [中] 63-30｜`pointee_nullable` 位移语义无注释，`<< (levels - 1)` 对 `TOKEN_POWER` 的复合语义易错
- 位置：`PointerTypes.inc:126-134`
- 类别：注释与实现脱节 / 语义脆弱
- 触发条件：阅读或修改指针可空性编码时；`TOKEN_POWER`（`**`）一次性加两层时。
- 后果：位序（bit0 对应最内层还是最外层 pointee）无注释，`((pn << 1) | nullable) << (levels - 1)` 在 `levels==2` 且已有深度时的位移方向依赖调用顺序；`PointerAnalysis.inc:121-124` 与 `TypeExpansion.inc:213-226` 各自复制了同一公式，一旦此处改正而其它处未同步就会产生新的分叉。
- 最小修复：在 `Ast.h:135` 注释中明确位序定义，并把该公式抽成单一 helper 供三处共用。
- 备注：`Ast.h:135` 现有注释只说明 “Bit 0 describes the immediately pointed-to slot”，未说明多层顺序。

### 第六十四批：ParserStatement.c（全量）

#### 覆盖范围

- 文件：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c`，全量 1674 行，已用 read 工具分段（1-200、201-420、421-640、641-870、871-1100、1101-1400、1401-1674）读完。
- 交叉核对：主会话已验证的 :540-580（赋值目标处理）已纳入本轮全量审查；另按契约需要 grep/读了 `ParserBase.h`、`Parser.h`、`Ast.c`、`Tokenizer.h/.c`、`KrtCommon.h`、`Allocator.c`、`Arena.c`、`ParserExpression.c` 的少量相关行。
- 前提 G1 核对结论（重要）：`KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC` 经 `KrtSafeMalloc/Realloc/Strdup` 失败返回 NULL（活路径）；但 `KrtParserAlloc`/`KrtParserCreateNode`/`KrtParserStrdup` 失败走 `KRT_COMPILE_ERROR` → `exit(1)`（`KrtCommon.h:60-65`、`ParserBase.h:9-35`），**不返回 NULL**。因此 G1 的 NULL 活路径只适用于 KRT_* 堆函数；`KRT_PARSER_*` 后的 NULL 守卫属不可达死代码（见 64-26）。
- 所有权模型：parser 节点由 `KrtParserCreateNode` 建在 `parser->arena`（`is_arena_allocated=1`），`ast_destroy_node` 对 arena 节点直接 return（`Ast.c:77`），其清理分支在默认 arena 模式下不可达；但本文件大量子数组/名字用 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC` 堆分配（见 64-25）。


##### [致命] 64-1｜数组赋值对 arena AST 节点调用 KRT_FREE（非法释放）
- 位置：`ParserStatement.c:537`
- 类别：契约破坏 / 资源与生命周期
- 触发条件：任何数组赋值 `arr[i] = v` 或 `arr[i] += v`。`left` 是 `AST_ARRAY_ACCESS`，由 `ParserExpression.c:814` 的 `KrtParserCreateNode` 建在 parser arena。
- 后果：`KRT_FREE(left)` → `KrtSafeFree` 对未登记指针 `find_block` 失败 → `KrtMemoryReportError("INVALID_FREE")`；release 构建静默、`KRT_DEBUG` 构建 `__debugbreak`。同时该容器本应由 arena 回收，释放动作多余且错误。
- 最小修复：删除 `KRT_FREE(left)`（子节点已转移给新节点，容器交给 arena）；如需显式回收，改用不接管所有权的处理。
- 备注：同函数 511-517 指针解引用分支不释放 `left` 反而是正确写法；537 是全文件唯一对 AST 节点调用 `KRT_FREE` 的位置。

##### [致命] 64-2｜print 参数数组 arena 分配后用 KRT_REALLOC/KRT_FREE（分配器混用）
- 位置：`ParserStatement.c:612`、`618`、`640`、`650`、`660`
- 类别：契约破坏 / 资源与生命周期 / 静默失败
- 触发条件：`print` 参数超过 8 个（初始容量 `KRT_PARSER_ARGUMENT_CAPACITY_INIT=8`）触发 618 扩容；或 `print` 语法错误走 640/650/660 清理。
- 后果：`values` 来自 `KRT_PARSER_ALLOC_FROM_ARENA`（arena）。`KRT_REALLOC` 对 arena 指针执行 `KrtPtrGetBlock` 失败 → `INVALID_REALLOC` 且返回 NULL，`print` 静默失败（无 `parser_report_error`，`error_count` 不增）；`KRT_FREE(values)` 同样触发 `INVALID_FREE`。即超过 8 个参数的 `print` 永远失败。
- 最小修复：起始分配改用 `KRT_MALLOC` 并全程用 `KRT_REALLOC`/`KRT_FREE`；或像 `parser_parse_function_declaration` 那样用 `KrtParserAlloc`+`memcpy` 手工扩容且不 `KRT_FREE`。
- 备注：619 是唯一一处 NULL 检查，但仍未报诊断；640/650/660 三处清理路径都含非法 `KRT_FREE`。

##### [致命] 64-3｜switch 子句数组/语句数组 KRT_REALLOC 未检查
- 位置：`ParserStatement.c:821`、`839`、`864`
- 类别：未检查前置条件（NULL）/ 静默失败
- 触发条件：单个 case 内语句数超过 4（`scap` 初始 4）触发 821/864；case 数超过 4（`cap` 初始 4）触发 839。G1：`KRT_REALLOC` 失败返回 NULL。
- 后果：821/864 之后 `stmts[sc++] = st` 对 NULL 解引用；839 之后 `cases[case_count++] = clause` 对 NULL 解引用 → 段错误；同时旧指针丢失造成泄漏。
- 最小修复：用局部变量接收返回值，失败时保留旧指针、走 `cleanup` 并 `parser_report_error`。
- 备注：826 的 `clause` 创建失败有清理，但 839 的 `cases` 扩容在其后且无检查，顺序放大了风险。

##### [致命] 64-4｜using 指令 path_parts 分配/追加/扩容未检查
- 位置：`ParserStatement.c:1462`、`1468`、`1476`、`1478`、`1483`、`1486`
- 类别：未检查前置条件（NULL）
- 触发条件：`KRT_MALLOC(16*sizeof(char*))` 失败 → 1468 写入 NULL；`KRT_STRDUP` 失败 → `path_parts` 中存 NULL；`KRT_REALLOC` 失败 → 1478/1488 写入 NULL。
- 后果：NULL 解引用崩溃；或 `namespace_path` 含 NULL 项，语义/克隆（`AstClone.inc:362`）阶段解引用崩溃。
- 最小修复：每处检查 NULL；失败时释放已建 `path_parts` 并 `parser_report_error`。
- 备注：1468 首次写入前也无容量检查（容量 16 尚够）；1491-1495 已有一处正确的清理样板可复用。

##### [致命] 64-5｜block 语句数组 KRT_MALLOC 未检查
- 位置：`ParserStatement.c:1197`
- 类别：未检查前置条件（NULL）
- 触发条件：`KRT_MALLOC(sizeof(ASTNode*) * 16)` 失败（G1）。
- 后果：1222 `statements[count++] = stmt` 对 NULL 解引用 → 崩溃。
- 最小修复：检查 NULL，返回 NULL 并（或）报 `parser_report_error`。
- 备注：同文件 680 的 `KRT_PARSER_MALLOC` 有检查，此处遗漏；1212 的 realloc 另有问题（见 64-6）。

##### [高] 64-6｜block 语句数组 KRT_REALLOC 失败丢指针且无诊断
- 位置：`ParserStatement.c:1212-1215`
- 类别：资源与生命周期 / 静默失败
- 触发条件：块内语句数超过 16 触发扩容且 `KRT_REALLOC` 失败。
- 后果：`statements` 被置 NULL，旧数组泄漏；直接 `return NULL`，无 `parser_report_error`，`error_count` 不增，上层误判为无错误。
- 最小修复：局部变量接收 realloc，失败时保留旧指针、清理已解析语句并报错。
- 备注：与 618 同款模式，但此处是纯 heap 数组，主要后果是泄漏 + 静默失败。

##### [致命] 64-7｜标识符赋值 KRT_STRDUP 未检查
- 位置：`ParserStatement.c:549`、`552`
- 类别：未检查前置条件（NULL）/ 静默失败
- 触发条件：`KRT_STRDUP(left->data.identifier_name)` 失败（G1）。
- 后果：`node->data.assignment.name` / `compound_assignment.name` 为 NULL；随后 `ast_destroy_node(left)`（arena 节点为 no-op），无诊断、`error_count` 不增；产出名字为空的赋值节点，语义/代码生成阶段可能崩溃或生成错误符号。
- 最小修复：检查 NULL，失败时释放 `value`/`left` 并报 `parser_report_error`。
- 备注：:540-580 段主会话已交叉验证赋值目标处理，但此分配失败分支仍未覆盖；549/552 是 G1 活路径。

##### [高] 64-8｜成员赋值被编码为 AST_BINARY_OPERATION，复合成员赋值直接报错
- 位置：`ParserStatement.c:558-571`、`573`
- 类别：语义分叉
- 触发条件：`a.b = c;` 与 `a.b += c;`。
- 后果：`=` 生成 `AST_BINARY_OPERATION(operator=TOKEN_ASSIGN)`，与标识符/数组赋值的 `AST_ASSIGNMENT`/`AST_ARRAY_ASSIGNMENT` 契约不一致，下游按 `node->type` 分派的阶段无法把它识别为赋值；`+=` 等复合赋值落到 573 `"Unsupported assignment target"`，同一语法结构在不同目标上能力不一致。
- 最小修复：新增 `AST_MEMBER_ASSIGNMENT`/`AST_MEMBER_COMPOUND_ASSIGNMENT`（全仓 grep 无此类节点，`Ast.c` 也无对应清理分支），或在统一 assignment 节点中表达成员目标。
- 备注：这是 540-580 段最实质的语义分叉点，建议优先修。

##### [高] 64-9｜return 语句表达式失败静默、分号不强制
- 位置：`ParserStatement.c:585-598`
- 类别：诊断缺失 / 静默失败
- 触发条件：`return <非法表达式>`；`return 1`（缺 `;`）；`return 1 2;`。
- 后果：`parser_parse_expression` 返回 NULL 时 `value` 保持 NULL 仍创建 `AST_RETURN_STATEMENT`，无 `parser_report_error`；随后既不检查也不消费 `TOKEN_SEMICOLON`，缺分号被静默接受，剩余 token 留给上层。
- 最小修复：`value==NULL` 时报错返回 NULL；解析后要求并消费 `;`。
- 备注：与 if/while 的错误路径相比，本函数既无表达式诊断也无分号诊断。

##### [高] 64-10｜print 语句全流程无诊断
- 位置：`ParserStatement.c:606-608`、`615-636`、`646-652`
- 类别：诊断缺失 / 静默失败
- 触发条件：缺 `(`、表达式解析失败、缺 `)`、缺 `;`。
- 后果：一律 `return NULL`，从不调用 `parser_report_error`，`error_count` 不增；调用方只能看到空指针，错误位置与原因丢失。
- 最小修复：每个失败分支补 `parser_report_error`（含 line/col）。
- 备注：636 失败分支还会对 `values` 中可能为 NULL 的项调用 `ast_destroy_node`（该函数 NULL 安全，但无意义）。

##### [高] 64-11｜block 错误恢复不报告诊断、强制跳 token、静默放弃
- 位置：`ParserStatement.c:1200-1243`、`1229`
- 类别：诊断缺失 / 静默丢弃 token / 注释与实现脱节
- 触发条件：块内语句解析失败（尤其未前进 token 的失败）。
- 后果：`consecutive_errors` 只计数不报告；超过 10 时直接清理并 `return NULL`，最多 10 个错误全部丢失；1229 注释自认「强制跳过,后续解决一下」，静默吞 token。
- 最小修复：每次失败调用 `parser_report_error`；删除 TODO 式强制跳过，改为报错后跳过。
- 备注：1228 的 EOF/RIGHT_BRACE 保护只防越界，不产生诊断。

##### [高] 64-12｜switch 多条错误路径静默
- 位置：`ParserStatement.c:796-799`、`812-817`、`879-881`
- 类别：诊断缺失 / 静默丢弃 token
- 触发条件：case 表达式失败；case 体内语句失败；switch 体出现非 case/default 的杂散 token。
- 后果：均无 `parser_report_error`；杂散 token 在 880 被直接 `parser_advance` 吞掉；`error_count` 不增，解析可能「成功」产出残缺 switch。
- 最小修复：补诊断；杂散 token 报错后再跳过。
- 备注：884 的缺 `}` 有诊断，同一函数内策略不一致。

##### [高] 64-13｜重复 default 静默覆盖
- 位置：`ParserStatement.c:868-878`
- 类别：语义分叉 / 资源与生命周期
- 触发条件：switch 中出现两个 `default:`。
- 后果：878 `default_case = blk;` 直接覆盖前一个 `AST_BLOCK`，第一个 default 子句被静默丢弃且无诊断（arena 节点不泄漏，但语义丢失）。
- 最小修复：检测 `default_case != NULL` 时报 `"duplicate default"` 并清理 `blk`。
- 备注：与 884 缺 `}` 的诊断不一致。

##### [高] 64-14｜函数声明多条错误路径无诊断
- 位置：`ParserStatement.c:35-37`、`42-44`、`83-85`、`91-93`、`98-100`、`115-117`、`121-125`、`127-129`
- 类别：诊断缺失
- 触发条件：缺函数名、缺 `(`、参数类型/数组/名字错误、缺 `)`、缺函数体、体解析失败。
- 后果：全部直接 `return NULL`，无 `parser_report_error`，`error_count` 不增，用户看不到具体语法错误。
- 最小修复：每个分支补诊断（含函数名/行号）。
- 备注：参数数组均为 `KrtParserAlloc`（arena，失败即 exit），提前返回不构成堆泄漏，但诊断缺失是实在问题。

##### [中] 64-15｜parameter_is_params 对所有参数恒为 1 且无人消费
- 位置：`ParserStatement.c:104`、`140`
- 类别：死代码 / 契约破坏
- 触发条件：任何函数声明。
- 后果：`param_is_params` 全为 1，无法区分 params/非 params；全仓 grep 显示该字段只在 `Ast.h`、`AstClone.inc`、`Ast.c:free_function_decl_data` 出现，无任何语义读取，写入值无意义。
- 最小修复：按 `TOKEN_PARAMS` 解析并设置，或删除该字段及 140 行。
- 备注：`ParserBase.h:44` 声明的 `parser_parse_parameter_list` 才是带语义的版本，但本文件未使用。

##### [中] 64-16｜函数参数表语法过宽（尾逗号/缺逗号静默接受）
- 位置：`ParserStatement.c:110-113`
- 类别：诊断缺失 / 语法过宽
- 触发条件：`f(int a,)` 或 `f(int a int b)`。
- 后果：尾逗号被静默接受；缺逗号时把下一个类型当作新参数，无诊断。
- 最小修复：逗号后遇 `)` 报错；参数末尾要求逗号或 `)`。
- 备注：与 115 缺 `)` 的严格性不一致。

##### [高] 64-17｜foreach 多条错误路径无诊断且类型识别与 for 不一致
- 位置：`ParserStatement.c:1139-1151`、`1158-1176`、`1144-1147`
- 类别：诊断缺失 / 语义分叉
- 触发条件：缺 `(`、非标识符、缺 `in`、iterable 失败、缺 `)`、体失败；`foreach (let x in ...)` 或 `foreach (var x in ...)`。
- 后果：均静默 `return NULL`；`let`/`var` 不被 1145 的类型识别接受（for 的 `looks_decl` 也不含 let/var，但 for 可经 `parser_parse_statement` 兜底，foreach 直接失败）；1145 的 `t >= TOKEN_UINT2 && t <= TOKEN_UINT128` 是 `INT2..VOID` 子集，属冗余死条件。
- 最小修复：补诊断；类型识别复用统一 helper；支持 let/var 或明确报错。
- 备注：1152 的 `KrtParserStrdup` 为 arena 分配，失败即 exit，不会返回 NULL；1178 的 `if (!node)` 不可达。

##### [高] 64-18｜for 语句：死代码分号检查、多条静默错误、体必须为块
- 位置：`ParserStatement.c:1033-1035`、`1047-1055`、`1085-1096`、`1099-1111`
- 类别：死代码 / 诊断缺失 / 阶段不对齐
- 触发条件：正常 for；缺第二/第三分界符；体为单语句。
- 后果：1033 的 `if (current==SEMICOLON) advance` 在两条 init 路径（`parser_parse_variable_declaration_with_type` 与表达式语句）都已消费分号后恒为假，属死代码；1047/1085 缺分号/`)` 静默返回 NULL；1099 只接受块，而 while/if/do/foreach 接受块或单语句，`for (...) stmt;` 静默失败。
- 最小修复：删除死分支；补诊断；体改用 `parser_parse_block_or_statement` 或明确报错。
- 备注：1099 与 669/918/990/1173 的 `block_or_statement` 用法不一致，是语法不统一的根源。

##### [高] 64-19｜if/while/do 错误路径普遍无诊断
- 位置：`ParserStatement.c:703-716`、`974-987`、`939-943`
- 类别：诊断缺失
- 触发条件：缺 `(`、条件表达式失败、缺 `)`、do 条件失败。
- 后果：直接 `return NULL`，无 `parser_report_error`，`error_count` 不增；同一函数内 do 的 923/931/945 却有诊断，策略不一致。
- 最小修复：补诊断（含行号与上下文）。
- 备注：do 的 954-956 分号可缺省且静默，同属诊断缺失。

##### [高] 64-20｜修饰符语义丢失/错映射
- 位置：`ParserStatement.c:156-176`
- 类别：语义分叉 / 死代码
- 触发条件：`const int x = 1;`、`static int x = 1;`、`extern int x;`、`public/private/protected` 修饰非声明语句。
- 后果：174 将 `is_static || is_const` 的变量统一映射为 `AST_STATIC_VARIABLE_DECLARATION`，const 与 static 不可区分；`extern`/access 修饰符被消费但无任何 AST 表示；修饰非声明（如 `static if`）时修饰符静默丢弃。
- 最小修复：为 const/extern/access 增加节点字段或节点类型；无法识别时报错。
- 备注：172 只对 static 函数做转换，const 函数无对应节点。

##### [高] 64-21｜keyword function 直接改写 parser->current_token.type
- 位置：`ParserStatement.c:1269-1273`
- 类别：共享可变状态 / 契约破坏
- 触发条件：`TOKEN_FUNCTION` 且形如 `function f(`。
- 后果：原地把当前 token 类型改为 `TOKEN_VOID`；解析失败时改写不回滚，污染 parser 状态；block 的 `token_advanced` 判定（1225）依赖 `current_token.type`，可能被误导而重置 `consecutive_errors`。
- 最小修复：用局部 `KrtSourceType` 或压入合成 token，不改写共享 token。
- 备注：注释称「语法一致」，但未说明状态改写；若原 token 的 `value` 被 `KrtParseSourceType` 之外逻辑读取也可能错位。

##### [高] 64-22｜unsafe 块体解析失败仍返回带空 expression 的节点
- 位置：`ParserStatement.c:1336-1340`
- 类别：静默失败 / 诊断缺失
- 触发条件：`unsafe(using a;)` 后 `{` 内块解析失败（如缺 `}`）。
- 后果：`parser_parse_block` 返回 NULL 时 1339 直接赋给 `expression` 并 `return node`，无诊断；产出 `expression==NULL` 的 `AST_UNSAFE_CALL`，下游（`Ast.c:483` 有 NULL 检查，但语义/代码生成未必）可能崩溃。
- 最小修复：检查 `expression==NULL`，报错并清理 `node`/`permissions`。
- 备注：1341 的 `unsafe_error` 会报错，但这条路径绕过了它。

##### [高] 64-23｜表达式语句路径缺分号静默、未知 token 静默跳过
- 位置：`ParserStatement.c:1600-1603`、`1630-1633`、`1666-1669`、`1651-1663`
- 类别：诊断缺失 / 静默丢弃 token
- 触发条件：表达式语句缺 `;`；default 分支遇到未知 token。
- 后果：缺分号时销毁 `stmt` 后 `return NULL`，无诊断；未知 token 在 1661 被直接 `parser_advance` 吞掉，无报错，可能吞掉预处理或未来新增 token。
- 最小修复：补诊断；未知 token 报错后再跳过。
- 备注：1654 对 EOF/UNKNOWN 直接返回 NULL 亦不报错；本文件无任何预处理指令交互代码，指令类 token 只能落入此静默路径。

##### [高] 64-24｜泛型/比较前瞻启发式误判且硬编码 256
- 位置：`ParserStatement.c:1532-1570`
- 类别：语义分叉 / 配置漂移
- 触发条件：语句 `a < b > c;`（比较表达式）；或超长泛型/表达式（>256 token）。
- 后果：1562 在 depth 归零且遇 `>` 时调用 `parser_parse_variable_declaration_with_type`，把 `a<b>` 当类型、`c` 当变量名，表达式语句被改写为声明；扫描上限 256 为 magic number，超长时静默落到 else 分支按表达式解析，行为随长度漂移。
- 最小修复：用真实泛型语法判定（如在类型位置）而非比较符深度；把上限提为具名常量并在溢出时报告。
- 备注：1549 与 1562 两个分支存在重叠，逻辑冗余；`depth < 0` 的提前 break 也可能把 `a > b` 这类表达式带入后续分支。

##### [高] 64-25｜heap 子数组挂在 arena 节点上，默认 arena 模式下永不释放
- 位置：`ParserStatement.c:1197/1264`（block statements）、`612/663`（print values）、`1462/1506`（using path_parts）、`788/896`（switch cases）、`808/835`（case stmts）、`1292/1319`（unsafe permissions）
- 类别：资源与生命周期 / 契约破坏
- 触发条件：默认 `parser_create`（arena 非空）下解析任何块/print/using/switch/unsafe。
- 后果：`Ast.c:77` 对 `is_arena_allocated` 节点直接 return，`AST_BLOCK`/`AST_PRINT_STATEMENT`/`AST_USING_DIRECTIVE`/`AST_SWITCH_STATEMENT` 等清理分支不可达；这些 `KRT_MALLOC`/`KRT_REALLOC` 出来的堆数组只在小 arena（NULL）路径下才被释放，默认路径全部泄漏。
- 最小修复：统一所有权（子数组也走 arena），或让 arena 节点登记需堆释放的子资源并由 destroy 释放。
- 备注：错误路径的显式 `KRT_FREE` 说明作者意识到堆所有权，但成功路径无对应释放；`Ast.c` 的清理代码在默认模式下形同虚设。

##### [中] 64-26｜大量 `if (!node)` 守卫在致命分配器后不可达
- 位置：`ParserStatement.c:132`、`296`、`423`、`542`、`560`、`656`、`688`、`741`、`826`、`868`、`892`、`958`、`996`、`1113`、`1178`、`1256`、`1355`、`1409`、`1425`、`1503`
- 类别：死代码 / 注释与实现脱节
- 触发条件：编译期即确定。
- 后果：`KrtParserCreateNode`/`KrtParserAlloc`/`KrtParserStrdup` 失败时走 `KRT_COMPILE_ERROR` → `exit(1)`（`KrtCommon.h:60-65`、`ParserBase.h:9-35`），永远不返回 NULL；这些 `if (!node/ptr)` 分支不可达，误导读者以为存在 OOM 恢复路径。
- 最小修复：删除不可达守卫，或把 `KRT_PARSER_*` 改为返回 NULL 并统一处理。
- 备注：与任务前提 G1 相反——G1 适用于 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC`（确实返回 NULL），不适用于 `KRT_PARSER_*` 系列。

##### [中] 64-27｜变量声明诊断策略不一致（关键字类型报错、用户类型静默）
- 位置：`ParserStatement.c:263-282`
- 类别：错误处理不一致 / 诊断缺失
- 触发条件：`MyType x = ;` 或 `MyType x` 缺分号（`type_token==TOKEN_IDENTIFIER`）。
- 后果：仅 `declared_by_keyword` 时在 266/280 报错，用户自定义类型声明缺表达式/分号时静默 `return NULL`；同一语法错误因类型写法不同诊断不同。
- 最小修复：去掉 `declared_by_keyword` 条件，统一报错。
- 备注：219 的 `KRT_STRDUP` 失败在 230 有检查但无诊断。

##### [中] 64-28｜with_type 拒绝类型位数组大小并静默
- 位置：`ParserStatement.c:338-343`
- 类别：语义分叉 / 诊断缺失
- 触发条件：`int[3] x;`（类型后带大小）。
- 后果：直接 `KRT_FREE(type_name)` 返回 NULL，无诊断；而 `int x[3];` 在 368-388 支持，同一语义两种写法一种静默失败。
- 最小修复：支持类型位大小，或在 342 报错。
- 备注：与 368 的变量位数组处理不一致。

##### [中] 64-29｜指针深度把类型 token 统一改写为 TOKEN_UINT64
- 位置：`ParserStatement.c:318-320`、`444-446`
- 类别：语义分叉 / 配置漂移
- 触发条件：任何带 `*`/`**` 的变量声明。
- 后果：`variable_decl.type` 丢失原始类型 token（仅 `declared_type.pointer_depth` 保留深度），下游若读 `type` 无法区分 `int*`/`char*`；两条声明路径行为一致但信息有损。
- 最小修复：保留原 token，或用专门的指针类型表示。
- 备注：若下游只读 `declared_type` 则影响有限，需跨文件确认。

##### [中] 64-30｜break/continue 缺分号静默、无循环上下文校验
- 位置：`ParserStatement.c:1404-1406`、`1420-1422`
- 类别：诊断缺失 / 阶段不对齐
- 触发条件：`break` 后缺 `;`；`break`/`continue` 出现在循环外。
- 后果：缺分号静默 `return NULL`；循环外 break/continue 被正常建成 AST，parser 不跟踪循环/函数上下文，非法结构推迟到语义阶段（若语义不查则漏到后端）。
- 最小修复：补分号诊断；parser 维护循环深度，或明确交给语义阶段并确认其检查。
- 备注：`Parser.h:46-47` 声明的 `parser_parse_break_statement`/`continue_statement` 未在本文件使用，存在重复/死声明。

##### [中] 64-31｜using 的 alias 从不解析，分号可缺省
- 位置：`ParserStatement.c:1465`、`1499-1501`、`1505`、`1508`
- 类别：死代码 / 诊断缺失
- 触发条件：`using a.b;`（无分号）或任何 using。
- 后果：`alias` 恒 NULL、`is_alias` 恒 0，`using X = Y` 形式不被识别；分号缺省时静默继续，`=` 留给上层当成新语句。
- 最小修复：实现 alias 语法，或删除该字段并对 `=` 报错；要求分号。
- 备注：`Ast.c:349-353` 会释放 alias/namespace_path，但本文件永远给不出 alias。

##### [中] 64-32｜print 的 has_newline 恒为 true
- 位置：`ParserStatement.c:645`、`665`
- 类别：死代码 / 契约破坏
- 触发条件：任何 print 语句。
- 后果：`has_newline` 字段无信息量，print 换行行为不可控；疑似未实现抑制换行的语法。
- 最小修复：根据语法（如尾部标记/参数）设置，或删除该字段。
- 备注：与 611-634 的参数解析无关联，属未接线字段。

### 第六十五批：ParserExpression.c + ParserAdvanced.c

#### 覆盖范围

- 全量读取（分段 read，非通读其他源文件）：
  - `Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（898 行，分 4 段读完）
  - `Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c`（266 行，一次读完）
- 契约核对（仅 grep 特定符号 / 读相关头文件少数行，未整文件通读）：
  - `ParserBase.h` / `ParserBase.c`：`KrtParserAlloc`、`KrtParserStrdup`、`KrtParserCreateNode`、`parser_advance`、`parser_report_error`、`parser_is_type_keyword`
  - `Ast.h` / `Ast.c`：ASTNode 字段、`ast_destroy_node`、`free_string_array`、lambda/call 析构
  - `Tokenizer.h` / `Tokenizer.c`：token 枚举、`token_free`、`lexer_read_identifier`、关键字 value、`lexer->arena`
  - `PointerTypes.inc`：`KrtParseNamedType`、`KrtParseSourceType`
  - `KrtCommon.h`：`KRT_MALLOC/KRT_STRDUP/KRT_FREE/KRT_COMPILE_ERROR`
  - `Arena.c` / `Allocator.c`：arena 指针非独立堆块、`KrtSafeFree` 的 INVALID_FREE 行为
  - `Parser.c` / `CompilerPipeline.c`：错误计数契约（`error_count` 只在 pipeline 层检查）
- 关键结论：`KrtParserAlloc`（arena）在 OOM 时 `KRT_COMPILE_ERROR` 直接 `exit(1)`，因此 `KRT_PARSER_STRDUP`/`KrtParserCreateNode` 的 NULL 分支基本是死代码；G1 的“分配失败返回 NULL 而不 exit”真实适用于 `KRT_MALLOC/KRT_STRDUP/KRT_REALLOC`（`KrtSafeMalloc/KrtSafeStrdup/KrtSafeRealloc`），本批中集中在 `ParserAdvanced.c`。

统计：共 32 条问题（致命 5、严重 15、一般 12）。


##### [致命] 65-1｜表达式解析失败普遍不报告诊断，error_count 不增长导致流水线静默继续
- 位置：`ParserExpression.c:100-102`、`107-109`、`185-192`、`196-208`、`247-254`、`396-399`、`438-447`、`483-489`、`521-523`、`570-572`、`593-604`、`618`、`786-789`、`807-811`、`860-869`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：实参列表、`sizeof`、lambda、括号表达式、数组字面量、`new`、成员/下标后缀、三元等任意语法错误。
- 后果：这些路径直接 `return NULL`，未调用 `parser_report_error`；`parser->error_count` 保持 0。`CompilerPipeline.c:300` 只检查 `parser->error_count > 0`，`Parser.c:26-30` 失败时仅 `parser_advance` 一格后继续，于是残缺 AST 被当作合法输入进入语义分析/代码生成，或把一个错误扩散成多条无关错误。
- 最小修复：为每个失败分支补 `parser_report_error(parser, line, col, "expected ...")`；或在 `parser_parse_call`/`parser_parse_primary` 的统一 cleanup 处集中报告。
- 备注：本文件最系统性的问题；`736` 行的 `"Expected right operand of binary operator"` 是少见的正面例子。

##### [致命] 65-2｜`as` / `??` / `?.` / `?:` 有词法 token 但表达式语法完全不处理
- 位置：`ParserExpression.c:625-662`（优先级表无这些 token）、`758-846`（后缀无 `?.`）、`848-890`（三元无 `?:`）
- 类别：5 死代码 / 3 语义分叉
- 触发条件：`x as T`、`a ?? b`、`a?.b`、`a ?: b`。
- 后果：`get_operator_precedence` 对 `TOKEN_AS/TOKEN_NULL_COALESCING/TOKEN_QUESTION_DOT/TOKEN_QUESTION_COLON` 一律返回 0，解析在操作符处静默停止，剩余 token 滞留；`Parser.c:26-30` 每次只前进一格，导致后续解析错位或静默丢语句。`AST_AS_EXPRESSION` 在 `Ast.h:90`、`AstVisit.inc:312`、`IrGen.c:2192` 都有处理，但没有任何解析路径创建它（grep 全 Parser 目录无 `TOKEN_AS`）。
- 最小修复：在优先级表/后缀/三元中补齐 `as`、`??`、`?.`、`?:`，或明确删除这些 token 与下游分支。
- 备注：与主会话交叉验证的优先级表属同一完整性问题。

##### [致命] 65-3｜深嵌套表达式递归无深度上限
- 位置：`ParserExpression.c:392`（括号）、`418`（数组元素）、`520/548/597`（new）、`802`（下标）、`859/872`（三元）、`705→669→734`（二元/一元互递归）
- 类别：8 未检查前置条件（递归无界）
- 触发条件：源码中出现大量嵌套括号/数组/调用/三元，例如数万个 `(`。
- 后果：`parser_parse_expression ↔ parser_parse_primary ↔ parser_parse_postfix_expression ↔ parser_parse_call` 互相递归，无深度计数，直接栈溢出崩溃，且没有任何诊断。
- 最小修复：在 `Parser` 增加 `parse_depth`，进入表达式解析时 +1，超过阈值（如 256）时报错返回。
- 备注：属于“深嵌套递归”专项。

##### [致命] 65-4｜命名空间 parts 元素 `KRT_STRDUP` 未检查，后续 `strlen/strcat` 空指针崩溃
- 位置：`ParserAdvanced.c:184`、`196`、`204`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`namespace A.B.C` 且 `KRT_STRDUP` 因内存压力返回 NULL（G1：`KrtSafeStrdup` 失败返回 NULL 而不 exit）。
- 后果：`parts[part_count++] = NULL`；`196` 行 `strlen(parts[i])` 直接空指针解引用崩溃；即便绕过，`204` 行 `strcat(full_name, parts[i])` 同样崩溃。
- 最小修复：检查 `KRT_STRDUP` 返回值，失败走统一 cleanup 并 `parser_report_error`。
- 备注：`parts` 是 `KRT_MALLOC`（157）、`KRT_REALLOC`（173），均属 G1 活路径。

##### [致命] 65-5｜类名 `KRT_STRDUP` 未检查且失败静默、token 已前进
- 位置：`ParserAdvanced.c:25`、`31-33`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`class Foo {}` 且堆分配失败。
- 后果：`name = NULL`，`31` 行直接 `return NULL`，无诊断；且 `26` 行已 `parser_advance`，错误位置与状态不一致，error_count 不增长。
- 最小修复：失败时 `parser_report_error` 并保证 token 状态可恢复。
- 备注：与 65-4 同属 G1 的 `KRT_STRDUP` 活路径。

##### [高] 65-6｜优先级表把一元 `TOKEN_NOT` 同时当作二元运算符
- 位置：`ParserExpression.c:657-658`（prec=11）、`702`（一元前缀列表）、`713-734`（二元循环）
- 类别：3 语义分叉
- 触发条件：`a ! b` 之类 `!` 出现在表达式中间（`a != b` 是 `TOKEN_NOT_EQUAL`，不受影响）。
- 后果：`!` 既在一元前缀列表又在优先级表中，`a ! b` 被解析为 `AST_BINARY_OPERATION(operator=TOKEN_NOT)`，右操作数按 prec 12 解析；下游语义/IR 未定义该二元运算，产生与 `!(a,b)` 预期不符的树。
- 最小修复：从 `get_operator_precedence` 移除 `TOKEN_NOT`，或在一元/二元语义间显式区分（如仅允许前缀并在此处拒绝）。
- 备注：与主会话交叉验证的优先级表（:620-657）直接相关。

##### [高] 65-7｜优先级表缺 `TOKEN_POWER`，且 `**` 在词法/语法间语义重载
- 位置：`ParserExpression.c:625-662`（无 `TOKEN_POWER` 分支）、`333-344`/`366-367`（指针深度 2）、`673-684`/`700-704`（前缀双重解引用）
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：表达式中出现 `**`（`a ** b`）。
- 后果：`get_operator_precedence(TOKEN_POWER)=0`，`a ** b` 在 `a` 处停止，剩余 token 滞留；而 `**` 又被当作“指向指针"/双重解引用。词法（`**`）与语法（仅前缀/类型）契约不一致，既非合法二元运算也不报错。
- 最小修复：明确 `**` 的唯一语义；若仅作指针构造，应在主循环显式拒绝并给诊断，而不是静默停止。
- 备注：与主会话交叉验证的 TOKEN_POWER 段落直接相关。

##### [高] 65-8｜lambda 参数直接保存 lexer 词素裸指针，跨 arena 生命周期
- 位置：`ParserExpression.c:240`
- 类别：10 资源与生命周期 / 6 契约破坏 / 7 共享可变状态
- 触发条件：解析 `function(x, y) => ...`。
- 后果：`params[i] = parser->current_token.value` 指向 `lexer->arena`（`Tokenizer.c:326/340`），而其它所有 AST 字符串用 `KRT_PARSER_STRDUP`（`parser->arena`）。两个 arena 分别由 `lexer_destroy` 与 `parser_destroy` 释放（`CompilerPipeline.c:101-105`），生命周期/顺序变化即悬垂；`AST_LAMBDA_EXPRESSION` 的析构 `free_string_array` 会对该指针 `KRT_FREE`（`Ast.c:393`），而它不是独立堆块 → INVALID_FREE（`KRT_DEBUG` 下 `debugbreak`）。
- 最小修复：`params[param_count++] = KRT_PARSER_STRDUP(parser->current_token.value);`
- 备注：对比 `EnumParser.inc:95`、`ParserExpression.c:276/294` 等均采用复制。

##### [高] 65-9｜`global::` 拼接用 512 字节栈缓冲并无条件覆盖原指针
- 位置：`ParserExpression.c:774-776`
- 类别：8 未检查前置条件 / 10 资源与生命周期 / 1 静默失败
- 触发条件：`global::<标识符>` 且 `strlen("global::")+strlen(标识符) ≥ 512`。
- 后果：`snprintf` 静默截断生成错误标识符；`primary->data.identifier_name = KRT_PARSER_STRDUP(tmp)` 直接覆盖旧值（`"global"`）而不释放，造成泄漏；无诊断。
- 最小修复：按 `strlen` 用 `KRT_PARSER_ALLOC_FROM_ARENA` 分配，或先释放旧指针；截断时报错。
- 备注：属于“临时缓冲区截断”专项。

##### [高] 65-10｜`is` 表达式分支泄漏 `left`/`node`、未检查 `node`、失败无诊断
- 位置：`ParserExpression.c:720-733`
- 类别：10 资源与生命周期 / 8 未检查前置条件 / 2 诊断缺失
- 触发条件：`x is UnknownType`（`KrtParseSourceType` 返回 false）。
- 后果：`721` 创建 node 后 `722` 立即解引用（未检查）；`723` 失败时 `return NULL`，`left` 已被 node 持有、node 被遗弃，均不回收；无 `parser_report_error`（依赖 `KrtParseSourceType` 内部报告，若其走 `return false` 分支则完全静默）。
- 最小修复：`if (!node) { ast_destroy_node(left); return NULL; }`；`KrtParseSourceType` 失败时 `ast_destroy_node(left)` 并报告。
- 备注：与 `735-739` 的二元操作数诊断不一致。

##### [高] 65-11｜`new` 表达式多条错误路径泄漏 class_name/size/args 且无诊断
- 位置：`ParserExpression.c:518-533`、`539-574`、`588-616`
- 类别：10 资源与生命周期 / 2 诊断缺失
- 触发条件：`new Foo[` 缺 `]`、`new Foo(` 参数错误/缺 `)`、`new int[` 缺 `]`、`new` 后既非标识符也非类型关键字。
- 后果：`class_name`（`KrtParseNamedType`）、`size`、`args` 被遗弃；`521-523/570-572/593-604/618` 均无 `parser_report_error`，error_count 不增长。
- 最小修复：统一 cleanup 标签，逐层释放并报告具体语法错误。
- 备注：arena 内存在 `parser_destroy` 统一回收，但诊断缺失是实在的。

##### [高] 65-12｜`parser_parse_call` 实参错误无诊断且错误恢复不同步
- 位置：`ParserExpression.c:37-104`、`100-102`、`107-109`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：`foo(a b)`、`foo(ref)`、`foo(` 未闭合、`foo(a: )`。
- 后果：`goto cleanup_error` 直接返回 NULL，不报告期望 `,`/`)`；当前 token 未同步，由 `Parser.c:26-30` 每次只前进一格，错误扩散或静默丢 token。
- 最小修复：`cleanup_error` 内先 `parser_report_error(parser, parser->current_token.line, column, "expected ',' or ')' in argument list")`。
- 备注：命名实参与三元/`ref` 关键字共用 `TOKEN_COLON`/`TOKEN_IDENTIFIER` 判定，边界错误不会报错。

##### [高] 65-13｜`KRT_FREE` 释放 arena 指针（命名实参 `param_name`）
- 位置：`ParserExpression.c:61`、`82`、`140`
- 类别：10 资源与生命周期 / 6 契约破坏
- 触发条件：命名实参解析失败或实参数组扩容失败。
- 后果：`param_name` 来自 `KRT_PARSER_STRDUP`（`KrtParserStrdup` → `KrtParserAlloc` → `parser->arena`），却用 `KRT_FREE`（`KrtSafeFree`）释放。arena 指针不是独立堆块，`find_block` 返回 NULL → INVALID_FREE；release 下静默 no-op，`KRT_DEBUG` 下 `debugbreak`。
- 最小修复：arena 内存不得用 `KRT_FREE`；若需可释放语义，改用 `KRT_STRDUP`。
- 备注：同 `ParserAdvanced.c:54/85/104/119/135`。

##### [高] 65-14｜类声明名用堆 `KRT_STRDUP`，而 AST 节点是 arena 且析构 no-op，造成泄漏并与 enum 路径分叉
- 位置：`ParserAdvanced.c:25`、`140`；对比 `EnumParser.inc:95`；`Ast.c:77-79`、`275-276`
- 类别：10 资源与生命周期 / 3 语义分叉
- 触发条件：每个 `class`/`struct`/`interface` 声明。
- 后果：`node->data.class_decl.name` 是堆指针，但 `ast_destroy_node` 对 `is_arena_allocated` 节点直接 `return`（`Ast.c:77-79`），永不释放 → 每处类声明泄漏；同一 AST 字段在 enum 路径（`EnumParser.inc:95`）是 arena 指针，克隆/释放语义不一致（`AstClone.inc:271` 用 `KRT_CLONE_STRING`）。
- 最小修复：统一用 `KRT_PARSER_STRDUP`（与 `EnumParser.inc` 一致），或让 arena 节点析构真正处理其堆字段。
- 备注：`KRT_FREE(name)` 在错误路径是正确的堆释放，但与成功路径的所有权模型冲突。

##### [高] 65-15｜模板参数 `KRT_FREE` 释放 arena 指针
- 位置：`ParserAdvanced.c:54`、`85`、`104`、`119`、`135`
- 类别：10 资源与生命周期 / 6 契约破坏
- 触发条件：类声明模板参数解析/主体解析/节点创建等错误路径。
- 后果：`template_params[i]` 来自 `KrtParserStrdup`（`72` 行，arena），`KRT_FREE` 触发 INVALID_FREE（`KRT_DEBUG` 下 `debugbreak`），release 下静默 no-op。
- 最小修复：删除这些 `KRT_FREE`，arena 由 `parser_destroy` 统一回收。
- 备注：与 65-13 同根。

##### [高] 65-16｜重复模板参数错误路径泄漏 `name` 与已收集参数
- 位置：`ParserAdvanced.c:66-71`
- 类别：10 资源与生命周期 / 9 错误处理不一致
- 触发条件：`class Foo<A, A>`。
- 后果：报告了 “Duplicate generic parameter” 后直接 `return NULL`，未释放堆 `name`，也未释放 `template_params[0..count-1]`（虽然是 arena，但与同函数其它 cleanup 不一致）。
- 最小修复：复用该函数既有的 cleanup 代码块。
- 备注：同函数 `82-88` 有完整 cleanup，此处遗漏。

##### [高] 65-17｜基类用表达式解析器解析类型位置，且 `base_class == NULL` 不检查
- 位置：`ParserAdvanced.c:92-96`、`98`
- 类别：4 阶段不对齐 / 3 语义分叉 / 8 未检查前置条件
- 触发条件：`class Foo : Bar.Baz()`、`class Foo : global::X`、`class Foo :`（缺类型）。
- 后果：`base_class` 得到的是 `AST_CALL`/`AST_MEMBER_ACCESS` 等表达式节点而非类型名，与 `class_decl.base_class` 的类型语义不符；`95` 行返回 NULL 时 `98` 行不检查，可能生成无基类声明并继续。
- 最小修复：改用 `KrtParseNamedType`/`KrtParseSourceType`；`base_class == NULL` 时报告并走 cleanup。
- 备注：`new` 表达式（`ParserExpression.c:513`）已正确使用 `KrtParseNamedType`，可对照。

##### [高] 65-18｜类声明大量语法错误无诊断
- 位置：`ParserAdvanced.c:27-29`、`62-64`、`82-88`、`98-107`、`113-123`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：缺类名、模板参数非标识符、缺 `>`、缺 `{`、类体解析失败。
- 后果：一律 `return NULL`，无 `parser_report_error`，error_count 不增长，流水线带残缺 AST 继续。
- 最小修复：每个分支补充具体诊断（期望标识符/`/`/`{`）。
- 备注：`class_type` 被 `(void)` 丢弃（19-20），也未校验 class/struct/interface。

##### [高] 65-19｜命名空间声明全程零诊断
- 位置：`ParserAdvanced.c:162-192`、`207-245`、`230-236`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：`namespace` 后非标识符、`namespace A.`、`namespace A` 缺 `{`/`;`、命名空间体解析失败。
- 后果：整个函数没有一次 `parser_report_error`；所有错误静默返回 NULL，error_count 不增长。
- 最小修复：补诊断；`230-236` 与 `238-245` 报告期望 `{`/`;` 或块解析失败。
- 备注：与类声明同属“诊断缺失”模式。

##### [高] 65-20｜`sizeof` 仅接受类型关键字，标识符类型与无括号形式静默失败
- 位置：`ParserExpression.c:194-214`
- 类别：2 诊断缺失 / 3 语义分叉
- 触发条件：`sizeof(MyClass)`、`sizeof x`。
- 后果：`196-198`（缺 `(`）、`200-202`（非类型）、`205-207`（缺 `)`）三处 `return NULL` 均无诊断；`sizeof(MyClass)` 与 `sizeof(int)` 语义不同却都不支持。
- 最小修复：接受标识符类型（`KrtParseNamedType`）；失败时报告具体期望。
- 备注：与 `stackalloc`（176）已支持 `KrtParseSourceType` 形成对照。

##### [中] 65-21｜类型关键字转换路径未填充 `declared_type`/`target_is_pointer`
- 位置：`ParserExpression.c:492-499`（对比 `376-387`）
- 类别：3 语义分叉 / 13 注释与实现脱节
- 触发条件：`int(x)` 与 `(int)x`。
- 后果：两条路径都生成 `AST_CAST_EXPRESSION`，但前者只设 `target_type`/`expression`，后者还设 `target_is_pointer` 与 `declared_type`；下游按 `declared_type` 判断时对同一节点类型产生分叉。
- 最小修复：统一填充 `target_is_pointer`、`declared_type`。
- 备注：`492-499` 的 `declared_type` 为零值。

##### [中] 65-22｜`{...}` 与 `[...]` 都生成 `AST_ARRAY_LITERAL`，无词法标记
- 位置：`ParserExpression.c:403-465`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：`{1,2,3}` 与 `[1,2,3]`。
- 后果：两种字面量落到同一 AST 类型且节点未保存起始 token，下游无法区分数组/集合/对象语义；若语义不同会被静默按同一种处理。
- 最小修复：节点记录 `token.type`，或映射到不同 AST 类型。
- 备注：`closing` 只用于校验，未存入节点。

##### [中] 65-23｜数组/集合字面量元素错误无诊断
- 位置：`ParserExpression.c:417-447`
- 类别：2 诊断缺失
- 触发条件：`[1,,2]`、`[1 2]`、`[1,2`（EOF）。
- 后果：`goto array_cleanup` 直接返回 NULL，无 `parser_report_error`，error_count 不增长。
- 最小修复：在 `419-421`/`438-440`/`445-447` 报告期望表达式/逗号/收尾符。
- 备注：与 65-1 同源但触发位置独立。

##### [中] 65-24｜`TOKEN_NUMBER` 的 `value` 未做 NULL/长度校验
- 位置：`ParserExpression.c:157-160`
- 类别：8 未检查前置条件
- 触发条件：数字 token 的 `value` 为 NULL 或空串（异常 lexer 状态）。
- 后果：`token.value[1]` 越界读；`strchr(token.value, ...)` 空指针崩溃。
- 最小修复：先判断 `token.value && token.value[0]`。
- 备注：正常路径 value 非 NULL，属防御性缺口。

##### [中] 65-25｜`atof` 静默接受非法/溢出浮点
- 位置：`ParserExpression.c:169`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`1.2.3`、`1e999`、`0x1p3`。
- 后果：`atof` 返回部分值/0/inf，无诊断，生成错误常量。
- 最小修复：改用 `strtod` 并检查 `endptr` 与 `errno`，失败时 `parser_report_error`。
- 备注：与 `162` 行 `KrtIntegerParse` 的严格校验形成对照。

##### [中] 65-26｜`KrtIntegerParse` 失败时已创建节点未回收
- 位置：`ParserExpression.c:162-166`
- 类别：10 资源与生命周期
- 触发条件：整数超限或非法。
- 后果：`node` 已创建并写入 `integer_value`，失败直接 `return NULL`，未 `ast_destroy_node(node)`；虽然 arena 节点析构本为 no-op，但清理意图与实现脱节。
- 最小修复：返回前 `ast_destroy_node(node)`。
- 备注：与 `175-192` stackalloc 的节点未回收同型。

##### [中] 65-27｜后缀 `.` 缺标识符、`[` 缺 `]` 无诊断
- 位置：`ParserExpression.c:786-789`、`807-811`
- 类别：2 诊断缺失
- 触发条件：`a.`、`a[1`。
- 后果：静默返回 NULL，error_count 不增长。
- 最小修复：报告期望标识符 / `]`。
- 备注：同文件 `778-783` 的 `::` 有诊断，前后不一致。

##### [中] 65-28｜一元前缀操作数失败无诊断
- 位置：`ParserExpression.c:669-672`
- 类别：2 诊断缺失
- 触发条件：`!`、`-`、`*`、`&` 后无操作数。
- 后果：静默 NULL，error_count 不增长。
- 最小修复：报告 “expected operand after unary operator”。
- 备注：与 `736` 二元操作数诊断不一致。

##### [中] 65-29｜`stackalloc` 错误报告位置错误且节点未回收
- 位置：`ParserExpression.c:184-192`
- 类别：2 诊断缺失（位置错） / 10 资源与生命周期
- 触发条件：`stackalloc int[`、`stackalloc int[1`。
- 后果：`parser_report_error` 使用 `stackalloc` token 的 line/col，而非实际出错的当前 token；`node`/`count_expr` 未回收即返回 NULL。
- 最小修复：用 `parser->current_token.line/column`；cleanup 节点。
- 备注：`176` 行 `KrtParseSourceType` 失败时 `node` 也未回收。

##### [中] 65-30｜cast 检测依赖枚举连续区间，配置漂移即误判
- 位置：`ParserExpression.c:329`
- 类别：11 配置漂移 / 6 契约破坏
- 触发条件：在 `TOKEN_INT2..TOKEN_VOID` 之间插入/重排 token。
- 后果：`next.type >= TOKEN_INT2 && next.type <= TOKEN_VOID` 会误判；`TOKEN_TYPE_STRING` 因定义在区间外需单独 `||`，已显脆弱。
- 最小修复：改用 `parser_is_type_keyword(next.type) || next.type == TOKEN_TYPE_STRING`。
- 备注：`parser_is_type_keyword` 已存在（`ParserBase.c:123`）。

##### [中] 65-31｜cast 的 `target_is_pointer` 与 `pointer_depth` 语义不一致
- 位置：`ParserExpression.c:383-384`
- 类别：3 语义分叉
- 触发条件：`(int[])x` 与 `(int*)x`。
- 后果：数组 cast `pointer_depth=0` 但 `target_is_pointer=1`；`declared_type.pointer_depth=0`。下游若按 `pointer_depth` 判断会认为数组 cast 不是指针，若按 `target_is_pointer` 则相反。
- 最小修复：统一 `pointer_depth`/`target_is_pointer` 的推导规则。
- 备注：`is_array_cast` 未参与 `pointer_depth` 计算（`365-369`）。

##### [中] 65-32｜`KRT_PARSER_UNLIKELY` 宏名不副实
- 位置：`ParserExpression.c:13`
- 类别：13 注释与实现脱节 / 11 配置漂移
- 触发条件：编译期。
- 后果：`#define KRT_PARSER_UNLIKELY(x) (x)` 仅原样展开，无分支预测提示，与命名暗示的 `__builtin_expect` 语义不符。
- 最小修复：改为 `(x)` 语义等价的 `__builtin_expect(!!(x), 0)`，或改名。
- 备注：轻微；不影响正确性。

##### [中] 65-33｜lambda 体解析失败泄漏 node/params；参数扩容失败分支为死代码
- 位置：`ParserExpression.c:228-239`、`257-267`
- 类别：10 资源与生命周期 / 5 死代码
- 触发条件：lambda 体解析失败；参数数量达到上限触发扩容失败。
- 后果：`265-267` 直接 `return NULL`，`node` 与 `params` 未清理；`228-239` 的 `if (!np) { params = NULL; break; }` 丢失旧数组指针，但 `KrtParserAlloc` 在 OOM 时 `exit(1)`，该分支实际不可达（死代码）。
- 最小修复：`265` 前清理；删除或标注不可达分支。
- 备注：arena OOM 致命这一前提由 `ParserBase.h:9-15` 确认。

##### [中] 65-34｜`class_type` 死变量，未校验 class/struct/interface
- 位置：`ParserAdvanced.c:19-20`
- 类别：5 死代码
- 触发条件：编译期。
- 后果：捕获 `class_type` 后 `(void)class_type` 丢弃；未校验当前 token 是否确为 class/struct/interface/enum。
- 最小修复：删除该变量，或用于校验并报错。
- 备注：轻微。

##### [中] 65-35｜类模板参数循环接受尾逗号、缺 EOF 显式检查
- 位置：`ParserAdvanced.c:47-80`
- 类别：8 未检查前置条件 / 3 语义分叉
- 触发条件：`class Foo<A,>`、`class Foo<`（EOF）。
- 后果：`75-79` 逗号后只要遇到 `>` 就退出循环，尾逗号被静默接受；`class Foo<` 靠 `62-64` 的 `break` 退出后报缺 `>`，但无诊断。
- 最小修复：显式拒绝尾逗号；循环条件加 `TOKEN_EOF` 检查并报错。
- 备注：与 `ParserExpression.c` 的实参/数组解析对尾逗号的处理不一致。

##### [中] 65-36｜命名空间用 `KRT_MALLOC/KRT_REALLOC`，与全 parser 的 arena 约定混用
- 位置：`ParserAdvanced.c:10-11`、`157`、`173`
- 类别：6 契约破坏 / 4 阶段不对齐
- 触发条件：解析命名空间声明/OOM。
- 后果：同文件其它部分（`ParserExpression.c`）统一用 `KRT_PARSER_ALLOC_FROM_ARENA`；此处用堆分配，失败静默返回 NULL 无诊断，且 `parts`（堆）与 `full_name`（arena，`198`）分属两个域，生命周期管理分裂。
- 最小修复：统一到 arena，或补诊断并明确 `parts`/`full_name` 的所有权。
- 备注：`full_name` 存入 AST（arena），`parts` 仅临时，建议都用 arena。

### 第六十六批：Ast.c + AstVisit.inc

#### 覆盖范围

- 全量读取：`Re.KrtC/src/Compiler/Frontend/Parser/Ast.c`（570 行）、`Re.KrtC/src/Compiler/Frontend/Parser/AstVisit.inc`（409 行）。
- 为核对契约与调用方，仅做定向 grep：`Ast.h`（ASTNodeType 枚举 106 项、data union 各结构字段与行号）、`AstClone.inc`（克隆覆盖与分配宏）、`ParserBase.h`（节点/字符串分配）、`ParserStatement.c`/`ParserExpression.c`（KRT_STRDUP/KRT_REALLOC/KrtParserAlloc 实际归属）、`CompilerPipeline.c`、`TypeExpansion.inc`、`StdlibCache.inc`（KrtAstVisit/KrtAstClone/ast_destroy_node 调用点）。
- 核心结论：`KrtAstVisit` 对 106 个节点类型覆盖完整；`AstClone.inc` 覆盖完整；**`ast_destroy_node` 只覆盖约 62 个类型**，且对 arena 节点提前返回，与「parser 用 arena 建节点 + KRT_STRDUP/KRT_REALLOC 填堆字段」的现实所有权模型冲突。


##### [致命] 66-1｜arena 节点提前返回，堆字符串/堆数组全部泄漏
- 位置：`Ast.c:77-79`
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：任何 `is_arena_allocated == 1` 的节点被 `ast_destroy_node` 释放。
- 后果：`return` 跳过 switch，节点内 `KRT_STRDUP` 的 `name`/`var_name`/`class_name` 等堆字符串与 `KRT_REALLOC` 的 `values`/`cases` 堆数组全部泄漏；无诊断。
- 最小修复：arena 节点仍应释放其自有堆字段（字符串/数组），仅节点本体与 arena 子节点交给 arena；或统一改为「arena 节点的一切资源都由 arena 分配」。
- 备注：已核实 `ParserBase.h:30` 用 `ast_create_node_arena` 建节点，`ParserStatement.c:219/549/552`、`ParserAdvanced.c:25/184` 用 `KRT_STRDUP` 填字段，`ParserStatement.c:618/821/839` 用 `KRT_REALLOC` 填数组 → 现实路径必然触发。

##### [致命] 66-2｜destroy 与 clone/visit 覆盖不对齐（总纲）
- 位置：`Ast.c:81-565`（destroy switch）对照 `Ast.h:9-120`（枚举）、`AstVisit.inc:6-408`、`AstClone.inc:128-549`
- 类别：6 契约破坏 / 13 注释与实现脱节 / 5 死代码
- 触发条件：凡新增节点类型或走到未覆盖类型。
- 后果：clone/visit 认得的类型，destroy 不认得；同一 AST 在「克隆—遍历—销毁」三阶段行为不一致，且编译期无任何告警。
- 最小修复：为 destroy 补齐全部缺失 case（见 66-3～66-34），或用 X-macro 从同一类型表生成 clone/visit/destroy 三张表。
- 备注：缺失类型共 32 个，逐条见下。

##### [致命] 66-3｜destroy 缺 AST_ASYNC_FUNCTION
- 位置：`Ast.c:81-565` 无此 case；字段见 `Ast.h:109,194-207`；visit/clone 按 function_decl 处理（`AstVisit.inc:19`、`AstClone.inc:142`）
- 类别：10 / 6
- 触发条件：销毁 async 函数节点。
- 后果：name/parameters/parameter_types/parameter_is_* /parameter_default_values/body 全泄漏。
- 最小修复：与 `AST_FUNCTION_DECLARATION` 合并 case，调用 `free_function_decl_data`。

##### [致命] 66-4｜destroy 缺 AST_PROGRAM
- 位置：`Ast.c` 无此 case；`Ast.h:9`；visit/clone 用 `data.block`（`AstVisit.inc:106-111`、`AstClone.inc:240-242`）
- 类别：10 / 6
- 触发条件：销毁程序根节点（如 `CompilerPipeline.c:99`）。
- 后果：整棵顶层语句数组及全部子树泄漏。
- 最小修复：`case AST_PROGRAM: case AST_BLOCK:` 合并走 `free_ast_node_array(block.statements, block.statement_count)`。

##### [致命] 66-5｜destroy 缺 AST_TERNARY_OPERATION
- 位置：`Ast.c` 无此 case；`Ast.h:24,287-291`；visit 见 `AstVisit.inc:95-99`
- 类别：10 / 6
- 触发条件：销毁三元表达式。
- 后果：condition/true_value/false_value 三个子树泄漏。
- 最小修复：补 case 递归销毁三个子节点。

##### [致命] 66-6｜destroy 缺 AST_NEW_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:33,325-328`；visit 见 `AstVisit.inc:126-130`
- 类别：10 / 6
- 触发条件：销毁 new 表达式。
- 后果：`class_name`、`arguments` 数组及元素、`argument_names` 全泄漏。
- 最小修复：补 case，仿 `AST_CALL` 释放。

##### [致命] 66-7｜destroy 缺 AST_NEW_ARRAY_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:34,332-333`；visit 见 `AstVisit.inc:131-133`
- 类别：10 / 6
- 触发条件：销毁 new 数组表达式。
- 后果：`element_type` 字符串与 `size` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-8｜destroy 缺 AST_NAMESPACE_DECLARATION
- 位置：`Ast.c` 无此 case；`Ast.h:35,336-337`；visit 见 `AstVisit.inc:134-136`
- 类别：10 / 6
- 触发条件：销毁命名空间节点。
- 后果：`name` 与 `body` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-9｜destroy 缺 AST_DEFAULT_CLAUSE
- 位置：`Ast.c` 无此 case；`Ast.h:63,432-434`；visit 见 `AstVisit.inc:211-215`
- 类别：10 / 6
- 触发条件：销毁 switch 的 default 分支（switch 节点只销毁 `default_case` 指针指向的该节点，但其 `statements` 数组无人释放）。
- 后果：`statements` 数组及其中语句泄漏。
- 最小修复：补 case 走 `free_ast_node_array`。

##### [致命] 66-10｜destroy 缺 AST_BREAK_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:64,435-438`；visit 见 `AstVisit.inc:216-218`
- 类别：10 / 6
- 触发条件：销毁 break 语句。
- 后果：`value` 子树与 `resolved_class_name` 字符串泄漏。
- 最小修复：补 case。

##### [致命] 66-11｜destroy 缺 AST_CONTINUE_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:65,439-441`；visit 见 `AstVisit.inc:219-221`
- 类别：10 / 6
- 触发条件：销毁 continue 语句。
- 后果：`value` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-12｜destroy 缺 AST_DELETE_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:66,442-446`；visit 见 `AstVisit.inc:222-224`
- 类别：10 / 6
- 触发条件：销毁 delete 语句。
- 后果：`value` 子树与 `resolved_class_name` 泄漏。
- 最小修复：补 case。

##### [致命] 66-13｜destroy 缺 AST_LINQ_LET
- 位置：`Ast.c` 无此 case；`Ast.h:82,524-526`；visit 见 `AstVisit.inc:280-282`
- 类别：10 / 6
- 触发条件：销毁 LINQ let 子句。
- 后果：`var_name` 与 `expression` 泄漏。
- 最小修复：补 case。

##### [致命] 66-14｜destroy 缺 AST_LINQ_GROUP
- 位置：`Ast.c` 无此 case；`Ast.h:83,528-531`；visit 见 `AstVisit.inc:283-286`
- 类别：10 / 6
- 触发条件：销毁 LINQ group 子句。
- 后果：`into_var_name`、`key_expression`、`element_expression` 泄漏。
- 最小修复：补 case。

##### [致命] 66-15｜destroy 缺 AST_SIZEOF_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:91,568-570`；visit 见 `AstVisit.inc:316-317`
- 类别：10 / 6
- 触发条件：销毁 sizeof 表达式。
- 后果：`type_name` 字符串泄漏。
- 最小修复：补 case。

##### [致命] 66-16｜destroy 缺 AST_STACKALLOC_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:92,572-575`；visit 见 `AstVisit.inc:318-320`
- 类别：10 / 6
- 触发条件：销毁 stackalloc 表达式。
- 后果：`type_name` 与 `count_expr` 泄漏。
- 最小修复：补 case。

##### [致命] 66-17｜destroy 缺 AST_INTERPOLATED_STRING
- 位置：`Ast.c` 无此 case；`Ast.h:93,577-581`；visit 见 `AstVisit.inc:321-326`
- 类别：10 / 6
- 触发条件：销毁插值字符串。
- 后果：`string_parts` 与 `expressions` 两个数组及内容泄漏。
- 最小修复：补 case，分别走 `free_string_array` / `free_ast_node_array`。

##### [致命] 66-18｜destroy 缺 AST_TUPLE_TYPE
- 位置：`Ast.c` 无此 case；`Ast.h:94,583-585`；visit 见 `AstVisit.inc:327-328`
- 类别：10 / 6
- 触发条件：销毁元组类型。
- 后果：`element_types` 数组泄漏。
- 最小修复：补 case。

##### [致命] 66-19｜destroy 缺 AST_TUPLE_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:95,587-589`；visit 见 `AstVisit.inc:329-333`
- 类别：10 / 6
- 触发条件：销毁元组表达式。
- 后果：`elements` 数组及元素泄漏。
- 最小修复：补 case。

##### [致命] 66-20｜destroy 缺 AST_TUPLE_ELEMENT_ACCESS
- 位置：`Ast.c` 无此 case；`Ast.h:96,591-593`；visit 见 `AstVisit.inc:334-336`
- 类别：10 / 6
- 触发条件：销毁元组元素访问。
- 后果：`tuple` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-21｜destroy 缺 AST_MATCH_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:97,595-598`；visit 见 `AstVisit.inc:337-342`
- 类别：10 / 6
- 触发条件：销毁 match 表达式。
- 后果：`expression` 与 `cases` 数组及其中 pattern_case 全部泄漏。
- 最小修复：补 case。

##### [致命] 66-22｜destroy 缺 AST_PATTERN_CASE
- 位置：`Ast.c` 无此 case；`Ast.h:98,600-603`；visit 见 `AstVisit.inc:343-347`
- 类别：10 / 6
- 触发条件：销毁 match 分支。
- 后果：`pattern`、`when_clause`、`body` 三棵子树泄漏。
- 最小修复：补 case。

##### [致命] 66-23｜destroy 缺 AST_PATTERN_LITERAL
- 位置：`Ast.c` 无此 case；`Ast.h:100,607-608`；visit 见 `AstVisit.inc:350-352`
- 类别：10 / 6
- 触发条件：销毁字面量模式。
- 后果：`value` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-24｜destroy 缺 AST_PATTERN_VARIABLE
- 位置：`Ast.c` 无此 case；`Ast.h:101,610-611`；visit 见 `AstVisit.inc:353-354`
- 类别：10 / 6
- 触发条件：销毁变量模式。
- 后果：`name` 字符串泄漏。
- 最小修复：补 case。

##### [致命] 66-25｜destroy 缺 AST_PATTERN_TUPLE
- 位置：`Ast.c` 无此 case；`Ast.h:102,613-615`；visit 见 `AstVisit.inc:355-359`
- 类别：10 / 6
- 触发条件：销毁元组模式。
- 后果：`elements` 数组及元素泄漏。
- 最小修复：补 case。

##### [致命] 66-26｜destroy 缺 AST_POINTER_DEREFERENCE
- 位置：`Ast.c` 无此 case；`Ast.h:104,620-622`；visit 见 `AstVisit.inc:366-368`
- 类别：10 / 6
- 触发条件：销毁指针解引用。
- 后果：`pointer` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-27｜destroy 缺 AST_POINTER_ASSIGNMENT
- 位置：`Ast.c` 无此 case；`Ast.h:120,623-627`；visit 见 `AstVisit.inc:362-365`
- 类别：10 / 6
- 触发条件：销毁指针赋值。
- 后果：`pointer` 与 `value` 两棵子树泄漏。
- 最小修复：补 case。

##### [致命] 66-28｜destroy 缺 AST_ADDRESS_OF
- 位置：`Ast.c` 无此 case；`Ast.h:105,628-630`；visit 见 `AstVisit.inc:369-371`
- 类别：10 / 6
- 触发条件：销毁取地址表达式。
- 后果：`operand` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-29｜destroy 缺 AST_FIXED_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:106,631-635`；visit 见 `AstVisit.inc:372-375`
- 类别：10 / 6
- 触发条件：销毁 fixed 语句。
- 后果：`variable_name`、`expression`、`body` 泄漏。
- 最小修复：补 case。

##### [致命] 66-30｜destroy 缺 AST_AWAIT_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:110,639-641`；visit 见 `AstVisit.inc:379-381`
- 类别：10 / 6
- 触发条件：销毁 await 表达式。
- 后果：`expression` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-31｜destroy 缺 AST_NULLABLE_TYPE
- 位置：`Ast.c` 无此 case；`Ast.h:115,658-661`；visit 见 `AstVisit.inc:392-394`
- 类别：10 / 6
- 触发条件：销毁可空类型节点。
- 后果：`type_expr` 子树泄漏。
- 最小修复：补 case。

##### [致命] 66-32｜destroy 缺 AST_NULL_COALESCING
- 位置：`Ast.c` 无此 case；`Ast.h:116,662-665`；visit 见 `AstVisit.inc:395-398`
- 类别：10 / 6
- 触发条件：销毁 ?? 表达式。
- 后果：`left`/`right` 两棵子树泄漏。
- 最小修复：补 case。

##### [致命] 66-33｜destroy 缺 AST_NULL_CONDITIONAL
- 位置：`Ast.c` 无此 case；`Ast.h:117,666-671`；visit 见 `AstVisit.inc:399-402`
- 类别：10 / 6
- 触发条件：销毁 ?. 表达式。
- 后果：`expression`、`member_name`、`arguments` 泄漏。
- 最小修复：补 case。

##### [致命] 66-34｜destroy 缺 AST_CAST_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:118,672-676`；visit 见 `AstVisit.inc:403-405`
- 类别：10 / 6
- 触发条件：销毁类型转换表达式。
- 后果：`expression` 子树泄漏。
- 最小修复：补 case。

##### [高] 66-35｜free_function_decl_data 漏释 parameter_default_values / parameter_is_nullable / parameter_is_array
- 位置：`Ast.c:55-70`（调用点 `Ast.c:92-102`）；字段见 `Ast.h:185-194,198-207`
- 类别：10 资源与生命周期 / 6 契约破坏
- 触发条件：销毁 `AST_FUNCTION_DECLARATION` 或 `AST_STATIC_FUNCTION_DECLARATION`。
- 后果：`parameter_default_values` 数组及其子节点、`parameter_is_nullable`、`parameter_is_array` 两个 int 数组全部泄漏；visit/clone 都认得这些字段，唯独 destroy 漏。
- 最小修复：在 helper 中补 `free_ast_node_array(parameter_default_values, param_count)` 与两个 `KRT_FREE`。
- 备注：clone 明确克隆了这三组（`AstClone.inc:147-149,157-159`），证明属自有资源。

##### [高] 66-36｜constructor_decl 漏释 base_arguments
- 位置：`Ast.c:261-267`；字段见 `Ast.h:361-366`
- 类别：10 / 6
- 触发条件：销毁构造函数声明。
- 后果：`base_arguments` 数组及其中基类实参子树泄漏（clone 已克隆，`AstClone.inc:290`）。
- 最小修复：补 `free_ast_node_array(constructor_decl.base_arguments, base_argument_count)`。

##### [高] 66-37｜property_decl 漏释 backing_field_name
- 位置：`Ast.c:364-378`；字段见 `Ast.h:466-473`
- 类别：10 / 6
- 触发条件：销毁属性声明。
- 后果：`backing_field_name` 字符串泄漏（clone 已克隆，`AstClone.inc:376`）。
- 最小修复：补 `KRT_FREE(property_decl.backing_field_name)`。

##### [高] 66-38｜is_expr 漏释 binding_name
- 位置：`Ast.c:506-516`；字段见 `Ast.h:557-561`
- 类别：10 / 6
- 触发条件：销毁 is 表达式。
- 后果：`binding_name` 字符串泄漏（clone 已克隆，`AstClone.inc:449`）。
- 最小修复：补 `KRT_FREE(is_expr.binding_name)`。

##### [高] 66-39｜default 分支仍 KRT_FREE(node)，释放行为随分配方式分叉
- 位置：`Ast.c:563-567`
- 类别：3 语义分叉 / 9 错误处理不一致 / 10
- 触发条件：destroy 一个未覆盖类型（如 66-3～66-34）的节点。
- 后果：堆节点只释放节点本体、子资源全泄漏；arena 节点连节点本体都不释放（66-1）。同一类型在不同分配路径下销毁语义不同。
- 最小修复：补齐 case；明确「节点本体归属由 is_arena_allocated 决定、字段资源一律释放」的统一规则。
- 备注：与 66-1 互为表里。

##### [高] 66-40｜KrtAstVisit 无深度上限、无环/共享子树检测
- 位置：`AstVisit.inc:2-408`
- 类别：8 未检查前置条件（递归无界/深度）/ 7 共享可变状态
- 触发条件：深层嵌套 AST，或 DAG/共享子节点（如 attribute_list.target 与 attributes 指向同一节点）。
- 后果：栈溢出崩溃；共享子树被重复访问，变异型 visitor 会对同一节点重复施加变换。
- 最小修复：加深度参数/上限与 visited 集合（或要求 visitor 幂等并在文档中写明）。
- 备注：`TypeExpansion.inc:334` 的 `expansion_substitute_node` 是变异型 visitor，重复访问会重复替换。

##### [高] 66-41｜KrtAstVisit 返回 void，visitor 返回 false 被静默截断
- 位置：`AstVisit.inc:2-5`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：任一 visitor 返回 false（如 `expansion_resolve_node`/`expansion_substitute_node` 出错）。
- 后果：遍历中途停止，函数无返回值、无诊断，调用方（`TypeExpansion.inc:117,334,446`）无法感知只处理了部分树，后续阶段基于不完整结果继续。
- 最小修复：返回 bool/错误码，或提供「遇 false 立即报错」的契约与日志。
- 备注：访问器返回值被忽略处即 `AstVisit.inc:3` 的 `!visitor(...)` 判定。

##### [高] 66-42｜visitor 在类型派发前调用，允许变异/释放节点导致悬垂访问
- 位置：`AstVisit.inc:3-6`
- 类别：7 共享可变状态 / 8 未检查前置条件
- 触发条件：visitor 释放当前节点或改写其子指针（TypeExpansion 的替换型 visitor 即属此类）。
- 后果：随后 `switch` 读取 `node->data.*` 可能是已释放/已改写的内存，造成 UAF 或漏访新子树。
- 最小修复：先缓存本节点需要的子指针再调用 visitor，或明确规定 visitor 不得释放当前节点并加断言。
- 备注：需验证 `expansion_substitute_node` 是否原地改写 `node->data`。

##### [中] 66-43｜ast_create_node 无任何调用方，堆节点清理路径实际不可达
- 位置：`Ast.c:9-11`；全仓 grep 仅 `Ast.h:681` 声明与本定义
- 类别：5 死代码 / 6 契约破坏
- 触发条件：——
- 后果：公开 API 暗示支持堆节点，但全仓所有节点都走 `ast_create_node_arena`（`ParserBase.h:30`），destroy 中针对堆节点的整段 switch 分支成为不可达代码；一旦有人用 `ast_create_node` 建节点并填入 arena 数组（parser 的数组正是 arena 分配，`ParserExpression.c:30`），destroy 会 `KRT_FREE` arena 指针 → 堆损坏。
- 最小修复：删除 `ast_create_node`，或让 destroy 严格按字段分配方式释放并加文档/断言。
- 备注：需验证是否存在测试或外部绑定调用 `ast_create_node`。

##### [中] 66-44｜free_function_decl_data 以 const char* 接收 name 却强制转换后 free
- 位置：`Ast.c:55-59`
- 类别：6 契约破坏 / 10
- 触发条件：调用方传入字符串字面量或只读内存。
- 后果：`KRT_FREE((void*)name)` 释放只读内存 → 崩溃；const 契约与实际行为矛盾。
- 最小修复：形参改为 `char*`，与 node 字段类型一致。

##### [中] 66-45｜AST_CALL 用 argument_count 释放 argument_names，计数假设未校验
- 位置：`Ast.c:210`；字段见 `Ast.h:293-302`
- 类别：8 未检查前置条件 / 10
- 触发条件：`argument_names` 非空但实际长度与 `argument_count` 不一致（命名参数少于位置参数时若只分配命名个数）。
- 后果：越界读/越界写或漏释。
- 最小修复：显式保存 `argument_name_count` 字段，或断言二者相等。
- 备注：clone 同样按 `argument_count` 克隆（`AstClone.inc:235`），需验证 parser 是否总是分配 `argument_count` 长。

##### [中] 66-46｜AstVisit.inc 无 include guard，存在重复包含风险
- 位置：`AstVisit.inc:1-2`；包含点 `Ast.c:570`
- 类别：6 契约破坏 / 5 死代码（潜在）
- 触发条件：其他文件再次 `#include "AstVisit.inc"`。
- 后果：`KrtAstVisit` 重复定义，链接错误；当前仅 `Ast.c` 包含，属潜伏缺陷。
- 最小修复：加 `#pragma once`/`#ifndef`  guard，或改为头文件声明 + 单独 .c。

##### [中] 66-47｜ast_create_node_arena 分配失败静默返回 NULL
- 位置：`Ast.c:13-23`
- 类别：1 静默失败 / 2 诊断缺失（G1）
- 触发条件：`KrtArenaAlloc`/`KRT_MALLOC` 失败。
- 后果：函数无日志直接返回 NULL；`KrtMemoryReportError` 为空操作，分配失败零诊断。
- 最小修复：失败时调用诊断接口或返回可区分错误；至少在有诊断框架处上报。
- 备注：`ParserBase.h:31-32` 用 `KRT_COMPILE_ERROR` 兜底，故 parser 路径最终会 exit；但其他调用方无保障。

##### [低] 66-48｜AST_THIS 空 case 多余空行
- 位置：`Ast.c:227-229`
- 类别：13 注释与实现脱节 / 5 死代码
- 触发条件：——
- 后果：无功能影响，但暴露 destroy 表是手工维护、易漏写。
- 最小修复：删除空行或与叶子类型合并 case。
- 备注：与 66-2 同一根因。

##### [中] 66-49｜测试不足：无 clone/visit/destroy 覆盖矩阵测试
- 位置：`Ast.c`、`AstVisit.inc` 全文（无对应测试断言）
- 类别：12 测试不足
- 触发条件：新增节点类型或修改 destroy 表。
- 后果：32 个类型漏销毁长期未被发现；无回归测试保证三表一致。
- 最小修复：加一个遍历 106 个类型的覆盖测试，并对每类构造最小节点验证 destroy 后无泄漏（ASan）。
- 备注：需验证测试目录是否已有此类用例。

### 第六十七批：IrInline.inc + IrRecursiveInline.inc

#### 覆盖范围

- 全量分段读取（read，非通读其他文件）：
  - `Re.KrtC/src/Compiler/Middle/Ir/IrInline.inc`，共 283 行（1-150 / 151-283）。
  - `Re.KrtC/src/Compiler/Middle/Ir/IrRecursiveInline.inc`，共 588 行（1-150 / 151-300 / 301-450 / 451-588）。
- 调用方用 grep 核实（只查符号，未整文件通读）：
  - `IrOptimizer.c:621-623` include 两个 `.inc`；`IrOptimizer.c:626` 调 `inline_small_functions`，`:628` 调 `inline_recursive_functions`，二者同属 `ir_optimize_function_inlining`（`:625-633`）。
  - `ir_optimize_function_inlining` 只被 `ir_optimize_module`（`IrOptimizer.c:647-649`）在 `OPT_FUNCTION_INLINING` flag 下调用，位置在 `while (changed && pass < KRT_MAX_OPTIMIZATION_PASSES)` 迭代循环（`:653-684`）**之前**，全模块仅执行一次。
  - `KrtIrSsaOptimize` 仅在 `IrSsa.h:114` 声明、`IrSsa.c:1497` 定义；调用方只有 `VmCodegen.c:220`、`X86Codegen.c:1019`，`KroCodegen.c:1296` 已注释禁用。**中端（含两个内联器）从不构造 SSA**，与背景结论一致；内联器运行时模块不在 SSA 形态，`IrRecursiveInline.inc:548-549` 的 probe CSE 是在无支配信息的 CFG 上按块局部运行。
- 辅助定点核对（小窗口/单符号，用于判定不变量与根因）：`Ir.h:281-343`（KrtIRInst/KrtIRBasicBlock/KrtIRFunction 字段）、`IrOptimizer.c:67-90`（`is_pure_operation`）、`IrPureCse.inc:30-186/249-344/390-467`（CSE 与未使用整数）、`Ir.c:406-412`（`KrtIrInteger`）。
- 内存 API：两文件全部使用 `KRT_MALLOC/KRT_REALLOC/KRT_CALLOC/KRT_STRDUP/KRT_FREE`，未发现裸 `malloc/free/realloc/calloc/strdup` 混用（此点合格）。


##### [致命] 67-1｜DIV/MOD 被当作纯操作，trap 语义被改变
- 位置：`IrRecursiveInline.inc:98`（配合 `IrOptimizer.c:67-90`；`IrInline.inc:33` 同因）
- 类别：语义分叉；注释与实现脱节
- 触发条件：候选函数体含 `KRT_IR_DIV` / `KRT_IR_MOD`（含除数为常量 0 或运行时 0 的情形）。
- 后果：`is_pure_operation` 把 DIV/MOD 列为纯操作，递归展开会把除法复制到每个展开路径，且 probe 里的 `eliminate_pure_expressions` 会把同参数重复除法 CSE 成 `COPY`，从而消除原本会发生的第二次 trap（除零/溢出）。文件头注释声称 “nonlocal effects are deliberately excluded”，但 trap 是可观测效应；同仓库 `IrPureCse.inc:383-384` 明确把 DIV/MOD 排除出可删除指令，反证二者在此代码库中被视为可 trap。
- 最小修复：在 `is_pure_operation` 中移除 DIV/MOD，或在 `:98` 与 `:33` 两处资格判定中显式排除这两个 opcode。
- 备注：`IrInline.inc:33` 会把含除法的小函数整体内联，问题同构。

##### [致命] 67-2｜recursive_value 未命中静默回退，提交时原块已被释放
- 位置：`IrRecursiveInline.inc:184`（`return value;` 原样返回）；`:573`（`recursive_destroy_blocks(function->entry_block)`）
- 类别：静默失败；契约破坏
- 触发条件：emit 阶段 `map` 查不到某个 TEMP，例如 `:443-446` 子块 `child_map.count = 0` 之后子块引用父块定义的 temp，或 continuation 的 `map` 与 `source_block` 错位。
- 后果：拷贝指令保留原函数的 temp id；提交时 `function->entry_block` 整条块链被 `KRT_FREE`，新 IR 引用已释放/未定义的 temp，产生错误代码或后续 pass 崩溃。
- 最小修复：`recursive_value` 未命中时置 `expansion->failed` 并返回显式哨兵；或在 emit 前断言 map 对 `recursive_check_block` 已验证的 def-use 完整。
- 备注：当前由 `recursive_check_block` 的跨块使用检查（`:131-140`）兜底，属防御缺失而非已触发缺陷。

##### [高] 67-3｜递归深度上限语义错乱：continuation 回弹 depth
- 位置：`IrRecursiveInline.inc:8`（`KRT_RECURSIVE_DEPTH_LIMIT 3`）；`:418-421`（`next = {..., depth, ...}` 后传入 `depth - 1`）
- 类别：终止条件；注释与实现脱节
- 触发条件：同一基本块内含 ≥2 个自调用（`calls <= 4` 允许）。
- 后果：continuation 保存的是调用点的 `depth`（未递减），callee 用 `depth - 1` 展开；resume 后同一块内后一个自调用又以完整 `depth = 3` 展开。实际嵌套展开层数 ≈ 自调用链长度 × 深度，而非 3；文件头 “Bounded self-recursive expansion” 与真实预算语义不符。
- 最小修复：continuation 保存 `depth - 1`，或在 `:412` 额外检查全局展开计数。
- 备注：有 `instruction_count <= 384` 兜底（`:318`、`:361`），故不会无界；但上限不是注释所称的“深度”。

##### [高] 67-4｜IrInline 增长预算按函数计，模块级代码膨胀无上限
- 位置：`IrInline.inc:4`（`KRT_INLINE_GROWTH_LIMIT 256`）、`:5`（`KRT_INLINE_CALLER_LIMIT 1024`）、`:254`（`growth` 在函数循环内清零）
- 类别：配置漂移；资源与生命周期
- 触发条件：模块含多个调用方或多个小函数。
- 后果：每个调用方独立获得 256 指令增长预算，模块体积随调用点数量近似线性无界增长；`KRT_INLINE_CALLER_LIMIT` 只限单个调用方。
- 最小修复：把 growth 预算提升到模块级，并在 `:262` 用模块累计值判断。
- 备注：与 `IrRecursiveInline.inc:475-478` 的模块级 `remaining_growth` 口径不一致，两个内联器预算不可比。

##### [高] 67-5｜probe 硬编码 optimization_level=3，与真实 pipeline 阶段不对齐
- 位置：`IrRecursiveInline.inc:546`（`.optimization_level = 3`）；`IrOptimizer.c:647-649`（内联在迭代循环前只跑一次）
- 类别：阶段不对齐；配置漂移
- 触发条件：模块 `optimization_level != 3`，或 `flags` 未启用后续 CSE/DCE pass。
- 后果：probe 中 `eliminate_pure_expressions`/`eliminate_unused_integers` 的收益（`cse_count`、`after_calls < before_calls` 判定）与真实 pipeline 不一致，可能选到在后续 pass 中无收益的深度；同时 probe 的 CSE 在无 SSA、无支配信息的 CFG 上按块局部工作（中端从不构造 SSA，见覆盖范围）。
- 最小修复：`probe.optimization_level` 取 `module->optimization_level`；把内联器纳入迭代循环或注明一次性语义。
- 备注：已 grep 核实 `KrtIrSsaOptimize` 不在中端调用链上。

##### [高] 67-6｜分配失败全部静默，零诊断（G1 前提成立）
- 位置：`IrInline.inc:147-155`、`:178-181`、`:228-231`；`IrRecursiveInline.inc:201-225`、`:323-337`、`:365-369`、`:408-413`
- 类别：静默失败；诊断缺失
- 触发条件：`KRT_CALLOC/KRT_MALLOC/KRT_REALLOC/KRT_STRDUP` 返回 NULL。
- 后果：优化静默失效（`inline_call` 返回 false、`inline_small_functions` 返回 false、`expansion.failed = true`），无日志、无统计；调用方 `ir_optimize_function_inlining` 只累计 `changed`，无法区分“无候选”与“内存不足”。
- 最小修复：失败路径输出诊断并计入独立统计（如 alloc_fail_count）。
- 备注：失败路径本身有正确释放（无泄漏），但完全无诊断。

##### [高] 67-7｜recursive_append_inst 无容量检查，安全性依赖两个常量严格相等
- 位置：`IrRecursiveInline.inc:306-314`（`block->insts[block->inst_count++]` 无检查）；`:210`（容量固定 `KRT_RECURSIVE_GROWTH_LIMIT`）；`:318`、`:361`（唯一上限）
- 类别：未检查前置条件；缓冲区越界风险
- 触发条件：任何使 `inst_count` 超过 384 的路径；当前仅靠 `instruction_count <= 384` 与容量 384 恰好相等来保证。
- 后果：`block->insts[block->inst_count++]` 越界写；安全性依赖两个独立常量严格相等，任一被调整即溢出。
- 最小修复：append 前检查 `inst_count < inst_capacity`，否则置 `failed`。
- 备注：当前边界恰好相等（384/384），属脆弱耦合。

##### [中] 67-8｜recursive_new_block 半初始化块先入链并消耗 block id
- 位置：`IrRecursiveInline.inc:196-226`（`:206` 分配 id、`:216-221` 接链发生在失败检查 `:222` 之前）
- 类别：资源与生命周期；未检查前置条件
- 触发条件：`block->label/insts/preds/succs` 任一分配失败。
- 后果：块已挂入 `expansion->first/last` 且 `next_block` 已推进，`expansion->last` 指向半初始化块；若后续代码继续 append 会写入未分配数组（当前靠 `failed` 短路避免）。
- 最小修复：先分配全部资源，成功后再接链并分配 id。
- 备注：失败后整体丢弃，故当前不落地。

##### [中] 67-9｜calls/size 在 DAG 上重复计数，资格判定与预算失真
- 位置：`IrRecursiveInline.inc:85-89`（`path` 只查环不去重）、`:106`（`(*calls)++`）、`:93`（`size++`）
- 类别：语义分叉（资格判定）；漏优化
- 触发条件：同一基本块经多条路径可达（DAG 形 CFG）。
- 后果：只有 1 个自调用的函数被重复计成 `calls >= 2` 而错误纳入候选；同时 `size` 被高估，`:493` 的 `remaining_growth < size` 会提前跳过本可展开的函数。
- 最小修复：用已访问集合去重后再计数，或明确按路径计数并注释。
- 备注：不会误纳不安全函数，只影响命中率与预算。

##### [中] 67-10｜terminal 之后的死代码被静默丢弃且仍计入 original_size
- 位置：`IrRecursiveInline.inc:145-159`（遇 terminal 即 `return`，不检查其后是否还有指令）；`:496-503`（`original_size` 全量计数）
- 类别：死代码；语义分叉
- 触发条件：`RETURN/JUMP/BRANCH` 之后仍有指令。
- 后果：展开时 `:401-429` 同样在 terminal 处停止，死代码被静默删除；而 `original_size` 仍把它算入，使 `growth` 被低估甚至为负，影响 `remaining_growth` 与 stats 准确性。
- 最小修复：eligibility 中若 terminal 后仍有指令则判不合格，或从 `original_size` 中剔除。
- 备注：删死代码本身无害，问题在计数口径与静默性。

##### [中] 67-11｜inline_call 失败回滚不回卷 next_temp
- 位置：`IrInline.inc:163`（`++*next_temp`）；`:198-205`（`failed` 只释放拷贝）
- 类别：共享可变状态；错误处理不一致
- 触发条件：拷贝途中 `KRT_CALLOC/KRT_MALLOC` 失败，或 `block->insts` 扩容（`:178`）失败。
- 后果：`next_temp` 已被推进且不复位，temp id 出现空洞；同一 block 的后续内联继续使用被推进的值，属部分修改。
- 最小修复：记录进入时的 `next_temp` 基线，`failed` 时回卷。
- 备注：id 单调递增，不会产生错误引用，属资源浪费与状态不一致。

##### [中] 67-12｜超限调用方被 continue 跳过 insts 重建与缓存失效
- 位置：`IrInline.inc:251-253`（`continue`）；`:270-274`（`insts` 重建与 `KrtIrBlockInvalidateCache`）
- 类别：契约破坏；死代码
- 触发条件：调用方指令数 > `KRT_INLINE_CALLER_LIMIT`（1024）。
- 后果：该函数所有 block 的 `insts` 重建与缓存失效被跳过；若其 `insts` 与链表已不一致（其他 pass 只改链表），不一致会保留到后端。
- 最小修复：把重建/失效移到 `continue` 之前，或无条件执行。
- 备注：正常路径下二者一致。

##### [中] 67-13｜内联/展开后 function 元数据（has_calls/stack_size）不更新
- 位置：`IrInline.inc:263`（`call` 改 `COPY` 后）；`IrRecursiveInline.inc:573-575`（替换 `entry_block/exit_block` 后）
- 类别：契约破坏；死代码
- 触发条件：内联移除了调用，或递归展开替换了整个块链。
- 后果：`KrtIRFunction.has_calls` / `stack_size` 与 IR 脱节；grep 全 `src` 显示 `has_calls` 仅在 `Ir.c:949`、`Ir.c:1034` 写入、无任何读取方，`stack_size` 在中端亦无读取方，属“写了不读”的死状态，内联器既不改也不删。
- 最小修复：内联/展开后重算这两个字段，或删除字段。
- 备注：当前无可观测影响，但属零容忍的“死代码/契约破坏”。

##### [中] 67-14｜候选查找假设函数名非 NULL 且唯一
- 位置：`IrInline.inc:85-89`（`strcmp(a->function->name, ...)` 无 NULL 检查）；`:91-106`（二分返回任意命中）
- 类别：未检查前置条件
- 触发条件：`function->name == NULL`（`strcmp` 崩溃）；模块内存在同名函数（任意命中，可能内联到错误目标）。
- 后果：崩溃或错误内联。
- 最小修复：收集候选时跳过 NULL 名；重名时返回错误或按更精确键匹配。
- 备注：正常模块名唯一。

##### [中] 67-15｜内联器不在迭代循环内，新机会不再被内联
- 位置：`IrOptimizer.c:626`、`:647-649`（在 `while (changed && ...)` 之前调用一次）
- 类别：阶段不对齐；漏优化
- 触发条件：内联后产生新的可内联候选（如某函数变成单块纯函数）。
- 后果：新机会不会被再次内联；收敛性依赖“先 small 后 recursive”的单次顺序，且内联产生的 `changed` 不参与迭代。
- 最小修复：把内联纳入迭代循环，或在文档中明确一次性语义。
- 备注：已 grep 核实调用位置。

##### [中] 67-16｜inline_operand 未命中 TEMP 时静默返回原值
- 位置：`IrInline.inc:112-119`
- 类别：静默失败；契约破坏
- 触发条件：callee 的 TEMP 操作数未在 `old_ids` 中（当前由 `inline_body_size:55-64` 的 `ids` 检查保证不发生）。
- 后果：拷贝保留 callee 的 temp id，与调用方 temp 空间冲突；一旦放宽 `inline_body_size` 条件即成为实际缺陷。
- 最小修复：未命中时返回 false，让 `inline_call` 走 `failed`。
- 备注：与 67-2 同源（不同文件），当前受 `:55-64` 兜底。

##### [低] 67-17｜缺少针对递归内联的回归测试
- 位置：`IrRecursiveInline.inc:1-588`、`IrInline.inc:1-283`（全仓无配套测试引用）
- 类别：测试不足
- 触发条件：回归测试缺失时，任何深度/仿射/回滚改动都无法被证伪。
- 后果：grep 全仓仅 `Test/Performance/O3Optimization/ReadMe.md:33` 描述 IrInline 设计、`Test/Performance/LLVMOptimization/Analysis/RunAnalysis.py:69` 记录 `recursive_inline_depths` 指标；未发现覆盖自递归/互递归、`depth=1..3`、`calls=2..4`、除零、分配失败注入、DAG 重复计数、跨块 SSA 拒绝的用例。
- 最小修复：为上述边界各加回归用例，并对 `KRT_*` 注入失败验证回滚。
- 备注：据 grep 结果据实描述，未通读测试目录。

### 第六十八批：Re.KrtC/stdlib/Array.krt（含 libs/System 副本比对）

#### 覆盖范围

- 审计对象：`/sdcard/KairoteLang/Re.KrtC/stdlib/Array.krt`，全量 1420 行（`ArrayOps` 静态类，覆盖 byte/int8/int16/uint16/int32/uint32/int64/uint64/int128/uint128/bool/float32/float64 共 13 种数组类型的 Length/Copy/Clear/Fill/Reverse/IndexOf/Contains）。
- 副本比对：`/sdcard/KairoteLang/libs/System/Array.krt` 全量 1420 行。
- 比对方法与限制：本会话 bash 沙箱后端不可用（`workspace-write` 无后端，`danger-full-access` 被自动拒绝），`mcp__phone__android_shell` 返回 `disabled`，因此无法执行 `md5sum`/`diff -q`。改用 read 工具对两文件做全量逐行比对（各分两段读完）：**两文件行数一致（1420 行）、逐行内容一致，未发现任何差异**。
- 比对结论：**两处副本当前同步，记为排除项（无问题）**；但这是人工逐行比对，非哈希校验，建议在具备 shell 的环境补做 `md5sum` 复核。
- 未读取范围：`System.Memory`（`Memory.TryMove`/`TryClear` 语义、参数顺序、原子性）与编译器堆头布局定义均不在允许读取范围内，相关判断已在条目中标注为「片外不可验证」。


##### [致命] 68-1｜null 指针被折叠为长度 0，对 null 的 Clear/Copy/Fill/Reverse 静默返回 true
- 位置：`Re.KrtC/stdlib/Array.krt:8-12`（`get_length` 对 `data == null` 返回 0）、`:70-74`、`:77-79`、`:95-97`、`:107-109`、`:126-128`、`:148-150`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：调用 `Clear(null)`、`Fill(null, v)`、`Reverse(null)`、`Copy(null, dst)`、`Copy(src, null)`（src 为空）等；`Length(null)` 同样返回 0。
- 后果：`get_length` 把「null 引用」与「长度 0 的合法数组」映射为同一返回值 0，随后 `valid_range(0,0,0)` 全部通过，函数返回 `true`。调用方得到「操作成功」的假信号，无法区分「什么都没做」与「已执行」，在自举标准库中会向上传播为静默错误结果（例如 `Clear(null)` 后继续读取依赖该数组的逻辑）。
- 最小修复：`get_length` 对 `data == null` 返回独立哨兵（如 `-2`）或让 `Length` 对 null 返回 -1；各 `Clear/Fill/Reverse/Copy` 在 `Length < 0`（含 null）时返回 `false`，使 null 与空数组可区分。
- 备注：文档注释仅声明 `Length` 对 null 返回 0，但未声明 `Clear/Copy/Fill/Reverse` 会把 null 当成功；契约与实现脱节。

##### [致命] 68-2｜get_length 无数组头校验，对裸缓冲区/野指针读 data-8，静默错误长度或越界读崩溃
- 位置：`Re.KrtC/stdlib/Array.krt:13-27`
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：把 `Memory.Allocate` 得到的裸缓冲区、已释放数组、或任何非「编译器数组」指针当作 `byte[]`/`int32[]` 等传入 `Length`/`Copy`/`Clear`。
- 后果：`header[-1]` 直接解引用 `data - 8`，只校验 `total_size >= 16` 与整除性和 `count <= INT32_MAX`，不校验魔数/类型标记/归属。裸缓冲区的 `data-8` 是无意义值：轻则算出错误长度并返回「成功」，重则读到未映射页直接崩溃；文件头注释（`:5`）已承认裸缓冲区不可传入，但运行期无任何强制或检测。
- 最小修复：在堆头中加入并校验魔数/元素类型标记；`get_length` 校验失败时返回 -1 并让上层返回 false；对 `data` 做可达性/归属断言（若运行期支持）。
- 备注：这是编译器自举标准库，错误长度会直接污染编译产物。

##### [高] 68-3｜失败信号语义分叉：bool / -1 / -2 三套约定，2 参 IndexOf 把「无效数组头」与「未找到」同编为 -1
- 位置：`Re.KrtC/stdlib/Array.krt:147-166`（对照 `:69-74`、`:94-104`）
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：无效数组头（`get_length` 返回 -1）时，`Length` 返回 -1、`Copy/Clear/Fill/Reverse` 返回 `false`、2 参 `IndexOf` 返回 -1、4 参 `IndexOf` 返回 -2。
- 后果：同一根因（数组头无效）在不同 API 得到三种不同信号；`IndexOf(values, value)` 的 -1 同时表示「未找到」和「数组无效」，调用方若按注释只区分 -1/-2 会误判；`Contains` 用 `>= 0` 掩盖了该区分。错误处理约定不统一，调用方无法写出一致的错误分支。
- 最小修复：统一错误码契约（如统一返回 -1 表示未找到、-2 表示参数/数组无效），并在全部 `IndexOf` 重载间保持一致；或统一改为返回 `bool` + 输出参数。
- 备注：注释 `:152` 只对 4 参重载声明了 -1/-2 语义，2 参重载注释（`:147`）未声明无效头情形。

##### [高] 68-4｜全量 Copy 以源长度覆盖目标，目标容量仅靠 TryMove 失败返回值表达，原子性契约未断言
- 位置：`Re.KrtC/stdlib/Array.krt:76-79`、`:38-54`（`copy_range`）
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：`Copy(source, destination)` 中 `destination` 长度小于 `source` 长度（或 `Copy(source, destination, count)` 中 count 超出目标容量）。
- 后果：函数把 `Length(source)` 当作 count 传入 `copy_range`，仅在 `Memory.TryMove` 返回 false 时报错。注释声称「false leaves destination unchanged」，但片内没有任何机制保证 `TryMove` 失败时零写入（部分写入后返回 false 则目标已被污染）；同时目标容量是隐式推断的，调用方无法在调用前断言。目标长于源时返回 true 但尾部保持原值，属未文档化的部分写入语义。
- 最小修复：在 `Copy` 入口显式断言 `Length(destination) >= Length(source)`（全量重载）或 `destination_index + count <= Length(destination)`，不满足直接返回 false；在片内或 Memory 契约中明确 `TryMove` 失败的原子性并加断言。
- 备注：`Memory.TryMove/TryClear` 的参数顺序与失败语义在允许读取范围外，本条基于 Array.krt 自身契约缺口成立。

##### [中] 68-5｜bool/float 数组使用 `(void*)(int64)` 强转，整数数组使用 `(void*)` 直转，data-8 头布局假设未对齐验证
- 位置：`Re.KrtC/stdlib/Array.krt:1110-1112`（bool）、`:1214-1216`（float32）、`:1318-1320`（float64），对照 `:70-72`、`:486-490`、`:694-698`、`:902-906`
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：对 `bool[]`/`float32[]`/`float64[]` 调用 `Length`/`Copy`/`Clear` 等走 `get_length` 的路径。
- 后果：同一 `get_length` 对全部类型假设「分配字节数在 data-8、堆头 16 字节」，但三类浮点/布尔类型需要经 `int64` 中转才能转 `void*`，与整数数组的直接转换风格不一致，暗示其指针表示或头布局可能不同（如引用类型/装箱表示）。若布局不同，`header[-1]` 读到的不是分配字节数，`Length` 会静默返回错误值，进而使范围校验全部错位。
- 最小修复：统一转换写法并在注释中记录每类数组的头布局来源；若 bool/float 数组确实使用不同表示，需为 `get_length` 增加按元素类型的头解析分支并补充断言。
- 备注：本条为表示层不一致引发的潜在语义分叉，需在可读 `System.Memory`/编译器头定义时确认；片内无法证伪。

##### [中] 68-6｜缺少 uint8（及 char/float16 等）重载，标准整数数组 API 覆盖存在缺口
- 位置：`Re.KrtC/stdlib/Array.krt:70-1420`（全文件仅有 byte/int8/int16/uint16/int32/uint32/int64/uint64/int128/uint128）
- 类别：5 死代码（API 缺口）/ 12 测试不足
- 触发条件：调用方需要 `uint8[]`（若 Krt 中 `uint8` 与 `byte` 为不同别名/不同类型）、`char[]`、`float16[]` 的数组操作时。
- 后果：文件头注释（`:4`）声称覆盖「standard integers」，但无 `uint8` 命名重载；若 `byte` 与 `uint8` 在类型系统中等价则无实际影响，若不等价则调用方只能拿到错误类型的 API 或被迫做不安全转换，且无编译期报错。
- 最小修复：确认 `byte` 与 `uint8` 是否同一类型；若不同则补齐 `uint8`（及实际存在的 `char`/`float16`）全套重载，或在注释中显式声明 `byte` 即 `uint8`。
- 备注：片内无法确认类型别名关系，需结合编译器类型定义核实。

##### [中] 68-7｜诊断缺失：null、空数组、头损坏、范围越界四种失败不可区分，标准库零诊断
- 位置：`Re.KrtC/stdlib/Array.krt:31-36`（`valid_range` 单一 false）、`:43-46`、`:58-60`、`:113-115`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：任何一次失败调用（范围越界、头无效、null、容量不足）。
- 后果：所有失败路径只产出 `false`/`-1`/`-2`，不携带任何原因信息，也无断言/日志/错误码细分。在自举编译器场景中，标准库失败会被编译产物静默继承，排查时无法从返回值定位是调用方越界还是数组损坏。
- 最小修复：为失败原因定义可区分错误码（如 0=OK，1= null，2=无效头，3=范围越界，4=容量不足），或至少在 `assert`/诊断通道输出失败原因；保持返回值契约兼容。
- 备注：与 68-1、68-3 互为表里，但本条聚焦「无任何诊断载体」这一独立缺陷。

##### [低] 68-8｜注释与实现脱节：Copy/Clear 注释宣称的语义与 null、部分填充行为不符
- 位置：`Re.KrtC/stdlib/Array.krt:76-79`（Copy 全量）、`:94-97`（Clear 全量）
- 类别：13 注释与实现脱节
- 触发条件：目标长于源时执行 `Copy(source, destination)`；对 null 执行 `Clear(values)`。
- 后果：注释称 Copy「Copy all source elements ... false leaves destination unchanged」，未说明目标剩余尾部保留原值（部分写入）以及 `Copy(null, dst)` 返回 true；注释称 Clear「reporting whether the array header is valid」，但 null 也返回 true。调用方按注释编码会得出错误预期。
- 最小修复：更新注释为精确契约（目标尾部不变、null 视为长度 0 且返回 true、无效头返回 false），或按 68-1 修复后同步注释。
- 备注：与 68-1 同源，但作为文档一致性独立记录。

##### [低] 68-9｜测试不足：片内无契约测试，null/零长度/重叠/失败原子性均无覆盖证据
- 位置：`Re.KrtC/stdlib/Array.krt:1-1420`（全文件无任何测试或示例调用）
- 类别：12 测试不足
- 触发条件：审计与回归验证时。
- 后果：`TryMove` 重叠正确性、失败时目标不变、`count == 0`、`start == length`、null、无效头、`IndexOf` 的 -1/-2 区分均只有注释声明，无片内测试固化；后续改动无法防止回归，尤其 68-4 的原子性契约无任何验证手段。
- 最小修复：为该标准库补一组契约测试，至少覆盖 null、空数组、零 count、start==length、源/目标重叠、目标容量不足、无效头哨兵。
- 备注：测试可能位于允许读取范围外的测试工程；若存在，本条可降级，但本切片内无据可查。

##### [低] 68-10｜未固定数组即取裸指针做内存拷贝，若 GC 可移动对象则存在悬垂/损坏风险
- 位置：`Re.KrtC/stdlib/Array.krt:40-53`（`copy_range`）、`:57-66`（`clear_range`）、`:8-29`（读头）
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：在 `unsafe(using krt.mem;)` 块内持有 `void*` 期间发生 GC（尤其压缩/整理式 GC）。
- 后果：数组以裸指针进入不安全内存操作，片内未见 pin/固定或 GC 协作（如 `GC.KeepAlive`、句柄化）。若 Krt 运行期使用可移动 GC，`TryMove`/`TryClear` 执行中对象被搬移将导致写到旧地址，静默数据损坏；若 GC 不可移动则无影响。
- 最小修复：确认 Krt GC 是否压缩；若压缩，需在操作期间固定数组（pin）或使用 GC 句柄；若不可移动，在注释中明确记录该前提。
- 备注：片外不可验证，基于片内「无固定措施」这一事实记录，供 GC 语义确认后定性。

#### 排除项

- 两处副本同步：`Re.KrtC/stdlib/Array.krt` 与 `libs/System/Array.krt` 均为 1420 行，全量逐行比对未发现差异，**不记语义分叉问题**。受限于 bash/Android shell 不可用，未做 `md5sum` 哈希级复核（见「覆盖范围」）。
- `valid_range` 的 `length - start` 与调用方 `start + count`：在 `start >= 0 && count >= 0 && count <= length - start` 成立时不会溢出，未发现整数溢出路径，不记问题。
- `element_size` 除零：仅由片内常量（1/2/4/8/16）调用，公开 API 无法传入 0，不记问题。

### 第六十九批：stdlib/String.krt + Math.krt + StringBuilder.krt

#### 覆盖范围

- 全量读取（分段）：
  - `/sdcard/KairoteLang/Re.KrtC/stdlib/String.krt`（407 行，1-140 / 141-280 / 281-407）
  - `/sdcard/KairoteLang/Re.KrtC/stdlib/Math.krt`（375 行，1-200 / 201-375）
  - `/sdcard/KairoteLang/Re.KrtC/stdlib/StringBuilder.krt`（271 行，1-140 / 141-271）
- 副本比对：本机 bash 不可用——默认 workspace-write 沙箱无可用后端，显式申请 danger-full-access 被拒绝（不得重试/绕过）。因此 `diff -q` / `md5sum` 未能执行。改用只读抽样核对 `libs/System/` 下三个同名副本：行数一致（407/375/271），头部、中部、尾部抽样文本与 stdlib 逐字一致，未发现不同步证据；但**无法据此出具 md5 一致性结论**，该结论缺项记于此处。
- 额外发现：`grep` 显示仓库里还存在第三套实现 `Re.KrtC/src/Runtime/Krt/{String,Math,StringBuilder}.krt}`，其签名结构与 stdlib 明显不同（见 69-1）。按授权限制未整文件通读这些副本，仅用 grep 取签名行作为分叉证据。
- 本批未读取任何测试文件，故「测试不足」不做无证据断言。


##### [致命] 69-1｜同仓库存在三套 stdlib 副本，API 与实现语义分叉
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:8,13,19,45,88,146,183,223,256`；`Re.KrtC/src/Runtime/Krt/Math.krt:8,11,18,49,57,71,89,107,121`；`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:11,15,30,54,64,71,84,98,132,153,159,180,206`（对比 `Re.KrtC/stdlib/String.krt:9`、`Math.krt:5`、`StringBuilder.krt:12`）
- 类别：语义分叉（多处副本/多套实现）、配置漂移、阶段不对齐
- 触发条件：编译/打包阶段选错切片，或两套副本同时进入构建；调用方按 stdlib 的 Try* 系列、`AppendInt64/AppendUInt64`、`FromUInt64`、`Trim*`、`IsNullOrWhiteSpace` 等 API 编码。
- 后果：src 版 Math 无 `TryAbs/TryClamp/TryPow/TrySqrt/TryLCM`、无 int64 版 `Pow/Sqrt/GCD/LCM`，且 `LCM` 用 `Abs(a / GCD(a, b) * b)` 走 int32 回绕路径；src 版 String 缺 `FromUInt64/IsNullOrEmpty/IsNullOrWhiteSpace/Trim*/ToUpper/ToLower` 并多一个 `ConcatMultiple`；src 版 StringBuilder 参数名漂移为 `bufferSize/currentLength`，缺 `AppendInt64/AppendUInt64`。编译期报未定义符号，或更糟：链接到旧实现后数值/字符串行为与 stdlib 文档承诺不一致且无任何告警。
- 最小修复：确定唯一权威副本（建议 `Re.KrtC/stdlib/`），删除或同步 `Re.KrtC/src/Runtime/Krt/` 下三个同名文件；在 CI 加 `diff -q`/md5 断言，禁止同名异文副本共存。
- 备注：受权限所限未能做全文 diff；以上为依据 grep 签名行的结构差异，已足以判定非同一实现。`libs/System/` 副本抽样与 stdlib 一致，但同样缺 md5 证据。

##### [致命] 69-2｜FreeBuffer 的容量由调用方回传，错配即堆损坏；负容量静默跳过释放
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:33-39`（关键 35-37）
- 类别：未检查前置条件、资源与生命周期、契约破坏（容量约定）
- 触发条件：`FreeBuffer(buf, cap)` 的 `cap` 与 `AllocateBuffer(cap)` 时的容量不一致（含先扩容/复用缓冲区后）；或传入 `capacity < 0`。
- 后果：按错误长度调用 `Memory.Free`，轻则分配器元数据破坏、重则 heap 损坏；`capacity < 0` 时函数直接不释放，造成内存泄漏且无任何诊断。
- 最小修复：缓冲区头部自带容量/长度元数据，`FreeBuffer(buffer)` 单参即可；至少在文档与实现中强制 `capacity >= 0`，并对非法值返回错误码而非静默忽略。
- 备注：类注释第 9 行声称「invalid arguments ... return -1」，但 `FreeBuffer` 返回 void，注释与实现脱节（类别 13）。

##### [致命] 69-3｜Remove 无 buffer_size 形参，current_length 无上界校验，可越界读写
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:174-192`（校验仅 176，写终止符 189）
- 类别：未检查前置条件（越界）、契约破坏
- 触发条件：调用方传入大于实际分配容量的 `current_length`（API 只要求调用方自持长度，函数无法验证）；或 `index+length` 接近长度上限。
- 后果：`Memory.Move((void*)(data + index), (void*)(data + tail_start), current_length - tail_start)` 从越界地址读并向越界区写；`data[result] = 0` 同样可能落在分配块外。
- 最小修复：与 `AppendString` 一致引入 `buffer_size`，用 `valid_buffer(buffer, buffer_size, current_length)` 校验；或提供带容量的重载。
- 备注：`Remove` 是全类中唯一完全丢弃容量维度的变更操作，与 `Insert/AppendString` 的契约不对称。

##### [高] 69-4｜Replace 同样缺容量校验，且 `data[length] = 0` 可越过分配块
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:252-270`（校验 254，越界写点 267）
- 类别：未检查前置条件、契约破坏
- 触发条件：`length` 大于真实容量（调用方误传或缓冲区被复用过）；特别地当 `length > buffer_size` 时 `data[length] = 0` 写到终止符槽之后。
- 后果：越界读整个 `[0, length)` 区间并越界写 1 字节，静默破坏相邻堆数据。
- 最小修复：增加 `buffer_size` 形参并校验 `length <= buffer_size`；把终止符写入限定在容量内。
- 备注：与 69-3 同源（显式长度 API 不携带容量），但函数与写点独立，单独记录。

##### [高] 69-5｜GCD 溢出静默回绕，且无 Try 版本可发现
- 位置：`Re.KrtC/stdlib/Math.krt:132-134`（int32）、`284-286`（int64）
- 类别：契约破坏（数值范围）、静默失败、未检查前置条件（溢出）
- 触发条件：int32 版 `a = b = -2147483648`（或任意 |gcd| = 2^31），数学结果 2147483648 超出 int32；`(int32)gcd_magnitude(...)` 直接回绕为 `-2147483648`。
- 后果：静默返回负的最小整数；调用方拿到的「最大公约数」既非正数也非正确值，且没有任何错误通道（GCD 没有 Try 重载）。
- 最小修复：返回 int64/uint64 量级，或提供 `TryGCD` 在幅度不可表示时返回 false；至少对负结果给出诊断。
- 备注：注释自认「unrepresentable positive magnitude wraps to the minimum integer」——属于已知但未阻断的静默错误。

##### [高] 69-6｜LCM 固定宽度回绕，返回错误的数学结果
- 位置：`Re.KrtC/stdlib/Math.krt:137-144`（int32，回绕点 143）、`289-296`（int64，回绕点 295）
- 类别：契约破坏（数值范围）、静默失败
- 触发条件：最小公倍数超过声明宽度，例如 int32 的 `LCM(-2147483648, 1)`、int64 的 `LCM(-9223372036854775808, 2)`。
- 后果：uint64 乘积被 `(int32)/(int64)` 截断后再 `Abs`，得到回绕值（甚至负值），与注释所称 LCM 语义不符；只有 `TryLCM` 才能发现溢出。
- 最小修复：让 `LCM` 在溢出时走与 `TryLCM` 相同的阈值判断并返回可诊断的结果，或明确将其标记为 deprecated 并强制使用 `TryLCM`。
- 备注：`Abs((int32)(...))` 对回绕后的 INT_MIN 仍返回 INT_MIN，错误被二次掩盖。

##### [高] 69-7｜Sqrt 对负数输入静默返回 0（负数开方）
- 位置：`Re.KrtC/stdlib/Math.krt:107-119`（int32，分支 108-110）、`259-271`（int64，分支 260-262）
- 类别：未检查前置条件（负数开方）、静默失败、诊断缺失
- 触发条件：任何 `value < 0`，包括 `-1`、`int32.MinValue`、`int64.MinValue`。
- 后果：返回 0，与「负数无实数平方根」相悖；调用方无法把「输入为负」与「输入为 0」区分开，后续计算继续传播错误。
- 最小修复：负数输入返回 NaN/错误码或断言；保留旧语义就必须在文档与调用点强制改用 `TrySqrt`，并在负数时给出告警。
- 备注：注释写明「negative inputs retain the legacy zero result」，说明是继承行为而非疏漏，但对调用方仍是无声错误。

##### [中] 69-8｜Clamp 在 minimum > maximum 时静默返回 minimum
- 位置：`Re.KrtC/stdlib/Math.krt:43-51`（int32）、`199-207`（int64）
- 类别：未检查前置条件、静默失败
- 触发条件：调用方交换了上下界，如 `Clamp(5, 10, 0)`。
- 后果：函数不报错，直接返回 `minimum`（10），把非法区间当合法处理；同类已有 `TryClamp` 却未在 `Clamp` 内复用校验。
- 最小修复：`Clamp` 内部先判 `minimum > maximum`，返回可诊断结果；或在调试构建中断言。
- 备注：注释仅声明「minimum must not exceed maximum」，未说明违反时行为，契约不完整。

##### [中] 69-9｜Pow 对负指数静默返回 1
- 位置：`Re.KrtC/stdlib/Math.krt:64-76`（int32）、`220-232`（int64）
- 类别：未检查前置条件、静默失败
- 触发条件：`exponent < 0`（数学上应为分数/未定义于整数域），以及任意溢出情形。
- 后果：`while (exponent > 0)` 直接跳过，返回 1；溢出则按声明宽度回绕，两者都不产生任何信号。
- 最小修复：负指数与溢出路径统一改为可诊断返回，或至少让文档/命名表明这是「整数幂的 legacy 截断语义」。
- 备注：`TryPow` 明确拒绝负指数，说明设计上承认该输入非法，但 `Pow` 未对齐。

##### [中] 69-10｜Abs 对最小整数回绕，仅 TryAbs 能暴露
- 位置：`Re.KrtC/stdlib/Math.krt:7-12`（int32，`-value` 在 9）、`163-168`（int64，`-value` 在 165）
- 类别：未检查前置条件（溢出）、静默失败
- 触发条件：`value == int32.MinValue` 或 `int64.MinValue`。
- 后果：`-value` 按声明宽度回绕回自身，返回负数当作「绝对值」；调用方若不知晓 TryAbs 版本就会拿到错误结果。
- 最小修复：让 `Abs` 在最小值上走可诊断路径，或在注释中显式标注返回值可能为负并禁止在无检查场景使用。
- 备注：`LCM`（69-6）正依赖该回绕行为，错误会级联。

##### [中] 69-11｜char 被静默截断为一个字节，造成数据损坏
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:60-70`（`(byte)value` 在 66）、`252-270`（`(byte)new_character` 在 262；`(int32)data[index]` 与 `(int32)old_character` 比较在 261）
- 类别：契约破坏（UTF-8/字符约定）、静默失败、诊断缺失
- 触发条件：传入码点 > 255 的 `char`，如 `'Ā'(0x100)`、CJK 字符；AppendChar 写入低 8 位，Replace 用被截断的码点匹配。
- 后果：写入字节与预期字符不符，且 Replace 的 `old_character` 也被截断，匹配目标随平台/编码漂移；全程无告警。
- 最小修复：明确 API 只接受 0-255 并在入口校验，或改为接收/写入完整码点的 UTF-8 序列。
- 备注：与 69-12 是同一「char 与 byte 语义混用」的两处独立表现。

##### [中] 69-12｜String.IndexOf/LastIndexOf 用码点与原始字节比较，>255 永不命中且可能误命中
- 位置：`Re.KrtC/stdlib/String.krt:147-160`（比较 153）、`163-179`（比较 172）
- 类别：契约破坏（UTF-8 约定）、诊断缺失
- 触发条件：`character` 码点 > 255（如中文、`'Ā'`）；或搜索某多字节 UTF-8 字符的首/后续字节恰与某码点低 8 位相同。
- 后果：合法字符永远返回 -1（假阴性）；某些码点又可能与缓冲区内的单个字节相等而返回错误下标（假阳性）。
- 最小修复：把参数类型改为字节（`byte`）或明确「只按单字节匹配」并在 >255 时给出诊断。
- 备注：文件头注释只声明「indices count bytes」，未声明 char 参数按字节截断还是按码点比较，契约不清。

##### [中] 69-13｜只读的显式长度 API 不校验 length 上界，可越界读
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:216-231`（IndexOf）、`234-249`（LastIndexOf）、`200-213`（BufferToString，拷贝点 209）
- 类别：未检查前置条件（越界）、契约破坏
- 触发条件：调用方传入的 `length` 大于缓冲区实际分配容量（这三个函数都没有 `buffer_size` 可校验）。
- 后果：按伪造长度扫描/拷贝，越界读堆数据；BufferToString 还会把越界内容复制进新分配串，扩大泄漏面。
- 最小修复：为只读 API 也增加容量形参并校验，或由缓冲区对象自身携带容量。
- 备注：与 69-3/69-4 构成同一「显式长度无容量维度」的系统性契约缺口，因函数独立而分条列出。

##### [中] 69-14｜Substring 对越界 start/length 一律静默钳制，错误输入被吞
- 位置：`Re.KrtC/stdlib/String.krt:263-293`（钳制 272-283）
- 类别：静默失败、未检查前置条件（索引约定）、诊断缺失
- 触发条件：`start > source_length`、`start < 0`、`length < 0`、`length > source_length - start`。
- 后果：非法请求被转成合法的空串/短串返回，调用方无法察觉自己传错了参数；与常见「越界抛错」契约相反。
- 最小修复：保留钳制版本的同时提供严格版（越界返回 null 或错误码），并在注释中把钳制列为显式选择而非默认。
- 备注：注释确实写明 clamped，因此属于「有意设计但对调用方无声」，按零容忍标准记录。

##### [中] 69-15｜TryPow 的 int32 与 int64 实现是两套独立算法，行为易漂移
- 位置：`Re.KrtC/stdlib/Math.krt:79-104`（int32 内联乘积检查，平方只查上界 95-98）对照 `235-256`（int64 复用 `multiply_checked`，338-358）
- 类别：语义分叉（多套实现）、维护风险
- 触发条件：未来修改溢出判定或指数循环时只改其中一处；两版对中间量 `base_value` 的平方时机相同但检查方式不同。
- 后果：同一数学函数在 32/64 位宽度下可能出现接受/拒绝边界不一致，且难以通过代码审阅发现。
- 最小修复：抽出一个泛型/共用 helper，两个宽度共用同一判定逻辑。
- 备注：当前两版结果等价（已逐分支核对 `multiply_checked` 四个象限含 INT64_MIN/-1 边界），但实现重复本身就是分叉源。

##### [中] 69-16｜String 各查询 API 对 null 一律静默降级，无诊断
- 位置：`Re.KrtC/stdlib/String.krt:114-116`（Equals(null,null)=true）、`119-121`、`187-196`、`222-234`、`253-255`、`296-312`、`315-325`
- 类别：静默失败、错误处理不一致
- 触发条件：任何 null 实参：`Equals`/`Contains`/`StartsWith`/`EndsWith` 返回 false，`Compare` 返回 ±1，`IndexOf` 返回 -1。
- 后果：调用方把「参数为空」与「确实不包含/不相等」混为一谈；`Equals(null, null) == true` 与 `IsNullOrEmpty(null) == true` 叠加后，空引用可被当作合法相等值参与逻辑。
- 最小修复：为 null 提供独立错误信号（如返回可选类型或断言），或至少在文档中把 null 语义提升为强契约并配套静态检查。
- 备注：注释均写明 null 行为，属已知设计；但跨 API 的错误表达方式不统一（bool 用 false、int 用 -1、Compare 用序关系），符合「错误处理不一致」。

##### [低] 69-17｜Length 的 -1 哨兵在判空 API 中变成 false，无法区分「超大非法」与「正常非空」
- 位置：`Re.KrtC/stdlib/String.krt:11-17`（哨兵来源）、`119-121`（IsNullOrEmpty）、`128-144`（IsNullOrWhiteSpace 在 131-133 显式返回 false）
- 类别：契约破坏、诊断缺失
- 触发条件：`InternalStringLength(value) > int32.MaxValue`。
- 后果：函数族对同一非法状态给出 `false`，与 `Concat/Substring/Trim/ToUpper` 的 `null` 表达不一致，调用方必须额外记得判断 `Length() == -1` 才能识别。
- 最小修复：统一错误通道（例如单独的 `TryLength` 或异常/断言），或在文档中明确要求调用方先检查 `Length()`。
- 备注：`-1` 同时被 `IndexOf` 用作「未找到」，哨兵复用加剧歧义。

##### [低] 69-18｜Clear 忽略入参且不触碰缓冲区，命名与实现脱节
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:195-197`（另见类注释 10）
- 类别：死代码、注释与实现脱节
- 触发条件：调用 `Clear(current_length)` 期望清空缓冲区内容。
- 后果：函数体恒返回 0，参数完全未使用；缓冲区旧字节仍在原处，仅靠后续 mutation 覆盖终止符，名字 `Clear` 暗示的动作并未发生。
- 最小修复：要么真正写 `data[0] = 0`（需要 buffer 形参），要么改名/删除该重载。
- 备注：注释自称「legacy overload does not access the buffer」，属于已知遗留物，但公共 API 保留即构成误导。

##### [低] 69-19｜append_owned 用 Length 反推释放长度，Length 为 -1 时按 0 字节释放
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:72-81`（释放点 78，Length 被计算两次）
- 类别：资源与生命周期、死代码（当前不可达分支）
- 触发条件：若该私有函数将来被复用于长度超过 int32 的字符串；当前唯一调用方传入 `FromInt64/FromUInt64` 产物（最多 20 位），故不可达。
- 后果：`Memory.Free(ptr, 0)` 尺寸错误，分配器可能报错或破坏；同时 Length 二次调用浪费且与 AppendString 内的校验各算一次。
- 最小修复：让 `FromInt64/FromUInt64` 返回时带上长度，或用局部变量保存首次 `Length` 结果并校验 `< 0` 后提前返回。
- 备注：标记为死分支是为了避免夸大，但释放尺寸来源本身不可靠。

##### [低] 69-20｜AllocateBuffer 把「非法容量」与「分配失败」合并为返回 0，无法区分
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:18-30`（20-22 与 24-26 都落到 0）
- 类别：错误处理不一致、诊断缺失
- 触发条件：`capacity < 0`，或 `Memory.Allocate` 返回 null。
- 后果：调用方拿到 0 后无法判断是参数错还是 OOM；随后的 `FreeBuffer(0, cap)` 被当作 null 忽略，错误被吞。
- 最小修复：非法容量走断言/错误码，与分配失败分开表达。
- 备注：与 69-2 的「容量需调用方回传」叠加后，错误定位链进一步变弱。

#### 备注（非问题条目）

- `libs/System/` 与 `Re.KrtC/stdlib/` 三个同名文件抽样一致、行数一致，但因 bash/diff/md5sum 权限被拒，不能声明完全相同；建议后续批次在具备权限时补做逐字节比对。
- 本批未发现明确的「阶段不对齐」编译错误证据；69-1 的副本分叉已覆盖该风险面。

### 第七十批：stdlib/{Console,Convert,Sys,Memory,Exception}.krt

#### 覆盖范围

- 已用 read 工具全量读完（未通读其他源文件）：
  - `Re.KrtC/stdlib/Console.krt`（153 行）
  - `Re.KrtC/stdlib/Convert.krt`（173 行）
  - `Re.KrtC/stdlib/Sys.krt`（76 行）
  - `Re.KrtC/stdlib/Memory.krt`（154 行）
  - `Re.KrtC/stdlib/Exception.krt`（20 行）
  - 合计 576 行。
- libs/System 副本比对：本环境下 bash 沙箱不可用（workspace-write 无可用后端，danger-full-access 申请被拒），未能执行 `diff -q` / `md5sum`。改用定向 grep 比对命名空间、常量、全部 public/private 函数签名与关键逻辑行号：`libs/System/{Console,Convert,Sys,Memory,Exception}.krt` 与 stdlib 副本在 `Console.Write:46`/`WriteFormat:124`、`Convert.ToInt64:12`/`TryToInt64:53`、`Sys.Syscall:13`/`InternalStringLength:24`、`Memory.Allocate:9`/`TryMove:75`、`Exception:4-17` 等处逐行对齐，未发现语义分叉。建议后续用 `md5sum` 做全量复核。
- 另注：`Re.KrtC/src/Runtime/Krt/` 下存在同名但实现不同的另一层（如 `Memory.Allocate` 无 size 校验、`Convert` 无 `Try*`、`Console.write_raw` 无 EINTR 重试），该分叉由第七十一批（`audit_extract/cont_14_runtime.md`）覆盖，本批不重复计入。


##### [中] 70-1｜write 系统调用失败与部分写被静默丢弃
- 位置：`Re.KrtC/stdlib/Console.krt:16-25`（关键 20-22）
- 类别：1 静默失败 / 2 诊断缺失 / 9 错误处理不一致
- 触发条件：`write_raw` 中 `Sys.Syscall(1, ...)` 返回 0 或负 errno（如 EBADF、EPIPE、ENOSPC、EIO），或只写出部分字节后返回错误。
- 后果：`Write`/`WriteLine`/`WriteFormat` 等全部 public API 返回 `void`，调用方无法感知输出丢失或截断；程序继续执行并可能报告成功。
- 最小修复：让 public 写入 API 返回 `int64`（已写字节数或负 errno），或至少提供 `TryWrite` 系列；文档明确失败不可见。
- 备注：`write_raw` 本身返回 `count`，但所有调用点都丢弃了返回值。

##### [低] 70-2｜write_raw 对非正长度静默返回 0
- 位置：`Re.KrtC/stdlib/Console.krt:11-13`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`length` 为负（私有辅助函数，当前调用点均为非负，但 API 本身无保护）。
- 后果：负长度被当作"写 0 字节成功"，与调用方预期不符，且无任何诊断。
- 最小修复：区分 `length == 0`（返回 0）与 `length < 0`（返回负值或断言）。
- 备注：当前 `WriteFormat` 的 `index - literal_start`、`length - literal_start` 恒非负，故为潜在而非现网触发。

##### [低] 70-3｜EINTR 重试无上限
- 位置：`Re.KrtC/stdlib/Console.krt:17-19`
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：描述符持续返回 `-4`（EINTR），例如安装了无 SA_RESTART 的信号处理且信号风暴。
- 后果：`while (written < length)` 永久空转，进程挂起且无超时/重试计数。
- 最小修复：加入重试上限或退避，超限后返回已写字节数或负错误码。
- 备注：常规单次 EINTR 重试是正确的，问题在于无终止条件。

##### [中] 70-4｜WriteFormat(null) 与 WriteLineFormat(null) 行为分叉
- 位置：`Re.KrtC/stdlib/Console.krt:125-127`、`Re.KrtC/stdlib/Console.krt:149-152`
- 类别：1 静默失败 / 9 错误处理不一致 / 13 注释与实现脱节
- 触发条件：`format == null`。
- 后果：`WriteFormat(null, arg)` 什么都不输出；`WriteLineFormat(null, arg)` 先走 `WriteFormat`（无输出）再 `WriteLine()`，输出一个换行。同族 API 对同一非法输入给出不同可见行为，且注释未说明 null 语义。
- 最小修复：统一为"null 即无输出"或"null 视为空串"，并同步注释。
- 备注：`WriteFormat` 的 null 分支无日志/断言。

##### [低] 70-5｜STDIN 常量无任何读取实现
- 位置：`Re.KrtC/stdlib/Console.krt:8`
- 类别：5 死代码 / 12 测试不足
- 触发条件：调用方使用 `Console.STDIN` 期望存在输入 API。
- 后果：整个 stdlib 中 `STDIN` 仅此一处声明，没有任何 `Read*` 方法消费它，属未完成的公开 API；`STDERR` 同样只声明未使用（`Write` 系列硬编码 `STDOUT`）。
- 最小修复：补充 `Read`/`ReadLine` 与基于 `STDERR` 的写入，或删除未实现常量并在文档中标注。
- 备注：已 grep 整个 `Re.KrtC/stdlib`，`STDIN`/`Read` 无其他命中。

##### [中] 70-6｜ToInt64 不接受 '+'，与 TryToInt64 语义分叉
- 位置：`Re.KrtC/stdlib/Convert.krt:22`（对比 `Re.KrtC/stdlib/Convert.krt:67`）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：输入形如 `"+5"`。
- 后果：`ToInt64("+5")` 在 `characters[0] == '-'` 判断后直接把 `'+'` 当非数字 `break`，返回 0；而 `TryToInt64("+5")` 接受 `'+'` 并返回 5。同一库内有符号解析规则不一致，调用方难以预期。
- 最小修复：`ToInt64` 同时接受 `'+'`/`'-'`，与 `TryToInt64` 对齐；或明确文档化差异。
- 备注：`ToInt32` 经由 `ToInt64`，同样受影响。

##### [中] 70-7｜ToInt64/ToInt32 溢出静默回绕
- 位置：`Re.KrtC/stdlib/Convert.krt:31-34`、`Re.KrtC/stdlib/Convert.krt:7-9`
- 类别：1 静默失败 / 8 未检查前置条件 / 2 诊断缺失
- 触发条件：数字串超出 int64/int32 范围，如 `"99999999999999999999"`。
- 后果：`result = result * 10 + digit` 回绕，函数仍返回一个"看起来合法"的值，调用方无法区分溢出与正常解析；`ToInt32` 还叠加一次 int64→int32 截断。
- 最小修复：改用 `TryToInt64` 的负数累加上限检查，或让 `ToInt64` 在溢出时返回 0/抛异常并文档化。
- 备注：注释已写"wrapping arithmetic"，但 public API 无失败信号，仍是静默错误。

##### [低] 70-8｜ToInt64 长度截断为 int32 未设上限
- 位置：`Re.KrtC/stdlib/Convert.krt:17`
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：字符串字节长度超过 `2147483647`。
- 后果：`(int32)Sys.InternalStringLength(value)` 回绕为负数或错误值，`while (index < length)` 直接不执行，返回 0，解析结果错误且无诊断。
- 最小修复：仿照 `TryToInt64:60`，先判断 `full_length > 2147483647` 再截断。
- 备注：`TryToInt64`/`TryToUInt64` 均有该上限检查，`ToInt64` 缺失。

##### [致命] 70-9｜ToString 重载所有权不一致，统一释放会导致非法 munmap
- 位置：`Re.KrtC/stdlib/Convert.krt:150-172`
- 类别：6 契约破坏 / 10 资源与生命周期 / 9 错误处理不一致
- 触发条件：调用方按数值重载注释的统一约定，对 `Convert.ToString(bool)` 的返回值调用 `Memory.Free(result, StringOps.Length(result) + 1)`。
- 后果：数值重载返回 `Memory.Allocate`（mmap）所得堆串，必须释放；`ToString(bool)` 返回静态字面量 `"True"`/`"False"`，不可释放。对字面量地址调用 `Memory.Free` 时，若该地址恰好页对齐，`Memory.TryFree:52-55` 会通过页对齐检查并执行 `munmap`，可能解除可执行文件数据页的映射，导致后续访问崩溃；若未对齐则静默返回 false。
- 最小修复：统一所有权语义（全部返回堆分配，或全部返回字面量），或在 API 命名/文档中强制区分，并禁止对 bool 结果调用 `Memory.Free`。
- 备注：`ToString(bool):169-172` 已注明"borrowed boolean literal that must not be freed"，但与同名其他重载的所有权约定冲突，属接口级陷阱。

##### [中] 70-10｜InternalStringLength 无界扫描 NUL，非终止串越界读
- 位置：`Re.KrtC/stdlib/Sys.krt:28-35`
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：传入非 NUL 结尾的字节缓冲（如外部内存、手工构造的 `string`、子范围指针）。
- 后果：`while (data[length] != 0) length++;` 会一直读出分配边界，直到撞到某个 0 字节或段错误；函数无长度参数，调用方无法前置校验。
- 最小修复：增加接受显式长度的重载，或在使用契约中强制 NUL 结尾并对来源做校验。
- 备注：文件头注释仅声明"Strings are zero-terminated byte sequences"，未提供检测手段。

##### [低] 70-11｜InternalInt32ToString 与 StringOps.FromInt32/FromInt64 重复实现
- 位置：`Re.KrtC/stdlib/Sys.krt:39-70`
- 类别：5 死代码 / 11 配置漂移
- 触发条件：任一 int32→十进制串需求。
- 后果：同一功能存在两套实现，分配路径不同（此处直接 `syscall(SYS_mmap,...)`，`StringOps` 走 `Memory.Allocate`），缓冲区尺寸与失败处理各自维护；后续修改其一易造成行为漂移（如 `StringOps` 已处理 int64 最小值，此处只处理 int32）。
- 最小修复：让 `InternalInt32ToString` 直接转发 `StringOps.FromInt32`，或明确其独立契约并加一致性测试。
- 备注：`Sys.krt:39` 与 `String.krt:69-81` 功能重叠。

##### [低] 70-12｜Syscall 对调用号与参数不做任何校验
- 位置：`Re.KrtC/stdlib/Sys.krt:13-16`
- 类别：8 未检查前置条件 / 2 诊断缺失
- 触发条件：传入非法系统调用号或错误参数个数/类型。
- 后果：内核返回 `-ENOSYS` 等负 errno，函数原样返回；调用方若按正数解释将得到无意义结果，且无诊断。
- 最小修复：至少对已知常量范围做断言，或在文档中明确"原语，调用方自负"。
- 备注：作为底层原语可接受，但 public 暴露后缺少使用约束说明。

##### [中] 70-13｜Free 丢弃 TryFree 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:39-41`
- 类别：1 静默失败 / 9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`TryFree` 返回 false（非法范围、未页对齐、munmap 失败）。
- 后果：`Free` 返回 void，调用方无法知道释放失败，内存持续泄漏且无告警。
- 最小修复：让 `Free` 返回 `bool`，或提供失败日志/断言。
- 备注：与 `Copy`/`Move`/`Set`/`Clear` 是同一模式，见 70-14～70-17。

##### [中] 70-14｜Copy 丢弃 TryCopy 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:60-62`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`length` 为负、范围为空指针或回绕。
- 后果：拷贝被跳过但调用方无感知，数据保持未初始化/旧值，可能被当作已拷贝使用。
- 最小修复：返回 `bool` 或断言。
- 备注：`TryCopy` 本身正确返回 false。

##### [中] 70-15｜Move 丢弃 TryMove 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:70-72`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：同 70-14（负长度、null 范围、回绕）。
- 后果：重叠搬移被静默跳过，目标区保留错误内容。
- 最小修复：返回 `bool` 或断言。
- 备注：`TryMove` 的重叠方向处理本身正确。

##### [中] 70-16｜Set 丢弃 TrySet 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:103-105`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`length` 为负或目标为 null（非零长度）。
- 后果：填充被静默跳过，缓冲区内容不符合预期。
- 最小修复：返回 `bool` 或断言。
- 备注：`Set` 先把 `char` 截为 `byte`，再丢失状态。

##### [中] 70-17｜Clear 丢弃 TryClear 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:124-126`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`length` 为负或目标为 null（非零长度）。
- 后果：清零被静默跳过，敏感数据可能残留（如密钥缓冲未清）。
- 最小修复：返回 `bool` 或断言。
- 备注：`TryClear` 转发 `TrySet`，状态在 `Clear` 处丢失。

##### [低] 70-18｜Copy/Move/TryCopy 与 TryMove 完全等价，重复 API
- 位置：`Re.KrtC/stdlib/Memory.krt:60-72`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：调用方在 `Copy` 与 `Move` 之间选择。
- 后果：`TryCopy` 直接 `return TryMove(...)`，`Copy` 与 `Move` 语义、实现完全相同，仅名称不同；两套名字增加维护面与误用概率（调用方可能以为 `Copy` 不支持重叠）。
- 最小修复：合并为单一 `Move`/`TryMove`，或让 `Copy` 明确非重叠语义并做前置检查。
- 备注：注释称两者都"with overlap support"，证实为重复。

##### [中] 70-19｜魔法上限 9223372036854771712 重复出现且注释未解释
- 位置：`Re.KrtC/stdlib/Memory.krt:11`、`Re.KrtC/stdlib/Memory.krt:46`
- 类别：13 注释与实现脱节 / 11 配置漂移
- 触发条件：阅读或维护 `Allocate`/`TryFree` 的尺寸上限。
- 后果：该值为 `2^63 - 4096`，两处各自硬编码；注释只写"overflow"，未说明推导依据。若任一处调整而另一处未同步，`Allocate` 成功而 `TryFree` 拒绝（或反之），造成静默释放失败。
- 最小修复：提取为命名常量（如 `MAX_MMAP_SIZE`）并注释推导过程。
- 备注：`Allocate` 用 `<=0 || >上限`，`TryFree` 用 `>上限`，语义不完全对称。

##### [中] 70-20｜Memory 自定义 SYS_mmap/SYS_munmap，与 Sys.krt 重复
- 位置：`Re.KrtC/stdlib/Memory.krt:5-6`（对比 `Re.KrtC/stdlib/Sys.krt:7-8`）
- 类别：11 配置漂移 / 5 死代码
- 触发条件：平台系统调用号变更或修正时。
- 后果：同一常量在两文件各自定义，`Memory` 已 `using System.Sys` 却不复用 `Sys.SYS_mmap`/`SYS_munmap`；一旦只改一处即产生漂移，导致 mmap/munmap 用错号。
- 最小修复：删除 `Memory` 内常量，改用 `Sys.SYS_mmap`/`Sys.SYS_munmap`。
- 备注：`Memory` 其余逻辑已通过 `Sys.Syscall` 调用，常量却另立一份。

##### [中] 70-21｜is_valid_range 对零长度直接放行，与 TryMove 注释矛盾
- 位置：`Re.KrtC/stdlib/Memory.krt:27-29`（对比 `Re.KrtC/stdlib/Memory.krt:74`）
- 类别：13 注释与实现脱节 / 6 契约破坏
- 触发条件：`length == 0` 且 `address == null`（或任意非法地址）。
- 后果：`is_valid_range` 在 null 检查之前返回 true，因此 `TrySet(null,v,0)`、`TryMove(null,null,0)`、`TryFree(null,0)` 均返回 true；而 `TryMove` 注释声称"null ranges ... fail"。契约描述与实际行为不符，调用方可能据此写出错误的前置判断。
- 最小修复：统一契约——要么把 `length == 0` 放在 null 检查之后，要么修改注释为"零长度恒成功（含 null 地址）"。
- 备注：`TryFree:49-51` 也会对 `size == 0` 直接返回 true。

##### [低] 70-22｜TryFree 的页对齐前置条件未在 Allocate 契约中说明
- 位置：`Re.KrtC/stdlib/Memory.krt:52-54`（对比 `Re.KrtC/stdlib/Memory.krt:8`）
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：调用方对 `Allocate` 返回指针做偏移后调用 `Free`，或释放非 `Allocate` 来源的地址。
- 后果：`TryFree` 静默返回 false，内存未释放；`Allocate` 注释只说"Release an Allocate result"，未强调必须释放原始基址、不可偏移。
- 最小修复：在 `Allocate`/`Free` 注释中明确"必须释放原始基址，禁止偏移；地址必须页对齐"。
- 备注：`Free` 丢弃该 false（见 70-13），使契约违反完全不可见。

##### [低] 70-23｜TryFree 不校验 size 与原始分配一致，部分 unmap 泄漏
- 位置：`Re.KrtC/stdlib/Memory.krt:44-57`
- 类别：6 契约破坏 / 10 资源与生命周期
- 触发条件：传入小于原始分配块的 `size`。
- 后果：`munmap(addr, size)` 只解除按 size 向上取整的部分页，剩余页仍映射，形成泄漏；函数仍返回 true。
- 最小修复：文档强制"size 必须与 Allocate 入参完全一致"，或让 `Allocate` 记录元数据以便校验。
- 备注：`Allocate` 未返回容量元数据，调用方只能靠自觉。

##### [低] 70-24｜Compare 以 -2 为无效哨兵，与 Try* 的 bool 约定不一致
- 位置：`Re.KrtC/stdlib/Memory.krt:133-153`
- 类别：9 错误处理不一致 / 6 契约破坏
- 触发条件：调用方把 `Compare` 返回值当布尔或与非负比较混用。
- 后果：有效返回值域为 `-1/0/1`，无效为 `-2`，与同文件 `Try*` 系列的 `bool` 失败约定不同；`-2` 与合法负值相邻，易被误判为"left < right"。
- 最小修复：改为 `bool TryCompare(...)` 或返回三态枚举，并在注释中给出判别表。
- 备注：注释已写明 -2，但 API 形态与文件内其他错误报告方式不统一。

##### [中] 70-25｜Exception 只带 Message/Code，无堆栈、内部异常与来源
- 位置：`Re.KrtC/stdlib/Exception.krt:4-20`
- 类别：2 诊断缺失 / 12 测试不足
- 触发条件：捕获异常后定位根因。
- 后果：payload 仅有 `Message` 与 `Code`，无调用栈、无 `InnerException`、无抛出点/线程信息；`catch` 后无法还原因果链，复杂程序几乎无法定位。
- 最小修复：增加 `InnerException`、抛出位置（文件/行/函数）或堆栈字段，并定义 `Code` 的取值规范。
- 备注：文件注释称"catch 收到同一对象"，但未说明任何诊断信息载体。

##### [低] 70-26｜Message/Code 为 public 可变字段，异常状态可被篡改
- 位置：`Re.KrtC/stdlib/Exception.krt:5-6`、`Re.KrtC/stdlib/Exception.krt:17-19`
- 类别：7 共享可变状态 / 6 契约破坏
- 触发条件：`catch` 中或捕获后修改 `e.Message`/`e.Code`。
- 后果：异常对象被多个处理层级共享时，后置处理读到被篡改的消息/错误码；`ToString()` 依赖可变的 `Message`，输出不稳定。
- 最小修复：改为只读属性（构造后不可变），或至少提供只读访问器。
- 备注：注释明确"catch 收到同一对象"，故共享可变性会被实际放大。

##### [低] 70-27｜Code 字段全库无读取方，单参构造不赋值
- 位置：`Re.KrtC/stdlib/Exception.krt:6`、`Re.KrtC/stdlib/Exception.krt:8-10`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：任何按 `Code` 分支的错误处理。
- 后果：`Code` 仅被双参构造写入，全 stdlib 无任何读取点；单参构造留下默认 0，调用方无法区分"未设置"与"错误码 0"。字段看似公开契约，实际未被使用。
- 最小修复：删除 `Code` 或在使用方（运行时/标准库）中消费它，并定义 0 的语义。
- 备注：已 grep `Re.KrtC/stdlib`，`Code` 仅出现在赋值与声明处。

##### [低] 70-28｜ToString() 对 null Message 返回 null
- 位置：`Re.KrtC/stdlib/Exception.krt:17-19`
- 类别：1 静默失败 / 2 诊断缺失 / 13 注释与实现脱节
- 触发条件：`new Exception(null)` 后调用 `ToString()`，或捕获到 Message 为 null 的异常再打印。
- 后果：返回 null，调用方（如日志/Console）可能输出空或触发后续空引用；无默认文本、无诊断。
- 最小修复：Message 为 null 时返回固定文本（如 `"Exception"`）或空串，并文档化。
- 备注：构造函数未拒绝 null message，故该路径可达。

### 第七十一批：src/Runtime/Krt/{String,StringBuilder,Console,Convert}.krt

#### 覆盖范围

- 全量读取（read 工具分段读毕，非抽样）：
  - `Re.KrtC/src/Runtime/Krt/String.krt`（303 行，全文读完）
  - `Re.KrtC/src/Runtime/Krt/StringBuilder.krt`（222 行，全文读完）
  - `Re.KrtC/src/Runtime/Krt/Console.krt`（152 行，全文读完）
  - `Re.KrtC/src/Runtime/Krt/Convert.krt`（90 行，全文读完）
- 同名副本存在性已确认：`Re.KrtC/stdlib/{String,StringBuilder,Console,Convert}.krt` 与 `libs/System/{String,StringBuilder,Console,Convert}.krt` 均有同名文件。
- **副本比对未完成**：本环境 `bash` 因无可用沙箱后端被拒（workspace-write 不可用），`mcp__phone__android_shell` 返回 `disabled`，无法执行 `diff -q`。因此「runtime 实现 vs 标准库封装」的逐字同步性未验证，仅能从本批文件内部的语义分叉（类别 3/4/11）记录。副本不同步风险需在具备 shell 的环境补做。
- 未读取上述四个文件以外的任何源文件内容（遵守审计边界）。


##### [致命] 71-1｜FromInt64 对 int64.MinValue 返回 "-"
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:100-104`、`115-137`
- 类别：8 未检查前置条件（数值溢出）＋ 1 静默失败
- 触发条件：调用 `StringOps.FromInt64(-9223372036854775808)`，或 `Console.WriteLine(int64.MinValue)`（`Console.krt:48-52` 经此路径）。
- 后果：`v = -v` 对最小负数溢出后仍为负，`while (temp > 0)` 得 `digits = 0`，最终只写入 `'-'` 与 `'\0'`，返回字符串 `"-"`。输出错误且无任何诊断；`Console.WriteLine(int64.MinValue)` 打印 `-`。
- 最小修复：在取负前对 `value == -9223372036854775808` 特判（返回常量 `"-9223372036854775808"`），或改用无符号幅值 `uint64` 迭代。
- 备注：`FromInt32` 无此问题（委托内置实现），见 71-9 的分叉。

##### [高] 71-2｜Concat 未检查 Memory.Allocate 返回值
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:29-41`
- 类别：10 资源与生命周期 ＋ 8 未检查前置条件
- 触发条件：`totalLen + 1` 较大或堆耗尽，`Memory.Allocate` 返回 0/负值。
- 后果：未判空即 `Memory.Copy((void*)buf, ...)` 与 `arr[totalLen] = '\0'`，向空指针写入，崩溃或静默内存破坏。
- 最小修复：分配后检查 `buf == 0`（或 `< 0`），失败时返回 `null`/触发 OOM 路径，并明确契约。
- 备注：同类问题见 71-3、71-18、71-20。

##### [高] 71-3｜ConcatMultiple 未检查 Memory.Allocate 返回值
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:62-82`
- 类别：10 资源与生命周期 ＋ 1 静默失败
- 触发条件：`parts` 中字符串总长度较大或堆耗尽。
- 后果：未判空即复制并写 `arr[totalLen]`，空指针写入；无诊断。
- 最小修复：同 71-2，分配失败显式返回 `null` 或抛出/上报。
- 备注：与 71-2 是不同函数，独立记录。

##### [高] 71-4｜Concat 总长度 int32 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:24-29`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`lenA + lenB > 2147483647`（两个接近上限的字符串）。
- 后果：`totalLen` 回绕为负或变小，`totalLen + 1` 分配不足，随后按真实长度复制导致堆越界写。
- 最小修复：用 `int64` 累加并检查上限后再分配，或明确拒绝超长输入。
- 备注：与 71-2 的「未判空」是不同缺陷（一个是溢出、一个是判空）。

##### [高] 71-5｜ConcatMultiple 总长度 int32 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:50-62`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`parts` 中多个字符串长度之和超过 `int32` 上限。
- 后果：`totalLen` 回绕，分配 `totalLen + 1` 偏小，循环复制越界写。
- 最小修复：累加用 `int64`，溢出即失败。
- 备注：与 71-4 独立（不同函数）。

##### [高] 71-6｜Substring(start,length) 的 start+length 溢出导致越界拷贝
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:240-243`
- 类别：8 未检查前置条件（溢出/NULL/越界）
- 触发条件：`Substring(s, start, length)` 中 `length` 为大正数（如 `int32.MaxValue`）且 `start > 0`，`start + length` 回绕为负。
- 后果：`if (start + length > len)` 判定失效，`length` 不被收敛，`Memory.Copy` 按巨大长度读取/写入，堆越界。
- 最小修复：改为 `if (length > len - start) length = len - start;`，并单独校验 `start`/`length` 非负。
- 备注：`len - start` 在 `start < len` 时已保证非负（238 行已拦截 `start >= len`）。

##### [中] 71-7｜Length(null) 未做前置检查，与同类函数 null 处理不一致
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:13-16`
- 类别：8 未检查前置条件 ＋ 9 错误处理不一致
- 触发条件：`StringOps.Length(null)`（`Convert`、`Console` 均经由它间接调用）。
- 后果：直接调用 `Sys.InternalStringLength(null)`，行为未定义/崩溃；而 `IndexOf`（185）、`LastIndexOf`（204）、`Compare`（148-150）都显式处理了 null。
- 最小修复：`Length` 内对 `s == null` 返回 0 或明确抛错，并统一全文件 null 契约。
- 备注：`Convert.ToInt32` 自身先判 null（`Convert.krt:13`），但其他调用方不受保护。

##### [中] 71-8｜Substring(null,...) 返回 null，与空串返回 "" 契约不一致
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:225`、`234`
- 类别：1 静默失败 ＋ 9 错误处理不一致
- 触发条件：`Substring(null, 0)` 与 `Substring("", 0)` 对比。
- 后果：前者返回 `null`，后者返回 `""`；调用方若假设「越界即空串」会对 null 解引用。两种失败（null 输入、越界）返回不同哨兵，语义未声明。
- 最小修复：统一返回 `""`（或统一返回 `null`），并在注释中写明契约。
- 备注：`Substring(s, start)` 与 `Substring(s, start, length)` 两处行为一致地返回 null，但与其他函数不一致。

##### [中] 71-9｜FromInt32 委托内置、FromInt64 自行实现，阶段/语义分叉
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:88-91` 对比 `94-140`
- 类别：3 语义分叉 ＋ 4 阶段不对齐 ＋ 5 死代码风险
- 触发条件：任何 `FromInt32` 调用；或对比 `FromInt32` 与 `FromInt64` 的边界行为。
- 后果：同一类中 int32 走 `Sys.InternalInt32ToString`（黑盒，边界/负数行为未知），int64 走纯 .krt 实现（存在 71-1 缺陷）。两者对 `MinValue`、`0`、格式的处理可能不一致，且维护时易只改一处。
- 最小修复：统一实现路径（都委托内置，或都自实现并共用同一算法），并让 `FromInt32` 复用 `FromInt64` 后截断/校验。
- 备注：`Console.Write(int32)`（`Console.krt:36-40`）走内置路径，`Console.Write(int64)` 走自实现路径，输出行为可能不同。

##### [中] 71-10｜Compare 以 len1-len2 返回，存在溢出且语义未定义
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:171`
- 类别：8 未检查前置条件（溢出）＋ 13 注释与实现脱节
- 触发条件：两字符串长度差超过 `int32` 上限（极端长串），或长度相等前缀但后续不同。
- 后果：`len1 - len2` 回绕，返回值符号与注释「<0 = s1 < s2」矛盾；调用方按符号判断会误判大小。
- 最小修复：返回 `len1 < len2 ? -1 : 1`，避免减法溢出。
- 备注：常规路径（字符不等）返回 `(int32)arr1[i] - (int32)arr2[i]` 同样可能溢出，但字符差值范围有限，风险低。

##### [中] 71-11｜字节分配与 char[] 索引混用，编码/ABI 约定未在本文件确立
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:29,38-39,62,79-80,116,242,245-246`
- 类别：6 契约破坏（C 运行时 ABI/编码约定）
- 触发条件：若 `char` 为 2 字节（宽字符/UTF-16 存储），而 `Memory.Allocate(totalLen + 1)` 按「字符数 + 1」分配字节。
- 后果：`arr[totalLen] = '\0'` 会写到第 `2*totalLen` 字节，越过 `totalLen + 1` 的分配边界（堆越界写）；同时 `Memory.Copy(..., lenA)` 的字节长度与 `char[]` 元素宽度不匹配。
- 最小修复：在文件头或 ABI 约定中明确 `char` 宽度与字符串编码；分配按 `sizeof(char) * (len + 1)`，或统一使用 `Sys` 提供的字符串构造接口。
- 备注：若 `char` 确为 1 字节且字符串为 UTF-8，则本条不成立；但四个文件均未写明该约定，属「契约未确立」，需与编译器 ABI 核对。

##### [致命] 71-12｜AppendLine 部分写入后返回 -1，状态与返回值不一致
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:84-89`
- 类别：9 错误处理不一致 ＋ 1 静默失败
- 触发条件：缓冲区剩余容量足够容纳 `value` 但不够再容纳 `'\n'`（`AppendString` 成功、`AppendChar` 返回 -1）。
- 后果：`value` 的字符已写入缓冲区，但函数返回 `-1`；调用方若按「-1 = 未写入」处理会重复追加，若忽略则丢换行。缓冲区内容与调用方认知的长度不一致。
- 最小修复：先检查 `currentLength + valueLen + 1 <= bufferSize` 再写入；或返回实际新长度并单独标记失败。
- 备注：与 71-13（仅返回 -1 不报因）独立。

##### [高] 71-13｜AppendString 容量不足静默返回 -1，无诊断
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:37-38`
- 类别：1 静默失败 ＋ 2 诊断缺失
- 触发条件：`currentLength + valueLen > bufferSize`。
- 后果：数据未追加，仅返回 `-1`；调用方若不检查（常见）则静默丢数据，且无法区分「容量不足」与「其他错误」。
- 最小修复：提供 `bool TryAppend` 或抛出/上报容量不足；至少在注释与调用约定中强制检查返回值。
- 备注：`Insert`（107）有相同模式。

##### [高] 71-14｜AppendString 的 currentLength + valueLen 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:37`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`currentLength` 与 `valueLen` 均接近 `int32` 上限，相加回绕为负。
- 后果：`> bufferSize` 判定失效，容量检查被绕过，循环写 `dst[currentLength + i]` 越界。
- 最小修复：用 `int64` 计算或先判 `valueLen > bufferSize - currentLength`。
- 备注：与 71-13 独立（溢出 vs 静默返回）。

##### [高] 71-15｜Insert 的 currentLength + valueLen 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:107`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`currentLength + valueLen` 回绕。
- 后果：容量检查失效，随后 `buf[i + valueLen] = buf[i]`（114）与 `buf[index + i]`（124）越界读写。
- 最小修复：同 71-14，改用减法比较或 `int64`。
- 备注：与 71-14 独立（不同函数）。

##### [高] 71-16｜IndexOf/LastIndexOf/Replace 无 buffer 与 length 前置检查
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:182-189,195-203,208-219`
- 类别：8 未检查前置条件（NULL/越界）
- 触发条件：`buffer == 0`，或 `length` 大于实际分配长度（调用方传入错误）。
- 后果：直接 `char[] buf = (char[])buffer` 后按 `length` 读写，空指针解引用或越界读；`Replace` 还会越界写。
- 最小修复：入口检查 `buffer != 0 && length >= 0`，并由调用方/结构体保证 `length <= 容量`；或把长度与容量绑定在同一对象中。
- 备注：`AppendString` 有 `bufferSize` 参数，但这三个函数没有，契约不统一。

##### [高] 71-17｜Remove 的 index + length 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:136`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`index` 与 `length` 均较大，`index + length` 回绕为负。
- 后果：`if (index + length > currentLength)` 判定失效，`length` 不收敛，随后 `buf[i - length] = buf[i]`（145）中 `i - length` 下溢/越界写。
- 最小修复：改为 `if (length > currentLength - index) length = currentLength - index;`。
- 备注：与 71-6 是同型缺陷在不同文件，独立记录。

##### [中] 71-18｜AllocateBuffer 未检查 mmap 返回值，且 capacity+1 可溢出/为负
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:15-19`
- 类别：8 未检查前置条件 ＋ 10 资源与生命周期
- 触发条件：`capacity` 为负或接近 `int32` 上限（`capacity + 1` 溢出/为负），或 mmap 失败返回负值。
- 后果：返回未校验的 syscall 结果；调用方拿到无效指针后写入，崩溃或静默破坏；`capacity` 为负时 `(int64)(capacity + 1)` 申请 0/负长度。
- 最小修复：检查 `capacity >= 0` 且 `capacity < int32.MaxValue`，并检查返回值 `< 0` 时报错。
- 备注：`FreeBuffer`（22-25）用同一表达式，负 capacity 同样失真。

##### [中] 71-19｜FreeBuffer 长度依赖调用方回传，生命周期契约脆弱
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:22-25`
- 类别：10 资源与生命周期 ＋ 7 共享可变状态
- 触发条件：调用方传入与分配时不同的 `capacity`（如扩容后忘记更新，或结构体字段被复制）。
- 后果：`munmap(buffer, capacity + 1)` 长度与分配长度不符，返回 EINVAL 或只解除部分映射，造成内存泄漏；且缓冲区指针可被多处持有（裸 `int64` 传递），释放后其他持有者悬垂。
- 最小修复：把指针、容量、长度封装为带所有权的对象，或用头部记录分配长度，`Free` 时读取真实长度；禁止裸指针跨函数传递。
- 备注：`BufferToString`（164）新分配的字符串无任何释放接口，同一所有权缺口。

##### [中] 71-20｜BufferToString 未检查分配返回值且 length 无界
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:159-177`
- 类别：8 未检查前置条件 ＋ 10 资源与生命周期
- 触发条件：mmap 失败，或调用方传入超过实际缓冲区长度的 `length`。
- 后果：未判空即 `dst[i] = src[i]` 与 `dst[length] = '\0'`，空指针写；`length` 过大则读越界。
- 最小修复：检查 `newBuf` 返回值，并校验 `length` 与源缓冲区容量的关系。
- 备注：`length == 0` 时返回 `""` 字面量而非新分配，与其他路径返回堆指针的所有权语义不一致。

##### [中] 71-21｜与 String.krt 的内存分配器分叉
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:15-25,164` 对比 `String.krt:29,62,116,242`
- 类别：3 语义分叉 ＋ 11 配置漂移
- 触发条件：同一 runtime 内混用两套分配方式（本文件裸 `Sys.syscall(9,...)`，String.krt 用 `Memory.Allocate`）。
- 后果：分配/释放路径不统一，`Memory.Allocate` 的包装（对齐、统计、失败处理）被绕过；后续替换分配器或开启内存追踪时本文件成为盲区。
- 最小修复：统一走 `Memory.Allocate`/`Memory.Free`，或在注释中说明为何必须绕过（并补错误处理）。
- 备注：`Memory.krt` 存在于 stdlib/libs，说明存在统一封装可用。

##### [中] 71-22｜Clear 忽略参数、不清空缓冲区，注释与实现脱节
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:153-156`
- 类别：5 死代码 ＋ 13 注释与实现脱节
- 触发条件：调用 `Clear(currentLength)`。
- 后果：参数 `currentLength` 完全未使用；函数只 `return 0`，既不重置缓冲区内容也不做任何清理，但注释称「清空缓冲区（重置长度）」。调用方若以为缓冲区已清零，后续 `BufferToString` 在长度未正确维护时会读到旧数据。
- 最小修复：要么真正把缓冲区前 `currentLength` 字节清零并返回 0，要么删除该死参数并明确「仅返回新长度」的契约。
- 备注：这是纯函数式 API 的产物，与 71-23 的无状态设计同源。

##### [中] 71-23｜无实例状态与标准成员，API 与标准库 StringBuilder 语义分叉
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:11-221`（全类）
- 类别：3 语义分叉 ＋ 13 注释与实现脱节
- 触发条件：任何按标准 `StringBuilder` 语义（`new`、`Append` 返回自身、`Length`/`Capacity` 属性、`ToString()`）使用的代码。
- 后果：本类是纯静态函数集，无实例、无状态、无 `Length`/`Capacity`/`ToString` 成员（只有 `BufferToString`）；调用方必须自行维护 `buffer/currentLength`，易错且与 `System.Text.StringBuilder` 或标准库封装的契约不同。
- 最小修复：改为实例类封装 `buffer/capacity/length`，或明确重命名为 `StringBuilderOps` 并在文档中声明过程式契约。
- 备注：`stdlib/StringBuilder.krt` 与 `libs/System/StringBuilder.krt` 存在同名副本，其语义是否一致未验证（见覆盖范围）。

##### [致命] 71-24｜Write(char)/WriteLine(char) 输出十进制码点而非字符
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:78-87`
- 类别：3 语义分叉 ＋ 13 注释与实现脱节
- 触发条件：`Console.Write('A')` 或 `Console.WriteLine('，')` 等字符输出。
- 后果：`Write((int32)value)` 把字符码点当整数打印，输出 `65` 而非 `A`；与 .NET `Console.Write(char)` 语义直接冲突，且无任何提示。
- 最小修复：实现真正的字符写入（按编码把该字符转为字节序列后 `WriteRaw`），或删除 char 重载强制显式转换。
- 备注：`Write(bool)` 输出 `"True"/"False"` 与 .NET 一致，唯独 char 分叉。

##### [高] 71-25｜WriteRaw 忽略 write 返回值，短写静默截断
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:9-12,22,33,106,141`
- 类别：1 静默失败 ＋ 9 错误处理不一致
- 触发条件：写管道/终端/文件时 `write(2)` 部分写（信号中断、非阻塞 fd、管道满），或 fd 无效返回 -1。
- 后果：返回值被丢弃，短写导致输出残缺且无诊断；`WriteFormat` 逐字符调用时每次短写都静默丢失一个字符。
- 最小修复：循环重试直到写完或返回错误；对 `EINTR` 重试，其他错误上报。
- 备注：`Write(string)` 把 `int64 len` 强转 `int32`（22），超长字符串还会截断长度。

##### [高] 71-26｜WriteFormat 静默丢弃未知占位符
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:127-138`
- 类别：1 静默失败 ＋ 2 诊断缺失
- 触发条件：格式串含 `{2}`、`{x}`、`{}` 等未支持占位符，或 `{{` 转义。
- 后果：`else if (arr[i+1]=='1')` 之后无 else 分支，但 `i = i + 3` 无条件执行——`{2}` 被整段吞掉且不输出、不报错；`{{` 被当作占位符解析，产生错乱输出（如 `"{{}}"` 输出 `"}"`）。
- 最小修复：仅识别 `{0}`/`{1}`，其余按字面输出（含 `{`）；或支持 `{{`/`}}` 转义并对接更多参数。
- 备注：单参版（99-109）只认 `{0}`，但同样会把 `{1}` 静默丢弃。

##### [高] 71-27｜WriteFormat 未检查 format 为 null
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:93-95,121-123`
- 类别：8 未检查前置条件（NULL）
- 触发条件：`Console.WriteFormat(null, "x")` 或 `WriteLineFormat(null, ...)`。
- 后果：直接 `Runtime.Sys.InternalStringLength(null)` 与 `InternalStringPtr(null)`，行为未定义/崩溃；`Write(string)` 反而对 null 做了静默返回（16-19），处理不一致。
- 最小修复：入口统一判 null（返回或报错），与 `Write` 的契约对齐。
- 备注：`arg0` 为 null 时不崩溃（`Write(null)` 静默返回），但参数被无声省略。

##### [中] 71-28｜Write(string) 对 null 静默返回，与 WriteLine(null) 叠加后语义含糊
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:14-19,25-29`
- 类别：1 静默失败 ＋ 9 错误处理不一致
- 触发条件：`Console.Write(null)` 或 `Console.WriteLine(null)`。
- 后果：`Write(null)` 什么都不输出且不报错；`WriteLine(null)` 则输出一个空行。与 `WriteFormat` 对 null 崩溃（71-27）行为不一致，调用方无法预期。
- 最小修复：统一 null 策略（建议输出空串或抛错），并在注释中写明。
- 备注：与 71-27 独立（一个是静默、一个是崩溃）。

##### [中] 71-29｜namespace 声明与同批文件不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:1`（对比 `String.krt:1`、`StringBuilder.krt:1`、`Convert.krt:1`）
- 类别：11 配置漂移
- 触发条件：编译期名称解析或按命名空间组织代码时。
- 后果：本文件为 `namespace System;`，其余三文件为 `namespace System.Runtime;`，同一 runtime 目录出现两套命名空间；`Console` 与 .NET 的 `System.Console` 巧合一致，但 `StringOps`/`Convert` 落在 `System.Runtime`，跨文件引用只能靠全限定名（如 `Runtime.StringOps`），易漂移。
- 最小修复：统一为 `System` 或 `System.Runtime`，并全批对齐。
- 备注：`Convert.krt:1` 的 `System.Runtime` 与 .NET `System.Convert` 亦不一致，见 71-35。

##### [中] 71-30｜STDIN/STDERR 常量与 Read 系列 API 缺失，成死代码
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:5-7`
- 类别：5 死代码 ＋ 12 测试不足
- 触发条件：任何需要读输入或写错误流的调用。
- 后果：`STDIN`、`STDERR` 定义后全文件无任何使用；无 `Read`/`ReadLine`/`Error` 等 API，常量与能力不匹配，易让调用方误以为可用。
- 最小修复：实现 Read/Error 系列，或删除未用常量并在文档中说明仅支持 stdout。
- 备注：`WriteRaw` 已支持任意 fd（9-12），具备扩展基础。

##### [中] 71-31｜Write(int64) 传播 FromInt64 的最小值缺陷
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:48-52`（根因见 `String.krt:100-104`）
- 类别：3 语义分叉 ＋ 8 未检查前置条件
- 触发条件：`Console.Write(int64.MinValue)` / `Console.WriteLine(int64.MinValue)`。
- 后果：经 `StringOps.FromInt64` 输出 `"-"`，与 `Console.Write(int32.MinValue)`（走内置路径）行为不可比，且无任何报错。
- 最小修复：修复 71-1 后本条自动消除；同时统一 int32/int64 的格式化路径（71-9）。
- 备注：这是 71-1 的跨文件传播点，独立记录以便追踪影响面。

##### [致命] 71-32｜ToInt32/ToInt64 溢出静默回绕，无溢出检测
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:33`、`64`
- 类别：8 未检查前置条件（数值解析/溢出）＋ 1 静默失败
- 触发条件：`Convert.ToInt32("2147483648")`、`Convert.ToInt32("999999999999")`、`Convert.ToInt64("9223372036854775808")` 等超范围输入。
- 后果：`result = result * 10 + digit` 直接回绕（如 `"2147483648"` 变 `-2147483648`），返回错误数值且无诊断；标准库语义应抛溢出异常，语义分叉。
- 最小修复：每次乘加前检查 `result > (max - digit) / 10`，溢出时抛错或返回失败标志。
- 备注：负数边界 `-2147483648`/`int64` 最小值因回绕巧合正确，但正边界不成立。

##### [高] 71-33｜非法/部分非法输入静默截断或返回 0，无法与合法 0 区分
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:29-38,60-69`
- 类别：1 静默失败 ＋ 2 诊断缺失
- 触发条件：`Convert.ToInt32("12abc")`、`"abc"`、`""`、`"  12"`、`"0x10"`。
- 后果：遇非数字即 `break`，返回已累积部分（`"12abc" → 12`）或 `0`；`"abc"` 与 `"0"` 都返回 0，调用方无法区分合法与非法输入。标准库 `int.Parse` 对非法输入抛 `FormatException`。
- 最小修复：解析结束后校验是否至少消费一个数字且无非法尾随；失败时抛错或返回 `bool TryParse` 模式。
- 备注：`null` 输入返回 0（13、44），同样无诊断。

##### [中] 71-34｜不支持前导 '+'、空白与十六进制，单独 "-" 返回 0
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:23-27,54-58`
- 类别：3 语义分叉 ＋ 13 注释与实现脱节
- 触发条件：`Convert.ToInt32("+12")`、`" 12"`、`"0x1F"`、`"-"`、`"-abc"`。
- 后果：仅识别前导 `'-'`，`'+'` 与空白被当作非法字符而 `break`，返回 0 或截断值；单独 `"-"` 返回 0（而非报错）。与标准 `int.Parse`（允许前导符号/空白）语义分叉。
- 最小修复：按标准解析规则处理 `+`/空白，并对「只有符号没有数字」显式报错。
- 备注：与 71-33 独立（字符集/格式支持 vs 失败静默）。

##### [中] 71-35｜Convert 命名空间与 .NET 及本批 Console 不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:1`
- 类别：11 配置漂移
- 触发条件：跨文件引用或对照标准库时。
- 后果：`Convert` 声明为 `System.Runtime.Convert`，而 .NET 为 `System.Convert`、本批 `Console.krt` 为 `System`，同批文件命名空间三处不一致（`System` / `System.Runtime`），增加引用漂移风险。
- 最小修复：统一命名空间约定（含 71-29）。
- 备注：`stdlib/Convert.krt`、`libs/System/Convert.krt` 副本的命名空间未验证。

##### [中] 71-36｜四个文件均无测试/断言，边界与错误路径无覆盖
- 位置：`String.krt:1-303`、`StringBuilder.krt:1-222`、`Console.krt:1-152`、`Convert.krt:1-90`（全批）
- 类别：12 测试不足
- 触发条件：回归验证或边界用例（MinValue、溢出、null、非法输入、容量边界）执行时。
- 后果：本批 30 余条缺陷（尤其 71-1、71-6、71-12、71-17、71-32）均属边界/错误路径，无测试支撑；后续修复无法防回归。
- 最小修复：为每个公开函数补最小用例集：正常值、空/null、边界值（MinValue/MaxValue）、溢出值、非法输入、容量恰好相等/差一。
- 备注：测试缺失是全局条目，不与其他条目合并。

### 第七十二批：SelfHost/Compiler/Middle/Ir/Ir.krt（1-1920）

#### 覆盖范围

- 文件：`SelfHost/Compiler/Middle/Ir/Ir.krt`，仅审计第 1-1920 行（Boxing lowering、Closure lowering、Exception lowering、Floating point lowering、Generator lowering、IR construction 的前半部分、`KrtIr*` 校验/常量折叠/字节缓冲 IR、`KrtNativeOp` 枚举、`KrtNativeInit`/`KrtNativeEmit`/`KrtNativeAppendData`/`KrtNativeLowerLogical`/`KrtNativeLowerLValue`/`KrtNativeLowerExpression` 等）。
- 交叉核对：仅按符号 grep / 定点读取，核对过 `KrtNativeFloatKind`（`Frontend/Semantic/Semantic.krt:2374`，已确认对 `node == null` 返回 0，故 521-522 行顺序无害）、`KrtNativePatchLoop`（本文件 2101，已确认 `head < 0` 安全）、`KrtNativeLowerDispatchTrap`（本文件 2678）、`KrtNativeRuntimeTransferPacket`/`KrtNativeLowerRuntimeReleasePacket`（本文件 2812/2824）。
- 未通读其他源文件；`Re.KrtC` 的 `Middle/Ir` 是 SSA 版 IR，与本文件这套栈式 IR 并非逐函数对应，无法逐行对照，相关结论已标「疑似」。


##### [高] 72-1｜KrtNativeEmit 在判空之前就解引用 module
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1613-1615`
- 类别：8 未检查前置条件（NULL）
- 触发条件：以 `module == null` 调用 `KrtNativeEmit`（`KrtNativeInit` 自己就允许 `module == null`，说明该入参被预期可能为空）。
- 后果：第 1614 行 `module.ssa = null;` 先对空指针写入，直接崩溃；第 1615 行的 `module == null` 判空永远执行不到，属于「判空写了但无效」。
- 最小修复：把 null 判断提到函数首行：`if (module == null) { return -1; }`，再执行 `module.ssa = null;`。
- 备注：无。

##### [高] 72-2｜KrtNativeLowerCreateCell 缺少分配失败陷阱，与同类分配点不一致
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:180-189`（对照 `:54`、`:2691`、`:2719`、`:2942`）
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`KrtNativeOp.Allocate` 返回 NULL（堆耗尽）。
- 后果：`StoreLocal declaration.native_cell_slot` 把 NULL 存入 cell 槽，紧接着 `KrtNativeLowerCellPointer` 取该 NULL 并 `Store 8` 写 `NULL+0`、`Address 8` 后写 `NULL+8`，造成空指针写或堆损坏；而 box 创建（`:54`）、dispatch（`:2691`）、new/数组（`:2719`、`:2942`）都在 `Allocate` 后接 `KrtNativeLowerDispatchTrap`，唯独 cell 创建没有。
- 最小修复：在 `:181` 的 `Allocate` 之后补 `Branch + KrtNativeLowerDispatchTrap`，与 `KrtNativeLowerBoxCreate` 的 `allocated` 写法一致。
- 备注：无。

##### [高] 72-3｜breaks/continues 固定 64 深度，嵌套过深即越界写
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:388`、`:945`、`:1010`（写入点 `:2224`、`:2437`、`:2598`；读取点 `:2243`、`:2448`、`:2608`）
- 类别：8 未检查前置条件（越界）
- 触发条件：函数体内循环/开关嵌套深度达到 64（`depth` 每次进入循环 `+1`，`Break`/`Continue` 用 `depth - 1` 索引）。
- 后果：`breaks[depth]` / `continues[depth]` 越界写，破坏相邻栈内存；无任何诊断。
- 最小修复：分配前检查 `depth < 64` 并返回 false（带诊断），或改用按深度增长的动态数组。
- 备注：无。

##### [高] 72-4｜KrtNativeLowerReleaseCellValue 结束时栈上仍留一个指针，PatternCell/TypeTest 路径未丢弃（疑似）
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:214-238`；调用点 `:242`、`:257`、`:326`、`:2152`
- 类别：6 契约破坏 / 10 资源与生命周期
- 触发条件：任何 cell 释放（捕获变量离开作用域、类型测试模式、闭包删除）。
- 后果：逐路径核对栈效果（`Branch` 的 `args` 为真目标，已由 `:46-47`、`:1776-1782`、`:2776-2777` 三处确认）：入口 `[P]`，三条出口（空指针走 `drop_start`、引用计数未清零走 `drop_start`、清零走 `free_start`）都终结为 `[P]`，即只「用掉」未「消耗」。`KrtNativeLowerReleaseCell`（`:240-245`）在 `StoreLocal` 后同样把 `P` 留在栈上；`KrtNativeLowerPatternCell`（`:257`）在 `KrtNativeLowerTypeTest` 这个**表达式**里直接返回，`TypeTest` 的布尔结果下方多出一个指针，后续 `Branch` 会取到指针而非布尔，造成条件误判/栈污染。
- 最小修复：明确契约。若应为「消耗」，在 `free_start`/`drop_start` 收尾处各补一次 `Drop`（或让 `ReleaseCellValue` 出口统一 `Drop`）；若确为「保留」，则 `KrtNativeLowerReleaseCell`、`KrtNativeLowerPatternCell`、`KrtNativeLowerDeleteClosure` 三处调用点必须显式 `Drop`。
- 备注：疑似——`Re.KrtC` 的 IR 是 SSA 版，无逐函数对应实现；需再确认 VM 是否在语句边界重置操作数栈（若是，`:2152`/`:326` 可容忍，但 `:257` 在表达式内部仍不可容忍）。

##### [中] 72-5｜KrtIrValidateFunction 未校验 results/immediates/types 的取值与部分数组非空
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1172-1225`（缺口在 `:1176`、`:1203`、`:1207-1212`）
- 类别：6 契约破坏
- 触发条件：构造出 `results == null`、`immediates == null`（且无 Branch）、或 `types[i]` 为非法枚举值的 `KrtIrFunction`。
- 后果：`:1176` 只检查 `opcodes/types/lefts` 非空，`results`、`immediates` 完全不看；`types` 取值从不校验（`:1210`、`:1211` 只比较相等）；`StoreLocal` 的 `rights`（本地槽号）既不检查非空也不检查范围。校验返回 true 后，后端/优化器按契约解引用即崩溃或静默错码。
- 最小修复：补 `results/immediates/rights` 非空检查、`types[i]` 枚举范围检查、`StoreLocal` 的 `rights[i]` 槽号范围检查。
- 备注：无。

##### [中] 72-6｜KrtIrTryEvaluateWithLocals 在 before 不在语句链上时走到空指针
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1265-1282`（循环 `:1270`，解引用 `:1271`）
- 类别：8 未检查前置条件（NULL）
- 触发条件：`before` 不是 `unit.left.left` 这条语句链中的节点（跨函数传入、或传入 null）。
- 后果：`while (statement != before)` 会一路走到 `statement == null`，随后 `:1271` 的 `statement.kind` 空解引用崩溃；函数没有 `statement != null` 兜底。
- 最小修复：循环条件改为 `while (statement != null && statement != before)`，并在走到链尾时返回 false。
- 备注：现有调用点（`:1348`、`:1372`、`:1380`、`:1394`、`:1452`、`:1276`）恰好都传链内节点，所以当前不触发，但函数自身无保护。

##### [中] 72-7｜常量折叠：整数溢出与无符号常量按有符号比较
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1227-1263`（溢出点 `:1235`、`:1249-1251`；比较点 `:1256-1259`；`KrtIrTryEvaluateWithLocals` 同缺陷 `:1294-1297`、`:1304-1314`）
- 类别：3 语义分叉 / 8 整数溢出
- 触发条件：折叠 `INT64_MIN / -1`、`INT64_MIN % -1`、超范围加/乘、或对无符号常量做 `<`/`>`/`<=`/`>=`。
- 后果：`:1235` 的 `-operand`、`:1249-1251` 的 `+ - *` 均无溢出检查；比较只按有符号语义（`:1256-1259`），而 `KrtAstNode` 自带 `native_unsigned` 未被使用，无符号常量会被折叠成相反结果，进而 `KrtIrLowerReturnFunction` 把错误常量直接写进 IR（`:1361`、`:1386`、`:1406`）。
- 最小修复：折叠前读取 `node.native_unsigned` 选择比较语义；对 `/ -1`、`% -1`、`*`/`+`/`-` 做溢出判断，溢出时返回 false（不折叠）而非给出错误值。
- 备注：疑似——需与 `Re.KrtC` 的常量折叠对照确认无符号语义是否符合语言规范。

##### [中] 72-8｜KrtNativeFindFunction / KrtNativeFindMemberFunction 的 1024 上限静默截断
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1638-1646`（`:1641`）、`:1648-1656`（`:1651`）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：模块内函数（或成员函数）数量超过 1024，且目标函数排在第 1024 个之后。
- 后果：循环在 `index < 1024` 处退出并 `return -1`，调用解析失败，无任何「函数过多」诊断；编译结果取决于函数声明顺序。
- 最小修复：去掉硬上限（链表长度本身有限），或超限时报错并指出具体符号。
- 备注：两处为不同函数，分别独立记录。

##### [中] 72-9｜KrtNativeSameName 对源偏移与长度无边界检查
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1625-1636`（解引用 `:1631`）
- 类别：8 未检查前置条件（越界）
- 触发条件：`first`/`second` 为负，或 `first + first_length` / `second + second_length` 超出源缓冲。
- 后果：`:1631` 的 `bytes[first + i]` / `bytes[second + i]` 越界读，可能读到相邻内存并误判符号相等，或崩溃。
- 最小修复：进入循环前校验 `first >= 0 && second >= 0 && first + first_length <= source_length && second + second_length <= source_length`。
- 备注：无。

##### [中] 72-10｜KrtIrAppendMemory 的 256 上限使字节缓冲函数静默失败
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1466-1474`（上限 `:1467`；调用链 `:1522-1549`）
- 类别：1 静默失败 / 2 诊断缺失 / 12 测试不足
- 触发条件：`KrtIrLowerByteBuffer` 接受的函数里语句数使指令数达到 256（约 128 条 `buf[i] = const` 赋值）。
- 后果：`KrtIrAppendMemory` 返回 -1，`KrtIrLowerByteBuffer` 返回 false，编译失败但没有任何「超出 IR 容量」的诊断；且上限 256 只存在于 memory 路径，`KrtIrValidateFunction` 的上限是 4096，两处容量口径不一致。
- 最小修复：改为按需扩容（或统一到 4096），并在真正的上限处给出带位置的诊断。
- 备注：无。

##### [中] 72-11｜生成器 lower 的失败路径遗留 module.generator，且泄漏 breaks/continues
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:926`/`:972`（Move）、`:998`/`:1030`（Dispose）、`:388-391`（TemporaryClosureCall）
- 类别：7 共享可变状态 / 10 资源与生命周期
- 触发条件：`KrtNativeLowerGeneratorMove`/`Dispose` 中 `module.generator = plan` 之后任意一步返回 false（例如 `:946`、`:952`、`:1019`、`:1024`）。
- 后果：`module.generator` 保持非空且指向该 plan，后续所有 `KrtNativeEmit` 都会被 `KrtNativeGeneratorEmit`（`:1613`）重定向到 generator 寻址逻辑，污染同一 module 的后续函数生成；同时已 `new` 的 `breaks`/`continues` 在失败路径上不会 `delete`。
- 最小修复：用统一的收尾（defer 语义）在函数出口同时恢复 `module.generator = null` 与释放两个数组；或在每个失败分支前清理。
- 备注：无。

##### [中] 72-12｜KrtNativeGeneratorAllocationGuard 分配失败时静默返回 0
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:830-837`（调用点 `:867`、`:881`）
- 类别：1 静默失败
- 触发条件：`KrtNativeGeneratorAllocate` 的 `Allocate` 返回 NULL。
- 后果：守卫分支直接 `Const 0; Return`，把空指针对象当正常结果返回给调用方，调用方拿到 NULL 后再解引用；与 box/dispatch 使用 `KrtNativeLowerDispatchTrap` 的惯例不一致。
- 最小修复：NULL 分支改接 `KrtNativeLowerDispatchTrap`（或抛出统一 OOM 异常），不要返回 0。
- 备注：无。

##### [中] 72-13｜KrtNativeLowerBaseConstructor 目标缺失时静默压入 0，跳过基类构造
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1138-1144`（`:1140`）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`expression.native_target == null`（基类构造目标解析失败）。
- 后果：函数返回 true 并只发一条 `Const 0`，基类构造调用被完全跳过，对象处于未初始化状态，且编译期无任何提示。
- 最小修复：返回 false 并附诊断；若确为「无基类」语义，应由调用方显式区分，而不是复用「解析失败」这一路径。
- 备注：无。

##### [中] 72-14｜KrtIrLowerReturnFunction 对 Return.left 为空无保护
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1392`、`:1438`
- 类别：8 未检查前置条件（NULL）
- 触发条件：`final_statement`（或循环中的 `Return` 语句）的 `left == null`（裸 `return;`）。
- 后果：`:1392` 的 `final_statement.left.kind`、`:1438` 的 `statement.left.kind` 直接空解引用崩溃。
- 最小修复：在两处访问 `.kind` 前先判断 `left != null`，为空时返回 false。
- 备注：无。

##### [中] 72-15｜KrtNativeLowerGeneratorFunction 写 module 元数据数组无下标边界检查
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1037-1038`（数组容量 16384 定义于 `:1582-1588`）
- 类别：8 未检查前置条件（越界）
- 触发条件：`index >= 16384`（模块内函数数超过元数据数组容量）。
- 后果：`module.starts[index]`、`module.parameter_counts[index]`、`module.local_counts[index]`、`module.function_frame_bytes[index]` 越界写，破坏堆。
- 最小修复：写入前检查 `index >= 0 && index < 16384`，超限返回 false 并诊断。
- 备注：无。

##### [中] 72-16｜KrtNativeFloatingBits 对非法 native_float 默认按 double 舍入
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:658-661`
- 类别：3 语义分叉
- 触发条件：`expression.native_float` 为 0 或既非 4 也非 8 的值。
- 后果：`fraction_bits`/`bias`/`infinity` 全部取 double 分支（52/1023），字面量按 52 位尾数舍入，与声明类型（如 float）不符；且 `:675`、`:694` 的溢出判定也用错误的 bias。
- 最小修复：只接受 4/8，其余返回 false 并诊断；或按目标类型显式传入格式。
- 备注：疑似——需确认上游是否保证 `native_float ∈ {4,8}`，若保证则应加断言而非静默降级。

##### [低] 72-17｜KrtNativeLowerFloatingBinary 未校验 width 非 0
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:546-554`（`:547-550`、`:554`）
- 类别：8 未检查前置条件
- 触发条件：`expression.left`/`expression.right` 的 `KrtNativeFloatKind` 均为 0（非浮点操作数）。
- 后果：`width = 0`，`target.native_float = 0`，仍以 `width = 0` 发 `FloatBinary`/`FloatCompare`，产生未定义行为的浮点指令。
- 最小修复：`width == 0` 时返回 false（或改走整数路径）。
- 备注：疑似——需确认调用方是否已保证至少一侧为浮点。

##### [低] 72-18｜KrtNativeLowerTry 中的死条件与未校验的 record slot
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:488-490`（以及 `:466`）
- 类别：5 死代码 / 8 未检查前置条件
- 触发条件：任意带 catch 子句的 try。
- 后果：`:489` 已保证 `catch_enter >= 0`，故 `:490` 的 `frame.active_handler = catch_enter >= 0;` 恒为 true，是死条件（掩盖了「catch_enter 失败」这一分支）；同时 `:466` 的 `TryEnter` 直接使用 `statement.local_slot`，未校验其非负。
- 最小修复：`:490` 直接 `frame.active_handler = true;`；在 `:466` 前校验 `statement.local_slot >= 0`。
- 备注：无。

##### [低] 72-19｜KrtIrContainsIdentifier 不检查 alternate 分支
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1320-1324`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：`Ternary` 的 `alternate` 中含标识符而 `left/right` 不含。
- 后果：函数对 `alternate` 视而不见，返回 false，使 `:1392-1394` 的折叠路径漏判（偏保守，不错误折叠，但与其他分支处理不一致，且无注释说明）。
- 最小修复：递归 `node.alternate`（若语言语义允许），或加注释说明为何只查两侧。
- 备注：无。

##### [低] 72-20｜KrtNativeInit 未初始化多数函数元数据与生成器状态
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1570-1600`（`:1582-1592`、`:1593`）
- 类别：11 配置漂移
- 触发条件：读取尚未 lower 的函数的 `starts`/`local_counts`/`parameter_counts`/`aggregate_return_sizes`/`aggregate_return_alignments`/`function_frame_bytes`/`function_name_starts`/`function_name_lengths`，或读取 `generator`/`generator_raw_emit`/`lower_function_index`。
- 后果：只对 `hidden_result_slots` 统一填了 -1 哨兵（`:1590`），其余数组依赖 `new` 的零初始化，读到的是 0 而非「未设置」哨兵，容易与合法值 0 混淆；`generator_raw_emit` 也未显式复位。
- 最小修复：对所有元数据数组统一初始化哨兵值，并显式复位 `generator`、`generator_raw_emit`、`lower_function_index`。
- 备注：无。

##### [低] 72-21｜KrtNativeLowerBoxFault 使用魔数 -2147483647，错误码无统一常量
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:19`（对照生成器错误码 `:757` 的 -2147483602、`:907` 的 -2147483601）
- 类别：11 配置漂移 / 2 诊断缺失
- 触发条件：每次拆箱失败。
- 后果：异常码以散落的字面量分布在各 lower 函数，没有集中常量表；-2147483647 与常规 `INT32_MIN`（-2147483648）差 1，易在对照 C 版时漂移，且注释（`:15-16`）只说明「走 int32 异常契约」，未给出码表。
- 最小修复：定义统一异常码常量（含名字与数值），各处引用常量。
- 备注：疑似——需与 `Re.KrtC` 的异常码表对照确认 -2147483647 是否为有意的保留值。

##### [低] 72-22｜KrtNativeLowerObjectEquality 的 source 形参未使用且临时槽分配后检查
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:124`（未用形参）、`:133-134`（先分配后检查）
- 类别：5 死代码 / 10 资源与生命周期
- 触发条件：`left = KrtNativeBoxTemporary(module)` 返回 -1（槽位耗尽）。
- 后果：`source` 形参在函数体内完全未使用；`right` 仍被分配（`local_counts` 已 +1）后才在 `:134` 判断 `left >= 0 && right >= 0`，失败时白白消耗一个临时槽。
- 最小修复：删除未用形参；先取 `left` 并判 `>= 0` 后再取 `right`。
- 备注：无。

##### [低] 72-23｜KrtNativeLowerGeneratorGet/Current 硬编码槽位 0/1，且 object_size-8 可能为负
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:879-886`（Get）、`:903-914`（Current）
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：getter/current 角色函数带额外参数（占用槽 1），或 `plan.object_size < 8`。
- 后果：`:879`、`:881`、`:884`、`:903`、`:910` 直接把 0/1 当接收者/临时槽使用，与参数槽约定耦合，参数较多时槽 1 冲突；`:886` 的 `plan.object_size - 8` 在 `object_size < 8` 时为负，传给 `AggregateCopy` 作长度。
- 最小修复：使用 plan 中显式的 receiver_slot/scratch_slot 字段；`AggregateCopy` 前校验 `object_size >= 8`。
- 备注：疑似——需确认生成器各角色函数的帧布局是否由上游固定为「接收者 0、临时 1」。

### 第七十三批：SelfHost/Compiler/Middle/Ir/Ir.krt（1921-3830）

#### 覆盖范围

- 文件：`SelfHost/Compiler/Middle/Ir/Ir.krt`，实际读取 1921-3829 行（该文件共 3829 行，请求的 3830 行不存在）。
- 内容：Krt 版 IR 层后半段——表达式/语句 lowering（`KrtNativeLowerExpression`/`KrtNativeLowerStatement`/`KrtNativeLowerFunction`/`KrtNativeLowerModule`）、new/数组/foreach/switch/迭代器/空合并 lowering、全局初始化、间接调用与虚分派、运行时类型测试、异常包编码（legacy/encoded packet、descriptor/key 匹配）、SSA 构建与校验（`KrtSsaBuild`/`KrtSsaValidate`/`KrtSsaValidateDominance`）、SSA 文本输出、聚合值 lowering、宽整数 lowering。
- 交叉核对：仅按需 grep 本文件符号（`KrtNativeOp` 枚举 1555-1567、`KrtNativeLowerDeleteClosure` 296-335、`KrtNativeBoxTemporary` 5-10、`KrtNativeLowerReleaseCell` 240、`KrtNativeSpanWord`/`KrtNativeSameName` 等）。未通读其他源文件；与 Re.KrtC 的 C 版对照因目标文件定位超时未完成，相关分叉标「疑似」。


##### [中] 73-1｜op 245 把 `native_size` 当 scratch 槽传给 DeleteClosure
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2160`（对照 2168、296-335）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：`statement.kind == Expression && statement.op == 245`（删除临时闭包描述符）时执行到 `KrtNativeLowerDeleteClosure(module, statement.left.native_size)`。
- 后果：`KrtNativeLowerDeleteClosure` 的第 2 参是 scratch 槽号，函数体内会写 `scratch`、`scratch+1`、`scratch+2`、`scratch+3`（299-335 行）。此处传入的 `native_size` 在别处语义是字节大小（如 2366 行 `expression.native_size > 0 ? expression.native_size : 8`）。若绑定器没有把该节点字段设为保留槽号，就会按字节数当槽号去写局部帧，造成越界写或串写无关局部变量。同时该值未做 `>= 0` 与 `scratch+3 < local_count` 检查。
- 最小修复：改用与 2168 一致的保留槽（或 `KrtNativeBoxTemporary` 取 4 个连续槽），并校验 `scratch >= 0 && scratch+3 < module.local_counts[module.lower_function_index]`。
- 备注：疑似。`native_size` 在 2409-2443（foreach）、488-511（try/catch）确实被当槽号用，需绑定器确认 op 245 节点是否同样约定；但两种语义共用同一字段本身就是隐患。

##### [中] 73-2｜Call 目标索引未复核，op==221 放行 -1
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2085`、`2095`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：`expression.native_target != null && expression.native_target.op == 221 && expression.native_target.value < 0`。
- 后果：2085 行显式放行 `function_index_local < 0`（仅排除 native_target 为空或非 221）；2091 行只校验 `parameter_count`；2095 行直接 `KrtNativeEmit(Call, function_index_local, ...)`，生成函数索引为 -1 的调用指令，且无任何诊断。后续 SSA/RuntimeBuild 都不会把它当非法值拦截。
- 最小修复：在 2091-2095 之间强制 `function_index_local >= 0`，否则 `module.errors++` 并返回 false。
- 备注：疑似。`op == 221` 在本文件仅此一处出现，语义由绑定器决定；若 221 表示外部符号并依赖后续重定位，则需在注释中说明并保证 -1 仅作为重定位占位。

##### [中] 73-3｜lowering 失败大多不写 error_start、不累计 errors
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2349-2351`（同类：2091、2176、2207、2256、2290、2312）
- 类别：诊断缺失 / 错误处理不一致
- 触发条件：`KrtNativeLowerFunction` 或函数体 `KrtNativeLowerBlock`/`KrtNativeLowerStatement`/`KrtNativeLowerExpression` 任一步返回 false。
- 后果：`KrtNativeLowerModule` 只在 2331、2350 两处设置 `output.error_start`，且全文件只有 2085 一处 `module.errors++`。函数体 lowering 失败时 `errors` 保持 0、`error_start` 保持初值，调用方无法区分「无错误」与「lowering 失败」，诊断也没有源码位置。
- 最小修复：在 `KrtNativeLowerModule` 的失败返回前统一设置 `output.error_start`（取当前 function_node 的 start/offset）并递增 `output.errors`。
- 备注：无。

##### [中] 73-4｜`new` 的 `native_size` 静默回退为 8
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2366`
- 类别：静默失败 / 未检查前置条件
- 触发条件：`KrtNativeLowerNew` 走到 `expression.left == null` 且 `expression.native_size <= 0`。
- 后果：按 8 字节分配对象，随后 2372-2373 写运行时描述符、2374-2377 调构造器，对象尺寸与实际类型不符，轻则构造器写越界，重则产生错误对象；全程无诊断。
- 最小修复：`native_size <= 0` 时返回 false 并报告（与 2368 行 `local_slot < 0` 的处理一致）。
- 备注：无。

##### [中] 73-5｜局部槽上限不一致：65535 与 131072
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3784`（对照 9）
- 类别：配置漂移 / 未检查前置条件
- 触发条件：函数已用 `KrtNativeBoxTemporary` 把 `local_counts` 推到 65535 附近后，再出现 `native_bits ∈ (64,128)` 的精确宽自增（1980 行路径）。
- 后果：`KrtNativeBoxTemporary`（9 行）在 `slot >= 65535` 时返回 -1，而 `KrtNativeLowerPreciseWideIncrement`（3784 行）允许 `value_slot` 达到 131071，并把 `local_counts` 推到 131072。两处共用 `module.local_counts` 却用不同上限；若运行时/VM 的局部槽字段是 16 位，65536 以上的槽号会溢出或错址。
- 最小修复：抽出统一的局部槽上限常量，两处复用同一检查，并在 `local_counts` 增长前做上界断言。
- 备注：疑似（取决于 VM 局部槽宽；但两个上限并存本身就是漂移）。

##### [低] 73-6｜不可达的宽类型分支（死代码）
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1958-1960`（同类：1938）
- 类别：死代码
- 触发条件：`expression.kind == Member || Index` 且 `KrtNativeWideKind(expression)` 为真。
- 后果：1951-1953 行已对宽表达式 `return`，执行不到 1958；因此 1959 行的 `if (KrtNativeWideKind(expression))` 恒为假，永不走 `NormalizeWide`。1938 行同理（1931-1933 已 return）。这类残留会误导维护者以为 Member/Index 的宽值需要单独归一化。
- 最小修复：删除 1959-1960 与 1938 行的死分支，或改为断言 `!KrtNativeWideKind(expression)`。
- 备注：无。

##### [低] 73-7｜`ssa.uses` 只写不读
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3269`、`3435`
- 类别：死代码
- 触发条件：每次 `KrtSsaBuild`。
- 后果：3269 分配 `ssa.uses`，3435 对每个 operand 做 `ssa.uses[incoming]++`，但全文件没有任何读取点。这是纯浪费，也说明预期的消费端（如死代码消除/引用计数）缺失。
- 最小修复：删除该数组与累计循环，或接入后续 pass。
- 备注：无。

##### [低] 73-8｜`KrtSsaValidate` 的结果类型检查恒为真（不可达）
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3458`（对照 3433）
- 类别：死代码 / 注释与实现脱节
- 触发条件：所有 `ssa.results[value]` 为真的值。
- 后果：`KrtSsaBuild` 在 3433 行已把每个 result 强制设为 `types=1、widths=64`，因此 3458 行的 `types<=0 || widths==0` 永不触发。该校验名义上检查结果类型，实际被 builder 的归一化完全掩盖，真实不变量（如 phi 操作数类型一致）只靠 3581 行兜。
- 最小修复：在归一化前保留原始类型用于校验，或删除该分支并改为检查 3581 行已覆盖的约束。
- 备注：无。

##### [低] 73-9｜SSA 类型推断失败静默兜底为 i8
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3431`
- 类别：静默失败
- 触发条件：某个 result（含 phi）在 3413-3428 的类型传播后 `widths[value]` 仍为 0。
- 后果：3431 静默写成 `widths=8、types=1`，3432 把它快照成语义类型 `i8`，随后 3433 归一化为 i64。下游文本 IR（3630-3634）会把该值标注为 `; i8`，与真实类型不符，且无任何诊断。
- 最小修复：无法推断类型时返回 null 并报告具体值编号/操作码。
- 备注：无。

##### [低] 73-10｜文本 IR 丢失 RuntimeDescriptor 类型号等参数
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3617-3619`
- 类别：诊断缺失 / 注释与实现脱节
- 触发条件：`KrtSsaWriteIr` 输出 op 74（RuntimeDescriptor）、73（ClosureEnvironment）、22（Load）、38/39/40/43（浮点）、60-68（宽操作）、26（Syscall）等。
- 后果：3617 的「打印 args」白名单是 `1,100,17,18,19,28,29,2,3,50,70,71,72`，不含 73/74；3619 只对 18/70/71/72 打印 extra。于是 RuntimeDescriptor 的类型索引、Load 的宽度、浮点操作的格式、wide 子操作码、Syscall 参数个数等关键信息在文本 IR 中全部丢失，输出的 `.ssa.ir` 无法往返/复核。
- 最小修复：补全 73/74 的 args、22/38/39/40/43/60-68 的 extra 输出。
- 备注：无。

##### [低] 73-11｜运行时描述符与异常包布局幻数硬编码
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2710`（步长 40）、`2699-2700`（72/80）、`2925/2946/2951`（88/32/16）、`2809`（0x7FFFFFFFFFFFFFFC）、`2818`（-3）、`2855`（3）
- 类别：配置漂移
- 触发条件：运行时侧的方法表/描述符/异常包结构布局调整时。
- 后果：这些偏移、步长与位掩码直接写在 lowering 里，与运行时结构体之间没有单一事实源，也没有断言。任一侧改动都会让分派、类型匹配或异常编解码静默错址，且编译期无报错。
- 最小修复：把这些布局常量集中到与运行时共享的定义，并在 lowering 入口加布局断言。
- 备注：疑似（需与运行时头文件核对具体数值）。

### 第七十四批：SelfHost/Compiler/Backend/Kro/KroBackend.krt（1-1735）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Backend/Kro/KroBackend.krt` 第 1-1735 行（分段读取，未整文件通读）。
- 交叉核对仅限 grep 特定符号：`SelfHost/Compiler/Middle/Ir/Ir.krt` 的 `enum KrtNativeOp`（1554-1568 行）、本文件内 `strtab_size`/`string_bytes`/`*_reloc_count` 的赋值点。未在 `/sdcard/KairoteLang/Re.KrtC` 下检索到 `KroCodegen/Kro*.inc`（grep 无命中），故涉及 C 版对照的条目一律标注「疑似」。
- 本批覆盖：浮点编码（1-117）、全局符号/ABI 清单（119-352）、异常帧（354-379）、整数规范化（381-396）、KrtKroObject 与文本写出（398-836）、IR 快速发射器（838-937）、原生栈 IR 后端（939-1557）、写出辅助与字符串运行时（1559-1631）、聚合/宽浮点位转换（1633-1735）。


##### [高] 74-1｜Compare 未知操作码静默回退为 setne
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1314`-`1319`
- 类别：未检查前置条件；语义分叉；诊断缺失
- 触发条件：`KrtNativeOp.Compare` 且 `module.args[index]` 不属于 `{61,101,60,62,102,103}`。`int32 cc = 149` 为默认值，六个 if/else 均不命中时既不报错也不返回 false。
- 后果：发射 `0x0f 0x95`（setne）作为比较结果，生成静默错误的机器码；有符号/无符号分支都受影响。
- 最小修复：default 分支改为 `return false`，或先校验 `args[index]` 白名单再查表。
- 备注：同文件 `KrtKroNativeFloatCompare`（77-78 行）对未知 condition 明确 `return false`，同一后端内两套比较的处理口径不一致。

##### [高] 74-2｜KrtKroGlobalSymbols 写 slot_symbols/data 前不校验下标与尺寸
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:256`-`267`
- 类别：未检查前置条件（数组下标越界）
- 触发条件：`entry.declaration.native_global-1 >= module.global_count`（258 行）、`entry.guard_slot >= module.global_count`（261、267 行）、`slot*8 + entry.storage_size > module.global_count*8`（265-266 行）。这些字段均来自 AST/模块元数据，本函数未做任何边界校验即直接索引写入。
- 后果：`slot_symbols[...]` 与 `data[offset+part]` 越界写，破坏相邻堆对象或符号表；越界写不触发任何诊断。
- 最小修复：在进入符号循环前对每个 entry 统一校验 `native_global-1`、`guard_slot`、`offset+storage_size` 落在 `[0, global_count*8)`。
- 备注：同函数 287 行对重定位 slot 做了 `slot<0||slot>=module.global_count` 校验，说明作者意识到该不变量，但 258/261/265 的写入点漏检，前后不一致。

##### [高] 74-3｜KrtKroWriteSymbols 用 total_reloc_count 读取只按 text 分配的缓冲
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:790`-`793`
- 类别：契约破坏；内存越界读；资源与生命周期
- 触发条件：`output.total_reloc_count = text_reloc_count + rodata_reloc_count + data_reloc_count`（292 行），而 `text_relocation_bytes` 在 282-284 行只按 `(old_relocs + global_reloc_count)*16+1` 分配；只要 `rodata_reloc_count` 或 `data_reloc_count` 非零即越界。
- 后果：`KrtKroWriteBytes(relocations, total_reloc_count*16, ...)` 越过数组尾部读取，把堆上无关字节写入目标文件（重定位表错乱 + 潜在信息泄露）。
- 最小修复：按节分别写入（text/rodata/data 各自计数与缓冲），或让 `total_reloc_count` 只统计确实连续存放于 `text_relocation_bytes` 的条目。
- 备注：疑似（取决于是否存在 rodata/data 重定位非零的组合路径）；本切片内 292 行的相加公式与 793 行的单缓冲读取在字面上即不自洽。

##### [中] 74-4｜KrtKroGlobalSymbols 失败路径留下半构建的 output
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:238`-`244`（先替换 `symbol_bytes`/`string_bytes`/`data_bytes`），失败点 `249`、`287`、`289`
- 类别：错误处理不一致；资源与生命周期
- 触发条件：`initializer_index` 非法（249）、重定位 slot 无对应符号（287）、`KrtKroLibraryWord`/重定位写入失败（288-289）。
- 后果：return false 时 `output` 已被改为新数组尺寸与部分符号内容，旧数组已 `delete`；调用方若复用或仍执行写盘，会得到结构不合法的对象，且中途分配未回收。
- 最小修复：先在临时变量中完成全部校验与构建，全部成功后再一次性提交到 `output`；或在每个失败分支回滚并释放新数组。
- 备注：与 74-15（module 状态污染）同类但位置与对象不同，独立记录。

##### [中] 74-5｜KrtKroAbiFinishNamed 追加符号名时无边界检查
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:347`-`348`
- 类别：未检查前置条件（数组下标）；契约破坏
- 触发条件：`name + strlen(marker) + 1 > output.string_bytes.length`。`string_bytes` 在 `KrtKroGlobalSymbols`（238 行）只按全局名总长 `names+1` 分配，未给 ABI 清单名预留；函数内也无扩容。
- 后果：`output.string_bytes[name]=text[index]` 越界写；同时本函数自身不更新 `output.strtab_size`（依赖 2037 行调用方补写），函数级不变量不完整。
- 最小修复：写入前校验剩余容量并按需扩容，同时在本函数内同步 `strtab_size`（或明确注释由调用方负责）。
- 备注：疑似——未允许读取 1735 行以后的完整调用链，无法确认调用方是否另行扩容；`marker` 以 `string` 传入后强转 `byte*` 遍历（347 行）本身也依赖未声明的字符串内存布局。

##### [中] 74-6｜KrtKroObjectIsValid 过弱，空函数产物通过校验却无法写出
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:470`-`474`；空函数路径 `858`-`866`
- 类别：契约破坏；静默失败；阶段不对齐
- 触发条件：`ir_function.instruction_count == 0` 时 `symbol_count=0`、`strtab_size=6`（857-865 行），`KrtKroObjectIsValid` 判定通过（只查 `symbol_count>=0`、`strtab_size>=0`），但 `KrtKroWriteSymbols`（796-797 行）要求 symbol_count 为 1 或 2 且 strtab_size 为 6/13，必然返回 false。
- 后果：同一对象在「有效性」与「可序列化」两个阶段得到相反结论，错误在写盘阶段才出现且无诊断，属于阶段间契约漂移。
- 最小修复：`KrtKroObjectIsValid` 增加与写出器一致的组合约束，或让写盘器接受 0 符号对象。
- 备注：`IsValid` 还不校验 `data_size>0 && data_bytes==null`、`symbol_count>0 && symbol_bytes==null` 等组合。

##### [中] 74-7｜KrtKroNativeGlobalsStart 是忽略 output/pc 的空实现
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:143`-`145`，调用点 `1240`
- 类别：死代码；阶段不对齐；注释与实现脱节
- 触发条件：每次编译入口函数（`function_index == module.entry`）都会在序言中调用它并传播其返回值。
- 后果：函数体只做 `slots` 范围校验，不发出任何字节、不推进 pc；若命名/调用语义期望在此登记 globals 区（.bss 或全局初始化起点），该动作缺失；若确实无需动作，则 1240 行的 `KrtKroNativeCode(...,16724293,3)` 与本次调用构成的「伪步骤」会误导维护者。
- 最小修复：对照 C 版确认此处是否应发射内容；无内容则删除调用并简化 1240 行，有内容则补齐实现与注释。
- 备注：疑似——本仓库未 grep 到 `KroCodegen/Kro*.inc`，无法完成交叉确认。

##### [中] 74-8｜异常槽位计算 `arity + 6` / `record + 6` 存在 int32 溢出
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1502`、`1509`、`1514`
- 类别：未检查前置条件（整数溢出）
- 触发条件：`arity`（即 `module.extra[index]`）或 `record`（即 `module.args[index]`）接近 `INT32_MAX`，`arity + 6` 回绕为负数，`< 0` 与 `> local_counts` 两道检查同时失效。
- 后果：`KrtKroNativeTryEnter/TryLeave` 收到巨大 slot，`-8*(slot+6)` 位移回绕，生成访问错误帧地址的代码；异常记录布局被破坏。
- 最小修复：比较前转为 int64，或先判断 `arity > module.local_counts[function_index]` 再算 `arity + 6`。
- 备注：正常前端不会产生该值，但后端对 IR 元数据零容忍要求下应显式拒绝。

##### [中] 74-9｜Data 操作码未拒绝负的 size（extra）
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1421`
- 类别：未检查前置条件；诊断缺失
- 触发条件：`module.extra[index] < 0` 且 `module.args[index] + module.extra[index] <= module.data_length`（`args>=0` 已单独检查）。
- 后果：负长度被静默接受，`patch_kinds=3` 的重定位仍按 `data_base + args` 解析，尺寸信息被丢弃，生成的数据引用语义不明。
- 最小修复：条件中加入 `module.extra[index] < 0` 直接 `return false`。

##### [中] 74-10｜KrtKroNativeStackDepths 先把 int64 extra 截断成 int32 再判范围
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1140`
- 类别：未检查前置条件（整数溢出/窄化）
- 触发条件：`module.extra[index]` 超出 int32 表示范围（例如负大数或 > 2^31-1）。
- 后果：`(int32)module.extra[index]` 回绕后参与 `need`/`delta` 计算（1149-1156 行），栈深度分析结果错误；可能把非法 IR 判为合法，或在后续发射中才以无关原因失败。
- 最小修复：在用范围内先以 int64 校验 `extra`，再窄化；或对其绝对值设置统一上限。

##### [中] 74-11｜文本缓冲容量估算使用未校验的 extra，可为负
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1193`
- 类别：未检查前置条件；静默失败
- 触发条件：`module.ops[i]==19` 且 `module.extra[i]` 为负，`capacity += extra*20 + 64` 反而缩小容量。
- 后果：`words=(capacity+3)/4` 偏小，发射中后期 `KrtKroEmitTextByte` 因越界返回 false，错误点在大量代码之后才暴露，定位困难。
- 最小修复：估算循环中对 `extra` 做非负校验（或按 0 处理并单独报错）。
- 备注：`KrtKroEmitTextByte`（454 行）有边界保护，不会造成越界写，故定为中而非高。

##### [中] 74-12｜宽指令暂存指针挂在 output 对象上并在逐函数间复用
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1260`（`output.native_wide_base/cursor/limit`）
- 类别：共享可变状态
- 触发条件：一次构建中连续编译多个函数（1225-1524 行循环），每个函数在 1260 行重置 cursor/limit；期间的 `return false` 路径会留下半更新状态。
- 后果：发射正确性依赖该隐式全局游标的生命周期；任何提前 return 后再重入，或并行编译同一 output，都会读到脏状态。注释（438-442 行）声明其为「compiler state, never object-file metadata」，但没有隔离措施。
- 最小修复：把 base/cursor/limit 收进发射上下文局部结构，随函数进出栈重置。
- 备注：疑似（需结合 1735 行后宽指令发射函数确认 cursor 的全部读者）。

##### [中] 74-13｜op==30 把负栈深度钳为 0，掩盖上游分析错误
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1408`-`1409`
- 类别：静默失败；诊断缺失
- 触发条件：`stack_depths[index] < 1`（例如该块未被 `KrtKroNativeFlowEdge` 到达而保持 -1，或上游 need/delta 计算有误）。
- 后果：`live_values` 被静默改为 0，后续搬移操作数指令按错误深度生成；真正的分析错误永不报告。
- 最小修复：`live_values < 0` 时直接 `return false` 并指明指令序号。

##### [中] 74-14｜同一 KrtKroObject 在动态文本与遗留 text_word 两条写出路径间状态串味
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:618`-`621`（动态路径优先），与 `487`-`496`、`842`-`876` 的遗留字段写入
- 类别：共享可变状态；契约破坏
- 触发条件：同一个 `KrtKroObject` 先经 `KrtKroBeginText`（445-450 行）写过动态文本，再交给 `KrtKroEmitConstantHelperCall`/`KrtKroEmitIrFunction` 等只写 `text_word0..7` 的发射器。
- 后果：`KrtKroWriteText` 因 `text_words != null` 走动态分支，完全忽略刚写入的遗留指令字，输出与请求不符且无任何报错。
- 最小修复：遗留发射器入口清空 `text_words`/`text_word_count`，或 `KrtKroObjectIsValid` 增加互斥断言。

##### [中] 74-15｜KrtKroEmitStackModule 失败后污染 module 的发布态
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1207`-`1208`、`1215`-`1216`（先发布 `function_offsets/function_sizes/external_reloc_*`），失败点 `1232`、`1237`、`1242`、`1374` 等
- 类别：共享可变状态；错误处理不一致；资源与生命周期
- 触发条件：任一轮发射在发布 offsets 后返回 false（如 patch 容量不足、KrtKroNativeStackDepths 失败、操作码非法）。
- 后果：`module` 持有指向旧数据的偏移数组与半填充的 `external_reloc_offsets`；若调用方重试或对同一 module 再编译，会得到错误的重定位目标。
- 最小修复：全部成功后再把 offsets 写回 module，或用局部数组构建完再交换。

##### [低] 74-16｜后端混用裸数字操作码与枚举常量
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1335`（`op == 75`）、`1457`（`op == 31`）、`1501`（`op == 50`）、`1333`-`1516` 全段
- 类别：配置漂移；注释与实现脱节
- 触发条件：`Ir.krt:1554-1568` 的 `KrtNativeOp` 枚举顺序或取值调整时，本文件大量字面量（30/31/32/33/35-43/44-46/50-54/60-69/73-76）不会随之变化。
- 后果：枚举重排后此处静默失配（把一种操作当另一种发射），且编译期无任何提示。
- 最小修复：全部改用 `(int32)KrtNativeOp.Xxx`；至少对宽操作 60-69 与守卫 75/76 建常量表。

##### [低] 74-17｜FrameLoad/FrameStore 自身不限制 slot，位移可按 int32 回绕
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:966`、`978`（`-8 * (slot + 1)`）
- 类别：未检查前置条件（整数溢出）
- 触发条件：以接近 `INT32_MAX/8` 的 slot 调用这两个通用 helper（`KrtKroNativeIndirectCall` 的 `scratch` 来自 `module.local_counts`，当前仅被 frame 上限间接约束）。
- 后果：displacement 回绕为正数，生成 `mov reg,[rbp+正偏移]`，读写调用者帧之外的内存。
- 最小修复：helper 入口校验 `slot >= 0 && slot <= 268435455`，或改用 int64 计算后判范围。
- 备注：在 `KrtKroEmitStackModule` 主路径中 slot 受 1257 行 `frame_bytes <= 1048576` 间接限制，故为低。

##### [低] 74-18｜Allocate 把 stride==0 当作按字节分配，语义未文档化
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1077`-`1083`（仅 `stride > 1` 时相乘），调用示例 `1622`（传 0）
- 类别：契约破坏；语义分叉
- 触发条件：任何 `KrtKroNativeAllocate(stride=0)` 调用（如字符串拼接）。
- 后果：0 与 1 行为完全相同；若 C 版把 0 视为非法或另有含义，即构成跨实现分叉；当前注释（1067-1069 行）只说明 payload/头部布局，未说明 0 的语义。
- 最小修复：明确 0/1 等价并写入注释，或拒绝 0。
- 备注：疑似——缺少 C 版对照，无法确认是否有意设计。

### 第七十五批：SelfHost/Compiler/Backend/Kro/KroBackend.krt（1736-3470）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Backend/Kro/KroBackend.krt`，仅读取 1736-3470 行（全文件共 3469 行）。
- 覆盖函数：`KrtKroNativeWideFloat8Bits` 尾部、`KrtKroLibraryWord/Symbol/Name`、`KrtKroLibraryNormalize/Value`、`KrtKroLibraryStaticData`、`KrtKroEmitLibrarySections`、`KrtKroEmitLibraryData`、`KrtKroLibraryPublicSignature/PublicDefinition/NeedsAbi`、`KrtKroLibraryFunctions`、`KrtModuleMetadataClose/Edits/File/Build`、`KrtKroModuleMetadata`、`KrtNativeAllocateRegisters`、`KrtKroNativeRegisterFrame/PreserveRegisters/AllocatedLoad/AllocatedStore`、`KrtKroNativeTemporaryOp/Flush/Push/Pop/Cast/Instruction`、`KrtKroRuntimeAddress/AddSymbol/Name/Map/Find/Descriptors`、`KrtSsaAllocate`、`KrtKroSsaLoad/Store/Edge/Read/ScalarOp/Scalar`、`KrtKroEmitSsaModule`、`KrtKroEmitNativeModule`、`KrtKroWideScratchBytes`、`KrtKroNativeWideBox/Load/Store/Convert/Operands/Binary/Compare/Unary/Post/Float/Ref`。
- 交叉核对（仅按符号 grep/定点读取，未通读其他文件）：`SelfHost/Compiler/Middle/Ir/Ir.krt` 的 `KrtNativeOp` 枚举、`KrtSsaValidate`、`KrtSsaBuild`、`KrtSsaInputs`；`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt` 的 `KrtNativeRuntimeRead`；`SelfHost/Compiler/Pipeline/CompilerPipeline.krt` 的调用点；同文件 1405-1419（非 SSA 路径）用于对照。
- Re.KrtC 的 Kro 后端为 IR 驱动的 `KroCodegen.c/.h + Kro*.inc`，与本 Krt 自举实现结构不同，无法逐函数对照；相关分叉均标「疑似」。


##### [高] 75-1｜SSA 路径 StackAllocate 的 `live_values` 恒为 0（死代码 + 语义分叉）
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3030-3031`（`int32 live_values = 0; if (live_values < 0) { live_values = 0; }`），使用点 `:3038`、`:3041`
- 类别：死代码 / 语义分叉 / 注释与实现脱节
- 触发条件：SSA 基本块内出现 op 30（StackAllocate），且该分配嵌在更大表达式中（其下仍有活跃表达式操作数）。
- 后果：注释宣称「Shift live expression operands below the allocation」，但 `live_values` 恒为 0，`if (live_values < 0)` 永远为假；`rep movsq`（rcx=0）为空操作，活跃操作数不会被搬到新 rsp 之上，随后 `mov rax,[rsp+live_values*8]` 取到未初始化单元，表达式栈序错位，可能导致栈损坏/错误结果。同文件非 SSA 路径 `:1408` 使用 `stack_depths[index]-1`，确认是实现分叉。
- 最小修复：在 SSA 构建/发射期保留每个 value 的表达式栈深度，用 `depth-1` 计算 `live_values`；或在 StackAllocate 前强制 flush 表达式栈。
- 备注：与非 SSA 路径 1405-1409 对照确认。

##### [高] 75-2｜`KrtKroRuntimeDescriptors` 经无边界检查的 `KrtNativeRuntimeRead` 读运行时 blob
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2520`（`KrtNativeRuntimeRead(module.runtime_bytes,record.offset+56)`）
- 类别：未检查前置条件 / 资源与生命周期
- 触发条件：runtime 类型记录的 `offset` 接近 blob 末尾，或 runtime blob 损坏/被伪造。
- 后果：`KrtNativeRuntimeRead`（`SemanticRuntime.krt:980-984`）直接 `bytes[offset+index]` 读 8 字节且无任何边界检查，越界读可能导致崩溃或把相邻内存当作 `interface_count` 使用，进而放大为后续越界。
- 最小修复：调用前验证 `record.offset >= 0 && record.offset + 64 <= module.runtime_length`；并让 `KrtNativeRuntimeRead` 自带边界检查。
- 备注：辅助函数位于其他文件，已按符号定点核对。

##### [中] 75-3｜`method.target` 未校验即索引 `module.function_offsets`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2535-2537`
- 类别：未检查前置条件
- 触发条件：runtime method 的 `target.value` 为负数以外的越界函数索引。
- 后果：`module.function_offsets[target]` / `module.function_sizes[target]` 越界读，生成错误符号 offset/size，产出损坏对象或未定义行为。
- 最小修复：增加 `if (target >= module.function_count) { return false; }`（`target < 0` 已处理）。
- 备注：疑似（C 版无对应实现可对照）。

##### [中] 75-4｜`relocation.target` 未校验即索引 `descriptor_symbols`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2560`
- 类别：未检查前置条件
- 触发条件：`runtime_relocation.kind == 1` 且 `target` 越界（负数或 `>= runtime_type_count`）。
- 后果：`descriptor_symbols[...]` 越界读，重定位被写成错误符号号。
- 最小修复：索引前检查 `relocation.target >= 0 && relocation.target < module.runtime_type_count`。
- 备注：疑似。

##### [中] 75-5｜`record.index` 未校验即写入 `descriptor_symbols`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2517`（`descriptor_symbols[record.index] = symbol;`）
- 类别：未检查前置条件
- 触发条件：runtime 记录的 `index` 越界。
- 后果：越界写，破坏堆上数组。
- 最小修复：写入前检查 `record.index >= 0 && record.index < module.runtime_type_count`。
- 备注：疑似。

##### [中] 75-6｜复制 rodata 重定位时源数组写成 `text_relocation_bytes`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2556`（`relocations[text_count*16+index] = output.text_relocation_bytes[previous_text*16+index];`）
- 类别：语义分叉 / 契约破坏
- 触发条件：`output.rodata_reloc_count > 0`（复用同一 output 对象，或先执行过带 rodata 重定位的发射）。
- 后果：从 `text_relocation_bytes + previous_text*16` 读取并当作数据重定位追加，源与长度错位，产出损坏的重定位表。
- 最小修复：改用 `output.rodata_relocation_bytes`（或实际存放 rodata 重定位的数组），并核对字段命名。
- 备注：疑似；当前管线中 `rodata_reloc_count` 似恒为 0，属潜在缺陷。

##### [中] 75-7｜库数据「局部名消歧」后缀未被符号表引用
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1942-1954`
- 类别：契约破坏 / 注释与实现脱节
- 触发条件：同一 data 模块内有两个同名 access-hidden 声明，或局部名与导出名相同。
- 后果：`KrtKroLibrarySymbol` 在 `:1942` 记录的 name 偏移指向未加后缀的原名；`:1949-1954` 追加的 `$local<index>` 副本没有任何符号引用，符号表仍出现重名，消费者/链接器无法区分，可能解析到错误符号。与函数路径 `:2004`「先改 `link_name` 再记录」的做法不一致。
- 最小修复：在追加后缀后再记录符号名偏移（或把 `name` 回退到后缀起点后调用 `KrtKroLibrarySymbol`）。
- 备注：疑似（C 版无对应实现）。

##### [中] 75-8｜`KrtKroLibraryFunctions` 字符串表容量无运行时校验
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1992`（`new byte[count*8192+64]`），写入点 `:2009-2010`、`:2028-2029`、`:2033`、`:2036`
- 类别：未检查前置条件 / 资源与生命周期
- 触发条件：函数/外部符号名、ABI manifest 或 marker 的总长度超过预留容量。
- 后果：Krt 数组无边界检查，静默越界写，堆损坏且无诊断。
- 最小修复：写入前检查 `name + length + 1 <= capacity`，否则 `return false`。
- 备注：疑似；`KrtKroAbiFinishNamed`（`:336-348`）同样直接写 `output.string_bytes[name]`。

##### [中] 75-9｜`KrtKroLibraryValue` 常量移位未校验移位量
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1851-1852`（`low = left << right;` / `left >> right`）
- 类别：未检查前置条件（移位）
- 触发条件：库静态数据中的常量表达式出现 `<<`/`>>`，且右操作数 `>= 64` 或为负。
- 后果：编译器自身执行未定义/实现定义移位，常量折叠结果错误；自举时错误常量会传播到生成代码，且无任何诊断。同函数对除零、`INT64_MIN / -1` 已有保护，仅移位缺失。
- 最小修复：`if (right < 0 || right >= 64) { return false; }`，或按语言语义折叠。
- 备注：疑似（C 版不可对照）。

##### [中] 75-10｜`KrtKroSsaScalar` 对 op 2/3/100 缺少局部槽位范围检查
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2752-2754`
- 类别：未检查前置条件 / 错误处理不一致
- 触发条件：优化开启（`KrtDriverOptimization > 0`）时，SSA 中 op 2/3/100 的 `args` 越界。`KrtSsaValidate` 不校验 op 2/3/100 的槽位，而 generic 路径 `:2919`、`:2922` 有检查。
- 后果：直接以 `rbp-8*(slot+1)` 读写帧外内存，静默错误或崩溃；与 generic 路径行为不一致。
- 最小修复：与 `:2919/:2922` 一致，检查 `args >= 0 && args < module.local_counts[function_index]`。
- 备注：疑似（正常 SSA 构建保证范围，但缺少纵深防御）。

##### [中] 75-11｜`KrtKroLibraryStaticData` 失败路径留下部分可变状态
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1884-1899`
- 类别：错误处理不一致 / 共享可变状态
- 触发条件：guard 槽位断言失败（`:1897` `module.initializer_guard_slot != module.global_count-2`）或在提交后失败。
- 后果：第二遍循环已写 `entry.static_data/static_low/static_high`，随后 return false；调用方只看到 false。管线 `CompilerPipeline.krt:515` 在返回 false 且 `function_count != 0` 时继续走 SSA，会用到被部分修改的 module/ast（static_data 标志、被摘链的 guard、被改小的 global_count）。
- 最小修复：先完成全部校验再提交修改；失败时回滚已写标志与计数。
- 备注：调用点已定点核对。

##### [低] 75-12｜SSA 发射中 `op == KrtNativeOp.Nop` 分支不可达
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2915`（`else if (op == (int32)KrtNativeOp.Nop) { }`）
- 类别：死代码
- 触发条件：恒不触发（`:2900` 已 `if (op == 0 || op == 101) { index+1; continue; }`，而 `Nop = 0`）。
- 后果：无功能影响，增加维护误导。
- 最小修复：删除该分支，或移除 `op==0` 的前置跳过。
- 备注：`Nop=0` 见 `Ir.krt:1555`。

##### [低] 75-13｜SSA 发射中 op 27（Drop）分支不可达
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3054-3055`
- 类别：死代码
- 触发条件：恒不触发（`KrtSsaBuild` 在 `Ir.krt:3369` 把 op 27 一律降级为 Nop）。
- 后果：无功能影响；若未来 SSA 不再降级 Drop，需重新确认该分支语义。
- 最小修复：删除该分支或注明依赖 SSA 降级。
- 备注：与 75-12 同类但位置/依据不同，独立列出。

##### [低] 75-14｜SSA 发射中 op 32（Dup）分支不可达且编码与临时路径不一致
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3083-3084`（`255,52,36` = `call qword [rsp]`）
- 类别：死代码 / 疑似语义分叉
- 触发条件：恒不触发（`KrtSsaBuild` 在 `Ir.krt:3370-3372` 把 op 32 降级为 Nop 并复制栈值）。
- 后果：当前无影响；若该分支被启用，`call [rsp]` 与临时路径 `:2426` 的 `mov rax,[rsp]`（0x24048b48）语义不符。
- 最小修复：删除该分支，或改为与 `:2426` 一致的 `mov rax,[rsp]`。
- 备注：疑似；因不可达暂列低。

##### [低] 75-15｜`KrtKroEmitSsaModule` 末尾重置 `symbol_count/strtab_size` 可能丢弃前序符号
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3195`（`output.symbol_count = 1; output.strtab_size = 6;`）
- 类别：契约破坏 / 共享可变状态
- 触发条件：同一 output 对象在本函数调用前已含符号（复用输出缓冲）。
- 后果：既有符号与字符串表计数被清零，若前序符号是必要的（如 ABI/manifest 符号），会丢失。
- 最小修复：明确输出对象所有权；重置前保存/迁移既有符号，或不重置。
- 备注：疑似；当前管线每次新建 output。

##### [中] 75-16｜`external_used` / `external_reloc_count` 未在发射开始时重置
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2817`（取 `module.external_used`）、`:3181`（`external_used[external]=true; module.external_reloc_count++`）；入口 `:2777` 只重置 `runtime_code_count`
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：同一 module 被发射两次，或输出/模块被复用。
- 后果：`external_used` 残留 true 使 `KrtKroLibraryFunctions` 多发射未引用外部符号；`external_reloc_count` 累积可能超过 `patch_capacity` 而失败，或产出重复重定位。
- 最小修复：发射开始时 `module.external_reloc_count = 0` 并清空 `external_used`，或每次使用独立数组。
- 备注：疑似；正常管线单次发射。

##### [低] 75-17｜释放 `allocation` 成员但未释放对象自身
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3196`（`delete allocation.registers; delete allocation.spills; delete allocation.frame_counts; delete allocation;` 中 `delete allocation;` 缺失）
- 类别：资源与生命周期
- 触发条件：每次成功发射。
- 后果：`KrtSsaAllocation` 对象本身泄漏（若 Krt 无 GC）。
- 最小修复：补 `delete allocation;`。
- 备注：低。

##### [中] 75-18｜`external_symbols[relocation_targets[index]]` 未校验索引
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2045`
- 类别：未检查前置条件
- 触发条件：`module.external_reloc_targets` 含越界或负值（损坏状态或复用 module 后累积）。
- 后果：`external_symbols` 越界读，写出错误符号号。
- 最小修复：索引前检查 `>= 0 && < module.external_count`。
- 备注：正常由 SSA 发射填充时安全；疑似。

##### [低] 75-19｜StackAllocate 分配大小对齐疑似错误（`and rsp` 而非 `and rax`）
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3032-3034`（`add rax,15`；`and rsp,-16`；`sub rsp,rax`）
- 类别：未检查前置条件 / 语义分叉（疑似）
- 触发条件：StackAllocate 的 size 不是「16 的倍数减 15」时。
- 后果：`rsp` 先对齐，再减去 `size+15`，结果 `rsp` 通常不再 16 字节对齐，后续 `rep movsq`/`push` 及依赖 rsp 对齐的操作可能出错或性能受损。若意图是向上取整分配大小，正确写法应为 `add rax,15; and rax,-16`。
- 最小修复：改为 `and rax,-16`（或在 `sub` 后重新对齐并同步调整帧）。
- 备注：疑似；非 SSA 路径 `:1410-1415` 写法相同，可能为有意 ABI。

##### [中] 75-20｜参数上限契约不一致：被调 `parameter_counts > 128` 拒绝，调用 `arity <= 129` 放行
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2832`（`parameter_counts[function_index] > 128` 返回 false）与 `:2966`（`arity > 129` 才拒绝）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：模块含 129 个源参数的函数，或调用 arity=129 的函数。
- 后果：调用点可生成，但被调函数发射阶段直接失败，编译中断；若为外部声明则生成无法解析的调用。128 源参数 + 1 隐藏聚合指针 = 129 物理参数，两处上限未统一表达。
- 最小修复：统一「源参数 128 + 隐藏指针 1」的上限语义，或区分 source/physical 计数。
- 备注：疑似。

##### [中] 75-21｜op 25（Data）未校验 `ssa.extra` 非负
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3043`（`ssa.args[index] < 0 || ssa.args[index] + ssa.extra[index] > module.data_length`）
- 类别：未检查前置条件
- 触发条件：SSA 中 op 25 的 `extra` 为负（损坏 SSA；`KrtSsaValidate` 不校验 op 25）。
- 后果：检查不触发，生成 `data_base + args` 的数据指针，长度语义丢失，可能越界。
- 最小修复：增加 `ssa.extra[index] < 0` 检查。
- 备注：疑似（正常构建 extra >= 0）。

##### [中] 75-22｜`output.data_reloc_count` 未更新
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2566-2567`（只设置 `text_reloc_count/total_reloc_count/relocation_count`）
- 类别：契约破坏
- 触发条件：消费者读取 `output.data_reloc_count`。
- 后果：数据段重定位计数保持陈旧值，对象契约不完整。
- 最小修复：设置 `output.data_reloc_count = data_count;`。
- 备注：疑似；取决于对象校验器是否读取该字段。

##### [中] 75-23｜`output.rodata_reloc_count` 未重置/初始化，直接被 `KrtKroRuntimeDescriptors` 读取
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2544`（`int32 previous_data = output.rodata_reloc_count;`），而 `KrtKroLibraryFunctions` 在 `:2038-2039` 只设置 text 相关字段
- 类别：共享可变状态 / 契约破坏
- 触发条件：output 对象复用，或 `rodata_reloc_count` 未初始化。
- 后果：`previous_data` 非 0，触发 `:2556` 的错误复制路径。
- 最小修复：在 `KrtKroLibraryFunctions`/`KrtKroEmitSsaModule` 开始时重置 `output.rodata_reloc_count`（及 `data_reloc_count`）。
- 备注：与 75-6 相关但位置不同，独立列出。

##### [低] 75-24｜`KrtKroEmitLibraryData` 将 `name_length` 由 int64 截断为 int32 累加
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1923`（`names = names + (int32)declaration.name_length + 25;`）
- 类别：未检查前置条件 / 整数溢出
- 触发条件：声明名长度超过 `2^31-1`。
- 后果：容量估算被截断/溢出，字符串表分配不足，后续写入越界。
- 最小修复：先检查 `declaration.name_length` 范围，并用 int64 累加后再验证总容量。
- 备注：低（正常名字远小于该上限）。

##### [低] 75-25｜`relocation.kind` 未校验，未知取值被当作函数重定位
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2559-2560`（`kind == 0 ? ... : (kind == 1 ? ... : KrtKroRuntimeFind(functions, relocation.offset))`）
- 类别：未检查前置条件
- 触发条件：runtime 重定位的 `kind` 为 0/1/2 之外的值。
- 后果：被静默当作 kind 2（函数重定位），查表失败则整体失败，或错误解析。
- 最小修复：显式处理 0/1/2，其余 `return false`。
- 备注：低；疑似。

### 第七十六批：SelfHost/Compiler/Frontend/Semantic/Semantic.krt（1-1276）

#### 覆盖范围

- 审计对象：`/sdcard/KairoteLang/SelfHost/Compiler/Frontend/Semantic/Semantic.krt` 第 1-1276 行（为补全 1270 行起始的 `KrtNativeBindConstructor` 函数头，实际读到 1280 行；全文件 2552 行，1277-2552 不在本批）。
- 读取方式：仅用 read 分段读取（1-220、221-460、461-700、701-940、941-1180、1181-1280），未整文件通读其他源文件。
- 交叉核对：仅用 grep 核对 SelfHost 内特定符号（`KrtNativeBindContextExpression`、`KrtNativeRejectNamedArguments`、`KrtNativeCommitArguments`、`KrtNativeAccessError`、`KrtNativeInlineValue`、`KrtNativeLayoutProbe`、`KrtNativeValidateControlFlow`、`op == 247` 等）。Re.KrtC 的 C 版 `SemanticAnalyzer.c` 未能定位（glob 超时、bash 沙箱不可用），因此凡涉及跨实现分叉的判断均标「疑似」。
- 本批主题：原生类型标注（`KrtNativeAnnotateType*`）、类型兼容/评分（`KrtNativeCompatibleValue`、`KrtNativeArgumentTypeScore`）、调用绑定与重载选择（`KrtNativeBindCall`）、表达式绑定（`KrtNativeBindExpression`）、语句绑定（`KrtNativeBindStatement`）、函数绑定（`KrtNativeBindFunction`）、数组字面量与构造器入口。


##### [高] 76-1｜指针/数组 cast 分支对 node.left 无 NULL 防护，缺失操作数时解引用空指针
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:734`、`:740`、`:742`、`:746`
- 类别：未检查前置条件（NULL）
- 触发条件：`Cast` 节点满足 `KrtNativePointerValue(node) || KrtNativePointerValue(node.left)` 或 `node.native_array`，且 `node.left == null`。`KrtNativeBindExpression` 在 `:583` 明确对 `node == null` 返回 true，因此 `cast<T*>()`、`cast<T[]>()` 这类缺操作数节点可被绑定到该分支。
- 后果：`:734` 的 `scalar.kind`（scalar 为 null）、`:740` 的 `node.left.native_fixed_length`、`:742` 的 `KrtNativeArrayElement(node.left)`、`:746` 的 `node.left.native_type` 均会空指针解引用，编译器崩溃。
- 最小修复：在进入 `node.kind == Cast && (指针/数组)` 分支前统一判 `node.left == null` 并返回 `KrtNativeBindingError(binding,node)`；或对上述四处逐一举指。
- 备注：疑似可达——取决于 parser 是否保证 Cast 必有操作数；但 `:583` 的显式空容忍使该前置条件在本文件内未被守住。

##### [高] 76-2｜构造器每次绑定都克隆并写回 unit.left，重复绑定叠加克隆且污染 AST
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1117`（关联 `:1149`）
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：`unit.op == 220`（构造器）时执行 `unit.left = KrtNativeCloneAst(unit.left, unit.native_owner, false)`。同一构造器被第二次 `KrtNativeBindFunction`（模块重绑定、泛型特化、错误后重试）时，`unit.left` 已是上一轮克隆体。
- 后果：AST 被就地改写，克隆逐次嵌套/膨胀；后续阶段（错误定位、代码生成）看到的是克隆体而非原始声明；绑定失败时 `unit.native_bound=false`、`unit.native_locals`、`unit.declarations` 仍被写入（`:1149`），形成部分生效状态。
- 最小修复：把克隆体放在绑定局部变量中，不写回 `unit.left`；或在入口用 `unit.native_bound`/`unit.native_cloned` 缓存，避免重复克隆。
- 备注：构造器克隆意图（避免绑定副作用污染原 AST）本身合理，问题在写回与缺缓存。

##### [高] 76-3｜syscall 回退路径不校验参数类型，且返回前不给 Call 节点设置结果类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:485-492`
- 类别：契约破坏 / 诊断缺失
- 触发条件：调用名 `syscall`、非成员调用、参数个数 1-7，且重载选择 `selected == null`（用户未定义同名函数）。
- 后果：`:488` 仅检查数组字面量是否带类型元素，对参数的实际类型（如给需要指针的 syscall 传字符串/浮点）无任何诊断；`return KrtNativeRejectNamedArguments(node.right, binding)` 后 `node.native_type`、`node.native_width` 等保持未设置状态。若 syscall 作为表达式使用（如 `int32 r = syscall(1);`），下游将读到未初始化类型。
- 最小修复：为该路径设置 Call 的结果类型（或显式声明为 void/未知并加诊断）；对参数做基本 ABI 类型校验或至少设置 `binding.error_start`。
- 备注：疑似——若 `KrtNativeRejectNamedArguments`（`:1419`）内部或调用方后续补类型，则后果降级；本文件内看不到补类型动作。

##### [中] 76-4｜空类型标注 length<=0 静默成功，留下半初始化类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:88`
- 类别：静默失败 / 诊断缺失
- 触发条件：`KrtNativeAnnotateType(Core)` 收到 `length <= 0` 的类型跨度（Cast/Sizeof/DefaultValue/Variable 的 `type_start/type_length` 为空）。
- 后果：直接 `return true`，节点保留 `native_width=8`、`native_bits=0`、`native_type=null`、`native_string=false` 等默认值；`sizeof` 得到 0 存储、`cast<>` 之类被当作无类型整数处理，全程无错误码。
- 最小修复：`length <= 0` 时返回 `KrtNativeBindingError(binding,node)` 并由调用方设置模块错误码；或要求调用方在调用前判空。
- 备注：与 76-5 同源（默认宽度/位数未区分「未标注」与「int64」）。

##### [中] 76-5｜var/let/auto/void 不解析类型，落入默认 width=8、bits=64，被当 int64 处理
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:120-128`
- 类别：静默失败 / 语义分叉
- 触发条件：类型名跨度为 `var`/`let`/`auto`（`:120-122` 把它们排除在 `KrtNativeResolveNamedType` 之外）或 `void`（同样不匹配任何宽度分支）。
- 后果：`width` 保持 `:106` 的初值 8，`:128` 因 `native_bits==0 && native_float==0 && !native_string && native_type==null` 写入 `native_bits=64`。于是 `cast<var>(x)`、`sizeof(var)` 被静默当作 int64；`void` 节点同时带 `native_void_pointer=true` 与 `native_bits=64`，而 `KrtNativePointerCastInteger`（`:255-259`）只检查 `native_bits>0`，不检查 `native_void_pointer`，存在把 void 当整数标量的风险。
- 最小修复：对 `var/let/auto/void` 分别显式处理（推断类型走上下文、void 走 void_pointer），不要在 `:128` 兜底写 bits；`KrtNativePointerCastInteger` 增加 `!native_void_pointer`。
- 备注：`var` 在 Variable 分支会被 `:1004` 覆盖，但 Cast/Sizeof 路径不覆盖，故仍可触发。

##### [中] 76-6｜KrtNativeArgumentScore 直接索引 parameter_refs[index]，未校验 index 范围
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:302-304`
- 类别：未检查前置条件（越界）
- 触发条件：`index` 来自 `argument.native_argument_index`（`:468`），而该值由 `KrtNativeMapArguments` 写入。若映射契约被破坏（扩展方法、可变参数、复用/克隆的 AST 节点带旧 index），`index >= function_node.parameter_count` 或为负。
- 后果：`function_node.parameter_refs[index]` 越界读，读到垃圾 `bool` 作为 `reference`，导致评分错误甚至后续按错误引用方式提交参数。
- 最小修复：函数入口断言 `index >= 0 && index < function_node.parameter_count`，越界直接返回 -1 并置 `candidate_error`。
- 备注：疑似——正常路径下 `KrtNativeMapArguments` 会写入合法索引，需核对该函数契约。

##### [中] 76-7｜参数签名失败只识别错误码 10，其他错误被静默丢弃
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:307-310`
- 类别：错误处理不一致 / 诊断缺失
- 触发条件：`KrtNativeSignatureType` 失败且设置 `module.native_error` 为 5（可空性）等非 10 值。
- 后果：只在 `== 10` 时记录 `candidate_error`，其余错误码被 `previous_error` 覆盖回滚，`candidate_error` 保持 0；最终 `:495` 只能报 4（未找到），真实的可空性/签名错误被吞。
- 最小修复：把 `binding.module.native_error` 非 0 的情况都记入 `candidate_error`（或至少区分 5/10），再恢复 `previous_error`。
- 备注：同模式在 `:448-452`（MapArguments 失败只认 10）重复出现。

##### [中] 76-8｜函数索引缺失时把 native_error 清 0 后返回失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:426`、`:483`
- 类别：诊断缺失 / 静默失败
- 触发条件：`binding.module.native_function_index == null || !ok`。
- 后果：显式 `binding.module.native_error = 0` 后 `return KrtNativeBindingError(...)`。若上游按 `native_error` 判定「无错误」，此失败被当作成功；即便按返回值处理，错误类别也是 0（未知），且 `:426` 发生在参数绑定（`:420-424`）之后，错误位置语义混乱。
- 最小修复：为该失败设置明确的模块错误码（如「内部：函数索引缺失」），不要写 0；并尽早检查索引，避免先绑定实参。
- 备注：两处重复同一模式，属复制粘贴型缺陷。

##### [中] 76-9｜found_mapping 为真时掩盖参数名错误，多候选失败只报最后一个
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:448-451`、`:493`
- 类别：诊断缺失（多错只报一个）
- 触发条件：多个候选重载，其中一个 `KrtNativeMapArguments` 成功（`found_mapping=true`）但未入选，另一个候选留下 `invalid_name`。
- 后果：`:493` 的 `!found_mapping && invalid_name != null` 不成立，参数名错误被吞，最终只报通用错误；`invalid_name` 也只保留最后一次赋值，无法定位全部问题。
- 最小修复：区分「映射失败原因」与「是否入选」，对每个失败候选记录错误；报告时优先报参数名错误。
- 备注：重载诊断 Common 弱点，但此处逻辑条件可证明会吞错。

##### [中] 76-10｜重载歧义仅对 owner.value==3 判等价，且报错无错误码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:475-478`、`:498`
- 类别：诊断缺失 / 语义分叉
- 触发条件：两个候选同 `scope_score/file_score/score`，但 owner 不是「值==3」或方法签名不同。
- 后果：`ambiguous=true` 后 `:498` 直接 `KrtNativeBindingError`，不设置 `module.native_error`，歧义被报成通用「未找到」；另外把 `owner.value==3` 且签名相同视为等价（`equivalent`）会静默选一个，可能掩盖真实歧义。
- 最小修复：为歧义设置专门错误码并在诊断中列出候选；`equivalent` 判定需同时核对返回类型/泛型参数。
- 备注：`owner.value==3` 的语义需与 C 版对照确认，标疑似。

##### [中] 76-11｜接口实现缺失/抽象实现时无错误码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:499-503`
- 类别：诊断缺失
- 触发条件：`selected.native_owner.value == 3`（对象/接口语义）且显式成员调用，`KrtNativeFindInterfaceImplementation` 返回 null 或 `implementation.native_abstract`。
- 后果：仅 `KrtNativeBindingError`，`module.native_error` 不设置，错误退化为通用失败，调用点拿不到「未实现/抽象方法」信息。
- 最小修复：设置专门错误码（如未实现接口方法）并保留 `error_start` 指向调用节点。
- 备注：与 C 版接口分派路径是否一致需对照确认，标疑似。

##### [中] 76-12｜隐式 this 解析失败时直接 return false，无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:528-531`
- 类别：诊断缺失
- 触发条件：非静态实例方法被当作普通函数调用，走到 `callee.left = KrtNativeThis(binding, node.start)`，而 `KrtNativeThis` 返回 null。
- 后果：`if (callee.left == null) { return false; }` 未调用 `KrtNativeBindingError`，`binding.error_start` 可能仍为 -1，上游无法定位；与 `:439` 的 `receiver_valid`（已要求 `KrtNativeHasInstanceContext`）判断不一致，说明存在判断口径分叉。
- 最小修复：改为 `return KrtNativeBindingError(binding, node)`，并统一 `HasInstanceContext` 与 `ClosureThis` 的判定。
- 备注：疑似可达——正常路径下 `receiver_valid` 已挡住无上下文情形。

##### [中] 76-13｜KrtNativeBindExpression 对 null 节点返回 true，缺失操作数被静默接受
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:583`（关联调用点 `:725`、`:750`、`:760`、`:1043`、`:1003`）
- 类别：静默失败 / 未检查前置条件
- 触发条件：任何以可能为 null 的子节点调用 `KrtNativeBindExpression` 的路径，如 Cast/Sizeof 缺操作数、Index 缺左/右、Unary 缺操作数、Assign 缺 RHS、Variable 缺初始化器。
- 后果：`:583` 直接 `return true`，把「节点缺失」与「绑定成功」等同；与 76-1 组合会进一步变成空指针解引用。调用点普遍不额外判空。
- 最小修复：区分「合法空节点」（如 DefaultValue）与「非法缺失」，对非法路径返回 `KrtNativeBindingError`；至少在所有调用点补 `node.left == null` 检查。
- 备注：`:582` 的 `op == 247` 提前返回是已绑定全局的正常路径（`op=247` 由 `SemanticFeatures.krt:1329` 写入），不要与本条混淆。

##### [中] 76-14｜无实例上下文时 this(op 211) 仍静默成功并赋予 owner 类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:621-624`
- 类别：静默失败 / 契约破坏
- 触发条件：标识符解析不到名字（`:601` name == null），且 `node.op == 211`（this），但 `KrtNativeHasInstanceContext(binding)` 为假（如静态方法/全局函数中写 this）。
- 后果：`:623` 直接 `node.native_type = binding.unit.native_owner; return true;`，既不做实例上下文检查，也不调用 `KrtNativeCheckValueAccess`；当 owner 为 null 时节点类型为 null 仍返回成功。
- 最小修复：`:623` 前加 `KrtNativeHasInstanceContext` 判断，否则 `KrtNativeBindingError`；并补 `KrtNativeCheckValueAccess`。
- 备注：正常 parser 路径会先命中 `:598` 的 op 211/212 分支，本条针对解析顺序异常/复用节点。

##### [中] 76-15｜整数字面量越界无范围检查，赋值兼容性对标量不比较位数
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:634`、`:1010`（关联 `KrtNativeCompatibleValue:914-942`）
- 类别：诊断缺失 / 静默失败
- 触发条件：`int8 x = 300;` 这类常量初始化/赋值。`:634` 只按值域设 width（0..2^31-1 → 4），不检查目标类型；`:1010` 的 `KrtNativeCompatibleValue` 对纯标量（`native_type==null`、stride 0）在 `:940` 后直接 `return true`，不比较 width/bits/float/unsigned。
- 后果：超出目标类型范围的常量被静默接受，后续代码生成按截断值处理，无任何编译期诊断。
- 最小修复：在 Variable/Assign 对已知常量值做目标类型范围检查；或让 `KrtNativeCompatibleValue` 对标量比较 width/bits/float 域（需评估对隐式转换的影响）。
- 备注：疑似——需确认 `KrtNativeBindContextExpression`（`SemanticFeatures.krt:526`）是否已在更早阶段插入转换/检查；本文件内看不到该检查。

##### [中] 76-16｜字段类型解析依赖全局可变 KrtNativeLayoutProbe 与 layout_state
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:672`（符号定义 `Types.krt:1944`，保存/恢复 `Types.krt:2016-2018`）
- 类别：共享可变状态
- 触发条件：`settled = !KrtNativeLayoutProbe && type.native_layout_state == 2 && ...`。布局探测把全局 `KrtNativeLayoutProbe` 置 true 期间，所有字段访问走 `KrtNativeAnnotateType` 分支而非 `CopyType` 分支。
- 后果：全局标志与每个类型的 `native_layout_state` 共同决定字段类型来源；探测路径若异常退出未恢复（`Types.krt:2016-2018` 的保存/恢复是否覆盖所有 return 需确认），会静默改变后续所有字段解析结果，且与具体出错位置无关。
- 最小修复：把探测状态放进 binding/上下文参数传递，避免全局变量；确保所有提前 return 都恢复。
- 备注：跨文件状态，标疑似；本文件内可确认 `settled` 同时受全局与 per-type 状态影响。

##### [中] 76-17｜KrtNativeCompatibleValue 对 value==null 直接返回 true，缺 RHS 赋值静默通过
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:916`（关联 `:1091`）
- 类别：静默失败 / 契约破坏
- 触发条件：`KrtNativeCompatibleValue(target, null)`；调用点 `:1091` 的赋值在 `statement.right` 为 null（缺 RHS）时命中。
- 后果：缺 RHS 的赋值被判定为兼容并返回成功；同时 `:916` 也使任何把 null 当「未知值」的调用点失去检查机会。
- 最小修复：调用点先判 `statement.right == null` 并报错；或让 `CompatibleValue` 对 null value 返回 false，由真正允许 null 的调用点显式放行。
- 备注：疑似可达——取决于 parser 是否允许缺 RHS；`:1003`/`:1056` 等调用点对 null 是刻意容忍。

##### [低] 76-18｜KrtNativeCompatibleValue 中 value.kind == Null 判断为死代码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:931`
- 类别：死代码
- 触发条件：任何调用。
- 后果：`:917` 已对 `value.kind == KrtAstKind.Null` 返回，`:931` 的同一判断不可达，属复制粘贴残留，误导读者以为该分支参与逻辑。
- 最小修复：删除 `:931`。
- 备注：纯死代码，无行为影响。

##### [中] 76-19｜推断类型 lambda 的「声明类型」在绑定后被 CopyType 覆盖，991-995 结果丢弃
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:991-995`、`:1004`
- 类别：注释与实现脱节 / 死代码
- 触发条件：`inferred && statement.left.kind == Lambda && statement.left.type_length > 0`，`:993` 把 `KrtNativeClosureDeclaredType` 的结果 CopyType 到 statement。
- 后果：`:1003` 绑定 lambda（非递归推断时 expected 传 null），`:1004` 又 `KrtNativeCopyType(statement, statement.left)`，把刚写入的声明类型覆盖为 lambda 自身类型；`declared` 分支实际无效，且 `:994` 的 `error_start` 检查在覆盖前发生，错误处理与类型写入脱节。
- 最小修复：明确二选一——要么用声明类型作 expected 并不再覆盖，要么删除 `:991-995`。
- 备注：疑似——若 lambda 类型与声明类型恒一致则无行为差异，但逻辑上仍是死分支。

##### [中] 76-20｜delete 函数指针仅分配 4 个 slot 即返回成功，delete null/array 静默成功
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1046-1049`
- 类别：语义分叉 / 契约破坏
- 触发条件：`delete p`，p 为函数指针（`native_function_pointer != null && !array && pointer_depth==0`）；或 `delete null`、`delete 数组`、`delete 引用对象`。
- 后果：`:1047` `binding.slots += 4` 后直接 return true，无释放语义校验、无诊断；`:1049` 对 null/数组/引用对象直接成功。`4` 这个魔数与 `binding.slots` 的关系无注释，疑似占位实现。
- 最小修复：明确 delete 的合法操作数集合；对函数指针删除给出定义或诊断；魔数 4 提取为具名常量并注释。
- 备注：疑似——4 slot 可能是下游 codegen 约定，需与代码生成对照。

##### [中] 76-21｜Return 仅在 unit.type_length>0 时校验，构造器等无返回类型标注时 return 值不检查
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1057`（关联 `:1052-1063`）
- 类别：诊断缺失
- 触发条件：`binding.unit.type_length == 0`（构造器 op 220、或未标注返回类型的函数），函数体内出现 `return expr;`。
- 后果：跳过 `is_void`、可空性、`CompatibleValue` 全部检查，`return 5;` 在构造器中静默通过。
- 最小修复：对 type_length==0 的单元推导其逻辑返回类型（构造器为所属类型）或明确禁止带值 return 并报错。
- 备注：疑似——构造器返回类型可能由别处补标，需核对 `unit.type_start/type_length` 在构造器上的取值。

##### [中] 76-22｜数组字面量全 null / 空字面量被赋予默认元素类型，无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1235-1248`
- 类别：静默失败（默认值）
- 触发条件：调用实参中的全 null 字面量（如 `f([null, null])`）走 `:1237`（item_type 仅设 `native_width=8`，bits/type 全默认）；空字面量 `[]` 走 `:1248`（默认 int32）。
- 后果：实参类型在未提供任何类型信息时被静默定为 int64/int32，可能与形参不符而在后续以通用错误暴露，也可能碰巧匹配而隐藏用户错误；`:1235` 只对非 argument 上下文报可空错误，argument 上下文完全放行。
- 最小修复：argument 上下文的默认类型应改为「未知」并强制后续 CommitArguments 精确匹配，或在此处对全 null/空字面量给出可空性诊断。
- 备注：疑似——若 `KrtNativeCommitArguments`（`:1427`）会用形参类型回填则后果有限。

##### [中] 76-23｜常量绑定失败路径不设模块错误码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:203`、`:214`
- 类别：诊断缺失 / 错误处理不一致
- 触发条件：常量声明无初值或 `constant_depth >= 64`（`:203`）；或 `KrtIrTryEvaluateConstant` 失败（`:214`，如常量表达式含非常量操作）。
- 后果：两处均只 `return KrtNativeBindingError(binding, node)`，不设置 `module.native_error`；错误类别为 0/未知，且 `:203` 把「递归深度超限」与「缺初值」混为同一通用错误。
- 最小修复：分别设置错误码（缺初值 / 常量递归过深 / 非常量表达式），并保持 `error_start` 指向声明节点。
- 备注：与 76-7、76-8 同属「BindingError 不设错误码」模式。

##### [中] 76-24｜全局 abstract/virtual/op223 函数绑定失败无诊断即返回 false
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1116`
- 类别：诊断缺失
- 触发条件：`unit.native_owner == null && (unit.native_abstract || unit.native_virtual || unit.op == 223)`，即全局作用域出现抽象/虚函数或 op 223 节点。
- 后果：直接 `return false`，不设置 `module.native_error` 也不设 `error_start`（若此前为 -1），上游只能得到「绑定失败」而无语义信息；同时该单元 `native_bound` 未被写入（保持默认），状态不一致。
- 最小修复：改为 `KrtNativeBindingError(binding,unit)` 并设置专门错误码；或先初始化 binding 再报错。
- 备注：疑似——需确认 op 223 语义与全局 abstract/virtual 是否由更前阶段拦截。

##### [低] 76-25｜KrtNativeArrayLiteralHasTypedElement 命名与实现脱节
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1195-1202`
- 类别：注释与实现脱节
- 触发条件：任何调用。
- 后果：函数名声称判断「是否有已类型化元素」，实现只判断是否存在 `kind != Null` 的元素；未类型化的标识符/表达式也会返回 true。调用点 `:338`、`:488` 据此决定精确匹配与 syscall 实参检查，语义与命名不符，易被后续维护者误用。
- 最小修复：改名为 `HasNonNullElement`，或实现真正的「元素已标注类型」判断。
- 备注：纯命名/可读性问题，当前调用点行为可能正确。

##### [低] 76-26｜局部变量 index 定义后从未使用
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:465`、`:470`
- 类别：死代码
- 触发条件：扩展方法实参评分循环。
- 后果：`int32 index = extra;` 后循环体用 `argument.native_argument_index`（`:468`），`index` 只在 `:470` 自增，从未被读取；说明实现从「按位置索引」改为「按映射索引」时残留。
- 最小修复：删除 `index` 及其自增。
- 备注：纯死代码，无行为影响。

### 第七十七批：SelfHost/Compiler/Frontend/Semantic/Semantic.krt（1277-2553）

#### 覆盖范围

- 文件：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt`，审计行段 1277-2553（文件实际共 2552 行，1277 行起自 `KrtNativeBindConstructor` 函数体中段）。
- 覆盖内容：
  - 原生构造函数绑定尾部 `KrtNativeBindConstructor`（1277-1308）。
  - `KrtNativeBindForeach`、`KrtNativeContextArguments`、`KrtNativePointerOffset`、`KrtNativeSemanticStride`、`KrtNativeLiteralArgument`。
  - 命名实参映射/诊断：`KrtNativeHasNamedArguments`、`KrtNativeMapArguments`、`KrtNativeArgumentNameError`、`KrtNativeRejectNamedArguments`、`KrtNativeCommitArguments`。
  - 括号转换：`KrtNativeParenthesizedValueName`、`KrtNativeParenthesizedOperandCandidate`、`KrtNativeBindParenthesizedCast`。
  - 名字查找与重载：`KrtNativeNameFilePriority`、`KrtNativeImportedFile`、`KrtNativeUsingMatches`、`KrtNativeVisibleUsingName`、`KrtNativeVisibleQualifiedName`、`KrtNativeFindUsingType`、`KrtNativeFindScopedType`、`KrtNativeExpressionTypeName`、`KrtNativeNamespaceCallable`、`KrtNativeFunctionScopeScore`。
  - 函数名索引：`KrtNativeFunctionNameHash`、`KrtNativeFunctionBucketSlot`、`KrtNativeFunctionIndexGrow`、`KrtNativeFunctionIndexAppend`、`KrtNativeFunctionIndexSynchronize`、`KrtNativeFunctionCandidates`、`KrtNativeFunctionCandidateNext`、`KrtNativeFunctionIndexInvalidate`。
  - legacy 语义段：`KrtSemanticError`、`KrtSymbol`、`KrtScope`、`KrtSemanticValidateBlockShape`、`KrtSemanticValidateIfShape`、`KrtSemanticValidateLoopControl`、`KrtSemanticValidateFunction`、`KrtSemanticSameName`、`KrtSemanticBindExpression`、`KrtSemanticFindFunction`、`KrtSemanticResolveCallExpression`、`KrtSemanticBindNestedExpression`、`KrtSemanticBindNestedBlock`、`KrtSemanticResolveCallsInBlock`、`KrtSemanticResolveModuleCalls`、`KrtSemanticBindLocals`、`KrtSemanticValidateCompilationUnit`、`KrtSemanticValidateFunctionNames`、`KrtSemanticValidateEntryPoint`、`KrtSemanticIsMain`、`KrtSemanticOrderHelperFirst`、`KrtSemanticInlineConstantCall`、`KrtSemanticValidateConstantHelperCall`、`KrtSemanticValidateConstantArgumentCall`、`KrtScopeLookup`。
  - 控制流/异常/类型测试：`KrtNativeFloatKind`、`KrtNativeWideKind`、`KrtNativeValidateControlFlow`、`KrtNativeExceptionType`、`KrtNativeBindThrow`、`KrtNativeBindTry`、`KrtNativeBindTypeTest`、`KrtNativeBindTruePatterns`。
- 调用方核对（仅 grep）：`CompilerPipeline.krt:351-388`、`Types.krt:300/303/332/360/374/391/567/823/950/1217`、`SemanticRuntime.krt:652/374`、`Ir.krt:2341/2800/2912`、`Parser.krt:371-396/613-627/1462-1491/1547-1568`。
- 说明：`Re.KrtC` 的 `SemanticAnalyzer.c` 未能在本机定位（`glob`/`bash` 不可用），涉及跨实现对照的条目一律标「疑似」。


##### [高] 77-1｜KrtNativeFindUsingType 把不可见声明作为查找结果返回
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1621`、`:1647`、`:1656`
- 类别：静默失败 / 契约破坏（访问控制）
- 触发条件：按名字查找类型时，候选声明的 `KrtNativeNameFilePriority` 为 0（file-private 位于其他文件、或 internal 位于其他模块），且不存在任何可视候选（priority>0）。该声明被写入 `hidden`，函数末尾 `return hidden`。
- 后果：本文件不可见的声明被当作“查找成功”返回。`KrtNativeExpressionTypeName`（:1680）会二次 `KrtNativeAccessAllowed`，但 `Types.krt:567`、`Types.krt:823`、`SemanticRuntime.krt:652` 直接调用 `KrtNativeFindUsingType`，绕过访问检查，形成跨文件私有/跨模块内部类型泄漏。
- 最小修复：把“找到但不可见”和“未找到”分成两个结果（返回 null 或带 visible 标志）；若确需 hidden 用于诊断，也必须在返回前调用 `KrtNativeAccessAllowed`，并让调用方无法忽略该状态。
- 备注：priority 定义见 :1533-1547；`KrtNativeAccessAllowed` 见 `SemanticFeatures.krt:92`。

##### [高] 77-2｜KrtSemanticResolveCallExpression 未定义函数被静默接受
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2044-2047`
- 类别：静默失败 / 诊断缺失
- 触发条件：`foo(...)` 中 `foo` 不在 `module.left` 中，`KrtSemanticFindFunction` 返回 null；此时 `target != null && ...` 的 arity 检查被整体跳过，函数继续绑定实参并在 :2055 `return true`。
- 后果：未定义函数调用在该校验路径下静默通过。成员调用分支（:2046）只绑定接收者 `expression.left.left`，从不解析方法名，`obj.undefined()` 同样通过；而 `Namespace.foo()` 这类限定调用又会因接收者标识符未定义被误拒，校验器内部不一致。
- 最小修复：`target == null` 时返回 false（UndefinedName）；成员调用增加方法名解析或明确的命名空间判定。
- 备注：该函数属 legacy 常量对象路径（`CompilerPipeline.krt:361/366` 在 `!ok` 分支调用）；native 主路径可能另有检查，故未定致命。

##### [高] 77-3｜KrtSemanticBindNestedExpression 忽略声明顺序与嵌套作用域
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2082-2088`
- 类别：语义分叉 / 静默失败
- 触发条件：嵌套块内引用“在其后声明”的变量，或引用兄弟块/内层块声明的变量。实现从 `unit.left.left` 线性扫描整个函数体，命中同名 `Variable` 即 `return true`，不判断该声明是否位于当前语句之前、是否在当前作用域。
- 后果：使用未初始化变量、越作用域引用被接受；legacy 路径（`CompilerPipeline.krt:383-386`）会据此生成 IR/KRO，产生错误代码。与 `KrtSemanticBindExpression`（:1983-1991）“只认当前语句之前声明”的规则构成同一语言两套绑定语义。
- 最小修复：改用作用域栈，只允许当前块及外层块、且仅允许已声明（位置在前）的变量；或复用 `KrtSemanticBindExpression` 的作用域规则。
- 备注：`KrtSemanticBindNestedBlock`（:2110-2126）是主要调用入口。

##### [中] 77-4｜KrtSemanticValidateLoopControl 漏判 switch/try/for/foreach/dowhile 及 else-if 深链
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1905-1913`
- 类别：诊断缺失
- 触发条件：(a) 函数顶层 `switch` 内的 `break`：`Switch` 未被处理，函数直接 `return true`；(b) `else if` 链中第二层及以后的 `alternate`：:1911-1913 只递归 `statement.alternate.right`，不递归 `alternate.alternate`；(c) `For/Foreach/DoWhile/Try` 体内的非法 `break/continue` 同样不会被遍历。
- 后果：非法 `break/continue` 在该校验器下静默通过。完整实现见 `KrtNativeValidateControlFlow`（:2419-2435），两处阶段不对齐。
- 最小修复：对齐 `KrtNativeValidateControlFlow` 的语句集合；else-if 分支改为循环/递归处理整条 `alternate` 链。
- 备注：`KrtSemanticValidateFunction` 在 :1926 强制调用本函数。

##### [中] 77-5｜KrtSemanticValidateBlockShape 语句白名单过窄，拒绝合法程序
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1884`（`else { return false; }`）
- 类别：阶段不对齐 / 死代码
- 触发条件：函数体含 `For`、`Foreach`、`DoWhile`、`Switch`、`Try`、`Throw`、`Point` 等语句。这些语句在原生绑定中存在（`Semantic.krt:972-974`、`:1022-1025`、`:2403-2435`），但本白名单只认 If/While/Unsafe/Delete/Break/Continue/Variable/Return/Assign/Expression。
- 后果：合法程序被 legacy 校验器判为非法，只能回退到 native 路径；若该 legacy 路径是唯一/首选路径则造成误拒。
- 最小修复：补齐语句种类，或删除该 legacy 校验器统一走 native。
- 备注：`KrtSemanticValidateFunction`（:1922-1959）同样只覆盖部分语句 kind。

##### [中] 77-6｜KrtSemanticValidateFunction / ValidateCompilationUnit 存在无依据的魔数上限
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1924`（`parameter_count > 8`）、`:2211`（`child_count > 256`）
- 类别：配置漂移 / 语义分叉
- 触发条件：函数参数多于 8，或编译单元顶层函数多于 256。
- 后果：legacy 路径直接拒绝；VM 校验允许参数至多 128（`Interpreter.krt:57`），两套限制不一致。同一语言在不同路径下能力不同。
- 最小修复：统一为共享常量并注释来源，或移除 legacy 限制。
- 备注：疑似。需与 `Re.KrtC/SemanticAnalyzer.c` 对照确认 8/256 是否为语言契约。

##### [中] 77-7｜KrtSemanticInlineConstantCall 返回值恒真且丢弃实参
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2299-2324`（恒 true 见 :2300/:2303/:2306/:2323；替换逻辑 :2314-2318）
- 类别：死代码 / 契约破坏 / 静默失败
- 触发条件：`main` 首语句为 `return helper(...)`，`helper` 首语句为 `return <整数>`。实现未检查 `returned.left.right == null`（无实参）、未检查 `helper_function.parameter_count == 0`，直接用 helper 的整数常量替换调用节点。
- 后果：(1) 函数声明返回 `bool` 但所有路径都返回 true，`CompilerPipeline.krt:380` 的 `&&` 契约失去意义；(2) 带实参的调用被替换为常量，实参被静默丢弃；(3) 只取 helper 首语句，后续语句被忽略。
- 最小修复：校验调用无实参且 helper 无参数；未发生内联时返回 false；只取首语句时明确校验 `child_count == 1`。
- 备注：与 `KrtSemanticValidateConstantHelperCall`（:2326-2340）的契约不对称。

##### [中] 77-8｜KrtSemanticValidateConstantHelperCall 未校验 helper 参数个数
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2326-2340`
- 类别：契约破坏
- 触发条件：helper 带参数、首语句返回整数常量，`main` 为 `return helper()`。本函数只检查 `main_return.left.kind == Call` 与 helper 返回 `Integer`，不检查 `helper_function.parameter_count`。
- 后果：带参 helper 被当作“常量 helper”，与参数版 `KrtSemanticValidateConstantArgumentCall`（:2347 明确要求 `parameter_count == 1`）规则不一致，常量对象语义被破坏。
- 最小修复：增加 `helper_function.parameter_count == 0` 前置条件。
- 备注：对比行 :2347。

##### [中] 77-9｜KrtSemanticIsMain 只匹配名字，不校验入口签名
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2267-2277`
- 类别：契约破坏 / 未检查前置条件
- 触发条件：名为 `main` 但带参数、带非 void 返回类型、或位于命名空间/类型作用域内的函数（只排除了 `native_owner != null`）。
- 后果：`KrtSemanticValidateEntryPoint`（:2256-2265）与 `Ir.krt:2341` 的入口选择把它当入口点，入口契约（参数个数、返回类型、全局作用域）未校验。
- 最小修复：校验 `parameter_count == 0`、返回类型与 `native_scope` 深度。
- 备注：疑似，取决于语言对 `main` 的约定；`CompilerPipeline.krt:355` 也用本函数。

##### [中] 77-10｜KrtNativeBindTry 错误早退未恢复共享绑定状态
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2486`（保存/设置）、`:2490-2495`（早退）、`:2499`（成功路径恢复）
- 类别：共享可变状态 / 错误处理不一致 / 资源与生命周期
- 触发条件：catch 子句类型标注失败（:2490-2491）、`KrtNativeBindAggregateFrame` 失败（:2494）、`KrtNativeAddName` 失败（:2495）。这三处直接 `return`，未恢复 `binding.names`、`binding.depth`、`binding.catch_target`。
- 后果：错误恢复或后续诊断在被污染的 `binding` 上进行；`catch_target` 残留指向当前 try，可能让后续 `throw`（:2469-2470）绑定到错误目标。成功路径 :2499 有恢复，失败路径缺失，处理不一致。
- 最小修复：改为单一出口，或在每个失败 return 前恢复 `saved`/`saved_catch`。
- 备注：`binding.names/depth/catch_target` 均为共享可变字段。

##### [中] 77-11｜KrtNativeFunctionIndexGrow 达 65536 后静默禁用索引
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1753`
- 类别：静默失败 / 配置漂移
- 触发条件：不同函数名数量增长到使容量翻倍超过 65536（负载因子 0.5，约 32768 个不同名字）。
- 后果：`index.ok = false`，`KrtNativeFunctionIndexSynchronize`（:1806）返回 false，`KrtNativeFunctionCandidates`（:1816）返回 null，所有调用解析拿不到候选，报“无候选/未定义”而非资源上限。
- 最小修复：返回明确错误码并向上报告；或允许更大容量/分级索引。
- 备注：:1751-1770。

##### [中] 77-12｜KrtNativeNameFilePriority 每次调用分配临时 AST 节点且不释放
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1545-1546`
- 类别：资源与生命周期
- 触发条件：每次计算文件优先级（每次名字查找、每个候选声明）都 `KrtAstMake(KrtAstKind.Function, position, 0)`，仅用于把 `position` 传给 `KrtNativeAccessSameFile`，随后不 `delete`。
- 后果：累计内存泄漏；`KrtNativeAccessSameFile` 只读取 `caller.start`（`SemanticFeatures.krt:38-42`），该分配完全多余。
- 最小修复：改为传递位置值/栈上结构，避免分配节点。
- 备注：调用频率随候选数放大。

##### [中] 77-13｜KrtNativeExceptionType 以 -1 静默表示失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2444-2451`
- 类别：静默失败 / 错误处理不一致
- 触发条件：`type` 为 void/数组/带 stride 指针，或 `type.native_type` 的声明不在 `module.declarations` 中（跨模块声明、泛型来源未展开）。
- 后果：返回 -1。调用方 `Ir.krt:2800`、`Ir.krt:2912` 直接把返回值当 identity 使用，若未校验 -1，不同非法类型会互相“相等”。
- 最小修复：区分“无 identity”与错误；调用方校验 `>= 0`。
- 备注：疑似，取决于 Ir 侧是否过滤 -1。

##### [中] 77-14｜KrtNativeBindThrow 不校验异常类型、不记录类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2467-2476`
- 类别：契约破坏 / 诊断缺失
- 触发条件：`throw <expr>` 中表达式非 void/null 且 `KrtNativeBoxableValue` 为真，但并非合法异常类型（如函数指针或结构体）；或 rethrow（:2468-2470）时 `binding.catch_target` 的类型与当前 throw 无兼容性校验。
- 后果：本函数从不调用 `KrtNativeExceptionType`，也不设置 `native_target`/类型 identity；合法性完全推给下游，非法异常类型可能静默通过。
- 最小修复：调用 `KrtNativeExceptionType` 并校验返回值，记录类型 identity 供降级使用。
- 备注：与 :2443 的 helper 关系需确认。

##### [中] 77-15｜KrtNativeBindTruePatterns 只处理 `&&`(104)，漏 `||`(105)
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2548-2550`
- 类别：语义分叉 / 诊断缺失
- 触发条件：`if (a is T x || ...)` 这类在 `||` 中携带绑定名的类型测试。解析器允许在 `is` 类型后取标识符（`Parser.krt:1561`），但本函数只对 `op == 104` 递归，`op == 105` 直接落到 :2551 `return true`。
- 后果：`x` 未 `KrtNativeAddName`，条件为真时引用 `x` 报未定义；若函数体不引用该名字，则模式变量被静默丢弃，无任何诊断。
- 最小修复：显式拒绝 `||` 中的绑定模式（报诊断），或按短路语义注册并限制作用域。
- 备注：`104`=`&&`、`105`=`||`（`Parser.krt:1477-1478`）。

##### [中] 77-16｜KrtNativeFunctionIndexSynchronize 只同步尾部追加，结构变更即失配
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1798-1813`
- 类别：共享可变状态 / 契约破坏
- 触发条件：`module.left` 在中间插入、删除或重排函数（非尾部追加），且未调用 `KrtNativeFunctionIndexInvalidate`。同步逻辑只从 `last_indexed_function.next` 向后取新节点。
- 后果：索引与 `module.left` 不一致，查找静默漏掉函数或返回过期声明。
- 最小修复：任何结构变更都使索引失效，或为索引维护版本号/代次。
- 备注：疑似。:1796-1797 注释自称只处理 append；`Invalidate` 的调用点见 `Types.krt:950/1217`、`SemanticRuntime.krt:374`、`CompilerPipeline.krt:93`。

##### [低] 77-17｜KrtNativeValidateControlFlow 的 64 层上限为无注释魔数
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2420`、`:2430`
- 类别：配置漂移 / 静默失败
- 触发条件：嵌套 `switch`/循环深度达到 64。
- 后果：直接 `KrtNativeBindingError`，限制值无来源说明，与语言其他深度限制（如 `KrtNativeAccessAllowedDepth` 的 64）是否一致无法判断。
- 最小修复：常量化并注释，或统一到公共递归深度限制。
- 备注：低。

##### [低] 77-18｜KrtNativeCommitArguments 把位置参数错误报成“参数名错误”
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1428-1429`（配合 `:1398`）
- 类别：诊断缺失 / 错误处理不一致
- 触发条件：`KrtNativeMapArguments` 因位置参数索引越界或重复而返回非 null（:1398/:1401），调用方一律走 `KrtNativeArgumentNameError`，设置 `native_error = 9`。
- 后果：位置参数问题被诊断为命名参数问题，误导定位。
- 最小修复：区分命名与位置两类错误码。
- 备注：低。

##### [低] 77-19｜KrtSymbol/KrtScope/KrtScopeLookup/KrtSemanticError 为死代码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1849`（enum）、`:1851-1861`（类）、`:2358-2369`（函数）
- 类别：死代码
- 触发条件：全仓 grep 仅见定义处，无调用方（`KrtScopeLookup` 仅 :2358；`KrtSemanticError` 仅 :1849；`KrtSymbol/KrtScope` 仅被 `KrtScopeLookup` 使用）。原生绑定走 `KrtNativeName`/`KrtNativeBinding`，不使用这套符号表。
- 后果：维护负担与误导；`KrtScopeLookup` 用 `string == name` 比较，若 `==` 为引用比较还会产生正确性陷阱。
- 最小修复：删除，或接入实际符号表并补充 `symbols == null` 防护。
- 备注：低。

##### [低] 77-20｜KrtNativeBindConstructor 在无候选且无实参时静默成功
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1301-1307`（所在函数 `KrtNativeBindConstructor` 始于 :1270）
- 类别：静默失败 / 默认值
- 触发条件：构造节点未匹配到任何候选（`declared == false`）、`node.parameter_count == 0`、且非聚合默认（`implicit_struct_default == false`）时，:1302 条件为假，直接 `node.native_target = selected`（null）并 `return true`。
- 后果：未知/不匹配的构造被静默接受，无诊断，下游拿到空的 `native_target`。
- 最小修复：`selected == null && !implicit_struct_default` 时一律报错，或明确记录“隐式默认构造”语义并加注释/诊断。
- 备注：疑似（需确认调用方是否已保证候选存在）；聚合默认分支（:1301）本身也属“默认值降级”。

### 第七十八批：SelfHost/Compiler/Frontend/Parser/Parser.krt（1-1213）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Frontend/Parser/Parser.krt`，仅第 1-1213 行（分三段读取 1-400 / 401-820 / 821-1220，结论只落在 1-1213）。
- 覆盖内容：AST 节点与工厂（5-196）、声明前瞻（198-426，含 Fork/Commit/TypeCloser/各类 Ahead/类型参数）、名称与作用域工具（428-671，KrtName*/KrtScope*/KrtDeclarationMatches/AppendSyntax）、using 作用域（673-718）、Parser 状态与诊断（720-773）、for 增量与约束（775-845）、错误恢复（847-901）、函数类型/全局变量/基类型/基构造器（897-1011）、修饰符（1013-1038）、实参与 lambda 前瞻（1040-1088）、lambda 解析（1090-1165）、ParsePrimary 前半（1167-1213）。
- 交叉核对方式：仅用 grep 核对符号，未整文件通读其他源文件。已核对：`KrtParserTypeSpanLength`（1666-1670，确实从尾部剔除空格/制表/换行）、`KrtParserOperator`（1462）、`KrtParserParseExpression`（1570，体内无 `return null`）、`KrtParserParseCompilationUnit`（1960-1978）、`KrtParserParseBlock`（1635-1659）、Lexer 的 `Rshift`（Lexer.krt:531）、`KrtTokenKind.Lambda`（Lexer.krt:847）、CompilerPipeline 的数组释放（CompilerPipeline.krt:308-312）、Re.KrtC 的 `CompilerError.c:145` 与 `ParserStatement.c`。
- 未覆盖：1214-2424 行（表达式/语句/类型声明/函数体/编译单元主体）。


##### [中] 78-1｜顶层声明数量上限 16384 截断解析，且只报通用错误
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:473`（退出后 `:545` 返回；上游检测在 `:1978`）
- 类别：静默失败（诊断信息缺失）/ 配置漂移
- 触发条件：单个编译单元的顶层声明数超过 16384。
- 后果：`while (... && declaration_count < 16384)` 提前退出，剩余声明不再进入 AST；`KrtParserParseCompilationUnit`（1978）只以 `KrtParserRecordError` 报一条通用 `invalid syntax`，不说明是数量上限；若发生在嵌套命名空间内，则报的是 544 的「expected closing namespace brace」，位置与原因都误导。
- 最小修复：达到上限时记录带上限信息的诊断（如 `KrtParserRecordExpected(parser, KrtTokenKind.Eof, "declaration limit exceeded")`）并返回 false；上限值集中定义为常量。
- 备注：C 版 Parser 无此上限（疑似分叉）。同文件的 `unit.child_count > 16384`（1978）说明该上限在别处是会诊断的，此处诊断质量不一致。

##### [中] 78-2｜诊断达到 64 条后静默丢弃
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:751`
- 类别：静默失败 / 诊断缺失
- 触发条件：同一 `KrtParser` 累计错误数达到 64。
- 后果：`KrtParserRecordExpected` 直接 `return`，第 65 条及以后的错误不再进入 `diagnostics` 链，`parser.errors` 停在 64；调用方无法感知被丢弃的错误，最终诊断列表不完整。
- 最小修复：上限处追加一条「too many errors」汇总诊断，或继续递增 `errors`、只停止追加明细。
- 备注：C 版 `Re.KrtC/src/Compiler/Frontend/CompilerError.c:145` 按容量动态扩容，无固定 64 上限（疑似分叉）。

##### [低] 78-3｜诊断去重只比较位置，吞掉同位置的不同诊断
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:751`（`parser.diagnostic_tail.position == parser.token.start`）
- 类别：诊断缺失
- 触发条件：同一位置产生两条 `expected` 不同的错误；或 fork/commit 回退到同一位置后再次报错。
- 后果：第二条诊断被丢弃，真实原因被掩盖；probe 的 `errors` 计数偏小（调用方多以布尔判断，影响有限）。
- 最小修复：去重时同时比较 `expected` 与 `message`，或仅在完全相同时跳过。
- 备注：与 78-2 同一行但机制不同，独立条目。

##### [中] 78-4｜`KrtParserParseFunctionType` 错误路径泄漏参数数组
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:910-912`（分配），`:920/:930/:934/:936/:940`（返回 null）
- 类别：资源与生命周期（AST 节点所有权/泄漏）
- 触发条件：函数类型解析失败，如 `fn(int,`、缺少 `->`、参数类型或返回类型写错。
- 后果：`signature` 节点被丢弃，其 `parameter_type_starts/lengths/refs`（各 `new int64[128]`/`new bool[128]`）无人释放；`CompilerPipeline.krt:308-312` 只遍历从根可达的节点，被丢弃节点上的数组会一直占用。
- 最小修复：错误返回前 `delete` 三个数组并清 `owns_parameter_*`，或把 `signature` 挂到 `Invalid` 节点上保证可达。
- 备注：对比 `KrtParserParseLambda`（1095-1098 分配，出错时返回 lambda 节点，仍可达），此处返回 null 是确定性的泄漏点。

##### [中] 78-5｜函数类型参数数组固定 128，溢出后继续解析并丢弃多余参数
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:914-926`
- 类别：语义分叉 / 配置漂移
- 触发条件：函数类型参数个数超过 128。
- 后果：第 129 个起参数仍被解析（消耗 token）但不进入 `syntax_parameter_types` 与索引数组，`parameter_count` 停在 128；只报告一次错误，调用方可能基于被截断的签名继续语义分析。
- 最小修复：改为动态扩容（与 C 版 `ParserStatement.c` 的 `param_capacity` 一致），或溢出时直接失败。
- 备注：与 78-6 是两处独立实现，故分列。

##### [中] 78-6｜lambda 参数数组固定 128，溢出后继续解析并丢弃多余参数
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:1101-1142`
- 类别：语义分叉 / 配置漂移
- 触发条件：lambda 参数个数超过 128。
- 后果：第 129 个起参数被解析但不进入 `syntax_parameter_types` 与 `parameter_*` 索引数组，`parameter_count` 停在 128；只报告一次错误。
- 最小修复：动态扩容，或溢出时直接失败并停止解析。
- 备注：与 78-5 同理，独立实现。

##### [低] 78-7｜`KrtParserFork` 不复制 `type_depth`、`KrtParserCommit` 不回写 `last_type`
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:200-212`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：外层类型深度已接近 64 时做前瞻（`KrtParserLocalTypeAhead`、`KrtParserTypeDeclarationAhead`、`KrtParserCastTypeAheadDepth` 等）。
- 后果：probe 的 `type_depth` 从 0 重新计数，64 层嵌套上限被绕过（最坏约 64+64 层）；commit 后 `parser.last_type` 仍是旧值，若后续代码依赖它会取到陈旧类型。
- 最小修复：fork 时复制 `type_depth`（probe 从该值继续），commit 时回写 `last_type`。
- 备注：1-1213 内暂无调用方在 commit 后读取 `last_type`，故当前影响有限；注释（199）只承诺 probe 拥有 lexer/token，未说明状态继承范围。

##### [低] 78-8｜`KrtParserParseTypeArguments` 的跨度未剔除尾部空白
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:241`
- 类别：契约破坏 / 阶段不对齐
- 触发条件：`>` 之后、下一个 token 之前有空白或注释。
- 后果：`arguments.length/type_length` 用 `parser.token.start - start` 计算，包含尾部空白；而 `KrtParserParseType`（300）用 `KrtParserTypeSpanLength`（1666-1670 会剔除尾部空格/制表/换行）。两处跨度口径不一致，依赖 `type_length` 的诊断定位可能偏移。
- 最小修复：改用 `KrtParserTypeSpanLength(parser, start, parser.token.start - start)`。
- 备注：同类写法还见于 `:485`、`:493` 的 `parser.token.start - start`。

##### [低] 78-9｜`KrtAstIsLeaf` 全项目无调用（死代码）
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:193-196`
- 类别：死代码
- 触发条件：任何解析路径都不会调用它。
- 后果：无用函数留存，增加维护面；全项目 grep `KrtAstIsLeaf` 仅此定义一处。
- 最小修复：删除该函数，或接入实际使用点。
- 备注：无行为影响。

##### [低] 78-10｜文件作用域命名空间边界 `boundary.value=1` 写而不读（死状态）
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:485`
- 类别：死代码 / 注释与实现脱节
- 触发条件：解析 `namespace;` 时总是执行。
- 后果：`boundary.value=1` 未被任何代码读取（全项目 `NamespaceDeclaration` 仅在 Parser.krt 出现，`.value == 1` 的读取点都是 `native_type`/`type`/`expression`）；同时 `KrtParserAppendSyntax`（663-671）只复制 kind/start/length，引用节点上也带不上该标记。
- 最小修复：若需要标记文件作用域命名空间，应在引用节点上复制 `value` 并提供读取方；否则删除该赋值。
- 备注：无行为影响，属实现残留。

##### [中] 78-11｜文件作用域命名空间的 using 作用域父链为 null，丢弃此前 using
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:484`（对比块命名空间 `:496`）
- 类别：语义分叉 / 契约破坏（疑似）
- 触发条件：`using A;` 出现在 `namespace;` 之前（同一文件）。
- 后果：`namespace;` 分支用 `KrtParserUsingScope(null, parser.native_scope, ...)`，父链为 null；而块命名空间用 `KrtParserUsingScope(previous_using, path, ...)` 继承父作用域。于是文件作用域命名空间内的代码看不到此前文件级 using 导入，与块命名空间行为不一致。
- 最小修复：file-scoped 分支改用 `KrtParserUsingScope(parser.current_using_scope, parser.native_scope, ...)`。
- 备注：疑似。若语言规则要求 `namespace;` 之前不得有 using，则不成立；需与语义/语言规范确认。

##### [低] 78-12｜全局 using 节点未标记 `native_using_scope`
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:520-525`（`continue` 跳过 539-540；对比局部 using `:717`）
- 类别：错误处理不一致 / 契约破坏（疑似）
- 触发条件：文件级 `using A;` / `using A = B;`。
- 后果：`using_node` 及其 `left`（imported）没有 `native_using_scope`；局部 using（713-717）会设置 `directive.native_using_scope`。若后续阶段用该字段区分导入来源，全局与局部导入表现会不同。
- 最小修复：在 520 前对 `using_node` 调用 `KrtParserMarkUsingScope`，或统一由 `KrtParserAppendSyntax` 的引用承载并移除 717 的重复设置。
- 备注：疑似。`KrtNativeUsingMatches`（Semantic.krt:1575-1578）主要用 `type_length/name_start`，未直接依赖该字段。

##### [低] 78-13｜`KrtParserSynchronize` 的括号计数在异常嵌套下变负
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:878-882`
- 类别：错误处理不一致 / 未检查前置条件
- 触发条件：错误恢复时，`}` 出现在尚未闭合的 `(` 或 `[` 内（如 `foo(}`）。
- 后果：顶层判断（872）要求 `braces==0 && parens==0 && brackets==0` 才拦截 `}`，因此 `}` 会走 878 分支把 `braces` 减成 -1；之后 `braces == 0` 的恢复条件永不成立，恢复过程可能一路吃到文件尾，延后后续诊断。
- 最小修复：在 878 分支加 `if (braces > 0)` 保护，或让 `}` 在 `parens>0 || brackets>0` 时只计数不参与 braces 归零判定。
- 备注：仅影响出错后的恢复质量，不影响正确输入的解析。

### 第七十九批：SelfHost/Compiler/Frontend/Parser/Parser.krt（1214-2425）

#### 覆盖范围
- 只读 `SelfHost/Compiler/Frontend/Parser/Parser.krt` 第 1214-2424 行（文件实际共 2424 行，任务给到 2425，末行无内容），分段读取。
- 符号核对仅用 grep 定点查签名/调用点，未整文件通读其他源文件；`Lexer.krt`、`KrtAstMake`、`KrtParserFork/Commit` 等内部实现未展开，相关结论标「疑似」。
- 与 `Re.KrtC/src/Compiler/Frontend/Parser/*.c` 做过定点对照：C 版无同名 `FinishGuard/NullConditional/native_size += 8/16384` 等实现，结构差异大，无法逐条比对，故凡依赖下游约定或词法器行为的均标「疑似」。
- 本段主要内容：ParsePrimary 尾部（new/数组/括号/字面量）、FinishGuard、ParsePostfix、Unary/Binary/Expression、Block、Function/FunctionTail、TypeDeclaration、GlobalConstant、CompilationUnit、Statement、InitializeConstructors、Body/CaseBody。


##### [高] 79-1｜字符字面量未知转义被静默接受
- 位置：`Parser.krt:1305-1323`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：字符字面量使用未识别转义，如 `'\q'`、`'\xZ'`、`'\x1Z'`、`'\nX'`。
- 后果：`value` 直接取 `source[token.start+2]`（`'q'`→113），或十六进制循环遇非十六进制字符 `break` 后保留部分值（`'\xZ'`→0、`'\x1Z'`→1），全程无诊断，生成错误常量并继续编译。
- 最小修复：未知转义分支与十六进制 `lexer_hex_digit < 0`/零位数分支均调用 `KrtParserRecordError(parser)`。
- 备注：多字符字面量 `'ab'` 也只会取首字符；`'\x'` 合法路径本身同样无诊断。

##### [中] 79-2｜字符字面量解析未校验 token.length，可能越界读
- 位置：`Parser.krt:1306-1308`（另见 1346 浮点后缀同类写法）
- 类别：8 未检查前置条件
- 触发条件：Character token 长度小于 2（如未闭合引号被词法切成只剩引号的 token），或引号位于源缓冲末尾。
- 后果：`source[token.start+1]`、`source[token.start+2]` 越界读，读到缓冲外或错误值。
- 最小修复：解析前校验 `token.length >= 2`（转义分支 `>= 3`），否则 `RecordError` 并返回 Invalid。
- 备注：疑似，取决于词法器对未闭合字符字面量的切分；未读 `Lexer.krt`。

##### [高] 79-3｜构造器初始化器与字段共享同一 AST 节点
- 位置：`Parser.krt:2382-2389`（`KrtParserInitializeConstructors`）
- 类别：7 共享可变状态 / 10 资源与生命周期 / 3 语义分叉
- 触发条件：类型含非 static 字段且字段带初始化器（`field.left != null`），且存在构造器。
- 后果：`assignment.right = field.left` 直接复用字段初始化器节点，同一节点同时挂在字段与合成构造器体两处；后续遍历/改写可能重复初始化、双重释放或漏改一处。
- 最小修复：为构造器体克隆初始化器子树，或把 `field.left` 移入构造器并清空字段引用，明确唯一所有权。
- 备注：疑似（取决于下游是否复制/是否释放共享节点）；本段未确认 `KrtAstMake` 分配器语义。

##### [中] 79-4｜try/catch 错误路径直接返回 try 体，丢弃 Try 语义
- 位置：`Parser.krt:2085、2088、2096`
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：catch 子句格式错误（缺标识符、`ParseType` 返回 null、缺右括号）。
- 后果：`return body` 把 Try 节点及已解析的 catch/finally 全部丢弃；错误虽记录，恢复后的 AST 变成「无保护地执行 try 体」。
- 最小修复：返回带错误标记的 Try 节点（保留 body/catches/finally），或统一返回 Invalid。
- 备注：2088 的 `caught_type == null` 分支未额外 `RecordError`，若 `ParseType` 不报错则也为静默。

##### [中] 79-5｜Try 节点 child_count 在 catches==null 时多算
- 位置：`Parser.krt:2109`
- 类别：6 契约破坏
- 触发条件：`try { } finally { }`（catches==null 且 finally!=null），或 `try { }`（两者皆 null）。
- 后果：child_count 分别为 3/2，但非空子节点只有 2/1（`right==null`），下游按 child_count 遍历会越界或解引用 null。
- 最小修复：`result.child_count = 1 + (catches != null ? 1 : 0) + (finally_body != null ? 1 : 0);`
- 备注：与 2101 的 `variable == null ? 1 : 2` 写法对比，可见此处遗漏了 catches 判空。

##### [中] 79-6｜Throw 无操作数时 child_count 与 Return/Yield 不一致
- 位置：`Parser.krt:2043-2044`
- 类别：6 契约破坏 / 9 错误处理不一致
- 触发条件：`throw;`（无操作数）。
- 后果：`left==null` 但 `child_count=1`；Return（2294）与 Yield（2061）在空操作数时均用 0，Throw 不一致，下游可能解引用 null。
- 最小修复：`result.child_count = value == null ? 0 : 1;`
- 备注：无。

##### [中] 79-7｜后缀表达式错误返回 node，丢失 `?.` 守卫链
- 位置：`Parser.krt:1383、1394、1396、1401、1441`（对照正常出口 1459）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：表达式带 `?.` 守卫链（`guarded != null`）后，调用缺 `)`、`::` 后非标识符、`.` 后非标识符、下标缺 `]`。
- 后果：这些分支 `return node`，而 1459 用 `KrtParserFinishGuard(guarded, node)`；返回的是丢失守卫链的底层节点，`a?.b(` 被恢复成 `b(` 语义。
- 最小修复：错误分支统一 `return KrtParserFinishGuard(guarded, node);`。
- 备注：`?.` 分支 1425/1431 返回 guarded 但仍有长度问题，见 79-8。

##### [中] 79-8｜`?.` 错误路径未调用 FinishGuard，guard.length 保持 0
- 位置：`Parser.krt:1425、1431`（对照 1459）
- 类别：9 错误处理不一致 / 13 注释与实现脱节
- 触发条件：`?.[` 缺 `]`，或 `?.` 后跟非标识符。
- 后果：直接 `return guarded`，未调用 `KrtParserFinishGuard`，所有 NullConditional 节点 length 仍是构造时的 0；下游按 span 处理会得到零长度守卫。
- 最小修复：改为 `return KrtParserFinishGuard(guarded, node);`，或在返回前补齐 length。
- 备注：正常路径 1459 会修复 length，仅错误路径遗漏。

##### [中] 79-9｜CompilationUnit 尾部把两种错误合并，且 16384 为魔数
- 位置：`Parser.krt:1978`
- 类别：2 诊断缺失 / 11 配置漂移 / 1 静默失败
- 触发条件：解析完声明后 `token.kind != Eof`（尾部多余 token），或 `unit.child_count > 16384`。
- 后果：两种不同情况共用一句无消息 `RecordError`；尾部多余 token 不同步、被静默丢弃；16384 为魔数，且 child_count 含展平的方法数，限值口径不清。
- 最小修复：拆分两个判断并给出具体消息；`token != Eof` 时同步/跳过尾部；把 16384 提为命名常量并注明计数口径。
- 备注：无。

##### [中] 79-10｜字段布局固定 +8，忽略类型宽度与数组长度
- 位置：`Parser.krt:1910-1911`
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：结构体/类含多个不同类型或数组成员。
- 后果：每个非 static 字段 `native_offset` 按固定 +8 递增，忽略类型大小、数组 rank/size 与对齐，布局与实际类型不符。
- 最小修复：按类型大小/对齐计算偏移（含数组），或在语言层面明确所有字段为 8 字节槽并写入注释/契约。
- 备注：疑似（可能语言规定统一 8 字节槽位）；C 版未找到同名逻辑可对照。

##### [中] 79-11｜Block 仅在「已出错」后对函数声明提前收尾，恢复行为不一致
- 位置：`Parser.krt:1644`（后续 1659 再记一次错误）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：块内已产生错误（`parser.errors > block_initial_errors`）后遇到函数声明。
- 后果：记录「expected closing block brace before declaration」并 break，随后 1659 再记一次错误；块被提前终止，后续语句改归外层作用域。类型声明无此分支，恢复行为不一致。
- 最小修复：统一恢复策略；若嵌套函数/类型非法，应在无错误时也报同一错误，避免双重诊断与作用域漂移。
- 备注：疑似（若嵌套函数非法，也应无条件报错而非仅由错误状态触发）。

##### [低] 79-12｜KrtParserTokenText(42) 在谓词中改写词法器状态
- 位置：`Parser.krt:1595`（`KrtParserSplitStars` 1587-1592）
- 类别：7 共享可变状态 / 13 注释与实现脱节
- 触发条件：任何以 `expected == 42` 调用 `KrtParserTokenText` 且当前 token 为 Power。
- 后果：谓词内部调用 `KrtParserSplitStars`，改写 `token.kind/length` 并回退 `lexer.position/line/column`；查询带副作用，隐含「调用方必须消费 `*`」的契约，与 1585-1586 注释「stars 另行消费」相悖。
- 最小修复：把拆分移到显式消费点（ParseUnary/ParsePrimary），`KrtParserTokenText` 只做只读判断。
- 备注：本段内 2299 调用后必解析表达式，暂未发现可观察错误，属脆弱设计。

##### [低] 79-13｜`global::name` 复用 Identifier 节点，name 字段未同步
- 位置：`Parser.krt:1391-1397`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：使用后缀 `global::name` 路径。
- 后果：复用 Identifier 节点且只改 `length` 覆盖 `global::name`，未更新/新建 name 信息；若下游按 `name_start/name_length` 取标识符（Member 节点即如此），只能得到 `global` 或默认值。
- 最小修复：生成限定标识符节点（更新 name_start/name_length），或明确约定 Identifier 只用 start/length 并统一所有节点。
- 备注：疑似；普通 Identifier 在 1299 也未设 name_start/name_length，需下游约定一致。

##### [低] 79-14｜枚举显式负值取负可能溢出
- 位置：`Parser.krt:1828`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：枚举显式赋值为 `-9223372036854775808`（`constant.left.value == INT64_MIN`）。
- 后果：`-constant.left.value` 有符号溢出（UB/回绕），枚举值错误。
- 最小修复：用无符号取负，或显式检查 INT64_MIN 并报错。
- 备注：无。

##### [低] 79-15｜1868 的 `method.kind != Function` 为死代码
- 位置：`Parser.krt:1868`
- 类别：5 死代码
- 触发条件：任何类型成员方法解析。
- 后果：`KrtParserParseFunctionTail` 所有路径均返回 Function 节点 `unit`，该条件恒为假，分支不可达。
- 最小修复：删除该检查；若确需保留，应让 `ParseFunctionTail` 在错误时返回 Invalid。
- 备注：无。

##### [低] 79-16｜函数关键字声明时 return_type 为 null 仍赋给 syntax_type
- 位置：`Parser.krt:1706`
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：使用函数关键字（`keyword_function == true`，如 `function foo()`）声明。
- 后果：`return_type` 保持 null 并赋给 `unit.syntax_type`；下游若假设返回类型非空会解引用 null。
- 最小修复：关键字声明时补默认返回类型（void/inferred），或在使用处判空。
- 备注：疑似；未确认 `KrtParserFunctionKeyword` 匹配集合与下游约定。

##### [低] 79-17｜ParseStatement 兜底在 Eof 上仍调用 KrtLexerNext
- 位置：`Parser.krt:2329-2331`
- 类别：8 未检查前置条件
- 触发条件：`ParseBody` 在 Eof 上调用 `ParseStatement`（如 `if (x)` 后文件结束）。
- 后果：兜底分支仍 `KrtLexerNext` 消费 Eof；若词法器不兜底会越界或状态异常。
- 最小修复：兜底中先判 Eof，直接返回 Invalid 且不 Next。
- 备注：疑似；未读 `Lexer.krt` 的 Eof 行为。

##### [低] 79-18｜非 extern/非 abstract 函数以 `;` 结束被静默当作声明
- 位置：`Parser.krt:1755-1759`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：非 extern、非 abstract 函数以 `;` 结束（无函数体）。
- 后果：被当作 `op=223` 声明静默接受，无「缺少函数体」诊断；与 abstract 必须显式处理不同。
- 最小修复：仅允许 extern/abstract/接口成员省略体，否则 `RecordError`。
- 备注：疑似（语言可能允许原型声明）。

##### [低] 79-19｜字段数组未递增 syntax_array_rank，与局部变量不一致
- 位置：`Parser.krt:1915-1923`（对照 2260）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：类型字段声明数组（如 `int a[4];`）。
- 后果：只设置 `field.syntax_array_size`，未递增 `syntax_array_rank`；局部变量在 2260 递增 rank，字段与局部数组元数据不一致。
- 最小修复：字段数组同样递增 `field.syntax_array_rank`，或统一由 size 推导 rank。
- 备注：疑似；需确认下游是否依赖 rank。

##### [低] 79-20｜整数字面量未检查 lexer_hex_digit 负值
- 位置：`Parser.krt:1329`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：Integer token 中出现 `lexer_hex_digit` 返回负值的字符（如允许的数字分隔符/后缀被并入 token）。
- 后果：`digit_value` 为负被当作 uint64 加入 `value`，整数字面量静默变成巨大错误值，无诊断。
- 最小修复：`if (digit_value < 0) { KrtParserRecordError(parser); break; }`。
- 备注：疑似；`Project.krt:749、753` 对同一 `lexer_hex_digit` 判了 `< 0`，提示该函数确实可能返回负值。

### 第八十批：SelfHost/Compiler/Frontend/Semantic/Types.krt（1-1113）

#### 覆盖范围

- 仅审计 `SelfHost/Compiler/Frontend/Semantic/Types.krt` 第 1-1113 行（分四段读取：1-300、301-600、601-900、901-1113）。
- 覆盖函数：泛型约束（`KrtNativeConstraintValueType`/`DefaultConstructor`/`Unmanaged`/`CheckConstraints`/`EvaluateConstraints`）、虚分派（`MethodSignatureEqual`/`FindVirtualImplementation`/`PrepareDispatch`/`BindDispatchCall`）、函数值与间接调用（`AnnotateFunctionType`/`PrepareFunctionShape`/`BindFunctionAddress`/`BindIndirectCall`/`NormalizeQualifiedSyscall`）、泛型实例化与推断（`TypeBaseWidth`/`TypeEqualDepth`/`ResolveNamedType`/`CloneAst`/`InstantiateClass`/`InstantiateFunction`/`InferenceFormal`/`InferenceContains`/`InferenceProjection`/`StripSourceTypeSuffix`/`InferPattern`/`InferTypeArguments`/`SpecializeCall`）、模块准备（`PrepareModule`）、闭包探针（`ClosureProbeCopyNode`/`ClosureProbeJournal`/`ClosureProbeSyntax`）。
- 对照：`Re.KrtC/src/Compiler/Middle/Ir/IrType.c` 的 `KrtIrTypeEqual`（293-312）等；C 版与本版在「双 NULL 相等」上一致，其余为不同层（IR vs 前端 AST），无法逐条对齐处标「疑似」。
- 未整文件通读其他源文件；仅按需 grep 特定符号（`native_dispatch_slot`、`parameter_count`、`native_layout_state`、`native_globals`、`syntax_suffixes`、`KrtParserAppendDeclaration` 等）核对契约。


##### [高] 80-1｜泛型类实例化只把第一个方法挂进实例，`method_tail` 为死变量
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:695-702`
- 类别：5 死代码 / 6 契约破坏 / 3 语义分叉
- 触发条件：实例化含 ≥2 个方法的泛型类（`KrtNativeInstantiateClass`），例如 `class Box<T> { void A(){} void B(){} }` 被 `Box<int>` 引用。
- 后果：`instance.declarations` 只含第一个生成的方法，`method_tail = generated;` 赋值后从未使用，后续方法仅进 `module.left`。在 `KrtNativeSnapshotTypeMembers`（940 行）之前，`KrtNativeTypeMemberFirst` 回退到 `owner.declarations`（SemanticRuntime.krt:423），因此 `KrtNativePrepareDispatch`（149-205 行）对实例类型做重复虚方法签名检查、override 匹配、抽象方法实现检查时只能看到第一个方法，可能漏判重复虚槽/错误 override；`KrtNativeNormalizeQualifiedSyscall`（395 行）等直接读 `.declarations` 的路径同样只看到一个成员。
- 最小修复：把 699-700 行改为 `if (instance.declarations == null) { instance.declarations = generated; } else { method_tail.next = generated; }`，并保留 `method_tail = generated;`。
- 备注：`KrtParserAppendDeclaration` 对 Function 追加到 `module.left`，故快照后成员链可重建，影响面限于快照前窗口与直接读 `.declarations` 的调用点；但缺失的链表链接本身是确定性缺陷。

##### [高] 80-2｜抽象方法实现检查用 `native_dispatch_slot == null` 误匹配非虚成员
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:199-200`（配合 `KrtNativeFindVirtualImplementation` 130-140）
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：具体类型继承含抽象方法的基类，且该抽象方法 `method.native_dispatch_slot == null`（例如基类在 145 行因 `native_generic_parameters != null` 或 `value == 4` 提前返回、未完成分派准备，或基类来自未完成 prepare 的路径）。
- 后果：`KrtNativeFindVirtualImplementation(type, null)` 会返回 `type` 中第一个 `native_dispatch_slot == null` 的成员（非虚方法默认即为 null），该成员 `native_abstract == false`，于是 200 行判定「抽象方法已实现」，具体类型缺少实现也能通过分派校验，后续可能生成调用不存在槽位的代码。
- 最小修复：进入 199 行前显式要求 `method.native_dispatch_slot != null`，否则视为未实现并置 `native_dispatch_state = 3`；或在 `KrtNativeFindVirtualImplementation` 中拒绝 `slot == null`。
- 备注：疑似。Parser.krt:1710/1873 令抽象方法 `native_virtual = true`，正常路径会在 160 行赋槽；需构造基类提前返回分派准备的场景才能触发，未能在本切片内完全确认可达性。

##### [高] 80-3｜数组类型层丢弃可空性，`?` 语义随书写顺序改变
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:532-533`（另见 546-554、508-517）
- 类别：6 契约破坏（可空性约定）/ 3 语义分叉
- 触发条件：类型后缀顺序为 `?` 在 `[]` 之前（`T?[]`）。`KrtNativeApplySourceTypeSuffix` 先执行 63 号后缀置 `native_nullable = true`，随后 91 号数组层在 532 行无条件 `node.native_nullable = false`、533 行 `pointee_nullable = 0`，可空性被清空。
- 后果：`T?[]` 与 `T[]` 的类型表示完全相同，可空性信息静默丢失；而 `T[]?` 因 `?` 在最后可保留，导致仅书写顺序不同即类型不同/相同，破坏可空性契约且无诊断。
- 最小修复：数组层若要表达「元素可空」，应把 `outer` 移入元素描述（`element.native_nullable`）或移入 `pointee_nullable`；若确为语言规则，应在文档/诊断中明确并在类型相等中保持一致。
- 备注：Parser.krt:296 在语法层同样 `type.syntax_nullable = false`，两层一致，可能是有意规则；但语义层无任何注释或诊断说明，标疑似。

##### [中] 80-4｜结构体布局未完成时宽度静默回退为 8
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:405-407`
- 类别：1 静默失败（默认值）/ 6 契约破坏（宽度）
- 触发条件：`KrtNativeTypeBaseWidth` 收到 `native_type.value == 2`（struct）但 `native_layout_state != 2`（0 未开始 / 1 进行中 / 3 错误，见 SemanticRuntime.krt:59、Types.krt:1962/2012）。
- 后果：405 行条件不成立，落到 407 行 `native_type != null → 8`，把未完成/布局失败的结构体当作 8 字节标量宽度参与类型相等（423 行）、推断与 stride 计算，可能产生错误匹配或错误布局。
- 最小修复：对 `value == 2 && native_layout_state != 2` 显式返回错误/触发布局，而不是回退 8；至少在非 `KrtNativeLayoutProbe` 阶段不应静默给值。
- 备注：疑似。布局通常先于绑定完成，但 1962 行会把 `native_layout_state` 置 3 并设 `module.native_error = 12`，此后仍可能继续查询宽度。

##### [中] 80-5｜约束求值深度超限静默失败且使用模块级共享计数器
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:68-71`
- 类别：7 共享可变状态 / 2 诊断缺失 / 1 静默失败
- 触发条件：泛型约束嵌套深度 ≥64（`module.native_constraint_depth >= 64`），或约束类型标注内部再次触发 `KrtNativeCheckConstraints`。
- 后果：68 行直接 `return false`，不设置任何错误码或诊断（此函数内），仅由调用方 572/893 行补 `native_error = 6`；深度限制这一具体原因被掩盖。`native_constraint_depth` 挂在 module 上，属于跨调用共享可变状态，若求值路径异常中断（如后续引入提前 return/异常）会残留计数。
- 最小修复：超限时设置专用错误码并调用 `KrtDiagnostic`；深度计数改用局部递归参数传递，或在函数入口/出口成对维护并断言归零。
- 备注：正常路径 69-71 行有配对的 +/-，但 68 行的失败路径不递增也不会残留；主要问题是无诊断与共享状态。

##### [中] 80-6｜约束 `op` 未识别时默认按 derives-from 处理
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:90-98`
- 类别：3 语义分叉 / 1 静默失败
- 触发条件：`constraint.op` 不是 1/2/3/4（新增约束种类或损坏的 op）。
- 后果：else 分支把任何未知 op 当作「类型派生」约束：91-97 行构造 `expected` 并做 `DerivesFrom`/`TypeEqual`，未知约束被静默解释为另一种语义，可能错误通过或错误拒绝，且无诊断。
- 最小修复：对未知 op 显式返回 false 并报告「未知约束」诊断；新增 op 时同步维护分派。
- 备注：当前 Parser 生成的 op 集合未在本切片内完整核对，标疑似；但缺省分支无防御是确定的。

##### [中] 80-7｜`KrtNativePrepareDispatch` 多处失败路径无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:153`、`157`、`179`、`187`、`200`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：153 行（虚方法带 static/构造/泛型等非法组合）、157 行（重复虚签名）、179 行（override 无匹配且非接口实现）、187 行（覆写签名不一致）、200 行（抽象方法未实现）。
- 后果：这些路径只置 `type.native_dispatch_state = 3` 并 `return false`，仅 180/185 行在部分场景发 `E_ACCESS`/`E_MODIFIER`；其余场景（如 override 拼错名、签名不匹配）没有具体诊断，调用方只拿到 `output.error_start`，用户无法定位。
- 最小修复：为每条失败路径补充错误码与 `KrtDiagnostic`（如 E_OVERRIDE/E_ABSTRACT/E_DISPATCH），保持与 180/185 一致。
- 备注：157 行还会因 `KrtNativeAnnotateType` 失败而静默走同一分支，进一步掩盖真实原因。

##### [中] 80-8｜`KrtNativePrepareModule` 重复调用 `KrtNativePrepareExternals`
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:903` 与 `939`
- 类别：9 错误处理不一致 / 11 配置漂移 / 10 资源与生命周期
- 触发条件：每次模块准备都会执行两次外部声明收集（903 在函数绑定前，939 在全局收尾后）。
- 后果：`KrtNativePrepareExternals` 会写 `candidate.value = -count-1`、`native_external_next`、`native_bound` 并追加到 `output.external_head`（SemanticRuntime.krt:392-395）。虽然第二次对已 `native_bound` 的声明会跳过，但两次调用之间存在状态依赖：若第一次因类型未布局而失败（389-390 置 `native_error = 13`），流程已 return；第二次属于冗余遍历，且 `output.external_count`/`external_head` 的构造分散在两处，配置来源不一致。
- 最小修复：只保留一次调用，或明确注释第二次为「布局完成后重试」并让其幂等（不重复计数、不改 `value`）。
- 备注：疑似。若第一次全部成功则第二次为空操作，属于冗余而非错误；但错误路径与计数状态耦合，建议收敛。

##### [中] 80-9｜模块准备对 16384 上限与振荡静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:906`、`909`、`921`、`938`
- 类别：2 诊断缺失 / 8 未检查前置条件
- 触发条件：模块函数数/声明数超过 16384（909 行 `count < 16384` 截断，921 行 `function_node != null`），或 `while (changed)` 连续 16384 轮仍 `changed`（906 行）。
- 后果：921 行与 938 行直接 `return false`，没有错误码、没有 `KrtDiagnostic`、没有指出是上限/振荡；938 行的 `changed` 为真时同样静默失败，调用方仅得 `output.error_start`。
- 最小修复：对上限与轮次耗尽分别设置错误码并诊断（如 E_LIMIT），并报告实际数量/轮次。
- 备注：909 行截断后 921 行才报错，期间已对部分函数做了绑定，失败状态不一致。

##### [中] 80-10｜`KrtNativeClosureProbeSyntax` 无深度/环保护
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1108-1113`
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：对深度较大或存在共享子树/回边的 AST 调用（递归遍历 `left/right/alternate/body/next`）。
- 后果：无递归深度上限、无访问标记，深 AST 直接栈溢出；若 AST 存在环（克隆/生成过程中可能产生）则无限递归。
- 最小修复：加入深度上限或显式栈/访问集合；超限返回并由调用方报告。
- 备注：同类函数 `KrtNativeTypeEqualDepth`（418）与 `KrtNativeInferenceContains`（741）都有 64 层守卫，此处缺失不一致。

##### [中] 80-11｜闭包探针日志线性去重且无界增长
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1097-1106`
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：同一 module 反复 probe 不同声明（`KrtNativePrepareDispatch`/`PrepareFunctionShape`/`InstantiateClass` 等均调用）。
- 后果：1100-1101 行对 `state.mutations` 链表做 O(n) 线性扫描去重，整体 O(n²)；每条 mutation 持有一个 `new KrtAstNode()` 全字段快照（956-1095 行），无清理/上限，长时间编译内存持续增长。
- 最小修复：用哈希/集合判重；为快照数量设上限或按 module 生命周期统一释放。
- 备注：`module.native_closure_probe_state` 为 module 级共享可变状态，多处并发/重入读取需确认单线程约束。

##### [中] 80-12｜给非泛型类型传类型实参时静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:569-571`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`Foo<int>` 中 `Foo` 解析为非泛型类型（`type.native_generic_parameters == null`），或实参绑定失败。
- 后果：571 行直接 `return false`，无错误码/诊断；调用方无法区分「类型不存在」「非泛型」「实参错误」。568 行 `FindUsingType` 失败同样静默。
- 最小修复：区分并报告 E_UNKNOWN_TYPE / E_NOT_GENERIC / E_TYPE_ARGUMENTS，至少设置 `module.native_error`。
- 备注：577 行（泛型类型在上下文外使用）也属同类静默失败。

##### [中] 80-13｜`KrtNativeBindFunctionAddress` 重置 `module.native_error = 0` 掩盖既有错误
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:310`、`334`
- 类别：9 错误处理不一致 / 7 共享可变状态
- 触发条件：函数索引失效（`native_function_index == null || !ok`）时。
- 后果：先 `binding.module.native_error = 0` 再 `KrtNativeBindingError`，会清除此前已记录的错误码（如 301 行依赖的 `native_error == 10`），导致错误归因错误或丢失；两处重复同一模式。
- 最小修复：不要清零既有错误码；若确需重置，应在 `KrtNativeBindingError` 内统一处理并保留原值。
- 备注：疑似（取决于 `KrtNativeBindingError` 是否会覆盖写入）。

##### [中] 80-14｜函数形状比较：闭包分支只比参数数量
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:247-252`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：`signature.native_closure_signature`/`native_closure_shape` 命中且仅参数数量相等。
- 后果：247 行只校验 `closure.native_type != null` 与 `parameter_count` 相等，随后直接复用 `closure.native_type/declarations/readonly`，不比较参数类型、`native_ref`、返回类型（返回类型仅判非空）。形状不同的闭包签名可能被判定相等，造成函数值类型混淆。
- 最小修复：复用 `KrtNativeFunctionShapesEqual` 或逐项比较参数类型与 ref。
- 备注：疑似。闭包形状通常在生成阶段已校验，但此处作为契约边界缺少防御。

##### [低] 80-15｜`KrtNativeTypeEqualDepth` 对双 NULL 返回 true
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:417`
- 类别：6 契约破坏（类型表示）
- 触发条件：`KrtNativeTypeEqual(null, null)`。
- 后果：417 行 `if (left == right) return true;` 在两者同为 null 时返回 true，与「类型相等」的直觉契约相反；调用方若未先判空（如 815 行 `KrtNativeTypeEqual(state.inferred, candidate)` 理论上可能传入 null）会把「未知」当作「相等」。
- 最小修复：改为 `if (left == right) { return left != null; }`，或在文档中明确 null==null 约定。
- 备注：`Re.KrtC/.../IrType.c:294-296` 的 `KrtIrTypeEqual` 行为相同，属既有约定；因此仅低，且需与 C 版保持一致。

##### [低] 80-16｜未知类型后缀 op 被当作指针层
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:546-554`
- 类别：3 语义分叉 / 1 静默失败
- 触发条件：`syntax_suffixes` 中出现非 63（nullable）、非 91（array）的 op。
- 后果：550 行 `KrtNativeApplyTypeLayer(node, suffix.op == 91)` 对任何其他 op 都按指针层处理，未知后缀静默变成指针，类型表示错误且无诊断。
- 最小修复：显式列举 42（指针）等已知 op，未知 op 返回 false 并报告。
- 备注：Parser 现仅生成 42/63/91（Parser.krt:283/287/297），但语义层无白名单。

##### [低] 80-17｜位宽舍入循环存在整数溢出风险
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:409-411`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：`type.native_bits` 极大（如 > 2^28）。
- 后果：`width *= 2` 与 `width * 8` 可能溢出为负，`while (width * 8 < type.native_bits)` 条件恒真，循环无法终止（或得到错误宽度）。
- 最小修复：先做上限校验（如 `native_bits` 必须为 8 的倍数且 ≤ 1024），或用除法/移位并检测溢出。
- 备注：疑似。正常内建位宽不会触发，但自定义 `native_bits` 来源未在本切片内确认。

##### [低] 80-18｜方法签名比较未校验参数数组长度
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:112-117`
- 类别：8 未检查前置条件（越界）
- 触发条件：`first.parameter_count` 与 `first.parameter_refs/parameter_type_starts` 实际长度不一致（损坏节点或构造期未分配）。
- 后果：113 行 `first.parameter_refs[index]`、116 行 `parameter_type_starts[index]` 直接索引，可能越界读取，导致错误比较或崩溃。
- 最小修复：比较前校验 `parameter_count` 不超过各数组长度（或确保不变量并断言）。
- 备注：正常路径数组按 128 分配（Parser.krt:1884-1887），但函数实例化会重建数组（339-347），需确认一致。

##### [低] 80-19｜约束函数对 `native_type == null` 默认判为值类型/非托管
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:5`、`29`
- 类别：1 静默失败（默认值）/ 6 契约破坏
- 触发条件：未解析/未知类型节点（`native_type == null`）进入 `KrtNativeConstraintValueType` 或 `KrtNativeConstraintUnmanaged`。
- 后果：5 行 `type.native_type == null || ...` 返回 true，29 行经 `KrtNativeConstraintValueType` 也返回 true，即未知类型被默认当作值类型且 unmanaged，约束可能错误通过。
- 最小修复：区分「已解析为无 native_type 的合法标量」与「未解析」，后者应返回 false 并报告。
- 备注：疑似。调用方（86/89 行）通常在解析后调用，但 `EvaluateConstraints` 82 行已对 null 参数提前返回。

##### [低] 80-20｜类型推断中闭包形状失败被静默跳过
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:849-852`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：实参需要闭包上下文，`KrtNativeClosureInferShape` 返回 null。
- 后果：851-852 行 `index = index + 1; continue;` 直接跳过该实参的推断，不记录原因；随后若 `state.inferred == null` 857 行返回 null，错误信息只表现为「推断失败」，无法定位是闭包形状问题。
- 最小修复：记录候选错误（如 `binding.candidate_error`）或立即返回 null 并诊断。
- 备注：跳过而非失败可能是有意（该参数不参与约束），但无任何注释。

##### [低] 80-21｜`KrtNativeInstantiateClass` 克隆基类节点时连同 `next` 兄弟
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:690`
- 类别：6 契约破坏 / 10 资源与生命周期
- 触发条件：`declaration.left`（基类类型节点）的 `next` 链非空。
- 后果：`KrtNativeCloneAst(..., siblings=true)` 会递归克隆 `next` 链，可能把无关兄弟节点复制进实例，产生悬挂引用或额外节点；对比 692 行 body 也需要 siblings，但 base 的 `next` 语义不同。
- 最小修复：确认基类节点是否需要 siblings；若不需要，改为 `false`。
- 备注：疑似。需确认 `left` 上 `next` 的实际用途（类型参数/基类列表可能在 `left` 链上）。

##### [低] 80-22｜探针快照共享 module/类型等可变指针
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1040`（另见 1001、1018、1051-1061）
- 类别：7 共享可变状态
- 触发条件：快照被用于后续 diff/回滚，而原节点字段被继续修改。
- 后果：`native_globals`、`native_type`、`native_generic_registry`、`native_instances`、`native_context` 等指针被快照与原节点共享；若快照用于比较，后续修改会使快照「跟着变」，失去快照意义。
- 最小修复：明确快照语义（浅拷贝 borrow）；若需隔离，对关键字段做深拷贝或记录版本号。
- 备注：函数注释（954-955 行）已声明「borrow all child and descriptor pointers」，故可能是有意；仅提示共享可变状态风险。

##### [低] 80-23｜`KrtNativeTypeParser` 未校验 `start + length` 边界
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:472`
- 类别：8 未检查前置条件（越界/溢出）
- 触发条件：`start + length` 溢出或超过源缓冲区实际长度。
- 后果：`KrtLexerInit(lexer, (byte*)source, start + length)` 可能读到源外内存或得到错误长度，后续解析越界。
- 最小修复：调用前校验 `start >= 0 && length >= 0 && start + length >= start` 且不超源长。
- 备注：疑似。`source` 为 int64 指针，源长度信息未在此函数内可见。

##### [低] 80-24｜`KrtNativePrepareModule` 对 `module.native_globals` 未判空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:904`、`907`
- 类别：8 未检查前置条件（NULL）
- 触发条件：`module.native_globals == null`（CompilerPipeline.krt:112 会置 null，直到 SemanticFeatures.krt:1182 才创建）。
- 后果：907 行 `state.changed = false` 解引用 null，直接崩溃。
- 最小修复：入口判空并返回 false/报告，或在上游保证创建完成后才调用 `PrepareModule`。
- 备注：疑似。正常流程由 SemanticFeatures 先初始化，但本函数无防御。

##### [低] 80-25｜上限/失败路径缺少可测试的显式错误契约
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:906`、`909`、`921`、`938`
- 类别：12 测试不足
- 触发条件：需要验证 16384 上限、轮次耗尽、绑定失败时行为。
- 后果：这些路径只 `return false`，无错误码/诊断，单测很难断言具体失败原因；本切片内也没有任何断言或自测钩子。
- 最小修复：为每个失败路径定义可断言的错误码，并补充边界测试（函数数=16384、约束深度=64 等）。
- 备注：未能读取测试文件（超出允许范围），仅据本切片实现判断。

### 第八十一批：SelfHost/Compiler/Frontend/Semantic/Types.krt（1114-2226）

#### 覆盖范围
- 文件：`SelfHost/Compiler/Frontend/Semantic/Types.krt`，仅读取 1114-2226 行（文件共 2225 行）。
- 覆盖函数：KrtNativeClosureProbeType / Tail / Registry / Globals / Inheritance / Begin / End；KrtGenericTypeHash / KrtGenericHash / KrtGenericRegistrySlot / Grow / For / Find / Add；KrtNativeDerivesFrom / InheritanceDistance / FieldOwner / ResolveInheritanceInner / PrepareInheritance / BindBaseConstructor；KrtInheritanceStackFor / Push / Pop / KrtNativeResolveInheritance / CycleName / Diagnostic；KrtNativeAddInterface / ResolveTypeBases / Implements / InterfaceDistance / InterfaceMethodMatches / FindMatchingInterfaceMethod / DeclaresInterface / FindInterfaceImplementation / MethodImplementsInterface / ValidateInterfaces / CompatibleInterfaceContracts；KrtNativeReferenceValue / FunctionLabelsEqual / MergeCallbackLabels / BindNullCoalescing / BindNullConditional；KrtNativeSignatureState / SignatureTypeCore / SignatureType；KrtNativeAggregateKind / InlineValue / AlignStorage / LayoutError / ValueStorage / ValueAlignment / DescribeAggregate / ResolveLayout / ReserveFrame / BindAggregateFrame；KrtNativeSwitchError / SwitchStringByte / SwitchSameConstant / BindSwitch / SwitchStatementFlow / SwitchClauseFlow / SwitchOutcomes / SwitchAlwaysReturns。
- 对照：Re.KrtC 中未找到同名函数（其泛型注册表为 `Generics.c` 的名称+参数实现），无法逐行对照；凡不能确认处均标「疑似」。


##### [高] 81-1｜字符串 switch 常量比较把嵌入 NUL 当字符串终止符
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2116`（配合 2094-2107）
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：switch 用例标签为含嵌入 NUL 的字符串，如 `"a\0b"` 与 `"a\0c"`；或字面量含 `\0` 转义。
- 后果：`KrtNativeSwitchStringByte` 对 `\0` 返回 0，`KrtNativeSwitchSameConstant` 在 `a <= 0` 处提前判定相等，两个不同标签被误判为同一常量，合法 switch 被报「重复用例」；比较语义与运行时按长度比较字符串不一致。
- 最小修复：比较时以 `literal.start + index == literal.start + literal.length - 1`（到达收尾引号）为唯一结束条件，不要用字节值 0/-1 兼作终止符；或改用长度+逐字节比较。
- 备注：该函数同时承担转义解码，`\0` 与结束符共用 0 值是根因。

##### [高] 81-2｜布局失败路径泄漏整个工作栈（4 个数组）
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1995`（泄漏点 2009、2019、2024、2026、2048、2049、2052、2067）
- 类别：10 资源与生命周期
- 触发条件：`KrtNativeResolveLayout` 任一字段注解失败、内联结构依赖状态错误、栈扩容失败、数组长度表达式绑定/常量求值失败、偏移或大小越界，走 `return KrtNativeLayoutError(...)`。
- 后果：`types/fields/offsets/alignments` 四个数组在成功路径 2072 才统一 `delete`，所有错误返回都不释放；单次失败泄漏最多 4×16384 个元素的数组，错误恢复或批量编译时持续累积。
- 最小修复：用统一出口（如 `defer` 或函数末尾集中释放）释放四个数组；或在每个 `return KrtNativeLayoutError` 前释放。
- 备注：`KrtNativeLayoutError` 自身不释放，调用方也无释放。

##### [高] 81-3｜接口实现缺失时静默失败，无专项诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1681`（判定 1669）
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：具体类（`type.value==1/2` 且 `!type.native_abstract`）实现了某接口，但 `KrtNativeFindInterfaceImplementation` 返回 null 或返回抽象方法；且继承链上没有访问模式非 1 的同名方法。
- 后果：直接 `return false`，不调用 `KrtDiagnostic`；调用方只拿到通用的继承/布局失败，用户无法定位缺失的接口方法。1691-1693 注释明确要求「diagnose them before either overload selection or codegen」，实现未诊断。
- 最小修复：在 1681 前对 `requirement` 发出 E_INTERFACE（或等价码）诊断，包含接口名与方法名位置。
- 备注：仅当候选方法访问模式非 1 时才在 1675 报 E_MODIFIER，其余情况完全静默。

##### [中] 81-4｜基类/接口列表非法组合静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1525`、`1529`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：重复列出同一基类/接口（1525）；struct 继承 class、类列出多个 class 基类、或先列接口再列 class 基类（1529）。
- 后果：均直接 `return false`，不发任何诊断；`KrtNativeResolveInheritanceInner` 将状态置 3，最终只有上层通用错误，无法指出具体是哪一条基类声明非法。
- 最小修复：在 1525/1529 返回前发出 E_INHERITANCE/E_BASE 诊断，位置用 `reference.start`。
- 备注：1532 行父类解析失败同样无追加诊断，但父类自身可能有诊断，未列为独立条目。

##### [中] 81-5｜接口契约返回类型冲突静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1710`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：类型实现的多个接口含同名同参但返回类型不同的方法（`KrtNativeMethodSignatureEqual(...,false)` 为真、`(...,true)` 为假）。
- 后果：`KrtNativeCompatibleInterfaceContracts` 返回 false，无诊断；1691-1693 注释称此类冲突应在重载选择或代码生成前诊断。
- 最小修复：在 1710 处对冲突的 `a`/`b` 方法发出诊断，位置取两者之一。
- 备注：调用方 `KrtNativeValidateInterfaces` 仅透传 false。

##### [中] 81-6｜接口成员声明违规静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1646`、`1649-1651`、`1655`
- 类别：2 诊断缺失
- 触发条件：接口含字段（1646）；接口方法不是 op 223、是 static、非 abstract、带泛型参数、带 base_call 或带方法体（1649-1651）；接口内重复签名（1655）。
- 后果：全部直接 `return false`，无 KrtDiagnostic；用户只能看到上层通用失败。
- 最小修复：为字段、非法修饰、重复签名分别补诊断，位置用 `field.start`/`method.start`。
- 备注：1669 分支的 E_MODIFIER 只覆盖类侧访问修饰，不覆盖接口自身声明。

##### [中] 81-7｜签名缓存键不匹配时覆盖旧缓存并泄漏其数组
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1868`（旧缓存来自 1855-1867）
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：同一 `function_node` 在不同 module/source/scope/using_scope/context/owner/parameter_count 下再次解析签名（如泛型实例化、跨模块复用、上下文变化）。
- 后果：`function_node.native_signature_cache = cache` 直接覆盖旧指针，旧 `KrtNativeSignatureCache` 及其 `parameters/parameter_starts/parameter_lengths` 三个数组无法再被释放；缓存随解析次数增长而泄漏。
- 最小修复：覆盖前 `delete` 旧缓存的三个数组与缓存对象，或让旧缓存挂入待释放链。
- 备注：`KrtNativeSignatureCache` 字段定义于 `CompilerState.krt:46-61`，确认为三个数组 + result。

##### [中] 81-8｜泛型注册表达到容量上限后静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1337`（调用方 688、718，位于本切片外）
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：实例数增长到 `(count+1)*2 >= capacity` 且 `KrtGenericRegistryGrow` 因 `capacity > 131072` 返回 false。
- 后果：`KrtGenericRegistryAdd` 返回 false；688/718 行调用方直接 `return null`，泛型实例静默丢失，后续可能以 null 继续或报无关错误。
- 最小修复：Add 失败时向调用方传递可诊断错误（设置 `module.native_error` 并发 E_GENERIC），调用方不得静默返回 null。
- 备注：容量上限 131072 为硬编码。

##### [中] 81-9｜回调标签合并可能截断参数表而 parameter_count 不变
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1764-1770`（计数赋值 1755）
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：`KrtNativeMergeCallbackLabels` 合并的两个回调形状参数个数少于 `shape.parameter_count`（左/右链表先于 original 结束）。
- 后果：`anonymous.declarations` 只重建到最短的 left/right 长度，但 `anonymous.parameter_count` 仍取 `shape.parameter_count`；后续按 parameter_count 遍历 declarations 会越读 null，或标签/参数错位。
- 最小修复：合并前校验三者参数个数一致，不一致直接返回（或同步调正 parameter_count 并报告冲突）。
- 备注：调用方 `KrtNativeBindNullCoalescing` 在指针分支做过 `KrtNativePointerEqualIgnoringOuter`，但是否覆盖参数个数无法确认，标「疑似」。

##### [中] 81-10｜NullConditional 结果可空性只认指针，遗漏数组/字符串
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1842`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：`node.right` 为数组或字符串（`KrtNativeReferenceValue` 为真但 `KrtNativePointerValue` 为假）。
- 后果：`node.native_nullable` 不被置真，而 `?.` 在左值为 null 时结果可为 null；与 1786 行允许数组/字符串作为 reference value 的约定不一致，可能导致空值检查被优化掉。
- 最小修复：改为 `if (KrtNativeReferenceValue(node)) { node.native_nullable = true; }`，或显式覆盖 array/string。
- 备注：`KrtNativePointerValue` 定义于 `Semantic.krt:224`，从 1726 行将 array/string 与 pointer 并列推断其不含 array/string，标「疑似」。

##### [中] 81-11｜switch 用例表达式非常量未诊断且去重被跳过
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2153`（绑定 2149）
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：case 表达式不是编译期常量（`KrtIrTryEvaluateConstant` 失败），或两个非常量表达式实际相等。
- 后果：兼容性检查通过后不报错；`KrtNativeSwitchSameConstant` 对非常量返回 false，重复用例检测被静默跳过，后续代码生成阶段才可能失败或生成错误跳转表。
- 最小修复：对 case 表达式强制常量求值，失败时发 E_CONSTANT 诊断；非常量一律报错而非仅跳过去重。
- 备注：当前代码未要求 case 常量，属于阶段间约束缺失。

##### [中] 81-12｜switch 比较节点宽度硬编码，与操作数类型无关
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2157`
- 类别：6 契约破坏 / 疑似
- 触发条件：switch 目标为字符串、wide（128 位）或大于 8 位的整数。
- 后果：`comparison.native_bits = 8; native_width = 1` 固定，未使用 `saved.native_bits/native_width`；若后端按此类型生成比较，宽值/字符串比较的位宽与语义可能不符。
- 最小修复：从 `saved`（或操作数公共类型）复制 `native_bits/native_width`，字符串走专用比较路径。
- 备注：字符串比较在 `KrtNativeSwitchSameConstant` 有专门分支，但比较节点类型未体现，标「疑似」。

##### [中] 81-13｜合成基类构造调用未检查 method.left 为空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1400-1403`
- 类别：8 未检查前置条件
- 触发条件：`method.op == 220` 且非 external_constructor、`type.native_base != null`，但 `method.left`（函数体块）为 null。
- 后果：1403 访问 `method.left.left` 并写 `method.left.child_count`，空指针解引用，编译器崩溃而非诊断。
- 最小修复：进入该分支前检查 `method.left != null`，否则发 E_LAYOUT/E_SYNTAX 并返回 false。
- 备注：正常构造函数应总有体块，但语法错误路径下不能保证，标「疑似」。

##### [中] 81-14｜签名类型核心未检查 parameter_type_starts/lengths 为空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1877-1878`
- 类别：8 未检查前置条件
- 触发条件：`function_node.parameter_count > 0` 但 `parameter_type_starts` 或 `parameter_type_lengths` 为 null（未预备好的函数节点）。
- 后果：直接下标访问 null 数组，崩溃；且 1876 只检查 index 范围，不检查数组存在。
- 最小修复：访问前检查两个数组非空，缺失时回退到 `KrtNativeAnnotateType` 或返回 false。
- 备注：缓存路径 1887 同样依赖这些数组。

##### [中] 81-15｜语义布局阶段反向调用 IR 常量求值与表达式绑定
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2044-2049`
- 类别：4 阶段不对齐 / 疑似
- 触发条件：解析内联数组长度表达式，调用 `KrtNativeBindExpression`（前端绑定）与 `KrtIrTryEvaluateConstant`（Middle/IR）。
- 后果：前端语义阶段依赖中间端 IR；若长度表达式引用正在布局中的结构体常量，可能形成布局↔绑定↔IR 的递归或顺序倒置，且伪造的 `constants.unit` 会承载槽位/帧分配后即丢弃。
- 最小修复：把数组长度常量求值移到独立的常量折叠阶段，或提供不依赖完整 IR 的常量求值入口，并明确伪造 unit 的字段初始化。
- 备注：`KrtIrTryEvaluateConstant` 位于 `Middle/Ir/Ir.krt`，阶段倒置可确认，递归风险标「疑似」。

##### [中] 81-16｜类型参数（value==4）带约束时直接判继承失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1383`
- 类别：3 语义分叉 / 2 诊断缺失 / 疑似
- 触发条件：`type.value == 4` 且 `type.left != null`（类型参数带约束/基类列表）。
- 后果：返回 false，既不解析约束也不发诊断；`KrtNativeResolveInheritance` 将其视为解析失败并置 `module.native_error`，受约束泛型参数无法通过继承解析。
- 最小修复：对 value==4 解析 `type.left` 中的约束并递归解析，或明确不支持时发 E_GENERIC 诊断。
- 备注：value 4 的语义由 `Types.krt:5/145`、`Semantic.krt:663/932` 等推断为类型参数，标「疑似」。

##### [低] 81-17｜泛型注册表 Grow 失败路径状态不一致且泄漏旧数组
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1307`
- 类别：9 错误处理不一致 / 10 资源与生命周期 / 疑似
- 触发条件：rehash 时 `KrtGenericRegistrySlot` 返回 -1（当前实现下因装载因子 0.5 实际不可达）。
- 后果：返回 false 前 `registry.capacity` 已更新、四个新数组已分配，旧数组未 delete（1313 未执行），注册表处于半迁移状态。
- 最小修复：失败时回滚 capacity/数组指针，或先在临时结构完成 rehash 再提交。
- 备注：触发条件标「疑似」，但错误路径本身无回滚。

##### [低] 81-18｜KrtNativeLayoutProbe 为静态全局共享可变状态
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1944`、`2016-2018`
- 类别：7 共享可变状态
- 触发条件：布局注解期间任意递归/重入（类型注解触发另一次布局或签名解析）。
- 后果：全局标志跨模块、跨类型共享；虽然 2016-2018 保存/恢复，但恢复值是布尔旧值，嵌套probe 内的判定会相互覆盖，且与 `module.native_closure_probe_state` 两套机制并存，易漂移。
- 最小修复：把 probe 标志放入 module/绑定上下文，或统一由 `native_closure_probe_state` 判定。
- 备注：`Semantic.krt:672` 也读取该全局量。

##### [低] 81-19｜探针继承栈复制可能留下 cycle_depth>0 而 cycle_types 为空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1173`、`1177-1180`
- 类别：8 未检查前置条件 / 疑似
- 触发条件：`original.cycle_types == null` 但 `original.cycle_depth > 0`（当前 Push 实现下不变量成立，故为潜在隐患）。
- 后果：复制体的 `cycle_types` 保持 null 而 `cycle_depth` 非 0；`KrtInheritanceDiagnostic`（1484）只检查 `cycle_depth == 0`，随后遍历 null 数组崩溃。
- 最小修复：复制时若 `cycle_types == null` 则强制 `cycle_depth = 0`，或诊断处同时检查 `cycle_types != null`。
- 备注：标「疑似」，依赖不变量。

##### [低] 81-20｜探针诊断上限硬编码默认 128，与驱动配置可能漂移
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1199`
- 类别：11 配置漂移 / 疑似
- 触发条件：`state.diagnostics == null`（探针开始时无当前诊断对象）。
- 后果：新诊断对象 limit 固定 128，忽略命令行/配置中的诊断条数上限；不同入口默认值可能不一致。
- 最小修复：从驱动级默认配置读取 limit，或在无当前诊断时继承编译会话默认值。
- 备注：标「疑似」，需核对驱动默认值。

##### [低] 81-21｜继承环诊断的堆字符串所有权不明，存在泄漏风险
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1487-1489`
- 类别：10 资源与生命周期 / 疑似
- 触发条件：发生继承环，走 `KrtDriverCopyString` + `KrtDriverConcat` 生成消息并传入 `KrtDiagnosticMessage`。
- 后果：`KrtErrorReportAdd`（`CompilerState.krt:277`）仅保存 `record.message` 指针，记录删除（1232）不释放字符串；`text` 与 `message` 可能泄漏。其他调用点多传字面量，故仅此处为堆串。
- 最小修复：明确消息所有权并在记录释放时释放，或让诊断接口复制字符串。
- 备注：未确认 Krt 字符串是否由运行期统一回收，标「疑似」。

##### [低] 81-22｜NullConditional 未检查右值为 void
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1833-1843`
- 类别：2 诊断缺失 / 疑似
- 触发条件：`a?.b` 右侧表达式类型为 void。
- 后果：直接 `KrtNativeCopyType(node,node.right)` 并返回；是否由 `KrtNativeCheckValueAccess` 拦截无法确认，若未拦截则产生 void 类型表达式。
- 最小修复：绑定后显式检查 `KrtNativeVoidValue(node.right)` 并报错，与 `KrtNativeBindNullCoalescing` 1790 保持一致。
- 备注：标「疑似」。

##### [低] 81-23｜闭包签名路径提前返回，绕过回调标签合并
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1924`（对比 1927-1936）
- 类别：13 注释与实现脱节 / 疑似
- 触发条件：`function_node.native_closure_signature != null` 或 `native_closure_shape` 为真。
- 后果：在 1924 直接 return，不执行 1930-1936 的 `KrtNativeMergeCallbackLabels`；若闭包形状需要保留参数标签，会与注释声称的「不少于原命名契约」不一致。
- 最小修复：确认闭包形状是否已含标签；若需要，在 return 前同样合并 contract 标签。
- 备注：闭包描述通常是已预备的不可变数据，可能无需合并，标「疑似」。

##### [低] 81-24｜KrtNativeFieldOwner 把静态字段计入实例字段查找
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1368-1378`
- 类别：3 语义分叉 / 疑似
- 触发条件：类型含与实例字段同名的 static 字段，调用方按名查找字段所有者。
- 后果：返回静态字段所在的 type；若调用方用于实例成员偏移/访问，会把静态字段当实例字段处理。
- 最小修复：遍历字段时跳过 `field.native_static`，或提供独立的静态字段查找函数。
- 备注：未确认所有调用方是否自行过滤 static，标「疑似」。

### 第八十二批：SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt（全量）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt`，共 1466 行，已用 read 分三段（1-400、401-800、801-1466）全量读完。
- 覆盖子系统：访问检查（1-198）、装箱与转换（200-372）、闭包与捕获（374-798）、生成器（800-1035）、全局值（1037-1249）、迭代器绑定（1251-1466）。
- 交叉核对方式：仅按符号 grep / 小窗口读取其它文件（Parser.krt、Semantic.krt、Types.krt、CompilerState.krt、CompilerPipeline.krt、Ir.krt），未整文件通读；无法确认处标「疑似」。
- 本批共抽出 23 条：致命 1、高 5、中 12、低 5。


##### [致命] 82-1｜foreach 的 let/readonly 循环变量丢失 readonly 标志
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt:1405-1419`（缺 `variable.native_readonly = original.native_readonly`）；对照 `Parser.krt:2236`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：`foreach (let T x in c) { x = ...; }`（或推断的 `let` 变量）。
- 后果：合成 variable 只复制 `syntax_modifiers`(4096)；`KrtNativeAnnotateType` 不会据此重算 `native_readonly`（Semantic.krt:78-132），`KrtNativeCopyType` 也不复制 readonly（Semantic.krt:54-76），而标识符解析会读取声明的 `native_readonly`（Semantic.krt:627-629），赋值判定的 `KrtNativeIsLvalue`（Semantic.krt:191-192）因此看到 false，循环体内写入被静默放行并生成存储指令，违反 `let` 只读契约且无任何诊断。
- 最小修复：在 1409 行后补 `variable.native_readonly = original.native_readonly;`；若走 1415 的 `KrtNativeCopyType` 推断路径，需在该调用后再次确认该位未被覆盖。
- 备注：已核对 Parser、AnnotateType、CopyType、名字解析与 IsLvalue 全部链路，非疑似。

##### [高] 82-2｜KrtNativeValueCarriesAddress 对聚合类型图无环检测，可无界展开
- 位置：`SemanticFeatures.krt:402-429`（展开循环 412-426，容量倍增 416-421）
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：对含自引用或互相引用字段的值类型调用 `KrtNativeInheritStackBorrow`（Semantic.krt:684、1156、1187、2535）进入 402；典型如递归 struct。
- 后果：`pending` 反复翻倍直至内存耗尽或 `capacity*2` 溢出；编译挂起/崩溃，而不是给出 E_LAYOUT 类诊断。
- 最小修复：用已访问聚合类型集合去重（同一 `native_type` 只展开一次），并设深度上限，超限转为显式诊断。
- 备注：class 字段在 408-410 提前返回，不受影响；值类型递归本应被 layout 阶段拒绝，但该函数自身无任何防御，属纵深防御缺失。

##### [高] 82-3｜base 接收器在 native_base 为空时静默产出无类型节点
- 位置：`SemanticFeatures.krt:505`（配合 497-507、509-516）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：在无基类类型（或 `native_owner`/`native_base` 缺失）中使用 `base` 成员访问。
- 后果：`receiver.native_type = binding.unit.native_owner.native_base` 得到 null，经 `KrtNativeCopyType` 复制到 this 节点；后续 `KrtNativeDerivesFrom(null,...)` 等判定静默走偏，成员访问可能被静默拒绝或按错误类型绑定，全程无 E_ 诊断。
- 最小修复：`base_receiver` 分支先检查 `binding.unit.native_owner != null && native_base != null`，否则 `KrtNativeBindingError(binding,node)` 并设置明确错误码。
- 备注：疑似，需与 Re.KrtC 版对照确认 C 版是否在语义期报「无基类」。

##### [高] 82-4｜全局初始化类型不匹配失败不设错误码、不发诊断
- 位置：`SemanticFeatures.krt:1211-1213`（另见 1217）
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：非只读全局变量的初始化表达式与声明类型不兼容，或绑定失败且未产生任何诊断。
- 后果：只写 `state.output.error_start`；`module.native_error` 可能保持 0，CompilerPipeline.krt:511 落入泛化 `E_LOWER`，真实类型错误信息丢失；`binding.error_start` 的精确位置还被 `declaration.start` 覆盖。
- 最小修复：失败时设置专门错误码（新增 E_GLOBAL_INIT 或复用既有转换/参数码），并在 1211 保留 `binding.error_start >= 0 ? binding.error_start : declaration.start` 之外补位置来源标注。
- 备注：与同文件 `KrtNativeIteratorError`（1256-1258）显式设码+发诊断的做法形成直接不一致。

##### [高] 82-5｜KrtNativeRegisterGlobal 失败路径无错误码且先污染全局状态
- 位置：`SemanticFeatures.krt:1121-1162`（重复定义 1131；尺寸/对齐 1148、1150；符号名 1155；上限 1152、1160；entry 插入与 context 写入 1135-1142）
- 类别：2 诊断缺失 / 9 错误处理不一致 / 10 资源与生命周期
- 触发条件：重复全局名、storage_size 越界、对齐后偏移越界、链接符号名无效、`global_count > 65536`。
- 后果：(a) 各失败分支只设 `output.error_start`，不设 `module.native_error`，上层按旧码映射，通常误报 E_LOWER；(b) entry 在校验前已插入 `state.first`、`state.changed=true`、`declaration.native_context=owner`，失败即留下半初始化表项；FinishGlobals 1247 以 `native_global-1` 索引 `global_entries`，坏项残留会负索引越界。
- 最小修复：先校验后插入；失败时设置专门错误码并用 `declaration.start`/`type_start` 定位，同时回滚已做的 `native_context` 修改。
- 备注：正常调用链失败后中止，尚未发现二次使用坏表项，故定为高而非致命。

##### [高] 82-6｜生成器方法构造失败静默返回 null/false
- 位置：`SemanticFeatures.krt:987`（SignatureType 失败 `return null`）、`1033`（判空后仅 `return false`）
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：生成游标类型/方法的签名解析失败（如 Current/MoveNext 契约签名异常）。
- 后果：不设 `module.native_error`、不发 `KrtDiagnostic`，`binding.error_start` 可能停留在旧值或为 -1，编译以泛化 E_LOWER 结束且位置错误；与 `KrtNativePrepareGenerator` 其它失败统一走 `KrtNativeIteratorError` 的做法不一致。
- 最小修复：`KrtNativeGeneratorMethod` 失败时调用 `KrtNativeIteratorError(binding,plan.factory)`，或在 FinishGenerator 1033 补设错误码与位置后再返回。
- 备注：疑似点在于 `KrtNativeSignatureType` 是否已自行写输出错误；无论其是否写，此处都未设置 `binding.module` 错误码。

##### [中] 82-7｜KrtNativeFindGlobal 把「二义性」折叠为 null
- 位置：`SemanticFeatures.krt:1100-1109`（1104 直接 `return null`；hidden/found 规则 1077 vs 1102）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：两个 using 导入提供同名可见全局且文件优先级相同（`priority == best` 且 `found != declaration`）。
- 后果：歧义被吞掉，调用方按「未找到」处理，最终报未定义类错误，位置与真实原因错位；两段扫描的可见性规则重复实现且条件不同，易漂移。
- 最小修复：引入可区分的「歧义」返回并给二义性诊断；抽取统一的名字可见性/优先级 helper。
- 备注：疑似，取决于语言是否允许同名 using 导入共存。

##### [中] 82-8｜常量全局每次引用重复求值并回写 binding.slots
- 位置：`SemanticFeatures.krt:1040-1055`（1053 `binding.slots = constant_binding.slots`；1054 用使用点覆盖错误位置）
- 类别：7 共享可变状态 / 10 资源与生命周期
- 触发条件：同一 readonly 全局被引用多次，尤其常量链（A=B+C 之类嵌套常量）。
- 后果：每个引用点都重新 `KrtNativeBindConstant`；临时槽位随引用次数单调增长造成帧膨胀，嵌套常量重复求值呈乘法放大；子绑定的精确 `error_start` 也被使用点位置覆盖。
- 最小修复：按 declaration 缓存已求值常量的类型/值，或使用独立临时槽区间并在求值后恢复 `binding.slots`。
- 备注：与 1190-1225 全局初始化器「只绑一次」的做法形成对照。

##### [中] 82-9｜闭包子绑定不继承 constant_depth
- 位置：`SemanticFeatures.krt:683-684`（对照 1048）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：lambda 体内引用常量、或触发常量递归保护路径。
- 后果：`child.constant_depth` 从默认 0 开始，与外层及全局常量求值的深度计数不一致，递归保护行为分叉，可能出现更深递归或栈开销异常。
- 最小修复：`child.constant_depth = binding.constant_depth;`。
- 备注：疑似，需确认 lambda 体内是否参与常量求值。

##### [中] 82-10｜探针隔离不一致：InferShape 复用真实 unit 且捕获表修改未 Journal
- 位置：`SemanticFeatures.krt:786-789`（`probe.unit=binding.unit`、`probe.parent=binding.parent`）；`439-460`（459 直接改 `native_captures`）；`Types.krt:1097-1106`（Journal 需显式调用）
- 类别：7 共享可变状态 / 9 错误处理不一致
- 触发条件：带捕获的 lambda 走 `KrtNativeClosureInferShape`，探测期间任何路径写 `probe.unit`（例如向 `binding.unit.native_captures` 追加、写 closure_return_* 状态）。
- 后果：`KrtNativeClosureProbeEnd` 只回滚被 Journal 的节点，`native_captures` 追加未经 Journal，探测状态会泄漏进已提交 AST；与其它探针（355、549 新建 Function 节点）约定不一致。
- 最小修复：像 BoxContextArgumentScore/ClosureArgumentScore 一样创建独立 `probe.unit`；或在修改前调用 `KrtNativeClosureProbeJournal(module,binding.unit)`。
- 备注：疑似——当前 InferShape 主路径可能只读 unit，属防御缺失而非已证实的活跃泄漏。

##### [中] 82-11｜KrtAstMake 返回值普遍未判空即解引用，Begin/End 检查不一致
- 位置：`SemanticFeatures.krt:355、549、652、676、686、700、743、760、909、952、961、970、984、989`；另 692 直接取 `lambda.left.kind`；Begin 返回值检查 352-353 有、547/784 无
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：AST 分配失败（内存压力/分配器返回 null），或空 lambda 体使 `lambda.left` 为 null。
- 后果：NULL 解引用崩溃，无诊断；同为探针入口，352-353 判空而 547/784 把 Begin 结果直接交给 End，错误处理策略不统一。
- 最小修复：统一安全分配辅助函数，失败走 `KrtNativeBindingError`；`KrtNativeClosureProbeBegin/End` 配对统一判空。
- 备注：疑似，取决于 KrtAstMake 的失败契约（是否会返回 null）。

##### [中] 82-12｜访问检查在 bundle 缺失时两条路径结论相反
- 位置：`SemanticFeatures.krt:36-37`（SameFile 返回 true）对 `52-53`（SeparateFiles 返回 false）
- 类别：3 语义分叉 / 9 错误处理不一致 / 1 静默失败
- 触发条件：`KrtDriverActiveBundle == null` 的语义查询/测试路径。
- 后果：AllowedDepth 第 80 行用 SameFile 判定 file-private，bundle 缺失即放行；而 SeparateFiles 对同一前提一律返回 false（也即不分离）。两条路径策略相反，访问控制可静默失效或误严。
- 最小修复：统一空 bundle 策略（建议均拒绝并报诊断），抽公共 helper。
- 备注：真实驱动下 bundle 通常非空，故为中；但与 49-51 注释「未知位置永不例外」相悖。

##### [中] 82-13｜扩展方法候选只取第一个，未比较接收者特异性
- 位置：`SemanticFeatures.krt:1272-1282`（1277 首个 extension 即采用）；使用点 1365、1374、1390、1397
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：同名扩展 GetEnumerator/MoveNext/Current 存在多个候选。
- 后果：静默选择模块顺序中的第一个，可能调用错误扩展方法；无二义性诊断。
- 最小修复：收集全部候选，按接收者距离/签名排序，平局时报错。
- 备注：实例方法优先于扩展（1276）本身合理，问题仅在扩展之间。

##### [中] 82-14｜foreach 的 Current 字段查找不检查访问级别与隐藏
- 位置：`SemanticFeatures.krt:1284-1294`；直到 1388 才检查 static/global
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：Current 是基类私有字段、静态字段，或被派生类隐藏的同名字段。
- 后果：私有/隐藏字段可能被合成 foreach 读取，破坏封装；静态字段虽在绑定后才报错，诊断阶段滞后、位置不准。
- 最小修复：查找时过滤 `native_static`，按访问级别与 owner 可见性筛选；命中多个报二义性。
- 备注：疑似——`KrtNativeBindExpression(current)` 内部可能有访问检查，但未在本次核对中证实。

##### [中] 82-15｜Dispose 仅认实例方法且缺失被静默接受
- 位置：`SemanticFeatures.krt:1396-1402`（1397 `extensions=false`）
- 类别：6 契约破坏 / 1 静默失败 / 12 测试不足
- 触发条件：枚举器把 Dispose 实现为扩展方法，或完全未提供 Dispose。
- 后果：前者不会在 finally 中释放（资源泄漏）；后者无诊断/警告，迭代器契约只被部分强制。
- 最小修复：对「应为 IDisposable 却找不到实例 Dispose」给警告；或明确模式要求并补测试。
- 备注：疑似，语言可能刻意允许可选 Dispose。

##### [中] 82-16｜无目标类型的 `() => null` 推断被拒且无错误码
- 位置：`SemanticFeatures.krt:608-619`（Null 分支）、`712-714`（kind==Null 直接 BindingError）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`var f = () => null;` 之类无期望类型的 null 返回 lambda 推断。
- 后果：`shape.native_type.kind == Null` 时仅 `KrtNativeBindingError`（不设码），上层走泛化诊断甚至无诊断；615-620 对 Null→指针/可空的处理顺序脆弱，依赖多处特判。
- 最小修复：把 Null 作为可空引用/指针候选参与兼容推断；失败时设置明确错误码。
- 备注：疑似，需确认 Kairote 是否要求此类 lambda 显式注解。

##### [中] 82-17｜generator 原函数体与生成 MoveNext 方法共享同一 AST 体
- 位置：`SemanticFeatures.krt:908`（`plan.body = unit.left`）、`983`（role 3 的 `method.left = plan.body`）
- 类别：7 共享可变状态 / 13 注释与实现脱节
- 触发条件：任意 generator 函数；IR 阶段对语句做 resume/状态改写，或其它阶段再次遍历 factory 体时。
- 后果：两侧互为别名，任何一侧的语句级修改都会影响另一侧；801-803 注释声称「源体只绑定一次」，但未声明所有权与禁止二次发射，仅靠约定维持。
- 最小修复：为生成方法克隆 body（`KrtNativeCloneAst`），或显式置空 `unit.left` 并断言单所有者。
- 备注：疑似——当前 IR 似乎只 lowering role 3，工厂体不再发射。

##### [中] 82-18｜KrtNativeFinishGlobals 未检查 state，越界检查在状态变更之后
- 位置：`SemanticFeatures.krt:1228-1248`（1244 先 `global_count += 2` 再判 `>65536`；1247 以 `native_global-1` 索引）
- 类别：8 未检查前置条件 / 2 诊断缺失 / 9 错误处理不一致
- 触发条件：未调用 PrepareGlobals、或 guard 加入后 `global_count` 超 65536。
- 后果：state 为 null 时 NULL 解引用；超限路径已改动 `output.global_count`/`initializer_guard_slot` 却只 `return false`（无错误码/位置），留下半成品输出；坏 entry（见 82-5）残留时 1247 负索引越界。
- 最小修复：入口检查 `module.native_globals`；把上限检查移到修改之前并设置错误码与位置。
- 备注：疑似，正常流程会先 Prepare。

##### [低] 82-19｜魔数与上限散落、递归深度阈值不一致（配置漂移）
- 位置：`SemanticFeatures.krt:1014`（current_offset=32）、`1220`/`1243`（异常帧 24 槽）、`1019`（131060）、`1148`/`1150`（16777216）、`1152`/`1160`（65536）、`742`（192）对比 `14`/`24`/`72`/`113`/`141`（64）；`976-979` 每方法 5×128 数组而 parameter_count 恒为 1
- 类别：11 配置漂移 / 13 注释与实现脱节
- 触发条件：ABI、帧布局或上限调整时需同步多处。
- 后果：漏改即引发布局/诊断漂移；64 与 192 两种递归上限来源无注释说明；固定 128 容量属死容量，与契约脱节。
- 最小修复：集中常量并注明推导来源，统一深度上限，按实际上限分配参数数组。
- 备注：低。

##### [低] 82-20｜KrtNativeAccessError 无条件覆盖模块错误码
- 位置：`SemanticFeatures.krt:96-109`（97 置 10）
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：调用方已设置更具体的 `native_error`/`candidate_error` 后再走访问错误。
- 后果：具体错误码被冲刷为 E_ACCESS；candidate 机制（Semantic.krt:308 仅保存 code==10）进一步放大不一致，真实原因丢失。
- 最小修复：仅在 `module.native_error == 0` 时设置，或定义统一的错误码优先级。
- 备注：低。

##### [低] 82-21｜ValidateAccess 诊断与错误码双轨，依赖调用方短路防重复
- 位置：`SemanticFeatures.krt:163-198`（168/174/184/192 直发 KrtDiagnostic + 196 `native_error=11` + `output.error_start`）
- 类别：9 错误处理不一致 / 12 测试不足
- 触发条件：非法修饰符；以及其它调用点（Ir.krt:2325、Types.krt:1411）之后继续执行时。
- 后果：CompilerPipeline.krt:494 用 `has_errors` 短路避免二次报错，但「直发诊断」与「设码+位置」两套约定并存，换调用点即可能重复诊断或漏报。
- 最小修复：统一为单一出口（要么只设码+位置由上层映射，要么只直发诊断）。
- 备注：低。

##### [低] 82-22｜yield 探测与校验各自重复 AST 遍历规则
- 位置：`SemanticFeatures.krt:844-853`（ContainsYield 只走 left/right/alternate/body/next，Lambda 整棵跳过）对比 `857-876`（ValidateSource 特判 Try 的 right=catch 链、alternate=finally）
- 类别：13 注释与实现脱节 / 12 测试不足
- 触发条件：新增语句种类或改变子节点字段约定时。
- 后果：两处遍历规则漂移会导致 yield 漏检（函数不被识别为 generator，随后误报 E_ITERATOR）或漏校验。
- 最小修复：抽统一的「遍历全部子节点」helper，Try 的特判集中一处。
- 备注：现有 Switch/Catch 布局经核对可被覆盖，故为低。

##### [低] 82-23｜KrtNativeBoxStorage 缺少 type 判空
- 位置：`SemanticFeatures.krt:262-277`（263 起直接解引用）
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：传入 null 类型（同文件 `KrtNativeBoxableValue`:238、`KrtNativeReferenceObjectValue`:232 均判空）。
- 后果：与相邻 helper 的前置条件不一致，null 时直接崩溃而非 false。
- 最小修复：入口 `if (type == null) { return 0; }` 或明确标注非空契约并让调用方保证。
- 备注：疑似，现核对了 Ir.krt:35 与 SemanticRuntime.krt:1024 两处调用均传入非 null shape。

### 第八十三批：SelfHost/Compiler/Backend/Vm/Interpreter.krt（全量）

#### 覆盖范围
- 目标文件：`/sdcard/KairoteLang/SelfHost/Compiler/Backend/Vm/Interpreter.krt`，共 1357 行，分 3 段（1-460、461-920、921-1357）用 read 全量读完。
- 内容覆盖：EBC v1/v2-v6 序列化与反序列化（`KrtVmWrite/Read`、`KrtVmEmit*`、`KrtVmReadV2`）、校验（`KrtVmValidateVersion`）、运行时（region/allocation/frame/handler）、执行主循环（op 0-76）、宽整数（`KrtVmWide*`）、legacy v1 解释器、runtime 类型校验。
- 对照：`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.c` 只实现 legacy EBC v1（`OP_CONSTANT/OP_SET_LOCAL/...`），不覆盖本机 EBC v2-v6 的 op 语义，故 v2-v6 条目无法与该 C 文件逐条对照；凡依赖 Kro/x86 行为推断的均标「疑似」。
- 结论：共 24 条问题（致命 1、高 2、中 8、低 13）。


##### [致命] 83-1｜整数溢出绕过 op 50/51/53/54 的 locals 边界校验，造成负偏移越界读写
- 位置：`Interpreter.krt:91-92`（校验）、`Interpreter.krt:806`（op 52 写）、`Interpreter.krt:810`（op 53/54 读）
- 类别：未检查前置条件 / 整数溢出 / 内存安全
- 触发条件：字节码中 op 50 的 `extra`（或 op 51/53/54 的 `argument`）取接近 `INT64_MAX` 的值（如 `9223372036854775802`），使 `extra + 6` 回绕为负，`extra + 6 > locals` 不成立，校验被绕过。
- 后果：`handler.slot` 被截断为 int32 负值；op 52 按 `(slot+1)*8`、`(slot+2)*8` 向 `frame.locals` 负偏移写 type/value，op 53/54 从负偏移读；`KrtVmWrite/KrtVmRead` 本身无区域检查，形成堆外读写，可崩溃或破坏相邻帧/池内存。
- 最小修复：校验改为 `extra < 0 || extra > locals - 6`（并保证 `locals >= 6`），`argument` 同理；执行端再对 slot 做非负与上界断言。
- 备注：若干脆让 `extra + 6` 触发有符号溢出陷阱，则变成对恶意字节的拒绝服务；两种语义都需显式处理。

##### [高] 83-2｜op 33（post.increment）接受 width=16，但宽值 scratch 未分配且实现按指针算术
- 位置：`Interpreter.krt:81`（校验允许）、`Interpreter.krt:732`（执行）、`Interpreter.krt:926-931`（layout）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：op 33 的 `extra=16`（`KrtVmWidth(16)` 为真，校验接受）。
- 后果：v6 下 `KrtVmLoad(width=16)` 调用 `KrtVmWideBox`，而 `KrtVmWideScratchBytes` 对 op 33 返回 0，`layout.offsets[index] = -1`，`KrtVmWideBox` 因 `wide_cursor < 0` 置 `state.failed`，合法指令必报 E_VM；即使在非 inline 的外部 v2-5 字节码下，`value + argument` 也是把 box 指针当整数加，结果错误。
- 最小修复：校验层禁止 op 33 使用 width=16（宽值自增应只走 op 67），或为 op 33 width=16 正确实现宽值加并分配 16 字节 scratch。
- 备注：当前 Ir 未发射 op 33 width=16（宽值走 op 67），故为潜在/外部字节码触发；但校验器与执行器对同一编码的接受性不一致本身是硬缺陷。

##### [高] 83-3｜op 26 在操作数栈下溢时仍执行 syscall
- 位置：`Interpreter.krt:721-725`、`Interpreter.krt:492-497`
- 类别：未检查前置条件 / 错误处理不一致
- 触发条件：op 26 的 `extra > 0` 而当前帧可用栈值不足；`KrtVmPop` 置 `state.failed` 并返回 0。
- 后果：`arguments` 数组部分或全部为零，仍调用 `KrtVmSystemCall` 执行 mmap/munmap/open/read/write 等系统调用，产生真实副作用后才在循环顶部因 `state.failed` 退出。
- 最小修复：在填充 `arguments` 前检查 `state.depth - frame.stack_base >= extra`，不足则置 `state.failed` 且不调用 syscall。
- 备注：`KrtKroNativeStackDepths` 静态分析通常能拦截，但运行时无兜底，且静态分析一旦与运行时栈基口径不同即被穿透。

##### [中] 83-4｜KrtVmAllocate 失败静默返回 0，未置 state.failed
- 位置：`Interpreter.krt:509-524`；调用点 `Interpreter.krt:719`（op 24）、`Interpreter.krt:729`（op 30）、`Interpreter.krt:833-836`（op 66）
- 类别：静默失败 / 诊断缺失
- 触发条件：`count < 0`、`stride < 0`、乘法/加法溢出或 `syscall(9)` 返回负。
- 后果：op 24/30/66 把 0 压栈后继续执行，直到后续内存访问才报 E_VM，报错位置与根因偏离；负数 count 本应是运行期错误却被当作空指针。
- 最小修复：失败时置 `state.failed = true` 并返回 0，或在压栈前检查返回值。
- 备注：需与 Kro 后端对分配失败的处理对齐（疑似不一致）。

##### [中] 83-5｜栈/宽布局分配失败静默返回 1 且泄漏 wide_layout
- 位置：`Interpreter.krt:663-667`
- 类别：资源与生命周期 / 诊断缺失
- 触发条件：`KrtDriverAlloc(2097152)` 失败，或 `KrtVmMakeWideLayout` 因单函数 scratch 超过 64MiB 返回 null。
- 后果：两条路径都只 `return 1`，无 `KrtDriverError`；`state.stack == 0` 分支直接 `delete state`，若 wide_layout 已创建则泄漏。
- 最小修复：两处失败均调用 `KrtDriverError("E_VM", ...)`；`stack == 0` 分支先释放 `state.wide_layout`。
- 备注：`KrtDriverAlloc` 自身失败不诊断，调用方必须补。

##### [中] 83-6｜globals 分配失败静默返回 1
- 位置：`Interpreter.krt:673-676`
- 类别：诊断缺失 / 静默失败
- 触发条件：`KrtDriverAlloc((int64)module.global_count * 8) == 0`。
- 后果：直接 `KrtVmDispose(state); return 1;`，无任何 E_VM 诊断，调用方无法区分是模块错误还是内存不足。
- 最小修复：失败前调用 `KrtDriverError("E_VM", ...)`。
- 备注：与 83-5 同类但位置独立。

##### [中] 83-7｜用 syscall munmap 释放 KrtDriverAlloc（bump pool）内存，页对齐会拆掉共享池
- 位置：`Interpreter.krt:618-619`（stack/globals）、`Interpreter.krt:914`（runtime_pointer）；分配点 `Interpreter.krt:660`、`Interpreter.krt:674`、`Interpreter.krt:1345`
- 类别：资源与生命周期 / 契约破坏
- 触发条件：VM 执行结束或失败清理时。
- 后果：`KrtDriverAlloc` 以 1MiB 起、页对齐的 mmap 作为 bump pool（`PlatformAbstraction.krt:23-30`）；`munmap(state.globals, global_count*8)` 或 `munmap(runtime_pointer, runtime_length)` 会按页拆掉同一池中相邻的编译器对象，后续分配/使用可能崩溃。
- 最小修复：VM 自有内存统一走 `KrtVmAllocate`（直接 `syscall(9)`）或提供配套释放；不要对 pool 指针直接 munmap。
- 备注：疑似，取决于驱动在 VM 运行后是否继续使用该池；2MiB 的 stack 通常独占映射，风险较低，globals/runtime_pointer 风险高。

##### [中] 83-8｜反序列化后的 module 未设置 runtime_bytes/data_storage，module 不自洽
- 位置：`Interpreter.krt:190-221`（ReadV2）、`Interpreter.krt:1307`（KrtVmRuntimeValidate）、`Interpreter.krt:1322`（Emit）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：`KrtVmReadV2` 从产物加载带 runtime 的模块。
- 后果：`module.runtime_bytes` 保持 null，`module.data_storage` 也未赋值；`KrtVmRuntimeValidate(module)` 会对已加载模块返回 false，任何依赖这两个字段的后续阶段（再发射、跨后端交换）拿到空数据。
- 最小修复：ReadV2 成功后令 `runtime_bytes` 指向 `runtime_pointer`（或复制），`data_storage` 指向 `module.data`。
- 备注：当前执行路径不读 `runtime_bytes`，故未立即暴露，属契约层面的阶段不对齐。

##### [低] 83-9｜反序列化失败路径泄漏 module 数组
- 位置：`Interpreter.krt:200-221`、`Interpreter.krt:1333-1346`
- 类别：资源与生命周期
- 触发条件：ReadV2/RuntimeRead 分配数组后，因范围校验或 `KrtDriverAlloc` 失败返回 false。
- 后果：`ops/args/extra/starts/local_counts/parameter_counts/...` 与 `runtime_offsets/runtime_relocations` 未释放。
- 最小修复：失败返回前 `delete` 已分配数组与 relocation 链表。
- 备注：进程生命周期短，影响低。

##### [低] 83-10｜KrtVmRun 全路径不回收 artifact.data
- 位置：`Interpreter.krt:902-916`
- 类别：资源与生命周期
- 触发条件：每次 `KrtVmRun`（v1 与 v2-6 路径）。
- 后果：整个字节码缓冲（最大 64MiB）来自 `KrtDriverAlloc` bump pool，运行后不回收，重复运行会持续增长。
- 最小修复：执行结束后释放 artifact.data（注意 v2 的 `module.data` 指向其内部，需先执行完）。
- 备注：若 `krt run` 为一次性进程则影响有限。

##### [低] 83-11｜KrtVmReadArtifact 失败路径泄漏 data
- 位置：`Interpreter.krt:887-896`
- 类别：资源与生命周期
- 触发条件：读中断（`count <= 0`）或尾读非 0。
- 后果：已分配的 `data` 未释放即返回 false。
- 最小修复：失败前释放 `data`。
- 备注：—

##### [低] 83-12｜op 69 WideRef 为死代码且 scratch 计算漏项
- 位置：`Interpreter.krt:100`（v6 拒绝）、`Interpreter.krt:158`（emit 提升到 6）、`Interpreter.krt:844-848`（执行）、`Interpreter.krt:926-931`（layout）
- 类别：死代码 / 阶段不对齐
- 触发条件：Ir 中 `KrtNativeOp.WideRef` 无任何发射点；emit 遇到 op 69 会先提升到版本 6，再被校验拒绝。
- 后果：VM/Kro 保留不可达分支；若外部 v2-5 字节码含 op 69 且 `pointer == 0`，执行期 `KrtVmWideBox` 需 16 字节 scratch，而 `KrtVmWideScratchBytes` 未为其预留（仅靠非 inline 走 heap 才侥幸工作），属布局约定缺口。
- 最小修复：删除 op 69 相关分支，或在校验层明确拒绝并同步移除 Kro 侧死分支。
- 备注：—

##### [中] 83-13｜op 60-68 缺 version>=6 前置校验，宽操作可在非 inline ABI 下执行
- 位置：`Interpreter.krt:93-102`、`Interpreter.krt:154-160`
- 类别：阶段不对齐 / 契约破坏
- 触发条件：外部/手工 version 2-5 字节码含 op 60-63、66-68。
- 后果：校验接受这些宽操作，执行时 `wide_inline=false`，宽值改走堆 box，与 v6 的「持久 inline 存储」ABI 不同；同一字节码在 v5 与 v6 下内存布局/语义契约不一致。
- 最小修复：对 op 60-69 统一增加 `version >= 6` 校验（op 69 维持现有更严规则）。
- 备注：emit 总会把含这些 op 的模块提升到 6，故仅外部字节码触发。

##### [低] 83-14｜op 61/62 的 extra（无符号标志）未校验
- 位置：`Interpreter.krt:820-823`、`Interpreter.krt:94-95`
- 类别：未检查前置条件 / 语义分叉
- 触发条件：`extra` 为任意非 0（如 2、-1）。
- 后果：VM 一律按 unsigned 解释；若 Kro 侧按 `extra == 1` 判定，则同一字节码两后端结果不同。
- 最小修复：校验 `extra ∈ {0,1}`。
- 备注：疑似，需与 Kro 的 `arity != 0` 判定点对齐。

##### [中] 83-15｜宽整数移位掩码与 Kro/x86 可能不同
- 位置：`Interpreter.krt:976-977`
- 类别：语义分叉
- 触发条件：移位量在 64..127 之间。
- 后果：VM 用 `((int64)b & 127)`；x86 64 位移位指令硬件把计数掩码为 63，Kro 若直接发射 `shl/shr/shrd` 序列，计数 64..127 的结果与 VM 不同。
- 最小修复：确认 Kro 的 128 位移位实现（是否显式掩码 127），统一两端语义。
- 备注：疑似，需反汇编 Kro 输出或查 `KrtKroNativeWideBinary` 的移位序列确认。

##### [中] 83-16｜宽整数除法/取模未处理 INT128_MIN / -1 溢出
- 位置：`Interpreter.krt:978-982`
- 类别：未检查前置条件 / 语义分叉
- 触发条件：`a = INT128_MIN`、`b = -1`（操作码 47 或 37）。
- 后果：VM 走软件除法，结果回绕或未定义；Kro/x86 `idiv` 会触发 #DE 异常，两后端行为不同。
- 最小修复：除法前检查 `a == INT128_MIN && b == -1`，按语言语义报错或回绕。
- 备注：疑似；b == 0 已检查，仅溢出未检查。

##### [低] 83-17｜负 double → uint64 转换语义未定义/可能分叉
- 位置：`Interpreter.krt:329-338`（`KrtVmFloatConvert` 的 `target == -8` 分支）
- 类别：语义分叉
- 触发条件：op 38 `extra == -8` 且源值为负或超出 uint64 范围。
- 后果：Krt 软件转换与 x86 `cvttsd2si` 的 INT_MIN 饱和行为可能不同，VM 与 Kro 结果分叉。
- 最小修复：明确并统一越界/负数转换语义。
- 备注：疑似。

##### [低] 83-18｜op 21 Address 的 extra 步长与 pointer+offset 溢出未校验
- 位置：`Interpreter.krt:713-716`
- 类别：未检查前置条件
- 触发条件：`extra` 任意（含 0、负、非 2 次幂），或 `pointer + offset` 溢出。
- 后果：地址回绕；与 Kro 的地址运算语义可能不同，且校验层对 op 21 无任何 argument/extra 约束。
- 最小修复：校验 `extra` 非负（最好为 2 次幂），并对加法做溢出检查。
- 备注：疑似。

##### [低] 83-19｜KrtVmEmitV1 未检查 entry 的聚合返回隐藏参数
- 位置：`Interpreter.krt:113-116`
- 类别：契约破坏
- 触发条件：entry 的 `parameter_counts == 0` 但 `aggregate_return_sizes > 0`。
- 后果：v1 产物按 0 参数入口序列化，而模块的 `KrtVmPhysicalParameters(entry) == 1`；v1 运行期与模块契约不一致。
- 最小修复：把入口检查改为 `KrtVmPhysicalParameters(module, module.entry) == 0`，并显式拒绝 `aggregate_return_sizes[entry] > 0`。
- 备注：—

##### [低] 83-20｜校验函数内调用 KrtDriverError，诊断重复且顺序依赖
- 位置：`Interpreter.krt:67`、`Interpreter.krt:76`、`Interpreter.krt:81`、`Interpreter.krt:140`
- 类别：错误处理不一致
- 触发条件：`KrtVmValidateVersion` 返回 false 的任意路径。
- 后果：函数内部先报一条具体错误，调用方再报 "invalid or unsupported VM module"，诊断重复且第一条细节可能被覆盖；纯校验函数带副作用，不利于复用与测试。
- 最小修复：校验函数只返回 bool，统一由调用方按上下文诊断。
- 备注：—

##### [低] 83-21｜KrtVmEnter 的 locals_size == 0 分支不可达
- 位置：`Interpreter.krt:586`
- 类别：死代码
- 触发条件：`locals_size = scalar_bytes + aggregate_size + wide_bytes + 30`，最小为 30。
- 后果：`if (frame.locals_size == 0) { frame.locals_size = 8; }` 永不成立，误导读者以为存在零大小帧。
- 最小修复：删除该分支或改为断言。
- 备注：—

##### [低] 83-22｜KrtVmNumberText 内部分配未释放
- 位置：`Interpreter.krt:254`、`Interpreter.krt:282`（`digits`/`text`；`KrtDriverNumber` 返回的 `significant`/`exponent_text` 亦为池内分配）
- 类别：资源与生命周期
- 触发条件：legacy v1 打印路径每次调用。
- 后果：中间缓冲与结果串不回收，bump pool 持续增长。
- 最小修复：返回前释放中间缓冲（若池支持回收）。
- 备注：仅 v1 路径，进程短生命周期。

##### [低] 83-23｜op 52 异常展开未校验 handler.frame 仍是当前帧祖先
- 位置：`Interpreter.krt:802-807`
- 类别：未检查前置条件
- 触发条件：handler 链被破坏（如未来引入跨帧注册或 op 51 误配）。
- 后果：`while (state.frame != handler.frame)` 可能走到 `state.frame == null`，随后 `state.frame.pc` 空指针解引用。
- 最小修复：循环内加 `if (state.frame == null) { state.failed = true; break; }`。
- 备注：当前 handler/frame 不变量下不可达，属纵深防御。

##### [低] 83-24｜代码/数据段 region 标记为可写
- 位置：`Interpreter.krt:668`（对比 `Interpreter.krt:669-672` 的 runtime immutable）
- 类别：契约破坏
- 触发条件：字节码用 op 23 向 op 25 取得的 data 地址写入。
- 后果：常量/数据段可自修改，与 runtime 段显式 `immutable` 的约定不一致；Kro 侧通常区分 .rodata/.data。
- 最小修复：按段区分可写性，至少把只读常量区标记 immutable。
- 备注：疑似设计意图，需与后端 ABI 确认。

### 第八十四批：SelfHost/Compiler/Driver/Driver.krt（全量）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Driver/Driver.krt`，全量 1346 行（分三段读完：1-340、341-680、681-1020、1021-1346）。
- 覆盖函数/区域：KRO ABI 校验（`KrtDriverAbi*`，1-199）、产物输出（`KrtArtifact*`，201-346）、命令行与入口（`KrtDriverParseArguments`/`KrtDriverMain`，348-518）、模块元数据导入（`KrtModules*`，520-743）、并行编译（745-834）、源加载与项目源收集（836-1346）。
- 对照来源：`ArkLink/src/Core/NativeAbi.c`（ABI 记录语义）、`SelfHost/Compiler/Platform/PlatformAbstraction.krt`（`KrtDriverAlloc`/`KrtDriverError`/`KrtDriverReadFile`/`KrtDriverAbsolutePath` 契约）、`BuildSystem.krt`/`Project.krt`/`CompilerPipeline.krt`/`SemanticFeatures.krt`（符号调用点，仅 grep 特定符号）。
- 未通读其他源文件；跨文件结论均标注「疑似」或给出 grep 行号。


##### [致命] 84-1｜模块接口 original/transformed 别名被就地清空，重复导入误报冲突
- 位置：`SelfHost/Compiler/Driver/Driver.krt:544`（`file.original=data;file.transformed=data;`）、`:537`（比较 `previous.original`）、`:1107-1109`（清空 `transformed`）
- 类别：3 语义分叉 / 10 资源与生命周期
- 触发条件：同一模块接口被第二次 `KrtModulesAppend`（同一 KRO 对象在 objects 链中重复，或两个对象声明同一 module path）。
- 后果：第一次追加时 `KrtSourcesParseDirective` 把 `transformed` 中的 import 字节改成空格，而 `original` 与 `transformed` 同指针，导致 `previous.original` 已不是提供方原始字节；第二次 `:537` 比较不等，直接报 `E_MODULE_METADATA "repeated module declaration interfaces disagree"`，合法重复导入被拒绝。同时 `Project.krt:337` 用 `file.original` 计算项目哈希，元数据哈希随 import 清空漂移，缓存判定不稳定。
- 最小修复：`KrtModulesAppend` 分别复制两份缓冲（`original` 保留提供方字节，`transformed` 单独分配并清空），或比较前用独立保存的 raw 副本。
- 备注：普通源文件路径 `KrtSourcesLoad` 是两份（`:1186` 与 `:1195`），仅元数据路径漏掉；根因是别名。

##### [高] 84-2｜`KrtModulesSameModule` 把不同根源文件判为同一模块
- 位置：`SelfHost/Compiler/Driver/Driver.krt:696-705`（关键 `:704`）
- 类别：3 语义分叉
- 触发条件：两个位置分别来自不同根源文件（`KrtSourcesLoad` 不设置 `module_authority`，默认 0），经 `SemanticFeatures.krt:67` 的 `module_internal` 可见性判断调用。
- 后果：`first_authority==second_authority==0` 直接返回 true，跨文件 internal 可见性被当作同模块放行；只有 imported metadata 才有非 0 authority，根文件语义缺失。
- 最小修复：`KrtSourcesLoad` 对普通源设置 `module_authority=path`（或文件唯一 id），并仅在两边 authority 相等且非 0 时判同模块；两边皆 0 时按同一文件判定。
- 备注：`KrtModulesSameModule` 还被 `Semantic.krt:1538/1543` 用于来源归属，影响面不止可见性。

##### [高] 84-3｜Project 构建路径未设置 `KrtDriverActiveBundle`，导入标记与去重整体失效
- 位置：`SelfHost/Compiler/Driver/Driver.krt:707-711`（active bundle 为 null 直接 return true）、`:642-643`（无 native_imported 直接 return true）；对照 `BuildSystem.krt:60` 设置、`Project.krt:397` 未设置
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：走 `KrtDriverProject` → `KrtSourcesCollectModules`（`Project.krt:397`）的项目构建。
- 后果：`KrtModulesMarkImportedAst` 在 `:708` 直接跳过，所有 `node.native_imported` 保持 false；`KrtModulesMergeImportedAst` 在 `:642-643` 因 `imports==false` 直接返回 true，导入类型不再去重、不再校验模板一致性；`KrtModulesSameModule`/`KrtModulesImportedPosition` 也退化。与单文件构建（BuildSystem 设置 active bundle）行为分叉。`CompilerPipeline.krt:543-544` 的基于 bundle 诊断同样走不到。
- 最小修复：在 `KrtSourcesCollectModules` 内统一设置/恢复 `KrtDriverActiveBundle`，或要求 Project 路径与 BuildSystem 一样先赋值。
- 备注：`grep "ActiveBundle\s*="` 仅命中 BuildSystem.krt:60/80，Project.krt 无赋值。

##### [高] 84-4｜`KrtModulesReadMapped` 缺 relocation 表一致性校验
- 位置：`SelfHost/Compiler/Driver/Driver.krt:556-560`；对照 `KrtDriverAbiRead:150-155`
- 类别：4 阶段不对齐 / 2 诊断缺失
- 触发条件：KRO 头 `relocations` 字段与 32/36/40 处段大小之和不相等。
- 后果：`KrtDriverAbiRead` 会拒绝该布局，`KrtModulesReadMapped` 只检查 `relocations>4194304` 与范围，仍继续定位 `symbols/strings`，同一对象在两个阶段得到不同接受结论，模块元数据可能按错位表解析。
- 最小修复：与 `KrtDriverAbiRead` 一致，计算 `relocation_sum` 并比较 `relocations!=relocation_sum`。
- 备注：该函数也没有 `KrtDriverAbiRead` 的 `count>1048576` 之外的头字段交叉校验。

##### [高] 84-5｜并行 worker 强制 `quiet=false`，quiet 语义在父子进程不一致
- 位置：`SelfHost/Compiler/Driver/Driver.krt:774`（`KrtDriverQuiet=false; task.options.quiet=false;`）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：父进程 `options.quiet=true`（如 `Project.krt:439` 项目单元硬编码 `unit.quiet=true`）或父进程已设 `KrtDriverQuiet`。
- 后果：子进程忽略 quiet，照常向日志/stderr 输出全部诊断；父进程恢复 quiet 后与子进程行为不一致，quiet 构建的输出不可控，项目构建的静默约定被破坏。
- 最小修复：worker 继承父进程 quiet 状态，不要覆盖；若必须强制输出，应在父进程统一收集并按 quiet 过滤。
- 备注：`BuildSystem.krt:34/113` 会把 `options.quiet` 写入全局。

##### [高] 84-6｜`KrtModulesRebuild` 死代码，`KrtModulesImport` 在 changed 且 data==0 时短路返回
- 位置：`SelfHost/Compiler/Driver/Driver.krt:596-608`、`:615`（`return !changed||bundle.data==0||KrtModulesRebuild(bundle);`）
- 类别：5 死代码 / 6 契约破坏
- 触发条件：任何调用 `KrtModulesImport` 的流程；此时 `bundle.data` 初始恒为 0（`KrtSourcesCollectModules:1327` 在 import 之后才分配）。
- 后果：`bundle.data==0` 短路，`KrtModulesRebuild` 永不执行；同时 `changed=true` 时直接返回 true 而不合并元数据，函数契约上依赖调用方稍后重建。若未来 bundle 复用导致 `data!=0`，`:1327` 直接覆盖 `bundle.data` 而不释放旧缓冲，产生泄漏。
- 最小修复：要么 changed 时无条件重建（并正确处理 `data==0`），要么删除 `KrtModulesRebuild` 并在注释中明确由 `KrtSourcesCollectModules:1329-1340` 统一合并。
- 备注：`grep KrtModulesRebuild` 仅 `:596` 定义与 `:615` 调用。

##### [中] 84-7｜命令行选项词/项目命令词与源文件名冲突
- 位置：`SelfHost/Compiler/Driver/Driver.krt:421-429`（`output`/`target`/`help`/`version` 等单词即选项）、`:466-467`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：源文件名恰为 `output`、`target`、`help`、`version`、`build`、`check`、`clean`、`new`、`run-vm` 等。
- 后果：这些词被静默当作选项或项目命令；例如 `KrtProject foo.krt clean` 中 `clean` 变成源文件，`KrtProject output` 被当作待赋值选项而报 `expected a value after option`。
- 最小修复：仅在尚未出现位置参数时识别项目命令；选项词按严格等值/前缀匹配并结合上下文判定，冲突时优先按源文件处理或显式报错。
- 备注：`--` 只关闭选项识别，`:466` 的 `!literal` 已正确排除命令词。

##### [中] 84-8｜`KrtSourcesScan` 对非法 namespace 静默继续
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1134`（`if (!KrtSourcesParseName(...) || name.wildcard) { continue; }`）
- 类别：2 诊断缺失
- 触发条件：`namespace 123;`、`namespace a.*;` 或名称解析失败。
- 后果：不产生任何 `E_PARSE`/`E_IMPORT` 诊断，扫描继续，错误被推迟到后续阶段或完全吞掉，用户只看到泛化失败。
- 最小修复：失败或 wildcard 时返回 `KrtSourcesFail(file, start, "E_PARSE", ...)`。
- 备注：同分支的 wildcard 分支也被静默跳过。

##### [中] 84-9｜`KrtSourcesVisit` 未检查 `KrtDriverAbsolutePath` 返回值，失败静默 false
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1286-1288`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`KrtDriverAbsolutePath` 返回 0（分配失败或符号链接解析超过 40 层，见 `PlatformAbstraction.krt:240-268`）。
- 后果：`KrtSourcesFindFile(bundle,0)` 返回 null，直接 `return false`，无 `E_MEMORY`/`E_INPUT` 诊断，上层只能报泛化错误。
- 最小修复：`normalized==0` 时 `KrtSourcesFail(file, directive.position, "E_MEMORY", ...)` 后返回 false。
- 备注：同类未检查返回值的还有 `KrtSourcesAtDirectory` 中 `KrtSourcesAppendExtension` 的 0 值（`:1249`）。

##### [中] 84-10｜`KrtSourcesLoad` 失败路径泄漏输入缓冲与 file 对象
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1185`、`:1190`、`:1193`、`:1196`
- 类别：10 资源与生命周期
- 触发条件：源文件读取失败、累计超过 16 MiB、非法 UTF-8、`transformed` 分配失败。
- 后果：`input.data` 与 `file` 未释放即返回 null；叠加 `KrtDriverReadFile` 自身在容量超过 32 MiB 或分配失败路径也不释放已分配缓冲（`PlatformAbstraction.krt:151-153`），单次失败即可泄漏数十 MiB。
- 最小修复：各失败分支 `delete input.data; delete file;`；并修正 `KrtDriverReadFile` 的失败释放。
- 备注：疑似（取决于 bundle 清理契约是否兜底，但当前函数返回 null 后无人持有该指针）。

##### [中] 84-11｜`KrtArtifactAssembly` 缺符号表/重定位字节边界校验
- 位置：`SelfHost/Compiler/Driver/Driver.krt:305`、`:316`、`:323`、`:328`
- 类别：8 未检查前置条件
- 触发条件：`KrtKroObject.symbol_bytes`/`text_relocation_bytes` 为 null 或长度不足以覆盖 `symbol_count*32`、`text_reloc_count*16`。
- 后果：`KrtArtifactWord` 直接按 `symbol_index*32+16`、`relocation*16` 读取，越界；`text_reloc_count>0` 但 `text_relocation_bytes==null` 时直接崩溃。`:316` 甚至未先确认 `symbol_bytes` 覆盖 `function_index*32+20`。
- 最小修复：入口校验 `symbol_bytes!=null && symbol_count*32<=symbol_bytes.length`、`text_relocation_bytes!=null && text_reloc_count*16<=...`。
- 备注：疑似（构造方可能已校验，但本函数无防御）。

##### [中] 84-12｜`KrtArtifactWriteIr` 对 module 数组无 NULL/长度校验
- 位置：`SelfHost/Compiler/Driver/Driver.krt:247-259`
- 类别：8 未检查前置条件
- 触发条件：`module.function_count>0` 而 `parameter_counts`/`local_counts`/`starts`/`ops`/`args`/`extra` 为 null 或长度不足。
- 后果：`:249-259` 直接索引，越界崩溃；`module.ssa==null` 且 module 不完整时无任何诊断。
- 最小修复：入口校验 module 与各数组非空、长度覆盖 `function_count`/`count`。
- 备注：疑似（取决于 `KrtNativeModule` 构造契约）。

##### [中] 84-13｜`KrtDriverParallelCompile` 用 `waitpid(-1)` 可能回收外部子进程
- 位置：`SelfHost/Compiler/Driver/Driver.krt:808-812`
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：父进程存在不属于本任务链的子进程（其他 fork 来源）。
- 后果：`waitpid(-1)` 回收未知 pid 后，按 pid 找不到 task，报 `E_WORKER "unexpected compilation worker state"` 并置 `ok=false`，造成误报失败；该分支 `continue` 不减少 `active`。
- 最小修复：仅接受 task 链中 pid 集合内的回收结果，或对未知 pid 记录后继续等待。
- 备注：疑似；需确认 `KrtDriverCompile` 是否会在父进程 fork 其他子进程。

##### [中] 84-14｜全局 `KrtDriverArtifactTarget`/`KrtDriverLastVm` 跨阶段共享可变状态
- 位置：`SelfHost/Compiler/Driver/Driver.krt:203-204`；读写点 `CompilerPipeline.krt:514-527`、`BuildSystem.krt:36-108`
- 类别：7 共享可变状态
- 触发条件：同一进程内多次/嵌套编译，或编译中再次触发产物选择。
- 后果：后一次编译覆盖全局 target 与上一次的 VM 缓冲；`BuildSystem` 手动保存/恢复，但 `CompilerPipeline` 只读不恢复，状态可漂移。
- 最小修复：把 target/VM 产物放入 `KrtDriverOptions` 或编译上下文参数传递。
- 备注：并行 fork 后子进程地址空间独立，单进程内才是隐患。

##### [中] 84-15｜全局 `KrtDriverLastErrorCode` 成功后残留旧值
- 位置：`SelfHost/Compiler/Driver/Driver.krt:748`；赋值点 `PlatformAbstraction.krt:135`（`KrtDriverError` 无条件覆盖）
- 类别：7 共享可变状态 / 9 错误处理不一致
- 触发条件：一次编译失败后，后续成功编译未产生新错误。
- 后果：全局保留旧错误码；worker 在 `:774` 重置，父进程非并行路径不重置，任何依据该全局做延迟失败分类的调用方可能误判。
- 最小修复：编译入口统一重置，或改为返回值/上下文传递错误码。
- 备注：疑似（取决于调用方是否依赖该全局）。

##### [中] 84-16｜`options.quiet` 无 CLI 入口，配置漂移
- 位置：`SelfHost/Compiler/Driver/Driver.krt:366`（字段声明）、`:401-409`（解析未设置）
- 类别：11 配置漂移 / 5 死代码
- 触发条件：用户尝试 `--quiet`；或审查者认为该字段可从命令行配置。
- 后果：`KrtDriverParseArguments` 从不设置 `options.quiet`，`KrtDriverUsage` 也不列出；实际只有 `Project.krt:439` 硬编码 `unit.quiet=true` 与 `BuildSystem.krt:34/113` 读取，CLI 配置面与生效路径不一致，且与 84-5 的 worker 覆盖叠加。
- 最小修复：增加 `--quiet` 解析，或删除该字段并统一 quiet 来源。
- 备注：`grep "\.quiet"` 仅命中 BuildSystem/Project/Driver 三处。

##### [中] 84-17｜`KrtSourcesFail` 在 quiet 下静默吞掉全部源级诊断
- 位置：`SelfHost/Compiler/Driver/Driver.krt:928`（`if (KrtDriverQuiet) { return false; }`）
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`KrtDriverQuiet=true` 时任何源级失败，包括 `E_INPUT`（源文件不存在）、`E_SIZE`、`E_ENCODING`。
- 后果：调用方只拿到 false，用户看不到失败原因；与 worker 强制 quiet=false（84-5）叠加后，quiet 行为在源级与 worker 级完全相反。
- 最小修复：quiet 只抑制冗余输出，保留首条错误或写日志；至少让调用方在 quiet 下也能获得错误码。
- 备注：`KrtSourcesFail` 被 `KrtSourcesLoad`/`KrtSourcesValidate` 等广泛使用。

##### [低] 84-18｜`KrtArtifactOp` 未覆盖 47/48/49/55-59/64/65，统一输出 `unknown`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:210-240`（默认分支 `:239`）
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：模块 IR 含这些 opcode。
- 后果：IR 文本静默失真为 `unknown`，不报错，排障困难。
- 最小修复：补全操作码名，或对未定义操作码显式报错。
- 备注：疑似（若这些编号是保留未用则无实际影响）。

##### [低] 84-19｜无参数调用静默探测 `program.krt` 并写死 `stage1-probe.kro`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:495-497`
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：`arguments.next==null` 且当前目录存在 `program.krt`。
- 后果：无参数调用的行为依赖目录内容，输出路径硬编码 `stage1-probe.kro`，用户不可控；否则打印 usage 返回 0。
- 最小修复：改为显式 probe 子命令/选项，或至少打印将写入的路径。
- 备注：注释 `:494` 已说明是自举探针约定，属有意行为但缺诊断。

##### [低] 84-20｜诊断定位依赖 `file.start` 升序，而 `KrtModulesRebuild` 不设置 `start`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:946-948`；对照 `KrtModulesRebuild:596-608`（未设 `start`）与最终构建 `:1335`（设置 `start`）
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：若 `KrtModulesRebuild` 被激活（当前因 84-6 为死代码而不可达）。
- 后果：`KrtSourcesDiagnosticDetails` 按 `file.start<=position` 选文件，若 `start` 全为 0 会选错文件，诊断位置错位。
- 最小修复：统一由最终构建设置 `start`，或让 rebuild 也设置 `start`。
- 备注：与 84-6 同源；当前流程不可达。

##### [低] 84-21｜worker 子进程分支 `return false` 不可达
- 位置：`SelfHost/Compiler/Driver/Driver.krt:802`（`KrtDriverCompileWorker(pending); syscall(60,2,...); return false;`）
- 类别：5 死代码
- 触发条件：`fork` 返回 0（子进程）。
- 后果：`KrtDriverCompileWorker` 所有路径均以 `syscall(60,...)` 退出（`:771/772/778`），`return false` 永不执行；若将来 worker 改成 return，控制流语义会出错。
- 最小修复：删除 `return false`，或让 worker 返回状态码后由子进程统一退出。
- 备注：仅死代码，不影响当前正确性。

##### [低] 84-22｜`KrtSourcesCollect` 包装函数无调用方且固定 `objects=null`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1344-1346`
- 类别：5 死代码 / 6 契约破坏
- 触发条件：—
- 后果：`grep "KrtSourcesCollect\("` 仅命中定义处；该包装固定 `objects=null`，一旦被启用会经 `KrtModulesImport:611` 直接返回 true，跳过 ABI 契约检查。
- 最小修复：删除该包装，或接入调用并传递真实 objects。
- 备注：与 `KrtSourcesCollectModules` 功能重复。

##### [低] 84-23｜`KrtArtifactIr`/`KrtArtifactAssembly` 打开输出失败无诊断
- 位置：`SelfHost/Compiler/Driver/Driver.krt:270`、`:296`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：输出路径不可创建（权限/目录不存在）。
- 后果：直接返回 false，不产生 `E_OUTPUT` 诊断；是否补打印完全依赖调用方。
- 最小修复：失败时 `KrtDriverError("E_OUTPUT", ...)`。
- 备注：疑似（调用方 `KrtDriverCompile` 可能统一报错）。

##### [低] 84-24｜`KrtDriverAbiReadBinary` 对 mmap 返回的非常规负值未拒绝
- 位置：`SelfHost/Compiler/Driver/Driver.krt:47`（`if(data<0&&data>=-4095){return false;}`）
- 类别：8 未检查前置条件
- 触发条件：`mmap` 返回 -4095 之外的负值。
- 后果：该返回值被当作有效指针写入 `buffer.data`，后续按对象解析崩溃。
- 最小修复：改为 `if (data < 0) { return false; }`。
- 备注：Linux errno 通常落在 [-4095,-1]，实际风险低。

### 第八十五批：SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt（全量）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt`，共 1072 行，已用 read 按 1-200 / 201-420 / 421-640 / 641-860 / 861-1072 五段全量读完。
- 对照范围：仅按符号 grep 核对调用点与消费方（`KrtNativeRuntimeRead`、`KrtModuleTypeSignature`、`KrtNativeTypeMemberFirst/Next`、`KrtNativeResolvePrototypes`、`KrtNativePrepareExternals`、`KrtNativeSnapshotTypeMembers`、`KrtNativeRuntimeRegister(Key)`、`signature.nominal`、`runtime_offsets` 等），未通读其他源文件。
- Re.KrtC 侧核对：在 `Re.KrtC/` 全目录 grep `KRTTYPE3|KRTABI|KRTWIDE3|_KRT1$|KRTMETHOD3|receiver_adjust|runtime_bucket` 均无命中，C 版没有与本文件对应的 ABI2 语义运行时实现，故涉及“与 C 版是否一致”的判断一律标「疑似」。
- 全局符号扫描结论：`KrtNativeRuntimeRead` 在 `KroBackend.krt:2520` 有调用（非死代码，且越界问题已在第 18 批 75-2 记录，本批不重复）；`KrtModuleTypeHash` 全仓仅有定义无调用。


##### [高] 85-1｜泛型方法契约编码失败被静默吞掉，损坏字节仍写入类型清单
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:722`（配套 671-690、576-581）
- 类别：1 静默失败；9 错误处理不一致
- 触发条件：`method.native_generic_parameters != null` 时调用 `KrtModuleGenericMethod`；约束位置查不到（683-684）或方法体编码失败（690）使返回的 `KrtAbiBytes.ok=false`。
- 后果：722 行把返回值直接 `KrtModuleAbiMemberInsert(methods, ...)`，从不检查 `.ok`；`KrtModuleAbiMembers`（576-581）也只读 `count/data`。最终 `layout` 内含被截断的成员契约，而 `signature.supported / contract.ok / nominal.ok` 全部保持 true。`KroBackend.krt:311-318` 的三重校验因此通过，坏 manifest 被写出，问题推迟到链接期甚至运行期才暴露。
- 最小修复：722 行检查返回值，`ok=false` 时置 `bytes.ok=false; signature.supported=false; return;`；并让 `KrtModuleAbiMembers` 拒绝 `item.contract.ok==false` 的节点。
- 备注：同一函数内 729/735 行的同类失败已正确传播 `signature.supported=false`，仅 722 遗漏，属明确实现疏漏而非设计取舍。

##### [高] 85-2｜Legacy 命名把 object 类型压成单个字节，丢失类型身份并与 ABI2 编码分叉
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:257-258`（对照 110-111、263）
- 类别：3 语义分叉；6 契约破坏
- 触发条件：`KrtNativeFunctionLinkName`（326-330）在 ABI2 不可用（`supported=false` 或 `uses_struct=false`）时回落到 `KrtNativeFunctionLinkNameLegacy`；参数或返回值的 `native_type.value==5`（object，见 438 行 `"object"`）。
- 后果：legacy 分支对任何 object 只写一个 `79('O')`，不带限定名/身份；ABI2 的 `KrtAbiShape` 则写 `78('N')+identity`（含声明名）。两个仅靠 object 类型区分的重载会得到完全相同的 `_KRT1$...` 签名，`KrtNativeResolvePrototypes`（353-364）据此误判为同一函数并删除其一；导出符号亦可能撞名。
- 最小修复：legacy 的 object 分支改为输出与 263 行一致的 `67('C')+KrtLinkDeclaration`（或带身份字节），至少保证不同 object 类型签名可区分。
- 备注：疑似（Re.KrtC 无对照实现，无法确认是否为刻意保留的历史拼写；但 258 行没有任何注释说明该压缩是有意为之）。

##### [中] 85-3｜成员契约 ok 状态在汇聚进 layout 时不检查
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:724-738`、`746-749`（配套 576-581）
- 类别：1 静默失败；9 错误处理不一致
- 触发条件：`KrtModuleMethodIdentity` 置 `identity.ok=false`（如 548/556 签名类型解析失败）、`KrtAbiAppend` 因无效输入置 `contract.ok=false`、或静态字段形状编码失败。
- 后果：724-725、738、748-749 均不检查 `identity.ok/contract.ok`，损坏契约照常插入 `methods/statics` 链，随后被 `KrtModuleAbiMembers` 原样写进 `layout`；`signature` 各标志仍为 true，`KroBackend.krt:311` 校验通过。
- 最小修复：插入前检查 `identity.ok && contract.ok`，失败即 `bytes.ok=false; signature.supported=false; return;`。
- 备注：与 85-1 同属“成员契约失败被吞”，但发生在普通方法/静态字段路径，位置与触发点独立，故单列。

##### [中] 85-4｜宽物理边界探测的深度上限静默返回 false，与 Shape/LinkType 的报错策略相反
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:68-73`、`75-79`、`81-85`
- 类别：8 未检查前置条件；1 静默失败
- 触发条件：数组/指针/固定长度/回调形状嵌套深度超过 64（`depth>64`）。
- 后果：三个 `KrtAbiWide*` 函数一律 `return false`，调用方 137/142/550/557 据此不标记 `KRTWIDE3`，契约按 ABI2 生成；而同样的深度超限在 `KrtAbiShape:92` 是 `bytes.ok=false; signature.supported=false`、在 `KrtLinkType:224` 是 `name.valid=false`。三处对同一边界一个静默撒谎、两处显式失败，策略互相矛盾，极端嵌套下会出现契约与代码生成物理约定不一致。
- 最小修复：深度超限时向调用方回传失败（改三态返回或提供 `KrtAbiWideQuery` 输出错误），与 `KrtAbiShape` 对齐。
- 备注：64 层实际极难触发，故定中；矛盾本身是确定的。

##### [中] 85-5｜开放泛型签名丢失 stride 与 pointee 空值位图，与完整 ABI 形状分叉
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:601-624`（对照 93、100、229）
- 类别：3 语义分叉；13 注释与实现脱节
- 触发条件：构造 `KRTGENERICMETHOD3`/泛型实例的“开放签名”时（601 注释自称描述类型构造器与参数位置）。
- 后果：指针/stride 统一编码为 `80('P')` 且不带 `KrtNativeSemanticStride`（对照 100 行完整编码 stride）；可空性只写顶层 `81`（603），完全不写 `native_pointee_nullable` 位图（对照 93、229）。仅 stride 或内层可空性不同的实例在该编码下字节相同，若消费方以此做实例去重/缓存键会错误合并；注释声称的完整描述与实现不符。
- 最小修复：在 606-608 补 stride 字，在 603 附近补 pointee 位图；或在注释中明确这是有意的弱化键并禁止用作实例唯一键。
- 备注：疑似（可能是有意简化的“开放签名”，但文件中无任何说明，且同名函数在 93/100/229 是完整编码）。

##### [中] 85-6｜成员快照缓存无失效机制，后续挂接的方法不进 ABI 清单
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:405-428`
- 类别：7 共享可变状态；4 阶段不对齐
- 触发条件：`KrtNativeSnapshotTypeMembers`（调用点 `Types.krt:940`）执行后，后续语义阶段继续向 `module.left`/类型挂接方法（泛型特化、接口补全等）。
- 后果：`type.native_members_saved` 永久为 true，`KrtNativeTypeMemberFirst/Next`（422-428）此后只走 `native_member_first/native_member_next` 链；新挂接方法不在该链上，导致 `KrtModuleAbiLayoutPrefix`（720-741）、`KrtNativeRuntimePrepareType`（945-962）及 `Types.krt` 多处枚举集体缺项，虚表/接口表/ABI 方法表静默少项。文件中只有 `KrtNativeFunctionIndexInvalidate`（374 调用）这类针对函数索引的失效，成员快照没有对应失效入口。
- 最小修复：新增 `KrtNativeTypeMembersInvalidate(module)` 在所有 `native_members_saved=true` 的类型上复位，并在重新挂接成员后调用；或改为每次按需重建。
- 备注：疑似——需确认 `Types.krt:940` 之后确实存在追加方法的阶段；机制上的脆弱性是确定的。

##### [中] 85-7｜`record.prepared` 先置位后校验，失败留下粘滞“已完成”状态
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:932-942`（失败返回点 936、939、949、960）
- 类别：7 共享可变状态；9 错误处理不一致
- 触发条件：`KrtNativeRuntimePrepareType` 中基类/接口注册失败（936、939）或虚/接口方法添加失败（949、960）。
- 后果：函数入口即 `record.prepared=true`，失败路径不回滚；本次 build 虽在 1005 行中止，但若同一 module 触发第二次 build/修复后重试，该 record 会被 933 行直接当作已准备，方法表缺项被永久忽略（静默）。
- 最小修复：仅在成功路径末尾置 `prepared=true`，或失败时复位并回滚已插入的 `record.methods`。
- 备注：与 85-8 叠加时记录状态半完成，建议一并处理。

##### [中] 85-8｜`KrtNativeRuntimeMethodAdd` 先把节点挂链再校验外部索引，失败不回滚
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:911-928`
- 类别：7 共享可变状态；9 错误处理不一致
- 触发条件：`target.value < 0` 且 `-1-target.value >= module.external_count`（926）。
- 后果：`inserted` 已在 921-923 挂入 `record.methods`（并持有 `identity`），此时才 `return false`；调用方 949/960 直接向上失败，节点与 identity 字节留在记录里，既污染共享状态又泄漏。
- 最小修复：把 external 范围校验提到插入之前；失败路径删除 `inserted.identity` 并摘链。
- 备注：正常路径不会触发，但失败路径状态不一致是确定的。

##### [中] 85-9｜运行时 blob 构建链全程 bool false，无错误码与定位，与同文件其他路径标准不一
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:932-966`、`993-1071`（上游短路 `Middle/Ir/Ir.krt:2356`）
- 类别：2 诊断缺失
- 触发条件：任一类型准备、key intern、重定位、reserve 失败。
- 后果：`KrtNativeRuntimeBuild` 只返回 false，不写 `error_start`/`native_error`；`Ir.krt:2356` 用 `output.errors == 0 && KrtNativeRuntimeBuild(output)` 直接短路，最终只剩通用失败。对比同文件 `KrtNativePrepareExternals`（385、388、390 设置 `output.error_start`、`module.native_error=13`）与 `CompilerPipeline.krt:480` 的 `E_LINKAGE` 诊断，标准明显不统一，用户无法定位是哪个类型/方法导致运行库构建失败。
- 最小修复：为 `KrtNativeModule` 增加错误位置/错误码字段并在各失败点填充，接入 `KrtDiagnostic`；至少区分“类型身份非法”“虚表冲突”“外部索引越界”。
- 备注：本文件多数失败是裸 false，此条以 runtime 构建链为代表，不与其他静默失败条目合并。

##### [中] 85-10｜ABI 签名对象图每次重建且从不释放
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:116-180`（调用链 326-330、353-363、389；另见 `KroBackend.krt:1962`）
- 类别：10 资源与生命周期
- 触发条件：任何需要函数链接名的路径；`KrtNativeResolvePrototypes` 对每个候选做 O(n²) 次 `KrtNativeFunctionLinkName`，`KrtNativePrepareExternals` 对每个外部候选调 `KrtNativeAbiSignatureSupported`（189-191，内部再建整张签名）。
- 后果：`KrtAbiFunction` 分配的 `KrtAbiSignature`、`types` 链表、每个 `KrtAbiType` 的 `identity/layout` 字节数组、以及 126 行临时 `shape` AST 节点，在返回 `KrtLinkName` 后全部不可达且无 delete。全仓未发现 `KrtAbiSignature` 的释放函数（`CompilerPipeline.krt:289-290` 释放的是另一结构）。大模块编译内存随候选数平方级增长。
- 最小修复：新增 `KrtAbiSignatureDelete`（递归释放 types→layout/identity→data、nominal/contract、临时 shape），在所有出口调用；或为常用声明缓存签名。
- 备注：疑似 allocator 是否统一回收无法从本文件确认；但文件内大量显式 `delete`（如 37、43、878、893）表明是手动管理，泄漏成立。

##### [中] 85-11｜`KrtAbiCompare` 不校验 `ok`/长度有效性，可把无效身份与既有类型合并
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:46`（关键调用 62-63；另 571、834、917）
- 类别：8 未检查前置条件；1 静默失败
- 触发条件：`KrtAbiRegister` 在 62 行拿到 `ok=false` 的身份（`KrtModuleTypeIdentity` 对 authority 长度为 0 或 >4096 的情况在 446-447 直接置 `identity.ok=false`；`KrtAbiTypeIdentity` 对 name 失效也无检查），随后 63 行仍用空/残缺 data 做排序比较。
- 后果：两个不同非导出类型在 authority 缺失时都得到空的无效身份，比较返回 0，第二个直接复用第一个的 `KrtAbiType`（63 行 `return item`），类型身份被静默错并，后续 layout/契约定到错误声明上。
- 最小修复：`KrtAbiRegister` 在 62 行后检查 `identity.ok`，无效即 `signature.supported=false; return null;`；`KrtAbiCompare` 内部对 `!left.ok||!right.ok` 返回可区分哨兵。
- 备注：疑似——authority 为 0/超长的实际可达性需结合 Driver 模块元数据确认；比较函数缺校验这一缺陷本身是确定的。

##### [中] 85-12｜运行时记录头 96 字节与 receiver 调整 16 为跨文件硬编码契约
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:1014`（`Reserve(bytes,96,16)`）、`922`（`record.kind == 2 ? 16 : 0`）
- 类别：6 契约破坏；11 配置漂移
- 触发条件：任何改动运行时记录头大小/字段偏移，或对象头部布局调整。
- 后果：96 同时被 `Interpreter.krt:1271`（`length < count*96`）、`KroBackend.krt:2516`（symbol size 96）硬编码引用；receiver 调整 16 也被运行期消费方隐含依赖。本文件没有常量或构建期输出，三处漂移时编译期不报错，只在链接/运行期错位。
- 最小修复：定义单一常量（如 `KRT_RUNTIME_RECORD_SIZE`）并由构建器输出 header 布局版本；接收方从构建产物读取而非写字面量。
- 备注：跨文件证据来自定点 grep（`runtime_offsets|interfaces_offset|\.key_offset`），未通读这些文件。

##### [低] 85-13｜void pointer 在 ABI1/ABI2 使用不同标签且无版本说明
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:256`（写 118 `'v'`）对照 101（写 86 `'V'`）；相关 252（readonly 回调结果经 `KrtLinkType` 也落到 118）
- 类别：3 语义分叉；13 注释与实现脱节
- 触发条件：void 指针出现在函数签名/回调结果中，分别经 legacy 与 ABI2 编码。
- 后果：同一 void 指针在两套规范下字节不同（`v` vs `V`），且 118 在 legacy 里同时承担“void 指针”和“readonly 结果转发”两种语义；256 行注释只说“Leave every historical nonnullable ABI1 spelling byte-identical”，未说明这是有意的双标签。
- 最小修复：统一标签或在注释/文档中明确两套编码的对应表。
- 备注：低（主流路径走 ABI2，legacy 标签差异主要影响回落路径）。

##### [低] 85-14｜`KrtNativeRuntimeReserve` 默认 alignment 为 2 的幂，且被传入 storage 尺寸
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:968-973`（调用 1025-1026、1037、1050）
- 类别：8 未检查前置条件
- 触发条件：非内联记录的 `alignment = storage`（1025），而 `storage` 取 `KrtNativeBoxStorage`；若得到 12 等非 2 幂值（≤16 时不被 1026 行强制成 8）。
- 后果：970 行 `(bytes.count & (alignment-1))` 对非 2 幂失去对齐意义，静默按错误边界填充；记录/接口表偏移与消费方假设不符时会读到错位字段。
- 最小修复：reserve 前显式校验 `alignment` 为 2 的幂且 ≤16，否则 `bytes.ok=false; return -1`；不要用 storage 充当 alignment。
- 备注：疑似——kind==1 在 1026 被强制为 8，其余 kind 的 BoxStorage/ValueAlignment 可能总是 2 幂，实际触发概率低。

##### [低] 85-15｜`KrtModuleTypeHash` 为死代码
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:482-484`
- 类别：5 死代码
- 触发条件：不适用（全仓 grep `KrtModuleTypeHash` 仅命中定义处，无调用）。
- 后果：无用函数增加维护面；其语义（对 identity 做 FNV 哈希）已被 `KrtAbiHash(KrtModuleTypeIdentity(...))` 的内联用法取代。
- 最小修复：删除，或接入实际调用点并补充测试。
- 备注：如为对外 API 预留，应加注释说明。

##### [低] 85-16｜`KrtLinkNumber` 对 INT32_MIN 溢出且无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:213-217`
- 类别：8 未检查前置条件；2 诊断缺失
- 触发条件：`value == -2147483648` 时 `value = -value` 仍为负（int32 溢出），随后 `value/divisor % 10` 得到负数位，`KrtLinkByte` 以 `value<0` 置 `valid=false`。
- 后果：静默产生无效链接名，错误延迟到调用方检查 `valid` 时才暴露，且无专用诊断。
- 最小修复：对 `value == INT32_MIN` 单独处理（或改用无符号转换/字符串转换），失败时给出明确诊断。
- 备注：低——当前调用点（fixed_length、float 宽度、bits）均非负，INT32_MIN 实际不可达，属防御性缺陷。

##### [低] 85-17｜`KrtNativeResolvePrototypes` 摘链后不释放节点，且 child_count 与链表状态耦合
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:364-370`
- 类别：10 资源与生命周期；7 共享可变状态
- 触发条件：找到同签名原型/定义后 `remove=true`，执行 `module.left = next` 或 `previous.next = next`。
- 后果：被移除的 `candidate` 节点既不 delete 也不再由任何链引用（仅局部 `next`），成为不可达堆内存；`module.child_count` 递减但节点内数据仍在，若后续逻辑按 child_count 反推链表规模易错位。
- 最小修复：摘链后 `delete candidate`（确认无外部引用），或交由统一 AST arena 回收并注释所有权。
- 备注：单次编译内泄漏量小，故低。

##### [低] 85-18｜临时 AST 节点清理不一致
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:305、318、547、555、728、734、126`（对照有 delete 的 657、668）
- 类别：10 资源与生命周期；9 错误处理不一致
- 触发条件：这些位置 `KrtAstMake` 生成临时 Type/Function 节点供签名编码使用。
- 后果：同文件对同类临时节点有时 `delete`（657、668）、有时不 delete，标准不一；未 delete 的节点在 `KrtNativeFunctionLinkNameLegacy` 的每次 O(n²) 调用中反复产生。
- 最小修复：统一所有权规则（谁创建谁释放或统一 arena），并在函数出口集中释放。
- 备注：疑似——若 AST 由全局 arena 统一回收则影响有限，需结合内存管理约定确认。

### 第八十六批：SelfHost/Compiler/Driver/Project.krt（全量）

#### 覆盖范围
- 目标文件：`SelfHost/Compiler/Driver/Project.krt`，全量 1–997 行，分 4 段读完（1–250、251–500、501–750、751–997）。
- 覆盖模块：项目发现与加载（`KrtProjectFind`/`KrtProjectLoad`/`KrtProjectResolveLibraries`）、原生 glob（`KrtProjectGlobWalk`/`KrtProjectPatternMatch`/`KrtProjectSpecs`）、成功构建缓存（`KrtProjectCache`/`KrtProjectFileHash`/`KrtProjectReadManifest`/`KrtProjectWriteManifest`/`KrtProjectRestore`/`KrtProjectBuildKey`/`KrtProjectBuild`/`KrtProjectClean`）、`project new/info`、项目配置语言（`KrtProjectParse`/`Configure`/`Json`/`Legacy`/`Expression`/`Quoted`/`Expand`/`Set`）。
- 对照方式：只对 `Re.KrtC/src/Compiler/Driver/ProjectKrt.c`、`Project.c` 及 SelfHost 的 `PlatformAbstraction.krt`、`Driver.krt`、`Build/BuildSystem.krt` 做定点 grep（符号级），未整文件通读其他源文件。C 版 `Project.c`/`ProjectKrt.c` 无 manifest/缓存实现，涉及缓存与旧式格式的可比性结论均标「疑似」。


##### [高] 86-1｜KrtProjectFileHash 把「读取失败/分配失败」折叠为哈希 0，污染缓存键与恢复校验
- 位置：`SelfHost/Compiler/Driver/Project.krt:48-60`（关键 51、56），使用点 `338-340`、`355`、`358`、`360`、`362`、`460`、`468`、`473`、`482`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：文件存在但 `open/read` 失败、或 `KrtDriverAlloc(8192)` 返回 0；另外 55 行对 `-4`(EINTR) 无限 `continue`，无重试上限。
- 后果：函数统一返回 0，与「文件不存在」不可区分。`KrtProjectBuildKey` 在 338-340 对 `.kro` 对象、编译器、链接器直接调用它且不检查文件是否可读；`460/468/473/482` 把 0 写入 manifest 的 `hash`/`artifact_hash`。恢复时 `358/362` 先 `FileExists` 再比对哈希，若缓存文件随后不可读则 `FileHash` 又返回 0，与存储的 0 相等，校验通过并把损坏/截断的缓存对象当有效产物发布；也可能与旧 key 相撞走 `up-to-date`。
- 最小修复：改为 `(uint64 hash, bool ok)`；失败时 `BuildKey` 强制失效缓存并报 `E_OUTPUT`，`Restore`/`Clean` 把哈希失败视为「不匹配」而非 0；EINTR 重试加次数上限。
- 备注：51 行已检查分配失败，但静默返回 0 仍是失败吞掉；`KrtDriverAlloc` 在别处（如 `Driver.krt:527`）按可失败处理。

##### [高] 86-2｜KrtProjectRestore 静默覆盖已存在的输出与对象文件
- 位置：`SelfHost/Compiler/Driver/Project.krt:360-364`
- 类别：6 契约破坏 / 1 静默失败
- 触发条件：`options.output` 或某个 `object.output` 已存在且哈希与 manifest 记录不同（用户手改、其他工具生成、上一次构建被中断后残留）。
- 后果：不提示、不备份，直接用缓存内容覆盖；若该路径是用户文件则数据丢失。`360` 与 `362` 两处同样行为。
- 最小修复：仅在目标不存在时发布；或覆盖前打印 `restored:`/保留 `.bak`，并让用户可用 `--rebuild` 显式选择。
- 备注：与 `481` 的构建期发布语义一致，但恢复路径没有任何确认或日志区分「新建」与「覆盖」。

##### [高] 86-3｜compile_together 模式下单文件对象编译失败被 defer_failures 吞掉，构建仍返回 0
- 位置：`SelfHost/Compiler/Driver/Project.krt:442`（`task.defer_failures = project.compile_together`）、`456-464`（463 打印 `object deferred`）、`447`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：`CompileTogether(true)` 项目中某个源文件无法单独编译（依赖合并上下文），且错误码被 `Driver.krt:762 KrtDriverDeferredCode` 认定为可延迟。
- 后果：`KrtDriverParallelCompile` 对这类任务返回成功，`458` 的 `task.status == 0` 跳过该对象，manifest 不写对应记录，但 `484` 仍打印 `built:` 并返回 0。缓存与 `Clean` 都看不到这些缺失对象，构建产物「成功」但不完整。
- 最小修复：区分「允许 defer 且合并产物已覆盖」与「必须成功」；至少把被 defer 的单元写入 manifest 的 pending 状态，或在不使用 `check` 的构建中把缺失对象计入失败。
- 备注：463 的文案 `requires the combined project context` 与 `compile_together` 已提供合并上下文相矛盾，疑似设计意图但未在注释中说明。

##### [高] 86-4｜发布顺序导致失败时输出已替换、缓存清单未更新，且无回滚
- 位置：`SelfHost/Compiler/Driver/Project.krt:481-483`（同类：`459`）
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`481` 的第二次发布（`cache/artifact`）失败，或 `483` 的 `KrtProjectWriteManifest` 失败（磁盘满、权限、I/O 错误）。
- 后果：用户可见的 `output` 已被新产物替换，但 `.krtcache/manifest.krtcache` 仍是旧 key/旧 artifact；临时目录被删除且无回滚。下一次构建可能用旧 manifest 走 `up-to-date`，或 manifest 与实际产物不一致。`459` 同样是先发布 `task.output` 再发布缓存，失败时对象输出已更新而缓存未更新。
- 最小修复：先写 `manifest.pending` 并最后原子发布，或失败时恢复/删除已发布的 `output` 与对象输出。
- 备注：`483` 有 `E_OUTPUT` 报错，但状态已不一致；`459` 的失败由 `KrtProjectPublish` 报错。

##### [中] 86-5｜glob 目录嵌套超限的错误被二次误报为「source pattern matched no files」
- 位置：`SelfHost/Compiler/Driver/Project.krt:99`（报错并 `ok=false`）、`133`（`return null`）、`243-249`
- 类别：2 诊断缺失/误导
- 触发条件：模式递归深度超过 128。
- 后果：`KrtProjectGlobWalk` 已报 `E_INPUT source pattern directory nesting limit exceeded`，但 `KrtProjectGlobPaths` 用 `null` 同时表达「出错」和「无匹配」；`243-249` 再拆 parts 重试并追加第二条 `E_INPUT source pattern matched no files`，把真实原因掩盖，用户看到两条矛盾诊断。
- 最小修复：`KrtProjectGlobPaths` 返回 `(paths, ok)` 或错误码，出错时直接向上返回，不再进入「无匹配」分支。
- 备注：`133` 的 `if (glob.ok) return glob.paths; return null;` 是根因。

##### [中] 86-6｜`**` 分支跳过所有隐藏文件/目录，与 C 版及非 magic 分支语义不一致
- 位置：`SelfHost/Compiler/Driver/Project.krt:107-111`
- 类别：3 语义分叉
- 触发条件：模式含 `**`，目录树中存在 `.foo.krt` 或 `.dir/x.krt`。
- 后果：SelfHost 直接跳过全部以 `.` 开头的条目；C 版 `ProjectKrt.c:317` 的 `krt_glob_append` 只跳过 `"."` 和 `".."`。同一项目在两版驱动下收集到的源文件集合不同。另：非 magic 分支 `121` 走精确路径，反而能命中隐藏文件，文件内部也不一致。
- 最小修复：只跳过 `"."`/`".."`，或把隐藏规则写入注释并同步 C 版。
- 备注：疑似；C 版对 `**` 的递归实现不同，无法逐字对齐，但「跳过全部点文件」在 C 版无对应逻辑。

##### [中] 86-7｜KrtProjectPatternMatch 的前导点规则使 `*`/`?`/括号模式一律失配点文件
- 位置：`SelfHost/Compiler/Driver/Project.krt:77`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：待匹配名字首字节为 `.` 且模式首字节不是 `.`（如 `*.krt` 对 `.hidden.krt`、`[a-z]*.krt` 对 `.a.krt`）。
- 后果：直接 `return false`，即使模式本身可以匹配点字符；用户不会得到任何「点文件被排除」的提示。C 版 `krt_glob_match`（`ProjectKrt.c:203` 起）未见任何 `'.'` 特判（对 `== 46`/`'\.'` 的 grep 无命中）。
- 最小修复：若为对齐 `fnmatch(FNM_PERIOD)` 则同步 C 版与文档；否则删除该特判。
- 备注：疑似；该规则与 `107-111` 的隐藏跳过叠加后行为更难预测。

##### [中] 86-8｜对象输出与最终输出同名时静默返回 1，无诊断
- 位置：`SelfHost/Compiler/Driver/Project.krt:437`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`KrtProjectObjectOutput(project, source)` 与 `options.output` 相等（例如把 `--output` 指向 `obj/xxx.kro`）。
- 后果：`KrtProjectOutputSafe` 那半条件会报 `E_OUTPUT`，但 `KrtDriverStringEqual(object_output, output)` 这半条件成立时直接 `KrtDriverRemoveTree(temporary); return 1;`，没有任何错误信息；用户只看到退出码。
- 最小修复：在该分支补 `KrtDriverError("E_OUTPUT","object output collides with final output")`。
- 备注：同一 if 的两个分支诊断行为不一致。

##### [中] 86-9｜retained_output 缺失时静默失败
- 位置：`SelfHost/Compiler/Driver/Project.krt:467`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`compile_together` 构建后 `temporary/retained.kro` 不存在（编译器未产出该目标、目标类型不保留 KRO）。
- 后果：`!KrtDriverFileExists(options.retained_output)` 成立即删临时目录并 `return 1`，无 `KrtDriverError`；同条件另外两种失败（发布失败）由 `KrtDriverPublish` 报错，唯独文件缺失是静默的，排查困难。
- 最小修复：该分支补 `E_OUTPUT`，说明缺少 retained 对象及对应目标类型。
- 备注：`BuildSystem.krt:105` 是 retained 的实际产出点，可对照确认何种目标不产出。

##### [中] 86-10｜Clean 在 manifest 无效时仍删除缓存目录，输出与对象残留
- 位置：`SelfHost/Compiler/Driver/Project.krt:486-498`（关键 `488` 的 `if (manifest.valid)`、`496`）
- 类别：1 静默失败 / 2 诊断缺失 / 10 资源与生命周期
- 触发条件：manifest 损坏/截断、`count>16384`、尾部多余字节，或 `manifest.project != project.path`（项目被移动、复制、路径写法变化）。
- 后果：`bin/...` 与 `obj/...` 产物全部保留，`.krtcache` 被 `RemoveTree` 删除，命令仍打印 `cleaned:` 并返回 0；用户以为项目已清理干净。
- 最小修复：manifest 无效时至少打印「无法确认上次产物」并保留缓存，或无条件删除已知 output/obj 后再删缓存。
- 备注：`225-227` 的注释只承诺「configuration 变化/依赖删除不阻塞清理」，不覆盖项目路径变化。

##### [中] 86-11｜KrtProjectClean 忽略 KrtDriverRemoveFile 的失败
- 位置：`SelfHost/Compiler/Driver/Project.krt:490`、`493`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：产物只读、权限不足、文件被占用，`unlink` 返回非 0 且非 `-2`。
- 后果：删除失败被忽略，随后 `496-497` 仍打印 `cleaned:` 并返回 0；对比 `496` 的 `KrtDriverRemoveTree` 有返回值检查，两处策略不一致。
- 最小修复：检查返回值，失败时 `KrtDriverError("E_OUTPUT", ...)` 并返回 1。
- 备注：`PlatformAbstraction.krt:366` 明确返回 bool。

##### [中] 86-12｜KrtProjectReadManifest 对损坏 manifest 完全静默
- 位置：`SelfHost/Compiler/Driver/Project.krt:291-302`（关键 `293`、`297`、`301`）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：magic 不匹配、`file.length < 48`、`count > 16384`、字符串越界、`reader.position != reader.length`、`project` 不匹配。
- 后果：一律返回 `valid=false`，构建路径静默重建（尚可接受），但 `Clean` 会走 86-10 的「删缓存留产物」路径；用户全程看不到「缓存已损坏」。
- 最小修复：区分「文件不存在」与「解析失败」，后者打印告警并计入诊断。
- 备注：构建静默重建本身合理，问题在不可观测与 clean 的连带后果。

##### [中] 86-13｜KrtProjectExpand 对未闭合 `${` 静默保留字面量
- 位置：`SelfHost/Compiler/Driver/Project.krt:719-721`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：字符串中出现 `${name`（扫描到串尾仍无 `}`）。
- 后果：`end == length` 时 `index++; continue;`，`${name` 原样进入配置值，后续 `Name`/`Output` 变成带 `${` 的路径，错误在很晚才以「文件不存在」等形式暴露，且无任何配置期诊断。
- 最小修复：该分支报 `E_PROJECT unterminated variable expansion` 并失败。
- 备注：疑似；C 版无可比符号，按诊断缺失记。

##### [中] 86-14｜Legacy 格式探测只认 5 个键，其它旧式配置被送去 Configure 解析器
- 位置：`SelfHost/Compiler/Driver/Project.krt:971-985`（关键 `980`）、`993`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：旧式 `key: value` 配置中出现 `optimization`、`target`、`includes`、`libraries`、`compiletogether`、`version`、`description` 等未列入 `980` 的键。
- 后果：`KrtProjectLegacyFormat` 返回 false，`KrtProjectParse` 走 `KrtProjectConfigure`，lexer 把 `optimization: 2` 解析成标识符后报 `unsupported configuration statement`（`875`），旧配置整体不可用。
- 最小修复：把 `980` 的键集合与 `KrtProjectOption`（`812-823`）对齐，或改为「存在合法的 `key:` 行即判定 legacy」。
- 备注：疑似；C 版配置模型不同，无同类探测逻辑可比。

##### [中] 86-15｜KrtProjectNew 部分写入失败不回滚，留下不可重试的半成品目录
- 位置：`SelfHost/Compiler/Driver/Project.krt:541-542`
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`project.krt`、`.gitignore` 或 companion 源写入失败（权限、磁盘满）。
- 后果：`src/main.krt` 已写入但命令返回 1；由于 `514` 用「目录非空即拒绝」，用户直接重跑 `new` 会报 `destination is not empty`，必须先手工清理。
- 最小修复：任一步失败时删除本次已创建的目录/文件，或先写临时目录再整体发布。
- 备注：`541` 的三连写用 `||` 短路，第一处失败后两者都不执行，但已写内容保留。

##### [中] 86-16｜KrtProjectBuildKey 不检查配置文件二次读取的失败
- 位置：`SelfHost/Compiler/Driver/Project.krt:335`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`KrtProjectLoad` 成功后 `project.path` 被删除或变为不可读，`KrtDriverReadFile` 失败（`configuration.ok == false`）。
- 后果：`configuration.data/length` 为空仍被 `KrtProjectHashBytes` 哈希，key 静默变小；若与旧 key 相撞则直接 `up-to-date`，用旧产物掩盖配置已丢失的事实。
- 最小修复：检查 `configuration.ok`，失败时强制 `rebuild` 或报 `E_PROJECT`。
- 备注：与 86-1 同源（读取失败被吞），但位置与后果独立。

##### [中] 86-17｜Sources glob 会把上一轮构建的 `obj/` 产物当作输入
- 位置：`SelfHost/Compiler/Driver/Project.krt:251`（收集点）、`125`（glob 收集文件）、`376`（对象输出固定落在 `obj/`）
- 类别：3 语义分叉 / 7 共享可变状态
- 触发条件：`p.Sources("**")`、`**/*`、`obj/**` 等会遍历 `obj/` 的模式。
- 后果：`obj/unit-N.kro` 被 `KrtProjectGlobWalk` 当作文件收集，`KrtProjectAdd` 按 `.kro` 加入 `project.objects`；build key 由此包含自身产物，形成自引用，输入集合随构建次数增长，链接输入也变化。
- 最小修复：`KrtProjectLoad` 收集时排除 `project.directory/obj` 与 `.krtcache`（以及 `project.output` 所在目录）。
- 备注：`KrtProjectNew` 生成的默认 `src/**/*.krt` 不受影响，仅用户宽模式触发。

##### [中] 86-18｜glob 对不存在/不可读目录无诊断，统一表现为「无匹配」
- 位置：`SelfHost/Compiler/Driver/Project.krt:105`、`120`（`KrtDriverListEntries` 返回值直接使用）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：目录不存在、`opendir` 权限不足；`PlatformAbstraction.krt:297` 的 `KrtDriverListEntries` 实现内无 `KrtDriverError`（该文件 grep 无错误调用）。
- 后果：返回空/null，`KrtProjectGlobWalk` 正常返回，最终报 `source pattern matched no files`，真实原因（不可读）被掩盖。
- 最小修复：区分「空目录」与「列举失败」，后者报 `E_INPUT`。
- 备注：疑似；未通读 `KrtDriverListEntries` 实现，仅确认其所在文件无错误上报。

##### [低] 86-19｜build key 未包含 show_ir，show-ir 构建会改写缓存清单
- 位置：`SelfHost/Compiler/Driver/Project.krt:326-342`（无 show_ir）、`419`（restore 条件排除 show_ir）
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：同一项目先 `krt build --show-ir` 再普通 `krt build`（或相反），其余选项不变。
- 后果：`--show-ir` 跳过 restore 并执行完整构建、写 manifest，但 key 与常规构建相同；随后常规构建会以该 manifest 直接 `up-to-date`。若 show-ir 路径对产物有副作用，常规构建不会再校验。
- 最小修复：把 `options.show_ir` 纳入 key，或明确 show-ir 构建不得写 manifest。
- 备注：正常情况产物相同，故为低。

##### [低] 86-20｜`if (!clean)` 与 `source == null` 为不可达死代码
- 位置：`SelfHost/Compiler/Driver/Project.krt:239`（`clean` 已在 `227` 提前返回）、`408`（`255` 已保证 `project.sources != null`）
- 类别：5 死代码
- 触发条件：任何调用路径。
- 后果：`239` 的条件恒真；`408` 的 `KrtDriverCompileBundle(options,null)` 分支不可达。若未来 `KrtProjectLoad` 放开 sources 检查，`408` 会以 null bundle 调用编译，行为未定义。
- 最小修复：删除 `if (!clean)` 外壳；把 `408` 改为断言或删除。
- 备注：不影响当前行为，属可维护性/防御性缺口。

##### [低] 86-21｜project-info 输出字段不全，无法审计实际构建配置
- 位置：`SelfHost/Compiler/Driver/Project.krt:551-561`
- 类别：2 诊断缺失 / 12 测试不足（契约不可验证）
- 触发条件：执行 `krt project-info`。
- 后果：输出仅含 path/name/type/output/configuration/sources/includes/objects，缺少 `optimization`、`target`、`compile_together`、`libraries`、`patterns`、`version`、`description`；其中 `libraries`/`patterns` 是声明态输入，恰恰是排查「为什么选了这些源」所需的字段。
- 最小修复：补齐上述字段（`patterns`/`libraries` 可复用 `KrtProjectInfoPaths`）。
- 备注：疑似契约不完整；不影响构建。

##### [低] 86-22｜KrtProjectSet 对空参数列表静默成功
- 位置：`SelfHost/Compiler/Driver/Project.krt:831-837`
- 类别：1 静默失败
- 触发条件：`p.Sources()`、`p.Includes()`、`p.Libraries()`（无实参）。
- 后果：`values == null` 时 while 被跳过，直接 `return true`，无任何诊断；若发生在 `Sources`，错误会推迟到 `240` 的 `no Sources defined`，掩盖真实写法问题。
- 最小修复：`values == null` 时报 `E_PROJECT option requires at least one value`。
- 备注：疑似；对比 `option == 11` 对 null 有显式报错，策略不一致。

##### [低] 86-23｜项目类型错误消息与实际接受集合不一致
- 位置：`SelfHost/Compiler/Driver/Project.krt:234`、`513`（接受集合见 `202-204`）
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：`p.Type("foo")` 或 `krt new foo dir`。
- 后果：提示只写 `choose console, system, or library`，但 `KrtProjectKind` 实际接受 `console/exe/system/web/library/lib/dll`，用户无法从诊断得知可用值。
- 最小修复：消息改为完整集合，或改为通用文案「unsupported project type」。
- 备注：两处消息完全相同，需同步。

##### [低] 86-24｜KrtProjectOptions 未复制 quiet/command/name，check 的逐文件单元行为与顶层不一致
- 位置：`SelfHost/Compiler/Driver/Project.krt:343-352`（缺 `quiet`/`command`/`name`）、`410-412`
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：`CompileTogether(false)` 的 `krt check`（`410` 构造 unit 时未设 `quiet`）。
- 后果：`unit.quiet` 取新对象的默认值而非 `original.quiet`，顶层 `--quiet` 对逐文件检查失效，输出噪声与顶层策略不一致；`command`/`name` 丢失，当前 `BuildSystem.krt` 未使用它们，属潜在契约缺口。
- 最小修复：在 `KrtProjectOptions` 中补拷 `quiet`/`command`/`name`（以及其它新增字段），或在 `410` 显式设置。
- 备注：疑似；`BuildSystem.krt:34/113` 确实读取 `options.quiet`。

##### [低] 86-25｜var 分支出错后仍继续 Assign，可能掩盖首个错误
- 位置：`SelfHost/Compiler/Driver/Project.krt:866-868`
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：`var 123 = ...`（变量名不是标识符）等导致 `866` 的 `KrtProjectFail`。
- 后果：Fail 后无 return，`KrtProjectToken`/`KrtProjectNext`/`KrtProjectTake` 在 `errors != 0` 下空转，`KrtProjectExpression` 返回 null，仍执行 `KrtProjectAssign(parser,name,null)`，把垃圾名字写入变量表；后续若再引用该名字，首错会被「unknown variable」类二次错误掩盖。
- 最小修复：`KrtProjectFail` 后立即 `return`（并补 `parser.depth--`）。
- 备注：影响仅限错误路径，故为低。

##### [低] 86-26｜构建临时目录只以 PID 命名，存在复用与残留风险
- 位置：`SelfHost/Compiler/Driver/Project.krt:423`（`syscall(39,...)` 为 getpid）
- 类别：7 共享可变状态 / 10 资源与生命周期
- 触发条件：同进程内并发/重入构建，或系统 PID 复用后碰上上次崩溃残留的 `.krtcache/.build-<pid>`。
- 后果：两次构建共用同一临时目录，`worker-*.log`/`unit-*.kro` 可能互相覆盖；`MakeDirectories` 对已存在目录大概率成功，残留文件会被当成本轮产物。
- 最小修复：加入构建序号/随机后缀，并在构建开始时清理陈旧的 `.build-*`。
- 备注：单进程 CLI 顺序执行下触发概率低。

### 第八十七批：SelfHost/Compiler/Frontend/Lexer/Lexer.krt（全量）

#### 覆盖范围

- 目标文件：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt`，共 953 行，已按 1–250 / 250–499 / 499–748 / 749–953 四段全量读完，无跳读。
- 对照文件（仅定点读取相关行，未整文件通读）：`Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c`（185–233、244–341、340–416、440–524、560–609、795–859）、`Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.h`（10–119、120–185、185–239）、`Re.KrtC/src/Core/Utils/IntegerWidths.def`、`Re.KrtC/src/Accelerator.c`（83–101）。
- 符号级核验（仅 grep 特定符号）：`lexer_integer_type_bits`、`KrtTokenKind.Print/Auto/Attribute/BitwiseOr`、`token.error`、`KrtTokenIntegerSize/KrtTokenIntegerCommon`、`KrtLexerInit/KrtLexerNext`；并定点读取调用方 `SelfHost/Compiler/Frontend/Parser/Parser.krt:211`、`SelfHost/Compiler/Driver/Driver.krt:1114–1166`、`SelfHost/Compiler/Middle/Ir/Ir.krt:1725–1764`、`SelfHost/Compiler/Driver/Project.krt:678,988`、`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:106`、`SelfHost/Compiler/Frontend/Parser/Parser.krt:1625`、`Test/SelfHost/test_lexer_vocabulary.py`。
- 结论：基础词法（标识符、关键字表、最长匹配、EOF 稳定性、无死循环）与 Re.KrtC 基本对齐；未发现必然崩溃路径。发现问题集中在换行计数、数值词法、转义、诊断链路与契约/死代码。


##### [高] 87-1｜CRLF 每次换行计两次，行号/列号整体漂移
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:39-42`（`lexer_advance`）
- 类别：语义分叉 / 诊断错位
- 触发条件：源文件使用 CRLF（`\r\n`）换行，或单独使用 `\r` 换行。
- 后果：`value == 10 || value == 13` 各自执行一次 `line = line + 1`，CRLF 被计为两行。含 N 个 CRLF 的文件实际行号为 2N+1 而非 N+1，所有 `token.line` 及后续 Parser/诊断报错行号整体错误。Re.KrtC 参考实现 `Tokenizer.c:185-193` 只在 `'\n'` 时 `line++`，`\r` 仅作空白跳过，二者语义分叉。
- 最小修复：仅在 `value == 10` 时换行；若需兼容孤立 `\r`，则在 `value == 13` 且下一个字节不是 10 时才换行。
- 备注：该函数是所有 token 位置的唯一来源，影响面为全量输入，非边缘用例。

##### [高] 87-2｜八进制字面量分支缺失，`0` 前缀数值语义与参考实现分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:89-105`（十进制 `else` 分支）
- 类别：语义分叉
- 触发条件：源码出现 `0` 后跟八进制数字且下一字符不是 `.`/`e`/`E`，例如 `010`、`0777`、`0128`。
- 后果：Re.KrtC `Tokenizer.c:266-271` 对 `0`+八进制数字有独立分支并按八进制数字串切片（`0128` 切成 `012` 与 `8`）；SelfHost 无此分支，`010` 被当作十进制数字串整体交给下游。同一源码在两个编译器下数值常量语义不同，属静默语义分叉。
- 最小修复：确认语言规范后二选一——实现与参考一致的八进制扫描，或在词法层显式拒绝前导零（给出错误码），不要保持现状。
- 备注：疑似移植遗漏；本条只判定「与参考实现不一致且无注释说明」，不预设哪种语义正确。

##### [高] 87-3｜`\u` / `\U` Unicode 转义未支持且被静默降级
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:111-121`（字符串/字符扫描的 escape 分支）
- 类别：语义分叉 / 静默失败 / 诊断缺失
- 触发条件：字符串或字符字面量中出现 `\uXXXX` / `\UXXXXXXXX`，如 `"\u0041"`、`'\u00e9'`。
- 后果：`else` 分支只前进一个字符（吞掉 `\` 与 `u`），既不识别 4/8 位十六进制，也不报错。下游唯一解码器 `SelfHost/Compiler/Middle/Ir/Ir.krt:1737-1759` 只支持 `\x`，遇到 `\u` 走 `!known` 分支把 `\` 与 `u` 原样写入数据段，最终字符串内容是字面文本 `\u0041` 而不是 `A`，程序输出错误且无任何诊断。Re.KrtC `Tokenizer.c:485-502` 明确支持 `\u`/`\U` 并编码为 UTF-8。
- 最小修复：在词法扫描处识别 `\u`/`\U` 并校验位数（缺失即置错误码），或在其文档/规范中显式声明不支持并让下游拒绝；至少不能静默通过。
- 备注：字符字面量因 `count` 统计会触发错误 6（`'\u0041'` 计 5），但字符串字面量完全无保护，是主要缺口。

##### [高] 87-4｜词法错误码 1/2/3/5/6 全链路无人消费，诊断退化为通用 invalid syntax
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:58,80,86,98,106,127,128,133,928,939`（写入端）；`SelfHost/Compiler/Frontend/Parser/Parser.krt:211`（仅拷贝）；`SelfHost/Compiler/Driver/Driver.krt:1122-1123,1165`（只取 `token.start`）
- 类别：诊断缺失 / 阶段不对齐 / 错误处理不一致
- 触发条件：出现未闭合块注释（2）、未闭合字符串（3）、数字格式错误（5）、字符长度错误（6）、未知标点（1）。
- 后果：`KrtToken.error` 被精确赋值，但全库 grep 显示除测试外没有任何读取点：Parser 只在 fork 提交时把 `probe.token.error` 拷回（`Parser.krt:211`），从不判值；Driver 仅记录 `token.start` 后统一报 `"E_PARSE", "invalid syntax"`（`Driver.krt:1165`）。未定义错误码 4，也没有码→消息映射表。用户无法区分「未闭合注释」与「非法数字」，定位困难。
- 最小修复：在 Parser/Driver 消费 `token.error` 并映射为具体诊断码与消息；补齐码表（含 4 的去向）。
- 备注：与 Re.KrtC 的 `lexer->error_count/error_line` 聚合模型（`Tokenizer.c:225,403,517,542`）也不同，属错误模型整体分叉，建议一并规整。

##### [中] 87-5｜`.5` 被并为单个 Number，与参考实现的 DOT+NUMBER 分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:74`
- 类别：语义分叉
- 触发条件：源码出现 `.5`、`.25` 等以点开头的小数。
- 后果：`(first == 46 && lexer_digit(lexer_at(lexer, 1)))` 使 `.5` 成为单个 `Number`/Floating token；Re.KrtC `Tokenizer.c:586-600` 的分派只认 digit/alpha，`.` 走标点分支，产生 `TOKEN_DOT` + `TOKEN_NUMBER`。同一表达式在两侧 token 流不同，若下游按参考语义解析会分叉。
- 最小修复：确认是否有意支持省略整数位的小数；若有意，补注释并在规范中登记，同时确认 Parser 对 `Dot` 的前瞻不受影响；若无意，删除该条件。
- 备注：疑似有意的语言扩展，但代码与注释均未说明，标「疑似」。

##### [中] 87-6｜数字后缀 f/F/d/D 与指数/尾缀错误策略同参考实现分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:95-106`
- 类别：语义分叉 / 错误处理不一致
- 触发条件：`1f`、`1.5d`、`1e`、`1e+`、`1abc`、`12x`。
- 后果：SelfHost 新增 `f/F/d/D` 后缀并把 token 标为 Floating（101-104），Re.KrtC `Tokenizer.c:244-313` 不识别后缀，`1f` 是 `NUMBER(1)` + `IDENTIFIER(f)`；`1float` 在参考侧是 `NUMBER` + 关键字 `float`，在 SelfHost 侧被切成带错误 5 的 `Number("1f")` + `IDENTIFIER("loat")`。同时 SelfHost 对指数缺数字（98）与数字后跟字母（106）报错，参考实现一律静默接受。同一源码 token 流与诊断均不同。
- 最小修复：明确后缀是否属于语言规范；若属于，同步修正参考或在文档登记扩展；若不属于，删除 101-104 与 98/106 的报错，避免与参考漂移。
- 备注：标「疑似」有意扩展；`1f` 这类写法虽少见，但 `1e` 类输入在真实文件中并不罕见。

##### [中] 87-7｜`KrtLexerInit` 前置条件不足且返回值可被静默忽略
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:10-17`；调用方 `SelfHost/Compiler/Driver/Project.krt:988`
- 类别：未检查前置条件 / 静默失败 / 契约破坏
- 触发条件：(a) `length` 大于 `source` 实际缓冲区长度；(b) 调用方丢弃 `bool` 返回值。
- 后果：Init 只校验 `length < 0` 与 `source == null && length != 0`，不校验 `length` 与真实缓冲容量的关系；随后 `lexer_at`（19-23）在 `unsafe` 块中按 `length` 直接索引，传错长度即越界读。`Project.krt:988` 写成 `KrtLexerInit(parser.lexer,(byte*)source.data,source.length);` 直接丢返回值，初始化失败时会以零值 lexer（source=0,length=0）继续，静默得到空 token 流而无诊断（对比 `CompilerPipeline.krt:345`、`Types.krt:472` 均有检查）。
- 最小修复：增加容量参数或让调用方保证并检查返回值；至少在所有调用点禁止忽略 `bool`。
- 备注：`KrtLexerWordEqual`（300-308）还把 `string` 强转 `byte*` 并按 `word_length` 读取，同样依赖调用方传入真实长度，属同类脆弱契约（内部字面量调用暂未触发）。

##### [中] 87-8｜UTF-8 字节无处理、无校验，且与参考实现的静默策略分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:129-136`（标点兜底）、`486-591`（`KrtLexerPunctuationKind`）
- 类别：未检查前置条件（UTF-8）/ 语义分叉
- 触发条件：源码在字面量/注释之外出现非 ASCII 字节，如中文标识符 `变量`、全角标点、孤立续字节。
- 后果：`lexer_letter`/`lexer_digit` 仅 ASCII（25-26），每个 ≥0x80 字节都落入 `KrtLexerPunctuationKind` 的 `Unknown` 并置错误 1；Re.KrtC `Tokenizer.c:798-820` 对 ≥0xC0 的前导字节按 UTF-8 长度跳过后续字节（其自身是静默丢弃），其余才报 `TOKEN_UNKNOWN`。两侧对同一输入的处理策略不同；SelfHost 既不支持非 ASCII 标识符，也不做 UTF-8 合法性校验（无效序列同样只报 error 1）。
- 最小修复：明确语言对非 ASCII 的立场；若不支持，统一在词法入口拒绝并给出准确诊断；若支持，按 UTF-8 解码标识符，且与参考实现策略对齐。
- 备注：参考实现的「静默跳过」本身也是缺陷，不主张照抄，但当前分叉会导致诊断噪声与恢复行为不一致。

##### [中] 87-9｜`KrtTokenKind` 尾部别名枚举制造取值陷阱
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:896`
- 类别：契约破坏 / 语义分叉风险
- 触发条件：任何按名字比较 `KrtTokenKind` 的代码，尤其是 `token.kind == KrtTokenKind.Invalid`（本文件 68、102；`Project.krt:678`；`Driver.krt:1122`）。
- 后果：`Integer = 2` 与 `Number` 同值，`Floating = 2` 也与 `Number` 同值，`Character = 4` 与 `CharLiteral` 同值，`Invalid = 298` 与 `Unknown` 同值。于是 `kind == Floating` 对任何整数 token 都为真；`Invalid` 实际就是 `Unknown`，`Lexer.krt:68` 的「无效 token」判定依赖这个别名巧合才能成立。Re.KrtC `Tokenizer.h` 中没有这些别名（已核对 12–200 行枚举），`KrtTokenCategory` 另有独立取值（901）。一旦有人按名字增删枚举或调整顺序，68/102 行的控制流会静默改变。
- 最小修复：删除这四个别名，错误判定改用 `category == KrtTokenCategory.Invalid` 或 `error != 0`；如必须保留，给它们独立数值并加静态断言。
- 备注：与 87-14 的「数值对齐」注释直接冲突。

##### [中] 87-10｜重复的自由函数五件套与恒真守卫构成死代码
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:943-953`（自由 `Reset/SetKind/SetCategory/SetError/SetLength`）；`102`（`if (token.kind != KrtTokenKind.Invalid)`）
- 类别：死代码
- 触发条件：任何阅读/维护该文件的路径；无运行时触发。
- 后果：943-953 与类方法 930-941 签名同构、语义重复，全库 grep 仅有定义、无任何调用（调用点全部使用 `token.SetXxx` 方法形式，如 46/58/72/75/92/108/132/137/138）。102 行守卫在数字分支内此时 `kind` 必为 `Number`，条件恒真，属永远成立的死分支。两者都增加「改了一处漏另一处」的维护风险。
- 最小修复：删除 943-953；102 行直接调用 `token.SetCategory(KrtTokenCategory.Floating);`。
- 备注：若自由函数是为外部 C 风格接口预留，应在注释中说明并补调用或测试，否则视为死代码。

##### [中] 87-11｜裸指针 source + 仅存 span 的 token，共享可变状态无生命周期约束
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:3-8`（public 字段 `source/length/position/line/column`）、`19-23`（`lexer_at`）、`921-927`（token 只存 `start/length/line/column`）；消费方 `Driver.krt:1146`、`Types.krt:472`、`KroBackend.krt:2069`
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：源缓冲在词法产出 token 后被修改、复用或释放；或调用方直接改写 `lexer.source/position`。
- 后果：`source` 是裸 `int64` 指针且 public，无所有权、无不可变约束；token 不缓存文本，后续阶段再用 `lexer.source`/`file.original + token.start` 回读（如 `KroBackend.krt:2069` 还基于 `source+type.start` 建子 lexer）。缓冲一旦失效，已产出的 token 全部变成悬垂引用或读到错误文本，且无任何机制能检测到。Re.KrtC 用 `lexer_create/lexer_destroy` 与 `source_owned` 管理生命周期（`Tokenizer.h:209-227`），SelfHost 完全缺失等价物。
- 最小修复：由 lexer 持有源缓冲的不可变副本或引用计数；至少在文档中固化「源缓冲必须存活到编译结束」的契约，并提供重置/释放入口。
- 备注：当前调用链多为同生命周期栈对象，尚未见实际悬垂，但契约完全靠约定。

##### [中] 87-12｜整数位宽解析存在两套实现，注释宣称的「语义精确」缺乏单一事实源
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:142-154`（`lexer_integer_type_bits`）与 `315-319`（`KrtLexerWordKind` 内联解析）
- 类别：阶段不对齐 / 配置漂移 / 注释与实现脱节
- 触发条件：维护者修改整数宽度规则（新增宽度、调整奇偶约束）时只改其中一处。
- 后果：两处都在解析 `u?int<偶数宽>`：142 用 `index + 3 >= end` 与循环内 `bits > 128` 提前返回 0；315 用 `index + 3 < end` 与循环条件 `width <= 128`，最后再交给 `KrtTokenIntegerType`。当前对合法输入结果一致（`Semantic.krt:106`、`Parser.krt:1625`、`test_lexer_vocabulary.py:59` 都依赖二者相等），但判据写法不同、边界路径不同，属典型双源漂移风险；141 行注释把 142 描述为「semantic precision」的来源，实际关键字识别走的是另一套代码。
- 最小修复：抽出唯一的 `lexer_integer_width_at(source,start,length)`，两处共用；141 行注释改为指向该唯一实现。
- 备注：`IntegerWidths.def` 是 C 侧事实源，SelfHost 枚举与解析均为手抄（见 87-13 关联风险）。

##### [低] 87-13｜枚举中存在无产生路径的死成员：`Print` / `BitwiseOr` / `Attribute` / `Auto`
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:44`（Print=44）、`215`（BitwiseOr=215）、`251`（Attribute=251）、`292`（Auto=292）；产生端 `310-484`、`486-592`
- 类别：死代码 / 配置漂移
- 触发条件：词法产生 token 的任何路径。
- 后果：全库 grep `KrtTokenKind.Print|Auto|Attribute|BitwiseOr` 仅命中 `Lexer.krt:916` 的分类判断，无任何赋值/返回：关键字表无 `print`/`auto` 词条；`|` 固定返回 `Pipe`（542），永远不会产生 `BitwiseOr`；`@` 不在标点表，属性语法只能落到 `Unknown`+错误 1；`Attribute` 因此被分类函数引用却永不存在。Re.KrtC 同样缺产生路径（`Tokenizer.c` 关键字表与标点表已核对），属继承性死枚举，但 595 行注释宣称与 `Tokenizer.h` 对齐，未说明这些成员不可达。
- 最小修复：删除不可达成员（会改变枚举数值，需同步全库），或补齐产生路径并在注释中登记为保留字。
- 备注：若为将来语法预留，应集中放到单独的保留枚举并注明，避免污染当前 token 分类。

##### [低] 87-14｜注释与实现脱节：无效 token 判定、数值对齐声明、NUMBER 语义说明三处失真
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:65`、`141`、`595`、`899-900`
- 类别：注释与实现脱节
- 触发条件：维护者按注释理解行为时。
- 后果：65 行称「An invalid token is recoverable; never leave the cursor stuck」，但 68 行判的是 `kind == KrtTokenKind.Invalid`，而该值是 `Unknown` 的别名（见 87-9），语义与注释不符；595 行称数值与 `Tokenizer.h` 完全一致，但 896 行追加了 C 侧不存在的四个别名；899-900 行称 NUMBER 的整数/浮点拼写与 Re.KrtC 对齐，实际 87-2/87-6 已列出多处分叉（八进制、后缀、指数与尾缀报错）。
- 最小修复：逐条改写注释使其与当前实现一致；对有意扩展（如 `.5`、`f/d` 后缀）在注释中显式登记，对未实现项（如 `\u`）标注限制。
- 备注：注释失真会直接误导后续移植与审计，优先级高于一般风格问题。

##### [低] 87-15｜测试覆盖不足：换行、数值分叉、转义、宽度边界均未覆盖
- 位置：`Test/SelfHost/test_lexer_vocabulary.py:100-124`（现有 4 个用例）
- 类别：测试不足
- 触发条件：回归测试运行时。
- 后果：现有用例覆盖关键字与整数宽度词表、最长匹配、前缀/大小写、字面量与未知字节恢复；未覆盖：CRLF/孤立 CR 的行号列号（87-1）、`010`/`0128` 八进制切片（87-2）、`\u`/`\U`（87-3）、`1e`/`1f`/`1abc` 的错误策略（87-6）、`.5`（87-5）、未闭合注释后的连续 EOF 稳定性、`int2`/`int128`/`int130` 边界、超长标识符、错误码 1/2/3/5/6 的码值断言。上述已证实的分叉点 therefore 无回归保护，未来改动可静默退化。
- 最小修复：为每个已确认分叉补充用例，至少断言 `token.line/column`、数字 token 的 `start/length/kind/category/error`、转义扫描后的 `length` 与错误码。
- 备注：`test_lexer_vocabulary.py:59` 已对 `lexer_integer_type_bits` 与 token 宽度做一致性断言，可作为 87-12 的扩展锚点。

##### [低] 87-16｜`KrtTokenIntegerType` 对非法位宽静默返回 `Eof`
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:165-298`（出口 `297`）
- 类别：静默失败 / 契约破坏
- 触发条件：调用方传入不在 2..128 偶数集合的 `bits`，或传入奇数/超界值。
- 后果：函数不返回错误而是返回 `KrtTokenKind.Eof`（0），与「文件结束」这一合法 token 种类同值。调用方若只判断「返回值非 0」或直接把结果写入 `token.kind`，会把非法类型当成 EOF 或产生错误控制流。Re.KrtC `Tokenizer.h:234-235` 确实约定了「unsupported width 返回 TOKEN_EOF」，因此这是继承契约，但 SelfHost 侧没有任何断言或调用方检查（`KrtTokenIntegerBits`/`KrtTokenIsUnsigned` 亦无防御）。
- 最小修复：改为返回 `Unknown` 或增加 `bool* ok` 出参；至少在调用点断言 `bits` 为 2..128 的偶数后再调用。
- 备注：标「低」是因为当前调用链未发现误用；但作为公开 API 契约，静默返回 EOF 属危险设计。

### 第八十八批：SelfHost/Compiler/Pipeline/CompilerPipeline.krt + Platform/PlatformAbstraction.krt

#### 覆盖范围

- 全量读取 `SelfHost/Compiler/Pipeline/CompilerPipeline.krt`（575 行，逐段读完）与 `SelfHost/Compiler/Platform/PlatformAbstraction.krt`（528 行，逐段读完）。
- 对照 `Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c`、`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.c`：两者与 SelfHost 版结构不同（C 版是 `KrtCompilePipeline`/`KrtPlatform` 另一套 API），仅对 `system()` vs `execve` 环境语义等可对照点做参照，其余标「疑似」。
- 符号级 grep 核对（未整文件通读）：`KrtNativeFunctionIndexInvalidate`、`KrtModulesMarkImportedAst`/`MergeImportedAst`、`KrtErrorReportFlush`/`Add`/`HasErrors`、`KrtDiagnosticMessage`、`KrtPrintRuntimeRequired/Source`、`KrtVmEmit`、`KrtParserParseCompilationUnit`、`KrtSemanticValidateFunction`、`KrtNativeClosureProbeState/Mutation`、`KrtNativeValidateAccess`、`KrtDriver*` 全部调用点、`Test/` 下相关测试引用。
- 结论：两文件共提取 **40 条**问题，其中致命 1、高 2、中 18、低 19。


##### [致命] 88-1｜KrtCompileSource 对 null 的 unit 调用语义校验导致崩溃
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:376`（关联 352-358、380-381）
- 类别：8 未检查前置条件（NULL）
- 触发条件：源码顶层只有 1 个声明（`compilation_unit.child_count == 1`）且其中没有 main 函数，`main_unit` 保持 null，`unit = main_unit` 也为 null。
- 后果：`KrtSemanticValidateFunction(unit)` 在 `Semantic.krt:1923` 直接读 `unit.kind`，空指针解引用，编译器进程崩溃；此时 `parser.errors == 0`，属完全正常的输入路径。
- 最小修复：376 行改为 `else if (!ok && compilation_unit.child_count == 1 && unit != null)`；或在校验前对 `unit == null` 直接返回并补一条 E_ENTRY 诊断。
- 备注：同函数 381 行对同一个 `unit` 已写 `unit != null` 检查，376 行遗漏，属明显不对称。该路径仅经 `KrtCompileSource`/`KrtCompileSourceToFd`/`KrtCompileSourceToPath` 可达（`Test/SelfHost/test_contract.krt` 在用），驱动主路径不经过。

##### [高] 88-2｜解析错误被静默吞掉，无任何诊断
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:360-394`（关键 383、388）
- 类别：1 静默失败 / 2 诊断缺失 / 9 错误处理不一致
- 触发条件：源码存在语法错误（`parser.errors != 0`）。
- 后果：360/365/371/383/388 各分支的 `parser.errors == 0` 前提全部不成立，函数直接 `return false`，全程未调用 `KrtDiagnostic`；而同一份源码走 `KrtCompileNativeSourceMode` 会在 469-475 输出 E_PARSE 及逐条 parser 诊断。同一编译请求因入口不同得到「有诊断 / 无诊断」两种结果。
- 最小修复：在 383 行之前（或函数尾部）对 `parser.errors != 0` 复用 469-475 的 E_PARSE 逻辑。
- 备注：`KrtCompileSourceToFd`/`ToPath` 把 false 直接回给调用方（含 `Driver.krt:496` 的 stage1 探针），调用方只能看到失败而看不到原因。

##### [中] 88-3｜KrtCompileSource 只删根节点，整棵 AST 泄漏
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:389-393`
- 类别：10 资源与生命周期
- 触发条件：`KrtCompileSource` 的任意返回路径（无论 `ok` 与否）。
- 后果：仅 `delete compilation_unit`（Program 节点自身），`unit.left` / `unit.declarations` / `unit.body` 整棵语法树全部未释放；对照 `KrtCompileReleaseAstGraph:305-314` 的逐节点释放。每次经该入口的编译都泄漏一整棵树。
- 最小修复：复用 `KrtCompileReleaseAstGraph(compilation_unit, null, parser.current_using_scope)`，或至少遍历各链表逐个释放。
- 备注：常量对象路径（363/369/374/386）与原生回退路径（388）都会执行到 390 行。

##### [中] 88-4｜IR 发射失败后静默回退到完整原生管线
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:383-388`
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：`KrtSemanticBindLocals`/`KrtIrLowerReturnFunction` 成功但 `KrtKroEmitIrFunction` 返回 false，或 383 行任一前置校验失败。
- 后果：不报告任何错误即调用 `KrtCompileNativeSource` 重新解析并编译同一源码；同一源码在「紧凑 IR 对象 ABI」与「完整原生管线」两套产物/诊断之间静默切换，调用方无法感知实际走了哪条路。
- 最小修复：区分「语义校验失败（应报错）」与「发射失败（可回退）」，回退前记录诊断或提供显式回退开关。
- 备注：388 行的 `!ok` 为恒真（见 88-6），回退实际是无条件发生的。

##### [中] 88-5｜诊断上下文不保存、不安装、不推进 stage
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:338-395`
- 类别：9 错误处理不一致 / 2 诊断缺失 / 4 阶段不对齐
- 触发条件：任何调用 `KrtCompileSource` 的路径。
- 后果：`KrtCompileNativeSourceMode:425-430` 会保存 `previous`、安装 `limit=64` 的 report 并在失败时 flush；`KrtCompileSource` 完全不设置 `KrtCurrentDiagnostics`，其内部诊断经 `CompilerState.krt:282-286` 直通 `KrtDiagnosticWrite` 写 stderr，绕过 bundle 诊断与计数；478/492/519/530 的 stage 推进也全部缺失。
- 最小修复：与 `KrtCompileNativeSourceMode` 一致地保存/安装/恢复 `KrtCurrentDiagnostics` 并推进 stage。
- 备注：两者是同一编译器的两套入口，诊断契约必须一致，否则排障信息不可比。

##### [低] 88-6｜`!ok` 为恒真死条件
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:360、365、371、376、377、383、388`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：总是成立（`ok` 在 343 行初始化为 false，仅在 364/370/375/386 各分支体内置 true）。
- 后果：`!ok &&` 从未起作用，读者会误以为存在「已成功则跳过」语义；后续若改动 `ok` 初始化，行为会无声改变。
- 最小修复：删除 `!ok &&`，或改写为显式状态机。
- 备注：当前无功能影响，但属零容忍审计中的死条件。

##### [中] 88-7｜先 O_TRUNC 打开输出再编译，失败残留空/半截文件
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:410-416`（配合 397-408）
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：`KrtCompileSourceToPath` 中编译失败（399 行），或写 header/text/symbols 任一失败（403-405 行）。
- 后果：411 行以 `O_CREAT|O_TRUNC` 先打开目标路径，失败后目标路径留下 0 字节或半截文件；函数只返回 false，不清理、不诊断（E_OUTPUT 只在 `KrtCompilePath` 里出现）。
- 最小修复：先编译到内存/临时文件，成功后再原子发布（可直接复用 `KrtDriverPublish` 的 temp+rename）；失败时 unlink 目标。
- 备注：`BuildSystem.krt:104` 已用 `KrtDriverPublish` 做原子发布，本函数是遗漏的旧实现。

##### [中] 88-8｜错误上报双轨：KrtDriverError 绕过诊断报告与 bundle
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:134-141`（对照 `CompilerPipeline.krt:540-546`）
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：任何调用 `KrtDriverError` 的路径（`BuildSystem.krt:39/40/45/48/56/75/85/102/104/105/106`、`Driver.krt` 等）。
- 后果：`KrtDiagnosticWrite` 在 `KrtDriverActiveBundle != null` 时改走 `KrtSourcesDiagnosticDetails` 上报；`KrtDriverError` 无条件写 fd 2，只设 `KrtDriverLastErrorCode`。同一逻辑错误是否进入 bundle 诊断、是否被 `KrtErrorReportHasErrors` 统计，取决于恰好调用哪个助手。
- 最小修复：让 `KrtDriverError` 复用 `KrtDiagnosticWrite`（或统一走 `KrtCurrentDiagnostics`/`ActiveBundle` 分流）。
- 备注：`KrtDriverError` 同时也不写 `KrtCurrentDiagnostics`，即使非 bundle 场景也不计入报告。

##### [高] 88-9｜`library.mode` 魔术文件静默切换编译模式
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:569-571`
- 类别：11 配置漂移 / 1 静默失败
- 触发条件：进程 CWD 下存在名为 `library.mode` 的路径——普通文件会触发，**同名目录也会触发**（`open(O_RDONLY)` 对目录成功）；反之文件缺失则不触发。
- 后果：`library_mode` 由当前目录的偶然内容决定，同一份 `program.krt` 在不同目录下会编译出可执行/库两种不同产物（`Driver.krt:496` 的 stage1 探针正走此路径）；打开失败（EACCES 等）与不存在都被当作 false，无任何诊断。
- 最小修复：用显式命令行选项或环境变量表达库模式；至少对「存在但不是普通文件」报错。
- 备注：全仓库仅此一处引用 `library.mode`；与 `KrtDriverArtifactTarget` 同属隐式全局配置，且与 88-16 叠加时会产生「静默产出可执行对象」。

##### [中] 88-10｜KrtCompilePath 在成功与失败路径都泄漏 object/source
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:565-573`
- 类别：10 资源与生命周期
- 触发条件：572 行编译失败、573 行写盘失败，以及正常成功路径。
- 后果：`new KrtKroObject()`（568）与 `new KrtDriverBuffer()`（565）在所有路径都不释放；`KrtCompileSourceToFd:406` 与 `BuildSystem.krt:74` 印证 object 归调用方所有。
- 最小修复：两条失败 return 前 `delete object;`，成功路径同样释放；补 `delete source;`。
- 备注：与 `BuildSystem.krt:65-74` 的同类泄漏同源，属系统性遗漏。

##### [中] 88-11｜KrtCompilePath 依赖未保存的全局 artifact target
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:564-575`（配合 514/524/525）
- 类别：6 契约破坏 / 11 配置漂移
- 触发条件：同一进程内此前执行过 `target=2/4` 的编译且 `KrtDriverArtifactTarget` 未被恢复，再调用 `KrtCompilePath`。
- 后果：`KrtCompilePath` 不保存/不设置 `KrtDriverArtifactTarget`，`CoreRun` 在 524/525 行会按残留值走 IR/VM 分支，「探针」产出非预期产物。
- 最小修复：`KrtCompilePath` 内显式保存并设置 `KrtDriverArtifactTarget`，返回前恢复。
- 备注：`Driver.krt:491-498` 显示该函数在参数解析前执行，当前全局为默认 0 故不触发，但契约脆弱。

##### [低] 88-12｜源文件长度上限两处不一致
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:567`（对照 `PlatformAbstraction.krt:151`）
- 类别：4 阶段不对齐 / 6 契约破坏
- 触发条件：输入文件长度落在 `(16777216, 33554432]` 区间。
- 后果：`KrtDriverReadFile` 允许读到 32MB，`KrtCompilePath` 读到 16MB 后才以 E_SIZE 拒绝；两个上限分属不同层，改动其一会产生「能读不能编」或「不能读」的漂移。
- 最小修复：把上限提为共享常量，或让 `KrtDriverReadFile` 接受调用方上限参数。
- 备注：`BuildSystem.krt:63` 也出现 16777216，但那是合并后 bundle 的上限，语义不同，易被误改。

##### [中] 88-13｜诊断报告与记录在 flush 后不释放
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:426、429-430`（根因 `CompilerState.krt:288-291` 的 `KrtErrorReportFlush` 无释放）
- 类别：10 资源与生命周期
- 触发条件：每次 `KrtCompileNativeSourceMode` 调用（无论成功或失败）。
- 后果：`report = new KrtCompilerDiagnostics()` 与 `KrtErrorReportAdd` 新建的 `KrtDiagnosticRecord` 链在 flush 后都不释放；对照 `Types.krt:1231-1233` 的 closure probe 会逐条 `delete record` 并 `delete KrtCurrentDiagnostics`。每次编译泄漏一条报告链。
- 最小修复：为 `KrtErrorReportFlush` 增加释放（或新增 `KrtErrorReportRelease`），在 430 行恢复 `previous` 前调用。
- 备注：成功路径若有记录同样被直接丢弃。

##### [低] 88-14｜诊断条数静默截断且无提示
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:426`（机制 `CompilerState.krt:269`）
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：单次编译产生的诊断位置数超过 64（`KrtErrorReportAdd` 还会按 `position + code` 去重）。
- 后果：第 65 条起被静默丢弃，用户看到的错误列表不完整，且没有任何「已截断」提示。
- 最小修复：达到 limit 时补一条溢出标记，或约定 limit=0 表示不限。
- 备注：`Types.krt:1199` 的嵌套 report 继承同一 limit，截断会叠加。

##### [中] 88-15｜KrtDriverLastVm 未检查 new，失败后残留且从不释放
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:526-528`（使用点 `BuildSystem.krt:73`）
- 类别：8 未检查前置条件 / 10 资源与生命周期 / 7 共享可变状态
- 触发条件：`new KrtDriverBuffer()` 返回 null（OOM）；或 `KrtVmEmit` 返回 false（E_VM）。
- 后果：526 行未检查即把 null 传给 `KrtVmEmit`，`Interpreter.krt:139` 的 `artifact.ok = false` 空指针崩溃；E_VM 失败时 `KrtDriverLastVm` 仍非空且从不释放，`BuildSystem.krt:73` 只凭 `!= null && ok` 判断，残留值跨编译存在（workspace 释放不清该全局）。
- 最小修复：检查 new；E_VM 失败时置 `KrtDriverLastVm = null`；提供释放路径。
- 备注：`KrtDriverLastModule` 在 320 行有对称清理，`KrtDriverLastVm` 没有，属遗漏。

##### [中] 88-16｜library_mode 被静默改写为 `library_mode && !has_main`
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:483`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：调用方传 `library_mode=true` 但源码含 main 函数（`KrtCompilePath` 在存在 `library.mode` 时即如此）。
- 后果：库模式被静默降级为可执行模式，484 行入口点校验、491 行 `unit.op = 240`、514 行库产物分支全部随之改变；调用方无法知道模式被改写。
- 最小修复：含 main 时显式报 E_MODULE，或忽略 main 继续库模式，二选一并记录诊断。
- 备注：与 88-9 叠加时，「CWD 有 library.mode + 源码含 main」= 静默产出可执行对象。

##### [中] 88-17｜closure probe 状态不在 AST 释放范围内
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:91-120`（`KrtCompileReleaseAstState`）
- 类别：10 资源与生命周期
- 触发条件：closure probe 未正常 `KrtNativeClosureProbeEnd` 就失败退出（如 `KrtNativeLowerModule` 在探测中途返回 false），随后 `KrtCompileWorkspaceRelease` → `KrtCompileReleaseAstState`。
- 后果：只释放 `native_generic_registry`/`native_globals`/`native_inheritance_stack`/`native_function_index`，不释放 `unit.native_closure_probe_state`；该状态含 mutation 快照（深拷贝的 `KrtAstNode`，`Types.krt:1102-1105`）与 `previous` 链，全部泄漏。
- 最小修复：在 `ReleaseAstState` 中遍历并释放 probe state 链（含 `mutation.snapshot`）。
- 备注：疑似——取决于 lower 失败时是否确实遗留未结束的 probe；正常路径 `Types.krt:1234` 会 `delete state`。

##### [中] 88-18｜释放队列失败后，回退路径只清 4 个字段，整图泄漏
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:283、328-330`
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：`KrtCompileAstPush` 扩容失败（节点数 > 16777216 或 `new` 返回 null）使 `queue.ok = false`。
- 后果：`KrtCompileAstReleaseMarks` 只清标记并释放 `queue.nodes`/`queue`，已标记的 AST 节点、scope、signature、generator、capture、cell_owner 全部未释放；调用方 328-330 的回退只调 `KrtCompileReleaseAstState`（仅清 unit 上 4 个字段），整棵树泄漏。
- 最小修复：失败回退时遍历 `queue.nodes` 释放节点及其自有数组，并释放各链。
- 备注：队列失败属 OOM 路径，但零容忍审计要求失败路径也不泄漏。

##### [低] 88-19｜print_runtime 的 op=241 标记只覆盖顶层声明
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:465-468`
- 类别：6 契约破坏 / 4 阶段不对齐
- 触发条件：`KrtPrintRuntimeRequired` 为真，且注入的运行时源中存在嵌套（非 `unit.left` 顶层）并需要 `op=241` 的节点。
- 后果：只遍历 `unit.left` 链设置 `op=241`，嵌套节点保持原 op；后续阶段若依赖该标记则行为不一致，且与 `start >= original_length` 这一「按源码位置」的判据本意不符。
- 最小修复：改用与 `KrtCompileReleaseAstGraph` 同构的全图 BFS 标记 `start >= original_length` 的节点。
- 备注：疑似——当前 `KrtPrintRuntimeSource()` 全是顶层函数，暂不触发；但判据本身支持嵌套。

##### [低] 88-20｜runtime_build_keys 释放侧硬编码 4096，与分配点隐式耦合
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:45-53`（分配点 `SemanticRuntime.krt:1010`）
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：`SemanticRuntime.krt:1010` 的 `new KrtNativeRuntimeKey[4096]` 大小被改动，而释放侧未同步。
- 后果：释放循环固定 `bucket < 4096`——分配变大则漏释放（内存泄漏），分配变小则越界读（未定义行为）。
- 最小修复：把容量存进 module 字段，释放时读该字段；或提为共享常量并加断言。
- 备注：疑似——当前两处都是 4096，无实际错误；但跨文件隐式契约无任何保护。

##### [中] 88-21｜分配器只增不减，旧 region 从不回收
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:19-32`
- 类别：10 资源与生命周期
- 触发条件：进程内反复申请字符串/缓冲（路径、诊断、源码副本、列目录结果都走 `KrtDriverAlloc`）。
- 后果：池满即 `mmap` 新 region，旧 region 从不 `munmap`，所有 `KrtDriver*` 字符串永久驻留；进程生命周期内内存单调上涨且无上限。
- 最小修复：引入可回收块分配器/分代 arena，或在编译单元边界重置（重置前确保无悬垂引用）。
- 备注：`CompilerPipeline.krt:450-453` 每次需要 print 的编译还会额外复制整份源码，进一步放大增长。

##### [中] 88-22｜子进程环境被清空，与 C 版 system() 语义分叉
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:513-518`（对照 `Re.KrtC/src/Compiler/Platform/PlatformAbstraction.c:119-120` 使用 `system()`）
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：`KrtDriverRun` 执行 linker/外部工具（`BuildSystem.krt:101` 调 ArkLink）。
- 后果：`environment[0] = 0` 使 `execve` 的 envp 为空数组，子进程丢失 PATH/HOME/TMPDIR/LD_* 等；C 版经 `system()` 继承父环境。ArkLink 若再调外部工具或读配置会失败或行为不同，且失败统一报 127，根因被掩盖。
- 最小修复：构造并传递 `environ`（或至少把当前进程环境导出为 `K=v` 数组）。
- 备注：`argv[0]` 直接置为 executable 路径，也与 shell 习惯不同，排障时易混淆。

##### [中] 88-23｜目录项插入排序未检查 new 与空值，级联空指针
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:313-319`
- 类别：8 未检查前置条件
- 触发条件：`new KrtDriverPath()` 返回 null；或 `KrtDriverJoinPath`/`KrtDriverCopyString` 因分配失败返回 0。
- 后果：`item.value = ...` 空指针崩溃；即使 item 非空，`item.value = 0` 会被传入无 null 检查的 `KrtDriverStringCompare`（56-62 行）解引用地址 0。两种情况都是编译器崩溃而非报错。
- 最小修复：检查 `new` 与 `value`；给 `KrtDriverStringCompare`/`KrtDriverStringLength` 增加 null 保护。
- 备注：该函数被 `KrtDriverListFiles`/`ListDirectories`/`ListKrtFiles`/`RemoveTree` 与 `Project.krt:105/120/514` 共用。

##### [中] 88-24｜中间目录链在转换后从不释放
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:330-346`（以及 370-372）
- 类别：10 资源与生命周期
- 触发条件：任何 `KrtDriverListFiles`/`KrtDriverListDirectories`/`KrtDriverListKrtFiles`/`KrtDriverRemoveTree` 调用。
- 后果：`KrtDriverListEntries` 返回的 `KrtDriverPath` 链（堆对象）在转换成 files/directories 后从不 `delete`；调用方 `Project.krt:105/120/514` 也直接丢弃返回值。每次列目录泄漏一整条链。
- 最小修复：转换完成后遍历释放 `entries`。
- 备注：全仓库多处同类泄漏，属系统性遗漏而非单点疏忽。

##### [中] 88-25｜KrtDriverGetEnv 拷贝分配未检查，失败即写空指针
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:457-462`
- 类别：8 未检查前置条件
- 触发条件：`KrtDriverAlloc(buffer.length + 2)` 返回 0（mmap 失败或超过 64MB 上限）。
- 后果：`target[index] = source[index]` 向地址 0 写入，崩溃；且 455 行已把 `KrtDriverEnvironment` 指向 `buffer.data`，失败后全局状态半初始化。
- 最小修复：检查 `copy`，失败时保持 `KrtDriverEnvironment = 0` 并返回空串。
- 备注：456 行注释「两个终止符使遍历独立于大小」只覆盖了正常路径，未覆盖分配失败。

##### [中] 88-26｜RemoveTree 泄漏目录链、递归无界、列目录失败被吞
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:368-373`
- 类别：10 资源与生命周期 / 8 未检查前置条件 / 1 静默失败
- 触发条件：深层目录树；或目录无权限导致 `KrtDriverListEntries` 返回 null。
- 后果：`entries` 链泄漏（同 88-24）；递归深度无上限，深树可爆栈；列目录失败时 `entries == null` 被当作空目录，继续 `rmdir`，错误被静默吞掉。
- 最小修复：释放 `entries`；改用显式栈迭代；列目录失败时直接返回 false。
- 备注：用于 `BuildSystem.krt:108` 清理临时目录，深树场景少，但静默失败会掩盖清理不干净。

##### [中] 88-27｜仓库定位失败静默回退 CWD
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:488-499`
- 类别：11 配置漂移 / 1 静默失败
- 触发条件：`KAIROTE_ROOT` 未设置或无效，且从可执行文件目录向上 12 层都找不到同时含 `SelfHost` 与 `libs` 的目录；或 `KrtDriverCurrentDirectory` 失败。
- 后果：返回 CWD（甚至 0），`Driver.krt:392-394` 据此拼出错误的 `library_root` 与 `linker` 路径，直到链接阶段才以 E_LINK 等形式失败，根因（仓库定位失败）完全不报告。
- 最小修复：定位失败时返回 0 并由调用方报 E_CONFIG，不要回退 CWD。
- 备注：`KAIROTE_ROOT` 存在时经 `KrtDriverAbsolutePath`，符号链接循环等失败同样只返回 0（241-268 行）。

##### [低] 88-28｜ReadFile 未检查 buffer，且上限「先读后拒」
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:143-169`
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：`buffer == null`（`CompilerPipeline.krt:565`、`KrtDriverArguments:436`、`KrtDriverGetEnv:453` 的 `new` 均未检查）；或文件达到 32MB。
- 后果：143-144 行对 null 解引用；32MB 路径在已读满 32MB 后才失败，`data` 与已读字节全部丢弃并滞留池中。
- 最小修复：函数入口检查 `buffer`；把上限上提给调用方，或先 `stat` 再读。
- 备注：与 88-21 的池增长叠加，属「读得越多、留得越多」。

##### [低] 88-29｜字符串原语无 null / 非 NUL 终止保护
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:34-62`
- 类别：8 未检查前置条件
- 触发条件：传入 0 或非 NUL 终止的指针（`KrtDriverStringCompare` 无 null 检查，`KrtDriverStringLength` 无长度上限）。
- 后果：空指针崩溃或越界读；调用链上多处把可能为 0 的返回值直接传入（见 88-23），一旦上游分配失败即连锁崩溃。
- 最小修复：原语对 0 返回安全值（长度 0、按空串比较），并在注释中固化 NUL 终止前置条件。
- 备注：疑似——当前多数调用方保证非空，但 88-23 的级联会破坏该前提。

##### [低] 88-30｜KrtDriverArguments 泄漏 buffer 且产生空参数项
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:435-449`
- 类别：10 资源与生命周期 / 9 错误处理不一致
- 触发条件：任何进程启动；或 `KrtDriverCopyString` 分配失败。
- 后果：`buffer` 从不 `delete`；`CopyString` 失败时 `value = 0` 进入参数链，后续 `KrtDriverWord`/`KrtDriverStringEqual` 对 0 的语义不统一（`StringEqual(0,0)=true`、`EndsWith(0,"")=false`）。
- 最小修复：返回前释放 `buffer`（保留 data 指针链）；对 `CopyString` 失败显式报错。
- 备注：每次进程启动调用一次，泄漏量小但确定存在。

##### [低] 88-31｜临时路径全局索引非原子
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:16、387-392`（使用点 `BuildSystem.krt:50-55、417-423`）
- 类别：7 共享可变状态 / 8 未检查前置条件
- 触发条件：同一进程内并发调用 `KrtDriverTemporaryPath`（当前并行靠 fork，进程内暂不并发，但 API 无任何保护或说明）。
- 后果：两个调用拿到同一 index → 同一路径 → `O_EXCL` 重试 128 次后失败，或互相覆盖产物。
- 最小修复：改用原子自增，或把 pid+计数器并入单一来源，并注释并发约束。
- 备注：疑似——`BuildSystem.krt:745-747` 的注释表明当前并行通过 fork 实现，进程内无并发。

##### [低] 88-32｜KrtDriverCopyFile 为死代码
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:412-414`
- 类别：5 死代码
- 触发条件：全仓库 grep 无任何调用点（仅 `KrtDriverPublish` 被 `BuildSystem.krt:104/105/106` 与 `CompilerPipeline.krt` 使用）。
- 后果：维护者可能误以为存在「复制文件」语义而依赖它，实际与 `Publish` 完全等价（多一次临时文件 + rename），语义重复且易误用。
- 最小修复：删除；或实现真正不同的语义并补齐调用点。
- 备注：与 `KrtDriverPublish` 仅差一个函数名。

##### [低] 88-33｜平台常量硬编码，与「平台抽象」命名脱节
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:190`（使用点 `Project.krt:260、989`）
- 类别：13 注释与实现脱节 / 11 配置漂移
- 触发条件：把编译器移植到非 Linux，或做交叉编译。
- 后果：`KrtDriverPlatform()` 恒返回 `"linux"`，`Project.krt` 用它拼 `bin/linux/...` 与构建脚本变量 `os`/`platform`；文件头注释也自述「Linux x86-64 services」，命名与用途（平台抽象）不符。
- 最小修复：按实际编译目标返回；或改名为 `KrtDriverLinuxPlatform` 并去掉「抽象」暗示。
- 备注：C 版有 `KrtPlatformType` 枚举与多平台实现，SelfHost 版已退化。

##### [低] 88-34｜空串后缀语义与 StartsWith 不一致
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:101-105`（对照 91-99）
- 类别：9 错误处理不一致 / 3 语义分叉
- 触发条件：`KrtDriverEndsWith(0, "")`，或 `KrtDriverEndsWith(text, "")`。
- 后果：`EndsWith(text,"")` 走 `KrtDriverStringEqual(text+length, "")` 返回 true；但 `text == 0` 时 `length=0、count=0`，`text+0 == 0` 与非 0 空串比较得 false，与 `StartsWith(0,"")==true` 矛盾。
- 最小修复：显式处理 `count == 0` 返回 true。
- 备注：影响面小，但属契约不一致，易被上层误用。

##### [低] 88-35｜getcwd 失败时把相对路径误规范化为 `/rel`
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:209-238`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`KrtDriverCurrentDirectory` 返回 0（如 CWD 被删除导致 getcwd 失败），且传入相对路径。
- 后果：`KrtDriverJoinPath(0, path)` 返回相对路径本身，随后 219 行无条件写 `target[0] = '/'`，把 `"foo"` 变成 `"/foo"`，静默制造错误的绝对路径。
- 最小修复：`current == 0` 时直接返回 0 并报错，不再继续规范化。
- 备注：`KrtDriverAbsolutePath:241-242` 对 0 有检查，但错误此时已被制造。

##### [低] 88-36｜Stem 对点前缀文件名的扩展名判定不一致
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:288-295`
- 类别：3 语义分叉
- 触发条件：basename 以 `.` 开头且仅有一个点（如 `.krt`、`.hidden`）。
- 后果：`while (index > 0)` 不检查 `index == 0` 处的点，`end` 保持 `length`，stem 等于原名；而 `"a.krt"` 会得到 `"a"`。与 `KrtDriverListKrtFiles`（按 `.krt` 过滤）组合时行为不统一。
- 最小修复：明确语义（点前缀名视为无扩展名则加注释，否则改为 `index >= 0`）。
- 备注：低风险，但属命名辅助函数的契约不清。

##### [低] 88-37｜KrtDriverPrint 忽略写失败
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:133`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：stdout 写失败（EPIPE/ENOSPC，例如输出管道被对端关闭）。
- 后果：完全忽略 `KrtDriverWriteAll` 的返回值，usage/version 等输出丢失且无任何提示；`KrtDriverError`（134-141）同样忽略写失败。
- 最小修复：至少记录失败，或改为返回 bool 供调用方判断。
- 备注：与 88-8 同属错误处理不一致，但本条聚焦「写失败被吞」。

##### [低] 88-38｜可执行路径失败静默传播为链接错误
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:480-486`（使用点 `Driver.krt:389`）
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`readlink("/proc/self/exe")` 失败（非 Linux 环境或权限受限）。
- 后果：返回 0，`Driver.krt:389` 的 `options.compiler = 0` 无校验，后续 `KrtDriverRun(0, ...)` → `execve(NULL)` → EFAULT → 127 → 报 "native linker failed"，真实原因被掩盖。
- 最小修复：返回 0 时由调用方报 E_CONFIG/E_DRIVER，而不是继续往下走。
- 备注：文件自述 Linux x86-64，但无任何运行期平台校验。

##### [低] 88-39｜关键路径缺自动化测试
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:564-575`、`SelfHost/Compiler/Platform/PlatformAbstraction.krt:416-433、501-528`（`Test/` 下无 `KrtCompileNativeSourceMode`/`KrtCompilePath`/`KrtDriverPublish`/`KrtDriverRun` 引用）
- 类别：12 测试不足
- 触发条件：回归验证或改动上述函数时。
- 后果：库模式/产物目标（2/4）、E_VM/E_EMIT/E_LIBRARY_SYMBOL、诊断 flush/恢复、原子发布、子进程退出码解码均无自动化覆盖；`Test/SelfHost/test_contract.krt` 只覆盖紧凑 `KrtCompileSource` 系列。
- 最小修复：为 `KrtCompilePath`（含 `library.mode` 分支）与 `KrtDriverPublish`/`KrtDriverRun` 增加契约测试。
- 备注：本条为覆盖性说明，具体缺陷见 88-9、88-13、88-15、88-22。

##### [低] 88-40｜两套诊断写出实现，格式与分流各有一套
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:540-561`（对照 `PlatformAbstraction.krt:134-141`）
- 类别：13 注释与实现脱节 / 9 错误处理不一致
- 触发条件：两处写出函数在不同调用路径被触发。
- 后果：`KrtDiagnosticWrite` 输出 `code at byte N`（十进制位置），`KrtDriverError` 输出 `code: detail`；同一 E_* 编码存在两种格式，且只有前者做 `position < 0` 钳制（552 行）与 `ActiveBundle` 分流（543 行）。
- 最小修复：统一到单一出口（code/detail/position 三参数）。
- 备注：与 88-8 相关但侧重不同（88-8 关注上报通路，本条关注输出格式与边界处理），故单列。

### 第八十九批：SelfHost Optimizer/CompilerState + stdlib Collections/Generic + stdlib Runtime/Sys

#### 覆盖范围

- `SelfHost/Compiler/Middle/Ir/Optimizer.krt`（381 行，全量读完）
- `SelfHost/Compiler/Frontend/CompilerState.krt`（328 行，全量读完；第 317 行打印运行时字符串被读取工具截断到 2000 字符，仅审可见部分）
- `Re.KrtC/stdlib/Collections/Generic.krt`（13 行，全量读完）——纯接口声明，**无问题条目**
- `Re.KrtC/stdlib/Runtime/Sys.krt`（2 行，全量读完）

交叉核对仅限 grep 特定符号：`KrtNativeOp` 枚举（`SelfHost/Compiler/Middle/Ir/Ir.krt:1555-1565`）、`KrtNativeWidthCode`（`Ir.krt:1788-1791`）、`KrtNativeValueWidth`（`Ir.krt:1806-1803`）、`KrtNativeInlineCandidate` 调用点、`KrtErrorReportFlush/HasErrors/Add` 调用点、`KrtCurrentDiagnostics` 使用点、`native_error` 赋值点、`KrtNativeBindingDiagnostic` 调用点、`System.Sys`/`Runtime.Sys` 引用点、`ssa.uses`/`semantic_types` 读取点。


##### [高] 89-1｜`KrtNativeInlineCandidate` 先解引用后校验 `callee` 范围
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:185-186`
- 类别：8 未检查前置条件
- 触发条件：`callee < 0` 或 `callee >= module.function_count`。
- 后果：第 185 行先读 `module.function_frame_bytes[callee]`、`module.aggregate_return_sizes[callee]`，第 186 行才做范围检查；越界时读到垃圾值或崩溃，并可能据此做出错误的内联决策。
- 最小修复：把第 186 行的范围判断移到第 185 行之前，越界直接 `return false`。
- 备注：当前唯一调用点 `Optimizer.krt:214` 以 `function_index < module.function_count` 循环，暂不可达；但函数是公共 API 且自身无前置检查，属契约破坏。

##### [高] 89-2｜`KrtCurrentDiagnostics` 进程级全局可变状态
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:266`（使用点 `:284`、`:293`）
- 类别：7 共享可变状态
- 触发条件：嵌套编译/探针（`SelfHost/Compiler/Frontend/Semantic/Types.krt:1198-1233` 保存-替换-删除报告）、并行编译、或 `KrtCurrentDiagnostics == null` 时调用 `KrtDiagnosticMessage`。
- 后果：诊断被写入错误的对象；`KrtDiagnosticMessage`（:284）在全局为 null 时绕过报告直接输出，跳过 limit 与去重；`KrtErrorReportHasErrors`（:293）检查的是全局而非调用方传入的报告，探针替换期间会检查错报告；无锁，线程不安全。
- 最小修复：把 `report` 作为参数沿调用链传递，删除 `static KrtCurrentDiagnostics`。
- 备注：`CompilerPipeline.krt:425-430`、`Types.krt:1198-1233` 已用手工保存/恢复规避，说明该全局已被认定为必要之恶。

##### [高] 89-3｜`KrtErrorReportFlush` 不清空链表且无 null 检查
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:288-291`
- 类别：6 契约破坏 / 9 错误处理不一致
- 触发条件：重复 flush 同一 report；或 flush 之后调用 `KrtErrorReportHasErrors()`；或 `report == null`。
- 后果：`head/tail/count` 均未重置，重复 flush 会重复打印全部诊断；flush 后 `count > 0` 使 `KrtErrorReportHasErrors()`（:293）持续为真，后续阶段被误判为失败；`report == null` 时第 289 行直接空指针解引用。
- 最小修复：flush 后重置 `head=null; tail=null; count=0;`，并在函数入口加 `if (report == null) { return; }`。
- 备注：`CompilerPipeline.krt:429` 在阶段失败时 flush，随后 `:494` 用 `KrtErrorReportHasErrors()` 判定，二者组合放大该缺陷。

##### [中] 89-4｜`starts` 数组硬编码 16384，优化器内无局部上界校验
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:139`、`:217`
- 类别：8 未检查前置条件 / 11 配置漂移
- 触发条件：`module.function_count > 16384`。
- 后果：`starts[function_index] = count`（:143、:225）越界写，破坏堆上相邻数组。
- 最小修复：按 `module.function_count` 分配 `starts`，并在 `KrtNativeOptimize` 入口拒绝超限模块。
- 备注：16384 是 `Ir.krt:3203` 的模块上限，但 `KrtNativeOptimize`（:286-287）只校验 `module == null` 与 level，未复用该上限；同一 magic number 在 `Ir.krt:1582-1592` 与本文件重复。

##### [中] 89-5｜容量计算 `old_count * 2 + 1024` / `old_count * 4 + 1024` 整数溢出
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:137`、`:215`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：`old_count` 足够大（约 > 2^30 或 > 2^29）使乘加溢出 int32。
- 后果：`capacity` 变为负数或过小，`new int32[capacity]` 抛异常/分配失败；或容量不足后在 :153/:159/:237/:265 返回 false，但此时 `module` 尚未替换（替换在 :171/:281），调用方只能拿到通用 `E_OPTIMIZE`。
- 最小修复：用 int64 计算并先校验 `old_count` 上限，再钳制到 16777216。
- 备注：与 89-4 同源于优化器入口缺模块校验。

##### [中] 89-6｜`KrtNativeInlineHelpers` 重定位无边界检查，且未做 `KrtNativeOptimizationTargets` 校验
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:273-280`（对照 `KrtNativeTailCalls:132`）
- 类别：8 未检查前置条件 / 4 阶段不对齐
- 触发条件：`module` 中存在 op 17/18/50 且 `args`/`extra` 指向 `[0, old_count)` 之外。
- 后果：`mapping[(int32)args[index]]` 越界读；`TailCalls` 先调用 `KrtNativeOptimizationTargets` 校验目标，`InlineHelpers` 完全跳过该阶段，两个优化阶段前置条件不一致。
- 最小修复：在 `KrtNativeInlineHelpers` 开头调用/复用 `KrtNativeOptimizationTargets`，并在重定位时检查映射下标 `<= old_count`。
- 备注：正常路径由 `Ir.krt:3203` 的模块校验兜底，但该校验不含分支目标范围。

##### [中] 89-7｜`KrtErrorReportAdd` 达到 `limit` 时静默丢弃诊断
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:269`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`report.count >= report.limit`（默认 128，见 `Types.krt:1199`）。
- 后果：第 129 条及以后的错误被直接丢弃，用户看到的诊断集不完整，且无任何「已截断」提示；`KrtDriverLastErrorCode` 仍被设置，调用方无法区分「已记录」与「被丢弃」。
- 最小修复：增加 `dropped` 计数并在摘要中报告，或超限时报内部诊断。
- 备注：`limit` 为 0 时所有诊断都会被静默吞掉。

##### [中] 89-8｜`KrtNativeBindingDiagnostic` 把 `native_error == 0` 与未知码统一降级为 `E_LOWER`
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:312`
- 类别：2 诊断缺失
- 触发条件：`Types.krt:910-914` 对未绑定函数先 `module.native_error = 0` 再调用本函数；或 binding 写入 17 及以上的新错误码。
- 后果：未绑定函数被报成通用 `E_LOWER`，丢失具体原因；后续新增错误码若忘记在此映射，同样静默降级，排查困难。
- 最小修复：为 0（未绑定）定义专属诊断码，未知码走显式 `E_INTERNAL`/`E_UNKNOWN` 而非 `E_LOWER`。
- 备注：已核对 1-16 的码长与字符串长度一致，无错配。

##### [中] 89-9｜`KrtNativeModule.data` 与 `data_storage` 双份所有权，注释与实现矛盾
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:199-209`
- 类别：10 资源与生命周期 / 13 注释与实现脱节
- 触发条件：后端按注释「Backends may use this field instead of data」在 `int64 data` 与 `byte[] data_storage` 之间选择。
- 后果：同一数据存储有两条拥有路径，`data` 与 `data_storage` 可漂移；注释称「不需要 unsafe 指针字段」却仍保留 `int64 data` 这一指针尺寸字段，`data_length` 与 `data_storage` 长度也无一致性约束。
- 最小修复：统一为 `data_storage`（或明确 `data` 只是别名并在写入处同步），并修正注释。
- 备注：`CompilerPipeline.krt` 中多处 `delete` 数组，双份所有权还带来重复释放/漏释风险。

##### [中] 89-10｜打印运行时 `__KrtPrintLength` 无界扫描、`__KrtPrintAllocate` 未校验 size
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:317`（`KrtPrintRuntimeSource` 字符串常量内）
- 类别：8 未检查前置条件
- 触发条件：`text` 指向非 NUL 结尾的内存（`while(data[n]!=0){n++;}` 无长度上界）；`size` 为负或超大时 `new byte[size]`。
- 后果：越界读直到撞上随机零字节，或分配异常/巨量分配；该运行时代码由编译器在检测到 `print` 时注入，影响所有使用 print 的程序。
- 最小修复：`__KrtPrintLength` 增加上界参数或由调用方保证 NUL；`__KrtPrintAllocate` 校验 `size >= 0` 并设上限。
- 备注：该行被读取工具截断，仅审可见的 `__KrtPrintLength`/`__KrtPrintAllocate`/`__KrtPrintConcat`/`__KrtPrintUnsigned` 等部分；截断部分未审。

##### [低] 89-11｜`KrtNativeOptimizationTargets` 未校验 `module.count` 即分配 `new bool[module.count + 1]`
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:56`
- 类别：8 未检查前置条件
- 触发条件：`module.count < 0`（或 `count + 1` 溢出）。
- 后果：`new bool[负数]` 抛异常，优化阶段崩溃而非返回 false。
- 最小修复：入口校验 `module != null && module.count >= 0 && module.count <= 16777216`。
- 备注：`KrtNativeOptimize:287` 只校验 `module == null` 与 level。

##### [低] 89-12｜`KrtNativeFoldConstants` 中两处 `!targets[...]` 为恒真死条件
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:84`、`:97`
- 类别：5 死代码
- 触发条件：任何执行到 :84/:97 的路径。
- 后果：:81 在 `targets[index]` 时已把 `previous` 置 -1，:107 只在非目标处置 `previous`，因此 `targets[index]`、`targets[previous]` 恒为假，两个取反恒真；读者会误以为已防护跨跳转目标的常量折叠。
- 最小修复：删除这两个条件，或改为断言 `!targets[index] && !targets[previous]` 以显式表达前提。
- 备注：不影响当前正确性，属误导性死代码。

##### [低] 89-13｜SSA 常量折叠 16 轮上限后静默返回成功
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:298`
- 类别：1 静默失败
- 触发条件：常量传播/死值消除需超过 16 轮才收敛（深链式表达式）。
- 后果：循环退出后 `KrtSsaFoldConstants` 仍 `return true`（:336），优化不完整且无任何标志或诊断，调用方 `KrtSsaOptimize` 认为成功。
- 最小修复：达到轮次上限时返回 false（或输出可查询的「未收敛」状态）。
- 备注：属有界不动点迭代的常见折中，但缺少可观测性。

##### [低] 89-14｜`KrtSsaRemoveDeadValues` 清理元数据不一致
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:364`
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：任何死值被移除。
- 后果：只清 `ops/results/operand_counts/types/widths`，未清 `semantic_types`/`semantic_widths`/`operand_starts`/`phi_predecessors`/`value_blocks`；死值残留语义元数据，后续按 `semantic_*` 判断的逻辑（如 `Ir.krt:3630`）可能读到陈旧值。
- 最小修复：移除时一并清零 `semantic_types`/`semantic_widths`，或重建整张表。
- 备注：`Ir.krt:3630` 以 `ssa.results[value]` 为前置条件，当前恰好绕过死值，故实际影响有限。

##### [低] 89-15｜`KrtPrintRuntimeRequired` 泄漏 lexer/token，且初始化失败静默返回 false
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:320-327`
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：每次编译调用；`KrtLexerInit` 失败时。
- 后果：`new KrtLexer()`/`new KrtToken()` 全程未 `delete`（对照 `CompilerPipeline.krt:346,392-393` 均显式 delete），每次编译泄漏两个对象；初始化失败时返回 false，调用方据此认为「不需要 print 运行时」，可能漏注入打印运行时。
- 最小修复：函数返回前 `delete token; delete lexer;`；用 out 参数区分「无 print」与「检测失败」。
- 备注：编译器进程长驻时泄漏会累积。

##### [低] 89-16｜`KrtErrorReportAdd` 将负 `position` 静默钳为 0
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:270`
- 类别：1 静默失败
- 触发条件：调用方传入 `position < 0`（无位置上下文时常见）。
- 后果：诊断被定位到文件偏移 0，掩盖真实来源，且与去重逻辑（:273 按 position 比对）叠加后，多个不同来源的无位置诊断会被合并丢弃。
- 最小修复：保留原值或单独约定「未知位置」哨兵，不要复用 0。
- 备注：与 89-17 的去重逻辑相互作用。

##### [低] 89-17｜诊断去重仅按 `position + code`，不同消息被静默合并
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:273`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：同一 position、同一 code 但 `message` 不同（例如同一错误码在不同上下文复用时）。
- 后果：第二条及以后的诊断被直接 `return` 丢弃，用户丢失信息；比较只取 `code` 指针且忽略 `code_length`，长度不一致的同前缀码也可能误判为重复。
- 最小修复：去重键加入 `message`（或 `code_length`），或仅对完全相同的记录去重。
- 备注：与 89-16 叠加会放大丢失。

##### [低] 89-18｜优化器各阶段失败统一返回 false，调用方无法区分具体阶段
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:286-292`
- 类别：2 诊断缺失 / 4 阶段不对齐
- 触发条件：`KrtNativeFoldConstants` / `KrtNativeTailCalls` / `KrtNativeInlineHelpers` 任一返回 false。
- 后果：`KrtNativeOptimize` 只把 false 透传给 `CompilerPipeline.krt:520`，后者统一报 `E_OPTIMIZE`，无法判断是常量折叠、尾调用还是内联失败，也无法区分「目标越界」「容量不足」「模块为 null」。
- 最小修复：返回阶段码或写入模块错误字段，由调用方带阶段信息报错。
- 备注：`KrtSsaOptimize` 同样只返回 false（:377），但 `E_SSA` 由调用方给出。

##### [低] 89-19｜`Runtime/Sys.krt` 只有一条 `using`，不构成有效兼容层且命名空间与实现对不上
- 位置：`Re.KrtC/stdlib/Runtime/Sys.krt:2`
- 类别：5 死代码 / 11 配置漂移 / 13 注释与实现脱节
- 触发条件：任何按路径/命名空间引用该文件、期望获得 `Sys` 符号的编译。
- 后果：文件不含 `namespace`、不含任何 `public` 声明，只有一条 `using System.Sys;`；`using` 只导入不重导出，下游不会因此获得 `Sys`。真正的 `Sys` 类在 `Re.KrtC/stdlib/Sys.krt:2` 且无 `namespace` 声明（全局命名空间），而库中同时存在 `using System.Sys;` 与 `using System.Runtime.Sys;`（`Test/StandardLibrary/test_imports.py:120`）两种引用路径，注释「runtime primitives have one implementation」无任何强制力。
- 最小修复：给 `Sys` 类补 `namespace System;` 并让本文件成为真正的命名空间别名/转发，或删除该 shim 并统一引用路径。
- 备注：`libs/System/Runtime/Sys.krt` 存在同名同内容 shim，属重复源；本文件是否被构建纳入未在本次允许读取范围内核实。

### 第九十批：src/Runtime/Krt/{Math,Memory,Runtime,Sys}.krt

#### 覆盖范围

- 已用 read 工具完整读取（全量）：
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Math.krt`（126 行）
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Memory.krt`（49 行）
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Runtime.krt`（4 行，仅注释）
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Sys.krt`（88 行）
- **比对情况说明**：本环境 bash 不可用（无沙箱后端，`workspace-write` 被拒、`danger-full-access` 升级被用户拒绝），因此任务要求的 `diff -q` **未能执行**。仅用 glob 列出 `Re.KrtC/stdlib/` 目录，确认存在同名文件 `Math.krt`、`Memory.krt`、`Sys.krt`，**不存在** `Runtime.krt`；按任务约束未读取这些 stdlib 文件内容，故语义分叉仅能给出「并存风险」级结论，无法逐行核对。
- 本次未通读任何其他源文件。


##### [致命] 90-1｜Abs(int32) 对 int32.MinValue 取负溢出，静默返回原负数
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:13`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`MathOps.Abs(int32.MinValue)`（即 -2147483648）。
- 后果：`-value` 溢出，结果仍为 -2147483648；调用方拿到「负的绝对值」，后续数组索引/尺寸计算全部错位，且无任何报错。
- 最小修复：入口判 `value == int32.MinValue` 时抛溢出异常或返回 int64；至少在注释中明确该输入未定义并加断言。
- 备注：与 90-2 是同源缺陷的两个重载，因签名不同单独记录。

##### [致命] 90-2｜InternalInt32ToString 对 int32.MinValue 生成字符串 `"-"`，静默错误
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:63-64`（叠加 74-80）
- 类别：8 未检查前置条件（溢出）/ 1 静默失败 / 2 诊断缺失
- 触发条件：`Sys.InternalInt32ToString(int32.MinValue)`。
- 后果：`number = 0 - number` 溢出后仍为负，`while (number > 0)` 一次都不执行，只写入 `'-'` 后返回 `buffer + 10`，得到字符串 `"-"`；任何打印/日志/数值解析都得到错误结果且无异常。
- 最小修复：改用 `int64 number = 0 - (int64)value`（或先转 int64 再取负），并在函数入口对 MinValue 单独处理。
- 备注：缓冲区 12 字节恰好容纳 `-2147483648`，说明作者本意支持该值，属实现缺陷而非设计取舍。

##### [致命] 90-3｜Memory.Allocate 把 mmap 错误码当作指针返回，失败静默
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:16`
- 类别：1 静默失败 / 8 未检查前置条件 / 9 错误处理不一致
- 触发条件：`syscall` 返回负数（mmap 失败，如 ENOMEM/EINVAL），`(void*)(-errno)` 被当作合法指针返回。
- 后果：调用方按「非 NULL 即成功」判断，拿到形如 `(void*)-22` 的伪指针；后续 `Memory.Copy/Set` 写入即野指针写，进程崩溃或静默损坏。
- 最小修复：`int64 r = Sys.syscall(...); if (r < 0) return null;`，与 `Sys.InternalInt32ToString:47` 的判负写法保持一致。
- 备注：同文件 `Sys.krt:47` 已用 `< 0` 判失败，两处错误处理不一致（类别 9）。

##### [高] 90-4｜Abs(int64) 对 int64.MinValue 取负溢出
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:20`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`MathOps.Abs(int64.MinValue)`。
- 后果：同 90-1，结果仍为负数；该重载被 `GCD`/`LCM` 内部调用（Math.krt:109-110、124），错误会向数论函数传播。
- 最小修复：入口特判 MinValue；或在文档中声明仅支持 `value > MinValue` 并加断言。
- 备注：与 90-1 同源，因签名与调用链不同而独立记录。

##### [高] 90-5｜InternalStringLength 无界扫描且对 null 静默返回 0
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:28-42`
- 类别：8 未检查前置条件（越界）/ 1 静默失败 / 6 契约破坏（编码约定）
- 触发条件：传入的 string 底层 `char[]` 未以 `'\0'` 结尾（或长度字段与内容不一致）时，`while (chars[(int32)length] != '\0')` 越界读；传入 null 时直接返回 0。
- 后果：越界读可致段错误或读到垃圾值；null 静默变 0 会把「未初始化」与「空串」混为一谈，掩盖上游缺陷。另外 `length` 为 int64 但下标强转 int32，超过 2^31 后下标回绕。
- 最小修复：使用字符串自带长度字段而非扫描终止符；null 单独抛异常或返回 -1；下标统一 int64 或先做范围校验。
- 备注：依赖「string 即 char[] 且 NUL 结尾」这一未文档化的编码约定，属类别 6。

##### [高] 90-6｜InternalInt32ToString 每次调用泄漏 12 字节 mmap，从不 munmap
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:46`
- 类别：10 资源与生命周期
- 触发条件：每次调用 `InternalInt32ToString`（打印整数、日志、错误信息都会触发）。
- 后果：mmap 的 12 字节永不回收，长时间运行/循环打印时地址空间持续增长，最终 mmap 失败；而失败分支只返回 null（见 90-14）。
- 最小修复：改用栈上 `char[]`/固定缓冲，或让调用方传入缓冲；若必须 mmap，则提供配套释放或改用 brk 分配器统一管理。
- 备注：文件头注释自称「纯 .krt 实现，不依赖 C 运行时库」，但每次字符串化都依赖内核映射，代价被隐藏。

##### [高] 90-7｜syscall 号硬编码 Linux x86_64，跨架构即整体失效
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:11-18`（`SYS_mmap`/`SYS_munmap` 亦被 `Memory.krt:10-11` 复制）
- 类别：6 契约破坏（ABI）/ 11 配置漂移
- 触发条件：在 aarch64（本项目环境为 Android/arm64）或 32 位目标上运行；arm64 的 read/write/mmap/munmap/exit 分别为 63/64/222/215/93。
- 后果：arm64 编号 9 是 `lgetxattr`、11 是 `getxattr`（非 munmap）、12 是 `fgetxattr`（非 brk），`Allocate`/`Free`/`InternalInt32ToString` 全部误调用，轻则 EINVAL 静默失败，重则按错误语义执行（如把内存当 xattr 操作）。
- 最小修复：按目标三元组提供常量表（条件编译或分文件），并在构建期断言架构；至少把「仅 x86_64」写入契约并在不支持时编译报错。
- 备注：`Memory.krt:10-11` 重复定义同一组常量，使修正必须改两处，见 90-19。

##### [高] 90-8｜Memory.Copy 无 NULL/越界/重叠检查
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:26-36`
- 类别：8 未检查前置条件（NULL/越界）/ 7 共享可变状态
- 触发条件：`dest`/`src` 为 NULL 或重叠，或 `length` 超出任一侧实际分配。
- 后果：NULL 直接解引用崩溃；重叠时行为等同于未定义（非 memmove 语义），数据错乱；越界写破坏相邻堆/映射。
- 最小修复：入口校验 `dest != null && src != null && length >= 0`；重叠时改用反向复制或显式要求调用方保证不重叠并写入契约。
- 备注：参数 `length` 为 int32，负值会让循环静默不执行（见 90-18）。

##### [高] 90-9｜Memory.Allocate 未校验 size（0 或负值）
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:14-16`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`Allocate(0)` 或 `Allocate(负数)`。
- 后果：mmap 对 size<=0 返回 EINVAL（-22），经 90-3 变成非空伪指针；调用方误以为分配成功，后续使用即野指针。
- 最小修复：`if (size <= 0) return null;` 并返回明确失败信号。
- 备注：与 90-3 叠加放大，建议一并修复。

##### [高] 90-10｜runtime 层与 stdlib 同名文件并存，语义分叉风险（本次未能比对）
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt`、`Memory.krt`、`Sys.krt` 与 `Re.KrtC/stdlib/` 下同名文件
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：同一符号（如 `Abs`、`Allocate`、`syscall` 封装）在 runtime 与 stdlib 两条路径各有一份实现，编译期按文件列表选择其一。
- 后果：两处实现一旦修正不同步，同一程序在不同构建组合下行为不一致；本次因 bash 不可用未能执行 `diff -q` 与内容比对，无法确认当前是否已分叉。
- 最小修复：明确单一事实来源（runtime 提供底层、stdlib 仅薄封装并直接链接），或补 `diff -q` 回归比对纳入 CI。
- 备注：glob 确认 stdlib 有 `Math/Memory/Sys.krt` 而无 `Runtime.krt`；按任务约束未读取其内容，故不定级为已确认分叉。

##### [中] 90-11｜Pow 对负指数静默返回 1
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:59`
- 类别：1 静默失败 / 8 未检查前置条件 / 2 诊断缺失
- 触发条件：`Pow(baseVal, exp)` 且 `exp < 0`（如 `Pow(2, -1)`）。
- 后果：返回 1，而非报错或 0；调用方无法区分「负指数」与「指数为 0」，数值结果静默错误。
- 最小修复：`if (exp < 0) { /* 抛异常或返回 0 并记录 */ }`，仅保留 `exp == 0` 返回 1。
- 备注：`exp <= 0` 把两种语义合并，属典型静默失败。

##### [中] 90-12｜Pow 乘法溢出静默回绕
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:64`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`Pow(10, 10)` 等结果超出 int32 的输入。
- 后果：`result = result * baseVal` 回绕为任意值（可能为负），无提示；作为「整数幂」基础函数会污染所有调用方。
- 最小修复：使用 checked 乘法并在溢出时抛异常，或改用 int64 中间量后判范围。
- 备注：与 90-1/90-4 同属「整型溢出无检查」家族，但发生点独立。

##### [中] 90-13｜Sqrt 对负数静默返回 0
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:73`
- 类别：8 未检查前置条件 / 1 静默失败 / 2 诊断缺失
- 触发条件：`Sqrt(value)` 且 `value < 0`（如 `Sqrt(-1)`）。
- 后果：返回 0，与 `Sqrt(0)` 无法区分；`IsPrime` 虽先判 `n < 2` 不受影响，但其它调用方会得到错误的 0。
- 最小修复：`if (value < 0) throw/返回 -1`；仅 `value == 0` 返回 0。
- 备注：注释「整数平方根」未声明定义域。

##### [中] 90-14｜Clamp 未校验 min <= max
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:49-54`
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：`Clamp(v, min, max)` 且 `min > max`（如 `Clamp(5, 10, 0)`）。
- 后果：`value < min` 时返回 `min`，结果落在 `[min, max]` 之外，违反「限制值在范围内」的注释契约，且无任何提示。
- 最小修复：入口断言 `min <= max`，或定义并文档化反向区间的行为。
- 备注：注释与实现契约不符，兼具类别 13。

##### [中] 90-15｜InternalInt32ToString 的 mmap 失败仅返回 null，无诊断
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:47-50`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：mmap 失败（返回负 errno）。
- 后果：静默返回 null，调用方既拿不到错误码也拿不到原因；与 90-3 的「返回伪指针」形成两种截然不同的失败表示。
- 最小修复：统一失败约定（始终返回 null 并暴露 errno/诊断信息），并在调用点检查 null。
- 备注：与 90-3 构成同一模块内错误表示不一致。

##### [中] 90-16｜Memory.Free 忽略 munmap 返回值且无 NULL/长度校验
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:20-23`
- 类别：1 静默失败 / 8 未检查前置条件 / 10 资源与生命周期
- 触发条件：`Free(NULL, n)`、长度与分配时不一致、或重复释放。
- 后果：munmap 失败（EFAULT/EINVAL）被丢弃，内存未真正回收；长度不匹配会解除错误区间，破坏其它映射。
- 最小修复：检查 `ptr != null`，校验返回值，并把「分配尺寸必须回传」写入契约（最好由分配器记录尺寸而非要求调用方记忆）。
- 备注：`Allocate` 不返回尺寸句柄，`Free` 却要求尺寸，属接口契约脆弱（类别 6）。

##### [中] 90-17｜Memory.Set 无 NULL 校验，负长度静默无操作
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:39-48`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`Set(NULL, v, n)` 或 `length < 0`。
- 后果：NULL 崩溃；负长度时 `while (i < length)` 直接不执行，返回后调用方以为已初始化，读到未初始化内存。
- 最小修复：入口校验 `dest != null && length >= 0`。
- 备注：与 90-8 同文件同风格缺陷，因函数独立单独记录。

##### [中] 90-18｜Copy/Set 长度用 int32，与 Allocate 的 int64 宽度不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:26`、`39`（对比 `14`）
- 类别：6 契约破坏 / 9 错误处理不一致
- 触发条件：对大于 2GiB 的块调用 `Copy`/`Set`，或把 `Allocate` 的 int64 尺寸直接传入。
- 后果：尺寸被截断为 int32，复制/初始化只覆盖前一部分，剩余区域保持原样，静默数据不完整。
- 最小修复：统一为 int64，或在入口断言 `length` 可表示且与分配尺寸一致。
- 备注：同一模块内宽度不统一，是典型契约破坏。

##### [中] 90-19｜SYS_mmap/SYS_munmap 常量在 Sys 与 Memory 重复定义
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:15-16` 与 `Re.KrtC/src/Runtime/Krt/Memory.krt:10-11`
- 类别：11 配置漂移 / 5 死代码
- 触发条件：需要按架构修正 syscall 号（见 90-7）时只改其中一处。
- 后果：两处常量各自演化，出现「Sys 用新号、Memory 用旧号」的分裂，分配与释放走不同 ABI，故障极难定位。
- 最小修复：删除 `Memory` 中的重复常量，统一引用 `Sys.SYS_mmap`/`Sys.SYS_munmap`。
- 备注：`Memory.Allocate` 已跨文件调用 `Sys.syscall`，复用常量无额外成本。

##### [中] 90-20｜Runtime.krt 无任何实现，仅 4 行注释，属死文件
- 位置：`Re.KrtC/src/Runtime/Krt/Runtime.krt:1-4`
- 类别：5 死代码 / 12 测试不足
- 触发条件：编译时按文件列表把它当编译单元加入。
- 后果：空文件无 namespace、无类型声明，对产物零贡献；若构建脚本依赖目录通配或该文件存在性，纯属噪音，并误导后来者以为存在「运行时示例」实现。
- 最小修复：删除该文件，或补成可编译的真实示例/入口（含 `namespace System.Runtime;`）。
- 备注：glob 确认 stdlib 下无同名 `Runtime.krt`，无对照实现。

##### [中] 90-21｜Runtime.krt 注释中的编译命令引用本目录不存在的文件
- 位置：`Re.KrtC/src/Runtime/Krt/Runtime.krt:3`
- 类别：13 注释与实现脱节 / 4 阶段不对齐
- 触发条件：按注释执行 `krtc build main.krt Sys.krt Memory.krt String.krt Math.krt Console.krt target exe`。
- 后果：`String.krt`、`Console.krt` 不在 `src/Runtime/Krt/`（本批目录仅 Math/Memory/Runtime/Sys），且 `target exe` 写法可疑；照抄命令必然失败，注释反而传播错误构建信息。
- 最小修复：改为指向真实路径与真实文件名，或删除该注释行。
- 备注：注释自称「运行时使用示例」却无任何示例代码，与实现完全脱节。

##### [中] 90-22｜四个运行时文件均无测试或断言，边界行为无回归保护
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt`（全文）、`Memory.krt`（全文）、`Sys.krt`（全文）、`Runtime.krt`（全文）
- 类别：12 测试不足
- 触发条件：任何对 MinValue、0、负数、NULL、越界尺寸的调用均无测试覆盖。
- 后果：90-1/90-2/90-3/90-5/90-8/90-9/90-11/90-13 等缺陷不会被测试发现，回归无保障。
- 最小修复：为每个公开函数补边界用例（MinValue、0、负数、NULL、size=0/负、重叠 Copy），并纳入构建流程。
- 备注：仅声明本批四个文件范围内未发现测试文件，未通读其他目录。

##### [低] 90-23｜LCM 中间乘法溢出静默回绕
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:124`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`LCM(a, b)` 结果超出 int32（如 `LCM(2147483647, 2)`）。
- 后果：`a / GCD(a,b) * b` 回绕为错误值（可能为负），无提示。
- 最小修复：用 int64 计算后判范围，或在溢出时抛异常。
- 备注：`a==0||b==0` 的特判正确；仅乘法宽度不足。

##### [低] 90-24｜GCD 经 Abs 继承 MinValue 溢出缺陷
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:109-110`
- 类别：8 未检查前置条件 / 3 语义分叉
- 触发条件：`GCD(int32.MinValue, b)`，或 `GCD(a, int32.MinValue)`。
- 后果：`Abs` 内部溢出（90-1）使 `a` 保持负值；欧几里得循环对负被除数走 C# 截断取余，结果依赖实现且与「非负 GCD」契约不符。
- 最小修复：在 `GCD` 入口对 MinValue 特判，或用无溢出方式取绝对值（先转 int64）。
- 备注：缺陷根因在 90-1，但此处是独立传播点，故单列。

##### [低] 90-25｜Pow 无 int64 重载，与 Abs/Max/Min 的重载策略不对称
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:57`
- 类别：6 契约破坏（API 一致性）/ 9 错误处理不一致
- 触发条件：需要 int64 幂运算的调用方。
- 后果：调用方被迫把 int64 截断为 int32 再调用，静默丢精度；同类「二元/一元算术」中仅 Pow 缺 int64 版本。
- 最小修复：补 `Pow(int64, int64)` 重载，或明确文档化仅支持 int32 并说明溢出边界。
- 备注：接口一致性问题，不单独致错但易诱发误用。

##### [低] 90-26｜Exit 忽略 syscall 返回值，失败静默
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:84-87`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`SYS_exit` 未能执行（理论上一旦成功不返回；若编号错误则可能落到其它 syscall）。
- 后果：无返回值可判断，失败时行为未定义；与同文件其它 syscall 封装同样不检查返回，风格一致但无诊断。
- 最小修复：文档化「不返回」，或在 debug 构建中断言编号与架构匹配。
- 备注：严重度低，因 exit 成功路径本就不返回。

##### [低] 90-27｜InternalStringPtr 指针与 int64 强转无校验
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:23-26`
- 类别：6 契约破坏（编码约定）/ 8 未检查前置条件
- 触发条件：对 null 调用，或在 32 位/带指针压缩的目标上使用。
- 后果：null 静默变 0；指针↔整数往返依赖「指针即 int64」的未文档化约定，目标平台指针宽度变化时截断。
- 最小修复：文档化该约定并限定目标为 64 位；null 明确返回 0 并注释。
- 备注：与 90-5 同属「string/ptr 与底层表示强转」家族。

##### [低] 90-28｜Runtime.krt 缺少 namespace 声明，与同目录文件不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Runtime.krt:1`
- 类别：4 阶段不对齐 / 6 契约破坏
- 触发条件：与 `Math.krt`/`Memory.krt`/`Sys.krt`（均声明 `namespace System.Runtime;`）一起编译时。
- 后果：文件级命名空间缺失，若其中出现类型声明会落入全局命名空间，与既有 `System.Runtime.*` 组织不一致，增加链接/解析歧义。
- 最小修复：补 `namespace System.Runtime;`，或按 90-20 直接删除该文件。
- 备注：当前文件无代码，仅属结构性隐患。

### 第九十一批：Re.KrtC/src/Accelerator.c + Accelerator.h

#### 覆盖范围

- 全文读完 `Re.KrtC/src/Accelerator.c`（161 行，16 个函数）与 `Re.KrtC/src/Accelerator.h`（29 行，16 条声明）。
- 调用方核对仅用 grep 特定符号：`accelerator_*`、`encode_utf8`、`#include "Accelerator.h"`、`Accelerator.c`（构建）、`KrtError`（KrtCommon.h:14/26）。
- 实际被调用的只有 7 个：`accelerator_hash_string`、`accelerator_is_alpha`、`accelerator_is_digit`、`accelerator_is_octal_digit`、`accelerator_is_hex_digit`、`accelerator_hex_to_int`、`encode_utf8`；调用点集中在 `Compiler/Frontend/Lexer/Tokenizer.c`。
- 构建：`Re.KrtC/build.zig:95` 无条件编译 `src/Accelerator.c`。
- G1 核对：本文件不含 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC`/裸 `malloc/free`，分配失败零诊断问题在本文件不触发。
- 未发现：全局/共享可变状态（类别 7）、资源与生命周期（类别 10）、阶段不对齐（类别 4）、构建配置漂移（类别 11）。
- 测试：`Test/` 下 grep `Accelerator|accelerator_|encode_utf8` 零命中。


##### [致命] 91-1｜encode_utf8 不校验码点合法域，静默产出非法 UTF-8
- 位置：`Re.KrtC/src/Accelerator.c:112-144`（关键分支 113-118、134-142）；声明 `Accelerator.h:24`
- 类别：3 语义分叉 / 8 未检查前置条件 / 1 静默失败
- 触发条件：`codepoint` 为负、大于 `0x10FFFF`、或落在代理区 `0xD800-0xDFFF`。
- 后果：负值命中 `codepoint < 0x80` 分支，写成单字节 `0xFF` 等非法序列；超界值走 4 字节分支按移位截断编码；代理区被编成 CESU-8 三字节。`Tokenizer.c:492/501` 直接写入字符串字面量，最终源码中的 `\u`/`\U` 转义变成非法 UTF-8 且无任何诊断。
- 最小修复：入口加 `if (codepoint < 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) { KrtError_LOC(...); return -1; }`。
- 备注：调用方 `\U` 把最多 8 位十六进制解析进 signed `int`（`Tokenizer.c:497-498`）本身会溢出并喂入负值；该溢出属调用方，本条只记 Accelerator.c 侧未设防。

##### [高] 91-2｜encode_utf8 未检查 buffer NULL 与 max_length 非正
- 位置：`Re.KrtC/src/Accelerator.c:112-117`（其余分支 119-143 同样无检查）
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`buffer == NULL`，或 `max_length <= 0`。
- 后果：`buffer[0] = ...` 直接解引用 NULL 崩溃；`max_length <= 0` 时各分支静默返回 0，与"空间不足"共用同一返回值，调用方无法区分非法码点与空间不足。
- 最小修复：入口 `if (!buffer || max_length <= 0) { KrtError_LOC(...); return -1; }`。
- 备注：头文件 `Accelerator.h:24` 未声明任何前置条件。

##### [高] 91-3｜encode_utf8 空间不足静默返回 0，字符被无声丢弃
- 位置：`Re.KrtC/src/Accelerator.c:114-115、120-121、127-128、135-136`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：剩余容量小于当前码点所需字节数。
- 后果：`Tokenizer.c:492/501` 以 `length += encode_utf8(...)` 累加返回值，拿到 0 后该转义字符直接消失，词法结果与源码不一致且无错误上报。
- 最小修复：空间不足返回 -1 并由调用方报错；至少 `KrtError_LOC`。
- 备注：调用方容量增长只按 2 字节预留（`Tokenizer.c` 字符串读取逻辑），提高了触发概率；本条定位 Accelerator.c 的返回契约。

##### [高] 91-4｜accelerator_div_double 除零返回 0，与真实结果不可区分
- 位置：`Re.KrtC/src/Accelerator.c:49-55`
- 类别：1 静默失败 / 3 语义分叉 / 9 错误处理不一致
- 触发条件：`b == 0`（含 `-0.0`）。
- 后果：返回 0 而非 inf/NaN/错误码，调用方拿到看似合法的 0，`a/0` 与 `0/1` 无法区分；`b == 0` 对 `-0.0` 成立却掩盖符号，对 NaN 又不触发。
- 最小修复：按 IEEE 返回 `a/b`（inf/NaN）或改为错误码返回；不要吞掉零除。
- 备注：该函数无任何调用方（见 91-5），但作为导出 API 契约已破坏。

##### [高] 91-5｜9 个函数全仓零调用，纯死代码
- 位置：`Re.KrtC/src/Accelerator.c:7,19,37,41,45,49,57,71,146`；`Re.KrtC/src/Accelerator.h:8,9,11,12,13,14,16,17,25`
- 类别：5 死代码
- 触发条件：全仓 grep `accelerator_*`，除定义与声明外无任何调用点。
- 后果：未使用的 `accelerator_strlen`、`accelerator_strcmp`、`accelerator_add_double`、`accelerator_sub_double`、`accelerator_mul_double`、`accelerator_div_double`、`accelerator_memcpy`、`accelerator_memset`、`accelerator_is_whitespace` 仍被 `build.zig:95` 编译进产物，且与 libc 形成语义已分叉的双实现（见 91-6/91-7/91-8）。
- 最小修复：删除未用函数；若刻意保留，必须补契约文档与测试。
- 备注：头文件仍被 10 个源文件包含（`Ast.c:3`、`Parser.c:5`、`ParserBase.c:5`、`ParserExpression.c:5`、`ParserStatement.c:5`、`ParserAdvanced.c:5`、`SemanticAnalyzer.c:20`、`Generics.c:2`、`NameMangling.c:2`、`Tokenizer.c:7`），其中仅 `Tokenizer.c` 真正使用 `accelerator_*`。

##### [中] 91-6｜accelerator_memcpy 不处理重叠区域且 NULL 静默返回
- 位置：`Re.KrtC/src/Accelerator.c:57-69`
- 类别：6 契约破坏 / 8 未检查前置条件 / 1 静默失败
- 触发条件：`dest` 与 `src` 内存重叠；或 `dest == NULL`、`src == NULL`。
- 后果：正向逐字节复制在重叠时破坏数据（超出 memcpy 的不重叠契约）；NULL 输入静默返回 `dest`，掩盖错误。
- 最小修复：改用 `memmove`，或在文档中明确禁止重叠；NULL 输入报错。
- 备注：函数未被调用。

##### [中] 91-7｜accelerator_memset 对 NULL/零长度静默返回，返回值语义混淆
- 位置：`Re.KrtC/src/Accelerator.c:71-81`
- 类别：9 错误处理不一致 / 3 语义分叉
- 触发条件：`s == NULL` 或 `n == 0`。
- 后果：NULL 时返回 NULL 冒充成功；与 libc `memset` 的未定义行为契约分叉；同文件 `accelerator_memcpy` 的 NULL 分支返回 `dest`，两个同类函数对 NULL 的返回值口径不一致。
- 最小修复：统一为报错或统一返回语义，并写进头文件。
- 备注：函数未被调用。

##### [中] 91-8｜accelerator_strlen/strcmp 的 NULL 语义静默且互不一致
- 位置：`Re.KrtC/src/Accelerator.c:7-17`（strlen）、`19-35`（strcmp）
- 类别：1 静默失败 / 3 语义分叉 / 9 错误处理不一致
- 触发条件：`str == NULL`；`strcmp` 单侧为 NULL。
- 后果：`accelerator_strlen(NULL)` 返回 0，与空串不可区分；`accelerator_strcmp(NULL,NULL)` 返回 0，但 `accelerator_strcmp(NULL,"")` 返回 -1，NULL 被当普通字符串参与比较，与 libc 契约分叉。
- 最小修复：NULL 视为错误并 `KrtError_LOC`，或直接删除这两个未被使用的函数。
- 备注：两函数均未被调用。

##### [中] 91-9｜accelerator_hex_to_int 非法输入静默返回 0
- 位置：`Re.KrtC/src/Accelerator.c:99-110`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：`c` 不是十六进制字符。
- 后果：返回值 0 与字符 `'0'` 相同，调用方若漏掉 `accelerator_is_hex_digit` 前置判断即静默算错，且无诊断。
- 最小修复：非法输入返回 -1 并同步调整调用方判断，或在头文件明确前置条件。
- 备注：当前调用方 `Tokenizer.c:386/479/488/497` 均有 `is_hex_digit` 守卫，运行路径暂安全；属契约缺陷。

##### [中] 91-10｜Accelerator.h 无契约注释、命名不一致，返回 0 语义未定义
- 位置：`Re.KrtC/src/Accelerator.h:8-27`（尤其第 24 行 `encode_utf8`）
- 类别：13 注释与实现脱节 / 6 契约破坏
- 触发条件：任何调用方阅读头文件。
- 后果：`encode_utf8` 不带 `accelerator_` 前缀，与其余 15 个函数命名规则不一致；返回 0（空间不足/非法）与正数长度含义未文档化；NULL 行为未声明，调用方只能靠实现猜。
- 最小修复：补前置条件/返回值/错误语义注释，统一命名前缀。
- 备注：该头文件被 10 个源文件包含，但仅 `Tokenizer.c` 使用其中符号，其余 9 处为无用包含依赖。

##### [低] 91-11｜accelerator_is_alpha 仅 ASCII 且把 '_' 当字母，非 ASCII 静默判否
- 位置：`Re.KrtC/src/Accelerator.c:83-85`
- 类别：3 语义分叉 / 13 注释与实现脱节
- 触发条件：`char` 为 `0x80-0xFF`（signed char 下为负值）。
- 后果：`Tokenizer.c:320/586` 用它界定标识符首字符，非 ASCII 标识符被静默截断；`'_'` 判定与调用方 `Tokenizer.c:321/586` 的显式 `c == '_'` 重复。
- 最小修复：文档明确限定 ASCII，或改用 `unsigned char` 并定义 Unicode 策略。
- 备注：该函数在活跃路径上被使用。

##### [低] 91-12｜accelerator_is_whitespace 未覆盖 \v/\f 且无任何调用方
- 位置：`Re.KrtC/src/Accelerator.c:146-148`；`Re.KrtC/src/Accelerator.h:25`
- 类别：5 死代码 / 3 语义分叉
- 触发条件：输入 `'\v'` 或 `'\f'`。
- 后果：与 libc `isspace` 分叉；函数全仓零调用。
- 最小修复：删除，或补齐字符集并加测试。
- 备注：与 91-5 的死代码清单重叠，但语义缺陷独立。

##### [低] 91-13｜accelerator_hash_string 用有符号 char 累加，高位字节符号扩展
- 位置：`Re.KrtC/src/Accelerator.c:150-161`
- 类别：3 语义分叉 / 8 未检查前置条件
- 触发条件：字符串含 `0x80-0xFF` 字节。
- 后果：`char c` 在 signed char 平台为负，参与 `uint32_t` 加法时符号扩展，结果依赖平台 char 符号性，与按 `unsigned char` 实现的标准 djb2 分叉；`Tokenizer.c:148/166` 用它构建关键字哈希表，属活跃路径。
- 最小修复：改为 `unsigned char c;`。
- 备注：哈希碰撞率未评估，但符号性分叉是确定缺陷。

##### [低] 91-14｜Accelerator.c 引入未使用的 <string.h>/<stdlib.h>
- 位置：`Re.KrtC/src/Accelerator.c:2-3`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：编译期。
- 后果：文件无任何 `malloc/free/strlen/memcpy` 调用，这两个 include 是死依赖，容易让人误以为本文件涉及分配。
- 最小修复：删除未使用的 include。
- 备注：G1 相关结论——本文件不做内存分配，分配失败零诊断问题不在此文件触发。

##### [低] 91-15｜accelerator_div_double 的 KrtError 缺位置信息
- 位置：`Re.KrtC/src/Accelerator.c:51`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：发生除零。
- 后果：`KrtError`（`KrtCommon.h:14`）只打印消息，不带 `file:line`；项目已有 `KrtError_LOC`（`KrtCommon.h:26`）却未使用，诊断无法定位到具体调用点。
- 最小修复：改用 `KrtError_LOC(__FILE__, __LINE__, "Division by zero in accelerator_div_double")`。
- 备注：与 91-4 相关但独立——一条是返回值语义，一条是诊断质量。

##### [中] 91-16｜Accelerator 全模块无测试覆盖
- 位置：`Re.KrtC/src/Accelerator.c:112-161`（`encode_utf8`、`accelerator_hash_string` 等）
- 类别：12 测试不足
- 触发条件：`Test/` 目录 grep `Accelerator|accelerator_|encode_utf8` 零命中。
- 后果：NULL、非法码点、容量不足、除零、有符号 char 哈希等边界均无回归保护，上述缺陷可长期潜伏。
- 最小修复：为 `encode_utf8`（含非法码点/容量不足）与 `accelerator_hash_string` 补单元测试。
- 备注：本批未发现测试目录存在该模块用例。

### 第九十二批：IrParamTable.c/.h + src/Bytecode/{Bytecode.h,Value.h} + Shared/{Bytecode.h,BytecodeGenerator.h}

#### 覆盖范围

逐行读完（read 工具，全量）：

- `Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c`（146 行）
- `Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h`（38 行）
- `Re.KrtC/src/Bytecode/Bytecode.h`（53 行）
- `Re.KrtC/src/Bytecode/Value.h`（52 行）
- `Re.KrtC/Shared/Bytecode.h`（80 行）
- `Re.KrtC/Shared/BytecodeGenerator.h`（15 行）

未通读其他源文件。为核对调用方/定义，仅做了定向 grep：`KrtIrParamTable*`、`KrtIrFunctionFindParam|GetParamIndex|param_table`、`KrtBytecodeGenerator*`、`KrtChunk*|KrtValueArray*|KrtValuePrint`、`KRT_BYTECODE_H|KRT_VM_VALUE_H`、`#include`（仅限定文件的行首匹配）、`KRT_MALLOC|KRT_CALLOC|KRT_FREE|KRT_STRDUP`（定义处）、`-I|include_directories`（CMakeLists.txt）。
`IrInline.inc` / `IrRecursiveInline.inc` 未触碰。

结论概览：**无致命条目**；高 3 条、中 8 条、低 5 条，共 16 条。核心是「两套 Bytecode 定义共用同一 include guard 互相静默屏蔽」与「IrParamTable 整模块死代码 + 返回节点所有权不一致」。

---


##### [高] 92-1｜两个 Bytecode.h 共用 `KRT_BYTECODE_H`，先包含者静默吞掉另一份定义
- 位置：`Re.KrtC/Shared/Bytecode.h:1-2`、`Re.KrtC/src/Bytecode/Bytecode.h:1-2`
- 类别：语义分叉 / 配置漂移 / 契约破坏
- 触发条件：任一 TU 同时（直接或间接）包含这两个头。实测链路：`src/Compiler/Backend/Vm/VmCodegen.h:5-6` 先 `#include "Bytecode.h"` 再 `#include "BytecodeGenerator.h"`；后者在 `Shared/BytecodeGenerator.h:3` 又 `#include "Bytecode.h"`。由于 `CMakeLists.txt:20` 的顺序是 `src src/Core src/Tools src/Bytecode Shared ...`，`VmCodegen.h:5` 命中 `src/Bytecode/Bytecode.h` 并置位 guard，于是 `Shared/Bytecode.h` 的**全部**定义（`KrtOpCode`/`KrtValueType`/`KrtValue`/`KrtValueArray`/`KrtChunk`）被整段跳过。
- 后果：`Shared/BytecodeGenerator.c`（包含自身目录的 `Shared/Bytecode.h`）与编译器侧 TU（实际拿到 `src/Bytecode/Bytecode.h`）使用的是两份文本不同、仅靠人工保持一致的枚举与结构体。当前布局碰巧相同所以能编过；一旦 Shared 侧增删 opcode 或改字段，编译器侧**收不到任何报错**，代码生成器与序列化器对同一 opcode 数值/字段偏移的理解静默分叉，产出错字节码。若将来调整 `-I` 顺序，则反向生效：`src/Bytecode/Bytecode.h` 被整段跳过，连它带进来的 `Value.h` 一并消失，`KrtValue`/`KrtValueType` 直接未定义而编译失败。
- 最小修复：给两个头使用各自唯一的 guard（如 `KRT_SHARED_BYTECODE_H` / `KRT_COMPILER_BYTECODE_H`），并让 `Shared/Bytecode.h` 改为 `#include` 唯一的规范定义；短期至少在两处 guard 加 `#ifndef` 一致性断言（如 `_Static_assert(sizeof(KrtChunk) == …)`）。
- 备注：本批最具扩散性的一条；92-2/92-11 是同一根因的不同侧面。

##### [高] 92-2｜`KrtValueType`/`KrtValue`/`KrtValueArray`/`KrtChunk`/`KrtOpCode` 双份定义，无单一真源、无编译期断言
- 位置：`Re.KrtC/Shared/Bytecode.h:48-78` 对照 `Re.KrtC/src/Bytecode/Value.h:6-45`、`Re.KrtC/src/Bytecode/Bytecode.h:8-45`
- 类别：语义分叉 / 契约破坏
- 触发条件：任何同时可见两侧定义的 TU，或任一侧被独立演进（加 opcode、改 `KrtValue` 联合、改 `KrtChunk` 字段顺序）。
- 后果：`Shared/Bytecode.h` 内联重定义了 `KrtValueType`/`KrtValue`/`KrtValueArray`，而不是 include `src/Bytecode/Value.h`；`src/Bytecode/Bytecode.h` 与 `Shared/Bytecode.h` 又各自重定义 `KrtOpCode`/`KrtChunk`。两份定义之间没有任何 `static_assert`/`_Static_assert` 或共享头，枚举值与结构体布局相等只是巧合。92-1 的 guard 冲突进一步保证「编译期永远不会发现」这种漂移。
- 最小修复：抽出一个唯一规范头（含 `KrtOpCode`/`KrtValueType`/`KrtValue`/`KrtValueArray`/`KrtChunk` + 布局与枚举值的 `_Static_assert`），两侧头都改为 include 它；禁止任何一侧再自带定义。
- 备注：与 92-1 同根因，但修复动作不同（合并定义 vs 拆 guard），故独立成条。

##### [高] 92-3｜`KrtIrFunctionFindParam` 返回节点所有权不一致，`KrtIrFunctionGetParamIndex` 无条件 `KRT_FREE`
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:117-131`、`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:144`
- 类别：契约破坏 / 资源与生命周期 / 未检查前置条件
- 触发条件：`func->param_table != NULL` 时走 117-119 行，返回的是 `KrtIrParamTableFind` 给出的 **arena 内存**节点；`KrtIrFunctionGetParamIndex` 在 144 行无条件 `KRT_FREE(param)`。该函数同时声明于 `IrParamTable.h:34` 与 `Ir.h:524`，是公开 API。
- 后果：对 arena 指针调用 `KrtSafeFree`（`KrtCommon.h:78`）属于释放非 malloc 内存，行为未定义（轻则报错/计数错乱，重则堆破坏）。当前 `param_table` 全仓从未被赋值（见 92-4），所以只走 121-133 的堆回退分支；一旦按设计接线 `param_table`，这条路径立刻变成内存破坏。附带问题：回退分支每次查找都 `KRT_CALLOC` 一个新节点（123 行），`GetParamIndex` 随即释放，高频查找产生纯浪费的堆 churn；且 `temp_node->name` 直接别名 `func->params[i].name`（127 行），任何调用方若释放/改写 `name` 会破坏 IR 参数表。
- 最小修复：统一所有权——要么 `FindParam` 永远返回 arena/借用节点且**不提供** `GetParamIndex` 的释放语义（改为调用方不释放），要么回退分支也写入 arena 并明确「返回值不可 FREE」；至少把 144 行的释放限制在确认为堆节点的路径，并用 out 参数或独立函数名区分两种来源。
- 备注：这是本批唯一的内存安全类隐患；因 `param_table` 未接线而处于休眠态，故未定致命。

##### [中] 92-4｜IrParamTable 整套 API 是死代码，`KrtIRFunction.param_table` 从未被赋值
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h:21-32`、`Re.KrtC/src/Compiler/Middle/Ir/Ir.h:333`
- 类别：死代码 / 阶段不对齐
- 触发条件：全仓 grep `KrtIrParamTableCreate|Add|Find|Count|Foreach` 与 `param_table`，除 `IrParamTable.c/.h` 自身及 `Ir.h:333` 的字段声明、`IrParamTable.c:117-118` 的读取外，无任何赋值或调用；后端 `VmCodegen.c` 直接使用 `func->params` / `val.data.index`。
- 后果：Middle/IR 阶段预留了「参数名→索引哈希表」这一层，但从未接线，形成事实上的双通道：`KrtIrFunctionFindParam` 优先查表、查不到不回退（118 行直接 return），而表又永远为空，于是永远走 121 行的线性扫描。阶段语义与实际实现脱节，后续维护者很容易误以为表已生效而绕过 `func->params`，或误以为扫描路径是死代码而删除。
- 最小修复：二选一——在 IR 构建函数参数时真正 `KrtIrParamTableCreate/Add` 并填充 `func->param_table`；或删除整套表 API 与 `param_table` 字段，只保留线性查找，并同步删掉 `IrParamTable.h` 中相关声明。
- 备注：`KrtIrParamTableDestroy` 的缺失定义（92-5）是这一团死代码的一部分，但因后果不同单独列条。

##### [中] 92-5｜`KrtIrParamTableDestroy` 只有声明，全仓无定义、无调用
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h:23`
- 类别：死代码 / 契约破坏
- 触发条件：任何 TU 调用该函数；或链接期符号解析。
- 后果：头文件承诺了析构能力却无实现，一旦有人按对称性在释放 arena 前调用它，就是链接错误（若走弱符号/动态链接则可能运行期失败）。当前无调用方，属未兑现的接口契约，也让 92-4 的「表」看起来比实际更完整。
- 最小修复：要么补上实现（arena 拥有全部节点时该函数应为 no-op，或改由 arena 统一回收并明确标注 no-op），要么删除该声明。
- 备注：与 92-4 独立：即使决定保留参数表，这条声明也必须落实。

##### [中] 92-6｜`KrtIrParamTableCreate`：buckets 分配失败静默返回 NULL，且已分配的 table 永久泄漏
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:23-31`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期（G1）
- 触发条件：`KrtIrArenaAlloc` 对 `sizeof(KrtIRParamTable)` 成功、对 `bucket_count * sizeof(KrtIRParamNode*)` 失败（G1：`KRT_MALLOC` 系宏失败返回 NULL 而非 exit，`IrMemory.c:79-100` 亦返回 NULL）。
- 后果：28-31 行直接 `return NULL`，23 行刚分配的 `table` 既不在 arena 的可达链上也被丢弃，无法回收；调用方拿到 NULL，与 14-16 行「参数非法」的 NULL 完全同形，无法区分是配置错误还是内存耗尽，零诊断。
- 最小修复：分配顺序改为先 buckets 后 table；失败时返回前不留下悬空分配，或让 arena 支持回滚/提供 `KrtIrArenaAlloc` 失败计数并向上层传递错误码（而非仅 NULL）。
- 备注：与 92-7 同属 G1 活路径零诊断，但发生位置与泄漏对象不同。

##### [中] 92-7｜`KrtIrParamTableAdd`：重名与分配失败都返回 false，且 strdup 失败会泄漏已分配节点
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:47-49`、`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:54-62`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期
- 触发条件：(a) 重名 → 47-49 返回 false；(b) 54 行 `KrtIrArenaAlloc` 失败 → 返回 false；(c) 60 行 `KrtIrArenaStrdup` 失败 → 返回 false，而 54 行刚分配的 `node` 未入桶、不可达。
- 后果：三种性质完全不同的失败（调用方bug / 内存耗尽 / 内存耗尽）对调用方是同一个 `false`，无日志、无错误码，符合「零容忍审计」中的静默失败；情形 (c) 还额外泄漏一个 `KrtIRParamNode`（arena 内存，进程生命周期内不回收）。
- 最小修复：区分返回码（重名用独立错误码/布尔 out 参数），并在 strdup 失败时把已分配 node 记入 arena 的空闲/回收结构，或先 strdup 再 alloc node（顺序对调，失败即无泄漏）。
- 备注：与 92-6 同根（G1），独立成条因泄漏点与可诊断性不同。

##### [中] 92-8｜`bucket_count` 移位放大可溢出为负再归零，导致死循环
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:18-21`
- 类别：未检查前置条件 / 溢出
- 触发条件：`initial_bucket_count > 2^30`（如 `0x40000001` 或 `INT_MAX`）。序列 `1,2,…,0x40000000`，下一步 `0x40000000<<1` 在 int 域溢出为 `0x80000000`（负），`负 < 正` 仍成立，再左移得 `0`，此后 `0<<1==0` 恒小于 initial，循环永不退出。
- 后果：进程无限循环（挂死）；即使避开该输入，`28` 行 `bucket_count * sizeof(KrtIRParamNode*)` 也是 int 与 size_t 混合运算，配合 52/80 行的 `hash & (bucket_count - 1)` 依赖「bucket_count 必为 2 的幂」这一未声明不变量。
- 最小修复：用 `size_t`/无符号做放大并设上限（如 `> (1u<<20)` 直接返回 NULL 或报错），或改用 `uint32_t` 并在溢出前判断 `bucket_count > INT_MAX/2`；同时把「bucket_count 是 2 的幂」写成断言。
- 备注：该 API 当前无调用方（92-4），故实际暴露面低，但作为公开头文件里的未检查前置条件必须记。

##### [中] 92-9｜`src/Bytecode/Bytecode.h` 的 5 个原型全仓无实现、无调用
- 位置：`Re.KrtC/src/Bytecode/Bytecode.h:47-51`
- 类别：死代码 / 契约破坏
- 触发条件：glob `Re.KrtC/src/Bytecode/*.c` 无结果（该目录没有 .c），grep `KrtChunkInit|Write|Free|AddConstant|AddStringConstant` 除本头声明外零命中。
- 后果：`KrtChunkInit/Write/Free/AddConstant/AddStringConstant` 是一组从未落地、也无人调用的接口；真正被 `VmCodegen.c` / `Compiler.c` 使用的是 `Shared/BytecodeGenerator.h` 的 `KrtBytecodeGenerator*` 系列。两套字节码写入 API 并存，名字近似（`Write` vs `WriteByte`、`AddConstant` vs `GeneratorAddConstant`），极易被误用；一旦误调即链接错误。
- 最小修复：删除 `src/Bytecode/Bytecode.h` 中这 5 个原型（连同 92-10 的 Value.h 原型），或补齐实现并明确哪套是规范 API。
- 备注：与 92-10 分列，因文件与符号集不同。

##### [中] 92-10｜`src/Bytecode/Value.h` 的 4 个原型全仓无实现、无调用
- 位置：`Re.KrtC/src/Bytecode/Value.h:47-50`
- 类别：死代码 / 配置漂移
- 触发条件：grep `KrtValueArrayInit|Write|Free|KrtValuePrint` 除本头声明外零命中；`src/Bytecode` 无 .c。
- 后果：同 92-9，孤儿接口。附加耦合：`Value.h:4` 写的是 `#include "Core/Utils/KrtCommon.h"`，该路径只有把 `src` 加入 include 路径才解析得到（`CMakeLists.txt:20` 恰好有 `src`）；单独包含此头或改变 `-I` 顺序即失败。`bool` 也依赖 `KrtCommon.h:87` 的 `<stdbool.h>` 传递进来，而 `Shared/Bytecode.h:6` 是自己包含 stdbool 的——同义类型的头在依赖方式上已经分叉。
- 最小修复：删除这组原型；若确需保留，改为 `#include "../Core/Utils/KrtCommon.h"` 之类的自洽相对路径并显式包含 `<stdbool.h>`。
- 备注：与 92-9 独立成条。

##### [中] 92-11｜序列化契约只存在于 .c，头文件无魔数/版本/格式约定，且两侧 `KrtChunk` 无布局断言
- 位置：`Re.KrtC/Shared/BytecodeGenerator.h:14`
- 类别：契约破坏 / 语义分叉
- 触发条件：跨编译单元/跨版本读写字节码；或 92-1/92-2 的任一定义漂移发生。
- 后果：`KrtBytecodeGeneratorSerializeToFile` 在头文件里没有任何格式说明，实际格式（magic `0x45534243`、version `1`，见 `Shared/BytecodeGenerator.c:84-87`）只存在于 .c；`KrtChunk` 结构里也没有版本/魔数字段。写端用 `Shared/Bytecode.h` 的类型尺寸（`sizeof(KrtValueType)`、`int`、`double`），读端若用 `src/Bytecode/Value.h` 那份定义，只要 enum/int 尺寸或字段顺序不同就静默错位，且没有任何编译期或运行期校验能发现。
- 最小修复：把 magic/version/各字段宽度提为头文件常量与结构体头部字段，写端先写 magic+version，读端校验；并对 `KrtChunk`/`KrtValue` 尺寸做 `_Static_assert`。
- 备注：证据来自定向 grep `Shared/BytecodeGenerator.c` 的 `fwrite|magic|version`，未通读该 .c。

##### [低] 92-12｜`KrtBytecodeGeneratorCreate/Destroy/WriteShort` 有定义无调用，opaque 句柄完全未使用
- 位置：`Re.KrtC/Shared/BytecodeGenerator.h:6`、`Re.KrtC/Shared/BytecodeGenerator.h:7`、`Re.KrtC/Shared/BytecodeGenerator.h:10`
- 类别：死代码
- 触发条件：grep 全仓，`KrtBytecodeGeneratorCreate` 仅命中 `Shared/BytecodeGenerator.c:13` 定义；`KrtBytecodeGeneratorDestroy` 仅命中 `.c:21` 定义；`KrtBytecodeGeneratorWriteShort` 仅命中 `.c:48` 定义。实际调用的是 chunk 版 API（`VmCodegen.c`、`Compiler.c:39,53,239`）。
- 后果：`KrtBytecodeGenerator` 这个 opaque 类型没有任何真实使用者，Create/Destroy 构成永远配不上对的死接口；`WriteShort` 是无人调用的工具函数，容易被误以为字节码里存在 16 位指令编码。
- 最小修复：删除句柄版 Create/Destroy 与 `KrtBytecodeGenerator` 类型，或真正改为句柄式 API 并迁移调用方；删除 `WriteShort`。
- 备注：与 92-9/92-10 同属「孤儿接口」，但位于 Shared 侧且有实现，故独立。

##### [低] 92-13｜`KrtIrFunctionGetParamIndex` 以 -1 作未找到哨兵，无文档、无调用方检查
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:138-146`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：调用方拿到返回值直接当索引用（如 `params[idx]`、`locals[idx]`）。当前全仓无调用方。
- 后果：`-1` 与合法索引 `int` 同型，头文件（`IrParamTable.h:36`、`Ir.h:525`）均未说明 -1 语义；一旦接入调用方而漏判，就是负索引越界读写。同时该函数的「查找-释放」把 92-3 的所有权陷阱埋在返回值里。
- 最小修复：改为 `bool KrtIrFunctionGetParamIndex(KrtIRFunction*, const char*, int* out_index)`，或在头文件明确标注 `-1 = 未找到` 并要求调用方检查。
- 备注：因当前无调用方记为低。

##### [低] 92-14｜`KrtIrFunctionFindParam` / `KrtIrFunctionGetParamIndex` 在两个头里重复声明
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h:34-36`、`Re.KrtC/src/Compiler/Middle/Ir/Ir.h:524-525`
- 类别：契约破坏 / 注释与实现脱节（声明漂移）
- 触发条件：两处声明分别演进（例如一侧改参数名/加 const/改返回类型）；或某 TU 只包含其中之一而未走 `Ir.h:6` 的包含链。
- 后果：`Ir.h` 已 include `IrParamTable.h`（`Ir.h:6`），524-525 行属冗余重声明。同一对函数两处独立维护，签名漂移时编译器只在「同一 TU 同时看到两处」时才报 redefinition 冲突，分属不同 TU 时无任何约束，容易静默通过。
- 最小修复：删除 `Ir.h:524-525` 的重复声明，统一由 `IrParamTable.h` 提供。
- 备注：低危但属零容忍项下的契约重复。

##### [低] 92-15｜`KrtIrParamTableForeach` 遍历顺序未定义且不稳定
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:103-109`
- 类别：契约破坏 / 共享可变状态
- 触发条件：任何按遍历顺序消费结果的调用方（例如用返回顺序决定参数布局或代码生成顺序）。当前无调用方。
- 后果：顺序 = 桶下标升序、桶内因 66 行头插法而成「后插入者先出」，既未在头文件（`IrParamTable.h:31-32`）说明，也不保证与 `index` 字段一致；同一逻辑参数集合在不同插入顺序/不同 bucket_count 下产出不同序列，跨运行不可复现。
- 最小修复：头文件明确「遍历顺序未指定」，或改为按 `index` 排序后回调（新增数组/排序成本），或提供 `KrtIrParamTableFindByIndex`。
- 备注：低危，因无调用方。

##### [低] 92-16｜本批 6 个文件零测试覆盖
- 位置：`IrParamTable.c/.h`、`src/Bytecode/Bytecode.h`、`src/Bytecode/Value.h`、`Shared/Bytecode.h`、`Shared/BytecodeGenerator.h`（整体）
- 类别：测试不足
- 触发条件：全仓 grep `KrtIrParamTable*`、`KrtChunk*`、`KrtValueArray*`、`KrtBytecodeGenerator*`，命中仅各定义/声明文件自身与 `VmCodegen.c`、`Compiler.c`、`Ir.h` 等生产代码，无任何测试文件。
- 后果：92-3 的所有权陷阱、92-6/92-7 的分配失败路径、92-8 的放大边界、92-1/92-2 的双定义一致性、92-11 的序列化 round-trip 均无回归保护；恰恰是最需要测试的失败路径完全没有覆盖。
- 最小修复：至少补：哈希表插入/重名/查找用例；`KrtIrFunctionGetParamIndex` 对 heap 与 arena 两种来源的释放行为用例；`SerializeToFile` 写后读回 round-trip 与版本不符拒绝用例；两份 Bytecode 头的 `_Static_assert` 布局一致性检查。
- 备注：测试基础设施未在本次允许范围内核查，仅陈述符号级无命中这一事实。

### 第九十三批：Core/Platform/{Platform.h,ThreadPool.h,Threading.h} + Compiler/Platform/PlatformAbstraction.h

#### 覆盖范围

- 全量读取（read 工具）：
  - `Re.KrtC/src/Core/Platform/Platform.h`（258 行，实际 258，任务书称约 259）
  - `Re.KrtC/src/Core/Platform/ThreadPool.h`（29 行）
  - `Re.KrtC/src/Core/Platform/Threading.h`（29 行）
  - `Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h`（39 行）
- 仅以 grep 特定符号核对调用方/定义（未整文件通读其它源文件）：`KrtPlatformCreateDirectory`、`KrtWriteFilePosix/Win32`、`KrtReadFilePosix/Win32`、`KRT_EXPORT/KRT_IMPORT/KRT_API/KRT_INLINE/KRT_NOINLINE/KRT_PACKED/KRT_UNPACKED/KRT_ALIGNED`、`KRT_LINKER_*`、`KRT_ASM_*`、`KrtMallocImpl/KrtCallocImpl/KrtReallocImpl/KrtFreeImpl`、`KrtThreadCreate/KrtMutex*/KrtAtomic*/KrtLoadLibrary/KrtGetProcAddress/KrtTimeMs/KrtTimeUs/KrtSleepMs/KrtGetLastError/KrtError_*`、`thread_pool_create/submit/wait/destroy/get_active_tasks`、`KrtPlatform*` 全部 API、`PlatformCommon.h` 的 include 情况、`KrtCreateDirectory`。
- 已核实：`PlatformCommon.h` 全工程零 `#include`；`Core.h:7-9` 同时包含 Platform.h / Threading.h / ThreadPool.h；`ThreadPool.c:1` 与 `ParallelCompiler.h:6` 只包含 ThreadPool.h。
- 说明：PlatformAbstraction.c 已审过，本批只记头文件与未实现声明；`PlatformCommon.h` 仅作为 Platform.h 的对照物出现（不在本批覆盖范围，但它是 Platform.h 的重复副本，必须一并定位）。


##### [高] 93-1｜Threading.h 与 ThreadPool.h 共用 include guard `THREAD_POOL_H`
- 位置：`Re.KrtC/src/Core/Platform/Threading.h:1-2` + `Re.KrtC/src/Core/Platform/ThreadPool.h:1-2`（触发点 `Re.KrtC/src/Core/Core.h:8-9`）
- 类别：语义分叉 / 配置漂移（include guard 撞名）
- 触发条件：任何 TU 同时包含这两个头；`Core.h` 第 8、9 行连续包含，`Threading.h` 先占位，`ThreadPool.h` 被整体跳过。
- 后果：`ThreadPool.h:4` 的 `#include "../Utils/KrtCommon.h"` 与 `Threading.h:4` 的 `#include "Platform.h"` 只有一处生效；两文件当前内容逐字相同所以暂未暴露，一旦独立演进即静默取错一份。若某 TU 依赖 Threading.h 传递包含 Platform.h 且 ThreadPool.h 已先被包含，Platform 宏静默缺失。
- 最小修复：`Threading.h` 改用 `KRT_THREADING_H`，并删除其中重复的 ThreadPool 定义（见 93-26）。
- 备注：与已记录的 S55-1（Error.h/StackCalculator.h guard 撞名）同类；因两文件当前内容一致，危害为潜伏期，故记「高」而非「致命」。

##### [高] 93-2｜Platform.h 与 PlatformCommon.h 是同一平台抽象的两份拷贝，且已产生 4 处分歧
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:1-2,163,244` vs `Re.KrtC/src/Core/Platform/PlatformCommon.h:1-2,148,164,245`
- 类别：死代码 / 配置漂移
- 触发条件：任何按 `KRT_LINKER_RUNTIME_LIBS` 拼链接命令、或使用建目录宏的代码；或将来把 PlatformCommon.h 接回构建。
- 后果：`PlatformCommon.h` 全工程零 `#include`（grep 确认），是死头文件；两份副本已出现 4 处不一致：① include guard（`PLATFORM_H` vs `KRT_PLATFORM_H`）；② Windows 建目录宏 `KrtCreateDirectory` 只在 PlatformCommon.h:148 有，Platform.h Windows 分支缺失；③ POSIX 建目录宏名 `KrtPlatformCreateDirectory`（Platform.h:163）vs `KrtCreateDirectory`（PlatformCommon.h:164）；④ POSIX 运行时库 `"-lc -lm"`（Platform.h:244）vs `"-lc -lm -lpthread"`（PlatformCommon.h:245）。调用方按其中一份编写即与另一份不兼容。
- 最小修复：删除 `PlatformCommon.h`，或让 `Platform.h` 成为唯一副本并统一宏名与 `-lpthread`。
- 备注：`audit_extract/chunk_08.md` S55-7 已记 `KrtCreateDirectory` 与 `Path.h:9` 的冲突；本条针对 Platform.h 侧的副本分叉。

##### [高] 93-3｜`KrtAtomicCmpXchg` 返回语义跨平台不一致
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:188`（Windows）vs `Platform.h:195-196`（POSIX）
- 类别：语义分叉
- 触发条件：调用方按 POSIX 习惯用返回值判 CAS 成功，或按 Windows 习惯把返回值与期望值比较。
- 后果：Windows `InterlockedCompareExchange` 返回**交换前旧值**；POSIX `__atomic_compare_exchange_n` 返回**成功布尔（1/0）**。同一句 `if (KrtAtomicCmpXchg(&x, e, d))` 在 Windows 上只要旧值非 0 就为真（CAS 失败也判成功），POSIX 上失败为 0，语义相反。
- 最小修复：统一为返回旧值（POSIX 先用 `__atomic_load_n` 取值再 CAS），或统一返回布尔并改名 `KrtAtomicTryCmpXchg`。
- 备注：全工程当前无调用点（见 93-13），属未爆雷的语义分叉。

##### [高] 93-4｜`KrtWriteConsole`/`KrtReadConsole`/`KrtWriteFilePosix`/`KrtReadFilePosix` 静默吞掉 write/read 错误与短写
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:77-80`
- 类别：静默失败 / 错误处理不一致
- 触发条件：`write`/`read` 返回 -1（EINTR/EAGAIN/EBADF）或返回值小于 `count`（短写/短读，管道、TTY、磁盘满）。
- 后果：`*(written) = write(...)` 把 `(size_t)-1` 直接写进出参，错误被永久覆盖、无法回溯；短写被当作完整写入，后续数据流错位；两个宏全工程零调用点，接口却看似可用。
- 最小修复：返回 `int` 状态；内部循环处理 EINTR 与短写；失败时 `KrtSetLastError(errno)`。
- 备注：`KrtWriteConsole`/`KrtReadConsole` 同样零调用（grep 仅命中两个头文件）。

##### [高] 93-5｜`KrtWriteFileWin32`/`KrtReadFileWin32` 忽略 `WriteFile`/`ReadFile` 的 BOOL 返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:63-74`
- 类别：静默失败 / 诊断缺失
- 触发条件：句柄无效、访问被拒、磁盘满、重叠 I/O 失败。
- 后果：返回值被丢弃；`WriteFile` 失败时 `_written` 从未赋值，`*written` 保持调用方传入的栈/堆原值，被下游当作有效写入长度；错误码随后被其他 API 覆盖，`KrtGetLastError` 取不到真实原因。
- 最小修复：检查 BOOL，失败时 `KrtSetLastError(GetLastError())` 并返回 0；`written` 先置 0。
- 备注：与 POSIX 侧 93-4 构成同层两套错误处理风格（一侧返回 ssize_t、一侧丢弃 BOOL）。

##### [高] 93-6｜`KrtThreadCreate` 丢弃 `pthread_create`/`_beginthreadex` 返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:105`（Windows）与 `Platform.h:117`（POSIX）
- 类别：静默失败 / 诊断缺失
- 触发条件：线程数超限（EAGAIN）、内存不足（ENOMEM）、`_beginthreadex` 返回 NULL。
- 后果：POSIX 下 `pthread_create` 失败时 `handle` 未被写入，后续 `KrtThreadWait`/`KrtThreadClose` 作用于未初始化 `pthread_t`（UB）；Windows 下 handle 为 NULL，`WaitForSingleObject(NULL)` 立即失败但无人检查。线程池静默少线程运行，吞吐下降无告警。
- 最小修复：宏改为返回 `int`，调用方检查；失败设 `KrtError_OUT_OF_MEMORY`。
- 备注：两平台回调签名也不统一（`_beginthreadex` 要 `unsigned (__stdcall*)(void*)`，`pthread_create` 要 `void*(*)(void*)`），同一份调用代码无法两平台编译（契约破坏，同一 macro 的隐含约定未文档化）。

##### [高] 93-7｜`KrtMutexInit/Lock/Unlock/Destroy` 全部丢弃返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:108-111`（Windows）与 `Platform.h:120-123`（POSIX）
- 类别：静默失败 / 错误处理不一致
- 触发条件：POSIX `pthread_mutex_init` 返回 EINVAL/EBUSY/ENOMEM；`pthread_mutex_lock` 在错误检测型 mutex 上返回 EOWNERDEAD/EINVAL；`pthread_mutex_destroy` 对仍锁定的 mutex 返回 EBUSY（POSIX 允许但结构已不可复用）。
- 后果：初始化失败（如静态内存未对齐/EINVAL）后继续使用该 mutex，加锁行为未定义且无任何诊断；同层其它模块（如 `KrtError`）用返回码而本层用丢弃，风格不统一。
- 最小修复：改为返回 `int` 的内联包装并断言；Destroy 前断言未锁定。

##### [高] 93-8｜`KrtThreadWait` 丢弃 `pthread_join` 返回值，且 `KrtThreadClose` 在 POSIX 是空宏
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:118-119`（POSIX）、`Platform.h:106-107`（Windows）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：对未初始化、已 join 或非可 join 的 `pthread_t` 调用 `pthread_join`（EINVAL/EDEADLK）；Windows 对已关闭句柄调用 `CloseHandle`。
- 后果：`KrtThreadWait` 无法区分成功/失败；`KrtThreadClose` 在 POSIX 展开为空语句，与 Windows `CloseHandle`（释放内核句柄）语义不对称——同一"关闭句柄"调用在两平台一个释放资源一个什么也不做，资源所有权约定未文档化、未断言。
- 最小修复：`KrtThreadWait` 返回 int；在头文件写明"每个 handle 只能 Wait/Close 一次"，并对重复调用断言。

##### [高] 93-9｜`KrtPlatformCreateDirectory` 只有 POSIX 定义，Windows 分支缺失且命名分叉
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:163`（POSIX 分支），对照 Windows 分支 `Platform.h:144-151`
- 类别：语义分叉 / 配置漂移 / 死代码
- 触发条件：Windows 平台任何调用建目录的代码；或从 `PlatformCommon.h` 迁移过来的调用方（那边叫 `KrtCreateDirectory`）。
- 后果：Platform.h 的 Windows 分支没有 `KrtPlatformCreateDirectory`/`KrtCreateDirectory`，Windows 下调用直接编译失败；POSIX 宏名又与 PlatformCommon.h 不一致（`KrtPlatformCreateDirectory` vs `KrtCreateDirectory`），且 `KrtPlatformCreateDirectory` 全工程零调用点。
- 最小修复：统一命名 `KrtPlatformCreateDirectory`，并在 `Platform.h:144-151` 补 `CreateDirectory(path, NULL)` 版本。
- 备注：与 93-2 的分歧清单②③对应。

##### [中] 93-10｜`KrtDirectoryExists` 返回域两平台不一致，且 Windows 重复调用 `GetFileAttributes`
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:146-147`（Windows）vs `Platform.h:159-161`（POSIX）
- 类别：语义分叉 / 未检查前置条件
- 触发条件：调用方写 `== 1` 或把返回值当整数用；或在两次 `GetFileAttributes` 之间目录属性被并发修改。
- 后果：Windows 返回 `FILE_ATTRIBUTE_DIRECTORY`（0x10）而非 1，`KrtDirectoryExists(p) == 1` 恒假；POSIX 返回 0/1，同一判断在 POSIX 为真。两次 `GetFileAttributes` 构成 TOCTOU 窗口。
- 最小修复：Windows 归一化为 `(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0`，并只调用一次。

##### [中] 93-11｜`KrtRemoveDirectory`/`KrtDeleteFile` 丢弃返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:148-149`（Windows）、`Platform.h:164-165`（POSIX）
- 类别：静默失败
- 触发条件：目录非空、文件只读、权限不足、路径不存在。
- 后果：`RemoveDirectory`/`rmdir`/`DeleteFile`/`unlink` 失败被吞，构建/清理脚本继续执行并误判成功；同层 `KrtFileExists` 有布尔返回而删除无，错误处理不一致。
- 最小修复：返回 `int`，失败时 `KrtSetLastError` 并由调用方诊断。

##### [中] 93-12｜`KrtMallocImpl/KrtFreeImpl/KrtReallocImpl/KrtCallocImpl` 在 POSIX 只有声明、全工程无定义
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:95-98`
- 类别：死代码 / 契约破坏
- 触发条件：任何在 POSIX 调用这四个函数（或包含该声明的 TU 被链接）。
- 后果：grep 全工程仅命中本头文件与 PlatformCommon.h 的声明，没有任何 `.c` 提供定义；一旦调用即 `undefined reference`。声明暗示存在统一分配实现，实际 Windows 侧是宏、POSIX 侧是空声明。
- 最小修复：在 `Core/Platform` 的 .c 中提供实现，或删除这 4 行声明。

##### [中] 93-13｜导出/调用约定/属性宏整族零使用
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:15-22`（`KRT_EXPORT/KRT_IMPORT/KRT_API`）与 `Platform.h:219-229`（`KRT_INLINE/KRT_NOINLINE/KRT_PACKED/KRT_UNPACKED/KRT_ALIGNED`）
- 类别：死代码
- 触发条件：任何时刻（grep 全工程仅命中 Platform.h 与 PlatformCommon.h 的定义处）。
- 后果：整族宏无调用点；`KrtCommon.h:123-134` 另有一套 `KRT_API_EXPORT/KRT_API_IMPORT/KRT_API_EXPORT_IMPORT`，命名与职责重叠，后续维护者无法判断该用哪套。
- 最小修复：删除未使用的一套（保留 KrtCommon.h 的 DLL 导出宏），或在头文件注明归属。

##### [中] 93-14｜`KRT_LINKER_*`/`KRT_ASM_*` 整族零调用，且 Windows 工具链自相矛盾
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:233-255`
- 类别：死代码 / 配置漂移
- 触发条件：任何按这些宏拼链接/汇编命令的代码；或将来启用 Windows 构建。
- 后果：全工程零调用点。Windows 分支 `KRT_LINKER_CMD "ld.exe"` 却配 `-subsystem:console`、`-entry:main`（GNU ld 语法；MSVC `link.exe` 用 `/SUBSYSTEM:CONSOLE`、`/ENTRY:`），且 `ld.exe` 不是 MSVC 自带工具；POSIX 分支 `KRT_LINKER_CONSOLE_FLAG` 为空串，拼接命令行会注入空参数。
- 最小修复：删除该族宏，或按平台给出正确的工具与 flag（Windows 用 `link.exe /SUBSYSTEM:...`，POSIX 用 `cc` 驱动）。

##### [中] 93-15｜`KRT_LINKER_RUNTIME_LIBS` POSIX 分支缺 `-lpthread`
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:244`
- 类别：配置漂移
- 触发条件：按该宏链接任何使用 pthread 的目标（工程内 `ThreadPool.h/.c` 直接使用 pthread）。
- 后果：glibc < 2.34 下 `pthread_create/pthread_mutex_*` 不在 libc 中，链接失败；与 PlatformCommon.h:245 的 `"-lc -lm -lpthread"` 不一致，两份副本给出不同链接语义。
- 最小修复：改为 `"-lc -lm -lpthread"`，或统一改用 `cc` 驱动链接。

##### [中] 93-16｜`KrtTimeMs` 时钟源语义分叉
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:129`（Windows `GetTickCount64`）vs `Platform.h:135-139`（POSIX `gettimeofday`）
- 类别：语义分叉 / 阶段不对齐
- 触发条件：POSIX 上 NTP 校时、手动改时间、闰秒导致墙钟回拨或跳变；调用方用返回值算 duration。
- 后果：Windows 为单调时钟、POSIX 为墙钟，同一 API 两平台语义不同；POSIX 下耗时可算出负值或巨大值。与 `Core/Utils/Path.c:30` 的 `CLOCK_MONOTONIC` 又构成第三套时间语义。
- 最小修复：POSIX 改用 `clock_gettime(CLOCK_MONOTONIC)`，与 Path.c 对齐。

##### [中] 93-17｜`KrtTimeUs` 的微秒位恒为 0
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:130` 与 `Platform.h:140`
- 类别：注释与实现脱节
- 触发条件：任何依赖微秒分辨率的计时/基准代码。
- 后果：两平台实现都是 `KrtTimeMs()*1000`，实际分辨率 1 ms，`KrtTimeUs` 命名承诺的微秒精度不存在；与同文件 `KrtTimeNowSeconds`（Path.c 用 `tv_nsec`）精度不一致。
- 最小修复：POSIX 直接返回 `tv_sec*1000000 + tv_usec`；Windows 改用 `QueryPerformanceCounter`。

##### [中] 93-18｜`KrtSleepMs` POSIX 分支无前置条件检查且使用 obsolete API
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:141`
- 类别：未检查前置条件
- 触发条件：`ms <= 0`（`usleep(0)` 尚可，负数经 `(ms)*1000` 转成巨大无符号）；`ms >= 1000`（POSIX 规定 `usleep` 入参 >= 1000000 时行为未定义/返回 EINVAL）。
- 后果：负值变成超长睡眠；`usleep` 已被 POSIX 标记为 obsolete（应为 `nanosleep`）；与 Windows `Sleep(ms)` 的语义（接受 0、负数未定义）也不对齐。
- 最小修复：`ms <= 0` 直接返回；改用 `nanosleep`。

##### [中] 93-19｜`KRT_INLINE` 链接属性跨平台不一致
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:219`（Windows `__forceinline`）vs `Platform.h:225`（POSIX `static inline`）
- 类别：语义分叉
- 触发条件：用 `KRT_INLINE` 修饰头文件中的函数定义。
- 后果：POSIX 得到内部链接（每 TU 一份本地副本，`&func` 地址不同、静态变量不共享）；Windows `__forceinline` 为外部链接语义。同一份头文件代码的 ODR/符号可见性/地址一致性在两平台不同。
- 最小修复：拆成两个宏（`KRT_FORCEINLINE` 与 `KRT_HEADER_INLINE`），或 POSIX 侧统一 `extern inline` 方案。

##### [中] 93-20｜`KRT_PACKED`/`KRT_UNPACKED` 在 Windows 是 pragma，不能当声明说明符用
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:221-222`（`__pragma(pack(push,1))` / `__pragma(pack(pop))`）vs `Platform.h:227-228`（`__attribute__((packed))` / 空）
- 类别：契约破坏 / 语义分叉
- 触发条件：按 POSIX 习惯写 `KRT_PACKED struct Foo {...} KRT_UNPACKED;`。
- 后果：`__pragma` 是语句级指令，不能出现在声明说明符位置，Windows 下该写法直接编译失败；两平台没有同一写法可用，抽象宏实际不可移植。
- 最小修复：提供两平台都可用的包装写法（如 `struct KRT_PACKED Foo {...};` + 单独 `KRT_UNPACKED` 空定义），或注明仅支持 GNU 属性语法。

##### [低] 93-21｜平台支持范围与 KrtCommon.h 分叉
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:10-11`（`__APPLE__` 落到 `#error "Unsupported platform"`）vs `Re.KrtC/src/Core/Utils/KrtCommon.h:140-143`（存在 `__APPLE__` 分支）
- 类别：配置漂移
- 触发条件：在 macOS 上编译任何包含 Platform.h 的 TU。
- 后果：KrtCommon.h 宣称支持 Apple，Platform.h 直接 `#error` 拒绝；同一工程对同一平台给出两种结论，编译期行为不可预期。
- 最小修复：统一平台判定表（以 Platform.h 为准删掉 KrtCommon.h 的 Apple 分支，或反之补齐 Platform.h 的 Apple 分支）。

##### [低] 93-22｜`KRT_STDIN/OUT/ERR_HANDLE` 未检查 `GetStdHandle` 失败
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:41-43`
- 类别：未检查前置条件
- 触发条件：进程无关联控制台/标准句柄被重定向关闭时，`GetStdHandle` 返回 NULL 或 `INVALID_HANDLE_VALUE`。
- 后果：句柄常量在宏展开期即固化为无效值，后续 `KrtWriteConsole` 静默失败（见 93-4/93-5），且没有任何诊断。
- 最小修复：提供 `KrtGetStdHandle()` 运行期函数并检查返回值。

##### [低] 93-23｜`KrtReadConsole` POSIX 分支静默忽略 `ctrl` 参数
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:78`（POSIX）vs `Platform.h:57-62`（Windows 传给 `ReadConsole`）
- 类别：语义分叉
- 触发条件：调用方依赖 `ctrl` 控制控制台读取行为。
- 后果：同签名（5 参）两平台语义不同：Windows 生效、POSIX 完全忽略且产生未使用参数警告；抽象层未文档化该差异。
- 最小修复：POSIX 分支 `(void)ctrl;` 显式标注，或在头文件注明仅 Windows 有效。

##### [低] 93-24｜重复包含与无用包含
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:32,92,134,154`（`<unistd.h>` 四次）、`Platform.h:26` 与 `Platform.h:84-86`（`<windows.h>` 两次，后者 `#ifndef _WINDOWS_` 守护）、`Platform.h:128`（`<time.h>` 无使用）
- 类别：死代码（低）
- 触发条件：编译期。
- 后果：重复包含靠系统头自身 guard 兜底，增加预处理噪音；`<time.h>` 在 Windows 分支引入却未被本文件使用，属复制粘贴残留。
- 最小修复：合并重复包含，删除未使用的 `<time.h>`。

##### [高] 93-25｜`ThreadPool` 的 `shutdown`/`active_tasks` 是普通 `int`，头文件未约束并发访问
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:19-20`（`Threading.h:19-20` 同）
- 类别：共享可变状态
- 触发条件：worker 线程读 `shutdown`、destroy 线程写 `shutdown`、调用方读 `active_tasks`（经 `thread_pool_get_active_tasks`，见 93-27）。
- 后果：结构体把两个跨线程可变字段暴露为普通 `int`，头文件既未用 `_Atomic` 也未注释"仅可持 `queue_mutex` 访问"；`thread_pool_get_active_tasks` 提供的是一个无锁读接口，数据竞争（C11 未定义行为）由声明层直接背书。
- 最小修复：改为 `_Atomic int`，或在结构体注释中强制"访问必须持 queue_mutex"并让 `thread_pool_get_active_tasks` 内部加锁。
- 备注：实现细节在 ThreadPool.c（已超出本批范围），但字段类型与注释义务在头文件。

##### [高] 93-26｜`Threading.h` 与 `ThreadPool.h` 内容逐字重复，仅 include 行不同
- 位置：`Re.KrtC/src/Core/Platform/Threading.h:7-27` vs `Re.KrtC/src/Core/Platform/ThreadPool.h:7-27`
- 类别：死代码 / 配置漂移（重复实现）
- 触发条件：任何时刻；任一处增删字段或函数。
- 后果：`ThreadTask`/`ThreadPool` 与 5 个函数声明存在两份独立拷贝，仅 `Threading.h:4` 的 `#include "Platform.h"` 与 `ThreadPool.h:4` 的 `#include "../Utils/KrtCommon.h"` 不同；配合 93-1 的 guard 撞名，实际只有一份生效，另一份成为"看似存在"的假接口，维护者无法判断改哪份。
- 最小修复：保留 `ThreadPool.h`，`Threading.h` 改为仅 `#include "ThreadPool.h"`（如需保留旧路径作兼容）。

##### [中] 93-27｜`thread_pool_get_active_tasks` 有定义、零调用
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:27`（定义 `Re.KrtC/src/Core/Platform/ThreadPool.c:163`）
- 类别：死代码
- 触发条件：任何时刻（grep 全工程无调用点）。
- 后果：暴露了一个无锁读接口却无人使用；后续调用者一旦使用即落入 93-25 的数据竞争。
- 最小修复：删除该声明与定义，或加锁后接入并行编译的进度上报。

##### [中] 93-28｜`thread_pool_create(int thread_count)` 前置条件未文档化、未断言
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:23`（`Threading.h:23` 同）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：`thread_count <= 0`（无 worker，submit 后任务永不执行）或极大值（`pthread_create` 逐个失败）。
- 后果：头文件对合法区间、上限、失败返回（NULL？）无任何约定，调用方 `ParallelCompiler.c:286` 传入 `compiler->max_threads`，其合法性与失败处理完全依赖实现细节。
- 最小修复：头文件注明 `thread_count > 0`，并在实现入口断言/返回 NULL + 诊断。

##### [中] 93-29｜`thread_pool_destroy/wait/submit` 的生命周期与所有权契约缺失
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:24-26`
- 类别：契约破坏（线程/所有权约定未文档化未断言）
- 触发条件：destroy 时队列仍有未执行任务；submit 与 destroy 并发；同一 pool 重复 destroy；`arg` 指向调用方栈对象。
- 后果：头文件未定义 destroy 是否等待在途任务、是否可重入、submit 返回后能否 destroy、task 节点由谁释放、`arg` 生命周期由谁保证。并发 `submit`+`destroy` 直接 UAF；destroy 丢弃排队任务也是静默失败（实现侧问题，但契约空白是根因）。
- 最小修复：在头文件写明状态机（create→submit*→wait?→destroy 单向），并断言 `pool != NULL`、destroy 只调一次。

##### [中] 93-30｜`thread_pool_wait` 语义未定义（排空队列 vs `active_tasks` 归零），无超时/返回码
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:26`
- 类别：契约破坏 / 诊断缺失
- 触发条件：调用方在 `wait` 返回后假设所有任务已完成并读取其产出。
- 后果：`wait` 返回 `void`，无超时、无失败码；"等待队列排空"与"等待活跃任务归零"是两种不同保证，调用方（`ParallelCompiler.c:294`）按后者使用，一旦实现是前者就会读到未写完的数据。
- 最小修复：头文件明确 `wait` 保证 `active_tasks == 0 && task_queue == NULL`，并返回 `int` 以便上报失败。

##### [中] 93-31｜两个线程池头文件依赖不同，可见类型随包含顺序变化
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:4`（`../Utils/KrtCommon.h`）vs `Re.KrtC/src/Core/Platform/Threading.h:4`（`Platform.h`）
- 类别：配置漂移
- 触发条件：不同 TU 以不同顺序包含两者（`Core.h` 三者全含；`ParallelCompiler.h:6` 只含 ThreadPool.h）。
- 后果：同一 `ThreadPool` 类型所在头文件对外部类型的可见集不同，预处理结果依赖包含顺序；配合 93-1 的 guard 撞名，`Core.h` 路径下 Threading.h 生效、`ParallelCompiler.h` 路径下 ThreadPool.h 生效，两条路径的隐式依赖不同。
- 最小修复：合并为一个头文件（93-26），统一依赖。

##### [高] 93-32｜`KrtPlatformPathJoin` 不在 vtable 中，且 `buffer_size` 语义无文档、无断言
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:29`（结构体 `PlatformAbstraction.h:7-20` 无 `path_join` 指针）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：任何调用点（`Main.c:965,975,1043,1058,1203,1232,1239,1240`）；`buffer_size` 传 0、`buffer`/`base`/`part` 为 NULL、或 `base`/`part` 长度之和接近 `buffer_size`。
- 后果：`buffer_size` 是否包含结尾 NUL、超容量是截断还是返回错误、返回值含义（该函数声明为 `void`）全部未定义；调用方按 `snprintf` 语义假设即可能越界写入或静默截断路径。结构体有 `path_exists`/`is_directory` 指针却无 `path_join`，抽象层对同类能力提供两种扩展方式。
- 最小修复：把 `path_join` 加入 `KrtPlatform` 结构体，或在头文件写明契约（含 NUL、超容返回 -1）并加 `assert(buffer && buffer_size > 0)`。

##### [高] 93-33｜`KrtPlatformExecuteCommand` 返回 `int` 的成功/失败语义未文档化
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:19`（`int (*execute_command)(const char*)`）与 `PlatformAbstraction.h:34`
- 类别：契约破坏 / 诊断缺失
- 触发条件：任何调用方按返回值判断命令是否成功。
- 后果：返回值究竟是子进程 exit code、`0/1` 布尔、还是 `system()` 的原始返回（须用 `WEXITSTATUS` 解析）未定义；三者在命令失败时给出完全不同的值，调用方无法统一错误处理，命令失败可能被当成成功。
- 最小修复：头文件明确"返回 0 表示成功，非 0 为子进程退出码或 -1（errno 语义）"，并规定失败时的诊断输出。

##### [中] 93-34｜`KrtPlatformCreate/Destroy` 的所有权与失败约定不完整（仅 `GetCurrent` 有注释）
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:22-23`（对照 `PlatformAbstraction.h:36-37` 的 `GetCurrent` 注释）
- 类别：契约破坏
- 触发条件：传入非法 `KrtPlatformType`；对同一指针重复 `Destroy`；`Destroy(NULL)`。
- 后果：`Create` 对非法 type 是否返回 NULL、`Destroy` 是否幂等/接受 NULL、`Create` 结果的所有权（谁 free）均未文档化、未断言；`Main.c` 中大量 `KrtPlatformDestroy(platform)` 紧跟错误分支，一旦 `Create` 返回 NULL 而 `Destroy` 不接受 NULL 就是崩溃。
- 最小修复：为全部 API 补注释（参照 `GetCurrent` 的"caller owns the result"写法），并规定 `Destroy(NULL)` 安全、`Create` 非法 type 返回 NULL。

##### [中] 93-35｜`path_separator` 字段与 `KRT_PATH_SEPARATOR` 宏构成双源真相
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:11`（`char path_separator`）vs `Re.KrtC/src/Core/Platform/Platform.h:150,166`（`KRT_PATH_SEPARATOR`/`_STR`）
- 类别：配置漂移
- 触发条件：任何同时使用两者的路径拼接代码（`Main.c:1041` 用 `KrtPlatformGetSeparator`，`ParallelCompiler.c:330-384` 用 `KRT_PATH_SEPARATOR_STR`）。
- 后果：同一路径分隔符存在两个独立定义点，新增平台或改宏时 `KrtPlatformGetSeparator` 不跟随；`KrtPlatformPathJoin` 内部用哪个也无从判断，拼接结果可能混用分隔符。
- 最小修复：`KrtPlatform.path_separator` 在 `KrtPlatformCreate` 中由 `KRT_PATH_SEPARATOR` 初始化（单一来源），或删除宏改用平台抽象。

##### [中] 93-36｜PlatformAbstraction.h 反向依赖 Driver 层，且平台枚举命名分叉
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:5`（`#include "../Driver/ConfigManager.h"`）；枚举定义在 `Re.KrtC/src/Compiler/Driver/ConfigManager.h:12`（`KRT_CONFIG_PLATFORM_WINDOWS/LINUX/UNKNOWN`），而 `Platform.h:5-9` 用 `KRT_PLATFORM_WINDOWS/LINUX`
- 类别：阶段不对齐 / 配置漂移
- 触发条件：任何包含 PlatformAbstraction.h 的底层 TU；或新增平台时维护两套枚举名。
- 后果：平台抽象层（应为最底层）依赖 Driver/Config 层，层次倒置；同一概念两套枚举命名（`KRT_CONFIG_PLATFORM_*` vs `KRT_PLATFORM_*`），且 `KRT_PLATFORM_DEFAULT` 只在 `.c` 里本地 `#define`（PlatformAbstraction.c:12,17），头文件无法表达默认平台。
- 最小修复：把 `KrtPlatformType` 移到平台层头文件并统一命名，PlatformAbstraction.h 不再 include ConfigManager.h。

##### [中] 93-37｜同族能力两种调用方式：结构体函数指针与自由函数混用
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:7-20`（`is_absolute_path/path_exists/is_directory/supports_color/set_utf8/execute_command` 指针）vs `PlatformAbstraction.h:25-29`（`KrtPlatformGetSeparator/KrtPlatformPathJoin` 自由函数）
- 类别：语义分叉
- 触发条件：扩展平台能力或新增调用方时。
- 后果：`path_exists` 走 `platform->path_exists`，`path_join` 走全局函数；`get_separator` 甚至只读字段而非指针。同类"平台能力"有三种访问方式（指针、自由函数、公开字段），新增能力时无可依据的约定，且 `KrtPlatform` 结构体可被调用方直接改写字段（无 const 保护）。
- 最小修复：统一为 vtable（含 `path_join`、`get_separator`），字段全部私有化（`const char* name` 已有 const，`path_separator` 也应只读）。

##### [低] 93-38｜路径判定函数的返回域与 NULL 入参行为未文档化
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:26-28`（`KrtPlatformPathIsAbsolute/PathExists/PathIsDirectory`）
- 类别：契约破坏
- 触发条件：传入 NULL 或空串；调用方按 `bool` 或按 `== 1` 使用返回值。
- 后果：三个函数都返回 `int`，但"1/0"还是"非 0/0"、NULL 是否安全返回 0、空串是否算存在，头文件无任何约定；与 `Platform.h` 的 `KrtFileExists`/`KrtDirectoryExists`（同样返回 int、返回域还不一致，见 93-10）构成两套路径判定 API。
- 最小修复：统一为 `KrtPlatformPathExists` 单点实现（`Platform.h` 的 `KrtFileExists`/`KrtDirectoryExists` 与 Path.c 的 `KrtPathExists` 已三处重复），并在头文件写明返回值与 NULL 约定。

##### [低] 93-39｜`PlatformAbstraction.h` 对 `size_t` 的依赖是隐式的
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:4,29`（`size_t buffer_size`）
- 类别：契约破坏（低）
- 触发条件：仅包含 PlatformAbstraction.h 而不包含 KrtCommon.h 的 TU（当前靠 `../../Core/Utils/KrtCommon.h` 间接提供 `size_t`）。
- 后果：头文件未直接 `#include <stddef.h>`，`size_t` 的可见性依赖被包含头的传递；一旦 KrtCommon.h 调整包含顺序即编译失败，且错误信息指向无关文件。
- 最小修复：在 PlatformAbstraction.h 直接 `#include <stddef.h>`。

### 第九十四批：ArkLink/StubInclude/后端/Build 头文件

#### 覆盖范围

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


##### [致命] 94-1｜`KrtBuildExecute` 返回约定未定义，且实现与调用方判断相反
- 位置：`Re.KrtC/src/Compiler/Build/BuildSystem.h:19`
- 类别：1 契约破坏（错误码约定缺失）
- 触发条件：任何一次成功或失败的 native 构建。
- 后果：头文件只写 `int KrtBuildExecute(...)`，无任何返回值约定。实现 `BuildSystem.c` 采用 `1=成功 / 0=失败`（`BuildSystem.c:29,34,41,47,53,59,66` 全部 `return 0;`，成功路径 `:70` `return 1;`）；而唯一调用方 `Main.c:485` 写成 `if (KrtBuildExecute(build_ctx, temp_eo, output_file)) { KrtError("构建失败：%s", KrtBuildGetError(build_ctx)); ... }`，即把非 0 当失败。于是「构建成功」被判定为失败并打印「构建失败」，「构建失败」（返回 0）反而被当作成功继续往下走，错误信息只在成功路径被读取。
- 最小修复：统一约定为 `0=成功，非 0=失败`（或 `1=成功`），同步修正 `BuildSystem.c` 与 `Main.c:485` 之一，并在 `BuildSystem.h:19` 注释中写明返回值与 `KrtBuildGetError` 的配套关系。
- 备注：本条为头文件契约缺失直接导致的调用方语义反转，非单纯文档问题。

##### [高] 94-2｜`NativeAbi.h` 缺少 `extern "C"` 包裹，C++ 调用方符号不匹配
- 位置：`ArkLink/include/ArkLink/NativeAbi.h:6`
- 类别：1 契约破坏（语言链接约定缺失）
- 触发条件：任何 C++ 翻译单元 `#include "ArkLink/NativeAbi.h"` 并调用 `ark_native_abi_validate`。
- 后果：该头没有 `#ifdef __cplusplus extern "C" { #endif`，声明按 C++ 链接处理（名字修饰），而 `ArkLink/src/Core/NativeAbi.c:259` 以 C 链接定义该符号，链接期报未定义符号。同目录 `Arklink.h:7-9`、`Context.h:6-8`、`LinkerNative.h:6-8`、`BackendElf.h:7-9`、`BackendPe.h:7-9` 全部带该包裹，唯 `NativeAbi.h` 遗漏。
- 最小修复：在 `#include "Loader.h"` 后补 `#ifdef __cplusplus extern "C" { #endif`，文件末尾补 `#ifdef __cplusplus } #endif`，与同目录头保持一致。
- 备注：该头是 `include/ArkLink/` 下的公开 API 头，且被 `Resolver.c` 使用；当前工程为纯 C，故为潜在链接破坏而非现网崩溃。

##### [中] 94-3｜StubInclude 与真实 Arklink.h 共用同一包含守卫 `ARKLINK_ARKLINK_H`
- 位置：`Re.KrtC/StubInclude/ArkLink/Arklink.h:1-2`、`ArkLink/include/ArkLink/Arklink.h:1-2`
- 类别：5 包含守卫撞名
- 触发条件：同一翻译单元同时包含这两个路径（编译器前端的 StubInclude 搜索路径与真实头搜索路径同时生效时即可能发生）。
- 后果：两个不同文件使用完全相同的守卫宏，后包含者被静默跳过，编译器不会报重定义，也不会提示缺失声明；一旦 stub 与真实头发生漂移，前端解析到的声明与实际链接的符号不一致且无任何编译期信号。
- 最小修复：为 stub 使用独立守卫（如 `ARKLINK_STUB_ARKLINK_H`），或让 stub 直接 include 真实头；至少在两文件头注释中互相指明守卫必须同步。
- 备注：本批逐条比对确认两者当前枚举值、函数签名完全一致（stub 仅缺 12 条 `@brief` 注释），故当前无语义分叉，属潜在撞名风险。

##### [中] 94-4｜`KrtBuildContext` 的 `config`/`platform` 所有权未约定
- 位置：`Re.KrtC/src/Compiler/Build/BuildSystem.h:11-15`
- 类别：1 契约破坏（所有权约定缺失）
- 触发条件：调用方按「`KrtBuildContextCreate` 接管 config/platform」或「`KrtBuildContextDestroy` 会释放它们」的假设编写代码。
- 后果：结构体把 `KrtConfig* config; KrtPlatform* platform;` 直接暴露为公开字段，但头文件没有任何所有权说明；实现 `BuildSystem.c:20-25` 的 `KrtBuildContextDestroy` 只 `KRT_FREE(ctx)`，不释放 `config`/`platform`。调用方若自行释放即双重释放，若依赖 destroy 释放则泄漏。
- 最小修复：在结构体或 `KrtBuildContextCreate` 声明处注释「ctx 不拥有 config/platform，生命周期由调用方维护」。
- 备注：`KrtBuildContextCreate` 对 `config`/`platform` 也未做 NULL 校验（`BuildSystem.c:6-18` 直接存入），头文件未声明该前置条件。

##### [中] 94-5｜`x86_optimize_peephole` 定义但全仓库无调用，整个 peephole 阶段为死代码
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86CodeOpt.h:14`
- 类别：2 死代码（定义无调用）
- 触发条件：编译任何走 X86 后端的目标时。
- 后果：`x86_optimize_peephole` 在 `X86CodeOpt.c:428` 实现（内部还按 `optimization_level` 决定 3/5 趟），但全仓库（含 Test/）没有任何调用点，X86 代码生成流程从不执行 peephole 优化；`optimization_level` 在该路径上完全无效。`KrtX86Generate`（`X86Codegen.c:1053`）也未引用优化器。
- 最小修复：在 `KrtX86Generate` 的合适阶段调用 `x86_optimize_peephole(module, optimization_level)`，或删除该阶段及其头文件声明。
- 备注：`x86_optimize_peephole` 内部调用的 `x86_peephole_optimizer_create/destroy`（`X86CodeOpt.c:433/457`）只服务于这条死路径。

##### [中] 94-6｜`codegen_alloc_register`/`codegen_free_register` 无调用，寄存器跟踪子系统失效
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:73`、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:75`
- 类别：2 死代码（定义无调用）
- 触发条件：任何 X86 代码生成。
- 后果：两函数在 `X86Codegen.c:201/223` 有完整实现，但全仓库无调用点；依赖它们的 `RegisterState registers[X86_MAX_REGISTERS]`（`X86Codegen.h:47`）、`RegisterState.content` 字段因此成为死数据。寄存器分配实际由 `X86RegAlloc.c` 的 `regalloc_allocate_with_capacity`/`x86_allocate_registers` 完成，两套机制并存且头文件把死 API 作为公开接口暴露，易被误用。
- 最小修复：将 `codegen_alloc_register/free_register` 接入 `X86Codegen.c` 的寄存器分配路径，或删除声明、定义及 `registers`/`RegisterState`。
- 备注：`codegen_get_temp_location`/`codegen_set_temp_*`/`codegen_emit_*` 均有调用，仅这两个寄存器管理函数孤立。

##### [中] 94-7｜`ark_backend_elf_link` 声明缺少所有权/错误契约，与 BackendPe.h 不对称
- 位置：`ArkLink/include/ArkLink/BackendElf.h:11`
- 类别：1 契约破坏（所有权约定缺失）/ 3 注释与实现脱节
- 触发条件：调用方以 `BackendPe.h:11` 的约定（"the caller owns successful image and section-map allocations"）使用 ELF 后端时。
- 后果：`BackendPe.h:11` 明确写出成功时调用方拥有 image 与 section-map 分配，`BackendElf.h:11` 对同名同构 API 无任何说明；`ArkBackendOutput` 的所有权、失败时是否已释放、`ctx` 是否为 NULL 容忍等前置条件均未文档化。两个后端头一个有一个没有，调用方无法据此编写正确的清理代码。
- 最小修复：在 `BackendElf.h:11` 前补与 `BackendPe.h:11` 相同的 `@brief`，并补充 NULL/错误码约定。
- 备注：实现位于 `ArkLink/src/Backend/BackendElf.c:2055`；`BackendElf.h` 与 `Backend.h:195` 存在重复声明（签名一致，无冲突）。

##### [低] 94-8｜`ARK_LINK_FLAG_VERBOSE`/`ARK_LINK_FLAG_QUIET` 无任何引用且无 setter
- 位置：`ArkLink/include/ArkLink/Arklink.h:11-14`
- 类别：2 死代码（声明无使用）
- 触发条件：任何需要控制 ArkLink 日志级别的调用方。
- 后果：匿名枚举定义了 `ARK_LINK_FLAG_VERBOSE = 1u << 0`、`ARK_LINK_FLAG_QUIET = 1u << 1`，但全仓库（含 `ArkLink/src/Main.c`、`Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.c`）无任何引用；`Arklink.h` 也没有 `arklink_session_set_flags` 之类的接口，`arklink_session_set_logger` 无级别过滤参数。两个公开枚举值不可达。
- 最小修复：实现 flag setter 并在 `Main.c` 解析 `-v/-q`，或删除这两个枚举值。
- 备注：`Main.c` 的 `logger_callback` 对所有级别一律打印。

##### [低] 94-9｜`X86_MAX_LABEL_LEN` 定义后无使用
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:13`
- 类别：2 死代码（定义无使用）
- 触发条件：—
- 后果：宏 `X86_MAX_LABEL_LEN 256` 全仓库仅在此处出现，`X86Codegen.c` 生成标签时未使用该上限，标签缓冲区长度无约束。
- 最小修复：删除该宏，或用于标签名缓冲区上限。
- 备注：同文件 `X86_MAX_VAR_NAME_LEN`、`X86_MAX_TEMP_SPACE` 等均有使用，仅此宏孤立。

##### [低] 94-10｜`X86_MAX_REGISTERS` 与 `X86_REGISTER_COUNT` 同值重复定义（配置漂移）
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:8`、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.h:12`
- 类别：8 配置漂移
- 触发条件：维护者只改动其中一个宏的值时。
- 后果：两个宏都等于 14，但分别用于不同位置——`registers[X86_MAX_REGISTERS]`（`X86Codegen.h:47`）与 `g_register_count = X86_REGISTER_COUNT`（`X86Codegen.c:9`）。任一处单独调整即造成数组容量与寄存器计数不一致，引发越界或寄存器漏用，且无任何编译期关联约束。
- 最小修复：`#define X86_REGISTER_COUNT X86_MAX_REGISTERS`，或合并为单一宏。
- 备注：当前值相同，属潜在漂移而非现网错误。

##### [低] 94-11｜`x86_peephole_get_optimization_count` 定义但无调用
- 位置：`Re.KrtC/src/Compiler/Backend/X86/X86CodeOpt.h:16`
- 类别：2 死代码（定义无调用）
- 触发条件：—
- 后果：该访问器在 `X86CodeOpt.c:460` 有定义，全仓库无调用点；与 94-5 同属已废弃的 peephole 公开 API。
- 最小修复：随 94-5 一并接入或删除。
- 备注：—

##### [低] 94-12｜`KROCodegenContext.current_phi_inst`、`phi_operand_index` 字段无任何读写
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h:83`、`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h:84`
- 类别：2 死代码（死字段）
- 触发条件：—
- 后果：两个字段在全仓库（含 `KroCodegen.c` 与全部 `.inc`）仅出现在头文件声明处，无任何赋值或读取；每个 `KROCodegenContext` 实例浪费 16 字节，并暗示存在未实现的 phi 处理路径，误导维护者。
- 最小修复：删除这两个字段；若确有 phi 支持计划，应补注释说明并加 `(void)` 使用点。
- 备注：同结构体其余字段（`tail_combine`、`tail_return`、`rotated_tail_leaf` 等）均有使用。

##### [低] 94-13｜`backend_allocate` 系列在 `KRT_COMPILE_ERROR` 后的 `return` 不可达
- 位置：`Re.KrtC/src/Compiler/Backend/BackendAllocation.h:12`、`Re.KrtC/src/Compiler/Backend/BackendAllocation.h:20`、`Re.KrtC/src/Compiler/Backend/BackendAllocation.h:28`
- 类别：2 死代码（不可达语句）/ 3 注释与实现脱节
- 触发条件：任何分配失败路径。
- 后果：`KRT_COMPILE_ERROR` 在 `Core/Utils/KrtCommon.h:60-64` 展开为 `fprintf(...); exit(1);`（`:64` 为 `exit(1)`），因此 `return allocation;` 永不执行；`if (!allocation)` 分支本身可达（`KrtSafeMalloc` 失败会返回 NULL，见 `Core/Memory/Allocator.c:161` 等），但「返回 NULL 给调用方」的设计意图与宏的退出语义矛盾。
- 最小修复：删除不可达的 `return allocation;`，或改用可返回错误码的宏使头文件契约自洽。
- 备注：`backend_allocate_zeroed` 的 `(count ? count : 1, size ? size : 1)` 与 `KRT_CALLOC(count, size)` 参数序一致，无错误。

##### [低] 94-14｜`arklink_session_link_native` 注释所称 "native platform linker" 与实现不符
- 位置：`ArkLink/include/ArkLink/LinkerNative.h:10`
- 类别：3 注释与实现脱节
- 触发条件：调用方依据注释理解该函数行为时。
- 后果：注释写 "Link session inputs using the native platform linker"，但实现 `ArkLink/src/Core/LinkerNative.c:33-175` 并不调用系统链接器（ld/link.exe），而是自行 `calloc` 单元、调用 `ark_loader_load_unit`、`ark_resolver_resolve`，再分派到 ArkLink 自带的 `ark_backend_pe_link`/`ark_backend_elf_link`（`:106/109`）。函数名 `link_native` 与注释共同误导 API 使用者。
- 最小修复：注释改为 "using ArkLink's PE/ELF backends"，或更名函数。
- 备注：函数签名与实现一致，非链接错误。

##### [低] 94-15｜`KrtKrtGenerate` 命名前缀重复，疑似 `KrtKroGenerate` 笔误
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.h:88`
- 类别：9 其它静态审计（命名一致性）
- 触发条件：按名称检索/调用 KRO 后端入口时。
- 后果：项目约定为 `Krt<模块>Generate`（`KrtX86Generate`、`KrtVmCodegenGenerate`），此处为 `Krt` + `Krt` + `Generate`，与 KRO 后端（KroCodegen.c:1470）不符，易被误认为另一 API，也给自动化检索/重构带来噪音。
- 最小修复：重命名为 `KrtKroGenerate`，同步 `KroCodegen.c:1470` 与 `Compiler.c:243,247` 两个调用点。
- 备注：声明与定义一致，当前无链接错误。

##### [低] 94-16｜`KrtBuildContext.error_message` 为公开可变固定缓冲，`KrtBuildGetError` 返回其裸指针
- 位置：`Re.KrtC/src/Compiler/Build/BuildSystem.h:14`、`Re.KrtC/src/Compiler/Build/BuildSystem.h:20`
- 类别：9 其它静态审计（封装/契约）
- 触发条件：调用方在 `KrtBuildExecute` 返回后读取错误消息，或自行写入 `ctx->error_message`。
- 后果：结构体公开 `char error_message[256]`，调用方可直接改写 `KrtBuildGetError` 将要返回的内容；`BuildSystem.c:33,39` 等用 `snprintf` 写入时超长消息被静默截断，头文件未给出长度上限或截断约定。
- 最小修复：把 `error_message` 移入不透明结构体（`BuildSystem.c` 内定义），或注释明确「调用方不得修改，缓冲 256 字节，超长截断」。
- 备注：`KrtBuildGetError` 对空消息返回字面量 `"Build failed"`（`BuildSystem.c:80`），头文件未说明。

##### [低] 94-17｜`VmCodegen.h` 以裸文件名包含两个分处异地的头，隐式依赖外部 `-I`
- 位置：`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.h:5`、`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.h:6`
- 类别：6 阶段不对齐 / 9 其它静态审计（包含路径脆弱）
- 触发条件：构建系统未把 `Re.KrtC/src/Bytecode` 与 `Re.KrtC/Shared` 同时加入包含路径时。
- 后果：`VmCodegen.h` 位于 `Compiler/Backend/Vm/`，却 `#include "Bytecode.h"`（实际在 `Re.KrtC/src/Bytecode/Bytecode.h`）与 `#include "BytecodeGenerator.h"`（实际在 `Re.KrtC/Shared/BytecodeGenerator.h`）。两个被依赖头都不在包含文件所在目录，必须依赖外部 `-I` 才能解析；`Vm/` 目录内仅有 `VmCodegen.c/.h`。一旦构建包含路径调整即编译失败，且从头部本身无法看出依赖位置。
- 最小修复：改用相对路径（如 `"../../../../Bytecode/Bytecode.h"`、`"../../../../Shared/BytecodeGenerator.h"`），或把两个头统一到同一目录。
- 备注：`Bytecode.h` 定义 `KrtChunk`（`:45`）、`BytecodeGenerator.h` 声明 `KrtBytecodeGeneratorWriteByte`（`:9`），均为 `KrtVmCodegenGenerate` 的必需类型。

### 第九十五批：Driver/Frontend/Core/Tools 头文件

#### 覆盖范围

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


##### [致命] 95-1｜聚合头与 Driver 头共用包含守卫 KRT_COMPILER_H，导致 Compiler.h 被整体吞掉
- 位置：`Re.KrtC/src/Compiler/Compiler.h:1-2` 与 `Re.KrtC/src/Compiler/Driver/Compiler.h:1-2`（聚合头在第 13 行包含 Driver 头）
- 类别：包含守卫撞名 / 契约破坏
- 触发条件：任一翻译单元先包含 `Compiler/Driver/Compiler.h` 再包含 `Compiler/Compiler.h`（或反之），第二次包含因守卫已定义而被预处理器整体跳过。
- 后果：`Main.c:28-29` 正是先 Driver 后聚合，聚合头 `Compiler.h` 的全部内容（含对 `Driver/Compiler.h` 的包含）被静默丢弃；`KrtCompiler`、`KrtTargetPlatform`、`KrtCompilerCreate/Destroy/Compile` 在聚合头路径下完全不可见。当前 Main.c 因逐个头显式包含而未暴露，但任何只包含聚合头的翻译单元会直接编译失败或退化为隐式声明，聚合头作为"总入口"的契约被破坏。
- 最小修复：把 `Compiler/Driver/Compiler.h` 的守卫改为 `KRT_DRIVER_COMPILER_H`（或把聚合头改为 `KRT_COMPILER_AGGREGATE_H`），二者不得同名。
- 备注：全仓库 grep `KRT_COMPILER_H` 仅命中这两个头，无第三方占用，改名安全。

##### [高] 95-2｜Parser.h 中 13 个 parser_parse_* 声明在全仓库无定义、无调用
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/Parser.h:32-47`
- 类别：死代码（声明无定义）/ 契约破坏
- 触发条件：任何外部代码按头文件声明调用这些函数；或审计/工具按声明生成绑定。
- 后果：`parser_parse_try_statement`、`parser_parse_catch_clause`、`parser_parse_finally_clause`、`parser_parse_throw_statement`、`parser_parse_lock_statement`、`parser_parse_yield_statement`、`parser_parse_using_statement`、`parser_parse_template_declaration`、`parser_parse_template_parameter`、`parser_parse_case_clause`、`parser_parse_default_clause`、`parser_parse_break_statement`、`parser_parse_continue_statement` 全部只有声明，仓库内无定义（`break`/`continue`/`case` 实际在 `parser_parse_statement` 内联处理）。一旦被调用即链接错误；同时声明与实现脱节，掩盖了这些语句并未走独立函数的事实。
- 最小修复：删除未实现声明；若确为规划中的 API，则补定义并加 `#ifdef` 标注未完成状态。
- 备注：全仓库 grep 这 13 个符号，除 Parser.h 外零命中。

##### [高] 95-3｜ParserStatement.h 声明的两个函数与实现不符：一个被 static 隐藏，一个根本不存在
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.h:6-7`；实现见 `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:180`（`static ASTNode* parser_parse_variable_declaration`）、`ParserStatement.c:490`（`static ... parser_parse_assignment_from_left`）
- 类别：契约破坏（声明与实现不符）/ 死声明
- 触发条件：其他翻译单元按头文件调用 `parser_parse_variable_declaration` 或 `parser_parse_assignment`。
- 后果：`parser_parse_variable_declaration` 在头文件中是外部可见符号，实现却是 `static`，外部调用会链接失败；`parser_parse_assignment` 只有声明、全仓库无定义（实际实现是 `parser_parse_assignment_from_left`），调用必然未定义符号。
- 最小修复：把 `parser_parse_variable_declaration` 的 `static` 去掉并同步签名；删除 `parser_parse_assignment` 声明或补上实现。
- 备注：`ParserStatement.c` 内部大量调用的是 `parser_parse_variable_declaration_with_type` / `parser_parse_assignment_from_left`，均未在头部暴露。

##### [高] 95-4｜SemanticAnalyzer.h 中 6 个声明在全仓库无定义、无调用
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.h:85,87,88,95,99,100`
- 类别：死代码（声明无定义）/ 契约破坏
- 触发条件：按声明调用 `semantic_analyzer_check_entry_point`、`semantic_analyzer_resolve_forward_references`、`semantic_analyzer_get_result`、`semantic_analyzer_add_error_ex_at`、`semantic_analyzer_get_symbol_type_name`、`semantic_analyzer_is_valid_symbol_name`。
- 后果：六个公开 API 只有声明没有定义，调用即链接错误。其中 `semantic_analyzer_get_result` 与 `semantic_analyzer_analyze` 同为返回 `SemanticAnalysisResult*`，功能重叠，暗示一次未完成的重构；`semantic_analyzer_add_error_ex_at` 带 `KrtErrorStage` 参数，本应是错误阶段上报的入口，缺失后阶段信息只能走 `add_error_at` 的默认 SEMANTIC。
- 最小修复：删除未实现声明；若需要则补定义（尤其 `add_error_ex_at` 与阶段枚举配套）。
- 备注：全仓库 grep 这 6 个符号仅命中头文件本身。

##### [高] 95-5｜`#include "Bytecode.h"` 同名同守卫、不同布局，KrtChunk 定义跨翻译单元不一致
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.h:20`（触发点；`Compiler.h:28` 的 `KrtChunk last_chunk` 字段）
- 类别：包含守卫撞名 / 语义分叉 / ODR 违反
- 触发条件：`src/Bytecode/Bytecode.h` 与 `Shared/Bytecode.h` 均以 `KRT_BYTECODE_H` 为守卫（分别见 `src/Bytecode/Bytecode.h:1-2`、`Shared/Bytecode.h:1-2`），且都定义 `KrtChunk`；`CMakeLists.txt:20` 的包含顺序为 `src src/Core src/Tools src/Bytecode Shared ...`，`src/Bytecode` 先于 `Shared`。
- 后果：`Compiler.c`/`VmCodegen.c` 经 `-I src/Bytecode` 取到 `src/Bytecode/Bytecode.h`（其 `KrtValueArray` 字段顺序为 `capacity,count,values`，见 `src/Bytecode/Value.h:41-44`），而 `Shared/BytecodeGenerator.c` 经相对包含取到 `Shared/Bytecode.h`（`KrtValueArray` 为 `count,capacity,values`，见 `Shared/Bytecode.h:66-70`）。两处 `KrtChunk.constants` 的 `count`/`capacity` 语义相反，跨 TU 操作同一对象时容量判断错位，存在堆越界/损坏风险；且因守卫同名，同一 TU 内后包含者被静默丢弃，问题不会以宏重定义告警的形式暴露。
- 最小修复：统一 `KrtValueArray`/`KrtChunk` 定义到唯一头文件；两个 `Bytecode.h` 使用不同守卫；包含改为可唯一解析的路径（如 `Bytecode/Bytecode.h`）并固定包含顺序。
- 备注：`src/Bytecode/Bytecode.h` 声明的 `KrtChunkInit/Write/Free/AddConstant/AddStringConstant` 全仓库无调用者，只有结构体被使用，说明该头基本被 Shared 版本取代但未清理。

##### [中] 95-6｜KRT_DEBUG_IR 宏引用了全仓库未定义的 KRT_DEBUG_LOG
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.h:9`
- 类别：注释与实现脱节 / 死代码
- 触发条件：任何翻译单元使用 `KRT_DEBUG_IR(...)`。
- 后果：`KRT_DEBUG_LOG` 在仓库中无定义（grep 仅命中该宏体自身），展开即编译错误；该宏目前是死宏， Driver 层缺少可用的 IR 调试输出通道。
- 最小修复：定义 `KRT_DEBUG_LOG(tag, fmt, ...)`（可复用 `KrtPrintf`/`KrtOutputCacheAdd`），或删除 `KRT_DEBUG_IR`。
- 备注：`Compiler.c` 中实际未使用该宏。

##### [中] 95-7｜版本号双源漂移：Version.h 与 KrtCommon.h 数值不一致
- 位置：`Re.KrtC/src/Version.h:4`（`KRT_COMPILER_VERSION "0.00.0006cc"`）；对照 `Re.KrtC/src/Core/Utils/KrtCommon.h:4-7`（`KRT_VERSION_MAJOR 1` / `MINOR 0` / `PATCH 0` / `KRT_VERSION_STRING "1.0.0"`）
- 类别：配置漂移
- 触发条件：CLI 打印版本（`Main.c:134`、`Main.c:1389`）或任何使用 `KRT_COMPILER_INFO` 的运行时信息。
- 后果：同一编译器的版本标识出现 `0.00.0006cc` 与 `1.0.0` 两套值，构建产物/日志/用户可见版本不一致，无法据版本号追溯产物。
- 最小修复：确立单一版本源（建议 KrtCommon.h），Version.h 由该源派生或删除；发布流程统一 bump。
- 备注：`KRT_COMPILER_INFO` 在仓库中无使用者，`KRT_VERSION_STRING` 目前仅服务于它。

##### [中] 95-8｜KrtErrorStage 阶段枚举与实现脱节：仅 SEMANTIC 会被产出
- 位置：`Re.KrtC/src/Compiler/Frontend/CompilerError.h:9-15`；唯一产出点 `Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.c:846,872`
- 类别：阶段不对齐 / 死枚举
- 触发条件：词法、语法、类型检查、代码生成、SSA 阶段产生错误时。
- 后果：`KRT_ERROR_STAGE_LEX`、`PARSE`、`TYPE_CHECK`、`CODEGEN`、`SSA`、`UNKNOWN` 在仓库中只出现于 `CompilerError.c` 的 switch 分支，没有任何生产者传入；所有语义错误一律上报为 `KRT_ERROR_STAGE_SEMANTIC`。错误分类、阶段统计与按阶段过滤全部失真。
- 最小修复：在 Lexer/Parser/Semantic/Codegen 各层调用 `KrtErrorReportAdd(AddEx)` 时传真实 stage；无法区分的阶段删除枚举值。
- 备注：`CompilerError.c:19-36` 的 `KrtErrorStageName` 已为这些值准备了名字，说明设计意图与实现脱节。

##### [中] 95-9｜KrtErrorReportAddEx 的扩展能力无生产者：end 位置与 error_code 永远为空
- 位置：`Re.KrtC/src/Compiler/Frontend/CompilerError.h:47-48`；唯一调用点 `Re.KrtC/src/Compiler/Frontend/CompilerError.c:136`
- 类别：死代码 / 契约破坏
- 触发条件：任何需要精确错误区间或错误码的调用方。
- 后果：`KrtErrorReportAdd` 调用 `AddEx` 时把 `end_line/end_column` 填成与 `line/column` 相同、`error_code` 填 `""`，仓库中再无其他 `AddEx` 调用者。于是 `KrtCompileError.end_line/end_column/error_code` 成为永不生效的死字段，错误定位退化为单点，`AddEx` 名不副实。
- 最小修复：让 SemanticAnalyzer 在 `add_error_at` 系列里传递真实 end 位置与错误码并走 `AddEx`；否则删除 `AddEx` 及相关字段。
- 备注：`CompilerError.c:267` 已按 `end_line/end_column` 打印，说明消费端就绪、生产端缺失。

##### [中] 95-10｜NameMangling.h 的 NameManglingResult 类型与 4 个公开函数无使用者
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.h:6-10`（`NameManglingResult`）、`:25`（`name_mangle_from_ast`）、`:30`（`name_mangle_class_member`）、`:32,34`（`name_demangle`/`name_demangle_free`）
- 类别：死代码 / 语义分叉
- 触发条件：任何外部模块使用该反混淆/成员名拼接 API。
- 后果：`NameManglingResult` 结构体全仓库零引用；`name_mangle_from_ast`、`name_mangle_class_member` 除自身文件内的相互调用外无外部调用者；`name_demangle`/`name_demangle_free` 仅在其自身错误清理路径被调用，未形成对外的反混淆能力。公开头承诺的 API 实际不可用/未接线。
- 最小修复：删除未用声明与类型；若反混淆是规划功能，应在符号表/错误显示处接线。
- 备注：`name_mangle_names` 虽在头文件公开，实际只被同文件其他 mangle 函数调用，属"半内部"API。

##### [中] 95-11｜项目/库配置三处重复建模，所有权类型不一致
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.h:7-17`（`KrtProjectKrtConfig`：`libraries/library_count` 为 `char**`）；`Re.KrtC/src/Compiler/Driver/ConfigManager.h:22-23`（`KrtConfig`：`libraries` 为 `const char* const*`、`library_count`）；`Re.KrtC/src/Compiler/Driver/Project.h:43-45`（`KrtProject`：`items/dependencies`）
- 类别：配置漂移 / 语义分叉
- 触发条件：任一需要读取项目库列表的模块（如 ArkLinkIntegration、ParallelCompiler）。
- 后果：同一"项目—库"信息存在三套表示，字段名相同而所有权/常量性不同（`char**` vs `const char* const*`），无单一事实来源，转换时易漏释放或悬垂；也是 95-12 中 `KrtProjectKrtSave` 无调用者的背景原因。
- 最小修复：确立唯一项目配置结构，其余层通过访问函数或 `const` 视图引用，禁止重复字段。
- 备注：`Main.c` 同时使用 `KrtProjCreate`（`Main.c:76`）与 `KrtProjectKrtLoad`（`Main.c:923,1156`），印证双模型并存。

##### [低] 95-12｜KrtProjectKrtSave 定义但全仓库无调用者
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.h:21`；定义 `Re.KrtC/src/Compiler/Driver/ProjectKrt.c:797`
- 类别：死代码（定义无调用）
- 触发条件：需要把 `KrtProjectKrtConfig` 写回 `.krt` 文件时。
- 后果：`.krt` 工程文件只能读不能写，保存/更新项目配置的功能缺失；`ProjectKrt.c` 中为此实现的序列化逻辑成为死代码。
- 最小修复：接入保存命令（如 `krt build --save`）或删除该函数及其实现。
- 备注：全仓库 grep `KrtProjectKrtSave` 仅命中头与定义。

##### [低] 95-13｜KrtArkLinkSetEntryPoint 定义但无调用者，入口点实际由 linker_config 传递
- 位置：`Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.h:18`；定义 `Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.c:167`；实际入口点设置点 `ArkLinkIntegration.c:65-66`
- 类别：死代码（定义无调用）
- 触发条件：调用方试图用 `KrtArkLinkSetEntryPoint` 覆盖入口点。
- 后果：公开 API 无调用者，入口点改由 `KrtLinkerConfig::entry_point` 在 `KrtArkLinkContextCreate` 内设置；调用者若误以为该函数必须调用，会写出无效代码，且 `KrtConfig::entry_point` 字段实际无人读取。
- 最小修复：删除该函数，或在 `KrtArkLinkLinkObjects` 中按 `config->entry_point` 调用它。
- 备注：全仓库 grep 仅命中头与定义两处。

##### [低] 95-14｜Logger.h 与 OutputCache.h 重复声明同一打印 API，且 Logger.h 缺格式属性
- 位置：`Re.KrtC/src/Core/Utils/Logger.h:7-9`；`Re.KrtC/src/Core/Utils/OutputCache.h:39-41`
- 类别：契约重复 / 配置漂移
- 触发条件：同时包含两个头的翻译单元（如经 `Core/Core.h` 与显式包含）。
- 后果：`KrtPrintf/KrtPrintFormat/KrtFprintf` 两处声明，`OutputCache.h` 带 `KRT_PRINTF_FORMAT` 而 `Logger.h` 不带；格式串检查只在包含 OutputCache.h 的路径生效，含 Logger.h 的路径失去编译期校验。两处需人工保持同步。
- 最小修复：Logger.h 的声明补齐 `KRT_PRINTF_FORMAT` 并包含 `Attributes.h`，或改为直接包含 OutputCache.h 并删除重复声明。
- 备注：实现只在 `Core/Utils/Logger.c:68,112,129`。

##### [低] 95-15｜ConsoleUtils.h 的 ANSI_SAVE_CURSOR / ANSI_RESTORE_CURSOR 宏无使用者
- 位置：`Re.KrtC/src/Compiler/Driver/ConsoleUtils.h:16-17`
- 类别：死代码
- 触发条件：需要保存/恢复光标的终端输出。
- 后果：两个宏在全仓库仅定义处出现，无调用者；`ANSI_CURSOR_UP`/`ANSI_CLEAR_LINE` 则被 TaskManager.c 使用，说明光标控制只实现了一半。
- 最小修复：删除未用宏，或在 TaskManager 的光标控制中补齐使用。
- 备注：全仓库 grep 仅命中定义行。

##### [低] 95-16｜Tokenizer.h 存在空的 `#ifdef _WIN32 ... #endif` 块
- 位置：`Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.h:8-10`
- 类别：死代码 / 配置漂移
- 触发条件：在 `_WIN32` 下编译时。
- 后果：平台分支内无任何声明，属被清空但未清理的占位；同时 `<stdio.h>`/`<stdlib.h>` 紧随其后，读者会误以为存在平台相关声明。
- 最小修复：删除该空块，或补齐曾经存在的平台相关声明。
- 备注：`Lexer` 结构体本身无平台差异。

##### [低] 95-17｜KrtOutputCacheSetEnabled 定义但无调用者，缓存开关不可达
- 位置：`Re.KrtC/src/Core/Utils/OutputCache.h:37`；定义 `Re.KrtC/src/Core/Utils/OutputCache.c:199`
- 类别：死代码（定义无调用）
- 触发条件：需要开启/关闭输出缓存时。
- 后果：`g_output_cache.enabled` 仅在 `KrtOutputCacheInit`（`OutputCache.c:11-13`）中被置 0，运行期无法改变；缓存功能实际恒定关闭，`SetEnabled` 成为不可达 API。
- 最小修复：在 CLI/配置层接入开关，或删除该函数。
- 备注：全仓库 grep `KrtOutputCacheSetEnabled` 仅命中头与定义。

##### [低] 95-18｜KrtCompilerCompile 以 void* 传递 semantic_analyzer，类型擦除
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.h:36`
- 类别：契约破坏（类型不安全）
- 触发条件：调用方传入非 `SemanticAnalyzer*` 的指针。
- 后果：编译期无法发现误传；实现 `Compiler.c:163` 直接把该 `void*` 转交 `KrtIrGenerateFromAst`，错误类型会延迟到运行期。注释所称 "borrowed analyzer" 的所有权约定也无编译期保障。
- 最小修复：在头文件中前向声明 `typedef struct SemanticAnalyzer SemanticAnalyzer;`，参数改为 `SemanticAnalyzer*`。
- 备注：`KrtCompiler` 结构体未回指该 analyzer，borrow 关系仅靠注释表达。

##### [低] 95-19｜NameMangling 各函数返回 char* 但未约定所有权与释放责任
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.h:19-34`
- 类别：契约破坏（所有权约定缺失）
- 触发条件：调用方使用 `name_mangle_simple/constructor/names/from_ast/function/class_member` 的返回值。
- 后果：头文件未注明返回值是否需 `free`、由谁释放；实现返回堆分配串（`NameMangling.c` 各 `return KRT_STRDUP/分配串`），调用方（IrGen.c、SemanticAnalyzer.c）只能靠约定 `KRT_FREE`，一旦新调用方遗漏即泄漏，误用 `free` 于静态串则崩溃。
- 最小修复：为每个返回 `char*` 的函数补充 `@brief 返回堆分配串，调用方负责 KRT_FREE` 注释，或改为调用方传入缓冲区。
- 备注：`KrtMangleTypeName` 已正确标注为不可变 ABI 编码，可作为注释范式。

##### [低] 95-20｜KroWriter 写函数无失败通道，reloc 字段宽度与结构体不一致
- 位置：`Re.KrtC/src/Tools/KroWriter.h:79-84`（`kro_write_code/data/rodata(_aligned)` 返回 `uint32_t`）、`:94-95`（`kro_add_reloc` 的 `uint16_t type, int16_t addend`）；结构体字段见 `:65-70`（`uint32_t type; int32_t addend`）
- 类别：契约破坏（错误码约定缺失）/ 语义分叉
- 触发条件：写入过程中发生分配失败，或新增超出 16 位的重定位类型/加数。
- 后果：`kro_write_*` 用 `uint32_t` 同时表达偏移与错误，无文档说明失败返回值，调用方（KroCodegen.c:182,190,194,1435,1464）无法区分成功偏移与失败；`kro_add_reloc` 把类型/加数收窄为 16 位，与写入结构体的 32 位字段不匹配，未来扩展会被静默截断。
- 最小修复：写函数改为 `bool` + 出参偏移，或定义 `KRO_WRITE_FAILED` 常量并注释；reloc 形参宽度与 `KRORelocation` 对齐。
- 备注：`kro_write_file` 已用 `bool` 表达成功，可作为范式。

### 第九十六批：examples/http-server + Test/Quality

#### 覆盖范围

- `examples/http-server/Main.krt`（3 行，全文读完）
- `examples/http-server/Server.krt`（258 行，全文读完）
- `Test/Quality/` 下全部 `.c/.h/.inc/.krt`：`test_backend_allocation.c`(179)、`test_elf_failure.c`(219)、`test_frontend_allocation.c`(100)、`test_ir.c`(260)、`test_linker.c`(121)、`test_memory.c`(157)、`test_memory_benchmark.c`(40)，合计 1076 行，逐个全量读完。
- 目录内 `.py`（RunTests.py/Audit.py/Benchmark.py/CheckStandards.py/test_native.py/test_standards.py）不在授权读取范围，仅对 `RunTests.py` 做过两次定点 grep 以核对调用参数，未通读。
- 全批无「致命」级；最高为「高」。共 42 条。


##### [中] 96-1｜示例工程零测试覆盖，Run 的端口契约与退出码无人验证
- 位置：`examples/http-server/Main.krt:2`
- 类别：覆盖缺口
- 触发条件：任何人修改 `HttpServer.Run` 的 `PORT=` 输出格式、就绪时序或返回码后直接合入。
- 后果：示例的行为契约（先 bind/listen 再打印 `PORT=`、连接上限用尽返回 0）没有任何断言保护，编译通过即等于验收。
- 最小修复：新增示例测试，以 `Run(0, 1, &handler)` 驱动一次连接，捕获 stdout 解析 `PORT=`，断言端口可连通、退出码为 0；或对 `Run` 做返回码表驱动用例。
- 备注：本条只针对该示例目录无任何测试的事实，不涉及 Main.krt 代码本身错误。

##### [高] 96-2｜Run 丢弃 HandleClient 的返回值，连接级失败全部静默，退出码仍为 0
- 位置：`examples/http-server/Server.krt:247`（失败源头 `Server.krt:170`、`Server.krt:172`）
- 类别：静默通过
- 触发条件：任一连接上 `SendResponse`/`Respond` 发送失败（EPIPE、EAGAIN 超时）、或 handler 抛错走 `catch` 分支。
- 后果：`HandleClient` 的 bool 结果被丢弃，`Run` 既不计数也不上报，正常结束时仍 `return 0`；调用方（含 Main.krt:2）拿到 0 会认为服务健康，与实际错误不符。
- 最小修复：记录失败数（如 `int32 failed++`），`Run` 结束按失败数返回非 0，或至少输出一行错误诊断。
- 备注：`Server.krt:250` 只检查 `configured/closed`，同样未看 HandleClient 结果。

##### [中] 96-3｜SendResponse 未校验 handler 返回的 body，空/default body 直接进入字符串取长
- 位置：`examples/http-server/Server.krt:82`
- 类别：契约破坏/不安全用法
- 触发条件：自定义 handler 返回 `status` 合法（200–599）但未赋 `body`（如 `default(HttpResponse)` 后只改 status），或 body 为 null。
- 后果：`Sys.InternalStringLength(response.body)` 作用于空引用，行为未定义（崩溃或长度错），后续 `InternalStringPtr` 与 Content-Length 全部失真；状态码合法性检查（79-81）帮不上，因为它只挡非法 status。
- 最小修复：在取长前判空（null 视为空串或统一回落 `Respond(client,500)`），并把「status 与 body 必须成对」写进 handler 契约注释（122-123 行已有借用电注释，需补所有权/非空约束）。
- 备注：`DefaultHandler`（115-120）自身总赋 body，所以只有自定义 handler 会踩。

##### [中] 96-4｜accept 任一错误即关闭 listener 退出，瞬时资源耗尽被当成永久故障
- 位置：`examples/http-server/Server.krt:239-244`
- 类别：可用性/错误处理
- 触发条件：`accept` 返回 `EMFILE`/`ENFILE`/`ENOBUFS` 等瞬时错误（fd 被瞬时打满）。
- 后果：服务端直接关闭监听套接字并 `return 7`，整个进程退出；本可稍后重试的瞬时故障变成永久拒绝服务，且无诊断。
- 最小修复：对可恢复错误 `continue`（可加退避与计数上限），仅对不可恢复错误关闭退出；至少在退出前打印 errno。
- 备注：`client == -4`（EINTR）已在 240 行 continue，说明作者已区分过一类可重试错误。

##### [中] 96-5｜handler 在 accept 线程同步执行且无隔离，单个阻塞 handler 瘫痪整个服务
- 位置：`examples/http-server/Server.krt:169`（分发点 `Server.krt:247`）
- 类别：示例中的不安全用法/契约破坏
- 触发条件：使用者按 `Run(port, limit, &handler)` 传入执行阻塞 I/O 或死循环的闭包。
- 后果：单线程事件循环被独占，后续 accept 与已排队连接全部停摆，`connection_limit` 形同虚设；示例没有任何「handler 必须快速返回」的说明或超时机制。
- 最小修复：在 `Run` 的文档注释中明确 handler 不得阻塞，或提供每连接超时/看门狗；示例层面至少补一条契约警告。
- 备注：作为教学示例，这个契约会直接被模仿到生产代码。

##### [低] 96-6｜SendBytes/recvfrom 对 EINTR 无上限重试
- 位置：`examples/http-server/Server.krt:39`、`examples/http-server/Server.krt:132`
- 类别：竞态与非确定性
- 触发条件：进程安装了不带 `SA_RESTART` 的信号处理且信号持续到达。
- 后果：`count == -4` 时 `continue` 形成无退避忙等，`written`/`length` 不前进，连接与服务器都被拖住。
- 最小修复：加重试计数上限并在超限时按失败返回，或改用 `MSG_NOSIGNAL` 之外的自动重启语义。
- 备注：两处都是同一模式，属示例代码的可重试性缺陷。

##### [低] 96-7｜退出码 4–9 无任何诊断文本，调用方无法区分失败原因
- 位置：`examples/http-server/Server.krt:217`、`:223`、`:229`、`:243`、`:252`、`:255`
- 类别：诊断与实际错误不符
- 触发条件：socket/setopt/bind/listen/getsockname/accept/close 任一失败。
- 后果：只有结构布局错误（198、210）会打印消息，其余失败仅以 4..9 的数字静默返回；Main.krt 原样透传，排障者看不到 errno 或阶段信息。
- 最小修复：每个返回点打印对应阶段与 errno（或统一错误表），保持退出码不变。
- 备注：与 96-4 同源，但本条关注可诊断性。

##### [低] 96-8｜硬编码 Linux x86-64 系统调用号与 loopback 地址，示例不可移植
- 位置：`examples/http-server/Server.krt:24-32`、`:201-203`
- 类别：可移植性
- 触发条件：在非 x86-64 Linux（macOS/ARM/Windows/WSL 差异）上编译运行该示例。
- 后果：`SYS_socket=41` 等常量与 `int64` 直接 syscall 约定在其他架构/平台含义不同，轻则绑定失败，重则按错误 ABI 解释参数；尽管 191-212 做了结构体自检，但系统调用号本身没有平台开关。
- 最小修复：用条件编译把 syscall 号/结构限定在 `#if defined(__linux__) && defined(__x86_64__)`，否则给出明确的不支持提示。
- 备注：4 行注释已声明「Linux x86-64」，属已知取舍，故记低。

##### [中] 96-9｜test_backend_allocation.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_backend_allocation.c:171-178`
- 类别：测试断言缺失/静默通过
- 触发条件：以 `-DNDEBUG` 构建该测试（CI 换优化档位时常见）。
- 后果：`assert` 全部被消去后，三种致命分配、PE 全部分配点失败注入与寄存器压力检查都不再产生任何失败信号，`main` 仍 `return 0`，测试变绿。
- 最小修复：改用自带 `CHECK(cond)` 宏（失败即 `fprintf+return 1`），或在构建脚本固定 `-UNDEBUG` 并加断言计数。
- 备注：文件内 `printf` 只报数量，不参与判定（168 行）。

##### [中] 96-10｜三种致命分配共用同一条消息前缀断言，无法区分错误来源
- 位置：`Test/Quality/test_backend_allocation.c:121`（用例分支 105-111）
- 类别：断言过弱/诊断不符
- 触发条件：`backend_allocate`、`backend_allocate_zeroed`、`backend_reallocate` 中任一路径打印了错误的上下文文本，只要仍含 `[KRT_COMPILE_ERROR] Backend`。
- 后果：三种 kind 的断言完全同构，诊断文案错位（例如 calloc 失败写成 malloc）不会被发现；`kind` 参数实际上没有区分能力。
- 最小修复：按 kind 断言各自的消息片段（或断言消息中出现分配器名/大小参数），并断言退出码与消息一一对应。
- 备注：子进程 `_exit(99)` 的兜底（112）只能证明「失败了」，不能证明「失败对了」。

##### [中] 96-11｜未初始化栈变量地址被当作 ArkLinkContext* 传入
- 位置：`Test/Quality/test_backend_allocation.c:136`、`:154`
- 类别：不安全用法
- 触发条件：`ark_backend_pe_link` 的实现开始读取 `context` 的任何字段（当前可能未读，故暂时不崩）。
- 后果：`context_placeholder` 是未初始化的 `char`，其地址被强转为 `ArkLinkContext*` 后一旦被解引用就是读取垃圾/越界，测试随机崩溃或读到脏状态，且崩溃点在被测代码而非测试。
- 最小修复：提供真正的 `ArkLinkContext` 实例，或在接口允许时直接传 `NULL`。
- 备注：与 `test_elf_failure.c:104/141` 传 `NULL` 的写法相比，这里明显是为了绕过空指针检查。

##### [中] 96-12｜allocation_count 为 0 时失败注入被整体跳过且无任何告警
- 位置：`Test/Quality/test_backend_allocation.c:138-166`
- 类别：静默通过
- 触发条件：PE 链接路径改为复用调用方缓冲、不再产生任何 `calloc/realloc`（例如引入 arena 或静态缓冲）。
- 后果：`allocation_count = allocation_index` 得 0，循环 `index <= 0` 只执行成功路径一次，所有失败分支不再被遍历；测试继续打印 `PE allocation failures checked: 0` 并以 0 退出，覆盖悄悄归零。
- 最小修复：在 `allocation_count == 0` 时显式断言「PE 路径至少有一次分配」，或直接断言 `allocation_count > 0`。
- 备注：与 96-15 是同一类「计数驱动循环」的静默降级，但发生在另一个文件。

##### [中] 96-13｜寄存器分配器的分配失败路径从未注入，故障测试只覆盖后端分配
- 位置：`Test/Quality/test_backend_allocation.c:74-92`
- 类别：覆盖缺口
- 触发条件：`regalloc_allocate_with_capacity` 内部堆分配失败时的清理/返回逻辑发生回归。
- 后果：`check_register_pressure` 全程 `failure_index=0`（永不失败），只验证成功着色结果；该文件虽已把 `KRT_MALLOC/KRT_CALLOC/KRT_REALLOC` 接管为可注入版本，却没有对 `X86RegAlloc.c` 做任何失败注入，X86 分配器的 OOM 分支零覆盖。
- 最小修复：仿照 `check_pe_allocation_failures`，记录成功路径的分配次数并对每个分配点注入一次失败，断言返回错误码且 `live_allocations==0`。
- 备注：`check_register_pressure` 只断言 `num_spilled` 与着色不冲突（82-88），失败路径完全未涉及。

##### [低] 96-14｜check_fatal_allocation 的管道读取与描述符处理脆弱
- 位置：`Test/Quality/test_backend_allocation.c:100-102`、`:115-117`
- 类别：竞态与边界
- 触发条件：(a) `pipe()` 恰好返回 `diagnostics[1]==2`（运行前已关闭 stderr）时 `dup2(2,2)` 后 `close(2)` 会把刚建好的stderr 重新关掉；(b) 子进程消息长于 255 字节或被信号打断。
- 后果：(a) 子进程写进已关闭 fd，父进程 `read` 得 0，断言以误导性方式失败；(b) 单次 `read` 只取前 255 字节，`strstr` 可能漏判。
- 最小修复：`dup2` 后仅在 `diagnostics[1] != STDERR_FILENO` 时 close；父进程循环读到 EOF 并处理 `EINTR`。
- 备注：常规终端下 `diagnostics[1]==4`，(a) 不易触发，故记低。

##### [高] 96-15｜test_elf_failure 目录模式静默跳过全部分配失败注入，并打印误导性的 checked: 0
- 位置：`Test/Quality/test_elf_failure.c:107`（输出 `:121`）
- 类别：静默通过 + 诊断与实际错误不符
- 触发条件：`argc > 1`，即按 `test_elf_failure <dir>` 运行（该模式的用途是落 ELF 快照）。
- 后果：`count = directory ? 0 : allocation_index` 把失败注入循环上限置 0，`for (index=0; index<=count; ...)` 只跑一次成功路径，170 余个分配点失败分支全部不执行；同时 `printf("ELF %s allocation failures checked: %zu")` 照常输出，读者会误以为失败路径已被覆盖。
- 最小修复：目录模式与失败注入解耦——先无目录跑完整失败注入，再（可选）带目录只落快照；或至少在 `count==0` 时打印「失败注入已跳过」并计入结果。
- 备注：据 `Test/Quality/RunTests.py:52` 可见常规调用只给 `test_linker` 追加参数，故默认 CI 路径不受影响；但任何手工/快照流程都会静默丢掉这层覆盖。

##### [高] 96-16｜test_elf_failure 直接按 file_offset 读取产物，无边界校验，测试自身可越界
- 位置：`Test/Quality/test_elf_failure.c:143-144`
- 类别：整数/边界
- 触发条件：被测链接器返回错误的 `section_maps[i].file_offset`（大于 `output.size-8`）或 `output.size` 异常。
- 后果：`memcpy(&absolute, output.data + output.section_maps[1].file_offset, 8)` 直接越界读堆，可能崩溃或读到无关数据；由于该函数没有先断言 `output.size` 与 `file_offset` 的关系，错误实现可以在「越界读恰好命中预期值」时假通过。
- 最小修复：读取前断言 `output.section_maps[1].file_offset + 8 <= output.size`、`...[0].file_offset + 4 <= output.size`，再 memcpy。
- 备注：同文件 141 行只断言了 `result == ARK_LINK_OK`，未校验产物尺寸。

##### [中] 96-17｜test_elf_failure.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_elf_failure.c:154-218`
- 类别：测试断言缺失/静默通过
- 触发条件：以 `-DNDEBUG` 构建。
- 后果：7 类场景的成功/失败断言、ABS64/PC32 地址校验（147-150）、快照 fwrite 结果全部消失，`main` 仍返回 0；ELF 后端回归不再被拦截。
- 最小修复：改用失败即返回非 0 的检查宏，或固定 `-UNDEBUG`。
- 备注：`snapshot()` 中的 `assert(fwrite(...)==...)`（88-95）被消去后连写盘短写都不会被发现。

##### [中] 96-18｜test_elf_failure 只覆盖 ARK_OUTPUT_EXECUTABLE，且重定位类型被硬编码为 1
- 位置：`Test/Quality/test_elf_failure.c:138-139`、`:180-181`、`:212`
- 类别：覆盖缺口
- 触发条件：共享库/可重定位输出路径或 `ARK_RELOC_*` 其他类型出现回归。
- 后果：`output_type` 恒为 `ARK_OUTPUT_EXECUTABLE`，reloc `type` 一律写 1，导入重定位与节重定位未按类型矩阵展开；非 executable 产物与其余重定位类型的编码错误无测试拦截。
- 最小修复：为 `output_type` 与各 `ARK_RELOC_*` 建参数化用例；至少覆盖共享对象与 PC32/ABS64 两类交叉。
- 备注：`check_full_width_relocation_addresses` 虽验了 ABS64+PC32 各一条，但只是单组固定输入。

##### [低] 96-19｜固定 4096 槽分配跟踪表，耗尽即 abort，测试不可扩展
- 位置：`Test/Quality/test_elf_failure.c:6`、`:19`
- 类别：边界/资源
- 触发条件：用例规模扩大（更多 section/import/export 或实现改为每次分配多块）使跟踪表超过 4096。
- 后果：`track_allocation` 直接 `assert(0 && "allocation tracker exhausted")`，测试以崩溃收场且信息不含实际分配数，无法区分「实现泄漏」与「表太小」。
- 最小修复：改用动态数组/哈希表记录指针，或至少把容量做成与输入规模相关的常量并在耗尽时打印计数。
- 备注：当前 40 导入+40 导出+6 节用例离上限尚远。

##### [低] 96-20｜快照文件写入调用方任意路径且不清理
- 位置：`Test/Quality/test_elf_failure.c:83-96`、`:156`
- 类别：资源未清理/可移植性
- 触发条件：传入已存在的目录或指向既有文件的路径运行。
- 后果：`fopen(path,"wb")` 直接截断覆盖同名 `<name>.elf`/`<name>.maps`，且测试结束不删除生成的 7×2 个文件，污染工作区；路径完全依赖外部传参，无临时目录约定。
- 最小修复：快照写入 `mkdtemp` 创建的临时目录并在结束时递归删除；或至少 `unlink` 本次生成的文件。
- 备注：默认无参运行时 `directory=NULL`（156 行），不触发写盘。

##### [低] 96-21｜ARK_ELF_TEST_SOURCE 默认相对包含路径绑定仓库布局
- 位置：`Test/Quality/test_elf_failure.c:75-78`
- 类别：可移植性
- 触发条件：从其它检出布局/子目录构建，或上层构建系统未把仓库根加入 include 路径。
- 后果：`#include "../../ArkLink/src/Backend/BackendElf.c"` 直接编译失败，错误信息指向 include 路径而非配置问题。
- 最小修复：由构建系统通过 `-DARK_ELF_TEST_SOURCE=...` 传入绝对/受控路径（该宏已有覆盖机制，默认值不必回退到相对路径）。
- 备注：同目录 test_backend_allocation.c:61,67-68 也硬编码 `../../Re.KrtC/...`、`../../ArkLink/...`，属同批构建耦合。

##### [中] 96-22｜__wrap_KrtSafeStrdup/KrtSafeRealloc 的失败分支零覆盖
- 位置：`Test/Quality/test_frontend_allocation.c:33-38`
- 类别：覆盖缺口
- 触发条件：`KrtSafeStrdup`/`KrtSafeRealloc` 的空指针或原地扩张失败处理发生回归。
- 后果：这两个包装器只是为了让链接期 `--wrap` 完整而定义，`semantic_analyzer_create` 与 `KrtAstClone` 路径并不调用它们（`calls` 只统计实际触发的分配），因此它们的失败返回路径从未被执行；`fail_heap` 对四个包装器一视同仁并不能证明四者都被覆盖。
- 最小修复：增加真正走 strdup/realloc 的前端用例（如带源码拷贝的分析器构造），或显式断言 `strdup` 分配点数量 > 0。
- 备注：`check_ast_cloning` 只用 `KrtArenaCreateLocal`（71 行）与 arena 分配，进一步缩小了被覆盖的分配器集合。

##### [中] 96-23｜test_frontend_allocation.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_frontend_allocation.c:93-99`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：分析器构造与 AST 克隆的全部失败注入断言失效，`assert(!KrtMemoryGetBlockCount())`（97）也不再校验内存平衡，main 仍返回 0。
- 最小修复：同 96-9：自带 CHECK 宏或固定 `-UNDEBUG`。
- 备注：该文件是 Test/Quality 中唯一的前端分配失败覆盖，失效后前端 OOM 路径完全裸奔。

##### [低] 96-24｜只断言「返回 NULL + 块数平衡」，不验证失败诊断与错误状态传播
- 位置：`Test/Quality/test_frontend_allocation.c:47-57`、`:81-90`
- 类别：断言过弱
- 触发条件：`semantic_analyzer_create` 失败时打印了错误信息却仍返回非 NULL 脏对象，或反之返回 NULL 但丢失错误上下文。
- 后果：`assert(!analyzer)` 只覆盖返回值，`KrtMemoryGetBlockCount()==baseline` 只覆盖内存平衡；错误信息缺失、错误码未设置、`project_namespace_collection_failed` 之类状态未更新都不会被发现。
- 最小修复：对失败用例断言诊断输出/内部错误标志，并对成功用例断言无错误残留。
- 备注：与 96-10 同属「失败已发生 ≠ 失败处理正确」。

##### [高] 96-25｜test_ir.c 在 count==0 时读 block->insts[-1]，测试自身越界
- 位置：`Test/Quality/test_ir.c:249`
- 类别：整数/边界
- 触发条件：某函数 `entry_block` 在 DCE/内联后指令数为 0（例如全是可删 NOP 且返回被提升/删除）。
- 后果：`block->last_inst == block->insts[count - 1]` 中 `count-1` 为 `size_t` 下溢到 `SIZE_MAX`，对 `insts` 数组做越界读，轻则读到垃圾导致断言随机失败，重则段错误把崩溃归因于被测优化器；同文件 25 行对同一不变量写了 `count ? ... : NULL` 的保护，说明该边界已知，此处遗漏。
- 最小修复：改为 `assert(count ? block->last_inst == block->insts[count-1] : block->last_inst == NULL)`，与 25 行一致。
- 备注：当前 50 个 Function 均含 Add+Return，暂时不会触发，属潜伏缺陷。

##### [中] 96-26｜test_ir.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_ir.c:210-259`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：CSE/死链消除、多重定义、无效指针 builder、内联幂等性与 `assert_instruction_index` 全部失效，main 仍返回 0；IR 优化器回归不再被拦截。
- 最小修复：同 96-9。
- 备注：`check_invalid_pointer_builders` 中对 `KrtIrCallIndirect` 负数参数等边界（200-203）也一并失去校验。

##### [中] 96-27｜IROptimizer stats 除 cse_count 外全无断言，优化器统计正确性零覆盖
- 位置：`Test/Quality/test_ir.c:65-67`
- 类别：断言过弱/覆盖缺口
- 触发条件：内联、DCE、CSE 的计数字段（如内联成功数、删除指令数）实现出错或未累加。
- 后果：只断言 `stats.cse_count > 0`，其余字段从未读取；`IROptimizer stats = {0}` 之外的返回结构对调用者不可靠，且 `ir_optimize_dead_code_elimination` 的 stats（228-229）完全没接收。
- 最小修复：断言各 stats 字段与手工计数的指令增量一致，并覆盖 DCE/inlining 的 stats。
- 备注：96-28 提到的无效指针路径也没有 stats 校验。

##### [中] 96-28｜用直接改写 last_inst->result 伪造非 SSA 输入，绕过公开 API 构造非法 IR
- 位置：`Test/Quality/test_ir.c:144`、`:160`
- 类别：测试方法/覆盖缺口
- 触发条件：真实使用者无法（或不应）构造这种「同一 temp 被两条指令定义」的 IR 时。
- 后果：测试通过手写结构体字段制造不变量违背，验证的是「优化器容忍脏输入」而非「合法非 SSA 输入（如外部 IR/手写字节码）的处理」；一旦失败，断言位置离真正入口很远，且该构造无法复用到其他用例。
- 最小修复：提供公开的测试辅助构造器（或从文本 IR/字节码解码）生成同构输入，并保留对 builder 拒绝重复定义的正向断言。
- 备注：若这是刻意的鲁棒性测试，应再加一条「builder 不允许产生同类结构」的对照断言。

##### [高] 96-29｜test_linker.c 对 iat/RVA 未做上下界校验即做指针运算与 memcpy，测试自身可越界
- 位置：`Test/Quality/test_linker.c:49`、`:52-53`、`:55`、`:58-59`、`:64-65`
- 类别：整数/边界
- 触发条件：`generate_import_table` 返回 `iat < base`（减法回绕成巨值）或 `ImportLookupTableRVA/NameRVA` 超出 `size`。
- 后果：`cursor = iat - base` 与 `data + directory.ImportLookupTableRVA - base` 直接成为巨大偏移，随后的 `memcpy`/`strcmp` 越界访问；唯一前置断言（48）只查 `size>200000`、`entries` 与 `iat%8`，挡不住这类错误，测试可能在越界读命中预期字节时假通过。
- 最小修复：在解引用前断言 `iat >= base`、`ImportLookupTableRVA >= base`、`NameRVA >= base` 且各 `+ 所需宽度 <= size`。
- 备注：该测试的定位是「直接验证序列化输出」（文件头注释），恰恰最需要边界护栏。

##### [中] 96-30｜test_linker.c 向调用方任意路径写文件，可覆盖既有文件且从不清理
- 位置：`Test/Quality/test_linker.c:98-110`
- 类别：资源未清理/不安全用法
- 触发条件：`argv[1]` 指向已存在文件或不可写目录。
- 后果：`kro_write_file` 以写方式创建/截断目标文件，测试结束既不 `unlink` 也不校验文件长度，残留 `Output.kro`（RunTests.py:52 传入临时目录）；若路径来自用户输入还存在覆盖任意文件的风险面。
- 最小修复：写入 `mkdtemp` 临时目录，结束删除；或至少 `remove(filename)` 并校验文件大小。
- 备注：`assert(argc==2)`（115）保证一定有路径，但没有约束路径性质。

##### [中] 96-31｜test_linker.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_linker.c:114-120`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：align_up 饱和边界（10-12）、buffer 复用/溢出（15-22）、1200 项导入表逐项比对（48-70）、KRO 对齐/回读（82-109）全部失效，main 仍返回 0。
- 最小修复：同 96-9。
- 备注：`check_kro` 中文件回读的 254 次 `fgetc` 断言（106-108）被消去后，写盘内容错也无人知。

##### [中] 96-32｜导入表的 is_function 维度零断言，lookup>=base 反而把 ordinal 编码锁死
- 位置：`Test/Quality/test_linker.c:41`、`:64`
- 类别：覆盖缺口/断言过弱
- 触发条件：实现对 `is_function==0` 的数据导入改用 ordinal 编码（置高位）或 hint/name 标志错误。
- 后果：`imports[i].is_function = i % 2` 造出两种导入，但断言只走「lookup 是指向 hint/name 的 RVA」一条路；ordinal 条目会让 `lookup >= base` 直接失败（高位为 1），于是测试要么强迫实现忽略 is_function，要么报错，无论哪种都说明该维度未被验证。
- 最小修复：按 `is_function` 分支断言：函数导入查 hint/name 字符串与标志位，数据导入查 ordinal/名称策略，并覆盖 ordinal 路径。
- 备注：测试注释声称验证「serialized output directly」，但实际只验证了 name 表一种表示。

##### [低] 96-33｜KRO 数据段回读内容未校验，只校验了代码段与头部字段
- 位置：`Test/Quality/test_linker.c:84-91`、`:104-109`
- 类别：覆盖缺口
- 触发条件：`kro_write_data`/`kro_write_data_aligned` 写入位置或内容错位（如写到代码区、偏移计算错误但偏移返回值正确）。
- 后果：`kro_get_data_offset()==33` 只证明偏移量数值，回读只检查了代码区的两个 0xcc 与填充 0x90；数据区那 1 字节从未被读回，数据段写坏仍全绿。
- 最小修复：按 `header`/data 偏移回读数据区字节并断言等于写入值，同时校验文件总长度。
- 备注：`kro_write_data_aligned(...,3)==0` 的失败分支（90）倒是被覆盖了。

##### [中] 96-34｜test_memory.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_memory.c:148-156`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：分配器溢出拒绝（27-28）、arena 分配/复位（45-68）、IR 池复用清零（98-118）、8 线程共享 arena 计数（134-145）与最终内存平衡（153）全部失效，main 仍返回 0。
- 最小修复：同 96-9。
- 备注：该文件是分配器安全性的主回归，失效影响面最大。

##### [中] 96-35｜分配器边界覆盖不足：不测 0/1 字节、页阈值与次极端大值
- 位置：`Test/Quality/test_memory.c:18`、`:27-28`、`:31`、`:59-60`、`:68`
- 类别：整数/边界、覆盖缺口
- 触发条件：实现在 0 字节分配、1 字节分配、mmap/块阈值（如 128KB）附近或 `SIZE_MAX-1` 等次极值上行为错误。
- 后果：现有用例只覆盖 `sizeof(max_align_t)..255`、`SIZE_MAX` 与 `SIZE_MAX/2+1`；`size=0` 的返回约定、跨块阈值的重分配、`SIZE_MAX-1` 的加法溢出判定均无测试，属关键边界无覆盖。
- 最小修复：补齐 0/1 字节、128KB±1、`SIZE_MAX-1`、对齐溢出（`size` 使 `size+align-1` 回绕）用例。
- 备注：`KrtBufferCheck` 只测了 `SIZE_MAX-3` 与非法宽度组合（31-32）。

##### [低] 96-36｜共享 arena 的 24000 精确断言兼作竞态探测，失败无竞争细节
- 位置：`Test/Quality/test_memory.c:144`
- 类别：竞态与非确定性
- 触发条件：arena 计数器非原子或线程调度使中间态被读到。
- 后果：断言只给出 `arena->allocation_count == 24000` 一个等式，失败时无法区分「竞态丢更新」「重复计数」还是「合法语义变更」；该用例既是功能测试又是竞态探测，职责混杂导致偶发失败难定位。
- 最小修复：把并发正确性交给 ThreadSanitizer/单独压力用例，功能用例只断言每个线程成功返回与最终对齐；或对计数失败打印每线程本地计数。
- 备注：`allocate_thread`（122-132）本身写得干净，无泄漏。

##### [低] 96-37｜IR 池只测同型 LIFO 复用，未测扩容/耗尽与跨类型混用
- 位置：`Test/Quality/test_memory.c:98-118`
- 类别：覆盖缺口
- 触发条件：池在当前 arena 块耗尽后的扩容/失败路径，或同一池被不同类型对象复用。
- 后果：用例始终在 200 个对象内反复 alloc/free，`KrtIrPoolAlloc` 的扩容分支与失败返回值从未触发；池耗尽时的行为（返回 NULL 还是向 arena 要新块）无断言。
- 最小修复：构造超过初始池容量的分配序列触发扩容，并断言失败/成功返回与对象清零。
- 备注：`manager.pools[type].object_size` 为 0 的退化类型也未验证（105 行 memset 长度取自该字段）。

##### [中] 96-38｜test_memory_benchmark 无阈值无判定，性能回归永远静默
- 位置：`Test/Quality/test_memory_benchmark.c:38-39`
- 类别：静默通过
- 触发条件：分配器/arena 实现性能回退（甚至退化到不可用）。
- 后果：程序只打印毫秒数并 `return 0`，没有任何基线比较、超时或断言；作为 Quality 套件的一部分，它对性能回归完全不设防，且 `assert` 也可被 `NDEBUG` 消除。
- 最小修复：与基线样本比较并设相对阈值（超阈返回非 0），或至少由 Benchmark.py 记录历史曲线并对突增报警。
- 备注：该文件的价值定位若是人工观测，应在 ReadMe 明确「不作为门禁」，避免被当成通过性测试。

##### [中] 96-39｜基准两种模式不做内存平衡校验，也不调用 Cleanup
- 位置：`Test/Quality/test_memory_benchmark.c:16-37`
- 类别：资源泄漏/静默通过
- 触发条件：free 模式漏 free、或 arena 模式 destroy 不彻底。
- 后果：两种模式结束都不检查 `KrtMemoryGetBlockCount()/GetTotalUsage()`，也不调用 `KrtMemorySafetyCleanup()`（对比 test_memory.c:153-154 的做法）；即使每次运行泄漏 3 万个块，基准依然打印时间并以 0 退出，泄漏被进程退出掩盖。
- 最小修复：在两分支末尾断言块数/总用量归零并调用 Cleanup，把结果打印出来。
- 备注：`pointers[30000]`（17）本身在栈上，不存在泄漏，但正因如此更应校验堆侧。

##### [低] 96-40｜基准的 argc 契约与两分支计时口径不一致
- 位置：`Test/Quality/test_memory_benchmark.c:14`、`:16-37`
- 类别：可移植性/诊断
- 触发条件：不带参数运行，或把两分支耗时横向比较。
- 后果：无参数时 `assert(argc==2)` 直接 abort，无用法提示；free 模式只测 malloc/free，arena 模式额外含 3×100000 次 64 字节 `memset`（32 行），两者计时口径不同，直接对比会得出错误结论。
- 最小修复：缺参时打印用法并返回非 0；两分支统一是否含 memset，或分别输出「纯分配」与「含写入」两个指标。
- 备注：ReadMe 已说明两版对比用途，但代码本身没有防误读保护。

##### [低] 96-41｜test_ir.c 的 20 次整组重跑放大成本且失败定位信息不足
- 位置：`Test/Quality/test_ir.c:213-256`
- 类别：测试工程/覆盖效率
- 触发条件：套件运行时间敏感或某次迭代出现偶发失败。
- 后果：每次迭代重建 50 函数模块并完整跑一遍 `check_cse_and_dead_chains`/`check_unused_traps_and_calls`/`check_multiple_definitions`（253-255），任一断言失败只指向具体 assert 行，无法知道是第几次迭代（无迭代编号输出）；NDEBUG 下这些成本仍然支付。
- 最小修复：失败信息带上 `pass` 编号（或把重复次数做成参数），并保持至少一次全量。
- 备注：不影响正确性，属可维护性问题。

##### [低] 96-42｜test_backend_allocation.c 的 printf 计数不参与判定，输出可被误读为覆盖率
- 位置：`Test/Quality/test_backend_allocation.c:168`
- 类别：诊断与实际错误不符
- 触发条件：`allocation_count` 因实现改变而变小（见 96-12），或失败注入实际未触发。
- 后果：`PE allocation failures checked: %zu` 只是打印，不与断言挂钩，也不校验 `allocation_count>0`；读者会把数字当成覆盖证明，与 96-15 的 `checked: 0` 属同一类「计数即结论」的误导性诊断。
- 最小修复：把计数纳入判定（计数为 0 直接失败），或改名并注明「本次实际执行的注入点数」。
- 备注：同文件 59 行 `Analyzer allocation failures checked` 同样是纯打印。

### 第九十七批：Test/Performance 全部源文件

#### 覆盖范围

- 枚举方式：`glob Test/Performance/**/*` 后筛选 `.c/.h/.inc/.krt`，得 **39 个文件**（实际无 `.h`、无 `.inc`），已逐个 `read` 全量；合计约 1700 行。
- 分布：
  - `CodegenComparison/Driver.c`
  - `GeneralOptimization/Driver.c`、`LLVMOptimization/Driver.c`、`O3Optimization/Driver.c`
  - `NativeFib/Driver.c`、`NativeFib/Fib.c`
  - `Runtime/` 5 个 `.krt`（test_empty_loop、test_fib35、test_fib40、test_file_io、test_output）
  - `LLVMOptimization/BudgetSelection/Sources/` 6 个 `.krt`（BinaryTree、Branching、Fib35、Fib40、NarrowArguments、Parameters）
  - `LLVMOptimization/Artifacts/` 10 个 `.c` + 10 个 `.krt`
  - `test_compiler_hotspots.c`、`test_register_allocation.c`
- 方法限制：本会话 bash 被沙箱拒绝（无 bubblewrap/Landlock），未能编译或运行任何用例；仅用 glob/read/grep。对 `Test/Performance` 之外的源文件未整文件通读，只对 Python harness 做符号级 grep（`subprocess`/`returncode`/`platform`/`Artifacts`/`split("int32 Fib("` 等）核对契约；「harness 是否补偿」写在备注。
- 共用样板：21 个 `.krt` 共享同一段 `BenchmarkNow`/`BenchmarkFinish`（`syscall(228,1,...)` + `syscall(1,2,...,16)`），同类问题用 97-32、97-33 两条汇总并在备注列全文件。


##### [致命] 97-1｜空循环体可被优化器整段消除，断言无法发现，基准测量为零仍通过
- 位置：`Test/Performance/Runtime/test_empty_loop.krt:22-24`
- 类别：性能测量方法缺陷 / 优化器消除被测代码 / 断言恒真
- 触发条件：以 `-O1` 及以上编译；循环 `for (; i < 100000; i++) { }` 体为空，只有 `i` 是活的。
- 后果：编译器可把循环折叠为 `i = 100000`，计时区间退化为几条指令；`if (i != 100000)` 仍成立，`BenchmarkFinish` 正常写报告，harness 的 `0 < duration_ns` 也通过，于是「空循环基准」长期输出无意义数值而无人察觉。
- 最小修复：让循环体产生可观测副作用（如累加到 `report[1]` 或 `volatile` 变量），或加编译器屏障；并在 harness 侧增加最小时长阈值（例如 `duration_ns >= 1000`）。
- 备注：`RuntimeBenchmarks.py:123` 只校验 `0 < duration_ns <= process_ns`，无法识别「被消除」与「真实极快」。

##### [高] 97-2｜test_file_io 遗留 409.6MB `payload.bin`，文件内无任何 unlink
- 位置：`Test/Performance/Runtime/test_file_io.krt:28`（写）、`:31-34`（100000×4096 写）、`:46-47`（结束）
- 类别：资源泄漏与残留产物
- 触发条件：直接运行该 `.krt` 编译产物，或 harness 被中断/杀进程。
- 后果：每次运行在 CWD 留下 409,600,000 字节文件；清理完全依赖外部 `RuntimeBenchmarks.py:100` 的 `TemporaryDirectory`，文件本身无 `unlink`（x86-64 应为 `syscall(87, path, 0,0,0,0,0)`）。磁盘可被反复跑满。
- 最小修复：在读完/关闭后 `unlink("payload.bin")`，或改用 `O_TMPFILE`/`tmpfile`；至少提供可配置路径。
- 备注：正常 harness 路径下 `TemporaryDirectory` 会清理，但 `dir=HERE` 把临时目录建在 `Test/Performance/` 内，仍属写源树。

##### [高] 97-3｜test_file_io 固定相对路径 + 依赖 CWD 可写，多实例并发互踩同一文件
- 位置：`Test/Performance/Runtime/test_file_io.krt:28`
- 类别：依赖 CWD/绝对路径/环境 / 非确定性竞态
- 触发条件：从任意非临时目录运行，或两个实例同时在同一 CWD 运行。
- 后果：`open("payload.bin", O_RDWR|O_CREAT|O_TRUNC)` 会污染任意可写目录；两实例互相 `O_TRUNC` 对方正在读的文件，导致短读/内容错乱，返回 2/6/7 或更糟的静默数据混合。
- 最小修复：路径改为参数/环境变量，或每次生成唯一文件名（含 pid/随机数），并在结束时删除。
- 备注：`RuntimeBenchmarks.py:117` 用 `cwd=directory` 隔离了 harness 场景，但 `.krt` 自身不可独立安全运行。

##### [中] 97-4｜test_file_io 读回只校验 4096 字节中的 2 字节
- 位置：`Test/Performance/Runtime/test_file_io.krt:41`
- 类别：覆盖缺口 / 断言过弱
- 触发条件：写/读过程中出现中间块损坏、错位或长度不足。
- 后果：`buffer[0]==0 && buffer[4095]==79` 仍可能成立（该模式每 251 字节重复），损坏的 I/O 被静默接受；`return 7` 的校验强度远低于 409.6MB 的传输量。
- 最小修复：逐块校验完整 4096 字节（或至少校验和/抽样多个偏移），并校验总字节数。
- 备注：`RuntimeBenchmarks.py:61-68` 只额外校验文件前两个块，整体仍非全量校验。

##### [中] 97-5｜test_file_io 计时把写、fsync、定位、读混在一个区间，无法归因
- 位置：`Test/Performance/Runtime/test_file_io.krt:27-46`
- 类别：性能测量方法缺陷（计时口径）
- 触发条件：任何存储介质/文件系统差异（SSD/HDD/tmpfs、页缓存状态）。
- 后果：`duration_ns` 同时包含 409.6MB 写 + `fsync` 刷盘 + `lseek`/`fadvise` + 409.6MB 读，跨机器不可比，也无法判断优化是否生效；`fsync` 使结果强依赖介质。
- 最小修复：拆成写、同步、读三个独立计时段分别报告；或明确声明这是「混合 I/O 吞吐」而非某一项性能。
- 备注：`RuntimeBenchmarks.py` 只记录一个 `duration_ns`。

##### [高] 97-6｜test_compiler_hotspots 向给定目录的父目录写 `Main.krt`/`Main.ir`，临时目录清理不到
- 位置：`Test/Performance/test_compiler_hotspots.c:157-158`（`../Main.krt`、`../Main.ir`）、`:204`（codegen 写 `Main.ir`）
- 类别：资源残留 / 依赖路径布局
- 触发条件：`cache` 模式；`test_compiler_hotspots.py:44` 传入的是 `/tmp/kairote-hotspot-case-*/`，故 `../Main.krt` 落在 `/tmp/Main.krt`。
- 后果：`TemporaryDirectory` 只清理自身，`/tmp/Main.krt`、`/tmp/Main.ir` 永久残留；多个测试进程并发时互踩同名文件；若父目录不可写则 `write_source` 失败。
- 最小修复：全部落在 `directory` 内（`Main.krt`、`Main.ir` 与 `Library.krt` 同目录），不要拼接 `..`。
- 备注：`Library.krt` 在临时目录内会被清理；`input%d.krt`/`output%d.ir` 也在临时目录内。

##### [高] 97-7｜test_compiler_hotspots 断言全局缓存 `misses == 2` 精确值，且由 4 线程并发触发
- 位置：`Test/Performance/test_compiler_hotspots.c:222-224`（另见 `:229-230` 的 `misses == 3`）
- 类别：非确定性/竞态 / 断言过强
- 触发条件：4 个 `cache_worker` 线程并发调用 `KrtCompilePipelineLoadStandardLibrary`；缓存实现若对 hit 路径有写操作（LRU/时间戳）或并发去重策略不同。
- 后果：额外一次 miss 即 `CHECK` 失败，测试 flaky；同时并发读全局缓存若不加锁就是数据竞争，可能崩溃或计数漂移。
- 最小修复：只断言下界（如 `hits >= 49 && misses <= 4`），或串行预热后再并发；把精确计数改为统计不变量。
- 备注：主线程在 `:209` 已预热，故现状通常稳定；但断言把「缓存实现细节」当成契约。

##### [中] 97-8｜bench_cache 只打印 cold/warm，不断言 warm 明显小于 cold
- 位置：`Test/Performance/test_compiler_hotspots.c:174-184`
- 类别：断言缺失 / 性能测量方法缺陷
- 触发条件：标准库缓存失效（每次 warm 都退化为 cold）或缓存被 `Reset` 清空。
- 后果：`stdlib_warm_seconds` 与 cold 相同甚至更大也照常输出，缓存回归完全静默；`:184` 无任何阈值。
- 最小修复：增加 `CHECK(warm / 8 < cold)`（或比值阈值），否则至少把 `cold == warm` 视为可疑并报告。
- 备注：`test_compiler_hotspots.py` 只检查 harness 返回码，不解析该 JSON。

##### [中] 97-9｜bench_symbols 的 checksum 只打印不校验
- 位置：`Test/Performance/test_compiler_hotspots.c:101-112`（`:111` 打印 `checksum`）
- 类别：只打印不校验
- 触发条件：`symbol_table_lookup_scope_chain` 返回了错误符号（非 NULL 但行号错误）。
- 后果：`CHECK(entry)` 只保证非空，`sum` 算错也照样输出，符号表回归静默通过。
- 最小修复：与基准 checksum 比较（如固定期望值或跨实现比对），或至少校验 `sum` 非零/单调。
- 备注：非基准分支 `:96-100` 的断言是有效的，仅基准分支缺校验。

##### [中] 97-10｜bench_preprocessor 只检查一个子串 `"+ 123 +"`
- 位置：`Test/Performance/test_compiler_hotspots.c:73`
- 类别：断言过弱 / 覆盖缺口
- 触发条件：预处理器只展开首个宏、漏展开后续宏或改变空白处理。
- 后果：`strstr(result, "+ 123 +")` 命中即通过，18000 行输入的其余部分是否正确无校验；`printf` 的 `input_bytes`/`macros` 也不校验。
- 最小修复：对完整输出做长度/内容校验（如校验宏展开计数或哈希），至少校验 `strstr` 出现次数。
- 备注：非基准分支 `:50-60` 有精确字符串比对，基准分支反而弱化。

##### [中] 97-11｜parallel_test 用 `strstr` 子串校验输出文件，且只读前 4095 字节
- 位置：`Test/Performance/test_compiler_hotspots.c:297-307`（`:303-306`）
- 类别：断言过弱 / 覆盖缺口
- 触发条件：IR 输出含多余内容、被截断，或正确片段出现在前 4095 字节而后续损坏。
- 后果：`strstr(data, "return N\n")` 命中即通过；文件大于 4096 字节时后半部分从不检查，代码生成回归可能静默通过。
- 最小修复：读取完整文件并做精确匹配（或至少校验文件长度与关键指令数），避免固定 4096 缓冲区截断。
- 备注：`:292` 的 `AddFile` 返回码有检查，问题仅在输出内容校验。

##### [中] 97-12｜parallel_benchmark 计时区间包含编译器创建/销毁与 64 次 AddFile，且单次无基线
- 位置：`Test/Performance/test_compiler_hotspots.c:322-331`（`:323` Create、`:325-327` AddFile、`:330` Destroy）
- 类别：性能测量方法缺陷（计时口径、单次测量、缺基线/阈值）
- 触发条件：任何运行；线程池创建/销毁开销与 64 个任务的编译开销同区间。
- 后果：`batch_seconds` 不是纯编译时间，跨版本比较会被线程池初始化主导；只测一次，无重复、无基线、无阈值，性能回归无告警。
- 最小修复：把 `ParallelCompilerCreate`/`AddFile`/`Destroy` 移出计时区间，只计 `ParallelCompilerExecute`；循环多次取中位数并与基线比较。
- 备注：`test_compiler_hotspots.py` 只检查返回码，不解析 `batch_seconds`。

##### [高] 97-13｜test_register_allocation 全部校验用 `assert`，`-DNDEBUG` 下整测静默通过
- 位置：`Test/Performance/test_register_allocation.c:28`、`:60`、`:101`、`:114-182`（共 15+ 处）
- 类别：断言缺失 / 静默通过
- 触发条件：构建带 `-DNDEBUG`（发布/性能构建常见），`assert` 被预处理器删除。
- 后果：所有 CFG/分配断言消失，`main` 恒返回 0，寄存器分配错误完全不可见；`:28` 的 `assert(index < 16)` 消失后还可能缓冲区溢出。
- 最小修复：改用显式 `CHECK` 宏（打印行号并 `return 1`），不依赖 `NDEBUG`。
- 备注：当前 `test_register_optimization.py:252` 用 `-O1 -g -Wall -Wextra -Werror` 且未定义 `NDEBUG`，故现状可工作；但这是脆弱的隐式约定。

##### [中] 97-14｜test_register_allocation 直接 `#include` 编译器 `.c` 并依赖 `../../Re.KrtC` 相对布局
- 位置：`Test/Performance/test_register_allocation.c:4`、`:13`
- 类别：依赖路径/与被测实现契约不符
- 触发条件：仓库改名、`Test/Performance` 与 `Re.KrtC` 相对位置变化，或改用安装式头文件。
- 后果：编译直接失败；测试与 `KroCodegen.c` 的内部实现（`prepare_temporary_storage`、`temp_slots` 等）强耦合，实现重构即破坏测试。
- 最小修复：把被测分配逻辑抽成公开头文件/库接口，测试只依赖稳定 API。
- 备注：`:7-12` 的 `#undef KRT_MALLOC` 等也依赖 `KrtCommon.h` 恰好定义了这些宏。

##### [中] 97-15｜test_eligible 用 VLA 并以 `slot - temp_slots` 作下标，存在越界/空指针风险
- 位置：`Test/Performance/test_register_allocation.c:103-108`
- 类别：越界/未定义行为
- 触发条件：`temp_slot_count == 0` 时 `eligible` 长度为 1，而 `find_temp_location(context, 1)` 可能返回 `temp_slots` 之外/空指针；或返回的 slot 索引 ≥ `temp_slot_count`。
- 后果：`eligible[slot - temp_slots]` 越界读，断言结果不可信，ASan 未开启时静默。
- 最小修复：显式检查 `slot >= temp_slots && slot < temp_slots + temp_slot_count`，并处理 `temp_slot_count == 0`。
- 备注：当前 fixture 均定义 temp 1，正常路径不触发。

##### [高] 97-16｜NativeFib/Driver 完全不校验 fib 结果，恒返回 0
- 位置：`Test/Performance/NativeFib/Driver.c:43-48`
- 类别：断言缺失 / 只打印不校验
- 触发条件：`fib_asm`/`fib_c`/`fib_c_o2` 返回错误值（如汇编实现符号/调用约定错误）。
- 后果：驱动只把 `value` 打印到 JSON 并 `return 0`；任何只看退出码的调用者都会静默接受错误结果。
- 最小修复：在驱动内与期望值比较（如 `if (value != expected) return 1;`），或 `#ifndef EXPECTED #error`。
- 备注：`NativeFibBenchmarks.py:82` 会校验 `sample['result'] != expected`，故当前 harness 下可发现；但驱动自身无自检。

##### [中] 97-17｜NativeFib/Driver 把空字符串参数当作 n=0 接受
- 位置：`Test/Performance/NativeFib/Driver.c:21-25`
- 类别：输入校验缺失 / 静默接受非法输入
- 触发条件：`binary c_recursive ""`（`strtol("")` 返回 0，`end` 指向起始，`*end == '\0'`）。
- 后果：非法参数被当成 `fib(0)`，基准/校验流程可能误用该样本；`:23` 的 `*end` 检查对空串失效。
- 最小修复：显式检查 `argv[2][0] == '\0'`，并拒绝前导空白/正号（如用 `strtol` 后校验首字符为数字）。
- 备注：`:18-20` 的 `argc` 检查是正确的。

##### [中] 97-18｜NativeFib/Driver 不检查 `printf` 返回值，报告仅写 stdout
- 位置：`Test/Performance/NativeFib/Driver.c:47-48`
- 类别：返回码忽略 / 静默通过
- 触发条件：stdout 被关闭、重定向到已满文件系统或管道断裂。
- 后果：JSON 未完整写出，驱动仍 `return 0`；若调用者只检查退出码则静默通过。
- 最小修复：检查 `printf(...) > 0`，或改用 `write` 并校验字节数（与 `:42` 的 stderr 写法一致）。
- 备注：`NativeFibBenchmarks.py:70` 用 `json.loads` 解析，截断会被发现；但驱动层无保护。

##### [中] 97-19｜NativeFib/Driver 使用 `CLOCK_MONOTONIC_RAW` 且单次测量、无预热
- 位置：`Test/Performance/NativeFib/Driver.c:40-47`
- 类别：性能测量方法缺陷（计时口径、单次测量）
- 触发条件：与其它套件（`CLOCK_MONOTONIC`）横向比较，或直接单次运行。
- 后果：RAW 时钟不受 NTP 频率校正影响，与 `CodegenComparison/Driver.c:16`、`.krt` 的 `CLOCK_MONOTONIC` 不同域，跨套件比较失真；单次冷启动测量噪声大。
- 最小修复：统一为 `CLOCK_MONOTONIC`（或全部 RAW），并在驱动内重复多次取中位数/最小值。
- 备注：`NativeFibBenchmarks.py:50` 明确记录「no warmup or discarded samples」，默认 10 轮由外层重复。

##### [低] 97-20｜NativeFib/Driver 时长为无符号减法，未检查 `finish >= start`
- 位置：`Test/Performance/NativeFib/Driver.c:47`
- 类别：性能测量方法缺陷 / 数值健壮性
- 触发条件：时钟回退（NTP 步进、计数器异常）或 `clock_gettime` 返回异常值。
- 后果：`nanoseconds(finish) - nanoseconds(start)` 回绕成约 1.8e19 ns，输出荒谬时长；`NativeFibBenchmarks.py:82` 的 `duration_ns <= 0` 检查的是 JSON 解析后的正值，无法识别回绕。
- 最小修复：用有符号差值并检查 `finish >= start`，或检查 `duration_ns` 上限。
- 备注：`CLOCK_MONOTONIC_RAW` 正常不会回退，属低概率。

##### [中] 97-21｜NativeFib/Fib.c 依赖 GCC/Clang 专属 `__attribute__((noinline))`，无 `#ifdef` 保护
- 位置：`Test/Performance/NativeFib/Fib.c:8`
- 类别：可移植性 / 与被测实现契约不符
- 触发条件：用非 GCC/Clang 编译器（或不支持该属性的前端）构建。
- 后果：属性被忽略时递归可能被内联/特化，基准失真；某些编译器在 `-Werror` 下直接报错。
- 最小修复：用 `__has_attribute` / `_MSC_VER` 等条件宏隔离，或提供编译器中立的阻止内联手段。
- 备注：当前 `NativeFibBenchmarks.py:38` 用 `cc -std=c11 -Wall -Wextra -Werror`，gcc/clang 下可编译。

##### [低] 97-22｜NativeFib/Fib.c 的 `FIB_NAME` 宏名冲突无任何防护
- 位置：`Test/Performance/NativeFib/Fib.c:3-12`
- 类别：构建契约 / 覆盖缺口
- 触发条件：构建脚本对两个变体使用相同 `-DFIB_NAME`（或都省略）。
- 后果：两个 TU 导出同名符号，链接冲突或串扰，`asm`/`c_o2` 比较失效；文件本身不会报错提示。
- 最小修复：在文件内静态断言/文档强制两个宏不同，或拆成两个文件。
- 备注：`NativeFibBenchmarks.py:57-58` 明确区分 `fib_c` 与 `fib_c_o2`。

##### [高] 97-23｜CodegenComparison/Driver 不校验 Fib 结果，除时钟/写失败外恒返回 0
- 位置：`Test/Performance/CodegenComparison/Driver.c:36-42`
- 类别：断言缺失 / 只打印不校验
- 触发条件：被链接的 `Fib` 实现返回错误值（如叶子门编码被诊断改变）。
- 后果：`value` 只写进 stderr 报告，退出码仍为 0；只看退出码的调用者静默接受错误结果。
- 最小修复：增加 `if (value != expected) return 1;`，并让构建传入 `-DFIB_EXPECTED`。
- 备注：`CodegenComparison/Run.py:141` 会校验 `value != expected`，当前 harness 下可发现；驱动自身无自检。

##### [中] 97-24｜CodegenComparison/Driver 单次测量、无预热
- 位置：`Test/Performance/CodegenComparison/Driver.c:35-37`
- 类别：性能测量方法缺陷（单次测量）
- 触发条件：直接运行驱动，或冷启动/首次页错误。
- 后果：单次冷测量噪声大，结果不可复现；驱动内无重复/取中位数。
- 最小修复：驱动内循环多次取最小/中位数，或文档声明单次并由 harness 重复。
- 备注：`CodegenComparison/Run.py:149` 默认 10 轮在外层重复。

##### [中] 97-25｜三个优化级别驱动字节完全相同，无共享来源，改一处不会同步
- 位置：`Test/Performance/GeneralOptimization/Driver.c:9-24`（`LLVMOptimization/Driver.c:9-24`、`O3Optimization/Driver.c:9-24` 逐字节相同）
- 类别：覆盖缺口 / 维护风险
- 触发条件：修复计时、报告或返回码语义时只改其中一份。
- 后果：三个「不同优化级别」的基准口径漂移，比较结论失效；且三份拷贝增加审计面。
- 最小修复：合并为同一 `Driver.c`，用 `-D` 区分；或抽成共享头由三者包含。
- 备注：三份的 `BENCH_A`/`BENCH_B`/`EXPECTED` 均由构建传入，缺失会编译失败（这点是好的）。

##### [中] 97-26｜General/LLVM/O3 Driver 只做正确性判断，无基线与性能阈值
- 位置：`Test/Performance/GeneralOptimization/Driver.c:23`（另两份同）
- 类别：性能测量方法缺陷（缺基线/阈值）
- 触发条件：Kernel 性能回归但结果仍正确。
- 后果：退出码为 0，驱动层无任何退化告警；性能结论完全依赖外层 harness。
- 最小修复：由 harness 与 `--baseline` 比较（`GeneralOptimization/Run.py:196` 已有 baseline），驱动内至少上报时长（已上报，见 `:18-22`）。
- 备注：`Run.py:123` 的 `valid` 已含 `duration >= 0`，但无阈值。

##### [中] 97-27｜test_fib35.krt 以字面量 `Fib(35)` 作被测调用，存在被编译期特化/折叠的风险
- 位置：`Test/Performance/Runtime/test_fib35.krt:26`（`test_fib40.krt:26` 同）
- 类别：优化器消除被测代码 / 性能测量方法缺陷
- 触发条件：编译器实现常量传播、递归特化或记忆化。
- 后果：计时区间可能只剩常数返回，基准失真；`.krt` 内无 `noinline`/`volatile` 或运行时输入防护。
- 最小修复：把 `n` 改为运行时输入（argv/环境），或显式禁止内联并在 harness 侧校验汇编中仍存在递归调用。
- 备注：`NativeFib/Fib.c:7` 的注释明确「runtime input, no memoization or constant folding」，说明项目意识到该风险，但 `.krt` 未继承该防护。

##### [中] 97-28｜test_fib35.krt 被 Run.py 以文本方式解析，存在跨目录脆弱耦合
- 位置：`Test/Performance/Runtime/test_fib35.krt:18`（`int32 Fib(` 分隔点）
- 类别：依赖/契约脆弱
- 触发条件：重命名 `Fib`、调整函数签名或改动 `int32 Fib(` 前缀文本。
- 后果：`LLVMOptimization/Run.py:162` 用 `read_text().split("int32 Fib(",1)[0]` 提取计时样板构造 Artifacts 计时器；文本一变就取错片段，Artifacts 基准全部受影响。
- 最小修复：把 `BenchmarkNow`/`BenchmarkFinish` 抽成独立 `.inc`/`.krt` 片段供双方包含，避免文本解析。
- 备注：该耦合是跨目录的（`Runtime/` ↔ `LLVMOptimization/`）。

##### [中] 97-29｜test_fib40.krt 同样以字面量 `Fib(40)` 调用，存在被特化/折叠风险
- 位置：`Test/Performance/Runtime/test_fib40.krt:26`
- 类别：优化器消除被测代码 / 性能测量方法缺陷
- 触发条件：同 97-27。
- 后果：同 97-27；`fib40` 是预算选择/优化级别对比的主力负载，失真影响面更大。
- 最小修复：同 97-27（运行时输入或禁止内联 + 汇编校验）。
- 备注：`RuntimeBenchmarks.py:28` 仅校验结果 `102334155` 与时长范围。

##### [中] 97-30｜test_output.krt 计时不含真实 I/O，测的是缓冲/格式化开销
- 位置：`Test/Performance/Runtime/test_output.krt:23-27`
- 类别：性能测量方法缺陷（计时口径）
- 触发条件：`Console.Write` 带用户态缓冲，实际 `write` 推迟到退出刷新。
- 后果：`duration_ns` 主要是在度量数字格式化与缓冲拷贝，真正的输出系统调用发生在计时区间之外；「output 基准」名不副实，跨实现不可比。
- 最小修复：在计时区间内显式 flush 并计入，或改用无缓冲写；至少在校验时把 flush 时间纳入。
- 备注：`RuntimeBenchmarks.py:115` 把 stdout 重定向到文件，缓冲行为取决于 libc/运行时。

##### [中] 97-31｜test_output.krt 自身不校验 stdout 内容，只校验循环计数
- 位置：`Test/Performance/Runtime/test_output.krt:24-28`
- 类别：断言缺失 / 覆盖缺口
- 触发条件：`Console.Write` 输出格式改变（加空格/换行/符号）。
- 后果：`.krt` 仍返回 0；只有 `RuntimeBenchmarks.py:53-56` 的精确字节比对能发现，`.krt` 单独运行/复用时无保护。
- 最小修复：在 `.krt` 内自校验输出字节数（如累计 `Write` 返回值）或回读校验。
- 备注：`RuntimeBenchmarks.py:23` 期望 488890 字节，与 `Write(i++)` 的十进制拼接一致。

##### [中] 97-32｜21 个 `.krt` 硬编码 x86-64 Linux syscall 号，不可移植
- 位置：`Test/Performance/Runtime/test_fib35.krt:4`（`syscall(228,...)`、`:14` `syscall(1,...)`）
- 类别：可移植性 / 依赖环境
- 触发条件：在 aarch64 等非 x86-64 平台运行（如 Android 原生环境）。
- 后果：228/1/2/3/0/74/8/221 在 aarch64 上分别对应完全不同的调用（clock_gettime 在 aarch64 为 113、write 为 64、openat 为 56 等），计时与报告写入直接错误，甚至崩溃。
- 最小修复：把 syscall 号/封装集中到条件编译的 `.inc`（`#ifdef __x86_64__`），或改用运行时库提供的 `clock_gettime`/`write` 包装。
- 备注：受影响共 21 个 `.krt`：`Runtime/` 5 个、`BudgetSelection/Sources/` 6 个、`Artifacts/` 10 个。`RuntimeBenchmarks.py:81`、`NativeFibBenchmarks.py:35` 有 x86_64 守卫；`BudgetSelection/Measure.py`、`LLVMOptimization/Run.py`、`GeneralOptimization/Run.py`、`O3Optimization/Run.py` 无守卫。

##### [中] 97-33｜21 个 `.krt` 均为单次测量、无预热，重复完全依赖外部 harness
- 位置：`Test/Performance/Runtime/test_fib35.krt:21-29`（`BenchmarkNow` 只包住一次 `Kernel`）
- 类别：性能测量方法缺陷（单次测量、无预热）
- 触发条件：直接运行 `.krt` 产物，或换用不重复的 harness。
- 后果：冷启动、页错误、动态链接器开销计入单次结果，方差大且不可复现；`.krt` 自身无重复/中位数/最小值逻辑。
- 最小修复：在 `.krt` 内做 N 次循环取最小/中位数（注意别让编译器合并重复调用），或文档明确单次语义。
- 备注：`RuntimeBenchmarks.py` 默认 10 轮、`BudgetSelection/Measure.py` 默认 3 轮在外层重复；`Artifacts` 由 `Run.py` 默认 10 轮重复。

##### [中] 97-34｜NarrowArguments.krt 的期望值编码了有符号 30 位回绕语义，文件内无契约声明
- 位置：`Test/Performance/LLVMOptimization/BudgetSelection/Sources/NarrowArguments.krt:19-23`、`:28-30`
- 类别：与被测实现契约不符 / 数值语义依赖
- 触发条件：编译器对 `int30` 采用更宽中间算术、插入溢出检查，或规范未定义有符号溢出。
- 后果：初值 `536870911` 为 `INT30_MAX`，`value+(int30)3` 必然溢出；期望 `531908544` 只在「30 位回绕」下成立。实现一变测试即失败，且失败原因可能被误判为预算选择错误。
- 最小修复：在文件/规范中显式声明 `int30` 有符号溢出按二补码回绕，或改用不溢出的初值/运算。
- 备注：该用例的价值正是验证窄参数传递，但期望值必须与规范绑定。

##### [低] 97-35｜Parameters.krt 的期望值依赖有符号 64 位溢出/回绕的具体行为
- 位置：`Test/Performance/LLVMOptimization/BudgetSelection/Sources/Parameters.krt:19-23`、`:28-30`
- 类别：数值语义依赖
- 触发条件：`a+b`、`b-3` 等中间结果溢出 `int64`，编译器按 UB 假设优化。
- 后果：期望 `517069303` 可能被编译器的 UB 假设改变，测试随机失败或约束错误行为。
- 最小修复：显式使用无符号类型，或在规范中定义有符号溢出语义。
- 备注：与 97-34 同类；`BinaryTree.krt` 的 `Tree` 结果也在 `int64` 范围但 2^22 不溢出。

##### [中] 97-36｜BinaryTree.krt 的 `Tree(n-1)+Tree(n-1)` 可被 CSE 合并，测量工作量不稳定
- 位置：`Test/Performance/LLVMOptimization/BudgetSelection/Sources/BinaryTree.krt:19-22`（另见 `Artifacts/BinaryTree.krt:19-22`）
- 类别：性能测量方法缺陷 / 优化器消除被测代码
- 触发条件：编译器实现公共子表达式消除（两个实参完全相同）。
- 后果：递归调用数减半，基准测的不再是「二叉递归」的工作量；不同优化级别下 CSE 与否会使预算选择结论漂移。
- 最小修复：让两个递归实参不同（如 `Tree(n-1)+Tree(n-1-b)`），或明确声明允许 CSE 并把它计入被测优化。
- 备注：`Branching.krt` 的 `Paths(n-1)+Paths(n-3)` 无此问题。

##### [中] 97-37｜Artifacts/*.krt 与 BudgetSelection/Sources/*.krt 有 4 个同名重复，无同步机制
- 位置：`Test/Performance/LLVMOptimization/Artifacts/Fib35.krt:19-33`、`Fib40.krt:19-33`、`BinaryTree.krt:19-33`、`Branching.krt:19-33`（对应 `BudgetSelection/Sources/` 同名文件）
- 类别：覆盖缺口 / 维护风险
- 触发条件：修改 `Sources/` 中的负载（函数体/参数/期望值）而不同步 `Artifacts/`，或反之。
- 后果：两处基准负载漂移，预算选择与优化级别对比结论失真；当前两者仅 `main` 的报告顺序不同，函数体一致，靠人工保持。
- 最小修复：让 `Artifacts/` 由 `Sources/` 生成/软链，或加一致性校验（哈希/期望值比对）。
- 备注：`LLVMOptimization/Run.py:139-199` 同时使用 `Artifacts` 的 `.krt` 与 `.c`。

##### [中] 97-38｜Artifacts/*.c 无 `main`、无自校验，且与同名 `.krt` 的一致性无自动校验
- 位置：`Test/Performance/LLVMOptimization/Artifacts/Fib35.c:1-7`（另 9 个 `.c` 同）
- 类别：覆盖缺口 / 契约脆弱
- 触发条件：转写 C 与 `.krt` 语义漂移（如 `uint32` 与 `int32`、循环边界、运算符）。
- 后果：gcc 基线与 Krt 基准不同源，比较结论无效；`.c` 只能靠 `Driver.c` 的 `EXPECTED` 间接校验，而 `EXPECTED` 由构建传入，写错即整体误判。
- 最小修复：为 `.c` 增加自校验 `main`（或统一由 `.krt` 生成 `.c`），并加 `.c`/`.krt` 语义一致性测试。
- 备注：本次人工核对 10 组 `.c`/`.krt` 函数体一致；`Run.py:177-196` 用 gcc 编译 `.c` 并与 `Driver.c` 链接。

##### [低] 97-39｜Artifacts/*.krt 先写报告再校验，与 Sources/*.krt 顺序相反，行为不一致
- 位置：`Test/Performance/LLVMOptimization/Artifacts/Fib35.krt:30-32`（另 9 个同）
- 类别：一致性 / 可维护性
- 触发条件：结果错误时排查；或调用方依赖「有报告写出」判断进程是否走到计时结束。
- 后果：`Artifacts` 在结果错误时仍输出 16 字节报告并 `return 1`，`Sources` 则不输出报告；同类负载两种失败语义，harness 若假设其中一种会误判。
- 最小修复：统一顺序（建议先校验再写报告，或统一先写报告），并在文档中声明。
- 备注：`BudgetSelection/Measure.py:40` 与 `LLVMOptimization/Run.py:128` 都要求 `len(stderr)==16`，故 `Sources` 的错误路径会直接抛异常而非报「错误值」。

##### [低] 97-40｜test_compiler_hotspots 初始化内存安全跟踪但从不关闭/上报统计
- 位置：`Test/Performance/test_compiler_hotspots.c:336`
- 类别：资源泄漏 / 覆盖缺口
- 触发条件：每次运行；进程退出时依赖运行时自带 atexit 清理。
- 后果：若 `KrtMemorySafetyInit` 注册了堆/统计，未显式 shutdown 则泄漏量与统计不可见；测试无法报告「有泄漏」。
- 最小修复：在 `main` 返回前调用对应的 shutdown/统计接口并校验无泄漏（若接口存在）。
- 备注：未在允许读取范围找到该 API 的定义，仅凭此文件无法确认是否必需。

### 第九十八批：Test/SelfHost 全部源文件

#### 覆盖范围

- 枚举方式：`glob Test/SelfHost/**/*.{c,h,inc,krt}`，命中 24 个 `.krt`，**0 个 `.c/.h/.inc`**；逐个全量读取，共 811 行。
- 文件清单（行数）：
  - 顶层：`test_contract.krt`(308)、`test_lexer.krt`(32)、`test_object_fields.krt`(43)
  - `fixtures/numeric/`：`arithmetic_calls_arrays_fields.krt`(17)、`constant_float.krt`(1)、`ieee_comparisons_conversions.krt`(22)、`literal_rounding.krt`(57)、`loops_and_memory.krt`(1)、`nonstandard_widths.krt`(17)、`overload_precision.krt`(1)、`wide_arithmetic.krt`(18)、`wide_conversions.krt`(13)、`wide_references.krt`(4)
  - `fixtures/closure-abi/LegacyApply.krt`(1)、`fixtures/module-abi-wide/LegacyPhysical.krt`(5)
  - `fixtures/runtime-http/Consumer.krt`(50)、`Provider.krt`(81)
  - `examples/daily.krt`(23)；`examples/language-project/`：`project.krt`(7)、`src/Box.krt`(9)、`src/Main.krt`(39)；`examples/dispatch-project/`：`project.krt`(7)、`src/Main.krt`(31)、`src/Measurements.krt`(24)
- 范围外未审：同目录另有 `test_*.py` / `Bootstrap.py` 驱动、`provenance.txt`（含构建机绝对路径，未读取）、`obj/`、`bin/`、`.krtcache/` 生成物；均不属于允许的 `.c/.h/.inc/.krt`。
- 符号核对仅限定向 grep / 局部窗口：`KrtIrLowerReturnFunction`、`KrtIrValidateFunction`、`KrtKroEmitIrFunction`、`KrtKroObjectIsValid`、`KrtCompileSourceToFd/ToPath`、`KrtLexerNext` 签名。
- 自举一致性结论：`test_contract.krt` 的 magic=5198411 / version=2 / architecture=34404 与 B8/C3 字节，和当前 `KroBackend.krt` 发射器一致；未发现活动的 ABI/IR 版本错配。但多处把当前实现细节固化为期望（98-10、98-29、98-31）。


##### [高] 98-1｜fd=-1 使负向编译断言恒真
- 位置：`Test/SelfHost/test_contract.krt:25`
- 类别：断言恒真 / 负向测试失效
- 触发条件：`invalid_source`（`"int32 main() { }"`）若被语义阶段接受，`KrtCompileSourceToFd` 会继续写 header/text/symbols；fd=-1 必然写入失败（`KroBackend.krt:782` 要求 `syscall(1,...) == 64`），函数仍返回 false。
- 后果：无论编译是否真的失败，`if (...) { return 35; }` 都不触发，语义校验回归（空体 `int32 main` 被接受）无法被本测试发现。
- 最小修复：负向用例改用 `KrtCompileSource` 直接断言 false；写盘路径另用有效 fd（`/dev/null` 或临时文件）单独测试。
- 备注：该行注释称文件集成由 Stage1 driver 负责，但恒真缺陷独立存在。

##### [中] 98-2｜产物写 CWD、无清理、无内容回读断言
- 位置：`Test/SelfHost/test_contract.krt:55,75,80,94,172,259`
- 类别：资源残留 / 依赖 CWD / 契约覆盖缺口
- 触发条件：任意 CWD 下运行即创建 `contract.kro`、`argument-call.kro`、`two-argument-call.kro`、`real-call.kro`、`local.kro`、`locals.kro`，返回 0 后不删除；测试从不打开或校验这些文件。
- 后果：污染仓库/工作目录（重跑残留旧对象），且写盘字节正确性仅凭返回值，内容错误可静默通过。
- 最小修复：写入临时目录并在结束时删除；增加回读断言（至少文件存在、长度、头部 magic）。
- 备注：与写入器相关的字节级缺口见 98-6。

##### [低] 98-3｜cfg 全程未释放，提前 return 路径成批泄漏
- 位置：`Test/SelfHost/test_contract.krt:28`（无 `delete cfg`）；`:25,41,43,46,48,70,134,207` 等在 delete 之前 return
- 类别：资源泄漏
- 触发条件：`KrtIrFunction cfg = new KrtIrFunction()` 直到函数结束都没有 `delete cfg`；多条失败分支在已分配 `object/direct/emitted/cfg` 后直接 return。
- 后果：进程内对象泄漏（进程退出虽回收，但与项目显式 delete 约定不一致）。
- 最小修复：成功路径末尾 `delete cfg`；各负向分支先释放再 return，或统一 cleanup。
- 备注：零容忍口径下的明确泄漏点。

##### [中] 98-4｜四处解析后未检查 parser.errors
- 位置：`Test/SelfHost/test_contract.krt:131-134,148-150,164-166,283-284`
- 类别：断言缺失 / 静默通过
- 触发条件：probe/expression/variable/unary 解析后直接使用 AST（`probe.left.child_count`、`expression_ir.immediates[0]`），未像 `:103/:116/:193` 那样先判 `parser.errors != 0`。
- 后果：解析报错但 AST 碰巧满足形状/数值时测试继续并通过，掩盖语法回归。
- 最小修复：每处解析后立即 `if (parser.errors != 0) { return <专属码>; }`。
- 备注：`:164-166` 依赖 `KrtSemanticBindLocals` 成功间接暴露错误，并不等价。

##### [中] 98-5｜manifest 字段与符号/重定位内容断言缺口
- 位置：`Test/SelfHost/test_contract.krt:21-22`；调用程序 `:60-63,72-84`
- 类别：契约测试覆盖缺口（ABI/manifest）
- 触发条件：`KrtKroObjectIsValid`（`KroBackend.krt:471-473`）校验 15 个字段，测试只断言 magic/version/architecture/text_size/symbol_count/entry_point/total_reloc_count/strtab_size；`rodata_size`、`data_size`、`bss_size`、`relocation_count`、`text/rodata/data_reloc_count`、`bss_align` 从未断言。
- 后果：这些 manifest 字段被写成错误尺寸/计数时测试仍通过。
- 最小修复：对 `emitted` 逐字段断言（当前应为 0）；对 call_program 增加符号表名字节与重定位项的正向断言，而非只看 `text_size`。
- 备注：现发射器仅支持无重定位的紧凑对象，"正向重定位"断言需先扩展发射器。

##### [中] 98-6｜机器码字节断言只覆盖 immediate=0
- 位置：`Test/SelfHost/test_contract.krt:23`
- 类别：断言覆盖不足
- 触发条件：direct 函数 immediate 为默认 0，只验 `text_word0&255==184`（B8）与 `(text_word1>>8)&255==195`（C3）；imm32 高 3 字节恒 0，符号扩展/字节序错误不改变被测字节。
- 后果：非零立即数编码（如 helper 返回 7 的 real-call 路径）无任何字节级断言，编码错位只要这两字节仍为 184/195 即通过。
- 最小修复：构造 immediate≠0（含负数）的 IR 并断言全部 5 字节；对 `real-call.kro` 也做字节断言。
- 备注：`KrtKroEmitConstantHelperCall` 产物在 `:93-94` 只断言 `text_size`。

##### [中] 98-7｜源码长度以魔法数字硬编码传入
- 位置：`Test/SelfHost/test_contract.krt:89,100,113,129,146,162,169,176,180,184,190,200,207,213,218,224,230,237,247,256,259,271,281,288,295`
- 类别：非确定性 / 越界读
- 触发条件：字面量被编辑而长度未同步：偏大则 lexer/编译器读到字符串边界之外（内容取决于分配器与邻接数据）；偏小则静默截断。
- 后果：测试结果依赖堆布局（flaky）或假通过/假失败，且构成未定义读。
- 最小修复：改用语言内建长度（如 `source.Length`）或集中常量，禁止裸数字。
- 备注：本批人工核对了上述全部长度，当前与字面量一致；属维护期高危写法而非现行错误。

##### [低] 98-8｜opcode/token 数值以裸数字断言
- 位置：`Test/SelfHost/test_contract.krt:167`（11/12）、`:274-284`（61/43/42/33/45）
- 类别：自举一致性 / 可维护性
- 触发条件：`KrtIrOpcode`（`Ir.krt:1152`）或 token/op 枚举重新编号后，断言仍比对旧数字。
- 后果：测试要么假失败，要么在数字巧合下假通过，回归时无法定位语义。
- 最小修复：全部改用枚举常量（如 `KrtIrOpcode.StoreLocal`）。
- 备注：当前 11=StoreLocal、12=LoadLocal 与 `Ir.krt:1152` 一致。

##### [低] 98-9｜多个互斥失败原因共用同一返回码
- 位置：`Test/SelfHost/test_contract.krt:74-75`(45)、`:79-80`(76)、`:93-94`(47)、`:21-22`(14/29)、`:58`(40)
- 类别：诊断与实际错误不符
- 触发条件：编译失败与写盘失败、发射失败与写盘失败、以及同一断言内任一字段不符，都归为同一返回码。
- 后果：CI 只看到 45/76/47，无法区分编译器、Kro 写入器或磁盘问题，定位成本转嫁。
- 最小修复：为每类原因分配独立返回码，或输出结构化诊断。
- 备注：numeric fixtures 基本做到 1 断言 1 码，可作范式。

##### [低] 98-10｜把"发射器仅支持单 block"这一当前限制固化为期望
- 位置：`Test/SelfHost/test_contract.krt:41`
- 类别：自举一致性
- 触发条件：`KroBackend.krt:840` 明确 `block_count != 1` 即返回 false；一旦发射器支持多 block，该断言反而失败。
- 后果：增强后端会让契约测试变红，维护者可能误改测试而非确认行为变更。
- 最小修复：注释标明这是当前紧凑发射器的已知限制，并在扩展时同步更新。
- 备注：该负向断言与当前实现一致，非现行 bug。

##### [中] 98-11｜词法器测试只覆盖标识符与二进制字面量
- 位置：`Test/SelfHost/test_lexer.krt:5-27`
- 类别：契约覆盖缺口
- 触发条件：全部用例仅含标识符、十进制整数、二进制/非法二进制与 EOF；运算符、标点、注释、换行/制表、字符串、浮点、关键字、非法字符均未测。
- 后果：这些词法路径回归（注释吞 token、字符串转义错误等）不会使测试失败。
- 最小修复：为每类 token 增加 start/kind/length 三元组断言，补注释/换行/字符串用例。
- 备注：非法二进制的 5 个断言是有效覆盖，可作为扩展范式。

##### [低] 98-12｜EOF 的 length 断言不一致
- 位置：`Test/SelfHost/test_lexer.krt:14` 断言 `length==0`，`:27` 只断言 `start==27`
- 类别：断言缺失
- 触发条件：第二段输入结束后的 Eof token 若 length 被改成非 0，`:27` 不报错。
- 后果：同类不变量只在一处成立，回归检测不对称。
- 最小修复：`:27` 补 `token.length != 0`。
- 备注：影响小。

##### [低] 98-13｜别名对象的删除顺序依赖未声明的 delete 语义
- 位置：`Test/SelfHost/test_object_fields.krt:35-41`（`holder.box = first;` 后 `delete holder; delete first;`）
- 类别：资源管理 / 断言缺口
- 触发条件：若 class 的 `delete` 递归释放引用字段，holder 与 first 指向同一对象会被释放两次；若为浅释放则正常。测试未声明期望哪种语义。
- 后果：双重释放/UAF 或语义变更时测试无法区分，甚至在本文件内崩溃。
- 最小修复：明确并断言 delete 语义；或先断开 `holder.box = null` 再删，并补 null/default 负例。
- 备注：文件缺 null/default 访问用例。

##### [中] 98-14｜浮点断言容差过宽
- 位置：`Test/SelfHost/fixtures/numeric/ieee_comparisons_conversions.krt:14`（1.8e19..1.9e19）、`:17`（13.19..13.21）
- 类别：断言强度不足
- 触发条件：uint64→float64 转换偏差若干 ULP（1 ULP≈2）仍落在 1e18 宽区间；表达式折叠偏差 0.001 仍落在 ±0.01。
- 后果：转换/折叠的舍入模式或末位错误被容差吞掉，测试假绿。
- 最小修复：按精确值断言（如 `wd == 18446744073709551616.0`）或断言到位级。
- 备注：同文件 `:15-16,:18` 的精确断言是正确范式。

##### [中] 98-15｜NaN/无穷用例依赖 FP 环境与优化策略
- 位置：`Test/SelfHost/fixtures/numeric/ieee_comparisons_conversions.krt:7-10`
- 类别：非确定性 / 环境依赖
- 触发条件：`zero/zero`→NaN、`1.0/zero`→inf 依赖运行期 IEEE 语义；若使用 fast-math、常量折叠精度不同或目标 FPU 非 IEEE，比较结果可能变化。
- 后果：同一源码在不同优化等级/目标上结果不一致（flaky），或把平台差异误报为回归。
- 最小修复：在夹具/驱动中固定 FP 语义（禁快算、固定舍入模式），并补 `-0.0 == 0.0`、NaN 载荷保持等不变量。
- 备注：当前无 `-0.0 == 0.0` 与 NaN payload 断言。

##### [低] 98-16｜decimal→binary 位模式假设 round-half-even 但无出处
- 位置：`Test/SelfHost/fixtures/numeric/literal_rounding.krt:53-54`（v100/v101 为 2^-24 平局），全文件位模式同此假设
- 类别：自举一致性 / 断言依据
- 触发条件：若语言规范或字面量解析采用 round-half-away/truncation，这两个 tie 样例会假失败；反之实现改舍入方向，位模式也失去参照。
- 后果：夹具正确性依赖一个未在文件内引用的规范假设，维护者无法判断该改夹具还是实现。
- 最小修复：文件头注明所依据的 ISO/IEC 60559 舍入条款与参考实现来源，并与规范文档交叉引用。
- 备注：抽验 v10/v11（2^53±1、2^53+3）与 tie-to-even 一致。

##### [低] 98-17｜对 float 局部变量做指针类型双关
- 位置：`Test/SelfHost/fixtures/numeric/literal_rounding.krt:2-52`（`*((uint64*)&vN)` 系列，float32 用 `uint32*`）
- 类别：未定义行为 / 优化器敏感
- 触发条件：通过不兼容类型指针读取（严格别名/TBAA）；若后端按 C 风格别名规则优化，或对齐/表示假设不同，读回值可与预期不符。
- 后果：字面量测试可能被自身 UB 误编译，产生假失败或假通过。
- 最小修复：改用语言提供的位转换内建（若有），或语义明确的 `union`/`memcpy`。
- 备注：语言标榜 unsafe 指针能力，仍需明确别名规则。

##### [中] 98-18｜无界 while 在回归时会挂起而非失败
- 位置：`Test/SelfHost/fixtures/numeric/loops_and_memory.krt:1`（`while(a[0]>0){a[0]-=0.5;}`）
- 类别：非确定性 / 测试健壮性
- 触发条件：若浮点比较/减法被错误编译（如 `a[0]` 恒 >0 或步长错误），循环不终止；夹具与驱动无超时/迭代上限。
- 后果：CI 表现为挂起/超时而非明确非零退出码，且难以定位到该夹具。
- 最小修复：加迭代计数器上限并断言到达 0；或由驱动统一超时并映射为失败码。
- 备注：起始 2.5、步长 0.5 目前精确，属防御性缺口。

##### [低] 98-19｜隐式转换与数组边界路径未断言
- 位置：`Test/SelfHost/fixtures/numeric/arithmetic_calls_arrays_fields.krt:6-8`
- 类别：覆盖缺口
- 触发条件：`sum(a,b,4)` 混合 float64/float32/int32、`v[1]=2` 隐式 int→float32，均只检查结果值；无数组越界、NaN/inf、溢出用例。
- 后果：提升/转换规则或边界检查回归不触发失败。
- 最小修复：增加转换中间值断言与越界负例。
- 备注：happy-path 断言本身正确（3.75/7.75/3.5）。

##### [低] 98-20｜常量折叠用例过于单薄
- 位置：`Test/SelfHost/fixtures/numeric/constant_float.krt:1`
- 类别：覆盖缺口
- 触发条件：仅 `Pi+Values.Half` 一个表达式，未覆盖 float32/float64 常量提升边界、跨类常量与折叠时机。
- 后果：常量求值回归（如提升宽度错误）可能被单个精确样例掩盖。
- 最小修复：补充非精确值与混合宽度断言，并覆盖静态类常量访问。
- 备注：文件仅 1 行，属抽查级覆盖。

##### [低] 98-21｜非标准宽度只测自增回绕
- 位置：`Test/SelfHost/fixtures/numeric/nonstandard_widths.krt:6-15`
- 类别：覆盖缺口
- 触发条件：int30/uint30/int6/int14/uint62/int62 仅验证 ++/--/+= 的模 2^N 回绕；未测算术、比较、移位、转换与符号扩展。
- 后果：这些宽度的运算/转换语义错误不会被发现；有符号溢出还依赖实现定义。
- 最小修复：为每宽度增加跨运算与转换断言，并固定溢出为回绕语义。
- 备注：`pick(ref a,n)` 的 int30/int32 选择是有效 ABI 钉扎。

##### [低] 98-22｜values 数组未释放
- 位置：`Test/SelfHost/fixtures/numeric/nonstandard_widths.krt:12`（`new int30[2]`，全文件无 delete）
- 类别：资源泄漏
- 触发条件：测试结束前未 `delete values`。
- 后果：与同目录其他夹具（arithmetic/wide_arithmetic 均释放数组）不一致。
- 最小修复：末尾 `delete values`。
- 备注：进程退出回收，故低。

##### [低] 98-23｜重载精度用例仅两个调用
- 位置：`Test/SelfHost/fixtures/numeric/overload_precision.krt:1`
- 类别：覆盖缺口
- 触发条件：仅 `f(1.0f)`/`f(1.0)` 验证 float32/float64 选择；未测 int 字面量、歧义/隐式转换与浮点精度边界。
- 后果：重载决议的精度相关回归可能漏检。
- 最小修复：加入 `f(1)`（若语言允许）与非精确值断言。
- 备注：1 行文件，定位明确但覆盖弱。

##### [中] 98-24｜int128 算术缺少溢出/除零/移位界用例
- 位置：`Test/SelfHost/fixtures/numeric/wide_arithmetic.krt:4-16`
- 类别：覆盖缺口
- 触发条件：现断言覆盖移位、乘除、取余、复合赋值与类字段，但未触及 int128 加法溢出、除零、移位≥128、有符号/无符号混用。
- 后果：128 位边界与异常路径回归静默通过。
- 最小修复：补充上述边界断言（溢出若定义为回绕需注明）。
- 备注：现有一半断言数值准确（2^100/2^65 等），可扩展。

##### [中] 98-25｜宽整数↔浮点转换全部取精确值，舍入模式未测
- 位置：`Test/SelfHost/fixtures/numeric/wide_conversions.krt:5-9`
- 类别：覆盖缺口
- 触发条件：`d=x`(2^100)、`s=-1.0`、`big=2^100`、`large=2^127` 均精确可表示；仅 `(int128)-5.5` 涉及截断方向。非精确 int→float（如 2^100+1）与 float→int 的最近舍入/越界转换未测。
- 后果：浮点往返舍入规则错误不会被发现。
- 最小修复：加入 2^100+1→float→int 往返与超范围转换断言。
- 备注：`:7-8` 的截断方向断言有效。

##### [低] 98-26｜数组 a 未释放
- 位置：`Test/SelfHost/fixtures/numeric/wide_conversions.krt:4`
- 类别：资源泄漏
- 触发条件：`int128[] a=[x,-x]` 在 `:11` foreach 后不再使用，但无 `delete a`。
- 最小修复：末尾 `delete a`。
- 备注：进程退出回收，故低。

##### [低] 98-27｜arr 与 box 未释放
- 位置：`Test/SelfHost/fixtures/numeric/wide_references.krt:2-3`
- 类别：资源泄漏
- 触发条件：`int128[] arr=[a,b]; Box box=new Box();` 之后全文件无任何 delete。
- 最小修复：末尾 `delete arr; delete box;`。
- 备注：同目录多数夹具释放数组；此文件仅 4 行。

##### [高] 98-28｜HTTP 成功路径的 status/body 未被断言
- 位置：`Test/SelfHost/fixtures/runtime-http/Consumer.krt:22-26`
- 类别：静默通过 / 契约覆盖缺口
- 触发条件：Reply 只断言 `request.tag[4]`、`snapshot.tag[0]/tag[4]`、`snapshot.id`；`response.status=snapshot.status; response.body=snapshot.body` 后直接 return，main 只汇总 audit 计数。路由成功语义（kind==0 → 200 与 `"你好，模块！\n"`，kind==1 → 404）在整条链路无断言。
- 后果：状态码或响应体被错误实现/回归时，只要 tag/id 正确且 Provider 不抛错，测试仍返回 0。
- 最小修复：Reply 内断言 `snapshot.status==200 && snapshot.body=="你好，模块！\n"`；或在 main 中经 handler 做一次端到端请求并校验 HttpResponse。
- 备注：`Provider.krt:53-57` 计算 status/body 却同样无断言，属双向缺口。

##### [中] 98-29｜audit 计数 6/9/6/0 无推导且耦合运行时枚举次数
- 位置：`Test/SelfHost/fixtures/runtime-http/Consumer.krt:46-47`
- 类别：自举一致性 / 非确定性
- 触发条件：Provider 每次完整 `Routes()` 产出 2 个 rule，`Dispose`/`resources` 计数守恒；期望 `enumerated==6、disposed==9、handled==6、resources==0` 依赖 `HttpServer.Run` 内部枚举/释放调度（部分枚举可能提前终止），文件内无说明或推导。
- 后果：运行时版本或调度策略变化即假失败；反之计数语义被破坏也可能看似合理。
- 最小修复：由 Provider 暴露确定性统计（固定请求数并按请求数推导），或在夹具内注明 6/9 的来源与守恒不变量。
- 备注：`resources==0` 与 `disposed==9` 的关系需注释写清。

##### [中] 98-30｜Provider 的 /fail 503 分支无任何用例触发
- 位置：`Test/SelfHost/fixtures/runtime-http/Provider.krt:48-52`
- 类别：覆盖缺口
- 触发条件：Consumer 只请求默认 path（handler 的 path 未传 `"/fail"`）；`throw failure` 的 Failure 结构、status 503、跨模块异常传播均未验证。
- 后果：错误路径回归静默通过；Provider 整文件无自身断言，全靠 Consumer 间接覆盖。
- 最小修复：Consumer/main 或外部驱动增加一次 `"/fail"` 请求并断言 503 与 id/tag。
- 备注：Provider 属被动 fixture。

##### [低] 98-31｜示例输出文本未被捕获断言
- 位置：`Test/SelfHost/examples/daily.krt:18-19`
- 类别：契约覆盖缺口
- 触发条件：`Console.WriteLine("Hello from SelfHost")` 与 `WriteLine(counter.Value())` 的输出不设防；唯一判据是退出码 0。
- 后果：WriteLine 格式/编码/求值回归（输出错值）仍返回 0。
- 最小修复：捕获 stdout 断言两行内容，或至少断言 `counter.Value()==42`。
- 备注：README 记录预期输出，但自动化层面未校验。

##### [低] 98-32｜泛型静态 Count 的按实例隔离未验证
- 位置：`Test/SelfHost/examples/language-project/src/Main.krt:20`（`Box<float64>.Count != 1`）
- 类别：覆盖缺口
- 触发条件：只实例化过一次 `Box<float64>`；若静态字段在所有泛型实例间共享，此断言仍为 1，无法区分。
- 后果：泛型静态字段作用域回归漏检。
- 最小修复：再用 `Box<int32>` 构造一次并断言两者计数独立。
- 备注：同文件 Average/异常路径断言较完整。

##### [中] 98-33｜foreach 中删除元素后又删除数组，所有权语义不明
- 位置：`Test/SelfHost/examples/dispatch-project/src/Main.krt:13-16`
- 类别：资源管理 / 潜在双重释放
- 触发条件：`foreach (IValue value in values) { ...; delete value; }` 逐个释放元素，随后 `delete values`；若数组析构也释放元素引用即双重释放，若枚举器缓存引用则 UAF。
- 后果：内存破坏或崩溃，且当前断言无法区分。
- 最小修复：明确数组与元素所有权；要么只 `delete values`，要么循环内仅解绑、最后统一释放并断言计数。
- 备注：`:7,:11` 的 `view` 别名在 `delete measurement` 后悬垂但未被使用，属次要清理点。

##### [低] 98-34｜Configure 脚本无自身契约断言，glob/输出依赖 CWD
- 位置：`Test/SelfHost/examples/language-project/project.krt:4-6`
- 类别：契约覆盖缺口 / 依赖 CWD
- 触发条件：`Sources("src/*.krt")`、`Output("bin/${config}/LanguageDemo")` 是否被驱动解释无校验；Configure 无返回值/字段断言。
- 后果：驱动忽略 Name/Sources/Optimization 时项目仍"配置成功"，工程级回归只在最终二进制层面暴露。
- 最小修复：配置后回读 `p` 字段断言，或用临时目录固定 CWD。
- 备注：`import "Box.krt"` 的相对路径同样依赖工程根。

##### [低] 98-35｜同上：Dispatch 工程配置无断言
- 位置：`Test/SelfHost/examples/dispatch-project/project.krt:4-6`
- 类别：契约覆盖缺口 / 依赖 CWD
- 触发条件/后果/修复：同 98-34（`Sources`/`Output`/`Optimization` 无校验）。
- 备注：两个 project.krt 为同构副本，问题独立成条。

##### [低] 98-36｜closure ABI 探针无入口、无断言、无 ABI 版本标记
- 位置：`Test/SelfHost/fixtures/closure-abi/LegacyApply.krt:1`
- 类别：自举一致性 / 覆盖缺口
- 触发条件：文件只有 `Apply(fn(int32)->int32,...)`，无 main、无 assert、无命名空间；接受/拒绝完全由外部 harness 决定，且文件名 `Legacy` 暗示针对旧 ABI。
- 后果：harness 名单漂移后探针失效无人察觉；夹具与当前 ABI 的版本关系未在文件内声明。
- 最小修复：文件头注明目标 ABI 版本与期望（拒绝/接受），并在 harness 中校验来源哈希。
- 备注：`fn` 回调按值传递是该探针的核心约定。

##### [低] 98-37｜wide ABI 探针同样无内嵌断言与版本标记
- 位置：`Test/SelfHost/fixtures/module-abi-wide/LegacyPhysical.krt:1-5`
- 类别：自举一致性 / 覆盖缺口
- 触发条件：uint128 的 value/ref/fn callback 三种约定仅由源文本表达，无 main/assert；目录另有旧对象 provenance（本批未审，非允许扩展名）。
- 后果：value-vs-ref 约定若被当前 ABI 悄然接受而 rejection gate 未跑，测试面为空。
- 最小修复：同 98-36，注明 ABI 版本与拒绝预期并绑定哈希。
- 备注：与 docs 描述的 rejection gate 对应，但该保证不在本文件内。

##### [低] 98-38｜Box.krt 无直接断言
- 位置：`Test/SelfHost/examples/language-project/src/Box.krt:3-9`
- 类别：覆盖缺口
- 触发条件：泛型约束 `where T:struct`、静态 `Count`、构造自增仅由 `Main.krt:20` 间接覆盖。
- 后果：约束违反（引用类型实参）等负向场景无用例。
- 最小修复：增加约束负例与 Count 隔离断言（与 98-32 合并设计）。
- 备注：支撑文件。

##### [低] 98-39｜Measurements.krt 无直接断言
- 位置：`Test/SelfHost/examples/dispatch-project/src/Measurements.krt:3-24`
- 类别：覆盖缺口
- 触发条件：接口继承、抽象方法、虚方法/override、泛型约束 `where T:IValue` 仅由 `Main.krt` 间接覆盖。
- 后果：接口分派与约束的负向回归漏检。
- 最小修复：补充抽象类未实现、约束不满足等负例。
- 备注：支撑文件；主逻辑断言在 Main.krt。

### 第九十九批：Test/SyntaxAudit 框架与 cases 用例

#### 覆盖范围

- 枚举：`glob **/*.{c,h,inc,krt}`（214 条）与 `glob *`（全量 575 条，含 `.exp/.kro/.log/.ir/.asm/.py/.sh/.md/.want`）。
- 实读：`Test/SyntaxAudit/` 下全部 `.krt` 源文件 **79 个，约 1330 行**：
  - 根：`prelude.krt`(164 行)、`prelude2.krt`(27 行)、`T00_Hello.krt`(5 行)
  - `cases/` 76 个：C01、C02、C03、C10–C14、C16–C18、C20–C29、C30–C39、C40–C43、C50–C56、C60–C79、M1_lib、M1_main、test_c80–test_c91
- 未读（超出本次授权扩展名范围；`probes/` 归他人）：`run.sh`、`test_language_regressions.py`、`ReadMe.md`、`.gitignore`、`cases/*.exp`（76 个）、`out/`、根目录残留 `*.kro`。
- 关键限制：本目录及其子目录**不含任何 `.c/.h/.inc`**（`glob *.{c,h,inc}` 返回 0），框架全部在授权扩展名之外。因此审计重点 1（期望诊断码/文本是否真被校验）只能从用例内容侧证：`cases/` 下 76 个用例**没有一条断言诊断码或诊断文本**；`.exp` 与 `run.sh` 的实际比对逻辑本批次无法核验（见 99-2、99-3 及备注）。
- 说明：`probes/` 未读；`out/`、根 `.kro` 仅凭 glob 枚举记录存在，未读内容。


##### [致命] 99-1｜C72_lambda 用占位符 "?" 断言，lambda 与 Apply 从未被调用
- 位置：`cases/C72_lambda.krt:4-7`
- 类别：静默通过 / 断言机制失效
- 触发条件：编译器对 lambda、类型推断、`object` 类型实现错误，只要进程不崩溃即通过
- 后果：唯一输出是第 7 行字面量 `P("?", 1)`，与实现正确性无关；第 6 行 `var inc = function (int32 a) => a + 1;` 与第 4 行 `Apply` 定义后从未使用，lambda 语法/推断/调用约定零校验
- 最小修复：改为 `D(inc(4))` 并让 `Apply` 真正接收并调用 `f`，输出确定值；否则删除该用例
- 备注：`object` 与 `=>` lambda 语法全目录仅此一处覆盖

##### [致命] 99-2｜cases/ 下 76 个用例全为正向程序，语法错误路径零覆盖
- 位置：`cases/*.krt` 全部 76 个（无任何负向用例）；配套 `cases/*.exp` 存在但不在授权读取范围
- 类别：覆盖缺口 / 断言机制
- 触发条件：任何语法错误路径——未闭合注释、非法转义、括号不匹配、非法类型名、重复定义、const 赋值、break/continue 越界、switch 重复 case 等
- 后果：无一条用例期望编译失败或诊断码，SyntaxAudit 对语法错误路径零覆盖；错误实现只要不崩溃就可能全绿
- 最小修复：每个语法错误路径至少一条用例，并在用例中声明期望诊断码/文本（当前 `.krt` 无任何诊断断言字段）
- 备注：`.exp` 是否含诊断码无法在本范围核验；建议下一批授权读取 `run.sh`/`test_language_regressions.py`

##### [致命] 99-3｜返回码断言无映射且 stdout 为空，框架若只比 stdout 则整批静默通过
- 位置：`cases/C02_retcode.krt:3-4`；`cases/test_c81_pointer_increment.krt:6,9,11`（test_c80–c91 同型）
- 类别：返回码忽略 / 断言机制
- 触发条件：框架只比对 stdout（这些用例 stdout 为空），或未把 `main` 返回值作为进程退出码
- 后果：`return 7` 与 `return 1..100` 全部退化为静默通过，12 个指针用例的 100+ 断言点全部失效
- 最小修复：用例内写明「返回码→期望」映射，框架断言退出码；或改为输出确定字符串
- 备注：C02 是唯一 C 系列返回码用例，返回 7 无注释

##### [高] 99-4｜main 签名与函数声明风格三种混用
- 位置：`cases/C01_empty.krt:3`（`function main()`）；`cases/C02_retcode.krt:3`（`int32 main()`）；`cases/test_c80_unsafe_pointers.krt:4`（`int main()`）；声明风格 `function P(...)`（`C01_empty.krt:2`）vs `int add(...)`（`test_c80_unsafe_pointers.krt:2`）
- 类别：用例自相矛盾 / 规范漂移
- 触发条件：编译器只接受其中一种 main 签名或一种函数声明风格
- 后果：约 60 个 `function main()` 用例或 12 个 `int main()` 用例整批编译失败；两种声明风格混用说明规范未固化
- 最小修复：统一 main 签名与声明语法；若确要测多种写法，应显式分组并在 ReadMe 说明规范
- 备注：`T00_Hello.krt` 是第三种（无 main）

##### [高] 99-5｜裸 syscall 号硬编码，Linux x86-64 专属
- 位置：每个用例第 1–2 行，如 `cases/C01_empty.krt:1-2`、`cases/C50_arrconst.krt:5`（test_c80–c91 用 `stackalloc`，不受影响）
- 类别：平台/ABI 依赖（非确定性）
- 触发条件：在非 x86-64 Linux（如本机 Android aarch64）上运行
- 后果：syscall 号 `1`(write)/`9`(mmap)/`34`(MAP_PRIVATE|MAP_ANONYMOUS) 在 aarch64 上并非同值，用例直接失败或语义错误
- 最小修复：改用语言内建输出/分配（`new`、Console）或经 `krt.mem` 封装，不裸写 syscall 号
- 备注：`syscall(1,1,...)` 还依赖 fd 1 可写（见 99-27）

##### [高] 99-6｜mmap/string 强转数组属未定义行为，且与 C71 注释自相矛盾
- 位置：`cases/C50_arrconst.krt:5-6`、`C51_arrvaridx.krt:5-6`、`C53_arrfill.krt:5-6`、`C77_charrw.krt:5-6`、`C78_intvaridx2.krt:5-6`（mmap→`T[]`）；`cases/C54_strconst.krt:6`、`C56_strescape.krt:6`、`C79_strlenwalk.krt:4`（string→`char[]`）
- 类别：用例自相矛盾 / 未定义行为
- 触发条件：编译器对 int64/string 到数组的强转实现与用例假设不一致（字符串带长度头、无 NUL 结尾、指针不可当数组基址）
- 后果：C54/C56/C79 依赖字符串内存布局与 NUL 结尾；C50/C51/C53/C77/C78 依赖裸 mmap 指针可直接当数组。`cases/C71_foreach.krt:5` 注释已明确写「裸 mmap 指针强转 T[] 属未定义行为(同 C#), 改正规分配」，但同目录 7 个用例仍用旧模式
- 最小修复：统一改用 `new int32[n]` 与语言内建字符串索引，删除强转写法
- 备注：与 99-7 叠加；`C77_charrw.krt:4`、`C78_intvaridx2.krt:4` 注释亦自认 mmap

##### [高] 99-7｜mmap 返回值未校验，失败即段错误
- 位置：`cases/C50_arrconst.krt:5`、`C51_arrvaridx.krt:5`、`C53_arrfill.krt:5`、`C77_charrw.krt:5`、`C78_intvaridx2.krt:5`
- 类别：静默通过 / 错误处理缺失
- 触发条件：`syscall(9,...)` 失败返回 -1（ENOMEM、权限、平台号错）
- 后果：`(int32[])-1` 后写 `arr[0]` 直接段错误，用例以崩溃而非断言失败收场；框架若只看退出码会按约定误判
- 最小修复：检查返回值 `< 0` 时输出确定错误标记；或改用 `new`
- 备注：mmap 长度固定 4096，未校验

##### [高] 99-8｜D 辅助函数把 ≥5/负数折叠为单字符，多个用例断言退化
- 位置：`cases/C21_divmod.krt:3,8-11`；`C22_compound.krt:3,10`；`C23_incdec.krt:3,10`；`C40_fib.krt:3,9-10`；`C41_manyargs.krt:3,8`；`C42_forward.krt:3,5`；`C68_struct.krt:3,12`；`C73_namespace.krt:3,10`；`C75_refparam.krt:3,10`
- 类别：断言机制不可靠
- 触发条件：被测值 ≥5（或负数）时，D 统一打印 `"+"`/`"N"`
- 后果：C40 `Fib(10)=55` 与 99 都输出 `"++"`；C41 21 与任意 ≥5 都 `"+"`；C21 `-3` 与 `-1` 都 `"NN"`——断言退化为「值 ≥5」
- 最小修复：D 支持完整十进制输出（可复用 `prelude2.krt` 的 `PrintNum`），或直接比较后打印 Y/N
- 备注：C30/C31/C32/C36 的 D 分支较全，可参考

##### [高] 99-9｜C75 ref 形参在调用处未加 ref
- 位置：`cases/C75_refparam.krt:4,9`
- 类别：用例与规范漂移 / 自相矛盾
- 触发条件：语言要求调用处显式 `ref`（C# 式）
- 后果：用例直接编译失败；若语言允许隐式 ref，则 `ref` 传引用与传值无法区分，等于没测
- 最小修复：改为 `Bump(ref x)`，并另加一个传值对照用例
- 备注：D(10) 亦为弱断言（见 99-8）

##### [高] 99-10｜C56 的 `\e` 转义与 `cs[7]=='\\'` 断言矛盾
- 位置：`cases/C56_strescape.krt:5,7`
- 类别：用例自相矛盾
- 触发条件：`\e` 被实现为 ESC(0x1B)、或作为未知转义按字面处理
- 后果：第 7 行断言 `cs[7] == '\\'` 与 `\e` 语义冲突（若 `\e`=ESC 应判 0x1B；若按字面应得 `'e'`），用例要么恒失败，要么掩盖真实转义行为
- 最小修复：先确认语言转义表；若意图是反斜杠应写 `\\` 并同步调整索引
- 备注：索引 1/3/5/7 与 `\t \n \" \e` 一一对齐，说明作者按「一个转义=1 字符」计数

##### [高] 99-12｜C82/C90 把未定义的求值顺序固化为断言
- 位置：`cases/test_c82_pointer_evaluation.krt:7-10`；`cases/test_c90_pointer_arguments.krt:8-10`
- 类别：用例自相矛盾 / 规范漂移
- 触发条件：语言规范未规定操作数/实参求值顺序（C/C# 均为未定义或实现定义）
- 后果：用例断言确定结果（`q==buf+1`、`check(...)==1`），规范一改或编译器换求值顺序即误报；把未定义行为写成断言
- 最小修复：确认规范并写入注释；若规范明确「从左到右」则保留，否则删除
- 备注：与 C83 的三元单分支求值（有定义）性质不同

##### [高] 99-13｜C86 依赖栈增长方向与地址算术
- 位置：`cases/test_c86_pointer_stack.krt:12-13`
- 类别：非确定性 / 平台依赖
- 触发条件：栈增长方向、优化器栈布局、对齐策略不同
- 后果：`(usize)&marker - (usize)a` 依赖 marker 地址高于 a；否则无符号回绕成巨大值恒 `> 1048576` → 误报 `return 3`；16 字节对齐断言也随 ABI 变化
- 最小修复：改用语言内建栈指针查询，或删除地址算术只测功能
- 备注：`stackalloc int[count]`（count=4097）动态长度亦依赖栈实现

##### [高] 99-14｜C87 用 `(uint32*)` 位模式改写 float，别名 UB
- 位置：`cases/test_c87_pointer_float.krt:17-24`
- 类别：未定义行为 / 非确定性
- 触发条件：严格别名规则下经 `(uint32*)p` 写 float 对象；NaN 比较依赖具体位模式
- 后果：编译器优化可能改写/消除位模式写入，用例结果不可信；`*p==*p`、`*p<0.0` 等判定依赖 `0x7FC00000` 与 `-0.0==0.0`
- 最小修复：若语言允许位重解释，需在规范中明确；否则改用语言提供的 bitcast
- 备注：`*bits=2147483648` 依赖 -0.0 与 0.0 相等

##### [高] 99-17｜C79 strlen 走查依赖 NUL 结尾，可能越界读
- 位置：`cases/C79_strlenwalk.krt:3-9`
- 类别：未定义行为 / 越界
- 触发条件：字符串内存无 NUL 结尾（如带长度头）或越界访问
- 后果：`while (cs[n] != '\0')` 可能无限越界读，用例崩溃或结果不定
- 最小修复：用语言内建长度，或确保 NUL 结尾；`(char[])s` 强转同 99-6
- 备注：C54/C56 同用 `(char[])s`

##### [中] 99-11｜C91 使用可疑类型名 `int30`
- 位置：`cases/test_c91_pointer_pressure.krt:3-102`
- 类别：用例与规范漂移 / 疑似非法类型
- 触发条件：`int30` 不是语言合法类型名（疑为 `int32` 笔误）
- 后果：整用例编译失败；若合法，30 位整型的符号/溢出行为未定义且无用例
- 最小修复：确认类型名；若为任意位宽特性，补 30 位边界用例
- 备注：同文件返回码 1..100，stdout 为空（见 99-3/99-29）

##### [中] 99-15｜C11 声明 int64 后从未校验
- 位置：`cases/C11_inttypes.krt:7`
- 类别：覆盖缺口
- 触发条件：int64 声明/初始化/比较实现错误
- 后果：`int64 d = 5000000000;` 定义后从未使用，int64 路径零校验
- 最小修复：补 `if (d == 5000000000) { ok = true; }`
- 备注：同文件 int8/int16/int32 均有断言

##### [中] 99-16｜C23 只验最终值，未验前缀/后缀表达式产出值
- 位置：`cases/C23_incdec.krt:6-9`
- 类别：覆盖缺口
- 触发条件：`i++`/`++i` 表达式产出的值实现错误（如都返回新值）
- 后果：只验证最终 `i=5`，`int32 y = i++` 这类「表达式值」路径无用例
- 最小修复：补 `int32 y = i++; D(y);` 等对照
- 备注：D(5) 为弱断言（见 99-8）

##### [中] 99-18｜T00_Hello 与其余用例运行时/风格完全冲突
- 位置：`T00_Hello.krt:1-5`
- 类别：用例自相矛盾 / 残留
- 触发条件：框架把根目录 `.krt` 一并编译
- 后果：`using System; Console.WriteLine(...)` 与其余用例的 `function main()` + `syscall` 运行时完全不同，疑似另一前端或历史残留；不支持必失败，支持则说明规范双轨
- 最小修复：明确其定位（独立冒烟测试或删除）并写入 ReadMe
- 备注：该文件无 `main` 包装

##### [中] 99-19｜两个 prelude 重复定义且 OutInt(0) 无输出，均未被引用
- 位置：`prelude.krt:16-27,29-164`；`prelude2.krt:3-15,16-26`
- 类别：残留 / 用例自相矛盾
- 触发条件：两个 prelude 同时被包含
- 后果：`OutDigit` 重复定义且 else 分支不同（`"9"` vs `"?"`）；`prelude.krt` 的 `OutInt(0)` 因 `started` 恒 false 无任何输出（第 152-163 行），负数 `INT32_MIN` 取负溢出；两 prelude 的 `OutStrN/OutInt/PrintNum` 在 `cases/` 中均无调用
- 最小修复：合并为一个 prelude 并修复 `OutInt(0)`；或删除未引用者
- 备注：各用例自带 `P`，prelude 实为死代码

##### [中] 99-20｜C15 期望文件成孤儿，编号多处空洞
- 位置：`cases/C15_hexbin.exp`（无对应 `.krt`）；缺 C04–C09、C19、C44–C49、C57–C59
- 类别：覆盖缺口 / 残留
- 触发条件：框架按 `cases/*.krt` 收集用例
- 后果：C15 期望值成孤儿（或触发「缺用例」告警）；编号空洞说明计划中的进制/位运算等用例未落地
- 最小修复：补 C15 源文件或删除其 `.exp`；补齐或重编号
- 备注：`C15_hexbin.exp` 是本目录唯一「有期望无源」的文件

##### [中] 99-21｜源码目录残留构建产物，清理缺失
- 位置：`Test/SyntaxAudit/C22_compound.kro`、`C23_incdec.kro`、`C41_manyargs.kro`、`C68_struct.kro`、`C73_namespace.kro`、`C75_refparam.kro`；`Test/SyntaxAudit/out/`（约 130 个 `.kro`/`.log`/`bin_*`）
- 类别：残留产物 / 清理缺失
- 触发条件：源码树即构建树
- 后果：根目录 `.kro` 与源同名易混淆；`out/` 中 `.kro`/`.log`/`bin_*` 混杂，审计或比对时可能误读旧产物
- 最小修复：构建前清理，产物统一入 `out/` 并确保 `.gitignore` 生效
- 备注：仅 glob 枚举所见，未读内容

##### [中] 99-22｜mmap 后从不 munmap
- 位置：`cases/C50_arrconst.krt:5`、`C51_arrvaridx.krt:5`、`C53_arrfill.krt:5`、`C77_charrw.krt:5`、`C78_intvaridx2.krt:5`
- 类别：资源清理缺失
- 触发条件：用例反复运行或进程驻留
- 后果：每次 mmap 4096 字节不回收，仅靠进程退出清理
- 最小修复：改用 `new` 或补 munmap
- 备注：与 99-6/99-7 同因但独立可修

##### [中] 99-23｜运算符与边界路径覆盖缺口（含一元负号缺位）
- 位置：`cases/C21_divmod.krt:8`（用 `(0-17)` 回避一元负号，且未测除零/溢出）；`C22_compound.krt:6-9`（缺 `%=` `&=` `|=` `^=` `<<=` `>>=`）；`C25_cmp.krt:5-11`（只 int，缺 float/字符串比较）；`C28_bitwise.krt:11-12`（移位只 4 位，缺负数/溢出/移≥宽度）；`C12_uinttypes.krt:7`（只测最大值相等，缺回绕）；`C24_float.krt:7-8`（只测精确可表示值，缺精度/舍入）；`C76_casts.krt:6-7`（缺窄化溢出/浮点截断）；`C20_prec.krt:5`（只 `+ - *`，缺 `/` `%` 与结合性）；`C14_streq.krt:5-6`（缺 `<` `>`）
- 类别：覆盖缺口
- 触发条件：上述路径实现错误
- 后果：相关语法/溢出路径无用例，错误实现可全绿；全目录无一元负号用例
- 最小修复：按路径补用例，越界/溢出类须有确定期望
- 备注：`(0-17)` 的写法暗示一元负号可能缺失，需确认

##### [中] 99-24｜控制流/函数/异常路径覆盖缺口
- 位置：`cases/C31_break.krt:6`、`C32_continue.krt:6`（只 while，缺 for/switch/嵌套）；`C37_switch.krt:6-16`（只命中 case 2，缺 default、fallthrough、重复 case、无 break）；`C38_earlyret.krt:4-8`（只 void 提前返回，缺有返回值函数的提前返回）；`C41_manyargs.krt:4`（6 参数，缺 >6 与寄存器溢出边界）；`C34_for_decl.krt:5`、`C35_for_outer.krt:6`（缺递减/多变量/空体）；`C42_forward.krt:4-9`（只函数前向，缺类/变量）；`C64_inherit.krt:10-12`、`C66_interface.krt:10-12`（只静态类型调用，缺基类引用多态）；`C70_trycatch.krt:5-10`（缺 finally、异常类型过滤、无 catch 传播）
- 类别：覆盖缺口
- 触发条件：上述路径实现错误
- 后果：控制流、多态、异常路径错误可全绿
- 最小修复：按路径补用例

##### [中] 99-25｜类型/对象语义覆盖缺口
- 位置：`cases/C68_struct.krt:8-12`（缺拷贝/传参/返回）；`C63_staticfield.krt:7-10`（缺多实例共享/继承隐藏）；`C69_generic.krt:9-12`（缺引用类型实例化/约束/默认值）；`C67_enum.krt:9-11`（缺未赋值项/重复值/显式底层类型）；`C74_null.krt:7-9`（缺 `!= null`、空引用成员访问）；`C71_foreach.krt:6-10`（只 int32[]，缺其他类型/嵌套）；`C55_strconcat.krt:5-8`（缺变量+字面量/链式）；`C54_strconst.krt:5-9`（只读，写见 C77）；`C61_instance.krt:13-16`（只经方法，缺字段直访）；`C62_ctor.krt:11-13`（缺默认构造/字段初始化器）；`C65_this.krt:11-14`（只测同名遮蔽）；`C10_var_infer.krt:4`（只 int，缺推断失败路径）；`C13_boolchar.krt:5-7`（缺 char 转义/算术）；`C17_global.krt:3-6`（只 main 内改，缺跨函数）；`C39_scope.krt:5-10`（只块作用域，缺 for/if 作用域）
- 类别：覆盖缺口
- 触发条件：上述语义实现错误
- 后果：相关类型/对象路径错误可全绿
- 最小修复：按路径补用例

##### [中] 99-26｜M1 多文件用例依赖构建顺序与 CWD
- 位置：`cases/M1_lib.krt:1-3`、`cases/M1_main.krt:4-5`；期望文件 `cases/M1_multi.exp`
- 类别：依赖 CWD / 构建顺序
- 触发条件：框架按单文件编译，或从不同 CWD 调用
- 后果：`LibAdd` 未链接 → 编译/链接失败或误报
- 最小修复：用例内显式声明多文件依赖与编译顺序
- 备注：`M1_lib.krt` 无 main，必须与 `M1_main.krt` 联合构建

##### [中] 99-27｜输出直写 fd 1，依赖环境且无编码/长度约定
- 位置：所有用例第 2 行 `syscall(1, 1, (int64)s, (int64)len, 0, 0, 0)`
- 类别：依赖环境 / 断言机制
- 触发条件：stdout 被重定向到非 fd 1、经管道捕获、或 fd 1 被关闭
- 后果：输出不可见 → 所有基于 stdout 的用例静默通过或误判
- 最小修复：改用语言内建输出；或在框架中显式约定 fd 1
- 备注：与 99-5 同源

##### [中] 99-28｜test_c80 依赖来源不明的命名空间与 unsafe 块语法
- 位置：`cases/test_c80_unsafe_pointers.krt:5`（`unsafe(using krt.mem, Acme.Ffi.Native;)`）、`:1-3`（首行注释 `// requested example`，C 风格声明）
- 类别：用例与规范漂移 / 来源不明
- 触发条件：`Acme.Ffi.Native` 命名空间不存在，或 `unsafe(...)`/`using` 块语法与规范不符
- 后果：整用例编译失败；`Acme.Ffi.Native` 疑似需求示例占位符，不是真实库
- 最小修复：确认真实命名空间与 unsafe 语法，删除占位符
- 备注：test_c80–c91 全部 `int main()` + C 风格声明，与 C 系列不一致（见 99-4）

##### [中] 99-29｜返回码→断言点映射缺失
- 位置：`cases/test_c91_pointer_pressure.krt:203-302`（返回 1..100）；`cases/C02_retcode.krt:4`（返回 7）
- 类别：诊断码/期望映射缺失
- 触发条件：维护者定位失败点
- 后果：返回码无注释、无映射表，失败时无法判断对应哪条断言；与 99-3 叠加
- 最小修复：在文件头或 ReadMe 给出「返回码→断言点」表
- 备注：C 系列其余用例用 stdout 断言，风格不统一

##### [低] 99-30｜C30 定义 D 但从未使用
- 位置：`cases/C30_while.krt:3`
- 类别：残留 / 死代码
- 触发条件：无
- 后果：增加维护噪声，掩盖「该用例未做数值断言」的事实
- 最小修复：删除 D 或启用
- 备注：main 只 `P("w")`

##### [低] 99-31｜用例注释暴露「示例拼凑」痕迹，期望来源不可追溯
- 位置：`cases/C71_foreach.krt:5`（注释宣称已改正规分配，同目录其他用例未改）；`C77_charrw.krt:4`、`C78_intvaridx2.krt:4`（注释自认 mmap 缓冲）；`test_c80_unsafe_pointers.krt:1`（`// requested example`）
- 类别：文档漂移 / 来源不明
- 触发条件：维护者依据注释判断期望
- 后果：注释与实现/规范脱节，无法判断期望值来自规范还是临时试值
- 最小修复：为每条用例补规范条目引用
- 备注：与 99-6、99-28 相关

##### [低] 99-32｜`int128`/`fn` 类型语法仅单点覆盖
- 位置：`cases/test_c85_pointer_callback.krt:3,12-13`
- 类别：覆盖缺口
- 触发条件：int128 边界/溢出、`fn` 类型不匹配实现错误
- 后果：缺 int128 边界与 `fn` 签名校验用例；与 C72 的 `object`/lambda 一样属未固化语法
- 最小修复：补 int128 边界与 `fn` 类型匹配/不匹配用例
- 备注：字面量 `(int128)18446744073709551616`（2^64）依赖 int128 字面量解析

### 第一百批：Test/SyntaxAudit/probes（前一半，按文件名字典序）

#### 覆盖范围

- 目录 `Test/SyntaxAudit/probes/` 共枚举到 **135** 个 `.krt` 文件（glob 结果按修改时间返回，已自行按文件名字典序重排）。
- 本批只读取字典序**前 68 个**，逐一全文读取；后 67 个（`m_two_b.krt` 起至 `u3.krt`）未读取。
- 另读取 `Test/SyntaxAudit/prelude.krt` 一次以理解约定：prelude 声明 `public static extern int64 syscall(...)`，并提供 `OutStrN/OutNL/OutBool/OutDigit/OutInt`（`OutInt` 处理负数与 10 位十进制），本身不含 `main`。
- 前 68 个文件清单（字典序）：
  `a1 a2 a3 a_t b1 b2 b3 b4 bad bsl bsl2 c1 c2 cc1 cc2 cc3 cc4 cc5 cmp_and cmp_involution cmp_pipe e1 e2 e3 e_A e_B e_C e_bs e_nl e_nul e_plain e_quote e_tab esc f1 f_assign f_decl f_inf fa fb fe_min fem formin g1 g2 g3 g4 g6 g_min gs_min hd4 hdr hdr2 hdr3 hdx hm_int323 hm_int327 hm_int643 k0 k1 l1 l2 lx m1 m2 m_nl2 m_three m_two`

##### 逐文件结论

| 文件 | 结论 |
|---|---|
| a1 | 正常：hex/bin/float 断言齐备 |
| a2 | 正常：`0b`/`0B` 前缀、浮点乘除断言齐备 |
| a3 | 低：与 a2 覆盖重复（`!=`、float 子集） |
| a_t | 正常：`D(8+7)`→`F`，断言明确 |
| b1 | 正常：`&&` 链断言 |
| b2 | 正常：六种比较断言 |
| b3 | 低：b2 子集 |
| b4 | 低：b1 子集 |
| bad | 中：负向探针混入未定义 `P`，诊断不唯一（见 100-11） |
| bsl | 正常：`(char)92` 断言 |
| bsl2 | 低：`'\\'` 可编译但无任何断言（见 100-18） |
| c1 | 低：b2 子集 |
| c2 | 低：与 b3 等价 |
| cc1 | 低：有确定性输出但无显式期望值（见 100-18） |
| cc2 | 低：同上 |
| cc3 | 低：与 cc2 对照但无断言标记（见 100-18） |
| cc4 | 低：同上 |
| cc5 | 低：变长输出，无断言（见 100-18） |
| cmp_and | 高：位运算结果无断言 + 命名不符（见 100-4） |
| cmp_involution | 高：同上（见 100-4） |
| cmp_pipe | 高：同上（见 100-4） |
| e1 | 正常：缺表达式负向探针 |
| e2 | 正常：未定义变量负向探针 |
| e3 | 中：与 e1 重复（见 100-12） |
| e_A | 正常：`12\|3==15`→`A`，else-if 链 |
| e_B | 低：与 e_A 输出等价（冗余） |
| e_C | 正常：`4^2==6`→`B` |
| e_bs | 高：长度边界错误（见 100-2） |
| e_nl | 正常：`"a\nb"` 3 字节参数正确 |
| e_nul | 正常：`"a\0b"` 3 字节参数正确 |
| e_plain | 正常 |
| e_quote | 正常：`"q\"q"` 3 字节参数正确 |
| e_tab | 正常 |
| esc | 高：缺 `syscall`/`P` + 长度错误 + 与 m_three 重复（见 100-3） |
| f1 | 低：命名与内容不符（见 100-14） |
| f_assign | 正常：for 初始化赋值 |
| f_decl | 中：与 fb/formin 三重重复（见 100-8） |
| f_inf | 致命：无限循环无终止（见 100-1） |
| fa | 正常：外部声明 + for 赋值 |
| fb | 中：与 f_decl 重复（见 100-8） |
| fe_min | 正常：int32 数组 + foreach |
| fem | 正常：int64 数组 + foreach |
| formin | 中：与 f_decl 重复（见 100-8） |
| g1 | 高：`Set/Get` 结果未校验且与 g2 重复（见 100-5） |
| g2 | 正常：`D(b.Get())` 断言 5 |
| g3 | 正常：泛型类 |
| g4 | 中：依赖 prelude 未提供的 `Console`（见 100-10） |
| g6 | 中：依赖 `Console`/`global::Sys`（见 100-10） |
| g_min | 正常：非 static 全局 |
| gs_min | 正常：static 全局 |
| hd4 | 中：与 hm_int643 重复 + 依赖内存布局（见 100-9/100-13） |
| hdr | 中：负索引读数组头，依赖内存布局（见 100-13） |
| hdr2 | 中：与 hm_int323 重复（见 100-9） |
| hdr3 | 中：依赖内存布局（见 100-13） |
| hdx | 中：与 hm_int327 重复（见 100-9） |
| hm_int323 | 中：与 hdr2 重复（见 100-9） |
| hm_int327 | 中：与 hdx 重复（见 100-9） |
| hm_int643 | 中：与 hd4 重复（见 100-9） |
| k0 | 中：外部运行时依赖 + 结果未断言（见 100-10） |
| k1 | 中：外部运行时依赖（`Memory.Copy`/`Console`）（见 100-10） |
| l1 | 中：缺 `P` 定义，掩盖 lambda 语法验证（见 100-6） |
| l2 | 中：缺 `P` 定义（见 100-6） |
| lx | 中：缺 `P` 定义，类型错误与未定义符号混杂（见 100-6） |
| m1 | 中：依赖 `Console`/`Sys`（见 100-10） |
| m2 | 中：依赖 `Console`/`Sys::`（见 100-10） |
| m_nl2 | 正常：5 字节参数正确 |
| m_three | 高：长度边界错误（见 100-2） |
| m_two | 正常：5 字节参数正确 |


##### [致命] 100-1｜f_inf.krt 无限循环无终止条件，运行审计会永久挂起
- 位置：`Test/SyntaxAudit/probes/f_inf.krt:6`
- 类别：静默通过/无断言 + 运行时死循环
- 触发条件：编译并运行该探针（`for (;;) { P("f", 1); }`）。
- 后果：循环无 `break`、无计数上限，`P` 无限输出；审计进程 hang，后续批次无法继续，且该探针没有任何期望值/退出码断言。
- 最小修复：加入计数上限并在达限时 `break`；或明确将其定为纯语法解析探针（不执行），并加运行超时保护。
- 备注：文件名 `f_inf` 似有意测无限循环，但缺少终止与超时保护，对批量审计是阻断级风险。

##### [高] 100-2｜字符串转义探针长度参数小于实际字节数，输出被截断
- 位置：`Test/SyntaxAudit/probes/e_bs.krt:5`（`P(s, 2)`，运行时 3 字节）；`Test/SyntaxAudit/probes/m_three.krt:5`（`P(s, 6)`，运行时 7 字节）；`Test/SyntaxAudit/probes/esc.krt:3`（`P(s, 6)`，运行时 7 字节）
- 类别：数值/字符串字面量边界错误
- 触发条件：运行探针。
- 后果：`"b\\s"` 实为 `b\s`（3 字节），`P(s,2)` 只输出 `b\`；`"a\tb\nc\"d"` 实为 7 字节，`P(s,6)` 丢掉末尾 `d`。若期望输出按完整串记录，测试必失败；若按现状记录，则把错误截断行为固化进基线。
- 最小修复：`e_bs.krt:5` 改为 `P(s, 3)`；`m_three.krt:5`、`esc.krt:3` 改为 `P(s, 7)`。
- 备注：`m_three.krt` 与 `esc.krt` 内容重复（见 100-3）。

##### [高] 100-3｜esc.krt 缺少 syscall/P 定义，无法编译且与 m_three 重复
- 位置：`Test/SyntaxAudit/probes/esc.krt:1-4`
- 类别：依赖缺失 + 重复探针
- 触发条件：编译该探针。
- 后果：文件未声明 `syscall`、未定义 `P`，`P(s, 6)` 解析失败；与 `m_three.krt` 的字符串与长度完全相同，属不完整重复，无法独立承担转义探针职责。
- 最小修复：补齐 `syscall`/`P` 定义（与 m_three 一致），或删除该文件、由 `m_three.krt` 承担覆盖。
- 备注：`esc.krt` 与 `bad.krt` 是同批中仅有的缺少 `syscall` 声明的正向探针之一。

##### [高] 100-4｜位运算探针只打印固定 "Y"，运算结果零断言，且命名不符
- 位置：`Test/SyntaxAudit/probes/cmp_and.krt:6-7`（`12 & 3`）；`Test/SyntaxAudit/probes/cmp_involution.krt:6-7`（`12 ^ 3`）；`Test/SyntaxAudit/probes/cmp_pipe.krt:6-7`（`12 | 3`）
- 类别：静默通过/无断言 + 命名与内容不符
- 触发条件：`&`/`^`/`|` 任一运算符实现被改错（例如 `&` 实现成 `|`）。
- 后果：`x` 被计算但从未使用，三个探针恒输出 `Y`，任何位运算实现都能通过，完全无法检测语义回归。
- 最小修复：对 `x` 断言（`12&3==0`、`12|3==15`、`12^3==15`），可参照 `e_A.krt:6-9`、`e_C.krt:6-9` 的写法。
- 备注：文件名前缀 `cmp_` 暗示比较运算，内容却是位运算；`cmp_involution` 用词与 XOR 语义不符。

##### [高] 100-5｜g1.krt 调用 Set/Get 但不校验返回值，与 g2 重复且无断言
- 位置：`Test/SyntaxAudit/probes/g1.krt:9-11`
- 类别：静默通过/无断言 + 重复探针
- 触发条件：`Box.Get()` 返回错误值、或 `Set` 未生效、或字段未初始化。
- 后果：`b.Set(5)` 后直接 `P("R", 1)`，`Get()` 从未被调用，类字段/方法语义错误无法发现；与 `g2.krt:9-11`（用 `D(b.Get())` 断言 5）重复且更弱。
- 最小修复：改为 `D(b.Get())` 并断言 5（同 g2），或删除 g1 仅保留 g2。
- 备注：`g2.krt` 是正确范例。

##### [高] 100-6｜l1/l2/lx 调用未定义的 P，正向探针无法编译，掩盖目标语法
- 位置：`Test/SyntaxAudit/probes/l1.krt:4`；`Test/SyntaxAudit/probes/l2.krt:4`；`Test/SyntaxAudit/probes/lx.krt:4`
- 类别：依赖缺失 + 静默失败
- 触发条件：编译上述探针。
- 后果：三个文件均未声明 `syscall`、未定义 `P`。`l1`（`var inc = function (int32 a) => a + 1;`）与 `l2`（`Apply(3, function (int32 a) => a + 1);`）本应验证 lambda 定义/传参，但错误被 `P` 未定义掩盖，无法区分"lambda 不支持"与"缺少输出函数"；`lx`（`int32 inc = function...`）的类型错误也与 `P` 未定义混杂，诊断不纯。
- 最小修复：三个文件均补齐 `syscall`/`P` 定义（与 a1 等同批探针一致）。
- 备注：`l1.krt:1` 的 `Apply` 在 `l1` 中未被调用，亦为冗余。

##### [高] 100-7｜自带 syscall/P 的探针与 prelude.krt 的约定冲突/脱节
- 位置：前 68 个文件中除 `bad/e1/e2/e3/esc/g4/g6/l1/l2/lx/m1/m2` 外的 56 个文件，均在第 1-2 行重复声明 `syscall` 并自定义 `P`（例：`Test/SyntaxAudit/probes/a1.krt:1-2`、`Test/SyntaxAudit/probes/m_two.krt:1-2`）
- 类别：探针期望值与 prelude 约定不一致
- 触发条件：按"prelude.krt + 探针"同一编译单元拼接编译。
- 后果：`syscall` 与 prelude 的声明重复；prelude 提供的 `OutStrN/OutBool/OutInt` 在前半 68 个探针中完全未被使用，`P` 与 `OutStrN` 功能重复。若为同文件拼接，重复声明可能直接导致编译失败；若探针独立编译，则 prelude 对前半无效，约定未落地。
- 最小修复：二选一——由 prelude 统一提供 `P`/输出函数、探针不再自带 `syscall`；或在审计文档中明确探针独立编译、prelude 不适用，并删除冗余声明。
- 备注：`Prelude` 的 `OutInt` 具备负数与位宽处理，而探针的 `P` 只能输出原始字节，能力不一致。

##### [中] 100-8｜f_decl/fb/formin 三个探针语义完全相同（for 初始化声明）
- 位置：`Test/SyntaxAudit/probes/f_decl.krt:6`；`Test/SyntaxAudit/probes/fb.krt:4`；`Test/SyntaxAudit/probes/formin.krt:5-8`
- 类别：重复探针
- 触发条件：—
- 后果：三者都是 `for (int32 i = 0; i < 3; i = i + 1) { P("f", 1); }`，仅花括号/空行排版不同，覆盖同一语法节点，维护成本三倍。
- 最小修复：保留其一，其余改为覆盖 for 的其他形态（如省略条件、省略步长、多层嵌套）。
- 备注：`f_assign.krt:6`（初始化赋值）与 `fa.krt:4-5`（外部声明）才是该族的有效变体。

##### [中] 100-9｜hdr 系列三对探针互为重复
- 位置：`Test/SyntaxAudit/probes/hdr2.krt:4-6` 与 `Test/SyntaxAudit/probes/hm_int323.krt:4-6`；`Test/SyntaxAudit/probes/hdx.krt:4-6` 与 `Test/SyntaxAudit/probes/hm_int327.krt:4-6`；`Test/SyntaxAudit/probes/hd4.krt:4-5` 与 `Test/SyntaxAudit/probes/hm_int643.krt:4-6`
- 类别：重复探针
- 触发条件：—
- 后果：`hdr2` 与 `hm_int323` 均为 `int32[3]`→`(int64[])`→`raw[-1]`（仅差一个显式 `(int64)` 转换）；`hdx` 与 `hm_int327` 均为 `int32[7]`→`raw[-1]`，逐字相同；`hd4` 与 `hm_int643` 均为 `int64[3]` 读 `[-1]`，后者仅多一次冗余 `(int64[])` 转换。
- 最小修复：每对保留一个；把腾出的名额用于覆盖其他头/边界（如 `int8[]`、`float64[]` 或越界诊断）。
- 备注：`hdr.krt:8` 用 `D((int32)raw[-1])` 断言 12，与上述 exit-code 型探针不同，不属重复。

##### [中] 100-10｜引用 prelude.krt 未提供的运行时符号（Console/Sys/Memory）
- 位置：`Test/SyntaxAudit/probes/g4.krt:1`（`Console.WriteLine`）；`Test/SyntaxAudit/probes/g6.krt:2`（`Console.WriteLine` + `global::Sys.InternalStringLength`）；`Test/SyntaxAudit/probes/k0.krt:4`（`Sys.InternalStringPtr`）；`Test/SyntaxAudit/probes/k1.krt:4-5`（`Sys.InternalStringPtr`、`Memory.Copy`、`Console.Write`）；`Test/SyntaxAudit/probes/m1.krt:2`（`Console.WriteLine` + `Sys.InternalStringLength`）；`Test/SyntaxAudit/probes/m2.krt:2`（`Console.WriteLine` + `Sys::InternalStringLength`）
- 类别：依赖外部状态/未声明运行时
- 触发条件：以 `prelude.krt` 作为唯一运行时/库编译探针。
- 后果：`Console`、`Sys`、`Memory` 在 prelude 与探针内均无声明，符号无法解析；这些探针要么编译失败，要么隐式依赖仓库外的运行时库，审计结果随外部库版本漂移。`k0` 还丢弃 `Sys.InternalStringPtr` 结果且无任何输出/断言；`k1` 依赖指针运算与 `Memory.Copy`，行为不可控。
- 最小修复：在 prelude 中提供这些符号的桩（声明 + syscall 实现），或改用 prelude 的 `OutStrN/OutInt` 与 `syscall` 重写。
- 备注：`m1` 与 `m2` 仅差 `.` 与 `::`，属有意的语法变体对，但都依赖外部 `Console`。

##### [中] 100-11｜bad.krt 在目标诊断之外引入未定义 P，负向探针诊断不唯一
- 位置：`Test/SyntaxAudit/probes/bad.krt:2-3`
- 类别：负向探针诊断噪声
- 触发条件：编译该探针。
- 后果：目标是未终止字符字面量 `'x`，但文件未声明 `syscall`、未定义 `P`，`P("Z",1)` 会额外产生未定义符号错误；若干诊断按集合/精确匹配，会失败或掩盖目标诊断。
- 最小修复：删除 `P("Z",1);`，或补齐 `syscall`/`P` 定义，使唯一诊断为字符字面量未终止。
- 备注：`e1/e2/e3` 未引入额外未定义符号，负向探针应保持同样的"单一错误"纪律。

##### [中] 100-12｜e1/e3 重复（e3 = e1 + return 0）
- 位置：`Test/SyntaxAudit/probes/e1.krt:1`；`Test/SyntaxAudit/probes/e3.krt:1`
- 类别：重复探针
- 触发条件：—
- 后果：两者都是 `int32 x = ;` 缺表达式语法错误，`e3` 仅多一句 `return 0;`，覆盖同一诊断。
- 最小修复：合并为一个探针，腾出名额覆盖其他缺表达式位置（如返回值、实参、下标）。
- 备注：`e2.krt:1`（未定义变量）是该族唯一有效补充。

##### [中] 100-13｜hdr 系列以负索引读取数组头，依赖未定义内存布局
- 位置：`Test/SyntaxAudit/probes/hdr.krt:8`；`Test/SyntaxAudit/probes/hdr2.krt:6`；`Test/SyntaxAudit/probes/hdr3.krt:6`；`Test/SyntaxAudit/probes/hdx.krt:6`；`Test/SyntaxAudit/probes/hm_int323.krt:6`；`Test/SyntaxAudit/probes/hm_int327.krt:6`；`Test/SyntaxAudit/probes/hm_int643.krt:6`；`Test/SyntaxAudit/probes/hd4.krt:5`
- 类别：依赖外部状态/未定义行为
- 触发条件：运行时/GC/数组头布局变化（如启用边界检查、压缩指针、对象头偏移调整）。
- 后果：`arr[-1]`、`raw[-1]`、`raw[0]` 依赖"长度头位于数据之前"的具体布局，属未定义行为；布局一变，探针结果（退出码/`D` 输出）随之改变，审计误报或漏报。`hdr3.krt:6` 用 `raw[0]` 读取首元素而非头部，语义与同族不一致，易被误读为同一断言。
- 最小修复：改用语言提供的长度/头信息 API；若必须测布局，固定并注释布局假设，并加编译期断言。
- 备注：若项目有意以这些探针锁定 ABI 布局，应在探针内写明假设，否则视为脆弱依赖。

##### [中] 100-14｜命名与内容不符
- 位置：`Test/SyntaxAudit/probes/f1.krt:4-5`（名为 f 族 for 循环，实为 float 乘法）；`Test/SyntaxAudit/probes/m1.krt:2`、`Test/SyntaxAudit/probes/m2.krt:2`（m 族为字符串转义，实为 `Console.WriteLine`）；`Test/SyntaxAudit/probes/cmp_and.krt:6`、`Test/SyntaxAudit/probes/cmp_pipe.krt:6`、`Test/SyntaxAudit/probes/cmp_involution.krt:6`（`cmp_` 前缀但为位运算，无比较）；`Test/SyntaxAudit/probes/e_A.krt:6`、`Test/SyntaxAudit/probes/e_B.krt:6`、`Test/SyntaxAudit/probes/e_C.krt:6`（e 族为错误/转义，实为 else-if 链）；`Test/SyntaxAudit/probes/hd4.krt:1-5`（名称无实义）
- 类别：命名与内容不符
- 触发条件：按文件名检索/维护时。
- 后果：`f1` 归入 for 族、`m1/m2` 归入转义族、`cmp_*` 归入比较族均会误导；`hd4` 无法从名称判断测什么，增加审计与回归定位成本。
- 最小修复：按内容改名（如 `bit_and`/`bit_or`/`bit_xor`、`float_mul`、`escape_multi`），或重排文件名前缀。
- 备注：`fe_min`/`fem` 缩写过度，亦建议补全为 `foreach_i32`/`foreach_i64`。

##### [中] 100-15｜覆盖缺口：全目录无 break 探针
- 位置：目录级（`Test/SyntaxAudit/probes/` 全量 135 个文件名；`continue` 有 `cc1-cc5`、`p08_continue`，无任何 `break` 命名）
- 类别：覆盖缺口
- 触发条件：`break` 实现出错（不跳出、只跳一层、在 `switch`/`for` 中行为不一致）。
- 后果：循环/开关的跳出语义无任何探针检测；`f_inf.krt` 恰因缺少 `break` 而 hang，更凸显该缺口。
- 最小修复：新增 `break.krt`（while/for 中 `break`，含嵌套循环只跳一层）。
- 备注：`p` 系列覆盖 `continue` 但未覆盖 `break`。

##### [低] 100-16｜覆盖缺口：全目录无 do-while 探针
- 位置：目录级（135 个文件名中无 `do`/`do_while` 命名）
- 类别：覆盖缺口
- 触发条件：`do-while` 的"至少执行一次"语义或条件位置实现出错。
- 后果：无探针检测。
- 最小修复：新增 `do_while.krt`，覆盖零次与多次执行。
- 备注：`while` 已由 `cc1-cc5`/`p02` 覆盖。

##### [低] 100-17｜子集/等价型冗余探针
- 位置：`Test/SyntaxAudit/probes/a3.krt:4-9`（⊂ a2）；`Test/SyntaxAudit/probes/b3.krt:4-6`（⊂ b2）；`Test/SyntaxAudit/probes/b4.krt:4-7`（⊂ b1）；`Test/SyntaxAudit/probes/c1.krt:4-5`（⊂ b2）；`Test/SyntaxAudit/probes/c2.krt:4-7`（≡ b3）；`Test/SyntaxAudit/probes/e_B.krt:6-9`（输出与 e_A 等价）
- 类别：重复探针（子集冗余）
- 触发条件：—
- 后果：覆盖同一语法节点/同一输出，稀释用例密度；`a3` 与 `a2` 的 `!=`、float 断言完全重叠。
- 最小修复：删除或改写为未覆盖变体（如 `a3` 改测 `0x` 边界、`b3/b4` 改测链式短路）。
- 备注：`c2.krt` 与 `b3.krt` 仅多一个中间变量 `q`，语义等价。

##### [低] 100-18｜无显式断言/期望值的探针
- 位置：`Test/SyntaxAudit/probes/bsl2.krt:5`（恒输出 `X`，不校验 `c`）；`Test/SyntaxAudit/probes/cc1.krt:8`、`Test/SyntaxAudit/probes/cc2.krt:8`、`Test/SyntaxAudit/probes/cc3.krt:7-8`、`Test/SyntaxAudit/probes/cc4.krt:5`、`Test/SyntaxAudit/probes/cc5.krt:8`（仅打印 `N`/`S`/变长串，无期望值标记）
- 类别：静默通过/无断言
- 触发条件：循环/continue 语义被改错但输出字符仍相似。
- 后果：这些探针的"期望输出"未在文件内固化，审计只能靠外部基线；`cc3.krt` 用 `if` 打印 `S` 与 `cc2` 的 `continue` 形成对照，但无断言标记，容易误判为通过。
- 最小修复：为每个探针补一个由被测值决定的标记（如用 `D()` 输出计数值或断言 `S` 出现次数）。
- 备注：与 100-4 同类，但严重度较低（输出部分依赖行为）。

### 第一百零一批：Test/SyntaxAudit/probes（后一半，按文件名字典序）

#### 覆盖范围

- 枚举：`glob Test/SyntaxAudit/probes/*.krt` 共 **135** 个文件。
- 排序：按文件名字典序（C locale，字节序，大写字母排在小写前；本目录大小写两种排法结果一致）。
- 抽取：第 **69–135** 个，共 **67** 个，逐个 `read` 全量：
  `m_two_b, mm, ne1, nf, ok1, ok2, or_t, p01_if_braceless, p02_while_braceless, p03_float, p04_bitwise, p05_compound, p06_ternary, p07_switch, p08_continue, p09_manyargs, p1, p10_streq, p11_struct, p12_negmul, p13_ternary_op, p14_global_class, p15_i64index, p16_shortcircuit, p17_i64arg, p18_boolarg, q1, s1, s2, s3, s4, sc_side, sc_t1, sf_t, sf_t2, st1, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, t16, t17, t18, t19, t20, t21, t_ASSIGN_AND, t_ASSIGN_SHIFT, t_DECL_SHIFT, t_MUL, u1, u2, u3, val, x_c2, xn`。
- 另按约定 `read prelude.krt` 一次（164 行）。prelude 仅提供 `syscall` extern 与 `OutStrN/OutNL/OutBool/OutDigit/OutInt`，**不提供 `Console`、`Sys`、`using`**。
- 未读取前 68 个文件，未通读其他目录源码。


##### [致命] 101-1｜x_c2 断言里残留 `%s` 模板占位符
- 位置：`Test/SyntaxAudit/probes/x_c2.krt:6`
- 类别：字面量/占位符未替换；期望值与实际不符
- 触发条件：编译或运行 x_c2.krt。
- 后果：`if (cs[1] == %s && cs[3] == 100)` 中 `%s` 不是合法表达式（`%` 被当作取模运算符），轻则编译失败，重则被预处理/文本替换成错误值后断言失真；`cs[3] == 100` 又用魔数代替 `'d'`，可读性差。该探针无法验证 `(char[])` 转换与字符比较。
- 最小修复：改为 `if (cs[1] == 'b' && cs[3] == 'd') { P("Y",1); } else { P("N",1); }`，并补 else 分支。
- 备注：与 xn.krt 同源（同为模板生成后未替换）。

##### [致命] 101-2｜xn 字符串字面量转义错误且残留两处 `%s`
- 位置：`Test/SyntaxAudit/probes/xn.krt:4`、`Test/SyntaxAudit/probes/xn.krt:6`
- 类别：字符串字面量边界错误；占位符未替换
- 触发条件：编译 xn.krt。
- 后果：第 4 行 `string s = "a\\tb\\nc\\"d";` 中反斜杠被双重转义，`\\"` 使字符串在 `c\\` 处提前闭合，后续 `d";` 变成未闭合/游离 token，语法树被破坏（对照 q1.krt:4 的正确写法 `"a\tb\nc\"d\e"`）；第 6 行 `cs[1] == %s && cs[3] == %s` 两处占位符同样非法。探针必然编译失败或断言无效。
- 最小修复：第 4 行改为 `string s = "a\tb\nc\"d";`，第 6 行改为 `if (cs[1] == '\t' && cs[3] == '\n')`。
- 备注：两个 `%s` 说明该文件由模板批量生成后未做替换校验。

##### [高] 101-3｜t_DECL_SHIFT 同作用域重复声明且无断言
- 位置：`Test/SyntaxAudit/probes/t_DECL_SHIFT.krt:5-7`
- 类别：命名与内容不符；静默通过/无断言
- 触发条件：编译/运行 t_DECL_SHIFT.krt。
- 后果：第 5、6 行连续 `int32 x = 0;` 与 `int32 x = 1 << 3;`，同名重复声明要么触发重定义诊断，要么静默覆盖，无法区分；第 7 行无条件 `P("Y",1)`，即使 `1 << 3` 结果错误也照样输出 Y，探针退化为“只测能否编译”。
- 最小修复：删除第 5 行重复声明；改为 `int32 x = 1 << 3; if (x == 8) P("Y",1); else P("N",1);`。
- 备注：与 t_ASSIGN_SHIFT 同为移位相关探针，但一个重复声明、一个普通赋值，覆盖重叠。

##### [高] 101-4｜t_ASSIGN_* / t_MUL 命名暗示赋值运算符，实为普通赋值且无断言
- 位置：`Test/SyntaxAudit/probes/t_ASSIGN_AND.krt:6-7`、`Test/SyntaxAudit/probes/t_ASSIGN_SHIFT.krt:6-7`、`Test/SyntaxAudit/probes/t_MUL.krt:6-7`
- 类别：命名与内容不符；静默通过/无断言
- 触发条件：编译/运行上述三个探针。
- 后果：文件名 `t_ASSIGN_AND`/`t_ASSIGN_SHIFT` 暗示复合赋值 `&=` / `<<=`，实际是 `x = 6 & 3;` / `x = 1 << 3;` 普通赋值，复合赋值语法完全未被覆盖；三例都以无条件 `P("Y",1)` 结束，不对 `x` 的取值做任何判断，恒真通过。
- 最小修复：若要测复合赋值则写 `x &= 3; if (x == 2) ...`、`x <<= 3; if (x == 8) ...`；若只测表达式则补 `if (x == 2/8/18) P("Y",1); else P("N",1);`。
- 备注：与 p04_bitwise.krt（有断言）覆盖重叠且更弱。

##### [高] 101-5｜ok2 / or_t / val 无任何输出与断言，静默通过
- 位置：`Test/SyntaxAudit/probes/ok2.krt:1-4`、`Test/SyntaxAudit/probes/or_t.krt:1`（空文件）、`Test/SyntaxAudit/probes/val.krt:1`（空文件）
- 类别：静默通过/无断言
- 触发条件：运行这三个探针。
- 后果：ok2 只有 `while (i<3) i=i+1;`，既不输出也不检查 `i`；or_t、val 为 0 字节空文件。三者无论编译/运行是否正确都没有可观测信号，失败与无法运行被误判为通过；or_t 名含 `or` 却毫无逻辑或运算内容。
- 最小修复：ok2 末尾 `if (i == 3) P("Y",1); else P("N",1);`；or_t 补 `(1==2)||(2==2)` 的 Y/N 断言；val 至少补一条最小 `main` 输出，否则删除。
- 备注：空文件可能是生成/拷贝事故，应直接删除而不是留在套件里。

##### [高] 101-6｜大量探针依赖 prelude 未提供的 `Console`
- 位置：`ok1.krt:1`；`t3.krt:1,3,5,6`；`t5.krt:2`；`t6.krt:2,4`；`t7.krt:5`；`t9.krt:2,4`；`t11.krt:7`；`t12.krt:13`；`t13.krt:22`；`t14.krt:100-103`；`t15.krt:40-43`；`t16.krt:37-40`；`t17.krt:25`；`t20.krt:2`；`t21.krt:2,3`
- 类别：探针期望值与 prelude 约定不一致；依赖外部状态
- 触发条件：按约定将探针与 `prelude.krt` 一起编译。
- 后果：prelude.krt 只定义 `syscall/OutStrN/OutNL/OutBool/OutDigit/OutInt`，没有 `Console`，也没有 `using`。上述 15 个文件引用 `Console.WriteLine/Write`，t3 还使用 `using System;`。除非另链接一个含 `Console` 的运行时，否则全部编译失败；即便链接，也与“配合 prelude.krt 使用”的约定不一致。
- 最小修复：统一改用 prelude 的 `OutStrN/OutInt/OutNL`（或把 `Console` 显式写入 prelude）；t3 去掉 `using System;`。
- 备注：这是后半部分最大面积的约定漂移，建议整批回归到 prelude 输出约定。

##### [高] 101-7｜Sys 被引用但文件内与 prelude 均未定义
- 位置：`t4.krt:3`；`t5.krt:2`；`t6.krt:2,4`；`t9.krt:2,4`；`t10.krt:3`；`t19.krt:3`；`t20.krt:2`；`t21.krt:2,3`
- 类别：依赖外部状态/未定义符号；期望值与 prelude 约定不一致
- 触发条件：单独编译上述探针（或仅与 prelude.krt 一起编译）。
- 后果：这 8 个文件调用 `Sys.InternalStringLength` / `Sys.InternalInt32ToString` 等，但自身没有定义 `Sys`，prelude.krt 也没有。t19 尤为明显：全文只有 `syscall` 声明和 `main`，却直接 `Sys.InternalInt32ToString(42)`。结果必然是未定义符号错误，或隐性依赖某个未声明的历史运行时。
- 最小修复：要么在这些文件内补 `Sys` 定义（可复用 t14/t15 的版本），要么把 `Sys` 并入 prelude.krt。
- 备注：t14/t15/t16/t18 自定义了 `Sys`，但并未被这些文件共享，说明缺少统一运行时文件。

##### [高] 101-8｜仅以进程退出码为断言，运行器不检查即静默
- 位置：`t4.krt:4`、`t8.krt:4-6`、`t10.krt:4`、`sf_t.krt:7`、`sf_t2.krt:12`
- 类别：依赖外部状态/断言方式不可靠
- 触发条件：运行器只收集 stdout、不收集退出码时。
- 后果：t4/t10 用 `syscall(60, L, ...)` 把字符串长度作为退出码；t8 用退出码 2/3/4 区分 mmap 返回值分支；sf_t/sf_t2 用退出码 5/6 表示静态字段读写结果。若运行器只看输出（这些文件 stdout 为空），所有分支都无信号，恒“通过”。
- 最小修复：改为输出 Y/N 或数字后再退出；至少在运行器中强制校验退出码。
- 备注：与 prelude 的 `OutInt` 输出约定冲突，退出码取值也被限制在 0–255。

##### [中] 101-9｜s2 与 s4 内容完全重复
- 位置：`Test/SyntaxAudit/probes/s2.krt:1-17`、`Test/SyntaxAudit/probes/s4.krt:1-17`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：两个文件逐行完全相同（D/E/F + struct Vec + main 三次调用），同一覆盖被计两次，维护时易漏改其一，掩盖真实覆盖率。
- 最小修复：删除其一（保留 s4），或让两者分别覆盖不同结构体特性（如 s2 只字段读、s4 字段写/嵌套）。
- 备注：s3.krt 又是 s2/s4 的子集（仅 D 与 F），三者叠加重复。

##### [中] 101-10｜u1 / u2 / u3 仅结构体声明顺序不同，覆盖重复
- 位置：`Test/SyntaxAudit/probes/u1.krt:1-17`、`Test/SyntaxAudit/probes/u2.krt:1-17`、`Test/SyntaxAudit/probes/u3.krt:1-17`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：三例同样实现 D/E/F + Vec + 相同 main，唯一差异是 `struct Vec` 相对函数的位置（u1 在 E/F 前，u2 在 D/E 与 F 之间，u3 在 F 后）。若语言允许任意位置声明，则后两例几乎不增加覆盖；若不允许，则只有一例能通过，另两例是噪声。
- 最小修复：保留一例即可；若要测声明顺序，应明确期望（有序/无序）并给出不同断言。
- 备注：与 s2/s4 的“声明顺序”主题重叠。

##### [中] 101-11｜t17 是 t13 的重复副本，仅多一个未使用函数
- 位置：`Test/SyntaxAudit/probes/t13.krt:1-23`、`Test/SyntaxAudit/probes/t17.krt:1-26`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：class T.Probe 与 main 主体逐行一致，t17 仅额外加入 `function zz_tail(int64 q)`（从未被调用，死代码）。同一 int32→字符串转换路径被覆盖两次，且死函数会额外掩盖“未使用函数”诊断。
- 最小修复：删除 t17，或让 t17 覆盖不同输入（如负数、0）并删除 zz_tail。
- 备注：t13 与 t17 都依赖未定义的 `Console`（见 101-6）。

##### [中] 101-12｜t14 / t15 / t16 / t18 的 Sys 类高度重复
- 位置：`t14.krt:4-97`、`t15.krt:4-38`、`t16.krt:4-35`、`t18.krt:4-97`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：四份 `Sys` 类都实现 `InternalStringLength/InternalStringPtr/InternalInt32ToString`；t14 与 t18 的类体完全相同（含常量表与 `Syscall/Exit`），t15 把 `const` 换成非 const `static` 字段，t16 直接内联 `syscall(9,...)`。差异仅在于声明形式，主体逻辑重复，维护时四处同步。
- 最小修复：抽成一份共享 `Sys` 运行时（或并入 prelude），各探针只保留差异点；至少合并 t14 与 t18。
- 备注：t18 还另行声明 `ksyscall` extern，与 prelude 的 `syscall` 并存，进一步放大冗余。

##### [中] 101-13｜p02_while_braceless 名不符实，且 braceless while 实际未覆盖
- 位置：`Test/SyntaxAudit/probes/p02_while_braceless.krt:6`
- 类别：命名与内容不符；覆盖缺口
- 触发条件：按文件名理解覆盖范围时。
- 后果：文件名声明“无花括号 while”，第 6 行却是 `while (i < 3) { P("w", 1); i = i + 1; }`，带花括号。真正无花括号的 while 语法在后半部分没有任何探针覆盖，名称却误导为已覆盖。
- 最小修复：改为 `while (i < 3) P("w",1), i = i + 1;` 之类的无花括号形式（或明确语言语法），或重命名为 `p02_while_braced` 并另补 braceless 用例。
- 备注：p01_if_braceless 的无花括号写法可参照。

##### [中] 101-14｜p06_ternary 名不符实，且与 p01 重复
- 位置：`Test/SyntaxAudit/probes/p06_ternary.krt:5-7`
- 类别：命名与内容不符；重复探针
- 触发条件：按文件名理解覆盖范围时。
- 后果：文件名声明三元运算符，正文只有 `if (x > 0) P("P",1); else P("N",1);`，没有任何 `?:`。三元运算实际由 p13_ternary_op.krt 覆盖，本例既名不符实又与 p01 的 if/else 覆盖重复。
- 最小修复：改为 `int32 x = 5; string s = x > 0 ? "P" : "N"; P(s,1);`（与 p13 二选一），或重命名并明确只测 if/else。
- 备注：与 p13 并存导致“ternary”关键词被两个文件分别冒名/正名。

##### [中] 101-15｜p16_shortcircuit 并未验证短路
- 位置：`Test/SyntaxAudit/probes/p16_shortcircuit.krt:7-8`
- 类别：命名与内容不符
- 触发条件：按文件名理解覆盖范围时。
- 后果：两行只判断 `&&`/`||` 的布尔结果（都必为真），没有引入副作用计数，无法区分“短路”与“两边都求值”。真正的短路语义由 sc_side.krt 通过静态字段计数覆盖，本例名称夸大。
- 最小修复：重命名为 `p16_boolop`，或改为复用 `G.count` 副作用的写法。
- 备注：与 sc_side 覆盖重叠。

##### [中] 101-16｜s1 / t11 / t12 断言过弱，字段写入与 mmap 内容不可见
- 位置：`Test/SyntaxAudit/probes/s1.krt:8-11`、`Test/SyntaxAudit/probes/t11.krt:5-7`、`Test/SyntaxAudit/probes/t12.krt:5-7`
- 类别：静默通过/无有效断言
- 触发条件：运行上述探针。
- 后果：s1 写入 `v.x=3; v.y=4` 后只打印固定 `"S"`，不比较字段值，结构体字段写错也照过；t11/t12 向 mmap 缓冲写入 `65/66` 后从不读取，只输出 `"ok\n"`，写入是否落到正确地址不可见。
- 最小修复：s1 增加 `if (v.x + v.y == 7) P("Y",1); else P("N",1);`；t11/t12 回读 `chars[0]/chars[1]` 并断言为 65/66。
- 备注：t13/t17 对同一 mmap 路径做了真实回读断言，可作为修复模板。

##### [中] 101-17｜q1 使用 `\e` 转义，语义依赖编译器实现
- 位置：`Test/SyntaxAudit/probes/q1.krt:4`、`Test/SyntaxAudit/probes/q1.krt:9`
- 类别：字符串字面量边界
- 触发条件：编译器未把 `\e` 定义为普通字符 `e` 时。
- 后果：`s = "a\tb\nc\"d\e"` 共 8 字符，第 9 行断言 `cs[7] == 'e'`。若按 C 惯例 `\e` 为 ESC(0x1B)，或编译器对未知转义报错/保留为 `\e` 两字符，则断言失败或编译失败；该探针的期望值建立在对转义扩展的隐含假设上。
- 最小修复：改用明确支持的转义（如 `\x1b` 或 `\033`）并在断言里写对应数值，或删去 `\e` 段。
- 备注：与 xn.krt 同属转义/字符数组转换主题，但 q1 至少字面量本身闭合正确。

##### [中] 101-18｜后半部分明显未覆盖的语法节点
- 位置：整个后半探针集（对照 prelude 与已读 67 文件）
- 类别：覆盖缺口
- 触发条件：评估套件完整性时。
- 后果：以下节点在后半部分未见任何用例：`for` 循环、`do-while`；右移位 `>>`、按位取反 `~`、`<=`、`>=`；`++`/`--`；除 `+=`（p05）外的复合赋值 `-= *= /= %= &= |= ^= <<= >>=`；`else if` 链；循环内 `break`（`break` 仅见于 p07 的 switch）；非 static 类实例化（`new class`）；`enum`、`namespace`、`interface`；`typeof`/`sizeof`；多维数组；字符串内插/常用方法；`const` 局部与 `static` 局部。已覆盖节点（if/else、while、continue、switch/default、struct、static class 字段、三元、`global::`、`using`、`!=`、int64 传参/下标、`new int32[n]`、`null`、`void`、`const` 类字段）说明这些缺口是遗漏而非语言不支持。
- 最小修复：为上述每类至少补一条带断言的探针，优先 `for`、`do-while`、`>>`、`<=`/`>=`、`++/--`、循环 `break`、复合赋值族。
- 备注：本结论仅基于后半 67 个文件；若前半已覆盖，请在合并时去重。

##### [低] 101-19｜t4/t10、t6/t9 仅作用域运算符写法不同
- 位置：`t4.krt:3` 与 `t10.krt:3`；`t6.krt:2,4` 与 `t9.krt:2,4`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：t4 用 `Sys::InternalStringLength`、t10 用 `Sys.InternalStringLength`，其余完全相同；t6/t9 同理。若 `::` 与 `.` 语义等价，则后一对是纯重复；若不等价，则应明确断言差异而不是两套输出格式。
- 最小修复：合并为参数化用例，或在两个文件中分别断言 `::` 与 `.` 的解析结果差异。
- 备注：两对文件本身又都落在 101-6/101-7 的未定义符号问题内。

## 附录 A　子代理审查结果（全部中断，无最终结论）


> **数据来源与限制（先读）**
> - 来源仅为 6 份可读转录 `subagents/rollout-2026-11-09T23-35-*.transcript.md`（用户/助手可见消息导出，**不含工具输出**）；按指示未读同名 `.jsonl`，未读项目源码。
> - 主会话转录（`审查编译器全量代码.transcript.md:6007-6051`）显示：6 个子任务全部因 **HTTP 402 Payment Required**（LongCat 上游 `Usage limit reached`）在 2026-11-09T16:59:43Z–17:01:57Z 之间报错终止，主会话只收到 6 条 `errored` 通知，**没有收到任何子代理结果**。
> - 因此 6 个子代理**均无最终结论、均无编号问题条目**（无 S 编号可保留）。下文如实标注，并只保留转录中明确写出的事实性发现／关键排除项（标注「转录:行」以便与源码行号区分）。
> - 本文件不涉及主会话自有批次（S1–S57，见 `audit_extract/chunk_*.md`）；G2（S28-1 作废）、G3（S38-1~S38-15）在这 6 份转录中无对应条目，无可应用对象。G1 已用于 Faraday 的过程结论。

---

### Feynman（IR CSE/内联）

转录：`rollout-2026-11-09T23-35-22-01a1c0f3-257d-7421-b851-f34ab8267d61.transcript.md`

#### 覆盖范围

- `Re.KrtC/src/Compiler/Middle/Ir/IrPureCse.inc`（467 行，重点 :300-467）
- `Re.KrtC/src/Compiler/Middle/Ir/IrRecursiveInline.inc`（588 行）
- `Re.KrtC/src/Compiler/Middle/Ir/IrInline.inc`（283 行）
- 重点：局部值编号正确性（`cse_hash_value`/`cse_equal_value` 是否覆盖所有 `KrtIRValueType`、IMM_F/INTEGER 位模式）、代数改写语义（有符号/无符号、溢出）、递归内联深度限制、内联后符号/作用域重命名、混合分配器、函数纯度分析调用图、`KRT_CSE_BLOCK_LIMIT` 超限处理。

#### 问题条目

- **无最终结论**：转录止于「我将基于交接总结继续…」（转录:91）与「I'll start by reading the three target files and confirming callers with grep.」（转录:97），报告从未产出。
- 无编号条目。

#### 过程发现（未成条，无编号/严重度）

- **[关键排除项] PHI 操作数存放方式已澄清**（转录:75）：PHI 操作数确认存放在 `inst->operands` 中（`Ir.c:846-848` 经 `ir_add_operand`），因此 `eliminate_unused_integers` **不会**误删仅被 PHI 引用的 temp。属排除项，非缺陷。
- **[未完成] 后端 ADD/MUL 溢出语义与 `KrtIRValueIsValid`**（转录:75）：中断前正在核查，无结论。
- 其余重点项（内联递归无界→栈溢出、`x+x→x*2` 语义保持、`KRT_CSE_BLOCK_LIMIT` 超限、混合分配器、内联后重命名）在转录中**未见任何明确判断**。

---

### Helmholz（KroCodegen.c）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-26ef-7aa3-80ce-ae8bbbf28da4.transcript.md`

#### 覆盖范围

- `Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c`（1622 行）全量。
- 重点：指令选择、栈帧布局、调用约定、寄存器分配接口、与 `KrtIRBasicBlock` 双表示的一致性、字段槽宽假设（S34-11）、调试 `fprintf` 残留、`:1296` 被注释掉的 `KrtIrSsaOptimize` 的影响、死代码、未检查前置条件、整数溢出、栈深度。

#### 问题条目

- **无最终结论**：转录止于「Now let me verify the remaining candidate findings with exact line numbers.」（转录:125），报告从未产出。
- 无编号条目。

#### 过程发现（未成条，无编号/严重度）

- **[关键排除项] 浮点比较掩码正确**（转录:75）：KroCodegen.c 的浮点比较掩码实际是正确的（`and al, dl`），此前疑点排除，非缺陷。
- **[未完成] 间接 load/store 与 16 字节路径**（转录:77）：正在核查 `emit_load_indirect`/`emit_store_indirect` 及 16 字节路径，无结论。
- **[未完成] 字符串转换 opcode**（转录:113）：中断前把「字符串转换 opcode」列为最高优先级待验证项，无结论。
- **[信息不明] 一条「关键可达性事实」已确认**（转录:119）：原文只说「Confirmed a critical reachability fact」，未写明是哪条事实，无法转述为条目；随后计划跑 Kro 后端端到端测试。
- 其余重点项（字段槽宽、fprintf、SsaOptimize 注释影响、死代码、整数溢出、栈深度）**未见任何明确判断**。

---

### James（Kro 整数/寄存器打包/尾调用/浮点转换）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-27a4-7931-b264-198d3b4238a1.transcript.md`

#### 覆盖范围

- `Re.KrtC/src/Compiler/Backend/Kro/KroInteger.inc`（898 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc`（608 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc`（364 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc`（264 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc`（182 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc`（123 行）
- 重点：`KroRegisterPacking.inc:265-289` 的 position 计算溢出/越界、寄存器打包边界、尾调用栈帧复用（双重释放/悬垂）、浮点转换精度、128 位整数运算、IR 操作数类型对应、未检查前置条件。

#### 问题条目

- **无最终结论**：转录止于「Let me read the remaining parts of `KroInteger.inc` and check key helper usages.」（转录:110），报告从未产出。
- 无编号条目。

#### 过程发现（未成条，无编号/严重度）

- **[关键排除项] 寄存器号越界疑点排除**（转录:62）：寄存器枚举 0–15，`next_write[16]` 与 `1u<<reg` 的越界疑点可排除。非缺陷。
- **[关键排除项] `integer_condition` 分支方向为误读**（转录:104）：`leaf_index ? condition : condition^1` 表示 leaf_index=0 时取 `condition^1`，是**正确**的；guard 与 backedge 均正确，`-0x10`/`-0x20` 只是 rel32/rel8 编码。此前「致命分支方向反转」线索不成立。
- **[未完成] `KroInteger.inc` 剩余部分与关键 helper 用法**（转录:110）：中断前正在读，无结论。
- 其余重点项（position 溢出/越界、尾调用栈帧复用、浮点转换精度、128 位整数）**未见任何明确判断**。

---

### Curie（Tokenizer/名称改写/枚举指针解析）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-2882-7703-8e9a-804aad726a7f.transcript.md`

#### 覆盖范围

- `Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c`（1232 行）
- `Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.c`（389 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/EnumParser.inc`（163 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/PointerTypes.inc`（152 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserBase.c`（141 行）
- 重点：mangling 冲突、tokenizer 状态机（转义/注释/字符串/数字/Unicode）、错误恢复、诊断位置、缓冲区边界、共享可变状态、指针算术溢出。

#### 问题条目

- **无最终结论**：转录仅两条进度消息（转录:53「先核验 harness 与关键发现再产出最终报告」、转录:59「核实文件现状与已有 harness，再补齐全量逐行审查并输出报告」），无任何发现内容，报告从未产出。
- 无编号条目，也无过程发现可保留。

---

### Faraday（AST/访问器/表达式高级解析）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-295c-78c1-b7d4-8ff67e5f8fb4.transcript.md`

#### 覆盖范围

- `Re.KrtC/src/Compiler/Frontend/Parser/Ast.c`（570 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/AstVisit.inc`（409 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（898 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c`（266 行）
- 重点：AST 生命周期/别名（节点共享/释放）、访问器正确性、表达式优先级与结合性、类型转换、错误恢复、诊断位置、未检查前置条件、递归无界。

#### 问题条目

- **无最终结论**：转录止于「我将继续完成审计工作。先读取关键支撑文件以确认前批发现，避免误报。」（转录:136），报告从未产出。
- 无编号条目。

#### 过程发现（未成条，无编号/严重度）

- **[过程结论，与 G1 一致] 分配器契约与任务给定的「全局事实」冲突，已按真实代码确认**（转录:88-89、118、124）：`KRT_MALLOC` 系列在 OOM 时**返回 NULL 且不 exit**，`KrtMemoryReportError` 是空操作 → 分配失败是**活路径且零诊断**。该子代理明确表示与给定全局事实冲突、将按真实代码分类。对应原判「死代码」的 `if (!ptr) return NULL;` 应改标为「静默失败/未检查前置条件（OOM NULL 解引用或静默 NULL 传播）」——即 **G1**。
- **[未完成] 未解析 token、死状态、IrGen 分支的交叉验证**（转录:130）：中断前正在进行，无结论。
- 其余重点项（AST 别名/生命周期、访问器、优先级与结合性、递归无界）**未见任何明确判断**。

---

### Pasteur（ParserStatement.c）

转录：`rollout-2026-11-09T23-35-24-01a1c0f3-2a43-7fd2-afa4-b91120ebcfcd.transcript.md`

#### 覆盖范围

- `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c`（1674 行）全量。
- 重点：语句解析（if/while/for/switch/try/catch/finally/throw/class/struct/enum/unsafe）、错误恢复、诊断位置、作用域处理、未检查前置条件、递归无界、与 `IrGen.c` 的契约（AST_PROPERTY_DECLARATION/AST_CONSTRUCTOR_DECLARATION/AST_DESTRUCTOR_DECLARATION/AST_ACCESS_MODIFIER 是否被 IrGen 处理）、死代码。

#### 问题条目

- **无最终结论**：转录仅两条进度消息（转录:38「接手继续完成审计，先确认目标文件的真实行号与关键代码」、转录:54「接手继续完成剩余验证项，然后输出完整报告」），无任何发现内容，报告从未产出。
- 无编号条目，也无过程发现可保留。

---

### 汇总

| 子代理 | 范围 | 最终结论 | 编号条目 | 过程发现 |
| --- | --- | --- | --- | --- |
| Feynman | IR CSE/递归内联/普通内联 | 无（402 中断） | 0 | 2（1 排除项 + 1 未完成） |
| Helmholz | KroCodegen.c | 无（402 中断） | 0 | 4（1 排除项 + 3 未完成/不明） |
| James | Kro 整数/寄存器打包/尾调用/浮点转换 | 无（402 中断） | 0 | 3（2 排除项 + 1 未完成） |
| Curie | Tokenizer/名称改写/枚举指针解析 | 无（402 中断） | 0 | 0 |
| Faraday | AST/访问器/表达式高级解析 | 无（402 中断） | 0 | 2（1 过程结论=G1 + 1 未完成） |
| Pasteur | ParserStatement.c | 无（402 中断） | 0 | 0 |

补充说明：

- 6 份转录都是「可见消息」级导出，工具输出与命令结果不在其中；子代理在断线前完成的逐行阅读与 grep 证据只存在于同名 `.jsonl`（本次按指示未读）。若需要更细的中间判断，只能从 jsonl 提取。
- 全局修订中 G1 已在 Faraday 条目上标注；G2、G3 在本批无对应编号条目。
