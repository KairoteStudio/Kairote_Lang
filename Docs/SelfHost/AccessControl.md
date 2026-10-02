# 原生访问控制契约

本页说明本轮 SelfHost 的访问规则和可执行测试。Re.KrtC 当前接受多种不合法的 private 访问，因此种子编译器不是这些规则的正确性基准；测试直接调用原生编译器，Python 只生成输入并检查结果。

## 可见性

| 声明 | 可访问范围 |
| --- | --- |
| 未标注访问修饰符 | 保持 public，兼容现有 Kairote 程序；不采用成员默认 private 的规则 |
| public | 可通过正常名称、类型、调用和导入规则访问 |
| private 成员 | 仅声明类；允许访问同类的其他实例。泛型实例按同一个原始泛型声明判断 owner，不能因 clone 丢失 private |
| protected 成员 | 声明类及派生类；派生 caller 使用普通实例 receiver 时，receiver 的静态类型必须是 caller 或其派生类。this/base 和合法静态访问使用各自规则 |
| internal | 同一次编译合并的 KRO assembly；显式 roots、quoted import 和 using 加载的源码属于同一 assembly |
| protected internal | 派生访问与同 assembly 访问取 OR |
| private protected | 派生访问与同 assembly 访问取 AND，并保留 protected receiver 限制 |
| 顶层 private 类型、函数、变量 | 原始源文件内可见；文件合并和导入不会将它变成全局 public |

顶层 protected 和 private protected 不合法。成员访问检查适用于读、写、自增、复合赋值、ref、实例/静态调用及函数取址。private 字段隐藏基类字段时，不可通过忽略派生字段来绕过检查；显式的基类静态视图仍访问基类自身的公开字段。

internal 的边界是一次源码编译，不是目录或 namespace。跨 KRO 的 public/default 符号可按已有外部 ABI 链接；private/internal 符号不导出，不能通过 extern 声明绕过。当前这不代表跨 KRO 的完整类 ABI 已实现。

不同原始文件可以在同一 namespace 各自声明同名 private 函数、变量、常量和类型；每个文件内的使用绑定到自己的声明。另一文件的同名 public 声明也可以共存，同文件 private 优先。同一文件的重复 private 声明仍拒绝，跨文件 extern 原型不能绑定到不可见的 private 定义。

extern 没有可链接的公开定义时报告 E_LINK；另一 KRO 的同名 public 定义仍可满足 extern，同时本次编译中的 file-private helper 继续绑定到自己的 private 定义。

## 构造、泛型与派发

直接 new 和 base 构造链检查所选构造函数的可见性。同 owner 的 private 构造工厂允许建立对象，外部直接 new 则拒绝。new() 泛型约束要求 public/default public 的无参构造；存在一个 private、protected 或 internal 无参构造并不满足约束。

override 必须保持相同的有效访问级别；default 与 public 等价。protected 改为 public 也拒绝，protected internal/private protected 的组合分别匹配。接口实现的方法必须 public/default public。

重载选择只使用可访问候选；一个 private 的更精确匹配不能掩盖仍然匹配的 public 重载。取函数地址同样检查访问，之后的间接调用不能绕过它。

访问检查保留类型来源，包括泛型实参、数组元素、函数指针签名、返回值和 var 推断。private 类型经公开工厂返回，并不允许另一原始文件通过 inferred 值访问它。本轮不以 public signature 暴露 private 类型的声明一致性规则作为验收要求；使用点仍必须检查访问。

调用参数为 null 不会消除形式参数类型的访问要求。另一文件调用公开函数时，若所选签名的参数包含不可访问的 private 类型，仍报告 E_ACCESS。

## 诊断与产物

不可访问的声明报告 E_ACCESS；访问修饰符重复、冲突、非法位置，以及派发契约中不合法的可见性报告 E_MODIFIER。new() 约束不满足报告 E_GENERIC_CONSTRAINT；跨 KRO 的未导出符号在链接阶段报告 E_LINK。

语义诊断指向原始文件的实际位置。不同函数中的独立错误分别报告。编译或链接失败时保留既有目标产物。native 和 VM 使用同一绑定规则，不能以选择不同后端绕过访问控制。

## 真实执行测试

`Test/SelfHost/test_access_control.py` 使用原始源码文件及空 PATH 调用 native CLI，覆盖上述正负例、源码导入、泛型 clone、private helper 的库内调用和 native/VM 一致性。它不调用种子编译器验证访问控制，也不将本应拒绝的程序标为跳过。

```sh
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" \
ARKLINK="$PWD/build/ArkLink/ArkLink" \
python -m unittest Test.SelfHost.test_access_control -v
```

测试定义是契约；实现是否通过应读取与被测编译器/源码哈希对应的实际运行日志。本页不把仅保留修饰符的 AST 当作已经执行的访问检查。readonly 的写入和引用规则有独立语义，不属于本轮访问规则的替代判断。
