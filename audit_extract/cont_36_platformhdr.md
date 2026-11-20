# 第九十三批：Core/Platform/{Platform.h,ThreadPool.h,Threading.h} + Compiler/Platform/PlatformAbstraction.h

## 覆盖范围

- 全量读取（read 工具）：
  - `Re.KrtC/src/Core/Platform/Platform.h`（258 行，实际 258，任务书称约 259）
  - `Re.KrtC/src/Core/Platform/ThreadPool.h`（29 行）
  - `Re.KrtC/src/Core/Platform/Threading.h`（29 行）
  - `Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h`（39 行）
- 仅以 grep 特定符号核对调用方/定义（未整文件通读其它源文件）：`KrtPlatformCreateDirectory`、`KrtWriteFilePosix/Win32`、`KrtReadFilePosix/Win32`、`KRT_EXPORT/KRT_IMPORT/KRT_API/KRT_INLINE/KRT_NOINLINE/KRT_PACKED/KRT_UNPACKED/KRT_ALIGNED`、`KRT_LINKER_*`、`KRT_ASM_*`、`KrtMallocImpl/KrtCallocImpl/KrtReallocImpl/KrtFreeImpl`、`KrtThreadCreate/KrtMutex*/KrtAtomic*/KrtLoadLibrary/KrtGetProcAddress/KrtTimeMs/KrtTimeUs/KrtSleepMs/KrtGetLastError/KrtError_*`、`thread_pool_create/submit/wait/destroy/get_active_tasks`、`KrtPlatform*` 全部 API、`PlatformCommon.h` 的 include 情况、`KrtCreateDirectory`。
- 已核实：`PlatformCommon.h` 全工程零 `#include`；`Core.h:7-9` 同时包含 Platform.h / Threading.h / ThreadPool.h；`ThreadPool.c:1` 与 `ParallelCompiler.h:6` 只包含 ThreadPool.h。
- 说明：PlatformAbstraction.c 已审过，本批只记头文件与未实现声明；`PlatformCommon.h` 仅作为 Platform.h 的对照物出现（不在本批覆盖范围，但它是 Platform.h 的重复副本，必须一并定位）。

## 问题条目

### [高] 93-1｜Threading.h 与 ThreadPool.h 共用 include guard `THREAD_POOL_H`
- 位置：`Re.KrtC/src/Core/Platform/Threading.h:1-2` + `Re.KrtC/src/Core/Platform/ThreadPool.h:1-2`（触发点 `Re.KrtC/src/Core/Core.h:8-9`）
- 类别：语义分叉 / 配置漂移（include guard 撞名）
- 触发条件：任何 TU 同时包含这两个头；`Core.h` 第 8、9 行连续包含，`Threading.h` 先占位，`ThreadPool.h` 被整体跳过。
- 后果：`ThreadPool.h:4` 的 `#include "../Utils/KrtCommon.h"` 与 `Threading.h:4` 的 `#include "Platform.h"` 只有一处生效；两文件当前内容逐字相同所以暂未暴露，一旦独立演进即静默取错一份。若某 TU 依赖 Threading.h 传递包含 Platform.h 且 ThreadPool.h 已先被包含，Platform 宏静默缺失。
- 最小修复：`Threading.h` 改用 `KRT_THREADING_H`，并删除其中重复的 ThreadPool 定义（见 93-26）。
- 备注：与已记录的 S55-1（Error.h/StackCalculator.h guard 撞名）同类；因两文件当前内容一致，危害为潜伏期，故记「高」而非「致命」。

### [高] 93-2｜Platform.h 与 PlatformCommon.h 是同一平台抽象的两份拷贝，且已产生 4 处分歧
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:1-2,163,244` vs `Re.KrtC/src/Core/Platform/PlatformCommon.h:1-2,148,164,245`
- 类别：死代码 / 配置漂移
- 触发条件：任何按 `KRT_LINKER_RUNTIME_LIBS` 拼链接命令、或使用建目录宏的代码；或将来把 PlatformCommon.h 接回构建。
- 后果：`PlatformCommon.h` 全工程零 `#include`（grep 确认），是死头文件；两份副本已出现 4 处不一致：① include guard（`PLATFORM_H` vs `KRT_PLATFORM_H`）；② Windows 建目录宏 `KrtCreateDirectory` 只在 PlatformCommon.h:148 有，Platform.h Windows 分支缺失；③ POSIX 建目录宏名 `KrtPlatformCreateDirectory`（Platform.h:163）vs `KrtCreateDirectory`（PlatformCommon.h:164）；④ POSIX 运行时库 `"-lc -lm"`（Platform.h:244）vs `"-lc -lm -lpthread"`（PlatformCommon.h:245）。调用方按其中一份编写即与另一份不兼容。
- 最小修复：删除 `PlatformCommon.h`，或让 `Platform.h` 成为唯一副本并统一宏名与 `-lpthread`。
- 备注：`audit_extract/chunk_08.md` S55-7 已记 `KrtCreateDirectory` 与 `Path.h:9` 的冲突；本条针对 Platform.h 侧的副本分叉。

### [高] 93-3｜`KrtAtomicCmpXchg` 返回语义跨平台不一致
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:188`（Windows）vs `Platform.h:195-196`（POSIX）
- 类别：语义分叉
- 触发条件：调用方按 POSIX 习惯用返回值判 CAS 成功，或按 Windows 习惯把返回值与期望值比较。
- 后果：Windows `InterlockedCompareExchange` 返回**交换前旧值**；POSIX `__atomic_compare_exchange_n` 返回**成功布尔（1/0）**。同一句 `if (KrtAtomicCmpXchg(&x, e, d))` 在 Windows 上只要旧值非 0 就为真（CAS 失败也判成功），POSIX 上失败为 0，语义相反。
- 最小修复：统一为返回旧值（POSIX 先用 `__atomic_load_n` 取值再 CAS），或统一返回布尔并改名 `KrtAtomicTryCmpXchg`。
- 备注：全工程当前无调用点（见 93-13），属未爆雷的语义分叉。

### [高] 93-4｜`KrtWriteConsole`/`KrtReadConsole`/`KrtWriteFilePosix`/`KrtReadFilePosix` 静默吞掉 write/read 错误与短写
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:77-80`
- 类别：静默失败 / 错误处理不一致
- 触发条件：`write`/`read` 返回 -1（EINTR/EAGAIN/EBADF）或返回值小于 `count`（短写/短读，管道、TTY、磁盘满）。
- 后果：`*(written) = write(...)` 把 `(size_t)-1` 直接写进出参，错误被永久覆盖、无法回溯；短写被当作完整写入，后续数据流错位；两个宏全工程零调用点，接口却看似可用。
- 最小修复：返回 `int` 状态；内部循环处理 EINTR 与短写；失败时 `KrtSetLastError(errno)`。
- 备注：`KrtWriteConsole`/`KrtReadConsole` 同样零调用（grep 仅命中两个头文件）。

### [高] 93-5｜`KrtWriteFileWin32`/`KrtReadFileWin32` 忽略 `WriteFile`/`ReadFile` 的 BOOL 返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:63-74`
- 类别：静默失败 / 诊断缺失
- 触发条件：句柄无效、访问被拒、磁盘满、重叠 I/O 失败。
- 后果：返回值被丢弃；`WriteFile` 失败时 `_written` 从未赋值，`*written` 保持调用方传入的栈/堆原值，被下游当作有效写入长度；错误码随后被其他 API 覆盖，`KrtGetLastError` 取不到真实原因。
- 最小修复：检查 BOOL，失败时 `KrtSetLastError(GetLastError())` 并返回 0；`written` 先置 0。
- 备注：与 POSIX 侧 93-4 构成同层两套错误处理风格（一侧返回 ssize_t、一侧丢弃 BOOL）。

### [高] 93-6｜`KrtThreadCreate` 丢弃 `pthread_create`/`_beginthreadex` 返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:105`（Windows）与 `Platform.h:117`（POSIX）
- 类别：静默失败 / 诊断缺失
- 触发条件：线程数超限（EAGAIN）、内存不足（ENOMEM）、`_beginthreadex` 返回 NULL。
- 后果：POSIX 下 `pthread_create` 失败时 `handle` 未被写入，后续 `KrtThreadWait`/`KrtThreadClose` 作用于未初始化 `pthread_t`（UB）；Windows 下 handle 为 NULL，`WaitForSingleObject(NULL)` 立即失败但无人检查。线程池静默少线程运行，吞吐下降无告警。
- 最小修复：宏改为返回 `int`，调用方检查；失败设 `KrtError_OUT_OF_MEMORY`。
- 备注：两平台回调签名也不统一（`_beginthreadex` 要 `unsigned (__stdcall*)(void*)`，`pthread_create` 要 `void*(*)(void*)`），同一份调用代码无法两平台编译（契约破坏，同一 macro 的隐含约定未文档化）。

### [高] 93-7｜`KrtMutexInit/Lock/Unlock/Destroy` 全部丢弃返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:108-111`（Windows）与 `Platform.h:120-123`（POSIX）
- 类别：静默失败 / 错误处理不一致
- 触发条件：POSIX `pthread_mutex_init` 返回 EINVAL/EBUSY/ENOMEM；`pthread_mutex_lock` 在错误检测型 mutex 上返回 EOWNERDEAD/EINVAL；`pthread_mutex_destroy` 对仍锁定的 mutex 返回 EBUSY（POSIX 允许但结构已不可复用）。
- 后果：初始化失败（如静态内存未对齐/EINVAL）后继续使用该 mutex，加锁行为未定义且无任何诊断；同层其它模块（如 `KrtError`）用返回码而本层用丢弃，风格不统一。
- 最小修复：改为返回 `int` 的内联包装并断言；Destroy 前断言未锁定。

### [高] 93-8｜`KrtThreadWait` 丢弃 `pthread_join` 返回值，且 `KrtThreadClose` 在 POSIX 是空宏
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:118-119`（POSIX）、`Platform.h:106-107`（Windows）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：对未初始化、已 join 或非可 join 的 `pthread_t` 调用 `pthread_join`（EINVAL/EDEADLK）；Windows 对已关闭句柄调用 `CloseHandle`。
- 后果：`KrtThreadWait` 无法区分成功/失败；`KrtThreadClose` 在 POSIX 展开为空语句，与 Windows `CloseHandle`（释放内核句柄）语义不对称——同一"关闭句柄"调用在两平台一个释放资源一个什么也不做，资源所有权约定未文档化、未断言。
- 最小修复：`KrtThreadWait` 返回 int；在头文件写明"每个 handle 只能 Wait/Close 一次"，并对重复调用断言。

### [高] 93-9｜`KrtPlatformCreateDirectory` 只有 POSIX 定义，Windows 分支缺失且命名分叉
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:163`（POSIX 分支），对照 Windows 分支 `Platform.h:144-151`
- 类别：语义分叉 / 配置漂移 / 死代码
- 触发条件：Windows 平台任何调用建目录的代码；或从 `PlatformCommon.h` 迁移过来的调用方（那边叫 `KrtCreateDirectory`）。
- 后果：Platform.h 的 Windows 分支没有 `KrtPlatformCreateDirectory`/`KrtCreateDirectory`，Windows 下调用直接编译失败；POSIX 宏名又与 PlatformCommon.h 不一致（`KrtPlatformCreateDirectory` vs `KrtCreateDirectory`），且 `KrtPlatformCreateDirectory` 全工程零调用点。
- 最小修复：统一命名 `KrtPlatformCreateDirectory`，并在 `Platform.h:144-151` 补 `CreateDirectory(path, NULL)` 版本。
- 备注：与 93-2 的分歧清单②③对应。

### [中] 93-10｜`KrtDirectoryExists` 返回域两平台不一致，且 Windows 重复调用 `GetFileAttributes`
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:146-147`（Windows）vs `Platform.h:159-161`（POSIX）
- 类别：语义分叉 / 未检查前置条件
- 触发条件：调用方写 `== 1` 或把返回值当整数用；或在两次 `GetFileAttributes` 之间目录属性被并发修改。
- 后果：Windows 返回 `FILE_ATTRIBUTE_DIRECTORY`（0x10）而非 1，`KrtDirectoryExists(p) == 1` 恒假；POSIX 返回 0/1，同一判断在 POSIX 为真。两次 `GetFileAttributes` 构成 TOCTOU 窗口。
- 最小修复：Windows 归一化为 `(attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0`，并只调用一次。

### [中] 93-11｜`KrtRemoveDirectory`/`KrtDeleteFile` 丢弃返回值
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:148-149`（Windows）、`Platform.h:164-165`（POSIX）
- 类别：静默失败
- 触发条件：目录非空、文件只读、权限不足、路径不存在。
- 后果：`RemoveDirectory`/`rmdir`/`DeleteFile`/`unlink` 失败被吞，构建/清理脚本继续执行并误判成功；同层 `KrtFileExists` 有布尔返回而删除无，错误处理不一致。
- 最小修复：返回 `int`，失败时 `KrtSetLastError` 并由调用方诊断。

### [中] 93-12｜`KrtMallocImpl/KrtFreeImpl/KrtReallocImpl/KrtCallocImpl` 在 POSIX 只有声明、全工程无定义
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:95-98`
- 类别：死代码 / 契约破坏
- 触发条件：任何在 POSIX 调用这四个函数（或包含该声明的 TU 被链接）。
- 后果：grep 全工程仅命中本头文件与 PlatformCommon.h 的声明，没有任何 `.c` 提供定义；一旦调用即 `undefined reference`。声明暗示存在统一分配实现，实际 Windows 侧是宏、POSIX 侧是空声明。
- 最小修复：在 `Core/Platform` 的 .c 中提供实现，或删除这 4 行声明。

### [中] 93-13｜导出/调用约定/属性宏整族零使用
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:15-22`（`KRT_EXPORT/KRT_IMPORT/KRT_API`）与 `Platform.h:219-229`（`KRT_INLINE/KRT_NOINLINE/KRT_PACKED/KRT_UNPACKED/KRT_ALIGNED`）
- 类别：死代码
- 触发条件：任何时刻（grep 全工程仅命中 Platform.h 与 PlatformCommon.h 的定义处）。
- 后果：整族宏无调用点；`KrtCommon.h:123-134` 另有一套 `KRT_API_EXPORT/KRT_API_IMPORT/KRT_API_EXPORT_IMPORT`，命名与职责重叠，后续维护者无法判断该用哪套。
- 最小修复：删除未使用的一套（保留 KrtCommon.h 的 DLL 导出宏），或在头文件注明归属。

### [中] 93-14｜`KRT_LINKER_*`/`KRT_ASM_*` 整族零调用，且 Windows 工具链自相矛盾
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:233-255`
- 类别：死代码 / 配置漂移
- 触发条件：任何按这些宏拼链接/汇编命令的代码；或将来启用 Windows 构建。
- 后果：全工程零调用点。Windows 分支 `KRT_LINKER_CMD "ld.exe"` 却配 `-subsystem:console`、`-entry:main`（GNU ld 语法；MSVC `link.exe` 用 `/SUBSYSTEM:CONSOLE`、`/ENTRY:`），且 `ld.exe` 不是 MSVC 自带工具；POSIX 分支 `KRT_LINKER_CONSOLE_FLAG` 为空串，拼接命令行会注入空参数。
- 最小修复：删除该族宏，或按平台给出正确的工具与 flag（Windows 用 `link.exe /SUBSYSTEM:...`，POSIX 用 `cc` 驱动）。

### [中] 93-15｜`KRT_LINKER_RUNTIME_LIBS` POSIX 分支缺 `-lpthread`
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:244`
- 类别：配置漂移
- 触发条件：按该宏链接任何使用 pthread 的目标（工程内 `ThreadPool.h/.c` 直接使用 pthread）。
- 后果：glibc < 2.34 下 `pthread_create/pthread_mutex_*` 不在 libc 中，链接失败；与 PlatformCommon.h:245 的 `"-lc -lm -lpthread"` 不一致，两份副本给出不同链接语义。
- 最小修复：改为 `"-lc -lm -lpthread"`，或统一改用 `cc` 驱动链接。

### [中] 93-16｜`KrtTimeMs` 时钟源语义分叉
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:129`（Windows `GetTickCount64`）vs `Platform.h:135-139`（POSIX `gettimeofday`）
- 类别：语义分叉 / 阶段不对齐
- 触发条件：POSIX 上 NTP 校时、手动改时间、闰秒导致墙钟回拨或跳变；调用方用返回值算 duration。
- 后果：Windows 为单调时钟、POSIX 为墙钟，同一 API 两平台语义不同；POSIX 下耗时可算出负值或巨大值。与 `Core/Utils/Path.c:30` 的 `CLOCK_MONOTONIC` 又构成第三套时间语义。
- 最小修复：POSIX 改用 `clock_gettime(CLOCK_MONOTONIC)`，与 Path.c 对齐。

### [中] 93-17｜`KrtTimeUs` 的微秒位恒为 0
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:130` 与 `Platform.h:140`
- 类别：注释与实现脱节
- 触发条件：任何依赖微秒分辨率的计时/基准代码。
- 后果：两平台实现都是 `KrtTimeMs()*1000`，实际分辨率 1 ms，`KrtTimeUs` 命名承诺的微秒精度不存在；与同文件 `KrtTimeNowSeconds`（Path.c 用 `tv_nsec`）精度不一致。
- 最小修复：POSIX 直接返回 `tv_sec*1000000 + tv_usec`；Windows 改用 `QueryPerformanceCounter`。

### [中] 93-18｜`KrtSleepMs` POSIX 分支无前置条件检查且使用 obsolete API
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:141`
- 类别：未检查前置条件
- 触发条件：`ms <= 0`（`usleep(0)` 尚可，负数经 `(ms)*1000` 转成巨大无符号）；`ms >= 1000`（POSIX 规定 `usleep` 入参 >= 1000000 时行为未定义/返回 EINVAL）。
- 后果：负值变成超长睡眠；`usleep` 已被 POSIX 标记为 obsolete（应为 `nanosleep`）；与 Windows `Sleep(ms)` 的语义（接受 0、负数未定义）也不对齐。
- 最小修复：`ms <= 0` 直接返回；改用 `nanosleep`。

### [中] 93-19｜`KRT_INLINE` 链接属性跨平台不一致
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:219`（Windows `__forceinline`）vs `Platform.h:225`（POSIX `static inline`）
- 类别：语义分叉
- 触发条件：用 `KRT_INLINE` 修饰头文件中的函数定义。
- 后果：POSIX 得到内部链接（每 TU 一份本地副本，`&func` 地址不同、静态变量不共享）；Windows `__forceinline` 为外部链接语义。同一份头文件代码的 ODR/符号可见性/地址一致性在两平台不同。
- 最小修复：拆成两个宏（`KRT_FORCEINLINE` 与 `KRT_HEADER_INLINE`），或 POSIX 侧统一 `extern inline` 方案。

### [中] 93-20｜`KRT_PACKED`/`KRT_UNPACKED` 在 Windows 是 pragma，不能当声明说明符用
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:221-222`（`__pragma(pack(push,1))` / `__pragma(pack(pop))`）vs `Platform.h:227-228`（`__attribute__((packed))` / 空）
- 类别：契约破坏 / 语义分叉
- 触发条件：按 POSIX 习惯写 `KRT_PACKED struct Foo {...} KRT_UNPACKED;`。
- 后果：`__pragma` 是语句级指令，不能出现在声明说明符位置，Windows 下该写法直接编译失败；两平台没有同一写法可用，抽象宏实际不可移植。
- 最小修复：提供两平台都可用的包装写法（如 `struct KRT_PACKED Foo {...};` + 单独 `KRT_UNPACKED` 空定义），或注明仅支持 GNU 属性语法。

### [低] 93-21｜平台支持范围与 KrtCommon.h 分叉
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:10-11`（`__APPLE__` 落到 `#error "Unsupported platform"`）vs `Re.KrtC/src/Core/Utils/KrtCommon.h:140-143`（存在 `__APPLE__` 分支）
- 类别：配置漂移
- 触发条件：在 macOS 上编译任何包含 Platform.h 的 TU。
- 后果：KrtCommon.h 宣称支持 Apple，Platform.h 直接 `#error` 拒绝；同一工程对同一平台给出两种结论，编译期行为不可预期。
- 最小修复：统一平台判定表（以 Platform.h 为准删掉 KrtCommon.h 的 Apple 分支，或反之补齐 Platform.h 的 Apple 分支）。

### [低] 93-22｜`KRT_STDIN/OUT/ERR_HANDLE` 未检查 `GetStdHandle` 失败
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:41-43`
- 类别：未检查前置条件
- 触发条件：进程无关联控制台/标准句柄被重定向关闭时，`GetStdHandle` 返回 NULL 或 `INVALID_HANDLE_VALUE`。
- 后果：句柄常量在宏展开期即固化为无效值，后续 `KrtWriteConsole` 静默失败（见 93-4/93-5），且没有任何诊断。
- 最小修复：提供 `KrtGetStdHandle()` 运行期函数并检查返回值。

### [低] 93-23｜`KrtReadConsole` POSIX 分支静默忽略 `ctrl` 参数
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:78`（POSIX）vs `Platform.h:57-62`（Windows 传给 `ReadConsole`）
- 类别：语义分叉
- 触发条件：调用方依赖 `ctrl` 控制控制台读取行为。
- 后果：同签名（5 参）两平台语义不同：Windows 生效、POSIX 完全忽略且产生未使用参数警告；抽象层未文档化该差异。
- 最小修复：POSIX 分支 `(void)ctrl;` 显式标注，或在头文件注明仅 Windows 有效。

### [低] 93-24｜重复包含与无用包含
- 位置：`Re.KrtC/src/Core/Platform/Platform.h:32,92,134,154`（`<unistd.h>` 四次）、`Platform.h:26` 与 `Platform.h:84-86`（`<windows.h>` 两次，后者 `#ifndef _WINDOWS_` 守护）、`Platform.h:128`（`<time.h>` 无使用）
- 类别：死代码（低）
- 触发条件：编译期。
- 后果：重复包含靠系统头自身 guard 兜底，增加预处理噪音；`<time.h>` 在 Windows 分支引入却未被本文件使用，属复制粘贴残留。
- 最小修复：合并重复包含，删除未使用的 `<time.h>`。

### [高] 93-25｜`ThreadPool` 的 `shutdown`/`active_tasks` 是普通 `int`，头文件未约束并发访问
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:19-20`（`Threading.h:19-20` 同）
- 类别：共享可变状态
- 触发条件：worker 线程读 `shutdown`、destroy 线程写 `shutdown`、调用方读 `active_tasks`（经 `thread_pool_get_active_tasks`，见 93-27）。
- 后果：结构体把两个跨线程可变字段暴露为普通 `int`，头文件既未用 `_Atomic` 也未注释"仅可持 `queue_mutex` 访问"；`thread_pool_get_active_tasks` 提供的是一个无锁读接口，数据竞争（C11 未定义行为）由声明层直接背书。
- 最小修复：改为 `_Atomic int`，或在结构体注释中强制"访问必须持 queue_mutex"并让 `thread_pool_get_active_tasks` 内部加锁。
- 备注：实现细节在 ThreadPool.c（已超出本批范围），但字段类型与注释义务在头文件。

### [高] 93-26｜`Threading.h` 与 `ThreadPool.h` 内容逐字重复，仅 include 行不同
- 位置：`Re.KrtC/src/Core/Platform/Threading.h:7-27` vs `Re.KrtC/src/Core/Platform/ThreadPool.h:7-27`
- 类别：死代码 / 配置漂移（重复实现）
- 触发条件：任何时刻；任一处增删字段或函数。
- 后果：`ThreadTask`/`ThreadPool` 与 5 个函数声明存在两份独立拷贝，仅 `Threading.h:4` 的 `#include "Platform.h"` 与 `ThreadPool.h:4` 的 `#include "../Utils/KrtCommon.h"` 不同；配合 93-1 的 guard 撞名，实际只有一份生效，另一份成为"看似存在"的假接口，维护者无法判断改哪份。
- 最小修复：保留 `ThreadPool.h`，`Threading.h` 改为仅 `#include "ThreadPool.h"`（如需保留旧路径作兼容）。

### [中] 93-27｜`thread_pool_get_active_tasks` 有定义、零调用
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:27`（定义 `Re.KrtC/src/Core/Platform/ThreadPool.c:163`）
- 类别：死代码
- 触发条件：任何时刻（grep 全工程无调用点）。
- 后果：暴露了一个无锁读接口却无人使用；后续调用者一旦使用即落入 93-25 的数据竞争。
- 最小修复：删除该声明与定义，或加锁后接入并行编译的进度上报。

### [中] 93-28｜`thread_pool_create(int thread_count)` 前置条件未文档化、未断言
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:23`（`Threading.h:23` 同）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：`thread_count <= 0`（无 worker，submit 后任务永不执行）或极大值（`pthread_create` 逐个失败）。
- 后果：头文件对合法区间、上限、失败返回（NULL？）无任何约定，调用方 `ParallelCompiler.c:286` 传入 `compiler->max_threads`，其合法性与失败处理完全依赖实现细节。
- 最小修复：头文件注明 `thread_count > 0`，并在实现入口断言/返回 NULL + 诊断。

### [中] 93-29｜`thread_pool_destroy/wait/submit` 的生命周期与所有权契约缺失
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:24-26`
- 类别：契约破坏（线程/所有权约定未文档化未断言）
- 触发条件：destroy 时队列仍有未执行任务；submit 与 destroy 并发；同一 pool 重复 destroy；`arg` 指向调用方栈对象。
- 后果：头文件未定义 destroy 是否等待在途任务、是否可重入、submit 返回后能否 destroy、task 节点由谁释放、`arg` 生命周期由谁保证。并发 `submit`+`destroy` 直接 UAF；destroy 丢弃排队任务也是静默失败（实现侧问题，但契约空白是根因）。
- 最小修复：在头文件写明状态机（create→submit*→wait?→destroy 单向），并断言 `pool != NULL`、destroy 只调一次。

### [中] 93-30｜`thread_pool_wait` 语义未定义（排空队列 vs `active_tasks` 归零），无超时/返回码
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:26`
- 类别：契约破坏 / 诊断缺失
- 触发条件：调用方在 `wait` 返回后假设所有任务已完成并读取其产出。
- 后果：`wait` 返回 `void`，无超时、无失败码；"等待队列排空"与"等待活跃任务归零"是两种不同保证，调用方（`ParallelCompiler.c:294`）按后者使用，一旦实现是前者就会读到未写完的数据。
- 最小修复：头文件明确 `wait` 保证 `active_tasks == 0 && task_queue == NULL`，并返回 `int` 以便上报失败。

### [中] 93-31｜两个线程池头文件依赖不同，可见类型随包含顺序变化
- 位置：`Re.KrtC/src/Core/Platform/ThreadPool.h:4`（`../Utils/KrtCommon.h`）vs `Re.KrtC/src/Core/Platform/Threading.h:4`（`Platform.h`）
- 类别：配置漂移
- 触发条件：不同 TU 以不同顺序包含两者（`Core.h` 三者全含；`ParallelCompiler.h:6` 只含 ThreadPool.h）。
- 后果：同一 `ThreadPool` 类型所在头文件对外部类型的可见集不同，预处理结果依赖包含顺序；配合 93-1 的 guard 撞名，`Core.h` 路径下 Threading.h 生效、`ParallelCompiler.h` 路径下 ThreadPool.h 生效，两条路径的隐式依赖不同。
- 最小修复：合并为一个头文件（93-26），统一依赖。

### [高] 93-32｜`KrtPlatformPathJoin` 不在 vtable 中，且 `buffer_size` 语义无文档、无断言
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:29`（结构体 `PlatformAbstraction.h:7-20` 无 `path_join` 指针）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：任何调用点（`Main.c:965,975,1043,1058,1203,1232,1239,1240`）；`buffer_size` 传 0、`buffer`/`base`/`part` 为 NULL、或 `base`/`part` 长度之和接近 `buffer_size`。
- 后果：`buffer_size` 是否包含结尾 NUL、超容量是截断还是返回错误、返回值含义（该函数声明为 `void`）全部未定义；调用方按 `snprintf` 语义假设即可能越界写入或静默截断路径。结构体有 `path_exists`/`is_directory` 指针却无 `path_join`，抽象层对同类能力提供两种扩展方式。
- 最小修复：把 `path_join` 加入 `KrtPlatform` 结构体，或在头文件写明契约（含 NUL、超容返回 -1）并加 `assert(buffer && buffer_size > 0)`。

### [高] 93-33｜`KrtPlatformExecuteCommand` 返回 `int` 的成功/失败语义未文档化
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:19`（`int (*execute_command)(const char*)`）与 `PlatformAbstraction.h:34`
- 类别：契约破坏 / 诊断缺失
- 触发条件：任何调用方按返回值判断命令是否成功。
- 后果：返回值究竟是子进程 exit code、`0/1` 布尔、还是 `system()` 的原始返回（须用 `WEXITSTATUS` 解析）未定义；三者在命令失败时给出完全不同的值，调用方无法统一错误处理，命令失败可能被当成成功。
- 最小修复：头文件明确"返回 0 表示成功，非 0 为子进程退出码或 -1（errno 语义）"，并规定失败时的诊断输出。

### [中] 93-34｜`KrtPlatformCreate/Destroy` 的所有权与失败约定不完整（仅 `GetCurrent` 有注释）
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:22-23`（对照 `PlatformAbstraction.h:36-37` 的 `GetCurrent` 注释）
- 类别：契约破坏
- 触发条件：传入非法 `KrtPlatformType`；对同一指针重复 `Destroy`；`Destroy(NULL)`。
- 后果：`Create` 对非法 type 是否返回 NULL、`Destroy` 是否幂等/接受 NULL、`Create` 结果的所有权（谁 free）均未文档化、未断言；`Main.c` 中大量 `KrtPlatformDestroy(platform)` 紧跟错误分支，一旦 `Create` 返回 NULL 而 `Destroy` 不接受 NULL 就是崩溃。
- 最小修复：为全部 API 补注释（参照 `GetCurrent` 的"caller owns the result"写法），并规定 `Destroy(NULL)` 安全、`Create` 非法 type 返回 NULL。

### [中] 93-35｜`path_separator` 字段与 `KRT_PATH_SEPARATOR` 宏构成双源真相
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:11`（`char path_separator`）vs `Re.KrtC/src/Core/Platform/Platform.h:150,166`（`KRT_PATH_SEPARATOR`/`_STR`）
- 类别：配置漂移
- 触发条件：任何同时使用两者的路径拼接代码（`Main.c:1041` 用 `KrtPlatformGetSeparator`，`ParallelCompiler.c:330-384` 用 `KRT_PATH_SEPARATOR_STR`）。
- 后果：同一路径分隔符存在两个独立定义点，新增平台或改宏时 `KrtPlatformGetSeparator` 不跟随；`KrtPlatformPathJoin` 内部用哪个也无从判断，拼接结果可能混用分隔符。
- 最小修复：`KrtPlatform.path_separator` 在 `KrtPlatformCreate` 中由 `KRT_PATH_SEPARATOR` 初始化（单一来源），或删除宏改用平台抽象。

### [中] 93-36｜PlatformAbstraction.h 反向依赖 Driver 层，且平台枚举命名分叉
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:5`（`#include "../Driver/ConfigManager.h"`）；枚举定义在 `Re.KrtC/src/Compiler/Driver/ConfigManager.h:12`（`KRT_CONFIG_PLATFORM_WINDOWS/LINUX/UNKNOWN`），而 `Platform.h:5-9` 用 `KRT_PLATFORM_WINDOWS/LINUX`
- 类别：阶段不对齐 / 配置漂移
- 触发条件：任何包含 PlatformAbstraction.h 的底层 TU；或新增平台时维护两套枚举名。
- 后果：平台抽象层（应为最底层）依赖 Driver/Config 层，层次倒置；同一概念两套枚举命名（`KRT_CONFIG_PLATFORM_*` vs `KRT_PLATFORM_*`），且 `KRT_PLATFORM_DEFAULT` 只在 `.c` 里本地 `#define`（PlatformAbstraction.c:12,17），头文件无法表达默认平台。
- 最小修复：把 `KrtPlatformType` 移到平台层头文件并统一命名，PlatformAbstraction.h 不再 include ConfigManager.h。

### [中] 93-37｜同族能力两种调用方式：结构体函数指针与自由函数混用
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:7-20`（`is_absolute_path/path_exists/is_directory/supports_color/set_utf8/execute_command` 指针）vs `PlatformAbstraction.h:25-29`（`KrtPlatformGetSeparator/KrtPlatformPathJoin` 自由函数）
- 类别：语义分叉
- 触发条件：扩展平台能力或新增调用方时。
- 后果：`path_exists` 走 `platform->path_exists`，`path_join` 走全局函数；`get_separator` 甚至只读字段而非指针。同类"平台能力"有三种访问方式（指针、自由函数、公开字段），新增能力时无可依据的约定，且 `KrtPlatform` 结构体可被调用方直接改写字段（无 const 保护）。
- 最小修复：统一为 vtable（含 `path_join`、`get_separator`），字段全部私有化（`const char* name` 已有 const，`path_separator` 也应只读）。

### [低] 93-38｜路径判定函数的返回域与 NULL 入参行为未文档化
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:26-28`（`KrtPlatformPathIsAbsolute/PathExists/PathIsDirectory`）
- 类别：契约破坏
- 触发条件：传入 NULL 或空串；调用方按 `bool` 或按 `== 1` 使用返回值。
- 后果：三个函数都返回 `int`，但"1/0"还是"非 0/0"、NULL 是否安全返回 0、空串是否算存在，头文件无任何约定；与 `Platform.h` 的 `KrtFileExists`/`KrtDirectoryExists`（同样返回 int、返回域还不一致，见 93-10）构成两套路径判定 API。
- 最小修复：统一为 `KrtPlatformPathExists` 单点实现（`Platform.h` 的 `KrtFileExists`/`KrtDirectoryExists` 与 Path.c 的 `KrtPathExists` 已三处重复），并在头文件写明返回值与 NULL 约定。

### [低] 93-39｜`PlatformAbstraction.h` 对 `size_t` 的依赖是隐式的
- 位置：`Re.KrtC/src/Compiler/Platform/PlatformAbstraction.h:4,29`（`size_t buffer_size`）
- 类别：契约破坏（低）
- 触发条件：仅包含 PlatformAbstraction.h 而不包含 KrtCommon.h 的 TU（当前靠 `../../Core/Utils/KrtCommon.h` 间接提供 `size_t`）。
- 后果：头文件未直接 `#include <stddef.h>`，`size_t` 的可见性依赖被包含头的传递；一旦 KrtCommon.h 调整包含顺序即编译失败，且错误信息指向无关文件。
- 最小修复：在 PlatformAbstraction.h 直接 `#include <stddef.h>`。
