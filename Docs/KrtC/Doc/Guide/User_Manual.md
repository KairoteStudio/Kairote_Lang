# Re.KrtC 用户手册

本手册面向使用仓库 C 版编译器 `Re.KrtC` 的开发者。SelfHost 是另一套实现，见 [SelfHost 文档](../../../../SelfHost/README.md)。

## 构建编译器

从仓库根目录运行：

```sh
cmake -S . -B build
cmake --build build --target KrtC ArkLink -j4
```

生成的程序位于 `build/Re.KrtC/KrtC` 和 `build/ArkLink/ArkLink`。查看当前命令说明：

```sh
build/Re.KrtC/KrtC help
```

## 编译单文件

`main.krt`：

```krt
int32 main() {
    int32 left = 19;
    int32 right = 23;
    return left + right == 42 ? 0 : 1;
}
```

编译并运行：

```sh
build/Re.KrtC/KrtC -O2 main.krt output main
./main
```

优化级别可选 `-O0`、`-O1`、`-O2`、`-O3`，默认是 `-O2`。`output` 后面的路径是生成的可执行文件路径。

## 编译多个源文件

把源码路径作为多个输入传给编译器：

```sh
build/Re.KrtC/KrtC main.krt helper.krt output app
```

类、函数和类型是否能跨文件使用，取决于当前解析、符号合并及链接规则；遇到问题时先用同一编译命令复现，并检查编译器诊断。

## 使用 `project.krt`

C 版驱动可以读取 `project.krt` 中的 `Configure(Project p)` 配置。配置至少要声明源文件和输出名，例如：

```krt
function Configure(Project p) {
    p.Name("Example");
    p.Type("console");
    p.CompileTogether(true);
    p.Sources("main.krt", "helper.krt");
    p.Output("Example");
}
```

在包含 `project.krt` 的目录构建：

```sh
build/Re.KrtC/KrtC build
```

也可以显式指定项目文件：

```sh
build/Re.KrtC/KrtC build path/to/project.krt
```

C 版项目解析器只执行其实现过的配置调用；不能把 SelfHost 的项目字段或行为自动套用到 C 版。实际支持项以 `Re.KrtC/src/Compiler/Driver/ProjectKrt.c` 和 `Re.KrtC/src/Main.c` 为准。

## 数组

```krt
int32 main() {
    int32[] values = new int32[2];
    values[0] = 17;
    values[1] = 25;
    int32 result = values[0] + values[1];
    delete values;
    return result == 42 ? 0 : 1;
}
```

编译器为可确定长度的托管数组和内联定长数组读写生成边界检查。索引必须是整数；负索引和大于等于数组长度的索引会以状态码 `70` 终止程序。普通裸指针索引不带数组长度信息，不会获得这项检查。

## 内存与指针

`new T[n]` 创建托管数组；使用完后应执行 `delete`。`Memory.Allocate` 返回的是裸内存块，没有数组长度元数据，不能当作托管数组传给 `ArrayOps`。指针、`stackalloc` 和底层内存操作需遵循源码中的 `unsafe(using krt.mem;)` 规则；栈分配空间不能在所在函数返回后继续使用。

更多细节见[内存模型](../Spec/Memory_Model.md)。
