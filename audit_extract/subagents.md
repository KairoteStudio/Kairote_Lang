# 子代理审计抽取

> **数据来源与限制（先读）**
> - 来源仅为 6 份可读转录 `subagents/rollout-2026-11-09T23-35-*.transcript.md`（用户/助手可见消息导出，**不含工具输出**）；按指示未读同名 `.jsonl`，未读项目源码。
> - 主会话转录（`审查编译器全量代码.transcript.md:6007-6051`）显示：6 个子任务全部因 **HTTP 402 Payment Required**（LongCat 上游 `Usage limit reached`）在 2026-11-09T16:59:43Z–17:01:57Z 之间报错终止，主会话只收到 6 条 `errored` 通知，**没有收到任何子代理结果**。
> - 因此 6 个子代理**均无最终结论、均无编号问题条目**（无 S 编号可保留）。下文如实标注，并只保留转录中明确写出的事实性发现／关键排除项（标注「转录:行」以便与源码行号区分）。
> - 本文件不涉及主会话自有批次（S1–S57，见 `audit_extract/chunk_*.md`）；G2（S28-1 作废）、G3（S38-1~S38-15）在这 6 份转录中无对应条目，无可应用对象。G1 已用于 Faraday 的过程结论。

---

## Feynman（IR CSE/内联）

转录：`rollout-2026-11-09T23-35-22-01a1c0f3-257d-7421-b851-f34ab8267d61.transcript.md`

### 覆盖范围

- `Re.KrtC/src/Compiler/Middle/Ir/IrPureCse.inc`（467 行，重点 :300-467）
- `Re.KrtC/src/Compiler/Middle/Ir/IrRecursiveInline.inc`（588 行）
- `Re.KrtC/src/Compiler/Middle/Ir/IrInline.inc`（283 行）
- 重点：局部值编号正确性（`cse_hash_value`/`cse_equal_value` 是否覆盖所有 `KrtIRValueType`、IMM_F/INTEGER 位模式）、代数改写语义（有符号/无符号、溢出）、递归内联深度限制、内联后符号/作用域重命名、混合分配器、函数纯度分析调用图、`KRT_CSE_BLOCK_LIMIT` 超限处理。

### 问题条目

- **无最终结论**：转录止于「我将基于交接总结继续…」（转录:91）与「I'll start by reading the three target files and confirming callers with grep.」（转录:97），报告从未产出。
- 无编号条目。

### 过程发现（未成条，无编号/严重度）

- **[关键排除项] PHI 操作数存放方式已澄清**（转录:75）：PHI 操作数确认存放在 `inst->operands` 中（`Ir.c:846-848` 经 `ir_add_operand`），因此 `eliminate_unused_integers` **不会**误删仅被 PHI 引用的 temp。属排除项，非缺陷。
- **[未完成] 后端 ADD/MUL 溢出语义与 `KrtIRValueIsValid`**（转录:75）：中断前正在核查，无结论。
- 其余重点项（内联递归无界→栈溢出、`x+x→x*2` 语义保持、`KRT_CSE_BLOCK_LIMIT` 超限、混合分配器、内联后重命名）在转录中**未见任何明确判断**。

---

## Helmholz（KroCodegen.c）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-26ef-7aa3-80ce-ae8bbbf28da4.transcript.md`

### 覆盖范围

- `Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c`（1622 行）全量。
- 重点：指令选择、栈帧布局、调用约定、寄存器分配接口、与 `KrtIRBasicBlock` 双表示的一致性、字段槽宽假设（S34-11）、调试 `fprintf` 残留、`:1296` 被注释掉的 `KrtIrSsaOptimize` 的影响、死代码、未检查前置条件、整数溢出、栈深度。

### 问题条目

- **无最终结论**：转录止于「Now let me verify the remaining candidate findings with exact line numbers.」（转录:125），报告从未产出。
- 无编号条目。

### 过程发现（未成条，无编号/严重度）

- **[关键排除项] 浮点比较掩码正确**（转录:75）：KroCodegen.c 的浮点比较掩码实际是正确的（`and al, dl`），此前疑点排除，非缺陷。
- **[未完成] 间接 load/store 与 16 字节路径**（转录:77）：正在核查 `emit_load_indirect`/`emit_store_indirect` 及 16 字节路径，无结论。
- **[未完成] 字符串转换 opcode**（转录:113）：中断前把「字符串转换 opcode」列为最高优先级待验证项，无结论。
- **[信息不明] 一条「关键可达性事实」已确认**（转录:119）：原文只说「Confirmed a critical reachability fact」，未写明是哪条事实，无法转述为条目；随后计划跑 Kro 后端端到端测试。
- 其余重点项（字段槽宽、fprintf、SsaOptimize 注释影响、死代码、整数溢出、栈深度）**未见任何明确判断**。

---

## James（Kro 整数/寄存器打包/尾调用/浮点转换）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-27a4-7931-b264-198d3b4238a1.transcript.md`

### 覆盖范围

- `Re.KrtC/src/Compiler/Backend/Kro/KroInteger.inc`（898 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc`（608 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc`（364 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc`（264 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc`（182 行）
- `Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc`（123 行）
- 重点：`KroRegisterPacking.inc:265-289` 的 position 计算溢出/越界、寄存器打包边界、尾调用栈帧复用（双重释放/悬垂）、浮点转换精度、128 位整数运算、IR 操作数类型对应、未检查前置条件。

### 问题条目

- **无最终结论**：转录止于「Let me read the remaining parts of `KroInteger.inc` and check key helper usages.」（转录:110），报告从未产出。
- 无编号条目。

### 过程发现（未成条，无编号/严重度）

- **[关键排除项] 寄存器号越界疑点排除**（转录:62）：寄存器枚举 0–15，`next_write[16]` 与 `1u<<reg` 的越界疑点可排除。非缺陷。
- **[关键排除项] `integer_condition` 分支方向为误读**（转录:104）：`leaf_index ? condition : condition^1` 表示 leaf_index=0 时取 `condition^1`，是**正确**的；guard 与 backedge 均正确，`-0x10`/`-0x20` 只是 rel32/rel8 编码。此前「致命分支方向反转」线索不成立。
- **[未完成] `KroInteger.inc` 剩余部分与关键 helper 用法**（转录:110）：中断前正在读，无结论。
- 其余重点项（position 溢出/越界、尾调用栈帧复用、浮点转换精度、128 位整数）**未见任何明确判断**。

---

## Curie（Tokenizer/名称改写/枚举指针解析）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-2882-7703-8e9a-804aad726a7f.transcript.md`

### 覆盖范围

- `Re.KrtC/src/Compiler/Frontend/Lexer/Tokenizer.c`（1232 行）
- `Re.KrtC/src/Compiler/Frontend/Semantic/NameMangling.c`（389 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/EnumParser.inc`（163 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/PointerTypes.inc`（152 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserBase.c`（141 行）
- 重点：mangling 冲突、tokenizer 状态机（转义/注释/字符串/数字/Unicode）、错误恢复、诊断位置、缓冲区边界、共享可变状态、指针算术溢出。

### 问题条目

- **无最终结论**：转录仅两条进度消息（转录:53「先核验 harness 与关键发现再产出最终报告」、转录:59「核实文件现状与已有 harness，再补齐全量逐行审查并输出报告」），无任何发现内容，报告从未产出。
- 无编号条目，也无过程发现可保留。

---

## Faraday（AST/访问器/表达式高级解析）

转录：`rollout-2026-11-09T23-35-23-01a1c0f3-295c-78c1-b7d4-8ff67e5f8fb4.transcript.md`

### 覆盖范围

- `Re.KrtC/src/Compiler/Frontend/Parser/Ast.c`（570 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/AstVisit.inc`（409 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserExpression.c`（898 行）
- `Re.KrtC/src/Compiler/Frontend/Parser/ParserAdvanced.c`（266 行）
- 重点：AST 生命周期/别名（节点共享/释放）、访问器正确性、表达式优先级与结合性、类型转换、错误恢复、诊断位置、未检查前置条件、递归无界。

### 问题条目

- **无最终结论**：转录止于「我将继续完成审计工作。先读取关键支撑文件以确认前批发现，避免误报。」（转录:136），报告从未产出。
- 无编号条目。

### 过程发现（未成条，无编号/严重度）

- **[过程结论，与 G1 一致] 分配器契约与任务给定的「全局事实」冲突，已按真实代码确认**（转录:88-89、118、124）：`KRT_MALLOC` 系列在 OOM 时**返回 NULL 且不 exit**，`KrtMemoryReportError` 是空操作 → 分配失败是**活路径且零诊断**。该子代理明确表示与给定全局事实冲突、将按真实代码分类。对应原判「死代码」的 `if (!ptr) return NULL;` 应改标为「静默失败/未检查前置条件（OOM NULL 解引用或静默 NULL 传播）」——即 **G1**。
- **[未完成] 未解析 token、死状态、IrGen 分支的交叉验证**（转录:130）：中断前正在进行，无结论。
- 其余重点项（AST 别名/生命周期、访问器、优先级与结合性、递归无界）**未见任何明确判断**。

---

## Pasteur（ParserStatement.c）

转录：`rollout-2026-11-09T23-35-24-01a1c0f3-2a43-7fd2-afa4-b91120ebcfcd.transcript.md`

### 覆盖范围

- `Re.KrtC/src/Compiler/Frontend/Parser/ParserStatement.c`（1674 行）全量。
- 重点：语句解析（if/while/for/switch/try/catch/finally/throw/class/struct/enum/unsafe）、错误恢复、诊断位置、作用域处理、未检查前置条件、递归无界、与 `IrGen.c` 的契约（AST_PROPERTY_DECLARATION/AST_CONSTRUCTOR_DECLARATION/AST_DESTRUCTOR_DECLARATION/AST_ACCESS_MODIFIER 是否被 IrGen 处理）、死代码。

### 问题条目

- **无最终结论**：转录仅两条进度消息（转录:38「接手继续完成审计，先确认目标文件的真实行号与关键代码」、转录:54「接手继续完成剩余验证项，然后输出完整报告」），无任何发现内容，报告从未产出。
- 无编号条目，也无过程发现可保留。

---

## 汇总

| 子代理 | 范围 | 最终结论 | 编号条目 | 过程发现 |
| --- | --- | --- | --- | --- |
| Feynman | IR CSE/递归内联/普通内联 | 无（402 中断） | 0 | 2（1 排除项 + 1 未完成） |
| Helmholz | KroCodegen.c | 无（402 中断） | 0 | 4（1 排除项 + 3 未完成/不明） |
| James | Kro 整数/寄存器打包/尾调用/浮点转换 | 无（402 中断） | 0 | 3（2 排除项 + 1 未完成） |
| Curie | Tokenizer/名称改写/枚举指针解析 | 无（402 中断） | 0 | 0 |
| Faraday | AST/访问器/表达式高级解析 | 无（402 中断） | 0 | 2（1 过程结论=G1 + 1 未完成） |
| Pasteur | ParserStatement.c | 无（402 中断） | 0 | 0 |

补充说明：

- 6 份转录都是「可见消息」级导出，工具输出与命令结果不在其中；子代理在断线前完成的逐行阅读与 grep 证据只存在于同名 `.jsonl`（本次按指示未读）。若需要更细的中间判断，只能从 jsonl 提取。
- 全局修订中 G1 已在 Faraday 条目上标注；G2、G3 在本批无对应编号条目。
