# 第七十八批：SelfHost/Compiler/Frontend/Parser/Parser.krt（1-1213）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Frontend/Parser/Parser.krt`，仅第 1-1213 行（分三段读取 1-400 / 401-820 / 821-1220，结论只落在 1-1213）。
- 覆盖内容：AST 节点与工厂（5-196）、声明前瞻（198-426，含 Fork/Commit/TypeCloser/各类 Ahead/类型参数）、名称与作用域工具（428-671，KrtName*/KrtScope*/KrtDeclarationMatches/AppendSyntax）、using 作用域（673-718）、Parser 状态与诊断（720-773）、for 增量与约束（775-845）、错误恢复（847-901）、函数类型/全局变量/基类型/基构造器（897-1011）、修饰符（1013-1038）、实参与 lambda 前瞻（1040-1088）、lambda 解析（1090-1165）、ParsePrimary 前半（1167-1213）。
- 交叉核对方式：仅用 grep 核对符号，未整文件通读其他源文件。已核对：`KrtParserTypeSpanLength`（1666-1670，确实从尾部剔除空格/制表/换行）、`KrtParserOperator`（1462）、`KrtParserParseExpression`（1570，体内无 `return null`）、`KrtParserParseCompilationUnit`（1960-1978）、`KrtParserParseBlock`（1635-1659）、Lexer 的 `Rshift`（Lexer.krt:531）、`KrtTokenKind.Lambda`（Lexer.krt:847）、CompilerPipeline 的数组释放（CompilerPipeline.krt:308-312）、Re.KrtC 的 `CompilerError.c:145` 与 `ParserStatement.c`。
- 未覆盖：1214-2424 行（表达式/语句/类型声明/函数体/编译单元主体）。

## 问题条目

### [中] 78-1｜顶层声明数量上限 16384 截断解析，且只报通用错误
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:473`（退出后 `:545` 返回；上游检测在 `:1978`）
- 类别：静默失败（诊断信息缺失）/ 配置漂移
- 触发条件：单个编译单元的顶层声明数超过 16384。
- 后果：`while (... && declaration_count < 16384)` 提前退出，剩余声明不再进入 AST；`KrtParserParseCompilationUnit`（1978）只以 `KrtParserRecordError` 报一条通用 `invalid syntax`，不说明是数量上限；若发生在嵌套命名空间内，则报的是 544 的「expected closing namespace brace」，位置与原因都误导。
- 最小修复：达到上限时记录带上限信息的诊断（如 `KrtParserRecordExpected(parser, KrtTokenKind.Eof, "declaration limit exceeded")`）并返回 false；上限值集中定义为常量。
- 备注：C 版 Parser 无此上限（疑似分叉）。同文件的 `unit.child_count > 16384`（1978）说明该上限在别处是会诊断的，此处诊断质量不一致。

### [中] 78-2｜诊断达到 64 条后静默丢弃
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:751`
- 类别：静默失败 / 诊断缺失
- 触发条件：同一 `KrtParser` 累计错误数达到 64。
- 后果：`KrtParserRecordExpected` 直接 `return`，第 65 条及以后的错误不再进入 `diagnostics` 链，`parser.errors` 停在 64；调用方无法感知被丢弃的错误，最终诊断列表不完整。
- 最小修复：上限处追加一条「too many errors」汇总诊断，或继续递增 `errors`、只停止追加明细。
- 备注：C 版 `Re.KrtC/src/Compiler/Frontend/CompilerError.c:145` 按容量动态扩容，无固定 64 上限（疑似分叉）。

### [低] 78-3｜诊断去重只比较位置，吞掉同位置的不同诊断
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:751`（`parser.diagnostic_tail.position == parser.token.start`）
- 类别：诊断缺失
- 触发条件：同一位置产生两条 `expected` 不同的错误；或 fork/commit 回退到同一位置后再次报错。
- 后果：第二条诊断被丢弃，真实原因被掩盖；probe 的 `errors` 计数偏小（调用方多以布尔判断，影响有限）。
- 最小修复：去重时同时比较 `expected` 与 `message`，或仅在完全相同时跳过。
- 备注：与 78-2 同一行但机制不同，独立条目。

### [中] 78-4｜`KrtParserParseFunctionType` 错误路径泄漏参数数组
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:910-912`（分配），`:920/:930/:934/:936/:940`（返回 null）
- 类别：资源与生命周期（AST 节点所有权/泄漏）
- 触发条件：函数类型解析失败，如 `fn(int,`、缺少 `->`、参数类型或返回类型写错。
- 后果：`signature` 节点被丢弃，其 `parameter_type_starts/lengths/refs`（各 `new int64[128]`/`new bool[128]`）无人释放；`CompilerPipeline.krt:308-312` 只遍历从根可达的节点，被丢弃节点上的数组会一直占用。
- 最小修复：错误返回前 `delete` 三个数组并清 `owns_parameter_*`，或把 `signature` 挂到 `Invalid` 节点上保证可达。
- 备注：对比 `KrtParserParseLambda`（1095-1098 分配，出错时返回 lambda 节点，仍可达），此处返回 null 是确定性的泄漏点。

### [中] 78-5｜函数类型参数数组固定 128，溢出后继续解析并丢弃多余参数
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:914-926`
- 类别：语义分叉 / 配置漂移
- 触发条件：函数类型参数个数超过 128。
- 后果：第 129 个起参数仍被解析（消耗 token）但不进入 `syntax_parameter_types` 与索引数组，`parameter_count` 停在 128；只报告一次错误，调用方可能基于被截断的签名继续语义分析。
- 最小修复：改为动态扩容（与 C 版 `ParserStatement.c` 的 `param_capacity` 一致），或溢出时直接失败。
- 备注：与 78-6 是两处独立实现，故分列。

### [中] 78-6｜lambda 参数数组固定 128，溢出后继续解析并丢弃多余参数
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:1101-1142`
- 类别：语义分叉 / 配置漂移
- 触发条件：lambda 参数个数超过 128。
- 后果：第 129 个起参数被解析但不进入 `syntax_parameter_types` 与 `parameter_*` 索引数组，`parameter_count` 停在 128；只报告一次错误。
- 最小修复：动态扩容，或溢出时直接失败并停止解析。
- 备注：与 78-5 同理，独立实现。

### [低] 78-7｜`KrtParserFork` 不复制 `type_depth`、`KrtParserCommit` 不回写 `last_type`
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:200-212`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：外层类型深度已接近 64 时做前瞻（`KrtParserLocalTypeAhead`、`KrtParserTypeDeclarationAhead`、`KrtParserCastTypeAheadDepth` 等）。
- 后果：probe 的 `type_depth` 从 0 重新计数，64 层嵌套上限被绕过（最坏约 64+64 层）；commit 后 `parser.last_type` 仍是旧值，若后续代码依赖它会取到陈旧类型。
- 最小修复：fork 时复制 `type_depth`（probe 从该值继续），commit 时回写 `last_type`。
- 备注：1-1213 内暂无调用方在 commit 后读取 `last_type`，故当前影响有限；注释（199）只承诺 probe 拥有 lexer/token，未说明状态继承范围。

### [低] 78-8｜`KrtParserParseTypeArguments` 的跨度未剔除尾部空白
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:241`
- 类别：契约破坏 / 阶段不对齐
- 触发条件：`>` 之后、下一个 token 之前有空白或注释。
- 后果：`arguments.length/type_length` 用 `parser.token.start - start` 计算，包含尾部空白；而 `KrtParserParseType`（300）用 `KrtParserTypeSpanLength`（1666-1670 会剔除尾部空格/制表/换行）。两处跨度口径不一致，依赖 `type_length` 的诊断定位可能偏移。
- 最小修复：改用 `KrtParserTypeSpanLength(parser, start, parser.token.start - start)`。
- 备注：同类写法还见于 `:485`、`:493` 的 `parser.token.start - start`。

### [低] 78-9｜`KrtAstIsLeaf` 全项目无调用（死代码）
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:193-196`
- 类别：死代码
- 触发条件：任何解析路径都不会调用它。
- 后果：无用函数留存，增加维护面；全项目 grep `KrtAstIsLeaf` 仅此定义一处。
- 最小修复：删除该函数，或接入实际使用点。
- 备注：无行为影响。

### [低] 78-10｜文件作用域命名空间边界 `boundary.value=1` 写而不读（死状态）
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:485`
- 类别：死代码 / 注释与实现脱节
- 触发条件：解析 `namespace;` 时总是执行。
- 后果：`boundary.value=1` 未被任何代码读取（全项目 `NamespaceDeclaration` 仅在 Parser.krt 出现，`.value == 1` 的读取点都是 `native_type`/`type`/`expression`）；同时 `KrtParserAppendSyntax`（663-671）只复制 kind/start/length，引用节点上也带不上该标记。
- 最小修复：若需要标记文件作用域命名空间，应在引用节点上复制 `value` 并提供读取方；否则删除该赋值。
- 备注：无行为影响，属实现残留。

### [中] 78-11｜文件作用域命名空间的 using 作用域父链为 null，丢弃此前 using
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:484`（对比块命名空间 `:496`）
- 类别：语义分叉 / 契约破坏（疑似）
- 触发条件：`using A;` 出现在 `namespace;` 之前（同一文件）。
- 后果：`namespace;` 分支用 `KrtParserUsingScope(null, parser.native_scope, ...)`，父链为 null；而块命名空间用 `KrtParserUsingScope(previous_using, path, ...)` 继承父作用域。于是文件作用域命名空间内的代码看不到此前文件级 using 导入，与块命名空间行为不一致。
- 最小修复：file-scoped 分支改用 `KrtParserUsingScope(parser.current_using_scope, parser.native_scope, ...)`。
- 备注：疑似。若语言规则要求 `namespace;` 之前不得有 using，则不成立；需与语义/语言规范确认。

### [低] 78-12｜全局 using 节点未标记 `native_using_scope`
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:520-525`（`continue` 跳过 539-540；对比局部 using `:717`）
- 类别：错误处理不一致 / 契约破坏（疑似）
- 触发条件：文件级 `using A;` / `using A = B;`。
- 后果：`using_node` 及其 `left`（imported）没有 `native_using_scope`；局部 using（713-717）会设置 `directive.native_using_scope`。若后续阶段用该字段区分导入来源，全局与局部导入表现会不同。
- 最小修复：在 520 前对 `using_node` 调用 `KrtParserMarkUsingScope`，或统一由 `KrtParserAppendSyntax` 的引用承载并移除 717 的重复设置。
- 备注：疑似。`KrtNativeUsingMatches`（Semantic.krt:1575-1578）主要用 `type_length/name_start`，未直接依赖该字段。

### [低] 78-13｜`KrtParserSynchronize` 的括号计数在异常嵌套下变负
- 位置：`SelfHost/Compiler/Frontend/Parser/Parser.krt:878-882`
- 类别：错误处理不一致 / 未检查前置条件
- 触发条件：错误恢复时，`}` 出现在尚未闭合的 `(` 或 `[` 内（如 `foo(}`）。
- 后果：顶层判断（872）要求 `braces==0 && parens==0 && brackets==0` 才拦截 `}`，因此 `}` 会走 878 分支把 `braces` 减成 -1；之后 `braces == 0` 的恢复条件永不成立，恢复过程可能一路吃到文件尾，延后后续诊断。
- 最小修复：在 878 分支加 `if (braces > 0)` 保护，或让 `}` 在 `parens>0 || brackets>0` 时只计数不参与 braces 归零判定。
- 备注：仅影响出错后的恢复质量，不影响正确输入的解析。
