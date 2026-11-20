# Chunk 01 抽取

## 覆盖范围
- 原文标题（行号）：`## Codex — 2026-11-09T13:01:33.097Z`（第161行）；审计报告正文自第163行起
- 读取范围：第161–749行（含「致命」1–42、「高」43–75、「中」76–90、「疑似待验证」、ArkLink 第一批/Loader/Resolver/BackendCommon 各批）
- 已删除：进度话语、环境上下文、子代理通知、「已核查未发现」段（第548–552行）、优先级建议

## 问题条目

### [致命] 1｜emit_binary_op DIV/MOD 有符号除法未符号扩展
- 位置：`X86Codegen.c:434-444`
- 类别：1/4
- 触发条件：任何负数除法/取模
- 后果：静默错误结果或进程崩溃（`INT_MIN/-1` 直接 SIGFPE）
- 最小修复：有符号 `cqo+idiv`，无符号 `xor rdx,rdx+div`；除零生成显式诊断/返回值，并保证 `.Ldiv_by_zero_%s` 标签生成条件与指令生成一致（见第12条）
- 备注：—

### [致命] 2｜emit_compare 无符号/浮点比较误用有符号 setcc
- 位置：`X86Codegen.c:463-499`
- 类别：4
- 触发条件：unsigned 或 float 比较
- 后果：条件分支静默错误
- 最小修复：按 IR 类型选 `setb/seta/setbe/setae`；浮点用 `ucomisd`+`seta/sete`
- 备注：—

### [致命] 3｜KRT_IR_POW 参数寄存器按 Windows ABI 硬编码
- 位置：`X86Codegen.c:820-858`
- 类别：4/11
- 触发条件：Linux 下调用 `pow`
- 后果：参数错位（应 rdi/rsi，实际 rcx/rdx），结果错误或崩溃
- 最小修复：加 `#ifdef __linux__` 分支
- 备注：—

### [致命] 4｜未实现 opcode 被静默丢弃
- 位置：`X86Codegen.c:789-792`（`KRT_IR_ALLOC`/`NOP`）+ `emit_instruction` default
- 类别：1/5
- 触发条件：IR 含 ALLOC、AND/OR/XOR/MOD/移位/LE/GE/NE/COPY/CAST/ARRAY_STORE/LOADPTR/STOREPTR 等未实现 opcode
- 后果：指令被静默丢弃，生成错误代码，无任何诊断
- 最小修复：实现或显式 `KrtError` 拒绝
- 备注：—

### [致命] 5｜KRT_IR_CAST 实现为空操作
- 位置：`X86Codegen.c:782-787`
- 类别：13
- 触发条件：任何类型转换
- 后果：int/float/bool 转换全部静默无效
- 最小修复：按源/目标类型插入 `cvtsi2sd`/`cvttsd2si` 等
- 备注：—

### [致命] 6｜栈大小截断导致越界写
- 位置：`X86Codegen.c:19-59、163-165、271-273`
- 类别：4/8
- 触发条件：temp 数 >32 或 spill 多
- 后果：`mov [rbp - offset]` 越界写，栈破坏（`temp_space` 上限截为 256 字节；regalloc 偏移无上限；`temp_stack_base` 硬编码）
- 最小修复：栈大小以 regalloc 的 `stack_space_needed` 为准，断言所有偏移 < `stack_size`
- 备注：—

### [致命] 7｜未分配 temp 的 load/store 位置不一致
- 位置：`X86Codegen.c:259-301`
- 类别：1/4
- 触发条件：temp 未分配（TEMP_LOC_NONE）
- 后果：同一 temp 的 load/store 位置不一致，读到未初始化栈
- 最小修复：统一分配策略，load 也分配或报错
- 备注：—

### [致命] 8｜X86_LIVESET_BITS=256 致高编号 temp 干扰边丢失
- 位置：`X86RegAlloc.h:7` + `X86RegAlloc.c:28-36、90-97`
- 类别：8
- 触发条件：函数 temp 数 >256
- 后果：高编号 temp 的干扰边全部丢失 → 寄存器分配冲突 → 静默错误结果
- 最小修复：动态位集/哈希，或显式限制并报错
- 备注：—

### [致命] 9｜conflict graph 节点/加边失败静默丢失
- 位置：`X86RegAlloc.c:253-275`
- 类别：1/8
- 触发条件：并发 temp >256
- 后果：`conflict_graph_get_or_create_node` 返回 NULL、`add_edge` 静默 return、`build` 不检查 → 干扰边丢失 → 错误分配
- 最小修复：动态扩容或拒绝编译
- 备注：—

### [致命] 10｜liveness 数组按无上限容量分配/填充不全
- 位置：`X86RegAlloc.c:200-250`
- 类别：8/4
- 触发条件：超大函数/超大 temp 索引
- 后果：`temp_capacity*sizeof(int)` 溢出堆；liveness 只填充 `i<10000` 但 build 遍历全部指令 → 读取零初始化 liveness → 干扰丢失 → 错误代码
- 最小修复：用 `KrtSizeMultiply` 检查溢出；指令数设上限报错；liveness 填充覆盖全部指令
- 备注：—

### [致命] 11｜mov-mov 链消除误删仍被使用的 temp 定义
- 位置：`X86CodeOpt.c:333-345`（`apply_mov_mov_chain_elimination`）
- 类别：1
- 触发条件：COPY 链中间 temp 被后续指令使用
- 后果：静默错误结果
- 最小修复：检查 use-count 或保留 inst2
- 备注：—

### [致命] 12｜peephole 优化器全项目死代码
- 位置：`X86CodeOpt.c:428`（`x86_optimize_peephole`）
- 类别：5
- 触发条件：全项目（grep 只有定义，无任何调用者）
- 后果：优化未生效；若将来接入会引入第11条错误
- 最小修复：接入流水线或删除
- 备注：—

### [致命] 13｜VM 目标只生成 main 函数
- 位置：`VmCodegen.c:280-300`（`KrtVmCodegenGenerate`）
- 类别：5
- 触发条件：多函数模块选择 VM 目标
- 后果：其他函数完全不生成 → 调用未定义符号/功能缺失
- 最小修复：遍历 `module->functions` 生成全部函数
- 备注：—

### [致命] 14｜VM generate_function switch 大量 opcode 落入 default
- 位置：`VmCodegen.c:200-280`
- 类别：1
- 触发条件：IR 含 AND/OR/XOR/MOD/移位/LE/GE/NE/BRANCH/JUMP/COPY/CAST/ARRAY_STORE/LOADPTR/STOREPTR/STRCAT/DOUBLE_TO_STRING 等
- 后果：静默错误代码（LOAD/IMM 只 push 操作数，不写 result）
- 最小修复：实现或报错
- 备注：—

### [致命] 15｜VM 带返回值调用多弹一次栈
- 位置：`VmCodegen.c:160-180`（`emit_function_call`）
- 类别：8
- 触发条件：带返回值的调用赋给局部变量
- 后果：result 为 VAR 时 `SET_LOCAL+POP` 后又无条件 `POP` → VM 栈失衡/下溢
- 最小修复：确认 `OP_SET_LOCAL` 语义后移除多余 POP
- 备注：疑（`OP_SET_LOCAL` 是否弹栈待查 VM 实现）

### [致命] 16｜VM temp 索引截断为 uint8
- 位置：`VmCodegen.c:40-55`（`emit_value_push` ARG/TEMP）
- 类别：8
- 触发条件：索引 >255
- 后果：`(uint8_t)index` 截断、无范围检查；`get_temp_index = local_count + temp_id` 可 >255 → 读写错误局部槽
- 最小修复：断言/报错
- 备注：—

### [致命] 17｜VM 输出文件名 strcat 越界
- 位置：`Compiler.c:208-216`
- 类别：8
- 触发条件：输出路径长度接近 255 且无 `.`
- 后果：`char output_filename[256]; KRT_STRNCPY(...); strcat(...,".ebc")` 越界写栈
- 最小修复：`snprintf` 并检查长度
- 备注：—

### [致命] 18｜module_fits_kro_limits 只统计定义数不统计最大索引
- 位置：`Compiler.c:108-146`
- 类别：4
- 触发条件：temp 索引跳号/稀疏
- 后果：绕过 `KRT_KRO_MAX_TEMP_STORAGE` 限制 → KRO 后端栈/表溢出
- 最小修复：跟踪最大索引+1
- 备注：—

### [致命] 19｜空遍历死循环
- 位置：`Compiler.c:148-160`
- 类别：5
- 触发条件：总是
- 后果：无用代码
- 最小修复：删除
- 备注：—

### [致命] 20｜module_has_native_pointers 覆盖不全
- 位置：`Compiler.c:100-107`
- 类别：4
- 触发条件：X86/VM 目标遇到 LOADPTR/STOREPTR/ARRAY_STORE/ARRAY_LOAD
- 后果：后端收到指针操作却生成错误代码，而非拒绝
- 最小修复：补齐指针类 opcode
- 备注：—

### [致命] 21｜AST→IR 失败返回值被忽略
- 位置：`Compiler.c:176`（`KrtIrGenerateFromAst`）
- 类别：1
- 触发条件：AST→IR 失败
- 后果：仍继续优化和代码生成，对空/错误 module 继续，错误信息缺失
- 最小修复：检查返回值并报错
- 备注：—

### [致命] 22｜KrtMemoryReportError 为空操作，内存错误全被吞
- 位置：`Core/Memory/Allocator.c:425-435`（原标 380-390）
- 类别：1/2 → 静默失败 / 未检查前置条件
- 触发条件：任何内存错误
- 后果：release 下 MALLOC_FAILED/INVALID_FREE/DOUBLE_FREE/CANARY_CORRUPTED/USE_AFTER_FREE 全部被静默吞掉，所有内存安全诊断丢失
- 最小修复：实现为打印/记录并设置全局错误标志
- 备注：【修订 G1】G1 确认该函数确为空操作；分配失败路径为活路径且零诊断

### [致命] 23｜KRT_ASSERT 被编译为 ((void)0)
- 位置：`Core/Utils/KrtCommon.h:90`
- 类别：12
- 触发条件：任何依赖 assert 的不变量
- 后果：不变量破坏无检测
- 最小修复：改为真实 `assert` 或带诊断检查
- 备注：—

### [致命] 24｜后端改写共享 IR（SSA 优化）
- 位置：`X86Codegen.c:1019`（`emit_function` 调用 `KrtIrSsaOptimize`）
- 类别：4/7
- 触发条件：同一 module 被多个后端/多次代码生成
- 后果：IR 被静默改写，结果不可预测（KroCodegen.c:1296 注释称 SSA 不稳定被禁用，X86/Vm 却启用）
- 最小修复：流水线层面统一优化一次，后端不得改 IR
- 备注：—

### [致命] 25｜畸形 IR 操作数未检查 NULL/数量
- 位置：`X86Codegen.c:751-772`
- 类别：8
- 触发条件：BRANCH/JUMP/CALL 的 `operands[..].data.name/function_name` 为 NULL；CALL 在 `operand_count==1` 时构造 `&operands[1]` 越界指针
- 后果：崩溃/UB
- 最小修复：检查操作数数量和 NULL
- 备注：—

### [致命] 26｜codegen_get_var_offset 失败返回 0 与合法 offset 冲突
- 位置：`X86Codegen.c:233-257`
- 类别：1/4
- 触发条件：>100 个变量或未登记变量
- 后果：变量别名/错误读写，静默丢变量
- 最小修复：返回 -1 并诊断
- 备注：—

### [致命] 27｜param_count>0 但 params==NULL 静默跳过保存
- 位置：`X86Codegen.c:900-930`（`emit_function_prologue`）
- 类别：1
- 触发条件：`param_count>0` 且 `params==NULL`
- 后果：参数读到未初始化栈
- 最小修复：断言 params 非空或报错
- 备注：—

### [致命] 28｜emit_load_value 字符串常量/宽整数边界缺失
- 位置：`X86Codegen.c:340-379`
- 类别：8
- 触发条件：越界字符串常量/宽整数
- 后果：STRING_CONST 不检查边界直接生成 `string_const_%d`；INTEGER 用 `%llu` 打印可能 128 位值 → 汇编错误或截断
- 最小修复：边界检查+按类型打印
- 备注：疑（`value->data.integer` 实际类型待查）

### [致命] 29｜128 位除零触发 SIGILL
- 位置：`KroInteger.inc:288-336`（`emit_wide_division`）
- 类别：1/8
- 触发条件：128 位除零
- 后果：发 `0F 0B`（`ud2`）触发 SIGILL，而非返回错误/诊断；除零判定只比较部分位
- 最小修复：生成运行时检查/诊断
- 备注：—

### [致命] 30｜128 位移位只检查低 64 位
- 位置：`KroInteger.inc:338-390`（`emit_integer_shift`）
- 类别：8
- 触发条件：128 位移位 count 高 64 位非零
- 后果：错误移位
- 最小修复：检查完整 128 位
- 备注：—

### [致命] 31｜未知条件码默认 JNE
- 位置：`KroInteger.inc:836-851`（`integer_condition`）
- 类别：1
- 触发条件：未知条件码
- 后果：非法比较被静默当 NE
- 最小修复：报错
- 备注：—

### [致命] 32｜KroRegisterPacking 参数/寄存器数组越界
- 位置：`KroRegisterPacking.inc:227-292`
- 类别：8
- 触发条件：`current_param_count>128` 或 reg 索引≥16
- 后果：`ctx->arg_packed` 大小 128；`next_write[16]` 硬编码 16 而 `g_packed_regs` 只有 5 → 越界写
- 最小修复：用 `KRT_KRO_PACKED_REG_COUNT` 并检查
- 备注：—

### [致命] 33｜ARG 索引≥32 移位 UB
- 位置：`KroTailUpdates.inc:145-148`
- 类别：8
- 触发条件：ARG 索引≥32
- 后果：`1u << index` 移位 UB；`current_param_count` 可 >32
- 最小修复：用 64 位掩码或检查范围
- 备注：—

### [致命] 34｜find_temp_location 返回 NULL 未检查即写
- 位置：`KroTailCalls.inc:116-118、149-156`
- 类别：8
- 触发条件：temp 未登记
- 后果：空指针写
- 最小修复：检查并报错
- 备注：—

### [致命] 35｜call→ret 链表遍历无环检测
- 位置：`KroTailCalls.inc:119-124`
- 类别：8
- 触发条件：IR 链表被破坏
- 后果：死循环/越界
- 最小修复：加环检测/步数上限
- 备注：—

### [致命] 36｜KroLeafReturn 多处未检查操作数数量/NULL
- 位置：`KroLeafReturn.inc:85-88、98、106-123、162-167`
- 类别：8/6
- 触发条件：畸形 IR
- 后果：`entry->succs[0/1]`、`compare->operands[0/1]`、`leaf->preds[0]`、`entry->first_inst` 未检查数量和 NULL → 崩溃
- 最小修复：断言/检查
- 备注：—

### [致命] 37｜128 位/非常规宽度浮点转换未支持
- 位置：`KroFloatCasts.inc:68-86、88-123`
- 类别：4/6
- 触发条件：128 位/非常规宽度
- 后果：`emit_integer_float_cast` 只处理 width 4/8；`emit_float_wide_cast` 假定输入为 double 位模式、未校验类型/宽度 → 错误代码
- 最小修复：显式支持或拒绝
- 备注：—

### [致命] 38｜allocate_packed_field bits=0/负数静默丢字段
- 位置：`KroRegisterPacking.inc:2-26`
- 类别：1/8
- 触发条件：bits=0/负数
- 后果：bits 未检查 ≤64/正数；bits==0 静默返回，字段无存储但调用方继续
- 最小修复：检查并报错
- 备注：—

### [致命] 39｜emit_store_value 未检查 dest->data.name/符号查找失败
- 位置：`KroInteger.inc:255-278`
- 类别：8
- 触发条件：`dest->data.name==NULL` 或符号查找失败
- 后果：未检查 NULL；`find_or_alloc_local_var` 可能返回 NULL，slot 未初始化 → 崩溃
- 最小修复：检查
- 备注：—

### [致命] 40｜KroInteger 多处未检查 operand_count>=2
- 位置：`KroInteger.inc:651-695、739-766、853-863`
- 类别：8/6
- 触发条件：畸形 IR
- 后果：多处未检查 `operand_count>=2` 即读 `operands[0/1]` → 越界读
- 最小修复：检查
- 备注：—

### [致命] 41｜definition_block 分配失败后状态未回滚
- 位置：`KroRegisterPacking.inc:386`
- 类别：7/9 → 静默失败 / 未检查前置条件
- 触发条件：分配失败
- 后果：直接 return false，但 `eligible` 数组可能已部分修改，调用方未回滚 → 状态不一致
- 最小修复：回滚或清理
- 备注：【修订 G1】分配失败为活路径且零诊断（非死代码）；后果为状态不一致

### [致命] 42｜KRO 后端静默截断超量参数
- 位置：`KroCodegen.c:1317`
- 类别：1/4
- 触发条件：`param_count > KRT_KRO_MAX_ARGS`
- 后果：`current_param_count` 被静默截断，超出参数丢弃 → 调用约定错位
- 最小修复：拒绝并报错（Compiler.c 已检查，后端再截断说明契约不一致）
- 备注：—

### [高] 43｜emit_return 生产路径残留 printf 调试输出
- 位置：`X86Codegen.c:676-685`
- 类别：13/5
- 触发条件：生成 main 的 sys_exit 时
- 后果：污染编译器 stdout
- 最小修复：删除或 `KRT_DEBUG` 守卫
- 备注：—

### [高] 44｜KrtX86Generate 未检查 module/函数名 NULL
- 位置：`X86Codegen.c:1053+`
- 类别：8
- 触发条件：空 module
- 后果：崩溃（`module`、`module->main_function->name` 未检查 NULL）
- 最小修复：检查
- 备注：—

### [高] 45｜codegen_free_register 未检查 ctx/reg NULL
- 位置：`X86Codegen.c:201-231`
- 类别：8
- 触发条件：reg 为 NULL
- 后果：`strcmp` 崩溃
- 最小修复：检查
- 备注：—

### [高] 46｜var_name NULL 生成 [rbp-0]
- 位置：`X86Codegen.c:223-231、303-311`
- 类别：8
- 触发条件：var_name 为 NULL
- 后果：生成 `[rbp-0]`
- 最小修复：检查
- 备注：—

### [高] 47｜[rbp-offset] 无边界检查
- 位置：`X86Codegen.c:271-273`
- 类别：8
- 触发条件：offset 为负/超大
- 后果：内存破坏
- 最小修复：断言
- 备注：—

### [高] 48｜temp_array_ensure_capacity 契约脆弱
- 位置：`X86Codegen.c:68-87`
- 类别：8
- 触发条件：needed 为负；capacity 初始 0 时 `while(0<=0)` 死循环（实际初始 256 不触发）
- 后果：契约脆弱
- 最小修复：检查 needed≤0
- 备注：—

### [高] 49｜spill_rcx_if_used 只保存 rcx
- 位置：`X86Codegen.c:318-331`
- 类别：1/4
- 触发条件：emit_call 14 寄存器快照不存在时
- 后果：调用破坏寄存器未完整保存
- 最小修复：确认设计/补全保存
- 备注：疑（需确认设计）

### [高] 50｜regalloc_get_reg_name/stack_offset 未检查 result NULL
- 位置：`X86RegAlloc.c:380-395`
- 类别：8
- 触发条件：调用方传 NULL
- 后果：崩溃
- 最小修复：检查
- 备注：—

### [高] 51｜liveness_analysis_run 未检查 analysis/func NULL
- 位置：`X86RegAlloc.c:140-160`
- 类别：8
- 触发条件：NULL 传入
- 后果：崩溃
- 最小修复：检查
- 备注：—

### [高] 52｜优化器对 .data.name 直接 strcmp
- 位置：`X86CodeOpt.c:60-70、110-120`
- 类别：8
- 触发条件：`.data.name` 为 NULL
- 后果：崩溃
- 最小修复：检查
- 备注：—

### [高] 53｜module_has_extended_integers 覆盖不全
- 位置：`Compiler.c:176-190`
- 类别：4
- 触发条件：全局数组元素类型/string 常量
- 后果：与 `module_has_native_pointers` 一样覆盖不全
- 最小修复：补齐
- 备注：—

### [高] 54｜VM 序列化返回值被忽略
- 位置：`Compiler.c:208-216`
- 类别：1
- 触发条件：`KrtBytecodeGeneratorSerializeToFile` 失败
- 后果：静默失败
- 最小修复：检查并报错
- 备注：—

### [高] 55｜未识别 target 静默回退 KrtIrPrint
- 位置：`Compiler.c:230-240`
- 类别：1/11
- 触发条件：target 未识别
- 后果：用户请求的目标被静默替换
- 最小修复：报错
- 备注：—

### [高] 56｜module_fits_kro_limits 栈数组/除零风险
- 位置：`Compiler.c:108-146`
- 类别：8
- 触发条件：常量较大时栈溢出；`hash % (KRT_KRO_MAX_LOCAL_VARS*2)` 在常量为 0 时除零
- 后果：栈溢出/除零
- 最小修复：检查常量值并防护
- 备注：疑（需确认常量值）

### [高] 57｜CompilerPipeline Codegen 未检查 config NULL / target 静默回退
- 位置：`CompilerPipeline.c:320-330`
- 类别：8/1
- 触发条件：`pipeline->config` 为 NULL；target 未识别
- 后果：崩溃或静默错误目标（回退 X86_ASM）
- 最小修复：检查并报错
- 备注：—

### [高] 58｜record_stage_result 静默丢弃/OOB 读
- 位置：`CompilerPipeline.c:19-25`
- 类别：1/8
- 触发条件：`stage_count>=8`；stage 越界
- 后果：阶段结果静默丢弃；`KrtCompileStageNames[stage]` OOB 读
- 最小修复：检查 stage 范围并扩容
- 备注：—

### [高] 59｜ReadSource fread 短读未检测
- 位置：`CompilerPipeline.c:130-160`
- 类别：1
- 触发条件：I/O 错误
- 后果：源文件不完整仍继续编译（尾部未初始化）
- 最小修复：检查 `read_bytes==file_size`
- 备注：—

### [高] 60｜Lex 词法错误诊断不具体
- 位置：`CompilerPipeline.c:170-200`
- 类别：2
- 触发条件：词法错误
- 后果：只报 "unterminated string or character literal near line %d"，不报具体类型；`error_line` 可能过期 → 诊断与实际错误不符
- 最小修复：透传 lexer 具体诊断
- 备注：—

### [高] 61｜Execute 未检查 config / 预置 ast 模式假设
- 位置：`CompilerPipeline.c:520-560`
- 类别：6
- 触发条件：`pipeline->config` 为 NULL；ast 非空而 source_code 为空
- 后果：后续阶段假设可能不成立
- 最小修复：检查
- 备注：疑

### [高] 62｜match_tail_return 未检查 params NULL
- 位置：`KroTailCalls.inc:33-43`
- 类别：8
- 触发条件：`func->params` 为 NULL 而 `param_count>0`（`IrFunctionCreate` 允许）
- 后果：崩溃
- 最小修复：检查
- 备注：—

### [高] 63｜尾调用优化整段跳过与 plan_function_storage 顺序
- 位置：`KroTailCalls.inc:87`
- 类别：4
- 触发条件：`func->param_count != ctx->current_param_count`
- 后果：整段尾调用优化被跳过；设置顺序需确认
- 最小修复：确认阶段顺序
- 备注：疑（阶段不对齐）

### [高] 64｜operands[i+1] 前置条件未显式断言
- 位置：`KroTailCalls.inc:265-299`、`KroTailUpdates.inc:117`
- 类别：6
- 触发条件：访问 `call->operands[i+1]` 依赖 `match_tail_return` 的 operand_count 前置
- 后果：契约未文档化，易被破坏
- 最小修复：加断言
- 备注：—

### [高] 65｜KroTailUpdates find_temp_location NULL 未检查
- 位置：`KroTailUpdates.inc:192-195`
- 类别：8
- 触发条件：`find_temp_location` 返回 NULL
- 后果：崩溃
- 最小修复：检查
- 备注：—

### [高] 66｜KroTailUpdates:228 分支可能把未加载寄存器当有效
- 位置：`KroTailUpdates.inc:228`
- 类别：1
- 触发条件：`left_reg==-1 && right_reg==target`
- 后果：`right_reg == target && left_reg != target` 分支选 RAX，把未加载的 left 当有效
- 最小修复：修正分支条件
- 备注：疑（需验证）

### [高] 67｜KroLeafReturn:152 返回 NULL 语义可能相反
- 位置：`KroLeafReturn.inc:152`
- 类别：1
- 触发条件：调用方按函数名/语义解读返回值
- 后果：`return ctx->tail_site_count && !ctx->rotated_tail_leaf ? NULL : leaf;` 语义可能与函数名/调用方相反
- 最小修复：核对调用方是否把 NULL 当失败
- 备注：疑（需验证调用方）

### [高] 68｜emit_load_packed_field 未检查 field/bit 参数
- 位置：`KroRegisterPacking.inc:28-40`
- 类别：8
- 触发条件：`field` 为 NULL；bit_offset/bit_width 非法
- 后果：bit_offset==0 且 bit_width==0 时 `emit_extend_scalar` 行为未定义
- 最小修复：检查
- 备注：—

### [高] 69｜KroRegisterPacking params NULL / 宽度未校验
- 位置：`KroRegisterPacking.inc:544-547、574-579`
- 类别：8
- 触发条件：`func->params[i]` 为 NULL；params/local 类型宽度非法
- 后果：`allocate_value_stack` 可能分配错误大小
- 最小修复：检查
- 备注：—

### [高] 70｜scalar_low_register arg_packed 越界/未知类型
- 位置：`KroInteger.inc:415-434`
- 类别：8
- 触发条件：同第32条越界；`value->value_type` 为 VOID/未知
- 后果：越界/未定义
- 最小修复：检查
- 备注：—

### [高] 71｜emit_imm_op64 负数 imm32 无符号比较
- 位置：`KroInteger.inc:483-493`
- 类别：8
- 触发条件：`value >= 0xffffffffffffff80`
- 后果：无符号比较表示负数 imm32，`emit_u32` 截断语义需确认
- 最小修复：确认语义并检查
- 备注：疑

### [高] 72｜emit_multiply_pair/constant_shift_count 符号未校验
- 位置：`KroInteger.inc:697-716`
- 类别：8
- 触发条件：`amount->value_type` 未校验
- 后果：未检查输入寄存器内容/符号；`value >= (KrtUInt128)width` 比较可能因符号扩展错误
- 最小修复：校验符号/类型
- 备注：疑

### [高] 73｜INT_MIN/-1 的 MOD 未处理 RDX
- 位置：`KroInteger.inc:782-814`
- 类别：8
- 触发条件：INT_MIN/-1
- 后果：MOD 直接 RAX=0，未处理 RDX；128 位 INT_MIN 除 -1 的 ABI 未确认
- 最小修复：处理 RDX/确认 ABI
- 备注：疑

### [高] 74｜emit_load_condition 对 float64 按整数 test
- 位置：`KroInteger.inc:195-202`
- 类别：4
- 触发条件：IR 未先把 float64 条件转 bool
- 后果：语义错误
- 最小修复：确认 IR 生成器契约并修正
- 备注：疑（需确认契约）

### [高] 75｜KroCodegen.c:1296 注释与 X86/Vm 启用 SSA 矛盾
- 位置：`KroCodegen.c:1296`
- 类别：13/4
- 触发条件：阅读/维护时
- 后果：注释「禁用SSA优化…SSA不稳定」与 X86/Vm 启用 SSA 矛盾；注释含个人名字
- 最小修复：统一策略并清理注释
- 备注：—

### [中] 76｜X86 runtime 符号映射与 Kro 后端分叉
- 位置：`X86Codegen.c:643-649`
- 类别：3
- 触发条件：`print_string/println_string` 改名
- 后果：其他 runtime 符号无映射；与 Kro 后端符号处理分叉
- 最小修复：统一符号映射表
- 备注：—

### [中] 77｜call %s 未定义符号无诊断
- 位置：`X86Codegen.c:649`
- 类别：2
- 触发条件：调用未定义符号
- 后果：链接时才发现
- 最小修复：生成阶段检查符号表
- 备注：—

### [中] 78｜LOADPTR/STOREPTR 忽略 size/类型
- 位置：`X86Codegen.c:798-818`
- 类别：3/4
- 触发条件：指针加载/存储
- 后果：与 Kro Sized 路径分叉
- 最小修复：统一 sized 访问
- 备注：—

### [中] 79｜STRCAT/INT_TO_STRING/DOUBLE_TO_STRING 固定符号调用
- 位置：`X86Codegen.c:820-858`
- 类别：3
- 触发条件：这些 runtime 调用
- 后果：未检查参数类型/符号存在；与 Kro 后端分叉
- 最小修复：统一 runtime 调用约定
- 备注：—

### [中] 80｜emit_call 无条件 push 14 寄存器/对齐公式可疑
- 位置：`X86Codegen.c:594-597、623-629、631-641、651-663`
- 类别：8
- 触发条件：函数调用
- 后果：无条件 push 全部 14 个寄存器（含 rax），save_count 逻辑冗余；outgoing 对齐公式可疑；恢复顺序需验证
- 最小修复：补测试/核对栈布局
- 备注：疑（栈布局可能正确但极复杂）

### [中] 81｜比较指令重复实现整数 setcc
- 位置：`X86Codegen.c:728-737`
- 类别：4
- 触发条件：float/unsigned 比较
- 后果：同第2条，重复实现
- 最小修复：合并到 emit_compare
- 备注：—

### [高] 82｜KrtSafeMalloc(0)/KrtSafeCalloc(0) 静默返回 NULL
- 位置：`Core/Memory/Allocator.c:159-162、213-216`（宏 `Core/Utils/KrtCommon.h:77-81`）
- 类别：1 → 静默失败 / 未检查前置条件
- 触发条件：`size==0` 或 malloc/calloc 失败
- 后果：调用方无法区分 size==0 与 OOM；所有分配失败路径为活路径且零诊断，NULL 静默传播/解引用
- 最小修复：返回非空或明确契约；调用点检查 NULL 并报错
- 备注：【修订 G1】原判「调用方可能误判 OOM」修正为「分配失败是活路径、零诊断」；严重度由中上调为高

### [中] 83｜KrtMemoryDumpBlocks/DumpStats 死代码
- 位置：`Core/Memory/Allocator.c:340-350`
- 类别：5
- 触发条件：调用内存统计
- 后果：循环但不做事；空实现
- 最小修复：实现或删除
- 备注：—

### [中] 84｜KrtArenaRealloc 不收缩
- 位置：`Core/Memory/Arena.c:155-165`
- 类别：6
- 触发条件：`new_size<=old_size`
- 后果：直接返回 ptr 不收缩；调用方若假设新大小则越界
- 最小修复：文档化或返回新大小
- 备注：—

### [中] 85｜KrtArenaStats 生产路径打印统计
- 位置：`Core/Memory/Arena.c:190-210`
- 类别：13
- 触发条件：调用统计
- 后果：污染 stdout
- 最小修复：删除或调试守卫
- 备注：—

### [中] 86｜KRT_STRNCPY 宏 n==0 越界
- 位置：`Core/Utils/KrtCommon.h:80-86`
- 类别：8
- 触发条件：n==0
- 后果：`dest[n-1]` 越界
- 最小修复：检查 n>0
- 备注：【修订 G1】宏无守卫（KrtCommon.h:77-81），分配类宏同样缺乏失败守卫

### [中] 87｜KRO_OBJ 与 EXE_PLATFORM 同一语义两处实现
- 位置：`Compiler.c:108-146`
- 类别：3
- 触发条件：`KRT_TARGET_KRO_OBJ`/`KRT_TARGET_EXE_PLATFORM`
- 后果：都调用 `KrtKrtGenerate`，重复实现
- 最小修复：合并为一个入口
- 备注：—

### [中] 88｜CompilerPipeline 重复包含头文件
- 位置：`CompilerPipeline.c:1-15`
- 类别：13
- 触发条件：编译
- 后果：`<string.h>`、`<sys/stat.h>` 重复包含
- 最小修复：清理
- 备注：—

### [中] 89｜X86 优化器无测试覆盖且算法错误
- 位置：`X86CodeOpt.c:333-345`
- 类别：12
- 触发条件：—
- 后果：优化器无测试覆盖，且算法本身错误（第11条）
- 最小修复：补 use-count 断言和测试
- 备注：—

### [中] 90｜Test/ 目录缺少关键回归测试
- 位置：`Test/`
- 类别：12
- 触发条件：—
- 后果：未覆盖除零、unsigned/float 比较、CAST、POW ABI、栈偏移溢出、>256 temp、VM 多函数/缺失 opcode、内存错误诊断等不变量
- 最小修复：为每个致命项加回归测试
- 备注：—

### [疑] 疑似-1｜X86RegAlloc liveness 区间可能欠近似
- 位置：`X86RegAlloc.c:200-250`
- 类别：8
- 触发条件：循环中 use 在 def 之前
- 后果：`[def_pos, last_use_pos]` 欠近似 → 干扰边丢失 → 错误分配
- 最小修复：构造循环 IR 验证并修正
- 备注：疑似待验证

### [疑] 疑似-2｜emit_call 栈布局/恢复顺序待验证
- 位置：`X86Codegen.c:594-663`
- 类别：8
- 触发条件：函数调用
- 后果：栈布局/恢复顺序是否完全正确未知
- 最小修复：反汇编生成的汇编并单步验证
- 备注：疑似待验证（与第80条相关）

### [疑] 疑似-3｜value->data.integer 类型与 %llu 匹配性待查
- 位置：`X86Codegen.c:340-379`
- 类别：8
- 触发条件：打印整数字面量
- 后果：`value->data.integer` 是否 128 位与 `%llu` 是否匹配未知
- 最小修复：查 `KrtIRValue` 定义
- 备注：疑似待验证（与第28条相关）

### [疑] 疑似-4｜KroTailUpdates:228 分支逻辑待验证
- 位置：`KroTailUpdates.inc:228`
- 类别：1
- 触发条件：`left_reg==-1 && right_reg==target`
- 后果：分支逻辑可能错误
- 最小修复：构造对应 IR 验证
- 备注：疑似待验证（同第66条）

### [疑] 疑似-5｜KroLeafReturn:152 返回 NULL 语义待查
- 位置：`KroLeafReturn.inc:152`
- 类别：1
- 触发条件：调用方解读返回值
- 后果：返回 NULL 语义未知
- 最小修复：查调用方
- 备注：疑似待验证（同第67条）

### [疑] 疑似-6｜KRT_KRO_MAX_LOCAL_VARS 实际值待确认
- 位置：`Compiler.c:114`
- 类别：8
- 触发条件：—
- 后果：判断栈数组是否溢出/除零
- 最小修复：确认常量值
- 备注：疑似待验证（与第56条相关）

### [疑] 疑似-7｜OP_SET_LOCAL 是否弹栈待查
- 位置：`VmCodegen.c:160-180`
- 类别：8
- 触发条件：带返回值调用
- 后果：决定双 POP 是否为 bug
- 最小修复：查 VM 实现
- 备注：疑似待验证（与第15条相关）

### [疑] 疑似-8｜KroInteger 符号/宽度边界待构造用例
- 位置：`KroInteger.inc:483-493、697-716、782-814`
- 类别：8
- 触发条件：128 位边界值
- 后果：符号/宽度边界行为未知
- 最小修复：构造 128 位用例验证
- 备注：疑似待验证（与第71/72/73条相关）

### [中] 第一批-1｜target 有效性双重判断 + native default 不可达
- 位置：`ArkLink/src/Core/Arklink.c:184-191` + `LinkerNative.c:104-114`
- 类别：语义分叉 + 死代码
- 触发条件：target 校验
- 后果：session 层判一次、native 层再判一次；`LinkerNative.c:111-113` 的 `default` 永不可达；两处规则若漂移则一处失效
- 最小修复：统一到单层校验
- 备注：—

### [中] 第一批-2｜符号解析失败丢弃具体诊断
- 位置：`ArkLink/src/Core/LinkerNative.c:76-88`
- 类别：诊断缺失
- 触发条件：符号解析失败
- 后果：只写 "Symbol resolution failed"，丢弃 resolver 的具体符号/位置信息；对比 loader 失败路径（行59）保留 `diag.message`，同层不一致
- 最小修复：透传 resolver 诊断
- 备注：—

### [中] 第一批-3｜后端链接失败丢弃细节
- 位置：`ArkLink/src/Core/LinkerNative.c:116-127`
- 类别：诊断缺失
- 触发条件：后端链接失败
- 后果：只写 "Backend linking failed"，丢弃后端细节
- 最小修复：透传后端诊断
- 备注：—

### [高] 第一批-4｜backend_input==NULL 仍调用后端 link
- 位置：`ArkLink/src/Core/LinkerNative.c:90-98`
- 类别：契约破坏
- 触发条件：`plan.backend_input == NULL`
- 后果：跳过 image_base 赋值后仍以 NULL 调用 `ark_backend_*_link`；后端是否处理 NULL 未断言
- 最小修复：检查 NULL 并报错
- 备注：—

### [中] 第一批-5｜Main.c -o/-t 缺参数静默丢弃
- 位置：`ArkLink/src/Main.c:51,53`
- 类别：静默失败
- 触发条件：`-o`/`-t` 缺参数
- 后果：`i+1 < argc` 为假时该分支不匹配，`-o` 被静默丢弃，继续用默认输出名
- 最小修复：报错
- 备注：—

### [中] 第一批-6｜Main.c 未知选项静默跳过
- 位置：`ArkLink/src/Main.c:63`
- 类别：静默失败
- 触发条件：未知选项（如 `-x`）
- 后果：以 `-` 开头直接跳过，无报错
- 最小修复：报错
- 备注：—

### [高] 第一批-7｜inputs 槽位置 NULL 但计数未增
- 位置：`ArkLink/src/Core/Arklink.c:130-139`
- 类别：契约破坏
- 触发条件：`strdup` 失败
- 后果：`inputs` 已 realloc 扩容但 `input_count` 未增，槽位置 NULL；调用者见失败但 session 内部状态已变
- 最小修复：失败回滚
- 备注：—

### [中] 第一批-8｜stack_size=0 未检查
- 位置：`ArkLink/src/Core/Arklink.c:117-123`
- 类别：未检查前置条件
- 触发条件：`stack_size = 0`
- 后果：直接接受，后端会生成零栈保留的程序
- 最小修复：范围校验
- 备注：—

### [中] 第一批-9｜output_kind/subsystem 非法值未校验
- 位置：`ArkLink/src/Core/Arklink.c:78-84,101-107`
- 类别：未检查前置条件
- 触发条件：传非法值
- 后果：直接接受，无范围校验
- 最小修复：范围校验
- 备注：—

### [中] 第一批-10｜error_buffer 只保留最后一次诊断
- 位置：`ArkLink/src/Core/Arklink.c:154-159`
- 类别：共享可变状态
- 触发条件：多次失败
- 后果：旧诊断被覆盖
- 最小修复：累积诊断
- 备注：—

### [高] 第一批-11｜四个 session 配置 API 只写不读 + backend_data 未管理
- 位置：`ArkLink/src/Core/Arklink.c`（session 层）+ 各后端（`BackendPe.c:925`、`BackendElf.c:1669`）
- 类别：配置漂移 / 静默失败
- 触发条件：用户通过 `output_kind`、`subsystem`、`stack_size`、`entry_point` API 配置
- 后果：四字段全部只写不读（native 层结构体声明但从不使用，后端硬编码）；`backend_data` 从未被赋值或释放 → 用户配置被静默忽略
- 最小修复：把配置传到后端或使用统一字段；管理 backend_data 生命周期
- 备注：关键发现

### [中] 第一批-12｜session 与后端枚举语义分叉
- 位置：`ArkLink/src/Core/Arklink.c`（`ArkLinkOutputKind`）vs 后端（`ArkOutputType`）
- 类别：语义分叉
- 触发条件：输出类型选择
- 后果：两套枚举描述同一概念，session 那套从未传到后端
- 最小修复：统一枚举并传递
- 备注：—

### [高] Loader-1｜COFF loader 不解析任何结构即返回 OK
- 位置：`LoaderCoff.c:6-49`
- 类别：死代码 + 静默失败
- 触发条件：任意 `.obj`/`.o` 输入
- 后果：只读文件、不解析 COFF 结构、不建 section/symbol/reloc，返回 OK；真 COFF 目标文件被静默变成空单元，符号全丢但链接"成功"
- 最小修复：实现 COFF 解析或显式拒绝
- 备注：—

### [高] Loader-2｜ELF sh_offset+sh_size 加法溢出绕过范围检查
- 位置：`LoaderElf.c:133,141-147,309,345,352,432,450`
- 类别：未检查前置条件
- 触发条件：恶意/损坏 ELF（`sh_offset` 接近 UINT64_MAX）
- 后果：加法溢出回绕绕过范围检查 → `file_data+sh_offset` 越界指针 → 崩溃/越界读
- 最小修复：减法形式检查并先校验 `offset < size`
- 备注：—

### [中] Loader-3｜32 位 ELF 先用 64 位结构体解析
- 位置：`LoaderElf.c:268-287`
- 类别：阶段不对齐
- 触发条件：32 位 ELF
- 后果：`elf_context_create` 先用 64 位结构体解析并做范围检查，class/endianness 检查在其后；32 位文件可能用错布局解析
- 最小修复：先判 class/endianness 再解析
- 备注：—

### [高] Loader-4｜r_info 符号索引越界未校验
- 位置：`LoaderElf.c:455-458,488`
- 类别：未检查前置条件
- 触发条件：`r_info` 符号索引 ≥ 符号表大小
- 后果：`elf_sym_idx--` 后仍越界，`reloc.sym_idx` 未校验 → 下游 resolver 越界访问符号表
- 最小修复：校验索引范围
- 备注：—

### [高] Loader-5｜未知 r_type 默认映射 ABS64
- 位置：`LoaderElf.c:481-483`
- 类别：静默失败
- 触发条件：未知 `r_type`
- 后果：默认映射为 `ARK_RELOC_ABS64`，生成错误的重定位，运行时地址错乱
- 最小修复：报错或显式支持
- 备注：—

### [高] Loader-6｜sh_entsize 不匹配仍按错误步长解析
- 位置：`LoaderElf.c:349,449`
- 类别：契约破坏
- 触发条件：`sh_entsize ≠ sizeof(Elf64_Sym_Loader)`
- 后果：符号/重定位表按错误步长解析，全部错位
- 最小修复：校验 sh_entsize
- 备注：—

### [高] Loader-7｜reloc malloc 失败/fread 短读静默跳过
- 位置：`LoaderKro.c:309-312,315`
- 类别：静默失败 / 未检查前置条件
- 触发条件：reloc malloc 失败或 fread 短读
- 后果：`continue`/静默跳过，重定位全部丢失但加载成功
- 最小修复：检查返回值并报错
- 备注：【修订 G1】malloc 失败为活路径、零诊断

### [中] Loader-8｜.bss 符号被当作未定义
- 位置：`LoaderKro.c:256,262`
- 类别：语义错误
- 触发条件：`.bss` 中的符号
- 后果：`kro_to_actual[4]=0` → bss 符号 `section_index=0`（未定义），链接报未解析
- 最小修复：修正映射
- 备注：—

### [高] Loader-9｜desc->name 超长 strncpy 不写 NUL
- 位置：`Loader.c:85,122`
- 类别：未检查前置条件
- 触发条件：`desc->name` 长度 ≥ `sizeof(sec->name)`
- 后果：`strncpy` 不写 NUL，`strcmp`/`printf` 越界读
- 最小修复：检查长度并强制 NUL
- 备注：—

### [高] Loader-10｜add_symbol 浅拷贝 name 致非法 free
- 位置：`Loader.c:95-110`（destroy 行50）
- 类别：契约破坏
- 触发条件：调用者传栈上/静态符号名
- 后果：`add_symbol` 浅拷贝 `name`，`destroy` 行50 `free(sym->name)` → free 非法指针崩溃；对比 `create` 用 strdup，同层不一致
- 最小修复：深拷贝 name
- 备注：—

### [中] Loader-11｜LoaderKro 四处空 #ifdef KRO_DEBUG 块
- 位置：`LoaderKro.c:6,65-66,241-246,266-267`
- 类别：死代码
- 触发条件：定义 KRO_DEBUG
- 后果：4 处空 `#ifdef` 块
- 最小修复：删除或填入调试代码
- 备注：—

### [高] Loader-12｜fread 短读致 string_table 未初始化
- 位置：`LoaderKro.c:232-238`
- 类别：未检查前置条件
- 触发条件：`fread` 短读
- 后果：返回值未检查，`string_table` 含未初始化字节，`kro_string_at` 越界读
- 最小修复：检查 fread 返回值
- 备注：—

### [中] Loader-13｜SHN_ABS/SHN_COMMON 符号被当作外部符号
- 位置：`LoaderElf.c:398-404`
- 类别：语义错误
- 触发条件：`SHN_ABS`/`SHN_COMMON` 符号
- 后果：映射为 `section_index=0`（未定义），绝对符号被当作外部符号
- 最小修复：修正特殊节索引映射
- 备注：—

### [中] Loader-14｜.text 无 EXEC 时强制标记可执行
- 位置：`LoaderElf.c:208-210`
- 类别：语义篡改
- 触发条件：`.text` 的 `sh_flags` 无 EXEC
- 后果：强制加 `ARK_SECTION_EXEC`，不可执行段被标记可执行
- 最小修复：按 sh_flags 设置
- 备注：—

### [中] Loader-15｜.init_array/.comment/.note 等 PROGBITS 静默丢弃
- 位置：`LoaderElf.c:220-229`
- 类别：静默失败
- 触发条件：`.init_array`/`.comment`/`.note` 等 PROGBITS
- 后果：静默丢弃，不报错
- 最小修复：报错或支持
- 备注：—

### [高] Loader-16｜add_symbol/add_reloc realloc 失败静默丢失
- 位置：`LoaderElf.c:423,493`、`LoaderKro.c:278,323`
- 类别：静默失败 / 未检查前置条件
- 触发条件：realloc 失败
- 后果：返回值未检查，符号/重定位静默丢失
- 最小修复：检查返回值并报错
- 备注：【修订 G1】realloc 失败为活路径、零诊断

### [中] Loader-17｜STB_GNU_UNIQUE 等绑定静默降级为 GLOBAL
- 位置：`LoaderElf.c:417-419`
- 类别：静默降级
- 触发条件：`STB_GNU_UNIQUE` 等绑定
- 后果：默认映射 GLOBAL
- 最小修复：保留/显式处理绑定语义
- 备注：—

### [致命] Resolver-1｜import_module 空串致 imports[] 堆溢出写
- 位置：`Resolver.c:487-511`
- 类别：未检查前置条件 → 堆溢出
- 触发条件：任一符号 `import_module != NULL` 但为空串（`LoaderKro.c:274` 允许任意 binding/flags 透传）
- 后果：统计用 `import_module[0]!='\0'`（行489），填充只判 `!=NULL`（行499）→ 填充数 > `import_count` → `imports[]` 堆溢出写
- 最小修复：统计与填充判定一致，并校验 idx < import_count
- 备注：本批唯一致命

### [高] Resolver-2｜imports 数量字段与实际内容不符
- 位置：`Resolver.c:498-510`
- 类别：契约破坏
- 触发条件：同 Resolver-1
- 后果：行510 用统计值 `import_count` 而非实际 `idx`，数量字段与实际内容不符
- 最小修复：使用实际 idx
- 备注：—

### [中] Resolver-3｜局部符号重命名泄漏 name
- 位置：`Resolver.c:257`
- 类别：资源泄漏
- 触发条件：局部符号重命名
- 后果：`strdup(renamed)` 赋给 `sym.name`，浅拷贝进 `sym_table`；`plan_destroy:588` 只 free 数组不 free name → 每次重命名泄漏一份
- 最小修复：销毁时释放 name
- 备注：—

### [中] Resolver-4｜total_sections==0 提前返回未释放
- 位置：`Resolver.c:314-319`
- 类别：资源泄漏
- 触发条件：`total_sections==0`
- 后果：提前返回未 `free(unit_sec_start/unit_sym_start)`
- 最小修复：返回前释放
- 备注：—

### [中] Resolver-5｜sections calloc 失败未释放前置分配
- 位置：`Resolver.c:321-327`
- 类别：资源泄漏
- 触发条件：`sections` calloc 失败
- 后果：未释放 `unit_sec_start/unit_sym_start`（对比行330-336 释放了，同层不一致）
- 最小修复：失败路径释放
- 备注：—

### [中] Resolver-6｜reloc_table_add 失败未释放 sections/rename_table
- 位置：`Resolver.c:463-475`
- 类别：资源泄漏
- 触发条件：`reloc_table_add` 失败
- 后果：未释放 `sections[i].data`、未调用 `rename_table_free`
- 最小修复：失败路径清理
- 备注：—

### [高] Resolver-7｜imports calloc 失败静默跳过全部导入
- 位置：`Resolver.c:495-511`
- 类别：静默失败 / 未检查前置条件
- 触发条件：`imports` calloc 失败
- 后果：`if (imports)` 为假时静默跳过全部导入处理 → 生成无导入表的 PE
- 最小修复：calloc 后检查 NULL 并报错
- 备注：【修订 G1】calloc 失败为活路径、零诊断

### [高] Resolver-8｜exports calloc 失败静默跳过全部导出
- 位置：`Resolver.c:536-563`
- 类别：静默失败 / 未检查前置条件
- 触发条件：`exports` calloc 失败
- 后果：静默跳过全部导出
- 最小修复：calloc 后检查 NULL 并报错
- 备注：【修订 G1】calloc 失败为活路径、零诊断

### [高] Resolver-9｜sym->name==NULL 时 strdup(NULL)
- 位置：`Resolver.c:551`
- 类别：未检查前置条件
- 触发条件：`sym->name == NULL`（`LoaderKro.c:270` 允许）
- 后果：`strdup(NULL)` 崩溃
- 最小修复：检查 NULL
- 备注：—

### [高] Resolver-10｜rename_table_lookup 内 strcmp(local_name, NULL)
- 位置：`Resolver.c:439,177`
- 类别：未检查前置条件
- 触发条件：重定位符号名 NULL
- 后果：`strcmp(local_name, NULL)` 崩溃
- 最小修复：检查 NULL
- 备注：—

### [中] Resolver-11｜未定义符号 fprintf 绕过 session logger
- 位置：`Resolver.c:448`
- 类别：错误处理不一致
- 触发条件：未定义符号
- 后果：直接 `fprintf(stderr,...)` 绕过 session logger；`LinkerNative.c:78` 又写 "Symbol resolution failed"，双重且不一致
- 最小修复：统一走 logger
- 备注：—

### [中] Resolver-12｜注释与实现脱节（zero 是否有效定义）
- 位置：`Resolver.c:250` vs `Resolver.h:19`
- 类别：注释与实现脱节
- 触发条件：—
- 后果：头文件注释"zero is a valid definition"，实现 `defined = section_index > 0`（0=未定义）
- 最小修复：统一注释/实现
- 备注：—

### [高] Resolver-13｜未定义 main 被硬编码强制定义
- 位置：`Resolver.c:268-277`
- 类别：语义错误
- 触发条件：`extern int main()` 未定义
- 后果：硬编码把名为 main 的未定义符号强制 `defined=1` 并指向 CODE 段
- 最小修复：移除硬编码，按正常解析
- 备注：—

### [中] Resolver-14｜四处空块/空循环
- 位置：`Resolver.c:263-264,424-427,401-402,522-523`
- 类别：死代码
- 触发条件：—
- 后果：4 处空块/空循环
- 最小修复：删除或补实现
- 备注：—

### [中] Resolver-15｜局部符号名+单元号超长 snprintf 截断
- 位置：`Resolver.c:255`
- 类别：未检查前置条件
- 触发条件：局部符号名 + 单元号 ≥ 512 字节
- 后果：`snprintf` 截断 → 重命名冲突，重定位解析错
- 最小修复：检查截断并报错
- 备注：—

### [中] Resolver-16｜image_base 两处设置语义分叉
- 位置：`Resolver.c:485` vs `LinkerNative.c:91-97`
- 类别：语义分叉
- 触发条件：—
- 后果：`image_base` 两处设置（resolver 硬编码 0x140000000，native 按 target 覆盖）
- 最小修复：统一设置点
- 备注：—

### [中] Resolver-17｜reloc_table.relocs 双所有者别名
- 位置：`Resolver.c:483-484,576`
- 类别：共享可变状态
- 触发条件：—
- 后果：`reloc_table.relocs` 同时赋给 `backend_input->relocs` 与 `plan->relocs`，两所有者别名
- 最小修复：明确单一所有权
- 备注：—

### [致命] BackendCommon-1｜unique_modules calloc 未判空即写入
- 位置：`ArkLink/src/Backend/BackendCommon.c:521,524-541`
- 类别：未检查前置条件 / 静默失败
- 触发条件：`count` 较大或内存不足时 calloc 返回 NULL
- 后果：紧接着 `unique_modules[unique_count++] = mod` → NULL 解引用崩溃
- 最小修复：calloc 后 `if (!unique_modules) { free(group); return NULL; }`
- 备注：【修订 G1】calloc 失败为活路径、零诊断

### [致命] BackendCommon-2｜imports[i].module 为 NULL 时 strcmp 崩溃
- 位置：`ArkLink/src/Backend/BackendCommon.c:562`
- 类别：未检查前置条件
- 触发条件：任一 import 的 module 为 NULL（第一遍 526 行只跳过统计，未从池中剔除）
- 后果：第二遍 `strcmp(imports[i].module, unique_modules[m])` → `strcmp(NULL, ...)` 崩溃
- 最小修复：第二遍同样 `if (!imports[i].module) continue;`，或第一遍整体排除
- 备注：—

### [致命] BackendCommon-3｜重定位 offset 加法溢出致越界写
- 位置：`ArkLink/src/Backend/BackendCommon.c:386,393,400`
- 类别：整数溢出 / 未检查前置条件
- 触发条件：恶意/损坏 `.kro`/ELF 重定位 offset 接近 SIZE_MAX
- 后果：`proc->offset + 2/4/8 <= size` 加法回绕，检查通过，随后 `memcpy(data+offset,...)` 越界写
- 最小修复：改为 `proc->offset <= size - 2` 等减法形式，并先校验 `offset < size`
- 备注：—

### [致命] BackendCommon-4｜section_count>0 但 sections==NULL 仍取地址
- 位置：`ArkLink/src/Backend/BackendCommon.c:250-252`
- 类别：未检查前置条件 / 契约破坏
- 触发条件：上游构造 `ArkBackendInput` 时 section_count 非零而 sections 未分配
- 后果：直接 `&input->sections[i]` → NULL 解引用崩溃
- 最小修复：`if (input->section_count && !input->sections) return NULL;` 并在 `Backend.h` 文档化前置条件
- 备注：—

### [致命] BackendCommon-5｜virtual_address+aligned_size 溢出致镜像布局错位
- 位置：`ArkLink/src/Backend/BackendCommon.c:282`
- 类别：整数溢出
- 触发条件：超大 section size
- 后果：`sec->virtual_address + ark_backend_align_up(sec->virtual_size, section_alignment)` 无溢出检查 → `sec_end` 回绕 → `code_segment_end`/`data_segment_end` 计算错误
- 最小修复：`__builtin_add_overflow` 或 `if (sec->virtual_address > UINT64_MAX - aligned) return NULL;`
- 备注：—

### [高] BackendCommon-6｜重定位未应用静默返回
- 位置：`ArkLink/src/Backend/BackendCommon.c:346-349,374-375,405`
- 类别：静默失败 / 诊断缺失
- 触发条件：`ark_reloc_apply_elf` 中 `!data/!proc/offset>=size`；action `default`；未知 field_size
- 后果：直接 `return`/静默不写；调用方 `ark_reloc_process_all` 无法感知 → 生成错误目标文件但链接"成功"
- 最小修复：返回 `int` 错误码或至少 `fprintf(stderr,...)` 带 section/offset 诊断
- 备注：—

### [高] BackendCommon-7｜PE 重定位写死偏移 0 忽略 proc->offset
- 位置：`ArkLink/src/Backend/BackendCommon.c:433-449`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：PE 后端处理任何重定位
- 后果：field_size 分支写死 `memcpy(data,...)`（偏移 0），完全忽略 `proc->offset`；ELF 版本（378-405）使用 `proc->offset` → 所有 PE 重定位写到 section 起始处，必然错位
- 最小修复：统一使用 `proc->offset` 并做边界检查；抽出唯一 `ark_reloc_write_field()`
- 备注：—

### [高] BackendCommon-8｜未知 reloc 类型静默 continue
- 位置：`ArkLink/src/Backend/BackendCommon.c:496-497`
- 类别：静默失败
- 触发条件：Loader 映射出未知类型（如 `LoaderElf.c:481-483` 默认 ABS64 或未来新类型）
- 后果：重定位静默丢弃，生成错误镜像
- 最小修复：记录诊断并返回错误计数，或至少在 `ARK_DEBUG` 下输出
- 备注：—

### [高] BackendCommon-9｜PE GENERATE_BASE_REL/action default 静默返回
- 位置：`ArkLink/src/Backend/BackendCommon.c:408-431`
- 类别：静默失败
- 触发条件：`ARK_RELOC_GENERATE_BASE_REL` 或未知 action
- 后果：静默返回，重定位未应用
- 最小修复：返回错误
- 备注：—

### [高] BackendCommon-10｜symbols calloc 失败仍计入 module
- 位置：`ArkLink/src/Backend/BackendCommon.c:567-570`
- 类别：静默失败 / 契约破坏
- 触发条件：`symbols = calloc(sym_count, ...)` 失败
- 后果：`continue`，该 module 的 `symbol_count` 保持 0，但 `module_count` 已计入 → 下游看到存在但无符号的模块，可能生成空导入表项或误判
- 最小修复：失败时回滚已分配资源并返回 NULL
- 备注：【修订 G1】calloc 失败为活路径、零诊断

### [高] BackendCommon-11｜ark_buffer_add_string 失败哨兵冲突
- 位置：`ArkLink/src/Backend/BackendCommon.c:150-166`
- 类别：契约破坏 / 诊断缺失
- 触发条件：追加失败或偏移超限
- 后果：失败返回 `(uint32_t)-1`（UINT32_MAX），与 156-158 行溢出时返回的 UINT32_MAX 哨兵冲突 → 调用方无法区分
- 最小修复：引入独立错误码或输出参数
- 备注：—

### [高] BackendCommon-12｜buffer 错误信号三种不一致
- 位置：`ArkLink/src/Backend/BackendCommon.c:175-183`
- 类别：错误处理不一致
- 触发条件：`ark_buffer_get_rva`/`ark_buffer_get_string` 出错
- 后果：`!buf` 返回 0、溢出返回 UINT32_MAX；而 `ark_buffer_get_string`（168-173）用 NULL 表示错误 → 同层三种错误信号
- 最小修复：统一为返回 NULL/0 并加 `errno` 风格输出参数
- 备注：—

### [高] BackendCommon-13｜ark_buffer_align 忽略 append_zero 失败
- 位置：`ArkLink/src/Backend/BackendCommon.c:185-196`
- 类别：静默失败
- 触发条件：`ark_buffer_append_zero` 返回 `(size_t)-1`
- 后果：对齐未完成但 size 未变 → 后续 section 偏移错位
- 最小修复：检查返回值并向上传播
- 备注：—

### [高] BackendCommon-14｜layout_get_section 返回 NULL 时 VA 静默置 0
- 位置：`ArkLink/src/Backend/BackendCommon.c:500-506`
- 类别：静默失败
- 触发条件：`reloc->section_index >= section_count`
- 后果：`p_vaddr` 静默置 0，重定位按 VA=0 计算 → PC-relative 重定位算错
- 最小修复：检查 NULL 并记录诊断
- 备注：—

### [高] BackendCommon-15｜unique_modules 借用指针所有权不明
- 位置：`ArkLink/src/Backend/BackendCommon.c:539`
- 类别：契约破坏
- 触发条件：调用方释放 imports 字符串
- 后果：`unique_modules[unique_count++] = mod` 借用 `imports[i].module` 指针（非常量所有权），`ark_import_group_destroy` 不释放 `module_name` → 悬垂
- 最小修复：在 `Backend.h` 明确所有权，或内部 `strdup`
- 备注：—

### [高] BackendCommon-16｜两处空 #ifdef ARK_DEBUG 块
- 位置：`ArkLink/src/Backend/BackendCommon.c:353-354,364-365`
- 类别：死代码
- 触发条件：release 构建
- 后果：永久为空
- 最小修复：删除或填入实际调试代码
- 备注：—

### [中] BackendCommon-17｜未知 field_size 默认按 32 位处理
- 位置：`ArkLink/src/Backend/BackendCommon.c:17-19`
- 类别：静默失败
- 触发条件：未知 `field_size`
- 后果：`ark_backend_calc_pc_relative` 默认 `p_vaddr + 4`，把非法枚举当 32 位
- 最小修复：`default: return 0;` 或断言
- 备注：—

### [中] BackendCommon-18｜alignment==0 直接返回原值
- 位置：`ArkLink/src/Backend/BackendCommon.c:22-29`
- 类别：静默失败 / 契约破坏
- 触发条件：`alignment == 0` 或溢出
- 后果：非法对齐被接受；溢出静默 clamp 到 UINT64_MAX → 布局错误
- 最小修复：`alignment == 0` 返回错误或断言 1
- 备注：—

### [中] BackendCommon-19｜align_up_32 溢出静默 clamp
- 位置：`ArkLink/src/Backend/BackendCommon.c:31-34`
- 类别：静默失败
- 触发条件：溢出
- 后果：静默 clamp 到 UINT32_MAX，调用方无感知
- 最小修复：提供 `int* ok` 输出或返回 0 并文档化
- 备注：—

### [中] BackendCommon-20｜new_cap 分支后恒假判断
- 位置：`ArkLink/src/Backend/BackendCommon.c:97-100`
- 类别：死代码
- 触发条件：—
- 后果：`new_cap = capacity <= SIZE_MAX/2 ? capacity*2 : needed;` 之后 `if (new_cap < needed)` 恒假
- 最小修复：删除
- 备注：—

### [中] BackendCommon-21｜未知 section kind 默认 DATA
- 位置：`ArkLink/src/Backend/BackendCommon.c:198-215`
- 类别：静默失败
- 触发条件：新增 section 类型未更新
- 后果：`classify_section_kind` 默认 `ARK_SEGMENT_DATA` → 段属性错误
- 最小修复：`default` 返回错误或 `ARK_SEGMENT_UNKNOWN` 并由调用方拒绝
- 备注：—

### [中] BackendCommon-22｜internal 判定范围不含 capacity 区
- 位置：`ArkLink/src/Backend/BackendCommon.c:112-126`
- 类别：未检查前置条件
- 触发条件：`data` 指向 `[data+size, data+capacity)`（未初始化区）
- 后果：被判为外部，`memmove` 读取未初始化内存并追加
- 最小修复：判定范围改为 `< buf->capacity`，或拒绝非常规指针
- 备注：—

### [中] BackendCommon-23｜VA/文件偏移累加无溢出检查
- 位置：`ArkLink/src/Backend/BackendCommon.c:246-248`
- 类别：整数溢出
- 触发条件：多 section 大尺寸
- 后果：`current_va`/`current_file_offset` 持续累加 `ark_backend_align_up(...)` 回绕 → 镜像布局错乱
- 最小修复：每次累加检查 `> UINT64_MAX - delta`
- 备注：—

### [中] BackendCommon-24｜不可信 size 对齐后累加回绕
- 位置：`ArkLink/src/Backend/BackendCommon.c:267`
- 类别：整数溢出
- 触发条件：`input_sec->size` 被 clamp 成 UINT64_MAX
- 后果：`ark_backend_align_up(input_sec->size, section_alignment)` 后 `current_va += aligned_size` 回绕
- 最小修复：同上，加溢出检查
- 备注：—

### [中] BackendCommon-25｜无数据段与全 BSS 共用 image_size 公式
- 位置：`ArkLink/src/Backend/BackendCommon.c:313-316`
- 类别：语义分叉 / 静默失败
- 触发条件：无数据段；或存在数据段但全部 BSS
- 后果：无数据段时 `data_segment_start/end` 强制设为 `code_segment_end` 使 `image_size` 恰好等于代码段尾；全 BSS 时 `data_segment_start` 来自 BSS VA、`file_size` 不含 BSS，二者语义不同却共用同一公式
- 最小修复：分离 `image_size`（含 BSS）与 `file_size` 计算路径并断言
- 备注：—

### [中] BackendCommon-26｜import_group_destroy 不释放 module_name
- 位置：`ArkLink/src/Backend/BackendCommon.c:584-597`
- 类别：注释与契约
- 触发条件：调用方误以为拥有 module_name
- 后果：只释放 `symbols`/`modules`，不释放借用的 `module_name`；函数名暗示完整销毁 → 可能双重释放
- 最小修复：文档化或改为深拷贝
- 备注：—

### [中] BackendCommon-27｜weak 标志用裸数字 5
- 位置：`ArkLink/src/Core/NativeAbi.c:157`
- 类别：魔法数字 / 未检查前置条件
- 触发条件：非法 flags 值（flags 来自 manifest）
- 后果：`record.weak = flags == 5;` 无枚举校验 → weak 语义错乱
- 最小修复：定义 `enum { ARK_ABI_FLAG_WEAK = 5 }` 并校验取值集合
- 备注：—

### [中] BackendCommon-28｜expected_size 冗余算术易失配
- 位置：`ArkLink/src/Core/NativeAbi.c:148`
- 类别：可读性 / 潜在维护缺陷
- 触发条件：布局改动
- 后果：`expected_size = 6 + nominal_size - 6 + 2 + 16` 冗余算术易失配
- 最小修复：`nominal_size + 18` 并注释布局
- 备注：—
