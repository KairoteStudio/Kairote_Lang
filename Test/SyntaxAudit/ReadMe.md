本文引用的原始结果、生成源码副本、汇编、IR 快照和日志均在本地归档，不随 PR 提交。

Re.KrtC 的语法审计与枚举、泛型、命名空间、`ref` 专项回归。

在仓库根目录运行：

```sh
bash Test/SyntaxAudit/run.sh
python3 Test/SyntaxAudit/test_language_regressions.py
```

2026-09-12 验证：现有语法审计 **75/75 通过**。原来的 C67_enum、C69_generic、C73_namespace、C75_refparam 均已修复；C69 的辅助输出函数补齐了数字 5，期望输出保持为 `5`。

新增 [test_language_regressions.py](test_language_regressions.py) 包含 **25 个测试方法、65 次编译**：36 次成功并运行通过、29 次预期拒绝，无编译器崩溃。

| 特性 | 回归范围 |
|---|---|
| 枚举 | 自动递增、显式负数、位运算常量、既有成员引用、int32 边界、传参与 switch；重复成员、溢出、除零、未知成员、修改常量均拒绝 |
| 泛型类 | 类型替换、多个类型参数、独立实例、嵌套实例、宽整数与浮点字段、构造函数、类型推断、参数与返回类型、300 字段对象布局；参数数量、未知类型和实例类型不符均拒绝 |
| 命名空间块 | 完整限定路径、点分与嵌套声明、重复打开、内部非限定调用、两个空间中的同名类、实例方法、前向类型引用 |
| ref | 显式与省略调用修饰符、共享别名、作用域遮蔽、转发、递归、全局变量、数组与单次索引求值、窄/宽整数、浮点、指针与可空槽、回调、实例及静态方法、超过六个参数 |
| 跨文件 | 三个文件的引用传递在全部六种输入顺序下运行；类型不符、值参数使用 ref、非左值、数组整体传入标量引用均拒绝 |

记录编译器散列、编译/运行次数与结果：

```sh
python3 Test/Pointers/RunTests.py --suite Test/SyntaxAudit --output Test/SyntaxAudit/LanguageResults.json
python3 Test/Pointers/BuildSanitized.py
KRTC=/tmp/KrtC-pointer-sanitized ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 python3 Test/Pointers/RunTests.py --suite Test/SyntaxAudit --output Test/SyntaxAudit/SanitizerLanguageResults.json
KRTC=/tmp/KrtC-pointer-sanitized ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 bash Test/SyntaxAudit/run.sh
```

`LanguageResults.json` 与 `SanitizerLanguageResults.json` 保存原始结果。ASan/UBSan 检查编译器本体，未对生成的机器码插桩，泄漏检查沿用指针套件配置禁用。审计脚本遇到任一失败返回非零状态。

当前泛型实现针对编译单元内的类实例化；泛型函数、完整约束系统及跨编译单元模板导出不属于这些通过结果。命名空间块的验证也不代表已完成所有 using/别名规则。指针有效性、生命周期与权限边界见 [Pointers/ReadMe.md](../Pointers/ReadMe.md)。
