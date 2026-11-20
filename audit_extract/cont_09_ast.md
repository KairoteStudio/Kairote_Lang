# 第六十六批：Ast.c + AstVisit.inc

## 覆盖范围

- 全量读取：`Re.KrtC/src/Compiler/Frontend/Parser/Ast.c`（570 行）、`Re.KrtC/src/Compiler/Frontend/Parser/AstVisit.inc`（409 行）。
- 为核对契约与调用方，仅做定向 grep：`Ast.h`（ASTNodeType 枚举 106 项、data union 各结构字段与行号）、`AstClone.inc`（克隆覆盖与分配宏）、`ParserBase.h`（节点/字符串分配）、`ParserStatement.c`/`ParserExpression.c`（KRT_STRDUP/KRT_REALLOC/KrtParserAlloc 实际归属）、`CompilerPipeline.c`、`TypeExpansion.inc`、`StdlibCache.inc`（KrtAstVisit/KrtAstClone/ast_destroy_node 调用点）。
- 核心结论：`KrtAstVisit` 对 106 个节点类型覆盖完整；`AstClone.inc` 覆盖完整；**`ast_destroy_node` 只覆盖约 62 个类型**，且对 arena 节点提前返回，与「parser 用 arena 建节点 + KRT_STRDUP/KRT_REALLOC 填堆字段」的现实所有权模型冲突。

## 问题条目

### [致命] 66-1｜arena 节点提前返回，堆字符串/堆数组全部泄漏
- 位置：`Ast.c:77-79`
- 类别：10 资源与生命周期 / 1 静默失败
- 触发条件：任何 `is_arena_allocated == 1` 的节点被 `ast_destroy_node` 释放。
- 后果：`return` 跳过 switch，节点内 `KRT_STRDUP` 的 `name`/`var_name`/`class_name` 等堆字符串与 `KRT_REALLOC` 的 `values`/`cases` 堆数组全部泄漏；无诊断。
- 最小修复：arena 节点仍应释放其自有堆字段（字符串/数组），仅节点本体与 arena 子节点交给 arena；或统一改为「arena 节点的一切资源都由 arena 分配」。
- 备注：已核实 `ParserBase.h:30` 用 `ast_create_node_arena` 建节点，`ParserStatement.c:219/549/552`、`ParserAdvanced.c:25/184` 用 `KRT_STRDUP` 填字段，`ParserStatement.c:618/821/839` 用 `KRT_REALLOC` 填数组 → 现实路径必然触发。

### [致命] 66-2｜destroy 与 clone/visit 覆盖不对齐（总纲）
- 位置：`Ast.c:81-565`（destroy switch）对照 `Ast.h:9-120`（枚举）、`AstVisit.inc:6-408`、`AstClone.inc:128-549`
- 类别：6 契约破坏 / 13 注释与实现脱节 / 5 死代码
- 触发条件：凡新增节点类型或走到未覆盖类型。
- 后果：clone/visit 认得的类型，destroy 不认得；同一 AST 在「克隆—遍历—销毁」三阶段行为不一致，且编译期无任何告警。
- 最小修复：为 destroy 补齐全部缺失 case（见 66-3～66-34），或用 X-macro 从同一类型表生成 clone/visit/destroy 三张表。
- 备注：缺失类型共 32 个，逐条见下。

### [致命] 66-3｜destroy 缺 AST_ASYNC_FUNCTION
- 位置：`Ast.c:81-565` 无此 case；字段见 `Ast.h:109,194-207`；visit/clone 按 function_decl 处理（`AstVisit.inc:19`、`AstClone.inc:142`）
- 类别：10 / 6
- 触发条件：销毁 async 函数节点。
- 后果：name/parameters/parameter_types/parameter_is_* /parameter_default_values/body 全泄漏。
- 最小修复：与 `AST_FUNCTION_DECLARATION` 合并 case，调用 `free_function_decl_data`。

### [致命] 66-4｜destroy 缺 AST_PROGRAM
- 位置：`Ast.c` 无此 case；`Ast.h:9`；visit/clone 用 `data.block`（`AstVisit.inc:106-111`、`AstClone.inc:240-242`）
- 类别：10 / 6
- 触发条件：销毁程序根节点（如 `CompilerPipeline.c:99`）。
- 后果：整棵顶层语句数组及全部子树泄漏。
- 最小修复：`case AST_PROGRAM: case AST_BLOCK:` 合并走 `free_ast_node_array(block.statements, block.statement_count)`。

### [致命] 66-5｜destroy 缺 AST_TERNARY_OPERATION
- 位置：`Ast.c` 无此 case；`Ast.h:24,287-291`；visit 见 `AstVisit.inc:95-99`
- 类别：10 / 6
- 触发条件：销毁三元表达式。
- 后果：condition/true_value/false_value 三个子树泄漏。
- 最小修复：补 case 递归销毁三个子节点。

### [致命] 66-6｜destroy 缺 AST_NEW_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:33,325-328`；visit 见 `AstVisit.inc:126-130`
- 类别：10 / 6
- 触发条件：销毁 new 表达式。
- 后果：`class_name`、`arguments` 数组及元素、`argument_names` 全泄漏。
- 最小修复：补 case，仿 `AST_CALL` 释放。

### [致命] 66-7｜destroy 缺 AST_NEW_ARRAY_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:34,332-333`；visit 见 `AstVisit.inc:131-133`
- 类别：10 / 6
- 触发条件：销毁 new 数组表达式。
- 后果：`element_type` 字符串与 `size` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-8｜destroy 缺 AST_NAMESPACE_DECLARATION
- 位置：`Ast.c` 无此 case；`Ast.h:35,336-337`；visit 见 `AstVisit.inc:134-136`
- 类别：10 / 6
- 触发条件：销毁命名空间节点。
- 后果：`name` 与 `body` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-9｜destroy 缺 AST_DEFAULT_CLAUSE
- 位置：`Ast.c` 无此 case；`Ast.h:63,432-434`；visit 见 `AstVisit.inc:211-215`
- 类别：10 / 6
- 触发条件：销毁 switch 的 default 分支（switch 节点只销毁 `default_case` 指针指向的该节点，但其 `statements` 数组无人释放）。
- 后果：`statements` 数组及其中语句泄漏。
- 最小修复：补 case 走 `free_ast_node_array`。

### [致命] 66-10｜destroy 缺 AST_BREAK_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:64,435-438`；visit 见 `AstVisit.inc:216-218`
- 类别：10 / 6
- 触发条件：销毁 break 语句。
- 后果：`value` 子树与 `resolved_class_name` 字符串泄漏。
- 最小修复：补 case。

### [致命] 66-11｜destroy 缺 AST_CONTINUE_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:65,439-441`；visit 见 `AstVisit.inc:219-221`
- 类别：10 / 6
- 触发条件：销毁 continue 语句。
- 后果：`value` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-12｜destroy 缺 AST_DELETE_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:66,442-446`；visit 见 `AstVisit.inc:222-224`
- 类别：10 / 6
- 触发条件：销毁 delete 语句。
- 后果：`value` 子树与 `resolved_class_name` 泄漏。
- 最小修复：补 case。

### [致命] 66-13｜destroy 缺 AST_LINQ_LET
- 位置：`Ast.c` 无此 case；`Ast.h:82,524-526`；visit 见 `AstVisit.inc:280-282`
- 类别：10 / 6
- 触发条件：销毁 LINQ let 子句。
- 后果：`var_name` 与 `expression` 泄漏。
- 最小修复：补 case。

### [致命] 66-14｜destroy 缺 AST_LINQ_GROUP
- 位置：`Ast.c` 无此 case；`Ast.h:83,528-531`；visit 见 `AstVisit.inc:283-286`
- 类别：10 / 6
- 触发条件：销毁 LINQ group 子句。
- 后果：`into_var_name`、`key_expression`、`element_expression` 泄漏。
- 最小修复：补 case。

### [致命] 66-15｜destroy 缺 AST_SIZEOF_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:91,568-570`；visit 见 `AstVisit.inc:316-317`
- 类别：10 / 6
- 触发条件：销毁 sizeof 表达式。
- 后果：`type_name` 字符串泄漏。
- 最小修复：补 case。

### [致命] 66-16｜destroy 缺 AST_STACKALLOC_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:92,572-575`；visit 见 `AstVisit.inc:318-320`
- 类别：10 / 6
- 触发条件：销毁 stackalloc 表达式。
- 后果：`type_name` 与 `count_expr` 泄漏。
- 最小修复：补 case。

### [致命] 66-17｜destroy 缺 AST_INTERPOLATED_STRING
- 位置：`Ast.c` 无此 case；`Ast.h:93,577-581`；visit 见 `AstVisit.inc:321-326`
- 类别：10 / 6
- 触发条件：销毁插值字符串。
- 后果：`string_parts` 与 `expressions` 两个数组及内容泄漏。
- 最小修复：补 case，分别走 `free_string_array` / `free_ast_node_array`。

### [致命] 66-18｜destroy 缺 AST_TUPLE_TYPE
- 位置：`Ast.c` 无此 case；`Ast.h:94,583-585`；visit 见 `AstVisit.inc:327-328`
- 类别：10 / 6
- 触发条件：销毁元组类型。
- 后果：`element_types` 数组泄漏。
- 最小修复：补 case。

### [致命] 66-19｜destroy 缺 AST_TUPLE_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:95,587-589`；visit 见 `AstVisit.inc:329-333`
- 类别：10 / 6
- 触发条件：销毁元组表达式。
- 后果：`elements` 数组及元素泄漏。
- 最小修复：补 case。

### [致命] 66-20｜destroy 缺 AST_TUPLE_ELEMENT_ACCESS
- 位置：`Ast.c` 无此 case；`Ast.h:96,591-593`；visit 见 `AstVisit.inc:334-336`
- 类别：10 / 6
- 触发条件：销毁元组元素访问。
- 后果：`tuple` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-21｜destroy 缺 AST_MATCH_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:97,595-598`；visit 见 `AstVisit.inc:337-342`
- 类别：10 / 6
- 触发条件：销毁 match 表达式。
- 后果：`expression` 与 `cases` 数组及其中 pattern_case 全部泄漏。
- 最小修复：补 case。

### [致命] 66-22｜destroy 缺 AST_PATTERN_CASE
- 位置：`Ast.c` 无此 case；`Ast.h:98,600-603`；visit 见 `AstVisit.inc:343-347`
- 类别：10 / 6
- 触发条件：销毁 match 分支。
- 后果：`pattern`、`when_clause`、`body` 三棵子树泄漏。
- 最小修复：补 case。

### [致命] 66-23｜destroy 缺 AST_PATTERN_LITERAL
- 位置：`Ast.c` 无此 case；`Ast.h:100,607-608`；visit 见 `AstVisit.inc:350-352`
- 类别：10 / 6
- 触发条件：销毁字面量模式。
- 后果：`value` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-24｜destroy 缺 AST_PATTERN_VARIABLE
- 位置：`Ast.c` 无此 case；`Ast.h:101,610-611`；visit 见 `AstVisit.inc:353-354`
- 类别：10 / 6
- 触发条件：销毁变量模式。
- 后果：`name` 字符串泄漏。
- 最小修复：补 case。

### [致命] 66-25｜destroy 缺 AST_PATTERN_TUPLE
- 位置：`Ast.c` 无此 case；`Ast.h:102,613-615`；visit 见 `AstVisit.inc:355-359`
- 类别：10 / 6
- 触发条件：销毁元组模式。
- 后果：`elements` 数组及元素泄漏。
- 最小修复：补 case。

### [致命] 66-26｜destroy 缺 AST_POINTER_DEREFERENCE
- 位置：`Ast.c` 无此 case；`Ast.h:104,620-622`；visit 见 `AstVisit.inc:366-368`
- 类别：10 / 6
- 触发条件：销毁指针解引用。
- 后果：`pointer` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-27｜destroy 缺 AST_POINTER_ASSIGNMENT
- 位置：`Ast.c` 无此 case；`Ast.h:120,623-627`；visit 见 `AstVisit.inc:362-365`
- 类别：10 / 6
- 触发条件：销毁指针赋值。
- 后果：`pointer` 与 `value` 两棵子树泄漏。
- 最小修复：补 case。

### [致命] 66-28｜destroy 缺 AST_ADDRESS_OF
- 位置：`Ast.c` 无此 case；`Ast.h:105,628-630`；visit 见 `AstVisit.inc:369-371`
- 类别：10 / 6
- 触发条件：销毁取地址表达式。
- 后果：`operand` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-29｜destroy 缺 AST_FIXED_STATEMENT
- 位置：`Ast.c` 无此 case；`Ast.h:106,631-635`；visit 见 `AstVisit.inc:372-375`
- 类别：10 / 6
- 触发条件：销毁 fixed 语句。
- 后果：`variable_name`、`expression`、`body` 泄漏。
- 最小修复：补 case。

### [致命] 66-30｜destroy 缺 AST_AWAIT_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:110,639-641`；visit 见 `AstVisit.inc:379-381`
- 类别：10 / 6
- 触发条件：销毁 await 表达式。
- 后果：`expression` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-31｜destroy 缺 AST_NULLABLE_TYPE
- 位置：`Ast.c` 无此 case；`Ast.h:115,658-661`；visit 见 `AstVisit.inc:392-394`
- 类别：10 / 6
- 触发条件：销毁可空类型节点。
- 后果：`type_expr` 子树泄漏。
- 最小修复：补 case。

### [致命] 66-32｜destroy 缺 AST_NULL_COALESCING
- 位置：`Ast.c` 无此 case；`Ast.h:116,662-665`；visit 见 `AstVisit.inc:395-398`
- 类别：10 / 6
- 触发条件：销毁 ?? 表达式。
- 后果：`left`/`right` 两棵子树泄漏。
- 最小修复：补 case。

### [致命] 66-33｜destroy 缺 AST_NULL_CONDITIONAL
- 位置：`Ast.c` 无此 case；`Ast.h:117,666-671`；visit 见 `AstVisit.inc:399-402`
- 类别：10 / 6
- 触发条件：销毁 ?. 表达式。
- 后果：`expression`、`member_name`、`arguments` 泄漏。
- 最小修复：补 case。

### [致命] 66-34｜destroy 缺 AST_CAST_EXPRESSION
- 位置：`Ast.c` 无此 case；`Ast.h:118,672-676`；visit 见 `AstVisit.inc:403-405`
- 类别：10 / 6
- 触发条件：销毁类型转换表达式。
- 后果：`expression` 子树泄漏。
- 最小修复：补 case。

### [严重] 66-35｜free_function_decl_data 漏释 parameter_default_values / parameter_is_nullable / parameter_is_array
- 位置：`Ast.c:55-70`（调用点 `Ast.c:92-102`）；字段见 `Ast.h:185-194,198-207`
- 类别：10 资源与生命周期 / 6 契约破坏
- 触发条件：销毁 `AST_FUNCTION_DECLARATION` 或 `AST_STATIC_FUNCTION_DECLARATION`。
- 后果：`parameter_default_values` 数组及其子节点、`parameter_is_nullable`、`parameter_is_array` 两个 int 数组全部泄漏；visit/clone 都认得这些字段，唯独 destroy 漏。
- 最小修复：在 helper 中补 `free_ast_node_array(parameter_default_values, param_count)` 与两个 `KRT_FREE`。
- 备注：clone 明确克隆了这三组（`AstClone.inc:147-149,157-159`），证明属自有资源。

### [严重] 66-36｜constructor_decl 漏释 base_arguments
- 位置：`Ast.c:261-267`；字段见 `Ast.h:361-366`
- 类别：10 / 6
- 触发条件：销毁构造函数声明。
- 后果：`base_arguments` 数组及其中基类实参子树泄漏（clone 已克隆，`AstClone.inc:290`）。
- 最小修复：补 `free_ast_node_array(constructor_decl.base_arguments, base_argument_count)`。

### [严重] 66-37｜property_decl 漏释 backing_field_name
- 位置：`Ast.c:364-378`；字段见 `Ast.h:466-473`
- 类别：10 / 6
- 触发条件：销毁属性声明。
- 后果：`backing_field_name` 字符串泄漏（clone 已克隆，`AstClone.inc:376`）。
- 最小修复：补 `KRT_FREE(property_decl.backing_field_name)`。

### [严重] 66-38｜is_expr 漏释 binding_name
- 位置：`Ast.c:506-516`；字段见 `Ast.h:557-561`
- 类别：10 / 6
- 触发条件：销毁 is 表达式。
- 后果：`binding_name` 字符串泄漏（clone 已克隆，`AstClone.inc:449`）。
- 最小修复：补 `KRT_FREE(is_expr.binding_name)`。

### [严重] 66-39｜default 分支仍 KRT_FREE(node)，释放行为随分配方式分叉
- 位置：`Ast.c:563-567`
- 类别：3 语义分叉 / 9 错误处理不一致 / 10
- 触发条件：destroy 一个未覆盖类型（如 66-3～66-34）的节点。
- 后果：堆节点只释放节点本体、子资源全泄漏；arena 节点连节点本体都不释放（66-1）。同一类型在不同分配路径下销毁语义不同。
- 最小修复：补齐 case；明确「节点本体归属由 is_arena_allocated 决定、字段资源一律释放」的统一规则。
- 备注：与 66-1 互为表里。

### [严重] 66-40｜KrtAstVisit 无深度上限、无环/共享子树检测
- 位置：`AstVisit.inc:2-408`
- 类别：8 未检查前置条件（递归无界/深度）/ 7 共享可变状态
- 触发条件：深层嵌套 AST，或 DAG/共享子节点（如 attribute_list.target 与 attributes 指向同一节点）。
- 后果：栈溢出崩溃；共享子树被重复访问，变异型 visitor 会对同一节点重复施加变换。
- 最小修复：加深度参数/上限与 visited 集合（或要求 visitor 幂等并在文档中写明）。
- 备注：`TypeExpansion.inc:334` 的 `expansion_substitute_node` 是变异型 visitor，重复访问会重复替换。

### [严重] 66-41｜KrtAstVisit 返回 void，visitor 返回 false 被静默截断
- 位置：`AstVisit.inc:2-5`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：任一 visitor 返回 false（如 `expansion_resolve_node`/`expansion_substitute_node` 出错）。
- 后果：遍历中途停止，函数无返回值、无诊断，调用方（`TypeExpansion.inc:117,334,446`）无法感知只处理了部分树，后续阶段基于不完整结果继续。
- 最小修复：返回 bool/错误码，或提供「遇 false 立即报错」的契约与日志。
- 备注：访问器返回值被忽略处即 `AstVisit.inc:3` 的 `!visitor(...)` 判定。

### [严重] 66-42｜visitor 在类型派发前调用，允许变异/释放节点导致悬垂访问
- 位置：`AstVisit.inc:3-6`
- 类别：7 共享可变状态 / 8 未检查前置条件
- 触发条件：visitor 释放当前节点或改写其子指针（TypeExpansion 的替换型 visitor 即属此类）。
- 后果：随后 `switch` 读取 `node->data.*` 可能是已释放/已改写的内存，造成 UAF 或漏访新子树。
- 最小修复：先缓存本节点需要的子指针再调用 visitor，或明确规定 visitor 不得释放当前节点并加断言。
- 备注：需验证 `expansion_substitute_node` 是否原地改写 `node->data`。

### [中] 66-43｜ast_create_node 无任何调用方，堆节点清理路径实际不可达
- 位置：`Ast.c:9-11`；全仓 grep 仅 `Ast.h:681` 声明与本定义
- 类别：5 死代码 / 6 契约破坏
- 触发条件：——
- 后果：公开 API 暗示支持堆节点，但全仓所有节点都走 `ast_create_node_arena`（`ParserBase.h:30`），destroy 中针对堆节点的整段 switch 分支成为不可达代码；一旦有人用 `ast_create_node` 建节点并填入 arena 数组（parser 的数组正是 arena 分配，`ParserExpression.c:30`），destroy 会 `KRT_FREE` arena 指针 → 堆损坏。
- 最小修复：删除 `ast_create_node`，或让 destroy 严格按字段分配方式释放并加文档/断言。
- 备注：需验证是否存在测试或外部绑定调用 `ast_create_node`。

### [中] 66-44｜free_function_decl_data 以 const char* 接收 name 却强制转换后 free
- 位置：`Ast.c:55-59`
- 类别：6 契约破坏 / 10
- 触发条件：调用方传入字符串字面量或只读内存。
- 后果：`KRT_FREE((void*)name)` 释放只读内存 → 崩溃；const 契约与实际行为矛盾。
- 最小修复：形参改为 `char*`，与 node 字段类型一致。

### [中] 66-45｜AST_CALL 用 argument_count 释放 argument_names，计数假设未校验
- 位置：`Ast.c:210`；字段见 `Ast.h:293-302`
- 类别：8 未检查前置条件 / 10
- 触发条件：`argument_names` 非空但实际长度与 `argument_count` 不一致（命名参数少于位置参数时若只分配命名个数）。
- 后果：越界读/越界写或漏释。
- 最小修复：显式保存 `argument_name_count` 字段，或断言二者相等。
- 备注：clone 同样按 `argument_count` 克隆（`AstClone.inc:235`），需验证 parser 是否总是分配 `argument_count` 长。

### [中] 66-46｜AstVisit.inc 无 include guard，存在重复包含风险
- 位置：`AstVisit.inc:1-2`；包含点 `Ast.c:570`
- 类别：6 契约破坏 / 5 死代码（潜在）
- 触发条件：其他文件再次 `#include "AstVisit.inc"`。
- 后果：`KrtAstVisit` 重复定义，链接错误；当前仅 `Ast.c` 包含，属潜伏缺陷。
- 最小修复：加 `#pragma once`/`#ifndef`  guard，或改为头文件声明 + 单独 .c。

### [中] 66-47｜ast_create_node_arena 分配失败静默返回 NULL
- 位置：`Ast.c:13-23`
- 类别：1 静默失败 / 2 诊断缺失（G1）
- 触发条件：`KrtArenaAlloc`/`KRT_MALLOC` 失败。
- 后果：函数无日志直接返回 NULL；`KrtMemoryReportError` 为空操作，分配失败零诊断。
- 最小修复：失败时调用诊断接口或返回可区分错误；至少在有诊断框架处上报。
- 备注：`ParserBase.h:31-32` 用 `KRT_COMPILE_ERROR` 兜底，故 parser 路径最终会 exit；但其他调用方无保障。

### [轻] 66-48｜AST_THIS 空 case 多余空行
- 位置：`Ast.c:227-229`
- 类别：13 注释与实现脱节 / 5 死代码
- 触发条件：——
- 后果：无功能影响，但暴露 destroy 表是手工维护、易漏写。
- 最小修复：删除空行或与叶子类型合并 case。
- 备注：与 66-2 同一根因。

### [中] 66-49｜测试不足：无 clone/visit/destroy 覆盖矩阵测试
- 位置：`Ast.c`、`AstVisit.inc` 全文（无对应测试断言）
- 类别：12 测试不足
- 触发条件：新增节点类型或修改 destroy 表。
- 后果：32 个类型漏销毁长期未被发现；无回归测试保证三表一致。
- 最小修复：加一个遍历 106 个类型的覆盖测试，并对每类构造最小节点验证 destroy 后无泄漏（ASan）。
- 备注：需验证测试目录是否已有此类用例。
