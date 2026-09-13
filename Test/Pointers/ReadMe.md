本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。

针对 Re.KrtC 的 Linux x86-64 KRO 后端，覆盖正常程序、编译错误和预期运行时故障。

在项目根目录运行：

```sh
python3 Test/Pointers/RunTests.py
python3 -m unittest discover -s Test/IntegerWidths -p 'test_*.py'
python3 Test/Performance/test_compiler_hotspots.py
bash Test/SyntaxAudit/run.sh
python3 Test/SyntaxAudit/test_language_regressions.py
```

2026-09-12 本机验证：指针专项 **50 个测试方法通过**，共 **457 次编译**：355 次成功、102 次预期拒绝，编译器无崩溃。成功编译的程序有 349 次正常退出、4 次分配溢出 `SIGILL`、2 次非法原始地址访问 `SIGSEGV`；故障用例在禁用 core dump 的独立子进程中执行。详见 `Results.json`。

| 覆盖范围 | 用例 |
|---|---|
| 整数与布局 | 全部 128 种 int/uint 偶数位宽 2–128，符号扩展、截断、字节别名、邻接哨兵 |
| 精确访问宽度 | 全部 128 种类型在保护页两端读写；非对齐 1/2/4/8/16 字节访问 |
| 指针读写 | 取地址、解引用、多级槽、数组、前后自增、自减、复合赋值、负索引和大索引 |
| 求值顺序 | 左操作数、数组基址/索引、实参、间接调用目标、赋值旧值在副作用之前求值 |
| 可空与作用域 | null/default、相等性、三元表达式、单次模式求值、分支收窄、嵌套遮蔽、可空指针槽与数组 |
| 栈分配 | 零元素、跨页、多次分配、递归、对齐、运行时负数、64/128 位数量溢出 |
| 函数调用 | 前向定义、回调、间接表达式、指针槽替换函数、0–7 个前置整数与宽参数混排、128 个指针参数 |
| 多文件 | 4 个文件全部 24 种输入顺序，跨文件可空返回/函数地址/128 位参数返回；单文件失败禁止链接 |
| 非整数 | bool/char、float32/64，单精度内存布局、字节重解释、NaN/负零、浮点回调与单次求值 |
| 容量与随机组合 | 100 个同时存活的取地址局部变量、64 层指针、超限诊断；16 个固定种子共 1,280 步别名操作，Python 独立计算期望结果 |
| 拒绝用例 | 缺失权限、未收窄解引用、类型/签名不符、非法算术与左值、错误索引/数量、错误语法、数据指针调用 |

代表程序已加入现有 SyntaxAudit 项目 C80–C91，12 项全部通过。完整 SyntaxAudit **75/75 通过**；枚举、泛型、命名空间和 `ref` 四项已修复，另有 **25 组语言回归**（65 次编译：36 次成功、29 次预期拒绝）。详见 [SyntaxAudit/ReadMe.md](../SyntaxAudit/ReadMe.md)。运行器遇到任何失败都会返回非零状态。
整数宽度专项 12 项、编译器热点及机器码检查 10 项全部通过。

ASan/UBSan 验证方法：

```sh
python3 Test/Pointers/BuildSanitized.py
KRTC=/tmp/KrtC-pointer-sanitized ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python3 Test/Pointers/RunTests.py --output Test/Pointers/SanitizerResults.json
```

同一组 50 项在 ASan/UBSan 下通过，见 `SanitizerResults.json`。该检查针对编译器本体，不对生成的机器码插桩；禁用泄漏检查，生成程序的越界验证使用保护页。测试暴露并修复了 IR 临时名称引用已返回栈帧的问题。枚举、泛型、命名空间、`ref` 的专项及完整语法审计也使用相同插桩配置验证。

本轮还修复了内存左值自增不写回、指针三元表达式截断、带副作用表达式重复或延迟求值、可空信息丢失、数组字面量漏查权限、分配数量高位截断、float32 内存格式、跨文件类型信息与错误传播等问题。

测试不能穷尽所有程序。unsafe 原始地址的有效性、对象边界与返回后的生命周期仍由调用者负责；完整 FFI 命名空间白名单、逃逸分析和 C 浮点 FFI ABI 不在已实现范围内。浮点取余复合赋值明确报错。

本机 Zig 0.16 的增量缓存曾复用修改 `.inc` 前的编译器；以上结果使用全新缓存构建后验证。遇到此情况，应使用新的 `--cache-dir` 重建再运行测试。

ASM/C 的 Fib35、Fib40 各十次测量在 [NativeFib/ReadMe.md](../Performance/NativeFib/ReadMe.md)，原始样本独立保存。
