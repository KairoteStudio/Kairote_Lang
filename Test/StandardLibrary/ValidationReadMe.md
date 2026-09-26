# 基础库验证记录

2026-09-13，基于 `origin/main` 的 `76aaf4f` 验证本次基础库及配套编译器修复。

| 检查 | 结果 |
| --- | --- |
| Zig 与 CMake Release 构建 | 通过 |
| ASan/UBSan 编译器构建 | 通过 |
| 基础库 59 项测试 | 普通版、插桩版各覆盖 O0/O1/O2/O3，全部通过 |
| 既有整数、指针、语言、性能与语法回归 | 普通版、插桩版各覆盖 O2/O3，每组 256 项，共 1024 项通过 |
| C 组件回归 | 6 组在 ASan/UBSan 下通过 |
| 原生质量与规范检查器回归 | 15 项通过 |
| GCC / Clang 诊断 | 各 65 个翻译单元，零警告、零错误 |
| 本次增量 DevStand 检查 | 12 个 C/头文件/include 文件，零违规、无超长函数报备 |
| 库接口和提交范围审查 | 公开 API 契约齐全，无新增生成产物，空白检查通过 |

基础库测试通过真实导入构建，覆盖范围及运行命令见 [回归说明](ReadMe.md)。
额外验证包括数字/字符串及混合整数重载、ref 精确匹配、数组表达式的地址和步长、
类内全局函数签名、条件提前返回、传递依赖和纯数据模块。

普通编译器 SHA-256：`df5dc3f1e8cec02c4bc7431104e6f7cb28b78038e9a560859c6626d3459e9b65`。

插桩编译器 SHA-256：`b03c45d62b3e2279a49e1f9fe9c3498a688d3bd4663a97d44e7c6b036ea1f0e9`。

最终回归报告的编译器哈希与上述构建一致；GCC/Clang 审计记录的源码哈希也与提交源码一致。
Sanitizer 检查编译器、链接器及 C 组件，生成程序通过执行断言、边界和预期错误测试验证，
未启用 LSan。本轮没有重新测量性能耗时。

```sh
python3 Test/Performance/LLVMOptimization/Validate.py --output-dir /tmp/kairote-stdlib-final-validation
python3 Test/Performance/LLVMOptimization/Validate.py --compiler /tmp/KrtC-stdlib-sanitized \
    --sanitized --output-dir /tmp/kairote-stdlib-final-validation
python3 Test/Quality/RunTests.py
python3 Test/Quality/Audit.py --jobs 4 --output /tmp/kairote-stdlib-audit.json
python3 Test/Quality/CheckStandards.py --base origin/main
```

原始日志和 JSON 位于本地 `/tmp/kairote-stdlib-*`，不加入提交。
