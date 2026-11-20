# 第七十批：stdlib/{Console,Convert,Sys,Memory,Exception}.krt

## 覆盖范围

- 已用 read 工具全量读完（未通读其他源文件）：
  - `Re.KrtC/stdlib/Console.krt`（153 行）
  - `Re.KrtC/stdlib/Convert.krt`（173 行）
  - `Re.KrtC/stdlib/Sys.krt`（76 行）
  - `Re.KrtC/stdlib/Memory.krt`（154 行）
  - `Re.KrtC/stdlib/Exception.krt`（20 行）
  - 合计 576 行。
- libs/System 副本比对：本环境下 bash 沙箱不可用（workspace-write 无可用后端，danger-full-access 申请被拒），未能执行 `diff -q` / `md5sum`。改用定向 grep 比对命名空间、常量、全部 public/private 函数签名与关键逻辑行号：`libs/System/{Console,Convert,Sys,Memory,Exception}.krt` 与 stdlib 副本在 `Console.Write:46`/`WriteFormat:124`、`Convert.ToInt64:12`/`TryToInt64:53`、`Sys.Syscall:13`/`InternalStringLength:24`、`Memory.Allocate:9`/`TryMove:75`、`Exception:4-17` 等处逐行对齐，未发现语义分叉。建议后续用 `md5sum` 做全量复核。
- 另注：`Re.KrtC/src/Runtime/Krt/` 下存在同名但实现不同的另一层（如 `Memory.Allocate` 无 size 校验、`Convert` 无 `Try*`、`Console.write_raw` 无 EINTR 重试），该分叉由第七十一批（`audit_extract/cont_14_runtime.md`）覆盖，本批不重复计入。

## 问题条目

### [中] 70-1｜write 系统调用失败与部分写被静默丢弃
- 位置：`Re.KrtC/stdlib/Console.krt:16-25`（关键 20-22）
- 类别：1 静默失败 / 2 诊断缺失 / 9 错误处理不一致
- 触发条件：`write_raw` 中 `Sys.Syscall(1, ...)` 返回 0 或负 errno（如 EBADF、EPIPE、ENOSPC、EIO），或只写出部分字节后返回错误。
- 后果：`Write`/`WriteLine`/`WriteFormat` 等全部 public API 返回 `void`，调用方无法感知输出丢失或截断；程序继续执行并可能报告成功。
- 最小修复：让 public 写入 API 返回 `int64`（已写字节数或负 errno），或至少提供 `TryWrite` 系列；文档明确失败不可见。
- 备注：`write_raw` 本身返回 `count`，但所有调用点都丢弃了返回值。

### [低] 70-2｜write_raw 对非正长度静默返回 0
- 位置：`Re.KrtC/stdlib/Console.krt:11-13`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`length` 为负（私有辅助函数，当前调用点均为非负，但 API 本身无保护）。
- 后果：负长度被当作"写 0 字节成功"，与调用方预期不符，且无任何诊断。
- 最小修复：区分 `length == 0`（返回 0）与 `length < 0`（返回负值或断言）。
- 备注：当前 `WriteFormat` 的 `index - literal_start`、`length - literal_start` 恒非负，故为潜在而非现网触发。

### [低] 70-3｜EINTR 重试无上限
- 位置：`Re.KrtC/stdlib/Console.krt:17-19`
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：描述符持续返回 `-4`（EINTR），例如安装了无 SA_RESTART 的信号处理且信号风暴。
- 后果：`while (written < length)` 永久空转，进程挂起且无超时/重试计数。
- 最小修复：加入重试上限或退避，超限后返回已写字节数或负错误码。
- 备注：常规单次 EINTR 重试是正确的，问题在于无终止条件。

### [中] 70-4｜WriteFormat(null) 与 WriteLineFormat(null) 行为分叉
- 位置：`Re.KrtC/stdlib/Console.krt:125-127`、`Re.KrtC/stdlib/Console.krt:149-152`
- 类别：1 静默失败 / 9 错误处理不一致 / 13 注释与实现脱节
- 触发条件：`format == null`。
- 后果：`WriteFormat(null, arg)` 什么都不输出；`WriteLineFormat(null, arg)` 先走 `WriteFormat`（无输出）再 `WriteLine()`，输出一个换行。同族 API 对同一非法输入给出不同可见行为，且注释未说明 null 语义。
- 最小修复：统一为"null 即无输出"或"null 视为空串"，并同步注释。
- 备注：`WriteFormat` 的 null 分支无日志/断言。

### [低] 70-5｜STDIN 常量无任何读取实现
- 位置：`Re.KrtC/stdlib/Console.krt:8`
- 类别：5 死代码 / 12 测试不足
- 触发条件：调用方使用 `Console.STDIN` 期望存在输入 API。
- 后果：整个 stdlib 中 `STDIN` 仅此一处声明，没有任何 `Read*` 方法消费它，属未完成的公开 API；`STDERR` 同样只声明未使用（`Write` 系列硬编码 `STDOUT`）。
- 最小修复：补充 `Read`/`ReadLine` 与基于 `STDERR` 的写入，或删除未实现常量并在文档中标注。
- 备注：已 grep 整个 `Re.KrtC/stdlib`，`STDIN`/`Read` 无其他命中。

### [中] 70-6｜ToInt64 不接受 '+'，与 TryToInt64 语义分叉
- 位置：`Re.KrtC/stdlib/Convert.krt:22`（对比 `Re.KrtC/stdlib/Convert.krt:67`）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：输入形如 `"+5"`。
- 后果：`ToInt64("+5")` 在 `characters[0] == '-'` 判断后直接把 `'+'` 当非数字 `break`，返回 0；而 `TryToInt64("+5")` 接受 `'+'` 并返回 5。同一库内有符号解析规则不一致，调用方难以预期。
- 最小修复：`ToInt64` 同时接受 `'+'`/`'-'`，与 `TryToInt64` 对齐；或明确文档化差异。
- 备注：`ToInt32` 经由 `ToInt64`，同样受影响。

### [中] 70-7｜ToInt64/ToInt32 溢出静默回绕
- 位置：`Re.KrtC/stdlib/Convert.krt:31-34`、`Re.KrtC/stdlib/Convert.krt:7-9`
- 类别：1 静默失败 / 8 未检查前置条件 / 2 诊断缺失
- 触发条件：数字串超出 int64/int32 范围，如 `"99999999999999999999"`。
- 后果：`result = result * 10 + digit` 回绕，函数仍返回一个"看起来合法"的值，调用方无法区分溢出与正常解析；`ToInt32` 还叠加一次 int64→int32 截断。
- 最小修复：改用 `TryToInt64` 的负数累加上限检查，或让 `ToInt64` 在溢出时返回 0/抛异常并文档化。
- 备注：注释已写"wrapping arithmetic"，但 public API 无失败信号，仍是静默错误。

### [低] 70-8｜ToInt64 长度截断为 int32 未设上限
- 位置：`Re.KrtC/stdlib/Convert.krt:17`
- 类别：8 未检查前置条件 / 9 错误处理不一致
- 触发条件：字符串字节长度超过 `2147483647`。
- 后果：`(int32)Sys.InternalStringLength(value)` 回绕为负数或错误值，`while (index < length)` 直接不执行，返回 0，解析结果错误且无诊断。
- 最小修复：仿照 `TryToInt64:60`，先判断 `full_length > 2147483647` 再截断。
- 备注：`TryToInt64`/`TryToUInt64` 均有该上限检查，`ToInt64` 缺失。

### [致命] 70-9｜ToString 重载所有权不一致，统一释放会导致非法 munmap
- 位置：`Re.KrtC/stdlib/Convert.krt:150-172`
- 类别：6 契约破坏 / 10 资源与生命周期 / 9 错误处理不一致
- 触发条件：调用方按数值重载注释的统一约定，对 `Convert.ToString(bool)` 的返回值调用 `Memory.Free(result, StringOps.Length(result) + 1)`。
- 后果：数值重载返回 `Memory.Allocate`（mmap）所得堆串，必须释放；`ToString(bool)` 返回静态字面量 `"True"`/`"False"`，不可释放。对字面量地址调用 `Memory.Free` 时，若该地址恰好页对齐，`Memory.TryFree:52-55` 会通过页对齐检查并执行 `munmap`，可能解除可执行文件数据页的映射，导致后续访问崩溃；若未对齐则静默返回 false。
- 最小修复：统一所有权语义（全部返回堆分配，或全部返回字面量），或在 API 命名/文档中强制区分，并禁止对 bool 结果调用 `Memory.Free`。
- 备注：`ToString(bool):169-172` 已注明"borrowed boolean literal that must not be freed"，但与同名其他重载的所有权约定冲突，属接口级陷阱。

### [中] 70-10｜InternalStringLength 无界扫描 NUL，非终止串越界读
- 位置：`Re.KrtC/stdlib/Sys.krt:28-35`
- 类别：8 未检查前置条件 / 10 资源与生命周期
- 触发条件：传入非 NUL 结尾的字节缓冲（如外部内存、手工构造的 `string`、子范围指针）。
- 后果：`while (data[length] != 0) length++;` 会一直读出分配边界，直到撞到某个 0 字节或段错误；函数无长度参数，调用方无法前置校验。
- 最小修复：增加接受显式长度的重载，或在使用契约中强制 NUL 结尾并对来源做校验。
- 备注：文件头注释仅声明"Strings are zero-terminated byte sequences"，未提供检测手段。

### [低] 70-11｜InternalInt32ToString 与 StringOps.FromInt32/FromInt64 重复实现
- 位置：`Re.KrtC/stdlib/Sys.krt:39-70`
- 类别：5 死代码 / 11 配置漂移
- 触发条件：任一 int32→十进制串需求。
- 后果：同一功能存在两套实现，分配路径不同（此处直接 `syscall(SYS_mmap,...)`，`StringOps` 走 `Memory.Allocate`），缓冲区尺寸与失败处理各自维护；后续修改其一易造成行为漂移（如 `StringOps` 已处理 int64 最小值，此处只处理 int32）。
- 最小修复：让 `InternalInt32ToString` 直接转发 `StringOps.FromInt32`，或明确其独立契约并加一致性测试。
- 备注：`Sys.krt:39` 与 `String.krt:69-81` 功能重叠。

### [低] 70-12｜Syscall 对调用号与参数不做任何校验
- 位置：`Re.KrtC/stdlib/Sys.krt:13-16`
- 类别：8 未检查前置条件 / 2 诊断缺失
- 触发条件：传入非法系统调用号或错误参数个数/类型。
- 后果：内核返回 `-ENOSYS` 等负 errno，函数原样返回；调用方若按正数解释将得到无意义结果，且无诊断。
- 最小修复：至少对已知常量范围做断言，或在文档中明确"原语，调用方自负"。
- 备注：作为底层原语可接受，但 public 暴露后缺少使用约束说明。

### [中] 70-13｜Free 丢弃 TryFree 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:39-41`
- 类别：1 静默失败 / 9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`TryFree` 返回 false（非法范围、未页对齐、munmap 失败）。
- 后果：`Free` 返回 void，调用方无法知道释放失败，内存持续泄漏且无告警。
- 最小修复：让 `Free` 返回 `bool`，或提供失败日志/断言。
- 备注：与 `Copy`/`Move`/`Set`/`Clear` 是同一模式，见 70-14～70-17。

### [中] 70-14｜Copy 丢弃 TryCopy 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:60-62`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`length` 为负、范围为空指针或回绕。
- 后果：拷贝被跳过但调用方无感知，数据保持未初始化/旧值，可能被当作已拷贝使用。
- 最小修复：返回 `bool` 或断言。
- 备注：`TryCopy` 本身正确返回 false。

### [中] 70-15｜Move 丢弃 TryMove 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:70-72`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：同 70-14（负长度、null 范围、回绕）。
- 后果：重叠搬移被静默跳过，目标区保留错误内容。
- 最小修复：返回 `bool` 或断言。
- 备注：`TryMove` 的重叠方向处理本身正确。

### [中] 70-16｜Set 丢弃 TrySet 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:103-105`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`length` 为负或目标为 null（非零长度）。
- 后果：填充被静默跳过，缓冲区内容不符合预期。
- 最小修复：返回 `bool` 或断言。
- 备注：`Set` 先把 `char` 截为 `byte`，再丢失状态。

### [中] 70-17｜Clear 丢弃 TryClear 的成败状态
- 位置：`Re.KrtC/stdlib/Memory.krt:124-126`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：`length` 为负或目标为 null（非零长度）。
- 后果：清零被静默跳过，敏感数据可能残留（如密钥缓冲未清）。
- 最小修复：返回 `bool` 或断言。
- 备注：`TryClear` 转发 `TrySet`，状态在 `Clear` 处丢失。

### [低] 70-18｜Copy/Move/TryCopy 与 TryMove 完全等价，重复 API
- 位置：`Re.KrtC/stdlib/Memory.krt:60-72`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：调用方在 `Copy` 与 `Move` 之间选择。
- 后果：`TryCopy` 直接 `return TryMove(...)`，`Copy` 与 `Move` 语义、实现完全相同，仅名称不同；两套名字增加维护面与误用概率（调用方可能以为 `Copy` 不支持重叠）。
- 最小修复：合并为单一 `Move`/`TryMove`，或让 `Copy` 明确非重叠语义并做前置检查。
- 备注：注释称两者都"with overlap support"，证实为重复。

### [中] 70-19｜魔法上限 9223372036854771712 重复出现且注释未解释
- 位置：`Re.KrtC/stdlib/Memory.krt:11`、`Re.KrtC/stdlib/Memory.krt:46`
- 类别：13 注释与实现脱节 / 11 配置漂移
- 触发条件：阅读或维护 `Allocate`/`TryFree` 的尺寸上限。
- 后果：该值为 `2^63 - 4096`，两处各自硬编码；注释只写"overflow"，未说明推导依据。若任一处调整而另一处未同步，`Allocate` 成功而 `TryFree` 拒绝（或反之），造成静默释放失败。
- 最小修复：提取为命名常量（如 `MAX_MMAP_SIZE`）并注释推导过程。
- 备注：`Allocate` 用 `<=0 || >上限`，`TryFree` 用 `>上限`，语义不完全对称。

### [中] 70-20｜Memory 自定义 SYS_mmap/SYS_munmap，与 Sys.krt 重复
- 位置：`Re.KrtC/stdlib/Memory.krt:5-6`（对比 `Re.KrtC/stdlib/Sys.krt:7-8`）
- 类别：11 配置漂移 / 5 死代码
- 触发条件：平台系统调用号变更或修正时。
- 后果：同一常量在两文件各自定义，`Memory` 已 `using System.Sys` 却不复用 `Sys.SYS_mmap`/`SYS_munmap`；一旦只改一处即产生漂移，导致 mmap/munmap 用错号。
- 最小修复：删除 `Memory` 内常量，改用 `Sys.SYS_mmap`/`Sys.SYS_munmap`。
- 备注：`Memory` 其余逻辑已通过 `Sys.Syscall` 调用，常量却另立一份。

### [中] 70-21｜is_valid_range 对零长度直接放行，与 TryMove 注释矛盾
- 位置：`Re.KrtC/stdlib/Memory.krt:27-29`（对比 `Re.KrtC/stdlib/Memory.krt:74`）
- 类别：13 注释与实现脱节 / 6 契约破坏
- 触发条件：`length == 0` 且 `address == null`（或任意非法地址）。
- 后果：`is_valid_range` 在 null 检查之前返回 true，因此 `TrySet(null,v,0)`、`TryMove(null,null,0)`、`TryFree(null,0)` 均返回 true；而 `TryMove` 注释声称"null ranges ... fail"。契约描述与实际行为不符，调用方可能据此写出错误的前置判断。
- 最小修复：统一契约——要么把 `length == 0` 放在 null 检查之后，要么修改注释为"零长度恒成功（含 null 地址）"。
- 备注：`TryFree:49-51` 也会对 `size == 0` 直接返回 true。

### [低] 70-22｜TryFree 的页对齐前置条件未在 Allocate 契约中说明
- 位置：`Re.KrtC/stdlib/Memory.krt:52-54`（对比 `Re.KrtC/stdlib/Memory.krt:8`）
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：调用方对 `Allocate` 返回指针做偏移后调用 `Free`，或释放非 `Allocate` 来源的地址。
- 后果：`TryFree` 静默返回 false，内存未释放；`Allocate` 注释只说"Release an Allocate result"，未强调必须释放原始基址、不可偏移。
- 最小修复：在 `Allocate`/`Free` 注释中明确"必须释放原始基址，禁止偏移；地址必须页对齐"。
- 备注：`Free` 丢弃该 false（见 70-13），使契约违反完全不可见。

### [低] 70-23｜TryFree 不校验 size 与原始分配一致，部分 unmap 泄漏
- 位置：`Re.KrtC/stdlib/Memory.krt:44-57`
- 类别：6 契约破坏 / 10 资源与生命周期
- 触发条件：传入小于原始分配块的 `size`。
- 后果：`munmap(addr, size)` 只解除按 size 向上取整的部分页，剩余页仍映射，形成泄漏；函数仍返回 true。
- 最小修复：文档强制"size 必须与 Allocate 入参完全一致"，或让 `Allocate` 记录元数据以便校验。
- 备注：`Allocate` 未返回容量元数据，调用方只能靠自觉。

### [低] 70-24｜Compare 以 -2 为无效哨兵，与 Try* 的 bool 约定不一致
- 位置：`Re.KrtC/stdlib/Memory.krt:133-153`
- 类别：9 错误处理不一致 / 6 契约破坏
- 触发条件：调用方把 `Compare` 返回值当布尔或与非负比较混用。
- 后果：有效返回值域为 `-1/0/1`，无效为 `-2`，与同文件 `Try*` 系列的 `bool` 失败约定不同；`-2` 与合法负值相邻，易被误判为"left < right"。
- 最小修复：改为 `bool TryCompare(...)` 或返回三态枚举，并在注释中给出判别表。
- 备注：注释已写明 -2，但 API 形态与文件内其他错误报告方式不统一。

### [中] 70-25｜Exception 只带 Message/Code，无堆栈、内部异常与来源
- 位置：`Re.KrtC/stdlib/Exception.krt:4-20`
- 类别：2 诊断缺失 / 12 测试不足
- 触发条件：捕获异常后定位根因。
- 后果：payload 仅有 `Message` 与 `Code`，无调用栈、无 `InnerException`、无抛出点/线程信息；`catch` 后无法还原因果链，复杂程序几乎无法定位。
- 最小修复：增加 `InnerException`、抛出位置（文件/行/函数）或堆栈字段，并定义 `Code` 的取值规范。
- 备注：文件注释称"catch 收到同一对象"，但未说明任何诊断信息载体。

### [低] 70-26｜Message/Code 为 public 可变字段，异常状态可被篡改
- 位置：`Re.KrtC/stdlib/Exception.krt:5-6`、`Re.KrtC/stdlib/Exception.krt:17-19`
- 类别：7 共享可变状态 / 6 契约破坏
- 触发条件：`catch` 中或捕获后修改 `e.Message`/`e.Code`。
- 后果：异常对象被多个处理层级共享时，后置处理读到被篡改的消息/错误码；`ToString()` 依赖可变的 `Message`，输出不稳定。
- 最小修复：改为只读属性（构造后不可变），或至少提供只读访问器。
- 备注：注释明确"catch 收到同一对象"，故共享可变性会被实际放大。

### [低] 70-27｜Code 字段全库无读取方，单参构造不赋值
- 位置：`Re.KrtC/stdlib/Exception.krt:6`、`Re.KrtC/stdlib/Exception.krt:8-10`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：任何按 `Code` 分支的错误处理。
- 后果：`Code` 仅被双参构造写入，全 stdlib 无任何读取点；单参构造留下默认 0，调用方无法区分"未设置"与"错误码 0"。字段看似公开契约，实际未被使用。
- 最小修复：删除 `Code` 或在使用方（运行时/标准库）中消费它，并定义 0 的语义。
- 备注：已 grep `Re.KrtC/stdlib`，`Code` 仅出现在赋值与声明处。

### [低] 70-28｜ToString() 对 null Message 返回 null
- 位置：`Re.KrtC/stdlib/Exception.krt:17-19`
- 类别：1 静默失败 / 2 诊断缺失 / 13 注释与实现脱节
- 触发条件：`new Exception(null)` 后调用 `ToString()`，或捕获到 Message 为 null 的异常再打印。
- 后果：返回 null，调用方（如日志/Console）可能输出空或触发后续空引用；无默认文本、无诊断。
- 最小修复：Message 为 null 时返回固定文本（如 `"Exception"`）或空串，并文档化。
- 备注：构造函数未拒绝 null message，故该路径可达。
