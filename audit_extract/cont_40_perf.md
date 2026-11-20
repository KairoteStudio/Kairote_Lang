# 第九十七批：Test/Performance 全部源文件

## 覆盖范围

- 枚举方式：`glob Test/Performance/**/*` 后筛选 `.c/.h/.inc/.krt`，得 **39 个文件**（实际无 `.h`、无 `.inc`），已逐个 `read` 全量；合计约 1700 行。
- 分布：
  - `CodegenComparison/Driver.c`
  - `GeneralOptimization/Driver.c`、`LLVMOptimization/Driver.c`、`O3Optimization/Driver.c`
  - `NativeFib/Driver.c`、`NativeFib/Fib.c`
  - `Runtime/` 5 个 `.krt`（test_empty_loop、test_fib35、test_fib40、test_file_io、test_output）
  - `LLVMOptimization/BudgetSelection/Sources/` 6 个 `.krt`（BinaryTree、Branching、Fib35、Fib40、NarrowArguments、Parameters）
  - `LLVMOptimization/Artifacts/` 10 个 `.c` + 10 个 `.krt`
  - `test_compiler_hotspots.c`、`test_register_allocation.c`
- 方法限制：本会话 bash 被沙箱拒绝（无 bubblewrap/Landlock），未能编译或运行任何用例；仅用 glob/read/grep。对 `Test/Performance` 之外的源文件未整文件通读，只对 Python harness 做符号级 grep（`subprocess`/`returncode`/`platform`/`Artifacts`/`split("int32 Fib("` 等）核对契约；「harness 是否补偿」写在备注。
- 共用样板：21 个 `.krt` 共享同一段 `BenchmarkNow`/`BenchmarkFinish`（`syscall(228,1,...)` + `syscall(1,2,...,16)`），同类问题用 97-32、97-33 两条汇总并在备注列全文件。

## 问题条目

### [致命] 97-1｜空循环体可被优化器整段消除，断言无法发现，基准测量为零仍通过
- 位置：`Test/Performance/Runtime/test_empty_loop.krt:22-24`
- 类别：性能测量方法缺陷 / 优化器消除被测代码 / 断言恒真
- 触发条件：以 `-O1` 及以上编译；循环 `for (; i < 100000; i++) { }` 体为空，只有 `i` 是活的。
- 后果：编译器可把循环折叠为 `i = 100000`，计时区间退化为几条指令；`if (i != 100000)` 仍成立，`BenchmarkFinish` 正常写报告，harness 的 `0 < duration_ns` 也通过，于是「空循环基准」长期输出无意义数值而无人察觉。
- 最小修复：让循环体产生可观测副作用（如累加到 `report[1]` 或 `volatile` 变量），或加编译器屏障；并在 harness 侧增加最小时长阈值（例如 `duration_ns >= 1000`）。
- 备注：`RuntimeBenchmarks.py:123` 只校验 `0 < duration_ns <= process_ns`，无法识别「被消除」与「真实极快」。

### [高] 97-2｜test_file_io 遗留 409.6MB `payload.bin`，文件内无任何 unlink
- 位置：`Test/Performance/Runtime/test_file_io.krt:28`（写）、`:31-34`（100000×4096 写）、`:46-47`（结束）
- 类别：资源泄漏与残留产物
- 触发条件：直接运行该 `.krt` 编译产物，或 harness 被中断/杀进程。
- 后果：每次运行在 CWD 留下 409,600,000 字节文件；清理完全依赖外部 `RuntimeBenchmarks.py:100` 的 `TemporaryDirectory`，文件本身无 `unlink`（x86-64 应为 `syscall(87, path, 0,0,0,0,0)`）。磁盘可被反复跑满。
- 最小修复：在读完/关闭后 `unlink("payload.bin")`，或改用 `O_TMPFILE`/`tmpfile`；至少提供可配置路径。
- 备注：正常 harness 路径下 `TemporaryDirectory` 会清理，但 `dir=HERE` 把临时目录建在 `Test/Performance/` 内，仍属写源树。

### [高] 97-3｜test_file_io 固定相对路径 + 依赖 CWD 可写，多实例并发互踩同一文件
- 位置：`Test/Performance/Runtime/test_file_io.krt:28`
- 类别：依赖 CWD/绝对路径/环境 / 非确定性竞态
- 触发条件：从任意非临时目录运行，或两个实例同时在同一 CWD 运行。
- 后果：`open("payload.bin", O_RDWR|O_CREAT|O_TRUNC)` 会污染任意可写目录；两实例互相 `O_TRUNC` 对方正在读的文件，导致短读/内容错乱，返回 2/6/7 或更糟的静默数据混合。
- 最小修复：路径改为参数/环境变量，或每次生成唯一文件名（含 pid/随机数），并在结束时删除。
- 备注：`RuntimeBenchmarks.py:117` 用 `cwd=directory` 隔离了 harness 场景，但 `.krt` 自身不可独立安全运行。

### [中] 97-4｜test_file_io 读回只校验 4096 字节中的 2 字节
- 位置：`Test/Performance/Runtime/test_file_io.krt:41`
- 类别：覆盖缺口 / 断言过弱
- 触发条件：写/读过程中出现中间块损坏、错位或长度不足。
- 后果：`buffer[0]==0 && buffer[4095]==79` 仍可能成立（该模式每 251 字节重复），损坏的 I/O 被静默接受；`return 7` 的校验强度远低于 409.6MB 的传输量。
- 最小修复：逐块校验完整 4096 字节（或至少校验和/抽样多个偏移），并校验总字节数。
- 备注：`RuntimeBenchmarks.py:61-68` 只额外校验文件前两个块，整体仍非全量校验。

### [中] 97-5｜test_file_io 计时把写、fsync、定位、读混在一个区间，无法归因
- 位置：`Test/Performance/Runtime/test_file_io.krt:27-46`
- 类别：性能测量方法缺陷（计时口径）
- 触发条件：任何存储介质/文件系统差异（SSD/HDD/tmpfs、页缓存状态）。
- 后果：`duration_ns` 同时包含 409.6MB 写 + `fsync` 刷盘 + `lseek`/`fadvise` + 409.6MB 读，跨机器不可比，也无法判断优化是否生效；`fsync` 使结果强依赖介质。
- 最小修复：拆成写、同步、读三个独立计时段分别报告；或明确声明这是「混合 I/O 吞吐」而非某一项性能。
- 备注：`RuntimeBenchmarks.py` 只记录一个 `duration_ns`。

### [高] 97-6｜test_compiler_hotspots 向给定目录的父目录写 `Main.krt`/`Main.ir`，临时目录清理不到
- 位置：`Test/Performance/test_compiler_hotspots.c:157-158`（`../Main.krt`、`../Main.ir`）、`:204`（codegen 写 `Main.ir`）
- 类别：资源残留 / 依赖路径布局
- 触发条件：`cache` 模式；`test_compiler_hotspots.py:44` 传入的是 `/tmp/kairote-hotspot-case-*/`，故 `../Main.krt` 落在 `/tmp/Main.krt`。
- 后果：`TemporaryDirectory` 只清理自身，`/tmp/Main.krt`、`/tmp/Main.ir` 永久残留；多个测试进程并发时互踩同名文件；若父目录不可写则 `write_source` 失败。
- 最小修复：全部落在 `directory` 内（`Main.krt`、`Main.ir` 与 `Library.krt` 同目录），不要拼接 `..`。
- 备注：`Library.krt` 在临时目录内会被清理；`input%d.krt`/`output%d.ir` 也在临时目录内。

### [高] 97-7｜test_compiler_hotspots 断言全局缓存 `misses == 2` 精确值，且由 4 线程并发触发
- 位置：`Test/Performance/test_compiler_hotspots.c:222-224`（另见 `:229-230` 的 `misses == 3`）
- 类别：非确定性/竞态 / 断言过强
- 触发条件：4 个 `cache_worker` 线程并发调用 `KrtCompilePipelineLoadStandardLibrary`；缓存实现若对 hit 路径有写操作（LRU/时间戳）或并发去重策略不同。
- 后果：额外一次 miss 即 `CHECK` 失败，测试 flaky；同时并发读全局缓存若不加锁就是数据竞争，可能崩溃或计数漂移。
- 最小修复：只断言下界（如 `hits >= 49 && misses <= 4`），或串行预热后再并发；把精确计数改为统计不变量。
- 备注：主线程在 `:209` 已预热，故现状通常稳定；但断言把「缓存实现细节」当成契约。

### [中] 97-8｜bench_cache 只打印 cold/warm，不断言 warm 明显小于 cold
- 位置：`Test/Performance/test_compiler_hotspots.c:174-184`
- 类别：断言缺失 / 性能测量方法缺陷
- 触发条件：标准库缓存失效（每次 warm 都退化为 cold）或缓存被 `Reset` 清空。
- 后果：`stdlib_warm_seconds` 与 cold 相同甚至更大也照常输出，缓存回归完全静默；`:184` 无任何阈值。
- 最小修复：增加 `CHECK(warm / 8 < cold)`（或比值阈值），否则至少把 `cold == warm` 视为可疑并报告。
- 备注：`test_compiler_hotspots.py` 只检查 harness 返回码，不解析该 JSON。

### [中] 97-9｜bench_symbols 的 checksum 只打印不校验
- 位置：`Test/Performance/test_compiler_hotspots.c:101-112`（`:111` 打印 `checksum`）
- 类别：只打印不校验
- 触发条件：`symbol_table_lookup_scope_chain` 返回了错误符号（非 NULL 但行号错误）。
- 后果：`CHECK(entry)` 只保证非空，`sum` 算错也照样输出，符号表回归静默通过。
- 最小修复：与基准 checksum 比较（如固定期望值或跨实现比对），或至少校验 `sum` 非零/单调。
- 备注：非基准分支 `:96-100` 的断言是有效的，仅基准分支缺校验。

### [中] 97-10｜bench_preprocessor 只检查一个子串 `"+ 123 +"`
- 位置：`Test/Performance/test_compiler_hotspots.c:73`
- 类别：断言过弱 / 覆盖缺口
- 触发条件：预处理器只展开首个宏、漏展开后续宏或改变空白处理。
- 后果：`strstr(result, "+ 123 +")` 命中即通过，18000 行输入的其余部分是否正确无校验；`printf` 的 `input_bytes`/`macros` 也不校验。
- 最小修复：对完整输出做长度/内容校验（如校验宏展开计数或哈希），至少校验 `strstr` 出现次数。
- 备注：非基准分支 `:50-60` 有精确字符串比对，基准分支反而弱化。

### [中] 97-11｜parallel_test 用 `strstr` 子串校验输出文件，且只读前 4095 字节
- 位置：`Test/Performance/test_compiler_hotspots.c:297-307`（`:303-306`）
- 类别：断言过弱 / 覆盖缺口
- 触发条件：IR 输出含多余内容、被截断，或正确片段出现在前 4095 字节而后续损坏。
- 后果：`strstr(data, "return N\n")` 命中即通过；文件大于 4096 字节时后半部分从不检查，代码生成回归可能静默通过。
- 最小修复：读取完整文件并做精确匹配（或至少校验文件长度与关键指令数），避免固定 4096 缓冲区截断。
- 备注：`:292` 的 `AddFile` 返回码有检查，问题仅在输出内容校验。

### [中] 97-12｜parallel_benchmark 计时区间包含编译器创建/销毁与 64 次 AddFile，且单次无基线
- 位置：`Test/Performance/test_compiler_hotspots.c:322-331`（`:323` Create、`:325-327` AddFile、`:330` Destroy）
- 类别：性能测量方法缺陷（计时口径、单次测量、缺基线/阈值）
- 触发条件：任何运行；线程池创建/销毁开销与 64 个任务的编译开销同区间。
- 后果：`batch_seconds` 不是纯编译时间，跨版本比较会被线程池初始化主导；只测一次，无重复、无基线、无阈值，性能回归无告警。
- 最小修复：把 `ParallelCompilerCreate`/`AddFile`/`Destroy` 移出计时区间，只计 `ParallelCompilerExecute`；循环多次取中位数并与基线比较。
- 备注：`test_compiler_hotspots.py` 只检查返回码，不解析 `batch_seconds`。

### [高] 97-13｜test_register_allocation 全部校验用 `assert`，`-DNDEBUG` 下整测静默通过
- 位置：`Test/Performance/test_register_allocation.c:28`、`:60`、`:101`、`:114-182`（共 15+ 处）
- 类别：断言缺失 / 静默通过
- 触发条件：构建带 `-DNDEBUG`（发布/性能构建常见），`assert` 被预处理器删除。
- 后果：所有 CFG/分配断言消失，`main` 恒返回 0，寄存器分配错误完全不可见；`:28` 的 `assert(index < 16)` 消失后还可能缓冲区溢出。
- 最小修复：改用显式 `CHECK` 宏（打印行号并 `return 1`），不依赖 `NDEBUG`。
- 备注：当前 `test_register_optimization.py:252` 用 `-O1 -g -Wall -Wextra -Werror` 且未定义 `NDEBUG`，故现状可工作；但这是脆弱的隐式约定。

### [中] 97-14｜test_register_allocation 直接 `#include` 编译器 `.c` 并依赖 `../../Re.KrtC` 相对布局
- 位置：`Test/Performance/test_register_allocation.c:4`、`:13`
- 类别：依赖路径/与被测实现契约不符
- 触发条件：仓库改名、`Test/Performance` 与 `Re.KrtC` 相对位置变化，或改用安装式头文件。
- 后果：编译直接失败；测试与 `KroCodegen.c` 的内部实现（`prepare_temporary_storage`、`temp_slots` 等）强耦合，实现重构即破坏测试。
- 最小修复：把被测分配逻辑抽成公开头文件/库接口，测试只依赖稳定 API。
- 备注：`:7-12` 的 `#undef KRT_MALLOC` 等也依赖 `KrtCommon.h` 恰好定义了这些宏。

### [中] 97-15｜test_eligible 用 VLA 并以 `slot - temp_slots` 作下标，存在越界/空指针风险
- 位置：`Test/Performance/test_register_allocation.c:103-108`
- 类别：越界/未定义行为
- 触发条件：`temp_slot_count == 0` 时 `eligible` 长度为 1，而 `find_temp_location(context, 1)` 可能返回 `temp_slots` 之外/空指针；或返回的 slot 索引 ≥ `temp_slot_count`。
- 后果：`eligible[slot - temp_slots]` 越界读，断言结果不可信，ASan 未开启时静默。
- 最小修复：显式检查 `slot >= temp_slots && slot < temp_slots + temp_slot_count`，并处理 `temp_slot_count == 0`。
- 备注：当前 fixture 均定义 temp 1，正常路径不触发。

### [高] 97-16｜NativeFib/Driver 完全不校验 fib 结果，恒返回 0
- 位置：`Test/Performance/NativeFib/Driver.c:43-48`
- 类别：断言缺失 / 只打印不校验
- 触发条件：`fib_asm`/`fib_c`/`fib_c_o2` 返回错误值（如汇编实现符号/调用约定错误）。
- 后果：驱动只把 `value` 打印到 JSON 并 `return 0`；任何只看退出码的调用者都会静默接受错误结果。
- 最小修复：在驱动内与期望值比较（如 `if (value != expected) return 1;`），或 `#ifndef EXPECTED #error`。
- 备注：`NativeFibBenchmarks.py:82` 会校验 `sample['result'] != expected`，故当前 harness 下可发现；但驱动自身无自检。

### [中] 97-17｜NativeFib/Driver 把空字符串参数当作 n=0 接受
- 位置：`Test/Performance/NativeFib/Driver.c:21-25`
- 类别：输入校验缺失 / 静默接受非法输入
- 触发条件：`binary c_recursive ""`（`strtol("")` 返回 0，`end` 指向起始，`*end == '\0'`）。
- 后果：非法参数被当成 `fib(0)`，基准/校验流程可能误用该样本；`:23` 的 `*end` 检查对空串失效。
- 最小修复：显式检查 `argv[2][0] == '\0'`，并拒绝前导空白/正号（如用 `strtol` 后校验首字符为数字）。
- 备注：`:18-20` 的 `argc` 检查是正确的。

### [中] 97-18｜NativeFib/Driver 不检查 `printf` 返回值，报告仅写 stdout
- 位置：`Test/Performance/NativeFib/Driver.c:47-48`
- 类别：返回码忽略 / 静默通过
- 触发条件：stdout 被关闭、重定向到已满文件系统或管道断裂。
- 后果：JSON 未完整写出，驱动仍 `return 0`；若调用者只检查退出码则静默通过。
- 最小修复：检查 `printf(...) > 0`，或改用 `write` 并校验字节数（与 `:42` 的 stderr 写法一致）。
- 备注：`NativeFibBenchmarks.py:70` 用 `json.loads` 解析，截断会被发现；但驱动层无保护。

### [中] 97-19｜NativeFib/Driver 使用 `CLOCK_MONOTONIC_RAW` 且单次测量、无预热
- 位置：`Test/Performance/NativeFib/Driver.c:40-47`
- 类别：性能测量方法缺陷（计时口径、单次测量）
- 触发条件：与其它套件（`CLOCK_MONOTONIC`）横向比较，或直接单次运行。
- 后果：RAW 时钟不受 NTP 频率校正影响，与 `CodegenComparison/Driver.c:16`、`.krt` 的 `CLOCK_MONOTONIC` 不同域，跨套件比较失真；单次冷启动测量噪声大。
- 最小修复：统一为 `CLOCK_MONOTONIC`（或全部 RAW），并在驱动内重复多次取中位数/最小值。
- 备注：`NativeFibBenchmarks.py:50` 明确记录「no warmup or discarded samples」，默认 10 轮由外层重复。

### [低] 97-20｜NativeFib/Driver 时长为无符号减法，未检查 `finish >= start`
- 位置：`Test/Performance/NativeFib/Driver.c:47`
- 类别：性能测量方法缺陷 / 数值健壮性
- 触发条件：时钟回退（NTP 步进、计数器异常）或 `clock_gettime` 返回异常值。
- 后果：`nanoseconds(finish) - nanoseconds(start)` 回绕成约 1.8e19 ns，输出荒谬时长；`NativeFibBenchmarks.py:82` 的 `duration_ns <= 0` 检查的是 JSON 解析后的正值，无法识别回绕。
- 最小修复：用有符号差值并检查 `finish >= start`，或检查 `duration_ns` 上限。
- 备注：`CLOCK_MONOTONIC_RAW` 正常不会回退，属低概率。

### [中] 97-21｜NativeFib/Fib.c 依赖 GCC/Clang 专属 `__attribute__((noinline))`，无 `#ifdef` 保护
- 位置：`Test/Performance/NativeFib/Fib.c:8`
- 类别：可移植性 / 与被测实现契约不符
- 触发条件：用非 GCC/Clang 编译器（或不支持该属性的前端）构建。
- 后果：属性被忽略时递归可能被内联/特化，基准失真；某些编译器在 `-Werror` 下直接报错。
- 最小修复：用 `__has_attribute` / `_MSC_VER` 等条件宏隔离，或提供编译器中立的阻止内联手段。
- 备注：当前 `NativeFibBenchmarks.py:38` 用 `cc -std=c11 -Wall -Wextra -Werror`，gcc/clang 下可编译。

### [低] 97-22｜NativeFib/Fib.c 的 `FIB_NAME` 宏名冲突无任何防护
- 位置：`Test/Performance/NativeFib/Fib.c:3-12`
- 类别：构建契约 / 覆盖缺口
- 触发条件：构建脚本对两个变体使用相同 `-DFIB_NAME`（或都省略）。
- 后果：两个 TU 导出同名符号，链接冲突或串扰，`asm`/`c_o2` 比较失效；文件本身不会报错提示。
- 最小修复：在文件内静态断言/文档强制两个宏不同，或拆成两个文件。
- 备注：`NativeFibBenchmarks.py:57-58` 明确区分 `fib_c` 与 `fib_c_o2`。

### [高] 97-23｜CodegenComparison/Driver 不校验 Fib 结果，除时钟/写失败外恒返回 0
- 位置：`Test/Performance/CodegenComparison/Driver.c:36-42`
- 类别：断言缺失 / 只打印不校验
- 触发条件：被链接的 `Fib` 实现返回错误值（如叶子门编码被诊断改变）。
- 后果：`value` 只写进 stderr 报告，退出码仍为 0；只看退出码的调用者静默接受错误结果。
- 最小修复：增加 `if (value != expected) return 1;`，并让构建传入 `-DFIB_EXPECTED`。
- 备注：`CodegenComparison/Run.py:141` 会校验 `value != expected`，当前 harness 下可发现；驱动自身无自检。

### [中] 97-24｜CodegenComparison/Driver 单次测量、无预热
- 位置：`Test/Performance/CodegenComparison/Driver.c:35-37`
- 类别：性能测量方法缺陷（单次测量）
- 触发条件：直接运行驱动，或冷启动/首次页错误。
- 后果：单次冷测量噪声大，结果不可复现；驱动内无重复/取中位数。
- 最小修复：驱动内循环多次取最小/中位数，或文档声明单次并由 harness 重复。
- 备注：`CodegenComparison/Run.py:149` 默认 10 轮在外层重复。

### [中] 97-25｜三个优化级别驱动字节完全相同，无共享来源，改一处不会同步
- 位置：`Test/Performance/GeneralOptimization/Driver.c:9-24`（`LLVMOptimization/Driver.c:9-24`、`O3Optimization/Driver.c:9-24` 逐字节相同）
- 类别：覆盖缺口 / 维护风险
- 触发条件：修复计时、报告或返回码语义时只改其中一份。
- 后果：三个「不同优化级别」的基准口径漂移，比较结论失效；且三份拷贝增加审计面。
- 最小修复：合并为同一 `Driver.c`，用 `-D` 区分；或抽成共享头由三者包含。
- 备注：三份的 `BENCH_A`/`BENCH_B`/`EXPECTED` 均由构建传入，缺失会编译失败（这点是好的）。

### [中] 97-26｜General/LLVM/O3 Driver 只做正确性判断，无基线与性能阈值
- 位置：`Test/Performance/GeneralOptimization/Driver.c:23`（另两份同）
- 类别：性能测量方法缺陷（缺基线/阈值）
- 触发条件：Kernel 性能回归但结果仍正确。
- 后果：退出码为 0，驱动层无任何退化告警；性能结论完全依赖外层 harness。
- 最小修复：由 harness 与 `--baseline` 比较（`GeneralOptimization/Run.py:196` 已有 baseline），驱动内至少上报时长（已上报，见 `:18-22`）。
- 备注：`Run.py:123` 的 `valid` 已含 `duration >= 0`，但无阈值。

### [中] 97-27｜test_fib35.krt 以字面量 `Fib(35)` 作被测调用，存在被编译期特化/折叠的风险
- 位置：`Test/Performance/Runtime/test_fib35.krt:26`（`test_fib40.krt:26` 同）
- 类别：优化器消除被测代码 / 性能测量方法缺陷
- 触发条件：编译器实现常量传播、递归特化或记忆化。
- 后果：计时区间可能只剩常数返回，基准失真；`.krt` 内无 `noinline`/`volatile` 或运行时输入防护。
- 最小修复：把 `n` 改为运行时输入（argv/环境），或显式禁止内联并在 harness 侧校验汇编中仍存在递归调用。
- 备注：`NativeFib/Fib.c:7` 的注释明确「runtime input, no memoization or constant folding」，说明项目意识到该风险，但 `.krt` 未继承该防护。

### [中] 97-28｜test_fib35.krt 被 Run.py 以文本方式解析，存在跨目录脆弱耦合
- 位置：`Test/Performance/Runtime/test_fib35.krt:18`（`int32 Fib(` 分隔点）
- 类别：依赖/契约脆弱
- 触发条件：重命名 `Fib`、调整函数签名或改动 `int32 Fib(` 前缀文本。
- 后果：`LLVMOptimization/Run.py:162` 用 `read_text().split("int32 Fib(",1)[0]` 提取计时样板构造 Artifacts 计时器；文本一变就取错片段，Artifacts 基准全部受影响。
- 最小修复：把 `BenchmarkNow`/`BenchmarkFinish` 抽成独立 `.inc`/`.krt` 片段供双方包含，避免文本解析。
- 备注：该耦合是跨目录的（`Runtime/` ↔ `LLVMOptimization/`）。

### [中] 97-29｜test_fib40.krt 同样以字面量 `Fib(40)` 调用，存在被特化/折叠风险
- 位置：`Test/Performance/Runtime/test_fib40.krt:26`
- 类别：优化器消除被测代码 / 性能测量方法缺陷
- 触发条件：同 97-27。
- 后果：同 97-27；`fib40` 是预算选择/优化级别对比的主力负载，失真影响面更大。
- 最小修复：同 97-27（运行时输入或禁止内联 + 汇编校验）。
- 备注：`RuntimeBenchmarks.py:28` 仅校验结果 `102334155` 与时长范围。

### [中] 97-30｜test_output.krt 计时不含真实 I/O，测的是缓冲/格式化开销
- 位置：`Test/Performance/Runtime/test_output.krt:23-27`
- 类别：性能测量方法缺陷（计时口径）
- 触发条件：`Console.Write` 带用户态缓冲，实际 `write` 推迟到退出刷新。
- 后果：`duration_ns` 主要是在度量数字格式化与缓冲拷贝，真正的输出系统调用发生在计时区间之外；「output 基准」名不副实，跨实现不可比。
- 最小修复：在计时区间内显式 flush 并计入，或改用无缓冲写；至少在校验时把 flush 时间纳入。
- 备注：`RuntimeBenchmarks.py:115` 把 stdout 重定向到文件，缓冲行为取决于 libc/运行时。

### [中] 97-31｜test_output.krt 自身不校验 stdout 内容，只校验循环计数
- 位置：`Test/Performance/Runtime/test_output.krt:24-28`
- 类别：断言缺失 / 覆盖缺口
- 触发条件：`Console.Write` 输出格式改变（加空格/换行/符号）。
- 后果：`.krt` 仍返回 0；只有 `RuntimeBenchmarks.py:53-56` 的精确字节比对能发现，`.krt` 单独运行/复用时无保护。
- 最小修复：在 `.krt` 内自校验输出字节数（如累计 `Write` 返回值）或回读校验。
- 备注：`RuntimeBenchmarks.py:23` 期望 488890 字节，与 `Write(i++)` 的十进制拼接一致。

### [中] 97-32｜21 个 `.krt` 硬编码 x86-64 Linux syscall 号，不可移植
- 位置：`Test/Performance/Runtime/test_fib35.krt:4`（`syscall(228,...)`、`:14` `syscall(1,...)`）
- 类别：可移植性 / 依赖环境
- 触发条件：在 aarch64 等非 x86-64 平台运行（如 Android 原生环境）。
- 后果：228/1/2/3/0/74/8/221 在 aarch64 上分别对应完全不同的调用（clock_gettime 在 aarch64 为 113、write 为 64、openat 为 56 等），计时与报告写入直接错误，甚至崩溃。
- 最小修复：把 syscall 号/封装集中到条件编译的 `.inc`（`#ifdef __x86_64__`），或改用运行时库提供的 `clock_gettime`/`write` 包装。
- 备注：受影响共 21 个 `.krt`：`Runtime/` 5 个、`BudgetSelection/Sources/` 6 个、`Artifacts/` 10 个。`RuntimeBenchmarks.py:81`、`NativeFibBenchmarks.py:35` 有 x86_64 守卫；`BudgetSelection/Measure.py`、`LLVMOptimization/Run.py`、`GeneralOptimization/Run.py`、`O3Optimization/Run.py` 无守卫。

### [中] 97-33｜21 个 `.krt` 均为单次测量、无预热，重复完全依赖外部 harness
- 位置：`Test/Performance/Runtime/test_fib35.krt:21-29`（`BenchmarkNow` 只包住一次 `Kernel`）
- 类别：性能测量方法缺陷（单次测量、无预热）
- 触发条件：直接运行 `.krt` 产物，或换用不重复的 harness。
- 后果：冷启动、页错误、动态链接器开销计入单次结果，方差大且不可复现；`.krt` 自身无重复/中位数/最小值逻辑。
- 最小修复：在 `.krt` 内做 N 次循环取最小/中位数（注意别让编译器合并重复调用），或文档明确单次语义。
- 备注：`RuntimeBenchmarks.py` 默认 10 轮、`BudgetSelection/Measure.py` 默认 3 轮在外层重复；`Artifacts` 由 `Run.py` 默认 10 轮重复。

### [中] 97-34｜NarrowArguments.krt 的期望值编码了有符号 30 位回绕语义，文件内无契约声明
- 位置：`Test/Performance/LLVMOptimization/BudgetSelection/Sources/NarrowArguments.krt:19-23`、`:28-30`
- 类别：与被测实现契约不符 / 数值语义依赖
- 触发条件：编译器对 `int30` 采用更宽中间算术、插入溢出检查，或规范未定义有符号溢出。
- 后果：初值 `536870911` 为 `INT30_MAX`，`value+(int30)3` 必然溢出；期望 `531908544` 只在「30 位回绕」下成立。实现一变测试即失败，且失败原因可能被误判为预算选择错误。
- 最小修复：在文件/规范中显式声明 `int30` 有符号溢出按二补码回绕，或改用不溢出的初值/运算。
- 备注：该用例的价值正是验证窄参数传递，但期望值必须与规范绑定。

### [低] 97-35｜Parameters.krt 的期望值依赖有符号 64 位溢出/回绕的具体行为
- 位置：`Test/Performance/LLVMOptimization/BudgetSelection/Sources/Parameters.krt:19-23`、`:28-30`
- 类别：数值语义依赖
- 触发条件：`a+b`、`b-3` 等中间结果溢出 `int64`，编译器按 UB 假设优化。
- 后果：期望 `517069303` 可能被编译器的 UB 假设改变，测试随机失败或约束错误行为。
- 最小修复：显式使用无符号类型，或在规范中定义有符号溢出语义。
- 备注：与 97-34 同类；`BinaryTree.krt` 的 `Tree` 结果也在 `int64` 范围但 2^22 不溢出。

### [中] 97-36｜BinaryTree.krt 的 `Tree(n-1)+Tree(n-1)` 可被 CSE 合并，测量工作量不稳定
- 位置：`Test/Performance/LLVMOptimization/BudgetSelection/Sources/BinaryTree.krt:19-22`（另见 `Artifacts/BinaryTree.krt:19-22`）
- 类别：性能测量方法缺陷 / 优化器消除被测代码
- 触发条件：编译器实现公共子表达式消除（两个实参完全相同）。
- 后果：递归调用数减半，基准测的不再是「二叉递归」的工作量；不同优化级别下 CSE 与否会使预算选择结论漂移。
- 最小修复：让两个递归实参不同（如 `Tree(n-1)+Tree(n-1-b)`），或明确声明允许 CSE 并把它计入被测优化。
- 备注：`Branching.krt` 的 `Paths(n-1)+Paths(n-3)` 无此问题。

### [中] 97-37｜Artifacts/*.krt 与 BudgetSelection/Sources/*.krt 有 4 个同名重复，无同步机制
- 位置：`Test/Performance/LLVMOptimization/Artifacts/Fib35.krt:19-33`、`Fib40.krt:19-33`、`BinaryTree.krt:19-33`、`Branching.krt:19-33`（对应 `BudgetSelection/Sources/` 同名文件）
- 类别：覆盖缺口 / 维护风险
- 触发条件：修改 `Sources/` 中的负载（函数体/参数/期望值）而不同步 `Artifacts/`，或反之。
- 后果：两处基准负载漂移，预算选择与优化级别对比结论失真；当前两者仅 `main` 的报告顺序不同，函数体一致，靠人工保持。
- 最小修复：让 `Artifacts/` 由 `Sources/` 生成/软链，或加一致性校验（哈希/期望值比对）。
- 备注：`LLVMOptimization/Run.py:139-199` 同时使用 `Artifacts` 的 `.krt` 与 `.c`。

### [中] 97-38｜Artifacts/*.c 无 `main`、无自校验，且与同名 `.krt` 的一致性无自动校验
- 位置：`Test/Performance/LLVMOptimization/Artifacts/Fib35.c:1-7`（另 9 个 `.c` 同）
- 类别：覆盖缺口 / 契约脆弱
- 触发条件：转写 C 与 `.krt` 语义漂移（如 `uint32` 与 `int32`、循环边界、运算符）。
- 后果：gcc 基线与 Krt 基准不同源，比较结论无效；`.c` 只能靠 `Driver.c` 的 `EXPECTED` 间接校验，而 `EXPECTED` 由构建传入，写错即整体误判。
- 最小修复：为 `.c` 增加自校验 `main`（或统一由 `.krt` 生成 `.c`），并加 `.c`/`.krt` 语义一致性测试。
- 备注：本次人工核对 10 组 `.c`/`.krt` 函数体一致；`Run.py:177-196` 用 gcc 编译 `.c` 并与 `Driver.c` 链接。

### [低] 97-39｜Artifacts/*.krt 先写报告再校验，与 Sources/*.krt 顺序相反，行为不一致
- 位置：`Test/Performance/LLVMOptimization/Artifacts/Fib35.krt:30-32`（另 9 个同）
- 类别：一致性 / 可维护性
- 触发条件：结果错误时排查；或调用方依赖「有报告写出」判断进程是否走到计时结束。
- 后果：`Artifacts` 在结果错误时仍输出 16 字节报告并 `return 1`，`Sources` 则不输出报告；同类负载两种失败语义，harness 若假设其中一种会误判。
- 最小修复：统一顺序（建议先校验再写报告，或统一先写报告），并在文档中声明。
- 备注：`BudgetSelection/Measure.py:40` 与 `LLVMOptimization/Run.py:128` 都要求 `len(stderr)==16`，故 `Sources` 的错误路径会直接抛异常而非报「错误值」。

### [低] 97-40｜test_compiler_hotspots 初始化内存安全跟踪但从不关闭/上报统计
- 位置：`Test/Performance/test_compiler_hotspots.c:336`
- 类别：资源泄漏 / 覆盖缺口
- 触发条件：每次运行；进程退出时依赖运行时自带 atexit 清理。
- 后果：若 `KrtMemorySafetyInit` 注册了堆/统计，未显式 shutdown 则泄漏量与统计不可见；测试无法报告「有泄漏」。
- 最小修复：在 `main` 返回前调用对应的 shutdown/统计接口并校验无泄漏（若接口存在）。
- 备注：未在允许读取范围找到该 API 的定义，仅凭此文件无法确认是否必需。
