# 第八十九批：SelfHost Optimizer/CompilerState + stdlib Collections/Generic + stdlib Runtime/Sys

## 覆盖范围

- `SelfHost/Compiler/Middle/Ir/Optimizer.krt`（381 行，全量读完）
- `SelfHost/Compiler/Frontend/CompilerState.krt`（328 行，全量读完；第 317 行打印运行时字符串被读取工具截断到 2000 字符，仅审可见部分）
- `Re.KrtC/stdlib/Collections/Generic.krt`（13 行，全量读完）——纯接口声明，**无问题条目**
- `Re.KrtC/stdlib/Runtime/Sys.krt`（2 行，全量读完）

交叉核对仅限 grep 特定符号：`KrtNativeOp` 枚举（`SelfHost/Compiler/Middle/Ir/Ir.krt:1555-1565`）、`KrtNativeWidthCode`（`Ir.krt:1788-1791`）、`KrtNativeValueWidth`（`Ir.krt:1806-1803`）、`KrtNativeInlineCandidate` 调用点、`KrtErrorReportFlush/HasErrors/Add` 调用点、`KrtCurrentDiagnostics` 使用点、`native_error` 赋值点、`KrtNativeBindingDiagnostic` 调用点、`System.Sys`/`Runtime.Sys` 引用点、`ssa.uses`/`semantic_types` 读取点。

## 问题条目

### [高] 89-1｜`KrtNativeInlineCandidate` 先解引用后校验 `callee` 范围
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:185-186`
- 类别：8 未检查前置条件
- 触发条件：`callee < 0` 或 `callee >= module.function_count`。
- 后果：第 185 行先读 `module.function_frame_bytes[callee]`、`module.aggregate_return_sizes[callee]`，第 186 行才做范围检查；越界时读到垃圾值或崩溃，并可能据此做出错误的内联决策。
- 最小修复：把第 186 行的范围判断移到第 185 行之前，越界直接 `return false`。
- 备注：当前唯一调用点 `Optimizer.krt:214` 以 `function_index < module.function_count` 循环，暂不可达；但函数是公共 API 且自身无前置检查，属契约破坏。

### [高] 89-2｜`KrtCurrentDiagnostics` 进程级全局可变状态
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:266`（使用点 `:284`、`:293`）
- 类别：7 共享可变状态
- 触发条件：嵌套编译/探针（`SelfHost/Compiler/Frontend/Semantic/Types.krt:1198-1233` 保存-替换-删除报告）、并行编译、或 `KrtCurrentDiagnostics == null` 时调用 `KrtDiagnosticMessage`。
- 后果：诊断被写入错误的对象；`KrtDiagnosticMessage`（:284）在全局为 null 时绕过报告直接输出，跳过 limit 与去重；`KrtErrorReportHasErrors`（:293）检查的是全局而非调用方传入的报告，探针替换期间会检查错报告；无锁，线程不安全。
- 最小修复：把 `report` 作为参数沿调用链传递，删除 `static KrtCurrentDiagnostics`。
- 备注：`CompilerPipeline.krt:425-430`、`Types.krt:1198-1233` 已用手工保存/恢复规避，说明该全局已被认定为必要之恶。

### [高] 89-3｜`KrtErrorReportFlush` 不清空链表且无 null 检查
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:288-291`
- 类别：6 契约破坏 / 9 错误处理不一致
- 触发条件：重复 flush 同一 report；或 flush 之后调用 `KrtErrorReportHasErrors()`；或 `report == null`。
- 后果：`head/tail/count` 均未重置，重复 flush 会重复打印全部诊断；flush 后 `count > 0` 使 `KrtErrorReportHasErrors()`（:293）持续为真，后续阶段被误判为失败；`report == null` 时第 289 行直接空指针解引用。
- 最小修复：flush 后重置 `head=null; tail=null; count=0;`，并在函数入口加 `if (report == null) { return; }`。
- 备注：`CompilerPipeline.krt:429` 在阶段失败时 flush，随后 `:494` 用 `KrtErrorReportHasErrors()` 判定，二者组合放大该缺陷。

### [中] 89-4｜`starts` 数组硬编码 16384，优化器内无局部上界校验
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:139`、`:217`
- 类别：8 未检查前置条件 / 11 配置漂移
- 触发条件：`module.function_count > 16384`。
- 后果：`starts[function_index] = count`（:143、:225）越界写，破坏堆上相邻数组。
- 最小修复：按 `module.function_count` 分配 `starts`，并在 `KrtNativeOptimize` 入口拒绝超限模块。
- 备注：16384 是 `Ir.krt:3203` 的模块上限，但 `KrtNativeOptimize`（:286-287）只校验 `module == null` 与 level，未复用该上限；同一 magic number 在 `Ir.krt:1582-1592` 与本文件重复。

### [中] 89-5｜容量计算 `old_count * 2 + 1024` / `old_count * 4 + 1024` 整数溢出
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:137`、`:215`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：`old_count` 足够大（约 > 2^30 或 > 2^29）使乘加溢出 int32。
- 后果：`capacity` 变为负数或过小，`new int32[capacity]` 抛异常/分配失败；或容量不足后在 :153/:159/:237/:265 返回 false，但此时 `module` 尚未替换（替换在 :171/:281），调用方只能拿到通用 `E_OPTIMIZE`。
- 最小修复：用 int64 计算并先校验 `old_count` 上限，再钳制到 16777216。
- 备注：与 89-4 同源于优化器入口缺模块校验。

### [中] 89-6｜`KrtNativeInlineHelpers` 重定位无边界检查，且未做 `KrtNativeOptimizationTargets` 校验
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:273-280`（对照 `KrtNativeTailCalls:132`）
- 类别：8 未检查前置条件 / 4 阶段不对齐
- 触发条件：`module` 中存在 op 17/18/50 且 `args`/`extra` 指向 `[0, old_count)` 之外。
- 后果：`mapping[(int32)args[index]]` 越界读；`TailCalls` 先调用 `KrtNativeOptimizationTargets` 校验目标，`InlineHelpers` 完全跳过该阶段，两个优化阶段前置条件不一致。
- 最小修复：在 `KrtNativeInlineHelpers` 开头调用/复用 `KrtNativeOptimizationTargets`，并在重定位时检查映射下标 `<= old_count`。
- 备注：正常路径由 `Ir.krt:3203` 的模块校验兜底，但该校验不含分支目标范围。

### [中] 89-7｜`KrtErrorReportAdd` 达到 `limit` 时静默丢弃诊断
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:269`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`report.count >= report.limit`（默认 128，见 `Types.krt:1199`）。
- 后果：第 129 条及以后的错误被直接丢弃，用户看到的诊断集不完整，且无任何「已截断」提示；`KrtDriverLastErrorCode` 仍被设置，调用方无法区分「已记录」与「被丢弃」。
- 最小修复：增加 `dropped` 计数并在摘要中报告，或超限时报内部诊断。
- 备注：`limit` 为 0 时所有诊断都会被静默吞掉。

### [中] 89-8｜`KrtNativeBindingDiagnostic` 把 `native_error == 0` 与未知码统一降级为 `E_LOWER`
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:312`
- 类别：2 诊断缺失
- 触发条件：`Types.krt:910-914` 对未绑定函数先 `module.native_error = 0` 再调用本函数；或 binding 写入 17 及以上的新错误码。
- 后果：未绑定函数被报成通用 `E_LOWER`，丢失具体原因；后续新增错误码若忘记在此映射，同样静默降级，排查困难。
- 最小修复：为 0（未绑定）定义专属诊断码，未知码走显式 `E_INTERNAL`/`E_UNKNOWN` 而非 `E_LOWER`。
- 备注：已核对 1-16 的码长与字符串长度一致，无错配。

### [中] 89-9｜`KrtNativeModule.data` 与 `data_storage` 双份所有权，注释与实现矛盾
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:199-209`
- 类别：10 资源与生命周期 / 13 注释与实现脱节
- 触发条件：后端按注释「Backends may use this field instead of data」在 `int64 data` 与 `byte[] data_storage` 之间选择。
- 后果：同一数据存储有两条拥有路径，`data` 与 `data_storage` 可漂移；注释称「不需要 unsafe 指针字段」却仍保留 `int64 data` 这一指针尺寸字段，`data_length` 与 `data_storage` 长度也无一致性约束。
- 最小修复：统一为 `data_storage`（或明确 `data` 只是别名并在写入处同步），并修正注释。
- 备注：`CompilerPipeline.krt` 中多处 `delete` 数组，双份所有权还带来重复释放/漏释风险。

### [中] 89-10｜打印运行时 `__KrtPrintLength` 无界扫描、`__KrtPrintAllocate` 未校验 size
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:317`（`KrtPrintRuntimeSource` 字符串常量内）
- 类别：8 未检查前置条件
- 触发条件：`text` 指向非 NUL 结尾的内存（`while(data[n]!=0){n++;}` 无长度上界）；`size` 为负或超大时 `new byte[size]`。
- 后果：越界读直到撞上随机零字节，或分配异常/巨量分配；该运行时代码由编译器在检测到 `print` 时注入，影响所有使用 print 的程序。
- 最小修复：`__KrtPrintLength` 增加上界参数或由调用方保证 NUL；`__KrtPrintAllocate` 校验 `size >= 0` 并设上限。
- 备注：该行被读取工具截断，仅审可见的 `__KrtPrintLength`/`__KrtPrintAllocate`/`__KrtPrintConcat`/`__KrtPrintUnsigned` 等部分；截断部分未审。

### [低] 89-11｜`KrtNativeOptimizationTargets` 未校验 `module.count` 即分配 `new bool[module.count + 1]`
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:56`
- 类别：8 未检查前置条件
- 触发条件：`module.count < 0`（或 `count + 1` 溢出）。
- 后果：`new bool[负数]` 抛异常，优化阶段崩溃而非返回 false。
- 最小修复：入口校验 `module != null && module.count >= 0 && module.count <= 16777216`。
- 备注：`KrtNativeOptimize:287` 只校验 `module == null` 与 level。

### [低] 89-12｜`KrtNativeFoldConstants` 中两处 `!targets[...]` 为恒真死条件
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:84`、`:97`
- 类别：5 死代码
- 触发条件：任何执行到 :84/:97 的路径。
- 后果：:81 在 `targets[index]` 时已把 `previous` 置 -1，:107 只在非目标处置 `previous`，因此 `targets[index]`、`targets[previous]` 恒为假，两个取反恒真；读者会误以为已防护跨跳转目标的常量折叠。
- 最小修复：删除这两个条件，或改为断言 `!targets[index] && !targets[previous]` 以显式表达前提。
- 备注：不影响当前正确性，属误导性死代码。

### [低] 89-13｜SSA 常量折叠 16 轮上限后静默返回成功
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:298`
- 类别：1 静默失败
- 触发条件：常量传播/死值消除需超过 16 轮才收敛（深链式表达式）。
- 后果：循环退出后 `KrtSsaFoldConstants` 仍 `return true`（:336），优化不完整且无任何标志或诊断，调用方 `KrtSsaOptimize` 认为成功。
- 最小修复：达到轮次上限时返回 false（或输出可查询的「未收敛」状态）。
- 备注：属有界不动点迭代的常见折中，但缺少可观测性。

### [低] 89-14｜`KrtSsaRemoveDeadValues` 清理元数据不一致
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:364`
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：任何死值被移除。
- 后果：只清 `ops/results/operand_counts/types/widths`，未清 `semantic_types`/`semantic_widths`/`operand_starts`/`phi_predecessors`/`value_blocks`；死值残留语义元数据，后续按 `semantic_*` 判断的逻辑（如 `Ir.krt:3630`）可能读到陈旧值。
- 最小修复：移除时一并清零 `semantic_types`/`semantic_widths`，或重建整张表。
- 备注：`Ir.krt:3630` 以 `ssa.results[value]` 为前置条件，当前恰好绕过死值，故实际影响有限。

### [低] 89-15｜`KrtPrintRuntimeRequired` 泄漏 lexer/token，且初始化失败静默返回 false
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:320-327`
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：每次编译调用；`KrtLexerInit` 失败时。
- 后果：`new KrtLexer()`/`new KrtToken()` 全程未 `delete`（对照 `CompilerPipeline.krt:346,392-393` 均显式 delete），每次编译泄漏两个对象；初始化失败时返回 false，调用方据此认为「不需要 print 运行时」，可能漏注入打印运行时。
- 最小修复：函数返回前 `delete token; delete lexer;`；用 out 参数区分「无 print」与「检测失败」。
- 备注：编译器进程长驻时泄漏会累积。

### [低] 89-16｜`KrtErrorReportAdd` 将负 `position` 静默钳为 0
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:270`
- 类别：1 静默失败
- 触发条件：调用方传入 `position < 0`（无位置上下文时常见）。
- 后果：诊断被定位到文件偏移 0，掩盖真实来源，且与去重逻辑（:273 按 position 比对）叠加后，多个不同来源的无位置诊断会被合并丢弃。
- 最小修复：保留原值或单独约定「未知位置」哨兵，不要复用 0。
- 备注：与 89-17 的去重逻辑相互作用。

### [低] 89-17｜诊断去重仅按 `position + code`，不同消息被静默合并
- 位置：`SelfHost/Compiler/Frontend/CompilerState.krt:273`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：同一 position、同一 code 但 `message` 不同（例如同一错误码在不同上下文复用时）。
- 后果：第二条及以后的诊断被直接 `return` 丢弃，用户丢失信息；比较只取 `code` 指针且忽略 `code_length`，长度不一致的同前缀码也可能误判为重复。
- 最小修复：去重键加入 `message`（或 `code_length`），或仅对完全相同的记录去重。
- 备注：与 89-16 叠加会放大丢失。

### [低] 89-18｜优化器各阶段失败统一返回 false，调用方无法区分具体阶段
- 位置：`SelfHost/Compiler/Middle/Ir/Optimizer.krt:286-292`
- 类别：2 诊断缺失 / 4 阶段不对齐
- 触发条件：`KrtNativeFoldConstants` / `KrtNativeTailCalls` / `KrtNativeInlineHelpers` 任一返回 false。
- 后果：`KrtNativeOptimize` 只把 false 透传给 `CompilerPipeline.krt:520`，后者统一报 `E_OPTIMIZE`，无法判断是常量折叠、尾调用还是内联失败，也无法区分「目标越界」「容量不足」「模块为 null」。
- 最小修复：返回阶段码或写入模块错误字段，由调用方带阶段信息报错。
- 备注：`KrtSsaOptimize` 同样只返回 false（:377），但 `E_SSA` 由调用方给出。

### [低] 89-19｜`Runtime/Sys.krt` 只有一条 `using`，不构成有效兼容层且命名空间与实现对不上
- 位置：`Re.KrtC/stdlib/Runtime/Sys.krt:2`
- 类别：5 死代码 / 11 配置漂移 / 13 注释与实现脱节
- 触发条件：任何按路径/命名空间引用该文件、期望获得 `Sys` 符号的编译。
- 后果：文件不含 `namespace`、不含任何 `public` 声明，只有一条 `using System.Sys;`；`using` 只导入不重导出，下游不会因此获得 `Sys`。真正的 `Sys` 类在 `Re.KrtC/stdlib/Sys.krt:2` 且无 `namespace` 声明（全局命名空间），而库中同时存在 `using System.Sys;` 与 `using System.Runtime.Sys;`（`Test/StandardLibrary/test_imports.py:120`）两种引用路径，注释「runtime primitives have one implementation」无任何强制力。
- 最小修复：给 `Sys` 类补 `namespace System;` 并让本文件成为真正的命名空间别名/转发，或删除该 shim 并统一引用路径。
- 备注：`libs/System/Runtime/Sys.krt` 存在同名同内容 shim，属重复源；本文件是否被构建纳入未在本次允许读取范围内核实。
