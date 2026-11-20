# 第七十七批：SelfHost/Compiler/Frontend/Semantic/Semantic.krt（1277-2553）

## 覆盖范围

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

## 问题条目

### [高] 77-1｜KrtNativeFindUsingType 把不可见声明作为查找结果返回
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1621`、`:1647`、`:1656`
- 类别：静默失败 / 契约破坏（访问控制）
- 触发条件：按名字查找类型时，候选声明的 `KrtNativeNameFilePriority` 为 0（file-private 位于其他文件、或 internal 位于其他模块），且不存在任何可视候选（priority>0）。该声明被写入 `hidden`，函数末尾 `return hidden`。
- 后果：本文件不可见的声明被当作“查找成功”返回。`KrtNativeExpressionTypeName`（:1680）会二次 `KrtNativeAccessAllowed`，但 `Types.krt:567`、`Types.krt:823`、`SemanticRuntime.krt:652` 直接调用 `KrtNativeFindUsingType`，绕过访问检查，形成跨文件私有/跨模块内部类型泄漏。
- 最小修复：把“找到但不可见”和“未找到”分成两个结果（返回 null 或带 visible 标志）；若确需 hidden 用于诊断，也必须在返回前调用 `KrtNativeAccessAllowed`，并让调用方无法忽略该状态。
- 备注：priority 定义见 :1533-1547；`KrtNativeAccessAllowed` 见 `SemanticFeatures.krt:92`。

### [高] 77-2｜KrtSemanticResolveCallExpression 未定义函数被静默接受
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2044-2047`
- 类别：静默失败 / 诊断缺失
- 触发条件：`foo(...)` 中 `foo` 不在 `module.left` 中，`KrtSemanticFindFunction` 返回 null；此时 `target != null && ...` 的 arity 检查被整体跳过，函数继续绑定实参并在 :2055 `return true`。
- 后果：未定义函数调用在该校验路径下静默通过。成员调用分支（:2046）只绑定接收者 `expression.left.left`，从不解析方法名，`obj.undefined()` 同样通过；而 `Namespace.foo()` 这类限定调用又会因接收者标识符未定义被误拒，校验器内部不一致。
- 最小修复：`target == null` 时返回 false（UndefinedName）；成员调用增加方法名解析或明确的命名空间判定。
- 备注：该函数属 legacy 常量对象路径（`CompilerPipeline.krt:361/366` 在 `!ok` 分支调用）；native 主路径可能另有检查，故未定致命。

### [高] 77-3｜KrtSemanticBindNestedExpression 忽略声明顺序与嵌套作用域
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2082-2088`
- 类别：语义分叉 / 静默失败
- 触发条件：嵌套块内引用“在其后声明”的变量，或引用兄弟块/内层块声明的变量。实现从 `unit.left.left` 线性扫描整个函数体，命中同名 `Variable` 即 `return true`，不判断该声明是否位于当前语句之前、是否在当前作用域。
- 后果：使用未初始化变量、越作用域引用被接受；legacy 路径（`CompilerPipeline.krt:383-386`）会据此生成 IR/KRO，产生错误代码。与 `KrtSemanticBindExpression`（:1983-1991）“只认当前语句之前声明”的规则构成同一语言两套绑定语义。
- 最小修复：改用作用域栈，只允许当前块及外层块、且仅允许已声明（位置在前）的变量；或复用 `KrtSemanticBindExpression` 的作用域规则。
- 备注：`KrtSemanticBindNestedBlock`（:2110-2126）是主要调用入口。

### [中] 77-4｜KrtSemanticValidateLoopControl 漏判 switch/try/for/foreach/dowhile 及 else-if 深链
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1905-1913`
- 类别：诊断缺失
- 触发条件：(a) 函数顶层 `switch` 内的 `break`：`Switch` 未被处理，函数直接 `return true`；(b) `else if` 链中第二层及以后的 `alternate`：:1911-1913 只递归 `statement.alternate.right`，不递归 `alternate.alternate`；(c) `For/Foreach/DoWhile/Try` 体内的非法 `break/continue` 同样不会被遍历。
- 后果：非法 `break/continue` 在该校验器下静默通过。完整实现见 `KrtNativeValidateControlFlow`（:2419-2435），两处阶段不对齐。
- 最小修复：对齐 `KrtNativeValidateControlFlow` 的语句集合；else-if 分支改为循环/递归处理整条 `alternate` 链。
- 备注：`KrtSemanticValidateFunction` 在 :1926 强制调用本函数。

### [中] 77-5｜KrtSemanticValidateBlockShape 语句白名单过窄，拒绝合法程序
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1884`（`else { return false; }`）
- 类别：阶段不对齐 / 死代码
- 触发条件：函数体含 `For`、`Foreach`、`DoWhile`、`Switch`、`Try`、`Throw`、`Point` 等语句。这些语句在原生绑定中存在（`Semantic.krt:972-974`、`:1022-1025`、`:2403-2435`），但本白名单只认 If/While/Unsafe/Delete/Break/Continue/Variable/Return/Assign/Expression。
- 后果：合法程序被 legacy 校验器判为非法，只能回退到 native 路径；若该 legacy 路径是唯一/首选路径则造成误拒。
- 最小修复：补齐语句种类，或删除该 legacy 校验器统一走 native。
- 备注：`KrtSemanticValidateFunction`（:1922-1959）同样只覆盖部分语句 kind。

### [中] 77-6｜KrtSemanticValidateFunction / ValidateCompilationUnit 存在无依据的魔数上限
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1924`（`parameter_count > 8`）、`:2211`（`child_count > 256`）
- 类别：配置漂移 / 语义分叉
- 触发条件：函数参数多于 8，或编译单元顶层函数多于 256。
- 后果：legacy 路径直接拒绝；VM 校验允许参数至多 128（`Interpreter.krt:57`），两套限制不一致。同一语言在不同路径下能力不同。
- 最小修复：统一为共享常量并注释来源，或移除 legacy 限制。
- 备注：疑似。需与 `Re.KrtC/SemanticAnalyzer.c` 对照确认 8/256 是否为语言契约。

### [中] 77-7｜KrtSemanticInlineConstantCall 返回值恒真且丢弃实参
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2299-2324`（恒 true 见 :2300/:2303/:2306/:2323；替换逻辑 :2314-2318）
- 类别：死代码 / 契约破坏 / 静默失败
- 触发条件：`main` 首语句为 `return helper(...)`，`helper` 首语句为 `return <整数>`。实现未检查 `returned.left.right == null`（无实参）、未检查 `helper_function.parameter_count == 0`，直接用 helper 的整数常量替换调用节点。
- 后果：(1) 函数声明返回 `bool` 但所有路径都返回 true，`CompilerPipeline.krt:380` 的 `&&` 契约失去意义；(2) 带实参的调用被替换为常量，实参被静默丢弃；(3) 只取 helper 首语句，后续语句被忽略。
- 最小修复：校验调用无实参且 helper 无参数；未发生内联时返回 false；只取首语句时明确校验 `child_count == 1`。
- 备注：与 `KrtSemanticValidateConstantHelperCall`（:2326-2340）的契约不对称。

### [中] 77-8｜KrtSemanticValidateConstantHelperCall 未校验 helper 参数个数
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2326-2340`
- 类别：契约破坏
- 触发条件：helper 带参数、首语句返回整数常量，`main` 为 `return helper()`。本函数只检查 `main_return.left.kind == Call` 与 helper 返回 `Integer`，不检查 `helper_function.parameter_count`。
- 后果：带参 helper 被当作“常量 helper”，与参数版 `KrtSemanticValidateConstantArgumentCall`（:2347 明确要求 `parameter_count == 1`）规则不一致，常量对象语义被破坏。
- 最小修复：增加 `helper_function.parameter_count == 0` 前置条件。
- 备注：对比行 :2347。

### [中] 77-9｜KrtSemanticIsMain 只匹配名字，不校验入口签名
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2267-2277`
- 类别：契约破坏 / 未检查前置条件
- 触发条件：名为 `main` 但带参数、带非 void 返回类型、或位于命名空间/类型作用域内的函数（只排除了 `native_owner != null`）。
- 后果：`KrtSemanticValidateEntryPoint`（:2256-2265）与 `Ir.krt:2341` 的入口选择把它当入口点，入口契约（参数个数、返回类型、全局作用域）未校验。
- 最小修复：校验 `parameter_count == 0`、返回类型与 `native_scope` 深度。
- 备注：疑似，取决于语言对 `main` 的约定；`CompilerPipeline.krt:355` 也用本函数。

### [中] 77-10｜KrtNativeBindTry 错误早退未恢复共享绑定状态
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2486`（保存/设置）、`:2490-2495`（早退）、`:2499`（成功路径恢复）
- 类别：共享可变状态 / 错误处理不一致 / 资源与生命周期
- 触发条件：catch 子句类型标注失败（:2490-2491）、`KrtNativeBindAggregateFrame` 失败（:2494）、`KrtNativeAddName` 失败（:2495）。这三处直接 `return`，未恢复 `binding.names`、`binding.depth`、`binding.catch_target`。
- 后果：错误恢复或后续诊断在被污染的 `binding` 上进行；`catch_target` 残留指向当前 try，可能让后续 `throw`（:2469-2470）绑定到错误目标。成功路径 :2499 有恢复，失败路径缺失，处理不一致。
- 最小修复：改为单一出口，或在每个失败 return 前恢复 `saved`/`saved_catch`。
- 备注：`binding.names/depth/catch_target` 均为共享可变字段。

### [中] 77-11｜KrtNativeFunctionIndexGrow 达 65536 后静默禁用索引
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1753`
- 类别：静默失败 / 配置漂移
- 触发条件：不同函数名数量增长到使容量翻倍超过 65536（负载因子 0.5，约 32768 个不同名字）。
- 后果：`index.ok = false`，`KrtNativeFunctionIndexSynchronize`（:1806）返回 false，`KrtNativeFunctionCandidates`（:1816）返回 null，所有调用解析拿不到候选，报“无候选/未定义”而非资源上限。
- 最小修复：返回明确错误码并向上报告；或允许更大容量/分级索引。
- 备注：:1751-1770。

### [中] 77-12｜KrtNativeNameFilePriority 每次调用分配临时 AST 节点且不释放
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1545-1546`
- 类别：资源与生命周期
- 触发条件：每次计算文件优先级（每次名字查找、每个候选声明）都 `KrtAstMake(KrtAstKind.Function, position, 0)`，仅用于把 `position` 传给 `KrtNativeAccessSameFile`，随后不 `delete`。
- 后果：累计内存泄漏；`KrtNativeAccessSameFile` 只读取 `caller.start`（`SemanticFeatures.krt:38-42`），该分配完全多余。
- 最小修复：改为传递位置值/栈上结构，避免分配节点。
- 备注：调用频率随候选数放大。

### [中] 77-13｜KrtNativeExceptionType 以 -1 静默表示失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2444-2451`
- 类别：静默失败 / 错误处理不一致
- 触发条件：`type` 为 void/数组/带 stride 指针，或 `type.native_type` 的声明不在 `module.declarations` 中（跨模块声明、泛型来源未展开）。
- 后果：返回 -1。调用方 `Ir.krt:2800`、`Ir.krt:2912` 直接把返回值当 identity 使用，若未校验 -1，不同非法类型会互相“相等”。
- 最小修复：区分“无 identity”与错误；调用方校验 `>= 0`。
- 备注：疑似，取决于 Ir 侧是否过滤 -1。

### [中] 77-14｜KrtNativeBindThrow 不校验异常类型、不记录类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2467-2476`
- 类别：契约破坏 / 诊断缺失
- 触发条件：`throw <expr>` 中表达式非 void/null 且 `KrtNativeBoxableValue` 为真，但并非合法异常类型（如函数指针或结构体）；或 rethrow（:2468-2470）时 `binding.catch_target` 的类型与当前 throw 无兼容性校验。
- 后果：本函数从不调用 `KrtNativeExceptionType`，也不设置 `native_target`/类型 identity；合法性完全推给下游，非法异常类型可能静默通过。
- 最小修复：调用 `KrtNativeExceptionType` 并校验返回值，记录类型 identity 供降级使用。
- 备注：与 :2443 的 helper 关系需确认。

### [中] 77-15｜KrtNativeBindTruePatterns 只处理 `&&`(104)，漏 `||`(105)
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2548-2550`
- 类别：语义分叉 / 诊断缺失
- 触发条件：`if (a is T x || ...)` 这类在 `||` 中携带绑定名的类型测试。解析器允许在 `is` 类型后取标识符（`Parser.krt:1561`），但本函数只对 `op == 104` 递归，`op == 105` 直接落到 :2551 `return true`。
- 后果：`x` 未 `KrtNativeAddName`，条件为真时引用 `x` 报未定义；若函数体不引用该名字，则模式变量被静默丢弃，无任何诊断。
- 最小修复：显式拒绝 `||` 中的绑定模式（报诊断），或按短路语义注册并限制作用域。
- 备注：`104`=`&&`、`105`=`||`（`Parser.krt:1477-1478`）。

### [中] 77-16｜KrtNativeFunctionIndexSynchronize 只同步尾部追加，结构变更即失配
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1798-1813`
- 类别：共享可变状态 / 契约破坏
- 触发条件：`module.left` 在中间插入、删除或重排函数（非尾部追加），且未调用 `KrtNativeFunctionIndexInvalidate`。同步逻辑只从 `last_indexed_function.next` 向后取新节点。
- 后果：索引与 `module.left` 不一致，查找静默漏掉函数或返回过期声明。
- 最小修复：任何结构变更都使索引失效，或为索引维护版本号/代次。
- 备注：疑似。:1796-1797 注释自称只处理 append；`Invalidate` 的调用点见 `Types.krt:950/1217`、`SemanticRuntime.krt:374`、`CompilerPipeline.krt:93`。

### [低] 77-17｜KrtNativeValidateControlFlow 的 64 层上限为无注释魔数
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:2420`、`:2430`
- 类别：配置漂移 / 静默失败
- 触发条件：嵌套 `switch`/循环深度达到 64。
- 后果：直接 `KrtNativeBindingError`，限制值无来源说明，与语言其他深度限制（如 `KrtNativeAccessAllowedDepth` 的 64）是否一致无法判断。
- 最小修复：常量化并注释，或统一到公共递归深度限制。
- 备注：低。

### [低] 77-18｜KrtNativeCommitArguments 把位置参数错误报成“参数名错误”
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1428-1429`（配合 `:1398`）
- 类别：诊断缺失 / 错误处理不一致
- 触发条件：`KrtNativeMapArguments` 因位置参数索引越界或重复而返回非 null（:1398/:1401），调用方一律走 `KrtNativeArgumentNameError`，设置 `native_error = 9`。
- 后果：位置参数问题被诊断为命名参数问题，误导定位。
- 最小修复：区分命名与位置两类错误码。
- 备注：低。

### [低] 77-19｜KrtSymbol/KrtScope/KrtScopeLookup/KrtSemanticError 为死代码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1849`（enum）、`:1851-1861`（类）、`:2358-2369`（函数）
- 类别：死代码
- 触发条件：全仓 grep 仅见定义处，无调用方（`KrtScopeLookup` 仅 :2358；`KrtSemanticError` 仅 :1849；`KrtSymbol/KrtScope` 仅被 `KrtScopeLookup` 使用）。原生绑定走 `KrtNativeName`/`KrtNativeBinding`，不使用这套符号表。
- 后果：维护负担与误导；`KrtScopeLookup` 用 `string == name` 比较，若 `==` 为引用比较还会产生正确性陷阱。
- 最小修复：删除，或接入实际符号表并补充 `symbols == null` 防护。
- 备注：低。

### [低] 77-20｜KrtNativeBindConstructor 在无候选且无实参时静默成功
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1301-1307`（所在函数 `KrtNativeBindConstructor` 始于 :1270）
- 类别：静默失败 / 默认值
- 触发条件：构造节点未匹配到任何候选（`declared == false`）、`node.parameter_count == 0`、且非聚合默认（`implicit_struct_default == false`）时，:1302 条件为假，直接 `node.native_target = selected`（null）并 `return true`。
- 后果：未知/不匹配的构造被静默接受，无诊断，下游拿到空的 `native_target`。
- 最小修复：`selected == null && !implicit_struct_default` 时一律报错，或明确记录“隐式默认构造”语义并加注释/诊断。
- 备注：疑似（需确认调用方是否已保证候选存在）；聚合默认分支（:1301）本身也属“默认值降级”。
