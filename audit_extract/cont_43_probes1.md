# 第一百批：Test/SyntaxAudit/probes（前一半，按文件名字典序）

## 覆盖范围

- 目录 `Test/SyntaxAudit/probes/` 共枚举到 **135** 个 `.krt` 文件（glob 结果按修改时间返回，已自行按文件名字典序重排）。
- 本批只读取字典序**前 68 个**，逐一全文读取；后 67 个（`m_two_b.krt` 起至 `u3.krt`）未读取。
- 另读取 `Test/SyntaxAudit/prelude.krt` 一次以理解约定：prelude 声明 `public static extern int64 syscall(...)`，并提供 `OutStrN/OutNL/OutBool/OutDigit/OutInt`（`OutInt` 处理负数与 10 位十进制），本身不含 `main`。
- 前 68 个文件清单（字典序）：
  `a1 a2 a3 a_t b1 b2 b3 b4 bad bsl bsl2 c1 c2 cc1 cc2 cc3 cc4 cc5 cmp_and cmp_involution cmp_pipe e1 e2 e3 e_A e_B e_C e_bs e_nl e_nul e_plain e_quote e_tab esc f1 f_assign f_decl f_inf fa fb fe_min fem formin g1 g2 g3 g4 g6 g_min gs_min hd4 hdr hdr2 hdr3 hdx hm_int323 hm_int327 hm_int643 k0 k1 l1 l2 lx m1 m2 m_nl2 m_three m_two`

### 逐文件结论

| 文件 | 结论 |
|---|---|
| a1 | 正常：hex/bin/float 断言齐备 |
| a2 | 正常：`0b`/`0B` 前缀、浮点乘除断言齐备 |
| a3 | 低：与 a2 覆盖重复（`!=`、float 子集） |
| a_t | 正常：`D(8+7)`→`F`，断言明确 |
| b1 | 正常：`&&` 链断言 |
| b2 | 正常：六种比较断言 |
| b3 | 低：b2 子集 |
| b4 | 低：b1 子集 |
| bad | 中：负向探针混入未定义 `P`，诊断不唯一（见 100-11） |
| bsl | 正常：`(char)92` 断言 |
| bsl2 | 低：`'\\'` 可编译但无任何断言（见 100-18） |
| c1 | 低：b2 子集 |
| c2 | 低：与 b3 等价 |
| cc1 | 低：有确定性输出但无显式期望值（见 100-18） |
| cc2 | 低：同上 |
| cc3 | 低：与 cc2 对照但无断言标记（见 100-18） |
| cc4 | 低：同上 |
| cc5 | 低：变长输出，无断言（见 100-18） |
| cmp_and | 高：位运算结果无断言 + 命名不符（见 100-4） |
| cmp_involution | 高：同上（见 100-4） |
| cmp_pipe | 高：同上（见 100-4） |
| e1 | 正常：缺表达式负向探针 |
| e2 | 正常：未定义变量负向探针 |
| e3 | 中：与 e1 重复（见 100-12） |
| e_A | 正常：`12\|3==15`→`A`，else-if 链 |
| e_B | 低：与 e_A 输出等价（冗余） |
| e_C | 正常：`4^2==6`→`B` |
| e_bs | 高：长度边界错误（见 100-2） |
| e_nl | 正常：`"a\nb"` 3 字节参数正确 |
| e_nul | 正常：`"a\0b"` 3 字节参数正确 |
| e_plain | 正常 |
| e_quote | 正常：`"q\"q"` 3 字节参数正确 |
| e_tab | 正常 |
| esc | 高：缺 `syscall`/`P` + 长度错误 + 与 m_three 重复（见 100-3） |
| f1 | 低：命名与内容不符（见 100-14） |
| f_assign | 正常：for 初始化赋值 |
| f_decl | 中：与 fb/formin 三重重复（见 100-8） |
| f_inf | 致命：无限循环无终止（见 100-1） |
| fa | 正常：外部声明 + for 赋值 |
| fb | 中：与 f_decl 重复（见 100-8） |
| fe_min | 正常：int32 数组 + foreach |
| fem | 正常：int64 数组 + foreach |
| formin | 中：与 f_decl 重复（见 100-8） |
| g1 | 高：`Set/Get` 结果未校验且与 g2 重复（见 100-5） |
| g2 | 正常：`D(b.Get())` 断言 5 |
| g3 | 正常：泛型类 |
| g4 | 中：依赖 prelude 未提供的 `Console`（见 100-10） |
| g6 | 中：依赖 `Console`/`global::Sys`（见 100-10） |
| g_min | 正常：非 static 全局 |
| gs_min | 正常：static 全局 |
| hd4 | 中：与 hm_int643 重复 + 依赖内存布局（见 100-9/100-13） |
| hdr | 中：负索引读数组头，依赖内存布局（见 100-13） |
| hdr2 | 中：与 hm_int323 重复（见 100-9） |
| hdr3 | 中：依赖内存布局（见 100-13） |
| hdx | 中：与 hm_int327 重复（见 100-9） |
| hm_int323 | 中：与 hdr2 重复（见 100-9） |
| hm_int327 | 中：与 hdx 重复（见 100-9） |
| hm_int643 | 中：与 hd4 重复（见 100-9） |
| k0 | 中：外部运行时依赖 + 结果未断言（见 100-10） |
| k1 | 中：外部运行时依赖（`Memory.Copy`/`Console`）（见 100-10） |
| l1 | 中：缺 `P` 定义，掩盖 lambda 语法验证（见 100-6） |
| l2 | 中：缺 `P` 定义（见 100-6） |
| lx | 中：缺 `P` 定义，类型错误与未定义符号混杂（见 100-6） |
| m1 | 中：依赖 `Console`/`Sys`（见 100-10） |
| m2 | 中：依赖 `Console`/`Sys::`（见 100-10） |
| m_nl2 | 正常：5 字节参数正确 |
| m_three | 高：长度边界错误（见 100-2） |
| m_two | 正常：5 字节参数正确 |

## 问题条目

### [致命] 100-1｜f_inf.krt 无限循环无终止条件，运行审计会永久挂起
- 位置：`Test/SyntaxAudit/probes/f_inf.krt:6`
- 类别：静默通过/无断言 + 运行时死循环
- 触发条件：编译并运行该探针（`for (;;) { P("f", 1); }`）。
- 后果：循环无 `break`、无计数上限，`P` 无限输出；审计进程 hang，后续批次无法继续，且该探针没有任何期望值/退出码断言。
- 最小修复：加入计数上限并在达限时 `break`；或明确将其定为纯语法解析探针（不执行），并加运行超时保护。
- 备注：文件名 `f_inf` 似有意测无限循环，但缺少终止与超时保护，对批量审计是阻断级风险。

### [高] 100-2｜字符串转义探针长度参数小于实际字节数，输出被截断
- 位置：`Test/SyntaxAudit/probes/e_bs.krt:5`（`P(s, 2)`，运行时 3 字节）；`Test/SyntaxAudit/probes/m_three.krt:5`（`P(s, 6)`，运行时 7 字节）；`Test/SyntaxAudit/probes/esc.krt:3`（`P(s, 6)`，运行时 7 字节）
- 类别：数值/字符串字面量边界错误
- 触发条件：运行探针。
- 后果：`"b\\s"` 实为 `b\s`（3 字节），`P(s,2)` 只输出 `b\`；`"a\tb\nc\"d"` 实为 7 字节，`P(s,6)` 丢掉末尾 `d`。若期望输出按完整串记录，测试必失败；若按现状记录，则把错误截断行为固化进基线。
- 最小修复：`e_bs.krt:5` 改为 `P(s, 3)`；`m_three.krt:5`、`esc.krt:3` 改为 `P(s, 7)`。
- 备注：`m_three.krt` 与 `esc.krt` 内容重复（见 100-3）。

### [高] 100-3｜esc.krt 缺少 syscall/P 定义，无法编译且与 m_three 重复
- 位置：`Test/SyntaxAudit/probes/esc.krt:1-4`
- 类别：依赖缺失 + 重复探针
- 触发条件：编译该探针。
- 后果：文件未声明 `syscall`、未定义 `P`，`P(s, 6)` 解析失败；与 `m_three.krt` 的字符串与长度完全相同，属不完整重复，无法独立承担转义探针职责。
- 最小修复：补齐 `syscall`/`P` 定义（与 m_three 一致），或删除该文件、由 `m_three.krt` 承担覆盖。
- 备注：`esc.krt` 与 `bad.krt` 是同批中仅有的缺少 `syscall` 声明的正向探针之一。

### [高] 100-4｜位运算探针只打印固定 "Y"，运算结果零断言，且命名不符
- 位置：`Test/SyntaxAudit/probes/cmp_and.krt:6-7`（`12 & 3`）；`Test/SyntaxAudit/probes/cmp_involution.krt:6-7`（`12 ^ 3`）；`Test/SyntaxAudit/probes/cmp_pipe.krt:6-7`（`12 | 3`）
- 类别：静默通过/无断言 + 命名与内容不符
- 触发条件：`&`/`^`/`|` 任一运算符实现被改错（例如 `&` 实现成 `|`）。
- 后果：`x` 被计算但从未使用，三个探针恒输出 `Y`，任何位运算实现都能通过，完全无法检测语义回归。
- 最小修复：对 `x` 断言（`12&3==0`、`12|3==15`、`12^3==15`），可参照 `e_A.krt:6-9`、`e_C.krt:6-9` 的写法。
- 备注：文件名前缀 `cmp_` 暗示比较运算，内容却是位运算；`cmp_involution` 用词与 XOR 语义不符。

### [高] 100-5｜g1.krt 调用 Set/Get 但不校验返回值，与 g2 重复且无断言
- 位置：`Test/SyntaxAudit/probes/g1.krt:9-11`
- 类别：静默通过/无断言 + 重复探针
- 触发条件：`Box.Get()` 返回错误值、或 `Set` 未生效、或字段未初始化。
- 后果：`b.Set(5)` 后直接 `P("R", 1)`，`Get()` 从未被调用，类字段/方法语义错误无法发现；与 `g2.krt:9-11`（用 `D(b.Get())` 断言 5）重复且更弱。
- 最小修复：改为 `D(b.Get())` 并断言 5（同 g2），或删除 g1 仅保留 g2。
- 备注：`g2.krt` 是正确范例。

### [高] 100-6｜l1/l2/lx 调用未定义的 P，正向探针无法编译，掩盖目标语法
- 位置：`Test/SyntaxAudit/probes/l1.krt:4`；`Test/SyntaxAudit/probes/l2.krt:4`；`Test/SyntaxAudit/probes/lx.krt:4`
- 类别：依赖缺失 + 静默失败
- 触发条件：编译上述探针。
- 后果：三个文件均未声明 `syscall`、未定义 `P`。`l1`（`var inc = function (int32 a) => a + 1;`）与 `l2`（`Apply(3, function (int32 a) => a + 1);`）本应验证 lambda 定义/传参，但错误被 `P` 未定义掩盖，无法区分"lambda 不支持"与"缺少输出函数"；`lx`（`int32 inc = function...`）的类型错误也与 `P` 未定义混杂，诊断不纯。
- 最小修复：三个文件均补齐 `syscall`/`P` 定义（与 a1 等同批探针一致）。
- 备注：`l1.krt:1` 的 `Apply` 在 `l1` 中未被调用，亦为冗余。

### [高] 100-7｜自带 syscall/P 的探针与 prelude.krt 的约定冲突/脱节
- 位置：前 68 个文件中除 `bad/e1/e2/e3/esc/g4/g6/l1/l2/lx/m1/m2` 外的 56 个文件，均在第 1-2 行重复声明 `syscall` 并自定义 `P`（例：`Test/SyntaxAudit/probes/a1.krt:1-2`、`Test/SyntaxAudit/probes/m_two.krt:1-2`）
- 类别：探针期望值与 prelude 约定不一致
- 触发条件：按"prelude.krt + 探针"同一编译单元拼接编译。
- 后果：`syscall` 与 prelude 的声明重复；prelude 提供的 `OutStrN/OutBool/OutInt` 在前半 68 个探针中完全未被使用，`P` 与 `OutStrN` 功能重复。若为同文件拼接，重复声明可能直接导致编译失败；若探针独立编译，则 prelude 对前半无效，约定未落地。
- 最小修复：二选一——由 prelude 统一提供 `P`/输出函数、探针不再自带 `syscall`；或在审计文档中明确探针独立编译、prelude 不适用，并删除冗余声明。
- 备注：`Prelude` 的 `OutInt` 具备负数与位宽处理，而探针的 `P` 只能输出原始字节，能力不一致。

### [中] 100-8｜f_decl/fb/formin 三个探针语义完全相同（for 初始化声明）
- 位置：`Test/SyntaxAudit/probes/f_decl.krt:6`；`Test/SyntaxAudit/probes/fb.krt:4`；`Test/SyntaxAudit/probes/formin.krt:5-8`
- 类别：重复探针
- 触发条件：—
- 后果：三者都是 `for (int32 i = 0; i < 3; i = i + 1) { P("f", 1); }`，仅花括号/空行排版不同，覆盖同一语法节点，维护成本三倍。
- 最小修复：保留其一，其余改为覆盖 for 的其他形态（如省略条件、省略步长、多层嵌套）。
- 备注：`f_assign.krt:6`（初始化赋值）与 `fa.krt:4-5`（外部声明）才是该族的有效变体。

### [中] 100-9｜hdr 系列三对探针互为重复
- 位置：`Test/SyntaxAudit/probes/hdr2.krt:4-6` 与 `Test/SyntaxAudit/probes/hm_int323.krt:4-6`；`Test/SyntaxAudit/probes/hdx.krt:4-6` 与 `Test/SyntaxAudit/probes/hm_int327.krt:4-6`；`Test/SyntaxAudit/probes/hd4.krt:4-5` 与 `Test/SyntaxAudit/probes/hm_int643.krt:4-6`
- 类别：重复探针
- 触发条件：—
- 后果：`hdr2` 与 `hm_int323` 均为 `int32[3]`→`(int64[])`→`raw[-1]`（仅差一个显式 `(int64)` 转换）；`hdx` 与 `hm_int327` 均为 `int32[7]`→`raw[-1]`，逐字相同；`hd4` 与 `hm_int643` 均为 `int64[3]` 读 `[-1]`，后者仅多一次冗余 `(int64[])` 转换。
- 最小修复：每对保留一个；把腾出的名额用于覆盖其他头/边界（如 `int8[]`、`float64[]` 或越界诊断）。
- 备注：`hdr.krt:8` 用 `D((int32)raw[-1])` 断言 12，与上述 exit-code 型探针不同，不属重复。

### [中] 100-10｜引用 prelude.krt 未提供的运行时符号（Console/Sys/Memory）
- 位置：`Test/SyntaxAudit/probes/g4.krt:1`（`Console.WriteLine`）；`Test/SyntaxAudit/probes/g6.krt:2`（`Console.WriteLine` + `global::Sys.InternalStringLength`）；`Test/SyntaxAudit/probes/k0.krt:4`（`Sys.InternalStringPtr`）；`Test/SyntaxAudit/probes/k1.krt:4-5`（`Sys.InternalStringPtr`、`Memory.Copy`、`Console.Write`）；`Test/SyntaxAudit/probes/m1.krt:2`（`Console.WriteLine` + `Sys.InternalStringLength`）；`Test/SyntaxAudit/probes/m2.krt:2`（`Console.WriteLine` + `Sys::InternalStringLength`）
- 类别：依赖外部状态/未声明运行时
- 触发条件：以 `prelude.krt` 作为唯一运行时/库编译探针。
- 后果：`Console`、`Sys`、`Memory` 在 prelude 与探针内均无声明，符号无法解析；这些探针要么编译失败，要么隐式依赖仓库外的运行时库，审计结果随外部库版本漂移。`k0` 还丢弃 `Sys.InternalStringPtr` 结果且无任何输出/断言；`k1` 依赖指针运算与 `Memory.Copy`，行为不可控。
- 最小修复：在 prelude 中提供这些符号的桩（声明 + syscall 实现），或改用 prelude 的 `OutStrN/OutInt` 与 `syscall` 重写。
- 备注：`m1` 与 `m2` 仅差 `.` 与 `::`，属有意的语法变体对，但都依赖外部 `Console`。

### [中] 100-11｜bad.krt 在目标诊断之外引入未定义 P，负向探针诊断不唯一
- 位置：`Test/SyntaxAudit/probes/bad.krt:2-3`
- 类别：负向探针诊断噪声
- 触发条件：编译该探针。
- 后果：目标是未终止字符字面量 `'x`，但文件未声明 `syscall`、未定义 `P`，`P("Z",1)` 会额外产生未定义符号错误；若干诊断按集合/精确匹配，会失败或掩盖目标诊断。
- 最小修复：删除 `P("Z",1);`，或补齐 `syscall`/`P` 定义，使唯一诊断为字符字面量未终止。
- 备注：`e1/e2/e3` 未引入额外未定义符号，负向探针应保持同样的"单一错误"纪律。

### [中] 100-12｜e1/e3 重复（e3 = e1 + return 0）
- 位置：`Test/SyntaxAudit/probes/e1.krt:1`；`Test/SyntaxAudit/probes/e3.krt:1`
- 类别：重复探针
- 触发条件：—
- 后果：两者都是 `int32 x = ;` 缺表达式语法错误，`e3` 仅多一句 `return 0;`，覆盖同一诊断。
- 最小修复：合并为一个探针，腾出名额覆盖其他缺表达式位置（如返回值、实参、下标）。
- 备注：`e2.krt:1`（未定义变量）是该族唯一有效补充。

### [中] 100-13｜hdr 系列以负索引读取数组头，依赖未定义内存布局
- 位置：`Test/SyntaxAudit/probes/hdr.krt:8`；`Test/SyntaxAudit/probes/hdr2.krt:6`；`Test/SyntaxAudit/probes/hdr3.krt:6`；`Test/SyntaxAudit/probes/hdx.krt:6`；`Test/SyntaxAudit/probes/hm_int323.krt:6`；`Test/SyntaxAudit/probes/hm_int327.krt:6`；`Test/SyntaxAudit/probes/hm_int643.krt:6`；`Test/SyntaxAudit/probes/hd4.krt:5`
- 类别：依赖外部状态/未定义行为
- 触发条件：运行时/GC/数组头布局变化（如启用边界检查、压缩指针、对象头偏移调整）。
- 后果：`arr[-1]`、`raw[-1]`、`raw[0]` 依赖"长度头位于数据之前"的具体布局，属未定义行为；布局一变，探针结果（退出码/`D` 输出）随之改变，审计误报或漏报。`hdr3.krt:6` 用 `raw[0]` 读取首元素而非头部，语义与同族不一致，易被误读为同一断言。
- 最小修复：改用语言提供的长度/头信息 API；若必须测布局，固定并注释布局假设，并加编译期断言。
- 备注：若项目有意以这些探针锁定 ABI 布局，应在探针内写明假设，否则视为脆弱依赖。

### [中] 100-14｜命名与内容不符
- 位置：`Test/SyntaxAudit/probes/f1.krt:4-5`（名为 f 族 for 循环，实为 float 乘法）；`Test/SyntaxAudit/probes/m1.krt:2`、`Test/SyntaxAudit/probes/m2.krt:2`（m 族为字符串转义，实为 `Console.WriteLine`）；`Test/SyntaxAudit/probes/cmp_and.krt:6`、`Test/SyntaxAudit/probes/cmp_pipe.krt:6`、`Test/SyntaxAudit/probes/cmp_involution.krt:6`（`cmp_` 前缀但为位运算，无比较）；`Test/SyntaxAudit/probes/e_A.krt:6`、`Test/SyntaxAudit/probes/e_B.krt:6`、`Test/SyntaxAudit/probes/e_C.krt:6`（e 族为错误/转义，实为 else-if 链）；`Test/SyntaxAudit/probes/hd4.krt:1-5`（名称无实义）
- 类别：命名与内容不符
- 触发条件：按文件名检索/维护时。
- 后果：`f1` 归入 for 族、`m1/m2` 归入转义族、`cmp_*` 归入比较族均会误导；`hd4` 无法从名称判断测什么，增加审计与回归定位成本。
- 最小修复：按内容改名（如 `bit_and`/`bit_or`/`bit_xor`、`float_mul`、`escape_multi`），或重排文件名前缀。
- 备注：`fe_min`/`fem` 缩写过度，亦建议补全为 `foreach_i32`/`foreach_i64`。

### [中] 100-15｜覆盖缺口：全目录无 break 探针
- 位置：目录级（`Test/SyntaxAudit/probes/` 全量 135 个文件名；`continue` 有 `cc1-cc5`、`p08_continue`，无任何 `break` 命名）
- 类别：覆盖缺口
- 触发条件：`break` 实现出错（不跳出、只跳一层、在 `switch`/`for` 中行为不一致）。
- 后果：循环/开关的跳出语义无任何探针检测；`f_inf.krt` 恰因缺少 `break` 而 hang，更凸显该缺口。
- 最小修复：新增 `break.krt`（while/for 中 `break`，含嵌套循环只跳一层）。
- 备注：`p` 系列覆盖 `continue` 但未覆盖 `break`。

### [低] 100-16｜覆盖缺口：全目录无 do-while 探针
- 位置：目录级（135 个文件名中无 `do`/`do_while` 命名）
- 类别：覆盖缺口
- 触发条件：`do-while` 的"至少执行一次"语义或条件位置实现出错。
- 后果：无探针检测。
- 最小修复：新增 `do_while.krt`，覆盖零次与多次执行。
- 备注：`while` 已由 `cc1-cc5`/`p02` 覆盖。

### [低] 100-17｜子集/等价型冗余探针
- 位置：`Test/SyntaxAudit/probes/a3.krt:4-9`（⊂ a2）；`Test/SyntaxAudit/probes/b3.krt:4-6`（⊂ b2）；`Test/SyntaxAudit/probes/b4.krt:4-7`（⊂ b1）；`Test/SyntaxAudit/probes/c1.krt:4-5`（⊂ b2）；`Test/SyntaxAudit/probes/c2.krt:4-7`（≡ b3）；`Test/SyntaxAudit/probes/e_B.krt:6-9`（输出与 e_A 等价）
- 类别：重复探针（子集冗余）
- 触发条件：—
- 后果：覆盖同一语法节点/同一输出，稀释用例密度；`a3` 与 `a2` 的 `!=`、float 断言完全重叠。
- 最小修复：删除或改写为未覆盖变体（如 `a3` 改测 `0x` 边界、`b3/b4` 改测链式短路）。
- 备注：`c2.krt` 与 `b3.krt` 仅多一个中间变量 `q`，语义等价。

### [低] 100-18｜无显式断言/期望值的探针
- 位置：`Test/SyntaxAudit/probes/bsl2.krt:5`（恒输出 `X`，不校验 `c`）；`Test/SyntaxAudit/probes/cc1.krt:8`、`Test/SyntaxAudit/probes/cc2.krt:8`、`Test/SyntaxAudit/probes/cc3.krt:7-8`、`Test/SyntaxAudit/probes/cc4.krt:5`、`Test/SyntaxAudit/probes/cc5.krt:8`（仅打印 `N`/`S`/变长串，无期望值标记）
- 类别：静默通过/无断言
- 触发条件：循环/continue 语义被改错但输出字符仍相似。
- 后果：这些探针的"期望输出"未在文件内固化，审计只能靠外部基线；`cc3.krt` 用 `if` 打印 `S` 与 `cc2` 的 `continue` 形成对照，但无断言标记，容易误判为通过。
- 最小修复：为每个探针补一个由被测值决定的标记（如用 `D()` 输出计数值或断言 `S` 出现次数）。
- 备注：与 100-4 同类，但严重度较低（输出部分依赖行为）。
