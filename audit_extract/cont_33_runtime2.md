# 第九十批：src/Runtime/Krt/{Math,Memory,Runtime,Sys}.krt

## 覆盖范围

- 已用 read 工具完整读取（全量）：
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Math.krt`（126 行）
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Memory.krt`（49 行）
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Runtime.krt`（4 行，仅注释）
  - `/sdcard/KairoteLang/Re.KrtC/src/Runtime/Krt/Sys.krt`（88 行）
- **比对情况说明**：本环境 bash 不可用（无沙箱后端，`workspace-write` 被拒、`danger-full-access` 升级被用户拒绝），因此任务要求的 `diff -q` **未能执行**。仅用 glob 列出 `Re.KrtC/stdlib/` 目录，确认存在同名文件 `Math.krt`、`Memory.krt`、`Sys.krt`，**不存在** `Runtime.krt`；按任务约束未读取这些 stdlib 文件内容，故语义分叉仅能给出「并存风险」级结论，无法逐行核对。
- 本次未通读任何其他源文件。

## 问题条目

### [致命] 90-1｜Abs(int32) 对 int32.MinValue 取负溢出，静默返回原负数
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:13`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`MathOps.Abs(int32.MinValue)`（即 -2147483648）。
- 后果：`-value` 溢出，结果仍为 -2147483648；调用方拿到「负的绝对值」，后续数组索引/尺寸计算全部错位，且无任何报错。
- 最小修复：入口判 `value == int32.MinValue` 时抛溢出异常或返回 int64；至少在注释中明确该输入未定义并加断言。
- 备注：与 90-2 是同源缺陷的两个重载，因签名不同单独记录。

### [致命] 90-2｜InternalInt32ToString 对 int32.MinValue 生成字符串 `"-"`，静默错误
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:63-64`（叠加 74-80）
- 类别：8 未检查前置条件（溢出）/ 1 静默失败 / 2 诊断缺失
- 触发条件：`Sys.InternalInt32ToString(int32.MinValue)`。
- 后果：`number = 0 - number` 溢出后仍为负，`while (number > 0)` 一次都不执行，只写入 `'-'` 后返回 `buffer + 10`，得到字符串 `"-"`；任何打印/日志/数值解析都得到错误结果且无异常。
- 最小修复：改用 `int64 number = 0 - (int64)value`（或先转 int64 再取负），并在函数入口对 MinValue 单独处理。
- 备注：缓冲区 12 字节恰好容纳 `-2147483648`，说明作者本意支持该值，属实现缺陷而非设计取舍。

### [致命] 90-3｜Memory.Allocate 把 mmap 错误码当作指针返回，失败静默
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:16`
- 类别：1 静默失败 / 8 未检查前置条件 / 9 错误处理不一致
- 触发条件：`syscall` 返回负数（mmap 失败，如 ENOMEM/EINVAL），`(void*)(-errno)` 被当作合法指针返回。
- 后果：调用方按「非 NULL 即成功」判断，拿到形如 `(void*)-22` 的伪指针；后续 `Memory.Copy/Set` 写入即野指针写，进程崩溃或静默损坏。
- 最小修复：`int64 r = Sys.syscall(...); if (r < 0) return null;`，与 `Sys.InternalInt32ToString:47` 的判负写法保持一致。
- 备注：同文件 `Sys.krt:47` 已用 `< 0` 判失败，两处错误处理不一致（类别 9）。

### [高] 90-4｜Abs(int64) 对 int64.MinValue 取负溢出
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:20`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`MathOps.Abs(int64.MinValue)`。
- 后果：同 90-1，结果仍为负数；该重载被 `GCD`/`LCM` 内部调用（Math.krt:109-110、124），错误会向数论函数传播。
- 最小修复：入口特判 MinValue；或在文档中声明仅支持 `value > MinValue` 并加断言。
- 备注：与 90-1 同源，因签名与调用链不同而独立记录。

### [高] 90-5｜InternalStringLength 无界扫描且对 null 静默返回 0
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:28-42`
- 类别：8 未检查前置条件（越界）/ 1 静默失败 / 6 契约破坏（编码约定）
- 触发条件：传入的 string 底层 `char[]` 未以 `'\0'` 结尾（或长度字段与内容不一致）时，`while (chars[(int32)length] != '\0')` 越界读；传入 null 时直接返回 0。
- 后果：越界读可致段错误或读到垃圾值；null 静默变 0 会把「未初始化」与「空串」混为一谈，掩盖上游缺陷。另外 `length` 为 int64 但下标强转 int32，超过 2^31 后下标回绕。
- 最小修复：使用字符串自带长度字段而非扫描终止符；null 单独抛异常或返回 -1；下标统一 int64 或先做范围校验。
- 备注：依赖「string 即 char[] 且 NUL 结尾」这一未文档化的编码约定，属类别 6。

### [高] 90-6｜InternalInt32ToString 每次调用泄漏 12 字节 mmap，从不 munmap
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:46`
- 类别：10 资源与生命周期
- 触发条件：每次调用 `InternalInt32ToString`（打印整数、日志、错误信息都会触发）。
- 后果：mmap 的 12 字节永不回收，长时间运行/循环打印时地址空间持续增长，最终 mmap 失败；而失败分支只返回 null（见 90-14）。
- 最小修复：改用栈上 `char[]`/固定缓冲，或让调用方传入缓冲；若必须 mmap，则提供配套释放或改用 brk 分配器统一管理。
- 备注：文件头注释自称「纯 .krt 实现，不依赖 C 运行时库」，但每次字符串化都依赖内核映射，代价被隐藏。

### [高] 90-7｜syscall 号硬编码 Linux x86_64，跨架构即整体失效
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:11-18`（`SYS_mmap`/`SYS_munmap` 亦被 `Memory.krt:10-11` 复制）
- 类别：6 契约破坏（ABI）/ 11 配置漂移
- 触发条件：在 aarch64（本项目环境为 Android/arm64）或 32 位目标上运行；arm64 的 read/write/mmap/munmap/exit 分别为 63/64/222/215/93。
- 后果：arm64 编号 9 是 `lgetxattr`、11 是 `getxattr`（非 munmap）、12 是 `fgetxattr`（非 brk），`Allocate`/`Free`/`InternalInt32ToString` 全部误调用，轻则 EINVAL 静默失败，重则按错误语义执行（如把内存当 xattr 操作）。
- 最小修复：按目标三元组提供常量表（条件编译或分文件），并在构建期断言架构；至少把「仅 x86_64」写入契约并在不支持时编译报错。
- 备注：`Memory.krt:10-11` 重复定义同一组常量，使修正必须改两处，见 90-19。

### [高] 90-8｜Memory.Copy 无 NULL/越界/重叠检查
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:26-36`
- 类别：8 未检查前置条件（NULL/越界）/ 7 共享可变状态
- 触发条件：`dest`/`src` 为 NULL 或重叠，或 `length` 超出任一侧实际分配。
- 后果：NULL 直接解引用崩溃；重叠时行为等同于未定义（非 memmove 语义），数据错乱；越界写破坏相邻堆/映射。
- 最小修复：入口校验 `dest != null && src != null && length >= 0`；重叠时改用反向复制或显式要求调用方保证不重叠并写入契约。
- 备注：参数 `length` 为 int32，负值会让循环静默不执行（见 90-18）。

### [高] 90-9｜Memory.Allocate 未校验 size（0 或负值）
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:14-16`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`Allocate(0)` 或 `Allocate(负数)`。
- 后果：mmap 对 size<=0 返回 EINVAL（-22），经 90-3 变成非空伪指针；调用方误以为分配成功，后续使用即野指针。
- 最小修复：`if (size <= 0) return null;` 并返回明确失败信号。
- 备注：与 90-3 叠加放大，建议一并修复。

### [高] 90-10｜runtime 层与 stdlib 同名文件并存，语义分叉风险（本次未能比对）
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt`、`Memory.krt`、`Sys.krt` 与 `Re.KrtC/stdlib/` 下同名文件
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：同一符号（如 `Abs`、`Allocate`、`syscall` 封装）在 runtime 与 stdlib 两条路径各有一份实现，编译期按文件列表选择其一。
- 后果：两处实现一旦修正不同步，同一程序在不同构建组合下行为不一致；本次因 bash 不可用未能执行 `diff -q` 与内容比对，无法确认当前是否已分叉。
- 最小修复：明确单一事实来源（runtime 提供底层、stdlib 仅薄封装并直接链接），或补 `diff -q` 回归比对纳入 CI。
- 备注：glob 确认 stdlib 有 `Math/Memory/Sys.krt` 而无 `Runtime.krt`；按任务约束未读取其内容，故不定级为已确认分叉。

### [中] 90-11｜Pow 对负指数静默返回 1
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:59`
- 类别：1 静默失败 / 8 未检查前置条件 / 2 诊断缺失
- 触发条件：`Pow(baseVal, exp)` 且 `exp < 0`（如 `Pow(2, -1)`）。
- 后果：返回 1，而非报错或 0；调用方无法区分「负指数」与「指数为 0」，数值结果静默错误。
- 最小修复：`if (exp < 0) { /* 抛异常或返回 0 并记录 */ }`，仅保留 `exp == 0` 返回 1。
- 备注：`exp <= 0` 把两种语义合并，属典型静默失败。

### [中] 90-12｜Pow 乘法溢出静默回绕
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:64`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`Pow(10, 10)` 等结果超出 int32 的输入。
- 后果：`result = result * baseVal` 回绕为任意值（可能为负），无提示；作为「整数幂」基础函数会污染所有调用方。
- 最小修复：使用 checked 乘法并在溢出时抛异常，或改用 int64 中间量后判范围。
- 备注：与 90-1/90-4 同属「整型溢出无检查」家族，但发生点独立。

### [中] 90-13｜Sqrt 对负数静默返回 0
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:73`
- 类别：8 未检查前置条件 / 1 静默失败 / 2 诊断缺失
- 触发条件：`Sqrt(value)` 且 `value < 0`（如 `Sqrt(-1)`）。
- 后果：返回 0，与 `Sqrt(0)` 无法区分；`IsPrime` 虽先判 `n < 2` 不受影响，但其它调用方会得到错误的 0。
- 最小修复：`if (value < 0) throw/返回 -1`；仅 `value == 0` 返回 0。
- 备注：注释「整数平方根」未声明定义域。

### [中] 90-14｜Clamp 未校验 min <= max
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:49-54`
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：`Clamp(v, min, max)` 且 `min > max`（如 `Clamp(5, 10, 0)`）。
- 后果：`value < min` 时返回 `min`，结果落在 `[min, max]` 之外，违反「限制值在范围内」的注释契约，且无任何提示。
- 最小修复：入口断言 `min <= max`，或定义并文档化反向区间的行为。
- 备注：注释与实现契约不符，兼具类别 13。

### [中] 90-15｜InternalInt32ToString 的 mmap 失败仅返回 null，无诊断
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:47-50`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：mmap 失败（返回负 errno）。
- 后果：静默返回 null，调用方既拿不到错误码也拿不到原因；与 90-3 的「返回伪指针」形成两种截然不同的失败表示。
- 最小修复：统一失败约定（始终返回 null 并暴露 errno/诊断信息），并在调用点检查 null。
- 备注：与 90-3 构成同一模块内错误表示不一致。

### [中] 90-16｜Memory.Free 忽略 munmap 返回值且无 NULL/长度校验
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:20-23`
- 类别：1 静默失败 / 8 未检查前置条件 / 10 资源与生命周期
- 触发条件：`Free(NULL, n)`、长度与分配时不一致、或重复释放。
- 后果：munmap 失败（EFAULT/EINVAL）被丢弃，内存未真正回收；长度不匹配会解除错误区间，破坏其它映射。
- 最小修复：检查 `ptr != null`，校验返回值，并把「分配尺寸必须回传」写入契约（最好由分配器记录尺寸而非要求调用方记忆）。
- 备注：`Allocate` 不返回尺寸句柄，`Free` 却要求尺寸，属接口契约脆弱（类别 6）。

### [中] 90-17｜Memory.Set 无 NULL 校验，负长度静默无操作
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:39-48`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`Set(NULL, v, n)` 或 `length < 0`。
- 后果：NULL 崩溃；负长度时 `while (i < length)` 直接不执行，返回后调用方以为已初始化，读到未初始化内存。
- 最小修复：入口校验 `dest != null && length >= 0`。
- 备注：与 90-8 同文件同风格缺陷，因函数独立单独记录。

### [中] 90-18｜Copy/Set 长度用 int32，与 Allocate 的 int64 宽度不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Memory.krt:26`、`39`（对比 `14`）
- 类别：6 契约破坏 / 9 错误处理不一致
- 触发条件：对大于 2GiB 的块调用 `Copy`/`Set`，或把 `Allocate` 的 int64 尺寸直接传入。
- 后果：尺寸被截断为 int32，复制/初始化只覆盖前一部分，剩余区域保持原样，静默数据不完整。
- 最小修复：统一为 int64，或在入口断言 `length` 可表示且与分配尺寸一致。
- 备注：同一模块内宽度不统一，是典型契约破坏。

### [中] 90-19｜SYS_mmap/SYS_munmap 常量在 Sys 与 Memory 重复定义
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:15-16` 与 `Re.KrtC/src/Runtime/Krt/Memory.krt:10-11`
- 类别：11 配置漂移 / 5 死代码
- 触发条件：需要按架构修正 syscall 号（见 90-7）时只改其中一处。
- 后果：两处常量各自演化，出现「Sys 用新号、Memory 用旧号」的分裂，分配与释放走不同 ABI，故障极难定位。
- 最小修复：删除 `Memory` 中的重复常量，统一引用 `Sys.SYS_mmap`/`Sys.SYS_munmap`。
- 备注：`Memory.Allocate` 已跨文件调用 `Sys.syscall`，复用常量无额外成本。

### [中] 90-20｜Runtime.krt 无任何实现，仅 4 行注释，属死文件
- 位置：`Re.KrtC/src/Runtime/Krt/Runtime.krt:1-4`
- 类别：5 死代码 / 12 测试不足
- 触发条件：编译时按文件列表把它当编译单元加入。
- 后果：空文件无 namespace、无类型声明，对产物零贡献；若构建脚本依赖目录通配或该文件存在性，纯属噪音，并误导后来者以为存在「运行时示例」实现。
- 最小修复：删除该文件，或补成可编译的真实示例/入口（含 `namespace System.Runtime;`）。
- 备注：glob 确认 stdlib 下无同名 `Runtime.krt`，无对照实现。

### [中] 90-21｜Runtime.krt 注释中的编译命令引用本目录不存在的文件
- 位置：`Re.KrtC/src/Runtime/Krt/Runtime.krt:3`
- 类别：13 注释与实现脱节 / 4 阶段不对齐
- 触发条件：按注释执行 `krtc build main.krt Sys.krt Memory.krt String.krt Math.krt Console.krt target exe`。
- 后果：`String.krt`、`Console.krt` 不在 `src/Runtime/Krt/`（本批目录仅 Math/Memory/Runtime/Sys），且 `target exe` 写法可疑；照抄命令必然失败，注释反而传播错误构建信息。
- 最小修复：改为指向真实路径与真实文件名，或删除该注释行。
- 备注：注释自称「运行时使用示例」却无任何示例代码，与实现完全脱节。

### [中] 90-22｜四个运行时文件均无测试或断言，边界行为无回归保护
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt`（全文）、`Memory.krt`（全文）、`Sys.krt`（全文）、`Runtime.krt`（全文）
- 类别：12 测试不足
- 触发条件：任何对 MinValue、0、负数、NULL、越界尺寸的调用均无测试覆盖。
- 后果：90-1/90-2/90-3/90-5/90-8/90-9/90-11/90-13 等缺陷不会被测试发现，回归无保障。
- 最小修复：为每个公开函数补边界用例（MinValue、0、负数、NULL、size=0/负、重叠 Copy），并纳入构建流程。
- 备注：仅声明本批四个文件范围内未发现测试文件，未通读其他目录。

### [低] 90-23｜LCM 中间乘法溢出静默回绕
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:124`
- 类别：8 未检查前置条件（溢出）/ 1 静默失败
- 触发条件：`LCM(a, b)` 结果超出 int32（如 `LCM(2147483647, 2)`）。
- 后果：`a / GCD(a,b) * b` 回绕为错误值（可能为负），无提示。
- 最小修复：用 int64 计算后判范围，或在溢出时抛异常。
- 备注：`a==0||b==0` 的特判正确；仅乘法宽度不足。

### [低] 90-24｜GCD 经 Abs 继承 MinValue 溢出缺陷
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:109-110`
- 类别：8 未检查前置条件 / 3 语义分叉
- 触发条件：`GCD(int32.MinValue, b)`，或 `GCD(a, int32.MinValue)`。
- 后果：`Abs` 内部溢出（90-1）使 `a` 保持负值；欧几里得循环对负被除数走 C# 截断取余，结果依赖实现且与「非负 GCD」契约不符。
- 最小修复：在 `GCD` 入口对 MinValue 特判，或用无溢出方式取绝对值（先转 int64）。
- 备注：缺陷根因在 90-1，但此处是独立传播点，故单列。

### [低] 90-25｜Pow 无 int64 重载，与 Abs/Max/Min 的重载策略不对称
- 位置：`Re.KrtC/src/Runtime/Krt/Math.krt:57`
- 类别：6 契约破坏（API 一致性）/ 9 错误处理不一致
- 触发条件：需要 int64 幂运算的调用方。
- 后果：调用方被迫把 int64 截断为 int32 再调用，静默丢精度；同类「二元/一元算术」中仅 Pow 缺 int64 版本。
- 最小修复：补 `Pow(int64, int64)` 重载，或明确文档化仅支持 int32 并说明溢出边界。
- 备注：接口一致性问题，不单独致错但易诱发误用。

### [低] 90-26｜Exit 忽略 syscall 返回值，失败静默
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:84-87`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`SYS_exit` 未能执行（理论上一旦成功不返回；若编号错误则可能落到其它 syscall）。
- 后果：无返回值可判断，失败时行为未定义；与同文件其它 syscall 封装同样不检查返回，风格一致但无诊断。
- 最小修复：文档化「不返回」，或在 debug 构建中断言编号与架构匹配。
- 备注：严重度低，因 exit 成功路径本就不返回。

### [低] 90-27｜InternalStringPtr 指针与 int64 强转无校验
- 位置：`Re.KrtC/src/Runtime/Krt/Sys.krt:23-26`
- 类别：6 契约破坏（编码约定）/ 8 未检查前置条件
- 触发条件：对 null 调用，或在 32 位/带指针压缩的目标上使用。
- 后果：null 静默变 0；指针↔整数往返依赖「指针即 int64」的未文档化约定，目标平台指针宽度变化时截断。
- 最小修复：文档化该约定并限定目标为 64 位；null 明确返回 0 并注释。
- 备注：与 90-5 同属「string/ptr 与底层表示强转」家族。

### [低] 90-28｜Runtime.krt 缺少 namespace 声明，与同目录文件不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Runtime.krt:1`
- 类别：4 阶段不对齐 / 6 契约破坏
- 触发条件：与 `Math.krt`/`Memory.krt`/`Sys.krt`（均声明 `namespace System.Runtime;`）一起编译时。
- 后果：文件级命名空间缺失，若其中出现类型声明会落入全局命名空间，与既有 `System.Runtime.*` 组织不一致，增加链接/解析歧义。
- 最小修复：补 `namespace System.Runtime;`，或按 90-20 直接删除该文件。
- 备注：当前文件无代码，仅属结构性隐患。
