# 第六十二批：Tokenizer.c（全量）

## 覆盖范围

- 全量读取 `Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c`（1232 行，分三段读完）：关键字表、`lexer_advance/skip_whitespace`、数字/标识符/字符/字符串读取、`lexer_next_token` 全部运算符分支、`peek/peek_nth`、`token_free`、`token_type_to_string`、整数类型辅助函数。
- 契约核对（只读少量行/单符号 grep）：`Tokenizer.h`（Token/Lexer 结构、函数声明）、`Accelerator.h/.c`（`accelerator_is_alpha/digit/octal_digit/hex_digit`、`encode_utf8`）、`Arena.h/.c`（`KrtArenaAlloc/Realloc` 失败返回 NULL 语义）、`Allocator.c`（`KrtSafeStrdup`、`KrtMemoryReportError` 确为空操作）、`IntegerWidths.def`、`Core/Utils/KrtCommon.h`（`KRT_STRDUP`→`KrtSafeStrdup`）、调用方 `CompilerPipeline.c` 的 error_count 检查与 `ParserStatement/ParserExpression` 的 peek 用法。
- 已应用关键前提 G1：`KRT_MALLOC/KRT_STRDUP/KrtArenaAlloc` 失败返回 NULL 且不 exit，`KrtMemoryReportError` 为空操作，故所有分配失败路径均为“活路径 + 零诊断”。前人指出的 `Tokenizer.c:428/436` 已核对并纳入（见 62-5）。

## 问题条目

### [致命] 62-1｜UTF-8 前导字节触发无限递归，栈溢出崩溃
- 位置：`Tokenizer.c:798-820`（关键 810-819）
- 类别：8 未检查前置条件（递归无界）
- 触发条件：源码中任意非字符串/注释/字符字面量位置出现字节 `0xC0..0xF7`（即几乎任意 UTF-8 非 ASCII 字符，如 `var café = 1;`、中文标识符、emoji）。
- 后果：`default` 分支算出 `bytes_remaining` 后，`while` 从当前 lead byte 开始判断 `0x80..0xBF`，lead byte 本身不是 continuation，立即 `break`，随后 `return lexer_next_token(lexer)` 在 `position` 未前进的情况下递归；无限递归直到栈溢出，编译器进程崩溃。
- 最小修复：进入 `while` 前先 `lexer_advance(lexer)` 消费 lead byte，或从 `position+1` 开始校验 continuation；并给递归设深度上限。
- 备注：`accelerator_is_alpha` 仅 ASCII（`Accelerator.c:83-85`），因此非 ASCII 字节必然走到该分支。字节 `0x80..0xBF` 走 `TOKEN_UNKNOWN`，`0xF8..0xFF` 走 62-21。

### [致命] 62-2｜字符字面量 arena 分配失败时返回指向栈局部数组的悬垂指针
- 位置：`Tokenizer.c:409-415`（`char value[2]` 定义于 349）
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：`KrtArenaAlloc(lexer->arena, 2)` 返回 NULL（OOM，G1）。
- 后果：`char_str ? char_str : value` 返回局部数组 `value` 的地址；函数返回后该数组生命周期结束，调用方拿到悬垂 `token.value`，后续 `strlen/strdup/strcmp` 为未定义行为；且此路径不增加 `error_count`，无任何诊断。
- 最小修复：分配失败时统一返回 `TOKEN_EOF` 并 `lexer->error_count++`，或把 `value` 改为调用方传入/堆分配；禁止把栈地址塞进 `Token`。
- 备注：与 62-4/62-5 同属 G1，但此路径不仅静默还产生悬垂指针。

### [致命] 62-3｜字符串字面量尾部反斜杠越界读（越过 NUL）
- 位置：`Tokenizer.c:441-513`（关键 443、509、431）
- 类别：8 未检查前置条件（越界）
- 触发条件：字符串在遇到 closing `"` 前以 `\` 结尾并直接碰到源码 NUL，例如 `"abc\`（无闭合引号）。
- 后果：443 处 `escape` 取到 `'\0'`，走 `default` 的 504-505 写入 `'\\'` 与 `'\0'`，随后 509 无条件 `lexer_advance` 把 `position` 推到 NUL 之后（`strlen+1`）；431 的循环条件与后续写入在分配边界外读写堆内存，属未定义行为，可能崩溃或把相邻内存当字符串内容。
- 最小修复：在 441/509 前后检查 `lexer->source[lexer->position] == '\0'`，反斜杠后遇 NUL 直接判未终止并 `error_count++`；默认转义写两个字节前也要再确认容量。
- 备注：`source_owned` 是 `KRT_STRDUP` 的精确 `len+1` 分配，无多余字节可利用。

### [致命] 62-4｜arena 分配失败静默伪装成 EOF（数字/标识符路径，G1）
- 位置：`Tokenizer.c:305-308`（数字）、`Tokenizer.c:326-329`（标识符）
- 类别：1 静默失败 / 10 资源与生命周期
- 触发条件：`KrtArenaAlloc(lexer->arena, length + 1)` 返回 NULL。
- 后果：返回 `TOKEN_EOF, ""`，不增加 `error_count`、不设 `error_line`；解析器把 OOM 当作正常文件结束，剩余源码被静默丢弃，流水线 `error_count` 检查（`CompilerPipeline.c:249/281`）也看不到任何错误。
- 最小修复：分配失败时返回带错误语义的 token（或置 `error_count`/`error_line` 并返回 `TOKEN_UNKNOWN`），绝不复用 `TOKEN_EOF`。
- 备注：G1 的直接体现；`KrtMemoryReportError` 为空操作（`Allocator.c:425-435`），上层无诊断。

### [致命] 62-5｜arena 分配/realloc 失败静默伪装成 EOF（字符串路径，G1；含前人已指出 428/436）
- 位置：`Tokenizer.c:426-429`（初始分配）、`Tokenizer.c:434-437`（扩容）
- 类别：1 静默失败 / 10 资源与生命周期
- 触发条件：字符串初始 `KrtArenaAlloc(arena, 32)` 失败，或扩容 `KrtArenaRealloc(arena, value, capacity/2, capacity)` 返回 NULL。
- 后果：返回 `TOKEN_EOF, ""`，不计错误；长字符串解析中一旦 OOM，整个token流提前终止且零诊断。已核对前人指出的 428/436 确为此问题。
- 最小修复：同 62-4；另扩容失败时保留旧 `value` 并标记错误，避免丢失已读内容。
- 备注：`KrtArenaRealloc` 失败时旧指针仍有效（`Arena.c:138-168`），但代码直接返回 EOF 丢弃。

### [严重] 62-6｜字符字面量 `\xNN` 被多一次 advance 误判为“未终止”
- 位置：`Tokenizer.c:384-396`、`402`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：任意 `'\x41'`、`'\x4'`、`'\x'`（`\x` 后 0~2 个十六进制位）。
- 后果：`\x` 分支在 384 消费 `x` 后，循环 386-389 每读一位就 `advance`，结束时 `position` 已落在 closing `'` 上；随后 396 无条件再 `advance` 跳过该引号，402 检查的是引号之后的字节，必然 `!= '\''` → `error_count++` 并返回 `TOKEN_EOF`。即字符 `\x` 转义完全不可用。
- 最小修复：`\x` 分支结束后 `continue`/跳过 396（与字符串版 `Tokenizer.c:476-484` 的 `continue` 对齐），或把 396 的 advance 移入各简单转义分支。
- 备注：字符串版 `\x` 用 `continue` 正确，字符版缺失，属同一语义在两个字面量间的实现分叉。

### [严重] 62-7｜字符字面量尾部反斜杠越界读
- 位置：`Tokenizer.c:351-402`（关键 396、402）
- 类别：8 未检查前置条件（越界）
- 触发条件：字符字面量以 `\` 结尾并碰到源码 NUL，例如 `'\`（无闭合引号）。
- 后果：353 取到 `escape='\0'`，392 写入 `value[0]='\0'`，396 `advance` 把 `position` 推到 NUL 之后，402 在边界外读取 `source[position]`，未定义行为。
- 最小修复：反斜杠后先判断 NUL，直接走未终止错误路径；396 前加边界检查。
- 备注：与 62-3 同源（默认转义后无条件 advance）。

### [严重] 62-8｜未终止字符串/字符/块注释统一返回 TOKEN_EOF，与真实 EOF 混淆
- 位置：`Tokenizer.c:224-228`（块注释）、`402-406`（字符）、`516-520`（字符串）
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：`"abc`、`'a`、`/* abc` 等未终止构造。
- 后果：词法错误与文件结束共用 `TOKEN_EOF`，解析器无法区分，剩余 token 全部消失；只留一个 `error_count` 与起始行，后续阶段只能报笼统的 “unterminated … near line N”（`CompilerPipeline.c:251/283/293`）。
- 最小修复：引入 `TOKEN_LEX_ERROR`/`TOKEN_UNKNOWN` 之类可区分类型，或让调用方在 EOF 时强制检查 `error_count` 并携带列号与原因。
- 备注：`TOKEN_EOF` 还被 62-4/62-5 的 OOM 路径复用，进一步放大混淆。

### [严重] 62-9｜`\0` / `\x00` 在字符串中写入嵌入 NUL，导致静默截断
- 位置：`Tokenizer.c:463-465`（字符串 `\0`）、`476-484`（字符串 `\x`）、`370-372`（字符 `\0`）
- 类别：1 静默失败 / 3 语义分叉
- 触发条件：字符串含 `\0` 或 `\x00`（以及 `\x` 无数字）等产生 0x00 的转义。
- 后果：`Token.value` 是无长度字段的 `char*`，嵌入 NUL 后所有 `strlen/strdup/strcmp` 消费者只能看到 NUL 之前的内容，后半段字面量被静默丢弃；如 `"a\0b"` 实际变成 `"a"`。
- 最小修复：为 `Token` 增加长度字段，或拒绝在字符串/字符中产生嵌入 NUL 并给出诊断。
- 备注：与 62-18 的转义不一致叠加；字符 `\0` 还会得到空字符串。

### [严重] 62-10｜peek/peek_nth 不恢复 error_count/error_line，lookahead 污染词法错误状态
- 位置：`Tokenizer.c:829-841`、`843-863`
- 类别：7 共享可变状态 / 9 错误处理不一致
- 触发条件：对含未终止字面量或非法字符的区域做 `lexer_peek_token`/`lexer_peek_nth_token`；或重复 peek。
- 后果：peek 内部调用 `lexer_next_token` 会 `error_count++` 并覆盖 `error_line`，而函数只恢复 `position/line/column`；重复 peek 使计数虚增、行号被最后一次 peek 覆盖，流水线据此报告错误，与实际消费位置不一致。
- 最小修复：进入时保存 `error_count/error_line`（以及 arena 状态）并在返回前恢复；或把 peek 实现为无副作用的纯预览。
- 备注：`ParserStatement.c:1534` 的 `for (look=1; look<256; look++)` 会放大该问题。

### [严重] 62-11｜字符串 `\U` 转义在容量边界被静默丢弃
- 位置：`Tokenizer.c:432-439`、`494-501`
- 类别：1 静默失败 / 8 未检查前置条件（缓冲区容量）
- 触发条件：字符串已写 `capacity-3` 个字节时遇到 `\U0001F600` 等需要 4 字节 UTF-8 的转义（如 capacity=32、length=29）。
- 后果：432 的 `length + 2 >= capacity` 不触发扩容，`capacity-length==3`；`encode_utf8` 对 `codepoint >= 0x10000` 要求 `max_length >= 4`，返回 0（`Accelerator.c:134-137`），`length += 0` 后 `continue`，该转义被无声删除，字符串内容缺失且无任何诊断。
- 最小修复：把容量检查改为按最坏 4 字节预留（如 `length + 4 >= capacity`），或检查 `encode_utf8` 返回值并在 0 时报错。
- 备注：`encode_utf8` 的 0 返回值在 485-501 三处 `length +=` 调用点均未被检查。

### [严重] 62-12｜`\U` 八位十六进制解析有符号溢出 UB 且无 Unicode 范围校验
- 位置：`Tokenizer.c:497-499`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：`\U` 后跟 8 个十六进制位且值超过 `INT_MAX`（如 `\UFFFFFFFF`），或值超过 `0x10FFFF`（如 `\U00110000`）。
- 后果：`unicode_val = unicode_val * 16 + ...` 对 `int` 做乘法，8 位十六进制可溢出，属有符号溢出未定义行为；同时 `encode_utf8` 不拒绝 `> 0x10FFFF`，会产出非法 UTF-8 字节序列。
- 最小修复：用 `unsigned` 累积并检查 `<= 0x10FFFF`，越界时报词法错误。
- 备注：`\u`（4 位）无溢出，但仍有范围/代理项问题（62-13）。

### [严重] 62-13｜`\u`/`\U` 代理项码点被静默编码为非法 UTF-8
- 位置：`Tokenizer.c:485-501`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：`\uD800`、`\uDFFF`、`\U0000D800` 等代理项码点。
- 后果：`encode_utf8`（`Accelerator.c:126-133`）按 3 字节模板输出 `ED A0 80` 等，即 UTF-8 标准禁止的代理项编码；词法层不报错，非法字节流入 AST/后续阶段。
- 最小修复：在 `encode_utf8` 调用前拒绝 `0xD800..0xDFFF`，或让 `encode_utf8` 返回 0 并由调用方报错。
- 备注：与 62-11/62-12 同属 `\u`/`\U` 校验缺失。

### [严重] 62-14｜八进制字面量 `08`/`09`/`018` 被静默切分为两个 NUMBER
- 位置：`Tokenizer.c:266-271`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：`08`、`09`、`018`、`0789` 等以 0 开头且次字符是数字但含 8/9。
- 后果：进入八进制分支后 `while (is_octal_digit(...))` 只吃掉 `0`（以及后续 0-7），遇到 8/9 停止，得到 `NUMBER "0"`，下一个 token 是 `NUMBER "8"`；解析器看到两个相邻数字，报“期望 ;”之类与真实原因无关的错误，且位置指向第二个数字。
- 最小修复：八进制分支遇到 8/9 时判为词法错误（或按十进制整体读取），不要静默截断。
- 备注：`0x`/`0b` 分支无此问题；`0` 后跟 `.`/`e`/`E` 已被 266-268 排除。

### [严重] 62-15｜残缺指数/小数尾部被静默接受并由 atof 吞掉
- 位置：`Tokenizer.c:278-302`（关键 294-302）
- 类别：1 静默失败 / 3 语义分叉
- 触发条件：`1e`、`1E`、`1e+`、`1e-`、`1.5e` 等指数后无数字；`1.` 等。
- 后果：词法层照常产出 `TOKEN_NUMBER`，无诊断；`ParserExpression.c:169` 用 `atof` 转换，`atof("1e") == 1.0`，于是 `x = 1e;` 被静默当成 `1.0`，语言层面错误的字面量变成合法值。
- 最小修复：指数分支要求至少一位数字，否则 `error_count++` 并返回错误 token；`1.` 是否合法需按语言规范明确。
- 备注：`0x`、`0b` 无数字会在下游 `KrtIntegerParse` 报错（非静默），但 `1e` 因 atof 而静默，危害更大。

### [严重] 62-16｜`token_type_to_string` 漏掉 21 个词法可产出 token，诊断显示 UNKNOWN
- 位置：`Tokenizer.c:872-1174`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：解析器对以下 token 调用 `token_type_to_string`：`TOKEN_FN`、`TOKEN_DO`、`TOKEN_FOREACH`、`TOKEN_IN`、`TOKEN_STRUCT`、`TOKEN_INTERFACE`、`TOKEN_ENUM`、`TOKEN_NAMESPACE`、`TOKEN_CHAR`、`TOKEN_BITWISE_AND`、`TOKEN_BITWISE_OR`、`TOKEN_BITWISE_XOR`、`TOKEN_LSHIFT`、`TOKEN_RSHIFT`、`TOKEN_POWER`、`TOKEN_NULL_COALESCING`、`TOKEN_QUESTION_DOT`、`TOKEN_SWITCH`、`TOKEN_CASE`、`TOKEN_BREAK`、`TOKEN_CONTINUE`、`TOKEN_DEFAULT`。
- 后果：这些分支全部落到 `default: return "UNKNOWN"`，语法错误信息变成 “expected X, got UNKNOWN”，无法定位是哪个关键字/运算符；`TOKEN_ATTRIBUTE`、`TOKEN_AUTO`、`TOKEN_PRINT` 同样缺失（当前无关键字产出）。
- 最小修复：补齐所有 `KrtTokenType` 分支，并加 `_Static_assert`/单元测试保证枚举与 switch 同步。
- 备注：`TOKEN_TYPE_STRING` 返回 `"STRING"` 而 `TOKEN_STRING` 返回 `"STRING_LITERAL"`，命名也易误导。

### [严重] 62-17｜空字符字面量 `''` 被静默接受
- 位置：`Tokenizer.c:397-407`
- 类别：2 诊断缺失 / 3 语义分叉
- 触发条件：源码 `''`。
- 后果：397 的条件排除了 `'` 与 `\0`，直接落到 402 的引号检查并通过，返回 `TOKEN_CHAR_LITERAL` 且 `value[0]='\0'`（空字符串）；非法字面量无诊断。
- 最小修复：在 402 前判断是否读到有效字符，空字面量报词法错误。
- 备注：与 62-6 同属字符字面量校验缺失。

### [严重] 62-18｜字符与字符串转义语义不一致，字符未知转义静默丢弃反斜杠
- 位置：`Tokenizer.c:382-394`（字符）对照 `475-507`（字符串）
- 类别：3 语义分叉 / 13 注释与实现脱节
- 触发条件：未知转义如 `'\q'` 与 `"\q"`；或 `'\u0041'`、`'\U0041'`。
- 后果：字符串默认分支保留 `'\\'+escape` 两字符，字符默认分支只写 `escape`（丢掉反斜杠）；字符不支持 `\u`/`\U`，`'\u0041'` 会被当成普通字符 `u` 并因 402 检查失败而报未终止。同一转义语法在两个字面量中结果不同，且都无诊断。
- 最小修复：抽取统一转义解析函数，或明确规范并让字符版支持 `\u`/`\U`、未知转义统一报错。
- 备注：与 62-6、62-17 一起构成字符字面量处理的系统性缺口。

### [严重] 62-19｜`lexer_peek_nth_token` 不回收 arena 且 O(n²) 重复词法分析
- 位置：`Tokenizer.c:843-863`；放大点 `ParserStatement.c:1534-1566`
- 类别：10 资源与生命周期 / 7 共享可变状态
- 触发条件：调用方在循环中递增 `n` 反复 peek（现有解析器 `for (look=1; look<256; look++)` 正是此模式）。
- 后果：每次 peek 都从保存位置重新产生前 `n` 个 token，并把每个 token 的 `value` 追加进 arena 且不回滚；`position` 恢复但 arena 分配保留，造成内存随 look 平方级膨胀，可把系统推向 OOM，进而触发 62-4/62-5 的“OOM 伪装 EOF”。
- 最小修复：peek 期间使用独立/可重置 arena，或缓存已产生 token；至少提供 `lexer_arena_save/restore`。
- 备注：`token_free` 是空操作（62-25），无法靠它回收。

### [严重] 62-20｜`lexer_peek_nth_token` 对 `n <= 0` 返回 `type = -1`、`value = NULL`
- 位置：`Tokenizer.c:843-856`
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：调用方传入 `n == 0` 或负数（公共 API 未限制）。
- 后果：`for` 循环不执行，返回未初始化的 `Token`（仅 `type=-1`），`value` 为 NULL；调用方若解引用 `value` 或把 `-1` 当枚举使用，行为未定义，且无任何断言/诊断。
- 最小修复：`n <= 0` 时返回 `TOKEN_EOF` 或 `TOKEN_UNKNOWN` 并 `error_count++`；头文件注明 `n >= 1`。
- 备注：当前调用点均传 `>=1`（`ParserStatement.c:1534/1608`），属潜在契约缺口。

### [严重] 62-21｜字节 `0xF8..0xFF` 静默返回 TOKEN_EOF
- 位置：`Tokenizer.c:804-807`、`826`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：源码中出现 `0xF8..0xFF` 字节（非法 UTF-8 首字节，如某些 Windows-1252 字节）。
- 后果：`else break` 跳出 `switch`，直接执行 826 返回 `TOKEN_EOF`，不计 `error_count`；词法流被当作正常结束，剩余源码全部丢失。
- 最小修复：该分支改为 `lexer_advance` 后返回 `TOKEN_UNKNOWN` 并 `error_count++`。
- 备注：与 62-1 同属 `default` 的 UTF-8 处理，但结果一个是崩溃、一个是静默 EOF。

### [严重] 62-22｜词法错误诊断只有计数+行号，无列号/原因，且 `error_line` 被覆盖
- 位置：`Tokenizer.c:225-226`、`403-404`、`517-518`
- 类别：2 诊断缺失
- 触发条件：任意多次词法错误。
- 后果：只累加 `error_count` 并写单个 `error_line`，没有列号、没有错误文本、不保留多条错误；后续错误覆盖前一条行号，调用方（`CompilerPipeline.c:251/283/293`）只能报 “near line N”。对 62-6 这类逻辑错误完全无法定位。
- 最小修复：引入错误数组（行/列/原因码），或至少提供列号与首/末错误位置。
- 备注：`error_count` 还被 62-10 的 peek 副作用污染。

### [中] 62-23｜`lexer_reset` 不完全重置且无任何调用方（死代码）
- 位置：`Tokenizer.c:569-573`
- 类别：5 死代码 / 7 共享可变状态
- 触发条件：调用 `lexer_reset`（当前代码库无调用方，grep 仅命中定义与头声明）。
- 后果：只重置 `position/line/column`，不重置 `error_count/error_line`、不清空 arena、不重置 `current_token`；若将来用于二次词法分析，旧错误与旧 arena 内容会泄漏到新一次分析，且 `error_count` 继续累加。
- 最小修复：删除该函数，或补齐 `error_count/error_line` 重置、`KrtArenaReset` 与 `current_token` 清空。
- 备注：`KrtArenaReset` 存在于 `Arena.c:171`，可直接使用。

### [中] 62-24｜`Lexer.current_token` 是从未初始化/读写的死字段
- 位置：`Tokenizer.h:215`；`Tokenizer.c:528-555`
- 类别：5 死代码 / 7 共享可变状态
- 触发条件：任何读取 `lexer->current_token` 的代码（当前词法器自身从不使用）。
- 后果：`lexer_create` 用 `KRT_MALLOC`（非 calloc）分配 `Lexer`，`current_token` 保持未初始化内存；字段既不设也不读，属死状态，一旦有调用方误用即未定义行为。
- 最小修复：删除该字段，或显式清零并让 `lexer_next_token` 维护它。
- 备注：解析器维护的是 `parser->current_token`，与此无关。

### [中] 62-25｜`token_free` 为空操作，token 所有权契约缺失
- 位置：`Tokenizer.c:865-870`；声明 `Tokenizer.h:227`
- 类别：6 契约破坏 / 13 注释与实现脱节
- 触发条件：调用方按名字期望 `token_free` 释放 `token.value`（`ParserStatement.c` 多处调用）。
- 后果：函数体只有 `(void)token`，不做任何事；`value` 实际由 lexer 的 arena 拥有，`lexer_destroy` 后全部悬垂，但头文件未说明“token 不得晚于 lexer 存活”，调用方无法安全复制/释放。
- 最小修复：在头文件明确所有权（arena 拥有、lexer 销毁后失效），或让 `token_free` 真正释放非 arena 来源的 value。
- 备注：`lexer_peek_nth_token` 内部的 `token_free(&token)` 因此只是噪音。

### [中] 62-26｜关键字哈希表无满表保护，容量与 `IntegerWidths.def` 强耦合
- 位置：`Tokenizer.c:138-156`（关键 151-153）、`14`
- 类别：8 未检查前置条件 / 11 配置漂移
- 触发条件：`keywords[]` 条目数达到 `KRT_KEYWORD_TABLE_SIZE`（512）——当前约 238 条，若 `IntegerWidths.def` 扩充或继续加关键字即触发。
- 后果：`init_keyword_table` 的 `while (keyword_table[index] != NULL) index = (index + 1) % 512;` 在满表时无限循环，且发生在 `pthread_once` 首次标识符处，进程挂死；表容量是硬编码常量，与 def 文件无编译期一致性校验。
- 最小修复：插入前统计关键字数并断言小于表大小，或改用链地址法/动态表；添加 `_Static_assert(sizeof(keywords)/sizeof(keywords[0]) < KRT_KEYWORD_TABLE_SIZE)`。
- 备注：`keywords[]` 无显式长度，依赖 NULL 哨兵。

### [中] 62-27｜公共 API 未检查 NULL 前置条件
- 位置：`Tokenizer.c:528-555`（`lexer_create`）、`569-573`（`lexer_reset`）、`575`（`lexer_next_token`）、`829`（`peek`）、`843`（`peek_nth`）
- 类别：8 未检查前置条件
- 触发条件：`lexer_create(NULL)`，或对 NULL `Lexer*` 调用 `lexer_reset/lexer_next_token/lexer_peek_*`。
- 后果：`lexer_create(NULL)` 经 `KrtSafeStrdup` 返回 NULL（`Allocator.c:263-266`）后静默返回 NULL，与真实 OOM 不可区分；其余函数直接解引用 NULL 崩溃，无断言无诊断。
- 最小修复：入口统一 `if (!lexer) return ...;`，`lexer_create` 对 NULL source 明确报错或约定。
- 备注：`lexer_destroy` 已做 NULL 检查，风格不一致。

### [低] 62-28｜前导小数点 `.5` 被拆成 `DOT` + `NUMBER`
- 位置：`Tokenizer.c:590-592`（数字分发）、`773-775`（`.` 分支）
- 类别：3 语义分叉
- 触发条件：源码 `x = .5;`。
- 后果：`.` 走 `TOKEN_DOT`，随后 `5` 走 `TOKEN_NUMBER`，解析器无法把它当浮点字面量；若语言允许前导小数点，则属功能缺失，且错误信息会指向表达式开头而非词法层。
- 最小修复：确认语言规范；若支持 `.5`，在 `lexer_next_token` 的 `.` 分支向前看数字并走 `lexer_read_number`。
- 备注：需语言规范确认，故列为低；`1..2` 之类也会被拆成 `1.`、`.`、`2`。
