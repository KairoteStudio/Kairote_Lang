# 第六十三批：NameMangling.c + EnumParser.inc + PointerTypes.inc

## 覆盖范围

- 全量读取（read 工具）：
  - `Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.c`（389 行）
  - `Re.KrtC/src/Compiler/Frontend/Parser/EnumParser.inc`（163 行）
  - `Re.KrtC/src/Compiler/Frontend/Parser/PointerTypes.inc`（152 行）
- 仅以 grep/定点读取核对以下符号，未通读其它源文件：
  - `KRT_MALLOC/KRT_REALLOC` → `KrtCommon.h:77,80`（失败返回 NULL，不 exit，G1 适用）。
  - `KrtParserAlloc/KrtParserStrdup/KrtParserCreateNode` → `ParserBase.h:9-35`（OOM 走 `KRT_COMPILE_ERROR` 致命错误，不返回 NULL）。
  - `IntegerWidths.def`（偶数宽度 2..128）、`KrtSourceType`（`Ast.h:131-141`，`pointee_nullable` 为 `uint64_t`）、`KrtUInt128 = unsigned __int128`（`Integer.h:8`）。
  - 调用方：`SemanticAnalyzer.c:1342-1347,1656-1661,2789`、`IrGen.c:1740,2044-2048,2834,569-575`、`CompilerPipeline.c:73,136`、`TypeExpansion.inc:368-370,403-413`。
  - 测试：`Test/SyntaxAudit/test_language_regressions.py:36-57`、`Test/SelfHost/test_symbol_index.py`、`Test/Pointers/*`（全库无任何 `mangl/demangl` 关键字测试）。
- 已应用前提 G1：所有 `KRT_MALLOC/KRT_REALLOC` 失败路径按“返回 NULL 且零诊断”的活路径审查；`KRT_STRCAT_S` 在本切片未出现，改核 `snprintf/memcpy/write_number` 的尺寸链。

## 问题条目

### [致命] 63-1｜name_demangle 长度字段无边界，memcpy 越界读堆
- 位置：`NameMangling.c:328-332`（长度累加）、`NameMangling.c:358`（`memcpy(name, ptr, (size_t)length)`）
- 类别：未检查前置条件（越界读）
- 触发条件：任何形如 `_ZN9999E` 的名字（`length` 远大于 `ptr` 之后剩余字符）。外部符号名、对象文件符号、用户传入的 mangled 名均可触发。
- 后果：`memcpy` 从只有数字节长的堆块读出 `length` 字节，造成堆越界读、崩溃或地址信息泄露；`length = length * 10 + (*ptr - '0')` 本身也可有符号整数溢出（UB）。
- 最小修复：解析期用 `size_t` 并做 `if (length == 0 || (size_t)length > strlen(ptr)) goto fail;`，通过后再 `memcpy`。
- 备注：`name_demangle` 目前无任何调用方（见 63-13），属“未接入的高危代码”。

### [致命] 63-2｜编解码不对称：函数参数编码被追加在 `E` 终止符之后，demangle 遇到 `E` 即停
- 位置：`NameMangling.c:263-270`（把参数码写在 `base_mangled` 尾部，而 `base_mangled` 已含 `E`）、`NameMangling.c:326,364`（读侧 `while (*ptr && *ptr != 'E')` 后 `ptr++` 直接返回）
- 类别：语义分叉 / 契约破坏（编解码不对称）
- 触发条件：任何带参函数，如 `f(int32)` 生成 `_ZN...Ei`。
- 后果：`name_demangle(name_mangle_function(...))` 只能还原命名空间与函数名，参数类型被永久丢弃；解码结果无法用于重载判别或 ABI 校验。
- 最小修复：参数编码写入 `E` 之前（如 `_ZN...iiE`），并让 `name_demangle` 解析尾部参数段。
- 备注：本文件没有第二个解码入口，`name_demangle` 是唯一解析器。

### [致命] 63-3｜同一文件并存三套互不兼容的 mangling 方案，且只有 `_Z` 方案有解码器
- 位置：`NameMangling.c:99-115`（`class__member`）、`NameMangling.c:117-135`（`class__constructor_N`）、`NameMangling.c:137-179`（`_ZN...E`）
- 类别：语义分叉（mangled 名构造非唯一入口）
- 触发条件：数据成员走 `name_mangle_simple`（`IrGen.c:690`、`SemanticAnalyzer.c:1472`），构造走 `name_mangle_constructor`，函数走 `name_mangle_function`。
- 后果：三类实体分属不同命名空间，无法互相解析；`name_demangle` 对前两套一律返回 0，调用方拿到 0 无法区分“不是 mangled 名”与“解码失败”。
- 最小修复：统一为单一 Scheme（推荐 `_ZN...E`），成员/构造改用 `name_mangle_names` 体系；过渡期至少让 `name_demangle` 返回不同错误码。
- 备注：`name_mangle_class_member`（本可用于统一的成员方案）已实现但零调用。

### [致命] 63-4｜类型编码碰撞：`int8` 与 `char` 同为 `"c"`，`string` 与 `type_string` 同为 `"r"`
- 位置：`NameMangling.c:21-22`（`TOKEN_INT8 → "c"`）、`NameMangling.c:43-44`（`TOKEN_CHAR → "c"`）、`NameMangling.c:45-47`（`TOKEN_STRING/TOKEN_TYPE_STRING → "r"`）
- 类别：语义分叉（碰撞）
- 触发条件：同一作用域重载 `f(int8)` 与 `f(char)`，或 `f(string)` 与 `f(type_string)`。
- 后果：两个不同类型得到完全相同的 mangled 名，符号表合并、调用错绑；解码端也无法区分。
- 最小修复：为 `char`/`int8`/`string`/`type_string` 分配互不相同的码（如 `char→"h"`、`int8→"i8_"`）。
- 备注：与 63-9 的宽度宏编码冲突是同一处根因。

### [致命] 63-5｜构造名与数据成员名碰撞：`A__constructor_2`
- 位置：`NameMangling.c:126,133`（`%s__constructor_%s`）、`NameMangling.c:113`（`%s__%s`）；调用方 `IrGen.c:1207,2961,3040` 与 `IrGen.c:690`
- 类别：语义分叉（碰撞）
- 触发条件：类 `A` 同时声明成员 `constructor_2` 与 2 参构造函数 `A(int32,int32)`。
- 后果：数据成员与构造函数符号完全相同，链接期合并或互相覆盖，行为未定义。
- 最小修复：构造名使用不可能出现在标识符中的分隔（如 `_ZN...E` 体系或 `@ctor`），或在拼接前转义类/成员名中的下划线。
- 备注：`name_mangle_simple` 与 `name_mangle_constructor` 共用 `__` 分隔是碰撞根源。

### [严重] 63-6｜`name_mangle_simple` 对含 `__` 的名字不唯一
- 位置：`NameMangling.c:106,113`
- 类别：语义分叉（碰撞）
- 触发条件：`name_mangle_simple("a__b","c")` 与 `name_mangle_simple("a","b__c")` 均得 `a__b__c`。
- 后果：不同类/成员映射到同一符号，静态字段访问可能读到错误对象。
- 最小修复：改用长度前缀（`strlen:class:member`）或转义名字中的下划线。
- 备注：与 63-5 同属 `__` 分隔方案缺陷，但触发面更广（任何带双下划线的名字）。

### [严重] 63-7｜函数 mangling 丢失指针层级、可空性与 ref 修饰
- 位置：`NameMangling.c:247-250,264-269`（只对 `param_types[i]` 取 `KrtMangleTypeName`）；调用方 `IrGen.c:1740`（`KrtSourceStorage` 把任何指针/函数压成 `TOKEN_UINT64`）、`SemanticAnalyzer.c:1342-1343`（只推断基础 token）
- 类别：语义分叉 / 契约破坏
- 触发条件：重载 `f(int32*)`、`f(int32**)`、`f(uint64)`，或 `f(int32*)` 与 `f(int32*?)`。
- 后果：`f(int32*)` 与 `f(int32**)` 与 `f(uint64)` 同为 `...L`；`f(int32*)` 与 `f(int32)` 虽可分，但可空性完全丢失，重载解析歧义或错绑。
- 最小修复：参数编码加入 `pointer_depth`、`pointee_nullable`、`nullable`、`is_ref`（如 `L` 前加层级/可空标记）。
- 备注：`KrtSourceType` 已携带这些字段，mangle 侧主动丢弃。

### [严重] 63-8｜`f(void)` 与 `f()` 碰撞为同一个 `...Ev`
- 位置：`NameMangling.c:48-49`（`TOKEN_VOID → "v"`）、`NameMangling.c:232-245`（无参时追加 `"v"`）；`ParserBase.c:135`（`TOKEN_VOID` 是合法类型关键字）
- 类别：语义分叉（碰撞）
- 触发条件：声明 `f(void)` 与 `f()` 两个重载（C 风格空参列表）。
- 后果：两者 mangled 名相同，符号冲突。
- 最小修复：无参与 `void` 参数使用不同编码（如无参不追加、`void` 参数编码为 `"V"`），或在语法层禁止 `f(void)`。
- 备注：与 63-2 叠加时，`void` 参数信息在解码端同样丢失。

### [严重] 63-9｜未知类型静默编码为 `"x"`；前置 switch 遮蔽宽度宏导致 6 个 case 死代码
- 位置：`NameMangling.c:50-63`（`default: return "x"`）、`NameMangling.c:21-44`（先于宏捕获 `INT8/16/32/64`、`UINT8/16/32/64`）、`NameMangling.c:53-60`（宏生成的 `TOKEN_INT8` 等分支不可达）
- 类别：静默失败 + 死代码 + 语义分叉
- 触发条件：传入未支持/非法的 `KrtTokenType`（如 `TOKEN_EOF`、`0`）时静默返回 `"x"`；`int8` 实际编码 `"c"` 而 `int2` 编码 `"i2_"`。
- 后果：非法类型不报错而生成合法外观的 mangled 名，多个未知类型互相碰撞；`i8_/i16_/i32_/i64_/u8_/...` 分支永不执行，编码体系自相矛盾。
- 最小修复：删除前置 switch 中的 `INT8..UINT64` case（让宽度宏统一编码），并让未知类型返回 `NULL`/触发诊断而非 `"x"`。
- 备注：`IntegerWidths.def` 定义 2..128 偶数宽度，遮蔽只影响 8/16/32/64。

### [严重] 63-10｜name_demangle 对畸形输入静默成功
- 位置：`NameMangling.c:322-326`（无 `E` 也继续）、`NameMangling.c:364-368`（`E` 后尾随垃圾不校验）、`NameMangling.c:316-320,322`（`_ZN` 零组件也返回 1）
- 类别：静默失败 / 契约破坏
- 触发条件：`_ZN3abc`（缺 `E`）、`_ZN3abcEii`（尾部垃圾）、`_ZNE`（空组件表）。
- 后果：调用方无法区分合法解码与“勉强解析”，错误名字被当作有效符号；与 63-2 叠加使参数垃圾被静默接受。
- 最小修复：要求存在 `E`、`E` 后必须到串尾、至少解析出一个组件，否则返回 0。
- 备注：返回值只有 0/1，无错误原因。

### [严重] 63-11｜name_demangle 的 `KRT_REALLOC` 失败路径泄漏旧数组与已分配名字
- 位置：`NameMangling.c:342-348`（`*out_names = KRT_REALLOC(...)` 直接覆盖）
- 类别：资源与生命周期（G1 活路径）
- 触发条件：组件数超过初始容量 8 且 realloc 失败（`KRT_REALLOC` 失败返回 NULL，原块仍有效但指针被覆盖）。
- 后果：旧 `out_names` 数组与其中已 `KRT_MALLOC` 的每个名字全部泄漏；`*out_names` 被置 NULL，部分状态被掩盖。
- 最小修复：`char** tmp = KRT_REALLOC(*out_names, ...); if (!tmp) { name_demangle_free(*out_names, *out_count); *out_names=NULL; *out_count=0; return 0; } *out_names = tmp;`
- 备注：同函数内 `name` 分配失败路径（350-356）清理是正确的，可作对照。

### [严重] 63-12｜name_mangle_from_ast 的 `KRT_REALLOC` 失败泄漏旧数组且无任何诊断
- 位置：`NameMangling.c:201-207`
- 类别：资源与生命周期 / 静默失败
- 触发条件：命名空间嵌套深度超过初始容量 8 且 realloc 失败。
- 后果：旧 `namespaces` 数组泄漏；函数返回 NULL 而无诊断，调用方（若接入）只能静默失败。
- 最小修复：临时变量接收 realloc，失败时 `KRT_FREE` 旧数组并返回 NULL 且由调用方报错。
- 备注：该函数当前零调用（见 63-13），属未接入的泄漏路径。

### [严重] 63-13｜整组 API 为死代码且解码器零测试覆盖
- 位置：`NameMangling.c:181-222`（`name_mangle_from_ast`）、`NameMangling.c:275-301`（`name_mangle_class_member`）、`NameMangling.c:303-382`（`name_demangle/name_demangle_free`）
- 类别：死代码 / 测试不足
- 触发条件：全库 grep 仅命中定义与头文件声明，无任何调用方；`Test/` 下无 `mangl/demangl` 关键字。
- 后果：63-1 的越界读、63-10 的静默成功、63-11 的泄漏长期无人触发也无人发现；`name_mangle_class_member` 本可统一成员方案却闲置。
- 最小修复：要么接入（符号索引/诊断输出）并补单元测试，要么删除；至少为 `name_demangle` 增加畸形输入与 OOM 测试。
- 备注：`name_mangling_init/cleanup` 虽被 `CompilerPipeline.c:73,136` 调用，但函数体为空（见 63-16）。

### [严重] 63-14｜`size_t`→`int` 截断贯穿长度计算与写数字
- 位置：`NameMangling.c:7-17`（`digit_count(int)`）、`NameMangling.c:66-84`（`write_number` 无负数/上限保护）、`NameMangling.c:93-94,145,163,171`
- 类别：未检查前置条件（截断/溢出）
- 触发条件：命名空间或名字长度超过 `INT_MAX`（或 `strlen` 结果被强转 `int` 后为负）。
- 后果：`digit_count` 返回 0、`write_number` 只写一个 `'\0'` 并返回 0，mangled 名中间被插入 NUL、长度字段与实际内容不符，产生错误碰撞或解析错乱；分配尺寸也随之算错。
- 最小修复：`digit_count/write_number` 内部统一使用 `size_t`，删除 `(int)` 强转。
- 备注：常规源码不会触发，但属于“分配尺寸链”上的未检查前置条件（G1 关注点）。

### [严重] 63-15｜阶段不对齐：调用点用推断类型、定义端用声明类型构造同一函数的 mangled 名
- 位置：`NameMangling.c:224-273`（契约未要求类型规范化）；调用方 `SemanticAnalyzer.c:1342-1347,1656-1661`（`semantic_analyzer_infer_expression_type` 的结果）对比 `IrGen.c:2834`（`stmt->data.function_decl.parameter_types[i]`）、`IrGen.c:2044-2048`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：实参推断类型与形参声明类型不一致（如字面量推断 INT32 而形参为 INT64，或指针/枚举载体差异）。
- 后果：调用点生成的 mangled 名与定义端不同，符号查找失败或绑定到错误重载。
- 最小修复：`name_mangle_function` 只接受声明期规范化后的类型数组；调用点改用被调函数的声明参数类型，而非表达式推断类型。
- 备注：`name_mangle_function` 对入参无任何校验，无法自行发现该不一致。

### [一般] 63-16｜`name_mangling_init/cleanup` 为空壳却接入流水线
- 位置：`NameMangling.c:384-389`；调用方 `CompilerPipeline.c:73,136`
- 类别：死代码 / 契约
- 触发条件：每次编译。
- 后果：生命周期钩子无状态、无资源，误导调用方以为存在需初始化的全局状态；与 63-13 组合说明该模块从未真正接入。
- 最小修复：删除钩子，或填充实际状态并让 `name_mangling_init` 返回有意义的结果。
- 备注：`name_mangling_init` 恒返回 1，错误不可表达。

### [严重] 63-17｜负枚举值存入 `unsigned __int128`，后续按无符号推断为 UINT128
- 位置：`EnumParser.inc:132`（`value->integer_value = (KrtUInt128)next;`，`next` 为 int64 负值）；消费方 `TypeExpansion.inc:411`、`IrGen.c:569-575`
- 类别：语义分叉 / 未检查前置条件
- 触发条件：`enum E { A = -1 };` 之后使用 `E.A`（如作为实参参与重载/mangling）。
- 后果：`-1` 变为 `2^128-1`，`krt_ir_infer_mangle_type` 走 `> INT64_MAX` 分支返回 `TOKEN_UINT128`，而枚举底层类型应为 INT32；同一值在 `enum_constant`（int64 `-1`）与 AST（uint128 全 1）中两种表示。
- 最小修复：枚举值统一存 `int64_t`，或存时用 `KrtIntegerNormalize` 按 INT32 有符号语义规范化，并在读取处显式转回 int32。
- 备注：`Test/SyntaxAudit/test_language_regressions.py:38,43` 覆盖负值，但只断言截断后的返回值，未覆盖类型标注，故未暴露。

### [严重] 63-18｜枚举底层类型在三处硬编码 `TOKEN_INT32`，且解析器不接受底层类型说明符
- 位置：`EnumParser.inc:113`（`item->...type = TOKEN_INT32`）、`EnumParser.inc:134`（`value->declared_type.token`）、`EnumParser.inc:97`（名字后直接要求 `{`，`:` 说明符一律 invalid）；消费方 `TypeExpansion.inc:368-370`
- 类别：契约破坏 / 配置漂移
- 触发条件：源码写 `enum E : int64 { ... }` 或需要 64 位枚举值的程序。
- 后果：说明符被静默判为语法错误（错误消息归为通用 Invalid enum）；即使接受也会被强制按 INT32 存储，值域与 ABI 与声明不符。
- 最小修复：要么在语法层显式拒绝并给出专门诊断，要么把底层类型贯穿 `KrtSourceType`/`value->declared_type`，消除三处硬编码。
- 备注：`IntegerWidths.def` 已支持到 128 位，底层类型固化属于设计缺口。

### [严重] 63-19｜枚举错误诊断位置与原因错位：全部报在 `enum` 关键字行列且共用一条消息
- 位置：`EnumParser.inc:88`（捕获 `line/col`）、`EnumParser.inc:159-162`（唯一 `invalid:` 出口）
- 类别：诊断缺失
- 触发条件：缺名字、缺 `{`、成员重名、非常值表达式、溢出、缺 `}`、EOF 未闭合等 6 类以上失败。
- 后果：所有错误都指向 `enum` 关键字而非实际出错 token，且消息无法区分重名与溢出；`test_language_regressions.py:48-52` 只能断言通用 `Invalid enum`。
- 最小修复：在各失败点用 `parser->current_token.line/column` 报专门消息（重名/溢出/非恒值表达式分开）。
- 备注：`goto invalid` 结构使位置信息在跳转前已丢失。

### [一般] 63-20｜`enum_constant` 递归无深度上限
- 位置：`EnumParser.inc:20`（一元递归）、`EnumParser.inc:32`（二元递归）
- 类别：未检查前置条件（栈溢出）
- 触发条件：深度嵌套的枚举常量表达式（如数万个括号/运算符），解析器表达式阶段未限制深度时。
- 后果：C 栈溢出导致编译器崩溃，且无诊断。
- 最小修复：引入深度计数器，超过阈值即报 “expression too deeply nested”。
- 备注：与 `KrtParseNamedType` 的 `depth > 32` 限制形成对照，后者有防护。

### [一般] 63-21｜`enum_constant` 对数字字面量只判上界，`INT64_MIN` 字面量不可表达
- 位置：`EnumParser.inc:7-9`（`expr->integer_value <= INT64_MAX`）
- 类别：未检查前置条件 / 语义限制
- 触发条件：`-9223372036854775808`：操作数 `9223372036854775808 > INT64_MAX` 使内层字面量判负，外层一元减法拿不到值。
- 后果：合法整数常量表达式被误判为 Invalid enum。
- 最小修复：显式处理 `INT64_MIN`（或允许操作数上界到 `INT64_MAX+1` 并在取负后校验）。
- 备注：枚举最终值域是 int32，但中间表达式用 int64，边界处理不对称。

### [一般] 63-22｜标识符引用分支把 `KrtUInt128` 强转 `int32_t`，依赖实现定义截断
- 位置：`EnumParser.inc:15`（`(int32_t)item->data.static_variable_decl.value->integer_value`）
- 类别：未检查前置条件 / 可移植性
- 触发条件：引用先前列出的枚举成员（尤其是 63-17 的负值成员）。
- 后果：截断是实现定义行为；对负值成员靠回绕恰好得到正确 int32，但无任何范围校验，成员值若超出 int32（其它路径混入时）会静默回绕。
- 最小修复：改为从 `enum_constant` 的 int64 结果取值，或显式校验 `>= INT32_MIN && <= INT32_MAX` 后再转。
- 备注：与 63-17 是同一表示分叉的读取侧。

### [严重] 63-23｜`TOKEN_MUL_ASSIGN` 在类型解析中被当作指针星并原地改写为 `TOKEN_ASSIGN`
- 位置：`PointerTypes.inc:124-125`（把 `*=` 计入指针）、`PointerTypes.inc:135-138`（`parser->current_token.type = TOKEN_ASSIGN;`）
- 类别：共享可变状态 / 契约破坏
- 触发条件：任何在类型位置出现的 `*=`（如 `T* = v` 被 lexer 贪心成 `T` `*=`）。
- 后果：解析器直接篡改共享 token 流，`*=` 永久丢失；错误恢复、回溯或调用方再次查看该 token 时看到的是 `=`，行为依赖调用时序。
- 最小修复：让 lexer 把 `*` 与 `=` 分开产出，或解析器在栈上复制 token 而不写回 `parser->current_token`。
- 备注：该分支还 `break` 出指针循环，语义与 `*=` 作为复合赋值的本意无关，注释未说明。

### [严重] 63-24｜连续 `?` 被静默丢弃：`T??` 的第二个 `?` 直接终止解析
- 位置：`PointerTypes.inc:140-145`（`else if (... && !type->nullable)`，条件不成立时落入 `else break`）
- 类别：静默失败 / 诊断缺失
- 触发条件：`int32??`、`int32*??` 等重复可空标记。
- 后果：多余的 `?` 未报错也未消费，留在 token 流中，可能被后续解析成三元/可空访问，静默改变结构或产生级联错误。
- 最小修复：`?` 已置位时再遇 `?` 显式报 “duplicate nullable marker”。
- 备注：`type->nullable` 在 `*` 后清零（133 行），使 `T?*?*?` 只有部分可空性被记录。

### [严重] 63-25｜`KrtParseNamedType` 对泛型内部 token 不做任何校验
- 位置：`PointerTypes.inc:6-49`（尤其 `8-17` 无条件追加 `token->value`、`31-47` 只判 DOT/LESS）
- 类别：静默失败 / 未检查前置条件
- 触发条件：`Foo<123>`、`Foo<"x">`、`Foo<+>`、`Foo<Bar<Baz>>` 之外的畸形拼写。
- 后果：数字、字符串、运算符被原样拼进类型名并返回“成功”，下游按不存在的类型名处理，错误被推迟且位置丢失。
- 最小修复：在循环内限定可出现的 token 集合（标识符、类型关键字、逗号、`<`/`>`/`>>`、`.`），其余报专门诊断。
- 备注：`depth > 32` 只限制嵌套层数，不限制内容。

### [一般] 63-26｜`KrtParseNamedType` 遗漏 `<=`/`>=`/`>>=`/`<<` 的 depth 处理
- 位置：`PointerTypes.inc:18-26`（只处理 `TOKEN_LESS/TOKEN_GREATER/TOKEN_RSHIFT`）
- 类别：静默失败
- 触发条件：`Foo<=Bar`、`Foo>>=Bar`、`Foo<<Bar` 等拼写。
- 后果：depth 不更新，`<=` 被当作普通字符拼接后可能在 depth==0 提前 break，生成错误类型名而不报错。
- 最小修复：显式拒绝或正确更新 depth（`>>=` 视为两个 `>`）。
- 备注：`TOKEN_RSHIFT` 已处理，`TOKEN_RSHIFT_ASSIGN` 未处理，属同类遗漏。

### [一般] 63-27｜`ref` 以字符串比较识别：占用用户类型名且 `value==NULL` 时崩溃
- 位置：`PointerTypes.inc:62`（`parser->current_token.type == TOKEN_IDENTIFIER && !strcmp(parser->current_token.value, "ref")`）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：用户定义类型恰好叫 `ref`；或标识符 token 的 `value` 为 NULL（lexer 异常路径）。
- 后果：名为 `ref` 的类型被永久当作修饰符，无法声明/引用；`value` 为 NULL 时 `strcmp` 直接崩溃。
- 最小修复：把 `ref` 提升为 token 关键字，并在比较前判 `value`。
- 备注：`KrtParserStrdup` 对 NULL 输入返回 NULL，说明 NULL value 在接口上是被允许的。

### [一般] 63-28｜空参数列表仍分配并保留非 NULL `parameters`，与“无参数”不可区分
- 位置：`PointerTypes.inc:79-81`（先分配 capacity 8）、`PointerTypes.inc:99-101`（空列表直接跳出，`parameters` 非 NULL、`parameter_count==0`）
- 类别：契约破坏
- 触发条件：`fn()` 类型。
- 后果：下游若用 `parameters != NULL` 判断“有参数”会误判；`fn()` 与 `fn` 参数缺失的语义无法区分。
- 最小修复：空列表时把 `fn->parameters` 置 NULL，统一以 `parameter_count` 为唯一判据。
- 备注：`IrGen.c:2042` 已按 `parameter_count` 分配，说明判据应统一。

### [一般] 63-29｜`fn` 分支 OOM 返回 false 但不产生诊断，与其它路径不一致
- 位置：`PointerTypes.inc:68-71`（`if (!fn) return false;`）
- 类别：错误处理不一致
- 触发条件：`KrtParserAlloc` 失败时（当前实现走 `KRT_COMPILE_ERROR`，故实际不可达，但契约上该返回值存在）。
- 后果：若 arena 分配改为返回 NULL，该路径静默失败且不报错误位置；与 `invalid:` 出口（148-151）行为不一致。
- 最小修复：统一走 `invalid:` 或在使用 `KrtParserAlloc` 后直接依赖其致命语义并删除该分支。
- 备注：与 63-30 同属防御性代码与实际契约脱节。

### [一般] 63-30｜`pointee_nullable` 位移语义无注释，`<< (levels - 1)` 对 `TOKEN_POWER` 的复合语义易错
- 位置：`PointerTypes.inc:126-134`
- 类别：注释与实现脱节 / 语义脆弱
- 触发条件：阅读或修改指针可空性编码时；`TOKEN_POWER`（`**`）一次性加两层时。
- 后果：位序（bit0 对应最内层还是最外层 pointee）无注释，`((pn << 1) | nullable) << (levels - 1)` 在 `levels==2` 且已有深度时的位移方向依赖调用顺序；`PointerAnalysis.inc:121-124` 与 `TypeExpansion.inc:213-226` 各自复制了同一公式，一旦此处改正而其它处未同步就会产生新的分叉。
- 最小修复：在 `Ast.h:135` 注释中明确位序定义，并把该公式抽成单一 helper 供三处共用。
- 备注：`Ast.h:135` 现有注释只说明 “Bit 0 describes the immediately pointed-to slot”，未说明多层顺序。
