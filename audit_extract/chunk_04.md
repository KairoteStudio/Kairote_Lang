# Chunk 04 抽取

## 覆盖范围
- 第十八批：SemanticAnalyzer.c:2300-2500（原文行 2059）
- 第十九批：SemanticAnalyzer.c:2500-2700（行 2117）
- 第二十批：SemanticAnalyzer.c:2700-2900（行 2185）
- 第二十一批：SemanticAnalyzer.c:2900-3100（行 2249）
- 第二十二批：SemanticAnalyzer.c:3100-3300（行 2313）
- 第二十三批：SemanticAnalyzer.c:3300-3500（行 2380）
- 第二十四批：SemanticAnalyzer.c:3500-3700（行 2438）
- 第二十五批：SemanticAnalyzer.c:3700-3900（行 2494）
- 第二十六批：SemanticAnalyzer.c:3900-4099（行 2545）
- 第二十七批：StdlibCache.inc（全 201 行）+ AstClone.inc（全 561 行）+ ParserBase.c:60-141（行 2642）
- 第二十八批：IrGen.c:1-420 + 运行时实证（行 2754）
- 第二十九批：IrGen.c 语句生成·控制流（行 2906）
- 全局修订应用：G1 已标注于分配失败相关活路径条目；G2 已作废 S28-1；G3（S38 系列）不在本块范围内。

## 问题条目

### [致命] F18-1｜重开已存在命名空间时函数体完全不分析
- 位置：`SemanticAnalyzer.c:2382-2393`
- 类别：静默失败
- 触发条件：重新打开一个已存在的命名空间（`is_new_namespace == false`），整个语句分析循环被 `if (is_new_namespace)` 包住，函数体完全不分析，只跑了 2365-2381 的类预声明。
- 后果：`namespace N { fn f() { 非法代码 } }` 第二次出现时所有语义错误被吞，成员符号不进入符号表，后续引用报「未定义」或静默通过。
- 最小修复：去掉 `if (is_new_namespace)` 包裹（预声明循环可跳过，语句分析必须执行）。

### [致命] F18-2｜命名空间体类型判断冗余致分支永不可达
- 位置：`SemanticAnalyzer.c:2362-2411`
- 类别：死代码
- 触发条件：外层 `if (body_node && body_node->type == AST_BLOCK)` 已保证类型，内部 `:2364` 再次判断 `AST_BLOCK` 恒真，其 `else if (AST_ACCESS_MODIFIER)`（:2394）与 `else if (is_new_namespace)`（:2409）永不可达。
- 后果：非 block 的命名空间体（单类声明）永远不会被分析；注释/实现脱节。
- 最小修复：把 `AST_BLOCK`/`AST_ACCESS_MODIFIER`/`else` 分支直接挂在外层 `if (body_node)` 上，删除 :2364 冗余判断。
- 备注：非分配失败类死代码，不适用 G1 重分类。

### [高] F18-3｜nested_table 创建失败静默跳过且作用域不切换
- 位置：`SemanticAnalyzer.c:2340-2344` + `:2353`
- 类别：静默失败 + 契约破坏
- 触发条件：`ns_entry->nested_table = symbol_table_create();` 失败返回 NULL 时，`parent_table` 赋值被跳过且无诊断；`:2353` 判定为假，`analyzer->symbol_table` 不切换。
- 后果：命名空间成员被定义到父作用域，作用域语义破坏，错误被吞。
- 最小修复：创建失败时 `semantic_analyzer_add_error` + `return false`。
- 备注：【修订 G1】分配失败路径为活路径且零诊断（KRT_* 失败返回 NULL），应按后果重定严重度；当前已判高。

### [高] F18-4｜foreach 循环变量类型从不推导
- 位置：`SemanticAnalyzer.c:2295-2313`
- 类别：阶段不对齐（类型推导缺失）
- 触发条件：`loop_var` 定义后从不从 `iterable->resolved_type` 推导元素类型，`loop_var->source_type` 保持零值。
- 后果：循环体内 `x` 类型为 TOKEN_NONE/INT32 兜底，成员访问、重载解析、指针深度全部错误；循环外类型不可知。
- 最小修复：从 `iterable->resolved_type` 推导元素类型（数组→元素类型，指针→pointee，`Length` 等）写入 `loop_var->source_type`。

### [高] F18-5｜重复类检查可被 SYMBOL_DECLARED 绕过
- 位置：`SemanticAnalyzer.c:2423`
- 类别：静默失败
- 触发条件：重复类检查仅当 `existing->state == SYMBOL_DEFINED` 才报错；同名符号处于 `SYMBOL_DECLARED`（如 :2375 预声明）时检查被绕过。
- 后果：`symbol_table_define`（:2448）可能静默覆盖既有声明，符号表状态不一致。
- 最小修复：改为 `if (existing) { error; return false; }`，或明确区分 declared/defined 的合法重声明场景。

### [中] F18-6｜模板参数全部添加失败时静默跳过注册
- 位置：`SemanticAnalyzer.c:2435`
- 类别：静默失败
- 触发条件：`template_param_count > 0` 但 `generics_add_parameter` 全部失败导致 `params_list == NULL` 时，静默跳过模板注册，无诊断。
- 后果：模板类未注册，后续实例化静默失败。
- 最小修复：`if (!params_list) { error; return false; }`。

### [中] F18-7｜current_scope 判空分支为冗余防御
- 位置：`SemanticAnalyzer.c:2465-2467`
- 类别：死代码（疑似）
- 触发条件：`if (!analyzer->symbol_table->current_scope) { symbol_table_push_scope(...); }`——`symbol_table_create()` 已设置 `global_scope` 与 `current_scope`，此分支大概率为死。
- 后果：误导性防御代码。
- 最小修复：验证后删除，或改为断言。
- 备注：非分配失败类死代码，不适用 G1 重分类。

### [高] F19-1｜命名空间内 main 劫持入口且多 main 静默覆盖
- 位置：`SemanticAnalyzer.c:2657-2663`
- 类别：静默失败 + 语义分叉
- 触发条件：入口点判定只用 `!current_class && strcmp(func_name, "main") == 0`；命名空间内 `main`（`current_class == NULL`）也被判为入口点；多个 `main` 时 `entry_point_name` 被后者静默覆盖（last-wins）。
- 后果：`namespace N { fn main() {} }` 劫持程序入口；多 main 冲突无诊断，链接/运行期行为不可预期。
- 最小修复：增加「当前作用域为全局作用域且不在任何命名空间内」的判定；检测到第二个 main 时报错。

### [高] F19-2｜参数类型绑定无越界保护
- 位置：`SemanticAnalyzer.c:2674`
- 类别：未检查前置条件
- 触发条件：`stmt->function_type->parameters[i]` 与 `stmt->data.function_decl.parameters[i]` 均无越界/判空保护；`function_type->parameter_count < parameter_count`（上游解析不一致）即越界读。
- 后果：崩溃或读到垃圾 `KrtSourceType`，参数类型全部错误。
- 最小修复：`if (i < stmt->function_type->parameter_count) param->source_type = ...`，并断言两处 count 一致。
- 备注：【修订 G1】相邻调用点 `SemanticAnalyzer.c:2677` 为未检查分配调用（失败返回 NULL 且零诊断），属活路径，OOM 时 NULL 静默传播。

### [高] F19-3｜类方法预声明可绕过重复定义检查并被静默覆盖
- 位置：`SemanticAnalyzer.c:2638` + `:2644`
- 类别：静默失败
- 触发条件：重复定义检查只看 `SYMBOL_DEFINED`；类方法在 `:2516` 被预声明为 `SYMBOL_DECLARED`，同名同签名方法因此绕过检查，`:2644` `symbol_table_define` 静默覆盖预声明符号。
- 后果：前向声明/预声明方法符号被覆盖，`source_type`/`value_type` 可能来自错误声明。
- 最小修复：`if (existing) { error; return false; }`，重声明走显式 `state` 转换路径。

### [高] F19-4｜name_mangle_function 失败静默 continue
- 位置：`SemanticAnalyzer.c:2508-2511`
- 类别：静默失败
- 触发条件：`name_mangle_function` 返回 NULL 时直接 `continue`，成员符号不被定义，无诊断。
- 后果：该类所有方法在符号表中缺失 → 调用报「未定义成员」，错误位置指向调用点而非声明点。
- 最小修复：`if (!key) { semantic_analyzer_add_error_at(...); body_ok = false; continue; }`。

### [高] F19-5｜类体首错后停止分析其余成员
- 位置：`SemanticAnalyzer.c:2527`
- 类别：诊断缺失（多错误只报一个）
- 触发条件：`for (...; ...; && body_ok; ...)` 在首个错误后停止分析类体其余成员。
- 后果：类体后续成员的所有错误被吞，用户只看到第一个。
- 最小修复：去掉 `&& body_ok`，用 `body_ok = semantic_analyzer_analyze_statement(...) && body_ok;` 累积。

### [中] F19-6｜switch 不校验 case 常量/重复且 case 无独立作用域
- 位置：`SemanticAnalyzer.c:2553-2568`
- 类别：诊断缺失
- 触发条件：switch 不校验 case 值是否为常量表达式、不检测重复 case / 重复 default；case 体没有独立作用域（各 case 共享 switch 作用域）。
- 后果：非常量 case 静默通过；case 内声明变量泄漏到后续 case，造成符号遮蔽/重定义歧义。
- 最小修复：case 值做常量判定；每个 case 推入/弹出作用域；重复 case 报错。

### [中] F19-7｜unsafe_call permissions 未判空即 strcmp
- 位置：`SemanticAnalyzer.c:2602`
- 类别：未检查前置条件
- 触发条件：`strcmp(stmt->data.unsafe_call.permissions[i], "krt.mem")` 未判空。
- 后果：权限项为 NULL 时崩溃。
- 最小修复：`if (stmt->data.unsafe_call.permissions[i] && !strcmp(...))`。

### [中] F19-8｜空访问修饰符节点静默 return true
- 位置：`SemanticAnalyzer.c:2613-2619`
- 类别：静默失败
- 触发条件：`access_modifier.member == NULL` 时静默 `return true`。
- 后果：空的访问修饰符节点被当作合法语句吞掉。
- 最小修复：报「access modifier without member」并返回 false。

### [中] F19-9｜未知类体成员类型静默 continue
- 位置：`SemanticAnalyzer.c:2503-2505`
- 类别：静默失败
- 触发条件：类体成员类型不是 `STATIC_FUNCTION_DECLARATION` / `FUNCTION_DECLARATION` 时静默 `continue`，不报错也不分析。
- 后果：未知成员节点被吞，符号表缺失。
- 最小修复：`semantic_analyzer_add_error_at(analyzer, member, "Unsupported class member type");`。

### [致命] F20-1｜静态方法跨作用域链复用符号致原地污染
- 位置：`SemanticAnalyzer.c:2718` + `:2727`
- 类别：共享可变状态 + 语义分叉
- 触发条件：静态方法用 `symbol_table_lookup_scope_chain`（跨作用域链）查找，命中后 `func_sym = existing;` 直接复用该符号，随后 `:2738-2741` 在该符号所在作用域改写 `state/value_type/source_type`。
- 后果：外层作用域同名符号（函数/变量）被静默当作本类静态方法并原地污染；当前类嵌套表无该方法符号 → 后续调用解析失败。
- 最小修复：改用 `symbol_table_lookup_current_scope`；`existing` 仅用于重复定义判定，不得复用；始终在当前作用域 `symbol_table_define`。

### [高] F20-2｜静态方法 mangle 失败静默回退裸名
- 位置：`SemanticAnalyzer.c:2709-2716`
- 类别：静默失败 + 语义分叉
- 触发条件：`name_mangle_function` 失败时 `if (mangled_name) function_name = mangled_name;` 静默回退为裸方法名；对比实例方法 `:2630-2633` 同场景是报错 + `return false`。
- 后果：同一语义两种处理；静态方法以裸名进入符号表，与实例方法 mangled 名并存 → 重载/调用解析分叉。
- 最小修复：与实例方法统一，mangle 失败即报错返回。

### [高] F20-3｜静态方法无 body 保护致合法前向声明误报
- 位置：`SemanticAnalyzer.c:2766`
- 类别：诊断与实际错误不符
- 触发条件：静态方法无 `if (body)` 保护（对比 `function_declaration:2666` 有保护）。body 为 NULL（纯声明）时 `analyze_statement(NULL)` 返回 false → `:2767-2772` 再叠加 "Not all control paths return a value"。
- 后果：合法的静态方法前向声明被报成控制流错误；`body_ok` 被置 false。
- 最小修复：`if (!stmt->data.static_function_decl.body) { free; return true; }`，与实例方法对齐。

### [高] F20-4｜构造器上下文错误/mangle 失败无诊断
- 位置：`SemanticAnalyzer.c:2784-2786` / `:2790-2792`
- 类别：诊断缺失
- 触发条件：构造器在类上下文之外（`!current_class`）或 `name_mangle_constructor` 返回 NULL 时直接 `return false`，无任何诊断。
- 后果：错误被上层当作「分析失败」吞掉，用户看不到原因。
- 最小修复：两处都补 `semantic_analyzer_add_error_at(analyzer, stmt, ...)`。

### [高] F20-5｜构造器符号从不设置 value_type/source_type
- 位置：`SemanticAnalyzer.c:2807-2808`
- 类别：静默失败 + 阶段不对齐
- 触发条件：构造器符号只设置 `state = SYMBOL_DEFINED`，从不设置 `value_type` / `source_type`。
- 后果：构造器类型全零，调用点（`infer_expression_type`）拿到 TOKEN_NONE/INT32 兜底 → 参数类型检查、重载评分全部错误。
- 最小修复：从 `stmt->function_type`（或参数表）构造 `TOKEN_FN` 类型写入 `value_type`/`source_type`，与实例方法一致。

### [高] F20-6｜静态方法参数绑定无越界保护（F19-2 同根第二处）
- 位置：`SemanticAnalyzer.c:2752`
- 类别：未检查前置条件
- 触发条件：`stmt->function_type->parameters[i]` 无越界保护（与 F19-2 同根）。
- 后果：`function_type->parameter_count < parameter_count` 时越界读。
- 最小修复：同 F19-2，并抽公共函数消除重复。
- 备注：【修订 G1】相邻调用点 `SemanticAnalyzer.c:2755` 为未检查分配调用（失败返回 NULL 且零诊断），属活路径，OOM 时 NULL 静默传播。

### [中] F20-7｜dispatcher 中 return 后 break 永不可达
- 位置：`SemanticAnalyzer.c:2863-2900`
- 类别：死代码
- 触发条件：`case AST_XXX: return semantic_...; break;` 中 `break` 在 `return` 之后永不可达（该模式出现 10+ 处）。
- 后果：误导性代码，看起来有 fall-through 保护。
- 最小修复：删除 `return` 后的 `break;`。

### [中] F20-8｜基类构造参数首错后停止分析
- 位置：`SemanticAnalyzer.c:2821`
- 类别：诊断缺失（多错误只报一个）
- 触发条件：`for (...; ... && base_ok; ...)` 在首个基类构造参数错误后停止。
- 后果：后续 base 参数错误被吞。
- 最小修复：`base_ok = semantic_analyzer_analyze_expression(...) && base_ok;`。

### [致命] F21-1｜语句 dispatcher default 静默返回 true
- 位置：`SemanticAnalyzer.c:2973-2974`
- 类别：静默失败
- 触发条件：语句 dispatcher 的 `default: return true;`——所有未处理的语句节点类型静默通过（与 F14-2 同根，但在语句层，影响更大）。
- 后果：未实现/新增语句类型（如 AST_MATCH_EXPRESSION、AST_DEFER、AST_TRY_CATCH 等）全部被吞，不做任何分析。
- 最小修复：`default: semantic_analyzer_add_error_at(analyzer, stmt, "Unsupported statement type %d", stmt->type); return false;`。

### [高] F21-2｜入口点判定第二处实现，类方法 main 也劫持入口
- 位置：`SemanticAnalyzer.c:3014-3021`
- 类别：语义分叉 + 静默失败
- 触发条件：入口点判定的第二处实现（第一处 :2657-2663），同样缺少 `!current_class` 判定，类方法 `main` 也会劫持入口；两处都覆盖 `analyzer->entry_point_name`，后执行者胜。
- 后果：同一语义两处实现且行为不一致（一处要求 `!current_class`，一处无条件）；`main` 冲突静默。
- 最小修复：合并为唯一的 `semantic_analyzer_mark_entry_point(analyzer, func_name, current_class)`，要求全局作用域且无类上下文，重复时报错。

### [高] F21-3｜has_return_statement 硬编码为 true
- 位置：`SemanticAnalyzer.c:3077`
- 类别：契约破坏
- 触发条件：`result.has_return_statement = true;` 硬编码，与函数实际是否有 return 无关。
- 后果：调用方若依赖该字段判断返回行为，得到永远为真的错误契约（该字段实际是死值）。
- 最小修复：从 body 分析结果推导（如 `semantic_statement_guarantees_return(body)`），或删除该字段。

### [高] F21-4｜静态函数返回类型误读 union 另一成员
- 位置：`SemanticAnalyzer.c:3062`
- 类别：契约破坏（union 成员误用）
- 触发条件：`analyzer->current_return_type = function_node->function_type ? ... : (KrtSourceType){.token = function_node->data.function_decl.return_type};`——对 `AST_STATIC_FUNCTION_DECLARATION`，当 `function_type == NULL` 时读的是 union 另一成员 `data.function_decl`。
- 后果：依赖 union 内存布局巧合；一旦两个成员偏移不同即读到垃圾返回类型。
- 最小修复：按 `function_node->type` 分支取 `data.static_function_decl.return_type`。

### [高] F21-5｜纯声明 body 为 NULL 被误报控制流错误（第三处）
- 位置：`SemanticAnalyzer.c:3063-3069`
- 类别：诊断与实际错误不符
- 触发条件：body 为 NULL（纯声明）时 `analyze_statement(NULL)` 返回 false，随后被报成 "Not all control paths return a value"（与 F20-3 同根）。
- 后果：合法声明被误报控制流错误。
- 最小修复：`if (!body) { result.success = true; return result; }`。

### [高] F21-6｜参数绑定无越界保护（第三处同根）
- 位置：`SemanticAnalyzer.c:3038`
- 类别：未检查前置条件
- 触发条件：`function_node->function_type->parameters[i]` 无越界保护（同根：:2674、:2752、:3038）。
- 后果：`function_type->parameter_count < param_count` 时越界读。
- 最小修复：抽 `semantic_analyzer_bind_parameters(analyzer, function_node, symbol_table)` 唯一实现，内部做边界断言。

### [高] F21-7｜analyze_function 入参非法静默失败无诊断
- 位置：`SemanticAnalyzer.c:2982-2985`
- 类别：诊断缺失
- 触发条件：`analyze_function` 入参非法时静默 `return result`（success=false），无诊断。
- 后果：调用方只看到失败，无原因。
- 最小修复：补 `semantic_analyzer_add_error_at(analyzer, function_node, "Invalid function node for analysis");`。

### [中] F21-8｜重复定义查找用全作用域链与 current_scope 不一致
- 位置：`SemanticAnalyzer.c:2991`
- 类别：语义分叉
- 触发条件：`symbol_table_lookup`（全作用域链）与 `function_declaration:2637` 的 `lookup_current_scope` 不一致。
- 后果：重复定义判定在不同路径上作用于不同作用域集合。
- 最小修复：统一为 current_scope（或明确记录 chain 查找的意图）。

### [致命] F22-1｜type_info->declaration 为 NULL 时实例化静默成功
- 位置：`SemanticAnalyzer.c:3150-3160`
- 类别：静默失败 + 死代码
- 触发条件：`type_info != NULL` 但 `type_info->declaration == NULL` 时，走 `else { if (type_info) { KRT_FREE(type_info); return true; } ... }`——实例化什么都没做却返回成功；内层 `if (type_info)` 在 `else`（`type_info` 为 NULL）分支里是死代码（`KRT_FREE(NULL)` 永不执行）。
- 后果：泛型类型声明缺失时实例化静默成功，下游拿到未实例化的符号；错误被吞。
- 最小修复：`if (!type_info) { error; return false; } if (!type_info->declaration) { error; KRT_FREE(type_info); return false; }`。
- 备注：【修订 G1】内层空指针死代码属 G1 所指「空指针检查类」判定，G1 前提修正后需重估；主结论（NULL declaration 静默 return true）仍成立。`generics_get_type_info` 返回堆拷贝、内部字段为借用指针，`KRT_FREE(type_info)` 只释放外壳，所有权疑虑已解除。

### [高] F22-2｜约束循环首处 strcmp 未判空
- 位置：`SemanticAnalyzer.c:3225` + `:3227`
- 类别：未检查前置条件 + 错误处理不一致
- 触发条件：第一个约束循环对 `constraint->data.generic_constraint.param_name` / `constraint_type` 未判空即 `strcmp`；同函数第二个循环 `:3251` 对同样字段做了判空。
- 后果：NULL 字段直接崩溃；同一函数内前置检查不一致。
- 最小修复：把判空逻辑抽到循环开头统一处理（两处共用）。

### [高] F22-3｜type_args 未判空且 class 白名单漏 char
- 位置：`SemanticAnalyzer.c:3270-3285`
- 类别：未检查前置条件 + 语义分叉
- 触发条件：`type_args[param_index]` 未判空即 `strcmp`；`class` 约束白名单为 `{"int","float","string","bool"}`，漏掉 `"char"`，而 `struct` 白名单含 `"char"` 不含 `"string"`。
- 后果：`where T : class` 配 `char` 被误报违反约束；两处白名单硬编码不一致，且与 tokenizer 类型集合分叉。
- 最小修复：`type_arg` 判空；白名单改为单一 `KrtTypeIsValueLike()` 辅助函数，两处共用。

### [高] F22-4｜未知约束类型静默忽略
- 位置：`SemanticAnalyzer.c:3293-3299`
- 类别：静默失败 + 诊断缺失
- 触发条件：`else` 分支对未知约束类型静默忽略，只对 `"IDisposable"` 做特判。
- 后果：拼写错误或新增约束（如 `IComparable`）静默失效，约束不生效且无诊断。
- 最小修复：`else { semantic_analyzer_add_error(analyzer, "Unknown constraint type '%s'", constraint_type); return false; }`。

### [高] F22-5｜非法模板参数节点静默跳过且返回值被忽略
- 位置：`SemanticAnalyzer.c:3108-3120`
- 类别：静默失败
- 触发条件：模板参数节点为 NULL 或类型不是 `AST_TEMPLATE_PARAMETER` 时静默跳过，`param_count` 不增加；`generics_add_parameter` 返回值也被忽略。
- 后果：注册参数个数少于实际个数 → 后续 `validate_constraints` 的 `arg_count != template_param_count` 判定错误（误报或误放行）。
- 最小修复：遇到非法参数节点时报错返回；检查 `generics_add_parameter` 返回值。

### [中] F22-6｜new() 约束不校验可构造性
- 位置：`SemanticAnalyzer.c:3291-3292`
- 类别：静默失败
- 触发条件：`new()` 约束仅 `continue`，不校验类型参数是否真的可构造。
- 后果：`where T : new()` 配结构体/无默认构造器的类型静默通过。
- 最小修复：查符号表确认存在默认构造器。

### [中] F22-7｜template_params[j] 未判空即 strcmp
- 位置：`SemanticAnalyzer.c:3258`
- 类别：未检查前置条件
- 触发条件：`strcmp(template_params[j], param_name)` 中 `template_params[j]` 未判空。
- 后果：模板参数数组含 NULL 时崩溃。
- 最小修复：`if (template_params[j] && strcmp(...) == 0)`。

### [致命] F23-1｜函数声明两条并行分析路径语义分叉
- 位置：`SemanticAnalyzer.c:3431-3439` vs `:2620-2701` / `:3441-3458`
- 类别：语义分叉（同一功能两套实现）
- 触发条件：顶层函数走 `semantic_analyzer_analyze_function`（:2978），类体内/经 dispatcher 的函数走 `semantic_analyze_stmt_function_declaration`（:2620）。二者在 mangle 失败处理（报错 vs 裸名回退）、入口点判定（有无 `current_class`）、参数绑定（越界保护）、返回类型来源（union 成员选择）上行为各不相同。
- 后果：同一份函数声明在不同位置被不同语义分析 → 类型/符号不一致；F19-1/F20-2/F21-2/F21-4 全部由此派生。
- 最小修复：合并为唯一 `semantic_analyzer_analyze_function_decl(analyzer, node)`，mangle/入口点/参数绑定只在其中实现一次；另一路径改为调用它。

### [高] F23-2｜semantic_expand_types 失败后继续分析残缺 AST
- 位置：`SemanticAnalyzer.c:3338-3340`
- 类别：契约破坏（失败后继续）
- 触发条件：`semantic_expand_types` 失败时仅置 `result->success = false`，不提前返回，随后在部分展开的 AST 上跑完整个分析流程。
- 后果：后续阶段基于残缺类型推导，产生大量伪错误或漏报；错误定位指向错误位置。
- 最小修复：`if (!semantic_expand_types(analyzer, ast)) { result->success = false; return result; }`。

### [高] F23-3｜symbol_table_check_undefined 返回值被丢弃
- 位置：`SemanticAnalyzer.c:3484`
- 类别：静默失败
- 触发条件：`symbol_table_check_undefined(analyzer->symbol_table);` 返回值被丢弃。
- 后果：未定义符号检查的发现不计入 `result->success`，符号表错误不影响编译成败（与批次十三「get_error_count 无人调用」叠加）。
- 最小修复：`if (!symbol_table_check_undefined(...)) result->success = false;`。

### [高] F23-4｜静态函数返回类型误读 union 另一成员（第二处）
- 位置：`SemanticAnalyzer.c:3396`
- 类别：契约破坏（union 成员误用）
- 触发条件：`pre->value_type = decl->data.function_decl.return_type;` 对 `AST_STATIC_FUNCTION_DECLARATION` 读 union 另一成员 `data.function_decl`（同 F21-4 模式）。
- 后果：依赖 union 布局巧合；布局变化即读到垃圾返回类型。
- 最小修复：按 `decl->type` 分支取 `data.static_function_decl.return_type`。

### [高] F23-5｜入口点兜底用跨作用域链查找 main
- 位置：`SemanticAnalyzer.c:3486-3491`
- 类别：语义分叉
- 触发条件：入口点兜底用 `symbol_table_lookup`（跨作用域链）找 `"main"`；命名空间内 main、类静态 main（若以裸名注册）都会命中。
- 后果：与 F19-1/F21-2 叠加 → 入口点可被非全局函数静默占用。
- 最小修复：改用 `symbol_table_lookup_current_scope` 在全局作用域查找。

### [中] F23-6｜FORWARD_REF 报错后不停止继续分析
- 位置：`SemanticAnalyzer.c:3378-3380`
- 类别：错误处理不一致
- 触发条件：`existing->state == SYMBOL_FORWARD_REF` 时报 "Class already declared"，但不返回 false、不停止，后续仍按已存在类处理。
- 后果：重复类声明错误后继续分析，产生二次错误。
- 最小修复：报错误后 `continue`（跳过该类后续处理）或记录 `result->success = false`。

### [中] F23-7｜顶层非 static 变量节点被重复分析
- 位置：`SemanticAnalyzer.c:3446-3449`
- 类别：重复处理
- 触发条件：顶层非 static 变量在 `:3420-3429` 已报错并置 `success = false`，此处再次调用 `semantic_analyzer_analyze_statement` 分析同一节点。
- 后果：同一节点两次分析，可能重复报错（诊断重复）。
- 最小修复：该分支改为只记录错误，不再分析。

### [高] F24-1｜success 被无条件重算覆盖累加语义
- 位置：`SemanticAnalyzer.c:3503-3507`
- 类别：语义分叉（同一状态两种更新方式）
- 触发条件：`if (result->error_count > 0) result->success = false; else result->success = true;` 无条件重算并覆盖 `success`；前面 `:3336/:3339/:3353/:3360/:3406/:3415/:3427/:3436/:3455/:3467/:3474/:3480` 全部用累加式 `result->success = false`。
- 后果：任何未通过 `add_error*` 计数的失败（如 `semantic_expand_types` 失败、`symbol_table_check_undefined` 返回值、未来新增失败路径）被静默抹掉 → 编译器报成功。
- 最小修复：删除 `:3503-3507` 的重算，保持 `success` 累加语义；错误计数单独由 `error_count` 承担。

### [高] F24-2｜path_parts[i] 未判空即 strcmp
- 位置：`SemanticAnalyzer.c:3528`
- 类别：未检查前置条件
- 触发条件：`strcmp(path_parts[i], "*")` 中 `path_parts[i]` 未判空。
- 后果：路径部分含 NULL 时崩溃。
- 最小修复：`if (!path_parts[i]) return NULL;`。

### [高] F24-3｜类型展开循环首错即整体终止
- 位置：`TypeExpansion.inc:445`
- 类别：诊断缺失（多错误只报一个）
- 触发条件：`for (int i = 0; i < context.count && !analyzer->error_count; i++)`——首个错误使 `error_count > 0` 后整个展开循环终止。
- 后果：AST 只被部分展开，后续分析基于残缺 AST，其余错误全部被吞。
- 最小修复：循环内不依赖全局 error_count 作为终止条件，改为累积局部 `ok`。

### [高] F24-4｜原地改写 AST 语句数组且契约未文档化
- 位置：`TypeExpansion.inc:448-452`
- 类别：契约破坏（阶段间假设未文档化）
- 触发条件：`ast->data.block.statement_count = context.count; ast->data.block.statements = expansion_alloc(...);` 原地改写 AST 的语句数量与指针，新 buffer 由独立分配器（`expansion_alloc`）给出。
- 后果：下游（IrGen/序列化/调试）若缓存旧 `statements` 指针或原 `statement_count` 即悬垂；该阶段契约完全未文档化、未断言。
- 最小修复：新增 `AST_PROGRAM.expanded` 标记或返回新 AST；至少加 `KRT_ASSERT(old_count == new_count)` 断言数量守恒。

### [中] F24-5｜路径 snprintf 返回值未检查致静默截断
- 位置：`SemanticAnalyzer.c:3640` / `:3666`
- 类别：静默失败
- 触发条件：`snprintf(sub, sizeof(sub), "%s/%s", ...)` 与 `snprintf(deep, sizeof(deep), "%s/%s", ...)` 返回值未检查。
- 后果：路径超长被静默截断 → 后续 `stat`/`opendir` 作用在错误路径上，导入静默失败。
- 最小修复：检查返回值 `>= sizeof(buf)` 时跳过该条目。

### [中] F24-6｜KRT_STRDUP 失败与文件不存在不可区分
- 位置：`SemanticAnalyzer.c:3608`
- 类别：静默失败
- 触发条件：`try_file_path` 中 `KRT_STRDUP(path)` 失败返回 NULL，调用方无法区分「文件不存在」与「内存不足」。
- 后果：OOM 被当作「文件不存在」处理。
- 最小修复：区分返回值语义或让调用方检查 `errno`。
- 备注：【修订 G1】KRT_STRDUP 失败返回 NULL 且零诊断，该 OOM 路径为活路径（非死代码），按后果维持「中」。

### [中] F24-7｜path 分量 size_t→int 转换可致溢出检查失效
- 位置：`SemanticAnalyzer.c:3535`
- 类别：整数溢出（未检查前置条件）
- 触发条件：`part_len = (int)strlen(path_parts[i])`——超长路径分量使 `size_t→int` 转换可能为负值，导致 `len + part_len + 2 > INT_MAX / 2` 检查失效。
- 后果：`new_capacity` 为负 → `KRT_REALLOC` 收到巨大 `size_t` → 失败返回 NULL 走错误路径（不崩溃），但检查形同虚设。
- 最小修复：`if (part_len < 0 || (size_t)part_len > INT_MAX - len - 2) { ... }`。
- 备注：【修订 G1】`KRT_REALLOC` 失败返回 NULL 且零诊断，属活路径。

### [高] F25-1｜静态函数名误读 union 另一成员（第三处同根）
- 位置：`SemanticAnalyzer.c:186`
- 类别：契约破坏（union 成员误用）
- 触发条件：`collect_exports_from_node` 的 `case AST_FUNCTION_DECLARATION: case AST_STATIC_FUNCTION_DECLARATION:` 分支中 `const char* name = node->data.function_decl.name;`——对静态函数读 union 另一成员。
- 后果：依赖 union 布局巧合（F21-4/F23-4 之后第三处同根）；布局变化即读到垃圾名字并注册错误符号。
- 最小修复：按 `node->type` 分支取 `data.static_function_decl.name`。

### [高] F25-2｜StdlibCache 共享 AST 原地改写疑点（已撤销）
- 位置：`SemanticAnalyzer.c:3769`
- 类别：共享可变状态（疑似，需验证）
- 触发条件：`KrtStdlibCacheClone(file_path, import_arena, false)` 若返回缓存中的共享 AST（而非深拷贝），随后 `:3788-3796` 起的 `semantic_analyzer_analyze_statement` 会原地改写 `resolved_type`、`data.call.name`（F14-1 的 `+= 8`）、`variable_decl.type`（F17-9）。
- 后果：缓存被污染，后续编译单元/轮次拿到被改写的 AST → 类型与符号全错。
- 最小修复：确认 `KrtStdlibCacheClone` 深拷贝语义；若为共享，改为 `KrtAstClone` 或每单元独立解析。
- 备注：【修订：第二十七批实证撤销】`KrtStdlibCacheClone` 经 `KrtAstClone` 深拷贝（`clone_source_type` 递归克隆 `KrtFunctionType`/`parameters`/`result`；switch 覆盖全部 106/106 `ASTNodeType`，`default: return NULL` 排除浅拷贝兜底），缓存 AST 与调用方 AST 无共享可变状态，本条不成立。

### [高] F25-3｜同一方法注册三次，裸名劫持全局符号
- 位置：`SemanticAnalyzer.c:3866-3900`
- 类别：语义分叉 + 静默覆盖
- 触发条件：同一方法被注册三次：mangled key（:3871）、`"%s__%s"` 别名（:3887）、裸方法名（:3899）。裸名注册到 `global_symbol_table` 会劫持全局命名空间同名函数（与 F14-9 `Length` 劫持同根）。
- 后果：全局函数 `write()` 与类方法 `write()` 冲突时后者覆盖前者；三份 `value_type`/`source_type` 重复设置，行为分叉。
- 最小修复：只保留 mangled key；别名/裸名改为 `SYMBOL_ALIAS` 引用同一符号，不重复定义。

### [中] F25-4｜导入文件 main 未计入入口点且无诊断
- 位置：`SemanticAnalyzer.c:3801-3813`
- 类别：阶段不对齐
- 触发条件：导入文件中的 `main` 被显式排除（:3801），但类静态 `main`（mangled 名）未被排除也未计入 `has_entry_point`。
- 后果：入口点判定在导入边界处不一致——主文件 `main` 才算入口，导入文件 `main` 静默忽略，编译产物可能没有入口而无任何诊断。
- 最小修复：统一入口点规则，导入文件的 main 应报错或明确忽略并给出说明。

### [中] F25-5｜full_name snprintf 返回值未检查致静默截断
- 位置：`SemanticAnalyzer.c:3883`
- 类别：静默失败
- 触发条件：`snprintf(full_name, sizeof(full_name), "%s__%s", class_name, mname)` 返回值未检查。
- 后果：类名+方法名超过 255 字节时静默截断，生成的别名可能与其他符号冲突，且错误地注册为合法别名。
- 最小修复：检查返回值 `>= sizeof(full_name)` 时跳过并诊断。

### [中] F25-6｜容量检查冗余（第一处）
- 位置：`SemanticAnalyzer.c:3710-3715`
- 类别：死代码（冗余检查）
- 触发条件：`analyzer->imported_source_capacity > INT_MAX / 2 || (size_t)(...) > SIZE_MAX / sizeof(...)`——第一个条件被第二个条件完全覆盖。
- 后果：误导性防御代码。
- 最小修复：删除第一个条件，保留 `size_t` 版本。

### [高] F26-1｜命名空间分隔符 `.` 与 `/` 不一致
- 位置：`SemanticAnalyzer.c:3561-3585` vs `:3512-3559`
- 类别：语义分叉（同一概念两种表示）
- 触发条件：`namespace_matches_using` 用 `.` 作分隔符匹配（:3578），而 `resolve_using_path` 用 `/` 拼接导入路径（:3550）。
- 后果：命名空间名含 `.` 时匹配判定与路径解析结果不一致 → 该 using 被错误判为项目命名空间（:4034）或解析到错误路径。
- 最小修复：统一分隔符（内部统一为 `.`，仅在文件系统拼接时转 `/`）。

### [中] F26-2｜目录枚举坏条目致后续文件全部不导入
- 位置：`SemanticAnalyzer.c:3966-3969`
- 类别：静默失败（不完整导入）
- 触发条件：目录枚举循环中任一条目 `snprintf` 截断或 `stat` 失败即 `success = false; break;`。
- 后果：该目录后续所有 `.krt` 文件全部不被导入，只产生一条错误；导入结果不完整且无明确说明。
- 最小修复：改为 `continue`（跳过坏条目），最后汇总。

### [中] F26-3｜首个 import_krt_file 失败即 break
- 位置：`SemanticAnalyzer.c:4005-4009`
- 类别：静默失败（不完整导入）
- 触发条件：`for` 循环中首个 `import_krt_file` 失败即 `break`。
- 后果：同目录下剩余文件静默不导入。
- 最小修复：累积 `success = false` 而不是 break。

### [中] F26-4｜容量检查冗余（第三处同模式）
- 位置：`SemanticAnalyzer.c:3974-3975`
- 类别：死代码（冗余检查）
- 触发条件：`capacity > INT_MAX / 2 || (size_t)(capacity * 2) > SIZE_MAX / sizeof(*paths)`——第一个条件被第二个完全覆盖（第三处同模式：`:3710`、`:3974`、另 `TypeExpansion.inc`）。
- 后果：误导性防御代码。
- 最小修复：统一抽 `safe_grow_capacity()` 辅助函数，删除冗余分支。

### [中] F26-5｜硬编码默认库路径 ./libs 与配置项并存
- 位置：`SemanticAnalyzer.c:4045`
- 类别：配置漂移
- 触发条件：`const char* libs_dir = analyzer->libs_path ? analyzer->libs_path : "./libs";`——硬编码默认库路径与 `libs_path` 配置项并存。
- 后果：未设置 `libs_path` 时静默使用 `./libs`，与用户预期「当前目录」可能不符；未文档化。
- 最小修复：默认值集中到配置模块，并在无 `libs_path` 时给出警告。

### [中] F26-6｜非法 using 指令静默 continue
- 位置：`SemanticAnalyzer.c:4031-4033`
- 类别：诊断缺失
- 触发条件：`!directive || !directive->namespace_path || directive->path_length == 0` 时静默 `continue`。
- 后果：非法 using 指令被吞，无任何诊断。
- 最小修复：报 "Malformed using directive" 并 `success = false`。

### [中] F26-7｜using X.* 展开到空目录即报错失败（疑似误报）
- 位置：`SemanticAnalyzer.c:3999-4001`
- 类别：诊断与实际错误不符（疑似误报）
- 触发条件：`using Foo.*` 展开到的目录中没有 `.krt` 文件时报 error 并失败。
- 后果：空子目录（合法存在）导致整个 using 失败。
- 最小修复：区分「目录不存在」（error）与「目录为空」（warning 或静默）。
- 备注：疑似；需验证 `using X.*` 的预期语义是否要求目录非空。

### [高] S27-1｜KrtStdlibCacheGetStats 与命中/未命中计数器零调用者
- 位置：`StdlibCache.inc:46-55`（计数器 `:22/:155/:177`；声明 `CompilerPipeline.h:71`）
- 类别：死代码
- 触发条件：`KrtStdlibCacheGetStats` 零外部调用者；`g_stdlib_cache_hits/misses` 只写不读。
- 后果：无效统计代码。
- 最小修复：删除该函数与两个计数器，或接入诊断输出。

### [高] S27-2｜全局缓存锁在锁内做 I/O 与 AST 克隆致完全串行
- 位置：`StdlibCache.inc:142,156,178,185`
- 类别：共享可变状态/并发
- 触发条件：并行编译多文件同时 `using` 同一 stdlib（`ParallelCompiler.c`）。
- 后果：全局 `g_stdlib_cache_mutex` 在锁内执行文件 I/O、词法/语法分析、万级节点 AST 克隆 → 所有导入完全串行化；一线程阻塞则全部编译线程停滞。
- 最小修复：锁内只做查找+指针交换，解析/克隆移到锁外；用 per-entry mutex 或引用计数。

### [高] S27-3｜缓存键不感知编译器版本与宏集合
- 位置：`StdlibCache.inc:151-153`
- 类别：配置漂移
- 触发条件：升级 KrtC / 修改 `load_standard_macros` 后继续编译。
- 后果：缓存键仅 `(path, expand_macros)+stat`，不感知编译器版本与宏集合 → 命中旧 AST，跨版本语义分叉且无告警。
- 最小修复：缓存键加入编译器版本号+宏集合哈希，或提供 `--no-stdlib-cache`。

### [中] S27-4｜stdlib 九个失败出口无法区分原因
- 位置：`StdlibCache.inc:74-135`
- 类别：诊断缺失
- 触发条件：stdlib 文件损坏/不可读。
- 后果：9 个失败出口（`:76/:80/:93/:99/:111/:115/:119/:123/:128`）全只返回 `bool`，SA:3772 只能报 "unable to read or parse imported source"，无法区分文件不存在 / fopen 失败 / fread 短读 / 预处理器失败 / 词法错误 / 语法错误。
- 最小修复：加 `const char** reason` 出参或在函数内带行号报错。

### [中] S27-5｜parse_stdlib_entry 失败时部分初始化依赖唯一调用方兜底
- 位置：`StdlibCache.inc:104-130` + `:24-34`
- 类别：契约破坏
- 触发条件：未来出现第二个调用方。
- 后果：`parse_stdlib_entry` 失败时 `entry` 处于部分初始化状态（source/path/lexer/parser 部分填充），依赖唯一调用方 `:179` 的 `free_stdlib_entry` 兜底；`free_stdlib_entry` 又依赖 `memset` 零初始化（:33）。契约未文档化。
- 最小修复：函数入口 `memset(entry,0,...)`，失败时自行清理，或显式注释该契约。

### [中] S27-6｜path==NULL 静默返回 NULL，调用方 printf("%s", NULL) 是 UB
- 位置：`StdlibCache.inc:138-140`
- 类别：未检查前置条件
- 触发条件：`path == NULL`（导出 API，`CompilerPipeline.h:73`）。
- 后果：静默返回 NULL；调用方 SA:3772 以 `'%s'` 打印 path → `printf("%s", NULL)` 是 UB（glibc 打 "(null)"，其他 libc 崩溃）。
- 最小修复：判空后用 `%s` 安全值，或 `KRT_COMPILE_ERROR`。

### [中] S27-7｜parser_parse 返回非 PROGRAM/BLOCK 时整个导入静默成功
- 位置：`SemanticAnalyzer.c:3787`
- 类别：静默失败
- 触发条件：`parser_parse` 返回非 PROGRAM/BLOCK 根节点。
- 后果：无 `else` 分支 → 整个导入静默「成功」（`success` 仅比较 error_count），符号表零导入符号，编译继续。
- 最小修复：加 `else { semantic_analyzer_add_error(...); }`。

### [疑似] S27-8｜缓存字节统计漏算 declared_functions 与预处理器产物
- 位置：`StdlibCache.inc:132-133`
- 类别：静默失败/统计失真（疑似，需验证）
- 触发条件：`entry->bytes` 只统计 `source + parser->arena + lexer->arena`，不含 `parser->declared_functions` 数组（KRT_MALLOC）与 Preprocessor 中间产物。
- 后果：64MB 上限可能系统性低估。
- 最小修复：补齐统计项。
- 备注：需验证——构造大 stdlib 文件，对比 `g_stdlib_cache_bytes` 与进程 RSS 增量。

### [疑似] S27-9｜atexit 清理与并发 Clone 的数据竞争
- 位置：`StdlibCache.inc:58`
- 类别：资源生命周期/并发（疑似，需验证）
- 触发条件：`atexit(KrtStdlibCacheClear)` 在进程退出时调 `parser_destroy`，若此时仍有线程在 `KrtStdlibCacheClone` 锁内。
- 后果：数据竞争。
- 最小修复：退出前 join 所有编译线程。
- 备注：需验证——是否存在编译线程未 join 就退出的路径。

### [致命] S28-1｜数组字面量元素步长语义分叉【已作废（G2）】
- 位置：`Re.KrtC/src/Ir/IrGen.c:1841-1855`（表达式路径）vs `:2742-2760`（变量初始化路径）
- 类别：语义分叉 + 静默错误数据
- 触发条件（原）：数组字面量出现在赋值右侧、函数实参、返回值等表达式上下文（dispatcher `:2166` 走 `:1841`）。
- 代码事实（原）：表达式路径 `:1848` `total_size = element_count * KRT_IMM_PTR_SIZE(builder)`（硬编码 8），`:1853` `KrtIrArrayStore(...)`（`Ir.c:1136-1138` 内部硬编码 size=8）；变量初始化路径 `:2751` 分配 `element_count * size`，`:2756` 用 `KrtIrArrayStoreSized(..., size)`。
- 后果（原）：表达式上下文数组字面量静默读错位置，产生错误数据，无编译错误/越界报警。
- 最小修复（原）：`:1848` 改为 `element_count * irgen_array_element_size(...)`；`:1853` 改用 `KrtIrArrayStoreSized(..., element_size)`；合并为唯一实现。
- 备注：【修订 G2】本条整体作废。原结论基于非法语法 `function int32 main()`，实证无效。正确结论见 S31-1：声明上下文正确，仅非声明上下文数组字面量步长错误。

### [高] S28-2｜get/set 作函数名被静默误解析为属性访问器
- 位置：`IrGen.c:251-256` 相关解析入口（实证触发）
- 类别：诊断与实际错误不符 + 语义分叉
- 触发条件：`int32 get(int32 a) { return a; }` → 编译报 `error: Undefined identifier: a`；`set` 同理；`foo`/`value` 正常。
- 后果：解析器把签名当 getter/setter，参数不进作用域，且不报「get/set 是保留字」，报的是无关的「未定义标识符」，误导排查。
- 最小修复：保留字作函数名时在声明处直接报「保留字不可作函数名」，而非进入属性访问器解析。

### [高] S28-3｜irgen_size_from_token 的 default: return 8 静默兜底
- 位置：`IrGen.c:54-56`
- 类别：静默失败 + 未检查前置条件
- 触发条件：`TOKEN_VOID`/`TOKEN_EOF`/`TOKEN_TYPE_STRING`/`TOKEN_FN` 全部静默当 8 字节。调用点 `:2197`（`sizeof`）、数组 stride `:1361/1367/1534/1858/2037/2233/2341/2506`。
- 后果：`sizeof(void)` 静默得 8；元素类型未解析时 stride 错误 → 后续读写越界。
- 最小修复：非数据 token 返回 0 并让调用方报「无法确定大小」，禁止默认 8。

### [高] S28-4｜irgen_checked_array_index 元素类型为 TOKEN_EOF 时跳过越界检查
- 位置：`IrGen.c:126-128`
- 类别：静默失败 + 契约破坏
- 触发条件：任何元素类型解析失败（返回 `TOKEN_EOF`）的数组下标访问。
- 后果：安全特性静默失效，越界检查被直接跳过，只做 `KrtIrCast(index, TOKEN_UINT64)`。
- 最小修复：元素类型未知时报编译错误，而非跳过检查。

### [高] S28-5｜new T[] 缺 size 静默返回 0 长度数组
- 位置：`IrGen.c:289-291`
- 类别：静默失败 + 诊断缺失
- 触发条件：`new T[]` 缺 size 表达式。
- 后果：`return KRT_IMM_ZERO(builder)` 当作「空数组」，下游按 0 长度处理，错误被吞。
- 最小修复：报「new 数组缺少长度」并终止。

### [高] S28-6｜堆分配乘法溢出未检查
- 位置：`IrGen.c:295` `KrtIrMul(count, element_size)`
- 类别：未检查前置条件
- 触发条件：`new int64[0x2000000000000000]` → 乘积回绕到 0 或极小值。
- 后果：分配过小但 length 记大值 → 堆越界写。
- 最小修复：生成代码前或运行期检查 `count > UINT64_MAX/element_size`。

### [高] S28-7｜irgen_free_memory 对非 mmap 指针调 munmap(ptr-16)
- 位置：`IrGen.c:353-374`，调用点 `:1661`（`KrtFree`）、`:1722`（`delete obj`）、`:2679`（delete 语句）
- 类别：未检查前置条件 + 资源生命周期
- 触发条件：对 `stackalloc` 缓冲区执行 `delete`/`KrtFree`。`irgen_expr_stackalloc_expression:2075-2076` 用 `KrtIrStackAlloc`，不走 `irgen_allocate_memory`，没有 16 字节头。
- 后果：读 `ptr-8` 当 total_size，对无效地址 `munmap` → 崩溃/破坏内存。
- 最小修复：引入所有权标记（指针头或元数据），释放时校验来源；编译期对 stackalloc 结果拒绝 delete。

### [中] S28-8｜g_generic_context 为 _Thread_local 全局，嵌套泛型互相覆盖
- 位置：`IrGen.c:172`
- 类别：共享可变状态
- 触发条件：泛型函数内再调泛型函数（嵌套实例化）。
- 后果：外层 context 被覆盖且无法恢复，实例化错乱。
- 最小修复：改为按调用栈保存/恢复，或用参数显式传递。

### [中] S28-9｜魔数散落（syscall 号、mmap flags）
- 位置：`:155-156`（exit 70 / syscall 60）、`:318`（mmap 9）、`:370`（munmap 11）、`:315-317`（flags 3 / 0x22）
- 类别：语义分叉 + 配置漂移
- 触发条件：同一 ABI 常量在多处硬编码。
- 后果：平台移植时漂移。
- 最小修复：集中到 `PlatformAbstraction`/`NativeAbi` 唯一常量表。

### [中] S28-10｜KRT_IRGEN_CHECK_ALLOC 宏错误值契约不统一
- 位置：`IrGen.c:202-207`
- 类别：契约破坏 + 错误处理不一致
- 触发条件：用于返回 `KrtIRValue` 结构体的函数时用 `void_val_return()`（`:1888`）而非真 NULL。
- 后果：调用方无法用统一方式判断失败。
- 最小修复：统一返回 `NULL` 或统一返回带错误位的 value，二选一并全量替换。

### [中] S28-11｜irgen_get_builtin_func 越界返回 NULL，调用方不检查
- 位置：`IrGen.c:251-256`
- 类别：静默失败 + 未检查前置条件
- 触发条件：builtin 名字不在表内。
- 后果：7 个 `irgen_call_builtin_*` 直接把可能为 NULL 的名字传给 `KrtIrCall`。
- 最小修复：越界时报错或返回静态错误节点。

### [中] S28-12｜irgen_array_expression_element_type 的 TOKEN_EOF 契约未文档化
- 位置：`:7` 个调用点（`:126/499/1326/1824/2346/3342` 等）
- 类别：契约破坏
- 触发条件：`:126`（跳过检查）与 `:499`（布尔判断）对同一 `TOKEN_EOF` 语义解释不同。
- 后果：正是 S28-4 的根因。
- 最小修复：定义枚举（如 `ELEMENT_TYPE_UNKNOWN`）替代 `TOKEN_EOF`，并在头文件写明契约。

### [疑似] S28-13｜`function int32 main(){...}` 不报 unexpected token
- 位置：解析器入口（`ParserStatement.c` 顶层声明函数）
- 类别：静默误解析（疑似，需验证）
- 触发条件：`function` 关键字 + 返回类型的形态不报「unexpected token」，静默误解析，函数体变顶层块 → 后续报 `Top-level variable must be static`。
- 后果：非法语法被误解析，诊断与实际错误不符。
- 最小修复：需先确认是语法定义有意支持还是分支遗漏，再决定报错或支持。
- 备注：疑似；该形态即 G2 所述非法语法，S28-1 的实证即建立在此形态上。

### [致命] S29-1｜try/catch/finally/throw 完全无语义，catch 在正常路径也执行
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2719-2736`（`irgen_stmt_try_statement`）、`:2711-2717`（`irgen_stmt_throw_statement`）、`:478-480`（`irgen_throw_exception`）
- 类别：静默失败 + 阶段不对齐 + 契约破坏
- 触发条件：任何含 `try`/`catch`/`finally`/`throw` 的程序。
- 实证：`Test/SyntaxAudit/cases/C70_trycatch.krt`（`try{P("t")} throw new Exception("boom") catch{P("c")}`）→ 实际输出 `tc`；`try{P("a")} finally{P("f")} P("b")` → 实际输出 `afb`。`irgen_stmt_try_statement` 只顺序调用 `krt_ir_generate_block` 生成 try、catch、finally 三个块，既无异常表/landing pad，也无条件分支；`throw` 仅调用 `IRGEN_FUNC_THROW_EXCEPTION` builtin，不改变控制流。
- 后果：异常处理彻底失效。catch 块在正常路径也执行（`tc`），finally 在 throw 后不保证执行，`throw` 后代码继续跑；资源清理与错误传播语义全部错误。
- 最小修复：实现异常处理 IR（异常表 + landing pad + 从 throw 到匹配 catch 的条件分支），或在语义分析阶段直接拒绝 `try/catch/throw/finally` 并报错；二者必居其一，不能保留「静默顺序展开」。

### [致命] S29-2｜switch 分派完全失效，仅 case 1/2 偶然正确
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2561-2629`（`irgen_stmt_switch_statement`），缺陷核心 `:2596-2604`
- 类别：未检查前置条件（IR 块多终止符）+ 死代码
- 触发条件：`switch` 含 ≥2 个 case，或含 `default`。
- 实证：IR entry 块中 `day = copy 2`；`t68 = eq day 1`；`t69 = branch t68`；`t71 = eq day 2`、`t72 = branch t71`、`t73 = jump` 均为死代码。运行实证：`day=3/5/9` 全部输出 `T`（case 2 的 body），而非 `F`/`D`/`D`；只有 `day=1`→`M`、`day=2`→`T` 正确。
- 根因：比较/分支循环（`:2596-2604`）在同一当前块连续追加多条 `KrtIrBranch`（`Ir.c:867` `KrtIrBranch` 直接 append，无「块已终止则另起新块」保护），后续分支全部不可达；`case_blocks[i]` 的 false 目标被硬编码为 `case_blocks[i+1]`，只有 i=0 时恰好等于正确链。
- 后果：所有 ≥3 分支的 switch 语义错误；default 永不执行；case 体内出现重复 `jump`。
- 最小修复：每个比较分支生成到独立前缀块（比较块），或用单条比较链 + 显式「比较块→下一比较块」的 fall-through 跳转；`KrtIrBranch` 在 `current_block` 已有终止符时应报错或自动切新块。

### [高] S29-3｜测试套件把 S29-1 的错误行为固化为期望值
- 位置：`Test/SyntaxAudit/cases/C70_trycatch.exp:1`（内容为 `"tc"`）
- 类别：测试与断言不足（测试掩盖实现缺陷）
- 触发条件：运行 `Test/SyntaxAudit/test_language_regressions.py`。
- 后果：`catch` 在正常路径执行被断言为正确，缺陷被永久掩盖，后续修复会被测试判为回归。
- 最小修复：在 S29-1 修复后把 `.exp` 改为 `"t"`；修复前应删除该用例或标注为已知缺陷。

### [疑似] S29-4｜using 语句的资源释放顺序/异常安全
- 位置：`IrGen.c:2705-2709`（`irgen_stmt_using_statement`）
- 类别：资源生命周期（疑似，需验证）
- 触发条件：`irgen_dispose`（`:470-472`）在 body 抛异常时是否仍执行；当前实现是顺序生成 body 后调用 dispose，若 S29-1 未修则无意义。
- 后果：异常路径下 dispose 可能不执行，资源泄漏。
- 最小修复：依赖 S29-1 的异常处理实现；需确认 `IRGEN_FUNC_DISPOSE` 对非 disposable 值的处理。
- 备注：疑似；需验证。
