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

## 内存管理

> 权威设计文档:`Doc/Spec/Memory_Model.md`(三块制 safe / scope / unsafe)。
> 本节为摘要;两处不一致时以 Memory_Model.md 为准。

Kairote Lang **不设后台 GC 线程**。自动内存管理由编译期完成:
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

unsafe(using 公司名.产品名.功能模块) {
    // 仅白名单命名空间的 _ 前缀内部函数可调用
    // 指针不得触及块外内存 —— 违反即硬错误
}
```

> 注:本节旧版描述的"自动垃圾回收 + malloc/free 函数 + using 资源管理语句"
> 与实现不符,已按三块制裁决重写;旧语义不再有效。

---

*本规范文档涵盖了 Kairote Lang 语言的核心特性，更多详细信息请参考 API 参考文档和开发者指南。