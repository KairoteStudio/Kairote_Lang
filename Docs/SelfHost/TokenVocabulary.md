# Lexer Token 词表

`SelfHost/Frontend/Lexer/Token.krt` 与 Re.KrtC 的 `Tokenizer.h` 及 `IntegerWidths.def` 对齐：299 个不同数值种类，EOF 为 0，UNKNOWN 为 298。`KrtLexerNext` 实际返回细分类种类，不再把所有关键字和运算符归为同一个 token。

| 范围 | 实现 |
| --- | --- |
| 关键字和类型名称 | 原版关键字表及整数位宽，共 244 个拼写；包括控制流、声明、访问修饰符、泛型/约束和 `op_*` 名称。 |
| 整数类型 | `int2` 到 `int128`、`uint2` 到 `uint128` 的全部偶数位宽，128 个细分类种类。非规范拼写、奇数和越界宽度保留为普通标识符。 |
| 运算符和标点 | 原版 52 个拼写，按最长匹配返回完整种类；覆盖 `**`、`::`、`->`、`=>`、`<<=`、`>>=`、`??`、`?.` 等。 |
| 字面量 | 数值使用原版 Number 种类；字符串、字符有独立种类。浮点和整数同时通过 category 区别；原始跨度和行列保留。 |
| 结束和错误 | 独立 EOF、UNKNOWN 和错误字段；非法字符会消费输入，便于继续诊断和恢复。 |

`KrtToken.category` 提供语法形状分类：EOF、Identifier、Integer、String、Character、Punctuation、Invalid、Floating。关键字仍具有单词 category，但 `kind` 返回对应的关键字；使用者需要关键字意义时检查细分类种类。Parser、源码加载器和工程配置已经迁移，实际调用这些分类接口。

`KrtTokenIntegerBits`、`KrtTokenIsUnsigned` 和 `KrtTokenIntegerType` 提供位宽与种类映射。类型或解引用中的双星、嵌套泛型的右移 token 由 Parser 在对应语法中拆分，原始 Lexer 的最长匹配不改变。

测试 `Test/SelfHost/test_lexer_vocabulary.py` 独立读取原版 C 表，逐个校验全部词表的数值、种类、category 和跨度，并在种子与原生生成后的可执行程序中验证。测试还覆盖前缀/大小写近似名称、非法类型宽度、各类字面量、UNKNOWN 恢复和 EOF。

完整词法词表不等于完整类型系统或运行语义。例如 66–126 位整数 token、delegate/template 关键字、幂 token 等，仍需对应的解析、绑定及生成证据才能宣称语言功能完整。总体能力范围见 [SyntaxParity.md](SyntaxParity.md)。
