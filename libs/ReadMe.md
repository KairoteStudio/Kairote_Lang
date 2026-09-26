# 常用基础库

使用 `using System;` 导入基础库，也可以按需导入 `System.String`、
`System.StringBuilder`、`System.Array`、`System.Math`、`System.Convert`、
`System.Memory` 或 `System.Console`。`using Stdlib;` 保留兼容入口。

| 模块 | 常用功能 |
| --- | --- |
| `StringOps` | 长度、连接、子串、比较、查找、前后缀、ASCII 去空白与大小写、整数格式化 |
| `StringBuilder` | 固定容量缓冲区、追加、插入、删除、清空、转换为独立字符串 |
| `ArrayOps` | 长度、复制、填充、清零、反转、查找、包含判断，支持整体和范围操作 |
| `MathOps` | int32/int64 的绝对值、最值、符号、夹取、幂、整数平方根、GCD、LCM，以及严格检查的 Try 方法 |
| `Convert` | 有符号/无符号 32/64 位十进制解析、整数转字符串 |
| `Memory` | 分配、释放、重叠复制、移动、填充、清零、无符号字节比较 |
| `Console` | 字符串、布尔值、32/64 位有符号/无符号整数输出和 `{0}` 替换 |

```csharp
using System;

int32 main() {
    int64 value = 0;
    if (!Convert.TryToInt64("9223372036854775807", ref value)) {
        return 1;
    }
    Console.WriteLine(value);
    int32[] values = [3, 1, 2];
    ArrayOps.Reverse(values);
    Console.WriteLine(ArrayOps.IndexOf(values, 1));
    return 0;
}
```

字符串按以零字节结尾的 UTF-8 字节序列处理：长度和下标是字节数，
大小写与空白转换只处理 ASCII。`Substring` 等操作不会修复被截断的 UTF-8 字符。
成功返回的新字符串由调用方拥有，包括空字符串；使用原始地址和分配长度调用
`Memory.Free`。普通字符串结果的分配长度为 `StringOps.Length(result) + 1`；
Builder 可以保留内部零字节，释放 `BufferToString` 结果时应使用缓冲区的显式长度加一。
字面量、输入参数和内部指针不能作为这些新分配结果释放。

内存接口目前使用 Linux x86-64 系统调用。`TryCopy`、`TryMove`、`TrySet`、
`TryClear` 在负长度、非零长度搭配空地址或地址回绕时返回 false，且不写入；
零长度成功且不访问地址。原始指针实际容量和可访问性仍由调用方保证。
`Compare` 返回 -1、0、1，无效参数返回 -2。分配结果必须用原始地址和正确大小释放。

数组操作使用编译器创建的数组头，只接受 `new T[n]` 或数组字面量等真实数组，
不能把 `Memory.Allocate` 的裸缓冲区强转为数组。范围以 `start/count` 表示，
无效范围不会部分修改数组。`Length(null)` 为 0，非法头为 -1；
`IndexOf` 未找到返回 -1，无效范围返回 -2。支持的元素类型和验证命令见
[基础库回归说明](../Test/StandardLibrary/ReadMe.md)。

[字符串与 Builder 契约](../Test/StandardLibrary/StringReadMe.md)和
[数学与转换契约](../Test/StandardLibrary/MathConvertReadMe.md)列出了空值、
溢出、所有权以及旧接口的兼容行为。每个接口的参数契约同时保留在库源码中。
