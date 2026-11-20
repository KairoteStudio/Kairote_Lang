# 第七十二批：SelfHost/Compiler/Middle/Ir/Ir.krt（1-1920）

## 覆盖范围

- 文件：`SelfHost/Compiler/Middle/Ir/Ir.krt`，仅审计第 1-1920 行（Boxing lowering、Closure lowering、Exception lowering、Floating point lowering、Generator lowering、IR construction 的前半部分、`KrtIr*` 校验/常量折叠/字节缓冲 IR、`KrtNativeOp` 枚举、`KrtNativeInit`/`KrtNativeEmit`/`KrtNativeAppendData`/`KrtNativeLowerLogical`/`KrtNativeLowerLValue`/`KrtNativeLowerExpression` 等）。
- 交叉核对：仅按符号 grep / 定点读取，核对过 `KrtNativeFloatKind`（`Frontend/Semantic/Semantic.krt:2374`，已确认对 `node == null` 返回 0，故 521-522 行顺序无害）、`KrtNativePatchLoop`（本文件 2101，已确认 `head < 0` 安全）、`KrtNativeLowerDispatchTrap`（本文件 2678）、`KrtNativeRuntimeTransferPacket`/`KrtNativeLowerRuntimeReleasePacket`（本文件 2812/2824）。
- 未通读其他源文件；`Re.KrtC` 的 `Middle/Ir` 是 SSA 版 IR，与本文件这套栈式 IR 并非逐函数对应，无法逐行对照，相关结论已标「疑似」。

## 问题条目

### [高] 72-1｜KrtNativeEmit 在判空之前就解引用 module
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1613-1615`
- 类别：8 未检查前置条件（NULL）
- 触发条件：以 `module == null` 调用 `KrtNativeEmit`（`KrtNativeInit` 自己就允许 `module == null`，说明该入参被预期可能为空）。
- 后果：第 1614 行 `module.ssa = null;` 先对空指针写入，直接崩溃；第 1615 行的 `module == null` 判空永远执行不到，属于「判空写了但无效」。
- 最小修复：把 null 判断提到函数首行：`if (module == null) { return -1; }`，再执行 `module.ssa = null;`。
- 备注：无。

### [高] 72-2｜KrtNativeLowerCreateCell 缺少分配失败陷阱，与同类分配点不一致
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:180-189`（对照 `:54`、`:2691`、`:2719`、`:2942`）
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`KrtNativeOp.Allocate` 返回 NULL（堆耗尽）。
- 后果：`StoreLocal declaration.native_cell_slot` 把 NULL 存入 cell 槽，紧接着 `KrtNativeLowerCellPointer` 取该 NULL 并 `Store 8` 写 `NULL+0`、`Address 8` 后写 `NULL+8`，造成空指针写或堆损坏；而 box 创建（`:54`）、dispatch（`:2691`）、new/数组（`:2719`、`:2942`）都在 `Allocate` 后接 `KrtNativeLowerDispatchTrap`，唯独 cell 创建没有。
- 最小修复：在 `:181` 的 `Allocate` 之后补 `Branch + KrtNativeLowerDispatchTrap`，与 `KrtNativeLowerBoxCreate` 的 `allocated` 写法一致。
- 备注：无。

### [高] 72-3｜breaks/continues 固定 64 深度，嵌套过深即越界写
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:388`、`:945`、`:1010`（写入点 `:2224`、`:2437`、`:2598`；读取点 `:2243`、`:2448`、`:2608`）
- 类别：8 未检查前置条件（越界）
- 触发条件：函数体内循环/开关嵌套深度达到 64（`depth` 每次进入循环 `+1`，`Break`/`Continue` 用 `depth - 1` 索引）。
- 后果：`breaks[depth]` / `continues[depth]` 越界写，破坏相邻栈内存；无任何诊断。
- 最小修复：分配前检查 `depth < 64` 并返回 false（带诊断），或改用按深度增长的动态数组。
- 备注：无。

### [高] 72-4｜KrtNativeLowerReleaseCellValue 结束时栈上仍留一个指针，PatternCell/TypeTest 路径未丢弃（疑似）
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:214-238`；调用点 `:242`、`:257`、`:326`、`:2152`
- 类别：6 契约破坏 / 10 资源与生命周期
- 触发条件：任何 cell 释放（捕获变量离开作用域、类型测试模式、闭包删除）。
- 后果：逐路径核对栈效果（`Branch` 的 `args` 为真目标，已由 `:46-47`、`:1776-1782`、`:2776-2777` 三处确认）：入口 `[P]`，三条出口（空指针走 `drop_start`、引用计数未清零走 `drop_start`、清零走 `free_start`）都终结为 `[P]`，即只「用掉」未「消耗」。`KrtNativeLowerReleaseCell`（`:240-245`）在 `StoreLocal` 后同样把 `P` 留在栈上；`KrtNativeLowerPatternCell`（`:257`）在 `KrtNativeLowerTypeTest` 这个**表达式**里直接返回，`TypeTest` 的布尔结果下方多出一个指针，后续 `Branch` 会取到指针而非布尔，造成条件误判/栈污染。
- 最小修复：明确契约。若应为「消耗」，在 `free_start`/`drop_start` 收尾处各补一次 `Drop`（或让 `ReleaseCellValue` 出口统一 `Drop`）；若确为「保留」，则 `KrtNativeLowerReleaseCell`、`KrtNativeLowerPatternCell`、`KrtNativeLowerDeleteClosure` 三处调用点必须显式 `Drop`。
- 备注：疑似——`Re.KrtC` 的 IR 是 SSA 版，无逐函数对应实现；需再确认 VM 是否在语句边界重置操作数栈（若是，`:2152`/`:326` 可容忍，但 `:257` 在表达式内部仍不可容忍）。

### [中] 72-5｜KrtIrValidateFunction 未校验 results/immediates/types 的取值与部分数组非空
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1172-1225`（缺口在 `:1176`、`:1203`、`:1207-1212`）
- 类别：6 契约破坏
- 触发条件：构造出 `results == null`、`immediates == null`（且无 Branch）、或 `types[i]` 为非法枚举值的 `KrtIrFunction`。
- 后果：`:1176` 只检查 `opcodes/types/lefts` 非空，`results`、`immediates` 完全不看；`types` 取值从不校验（`:1210`、`:1211` 只比较相等）；`StoreLocal` 的 `rights`（本地槽号）既不检查非空也不检查范围。校验返回 true 后，后端/优化器按契约解引用即崩溃或静默错码。
- 最小修复：补 `results/immediates/rights` 非空检查、`types[i]` 枚举范围检查、`StoreLocal` 的 `rights[i]` 槽号范围检查。
- 备注：无。

### [中] 72-6｜KrtIrTryEvaluateWithLocals 在 before 不在语句链上时走到空指针
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1265-1282`（循环 `:1270`，解引用 `:1271`）
- 类别：8 未检查前置条件（NULL）
- 触发条件：`before` 不是 `unit.left.left` 这条语句链中的节点（跨函数传入、或传入 null）。
- 后果：`while (statement != before)` 会一路走到 `statement == null`，随后 `:1271` 的 `statement.kind` 空解引用崩溃；函数没有 `statement != null` 兜底。
- 最小修复：循环条件改为 `while (statement != null && statement != before)`，并在走到链尾时返回 false。
- 备注：现有调用点（`:1348`、`:1372`、`:1380`、`:1394`、`:1452`、`:1276`）恰好都传链内节点，所以当前不触发，但函数自身无保护。

### [中] 72-7｜常量折叠：整数溢出与无符号常量按有符号比较
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1227-1263`（溢出点 `:1235`、`:1249-1251`；比较点 `:1256-1259`；`KrtIrTryEvaluateWithLocals` 同缺陷 `:1294-1297`、`:1304-1314`）
- 类别：3 语义分叉 / 8 整数溢出
- 触发条件：折叠 `INT64_MIN / -1`、`INT64_MIN % -1`、超范围加/乘、或对无符号常量做 `<`/`>`/`<=`/`>=`。
- 后果：`:1235` 的 `-operand`、`:1249-1251` 的 `+ - *` 均无溢出检查；比较只按有符号语义（`:1256-1259`），而 `KrtAstNode` 自带 `native_unsigned` 未被使用，无符号常量会被折叠成相反结果，进而 `KrtIrLowerReturnFunction` 把错误常量直接写进 IR（`:1361`、`:1386`、`:1406`）。
- 最小修复：折叠前读取 `node.native_unsigned` 选择比较语义；对 `/ -1`、`% -1`、`*`/`+`/`-` 做溢出判断，溢出时返回 false（不折叠）而非给出错误值。
- 备注：疑似——需与 `Re.KrtC` 的常量折叠对照确认无符号语义是否符合语言规范。

### [中] 72-8｜KrtNativeFindFunction / KrtNativeFindMemberFunction 的 1024 上限静默截断
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1638-1646`（`:1641`）、`:1648-1656`（`:1651`）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：模块内函数（或成员函数）数量超过 1024，且目标函数排在第 1024 个之后。
- 后果：循环在 `index < 1024` 处退出并 `return -1`，调用解析失败，无任何「函数过多」诊断；编译结果取决于函数声明顺序。
- 最小修复：去掉硬上限（链表长度本身有限），或超限时报错并指出具体符号。
- 备注：两处为不同函数，分别独立记录。

### [中] 72-9｜KrtNativeSameName 对源偏移与长度无边界检查
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1625-1636`（解引用 `:1631`）
- 类别：8 未检查前置条件（越界）
- 触发条件：`first`/`second` 为负，或 `first + first_length` / `second + second_length` 超出源缓冲。
- 后果：`:1631` 的 `bytes[first + i]` / `bytes[second + i]` 越界读，可能读到相邻内存并误判符号相等，或崩溃。
- 最小修复：进入循环前校验 `first >= 0 && second >= 0 && first + first_length <= source_length && second + second_length <= source_length`。
- 备注：无。

### [中] 72-10｜KrtIrAppendMemory 的 256 上限使字节缓冲函数静默失败
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1466-1474`（上限 `:1467`；调用链 `:1522-1549`）
- 类别：1 静默失败 / 2 诊断缺失 / 12 测试不足
- 触发条件：`KrtIrLowerByteBuffer` 接受的函数里语句数使指令数达到 256（约 128 条 `buf[i] = const` 赋值）。
- 后果：`KrtIrAppendMemory` 返回 -1，`KrtIrLowerByteBuffer` 返回 false，编译失败但没有任何「超出 IR 容量」的诊断；且上限 256 只存在于 memory 路径，`KrtIrValidateFunction` 的上限是 4096，两处容量口径不一致。
- 最小修复：改为按需扩容（或统一到 4096），并在真正的上限处给出带位置的诊断。
- 备注：无。

### [中] 72-11｜生成器 lower 的失败路径遗留 module.generator，且泄漏 breaks/continues
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:926`/`:972`（Move）、`:998`/`:1030`（Dispose）、`:388-391`（TemporaryClosureCall）
- 类别：7 共享可变状态 / 10 资源与生命周期
- 触发条件：`KrtNativeLowerGeneratorMove`/`Dispose` 中 `module.generator = plan` 之后任意一步返回 false（例如 `:946`、`:952`、`:1019`、`:1024`）。
- 后果：`module.generator` 保持非空且指向该 plan，后续所有 `KrtNativeEmit` 都会被 `KrtNativeGeneratorEmit`（`:1613`）重定向到 generator 寻址逻辑，污染同一 module 的后续函数生成；同时已 `new` 的 `breaks`/`continues` 在失败路径上不会 `delete`。
- 最小修复：用统一的收尾（defer 语义）在函数出口同时恢复 `module.generator = null` 与释放两个数组；或在每个失败分支前清理。
- 备注：无。

### [中] 72-12｜KrtNativeGeneratorAllocationGuard 分配失败时静默返回 0
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:830-837`（调用点 `:867`、`:881`）
- 类别：1 静默失败
- 触发条件：`KrtNativeGeneratorAllocate` 的 `Allocate` 返回 NULL。
- 后果：守卫分支直接 `Const 0; Return`，把空指针对象当正常结果返回给调用方，调用方拿到 NULL 后再解引用；与 box/dispatch 使用 `KrtNativeLowerDispatchTrap` 的惯例不一致。
- 最小修复：NULL 分支改接 `KrtNativeLowerDispatchTrap`（或抛出统一 OOM 异常），不要返回 0。
- 备注：无。

### [中] 72-13｜KrtNativeLowerBaseConstructor 目标缺失时静默压入 0，跳过基类构造
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1138-1144`（`:1140`）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`expression.native_target == null`（基类构造目标解析失败）。
- 后果：函数返回 true 并只发一条 `Const 0`，基类构造调用被完全跳过，对象处于未初始化状态，且编译期无任何提示。
- 最小修复：返回 false 并附诊断；若确为「无基类」语义，应由调用方显式区分，而不是复用「解析失败」这一路径。
- 备注：无。

### [中] 72-14｜KrtIrLowerReturnFunction 对 Return.left 为空无保护
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1392`、`:1438`
- 类别：8 未检查前置条件（NULL）
- 触发条件：`final_statement`（或循环中的 `Return` 语句）的 `left == null`（裸 `return;`）。
- 后果：`:1392` 的 `final_statement.left.kind`、`:1438` 的 `statement.left.kind` 直接空解引用崩溃。
- 最小修复：在两处访问 `.kind` 前先判断 `left != null`，为空时返回 false。
- 备注：无。

### [中] 72-15｜KrtNativeLowerGeneratorFunction 写 module 元数据数组无下标边界检查
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1037-1038`（数组容量 16384 定义于 `:1582-1588`）
- 类别：8 未检查前置条件（越界）
- 触发条件：`index >= 16384`（模块内函数数超过元数据数组容量）。
- 后果：`module.starts[index]`、`module.parameter_counts[index]`、`module.local_counts[index]`、`module.function_frame_bytes[index]` 越界写，破坏堆。
- 最小修复：写入前检查 `index >= 0 && index < 16384`，超限返回 false 并诊断。
- 备注：无。

### [中] 72-16｜KrtNativeFloatingBits 对非法 native_float 默认按 double 舍入
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:658-661`
- 类别：3 语义分叉
- 触发条件：`expression.native_float` 为 0 或既非 4 也非 8 的值。
- 后果：`fraction_bits`/`bias`/`infinity` 全部取 double 分支（52/1023），字面量按 52 位尾数舍入，与声明类型（如 float）不符；且 `:675`、`:694` 的溢出判定也用错误的 bias。
- 最小修复：只接受 4/8，其余返回 false 并诊断；或按目标类型显式传入格式。
- 备注：疑似——需确认上游是否保证 `native_float ∈ {4,8}`，若保证则应加断言而非静默降级。

### [低] 72-17｜KrtNativeLowerFloatingBinary 未校验 width 非 0
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:546-554`（`:547-550`、`:554`）
- 类别：8 未检查前置条件
- 触发条件：`expression.left`/`expression.right` 的 `KrtNativeFloatKind` 均为 0（非浮点操作数）。
- 后果：`width = 0`，`target.native_float = 0`，仍以 `width = 0` 发 `FloatBinary`/`FloatCompare`，产生未定义行为的浮点指令。
- 最小修复：`width == 0` 时返回 false（或改走整数路径）。
- 备注：疑似——需确认调用方是否已保证至少一侧为浮点。

### [低] 72-18｜KrtNativeLowerTry 中的死条件与未校验的 record slot
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:488-490`（以及 `:466`）
- 类别：5 死代码 / 8 未检查前置条件
- 触发条件：任意带 catch 子句的 try。
- 后果：`:489` 已保证 `catch_enter >= 0`，故 `:490` 的 `frame.active_handler = catch_enter >= 0;` 恒为 true，是死条件（掩盖了「catch_enter 失败」这一分支）；同时 `:466` 的 `TryEnter` 直接使用 `statement.local_slot`，未校验其非负。
- 最小修复：`:490` 直接 `frame.active_handler = true;`；在 `:466` 前校验 `statement.local_slot >= 0`。
- 备注：无。

### [低] 72-19｜KrtIrContainsIdentifier 不检查 alternate 分支
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1320-1324`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：`Ternary` 的 `alternate` 中含标识符而 `left/right` 不含。
- 后果：函数对 `alternate` 视而不见，返回 false，使 `:1392-1394` 的折叠路径漏判（偏保守，不错误折叠，但与其他分支处理不一致，且无注释说明）。
- 最小修复：递归 `node.alternate`（若语言语义允许），或加注释说明为何只查两侧。
- 备注：无。

### [低] 72-20｜KrtNativeInit 未初始化多数函数元数据与生成器状态
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:1570-1600`（`:1582-1592`、`:1593`）
- 类别：11 配置漂移
- 触发条件：读取尚未 lower 的函数的 `starts`/`local_counts`/`parameter_counts`/`aggregate_return_sizes`/`aggregate_return_alignments`/`function_frame_bytes`/`function_name_starts`/`function_name_lengths`，或读取 `generator`/`generator_raw_emit`/`lower_function_index`。
- 后果：只对 `hidden_result_slots` 统一填了 -1 哨兵（`:1590`），其余数组依赖 `new` 的零初始化，读到的是 0 而非「未设置」哨兵，容易与合法值 0 混淆；`generator_raw_emit` 也未显式复位。
- 最小修复：对所有元数据数组统一初始化哨兵值，并显式复位 `generator`、`generator_raw_emit`、`lower_function_index`。
- 备注：无。

### [低] 72-21｜KrtNativeLowerBoxFault 使用魔数 -2147483647，错误码无统一常量
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:19`（对照生成器错误码 `:757` 的 -2147483602、`:907` 的 -2147483601）
- 类别：11 配置漂移 / 2 诊断缺失
- 触发条件：每次拆箱失败。
- 后果：异常码以散落的字面量分布在各 lower 函数，没有集中常量表；-2147483647 与常规 `INT32_MIN`（-2147483648）差 1，易在对照 C 版时漂移，且注释（`:15-16`）只说明「走 int32 异常契约」，未给出码表。
- 最小修复：定义统一异常码常量（含名字与数值），各处引用常量。
- 备注：疑似——需与 `Re.KrtC` 的异常码表对照确认 -2147483647 是否为有意的保留值。

### [低] 72-22｜KrtNativeLowerObjectEquality 的 source 形参未使用且临时槽分配后检查
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:124`（未用形参）、`:133-134`（先分配后检查）
- 类别：5 死代码 / 10 资源与生命周期
- 触发条件：`left = KrtNativeBoxTemporary(module)` 返回 -1（槽位耗尽）。
- 后果：`source` 形参在函数体内完全未使用；`right` 仍被分配（`local_counts` 已 +1）后才在 `:134` 判断 `left >= 0 && right >= 0`，失败时白白消耗一个临时槽。
- 最小修复：删除未用形参；先取 `left` 并判 `>= 0` 后再取 `right`。
- 备注：无。

### [低] 72-23｜KrtNativeLowerGeneratorGet/Current 硬编码槽位 0/1，且 object_size-8 可能为负
- 位置：`SelfHost/Compiler/Middle/Ir/Ir.krt:879-886`（Get）、`:903-914`（Current）
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：getter/current 角色函数带额外参数（占用槽 1），或 `plan.object_size < 8`。
- 后果：`:879`、`:881`、`:884`、`:903`、`:910` 直接把 0/1 当接收者/临时槽使用，与参数槽约定耦合，参数较多时槽 1 冲突；`:886` 的 `plan.object_size - 8` 在 `object_size < 8` 时为负，传给 `AggregateCopy` 作长度。
- 最小修复：使用 plan 中显式的 receiver_slot/scratch_slot 字段；`AggregateCopy` 前校验 `object_size >= 8`。
- 备注：疑似——需确认生成器各角色函数的帧布局是否由上游固定为「接收者 0、临时 1」。
