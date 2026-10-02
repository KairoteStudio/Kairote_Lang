# 可达前端与执行差分审计

本次审计以 Re.KrtC 的实际 Parser 入口、生成的 AST 和程序行为为依据。2026-10-01 的快照包含 72 个最小程序；原生工具来自 `build/selfhost-restoration-native/native/program`。这是一份差分证据，不是完整语言一致性结论。

## 证据与复现

`build/selfhost-frontend-audit/report.json` 保存源程序、预期退出码、工具 SHA-256、源码哈希、编译/链接/执行参数及结果。工具先复制到 `tool-seed`、`tool-native` 和 `tool-linker`，避免运行期间的重新构建混入同一次审计。原生编译、独立 ArkLink 链接和执行均使用空 PATH。

`seed-parser-dump.c` 用当前种子 Parser 对象构建一个只读 AST 查看工具。构建命令、工具与 Parser 对象哈希在 `seed-parser-dump-build.json`；每个程序的 AST 在 `cases/<name>/seed-parser.txt`。它帮助区分真正解析出来的语法和错误恢复时丢失的 Token。

```sh
python build/selfhost-frontend-audit/run-audit.py

build/selfhost-frontend-audit/tool-seed -O0 \
  build/selfhost-frontend-audit/cases/function_style_integer_cast/program.krt \
  output /tmp/seed-cast

PATH='' build/selfhost-frontend-audit/tool-native -O0 \
  build/selfhost-frontend-audit/cases/function_style_integer_cast/program.krt \
  -c -o /tmp/native-cast.kro
```

审计脚本和 AST 查看工具仅生成证据，不参与应用编译入口，也没有新增要求通过的测试门槛。JSON 的工具哈希用于标识被测版本；后续修复应重跑对应程序，不能将这里的旧快照失败当作当前失败。

## Parser 的实际来源

Re.KrtC 的 `Ast.h` 枚举了 106 种节点。对 Parser 入口文件的节点创建调用和直接种类赋值扫描找到 51 种，另 55 种没有这些 Parser 创建来源。地址/解引用的动态种类选择也计入。源码引用逐项保存在 JSON 的 `seed_ast_inventory.creator_references`。

这个数量不是支持数量：某些语法用字段而非独立节点表示，部分节点只供转换阶段使用，创建节点也不能证明绑定和运行语义正确。

| Re 的可达来源 | SelfHost 的 AST、绑定和生成 | 本轮观察 |
| --- | --- | --- |
| Program、普通/静态函数、普通/静态变量 | Program、Function、Variable，修饰符与静态状态分别保留；NativeBinding 与原生 IR 消费这些节点 | 已有自举及函数/全局量执行证据；参数上限另有 128 边界测试 |
| 数字、字符串、字符、布尔、null、Identifier、this | 数字/浮点及字符标记、String、Boolean、Null、Identifier；类型绑定与真实数值生成 | 二进制和字符转义正例正确；种子也会接受某些错误数字，不能当成正确行为 |
| Binary、Unary、Ternary、Call、Member | 对应真实子节点与优先级，名称/参数绑定，直接及间接调用 | 额外发现内置函数式转换和命名参数语法差距，见下表 |
| 地址、解引用、指针/数组赋值及复合赋值 | Unary 的地址/解引用和 Assign/CompoundAssign；类型、地址及存储宽度进入生成 | 原生已有地址只求值一次、指针层数及存储宽度测试 |
| Block、if/while/do/for/foreach、return、break/continue | 对应控制流节点，独立 body/alternate，作用域及跳转检查 | 普通控制流已有执行证据；非法节点恢复后保留后续 AST |
| ArrayLiteral、ArrayAccess、New、NewArray、StackAlloc | 字面量、Index、New、StackAlloc；元素类型、分配和内存操作进入原生生成 | `stackalloc` 的完整元素类型是本轮真实遗漏 |
| Namespace、UsingDirective、Class，enum 转成带标记的 Class | 源码顺序 namespace/using 节点、类型声明与 typed using frame | 普通 class 可达；`struct` 关键字没有对应的种子 Parser 分支 |
| Print、Unsafe、Point | Print、Unsafe、Point，保留权限名称和块，实际绑定与生成 | Point 正例两者均为 42；这些与只有枚举的语法有区别 |
| Switch、Case；default 存为专用 body 字段 | Switch/Case/Default，标签和 case body 保留 | 原生多个标签、default、贯穿测试正确；种子部分场景虽生成 AST，执行错误 |
| Sizeof、Cast、Is、GenericType | Sizeof/Cast/TypeTest 及类型子树 | 数字 sizeof 正确；GenericType 节点存在本身不代表独立元类型值语义 |
| Lambda | Lambda 的参数与表达式确实保留；原生未实现闭包绑定/生成 | 种子可达，但生成表达式值为零，调用最小程序无法正确链接/执行 |

主要来源是 `ParserStatement.c`、`ParserExpression.c`、`ParserAdvanced.c`、`PointerTypes.inc` 和 `EnumParser.inc`。SelfHost 对应入口在 `Frontend/Parser/`，绑定在 `Frontend/Semantic/NativeBinding.krt`，生成在 `Middle/Ir/Ir.krt` 及专用模块。

## 被测快照的真实遗漏

| 最小程序的关键部分 | 种子实际结果 | 原生快照结果 | 原因 |
| --- | --- | --- | --- |
| `return int8(258);` | 2 | E_UNDEFINED_METHOD | 种子 `ParserExpression.c` 的内置类型调用生成 Cast；原生生成普通 Call |
| `return int32(float32(42.75));` | 42 | E_UNDEFINED_METHOD | 同上；嵌套转换也正确 |
| `int32 r=int32(n++);`，检查 r=41、n=42 | 42 | E_UNDEFINED_METHOD | 确认转换操作数需要保留一次求值的副作用 |
| `int32** p=stackalloc int32*[1];`，写入地址并解引用 | 42 | E_PARSE | 种子 StackAlloc 调用完整 `KrtParseSourceType`；原生旧入口只读一个类型 Token |
| `pack(a:4,b:2)`，函数返回 `a*10+b` | 42 | E_PARSE | 种子 ParseCall 保存 argument_names；原生旧入口没有冒号参数节点 |

命名参数的重排对照 `pack(b:2,a:4)` 在种子中返回 **24**，预期是 42。种子保留了名称，但当前绑定和生成没有重排参数。这里确认的是保留名称并按源码顺序调用的可达语法，不能据此声称种子已有正确的命名参数重排语义。

### 对这些快照遗漏的修复

后续 Parser 已将内置函数式调用解析为带类型子树的 Cast，stackalloc 使用完整 ParseType 并继续解析后缀，命名参数在独立标签字段中保存名称。普通调用、new 和 base 构造共享参数入口，参数兄弟链保持源码顺序。`Test/SelfHost/test_parser.py` 新增真实 AST 断言覆盖这三组语法、操作数结构、ref 标签、成员名跨度及损坏参数的恢复；另执行上述五个种子正例，证明它们是当前种子真正能执行的入口。命名参数的绑定、重排及副作用执行由对应语义/生成测试验证；上表仍记录审计工具哈希所对应的旧快照。

## 只有 Token/枚举或语法被丢失的项目

| 项目 | 种子来源与实测 | 判断 |
| --- | --- | --- |
| struct、值复制、值参数/返回 | Tokenizer 有 struct，但 Parser 无 TOKEN_STRUCT 分支。AST 查看中 `struct Pair{...}` 变成顶层 Block 和字段，没有类型声明；复制/参数/返回对照也不正确 | `struct_default_local` 的退出码 18 和双变量例的 7 不证明结构体语义；它们来自残留普通 AST。原生结构体语义仍是单独待实现能力 |
| 非捕获 lambda、捕获 lambda | `ParserExpression.c:215` 生成 Lambda；`IrGen.c:1788` 建立函数后返回零值，未构造可调用值或捕获环境。常量 lambda 和有参/capture 调用均失败 | 可达解析，缺少实际可调用语义；不能把种子 AST 或函数生成当作闭包支持 |
| lock | 无 Parser 创建 Lock 的来源；AST 查看仅保留 body Block，selector 消失。selector 副作用例返回 7，预期 8 | 普通 body 恰好返回 7 不代表互斥或 selector 求值语义 |
| yield return / yield break | 无 Parser 创建 Yield 来源。`yield return 17;return 42;` 留下普通 Return，实际返回 17；yield break 后仍执行 return 42 | `yield` Token 被丢失，既不是生成器，也没有正确的 yield break 控制流 |
| await、async | 有 Token/AST 枚举，没有对应 Parser 创建入口。await 最小值例拒绝；await 调用例返回错误值；async 函数拒绝 | 不能据此要求复制不存在的异步状态机。原生接受 async 修饰符也不等于实现异步语义 |
| fixed | Token/AST 枚举和 IR 分支存在，但没有 Parser 创建入口；固定指针声明例拒绝 | 不属于已正确执行的种子 Parser 功能 |
| LINQ、property/get/set、delegate、operator overload、插值字符串、tuple/match/pattern | 枚举或 Token 存在，Parser 创建入口缺失；本轮相关最小程序拒绝 | 不能用跳过语法或生成占位节点宣称恢复成功 |
| try/catch/finally/throw、default(T)、as、null operators | 部分头文件/枚举/IR 分支存在，但当前种子 Parser 没有对应创建入口；相关程序拒绝或执行错误 | SelfHost 的异常/default/type-test等已有独立实现证据；这不是缺失种子功能的证据 |
| nameof、参数默认值、params 参数、析构 | nameof 未形成真实功能；参数默认值数组在 Parser 中置 null；本轮最小程序拒绝 | 未建立种子正确执行证据 |

`Parser.h` 中声明了 try/lock/yield/template 等函数，但当前 Parser 实现中没有这些函数定义或可达调用；AstVisit/AstClone/IrGen 中的分支也不能补足解析入口。此表只判断当前 Re.KrtC 基线，不取消这些语言功能作为后续实现目标。

## 限制

本轮不覆盖全部合法程序、完整类型系统和并发/生成器协议。51 种可达创建来源不意味着每种节点的全部组合都已验证。访问控制的负例中，种子和原生快照都接受了本应拒绝的 private 成员访问；这些是待完成规则，而非已达成的一致性。后续自举、最新执行回归及标准库结果应与工具哈希一起阅读。
