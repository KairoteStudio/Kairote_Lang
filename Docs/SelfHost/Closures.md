# 闭包与回调

lambda 与自由函数使用同一种 `fn(...) -> ...` 类型，可保存到局部量、字段、数组和结构体，也可作为参数或返回值。Parser、绑定、捕获分析、IR、原生 SSA 后端和 VM 都由 Kairote 实现。

## 语法与类型

```krt
var increment = function(int32 value) => value + 1;
fn(int32)->int32 twice = function(value) => value * 2;
var explicit_return = function(int32 value)->int32 {
    return value + 7;
};
fn(ref int32)->void change = function(ref value) => { value += 7; };
```

`function`、`func`、`fn` 均可引入 lambda。签名后可写 `=> 表达式`、`=> { 语句 }` 或直接写块体；`-> 类型` 可明确返回类型。块体支持普通返回、条件、循环和异常语句。可立即调用 `(function(int32 x)=>x+1)(41)`，也可返回另一 lambda。

参数可明确类型，或在已知回调上下文中省略类型；混合参数保留各自的类型槽位、`ref` 和 `readonly`。初始化、字段/元素赋值、实参、返回和条件分支可提供上下文，泛型与重载按完整签名和布局匹配。没有上下文的省略参数类型会报错。各返回值需要兼容，不可达语句仍经过名称、类型与访问检查。`void` 回调可单独调用，不能作为值消费。

命名实参遵循调用点保留的静态参数名契约。动态选择只保留所有分支共有的静态标签，嵌套回调签名也应用同样规则；匿名 `fn` 类型不提供参数标签，条件合并或跨函数返回不会补出名称。

## 捕获与作用域

```krt
fn(int32)->int32 Counter(int32 initial) {
    return function(int32 amount) => {
        initial += amount;
        return initial;
    };
}
int32 main() {
    var counter = Counter(5);
    int32 first = counter(7);
    int32 second = counter(3);
    delete counter;
    return first == 12 && second == 15 ? 0 : 1;
}
```

捕获绑定到词法声明。外层代码和捕获同一声明的回调共享一个可变 cell；赋值和合法 `ref` 写回访问同一份值。不同函数调用有独立的局部 cell；嵌套闭包保留所需 cell，创建函数返回后仍可调用。

cell 保存完整数值宽度、内联结构体及固定数组字节。按值参数先复制再捕获，调用方修改原结构体不会改变该参数副本。类、数组及其他引用值复制引用，不深拷贝指向的对象。只读和访问权限继续生效；只读结构体接收者使用防御性副本。

循环体内每次执行的声明、foreach 变量及每次成功匹配的 `is Type name` 模式变量有独立 cell。`for` 初始化声明属于整个循环作用域，多个回调观察同一个循环变量。失败模式不创建目标类型的捕获值。catch 参数在 catch 块进入时复制到 cell。作用域 owner 在正常退出、`return`、`break`、`continue` 和 `throw` 时释放引用；已有闭包继续保留其 cell。

## 释放与别名

保存的捕获闭包采用手动所有权。`delete callback` 释放该闭包的描述符和环境，并减少环境持有的 cell 引用。持有该 cell 的所有闭包环境均已释放，且词法作用域 owner 已退出后，cell 被释放。删除自由函数或无捕获 lambda 没有堆释放效果；`delete null` 安全。

回调赋值与参数传递复制的是同一描述符值，不增加独立的描述符所有权。删除捕获闭包后，其全部别名失效；只删除一次，随后不能再调用或删除别名。删除容器不会递归删除字段或元素中的回调，覆盖回调前也需要安排旧值的释放。

直接立即调用的捕获 lambda，例如 `(function()=>value)()`，在正常返回或异常展开时自动释放临时描述符。这个规则不自动转移回调实参所有权，也不自动释放普通函数返回后再被调用的回调。保存供以后调用的闭包仍需 `delete`。

cell 引用计数不是 GC，也不是描述符别名引用计数。宽整数使用完整 16 字节值存储；局部量、参数、返回、捕获和装箱均按值复制。运算临时值使用当前调用帧的固定空间，重复计算不会持续分配宽整数堆对象。旧 VM v1–v5 字节码保留原有指针宽整数约定，新约定使用 v6。字符串拼接结果仍遵循语言的手动所有权。

## 借用边界

闭包可以声明和使用 `ref` 形参。捕获外层 `ref` 形参、栈上的结构体 `this`、已知局部地址或 `stackalloc` 指针及已知别名，会报告 `E_CAPTURE_BORROW`。类的 `this` 可按对象引用捕获，对象本身的生命周期仍由程序管理。

`unsafe` 强制转换、整数地址、外部函数和系统调用结果不提供完整的生命周期证明。捕获指针不会延长目标内存的生命周期，调用方必须保证地址在所有使用期间有效；回调收到的借用字符串和缓冲区同样如此。HTTP handler 的规则见 [HTTP 服务](../../examples/http-server/README.md#自定义-handler)。

## 调用约定与兼容性

回调值占一个 64 位字。原生自由函数和无捕获 lambda 使用代码地址，捕获 lambda 使用最高位为 1 的描述符值，描述符包含代码与环境地址。间接调用识别两种值，并通过隐藏寄存器 `r10` 传环境。IR 的 `closure.environment` 读取环境；原生路径继续验证 SSA、分配寄存器并生成 KRO。

环境不占源码形参，仍允许最多 128 个源码参数；实例接收者按既有规则计入限额，结构体返回的隐藏结果指针保持独立规则。

分别编译的普通回调使用 `fn2` 名称修饰。含宽整数参数或返回的回调使用 `fn3`，并通过 ABI3 的 `KRTWIDE3` 契约记录 16 字节值及隐藏结果参数约定。公开契约使用不兼容的旧 `fn` 或旧宽整数物理约定时，链接会明确拒绝并保留已有输出；不含这些变化的具体 ABI2 对象继续兼容。

ABI3 同时记录跨模块类、接口、枚举及回调签名的完整规范类型身份；哈希只用于索引，不代替实际类型比较。回调可装箱为 `object` 并按完整签名检查拆箱。装箱包装器引用原回调描述符，删除包装器不会删除回调；调用者仍需安排捕获回调及其别名的生命周期。这些规则属于 Kairote 调用约定，不是通用 C ABI。

含 lambda 环境指令的 VM 模块使用 EBC v4，函数元数据与 v3 相同。无捕获回调是函数索引加一的 handle，捕获回调使用高位描述符标记，环境单独保存在调用帧。v1–v3 文件继续按各自语义读取，旧版本不接受闭包环境指令。详见 [VM 格式](Vm.md#ebc-version-4)。

同时使用运行时类型描述符或新的宽整数值约定时，编译器输出 EBC v6，闭包环境约定保持一致。v4 和 v5 文件也继续按各自版本读取。

## 验证

```sh
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" \
ARKLINK="$PWD/build/ArkLink/ArkLink" \
python3 -m unittest Test.SelfHost.test_closures \
  Test.SelfHost.test_closure_patterns Test.SelfHost.test_closure_inference \
  Test.SelfHost.test_http_server -v
```

测试覆盖共享捕获与逃逸、布局、循环与模式、异常清理、递归回调、上下文推断、独立 KRO 和真实 HTTP 请求。应核对当前源码与报告哈希；闭包通过不能推导整个语言与 Re.KrtC 完全一致。

候选 7 的模式、值参数与立即调用补充套件包含 16 份成功源码，Native/VM × O0/O2 共 64 次编译执行全部通过；已知栈指针模式捕获另以 16 次重复编译检查准确的 `E_CAPTURE_BORROW` 位置及已有输出保留。实际命令、源码和产物哈希保存在 `build/selfhost-closures/ir/parameter-contracts/formal-candidate7-report.json`，文档示例证据在 `build/selfhost-closures/ir/doc-examples/report.candidate7.json`。

完整闭包套件、真实 HTTP、压力与自举验收应核对 `build/selfhost-closures/report.json` 中对应门禁及源码哈希。补充套件目录同时保留此前的失败源码、候选报告和测试字节，便于比较失败模式、栈借用来源、结构体副本和宽整数副本等一般规则的修复。

新的宽整数值与跨模块回调门禁见 [test_wide_lifetimes.py](../../Test/SelfHost/test_wide_lifetimes.py) 和 [test_module_abi.py](../../Test/SelfHost/test_module_abi.py)。[装箱测试](../../Test/SelfHost/test_boxing.py) 覆盖回调包装器别名及错误签名拆箱；[组合 HTTP 测试](../../Test/SelfHost/test_runtime_http.py) 将捕获 handler、通用迭代器、私有结构体装箱和跨模块调用用于真实请求。以上旧报告继续保留其原始验收范围和源码哈希。
