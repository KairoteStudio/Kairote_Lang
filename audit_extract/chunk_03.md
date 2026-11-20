# Chunk 03 抽取

## 覆盖范围
- 第六批（续）高/中：`PlatformAbstraction.c`、`CompilerError.c`、`Path.c`、`SmartPtr.c`、`ArkLinkIntegration.c`、`KroWriter.c`（原文 1230–1388 行）
- 第七批：`SemanticAnalyzer.c:1-440`
- 第八批：`SemanticAnalyzer.c:440-700`
- 第九批：`SemanticAnalyzer.c:700-900`
- 第十批：`SemanticAnalyzer.c:900-1120` + `ParserExpression.c` 交叉验证（含其后复核更正）
- 第十一批：`SemanticAnalyzer.c:1120-1200,2590-2620` + `ParserStatement.c:540-580`（含复核更正）
- 第十二批：`SymbolTable.c` 全量 + `SemanticAnalyzer.c:2440-2480`
- 第十三批：`SymbolTable.c` 调用面统计 + `SemanticAnalyzer.c:3743-3924`
- 第十四批：`SemanticAnalyzer.c:1400-1620` + `PointerAnalysis.inc` 全量 545 行
- 第十五批：`SemanticAnalyzer.c:1620-1900`（`analyze_function_call`）
- 第十六批：`SemanticAnalyzer.c:1900-2010`
- 第十七批：`SemanticAnalyzer.c:2010-2300`（本范围截至 2056 行，含 F17-1~F17-9）

## 全局修订应用
- G1（分配失败前提修正，影响第 1–55 批）：`KRT_MALLOC/KRT_CALLOC/KRT_REALLOC/KRT_STRDUP` 失败**不 exit(1)**，而是返回 NULL；`KrtMemoryReportError`（`Allocator.c:425-435`）为空操作。凡原文判为「死代码」的 `if (!ptr) return NULL;` 类条目，改标为【修订 G1】「静默失败 / 未检查前置条件（OOM 时 NULL 解引用或静默 NULL 传播，零诊断）」。本范围受影响条目：6-30、7-9、F14-15、F15-10、F16-2、F16-8。
- G2（S28-1 作废）：本范围无对应条目。
- G3（S38-1~15 死代码降级）：本范围无对应条目。

---

## 第六批（续）· 高

### [高] 6-8｜`ark_link_logger_callback` 吞掉所有 ArkLink 日志
- 位置：`ArkLinkIntegration.c:24-28`
- 类别：诊断缺失
- 触发：ArkLink 内部产生任何 warning/error/info
- 后果：`level`/`message` 全部 `(void)` 丢弃，链接期问题完全不可见
- 修复：按 level 转发到 `KrtError`/`KrtPrintf`

### [高] 6-9｜`output_kind`/`stack_size` 硬编码，忽略配置
- 位置：`ArkLinkIntegration.c:62/79`
- 类别：配置漂移
- 触发：`config->target_type` 为库/静态库，或用户设置 stack size
- 后果：`KRT_TARGET_LIBRARY` 也生成 EXECUTABLE；`ArkBackendInput.stack_size` 被无视
- 修复：从 config 读取并下发

### [高] 6-10｜`KrtArkLinkAddRuntimeObjects` 返回值被忽略
- 位置：`ArkLinkIntegration.c:275`
- 类别：静默失败
- 触发：runtime 对象添加失败
- 后果：链接缺 runtime → 缺符号，错误被推迟到链接期且信息不足
- 修复：检查返回值

### [高] 6-11｜`chmod` 返回值未检查
- 位置：`ArkLinkIntegration.c:299`
- 类别：静默失败
- 触发：chmod 失败（如文件系统不支持）
- 后果：产物无执行权限，用户运行时报「Permission denied」，无任何提示
- 修复：失败时 `KrtError` 警告

### [高] 6-12｜写函数用返回值 0 表示失败，但 0 是合法偏移
- 位置：`KroWriter.c:164/178/192/213/226`
- 类别：契约破坏
- 触发：`ensure_capacity` 失败或 offset 溢出
- 后果：调用方无法区分「失败」与「偏移 0」，静默生成错误代码
- 修复：返回 `int64_t`（-1 表失败）或用 out-param

### [高] 6-13｜`uint64_t value/offset` 截断为 `uint32_t`
- 位置：`KroWriter.c:316/320/378/404`
- 类别：未检查前置条件 / 静默失败
- 触发：符号值或重定位偏移 > 4GiB
- 后果：符号地址/重定位位置被静默截断 → 链接错误
- 修复：截断前检查 `> UINT32_MAX` 并报错

### [高] 6-14｜`add_string` 失败返回 0，被 `kro_add_symbol` 当合法 `name_offset`
- 位置：`KroWriter.c:296`
- 类别：契约破坏 / 静默失败
- 触发：字符串表 OOM
- 后果：符号名变成空串（offset 0 是保留空串），`kro_find_symbol` 可匹配到空名符号
- 修复：`add_string` 失败时 `kro_add_symbol` 返回 -1

### [高] 6-15｜`kro_set_code_offset` 静默忽略越界 offset
- 位置：`KroWriter.c:470-474`
- 类别：契约破坏
- 触发：offset > text_high_water
- 后果：调用方以为已回退，实际未生效，后续写入位置错误
- 修复：返回 bool

### [高] 6-16｜`KrtErrorReportAddEx` 的 `realloc` 失败静默 return
- 位置：`CompilerError.c:145-148`
- 类别：静默失败
- 触发：OOM
- 后果：错误被丢弃，`error_count` 不增，编译可能继续并产出错误结果；用户看不到本应报告的错误
- 修复：设置 `report->oom = true` 并在打印时提示；或终止

### [高] 6-17｜`error_capacity * 2` 溢出
- 位置：`CompilerError.c:146`
- 类别：未检查前置条件
- 修复：检查 `> INT_MAX/2`

### [高] 6-18｜`strdup` 失败未检查
- 位置：`CompilerError.c:98`
- 类别：静默失败
- 触发：`KrtErrorReportSetSourceCode` 且 OOM
- 后果：`source_code` 为 NULL，打印时退化为无源码片段，无提示
- 修复：返回 bool 或记录失败

### [高] 6-19｜`error_code` 字段被收集但打印时从不输出
- 位置：`CompilerError.c:239-306`
- 类别：诊断缺失 / 死代码
- 触发：任何带 `error_code` 的错误
- 后果：错误码（如 `E0001`）被丢弃，用户无法按码检索文档
- 修复：打印 `error->error_code`

### [高] 6-20｜使用 `malloc` 而非 `KRT_MALLOC`
- 位置：`CompilerError.c:54/59`
- 类别：错误处理不一致
- 触发：OOM
- 后果：与 `KRT_MALLOC`/`KrtSafeFree` 体系混用风险（裸 malloc 指针交给安全分配器会静默 no-op 泄漏/非法释放）
- 修复：统一分配器
- 备注：【修订 G1】原文「`KRT_MALLOC` 失败即 exit」前提已废；两分配器都不保证终止，混用与 NULL 传播问题更突出

### [高] 6-21｜`KrtSharedRelease` 过度释放不检测
- 位置：`SmartPtr.c:113-134`
- 类别：未检查前置条件 / 共享可变状态
- 触发：对同一 `SmartPtr` 重复 `KrtSharedRelease`，或 `ref_count` 已为 0
- 后果：`ref_count` 变负，`old_count == 1` 不再触发，数据已释放但计数错乱；`KrtPtrIsValid` 用 `> 0` 判断，负数视为无效但控制块可能已释放 → 悬垂
- 修复：`old_count <= 0` 时报错并不减

### [高] 6-22｜`KrtSharedAcquire` 可复活已过期对象
- 位置：`SmartPtr.c:100-106`
- 类别：共享可变状态 / 未检查前置条件
- 触发：`ref_count == 0` 时 acquire
- 后果：`data` 可能已释放（weak_count==0 时 `KRT_FREE(control)`），acquire 后 `ptr->control` 悬垂
- 修复：`ref_count == 0` 时拒绝 acquire

### [高] 6-23｜`total_allocations - total_deallocations` 无符号下溢
- 位置：`SmartPtr.c:306`
- 类别：未检查前置条件
- 触发：过度释放使 deallocations > allocations
- 后果：打印巨大「Potential Leaks」数，误导
- 修复：用有符号差值并饱和

### [高] 6-24｜`GetModuleFileNameA` 未检查 `len == MAX_PATH`（截断无 NUL）
- 位置：`Path.c:121`
- 类别：未检查前置条件
- 触发：Windows 可执行路径长度 ≥ MAX_PATH
- 后果：`path` 可能未 NUL 终止，后续 `strlen(path)`（:137）越界读
- 修复：`if (len == 0 || len >= MAX_PATH) return -1;` 或显式补 NUL

### [高] 6-25｜`KrtEnsureDirectoryRecursive` Windows 分支对 `C:` 前缀相对路径处理错误
- 位置：`Path.c:279-330`
- 类别：语义分叉 / 未检查前置条件
- 触发：`path = "C:foo"`（盘符相对路径，len=5）
- 后果：`temp[1]==':'` 进入分支，`p=temp+2`，循环把 `C:` 当目录检查/创建，语义错误
- 修复：仅当 `temp[2]` 是 `/` 或 `\` 时才视为盘符绝对路径

### [高] 6-26｜`KrtPathExists` 不检查 NULL
- 位置：`Path.c:36-39`
- 类别：未检查前置条件
- 触发：传入 NULL
- 后果：`stat(NULL,&st)` 返回 -1（errno EFAULT），函数返回 0，非法输入被静默吞掉
- 修复：`if (!path) return 0;`

### [高] 6-27｜`last_sep = (slash > backslash) ? slash : backslash;` 两者均 NULL 时比较无效
- 位置：`Path.c:185`
- 类别：未检查前置条件（脆弱）
- 触发：路径不含 `/` 或 `\`
- 后果：实际结果恰好正确，但依赖实现定义行为，属脆弱写法
- 修复：显式 `last_sep = slash ? slash : backslash;`

### [高] 6-28｜`win32_is_absolute_path` 把 `C:foo` 当绝对路径
- 位置：`PlatformAbstraction.c:27`
- 类别：语义分叉
- 触发：`path = "C:foo"`
- 后果：返回 1（绝对），实际是相对当前目录的盘符相对路径
- 修复：要求 `path[2] == '/' || path[2] == '\\'`

### [高] 6-29｜`KrtPlatformPathJoin` 不检查截断，且 `part` 为绝对路径时结果错误
- 位置：`PlatformAbstraction.c:187-212`
- 类别：静默失败 / 未检查前置条件
- 触发：结果长度 ≥ buffer_size；或 `part` 以 `/` 开头
- 后果：静默截断；`base//abs` 拼接出无效路径
- 修复：检查 `snprintf` 返回值；`part` 绝对时直接返回 part

### [高] 6-30｜`KrtPlatformCreate` 的 NULL 检查
- 位置：`PlatformAbstraction.c:124-127`
- 类别：静默失败 / 契约破坏
- 触发：OOM（`KRT_MALLOC` 失败）
- 后果：`if (!platform) return NULL` **并非死代码**——分配器失败返回 NULL 而不终止；调用方若按「必成功」处理则 NULL 传播崩溃
- 修复：统一分配器语义（要么全承认可失败并检查，要么提供 OR_DIE 宏）
- 备注：【修订 G1】原文「死代码」结论作废

### [高] 6-31｜非 Windows 编译时 `win32_*` 三个函数为死代码
- 位置：`PlatformAbstraction.c:53-76`
- 类别：死代码
- 后果：维护负担；`win32_supports_color` 在 Linux 恒返回 0
- 修复：用 `#ifdef _WIN32` 包裹整组函数

## 第六批（续）· 中

### [中] 6-32｜`header.entry_point` 截断
- 位置：`KroWriter.c:491`（同 6-13，入口 < 4GiB 无实际影响，降级）

### [中] 6-33｜`fclose` 未检查
- 位置：`KroWriter.c:566`（同 6-12）

### [中] 6-34｜`warning_count` 与 `error_count` 统计口径不一致
- 位置：`CompilerError.c:252-254`
- 后果：一个累计、一个实时重算；若 `AddEx` 被外部修改 `warning_count` 会不一致
- 修复：统一循环统计

### [中] 6-35｜`KrtTimeNowSeconds` 跨平台语义分叉
- 位置：`Path.c:20-33`
- 触发：系统时间调整
- 后果：Linux 用 `CLOCK_MONOTONIC`，Windows 用 `GetSystemTimeAsFileTime`（墙钟）→ Windows 耗时跳变/为负
- 修复：Windows 也用 `QueryPerformanceCounter`

### [中] 6-36｜`KrtEnsureDirectoryRecursive` 的 `temp[1024]` 固定尺寸
- 位置：`Path.c:271-273`
- 触发：`KRT_MAX_PATH` 被重定义为 > 1024
- 后果：`len >= sizeof(temp)` 漏检 → 溢出
- 修复：用 `sizeof(temp)` 常量并加 `static_assert`

### [中] 6-37｜`KrtEnsureDirectoryRecursive(obj_dir)` 返回值被忽略
- 位置：`Main.c:986`（配合 6-25 的静默失败）
- 修复：检查并中止

### [中] 6-38｜runtime 目录定位假设 exe 的父目录
- 位置：`ArkLinkIntegration.c:268-277`
- 触发：exe 在 `/usr/bin` 等目录
- 后果：`strrchr(runtime_dir, sep)` 取父目录 → runtime_dir 变 `/usr`，几乎必然找不到 runtime 对象
- 备注：疑似，需验证运行时部署约定

---

## 第七批 · `SemanticAnalyzer.c:1-440`

### [致命] 7-1｜类上下文入栈失败被静默丢弃，后续解析沿用错误上下文
- 位置：`SemanticAnalyzer.c:421-440`
- 类别：资源与生命周期 / 静默失败
- 触发：`KRT_REALLOC` 失败
- 后果：`semantic_analyzer_push_class_context` 为 void 直接 return；类体内 `self`/字段/方法解析继续使用上一个类或全局上下文，产生错误符号绑定且无诊断
- 修复：改为返回 `bool` 并让调用方中止；同时检查 `class_stack_capacity * 2` 的 int 溢出

### [致命] 7-2｜imported_file 注册：未注册却报告成功
- 位置：`SemanticAnalyzer.c:170-177`
- 类别：静默失败 / 诊断缺失
- 触发：`pipeline == NULL`（未调用 `set_pipeline`）但 analyzer/file_path 有效
- 后果：返回 true（未注册任何文件却成功）；而 `analyzer==NULL && file_path!=NULL` 返回 false，同一函数对「未注册」给出相反语义
- 修复：统一返回 false 并通过 `error_report` 记录原因

### [高] 7-3｜`KrtArenaDestroy(analyzer->type_arena)` 未判空
- 位置：`SemanticAnalyzer.c:85-162`（`create` 失败路径 `:60-63,75-84`）
- 类别：未检查前置条件 / 契约破坏
- 触发：`type_arena` 分配失败
- 后果：依赖 `KrtArenaDestroy` 是否判空
- 修复：显式判空并定义契约
- 备注：后续核查发现 `Arena.c:34-36` 已显式判空，此条**降级为无问题**

### [高] 7-4｜`snprintf(project_root,...)` 未检查返回值
- 位置：`SemanticAnalyzer.c:294-378`
- 类别：静默失败
- 触发：路径长度 ≥ `PATH_MAX`
- 后果：静默截断，`project.krt` 探测与 `libs_path` 拼接基于错误路径，`libs_path` 保持 NULL 而无诊断
- 修复：检查返回值，截断即报错拒绝

### [高] 7-5｜`/libs/` 定位取最后一个匹配并就地截断
- 位置：`SemanticAnalyzer.c:326-355`
- 类别：语义分叉
- 触发：`a/libs/x/libs/y` 这类路径
- 后果：取错 project root；与 `import_krt_file`/`import_namespace_folder` 的路径构造各自独立实现
- 修复：抽出唯一 `project_root_from_source_path()` 供各阶段共用

### [高] 7-6｜`realpath()` 失败静默降级 + 错误的绝对路径判定
- 位置：`SemanticAnalyzer.c:380-420`
- 类别：静默失败 / 阶段不对齐
- 触发：`realpath` 失败（文件不存在/权限不足）；或 `file_path[0]=='\\'`（Linux 下非绝对路径）
- 后果：降级为 `cwd + "/" + file_path`，整个导入解析阶段随之错位
- 修复：`realpath` 失败必须报错；绝对路径判定统一走平台抽象层

### [高] 7-7｜跨文件同名导出符号先收集者胜
- 位置：`SemanticAnalyzer.c:191-224`
- 类别：静默失败 / 语义分叉
- 触发：两个文件导出同名函数（签名不同）
- 后果：不比较签名、不报冲突，后者被静默丢弃，跨文件引用可能绑定到错误签名
- 修复：命中已存在 `SYMBOL_FUNCTION` 时比较 mangled 签名，不一致报诊断

### [高] 7-8｜命名空间收集无界递归 + 失败状态不上报
- 位置：`SemanticAnalyzer.c:238-292`
- 类别：未检查前置条件 / 静默失败
- 触发：深嵌套命名空间（栈溢出）
- 后果：`project_namespace_collection_failed` 置位后在本函数内不再上报
- 修复：加深度上限；确认调用方检查
- 备注：后续核查 `SemanticAnalyzer.c:3334` 有检查并转 `result->success=false`，**有检查**（递归深度问题仍需处理）

### [中] 7-9｜`KRT_CALLOC` 失败后返回 NULL 的语义未统一
- 位置：`SemanticAnalyzer.c:41-84`
- 类别：错误处理不一致 / 契约破坏
- 触发：OOM
- 后果：`if (!analyzer)` 分支并非死代码（分配器失败返回 NULL）；但 `symbol_table_create`/`generics_create_registry`/`KrtArenaCreateLocal` 的失败语义未统一
- 修复：统一「分配失败返回 NULL + 上层检查」或「OR_DIE」约定
- 备注：【修订 G1】原文「死代码」结论作废

### [中] 7-10｜容量增长两种标准（溢出检查不一致）
- 位置：`SemanticAnalyzer.c:421-440` 与 `:238-292`
- 类别：未检查前置条件 / 错误处理不一致
- 后果：`push_class_context` 用 `capacity * 2` 无溢出检查，`collect_project_namespaces` 做了 `INT_MAX/2`/`SIZE_MAX` 检查
- 修复：统一溢出检查

### [疑似] 7-S1｜`project_namespace_collection_failed` 是否有调用方检查
- 位置：`SemanticAnalyzer.c:282-292`
- 需验证：grep 全部引用；若无 → 升级致命
- 备注：后续已核查有检查（`:3334`）

### [疑似] 7-S2｜`KrtArenaDestroy(NULL)` 是否安全
- 位置：`SemanticAnalyzer.c:85`
- 需验证：查 `Arena.c`
- 备注：已核查 `Arena.c:34-36` 判空返回，安全

---

## 第八批 · `SemanticAnalyzer.c:440-700`

### [高] 8-1｜符号查找两套实现（当前表 vs 作用域链）
- 位置：`SemanticAnalyzer.c:572-598`（lookup_with_usings）vs `:660`（array_element_type）
- 类别：语义分叉 / 阶段不对齐
- 触发：块内 shadowing
- 后果：`lookup_with_usings` 只用当前表，`semantic_array_element_type` 用 `lookup_scope_chain`，结果不同
- 修复：统一为 `symbol_table_lookup_scope_chain`

### [高] 8-2｜多个 using 同名符号按声明顺序取第一个
- 位置：`SemanticAnalyzer.c:572-598`
- 类别：静默失败 / 诊断缺失
- 后果：歧义被静默消除，无诊断
- 修复：命中 ≥2 个不同符号时报 ambiguous

### [高] 8-3｜找不到「有成员」的类时返回 fallback（无成员类）
- 位置：`SemanticAnalyzer.c:618-648`
- 类别：静默失败 / 契约破坏
- 后果：调用方拿到不完整类符号，后续成员解析必然失败或错误绑定，无诊断
- 修复：区分前向声明与已定义，返回 NULL 或明确诊断

### [高] 8-4｜限定名两种解释先到先得 + `parts[...]` 未判空
- 位置：`SemanticAnalyzer.c:547-570`
- 类别：静默失败 / 未检查前置条件
- 触发：`A.B.C` 可整体当命名空间或「命名空间 A.B 中的符号 C」；或上游传 NULL 元素
- 后果：歧义静默裁决；NULL 崩溃
- 修复：判定歧义报错；对 parts 元素判空/断言

### [高] 8-5｜未知类型统一返回 `TOKEN_EOF`
- 位置：`SemanticAnalyzer.c:650-670`
- 类别：阶段不对齐
- 后果：把「文件结束符」当「未知类型」哨兵；`AST_MEMBER_ACCESS` 依赖 `resolved_type` 就绪，时序早于解析完成时静默返回 EOF
- 修复：引入 `KRT_TYPE_UNKNOWN` 并在解析前给诊断

### [中] 8-6｜`add_using_directive` 清理逻辑经核查基本正确
- 位置：`SemanticAnalyzer.c:440-513`
- 类别：未检查前置条件（待验证）
- 说明：循环内 `KRT_STRDUP` 失败时槽位由 CALLOC 保证为 NULL，释放遍历完整，`using_count` 未递增，无泄漏；真正待验证的是 `semantic_analyzer_add_error_at` 在 analyzer 有效但 using_node 为 NULL 时是否安全

### [中] 8-7｜`semantic_class_has_members` 双遍历存储模型未确认
- 位置：`SemanticAnalyzer.c:600-616`
- 类别：疑似缺陷
- 需验证：`SymbolTable` 的 `symbols` 数组与哈希表是否独立存储；若独立则漏判/误判

### [中] 8-8｜三元表达式递归无深度上限
- 位置：`SemanticAnalyzer.c:650-670`
- 类别：未检查前置条件
- 后果：深嵌套三元（Parser 可产生）栈溢出，与 `:238-292` 同族
- 修复：深度上限

---

## 第九批 · `SemanticAnalyzer.c:700-900`

### [致命] 9-1｜`infer_expression_type` 所有失败路径统一降级为 `TOKEN_INT32`
- 位置：`SemanticAnalyzer.c:708-803`
- 类别：静默失败（全项目最大的汇聚点）
- 触发：`expr==NULL`、标识符查不到符号、调用查不到 callee、`default:` 分支
- 后果：未解析符号/未知表达式被静默赋予 int32 并流入 IR 生成，产出类型错误的机器码而非报错
- 修复：未知类型返回 `KRT_TYPE_UNKNOWN`，调用方中止分析并给带位置诊断

### [致命] 9-2｜三种错误上报函数行为互不一致且 `add_error_at` 对用户不可见
- 位置：`SemanticAnalyzer.c:846-860`（只写 report）、`:805-816`（只写 OutputCache）、`:818-835`（两者都写但打印不带行列）
- 类别：诊断缺失 / 错误处理不一致
- 后果：`add_error_at` 的错误用户完全不可见（除非别处转储 report）
- 修复：合并为唯一 `semantic_analyzer_error(analyzer, node, fmt, ...)`，同时写 report 与输出流并总是带位置

### [致命] 9-3｜legacy mangling 手工 `__` 拼接 + snprintf 截断
- 位置：`SemanticAnalyzer.c:730-745`
- 类别：语义分叉 / 静默失败
- 触发：超长类名/方法名
- 后果：`char legacy[256]` 截断无检查 → 错误 mangled name，查找失败落入 INT32 默认分支；该规则与 `NameMangling.c` 并行实现
- 修复：调用 `NameMangling.c` 唯一入口，删除此处拼接

### [高] 9-4｜`AST_NUMBER` 只做上界检查（无下界）
- 位置：`SemanticAnalyzer.c:723-733`
- 类别：未检查前置条件
- 状态：**已撤回**。复核确认 `integer_value` 是 `unsigned __int128`（`Ast.h:168`），不存在负值下界问题

### [高] 9-5｜三元类型推断两套逻辑
- 位置：`SemanticAnalyzer.c:757-777` vs `:700-707`
- 类别：语义分叉
- 后果：前者 `TOKEN_STRING && TOKEN_NULL` 组合判断并含 `left==right` 分支，后者用 `AST_NULL` 节点判断且无该分支
- 修复：抽成唯一 `semantic_ternary_result_type()`

### [高] 9-6｜一元运算对所有运算符返回操作数类型
- 位置：`SemanticAnalyzer.c:748`
- 类别：静默失败
- 触发：逻辑非 `!x` 应返回 BOOL；若返回 INT 会使 `if (!x)` 条件类型错误
- 备注：需确认 `TOKEN_NOT`/`TOKEN_BITWISE_NOT` 语义划分

### [高] 9-7｜二元运算见到 STRING 即返回 STRING，且不校验操作数
- 位置：`SemanticAnalyzer.c:780-786`
- 类别：静默失败
- 后果：`-`、`*` 等非法字符串运算被赋予 STRING 类型；`TOKEN_AND`/`TOKEN_OR` 直接返回 BOOL 而不校验是按位还是逻辑
- 修复：按运算符白名单校验

### [高] 9-8｜错误上报缓冲区尺寸三种（1024/512）+ 无位置
- 位置：`SemanticAnalyzer.c:805-816`、`:862-873`、`:846-860`
- 类别：错误处理不一致 / 诊断缺失
- 后果：`add_error` 完全不带位置且绕过 `error_report`
- 修复：统一缓冲尺寸与通道

### [高] 9-9｜`add_error_at` 中 `vsnprintf` 返回值未检查
- 位置：`SemanticAnalyzer.c:846-860`
- 类别：诊断缺失
- 后果：超 512 的诊断被静默截断，可能截掉关键路径/符号名
- 修复：检查返回值，超长做截断标记或动态缓冲

### [高] 9-10｜`add_error_ex` 接收行列不校验且打印不含位置
- 位置：`SemanticAnalyzer.c:818-835`
- 类别：诊断缺失 / 阶段不对齐
- 后果：0/负值照传；终端看不到行列
- 修复：打印 `[stage:line:col]`

### [中] 9-11｜`analyzer == NULL` 时静默返回，错误被吞
- 位置：`SemanticAnalyzer.c:805-816`
- 修复：至少 `fprintf(stderr,...)` 兜底

### [中] 9-12｜「无位置」表示不一致（node?line:0 vs 调用方传 0）
- 位置：`SemanticAnalyzer.c:838`
- 类别：阶段不对齐
- 修复：统一表示

### [疑似] 9-S1｜`integer_value` 符号性与 parser 负字面量处理（决定 9-4 是否成立）
- 备注：已撤回（见 9-4）

### [疑似] 9-S2｜`TOKEN_NOT` 逻辑非还是按位非；`TOKEN_AND`/`TOKEN_OR` 逻辑还是按位（决定 9-6、9-7）

### [疑似] 9-S3｜`symbol_table_lookup_scope_chain` 作用于 class 的 nested_table 时父链是否通向全局（决定 `semantic_lookup_exact_in_class` 是否名不副实）

### [疑似] 9-S4｜`error_report` 是否在别处被转储（若无，9-2 升级更严重）

---

## 第十批 · `SemanticAnalyzer.c:900-1120` + `ParserExpression.c` 交叉验证

### [致命] 10-1｜参数个数不同的重载可能被判分并选中
- 位置：`SemanticAnalyzer.c:937-1010`（`semantic_parameter_score`）
- 类别：未检查前置条件
- 触发：候选 `parameter_count != argc`
- 后果：不拒绝候选，而用全零伪参数继续评分 → 参数个数不同的重载可能选中，产生错误绑定
- 修复：`parameter_count != argc` 直接 `return false`

### [致命] 10-2｜`_ZN...` 前缀 `snprintf` 截断无检查 → 可能匹配错误符号
- 位置：`SemanticAnalyzer.c:1012-1061`
- 类别：静默失败
- 触发：类名/方法名较长
- 后果：前缀截断后 `strncmp` 仍可能匹配到错误符号或全不匹配 → 静默选错重载或回退 INT32
- 修复：检查返回值，超长报诊断

### [致命] 10-3｜`TOKEN_POWER` 无优先级，`**` 永不被当作二元运算符
- 位置：`ParserExpression.c:620-657`
- 状态：**已撤回**（见本节末复核更正）。全项目 `TOKEN_POWER` 只作双指针解引用，语言中不存在幂运算；`get_operator_precedence` 不含它是正确的。但 `IrGen.c:1468` 的 `case TOKEN_POWER` 与之冲突，见 11-1

### [致命] 10-4｜`var_name[0]` 在 `var_name == NULL` 时解引用
- 位置：`SemanticAnalyzer.c:1096-1099`
- 类别：未检查前置条件
- 触发：parser 错误恢复产生 NULL 名字
- 后果：崩溃
- 修复：判空并报带位置错误

### [高] 10-5｜`bits = bits * 10 + suffix[i] - '0'` 溢出检查在累加之前
- 位置：`SemanticAnalyzer.c:905-935`
- 类别：未检查前置条件 / 语义分叉
- 触发：超长数字串；或 `bits == 0`（`i0_`）
- 后果：int 溢出为负通过检查传给 `KrtTokenIntegerType(bits,...)`；与 `NameMangling.c` 并行实现
- 修复：累加后立即检查 `bits < 0 || bits > 128` 并拒绝 0

### [高] 10-6｜同时遍历 scope 链与 hash_table 的重复评分/过期副本风险
- 位置：`SemanticAnalyzer.c:1012-1061`
- 类别：疑似缺陷
- 需验证：`SymbolTable.c` 存储模型；若哈希表持过期副本可能选中错误条目

### [高] 10-7｜评分相等的多个重载先遍历者胜
- 位置：`SemanticAnalyzer.c:1012-1061`
- 类别：静默失败
- 后果：歧义静默裁决，无诊断
- 修复：相等时报 ambiguous overload

### [高] 10-8｜`is_unsafe_mode` 作用域管理（降级）
- 位置：`SemanticAnalyzer.c:1087-1094`
- 类别：配置漂移 / 作用域错误
- 后果：analyzer 级全局开关；复核发现 `:1126-1129` 与 `:2599-2609` 均有 `old_mode` 保存/恢复（有作用域管理），但**恢复是否在所有出口（含提前 return）执行未验证**
- 修复：改为块作用域并验证所有出口

### [高] 10-9｜mangled 名构造/解析/legacy 拼接分散三处
- 位置：`SemanticAnalyzer.c:1012-1061`、`:730-745`、`:905-935`
- 类别：语义分叉
- 修复：收敛到 `NameMangling.c` 唯一编解码入口

### [中] 10-10｜`signature->parameters[k]` 未断言非 NULL
- 位置：`SemanticAnalyzer.c:937-1010`
- 触发：`parameter_count == argc` 但 `parameters == NULL`
- 后果：NULL 解引用
- 修复：契约断言

### [中] 10-11｜`nested->hash_size` 与实际桶数一致性未断言
- 位置：`SemanticAnalyzer.c:1012-1061`
- 需验证：realloc 失败路径；否则越界
- 修复：查 `SymbolTable.c`

### [中] 10-12｜运算符优先级表需与语言规范核对
- 位置：`ParserExpression.c:620-657`
- 说明：`TOKEN_PIPE` 与 `TOKEN_BITWISE_OR` 同为 3，`TOKEN_BITWISE_AND`(5) 低于 `TOKEN_EQUAL`(6)；C 语义下正确，若语言规范不同则为缺陷

### [疑似] 10-S1｜`TOKEN_POWER` 到底是否仅为双解引用（10-3 已撤回，仍需确认 `IrGen.c:1468` 不是二元运算）

### [疑似] 10-S2｜`is_unsafe_mode` 设置/复位点是否按块作用域管理（见 10-8）

### [疑似] 10-S3｜`SymbolTable` 的 symbols 作用域链与 hash_table 是否同一批条目（决定 10-6、10-11）

### 复核更正（紧接第十批之后）
- **撤回** 10-3（`TOKEN_POWER` 无优先级）：全项目仅作双指针解引用，无幂运算。
- **降级** 10-8（unsafe 全局开关）：有 `old_mode` 保存/恢复；但所有出口未验证。
- **新增线索**：`SymbolScope` 自带 `hash_table/hash_size`（`SymbolTable.h:47-53`），而 `semantic_lookup_method_by_arity` 只遍历 `sc->symbols` 链 + `nested->hash_table`，**从不遍历 `sc->hash_table`**；若 `symbol_table_define` 写 `scope->hash_table`，该类查找会漏掉整批方法（并入 12-2 处理）。

---

## 第十一批 · `SemanticAnalyzer.c:1120-1200,2590-2620` + `ParserStatement.c:540-580`

### [致命] 11-1｜`**` 语义前后端冲突（前端双解引用 vs 后端 `KrtIrPow`）
- 位置：`IrGen.c:1468-1469` ↔ `ParserExpression.c:620-657`、`ParserStatement.c:197-210`
- 类别：语义分叉 / 死代码
- 触发：用户写 `a ** b`
- 后果：前端只把 `**` 当双指针解引用且优先级为 0，不产生二元 POWER 节点；后端 `case TOKEN_POWER: return KrtIrPow(...)` 的唯一调用点即此处 → `KrtIrPow`（`Ir.c:799`）是死代码
- 修复：统一——前端补 `TOKEN_POWER` 优先级/结合性，或删除 `KrtIrPow` 分支并文档化 `**` 仅为多级解引用

### [致命] 11-2｜赋值名 `KRT_STRDUP` 返回值与源指针均未判空
- 位置：`ParserStatement.c:540-548`
- 类别：未检查前置条件
- 触发：strdup 失败或 `left->data.identifier_name == NULL`
- 后果：写入 NULL；`SemanticAnalyzer.c:1184` 把 `var_name` 直接交给 `lookup_scope_chain` → NULL 解引用崩溃
- 修复：两处判空并报带位置错误

### [致命] 11-3｜表达式形式 `AST_UNSAFE_CALL` 完全忽略 permissions
- 位置：`SemanticAnalyzer.c:1124-1130`
- 类别：语义分叉 / 静默失效
- 触发：表达式写法 `unsafe("krt.mem", ...)`
- 后果：不设置 `unsafe_mem`（语句形式 `:2595-2610` 会设置）；两种形式共用字段却行为不同
- 状态：**降级**。复核确认 `unsafe_mem` 仅在 `SemanticAnalyzer.c:1924` 一处生效（影响面为指针深度声明检查），仍属语义分叉但范围有限
- 修复：抽出唯一 `semantic_analyzer_enter_unsafe(analyzer, permissions, count)`

### [高] 11-4｜二元表达式左右操作数未判空
- 位置：`SemanticAnalyzer.c:1133-1162`
- 触发：`left == NULL`；`binary_op.right == NULL`
- 后果：解引用崩溃
- 修复：入口统一判空

### [高] 11-5｜`TOKEN_ASSIGN` 不做类型兼容性检查
- 位置：`SemanticAnalyzer.c:1133-1162`
- 后果：`int_var = "hello"` 静默通过流入 IR
- 修复：比较 left/right 类型可转换性

### [高] 11-6｜赋值语句不检查可变性与类型
- 位置：`SemanticAnalyzer.c:1182-1190`
- 后果：const/静态字段/只读成员可被赋值；类型不校验；`var_name` 未判空
- 修复：加可变性与类型校验

### [高] 11-7｜一元运算无运算符相关校验
- 位置：`SemanticAnalyzer.c:1164-1165`
- 后果：`-` 作用字符串、`!` 作用 void 全放行
- 修复：按运算符白名单校验

### [高] 11-8｜数组/元组字面量不检查元素类型一致性
- 位置：`SemanticAnalyzer.c:1170-1179`
- 后果：异构数组/元组静默通过
- 修复：比较元素类型

### [高] 11-9｜INT32 兜底使 11-5/11-7 校验形同虚设
- 位置：`SemanticAnalyzer.c:1133-1162`
- 后果：存在未解析符号时校验基于假类型（依赖 9-1）
- 修复：随 9-1 一并解决

### [中] 11-10｜数组赋值目标落入 "Unsupported assignment target"
- 位置：`ParserStatement.c:552-570` vs `SemanticAnalyzer.c:1136`
- 状态：**已撤回**。复核确认 `ParserStatement.c:512-537` 明确处理 `AST_ARRAY_ACCESS` → `AST_ARRAY_ASSIGNMENT`/`AST_ARRAY_COMPOUND_ASSIGNMENT`，阶段契约一致

### [中] 11-11｜strdup 失败时销毁 AST 丢失标识符，诊断缺位置
- 位置：`ParserStatement.c:540-548`
- 后果：后续错误诊断丢失标识符
- 修复：失败时保留名字用于诊断

### [疑似] 11-S1｜`unsafe_mem` 的实际强制点（决定 11-3 影响面；已知 `:1924`）

### [疑似] 11-S2｜数组赋值是否真的无法解析（已撤回，见 11-10）

### [疑似] 11-S3｜`SymbolTable` 的 `scope->hash_table` 是否被写入（决定方法查找漏项；并入 12-2）

### 复核更正（紧接第十一批之后）
- **撤回** 11-10（数组赋值无法解析）：`ParserStatement.c:512-537` 已处理。
- **降级** 11-3（unsafe 表达式忽略 permissions）：影响面限 `:1924` 指针深度检查。
- **新增**：`symbol_table_lookup`（`SymbolTable.c:330-346`）只查表级哈希，而 `define/declare` 同时写表级哈希与 scope 链 → 需确认 `pop_scope` 清理完整性（并入 12-2/12-5）。

---

## 第十二批 · `SymbolTable.c` 全量 + `SemanticAnalyzer.c:2440-2480`

### [致命] 12-1｜作用域链遍历起点错误导致子作用域全部泄漏
- 位置：`SymbolTable.c:123-153`（`destroy` 从 `global_scope` 起，而 `push_scope` 把 `new_scope->parent = current_scope`，global 是链尾 parent==NULL）
- 类别：资源与生命周期
- 触发：错误路径未配对 `pop_scope`
- 后果：残留子作用域的 `SymbolScope`、桶数组与符号全部泄漏
- 修复：从 `current_scope` 起向上遍历到 global_scope

### [致命] 12-2｜符号查找只查表级哈希 + 作用域链失败回退全表哈希
- 位置：`SymbolTable.c:330-346`；`declare`（`:249-251`）无条件写表级哈希；`lookup_scope_chain`（`:354-368`）回退全表哈希
- 类别：语义分叉 / 作用域泄漏
- 触发：块内 shadowing；残留作用域
- 后果：`SemanticAnalyzer.c:573` 的 `lookup_with_usings` 直接用它，块内 shadowing 会取到外层/残留符号
- 修复：删除全表回退，或让 `symbol_table_lookup` 只查 `global_scope`

### [致命] 12-3｜两个 SymbolEntry 共享同一 nested_table
- 位置：`SemanticAnalyzer.c:2470-2471`（`self_class_symbol->nested_table = class_sym->nested_table`）
- 类别：资源与生命周期 / 契约破坏
- 触发：任何新的共享（别名、命名空间合并、嵌套类引用）
- 后果：`symbol_table_destroy` 仅靠 `sym->nested_table != table` 防重入，新共享会 double free
- 修复：引入 `nested_table_owner` 字段，只有所有者负责销毁

### [高] 12-4｜前向引用机制整体失效
- 位置：`SymbolTable.h:79-81` 声明 `register_forward_ref`/`resolve_forward_refs`，`SymbolTable.c` 无定义
- 类别：死代码 / 功能缺失
- 后果：`forward_refs` 数组是纯死代码；`SYMBOL_FORWARD_REF` 状态（`SemanticAnalyzer.c:191` 预登记）没有任何机制转为 DEFINED
- 修复：实现或删除

### [高] 12-5｜`pop_scope` 哈希找不到时不释放符号
- 位置：`SymbolTable.c:169-208`
- 类别：资源与生命周期 / 契约破坏
- 触发：任何只写哈希不写 `scope->symbols` 的新路径
- 后果：`sym`/`sym->name` 泄漏，符号仍留在表级哈希（作用域泄漏）
- 修复：找不到时也释放并 `KRT_ASSERT`/报错

### [高] 12-6｜`SYMBOL_DECLARED → SYMBOL_DEFINED` 不校验 type 一致性
- 位置：`SymbolTable.c:265-302`
- 后果：先 declare VARIABLE 再 define FUNCTION 会留下错误 type，无诊断
- 修复：类型不一致报错

### [高] 12-7｜`grow_table_index` 失败静默继续
- 位置：`SymbolTable.c:228-232`
- 类别：错误处理不一致
- 后果：负载因子无上限增长
- 修复：改为返回 bool 并传播

### [高] 12-8｜共享 nested_table 置空只对当前符号生效
- 位置：`SymbolTable.c:2471`
- 后果：另一方仍持悬垂指针（销毁顺序变化即 UAF），与 12-3 同源
- 修复：同 12-3

### [中] 12-9｜`hash_size == 0` 无保护 → 除零崩溃
- 位置：`SymbolTable.c:330`（`lookup`）、`hash_string`（`:9-15`）
- 后果：`hash_string` 除零
- 修复：函数内统一判零

### [中] 12-10｜`symbol_table_add_error` 是否带位置未确认
- 位置：`SymbolTable.c:542-557`
- 备注：`declare`/`define` 的重复错误只传名字无行列

### [疑似] 12-S1｜`SYMBOL_FORWARD_REF` 是否有其他转换路径（决定 12-4 是「死接口」还是「功能缺失」）

### [疑似] 12-S2｜`symbol_table_add_error` 是否打印行列

### [疑似] 12-S3｜`enter/exit_nested_scope`（`:607-632`）在错误路径下是否泄漏 scope

---

## 第十三批 · `SymbolTable.c` 调用面统计 + `SemanticAnalyzer.c:3743-3924`

### [致命] 13-1｜SymbolTable 大量接口无人调用，连带 error_count 不影响成败
- 位置：`SymbolTable.h:79-82` + `SymbolTable.c:439-466`
- 类别：死代码 / 静默失败
- 后果：`get/set_entry_point`、`check_entry_point_conflict`、`mark_defined`、`define_namespace`、`print_stats`、`get_error_count` 外部引用数 0；`error_count` 无消费者 → 符号表错误不影响编译成败判定；多入口点从不被检测；forward_refs 与 FORWARD_REF 状态无解析机制
- 修复：删除死接口，或接入 pipeline 并让 error_count 参与成败判定

### [致命] 13-2｜同名重载的第二个方法被静默跳过
- 位置：`SemanticAnalyzer.c:3894-3905`
- 类别：静默失败
- 触发：同一类中定义同名重载
- 后果：裸名 `bare_sym` 已定义 → 第二个方法 `lookup_current_scope` 命中后跳过；两个重载共享同一 `SymbolEntry`，`value_type`/`source_type` 停留在第一个重载（`alias_sym` `:3879-3890` 同问题）
- 修复：裸名/别名只在无重载时定义，或定义为容器挂载所有重载

### [致命] 13-3｜`enter_nested_scope` 永久改变 current_scope，错误路径脆弱
- 位置：`SemanticAnalyzer.c:3826-3909`
- 类别：契约破坏 / 脆弱状态管理
- 触发：将来在该分支内加入任何 `return false`/`goto`
- 后果：作用域永久错位（当前分支无提前 return，全靠 continue，暂安全）
- 修复：goto cleanup 模式统一恢复

### [高] 13-4｜`symbol_table_enter_nested_scope` 对 create 返回 NULL 直接解引用
- 位置：`SymbolTable.c:607-623`
- 触发：`symbol_table_create()` 返回 NULL
- 后果：下一行 `entry->nested_table->current_scope` NULL 解引用（对比 `SemanticAnalyzer.c:2455-2460` 有显式检查）
- 修复：`if (!entry->nested_table || !entry->nested_table->current_scope)`

### [高] 13-5｜`full_name[256]` 截断无检查
- 位置：`SemanticAnalyzer.c:3879-3882`
- 类别：静默失败
- 后果：长类名/方法名静默生成错误别名；与 `:730-745`、`:1012-1020` 三处独立 256/512 缓冲同族
- 修复：统一动态拼接并检查

### [高] 13-6｜构造器返回类型被静默设为 INT32
- 位置：`SemanticAnalyzer.c:3866-3876`
- 类别：静默失败
- 修复：构造器显式设为类类型或 void

### [高] 13-7｜`member->function_type` 未判空即克隆
- 位置：`SemanticAnalyzer.c:3869-3876`（`:3884-3886`、`:3896-3899`、`:3902-3904`）
- 需验证：`KrtSourceTypeClone` 是否判空

### [高] 13-8｜未定义检查不覆盖 `SYMBOL_FORWARD_REF`
- 位置：`SymbolTable.c:398-412`
- 后果：跨文件预登记但未定义的函数永远不会被告警（`SemanticAnalyzer.c:195,3373,3393` 大量产生 FORWARD_REF）
- 修复：纳入未定义检查或明确区分

### [高] 13-9｜入口点设置两套机制，其中一套是死代码
- 位置：`SymbolTable.c:457-466`
- 后果：`set_entry_point` 找不到/类型不符时静默返回无诊断；与 `SemanticAnalyzer.c:2659-2663,3016-3020` 直接操作 `is_entry_point` 并存
- 修复：统一入口点设置

### [高] 13-10｜`get_entry_point` 硬编码 `"main"`，忽略可配置入口名
- 位置：`SymbolTable.c:447`
- 类别：配置漂移
- 后果：与 `analyzer->entry_point_name`（`SemanticAnalyzer.c:2662,3019`）不一致；死代码一旦启用即出错
- 修复：读取配置

### [中] 13-11｜`symbol_table_add_error` `vsnprintf` 未检查、无位置
- 位置：`SymbolTable.c:542-557`

### [中] 13-12｜`print_stats` 未判 `current_scope == NULL`
- 位置：`SymbolTable.c:504-537`

### [中] 13-13｜预登记分支 `func_name == NULL` 静默跳过；硬编码 "main"
- 位置：`SemanticAnalyzer.c:3800-3808`
- 后果：配置漂移 + 诊断缺失

### [中] 13-14｜`imported_index < 0` 时导入失败状态丢失
- 位置：`SemanticAnalyzer.c:3916-3919`
- 后果：`imported_source_failed` 不记录

### [疑似] 13-S1｜`KrtSourceTypeClone` 对 `function == NULL` 是否安全（决定 13-7）

### [疑似] 13-S2｜`symbol_table_check_undefined` 的唯一调用点是否真的依赖它做未定义检查

---

## 第十四批 · `SemanticAnalyzer.c:1400-1620` + `PointerAnalysis.inc` 全量

### [致命] F14-1｜`global::` 调用名被指针前移，AST 销毁时释放内部指针
- 位置：`SemanticAnalyzer.c:1621-1625`；`Ast.c:205-207`；`Allocator.c:276-290`
- 类别：资源与生命周期 + 静默失败
- 触发：源码出现 `global::foo()`
- 后果：`call.name = call.name + 8` 前移；销毁时 `KRT_FREE` 释放块内偏移指针 → `find_block` 精确匹配失败 → `KrtMemoryReportError`（release 下空操作）→ 错误被吞、字符串永久泄漏、无诊断（debug 下 `__debugbreak`）
- 修复：不要原地改写指针，用局部变量 `name + 8` 或已有的 `is_global_scope` 字段

### [致命] F14-2｜`semantic_expression_impl` 的 `default: return true;`
- 位置：`SemanticAnalyzer.c:1601-1602`
- 类别：静默失败
- 触发：未被 switch 覆盖的表达式节点（当前唯一实际可达：`AST_SIZEOF_EXPRESSION`）
- 后果：`sizeof(T)` 语义阶段不校验 `sizeof_expr.type_token`，`resolved_type` 被兜底改写成 INT32；新增节点忘加 case 无任何报错
- 修复：`default` 改为报错 return false；至少补 `AST_SIZEOF_EXPRESSION` case

### [致命] F14-3｜解析器从不产出但 IrGen 实现了的节点类型 = 死代码
- 位置：`IrGen.c:2193/2198/2200-2201/2203`；枚举在 `Ast.h`，销毁分支在 `Ast.c`
- 类别：死代码 / 注释与实现脱节
- 后果：`as`、`??`、`?.`、字符串插值、模式匹配、元组访问、delegate 等特性在 IrGen 有完整实现却永不可达；语义分析对它们一律 return true，三层空转
- 修复：补齐语法产出，或删除死分支与枚举

### [高] F14-4｜成员访问无法解析时返回 `object_ok`（true）
- 位置：`SemanticAnalyzer.c:1519`
- 类别：静默失败 / 诊断缺失
- 触发：未定义成员、类名拼写错误、fallback 类、静态成员查找失败
- 后果：错误成员访问进入 IR 生成，可能生成错误成员偏移
- 修复：报 "Undefined member" 并 return false

### [高] F14-5｜LINQ 子句空指针 + 未处理子句静默跳过 + 结果类型不设置
- 位置：`SemanticAnalyzer.c:1581-1593`
- 类别：未检查前置条件 / 静默失败 / 阶段不对齐
- 触发：`from`/`select` 结构不完整；出现 group/join/let 子句
- 后果：`clause == NULL` 崩溃；group/join/let 被静默跳过；`resolved_type` 保持 0 下游被兜底 INT32
- 修复：判空；未支持子句报错；设置 resolved_type

### [高] F14-6｜`ok = ok && analyze(...)` 短路吞掉后续错误
- 位置：`SemanticAnalyzer.c:1577-1578`、`:1584`、`:1586`、`:1591-1592`
- 类别：诊断缺失（多错误只报一个）
- 后果：用户只看到第一个错误
- 修复：`bool sub = analyze(...); ok = ok && sub;`（不用短路）

### [高] F14-7｜lambda 参数名/参数节点未判空；空 body 无诊断
- 位置：`SemanticAnalyzer.c:1552`、`:1551`、`:1561-1570`
- 类别：未检查前置条件 / 诊断缺失
- 后果：`symbol_table_define(table, NULL, ...)` 行为未知；空 body 静默失败
- 修复：补判空报错；expression/body 皆空显式报错

### [高] F14-8｜`PointerAnalysis.inc` AST_CALL 分支对 `name` 未判空即 strcmp
- 位置：`PointerAnalysis.inc:254`
- 类别：未检查前置条件
- 触发：`data.call.object != NULL` 且 `data.call.name == NULL`（`KrtParserStrdup` 对 NULL 入参返回 NULL）
- 后果：`strcmp(NULL,...)` 崩溃；同文件 `:250-251` 的 lookup 是判空的，属同层不一致
- 修复：`object && name && !strcmp(...)`

### [高] F14-9｜内建 `Length` 按名字匹配，劫持用户同名成员
- 位置：`SemanticAnalyzer.c:1504`
- 类别：语义分叉 / 静默失败
- 触发：用户定义名为 `Length` 的成员且未被前面分支命中（如 SYMBOL_VARIABLE 或 fallback 类）
- 后果：用户成员永远不可见，落到 `:1519` 返回 true
- 修复：内建成员用独立命名空间/属性标记判定，或先确认用户表中不存在该名字

### [高] F14-10｜实例类型来源双轨（`resolved_type.type_name` vs `class_type_name`）
- 位置：`SemanticAnalyzer.c:1421` 与 `:1425`
- 类别：阶段不对齐 / 语义分叉
- 后果：同一表达式在不同上下文可能解析到不同类；1421 先赋值后又被 1422-1427 覆盖，冗余
- 修复：只保留一个真源（建议 `variable->class_type_name`）

### [高] F14-11｜非标识符数组访问不做非数组检查；索引检查两套口径
- 位置：`SemanticAnalyzer.c:1535-1541` vs `PointerAnalysis.inc` AST_ARRAY_ACCESS 分支
- 类别：静默失败 / 语义分叉
- 触发：`f()[0]`、`(a+b)[0]`、`this.items[0]`
- 后果：非数组表达式用下标完全跳过检查；整数校验逻辑分散两处
- 修复：抽成单一 helper 对所有表达式类型生效

### [高] F14-12｜`AST_SIZEOF_EXPRESSION` 语义类型被兜底为 INT32
- 位置：`SemanticAnalyzer.c:1598-1599` + `:1694-1695`；`IrGen.c:2196`
- 类别：阶段不对齐
- 后果：语义阶段认为 sizeof 是 int32，IR 阶段生成 usize 常量；存在 `f(int)/f(usize)` 重载时错误绑定 `f(int)`
- 修复：显式设置 `TOKEN_USIZE`

### [中] F14-13｜`AST_CAST_EXPRESSION` impl 只递归不校验
- 位置：`SemanticAnalyzer.c:1598-1599`
- 类别：契约破坏
- 后果：`cast_expr.expression == NULL` 时静默返回 false 不报错；`target_type` 合法性全链路无校验
- 修复：显式检查 target_type 与 expression

### [中] F14-14｜错误信息可能传 NULL
- 位置：`SemanticAnalyzer.c:1462`
- 后果：`printf("%s", NULL)` 是 UB（glibc 打 `(null)`，其他 libc 崩溃）
- 修复：报错前判空

### [中] F14-15｜`KRT_STRDUP` 返回值未检查
- 位置：`SemanticAnalyzer.c:1434`、`:1454`、`:1487`、`:1490` 附近
- 类别：静默失败（【修订 G1】）
- 后果：strdup 失败返回 NULL 且不 exit（G1）→ 静默 NULL 传播、无诊断；原文「死代码式防御」结论作废
- 修复：统一分配失败约定并真正检查/报错

### [高] F14-16｜`PointerAnalysis.inc:294-296` 冗余二次推导，部分节点最终类型变 INT32
- 位置：`PointerAnalysis.inc:294-296`（post-switch 约 `:380`）
- 类别：语义分叉 / 阶段不对齐
- 触发：没有 post-switch 分支的节点类型（`AST_BOOLEAN`、`AST_STRING`、`AST_CHAR_LITERAL`、`AST_ASSIGNMENT`、`AST_COMPOUND_ASSIGNMENT`、`AST_UNSAFE_CALL`、`AST_TUPLE_EXPRESSION`、`AST_LAMBDA_EXPRESSION`、`AST_LINQ_QUERY`、`AST_STATIC_METHOD_CALL`）
- 后果：覆盖结果成为最终结果，`resolved_type` 全部变 INT32（F14-5/F14-12 根因之一）
- 修复：删除冗余覆盖，统一由 post-switch 推导
- 备注：本条是复核更正中「撤销上一轮『覆盖摧毁所有指针元数据』致命推断」后重新定级的真实缺陷；原推断经实证（14 个指针对用例全过）不成立

### [疑似] F14-S1｜`call_argument_assignable` 对 `parameters == NULL` 的解引用
- 位置：`PointerAnalysis.inc:275`
- 需验证：`KrtSourceTypeClone` 与函数类型构造是否保证 `parameters != NULL`

### [疑似] F14-S2｜`callee.function && !callee.pointer_depth` 优先级
- 位置：`PointerAnalysis.inc:262`
- 需验证：`KrtSourceTypeClone` 是否可能同时设置两者

### [疑似] F14-S3｜用户自定义 `Length` 是否真被内建分支劫持（需构造测试用例）

---

## 第十五批 · `SemanticAnalyzer.c:1620-1900`（`analyze_function_call`）

### [致命] F15-1｜`strcmp(method_symbol->name, mangled_name)` 中 `mangled_name` 可为 NULL
- 位置：`SemanticAnalyzer.c:1749`（同类模式 `:1816`）
- 类别：未检查前置条件
- 触发：`name_mangle_function` 返回 NULL → 兜底 `lookup_method_by_arity` 返回非 NULL → 类型检查通过 → `strcmp(..., NULL)`
- 后果：SIGSEGV（`if (!method_symbol)` 拦不住，因为兜底已填上）
- 修复：`if (mangled_name && strcmp(...) != 0)`，或 mangled_name 为 NULL 时直接复制

### [高] F15-2｜变量被当函数调用时完全不检查参数
- 位置：`SemanticAnalyzer.c:1874`、`:1893`
- 类别：静默失败 / 契约破坏
- 触发：`let f = 3; f(1, 2);`
- 后果：语义放行，错误推迟到 IR；与 `PointerAnalysis.inc` AST_CALL post-switch 的检查强度不同（两套实现）
- 修复：变量调用走函数指针类型检查，否则报 "is not callable"

### [高] F15-3｜`resolved_class_name` 覆盖赋值前未释放 → 泄漏
- 位置：`SemanticAnalyzer.c:1847`
- 类别：资源与生命周期
- 触发：同一 AST_CALL 被多次分析且第一次已设置该字段
- 后果：旧字符串永久泄漏；同文件 `:1754-1756`、`:1823-1826` 均先释放 → 同层三种写法不一致
- 修复：抽 `set_resolved_class_name()`，内部先 FREE 再 STRDUP

### [高] F15-4｜未定义变量被当作类名，诊断与实际错误不符
- 位置：`SemanticAnalyzer.c:1700-1701`、`:1762`
- 触发：`undefinedVar.method()`
- 后果：报 "Undefined method/function" 而非 "Undefined variable"，误导用户
- 修复：`var_sym == NULL` 时显式报未定义变量

### [高] F15-5｜`method_name`/`class_name` 未判空即 strstr/strlen
- 位置：`SemanticAnalyzer.c:1837`、`:1842`
- 后果：UB/崩溃
- 修复：进分支前统一判空

### [高] F15-6｜完全相同查找重复执行（死代码）
- 位置：`SemanticAnalyzer.c:1860` 与 `:1864`
- 后果：同一表同一键查两遍，结果必然相同
- 修复：删除 `:1864` 重复查找

### [高] F15-7｜`KRT_STRDUP(NULL)` 失败被误报为内存分配失败
- 位置：`SemanticAnalyzer.c:1852` + `:1855-1857`
- 类别：诊断与实际错误不符
- 后果：真实原因是节点缺名，却报 "Memory allocation failed"
- 修复：判空后报 "Call expression has no name"

### [高] F15-8｜方法查找三段静默回退链
- 位置：`SemanticAnalyzer.c:1732-1743`、`:1797-1815`
- 类别：语义分叉 / 静默失败
- 触发：方法在类内不存在
- 后果：同名全局方法「接管」类内缺失调用，无歧义/缺失提示；两条实例调用路径回退策略不一致
- 修复：统一为单一解析函数，回退至少产生 warning

### [中] F15-9｜legacy 手工 mangling 与 NameMangling.c 分叉
- 位置：`SemanticAnalyzer.c:1837-1849`
- 后果：`class__method` 两处实现，规则变化必然不同步
- 修复：改调 `name_mangle_simple(class_name, method_name)`

### [中] F15-10｜`KRT_MALLOC` 失败检查为死代码但同层其他分支当真
- 位置：`SemanticAnalyzer.c:1744`、`:1844`
- 类别：错误处理不一致（【修订 G1】）
- 后果：G1 后这些检查是**活路径**（分配失败返回 NULL）；但 `:1855` 又为 `!func_name` 报错 → 同函数两种假设
- 修复：统一分配失败约定

### [疑似] F15-S1｜手工识别 mangled 名 `name[0]=='_' && name[1]=='_Z'` 是第三种 mangling 约定判定
- 位置：`SemanticAnalyzer.c:1816-1817`
- 需验证：`NameMangling.c` 实际前缀；是否存在非 mangled 但下划线开头的用户方法被误判

### [疑似] F15-S2｜1700 分支与 1762 分支 `is_instance_call` 设置不一致（1758 设置，1762 未见）
- 需验证：IR 生成是否依赖该标记区分实例/静态调用；若依赖则 1762 路径调用会被错误降级

---

## 第十六批 · `SemanticAnalyzer.c:1900-2010`

### [致命] F16-1｜全局函数调用解析成功后覆盖 `call.name` 未释放旧指针
- 位置：`SemanticAnalyzer.c:1901-1905`
- 类别：资源与生命周期
- 触发：`call_expr->data.call.name = func_name;` 直接覆盖
- 后果：旧 name 永久泄漏；若旧 name 是 F14-1 的前移内部指针则加剧
- 修复：保存 old → 赋值 → `KRT_FREE(old)`

### [致命] F16-2｜泛型多参数被静默截断 + 缺 `>` 不报错
- 位置：`SemanticAnalyzer.c:1944-1998`
- 类别：静默失败 / 未检查前置条件
- 触发 A：`type_args = KRT_MALLOC(sizeof(char*))` 只分配 1 元素，但循环按 `arg_count` 填充
- 后果 A：`Foo<T,U>` 静默截断为 1 个参数，实例化结果错误
- 触发 B：`args_end == NULL`（缺 `>`）走 else 静默跳过整个泛型校验
- 后果 B：`List<int` 被放行
- 修复：按 arg_count 分配；`args_end == NULL` 时报错 return false
- 备注：【修订 G1】「KRT_MALLOC 失败即 exit，if(base_type) 全死代码」的前提作废，但上述截断/缺 `>` 缺陷仍成立

### [高] F16-3｜全局调用只分析实参就返回，不校验签名
- 位置：`SemanticAnalyzer.c:1907-1911`
- 类别：静默失败（F15-2 第三次同根出现：1874、1893、1907）
- 后果：`foo(1,2)` 对 `foo(int)` 静默通过
- 修复：抽 `semantic_check_call_signature(call_expr, func_symbol)` 三处统一调用

### [高] F16-4｜`var_name`/`current_scope` 未判空 + 魔数 `scope_level == 1`
- 位置：`SemanticAnalyzer.c:1916-1936`
- 后果：空指针崩溃；嵌套层级 ≠ 1 时 let 语义判断错误
- 修复：判空；用 `GLOBAL_SCOPE_LEVEL` 常量替代魔数

### [高] F16-5｜重复声明检查做两遍
- 位置：`SemanticAnalyzer.c:1938-1942` vs `symbol_table_define` 内部（SA:2006 调用）
- 类别：语义分叉 / 诊断不符
- 后果：一处报 duplicate，一处静默
- 修复：只保留 define 内部检查

### [高] F16-6｜类体内局部变量被标记为 `SYMBOL_FIELD`
- 位置：`SemanticAnalyzer.c:2000-2004`
- 类别：阶段不对齐
- 后果：与 IR 生成对「局部 vs 字段」的假设不一致
- 修复：仅当 scope_level 为类体层级才设 FIELD

### [中] F16-7｜泛型实例化失败用 `add_error`（只打印不写 report）
- 位置：`SemanticAnalyzer.c:1987`
- 类别：错误处理不一致
- 修复：统一上报通道

### [中] F16-8｜若干 OOM 防御分支
- 位置：`SemanticAnalyzer.c:1944-1955`
- 类别：未检查前置条件（【修订 G1】）
- 后果：`if (base_type)`/`if (args_str)` 等在 G1 后是**活路径**（分配失败返回 NULL），但均无诊断、静默继续
- 修复：分配失败时报错或中止

---

## 第十七批 · `SemanticAnalyzer.c:2010-2300`（本范围截至 2056 行）

### [致命] F17-1｜复合赋值路径使用未初始化的 `stmt->resolved_type`
- 位置：`SemanticAnalyzer.c:2180`
- 类别：未检查前置条件（使用未初始化字段）
- 触发：指针数组复合赋值 `pointer_update_assignable(analyzer, stmt, stmt->resolved_type, ...)`，该分支未赋值 `resolved_type`（非指针路径 `:2187` 才赋值）
- 后果：零值/残留类型当作赋值目标类型做兼容性判定 → 放行非法复合赋值或误报
- 修复：先赋值再传入

### [高] F17-2｜赋值语句不检查左值类别
- 位置：`SemanticAnalyzer.c:2109-2121`
- 类别：静默失败
- 触发：`func_name = 1;` 或 `ClassName = x;`
- 后果：只要 `pointer_assignable` 恰好通过就放行
- 修复：限定 SYMBOL_VARIABLE/FIELD/PARAMETER

### [高] F17-3｜静态变量初始化不检查类型兼容性、不设 resolved_type
- 位置：`SemanticAnalyzer.c:2091-2104`
- 类别：诊断缺失 / 阶段不对齐
- 触发：`static int x = "hello";`
- 后果：静默通过；下游 IR 拿到未设置的 `resolved_type`；与实例变量路径（`:2059`）不一致
- 修复：复用实例变量类型兼容性检查

### [高] F17-4｜整数范围检查被一元负号绕过
- 位置：`SemanticAnalyzer.c:2048`
- 类别：静默失败
- 触发：`-9999999999`（AST 是 `AST_UNARY_OPERATION`）
- 后果：溢出被吞
- 修复：对 `-` 一元运算递归取绝对值做范围检查

### [高] F17-5｜数组赋值路径不校验下标类型
- 位置：`SemanticAnalyzer.c:2142-2160`
- 类别：静默失败 / 语义分叉
- 后果：`arr[1.5] = x;`、`arr["s"] = x;` 静默通过；与 PointerAnalysis 索引校验强度不一致
- 修复：统一调用 `analyze_index_type()`

### [高] F17-6｜return 保证判定漏 switch/try-catch → 假阳性
- 位置：`SemanticAnalyzer.c:2206-2231`
- 类别：诊断与实际错误不符
- 触发：函数体以「所有分支都 return 的 switch」结尾
- 后果：仍报 "Non-void function must return a value"
- 修复：补充 switch/try-catch 全分支 return 判定

### [中] F17-7｜`array_size` 从不设置且不检查常量表达式
- 位置：`SemanticAnalyzer.c:2026-2030`
- 后果：`int arr[n];`（n 非常量）静默通过，符号表丢失数组长度信息
- 修复：常量折叠后写入，否则报错

### [中] F17-8｜`class_type_name = KRT_STRDUP(...)` 失败无诊断
- 位置：`SemanticAnalyzer.c:2019`
- 后果：类名丢失，后续成员解析退化（【修订 G1】后 strdup 失败返回 NULL 是活路径）
- 修复：判空 return false

### [中] F17-9｜AST 声明类型令牌被原地改写
- 位置：`SemanticAnalyzer.c:2076-2081`
- 类别：阶段不对齐
- 触发：`var_decl->data.variable_decl.type = KrtSourceStorage(type);` 把 `int` 改写为 `TOKEN_UINT64`
- 后果：AST 上声明信息被销毁；IrGen 若再读 `variable_decl.type` 会与 `resolved_type` 分叉
- 修复：不要把存储类型写回 AST
