# 第八十一批：SelfHost/Compiler/Frontend/Semantic/Types.krt（1114-2226）

## 覆盖范围
- 文件：`SelfHost/Compiler/Frontend/Semantic/Types.krt`，仅读取 1114-2226 行（文件共 2225 行）。
- 覆盖函数：KrtNativeClosureProbeType / Tail / Registry / Globals / Inheritance / Begin / End；KrtGenericTypeHash / KrtGenericHash / KrtGenericRegistrySlot / Grow / For / Find / Add；KrtNativeDerivesFrom / InheritanceDistance / FieldOwner / ResolveInheritanceInner / PrepareInheritance / BindBaseConstructor；KrtInheritanceStackFor / Push / Pop / KrtNativeResolveInheritance / CycleName / Diagnostic；KrtNativeAddInterface / ResolveTypeBases / Implements / InterfaceDistance / InterfaceMethodMatches / FindMatchingInterfaceMethod / DeclaresInterface / FindInterfaceImplementation / MethodImplementsInterface / ValidateInterfaces / CompatibleInterfaceContracts；KrtNativeReferenceValue / FunctionLabelsEqual / MergeCallbackLabels / BindNullCoalescing / BindNullConditional；KrtNativeSignatureState / SignatureTypeCore / SignatureType；KrtNativeAggregateKind / InlineValue / AlignStorage / LayoutError / ValueStorage / ValueAlignment / DescribeAggregate / ResolveLayout / ReserveFrame / BindAggregateFrame；KrtNativeSwitchError / SwitchStringByte / SwitchSameConstant / BindSwitch / SwitchStatementFlow / SwitchClauseFlow / SwitchOutcomes / SwitchAlwaysReturns。
- 对照：Re.KrtC 中未找到同名函数（其泛型注册表为 `Generics.c` 的名称+参数实现），无法逐行对照；凡不能确认处均标「疑似」。

## 问题条目

### [高] 81-1｜字符串 switch 常量比较把嵌入 NUL 当字符串终止符
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2116`（配合 2094-2107）
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：switch 用例标签为含嵌入 NUL 的字符串，如 `"a\0b"` 与 `"a\0c"`；或字面量含 `\0` 转义。
- 后果：`KrtNativeSwitchStringByte` 对 `\0` 返回 0，`KrtNativeSwitchSameConstant` 在 `a <= 0` 处提前判定相等，两个不同标签被误判为同一常量，合法 switch 被报「重复用例」；比较语义与运行时按长度比较字符串不一致。
- 最小修复：比较时以 `literal.start + index == literal.start + literal.length - 1`（到达收尾引号）为唯一结束条件，不要用字节值 0/-1 兼作终止符；或改用长度+逐字节比较。
- 备注：该函数同时承担转义解码，`\0` 与结束符共用 0 值是根因。

### [高] 81-2｜布局失败路径泄漏整个工作栈（4 个数组）
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1995`（泄漏点 2009、2019、2024、2026、2048、2049、2052、2067）
- 类别：10 资源与生命周期
- 触发条件：`KrtNativeResolveLayout` 任一字段注解失败、内联结构依赖状态错误、栈扩容失败、数组长度表达式绑定/常量求值失败、偏移或大小越界，走 `return KrtNativeLayoutError(...)`。
- 后果：`types/fields/offsets/alignments` 四个数组在成功路径 2072 才统一 `delete`，所有错误返回都不释放；单次失败泄漏最多 4×16384 个元素的数组，错误恢复或批量编译时持续累积。
- 最小修复：用统一出口（如 `defer` 或函数末尾集中释放）释放四个数组；或在每个 `return KrtNativeLayoutError` 前释放。
- 备注：`KrtNativeLayoutError` 自身不释放，调用方也无释放。

### [高] 81-3｜接口实现缺失时静默失败，无专项诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1681`（判定 1669）
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：具体类（`type.value==1/2` 且 `!type.native_abstract`）实现了某接口，但 `KrtNativeFindInterfaceImplementation` 返回 null 或返回抽象方法；且继承链上没有访问模式非 1 的同名方法。
- 后果：直接 `return false`，不调用 `KrtDiagnostic`；调用方只拿到通用的继承/布局失败，用户无法定位缺失的接口方法。1691-1693 注释明确要求「diagnose them before either overload selection or codegen」，实现未诊断。
- 最小修复：在 1681 前对 `requirement` 发出 E_INTERFACE（或等价码）诊断，包含接口名与方法名位置。
- 备注：仅当候选方法访问模式非 1 时才在 1675 报 E_MODIFIER，其余情况完全静默。

### [中] 81-4｜基类/接口列表非法组合静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1525`、`1529`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：重复列出同一基类/接口（1525）；struct 继承 class、类列出多个 class 基类、或先列接口再列 class 基类（1529）。
- 后果：均直接 `return false`，不发任何诊断；`KrtNativeResolveInheritanceInner` 将状态置 3，最终只有上层通用错误，无法指出具体是哪一条基类声明非法。
- 最小修复：在 1525/1529 返回前发出 E_INHERITANCE/E_BASE 诊断，位置用 `reference.start`。
- 备注：1532 行父类解析失败同样无追加诊断，但父类自身可能有诊断，未列为独立条目。

### [中] 81-5｜接口契约返回类型冲突静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1710`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：类型实现的多个接口含同名同参但返回类型不同的方法（`KrtNativeMethodSignatureEqual(...,false)` 为真、`(...,true)` 为假）。
- 后果：`KrtNativeCompatibleInterfaceContracts` 返回 false，无诊断；1691-1693 注释称此类冲突应在重载选择或代码生成前诊断。
- 最小修复：在 1710 处对冲突的 `a`/`b` 方法发出诊断，位置取两者之一。
- 备注：调用方 `KrtNativeValidateInterfaces` 仅透传 false。

### [中] 81-6｜接口成员声明违规静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1646`、`1649-1651`、`1655`
- 类别：2 诊断缺失
- 触发条件：接口含字段（1646）；接口方法不是 op 223、是 static、非 abstract、带泛型参数、带 base_call 或带方法体（1649-1651）；接口内重复签名（1655）。
- 后果：全部直接 `return false`，无 KrtDiagnostic；用户只能看到上层通用失败。
- 最小修复：为字段、非法修饰、重复签名分别补诊断，位置用 `field.start`/`method.start`。
- 备注：1669 分支的 E_MODIFIER 只覆盖类侧访问修饰，不覆盖接口自身声明。

### [中] 81-7｜签名缓存键不匹配时覆盖旧缓存并泄漏其数组
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1868`（旧缓存来自 1855-1867）
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：同一 `function_node` 在不同 module/source/scope/using_scope/context/owner/parameter_count 下再次解析签名（如泛型实例化、跨模块复用、上下文变化）。
- 后果：`function_node.native_signature_cache = cache` 直接覆盖旧指针，旧 `KrtNativeSignatureCache` 及其 `parameters/parameter_starts/parameter_lengths` 三个数组无法再被释放；缓存随解析次数增长而泄漏。
- 最小修复：覆盖前 `delete` 旧缓存的三个数组与缓存对象，或让旧缓存挂入待释放链。
- 备注：`KrtNativeSignatureCache` 字段定义于 `CompilerState.krt:46-61`，确认为三个数组 + result。

### [中] 81-8｜泛型注册表达到容量上限后静默失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1337`（调用方 688、718，位于本切片外）
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：实例数增长到 `(count+1)*2 >= capacity` 且 `KrtGenericRegistryGrow` 因 `capacity > 131072` 返回 false。
- 后果：`KrtGenericRegistryAdd` 返回 false；688/718 行调用方直接 `return null`，泛型实例静默丢失，后续可能以 null 继续或报无关错误。
- 最小修复：Add 失败时向调用方传递可诊断错误（设置 `module.native_error` 并发 E_GENERIC），调用方不得静默返回 null。
- 备注：容量上限 131072 为硬编码。

### [中] 81-9｜回调标签合并可能截断参数表而 parameter_count 不变
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1764-1770`（计数赋值 1755）
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：`KrtNativeMergeCallbackLabels` 合并的两个回调形状参数个数少于 `shape.parameter_count`（左/右链表先于 original 结束）。
- 后果：`anonymous.declarations` 只重建到最短的 left/right 长度，但 `anonymous.parameter_count` 仍取 `shape.parameter_count`；后续按 parameter_count 遍历 declarations 会越读 null，或标签/参数错位。
- 最小修复：合并前校验三者参数个数一致，不一致直接返回（或同步调正 parameter_count 并报告冲突）。
- 备注：调用方 `KrtNativeBindNullCoalescing` 在指针分支做过 `KrtNativePointerEqualIgnoringOuter`，但是否覆盖参数个数无法确认，标「疑似」。

### [中] 81-10｜NullConditional 结果可空性只认指针，遗漏数组/字符串
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1842`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：`node.right` 为数组或字符串（`KrtNativeReferenceValue` 为真但 `KrtNativePointerValue` 为假）。
- 后果：`node.native_nullable` 不被置真，而 `?.` 在左值为 null 时结果可为 null；与 1786 行允许数组/字符串作为 reference value 的约定不一致，可能导致空值检查被优化掉。
- 最小修复：改为 `if (KrtNativeReferenceValue(node)) { node.native_nullable = true; }`，或显式覆盖 array/string。
- 备注：`KrtNativePointerValue` 定义于 `Semantic.krt:224`，从 1726 行将 array/string 与 pointer 并列推断其不含 array/string，标「疑似」。

### [中] 81-11｜switch 用例表达式非常量未诊断且去重被跳过
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2153`（绑定 2149）
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：case 表达式不是编译期常量（`KrtIrTryEvaluateConstant` 失败），或两个非常量表达式实际相等。
- 后果：兼容性检查通过后不报错；`KrtNativeSwitchSameConstant` 对非常量返回 false，重复用例检测被静默跳过，后续代码生成阶段才可能失败或生成错误跳转表。
- 最小修复：对 case 表达式强制常量求值，失败时发 E_CONSTANT 诊断；非常量一律报错而非仅跳过去重。
- 备注：当前代码未要求 case 常量，属于阶段间约束缺失。

### [中] 81-12｜switch 比较节点宽度硬编码，与操作数类型无关
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2157`
- 类别：6 契约破坏 / 疑似
- 触发条件：switch 目标为字符串、wide（128 位）或大于 8 位的整数。
- 后果：`comparison.native_bits = 8; native_width = 1` 固定，未使用 `saved.native_bits/native_width`；若后端按此类型生成比较，宽值/字符串比较的位宽与语义可能不符。
- 最小修复：从 `saved`（或操作数公共类型）复制 `native_bits/native_width`，字符串走专用比较路径。
- 备注：字符串比较在 `KrtNativeSwitchSameConstant` 有专门分支，但比较节点类型未体现，标「疑似」。

### [中] 81-13｜合成基类构造调用未检查 method.left 为空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1400-1403`
- 类别：8 未检查前置条件
- 触发条件：`method.op == 220` 且非 external_constructor、`type.native_base != null`，但 `method.left`（函数体块）为 null。
- 后果：1403 访问 `method.left.left` 并写 `method.left.child_count`，空指针解引用，编译器崩溃而非诊断。
- 最小修复：进入该分支前检查 `method.left != null`，否则发 E_LAYOUT/E_SYNTAX 并返回 false。
- 备注：正常构造函数应总有体块，但语法错误路径下不能保证，标「疑似」。

### [中] 81-14｜签名类型核心未检查 parameter_type_starts/lengths 为空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1877-1878`
- 类别：8 未检查前置条件
- 触发条件：`function_node.parameter_count > 0` 但 `parameter_type_starts` 或 `parameter_type_lengths` 为 null（未预备好的函数节点）。
- 后果：直接下标访问 null 数组，崩溃；且 1876 只检查 index 范围，不检查数组存在。
- 最小修复：访问前检查两个数组非空，缺失时回退到 `KrtNativeAnnotateType` 或返回 false。
- 备注：缓存路径 1887 同样依赖这些数组。

### [中] 81-15｜语义布局阶段反向调用 IR 常量求值与表达式绑定
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:2044-2049`
- 类别：4 阶段不对齐 / 疑似
- 触发条件：解析内联数组长度表达式，调用 `KrtNativeBindExpression`（前端绑定）与 `KrtIrTryEvaluateConstant`（Middle/IR）。
- 后果：前端语义阶段依赖中间端 IR；若长度表达式引用正在布局中的结构体常量，可能形成布局↔绑定↔IR 的递归或顺序倒置，且伪造的 `constants.unit` 会承载槽位/帧分配后即丢弃。
- 最小修复：把数组长度常量求值移到独立的常量折叠阶段，或提供不依赖完整 IR 的常量求值入口，并明确伪造 unit 的字段初始化。
- 备注：`KrtIrTryEvaluateConstant` 位于 `Middle/Ir/Ir.krt`，阶段倒置可确认，递归风险标「疑似」。

### [中] 81-16｜类型参数（value==4）带约束时直接判继承失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1383`
- 类别：3 语义分叉 / 2 诊断缺失 / 疑似
- 触发条件：`type.value == 4` 且 `type.left != null`（类型参数带约束/基类列表）。
- 后果：返回 false，既不解析约束也不发诊断；`KrtNativeResolveInheritance` 将其视为解析失败并置 `module.native_error`，受约束泛型参数无法通过继承解析。
- 最小修复：对 value==4 解析 `type.left` 中的约束并递归解析，或明确不支持时发 E_GENERIC 诊断。
- 备注：value 4 的语义由 `Types.krt:5/145`、`Semantic.krt:663/932` 等推断为类型参数，标「疑似」。

### [低] 81-17｜泛型注册表 Grow 失败路径状态不一致且泄漏旧数组
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1307`
- 类别：9 错误处理不一致 / 10 资源与生命周期 / 疑似
- 触发条件：rehash 时 `KrtGenericRegistrySlot` 返回 -1（当前实现下因装载因子 0.5 实际不可达）。
- 后果：返回 false 前 `registry.capacity` 已更新、四个新数组已分配，旧数组未 delete（1313 未执行），注册表处于半迁移状态。
- 最小修复：失败时回滚 capacity/数组指针，或先在临时结构完成 rehash 再提交。
- 备注：触发条件标「疑似」，但错误路径本身无回滚。

### [低] 81-18｜KrtNativeLayoutProbe 为静态全局共享可变状态
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1944`、`2016-2018`
- 类别：7 共享可变状态
- 触发条件：布局注解期间任意递归/重入（类型注解触发另一次布局或签名解析）。
- 后果：全局标志跨模块、跨类型共享；虽然 2016-2018 保存/恢复，但恢复值是布尔旧值，嵌套probe 内的判定会相互覆盖，且与 `module.native_closure_probe_state` 两套机制并存，易漂移。
- 最小修复：把 probe 标志放入 module/绑定上下文，或统一由 `native_closure_probe_state` 判定。
- 备注：`Semantic.krt:672` 也读取该全局量。

### [低] 81-19｜探针继承栈复制可能留下 cycle_depth>0 而 cycle_types 为空
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1173`、`1177-1180`
- 类别：8 未检查前置条件 / 疑似
- 触发条件：`original.cycle_types == null` 但 `original.cycle_depth > 0`（当前 Push 实现下不变量成立，故为潜在隐患）。
- 后果：复制体的 `cycle_types` 保持 null 而 `cycle_depth` 非 0；`KrtInheritanceDiagnostic`（1484）只检查 `cycle_depth == 0`，随后遍历 null 数组崩溃。
- 最小修复：复制时若 `cycle_types == null` 则强制 `cycle_depth = 0`，或诊断处同时检查 `cycle_types != null`。
- 备注：标「疑似」，依赖不变量。

### [低] 81-20｜探针诊断上限硬编码默认 128，与驱动配置可能漂移
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1199`
- 类别：11 配置漂移 / 疑似
- 触发条件：`state.diagnostics == null`（探针开始时无当前诊断对象）。
- 后果：新诊断对象 limit 固定 128，忽略命令行/配置中的诊断条数上限；不同入口默认值可能不一致。
- 最小修复：从驱动级默认配置读取 limit，或在无当前诊断时继承编译会话默认值。
- 备注：标「疑似」，需核对驱动默认值。

### [低] 81-21｜继承环诊断的堆字符串所有权不明，存在泄漏风险
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1487-1489`
- 类别：10 资源与生命周期 / 疑似
- 触发条件：发生继承环，走 `KrtDriverCopyString` + `KrtDriverConcat` 生成消息并传入 `KrtDiagnosticMessage`。
- 后果：`KrtErrorReportAdd`（`CompilerState.krt:277`）仅保存 `record.message` 指针，记录删除（1232）不释放字符串；`text` 与 `message` 可能泄漏。其他调用点多传字面量，故仅此处为堆串。
- 最小修复：明确消息所有权并在记录释放时释放，或让诊断接口复制字符串。
- 备注：未确认 Krt 字符串是否由运行期统一回收，标「疑似」。

### [低] 81-22｜NullConditional 未检查右值为 void
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1833-1843`
- 类别：2 诊断缺失 / 疑似
- 触发条件：`a?.b` 右侧表达式类型为 void。
- 后果：直接 `KrtNativeCopyType(node,node.right)` 并返回；是否由 `KrtNativeCheckValueAccess` 拦截无法确认，若未拦截则产生 void 类型表达式。
- 最小修复：绑定后显式检查 `KrtNativeVoidValue(node.right)` 并报错，与 `KrtNativeBindNullCoalescing` 1790 保持一致。
- 备注：标「疑似」。

### [低] 81-23｜闭包签名路径提前返回，绕过回调标签合并
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1924`（对比 1927-1936）
- 类别：13 注释与实现脱节 / 疑似
- 触发条件：`function_node.native_closure_signature != null` 或 `native_closure_shape` 为真。
- 后果：在 1924 直接 return，不执行 1930-1936 的 `KrtNativeMergeCallbackLabels`；若闭包形状需要保留参数标签，会与注释声称的「不少于原命名契约」不一致。
- 最小修复：确认闭包形状是否已含标签；若需要，在 return 前同样合并 contract 标签。
- 备注：闭包描述通常是已预备的不可变数据，可能无需合并，标「疑似」。

### [低] 81-24｜KrtNativeFieldOwner 把静态字段计入实例字段查找
- 位置：`SelfHost/Compiler/Frontend/Semantic/Types.krt:1368-1378`
- 类别：3 语义分叉 / 疑似
- 触发条件：类型含与实例字段同名的 static 字段，调用方按名查找字段所有者。
- 后果：返回静态字段所在的 type；若调用方用于实例成员偏移/访问，会把静态字段当实例字段处理。
- 最小修复：遍历字段时跳过 `field.native_static`，或提供独立的静态字段查找函数。
- 备注：未确认所有调用方是否自行过滤 static，标「疑似」。
