# SelfHost 编译器

这里存放真正自举编译器的 Kairote 实现，不生成 C 源码。当前已完成 M0，以及 M1/M2 的可执行最小切片：

- `Frontend/Lexer/` 定义借用源缓冲区的 Token、位置和错误码，并实现注释、标识符、整数、字符串和标点扫描。
- `Frontend/Parser/` 定义只消费 Token 的 Parser 接口。
- Parser 已能跳过顶层 `enum`/`class` 声明并解析最多 256 个函数节点；类成员语义和多函数后端链接仍待实现。
- Parser/Semantic 已能构建并验证嵌套 `if/else` block；IR 已支持编译期常量条件的单返回分支折叠，动态条件的控制流 IR 和机器码分支仍待实现。
- Parser/Semantic 也能保留 `while` 和 `else if` 的嵌套结构；循环降低与动态分支发射仍待实现。
- Parser/Semantic 已能表示对象字段访问、赋值语句和表达式语句；字段布局、存储 IR 与对应机器码仍待实现。
- Parser/Semantic 已能识别通用类型局部声明和 `new Type()` 初始化；类型检查、对象布局和分配运行时仍待实现。
- Parser/Semantic 也能表示 `new Type[count]` 数组构造及其长度表达式；数组布局和运行时分配仍待实现。
- Parser 可承载最多 8 个 `int32` 参数和逗号分隔的调用实参；Semantic 会逐项绑定并检查参数数量，后端当前仍只实现单参数 ABI。模块解析会先为所有函数建立局部/参数绑定，再解析跨函数调用。
- Parser 可识别 `ref` 参数修饰符和 `ref value` 实参节点；引用别名/写回语义及其 ABI 仍待实现。
- 函数参数类型现在接受任意标识符类型及可选指针修饰，覆盖 `KrtAstNode` 等自定义类型；完整类型检查仍待实现。
- 函数返回类型同样接受任意标识符类型，覆盖 AST/符号对象返回；返回 ABI 和类型一致性检查仍待实现。
- Parser/Semantic 已能表示 `byte*` 声明和 `stackalloc Type[count]`；指针类型检查、生命周期和栈分配 IR 仍待实现。
- Parser/Semantic 已能表示下标访问（`p[i]`）以及一元解引用/取址节点；地址计算、边界/别名分析和存储 IR 仍待实现。
- Parser/Semantic 已能表示 `(Type)value` 与 `(Type*)value` 转换节点；转换的类型检查和机器码语义仍待实现。
- Parser/Semantic 已能保留 `void` 函数中的空 `return;`；非 void 返回路径和完整返回类型检查仍待实现。
- Parser/Semantic 已能表示和递归检查 `unsafe(using krt.mem;) { ... }` 块；安全边界和后端内存模型仍待实现。
- Parser/Semantic 已能保留 `delete value;` 生命周期语句；析构语义、所有权检查和后端释放路径仍待实现。
- Parser/Semantic 已能表示循环中的 `break;` 与 `continue;`；循环 CFG 降低和跳转机器码仍待实现。
- 已有契约覆盖 `void` + `unsafe` + `while(true)` + `break` + 空 `return` 的组合路径。
- Semantic 会递归验证 `break/continue` 只能出现在循环上下文中，非法控制转移不会进入 IR。
- Parser/Semantic/IR 已识别 `true`、`false` 布尔字面量并参与常量求值，避免循环和条件中的布尔名称被误报为未定义变量。
- Parser/Semantic/IR 已识别 `null` 字面量并按零值参与常量比较；指针类型检查和非整数空值表示仍待实现。
- Parser/Semantic/IR 已支持三元表达式 `condition ? when_true : when_false` 的 AST、绑定和常量求值；一般非恒定选择仍待降低。
- IR 现在提供受验证的 `StackAlloc`、`AddressOffset`、`Load`、`Store` 指令，并能降低常量下标的 `byte` 缓冲区读写；Kro 后端已能发射固定缓冲区读写切片并由 ArkLink 实际执行。
- 对局部整型常量赋值，IR 会追踪最新赋值并可折叠最终返回；对象字段写入和动态赋值仍未发射。
- `Frontend/Parser/Ast.krt` 定义带源码跨度的 AST 节点和叶节点验证。
- `Frontend/Semantic/` 按源码名称及声明顺序绑定函数体局部变量，分配独立槽位，拒绝重复声明、未声明引用和缺失返回。
- Semantic 会对整个函数链执行名称唯一性检查，重复函数名在进入后端前被拒绝。
- Semantic 会对任意长度函数链执行确定性排序，将 `main` 移到链尾；多函数后端/重定位仍在实现中。
- Semantic 会在模块级解析调用目标，检查被调用函数存在且实参与形参数量一致；未解析调用不会进入 IR。
- 成员调用（例如 `token.SetError(...)`）会保留接收者绑定并进入后续阶段；类方法分派和 ABI 仍待实现，裸函数调用继续执行严格符号检查。
- Semantic 会递归绑定 `if/while` 嵌套块中的参数、局部变量和赋值目标；控制流本身仍需降低到多块 IR。
- 函数表达式检查已覆盖转换、成员访问和下标初始化节点，避免已解析的合法 AST 在白名单阶段被误拒。
- Semantic 要求函数链中恰好存在一个 `main` 入口；缺失入口的模块不会进入后端。
- 多函数单元在语义阶段规范化为 helper-first，因此源文件中的 `main`/helper 声明顺序不影响调用发射。
- `Middle/Ir/` 使用扁平指令表和显式基本块起点；验证器支持最多 32 块，检查块尾终结、跳转目标、返回/条件类型及部分值引用。单块内 `return` 后的指令被拒绝；完整 SSA 支配关系验证仍待实现。整数一元/二元常量表达式会在降低阶段求值。
- IR 会对已绑定的局部整数字面量执行确定性的常量传播，例如 `x + 2`；常量求值覆盖算术、比较和短路逻辑的值语义，无法证明为常量的表达式仍保留局部加载路径。
- `Backend/Kro/` 生成真实 x86-64 指令和完整的最小 Kro 文件（头、文本、符号表、字符串表），并支持通过 ArkLink ELF 链接执行；当前同时覆盖常量返回（文件驱动入口会补进程退出序列）、单/双局部槽位、无参 helper→main，以及单/双整数参数 helper→main 调用切片。
- `Driver/` 串起 Lexer → Parser → Semantic → IR → Kro，并提供源码到文件的编译入口。
- Driver 会在任意长度函数链中显式查找唯一 `main`，不再假设入口位于链首；多函数后端发射仍待实现。

这些模块目前由 Stage 0 的 `KrtC` 编译进行接口验证；它们还不是完整编译器，也没有宣称已经完成自举。当前 Driver 支持无参数函数、单个 `int32` 参数、`int32` 局部声明（常量初始化）、常量表达式返回、直接局部变量返回，以及无参/单整数参数 helper 调用的直接机器码验证。IR 的 StoreLocal 使用 `lefts` 保存值指令索引、`rights` 保存槽位，LoadLocal 使用 `lefts` 保存槽位；一般调用、控制流和完整 ABI 仍在实现中。

`Main.krt` 是可重复的 Stage1 探针：Stage 0 编译全部 `SelfHost/*.krt` 后，Stage1 会直接写出一个 Kro 调用对象；`Test/SelfHost/test_contract.py` 再用 `ArkLink --target elf` 链接并实际运行它。这个探针验证了 Stage0 → Kairote 编译器 → Kro → ArkLink → ELF 的链路，但不等同于完整的 Stage1→Stage2 自编译。

文件驱动入口使用 256 KiB 分块读取上限；超过上限会明确失败，不会静默截断源文件。

仍需实现完整作用域和类型检查、一般表达式、控制流降低、多参数/多函数调用、重定位写出，以及 Stage1 → Stage2 → Stage3 自编译验证。现有常量求值也尚未完整覆盖运算符及整数边界；多块 IR 可进行结构验证，但当前机器码后端明确拒绝它。后续实现必须保持 Frontend → Semantic → IR → Kro 的依赖方向，并直接输出 Kro 对象。
