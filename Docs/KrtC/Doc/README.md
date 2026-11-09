# Re.KrtC 文档

这里记录仓库中的 C 版编译器 `Re.KrtC` 及其当前库接口。它和 Kairote 编写的 SelfHost 编译器是两个独立实现；语法或后端能力不能仅凭其中一方的文档推断另一方也已支持。SelfHost 的构建与状态见 [SelfHost 文档](../../../SelfHost/README.md)。

## 文档导航

- [用户手册](Guide/User_Manual.md)：构建编译器、编译单文件和使用 `project.krt`。
- [语言实现说明](Spec/Lang_Spec.md)：当前 C 编译器的类型、数组、指针和控制流基线。
- [内存模型](Spec/Memory_Model.md)：堆数组、字符串、裸指针和显式释放。
- [标准库 API 概览](Api/Api_Reference.md)：只列仓库中实际存在的 `libs/System` 接口。
- [示例与教程](Examples/Tut&Example.md)：可用原生编译器构建的短示例。
- [开发者指南](Guide/Dev_Guide.md)：源码位置、修改路径和验证方法。

## 构建与运行

在仓库根目录构建 C 编译器和链接器：

```sh
cmake -S . -B build
cmake --build build --target KrtC ArkLink -j4
```

编译单文件并运行：

```sh
build/Re.KrtC/KrtC -O2 hello.krt output hello
./hello
```

编译器命令及项目配置以当前源码为准。本文档不把设计草案、尚未接通的 API 名称或 SelfHost 的实现状态描述成 Re.KrtC 已有能力。
