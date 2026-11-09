# Re.KrtC 示例与教程

以下示例针对仓库的 C 版编译器 `Re.KrtC`。它们只使用原生可执行文件编译路径。

## 1. 编译一个程序

创建 `main.krt`：

```krt
int32 main() {
    int32 answer = 6 * 7;
    return answer == 42 ? 0 : 1;
}
```

在仓库根目录构建工具并编译：

```sh
cmake -S . -B build
cmake --build build --target KrtC ArkLink -j4
build/Re.KrtC/KrtC -O2 main.krt output main
./main
```

程序返回状态码 `0` 表示示例检查通过。

## 2. 创建和访问数组

```krt
int32 main() {
    int32[] values = [10, 20, 12];
    int32 total = values[0] + values[1] + values[2];
    delete values;
    return total == 42 ? 0 : 1;
}
```

数组字面量和 `new T[n]` 创建托管数组。编译器会在托管数组读写前检查索引；负索引或 `index >= Length` 会以状态码 `70` 终止。数组的所有权仍由程序显式管理。

声明为 `T name[n]` 的内联定长数组也会使用编译期长度检查；它不是托管堆数组，不需要对它执行 `delete`。

## 3. 单次求值

索引表达式在读写中只求值一次：

```krt
int32 next(ref int32 count) {
    count++;
    return count - 1;
}

int32 main() {
    int32[] values = [17, 25];
    int32 count = 0;
    int32 first = values[next(ref count)];
    values[next(ref count)] = first;
    int32 ok = count == 2 && values[1] == 17 ? 0 : 1;
    delete values;
    return ok;
}
```

对于写入，数组表达式和索引会先计算并完成检查，然后才计算右侧值。

## 4. 栈上缓冲区和裸指针

```krt
int32 main() {
    unsafe(using krt.mem;) {
        int32* values = stackalloc int32[2];
        values[0] = 19;
        values[1] = 23;
        return values[0] + values[1] == 42 ? 0 : 1;
    }
}
```

裸指针没有可供编译器查询的数组长度，因此 `values[index]` 不会做托管数组边界检查。`stackalloc` 的存储仅在当前函数栈帧有效。

## 5. 项目配置

`project.krt` 通过 `Configure(Project p)` 提供项目名、项目类型、编译方式、源文件和输出名。一个最小配置示例：

```krt
function Configure(Project p) {
    p.Name("Example");
    p.Type("console");
    p.CompileTogether(true);
    p.Sources("main.krt");
    p.Output("Example");
}
```

在项目目录运行 `build/Re.KrtC/KrtC build`。配置解析的实际范围见[用户手册](../Guide/User_Manual.md#使用-projectkrt)。
