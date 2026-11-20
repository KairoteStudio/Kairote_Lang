# 第六十一批：KroTailCalls.inc + KroTailUpdates.inc + KroFloatCasts.inc

## 覆盖范围

- 全量读取（分段 read，非通读其他文件）：
  - `Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc`（364 行，含 `#include "KroTailUpdates.inc"`）
  - `Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc`（264 行）
  - `Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc`（123 行）
- 调用方/契约核对仅限 grep 特定符号：`tail_site_count`、`tail_acc_opcode`、`tail_register_argument`、`tail_arg_offsets`、`early_tail_accumulate`、`deferred_tail_argument`、`no_storage`、`packed_reg_bits`、`arg_packed`、`find_temp_location`、`scalar_binary_encoding`、`scalar_register64`、`emit_load_for_type`、`emit_local_jump`、`allocate_packed_field`、`allocate_temporary_registers`、`forwarded_result_register`、`KRT_REG_*`、`KRT_MALLOC` 系列。
- G1（`KRT_MALLOC` 失败返回 NULL 而不 exit）：这三个被审文件内 **没有任何** `KRT_MALLOC/KRT_CALLOC/KRT_REALLOC/KRT_FREE` 调用，故 G1 的分配失败路径在本切片不直接触发。已按 G1 的“返回值可空、调用点零诊断”模式核查同类可空返回 `find_temp_location()`：`KroTailCalls.inc:116`、`:155`、`KroTailUpdates.inc:193` 三处均未显式判空，但其在调用前都已由 `single_use_temporary()`（内部 `find_temp_location` 判空）间接保证非空，当前不可达空指针，记为契约脆弱点（见 61-4 同类）。
- 关键寄存器事实（用于判定可达性）：`KRT_REG_RAX=0 … KRT_REG_R15=15`；`g_packed_regs = {RBX,R12,R13,R14,R15}`；`allocate_packed_field()` 只从 `g_packed_regs` 取寄存器；`plan_tail_calls()` 在累加器启用时把 `packed_reg_bits[4]（R15）`、宽度 >64 时再把 `[3]（R14）` 占满；`scalar_low_register()` 对宽度 >64 直接返回 -1。

## 问题条目

### [致命] 61-1｜float→宽整数转换越界/NaN/无符号负数静默出错
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc:88-123`（关键：`:91-93`、`:111-118`）
- 类别：语义分叉；未检查前置条件（浮点转换/饱和）；诊断缺失；静默失败
- 触发条件：`emit_load_for_type()` 走 `source_float && KrtTokenIntegerBits(type) > 64` 分支（`KroInteger.inc:173-175`），即 double → 128 位整数。具体触发：
  1. `|x| >= 2^128`（指数域 > 0x47E）→ `zero_large` 直接清零；
  2. `NaN / ±Inf`（指数域 0x7FF）→ 同样落到 `zero`；
  3. 目标类型为**无符号** 128 位且 `x < 0` → 仍执行 `neg rax; adc rdx,0; neg rdx`（`:113`），得到 `2^128-|x|` 的回绕值；
  4. 目标类型为**有符号** 128 位且 `x >= 2^127` → 保留原值，最高位置位后按有符号解释成负数（回绕）。
- 后果：越界与 NaN/Inf 全部静默变成 0；无符号目标遇到负数得到回绕巨值；有符号目标在 `[2^127, 2^128)` 回绕。没有任何诊断，也没有饱和/截断约定，与常见语言语义（饱和、截断或显式 UB 陷阱）均不一致，且 4 种情形行为互不相同（0 / 回绕 / 保留）。
- 最小修复：在 `emit_float_wide_cast()` 入口按 `KrtTokenIsUnsigned(type)` 与目标位宽显式定义语义：无符号目标负数 → 0 或 UINT128_MAX；有符号目标越界 → INT128_MIN/MAX 饱和；NaN/Inf → 明确值或 `KrtError` 诊断。至少把 `zero_small/zero_large` 的“清零”改为饱和，并区分符号。
- 备注：`emit_integer_float_cast()`（`:68-86`）方向相反，未发现同类问题；本条只针对 double→宽整数。

### [中] 61-2｜`cast_saved_offset()` 对未知寄存器返回 0，恢复时写错寄存器
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroFloatCasts.inc:15-23`（使用点 `:27`、`:30`）
- 类别：契约破坏；静默失败
- 触发条件：`emit_cast_save()/emit_cast_restore()` 的 `low` 或 `high` 不是 `{RAX,RCX,RDX,R8,R9}` 之一（例如 R10/R11/RBX/R12-R15）。`cast_saved_offset()` 走 `default: return 0`，而 0 号槽是 `emit_cast_save()` 压栈的 **R9** 保存槽（`:3` 的压栈顺序 RAX,RCX,RDX,R8,R9）。
- 后果：`emit_cast_restore()` 把转换结果写进 `[rsp+0]`，随后 `pop r9` 把它当作 R9 的旧值恢复——结果被送进 R9，而调用方期望的目标寄存器拿到的是旧值。整条转换静默算错。
- 最小修复：`cast_saved_offset()` 对未知寄存器返回 -1 并在 `emit_cast_save/restore` 中断言或回退；或把 `low/high` 限定为 `{RAX,RDX,RCX,R8}` 并加 `KrtError`。
- 备注：当前唯一调用链 `emit_load_for_type()` 只传 `RAX/RDX` 与 `RCX/R8`（`KroInteger.inc:692,760,871`），故为**潜伏契约破坏**，一旦新增调用即触发。

### [中] 61-3｜`emit_tail_update()` 先发射指令后校验编码，失败留下半条机器码
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc:229-234`
- 类别：错误处理不一致；静默失败；资源与生命周期（代码缓冲区）
- 触发条件：`scalar_binary_encoding(update->opcode, &operation, &opcode)` 返回 false。`emit_tail_update()` 在 `:229` 已经执行 `emit_load_value_to_reg(ctx, left, result_reg)`，直到 `:232` 才校验，失败直接 `return`。
- 后果：代码缓冲区里留下一条没有对应操作的 `mov`，后续字节错位；函数返回后调用方 `emit_scheduled_tail_updates()` 仍继续 `emit_tail_backedge()`，把错误机器码当成成功路径，无任何诊断。
- 最小修复：把 `scalar_binary_encoding()` 校验移到 `:229` 之前；或让 `emit_tail_update()` 返回 bool，失败时向上传播并放弃本次尾调用优化。
- 备注：`build_tail_update_plan()` 在规划期已用同一函数校验过，故当前不可达；但规划期与发射期是两次独立重建（`plan_tail_register_updates()` 与 `emit_scheduled_tail_updates()`），契约上必须自洽。

### [中] 61-4｜尾调用贡献寄存器在参数快照前捕获，快照会覆盖 RAX/RDX
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc:326`（捕获）、`:335-341`（快照覆盖 RAX/RDX）、`:329`（仅在 `tail_register_argument` 分支防御 RAX）、`:343-348`（使用）
- 类别：契约破坏；共享可变状态（寄存器）；未检查前置条件
- 触发条件：`emit_tail_call()` 走通用路径（`tail_register_argument == false`，即 `param_count >= 2`）且 `slot->tail_combine != NULL`。`:326` 先取 `contribution_reg = scalar_low_register(ctx, contribution)`；随后 `:335-341` 的快照循环用 `emit_load_pair(..., RAX, RDX)` 把 RAX/RDX 全部改写；`:343-348` 却在 `contribution_reg >= 0` 时直接拿旧寄存器做 `emit_tail_accumulate(ctx, true, contribution_reg)`。`:329` 只为 `tail_register_argument` 分支选了 R9 来避让 RAX，快照分支没有对应保护。
- 后果：一旦 `contribution_reg ∈ {RAX, RDX}`，累加器加到的是最后一个参数的快照值而非贡献值，尾调用返回结果静默错误。
- 最小修复：把贡献值的读取/加载移到快照之后（或在快照前把贡献溢出到栈/安全寄存器）；`contribution_reg == RAX` 的防御要对两条分支一致。
- 备注：**当前可达性受限**：`scalar_low_register()` 对宽度 >64 返回 -1；参数与临时寄存器都取自 `g_packed_regs={RBX,R12..R15}`，而 `plan_tail_calls()` 已把 R15（宽度 >64 时含 R14）占满，故 `contribution_reg` 实际只会是 RBX/R12/R13 或 -1，暂不会命中 RAX/RDX。但 `:329` 的存在说明作者认为 RAX 可达，IR/分配器一旦变化即变现为静默错算，属零容忍范围。

### [低] 61-5｜`emit_tail_update()` 的 RCX 备用分支不可达（死代码）
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc:238-241`
- 类别：死代码；注释与实现脱节
- 触发条件：`right_reg < 0` 时执行 `right_reg = result_reg == KRT_REG_RAX ? KRT_REG_RCX : KRT_REG_RAX;`。
- 后果：`result_reg`（`:228`）只在 `right_reg == target && left_reg != target` 时才取 RAX；而 `right_reg < 0` 时 `right_reg == target` 恒不成立，故 `result_reg == target`。`target = ctx->arg_packed[parameter].reg` 只可能取 `g_packed_regs={RBX,R12..R15}`（`allocate_packed_field()` 仅从该集合分配），永远不是 RAX。因此 `KRT_REG_RCX` 分支恒不执行。
- 最小修复：删除 RCX 分支，或改为 `right_reg = KRT_REG_RAX;` 并同步删除 `:226-227` 中关于 RAX/参数寄存器集的过时注释。
- 备注：若他处把 `arg_packed` 改为可放入 RAX/RCX，该分支会立即变成真实的寄存器覆盖 bug。

### [低] 61-6｜`tail_update_leaf()` 对 COPY/CAST 链无环检测，循环引用导致编译器死循环
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailUpdates.inc:53-60`
- 类别：未检查前置条件；资源与生命周期（编译期挂死）
- 触发条件：`value` 沿 `definition->operands[0]` 下行时出现自引用（`t = copy t`）或互引用环（`t1 = copy t2; t2 = copy t1`），且两者 `use_count == 1`（`tail_update_definition()` 的要求）。`while (value.type == KRT_IR_VALUE_TEMP)` 没有访问深度上限或已访问集合。
- 后果：`plan_tail_register_updates()` → `build_tail_update_plan()` → `tail_update_leaf()` 无限循环，编译器挂死，无超时无诊断。
- 最小修复：加深度上限（如 `KRT_KRO_TAIL_PREFIX_LIMIT`）或 `definitions` 已收录集合判重，超限即返回 false（回退到普通快照路径）。
- 备注：`build_tail_update_plan()` 的 `plan->prefix` 本身有上限，但互引用环的两个节点都在 prefix 内，不会被前缀上限截断。

### [低] 61-7｜头注释只声明加法/乘法，实现还包含 AND/OR/XOR
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroTailCalls.inc:1-3`（实现：`:16-19` `associative_integer_opcode()`）
- 类别：注释与实现脱节
- 触发条件：阅读 `:1-3` 的说明「Integer associative returns can carry an accumulator because fixed-width addition/multiplication wrap modulo 2^N」。
- 后果：注释把累加器限定为加法/乘法，实际 `associative_integer_opcode()` 还接受 `KRT_IR_AND/OR/XOR`，且 `emit_tail_accumulator_init()`（`:180`）为 AND 选 `UINT64_MAX`、OR/XOR 选 0。注释误导后续维护者对恒等元与溢出语义的判断。
- 最小修复：把注释改为「addition/multiplication/and/or/xor」，或显式列出恒等元表。
- 备注：语义本身（结合律 + 固定宽度回绕）对这四个算子成立，仅注释不准确。
