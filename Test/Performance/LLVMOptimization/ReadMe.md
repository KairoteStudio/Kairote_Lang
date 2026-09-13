本页记录规范整改前的一轮优化、测量和验证；“当前”指该轮构建，数值与哈希不代表后续源码的新测量。原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。

后续开发规范整改及最新正确性验证见 [StandardsReadMe.md](../../Quality/StandardsReadMe.md)。

该轮通过三个并行子代理研究 LLVM、GCC 与相关论文，将通用优化接入 KrtC 的 `-O3`。Fib35、Fib40、分支递归和重复等参递归快于本机自然 GCC O3；三个整数循环接近或略快。乘积测试仍慢约 8.5%，求和与 GCD 的该轮均值慢约 2%，因此不宣称所有程序都已超越 GCC。

**十次运行平均耗时。** 单位 ms，均保留全部十个样本。修改前与当前两列都使用 `-O3`；当前 `-O2` 的完整数据也保存在 `Results.json`。

| 负载 | 修改前 O3 | 当前 O3 | GCC O3 | 当前 / GCC |
|---|---:|---:|---:|---:|
| Fib35 | 22.602071 | 0.045711 | 9.688739 | 0.0047× |
| Fib40 | 205.311459 | 0.141764 | 82.817873 | 0.0017× |
| TailSum | 0.211221 | 0.101870 | 0.099852 | 1.0202× |
| Product | 0.350610 | 0.205403 | 0.189256 | 1.0853× |
| Gcd | 3.280278 | 3.304974 | 3.240385 | 1.0199× |
| Branching | 19.629191 | 0.259782 | 5.053395 | 0.0514× |
| BinaryTree | 7.054949 | 0.001187 | 1.283948 | 0.0009× |
| IntegerLoop | 1.171630 | 1.190745 | 1.206774 | 0.9867× |
| ExpandedLoop | 1.171630 | 1.170324 | 1.200243 | 0.9751× |
| HelperLoop | 1.184334 | 1.190389 | 1.223159 | 0.9732× |

测试机器为 AMD Ryzen 9 8945HX with Radeon Graphics，固定 CPU 0；GCC 版本为 `gcc (GCC) 16.2.1 20260810`。测量时间为 `2026-09-13T05:03:06.738194+00:00`。每组先记录一次有效预热，再串行轮换四个版本，测十次；40 次预热和 400 次正式运行全部返回正确数值。

两端均在一次 Kernel 调用前后执行 `CLOCK_MONOTONIC` 系统调用，排除进程启动、编译和结果输出。C 的 Kernel 与 [Driver.c](Driver.c) 分开编译，使用 `-O3 -fwrapv -fno-lto -fno-pie`；未限制 GCC 的正常内联。Krt 和 C 都在测量区间真实计算，未把输入折叠为预计算结果。微秒级 BinaryTree 的包装和时钟开销占比较高，不宜解读微小时间差。

等价源文件副本、GCC `.s`、反汇编和二进制保存在本地 `Artifacts/`，核心函数摘录和静态大小分别在本地 `Functions/` 与 `Analysis.json`；源程序由 [Run.py](Run.py) 生成。该轮 Fib35/Fib40 的递归函数机器码一致。修改后 Recur 是 145 字节、两个静态 CALL 站点；修改前是 82 字节、一个站点。速度提升来自减少动态递归子问题，静态 CALL 数量本身不能说明执行了多少次。

**已实现的通用规则。**

- 用封闭调用图推断没有内存访问或可观察副作用的函数，支持递归调用环；按目标、实参和精确整数类型复用同一基本块内已经返回的调用结果。内存访问、未知调用等会使相应函数及调用者退出该分析。
- 自适应递归内联从一层试起，首次能通过 CSE 消除重复自调用即停止，没有复用时最多三层。生成体最多 384 条 IR，模块净增长不超过 `max(384, 原始 IR / 4)`；失败保留原函数或先前成功的候选。没有按名称、源码路径、35/40 等输入分支。
- 按源位宽和符号规则规范化常量，把等价参数表达式统一为原参数加常量；使用精确的 128 位整数运算，保留窄整数回绕。
- 清除无用的非陷阱整数临时值，保留第一次函数调用、除法、取余和内存操作；保留非标准 bool 字节转为 0/1 的必要转换。
- 合并安全的整寄存器复制，直接比较已有寄存器；为单定义、支配所有使用点的无环跨块临时值分配寄存器。循环、非 SSA、地址、压力和大小边界保留回退。
- 根据旧参数的读取依赖安排尾调用参数更新，有环时继续使用快照。TailSum 热循环从约 11 条缩至 4 条指令；整数返回值等于结合运算恒等元时，直接返回已归一化的累加器，消除最终乘 1、加 0 等操作。

这条优化路线参考了 [LLVM EarlyCSE](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/lib/Transforms/Scalar/EarlyCSE.cpp)、尾递归变换和寄存器分配研究。完整的一手资料、六篇论文/报告的适用范围、21 组 GCC/Clang 消融结果说明见 [Research.md](Research.md)。[BudgetSelection](BudgetSelection/ReadMe.md) 记录六种预算策略、六种不同程序的 108 次正确运行，并说明本地源码快照和二进制的复测前提；它是三次方向实验，不替代本页十次均值。

**编译耗时。** 两组各 14 个输入/目标组合、两版编译器、每版十次，共 560 次正式编译，全部成功，生成的可执行文件也校验正确。单次耗时包含新进程、解析、优化、链接和写文件；预热一次，轮换顺序，固定 CPU 0。默认仍是 O2。

| 输入 / 目标 | 默认修改前 | 默认当前 | O3 修改前 | O3 当前 | O3 变化 |
|---|---:|---:|---:|---:|---:|
| Empty | 2.648 | 2.592 | 2.675 | 2.624 | -1.9% |
| Fib | 2.842 | 2.769 | 2.949 | 3.101 | +5.2% |
| Hello | 25.368 | 25.795 | 23.458 | 24.215 | +3.2% |
| SysImport | 3.738 | 3.695 | 3.777 | 3.819 | +1.1% |
| Pointers | 2.642 | 2.667 | 2.656 | 2.706 | +1.9% |
| Functions200 | 10.121 | 10.080 | 10.226 | 10.624 | +3.9% |
| Functions1000 | 102.252 | 102.563 | 102.213 | 106.293 | +4.0% |
| Recursive500 | 169.169 | 170.401 | 172.613 | 84.259 | -51.2% |
| BranchRecursive500 | 175.959 | 175.271 | 178.116 | 128.823 | -27.7% |
| IntegerWidths | 20.342 | 20.147 | 20.215 | 21.400 | +5.9% |
| Dense500 | 6.849 | 6.797 | 6.753 | 7.548 | +11.8% |
| Functions1000IR | 24.800 | 24.727 | 26.243 | 28.429 | +8.3% |
| Recursive500IR | 25.095 | 24.822 | 25.760 | 33.837 | +31.4% |
| Dense500IR | 6.196 | 6.039 | 6.175 | 6.862 | +11.1% |

默认编译的大多数样例基本持平；O3 增加分析工作，并非所有编译都更快。纯 IR 的 Recursive500IR 本轮增加约 31.4%，Dense500 原生编译增加约 11.8%；而 Recursive500、BranchRecursive500 的原生编译分别降低约 51.2%、27.7%。IR 和原生目标不是同一条成本路径，不能混为一谈。原始样本、CPU 时间、波动范围、命令和哈希见 默认结果（`CompileSpeed/DefaultResults.json`） 与 O3 结果（`CompileSpeed/O3Results.json`）。

**正确性与质量验证。** 新增 44 项优化回归（含恒等元返回结构检查），性能套件从 43 项增至 87 项。原生及 ASan/UBSan 编译器分别运行 O2/O3，四组各 256 个测试方法/语法用例全部通过：50 指针、19 整数、87 性能与编译器、25 语言回归、75 语法样例。方法内包含全部 128 种整数类型、256 种 bool 原始字节、改名/变参递归、宽整数、别名、副作用、间接调用、栈生存期、除零与不终止检查。

四个 C 质量测试和三个原生质量测试通过；寄存器分配另有 ASan/UBSan 白盒 IR 测试，验证非支配使用、多定义、循环、PHI 和寄存器不足等回退。GCC/Clang 对 65 个编译单元审计：零警告、零错误、零格式问题。Zig、CMake Release 与 ASan/UBSan 完整构建通过。Sanitizer 检查的是编译器和链接器；生成程序运行原生断言。`detect_leaks=0`，不宣称通过 LSan，但 C 测试检查了跟踪分配余额和 IR 指令索引。

该轮验证日志保存在本地 `Validation/*.log`，结果 JSON 与源码哈希清单也仅保留在本地。测量使用的修改后编译器 SHA-256：`a0ea6eb5a5cb768d4c99eb2264a2b2f3f49c5780162356db21d40edfd9877faf`；修改前：`0eb221e80059667d92eb3cc6de3e4894f6f394097beaba9eae2e63bcf8082d89`。这些哈希仅标识该轮构建。历史 O3 测量数值未改动，中间实验保留在本地忽略目录 `Probes/`。

发布说明：编译器源码、回归与复测脚本、研究说明和 Markdown 报告随 PR 提交；原始结果、生成产物、输入副本、编译器快照及日志仅在本地归档。普通基准可由脚本重新生成输入并构建；预算实验的 `Measure.py` 则同时依赖本地原始 `BudgetSelection/Results.json` 和 `BudgetSelection/Artifacts/` 中哈希匹配的 36 个固定 ELF，这些文件均不随 PR 提交。历史编译器本体也不随 PR 提交；首次复测需先构建当前编译器，并另备原始报告哈希对应的旧编译器，通过 `Run.py --baseline` 或 `CompileSpeed/Run.py --before` 指定路径。新构建的测量与验证结果应单独记录。

复测命令（使用本机已保留的历史编译器路径）：

```bash
python3 Test/Performance/LLVMOptimization/Run.py --prepare-only
python3 Test/Performance/LLVMOptimization/Run.py --measure-only --rounds 10 --cpu 0
python3 Test/Performance/LLVMOptimization/Inspect.py
python3 Test/Performance/LLVMOptimization/Validate.py
python3 Test/Pointers/BuildSanitized.py
python3 Test/Performance/LLVMOptimization/Validate.py --compiler /tmp/KrtC-pointer-sanitized --sanitized
python3 Test/Performance/LLVMOptimization/CompileSpeed/Run.py --before-flag=-O3 --after-flag=-O3 --output Test/Performance/LLVMOptimization/CompileSpeed/O3Results.json
python3 Test/Performance/LLVMOptimization/CompileSpeed/Run.py --output Test/Performance/LLVMOptimization/CompileSpeed/DefaultResults.json
```

计时期间应停止构建和其他基准进程；新测量会覆盖本轮对应 JSON，历史证据请先归档。
