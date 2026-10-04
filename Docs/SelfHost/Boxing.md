# 装箱、拆箱与 object

`object` 保存通用对象引用。将数值、枚举或结构体转换到 `object` 时，编译器创建保留实际类型的独立装箱值；将实现接口的结构体转换到该接口时同样装箱。类和接口转换到 `object` 保留原对象引用。

`new object()` 创建独立对象，`default(object)` 为 `null`。`object` 满足泛型 `class` 和 `new()` 约束；`new object(value)` 没有对应构造函数。

```krt
interface ICounter {
    int32 Add(int32 amount);
}
struct Counter : ICounter {
    int32 value;
    public int32 Add(int32 amount) {
        value += amount;
        return value;
    }
}
int32 main() {
    Counter source = default(Counter);
    source.value = 7;
    ICounter boxed = source;
    int32 result = boxed.Add(5);
    Counter copy = (Counter)boxed;
    copy.value = 99;
    delete boxed;
    return result == 12 && source.value == 7 && copy.value == 99 ? 0 : 1;
}
```

## 值复制与实际类型

装箱复制结构体的完整内联布局，包括嵌套结构体、固定数组和宽整数。修改原值不影响箱内值。通过接口调用可变结构体的方法，会修改箱内副本；接口引用的别名观察同一份副本。

显式拆箱 `(Type)value` 检查实际类型，再生成独立的值副本。结构体拆箱结果有自己的存储，修改它不会写回原箱。数值拆箱要求精确的类型：装箱的 `int32` 不能直接拆为 `int64`，需要先拆为 `int32` 再进行普通数值转换。布尔、字符、不同精度或符号的整数、不同浮点类型，以及名称不同的枚举都保持各自的身份。闭合泛型的完整类型参数也参与身份。

`is Type name` 先进行运行时匹配，仅在匹配成功时生成对应的值。失败模式不会读取不兼容的结构体或宽整数载荷。类与接口的匹配遵循继承和接口实现关系；装箱结构体保留所实现的接口。

## 异常与空值

checked 拆箱失败抛出 `int32` 值 `-2147483647`，可使用普通 `try` / `catch(int32 error)` 处理；异常展开执行现有的 `finally`。空对象不匹配 `is` 模式，也不能拆为普通值类型。显式转换到允许空值的引用类型保留空引用。

## 引用与所有权

装箱值采用手动所有权。保存的装箱对象需要 `delete`，赋值和传参产生同一引用的别名；只删除一次，随后全部别名失效。覆盖对象字段或数组元素前，需要安排旧装箱值的释放。删除容器不会递归删除其字段或元素保存的对象。

字符串、动态数组及 `fn` 回调转换到 `object` 时保存带完整实际类型的引用包装，内部引用保持浅复制；删除包装不会释放其指向的字符串、数组或捕获回调。原引用的生命周期由程序管理，空引用转换仍为空对象。回调拆箱校验完整参数、结果和 `ref` 签名，并保留自由函数地址或捕获环境描述符。

`object` 的相等比较使用引用身份。同一个箱的别名相等；分别创建的值箱不因内容相同而相等。引用包装使用内部引用的身份，包装同一数组、字符串或回调的两个对象相等。普通字符串之间的比较仍使用现有字符串内容比较。

对象引用不能和数值直接比较，也不能使用 `<`、`>` 等排序运算。需要先检查实际类型并拆箱，再对取出的值进行数值比较。

装箱不能隐式完成 `ref` 类型转换。`ref object` 需要实际的可写 `object` 槽位，不能传入 `ref int32`。`readonly` 结构体输入可以按值装箱，箱内副本与原只读存储无关。

普通不安全指针不能通过装箱擦除其地址契约。包含已知栈借用的结构体保留借用信息，转换到 `object` 不能绕过闭包的 `E_CAPTURE_BORROW` 检查。

## 实现与验证

绑定与值复制分别由 [Semantic/Boxing.krt](../../SelfHost/Frontend/Semantic/Boxing.krt) 和 [Ir/Boxing.krt](../../SelfHost/Middle/Ir/Boxing.krt) 实现。运行时类型描述符与跨模块对象分派共用精确的类型身份；装箱不会使用模块内声明序号作为类型身份。

```sh
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" \
ARKLINK="$PWD/build/ArkLink/ArkLink" \
python3 -m unittest Test.SelfHost.test_boxing -v
```

[装箱测试](../../Test/SelfHost/test_boxing.py) 对同一份程序分别使用 Native、VM 和 O0–O3 编译执行，包含结构体请求/响应处理、接口状态修改、泛型、宽字段复制、模式与异常、只读和引用边界、容器以及手动释放循环；错误程序另检查重复诊断及已有输出保留。

[组合 HTTP 测试](../../Test/SelfHost/test_runtime_http.py) 使用真实本地 TCP 请求，将提供方私有结构体装箱为接口，经 `yield` 逐项返回，在消费方执行路由及请求/响应结构体传值。提供方、消费方和 HTTP 传输分别编译为 KRO；提供方源码删除后再编译消费方。相同服务也编译到 VM 执行。测试检查中文响应、404、POST 405、分段请求、私有异常到 500 的转换，以及路由资源、生成器和套接字的释放。

`build/selfhost-boxing/formal-candidate10/report.json` 记录候选 10 的完整装箱验收：26 份成功程序在 Native/VM × O0–O3 下执行 208 次；13 份错误程序重复编译 416 次，检查准确诊断和输出保留。共 27 个测试方法全部通过，无跳过，源码、测试、编译器和链接器哈希在验收前后保持一致。报告保存每次实际命令、输出和产物哈希；编译器 SHA256 为 `93edb61adb3c723807a57b0cb90520c2d3074055f0b149c6907b0168b9c92c61`。

同一候选的 `vm-runtime.tests.log` 与 `runtime-http.tests.log` 位于 `build/selfhost-types/candidate10/`，分别记录 5 项 VM 格式测试和 12 个实际服务进程的 96 次 HTTP 请求。候选 10 由候选 9f 编译全部 SelfHost 源码产生，其构建命令、源码快照、峰值内存和工具哈希见同目录 `report.json`。这些记录对应其保存的源码，不代替后续整个语言的自举验收。
