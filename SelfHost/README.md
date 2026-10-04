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
| 集合与循环 | 类型化数组、`[...]` 和初始化表达式中的 `{...}`、类型推断、空数组、数组返回值、对象/字符串元素；foreach 支持数组、字符串和通用迭代器，集合只求值一次，退出时执行 Dispose；yield 生成可独立恢复的惰性游标。 |
| 名称与泛型 | 文件级、块级、嵌套 namespace，限定类型/函数名称、using 和别名；泛型类、函数、方法，显式类型参数和已有推断规则，嵌套实例、泛型构造函数、ref、数组和静态调用。泛型通过生成具体实例执行；支持 where 约束与每个具体类型独立的静态字段。 |
| 数值 | 2–128 偶数位宽有符号与无符号整数（含 int30、int66 等），转换与存储按声明精度截断和符号扩展；float32/float64 字面量、算术、比较和转换，含数组、字段、参数及返回值。 |
| 异常 | throw、类型捕获、多个 catch、catch-all、rethrow、finally；支持跨函数展开以及 return、break、continue 时执行 finally。类异常按对象实际类型匹配本类和基类，即使通过基类变量抛出或重抛也保留派生类型；标量异常按类型精确匹配。 |
| 程序状态与回调 | 可变全局量、类静态字段及按声明顺序执行的初始化；`fn(...) -> ...` 回调、自由函数/静态方法取址、lambda、词法共享捕获与逃逸环境、带 ref 的间接调用及手动释放。 |
| 字符串与内存 | 内容相等比较、双字符串拼接、只读 Length、对象/数组分配、栈分配、类型化指针步长、动态系统调用，以及实际释放对象和数组的 delete。 |
| 指针类型 | 完整指针空值层、源码顺序类型后缀、直接/间接调用的统一参数规则、精确 ref、非空类型模式与规范元素布局。规则和示例见 [指针类型](../Docs/SelfHost/Pointers.md)。 |
| 恢复的语句 | 命名数组声明、auto/var/let 推断、只读局部量、point 块、前置/后置自增与自减；is 根据实际类身份判断并在成功分支绑定变量；print 输出字符串、字符、布尔、整数和浮点值。 |

字符串 Length 和 foreach 按 UTF-8 字节处理，不按 Unicode 字符处理。字符串与空指针比较安全；拼接将空指针当空串，空字符串引用的长度为 0。数组 Length 只读。堆分配检查负数、大小溢出和系统调用失败，失败返回空指针；`delete null` 安全，不能用 delete 释放字符串或栈地址。

泛型约束使用 `where T : class`、`struct`、`unmanaged`、`new()`、基类/接口名或另一个类型参数；可以组合约束，并用于类、函数和方法。实例化前验证实际类型，失败报告 `E_GENERIC_CONSTRAINT`。`struct` 约束包括标量、枚举和结构体值；`unmanaged` 检查嵌套字段中的托管引用。`new T()` 支持公开无参构造、结构体隐式无参构造及标量零值；接口和抽象类不满足 `new()`。

结构体使用真实内联布局、自然对齐和独立值复制，支持嵌套/泛型字段、固定数组、值参数、引用参数及隐藏结果指针返回。只读接收者调用使用防御性副本；`finally` 在返回值复制之后执行。结构体可实现接口，通过约束调用或装箱调用。原生 SSA 后端和 VM 共用布局，VM v6 还将宽整数统一为 16 字节值存储。布局、调用约定及当前边界见 [结构体值语义](../Docs/SelfHost/Structs.md) 和 [宽整数](../Docs/SelfHost/WideValues.md)，实际 socket 示例见 [HTTP 服务](../examples/http-server/README.md)。访问规则见 [访问控制](../Docs/SelfHost/AccessControl.md)。

虚方法和接口调用根据对象实际类型选择实现；接收者只求值一次，`base.Method()` 直接调用基类实现。非虚方法隐藏仍按静态接收者类型选择。接口支持多个父接口、泛型接口、继承的实现、重载和 ref 参数；签名必须匹配，缺失实现及冲突契约会报错。继承的接口映射随虚方法 override 更新，普通同名方法隐藏不替换它；派生类重新声明实现该接口时建立新映射。接口引用保持原对象指针，可用于数组、delete、throw 和 catch。虚方法或接口调用的空接收者以状态 126 退出。

派发读取对象的运行时类型描述符，以完整方法身份查询实现；独立 KRO 中的类、接口和装箱结构体可使用同一派发机制。泛型类中的普通虚方法可实例化，但方法自身的泛型虚派发、接口默认方法和显式接口实现尚未支持。

命名参数按静态签名的形参名匹配，转换和求值保持源码顺序，每个实际参数只求值一次，再按形参顺序传入。普通、泛型、扩展、虚方法、接口、构造与基类构造调用使用同一路径；重名或未知标签报告 `E_ARGUMENT_NAME`。已知函数取址保留名称契约，匿名 `fn(...)` 类型和 `syscall` 不接受标签。

数组字面量在直接调用、函数指针调用和构造调用中按候选参数的元素类型逐项检查，选定签名后执行转换。重载优先选择元素类型完整一致的候选，包含整数符号、字符/布尔身份、类身份和回调签名。空字面量及全 null 字面量可以使用已知的上下文类型；它们自身不提供泛型元素类型，歧义调用会报错。已有数组变量和 `ref` 参数仍要求完整元素类型匹配。

泛型静态字段按具体类型分别分配存储，继承访问共享所属基类具体实例的静态字段。未使用的模板不分配存储。模块入口与跨模块访问通过共享初始化保护执行声明初始化；递归访问保留同一初始化过程，初始化抛错后可以重试。独立 KRO 的 extern 静态字段引用提供方的存储，闭泛型实例共享相同身份的存储。具体顺序和契约见 [跨模块 ABI](../Docs/SelfHost/ModuleAbi.md)。

函数名称查询使用按简单名称分桶的索引，桶内保持原始声明顺序；泛型实例追加时同步新声明，原型和模板移除后使索引失效。作用域、文件私有性、接收者、参数映射、重载评分和访问检查仍由原绑定流程处理。哈希碰撞会比较完整名称，索引扩容不会改变候选次序。

每个具体函数单独缓存成功解析的参数与返回类型，保留完整元素类型、空值层和回调签名。缓存独立于函数激活时的局部声明与引用状态；每次使用仍在原源码位置检查类型访问权限。布局探测不发布缓存，失败的类型解析也不会缓存。非固定字段只在声明布局与相关继承完成后复用类型描述，并刷新名义类型的最终对象大小；闭泛型名义类型、嵌套容器与回调继续执行完整解析。

原生 IR 显式表示调用、控制流、内存与异常处理。后端验证跳转目标、操作数栈高度及局部槽位，非法 IR 不会被静默修正。

绑定器检查完整函数体，包含不可达语句中的名称、类型及 break/continue 目标；IR 按控制流出口停止生成不可达语句，保留 switch 贯穿和 finally 清理。合法的早返回后可以保留其他语句，不会因为存在第二个 return 被直接拒绝。

Parser 用独立游标构造类型 AST 前瞻，错误后按语句、声明及 case 边界恢复，并保留后续合法 AST。编译器收集带阶段、代码、消息和位置的诊断，最多报告 64 条；继承循环包含实际类型路径。using frame 按父作用域继承，导入的模块只暴露其实际加载文件中的声明；别名和块级 using 使用同一管理路径。泛型注册表按完整类型形状缓存具体实例并扩容，名称修饰包含命名空间、泛型 owner/参数、指针层级和字符身份。详见 [Parser](../Docs/SelfHost/Parser.md) 与 [泛型](../Docs/SelfHost/Generics.md)。

`-O1` 执行整数常量折叠、转换折叠和常量分支简化；`-O2` 进一步消除死 SSA 值，并把安全的直接自身尾调用改为循环，保留需要栈地址或异常帧的函数；`-O3` 还内联符合大小和调用条件的小函数。`-O0` 关闭这些优化。

原生编译路径构造函数 CFG、独立值编号和带前驱信息的 phi，验证支配关系后优化 SSA。KRO 后端直接读取 SSA 操作数，并通过活跃性分析和线性扫描为值分配 `r12`、`r13`，其余值溢出到栈；phi 在控制流边上执行并行复制。地址逃逸的局部变量保留栈槽。机器 SSA 使用 `i64` 字位型，浮点格式、指针和宽整数载荷另有语义注解，精度转换由明确的 IR 操作完成。宽整数临时值使用固定函数帧存储。VM 目标继续使用栈式表示。

## 闭包

`function`、`func` 和 `fn` lambda 支持表达式体、块体、明确返回类型及上下文参数推断。捕获按词法声明共享 heap cell，保留完整数值与结构体布局；嵌套及逃逸闭包可在创建函数退出后调用。循环、模式及 catch 变量使用相应作用域，所有退出路径释放作用域 owner。

保存的捕获闭包需要 `delete`；回调副本共享描述符，删除后全部别名失效。直接立即调用的捕获 lambda 自动清理临时描述符。已知栈地址、外层 ref 参数及结构体 this 捕获会报告 `E_CAPTURE_BORROW`；unsafe 外部指针的生命周期仍由调用方保证。原生隐藏环境通过 `r10` 传递，不占 128 个源码参数；独立 KRO 使用 `fn2` 回调契约，具有直接宽整数边界的回调使用 `fn3`。VM 写出 EBC v6 并保留 v1–v5 读取兼容性。完整语义、所有权及验收命令见 [闭包与回调](../Docs/SelfHost/Closures.md)。

## 空值运算与循环变量

`left ?? right` 按右结合解析，优先级高于三元条件表达式。左侧引用只求值一次；非空时保留原值，空时才求值右侧。类和接口求共同可赋值类型；数组元素、指针内层空值标记、回调签名保持完整。裸指针和回调结果的最外层可空性由右侧决定。`null ?? value` 可使用右侧的值类型，包括结构体。普通标量和内联结构体不能作为已定类型的左侧。

`receiver?.member`、`receiver?[index]` 对整个未分组的后缀访问链执行空值检查，跳过成员、索引和调用实参的运行时求值；所有分支仍执行正常编译期名称、类型和访问检查。括号结束当前条件链，后续普通访问遵循已有类型规则。结果在接收者为空时使用其类型的零值：整数、浮点和宽整数为零，引用和回调为空，结构体和固定数组全部字节为零；本语言没有为标量另加 nullable 包装。结构体和固定数组结果有独立值存储。条件结果不能直接赋值、递增递减、取地址或作为 writable ref；链内普通方法的 ref 参数仍按源码顺序求值。

普通调用和条件调用返回 `void` 时可以作为表达式语句；它们不能用于初始化、赋值、实参、条件、运算、索引、分配大小、打印、异常载荷或其他值消费位置。`default(void)`、`sizeof(void)` 和 `catch(void)` 会报错。

foreach 与普通局部声明共用 `var`、`auto`、`let` 类型推断规则，保留完整元素类型；字符串元素是 UTF-8 字节形式的 `char`。`let` 循环变量只读，结构体接收者的可变方法使用防御性副本，既不会修改只读循环变量，也不会修改原数组元素。

专项回归位于 `test_nullable_operators.py`、`test_foreach_bindings.py` 和 `test_void_values.py`；HTTP 回归使用真实 TCP 请求验证可选配置的默认值与响应结构体副本。

## 验证与限制

2026-10-02 的编译性能对照使用提交 `2000175` 中冻结的 89 个原始源文件。新编译器本身包含 91 个源文件。两个工具均以原生 CLI、空 PATH、`-O2` 编译同一批原始文件，独立链接；原生编译子进程的资源用 `wait4` 记录。

| 本机单次对照 | 原编译器 | 加入索引与缓存后 |
| --- | ---: | ---: |
| 编译用时 | 54.70 秒 | 51.15 秒 |
| 峰值 RSS | 4,221,428 KiB | 3,143,400 KiB |

生成的 KRO 和 ELF 均逐字节相同。该输入上的耗时减少约 6.5%，峰值 RSS 减少约 25.5%；这不是所有工程的性能保证。完整源文件、工具和产物哈希，以及命令与资源数据见 `build/selfhost-performance/baseline/report.json`、`build/selfhost-performance/after2/same-baseline89/report.json` 与 `build/selfhost-performance/after2/performance-comparison.json`。新回归额外检查缓存后的访问诊断、私有类型布局、深层空值标记、前向及嵌套泛型继承，以及字段推断参与 `new T()` 时的分配大小。

```sh
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" \
  python3 -m unittest discover -s Test/SelfHost
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" python3 -m unittest \
  Test.SelfHost.test_native_collections Test.SelfHost.test_native_generics \
  Test.SelfHost.test_native_exceptions Test.SelfHost.test_inheritance Test.SelfHost.test_namespaces
```

专项测试覆盖构造与初始化顺序、集合求值顺序、循环作用域、泛型实例隔离、类型/引用不匹配、跨调用异常展开、finally 的多种出口，以及实际机器码运行。具体测试数量、源码版本和通过情况应读取相应报告，不使用文档中的固定数量代替验收。语法边界见 [SyntaxParity.md](../Docs/SelfHost/SyntaxParity.md)，实际对照结果见 [日常使用对比](../Docs/SelfHost/DailyUseComparison.md)。

通用迭代器、惰性生成器、结构体接口和装箱的语义及所有权见 [迭代器](../Docs/SelfHost/Iterators.md) 与 [装箱](../Docs/SelfHost/Boxing.md)。原生 extern 函数通过 KRO 未定义符号和重定位链接；公有具体结构体使用 ABI2 布局契约，命名类型、二进制泛型模板和静态存储使用 ABI3。链接前校验完整类型身份和布局，具体边界见 [模块 ABI](../Docs/SelfHost/ModuleAbi.md)。类和接口的显式转换检查实际运行时类型，失败抛出可捕获的错误。当前未捕获异常以状态 70 退出。全部类型推断规则、通用外部 ABI、多个类基类、锯齿数组、数组边界检查和 GC 尚未实现。

所有输入源码及导入合计上限为 16 MiB；参数最多 128 个（实例接收者计入），具体函数最多 16384 个；泛型注册表动态扩容，仍受容量上限约束。指令与 SSA 值采用受预算约束的动态容量。堆对象分别使用匿名映射，小对象开销较高；拼接字符串的临时分配仍需考虑长期内存占用。宽整数运算的临时存储随函数帧释放，用户拥有的装箱对象、保存的闭包和生成器需遵守各自释放契约。旧紧凑对象 API 仍用于既有二进制契约测试。
