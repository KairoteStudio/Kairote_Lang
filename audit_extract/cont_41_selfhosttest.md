# 第九十八批：Test/SelfHost 全部源文件

## 覆盖范围

- 枚举方式：`glob Test/SelfHost/**/*.{c,h,inc,krt}`，命中 24 个 `.krt`，**0 个 `.c/.h/.inc`**；逐个全量读取，共 811 行。
- 文件清单（行数）：
  - 顶层：`test_contract.krt`(308)、`test_lexer.krt`(32)、`test_object_fields.krt`(43)
  - `fixtures/numeric/`：`arithmetic_calls_arrays_fields.krt`(17)、`constant_float.krt`(1)、`ieee_comparisons_conversions.krt`(22)、`literal_rounding.krt`(57)、`loops_and_memory.krt`(1)、`nonstandard_widths.krt`(17)、`overload_precision.krt`(1)、`wide_arithmetic.krt`(18)、`wide_conversions.krt`(13)、`wide_references.krt`(4)
  - `fixtures/closure-abi/LegacyApply.krt`(1)、`fixtures/module-abi-wide/LegacyPhysical.krt`(5)
  - `fixtures/runtime-http/Consumer.krt`(50)、`Provider.krt`(81)
  - `examples/daily.krt`(23)；`examples/language-project/`：`project.krt`(7)、`src/Box.krt`(9)、`src/Main.krt`(39)；`examples/dispatch-project/`：`project.krt`(7)、`src/Main.krt`(31)、`src/Measurements.krt`(24)
- 范围外未审：同目录另有 `test_*.py` / `Bootstrap.py` 驱动、`provenance.txt`（含构建机绝对路径，未读取）、`obj/`、`bin/`、`.krtcache/` 生成物；均不属于允许的 `.c/.h/.inc/.krt`。
- 符号核对仅限定向 grep / 局部窗口：`KrtIrLowerReturnFunction`、`KrtIrValidateFunction`、`KrtKroEmitIrFunction`、`KrtKroObjectIsValid`、`KrtCompileSourceToFd/ToPath`、`KrtLexerNext` 签名。
- 自举一致性结论：`test_contract.krt` 的 magic=5198411 / version=2 / architecture=34404 与 B8/C3 字节，和当前 `KroBackend.krt` 发射器一致；未发现活动的 ABI/IR 版本错配。但多处把当前实现细节固化为期望（98-10、98-29、98-31）。

## 问题条目

### [高] 98-1｜fd=-1 使负向编译断言恒真
- 位置：`Test/SelfHost/test_contract.krt:25`
- 类别：断言恒真 / 负向测试失效
- 触发条件：`invalid_source`（`"int32 main() { }"`）若被语义阶段接受，`KrtCompileSourceToFd` 会继续写 header/text/symbols；fd=-1 必然写入失败（`KroBackend.krt:782` 要求 `syscall(1,...) == 64`），函数仍返回 false。
- 后果：无论编译是否真的失败，`if (...) { return 35; }` 都不触发，语义校验回归（空体 `int32 main` 被接受）无法被本测试发现。
- 最小修复：负向用例改用 `KrtCompileSource` 直接断言 false；写盘路径另用有效 fd（`/dev/null` 或临时文件）单独测试。
- 备注：该行注释称文件集成由 Stage1 driver 负责，但恒真缺陷独立存在。

### [中] 98-2｜产物写 CWD、无清理、无内容回读断言
- 位置：`Test/SelfHost/test_contract.krt:55,75,80,94,172,259`
- 类别：资源残留 / 依赖 CWD / 契约覆盖缺口
- 触发条件：任意 CWD 下运行即创建 `contract.kro`、`argument-call.kro`、`two-argument-call.kro`、`real-call.kro`、`local.kro`、`locals.kro`，返回 0 后不删除；测试从不打开或校验这些文件。
- 后果：污染仓库/工作目录（重跑残留旧对象），且写盘字节正确性仅凭返回值，内容错误可静默通过。
- 最小修复：写入临时目录并在结束时删除；增加回读断言（至少文件存在、长度、头部 magic）。
- 备注：与写入器相关的字节级缺口见 98-6。

### [低] 98-3｜cfg 全程未释放，提前 return 路径成批泄漏
- 位置：`Test/SelfHost/test_contract.krt:28`（无 `delete cfg`）；`:25,41,43,46,48,70,134,207` 等在 delete 之前 return
- 类别：资源泄漏
- 触发条件：`KrtIrFunction cfg = new KrtIrFunction()` 直到函数结束都没有 `delete cfg`；多条失败分支在已分配 `object/direct/emitted/cfg` 后直接 return。
- 后果：进程内对象泄漏（进程退出虽回收，但与项目显式 delete 约定不一致）。
- 最小修复：成功路径末尾 `delete cfg`；各负向分支先释放再 return，或统一 cleanup。
- 备注：零容忍口径下的明确泄漏点。

### [中] 98-4｜四处解析后未检查 parser.errors
- 位置：`Test/SelfHost/test_contract.krt:131-134,148-150,164-166,283-284`
- 类别：断言缺失 / 静默通过
- 触发条件：probe/expression/variable/unary 解析后直接使用 AST（`probe.left.child_count`、`expression_ir.immediates[0]`），未像 `:103/:116/:193` 那样先判 `parser.errors != 0`。
- 后果：解析报错但 AST 碰巧满足形状/数值时测试继续并通过，掩盖语法回归。
- 最小修复：每处解析后立即 `if (parser.errors != 0) { return <专属码>; }`。
- 备注：`:164-166` 依赖 `KrtSemanticBindLocals` 成功间接暴露错误，并不等价。

### [中] 98-5｜manifest 字段与符号/重定位内容断言缺口
- 位置：`Test/SelfHost/test_contract.krt:21-22`；调用程序 `:60-63,72-84`
- 类别：契约测试覆盖缺口（ABI/manifest）
- 触发条件：`KrtKroObjectIsValid`（`KroBackend.krt:471-473`）校验 15 个字段，测试只断言 magic/version/architecture/text_size/symbol_count/entry_point/total_reloc_count/strtab_size；`rodata_size`、`data_size`、`bss_size`、`relocation_count`、`text/rodata/data_reloc_count`、`bss_align` 从未断言。
- 后果：这些 manifest 字段被写成错误尺寸/计数时测试仍通过。
- 最小修复：对 `emitted` 逐字段断言（当前应为 0）；对 call_program 增加符号表名字节与重定位项的正向断言，而非只看 `text_size`。
- 备注：现发射器仅支持无重定位的紧凑对象，"正向重定位"断言需先扩展发射器。

### [中] 98-6｜机器码字节断言只覆盖 immediate=0
- 位置：`Test/SelfHost/test_contract.krt:23`
- 类别：断言覆盖不足
- 触发条件：direct 函数 immediate 为默认 0，只验 `text_word0&255==184`（B8）与 `(text_word1>>8)&255==195`（C3）；imm32 高 3 字节恒 0，符号扩展/字节序错误不改变被测字节。
- 后果：非零立即数编码（如 helper 返回 7 的 real-call 路径）无任何字节级断言，编码错位只要这两字节仍为 184/195 即通过。
- 最小修复：构造 immediate≠0（含负数）的 IR 并断言全部 5 字节；对 `real-call.kro` 也做字节断言。
- 备注：`KrtKroEmitConstantHelperCall` 产物在 `:93-94` 只断言 `text_size`。

### [中] 98-7｜源码长度以魔法数字硬编码传入
- 位置：`Test/SelfHost/test_contract.krt:89,100,113,129,146,162,169,176,180,184,190,200,207,213,218,224,230,237,247,256,259,271,281,288,295`
- 类别：非确定性 / 越界读
- 触发条件：字面量被编辑而长度未同步：偏大则 lexer/编译器读到字符串边界之外（内容取决于分配器与邻接数据）；偏小则静默截断。
- 后果：测试结果依赖堆布局（flaky）或假通过/假失败，且构成未定义读。
- 最小修复：改用语言内建长度（如 `source.Length`）或集中常量，禁止裸数字。
- 备注：本批人工核对了上述全部长度，当前与字面量一致；属维护期高危写法而非现行错误。

### [低] 98-8｜opcode/token 数值以裸数字断言
- 位置：`Test/SelfHost/test_contract.krt:167`（11/12）、`:274-284`（61/43/42/33/45）
- 类别：自举一致性 / 可维护性
- 触发条件：`KrtIrOpcode`（`Ir.krt:1152`）或 token/op 枚举重新编号后，断言仍比对旧数字。
- 后果：测试要么假失败，要么在数字巧合下假通过，回归时无法定位语义。
- 最小修复：全部改用枚举常量（如 `KrtIrOpcode.StoreLocal`）。
- 备注：当前 11=StoreLocal、12=LoadLocal 与 `Ir.krt:1152` 一致。

### [低] 98-9｜多个互斥失败原因共用同一返回码
- 位置：`Test/SelfHost/test_contract.krt:74-75`(45)、`:79-80`(76)、`:93-94`(47)、`:21-22`(14/29)、`:58`(40)
- 类别：诊断与实际错误不符
- 触发条件：编译失败与写盘失败、发射失败与写盘失败、以及同一断言内任一字段不符，都归为同一返回码。
- 后果：CI 只看到 45/76/47，无法区分编译器、Kro 写入器或磁盘问题，定位成本转嫁。
- 最小修复：为每类原因分配独立返回码，或输出结构化诊断。
- 备注：numeric fixtures 基本做到 1 断言 1 码，可作范式。

### [低] 98-10｜把"发射器仅支持单 block"这一当前限制固化为期望
- 位置：`Test/SelfHost/test_contract.krt:41`
- 类别：自举一致性
- 触发条件：`KroBackend.krt:840` 明确 `block_count != 1` 即返回 false；一旦发射器支持多 block，该断言反而失败。
- 后果：增强后端会让契约测试变红，维护者可能误改测试而非确认行为变更。
- 最小修复：注释标明这是当前紧凑发射器的已知限制，并在扩展时同步更新。
- 备注：该负向断言与当前实现一致，非现行 bug。

### [中] 98-11｜词法器测试只覆盖标识符与二进制字面量
- 位置：`Test/SelfHost/test_lexer.krt:5-27`
- 类别：契约覆盖缺口
- 触发条件：全部用例仅含标识符、十进制整数、二进制/非法二进制与 EOF；运算符、标点、注释、换行/制表、字符串、浮点、关键字、非法字符均未测。
- 后果：这些词法路径回归（注释吞 token、字符串转义错误等）不会使测试失败。
- 最小修复：为每类 token 增加 start/kind/length 三元组断言，补注释/换行/字符串用例。
- 备注：非法二进制的 5 个断言是有效覆盖，可作为扩展范式。

### [低] 98-12｜EOF 的 length 断言不一致
- 位置：`Test/SelfHost/test_lexer.krt:14` 断言 `length==0`，`:27` 只断言 `start==27`
- 类别：断言缺失
- 触发条件：第二段输入结束后的 Eof token 若 length 被改成非 0，`:27` 不报错。
- 后果：同类不变量只在一处成立，回归检测不对称。
- 最小修复：`:27` 补 `token.length != 0`。
- 备注：影响小。

### [低] 98-13｜别名对象的删除顺序依赖未声明的 delete 语义
- 位置：`Test/SelfHost/test_object_fields.krt:35-41`（`holder.box = first;` 后 `delete holder; delete first;`）
- 类别：资源管理 / 断言缺口
- 触发条件：若 class 的 `delete` 递归释放引用字段，holder 与 first 指向同一对象会被释放两次；若为浅释放则正常。测试未声明期望哪种语义。
- 后果：双重释放/UAF 或语义变更时测试无法区分，甚至在本文件内崩溃。
- 最小修复：明确并断言 delete 语义；或先断开 `holder.box = null` 再删，并补 null/default 负例。
- 备注：文件缺 null/default 访问用例。

### [中] 98-14｜浮点断言容差过宽
- 位置：`Test/SelfHost/fixtures/numeric/ieee_comparisons_conversions.krt:14`（1.8e19..1.9e19）、`:17`（13.19..13.21）
- 类别：断言强度不足
- 触发条件：uint64→float64 转换偏差若干 ULP（1 ULP≈2）仍落在 1e18 宽区间；表达式折叠偏差 0.001 仍落在 ±0.01。
- 后果：转换/折叠的舍入模式或末位错误被容差吞掉，测试假绿。
- 最小修复：按精确值断言（如 `wd == 18446744073709551616.0`）或断言到位级。
- 备注：同文件 `:15-16,:18` 的精确断言是正确范式。

### [中] 98-15｜NaN/无穷用例依赖 FP 环境与优化策略
- 位置：`Test/SelfHost/fixtures/numeric/ieee_comparisons_conversions.krt:7-10`
- 类别：非确定性 / 环境依赖
- 触发条件：`zero/zero`→NaN、`1.0/zero`→inf 依赖运行期 IEEE 语义；若使用 fast-math、常量折叠精度不同或目标 FPU 非 IEEE，比较结果可能变化。
- 后果：同一源码在不同优化等级/目标上结果不一致（flaky），或把平台差异误报为回归。
- 最小修复：在夹具/驱动中固定 FP 语义（禁快算、固定舍入模式），并补 `-0.0 == 0.0`、NaN 载荷保持等不变量。
- 备注：当前无 `-0.0 == 0.0` 与 NaN payload 断言。

### [低] 98-16｜decimal→binary 位模式假设 round-half-even 但无出处
- 位置：`Test/SelfHost/fixtures/numeric/literal_rounding.krt:53-54`（v100/v101 为 2^-24 平局），全文件位模式同此假设
- 类别：自举一致性 / 断言依据
- 触发条件：若语言规范或字面量解析采用 round-half-away/truncation，这两个 tie 样例会假失败；反之实现改舍入方向，位模式也失去参照。
- 后果：夹具正确性依赖一个未在文件内引用的规范假设，维护者无法判断该改夹具还是实现。
- 最小修复：文件头注明所依据的 ISO/IEC 60559 舍入条款与参考实现来源，并与规范文档交叉引用。
- 备注：抽验 v10/v11（2^53±1、2^53+3）与 tie-to-even 一致。

### [低] 98-17｜对 float 局部变量做指针类型双关
- 位置：`Test/SelfHost/fixtures/numeric/literal_rounding.krt:2-52`（`*((uint64*)&vN)` 系列，float32 用 `uint32*`）
- 类别：未定义行为 / 优化器敏感
- 触发条件：通过不兼容类型指针读取（严格别名/TBAA）；若后端按 C 风格别名规则优化，或对齐/表示假设不同，读回值可与预期不符。
- 后果：字面量测试可能被自身 UB 误编译，产生假失败或假通过。
- 最小修复：改用语言提供的位转换内建（若有），或语义明确的 `union`/`memcpy`。
- 备注：语言标榜 unsafe 指针能力，仍需明确别名规则。

### [中] 98-18｜无界 while 在回归时会挂起而非失败
- 位置：`Test/SelfHost/fixtures/numeric/loops_and_memory.krt:1`（`while(a[0]>0){a[0]-=0.5;}`）
- 类别：非确定性 / 测试健壮性
- 触发条件：若浮点比较/减法被错误编译（如 `a[0]` 恒 >0 或步长错误），循环不终止；夹具与驱动无超时/迭代上限。
- 后果：CI 表现为挂起/超时而非明确非零退出码，且难以定位到该夹具。
- 最小修复：加迭代计数器上限并断言到达 0；或由驱动统一超时并映射为失败码。
- 备注：起始 2.5、步长 0.5 目前精确，属防御性缺口。

### [低] 98-19｜隐式转换与数组边界路径未断言
- 位置：`Test/SelfHost/fixtures/numeric/arithmetic_calls_arrays_fields.krt:6-8`
- 类别：覆盖缺口
- 触发条件：`sum(a,b,4)` 混合 float64/float32/int32、`v[1]=2` 隐式 int→float32，均只检查结果值；无数组越界、NaN/inf、溢出用例。
- 后果：提升/转换规则或边界检查回归不触发失败。
- 最小修复：增加转换中间值断言与越界负例。
- 备注：happy-path 断言本身正确（3.75/7.75/3.5）。

### [低] 98-20｜常量折叠用例过于单薄
- 位置：`Test/SelfHost/fixtures/numeric/constant_float.krt:1`
- 类别：覆盖缺口
- 触发条件：仅 `Pi+Values.Half` 一个表达式，未覆盖 float32/float64 常量提升边界、跨类常量与折叠时机。
- 后果：常量求值回归（如提升宽度错误）可能被单个精确样例掩盖。
- 最小修复：补充非精确值与混合宽度断言，并覆盖静态类常量访问。
- 备注：文件仅 1 行，属抽查级覆盖。

### [低] 98-21｜非标准宽度只测自增回绕
- 位置：`Test/SelfHost/fixtures/numeric/nonstandard_widths.krt:6-15`
- 类别：覆盖缺口
- 触发条件：int30/uint30/int6/int14/uint62/int62 仅验证 ++/--/+= 的模 2^N 回绕；未测算术、比较、移位、转换与符号扩展。
- 后果：这些宽度的运算/转换语义错误不会被发现；有符号溢出还依赖实现定义。
- 最小修复：为每宽度增加跨运算与转换断言，并固定溢出为回绕语义。
- 备注：`pick(ref a,n)` 的 int30/int32 选择是有效 ABI 钉扎。

### [低] 98-22｜values 数组未释放
- 位置：`Test/SelfHost/fixtures/numeric/nonstandard_widths.krt:12`（`new int30[2]`，全文件无 delete）
- 类别：资源泄漏
- 触发条件：测试结束前未 `delete values`。
- 后果：与同目录其他夹具（arithmetic/wide_arithmetic 均释放数组）不一致。
- 最小修复：末尾 `delete values`。
- 备注：进程退出回收，故低。

### [低] 98-23｜重载精度用例仅两个调用
- 位置：`Test/SelfHost/fixtures/numeric/overload_precision.krt:1`
- 类别：覆盖缺口
- 触发条件：仅 `f(1.0f)`/`f(1.0)` 验证 float32/float64 选择；未测 int 字面量、歧义/隐式转换与浮点精度边界。
- 后果：重载决议的精度相关回归可能漏检。
- 最小修复：加入 `f(1)`（若语言允许）与非精确值断言。
- 备注：1 行文件，定位明确但覆盖弱。

### [中] 98-24｜int128 算术缺少溢出/除零/移位界用例
- 位置：`Test/SelfHost/fixtures/numeric/wide_arithmetic.krt:4-16`
- 类别：覆盖缺口
- 触发条件：现断言覆盖移位、乘除、取余、复合赋值与类字段，但未触及 int128 加法溢出、除零、移位≥128、有符号/无符号混用。
- 后果：128 位边界与异常路径回归静默通过。
- 最小修复：补充上述边界断言（溢出若定义为回绕需注明）。
- 备注：现有一半断言数值准确（2^100/2^65 等），可扩展。

### [中] 98-25｜宽整数↔浮点转换全部取精确值，舍入模式未测
- 位置：`Test/SelfHost/fixtures/numeric/wide_conversions.krt:5-9`
- 类别：覆盖缺口
- 触发条件：`d=x`(2^100)、`s=-1.0`、`big=2^100`、`large=2^127` 均精确可表示；仅 `(int128)-5.5` 涉及截断方向。非精确 int→float（如 2^100+1）与 float→int 的最近舍入/越界转换未测。
- 后果：浮点往返舍入规则错误不会被发现。
- 最小修复：加入 2^100+1→float→int 往返与超范围转换断言。
- 备注：`:7-8` 的截断方向断言有效。

### [低] 98-26｜数组 a 未释放
- 位置：`Test/SelfHost/fixtures/numeric/wide_conversions.krt:4`
- 类别：资源泄漏
- 触发条件：`int128[] a=[x,-x]` 在 `:11` foreach 后不再使用，但无 `delete a`。
- 最小修复：末尾 `delete a`。
- 备注：进程退出回收，故低。

### [低] 98-27｜arr 与 box 未释放
- 位置：`Test/SelfHost/fixtures/numeric/wide_references.krt:2-3`
- 类别：资源泄漏
- 触发条件：`int128[] arr=[a,b]; Box box=new Box();` 之后全文件无任何 delete。
- 最小修复：末尾 `delete arr; delete box;`。
- 备注：同目录多数夹具释放数组；此文件仅 4 行。

### [高] 98-28｜HTTP 成功路径的 status/body 未被断言
- 位置：`Test/SelfHost/fixtures/runtime-http/Consumer.krt:22-26`
- 类别：静默通过 / 契约覆盖缺口
- 触发条件：Reply 只断言 `request.tag[4]`、`snapshot.tag[0]/tag[4]`、`snapshot.id`；`response.status=snapshot.status; response.body=snapshot.body` 后直接 return，main 只汇总 audit 计数。路由成功语义（kind==0 → 200 与 `"你好，模块！\n"`，kind==1 → 404）在整条链路无断言。
- 后果：状态码或响应体被错误实现/回归时，只要 tag/id 正确且 Provider 不抛错，测试仍返回 0。
- 最小修复：Reply 内断言 `snapshot.status==200 && snapshot.body=="你好，模块！\n"`；或在 main 中经 handler 做一次端到端请求并校验 HttpResponse。
- 备注：`Provider.krt:53-57` 计算 status/body 却同样无断言，属双向缺口。

### [中] 98-29｜audit 计数 6/9/6/0 无推导且耦合运行时枚举次数
- 位置：`Test/SelfHost/fixtures/runtime-http/Consumer.krt:46-47`
- 类别：自举一致性 / 非确定性
- 触发条件：Provider 每次完整 `Routes()` 产出 2 个 rule，`Dispose`/`resources` 计数守恒；期望 `enumerated==6、disposed==9、handled==6、resources==0` 依赖 `HttpServer.Run` 内部枚举/释放调度（部分枚举可能提前终止），文件内无说明或推导。
- 后果：运行时版本或调度策略变化即假失败；反之计数语义被破坏也可能看似合理。
- 最小修复：由 Provider 暴露确定性统计（固定请求数并按请求数推导），或在夹具内注明 6/9 的来源与守恒不变量。
- 备注：`resources==0` 与 `disposed==9` 的关系需注释写清。

### [中] 98-30｜Provider 的 /fail 503 分支无任何用例触发
- 位置：`Test/SelfHost/fixtures/runtime-http/Provider.krt:48-52`
- 类别：覆盖缺口
- 触发条件：Consumer 只请求默认 path（handler 的 path 未传 `"/fail"`）；`throw failure` 的 Failure 结构、status 503、跨模块异常传播均未验证。
- 后果：错误路径回归静默通过；Provider 整文件无自身断言，全靠 Consumer 间接覆盖。
- 最小修复：Consumer/main 或外部驱动增加一次 `"/fail"` 请求并断言 503 与 id/tag。
- 备注：Provider 属被动 fixture。

### [低] 98-31｜示例输出文本未被捕获断言
- 位置：`Test/SelfHost/examples/daily.krt:18-19`
- 类别：契约覆盖缺口
- 触发条件：`Console.WriteLine("Hello from SelfHost")` 与 `WriteLine(counter.Value())` 的输出不设防；唯一判据是退出码 0。
- 后果：WriteLine 格式/编码/求值回归（输出错值）仍返回 0。
- 最小修复：捕获 stdout 断言两行内容，或至少断言 `counter.Value()==42`。
- 备注：README 记录预期输出，但自动化层面未校验。

### [低] 98-32｜泛型静态 Count 的按实例隔离未验证
- 位置：`Test/SelfHost/examples/language-project/src/Main.krt:20`（`Box<float64>.Count != 1`）
- 类别：覆盖缺口
- 触发条件：只实例化过一次 `Box<float64>`；若静态字段在所有泛型实例间共享，此断言仍为 1，无法区分。
- 后果：泛型静态字段作用域回归漏检。
- 最小修复：再用 `Box<int32>` 构造一次并断言两者计数独立。
- 备注：同文件 Average/异常路径断言较完整。

### [中] 98-33｜foreach 中删除元素后又删除数组，所有权语义不明
- 位置：`Test/SelfHost/examples/dispatch-project/src/Main.krt:13-16`
- 类别：资源管理 / 潜在双重释放
- 触发条件：`foreach (IValue value in values) { ...; delete value; }` 逐个释放元素，随后 `delete values`；若数组析构也释放元素引用即双重释放，若枚举器缓存引用则 UAF。
- 后果：内存破坏或崩溃，且当前断言无法区分。
- 最小修复：明确数组与元素所有权；要么只 `delete values`，要么循环内仅解绑、最后统一释放并断言计数。
- 备注：`:7,:11` 的 `view` 别名在 `delete measurement` 后悬垂但未被使用，属次要清理点。

### [低] 98-34｜Configure 脚本无自身契约断言，glob/输出依赖 CWD
- 位置：`Test/SelfHost/examples/language-project/project.krt:4-6`
- 类别：契约覆盖缺口 / 依赖 CWD
- 触发条件：`Sources("src/*.krt")`、`Output("bin/${config}/LanguageDemo")` 是否被驱动解释无校验；Configure 无返回值/字段断言。
- 后果：驱动忽略 Name/Sources/Optimization 时项目仍"配置成功"，工程级回归只在最终二进制层面暴露。
- 最小修复：配置后回读 `p` 字段断言，或用临时目录固定 CWD。
- 备注：`import "Box.krt"` 的相对路径同样依赖工程根。

### [低] 98-35｜同上：Dispatch 工程配置无断言
- 位置：`Test/SelfHost/examples/dispatch-project/project.krt:4-6`
- 类别：契约覆盖缺口 / 依赖 CWD
- 触发条件/后果/修复：同 98-34（`Sources`/`Output`/`Optimization` 无校验）。
- 备注：两个 project.krt 为同构副本，问题独立成条。

### [低] 98-36｜closure ABI 探针无入口、无断言、无 ABI 版本标记
- 位置：`Test/SelfHost/fixtures/closure-abi/LegacyApply.krt:1`
- 类别：自举一致性 / 覆盖缺口
- 触发条件：文件只有 `Apply(fn(int32)->int32,...)`，无 main、无 assert、无命名空间；接受/拒绝完全由外部 harness 决定，且文件名 `Legacy` 暗示针对旧 ABI。
- 后果：harness 名单漂移后探针失效无人察觉；夹具与当前 ABI 的版本关系未在文件内声明。
- 最小修复：文件头注明目标 ABI 版本与期望（拒绝/接受），并在 harness 中校验来源哈希。
- 备注：`fn` 回调按值传递是该探针的核心约定。

### [低] 98-37｜wide ABI 探针同样无内嵌断言与版本标记
- 位置：`Test/SelfHost/fixtures/module-abi-wide/LegacyPhysical.krt:1-5`
- 类别：自举一致性 / 覆盖缺口
- 触发条件：uint128 的 value/ref/fn callback 三种约定仅由源文本表达，无 main/assert；目录另有旧对象 provenance（本批未审，非允许扩展名）。
- 后果：value-vs-ref 约定若被当前 ABI 悄然接受而 rejection gate 未跑，测试面为空。
- 最小修复：同 98-36，注明 ABI 版本与拒绝预期并绑定哈希。
- 备注：与 docs 描述的 rejection gate 对应，但该保证不在本文件内。

### [低] 98-38｜Box.krt 无直接断言
- 位置：`Test/SelfHost/examples/language-project/src/Box.krt:3-9`
- 类别：覆盖缺口
- 触发条件：泛型约束 `where T:struct`、静态 `Count`、构造自增仅由 `Main.krt:20` 间接覆盖。
- 后果：约束违反（引用类型实参）等负向场景无用例。
- 最小修复：增加约束负例与 Count 隔离断言（与 98-32 合并设计）。
- 备注：支撑文件。

### [低] 98-39｜Measurements.krt 无直接断言
- 位置：`Test/SelfHost/examples/dispatch-project/src/Measurements.krt:3-24`
- 类别：覆盖缺口
- 触发条件：接口继承、抽象方法、虚方法/override、泛型约束 `where T:IValue` 仅由 `Main.krt` 间接覆盖。
- 后果：接口分派与约束的负向回归漏检。
- 最小修复：补充抽象类未实现、约束不满足等负例。
- 备注：支撑文件；主逻辑断言在 Main.krt。
