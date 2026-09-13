# 字符串与 StringBuilder

实现位于 [String.krt](../../libs/System/String.krt) 和 [StringBuilder.krt](../../libs/System/StringBuilder.krt)，使用 `using System.String;` 与 `using System.StringBuilder;` 导入。依赖的 Memory、Sys 会按正常导入链加载。

`string` 是以零字节结束的字节序列。长度、搜索位置和截取范围按字节计算；UTF-8 多字节字符不作整体识别，截取可能切开编码。大小写和空白处理仅实现 ASCII，其他字节原样保留。

## StringOps

| 方法 | 行为 |
|---|---|
| `Length(value)` | 返回字节数；null 为 0，超过 int32 范围为 -1。 |
| `IsNullOrEmpty(value)` | 判断 null 或空串。 |
| `IsNullOrWhiteSpace(value)` | 判断 null、空串或仅含 ASCII 空白；空白为 9～13、32。 |
| `Compare(left, right)` / `Equals(left, right)` | 按无符号字节比较；null 小于非 null，两个 null 相等。Compare 的正负表示顺序，不保证恰为 ±1。 |
| `Concat(left, right)` | 拼接，null 当空串；总长度溢出或分配失败返回 null。 |
| `FromInt32` / `FromInt64` / `FromUInt64` | 完整整数范围的十进制格式化，包含有符号最小值和 uint64 最大值；失败返回 null。 |
| `IndexOf(value, character)` / `LastIndexOf(value, character)` | 查找一个字节；不包含结尾零字节，未找到返回 -1。 |
| `IndexOf(value, needle)` / `IndexOf(value, needle, start)` | 搜索子串；start 必须在 `[0, Length(value)]`；空 needle 在 start 匹配。 |
| `LastIndexOf(value, needle)` | 最后一个子串匹配；空 needle 在末尾匹配。 |
| `Contains(value, needle)` | 是否包含子串；null 参数为 false，非 null 字符串包含空串。 |
| `StartsWith` / `EndsWith` | 精确字节前缀/后缀；null 参数为 false，空模式匹配。 |
| `Substring(value, start)` / `Substring(value, start, length)` | 保留既有截断规则：负 start 按 0，超过末尾取空串，负 length 按 0，超长范围截到末尾。以减法检查范围，避免 `start + length` 溢出。 |
| `Trim` / `TrimStart` / `TrimEnd` | 去掉两端、开头或结尾的 ASCII 空白，返回副本。 |
| `ToUpper` / `ToLower` | 只转换 ASCII a-z / A-Z，返回副本。 |

子串搜索的任一 null 参数返回 -1。Substring、Trim 和大小写转换的 null 输入返回 null；分配失败也返回 null。

所有返回字符串的 StringOps 方法，在成功时都返回独立分配的**基址**，包括空串和数字 0；不会返回输入的内部地址或借用的空串常量。调用方使用 `Length(result) + 1` 释放。字符串字面量、输入字符串和 `Sys.InternalStringPtr` 返回的借用地址不能因此释放。

```csharp
using System.String;

int32 main() {
    string text = StringOps.FromInt64((int64)-9223372036854775808);
    if (text == null) {
        return 1;
    }
    unsafe(using krt.mem;) {
        Memory.Free((void*)Sys.InternalStringPtr(text), (int64)StringOps.Length(text) + 1);
    }
    return 0;
}
```

## StringBuilder

这是显式缓冲区接口，保留原有 `int64 buffer` 和独立长度参数。`AllocateBuffer(capacity)` 分配 `capacity + 1` 个字节并写入初始终止符；capacity 不包含终止符。失败或负容量返回 0；使用相同 capacity 调用 `FreeBuffer`。调用方负责保证实际内存容量与传入的容量、长度一致。

| 方法 | 行为 |
|---|---|
| `AppendString` / `AppendChar` | 追加字符串或一个字节，返回新长度并维护零终止；字符串 null 按空串。 |
| `AppendInt32` / `AppendInt64` / `AppendUInt64` | 追加十进制整数；临时字符串在成功和容量不足时都会释放。 |
| `AppendBool` | 追加 `True` / `False`。 |
| `AppendLine` | 追加字符串及换行，或仅换行；先验证完整容量，失败不会只写入字符串部分。 |
| `Insert` | 将 index 截断到 `[0, current_length]`；允许来源与目标重叠，移动前保存受影响的源字节。 |
| `Remove` | 无效 index 或非正 length 保持原长度；超长范围截到末尾，移动后补终止符。 |
| `Clear(current_length)` | 旧接口仅返回新的逻辑长度 0，不访问缓冲区；随后追加，包括追加空串，会重写终止符。 |
| `BufferToString(buffer, length)` | 返回独立的零终止副本，包括空串；失败返回 null。使用传入的 `length + 1` 释放。 |
| `IndexOf` / `LastIndexOf` / `Replace` | 按显式长度搜索或替换字节；Replace 返回替换次数。 |

追加、插入、替换遇到无效缓冲区或长度时返回 -1；追加与插入的容量不足或分配失败也返回 -1，保持原缓冲区内容。插入、追加和移除支持有效内存范围内的重叠，容量检查使用减法避免整数溢出。

Builder 保留显式长度中的内嵌零字节；搜索和替换仍会处理它之后的字节。转成 string 后，普通字符串操作只看到第一个零字节之前的部分。因此 `BufferToString` 的释放大小必须使用传入的 length，而不是重新测得的字符串长度。

```csharp
using System.StringBuilder;

int32 main() {
    int32 capacity = 64;
    int64 buffer = StringBuilder.AllocateBuffer(capacity);
    if (buffer == 0) {
        return 1;
    }
    int32 length = StringBuilder.AppendString(buffer, capacity, 0, "value=");
    if (length >= 0) {
        length = StringBuilder.AppendUInt64(buffer, capacity, length, (uint64)18446744073709551615);
    }
    StringBuilder.FreeBuffer(buffer, capacity);
    return length < 0 ? 1 : 0;
}
```

## 回归

[test_strings.py](test_strings.py) 使用复制后的真实库源码和正常 using 导入，默认分别运行 O0、O1、O2、O3。15 项测试覆盖整数极值与固定种子的差分值、全部 255 个非零字节的大小写处理、null 比较与表达式求值次数、截取/容量溢出、前后重叠、终止符和结果释放。

分配错误在生成程序的独立子进程中用 Linux `RLIMIT_AS` 触发；另在有限地址空间中重复成功和失败的整数追加，检查临时分配没有积累。编译器进程及调用方进程不受这些限制影响。

```bash
PYTHONPATH=Test/StandardLibrary python3 -m unittest discover -s Test/StandardLibrary -p test_strings.py -v
```
