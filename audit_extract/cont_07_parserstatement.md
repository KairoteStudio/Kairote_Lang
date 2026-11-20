# 第六十四批：ParserStatement.c（全量）

## 覆盖范围

- 文件：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c`，全量 1674 行，已用 read 工具分段（1-200、201-420、421-640、641-870、871-1100、1101-1400、1401-1674）读完。
- 交叉核对：主会话已验证的 :540-580（赋值目标处理）已纳入本轮全量审查；另按契约需要 grep/读了 `ParserBase.h`、`Parser.h`、`Ast.c`、`Tokenizer.h/.c`、`KrtCommon.h`、`Allocator.c`、`Arena.c`、`ParserExpression.c` 的少量相关行。
- 前提 G1 核对结论（重要）：`KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC` 经 `KrtSafeMalloc/Realloc/Strdup` 失败返回 NULL（活路径）；但 `KrtParserAlloc`/`KrtParserCreateNode`/`KrtParserStrdup` 失败走 `KRT_COMPILE_ERROR` → `exit(1)`（`KrtCommon.h:60-65`、`ParserBase.h:9-35`），**不返回 NULL**。因此 G1 的 NULL 活路径只适用于 KRT_* 堆函数；`KRT_PARSER_*` 后的 NULL 守卫属不可达死代码（见 64-26）。
- 所有权模型：parser 节点由 `KrtParserCreateNode` 建在 `parser->arena`（`is_arena_allocated=1`），`ast_destroy_node` 对 arena 节点直接 return（`Ast.c:77`），其清理分支在默认 arena 模式下不可达；但本文件大量子数组/名字用 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC` 堆分配（见 64-25）。

## 问题条目

### [致命] 64-1｜数组赋值对 arena AST 节点调用 KRT_FREE（非法释放）
- 位置：`ParserStatement.c:537`
- 类别：契约破坏 / 资源与生命周期
- 触发条件：任何数组赋值 `arr[i] = v` 或 `arr[i] += v`。`left` 是 `AST_ARRAY_ACCESS`，由 `ParserExpression.c:814` 的 `KrtParserCreateNode` 建在 parser arena。
- 后果：`KRT_FREE(left)` → `KrtSafeFree` 对未登记指针 `find_block` 失败 → `KrtMemoryReportError("INVALID_FREE")`；release 构建静默、`KRT_DEBUG` 构建 `__debugbreak`。同时该容器本应由 arena 回收，释放动作多余且错误。
- 最小修复：删除 `KRT_FREE(left)`（子节点已转移给新节点，容器交给 arena）；如需显式回收，改用不接管所有权的处理。
- 备注：同函数 511-517 指针解引用分支不释放 `left` 反而是正确写法；537 是全文件唯一对 AST 节点调用 `KRT_FREE` 的位置。

### [致命] 64-2｜print 参数数组 arena 分配后用 KRT_REALLOC/KRT_FREE（分配器混用）
- 位置：`ParserStatement.c:612`、`618`、`640`、`650`、`660`
- 类别：契约破坏 / 资源与生命周期 / 静默失败
- 触发条件：`print` 参数超过 8 个（初始容量 `KRT_PARSER_ARGUMENT_CAPACITY_INIT=8`）触发 618 扩容；或 `print` 语法错误走 640/650/660 清理。
- 后果：`values` 来自 `KRT_PARSER_ALLOC_FROM_ARENA`（arena）。`KRT_REALLOC` 对 arena 指针执行 `KrtPtrGetBlock` 失败 → `INVALID_REALLOC` 且返回 NULL，`print` 静默失败（无 `parser_report_error`，`error_count` 不增）；`KRT_FREE(values)` 同样触发 `INVALID_FREE`。即超过 8 个参数的 `print` 永远失败。
- 最小修复：起始分配改用 `KRT_MALLOC` 并全程用 `KRT_REALLOC`/`KRT_FREE`；或像 `parser_parse_function_declaration` 那样用 `KrtParserAlloc`+`memcpy` 手工扩容且不 `KRT_FREE`。
- 备注：619 是唯一一处 NULL 检查，但仍未报诊断；640/650/660 三处清理路径都含非法 `KRT_FREE`。

### [致命] 64-3｜switch 子句数组/语句数组 KRT_REALLOC 未检查
- 位置：`ParserStatement.c:821`、`839`、`864`
- 类别：未检查前置条件（NULL）/ 静默失败
- 触发条件：单个 case 内语句数超过 4（`scap` 初始 4）触发 821/864；case 数超过 4（`cap` 初始 4）触发 839。G1：`KRT_REALLOC` 失败返回 NULL。
- 后果：821/864 之后 `stmts[sc++] = st` 对 NULL 解引用；839 之后 `cases[case_count++] = clause` 对 NULL 解引用 → 段错误；同时旧指针丢失造成泄漏。
- 最小修复：用局部变量接收返回值，失败时保留旧指针、走 `cleanup` 并 `parser_report_error`。
- 备注：826 的 `clause` 创建失败有清理，但 839 的 `cases` 扩容在其后且无检查，顺序放大了风险。

### [致命] 64-4｜using 指令 path_parts 分配/追加/扩容未检查
- 位置：`ParserStatement.c:1462`、`1468`、`1476`、`1478`、`1483`、`1486`
- 类别：未检查前置条件（NULL）
- 触发条件：`KRT_MALLOC(16*sizeof(char*))` 失败 → 1468 写入 NULL；`KRT_STRDUP` 失败 → `path_parts` 中存 NULL；`KRT_REALLOC` 失败 → 1478/1488 写入 NULL。
- 后果：NULL 解引用崩溃；或 `namespace_path` 含 NULL 项，语义/克隆（`AstClone.inc:362`）阶段解引用崩溃。
- 最小修复：每处检查 NULL；失败时释放已建 `path_parts` 并 `parser_report_error`。
- 备注：1468 首次写入前也无容量检查（容量 16 尚够）；1491-1495 已有一处正确的清理样板可复用。

### [致命] 64-5｜block 语句数组 KRT_MALLOC 未检查
- 位置：`ParserStatement.c:1197`
- 类别：未检查前置条件（NULL）
- 触发条件：`KRT_MALLOC(sizeof(ASTNode*) * 16)` 失败（G1）。
- 后果：1222 `statements[count++] = stmt` 对 NULL 解引用 → 崩溃。
- 最小修复：检查 NULL，返回 NULL 并（或）报 `parser_report_error`。
- 备注：同文件 680 的 `KRT_PARSER_MALLOC` 有检查，此处遗漏；1212 的 realloc 另有问题（见 64-6）。

### [严重] 64-6｜block 语句数组 KRT_REALLOC 失败丢指针且无诊断
- 位置：`ParserStatement.c:1212-1215`
- 类别：资源与生命周期 / 静默失败
- 触发条件：块内语句数超过 16 触发扩容且 `KRT_REALLOC` 失败。
- 后果：`statements` 被置 NULL，旧数组泄漏；直接 `return NULL`，无 `parser_report_error`，`error_count` 不增，上层误判为无错误。
- 最小修复：局部变量接收 realloc，失败时保留旧指针、清理已解析语句并报错。
- 备注：与 618 同款模式，但此处是纯 heap 数组，主要后果是泄漏 + 静默失败。

### [致命] 64-7｜标识符赋值 KRT_STRDUP 未检查
- 位置：`ParserStatement.c:549`、`552`
- 类别：未检查前置条件（NULL）/ 静默失败
- 触发条件：`KRT_STRDUP(left->data.identifier_name)` 失败（G1）。
- 后果：`node->data.assignment.name` / `compound_assignment.name` 为 NULL；随后 `ast_destroy_node(left)`（arena 节点为 no-op），无诊断、`error_count` 不增；产出名字为空的赋值节点，语义/代码生成阶段可能崩溃或生成错误符号。
- 最小修复：检查 NULL，失败时释放 `value`/`left` 并报 `parser_report_error`。
- 备注：:540-580 段主会话已交叉验证赋值目标处理，但此分配失败分支仍未覆盖；549/552 是 G1 活路径。

### [严重] 64-8｜成员赋值被编码为 AST_BINARY_OPERATION，复合成员赋值直接报错
- 位置：`ParserStatement.c:558-571`、`573`
- 类别：语义分叉
- 触发条件：`a.b = c;` 与 `a.b += c;`。
- 后果：`=` 生成 `AST_BINARY_OPERATION(operator=TOKEN_ASSIGN)`，与标识符/数组赋值的 `AST_ASSIGNMENT`/`AST_ARRAY_ASSIGNMENT` 契约不一致，下游按 `node->type` 分派的阶段无法把它识别为赋值；`+=` 等复合赋值落到 573 `"Unsupported assignment target"`，同一语法结构在不同目标上能力不一致。
- 最小修复：新增 `AST_MEMBER_ASSIGNMENT`/`AST_MEMBER_COMPOUND_ASSIGNMENT`（全仓 grep 无此类节点，`Ast.c` 也无对应清理分支），或在统一 assignment 节点中表达成员目标。
- 备注：这是 540-580 段最实质的语义分叉点，建议优先修。

### [严重] 64-9｜return 语句表达式失败静默、分号不强制
- 位置：`ParserStatement.c:585-598`
- 类别：诊断缺失 / 静默失败
- 触发条件：`return <非法表达式>`；`return 1`（缺 `;`）；`return 1 2;`。
- 后果：`parser_parse_expression` 返回 NULL 时 `value` 保持 NULL 仍创建 `AST_RETURN_STATEMENT`，无 `parser_report_error`；随后既不检查也不消费 `TOKEN_SEMICOLON`，缺分号被静默接受，剩余 token 留给上层。
- 最小修复：`value==NULL` 时报错返回 NULL；解析后要求并消费 `;`。
- 备注：与 if/while 的错误路径相比，本函数既无表达式诊断也无分号诊断。

### [严重] 64-10｜print 语句全流程无诊断
- 位置：`ParserStatement.c:606-608`、`615-636`、`646-652`
- 类别：诊断缺失 / 静默失败
- 触发条件：缺 `(`、表达式解析失败、缺 `)`、缺 `;`。
- 后果：一律 `return NULL`，从不调用 `parser_report_error`，`error_count` 不增；调用方只能看到空指针，错误位置与原因丢失。
- 最小修复：每个失败分支补 `parser_report_error`（含 line/col）。
- 备注：636 失败分支还会对 `values` 中可能为 NULL 的项调用 `ast_destroy_node`（该函数 NULL 安全，但无意义）。

### [严重] 64-11｜block 错误恢复不报告诊断、强制跳 token、静默放弃
- 位置：`ParserStatement.c:1200-1243`、`1229`
- 类别：诊断缺失 / 静默丢弃 token / 注释与实现脱节
- 触发条件：块内语句解析失败（尤其未前进 token 的失败）。
- 后果：`consecutive_errors` 只计数不报告；超过 10 时直接清理并 `return NULL`，最多 10 个错误全部丢失；1229 注释自认「强制跳过,后续解决一下」，静默吞 token。
- 最小修复：每次失败调用 `parser_report_error`；删除 TODO 式强制跳过，改为报错后跳过。
- 备注：1228 的 EOF/RIGHT_BRACE 保护只防越界，不产生诊断。

### [严重] 64-12｜switch 多条错误路径静默
- 位置：`ParserStatement.c:796-799`、`812-817`、`879-881`
- 类别：诊断缺失 / 静默丢弃 token
- 触发条件：case 表达式失败；case 体内语句失败；switch 体出现非 case/default 的杂散 token。
- 后果：均无 `parser_report_error`；杂散 token 在 880 被直接 `parser_advance` 吞掉；`error_count` 不增，解析可能「成功」产出残缺 switch。
- 最小修复：补诊断；杂散 token 报错后再跳过。
- 备注：884 的缺 `}` 有诊断，同一函数内策略不一致。

### [严重] 64-13｜重复 default 静默覆盖
- 位置：`ParserStatement.c:868-878`
- 类别：语义分叉 / 资源与生命周期
- 触发条件：switch 中出现两个 `default:`。
- 后果：878 `default_case = blk;` 直接覆盖前一个 `AST_BLOCK`，第一个 default 子句被静默丢弃且无诊断（arena 节点不泄漏，但语义丢失）。
- 最小修复：检测 `default_case != NULL` 时报 `"duplicate default"` 并清理 `blk`。
- 备注：与 884 缺 `}` 的诊断不一致。

### [严重] 64-14｜函数声明多条错误路径无诊断
- 位置：`ParserStatement.c:35-37`、`42-44`、`83-85`、`91-93`、`98-100`、`115-117`、`121-125`、`127-129`
- 类别：诊断缺失
- 触发条件：缺函数名、缺 `(`、参数类型/数组/名字错误、缺 `)`、缺函数体、体解析失败。
- 后果：全部直接 `return NULL`，无 `parser_report_error`，`error_count` 不增，用户看不到具体语法错误。
- 最小修复：每个分支补诊断（含函数名/行号）。
- 备注：参数数组均为 `KrtParserAlloc`（arena，失败即 exit），提前返回不构成堆泄漏，但诊断缺失是实在问题。

### [一般] 64-15｜parameter_is_params 对所有参数恒为 1 且无人消费
- 位置：`ParserStatement.c:104`、`140`
- 类别：死代码 / 契约破坏
- 触发条件：任何函数声明。
- 后果：`param_is_params` 全为 1，无法区分 params/非 params；全仓 grep 显示该字段只在 `Ast.h`、`AstClone.inc`、`Ast.c:free_function_decl_data` 出现，无任何语义读取，写入值无意义。
- 最小修复：按 `TOKEN_PARAMS` 解析并设置，或删除该字段及 140 行。
- 备注：`ParserBase.h:44` 声明的 `parser_parse_parameter_list` 才是带语义的版本，但本文件未使用。

### [一般] 64-16｜函数参数表语法过宽（尾逗号/缺逗号静默接受）
- 位置：`ParserStatement.c:110-113`
- 类别：诊断缺失 / 语法过宽
- 触发条件：`f(int a,)` 或 `f(int a int b)`。
- 后果：尾逗号被静默接受；缺逗号时把下一个类型当作新参数，无诊断。
- 最小修复：逗号后遇 `)` 报错；参数末尾要求逗号或 `)`。
- 备注：与 115 缺 `)` 的严格性不一致。

### [严重] 64-17｜foreach 多条错误路径无诊断且类型识别与 for 不一致
- 位置：`ParserStatement.c:1139-1151`、`1158-1176`、`1144-1147`
- 类别：诊断缺失 / 语义分叉
- 触发条件：缺 `(`、非标识符、缺 `in`、iterable 失败、缺 `)`、体失败；`foreach (let x in ...)` 或 `foreach (var x in ...)`。
- 后果：均静默 `return NULL`；`let`/`var` 不被 1145 的类型识别接受（for 的 `looks_decl` 也不含 let/var，但 for 可经 `parser_parse_statement` 兜底，foreach 直接失败）；1145 的 `t >= TOKEN_UINT2 && t <= TOKEN_UINT128` 是 `INT2..VOID` 子集，属冗余死条件。
- 最小修复：补诊断；类型识别复用统一 helper；支持 let/var 或明确报错。
- 备注：1152 的 `KrtParserStrdup` 为 arena 分配，失败即 exit，不会返回 NULL；1178 的 `if (!node)` 不可达。

### [严重] 64-18｜for 语句：死代码分号检查、多条静默错误、体必须为块
- 位置：`ParserStatement.c:1033-1035`、`1047-1055`、`1085-1096`、`1099-1111`
- 类别：死代码 / 诊断缺失 / 阶段不对齐
- 触发条件：正常 for；缺第二/第三分界符；体为单语句。
- 后果：1033 的 `if (current==SEMICOLON) advance` 在两条 init 路径（`parser_parse_variable_declaration_with_type` 与表达式语句）都已消费分号后恒为假，属死代码；1047/1085 缺分号/`)` 静默返回 NULL；1099 只接受块，而 while/if/do/foreach 接受块或单语句，`for (...) stmt;` 静默失败。
- 最小修复：删除死分支；补诊断；体改用 `parser_parse_block_or_statement` 或明确报错。
- 备注：1099 与 669/918/990/1173 的 `block_or_statement` 用法不一致，是语法不统一的根源。

### [严重] 64-19｜if/while/do 错误路径普遍无诊断
- 位置：`ParserStatement.c:703-716`、`974-987`、`939-943`
- 类别：诊断缺失
- 触发条件：缺 `(`、条件表达式失败、缺 `)`、do 条件失败。
- 后果：直接 `return NULL`，无 `parser_report_error`，`error_count` 不增；同一函数内 do 的 923/931/945 却有诊断，策略不一致。
- 最小修复：补诊断（含行号与上下文）。
- 备注：do 的 954-956 分号可缺省且静默，同属诊断缺失。

### [严重] 64-20｜修饰符语义丢失/错映射
- 位置：`ParserStatement.c:156-176`
- 类别：语义分叉 / 死代码
- 触发条件：`const int x = 1;`、`static int x = 1;`、`extern int x;`、`public/private/protected` 修饰非声明语句。
- 后果：174 将 `is_static || is_const` 的变量统一映射为 `AST_STATIC_VARIABLE_DECLARATION`，const 与 static 不可区分；`extern`/access 修饰符被消费但无任何 AST 表示；修饰非声明（如 `static if`）时修饰符静默丢弃。
- 最小修复：为 const/extern/access 增加节点字段或节点类型；无法识别时报错。
- 备注：172 只对 static 函数做转换，const 函数无对应节点。

### [严重] 64-21｜keyword function 直接改写 parser->current_token.type
- 位置：`ParserStatement.c:1269-1273`
- 类别：共享可变状态 / 契约破坏
- 触发条件：`TOKEN_FUNCTION` 且形如 `function f(`。
- 后果：原地把当前 token 类型改为 `TOKEN_VOID`；解析失败时改写不回滚，污染 parser 状态；block 的 `token_advanced` 判定（1225）依赖 `current_token.type`，可能被误导而重置 `consecutive_errors`。
- 最小修复：用局部 `KrtSourceType` 或压入合成 token，不改写共享 token。
- 备注：注释称「语法一致」，但未说明状态改写；若原 token 的 `value` 被 `KrtParseSourceType` 之外逻辑读取也可能错位。

### [严重] 64-22｜unsafe 块体解析失败仍返回带空 expression 的节点
- 位置：`ParserStatement.c:1336-1340`
- 类别：静默失败 / 诊断缺失
- 触发条件：`unsafe(using a;)` 后 `{` 内块解析失败（如缺 `}`）。
- 后果：`parser_parse_block` 返回 NULL 时 1339 直接赋给 `expression` 并 `return node`，无诊断；产出 `expression==NULL` 的 `AST_UNSAFE_CALL`，下游（`Ast.c:483` 有 NULL 检查，但语义/代码生成未必）可能崩溃。
- 最小修复：检查 `expression==NULL`，报错并清理 `node`/`permissions`。
- 备注：1341 的 `unsafe_error` 会报错，但这条路径绕过了它。

### [严重] 64-23｜表达式语句路径缺分号静默、未知 token 静默跳过
- 位置：`ParserStatement.c:1600-1603`、`1630-1633`、`1666-1669`、`1651-1663`
- 类别：诊断缺失 / 静默丢弃 token
- 触发条件：表达式语句缺 `;`；default 分支遇到未知 token。
- 后果：缺分号时销毁 `stmt` 后 `return NULL`，无诊断；未知 token 在 1661 被直接 `parser_advance` 吞掉，无报错，可能吞掉预处理或未来新增 token。
- 最小修复：补诊断；未知 token 报错后再跳过。
- 备注：1654 对 EOF/UNKNOWN 直接返回 NULL 亦不报错；本文件无任何预处理指令交互代码，指令类 token 只能落入此静默路径。

### [严重] 64-24｜泛型/比较前瞻启发式误判且硬编码 256
- 位置：`ParserStatement.c:1532-1570`
- 类别：语义分叉 / 配置漂移
- 触发条件：语句 `a < b > c;`（比较表达式）；或超长泛型/表达式（>256 token）。
- 后果：1562 在 depth 归零且遇 `>` 时调用 `parser_parse_variable_declaration_with_type`，把 `a<b>` 当类型、`c` 当变量名，表达式语句被改写为声明；扫描上限 256 为 magic number，超长时静默落到 else 分支按表达式解析，行为随长度漂移。
- 最小修复：用真实泛型语法判定（如在类型位置）而非比较符深度；把上限提为具名常量并在溢出时报告。
- 备注：1549 与 1562 两个分支存在重叠，逻辑冗余；`depth < 0` 的提前 break 也可能把 `a > b` 这类表达式带入后续分支。

### [严重] 64-25｜heap 子数组挂在 arena 节点上，默认 arena 模式下永不释放
- 位置：`ParserStatement.c:1197/1264`（block statements）、`612/663`（print values）、`1462/1506`（using path_parts）、`788/896`（switch cases）、`808/835`（case stmts）、`1292/1319`（unsafe permissions）
- 类别：资源与生命周期 / 契约破坏
- 触发条件：默认 `parser_create`（arena 非空）下解析任何块/print/using/switch/unsafe。
- 后果：`Ast.c:77` 对 `is_arena_allocated` 节点直接 return，`AST_BLOCK`/`AST_PRINT_STATEMENT`/`AST_USING_DIRECTIVE`/`AST_SWITCH_STATEMENT` 等清理分支不可达；这些 `KRT_MALLOC`/`KRT_REALLOC` 出来的堆数组只在小 arena（NULL）路径下才被释放，默认路径全部泄漏。
- 最小修复：统一所有权（子数组也走 arena），或让 arena 节点登记需堆释放的子资源并由 destroy 释放。
- 备注：错误路径的显式 `KRT_FREE` 说明作者意识到堆所有权，但成功路径无对应释放；`Ast.c` 的清理代码在默认模式下形同虚设。

### [一般] 64-26｜大量 `if (!node)` 守卫在致命分配器后不可达
- 位置：`ParserStatement.c:132`、`296`、`423`、`542`、`560`、`656`、`688`、`741`、`826`、`868`、`892`、`958`、`996`、`1113`、`1178`、`1256`、`1355`、`1409`、`1425`、`1503`
- 类别：死代码 / 注释与实现脱节
- 触发条件：编译期即确定。
- 后果：`KrtParserCreateNode`/`KrtParserAlloc`/`KrtParserStrdup` 失败时走 `KRT_COMPILE_ERROR` → `exit(1)`（`KrtCommon.h:60-65`、`ParserBase.h:9-35`），永远不返回 NULL；这些 `if (!node/ptr)` 分支不可达，误导读者以为存在 OOM 恢复路径。
- 最小修复：删除不可达守卫，或把 `KRT_PARSER_*` 改为返回 NULL 并统一处理。
- 备注：与任务前提 G1 相反——G1 适用于 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC`（确实返回 NULL），不适用于 `KRT_PARSER_*` 系列。

### [一般] 64-27｜变量声明诊断策略不一致（关键字类型报错、用户类型静默）
- 位置：`ParserStatement.c:263-282`
- 类别：错误处理不一致 / 诊断缺失
- 触发条件：`MyType x = ;` 或 `MyType x` 缺分号（`type_token==TOKEN_IDENTIFIER`）。
- 后果：仅 `declared_by_keyword` 时在 266/280 报错，用户自定义类型声明缺表达式/分号时静默 `return NULL`；同一语法错误因类型写法不同诊断不同。
- 最小修复：去掉 `declared_by_keyword` 条件，统一报错。
- 备注：219 的 `KRT_STRDUP` 失败在 230 有检查但无诊断。

### [一般] 64-28｜with_type 拒绝类型位数组大小并静默
- 位置：`ParserStatement.c:338-343`
- 类别：语义分叉 / 诊断缺失
- 触发条件：`int[3] x;`（类型后带大小）。
- 后果：直接 `KRT_FREE(type_name)` 返回 NULL，无诊断；而 `int x[3];` 在 368-388 支持，同一语义两种写法一种静默失败。
- 最小修复：支持类型位大小，或在 342 报错。
- 备注：与 368 的变量位数组处理不一致。

### [一般] 64-29｜指针深度把类型 token 统一改写为 TOKEN_UINT64
- 位置：`ParserStatement.c:318-320`、`444-446`
- 类别：语义分叉 / 配置漂移
- 触发条件：任何带 `*`/`**` 的变量声明。
- 后果：`variable_decl.type` 丢失原始类型 token（仅 `declared_type.pointer_depth` 保留深度），下游若读 `type` 无法区分 `int*`/`char*`；两条声明路径行为一致但信息有损。
- 最小修复：保留原 token，或用专门的指针类型表示。
- 备注：若下游只读 `declared_type` 则影响有限，需跨文件确认。

### [一般] 64-30｜break/continue 缺分号静默、无循环上下文校验
- 位置：`ParserStatement.c:1404-1406`、`1420-1422`
- 类别：诊断缺失 / 阶段不对齐
- 触发条件：`break` 后缺 `;`；`break`/`continue` 出现在循环外。
- 后果：缺分号静默 `return NULL`；循环外 break/continue 被正常建成 AST，parser 不跟踪循环/函数上下文，非法结构推迟到语义阶段（若语义不查则漏到后端）。
- 最小修复：补分号诊断；parser 维护循环深度，或明确交给语义阶段并确认其检查。
- 备注：`Parser.h:46-47` 声明的 `parser_parse_break_statement`/`continue_statement` 未在本文件使用，存在重复/死声明。

### [一般] 64-31｜using 的 alias 从不解析，分号可缺省
- 位置：`ParserStatement.c:1465`、`1499-1501`、`1505`、`1508`
- 类别：死代码 / 诊断缺失
- 触发条件：`using a.b;`（无分号）或任何 using。
- 后果：`alias` 恒 NULL、`is_alias` 恒 0，`using X = Y` 形式不被识别；分号缺省时静默继续，`=` 留给上层当成新语句。
- 最小修复：实现 alias 语法，或删除该字段并对 `=` 报错；要求分号。
- 备注：`Ast.c:349-353` 会释放 alias/namespace_path，但本文件永远给不出 alias。

### [一般] 64-32｜print 的 has_newline 恒为 true
- 位置：`ParserStatement.c:645`、`665`
- 类别：死代码 / 契约破坏
- 触发条件：任何 print 语句。
- 后果：`has_newline` 字段无信息量，print 换行行为不可控；疑似未实现抑制换行的语法。
- 最小修复：根据语法（如尾部标记/参数）设置，或删除该字段。
- 备注：与 611-634 的参数解析无关联，属未接线字段。
