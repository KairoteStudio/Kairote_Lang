# 第六十九批：stdlib/String.krt + Math.krt + StringBuilder.krt

## 覆盖范围

- 全量读取（分段）：
  - `/sdcard/KairoteLang/Re.KrtC/stdlib/String.krt`（407 行，1-140 / 141-280 / 281-407）
  - `/sdcard/KairoteLang/Re.KrtC/stdlib/Math.krt`（375 行，1-200 / 201-375）
  - `/sdcard/KairoteLang/Re.KrtC/stdlib/StringBuilder.krt`（271 行，1-140 / 141-271）
- 副本比对：本机 bash 不可用——默认 workspace-write 沙箱无可用后端，显式申请 danger-full-access 被拒绝（不得重试/绕过）。因此 `diff -q` / `md5sum` 未能执行。改用只读抽样核对 `libs/System/` 下三个同名副本：行数一致（407/375/271），头部、中部、尾部抽样文本与 stdlib 逐字一致，未发现不同步证据；但**无法据此出具 md5 一致性结论**，该结论缺项记于此处。
- 额外发现：`grep` 显示仓库里还存在第三套实现 `Re.KrtC/src/Runtime/Krt/{String,Math,StringBuilder}.krt}`，其签名结构与 stdlib 明显不同（见 69-1）。按授权限制未整文件通读这些副本，仅用 grep 取签名行作为分叉证据。
- 本批未读取任何测试文件，故「测试不足」不做无证据断言。

## 问题条目

### [致命] 69-1｜同仓库存在三套 stdlib 副本，API 与实现语义分叉
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:8,13,19,45,88,146,183,223,256`；`Re.KrtC/src/Runtime/Krt/Math.krt:8,11,18,49,57,71,89,107,121`；`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:11,15,30,54,64,71,84,98,132,153,159,180,206`（对比 `Re.KrtC/stdlib/String.krt:9`、`Math.krt:5`、`StringBuilder.krt:12`）
- 类别：语义分叉（多处副本/多套实现）、配置漂移、阶段不对齐
- 触发条件：编译/打包阶段选错切片，或两套副本同时进入构建；调用方按 stdlib 的 Try* 系列、`AppendInt64/AppendUInt64`、`FromUInt64`、`Trim*`、`IsNullOrWhiteSpace` 等 API 编码。
- 后果：src 版 Math 无 `TryAbs/TryClamp/TryPow/TrySqrt/TryLCM`、无 int64 版 `Pow/Sqrt/GCD/LCM`，且 `LCM` 用 `Abs(a / GCD(a, b) * b)` 走 int32 回绕路径；src 版 String 缺 `FromUInt64/IsNullOrEmpty/IsNullOrWhiteSpace/Trim*/ToUpper/ToLower` 并多一个 `ConcatMultiple`；src 版 StringBuilder 参数名漂移为 `bufferSize/currentLength`，缺 `AppendInt64/AppendUInt64`。编译期报未定义符号，或更糟：链接到旧实现后数值/字符串行为与 stdlib 文档承诺不一致且无任何告警。
- 最小修复：确定唯一权威副本（建议 `Re.KrtC/stdlib/`），删除或同步 `Re.KrtC/src/Runtime/Krt/` 下三个同名文件；在 CI 加 `diff -q`/md5 断言，禁止同名异文副本共存。
- 备注：受权限所限未能做全文 diff；以上为依据 grep 签名行的结构差异，已足以判定非同一实现。`libs/System/` 副本抽样与 stdlib 一致，但同样缺 md5 证据。

### [致命] 69-2｜FreeBuffer 的容量由调用方回传，错配即堆损坏；负容量静默跳过释放
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:33-39`（关键 35-37）
- 类别：未检查前置条件、资源与生命周期、契约破坏（容量约定）
- 触发条件：`FreeBuffer(buf, cap)` 的 `cap` 与 `AllocateBuffer(cap)` 时的容量不一致（含先扩容/复用缓冲区后）；或传入 `capacity < 0`。
- 后果：按错误长度调用 `Memory.Free`，轻则分配器元数据破坏、重则 heap 损坏；`capacity < 0` 时函数直接不释放，造成内存泄漏且无任何诊断。
- 最小修复：缓冲区头部自带容量/长度元数据，`FreeBuffer(buffer)` 单参即可；至少在文档与实现中强制 `capacity >= 0`，并对非法值返回错误码而非静默忽略。
- 备注：类注释第 9 行声称「invalid arguments ... return -1」，但 `FreeBuffer` 返回 void，注释与实现脱节（类别 13）。

### [致命] 69-3｜Remove 无 buffer_size 形参，current_length 无上界校验，可越界读写
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:174-192`（校验仅 176，写终止符 189）
- 类别：未检查前置条件（越界）、契约破坏
- 触发条件：调用方传入大于实际分配容量的 `current_length`（API 只要求调用方自持长度，函数无法验证）；或 `index+length` 接近长度上限。
- 后果：`Memory.Move((void*)(data + index), (void*)(data + tail_start), current_length - tail_start)` 从越界地址读并向越界区写；`data[result] = 0` 同样可能落在分配块外。
- 最小修复：与 `AppendString` 一致引入 `buffer_size`，用 `valid_buffer(buffer, buffer_size, current_length)` 校验；或提供带容量的重载。
- 备注：`Remove` 是全类中唯一完全丢弃容量维度的变更操作，与 `Insert/AppendString` 的契约不对称。

### [高] 69-4｜Replace 同样缺容量校验，且 `data[length] = 0` 可越过分配块
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:252-270`（校验 254，越界写点 267）
- 类别：未检查前置条件、契约破坏
- 触发条件：`length` 大于真实容量（调用方误传或缓冲区被复用过）；特别地当 `length > buffer_size` 时 `data[length] = 0` 写到终止符槽之后。
- 后果：越界读整个 `[0, length)` 区间并越界写 1 字节，静默破坏相邻堆数据。
- 最小修复：增加 `buffer_size` 形参并校验 `length <= buffer_size`；把终止符写入限定在容量内。
- 备注：与 69-3 同源（显式长度 API 不携带容量），但函数与写点独立，单独记录。

### [高] 69-5｜GCD 溢出静默回绕，且无 Try 版本可发现
- 位置：`Re.KrtC/stdlib/Math.krt:132-134`（int32）、`284-286`（int64）
- 类别：契约破坏（数值范围）、静默失败、未检查前置条件（溢出）
- 触发条件：int32 版 `a = b = -2147483648`（或任意 |gcd| = 2^31），数学结果 2147483648 超出 int32；`(int32)gcd_magnitude(...)` 直接回绕为 `-2147483648`。
- 后果：静默返回负的最小整数；调用方拿到的「最大公约数」既非正数也非正确值，且没有任何错误通道（GCD 没有 Try 重载）。
- 最小修复：返回 int64/uint64 量级，或提供 `TryGCD` 在幅度不可表示时返回 false；至少对负结果给出诊断。
- 备注：注释自认「unrepresentable positive magnitude wraps to the minimum integer」——属于已知但未阻断的静默错误。

### [高] 69-6｜LCM 固定宽度回绕，返回错误的数学结果
- 位置：`Re.KrtC/stdlib/Math.krt:137-144`（int32，回绕点 143）、`289-296`（int64，回绕点 295）
- 类别：契约破坏（数值范围）、静默失败
- 触发条件：最小公倍数超过声明宽度，例如 int32 的 `LCM(-2147483648, 1)`、int64 的 `LCM(-9223372036854775808, 2)`。
- 后果：uint64 乘积被 `(int32)/(int64)` 截断后再 `Abs`，得到回绕值（甚至负值），与注释所称 LCM 语义不符；只有 `TryLCM` 才能发现溢出。
- 最小修复：让 `LCM` 在溢出时走与 `TryLCM` 相同的阈值判断并返回可诊断的结果，或明确将其标记为 deprecated 并强制使用 `TryLCM`。
- 备注：`Abs((int32)(...))` 对回绕后的 INT_MIN 仍返回 INT_MIN，错误被二次掩盖。

### [高] 69-7｜Sqrt 对负数输入静默返回 0（负数开方）
- 位置：`Re.KrtC/stdlib/Math.krt:107-119`（int32，分支 108-110）、`259-271`（int64，分支 260-262）
- 类别：未检查前置条件（负数开方）、静默失败、诊断缺失
- 触发条件：任何 `value < 0`，包括 `-1`、`int32.MinValue`、`int64.MinValue`。
- 后果：返回 0，与「负数无实数平方根」相悖；调用方无法把「输入为负」与「输入为 0」区分开，后续计算继续传播错误。
- 最小修复：负数输入返回 NaN/错误码或断言；保留旧语义就必须在文档与调用点强制改用 `TrySqrt`，并在负数时给出告警。
- 备注：注释写明「negative inputs retain the legacy zero result」，说明是继承行为而非疏漏，但对调用方仍是无声错误。

### [中] 69-8｜Clamp 在 minimum > maximum 时静默返回 minimum
- 位置：`Re.KrtC/stdlib/Math.krt:43-51`（int32）、`199-207`（int64）
- 类别：未检查前置条件、静默失败
- 触发条件：调用方交换了上下界，如 `Clamp(5, 10, 0)`。
- 后果：函数不报错，直接返回 `minimum`（10），把非法区间当合法处理；同类已有 `TryClamp` 却未在 `Clamp` 内复用校验。
- 最小修复：`Clamp` 内部先判 `minimum > maximum`，返回可诊断结果；或在调试构建中断言。
- 备注：注释仅声明「minimum must not exceed maximum」，未说明违反时行为，契约不完整。

### [中] 69-9｜Pow 对负指数静默返回 1
- 位置：`Re.KrtC/stdlib/Math.krt:64-76`（int32）、`220-232`（int64）
- 类别：未检查前置条件、静默失败
- 触发条件：`exponent < 0`（数学上应为分数/未定义于整数域），以及任意溢出情形。
- 后果：`while (exponent > 0)` 直接跳过，返回 1；溢出则按声明宽度回绕，两者都不产生任何信号。
- 最小修复：负指数与溢出路径统一改为可诊断返回，或至少让文档/命名表明这是「整数幂的 legacy 截断语义」。
- 备注：`TryPow` 明确拒绝负指数，说明设计上承认该输入非法，但 `Pow` 未对齐。

### [中] 69-10｜Abs 对最小整数回绕，仅 TryAbs 能暴露
- 位置：`Re.KrtC/stdlib/Math.krt:7-12`（int32，`-value` 在 9）、`163-168`（int64，`-value` 在 165）
- 类别：未检查前置条件（溢出）、静默失败
- 触发条件：`value == int32.MinValue` 或 `int64.MinValue`。
- 后果：`-value` 按声明宽度回绕回自身，返回负数当作「绝对值」；调用方若不知晓 TryAbs 版本就会拿到错误结果。
- 最小修复：让 `Abs` 在最小值上走可诊断路径，或在注释中显式标注返回值可能为负并禁止在无检查场景使用。
- 备注：`LCM`（69-6）正依赖该回绕行为，错误会级联。

### [中] 69-11｜char 被静默截断为一个字节，造成数据损坏
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:60-70`（`(byte)value` 在 66）、`252-270`（`(byte)new_character` 在 262；`(int32)data[index]` 与 `(int32)old_character` 比较在 261）
- 类别：契约破坏（UTF-8/字符约定）、静默失败、诊断缺失
- 触发条件：传入码点 > 255 的 `char`，如 `'Ā'(0x100)`、CJK 字符；AppendChar 写入低 8 位，Replace 用被截断的码点匹配。
- 后果：写入字节与预期字符不符，且 Replace 的 `old_character` 也被截断，匹配目标随平台/编码漂移；全程无告警。
- 最小修复：明确 API 只接受 0-255 并在入口校验，或改为接收/写入完整码点的 UTF-8 序列。
- 备注：与 69-12 是同一「char 与 byte 语义混用」的两处独立表现。

### [中] 69-12｜String.IndexOf/LastIndexOf 用码点与原始字节比较，>255 永不命中且可能误命中
- 位置：`Re.KrtC/stdlib/String.krt:147-160`（比较 153）、`163-179`（比较 172）
- 类别：契约破坏（UTF-8 约定）、诊断缺失
- 触发条件：`character` 码点 > 255（如中文、`'Ā'`）；或搜索某多字节 UTF-8 字符的首/后续字节恰与某码点低 8 位相同。
- 后果：合法字符永远返回 -1（假阴性）；某些码点又可能与缓冲区内的单个字节相等而返回错误下标（假阳性）。
- 最小修复：把参数类型改为字节（`byte`）或明确「只按单字节匹配」并在 >255 时给出诊断。
- 备注：文件头注释只声明「indices count bytes」，未声明 char 参数按字节截断还是按码点比较，契约不清。

### [中] 69-13｜只读的显式长度 API 不校验 length 上界，可越界读
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:216-231`（IndexOf）、`234-249`（LastIndexOf）、`200-213`（BufferToString，拷贝点 209）
- 类别：未检查前置条件（越界）、契约破坏
- 触发条件：调用方传入的 `length` 大于缓冲区实际分配容量（这三个函数都没有 `buffer_size` 可校验）。
- 后果：按伪造长度扫描/拷贝，越界读堆数据；BufferToString 还会把越界内容复制进新分配串，扩大泄漏面。
- 最小修复：为只读 API 也增加容量形参并校验，或由缓冲区对象自身携带容量。
- 备注：与 69-3/69-4 构成同一「显式长度无容量维度」的系统性契约缺口，因函数独立而分条列出。

### [中] 69-14｜Substring 对越界 start/length 一律静默钳制，错误输入被吞
- 位置：`Re.KrtC/stdlib/String.krt:263-293`（钳制 272-283）
- 类别：静默失败、未检查前置条件（索引约定）、诊断缺失
- 触发条件：`start > source_length`、`start < 0`、`length < 0`、`length > source_length - start`。
- 后果：非法请求被转成合法的空串/短串返回，调用方无法察觉自己传错了参数；与常见「越界抛错」契约相反。
- 最小修复：保留钳制版本的同时提供严格版（越界返回 null 或错误码），并在注释中把钳制列为显式选择而非默认。
- 备注：注释确实写明 clamped，因此属于「有意设计但对调用方无声」，按零容忍标准记录。

### [中] 69-15｜TryPow 的 int32 与 int64 实现是两套独立算法，行为易漂移
- 位置：`Re.KrtC/stdlib/Math.krt:79-104`（int32 内联乘积检查，平方只查上界 95-98）对照 `235-256`（int64 复用 `multiply_checked`，338-358）
- 类别：语义分叉（多套实现）、维护风险
- 触发条件：未来修改溢出判定或指数循环时只改其中一处；两版对中间量 `base_value` 的平方时机相同但检查方式不同。
- 后果：同一数学函数在 32/64 位宽度下可能出现接受/拒绝边界不一致，且难以通过代码审阅发现。
- 最小修复：抽出一个泛型/共用 helper，两个宽度共用同一判定逻辑。
- 备注：当前两版结果等价（已逐分支核对 `multiply_checked` 四个象限含 INT64_MIN/-1 边界），但实现重复本身就是分叉源。

### [中] 69-16｜String 各查询 API 对 null 一律静默降级，无诊断
- 位置：`Re.KrtC/stdlib/String.krt:114-116`（Equals(null,null)=true）、`119-121`、`187-196`、`222-234`、`253-255`、`296-312`、`315-325`
- 类别：静默失败、错误处理不一致
- 触发条件：任何 null 实参：`Equals`/`Contains`/`StartsWith`/`EndsWith` 返回 false，`Compare` 返回 ±1，`IndexOf` 返回 -1。
- 后果：调用方把「参数为空」与「确实不包含/不相等」混为一谈；`Equals(null, null) == true` 与 `IsNullOrEmpty(null) == true` 叠加后，空引用可被当作合法相等值参与逻辑。
- 最小修复：为 null 提供独立错误信号（如返回可选类型或断言），或至少在文档中把 null 语义提升为强契约并配套静态检查。
- 备注：注释均写明 null 行为，属已知设计；但跨 API 的错误表达方式不统一（bool 用 false、int 用 -1、Compare 用序关系），符合「错误处理不一致」。

### [轻] 69-17｜Length 的 -1 哨兵在判空 API 中变成 false，无法区分「超大非法」与「正常非空」
- 位置：`Re.KrtC/stdlib/String.krt:11-17`（哨兵来源）、`119-121`（IsNullOrEmpty）、`128-144`（IsNullOrWhiteSpace 在 131-133 显式返回 false）
- 类别：契约破坏、诊断缺失
- 触发条件：`InternalStringLength(value) > int32.MaxValue`。
- 后果：函数族对同一非法状态给出 `false`，与 `Concat/Substring/Trim/ToUpper` 的 `null` 表达不一致，调用方必须额外记得判断 `Length() == -1` 才能识别。
- 最小修复：统一错误通道（例如单独的 `TryLength` 或异常/断言），或在文档中明确要求调用方先检查 `Length()`。
- 备注：`-1` 同时被 `IndexOf` 用作「未找到」，哨兵复用加剧歧义。

### [轻] 69-18｜Clear 忽略入参且不触碰缓冲区，命名与实现脱节
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:195-197`（另见类注释 10）
- 类别：死代码、注释与实现脱节
- 触发条件：调用 `Clear(current_length)` 期望清空缓冲区内容。
- 后果：函数体恒返回 0，参数完全未使用；缓冲区旧字节仍在原处，仅靠后续 mutation 覆盖终止符，名字 `Clear` 暗示的动作并未发生。
- 最小修复：要么真正写 `data[0] = 0`（需要 buffer 形参），要么改名/删除该重载。
- 备注：注释自称「legacy overload does not access the buffer」，属于已知遗留物，但公共 API 保留即构成误导。

### [轻] 69-19｜append_owned 用 Length 反推释放长度，Length 为 -1 时按 0 字节释放
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:72-81`（释放点 78，Length 被计算两次）
- 类别：资源与生命周期、死代码（当前不可达分支）
- 触发条件：若该私有函数将来被复用于长度超过 int32 的字符串；当前唯一调用方传入 `FromInt64/FromUInt64` 产物（最多 20 位），故不可达。
- 后果：`Memory.Free(ptr, 0)` 尺寸错误，分配器可能报错或破坏；同时 Length 二次调用浪费且与 AppendString 内的校验各算一次。
- 最小修复：让 `FromInt64/FromUInt64` 返回时带上长度，或用局部变量保存首次 `Length` 结果并校验 `< 0` 后提前返回。
- 备注：标记为死分支是为了避免夸大，但释放尺寸来源本身不可靠。

### [轻] 69-20｜AllocateBuffer 把「非法容量」与「分配失败」合并为返回 0，无法区分
- 位置：`Re.KrtC/stdlib/StringBuilder.krt:18-30`（20-22 与 24-26 都落到 0）
- 类别：错误处理不一致、诊断缺失
- 触发条件：`capacity < 0`，或 `Memory.Allocate` 返回 null。
- 后果：调用方拿到 0 后无法判断是参数错还是 OOM；随后的 `FreeBuffer(0, cap)` 被当作 null 忽略，错误被吞。
- 最小修复：非法容量走断言/错误码，与分配失败分开表达。
- 备注：与 69-2 的「容量需调用方回传」叠加后，错误定位链进一步变弱。

## 备注（非问题条目）

- `libs/System/` 与 `Re.KrtC/stdlib/` 三个同名文件抽样一致、行数一致，但因 bash/diff/md5sum 权限被拒，不能声明完全相同；建议后续批次在具备权限时补做逐字节比对。
- 本批未发现明确的「阶段不对齐」编译错误证据；69-1 的副本分叉已覆盖该风险面。