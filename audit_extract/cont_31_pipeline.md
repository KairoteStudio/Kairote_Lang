# 第八十八批：SelfHost/Compiler/Pipeline/CompilerPipeline.krt + Platform/PlatformAbstraction.krt

## 覆盖范围

- 全量读取 `SelfHost/Compiler/Pipeline/CompilerPipeline.krt`（575 行，逐段读完）与 `SelfHost/Compiler/Platform/PlatformAbstraction.krt`（528 行，逐段读完）。
- 对照 `Re.KrtC/src/Compiler/Pipeline/CompilerPipeline.c`、`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.c`：两者与 SelfHost 版结构不同（C 版是 `KrtCompilePipeline`/`KrtPlatform` 另一套 API），仅对 `system()` vs `execve` 环境语义等可对照点做参照，其余标「疑似」。
- 符号级 grep 核对（未整文件通读）：`KrtNativeFunctionIndexInvalidate`、`KrtModulesMarkImportedAst`/`MergeImportedAst`、`KrtErrorReportFlush`/`Add`/`HasErrors`、`KrtDiagnosticMessage`、`KrtPrintRuntimeRequired/Source`、`KrtVmEmit`、`KrtParserParseCompilationUnit`、`KrtSemanticValidateFunction`、`KrtNativeClosureProbeState/Mutation`、`KrtNativeValidateAccess`、`KrtDriver*` 全部调用点、`Test/` 下相关测试引用。
- 结论：两文件共提取 **40 条**问题，其中致命 1、高 2、中 18、低 19。

## 问题条目

### [致命] 88-1｜KrtCompileSource 对 null 的 unit 调用语义校验导致崩溃
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:376`（关联 352-358、380-381）
- 类别：8 未检查前置条件（NULL）
- 触发条件：源码顶层只有 1 个声明（`compilation_unit.child_count == 1`）且其中没有 main 函数，`main_unit` 保持 null，`unit = main_unit` 也为 null。
- 后果：`KrtSemanticValidateFunction(unit)` 在 `Semantic.krt:1923` 直接读 `unit.kind`，空指针解引用，编译器进程崩溃；此时 `parser.errors == 0`，属完全正常的输入路径。
- 最小修复：376 行改为 `else if (!ok && compilation_unit.child_count == 1 && unit != null)`；或在校验前对 `unit == null` 直接返回并补一条 E_ENTRY 诊断。
- 备注：同函数 381 行对同一个 `unit` 已写 `unit != null` 检查，376 行遗漏，属明显不对称。该路径仅经 `KrtCompileSource`/`KrtCompileSourceToFd`/`KrtCompileSourceToPath` 可达（`Test/SelfHost/test_contract.krt` 在用），驱动主路径不经过。

### [高] 88-2｜解析错误被静默吞掉，无任何诊断
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:360-394`（关键 383、388）
- 类别：1 静默失败 / 2 诊断缺失 / 9 错误处理不一致
- 触发条件：源码存在语法错误（`parser.errors != 0`）。
- 后果：360/365/371/383/388 各分支的 `parser.errors == 0` 前提全部不成立，函数直接 `return false`，全程未调用 `KrtDiagnostic`；而同一份源码走 `KrtCompileNativeSourceMode` 会在 469-475 输出 E_PARSE 及逐条 parser 诊断。同一编译请求因入口不同得到「有诊断 / 无诊断」两种结果。
- 最小修复：在 383 行之前（或函数尾部）对 `parser.errors != 0` 复用 469-475 的 E_PARSE 逻辑。
- 备注：`KrtCompileSourceToFd`/`ToPath` 把 false 直接回给调用方（含 `Driver.krt:496` 的 stage1 探针），调用方只能看到失败而看不到原因。

### [中] 88-3｜KrtCompileSource 只删根节点，整棵 AST 泄漏
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:389-393`
- 类别：10 资源与生命周期
- 触发条件：`KrtCompileSource` 的任意返回路径（无论 `ok` 与否）。
- 后果：仅 `delete compilation_unit`（Program 节点自身），`unit.left` / `unit.declarations` / `unit.body` 整棵语法树全部未释放；对照 `KrtCompileReleaseAstGraph:305-314` 的逐节点释放。每次经该入口的编译都泄漏一整棵树。
- 最小修复：复用 `KrtCompileReleaseAstGraph(compilation_unit, null, parser.current_using_scope)`，或至少遍历各链表逐个释放。
- 备注：常量对象路径（363/369/374/386）与原生回退路径（388）都会执行到 390 行。

### [中] 88-4｜IR 发射失败后静默回退到完整原生管线
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:383-388`
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：`KrtSemanticBindLocals`/`KrtIrLowerReturnFunction` 成功但 `KrtKroEmitIrFunction` 返回 false，或 383 行任一前置校验失败。
- 后果：不报告任何错误即调用 `KrtCompileNativeSource` 重新解析并编译同一源码；同一源码在「紧凑 IR 对象 ABI」与「完整原生管线」两套产物/诊断之间静默切换，调用方无法感知实际走了哪条路。
- 最小修复：区分「语义校验失败（应报错）」与「发射失败（可回退）」，回退前记录诊断或提供显式回退开关。
- 备注：388 行的 `!ok` 为恒真（见 88-6），回退实际是无条件发生的。

### [中] 88-5｜诊断上下文不保存、不安装、不推进 stage
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:338-395`
- 类别：9 错误处理不一致 / 2 诊断缺失 / 4 阶段不对齐
- 触发条件：任何调用 `KrtCompileSource` 的路径。
- 后果：`KrtCompileNativeSourceMode:425-430` 会保存 `previous`、安装 `limit=64` 的 report 并在失败时 flush；`KrtCompileSource` 完全不设置 `KrtCurrentDiagnostics`，其内部诊断经 `CompilerState.krt:282-286` 直通 `KrtDiagnosticWrite` 写 stderr，绕过 bundle 诊断与计数；478/492/519/530 的 stage 推进也全部缺失。
- 最小修复：与 `KrtCompileNativeSourceMode` 一致地保存/安装/恢复 `KrtCurrentDiagnostics` 并推进 stage。
- 备注：两者是同一编译器的两套入口，诊断契约必须一致，否则排障信息不可比。

### [低] 88-6｜`!ok` 为恒真死条件
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:360、365、371、376、377、383、388`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：总是成立（`ok` 在 343 行初始化为 false，仅在 364/370/375/386 各分支体内置 true）。
- 后果：`!ok &&` 从未起作用，读者会误以为存在「已成功则跳过」语义；后续若改动 `ok` 初始化，行为会无声改变。
- 最小修复：删除 `!ok &&`，或改写为显式状态机。
- 备注：当前无功能影响，但属零容忍审计中的死条件。

### [中] 88-7｜先 O_TRUNC 打开输出再编译，失败残留空/半截文件
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:410-416`（配合 397-408）
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：`KrtCompileSourceToPath` 中编译失败（399 行），或写 header/text/symbols 任一失败（403-405 行）。
- 后果：411 行以 `O_CREAT|O_TRUNC` 先打开目标路径，失败后目标路径留下 0 字节或半截文件；函数只返回 false，不清理、不诊断（E_OUTPUT 只在 `KrtCompilePath` 里出现）。
- 最小修复：先编译到内存/临时文件，成功后再原子发布（可直接复用 `KrtDriverPublish` 的 temp+rename）；失败时 unlink 目标。
- 备注：`BuildSystem.krt:104` 已用 `KrtDriverPublish` 做原子发布，本函数是遗漏的旧实现。

### [中] 88-8｜错误上报双轨：KrtDriverError 绕过诊断报告与 bundle
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:134-141`（对照 `CompilerPipeline.krt:540-546`）
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：任何调用 `KrtDriverError` 的路径（`BuildSystem.krt:39/40/45/48/56/75/85/102/104/105/106`、`Driver.krt` 等）。
- 后果：`KrtDiagnosticWrite` 在 `KrtDriverActiveBundle != null` 时改走 `KrtSourcesDiagnosticDetails` 上报；`KrtDriverError` 无条件写 fd 2，只设 `KrtDriverLastErrorCode`。同一逻辑错误是否进入 bundle 诊断、是否被 `KrtErrorReportHasErrors` 统计，取决于恰好调用哪个助手。
- 最小修复：让 `KrtDriverError` 复用 `KrtDiagnosticWrite`（或统一走 `KrtCurrentDiagnostics`/`ActiveBundle` 分流）。
- 备注：`KrtDriverError` 同时也不写 `KrtCurrentDiagnostics`，即使非 bundle 场景也不计入报告。

### [高] 88-9｜`library.mode` 魔术文件静默切换编译模式
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:569-571`
- 类别：11 配置漂移 / 1 静默失败
- 触发条件：进程 CWD 下存在名为 `library.mode` 的路径——普通文件会触发，**同名目录也会触发**（`open(O_RDONLY)` 对目录成功）；反之文件缺失则不触发。
- 后果：`library_mode` 由当前目录的偶然内容决定，同一份 `program.krt` 在不同目录下会编译出可执行/库两种不同产物（`Driver.krt:496` 的 stage1 探针正走此路径）；打开失败（EACCES 等）与不存在都被当作 false，无任何诊断。
- 最小修复：用显式命令行选项或环境变量表达库模式；至少对「存在但不是普通文件」报错。
- 备注：全仓库仅此一处引用 `library.mode`；与 `KrtDriverArtifactTarget` 同属隐式全局配置，且与 88-16 叠加时会产生「静默产出可执行对象」。

### [中] 88-10｜KrtCompilePath 在成功与失败路径都泄漏 object/source
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:565-573`
- 类别：10 资源与生命周期
- 触发条件：572 行编译失败、573 行写盘失败，以及正常成功路径。
- 后果：`new KrtKroObject()`（568）与 `new KrtDriverBuffer()`（565）在所有路径都不释放；`KrtCompileSourceToFd:406` 与 `BuildSystem.krt:74` 印证 object 归调用方所有。
- 最小修复：两条失败 return 前 `delete object;`，成功路径同样释放；补 `delete source;`。
- 备注：与 `BuildSystem.krt:65-74` 的同类泄漏同源，属系统性遗漏。

### [中] 88-11｜KrtCompilePath 依赖未保存的全局 artifact target
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:564-575`（配合 514/524/525）
- 类别：6 契约破坏 / 11 配置漂移
- 触发条件：同一进程内此前执行过 `target=2/4` 的编译且 `KrtDriverArtifactTarget` 未被恢复，再调用 `KrtCompilePath`。
- 后果：`KrtCompilePath` 不保存/不设置 `KrtDriverArtifactTarget`，`CoreRun` 在 524/525 行会按残留值走 IR/VM 分支，「探针」产出非预期产物。
- 最小修复：`KrtCompilePath` 内显式保存并设置 `KrtDriverArtifactTarget`，返回前恢复。
- 备注：`Driver.krt:491-498` 显示该函数在参数解析前执行，当前全局为默认 0 故不触发，但契约脆弱。

### [低] 88-12｜源文件长度上限两处不一致
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:567`（对照 `PlatformAbstraction.krt:151`）
- 类别：4 阶段不对齐 / 6 契约破坏
- 触发条件：输入文件长度落在 `(16777216, 33554432]` 区间。
- 后果：`KrtDriverReadFile` 允许读到 32MB，`KrtCompilePath` 读到 16MB 后才以 E_SIZE 拒绝；两个上限分属不同层，改动其一会产生「能读不能编」或「不能读」的漂移。
- 最小修复：把上限提为共享常量，或让 `KrtDriverReadFile` 接受调用方上限参数。
- 备注：`BuildSystem.krt:63` 也出现 16777216，但那是合并后 bundle 的上限，语义不同，易被误改。

### [中] 88-13｜诊断报告与记录在 flush 后不释放
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:426、429-430`（根因 `CompilerState.krt:288-291` 的 `KrtErrorReportFlush` 无释放）
- 类别：10 资源与生命周期
- 触发条件：每次 `KrtCompileNativeSourceMode` 调用（无论成功或失败）。
- 后果：`report = new KrtCompilerDiagnostics()` 与 `KrtErrorReportAdd` 新建的 `KrtDiagnosticRecord` 链在 flush 后都不释放；对照 `Types.krt:1231-1233` 的 closure probe 会逐条 `delete record` 并 `delete KrtCurrentDiagnostics`。每次编译泄漏一条报告链。
- 最小修复：为 `KrtErrorReportFlush` 增加释放（或新增 `KrtErrorReportRelease`），在 430 行恢复 `previous` 前调用。
- 备注：成功路径若有记录同样被直接丢弃。

### [低] 88-14｜诊断条数静默截断且无提示
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:426`（机制 `CompilerState.krt:269`）
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：单次编译产生的诊断位置数超过 64（`KrtErrorReportAdd` 还会按 `position + code` 去重）。
- 后果：第 65 条起被静默丢弃，用户看到的错误列表不完整，且没有任何「已截断」提示。
- 最小修复：达到 limit 时补一条溢出标记，或约定 limit=0 表示不限。
- 备注：`Types.krt:1199` 的嵌套 report 继承同一 limit，截断会叠加。

### [中] 88-15｜KrtDriverLastVm 未检查 new，失败后残留且从不释放
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:526-528`（使用点 `BuildSystem.krt:73`）
- 类别：8 未检查前置条件 / 10 资源与生命周期 / 7 共享可变状态
- 触发条件：`new KrtDriverBuffer()` 返回 null（OOM）；或 `KrtVmEmit` 返回 false（E_VM）。
- 后果：526 行未检查即把 null 传给 `KrtVmEmit`，`Interpreter.krt:139` 的 `artifact.ok = false` 空指针崩溃；E_VM 失败时 `KrtDriverLastVm` 仍非空且从不释放，`BuildSystem.krt:73` 只凭 `!= null && ok` 判断，残留值跨编译存在（workspace 释放不清该全局）。
- 最小修复：检查 new；E_VM 失败时置 `KrtDriverLastVm = null`；提供释放路径。
- 备注：`KrtDriverLastModule` 在 320 行有对称清理，`KrtDriverLastVm` 没有，属遗漏。

### [中] 88-16｜library_mode 被静默改写为 `library_mode && !has_main`
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:483`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：调用方传 `library_mode=true` 但源码含 main 函数（`KrtCompilePath` 在存在 `library.mode` 时即如此）。
- 后果：库模式被静默降级为可执行模式，484 行入口点校验、491 行 `unit.op = 240`、514 行库产物分支全部随之改变；调用方无法知道模式被改写。
- 最小修复：含 main 时显式报 E_MODULE，或忽略 main 继续库模式，二选一并记录诊断。
- 备注：与 88-9 叠加时，「CWD 有 library.mode + 源码含 main」= 静默产出可执行对象。

### [中] 88-17｜closure probe 状态不在 AST 释放范围内
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:91-120`（`KrtCompileReleaseAstState`）
- 类别：10 资源与生命周期
- 触发条件：closure probe 未正常 `KrtNativeClosureProbeEnd` 就失败退出（如 `KrtNativeLowerModule` 在探测中途返回 false），随后 `KrtCompileWorkspaceRelease` → `KrtCompileReleaseAstState`。
- 后果：只释放 `native_generic_registry`/`native_globals`/`native_inheritance_stack`/`native_function_index`，不释放 `unit.native_closure_probe_state`；该状态含 mutation 快照（深拷贝的 `KrtAstNode`，`Types.krt:1102-1105`）与 `previous` 链，全部泄漏。
- 最小修复：在 `ReleaseAstState` 中遍历并释放 probe state 链（含 `mutation.snapshot`）。
- 备注：疑似——取决于 lower 失败时是否确实遗留未结束的 probe；正常路径 `Types.krt:1234` 会 `delete state`。

### [中] 88-18｜释放队列失败后，回退路径只清 4 个字段，整图泄漏
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:283、328-330`
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：`KrtCompileAstPush` 扩容失败（节点数 > 16777216 或 `new` 返回 null）使 `queue.ok = false`。
- 后果：`KrtCompileAstReleaseMarks` 只清标记并释放 `queue.nodes`/`queue`，已标记的 AST 节点、scope、signature、generator、capture、cell_owner 全部未释放；调用方 328-330 的回退只调 `KrtCompileReleaseAstState`（仅清 unit 上 4 个字段），整棵树泄漏。
- 最小修复：失败回退时遍历 `queue.nodes` 释放节点及其自有数组，并释放各链。
- 备注：队列失败属 OOM 路径，但零容忍审计要求失败路径也不泄漏。

### [低] 88-19｜print_runtime 的 op=241 标记只覆盖顶层声明
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:465-468`
- 类别：6 契约破坏 / 4 阶段不对齐
- 触发条件：`KrtPrintRuntimeRequired` 为真，且注入的运行时源中存在嵌套（非 `unit.left` 顶层）并需要 `op=241` 的节点。
- 后果：只遍历 `unit.left` 链设置 `op=241`，嵌套节点保持原 op；后续阶段若依赖该标记则行为不一致，且与 `start >= original_length` 这一「按源码位置」的判据本意不符。
- 最小修复：改用与 `KrtCompileReleaseAstGraph` 同构的全图 BFS 标记 `start >= original_length` 的节点。
- 备注：疑似——当前 `KrtPrintRuntimeSource()` 全是顶层函数，暂不触发；但判据本身支持嵌套。

### [低] 88-20｜runtime_build_keys 释放侧硬编码 4096，与分配点隐式耦合
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:45-53`（分配点 `SemanticRuntime.krt:1010`）
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：`SemanticRuntime.krt:1010` 的 `new KrtNativeRuntimeKey[4096]` 大小被改动，而释放侧未同步。
- 后果：释放循环固定 `bucket < 4096`——分配变大则漏释放（内存泄漏），分配变小则越界读（未定义行为）。
- 最小修复：把容量存进 module 字段，释放时读该字段；或提为共享常量并加断言。
- 备注：疑似——当前两处都是 4096，无实际错误；但跨文件隐式契约无任何保护。

### [中] 88-21｜分配器只增不减，旧 region 从不回收
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:19-32`
- 类别：10 资源与生命周期
- 触发条件：进程内反复申请字符串/缓冲（路径、诊断、源码副本、列目录结果都走 `KrtDriverAlloc`）。
- 后果：池满即 `mmap` 新 region，旧 region 从不 `munmap`，所有 `KrtDriver*` 字符串永久驻留；进程生命周期内内存单调上涨且无上限。
- 最小修复：引入可回收块分配器/分代 arena，或在编译单元边界重置（重置前确保无悬垂引用）。
- 备注：`CompilerPipeline.krt:450-453` 每次需要 print 的编译还会额外复制整份源码，进一步放大增长。

### [中] 88-22｜子进程环境被清空，与 C 版 system() 语义分叉
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:513-518`（对照 `Re.KrtC/src/Compiler/Platform/PlatformAbstraction.c:119-120` 使用 `system()`）
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：`KrtDriverRun` 执行 linker/外部工具（`BuildSystem.krt:101` 调 ArkLink）。
- 后果：`environment[0] = 0` 使 `execve` 的 envp 为空数组，子进程丢失 PATH/HOME/TMPDIR/LD_* 等；C 版经 `system()` 继承父环境。ArkLink 若再调外部工具或读配置会失败或行为不同，且失败统一报 127，根因被掩盖。
- 最小修复：构造并传递 `environ`（或至少把当前进程环境导出为 `K=v` 数组）。
- 备注：`argv[0]` 直接置为 executable 路径，也与 shell 习惯不同，排障时易混淆。

### [中] 88-23｜目录项插入排序未检查 new 与空值，级联空指针
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:313-319`
- 类别：8 未检查前置条件
- 触发条件：`new KrtDriverPath()` 返回 null；或 `KrtDriverJoinPath`/`KrtDriverCopyString` 因分配失败返回 0。
- 后果：`item.value = ...` 空指针崩溃；即使 item 非空，`item.value = 0` 会被传入无 null 检查的 `KrtDriverStringCompare`（56-62 行）解引用地址 0。两种情况都是编译器崩溃而非报错。
- 最小修复：检查 `new` 与 `value`；给 `KrtDriverStringCompare`/`KrtDriverStringLength` 增加 null 保护。
- 备注：该函数被 `KrtDriverListFiles`/`ListDirectories`/`ListKrtFiles`/`RemoveTree` 与 `Project.krt:105/120/514` 共用。

### [中] 88-24｜中间目录链在转换后从不释放
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:330-346`（以及 370-372）
- 类别：10 资源与生命周期
- 触发条件：任何 `KrtDriverListFiles`/`KrtDriverListDirectories`/`KrtDriverListKrtFiles`/`KrtDriverRemoveTree` 调用。
- 后果：`KrtDriverListEntries` 返回的 `KrtDriverPath` 链（堆对象）在转换成 files/directories 后从不 `delete`；调用方 `Project.krt:105/120/514` 也直接丢弃返回值。每次列目录泄漏一整条链。
- 最小修复：转换完成后遍历释放 `entries`。
- 备注：全仓库多处同类泄漏，属系统性遗漏而非单点疏忽。

### [中] 88-25｜KrtDriverGetEnv 拷贝分配未检查，失败即写空指针
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:457-462`
- 类别：8 未检查前置条件
- 触发条件：`KrtDriverAlloc(buffer.length + 2)` 返回 0（mmap 失败或超过 64MB 上限）。
- 后果：`target[index] = source[index]` 向地址 0 写入，崩溃；且 455 行已把 `KrtDriverEnvironment` 指向 `buffer.data`，失败后全局状态半初始化。
- 最小修复：检查 `copy`，失败时保持 `KrtDriverEnvironment = 0` 并返回空串。
- 备注：456 行注释「两个终止符使遍历独立于大小」只覆盖了正常路径，未覆盖分配失败。

### [中] 88-26｜RemoveTree 泄漏目录链、递归无界、列目录失败被吞
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:368-373`
- 类别：10 资源与生命周期 / 8 未检查前置条件 / 1 静默失败
- 触发条件：深层目录树；或目录无权限导致 `KrtDriverListEntries` 返回 null。
- 后果：`entries` 链泄漏（同 88-24）；递归深度无上限，深树可爆栈；列目录失败时 `entries == null` 被当作空目录，继续 `rmdir`，错误被静默吞掉。
- 最小修复：释放 `entries`；改用显式栈迭代；列目录失败时直接返回 false。
- 备注：用于 `BuildSystem.krt:108` 清理临时目录，深树场景少，但静默失败会掩盖清理不干净。

### [中] 88-27｜仓库定位失败静默回退 CWD
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:488-499`
- 类别：11 配置漂移 / 1 静默失败
- 触发条件：`KAIROTE_ROOT` 未设置或无效，且从可执行文件目录向上 12 层都找不到同时含 `SelfHost` 与 `libs` 的目录；或 `KrtDriverCurrentDirectory` 失败。
- 后果：返回 CWD（甚至 0），`Driver.krt:392-394` 据此拼出错误的 `library_root` 与 `linker` 路径，直到链接阶段才以 E_LINK 等形式失败，根因（仓库定位失败）完全不报告。
- 最小修复：定位失败时返回 0 并由调用方报 E_CONFIG，不要回退 CWD。
- 备注：`KAIROTE_ROOT` 存在时经 `KrtDriverAbsolutePath`，符号链接循环等失败同样只返回 0（241-268 行）。

### [低] 88-28｜ReadFile 未检查 buffer，且上限「先读后拒」
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:143-169`
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：`buffer == null`（`CompilerPipeline.krt:565`、`KrtDriverArguments:436`、`KrtDriverGetEnv:453` 的 `new` 均未检查）；或文件达到 32MB。
- 后果：143-144 行对 null 解引用；32MB 路径在已读满 32MB 后才失败，`data` 与已读字节全部丢弃并滞留池中。
- 最小修复：函数入口检查 `buffer`；把上限上提给调用方，或先 `stat` 再读。
- 备注：与 88-21 的池增长叠加，属「读得越多、留得越多」。

### [低] 88-29｜字符串原语无 null / 非 NUL 终止保护
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:34-62`
- 类别：8 未检查前置条件
- 触发条件：传入 0 或非 NUL 终止的指针（`KrtDriverStringCompare` 无 null 检查，`KrtDriverStringLength` 无长度上限）。
- 后果：空指针崩溃或越界读；调用链上多处把可能为 0 的返回值直接传入（见 88-23），一旦上游分配失败即连锁崩溃。
- 最小修复：原语对 0 返回安全值（长度 0、按空串比较），并在注释中固化 NUL 终止前置条件。
- 备注：疑似——当前多数调用方保证非空，但 88-23 的级联会破坏该前提。

### [低] 88-30｜KrtDriverArguments 泄漏 buffer 且产生空参数项
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:435-449`
- 类别：10 资源与生命周期 / 9 错误处理不一致
- 触发条件：任何进程启动；或 `KrtDriverCopyString` 分配失败。
- 后果：`buffer` 从不 `delete`；`CopyString` 失败时 `value = 0` 进入参数链，后续 `KrtDriverWord`/`KrtDriverStringEqual` 对 0 的语义不统一（`StringEqual(0,0)=true`、`EndsWith(0,"")=false`）。
- 最小修复：返回前释放 `buffer`（保留 data 指针链）；对 `CopyString` 失败显式报错。
- 备注：每次进程启动调用一次，泄漏量小但确定存在。

### [低] 88-31｜临时路径全局索引非原子
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:16、387-392`（使用点 `BuildSystem.krt:50-55、417-423`）
- 类别：7 共享可变状态 / 8 未检查前置条件
- 触发条件：同一进程内并发调用 `KrtDriverTemporaryPath`（当前并行靠 fork，进程内暂不并发，但 API 无任何保护或说明）。
- 后果：两个调用拿到同一 index → 同一路径 → `O_EXCL` 重试 128 次后失败，或互相覆盖产物。
- 最小修复：改用原子自增，或把 pid+计数器并入单一来源，并注释并发约束。
- 备注：疑似——`BuildSystem.krt:745-747` 的注释表明当前并行通过 fork 实现，进程内无并发。

### [低] 88-32｜KrtDriverCopyFile 为死代码
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:412-414`
- 类别：5 死代码
- 触发条件：全仓库 grep 无任何调用点（仅 `KrtDriverPublish` 被 `BuildSystem.krt:104/105/106` 与 `CompilerPipeline.krt` 使用）。
- 后果：维护者可能误以为存在「复制文件」语义而依赖它，实际与 `Publish` 完全等价（多一次临时文件 + rename），语义重复且易误用。
- 最小修复：删除；或实现真正不同的语义并补齐调用点。
- 备注：与 `KrtDriverPublish` 仅差一个函数名。

### [低] 88-33｜平台常量硬编码，与「平台抽象」命名脱节
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:190`（使用点 `Project.krt:260、989`）
- 类别：13 注释与实现脱节 / 11 配置漂移
- 触发条件：把编译器移植到非 Linux，或做交叉编译。
- 后果：`KrtDriverPlatform()` 恒返回 `"linux"`，`Project.krt` 用它拼 `bin/linux/...` 与构建脚本变量 `os`/`platform`；文件头注释也自述「Linux x86-64 services」，命名与用途（平台抽象）不符。
- 最小修复：按实际编译目标返回；或改名为 `KrtDriverLinuxPlatform` 并去掉「抽象」暗示。
- 备注：C 版有 `KrtPlatformType` 枚举与多平台实现，SelfHost 版已退化。

### [低] 88-34｜空串后缀语义与 StartsWith 不一致
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:101-105`（对照 91-99）
- 类别：9 错误处理不一致 / 3 语义分叉
- 触发条件：`KrtDriverEndsWith(0, "")`，或 `KrtDriverEndsWith(text, "")`。
- 后果：`EndsWith(text,"")` 走 `KrtDriverStringEqual(text+length, "")` 返回 true；但 `text == 0` 时 `length=0、count=0`，`text+0 == 0` 与非 0 空串比较得 false，与 `StartsWith(0,"")==true` 矛盾。
- 最小修复：显式处理 `count == 0` 返回 true。
- 备注：影响面小，但属契约不一致，易被上层误用。

### [低] 88-35｜getcwd 失败时把相对路径误规范化为 `/rel`
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:209-238`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`KrtDriverCurrentDirectory` 返回 0（如 CWD 被删除导致 getcwd 失败），且传入相对路径。
- 后果：`KrtDriverJoinPath(0, path)` 返回相对路径本身，随后 219 行无条件写 `target[0] = '/'`，把 `"foo"` 变成 `"/foo"`，静默制造错误的绝对路径。
- 最小修复：`current == 0` 时直接返回 0 并报错，不再继续规范化。
- 备注：`KrtDriverAbsolutePath:241-242` 对 0 有检查，但错误此时已被制造。

### [低] 88-36｜Stem 对点前缀文件名的扩展名判定不一致
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:288-295`
- 类别：3 语义分叉
- 触发条件：basename 以 `.` 开头且仅有一个点（如 `.krt`、`.hidden`）。
- 后果：`while (index > 0)` 不检查 `index == 0` 处的点，`end` 保持 `length`，stem 等于原名；而 `"a.krt"` 会得到 `"a"`。与 `KrtDriverListKrtFiles`（按 `.krt` 过滤）组合时行为不统一。
- 最小修复：明确语义（点前缀名视为无扩展名则加注释，否则改为 `index >= 0`）。
- 备注：低风险，但属命名辅助函数的契约不清。

### [低] 88-37｜KrtDriverPrint 忽略写失败
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:133`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：stdout 写失败（EPIPE/ENOSPC，例如输出管道被对端关闭）。
- 后果：完全忽略 `KrtDriverWriteAll` 的返回值，usage/version 等输出丢失且无任何提示；`KrtDriverError`（134-141）同样忽略写失败。
- 最小修复：至少记录失败，或改为返回 bool 供调用方判断。
- 备注：与 88-8 同属错误处理不一致，但本条聚焦「写失败被吞」。

### [低] 88-38｜可执行路径失败静默传播为链接错误
- 位置：`SelfHost/Compiler/Platform/PlatformAbstraction.krt:480-486`（使用点 `Driver.krt:389`）
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`readlink("/proc/self/exe")` 失败（非 Linux 环境或权限受限）。
- 后果：返回 0，`Driver.krt:389` 的 `options.compiler = 0` 无校验，后续 `KrtDriverRun(0, ...)` → `execve(NULL)` → EFAULT → 127 → 报 "native linker failed"，真实原因被掩盖。
- 最小修复：返回 0 时由调用方报 E_CONFIG/E_DRIVER，而不是继续往下走。
- 备注：文件自述 Linux x86-64，但无任何运行期平台校验。

### [低] 88-39｜关键路径缺自动化测试
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:564-575`、`SelfHost/Compiler/Platform/PlatformAbstraction.krt:416-433、501-528`（`Test/` 下无 `KrtCompileNativeSourceMode`/`KrtCompilePath`/`KrtDriverPublish`/`KrtDriverRun` 引用）
- 类别：12 测试不足
- 触发条件：回归验证或改动上述函数时。
- 后果：库模式/产物目标（2/4）、E_VM/E_EMIT/E_LIBRARY_SYMBOL、诊断 flush/恢复、原子发布、子进程退出码解码均无自动化覆盖；`Test/SelfHost/test_contract.krt` 只覆盖紧凑 `KrtCompileSource` 系列。
- 最小修复：为 `KrtCompilePath`（含 `library.mode` 分支）与 `KrtDriverPublish`/`KrtDriverRun` 增加契约测试。
- 备注：本条为覆盖性说明，具体缺陷见 88-9、88-13、88-15、88-22。

### [低] 88-40｜两套诊断写出实现，格式与分流各有一套
- 位置：`SelfHost/Compiler/Pipeline/CompilerPipeline.krt:540-561`（对照 `PlatformAbstraction.krt:134-141`）
- 类别：13 注释与实现脱节 / 9 错误处理不一致
- 触发条件：两处写出函数在不同调用路径被触发。
- 后果：`KrtDiagnosticWrite` 输出 `code at byte N`（十进制位置），`KrtDriverError` 输出 `code: detail`；同一 E_* 编码存在两种格式，且只有前者做 `position < 0` 钳制（552 行）与 `ActiveBundle` 分流（543 行）。
- 最小修复：统一到单一出口（code/detail/position 三参数）。
- 备注：与 88-8 相关但侧重不同（88-8 关注上报通路，本条关注输出格式与边界处理），故单列。
