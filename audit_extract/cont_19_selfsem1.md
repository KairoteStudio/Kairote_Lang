# 第七十六批：SelfHost/Compiler/Frontend/Semantic/Semantic.krt（1-1276）

## 覆盖范围

- 审计对象：`/sdcard/KairoteLang/SelfHost/Compiler/Frontend/Semantic/Semantic.krt` 第 1-1276 行（为补全 1270 行起始的 `KrtNativeBindConstructor` 函数头，实际读到 1280 行；全文件 2552 行，1277-2552 不在本批）。
- 读取方式：仅用 read 分段读取（1-220、221-460、461-700、701-940、941-1180、1181-1280），未整文件通读其他源文件。
- 交叉核对：仅用 grep 核对 SelfHost 内特定符号（`KrtNativeBindContextExpression`、`KrtNativeRejectNamedArguments`、`KrtNativeCommitArguments`、`KrtNativeAccessError`、`KrtNativeInlineValue`、`KrtNativeLayoutProbe`、`KrtNativeValidateControlFlow`、`op == 247` 等）。Re.KrtC 的 C 版 `SemanticAnalyzer.c` 未能定位（glob 超时、bash 沙箱不可用），因此凡涉及跨实现分叉的判断均标「疑似」。
- 本批主题：原生类型标注（`KrtNativeAnnotateType*`）、类型兼容/评分（`KrtNativeCompatibleValue`、`KrtNativeArgumentTypeScore`）、调用绑定与重载选择（`KrtNativeBindCall`）、表达式绑定（`KrtNativeBindExpression`）、语句绑定（`KrtNativeBindStatement`）、函数绑定（`KrtNativeBindFunction`）、数组字面量与构造器入口。

## 问题条目

### [高] 76-1｜指针/数组 cast 分支对 node.left 无 NULL 防护，缺失操作数时解引用空指针
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:734`、`:740`、`:742`、`:746`
- 类别：未检查前置条件（NULL）
- 触发条件：`Cast` 节点满足 `KrtNativePointerValue(node) || KrtNativePointerValue(node.left)` 或 `node.native_array`，且 `node.left == null`。`KrtNativeBindExpression` 在 `:583` 明确对 `node == null` 返回 true，因此 `cast<T*>()`、`cast<T[]>()` 这类缺操作数节点可被绑定到该分支。
- 后果：`:734` 的 `scalar.kind`（scalar 为 null）、`:740` 的 `node.left.native_fixed_length`、`:742` 的 `KrtNativeArrayElement(node.left)`、`:746` 的 `node.left.native_type` 均会空指针解引用，编译器崩溃。
- 最小修复：在进入 `node.kind == Cast && (指针/数组)` 分支前统一判 `node.left == null` 并返回 `KrtNativeBindingError(binding,node)`；或对上述四处逐一举指。
- 备注：疑似可达——取决于 parser 是否保证 Cast 必有操作数；但 `:583` 的显式空容忍使该前置条件在本文件内未被守住。

### [高] 76-2｜构造器每次绑定都克隆并写回 unit.left，重复绑定叠加克隆且污染 AST
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1117`（关联 `:1149`）
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：`unit.op == 220`（构造器）时执行 `unit.left = KrtNativeCloneAst(unit.left, unit.native_owner, false)`。同一构造器被第二次 `KrtNativeBindFunction`（模块重绑定、泛型特化、错误后重试）时，`unit.left` 已是上一轮克隆体。
- 后果：AST 被就地改写，克隆逐次嵌套/膨胀；后续阶段（错误定位、代码生成）看到的是克隆体而非原始声明；绑定失败时 `unit.native_bound=false`、`unit.native_locals`、`unit.declarations` 仍被写入（`:1149`），形成部分生效状态。
- 最小修复：把克隆体放在绑定局部变量中，不写回 `unit.left`；或在入口用 `unit.native_bound`/`unit.native_cloned` 缓存，避免重复克隆。
- 备注：构造器克隆意图（避免绑定副作用污染原 AST）本身合理，问题在写回与缺缓存。

### [高] 76-3｜syscall 回退路径不校验参数类型，且返回前不给 Call 节点设置结果类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:485-492`
- 类别：契约破坏 / 诊断缺失
- 触发条件：调用名 `syscall`、非成员调用、参数个数 1-7，且重载选择 `selected == null`（用户未定义同名函数）。
- 后果：`:488` 仅检查数组字面量是否带类型元素，对参数的实际类型（如给需要指针的 syscall 传字符串/浮点）无任何诊断；`return KrtNativeRejectNamedArguments(node.right, binding)` 后 `node.native_type`、`node.native_width` 等保持未设置状态。若 syscall 作为表达式使用（如 `int32 r = syscall(1);`），下游将读到未初始化类型。
- 最小修复：为该路径设置 Call 的结果类型（或显式声明为 void/未知并加诊断）；对参数做基本 ABI 类型校验或至少设置 `binding.error_start`。
- 备注：疑似——若 `KrtNativeRejectNamedArguments`（`:1419`）内部或调用方后续补类型，则后果降级；本文件内看不到补类型动作。

### [中] 76-4｜空类型标注 length<=0 静默成功，留下半初始化类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:88`
- 类别：静默失败 / 诊断缺失
- 触发条件：`KrtNativeAnnotateType(Core)` 收到 `length <= 0` 的类型跨度（Cast/Sizeof/DefaultValue/Variable 的 `type_start/type_length` 为空）。
- 后果：直接 `return true`，节点保留 `native_width=8`、`native_bits=0`、`native_type=null`、`native_string=false` 等默认值；`sizeof` 得到 0 存储、`cast<>` 之类被当作无类型整数处理，全程无错误码。
- 最小修复：`length <= 0` 时返回 `KrtNativeBindingError(binding,node)` 并由调用方设置模块错误码；或要求调用方在调用前判空。
- 备注：与 76-5 同源（默认宽度/位数未区分「未标注」与「int64」）。

### [中] 76-5｜var/let/auto/void 不解析类型，落入默认 width=8、bits=64，被当 int64 处理
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:120-128`
- 类别：静默失败 / 语义分叉
- 触发条件：类型名跨度为 `var`/`let`/`auto`（`:120-122` 把它们排除在 `KrtNativeResolveNamedType` 之外）或 `void`（同样不匹配任何宽度分支）。
- 后果：`width` 保持 `:106` 的初值 8，`:128` 因 `native_bits==0 && native_float==0 && !native_string && native_type==null` 写入 `native_bits=64`。于是 `cast<var>(x)`、`sizeof(var)` 被静默当作 int64；`void` 节点同时带 `native_void_pointer=true` 与 `native_bits=64`，而 `KrtNativePointerCastInteger`（`:255-259`）只检查 `native_bits>0`，不检查 `native_void_pointer`，存在把 void 当整数标量的风险。
- 最小修复：对 `var/let/auto/void` 分别显式处理（推断类型走上下文、void 走 void_pointer），不要在 `:128` 兜底写 bits；`KrtNativePointerCastInteger` 增加 `!native_void_pointer`。
- 备注：`var` 在 Variable 分支会被 `:1004` 覆盖，但 Cast/Sizeof 路径不覆盖，故仍可触发。

### [中] 76-6｜KrtNativeArgumentScore 直接索引 parameter_refs[index]，未校验 index 范围
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:302-304`
- 类别：未检查前置条件（越界）
- 触发条件：`index` 来自 `argument.native_argument_index`（`:468`），而该值由 `KrtNativeMapArguments` 写入。若映射契约被破坏（扩展方法、可变参数、复用/克隆的 AST 节点带旧 index），`index >= function_node.parameter_count` 或为负。
- 后果：`function_node.parameter_refs[index]` 越界读，读到垃圾 `bool` 作为 `reference`，导致评分错误甚至后续按错误引用方式提交参数。
- 最小修复：函数入口断言 `index >= 0 && index < function_node.parameter_count`，越界直接返回 -1 并置 `candidate_error`。
- 备注：疑似——正常路径下 `KrtNativeMapArguments` 会写入合法索引，需核对该函数契约。

### [中] 76-7｜参数签名失败只识别错误码 10，其他错误被静默丢弃
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:307-310`
- 类别：错误处理不一致 / 诊断缺失
- 触发条件：`KrtNativeSignatureType` 失败且设置 `module.native_error` 为 5（可空性）等非 10 值。
- 后果：只在 `== 10` 时记录 `candidate_error`，其余错误码被 `previous_error` 覆盖回滚，`candidate_error` 保持 0；最终 `:495` 只能报 4（未找到），真实的可空性/签名错误被吞。
- 最小修复：把 `binding.module.native_error` 非 0 的情况都记入 `candidate_error`（或至少区分 5/10），再恢复 `previous_error`。
- 备注：同模式在 `:448-452`（MapArguments 失败只认 10）重复出现。

### [中] 76-8｜函数索引缺失时把 native_error 清 0 后返回失败
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:426`、`:483`
- 类别：诊断缺失 / 静默失败
- 触发条件：`binding.module.native_function_index == null || !ok`。
- 后果：显式 `binding.module.native_error = 0` 后 `return KrtNativeBindingError(...)`。若上游按 `native_error` 判定「无错误」，此失败被当作成功；即便按返回值处理，错误类别也是 0（未知），且 `:426` 发生在参数绑定（`:420-424`）之后，错误位置语义混乱。
- 最小修复：为该失败设置明确的模块错误码（如「内部：函数索引缺失」），不要写 0；并尽早检查索引，避免先绑定实参。
- 备注：两处重复同一模式，属复制粘贴型缺陷。

### [中] 76-9｜found_mapping 为真时掩盖参数名错误，多候选失败只报最后一个
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:448-451`、`:493`
- 类别：诊断缺失（多错只报一个）
- 触发条件：多个候选重载，其中一个 `KrtNativeMapArguments` 成功（`found_mapping=true`）但未入选，另一个候选留下 `invalid_name`。
- 后果：`:493` 的 `!found_mapping && invalid_name != null` 不成立，参数名错误被吞，最终只报通用错误；`invalid_name` 也只保留最后一次赋值，无法定位全部问题。
- 最小修复：区分「映射失败原因」与「是否入选」，对每个失败候选记录错误；报告时优先报参数名错误。
- 备注：重载诊断 Common 弱点，但此处逻辑条件可证明会吞错。

### [中] 76-10｜重载歧义仅对 owner.value==3 判等价，且报错无错误码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:475-478`、`:498`
- 类别：诊断缺失 / 语义分叉
- 触发条件：两个候选同 `scope_score/file_score/score`，但 owner 不是「值==3」或方法签名不同。
- 后果：`ambiguous=true` 后 `:498` 直接 `KrtNativeBindingError`，不设置 `module.native_error`，歧义被报成通用「未找到」；另外把 `owner.value==3` 且签名相同视为等价（`equivalent`）会静默选一个，可能掩盖真实歧义。
- 最小修复：为歧义设置专门错误码并在诊断中列出候选；`equivalent` 判定需同时核对返回类型/泛型参数。
- 备注：`owner.value==3` 的语义需与 C 版对照确认，标疑似。

### [中] 76-11｜接口实现缺失/抽象实现时无错误码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:499-503`
- 类别：诊断缺失
- 触发条件：`selected.native_owner.value == 3`（对象/接口语义）且显式成员调用，`KrtNativeFindInterfaceImplementation` 返回 null 或 `implementation.native_abstract`。
- 后果：仅 `KrtNativeBindingError`，`module.native_error` 不设置，错误退化为通用失败，调用点拿不到「未实现/抽象方法」信息。
- 最小修复：设置专门错误码（如未实现接口方法）并保留 `error_start` 指向调用节点。
- 备注：与 C 版接口分派路径是否一致需对照确认，标疑似。

### [中] 76-12｜隐式 this 解析失败时直接 return false，无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:528-531`
- 类别：诊断缺失
- 触发条件：非静态实例方法被当作普通函数调用，走到 `callee.left = KrtNativeThis(binding, node.start)`，而 `KrtNativeThis` 返回 null。
- 后果：`if (callee.left == null) { return false; }` 未调用 `KrtNativeBindingError`，`binding.error_start` 可能仍为 -1，上游无法定位；与 `:439` 的 `receiver_valid`（已要求 `KrtNativeHasInstanceContext`）判断不一致，说明存在判断口径分叉。
- 最小修复：改为 `return KrtNativeBindingError(binding, node)`，并统一 `HasInstanceContext` 与 `ClosureThis` 的判定。
- 备注：疑似可达——正常路径下 `receiver_valid` 已挡住无上下文情形。

### [中] 76-13｜KrtNativeBindExpression 对 null 节点返回 true，缺失操作数被静默接受
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:583`（关联调用点 `:725`、`:750`、`:760`、`:1043`、`:1003`）
- 类别：静默失败 / 未检查前置条件
- 触发条件：任何以可能为 null 的子节点调用 `KrtNativeBindExpression` 的路径，如 Cast/Sizeof 缺操作数、Index 缺左/右、Unary 缺操作数、Assign 缺 RHS、Variable 缺初始化器。
- 后果：`:583` 直接 `return true`，把「节点缺失」与「绑定成功」等同；与 76-1 组合会进一步变成空指针解引用。调用点普遍不额外判空。
- 最小修复：区分「合法空节点」（如 DefaultValue）与「非法缺失」，对非法路径返回 `KrtNativeBindingError`；至少在所有调用点补 `node.left == null` 检查。
- 备注：`:582` 的 `op == 247` 提前返回是已绑定全局的正常路径（`op=247` 由 `SemanticFeatures.krt:1329` 写入），不要与本条混淆。

### [中] 76-14｜无实例上下文时 this(op 211) 仍静默成功并赋予 owner 类型
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:621-624`
- 类别：静默失败 / 契约破坏
- 触发条件：标识符解析不到名字（`:601` name == null），且 `node.op == 211`（this），但 `KrtNativeHasInstanceContext(binding)` 为假（如静态方法/全局函数中写 this）。
- 后果：`:623` 直接 `node.native_type = binding.unit.native_owner; return true;`，既不做实例上下文检查，也不调用 `KrtNativeCheckValueAccess`；当 owner 为 null 时节点类型为 null 仍返回成功。
- 最小修复：`:623` 前加 `KrtNativeHasInstanceContext` 判断，否则 `KrtNativeBindingError`；并补 `KrtNativeCheckValueAccess`。
- 备注：正常 parser 路径会先命中 `:598` 的 op 211/212 分支，本条针对解析顺序异常/复用节点。

### [中] 76-15｜整数字面量越界无范围检查，赋值兼容性对标量不比较位数
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:634`、`:1010`（关联 `KrtNativeCompatibleValue:914-942`）
- 类别：诊断缺失 / 静默失败
- 触发条件：`int8 x = 300;` 这类常量初始化/赋值。`:634` 只按值域设 width（0..2^31-1 → 4），不检查目标类型；`:1010` 的 `KrtNativeCompatibleValue` 对纯标量（`native_type==null`、stride 0）在 `:940` 后直接 `return true`，不比较 width/bits/float/unsigned。
- 后果：超出目标类型范围的常量被静默接受，后续代码生成按截断值处理，无任何编译期诊断。
- 最小修复：在 Variable/Assign 对已知常量值做目标类型范围检查；或让 `KrtNativeCompatibleValue` 对标量比较 width/bits/float 域（需评估对隐式转换的影响）。
- 备注：疑似——需确认 `KrtNativeBindContextExpression`（`SemanticFeatures.krt:526`）是否已在更早阶段插入转换/检查；本文件内看不到该检查。

### [中] 76-16｜字段类型解析依赖全局可变 KrtNativeLayoutProbe 与 layout_state
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:672`（符号定义 `Types.krt:1944`，保存/恢复 `Types.krt:2016-2018`）
- 类别：共享可变状态
- 触发条件：`settled = !KrtNativeLayoutProbe && type.native_layout_state == 2 && ...`。布局探测把全局 `KrtNativeLayoutProbe` 置 true 期间，所有字段访问走 `KrtNativeAnnotateType` 分支而非 `CopyType` 分支。
- 后果：全局标志与每个类型的 `native_layout_state` 共同决定字段类型来源；探测路径若异常退出未恢复（`Types.krt:2016-2018` 的保存/恢复是否覆盖所有 return 需确认），会静默改变后续所有字段解析结果，且与具体出错位置无关。
- 最小修复：把探测状态放进 binding/上下文参数传递，避免全局变量；确保所有提前 return 都恢复。
- 备注：跨文件状态，标疑似；本文件内可确认 `settled` 同时受全局与 per-type 状态影响。

### [中] 76-17｜KrtNativeCompatibleValue 对 value==null 直接返回 true，缺 RHS 赋值静默通过
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:916`（关联 `:1091`）
- 类别：静默失败 / 契约破坏
- 触发条件：`KrtNativeCompatibleValue(target, null)`；调用点 `:1091` 的赋值在 `statement.right` 为 null（缺 RHS）时命中。
- 后果：缺 RHS 的赋值被判定为兼容并返回成功；同时 `:916` 也使任何把 null 当「未知值」的调用点失去检查机会。
- 最小修复：调用点先判 `statement.right == null` 并报错；或让 `CompatibleValue` 对 null value 返回 false，由真正允许 null 的调用点显式放行。
- 备注：疑似可达——取决于 parser 是否允许缺 RHS；`:1003`/`:1056` 等调用点对 null 是刻意容忍。

### [低] 76-18｜KrtNativeCompatibleValue 中 value.kind == Null 判断为死代码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:931`
- 类别：死代码
- 触发条件：任何调用。
- 后果：`:917` 已对 `value.kind == KrtAstKind.Null` 返回，`:931` 的同一判断不可达，属复制粘贴残留，误导读者以为该分支参与逻辑。
- 最小修复：删除 `:931`。
- 备注：纯死代码，无行为影响。

### [中] 76-19｜推断类型 lambda 的「声明类型」在绑定后被 CopyType 覆盖，991-995 结果丢弃
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:991-995`、`:1004`
- 类别：注释与实现脱节 / 死代码
- 触发条件：`inferred && statement.left.kind == Lambda && statement.left.type_length > 0`，`:993` 把 `KrtNativeClosureDeclaredType` 的结果 CopyType 到 statement。
- 后果：`:1003` 绑定 lambda（非递归推断时 expected 传 null），`:1004` 又 `KrtNativeCopyType(statement, statement.left)`，把刚写入的声明类型覆盖为 lambda 自身类型；`declared` 分支实际无效，且 `:994` 的 `error_start` 检查在覆盖前发生，错误处理与类型写入脱节。
- 最小修复：明确二选一——要么用声明类型作 expected 并不再覆盖，要么删除 `:991-995`。
- 备注：疑似——若 lambda 类型与声明类型恒一致则无行为差异，但逻辑上仍是死分支。

### [中] 76-20｜delete 函数指针仅分配 4 个 slot 即返回成功，delete null/array 静默成功
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1046-1049`
- 类别：语义分叉 / 契约破坏
- 触发条件：`delete p`，p 为函数指针（`native_function_pointer != null && !array && pointer_depth==0`）；或 `delete null`、`delete 数组`、`delete 引用对象`。
- 后果：`:1047` `binding.slots += 4` 后直接 return true，无释放语义校验、无诊断；`:1049` 对 null/数组/引用对象直接成功。`4` 这个魔数与 `binding.slots` 的关系无注释，疑似占位实现。
- 最小修复：明确 delete 的合法操作数集合；对函数指针删除给出定义或诊断；魔数 4 提取为具名常量并注释。
- 备注：疑似——4 slot 可能是下游 codegen 约定，需与代码生成对照。

### [中] 76-21｜Return 仅在 unit.type_length>0 时校验，构造器等无返回类型标注时 return 值不检查
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1057`（关联 `:1052-1063`）
- 类别：诊断缺失
- 触发条件：`binding.unit.type_length == 0`（构造器 op 220、或未标注返回类型的函数），函数体内出现 `return expr;`。
- 后果：跳过 `is_void`、可空性、`CompatibleValue` 全部检查，`return 5;` 在构造器中静默通过。
- 最小修复：对 type_length==0 的单元推导其逻辑返回类型（构造器为所属类型）或明确禁止带值 return 并报错。
- 备注：疑似——构造器返回类型可能由别处补标，需核对 `unit.type_start/type_length` 在构造器上的取值。

### [中] 76-22｜数组字面量全 null / 空字面量被赋予默认元素类型，无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1235-1248`
- 类别：静默失败（默认值）
- 触发条件：调用实参中的全 null 字面量（如 `f([null, null])`）走 `:1237`（item_type 仅设 `native_width=8`，bits/type 全默认）；空字面量 `[]` 走 `:1248`（默认 int32）。
- 后果：实参类型在未提供任何类型信息时被静默定为 int64/int32，可能与形参不符而在后续以通用错误暴露，也可能碰巧匹配而隐藏用户错误；`:1235` 只对非 argument 上下文报可空错误，argument 上下文完全放行。
- 最小修复：argument 上下文的默认类型应改为「未知」并强制后续 CommitArguments 精确匹配，或在此处对全 null/空字面量给出可空性诊断。
- 备注：疑似——若 `KrtNativeCommitArguments`（`:1427`）会用形参类型回填则后果有限。

### [中] 76-23｜常量绑定失败路径不设模块错误码
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:203`、`:214`
- 类别：诊断缺失 / 错误处理不一致
- 触发条件：常量声明无初值或 `constant_depth >= 64`（`:203`）；或 `KrtIrTryEvaluateConstant` 失败（`:214`，如常量表达式含非常量操作）。
- 后果：两处均只 `return KrtNativeBindingError(binding, node)`，不设置 `module.native_error`；错误类别为 0/未知，且 `:203` 把「递归深度超限」与「缺初值」混为同一通用错误。
- 最小修复：分别设置错误码（缺初值 / 常量递归过深 / 非常量表达式），并保持 `error_start` 指向声明节点。
- 备注：与 76-7、76-8 同属「BindingError 不设错误码」模式。

### [中] 76-24｜全局 abstract/virtual/op223 函数绑定失败无诊断即返回 false
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1116`
- 类别：诊断缺失
- 触发条件：`unit.native_owner == null && (unit.native_abstract || unit.native_virtual || unit.op == 223)`，即全局作用域出现抽象/虚函数或 op 223 节点。
- 后果：直接 `return false`，不设置 `module.native_error` 也不设 `error_start`（若此前为 -1），上游只能得到「绑定失败」而无语义信息；同时该单元 `native_bound` 未被写入（保持默认），状态不一致。
- 最小修复：改为 `KrtNativeBindingError(binding,unit)` 并设置专门错误码；或先初始化 binding 再报错。
- 备注：疑似——需确认 op 223 语义与全局 abstract/virtual 是否由更前阶段拦截。

### [低] 76-25｜KrtNativeArrayLiteralHasTypedElement 命名与实现脱节
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:1195-1202`
- 类别：注释与实现脱节
- 触发条件：任何调用。
- 后果：函数名声称判断「是否有已类型化元素」，实现只判断是否存在 `kind != Null` 的元素；未类型化的标识符/表达式也会返回 true。调用点 `:338`、`:488` 据此决定精确匹配与 syscall 实参检查，语义与命名不符，易被后续维护者误用。
- 最小修复：改名为 `HasNonNullElement`，或实现真正的「元素已标注类型」判断。
- 备注：纯命名/可读性问题，当前调用点行为可能正确。

### [低] 76-26｜局部变量 index 定义后从未使用
- 位置：`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:465`、`:470`
- 类别：死代码
- 触发条件：扩展方法实参评分循环。
- 后果：`int32 index = extra;` 后循环体用 `argument.native_argument_index`（`:468`），`index` 只在 `:470` 自增，从未被读取；说明实现从「按位置索引」改为「按映射索引」时残留。
- 最小修复：删除 `index` 及其自增。
- 备注：纯死代码，无行为影响。
