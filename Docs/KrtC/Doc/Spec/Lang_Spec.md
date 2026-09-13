# Kairote Lang 语言规范

## 目录

1. [概述](#概述)
2. [词法结构](#词法结构)
3. [语法结构](#语法结构)
4. [类型系统](#类型系统)
5. [表达式](#表达式)
6. [语句](#语句)
7. [函数](#函数)
8. [类和对象](#类和对象)
9. [命名空间](#命名空间)
10. [泛型编程](#泛型编程)
11. [异常处理](#异常处理)
12. [并发编程](#并发编程)

## 概述

Kairote Lang 是一种现代的、面向对象的编程语言，设计目标是提供高性能、类型安全和易用性的平衡。它融合了多种编程语言的特性，包括 C++ 的性能、C# 的语法和 Java 的跨平台能力。

### 主要特性

- 强类型系统
- 面向对象编程（类、继承、多态）
- 泛型编程
- 垃圾回收与手动内存管理
- 异常处理机制
- 命名空间支持
- 函数式编程特性
- 跨平台编译

## 词法结构

### 关键字

Kairote Lang 保留以下关键字：

```
function, var, if, else, while, for, foreach, in, return, print,
true, false, new, delete, class, struct, interface,
enum, namespace, this, base, public, private, protected, static,
virtual, abstract, override, using, package, console,
try, catch, finally, throw, exception, template, typename, where,
switch, case, break, continue, default
```

### 数据类型关键字

```
int8, int16, int32, int64, uint8, uint16, uint32, uint64,
float32, float64, bool, char, void, string
```

### 标识符

标识符由字母、数字和下划线组成，必须以字母或下划线开头。标识符区分大小写。

### 注释

```KrtL
// 单行注释

/*
 * 多行注释
 */
```

### 字面量

#### 数字字面量

```KrtL
42          // 整数
3.14        // 浮点数
0xFF        // 十六进制
0b1010      // 二进制
```

#### 字符串字面量

```KrtL
"Hello, World!"    // 普通字符串
"Line 1\nLine 2"   // 包含转义字符的字符串
```

#### 布尔字面量

```KrtL
true
false
```

## 语法结构

### 程序结构

Kairote Lang 程序由一个或多个命名空间组成，每个命名空间可以包含类、结构、接口、枚举和函数。

```KrtL
namespace MyNamespace {
    class MyClass {
        // 类成员
    }
    
    function MyFunction() {
        // 函数体
    }
}
```
或:
```KrtL
namespace MyNamespace;
class MyClass {
    // 类成员
}

function MyFunction() {
    // 函数体
}
```

### 声明

#### 变量声明

```KrtL
var x = 10;              // 类型推断
int32 y = 20;           // 显式类型
string name = "Kairote Lang";     // 字符串类型
bool isReady = true;    // 布尔类型
```

#### 常量声明

```KrtL
const int32 MAX_SIZE = 100;
```

#### 数组声明

```KrtL
int32[] numbers = new int32[10];
string[] names = ["Alice", "Bob", "Charlie"];
```

## 类型系统

### 基本类型

| 类型 | 描述 | 大小 |
|------|------|------|
| int8 | 8位有符号整数 | 1字节 |
| int16 | 16位有符号整数 | 2字节 |
| int32 | 32位有符号整数 | 4字节 |
| int64 | 64位有符号整数 | 8字节 |
| uint8 | 8位无符号整数 | 1字节 |
| uint16 | 16位无符号整数 | 2字节 |
| uint32 | 32位无符号整数 | 4字节 |
| uint64 | 64位无符号整数 | 8字节 |
| float32 | 32位浮点数 | 4字节 |
| float64 | 64位浮点数 | 8字节 |
| bool | 布尔值 | 1字节 |
| char | 字符 | 2字节 |
| string | 字符串 | 可变 |
| void | 无类型 | - |

#### 偶数位宽整数（Re.KrtC）

`intN` 和 `uintN` 支持所有 `N = 2, 4, 6, …, 128`，共 128 种类型。
`int`、`long` 仍分别是 `int32`、`int64` 的别名。

| 类型 | 取值范围 |
|------|----------|
| intN | −2^(N−1) 到 2^(N−1)−1 |
| uintN | 0 到 2^N−1 |

整数按补码计算，赋值、整数转换和运算结果保留低 N 位；有符号类型按第 N 位进行符号扩展。
溢出按模 2^N 回绕，例如 `int6` 的 31 加 1 得到 −32，`uint6` 的 63 加 1 得到 0。
二元整数运算采用较大的位宽；位宽相同时，任一操作数无符号则结果无符号。
移位保留左操作数的类型；有符号右移补符号位，无符号右移补零。
移位数大于等于位宽时，左移和无符号右移结果为零，有符号右移结果为零或 −1。
除法向零截断，余数与被除数同号；最小有符号整数除以 −1 按同样的回绕规则处理。

整数支持 `&= |= ^= <<= >>=` 复合赋值，适用于标量变量、参数、全局变量、引用和数组/指针元素。
目标地址与旧值在右侧表达式之前求值，且仅求值一次；运算后按目标位宽回绕。
移位沿用左操作数的位宽和符号性；这些复合运算的两侧必须是整数，指针自身只支持 `+=`、`-=` 整数。

十进制、十六进制和二进制整数字面量精确保存到 128 位，不经过浮点数转换。
正字面量依次选用能容纳它的 `int32`、`int64`、`int128`、`uint128`；
超过 `uint128` 最大值的字面量报编译错误。
需要让运算在特定位宽内进行时，先转换操作数，例如 `(uint128)1 << 100`。

`sizeof(intN)` 与 `sizeof(uintN)` 使用能容纳 N 位的最小 1、2、4、8 或 16 字节存储单元，
数组元素使用相同大小。例如 `int6` 占 1 字节，`int24` 占 4 字节，`uint96` 占 16 字节。
局部变量、全局变量和类字段可能因对齐或栈槽分配占用更多空间。

```KrtL
int6 small = 31;
small++;                       // -32
uint128 high = (uint128)1 << 100;
uint128 max = 340282366920938463463374607431768211455;
uint96[] values = [1, 79228162514264337593543950335];
```

新增位宽已接入 KRO / 本机可执行文件后端，涵盖整数算术、比较、位运算、整数间转换、
函数参数和返回值、局部与全局变量、类字段及数组。旧的 `asm`、`vm` 后端遇到新增位宽会报错。
标准库的 Console / Convert 重载仍以它们声明的参数类型为准。

#### 整数的寄存器打包（x86-64 KRO）

局部整数变量和按值传入的整数参数会自动尝试共享寄存器中的不同位段。
后端使用 `RBX`、`R12`～`R15`，优先填充已有空位；每个变量的位段在一次函数执行中固定，
读取时按该类型进行符号扩展或补零；共享寄存器时，写入用掩码只替换自己的位段。
位段独占寄存器时直接写入。8 / 16 / 32 位整数优先使用原生扩展指令，普通整数运算不计算不需要的高 64 位。

例如下面四个变量可以同时保存在同一个 64 位寄存器中：

| 变量类型 | 使用的位（从低位开始编号） |
|----------|---------------------------|
| int2 | 0～1 |
| int30 | 2～31 |
| uint6 | 32～37 |
| uint26 | 38～63 |

2～62 位的整数可以完整放入一个位段，64 位整数占用一个完整寄存器。66～126 位的整数保留一个低 64 位存储单元，
剩余的高 N−64 位参与寄存器打包，例如 `int66` 的高 2 位可以和 `int62` 共享一个寄存器。
寄存器空间不足时使用栈槽。函数入口保存用到的寄存器，返回时恢复，因此调用和递归会保留调用者的值。
局部变量和参数采用函数内固定分配。基本块内的临时计算值按活跃区间复用剩余的完整寄存器，
超过 64 位的临时值需要两个寄存器；跨基本块、重复定义或寄存器不足的临时值使用栈槽。
内存中的数组和类字段遵循上文的存储大小规则。

### 复合类型

#### 数组

```KrtL
int32[] numbers;        // 整数数组
string[] names;         // 字符串数组
int32[][] matrix;       // 二维数组
```

#### 类

```KrtL
class Person {
    var name;
    int32 age;
    
    function Person(name, int32 age) {
        this.name = name;
        this.age = age;
    }
}
```

#### 结构

```KrtL
struct Point {
    float32 x;
    float32 y;
    var z;
}
```

### 类型转换

#### 隐式转换

```KrtL
int32 i = 42;
float64 f = i;          // int32 到 float64 的隐式转换
```

#### 显式转换

```KrtL
float64 f = 3.14;
int32 i = (int32)f;     // 显式转换
```

## 表达式

### 算术表达式

```KrtL
var a = 10 + 5;         // 加法
var b = 10 - 5;         // 减法
var c = 10 * 5;         // 乘法
var d = 10 / 5;         // 除法
var e = 10 % 5;         // 取模
var f = 2 ** 3;         // 幂运算
```

### 比较表达式

```KrtL
var a = 10 == 5;        // 等于
var b = 10 != 5;        // 不等于
var c = 10 > 5;         // 大于
var d = 10 < 5;         // 小于
var e = 10 >= 5;        // 大于等于
var f = 10 <= 5;        // 小于等于
```

### 逻辑表达式

```KrtL
var a = true and false; // 逻辑与
var b = true or false;  // 逻辑或
var c = not true;       // 逻辑非
```

### 位运算表达式

```KrtL
var a = 5 & 3;          // 按位与
var b = 5 | 3;          // 按位或
var c = 5 ^ 3;          // 按位异或
var d = ~5;             // 按位取反
var e = 5 << 2;         // 左移
var f = 5 >> 2;         // 右移
```

### 三元表达式

```KrtL
var result = (x > 0) ? "positive" : "non-positive";
```

## 语句

### 条件语句

#### if 语句

```KrtL
if (x > 0) {
    print("x is positive");
} else if (x < 0) {
    print("x is negative");
} else {
    print("x is zero");
}
```

#### switch 语句

```KrtL
switch (day) {
    case 0:
        print("Sunday");
        break;
    case 1:
        print("Monday");
        break;
    default:
        print("Other day");
        break;
}
```

### 循环语句

#### while 循环

```KrtL
var i = 0;
while (i < 10) {
    print(i);
    i = i + 1;
}
```

#### for 循环

```KrtL
for (var i = 0; i < 10; i = i + 1) {
    print(i);
}
```

#### foreach 循环

```KrtL
var numbers = [1, 2, 3, 4, 5];
foreach (var num in numbers) {
    print(num);
}
```

### 跳转语句

```KrtL
for (var i = 0; i < 10; i = i + 1) {
    if (i == 5) {
        break;          // 跳出循环
    }
    if (i % 2 == 0) {
        continue;       // 跳过本次迭代
    }
    print(i);
}
```

## 函数

### 函数声明

```KrtL
int32 Add(int32 a, int32 b) {
    return a + b;
}
```

### 函数调用

```KrtL
var result = Add(5, 3);
```

### 参数传递

#### 值传递

```KrtL
int32 Increment(int32 x) {
    x = x + 1;
    return x;
}

var a = 5;
var b = Increment(a);  // a 仍然是 5
```

#### 引用传递

```KrtL
function Swap(ref int32 a, ref int32 b) {
    var temp = a;
    a = b;
    b = temp;
}

var x = 5, y = 10;
Swap(ref x, ref y);     // x = 10, y = 5
```

Re.KrtC 的 KRO 后端也允许省略调用位置的 `ref`，例如 `Swap(x, y)`；参数声明决定是否传引用。实参必须是变量、数组元素或已验证的指针解引用，类型必须完全一致，包括指针可空性。引用参数读写同一存储位置，支持别名、转发、递归、宽整数和浮点类型；函数指针签名必须保留 `ref`。普通引用传参无需 `unsafe`，原始指针操作仍遵守权限要求。

### 递归函数

```KrtL
int32 Factorial(int32 n) {
    if (n <= 1) {
        return 1;
    }
    return n * Factorial(n - 1);
}
```

### 匿名函数

```KrtL
// 匿名函数支持返回类型推导，返回类型可省略
var add = function (int32 a, int32 b) => a + b;

var result = add(5, 3);
```

## 类和对象

### 类定义

```KrtL
class Person {
    // 私有字段
    private var name;
    private int32 age;
    
    // 构造函数
    function Person(name, int32 age) {
        this.name = name;
        this.age = age;
    }
    
    // 公共方法
    public function GetName() {
        return this.name;
    }
    
    public int32 GetAge() {
        return this.age;
    }
    
    public function SetAge(int32 age) {
        if (age >= 0) {
            this.age = age;
        }
    }
}
```

### 对象创建

```KrtL
var person = new Person("Alice", 30);
print(person.GetName());
print(person.GetAge());
```

### 继承

```KrtL
class Animal {
    protected var name;
    
    function Animal(name) {
        this.name = name;
    }
    
    public function Speak() {
        print("Animal sound");
    }
}

class Dog : Animal {
    private var breed;
    
    function Dog(name, breed): base(name) {
        this.breed = breed;
    }
    
    public override function Speak() {
        print("Woof!");
    }
}
```

### 接口

```KrtL
interface IDrawable {
    function Draw();
}

class Circle : IDrawable {
    private float32 radius;
    
    function Circle(float32 radius) {
        this.radius = radius;
    }
    
    public function Draw() {
        print("Drawing a circle with radius " + this.radius);
    }
}
```

### 抽象类

```KrtL
abstract class Shape {
    protected var name;
    
    function Shape(name) {
        this.name = name;
    }
    
    public abstract float32 GetArea();
    
    public function GetName() {
        return this.name;
    }
}

class Rectangle : Shape {
    private float32 width;
    private float32 height;
    
    function Rectangle(float32 width, float32 height): base("Rectangle") {
        this.width = width;
        this.height = height;
    }
    
    public override float32 GetArea() {
        return this.width * this.height;
    }
}
```

## 命名空间

### 命名空间声明

```KrtL
namespace MyCompany.MyApp {
    class MyClass {
        // 类实现
    }
}
```

### 命名空间导入

```KrtL
using MyCompany.MyApp;

function Main() {
    var obj = new MyClass();
}
```

### 命名空间别名

```KrtL
using MyApp = MyCompany.MyApp;

function Main() {
    var obj = new MyApp.MyClass();
}
```

## 泛型编程

### 泛型类

```KrtL
class Box<T> {
    private T value;
    
    function Box(T value) {
        this.value = value;
    }
    
    public T GetValue() {
        return this.value;
    }
    
    public function SetValue(T value) {
        this.value = value;
    }
}

var intBox = new Box<int32>(42);
var stringBox = new Box<string>("Hello");
```

### 泛型函数

```KrtL
function Swap<T>(ref T a, ref T b) {
    var temp = a;
    a = b;
    b = temp;
}

var x = 5, y = 10;
Swap<int32>(ref x, ref y);
```

### 泛型约束

```KrtL
interface IComparable<T> {
    int32 CompareTo(T other);
}

T Max<T where T: IComparable<T>>(T a, T b) {
    if (a.CompareTo(b) > 0) {
        return a;
    }
    return b;
}
```

## 异常处理

### 异常抛出

```KrtL
float64 Divide(float64 a, float64 b) {
    if (b == 0.0) {
        throw new Exception("Division by zero");
    }
    return a / b;
}
```

### 异常捕获

```KrtL
try {
    var result = Divide(10.0, 0.0);
    print(result);
} catch (Exception e) {
    print("Error: " + e.Message);
} finally {
    print("Cleanup code");
}
```

### 自定义异常

```KrtL
class InvalidArgumentException : Exception {
    function InvalidArgumentException(message): base(message) {
    }
}

function ProcessAge(int32 age) {
    if (age < 0 || age > 150) {
        throw new InvalidArgumentException("Invalid age: " + age);
    }
    // 处理年龄
}
```

## 并发编程

### 线程创建

```KrtL
using System.Threading;

function ThreadFunction() {
    for (var i = 0; i < 5; i = i + 1) {
        print("Thread: " + i);
        Thread.Sleep(1000);
    }
}

var thread = new Thread(ThreadFunction);
thread.Start();
```

### 同步机制

```KrtL
class Counter {
    private int32 count = 0;
    private var lock = new Object();
    
    public function Increment() {
        lock (this.lock) {
            this.count = this.count + 1;
        }
    }
    
    public int32 GetCount() {
        lock (this.lock) {
            return this.count;
        }
    }
}
```

## unsafe 指针（Re.KrtC / KRO 本机后端）

```KrtL
int add(int a, int b) { return a + b; }
int*? find(int* buffer) { return buffer; }

int main() {
    unsafe(using krt.mem, Acme.Ffi.Native;) {
        var x = 42;
        let p = &x;
        *p = 7;
        let y = *p;
        let q = p + 4;
        let n = q - p;
        var buf = stackalloc int[64];
        buf[3] = 1;
        let bp = (byte*)p;
        let vp: void* = p;
        let pp: int** = &p;
        let f: fn(int, int) -> int = &add;
        let answer = f(3, 4);
        let op = p < q;
        let maybe: int*? = find(buf);
        if (maybe is int* hit) { *hit = 0; }
    }
    return 0;
}
```

- `int` 等于 `int32`，`byte` 等于 `uint8`，本机 `usize` 等于 `uint64`。
- `var name: T = value` / `let name: T = value` 支持显式类型；省略 `: T` 时推断类型。指针层级和函数签名参与类型检查，不退化成无类型整数。
- `&x` 取得变量、参数或数组元素的实际地址。取地址的局部变量和参数使用独立栈槽；其他整数仍可共用寄存器。通过别名修改后，直接读取原变量能看见新值。
- `(*p)++`、`++p[i]` 和指向指针槽的 `*pp += n` 会写回对应位置；地址与索引只求值一次。表达式和实参从左向右求值；复合赋值先读取旧值再计算右侧。
- `T* +/- 整数` 按 `sizeof(T)` 缩放；`p[i]` 等价于 `*(p + i)`。所有偶数位宽整数均可作元素类型，存储大小沿用整数规范的 1/2/4/8/16 字节。多级指针元素占 8 字节。
- 同类型指针可相减，结果是 `usize` 元素数；负差按模 `2^64` 表示。指针比较使用无符号地址顺序。范围与地址有效性由 unsafe 代码负责。
- `stackalloc T[count]` 真正分配当前函数的栈空间，按 16 字节对齐，逐页探测；支持运行时整数数量，大小溢出会终止执行。内存在函数返回时回收，不初始化内容；其地址不能在函数返回后继续使用。
- `(T*)p` 显式转换指针；类型不同的指针不能隐式互转，`T*` 到 `void*` 除外。`void*` 必须先转换成有大小的元素类型，才能解引用、索引或做算术。
- `fn(T, ...) -> R` 保存函数签名，`&add` 取得入口地址，`f(...)` 生成间接调用。整数与数据指针使用本机整数调用约定，包括宽整数和栈上传参。浮点函数沿用 Kairote 内部传参方式，尚未实现 C 浮点 FFI ABI。
- `T*?` 可保存 `null`。解引用、索引、算术前必须用 `is T* name` 收窄；绑定仅存在于 `if` 的真分支，匹配表达式求值一次。赋值和转换都不能直接去掉可空标记。
- 每层指针独立保留可空信息：`int*?*` 是指向可空指针槽的非空指针；`&maybe` 不会把槽本身视作可空。指针数组也保留元素的可空信息。可空指针允许与同类型指针或 `null` 做相等性比较。
- `float32*` 使用 4 字节 IEEE 单精度存储，`float64*` 使用 8 字节双精度存储；字节别名、非对齐访问和 NaN 比较参与回归。浮点取余复合赋值尚不支持，会报错。
- 当前指针层级上限为 64；KRO 函数参数上限为 128，局部存储上限为 1024，临时值上限为 4096；超限会报告编译错误。
- 指针操作要求 `unsafe(using krt.mem;) { ... }`，权限在离开块时恢复。括号中的分号是语法的一部分，逗号可分隔多个命名空间；该列表本身不导入库或加载 FFI。现阶段实现 `krt.mem` 权限检查，其他命名空间记录为权限元数据，完整内部函数白名单机制仍见内存模型设计。

可运行的检查示例：`Test/Pointers/UnsafePointers.krt`；测试命令：
`python3 Test/Pointers/RunTests.py`。代表用例同时纳入 `Test/SyntaxAudit/cases/C80` 至 `C91`。

## 内存管理

> 权威设计文档:`Doc/Spec/Memory_Model.md`(三块制 safe / scope / unsafe)。
> 本节为设计摘要；当前实现以该文档 §6 和上面的 unsafe 指针章节为准。

Kairote Lang **不设后台 GC 线程**。设计中的自动内存管理由编译期完成:
`scope` 块经静态活性扫描后在恰当位置自动插入释放命令,
运行时行为与手写释放完全一致。

### 三块总览

| 块 | 机制 | 强度 |
|---|---|---|
| `unsafe(using ...)` | 指针操作与 `_` 前缀底层函数调用,命名空间白名单放行 | 硬边界(越界即编译错误) |
| `scope` | 编译期活性扫描 + 自动插释放 | 效果等同 GC |
| `safe` | 泄漏风险静态检查(循环引用、事件未解绑等) | 仅警告 |

三块互不嵌套,亦不自嵌套。

```KrtL
scope {
    var obj = new MyClass();
}   // 编译器在此自动插入 obj 的释放

unsafe(using 公司名.产品名.功能模块;) {
    // 仅白名单命名空间的 _ 前缀内部函数可调用
    // 指针不得触及块外内存 —— 违反即硬错误
}
```

> 注:本节旧版描述的"自动垃圾回收 + malloc/free 函数 + using 资源管理语句"
> 与实现不符,已按三块制裁决重写;旧语义不再有效。

---

*本规范文档涵盖了 Kairote Lang 语言的核心特性，更多详细信息请参考 API 参考文档和开发者指南。

## Re.KrtC 优化级别

Re.KrtC 支持 `-O0`、`-O1`、`-O2` 和 `-O3`，默认为 `-O2`。选项可以放在源文件前后；重复指定时最后一个生效。`-o <文件>` 与 `output <文件>` 等价。单文件、多文件和 `build project.krt` 均传递优化级别；项目构建直接生成 KRO 对象。

```bash
Re.KrtC/build/KrtC -O3 Test.krt -o Test
Re.KrtC/build/KrtC -O3 build project.krt
```

| 级别 | 当前实现 |
|---|---|
| `-O0` | 关闭 IR 优化遍历、快速入口返回和自尾递归转换；仍进行正确执行所需的类型归一化、寄存器/栈分配及基础指令选择。 |
| `-O1` | 开启已有常量折叠、死指令清理、强度削减与快速入口返回。 |
| `-O2` | 在 O1 上启用安全的自尾递归与整数结合运算累加器；默认级别。 |
| `-O3` | 在 O2 上启用受代码量限制的小整数函数内联、64 位整数直接指令选择、调用结果直接累积、参数寄存器复制和尾循环布局优化。 |

O3 内联目前限于无内存操作、无调用和无分支的小整数函数；对大调用方保留原调用，控制临时值和代码增长。尾调用优化保留取地址、栈分配、浮点及副作用的原语义。不会按函数名称、特定输入值或基准识别选择算法，也不启用浮点重结合。自动加载的 `stdlib` 函数随当前 IR 模块按所选级别编译，链接阶段不重复编译它们；语法树缓存仍以源码版本区分，并为各 Pipeline 克隆。

这些级别名称提供优化强度选择，不意味着已实现 GCC 对应级别的全部优化。当前尚不具备 GCC O3 的多层递归内联、通用循环展开及自动向量化；同机实测和适用边界见 [O3 对测报告](../../../../Test/Performance/O3Optimization/ReadMe.md)。
