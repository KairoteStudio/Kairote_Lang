# 性能、对齐与代码质量回归

开发规范整改后的检查范围、最新构建与回归结果见 [StandardsReadMe.md](StandardsReadMe.md)。以下保留历史性能测量记录。

本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。

2026-09-13，本机 Linux x86-64，AMD Ryzen 9 8945HX。本轮在已完成整数位宽、指针和语言修复的 Re.KrtC 上继续优化，保留修改前的编译器进行对比。

## 十次运行平均耗时

全部测试固定 CPU 0，前后版本交替、串行运行，各十次，不预热、不剔除样本。计时期间没有同时运行本任务的构建或回归进程。操作系统调度与处理器频率仍可能带来波动。

| 测试 | 修改前平均 | 修改后平均 | 耗时下降 |
|---|---:|---:|---:|
| 30,000 块内存分配并按分配顺序释放 | 2949.800 ms | 4.176 ms | 99.86% |
| Arena：三轮各 100,000 次 64 字节分配、写入和重置，最后销毁 | 226.216 ms | 4.285 ms | 98.11% |
| 200 个函数：启动编译器至完成链接 | 31.941 ms | 18.370 ms | 42.49% |
| 1000 个函数：启动编译器至完成链接 | 759.326 ms | 165.469 ms | 78.21% |
| 递归 fib35 | 58.028 ms | 56.712 ms | 2.27% |
| 递归 fib40 | 671.797 ms | 650.572 ms | 3.16% |

内存组件测试两版均使用 `cc -std=gnu11 -O2 -pthread`，工作负载来自同一份 [test_memory_benchmark.c](test_memory_benchmark.c)。前版使用保存的 `Allocator.c/.h` 与 `Arena.c/.h`；其他 Core 文件相同。两个 Re.KrtC 编译器均为 Zig Debug 构建；编译耗时包含进程启动、前端、代码生成与链接，不包含随后验证产物退出码的耗时。

fib 的计时来自程序内部 `CLOCK_MONOTONIC`，每次验证结果分别为 9227465 和 102334155，检查反汇编中仍有真实递归调用。Fib 函数反汇编指令计数从 74 降到 58；本轮递归耗时改善较小，不能把内存组件的加速比例套用到程序运行性能上。fib40 修改前十次范围为 644.213–735.703 ms，修改后为 616.536–688.447 ms，两组存在重叠。

原始数据及完整 SHA-256：`PerformanceResults.json`、`FibResults.json`。修改前编译器摘要为 `3ba7810e9982`，修改后为 `1c0a2870eab1`。本地基线保存在 `Re.KrtC/build/QualityBaseline/`，此构建目录不纳入 Git。此前 ASM/C 基准另见 [NativeFib/ReadMe.md](../Performance/NativeFib/ReadMe.md)。

## 实际修改

| 范围 | 修改与目的 |
|---|---|
| 编译器内存分配 | 用指针哈希索引替代释放时的链表查找，缓存使用量；静态管理器替代 `sbrk` 启动分配，修复清理时错误释放和再次初始化问题；检查大小溢出。 |
| 对齐与池复用 | 分配器保护头、Arena、IR arena 和池节点保持 `max_align_t` 对齐；重置后复用已有块，游标避免反复扫描；末次 Arena 分配可原地扩容；线程私有 Arena 省去互斥锁。 |
| IR 生命周期 | 完整释放指令、操作数、参数名、基本块及布局元数据；死代码删除同时维护链表和指令索引；消除不改变求值语义的整数恒等转换。 |
| 本机代码 | 单次使用的标量整数比较直接生成条件跳转；省去跳向紧邻块的跳转；使用较短的栈偏移与栈帧编码。保留宽整数、浮点和多次使用布尔值的语义。 |
| 生成程序的堆 | 分配头改为 16 字节，保持 `int128` 等数据对齐；数组长度读取和释放路径同步使用新布局。 |
| KRO 写入 | 批量补齐代码与数据，检查大小和 BSS 对齐溢出，正确保存 BSS 对齐；跳转修补的游标边界按已写入范围校验，允许回退修补后恢复。 |
| ArkLink | 缓冲区支持扩容时的自身切片追加；修复 PE 导入表指针在扩容后失效，保证查找表/IAT 对齐和 IAT 连续；避免未对齐标量写入；修复 ELF 边界、变量遮蔽、未初始化清理与重复释放路径。 |
| 复用与质量 | 合并 Parser 中重复的 arena 字符串复制、安装目录探测、大小检查与格式注解；移除确认未使用的静态函数/变量及空循环；修复格式字符串使用；统一缩进、尾随空白和文件末尾换行。 |
| 构建 | 修复根 CMake 的旧目录引用，Re.KrtC 与 Zig 共用 C 源文件清单；恢复 ArkLink CLI 构建；Zig/CMake 对未使用函数、变量、参数及隐式声明启用错误检查。 |

堆头部布局从 8 字节变为 16 字节。跨模块共享这些堆分配时，旧 `.kro` **必须重新编译**，不能混用两版分配、遍历或释放代码；详细约定见 [Memory_Model.md](../../Docs/KrtC/Doc/Spec/Memory_Model.md)。

## 全量扫描与回归结果

扫描覆盖 `Re.KrtC/src`、`Shared`、`stub_include` 和 `ArkLink/src`、`include` 的全部自有 C 源码、头文件和 `.inc`，不扫描生成的构建缓存。共 65 个 C 翻译单元，两套编译器各编译一次，共 130 次检查：GCC 16.2.1 与 Clang 22.1.8，**0 警告、0 错误**。145 份源码的制表符缩进、尾随空白和末尾换行检查均通过。结果和逐文件 SHA-256 见 `AuditResults.json`。

扫描启用 `-Wall -Wextra -Wformat=2 -Wshadow -Wundef` 及对齐告警。另列出 212 个在扫描到的 C/INC 实现中仅出现定义的外部 API 候选；清单会计入测试代码与头文件引用。这些候选可能用于公开接口、可选后端或动态调用，不能仅凭词法计数认定为死代码。可确定的未使用静态实现已清理，构建会拦截对应告警。

| 回归 | 结果 |
|---|---|
| 语法运行用例 | 75/75；Zig Debug、CMake Release、ASan/UBSan 编译器均通过 |
| 指针专项 | 50/50；457 次编译，355 次成功、102 次按预期拒绝；349 次正常运行，4 次预期 SIGILL、2 次预期 SIGSEGV；普通与插桩编译器均通过 |
| 枚举、泛型、命名空间、引用传参 | 25/25；65 次编译，36 次成功、29 次按预期拒绝；普通与插桩编译器均通过 |
| 整数位宽与寄存器打包 | 12/12 |
| 编译热点、缓存隔离及并发、本机代码 | 10/10 |
| 新增本机质量用例 | 3/3；整数比较边界、宽整数/浮点 NaN 与零、堆对齐与数组遍历；普通与插桩编译器均通过 |
| 新增 C 组件回归 | 4/4，ASan/UBSan 下全部通过 |

新增 C 组件回归包括：

- [test_memory.c](test_memory.c)：奇数大小分配与 128 位访问、溢出拒绝、清理后复用、多块 Arena/IR arena 重置、各类池节点归还复用，以及 8 线程共享分配。
- [test_ir.c](test_ir.c)：20 轮各 50 个函数的生成和销毁、死代码删除幂等性、指令链表/索引一致性，以及跟踪分配计数回到基线。
- [test_linker.c](test_linker.c)：缓冲区自身追加与溢出、1200 个 PE 导入跨多次扩容后的表项/名字/IAT 校验、KRO 对齐与跳转修补游标恢复。
- [test_elf_failure.c](test_elf_failure.c)：逐个注入静态/动态 ELF 路径中 8/27 个 `calloc`、`realloc` 失败点，检查错误返回及清理过程。

组件结果见 `Results.json`，其他回归的构建身份与摘要见 `RegressionResults.json`。ASan/UBSan 编译器同时插桩 Re.KrtC 与 ArkLink，检查的是编译、链接过程；生成的机器码通过实际运行与结果校验测试。

当前环境的 LSan 曾因 ptrace 限制无法运行，本轮使用 `detect_leaks=0`；内存和 IR 组件另断言跟踪分配数归零。本轮结果不能证明整个编译器不存在泄漏。PE 测试验证序列化布局和缓冲区安全，未在 Windows 上执行生成的 PE 程序。

## 复现

在仓库根目录运行。先构建 ArkLink，再构建 Re.KrtC；本轮使用 Zig 0.16.0 默认 Debug。

```bash
cmake -S ArkLink -B ArkLink/build -DCMAKE_BUILD_TYPE=Release
cmake --build ArkLink/build -j 4
cd Re.KrtC
zig build --global-cache-dir /tmp/kairote-zig-cache --cache-dir build/QualityCache
cd ..
python3 Test/Quality/Audit.py --jobs 4
python3 Test/Quality/RunTests.py
python3 Test/Pointers/RunTests.py
python3 Test/Pointers/RunTests.py --suite Test/SyntaxAudit --output Test/SyntaxAudit/LanguageResults.json
python3 -m unittest discover -s Test/IntegerWidths -p 'test_*.py'
python3 -m unittest discover -s Test/Performance -p test_compiler_hotspots.py
bash Test/SyntaxAudit/run.sh
```

插桩编译器回归：

```bash
python3 Test/Pointers/BuildSanitized.py
export KRTC=/tmp/KrtC-pointer-sanitized
export ASAN_OPTIONS=detect_leaks=0:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
python3 Test/Pointers/RunTests.py --output Test/Pointers/SanitizerResults.json
python3 Test/Pointers/RunTests.py --suite Test/SyntaxAudit --output Test/SyntaxAudit/SanitizerLanguageResults.json
python3 -m unittest discover -s Test/Quality -p test_native.py
bash Test/SyntaxAudit/run.sh
unset KRTC ASAN_OPTIONS UBSAN_OPTIONS
```

性能测试应等待其他构建与回归完成后串行执行。需要保留本轮基线目录；脚本将校验每次产物的结果并保存十次原始数据。

```bash
python3 Test/Quality/Benchmark.py --baseline Re.KrtC/build/QualityBaseline --rounds 10 --cpu 0
python3 Test/Performance/FibComparison.py --baseline before Re.KrtC/build/QualityBaseline/KrtC --rounds 10 --cpu 0 --output Test/Quality/FibResults.json
```

根 CMake 构建与独立运行验证：

```bash
cmake -S . -B /tmp/kairote-cmake-quality -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/kairote-cmake-quality -j 4
KRTC=/tmp/kairote-cmake-quality/Re.KrtC/KrtC bash Test/SyntaxAudit/run.sh
```
