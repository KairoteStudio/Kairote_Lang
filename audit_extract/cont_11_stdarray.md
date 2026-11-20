# 第六十八批：Re.KrtC/stdlib/Array.krt（含 libs/System 副本比对）

## 覆盖范围

- 审计对象：`/sdcard/KairoteLang/Re.KrtC/stdlib/Array.krt`，全量 1420 行（`ArrayOps` 静态类，覆盖 byte/int8/int16/uint16/int32/uint32/int64/uint64/int128/uint128/bool/float32/float64 共 13 种数组类型的 Length/Copy/Clear/Fill/Reverse/IndexOf/Contains）。
- 副本比对：`/sdcard/KairoteLang/libs/System/Array.krt` 全量 1420 行。
- 比对方法与限制：本会话 bash 沙箱后端不可用（`workspace-write` 无后端，`danger-full-access` 被自动拒绝），`mcp__phone__android_shell` 返回 `disabled`，因此无法执行 `md5sum`/`diff -q`。改用 read 工具对两文件做全量逐行比对（各分两段读完）：**两文件行数一致（1420 行）、逐行内容一致，未发现任何差异**。
- 比对结论：**两处副本当前同步，记为排除项（无问题）**；但这是人工逐行比对，非哈希校验，建议在具备 shell 的环境补做 `md5sum` 复核。
- 未读取范围：`System.Memory`（`Memory.TryMove`/`TryClear` 语义、参数顺序、原子性）与编译器堆头布局定义均不在允许读取范围内，相关判断已在条目中标注为「片外不可验证」。

## 问题条目

### [致命] 68-1｜null 指针被折叠为长度 0，对 null 的 Clear/Copy/Fill/Reverse 静默返回 true
- 位置：`Re.KrtC/stdlib/Array.krt:8-12`（`get_length` 对 `data == null` 返回 0）、`:70-74`、`:77-79`、`:95-97`、`:107-109`、`:126-128`、`:148-150`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：调用 `Clear(null)`、`Fill(null, v)`、`Reverse(null)`、`Copy(null, dst)`、`Copy(src, null)`（src 为空）等；`Length(null)` 同样返回 0。
- 后果：`get_length` 把「null 引用」与「长度 0 的合法数组」映射为同一返回值 0，随后 `valid_range(0,0,0)` 全部通过，函数返回 `true`。调用方得到「操作成功」的假信号，无法区分「什么都没做」与「已执行」，在自举标准库中会向上传播为静默错误结果（例如 `Clear(null)` 后继续读取依赖该数组的逻辑）。
- 最小修复：`get_length` 对 `data == null` 返回独立哨兵（如 `-2`）或让 `Length` 对 null 返回 -1；各 `Clear/Fill/Reverse/Copy` 在 `Length < 0`（含 null）时返回 `false`，使 null 与空数组可区分。
- 备注：文档注释仅声明 `Length` 对 null 返回 0，但未声明 `Clear/Copy/Fill/Reverse` 会把 null 当成功；契约与实现脱节。

### [致命] 68-2｜get_length 无数组头校验，对裸缓冲区/野指针读 data-8，静默错误长度或越界读崩溃
- 位置：`Re.KrtC/stdlib/Array.krt:13-27`
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：把 `Memory.Allocate` 得到的裸缓冲区、已释放数组、或任何非「编译器数组」指针当作 `byte[]`/`int32[]` 等传入 `Length`/`Copy`/`Clear`。
- 后果：`header[-1]` 直接解引用 `data - 8`，只校验 `total_size >= 16` 与整除性和 `count <= INT32_MAX`，不校验魔数/类型标记/归属。裸缓冲区的 `data-8` 是无意义值：轻则算出错误长度并返回「成功」，重则读到未映射页直接崩溃；文件头注释（`:5`）已承认裸缓冲区不可传入，但运行期无任何强制或检测。
- 最小修复：在堆头中加入并校验魔数/元素类型标记；`get_length` 校验失败时返回 -1 并让上层返回 false；对 `data` 做可达性/归属断言（若运行期支持）。
- 备注：这是编译器自举标准库，错误长度会直接污染编译产物。

### [高] 68-3｜失败信号语义分叉：bool / -1 / -2 三套约定，2 参 IndexOf 把「无效数组头」与「未找到」同编为 -1
- 位置：`Re.KrtC/stdlib/Array.krt:147-166`（对照 `:69-74`、`:94-104`）
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：无效数组头（`get_length` 返回 -1）时，`Length` 返回 -1、`Copy/Clear/Fill/Reverse` 返回 `false`、2 参 `IndexOf` 返回 -1、4 参 `IndexOf` 返回 -2。
- 后果：同一根因（数组头无效）在不同 API 得到三种不同信号；`IndexOf(values, value)` 的 -1 同时表示「未找到」和「数组无效」，调用方若按注释只区分 -1/-2 会误判；`Contains` 用 `>= 0` 掩盖了该区分。错误处理约定不统一，调用方无法写出一致的错误分支。
- 最小修复：统一错误码契约（如统一返回 -1 表示未找到、-2 表示参数/数组无效），并在全部 `IndexOf` 重载间保持一致；或统一改为返回 `bool` + 输出参数。
- 备注：注释 `:152` 只对 4 参重载声明了 -1/-2 语义，2 参重载注释（`:147`）未声明无效头情形。

### [高] 68-4｜全量 Copy 以源长度覆盖目标，目标容量仅靠 TryMove 失败返回值表达，原子性契约未断言
- 位置：`Re.KrtC/stdlib/Array.krt:76-79`、`:38-54`（`copy_range`）
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：`Copy(source, destination)` 中 `destination` 长度小于 `source` 长度（或 `Copy(source, destination, count)` 中 count 超出目标容量）。
- 后果：函数把 `Length(source)` 当作 count 传入 `copy_range`，仅在 `Memory.TryMove` 返回 false 时报错。注释声称「false leaves destination unchanged」，但片内没有任何机制保证 `TryMove` 失败时零写入（部分写入后返回 false 则目标已被污染）；同时目标容量是隐式推断的，调用方无法在调用前断言。目标长于源时返回 true 但尾部保持原值，属未文档化的部分写入语义。
- 最小修复：在 `Copy` 入口显式断言 `Length(destination) >= Length(source)`（全量重载）或 `destination_index + count <= Length(destination)`，不满足直接返回 false；在片内或 Memory 契约中明确 `TryMove` 失败的原子性并加断言。
- 备注：`Memory.TryMove/TryClear` 的参数顺序与失败语义在允许读取范围外，本条基于 Array.krt 自身契约缺口成立。

### [中] 68-5｜bool/float 数组使用 `(void*)(int64)` 强转，整数数组使用 `(void*)` 直转，data-8 头布局假设未对齐验证
- 位置：`Re.KrtC/stdlib/Array.krt:1110-1112`（bool）、`:1214-1216`（float32）、`:1318-1320`（float64），对照 `:70-72`、`:486-490`、`:694-698`、`:902-906`
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：对 `bool[]`/`float32[]`/`float64[]` 调用 `Length`/`Copy`/`Clear` 等走 `get_length` 的路径。
- 后果：同一 `get_length` 对全部类型假设「分配字节数在 data-8、堆头 16 字节」，但三类浮点/布尔类型需要经 `int64` 中转才能转 `void*`，与整数数组的直接转换风格不一致，暗示其指针表示或头布局可能不同（如引用类型/装箱表示）。若布局不同，`header[-1]` 读到的不是分配字节数，`Length` 会静默返回错误值，进而使范围校验全部错位。
- 最小修复：统一转换写法并在注释中记录每类数组的头布局来源；若 bool/float 数组确实使用不同表示，需为 `get_length` 增加按元素类型的头解析分支并补充断言。
- 备注：本条为表示层不一致引发的潜在语义分叉，需在可读 `System.Memory`/编译器头定义时确认；片内无法证伪。

### [中] 68-6｜缺少 uint8（及 char/float16 等）重载，标准整数数组 API 覆盖存在缺口
- 位置：`Re.KrtC/stdlib/Array.krt:70-1420`（全文件仅有 byte/int8/int16/uint16/int32/uint32/int64/uint64/int128/uint128）
- 类别：5 死代码（API 缺口）/ 12 测试不足
- 触发条件：调用方需要 `uint8[]`（若 Krt 中 `uint8` 与 `byte` 为不同别名/不同类型）、`char[]`、`float16[]` 的数组操作时。
- 后果：文件头注释（`:4`）声称覆盖「standard integers」，但无 `uint8` 命名重载；若 `byte` 与 `uint8` 在类型系统中等价则无实际影响，若不等价则调用方只能拿到错误类型的 API 或被迫做不安全转换，且无编译期报错。
- 最小修复：确认 `byte` 与 `uint8` 是否同一类型；若不同则补齐 `uint8`（及实际存在的 `char`/`float16`）全套重载，或在注释中显式声明 `byte` 即 `uint8`。
- 备注：片内无法确认类型别名关系，需结合编译器类型定义核实。

### [中] 68-7｜诊断缺失：null、空数组、头损坏、范围越界四种失败不可区分，标准库零诊断
- 位置：`Re.KrtC/stdlib/Array.krt:31-36`（`valid_range` 单一 false）、`:43-46`、`:58-60`、`:113-115`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：任何一次失败调用（范围越界、头无效、null、容量不足）。
- 后果：所有失败路径只产出 `false`/`-1`/`-2`，不携带任何原因信息，也无断言/日志/错误码细分。在自举编译器场景中，标准库失败会被编译产物静默继承，排查时无法从返回值定位是调用方越界还是数组损坏。
- 最小修复：为失败原因定义可区分错误码（如 0=OK，1= null，2=无效头，3=范围越界，4=容量不足），或至少在 `assert`/诊断通道输出失败原因；保持返回值契约兼容。
- 备注：与 68-1、68-3 互为表里，但本条聚焦「无任何诊断载体」这一独立缺陷。

### [低] 68-8｜注释与实现脱节：Copy/Clear 注释宣称的语义与 null、部分填充行为不符
- 位置：`Re.KrtC/stdlib/Array.krt:76-79`（Copy 全量）、`:94-97`（Clear 全量）
- 类别：13 注释与实现脱节
- 触发条件：目标长于源时执行 `Copy(source, destination)`；对 null 执行 `Clear(values)`。
- 后果：注释称 Copy「Copy all source elements ... false leaves destination unchanged」，未说明目标剩余尾部保留原值（部分写入）以及 `Copy(null, dst)` 返回 true；注释称 Clear「reporting whether the array header is valid」，但 null 也返回 true。调用方按注释编码会得出错误预期。
- 最小修复：更新注释为精确契约（目标尾部不变、null 视为长度 0 且返回 true、无效头返回 false），或按 68-1 修复后同步注释。
- 备注：与 68-1 同源，但作为文档一致性独立记录。

### [低] 68-9｜测试不足：片内无契约测试，null/零长度/重叠/失败原子性均无覆盖证据
- 位置：`Re.KrtC/stdlib/Array.krt:1-1420`（全文件无任何测试或示例调用）
- 类别：12 测试不足
- 触发条件：审计与回归验证时。
- 后果：`TryMove` 重叠正确性、失败时目标不变、`count == 0`、`start == length`、null、无效头、`IndexOf` 的 -1/-2 区分均只有注释声明，无片内测试固化；后续改动无法防止回归，尤其 68-4 的原子性契约无任何验证手段。
- 最小修复：为该标准库补一组契约测试，至少覆盖 null、空数组、零 count、start==length、源/目标重叠、目标容量不足、无效头哨兵。
- 备注：测试可能位于允许读取范围外的测试工程；若存在，本条可降级，但本切片内无据可查。

### [低] 68-10｜未固定数组即取裸指针做内存拷贝，若 GC 可移动对象则存在悬垂/损坏风险
- 位置：`Re.KrtC/stdlib/Array.krt:40-53`（`copy_range`）、`:57-66`（`clear_range`）、`:8-29`（读头）
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：在 `unsafe(using krt.mem;)` 块内持有 `void*` 期间发生 GC（尤其压缩/整理式 GC）。
- 后果：数组以裸指针进入不安全内存操作，片内未见 pin/固定或 GC 协作（如 `GC.KeepAlive`、句柄化）。若 Krt 运行期使用可移动 GC，`TryMove`/`TryClear` 执行中对象被搬移将导致写到旧地址，静默数据损坏；若 GC 不可移动则无影响。
- 最小修复：确认 Krt GC 是否压缩；若压缩，需在操作期间固定数组（pin）或使用 GC 句柄；若不可移动，在注释中明确记录该前提。
- 备注：片外不可验证，基于片内「无固定措施」这一事实记录，供 GC 语义确认后定性。

## 排除项

- 两处副本同步：`Re.KrtC/stdlib/Array.krt` 与 `libs/System/Array.krt` 均为 1420 行，全量逐行比对未发现差异，**不记语义分叉问题**。受限于 bash/Android shell 不可用，未做 `md5sum` 哈希级复核（见「覆盖范围」）。
- `valid_range` 的 `length - start` 与调用方 `start + count`：在 `start >= 0 && count >= 0 && count <= length - start` 成立时不会溢出，未发现整数溢出路径，不记问题。
- `element_size` 除零：仅由片内常量（1/2/4/8/16）调用，公开 API 无法传入 0，不记问题。
