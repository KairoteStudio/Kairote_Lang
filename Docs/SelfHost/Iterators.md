# 通用迭代器与惰性生成器

`foreach` 支持数组、字符串以及用户定义的迭代协议。协议成员通过普通名称查找、重载选择、访问检查、泛型实例化和接口分派绑定。

## 手写迭代器

集合需要无参实例方法 `GetEnumerator()`，返回类、结构体、接口或提供静态扩展协议的 `object`。枚举器需要无参 `bool MoveNext()`，以及 `Current` 字段或无参非 `void` 方法 `Current()`。接口使用方法形式。枚举器可提供无参 `void Dispose()`。

```krt
struct Cursor {
    int32 Current;
    int32 last;
    bool MoveNext() {
        Current++;
        return Current <= last;
    }
}
struct Range {
    int32 first;
    int32 last;
    Cursor GetEnumerator() {
        Cursor cursor = default(Cursor);
        cursor.Current = first - 1;
        cursor.last = last;
        return cursor;
    }
}
int32 main() {
    Range range = default(Range);
    range.first = 2;
    range.last = 6;
    int32 sum = 0;
    foreach (var item in range) { sum += item; }
    return sum == 20 ? 0 : 1;
}
```

集合表达式和 `GetEnumerator()` 各求值一次。结构体集合的可写左值保留正常实例方法的修改语义；只读接收者使用现有防御性复制。结构体枚举器存储完整的可变值，`MoveNext()` 的状态跨迭代保留。每次读取 `Current` 后，循环变量得到普通值复制，包括嵌套结构体、固定数组和宽整数。

类与接口支持继承、虚方法和接口分派。`GetEnumerator`、`MoveNext` 和 `Current` 也可以通过普通扩展调用语法绑定，`using` 作用域及接收者重载均参与查找；`ref` 扩展接收者要求可写左值。`object` 可使用同样的静态扩展协议。可选的 `Dispose` 使用实例成员。空集合引用和返回空枚举器的集合产生空遍历，与数组和字符串的现有规则一致。

有 `Dispose` 的遍历使用真实 `try` / `finally`。正常结束、`break`、`return` 和异常展开执行它；`continue` 保持当前枚举器。嵌套遍历和用户 `finally` 按普通异常展开顺序清理。缺失或不匹配的协议报告 `E_ITERATOR`；普通名称、访问、重载和类型错误保留相应诊断。

## 编写惰性生成器

标准协议位于 `System.Collections.Generic`：

```krt
using System.Collections.Generic;

IEnumerable<int32> Numbers(int32 start, int32 count) {
    for (int32 index = 0; index < count; index++) {
        yield return start + index;
    }
}
int32 main() {
    var numbers = Numbers(7, 3);
    int32 sum = 0;
    foreach (var number in numbers) { sum += number; }
    delete numbers;
    return sum == 24 ? 0 : 1;
}
```

生成器函数的声明结果必须是接口。该接口可以直接提供 `MoveNext()`、`Current()` 和 `Dispose()`，或提供返回此类接口的 `GetEnumerator()`。编译器检查结构契约，用户可以定义自己的接口名称，并通过泛型接口继承组合这些成员。标准 `IEnumerable<T>` 和 `IEnumerator<T>` 分别对应这两种形式。

调用 `IEnumerable<T>` 生成器创建保存参数的序列对象，不运行源函数体。每次 `GetEnumerator()` 创建独立的游标。第一次 `MoveNext()` 开始执行；`yield return` 保存一个结果并立即暂停；下一次 `MoveNext()` 从该位置继续。`yield break` 或执行到函数末尾结束。直接声明返回 `IEnumerator<T>` 时，函数直接创建游标。

生成器支持普通条件、所有循环形式、`switch`、嵌套遍历、可变局部、泛型、类实例方法和闭包。按值结构体参数保存完整副本，宽数值参数保存在游标自己的存储中。局部地址指向游标的持久存储，普通 `ref` 调用可修改它。循环中的捕获局部仍为每轮创建独立单元，逃逸回调通过现有闭包引用计数保留捕获单元。

`try` / `finally` 中的 `yield return` 暂停时保留清理义务。恢复时重新进入对应异常处理帧；结束、提前 `Dispose()` 或异常展开时执行各层 `finally`。内层 `finally` 抛异常仍执行外层 `finally`。`yield break` 可从 `catch` 终止生成器。

以下语法和生命周期边界在绑定期报告错误：

- 生成器使用普通 `return`；结束应写 `yield break`。
- 在 `catch`、`finally` 或带 `catch` 的 `try` 保护体中写 `yield return`。
- 在 `finally` 中写 `yield break`。
- 生成器的 `ref` 参数、借用结构体实例的 `this` 或 `stackalloc` 存储。
- 不符合声明元素类型的 `yield return`，或不完整的结果接口契约。

`yield` 必须写为 `yield return expression;` 或 `yield break;`。生成器不能以匿名函数形式声明。

## 状态与所有权

生成器游标的 `Current()` 仅在最近一次 `MoveNext()` 返回 `true` 后有效。首次移动前以及完成后调用它会抛出 `int32` 值 `-2147483601`。重入正在执行或清理的 `MoveNext()` / `Dispose()` 会抛出 `-2147483602`。完成后再次 `MoveNext()` 返回 `false`。

序列和游标遵循语言的手动所有权。`foreach` 调用游标的 `Dispose()`，生成器游标在其中执行清理并释放持久状态；序列对象仍需 `delete`。手动获得生成器游标时应调用一次 `Dispose()`，不能再删除或使用该游标及其别名。序列按值参数已复制到独立游标，因此获得游标后可释放序列。

通过接口返回的结构体枚举器具有独立装箱包装。`foreach` 拥有其隐藏的枚举器包装，在用户 `Dispose()` 之后释放它，清理抛异常时也会释放。用户结构体 `Dispose()` 负责自身资源，不能再次释放这个包装；遍历结束后其接口别名失效。手动获得装箱结构体枚举器时，调用 `Dispose()` 后还需要 `delete` 包装。普通类枚举器的对象释放仍由其 `Dispose()` 契约负责。

类、字符串、动态数组和回调参数保留普通引用语义。生成器不会递归释放它们，程序需要让这些引用及类方法的接收者覆盖实际遍历过程。生成器产出的对象或回调也由消费者管理；保存的逃逸回调可以在游标清理后继续使用，并需在使用结束后 `delete`。

## 原生实现与验证

协议绑定与降低分别位于 [Semantic/Iterators.krt](../../SelfHost/Frontend/Semantic/Iterators.krt) 和 [Ir/Iterators.krt](../../SelfHost/Middle/Ir/Iterators.krt)。惰性生成器分别位于 [Semantic/Generators.krt](../../SelfHost/Frontend/Semantic/Generators.krt) 和 [Ir/Generators.krt](../../SelfHost/Middle/Ir/Generators.krt)。编译和降低均由 Kairote 实现。

生成器保留一次绑定的源 AST 与 SSA 控制流。编译器将局部和聚合帧地址映射到堆上的持久激活记录，并建立各个 `yield` 的恢复边。现有 SSA、寄存器分配、异常指令和 Native / VM 后端处理这些指令；结果不预先收集到数组。生成对象使用普通运行时类型描述符及接口 ABI，身份由工厂声明和完整闭合签名确定。

```sh
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" \
ARKLINK="$PWD/build/ArkLink/ArkLink" \
python3 -m unittest Test.SelfHost.test_iterators Test.SelfHost.test_generators \
    Test.SelfHost.test_iterator_boxing Test.SelfHost.test_generator_modules -v
```

[协议测试](../../Test/SelfHost/test_iterators.py) 和 [生成器测试](../../Test/SelfHost/test_generators.py) 对同一份源程序分别进行 Native / VM 的 O0、O1、O2、O3 编译和执行。测试覆盖完整结构体复制、状态修改、继承与泛型、自定义继承序列及游标接口、控制流和清理、惰性分页查询、闭包与错误位置；错误输入同时检查重复诊断及已有输出保留。资源测试在 Native / VM 的 O0、O2 下执行 25,000 轮正常完成、提前退出、异常和逃逸闭包生命周期，并从独立运行进程读取内存增长。

[跨模块生成器测试](../../Test/SelfHost/test_generator_modules.py) 对 Native KRO ABI 在 O0、O1、O2、O3 下独立编译提供者、服务和消费者，遍历全部对象链接顺序；消费者只知道接口，覆盖结构体 `Current`、逃逸闭包、带结构体异常的清理、不同闭合泛型，以及删除原始提供者源文件后的 KRO 元数据导入。宽整数组合测试检查 `IEnumerator<uint128>.Current()` 的隐藏结果地址、完整值快照、闭合泛型身份与提前清理。VM 验证同一语言协议，不定义独立对象链接 ABI。

[迭代器装箱组合测试](../../Test/SelfHost/test_iterator_boxing.py) 在 Native / VM 的 O0、O1、O2、O3 下检查装箱结构体枚举器的可变状态及异常释放，并对数组、手写迭代器和生成器产出的完整结构体进行接口 / `object` 转换；另在 O0、O2 下执行 25,000 轮装箱游标生命周期，覆盖 `MoveNext`、`Current` 和 `Dispose` 的异常。
