# 第九十一批：Re.KrtC/src/Accelerator.c + Accelerator.h

## 覆盖范围

- 全文读完 `Re.KrtC/src/Accelerator.c`（161 行，16 个函数）与 `Re.KrtC/src/Accelerator.h`（29 行，16 条声明）。
- 调用方核对仅用 grep 特定符号：`accelerator_*`、`encode_utf8`、`#include "Accelerator.h"`、`Accelerator.c`（构建）、`KrtError`（KrtCommon.h:14/26）。
- 实际被调用的只有 7 个：`accelerator_hash_string`、`accelerator_is_alpha`、`accelerator_is_digit`、`accelerator_is_octal_digit`、`accelerator_is_hex_digit`、`accelerator_hex_to_int`、`encode_utf8`；调用点集中在 `Compiler/Frontend/Lexer/Tokenizer.c`。
- 构建：`Re.KrtC/build.zig:95` 无条件编译 `src/Accelerator.c`。
- G1 核对：本文件不含 `KRT_MALLOC`/`KRT_STRDUP`/`KRT_REALLOC`/裸 `malloc/free`，分配失败零诊断问题在本文件不触发。
- 未发现：全局/共享可变状态（类别 7）、资源与生命周期（类别 10）、阶段不对齐（类别 4）、构建配置漂移（类别 11）。
- 测试：`Test/` 下 grep `Accelerator|accelerator_|encode_utf8` 零命中。

## 问题条目

### [致命] 91-1｜encode_utf8 不校验码点合法域，静默产出非法 UTF-8
- 位置：`Re.KrtC/src/Accelerator.c:112-144`（关键分支 113-118、134-142）；声明 `Accelerator.h:24`
- 类别：3 语义分叉 / 8 未检查前置条件 / 1 静默失败
- 触发条件：`codepoint` 为负、大于 `0x10FFFF`、或落在代理区 `0xD800-0xDFFF`。
- 后果：负值命中 `codepoint < 0x80` 分支，写成单字节 `0xFF` 等非法序列；超界值走 4 字节分支按移位截断编码；代理区被编成 CESU-8 三字节。`Tokenizer.c:492/501` 直接写入字符串字面量，最终源码中的 `\u`/`\U` 转义变成非法 UTF-8 且无任何诊断。
- 最小修复：入口加 `if (codepoint < 0 || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) { KrtError_LOC(...); return -1; }`。
- 备注：调用方 `\U` 把最多 8 位十六进制解析进 signed `int`（`Tokenizer.c:497-498`）本身会溢出并喂入负值；该溢出属调用方，本条只记 Accelerator.c 侧未设防。

### [高] 91-2｜encode_utf8 未检查 buffer NULL 与 max_length 非正
- 位置：`Re.KrtC/src/Accelerator.c:112-117`（其余分支 119-143 同样无检查）
- 类别：8 未检查前置条件 / 1 静默失败
- 触发条件：`buffer == NULL`，或 `max_length <= 0`。
- 后果：`buffer[0] = ...` 直接解引用 NULL 崩溃；`max_length <= 0` 时各分支静默返回 0，与"空间不足"共用同一返回值，调用方无法区分非法码点与空间不足。
- 最小修复：入口 `if (!buffer || max_length <= 0) { KrtError_LOC(...); return -1; }`。
- 备注：头文件 `Accelerator.h:24` 未声明任何前置条件。

### [高] 91-3｜encode_utf8 空间不足静默返回 0，字符被无声丢弃
- 位置：`Re.KrtC/src/Accelerator.c:114-115、120-121、127-128、135-136`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：剩余容量小于当前码点所需字节数。
- 后果：`Tokenizer.c:492/501` 以 `length += encode_utf8(...)` 累加返回值，拿到 0 后该转义字符直接消失，词法结果与源码不一致且无错误上报。
- 最小修复：空间不足返回 -1 并由调用方报错；至少 `KrtError_LOC`。
- 备注：调用方容量增长只按 2 字节预留（`Tokenizer.c` 字符串读取逻辑），提高了触发概率；本条定位 Accelerator.c 的返回契约。

### [高] 91-4｜accelerator_div_double 除零返回 0，与真实结果不可区分
- 位置：`Re.KrtC/src/Accelerator.c:49-55`
- 类别：1 静默失败 / 3 语义分叉 / 9 错误处理不一致
- 触发条件：`b == 0`（含 `-0.0`）。
- 后果：返回 0 而非 inf/NaN/错误码，调用方拿到看似合法的 0，`a/0` 与 `0/1` 无法区分；`b == 0` 对 `-0.0` 成立却掩盖符号，对 NaN 又不触发。
- 最小修复：按 IEEE 返回 `a/b`（inf/NaN）或改为错误码返回；不要吞掉零除。
- 备注：该函数无任何调用方（见 91-5），但作为导出 API 契约已破坏。

### [高] 91-5｜9 个函数全仓零调用，纯死代码
- 位置：`Re.KrtC/src/Accelerator.c:7,19,37,41,45,49,57,71,146`；`Re.KrtC/src/Accelerator.h:8,9,11,12,13,14,16,17,25`
- 类别：5 死代码
- 触发条件：全仓 grep `accelerator_*`，除定义与声明外无任何调用点。
- 后果：未使用的 `accelerator_strlen`、`accelerator_strcmp`、`accelerator_add_double`、`accelerator_sub_double`、`accelerator_mul_double`、`accelerator_div_double`、`accelerator_memcpy`、`accelerator_memset`、`accelerator_is_whitespace` 仍被 `build.zig:95` 编译进产物，且与 libc 形成语义已分叉的双实现（见 91-6/91-7/91-8）。
- 最小修复：删除未用函数；若刻意保留，必须补契约文档与测试。
- 备注：头文件仍被 10 个源文件包含（`Ast.c:3`、`Parser.c:5`、`ParserBase.c:5`、`ParserExpression.c:5`、`ParserStatement.c:5`、`ParserAdvanced.c:5`、`SemanticAnalyzer.c:20`、`Generics.c:2`、`NameMangling.c:2`、`Tokenizer.c:7`），其中仅 `Tokenizer.c` 真正使用 `accelerator_*`。

### [中] 91-6｜accelerator_memcpy 不处理重叠区域且 NULL 静默返回
- 位置：`Re.KrtC/src/Accelerator.c:57-69`
- 类别：6 契约破坏 / 8 未检查前置条件 / 1 静默失败
- 触发条件：`dest` 与 `src` 内存重叠；或 `dest == NULL`、`src == NULL`。
- 后果：正向逐字节复制在重叠时破坏数据（超出 memcpy 的不重叠契约）；NULL 输入静默返回 `dest`，掩盖错误。
- 最小修复：改用 `memmove`，或在文档中明确禁止重叠；NULL 输入报错。
- 备注：函数未被调用。

### [中] 91-7｜accelerator_memset 对 NULL/零长度静默返回，返回值语义混淆
- 位置：`Re.KrtC/src/Accelerator.c:71-81`
- 类别：9 错误处理不一致 / 3 语义分叉
- 触发条件：`s == NULL` 或 `n == 0`。
- 后果：NULL 时返回 NULL 冒充成功；与 libc `memset` 的未定义行为契约分叉；同文件 `accelerator_memcpy` 的 NULL 分支返回 `dest`，两个同类函数对 NULL 的返回值口径不一致。
- 最小修复：统一为报错或统一返回语义，并写进头文件。
- 备注：函数未被调用。

### [中] 91-8｜accelerator_strlen/strcmp 的 NULL 语义静默且互不一致
- 位置：`Re.KrtC/src/Accelerator.c:7-17`（strlen）、`19-35`（strcmp）
- 类别：1 静默失败 / 3 语义分叉 / 9 错误处理不一致
- 触发条件：`str == NULL`；`strcmp` 单侧为 NULL。
- 后果：`accelerator_strlen(NULL)` 返回 0，与空串不可区分；`accelerator_strcmp(NULL,NULL)` 返回 0，但 `accelerator_strcmp(NULL,"")` 返回 -1，NULL 被当普通字符串参与比较，与 libc 契约分叉。
- 最小修复：NULL 视为错误并 `KrtError_LOC`，或直接删除这两个未被使用的函数。
- 备注：两函数均未被调用。

### [中] 91-9｜accelerator_hex_to_int 非法输入静默返回 0
- 位置：`Re.KrtC/src/Accelerator.c:99-110`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：`c` 不是十六进制字符。
- 后果：返回值 0 与字符 `'0'` 相同，调用方若漏掉 `accelerator_is_hex_digit` 前置判断即静默算错，且无诊断。
- 最小修复：非法输入返回 -1 并同步调整调用方判断，或在头文件明确前置条件。
- 备注：当前调用方 `Tokenizer.c:386/479/488/497` 均有 `is_hex_digit` 守卫，运行路径暂安全；属契约缺陷。

### [中] 91-10｜Accelerator.h 无契约注释、命名不一致，返回 0 语义未定义
- 位置：`Re.KrtC/src/Accelerator.h:8-27`（尤其第 24 行 `encode_utf8`）
- 类别：13 注释与实现脱节 / 6 契约破坏
- 触发条件：任何调用方阅读头文件。
- 后果：`encode_utf8` 不带 `accelerator_` 前缀，与其余 15 个函数命名规则不一致；返回 0（空间不足/非法）与正数长度含义未文档化；NULL 行为未声明，调用方只能靠实现猜。
- 最小修复：补前置条件/返回值/错误语义注释，统一命名前缀。
- 备注：该头文件被 10 个源文件包含，但仅 `Tokenizer.c` 使用其中符号，其余 9 处为无用包含依赖。

### [低] 91-11｜accelerator_is_alpha 仅 ASCII 且把 '_' 当字母，非 ASCII 静默判否
- 位置：`Re.KrtC/src/Accelerator.c:83-85`
- 类别：3 语义分叉 / 13 注释与实现脱节
- 触发条件：`char` 为 `0x80-0xFF`（signed char 下为负值）。
- 后果：`Tokenizer.c:320/586` 用它界定标识符首字符，非 ASCII 标识符被静默截断；`'_'` 判定与调用方 `Tokenizer.c:321/586` 的显式 `c == '_'` 重复。
- 最小修复：文档明确限定 ASCII，或改用 `unsigned char` 并定义 Unicode 策略。
- 备注：该函数在活跃路径上被使用。

### [低] 91-12｜accelerator_is_whitespace 未覆盖 \v/\f 且无任何调用方
- 位置：`Re.KrtC/src/Accelerator.c:146-148`；`Re.KrtC/src/Accelerator.h:25`
- 类别：5 死代码 / 3 语义分叉
- 触发条件：输入 `'\v'` 或 `'\f'`。
- 后果：与 libc `isspace` 分叉；函数全仓零调用。
- 最小修复：删除，或补齐字符集并加测试。
- 备注：与 91-5 的死代码清单重叠，但语义缺陷独立。

### [低] 91-13｜accelerator_hash_string 用有符号 char 累加，高位字节符号扩展
- 位置：`Re.KrtC/src/Accelerator.c:150-161`
- 类别：3 语义分叉 / 8 未检查前置条件
- 触发条件：字符串含 `0x80-0xFF` 字节。
- 后果：`char c` 在 signed char 平台为负，参与 `uint32_t` 加法时符号扩展，结果依赖平台 char 符号性，与按 `unsigned char` 实现的标准 djb2 分叉；`Tokenizer.c:148/166` 用它构建关键字哈希表，属活跃路径。
- 最小修复：改为 `unsigned char c;`。
- 备注：哈希碰撞率未评估，但符号性分叉是确定缺陷。

### [低] 91-14｜Accelerator.c 引入未使用的 <string.h>/<stdlib.h>
- 位置：`Re.KrtC/src/Accelerator.c:2-3`
- 类别：5 死代码 / 13 注释与实现脱节
- 触发条件：编译期。
- 后果：文件无任何 `malloc/free/strlen/memcpy` 调用，这两个 include 是死依赖，容易让人误以为本文件涉及分配。
- 最小修复：删除未使用的 include。
- 备注：G1 相关结论——本文件不做内存分配，分配失败零诊断问题不在此文件触发。

### [低] 91-15｜accelerator_div_double 的 KrtError 缺位置信息
- 位置：`Re.KrtC/src/Accelerator.c:51`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：发生除零。
- 后果：`KrtError`（`KrtCommon.h:14`）只打印消息，不带 `file:line`；项目已有 `KrtError_LOC`（`KrtCommon.h:26`）却未使用，诊断无法定位到具体调用点。
- 最小修复：改用 `KrtError_LOC(__FILE__, __LINE__, "Division by zero in accelerator_div_double")`。
- 备注：与 91-4 相关但独立——一条是返回值语义，一条是诊断质量。

### [中] 91-16｜Accelerator 全模块无测试覆盖
- 位置：`Re.KrtC/src/Accelerator.c:112-161`（`encode_utf8`、`accelerator_hash_string` 等）
- 类别：12 测试不足
- 触发条件：`Test/` 目录 grep `Accelerator|accelerator_|encode_utf8` 零命中。
- 后果：NULL、非法码点、容量不足、除零、有符号 char 哈希等边界均无回归保护，上述缺陷可长期潜伏。
- 最小修复：为 `encode_utf8`（含非法码点/容量不足）与 `accelerator_hash_string` 补单元测试。
- 备注：本批未发现测试目录存在该模块用例。
