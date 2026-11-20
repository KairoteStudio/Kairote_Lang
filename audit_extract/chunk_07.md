# Chunk 07 抽取

## 覆盖范围
- 第四十批（IrOptimizer.c，CSE/DCE/常量与 COPY 传播；原文标题在 4025 行之前，本范围自 `:4025` 的「## 高」起）：`:4025-4085`（含 S40-2~S40-7；S40-1 在范围外，未抽取）
- 第四十一批（IrOptimizer.c :500-718，强度削减/控制流/内联调度/统计）：`:4091-4161`
- 第四十二批（IrPureCse.inc :1-140，纯函数分析与 CSE 前置）：`:4167-4215`
- 第四十三批（Generics.c 295 行全量）：`:4261-4339`
- 第四十四批（TypeExpansion.inc + PointerAnalysis.inc + Generics.h + SemanticAnalyzer.c 赋值/调用路径）：`:4381-4497`
- 第四十五批（StackCalculator.c + OutputCache.c + LeakDetector.c + Logger.c + KrtString.c + KrtTime.c）：`:4541-4786`
- 第四十六批（Compiler.c + ArkLinkIntegration.c + ConsoleUtils.c）：`:4818-4969`
- 第四十七批（Main.c 全量 1406 行，范围在 `:5022` 截断）：`:4993-5022`

剔除说明：已删除各批「已核查，未发现」段与「疑似但不确定（需验证）」段；另剔除 4 条本身标注「已核查，未发现（正确）」的编号条目（S46-11、S46-15、S46-17、S47-4）。本块无受 G2（S28-1 作废）、G3（S38-1~S38-15）影响的条目；G1 已应用于 S45-5、S45-11、S45-20、S46-3、S46-4、S46-13。

## 问题条目

### [高] S40-2｜CSE 的 `expr_capacity = 256` 固定，超出静默丢弃
- 位置：`IrOptimizer.c:383`（`int expr_capacity = 256`）、`:421`（`if (!found && expr_count < expr_capacity)`）
- 类别：静默失败
- 触发条件：函数内纯表达式数 > 256。
- 后果：超出部分不参与 CSE，优化不完整且无诊断。
- 最小修复：动态扩容。

### [高] S40-3｜CSE 把匹配 inst 改成 COPY 时只改 `type`/`index`，不改 `value_type`
- 位置：`IrOptimizer.c:401-403`
- 类别：阶段不对齐
- 触发条件：CSE 命中时。
- 后果：`inst->operands[0].value_type` 保留原操作数类型，但 `type` 已改为 `KRT_IR_VALUE_TEMP` → 后续阶段按 `value_type` 解释时类型不符。
- 最小修复：同步设置 `value_type`。

### [高] S40-4｜`ir_optimize_copy_propagation` 假设 `operands[0].type == KRT_IR_VALUE_TEMP`，否则读 union 错误字段
- 位置：`IrOptimizer.c:445`（`int copy_from = inst->operands[0].data.index;`）
- 类别：未检查前置条件
- 触发条件：`KRT_IR_COPY` 指令的 `operands[0].type` 是 `IMM`/`VAR`/`INTEGER`（如 `KrtIrSsaLowerPhis:1245` 生成的 COPY，其 operands[0] 是 phi 值，可能是 VAR/IMM）。
- 后果：`data.index` 读取 union 的 `index` 字段，而 `IMM` 的 union 是 `double imm` → 把 double 位模式当 int → 垃圾 `copy_from` → 后续把垃圾索引写入其他指令 → **IR 破坏**。
- 最小修复：`if (inst->operands[0].type != KRT_IR_VALUE_TEMP) { inst = inst->next; continue; }`

### [高] S40-5｜`ir_optimize_copy_propagation` 只遍历块内后续指令，不跨块 → 传播不完整
- 位置：`IrOptimizer.c:455`（`KrtIRInst* use = inst->next;`）
- 类别：静默失败 / 语义分叉
- 触发条件：COPY 的结果在后续基本块使用。
- 后果：跨块使用不被传播，与常量传播（同样只块内）一致但优化不完整。
- 最小修复：改为跨块数据流分析，或文档化"仅块内"契约。

### [中] S40-6｜DCE 删除 inst 后 `block->insts[]` 尾部未清零 → 悬垂指针
- 位置：`IrOptimizer.c:347-348`（`block->insts[live_count++] = inst;` 后 `block->inst_count = live_count;`）
- 类别：资源与生命周期
- 触发条件：DCE 删除 inst 后。
- 后果：`insts[live_count..old_count-1]` 仍指向已 `KRT_FREE` 的 inst；若有代码按 `inst_capacity` 或旧 `inst_count` 遍历（如 `KroRegisterPacking.inc:267` 读 `insts[index]`），会读到悬垂指针。
- 最小修复：`for (int i = live_count; i < old_count; i++) block->insts[i] = NULL;`

### [中] S40-7｜DCE 依赖 `is_side_effect_free` 的 `default: return true` → 有副作用指令可能被删
- 位置：`IrOptimizer.c:318`（`is_side_effect_free(inst->opcode)`）、`:99`（`default: return true`）
- 类别：静默失败（与 S39-9 同源）
- 触发条件：`result.type == VOID` 且 opcode 不在 `is_side_effect_free` 的"有副作用"列表中。
- 后果：若有未列出的有副作用 opcode，其 VOID 结果指令被删除 → 副作用丢失。
- 最小修复：`default: return false;`

### [致命] S41-1｜强度削减把 `x*0`/`x^0` 改成 IMM 时只写 `data.imm` 不改 `type` → 后端读 union 错误字段
- 位置：`IrOptimizer.c:511-514`（MUL val==0）、`:534-537`（POW exp==0）
- 类别：未检查前置条件 / 阶段不对齐
- 触发条件：启用 `OPT_STRENGTH_REDUCTION` 且存在 `x * 0` 或 `x ^ 0`（`0` 为 `KRT_IR_VALUE_IMM`）。
- 后果：`inst->operands[0].data.imm = 0` 覆盖 union，但 `operands[0].type` 仍是原类型（`VAR`/`TEMP`）。后端按 `type == VAR` 读 `data.name`（即 double 0 的位模式当指针）→ **空指针解引用 / SIGSEGV**；或按 `TEMP` 读 `data.index` → 垃圾索引。
- 最小修复：`inst->operands[0].type = KRT_IR_VALUE_IMM; inst->operands[0].value_type = TOKEN_FLOAT;`（或对应 token 类型）。
- 备注：原文列为疑似——需确认 `OPT_STRENGTH_REDUCTION` 是否默认启用（若启用可立即触发）。

### [高] S41-2｜`ir_optimize_loop_invariant_code_motion` 是空操作 → 死代码
- 位置：`IrOptimizer.c:618-622`
- 类别：死代码 / 注释脱节
- 触发条件：任何调用。
- 后果：函数存在但 `(void)module; (void)stats; return false;`，LICM 从未实现；若 `OptimizationFlags` 有对应位，启用后静默无效果。
- 最小修复：实现或删除，并在头文件标注"未实现"。

### [高] S41-3｜`ir_optimize_escape_analysis` 是空操作 → 死代码
- 位置：`IrOptimizer.c:638-642`
- 类别：死代码
- 触发条件：任何调用。
- 后果：同 S41-2。
- 最小修复：同 S41-2（实现或删除并标注未实现）。

### [中] S41-4｜`ir_optimize_function_inlining` 重复调用 `eliminate_pure_expressions`
- 位置：`IrOptimizer.c:626`、`:628`
- 类别：语义分叉 / 死代码
- 触发条件：启用 `OPT_FUNCTION_INLINING`。
- 后果：`eliminate_pure_expressions` 被调用两次，第二次在 `inline_recursive_functions` 后，可能有意（内联后再次消除）但无注释说明 → 语义不清。
- 最小修复：加注释说明为何调用两次，或合并。

### [中] S41-5｜`ir_optimizer_print_stats` 用 `printf`（stdout）而非 stderr → 诊断流不一致
- 位置：`IrOptimizer.c:692-701`
- 类别：错误处理不一致
- 触发条件：调用该函数。
- 后果：统计信息污染 stdout（可能干扰 `-o` 输出或其他工具链）。
- 最小修复：改用 `fprintf(stderr, ...)`。

### [中] S41-6｜`get_time_seconds` 用 `clock()`（CPU 时间）而非 wall time
- 位置：`IrOptimizer.c:27-29`
- 类别：注释脱节 / 阶段不对齐
- 触发条件：多线程/并行编译时。
- 后果：`time_spent` 统计 CPU 时间，与用户预期的 wall time 不符；并行场景下数值无意义。
- 最小修复：改用 `clock_gettime(CLOCK_MONOTONIC)`。

### [中] S41-7｜`MUL val==2` 把 `operands[1] = operands[0]` 浅拷贝 → 两个操作数共享 `char* name`
- 位置：`IrOptimizer.c:520`
- 类别：资源与生命周期
- 触发条件：`x * 2` 且 `operands[0].type == KRT_IR_VALUE_VAR`。
- 后果：两个 operand 共享同一 `name` 指针；当前无释放逻辑，但一旦有 `KrtIRValue` 析构会双重释放。
- 最小修复：`KRT_STRDUP` 或文档化"operand 不拥有 name"。

### [高] S42-1｜`find_memory_free_functions` 超上限或遇匿名函数时返回 NULL → 整个 CSE 静默失效
- 位置：`IrPureCse.inc:96-98`（`if (!f->name || ++count > KRT_PURE_FUNCTION_LIMIT) return NULL;`）、`:104-106`（call 超限 return NULL）
- 类别：静默失败
- 触发条件：模块函数数 > 65536，或 CALL 指令数 > 1048576，或存在匿名函数（`f->name == NULL`）。
- 后果：`eliminate_pure_expressions` 拿不到函数表，整个纯函数 CSE 静默跳过，无任何诊断。
- 最小修复：匿名函数跳过而非整体失败；超限时打印诊断。

### [高] S42-2｜`cse_direct_callee` 不检查 `callee->params` 是否为 NULL → OOB 读
- 位置：`IrPureCse.inc:86`（`callee->params[i].is_array`）
- 类别：未检查前置条件
- 触发条件：`callee->param_count > 0` 但 `callee->params == NULL`（`KrtIrTypeFunction` :138-160 允许此情况，S35-2 已报告）。
- 后果：空指针解引用 → SIGSEGV。
- 最小修复：`if (!callee->params) return -1;`

### [中] S42-3｜`cse_scalar` 漏 `KRT_IR_VALUE_IMM_F` → 浮点不参与局部值编号
- 位置：`IrPureCse.inc:27-30`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：浮点立即数操作数。
- 后果：整数/布尔参与 CSE，浮点不参与 → 同层功能行为不一致（与 S39-8 同源）。
- 最小修复：若浮点可纯，加入 `IMM_F`。

### [中] S42-4｜`KRT_PURE_FUNCTION_LIMIT`/`KRT_PURE_CALL_LIMIT` 硬编码上限未文档化
- 位置：`IrPureCse.inc:4-6`
- 类别：契约破坏 / 配置漂移
- 触发条件：大模块。
- 后果：上限行为（整体失败）未在头文件文档化，调用方无法预知。
- 最小修复：在 `IrOptimizer.h` 文档化，或改为动态扩容。

### [致命] S43-1｜`generics_build_instantiated_name` 对 `type_args[i]` 无 NULL 检查 → 空指针崩溃
- 位置：`Generics.c:245`（`strlen(type_args[i])`）、`:257-259`（`KRT_STRCAT_S(result, total_size, type_args[i])`）
- 类别：未检查前置条件
- 触发条件：任一 `type_args[i] == NULL`（`generics_instantiate_type` 允许 NULL arg，:138 `if (type_args[i])`）。
- 后果：`strlen(NULL)` → SIGSEGV；或 `strcat(NULL)` → SIGSEGV。
- 最小修复：拼接前跳过 NULL arg，并同步修正 `total_size`。

### [致命] S43-2｜`generics_instantiate_type` 的 `total_size` 低估 → `KRT_STRCAT_S` 缓冲区溢出
- 位置：`Generics.c:137-146`（计算）vs `:162-168`（拼接）
- 类别：未检查前置条件
- 触发条件：`type_args[i] == NULL` 且 `i > 0`。
- 后果：计算时 NULL arg 不加逗号分隔符（:142 `if (i > 0) total_size += 1` 在 `if (type_args[i])` 内），但拼接时 `if (i > 0) KRT_STRCAT_S(",")` 无条件执行 → 实际写入超过 `total_size` → 堆溢出。
- 最小修复：拼接与计算用同一条件（分隔符逻辑放进 `if (type_args[i])` 内，或计算时无条件加）。

### [高] S43-3｜`generics_lookup_type` 持锁返回内部指针 → 竞态
- 位置：`Generics.c:84-96`（:94 `pthread_mutex_unlock` 后 `return current`）
- 类别：共享可变状态
- 触发条件：多线程并发访问 registry。
- 后果：返回的 `GenericType*` 在锁外可能被其他线程修改/释放（`generics_destroy_registry`）→ 悬垂/竞态。
- 最小修复：返回副本，或文档化"调用方不得在锁外使用"。

### [高] S43-4｜`generics_register_type` 重名时静默丢弃新 params/definition
- 位置：`Generics.c:49-59`
- 类别：静默失败
- 触发条件：同名泛型重复注册且 `existing->parameters` 已非空。
- 后果：新 `params` 被丢弃（且未释放 → 泄漏），`parameter_count` 不更新 → 实例化参数个数错误。
- 最小修复：重名时比较 `param_count`，不一致则报错；或释放旧 params 再替换。

### [高] S43-5｜`generics_validate_instantiation` 只比数量不验证 type_args 内容
- 位置：`Generics.c:106-110`（`(void)type_args;`）
- 类别：静默失败 / 契约破坏
- 触发条件：任意类型参数。
- 后果：非法类型参数（如不存在的类型）被接受，实例化产出无效符号。
- 最小修复：验证每个 `type_args[i]` 是否为已注册类型。

### [高] S43-6｜`generics_instantiate_type` 对已存在的实例化名直接返回 true，不验证兼容性
- 位置：`Generics.c:170-173`
- 类别：语义分叉
- 触发条件：同名实例化已存在但定义不同（如泛型定义被修改）。
- 后果：返回过期的特化符号。
- 最小修复：比较定义一致性。

### [高] S43-7｜`specialized_symbols` 创建后从未使用 → 死代码/资源浪费
- 位置：`Generics.c:175-179`
- 类别：死代码
- 触发条件：每次首次实例化。
- 后果：`symbol_table_create()` 分配的符号表从未被读写（需 grep 确认无其他使用者），纯浪费。
- 最小修复：删除，或实现其用途。

### [高] S43-8｜`generics_get_type_info` 返回内部指针的浅拷贝 → 契约破坏
- 位置：`Generics.c:280-292`
- 类别：资源与生命周期
- 触发条件：调用方修改/释放 `info->name`/`parameters`/`declaration`。
- 后果：破坏 registry 内部状态。
- 最小修复：深拷贝，或文档化"只读"。

### [中] S43-9｜`generics_instantiate_type` 要求 `arg_count > 0` → 无参泛型无法实例化
- 位置：`Generics.c:118`
- 类别：静默失败 / 语义分叉
- 触发条件：`parameter_count == 0` 的泛型。
- 后果：`generics_validate_instantiation` 允许 `arg_count == 0`，但 `instantiate` 直接拒绝 → 不一致。
- 最小修复：改为 `arg_count < 0`。

### [中] S43-10｜`generics_register_type` 不检查 `param_count > 0` 时 `params` 是否为 NULL
- 位置：`Generics.c:44`
- 类别：未检查前置条件
- 触发条件：`param_count > 0 && params == NULL`。
- 后果：`generics_free_parameters(NULL)` 安全，但 `params[i]` 访问会崩溃（若后续代码遍历）。
- 最小修复：`if (param_count > 0 && !params) return false;`

### [致命] S44-1｜未定义函数调用被语义分析静默接受，诊断与实际错误不符、无位置信息
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:356-368`（`method` 在 `local_scope`/`global_scope` 都找不到、fallback 也找不到 → 直接 `break`，无任何错误）；`Re.KrtC/src/Compiler/Frontend/Semantic/SemanticAnalyzer.c:75-78`（`if (!method_symbol) { KRT_FREE(mangled_name); return true; }` → 静默成功）
- 类别：静默失败 / 诊断缺失 / 阶段不对齐
- 触发条件：已实证 t1.krt：`class A { public function A() {} public function int32 F() { return Undefined(); } } int32 main() { return 0; }` → 语义分析阶段 0 错误。
- 后果：IrGen 发出调用伪符号 `_ZN9UndefinedEv`，直到链接期才报 `Undefined symbol: _ZN9UndefinedEv`，无源码位置、名字被 mangling，用户无法知道是 `A.F` 里的 `Undefined()`；对照 t2.krt（类外同名调用）能正确报 `t2.krt(1,34): error: Undefined function: Undefined`，两条路径诊断能力不一致。
- 最小修复：`PointerAnalysis.inc:368` 后若 `method == NULL` 且非 object 调用，调用 `semantic_analyzer_add_error_at(analyzer, expr, "Undefined function: %s", expr->data.call.name)` 并返回 false；`SemanticAnalyzer.c:75-78` 改为报错而非 `return true`。

### [致命] S44-2｜赋值类型兼容性对非标量不检查 → 隐式截断/溢出无诊断
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:40-63`（`pointer_assignable` 对非指针/非字符串/非 IDENTIFIER 的标量对一律 `return true`）；`SemanticAnalyzer.c:109-117`（AST_ASSIGNMENT 只查符号存在）；`SemanticAnalyzer.c:1135-1169`（二元 ASSIGN 只查 lvalue/字符串拼接/只读成员，不查类型）
- 类别：静默失败 / 阶段不对齐
- 触发条件：已实证——`int32 main() { int32 x = 1; double d = 3.9; x = d; return x; }` 编译成功、运行 `exit=3`（double→int32 静默截断）；`x = 100000000000;` 编译成功、静默整数溢出。
- 后果：类型系统对赋值无约束，数值静默丢失/回绕，无任何诊断。
- 最小修复：在赋值路径增加标量转换规则（仅允许同类型或有文档记录的显式加宽），否则 `semantic_analyzer_add_error_at`。

### [高（疑似）] S44-3｜`source_type_equal` 对 NULL `type_name` 的两个不同类判等
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/PointerAnalysis.inc:3-28`
- 类别：阶段不对齐 / 未检查前置条件
- 触发条件：`token==TOKEN_IDENTIFIER` 且至少一方 `type_name==NULL` 时只比较 token。
- 后果：两个不同的 NULL-type_name 类类型被判等 → 错误接受赋值/传参。
- 最小修复：（原文未给出）
- 备注：疑似——需验证 `type_name` 是否可能为 NULL（检查 `KrtParseSourceType`/`KrtSourceTypeClone` 构造路径）；若不可能为 NULL 则降级为防御性缺陷。

### [高] S44-4｜`TypeExpansion.inc` 把 enum token 改成 INT32 但保留 enum `type_name` → 矛盾组合下传
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/TypeExpansion.inc:368-370`
- 类别：阶段不对齐
- 触发条件：枚举类型实例化/展开后，`token=TOKEN_INT32` 但 `type_name` 仍指向 enum 名。
- 后果：下游若先看 `type_name`（类查找/泛型实例化/IrGen 类型映射）会得到"INT32 token + 类名"的矛盾组合，行为取决于消费顺序。
- 最小修复：（原文未给出）
- 备注：需验证——grep IrGen/Semantic 中 `declared_type.type_name` 的消费顺序与优先级。

### [中] S44-5｜`TypeExpansion.inc` 用 arena 替换堆数组，原数组泄漏且所有权不一致
- 位置：`Re.KrtC/src/Compiler/Frontend/Semantic/TypeExpansion.inc:448-453`（`expansion_alloc`（type_arena）替换 `ast->data.block.statements`，原 `KRT_REALLOC` 数组无指针释放）；`context.count==0` 时 `expansion_alloc` 因 `size?size:1` 返回 1 字节非 NULL 指针
- 类别：资源与生命周期 / 契约破坏
- 触发条件：任何带泛型实例化的程序展开。
- 后果：`Parser.c:33` 的堆 statements 数组泄漏；后续若有人对 block statements 调 `KRT_FREE` 会 mixed-free。已确认 `ast_destroy_node`（`Ast.c:48`）对 arena 节点提前 return，故当前不会崩溃，但所有权契约未文档化。
- 最小修复：展开时释放原 statements 数组，或在契约中明确"展开后 block 由 arena 拥有、不得 free"。

### [中] S44-6｜实例计数只增不复位，失败尝试污染成功路径
- 位置：`TypeExpansion.inc:276`（`++context->instances > 256` 对失败尝试也计数且从不复位）；`:320-325` 递归 `expansion_instantiate` 在验证前就可能已 `expansion_append` 实例（:347），失败路径 `invalid:` 仍把这些实例留在 `context.nodes`，最终被 :448-451 复制进 AST
- 类别：静默失败 / 共享可变状态
- 触发条件：一次失败的类型展开后跟一次成功展开。
- 后果：失败残留实例进入 AST；`instances` 计数被失败尝试耗尽，后续合法展开被误判超限。
- 最小修复：失败路径回滚已 append 实例；`instances` 只在成功时计数。

### [中] S44-7｜`expansion_collect` 只处理顶层，嵌套类/限定名解析失败
- 位置：`TypeExpansion.inc:98-153`（只处理 AST_PROGRAM/AST_NAMESPACE_DECLARATION，不递归类体/不收集嵌套类）；`expansion_find`（:71-79）只匹配顶层收集名
- 类别：语义分叉 / 阶段不对齐
- 触发条件：嵌套类或跨作用域限定名泛型。
- 后果：与 SymbolTable 作用域链规则分叉，嵌套类解析失败。
- 最小修复：（原文未给出）
- 备注：需验证——用嵌套类/命名空间嵌套实证。

### [中] S44-8｜表达式/成员链递归无深度限制
- 位置：`TypeExpansion.inc:383`（`expansion_expression_name`）；`PointerAnalysis.inc:199/204/238/255/274`；`SemanticAnalyzer.c:1141-1142`
- 类别：未检查前置条件
- 触发条件：深嵌套表达式/成员链。
- 后果：栈溢出崩溃。
- 最小修复：加深度上限并诊断。

### [中] S44-9｜AST_CAST_EXPRESSION 不校验类型兼容性
- 位置：`PointerAnalysis.inc:394-414`（只校验非指针侧是整数/null/指针）
- 类别：静默失败
- 触发条件：`(int32)someObject` 之类。
- 后果：无诊断通过。
- 最小修复：（原文未给出）
- 备注：需验证——实证 `(int32)obj` 是否编译成功。

### [中] S44-10｜第二段 AST_CALL：非函数符号/找不到符号均无错误
- 位置：`PointerAnalysis.inc:353-380`
- 类别：诊断缺失
- 触发条件：`method` 找到但 `source_type.function==NULL`，或 `method` 找不到。
- 后果：静默通过。
- 最小修复：（原文未给出）

### [中] S44-11｜诊断与实际错误不符：未定义变量与数组整体 ref 合并
- 位置：`PointerAnalysis.inc:97-101`
- 类别：诊断缺失
- 触发条件：`!symbol`（未定义变量）与 `symbol->is_array` 走同一分支。
- 后果：报 "ref requires a scalar slot; pass an array element instead"，掩盖真实错误。
- 最小修复：（原文未给出）

### [中] S44-12｜`global::` 前缀原地剥除，名字永久改变且不可恢复
- 位置：`SemanticAnalyzer.c:16-19`（`call_expr->data.call.name = call_expr->data.call.name + 8;`）
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：`global::X()` 调用。
- 后果：原串泄漏；AST 名字被永久改变，二次分析/诊断打印显示错误名字。
- 最小修复：用长度参数或拷贝，不改原指针。

### [中（疑似）] S44-13｜字符串下标特例与指针分支覆盖关系
- 位置：`PointerAnalysis.inc:432-441`
- 类别：阶段不对齐
- 触发条件：字符串下标访问（原文未展开）。
- 后果：原文未展开，仅指出覆盖关系存疑。
- 最小修复：（原文未给出）
- 备注：疑似——需验证 `KrtSourceIsPointer` 对 TOKEN_TYPE_STRING 的判定（取决于 string 是否 pointer_depth>0），确认字符串下标是否被正确覆盖。

### [中（疑似）] S44-14｜`new` 表达式 `resolved_type` pointer_depth=0 → 成员/方法调用走错分支
- 位置：`PointerAnalysis.inc:305-307`
- 类别：阶段不对齐
- 触发条件：`new` 表达式得到的 `resolved_type` pointer_depth=0。
- 后果：后续成员/方法调用走错分支。
- 最小修复：（原文未给出）
- 备注：疑似——需验证实证 `new Box(2)` 后 `b.Get()` 是否走错分支。

### [高] S45-1｜`StackCalculator.c` 整套 244 行零调用者 = 死代码
- 位置：`Re.KrtC/src/Core/Utils/StackCalculator.c:1-244`（全部函数）
- 类别：死代码 / 无人调用的实现
- 触发条件：任何构建。
- 后果：栈帧分析、栈使用预测、溢出检查、全局 analyzer 全部从不执行；`Core.h:15` 仍 include 该头文件。
- 最小修复：删除该文件及 `Error.h`/`StackCalculator.h` 中相关声明，或接入实际调用点。

### [高] S45-2｜`LeakDetector` 全套 API 零调用者，泄漏检测永久关闭
- 位置：`Re.KrtC/src/Core/Memory/LeakDetector.c:1-203`（全部函数）；`Re.KrtC/src/Core/Utils/KrtCommon.h:168`（`#define KRT_LEAK_DETECTION_ENABLED 0`）；`Re.KrtC/src/Core/Memory/LeakDetector.h:10`（重复定义 0）
- 类别：死代码 / 配置漂移
- 触发条件：任何构建。
- 后果：`KRT_MALLOC_LEAK`/`KRT_CALLOC_LEAK`/`KRT_REALLOC_LEAK`/`KRT_STRDUP_LEAK`/`KrtFree_LEAK` 宏在 `KrtCommon.h:206-210` 全部退化为普通 `KRT_MALLOC`/`KRT_FREE`；`KRT_LEAK_DETECTOR_INIT/CLEANUP/REPORT_LEAKS/GET_MEMORY_STATS`（`LeakDetector.h:38-51`）退化为 `((void)0)`/零值宏，泄漏检测功能整体不可用。
- 最小修复：删除死实现，或把开关改为构建期可配置并接入 `Main.c` 初始化/清理。

### [高] S45-3｜`KRT_LEAK_DETECTION_ENABLED` 在两处独立定义为 0 → 配置漂移
- 位置：`Re.KrtC/src/Core/Utils/KrtCommon.h:168` 与 `Re.KrtC/src/Core/Memory/LeakDetector.h:10`
- 类别：配置漂移 / 语义分叉
- 触发条件：任一处改为 1 而另一处未改。
- 后果：`KrtCommon.h` 的宏走 `#ifdef`（恒真分支，因为宏被定义为 0 但 `#ifdef` 仍成立）→ 实际 `KRT_MALLOC_LEAK` 展开为 `KrtMallocLeakDetected`，而 `LeakDetector.h` 的 `#if KRT_LEAK_DETECTION_ENABLED` 为假 → 声明/宏不一致，编译期或链接期矛盾。
- 最小修复：只在 `LeakDetector.h` 定义一次，`KrtCommon.h` include 它。

### [高] S45-4｜`KRT_STACK_ALIGNMENT`/`KRT_MIN_STACK_SIZE` 在三处独立定义
- 位置：`Re.KrtC/src/Core/Target/KairoteTarget.h:8`、`Re.KrtC/src/Core/Utils/StackCalculator.h:7-8`、`Re.KrtC/src/Core/Utils/Error.h:7-8`
- 类别：配置漂移 / 语义分叉
- 触发条件：任一处分值调整。
- 后果：栈布局计算与目标 ABI 常量可能不一致，且都是死代码路径（S45-1），掩盖了风险。
- 最小修复：统一到 `KairoteTarget.h`，其余 include。

### [高] S45-5｜`KrtStackAnalyzerBeginFunction` 扩容失败时 `frame_count` 已递增但 frame 未初始化
- 位置：`Re.KrtC/src/Core/Utils/StackCalculator.c:57-79`（:68 `frame_count++` 在 :74 `usages` 分配之前；:76-79 失败直接 `return NULL`，但 `frame_count` 已加且 frame 字段未初始化）
- 类别：未检查前置条件 / 资源与生命周期
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 失败返回 NULL，活路径且零诊断）。
- 后果：`frame_count` 与有效 frame 数不一致；`KrtStackAnalyzerDestroy` 会读未初始化 `usages` 并 `KRT_FREE` 垃圾指针 → 堆破坏。
- 最小修复：先分配 `usages` 成功后再 `frame_count++`。
- 备注：【修订 G1】原「`KRT_MALLOC` 失败即 exit(1)，实际不可达，属防御性缺陷」作废；该文件同时是零调用者死代码（S45-1），按后果定高。

### [中] S45-6｜`KrtStackFrameOptimizeLayout` 与 `KrtStackFrameGetTotalSize` 对齐逻辑重复实现
- 位置：`StackCalculator.c:133-143` 与 `:150-156`
- 类别：语义分叉
- 触发条件：任何调用（当前零调用）。
- 后果：同一"对齐到 KRT_STACK_ALIGNMENT 且下限 KRT_MIN_STACK_SIZE"规则两处实现，易漂移。
- 最小修复：`GetTotalSize` 调用 `OptimizeLayout` 或抽公共函数。

### [中] S45-7｜`KrtStackFrameGenerateReport` 是空操作
- 位置：`StackCalculator.c:161-165`
- 类别：死代码 / 注释与实现脱节
- 触发条件：任何调用（当前零调用）。
- 后果：报告功能未实现。
- 最小修复：实现或删除。

### [中] S45-8｜`KrtStackFrameCheckOverflow` 计算 `usage_ratio` 后丢弃
- 位置：`StackCalculator.c:178-179`（`double usage_ratio = ...; (void)usage_ratio;`）
- 类别：死代码
- 触发条件：任何调用（当前零调用）。
- 后果：利用率信息被丢弃。
- 最小修复：用于诊断或删除。

### [中] S45-9｜`KrtStackAnalyzerEndFunction` 用 `KrtError` 报错但返回 void，调用方无法感知
- 位置：`StackCalculator.c:88-94`
- 类别：错误处理不一致 / 静默失败
- 触发条件：`current_depth == 0` 时调用。
- 后果：只打印不拒绝，栈深度状态已错。
- 最小修复：返回错误码或断言。

### [中] S45-10｜`KrtStackFrameAddUsage` 对 `size==0` 报错但 `size` 来自外部
- 位置：`StackCalculator.c:96-101`
- 类别：错误处理不一致
- 触发条件：`size==0`。
- 后果：`KrtError` 只打印，frame 状态不变；调用方继续。
- 最小修复：返回错误码。

### [中] S45-11｜`KrtStackAnalyzerInit` 的 `KRT_MALLOC` 失败分支（原判死代码）
- 位置：`StackCalculator.c:12-15`、`:19-23`、`:76-79`、`:106-109`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：返回 NULL 且无诊断，调用方若解引用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码（与全局 `KRT_MALLOC` 恒成功一致）」结论作废；该文件为零调用者死代码（S45-1），严重度按后果维持中。

### [中] S45-12｜`KrtCalculateDynamicStackSize` 用 IR 文本行数估算栈大小，与真实帧布局无关
- 位置：`StackCalculator.c:184-209`
- 类别：契约破坏 / 静默失败
- 触发条件：任何调用（当前零调用）。
- 后果：估算与实际栈使用无对应关系，可能严重高估/低估。
- 最小修复：基于真实 IR 帧信息计算，或删除。

### [中] S45-13｜`KrtPredictStackUsage` 忽略 `function_signature` 参数
- 位置：`StackCalculator.c:211-212`（`__attribute__((unused))`）
- 类别：死代码 / 契约破坏
- 触发条件：任何调用（当前零调用）。
- 后果：签名信息未用，参数个数/局部变量数由调用方硬编码。
- 最小修复：从签名解析或使用真实数据。

### [中] S45-14｜`KrtOutputCacheInit` 先 `Clear` 再 `enabled=0`，`Clear` 不清 `enabled`
- 位置：`OutputCache.c:11-14`（`KrtOutputCacheClear` :24-35 不清 `enabled`）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：`enabled=1` 时调 `Init`。
- 后果：`Clear` 后 `enabled` 仍为 1，但 `Init` 紧接着置 0；若有人单独调 `Clear` 期望保留配置，行为不一致。
- 最小修复：明确 `Clear` 是否重置 `enabled`。

### [中] S45-15｜`KrtOutputCacheCleanup` 先 `Flush` 再置空，但 `Flush` 已置空 → 二次置空冗余
- 位置：`OutputCache.c:16-22` 与 `:168-193`
- 类别：语义分叉（轻微）
- 触发条件：任何调用。
- 后果：无功能影响，但两处都在维护链表状态。
- 最小修复：`Cleanup` 只调 `Flush` + `enabled=0`。

### [中] S45-16｜`KrtOutputCacheAddv`/`AddErrorv` 复制 `va_list` 后内部再复制一次
- 位置：`OutputCache.c:144-149`、`:161-166`、`:37-46`
- 类别：资源与生命周期（冗余）
- 触发条件：任何调用。
- 后果：双重 `va_copy`，无功能错误但冗余。
- 最小修复：去掉一层。

### [中] S45-17｜`KrtOutputCacheAddInternal` 在 `enabled` 时把格式化串当 `%s` 参数，格式串注入风险
- 位置：`Logger.c:44`（`KrtOutputCacheAdd("%s", buffer)`）与 `OutputCache.c:89`（`fputs(buffer, target)`）
- 类别：契约破坏
- 触发条件：任何带格式串的日志。
- 后果：`buffer` 内容被当作数据而非格式串（这是正确的）；但 `OutputCache.c:134-142` 的 `KrtOutputCacheAdd` 直接把调用方 `format` 当格式串，若调用方传入用户数据作为 format 则有注入风险。
- 最小修复：（原文未给出）
- 备注：需验证——grep `KrtOutputCacheAdd(` 的调用方是否传入非字面量。

### [中] S45-18｜`Logger.c` 与 `OutputCache.c` 各自实现一套 `format_and_output`/`output_cache_add_internal` 格式化逻辑
- 位置：`Logger.c:9-66` 与 `OutputCache.c:37-132`
- 类别：语义分叉
- 触发条件：任何日志输出。
- 后果：两套格式化+缓存逻辑并行，行为可能不一致（如 `Logger.c:42-51` 在 cache 启用时对 `stdout`/`stderr` 分流，而 `OutputCache.c` 内部也分流）。
- 最小修复：统一到一个实现。

### [中] S45-19｜`KrtPrintFormat` 在 cache 启用时把结果写到 `stdout`，忽略 `stream` 语义
- 位置：`Logger.c:77-102`（:96 `fputs(buffer, stdout)`）
- 类别：阶段不对齐
- 触发条件：cache 启用时调 `KrtPrintFormat`。
- 后果：`KrtPrintFormat` 无 `stream` 参数，恒写 stdout，与 `KrtFprintf` 行为分叉。
- 最小修复：统一输出目标。

### [中] S45-20｜`KrtPrintFormat` cache 启用分支 `KRT_MALLOC` 失败静默返回 -1
- 位置：`Logger.c:88-92`
- 类别：静默失败 / 未检查前置条件（OOM 时静默失败，零诊断）
- 触发条件：分配失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：无输出无诊断。
- 最小修复：降级到 `vfprintf`。
- 备注：【修订 G1】原「当前 `KRT_MALLOC` 恒成功，不可达」作废。

### [中] S45-21｜`KrtFprintf` 对 `stream==NULL` 且 cache 启用时走 `fputs(buffer, stdout)`
- 位置：`Logger.c:52-62`（:57-60 `else` 分支写 stdout）
- 类别：静默失败 / 契约破坏
- 触发条件：`stream==NULL`。
- 后果：本应报错或写 stdout，实际写 stdout 但 `result` 为 `fputs` 返回值，调用方可能误判。
- 最小修复：显式文档化 `stream==NULL` 语义。

### [中] S45-22｜`KrtPrintf` 与 `KrtFprintf` 在 cache 启用时行为不一致
- 位置：`Logger.c:129-140`（`KrtPrintf` 恒 `vfprintf(stdout)` + `fflush`，不查 cache）vs `:112-127`（`KrtFprintf` 查 cache）
- 类别：语义分叉 / 配置漂移
- 触发条件：cache 启用时。
- 后果：`KrtPrintf` 绕过缓存直接输出，`KrtFprintf` 走缓存 → 同一进程内输出顺序错乱。
- 最小修复：`KrtPrintf` 委托 `KrtFprintf(stdout, ...)`。

### [中] S45-23｜`KrtStrcpySafe` 截断警告无位置信息
- 位置：`KrtString.c:33-36`
- 类别：诊断缺失
- 触发条件：`src` 长度 ≥ `dest_size-1`。
- 后果：只报 "String truncation"，无文件/行/目标缓冲区大小。
- 最小修复：加 `__FILE__`/`__LINE__` 参数。

### [中] S45-24｜`KrtStrcatSafe` 在 `dest_len >= dest_size-1` 时静默返回，不警告
- 位置：`KrtString.c:50-52`
- 类别：静默失败 / 诊断缺失
- 触发条件：目标已满。
- 后果：静默丢弃 `src`，与 `KrtStrcpySafe` 的警告行为不一致（S45-23）。
- 最小修复：统一警告行为。

### [中] S45-25｜`KrtStrcatSafe` 截断警告同样无位置信息
- 位置：`KrtString.c:60-63`
- 类别：诊断缺失
- 触发条件：`src` 剩余未拷贝。
- 后果：同 S45-23。
- 最小修复：同 S45-23（加 `__FILE__`/`__LINE__`）。

### [中] S45-26｜`KrtSprintfSafe` 对 `buffer==NULL` 或 `buffer_size==0` 无保护
- 位置：`KrtString.c:68-82`（:72 `vsnprintf(buffer, buffer_size, ...)`）
- 类别：未检查前置条件
- 触发条件：`buffer==NULL`。
- 后果：`vsnprintf` 对 NULL 缓冲区是 UB/崩溃。
- 最小修复：加 `if (!buffer || buffer_size == 0) return -1;`。

### [中] S45-27｜`KrtSprintfSafe` 截断检查用 `result >= buffer_size`，负返回值静默
- 位置：`KrtString.c:76-79`
- 类别：契约破坏（轻微）
- 触发条件：输出长度恰等于 `buffer_size-1`；或 `vsnprintf` 返回负值（编码错误）。
- 后果：`result == buffer_size-1` 不触发警告（正确）、`result == buffer_size` 触发（正确）；但 `result` 是 `int`、`buffer_size` 是 `size_t`，比较时 `result` 转 `size_t`，负值则比较为假 → 静默。
- 最小修复：先判 `result < 0`。
- 备注：原文标注需验证。

### [中] S45-28｜`KrtStrlcpy` 在 `dest_size==0` 时返回 0，但 `dest` 可能为 NULL
- 位置：`KrtString.c:84-87`
- 类别：静默失败
- 触发条件：`dest_size==0`。
- 后果：无诊断。
- 最小修复：加 `KrtError`。

### [中] S45-29｜`KrtStrlcat` 在 `dest_len >= dest_size-1` 时返回 `dest_len + strlen(src)`，可能溢出 `size_t`
- 位置：`KrtString.c:111-113`
- 类别：未检查前置条件
- 触发条件：极大字符串。
- 后果：返回值溢出。
- 最小修复：饱和或诊断。

### [中] S45-30｜`KrtCalculateConcatenatedSize` 不检查 `parts==NULL` 且 `part_count<0`
- 位置：`KrtString.c:125-135`
- 类别：未检查前置条件
- 触发条件：`part_count<0` 时循环不执行（正确）；`parts==NULL` 且 `part_count>0` → 崩溃。
- 后果：空指针解引用。
- 最小修复：加 `if (!parts && part_count > 0) return strlen(base)+1;`。

### [中] S45-31｜`KrtCalculateConcatenatedSize` 累加 `strlen` 可能溢出 `size_t`
- 位置：`KrtString.c:126-132`
- 类别：未检查前置条件
- 触发条件：多个超长字符串。
- 后果：溢出后分配不足 → 堆溢出。
- 最小修复：溢出检查。

### [中] S45-32｜`KrtError` 在 `#ifndef _WIN32` 下定义，Windows 下无定义
- 位置：`KrtString.c:7-16`
- 类别：配置漂移 / 契约破坏
- 触发条件：Windows 构建。
- 后果：`KrtError` 未定义 → 链接错误；但 `KrtCommon.h` 可能另有定义。
- 最小修复：（原文未给出）
- 备注：需验证——Windows 下 `KrtError` 的定义位置。

### [中] S45-33｜`KrtGetTime` 用 `CLOCK_MONOTONIC`，与 `IrOptimizer.c` 的 `clock()` 不一致
- 位置：`Re.KrtC/src/Core/Utils/KrtTime.c:4-8` vs `Re.KrtC/src/Compiler/Optimizer/IrOptimizer.c:27-29`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：任何计时。
- 后果：两处计时基准不同（monotonic vs CPU time），性能统计不可比。
- 最小修复：统一用 `KrtGetTime`。

### [高] S46-1｜`KrtCompilerCompile` 中 177-189 行是纯死代码空循环
- 位置：`Re.KrtC/src/Compiler/Driver/Compiler.c:177-189`
- 类别：死代码
- 触发条件：任何编译。
- 后果：遍历 `main` 函数所有指令但什么都不做；`inst` 循环体为空，编译器可能警告但未报错。
- 最小修复：删除该块，或填入实际检查逻辑。

### [高] S46-2｜`KrtCompilerCreate` 对 `KRT_TARGET_WASM` 返回 NULL，但 `KrtCompilerCompile` 仍有 WASM 分支
- 位置：`Compiler.c:13-16`（Create 拒绝 WASM）与 `:220-223`（Compile 处理 WASM）
- 类别：死代码 / 契约破坏
- 触发条件：任何路径。
- 后果：`KRT_TARGET_WASM` 永远无法通过 `KrtCompilerCreate` 到达 Compile 的 WASM 分支 → `:220-223` 不可达；若有人绕过 Create 直接构造 `KrtCompiler`，行为未定义。
- 最小修复：删除 Compile 的 WASM 分支，或让 Create 接受 WASM。

### [高] S46-3｜`KrtCompilerCreate` 的 `KRT_MALLOC` 失败分支（原判死代码）
- 位置：`Compiler.c:19-21`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：返回 NULL 且无诊断，调用方解引用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码（`KRT_MALLOC` 恒成功）」结论作废。

### [高] S46-4｜`KrtCompilerCompile` 的 `ir_builder` 创建失败分支（原判死代码）
- 位置：`Compiler.c:159-161`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：分配失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：`ir_builder` 为 NULL 且无诊断，后续使用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码」结论作废。

### [高] S46-5｜`module_has_extended_integers` 只检查 `value_type`，不检查 `opcode` 隐含类型
- 位置：`Compiler.c:62-91`
- 类别：契约破坏 / 静默失败
- 触发条件：扩展整数通过 `KRT_IR_VALUE_INTEGER`（128 位）出现在 `data` 而非 `value_type` 字段。
- 后果：漏检 → 后端可能收到不支持的类型。
- 最小修复：（原文未给出）
- 备注：需验证——`KrtIRInst` 结构与 `value_type` 语义。

### [高] S46-6｜`module_fits_kro_limits` 的 `names` 数组固定 `KRT_KRO_MAX_LOCAL_VARS * 2`，但 `locals` 计数可能超过
- 位置：`Compiler.c:114`（`const char* names[KRT_KRO_MAX_LOCAL_VARS * 2] = {0};`）与 `:117-142`
- 类别：未检查前置条件
- 触发条件：局部变量数超过 `KRT_KRO_MAX_LOCAL_VARS * 2`。
- 后果：`names` 数组越界写；:142 才检查 `locals > KRT_KRO_MAX_LOCAL_VARS`，而 `names` 容量是 `MAX*2`、阈值是 `MAX` → 若写入按 `locals` 索引且 `locals` 可达 `MAX*2-1`，则检查前已越界。
- 最小修复：把 `names` 容量与检查阈值对齐，或在写入前检查。
- 备注：需验证——读 :118-141 完整写入逻辑。

### [高] S46-7｜`module_fits_kro_limits` 的 `goto storage_limit` 标签在循环内，跨迭代跳转
- 位置：`Compiler.c:142-147`（标签 `storage_limit:` 在 `for` 循环体内）
- 类别：可读性/维护风险（非缺陷）
- 触发条件：任何超限。
- 后果：标签在循环内，若后续有人在标签后加代码易出错；当前正确。
- 最小修复：抽成函数返回 false。

### [高] S46-8｜`KrtCompilerCompile` 对 `KRT_TARGET_VM_BYTECODE` 先 `fclose` 再序列化，序列化失败无检查
- 位置：`Compiler.c:224-241`（:239 无返回值检查）
- 类别：静默失败 / 诊断缺失
- 触发条件：序列化失败（磁盘满/路径错误）。
- 后果：`KrtCompilerCompile` 返回 true，调用方以为成功。
- 最小修复：检查返回值并报错。

### [高] S46-9｜`KRT_TARGET_KRO_OBJ` 与 `KRT_TARGET_EXE_PLATFORM` 分支完全相同
- 位置：`Compiler.c:242-249`
- 类别：语义分叉 / 配置漂移
- 触发条件：两种目标。
- 后果：同一 `KrtKrtGenerate` 调用，但 `KrtCompilerCreate` 对两者用同一 `fopen` 模式（:25-29 只区分 VM/KRO vs 其他），`KRT_TARGET_EXE_PLATFORM` 走 `fopen(..., "w")`（文本模式）→ 生成二进制对象用文本模式，Windows 下会损坏。
- 最小修复：区分 `KRT_TARGET_EXE_PLATFORM` 的二进制模式，或合并两个枚举。

### [高] S46-10｜`KrtArkLinkAddObjectFile` 在 `arklink_session_add_input` 失败时已 `KRT_STRDUP` 但 `obj_count` 未递增 → 内存泄漏
- 位置：`Re.KrtC/src/Compiler/Driver/ArkLinkIntegration.c:118-127`
- 类别：资源与生命周期
- 触发条件：`arklink_session_add_input` 失败。
- 后果：`ctx->obj_files[ctx->obj_count]` 已分配但未计入 `obj_count`，`KrtArkLinkContextDestroy` 不会释放它 → 泄漏。
- 最小修复：失败时 `KRT_FREE(ctx->obj_files[ctx->obj_count])`。

### [高] S46-12｜`KrtArkLinkAddRuntimeObjects` 忽略 `KrtArkLinkAddObjectFile` 返回值
- 位置：`ArkLinkIntegration.c:141-148`
- 类别：静默失败 / 错误处理不一致
- 触发条件：runtime 对象添加失败。
- 后果：继续链接，可能生成缺运行时符号的可执行文件。
- 最小修复：检查返回值并报错。

### [高] S46-13｜`KrtArkLinkContextCreate` 的 `KRT_MALLOC` 失败分支（原判死代码）
- 位置：`ArkLinkIntegration.c:36-38`、`:45-48`、`:51-55`
- 类别：未检查前置条件 / 静默失败（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（G1：`KrtSafeMalloc` 返回 NULL，活路径）。
- 后果：返回 NULL 且无诊断，调用方解引用即崩溃。
- 最小修复：删除冗余分支，或改为显式可失败处理并加诊断。
- 备注：【修订 G1】原「死代码（`KRT_MALLOC` 恒成功）」结论作废。

### [高] S46-14｜`ark_link_logger_callback` 是空操作，ArkLink 所有诊断被丢弃
- 位置：`ArkLinkIntegration.c:24-28`
- 类别：静默失败 / 诊断缺失
- 触发条件：任何 ArkLink 日志（警告/错误）。
- 后果：ArkLink 内部警告全部不可见；只有 `KrtArkLinkLink` 在最终失败时取 `arklink_session_get_error`。
- 最小修复：转发到 `KrtError`/`KrtPrintf`。

### [高] S46-16｜`KrtArkLinkLoadProjectLibraries` 忽略 `ctx` 与 `config` 一致性
- 位置：`ArkLinkIntegration.c:197-212`（`config` 可为 NULL，但 `ctx->config` 已设）
- 类别：契约破坏
- 触发条件：`config==NULL`。
- 后果：`base_dir` 保持空，退化为当前目录；与 `ctx->config` 可能不一致。
- 最小修复：用 `ctx->config` 或报错。

### [高] S46-18｜`KrtArkLinkLinkObjects` 的 `KrtArkLinkSetOutput` 与 `KrtArkLinkAddObjectFile` 顺序依赖
- 位置：`ArkLinkIntegration.c:252-295`
- 类别：契约破坏（顺序依赖）
- 触发条件：任何链接。
- 后果：`arklink_session_set_output` 必须在 `add_input` 之后？需确认 ArkLink API 契约。
- 最小修复：（原文未给出）
- 备注：需验证——ArkLink `set_output` 与 `add_input` 的顺序要求。

### [高] S46-19｜`KrtConsoleSupportsColor` 在 Windows 下修改全局控制台 CP 与模式，有副作用
- 位置：`Re.KrtC/src/Compiler/Driver/ConsoleUtils.c:12-35`（:14 `SetConsoleOutputCP`、:27 `SetConsoleMode`）
- 类别：共享可变状态 / 契约破坏
- 触发条件：Windows 下调用。
- 后果：探测颜色支持时永久改变进程控制台状态，影响其他输出。
- 最小修复：文档化副作用或恢复原模式。

### [高] S46-20｜`KrtConsoleSupportsColor` 返回值语义在 Windows/non-Windows 间分叉
- 位置：`ConsoleUtils.c:12-35`（Windows 返回 0/1；non-Windows 返回 `isatty` 的 0/1）
- 类别：语义分叉（轻微）
- 触发条件：任何调用。
- 后果：`isatty` 可能返回非 0/1 值（标准只保证非零），Windows 分支只返回 0/1；调用方若用 `== 1` 判断可能不一致。
- 最小修复：non-Windows 也 `return isatty(...) ? 1 : 0;`。

### [高] S46-21｜`KrtColor` 在 `g_color_enabled` 时返回原串，否则返回空串，但调用方可能拼接后仍计算长度
- 位置：`ConsoleUtils.c:41-43`
- 类别：契约破坏（轻微）
- 触发条件：颜色禁用时。
- 后果：空串仍参与 `printf`，无功能错误。
- 最小修复：无需修复。

### [高] S46-22｜`g_color_enabled` 是进程级全局，无锁
- 位置：`ConsoleUtils.c:10`、`:37-39`
- 类别：共享可变状态
- 触发条件：多线程编译（`ParallelCompiler.c` 存在）。
- 后果：颜色开关竞态。
- 最小修复：文档化单线程假设或加锁。

### [致命（原文自降级为疑似）] S47-1｜`link_kro_executable` 在 `import_kro_files` 分配失败时仍继续
- 位置：`Re.KrtC/src/Main.c:259-313`（:260-312 循环中 `imports_ok=0` 后 `break`，:315 `total_obj_count = 1 + import_kro_count` 仍按已完成的 `import_kro_count` 计算）；:316-328（`obj_files` 分配后只填 `import_kro_count` 个）；:330-341、:355；调用方 `compile_single_file:476`
- 类别：未检查前置条件 / 静默失败
- 触发条件：`import_kro_files` 分配失败（:260 `malloc` 返回 NULL → :310-312 `imports_ok=0`，但 `import_kro_count=0`）。
- 后果：`obj_files` 只含 `kro_file`，链接继续；`imports_ok=0` 在 :330 才检查，而 :330 `if (!imports_ok)` 先命中 → `result=0`、:355 返回 0，调用方 `compile_single_file:476` 判断失败 → 实际正确（原文自我推翻）。
- 最小修复：`imports_ok=0` 时立即跳过链接。
- 备注：原结论被原文自身核对推翻，**降级为疑似**；需验证 `import_kro_files` 分配失败路径的最终返回值。

### [致命] S47-2｜`compile_multiple_files` 在 `parsed_count != input_count` 时释放 `source` 但 `ast` 未释放
- 位置：`Re.KrtC/src/Main.c:720-732`（:721-725 只 `free(source)`、`lexer_destroy`、`parser_destroy`，但 `parsed_files[i].ast` 未 `ast_destroy`）
- 类别：资源与生命周期
- 触发条件：部分文件解析失败。
- 后果：AST 泄漏（每个失败文件的 AST 节点未释放）。
- 最小修复：补 `ast_destroy(parsed_files[i].ast)`。

### [致命] S47-3｜`compile_multiple_files` 在 `semantic_analyzer_create` 失败时同样泄漏 AST
- 位置：`Re.KrtC/src/Main.c:734-746`
- 类别：资源与生命周期
- 触发条件：`semantic_analyzer_create` 失败。
- 后果：同 S47-2（AST 泄漏）。
- 最小修复：补 `ast_destroy`。
