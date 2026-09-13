# 基础库回归

最终构建身份和检查结果见 [验证记录](ValidationReadMe.md)。

从仓库根目录运行：

```sh
PYTHONPATH=Test/StandardLibrary python3 -m unittest discover -s Test/StandardLibrary -v
```

默认使用 `Re.KrtC/build/KrtC`，每个程序分别以 O0、O1、O2、O3 编译并执行。
`KRTC=/absolute/path/KrtC` 可指定编译器，`KRT_STDLIB_TEST_LEVELS=0,3` 可缩小级别。
每项测试把库源文件复制到独立临时目录，通过真实 `using` 路径导入；
不复用仓库内的 KRO 文件。临时产物在测试后清理。

| 测试 | 覆盖 |
| --- | --- |
| `test_strings.py` | 空指针、不同地址的相等字符串、255 种非零字节、UTF-8 字节边界、别名、容量、整数极值、实际 OOM、分配释放 |
| `test_arrays.py` | 精确导入、13 种类型的元素宽度、数组参数 ABI、空数组、重叠复制、范围溢出、头信息错误、NaN 与正负零、无效操作不部分写入 |
| `test_math.py` | int32/int64 边界、平方根不等式、幂和 LCM 溢出、严格 Try 方法与兼容语义 |
| `test_convert.py` | 空输入、正负号、非法字符、完整解析、边界溢出、失败时输出清零、格式化往返 |
| `test_memory.py` | 仅导入 System.Memory、零长度、负长度、空地址、地址回绕、重叠方向、填充、释放和无符号字节比较 |
| `test_runtime.py` | Console 精确输出、整数极值、重复输出、Sys 字节长度、格式化所有权、前向重载 |
| `test_imports.py` | 精确导入、兼容入口、空转发模块、项目并行编译、循环/菱形/深层导入、错误传播、重载保值与 ref 精确匹配、数组表达式布局与非法条件分支拒绝 |

`ArrayOps` 提供 13 种基础类型的明确重载：byte（uint8 的别名）、int8、int16、
uint16、int32、uint32、int64、uint64、int128、uint128、bool、float32、float64。
当前方法泛型尚未实现，其他整数位宽不在本次支持范围内；数组的元素存储宽度不能靠
强制转换互换。`IndexOf` 和 `Contains` 使用元素的 `==` 语义：浮点 NaN 与任何值
（包括自身）都不相等，因此搜索 NaN 返回 -1；+0.0 与 -0.0 相等。

内存测试仅通过 `using System.Memory;` 导入，数组测试仅通过 `using System.Array;`
导入，验证 `Array → Memory → Sys` 的显式依赖链，无需导入整个 System。

ASan/UBSan 检查编译器本身，生成的 KRO 可执行程序通过结果断言和边界测试验证：

```sh
python3 Test/Pointers/BuildSanitized.py --output /tmp/KrtC-stdlib-sanitized
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
KRTC=/tmp/KrtC-stdlib-sanitized PYTHONPATH=Test/StandardLibrary \
python3 -m unittest discover -s Test/StandardLibrary -v
```

内存失败测试使用子进程 `RLIMIT_AS`，不会修改整个测试进程的资源限制。
详细 API 契约见 [字符串](StringReadMe.md)、[数学与转换](MathConvertReadMe.md)和
[基础库使用说明](../../libs/ReadMe.md)。
