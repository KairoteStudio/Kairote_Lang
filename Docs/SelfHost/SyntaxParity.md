# 语法一致性验收

目标仍是覆盖原版实际接受的全部语法。本文件记录验收边界，不表示已经完成。

## 验收规则

- 原版依据是 `Re.KrtC/src/compiler/Frontend/Parser/` 的可达解析入口和实测结果；Token/AST 枚举本身不是支持证据。
- SelfHost 必须保存声明、类型、参数、子表达式及源码位置；跳过 Token 或平衡大括号不算完成解析。
- 正例必须检查 AST 内容及 EOF；负例必须拒绝并在有限时间内结束。
- 自编译必须另行验证完整源码生成下一代产物。Stage1 探针通过不能证明自编译成功。

## 当前已知缺口

- `SkipTypeDeclaration`、`SkipSourceDirective`、`SkipGlobalConstant`、`SkipDelegateOrTemplate` 会丢弃声明；需要替换为声明 AST 构建。
- 泛型、限定名和数组类型的 skip helper 尚未建立完整结构类型。
- 表达式分组和类型转换存在歧义；三元表达式与列表复用 `next`，可能丢失子树。
- 函数参数仍有固定容量，修饰符、外部声明和类型信息保留不完整。
- `fixed`、异常处理、循环增量等入口仍需与原版逐项差分测试。
- 当前基础回归不覆盖全部原版语法；不得用通过数量宣称语法一致。

## 可运行证据

`python3 -m unittest Test.SelfHost.test_parser Test.SelfHost.test_contract Test.SelfHost.test_lexer`

Parser 专项目前检查函数关键字及跨度、循环体不被兄弟语句覆盖、lambda 参数保留、类型元表达式、空值运算符 Token 与结合方向。这些检查只证明各自断言，不替代全量一致性验收。
