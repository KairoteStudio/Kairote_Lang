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

脚本优先使用上述构建产物，也支持 `KRTC`、`ARKLINK` 环境变量。只有生成 Stage 1 时拼接种子输入并调用 C 编写的 Stage 0；Stage 2、3、4 由生成的 Kairote 编译器通过原生 CLI 直接读取完整原始 `.krt` 文件列表，链接运行 ArkLink。后续子进程使用空 PATH。Python 只运行测试、调用工具和记录证据。

`build/selfhost/report.json` 保存命令、逐文件源码与工具哈希、各代 KRO 哈希、程序运行、诊断及原生 CLI 测试结果；`Compiler.krt` 是 Stage 0 使用的种子输入快照。各代编译器位于 `stage2/program`、`stage3/program`、`stage4-check/program`。只有全部检查通过且源码哈希保持一致，报告才标记 `complete: true`。判断报告是否仍适用，还需核对报告中的文件哈希与当前 `SelfHost/**/*.krt`。`--probes-only` 用于开发检查，不会标记完整自举完成。

生成链已完成、验收因运行环境中断时，可执行 `python3 Test/SelfHost/Bootstrap.py --resume-native-driver`，或显式运行 `python -m unittest Test.SelfHost.BootstrapResume -v`。恢复会核验原源码、工具、生成物、程序探针和诊断证据，再完整运行 Stage 2、Stage 3 的原生验收，包含真实 HTTP 请求。它保留历史失败记录；输入或生成物改变时拒绝恢复，必须重新生成自举链。

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

`SelfHost/krtc` 是直接执行原生编译器的 shell 启动器。也可直接运行 `build/selfhost/stage2/program`，参数相同。省略 `-o` 时，在当前目录生成首个输入的文件名主干；`-c` 默认增加 `.kro` 后缀。兼容 `output 路径`，支持带空格的路径。日常编译、工程命令和链接均不依赖 Python。

`-I` 添加模块搜索目录，默认包含仓库 `libs`。加载器支持文件级 `using System.Console;`、`using System;`、`using System.*;`、using 别名和 `import "helper.krt";`。依赖按确定顺序加载，同一文件只加载一次；循环依赖共享声明，缺失依赖会报错。每个文件的 namespace 和 using 作用域单独保存。

Kairote 原生驱动负责命令行参数、依赖加载、源码位置映射、临时文件、工程配置、缓存和链接进程；词法、语法、绑定、IR 与机器码生成也在该可执行文件内完成。错误会映射回原文件的 UTF-8 行列。编译或链接失败不会覆盖已有输出。多个源码先编译为一个模块，也可传入已有 `.kro` 参与链接。对象输出、跨模块符号和链接目标的支持范围见 [工程构建](../Docs/SelfHost/Projects.md)；不能将内部调用约定等同于完整外部 ABI。

空文件和纯注释文件可以生成 KRO，并参与工程链接；生成可执行文件仍需要入口，缺少入口报告 `E_MODULE`。

`--compiler` 或 `SELFHOST_COMPILER` 可选择其他原生编译器。原生可执行文件直接接受源码路径、`-I`、`-c`、`--check`、`-o`、`--linker` 及工程命令。仅既有内部编译器测试保留无参数的固定文件协议：读取 `program.krt` 并写出 `stage1-probe.kro`。支持 `-O0` 到 `-O3`，命令行默认 `-O2`。

## 工程与标准库

```sh
./SelfHost/krtc new console /tmp/kairote-example
./SelfHost/krtc build /tmp/kairote-example
./SelfHost/krtc build /tmp/kairote-example -j4
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

并行构建由原生进程管理编译任务，默认最多 8 个 worker；`-jN`、`-j N` 或 `--jobs N` 支持 1–256。父进程等待 worker、汇总诊断并发布产物，失败时保留已有输出与缓存清单。

标准库从仓库 `libs` 加载，经过同一原生编译路径；没有在 Python 中替代 Console、Math、Memory、Array、String、Convert 或 StringBuilder 的实现。`System.Exception` 提供普通对象形式的异常载荷，包含 Message、Code 和 ToString。完整库契约的覆盖情况以运行结果为准：

```sh
python3 Test/SelfHost/RunStandardLibrary.py --compiler build/selfhost/stage2/program
```

该命令使用原有 `Test/StandardLibrary` 契约测试，保存编译器与库源码哈希、每项结果及日志。成功加载某个模块不等于其全部契约已经通过。

## 已有原生执行路径

| 范围 | 实现 |
| --- | --- |
| 对象与调用 | 类字段、静态/实例方法、隐式 this、重载、命名参数、构造函数、字段初始化、默认初始化、递归及 ref 写回；单类继承、限定/泛型基类、基类构造链、base 成员访问及向上转换；virtual/override/abstract、接口实现及接口继承。 |
| 集合与循环 | 类型化数组、`[...]` 和初始化表达式中的 `{...}`、类型推断、空数组、数组返回值、对象/字符串元素；foreach 遍历数组或字符串，集合只求值一次，支持嵌套、break 和 continue。 |
| 名称与泛型 | 文件级、块级、嵌套 namespace，限定类型/函数名称、using 和别名；泛型类、函数、方法，显式类型参数和已有推断规则，嵌套实例、泛型构造函数、ref、数组和静态调用。泛型通过生成具体实例执行；支持 where 约束与每个具体类型独立的静态字段。 |
| 数值 | 2–128 偶数位宽有符号与无符号整数（含 int30、int66 等），转换与存储按声明精度截断和符号扩展；float32/float64 字面量、算术、比较和转换，含数组、字段、参数及返回值。 |
| 异常 | throw、类型捕获、多个 catch、catch-all、rethrow、finally；支持跨函数展开以及 return、break、continue 时执行 finally。类异常按对象实际类型匹配本类和基类，即使通过基类变量抛出或重抛也保留派生类型；标量异常按类型精确匹配。 |
| 程序状态与回调 | 可变全局量、类静态字段及按声明顺序执行的初始化；`fn(...) -> ...` 函数指针、自由函数/静态方法取址、带 ref 的间接调用。 |
| 字符串与内存 | 内容相等比较、双字符串拼接、只读 Length、对象/数组分配、栈分配、类型化指针步长、动态系统调用，以及实际释放对象和数组的 delete。 |
| 指针类型 | 完整指针空值层、源码顺序类型后缀、直接/间接调用的统一参数规则、精确 ref、非空类型模式与规范元素布局。规则和示例见 [指针类型](../Docs/SelfHost/Pointers.md)。 |
| 恢复的语句 | 命名数组声明、auto/var/let 推断、只读局部量、point 块、前置/后置自增与自减；is 根据实际类身份判断并在成功分支绑定变量；print 输出字符串、字符、布尔、整数和浮点值。 |

字符串 Length 和 foreach 按 UTF-8 字节处理，不按 Unicode 字符处理。字符串与空指针比较安全；拼接将空指针当空串，空字符串引用的长度为 0。数组 Length 只读。堆分配检查负数、大小溢出和系统调用失败，失败返回空指针；`delete null` 安全，不能用 delete 释放字符串或栈地址。

泛型约束使用 `where T : class`、`struct`、`unmanaged`、`new()`、基类/接口名或另一个类型参数；可以组合约束，并用于类、函数和方法。实例化前验证实际类型，失败报告 `E_GENERIC_CONSTRAINT`。`struct` 约束包括标量、枚举和结构体值；`unmanaged` 检查嵌套字段中的托管引用。`new T()` 支持公开无参构造、结构体隐式无参构造及标量零值；接口和抽象类不满足 `new()`。

结构体使用真实内联布局、自然对齐和独立值复制，支持嵌套/泛型字段、固定数组、值参数、引用参数及隐藏结果指针返回。只读接收者调用使用防御性副本；`finally` 在返回值复制之后执行。原生 SSA 后端和 VM v3 共用布局。布局、调用约定及当前边界见 [结构体值语义](../Docs/SelfHost/Structs.md)，实际 socket 示例见 [HTTP 服务](../examples/http-server/README.md)。访问规则见 [访问控制](../Docs/SelfHost/AccessControl.md)。

虚方法和接口调用根据对象实际类型选择实现；接收者只求值一次，`base.Method()` 直接调用基类实现。非虚方法隐藏仍按静态接收者类型选择。接口支持多个父接口、泛型接口、继承的实现、重载和 ref 参数；签名必须匹配，缺失实现及冲突契约会报错。继承的接口映射随虚方法 override 更新，普通同名方法隐藏不替换它；派生类重新声明实现该接口时建立新映射。接口引用保持原对象指针，可用于数组、delete、throw 和 catch。虚方法或接口调用的空接收者以状态 126 退出。

派发代码枚举当前模块内的具体类，暂不使用可跨模块扩展的虚表；泛型类中的普通虚方法可实例化，但方法自身的泛型参数、接口默认方法和显式接口实现尚未支持。

命名参数按静态签名的形参名匹配，转换和求值保持源码顺序，每个实际参数只求值一次，再按形参顺序传入。普通、泛型、扩展、虚方法、接口、构造与基类构造调用使用同一路径；重名或未知标签报告 `E_ARGUMENT_NAME`。已知函数取址保留名称契约，匿名 `fn(...)` 类型和 `syscall` 不接受标签。

数组字面量在直接调用、函数指针调用和构造调用中按候选参数的元素类型逐项检查，选定签名后执行转换。重载优先选择元素类型完整一致的候选，包含整数符号、字符/布尔身份、类身份和回调签名。空字面量及全 null 字面量可以使用已知的上下文类型；它们自身不提供泛型元素类型，歧义调用会报错。已有数组变量和 `ref` 参数仍要求完整元素类型匹配。

泛型静态字段按具体类型分别分配存储，启动时按源码顺序初始化一次；继承访问共享所属基类具体实例的静态字段。未使用的模板不分配存储，初始化顺序不依赖运行时首次访问。

原生 IR 显式表示调用、控制流、内存与异常处理。后端验证跳转目标、操作数栈高度及局部槽位，非法 IR 不会被静默修正。

绑定器检查完整函数体，包含不可达语句中的名称、类型及 break/continue 目标；IR 按控制流出口停止生成不可达语句，保留 switch 贯穿和 finally 清理。合法的早返回后可以保留其他语句，不会因为存在第二个 return 被直接拒绝。

Parser 用独立游标构造类型 AST 前瞻，错误后按语句、声明及 case 边界恢复，并保留后续合法 AST。编译器收集带阶段、代码、消息和位置的诊断，最多报告 64 条；继承循环包含实际类型路径。using frame 按父作用域继承，导入的模块只暴露其实际加载文件中的声明；别名和块级 using 使用同一管理路径。泛型注册表按完整类型形状缓存具体实例并扩容，名称修饰包含命名空间、泛型 owner/参数、指针层级和字符身份。详见 [Parser](../Docs/SelfHost/Parser.md) 与 [泛型](../Docs/SelfHost/Generics.md)。

`-O1` 执行整数常量折叠、转换折叠和常量分支简化；`-O2` 进一步消除死 SSA 值，并把安全的直接自身尾调用改为循环，保留需要栈地址或异常帧的函数；`-O3` 还内联符合大小和调用条件的小函数。`-O0` 关闭这些优化。

原生编译路径构造函数 CFG、独立值编号和带前驱信息的 phi，验证支配关系后优化 SSA。KRO 后端直接读取 SSA 操作数，并通过活跃性分析和线性扫描为值分配 `r12`、`r13`，其余值溢出到栈；phi 在控制流边上执行并行复制。地址逃逸的局部变量保留栈槽。机器 SSA 使用 `i64` 字位型，浮点格式、指针和盒装宽整数另有语义注解，精度转换由明确的 IR 操作完成。VM 目标继续使用栈式表示。

## 验证与限制

```sh
python3 -m unittest discover -s Test/SelfHost
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" python3 -m unittest \
  Test.SelfHost.test_native_collections Test.SelfHost.test_native_generics \
  Test.SelfHost.test_native_exceptions Test.SelfHost.test_inheritance Test.SelfHost.test_namespaces
```

专项测试覆盖构造与初始化顺序、集合求值顺序、循环作用域、泛型实例隔离、类型/引用不匹配、跨调用异常展开、finally 的多种出口，以及实际机器码运行。具体测试数量、源码版本和通过情况应读取相应报告，不使用文档中的固定数量代替验收。语法边界见 [SyntaxParity.md](../Docs/SelfHost/SyntaxParity.md)，实际对照结果见 [日常使用对比](../Docs/SelfHost/DailyUseComparison.md)。

仍未完成的能力包括闭包/捕获 lambda、完整迭代器协议、全部类型推断规则、结构体接口/装箱、跨对象文件的命名类/接口/枚举 ABI、二进制泛型函数模板，以及通用外部 ABI。原生 extern 函数可以通过 KRO 未定义符号和重定位链接；公有具体结构体（含闭泛型结构体）使用 ABI2 完整布局契约并在链接前逐字节校验；标量对象保留 KRT1。具体支持的参数类型及库边界见工程文档。当前未捕获异常以状态 70 退出。单继承保持基类字段前缀和对象身份；多个类基类和未经检查的类/接口向下转换会拒绝。锯齿数组、数组边界检查和 GC 尚未实现。

所有输入源码及导入合计上限为 16 MiB；参数最多 128 个（实例接收者计入），具体函数最多 16384 个；泛型注册表动态扩容，仍受容量上限约束。指令与 SSA 值采用受预算约束的动态容量。堆对象分别使用匿名映射，小对象开销较高；拼接字符串和 128 位运算的内部盒装分配会保留内存，长时间运行需考虑这一限制。旧紧凑对象 API 仍用于既有二进制契约测试。
