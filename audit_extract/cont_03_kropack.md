# 第六十批：KroRegisterPacking.inc + KroLeafReturn.inc

## 覆盖范围

- 全量读取（read 分段读完，无整文件通读其他源文件）：
  - `Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc`（608 行，实际 608 行）
  - `Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc`（实际 182 行，非任务描述的约 250 行）
- 仅为核对符号/调用方做了定向 grep 与小区间 read，确认了以下前置事实：
  - `KRT_KRO_PACKED_REG_COUNT = 5`，`g_packed_regs = {RBX=3, R12=12, R13=13, R14=14, R15=15}`（KroCodegen.h:11、KroCodegen.c:44-45）。
  - `KRT_KRO_MAX_ARGS = 128`，`ctx->current_param_count = min(param_count, 128)`（KroCodegen.h:10、KroCodegen.c:1317）。
  - `KRT_MALLOC -> KrtSafeMalloc`，失败 `return NULL`；`KrtMemoryReportError` 在 Allocator.c:425-435 为空操作（仅 `KRT_DEBUG` 断点），故分配失败零诊断。
  - `emit_extend_scalar`（KroInteger.inc:34-59）、`emit_shift_imm`（KroInteger.inc:8-13，4=SHL/5=SHR/7=SAR）、`integer_condition`（KroInteger.inc:836-851，返回 0x92..0x9F，减 0x20/0x10 后分别对应 short/near jcc）、`KrtIntegerNormalize`（Integer.h:16-26，符号扩展）。
  - 每函数重置：`arg_packed/arg_stack_offsets/arg_address_taken/packed_reg_bits/packed_reg_save_offsets/packed_reg_needs_zero` 均在 KroCodegen.c:1318-1323 清零；`prepare_temporary_storage` 失败会 `KrtError` 并中止（KroCodegen.c:1291-1294），故临时存储容量耗尽路径有诊断，不在本批条目内。
- 关键前提 G1 已应用：本批两条致命条目即 KRT_MALLOC/KRT_CALLOC 失败静默回退。

## 问题条目

### [致命] 60-1｜跨块临时寄存器分析中 KRT_MALLOC 失败被静默吞掉
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:386-387`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期（G1）
- 触发条件：`KRT_MALLOC((size_t)ctx->temp_slot_count * sizeof(*definition_block))` 返回 NULL（内存不足，或 `temp_slot_count` 很大导致请求过大）。
- 后果：`plan_cross_block_temporaries` 直接 `return false`，`allocate_temporary_registers` 中 `global_intervals` 置 false，跨块临时变量全部退化为块内分配；`KrtMemoryReportError` 为空操作，用户与调用方都收不到任何诊断，代码生成照常“成功”。这是 G1 描述的活路径零诊断。
- 最小修复：分配失败时记录错误（`KrtError`）并设置 ctx 失败标记，中止该函数代码生成；或至少让调用链把失败上报到 `KrtKrtGenerate`。
- 备注：同函数 456 行的 `KRT_CALLOC` 是同一模式，见 60-2。

### [致命] 60-2｜跨块布尔表 KRT_CALLOC 失败被静默吞掉
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:456-457`
- 类别：静默失败 / 诊断缺失 / 资源与生命周期（G1）
- 触发条件：`KRT_CALLOC(ctx->temp_slot_count ? ctx->temp_slot_count : 1, sizeof(*cross_block))` 返回 NULL。
- 后果：`cross_block = NULL`，`global_intervals = cross_block && ...` 静默变为 false，跨块临时寄存器分配被无声禁用；`KRT_FREE(cross_block)`（510 行）对 NULL 释放虽安全，但整条优化路径的失败不可观测，最终目标文件仍正常产出。
- 最小修复：分配失败时 `KrtError` 并中止代码生成，或显式回退并记录“跨块临时寄存器优化已禁用”。
- 备注：与 60-1 独立（不同分配点、不同数组），不可合并。

### [严重] 60-3｜allocate_packed_field 容量不足/128 位时静默返回，字段保持 0
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:3-7`、`18-20`
- 类别：静默失败 / 诊断缺失 / 契约破坏
- 触发条件：(a) 5 个打包寄存器剩余位宽都不足以容纳 `fragment`（`best < 0`）；(b) `type` 为 128 位，`bits % 64 == 0`，`fragment = 0`，第 5-7 行直接返回。
- 后果：`field->bit_width` 保持调用前的 0（`arg_packed` 每函数 memset、`KROLocalVar` 分配时 memset），`plan_function_storage`（546/552 行）据此静默改用栈分配。行为虽正确回退，但没有任何诊断，寄存器打包率下降不可观测；128 位走此路径时调用方 `allocate_value_stack` 按 `field->bit_width ? 8 : 16` 得 16 字节，与 128 位匹配，但注释与调用点均未说明“128 位必然不进打包”这一隐含契约。
- 最小修复：`allocate_packed_field` 返回 bool，失败/不支持时由调用方 `KrtError` 或至少记录；对 128 位直接走栈的约定写进注释。
- 备注：与 60-1/60-2 的分配失败不同，这里是容量/枚举范围导致的静默降级。

### [严重] 60-4｜plan_cross_block_temporaries 多处静默回退，无任何诊断
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:338-340`、`358-360`、`364-366`、`373-375`、`404-407`、`427-430`
- 类别：静默失败 / 诊断缺失 / 阶段不对齐
- 触发条件：函数块数达到 `KRT_KRO_ACYCLIC_BLOCK_LIMIT`（256）；BRANCH 的 `succ_count != 2`；JUMP 的 `succ_count != 1`；任一前向边 `target <= i`（回边/不可约图）；某 temp 定义块偏移越界或定义类型为非整数或位于 entry（`tail_site_count` 时）；任一使用点不满足 `first_position < position` 或不被定义块支配。
- 后果：统一 `return false` 或 `eligible[i] = false`，跨块临时寄存器优化被无声禁用或逐项丢弃，无任何诊断；用户无法区分“优化不适用”与“分析出错”。
- 最小修复：至少对“块数超限”“非拓扑边”这类异常输入 `KrtError`；对正常不适用路径可保留静默，但需与真正错误区分。
- 备注：`KRT_KRO_ACYCLIC_BLOCK_LIMIT` 为 256 的魔法上限（302 行），与 `KRT_KRO_TAIL_UPDATE_LIMIT`(5)、`KRT_KRO_TAIL_PREFIX_LIMIT`(64) 等上限无统一命名/配置来源。

### [严重] 60-5｜next_write[16] 为魔法上界，且以机器寄存器号而非打包索引索引
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:261`（定义）、`271`（`next_write[written] = here`）
- 类别：未检查前置条件 / 数组越界（潜在）/ 契约破坏
- 触发条件：`full_storage_register` 返回的 `field->reg`（机器寄存器号）>= 16。当前 `g_packed_regs` 最大为 `KRT_REG_R15 = 15`，故暂不触发；一旦 `g_packed_regs` 增加/改号（如加入 R16 或重排），`written` 即越界。
- 后果：`next_write[written]` 越界写，破坏栈上相邻对象（`locals[]`、`position` 等），且 `written >= 0` 检查（270 行）无法防御上界越界；静默内存破坏。
- 最小修复：把 `next_write` 改为按打包索引（0..KRT_KRO_PACKED_REG_COUNT-1）索引，或在写入前 `if ((unsigned)written >= sizeof(next_write)/sizeof(next_write[0])) return false;`。
- 备注：该数组还承担“寄存器改写位置”语义，用机器寄存器号做下标是隐式耦合，属共享可变状态与边界契约脆弱点。

### [严重] 60-6｜emit_store_parameter_word 对 index 无任何边界检查
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:596-607`（598 读 `func->params[index]`，601 读 `ctx->arg_packed[index]`，606 读 `ctx->arg_stack_offsets[index]`）
- 类别：未检查前置条件 / 数组越界（潜在）/ 契约破坏
- 触发条件：调用方传入 `index >= KRT_KRO_MAX_ARGS`(128) 或 `index >= func->param_count`。当前唯一潜在路径是 `KroTailCalls.inc:335`、`353` 的 `for (i = 0; i < func->param_count; i++)` 循环（未用 `current_param_count` 收敛），它依赖 `plan_tail_calls` 在 `KroTailCalls.inc:87` 的 `func->param_count != ctx->current_param_count` 提前返回来保证不越界。
- 后果：一旦该前置守卫被改动、绕过，或 `param_count` 在 plan 与 emit 之间变化，即对 `func->params`（IR 数组）、`ctx->arg_packed[128]`、`ctx->arg_stack_offsets[128]` 越界读写，静默生成错误代码或崩溃。
- 最小修复：函数入口加 `if (index < 0 || index >= ctx->current_param_count) { KrtError(...); return; }`；同时把 KroTailCalls 的循环上界改为 `ctx->current_param_count`。
- 备注：`emit_store_value`（KroInteger.inc:236-245）对 ARG 同样未做 `current_param_count` 检查，属同一契约缺口。

### [严重] 60-7｜emit_leaf_return_guard 先发射代码再决定是否采用，>16 时留下半激活冗余快路径
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:137-152`（发射在 137-146，判定在 149-152）
- 类别：阶段不对齐 / 死代码 / 配置漂移 / 共享可变状态
- 触发条件：`entry_leaf_return` 成功且 `ctx->tail_site_count > 16`（`ctx->optimization_level >= 3`）。
- 后果：比较 + 短跳转 + 内联 `mov RAX, imm/extend; ret` 序列已经写入代码缓冲，但 `rotated_tail_leaf` 保持 NULL、函数返回 NULL，于是 `fast_leaf == NULL`，entry 块与 leaf 块在 KroCodegen.c:1382-1388 仍被完整发射。结果是同一函数存在两条并行的叶返回路径（一条内联在 prologue 前，一条在 leaf 块），共享 R10/R11/RAX；`tail_site_count <= 16` 为无注释魔法上限，超限既无诊断也不影响正确性，属隐性分叉。
- 最小修复：把上限判定提到发射之前（超限直接 `return NULL`，不发射），或超限时仍设置 `rotated_tail_leaf` 并返回 leaf，保持单一快路径。
- 备注：`ctx->rotated_tail_leaf` 是跨函数复用的上下文状态，每函数在 KroCodegen.c:1312 清空，但本函数在未采用时仍会写它（150 行），语义易误读。

### [严重] 60-8｜emit_tail_backedge 在 trivial_leaf_value 失败时静默 return，丢弃返回路径
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:169-171`
- 类别：静默失败 / 诊断缺失 / 契约破坏
- 触发条件：`trivial_leaf_value(ctx, func, ctx->rotated_tail_leaf, &value)` 返回 false。当前因 `rotated_tail_leaf` 来自 `emit_leaf_return_guard` 中已验证成功的同一 leaf 而不可达，但函数自身无任何防御或断言。
- 后果：直接 `return`，此时比较与条件跳转（165-167 行）已发射，后续既无叶返回值、也无 `jmp epilogue`；控制流落入 `generate_block` 之后要发射的下一块或 epilogue，静默生成错误代码，且无诊断。
- 最小修复：失败时回退到 `emit_jump_to_block(ctx, func->entry_block)`（与 157-159 行一致）或 `KrtError` 后中止。
- 备注：与 60-7 同源（都依赖 rotated_tail_leaf 的事前验证），但失败点与后果独立，单独成条。

### [中] 60-9｜allocate_value_stack 对任何未打包值一律分配 16 字节
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:86-92`
- 类别：资源与生命周期 / 契约破坏
- 触发条件：`field->bit_width == 0` 的所有参数/局部变量：8/16/32/64 位整数（未打包成功者）、`TOKEN_FLOAT32/FLOAT64`、指针/数组、`TOKEN_VOID`，以及 128 位整数。
- 后果：`current_stack_offset += 16`，但 `emit_store_value`/`emit_load_value_to_reg` 对 <=64 位只访问 8 字节（`emit_store_to_stack(slot)`、`emit_load_from_stack(slot)`），每个此类变量浪费 8 字节并在栈上留下 8 字节空洞；大函数栈帧无谓膨胀，`packed_reg_save_offsets` 之后的偏移全部顺延。
- 最小修复：按 `int bits = KrtTokenIntegerBits(type); ctx->current_stack_offset += (bits > 64) ? 16 : 8;`（浮点/指针按 8）。
- 备注：128 位与“未打包成功”共用 `field->bit_width == 0` 这一状态，无法区分，是尺寸判断退化的根因。

### [中] 60-10｜emit_entry_compare 32 位与 64 位立即数路径归一化不一致
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:111`（原始 `data.integer` 截断）与 `116`（`scalar_integer_constant` 归一化）
- 类别：语义分叉 / 一致性
- 触发条件：`compare->operands[1]` 为 `KRT_IR_VALUE_INTEGER`，其 `data.integer` 未按自身 `value_type` 预先归一化（或宽度与 `type` 不一致）。
- 后果：32 位路径 `(uint32_t)compare->operands[1].data.integer` 直接截断，64 位路径 `scalar_integer_constant()` 走 `KrtIntegerNormalize`（符号扩展）；两条路径对同一语义常量可能得到不同位型，生成错误比较。当前依赖 IR 构造期已归一化（Ir.c:410）才未暴露。
- 最小修复：32 位路径统一改为 `(uint32_t)scalar_integer_constant(&compare->operands[1])`。
- 备注：`emit_scalar_compare_flags`（KroInteger.inc:683）也用原始 `data.integer`，但那是既有实现；本条只针对 KroLeafReturn.inc 内部两条路径的不一致。

### [中] 60-11｜叶返回短跳转位移无范围检查即截断为 uint8_t
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroLeafReturn.inc:138-146`（145 行 `(uint8_t)(end - displacement - 1)`）
- 类别：未检查前置条件 / 溢出（潜在）
- 触发条件：内联叶返回序列（`emit_entry_scalar` 的立即数/扩展 + `RET`）字节数 > 127，使 `end - displacement - 1` 超出有符号 8 位范围。
- 后果：`uint8_t` 截断后短跳转到错误目标，静默误编译；当前序列约 11 字节故未触发，但 `emit_entry_scalar` 对 ARG 的扩展长度、立即数编码长度可变，无断言保护。
- 最小修复：写入前 `if (end - displacement - 1 > 127) { 回退到 near jump 或 KrtError }`。
- 备注：同文件 149 行的 `tail_site_count <= 16` 同属无检查魔法上限。

### [低] 60-12｜packed_reg_needs_zero 只覆盖局部变量，不覆盖参数，初始化契约不对称
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroRegisterPacking.inc:544-561`（参数打包在 544-548，needs_zero 仅在 549-561 的局部变量循环里置位）
- 类别：契约破坏 / 共享可变状态 / 注释与实现脱节
- 触发条件：某打包寄存器仅由参数占用（无同名局部变量触发 needs_zero），且该参数是偏移 0 的部分宽度字段（如 32 位）。
- 后果：prologue（KroCodegen.c:370-378）不会清零该寄存器；`emit_store_packed_field` 的快路径（52-55 行）执行 64 位 `mov`，把源寄存器高位垃圾写入该寄存器高位。当前 `emit_load_packed_field`/`scalar_low_register` 都按位域扩展/掩码，高位不可观测，但“寄存器初始为零”这一不变量在参数与局部变量之间不一致；一旦将来加入全宽读取（如把该寄存器当 64 位临时/累加器）会静默出错。
- 最小修复：对参数打包寄存器同样置 `packed_reg_needs_zero[j] = true`，或在 `emit_store_packed_field` 快路径改为按位域合并并注释该不变量。
- 备注：与 60-3 的“128 位不进打包”同属打包初始化契约的隐式约定，建议统一在头文件注释中固化。
