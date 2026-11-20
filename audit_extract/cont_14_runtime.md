# 第七十一批：src/Runtime/Krt/{String,StringBuilder,Console,Convert}.krt

## 覆盖范围

- 全量读取（read 工具分段读毕，非抽样）：
  - `Re.KrtC/src/Runtime/Krt/String.krt`（303 行，全文读完）
  - `Re.KrtC/src/Runtime/Krt/StringBuilder.krt`（222 行，全文读完）
  - `Re.KrtC/src/Runtime/Krt/Console.krt`（152 行，全文读完）
  - `Re.KrtC/src/Runtime/Krt/Convert.krt`（90 行，全文读完）
- 同名副本存在性已确认：`Re.KrtC/stdlib/{String,StringBuilder,Console,Convert}.krt` 与 `libs/System/{String,StringBuilder,Console,Convert}.krt` 均有同名文件。
- **副本比对未完成**：本环境 `bash` 因无可用沙箱后端被拒（workspace-write 不可用），`mcp__phone__android_shell` 返回 `disabled`，无法执行 `diff -q`。因此「runtime 实现 vs 标准库封装」的逐字同步性未验证，仅能从本批文件内部的语义分叉（类别 3/4/11）记录。副本不同步风险需在具备 shell 的环境补做。
- 未读取上述四个文件以外的任何源文件内容（遵守审计边界）。

## 问题条目

### [致命] 71-1｜FromInt64 对 int64.MinValue 返回 "-"
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:100-104`、`115-137`
- 类别：8 未检查前置条件（数值溢出）＋ 1 静默失败
- 触发条件：调用 `StringOps.FromInt64(-9223372036854775808)`，或 `Console.WriteLine(int64.MinValue)`（`Console.krt:48-52` 经此路径）。
- 后果：`v = -v` 对最小负数溢出后仍为负，`while (temp > 0)` 得 `digits = 0`，最终只写入 `'-'` 与 `'\0'`，返回字符串 `"-"`。输出错误且无任何诊断；`Console.WriteLine(int64.MinValue)` 打印 `-`。
- 最小修复：在取负前对 `value == -9223372036854775808` 特判（返回常量 `"-9223372036854775808"`），或改用无符号幅值 `uint64` 迭代。
- 备注：`FromInt32` 无此问题（委托内置实现），见 71-9 的分叉。

### [高] 71-2｜Concat 未检查 Memory.Allocate 返回值
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:29-41`
- 类别：10 资源与生命周期 ＋ 8 未检查前置条件
- 触发条件：`totalLen + 1` 较大或堆耗尽，`Memory.Allocate` 返回 0/负值。
- 后果：未判空即 `Memory.Copy((void*)buf, ...)` 与 `arr[totalLen] = '\0'`，向空指针写入，崩溃或静默内存破坏。
- 最小修复：分配后检查 `buf == 0`（或 `< 0`），失败时返回 `null`/触发 OOM 路径，并明确契约。
- 备注：同类问题见 71-3、71-18、71-20。

### [高] 71-3｜ConcatMultiple 未检查 Memory.Allocate 返回值
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:62-82`
- 类别：10 资源与生命周期 ＋ 1 静默失败
- 触发条件：`parts` 中字符串总长度较大或堆耗尽。
- 后果：未判空即复制并写 `arr[totalLen]`，空指针写入；无诊断。
- 最小修复：同 71-2，分配失败显式返回 `null` 或抛出/上报。
- 备注：与 71-2 是不同函数，独立记录。

### [高] 71-4｜Concat 总长度 int32 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:24-29`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`lenA + lenB > 2147483647`（两个接近上限的字符串）。
- 后果：`totalLen` 回绕为负或变小，`totalLen + 1` 分配不足，随后按真实长度复制导致堆越界写。
- 最小修复：用 `int64` 累加并检查上限后再分配，或明确拒绝超长输入。
- 备注：与 71-2 的「未判空」是不同缺陷（一个是溢出、一个是判空）。

### [高] 71-5｜ConcatMultiple 总长度 int32 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:50-62`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`parts` 中多个字符串长度之和超过 `int32` 上限。
- 后果：`totalLen` 回绕，分配 `totalLen + 1` 偏小，循环复制越界写。
- 最小修复：累加用 `int64`，溢出即失败。
- 备注：与 71-4 独立（不同函数）。

### [高] 71-6｜Substring(start,length) 的 start+length 溢出导致越界拷贝
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:240-243`
- 类别：8 未检查前置条件（溢出/NULL/越界）
- 触发条件：`Substring(s, start, length)` 中 `length` 为大正数（如 `int32.MaxValue`）且 `start > 0`，`start + length` 回绕为负。
- 后果：`if (start + length > len)` 判定失效，`length` 不被收敛，`Memory.Copy` 按巨大长度读取/写入，堆越界。
- 最小修复：改为 `if (length > len - start) length = len - start;`，并单独校验 `start`/`length` 非负。
- 备注：`len - start` 在 `start < len` 时已保证非负（238 行已拦截 `start >= len`）。

### [中] 71-7｜Length(null) 未做前置检查，与同类函数 null 处理不一致
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:13-16`
- 类别：8 未检查前置条件 ＋ 9 错误处理不一致
- 触发条件：`StringOps.Length(null)`（`Convert`、`Console` 均经由它间接调用）。
- 后果：直接调用 `Sys.InternalStringLength(null)`，行为未定义/崩溃；而 `IndexOf`（185）、`LastIndexOf`（204）、`Compare`（148-150）都显式处理了 null。
- 最小修复：`Length` 内对 `s == null` 返回 0 或明确抛错，并统一全文件 null 契约。
- 备注：`Convert.ToInt32` 自身先判 null（`Convert.krt:13`），但其他调用方不受保护。

### [中] 71-8｜Substring(null,...) 返回 null，与空串返回 "" 契约不一致
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:225`、`234`
- 类别：1 静默失败 ＋ 9 错误处理不一致
- 触发条件：`Substring(null, 0)` 与 `Substring("", 0)` 对比。
- 后果：前者返回 `null`，后者返回 `""`；调用方若假设「越界即空串」会对 null 解引用。两种失败（null 输入、越界）返回不同哨兵，语义未声明。
- 最小修复：统一返回 `""`（或统一返回 `null`），并在注释中写明契约。
- 备注：`Substring(s, start)` 与 `Substring(s, start, length)` 两处行为一致地返回 null，但与其他函数不一致。

### [中] 71-9｜FromInt32 委托内置、FromInt64 自行实现，阶段/语义分叉
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:88-91` 对比 `94-140`
- 类别：3 语义分叉 ＋ 4 阶段不对齐 ＋ 5 死代码风险
- 触发条件：任何 `FromInt32` 调用；或对比 `FromInt32` 与 `FromInt64` 的边界行为。
- 后果：同一类中 int32 走 `Sys.InternalInt32ToString`（黑盒，边界/负数行为未知），int64 走纯 .krt 实现（存在 71-1 缺陷）。两者对 `MinValue`、`0`、格式的处理可能不一致，且维护时易只改一处。
- 最小修复：统一实现路径（都委托内置，或都自实现并共用同一算法），并让 `FromInt32` 复用 `FromInt64` 后截断/校验。
- 备注：`Console.Write(int32)`（`Console.krt:36-40`）走内置路径，`Console.Write(int64)` 走自实现路径，输出行为可能不同。

### [中] 71-10｜Compare 以 len1-len2 返回，存在溢出且语义未定义
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:171`
- 类别：8 未检查前置条件（溢出）＋ 13 注释与实现脱节
- 触发条件：两字符串长度差超过 `int32` 上限（极端长串），或长度相等前缀但后续不同。
- 后果：`len1 - len2` 回绕，返回值符号与注释「<0 = s1 < s2」矛盾；调用方按符号判断会误判大小。
- 最小修复：返回 `len1 < len2 ? -1 : 1`，避免减法溢出。
- 备注：常规路径（字符不等）返回 `(int32)arr1[i] - (int32)arr2[i]` 同样可能溢出，但字符差值范围有限，风险低。

### [中] 71-11｜字节分配与 char[] 索引混用，编码/ABI 约定未在本文件确立
- 位置：`Re.KrtC/src/Runtime/Krt/String.krt:29,38-39,62,79-80,116,242,245-246`
- 类别：6 契约破坏（C 运行时 ABI/编码约定）
- 触发条件：若 `char` 为 2 字节（宽字符/UTF-16 存储），而 `Memory.Allocate(totalLen + 1)` 按「字符数 + 1」分配字节。
- 后果：`arr[totalLen] = '\0'` 会写到第 `2*totalLen` 字节，越过 `totalLen + 1` 的分配边界（堆越界写）；同时 `Memory.Copy(..., lenA)` 的字节长度与 `char[]` 元素宽度不匹配。
- 最小修复：在文件头或 ABI 约定中明确 `char` 宽度与字符串编码；分配按 `sizeof(char) * (len + 1)`，或统一使用 `Sys` 提供的字符串构造接口。
- 备注：若 `char` 确为 1 字节且字符串为 UTF-8，则本条不成立；但四个文件均未写明该约定，属「契约未确立」，需与编译器 ABI 核对。

### [致命] 71-12｜AppendLine 部分写入后返回 -1，状态与返回值不一致
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:84-89`
- 类别：9 错误处理不一致 ＋ 1 静默失败
- 触发条件：缓冲区剩余容量足够容纳 `value` 但不够再容纳 `'\n'`（`AppendString` 成功、`AppendChar` 返回 -1）。
- 后果：`value` 的字符已写入缓冲区，但函数返回 `-1`；调用方若按「-1 = 未写入」处理会重复追加，若忽略则丢换行。缓冲区内容与调用方认知的长度不一致。
- 最小修复：先检查 `currentLength + valueLen + 1 <= bufferSize` 再写入；或返回实际新长度并单独标记失败。
- 备注：与 71-13（仅返回 -1 不报因）独立。

### [高] 71-13｜AppendString 容量不足静默返回 -1，无诊断
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:37-38`
- 类别：1 静默失败 ＋ 2 诊断缺失
- 触发条件：`currentLength + valueLen > bufferSize`。
- 后果：数据未追加，仅返回 `-1`；调用方若不检查（常见）则静默丢数据，且无法区分「容量不足」与「其他错误」。
- 最小修复：提供 `bool TryAppend` 或抛出/上报容量不足；至少在注释与调用约定中强制检查返回值。
- 备注：`Insert`（107）有相同模式。

### [高] 71-14｜AppendString 的 currentLength + valueLen 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:37`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`currentLength` 与 `valueLen` 均接近 `int32` 上限，相加回绕为负。
- 后果：`> bufferSize` 判定失效，容量检查被绕过，循环写 `dst[currentLength + i]` 越界。
- 最小修复：用 `int64` 计算或先判 `valueLen > bufferSize - currentLength`。
- 备注：与 71-13 独立（溢出 vs 静默返回）。

### [高] 71-15｜Insert 的 currentLength + valueLen 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:107`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`currentLength + valueLen` 回绕。
- 后果：容量检查失效，随后 `buf[i + valueLen] = buf[i]`（114）与 `buf[index + i]`（124）越界读写。
- 最小修复：同 71-14，改用减法比较或 `int64`。
- 备注：与 71-14 独立（不同函数）。

### [高] 71-16｜IndexOf/LastIndexOf/Replace 无 buffer 与 length 前置检查
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:182-189,195-203,208-219`
- 类别：8 未检查前置条件（NULL/越界）
- 触发条件：`buffer == 0`，或 `length` 大于实际分配长度（调用方传入错误）。
- 后果：直接 `char[] buf = (char[])buffer` 后按 `length` 读写，空指针解引用或越界读；`Replace` 还会越界写。
- 最小修复：入口检查 `buffer != 0 && length >= 0`，并由调用方/结构体保证 `length <= 容量`；或把长度与容量绑定在同一对象中。
- 备注：`AppendString` 有 `bufferSize` 参数，但这三个函数没有，契约不统一。

### [高] 71-17｜Remove 的 index + length 溢出
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:136`
- 类别：8 未检查前置条件（溢出）
- 触发条件：`index` 与 `length` 均较大，`index + length` 回绕为负。
- 后果：`if (index + length > currentLength)` 判定失效，`length` 不收敛，随后 `buf[i - length] = buf[i]`（145）中 `i - length` 下溢/越界写。
- 最小修复：改为 `if (length > currentLength - index) length = currentLength - index;`。
- 备注：与 71-6 是同型缺陷在不同文件，独立记录。

### [中] 71-18｜AllocateBuffer 未检查 mmap 返回值，且 capacity+1 可溢出/为负
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:15-19`
- 类别：8 未检查前置条件 ＋ 10 资源与生命周期
- 触发条件：`capacity` 为负或接近 `int32` 上限（`capacity + 1` 溢出/为负），或 mmap 失败返回负值。
- 后果：返回未校验的 syscall 结果；调用方拿到无效指针后写入，崩溃或静默破坏；`capacity` 为负时 `(int64)(capacity + 1)` 申请 0/负长度。
- 最小修复：检查 `capacity >= 0` 且 `capacity < int32.MaxValue`，并检查返回值 `< 0` 时报错。
- 备注：`FreeBuffer`（22-25）用同一表达式，负 capacity 同样失真。

### [中] 71-19｜FreeBuffer 长度依赖调用方回传，生命周期契约脆弱
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:22-25`
- 类别：10 资源与生命周期 ＋ 7 共享可变状态
- 触发条件：调用方传入与分配时不同的 `capacity`（如扩容后忘记更新，或结构体字段被复制）。
- 后果：`munmap(buffer, capacity + 1)` 长度与分配长度不符，返回 EINVAL 或只解除部分映射，造成内存泄漏；且缓冲区指针可被多处持有（裸 `int64` 传递），释放后其他持有者悬垂。
- 最小修复：把指针、容量、长度封装为带所有权的对象，或用头部记录分配长度，`Free` 时读取真实长度；禁止裸指针跨函数传递。
- 备注：`BufferToString`（164）新分配的字符串无任何释放接口，同一所有权缺口。

### [中] 71-20｜BufferToString 未检查分配返回值且 length 无界
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:159-177`
- 类别：8 未检查前置条件 ＋ 10 资源与生命周期
- 触发条件：mmap 失败，或调用方传入超过实际缓冲区长度的 `length`。
- 后果：未判空即 `dst[i] = src[i]` 与 `dst[length] = '\0'`，空指针写；`length` 过大则读越界。
- 最小修复：检查 `newBuf` 返回值，并校验 `length` 与源缓冲区容量的关系。
- 备注：`length == 0` 时返回 `""` 字面量而非新分配，与其他路径返回堆指针的所有权语义不一致。

### [中] 71-21｜与 String.krt 的内存分配器分叉
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:15-25,164` 对比 `String.krt:29,62,116,242`
- 类别：3 语义分叉 ＋ 11 配置漂移
- 触发条件：同一 runtime 内混用两套分配方式（本文件裸 `Sys.syscall(9,...)`，String.krt 用 `Memory.Allocate`）。
- 后果：分配/释放路径不统一，`Memory.Allocate` 的包装（对齐、统计、失败处理）被绕过；后续替换分配器或开启内存追踪时本文件成为盲区。
- 最小修复：统一走 `Memory.Allocate`/`Memory.Free`，或在注释中说明为何必须绕过（并补错误处理）。
- 备注：`Memory.krt` 存在于 stdlib/libs，说明存在统一封装可用。

### [中] 71-22｜Clear 忽略参数、不清空缓冲区，注释与实现脱节
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:153-156`
- 类别：5 死代码 ＋ 13 注释与实现脱节
- 触发条件：调用 `Clear(currentLength)`。
- 后果：参数 `currentLength` 完全未使用；函数只 `return 0`，既不重置缓冲区内容也不做任何清理，但注释称「清空缓冲区（重置长度）」。调用方若以为缓冲区已清零，后续 `BufferToString` 在长度未正确维护时会读到旧数据。
- 最小修复：要么真正把缓冲区前 `currentLength` 字节清零并返回 0，要么删除该死参数并明确「仅返回新长度」的契约。
- 备注：这是纯函数式 API 的产物，与 71-23 的无状态设计同源。

### [中] 71-23｜无实例状态与标准成员，API 与标准库 StringBuilder 语义分叉
- 位置：`Re.KrtC/src/Runtime/Krt/StringBuilder.krt:11-221`（全类）
- 类别：3 语义分叉 ＋ 13 注释与实现脱节
- 触发条件：任何按标准 `StringBuilder` 语义（`new`、`Append` 返回自身、`Length`/`Capacity` 属性、`ToString()`）使用的代码。
- 后果：本类是纯静态函数集，无实例、无状态、无 `Length`/`Capacity`/`ToString` 成员（只有 `BufferToString`）；调用方必须自行维护 `buffer/currentLength`，易错且与 `System.Text.StringBuilder` 或标准库封装的契约不同。
- 最小修复：改为实例类封装 `buffer/capacity/length`，或明确重命名为 `StringBuilderOps` 并在文档中声明过程式契约。
- 备注：`stdlib/StringBuilder.krt` 与 `libs/System/StringBuilder.krt` 存在同名副本，其语义是否一致未验证（见覆盖范围）。

### [致命] 71-24｜Write(char)/WriteLine(char) 输出十进制码点而非字符
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:78-87`
- 类别：3 语义分叉 ＋ 13 注释与实现脱节
- 触发条件：`Console.Write('A')` 或 `Console.WriteLine('，')` 等字符输出。
- 后果：`Write((int32)value)` 把字符码点当整数打印，输出 `65` 而非 `A`；与 .NET `Console.Write(char)` 语义直接冲突，且无任何提示。
- 最小修复：实现真正的字符写入（按编码把该字符转为字节序列后 `WriteRaw`），或删除 char 重载强制显式转换。
- 备注：`Write(bool)` 输出 `"True"/"False"` 与 .NET 一致，唯独 char 分叉。

### [高] 71-25｜WriteRaw 忽略 write 返回值，短写静默截断
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:9-12,22,33,106,141`
- 类别：1 静默失败 ＋ 9 错误处理不一致
- 触发条件：写管道/终端/文件时 `write(2)` 部分写（信号中断、非阻塞 fd、管道满），或 fd 无效返回 -1。
- 后果：返回值被丢弃，短写导致输出残缺且无诊断；`WriteFormat` 逐字符调用时每次短写都静默丢失一个字符。
- 最小修复：循环重试直到写完或返回错误；对 `EINTR` 重试，其他错误上报。
- 备注：`Write(string)` 把 `int64 len` 强转 `int32`（22），超长字符串还会截断长度。

### [高] 71-26｜WriteFormat 静默丢弃未知占位符
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:127-138`
- 类别：1 静默失败 ＋ 2 诊断缺失
- 触发条件：格式串含 `{2}`、`{x}`、`{}` 等未支持占位符，或 `{{` 转义。
- 后果：`else if (arr[i+1]=='1')` 之后无 else 分支，但 `i = i + 3` 无条件执行——`{2}` 被整段吞掉且不输出、不报错；`{{` 被当作占位符解析，产生错乱输出（如 `"{{}}"` 输出 `"}"`）。
- 最小修复：仅识别 `{0}`/`{1}`，其余按字面输出（含 `{`）；或支持 `{{`/`}}` 转义并对接更多参数。
- 备注：单参版（99-109）只认 `{0}`，但同样会把 `{1}` 静默丢弃。

### [高] 71-27｜WriteFormat 未检查 format 为 null
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:93-95,121-123`
- 类别：8 未检查前置条件（NULL）
- 触发条件：`Console.WriteFormat(null, "x")` 或 `WriteLineFormat(null, ...)`。
- 后果：直接 `Runtime.Sys.InternalStringLength(null)` 与 `InternalStringPtr(null)`，行为未定义/崩溃；`Write(string)` 反而对 null 做了静默返回（16-19），处理不一致。
- 最小修复：入口统一判 null（返回或报错），与 `Write` 的契约对齐。
- 备注：`arg0` 为 null 时不崩溃（`Write(null)` 静默返回），但参数被无声省略。

### [中] 71-28｜Write(string) 对 null 静默返回，与 WriteLine(null) 叠加后语义含糊
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:14-19,25-29`
- 类别：1 静默失败 ＋ 9 错误处理不一致
- 触发条件：`Console.Write(null)` 或 `Console.WriteLine(null)`。
- 后果：`Write(null)` 什么都不输出且不报错；`WriteLine(null)` 则输出一个空行。与 `WriteFormat` 对 null 崩溃（71-27）行为不一致，调用方无法预期。
- 最小修复：统一 null 策略（建议输出空串或抛错），并在注释中写明。
- 备注：与 71-27 独立（一个是静默、一个是崩溃）。

### [中] 71-29｜namespace 声明与同批文件不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:1`（对比 `String.krt:1`、`StringBuilder.krt:1`、`Convert.krt:1`）
- 类别：11 配置漂移
- 触发条件：编译期名称解析或按命名空间组织代码时。
- 后果：本文件为 `namespace System;`，其余三文件为 `namespace System.Runtime;`，同一 runtime 目录出现两套命名空间；`Console` 与 .NET 的 `System.Console` 巧合一致，但 `StringOps`/`Convert` 落在 `System.Runtime`，跨文件引用只能靠全限定名（如 `Runtime.StringOps`），易漂移。
- 最小修复：统一为 `System` 或 `System.Runtime`，并全批对齐。
- 备注：`Convert.krt:1` 的 `System.Runtime` 与 .NET `System.Convert` 亦不一致，见 71-35。

### [中] 71-30｜STDIN/STDERR 常量与 Read 系列 API 缺失，成死代码
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:5-7`
- 类别：5 死代码 ＋ 12 测试不足
- 触发条件：任何需要读输入或写错误流的调用。
- 后果：`STDIN`、`STDERR` 定义后全文件无任何使用；无 `Read`/`ReadLine`/`Error` 等 API，常量与能力不匹配，易让调用方误以为可用。
- 最小修复：实现 Read/Error 系列，或删除未用常量并在文档中说明仅支持 stdout。
- 备注：`WriteRaw` 已支持任意 fd（9-12），具备扩展基础。

### [中] 71-31｜Write(int64) 传播 FromInt64 的最小值缺陷
- 位置：`Re.KrtC/src/Runtime/Krt/Console.krt:48-52`（根因见 `String.krt:100-104`）
- 类别：3 语义分叉 ＋ 8 未检查前置条件
- 触发条件：`Console.Write(int64.MinValue)` / `Console.WriteLine(int64.MinValue)`。
- 后果：经 `StringOps.FromInt64` 输出 `"-"`，与 `Console.Write(int32.MinValue)`（走内置路径）行为不可比，且无任何报错。
- 最小修复：修复 71-1 后本条自动消除；同时统一 int32/int64 的格式化路径（71-9）。
- 备注：这是 71-1 的跨文件传播点，独立记录以便追踪影响面。

### [致命] 71-32｜ToInt32/ToInt64 溢出静默回绕，无溢出检测
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:33`、`64`
- 类别：8 未检查前置条件（数值解析/溢出）＋ 1 静默失败
- 触发条件：`Convert.ToInt32("2147483648")`、`Convert.ToInt32("999999999999")`、`Convert.ToInt64("9223372036854775808")` 等超范围输入。
- 后果：`result = result * 10 + digit` 直接回绕（如 `"2147483648"` 变 `-2147483648`），返回错误数值且无诊断；标准库语义应抛溢出异常，语义分叉。
- 最小修复：每次乘加前检查 `result > (max - digit) / 10`，溢出时抛错或返回失败标志。
- 备注：负数边界 `-2147483648`/`int64` 最小值因回绕巧合正确，但正边界不成立。

### [高] 71-33｜非法/部分非法输入静默截断或返回 0，无法与合法 0 区分
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:29-38,60-69`
- 类别：1 静默失败 ＋ 2 诊断缺失
- 触发条件：`Convert.ToInt32("12abc")`、`"abc"`、`""`、`"  12"`、`"0x10"`。
- 后果：遇非数字即 `break`，返回已累积部分（`"12abc" → 12`）或 `0`；`"abc"` 与 `"0"` 都返回 0，调用方无法区分合法与非法输入。标准库 `int.Parse` 对非法输入抛 `FormatException`。
- 最小修复：解析结束后校验是否至少消费一个数字且无非法尾随；失败时抛错或返回 `bool TryParse` 模式。
- 备注：`null` 输入返回 0（13、44），同样无诊断。

### [中] 71-34｜不支持前导 '+'、空白与十六进制，单独 "-" 返回 0
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:23-27,54-58`
- 类别：3 语义分叉 ＋ 13 注释与实现脱节
- 触发条件：`Convert.ToInt32("+12")`、`" 12"`、`"0x1F"`、`"-"`、`"-abc"`。
- 后果：仅识别前导 `'-'`，`'+'` 与空白被当作非法字符而 `break`，返回 0 或截断值；单独 `"-"` 返回 0（而非报错）。与标准 `int.Parse`（允许前导符号/空白）语义分叉。
- 最小修复：按标准解析规则处理 `+`/空白，并对「只有符号没有数字」显式报错。
- 备注：与 71-33 独立（字符集/格式支持 vs 失败静默）。

### [中] 71-35｜Convert 命名空间与 .NET 及本批 Console 不一致
- 位置：`Re.KrtC/src/Runtime/Krt/Convert.krt:1`
- 类别：11 配置漂移
- 触发条件：跨文件引用或对照标准库时。
- 后果：`Convert` 声明为 `System.Runtime.Convert`，而 .NET 为 `System.Convert`、本批 `Console.krt` 为 `System`，同批文件命名空间三处不一致（`System` / `System.Runtime`），增加引用漂移风险。
- 最小修复：统一命名空间约定（含 71-29）。
- 备注：`stdlib/Convert.krt`、`libs/System/Convert.krt` 副本的命名空间未验证。

### [中] 71-36｜四个文件均无测试/断言，边界与错误路径无覆盖
- 位置：`String.krt:1-303`、`StringBuilder.krt:1-222`、`Console.krt:1-152`、`Convert.krt:1-90`（全批）
- 类别：12 测试不足
- 触发条件：回归验证或边界用例（MinValue、溢出、null、非法输入、容量边界）执行时。
- 后果：本批 30 余条缺陷（尤其 71-1、71-6、71-12、71-17、71-32）均属边界/错误路径，无测试支撑；后续修复无法防回归。
- 最小修复：为每个公开函数补最小用例集：正常值、空/null、边界值（MinValue/MaxValue）、溢出值、非法输入、容量恰好相等/差一。
- 备注：测试缺失是全局条目，不与其他条目合并。
