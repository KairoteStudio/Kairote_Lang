# `libs/System` API 概览

本页只概览仓库中实际存在的库声明。完整重载、参数类型、注释和所有权说明以链接的 `.krt` 源文件为准；这里不声明未实现的网络、日期、线程或 GUI API。大多数文件没有单独命名空间声明，使用时的类型名以源文件为准。

## `Console` (`System`)

源码：[libs/System/Console.krt](../../../../libs/System/Console.krt)

提供标准输入、输出和错误文件描述符常量，以及 `Write`、`WriteLine`、`WriteFormat`、`WriteLineFormat`。已声明的重载包括字符串、常用有符号/无符号整数和布尔值；没有 `ReadLine` 声明。

## `Memory`（全局类型）

源码：[libs/System/Memory.krt](../../../../libs/System/Memory.krt)

提供 `Allocate`、`Free` / `TryFree`、`Copy` / `TryCopy`、`Move` / `TryMove`、`Set` / `TrySet`、`Clear` / `TryClear` 和 `Compare`。它是 Linux x86-64 裸内存接口；参数地址必须指向调用方可访问的有效区域。该分配结果不带编译器托管数组头部。

## `ArrayOps`（全局类型）

源码：[libs/System/Array.krt](../../../../libs/System/Array.krt)

为声明支持的整数、布尔及浮点数组提供 `Length`、`Copy`、`Clear`、`Fill`、`Reverse`、`IndexOf` 和 `Contains` 重载。区间操作在写入前验证源和目标范围，返回 `bool` 报告成功；`Copy` 支持重叠范围。它要求有效的托管数组，不适用于 `Memory.Allocate` 返回的裸缓冲区。

## `StringOps`（全局类型）

源码：[libs/System/String.krt](../../../../libs/System/String.krt)

提供 `Length`、`Concat`、整数转字符串、`Compare`、`Equals`、空白判断、字符或子串查找、`Contains`、`Substring`、前后缀判断、修剪和 ASCII 大小写转换。字符串长度和索引以字节计。新分配字符串的释放方式见每个方法的源码注释。

## `StringBuilder`（`System.Runtime`）

源码：[libs/System/StringBuilder.krt](../../../../libs/System/StringBuilder.krt)

这是显式缓冲区操作集合，不是自动拥有存储的 builder 对象。它提供缓冲区分配/释放、追加字符串和数字、插入、删除、清空、转字符串、查找和替换等方法。调用方需维护容量和当前字节长度。

## `Convert` 和 `MathOps`（`System.Runtime`）

源码：[Convert.krt](../../../../libs/System/Convert.krt)、[Math.krt](../../../../libs/System/Math.krt)

`Convert` 提供有符号和无符号十进制解析及 `Try` 形式，并将常用整数或布尔值转换为字符串。`MathOps` 提供整数绝对值、最小/最大值、符号、区间限制、幂、平方根、GCD、LCM 等操作；`Try` 版本报告无效输入或溢出。具体支持的整数重载以源码为准。

## `Exception`（`System`）

源码：[libs/System/Exception.krt](../../../../libs/System/Exception.krt)

普通类，含 `Message`、`Code` 字段、对应构造函数和 `ToString()`。异常对象仍遵循对象的显式所有权管理。

## `Sys`（全局类型）

源码：[libs/System/Sys.krt](../../../../libs/System/Sys.krt)

提供 Linux x86-64 系统调用封装、常见系统调用号、进程退出及运行时内部字符串/数字辅助方法。`Internal*` 方法属于低层运行时接口，不应当作稳定的通用应用 API。

## 泛型迭代协议

源码：[libs/System/Collections/Generic.krt](../../../../libs/System/Collections/Generic.krt)

声明 `IEnumerable<T>.GetEnumerator()` 和 `IEnumerator<T>` 的 `MoveNext()`、`Current()`、`Dispose()`。协议消费者应遵循 `Current` 仅在成功的 `MoveNext` 后有效的约定。

## 如何核实 API

查看链接的 `.krt` 文件，确认方法声明、命名空间、参数、失败返回值、所有权注释和调用依赖。`libs/System` 是库接口的实际来源；`Re.KrtC/src/Runtime/Krt` 中的运行时辅助代码是另一层，不应把内部函数名直接当作标准库公开契约。
