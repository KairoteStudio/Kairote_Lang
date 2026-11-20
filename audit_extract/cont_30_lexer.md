# 第八十七批：SelfHost/Compiler/Frontend/Lexer/Lexer.krt（全量）

## 覆盖范围

- 目标文件：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt`，共 953 行，已按 1–250 / 250–499 / 499–748 / 749–953 四段全量读完，无跳读。
- 对照文件（仅定点读取相关行，未整文件通读）：`Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c`（185–233、244–341、340–416、440–524、560–609、795–859）、`Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.h`（10–119、120–185、185–239）、`Re.KrtC/src/Core/Utils/IntegerWidths.def`、`Re.KrtC/src/Accelerator.c`（83–101）。
- 符号级核验（仅 grep 特定符号）：`lexer_integer_type_bits`、`KrtTokenKind.Print/Auto/Attribute/BitwiseOr`、`token.error`、`KrtTokenIntegerSize/KrtTokenIntegerCommon`、`KrtLexerInit/KrtLexerNext`；并定点读取调用方 `SelfHost/Compiler/Frontend/Parser/Parser.krt:211`、`SelfHost/Compiler/Driver/Driver.krt:1114–1166`、`SelfHost/Compiler/Middle/Ir/Ir.krt:1725–1764`、`SelfHost/Compiler/Driver/Project.krt:678,988`、`SelfHost/Compiler/Frontend/Semantic/Semantic.krt:106`、`SelfHost/Compiler/Frontend/Parser/Parser.krt:1625`、`Test/SelfHost/test_lexer_vocabulary.py`。
- 结论：基础词法（标识符、关键字表、最长匹配、EOF 稳定性、无死循环）与 Re.KrtC 基本对齐；未发现必然崩溃路径。发现问题集中在换行计数、数值词法、转义、诊断链路与契约/死代码。

## 问题条目

### [高] 87-1｜CRLF 每次换行计两次，行号/列号整体漂移
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:39-42`（`lexer_advance`）
- 类别：语义分叉 / 诊断错位
- 触发条件：源文件使用 CRLF（`\r\n`）换行，或单独使用 `\r` 换行。
- 后果：`value == 10 || value == 13` 各自执行一次 `line = line + 1`，CRLF 被计为两行。含 N 个 CRLF 的文件实际行号为 2N+1 而非 N+1，所有 `token.line` 及后续 Parser/诊断报错行号整体错误。Re.KrtC 参考实现 `Tokenizer.c:185-193` 只在 `'\n'` 时 `line++`，`\r` 仅作空白跳过，二者语义分叉。
- 最小修复：仅在 `value == 10` 时换行；若需兼容孤立 `\r`，则在 `value == 13` 且下一个字节不是 10 时才换行。
- 备注：该函数是所有 token 位置的唯一来源，影响面为全量输入，非边缘用例。

### [高] 87-2｜八进制字面量分支缺失，`0` 前缀数值语义与参考实现分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:89-105`（十进制 `else` 分支）
- 类别：语义分叉
- 触发条件：源码出现 `0` 后跟八进制数字且下一字符不是 `.`/`e`/`E`，例如 `010`、`0777`、`0128`。
- 后果：Re.KrtC `Tokenizer.c:266-271` 对 `0`+八进制数字有独立分支并按八进制数字串切片（`0128` 切成 `012` 与 `8`）；SelfHost 无此分支，`010` 被当作十进制数字串整体交给下游。同一源码在两个编译器下数值常量语义不同，属静默语义分叉。
- 最小修复：确认语言规范后二选一——实现与参考一致的八进制扫描，或在词法层显式拒绝前导零（给出错误码），不要保持现状。
- 备注：疑似移植遗漏；本条只判定「与参考实现不一致且无注释说明」，不预设哪种语义正确。

### [高] 87-3｜`\u` / `\U` Unicode 转义未支持且被静默降级
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:111-121`（字符串/字符扫描的 escape 分支）
- 类别：语义分叉 / 静默失败 / 诊断缺失
- 触发条件：字符串或字符字面量中出现 `\uXXXX` / `\UXXXXXXXX`，如 `"\u0041"`、`'\u00e9'`。
- 后果：`else` 分支只前进一个字符（吞掉 `\` 与 `u`），既不识别 4/8 位十六进制，也不报错。下游唯一解码器 `SelfHost/Compiler/Middle/Ir/Ir.krt:1737-1759` 只支持 `\x`，遇到 `\u` 走 `!known` 分支把 `\` 与 `u` 原样写入数据段，最终字符串内容是字面文本 `\u0041` 而不是 `A`，程序输出错误且无任何诊断。Re.KrtC `Tokenizer.c:485-502` 明确支持 `\u`/`\U` 并编码为 UTF-8。
- 最小修复：在词法扫描处识别 `\u`/`\U` 并校验位数（缺失即置错误码），或在其文档/规范中显式声明不支持并让下游拒绝；至少不能静默通过。
- 备注：字符字面量因 `count` 统计会触发错误 6（`'\u0041'` 计 5），但字符串字面量完全无保护，是主要缺口。

### [高] 87-4｜词法错误码 1/2/3/5/6 全链路无人消费，诊断退化为通用 invalid syntax
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:58,80,86,98,106,127,128,133,928,939`（写入端）；`SelfHost/Compiler/Frontend/Parser/Parser.krt:211`（仅拷贝）；`SelfHost/Compiler/Driver/Driver.krt:1122-1123,1165`（只取 `token.start`）
- 类别：诊断缺失 / 阶段不对齐 / 错误处理不一致
- 触发条件：出现未闭合块注释（2）、未闭合字符串（3）、数字格式错误（5）、字符长度错误（6）、未知标点（1）。
- 后果：`KrtToken.error` 被精确赋值，但全库 grep 显示除测试外没有任何读取点：Parser 只在 fork 提交时把 `probe.token.error` 拷回（`Parser.krt:211`），从不判值；Driver 仅记录 `token.start` 后统一报 `"E_PARSE", "invalid syntax"`（`Driver.krt:1165`）。未定义错误码 4，也没有码→消息映射表。用户无法区分「未闭合注释」与「非法数字」，定位困难。
- 最小修复：在 Parser/Driver 消费 `token.error` 并映射为具体诊断码与消息；补齐码表（含 4 的去向）。
- 备注：与 Re.KrtC 的 `lexer->error_count/error_line` 聚合模型（`Tokenizer.c:225,403,517,542`）也不同，属错误模型整体分叉，建议一并规整。

### [中] 87-5｜`.5` 被并为单个 Number，与参考实现的 DOT+NUMBER 分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:74`
- 类别：语义分叉
- 触发条件：源码出现 `.5`、`.25` 等以点开头的小数。
- 后果：`(first == 46 && lexer_digit(lexer_at(lexer, 1)))` 使 `.5` 成为单个 `Number`/Floating token；Re.KrtC `Tokenizer.c:586-600` 的分派只认 digit/alpha，`.` 走标点分支，产生 `TOKEN_DOT` + `TOKEN_NUMBER`。同一表达式在两侧 token 流不同，若下游按参考语义解析会分叉。
- 最小修复：确认是否有意支持省略整数位的小数；若有意，补注释并在规范中登记，同时确认 Parser 对 `Dot` 的前瞻不受影响；若无意，删除该条件。
- 备注：疑似有意的语言扩展，但代码与注释均未说明，标「疑似」。

### [中] 87-6｜数字后缀 f/F/d/D 与指数/尾缀错误策略同参考实现分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:95-106`
- 类别：语义分叉 / 错误处理不一致
- 触发条件：`1f`、`1.5d`、`1e`、`1e+`、`1abc`、`12x`。
- 后果：SelfHost 新增 `f/F/d/D` 后缀并把 token 标为 Floating（101-104），Re.KrtC `Tokenizer.c:244-313` 不识别后缀，`1f` 是 `NUMBER(1)` + `IDENTIFIER(f)`；`1float` 在参考侧是 `NUMBER` + 关键字 `float`，在 SelfHost 侧被切成带错误 5 的 `Number("1f")` + `IDENTIFIER("loat")`。同时 SelfHost 对指数缺数字（98）与数字后跟字母（106）报错，参考实现一律静默接受。同一源码 token 流与诊断均不同。
- 最小修复：明确后缀是否属于语言规范；若属于，同步修正参考或在文档登记扩展；若不属于，删除 101-104 与 98/106 的报错，避免与参考漂移。
- 备注：标「疑似」有意扩展；`1f` 这类写法虽少见，但 `1e` 类输入在真实文件中并不罕见。

### [中] 87-7｜`KrtLexerInit` 前置条件不足且返回值可被静默忽略
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:10-17`；调用方 `SelfHost/Compiler/Driver/Project.krt:988`
- 类别：未检查前置条件 / 静默失败 / 契约破坏
- 触发条件：(a) `length` 大于 `source` 实际缓冲区长度；(b) 调用方丢弃 `bool` 返回值。
- 后果：Init 只校验 `length < 0` 与 `source == null && length != 0`，不校验 `length` 与真实缓冲容量的关系；随后 `lexer_at`（19-23）在 `unsafe` 块中按 `length` 直接索引，传错长度即越界读。`Project.krt:988` 写成 `KrtLexerInit(parser.lexer,(byte*)source.data,source.length);` 直接丢返回值，初始化失败时会以零值 lexer（source=0,length=0）继续，静默得到空 token 流而无诊断（对比 `CompilerPipeline.krt:345`、`Types.krt:472` 均有检查）。
- 最小修复：增加容量参数或让调用方保证并检查返回值；至少在所有调用点禁止忽略 `bool`。
- 备注：`KrtLexerWordEqual`（300-308）还把 `string` 强转 `byte*` 并按 `word_length` 读取，同样依赖调用方传入真实长度，属同类脆弱契约（内部字面量调用暂未触发）。

### [中] 87-8｜UTF-8 字节无处理、无校验，且与参考实现的静默策略分叉
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:129-136`（标点兜底）、`486-591`（`KrtLexerPunctuationKind`）
- 类别：未检查前置条件（UTF-8）/ 语义分叉
- 触发条件：源码在字面量/注释之外出现非 ASCII 字节，如中文标识符 `变量`、全角标点、孤立续字节。
- 后果：`lexer_letter`/`lexer_digit` 仅 ASCII（25-26），每个 ≥0x80 字节都落入 `KrtLexerPunctuationKind` 的 `Unknown` 并置错误 1；Re.KrtC `Tokenizer.c:798-820` 对 ≥0xC0 的前导字节按 UTF-8 长度跳过后续字节（其自身是静默丢弃），其余才报 `TOKEN_UNKNOWN`。两侧对同一输入的处理策略不同；SelfHost 既不支持非 ASCII 标识符，也不做 UTF-8 合法性校验（无效序列同样只报 error 1）。
- 最小修复：明确语言对非 ASCII 的立场；若不支持，统一在词法入口拒绝并给出准确诊断；若支持，按 UTF-8 解码标识符，且与参考实现策略对齐。
- 备注：参考实现的「静默跳过」本身也是缺陷，不主张照抄，但当前分叉会导致诊断噪声与恢复行为不一致。

### [中] 87-9｜`KrtTokenKind` 尾部别名枚举制造取值陷阱
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:896`
- 类别：契约破坏 / 语义分叉风险
- 触发条件：任何按名字比较 `KrtTokenKind` 的代码，尤其是 `token.kind == KrtTokenKind.Invalid`（本文件 68、102；`Project.krt:678`；`Driver.krt:1122`）。
- 后果：`Integer = 2` 与 `Number` 同值，`Floating = 2` 也与 `Number` 同值，`Character = 4` 与 `CharLiteral` 同值，`Invalid = 298` 与 `Unknown` 同值。于是 `kind == Floating` 对任何整数 token 都为真；`Invalid` 实际就是 `Unknown`，`Lexer.krt:68` 的「无效 token」判定依赖这个别名巧合才能成立。Re.KrtC `Tokenizer.h` 中没有这些别名（已核对 12–200 行枚举），`KrtTokenCategory` 另有独立取值（901）。一旦有人按名字增删枚举或调整顺序，68/102 行的控制流会静默改变。
- 最小修复：删除这四个别名，错误判定改用 `category == KrtTokenCategory.Invalid` 或 `error != 0`；如必须保留，给它们独立数值并加静态断言。
- 备注：与 87-14 的「数值对齐」注释直接冲突。

### [中] 87-10｜重复的自由函数五件套与恒真守卫构成死代码
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:943-953`（自由 `Reset/SetKind/SetCategory/SetError/SetLength`）；`102`（`if (token.kind != KrtTokenKind.Invalid)`）
- 类别：死代码
- 触发条件：任何阅读/维护该文件的路径；无运行时触发。
- 后果：943-953 与类方法 930-941 签名同构、语义重复，全库 grep 仅有定义、无任何调用（调用点全部使用 `token.SetXxx` 方法形式，如 46/58/72/75/92/108/132/137/138）。102 行守卫在数字分支内此时 `kind` 必为 `Number`，条件恒真，属永远成立的死分支。两者都增加「改了一处漏另一处」的维护风险。
- 最小修复：删除 943-953；102 行直接调用 `token.SetCategory(KrtTokenCategory.Floating);`。
- 备注：若自由函数是为外部 C 风格接口预留，应在注释中说明并补调用或测试，否则视为死代码。

### [中] 87-11｜裸指针 source + 仅存 span 的 token，共享可变状态无生命周期约束
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:3-8`（public 字段 `source/length/position/line/column`）、`19-23`（`lexer_at`）、`921-927`（token 只存 `start/length/line/column`）；消费方 `Driver.krt:1146`、`Types.krt:472`、`KroBackend.krt:2069`
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：源缓冲在词法产出 token 后被修改、复用或释放；或调用方直接改写 `lexer.source/position`。
- 后果：`source` 是裸 `int64` 指针且 public，无所有权、无不可变约束；token 不缓存文本，后续阶段再用 `lexer.source`/`file.original + token.start` 回读（如 `KroBackend.krt:2069` 还基于 `source+type.start` 建子 lexer）。缓冲一旦失效，已产出的 token 全部变成悬垂引用或读到错误文本，且无任何机制能检测到。Re.KrtC 用 `lexer_create/lexer_destroy` 与 `source_owned` 管理生命周期（`Tokenizer.h:209-227`），SelfHost 完全缺失等价物。
- 最小修复：由 lexer 持有源缓冲的不可变副本或引用计数；至少在文档中固化「源缓冲必须存活到编译结束」的契约，并提供重置/释放入口。
- 备注：当前调用链多为同生命周期栈对象，尚未见实际悬垂，但契约完全靠约定。

### [中] 87-12｜整数位宽解析存在两套实现，注释宣称的「语义精确」缺乏单一事实源
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:142-154`（`lexer_integer_type_bits`）与 `315-319`（`KrtLexerWordKind` 内联解析）
- 类别：阶段不对齐 / 配置漂移 / 注释与实现脱节
- 触发条件：维护者修改整数宽度规则（新增宽度、调整奇偶约束）时只改其中一处。
- 后果：两处都在解析 `u?int<偶数宽>`：142 用 `index + 3 >= end` 与循环内 `bits > 128` 提前返回 0；315 用 `index + 3 < end` 与循环条件 `width <= 128`，最后再交给 `KrtTokenIntegerType`。当前对合法输入结果一致（`Semantic.krt:106`、`Parser.krt:1625`、`test_lexer_vocabulary.py:59` 都依赖二者相等），但判据写法不同、边界路径不同，属典型双源漂移风险；141 行注释把 142 描述为「semantic precision」的来源，实际关键字识别走的是另一套代码。
- 最小修复：抽出唯一的 `lexer_integer_width_at(source,start,length)`，两处共用；141 行注释改为指向该唯一实现。
- 备注：`IntegerWidths.def` 是 C 侧事实源，SelfHost 枚举与解析均为手抄（见 87-13 关联风险）。

### [低] 87-13｜枚举中存在无产生路径的死成员：`Print` / `BitwiseOr` / `Attribute` / `Auto`
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:44`（Print=44）、`215`（BitwiseOr=215）、`251`（Attribute=251）、`292`（Auto=292）；产生端 `310-484`、`486-592`
- 类别：死代码 / 配置漂移
- 触发条件：词法产生 token 的任何路径。
- 后果：全库 grep `KrtTokenKind.Print|Auto|Attribute|BitwiseOr` 仅命中 `Lexer.krt:916` 的分类判断，无任何赋值/返回：关键字表无 `print`/`auto` 词条；`|` 固定返回 `Pipe`（542），永远不会产生 `BitwiseOr`；`@` 不在标点表，属性语法只能落到 `Unknown`+错误 1；`Attribute` 因此被分类函数引用却永不存在。Re.KrtC 同样缺产生路径（`Tokenizer.c` 关键字表与标点表已核对），属继承性死枚举，但 595 行注释宣称与 `Tokenizer.h` 对齐，未说明这些成员不可达。
- 最小修复：删除不可达成员（会改变枚举数值，需同步全库），或补齐产生路径并在注释中登记为保留字。
- 备注：若为将来语法预留，应集中放到单独的保留枚举并注明，避免污染当前 token 分类。

### [低] 87-14｜注释与实现脱节：无效 token 判定、数值对齐声明、NUMBER 语义说明三处失真
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:65`、`141`、`595`、`899-900`
- 类别：注释与实现脱节
- 触发条件：维护者按注释理解行为时。
- 后果：65 行称「An invalid token is recoverable; never leave the cursor stuck」，但 68 行判的是 `kind == KrtTokenKind.Invalid`，而该值是 `Unknown` 的别名（见 87-9），语义与注释不符；595 行称数值与 `Tokenizer.h` 完全一致，但 896 行追加了 C 侧不存在的四个别名；899-900 行称 NUMBER 的整数/浮点拼写与 Re.KrtC 对齐，实际 87-2/87-6 已列出多处分叉（八进制、后缀、指数与尾缀报错）。
- 最小修复：逐条改写注释使其与当前实现一致；对有意扩展（如 `.5`、`f/d` 后缀）在注释中显式登记，对未实现项（如 `\u`）标注限制。
- 备注：注释失真会直接误导后续移植与审计，优先级高于一般风格问题。

### [低] 87-15｜测试覆盖不足：换行、数值分叉、转义、宽度边界均未覆盖
- 位置：`Test/SelfHost/test_lexer_vocabulary.py:100-124`（现有 4 个用例）
- 类别：测试不足
- 触发条件：回归测试运行时。
- 后果：现有用例覆盖关键字与整数宽度词表、最长匹配、前缀/大小写、字面量与未知字节恢复；未覆盖：CRLF/孤立 CR 的行号列号（87-1）、`010`/`0128` 八进制切片（87-2）、`\u`/`\U`（87-3）、`1e`/`1f`/`1abc` 的错误策略（87-6）、`.5`（87-5）、未闭合注释后的连续 EOF 稳定性、`int2`/`int128`/`int130` 边界、超长标识符、错误码 1/2/3/5/6 的码值断言。上述已证实的分叉点 therefore 无回归保护，未来改动可静默退化。
- 最小修复：为每个已确认分叉补充用例，至少断言 `token.line/column`、数字 token 的 `start/length/kind/category/error`、转义扫描后的 `length` 与错误码。
- 备注：`test_lexer_vocabulary.py:59` 已对 `lexer_integer_type_bits` 与 token 宽度做一致性断言，可作为 87-12 的扩展锚点。

### [低] 87-16｜`KrtTokenIntegerType` 对非法位宽静默返回 `Eof`
- 位置：`SelfHost/Compiler/Frontend/Lexer/Lexer.krt:165-298`（出口 `297`）
- 类别：静默失败 / 契约破坏
- 触发条件：调用方传入不在 2..128 偶数集合的 `bits`，或传入奇数/超界值。
- 后果：函数不返回错误而是返回 `KrtTokenKind.Eof`（0），与「文件结束」这一合法 token 种类同值。调用方若只判断「返回值非 0」或直接把结果写入 `token.kind`，会把非法类型当成 EOF 或产生错误控制流。Re.KrtC `Tokenizer.h:234-235` 确实约定了「unsupported width 返回 TOKEN_EOF」，因此这是继承契约，但 SelfHost 侧没有任何断言或调用方检查（`KrtTokenIntegerBits`/`KrtTokenIsUnsigned` 亦无防御）。
- 最小修复：改为返回 `Unknown` 或增加 `bool* ok` 出参；至少在调用点断言 `bits` 为 2..128 的偶数后再调用。
- 备注：标「低」是因为当前调用链未发现误用；但作为公开 API 契约，静默返回 EOF 属危险设计。
