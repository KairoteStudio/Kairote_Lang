# 原生 Parser 的结构与验证

Parser、前瞻、AST、using 作用域和错误恢复均由 Kairote 实现。Python 只运行测试和记录执行结果。

## 前瞻与类型 AST

`Lookahead.krt` 的 `KrtParserFork` 为探针创建独立 lexer/token，复制当前位置、行列和 token 字段。探针实际构造类型 AST；失败不会改变主 parser。`KrtParserCommit` 只提交已识别的语法，主 parser 不再保存并回退游标来猜测声明，也不按名称首字母大小写判断类型。

同一类型语法用于函数返回值、参数、局部/全局声明、构造、类型元表达式、转换和泛型实参。类型节点保留原始跨度、限定名称组件、每个组件的泛型子节点、函数指针签名、指针层数、数组层数和可空标记。注释由 lexer 处理，不需要重新扫描原始字符猜测右括号。`>>` 和 `**` 在类型或解引用语法中逐字符消费，lexer 仍返回正确的完整运算符 token。

继承列表和类型约束保留各自的包装节点，其 `syntax_type` 指向完整类型子树；包装链的 `next` 不会破坏泛型实参链。声明的源码顺序包装引用同一实际声明，因此可以访问这些类型子树。namespace 和 using frame 传播也遍历类型、参数、约束、权限名称和数组尺寸子节点。

普通名称与转换之间的歧义保留表达式语法的边界。例如 `(value)+1` 解析为括号表达式；明确的内置类型、类型后缀或接续操作数构成转换。分组表达式继续解析索引、成员及调用后缀。类型解析最多递归 64 层，指针最多 64 层。

内置类型的函数式转换，例如 `int8(258)` 和 `int32(float32(value))`，生成带完整 `syntax_type` 的 Cast，操作数只保留一个子节点。类型探针区分转换表达式与局部声明。`stackalloc` 同样解析完整元素类型，保留限定名、泛型、指针、数组和可空信息，并继续解析索引等后缀。

调用、`new` 构造及 `base(...)` 的参数链保持源码顺序。命名参数用每个参数自身的 `syntax_argument_name_start/length` 保存标签，不覆盖 Member 的成员名跨度；`ref name: value` 和 `name: ref value` 均保留引用节点。绑定使用独立的 `native_argument_index/native_argument_slot`，初始均为 -1，参数匹配与重排不需要破坏源码求值顺序。

普通函数和函数指针签名最多 128 个参数。实例方法的 128 个 ABI 参数包含隐式 this，因此最多有 127 个显式参数。跨度/ref 数组包含隐式 receiver，`syntax_parameter_types` 保留源码显式参数链，泛型推断据此偏移一个位置。129 参数会报告诊断并恢复后续声明；这些边界有真实 AST 执行断言。

`Ast.krt` 同时保留用于绑定的函数/类型/全局声明列表和用于语法检查的 `syntax_declarations`。后者按源码顺序保存引用包装节点，包括 namespace 和 using；包装节点不复用语义列表的兄弟链接。循环体、分支、异常子节点、泛型和类型信息仍是实际 AST 子节点。

## using 作用域

`KrtUsingScope` 保存父 frame、当前 frame 的 imports 和所属 namespace。namespace 与函数块创建自己的 frame，退出块时恢复父 frame；源码文件边界 `namespace;` 创建新的根 frame。using 列表只写入当前 frame，语义查询沿父链查找。namespace AST 和声明/表达式保留 frame 引用；旧的 `scope.body` 导入复制已删除。

## 错误恢复

`ParserBase.krt` 保存 `KrtParserDiagnostic` 列表：位置、实际 token、期望 token、说明及下一条诊断。相同位置的连续错误合并，最多记录 64 条。错误数量达到上限后仍继续恢复，不会陷入无进展循环。

`ErrorRecovery.krt` 根据分号、闭括号/大括号和后续声明或语句的语法起点恢复。块、switch case body、类型成员和顶层声明继续解析后续代码；case/default 是 switch 恢复边界。已经出现错误的块遇到后续函数声明时报告缺失右大括号，将声明留给外层解析。错误语句以 `Invalid` 节点保留，能够保留已经构建的部分子节点。诊断存在时编译器拒绝生成新产物。

## 已验证的恢复和语法

`Test/SelfHost/test_parser.py` 在实际可执行程序中遍历 parser 生成的 AST，覆盖以下行为：

- 嵌套泛型、限定类型、注释、指针、转换和分组后缀；包括编译器自身的 `(int64)((byte*)this.source)[index]`。
- 继承/约束包装的类型跨度、限定泛型子节点、指针层数、数组层数、各层可空标记及 namespace/using frame。
- namespace/block/file using frame 的继承、退出、隔离，以及源码顺序节点。
- 多个错误的有序诊断；错误字段、函数签名、局部及全局声明之后仍保留合法声明；100 个错误时限制为 64 条并保留后续函数。
- switch 的损坏语句之后保留后续语句及所有 case；嵌套类型/函数保留节点，缺失右大括号后恢复后续函数。
- Print、Point、类型检查 `is`、unsafe permission 名称和单语句循环/分支的实际子节点。
- `var name: Type`、`var Type name`、`let`、后置数组尺寸及声明修饰符的 AST 信息。
- 前置/后置 `++/--` 区分更新值与旧值，保留完整跨度；字符字面量保留字符标记与种子支持的转义值。
- 函数式内置转换的嵌套类型和唯一操作数，stackalloc 完整元素类型及后缀，命名/ref/构造/base 参数标签与成员名称的独立跨度；缺失命名参数值后保留后续语句。

分别执行种子生成和原生生成后的 parser 程序：

```sh
KRTC=build/Re.KrtC/KrtC python -m unittest Test.SelfHost.test_parser -v
SELFHOST_COMPILER=build/selfhost/stage2/program \
ARKLINK=build/ArkLink/ArkLink python -m unittest Test.SelfHost.test_parser -v
```

这些测试证明 parser 的结构和恢复行为。Print、类型检查、数组声明、访问控制等功能还需要对应的绑定和生成测试；AST 存在本身不代表运行语义完整。Re.KrtC 的 delegate/template/LINQ/property 等 AST 枚举或头文件 API 中，部分没有可达的解析实现，不能据此宣称支持，也不能用平衡括号跳过它们来伪装成功。
