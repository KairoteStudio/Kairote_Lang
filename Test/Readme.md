## KrtC.Cases
这个目录下的SyntaxAudit/ 为KrtC基准测试,确保语句编译,运行过程无问题
在提交前请确保SyntaxAudit/run.sh运行后无问题

指针专项与当前完整回归结果：[Pointers/ReadMe.md](Pointers/ReadMe.md)。
ASM/C 递归 Fibonacci 十次实测：[Performance/NativeFib/ReadMe.md](Performance/NativeFib/ReadMe.md)。

枚举、泛型、命名空间、引用传参回归：[SyntaxAudit/ReadMe.md](SyntaxAudit/ReadMe.md)。

- [性能、对齐和代码质量审计](Quality/ReadMe.md)：全项目 GCC/Clang 检查、ASan/UBSan 内存和链接器回归，以及十次性能对比。
- [KrtC 与 GCC 的 Fibonacci 代码生成对比](Performance/CodegenComparison/ReadMe.md)：等价 C、保留汇编、十次计时、指令路径分析及叶子返回/对齐诊断。
- [KRO 代码生成优化与十次复测](Performance/CodegenOptimization/ReadMe.md)：叶子返回、32 位指令、临时值转发、栈槽精简及完整回归。
- [通用尾递归优化与九组负载十次复测](Performance/GeneralOptimization/ReadMe.md)：结合运算累加器、全部整数位宽与副作用回归、复合赋值修复及当前 GCC 差距。
- [编译耗时退化调查与修复](Performance/CompileSpeed/ReadMe.md)：旧基线、修改前后和 O3 的十次编译耗时。
- [优化级别与 GCC O3 对测](Performance/O3Optimization/ReadMe.md)：`-O0`～`-O3`、等价源码和汇编、十组负载及两个级别的完整回归。
- [LLVM 与论文研究后的通用优化](Performance/LLVMOptimization/ReadMe.md)：纯调用复用、自适应递归内联、寄存器优化、十次对测与编译耗时。
