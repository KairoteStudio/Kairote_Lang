# 第五十九批：KroInteger.inc（全量）

## 覆盖范围

- 审计对象：`Re.KrtC/src/Compiler/Backend/Kro/KroInteger.inc`，全量 898 行（分 1–300 / 301–600 / 601–898 三段读完）。
- 覆盖函数（按文件顺序）：`emit_reg_op`、`emit_shift_imm`、`emit_local_jump`、`patch_local_jump`、`emit_extend_scalar`、`emit_normalize_pair`、`emit_load_high`、`emit_load_pair`、`emit_load_for_type`（含 `#include "KroFloatCasts.inc"` 展开点）、`emit_load_condition`、`emit_store_value`、`emit_negate_pair_by_sign`、`emit_wide_division`、`emit_integer_shift`、`emit_reg_op32`、`emit_imm_op32`、`scalar_low_register`、`scalar_register32`、`emit_load_scalar32`、`has_scalar32_operands`、`can_emit_binary32`、`fits_signed_imm32`、`scalar_integer_constant`、`emit_imm_op64`、`scalar_register64`、`scalar_binary_encoding`、`emit_binary64`、`emit_binary32`、`emit_scalar_compare_flags`、`emit_multiply_pair`、`constant_shift_count`、`emit_shift_by_constant`、`emit_integer_binary`、`integer_condition`、`emit_integer_compare`。
- 核对方式：仅对调用方/契约做定向 grep 与窄窗口读取（`KroCodegen.c` 的 `emit_load_value_to_reg`/`emit_float32_conversion`/`emit_store_to_stack`/寄存器与栈帧约定、`KroRegisterPacking.inc` 的打包与临时寄存器分配、`KroWriter.c` 的重定位 addend 语义、`Integer.h` 的 `KrtIntegerNormalize`、`Tokenizer.c` 的 `KrtTokenIntegerBits/Common`、`X86Codegen.c`、`VmCodegen.c`）。
- 已确认无问题的部分：x86-64 编码（REX/ModRM/SIB、LEA、SHRD/SHLD、MUL/IMUL、IDIV/DIV、SETcc、重定位 addend）逐条核对正确；128 位乘加/移位/比较、宽除法恢复算法、`fits_signed_imm32` 与 `emit_imm_op*` 的符号扩展判断、`patch_local_jump` 的 rel32 计算均正确；`emit_extend_scalar` 在 bits=64/128 的 move 分支不会产生负移位（`bits < 64` 条件已挡住），故不列为问题。

## 问题条目

### [严重] 59-1｜emit_store_value 对 ARG 下标缺少边界检查，可越界读/写
- 位置：`KroInteger.inc:236-245`（关键 237、245）
- 类别：8 未检查前置条件（越界）
- 触发条件：目的操作数为 `KRT_IR_VALUE_ARG` 且 `data.index < 0` 或 `>= ctx->current_param_count`（或超出 `KRT_KRO_MAX_ARGS`）。
- 后果：`ctx->arg_packed[index]`、`ctx->arg_stack_offsets[index]` 越界读，随后按越界 `slot` 调用 `emit_store_to_stack` 写栈，破坏栈帧；同一文件 `emit_load_high:108` 与 `KroCodegen.c:657/660` 都有 `index >= 0 && index < current_param_count` 守卫，唯独写路径没有。
- 最小修复：复用与 `emit_load_high` 相同的守卫；越界时回退到局部变量或报诊断并终止代码生成。
- 备注：同文件三种 ARG 访问三种口径（读低字有守卫、读高字有守卫、写无守卫）。

### [严重] 59-2｜窄整数除/模对除数 0 无检查，依赖 #DE，与宽路径及 X86 后端三重分叉
- 位置：`KroInteger.inc:787-814`（实际 `DIV/IDIV` 在 807）
- 类别：9 错误处理不一致 / 2 诊断缺失 / 3 语义分叉
- 触发条件：任意 ≤64 位整数 `KRT_IR_DIV`/`KRT_IR_MOD`，运行时除数为 0。
- 后果：直接执行 `48 F7 F1`（DIV）或 `48 F7 F9`（IDIV），触发硬件 #DE → SIGFPE 崩溃，无源码位置与诊断；X86 后端除零返回 -1（`X86Codegen.c:1044-1047`），本文件宽路径除零执行 UD2（`KroInteger.inc:293-295`），三处行为互不相同。
- 最小修复：统一除零策略（约定返回值或统一运行时错误例程），并让窄/宽路径一致；复用已有 `uses_division/uses_modulo` 标记。
- 备注：仅处理了 `INT64_MIN / -1` 溢出，未处理除零。

### [严重] 59-3｜宽整数除零仅执行 UD2，无诊断且与窄路径信号不同
- 位置：`KroInteger.inc:293-295`
- 类别：2 诊断缺失 / 9 错误处理不一致
- 触发条件：128 位 `KRT_IR_DIV`/`KRT_IR_MOD`，除数 RCX:R8 为 0。
- 后果：执行 `0F 0B`（UD2）→ SIGILL，无诊断；窄路径是 SIGFPE，X86 后端是返回 -1。
- 最小修复：与 59-2 统一为同一套除零处理。
- 备注：与 59-2 为不同代码点、不同信号，独立记录。

### [严重] 59-4｜emit_load_condition 直接测浮点位型，-0.0 被判为真
- 位置：`KroInteger.inc:195-202`（浮点加载分支 166-193）
- 类别：3 语义分叉
- 触发条件：浮点表达式直接作为条件（`IrGen.c:2405` 对 while 条件不做 bool 转换，语义分析也未限制条件类型）或 `CAST→BOOL`。
- 后果：`emit_load_for_type` 对 FLOAT32/FLOAT64 只保留/舍入位型，`emit_load_condition` 用 `TEST RAX,RAX` 判非零；-0.0（FLOAT32 `0x80000000`、FLOAT64 `0x8000000000000000`）被判为真，与“值 != 0”及 X86/Vm 的浮点比较语义不符。
- 最小修复：浮点源先与 0.0 比较（CMPSS/CMPSD，或按符号位+指数判零）再 SETcc；或在前端禁止浮点条件。
- 备注：NaN 判真与 C 一致，仅 -0.0 错误；`emit_load_condition` 也不区分 FLOAT32/FLOAT64 的零编码。

### [严重] 59-5｜emit_load_high 对未解析 VAR 静默把高字置 0，与低字回退口径不一致
- 位置：`KroInteger.inc:118-152`（148-152）
- 类别：1 静默失败 / 3 语义分叉
- 触发条件：`KRT_IR_VALUE_VAR` 既不在局部变量表（`find_local_var` 返回 NULL）、`kro_find_symbol` 也失败（符号缺失/声明缺失）。
- 后果：低字由 `emit_load_value_to_reg` 回退到 `find_or_alloc_local_var` 读未初始化栈（`KroCodegen.c:637-642`），而高字走 `slot=-1` 分支被置 0（`KroInteger.inc:151`），宽值静默错误；`kro_find_symbol` 的失败被完全吞掉，无任何诊断。
- 最小修复：与 `KroCodegen.c:637-642` 统一回退路径，或报告未解析符号并终止。
- 备注：`slot > 0` 的 else 分支同时承担“无效 slot”和“未解析”两种语义。

### [一般] 59-6｜emit_store_value 在 slot<=0 或局部变量表满时静默丢弃存储
- 位置：`KroInteger.inc:266-278`（273）
- 类别：1 静默失败
- 触发条件：`find_or_alloc_local_var` 返回 NULL（`KRT_KRO_MAX_LOCAL_VARS` 满，`KroCodegen.c:111-113`），或 `alloc_temp_slot` 返回 -1。
- 后果：`slot` 保持 -1/0，`if (slot > 0)` 直接跳过，赋值被静默丢弃，后续读到旧值或未初始化值，无诊断。
- 最小修复：失败时报告诊断并停止代码生成，不要静默跳过。
- 备注：G1 前提下 `KRT_MALLOC/KRT_CALLOC` 失败返回 NULL 且 `KrtMemoryReportError` 为空，同类失败均无信号。

### [一般] 59-7｜emit_load_high 的 ARG/VAR 打包字段分支缺少 <=64 守卫
- 位置：`KroInteger.inc:110-113`（ARG）、`121-124`（VAR）
- 类别：8 未检查前置条件 / 6 契约破坏
- 触发条件：`KrtTokenIntegerBits(value->value_type) > 64` 的值带有非零 `packed.bit_width`。
- 后果：把该字段当作高字加载（`emit_load_packed_field` 只取一个 64 位字段），宽值高字静默错误；低字路径 `KroCodegen.c:609/657` 均有 `KrtTokenIntegerBits(...) <= 64` 守卫，此处缺失。
- 最小修复：与 `KroCodegen.c:609/657` 一致，增加 `KrtTokenIntegerBits(value->value_type) <= 64` 条件。
- 备注：当前 `allocate_packed_field` 对 128 位返回 fragment=0，故为潜在/契约性缺陷；若将来允许宽值打包即触发。

### [一般] 59-8｜emit_shift_imm 不校验 count，静默截断为 uint8
- 位置：`KroInteger.inc:8-13`（12）
- 类别：8 未检查前置条件（移位）/ 1 静默失败
- 触发条件：调用方传入负数或 >255 的 `count`。
- 后果：`(uint8_t)count` 静默截断，x86 再按 6/5 位取模，实际移位次数与预期不符，生成错误码且无提示；对比 `emit_integer_shift:339-343` 对动态计数做了显式范围守卫。
- 最小修复：在辅助函数内断言 `0 <= count < 操作数宽度`，或返回失败并由调用方报告。
- 备注：当前所有调用方（`emit_extend_scalar`、`emit_normalize_pair`、`emit_integer_shift`、`emit_shift_by_constant`）传入值均在 0..63，属潜在缺陷。

### [一般] 59-9｜与 Vm 后端语义分叉：Vm 对 LSHIFT/RSHIFT（及 AND/OR/XOR/MOD 等）静默丢弃
- 位置：`KroInteger.inc:739-834`（Kro 正常生成移位/位运算），对照 `VmCodegen.c:233-289`（`default: break`）
- 类别：3 语义分叉 / 4 阶段不对齐
- 触发条件：同一份含 `KRT_IR_LSHIFT/RSHIFT`（以及 AND/OR/XOR/MOD/LE/GE/NE）的 IR 分别用 Kro 与 Vm 后端编译。
- 后果：Vm 后端这些指令落入 `default: break`，指令被静默丢弃，运算结果与 Kro 不一致（Kro 侧实现正确）。
- 最小修复：Vm 后端补齐 opcode 实现，或在驱动层禁止用 Vm 编译含这些指令的 IR 并给出诊断。
- 备注：缺陷在 Vm 后端；按“与 Vm 语义一致性”标准记录，供跨后端整改。

### [一般] 59-10｜与 X86 后端符号右移语义分叉：X86 恒用 SAR
- 位置：`KroInteger.inc:350`、`370`（无符号用 SHR），对照 `X86Codegen.c:878`（恒 `sar rax, cl`）
- 类别：3 语义分叉
- 触发条件：无符号整数的 `KRT_IR_RSHIFT`，且被移出位的最高有效位为 1。
- 后果：Kro 得逻辑移位（SHR），X86 得算术移位（SAR），同型同值结果不同；X86 后端全文无 `KrtTokenIsUnsigned` 处理。
- 最小修复：统一由类型符号性选择 `shr/sar`（X86 侧补齐无符号分支）。
- 备注：缺陷在 X86 侧；Kro 侧实现与类型系统一致。
