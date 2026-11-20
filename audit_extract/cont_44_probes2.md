# 第一百零一批：Test/SyntaxAudit/probes（后一半，按文件名字典序）

## 覆盖范围

- 枚举：`glob Test/SyntaxAudit/probes/*.krt` 共 **135** 个文件。
- 排序：按文件名字典序（C locale，字节序，大写字母排在小写前；本目录大小写两种排法结果一致）。
- 抽取：第 **69–135** 个，共 **67** 个，逐个 `read` 全量：
  `m_two_b, mm, ne1, nf, ok1, ok2, or_t, p01_if_braceless, p02_while_braceless, p03_float, p04_bitwise, p05_compound, p06_ternary, p07_switch, p08_continue, p09_manyargs, p1, p10_streq, p11_struct, p12_negmul, p13_ternary_op, p14_global_class, p15_i64index, p16_shortcircuit, p17_i64arg, p18_boolarg, q1, s1, s2, s3, s4, sc_side, sc_t1, sf_t, sf_t2, st1, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15, t16, t17, t18, t19, t20, t21, t_ASSIGN_AND, t_ASSIGN_SHIFT, t_DECL_SHIFT, t_MUL, u1, u2, u3, val, x_c2, xn`。
- 另按约定 `read prelude.krt` 一次（164 行）。prelude 仅提供 `syscall` extern 与 `OutStrN/OutNL/OutBool/OutDigit/OutInt`，**不提供 `Console`、`Sys`、`using`**。
- 未读取前 68 个文件，未通读其他目录源码。

## 问题条目

### [致命] 101-1｜x_c2 断言里残留 `%s` 模板占位符
- 位置：`Test/SyntaxAudit/probes/x_c2.krt:6`
- 类别：字面量/占位符未替换；期望值与实际不符
- 触发条件：编译或运行 x_c2.krt。
- 后果：`if (cs[1] == %s && cs[3] == 100)` 中 `%s` 不是合法表达式（`%` 被当作取模运算符），轻则编译失败，重则被预处理/文本替换成错误值后断言失真；`cs[3] == 100` 又用魔数代替 `'d'`，可读性差。该探针无法验证 `(char[])` 转换与字符比较。
- 最小修复：改为 `if (cs[1] == 'b' && cs[3] == 'd') { P("Y",1); } else { P("N",1); }`，并补 else 分支。
- 备注：与 xn.krt 同源（同为模板生成后未替换）。

### [致命] 101-2｜xn 字符串字面量转义错误且残留两处 `%s`
- 位置：`Test/SyntaxAudit/probes/xn.krt:4`、`Test/SyntaxAudit/probes/xn.krt:6`
- 类别：字符串字面量边界错误；占位符未替换
- 触发条件：编译 xn.krt。
- 后果：第 4 行 `string s = "a\\tb\\nc\\"d";` 中反斜杠被双重转义，`\\"` 使字符串在 `c\\` 处提前闭合，后续 `d";` 变成未闭合/游离 token，语法树被破坏（对照 q1.krt:4 的正确写法 `"a\tb\nc\"d\e"`）；第 6 行 `cs[1] == %s && cs[3] == %s` 两处占位符同样非法。探针必然编译失败或断言无效。
- 最小修复：第 4 行改为 `string s = "a\tb\nc\"d";`，第 6 行改为 `if (cs[1] == '\t' && cs[3] == '\n')`。
- 备注：两个 `%s` 说明该文件由模板批量生成后未做替换校验。

### [高] 101-3｜t_DECL_SHIFT 同作用域重复声明且无断言
- 位置：`Test/SyntaxAudit/probes/t_DECL_SHIFT.krt:5-7`
- 类别：命名与内容不符；静默通过/无断言
- 触发条件：编译/运行 t_DECL_SHIFT.krt。
- 后果：第 5、6 行连续 `int32 x = 0;` 与 `int32 x = 1 << 3;`，同名重复声明要么触发重定义诊断，要么静默覆盖，无法区分；第 7 行无条件 `P("Y",1)`，即使 `1 << 3` 结果错误也照样输出 Y，探针退化为“只测能否编译”。
- 最小修复：删除第 5 行重复声明；改为 `int32 x = 1 << 3; if (x == 8) P("Y",1); else P("N",1);`。
- 备注：与 t_ASSIGN_SHIFT 同为移位相关探针，但一个重复声明、一个普通赋值，覆盖重叠。

### [高] 101-4｜t_ASSIGN_* / t_MUL 命名暗示赋值运算符，实为普通赋值且无断言
- 位置：`Test/SyntaxAudit/probes/t_ASSIGN_AND.krt:6-7`、`Test/SyntaxAudit/probes/t_ASSIGN_SHIFT.krt:6-7`、`Test/SyntaxAudit/probes/t_MUL.krt:6-7`
- 类别：命名与内容不符；静默通过/无断言
- 触发条件：编译/运行上述三个探针。
- 后果：文件名 `t_ASSIGN_AND`/`t_ASSIGN_SHIFT` 暗示复合赋值 `&=` / `<<=`，实际是 `x = 6 & 3;` / `x = 1 << 3;` 普通赋值，复合赋值语法完全未被覆盖；三例都以无条件 `P("Y",1)` 结束，不对 `x` 的取值做任何判断，恒真通过。
- 最小修复：若要测复合赋值则写 `x &= 3; if (x == 2) ...`、`x <<= 3; if (x == 8) ...`；若只测表达式则补 `if (x == 2/8/18) P("Y",1); else P("N",1);`。
- 备注：与 p04_bitwise.krt（有断言）覆盖重叠且更弱。

### [高] 101-5｜ok2 / or_t / val 无任何输出与断言，静默通过
- 位置：`Test/SyntaxAudit/probes/ok2.krt:1-4`、`Test/SyntaxAudit/probes/or_t.krt:1`（空文件）、`Test/SyntaxAudit/probes/val.krt:1`（空文件）
- 类别：静默通过/无断言
- 触发条件：运行这三个探针。
- 后果：ok2 只有 `while (i<3) i=i+1;`，既不输出也不检查 `i`；or_t、val 为 0 字节空文件。三者无论编译/运行是否正确都没有可观测信号，失败与无法运行被误判为通过；or_t 名含 `or` 却毫无逻辑或运算内容。
- 最小修复：ok2 末尾 `if (i == 3) P("Y",1); else P("N",1);`；or_t 补 `(1==2)||(2==2)` 的 Y/N 断言；val 至少补一条最小 `main` 输出，否则删除。
- 备注：空文件可能是生成/拷贝事故，应直接删除而不是留在套件里。

### [高] 101-6｜大量探针依赖 prelude 未提供的 `Console`
- 位置：`ok1.krt:1`；`t3.krt:1,3,5,6`；`t5.krt:2`；`t6.krt:2,4`；`t7.krt:5`；`t9.krt:2,4`；`t11.krt:7`；`t12.krt:13`；`t13.krt:22`；`t14.krt:100-103`；`t15.krt:40-43`；`t16.krt:37-40`；`t17.krt:25`；`t20.krt:2`；`t21.krt:2,3`
- 类别：探针期望值与 prelude 约定不一致；依赖外部状态
- 触发条件：按约定将探针与 `prelude.krt` 一起编译。
- 后果：prelude.krt 只定义 `syscall/OutStrN/OutNL/OutBool/OutDigit/OutInt`，没有 `Console`，也没有 `using`。上述 15 个文件引用 `Console.WriteLine/Write`，t3 还使用 `using System;`。除非另链接一个含 `Console` 的运行时，否则全部编译失败；即便链接，也与“配合 prelude.krt 使用”的约定不一致。
- 最小修复：统一改用 prelude 的 `OutStrN/OutInt/OutNL`（或把 `Console` 显式写入 prelude）；t3 去掉 `using System;`。
- 备注：这是后半部分最大面积的约定漂移，建议整批回归到 prelude 输出约定。

### [高] 101-7｜Sys 被引用但文件内与 prelude 均未定义
- 位置：`t4.krt:3`；`t5.krt:2`；`t6.krt:2,4`；`t9.krt:2,4`；`t10.krt:3`；`t19.krt:3`；`t20.krt:2`；`t21.krt:2,3`
- 类别：依赖外部状态/未定义符号；期望值与 prelude 约定不一致
- 触发条件：单独编译上述探针（或仅与 prelude.krt 一起编译）。
- 后果：这 8 个文件调用 `Sys.InternalStringLength` / `Sys.InternalInt32ToString` 等，但自身没有定义 `Sys`，prelude.krt 也没有。t19 尤为明显：全文只有 `syscall` 声明和 `main`，却直接 `Sys.InternalInt32ToString(42)`。结果必然是未定义符号错误，或隐性依赖某个未声明的历史运行时。
- 最小修复：要么在这些文件内补 `Sys` 定义（可复用 t14/t15 的版本），要么把 `Sys` 并入 prelude.krt。
- 备注：t14/t15/t16/t18 自定义了 `Sys`，但并未被这些文件共享，说明缺少统一运行时文件。

### [高] 101-8｜仅以进程退出码为断言，运行器不检查即静默
- 位置：`t4.krt:4`、`t8.krt:4-6`、`t10.krt:4`、`sf_t.krt:7`、`sf_t2.krt:12`
- 类别：依赖外部状态/断言方式不可靠
- 触发条件：运行器只收集 stdout、不收集退出码时。
- 后果：t4/t10 用 `syscall(60, L, ...)` 把字符串长度作为退出码；t8 用退出码 2/3/4 区分 mmap 返回值分支；sf_t/sf_t2 用退出码 5/6 表示静态字段读写结果。若运行器只看输出（这些文件 stdout 为空），所有分支都无信号，恒“通过”。
- 最小修复：改为输出 Y/N 或数字后再退出；至少在运行器中强制校验退出码。
- 备注：与 prelude 的 `OutInt` 输出约定冲突，退出码取值也被限制在 0–255。

### [中] 101-9｜s2 与 s4 内容完全重复
- 位置：`Test/SyntaxAudit/probes/s2.krt:1-17`、`Test/SyntaxAudit/probes/s4.krt:1-17`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：两个文件逐行完全相同（D/E/F + struct Vec + main 三次调用），同一覆盖被计两次，维护时易漏改其一，掩盖真实覆盖率。
- 最小修复：删除其一（保留 s4），或让两者分别覆盖不同结构体特性（如 s2 只字段读、s4 字段写/嵌套）。
- 备注：s3.krt 又是 s2/s4 的子集（仅 D 与 F），三者叠加重复。

### [中] 101-10｜u1 / u2 / u3 仅结构体声明顺序不同，覆盖重复
- 位置：`Test/SyntaxAudit/probes/u1.krt:1-17`、`Test/SyntaxAudit/probes/u2.krt:1-17`、`Test/SyntaxAudit/probes/u3.krt:1-17`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：三例同样实现 D/E/F + Vec + 相同 main，唯一差异是 `struct Vec` 相对函数的位置（u1 在 E/F 前，u2 在 D/E 与 F 之间，u3 在 F 后）。若语言允许任意位置声明，则后两例几乎不增加覆盖；若不允许，则只有一例能通过，另两例是噪声。
- 最小修复：保留一例即可；若要测声明顺序，应明确期望（有序/无序）并给出不同断言。
- 备注：与 s2/s4 的“声明顺序”主题重叠。

### [中] 101-11｜t17 是 t13 的重复副本，仅多一个未使用函数
- 位置：`Test/SyntaxAudit/probes/t13.krt:1-23`、`Test/SyntaxAudit/probes/t17.krt:1-26`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：class T.Probe 与 main 主体逐行一致，t17 仅额外加入 `function zz_tail(int64 q)`（从未被调用，死代码）。同一 int32→字符串转换路径被覆盖两次，且死函数会额外掩盖“未使用函数”诊断。
- 最小修复：删除 t17，或让 t17 覆盖不同输入（如负数、0）并删除 zz_tail。
- 备注：t13 与 t17 都依赖未定义的 `Console`（见 101-6）。

### [中] 101-12｜t14 / t15 / t16 / t18 的 Sys 类高度重复
- 位置：`t14.krt:4-97`、`t15.krt:4-38`、`t16.krt:4-35`、`t18.krt:4-97`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：四份 `Sys` 类都实现 `InternalStringLength/InternalStringPtr/InternalInt32ToString`；t14 与 t18 的类体完全相同（含常量表与 `Syscall/Exit`），t15 把 `const` 换成非 const `static` 字段，t16 直接内联 `syscall(9,...)`。差异仅在于声明形式，主体逻辑重复，维护时四处同步。
- 最小修复：抽成一份共享 `Sys` 运行时（或并入 prelude），各探针只保留差异点；至少合并 t14 与 t18。
- 备注：t18 还另行声明 `ksyscall` extern，与 prelude 的 `syscall` 并存，进一步放大冗余。

### [中] 101-13｜p02_while_braceless 名不符实，且 braceless while 实际未覆盖
- 位置：`Test/SyntaxAudit/probes/p02_while_braceless.krt:6`
- 类别：命名与内容不符；覆盖缺口
- 触发条件：按文件名理解覆盖范围时。
- 后果：文件名声明“无花括号 while”，第 6 行却是 `while (i < 3) { P("w", 1); i = i + 1; }`，带花括号。真正无花括号的 while 语法在后半部分没有任何探针覆盖，名称却误导为已覆盖。
- 最小修复：改为 `while (i < 3) P("w",1), i = i + 1;` 之类的无花括号形式（或明确语言语法），或重命名为 `p02_while_braced` 并另补 braceless 用例。
- 备注：p01_if_braceless 的无花括号写法可参照。

### [中] 101-14｜p06_ternary 名不符实，且与 p01 重复
- 位置：`Test/SyntaxAudit/probes/p06_ternary.krt:5-7`
- 类别：命名与内容不符；重复探针
- 触发条件：按文件名理解覆盖范围时。
- 后果：文件名声明三元运算符，正文只有 `if (x > 0) P("P",1); else P("N",1);`，没有任何 `?:`。三元运算实际由 p13_ternary_op.krt 覆盖，本例既名不符实又与 p01 的 if/else 覆盖重复。
- 最小修复：改为 `int32 x = 5; string s = x > 0 ? "P" : "N"; P(s,1);`（与 p13 二选一），或重命名并明确只测 if/else。
- 备注：与 p13 并存导致“ternary”关键词被两个文件分别冒名/正名。

### [中] 101-15｜p16_shortcircuit 并未验证短路
- 位置：`Test/SyntaxAudit/probes/p16_shortcircuit.krt:7-8`
- 类别：命名与内容不符
- 触发条件：按文件名理解覆盖范围时。
- 后果：两行只判断 `&&`/`||` 的布尔结果（都必为真），没有引入副作用计数，无法区分“短路”与“两边都求值”。真正的短路语义由 sc_side.krt 通过静态字段计数覆盖，本例名称夸大。
- 最小修复：重命名为 `p16_boolop`，或改为复用 `G.count` 副作用的写法。
- 备注：与 sc_side 覆盖重叠。

### [中] 101-16｜s1 / t11 / t12 断言过弱，字段写入与 mmap 内容不可见
- 位置：`Test/SyntaxAudit/probes/s1.krt:8-11`、`Test/SyntaxAudit/probes/t11.krt:5-7`、`Test/SyntaxAudit/probes/t12.krt:5-7`
- 类别：静默通过/无有效断言
- 触发条件：运行上述探针。
- 后果：s1 写入 `v.x=3; v.y=4` 后只打印固定 `"S"`，不比较字段值，结构体字段写错也照过；t11/t12 向 mmap 缓冲写入 `65/66` 后从不读取，只输出 `"ok\n"`，写入是否落到正确地址不可见。
- 最小修复：s1 增加 `if (v.x + v.y == 7) P("Y",1); else P("N",1);`；t11/t12 回读 `chars[0]/chars[1]` 并断言为 65/66。
- 备注：t13/t17 对同一 mmap 路径做了真实回读断言，可作为修复模板。

### [中] 101-17｜q1 使用 `\e` 转义，语义依赖编译器实现
- 位置：`Test/SyntaxAudit/probes/q1.krt:4`、`Test/SyntaxAudit/probes/q1.krt:9`
- 类别：字符串字面量边界
- 触发条件：编译器未把 `\e` 定义为普通字符 `e` 时。
- 后果：`s = "a\tb\nc\"d\e"` 共 8 字符，第 9 行断言 `cs[7] == 'e'`。若按 C 惯例 `\e` 为 ESC(0x1B)，或编译器对未知转义报错/保留为 `\e` 两字符，则断言失败或编译失败；该探针的期望值建立在对转义扩展的隐含假设上。
- 最小修复：改用明确支持的转义（如 `\x1b` 或 `\033`）并在断言里写对应数值，或删去 `\e` 段。
- 备注：与 xn.krt 同属转义/字符数组转换主题，但 q1 至少字面量本身闭合正确。

### [中] 101-18｜后半部分明显未覆盖的语法节点
- 位置：整个后半探针集（对照 prelude 与已读 67 文件）
- 类别：覆盖缺口
- 触发条件：评估套件完整性时。
- 后果：以下节点在后半部分未见任何用例：`for` 循环、`do-while`；右移位 `>>`、按位取反 `~`、`<=`、`>=`；`++`/`--`；除 `+=`（p05）外的复合赋值 `-= *= /= %= &= |= ^= <<= >>=`；`else if` 链；循环内 `break`（`break` 仅见于 p07 的 switch）；非 static 类实例化（`new class`）；`enum`、`namespace`、`interface`；`typeof`/`sizeof`；多维数组；字符串内插/常用方法；`const` 局部与 `static` 局部。已覆盖节点（if/else、while、continue、switch/default、struct、static class 字段、三元、`global::`、`using`、`!=`、int64 传参/下标、`new int32[n]`、`null`、`void`、`const` 类字段）说明这些缺口是遗漏而非语言不支持。
- 最小修复：为上述每类至少补一条带断言的探针，优先 `for`、`do-while`、`>>`、`<=`/`>=`、`++/--`、循环 `break`、复合赋值族。
- 备注：本结论仅基于后半 67 个文件；若前半已覆盖，请在合并时去重。

### [低] 101-19｜t4/t10、t6/t9 仅作用域运算符写法不同
- 位置：`t4.krt:3` 与 `t10.krt:3`；`t6.krt:2,4` 与 `t9.krt:2,4`
- 类别：重复探针
- 触发条件：套件去重/覆盖率统计时。
- 后果：t4 用 `Sys::InternalStringLength`、t10 用 `Sys.InternalStringLength`，其余完全相同；t6/t9 同理。若 `::` 与 `.` 语义等价，则后一对是纯重复；若不等价，则应明确断言差异而不是两套输出格式。
- 最小修复：合并为参数化用例，或在两个文件中分别断言 `::` 与 `.` 的解析结果差异。
- 备注：两对文件本身又都落在 101-6/101-7 的未定义符号问题内。
