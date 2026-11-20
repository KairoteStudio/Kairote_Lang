# Chunk 08 抽取

## 全局修订（本范围新增/确认）

- **G1（前提修正，影响第 1–55 批）**：审计前期假设 `KRT_MALLOC`/`KRT_CALLOC`/`KRT_REALLOC`/`KRT_STRDUP` 失败即 `exit(1)`；实际 `Core/Memory/Allocator.c:159-211` 的 `KrtSafeMalloc` 失败返回 NULL（`size==0` 或 malloc 失败均返回 NULL），`KrtCommon.h:77-81` 宏无守卫，`KrtMemoryReportError`（`Allocator.c:425-435`）是空操作。因此所有分配失败路径都是活路径且零诊断：凡原文判为「死代码」的 `if (!ptr) return NULL;` 类条目，改标为「静默失败 / 未检查前置条件（OOM 时 NULL 解引用或静默 NULL 传播，零诊断）」，并按后果重定严重度（崩溃→致命/高）。抽样未检查调用点：`IrGen.c:743,981`、`ParserBase.c:49`、`SemanticAnalyzer.c:426,1396,1686,1751,1839,1847,1852,2019,2071,2677,2755`、`ProjectKrt.c:118,241,433,757,760`、`ConfigManager.c:88`、`ParallelCompiler.c:32`、`IrType.c:193`、`IrSsa.c:273,1460`、`Ir.c:1074`。本范围内受影响条目已加【修订 G1】。
- **G2**：S28-1 作废（原结论基于非法语法 `function int32 main()`，无效；正确结论见 S31-1：声明上下文正确，仅非声明上下文数组字面量步长错误）。
- **G3**：第三十八批修正：`KrtIrSsaOptimize` 只调用 `ssa_constant_propagation`/`ssa_copy_propagation`/`ssa_eliminate_dead_code`，从不调用 SSA 构造/`ReplaceVarUses`/`RemoveStoreInsts`/`LowerPhis`；S38-1~S38-15 全部位于零调用者死代码内，严重度降为「高/中（死代码内潜在）」；S38-11 的 prev 疑似项撤回。

## 覆盖范围

- 第 47 批（部分，S47-5～S47-38，`Re.KrtC/src/Main.c`）：原文行 5023–5256
- 第 48 批（`Compiler/Driver/ProjectKrt.c` 全量 953 行）：行 5271–5351
- 第 49 批（`Project.c` 全量 788 行 + `Project.h`）：行 5359–5411
- 第 50 批（`ParallelCompiler.c` 全量 552 行）：行 5419–5460
- 第 51 批（`CompilerPipeline.c` 全量 687 行）：行 5468–5512
- 第 52 批（`ConfigManager.c` 全量 228 行 + `ConfigManager.h`）：行 5520–5566
- 第 53 批（`Preprocessor.c` 全量 191 行）：行 5586–5625
- 第 54 批（`TaskManager.c` 162 行 + `ParallelCompilerLink.c` 91 行）：行 5629–5683
- 第 55 批（`Core/Utils` 补充：Integer.h、KrtCommon.h、Error.h/.c、Attributes.h、Path.h、OutputCache.c）：行 5705–5779
- 第 56 批（`Core/Utils/Path.c` 384 行 + `KrtString.h` 39 行）：行 5801–5884
- ⚠️ 前提修正（推翻前 55 批部分判定）：行 5902–5910
- 第 57 批（部分，`Core/Memory`：Allocator.c + Arena.c + SmartPtr.c，S57-1～S57-2）：行 5914–5928

## 问题条目

### [高] S47-5｜`get_kro_filename` 对长度 ≤4 的输入静默不替换扩展名
- 位置：`Re.KrtC/src/Main.c:551-559`（:556 `if (len > 4)` 才替换）
- 类别：静默失败 / 契约破坏
- 触发条件：输入文件名长度 ≤4（如 `x`，长度 1）。
- 后果：不替换扩展名，`kro_file` 与 `krt_file` 同名 → 后续 `KrtCompilePipelineExecute` 输出路径与输入相同，可能覆盖源文件。
- 最小修复：无条件替换或追加 `.kro`。
- 备注：—

### [高] S47-6｜`get_kro_filename` 用 `strcpy(kro_file + len - 4, ".kro")` 假设扩展名是 4 字符
- 位置：`Re.KrtC/src/Main.c:556-558`
- 类别：契约破坏 / 未检查前置条件
- 触发条件：输入扩展名不是 `.krt`（如 `.txt`）或文件名无扩展名但长度 >4（如 `abcd`）。
- 后果：`kro_file + len - 4` 指向错误位置，`strcpy` 可能越界写；`abcd` → `.kro`（丢失文件名）。
- 最小修复：用 `strrchr(kro_file, '.')` 找扩展名。
- 备注：—

### [高] S47-8｜`compile_single_file` 截断后 `snprintf` 到 `temp_kro[KRT_MAX_PATH]`，但 `KRT_MAX_PATH` 可能 < 1010
- 位置：`Re.KrtC/src/Main.c:435-443`
- 类别：未检查前置条件
- 触发条件：`KRT_MAX_PATH < 1010`。
- 后果：`snprintf` 截断，`temp_kro` 路径不完整。
- 最小修复：截断到 `KRT_MAX_PATH`。
- 备注：疑似，需验证 `KRT_MAX_PATH` 的值。

### [高] S47-9｜`KRT_TARGET_CMD_KRO` 且无 `-o` 时 :508-519 为死代码
- 位置：`Re.KrtC/src/Main.c:366-374` 与 `:508-519`
- 类别：语义分叉 / 死代码
- 触发条件：`KRT_TARGET_CMD_KRO` 且无 `-o`。
- 后果：:368 `default_output = "output.exe"` 非空 → :509 条件不成立 → :519 不执行，`default_exe_output` 覆盖逻辑死代码。
- 最小修复：删除 :508-519 或修正条件。
- 备注：—

### [高] S47-10｜`KRT_TARGET_CMD_KRO` 成功路径可能双重销毁 `pipeline->compiler`
- 位置：`Re.KrtC/src/Main.c:500-503` 与 `:544`
- 类别：资源与生命周期 / 契约破坏
- 触发条件：`KRT_TARGET_CMD_KRO` 成功。
- 后果：:502 `pipeline->compiler = NULL`，若 `KrtCompilePipelineDestroy` 不检查 NULL 则双重释放。
- 最小修复：确认 `KrtCompilePipelineDestroy` 检查 `compiler==NULL`，否则补检查。
- 备注：疑似，需验证 `KrtCompilePipelineDestroy`。

### [高] S47-11｜`KRT_TARGET_CMD_EXE` 成功路径同样的双重销毁问题
- 位置：`Re.KrtC/src/Main.c:462-465`
- 类别：同 S47-10（资源与生命周期 / 契约破坏）
- 触发条件：`KRT_TARGET_CMD_EXE` 成功。
- 后果：同 S47-10。
- 最小修复：同 S47-10。
- 备注：—

### [高] S47-12｜`#ifdef __linux__` 与非 Linux 分支产生不同中间文件扩展名
- 位置：`Re.KrtC/src/Main.c:427-444`
- 类别：配置漂移 / 语义分叉
- 触发条件：Linux vs 非 Linux。
- 后果：同一命令在不同平台产出不同中间文件（`.kro` vs `.eo`），链接路径行为不同。
- 最小修复：统一中间格式或文档化。
- 备注：—

### [高] S47-18｜`build_project` 的 `source_paths` 含 NULL 时 `qsort` 解引用 NULL
- 位置：`Re.KrtC/src/Main.c:1186-1216`（:1188-1190 `continue`）与 `:1217`（`qsort`）
- 类别：未检查前置条件
- 触发条件：`project->sources[i]` 为空字符串。
- 后果：`source_paths[i]` 保持 NULL，`qsort` 比较函数 `strcmp(*a, *b)` 对 NULL 解引用 → 崩溃。
- 最小修复：过滤空项或报错。
- 备注：—

### [高] S47-20｜`build_project_independent` 的 `max_threads=8` 硬编码
- 位置：`Re.KrtC/src/Main.c:932`
- 类别：配置漂移
- 触发条件：任何项目构建。
- 后果：线程数不可配。
- 最小修复：从 config 读取。
- 备注：—

### [高] S47-21｜`obj_dir_truncated[500]`/`file_name_truncated[500]` 截断后拼接路径可能不完整
- 位置：`Re.KrtC/src/Main.c:993-1000`
- 类别：未检查前置条件
- 触发条件：长路径。
- 后果：中间对象路径错误。
- 最小修复：检查截断。
- 备注：—

### [高] S47-23｜`aggregate_kro` 用相对路径导致临时文件泄漏
- 位置：`Re.KrtC/src/Main.c:1090` 与 `:1137`
- 类别：阶段不对齐 / 资源与生命周期
- 触发条件：`object_dir` 不是当前目录。
- 后果：:1137 `KrtDeleteFile(aggregate_kro)` 删除相对路径，但实际 `.kro` 生成在 `object_dir` 下 → 临时文件泄漏。
- 最小修复：用绝对路径或在 `object_dir` 下删除。
- 备注：—

### [高] S47-24｜`run_compiler` 的 `is_project` 只检查 `input_files[0]`
- 位置：`Re.KrtC/src/Main.c:1271-1284`
- 类别：契约破坏
- 触发条件：多文件时只检查第一个。
- 后果：`a.krt b.project.krt` 时 `is_project=0`，走多文件路径。
- 最小修复：文档化或检查所有文件。
- 备注：—

### [高] S47-25｜项目构建时 `final_output` 完全无效（死代码）
- 位置：`Re.KrtC/src/Main.c:1290-1301` 与 `:1219-1220`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：项目构建。
- 后果：`build_project` 的 `output_path` 参数被 `__attribute__((unused))` 忽略（:1148），实际用 `project->output_name` → `run_compiler` 计算的 `final_output` 对项目构建完全无效。
- 最小修复：删除 :1286-1301 的项目分支，或让 `build_project` 使用 `output_path`。
- 备注：—

### [高] S47-27｜`main` 调用 `KrtOutputCacheCleanup` 但 `KrtOutputCacheInit` 从未调用
- 位置：`Re.KrtC/src/Main.c:1360-1405`
- 类别：死代码 / 契约破坏
- 触发条件：任何执行。
- 后果：`g_output_cache` 静态零初始化 `enabled=0`，`Cleanup` 调 `Flush`（空链表）安全；`Init` 是死代码。
- 最小修复：调用 `Init` 或删除。
- 备注：与 S55-2 同源（OutputCache 整套未启用）。

### [高] S47-28｜`KrtConsoleSetColorEnabled(KrtConsoleSupportsColor())` 在 Windows 下有副作用
- 位置：`Re.KrtC/src/Main.c:1361`
- 类别：共享可变状态
- 触发条件：Windows。
- 后果：同 S46-19。
- 最小修复：同 S46-19。
- 备注：—

### [高] S47-31｜`build` 命令会覆盖此前已设置的 `input_file`
- 位置：`Re.KrtC/src/Main.c:170-188`
- 类别：语义分叉
- 触发条件：`build` 前有输入文件参数。
- 后果：`input_file` 被覆盖。
- 最小修复：文档化。
- 备注：—

### [高] S47-32｜`options.target_type_set` 赋值后从未使用
- 位置：`Re.KrtC/src/Main.c:198`
- 类别：死代码
- 触发条件：任何 `target` 命令。
- 后果：`target_type_set` 字段无人读取。
- 最小修复：删除。
- 备注：—

### [高] S47-33｜`options.output_file_set` 从未使用
- 位置：`Re.KrtC/src/Main.c:222`
- 类别：死代码
- 触发条件：任何 `-o`。
- 后果：无。
- 最小修复：删除。
- 备注：—

### [高] S47-34｜`print_usage` 的 `program_name` 参数被 `(void)` 忽略
- 位置：`Re.KrtC/src/Main.c:119-120`
- 类别：死代码 / 契约破坏
- 触发条件：任何帮助。
- 后果：用法信息不含程序名。
- 最小修复：使用 `program_name` 或删除参数。
- 备注：—

### [高] S47-35｜`project_name_from_type("dll")` 与 `create_project` 的 `ptype` 判断不一致
- 位置：`Re.KrtC/src/Main.c:48-65` 与 `:67-87`
- 类别：语义分叉
- 触发条件：`new dll name`。
- 后果：`project_name_from_type("dll")` 返回 `"console"`（默认），但 `ptype` 是 `LIBRARY`，名字与类型不一致。
- 最小修复：统一。
- 备注：—

### [高] S47-37｜`CLEAN`/`CHECK`/`VERSION` 目标不设 `target_type`，可能产生无意义编译
- 位置：`Re.KrtC/src/Main.c:401-406`
- 类别：死代码
- 触发条件：`clean`/`check`/`version` 目标。
- 后果：`config->target_type` 保持默认值；`VERSION` 在 `main` 提前返回，`CLEAN`/`CHECK` 走 `compile_single_file` → 可能产生无意义编译。
- 最小修复：补齐目标处理。
- 备注：疑似，需验证 `run_compiler` 对 `CLEAN`/`CHECK` 的处理。

### [高] S47-38｜`show_ir` 是否被 pipeline 使用未确认
- 位置：`Re.KrtC/src/Main.c:409`
- 类别：疑似（契约破坏）
- 触发条件：使用 `--show-ir`。
- 后果：`show_ir` 传入 `config->show_ir`，但 `KrtCompilePipelineExecute` 是否使用需验证。
- 最小修复：核实后在 pipeline 中生效或删除参数。
- 备注：疑似，需验证。

### [高] S48-1｜`KrtProjectKrtSave` 产物无法被 `KrtProjectKrtLoad` 解析
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:807-830`
- 类别：契约破坏 + 语义分叉
- 触发条件：调用 `KrtProjectKrtSave` 生成 project.krt 后再用 `KrtProjectKrtLoad` 读取。
- 后果：Save 写键值行（`project:`/`type:`/`output:`/`sources:`），Load（:690-748）要求 AST 中存在 `Configure` 函数并以 `Name(...)`/`Type(...)`/`Sources(...)` 调用求值（:490-580），语法完全不重叠 → `krt_proj_find_configure` 返回 NULL，报「未找到 Configure 函数」，无法往返编辑项目文件。
- 最小修复：Save 输出 `function Configure() { Name("..."); Type("..."); Sources("..."); }`，或让 Load 同时支持键值行解析。
- 备注：—

### [中] S48-2｜Save 无条件先写一行 `type: console`
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:808-820`
- 类别：注释与实现脱节 + 阶段不对齐
- 触发条件：非 console 项目（LIBRARY/WEB/SYSTEM）。
- 后果：写出两个 `type:` 行，第一个恒为 console；Load 若按行读取则取到错误类型。
- 最小修复：删除无条件 console 行，改为只在 `KRT_PROJ_TYPE_CONSOLE` 分支写。
- 备注：—

### [中] S48-3｜`parser_create` 失败分支未销毁 `lexer`
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:721-724`
- 类别：资源与生命周期
- 触发条件：`lexer_create` 成功后 `parser_create` 失败。
- 后果：只 `KRT_FREE(source)` + `KrtProjectKrtDestroy`，未 `lexer_destroy(lexer)` → Lexer 结构体、`source_owned` 副本、64KB arena 全部泄漏。
- 最小修复：在 `if (!parser)` 分支补 `lexer_destroy(lexer);`。
- 备注：—

### [中] S48-4｜项目文件 `fopen` 失败静默返回 NULL
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:685-687`
- 类别：诊断缺失（静默失败）
- 触发条件：项目文件缺失/权限不足。
- 后果：直接 `return NULL`，无 `KrtError`/`KRT_WARNING`，无路径与 errno → 上层只看到空指针，无法区分「文件不存在」与「解析失败」。
- 最小修复：失败时输出含 `path` 与 `strerror(errno)` 的诊断。
- 备注：—

### [中] S48-5｜`${unknown}` 仅告警后替换为空串继续
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:164-170`
- 类别：静默失败
- 触发条件：`Sources("${typo}/**/*.krt")` 等未知变量。
- 后果：静默展开为空 → 项目编译 0 个源文件却「成功」。
- 最小修复：展开期未知变量应视为错误，`KrtProjectKrtLoad` 返回 NULL。
- 备注：—

### [中] S48-6｜`trim("")` 构造数组起点之前的指针（UB）
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:18-19`
- 类别：未检查前置条件
- 触发条件：`project.krt` 中 `Sources("")` 或空 token。
- 后果：`e = s + strlen(s) - 1 == s - 1`，构造数组起点之前的指针（C 标准 UB）；`while (e > s)` 虽不解引用，但指针算术本身越界。
- 最小修复：`if (*s == '\0') return s;` 前置。
- 备注：—

### [中] S48-7｜条件表达式只支持 `==`/`!=` 与裸标识符，其余恒为假
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:455-465`
- 类别：静默失败 + 语义分叉
- 触发条件：`if (config == "release" && os == "linux")` 等含 `>`/`>=`/`&&`/`||` 的条件。
- 后果：一律落到 `return 0`（:465），配置块被静默跳过。
- 最小修复：对不支持的运算符返回「未知」而非 false，并给出诊断。
- 备注：—

### [中] S48-8｜未定义变量在条件比较中被当作空串
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:461`
- 类别：静默失败
- 触发条件：`${undefined} == ""`。
- 后果：`strcmp(lval ? lval : "", rval ? rval : "")` 使未定义变量判真。
- 最小修复：lval/rval 任一为 NULL 时判条件不成立并报错。
- 备注：—

### [中] S48-9｜`Type("bogus")` 返回值被丢弃，类型静默回退 console
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:526-529`
- 类别：静默失败
- 触发条件：非法项目类型。
- 后果：`parse_project_type` 返回 0，返回值被丢弃，`cfg->type` 保持旧值，用户得不到任何提示。
- 最小修复：检查返回值并 `KrtError`。
- 备注：—

### [中] S48-10｜`Sources` 与 `Libraries` 使用不同展开策略
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:554` vs `:572`
- 类别：语义分叉
- 触发条件：`Libraries("libs/*")`。
- 后果：`Sources` 走 `krt_proj_add_sources`（支持 `*`/`?` glob、递归展开目录），`Libraries` 走 `krt_proj_add_libraries`（仅 `krt_list_add`，字面量）→ glob 被当作字面文件名，链接期报「库不存在」。
- 最小修复：统一为同一展开函数，或在文档中明确区分并拒绝 glob。
- 备注：—

### [中] S48-11｜变量声明只处理字符串，数字/表达式/布尔展开为空
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:595-600`
- 类别：静默失败
- 触发条件：`var n = 3;` 后在 `${n}` 处展开。
- 后果：`AST_VARIABLE_DECLARATION` 只处理 `value->type == AST_STRING`，其余一律被设为 `""`。
- 最小修复：至少支持 `AST_INTEGER`/`AST_BOOLEAN`，其余报诊断。
- 备注：—

### [中] S48-12｜`opendir` 失败静默返回，项目 0 源文件却成功
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:311-313`
- 类别：静默失败
- 触发条件：`Sources("nonexistent/**/*.krt")`。
- 后果：静默产出 0 个源文件，项目「编译成功」但无输出。
- 最小修复：目录不存在时 `KrtError` 并报路径。
- 备注：—

### [中] S48-13｜发布路径无条件打印 `[KrtProj.debug]` 到 stderr
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:762-770`
- 类别：死代码（调试残留）
- 触发条件：任何发布路径执行。
- 后果：污染 CLI 输出、泄露项目结构，且无法关闭。
- 最小修复：删除，或置于 `KRT_DEBUG` 宏内。
- 备注：—

### [中] S48-14｜`krt_proj_eval_top_level_vars` 不递归，命名空间内变量展开为空
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:644-652`
- 类别：静默失败
- 触发条件：命名空间/块内声明 `var`。
- 后果：只扫描 `AST_PROGRAM` 顶层变量，嵌套 namespace/block 不递归 → 值静默为空。
- 最小修复：递归遍历或明确文档化仅支持顶层。
- 备注：—

### [高] S48-15｜`KRT_REALLOC` 失败分支实为活路径（原判死代码）
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:131-134`、`:228-230`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）
- 触发条件：OOM（`KRT_REALLOC` 失败返回 NULL）。
- 后果：`krt_proj_set_var`/`krt_list_add` 的失败分支不再不可达；realloc 失败 → NULL 静默传播/字段为 NULL，零诊断。
- 最小修复：调用点检查 NULL 并报诊断，或改为断言。
- 备注：【修订 G1】原结论「`KRT_REALLOC` 失败即 `exit(1)`，分支不可达」作废，重定为高。

### [轻微] S48-16｜`krt_proj_expand` 的 `close==NULL` 分支写入未做容量检查
- 位置：`Re.KrtC/src/Compiler/Driver/ProjectKrt.c:213-215`
- 类别：未检查前置条件
- 触发条件：`${` 后无 `}`（`close==NULL`）。
- 后果：`buf[len++] = *p++` 未做容量检查（仅 `copy_literal` 分支检查）；当前输入下不会越界，但依赖隐式不变量。
- 最小修复：在该分支复用 `copy_literal` 的容量检查。
- 备注：—

### [高] S49-1｜`KrtProjRemoveFile` 仅有声明、无实现
- 位置：`Re.KrtC/src/Compiler/Driver/Project.h:58`
- 类别：死代码 / 契约破坏
- 触发条件：调用 `KrtProjRemoveFile`。
- 后果：全项目无任何实现（`grep -rn` 仅命中头文件）→ 调用即链接失败；同时 `KrtProject.items` 链表只有头插（Project.c:455-457）无任何删除路径，项目模型无法移除文件。
- 最小修复：实现该函数（遍历链表、摘链、释放 `file_path`/`item_type`），或删除声明。
- 备注：—

### [中] S49-2｜`project_root` 存原始空串导致路径拼接错误
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:554-556`
- 类别：阶段不对齐
- 触发条件：`KrtProjCreateTemplate` 且 `output_dir == ""`。
- 后果：局部 `output_dir` 被改写为 `project->name`，但 `project->project_root` 存的是原始空串 → 后续 `KrtProjGetOutputPath`/依赖 `project_root` 的代码看到空根目录。
- 最小修复：先归一化 `output_dir`，再写 `project_root`。
- 备注：—

### [中] S49-3｜`KrtProjGetOutputPath`/`KrtProjGetIntermediatePath` 返回内部指针
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:758-781`
- 类别：共享可变状态 / 生命周期
- 触发条件：调用方 `KRT_FREE` 或修改返回值。
- 后果：与 `KrtProjDestroy` 的 `KRT_FREE(prop->output_path)`（Project.c:418）形成双重释放风险。
- 最小修复：返回 `KRT_STRDUP` 副本，或改为 `const char*` 并在头文件文档化「不可释放」。
- 备注：—

### [中] S49-4｜`copy_directory_recursive` 无深度限制、不检测符号链接环
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:91-190`
- 类别：未检查前置条件（递归无界）
- 触发条件：stdlib 目录存在符号链接环或层级过深（`KrtProjCreateTemplate` 用它复制整个 stdlib 树，Project.c:741）。
- 后果：无限递归/栈溢出。
- 最小修复：加 depth 上限（如 64）与 `lstat`+`S_ISLNK` 跳过符号链接。
- 备注：—

### [中] S49-5｜WEB/SYSTEM 项目输出类型被设为 `"exe"`
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:348`
- 类别：阶段不对齐
- 触发条件：`KRT_PROJ_TYPE_WEB`/`SYSTEM`。
- 后果：`KrtProjCreate` 仅按 `type == LIBRARY` 设 `"dll"`，否则一律 `"exe"` → 与实际后端不一致（配置漂移）。
- 最小修复：为 WEB/SYSTEM 分别设定输出类型，或删除该字段统一由后端决定。
- 备注：—

### [中] S49-6｜`readlink("/proc/self/exe")` 失败静默返回 0
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:306-309`
- 类别：诊断缺失
- 触发条件：非 Linux 容器、proc 未挂载等。
- 后果：调用方只报「无法找到捆绑标准库」，丢失真实原因。
- 最小修复：失败时输出 `errno` 诊断。
- 备注：—

### [中] S49-7｜生成 project.krt 时直接插值文件名，未走字符串字面量转义
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:714-718`
- 类别：静默失败
- 触发条件：文件名含 `"` 或 `\`。
- 后果：`fprintf(pk, "\"%s\"", tok)` 生成的 project.krt 语法被破坏，且无诊断。
- 最小修复：改用 `write_krt_string_literal`。
- 备注：—

### [中] S49-8｜源文件顺序在两条路径下相反
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:455-457` + `:491-520`
- 类别：语义分叉
- 触发条件：`KrtProjAddFile` 与 `KrtProjectKrtLoad` 两条路径。
- 后果：`KrtProjAddFile` 头插、`KrtProjGetSourceFiles` 按链表顺序输出 → 顺序与添加顺序相反；`Sources` 按书写顺序保留 → 同一项目编译顺序不同，链接符号解析/初始化顺序可能不同。
- 最小修复：`KrtProjAddFile` 尾插，或 `KrtProjGetSourceFiles` 反转。
- 备注：—

### [中] S49-9｜`localtime()` 非线程安全（疑似数据竞争）
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:356`、`:443`
- 类别：共享可变状态
- 触发条件：`ParallelCompiler` 并发调用 `KrtProjCreate`/`KrtProjAddFile`。
- 后果：`localtime()` 返回静态缓冲区指针，产生数据竞争。
- 最小修复：改用 `localtime_r`。
- 备注：疑似，需验证 `ParallelCompiler.c` 是否在同一进程内并发调用这两个函数。

### [中] S49-10｜`Configure(Project p)` 形参在求值期完全无意义（契约未文档化）
- 位置：`Re.KrtC/src/Compiler/Driver/Project.c:686` vs `ProjectKrt.c:690-748`
- 类别：契约破坏
- 触发条件：使用 `KrtProjCreateTemplate` 生成的模板。
- 后果：`krt_proj_find_configure` 只匹配函数名、不校验参数，`krt_proj_eval_block` 从未绑定 `p`；无参 `Configure()` 同样被接受。已实证当前模板可正确加载（`p.Name(...)` 被解析为 call name `Name`），但契约未文档化。
- 最小修复：在 `krt_proj_find_configure` 中断言参数列表为空或忽略，并在文档中固定模板形式。
- 备注：疑似（当前可用，契约缺陷）。

### [中] S50-1｜worker 丢弃 `config->imported_files` 可能泄漏/双重释放
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:100-104`
- 类别：资源与生命周期
- 触发条件：调用方在 `ParallelCompilerCreate` 前已向 `config->imported_files` 填入内容。
- 后果：worker 拷贝 config 后强制 `imported_files = NULL` 并清零计数，丢弃 `compiler->config` 已有数组指针；worker 只释放自己重建的数组（:120-123）→ 泄漏；原 `config` 仍持有该指针，可能与 worker 的释放形成双重释放。
- 最小修复：worker 内改用局部 `KrtConfig config = *compiler->config;` 后不清空，或明确文档化「config 的 imported_files 由 pipeline 接管」。
- 备注：疑似，需验证 `KrtCompilePipelineExecute` 是否写该字段、调用方是否预置。

### [中] S50-2｜`run_nasm` 失败不捕获 nasm 的 stderr/退出码
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:76-80`
- 类别：诊断缺失
- 触发条件：nasm 汇编失败。
- 后果：只报「汇编失败: %s -> %s」，无法定位是语法错误、宏未定义还是 nasm 缺失。
- 最小修复：在 error_msg 中附带 `WEXITSTATUS(status)`，并将 nasm stderr 重定向到临时文件后回显。
- 备注：—

### [中] S50-3｜`FindRuntimeObj` 未命中时返回未解析原始名
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:397`
- 类别：静默失败
- 触发条件：所有搜索路径都未命中。
- 后果：兜底 `KRT_STRCPY_S(result, size, obj_name)` 返回未解析的原始名 → 调用方据此链接，报错「找不到对象文件」而非「运行时库缺失」，诊断与实际原因不符。
- 最小修复：返回状态码或置 `result[0]='\0'` 并由调用方报错。
- 备注：与 S54-6 叠加。

### [中] S50-4｜泛型类型发现用纯文本 `strstr` 扫描，不区分注释/字符串
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:453-467`
- 类别：语义分叉 / 误报
- 触发条件：注释或字符串里出现 `template class Foo`。
- 后果：注册不存在的泛型类型；与 `Generics.c` 的真正词法/语义注册（`generics_register_type`）形成两套发现机制。
- 最小修复：复用词法分析器或至少跳过注释/字符串区间。
- 备注：—

### [中] S50-5｜`generics_register_type` 传 `params=NULL` 但 `param_count` 可能 >0
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:536-539`
- 类别：契约破坏
- 触发条件：`<...>` 逗号计数得出 `param_count > 0`。
- 后果：与 `Generics.c` 中 `param_count>0` 时访问 `params[i]` 的假设冲突（S43-10），实例化时越界。
- 最小修复：`params` 传 NULL 时强制 `param_count=0`，或在注册期校验。
- 备注：—

### [中] S50-6｜worker 数由源码字节数估算，与 `max_threads` 无关
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:275-287`
- 类别：配置漂移（未文档化）
- 触发条件：任何并行编译。
- 后果：worker 数由 `source_bytes / 64KB` 估算，小于 64KB 的项目永远单线程；同一项目在不同机器/源文件大小时行为（耗时、日志顺序、错误顺序）不可预测。
- 最小修复：`workers` 至少为 1 且由 `max_threads` 直接控制，或文档化阈值。
- 备注：—

### [高] S50-7｜`KRT_MALLOC`/`KRT_REALLOC` 失败分支实为活路径（原判死代码）
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:133-137`、`:190-193`、`:228-232`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）
- 触发条件：OOM（分配失败返回 NULL）。
- 后果：失败分支不再不可达；调用点若不检查即 NULL 解引用或静默 NULL 传播，零诊断。
- 最小修复：调用点检查 NULL 并报诊断，或改为断言。
- 备注：【修订 G1】原结论「失败即 `exit(1)`，分支不可达」作废，重定为高。

### [中] S50-8｜运行时对象搜索顺序与库搜索顺序是两套硬编码列表
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompiler.c:292-320` vs `ProjectKrt.c:896-953`
- 类别：语义分叉
- 触发条件：运行时/库路径维护时。
- 后果：`obj/runtime`、`obj/core/utils`… 与 `libs`、`libs/System`… 无共享常量，易漂移。
- 最小修复：提取共享的搜索路径表。
- 备注：—

### [高] S51-1｜错误报告永不显示行列（`error_line`/`error_column` 从未赋值）
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:557-585`
- 类别：诊断缺失 + 死代码
- 触发条件：任何编译错误。
- 后果：`KrtCompilePipelinePrintErrorReport` 的「错误位置」分支读 `pipeline->error_line`/`error_column`，但全代码库这两个字段从未被赋值（`grep -rn "error_line\s*="` 仅命中 `Tokenizer.c` 的 `lexer->error_line`）→ 报告永远只显示「失败阶段」，`^ 错误在这里` 永不出现。
- 最小修复：在 `KrtCompilePipelineLex`/`Parse`/`Semantic` 失败时从 `lexer->error_line`、`parser->error_line` 写入，或删除该分支。
- 备注：—

### [中] S51-2｜`lexer_create` 后检查 `lexer->error_count` 恒假
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:249-256`
- 类别：死代码（恒假条件）
- 触发条件：词法错误（未闭合字符串/字符）。
- 后果：`lexer_create`（Tokenizer.c:528-555）只初始化字段、不扫描，`error_count` 恒为 0 → 词法错误被放过，延后到 Parse 阶段才以「unterminated block comment or literal」报出，诊断与实际阶段不符。
- 最小修复：在 Lex 阶段触发一次完整扫描，或删除该恒假检查。
- 备注：—

### [中] S51-3｜`parser_create` 后检查 `lexer->error_count` 同样恒假
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:281-288`、`:291-298`
- 类别：死代码（恒假条件）
- 触发条件：解析前。
- 后果：`parser_create` 不扫描，`lexer->error_count` 仍恒为 0 → 两处检查均不可达。
- 最小修复：合并为 `parser_parse` 之后的一次检查。
- 备注：—

### [中] S51-4｜`require_entry_point` 由输出文件名后缀决定
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:345-349`
- 类别：契约破坏 / 配置漂移
- 触发条件：输出名不是 `.kro` 结尾（如 `foo.kro.bak`）。
- 后果：`ol >= 4 && strcmp(of + ol - 4, ".kro") == 0` 使改名即改变语义，且未文档化。
- 最小修复：改用 `pipeline->config->target_type == KRT_TARGET_KRO_OBJ` 显式判断。
- 备注：—

### [中] S51-5｜`pipeline->input_file`/`output_file` 直接保存调用方指针
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:449-450`
- 类别：契约破坏（悬垂指针）
- 触发条件：调用方传栈上字符串（如局部缓冲）。
- 后果：不 `KRT_STRDUP`，`pipeline` 持有悬垂指针；`record_stage_result` 的 `file_name` 同样保存该指针。
- 最小修复：Create/Execute 时 `KRT_STRDUP`，Reset 时释放。
- 备注：—

### [中] S51-6｜`record_stage_result` 在 `stage_count >= 8` 时静默丢弃
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:45-47`
- 类别：静默失败
- 触发条件：阶段数超过 8。
- 后果：直接 return，丢弃后续阶段结果，无诊断 → 阶段统计被静默截断。
- 最小修复：扩容或报错。
- 备注：—

### [中] S51-7｜`GetStageResults`/`GetImportedFiles` 返回内部数组指针
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:505-512`、`:681-686`
- 类别：共享可变状态
- 触发条件：调用方修改或释放返回值。
- 后果：内部数组可被外部修改/释放。
- 最小修复：返回 `const` 或副本。
- 备注：—

### [高] S51-8｜`config->target_type` 无 NULL 检查（G1 后确认为活路径）
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:383-389`
- 类别：未检查前置条件（原疑似）
- 触发条件：`KrtCompilePipelineCreate` 收到 NULL config（`KrtConfigCreate()` 失败返回 NULL）。
- 后果：`pipeline->config->target_type` 无 NULL 检查 → NULL 解引用崩溃，零诊断。
- 最小修复：Create 时断言 `config != NULL`，并检查 `Main.c` 三处 `KrtConfigCreate()` 返回值。
- 备注：【修订 G1】原备注「若内部用 `KRT_MALLOC` 则失败即 exit，不会返回 NULL」作废；G1 确认 `KRT_MALLOC` 失败返回 NULL，该路径为活路径，重定为高。

### [高] S51-9｜`KRT_MALLOC` 失败分支实为活路径（原判死代码）
- 位置：`Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c:63-65`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）
- 触发条件：OOM。
- 后果：失败分支不再不可达；不检查即 NULL 解引用或静默 NULL 传播，零诊断。
- 最小修复：调用点检查 NULL 并报诊断，或改为断言。
- 备注：【修订 G1】原结论「失败即 `exit(1)`，分支不可达」作废，重定为高。

### [高] S52-1｜`KrtConfigCreateProject` 零调用者且模板内容非法
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:162-218`
- 类别：死代码 + 语义分叉
- 触发条件：若被调用。
- 后果：生成 `.esproj` + `main.es` 模板，内容为 `function int main() { ... return 0; }`——该形式已被确认非法/被误解析（S30-1），正确形式为 `int32 main()` 或 `function main()`；与 `KrtProjCreateTemplate`（Project.c:539，生成 project.krt + main.krt）构成两套互不相通的项目模板系统，本路径产出的项目必然编译失败。
- 最小修复：删除 `KrtConfigCreateProject` 及 `.esproj` 相关代码，统一到 `KrtProjCreateTemplate`。
- 备注：—

### [高] S52-2｜Windows 对象/库数组初始化为 `{NULL}` 导致计数为 1
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:21-26`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：按 `object_count`/`library_count` 遍历 `linker_config->object_files[i]`。
- 后果：四个 Windows 数组初始化为 `{NULL}`，`sizeof(...)/sizeof(char*)` 计数为 1，唯一元素是 NULL → 解引用 NULL。当前 `linker_config` 只被读取 `entry_point`（ArkLinkIntegration.c:64-66），尚未触发，但契约是错的。
- 最小修复：改为空数组 `{0}` 且计数 0，或在读取处判 NULL。
- 备注：—

### [中] S52-3｜`KrtLinkerConfig` 六个字段全项目无读取
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.h:14-27` + `ConfigManager.c:27-34`
- 类别：死字段
- 触发条件：任何链接配置。
- 后果：`object_files`/`end_objects`/`library_paths`/`libraries`/`subsystem`/`dynamic_linker` 全项目无任何读取（`grep` 仅命中 `entry_point`）→ 链接配置形同虚设，Windows/Linux 行为无差异，维护者易误以为生效。
- 最小修复：删除未使用字段，或实现其语义。
- 备注：—

### [中] S52-4｜`KrtConfigGetDefaultOutput` 未覆盖 VM/KRO 且零调用者
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:129-142`
- 类别：阶段不对齐
- 触发条件：`KRT_TARGET_VM`、`KRT_TARGET_KRO`。
- 后果：switch 只覆盖 EXE/IR/ASM，VM/KRO 落 `default` 返回 `"output.asm"`，默认输出名与实际产物不符；该函数零调用者（死代码）。
- 最小修复：补齐枚举分支并加 `default` 断言。
- 备注：—

### [中] S52-5｜「是否需要链接」两处判断不一致
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:145-147`
- 类别：语义分叉
- 触发条件：KRO 目标。
- 后果：`KrtConfigNeedsLinking` 仅 `KRT_TARGET_EXE` 需要链接，而 `Main.c` 对 KRO 也走链接路径（`link_kro_executable`，Main.c:247）；该函数零调用者。
- 最小修复：统一为一个函数，或删除。
- 备注：—

### [中] S52-6｜`KrtConfigGetLinkerConfig` 的 `default` 静默按 Linux 处理
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:99-107`
- 类别：静默降级
- 触发条件：平台探测失败（`KRT_CONFIG_PLATFORM_UNKNOWN`）。
- 后果：`default` 分支返回 Linux 配置，链接参数错误且无诊断。
- 最小修复：`default` 返回 NULL 并由调用方报错。
- 备注：—

### [中] S52-7｜导入文件去重表有两套并行实现
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:67-93`
- 类别：语义分叉
- 触发条件：维护/排错时。
- 后果：与 `CompilerPipeline.c:658-686` 的 `KrtCompilePipelineAddImportedFile` 是两套实现：前者无 `INT_MAX/2` 与 `SIZE_MAX` 溢出检查，后者有；前者 `KRT_STRDUP` 失败返回 void，后者返回 false → 同一概念两套语义与错误处理。
- 最小修复：提取共享实现。
- 备注：—

### [中] S52-8｜`imported_file_capacity * 2` 可能整数溢出
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:67-70`
- 类别：未检查前置条件
- 触发条件：容量翻倍时溢出。
- 后果：无上限检查 → 整数溢出。
- 最小修复：加入与 Pipeline 版本一致的溢出检查。
- 备注：—

### [高] S52-9｜`KRT_MALLOC` 失败分支实为活路径；`keep_temp_files` 默认值与文档不符
- 位置：`Re.KrtC/src/Compiler/Driver/ConfigManager.c:42-44`、`:46-49`
- 类别：静默失败 / 未检查前置条件（原判「死代码」）+ 配置漂移
- 触发条件：OOM；以及任何使用临时文件清理的构建。
- 后果：失败分支不再不可达 → OOM 时 NULL 传播，零诊断；另 `keep_temp_files = 1` 默认保留临时文件，与 CLI 文档（`--keep-temp` 显式开启）不一致。
- 最小修复：调用点检查 NULL 并报诊断/断言；默认值与文档对齐。
- 备注：【修订 G1】原结论「失败即 `exit(1)`，分支不可达」作废，重定为高。

### [高] S53-1｜宏注册失败被忽略，错误推迟为「未定义符号」
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:69-78`、`CompilerPipeline.c:31-38`
- 类别：静默失败 + 诊断缺失
- 触发条件：宏名非法（空串、数字开头、含非标识符字符）。
- 后果：`PreprocessorAddMacro` 返回 `false`，但唯一调用方 `load_standard_macros` 三次调用全部忽略返回值，`PreprocessorProcess` 对未注册宏名静默原样透传 → 宏注册失败零诊断，错误推迟到词法/语义阶段表现为「未定义符号 `Console.WriteLine`」，诊断与实际错误不符。
- 最小修复：`load_standard_macros` 检查返回值并用 `KrtError` 报出宏名；或让 `PreprocessorAddMacro` 对非法名直接 `KRT_COMPILE_ERROR`。
- 备注：—

### [高] S53-2｜宏展开只做一轮，不递归
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:159-170`
- 类别：语义分叉
- 触发条件：宏 A 的替换文本中出现宏 B 的名字（含 `PreprocessorAddMacro(pp,"Console","X")` 后使用 `println`）。
- 后果：展开只做一轮，替换结果不再参与宏查找 → 与标准预处理器递归展开语义分叉；当前 3 个标准宏替换文本中的 `Console`/`WriteLine` 恰好不是宏，暂不可触发，但 API 是 public（`Preprocessor.h:24`）。
- 最小修复：在 `append` 后把替换文本重新入队扫描；或显式文档化「不递归展开」。
- 备注：—

### [高] S53-3｜`PreprocessorCreate` 无 NULL 检查，调用方 NULL 检查的契约不一致（G1）
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:17-19` vs `CompilerPipeline.c:205-207`
- 类别：静默失败 / 契约破坏（原判「死代码」）
- 触发条件：OOM（`KRT_CALLOC` 失败返回 NULL）。
- 后果：`PreprocessorCreate` 直接返回 `KRT_CALLOC` 结果、无 NULL 检查；调用方检查 `if (!preprocessor)` 不再是死代码，但失败时零诊断 → 静默 NULL 传播。
- 最小修复：统一为「分配失败即 `KRT_COMPILE_ERROR`」或统一返回 NULL 并由调用方处理；Create 内报诊断。
- 备注：【修订 G1】原结论「`KRT_CALLOC` 失败即 `exit(1)`，调用方检查为死代码」作废，重定为高。

### [高] S53-4｜宏表扩容/`KRT_REALLOC`/`KRT_STRDUP` 失败仅 `return false` 无诊断
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:91-94`、`:98-101`、`:106-109`
- 类别：静默失败
- 触发条件：OOM。
- 后果：失败时仅 `return false`，无诊断，调用方忽略 → 宏注册/复制被静默吞掉，编译继续。
- 最小修复：加 `KrtError("Preprocessor: macro table grow failed")` 等诊断。
- 备注：【修订 G1】G1 确认失败返回 NULL 而非 exit，上述路径为活路径，重定为高。

### [高] S53-5｜无宏时 `KRT_STRDUP(source)` 失败被误报为「预处理失败」
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:124-126` + `CompilerPipeline.c:216-227`
- 类别：静默失败 + 诊断与实际不符
- 触发条件：`macro_count == 0` 且 OOM。
- 后果：`KRT_STRDUP(source)` 失败返回 NULL，调用方报「预处理失败: <文件>」，丢失真实原因（内存分配失败）。
- 最小修复：`PreprocessorProcess` 用 out-param 区分「分配失败」与「无宏」，调用方在报文中带上阶段与原因。
- 备注：【修订 G1】G1 确认该路径为活路径，重定为高。

### [中] S53-6｜扩容条件恒真且 `bucket_count * 2` 极端可溢出
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:91`
- 类别：死代码 / 恒真条件
- 触发条件：`bucket_count == 0`（首次）。
- 后果：`(macro_count + 1) * 4 >= bucket_count * 3` 恒真 → 首次必扩容，逻辑结果正确但把「空表」与「负载」混在一条表达式里；`bucket_count` 为 `int`，`:51` 的 `bucket_count * 2` 极端情况下可溢出（不可达）。
- 最小修复：`if (bucket_count == 0 || (macro_count + 1) * 4 >= bucket_count * 3)`。
- 备注：—

### [中] S53-7｜`cursor[1]` 越界依赖隐式 NUL 结尾
- 位置：`Re.KrtC/src/Compiler/Driver/Preprocessor.c:139`
- 类别：未检查前置条件（轻微）
- 触发条件：`source` 非 NUL 结尾。
- 后果：`if (*cursor == '\\' && cursor[1])` 越界；实际由 `CompilerPipeline.c:191` 的 `source_code[read_bytes] = '\0'` 保证。
- 最小修复：`if (*cursor == '\\' && cursor[1] != '\0')`。
- 备注：—

### [高] S54-1｜FAILED 分支重复计数导致构建摘要自相矛盾
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:32-35` + `:100`
- 类别：诊断与实际不符
- 触发条件：任何任务失败。
- 后果：FAILED 分支同时执行 `stats->executed++` 和 `stats->failed++`，而 `KrtPrintBuildSummary` 的 `total_tasks = executed + up_to_date + skipped` 不含 failed → 例如「2 tasks: 2 executed, 1 failed」。
- 最小修复：FAILED 分支去掉 `executed++`，或 `total_tasks` 加上 `failed`。
- 备注：—

### [高] S54-2｜`KrtBuildSummaryAccumulate` 零调用者
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:85-97`
- 类别：死代码
- 触发条件：任何时刻。
- 后果：全代码库零调用者（仅 `TaskManager.h:28` 声明）。
- 最小修复：删除，或在 `Main.c` 中实际使用。
- 备注：—

### [中] S54-3｜`if (total_tasks > 0)` 恒真
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:119`
- 类别：死代码
- 触发条件：任何时刻。
- 后果：在 `:101-103` 提前 return 之后恒为真。
- 最小修复：删除冗余判断。
- 备注：—

### [中] S54-4｜`!stage || !stats` 静默丢弃整条报告
- 位置：`Re.KrtC/src/Compiler/Driver/TaskManager.c:12-14`
- 类别：静默失败
- 触发条件：参数为 NULL。
- 后果：静默丢弃报告，无诊断；当前 `Main.c` 所有调用点传 `KrtGetGlobalTaskStats()`（非 NULL），但契约上无保护。
- 最小修复：参数非法时 `KrtError`。
- 备注：—

### [中] S54-5｜对象文件添加失败不报文件名
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:29-39`
- 类别：静默失败 + 诊断缺失
- 触发条件：某任务 `obj_file`/`output_file` 不存在，或 ArkLink 拒绝该输入。
- 后果：`KrtArkLinkAddObjectFile` 返回非 0 时只不增加 `success_count`，不报出是哪个 .o 失败；`file_to_link == NULL` 时静默跳过 → 最终只报「No object files to link」，无法定位。
- 最小修复：失败时 `KrtError("Failed to add object file: %s", file_to_link)`。
- 备注：—

### [中] S54-6｜运行时 .o 添加全部忽略返回值
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:34` vs `:58-61`
- 类别：错误处理不一致
- 触发条件：运行时 .o 缺失或 ArkLink 拒绝。
- 后果：`:34` 检查 `KrtArkLinkAddObjectFile` 返回值，而 `:58-61` 对 `runtime.o`/`output_cache.o`/`allocator.o`/`KrtString.o` 的添加全部忽略返回值 → 静默缺少运行时对象，最后只报「ArkLink linking failed」，不报「找不到 runtime.o」（与 S50-3 叠加）。
- 最小修复：统一检查四个返回值并报出文件名。
- 备注：—

### [中] S54-7｜`KrtArkLinkLoadProjectLibraries` 失败无具体原因
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:64-67`
- 类别：诊断缺失
- 触发条件：库加载失败。
- 后果：直接 `return -1`，无具体原因；上层 `Main.c:1060` 只报「链接失败」→ 哪个库、什么错误全部丢失。
- 最小修复：此处 `KrtError` 报出 config 的库列表或 ArkLink 错误码。
- 备注：—

### [中] S54-8｜ArkLink 全部诊断被空操作 logger 丢弃
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:79-82` + `ArkLinkIntegration.c:24-28`
- 类别：诊断缺失（根因）
- 触发条件：链接失败。
- 后果：`link_result != 0` 只报「ArkLink linking failed」，未取内部错误；`ark_link_logger_callback` 是空操作（`(void)message;`）→ 所有 ArkLink 诊断被丢弃（即 S46-14 的具体后果）。
- 最小修复：实现 logger 回调输出到 stderr，或提供 `arklink_session_get_last_error` 并在链接失败时打印。
- 备注：—

### [中] S54-9｜对 KRO 字节码也执行 `chmod 0755`
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:84-87`
- 类别：语义不当
- 触发条件：`KRT_TARGET_KRO`。
- 后果：对字节码（非可执行）也 chmod。
- 最小修复：仅在 `target_type` 为 EXE/DLL 时 chmod。
- 备注：—

### [中] S54-10｜`success_count == 0` 三种原因同一报文
- 位置：`Re.KrtC/src/Compiler/Driver/ParallelCompilerLink.c:41-45`
- 类别：诊断与实际不符
- 触发条件：所有任务 `has_object_code` 为假 / 所有 `file_to_link` 为 NULL / 所有添加失败。
- 后果：统一报「No object files to link」，三种原因无法区分。
- 最小修复：分别计数并给出对应报文。
- 备注：—

### [致命] S55-1｜`Error.h` 的 include guard 撞名导致 `StackCalculator.h` 被跳过
- 位置：`Re.KrtC/src/Core/Utils/Error.h:1-2` + `StackCalculator.h:1-2` + `Core.h:13,15`
- 类别：契约破坏 / 静默失败
- 触发条件：任何经 `Core.h` 引用 `KrtStack*` 类型的代码。
- 后果：`Error.h` 的 guard 被写成 `#ifndef KRT_STACK_CALCULATOR_H`/`#define KRT_STACK_CALCULATOR_H`（复制粘贴自 StackCalculator.h）；`Core.h:13` 先包含 `Utils/Error.h`（占用 guard），`Core.h:15` 再包含 `Utils/StackCalculator.h` 时整个文件被跳过 → `KrtStackAnalyzer`/`KrtStackFrame` 类型与全部 API 声明在所有经 `Core.h` 的 TU 中不可见 → 编译期报错或（更糟）隐式声明；当前因 StackCalculator 整套零调用者而未暴露。
- 最小修复：把 `Error.h:1-2` 改为 `KRT_ERROR_H`；同时删除空的 Error.h/Error.c（见 S55-3）。
- 备注：—

### [高] S55-2｜整套 OutputCache 是死功能
- 位置：`Re.KrtC/src/Core/Utils/OutputCache.c:103-131`、`:199-201`、`KrtCommon.h:14-58`
- 类别：死代码 + 配置漂移
- 触发条件：任何时刻。
- 后果：`KrtOutputCacheSetEnabled` 全代码库零调用者 → `g_output_cache.enabled` 恒为 0（`KrtOutputCacheInit` 也零调用者，静态零初始化）→ `output_cache_add_internal` 永远走 `:87-101` 直接打印分支，`:103-131` 链表队列路径永不执行；`KrtError`/`KRT_WARNING` 的 cache 分支、`KrtOutputCacheCleanup`/`Init`/`Clear` 全是死代码。`KRT_COMPILE_ERROR` 不经过 cache 因此「碰巧正确」，属巧合而非设计。
- 最小修复：接入 `KrtOutputCacheSetEnabled` 并测试缓冲语义，或删除整条 cache 路径。
- 备注：与 S47-27 同源。

### [高] S55-3｜`Error.h`/`Error.c` 是空模块
- 位置：`Re.KrtC/src/Core/Utils/Error.h`（6 行，无任何声明）+ `Error.c`（6 行，只有 `#include`，无任何定义）
- 类别：死代码 / 注释与实现脱节
- 触发条件：任何时刻。
- 后果：空模块；`Error.c` 编译产出空目标文件；文件名 `Error` 暗示存在错误处理 API，实际不存在。
- 最小修复：删除两文件及 `Core.h:13` 的 include。
- 备注：与 S55-1 联动。

### [高] S55-4｜`KRT_STRNCPY` 在 `n == 0` 时缓冲区下溢写
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:113-117`
- 类别：未检查前置条件（潜在 UB）
- 触发条件：以 `n = 0` 调用 `KRT_STRNCPY`（当前 4 个调用点均传 `sizeof(...)` 或 `sizeof(...)-1`，未触发）。
- 后果：`n == 0` 时 `(dest)[(n) - 1] = '\0'` → **缓冲区下溢写**（UB / 内存破坏）；宏也不校验 `n <= sizeof(dest)`。
- 最小修复：`if ((n) > 0) { strncpy(...); (dest)[(n)-1] = '\0'; }`，并用 `sizeof(dest)` 做静态断言。
- 备注：—

### [高] S55-5｜两套并行 Path API，所有权模型相反
- 位置：`Re.KrtC/src/Core/Utils/Path.h:8-18` vs `KrtCommon.h:220-224`
- 类别：语义分叉
- 触发条件：开发者按名字猜测 API，选错一套。
- 后果：`Path.h` 声明 `KrtGetFilename`/`KrtJoinPath`（返回 `char*`，堆分配）；`KrtCommon.h` 声明 `KrtPathGetFilename`/`KrtPathJoin`（调用方缓冲）；两套名称近似、语义不同、内存所有权模型相反，且 `KrtPathExists`/`KrtTimeNowSeconds` 在两个头文件重复声明 → 所有权混淆，泄漏或双重释放。
- 最小修复：合并为唯一一套（建议保留 Path.h 的堆分配版），删除 `KrtCommon.h:220-224` 的重复声明。
- 备注：—

### [高] S55-6｜`Path.h` 六个函数只有声明、无定义
- 位置：`Re.KrtC/src/Core/Utils/Path.h:11-17`
- 类别：死代码 / 未实现声明
- 触发条件：调用 `KrtGetFilename`、`KrtJoinPath`、`KrtResolvePath`、`KrtGetExtension`、`KrtReadFile`、`KrtWriteFile` 中任一个。
- 后果：全代码库无任何定义（`KrtGetDirectory`/`KrtGetCurrentDirectory`/`KrtGetExecutableDirectory` 有定义）→ **链接错误**。
- 最小修复：实现或删除；若保留 Path.h 为唯一 API，则必须实现。
- 备注：—

### [高] S55-7｜`KrtCreateDirectory` 宏与函数同名冲突
- 位置：`Re.KrtC/src/Core/Utils/Path.h:9` + `Path.c:41` vs `PlatformCommon.h:148,164`
- 类别：死代码 / 宏与函数同名冲突
- 触发条件：任何同时包含 `Path.h` 与 `PlatformCommon.h` 的 TU。
- 后果：`PlatformCommon.h:148`（Windows）/`:164`（POSIX）`#define KrtCreateDirectory(path) CreateDirectory(path, NULL)` / `mkdir(path, 0755)`，使 `Path.h:9` 的声明被宏展开成 `int CreateDirectory(const char*, NULL);`（Win）或 `int mkdir(const char*, 0755);`（POSIX）→ 声明与定义不匹配。当前无 TU 同时包含两者（已核实），一旦新增就会编译失败或产生错误声明。
- 最小修复：把 `PlatformCommon.h` 的宏改名（如 `KrtPlatformCreateDirectory`），或删掉 `Path.c` 中的函数实现。
- 备注：—

### [高] S55-8｜`KrtMallocChecked` 等「checked」分配 API 只有声明、无定义
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:96-100`
- 类别：死代码 / 未实现声明
- 触发条件：调用 `KrtMallocChecked`/`KrtCallocChecked`/`KrtReallocChecked`/`KrtStrdupChecked`/`KrtFreeChecked`。
- 后果：全代码库无定义、无调用 → 调用即链接错误；暗示存在「返回 NULL 而非 exit」的分配 API，实际不存在 → 声明与实现脱节。
- 最小修复：删除声明。
- 备注：【修订 G1】G1 确认分配失败返回 NULL 而非 `exit(1)`，因此调用方必须自行检查返回值；该「checked」API 并不存在，不能作为兜底。

### [中] S55-9｜`KrtIntegerParse` 无 NULL 检查且格式支持不全
- 位置：`Re.KrtC/src/Core/Utils/Integer.h:29-31`
- 类别：未检查前置条件
- 触发条件：传 NULL → 崩溃；传 `" 123"`/`"-1"`/`"077"` → 静默返回 false。
- 后果：直接 `text[0]` 无 NULL 检查；不跳过前导空白、不支持 `+`/`-`、不支持八进制与前缀 `_`，可能不是调用方预期。
- 最小修复：加 `if (!text || !result) return false;`；文档化不支持的格式。
- 备注：—

### [中] S55-10｜`KrtIntegerMask` 在 `bits < 0` 时为 UB
- 位置：`Re.KrtC/src/Core/Utils/Integer.h:11-13`
- 类别：未检查前置条件
- 触发条件：`bits < 0`（`KrtIntegerNormalize` 已用 `bits <= 0` 挡住，但 `KrtIntegerMask` 是 public inline）。
- 后果：`bits < 0` 时执行 `1 << bits`（UB）；`bits > 128` 时 `(1 << bits)` 亦 UB（当前只挡了 `>=128`）。
- 最小修复：`if (bits <= 0) return 0; if (bits >= 128) return ~(KrtUInt128)0;`。
- 备注：—

### [中] S55-11｜`KrtIntegerFormat` 缓冲余量无静态断言
- 位置：`Re.KrtC/src/Core/Utils/Integer.h:63-81`
- 类别：契约破坏
- 触发条件：任何调用。
- 后果：`char digits[40]` 存放十进制位，UINT128 最大 39 位十进制数，加符号共 40 字符 + NUL = 41 ≤ 42，**当前安全**；但余量关系只靠注释说明，无静态断言。
- 最小修复：加 `_Static_assert` 或编译期检查。
- 备注：—

### [中] S55-12｜`KRT_STRLEN` 是裸宏，同层错误处理不一致
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:102`
- 类别：语义分叉
- 触发条件：传 NULL。
- 后果：`#define KRT_STRLEN(str) strlen(str)` 与直接用 `strlen` 无差别且无 NULL 检查；同文件 `KRT_STRCPY` 做了溢出检查、`KRT_STRNCPY` 做了截断，唯独它是裸宏。
- 最小修复：删除该宏，或加 `if (!(str)) return 0;`（需改成 inline 函数）。
- 备注：—

### [高] S56-1｜`KrtTimeNowSeconds` 两平台语义不同（墙钟 vs 单调钟）
- 位置：`Re.KrtC/src/Core/Utils/Path.c:20-34`
- 类别：阶段不对齐 / 配置漂移
- 触发条件：任何平台；调用方 `ParallelCompiler.c:59,85,265,298` 用它计算 duration。
- 后果：Windows 分支用 `GetSystemTimeAsFileTime`（墙钟），POSIX 分支用 `clock_gettime(CLOCK_MONOTONIC)`（单调钟）→ Windows 上 NTP 校时/闰秒可让 duration 为负或巨大，构建耗时统计失真；同一 API 两平台语义不同。
- 最小修复：Windows 改用 `QueryPerformanceCounter` 或 `GetTickCount64`。
- 备注：附带 `:31` `(void)ts;` 在 `:32` 使用 `ts` 之前是死语句。

### [高] S56-2｜`KrtPathGetFilename` 超长返回 -1 无诊断，调用点忽略导致未初始化内存
- 位置：`Re.KrtC/src/Core/Utils/Path.c:53-82` + `Main.c:433,448,609,774,987,1292,1306,1323`
- 类别：静默失败 + 未检查前置条件（使用未初始化内存）
- 触发条件：文件名（含路径）长度 ≥ `KRT_MAX_PATH`(1024)。
- 后果：`KrtPathGetFilename` 在 `len >= size` 时 `return -1`（无诊断），Main.c 全部 8 个调用点忽略返回值；`char file_base[KRT_MAX_PATH];` 未初始化，失败后内容未定义，紧接着 `KrtPathRemoveExtension(file_base, ...)`（`Path.c:193-207`）读未初始化栈 → UB / 垃圾输出文件名，可能覆盖错误文件（与 S47-5 叠加）。
- 最小修复：调用点检查返回值并 `KrtError`；或 `KrtPathGetFilename` 内部对超限 `KRT_COMPILE_ERROR`。
- 备注：—

### [高] S56-3｜`last_sep` 指针比较 UB（必然触发）
- 位置：`Re.KrtC/src/Core/Utils/Path.c:185`
- 类别：未检查前置条件（指针比较 UB）
- 触发条件：路径只含一种分隔符（Windows 含 `\`、POSIX 含 `/`，必然触发）。
- 后果：`const char* last_sep = (slash > backslash) ? slash : backslash;` —— 当 `slash` 与 `backslash` 恰有一个为 NULL 时，用 `>` 比较 NULL 与有效指针是 C 标准未定义行为；编译器可假设两指针同对象而优化掉比较 → `last_sep` 取值错误 → `KrtPathRemoveExtension` 对 `a.b/c`（应保留）或 `a/b.c`（应去扩展名）判断错误。
- 最小修复：`const char* last_sep = (slash && backslash) ? (slash > backslash ? slash : backslash) : (slash ? slash : backslash);`
- 备注：—

### [高] S56-4｜`KRT_STRCPY_S` 超长静默截断
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:8-16`
- 类别：静默失败（截断）
- 触发条件：`src` 比 `dest` 长。
- 后果：`copy_len = (src_len < dest_size-1) ? src_len : dest_size-1;` 超长时静默截断，无诊断、无返回值指示。调用点 `Path.c:80,207,231,275`、`ParallelCompiler.c:323,340,358`、`LeakDetector.c:192` → 路径/对象名被静默截断，链接错文件、找不到运行时对象（与 S50-3/S54-6 叠加）。
- 最小修复：加返回值或让调用方在调用前检查长度（当前仅 `KRT_STRCPY` 做了）。
- 备注：—

### [高] S56-5｜`KRT_STRCAT_S` 整数下溢导致 `memcpy` 越界写
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:18-30`
- 类别：静默失败 + 未检查前置条件（整数下溢）
- 触发条件：`dest` 不是以 NUL 结尾，或 `dest_len >= dest_size`。
- 后果：`size_t remaining = dest_size - dest_len - 1;` 下溢为巨大值 → `copy_len = src_len` → `memcpy` 越界写 → 内存破坏。
- 最小修复：`if (dest_len >= dest_size) break/return;` 先做前置断言。
- 备注：—

### [中] S56-6｜`KrtPathExists` 无 NULL 检查
- 位置：`Re.KrtC/src/Core/Utils/Path.c:36-39`
- 类别：未检查前置条件
- 触发条件：传 NULL。
- 后果：`stat(path, &st)` 无 NULL 检查 → 崩溃。调用点 `Main.c:179` 传字面量、`Path.c:303,322,356,375` 传内部缓冲（非 NULL），但 API 是 public（`Path.h:8`、`KrtCommon.h:220`）。
- 最小修复：`if (!path) return 0;`。
- 备注：—

### [中] S56-7｜`KrtGetExecutableDirectory` 路径超长静默截断
- 位置：`Re.KrtC/src/Core/Utils/Path.c:114-161`
- 类别：静默失败 + 未检查前置条件
- 触发条件：安装路径深度大（超 `MAX_PATH` / 1023）。
- 后果：Windows `GetModuleFileNameA(NULL, path, MAX_PATH)` 超长时静默截断，`len` 赋值后从未使用；POSIX `readlink("/proc/self/exe", path, 1023)` 超长时静默截断；若截断后串中无 `/`，`last_slash == NULL` → 不裁剪 → 把完整（截断的）可执行文件路径当作目录返回。`FindRuntimeObj`（`ParallelCompiler.c:367`、`ArkLinkIntegration.c:264`、`ProjectKrt.c:940`）找不到运行时 .o → 链接缺运行时对象（与 S50-3/S54-6 叠加）。
- 最小修复：检查 `len == MAX_PATH-1` / `readlink` 返回值是否等于缓冲上限，失败返回 -1 并报诊断。
- 备注：—

### [中] S56-8｜`KrtPathJoin` 在 Windows 硬编码 `"/"`
- 位置：`Re.KrtC/src/Core/Utils/Path.c:233-235`
- 类别：平台语义分叉
- 触发条件：Windows 平台调用 `KrtPathJoin`。
- 后果：硬编码 `KRT_STRCAT_S(result, size, "/")`，Windows 下应使用 `KRT_PATH_SEPARATOR`（`'\\'`）→ 产出混合分隔符路径，部分 API 不接受。
- 最小修复：`#ifdef _WIN32` 选分隔符，或统一用 `KRT_PATH_SEPARATOR`。
- 备注：—

### [中] S56-9｜`KrtEnsureDirectoryRecursive` 原地篡改调用方路径
- 位置：`Re.KrtC/src/Core/Utils/Path.c:314,367`
- 类别：静默失败 + 语义分叉
- 触发条件：路径含 `\` 分隔符（Windows）。
- 后果：`*slash = '\0'; ... *slash = '/';` 无论原分隔符是 `/` 还是 `\`，恢复时一律写 `'/'`，原地修改调用方路径；Windows 下 `C:\a\b` 在循环中变成 `C:\a/b`，后续 `KrtPathExists(temp)` 用的是被篡改的串。
- 最小修复：保存原字符 `char saved = *slash; ... *slash = saved;`。
- 备注：—

### [中] S56-10｜`KrtEnsureDirectoryRecursive` 路径超长静默失败
- 位置：`Re.KrtC/src/Core/Utils/Path.c:266,271-273`
- 类别：静默失败
- 触发条件：路径 ≥ 1024。
- 后果：`char temp[1024]` 固定栈缓冲，`if (len >= sizeof(temp)) return -1;` 无诊断 → 目录创建静默失败，后续编译因缺目录报错，原因丢失。
- 最小修复：`KrtError("Path too long: %s", path)` 或动态分配。
- 备注：—

### [中] S56-11｜`result[len] = '\0';` 连续写两遍
- 位置：`Re.KrtC/src/Core/Utils/Path.c:196-197`
- 类别：死代码 / 冗余
- 触发条件：任何调用。
- 后果：冗余写入。
- 最小修复：删一行。
- 备注：—

### [中] S56-12｜`KRT_SPRINTF_S` 用 `__VA_ARGS__` 而非 `##__VA_ARGS__`
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:32`
- 类别：契约破坏 / 潜在编译错误
- 触发条件：格式串无额外参数时调用。
- 后果：`#define KRT_SPRINTF_S(buffer, buffer_size, format, ...) snprintf(buffer, buffer_size, format, __VA_ARGS__)` 用 `__VA_ARGS__` 而非 `##__VA_ARGS__` → 无额外参数时调用会语法错误；且忽略 `snprintf` 返回值（截断无指示）。
- 最小修复：改 `##__VA_ARGS__`，或改 inline 函数并返回截断标志。
- 备注：—

### [中] S56-13｜`KRT_STRCAT_S` 在缓冲已满时静默无操作
- 位置：`Re.KrtC/src/Core/Utils/KrtString.h:18-30`
- 类别：静默失败
- 触发条件：`dest_len == dest_size - 1`（缓冲已满）。
- 后果：`remaining == 0` → 整个宏静默无操作，调用方以为拼接成功。
- 最小修复：返回剩余空间或截断标志。
- 备注：—

### [高] S57-1｜裸 `malloc`/`strdup`/`realloc` 与 `KRT_FREE`/`KrtSafeFree` 混用风险
- 位置：`Re.KrtC/src/Compiler/Frontend/CompilerError.c:54,59,61,79,82,84,93,98,147`
- 类别：资源与生命周期 + 契约破坏（两套分配器混用）
- 触发条件：`KrtErrorReportCreate` 用裸 `malloc` 分配 `report`（:54）与 `report->errors`（:59），`KrtErrorReportSetSourceCode` 用裸 `strdup`（:98），`KrtErrorReportAddEx` 用裸 `realloc`（:147）；若这些指针被交给 `KRT_FREE`/`KrtSafeFree`，或反向把 `KRT_MALLOC` 的指针交给裸 `free`。
- 后果：`KrtSafeFree`（`Allocator.c:276-287`）查不到索引 → `KrtMemoryReportError` 空操作 → 静默 no-op → 泄漏；反向时 `user_ptr = actual_ptr + KRT_CANARY_PREFIX`（:186），裸 `free` 释放带前缀的中间指针 → glibc abort / 堆破坏。
- 最小修复：统一到 `KRT_MALLOC`/`KRT_FREE`；或在 `KrtErrorReport` 加 `allocator` 标记，`KrtSafeFree` 对未跟踪指针 `KRT_COMPILE_ERROR`。
- 备注：原列「致命」，现状核实已 grep 确认 `CompilerError.c` 内无 `KRT_FREE`、`KroWriter.c`/`Main.c` 内无 `KRT_FREE` → 当前无实际交叉释放，属潜伏地雷，故定级 **高**（非致命）。

### [致命] S57-2｜分配失败系统性静默（零诊断）
- 位置：`Re.KrtC/src/Core/Memory/Allocator.c:159-211,213-227,229-261,263-274` + `KrtCommon.h:77-81`
- 类别：静默失败（系统性，零诊断）
- 触发条件：OOM；`size == 0` / `count == 0`（:160-162、:214-216）；`KrtSafeRealloc` 传入未跟踪指针（:239-244）。
- 后果：返回 NULL 而非终止，且诊断函数为空操作 → 调用点若不检查即 NULL 解引用（崩溃），或产生静默 NULL 字段向下传播。
- 最小修复：`KrtSafeMalloc` 失败时改用 `KrtError_LOC(file,line,...)` 并 `KRT_COMPILE_ERROR`；或提供 `KRT_MALLOC_OR_DIE` 宏并强制调用点检查。
- 备注：即 G1 前提修正对应的条目。
