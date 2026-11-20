# 第七十九批：SelfHost/Compiler/Frontend/Parser/Parser.krt（1214-2425）

## 覆盖范围
- 只读 `SelfHost/Compiler/Frontend/Parser/Parser.krt` 第 1214-2424 行（文件实际共 2424 行，任务给到 2425，末行无内容），分段读取。
- 符号核对仅用 grep 定点查签名/调用点，未整文件通读其他源文件；`Lexer.krt`、`KrtAstMake`、`KrtParserFork/Commit` 等内部实现未展开，相关结论标「疑似」。
- 与 `Re.KrtC/src/Compiler/Frontend/Parser/*.c` 做过定点对照：C 版无同名 `FinishGuard/NullConditional/native_size += 8/16384` 等实现，结构差异大，无法逐条比对，故凡依赖下游约定或词法器行为的均标「疑似」。
- 本段主要内容：ParsePrimary 尾部（new/数组/括号/字面量）、FinishGuard、ParsePostfix、Unary/Binary/Expression、Block、Function/FunctionTail、TypeDeclaration、GlobalConstant、CompilationUnit、Statement、InitializeConstructors、Body/CaseBody。

## 问题条目

### [高] 79-1｜字符字面量未知转义被静默接受
- 位置：`Parser.krt:1305-1323`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：字符字面量使用未识别转义，如 `'\q'`、`'\xZ'`、`'\x1Z'`、`'\nX'`。
- 后果：`value` 直接取 `source[token.start+2]`（`'q'`→113），或十六进制循环遇非十六进制字符 `break` 后保留部分值（`'\xZ'`→0、`'\x1Z'`→1），全程无诊断，生成错误常量并继续编译。
- 最小修复：未知转义分支与十六进制 `lexer_hex_digit < 0`/零位数分支均调用 `KrtParserRecordError(parser)`。
- 备注：多字符字面量 `'ab'` 也只会取首字符；`'\x'` 合法路径本身同样无诊断。

### [中] 79-2｜字符字面量解析未校验 token.length，可能越界读
- 位置：`Parser.krt:1306-1308`（另见 1346 浮点后缀同类写法）
- 类别：8 未检查前置条件
- 触发条件：Character token 长度小于 2（如未闭合引号被词法切成只剩引号的 token），或引号位于源缓冲末尾。
- 后果：`source[token.start+1]`、`source[token.start+2]` 越界读，读到缓冲外或错误值。
- 最小修复：解析前校验 `token.length >= 2`（转义分支 `>= 3`），否则 `RecordError` 并返回 Invalid。
- 备注：疑似，取决于词法器对未闭合字符字面量的切分；未读 `Lexer.krt`。

### [高] 79-3｜构造器初始化器与字段共享同一 AST 节点
- 位置：`Parser.krt:2382-2389`（`KrtParserInitializeConstructors`）
- 类别：7 共享可变状态 / 10 资源与生命周期 / 3 语义分叉
- 触发条件：类型含非 static 字段且字段带初始化器（`field.left != null`），且存在构造器。
- 后果：`assignment.right = field.left` 直接复用字段初始化器节点，同一节点同时挂在字段与合成构造器体两处；后续遍历/改写可能重复初始化、双重释放或漏改一处。
- 最小修复：为构造器体克隆初始化器子树，或把 `field.left` 移入构造器并清空字段引用，明确唯一所有权。
- 备注：疑似（取决于下游是否复制/是否释放共享节点）；本段未确认 `KrtAstMake` 分配器语义。

### [中] 79-4｜try/catch 错误路径直接返回 try 体，丢弃 Try 语义
- 位置：`Parser.krt:2085、2088、2096`
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：catch 子句格式错误（缺标识符、`ParseType` 返回 null、缺右括号）。
- 后果：`return body` 把 Try 节点及已解析的 catch/finally 全部丢弃；错误虽记录，恢复后的 AST 变成「无保护地执行 try 体」。
- 最小修复：返回带错误标记的 Try 节点（保留 body/catches/finally），或统一返回 Invalid。
- 备注：2088 的 `caught_type == null` 分支未额外 `RecordError`，若 `ParseType` 不报错则也为静默。

### [中] 79-5｜Try 节点 child_count 在 catches==null 时多算
- 位置：`Parser.krt:2109`
- 类别：6 契约破坏
- 触发条件：`try { } finally { }`（catches==null 且 finally!=null），或 `try { }`（两者皆 null）。
- 后果：child_count 分别为 3/2，但非空子节点只有 2/1（`right==null`），下游按 child_count 遍历会越界或解引用 null。
- 最小修复：`result.child_count = 1 + (catches != null ? 1 : 0) + (finally_body != null ? 1 : 0);`
- 备注：与 2101 的 `variable == null ? 1 : 2` 写法对比，可见此处遗漏了 catches 判空。

### [中] 79-6｜Throw 无操作数时 child_count 与 Return/Yield 不一致
- 位置：`Parser.krt:2043-2044`
- 类别：6 契约破坏 / 9 错误处理不一致
- 触发条件：`throw;`（无操作数）。
- 后果：`left==null` 但 `child_count=1`；Return（2294）与 Yield（2061）在空操作数时均用 0，Throw 不一致，下游可能解引用 null。
- 最小修复：`result.child_count = value == null ? 0 : 1;`
- 备注：无。

### [中] 79-7｜后缀表达式错误返回 node，丢失 `?.` 守卫链
- 位置：`Parser.krt:1383、1394、1396、1401、1441`（对照正常出口 1459）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：表达式带 `?.` 守卫链（`guarded != null`）后，调用缺 `)`、`::` 后非标识符、`.` 后非标识符、下标缺 `]`。
- 后果：这些分支 `return node`，而 1459 用 `KrtParserFinishGuard(guarded, node)`；返回的是丢失守卫链的底层节点，`a?.b(` 被恢复成 `b(` 语义。
- 最小修复：错误分支统一 `return KrtParserFinishGuard(guarded, node);`。
- 备注：`?.` 分支 1425/1431 返回 guarded 但仍有长度问题，见 79-8。

### [中] 79-8｜`?.` 错误路径未调用 FinishGuard，guard.length 保持 0
- 位置：`Parser.krt:1425、1431`（对照 1459）
- 类别：9 错误处理不一致 / 13 注释与实现脱节
- 触发条件：`?.[` 缺 `]`，或 `?.` 后跟非标识符。
- 后果：直接 `return guarded`，未调用 `KrtParserFinishGuard`，所有 NullConditional 节点 length 仍是构造时的 0；下游按 span 处理会得到零长度守卫。
- 最小修复：改为 `return KrtParserFinishGuard(guarded, node);`，或在返回前补齐 length。
- 备注：正常路径 1459 会修复 length，仅错误路径遗漏。

### [中] 79-9｜CompilationUnit 尾部把两种错误合并，且 16384 为魔数
- 位置：`Parser.krt:1978`
- 类别：2 诊断缺失 / 11 配置漂移 / 1 静默失败
- 触发条件：解析完声明后 `token.kind != Eof`（尾部多余 token），或 `unit.child_count > 16384`。
- 后果：两种不同情况共用一句无消息 `RecordError`；尾部多余 token 不同步、被静默丢弃；16384 为魔数，且 child_count 含展平的方法数，限值口径不清。
- 最小修复：拆分两个判断并给出具体消息；`token != Eof` 时同步/跳过尾部；把 16384 提为命名常量并注明计数口径。
- 备注：无。

### [中] 79-10｜字段布局固定 +8，忽略类型宽度与数组长度
- 位置：`Parser.krt:1910-1911`
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：结构体/类含多个不同类型或数组成员。
- 后果：每个非 static 字段 `native_offset` 按固定 +8 递增，忽略类型大小、数组 rank/size 与对齐，布局与实际类型不符。
- 最小修复：按类型大小/对齐计算偏移（含数组），或在语言层面明确所有字段为 8 字节槽并写入注释/契约。
- 备注：疑似（可能语言规定统一 8 字节槽位）；C 版未找到同名逻辑可对照。

### [中] 79-11｜Block 仅在「已出错」后对函数声明提前收尾，恢复行为不一致
- 位置：`Parser.krt:1644`（后续 1659 再记一次错误）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：块内已产生错误（`parser.errors > block_initial_errors`）后遇到函数声明。
- 后果：记录「expected closing block brace before declaration」并 break，随后 1659 再记一次错误；块被提前终止，后续语句改归外层作用域。类型声明无此分支，恢复行为不一致。
- 最小修复：统一恢复策略；若嵌套函数/类型非法，应在无错误时也报同一错误，避免双重诊断与作用域漂移。
- 备注：疑似（若嵌套函数非法，也应无条件报错而非仅由错误状态触发）。

### [低] 79-12｜KrtParserTokenText(42) 在谓词中改写词法器状态
- 位置：`Parser.krt:1595`（`KrtParserSplitStars` 1587-1592）
- 类别：7 共享可变状态 / 13 注释与实现脱节
- 触发条件：任何以 `expected == 42` 调用 `KrtParserTokenText` 且当前 token 为 Power。
- 后果：谓词内部调用 `KrtParserSplitStars`，改写 `token.kind/length` 并回退 `lexer.position/line/column`；查询带副作用，隐含「调用方必须消费 `*`」的契约，与 1585-1586 注释「stars 另行消费」相悖。
- 最小修复：把拆分移到显式消费点（ParseUnary/ParsePrimary），`KrtParserTokenText` 只做只读判断。
- 备注：本段内 2299 调用后必解析表达式，暂未发现可观察错误，属脆弱设计。

### [低] 79-13｜`global::name` 复用 Identifier 节点，name 字段未同步
- 位置：`Parser.krt:1391-1397`
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：使用后缀 `global::name` 路径。
- 后果：复用 Identifier 节点且只改 `length` 覆盖 `global::name`，未更新/新建 name 信息；若下游按 `name_start/name_length` 取标识符（Member 节点即如此），只能得到 `global` 或默认值。
- 最小修复：生成限定标识符节点（更新 name_start/name_length），或明确约定 Identifier 只用 start/length 并统一所有节点。
- 备注：疑似；普通 Identifier 在 1299 也未设 name_start/name_length，需下游约定一致。

### [低] 79-14｜枚举显式负值取负可能溢出
- 位置：`Parser.krt:1828`
- 类别：8 未检查前置条件（整数溢出）
- 触发条件：枚举显式赋值为 `-9223372036854775808`（`constant.left.value == INT64_MIN`）。
- 后果：`-constant.left.value` 有符号溢出（UB/回绕），枚举值错误。
- 最小修复：用无符号取负，或显式检查 INT64_MIN 并报错。
- 备注：无。

### [低] 79-15｜1868 的 `method.kind != Function` 为死代码
- 位置：`Parser.krt:1868`
- 类别：5 死代码
- 触发条件：任何类型成员方法解析。
- 后果：`KrtParserParseFunctionTail` 所有路径均返回 Function 节点 `unit`，该条件恒为假，分支不可达。
- 最小修复：删除该检查；若确需保留，应让 `ParseFunctionTail` 在错误时返回 Invalid。
- 备注：无。

### [低] 79-16｜函数关键字声明时 return_type 为 null 仍赋给 syntax_type
- 位置：`Parser.krt:1706`
- 类别：6 契约破坏 / 8 未检查前置条件
- 触发条件：使用函数关键字（`keyword_function == true`，如 `function foo()`）声明。
- 后果：`return_type` 保持 null 并赋给 `unit.syntax_type`；下游若假设返回类型非空会解引用 null。
- 最小修复：关键字声明时补默认返回类型（void/inferred），或在使用处判空。
- 备注：疑似；未确认 `KrtParserFunctionKeyword` 匹配集合与下游约定。

### [低] 79-17｜ParseStatement 兜底在 Eof 上仍调用 KrtLexerNext
- 位置：`Parser.krt:2329-2331`
- 类别：8 未检查前置条件
- 触发条件：`ParseBody` 在 Eof 上调用 `ParseStatement`（如 `if (x)` 后文件结束）。
- 后果：兜底分支仍 `KrtLexerNext` 消费 Eof；若词法器不兜底会越界或状态异常。
- 最小修复：兜底中先判 Eof，直接返回 Invalid 且不 Next。
- 备注：疑似；未读 `Lexer.krt` 的 Eof 行为。

### [低] 79-18｜非 extern/非 abstract 函数以 `;` 结束被静默当作声明
- 位置：`Parser.krt:1755-1759`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：非 extern、非 abstract 函数以 `;` 结束（无函数体）。
- 后果：被当作 `op=223` 声明静默接受，无「缺少函数体」诊断；与 abstract 必须显式处理不同。
- 最小修复：仅允许 extern/abstract/接口成员省略体，否则 `RecordError`。
- 备注：疑似（语言可能允许原型声明）。

### [低] 79-19｜字段数组未递增 syntax_array_rank，与局部变量不一致
- 位置：`Parser.krt:1915-1923`（对照 2260）
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：类型字段声明数组（如 `int a[4];`）。
- 后果：只设置 `field.syntax_array_size`，未递增 `syntax_array_rank`；局部变量在 2260 递增 rank，字段与局部数组元数据不一致。
- 最小修复：字段数组同样递增 `field.syntax_array_rank`，或统一由 size 推导 rank。
- 备注：疑似；需确认下游是否依赖 rank。

### [低] 79-20｜整数字面量未检查 lexer_hex_digit 负值
- 位置：`Parser.krt:1329`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：Integer token 中出现 `lexer_hex_digit` 返回负值的字符（如允许的数字分隔符/后缀被并入 token）。
- 后果：`digit_value` 为负被当作 uint64 加入 `value`，整数字面量静默变成巨大错误值，无诊断。
- 最小修复：`if (digit_value < 0) { KrtParserRecordError(parser); break; }`。
- 备注：疑似；`Project.krt:749、753` 对同一 `lexer_hex_digit` 判了 `< 0`，提示该函数确实可能返回负值。
