本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。 测量数值与构建哈希属于文中记录的历史版本，不代表后续规范整改后的新测量。

截至 2026-09-13，本轮最值得实现的是**受预算限制的递归内联、纯调用公共子表达式消除，以及内联后的控制流和寄存器整理**。直接更换成 LLVM O3 并不能保证追上 GCC 的递归速度：本机 Clang 在 Fib 上拒绝递归内联，而在重复调用的 Tree 上又比 GCC 做了更强的冗余消除。这两条优化路线需要分开验证。

本文件记录研究结论和实施建议，不是新性能报告。起点为 [上一轮十次对测](../O3Optimization/ReadMe.md)：KrtC O3 的 Fib35/Fib40 为 18.825690/208.285707 ms，GCC O3 为 7.757810/83.696973 ms；Paths 和 Tree 的耗时比分别为 3.851×、5.495×。本研究仅生成汇编、LLVM IR 和优化日志，未运行性能计时，也未覆盖上一轮产物。这里的“起点实现”指上一轮编译器，后续实现及成绩由本轮实际验证记录确认。

**已取得的本机证据。** [RunAnalysis.py](Analysis/RunAnalysis.py) 对三份原始等价 C 源码生成 21 组产物，使用 GCC 16.2.1 20260810、Clang 22.1.8，统一 `-O3 -std=gnu11 -fwrapv -fno-lto -fno-pie`。保留源码、命令、工具版本及 SHA-256 的 `Analysis/Manifest.json`，并保留 GCC IPA/tailr1/最终 GIMPLE、Clang 优化备注与 LLVM IR。表中的数值是核心函数的静态机器码字节数，不能代替动态指令次数或耗时。

研究脚本随 PR 提交，21 组生成产物仅在本地归档。重新运行前须准备 `../O3Optimization/Artifacts/` 中的三份等价 C 输入；可按 [O3 复测说明](../O3Optimization/ReadMe.md) 生成，或恢复原始输入归档。新的工具版本或源码生成的日志与哈希应另行记录。

| 编译方式 | Recur / 字节 | Paths / 字节 | Tree / 字节 |
|---|---:|---:|---:|
| 上一轮 KrtC O3 | 82 | 85 | 85 |
| GCC O3，递归内联深度 0 | 77 | 54 | 54 |
| GCC O3，递归内联深度 2 | 255 | 155 | 166 |
| GCC O3，递归内联深度 4 | 436 | 389 | 253 |
| GCC O3，默认递归内联深度 8 | 924 | 831 | 517 |
| GCC O3，关闭所有内联 | 77 | 54 | 54 |
| GCC O3，关闭尾调用优化 | 444 | 710 | 239 |
| Clang O3，默认 | 68 | 75 | 28 |

“深度 0”仅限制 GCC 的专门递归内联阶段，仍允许把函数内联到 Kernel 中，因此不等于 `-fno-inline`。详情和实际展开深度在 `Analysis/Summary.json`。这些开关是研究用的消融对照，正式 GCC 对照仍应使用自然 O3。

GCC 的 tailr1 日志（`Analysis/Fib40/GccDefault/Kernel.c.051t.tailr1`） 先把第二路递归变成 PHI 累加器和循环；随后 IPA 日志（`Analysis/Fib40/GccDefault/Kernel.c.095i.inline`） 明确记录深度 1～8，每层沿剩下的递归调用展开一次，因深度上限停止。当前机器参数为深度 8、递归体预算 450 个 GCC 内部指令、模块增长参数 40%，见 `Analysis/GccParameters.txt`。不能把 450 当成 x86 指令数，也不能假设 GCC 的预算适合 Krt IR。[GCC 官方内联实现](https://raw.githubusercontent.com/gcc-mirror/gcc/master/gcc/ipa-inline.cc) 体现了递归边挑选、深度和增长限制；此链接是滚动主分支，具体本机行为以保存的日志为准。[官方优化选项](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) 还明确区分内联、尾调用、循环变换和其他 O3 优化。

由此可解释目前差距的主要机制：Krt 每次走剩余递归边仍会产生真实 `call/ret`，较深路径要建栈、保存参数和累加器；GCC 在一次真实调用内部执行了多层工作，浅层分支还会被常量/范围传播和循环整理吸收。虽然 GCC 函数更大，动态调用开销会下降。这个解释由日志与汇编共同支持；各类指令到底贡献多少毫秒尚未用硬件计数器拆分，不能据此声称全部差距都来自调用指令。上一轮 Paths/Tree 的常量返回因跨位宽 CAST 未进入栈前快速返回，也会增加叶子路径开销，属于额外可修复的问题。

**LLVM 的结果与 GCC 不同。** 本机 Clang Fib 备注（`Analysis/Fib40/ClangDefault/Diagnostics.txt`） 明确以 `recursive` 为由拒绝内联，并执行尾递归转循环；最终 IR（`Analysis/Fib40/ClangDefault/Kernel.ll`） 只有一个循环和一个真实递归调用。[LLVM 22.1.8 InlineCost.cpp](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/lib/Analysis/InlineCost.cpp) 对递归及递归调用方的栈增长设有约束。这意味着接入 LLVM 后仍须对本机 Fib 单独验证，不能推断 LLVM O3 自动实现了 GCC 的多层展开。

Tree 则相反：Clang Tree IR（`Analysis/BinaryTree/ClangDefault/Kernel.ll`） 把两个相同实参的 `Tree(n-1)` 合并为一次调用，随后 `shl i64 ..., 1`；函数属性为 `memory(none)`。原本指数数量的相同子调用因此变成线性调用链。GCC 保存的 Tree 汇编（`Analysis/BinaryTree/GccDefault/Kernel.asm`） 仍是展开后的嵌套尾循环，没有这次合并。LLVM 的 [EarlyCSE 22.1.8 源码](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/lib/Transforms/Scalar/EarlyCSE.cpp) 提供了通用依据：对无内存访问的函数调用进行值编号，按调用目标和操作数判断等价。这里复用已经求得的结果，不需要表格、运行时缓存、递推式识别或函数名称判断；是否在 Krt 中实现，以本轮实际代码为准。

**建议的通用实现次序与预算。** 以下是针对当前 IR/后端能力的工程建议，不是论文已证明的最佳参数。

| 顺序 | 通用变换 | 起步约束与停止条件 | 主要验证 |
|---|---|---|---|
| 1 | 修复常量 CAST 后的栈前返回；纯整数局部值编号 | 保留源位宽转换；同块查找，哈希表或明确小预算，避免每条指令扫描全模块 | 所有 128 种整数类型、负常量、边界转换 |
| 2 | 推断无可观察副作用的函数，消除等参重复调用 | 初版只接收纯整数参数/返回、可验证 IR 和封闭调用图；排除未知调用、指针/全局读取、I/O、原子、地址生成；递归 SCC 以不动点传播“有副作用” | 变参、重定义、间接调用、全局修改、引用参数、递归循环、异常信号 |
| 3 | 尾递归规范化后有限递归内联 | 在保留原程序的预算下试 1/2/4/8 层，按实际体积和活跃值数决定；每层完成 SSA/参数重命名与返回汇合；接近后端槽上限即停止 | 改名、不同递归步长、多个参数、两个以上出口、混合位宽、地址存活 |
| 4 | 内联后再次常量/复制传播、CSE、分支简化与 DCE | 少量有上限的轮次；维护 CFG、指令索引与 use/def；纯操作的危险异常不能被错误移动 | 禁止删除第一次可能不返回的调用或除零操作；不同分支不投机执行 |
| 5 | 跨基本块活跃区间、寄存器复用与按需溢出 | 显式处理调用破坏寄存器、固定寄存器和并行参数复制；共享物理寄存器的窄整数有位段冲突约束 | 分支汇合、循环携带值、参数置换环、int2+int30 共存、128 位值 |
| 6 | 循环展开、多个累加器、展开后融合 | 先形成规范循环和明确退出条件，再按依赖关系、活跃值压力及体积选择倍数；unsafe 内存需别名/依赖证明 | 0/1/倍数边界、提前退出、回绕、别名、重叠内存、非结合浮点 |

目前尾递归变换在 Krt 后端，直接在它之前复制原始二叉递归树会指数增加代码，还可能拆散后端依赖的 `call → combine → return` 形式。应优先只展开一条有预算的边，或把尾递归规范化移至能表达循环/PHI 的中间层后再内联。GCC 的顺序是可参考的实例，照抄其深度 8 而不照顾表示形式，可能同时降低编译和运行速度。

纯调用 CSE 不需要证明函数在所有输入上都终止：若第一条完全相同、确定性的调用已经返回，其后的同参调用可复用该结果。但是“没有内存副作用”不意味着可以删除第一次调用、将调用提到条件分支之前或越过会终止程序的指令；这些变换会改变不终止行为或信号。初版只消除已有支配结果的重复执行更容易建立正确性依据。

**论文和实现资料给出的具体启发。** 以下仅列已读到的一手来源，区分论文、技术报告和开发者会议资料；其中旧平台结果不用于预测本机速度。

| 来源 | 本研究采用的内容 | 对 Krt 的用途 |
|---|---|---|
| Chang、Hwu，1989，*Inline Function Expansion for Compiling C Programs*，[作者所属机构记录](https://experts.illinois.edu/en/publications/inline-function-expansion-for-compiling-c-programs-2) | 用调用信息选择展开，并同时约束代码与栈增长；已读取摘要和书目信息，未宣称完成全文复核 | 为内联提供明确成本边界，避免无限扩大函数 |
| Barrio、Carruth、Molloy，2015，*Recursion Inlining in LLVM*，[LLVM 开发者会议幻灯片](https://www.llvm.org/devmtg/2015-04/slides/recursion-inlining-2015.pdf) | 有限迭代内联暴露更多优化机会，代码量与寄存器压力需要控制；并比较了显式栈与展开 | 支持先做有限内联；这是会议资料，不当作当前 LLVM 已具备此能力的证明 |
| Wimmer、Mössenböck，2005，*Optimized Interval Splitting in a Linear Scan Register Allocator*，[会议论文全文](https://www.usenix.org/legacy/publications/library/proceedings/vee05/full_papers/p132-wimmer.pdf) | 利用活跃区间空洞、使用位置和分段溢出，同时处理固定寄存器 | 内联增加活跃值后，减少全生命周期占用寄存器或整个临时值落栈 |
| Carr、Kennedy，1994，*Improving the Ratio of Memory Operations to Floating-Point Operations in Loops*，[作者所属机构记录](https://digitalcommons.mtu.edu/michigantech-p/12517/) | 按内存操作与计算比例选择循环重构；已读取摘要和书目信息 | 提醒循环变换按实际资源瓶颈选择，不能给所有循环统一倍数 |
| Lopes 等，2021，*Alive2: Bounded Translation Validation for LLVM*，[作者论文全文](https://users.cs.utah.edu/~regehr/alive2-pldi21.pdf) | 对每次优化的输入输出做位向量/内存语义的有界翻译验证 | 为窄整数规则建立小规模穷举或求解验证；不能仅以大基准结果正确为依据 |
| Evlogimenos，2004，*Improvements to Linear Scan register allocation*，[LLVM 技术报告](https://llvm.org/ProjectsWithLLVM/2004-Fall-CS426-LS.pdf) | 活跃区间空洞、内存操作数与减少永久保留的溢出暂存寄存器 | 后端资源规划的补充；属于技术报告，区别于正式会议论文 |

Alive2 的工具维护者还明确说明目前不支持跨过程变换；因此不能把它直接套在自递归内联上并声称已获得证明。[Alive2 官方仓库](https://github.com/AliveToolkit/alive2) 对这项限制有说明。对 Krt 当前自定义 IR，可先对局部整数变换使用精确位向量模型；递归内联另做结构验证和包含副作用顺序的差分测试。

循环 `unroll-and-jam` 的含义是展开外层、再融合复制出来的内层循环，用于共享加载和减少重复控制开销。[LLVM 官方 pass 文档](https://www.llvm.org/docs/Passes.html#loop-unroll-and-jam-unroll-and-jam-loops) 明确依赖内存依赖分析。它与本机 GCC 的递归内联不是同一件事，现有日志不能证明 Fib 的收益来自 jam。LLVM 22.1.8 的 [LoopUnrollPass.cpp](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/lib/Transforms/Scalar/LoopUnrollPass.cpp) 使用成本阈值、运行时余数路径和目标相关偏好；这些机制比无条件复制循环更重要。

**不能放松的语言边界。** Krt 的同位宽整数加法、乘法和按位运算可以在模 `2^w` 意义下验证结合律；跨位宽 CAST、比较的有符号解释、除余、移位、浮点和指针运算必须分别处理。LLVM 的任意位宽整数表示与普通 `add` 可供建模，但不能无证明地加 `nsw`/`nuw`，否则会把本来合法的 Krt 回绕变成 LLVM poison。[LLVM LangRef](https://llvm.org/docs/LangRef.html#add-instruction) 定义了这些区别。保存的 Clang IR 某些递减带 `nsw` 是范围分析后的结果，不是 `-fwrapv` 失效，也不是 Krt 可以给全部运算附加该标记的依据。

取地址、`stackalloc`、多级/可空指针、传引用和 FFI 都涉及实际存储与生存期。内联可以消除调用边界，却仍须保留同时存活的不同局部对象；尾调用复用整个栈帧则需要更强的无逃逸条件。LLVM 的 [TailRecursionElimination 22.1.8 实现](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/llvm/lib/Transforms/Scalar/TailRecursionElimination.cpp) 同时处理累加器、指令可移动性和调用方栈访问。Krt 初版继续排除地址操作，是保守合法的范围，并非要靠弱化 unsafe 语义提速。

后续对测应保留全套负载、全部十个样本和数值验证，使用自然 GCC O3，另列 Clang O3 作诊断；编译耗时也要继续记录。递归内联参数只依据统一成本模型和多负载结果选择，不能按函数名字、35/40 等输入或基准路径开关。本文提出的尚未完成能力、未计时的消融版本和历史论文中的速度数字，都不计入“已追平 O3”。
