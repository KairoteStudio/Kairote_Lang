# 语法一致性验收

目标是逐项覆盖 Re.KrtC 实际接受并正确执行的语法。SelfHost 已能编译完整自身源码，也已补入构造函数、集合、命名空间、泛型、异常和更多数值类型；这仍不表示全部语言能力与 Re.KrtC 等价。

## 验收规则

- 原版依据是 `Re.KrtC/src/Compiler/Frontend/Parser/` 的可达入口及实际执行结果；Token 或 AST 枚举存在不构成支持证据。
- 解析需要保留声明、类型、参数、子表达式及源码位置。跳过 Token 或平衡大括号不能算成功实现。
- 正例分别验证 AST、绑定和实际程序行为；负例必须在有限时间内拒绝，且失败不得留下可误用的新产物。
- 自编译另行验证完整源码生成后续编译器、跨代固定点和执行结果。Stage 1 探针通过不能替代完整自举。

## 已有执行验证的范围

| 能力 | 当前范围 | 主要证据 |
| --- | --- | --- |
| Lexer 词表 | 与 C 词表数值一致的 299 个细分类 Token；244 个关键字/类型拼写，全部 2–128 偶数整数位宽和 52 个运算符/标点。调用方使用 category 保留语法形状分类。 | `test_lexer_vocabulary.py`，详见 [TokenVocabulary.md](TokenVocabulary.md) |
| Parser 结构和恢复 | 独立 typed AST 探针、类型子节点、源码顺序 namespace/using 节点、块作用域；多条诊断、语句/声明同步和错误后的合法 AST。 | `test_parser.py`，详见 [Parser.md](Parser.md) |
| 类与构造 | 字段、方法、重载、隐式 this、构造参数/ref、字段初始化、默认初始化；对象方法和字段参与原生生成。 | `test_native_collections.py`、`test_native_binding.py` |
| 单类继承 | 基类字段前缀布局、构造链与初始化顺序、base 成员访问、向上转换、限定/泛型基类，以及保留实际派生类型的异常捕获。 | `test_inheritance.py` |
| 虚方法与接口 | virtual/override/abstract、抽象实现检查、base 直接调用、泛型类虚方法；多接口实现、接口继承/泛型接口/约束、签名检查、接口数组和异常引用。 | `test_virtual_dispatch.py`、`test_interfaces.py`、`test_language_project.py` |
| 数组字面量与 foreach | `[...]`、初始化表达式中的 `{...}`、上下文类型和推断、空数组、对象/字符串元素；直接/间接调用、构造与重载逐元素匹配字面量，保留元素完整类型和源码求值顺序；数组/字符串遍历、一次求值、作用域、嵌套与循环跳转。 | `test_native_collections.py`、`test_contextual_arguments.py` |
| 命名空间 | 文件级、块级、嵌套作用域、限定类型和自由函数名称、using、类型/命名空间别名，以及重复和歧义诊断。 | `test_namespaces.py`、`test_driver.py` |
| 泛型 | 类、函数及方法的具体实例生成；显式参数与已有推断规则、嵌套实例、构造、静态调用、ref、数组、每个具体类型独立的静态字段、where 类型及无参构造约束。 | `test_native_generics.py`、`test_generic_statics.py`、`test_generic_constraints.py` |
| 异常 | 跨函数 throw、类/基类匹配及标量精确匹配的 catch、多个 catch、catch-all、rethrow、finally；覆盖正常出口、未匹配异常、catch 内抛出及 return/break/continue。 | `test_native_exceptions.py` |
| switch | 选择器一次求值、表达式标签按顺序比较、default、分组标签与顺序贯穿、嵌套循环的 break/continue、finally 清理；整数、字符串/null、字符、浮点和 128 位数值。 | `test_native_switch.py` |
| 数值 | 2–128 偶数整数位宽及符号规则，含 66–126 位的精度截断、符号扩展和自增；float32/float64 的字面量、运算、比较、转换与存储。 | `test_native_numeric.py`、`test_native_floating.py`、`test_wide_precision.py` |
| 二进制字面量 | `0b`/`0B`、64/128 位整数、枚举与运算，缺少数字、非法数字/后缀和 128 位溢出诊断。 | `test_binary_literals.py`、`test_lexer.krt` |
| 全局状态与回调 | 可变全局量/静态字段、初始化顺序、函数与静态方法取址、类型化函数指针、间接调用和 ref。 | `test_globals_functions.py` |
| 闭包与 lambda | function/func/fn 的表达式与块体、上下文参数、完整返回类型；共享词法 cell、嵌套与逃逸捕获、循环/模式/catch 作用域、结构体与宽整数布局、所有退出清理及手动 delete；原生 SSA/r10、独立 KRO fn2 契约和 EBC v4。 | `test_closures.py`、`test_closure_patterns.py`、`test_closure_inference.py`、`test_http_server.py`；语义及验收范围见 [Closures.md](Closures.md) |
| 空值运算 | `??` 右结合、结果完整类型与右侧懒求值；`?.`、`?[...]` 整条未分组后缀链的单次接收者求值与短路，零值结果、结构体/固定数组独立复制，以及动态选择回调的参数名契约。 | `test_nullable_operators.py`、`test_nested_callback_contracts.py`、`test_http_server.py`；语义见 [README](../../SelfHost/README.md#空值运算与循环变量) |
| 循环变量与 void 值 | 普通声明和 foreach 共用 var/auto/let 推断，完整 char 元素类型与只读结构体防御副本；void 调用可单独执行，不能被当作其他表达式的值。 | `test_foreach_bindings.py`、`test_void_values.py` |
| 指针空值与层级 | 源码顺序后缀、完整内外空值层、泛型回调、精确 ref、非空类型模式，以及直接/间接调用的一致匹配。规范元素布局用于指针算术和索引更新。 | `test_indirect_arguments.py`、`test_struct_pointer_layout.py`，见 [Pointers.md](Pointers.md) |
| 不可达语句 | 合法早返回按 CFG 出口生成；完整函数体仍验证名称、类型和循环/选择目标，保留 switch 贯穿及 finally 清理。 | `test_control_flow.py` |
| 内联值与字符串更新 | 结构体、嵌套固定数组、固定数组泛型值的复制与隐藏返回槽；无隐式布尔转换的值不能作为控制条件。字符串 `+=` 保留左值一次求值和原值捕获，支持局部量、字段、数组元素、ref、null。 | `test_struct_values.py`、`test_fixed_generic_values.py`、`test_aggregate_conditions.py`、`test_string_compound.py` |
| 恢复的语义路径 | 块级 using 隔离和模块别名、继承循环路径、多函数诊断、命名数组、auto/var/let、只读局部量、point、前后自增、is 类型模式和原生 print。 | `test_semantic_restoration.py` |
| 驱动与库 | 原生 CLI、多文件、源码导入、文件边界、位置诊断、工程构建/缓存；真实仓库标准库使用原有契约测试验收。 | `test_native_driver.py`、`test_driver.py`、`test_project.py`、`RunStandardLibrary.py` |

表中测试位于 `Test/SelfHost/`。它们证明所断言的行为，不证明整行能力已经覆盖原版的所有变体。字符串索引、Length 和 foreach 使用 UTF-8 字节语义。

三元分支使用独立 `alternate`，循环体使用独立 `body`，避免与兄弟节点链混用。类型、泛型参数和命名空间作用域进入绑定与实例化过程；未使用模板不会被当作已生成的机器码函数。

## 尚未通过完整一致性验收

- 已实现单类继承、虚派发、接口及[内联结构体值语义](Structs.md)。公有具体结构体（含闭泛型结构体）的跨对象调用使用 [ABI2 布局契约](Projects.md#struct-objects-and-abi2)。多个类基类、结构体接口和装箱尚未支持。派发以当前模块的实际类身份选择目标，跨模块类/接口/枚举 ABI、方法级泛型虚派发、接口默认实现和显式接口实现尚未支持。未经检查的类/接口向下转换会拒绝；非虚方法隐藏按静态接收者类型选择，已存储数组与 ref 参数不允许协变。数组字面量可以按选定参数的元素类型构造。
- 已支持 class/struct/unmanaged/new()、基类/接口和类型参数间的 where 约束，以及[访问控制](AccessControl.md)中的文件私有、成员和继承规则；全部类型推断及其他完整类型系统规则尚未覆盖。struct 约束涵盖标量、枚举与结构体，unmanaged 检查嵌套内联字段；具体实例数量有限。
- foreach 当前面向数组和字符串，没有完整迭代器协议；锯齿数组尚未实现。SelfHost 保留 fixed/lock/yield/await 的解析节点，但节点存在不代表运行语义。此前对 Re.KrtC 的只读审计发现其 lambda 可达却没有正确可调用值，其余多项只有 Token/枚举或恢复时丢失语法；这份历史审计不能替代本次 SelfHost 闭包执行验收。实际差分和完整源码引用见 [FrontendParityAudit.md](FrontendParityAudit.md)。switch 对静态重复标签/default 和不兼容标签进行诊断；表达式标签仍允许运行时计算。
- 类异常可按实际对象类型捕获本类或基类，标量异常按类型精确匹配。`throw null` 字面量、catch 外 rethrow 等会拒绝；运行时为空的类引用不匹配类型 catch，但可被 catch-all 捕获。未捕获异常以状态 70 退出。
- 原生 extern 调用通过稳定签名符号和 KRO 重定位支持分别编译的函数库。通用 C/系统 ABI、共享库、跨模块类身份和独立库的动态全局初始化尚未完成；具体边界见 [Projects.md](Projects.md)。
- 原生优化支持 `-O0` 到 `-O3`，默认 `-O2`，包括整数常量与转换折叠、常量分支简化、SSA 死值消除、安全的直接自身尾调用消除和受限小函数内联。原生 KRO 后端直接消费带值编号和 phi 的 SSA，通过活跃性分析分配寄存器及溢出栈槽。工程命令的具体工程类型以 [Projects.md](Projects.md) 为准；不提供任意构建脚本或包下载。
- 数组缺少边界检查；字符串拼接和 128 位数值内部盒装分配尚无 GC 或完整生命周期回收。语法和数值结果正确，不代表具有与 Re.KrtC 相同的性能和内存成本。

## 运行证据

```sh
python3 -m unittest Test.SelfHost.test_parser Test.SelfHost.test_contract Test.SelfHost.test_lexer
python3 -m unittest Test.SelfHost.test_native_collections Test.SelfHost.test_native_generics \
  Test.SelfHost.test_native_exceptions Test.SelfHost.test_inheritance Test.SelfHost.test_namespaces
python3 Test/SelfHost/Bootstrap.py
python3 Test/SelfHost/RunStandardLibrary.py --compiler build/selfhost/stage2/program
```

`test_parser` 包含实际类型 AST 前瞻、using frame、源码顺序节点、错误同步/多条诊断、后续有效声明及 64 条错误上限，并保留函数、分支、循环和转换等子节点。AST 探针支持 `SELFHOST_COMPILER`，可以运行原生生成后的 parser 程序；套件中的独立种子回归仍直接调用 `KRTC`。探针使用实际组件的依赖闭包，`CompilerState.krt` 与 `DiagnosticsState.krt` 提供完整共享状态和诊断记录，避免记录字段引入无关运行算法；这些属于解析结构证据。集合、泛型、异常和命名空间测试支持 `SELFHOST_COMPILER`，可显式指定已生成的 Stage 2 进行执行验证。

自举报告 `build/selfhost/report.json` 记录实际源码与工具哈希、Stage 2/3/4 固定点、各代正例运行、重复负例诊断和原生 CLI 测试。只有 Stage 0 的种子输入由测试脚本拼接；后续各代直接通过原生 CLI 读取原始 Kairote 文件，依赖加载、工程配置、缓存、诊断和链接调用全部参与自编译。Python 仅用于测试与证据记录。标准库报告位于 `build/selfhost-standard-library/report.json`，具体命令可用 `--work` 选择其他目录。应使用与当前源码一致的报告，不能把旧报告的通过数量当作当前验收结论。

工程配置见 [Projects.md](Projects.md)，日常使用差分结果见 [DailyUseComparison.md](DailyUseComparison.md)。
