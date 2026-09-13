本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。 测量数值与构建哈希属于文中记录的历史版本，不代表后续规范整改后的新测量。

本轮在 Re.KrtC 的 KRO 后端加入通用自尾递归消除，以及固定整数位宽下的累加器转换。判断依据是 IR 类型、使用关系、调用后的操作和栈帧生命周期，没有按 Fib 名称、35/40 输入或递归深度选择代码路径。函数改名、其他递推关系、全部 128 种偶数整数类型均有回归验证。

2026-09-13，本机 AMD Ryzen 9 8945HX，固定逻辑 CPU 0。每项每个版本运行十次，轮换版本顺序、串行执行；计时期间没有并行构建或测试。Krt 和 C 都通过 syscall 读取 CLOCK_MONOTONIC，只测一次 Kernel 调用，排除编译、进程启动和输出。C 函数使用 `noinline`，计时驱动分开编译，无 LTO；GCC 16.2.1 使用 `-O2 -fwrapv`，允许正常的尾递归与循环优化。

| 负载 | 修改前 KrtC / ms | 当前 KrtC / ms | GCC O2 / ms | KrtC 均值下降 |
|---|---:|---:|---:|---:|
| 递归 Fib35 | 31.688943 | 21.329194 | 17.972713 | 32.69% |
| 递归 Fib40 | 336.545415 | 215.896001 | 187.331298 | 35.85% |
| 尾递归求和，约一万层 × 50 次 | 1.149342 | 0.397639 | 0.099617 | 65.40% |
| 递归阶乘，15～22 × 四万次 | 0.938590 | 0.338272 | 0.197335 | 63.96% |
| 欧几里得算法，八万组输入 | 4.107115 | 3.154990 | 3.311125 | 23.18% |
| 不同分支递推：P(n−1)+P(n−3)，n=42 | 37.217179 | 19.571816 | 11.937480 | 47.41% |
| 二叉递归：T(n−1)+T(n−1)，n=22 | 12.435924 | 6.794770 | 4.533488 | 45.36% |
| 百万次整数混合循环，使用 `^=` | 结果错误，不计均值 | 1.183774 | 1.232693 | — |
| 同一整数循环，展开为 `x = x ^ (...)` | 1.179025 | 1.167983 | 1.184249 | 0.94% |

以上均为完整十次的算术平均值，没有删除慢样本。Fib35 的修改前/后中位数为 30.355446 / 19.736502 ms，十次范围分别为 30.288～41.440 / 19.064～28.293 ms；Fib40 中位数为 336.115951 / 214.617598 ms，范围为 335.134～341.375 / 211.938～229.151 ms。当前 Fib40 的均值仍比 GCC 高约 15.25%。展开的普通循环前后机器码逐字节相同，0.94% 的计时差不代表这项获得了代码优化。

全部 270 次执行记录、数值校验、CPU 时间、命令和 SHA-256 保存在 `Results.json`。其中 260 次结果正确；10 次失败均为旧版的 `^=` 循环，返回 1794897017，正确结果是 1267360084。旧版没有识别 `^=`，还静默丢弃了该表达式，错误执行不能作为性能成绩。当前已贯通词法、语法、类型检查和 IR，支持 `&= |= ^= <<= >>=`；普通变量、参数、全局、引用、指针与数组元素共用计算逻辑，先捕获左值和旧值再计算右侧。位运算拒绝非整数操作数；缺少右操作数和不支持的赋值目标会报错。新增 [复合赋值回归](../../IntegerWidths/test_compound_bitwise.py) 覆盖这些行为，并验证百万次循环的结果。

该轮基线是上一轮已经优化过的编译器，SHA-256 为 `1a04b02c11de101ac5d656b75f5414d42e41a694f9b728cca8361447f6fcda4e`；测量使用的修改后编译器为 `baed7a2e6d60a86f0146de23fd30e703ab2624872263a90f22235dbf19bbc39d`。发现复合赋值错误之前完成的首轮 210 个样本与对应编译器、汇编保存在本地忽略目录 `BeforeCompoundFix/`，该轮未完成，不与最终样本混合。最终表中的两种循环写法都有独立的等价 C/Krt 源码与汇编。

实现主要在 [KroTailCalls.inc](../../../Re.KrtC/src/compiler/Backend/Kro/KroTailCalls.inc)，由 [寄存器和栈规划](../../../Re.KrtC/src/compiler/Backend/Kro/KroRegisterPacking.inc) 与 [代码生成](../../../Re.KrtC/src/compiler/Backend/Kro/KroCodegen.c) 接入：

- 自尾调用复用当前栈帧；先保存全部新参数，再更新原参数，保留参数交换、循环依赖和共享寄存器位段。宽参数、超过寄存器数量的参数和引用参数均有测试。
- 固定位宽的加法、乘法、按位与/或/异或可累积到保留寄存器。利用整数模 2^N 的语义，最终返回时按原类型归一化；超过 64 位使用两个寄存器。这里不重结合浮点运算，也不将减法等非结合运算转换成累加器。
- 调用后有副作用、读取可能变化的全局值、存在实际窄化转换时保留原调用。当前还保守排除含局部存储、取地址、stackalloc、数组参数或 PHI 的函数，避免改变可观察的栈帧生命周期。
- 原有快速入口返回仍有效；跳回循环头时重新执行原控制流判断，累加器仅在初次进入时初始化。生成参数和返回值时复用已有的宽整数及寄存器打包逻辑。

实际汇编的区别如下。静态大小和指令数由 [Inspect.py](Inspect.py) 从保留的反汇编计算，不含末尾填充，详细结果在 `Analysis.json`；它们不是硬件动态指令计数。

| 函数 | 旧版静态 call 数 | 当前静态 call 数 | 旧版 / 当前函数字节数 | 当前 / GCC 静态指令数 |
|---|---:|---:|---:|---:|
| Recur（Fib35/40） | 2 | 1 | 72 / 108 | 34 / 23 |
| Sum | 1 | 0 | 103 / 151 | 47 / 14 |
| Product | 1 | 0 | 94 / 106 | 29 / 9 |
| Divisor | 1 | 0 | 100 / 148 | 47 / 17 |
| Paths | 2 | 1 | 106 / 114 | 33 / 20 |
| Tree | 2 | 1 | 106 / 114 | 33 / 21 |

Fib 的一条递归分支已改成循环；按该递推式计算，Fib40 的函数进入次数从 331160281 降到 165580141，包含最外层调用。函数静态代码变大，但减少了动态调用与建栈次数。Fib35/40 的对应函数机器码完全一致。可查看 旧版 Recur（`Functions/Fib40Before.asm`）、当前 Recur（`Functions/Fib40After.asm`）、GCC Recur（`Functions/Fib40Gcc.asm`），以及完整 GCC 汇编（`Artifacts/Fib40Gcc.s`） 和 等价 C（`Artifacts/Fib40.c`）。

剩余差距可以直接从汇编看到：当前 Recur 保存了额外临时寄存器，循环头有多余搬运和回跳，返回附近仍有重复扩展；多个尾调用参数通过栈快照完成并行复制。Sum（`Functions/TailSumAfter.asm`） 因此仍有四次参数快照读写，而 GCC Sum（`Functions/TailSumGcc.asm`） 完全用寄存器并做了循环展开。这轮尚未加入通用循环展开、跨基本块寄存器分配和参数并行复制排程，不能据这些结果声称已全面追平 GCC。

新增 [12 项尾调用回归](../test_tail_calls.py) 包含所有偶数位宽的五种结合运算、零次递归、溢出回绕、不同名称和递推关系、参数置换、宽参数、栈参数、引用副作用、互递归、浮点与布尔转换、全局值、取地址和栈分配。20 万层自尾调用在 256 KiB 栈限制下通过；不能转换的有副作用或地址生命周期用例仍保持原语义。复合赋值另新增 7 项测试方法，其中循环生成了全部位宽及非法操作数组合。

最终检查均通过，以下结果已复制到本目录，避免后续运行覆盖历史证据：

| 检查 | 结果 |
|---|---|
| 指针（`Validation/Pointers.json`） | 50 项；457 次编译，355 次成功、102 次预期拒绝；349 次正常执行、4 次预期 SIGILL、2 次预期 SIGSEGV |
| 整数（`Validation/Integers.json`） | 19 项；544 次编译，485 次成功、59 次预期拒绝；485 次程序正常执行 |
| 语言（`Validation/Language.json`） | 25 项；65 次编译，36 次成功、29 次预期拒绝 |
| 性能与代码生成（`Validation/Performance.json`） | 32 项；177 次 Krt 编译成功，含 C 组件和反汇编检查 |
| [基础语法](Validation/Syntax.md) | 75/75；无编译失败、错误输出或崩溃 |
| 质量（`Validation/Quality.json`） | 4 个 C 组件检查及 3 项原生检查通过 |
| GCC / Clang 审计（`Validation/Audit.json`） | 各 65 个翻译单元，0 警告、0 错误、0 格式问题 |
| 构建 | Zig、CMake Release、全量 ASan/UBSan 构建通过 |

另外用全量 ASan/UBSan 编译器和链接器重跑 指针（`Validation/SanitizerPointers.json`）、整数（`Validation/SanitizerIntegers.json`）、语言（`Validation/SanitizerLanguage.json`） 与 性能回归（`Validation/SanitizerPerformance.json`），全部通过。生成的 Krt 程序本身没有插桩；预期信号用例在隔离子进程中运行并关闭 core dump。本环境使用 `detect_leaks=0`，不据此宣称通过了 LSan；C 内存回归另有受跟踪分配平衡断言。

复现时先构建并跑完回归，再单独计时。基线默认读取本地 `Re.KrtC/build/GeneralBaseline/KrtC`，其他环境可用 `--baseline` 指定同版本编译器。基准逐次检查结果；旧版错误会如实保留且不产生均值，当前编译器或 GCC 结果错误会导致命令失败。

```bash
python3 Test/Pointers/RunTests.py --suite Test/Performance --output Test/Performance/RegressionResults.json
python3 Test/Pointers/RunTests.py --suite Test/IntegerWidths --output Test/IntegerWidths/Results.json
python3 Test/Performance/GeneralOptimization/Run.py --prepare-only
python3 Test/Performance/GeneralOptimization/Run.py --measure-only --rounds 10 --cpu 0
python3 Test/Performance/GeneralOptimization/Inspect.py
```
