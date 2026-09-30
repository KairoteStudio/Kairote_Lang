# 语法一致性验收

目标是逐项覆盖 Re.KrtC 实际接受并正确执行的语法。SelfHost 已能编译完整自身源码，也已补入构造函数、集合、命名空间、泛型、异常和更多数值类型；这仍不表示全部语言能力与 Re.KrtC 等价。

## 验收规则

- 原版依据是 `Re.KrtC/src/compiler/Frontend/Parser/` 的可达入口及实际执行结果；Token 或 AST 枚举存在不构成支持证据。
- 解析需要保留声明、类型、参数、子表达式及源码位置。跳过 Token 或平衡大括号不能算成功实现。
- 正例分别验证 AST、绑定和实际程序行为；负例必须在有限时间内拒绝，且失败不得留下可误用的新产物。
- 自编译另行验证完整源码生成后续编译器、跨代固定点和执行结果。Stage 1 探针通过不能替代完整自举。

## 已有执行验证的范围

| 能力 | 当前范围 | 主要证据 |
| --- | --- | --- |
| 类与构造 | 字段、方法、重载、隐式 this、构造参数/ref、字段初始化、默认初始化；对象方法和字段参与原生生成。 | `test_native_collections.py`、`test_native_binding.py` |
| 单类继承 | 基类字段前缀布局、构造链与初始化顺序、base 成员访问、向上转换、限定/泛型基类，以及保留实际派生类型的异常捕获。 | `test_inheritance.py` |
| 虚方法与接口 | virtual/override/abstract、抽象实现检查、base 直接调用、泛型类虚方法；多接口实现、接口继承/泛型接口/约束、签名检查、接口数组和异常引用。 | `test_virtual_dispatch.py`、`test_interfaces.py`、`test_language_project.py` |
| 数组字面量与 foreach | `[...]`、初始化表达式中的 `{...}`、上下文类型和推断、空数组、对象/字符串元素；数组/字符串遍历、一次求值、作用域、嵌套与循环跳转。 | `test_native_collections.py` |
| 命名空间 | 文件级、块级、嵌套作用域、限定类型和自由函数名称、using、类型/命名空间别名，以及重复和歧义诊断。 | `test_namespaces.py`、`test_driver.py` |
| 泛型 | 类、函数及方法的具体实例生成；显式参数与已有推断规则、嵌套实例、构造、静态调用、ref、数组、每个具体类型独立的静态字段、where 类型及无参构造约束。 | `test_native_generics.py`、`test_generic_statics.py`、`test_generic_constraints.py` |
| 异常 | 跨函数 throw、类/基类匹配及标量精确匹配的 catch、多个 catch、catch-all、rethrow、finally；覆盖正常出口、未匹配异常、catch 内抛出及 return/break/continue。 | `test_native_exceptions.py` |
| 数值 | 2–64 偶数整数位宽及符号规则、int128/uint128、float32/float64 的字面量、运算、比较、转换与存储。 | `test_native_numeric.py`、`test_native_floating.py` |
| 全局状态与回调 | 可变全局量/静态字段、初始化顺序、函数与静态方法取址、类型化函数指针、间接调用和 ref。 | `test_globals_functions.py` |
| 驱动与库 | 多文件、源码导入、文件边界、位置诊断、工程构建/缓存；真实仓库标准库使用原有契约测试验收。 | `test_driver.py`、`test_project.py`、`run_standard_library.py` |

表中测试位于 `Test/SelfHost/`。它们证明所断言的行为，不证明整行能力已经覆盖原版的所有变体。字符串索引、Length 和 foreach 使用 UTF-8 字节语义。

三元分支使用独立 `alternate`，循环体使用独立 `body`，避免与兄弟节点链混用。类型、泛型参数和命名空间作用域进入绑定与实例化过程；未使用模板不会被当作已生成的机器码函数。

## 尚未通过完整一致性验收

- 已实现单类继承、虚派发和接口；多个类基类和结构体值语义仍未实现。派发以当前模块的实际类身份选择目标，跨模块类 ABI、方法级泛型虚派发、接口默认实现和显式接口实现尚未支持。未经检查的类/接口向下转换会拒绝；非虚方法隐藏按静态接收者类型选择，数组与 ref 参数不允许协变。
- 已支持 class/struct/unmanaged/new()、基类/接口和类型参数间的 where 约束；全部类型推断、访问控制及其他完整类型系统规则尚未覆盖。struct 约束涵盖当前标量与枚举，结构体布局另行实现；具体实例数量有限。
- foreach 当前面向数组和字符串，没有完整迭代器协议；锯齿数组、闭包和捕获 lambda 未实现。lambda、fixed、lock、yield、await、switch 等已有解析入口或 AST 的语法，需要单独的执行验收，不能据此宣称支持。
- 类异常可按实际对象类型捕获本类或基类，标量异常按类型精确匹配。`throw null` 字面量、catch 外 rethrow 等会拒绝；运行时为空的类引用不匹配类型 catch，但可被 catch-all 捕获。未捕获异常以状态 70 退出。
- 原生 extern 调用通过稳定签名符号和 KRO 重定位支持分别编译的函数库。通用 C/系统 ABI、共享库、跨模块类身份和动态全局初始化尚未完成；具体边界见 [Projects.md](Projects.md)。
- 原生优化流水线尚未实现，只接受 `-O0`。工程命令已经存在，具体工程类型以 [Projects.md](Projects.md) 为准；不提供任意构建脚本或包下载。
- 数组缺少边界检查；字符串拼接和 128 位数值内部盒装分配尚无 GC 或完整生命周期回收。语法和数值结果正确，不代表具有与 Re.KrtC 相同的性能和内存成本。

## 运行证据

```sh
python3 -m unittest Test.SelfHost.test_parser Test.SelfHost.test_contract Test.SelfHost.test_lexer
python3 -m unittest Test.SelfHost.test_native_collections Test.SelfHost.test_native_generics \
  Test.SelfHost.test_native_exceptions Test.SelfHost.test_inheritance Test.SelfHost.test_namespaces
python3 Test/SelfHost/bootstrap.py
python3 Test/SelfHost/run_standard_library.py --compiler build/selfhost/stage2/program
```

`test_parser` 包含函数关键字/跨度、循环体与兄弟关系、lambda 参数保留、类型元表达式以及部分 Token 和结合方向检查；这些属于解析证据。集合、泛型、异常和命名空间测试支持 `SELFHOST_COMPILER`，可显式指定已生成的 Stage 2 进行执行验证。

自举报告 `build/selfhost/report.json` 记录实际源码与工具哈希、Stage 2/3/4 固定点、各代正例运行及重复负例诊断。标准库报告位于 `build/selfhost-standard-library/report.json`，具体命令可用 `--work` 选择其他目录。应使用与当前源码一致的报告，不能把旧报告的通过数量当作当前验收结论。

工程配置见 [Projects.md](Projects.md)，日常使用差分结果见 [DailyUseComparison.md](DailyUseComparison.md)。
