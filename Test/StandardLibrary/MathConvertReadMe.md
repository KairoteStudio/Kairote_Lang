# 数学与整数转换

通过 `using System;` 导入完整基础库，也可以分别使用 `using System.Math;` 和 `using System.Convert;`。公开类名称保持为 `MathOps`、`Convert`。

## MathOps

以下方法均提供 `int32`、`int64` 重载；`Sign` 的返回类型及幂运算的指数类型始终为 `int32`。

| 方法 | 契约 |
| --- | --- |
| `Abs(value)` | 绝对值。保留旧的定宽回绕：`Abs(MIN) == MIN`。 |
| `Min(a, b)` / `Max(a, b)` | 返回较小值或较大值。 |
| `Sign(value)` | 分别返回 `-1`、`0`、`1`。 |
| `Clamp(value, minimum, maximum)` | 闭区间限制；调用者应保证 `minimum <= maximum`。仍按旧顺序先判断下限，再判断上限。 |
| `Pow(base_value, exponent)` | 平方求幂，乘法按返回类型回绕；保留 `exponent <= 0` 时返回 `1`，包括 `0^0`。 |
| `Sqrt(value)` | 向下取整的整数平方根；非正数返回 `0`。计算覆盖有符号最大值，不执行会溢出的 `value + 1`。 |
| `GCD(a, b)` | 对绝对值求最大公约数，`GCD(0, 0) == 0`。若结果为 `2^(位宽-1)`，返回其定宽表示 `MIN`。 |
| `LCM(a, b)` | 任一参数为零时返回零。保留旧的定宽乘法回绕及随后 `Abs` 的行为；溢出时结果不代表数学上的最小公倍数。 |

`IsPrime(int32 value)` 保留原接口；小于 `2` 的值返回 `false`。

需要拒绝非法输入或不可表示的结果时，使用以下接口。**全部返回 `bool`，失败时将 `ref result` 置为零，成功时写入结果。**

| 方法 | 失败条件 |
| --- | --- |
| `TryAbs(value, ref result)` | 输入为对应类型的 `MIN`。 |
| `TryClamp(value, minimum, maximum, ref result)` | 下限大于上限。 |
| `TryPow(base_value, exponent, ref result)` | 指数为负，或数学结果超出对应有符号类型；`0^0` 成功且结果为 `1`。 |
| `TrySqrt(value, ref result)` | 输入为负；零是合法输入。 |
| `TryLCM(a, b, ref result)` | 数学上的非负最小公倍数超出对应有符号类型；任一输入为零时成功且结果为零。 |

调用时可通过显式转换选择 64 位重载，例如 `MathOps.Sqrt((int64)9223372036854775807)`，结果为 `3037000499`。

混合 `int32` 与 `int64` 值时，重载选择优先保值扩宽，例如 `MathOps.Max((int64)5000000000, 1)` 保留 64 位值，`MathOps.Clamp((int64)5000000000, 0, 100)` 返回 `100`。`ref result` 仍要求与选中重载的输出类型完全一致。

## Convert

严格十进制解析采用统一名称：

```csharp
int64 value = 0;
bool parsed = Convert.TryToInt64("-9223372036854775808", ref value);
```

| 接口 | 接受的数值范围 |
| --- | --- |
| `TryToInt32(string value, ref int32 result)` | `-2147483648` 至 `2147483647` |
| `TryToInt64(string value, ref int64 result)` | `-9223372036854775808` 至 `9223372036854775807` |
| `TryToUInt32(string value, ref uint32 result)` | `0` 至 `4294967295` |
| `TryToUInt64(string value, ref uint64 result)` | `0` 至 `18446744073709551615` |

这四个接口读取完整的零终止字符串，只接受 ASCII 十进制数字及开头可选的 `+`。有符号接口也接受开头的 `-`；无符号接口拒绝所有负号，包括 `-0`。允许前导零，不允许空白、分隔符、小数、指数、进制前缀或其他字符。

`null`、空字符串、单独的符号、非法字符、长度超过 `2147483647` 字节及溢出均返回 `false`，并将结果置零。合法的零返回 `true`，可以据此区分失败。解析实现通过 `unsafe` 内的 `byte*` 读取字符串字节，不将原始地址伪装成数组。

旧的 `ToInt32(string)`、`ToInt64(string)` 保持兼容：读取十进制前缀，可接受开头的 `-`，遇到其他字符停止，算术定宽回绕；空值或没有数字的前缀返回零。例如 `ToInt32("123tail") == 123`，`ToInt32("+12") == 0`。

`ToString` 支持 `int32`、`int64`、`uint32`、`uint64`、`bool`。整数输出为精确十进制，包括最小负数及最大无符号数。整数结果为新分配的零终止缓冲区，分配失败返回 `null`；释放时使用分配基址及 `StringOps.Length(result) + 1` 字节。布尔结果是借用的 `"True"` / `"False"` 字面量，不能释放。

## 回归测试

`test_math.py` 覆盖极值、随机平方根、回绕与检查幂、GCD/LCM、素数；`test_convert.py` 覆盖严格解析、符号及溢出、旧前缀行为、四种整数类型的格式化往返和释放。

```sh
PYTHONPATH=Test/StandardLibrary python3 -m unittest test_math test_convert
```

默认在 `O0`、`O1`、`O2`、`O3` 分别编译并运行。可使用 `KRTC` 指定编译器，或通过 `KRT_STDLIB_TEST_LEVELS` 选择级别。
