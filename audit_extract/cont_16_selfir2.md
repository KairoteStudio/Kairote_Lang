# 第七十三批：SelfHost/Compiler/Middle/Ir/Ir.krt（1921-3830）

## 覆盖范围

- 文件：`SelfHost/Compiler/Middle/Ir/Ir.krt`，实际读取 1921-3829 行（该文件共 3829 行，请求的 3830 行不存在）。
- 内容：Krt 版 IR 层后半段——表达式/语句 lowering（`KrtNativeLowerExpression`/`KrtNativeLowerStatement`/`KrtNativeLowerFunction`/`KrtNativeLowerModule`）、new/数组/foreach/switch/迭代器/空合并 lowering、全局初始化、间接调用与虚分派、运行时类型测试、异常包编码（legacy/encoded packet、descriptor/key 匹配）、SSA 构建与校验（`KrtSsaBuild`/`KrtSsaValidate`/`KrtSsaValidateDominance`）、SSA 文本输出、聚合值 lowering、宽整数 lowering。
- 交叉核对：仅按需 grep 本文件符号（`KrtNativeOp` 枚举 1555-1567、`KrtNativeLowerDeleteClosure` 296-335、`KrtNativeBoxTemporary` 5-10、`KrtNativeLowerReleaseCell` 240、`KrtNativeSpanWord`/`KrtNativeSameName` 等）。未通读其他源文件；与 Re.KrtC 的 C 版对照因目标文件定位超时未完成，相关分叉标「疑似」。

## 问题条目

### [中] 73-1｜op 245 把 `native_size` 当 scratch 槽传给 DeleteClosure
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2160`（对照 2168、296-335）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：`statement.kind == Expression && statement.op == 245`（删除临时闭包描述符）时执行到 `KrtNativeLowerDeleteClosure(module, statement.left.native_size)`。
- 后果：`KrtNativeLowerDeleteClosure` 的第 2 参是 scratch 槽号，函数体内会写 `scratch`、`scratch+1`、`scratch+2`、`scratch+3`（299-335 行）。此处传入的 `native_size` 在别处语义是字节大小（如 2366 行 `expression.native_size > 0 ? expression.native_size : 8`）。若绑定器没有把该节点字段设为保留槽号，就会按字节数当槽号去写局部帧，造成越界写或串写无关局部变量。同时该值未做 `>= 0` 与 `scratch+3 < local_count` 检查。
- 最小修复：改用与 2168 一致的保留槽（或 `KrtNativeBoxTemporary` 取 4 个连续槽），并校验 `scratch >= 0 && scratch+3 < module.local_counts[module.lower_function_index]`。
- 备注：疑似。`native_size` 在 2409-2443（foreach）、488-511（try/catch）确实被当槽号用，需绑定器确认 op 245 节点是否同样约定；但两种语义共用同一字段本身就是隐患。

### [中] 73-2｜Call 目标索引未复核，op==221 放行 -1
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2085`、`2095`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：`expression.native_target != null && expression.native_target.op == 221 && expression.native_target.value < 0`。
- 后果：2085 行显式放行 `function_index_local < 0`（仅排除 native_target 为空或非 221）；2091 行只校验 `parameter_count`；2095 行直接 `KrtNativeEmit(Call, function_index_local, ...)`，生成函数索引为 -1 的调用指令，且无任何诊断。后续 SSA/RuntimeBuild 都不会把它当非法值拦截。
- 最小修复：在 2091-2095 之间强制 `function_index_local >= 0`，否则 `module.errors++` 并返回 false。
- 备注：疑似。`op == 221` 在本文件仅此一处出现，语义由绑定器决定；若 221 表示外部符号并依赖后续重定位，则需在注释中说明并保证 -1 仅作为重定位占位。

### [中] 73-3｜lowering 失败大多不写 error_start、不累计 errors
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2349-2351`（同类：2091、2176、2207、2256、2290、2312）
- 类别：诊断缺失 / 错误处理不一致
- 触发条件：`KrtNativeLowerFunction` 或函数体 `KrtNativeLowerBlock`/`KrtNativeLowerStatement`/`KrtNativeLowerExpression` 任一步返回 false。
- 后果：`KrtNativeLowerModule` 只在 2331、2350 两处设置 `output.error_start`，且全文件只有 2085 一处 `module.errors++`。函数体 lowering 失败时 `errors` 保持 0、`error_start` 保持初值，调用方无法区分「无错误」与「lowering 失败」，诊断也没有源码位置。
- 最小修复：在 `KrtNativeLowerModule` 的失败返回前统一设置 `output.error_start`（取当前 function_node 的 start/offset）并递增 `output.errors`。
- 备注：无。

### [中] 73-4｜`new` 的 `native_size` 静默回退为 8
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2366`
- 类别：静默失败 / 未检查前置条件
- 触发条件：`KrtNativeLowerNew` 走到 `expression.left == null` 且 `expression.native_size <= 0`。
- 后果：按 8 字节分配对象，随后 2372-2373 写运行时描述符、2374-2377 调构造器，对象尺寸与实际类型不符，轻则构造器写越界，重则产生错误对象；全程无诊断。
- 最小修复：`native_size <= 0` 时返回 false 并报告（与 2368 行 `local_slot < 0` 的处理一致）。
- 备注：无。

### [中] 73-5｜局部槽上限不一致：65535 与 131072
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3784`（对照 9）
- 类别：配置漂移 / 未检查前置条件
- 触发条件：函数已用 `KrtNativeBoxTemporary` 把 `local_counts` 推到 65535 附近后，再出现 `native_bits ∈ (64,128)` 的精确宽自增（1980 行路径）。
- 后果：`KrtNativeBoxTemporary`（9 行）在 `slot >= 65535` 时返回 -1，而 `KrtNativeLowerPreciseWideIncrement`（3784 行）允许 `value_slot` 达到 131071，并把 `local_counts` 推到 131072。两处共用 `module.local_counts` 却用不同上限；若运行时/VM 的局部槽字段是 16 位，65536 以上的槽号会溢出或错址。
- 最小修复：抽出统一的局部槽上限常量，两处复用同一检查，并在 `local_counts` 增长前做上界断言。
- 备注：疑似（取决于 VM 局部槽宽；但两个上限并存本身就是漂移）。

### [低] 73-6｜不可达的宽类型分支（死代码）
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1958-1960`（同类：1938）
- 类别：死代码
- 触发条件：`expression.kind == Member || Index` 且 `KrtNativeWideKind(expression)` 为真。
- 后果：1951-1953 行已对宽表达式 `return`，执行不到 1958；因此 1959 行的 `if (KrtNativeWideKind(expression))` 恒为假，永不走 `NormalizeWide`。1938 行同理（1931-1933 已 return）。这类残留会误导维护者以为 Member/Index 的宽值需要单独归一化。
- 最小修复：删除 1959-1960 与 1938 行的死分支，或改为断言 `!KrtNativeWideKind(expression)`。
- 备注：无。

### [低] 73-7｜`ssa.uses` 只写不读
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3269`、`3435`
- 类别：死代码
- 触发条件：每次 `KrtSsaBuild`。
- 后果：3269 分配 `ssa.uses`，3435 对每个 operand 做 `ssa.uses[incoming]++`，但全文件没有任何读取点。这是纯浪费，也说明预期的消费端（如死代码消除/引用计数）缺失。
- 最小修复：删除该数组与累计循环，或接入后续 pass。
- 备注：无。

### [低] 73-8｜`KrtSsaValidate` 的结果类型检查恒为真（不可达）
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3458`（对照 3433）
- 类别：死代码 / 注释与实现脱节
- 触发条件：所有 `ssa.results[value]` 为真的值。
- 后果：`KrtSsaBuild` 在 3433 行已把每个 result 强制设为 `types=1、widths=64`，因此 3458 行的 `types<=0 || widths==0` 永不触发。该校验名义上检查结果类型，实际被 builder 的归一化完全掩盖，真实不变量（如 phi 操作数类型一致）只靠 3581 行兜。
- 最小修复：在归一化前保留原始类型用于校验，或删除该分支并改为检查 3581 行已覆盖的约束。
- 备注：无。

### [低] 73-9｜SSA 类型推断失败静默兜底为 i8
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3431`
- 类别：静默失败
- 触发条件：某个 result（含 phi）在 3413-3428 的类型传播后 `widths[value]` 仍为 0。
- 后果：3431 静默写成 `widths=8、types=1`，3432 把它快照成语义类型 `i8`，随后 3433 归一化为 i64。下游文本 IR（3630-3634）会把该值标注为 `; i8`，与真实类型不符，且无任何诊断。
- 最小修复：无法推断类型时返回 null 并报告具体值编号/操作码。
- 备注：无。

### [低] 73-10｜文本 IR 丢失 RuntimeDescriptor 类型号等参数
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:3617-3619`
- 类别：诊断缺失 / 注释与实现脱节
- 触发条件：`KrtSsaWriteIr` 输出 op 74（RuntimeDescriptor）、73（ClosureEnvironment）、22（Load）、38/39/40/43（浮点）、60-68（宽操作）、26（Syscall）等。
- 后果：3617 的「打印 args」白名单是 `1,100,17,18,19,28,29,2,3,50,70,71,72`，不含 73/74；3619 只对 18/70/71/72 打印 extra。于是 RuntimeDescriptor 的类型索引、Load 的宽度、浮点操作的格式、wide 子操作码、Syscall 参数个数等关键信息在文本 IR 中全部丢失，输出的 `.ssa.ir` 无法往返/复核。
- 最小修复：补全 73/74 的 args、22/38/39/40/43/60-68 的 extra 输出。
- 备注：无。

### [低] 73-11｜运行时描述符与异常包布局幻数硬编码
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:2710`（步长 40）、`2699-2700`（72/80）、`2925/2946/2951`（88/32/16）、`2809`（0x7FFFFFFFFFFFFFFC）、`2818`（-3）、`2855`（3）
- 类别：配置漂移
- 触发条件：运行时侧的方法表/描述符/异常包结构布局调整时。
- 后果：这些偏移、步长与位掩码直接写在 lowering 里，与运行时结构体之间没有单一事实源，也没有断言。任一侧改动都会让分派、类型匹配或异常编解码静默错址，且编译期无报错。
- 最小修复：把这些布局常量集中到与运行时共享的定义，并在 lowering 入口加布局断言。
- 备注：疑似（需与运行时头文件核对具体数值）。
