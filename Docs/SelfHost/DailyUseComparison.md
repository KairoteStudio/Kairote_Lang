# SelfHost 与 Re.KrtC 的日常使用对比

2026-09-29：本轮把构造函数、foreach、数组字面量、命名空间、浮点、泛型、异常和工程构建接入原生编译链。随后继续补齐泛型静态字段、where 约束、单类继承与基类异常捕获、原生函数跨 KRO 链接，以及虚方法、抽象类和接口。日常程序可以组合使用这些能力；完整语言一致性仍需逐项验收。

## 可复现命令

```sh
python3 Test/SelfHost/Bootstrap.py
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" python3 -m unittest discover -s Test/SelfHost
python3 Test/SelfHost/RunStandardLibrary.py
python3 Test/SelfHost/CompareDaily.py --work build/selfhost-comparison/current --fail-on-wrong
./SelfHost/krtc build Test/SelfHost/examples/language-project
./Test/SelfHost/examples/language-project/bin/release/LanguageDemo
```

LanguageDemo 是实际的多文件工程，包含泛型构造、浮点数组、foreach、标准库调用、跨函数抛出异常及 finally 清理，同时验证具体泛型类型的静态计数、struct 约束和派生异常的基类捕获；预期输出 `average verified` 和 `empty input`，退出码为 0。

`Test/SelfHost/examples/dispatch-project` 的 DispatchDemo 进一步组合抽象基类、虚方法、base 调用、接口继承、泛型接口约束、接口数组及接口类型的异常捕获。使用同样的 build 命令后，输出 `reading verified` 和 `interface collection verified`，退出码为 0。

Python 驱动处理源码依赖、配置、缓存和链接；类型绑定、泛型实例化、异常展开、浮点和整数机器码、KRO 数据与符号由 Kairote 编译器生成。没有在编译失败时回退到 Re.KrtC，也没有用 Python 替换标准库实现。

## 本轮补充内容

| 需求 | 已接通的执行路径 | 当前边界 |
| --- | --- | --- |
| 构造函数与派发 | 两种构造声明、重载、参数/ref、字段初始化、单类继承、基类构造链；虚方法、抽象类和泛型类中的虚方法 | 派发枚举模块内具体类型；多个类基类、方法级泛型虚派发和跨模块虚表未实现；非虚方法按静态接收者类型选择 |
| 接口 | 多接口实现、接口多继承与泛型接口、精确签名匹配、接口引用调用、数组及异常捕获 | 尚无默认方法、显式接口实现和完整访问控制 |
| foreach | 数组和字符串、var/显式类型、一次求值、嵌套、break/continue | 字符串按 UTF-8 字节遍历；没有迭代器协议 |
| 数组字面量 | `[...]`、初始化表达式中的 `{...}`、推断/上下文类型、空数组、函数返回值、对象和字符串元素 | 没有锯齿数组和边界检查 |
| 命名空间 | 文件级/块级/嵌套 namespace、限定类型/函数/全局量、using、类型与命名空间别名、局部优先级和歧义检测 | 模块文件路径与声明命名空间可不同；访问控制尚未完整验收 |
| 标准库 | 原有 Console、Sys、Math、Convert、Memory、Array、String、StringBuilder 通过正常源码加载；增加 System.Exception | 以原有契约套件和新增专项测试为证据，不把一次导入成功当作全库语义证明 |
| 浮点与整数 | IEEE float32/float64 字面量、算术/比较/转换、数组/字段/ref/调用；int128/uint128；2–64 偶数位宽整数 | 浮点采用内部调用约定；128 位临时盒装分配尚未回收 |
| 泛型 | 类、函数与方法，显式参数/已有推断规则、嵌套实例、ref/数组、按具体类型隔离的静态字段，以及 class/struct/unmanaged/new()/基类/接口/类型参数约束 | 全部推断和访问控制尚未完成；实例数有限 |
| 异常 | throw、多个 typed catch、基类捕获、实际对象类型保留、rethrow、跨函数展开、finally 各种出口 | 类身份限于当前模块；跨 KRO 支持基本类型异常，库模式拒绝类/枚举异常；未捕获退出码 70 |
| 全局量和回调 | 可变全局量/静态字段、声明顺序初始化、类型化函数指针、自由函数/静态方法取址及间接调用 | 不包含闭包环境或捕获 lambda |
| 工程构建 | new/build/check/clean、Configure(Project p)/JSON、通配符、配置变量、依赖/工具哈希缓存、真实 extern 函数跨 KRO 链接、失败保留原产物 | 原生 ABI、独立链接及工程类型的具体边界见 [Projects.md](Projects.md) |

Math 的 checked power 实现改为显式检查乘法边界，避免依赖更宽的中间类型；标准库原有契约保持不变。数组和对象使用兼容的 16 字节分配头，`delete` 实际执行 munmap。数值专项另含独立整数预期和浮点位模式检查。

ArkLink 也修复了两处实际链接问题：命令行曾只保留最后一个输入对象；未定义符号曾被误认为第 0 段的有效定义。现在收集全部输入，并对缺少定义的引用返回链接错误。新增测试直接执行分别编译的产物，并检查真实符号、重定位、对象输入顺序和失败保留旧产物。

后续检查又修复了原生 `_KRT1$` 强符号重复定义随输入顺序静默选取实现的问题，现在两种输入顺序均报链接错误，并保留已有输出。类/接口等当前不支持跨模块传递的签名保持局部符号，避免泛型类型名碰撞。整数异常身份按声明位宽区分，`int30` 不再被 `int32` 捕获，对象字段也不会因 8 字节存储槽被误判为 int64。

## 差分验收方法

`CompareDaily.py` 仍使用原来的 62 项固定用例，包含合法和非法程序。期望输出与退出码独立定义，Re.KrtC 只是比较对象。SelfHost 使用现有的无优化后端，Re 使用 `-O2`；这不是等优化级别的性能比较。报告保留命令、stdout/stderr、超时、产物、编译器/驱动/库源码哈希；测试期间工具或库变化会令 `complete` 为 false。

`--fail-on-wrong` 检查错算、崩溃、超时、错误产物和非法接受。明确拒绝的合法用例另行统计，不能算作能力已支持。`.Length` 属性与 `.Length()` 方法分别测试；目前两种编译器都拒绝这两个 Length 方法形式。

早期版本的证据仍保存在 `build/selfhost-comparison/baseline/report.json`（旧编译器）和 `build/selfhost-comparison/final/report.json`（本轮语言扩展前）。固定集合中的正确执行项分别为 10 和 34；这些是历史数据，不能用作当前版本通过率。

最新 Stage2 的同一集合结果保存在 `build/selfhost-comparison/current/report.json`，`complete=true`：

| 62 项用例的结果 | 本轮扩展前 SelfHost | 当前 SelfHost | Re.KrtC |
| --- | ---: | ---: | ---: |
| 正确执行合法程序 | 34 | 43 | 40 |
| 正确拒绝非法程序 | 17 | 17 | 8 |
| 拒绝合法用例 | 11 | 2 | 3 |
| 生成程序但结果错误 | 0 | 0 | 2 |
| 错误接受非法程序 | 0 | 0 | 8 |
| 编译器崩溃 | 0 | 0 | 1 |
| 编译/执行超时 | 0 | 0 | 0 |

当前 SelfHost 的两项拒绝都是 `.Length()` 方法形式；对应的 `.Length` 属性正常执行。Re 的错值来自数组/字符串 Length 属性用例，崩溃来自未结束的块注释。这些结果只描述这个固定集合，不是完整语言的通过率。

原有 `Test/StandardLibrary` 的 59 项测试通过原生 Stage2 在 `-O0` 全部通过，报告位于 `build/selfhost-standard-library/report.json`，输入哈希在测试前后保持一致。同一套库用重新链接当前 ArkLink 的 Re.KrtC 在 `-O0` 也全部通过；兼容性报告见 `build/linkage-toolchain/compatibility-report.json`。编译器自举完成 138 次实际程序运行和 132 次重复错误诊断检查；Stage2/3/4 的 KRO SHA256 均为 `ca0be89f2906e9c72a8006d4b54b6bfbc0bdf9a989e515f82fc442331618a06d`。

当前自举证据读取 `build/selfhost/report.json`；191 项 SelfHost 回归全部通过，运行命令使用本次生成的原生 Stage2。完整日志、源码/工具哈希、库契约和差分结果见 `build/selfhost/verification.json` 及其引用的报告。Stage 2/3/4 的 KRO 需要逐字节一致，且当前源码需与报告快照相符。仅有 seed 或单代程序通过不构成完整自举。

## 仍需补充

1. 结构体值语义、闭包、完整访问控制、方法级泛型虚派发、接口默认方法和显式接口实现；已有虚派发和接口约束不能替代完整类型系统。
2. 跨模块类身份、动态全局初始化、通用 C/外部 ABI、共享库和包管理。现有 `_KRT1$` 签名符号支持原生函数库的分别编译和链接，泛型特化保持局部，源码模板需参与实例化。
3. 数组边界检查、统一分配器及长期内存管理。小对象逐个 mmap；拼接字符串及 128 位临时盒装对象会保留内存。
4. 优化流水线、调试信息和其他目标。当前仅 Linux x86-64、`-O0`、栈式 IR；`-O1..3` 明确拒绝，未实现完整 SSA。
5. 更细的诊断。参数数量、可写 ref、数组三元类型、不可空赋值、泛型约束和未定义方法已分别报告，部分名称/类型/未实现能力仍共用 E_LOWER。
6. 容量：源码与全部导入合计 1 MiB、最多 32 参数（含接收者）、1024 个具体函数和固定的泛型实例容量。

之前文档的编译/运行耗时属于旧编译器样本，不能当作新增异常、泛型、浮点及宽整数实现的性能。当前性能样本随差分报告保存；CLI 计时包含 Python 启动和链接，运行计时包含进程启动，不能直接等同于编译核心吞吐量。
