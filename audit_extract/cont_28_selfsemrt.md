# 第八十五批：SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt（全量）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt`，共 1072 行，已用 read 按 1-200 / 201-420 / 421-640 / 641-860 / 861-1072 五段全量读完。
- 对照范围：仅按符号 grep 核对调用点与消费方（`KrtNativeRuntimeRead`、`KrtModuleTypeSignature`、`KrtNativeTypeMemberFirst/Next`、`KrtNativeResolvePrototypes`、`KrtNativePrepareExternals`、`KrtNativeSnapshotTypeMembers`、`KrtNativeRuntimeRegister(Key)`、`signature.nominal`、`runtime_offsets` 等），未通读其他源文件。
- Re.KrtC 侧核对：在 `Re.KrtC/` 全目录 grep `KRTTYPE3|KRTABI|KRTWIDE3|_KRT1$|KRTMETHOD3|receiver_adjust|runtime_bucket` 均无命中，C 版没有与本文件对应的 ABI2 语义运行时实现，故涉及“与 C 版是否一致”的判断一律标「疑似」。
- 全局符号扫描结论：`KrtNativeRuntimeRead` 在 `KroBackend.krt:2520` 有调用（非死代码，且越界问题已在第 18 批 75-2 记录，本批不重复）；`KrtModuleTypeHash` 全仓仅有定义无调用。

## 问题条目

### [高] 85-1｜泛型方法契约编码失败被静默吞掉，损坏字节仍写入类型清单
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:722`（配套 671-690、576-581）
- 类别：1 静默失败；9 错误处理不一致
- 触发条件：`method.native_generic_parameters != null` 时调用 `KrtModuleGenericMethod`；约束位置查不到（683-684）或方法体编码失败（690）使返回的 `KrtAbiBytes.ok=false`。
- 后果：722 行把返回值直接 `KrtModuleAbiMemberInsert(methods, ...)`，从不检查 `.ok`；`KrtModuleAbiMembers`（576-581）也只读 `count/data`。最终 `layout` 内含被截断的成员契约，而 `signature.supported / contract.ok / nominal.ok` 全部保持 true。`KroBackend.krt:311-318` 的三重校验因此通过，坏 manifest 被写出，问题推迟到链接期甚至运行期才暴露。
- 最小修复：722 行检查返回值，`ok=false` 时置 `bytes.ok=false; signature.supported=false; return;`；并让 `KrtModuleAbiMembers` 拒绝 `item.contract.ok==false` 的节点。
- 备注：同一函数内 729/735 行的同类失败已正确传播 `signature.supported=false`，仅 722 遗漏，属明确实现疏漏而非设计取舍。

### [高] 85-2｜Legacy 命名把 object 类型压成单个字节，丢失类型身份并与 ABI2 编码分叉
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:257-258`（对照 110-111、263）
- 类别：3 语义分叉；6 契约破坏
- 触发条件：`KrtNativeFunctionLinkName`（326-330）在 ABI2 不可用（`supported=false` 或 `uses_struct=false`）时回落到 `KrtNativeFunctionLinkNameLegacy`；参数或返回值的 `native_type.value==5`（object，见 438 行 `"object"`）。
- 后果：legacy 分支对任何 object 只写一个 `79('O')`，不带限定名/身份；ABI2 的 `KrtAbiShape` 则写 `78('N')+identity`（含声明名）。两个仅靠 object 类型区分的重载会得到完全相同的 `_KRT1$...` 签名，`KrtNativeResolvePrototypes`（353-364）据此误判为同一函数并删除其一；导出符号亦可能撞名。
- 最小修复：legacy 的 object 分支改为输出与 263 行一致的 `67('C')+KrtLinkDeclaration`（或带身份字节），至少保证不同 object 类型签名可区分。
- 备注：疑似（Re.KrtC 无对照实现，无法确认是否为刻意保留的历史拼写；但 258 行没有任何注释说明该压缩是有意为之）。

### [中] 85-3｜成员契约 ok 状态在汇聚进 layout 时不检查
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:724-738`、`746-749`（配套 576-581）
- 类别：1 静默失败；9 错误处理不一致
- 触发条件：`KrtModuleMethodIdentity` 置 `identity.ok=false`（如 548/556 签名类型解析失败）、`KrtAbiAppend` 因无效输入置 `contract.ok=false`、或静态字段形状编码失败。
- 后果：724-725、738、748-749 均不检查 `identity.ok/contract.ok`，损坏契约照常插入 `methods/statics` 链，随后被 `KrtModuleAbiMembers` 原样写进 `layout`；`signature` 各标志仍为 true，`KroBackend.krt:311` 校验通过。
- 最小修复：插入前检查 `identity.ok && contract.ok`，失败即 `bytes.ok=false; signature.supported=false; return;`。
- 备注：与 85-1 同属“成员契约失败被吞”，但发生在普通方法/静态字段路径，位置与触发点独立，故单列。

### [中] 85-4｜宽物理边界探测的深度上限静默返回 false，与 Shape/LinkType 的报错策略相反
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:68-73`、`75-79`、`81-85`
- 类别：8 未检查前置条件；1 静默失败
- 触发条件：数组/指针/固定长度/回调形状嵌套深度超过 64（`depth>64`）。
- 后果：三个 `KrtAbiWide*` 函数一律 `return false`，调用方 137/142/550/557 据此不标记 `KRTWIDE3`，契约按 ABI2 生成；而同样的深度超限在 `KrtAbiShape:92` 是 `bytes.ok=false; signature.supported=false`、在 `KrtLinkType:224` 是 `name.valid=false`。三处对同一边界一个静默撒谎、两处显式失败，策略互相矛盾，极端嵌套下会出现契约与代码生成物理约定不一致。
- 最小修复：深度超限时向调用方回传失败（改三态返回或提供 `KrtAbiWideQuery` 输出错误），与 `KrtAbiShape` 对齐。
- 备注：64 层实际极难触发，故定中；矛盾本身是确定的。

### [中] 85-5｜开放泛型签名丢失 stride 与 pointee 空值位图，与完整 ABI 形状分叉
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:601-624`（对照 93、100、229）
- 类别：3 语义分叉；13 注释与实现脱节
- 触发条件：构造 `KRTGENERICMETHOD3`/泛型实例的“开放签名”时（601 注释自称描述类型构造器与参数位置）。
- 后果：指针/stride 统一编码为 `80('P')` 且不带 `KrtNativeSemanticStride`（对照 100 行完整编码 stride）；可空性只写顶层 `81`（603），完全不写 `native_pointee_nullable` 位图（对照 93、229）。仅 stride 或内层可空性不同的实例在该编码下字节相同，若消费方以此做实例去重/缓存键会错误合并；注释声称的完整描述与实现不符。
- 最小修复：在 606-608 补 stride 字，在 603 附近补 pointee 位图；或在注释中明确这是有意的弱化键并禁止用作实例唯一键。
- 备注：疑似（可能是有意简化的“开放签名”，但文件中无任何说明，且同名函数在 93/100/229 是完整编码）。

### [中] 85-6｜成员快照缓存无失效机制，后续挂接的方法不进 ABI 清单
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:405-428`
- 类别：7 共享可变状态；4 阶段不对齐
- 触发条件：`KrtNativeSnapshotTypeMembers`（调用点 `Types.krt:940`）执行后，后续语义阶段继续向 `module.left`/类型挂接方法（泛型特化、接口补全等）。
- 后果：`type.native_members_saved` 永久为 true，`KrtNativeTypeMemberFirst/Next`（422-428）此后只走 `native_member_first/native_member_next` 链；新挂接方法不在该链上，导致 `KrtModuleAbiLayoutPrefix`（720-741）、`KrtNativeRuntimePrepareType`（945-962）及 `Types.krt` 多处枚举集体缺项，虚表/接口表/ABI 方法表静默少项。文件中只有 `KrtNativeFunctionIndexInvalidate`（374 调用）这类针对函数索引的失效，成员快照没有对应失效入口。
- 最小修复：新增 `KrtNativeTypeMembersInvalidate(module)` 在所有 `native_members_saved=true` 的类型上复位，并在重新挂接成员后调用；或改为每次按需重建。
- 备注：疑似——需确认 `Types.krt:940` 之后确实存在追加方法的阶段；机制上的脆弱性是确定的。

### [中] 85-7｜`record.prepared` 先置位后校验，失败留下粘滞“已完成”状态
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:932-942`（失败返回点 936、939、949、960）
- 类别：7 共享可变状态；9 错误处理不一致
- 触发条件：`KrtNativeRuntimePrepareType` 中基类/接口注册失败（936、939）或虚/接口方法添加失败（949、960）。
- 后果：函数入口即 `record.prepared=true`，失败路径不回滚；本次 build 虽在 1005 行中止，但若同一 module 触发第二次 build/修复后重试，该 record 会被 933 行直接当作已准备，方法表缺项被永久忽略（静默）。
- 最小修复：仅在成功路径末尾置 `prepared=true`，或失败时复位并回滚已插入的 `record.methods`。
- 备注：与 85-8 叠加时记录状态半完成，建议一并处理。

### [中] 85-8｜`KrtNativeRuntimeMethodAdd` 先把节点挂链再校验外部索引，失败不回滚
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:911-928`
- 类别：7 共享可变状态；9 错误处理不一致
- 触发条件：`target.value < 0` 且 `-1-target.value >= module.external_count`（926）。
- 后果：`inserted` 已在 921-923 挂入 `record.methods`（并持有 `identity`），此时才 `return false`；调用方 949/960 直接向上失败，节点与 identity 字节留在记录里，既污染共享状态又泄漏。
- 最小修复：把 external 范围校验提到插入之前；失败路径删除 `inserted.identity` 并摘链。
- 备注：正常路径不会触发，但失败路径状态不一致是确定的。

### [中] 85-9｜运行时 blob 构建链全程 bool false，无错误码与定位，与同文件其他路径标准不一
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:932-966`、`993-1071`（上游短路 `Middle/Ir/Ir.krt:2356`）
- 类别：2 诊断缺失
- 触发条件：任一类型准备、key intern、重定位、reserve 失败。
- 后果：`KrtNativeRuntimeBuild` 只返回 false，不写 `error_start`/`native_error`；`Ir.krt:2356` 用 `output.errors == 0 && KrtNativeRuntimeBuild(output)` 直接短路，最终只剩通用失败。对比同文件 `KrtNativePrepareExternals`（385、388、390 设置 `output.error_start`、`module.native_error=13`）与 `CompilerPipeline.krt:480` 的 `E_LINKAGE` 诊断，标准明显不统一，用户无法定位是哪个类型/方法导致运行库构建失败。
- 最小修复：为 `KrtNativeModule` 增加错误位置/错误码字段并在各失败点填充，接入 `KrtDiagnostic`；至少区分“类型身份非法”“虚表冲突”“外部索引越界”。
- 备注：本文件多数失败是裸 false，此条以 runtime 构建链为代表，不与其他静默失败条目合并。

### [中] 85-10｜ABI 签名对象图每次重建且从不释放
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:116-180`（调用链 326-330、353-363、389；另见 `KroBackend.krt:1962`）
- 类别：10 资源与生命周期
- 触发条件：任何需要函数链接名的路径；`KrtNativeResolvePrototypes` 对每个候选做 O(n²) 次 `KrtNativeFunctionLinkName`，`KrtNativePrepareExternals` 对每个外部候选调 `KrtNativeAbiSignatureSupported`（189-191，内部再建整张签名）。
- 后果：`KrtAbiFunction` 分配的 `KrtAbiSignature`、`types` 链表、每个 `KrtAbiType` 的 `identity/layout` 字节数组、以及 126 行临时 `shape` AST 节点，在返回 `KrtLinkName` 后全部不可达且无 delete。全仓未发现 `KrtAbiSignature` 的释放函数（`CompilerPipeline.krt:289-290` 释放的是另一结构）。大模块编译内存随候选数平方级增长。
- 最小修复：新增 `KrtAbiSignatureDelete`（递归释放 types→layout/identity→data、nominal/contract、临时 shape），在所有出口调用；或为常用声明缓存签名。
- 备注：疑似 allocator 是否统一回收无法从本文件确认；但文件内大量显式 `delete`（如 37、43、878、893）表明是手动管理，泄漏成立。

### [中] 85-11｜`KrtAbiCompare` 不校验 `ok`/长度有效性，可把无效身份与既有类型合并
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:46`（关键调用 62-63；另 571、834、917）
- 类别：8 未检查前置条件；1 静默失败
- 触发条件：`KrtAbiRegister` 在 62 行拿到 `ok=false` 的身份（`KrtModuleTypeIdentity` 对 authority 长度为 0 或 >4096 的情况在 446-447 直接置 `identity.ok=false`；`KrtAbiTypeIdentity` 对 name 失效也无检查），随后 63 行仍用空/残缺 data 做排序比较。
- 后果：两个不同非导出类型在 authority 缺失时都得到空的无效身份，比较返回 0，第二个直接复用第一个的 `KrtAbiType`（63 行 `return item`），类型身份被静默错并，后续 layout/契约定到错误声明上。
- 最小修复：`KrtAbiRegister` 在 62 行后检查 `identity.ok`，无效即 `signature.supported=false; return null;`；`KrtAbiCompare` 内部对 `!left.ok||!right.ok` 返回可区分哨兵。
- 备注：疑似——authority 为 0/超长的实际可达性需结合 Driver 模块元数据确认；比较函数缺校验这一缺陷本身是确定的。

### [中] 85-12｜运行时记录头 96 字节与 receiver 调整 16 为跨文件硬编码契约
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:1014`（`Reserve(bytes,96,16)`）、`922`（`record.kind == 2 ? 16 : 0`）
- 类别：6 契约破坏；11 配置漂移
- 触发条件：任何改动运行时记录头大小/字段偏移，或对象头部布局调整。
- 后果：96 同时被 `Interpreter.krt:1271`（`length < count*96`）、`KroBackend.krt:2516`（symbol size 96）硬编码引用；receiver 调整 16 也被运行期消费方隐含依赖。本文件没有常量或构建期输出，三处漂移时编译期不报错，只在链接/运行期错位。
- 最小修复：定义单一常量（如 `KRT_RUNTIME_RECORD_SIZE`）并由构建器输出 header 布局版本；接收方从构建产物读取而非写字面量。
- 备注：跨文件证据来自定点 grep（`runtime_offsets|interfaces_offset|\.key_offset`），未通读这些文件。

### [低] 85-13｜void pointer 在 ABI1/ABI2 使用不同标签且无版本说明
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:256`（写 118 `'v'`）对照 101（写 86 `'V'`）；相关 252（readonly 回调结果经 `KrtLinkType` 也落到 118）
- 类别：3 语义分叉；13 注释与实现脱节
- 触发条件：void 指针出现在函数签名/回调结果中，分别经 legacy 与 ABI2 编码。
- 后果：同一 void 指针在两套规范下字节不同（`v` vs `V`），且 118 在 legacy 里同时承担“void 指针”和“readonly 结果转发”两种语义；256 行注释只说“Leave every historical nonnullable ABI1 spelling byte-identical”，未说明这是有意的双标签。
- 最小修复：统一标签或在注释/文档中明确两套编码的对应表。
- 备注：低（主流路径走 ABI2，legacy 标签差异主要影响回落路径）。

### [低] 85-14｜`KrtNativeRuntimeReserve` 默认 alignment 为 2 的幂，且被传入 storage 尺寸
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:968-973`（调用 1025-1026、1037、1050）
- 类别：8 未检查前置条件
- 触发条件：非内联记录的 `alignment = storage`（1025），而 `storage` 取 `KrtNativeBoxStorage`；若得到 12 等非 2 幂值（≤16 时不被 1026 行强制成 8）。
- 后果：970 行 `(bytes.count & (alignment-1))` 对非 2 幂失去对齐意义，静默按错误边界填充；记录/接口表偏移与消费方假设不符时会读到错位字段。
- 最小修复：reserve 前显式校验 `alignment` 为 2 的幂且 ≤16，否则 `bytes.ok=false; return -1`；不要用 storage 充当 alignment。
- 备注：疑似——kind==1 在 1026 被强制为 8，其余 kind 的 BoxStorage/ValueAlignment 可能总是 2 幂，实际触发概率低。

### [低] 85-15｜`KrtModuleTypeHash` 为死代码
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:482-484`
- 类别：5 死代码
- 触发条件：不适用（全仓 grep `KrtModuleTypeHash` 仅命中定义处，无调用）。
- 后果：无用函数增加维护面；其语义（对 identity 做 FNV 哈希）已被 `KrtAbiHash(KrtModuleTypeIdentity(...))` 的内联用法取代。
- 最小修复：删除，或接入实际调用点并补充测试。
- 备注：如为对外 API 预留，应加注释说明。

### [低] 85-16｜`KrtLinkNumber` 对 INT32_MIN 溢出且无诊断
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:213-217`
- 类别：8 未检查前置条件；2 诊断缺失
- 触发条件：`value == -2147483648` 时 `value = -value` 仍为负（int32 溢出），随后 `value/divisor % 10` 得到负数位，`KrtLinkByte` 以 `value<0` 置 `valid=false`。
- 后果：静默产生无效链接名，错误延迟到调用方检查 `valid` 时才暴露，且无专用诊断。
- 最小修复：对 `value == INT32_MIN` 单独处理（或改用无符号转换/字符串转换），失败时给出明确诊断。
- 备注：低——当前调用点（fixed_length、float 宽度、bits）均非负，INT32_MIN 实际不可达，属防御性缺陷。

### [低] 85-17｜`KrtNativeResolvePrototypes` 摘链后不释放节点，且 child_count 与链表状态耦合
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:364-370`
- 类别：10 资源与生命周期；7 共享可变状态
- 触发条件：找到同签名原型/定义后 `remove=true`，执行 `module.left = next` 或 `previous.next = next`。
- 后果：被移除的 `candidate` 节点既不 delete 也不再由任何链引用（仅局部 `next`），成为不可达堆内存；`module.child_count` 递减但节点内数据仍在，若后续逻辑按 child_count 反推链表规模易错位。
- 最小修复：摘链后 `delete candidate`（确认无外部引用），或交由统一 AST arena 回收并注释所有权。
- 备注：单次编译内泄漏量小，故低。

### [低] 85-18｜临时 AST 节点清理不一致
- 位置：`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt:305、318、547、555、728、734、126`（对照有 delete 的 657、668）
- 类别：10 资源与生命周期；9 错误处理不一致
- 触发条件：这些位置 `KrtAstMake` 生成临时 Type/Function 节点供签名编码使用。
- 后果：同文件对同类临时节点有时 `delete`（657、668）、有时不 delete，标准不一；未 delete 的节点在 `KrtNativeFunctionLinkNameLegacy` 的每次 O(n²) 调用中反复产生。
- 最小修复：统一所有权规则（谁创建谁释放或统一 arena），并在函数出口集中释放。
- 备注：疑似——若 AST 由全局 arena 统一回收则影响有限，需结合内存管理约定确认。
