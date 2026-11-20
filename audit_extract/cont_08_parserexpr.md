# 第六十五批：ParserExpression.c + ParserAdvanced.c

## 覆盖范围

- 全量读取（分段 read，非通读其他源文件）：
  - `Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（898 行，分 4 段读完）
  - `Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c`（266 行，一次读完）
- 契约核对（仅 grep 特定符号 / 读相关头文件少数行，未整文件通读）：
  - `ParserBase.h` / `ParserBase.c`：`KrtParserAlloc`、`KrtParserStrdup`、`KrtParserCreateNode`、`parser_advance`、`parser_report_error`、`parser_is_type_keyword`
  - `Ast.h` / `Ast.c`：ASTNode 字段、`ast_destroy_node`、`free_string_array`、lambda/call 析构
  - `Tokenizer.h` / `Tokenizer.c`：token 枚举、`token_free`、`lexer_read_identifier`、关键字 value、`lexer->arena`
  - `PointerTypes.inc`：`KrtParseNamedType`、`KrtParseSourceType`
  - `KrtCommon.h`：`KRT_MALLOC/KRT_STRDUP/KRT_FREE/KRT_COMPILE_ERROR`
  - `Arena.c` / `Allocator.c`：arena 指针非独立堆块、`KrtSafeFree` 的 INVALID_FREE 行为
  - `Parser.c` / `CompilerPipeline.c`：错误计数契约（`error_count` 只在 pipeline 层检查）
- 关键结论：`KrtParserAlloc`（arena）在 OOM 时 `KRT_COMPILE_ERROR` 直接 `exit(1)`，因此 `KRT_PARSER_STRDUP`/`KrtParserCreateNode` 的 NULL 分支基本是死代码；G1 的“分配失败返回 NULL 而不 exit”真实适用于 `KRT_MALLOC/KRT_STRDUP/KRT_REALLOC`（`KrtSafeMalloc/KrtSafeStrdup/KrtSafeRealloc`），本批中集中在 `ParserAdvanced.c`。

统计：共 32 条问题（致命 5、严重 15、一般 12）。

## 问题条目

### [致命] 65-1｜表达式解析失败普遍不报告诊断，error_count 不增长导致流水线静默继续
- 位置：`ParserExpression.c:100-102`、`107-109`、`185-192`、`196-208`、`247-254`、`396-399`、`438-447`、`483-489`、`521-523`、`570-572`、`593-604`、`618`、`786-789`、`807-811`、`860-869`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：实参列表、`sizeof`、lambda、括号表达式、数组字面量、`new`、成员/下标后缀、三元等任意语法错误。
- 后果：这些路径直接 `return NULL`，未调用 `parser_report_error`；`parser->error_count` 保持 0。`CompilerPipeline.c:300` 只检查 `parser->error_count > 0`，`Parser.c:26-30` 失败时仅 `parser_advance` 一格后继续，于是残缺 AST 被当作合法输入进入语义分析/代码生成，或把一个错误扩散成多条无关错误。
- 最小修复：为每个失败分支补 `parser_report_error(parser, line, col, "expected ...")`；或在 `parser_parse_call`/`parser_parse_primary` 的统一 cleanup 处集中报告。
- 备注：本文件最系统性的问题；`736` 行的 `"Expected right operand of binary operator"` 是少见的正面例子。

### [致命] 65-2｜`as` / `??` / `?.` / `?:` 有词法 token 但表达式语法完全不处理
- 位置：`ParserExpression.c:625-662`（优先级表无这些 token）、`758-846`（后缀无 `?.`）、`848-890`（三元无 `?:`）
- 类别：5 死代码 / 3 语义分叉
- 触发条件：`x as T`、`a ?? b`、`a?.b`、`a ?: b`。
- 后果：`get_operator_precedence` 对 `TOKEN_AS/TOKEN_NULL_COALESCING/TOKEN_QUESTION_DOT/TOKEN_QUESTION_COLON` 一律返回 0，解析在操作符处静默停止，剩余 token 滞留；`Parser.c:26-30` 每次只前进一格，导致后续解析错位或静默丢语句。`AST_AS_EXPRESSION` 在 `Ast.h:90`、`AstVisit.inc:312`、`IrGen.c:2192` 都有处理，但没有任何解析路径创建它（grep 全 Parser 目录无 `TOKEN_AS`）。
- 最小修复：在优先级表/后缀/三元中补齐 `as`、`??`、`?.`、`?:`，或明确删除这些 token 与下游分支。
- 备注：与主会话交叉验证的优先级表属同一完整性问题。

### [致命] 65-3｜深嵌套表达式递归无深度上限
- 位置：`ParserExpression.c:392`（括号）、`418`（数组元素）、`520/548/597`（new）、`802`（下标）、`859/872`（三元）、`705→669→734`（二元/一元互递归）
- 类别：8 未检查前置条件（递归无界）
- 触发条件：源码中出现大量嵌套括号/数组/调用/三元，例如数万个 `(`。
- 后果：`parser_parse_expression ↔ parser_parse_primary ↔ parser_parse_postfix_expression ↔ parser_parse_call` 互相递归，无深度计数，直接栈溢出崩溃，且没有任何诊断。
- 最小修复：在 `Parser` 增加 `parse_depth`，进入表达式解析时 +1，超过阈值（如 256）时报错返回。
- 备注：属于“深嵌套递归”专项。

### [致命] 65-4｜命名空间 parts 元素 `KRT_STRDUP` 未检查，后续 `strlen/strcat` 空指针崩溃
- 位置：`ParserAdvanced.c:184`、`196`、`204`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`namespace A.B.C` 且 `KRT_STRDUP` 因内存压力返回 NULL（G1：`KrtSafeStrdup` 失败返回 NULL 而不 exit）。
- 后果：`parts[part_count++] = NULL`；`196` 行 `strlen(parts[i])` 直接空指针解引用崩溃；即便绕过，`204` 行 `strcat(full_name, parts[i])` 同样崩溃。
- 最小修复：检查 `KRT_STRDUP` 返回值，失败走统一 cleanup 并 `parser_report_error`。
- 备注：`parts` 是 `KRT_MALLOC`（157）、`KRT_REALLOC`（173），均属 G1 活路径。

### [致命] 65-5｜类名 `KRT_STRDUP` 未检查且失败静默、token 已前进
- 位置：`ParserAdvanced.c:25`、`31-33`
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`class Foo {}` 且堆分配失败。
- 后果：`name = NULL`，`31` 行直接 `return NULL`，无诊断；且 `26` 行已 `parser_advance`，错误位置与状态不一致，error_count 不增长。
- 最小修复：失败时 `parser_report_error` 并保证 token 状态可恢复。
- 备注：与 65-4 同属 G1 的 `KRT_STRDUP` 活路径。

### [严重] 65-6｜优先级表把一元 `TOKEN_NOT` 同时当作二元运算符
- 位置：`ParserExpression.c:657-658`（prec=11）、`702`（一元前缀列表）、`713-734`（二元循环）
- 类别：3 语义分叉
- 触发条件：`a ! b` 之类 `!` 出现在表达式中间（`a != b` 是 `TOKEN_NOT_EQUAL`，不受影响）。
- 后果：`!` 既在一元前缀列表又在优先级表中，`a ! b` 被解析为 `AST_BINARY_OPERATION(operator=TOKEN_NOT)`，右操作数按 prec 12 解析；下游语义/IR 未定义该二元运算，产生与 `!(a,b)` 预期不符的树。
- 最小修复：从 `get_operator_precedence` 移除 `TOKEN_NOT`，或在一元/二元语义间显式区分（如仅允许前缀并在此处拒绝）。
- 备注：与主会话交叉验证的优先级表（:620-657）直接相关。

### [严重] 65-7｜优先级表缺 `TOKEN_POWER`，且 `**` 在词法/语法间语义重载
- 位置：`ParserExpression.c:625-662`（无 `TOKEN_POWER` 分支）、`333-344`/`366-367`（指针深度 2）、`673-684`/`700-704`（前缀双重解引用）
- 类别：6 契约破坏 / 3 语义分叉
- 触发条件：表达式中出现 `**`（`a ** b`）。
- 后果：`get_operator_precedence(TOKEN_POWER)=0`，`a ** b` 在 `a` 处停止，剩余 token 滞留；而 `**` 又被当作“指向指针"/双重解引用。词法（`**`）与语法（仅前缀/类型）契约不一致，既非合法二元运算也不报错。
- 最小修复：明确 `**` 的唯一语义；若仅作指针构造，应在主循环显式拒绝并给诊断，而不是静默停止。
- 备注：与主会话交叉验证的 TOKEN_POWER 段落直接相关。

### [严重] 65-8｜lambda 参数直接保存 lexer 词素裸指针，跨 arena 生命周期
- 位置：`ParserExpression.c:240`
- 类别：10 资源与生命周期 / 6 契约破坏 / 7 共享可变状态
- 触发条件：解析 `function(x, y) => ...`。
- 后果：`params[i] = parser->current_token.value` 指向 `lexer->arena`（`Tokenizer.c:326/340`），而其它所有 AST 字符串用 `KRT_PARSER_STRDUP`（`parser->arena`）。两个 arena 分别由 `lexer_destroy` 与 `parser_destroy` 释放（`CompilerPipeline.c:101-105`），生命周期/顺序变化即悬垂；`AST_LAMBDA_EXPRESSION` 的析构 `free_string_array` 会对该指针 `KRT_FREE`（`Ast.c:393`），而它不是独立堆块 → INVALID_FREE（`KRT_DEBUG` 下 `debugbreak`）。
- 最小修复：`params[param_count++] = KRT_PARSER_STRDUP(parser->current_token.value);`
- 备注：对比 `EnumParser.inc:95`、`ParserExpression.c:276/294` 等均采用复制。

### [严重] 65-9｜`global::` 拼接用 512 字节栈缓冲并无条件覆盖原指针
- 位置：`ParserExpression.c:774-776`
- 类别：8 未检查前置条件 / 10 资源与生命周期 / 1 静默失败
- 触发条件：`global::<标识符>` 且 `strlen("global::")+strlen(标识符) ≥ 512`。
- 后果：`snprintf` 静默截断生成错误标识符；`primary->data.identifier_name = KRT_PARSER_STRDUP(tmp)` 直接覆盖旧值（`"global"`）而不释放，造成泄漏；无诊断。
- 最小修复：按 `strlen` 用 `KRT_PARSER_ALLOC_FROM_ARENA` 分配，或先释放旧指针；截断时报错。
- 备注：属于“临时缓冲区截断”专项。

### [严重] 65-10｜`is` 表达式分支泄漏 `left`/`node`、未检查 `node`、失败无诊断
- 位置：`ParserExpression.c:720-733`
- 类别：10 资源与生命周期 / 8 未检查前置条件 / 2 诊断缺失
- 触发条件：`x is UnknownType`（`KrtParseSourceType` 返回 false）。
- 后果：`721` 创建 node 后 `722` 立即解引用（未检查）；`723` 失败时 `return NULL`，`left` 已被 node 持有、node 被遗弃，均不回收；无 `parser_report_error`（依赖 `KrtParseSourceType` 内部报告，若其走 `return false` 分支则完全静默）。
- 最小修复：`if (!node) { ast_destroy_node(left); return NULL; }`；`KrtParseSourceType` 失败时 `ast_destroy_node(left)` 并报告。
- 备注：与 `735-739` 的二元操作数诊断不一致。

### [严重] 65-11｜`new` 表达式多条错误路径泄漏 class_name/size/args 且无诊断
- 位置：`ParserExpression.c:518-533`、`539-574`、`588-616`
- 类别：10 资源与生命周期 / 2 诊断缺失
- 触发条件：`new Foo[` 缺 `]`、`new Foo(` 参数错误/缺 `)`、`new int[` 缺 `]`、`new` 后既非标识符也非类型关键字。
- 后果：`class_name`（`KrtParseNamedType`）、`size`、`args` 被遗弃；`521-523/570-572/593-604/618` 均无 `parser_report_error`，error_count 不增长。
- 最小修复：统一 cleanup 标签，逐层释放并报告具体语法错误。
- 备注：arena 内存在 `parser_destroy` 统一回收，但诊断缺失是实在的。

### [严重] 65-12｜`parser_parse_call` 实参错误无诊断且错误恢复不同步
- 位置：`ParserExpression.c:37-104`、`100-102`、`107-109`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：`foo(a b)`、`foo(ref)`、`foo(` 未闭合、`foo(a: )`。
- 后果：`goto cleanup_error` 直接返回 NULL，不报告期望 `,`/`)`；当前 token 未同步，由 `Parser.c:26-30` 每次只前进一格，错误扩散或静默丢 token。
- 最小修复：`cleanup_error` 内先 `parser_report_error(parser, parser->current_token.line, column, "expected ',' or ')' in argument list")`。
- 备注：命名实参与三元/`ref` 关键字共用 `TOKEN_COLON`/`TOKEN_IDENTIFIER` 判定，边界错误不会报错。

### [严重] 65-13｜`KRT_FREE` 释放 arena 指针（命名实参 `param_name`）
- 位置：`ParserExpression.c:61`、`82`、`140`
- 类别：10 资源与生命周期 / 6 契约破坏
- 触发条件：命名实参解析失败或实参数组扩容失败。
- 后果：`param_name` 来自 `KRT_PARSER_STRDUP`（`KrtParserStrdup` → `KrtParserAlloc` → `parser->arena`），却用 `KRT_FREE`（`KrtSafeFree`）释放。arena 指针不是独立堆块，`find_block` 返回 NULL → INVALID_FREE；release 下静默 no-op，`KRT_DEBUG` 下 `debugbreak`。
- 最小修复：arena 内存不得用 `KRT_FREE`；若需可释放语义，改用 `KRT_STRDUP`。
- 备注：同 `ParserAdvanced.c:54/85/104/119/135`。

### [严重] 65-14｜类声明名用堆 `KRT_STRDUP`，而 AST 节点是 arena 且析构 no-op，造成泄漏并与 enum 路径分叉
- 位置：`ParserAdvanced.c:25`、`140`；对比 `EnumParser.inc:95`；`Ast.c:77-79`、`275-276`
- 类别：10 资源与生命周期 / 3 语义分叉
- 触发条件：每个 `class`/`struct`/`interface` 声明。
- 后果：`node->data.class_decl.name` 是堆指针，但 `ast_destroy_node` 对 `is_arena_allocated` 节点直接 `return`（`Ast.c:77-79`），永不释放 → 每处类声明泄漏；同一 AST 字段在 enum 路径（`EnumParser.inc:95`）是 arena 指针，克隆/释放语义不一致（`AstClone.inc:271` 用 `KRT_CLONE_STRING`）。
- 最小修复：统一用 `KRT_PARSER_STRDUP`（与 `EnumParser.inc` 一致），或让 arena 节点析构真正处理其堆字段。
- 备注：`KRT_FREE(name)` 在错误路径是正确的堆释放，但与成功路径的所有权模型冲突。

### [严重] 65-15｜模板参数 `KRT_FREE` 释放 arena 指针
- 位置：`ParserAdvanced.c:54`、`85`、`104`、`119`、`135`
- 类别：10 资源与生命周期 / 6 契约破坏
- 触发条件：类声明模板参数解析/主体解析/节点创建等错误路径。
- 后果：`template_params[i]` 来自 `KrtParserStrdup`（`72` 行，arena），`KRT_FREE` 触发 INVALID_FREE（`KRT_DEBUG` 下 `debugbreak`），release 下静默 no-op。
- 最小修复：删除这些 `KRT_FREE`，arena 由 `parser_destroy` 统一回收。
- 备注：与 65-13 同根。

### [严重] 65-16｜重复模板参数错误路径泄漏 `name` 与已收集参数
- 位置：`ParserAdvanced.c:66-71`
- 类别：10 资源与生命周期 / 9 错误处理不一致
- 触发条件：`class Foo<A, A>`。
- 后果：报告了 “Duplicate generic parameter” 后直接 `return NULL`，未释放堆 `name`，也未释放 `template_params[0..count-1]`（虽然是 arena，但与同函数其它 cleanup 不一致）。
- 最小修复：复用该函数既有的 cleanup 代码块。
- 备注：同函数 `82-88` 有完整 cleanup，此处遗漏。

### [严重] 65-17｜基类用表达式解析器解析类型位置，且 `base_class == NULL` 不检查
- 位置：`ParserAdvanced.c:92-96`、`98`
- 类别：4 阶段不对齐 / 3 语义分叉 / 8 未检查前置条件
- 触发条件：`class Foo : Bar.Baz()`、`class Foo : global::X`、`class Foo :`（缺类型）。
- 后果：`base_class` 得到的是 `AST_CALL`/`AST_MEMBER_ACCESS` 等表达式节点而非类型名，与 `class_decl.base_class` 的类型语义不符；`95` 行返回 NULL 时 `98` 行不检查，可能生成无基类声明并继续。
- 最小修复：改用 `KrtParseNamedType`/`KrtParseSourceType`；`base_class == NULL` 时报告并走 cleanup。
- 备注：`new` 表达式（`ParserExpression.c:513`）已正确使用 `KrtParseNamedType`，可对照。

### [严重] 65-18｜类声明大量语法错误无诊断
- 位置：`ParserAdvanced.c:27-29`、`62-64`、`82-88`、`98-107`、`113-123`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：缺类名、模板参数非标识符、缺 `>`、缺 `{`、类体解析失败。
- 后果：一律 `return NULL`，无 `parser_report_error`，error_count 不增长，流水线带残缺 AST 继续。
- 最小修复：每个分支补充具体诊断（期望标识符/`/`/`{`）。
- 备注：`class_type` 被 `(void)` 丢弃（19-20），也未校验 class/struct/interface。

### [严重] 65-19｜命名空间声明全程零诊断
- 位置：`ParserAdvanced.c:162-192`、`207-245`、`230-236`
- 类别：2 诊断缺失 / 1 静默失败
- 触发条件：`namespace` 后非标识符、`namespace A.`、`namespace A` 缺 `{`/`;`、命名空间体解析失败。
- 后果：整个函数没有一次 `parser_report_error`；所有错误静默返回 NULL，error_count 不增长。
- 最小修复：补诊断；`230-236` 与 `238-245` 报告期望 `{`/`;` 或块解析失败。
- 备注：与类声明同属“诊断缺失”模式。

### [严重] 65-20｜`sizeof` 仅接受类型关键字，标识符类型与无括号形式静默失败
- 位置：`ParserExpression.c:194-214`
- 类别：2 诊断缺失 / 3 语义分叉
- 触发条件：`sizeof(MyClass)`、`sizeof x`。
- 后果：`196-198`（缺 `(`）、`200-202`（非类型）、`205-207`（缺 `)`）三处 `return NULL` 均无诊断；`sizeof(MyClass)` 与 `sizeof(int)` 语义不同却都不支持。
- 最小修复：接受标识符类型（`KrtParseNamedType`）；失败时报告具体期望。
- 备注：与 `stackalloc`（176）已支持 `KrtParseSourceType` 形成对照。

### [一般] 65-21｜类型关键字转换路径未填充 `declared_type`/`target_is_pointer`
- 位置：`ParserExpression.c:492-499`（对比 `376-387`）
- 类别：3 语义分叉 / 13 注释与实现脱节
- 触发条件：`int(x)` 与 `(int)x`。
- 后果：两条路径都生成 `AST_CAST_EXPRESSION`，但前者只设 `target_type`/`expression`，后者还设 `target_is_pointer` 与 `declared_type`；下游按 `declared_type` 判断时对同一节点类型产生分叉。
- 最小修复：统一填充 `target_is_pointer`、`declared_type`。
- 备注：`492-499` 的 `declared_type` 为零值。

### [一般] 65-22｜`{...}` 与 `[...]` 都生成 `AST_ARRAY_LITERAL`，无词法标记
- 位置：`ParserExpression.c:403-465`
- 类别：3 语义分叉 / 6 契约破坏
- 触发条件：`{1,2,3}` 与 `[1,2,3]`。
- 后果：两种字面量落到同一 AST 类型且节点未保存起始 token，下游无法区分数组/集合/对象语义；若语义不同会被静默按同一种处理。
- 最小修复：节点记录 `token.type`，或映射到不同 AST 类型。
- 备注：`closing` 只用于校验，未存入节点。

### [一般] 65-23｜数组/集合字面量元素错误无诊断
- 位置：`ParserExpression.c:417-447`
- 类别：2 诊断缺失
- 触发条件：`[1,,2]`、`[1 2]`、`[1,2`（EOF）。
- 后果：`goto array_cleanup` 直接返回 NULL，无 `parser_report_error`，error_count 不增长。
- 最小修复：在 `419-421`/`438-440`/`445-447` 报告期望表达式/逗号/收尾符。
- 备注：与 65-1 同源但触发位置独立。

### [一般] 65-24｜`TOKEN_NUMBER` 的 `value` 未做 NULL/长度校验
- 位置：`ParserExpression.c:157-160`
- 类别：8 未检查前置条件
- 触发条件：数字 token 的 `value` 为 NULL 或空串（异常 lexer 状态）。
- 后果：`token.value[1]` 越界读；`strchr(token.value, ...)` 空指针崩溃。
- 最小修复：先判断 `token.value && token.value[0]`。
- 备注：正常路径 value 非 NULL，属防御性缺口。

### [一般] 65-25｜`atof` 静默接受非法/溢出浮点
- 位置：`ParserExpression.c:169`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`1.2.3`、`1e999`、`0x1p3`。
- 后果：`atof` 返回部分值/0/inf，无诊断，生成错误常量。
- 最小修复：改用 `strtod` 并检查 `endptr` 与 `errno`，失败时 `parser_report_error`。
- 备注：与 `162` 行 `KrtIntegerParse` 的严格校验形成对照。

### [一般] 65-26｜`KrtIntegerParse` 失败时已创建节点未回收
- 位置：`ParserExpression.c:162-166`
- 类别：10 资源与生命周期
- 触发条件：整数超限或非法。
- 后果：`node` 已创建并写入 `integer_value`，失败直接 `return NULL`，未 `ast_destroy_node(node)`；虽然 arena 节点析构本为 no-op，但清理意图与实现脱节。
- 最小修复：返回前 `ast_destroy_node(node)`。
- 备注：与 `175-192` stackalloc 的节点未回收同型。

### [一般] 65-27｜后缀 `.` 缺标识符、`[` 缺 `]` 无诊断
- 位置：`ParserExpression.c:786-789`、`807-811`
- 类别：2 诊断缺失
- 触发条件：`a.`、`a[1`。
- 后果：静默返回 NULL，error_count 不增长。
- 最小修复：报告期望标识符 / `]`。
- 备注：同文件 `778-783` 的 `::` 有诊断，前后不一致。

### [一般] 65-28｜一元前缀操作数失败无诊断
- 位置：`ParserExpression.c:669-672`
- 类别：2 诊断缺失
- 触发条件：`!`、`-`、`*`、`&` 后无操作数。
- 后果：静默 NULL，error_count 不增长。
- 最小修复：报告 “expected operand after unary operator”。
- 备注：与 `736` 二元操作数诊断不一致。

### [一般] 65-29｜`stackalloc` 错误报告位置错误且节点未回收
- 位置：`ParserExpression.c:184-192`
- 类别：2 诊断缺失（位置错） / 10 资源与生命周期
- 触发条件：`stackalloc int[`、`stackalloc int[1`。
- 后果：`parser_report_error` 使用 `stackalloc` token 的 line/col，而非实际出错的当前 token；`node`/`count_expr` 未回收即返回 NULL。
- 最小修复：用 `parser->current_token.line/column`；cleanup 节点。
- 备注：`176` 行 `KrtParseSourceType` 失败时 `node` 也未回收。

### [一般] 65-30｜cast 检测依赖枚举连续区间，配置漂移即误判
- 位置：`ParserExpression.c:329`
- 类别：11 配置漂移 / 6 契约破坏
- 触发条件：在 `TOKEN_INT2..TOKEN_VOID` 之间插入/重排 token。
- 后果：`next.type >= TOKEN_INT2 && next.type <= TOKEN_VOID` 会误判；`TOKEN_TYPE_STRING` 因定义在区间外需单独 `||`，已显脆弱。
- 最小修复：改用 `parser_is_type_keyword(next.type) || next.type == TOKEN_TYPE_STRING`。
- 备注：`parser_is_type_keyword` 已存在（`ParserBase.c:123`）。

### [一般] 65-31｜cast 的 `target_is_pointer` 与 `pointer_depth` 语义不一致
- 位置：`ParserExpression.c:383-384`
- 类别：3 语义分叉
- 触发条件：`(int[])x` 与 `(int*)x`。
- 后果：数组 cast `pointer_depth=0` 但 `target_is_pointer=1`；`declared_type.pointer_depth=0`。下游若按 `pointer_depth` 判断会认为数组 cast 不是指针，若按 `target_is_pointer` 则相反。
- 最小修复：统一 `pointer_depth`/`target_is_pointer` 的推导规则。
- 备注：`is_array_cast` 未参与 `pointer_depth` 计算（`365-369`）。

### [一般] 65-32｜`KRT_PARSER_UNLIKELY` 宏名不副实
- 位置：`ParserExpression.c:13`
- 类别：13 注释与实现脱节 / 11 配置漂移
- 触发条件：编译期。
- 后果：`#define KRT_PARSER_UNLIKELY(x) (x)` 仅原样展开，无分支预测提示，与命名暗示的 `__builtin_expect` 语义不符。
- 最小修复：改为 `(x)` 语义等价的 `__builtin_expect(!!(x), 0)`，或改名。
- 备注：轻微；不影响正确性。

### [一般] 65-33｜lambda 体解析失败泄漏 node/params；参数扩容失败分支为死代码
- 位置：`ParserExpression.c:228-239`、`257-267`
- 类别：10 资源与生命周期 / 5 死代码
- 触发条件：lambda 体解析失败；参数数量达到上限触发扩容失败。
- 后果：`265-267` 直接 `return NULL`，`node` 与 `params` 未清理；`228-239` 的 `if (!np) { params = NULL; break; }` 丢失旧数组指针，但 `KrtParserAlloc` 在 OOM 时 `exit(1)`，该分支实际不可达（死代码）。
- 最小修复：`265` 前清理；删除或标注不可达分支。
- 备注：arena OOM 致命这一前提由 `ParserBase.h:9-15` 确认。

### [一般] 65-34｜`class_type` 死变量，未校验 class/struct/interface
- 位置：`ParserAdvanced.c:19-20`
- 类别：5 死代码
- 触发条件：编译期。
- 后果：捕获 `class_type` 后 `(void)class_type` 丢弃；未校验当前 token 是否确为 class/struct/interface/enum。
- 最小修复：删除该变量，或用于校验并报错。
- 备注：轻微。

### [一般] 65-35｜类模板参数循环接受尾逗号、缺 EOF 显式检查
- 位置：`ParserAdvanced.c:47-80`
- 类别：8 未检查前置条件 / 3 语义分叉
- 触发条件：`class Foo<A,>`、`class Foo<`（EOF）。
- 后果：`75-79` 逗号后只要遇到 `>` 就退出循环，尾逗号被静默接受；`class Foo<` 靠 `62-64` 的 `break` 退出后报缺 `>`，但无诊断。
- 最小修复：显式拒绝尾逗号；循环条件加 `TOKEN_EOF` 检查并报错。
- 备注：与 `ParserExpression.c` 的实参/数组解析对尾逗号的处理不一致。

### [一般] 65-36｜命名空间用 `KRT_MALLOC/KRT_REALLOC`，与全 parser 的 arena 约定混用
- 位置：`ParserAdvanced.c:10-11`、`157`、`173`
- 类别：6 契约破坏 / 4 阶段不对齐
- 触发条件：解析命名空间声明/OOM。
- 后果：同文件其它部分（`ParserExpression.c`）统一用 `KRT_PARSER_ALLOC_FROM_ARENA`；此处用堆分配，失败静默返回 NULL 无诊断，且 `parts`（堆）与 `full_name`（arena，`198`）分属两个域，生命周期管理分裂。
- 最小修复：统一到 arena，或补诊断并明确 `parts`/`full_name` 的所有权。
- 备注：`full_name` 存入 AST（arena），`parts` 仅临时，建议都用 arena。
