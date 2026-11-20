# Chunk 05 抽取

## 覆盖范围
- 第三十批（解析器作用域 · 数组分配 · ArkLink 生命周期）｜原文行 3002–3094
- 第三十一批（IrGen.c 函数声明与调用约定）｜原文行 3126–3166
- 重要更正：S28-1 作废｜原文行 3145–3147
- 第三十二批（IrGen.c：访问控制 / 继承 / 构造）｜原文行 3170–3265
- 第三十三批（IrGen.c 尾部：KrtIrGenerateFromAst / krt_ir_check_has_return / krt_ir_generate_print）｜原文行 3275–3327
- 第三十四批（Ir.c 全量 1768 行 + Ir.h 契约面）｜原文行 3331–3433
- 第三十五批（IrType.c 全量 561 行）｜原文行 3437–3504
- 全局修订应用：G1 已应用于 S34-12（分配失败不再 exit，改为活路径并重定严重度），并据此复核 S34-5 / S34-6 / S35-2 的分配失败判定；G2 已应用于 S28-1（作废）；G3 不涉及本块（第三十八批不在本范围）。

## 问题条目

### [致命] S30-1｜`function <类型> 名()` 被静默误路由，函数体按「顶层作用域」解析
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:1639-1648`（`case TOKEN_FUNCTION`）、`:451-458`（`parser_parse_type_inferred_declaration`）、`Re.KrtC/src/Compiler/Frontend/Parser/Parser.c:24-33`
- 类别：1 静默失败 + 2 诊断缺失 + 6 契约破坏
- 触发条件：源码写出 `function int32 main() { ... }`（`function` 后跟类型关键字）。
- 后果：`:1641-1644` 只把 `function 标识符 (` 判为关键字函数，此写法 `is_keyword_fn=false`；`:1648` 静默改走类型推断声明，`:455-458` 吃掉 `function` 后发现当前 token 是 `int32` 而非标识符，直接 `return NULL` 且无诊断；`Parser.c:29-31` 对 NULL 语句 `parser_advance()` 静默跳过一个 token，`int32` 被丢弃。随后 `main()` 被当顶层表达式丢弃、`{...}` 被当顶层块，块内声明落入全局作用域。实证：`function int32 main() { int32 y = 1; }` → 报 `Top-level variable 'y' must be declared 'static'`；`function int32 main() { int32[] a = new int32[3]; a[0]=5; }` → 报 `Top-level variable 'a'`；`function int32 main() { return 5; }` → 顶层 return 被丢弃、IR 为空（见 S30-2），编译器自身 SIGSEGV（exit=139）。对照 `int32 main()` 与 `function main()` 均正常。
- 最小修复：`:1641` 判定改为「`function` 后是类型关键字且再后是 `标识符 (`」时报错并 `parser_report_error` 指明不支持该写法，或直接按带返回类型的函数声明解析；`Parser.c:29-31` NULL 分支必须报告诊断而非静默跳过。
- 备注：无

### [致命] S30-2｜ArkLink：空模块时 `free` 后未置 NULL 即返回 OK → use-after-free + double-free → SIGSEGV
- 位置：`ArkLink/src/Core/Resolver.c:314-319`（`ark_resolver_resolve` 的 `total_sections == 0` 分支）；触发点 `ArkLink/src/Core/LinkerNative.c:87-95`；崩溃点 `ArkLink/src/Core/Resolver.c:582`（`ark_resolver_plan_destroy`）
- 类别：10 资源与生命周期 + 1 静默失败 + 8 未检查前置条件
- 触发条件：链接一个没有任何 section 的 Kro 对象（由 S30-1 的空 IR 产生，或任何损坏的 Kro）。
- 后果：`:317` `free(out_plan->backend_input)` 后未置 NULL；`:319` `return ARK_LINK_OK` 把「无 section」当成功上报（应为错误）；`LinkerNative.c:88` `if (plan.backend_input)` 判真（悬垂非 NULL），`:90-94` 向已释放内存写 `image_base` → use-after-free；后端读已释放内存，`ark_resolver_plan_destroy` 再次 `free(plan->backend_input)` → double-free → SIGSEGV。实证 gdb 回溯 `ark_resolver_plan_destroy ← arklink_session_link_native ← KrtArkLinkLinkObjects ← link_kro_executable ← compile_single_file`；输入 `function int32 main() { return 5; }` 时 exit=139，`a3.kro` 仅 64 字节（只有文件头）。
- 最小修复：`:317` 改为 `free(...); out_plan->backend_input = NULL;`，`:319` 改为 `return ARK_LINK_ERR_INVALID_ARGUMENT;`；`ark_resolver_plan_destroy` 内所有 `free` 分支补 `= NULL`（`:586-590` 已置，`:603-608` 的 `backend_input` 分支在 `:637` 才置，重复调用仍会 double-free）。
- 备注：无

### [高] S30-3｜C 风格数组长度为非常量表达式时，静默按 10 个元素分配
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2761-2766`
- 类别：1 静默失败 + 8 未检查前置条件
- 触发条件：`int32 a[n];`（`n` 为变量或任意非恒定表达式）。
- 后果：实证 `int32 n = 3; int32 a[n];` 编译运行均成功，IR 显示 `t10 = imm 56`（16 字节头 + 10×4），长度标记存 `10`，完全忽略 `n=3`；数组大小与源码语义不符，越界读写风险，且本应报错（动态长度在 C 风格数组中不合法）。
- 最小修复：`try_extract_constant` 失败时 `KRT_COMPILE_ERROR` 报出变量名与位置，不要回落到 `10`。
- 备注：无

### [高] S30-4｜数组长度 ≤ 0 时静默变成 10
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2763-2765`（`(size_value > 0) ? (int)size_value : 10`）
- 类别：1 静默失败
- 触发条件：`int32 a[0];`、`int32 a[-1];`。
- 后果：实证 `int32 a[0]; a[0]=7;` 编译并运行成功（实际分配 10 元素），无任何诊断；非法长度被静默接受并改变语义，`a[-1]` 这类明显错误被吞掉。
- 最小修复：`size_value <= 0` 时报错并停止。
- 备注：无

### [高] S30-5｜C 风格数组总字节数整数溢出未检查
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2768`（`int total_bytes = array_size * element_size;`）
- 类别：8 未检查前置条件
- 触发条件：`array_size` 较大（如 `int32 a[1000000000];`）→ `int` 溢出为负数或回绕。
- 后果：`irgen_allocate_memory` 收到错误的负/小尺寸，分配不足 → 堆越界写；或 `mmap` 长度异常。
- 最小修复：用 `int64`/`size_t` 计算并检查 `array_size > SIZE_MAX / element_size`，超限报错。
- 备注：无

### [高] S30-6｜`parser_parse_function_declaration` 全部失败路径静默返回 NULL，无诊断
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:30`、`:35`、`:40`、`:105`、`:113`、`:120`、`:125`
- 类别：2 诊断缺失 + 1 静默失败
- 触发条件：函数声明缺少标识符、缺少 `(`、缺少 `)`、缺少 `{`、缺少函数体等。
- 后果：所有这些语法错误都不产生任何消息，直接被 `Parser.c:29-31` 的 NULL 分支静默吞掉，用户看到「成功」或无关错误。
- 最小修复：每个 `return NULL` 前调用 `parser_report_error` 并带上 `line/col` 与具体原因。
- 备注：无

### [高] S30-7｜顶层语句解析失败时静默跳过一个 token
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/Parser.c:29-31`
- 类别：1 静默失败 + 2 诊断缺失
- 触发条件：任何 `parser_parse_statement` 返回 NULL 的情形（含 S30-1、S30-6）。
- 后果：错误被吞、token 被静默丢弃，后续解析基于错误状态继续，产生与真实原因无关的次生诊断（如 S30-1 的「Top-level variable」）。
- 最小修复：`!stmt` 分支必须 `parser_report_error` 并计入错误数，且不得静默 `advance`。
- 备注：无

### [中] S30-8｜静态全局重复声明时复用旧符号且不校验类型
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2806-2812`（`KrtIrModuleFindGlobal` 命中即复用）
- 类别：3 语义分叉 + 1 静默失败
- 触发条件：同一模块内两次声明同名 `static` 变量且类型不同。
- 后果：`global->type` 保持首次类型，第二处声明的类型与初始化被静默忽略，前后不一致。
- 最小修复：命中已存在 global 时校验类型一致，否则报错。
- 备注：无

### [中] S30-9｜`is_array_literal` 分支缺少 `KrtIrAlloc`
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2745-2758`（对比 `:2770` 的 `KrtIrAlloc(builder, ir_slot)`）
- 类别：7 共享可变状态（疑似）
- 触发条件：数组字面量路径未把变量登记进版本化变量表，后续同名变量的版本解析可能错位。
- 后果：同一作用域内先声明数组字面量、再用同名变量遮蔽时，`KrtIrLoad` 可能取到旧版本。
- 最小修复：确认后补 `KrtIrAlloc(builder, ir_slot);`。
- 备注：疑似，需验证（观察同名遮蔽下 `KrtIrLoad` 是否取到旧版本）。

### [致命] S31-1｜数组字面量步长语义分叉：表达式路径硬编码 8 字节，声明路径用真实元素大小
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:1841-1855`（`irgen_expr_array_literal`）对比 `:2745-2758`（`irgen_stmt_variable_declaration` 的 `is_array_literal` 分支）；底层硬编码在 `Re.KrtC/src/Compiler/Middle/Ir/Ir.c:1136-1139`（`KrtIrArrayStore` 恒 `element_size=8`）
- 类别：3 语义分叉 + 4 阶段不对齐 + 1 静默失败
- 触发条件：数组字面量出现在任何非变量声明上下文——函数实参、返回值、三元表达式、赋值右侧等。
- 后果：表达式路径 `:1848` `total_size = element_count * KRT_IMM_PTR_SIZE`（恒 8 字节/元素），`:1853` 调 `KrtIrArrayStore` → `Ir.c:1138` 写死 `element_size=8`；声明路径 `:2749` 用 `irgen_size_from_token(type)`、`:2757` 调 `KrtIrArrayStoreSized`；读取侧 `irgen_expr_array_access`（`:1857` 起）用 `irgen_array_element_size`（`:120`）按声明类型算偏移，与表达式路径 8 字节写入步长不一致。实证（语法均合法）：`int32 get1(int32[] a){ return a[1]; } int32 main(){ return get1([11,22,33]); }` → exit=0（应为 22）；`int32 main(){ int32[] a = [11,22,33]; return a[1]; }` → exit=22（声明路径正确）。同一特性两阶段两种内存布局，静默得到 0 或垃圾，无诊断。
- 最小修复：删除 `irgen_expr_array_literal` 的 8 字节假设，改为从 `expr` 元素类型推导 `element_size`（复用 `irgen_array_expression_element_type`，`:60`）并调 `KrtIrArrayStoreSized`；进一步合并为同一 `irgen_emit_array_literal(builder, elements, element_size)`，删除 `KrtIrArrayStore` 的 8 字节默认值。
- 备注：本条即 S28-1 的正确结论（见下）。

### [已作废（G2）] S28-1｜数组字面量元素步长语义分叉（原结论）
- 位置：原第二十八批（本范围外）；相关代码同 S31-1
- 类别：原判为语义分叉
- 触发条件：原实证 `cs=['A','B','C']; cs[1]` → exit=1
- 后果：原结论称声明上下文也出错。
- 最小修复：不适用。
- 备注：【已作废（G2）】原实证使用 `function int32 main()`，S30-1 已证明这是非法/被误解析的语法，结论无效。正确结论见 S31-1：声明上下文正确，仅非声明上下文（表达式路径）步长错误。

### [疑似（需验证）] S31-2｜`params[i].type` 可能越界读 `function_type->parameters`
- 位置：`IrGen.c:2833`、`IrGen.c:3038`（`stmt->function_type ? KrtSourceAbiStorage(stmt->function_type->parameters[i]) : ...`）
- 类别：8 未检查前置条件（疑似）
- 触发条件：`function_type->parameter_count` 与 `function_decl.parameter_count` 不等时。
- 后果：`function_type` 来自 `AstClone.inc:126` 克隆、缓存/复用符号或克隆后参数被增删时，`parameters[i]` 越界读。
- 最小修复：加 `i < stmt->function_type->parameter_count` 断言或回退到 `function_decl.parameters[i]`。
- 备注：疑似，需验证（目前只找到 `ParserStatement.c:147-148` 一处赋值使用同一 `param_count`）。

### [致命] S32-1｜访问控制（`private`/`protected`）完全未实现，外部代码可读写私有成员
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:156-164`（`while (... TOKEN_PUBLIC || TOKEN_PRIVATE || TOKEN_PROTECTED ...)` 只 `parser_advance()` 吞掉修饰符，从不写入 AST）；`Re.KrtC/src/Compiler/Frontend/Parser/Ast.h:357` 的 `access_modifier` 字段无任何生产者；`SemanticAnalyzer.c` 全文无 `E_ACCESS`
- 类别：1 静默失败 + 3 语义分叉 + 4 阶段不对齐
- 触发条件：任何 `private`/`protected` 成员被类外代码访问。
- 后果：实证 `class Box { private int32 secret = 7; } ... return box.secret;` 编译成功 exit=0（应拒绝）；`private int32 Read(){...}` 外部调用 exit=7（应拒绝）；对照 `build/selfhost/stage2/program` 同样代码被拒（E_ACCESS 只在 `SelfHost/Compiler/Frontend/Semantic/Types.krt:180,1521` 实现）。封装契约完全失效；`Test/SelfHost/test_access_control.py`、`test_symbol_index.py:134-136` 依赖的 `E_ACCESS` 在 C 编译器上永不触发。
- 最小修复：在 `ASTNode` 记录访问级别（或给 `function_decl`/字段节点加 `access`），由 `parser_parse_class_member` 写入，并在 `SemanticAnalyzer` 的字段访问/方法调用/静态引用处统一检查，缺失时报 `E_ACCESS` 并带位置。
- 备注：无

### [致命] S32-2｜`base` 表达式与 `:base(...)` 构造初始化器未实现
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（`base` 不作关键字处理，`AST_THIS` 仅在 `:321` 为 `this` 产生）；`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:30-130`（`parser_parse_function_declaration` 在 `)` 后直接要求 `{`，不识别 `:base(...)`）
- 类别：1 静默失败 + 4 阶段不对齐
- 触发条件：派生类方法体内使用 `base.成员`，或构造函数写 `:base(args)`。
- 后果：实证 `class Child:Parent{ public int32 Total(){ return base.value + base.Add(2); } }` → 种子报 `t35.krt(2,64): error: Expected right operand of binary operator`，stage2 同代码 exit=36（正确）；`class B:A{ public function B(int32 v):base(v){...} }` → 种子报 `Undefined identifier: v`，stage2 编译并正确调用基类构造（`b.x==5`）。继承语义在种子编译器上不可表达。
- 最小修复：新增 `TOKEN_BASE` 并在表达式解析中产生 `AST_BASE`；`parser_parse_function_declaration` 在 `)` 后接受 `TOKEN_COLON` + `base(...)`，存入 `function_decl` 的基调用列表，由 IrGen 在方法体前发出基构造调用。
- 备注：无

### [致命] S32-3｜构造/析构/属性 AST 节点类型从不产生，IrGen 对应实现整体为死代码，基类构造链缺失
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:2941-2944`（`AST_ACCESS_MODIFIER`）、`:2946-2990`（`AST_CONSTRUCTOR_DECLARATION`）、`:2991-3017`（`AST_DESTRUCTOR_DECLARATION`）、`:3072-3130` 与 `:3145-3203`（`AST_PROPERTY_DECLARATION`）；生产者缺失见 `Re.KrtC/src/Compiler/Frontend/Parser/` 全目录（四类节点零次创建）
- 类别：5 死代码 + 3 语义分叉 + 1 静默失败
- 触发条件：任何类声明。
- 后果：构造函数实际由 `IrGen.c:3036-3044` 的 `is_ctor`（与类同名）名称比较处理，该路径无任何基类构造调用代码；`has_base_call` 全项目只被读、从未写为 1。实证 `class A{...A(int32 v){this.x=v;}} class B:A{...B(int32 v){this.y=v+1;}} ... return b.x;` → 编译成功 exit=0（基类构造未执行，`b.x` 为 0，应为 5）。继承场景下基类字段保持未初始化且无诊断；约 120 行构造/析构/属性代码永不执行。
- 最小修复：删除 `IrGen.c:2941-3017`、`:3072-3130`、`:3136-3203` 中不可达分支，把构造函数统一到 `:3020-3068` 的 `is_ctor` 路径并补齐基类构造调用（对齐 `SelfHost/Compiler/Frontend/Parser/Parser.krt:993-1010` 的 `KrtParserBaseConstructor` 语义）。
- 备注：无

### [高] S32-4｜种子编译器无法编译本语言自带的示例与验收测试源码
- 位置：`Test/SelfHost/examples/language-project/src/Main.krt:7`（`public EmptyInput():base("empty input", 1) {}`）；`Test/SelfHost/test_inheritance.py:34,38,54,61,69-70`
- 类别：12 测试与断言不足 + 4 阶段不对齐
- 触发条件：用种子编译器编译依赖 `base`/构造初始化器的自带源码或跑对应验收测试。
- 后果：实证 `KrtC Test/SelfHost/examples/language-project/src/Main.krt -o m.bin` → 2 个语法错误、`BUILD FAILED`；`SELFHOST_COMPILER=build/Re.KrtC/KrtC python3 -m unittest ...NativeInheritanceTests.test_base_prefix_constructor_chain_and_upcasts` → `RuntimeError: Compiler produced no KRO`、FAILED。`Bootstrap.NATIVE_DRIVER_MODULES` 中 `test_inheritance` 在种子路径必然失败，发布的 example 项目不可构建。
- 最小修复：先实现 S32-2/S32-3，或在 Bootstrap 中把依赖这些特性的模块从种子验收集移除并显式标注，避免「绿灯假象」。
- 备注：无

### [高] S32-5｜auto-property 代码两份完整重复，且所在分支整体不可达
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:3072-3130`（第一份）与 `:3145-3203`（第二份，逐字重复）；不可达原因：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:1188-1265` 的 `parser_parse_block` 只返回 `AST_BLOCK` 或 `NULL`，故 `IrGen.c:3135` 的 `else`（`body->type != AST_BLOCK`）永不进入
- 类别：3 语义分叉 + 5 死代码
- 触发条件：任何类声明（用于触发 `body->type` 判断）。
- 后果：同一属性 getter/setter 生成逻辑存在两份，未来修改必然只改一份；`IrGen.c:3141-3142` 的空 `AST_FUNCTION_DECLARATION` 分支是永远为空的陷阱（一旦 `else` 变可达即静默丢函数）。
- 最小修复：删除 `IrGen.c:3135-3205` 整个 `else` 分支；属性生成只保留一份并改为由 `AST_PROPERTY_DECLARATION` 真正驱动（当前该节点从不产生，属性功能实际缺失）。
- 备注：无

### [高] S32-6｜C 种子与自托管编译器的语言特性集不一致（阶段不对齐）
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c:156-164`（访问修饰符）、`ParserExpression.c`（`base`）、`ParserStatement.c:30-130`（构造初始化器）；对照 `SelfHost/Compiler/Frontend/Parser/Parser.krt:993-1010`、`SelfHost/Compiler/Frontend/Semantic/Types.krt:180,1521`
- 类别：4 阶段不对齐 + 3 语义分叉 + 11 配置漂移
- 触发条件：同一份 `.krt` 源码分别用 `build/Re.KrtC/KrtC` 与 `build/selfhost/stage2/program` 编译。
- 后果：实证 `t34.krt`（private 字段外部读）种子 exit=0 / stage2 拒绝；`t35.krt`（`base.Add`）种子编译失败 / stage2 exit=36；`t33.krt`（`:base(v)`）种子编译失败 / stage2 exit=5。stage0→stage1 自举虽能跑（因 `SelfHost/*.krt` 未用这些语法），但「同一语言两种语义」，测试结果依赖所选编译器，无法据种子结果推断自托管行为。
- 最小修复：把访问控制、`base`、构造初始化器三项特性在 C 种子中补齐，或在 `Bootstrap`/CI 中显式记录「种子不支持特性清单」并对相关测试加 `skipUnless`，禁止无标注的语义分叉。
- 备注：无

### [中] S32-7｜解析错误前缀缓冲区过小，长路径下行号列号被截断丢失
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserBase.c:57`（`char prefix[64];`）、`:59`（`snprintf(prefix, sizeof(prefix), "%s(%d,%d): ", ...)`）
- 类别：2 诊断缺失
- 触发条件：源文件路径长度使 `prefix` 超过 63 字符。
- 后果：实证编译 `Test/SelfHost/examples/language-project/src/Main.krt`（路径 88 字符）输出 `.../language-project/src/languagerror: Expected right operand...`——路径被从中间截断、`(行,列)` 完全消失；诊断无位置信息，用户无法定位。
- 最小修复：`prefix` 改为动态长度（两次 `snprintf` 取长度后 `KRT_MALLOC`），或至少扩大到 512 并单独打印 `source_name` 与 `line:col`。
- 备注：无

### [中] S32-8｜arena 分配的内存在 `ParserAdvanced.c` 被当作堆指针 `KRT_FREE`，静默 no-op
- 位置：`Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c:41`、`:50`（`KRT_PARSER_ALLOC_FROM_ARENA`）、`:72`（`KrtParserStrdup`）分配；`:54`、`:85`、`:104`、`:120`、`:135`（`KRT_FREE(template_params[i])`）释放；`Re.KrtC/src/Core/Memory/Allocator.c:276-290`（`KrtSafeFree` 对未跟踪指针仅调 `KrtMemoryReportError`）、`:425-437`（该函数为空操作）
- 类别：10 资源与生命周期 + 1 静默失败
- 触发条件：泛型类声明解析失败（如 `class A<T> {` 后遇 EOF/语法错误）走错误返回路径。
- 后果：释放静默失败（既不崩溃也不报错），错误路径的清理形同虚设；若他日 `KrtMemoryReportError` 改为 `exit`，这些路径会立即崩溃。
- 最小修复：arena 内存不得 `KRT_FREE`——删除这些 `KRT_FREE(template_params[i])`（arena 生命周期由 parser 持有），或提供 `KrtArenaFree` 并统一使用。
- 备注：无

### [致命] S33-1｜`KrtIrGenerateFromAst` 后三个循环与尾部统计循环无 NULL 守卫 → SIGSEGV
- 位置：`Re.KrtC/src/Compiler/Middle/Codegen/IrGen.c:3563-3564`、`:3574-3575`、`:3594-3595`、`:3609-3610`
- 类别：8 未检查前置条件 + 1 静默失败 + 错误处理不一致
- 触发条件：解析器在错误恢复/部分失败时向 `statements` 写入 NULL 槽（`parser_parse_block` 的 `statements[i] = NULL` 路径），或 `statement_count > 0` 而 `statements == NULL`。
- 后果：四处循环直接 `ast->data.block.statements[i]` 后立即 `stmt->type`，无 `if (!stmt) continue;`，也无 `statements ? ... : NULL` 数组守卫。对照同函数 `:3549-3552`（pass1）、`:3560-3563`（pass2）与 `krt_ir_generate_block :3490-3492` 均有守卫——同一份代码四种写法。后果 SIGSEGV，编译器直接崩溃、无诊断，崩溃点取决于哪趟循环先撞到 NULL。
- 最小修复：在 `:3564`、`:3575`、`:3595`、`:3610` 四处循环体首行加 `if (!stmt) continue;`（尾部两处为 `if (!s) continue;`），并把 `statements` 的 NULL 守卫统一为循环前置条件，全函数只保留一种写法。
- 备注：无

### [高] S33-2｜入口函数名集合三处实现、两种取值（mangle 不一致）
- 位置：`IrGen.c:2048`、`:2845-2847`、`:3515-3517`、`:959`
- 类别：3 语义分叉 + 4 阶段不对齐
- 触发条件：源中出现名为 `_start` 或 `_KrtMainEntry` 的函数（或模块入口不是 `main`），且存在对该函数的取地址/引用。
- 后果：`:2048`（`irgen_address` 函数引用路径）只对 `"main"` 跳过 mangle；`:2845-2847`（定义路径）与 `:3515-3517`（声明路径）对 `"main" || "_start" || "_KrtMainEntry"` 跳过；`:959`（`krt_ir_ensure_main_entry`）只匹配 `"main"`。引用点生成 mangled 名、定义点注册裸名 → 链接期未解析符号；`krt_ir_ensure_main_entry` 在入口为 `_start` 时找不到 `main_function`，模块入口指针为 NULL。
- 最小修复：提取 `static bool krt_ir_is_entry_name(const char* n)`（含 main/_start/_KrtMainEntry 三值），`:2048`、`:2845`、`:3515`、`:959` 四处统一调用，`krt_ir_ensure_main_entry` 同步使用。
- 备注：需验证——KRT 源是否允许用户声明 `_start`/`_KrtMainEntry`（若不允许，`:2845`/`:3515` 的两个额外分支为不可达死代码）。

### [高] S33-3｜`krt_ir_check_has_return` 不识别 switch/try
- 位置：`IrGen.c:3450-3477`（switch 覆盖 `:3455-3476`；switch 与 try 语句均落入 `default: return 0;` `:3475-3476`）
- 类别：6 契约破坏 + 12 测试与断言不足
- 触发条件：函数体以 `switch`（含 default 且所有 case 都 return）或 `try/finally` 结尾。
- 后果：该判定器只识别 RETURN/BLOCK/POINT_BLOCK/IF/WHILE/DO/FOR/FOREACH，`AST_SWITCH_STATEMENT`、`AST_TRY_STATEMENT`、`AST_CASE_CLAUSE` 全落入 `default: return 0`，于是 `:2894` 与 `:3621` 追加一条不可达的 `KrtIrReturn(ZERO)`。当前因 S29-2（switch 分派失效）这条追加 return 反而成为唯一可达出口，把 switch 分派 bug 掩盖成「碰巧能跑」；一旦 switch 修复，同一条语句变成真实死代码。
- 最小修复：补 `AST_SWITCH_STATEMENT`（要求存在 default 且所有 case 子句都 return）与 `AST_TRY_STATEMENT`（try 体与 finally 体均 return）的递归分支。
- 备注：无

### [中] S33-4｜栈上伪造 AST 节点传入生成链，靠「不缓存指针」未文档化约定保命（疑似）
- 位置：`IrGen.c:3322-3344`（ref binding 伪造节点）、`:3349-3363`（`AST_ARRAY_COMPOUND_ASSIGNMENT` 同构）
- 类别：6 契约破坏 + 资源与生命周期（疑似）
- 触发条件：任何 `is_ref_binding` 的赋值/复合赋值，或 `a[i] += x` 形式。
- 后果：在栈上伪造 `ASTNode target/address/store`，把 `&target`、`&address` 塞进 `address_of.operand` / `pointer_assignment.pointer`，再调 `krt_ir_generate_expression(builder, &store)`，靠被调方「不缓存指针」这一未文档化约定保命。若下游任一路径把节点指针存入 builder（延迟表达式、`KrtIrVarTypePush` 保存节点指针、诊断回调复制节点），即悬垂栈指针 → UB；当前未爆是因为下游恰好即时取值。
- 最小修复：改为从 `stmt` 直接派生（让 `irgen_expr_address_of`/`irgen_expr_pointer_assignment` 接受 `ASTNode*` 并即时取值），或把伪造节点改为 `KRT_IRGEN_ALLOC` 堆分配并在生成后释放。
- 备注：疑似，需验证——通读 `krt_ir_generate_expression` → `irgen_expr_pointer_assignment` → `irgen_expr_address_of` 全链，确认无节点指针缓存/延迟解引用。

### [中] S33-5｜四趟发射顺序为隐式契约，无断言
- 位置：`IrGen.c:3547-3584`（pass1 静态变量 `:3549-3557`；pass2 嵌套 block/namespace `:3559-3571`；pass3 函数/类 `:3573-3583`；pass4 顶层语句 `:3593-3603`）
- 类别：6 契约破坏
- 触发条件：后续维护者调整循环顺序，或在 pass2 的嵌套 block 内引用 pass3 才声明的函数。
- 后果：顺序是隐式契约——pass3 先创建全部函数/类，pass4 才发射顶层语句，因此顶层语句可调用后声明的函数；但该顺序只靠代码排列维持，无注释、无 `KRT_ASSERT`（而 `KRT_ASSERT` 本身是 `((void)0)`）。一旦顺序被调整，函数尚未声明即被引用 → 符号解析失败或静默回退到未 mangle 名，且无断言保护。
- 最小修复：在 `KrtIrGenerateFromAst` 头写明四趟顺序契约并加阶段断言（如 `KRT_ASSERT(builder->module->functions)`，需换成有效断言）；或改为单趟 `irgen_collect_declarations` 先收集、再发射。
- 备注：无

### [致命] S34-1｜`ir_create_inst` 不检查块是否已有终结指令（S29-2 的 IR 层根因）
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/Ir.c:855-866`（`KrtIrJump`）、`:867-881`（`KrtIrBranch`）、`:882-894`（`KrtIrReturn`）、`:315-358`（`ir_create_inst`）
- 类别：6 契约破坏 + 8 未检查前置条件
- 触发条件：任何在同一 `current_block` 上连续调用两次终结 API。已实证：`IrGen.c:2596-2604` 的 switch 分派连续 append 多条 `KrtIrBranch`（S29-2）。
- 后果：`ir_create_inst` 只把指令追加到 `block->insts[]` 尾部，从不检查 `block->last_inst` 是否已是终结指令；三个终结 API 也无条件下发。对照 `KrtIrBlockAddSucc`/`AddPred` 会维护前驱后继，说明 IR 层知道「块唯一终结」不变量但未强制。后果：一个基本块出现多条互相矛盾的终结指令，块语义不确定；后端按首条还是末条解释取决于实现 → switch 分派失效（S29-2）。
- 最小修复：在 `ir_create_inst` 中对 `KRT_IR_JUMP/KRT_IR_BRANCH/KRT_IR_RETURN` 加断言或报错（`KRT_IR_ASSERT` 目前是空操作，需换成 `KRT_COMPILE_ERROR`），或让终结 API 拒绝在已有终结指令的块上再次下发。
- 备注：无

### [高] S34-2｜`KrtIrPrint` 静默丢弃浮点指令
- 位置：`Ir.c:1753`（守卫 `if (inst->opcode >= 0 && inst->opcode <= KRT_IR_NOP)`）、`Ir.c:1629-1676`（`ir_opcode_names[]` 共 47 项）、`Ir.h:262-273`（`KRT_IR_NOP`=37，`KRT_IR_FADD`=38 … `KRT_IR_FGE`=46）
- 类别：2 诊断缺失 + 注释/实现脱节
- 触发条件：`-target ir` 打印含浮点指令的 IR。
- 后果：守卫上界用 `KRT_IR_NOP`，把所有浮点指令（FADD/FSUB/FMUL/FDIV/FEQ/FNE/FLT/FLE/FGT/FGE）排除在打印之外，静默跳过、无提示。实证（`float64 a=1.5; float64 b=2.5; float64 c=a+b; if(c>3.0){return 1;} return 0;`）IR 文本中 `c = copy t3`（t3 从未定义，FADD 被丢弃）、`t7 = branch t6`（t6 从未定义，FGE 被丢弃）。`-target ir` 产出的 IR 不完整且引用未定义临时量，基于该文本的诊断、回归对比、教学验证全部失真。
- 最小修复：守卫改为 `inst->opcode >= KRT_IR_LOAD && inst->opcode < (int)(sizeof(ir_opcode_names)/sizeof(ir_opcode_names[0]))`，越界时打印 `unknown(%d)` 而非跳过。
- 备注：无

### [高] S34-3｜IR 指令合法性判据漏掉浮点码
- 位置：`Ir.h:583-585`（`KrtIrInstIsValid`）、`Ir.h:81-87`（`KRT_IR_ASSERT_VALID_INST`）
- 类别：6 契约破坏 + 5 死代码
- 触发条件：DEBUG 构建下（`KRT_IR_ASSERT_ENABLED` 默认为 1，`Ir.h:26-30`）任何使用 `KRT_IR_ASSERT_VALID_INST` 的校验路径。
- 后果：判据 `inst->opcode >= KRT_IR_LOAD && inst->opcode <= KRT_IR_NOP` 把 9 个浮点 opcode 判为非法；`KrtIrInstIsValid` 全项目无调用点（死代码），但一旦有代码使用该宏即误报。契约与枚举定义不一致，浮点 IR 在任何校验路径上都是「非法指令」。
- 最小修复：上界统一改为 `KRT_IR_FGE`；同时给 `KrtIrInstIsValid` 加调用点或删除。
- 备注：无

### [高] S34-4｜syscall 参数被静默截断
- 位置：`Ir.c:955-973`
- 类别：1 静默失败 + 8 未检查前置条件
- 触发条件：`IrGen.c:1706`、`:1764`、`:1799` 的 `KrtIrSyscall(builder, args[0], args + 1, argument_count - 1)`，当调用参数 ≥ 7 个时第 7 个起消失；或 `arg_count > 0 && args == NULL`。
- 后果：`for (int i = 0; i < arg_count && i < 6; i++)` 超过 6 个的实参被静默丢弃；且未检查 `args` 是否为 NULL（空指针解引用）。系统调用参数静默丢失，行为与源码不符，无诊断。
- 最小修复：入口加 `if (arg_count > 0 && !args) return result;`，并在 `arg_count > 6` 时 `KRT_COMPILE_ERROR("syscall takes at most 6 arguments")`。
- 备注：无

### [中] S34-5｜`KrtIrAlloc` 无 arena/extensions 守卫
- 位置：`Ir.c:560-580`（`:565` 直接 `(KrtIRVarTable*)builder->extensions` 后 `table->buckets[idx]`）
- 类别：8 未检查前置条件 + 1 静默失败
- 触发条件：builder 未正常初始化（`KrtIrBuilderCreate` 之外构造），或 arena 耗尽。
- 后果：只检查 `builder` 和 `name`；`builder->extensions` 为 NULL 时空指针解引用；`KrtIrArenaStrdup` 返回 NULL 时静默插入 `name == NULL` 的表项，`KrtIrVarTypePush :1279` 的 `strcmp(history->name, key)` 随后解引用 NULL。后果 SIGSEGV 或静默坏表。
- 最小修复：入口加 `if (!builder || !name || !builder->arena || !builder->extensions) return;`；`entry->name` 判空后再插入。
- 备注：【修订 G1】`KrtIrArenaStrdup` 失败返回 NULL 而非 exit(1)，故「arena 耗尽 → NULL 传播」是活路径，OOM 时该崩溃/坏表真实可达。

### [中] S34-6｜`KrtIrLoad` 把错误吞成常量
- 位置：`Ir.c:538-558`（`:543-547` 返回 `IMM 0`；`:556` 返回 `value_type = TOKEN_EOF` 的 VAR）
- 类别：1 静默失败 + 2 诊断缺失
- 触发条件：`builder`/`name` 为 NULL，或符号未在 var 表/global 中注册（上游检查被绕过，如新增语法路径、IR 层直调）。
- 后果：`builder`/`name` 为 NULL 时返回整数 0 常量；符号未注册时返回无类型 VAR，无任何诊断；`:555` `KrtIrArenaStrdup(builder->arena, name)` 未检查 arena。一旦上游检查被绕过，错误静默变成常量 0 → 计算结果错误且无法定位。已核查：语义分析层会拦截 KRT 源的未定义变量（前批实证），故当前不可从正常源码触发；但 `KrtIrLoad` 是公共 IR API，被 `KrtIrVar`（`:983-991`）等多处复用。
- 最小修复：`builder`/`name` 为空时 `KRT_COMPILE_ERROR`；符号未解析时同样报错而非返回 TOKEN_EOF。
- 备注：【修订 G1】`KrtIrArenaStrdup` 失败不再 exit，NULL 会静默进入表项/返回路径，OOM 场景从「不可达」变为活路径。

### [中] S34-7｜`KrtIrStore` 自动补登记未声明变量
- 位置：`Ir.c:510-537`（`:531-536` `if (!KrtIrVarTypeFind(...)) KrtIrVarTypePush(...)`）
- 类别：1 静默失败
- 触发条件：任何名字首次被 store。
- 后果：未声明变量不报错，静默注册其类型；`inst->result.value_type == TOKEN_EOF` 且 `value.value_type == TOKEN_EOF` 时产生无类型 COPY 指令。拼写错误的变量名在 IR 层被静默接受为新建变量。
- 最小修复：store 前要求 `KrtIrVarTypeFind` 命中（参数/全局/已声明局部），否则 `KRT_COMPILE_ERROR`。
- 备注：无

### [中] S34-8｜`KrtIrVarTypeTruncate` 非法 count 静默忽略
- 位置：`Ir.c:1364-1368`
- 类别：1 静默失败
- 触发条件：作用域回退时传入过期/错误的基线计数（异常路径、手工调用）。
- 后果：`count > builder->var_type_count` 或负数时直接 return，不报错、不断言；变量可见性静默泄漏到外层作用域，后续引用解析到错误类型/错误 IR 槽位。
- 最小修复：改为 `KRT_IR_ASSERT(count >= 0 && count <= builder->var_type_count)` 或 `KRT_COMPILE_ERROR`。
- 备注：无

### [中] S34-9｜IR 文本字符串常量无转义
- 位置：`Ir.c:1730`（`fprintf(out, "str%d = \"%s\"\n", i, module->string_constants[i]);`）
- 类别：2 诊断缺失
- 触发条件：字符串常量含 `"`、`\n`、`\`，或 `string_constants[i]` 为 NULL。
- 后果：`%s` 直接嵌入引号内无转义，破坏 IR 文本格式；NULL 时向 `%s` 传 NULL。IR 文本不可靠，无法作为诊断/回归基线。
- 最小修复：改为输出长度 + 十六进制/转义序列，并对 NULL 加守卫。
- 备注：无

### [中] S34-10｜global 表返回内部指针（潜在悬垂 + 重复符号）
- 位置：`Ir.c:1237-1258`（返回 `&builder->module->globals[idx]`，`:1249-1252` realloc 后固化下标）、`Ir.c:1370-1381`（`KrtIrModuleFindGlobal` 同样返回内部指针）
- 类别：7 共享可变状态 + 10 资源与生命周期
- 触发条件：持有返回的指针期间再次 `KrtIrModuleAddGlobal` 触发 realloc；或同名 global 重复入表。
- 后果：返回指向 `module->globals` 数组内部的指针，realloc 后悬垂；`AddGlobal` 不查重，同名 global 可重复入表。已核查：当前唯一调用点 `IrGen.c:2800-2803` 先用 `FindGlobal` 去重并立即使用，暂不可触发；后果为潜在悬垂 + 重复符号。
- 最小修复：`AddGlobal` 内部先查重（返回已有项）；对外返回 id/副本而非内部指针。
- 备注：无

### [中] S34-11｜类布局的宽度与失败值约定
- 位置：`Ir.c:1505-1512`（同名 class 静默 return）、`:1554`（`int size = KrtTokenIntegerBits(field->type) > 64 ? 16 : 8;`）、`:1589-1603`（`KrtIrLayoutGetType` 对数组字段返回 TOKEN_EOF）、`:1616-1627`（`KrtIrLayoutGetSize` 未知类返回 0）
- 类别：1 静默失败 + 3 语义分叉 + 4 阶段不对齐
- 触发条件：任何 class 字段访问（`IrGen.c:1163`、`:1383`、`:2296` 用 offset；`:1201` 用 size 分配）。
- 后果：① 重复 class 定义静默丢弃第二份；② 所有 ≤64 位字段一律 8 字节开槽（int8/int16/int32/bool 同宽），与真实 ABI 布局不同；③ `GetType` 的「数组字段」与「字段不存在」都返回 TOKEN_EOF，不可区分；④ `GetSize` 的「空类」与「未知类」都返回 0，不可区分。字段偏移由该布局唯一决定，若与 KroCodegen/VM 后端的字段访问宽度假设不一致即产生读写错位；错误约定不可区分使「字段不存在」被静默当成「数组字段」。
- 最小修复：重复定义报错；`GetType`/`GetSize` 增加 `bool* found` 出参或返回带错误码的结构；字段宽度按真实类型大小（或明确注释这是 slot 布局并让后端同步）。
- 备注：需验证——KroCodegen/VM 后端是否按「8 字节槽」读取字段（若是，则②为有意设计，应写进注释并统一；若不是，则为高危偏移错位）。

### [高] S34-12｜前驱/后继表扩容未判空即写入（G1：非死代码，OOM 时 NULL 解引用）
- 位置：`Ir.c:253-276`（`capacity *= 2` 后 `KRT_REALLOC` 未判空即写入）对照 `Ir.c:318-325`（判空返回 NULL）
- 类别：1 静默失败 + 8 未检查前置条件（【修订 G1】原判「与 KRT_* 失败即 exit 叠加时是死代码」不成立）
- 触发条件：IR 前驱/后继表扩容时 `KRT_REALLOC` 失败（OOM），或 `capacity` 接近 INT_MAX 时 `*= 2` 溢出为负/0。
- 后果：`KRT_REALLOC` 失败返回 NULL 而非 `exit(1)`（`Core/Memory/Allocator.c:159-211` 的 `KrtSafeMalloc`；`KrtCommon.h:77-81` 宏无守卫），未判空即写入 → NULL 解引用崩溃；`*= 2` 无溢出检查，容量接近 INT_MAX 时 realloc 0 字节。同一文件两种扩容写法本身也是维护陷阱。
- 最小修复：统一为 `ir_reserve` 辅助函数，判空 + 溢出检查（并改为 `KRT_COMPILE_ERROR` 上报，而非静默传播 NULL）。
- 备注：【修订 G1】严重度由「中」上调为「高」：分配失败是活路径，后果为崩溃。

### [高] S35-1｜整个类型子系统无调用者（死代码）
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrType.c:1-588`（全文件）
- 类别：5 死代码
- 触发条件：无条件——该模块永远不被执行。
- 后果：`KrtIrTypePoolInit/Destroy`、`KrtIrTypeVoid/Integer/Int{bits}/Uint{bits}/Float32/Float64/Bool/Char/String/Any/Unknown/Pointer/Array/Function/Struct/FromToken`、`KrtIrTypeSize/Align/ToString/Equal/Compatible/IsInteger/IsUnsigned/IsFloat/IsNumeric/IsPointer/Pointee/BinaryResult/CompareResult/Promote/Common/CanAssign/CanCast/SupportsOp/DefaultValue` 逐一对 `Re.KrtC/src` 全量 grep，除 `IrType.c` 自身外零引用；`IrType.h` 仅被 `IrSsa.h:5` include，而 IrSsa 只用 `KrtIRType*` 结构体指针（`IrSsa.h:10`、`:25`），从不调用任何构造/判定函数。561 行死代码进入构建（`build/Re.KrtC/Makefile:1043-1065` 有 `IrType.o` 目标），其中 `KrtIrTypeEqual`、`KrtIrTypeBinaryResult`、`KrtIrTypeSupportsOp`、`KrtIrTypeFunction` 带真实缺陷（见 S35-2~S35-8），一旦将来有人「顺手接上」就会立即踩中。
- 最小修复：删除 `IrType.c`/`IrType.h`（保留 `IrSsa.h` 需要的结构体定义则拆出到独立头），或补齐调用点并修复下列缺陷。
- 备注：无

### [中] S35-2｜`KrtIrTypeFunction` 参数表可空但计数非零
- 位置：`IrType.c:138-160`（`:150-156`：`if (param_count > 0 && params)` 才分配，否则 `params = NULL`，但 `type->data.function.param_count = param_count` 在 `:147` 已无条件写为传入值）
- 类别：8 未检查前置条件
- 触发条件：调用方传 `params = NULL` 或 `KRT_MALLOC` 分配失败。
- 后果：记录 `param_count > 0` 而 `params == NULL`；下游任何 `params[i]` 都是空指针偏移解引用（OOB 读）。
- 最小修复：分配失败/无 params 时把 `param_count` 一并归零，或返回 NULL。
- 备注：【修订 G1】`KRT_MALLOC` 失败返回 NULL 而非 `exit(1)`，故 OOM 时 `param_count > 0` + `params == NULL` 状态真实可达（本模块当前无调用者，属休眠缺陷）。

### [高] S35-3｜`KrtIrTypeEqual` 对复合类型只比 kind
- 位置：`IrType.c:292-312`（`:310` `return true;` 是 FUNCTION/STRUCT/CLASS/ANY/UNKNOWN/... 的共同出口）
- 类别：3 语义分叉 + 8 未检查前置条件
- 触发条件：任何调用 `KrtIrTypeEqual` 的路径（当前无调用者，属休眠缺陷）。
- 后果：① 函数类型只比 `kind`，完全不比较 `param_count`/`params`/`ret` → `int32()` 与 `float64(float64)` 判定相等；结构体/类只比 kind → 任意两个类相等。② POINTER 分支 `KrtIrTypeEqual(a->pointee, b->pointee)` 无环检测，若 pointee 链成环（自指或互相指向）则无限递归 → 栈溢出。类型等价判定错误 → 重载解析/类型检查错乱；循环 pointee 直接崩溃。
- 最小修复：FUNCTION 比较参数个数与各参数类型及返回类型；STRUCT/CLASS 比较名字与字段；递归加深度上限或已访问集合。
- 备注：无

### [中] S35-4｜`KrtIrTypeBinaryResult` 漏浮点比较码
- 位置：`IrType.c:374-406`（`:378` `if (op >= KRT_IR_LT && op <= KRT_IR_NE) return KrtIrTypeBool(pool);`）
- 类别：4 阶段不对齐 + 3 语义分叉
- 触发条件：对浮点操作数调用 `KrtIrTypeBinaryResult(..., KRT_IR_FEQ/FNE/FLE/FLT/FGT/FGE)`。
- 后果：布尔结果判定区间 `KRT_IR_LT..KRT_IR_NE` 不含浮点比较码（枚举中位于 `KRT_IR_NOP` 之后），这些 opcode 落到 `:382` 的数值分支，返回 FLOAT32/FLOAT64 而非 BOOL；比较表达式的类型被推成浮点，下游若按此类型分配寄存器/选择指令即出错。
- 最小修复：区间上界改为 `KRT_IR_FGE`，或显式列出 5 个浮点比较码。
- 备注：无

### [中] S35-5｜`KrtIrTypeSupportsOp` 未列举的 opcode 一律返回 true
- 位置：`IrType.c:523-558`（`:556-557` `default: return true;`）
- 类别：1 静默失败
- 触发条件：任何对该函数的调用（当前无调用者）。
- 后果：JUMP/BRANCH/CALL/RETURN/PHI/SYSCALL/LOADPTR/STOREPTR/ARRAY_STORE/INT_TO_STRING/浮点运算/浮点比较等全部落入 `default`，被判为「支持」；新增 opcode 时不会被发现「未实现支持性判定」，静默放行。
- 最小修复：`default: return false;`，并显式列出支持的码。
- 备注：无

### [中] S35-6｜两套并行的「可转换」规则
- 位置：`IrType.c:314-334`（`KrtIrTypeCompatible`）与 `:490-509`（`KrtIrTypeCanCast`），另 `:486-488` `KrtIrTypeCanAssign` 直接转发 `Compatible`
- 类别：3 语义分叉
- 触发条件：任何调用（当前无调用者）。
- 后果：`Compatible` 含 `dst == ANY` 与「任意指针互转」；`CanCast` 不含 ANY，但额外允许 pointer↔integer；三条 API 三套规则，且 `CanAssign == Compatible`。同一对类型在不同 API 下得到不同结论，语义分叉。
- 最小修复：合并为单一 `KrtIrTypeConvertible(src, dst, mode)`，mode 区分隐式/显式/赋值。
- 备注：无

### [低] S35-7｜`KrtIrTypeDefaultValue` 的 switch 各分支等价
- 位置：`IrType.c:560-588`
- 类别：5 死代码 + 1 静默失败
- 触发条件：任何对该函数的调用（当前无调用者）。
- 后果：FLOAT32/FLOAT64、BOOL、POINTER/STRING、default 四个分支全部 `value.data.imm = 0;`，且 `value.value_type` 从未赋值（保持 0 = `TOKEN_EOF`）。switch 结构是纯噪音；返回值无类型信息。
- 最小修复：按 kind 设置 `value.value_type`；若默认值恒为 0，删除 switch。
- 备注：无

### [中] S35-8｜`KrtIrTypeArray` 大小溢出
- 位置：`IrType.c:127-136`（`:131` `element->size * size`）
- 类别：8 未检查前置条件
- 触发条件：`element->size * size` 溢出，或 `size` 为负数。
- 后果：两个 int 相乘无溢出检查，`size` 为负也未拒绝；数组类型大小回绕，下游按错误大小分配/索引。
- 最小修复：`if (size < 0 || element->size > INT_MAX / size) return NULL;`
- 备注：无

### [疑似（需验证）] 未编号｜`KrtIrTypeIsPointer` 与 `KrtIrTypePointee` 对 STRING 语义矛盾
- 位置：`IrType.c:354-360`（`KrtIrTypeIsPointer`）、`:362-370`（`KrtIrTypePointee`）
- 类别：3 语义分叉（疑似）
- 触发条件：字符串解引用/取址路径依赖 `Pointee` 时。
- 后果：`IsPointer` 把 `KRT_IR_TYPE_STRING` 也算指针，而 `Pointee` 对 STRING 返回 NULL；若存在依赖路径，字符串取 pointee 静默得到 NULL。
- 最小修复：确认后统一 STRING 的指针语义（要么不算指针，要么 `Pointee` 返回字符类型）。
- 备注：疑似，需验证——字符串解引用/取址路径是否依赖 `Pointee`。另需向作者确认 `IrType.c` 是「预留未接线」还是「应删除」。
