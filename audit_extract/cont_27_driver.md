# 第八十四批：SelfHost/Compiler/Driver/Driver.krt（全量）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Driver/Driver.krt`，全量 1346 行（分三段读完：1-340、341-680、681-1020、1021-1346）。
- 覆盖函数/区域：KRO ABI 校验（`KrtDriverAbi*`，1-199）、产物输出（`KrtArtifact*`，201-346）、命令行与入口（`KrtDriverParseArguments`/`KrtDriverMain`，348-518）、模块元数据导入（`KrtModules*`，520-743）、并行编译（745-834）、源加载与项目源收集（836-1346）。
- 对照来源：`ArkLink/src/Core/NativeAbi.c`（ABI 记录语义）、`SelfHost/Compiler/Platform/PlatformAbstraction.krt`（`KrtDriverAlloc`/`KrtDriverError`/`KrtDriverReadFile`/`KrtDriverAbsolutePath` 契约）、`BuildSystem.krt`/`Project.krt`/`CompilerPipeline.krt`/`SemanticFeatures.krt`（符号调用点，仅 grep 特定符号）。
- 未通读其他源文件；跨文件结论均标注「疑似」或给出 grep 行号。

## 问题条目

### [致命] 84-1｜模块接口 original/transformed 别名被就地清空，重复导入误报冲突
- 位置：`SelfHost/Compiler/Driver/Driver.krt:544`（`file.original=data;file.transformed=data;`）、`:537`（比较 `previous.original`）、`:1107-1109`（清空 `transformed`）
- 类别：3 语义分叉 / 10 资源与生命周期
- 触发条件：同一模块接口被第二次 `KrtModulesAppend`（同一 KRO 对象在 objects 链中重复，或两个对象声明同一 module path）。
- 后果：第一次追加时 `KrtSourcesParseDirective` 把 `transformed` 中的 import 字节改成空格，而 `original` 与 `transformed` 同指针，导致 `previous.original` 已不是提供方原始字节；第二次 `:537` 比较不等，直接报 `E_MODULE_METADATA "repeated module declaration interfaces disagree"`，合法重复导入被拒绝。同时 `Project.krt:337` 用 `file.original` 计算项目哈希，元数据哈希随 import 清空漂移，缓存判定不稳定。
- 最小修复：`KrtModulesAppend` 分别复制两份缓冲（`original` 保留提供方字节，`transformed` 单独分配并清空），或比较前用独立保存的 raw 副本。
- 备注：普通源文件路径 `KrtSourcesLoad` 是两份（`:1186` 与 `:1195`），仅元数据路径漏掉；根因是别名。

### [高] 84-2｜`KrtModulesSameModule` 把不同根源文件判为同一模块
- 位置：`SelfHost/Compiler/Driver/Driver.krt:696-705`（关键 `:704`）
- 类别：3 语义分叉
- 触发条件：两个位置分别来自不同根源文件（`KrtSourcesLoad` 不设置 `module_authority`，默认 0），经 `SemanticFeatures.krt:67` 的 `module_internal` 可见性判断调用。
- 后果：`first_authority==second_authority==0` 直接返回 true，跨文件 internal 可见性被当作同模块放行；只有 imported metadata 才有非 0 authority，根文件语义缺失。
- 最小修复：`KrtSourcesLoad` 对普通源设置 `module_authority=path`（或文件唯一 id），并仅在两边 authority 相等且非 0 时判同模块；两边皆 0 时按同一文件判定。
- 备注：`KrtModulesSameModule` 还被 `Semantic.krt:1538/1543` 用于来源归属，影响面不止可见性。

### [高] 84-3｜Project 构建路径未设置 `KrtDriverActiveBundle`，导入标记与去重整体失效
- 位置：`SelfHost/Compiler/Driver/Driver.krt:707-711`（active bundle 为 null 直接 return true）、`:642-643`（无 native_imported 直接 return true）；对照 `BuildSystem.krt:60` 设置、`Project.krt:397` 未设置
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：走 `KrtDriverProject` → `KrtSourcesCollectModules`（`Project.krt:397`）的项目构建。
- 后果：`KrtModulesMarkImportedAst` 在 `:708` 直接跳过，所有 `node.native_imported` 保持 false；`KrtModulesMergeImportedAst` 在 `:642-643` 因 `imports==false` 直接返回 true，导入类型不再去重、不再校验模板一致性；`KrtModulesSameModule`/`KrtModulesImportedPosition` 也退化。与单文件构建（BuildSystem 设置 active bundle）行为分叉。`CompilerPipeline.krt:543-544` 的基于 bundle 诊断同样走不到。
- 最小修复：在 `KrtSourcesCollectModules` 内统一设置/恢复 `KrtDriverActiveBundle`，或要求 Project 路径与 BuildSystem 一样先赋值。
- 备注：`grep "ActiveBundle\s*="` 仅命中 BuildSystem.krt:60/80，Project.krt 无赋值。

### [高] 84-4｜`KrtModulesReadMapped` 缺 relocation 表一致性校验
- 位置：`SelfHost/Compiler/Driver/Driver.krt:556-560`；对照 `KrtDriverAbiRead:150-155`
- 类别：4 阶段不对齐 / 2 诊断缺失
- 触发条件：KRO 头 `relocations` 字段与 32/36/40 处段大小之和不相等。
- 后果：`KrtDriverAbiRead` 会拒绝该布局，`KrtModulesReadMapped` 只检查 `relocations>4194304` 与范围，仍继续定位 `symbols/strings`，同一对象在两个阶段得到不同接受结论，模块元数据可能按错位表解析。
- 最小修复：与 `KrtDriverAbiRead` 一致，计算 `relocation_sum` 并比较 `relocations!=relocation_sum`。
- 备注：该函数也没有 `KrtDriverAbiRead` 的 `count>1048576` 之外的头字段交叉校验。

### [高] 84-5｜并行 worker 强制 `quiet=false`，quiet 语义在父子进程不一致
- 位置：`SelfHost/Compiler/Driver/Driver.krt:774`（`KrtDriverQuiet=false; task.options.quiet=false;`）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：父进程 `options.quiet=true`（如 `Project.krt:439` 项目单元硬编码 `unit.quiet=true`）或父进程已设 `KrtDriverQuiet`。
- 后果：子进程忽略 quiet，照常向日志/stderr 输出全部诊断；父进程恢复 quiet 后与子进程行为不一致，quiet 构建的输出不可控，项目构建的静默约定被破坏。
- 最小修复：worker 继承父进程 quiet 状态，不要覆盖；若必须强制输出，应在父进程统一收集并按 quiet 过滤。
- 备注：`BuildSystem.krt:34/113` 会把 `options.quiet` 写入全局。

### [高] 84-6｜`KrtModulesRebuild` 死代码，`KrtModulesImport` 在 changed 且 data==0 时短路返回
- 位置：`SelfHost/Compiler/Driver/Driver.krt:596-608`、`:615`（`return !changed||bundle.data==0||KrtModulesRebuild(bundle);`）
- 类别：5 死代码 / 6 契约破坏
- 触发条件：任何调用 `KrtModulesImport` 的流程；此时 `bundle.data` 初始恒为 0（`KrtSourcesCollectModules:1327` 在 import 之后才分配）。
- 后果：`bundle.data==0` 短路，`KrtModulesRebuild` 永不执行；同时 `changed=true` 时直接返回 true 而不合并元数据，函数契约上依赖调用方稍后重建。若未来 bundle 复用导致 `data!=0`，`:1327` 直接覆盖 `bundle.data` 而不释放旧缓冲，产生泄漏。
- 最小修复：要么 changed 时无条件重建（并正确处理 `data==0`），要么删除 `KrtModulesRebuild` 并在注释中明确由 `KrtSourcesCollectModules:1329-1340` 统一合并。
- 备注：`grep KrtModulesRebuild` 仅 `:596` 定义与 `:615` 调用。

### [中] 84-7｜命令行选项词/项目命令词与源文件名冲突
- 位置：`SelfHost/Compiler/Driver/Driver.krt:421-429`（`output`/`target`/`help`/`version` 等单词即选项）、`:466-467`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：源文件名恰为 `output`、`target`、`help`、`version`、`build`、`check`、`clean`、`new`、`run-vm` 等。
- 后果：这些词被静默当作选项或项目命令；例如 `KrtProject foo.krt clean` 中 `clean` 变成源文件，`KrtProject output` 被当作待赋值选项而报 `expected a value after option`。
- 最小修复：仅在尚未出现位置参数时识别项目命令；选项词按严格等值/前缀匹配并结合上下文判定，冲突时优先按源文件处理或显式报错。
- 备注：`--` 只关闭选项识别，`:466` 的 `!literal` 已正确排除命令词。

### [中] 84-8｜`KrtSourcesScan` 对非法 namespace 静默继续
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1134`（`if (!KrtSourcesParseName(...) || name.wildcard) { continue; }`）
- 类别：2 诊断缺失
- 触发条件：`namespace 123;`、`namespace a.*;` 或名称解析失败。
- 后果：不产生任何 `E_PARSE`/`E_IMPORT` 诊断，扫描继续，错误被推迟到后续阶段或完全吞掉，用户只看到泛化失败。
- 最小修复：失败或 wildcard 时返回 `KrtSourcesFail(file, start, "E_PARSE", ...)`。
- 备注：同分支的 wildcard 分支也被静默跳过。

### [中] 84-9｜`KrtSourcesVisit` 未检查 `KrtDriverAbsolutePath` 返回值，失败静默 false
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1286-1288`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`KrtDriverAbsolutePath` 返回 0（分配失败或符号链接解析超过 40 层，见 `PlatformAbstraction.krt:240-268`）。
- 后果：`KrtSourcesFindFile(bundle,0)` 返回 null，直接 `return false`，无 `E_MEMORY`/`E_INPUT` 诊断，上层只能报泛化错误。
- 最小修复：`normalized==0` 时 `KrtSourcesFail(file, directive.position, "E_MEMORY", ...)` 后返回 false。
- 备注：同类未检查返回值的还有 `KrtSourcesAtDirectory` 中 `KrtSourcesAppendExtension` 的 0 值（`:1249`）。

### [中] 84-10｜`KrtSourcesLoad` 失败路径泄漏输入缓冲与 file 对象
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1185`、`:1190`、`:1193`、`:1196`
- 类别：10 资源与生命周期
- 触发条件：源文件读取失败、累计超过 16 MiB、非法 UTF-8、`transformed` 分配失败。
- 后果：`input.data` 与 `file` 未释放即返回 null；叠加 `KrtDriverReadFile` 自身在容量超过 32 MiB 或分配失败路径也不释放已分配缓冲（`PlatformAbstraction.krt:151-153`），单次失败即可泄漏数十 MiB。
- 最小修复：各失败分支 `delete input.data; delete file;`；并修正 `KrtDriverReadFile` 的失败释放。
- 备注：疑似（取决于 bundle 清理契约是否兜底，但当前函数返回 null 后无人持有该指针）。

### [中] 84-11｜`KrtArtifactAssembly` 缺符号表/重定位字节边界校验
- 位置：`SelfHost/Compiler/Driver/Driver.krt:305`、`:316`、`:323`、`:328`
- 类别：8 未检查前置条件
- 触发条件：`KrtKroObject.symbol_bytes`/`text_relocation_bytes` 为 null 或长度不足以覆盖 `symbol_count*32`、`text_reloc_count*16`。
- 后果：`KrtArtifactWord` 直接按 `symbol_index*32+16`、`relocation*16` 读取，越界；`text_reloc_count>0` 但 `text_relocation_bytes==null` 时直接崩溃。`:316` 甚至未先确认 `symbol_bytes` 覆盖 `function_index*32+20`。
- 最小修复：入口校验 `symbol_bytes!=null && symbol_count*32<=symbol_bytes.length`、`text_relocation_bytes!=null && text_reloc_count*16<=...`。
- 备注：疑似（构造方可能已校验，但本函数无防御）。

### [中] 84-12｜`KrtArtifactWriteIr` 对 module 数组无 NULL/长度校验
- 位置：`SelfHost/Compiler/Driver/Driver.krt:247-259`
- 类别：8 未检查前置条件
- 触发条件：`module.function_count>0` 而 `parameter_counts`/`local_counts`/`starts`/`ops`/`args`/`extra` 为 null 或长度不足。
- 后果：`:249-259` 直接索引，越界崩溃；`module.ssa==null` 且 module 不完整时无任何诊断。
- 最小修复：入口校验 module 与各数组非空、长度覆盖 `function_count`/`count`。
- 备注：疑似（取决于 `KrtNativeModule` 构造契约）。

### [中] 84-13｜`KrtDriverParallelCompile` 用 `waitpid(-1)` 可能回收外部子进程
- 位置：`SelfHost/Compiler/Driver/Driver.krt:808-812`
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：父进程存在不属于本任务链的子进程（其他 fork 来源）。
- 后果：`waitpid(-1)` 回收未知 pid 后，按 pid 找不到 task，报 `E_WORKER "unexpected compilation worker state"` 并置 `ok=false`，造成误报失败；该分支 `continue` 不减少 `active`。
- 最小修复：仅接受 task 链中 pid 集合内的回收结果，或对未知 pid 记录后继续等待。
- 备注：疑似；需确认 `KrtDriverCompile` 是否会在父进程 fork 其他子进程。

### [中] 84-14｜全局 `KrtDriverArtifactTarget`/`KrtDriverLastVm` 跨阶段共享可变状态
- 位置：`SelfHost/Compiler/Driver/Driver.krt:203-204`；读写点 `CompilerPipeline.krt:514-527`、`BuildSystem.krt:36-108`
- 类别：7 共享可变状态
- 触发条件：同一进程内多次/嵌套编译，或编译中再次触发产物选择。
- 后果：后一次编译覆盖全局 target 与上一次的 VM 缓冲；`BuildSystem` 手动保存/恢复，但 `CompilerPipeline` 只读不恢复，状态可漂移。
- 最小修复：把 target/VM 产物放入 `KrtDriverOptions` 或编译上下文参数传递。
- 备注：并行 fork 后子进程地址空间独立，单进程内才是隐患。

### [中] 84-15｜全局 `KrtDriverLastErrorCode` 成功后残留旧值
- 位置：`SelfHost/Compiler/Driver/Driver.krt:748`；赋值点 `PlatformAbstraction.krt:135`（`KrtDriverError` 无条件覆盖）
- 类别：7 共享可变状态 / 9 错误处理不一致
- 触发条件：一次编译失败后，后续成功编译未产生新错误。
- 后果：全局保留旧错误码；worker 在 `:774` 重置，父进程非并行路径不重置，任何依据该全局做延迟失败分类的调用方可能误判。
- 最小修复：编译入口统一重置，或改为返回值/上下文传递错误码。
- 备注：疑似（取决于调用方是否依赖该全局）。

### [中] 84-16｜`options.quiet` 无 CLI 入口，配置漂移
- 位置：`SelfHost/Compiler/Driver/Driver.krt:366`（字段声明）、`:401-409`（解析未设置）
- 类别：11 配置漂移 / 5 死代码
- 触发条件：用户尝试 `--quiet`；或审查者认为该字段可从命令行配置。
- 后果：`KrtDriverParseArguments` 从不设置 `options.quiet`，`KrtDriverUsage` 也不列出；实际只有 `Project.krt:439` 硬编码 `unit.quiet=true` 与 `BuildSystem.krt:34/113` 读取，CLI 配置面与生效路径不一致，且与 84-5 的 worker 覆盖叠加。
- 最小修复：增加 `--quiet` 解析，或删除该字段并统一 quiet 来源。
- 备注：`grep "\.quiet"` 仅命中 BuildSystem/Project/Driver 三处。

### [中] 84-17｜`KrtSourcesFail` 在 quiet 下静默吞掉全部源级诊断
- 位置：`SelfHost/Compiler/Driver/Driver.krt:928`（`if (KrtDriverQuiet) { return false; }`）
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`KrtDriverQuiet=true` 时任何源级失败，包括 `E_INPUT`（源文件不存在）、`E_SIZE`、`E_ENCODING`。
- 后果：调用方只拿到 false，用户看不到失败原因；与 worker 强制 quiet=false（84-5）叠加后，quiet 行为在源级与 worker 级完全相反。
- 最小修复：quiet 只抑制冗余输出，保留首条错误或写日志；至少让调用方在 quiet 下也能获得错误码。
- 备注：`KrtSourcesFail` 被 `KrtSourcesLoad`/`KrtSourcesValidate` 等广泛使用。

### [低] 84-18｜`KrtArtifactOp` 未覆盖 47/48/49/55-59/64/65，统一输出 `unknown`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:210-240`（默认分支 `:239`）
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：模块 IR 含这些 opcode。
- 后果：IR 文本静默失真为 `unknown`，不报错，排障困难。
- 最小修复：补全操作码名，或对未定义操作码显式报错。
- 备注：疑似（若这些编号是保留未用则无实际影响）。

### [低] 84-19｜无参数调用静默探测 `program.krt` 并写死 `stage1-probe.kro`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:495-497`
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：`arguments.next==null` 且当前目录存在 `program.krt`。
- 后果：无参数调用的行为依赖目录内容，输出路径硬编码 `stage1-probe.kro`，用户不可控；否则打印 usage 返回 0。
- 最小修复：改为显式 probe 子命令/选项，或至少打印将写入的路径。
- 备注：注释 `:494` 已说明是自举探针约定，属有意行为但缺诊断。

### [低] 84-20｜诊断定位依赖 `file.start` 升序，而 `KrtModulesRebuild` 不设置 `start`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:946-948`；对照 `KrtModulesRebuild:596-608`（未设 `start`）与最终构建 `:1335`（设置 `start`）
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：若 `KrtModulesRebuild` 被激活（当前因 84-6 为死代码而不可达）。
- 后果：`KrtSourcesDiagnosticDetails` 按 `file.start<=position` 选文件，若 `start` 全为 0 会选错文件，诊断位置错位。
- 最小修复：统一由最终构建设置 `start`，或让 rebuild 也设置 `start`。
- 备注：与 84-6 同源；当前流程不可达。

### [低] 84-21｜worker 子进程分支 `return false` 不可达
- 位置：`SelfHost/Compiler/Driver/Driver.krt:802`（`KrtDriverCompileWorker(pending); syscall(60,2,...); return false;`）
- 类别：5 死代码
- 触发条件：`fork` 返回 0（子进程）。
- 后果：`KrtDriverCompileWorker` 所有路径均以 `syscall(60,...)` 退出（`:771/772/778`），`return false` 永不执行；若将来 worker 改成 return，控制流语义会出错。
- 最小修复：删除 `return false`，或让 worker 返回状态码后由子进程统一退出。
- 备注：仅死代码，不影响当前正确性。

### [低] 84-22｜`KrtSourcesCollect` 包装函数无调用方且固定 `objects=null`
- 位置：`SelfHost/Compiler/Driver/Driver.krt:1344-1346`
- 类别：5 死代码 / 6 契约破坏
- 触发条件：—
- 后果：`grep "KrtSourcesCollect\("` 仅命中定义处；该包装固定 `objects=null`，一旦被启用会经 `KrtModulesImport:611` 直接返回 true，跳过 ABI 契约检查。
- 最小修复：删除该包装，或接入调用并传递真实 objects。
- 备注：与 `KrtSourcesCollectModules` 功能重复。

### [低] 84-23｜`KrtArtifactIr`/`KrtArtifactAssembly` 打开输出失败无诊断
- 位置：`SelfHost/Compiler/Driver/Driver.krt:270`、`:296`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：输出路径不可创建（权限/目录不存在）。
- 后果：直接返回 false，不产生 `E_OUTPUT` 诊断；是否补打印完全依赖调用方。
- 最小修复：失败时 `KrtDriverError("E_OUTPUT", ...)`。
- 备注：疑似（调用方 `KrtDriverCompile` 可能统一报错）。

### [低] 84-24｜`KrtDriverAbiReadBinary` 对 mmap 返回的非常规负值未拒绝
- 位置：`SelfHost/Compiler/Driver/Driver.krt:47`（`if(data<0&&data>=-4095){return false;}`）
- 类别：8 未检查前置条件
- 触发条件：`mmap` 返回 -4095 之外的负值。
- 后果：该返回值被当作有效指针写入 `buffer.data`，后续按对象解析崩溃。
- 最小修复：改为 `if (data < 0) { return false; }`。
- 备注：Linux errno 通常落在 [-4095,-1]，实际风险低。
