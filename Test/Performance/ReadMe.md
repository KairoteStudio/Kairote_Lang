# Re.KrtC 编译热点与机器码回归

本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。 测量数值与构建哈希属于文中记录的历史版本，不代表后续规范整改后的新测量。

空循环、递归 Fibonacci、数字输出和文件 I/O 的十次运行平均耗时见 [运行性能测试](RuntimeReadMe.md)。
后续发现并修复的普通整数指令开销见 [Fibonacci 性能退化对照](FibComparisonReadMe.md)。

本轮优化保留 `int2`～`int128`、`uint2`～`uint128` 的全部偶数位宽，以及小整数共享寄存器的实现，进一步减少编译器重复工作和 KRO 后端的冗余指令。

## 改动范围

| 问题 | 实现 |
|---|---|
| 每个字符遍历所有宏 | [Preprocessor.c](../../Re.KrtC/src/compiler/Driver/Preprocessor.c) 一次扫描完整标识符，再查可扩容哈希索引；同时保护字符串、字符和注释，不再替换较长标识符内部的宏名 |
| 标准库反复解析 | [StdlibCache.inc](../../Re.KrtC/src/compiler/Pipeline/StdlibCache.inc) 缓存解析树；预加载标准库和语义分析期间的库导入共用缓存 |
| 符号双重扫描 | [SymbolTable.c](../../Re.KrtC/src/compiler/Frontend/Semantic/SymbolTable.c) 为每个作用域建立哈希索引，并按负载扩容全局索引；类内精确查找直接使用作用域链索引 |
| 小文件线程和 Pipeline 开销 | [ParallelCompiler.c](../../Re.KrtC/src/compiler/Driver/ParallelCompiler.c) 按批领取任务，每个工作线程复用一个 Pipeline；源文件总量不足 128 KiB 时顺序执行，不创建线程池 |
| 重复 epilogue | 每个函数生成一份寄存器恢复、栈帧恢复和 `ret`，所有显式 `return` 跳到该位置 |
| `_start` 后半段不可达 | `main` 正常返回，由 `_start` 执行自动退出序列 |
| 零值加载过长 | 使用 `xor eax, eax` 等短指令；可补零扩展的非零立即数使用 32 位 `mov` |
| 临时值全部落栈 | [KroRegisterPacking.inc](../../Re.KrtC/src/compiler/Backend/Kro/KroRegisterPacking.inc) 对基本块内的临时值按活跃区间分配、复用剩余寄存器 |
| 参数先存栈再加载 | 可分配的整数参数从 ABI 入参寄存器直接转入保存值的寄存器；64 位参数占用完整寄存器，小整数仍可共享位段 |

参数进入函数后存栈本身符合 AMD64 调用约定；本次优化减少这部分读写。寄存器分配仍会保存和恢复实际使用的 `RBX`、`R12`～`R15`。跨基本块的临时值、PHI、重复定义和寄存器不足时保留栈槽；本次没有启用实验性的 SSA 优化。

缓存是**进程内缓存**，不跨独立 CLI 进程持久化。缓存键包含路径和预处理模式；文件大小、修改时间、状态变更时间及文件身份变化会使条目失效，Linux 还比较纳秒时间。失败的解析不会回退到旧 AST。每次编译使用自己的 AST 深拷贝，语义分析可以修改它；缓存访问由互斥锁保护。缓存最多保留 256 个条目，并按源码及 arena 占用设置约 64 MiB 的淘汰预算。

## 验证

在 `Re.KrtC` 目录构建：

```sh
zig build --global-cache-dir /tmp/kairote-zig-cache
```

在项目根目录运行：

```sh
python3 Test/Performance/test_compiler_hotspots.py
python3 -m unittest discover -s Test/IntegerWidths -p 'test_*.py'
bash Test/SyntaxAudit/run.sh
```

性能回归脚本需要系统 C 编译器、`objdump` 和已构建的 `ArkLink/build/libarklink`。10 项检查覆盖宏边界和索引扩容、符号遮蔽和嵌套作用域、缓存修改隔离和失效、并发读取、Pipeline 重用、大小文件调度，以及实际机器码与运行结果；其中包含普通整数冗余高位指令检查。整数专项另有 12 项测试覆盖所有偶数位宽、溢出、运算、调用、内存布局和寄存器打包。

2026-09-12 更新：原有枚举、泛型、命名空间和 `ref` 四项问题已修复，当前完整语法审计 **75/75 通过**；性能测量仍保留各次实验的原始结果。

## 复现测量

```sh
python3 Test/Performance/Benchmark.py --rounds 5 --output /tmp/Kairote-Performance.json
```

若保留了本轮修改之前的编译器，可同时对比：

```sh
python3 Test/Performance/Benchmark.py --rounds 5 \
  --baseline-ref f54a9b459142b9a0deb8efc58daa1119586dd979 \
  --baseline-compiler Re.KrtC/build/KrtC-before-performance \
  --output /tmp/Kairote-Performance.json
```

组件对比只替换该版本的 `Preprocessor.c`、`SymbolTable.c`、`ParallelCompiler.c`，其余实现和构建参数相同。缓存对比为当前实现的首次加载与进程内重复加载。机器码对比使用本轮性能修改前保存的完整编译器，该编译器已经包含偶数整数位宽和局部变量寄存器打包。

每项测量取 5 轮中位数，新旧版本交替执行。预处理计时不含宏注册，符号查找计时不含声明；批编译包含创建、执行、销毁及 IR 文件写入，不含输入文件生成。机器码分别统计主程序 `.text` 和所有带 `CODE` 标记的代码段，运行结果也会核对。毫秒级端到端耗时容易受系统负载影响，不能据此把单个热点的提速倍数外推到整个编译器。

## 本次结果

2026-09-12，Linux x86-64，组件使用 `cc -std=gnu11 -O2`。完整数据见 `Results.json`。此表记录首次性能优化后的快照，早于后续普通整数指令精简；后续数据见上方的运行性能及 Fibonacci 对照报告。

| 用例 | 优化前 | 优化后 |
|---|---:|---:|
| 258 个宏，882,000 字节输入的预处理 | 918.23 ms | 1.68 ms |
| 4000 个符号，160,000 次作用域链查找 | 1417.98 ms | 6.21 ms |
| 64 个小文件编译为 IR | 6.29 ms | 3.92 ms |
| 1200 个库函数：首次加载 / 缓存重复加载 | 2.90 ms（首次） | 0.46 ms（重复） |
| 空 `main` 的代码段 | 113 字节 | 92 字节 |
| Hello 主程序 `.text` | 106 字节 | 76 字节 |
| Hello 加标准库的全部代码段 | 49,967 字节 | 43,808 字节 |
| 两参数表达式示例的代码段 | 824 字节 | 749 字节 |
| 两参数表达式示例的栈帧内存访问指令数 | 33 | 14 |

表达式用例为 `(a+b)*(a-b)+(a^b)`，包含调用它的 `main`；内存访问数字是反汇编中的静态 `[rbp-...]` 引用数，包含保存、恢复寄存器。三个本机程序的输出或退出码均核对通过。

本次单次 CLI 编译 Hello 为 52.64 ms → 53.05 ms，空程序为 1.11 ms → 1.19 ms，表达式示例为 1.20 ms → 1.37 ms，没有证明这类单文件编译整体提速。缓存的收益来自同一进程中的重复加载，寄存器分配也有编译时成本；上表的热点收益和产物缩小应分别理解。
