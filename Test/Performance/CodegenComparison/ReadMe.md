# 当前 KrtC / KRO 与 GCC 的 Fibonacci 代码生成对比

本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。 测量数值与构建哈希属于文中记录的历史版本，不代表后续规范整改后的新测量。

2026-09-13，AMD Ryzen 9 8945HX，Linux x86-64，固定逻辑 CPU 0。当前 KrtC 已重新构建，SHA-256 为 `d717afd1e47dac05ec4ab2cedb83b733cf5c7132ad68a804453c214bef151288`。GCC 为 16.2.1 20260810。

当前差距包含两部分：**即使保留完全相同的双递归调用树，Krt 也约慢 2 倍；GCC 普通 `-O2` 还将一支递归改成循环，总体约快 3.1 倍。** 本轮保存了汇编、KRO、ELF 和所有计时样本；诊断实验位于测试目录，尚未应用到编译器后端。

## 等价实现与计时口径

C 使用 [NativeFib/Fib.c](../NativeFib/Fib.c)，函数名通过 `-DFIB_NAME=Fib` 设置：

```c
__attribute__((noinline)) int32_t Fib(int32_t n) {
    if (n <= 1) return n;
    return Fib(n - 1) + Fib(n - 2);
}
```

Krt 使用现有 [test_fib35.krt](../Runtime/test_fib35.krt) 和 [test_fib40.krt](../Runtime/test_fib40.krt)，Fib 函数同样接收并返回 `int32`。两者均无缓存。C 函数独立编译、禁止内联和 LTO；`-fwrapv` 明确有符号整数环绕语义。在本次输入范围内，结果本来也不会溢出。

- GCC 双递归：`-O2 -fwrapv -fno-lto -fno-pie -fno-optimize-sibling-calls`，反汇编保留两个递归调用点。
- GCC 普通优化：`-O2 -fwrapv -fno-lto -fno-pie`，其余条件相同，允许 GCC 将一支递归循环化。
- C 的 [Driver.c](Driver.c) 分别以 `-DFIB_INPUT=35/40` 编译，调用独立目标文件中的 Fib。两边都通过 `syscall` 读取 `CLOCK_MONOTONIC`，仅计时一次 Fib 调用，不计编译、进程启动、辅助分配、结果校验和输出。

每种实现、每个输入均运行十次，每轮轮换先后顺序，串行执行，不剔除样本。每个样本验证 fib35=9227465、fib40=102334155；正式计时前，C 驱动检查 -100、0、1、2、10，包括装载原 KRO 函数字节的对照版，不运行完整 fib35/fib40 预热。Krt 的 fib35/40 函数字节完全相同，仅调用者输入不同。

## 十次平均结果

| 实现 | fib35 | fib40 |
|---|---:|---:|
| 当前 KrtC → KRO → ELF | **56.364 ms** | **627.274 ms** |
| GCC `-O2`，保留双递归 | **28.049 ms** | **320.904 ms** |
| GCC 普通 `-O2` | **18.195 ms** | **202.430 ms** |

Krt 与 GCC 双递归版的耗时比为 2.01 / 1.95；与 GCC 普通优化版为 3.10 / 3.10。上述数字是本轮同场对照，没有混用上一轮测试的时间。全部样本、标准差、最小/最大值、进程 CPU 时间与构建命令见 `Results.json`。例如 GCC 双递归 fib40 十次范围为 299.107–437.643 ms，均值保留其中较慢的样本。

## 实际执行的操作差异

以下路径计数根据本次反汇编控制流计算，**不是硬件性能计数器测量值**。排除填充 NOP；局部路径包含调用指令本身，不包含子调用的指令。栈访问包括 `push/pop`，不包括 `call/ret` 的隐式访问；`lea` 不算内存读取。

| 指标 | Krt | GCC 双递归 | GCC 普通 `-O2` |
|---|---:|---:|---:|
| Fib 函数字节数 | 192 | 59 | 77 |
| 静态指令数，排除填充 | 58 | 19 | 23 |
| 递归调用点 | 2 | 2 | 1，加回跳循环 |
| 叶子路径 `n <= 1` 指令数 | **27** | **5** | **9** |
| 叶子路径显式栈读写次数 | **8** | **0** | **2** |
| 叶子路径符号扩展次数 | **5** | **0** | **0** |
| 叶子路径额外占栈，不含入栈返回地址 | 120 B | 0 B | 24 B |
| 一次非叶子调用额外占栈，不含子调用 | 120 B | 24 B | 24 B |

### 1. Krt 的叶子返回也承担完整序言和尾声

当前 Krt 先执行 `push rbp; mov rbp,rsp; sub rsp,0x70`，保存并清零 RBX、R12、R13，再判断是否 `n <= 1`。退出时恢复这些寄存器。120 B 额外占栈由 112 B 分配空间和 8 B 保存的 RBP 组成；函数实际显式引用的局部栈槽只有三个 8 B 保存槽。

GCC 双递归版先比较参数，叶子直接返回，无局部栈帧。fib40 的双递归调用树共有 **331,160,281** 次函数进入，其中 **165,580,141** 次是叶子，因此这一差异会重复上亿次。当前 Krt 的参数和表达式临时值已放在 RBX/R12/R13；问题在于每次调用的保存/恢复、清零和取值成本。

代码来源：[KroCodegen.c](../../../Re.KrtC/src/compiler/Backend/Kro/KroCodegen.c) 的 `emit_function_prologue`、`emit_function_epilogue`、`KroGenerateFunction`；[KroRegisterPacking.inc](../../../Re.KrtC/src/compiler/Backend/Kro/KroRegisterPacking.inc) 的 `plan_function_storage`。存储规划为一些最终未产生访存的值预留槽位，形成较大的栈帧。当前调用点的栈对齐符合 16 字节要求。

### 2. `int32` 被反复扩展成 64 位并通过临时寄存器中转

Krt 计算第一个递归参数 `n - 1` 的指令是：

```asm
movsxd rax, ebx
movsxd rax, eax
mov    ecx, 1
movsxd rcx, ecx
sub    rax, rcx
movsxd rax, eax
mov    r12, rax
mov    rdi, r12
```

GCC 对应的参数计算只需 `lea edi,[rdi-1]`。Krt 的第二条扩展重复了第一条；常量 1 的扩展也可避免；后续算术和结果保存再次做扩展。一次非叶子局部路径中，Krt 有 **16 次** `movsxd`，GCC 双递归版为零。Krt 返回前还有 `mov r12,rax; mov rax,r12`，以及编码为 `e9 00 00 00 00` 的跳向下一条指令的 `jmp`。

代码来源：[KroInteger.inc](../../../Re.KrtC/src/compiler/Backend/Kro/KroInteger.inc) 的 `emit_extend_scalar`、`emit_integer_binary`、`emit_store_value`，以及 [KroRegisterPacking.inc](../../../Re.KrtC/src/compiler/Backend/Kro/KroRegisterPacking.inc) 的 `emit_load_packed_field`。这些层各自保证类型归一化，叠加后留下重复操作。

### 3. GCC 普通 `-O2` 将一支递归合入循环

本机 GCC 普通优化版的计算结构可写成：

```c
int32_t total = 0;
while (n > 1) {
    total += Fib(n - 1);
    n -= 2;
}
return total + n;
```

仍有真实递归、仍为指数复杂度，未改成线性动态规划。实际汇编用一个自调用点和向后的 `jg` 实现循环。按该控制流，fib40 的函数进入次数从 **331,160,281** 降到 **165,580,141**，约减半；同时寄存器分配和局部循环开销也发生变化。

| fib40 动态总量，按控制流求和 | Krt | GCC 双递归 | GCC 普通 `-O2` |
|---|---:|---:|---:|
| 函数进入次数 | 331,160,281 | 331,160,281 | 165,580,141 |
| 指令次数 | 13,246,411,227 | 3,642,763,085 | 3,302,375,349 |
| 显式栈读写次数 | 2,649,282,248 | 662,320,560 | 740,496,902 |

这里的指令数不等于微操作数或周期数，栈访问数也不等于 DRAM 流量。寄存器移动消除、分支预测、缓存及频率都会影响最终耗时。本轮没有硬件事件测量，因此不据此声称每类指令消耗了多少百分比的 CPU 时间。可复现计数见 [Analyze.py](Analyze.py)、`OperationCounts.json`，函数进入总数另以 Fibonacci 递推式交叉验证。

## 诊断实验：分离叶子路径与入口对齐

所有诊断版均用同一个 C 驱动计时，均保留两个自调用点，每种运行十次：

| 独立汇编对照 | fib35 | fib40 |
|---|---:|---:|
| 原 KRO 函数字节，保留入口地址模 64 的余数 | 56.468 ms | 614.923 ms |
| 字节完全相同，入口改为 64 B 对齐 | 53.014 ms | 595.105 ms |
| 原函数体前增加叶子直接返回 | 38.505 ms | 431.612 ms |

对齐版的 fib40 耗时下降 **3.22%**，fib35 下降 **6.12%**；本次原始函数入口为 `0x401333`，并未按 16 B 对齐。入口对齐有改善，但收益低于叶子路径修改，且可能随布局变化。

叶子快速返回版增加以下 9 字节前置代码，保留原函数体及其模 64 对齐，只调整自调用位移使递归也经过入口检查：

```asm
cmp    edi, 1
jg     original_body
movsxd rax, edi
ret
original_body:
; 原 KRO 函数体，仍包含原来的比较与两个递归调用
```

fib40 耗时下降 **29.81%**，fib35 下降 **31.81%**。此实验还保留了非叶子路径上的重复比较，因此它只是用于定位成本的局部实验。它支持优先处理叶子快速返回；后续再处理 32 位机器操作、重复扩展、临时值中转、无用栈槽和循环化。实验汇编均单独保存，当前 KrtC 实际产物对应主结果表第一行。

## 保留的产物与 KRO 核验

- GCC 可重新汇编的输出：`Artifacts/GccRecursive.s`、`Artifacts/GccO2.s`。两个输入复用相同的 Fib 目标代码，驱动分别使用 35 和 40。
- Krt 链接后 Fib 反汇编：fib35（`Artifacts/KrtFib35.Fib.asm`）、fib40（`Artifacts/KrtFib40.Fib.asm`）。完整程序反汇编：`Artifacts/KrtFib35.asm`、`Artifacts/KrtFib40.asm`。
- GCC 链接后 Fib 反汇编：双递归 fib35（`Artifacts/GccRecursive35.Fib.asm`）、双递归 fib40（`Artifacts/GccRecursive40.Fib.asm`）、普通 O2 fib35（`Artifacts/GccO235.Fib.asm`）、普通 O2 fib40（`Artifacts/GccO240.Fib.asm`）。
- KRO 原始函数反汇编：`Artifacts/KrtFib35.RawFib.asm`，重定位清单：`Artifacts/KrtFib35.Relocations.json`。
- 诊断汇编：`Artifacts/KrtBytes.S`、`Artifacts/KrtAligned.S`、`Artifacts/KrtFastLeaf.S`。原始字节按指令逐行保留并附有反汇编注释。

比较了 `.kro` 中 Fib 的 192 字节与 ArkLink 链接后的同一函数，**除五个 32 位跳转/调用重定位字段外，其余字节完全一致**。KRO 保存的已经是本机机器码；本次测量执行链接后的 ELF，函数体没有额外解释执行步骤，差异可定位到 KrtC 后端生成的指令。原始 KRO 中的重定位占位值还未解析，应以链接后反汇编判断跳转目的地。

构建生成的 `.json`、`.asm`、`.kro`、`.o`、`.elf` 与原始字节文件保存在本地，不随 PR 提交。产物 SHA-256 保存在本地 `OperationCounts.json`。

复现命令，在仓库根目录执行；基准期间应停止其他构建与回归任务：

```bash
python3 Test/Performance/CodegenComparison/Run.py --rounds 10 --cpu 0
python3 Test/Performance/CodegenComparison/Analyze.py
```

只重新编译、保留汇编并核验字节，可以使用 `--prepare-only`；它会将 `Results.json` 标记为未完成计时，随后需完整运行基准才能生成动态计数报告。
