# KairoteLang 标准库实现 TODO

> 目标：构建不依赖 C 运行时库的纯 .krt 标准库
> 原则：每个阶段都可编译、可测试、可验证

---

## 阶段一：内存分配基础

### 1.1 实现 `KrtMalloc` / `KrtFree` 内建函数

**目标**：提供堆内存分配能力

```
KrtMalloc(size) → void*    // 基于 mmap 分配
KrtFree(ptr)               // 基于 munmap 释放
```

**实现方案**：
- `IrGen.c`：新增 `IRGEN_FUNC_MALLOC` / `IRGEN_FUNC_FREE` 调度
- `KroCodegen.c`：生成 `syscall(SYS_mmap, ...)` / `syscall(SYS_munmap, ...)`
- 内部维护一个简单的分配器（首次适应 / 固定大小缓冲区）

**验证**：
```krt
void* p = KrtMalloc(64);
// 写入数据
KrtFree(p);
```

---

### 1.2 实现 `internal_int32_to_string` 内建函数

**目标**：整数 → 字符串转换

**实现方案**：
- 基于 `KrtMalloc` 分配缓冲区
- 逐位提取数字（从低位到高位）
- 处理负数和零的特殊情况
- 返回 null-terminated string

**验证**：
```krt
string s = internal_int32_to_string(-123);
// s == "-123"
```

---

### 1.3 实现 `internal_strdup` / `internal_strcpy`

**目标**：字符串复制

**实现方案**：
- `strlen` + `malloc` + `memcpy` 的纯 .krt 实现

---

## 阶段二：字符串操作

### 2.1 `String` 类基础

```krt
public static class String
{
    public static string Concat(string a, string b);
    public static string Substring(string s, int32 start, int32 length);
    public static int32 IndexOf(string s, char c);
    public static int32 LastIndexOf(string s, char c);
    public static bool StartsWith(string s, string prefix);
    public static bool EndsWith(string s, string suffix);
}
```

### 2.2 `StringBuilder` 类

```krt
public static class StringBuilder
{
    public StringBuilder(int32 capacity);
    public void Append(string s);
    public void Append(int32 value);
    public void Append(char c);
    public string ToString();
}
```

---

## 阶段三：Console 完整实现

### 3.1 基础输出

```krt
public static class Console
{
    public static void Write(string value);
    public static void Write(int32 value);
    public static void Write(int64 value);
    public static void Write(bool value);
    public static void Write(char value);
    public static void WriteLine(string value);
    public static void WriteLine(int32 value);
    public static void WriteLine();
}
```

### 3.2 格式化输出

```krt
public static class Console
{
    public static void WriteFormat(string format, string arg0);
    public static void WriteFormat(string format, int32 arg0);
    public static void WriteFormat(string format, string arg0, string arg1);
    // ...
}
```

### 3.3 输入

```krt
public static class Console
{
    public static string ReadLine();
    public static int32 ReadKey();
}
```

---

## 阶段四：集合基础（无泛型）

### 4.1 类型安全包装器模式

> 由于泛型单态化尚未实现，使用类型特化的包装器

```krt
public static class Int32List
{
    private int32[] _data;
    private int32 _count;
    
    public Int32List(int32 capacity);
    public void Add(int32 item);
    public int32 Get(int32 index);
    public void Set(int32 index, int32 value);
    public int32 Count();
    public void Clear();
}

public static class StringList
{
    private string[] _data;
    private int32 _count;
    
    public StringList(int32 capacity);
    public void Add(string item);
    public string Get(int32 index);
    // ...
}
```

### 4.2 动态数组

```krt
public static class Array
{
    public static void Resize(void** array, int32 newSize);
    public static void Copy(void* src, void* dst, int32 length);
}
```

---

## 阶段五：数学与转换

### 5.1 `Math` 类

```krt
public static class Math
{
    public static int32 Abs(int32 value);
    public static int64 Abs(int64 value);
    public static int32 Max(int32 a, int32 b);
    public static int32 Min(int32 a, int32 b);
    public static int32 Clamp(int32 value, int32 min, int32 max);
    public static double Sqrt(double value);
    public static double Pow(double base, double exponent);
}
```

### 5.2 `Convert` 类

```krt
public static class Convert
{
    public static int32 ToInt32(string s);
    public static int64 ToInt64(string s);
    public static string ToString(int32 value);
    public static string ToString(int64 value);
    public static string ToString(bool value);
}
```

---

## 阶段六：文件 I/O

### 6.1 `File` / `FileStream`

```krt
public static class File
{
    public static bool Exists(string path);
    public static FileStream Open(string path, int32 mode);
    public static void Delete(string path);
}

public static class FileStream
{
    public FileStream(int32 fd);
    public int32 Read(byte[] buffer, int32 offset, int32 count);
    public void Write(byte[] buffer, int32 offset, int32 count);
    public void Close();
}
```

---

## 阶段七：泛型系统

### 7.1 泛型单态化

**目标**：`List<T>` 可以为每个 T 生成专门代码

**实现方案**：
- 解析时记录泛型参数
- IR 生成时为每种具体类型创建副本
- Name mangling 包含类型参数信息

### 7.2 接口系统

```krt
public interface IEnumerable<T>
{
    T GetEnumerator();
}

public interface IComparable<T>
{
    int32 CompareTo(T other);
}

public interface IDisposable
{
    void Dispose();
}
```

---

## 阶段八：高级语言特性

### 8.1 虚方法分派（vtable）

- 解析 `virtual` / `override` 关键字
- 生成 vtable 结构
- 方法调用改为间接寻址

### 8.2 枚举

```krt
public enum ConsoleColor
{
    Black = 0,
    Red = 1,
    Green = 2,
    // ...
}
```

### 8.3 foreach 支持

```krt
// 依赖 IEnumerable<T> 接口
foreach (string item in list) { }
```

### 8.4 异常处理

```krt
// 解析 try/catch/finally
// 生成 .eh_frame 异常表
// 实现栈展开
```

### 8.5 `Point` / `Unsafe` / `Safe` 与静态生命周期管理

- `Point { ... }`：裸指针声明、`KrtMalloc`、`KrtFree` 和指针运算的唯一合法区域
- `Unsafe(Z.A.b.*) { ... }`：按参数导入标准库或外部库能力；无重名时允许省略限定名调用
- `Safe { ... }`：编译期验证分配的内存不能逃逸或泄漏
- 静态生命周期分析：定位最后一次使用和作用域出口，自动插入释放操作，实现与 GC 等价的效果

---

## 测试策略

每个阶段完成后，编写测试用例验证：

```
stdlib_tests/
├── phase01_malloc/
│   ├── TestMalloc.krt
│   ├── TestIntToString.krt
│   └── RunTests.krt
├── phase02_string/
│   ├── TestStringConcat.krt
│   ├── TestStringBuilder.krt
│   └── RunTests.krt
├── phase03_console/
│   ├── TestConsoleWrite.krt
│   ├── TestConsoleFormat.krt
│   └── RunTests.krt
└── ...
```

---

## 优先级排序

| 优先级 | 阶段 | 依赖 |
|--------|------|------|
| P0 | 阶段一（内存分配） | 无 |
| P1 | 阶段三（Console） | 阶段一 |
| P1 | 阶段二（字符串） | 阶段一 |
| P2 | 阶段五（数学） | 阶段二 |
| P2 | 阶段四（集合） | 阶段一 |
| P3 | 阶段六（文件 I/O） | 阶段一 |
| P4 | 阶段七（泛型） | 无（独立） |
| P5 | 阶段八（高级特性） | 阶段七 |

---

## 已完成 ✅

- [x] `KrtMalloc` / `KrtFree` 内建函数（`mmap` / `munmap`）
- [x] `syscall` 内建函数
- [x] `internal_string_ptr` / `internal_string_len` 内建函数
- [x] 类型转换表达式 `(int64)expr`
- [x] `string` 类型声明支持
- [x] ArkLink 链接器
- [x] Kro 代码生成器（syscall、cast）
- [x] X86 汇编生成器（syscall、cast）
