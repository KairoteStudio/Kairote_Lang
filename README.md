<div align="center">

Kairote Programming Language

<svg><g><text x="18" y="1.5" font-family="monospace" font-size="8" fill="#00FF88">BY KairoteStudio</text></g></svg>

# **Kairote Programming Language**

### *系统级编程语言 | 编译速度比你想的快亿点点*

[![License: MPL](https://img.shields.io/badge/License-MPL-yellow.svg?style=for-the-badge)](https://opensource.org/licenses/MPL)
[![Stars](https://img.shields.io/github/stars/KairoteStudio/Kairote_Lang?style=for-the-badge&logo=github&color=blue)](https://github.com/KairoteStudio/Kairote_Lang/stargazers)
[![Forks](https://img.shields.io/github/forks/KairoteStudio/Kairote_Lang?style=for-the-badge&logo=github&color=orange)](https://github.com/KairoteStudio/Kairote_Lang/network/members)
[![Issues](https://img.shields.io/github/issues/KairoteStudio/Kairote_Lang?style=for-the-badge&logo=github&color=red)](https://github.com/KairoteStudio/Kairote_Lang/issues)
[![Last Commit](https://img.shields.io/github/last-commit/KairoteStudio/Kairote_Lang?style=for-the-badge&logo=git&color=green)](https://github.com/KairoteStudio/Kairote_Lang/commits)
[![Code Size](https://img.shields.io/github/languages/code-size/KairoteStudio/Kairote_Lang?style=for-the-badge&logo=github&color=purple)](https://github.com/KairoteStudio/Kairote_Lang)
[![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Python](https://img.shields.io/badge/Python-3776AB?style=for-the-badge&logo=python&logoColor=white)](https://www.python.org/)
[![Made by Kairote](https://img.shields.io/badge/Made%20by-Kairote%20Studio-00d4aa?style=for-the-badge)](https://none/)

[官网](https://none/) | [文档](https://none/) | [讨论区](https://github.com/KairoteStudio/Kairote_Lang/discussions) | [提交问题](https://github.com/KairoteStudio/Kairote_Lang/issues/new/choose)

</div>

---

## 概述

**KairoteLang** 是一个由 **Kairote Studio** 开发的现代系统级编程语言，专为追求极致性能与优雅语法的开发者而生。

> **我们的愿景**：打造一款兼具 C++ 性能(但目前仅在部分领域上赶超)、C# 优雅、Rust 安全(当前阶段尚未实现)的编程语言，让系统编程变得简单而强大。

### 为什么选择 KairoteLang？

| 特性 | 描述 |
|------|------|
| **极致性能** | 自研编译器 toolchain，零成本抽象，编译速度极快 |
| **优雅语法** | 基于 C# 语法扩展，学习曲线平缓，表达能力强大 |
| **自研工具链** | 独立实现的前端、SSA 形式的 IR 及多目标后端代码生成 |
| **现代架构** | 模块化设计，支持增量编译和并行构建 |
| **类型安全** | 静态类型系统，编译期捕获错误 |

---

## 预览

```KrtL
// KairoteLang 代码示例
Namespace HelloWorld;

using System;

public class Program
{
    public static void Main(string[] args)
    {        
        Console.WriteLine("Hello, KairoteLang!");
    }
}
```

---

## 快速开始

### 环境要求

- **操作系统**: Linux | Windows | macOS
- **Zig**: 0.17.0+ (作为 C 编译器和构建系统)
- **C 标准库**: 需要 `libm` 和 `libpthread`

### 安装

```bash
# 克隆仓库
git clone https://github.com/KairoteStudio/Kairote_Lang.git
cd Kairote_Lang
```

### 构建项目

```bash
# 构建 Re.KrtC 编译器（使用 zig）
cd Re.KrtC
zig build
```

> 编译器依赖 `libarklink`。如果 `../ArkLink/build/libarklink.a` 不存在，需先构建 ArkLink：
> ```bash
> cd ../ArkLink && cmake -S . -B build && cmake --build build
> ```

### 编写你的第一个 KairoteLang 程序 或 [学习文档](./docs/KrtC/doc/README.md)

创建 `hello.krt` 文件：

```KrtL
using System

Namespace Hello;

public class Program
{
    public static void Main()
    {
        Console.WriteLine("Hello, KairoteLang!");
    }
}
```

编译并运行：

```bash
KrtC build hello.krt
./hello
```

---

## 架构概览

```
┌─────────────────────────────────────────────────────────────┐
│                    KairoteLang Toolchain                    │
├─────────────┬─────────────┬─────────────────────────────────┤
│   Re.KrtC   │   ArkLink   │   Runtime                       │
│  编译器      │   链接器    │   运行时                         │
├─────────────┼─────────────┼─────────────────────────────────┤
│   词法分析   │  COFF/ELF   │  内存管理                        │
│   语法分析   │    PE后端   │  标准库                          │
│   语义分析   │   符号解析   │  异常处理                        │
│   SSA IR    │   重定位     │                                  │
│   代码生成   │   库文件     │                                  │
└─────────────┴─────────────┴─────────────────────────────────┘
```

### 项目结构

```
Kairote_Lang/
├── Re.KrtC/                # 编译器核心 (KrtC → Re.KrtC)
│   ├── src/
│   │   ├── Core/           # 基础库（内存、平台、工具）
│   │   ├── compiler/       # 编译器前端、中端、后端
│   │   ├── Bytecode/       # 字节码定义
│   │   └── Tools/          # 开发工具
│   ├── Shared/             # 共享代码生成器
│   ├── stub_include/       # 存根头文件
│   └── build.zig           # Zig 构建脚本
├── ArkLink/                # 链接器
│   ├── src/                # 核心链接逻辑与后端
│   └── include/            # 头文件
├── libs/                   # 标准库 (.krt)
└── docs/                   # 文档
```

---

## 技术特性

### 编译器 (KrtC)

- **前端**: 递归下降语法分析器，支持完整的 C# 语法子集
- **中端**: SSA 形式的中间表示，支持多种优化 passes
- **后端**: 多目标代码生成（x86, x64, ARM）

### 链接器 (ArkLink)

- 支持 COFF、ELF、PE 格式
- 增量链接支持
- 符号版本控制
- 死代码消除

---

## 路线图

- [x] 基础编译器框架
- [x] 链接器实现
- [x] SSA IR 中端
- [x] x86 / Kro 后端
- [ ] 标准库完善
- [ ] 包管理器
- [ ] IDE 插件
- [ ] JIT 编译支持
- [ ] 跨平台优化

---

## 参与贡献

我们欢迎所有形式的贡献！无论是提交 Bug 报告、功能建议，还是代码贡献。

### 如何贡献

1. **Fork** 本仓库
2. 创建你的 **Feature Branch** (`git checkout -b feature/Feature`)
3. **Commit** 你的更改 (`git commit -m 'Add some Feature'`)
4. **Push** 到分支 (`git push origin feature/Feature`)
5. 打开一个 **Pull Request**

### 提交规范

- 使用清晰的提交信息描述变更
- 确保代码通过现有测试
- 新增功能请附带测试用例
- 遵循现有代码风格

[查看贡献指南](./DevStand.md)

[提交 Bug 报告](https://github.com/KairoteStudio/Kairote_Lang/issues/new?template=bug_report.md)

[提交功能建议](https://github.com/KairoteStudio/Kairote_Lang/issues/new?template=feature_request.md)

---

## 许可证

本项目采用 [MPL 2.0 License](LICENSE) 开源。

---

## 相关项目
- None

---

## 联系我们

<div align="center">

| 渠道 | 链接 |
|------|------|
| 邮箱 | [?@?.?](?@?.?) |
| QQ 群 | [791809691](https://jq.qq.com/?_wv=1027&k=193021037) |
| GitHub | [https://github.com/KairoteStudio_Lang](https://github.com/KairoteStudio/Kairote_Lang) |
</div>

---

## 致谢

### 技术参考

- [LLVM](https://llvm.org/) - SSA 设计理念
- [C# Language Specification](https://docs.microsoft.com/en-us/dotnet/csharp/) - 语法基础
- [GCC](https://gcc.gnu.org/) - 代码生成参考
- [Rust](https://www.rust-lang.org/) - 类型系统灵感

### 贡献者

感谢所有为这个项目做出贡献的开发者！

<a href="https://github.com/KairoteStudio/Kairote_Lang/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=KairoteStudio/Kairote_Lang" />
</a>

---

<div align="center">

### 如果这个项目对你有帮助，请给我们一颗Star！

**Made with ❤️ by Kairote Studio**

</div>
