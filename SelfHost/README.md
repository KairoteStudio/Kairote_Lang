# SelfHost 编译器

Kairote 编写的 Linux x86-64 编译器直接生成 KRO，再由 ArkLink 链接。完整 `SelfHost/**/*.krt` 源码参与自编译：Stage 0 生成 Stage 1，随后生成 Stage 2、Stage 3 和用于固定点检查的 Stage 4。验证要求 Stage 2、3、4 的 KRO 逐字节相同，并在生成的编译器上执行程序、比较输出和错误诊断。

这说明已实现的编译器能够完整自举。与 Re.KrtC 的语言、优化和独立链接能力是否一致，需要分别验收，不能由自举成功推导。

## 运行自举

在仓库根目录执行：

```sh
cmake -S . -B build
cmake --build build --target KrtC ArkLink -j4
python3 Test/SelfHost/Bootstrap.py
```

脚本优先使用上述构建产物，也支持 `KRTC`、`ARKLINK` 环境变量。只有生成 Stage 1 时调用 C 编写的 Stage 0；后续编译运行生成的 Kairote 编译器，链接运行 ArkLink。后续子进程使用空 PATH，没有中间 C 文件或 Stage 0 回退。

`build/selfhost/report.json` 保存命令、源码与工具哈希、各代 KRO 哈希、程序运行及诊断结果；`Compiler.krt` 是该次验证使用的源码快照。各代编译器位于 `stage2/program`、`stage3/program`、`stage4-check/program`。只有全部检查通过，报告才标记 `complete: true`。判断报告是否仍适用，还需核对源码快照与当前 `SelfHost/**/*.krt`。`--probes-only` 用于开发检查，不会标记完整自举完成。

## 日常编译

完成自举后，默认使用 Stage 2：

```sh
./SelfHost/krtc program.krt -o program
./program
./SelfHost/krtc helper.krt main.krt -o combined
./SelfHost/krtc program.krt -c -o program.kro
./SelfHost/krtc --check program.krt
./SelfHost/krtc -I libs Test/SelfHost/examples/daily.krt -o build/daily
```

也可以使用 `python3 SelfHost/Compile.py`。省略 `-o` 时，在当前目录生成首个输入的文件名主干；`-c` 默认增加 `.kro` 后缀。兼容 `output 路径`，支持带空格的路径。

`-I` 添加模块搜索目录，默认包含仓库 `libs`。加载器支持文件级 `using System.Console;`、`using System;`、`using System.*;`、using 别名和 `import "helper.krt";`。依赖按确定顺序加载，同一文件只加载一次；循环依赖共享声明，缺失依赖会报错。每个文件的 namespace 和 using 作用域单独保存。

Python 驱动负责依赖加载、源码位置映射、临时文件、工程缓存和链接；词法、语法、绑定、IR 与机器码生成均由 Kairote 编译器完成。错误会映射回原文件的 UTF-8 行列。编译或链接失败不会覆盖已有输出。多个源码先编译为一个模块，也可传入已有 `.kro` 参与链接。对象输出、跨模块符号和链接目标的支持范围见 [工程构建](../Docs/SelfHost/Projects.md)；不能将内部调用约定等同于完整外部 ABI。

`--compiler` 或 `SELFHOST_COMPILER` 可选择其他原生编译器。原生可执行文件本身使用固定文件协议：读取当前目录的 `program.krt`，成功后写出 `stage1-probe.kro`。当前支持 `-O0`；`-O1`、`-O2`、`-O3` 明确报错。

## 工程与标准库

```sh
./SelfHost/krtc new console /tmp/kairote-example
./SelfHost/krtc build /tmp/kairote-example
./SelfHost/krtc check /tmp/kairote-example
./SelfHost/krtc clean /tmp/kairote-example
```

仓库中也提供了组合使用新语言能力的 [LanguageDemo 工程](../Test/SelfHost/examples/language-project/project.krt)：

```sh
./SelfHost/krtc build Test/SelfHost/examples/language-project
./Test/SelfHost/examples/language-project/bin/release/LanguageDemo
```

[DispatchDemo 工程](../Test/SelfHost/examples/dispatch-project/project.krt) 演示抽象基类、虚方法、接口继承、接口泛型约束、接口数组及接口异常捕获：

```sh
./SelfHost/krtc build Test/SelfHost/examples/dispatch-project
./Test/SelfHost/examples/dispatch-project/bin/release/DispatchDemo
```

工程驱动接受 `project.krt` 的 `Configure(Project p)` 配置和 `project.json`，提供源码通配符、模块搜索路径、源码库、输出路径、配置变量及缓存。缓存包含源码、依赖、工具与配置的哈希。具体配置、增量构建和 clean 行为见 [工程构建](../Docs/SelfHost/Projects.md)。

标准库从仓库 `libs` 加载，经过同一原生编译路径；没有在 Python 中替代 Console、Math、Memory、Array、String、Convert 或 StringBuilder 的实现。`System.Exception` 提供普通对象形式的异常载荷，包含 Message、Code 和 ToString。完整库契约的覆盖情况以运行结果为准：

```sh
python3 Test/SelfHost/RunStandardLibrary.py --compiler build/selfhost/stage2/program
```

该命令使用原有 `Test/StandardLibrary` 契约测试，保存编译器与库源码哈希、每项结果及日志。成功加载某个模块不等于其全部契约已经通过。

## 已有原生执行路径

| 范围 | 实现 |
| --- | --- |
| 对象与调用 | 类字段、静态/实例方法、隐式 this、重载、构造函数、字段初始化、默认初始化、递归及 ref 写回；单类继承、限定/泛型基类、基类构造链、base 成员访问及向上转换；virtual/override/abstract、接口实现及接口继承。 |
| 集合与循环 | 类型化数组、`[...]` 和初始化表达式中的 `{...}`、类型推断、空数组、数组返回值、对象/字符串元素；foreach 遍历数组或字符串，集合只求值一次，支持嵌套、break 和 continue。 |
| 名称与泛型 | 文件级、块级、嵌套 namespace，限定类型/函数名称、using 和别名；泛型类、函数、方法，显式类型参数和已有推断规则，嵌套实例、泛型构造函数、ref、数组和静态调用。泛型通过生成具体实例执行；支持 where 约束与每个具体类型独立的静态字段。 |
| 数值 | 2–64 偶数位宽有符号与无符号整数（含 int30 等），以及 int128/uint128；float32/float64 字面量、算术、比较和转换，含数组、字段、参数及返回值。 |
| 异常 | throw、类型捕获、多个 catch、catch-all、rethrow、finally；支持跨函数展开以及 return、break、continue 时执行 finally。类异常按对象实际类型匹配本类和基类，即使通过基类变量抛出或重抛也保留派生类型；标量异常按类型精确匹配。 |
| 程序状态与回调 | 可变全局量、类静态字段及按声明顺序执行的初始化；`fn(...) -> ...` 函数指针、自由函数/静态方法取址、带 ref 的间接调用。 |
| 字符串与内存 | 内容相等比较、双字符串拼接、只读 Length、对象/数组分配、栈分配、类型化指针步长、动态系统调用，以及实际释放对象和数组的 delete。 |

字符串 Length 和 foreach 按 UTF-8 字节处理，不按 Unicode 字符处理。字符串与空指针比较安全；拼接将空指针当空串，空字符串引用的长度为 0。数组 Length 只读。堆分配检查负数、大小溢出和系统调用失败，失败返回空指针；`delete null` 安全，不能用 delete 释放字符串或栈地址。

泛型约束使用 `where T : class`、`struct`、`unmanaged`、`new()`、基类/接口名或另一个类型参数；可以组合约束，并用于类、函数和方法。实例化前验证实际类型，失败报告 `E_GENERIC_CONSTRAINT`。`struct` 约束目前面向已实现的标量与枚举值类型，不表示结构体布局已经完成。`new T()` 支持无参类构造及标量零值；接口和抽象类不满足 `new()`。

虚方法和接口调用根据对象实际类型选择实现；接收者只求值一次，`base.Method()` 直接调用基类实现。非虚方法隐藏仍按静态接收者类型选择。接口支持多个父接口、泛型接口、继承的实现、重载和 ref 参数；签名必须匹配，缺失实现及冲突契约会报错。继承的接口映射随虚方法 override 更新，普通同名方法隐藏不替换它；派生类重新声明实现该接口时建立新映射。接口引用保持原对象指针，可用于数组、delete、throw 和 catch。虚方法或接口调用的空接收者以状态 126 退出。

派发代码枚举当前模块内的具体类，暂不使用可跨模块扩展的虚表；泛型类中的普通虚方法可实例化，但方法自身的泛型参数、接口默认方法和显式接口实现尚未支持。

泛型静态字段按具体类型分别分配存储，启动时按源码顺序初始化一次；继承访问共享所属基类具体实例的静态字段。未使用的模板不分配存储，初始化顺序不依赖运行时首次访问。

原生 IR 显式表示调用、控制流、内存与异常处理。后端验证跳转目标、操作数栈高度及局部槽位，非法 IR 不会被静默修正。

## 验证与限制

```sh
python3 -m unittest discover -s Test/SelfHost
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" python3 -m unittest \
  Test.SelfHost.test_native_collections Test.SelfHost.test_native_generics \
  Test.SelfHost.test_native_exceptions Test.SelfHost.test_inheritance Test.SelfHost.test_namespaces
```

专项测试覆盖构造与初始化顺序、集合求值顺序、循环作用域、泛型实例隔离、类型/引用不匹配、跨调用异常展开、finally 的多种出口，以及实际机器码运行。具体测试数量、源码版本和通过情况应读取相应报告，不使用文档中的固定数量代替验收。语法边界见 [SyntaxParity.md](../Docs/SelfHost/SyntaxParity.md)，实际对照结果见 [日常使用对比](../Docs/SelfHost/DailyUseComparison.md)。

仍未完成的能力包括结构体值语义、闭包/捕获 lambda、完整迭代器协议、全部类型推断和访问控制规则、通用外部 ABI，以及优化流水线。原生 extern 函数可以通过 KRO 未定义符号和重定位链接；具体支持的参数类型及库边界见工程文档。当前未捕获异常以状态 70 退出。单继承保持基类字段前缀和对象身份；多个类基类和未经检查的类/接口向下转换会拒绝。锯齿数组、字符串 `+=`、数组边界检查和 GC 尚未实现。

所有输入源码及导入合计上限为 1 MiB；参数最多 32 个（实例接收者计入），具体函数最多 1024 个，泛型实例存在固定容量限制。原生 IR 仍为栈式，没有完整 SSA 优化架构。堆对象分别使用匿名映射，小对象开销较高；拼接字符串和 128 位运算的内部盒装分配会保留内存，长时间运行需考虑这一限制。旧紧凑对象 API 仍用于既有二进制契约测试。
