# 第八十批：SelfHost/Compiler/Frontend/Semantic/Types.krt（1-1113）

## 覆盖范围

- 仅审计 `SelfHost/Compiler/Frontend/Semantic/Types.krt` 第 1-1113 行（分四段读取：1-300、301-600、601-900、901-1113）。
- 覆盖函数：泛型约束（`KrtNativeConstraintValueType`/`DefaultConstructor`/`Unmanaged`/`CheckConstraints`/`EvaluateConstraints`）、虚分派（`MethodSignatureEqual`/`FindVirtualImplementation`/`PrepareDispatch`/`BindDispatchCall`）、函数值与间接调用（`AnnotateFunctionType`/`PrepareFunctionShape`/`BindFunctionAddress`/`BindIndirectCall`/`NormalizeQualifiedSyscall`）、泛型实例化与推断（`TypeBaseWidth`/`TypeEqualDepth`/`ResolveNamedType`/`CloneAst`/`InstantiateClass`/`InstantiateFunction`/`InferenceFormal`/`InferenceContains`/`InferenceProjection`/`StripSourceTypeSuffix`/`InferPattern`/`InferTypeArguments`/`SpecializeCall`）、模块准备（`PrepareModule`）、闭包探针（`ClosureProbeCopyNode`/`ClosureProbeJournal`/`ClosureProbeSyntax`）。
- 对照：`Re.KrtC/src/Compiler/Middle/Ir/IrType.c` 的 `KrtIrTypeEqual`（293-312）等；C 版与本版在「双 NULL 相等」上一致，其余为不同层（IR vs 前端 AST），无法逐条对齐处标「疑似」。
- 未整文件通读其他源文件；仅按需 grep 特定符号（`native_dispatch_slot`、`parameter_count`、`native_layout_state`、`native_globals`、`syntax_suffixes`、`KrtParserAppendDeclaration` 等）核对契约。

## 问题条目

### [高] 80-1｜泛型类实例化只把第一个方法挂进实例，`method_tail` 为死变量
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:695-702`
- 类别：5 死代码 / 6 契约破坏 / 3 语义分叉
- 触发条件：实例化含 ≥2 个方法的泛型类（`KrtNativeInstantiateClass`），例如 `class Box<T> { void A(){} void B(){} }` 被 `Box<int>` 引用。
- 后果：`instance.declarations` 只含第一个生成的方法，`method_tail = generated;` 赋值后从未使用，后续方法仅进 `module.left`。在 `KrtNativeSnapshotTypeMembers`（940 行）之前，`KrtNativeTypeMemberFirst` 回退到 `owner.declarations`（SemanticRuntime.krt:423），因此 `KrtNativePrepareDispatch`（149-205 行）对实例类型做重复虚方法签名检查、override 匹配、抽象方法实现检查时只能看到第一个方法，可能漏判重复虚槽/错误 override；`KrtNativeNormalizeQualifiedSyscall`（395 行）等直接读 `.declarations` 的路径同样只看到一个成员。
- 最小修复：把 699-700 行改为 `if (instance.declarations == null) { instance.declarations = generated; } else { method_tail.next = generated; }`，并保留 `method_tail = generated;`。
- 备注：`KrtParserAppendDeclaration` 对 Function 追加到 `module.left`，故快照后成员链可重建，影响面限于快照前窗口与直接读 `.declarations` 的调用点；但缺失的链表链接本身是确定性缺陷。

### [高] 80-2｜抽象方法实现检查用 `native_dispatch_slot == null` 误匹配非虚成员
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:199-200`（配合 `KrtNativeFindVirtualImplementation` 130-140）
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：具体类型继承含抽象方法的基类，且该抽象方法 `method.native_dispatch_slot == null`（例如基类在 145 行因 `native_generic_parameters != null` 或 `value == 4` 提前返回、未完成分派准备，或基类来自未完成 prepare 的路径）。
- 后果：`KrtNativeFindVirtualImplementation(type, null)` 会返回 `type` 中第一个 `native_dispatch_slot == null` 的成员（非虚方法默认即为 null），该成员 `native_abstract == false`，于是 200 行判定「抽象方法已实现」，具体类型缺少实现也能通过分派校验，后续可能生成调用不存在槽位的代码。
- 最小修复：进入 199 行前显式要求 `method.native_dispatch_slot != null`，否则视为未实现并置 `native_dispatch_state = 3`；或在 `KrtNativeFindVirtualImplementation` 中拒绝 `slot == null`。
- 备注：疑似。Parser.krt:1710/1873 令抽象方法 `native_virtual = true`，正常路径会在 160 行赋槽；需构造基类提前返回分派准备的场景才能触发，未能在本切片内完全确认可达性。

### [高] 80-3｜数组类型层丢弃可空性，`?` 语义随书写顺序改变
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:532-533`（另见 546-554、508-517）
- 类别：6 契约破坏（可空性约定）/ 3 语义分叉
- 触发条件：类型后缀顺序为 `?` 在 `[]` 之前（`T?[]`）。`KrtNativeApplySourceTypeSuffix` 先执行 63 号后缀置 `native_nullable = true`，随后 91 号数组层在 532 行无条件 `node.native_nullable = false`、533 行 `pointee_nullable = 0`，可空性被清空。
- 后果：`T?[]` 与 `T[]` 的类型表示完全相同，可空性信息静默丢失；而 `T[]?` 因 `?` 在最后可保留，导致仅书写顺序不同即类型不同/相同，破坏可空性契约且无诊断。
- 最小修复：数组层若要表达「元素可空」，应把 `outer` 移入元素描述（`element.native_nullable`）或移入 `pointee_nullable`；若确为语言规则，应在文档/诊断中明确并在类型相等中保持一致。
- 备注：Parser.krt:296 在语法层同样 `type.syntax_nullable = false`，两层一致，可能是有意规则；但语义层无任何注释或诊断说明，标疑似。

### [中] 80-4｜结构体布局未完成时宽度静默回退为 8
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:405-407`
- 类别：1 静默失败（默认值）/ 6 契约破坏（宽度）
- 触发条件：`KrtNativeTypeBaseWidth` 收到 `native_type.value == 2`（struct）但 `native_layout_state != 2`（0 未开始 / 1 进行中 / 3 错误，见 SemanticRuntime.krt:59、Types.krt:1962/2012）。
- 后果：405 行条件不成立，落到 407 行 `native_type != null → 8`，把未完成/布局失败的结构体当作 8 字节标量宽度参与类型相等（423 行）、推断与 stride 计算，可能产生错误匹配或错误布局。
- 最小修复：对 `value == 2 && native_layout_state != 2` 显式返回错误/触发布局，而不是回退 8；至少在非 `KrtNativeLayoutProbe` 阶段不应静默给值。
- 备注：疑似。布局通常先于绑定完成，但 1962 行会把 `native_layout_state` 置 3 并设 `module.native_error = 12`，此后仍可能继续查询宽度。

### [中] 80-5｜约束求值深度超限静默失败且使用模块级共享计数器
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:68-71`
- 类别：7 共享可变状态 / 2 诊断缺失 / 1 静默失败
- 触发条件：泛型约束嵌套深度 ≥64（`module.native_constraint_depth >= 64`），或约束类型标注内部再次触发 `KrtNativeCheckConstraints`。
- 后果：68 行直接 `return false`，不设置任何错误码或诊断（此函数内），仅由调用方 572/893 行补 `native_error = 6`；深度限制这一具体原因被掩盖。`native_constraint_depth` 挂在 module 上，属于跨调用共享可变状态，若求值路径异常中断（如后续引入提前 return/异常）会残留计数。
- 最小修复：超限时设置专用错误码并调用 `KrtDiagnostic`；深度计数改用局部递归参数传递，或在函数入口/出口成对维护并断言归零。
- 备注：正常路径 69-71 行有配对的 +/-，但 68 行的失败路径不递增也不会残留；主要问题是无诊断与共享状态。

### [中] 80-6｜约束 `op` 未识别时默认按 derives-from 处理
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:90-98`
- 类别：3 语义分叉 / 1 静默失败
- 触发条件：`constraint.op` 不是 1/2/3/4（新增约束种类或损坏的 op）。
- 后果：else 分支把任何未知 op 当作「类型派生」约束：91-97 行构造 `expected` 并做 `DerivesFrom`/`TypeEqual`，未知约束被静默解释为另一种语义，可能错误通过或错误拒绝，且无诊断。
- 最小修复：对未知 op 显式返回 false 并报告「未知约束」诊断；新增 op 时同步维护分派。
- 备注：当前 Parser 生成的 op 集合未在本切片内完整核对，标疑似；但缺省分支无防御是确定的。

### [中] 80-7｜`KrtNativePrepareDispatch` 多处失败路径无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:153`、`157`、`179`、`187`、`200`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：153 行（虚方法带 static/构造/泛型等非法组合）、157 行（重复虚签名）、179 行（override 无匹配且非接口实现）、187 行（覆写签名不一致）、200 行（抽象方法未实现）。
- 后果：这些路径只置 `type.native_dispatch_state = 3` 并 `return false`，仅 180/185 行在部分场景发 `E_ACCESS`/`E_MODIFIER`；其余场景（如 override 拼错名、签名不匹配）没有具体诊断，调用方只拿到 `output.error_start`，用户无法定位。
- 最小修复：为每条失败路径补充错误码与 `KrtDiagnostic`（如 E_OVERRIDE/E_ABSTRACT/E_DISPATCH），保持与 180/185 一致。
- 备注：157 行还会因 `KrtNativeAnnotateType` 失败而静默走同一分支，进一步掩盖真实原因。

### [中] 80-8｜`KrtNativePrepareModule` 重复调用 `KrtNativePrepareExternals`
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:903` 与 `939`
- 类别：9 错误处理不一致 / 11 配置漂移 / 10 资源与生命周期
- 触发条件：每次模块准备都会执行两次外部声明收集（903 在函数绑定前，939 在全局收尾后）。
- 后果：`KrtNativePrepareExternals` 会写 `candidate.value = -count-1`、`native_external_next`、`native_bound` 并追加到 `output.external_head`（SemanticRuntime.krt:392-395）。虽然第二次对已 `native_bound` 的声明会跳过，但两次调用之间存在状态依赖：若第一次因类型未布局而失败（389-390 置 `native_error = 13`），流程已 return；第二次属于冗余遍历，且 `output.external_count`/`external_head` 的构造分散在两处，配置来源不一致。
- 最小修复：只保留一次调用，或明确注释第二次为「布局完成后重试」并让其幂等（不重复计数、不改 `value`）。
- 备注：疑似。若第一次全部成功则第二次为空操作，属于冗余而非错误；但错误路径与计数状态耦合，建议收敛。

### [中] 80-9｜模块准备对 16384 上限与振荡静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:906`、`909`、`921`、`938`
- 类别：2 诊断缺失 / 8 未检查前置条件
- 触发条件：模块函数数/声明数超过 16384（909 行 `count < 16384` 截断，921 行 `function_node != null`），或 `while (changed)` 连续 16384 轮仍 `changed`（906 行）。
- 后果：921 行与 938 行直接 `return false`，没有错误码、没有 `KrtDiagnostic`、没有指出是上限/振荡；938 行的 `changed` 为真时同样静默失败，调用方仅得 `output.error_start`。
- 最小修复：对上限与轮次耗尽分别设置错误码并诊断（如 E_LIMIT），并报告实际数量/轮次。
- 备注：909 行截断后 921 行才报错，期间已对部分函数做了绑定，失败状态不一致。

### [中] 80-10｜`KrtNativeClosureProbeSyntax` 无深度/环保护
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1108-1113`
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：对深度较大或存在共享子树/回边的 AST 调用（递归遍历 `left/right/alternate/body/next`）。
- 后果：无递归深度上限、无访问标记，深 AST 直接栈溢出；若 AST 存在环（克隆/生成过程中可能产生）则无限递归。
- 最小修复：加入深度上限或显式栈/访问集合；超限返回并由调用方报告。
- 备注：同类函数 `KrtNativeTypeEqualDepth`（418）与 `KrtNativeInferenceContains`（741）都有 64 层守卫，此处缺失不一致。

### [中] 80-11｜闭包探针日志线性去重且无界增长
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1097-1106`
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：同一 module 反复 probe 不同声明（`KrtNativePrepareDispatch`/`PrepareFunctionShape`/`InstantiateClass` 等均调用）。
- 后果：1100-1101 行对 `state.mutations` 链表做 O(n) 线性扫描去重，整体 O(n²)；每条 mutation 持有一个 `new KrtAstNode()` 全字段快照（956-1095 行），无清理/上限，长时间编译内存持续增长。
- 最小修复：用哈希/集合判重；为快照数量设上限或按 module 生命周期统一释放。
- 备注：`module.native_closure_probe_state` 为 module 级共享可变状态，多处并发/重入读取需确认单线程约束。

### [中] 80-12｜给非泛型类型传类型实参时静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:569-571`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`Foo<int>` 中 `Foo` 解析为非泛型类型（`type.native_generic_parameters == null`），或实参绑定失败。
- 后果：571 行直接 `return false`，无错误码/诊断；调用方无法区分「类型不存在」「非泛型」「实参错误」。568 行 `FindUsingType` 失败同样静默。
- 最小修复：区分并报告 E_UNKNOWN_TYPE / E_NOT_GENERIC / E_TYPE_ARGUMENTS，至少设置 `module.native_error`。
- 备注：577 行（泛型类型在上下文外使用）也属同类静默失败。

### [中] 80-13｜`KrtNativeBindFunctionAddress` 重置 `module.native_error = 0` 掩盖既有错误
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:310`、`334`
- 类别：9 错误处理不一致 / 7 共享可变状态
- 触发条件：函数索引失效（`native_function_index == null || !ok`）时。
- 后果：先 `binding.module.native_error = 0` 再 `KrtNativeBindingError`，会清除此前已记录的错误码（如 301 行依赖的 `native_error == 10`），导致错误归因错误或丢失；两处重复同一模式。
- 最小修复：不要清零既有错误码；若确需重置，应在 `KrtNativeBindingError` 内统一处理并保留原值。
- 备注：疑似（取决于 `KrtNativeBindingError` 是否会覆盖写入）。

### [中] 80-14｜函数形状比较：闭包分支只比参数数量
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:247-252`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：`signature.native_closure_signature`/`native_closure_shape` 命中且仅参数数量相等。
- 后果：247 行只校验 `closure.native_type != null` 与 `parameter_count` 相等，随后直接复用 `closure.native_type/declarations/readonly`，不比较参数类型、`native_ref`、返回类型（返回类型仅判非空）。形状不同的闭包签名可能被判定相等，造成函数值类型混淆。
- 最小修复：复用 `KrtNativeFunctionShapesEqual` 或逐项比较参数类型与 ref。
- 备注：疑似。闭包形状通常在生成阶段已校验，但此处作为契约边界缺少防御。

### [低] 80-15｜`KrtNativeTypeEqualDepth` 对双 NULL 返回 true
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:417`
- 类别：6 契约破坏（类型表示）
- 触发条件：`KrtNativeTypeEqual(null, null)`。
- 后果：417 行 `if (left == right) return true;` 在两者同为 null 时返回 true，与「类型相等」的直觉契约相反；调用方若未先判空（如 815 行 `KrtNativeTypeEqual(state.inferred, candidate)` 理论上可能传入 null）会把「未知」当作「相等」。
- 最小修复：改为 `if (left == right) { return left != null; }`，或在文档中明确 null==null 约定。
- 备注：`Re.KrtC/.../IrType.c:294-296` 的 `KrtIrTypeEqual` 行为相同，属既有约定；因此仅低，且需与 C 版保持一致。

### [低] 80-16｜未知类型后缀 op 被当作指针层
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:546-554`
- 类别：3 语义分叉 / 1 静默失败
- 触发条件：`syntax_suffixes` 中出现非 63（nullable）、非 91（array）的 op。
- 后果：550 行 `KrtNativeApplyTypeLayer(node, suffix.op == 91)` 对任何其他 op 都按指针层处理，未知后缀静默变成指针，类型表示错误且无诊断。
- 最小修复：显式列举 42（指针）等已知 op，未知 op 返回 false 并报告。
- 备注：Parser 现仅生成 42/63/91（Parser.krt:283/287/297），但语义层无白名单。

### [低] 80-17｜位宽舍入循环存在整数溢出风险
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:409-411`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：`type.native_bits` 极大（如 > 2^28）。
- 后果：`width *= 2` 与 `width * 8` 可能溢出为负，`while (width * 8 < type.native_bits)` 条件恒真，循环无法终止（或得到错误宽度）。
- 最小修复：先做上限校验（如 `native_bits` 必须为 8 的倍数且 ≤ 1024），或用除法/移位并检测溢出。
- 备注：疑似。正常内建位宽不会触发，但自定义 `native_bits` 来源未在本切片内确认。

### [低] 80-18｜方法签名比较未校验参数数组长度
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:112-117`
- 类别：8 未检查前置条件（越界）
- 触发条件：`first.parameter_count` 与 `first.parameter_refs/parameter_type_starts` 实际长度不一致（损坏节点或构造期未分配）。
- 后果：113 行 `first.parameter_refs[index]`、116 行 `parameter_type_starts[index]` 直接索引，可能越界读取，导致错误比较或崩溃。
- 最小修复：比较前校验 `parameter_count` 不超过各数组长度（或确保不变量并断言）。
- 备注：正常路径数组按 128 分配（Parser.krt:1884-1887），但函数实例化会重建数组（339-347），需确认一致。

### [低] 80-19｜约束函数对 `native_type == null` 默认判为值类型/非托管
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:5`、`29`
- 类别：1 静默失败（默认值）/ 6 契约破坏
- 触发条件：未解析/未知类型节点（`native_type == null`）进入 `KrtNativeConstraintValueType` 或 `KrtNativeConstraintUnmanaged`。
- 后果：5 行 `type.native_type == null || ...` 返回 true，29 行经 `KrtNativeConstraintValueType` 也返回 true，即未知类型被默认当作值类型且 unmanaged，约束可能错误通过。
- 最小修复：区分「已解析为无 native_type 的合法标量」与「未解析」，后者应返回 false 并报告。
- 备注：疑似。调用方（86/89 行）通常在解析后调用，但 `EvaluateConstraints` 82 行已对 null 参数提前返回。

### [低] 80-20｜类型推断中闭包形状失败被静默跳过
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:849-852`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：实参需要闭包上下文，`KrtNativeClosureInferShape` 返回 null。
- 后果：851-852 行 `index = index + 1; continue;` 直接跳过该实参的推断，不记录原因；随后若 `state.inferred == null` 857 行返回 null，错误信息只表现为「推断失败」，无法定位是闭包形状问题。
- 最小修复：记录候选错误（如 `binding.candidate_error`）或立即返回 null 并诊断。
- 备注：跳过而非失败可能是有意（该参数不参与约束），但无任何注释。

### [低] 80-21｜`KrtNativeInstantiateClass` 克隆基类节点时连同 `next` 兄弟
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:690`
- 类别：6 契约破坏 / 10 资源与生命周期
- 触发条件：`declaration.left`（基类类型节点）的 `next` 链非空。
- 后果：`KrtNativeCloneAst(..., siblings=true)` 会递归克隆 `next` 链，可能把无关兄弟节点复制进实例，产生悬挂引用或额外节点；对比 692 行 body 也需要 siblings，但 base 的 `next` 语义不同。
- 最小修复：确认基类节点是否需要 siblings；若不需要，改为 `false`。
- 备注：疑似。需确认 `left` 上 `next` 的实际用途（类型参数/基类列表可能在 `left` 链上）。

### [低] 80-22｜探针快照共享 module/类型等可变指针
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1040`（另见 1001、1018、1051-1061）
- 类别：7 共享可变状态
- 触发条件：快照被用于后续 diff/回滚，而原节点字段被继续修改。
- 后果：`native_globals`、`native_type`、`native_generic_registry`、`native_instances`、`native_context` 等指针被快照与原节点共享；若快照用于比较，后续修改会使快照「跟着变」，失去快照意义。
- 最小修复：明确快照语义（浅拷贝 borrow）；若需隔离，对关键字段做深拷贝或记录版本号。
- 备注：函数注释（954-955 行）已声明「borrow all child and descriptor pointers」，故可能是有意；仅提示共享可变状态风险。

### [低] 80-23｜`KrtNativeTypeParser` 未校验 `start + length` 边界
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:472`
- 类别：8 未检查前置条件（越界/溢出）
- 触发条件：`start + length` 溢出或超过源缓冲区实际长度。
- 后果：`KrtLexerInit(lexer, (byte*)source, start + length)` 可能读到源外内存或得到错误长度，后续解析越界。
- 最小修复：调用前校验 `start >= 0 && length >= 0 && start + length >= start` 且不超源长。
- 备注：疑似。`source` 为 int64 指针，源长度信息未在此函数内可见。

### [低] 80-24｜`KrtNativePrepareModule` 对 `module.native_globals` 未判空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:904`、`907`
- 类别：8 未检查前置条件（NULL）
- 触发条件：`module.native_globals == null`（CompilerPipeline.krt:112 会置 null，直到 SemanticFeatures.krt:1182 才创建）。
- 后果：907 行 `state.changed = false` 解引用 null，直接崩溃。
- 最小修复：入口判空并返回 false/报告，或在上游保证创建完成后才调用 `PrepareModule`。
- 备注：疑似。正常流程由 SemanticFeatures 先初始化，但本函数无防御。

### [低] 80-25｜上限/失败路径缺少可测试的显式错误契约
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:906`、`909`、`921`、`938`
- 类别：12 测试不足
- 触发条件：需要验证 16384 上限、轮次耗尽、绑定失败时行为。
- 后果：这些路径只 `return false`，无错误码/诊断，单测很难断言具体失败原因；本切片内也没有任何断言或自测钩子。
- 最小修复：为每个失败路径定义可断言的错误码，并补充边界测试（函数数=16384、约束深度=64 等）。
- 备注：未能读取测试文件（超出允许范围），仅据本切片实现判断。
