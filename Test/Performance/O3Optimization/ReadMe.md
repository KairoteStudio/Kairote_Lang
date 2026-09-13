本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。 测量数值与构建哈希属于文中记录的历史版本，不代表后续规范整改后的新测量。

已加入可选择的 `-O0`～`-O3`，默认为 O2；使用通用 IR 和机器指令优化，没有按 Fibonacci 名称、35/40 输入或递归深度选择算法。**当前尚未达到 GCC O3 的整体速度目标。** 十次对测中，小函数循环和整数循环已接近 GCC；Fib35/40 仍约慢 2.4～2.5 倍，部分其他负载在 O3 下比默认级别略慢，完整列在下面。

```bash
Re.KrtC/build/KrtC -O3 Test.krt -o Test
Re.KrtC/build/KrtC -O3 build project.krt
```

单文件、多文件和项目构建都传递优化级别，重复指定时最后一个生效。O0 关闭 IR 优化与快速返回/尾递归转换；O1 开启已有的局部 IR 优化与快速返回；O2 增加自尾递归和整数累加器；O3 增加小函数内联与下面的后端优化。这些名称不代表实现了 GCC 对应级别的全部优化，也不改变浮点语义。

2026-09-13，同机 AMD Ryzen 9 8945HX、逻辑 CPU 0，GCC 16.2.1。每项每个版本运行十次，轮换版本顺序、串行执行；计时期间没有并行构建或测试。Krt 和 C 使用 syscall/CLOCK_MONOTONIC 测一次 Kernel 调用，排除编译、启动及输出。C 与 Krt 算法、类型、输入一致，GCC 使用 `-O3 -fwrapv -fno-lto`，计时驱动单独编译，**没有 noinline 限制**，允许 GCC 自然内联、递归展开和循环优化。此前报告中的 GCC O2/noinline 数据不能代替这里的 O3 对照。

| 负载 | 修改前默认 / ms | 当前 O2 / ms | 当前 O3 / ms | GCC O3 / ms | O3 / GCC 耗时比 |
|---|---:|---:|---:|---:|---:|
| 递归 Fib35 | 21.046413 | 19.762706 | 18.825690 | 7.757810 | 2.427× |
| 递归 Fib40 | 217.249220 | 219.090008 | 208.285707 | 83.696973 | 2.489× |
| 尾递归求和，约一万层 × 50 | 0.401307 | 0.405225 | 0.201722 | 0.104720 | 1.926× |
| 递归阶乘，15～22 × 四万 | 0.344673 | 0.336718 | 0.346216 | 0.194005 | 1.785× |
| 欧几里得算法，八万组 | 3.155128 | 3.146461 | 3.312446 | 3.261867 | 1.016× |
| P(n−1)+P(n−3)，n=42 | 19.826113 | 19.833118 | 19.692305 | 5.113816 | 3.851× |
| T(n−1)+T(n−1)，n=22 | 6.639851 | 6.529551 | 7.120470 | 1.295699 | 5.495× |
| 百万次整数混合，复合赋值 | 1.181363 | 1.192286 | 1.178988 | 1.218644 | 0.967× |
| 同一循环，展开赋值 | 1.184450 | 1.182906 | 1.199053 | 1.214607 | 0.987× |
| 同一循环，两个小函数调用 | 2.551624 | 2.519567 | 1.225055 | 1.228617 | 0.997× |

全部均值保留十个样本，未剔除慢样本；400 次运行均验证了数值与退出状态。`Results.json` 包含每次耗时、CPU 时间、返回值、全部构建命令、编译器/源文件/产物 SHA-256。Fib35 当前 O3 的十次范围为 18.626～19.130 ms，GCC 为 7.717～7.823 ms；Fib40 分别为 207.269～211.749 ms 和 82.832～84.698 ms。修改前 Fib35 有一个 33.342 ms 样本，也完整保留。不能用单次最快值声称追平。

该轮起点为 `baed7a2e6d60a86f0146de23fd30e703ab2624872263a90f22235dbf19bbc39d`，测量使用的修改后编译器为 `0eb221e80059667d92eb3cc6de3e4894f6f394097beaba9eae2e63bcf8082d89`。生成的 C 源码（`Artifacts/Fib40.c`）、Krt 源码（`Artifacts/Fib40.krt`）、GCC -S 汇编（`Artifacts/Fib40Gcc.s`）与四个版本的完整反汇编均位于本地 `Artifacts/`；ELF/KRO/对象文件也仅在本地归档。首轮、返回条件内联试验的结果分别保存在本地 `Probes/First` 和 `Probes/Guard`，没有混入最终样本；返回条件内联因实测未提速而撤回。

实际实现与边界：

- [IR 内联](../../../Re.KrtC/src/compiler/Middle/Ir/IrInline.inc)：单基本块纯整数函数，最多 32 条指令、8 个参数，每个调用方增长预算 256；原有函数超过 1024 条指令时不扩张。候选表排序查找，克隆临时值并维护指令索引；保留调用方参数求值顺序。内存、局部可变槽、函数调用及控制流仍保留原调用。
- [64 位指令选择](../../../Re.KrtC/src/compiler/Backend/Kro/KroInteger.inc)：直接使用寄存器和可编码立即数，严格区分 x86 立即数的符号扩展与整数位宽；不适用时回退原路径。测试覆盖立即数边界、乘法/按位运算和回绕。
- [尾递归](../../../Re.KrtC/src/compiler/Backend/Kro/KroTailCalls.inc)：单次使用的调用结果可直接累积；2～5 个标量参数在有足够安全暂存寄存器时先读取全部新值，再更新参数，保留置换环和共享寄存器邻位。宽参数、寄存器不足等情况使用已有栈快照。
- [循环布局](../../../Re.KrtC/src/compiler/Backend/Kro/KroLeafReturn.inc)：可证明纯净的入口判断在尾循环末尾直接执行，减少回到原入口的无条件跳转；限制复制数量。取地址、stackalloc、浮点、副作用、非结合运算及窄化转换等边界保留原语义。
- 后端槽只在分配时初始化，避免每个函数都清空最大容量；[编译速度报告](../CompileSpeed/ReadMe.md) 给出原有退化、最终修复和 O3 编译成本。
- 标准库 Memory/String 的原生指针操作补齐 unsafe，并改用指针访问复制/填充字节。自动 stdlib 已属于当前 IR，去掉链接阶段重复编译及其旧对象缓存；项目构建改用 KRO，避免误走旧汇编后端、依赖缺失的 runtime.o，并正确设置执行权限。IR 调用名称由 arena 回收，避免内联后遗留堆分配。

汇编统计由 [Inspect.py](Inspect.py) 从实际产物提取，详见 `Analysis.json`。静态指令数不能当作动态执行次数：

| 核心函数 | O2 字节 / 指令 | O3 字节 / 指令 | GCC O3 字节 / 指令 |
|---|---:|---:|---:|
| Fib40 | 108 / 34 | 82 / 27 | 924 / 235 |
| TailSum | 151 / 47 | 102 / 32 | 45 / 14 |
| Product | 106 / 29 | 92 / 25 | 31 / 9 |
| Gcd | 148 / 47 | 109 / 35 | 47 / 17 |
| Branching | 114 / 33 | 85 / 24 | 831 / 192 |
| BinaryTree | 114 / 33 | 85 / 24 | 517 / 122 |
| HelperLoop | 161 / 50 | 230 / 70 | 63 / 16 |

当前 Recur（`Functions/Fib40O3.asm`） 仍有一个递归调用点；另一路是尾循环。GCC 的 Recur（`Functions/Fib40Gcc.asm`） 展开了多层递归，较大的函数体减少了动态建栈和调用次数，调用处还允许进一步内联。Krt 的函数更短并不意味着更快。Fib35/40 的 Krt Recur 机器码逐字节一致；小函数循环的调用从两个降为零，代码变化见 O2（`Functions/HelperLoopO2.asm`） / O3（`Functions/HelperLoopO3.asm`）。

目前缺少通用的多层递归/跨控制流内联、完整循环展开、跨基本块寄存器分配和自动向量化。本轮不将这些缺失能力计入已完成的 O3，也不声称实现了与 GCC O3 同速。二叉递归、Gcd、阶乘的 O3 均值均高于当前 O2；需要继续通过通用优化及独立负载验证解决，不能用缩小基准集合掩盖。

[新增优化级别回归](../test_optimization_levels.py) 覆盖全部 128 种整数类型、64 位立即数边界、参数环、函数间存储隔离、可观察副作用、不同入口条件、项目/多文件构建、自动标准库和源码库的内存复制/填充。原有尾调用测试现在验证实际向后分支，兼容布局调整后的条件分支。

| 验证 | 每个级别的结果 |
|---|---|
| 指针 | 50 项；457 次编译，其中 102 次预期拒绝 |
| 整数 | 19 项；544 次编译，其中 59 次预期拒绝 |
| 性能、优化级别和代码生成 | 43 项；包含所有级别、C 组件和反汇编检查 |
| 语言专项 | 25 项；65 次编译，其中 29 次预期拒绝 |
| 基础语法 | 75/75 |
| 质量 | 4 个 ASan/UBSan C 组件与 3 项原生检查 |
| 构建与审计 | Zig、CMake Release、完整 ASan/UBSan 构建通过；GCC/Clang 各 65 个翻译单元，0 警告、0 错误、0 格式问题 |

上述五组语言/代码生成回归分别使用默认 O2、O3，以及完整 ASan/UBSan 编译器的 O2、O3 运行，均通过。原始报告保存在 Validation（`Validation/Native.json`）（sanitizer（`Validation/Sanitizer.json`）、质量（`Validation/Quality.json`）、审计（`Validation/Audit.json`））。补充的标准库实际内存复制/填充用例在原生和 ASan/UBSan 编译器的 O0/O2/O3 均通过，记录在 `Validation/Library.json`。生成的 Krt 程序本身没有插桩；指针信号测试在隔离子进程中运行。环境使用 `detect_leaks=0`，不声称通过 LSan；C 内存与 IR 回归另有跟踪分配平衡断言。

复现时先构建和验证，再单独计时：

```bash
python3 Test/Performance/O3Optimization/Validate.py
python3 Test/Pointers/BuildSanitized.py
python3 Test/Performance/O3Optimization/Validate.py --compiler /tmp/KrtC-pointer-sanitized --sanitized
python3 Test/Performance/O3Optimization/Run.py --prepare-only
python3 Test/Performance/O3Optimization/Run.py --measure-only --rounds 10 --cpu 0
python3 Test/Performance/O3Optimization/Inspect.py
```

基线默认使用本地 `Re.KrtC/build/BeforeO3/KrtC`；其他环境可通过 `--baseline` 指定对应版本。`Run.py` 校验结果并拒绝错误产物，`Inspect.py` 验证保存文件的哈希与全部 400 个样本。
