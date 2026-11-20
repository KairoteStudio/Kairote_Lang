# 第八十二批：SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt（全量）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt`，共 1466 行，已用 read 分三段（1-400、401-800、801-1466）全量读完。
- 覆盖子系统：访问检查（1-198）、装箱与转换（200-372）、闭包与捕获（374-798）、生成器（800-1035）、全局值（1037-1249）、迭代器绑定（1251-1466）。
- 交叉核对方式：仅按符号 grep / 小窗口读取其它文件（Parser.krt、Semantic.krt、Types.krt、CompilerState.krt、CompilerPipeline.krt、Ir.krt），未整文件通读；无法确认处标「疑似」。
- 本批共抽出 23 条：致命 1、高 5、中 12、低 5。

## 问题条目

### [致命] 82-1｜foreach 的 let/readonly 循环变量丢失 readonly 标志
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticFeatures.krt:1405-1419`（缺 `variable.native_readonly = original.native_readonly`）；对照 `Parser.krt:2236`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：`foreach (let T x in c) { x = ...; }`（或推断的 `let` 变量）。
- 后果：合成 variable 只复制 `syntax_modifiers`(4096)；`KrtNativeAnnotateType` 不会据此重算 `native_readonly`（Semantic.krt:78-132），`KrtNativeCopyType` 也不复制 readonly（Semantic.krt:54-76），而标识符解析会读取声明的 `native_readonly`（Semantic.krt:627-629），赋值判定的 `KrtNativeIsLvalue`（Semantic.krt:191-192）因此看到 false，循环体内写入被静默放行并生成存储指令，违反 `let` 只读契约且无任何诊断。
- 最小修复：在 1409 行后补 `variable.native_readonly = original.native_readonly;`；若走 1415 的 `KrtNativeCopyType` 推断路径，需在该调用后再次确认该位未被覆盖。
- 备注：已核对 Parser、AnnotateType、CopyType、名字解析与 IsLvalue 全部链路，非疑似。

### [高] 82-2｜KrtNativeValueCarriesAddress 对聚合类型图无环检测，可无界展开
- 位置：`SemanticFeatures.krt:402-429`（展开循环 412-426，容量倍增 416-421）
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：对含自引用或互相引用字段的值类型调用 `KrtNativeInheritStackBorrow`（Semantic.krt:684、1156、1187、2535）进入 402；典型如递归 struct。
- 后果：`pending` 反复翻倍直至内存耗尽或 `capacity*2` 溢出；编译挂起/崩溃，而不是给出 E_LAYOUT 类诊断。
- 最小修复：用已访问聚合类型集合去重（同一 `native_type` 只展开一次），并设深度上限，超限转为显式诊断。
- 备注：class 字段在 408-410 提前返回，不受影响；值类型递归本应被 layout 阶段拒绝，但该函数自身无任何防御，属纵深防御缺失。

### [高] 82-3｜base 接收器在 native_base 为空时静默产出无类型节点
- 位置：`SemanticFeatures.krt:505`（配合 497-507、509-516）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：在无基类类型（或 `native_owner`/`native_base` 缺失）中使用 `base` 成员访问。
- 后果：`receiver.native_type = binding.unit.native_owner.native_base` 得到 null，经 `KrtNativeCopyType` 复制到 this 节点；后续 `KrtNativeDerivesFrom(null,...)` 等判定静默走偏，成员访问可能被静默拒绝或按错误类型绑定，全程无 E_ 诊断。
- 最小修复：`base_receiver` 分支先检查 `binding.unit.native_owner != null && native_base != null`，否则 `KrtNativeBindingError(binding,node)` 并设置明确错误码。
- 备注：疑似，需与 Re.KrtC 版对照确认 C 版是否在语义期报「无基类」。

### [高] 82-4｜全局初始化类型不匹配失败不设错误码、不发诊断
- 位置：`SemanticFeatures.krt:1211-1213`（另见 1217）
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：非只读全局变量的初始化表达式与声明类型不兼容，或绑定失败且未产生任何诊断。
- 后果：只写 `state.output.error_start`；`module.native_error` 可能保持 0，CompilerPipeline.krt:511 落入泛化 `E_LOWER`，真实类型错误信息丢失；`binding.error_start` 的精确位置还被 `declaration.start` 覆盖。
- 最小修复：失败时设置专门错误码（新增 E_GLOBAL_INIT 或复用既有转换/参数码），并在 1211 保留 `binding.error_start >= 0 ? binding.error_start : declaration.start` 之外补位置来源标注。
- 备注：与同文件 `KrtNativeIteratorError`（1256-1258）显式设码+发诊断的做法形成直接不一致。

### [高] 82-5｜KrtNativeRegisterGlobal 失败路径无错误码且先污染全局状态
- 位置：`SemanticFeatures.krt:1121-1162`（重复定义 1131；尺寸/对齐 1148、1150；符号名 1155；上限 1152、1160；entry 插入与 context 写入 1135-1142）
- 类别：2 诊断缺失 / 9 错误处理不一致 / 10 资源与生命周期
- 触发条件：重复全局名、storage_size 越界、对齐后偏移越界、链接符号名无效、`global_count > 65536`。
- 后果：(a) 各失败分支只设 `output.error_start`，不设 `module.native_error`，上层按旧码映射，通常误报 E_LOWER；(b) entry 在校验前已插入 `state.first`、`state.changed=true`、`declaration.native_context=owner`，失败即留下半初始化表项；FinishGlobals 1247 以 `native_global-1` 索引 `global_entries`，坏项残留会负索引越界。
- 最小修复：先校验后插入；失败时设置专门错误码并用 `declaration.start`/`type_start` 定位，同时回滚已做的 `native_context` 修改。
- 备注：正常调用链失败后中止，尚未发现二次使用坏表项，故定为高而非致命。

### [高] 82-6｜生成器方法构造失败静默返回 null/false
- 位置：`SemanticFeatures.krt:987`（SignatureType 失败 `return null`）、`1033`（判空后仅 `return false`）
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：生成游标类型/方法的签名解析失败（如 Current/MoveNext 契约签名异常）。
- 后果：不设 `module.native_error`、不发 `KrtDiagnostic`，`binding.error_start` 可能停留在旧值或为 -1，编译以泛化 E_LOWER 结束且位置错误；与 `KrtNativePrepareGenerator` 其它失败统一走 `KrtNativeIteratorError` 的做法不一致。
- 最小修复：`KrtNativeGeneratorMethod` 失败时调用 `KrtNativeIteratorError(binding,plan.factory)`，或在 FinishGenerator 1033 补设错误码与位置后再返回。
- 备注：疑似点在于 `KrtNativeSignatureType` 是否已自行写输出错误；无论其是否写，此处都未设置 `binding.module` 错误码。

### [中] 82-7｜KrtNativeFindGlobal 把「二义性」折叠为 null
- 位置：`SemanticFeatures.krt:1100-1109`（1104 直接 `return null`；hidden/found 规则 1077 vs 1102）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：两个 using 导入提供同名可见全局且文件优先级相同（`priority == best` 且 `found != declaration`）。
- 后果：歧义被吞掉，调用方按「未找到」处理，最终报未定义类错误，位置与真实原因错位；两段扫描的可见性规则重复实现且条件不同，易漂移。
- 最小修复：引入可区分的「歧义」返回并给二义性诊断；抽取统一的名字可见性/优先级 helper。
- 备注：疑似，取决于语言是否允许同名 using 导入共存。

### [中] 82-8｜常量全局每次引用重复求值并回写 binding.slots
- 位置：`SemanticFeatures.krt:1040-1055`（1053 `binding.slots = constant_binding.slots`；1054 用使用点覆盖错误位置）
- 类别：7 共享可变状态 / 10 资源与生命周期
- 触发条件：同一 readonly 全局被引用多次，尤其常量链（A=B+C 之类嵌套常量）。
- 后果：每个引用点都重新 `KrtNativeBindConstant`；临时槽位随引用次数单调增长造成帧膨胀，嵌套常量重复求值呈乘法放大；子绑定的精确 `error_start` 也被使用点位置覆盖。
- 最小修复：按 declaration 缓存已求值常量的类型/值，或使用独立临时槽区间并在求值后恢复 `binding.slots`。
- 备注：与 1190-1225 全局初始化器「只绑一次」的做法形成对照。

### [中] 82-9｜闭包子绑定不继承 constant_depth
- 位置：`SemanticFeatures.krt:683-684`（对照 1048）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：lambda 体内引用常量、或触发常量递归保护路径。
- 后果：`child.constant_depth` 从默认 0 开始，与外层及全局常量求值的深度计数不一致，递归保护行为分叉，可能出现更深递归或栈开销异常。
- 最小修复：`child.constant_depth = binding.constant_depth;`。
- 备注：疑似，需确认 lambda 体内是否参与常量求值。

### [中] 82-10｜探针隔离不一致：InferShape 复用真实 unit 且捕获表修改未 Journal
- 位置：`SemanticFeatures.krt:786-789`（`probe.unit=binding.unit`、`probe.parent=binding.parent`）；`439-460`（459 直接改 `native_captures`）；`Types.krt:1097-1106`（Journal 需显式调用）
- 类别：7 共享可变状态 / 9 错误处理不一致
- 触发条件：带捕获的 lambda 走 `KrtNativeClosureInferShape`，探测期间任何路径写 `probe.unit`（例如向 `binding.unit.native_captures` 追加、写 closure_return_* 状态）。
- 后果：`KrtNativeClosureProbeEnd` 只回滚被 Journal 的节点，`native_captures` 追加未经 Journal，探测状态会泄漏进已提交 AST；与其它探针（355、549 新建 Function 节点）约定不一致。
- 最小修复：像 BoxContextArgumentScore/ClosureArgumentScore 一样创建独立 `probe.unit`；或在修改前调用 `KrtNativeClosureProbeJournal(module,binding.unit)`。
- 备注：疑似——当前 InferShape 主路径可能只读 unit，属防御缺失而非已证实的活跃泄漏。

### [中] 82-11｜KrtAstMake 返回值普遍未判空即解引用，Begin/End 检查不一致
- 位置：`SemanticFeatures.krt:355、549、652、676、686、700、743、760、909、952、961、970、984、989`；另 692 直接取 `lambda.left.kind`；Begin 返回值检查 352-353 有、547/784 无
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：AST 分配失败（内存压力/分配器返回 null），或空 lambda 体使 `lambda.left` 为 null。
- 后果：NULL 解引用崩溃，无诊断；同为探针入口，352-353 判空而 547/784 把 Begin 结果直接交给 End，错误处理策略不统一。
- 最小修复：统一安全分配辅助函数，失败走 `KrtNativeBindingError`；`KrtNativeClosureProbeBegin/End` 配对统一判空。
- 备注：疑似，取决于 KrtAstMake 的失败契约（是否会返回 null）。

### [中] 82-12｜访问检查在 bundle 缺失时两条路径结论相反
- 位置：`SemanticFeatures.krt:36-37`（SameFile 返回 true）对 `52-53`（SeparateFiles 返回 false）
- 类别：3 语义分叉 / 9 错误处理不一致 / 1 静默失败
- 触发条件：`KrtDriverActiveBundle == null` 的语义查询/测试路径。
- 后果：AllowedDepth 第 80 行用 SameFile 判定 file-private，bundle 缺失即放行；而 SeparateFiles 对同一前提一律返回 false（也即不分离）。两条路径策略相反，访问控制可静默失效或误严。
- 最小修复：统一空 bundle 策略（建议均拒绝并报诊断），抽公共 helper。
- 备注：真实驱动下 bundle 通常非空，故为中；但与 49-51 注释「未知位置永不例外」相悖。

### [中] 82-13｜扩展方法候选只取第一个，未比较接收者特异性
- 位置：`SemanticFeatures.krt:1272-1282`（1277 首个 extension 即采用）；使用点 1365、1374、1390、1397
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：同名扩展 GetEnumerator/MoveNext/Current 存在多个候选。
- 后果：静默选择模块顺序中的第一个，可能调用错误扩展方法；无二义性诊断。
- 最小修复：收集全部候选，按接收者距离/签名排序，平局时报错。
- 备注：实例方法优先于扩展（1276）本身合理，问题仅在扩展之间。

### [中] 82-14｜foreach 的 Current 字段查找不检查访问级别与隐藏
- 位置：`SemanticFeatures.krt:1284-1294`；直到 1388 才检查 static/global
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：Current 是基类私有字段、静态字段，或被派生类隐藏的同名字段。
- 后果：私有/隐藏字段可能被合成 foreach 读取，破坏封装；静态字段虽在绑定后才报错，诊断阶段滞后、位置不准。
- 最小修复：查找时过滤 `native_static`，按访问级别与 owner 可见性筛选；命中多个报二义性。
- 备注：疑似——`KrtNativeBindExpression(current)` 内部可能有访问检查，但未在本次核对中证实。

### [中] 82-15｜Dispose 仅认实例方法且缺失被静默接受
- 位置：`SemanticFeatures.krt:1396-1402`（1397 `extensions=false`）
- 类别：6 契约破坏 / 1 静默失败 / 12 测试不足
- 触发条件：枚举器把 Dispose 实现为扩展方法，或完全未提供 Dispose。
- 后果：前者不会在 finally 中释放（资源泄漏）；后者无诊断/警告，迭代器契约只被部分强制。
- 最小修复：对「应为 IDisposable 却找不到实例 Dispose」给警告；或明确模式要求并补测试。
- 备注：疑似，语言可能刻意允许可选 Dispose。

### [中] 82-16｜无目标类型的 `() => null` 推断被拒且无错误码
- 位置：`SemanticFeatures.krt:608-619`（Null 分支）、`712-714`（kind==Null 直接 BindingError）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`var f = () => null;` 之类无期望类型的 null 返回 lambda 推断。
- 后果：`shape.native_type.kind == Null` 时仅 `KrtNativeBindingError`（不设码），上层走泛化诊断甚至无诊断；615-620 对 Null→指针/可空的处理顺序脆弱，依赖多处特判。
- 最小修复：把 Null 作为可空引用/指针候选参与兼容推断；失败时设置明确错误码。
- 备注：疑似，需确认 Kairote 是否要求此类 lambda 显式注解。

### [中] 82-17｜generator 原函数体与生成 MoveNext 方法共享同一 AST 体
- 位置：`SemanticFeatures.krt:908`（`plan.body = unit.left`）、`983`（role 3 的 `method.left = plan.body`）
- 类别：7 共享可变状态 / 13 注释与实现脱节
- 触发条件：任意 generator 函数；IR 阶段对语句做 resume/状态改写，或其它阶段再次遍历 factory 体时。
- 后果：两侧互为别名，任何一侧的语句级修改都会影响另一侧；801-803 注释声称「源体只绑定一次」，但未声明所有权与禁止二次发射，仅靠约定维持。
- 最小修复：为生成方法克隆 body（`KrtNativeCloneAst`），或显式置空 `unit.left` 并断言单所有者。
- 备注：疑似——当前 IR 似乎只 lowering role 3，工厂体不再发射。

### [中] 82-18｜KrtNativeFinishGlobals 未检查 state，越界检查在状态变更之后
- 位置：`SemanticFeatures.krt:1228-1248`（1244 先 `global_count += 2` 再判 `>65536`；1247 以 `native_global-1` 索引）
- 类别：8 未检查前置条件 / 2 诊断缺失 / 9 错误处理不一致
- 触发条件：未调用 PrepareGlobals、或 guard 加入后 `global_count` 超 65536。
- 后果：state 为 null 时 NULL 解引用；超限路径已改动 `output.global_count`/`initializer_guard_slot` 却只 `return false`（无错误码/位置），留下半成品输出；坏 entry（见 82-5）残留时 1247 负索引越界。
- 最小修复：入口检查 `module.native_globals`；把上限检查移到修改之前并设置错误码与位置。
- 备注：疑似，正常流程会先 Prepare。

### [低] 82-19｜魔数与上限散落、递归深度阈值不一致（配置漂移）
- 位置：`SemanticFeatures.krt:1014`（current_offset=32）、`1220`/`1243`（异常帧 24 槽）、`1019`（131060）、`1148`/`1150`（16777216）、`1152`/`1160`（65536）、`742`（192）对比 `14`/`24`/`72`/`113`/`141`（64）；`976-979` 每方法 5×128 数组而 parameter_count 恒为 1
- 类别：11 配置漂移 / 13 注释与实现脱节
- 触发条件：ABI、帧布局或上限调整时需同步多处。
- 后果：漏改即引发布局/诊断漂移；64 与 192 两种递归上限来源无注释说明；固定 128 容量属死容量，与契约脱节。
- 最小修复：集中常量并注明推导来源，统一深度上限，按实际上限分配参数数组。
- 备注：低。

### [低] 82-20｜KrtNativeAccessError 无条件覆盖模块错误码
- 位置：`SemanticFeatures.krt:96-109`（97 置 10）
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：调用方已设置更具体的 `native_error`/`candidate_error` 后再走访问错误。
- 后果：具体错误码被冲刷为 E_ACCESS；candidate 机制（Semantic.krt:308 仅保存 code==10）进一步放大不一致，真实原因丢失。
- 最小修复：仅在 `module.native_error == 0` 时设置，或定义统一的错误码优先级。
- 备注：低。

### [低] 82-21｜ValidateAccess 诊断与错误码双轨，依赖调用方短路防重复
- 位置：`SemanticFeatures.krt:163-198`（168/174/184/192 直发 KrtDiagnostic + 196 `native_error=11` + `output.error_start`）
- 类别：9 错误处理不一致 / 12 测试不足
- 触发条件：非法修饰符；以及其它调用点（Ir.krt:2325、Types.krt:1411）之后继续执行时。
- 后果：CompilerPipeline.krt:494 用 `has_errors` 短路避免二次报错，但「直发诊断」与「设码+位置」两套约定并存，换调用点即可能重复诊断或漏报。
- 最小修复：统一为单一出口（要么只设码+位置由上层映射，要么只直发诊断）。
- 备注：低。

### [低] 82-22｜yield 探测与校验各自重复 AST 遍历规则
- 位置：`SemanticFeatures.krt:844-853`（ContainsYield 只走 left/right/alternate/body/next，Lambda 整棵跳过）对比 `857-876`（ValidateSource 特判 Try 的 right=catch 链、alternate=finally）
- 类别：13 注释与实现脱节 / 12 测试不足
- 触发条件：新增语句种类或改变子节点字段约定时。
- 后果：两处遍历规则漂移会导致 yield 漏检（函数不被识别为 generator，随后误报 E_ITERATOR）或漏校验。
- 最小修复：抽统一的「遍历全部子节点」helper，Try 的特判集中一处。
- 备注：现有 Switch/Catch 布局经核对可被覆盖，故为低。

### [低] 82-23｜KrtNativeBoxStorage 缺少 type 判空
- 位置：`SemanticFeatures.krt:262-277`（263 起直接解引用）
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：传入 null 类型（同文件 `KrtNativeBoxableValue`:238、`KrtNativeReferenceObjectValue`:232 均判空）。
- 后果：与相邻 helper 的前置条件不一致，null 时直接崩溃而非 false。
- 最小修复：入口 `if (type == null) { return 0; }` 或明确标注非空契约并让调用方保证。
- 备注：疑似，现核对了 Ir.krt:35 与 SemanticRuntime.krt:1024 两处调用均传入非 null shape。
