# 第九十二批：IrParamTable.c/.h + src/Bytecode/{Bytecode.h,Value.h} + Shared/{Bytecode.h,BytecodeGenerator.h}

## 覆盖范围

逐行读完（read 工具，全量）：

- `Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c`（146 行）
- `Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h`（38 行）
- `Re.KrtC/src/Bytecode/Bytecode.h`（53 行）
- `Re.KrtC/src/Bytecode/Value.h`（52 行）
- `Re.KrtC/Shared/Bytecode.h`（80 行）
- `Re.KrtC/Shared/BytecodeGenerator.h`（15 行）

未通读其他源文件。为核对调用方/定义，仅做了定向 grep：`KrtIrParamTable*`、`KrtIrFunctionFindParam|GetParamIndex|param_table`、`KrtBytecodeGenerator*`、`KrtChunk*|KrtValueArray*|KrtValuePrint`、`KRT_BYTECODE_H|KRT_VM_VALUE_H`、`#include`（仅限定文件的行首匹配）、`KRT_MALLOC|KRT_CALLOC|KRT_FREE|KRT_STRDUP`（定义处）、`-I|include_directories`（CMakeLists.txt）。
`IrInline.inc` / `IrRecursiveInline.inc` 未触碰。

结论概览：**无致命条目**；高 3 条、中 8 条、低 5 条，共 16 条。核心是「两套 Bytecode 定义共用同一 include guard 互相静默屏蔽」与「IrParamTable 整模块死代码 + 返回节点所有权不一致」。

---

## 问题条目

### [高] 92-1｜两个 Bytecode.h 共用 `KRT_BYTECODE_H`，先包含者静默吞掉另一份定义
- 位置：`Re.KrtC/Shared/Bytecode.h:1-2`、`Re.KrtC/src/Bytecode/Bytecode.h:1-2`
- 类别：语义分叉 / 配置漂移 / 契约破坏
- 触发条件：任一 TU 同时（直接或间接）包含这两个头。实测链路：`src/Compiler/Backend/Vm/VmCodegen.h:5-6` 先 `#include "Bytecode.h"` 再 `#include "BytecodeGenerator.h"`；后者在 `Shared/BytecodeGenerator.h:3` 又 `#include "Bytecode.h"`。由于 `CMakeLists.txt:20` 的顺序是 `src src/Core src/Tools src/Bytecode Shared ...`，`VmCodegen.h:5` 命中 `src/Bytecode/Bytecode.h` 并置位 guard，于是 `Shared/Bytecode.h` 的**全部**定义（`KrtOpCode`/`KrtValueType`/`KrtValue`/`KrtValueArray`/`KrtChunk`）被整段跳过。
- 后果：`Shared/BytecodeGenerator.c`（包含自身目录的 `Shared/Bytecode.h`）与编译器侧 TU（实际拿到 `src/Bytecode/Bytecode.h`）使用的是两份文本不同、仅靠人工保持一致的枚举与结构体。当前布局碰巧相同所以能编过；一旦 Shared 侧增删 opcode 或改字段，编译器侧**收不到任何报错**，代码生成器与序列化器对同一 opcode 数值/字段偏移的理解静默分叉，产出错字节码。若将来调整 `-I` 顺序，则反向生效：`src/Bytecode/Bytecode.h` 被整段跳过，连它带进来的 `Value.h` 一并消失，`KrtValue`/`KrtValueType` 直接未定义而编译失败。
- 最小修复：给两个头使用各自唯一的 guard（如 `KRT_SHARED_BYTECODE_H` / `KRT_COMPILER_BYTECODE_H`），并让 `Shared/Bytecode.h` 改为 `#include` 唯一的规范定义；短期至少在两处 guard 加 `#ifndef` 一致性断言（如 `_Static_assert(sizeof(KrtChunk) == …)`）。
- 备注：本批最具扩散性的一条；92-2/92-11 是同一根因的不同侧面。

### [高] 92-2｜`KrtValueType`/`KrtValue`/`KrtValueArray`/`KrtChunk`/`KrtOpCode` 双份定义，无单一真源、无编译期断言
- 位置：`Re.KrtC/Shared/Bytecode.h:48-78` 对照 `Re.KrtC/src/Bytecode/Value.h:6-45`、`Re.KrtC/src/Bytecode/Bytecode.h:8-45`
- 类别：语义分叉 / 契约破坏
- 触发条件：任何同时可见两侧定义的 TU，或任一侧被独立演进（加 opcode、改 `KrtValue` 联合、改 `KrtChunk` 字段顺序）。
- 后果：`Shared/Bytecode.h` 内联重定义了 `KrtValueType`/`KrtValue`/`KrtValueArray`，而不是 include `src/Bytecode/Value.h`；`src/Bytecode/Bytecode.h` 与 `Shared/Bytecode.h` 又各自重定义 `KrtOpCode`/`KrtChunk`。两份定义之间没有任何 `static_assert`/`_Static_assert` 或共享头，枚举值与结构体布局相等只是巧合。92-1 的 guard 冲突进一步保证「编译期永远不会发现」这种漂移。
- 最小修复：抽出一个唯一规范头（含 `KrtOpCode`/`KrtValueType`/`KrtValue`/`KrtValueArray`/`KrtChunk` + 布局与枚举值的 `_Static_assert`），两侧头都改为 include 它；禁止任何一侧再自带定义。
- 备注：与 92-1 同根因，但修复动作不同（合并定义 vs 拆 guard），故独立成条。

### [高] 92-3｜`KrtIrFunctionFindParam` 返回节点所有权不一致，`KrtIrFunctionGetParamIndex` 无条件 `KRT_FREE`
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:117-131`、`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:144`
- 类别：契约破坏 / 资源与生命周期 / 未检查前置条件
- 触发条件：`func->param_table != NULL` 时走 117-119 行，返回的是 `KrtIrParamTableFind` 给出的 **arena 内存**节点；`KrtIrFunctionGetParamIndex` 在 144 行无条件 `KRT_FREE(param)`。该函数同时声明于 `IrParamTable.h:34` 与 `Ir.h:524`，是公开 API。
- 后果：对 arena 指针调用 `KrtSafeFree`（`KrtCommon.h:78`）属于释放非 malloc 内存，行为未定义（轻则报错/计数错乱，重则堆破坏）。当前 `param_table` 全仓从未被赋值（见 92-4），所以只走 121-133 的堆回退分支；一旦按设计接线 `param_table`，这条路径立刻变成内存破坏。附带问题：回退分支每次查找都 `KRT_CALLOC` 一个新节点（123 行），`GetParamIndex` 随即释放，高频查找产生纯浪费的堆 churn；且 `temp_node->name` 直接别名 `func->params[i].name`（127 行），任何调用方若释放/改写 `name` 会破坏 IR 参数表。
- 最小修复：统一所有权——要么 `FindParam` 永远返回 arena/借用节点且**不提供** `GetParamIndex` 的释放语义（改为调用方不释放），要么回退分支也写入 arena 并明确「返回值不可 FREE」；至少把 144 行的释放限制在确认为堆节点的路径，并用 out 参数或独立函数名区分两种来源。
- 备注：这是本批唯一的内存安全类隐患；因 `param_table` 未接线而处于休眠态，故未定致命。

### [中] 92-4｜IrParamTable 整套 API 是死代码，`KrtIRFunction.param_table` 从未被赋值
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h:21-32`、`Re.KrtC/src/Compiler/Middle/Ir/Ir.h:333`
- 类别：死代码 / 阶段不对齐
- 触发条件：全仓 grep `KrtIrParamTableCreate|Add|Find|Count|Foreach` 与 `param_table`，除 `IrParamTable.c/.h` 自身及 `Ir.h:333` 的字段声明、`IrParamTable.c:117-118` 的读取外，无任何赋值或调用；后端 `VmCodegen.c` 直接使用 `func->params` / `val.data.index`。
- 后果：Middle/IR 阶段预留了「参数名→索引哈希表」这一层，但从未接线，形成事实上的双通道：`KrtIrFunctionFindParam` 优先查表、查不到不回退（118 行直接 return），而表又永远为空，于是永远走 121 行的线性扫描。阶段语义与实际实现脱节，后续维护者很容易误以为表已生效而绕过 `func->params`，或误以为扫描路径是死代码而删除。
- 最小修复：二选一——在 IR 构建函数参数时真正 `KrtIrParamTableCreate/Add` 并填充 `func->param_table`；或删除整套表 API 与 `param_table` 字段，只保留线性查找，并同步删掉 `IrParamTable.h` 中相关声明。
- 备注：`KrtIrParamTableDestroy` 的缺失定义（92-5）是这一团死代码的一部分，但因后果不同单独列条。

### [中] 92-5｜`KrtIrParamTableDestroy` 只有声明，全仓无定义、无调用
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h:23`
- 类别：死代码 / 契约破坏
- 触发条件：任何 TU 调用该函数；或链接期符号解析。
- 后果：头文件承诺了析构能力却无实现，一旦有人按对称性在释放 arena 前调用它，就是链接错误（若走弱符号/动态链接则可能运行期失败）。当前无调用方，属未兑现的接口契约，也让 92-4 的「表」看起来比实际更完整。
- 最小修复：要么补上实现（arena 拥有全部节点时该函数应为 no-op，或改由 arena 统一回收并明确标注 no-op），要么删除该声明。
- 备注：与 92-4 独立：即使决定保留参数表，这条声明也必须落实。

### [中] 92-6｜`KrtIrParamTableCreate`：buckets 分配失败静默返回 NULL，且已分配的 table 永久泄漏
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:23-31`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期（G1）
- 触发条件：`KrtIrArenaAlloc` 对 `sizeof(KrtIRParamTable)` 成功、对 `bucket_count * sizeof(KrtIRParamNode*)` 失败（G1：`KRT_MALLOC` 系宏失败返回 NULL 而非 exit，`IrMemory.c:79-100` 亦返回 NULL）。
- 后果：28-31 行直接 `return NULL`，23 行刚分配的 `table` 既不在 arena 的可达链上也被丢弃，无法回收；调用方拿到 NULL，与 14-16 行「参数非法」的 NULL 完全同形，无法区分是配置错误还是内存耗尽，零诊断。
- 最小修复：分配顺序改为先 buckets 后 table；失败时返回前不留下悬空分配，或让 arena 支持回滚/提供 `KrtIrArenaAlloc` 失败计数并向上层传递错误码（而非仅 NULL）。
- 备注：与 92-7 同属 G1 活路径零诊断，但发生位置与泄漏对象不同。

### [中] 92-7｜`KrtIrParamTableAdd`：重名与分配失败都返回 false，且 strdup 失败会泄漏已分配节点
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:47-49`、`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:54-62`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期
- 触发条件：(a) 重名 → 47-49 返回 false；(b) 54 行 `KrtIrArenaAlloc` 失败 → 返回 false；(c) 60 行 `KrtIrArenaStrdup` 失败 → 返回 false，而 54 行刚分配的 `node` 未入桶、不可达。
- 后果：三种性质完全不同的失败（调用方bug / 内存耗尽 / 内存耗尽）对调用方是同一个 `false`，无日志、无错误码，符合「零容忍审计」中的静默失败；情形 (c) 还额外泄漏一个 `KrtIRParamNode`（arena 内存，进程生命周期内不回收）。
- 最小修复：区分返回码（重名用独立错误码/布尔 out 参数），并在 strdup 失败时把已分配 node 记入 arena 的空闲/回收结构，或先 strdup 再 alloc node（顺序对调，失败即无泄漏）。
- 备注：与 92-6 同根（G1），独立成条因泄漏点与可诊断性不同。

### [中] 92-8｜`bucket_count` 移位放大可溢出为负再归零，导致死循环
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:18-21`
- 类别：未检查前置条件 / 溢出
- 触发条件：`initial_bucket_count > 2^30`（如 `0x40000001` 或 `INT_MAX`）。序列 `1,2,…,0x40000000`，下一步 `0x40000000<<1` 在 int 域溢出为 `0x80000000`（负），`负 < 正` 仍成立，再左移得 `0`，此后 `0<<1==0` 恒小于 initial，循环永不退出。
- 后果：进程无限循环（挂死）；即使避开该输入，`28` 行 `bucket_count * sizeof(KrtIRParamNode*)` 也是 int 与 size_t 混合运算，配合 52/80 行的 `hash & (bucket_count - 1)` 依赖「bucket_count 必为 2 的幂」这一未声明不变量。
- 最小修复：用 `size_t`/无符号做放大并设上限（如 `> (1u<<20)` 直接返回 NULL 或报错），或改用 `uint32_t` 并在溢出前判断 `bucket_count > INT_MAX/2`；同时把「bucket_count 是 2 的幂」写成断言。
- 备注：该 API 当前无调用方（92-4），故实际暴露面低，但作为公开头文件里的未检查前置条件必须记。

### [中] 92-9｜`src/Bytecode/Bytecode.h` 的 5 个原型全仓无实现、无调用
- 位置：`Re.KrtC/src/Bytecode/Bytecode.h:47-51`
- 类别：死代码 / 契约破坏
- 触发条件：glob `Re.KrtC/src/Bytecode/*.c` 无结果（该目录没有 .c），grep `KrtChunkInit|Write|Free|AddConstant|AddStringConstant` 除本头声明外零命中。
- 后果：`KrtChunkInit/Write/Free/AddConstant/AddStringConstant` 是一组从未落地、也无人调用的接口；真正被 `VmCodegen.c` / `Compiler.c` 使用的是 `Shared/BytecodeGenerator.h` 的 `KrtBytecodeGenerator*` 系列。两套字节码写入 API 并存，名字近似（`Write` vs `WriteByte`、`AddConstant` vs `GeneratorAddConstant`），极易被误用；一旦误调即链接错误。
- 最小修复：删除 `src/Bytecode/Bytecode.h` 中这 5 个原型（连同 92-10 的 Value.h 原型），或补齐实现并明确哪套是规范 API。
- 备注：与 92-10 分列，因文件与符号集不同。

### [中] 92-10｜`src/Bytecode/Value.h` 的 4 个原型全仓无实现、无调用
- 位置：`Re.KrtC/src/Bytecode/Value.h:47-50`
- 类别：死代码 / 配置漂移
- 触发条件：grep `KrtValueArrayInit|Write|Free|KrtValuePrint` 除本头声明外零命中；`src/Bytecode` 无 .c。
- 后果：同 92-9，孤儿接口。附加耦合：`Value.h:4` 写的是 `#include "Core/Utils/KrtCommon.h"`，该路径只有把 `src` 加入 include 路径才解析得到（`CMakeLists.txt:20` 恰好有 `src`）；单独包含此头或改变 `-I` 顺序即失败。`bool` 也依赖 `KrtCommon.h:87` 的 `<stdbool.h>` 传递进来，而 `Shared/Bytecode.h:6` 是自己包含 stdbool 的——同义类型的头在依赖方式上已经分叉。
- 最小修复：删除这组原型；若确需保留，改为 `#include "../Core/Utils/KrtCommon.h"` 之类的自洽相对路径并显式包含 `<stdbool.h>`。
- 备注：与 92-9 独立成条。

### [中] 92-11｜序列化契约只存在于 .c，头文件无魔数/版本/格式约定，且两侧 `KrtChunk` 无布局断言
- 位置：`Re.KrtC/Shared/BytecodeGenerator.h:14`
- 类别：契约破坏 / 语义分叉
- 触发条件：跨编译单元/跨版本读写字节码；或 92-1/92-2 的任一定义漂移发生。
- 后果：`KrtBytecodeGeneratorSerializeToFile` 在头文件里没有任何格式说明，实际格式（magic `0x45534243`、version `1`，见 `Shared/BytecodeGenerator.c:84-87`）只存在于 .c；`KrtChunk` 结构里也没有版本/魔数字段。写端用 `Shared/Bytecode.h` 的类型尺寸（`sizeof(KrtValueType)`、`int`、`double`），读端若用 `src/Bytecode/Value.h` 那份定义，只要 enum/int 尺寸或字段顺序不同就静默错位，且没有任何编译期或运行期校验能发现。
- 最小修复：把 magic/version/各字段宽度提为头文件常量与结构体头部字段，写端先写 magic+version，读端校验；并对 `KrtChunk`/`KrtValue` 尺寸做 `_Static_assert`。
- 备注：证据来自定向 grep `Shared/BytecodeGenerator.c` 的 `fwrite|magic|version`，未通读该 .c。

### [低] 92-12｜`KrtBytecodeGeneratorCreate/Destroy/WriteShort` 有定义无调用，opaque 句柄完全未使用
- 位置：`Re.KrtC/Shared/BytecodeGenerator.h:6`、`Re.KrtC/Shared/BytecodeGenerator.h:7`、`Re.KrtC/Shared/BytecodeGenerator.h:10`
- 类别：死代码
- 触发条件：grep 全仓，`KrtBytecodeGeneratorCreate` 仅命中 `Shared/BytecodeGenerator.c:13` 定义；`KrtBytecodeGeneratorDestroy` 仅命中 `.c:21` 定义；`KrtBytecodeGeneratorWriteShort` 仅命中 `.c:48` 定义。实际调用的是 chunk 版 API（`VmCodegen.c`、`Compiler.c:39,53,239`）。
- 后果：`KrtBytecodeGenerator` 这个 opaque 类型没有任何真实使用者，Create/Destroy 构成永远配不上对的死接口；`WriteShort` 是无人调用的工具函数，容易被误以为字节码里存在 16 位指令编码。
- 最小修复：删除句柄版 Create/Destroy 与 `KrtBytecodeGenerator` 类型，或真正改为句柄式 API 并迁移调用方；删除 `WriteShort`。
- 备注：与 92-9/92-10 同属「孤儿接口」，但位于 Shared 侧且有实现，故独立。

### [低] 92-13｜`KrtIrFunctionGetParamIndex` 以 -1 作未找到哨兵，无文档、无调用方检查
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:138-146`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：调用方拿到返回值直接当索引用（如 `params[idx]`、`locals[idx]`）。当前全仓无调用方。
- 后果：`-1` 与合法索引 `int` 同型，头文件（`IrParamTable.h:36`、`Ir.h:525`）均未说明 -1 语义；一旦接入调用方而漏判，就是负索引越界读写。同时该函数的「查找-释放」把 92-3 的所有权陷阱埋在返回值里。
- 最小修复：改为 `bool KrtIrFunctionGetParamIndex(KrtIRFunction*, const char*, int* out_index)`，或在头文件明确标注 `-1 = 未找到` 并要求调用方检查。
- 备注：因当前无调用方记为低。

### [低] 92-14｜`KrtIrFunctionFindParam` / `KrtIrFunctionGetParamIndex` 在两个头里重复声明
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.h:34-36`、`Re.KrtC/src/Compiler/Middle/Ir/Ir.h:524-525`
- 类别：契约破坏 / 注释与实现脱节（声明漂移）
- 触发条件：两处声明分别演进（例如一侧改参数名/加 const/改返回类型）；或某 TU 只包含其中之一而未走 `Ir.h:6` 的包含链。
- 后果：`Ir.h` 已 include `IrParamTable.h`（`Ir.h:6`），524-525 行属冗余重声明。同一对函数两处独立维护，签名漂移时编译器只在「同一 TU 同时看到两处」时才报 redefinition 冲突，分属不同 TU 时无任何约束，容易静默通过。
- 最小修复：删除 `Ir.h:524-525` 的重复声明，统一由 `IrParamTable.h` 提供。
- 备注：低危但属零容忍项下的契约重复。

### [低] 92-15｜`KrtIrParamTableForeach` 遍历顺序未定义且不稳定
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrParamTable.c:103-109`
- 类别：契约破坏 / 共享可变状态
- 触发条件：任何按遍历顺序消费结果的调用方（例如用返回顺序决定参数布局或代码生成顺序）。当前无调用方。
- 后果：顺序 = 桶下标升序、桶内因 66 行头插法而成「后插入者先出」，既未在头文件（`IrParamTable.h:31-32`）说明，也不保证与 `index` 字段一致；同一逻辑参数集合在不同插入顺序/不同 bucket_count 下产出不同序列，跨运行不可复现。
- 最小修复：头文件明确「遍历顺序未指定」，或改为按 `index` 排序后回调（新增数组/排序成本），或提供 `KrtIrParamTableFindByIndex`。
- 备注：低危，因无调用方。

### [低] 92-16｜本批 6 个文件零测试覆盖
- 位置：`IrParamTable.c/.h`、`src/Bytecode/Bytecode.h`、`src/Bytecode/Value.h`、`Shared/Bytecode.h`、`Shared/BytecodeGenerator.h`（整体）
- 类别：测试不足
- 触发条件：全仓 grep `KrtIrParamTable*`、`KrtChunk*`、`KrtValueArray*`、`KrtBytecodeGenerator*`，命中仅各定义/声明文件自身与 `VmCodegen.c`、`Compiler.c`、`Ir.h` 等生产代码，无任何测试文件。
- 后果：92-3 的所有权陷阱、92-6/92-7 的分配失败路径、92-8 的放大边界、92-1/92-2 的双定义一致性、92-11 的序列化 round-trip 均无回归保护；恰恰是最需要测试的失败路径完全没有覆盖。
- 最小修复：至少补：哈希表插入/重名/查找用例；`KrtIrFunctionGetParamIndex` 对 heap 与 arena 两种来源的释放行为用例；`SerializeToFile` 写后读回 round-trip 与版本不符拒绝用例；两份 Bytecode 头的 `_Static_assert` 布局一致性检查。
- 备注：测试基础设施未在本次允许范围内核查，仅陈述符号级无命中这一事实。
