本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。 测量数值与构建哈希属于文中记录的历史版本，不代表后续规范整改后的新测量。

此页保留上一轮测量。当前版本已加入通用尾递归消除，最新结果见 [九组负载十次复测](../GeneralOptimization/ReadMe.md)。

本轮优化已加入 Re.KrtC 的 KRO 后端。规则依据 IR 类型、控制流和临时值的使用关系，适用于满足条件的函数。

- 将纯入口比较及直接返回参数/常量的分支移到栈帧之前；确认没有其他入口后，删除原来的重复分支。
- 单次使用、紧邻消费的整数临时值直接通过 RAX/RCX 传递；32 位交换律运算可直接使用调用返回值。
- 32 位加减乘、位运算和比较使用对应指令，常量加减使用 LEA；仅在唯一消费者只读取低 32 位时省去临时值的扩展。整数提升、共享值和内存存储仍保留必要转换。
- 不为无使用者的结果、已融合的比较条件分配栈槽；按实际存储量建立栈帧。参数和临时寄存器免于多余清零，含局部变量的打包寄存器仍初始化。
- 函数按 16 字节对齐；末尾返回直接落入共享 epilogue，并正确处理 return 后的不可达 IR。

实现见 [叶子返回](../../../Re.KrtC/src/compiler/Backend/Kro/KroLeafReturn.inc)、[整数指令](../../../Re.KrtC/src/compiler/Backend/Kro/KroInteger.inc)、[寄存器与栈规划](../../../Re.KrtC/src/compiler/Backend/Kro/KroRegisterPacking.inc) 和 [代码生成](../../../Re.KrtC/src/compiler/Backend/Kro/KroCodegen.c)。

2026-09-13 在本机 AMD Ryzen 9 8945HX 上固定逻辑 CPU 0，逐个进程轮换运行，每项十次。表中是完整十次的算术平均值，单位 ms；编译、进程启动和输出不在计时区间内。两侧都通过 syscall 读取 CLOCK_MONOTONIC，并逐次检查 Fib35 = 9227465、Fib40 = 102334155。

| 实现 | Fib35 十次均值 | Fib40 十次均值 |
|---|---:|---:|
| 本轮修改前 KrtC | 57.469840 | 764.582753 |
| 本轮修改后 KrtC | 32.568391 | 341.345482 |
| GCC O2，保留两处递归调用 | 28.525778 | 306.739446 |
| GCC O2，允许递归转循环 | 18.561423 | 201.351980 |

两项均值分别下降 43.33% 和 55.36%。环境存在波动：旧版 Fib40 的十次范围为 612.519–1545.496 ms，新版为 338.833–344.636 ms，因此不能将 55.36% 全部归因于代码优化。补充看中位数：Fib35 为 **55.742 → 30.702 ms**，Fib40 为 **620.054 → 340.950 ms**，两者均下降约 **45%**。所有慢样本均计入均值。中间版本的首轮十次数据也完整保留在 `FirstPassResults.json`，其二进制及汇编归档于本地忽略目录 `FirstPass/`。

最终原始样本、CPU 时间、编译命令、编译器/源码/产物 SHA-256、KRO 重定位核对和指令统计均在 `Results.json`。修改前编译器 SHA-256 为 `d717afd1e47dac05ec4ab2cedb83b733cf5c7132ad68a804453c214bef151288`，修改后为 `1a04b02c11de101ac5d656b75f5414d42e41a694f9b728cca8361447f6fcda4e`。

| Fib 代码特征 | 修改前 | 修改后 |
|---|---:|---:|
| 函数机器码，不含末尾填充 | 192 字节 | 72 字节 |
| 静态指令数，不含末尾填充 | 58 | 24 |
| 叶子路径执行指令 | 27 | 4 |
| 叶子路径显式栈访问 | 8 | 0 |
| 非叶子路径自身指令，含两次 call，不含子调用体 | 53 | 22 |
| 非叶子路径符号扩展 | 16 | 1 |
| 非叶子单次调用额外栈空间，不含传入返回地址 | 120 字节 | 24 字节 |

以上路径统计是对保留汇编的离线核算，不是硬件计数器。新版仍执行两处递归调用，Fib40 总函数进入次数仍为 331160281。GCC 普通 O2 将一条递归分支转为循环，函数进入次数为 165580141；这一转换尚未加入 KrtC。相同双递归的 GCC 也仍有更少的非叶子指令和栈访问，后续可继续改善栈帧与参数传递。

可直接查看 修改前 Fib 汇编（`Artifacts/KrtBefore35.Fib.asm`）、修改后 Fib 汇编（`Artifacts/KrtAfter35.Fib.asm`）、GCC 保留递归汇编（`Artifacts/GccRecursive.s`）、GCC O2 汇编（`Artifacts/GccO2.s`），以及 修改后 KRO 原始反汇编（`Artifacts/KrtAfter35.RawFib.asm`）。Fib35/40 对应函数体逐字节相同；KRO 与 ELF 除重定位字段外一致。两个 GCC 版本使用相同的 [C 源码](../NativeFib/Fib.c) 和 [计时驱动](../CodegenComparison/Driver.c)，分别编译，无 LTO，带 `-fwrapv`；保留递归版本另外使用 `-fno-optimize-sibling-calls`。

新增 [test_scalar_codegen.py](../test_scalar_codegen.py) 的 10 项回归覆盖六种比较及符号边界、真假返回分支、循环、副作用、转换、宽/栈参数、随机整数运算、立即数编码边界、嵌套调用、函数指针、打包邻接字段、共享值与整数提升。全部最终检查通过：

| 检查 | 结果 |
|---|---|
| 指针专项 | 50 项；457 次编译，355 次成功、102 次预期拒绝 |
| 指针程序执行 | 349 次正常通过、4 次预期 SIGILL、2 次预期 SIGSEGV，信号用例隔离执行 |
| 整数宽度与打包 | 12 项通过 |
| 语言专项 | 25 项通过 |
| 编译热点及代码生成 | 20 项通过，含本轮新增 10 项 |
| 基础语法 | 75/75 通过，无编译失败、输出错误或崩溃 |
| 质量专项 | 4 个 C 组件检查及 3 项原生检查通过 |
| GCC / Clang 全项目审计 | 各 65 个翻译单元；0 警告、0 错误、0 格式问题 |
| 构建 | Zig 和 CMake Release 均通过 |

指针、整数、语言及代码生成用例另外使用全量 ASan/UBSan 编译器与链接器重跑通过。生成的 Krt 程序本身不带 Sanitizer。当前环境关闭 LSan；C 内存检查另有受跟踪分配平衡断言。对应结果见 指针（`../../Pointers/Results.json`）、整数（`../../IntegerWidths/Results.json`）、语言（`../../SyntaxAudit/LanguageResults.json`）、性能回归（`../RegressionResults.json`）、Sanitizer 性能回归（`../SanitizerRegressionResults.json`）、质量（`../../Quality/Results.json`） 和 审计（`../../Quality/AuditResults.json`）。

复现时先完成构建与测试，再独立执行计时。基线默认读取本地保留的 `Re.KrtC/build/CodegenBaseline/KrtC`；在其他 checkout 可用 `--baseline` 指定同版本编译器。

```bash
python3 -m unittest discover -s Test/Performance -p 'test_*.py' -v
python3 Test/Performance/CodegenOptimization/Run.py --prepare-only
python3 Test/Performance/CodegenOptimization/Run.py --measure-only --rounds 10 --cpu 0
```
