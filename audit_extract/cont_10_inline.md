# 第六十七批：IrInline.inc + IrRecursiveInline.inc

## 覆盖范围

- 全量分段读取（read，非通读其他文件）：
  - `Re.KrtC/src/Compiler/Middle/Ir/IrInline.inc`，共 283 行（1-150 / 151-283）。
  - `Re.KrtC/src/Compiler/Middle/Ir/IrRecursiveInline.inc`，共 588 行（1-150 / 151-300 / 301-450 / 451-588）。
- 调用方用 grep 核实（只查符号，未整文件通读）：
  - `IrOptimizer.c:621-623` include 两个 `.inc`；`IrOptimizer.c:626` 调 `inline_small_functions`，`:628` 调 `inline_recursive_functions`，二者同属 `ir_optimize_function_inlining`（`:625-633`）。
  - `ir_optimize_function_inlining` 只被 `ir_optimize_module`（`IrOptimizer.c:647-649`）在 `OPT_FUNCTION_INLINING` flag 下调用，位置在 `while (changed && pass < KRT_MAX_OPTIMIZATION_PASSES)` 迭代循环（`:653-684`）**之前**，全模块仅执行一次。
  - `KrtIrSsaOptimize` 仅在 `IrSsa.h:114` 声明、`IrSsa.c:1497` 定义；调用方只有 `VmCodegen.c:220`、`X86Codegen.c:1019`，`KroCodegen.c:1296` 已注释禁用。**中端（含两个内联器）从不构造 SSA**，与背景结论一致；内联器运行时模块不在 SSA 形态，`IrRecursiveInline.inc:548-549` 的 probe CSE 是在无支配信息的 CFG 上按块局部运行。
- 辅助定点核对（小窗口/单符号，用于判定不变量与根因）：`Ir.h:281-343`（KrtIRInst/KrtIRBasicBlock/KrtIRFunction 字段）、`IrOptimizer.c:67-90`（`is_pure_operation`）、`IrPureCse.inc:30-186/249-344/390-467`（CSE 与未使用整数）、`Ir.c:406-412`（`KrtIrInteger`）。
- 内存 API：两文件全部使用 `KRT_MALLOC/KRT_REALLOC/KRT_CALLOC/KRT_STRDUP/KRT_FREE`，未发现裸 `malloc/free/realloc/calloc/strdup` 混用（此点合格）。

## 问题条目

### [致命] 67-1｜DIV/MOD 被当作纯操作，trap 语义被改变
- 位置：`IrRecursiveInline.inc:98`（配合 `IrOptimizer.c:67-90`；`IrInline.inc:33` 同因）
- 类别：语义分叉；注释与实现脱节
- 触发条件：候选函数体含 `KRT_IR_DIV` / `KRT_IR_MOD`（含除数为常量 0 或运行时 0 的情形）。
- 后果：`is_pure_operation` 把 DIV/MOD 列为纯操作，递归展开会把除法复制到每个展开路径，且 probe 里的 `eliminate_pure_expressions` 会把同参数重复除法 CSE 成 `COPY`，从而消除原本会发生的第二次 trap（除零/溢出）。文件头注释声称 “nonlocal effects are deliberately excluded”，但 trap 是可观测效应；同仓库 `IrPureCse.inc:383-384` 明确把 DIV/MOD 排除出可删除指令，反证二者在此代码库中被视为可 trap。
- 最小修复：在 `is_pure_operation` 中移除 DIV/MOD，或在 `:98` 与 `:33` 两处资格判定中显式排除这两个 opcode。
- 备注：`IrInline.inc:33` 会把含除法的小函数整体内联，问题同构。

### [致命] 67-2｜recursive_value 未命中静默回退，提交时原块已被释放
- 位置：`IrRecursiveInline.inc:184`（`return value;` 原样返回）；`:573`（`recursive_destroy_blocks(function->entry_block)`）
- 类别：静默失败；契约破坏
- 触发条件：emit 阶段 `map` 查不到某个 TEMP，例如 `:443-446` 子块 `child_map.count = 0` 之后子块引用父块定义的 temp，或 continuation 的 `map` 与 `source_block` 错位。
- 后果：拷贝指令保留原函数的 temp id；提交时 `function->entry_block` 整条块链被 `KRT_FREE`，新 IR 引用已释放/未定义的 temp，产生错误代码或后续 pass 崩溃。
- 最小修复：`recursive_value` 未命中时置 `expansion->failed` 并返回显式哨兵；或在 emit 前断言 map 对 `recursive_check_block` 已验证的 def-use 完整。
- 备注：当前由 `recursive_check_block` 的跨块使用检查（`:131-140`）兜底，属防御缺失而非已触发缺陷。

### [高] 67-3｜递归深度上限语义错乱：continuation 回弹 depth
- 位置：`IrRecursiveInline.inc:8`（`KRT_RECURSIVE_DEPTH_LIMIT 3`）；`:418-421`（`next = {..., depth, ...}` 后传入 `depth - 1`）
- 类别：终止条件；注释与实现脱节
- 触发条件：同一基本块内含 ≥2 个自调用（`calls <= 4` 允许）。
- 后果：continuation 保存的是调用点的 `depth`（未递减），callee 用 `depth - 1` 展开；resume 后同一块内后一个自调用又以完整 `depth = 3` 展开。实际嵌套展开层数 ≈ 自调用链长度 × 深度，而非 3；文件头 “Bounded self-recursive expansion” 与真实预算语义不符。
- 最小修复：continuation 保存 `depth - 1`，或在 `:412` 额外检查全局展开计数。
- 备注：有 `instruction_count <= 384` 兜底（`:318`、`:361`），故不会无界；但上限不是注释所称的“深度”。

### [高] 67-4｜IrInline 增长预算按函数计，模块级代码膨胀无上限
- 位置：`IrInline.inc:4`（`KRT_INLINE_GROWTH_LIMIT 256`）、`:5`（`KRT_INLINE_CALLER_LIMIT 1024`）、`:254`（`growth` 在函数循环内清零）
- 类别：配置漂移；资源与生命周期
- 触发条件：模块含多个调用方或多个小函数。
- 后果：每个调用方独立获得 256 指令增长预算，模块体积随调用点数量近似线性无界增长；`KRT_INLINE_CALLER_LIMIT` 只限单个调用方。
- 最小修复：把 growth 预算提升到模块级，并在 `:262` 用模块累计值判断。
- 备注：与 `IrRecursiveInline.inc:475-478` 的模块级 `remaining_growth` 口径不一致，两个内联器预算不可比。

### [高] 67-5｜probe 硬编码 optimization_level=3，与真实 pipeline 阶段不对齐
- 位置：`IrRecursiveInline.inc:546`（`.optimization_level = 3`）；`IrOptimizer.c:647-649`（内联在迭代循环前只跑一次）
- 类别：阶段不对齐；配置漂移
- 触发条件：模块 `optimization_level != 3`，或 `flags` 未启用后续 CSE/DCE pass。
- 后果：probe 中 `eliminate_pure_expressions`/`eliminate_unused_integers` 的收益（`cse_count`、`after_calls < before_calls` 判定）与真实 pipeline 不一致，可能选到在后续 pass 中无收益的深度；同时 probe 的 CSE 在无 SSA、无支配信息的 CFG 上按块局部工作（中端从不构造 SSA，见覆盖范围）。
- 最小修复：`probe.optimization_level` 取 `module->optimization_level`；把内联器纳入迭代循环或注明一次性语义。
- 备注：已 grep 核实 `KrtIrSsaOptimize` 不在中端调用链上。

### [高] 67-6｜分配失败全部静默，零诊断（G1 前提成立）
- 位置：`IrInline.inc:147-155`、`:178-181`、`:228-231`；`IrRecursiveInline.inc:201-225`、`:323-337`、`:365-369`、`:408-413`
- 类别：静默失败；诊断缺失
- 触发条件：`KRT_CALLOC/KRT_MALLOC/KRT_REALLOC/KRT_STRDUP` 返回 NULL。
- 后果：优化静默失效（`inline_call` 返回 false、`inline_small_functions` 返回 false、`expansion.failed = true`），无日志、无统计；调用方 `ir_optimize_function_inlining` 只累计 `changed`，无法区分“无候选”与“内存不足”。
- 最小修复：失败路径输出诊断并计入独立统计（如 alloc_fail_count）。
- 备注：失败路径本身有正确释放（无泄漏），但完全无诊断。

### [高] 67-7｜recursive_append_inst 无容量检查，安全性依赖两个常量严格相等
- 位置：`IrRecursiveInline.inc:306-314`（`block->insts[block->inst_count++]` 无检查）；`:210`（容量固定 `KRT_RECURSIVE_GROWTH_LIMIT`）；`:318`、`:361`（唯一上限）
- 类别：未检查前置条件；缓冲区越界风险
- 触发条件：任何使 `inst_count` 超过 384 的路径；当前仅靠 `instruction_count <= 384` 与容量 384 恰好相等来保证。
- 后果：`block->insts[block->inst_count++]` 越界写；安全性依赖两个独立常量严格相等，任一被调整即溢出。
- 最小修复：append 前检查 `inst_count < inst_capacity`，否则置 `failed`。
- 备注：当前边界恰好相等（384/384），属脆弱耦合。

### [中] 67-8｜recursive_new_block 半初始化块先入链并消耗 block id
- 位置：`IrRecursiveInline.inc:196-226`（`:206` 分配 id、`:216-221` 接链发生在失败检查 `:222` 之前）
- 类别：资源与生命周期；未检查前置条件
- 触发条件：`block->label/insts/preds/succs` 任一分配失败。
- 后果：块已挂入 `expansion->first/last` 且 `next_block` 已推进，`expansion->last` 指向半初始化块；若后续代码继续 append 会写入未分配数组（当前靠 `failed` 短路避免）。
- 最小修复：先分配全部资源，成功后再接链并分配 id。
- 备注：失败后整体丢弃，故当前不落地。

### [中] 67-9｜calls/size 在 DAG 上重复计数，资格判定与预算失真
- 位置：`IrRecursiveInline.inc:85-89`（`path` 只查环不去重）、`:106`（`(*calls)++`）、`:93`（`size++`）
- 类别：语义分叉（资格判定）；漏优化
- 触发条件：同一基本块经多条路径可达（DAG 形 CFG）。
- 后果：只有 1 个自调用的函数被重复计成 `calls >= 2` 而错误纳入候选；同时 `size` 被高估，`:493` 的 `remaining_growth < size` 会提前跳过本可展开的函数。
- 最小修复：用已访问集合去重后再计数，或明确按路径计数并注释。
- 备注：不会误纳不安全函数，只影响命中率与预算。

### [中] 67-10｜terminal 之后的死代码被静默丢弃且仍计入 original_size
- 位置：`IrRecursiveInline.inc:145-159`（遇 terminal 即 `return`，不检查其后是否还有指令）；`:496-503`（`original_size` 全量计数）
- 类别：死代码；语义分叉
- 触发条件：`RETURN/JUMP/BRANCH` 之后仍有指令。
- 后果：展开时 `:401-429` 同样在 terminal 处停止，死代码被静默删除；而 `original_size` 仍把它算入，使 `growth` 被低估甚至为负，影响 `remaining_growth` 与 stats 准确性。
- 最小修复：eligibility 中若 terminal 后仍有指令则判不合格，或从 `original_size` 中剔除。
- 备注：删死代码本身无害，问题在计数口径与静默性。

### [中] 67-11｜inline_call 失败回滚不回卷 next_temp
- 位置：`IrInline.inc:163`（`++*next_temp`）；`:198-205`（`failed` 只释放拷贝）
- 类别：共享可变状态；错误处理不一致
- 触发条件：拷贝途中 `KRT_CALLOC/KRT_MALLOC` 失败，或 `block->insts` 扩容（`:178`）失败。
- 后果：`next_temp` 已被推进且不复位，temp id 出现空洞；同一 block 的后续内联继续使用被推进的值，属部分修改。
- 最小修复：记录进入时的 `next_temp` 基线，`failed` 时回卷。
- 备注：id 单调递增，不会产生错误引用，属资源浪费与状态不一致。

### [中] 67-12｜超限调用方被 continue 跳过 insts 重建与缓存失效
- 位置：`IrInline.inc:251-253`（`continue`）；`:270-274`（`insts` 重建与 `KrtIrBlockInvalidateCache`）
- 类别：契约破坏；死代码
- 触发条件：调用方指令数 > `KRT_INLINE_CALLER_LIMIT`（1024）。
- 后果：该函数所有 block 的 `insts` 重建与缓存失效被跳过；若其 `insts` 与链表已不一致（其他 pass 只改链表），不一致会保留到后端。
- 最小修复：把重建/失效移到 `continue` 之前，或无条件执行。
- 备注：正常路径下二者一致。

### [中] 67-13｜内联/展开后 function 元数据（has_calls/stack_size）不更新
- 位置：`IrInline.inc:263`（`call` 改 `COPY` 后）；`IrRecursiveInline.inc:573-575`（替换 `entry_block/exit_block` 后）
- 类别：契约破坏；死代码
- 触发条件：内联移除了调用，或递归展开替换了整个块链。
- 后果：`KrtIRFunction.has_calls` / `stack_size` 与 IR 脱节；grep 全 `src` 显示 `has_calls` 仅在 `Ir.c:949`、`Ir.c:1034` 写入、无任何读取方，`stack_size` 在中端亦无读取方，属“写了不读”的死状态，内联器既不改也不删。
- 最小修复：内联/展开后重算这两个字段，或删除字段。
- 备注：当前无可观测影响，但属零容忍的“死代码/契约破坏”。

### [中] 67-14｜候选查找假设函数名非 NULL 且唯一
- 位置：`IrInline.inc:85-89`（`strcmp(a->function->name, ...)` 无 NULL 检查）；`:91-106`（二分返回任意命中）
- 类别：未检查前置条件
- 触发条件：`function->name == NULL`（`strcmp` 崩溃）；模块内存在同名函数（任意命中，可能内联到错误目标）。
- 后果：崩溃或错误内联。
- 最小修复：收集候选时跳过 NULL 名；重名时返回错误或按更精确键匹配。
- 备注：正常模块名唯一。

### [中] 67-15｜内联器不在迭代循环内，新机会不再被内联
- 位置：`IrOptimizer.c:626`、`:647-649`（在 `while (changed && ...)` 之前调用一次）
- 类别：阶段不对齐；漏优化
- 触发条件：内联后产生新的可内联候选（如某函数变成单块纯函数）。
- 后果：新机会不会被再次内联；收敛性依赖“先 small 后 recursive”的单次顺序，且内联产生的 `changed` 不参与迭代。
- 最小修复：把内联纳入迭代循环，或在文档中明确一次性语义。
- 备注：已 grep 核实调用位置。

### [中] 67-16｜inline_operand 未命中 TEMP 时静默返回原值
- 位置：`IrInline.inc:112-119`
- 类别：静默失败；契约破坏
- 触发条件：callee 的 TEMP 操作数未在 `old_ids` 中（当前由 `inline_body_size:55-64` 的 `ids` 检查保证不发生）。
- 后果：拷贝保留 callee 的 temp id，与调用方 temp 空间冲突；一旦放宽 `inline_body_size` 条件即成为实际缺陷。
- 最小修复：未命中时返回 false，让 `inline_call` 走 `failed`。
- 备注：与 67-2 同源（不同文件），当前受 `:55-64` 兜底。

### [低] 67-17｜缺少针对递归内联的回归测试
- 位置：`IrRecursiveInline.inc:1-588`、`IrInline.inc:1-283`（全仓无配套测试引用）
- 类别：测试不足
- 触发条件：回归测试缺失时，任何深度/仿射/回滚改动都无法被证伪。
- 后果：grep 全仓仅 `Test/Performance/O3Optimization/ReadMe.md:33` 描述 IrInline 设计、`Test/Performance/LLVMOptimization/Analysis/RunAnalysis.py:69` 记录 `recursive_inline_depths` 指标；未发现覆盖自递归/互递归、`depth=1..3`、`calls=2..4`、除零、分配失败注入、DAG 重复计数、跨块 SSA 拒绝的用例。
- 最小修复：为上述边界各加回归用例，并对 `KRT_*` 注入失败验证回滚。
- 备注：据 grep 结果据实描述，未通读测试目录。
