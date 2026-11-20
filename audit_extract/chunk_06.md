# Chunk 06 抽取

## 覆盖范围
- 第三十六批：`Re.KrtC/src/Compiler/Middle/Ir/IrMemory.c`（全量 852 行）（原文 :3518）
- 第三十七批：`Re.KrtC/src/Compiler/Middle/Ir/IrSsa.c`（1524 行，含调用方交叉核对）（原文 :3625）
- 第三十八批：`IrSsa.c`（:792-1290，SSA 重命名与 lowering）（原文 :3762）
- 第三十九批：`IrOptimizer.c`（:1-300，常量折叠/常量传播）（原文 :3904）
- 第四十批：`IrOptimizer.c`（:300-500，DCE / CSE / 拷贝传播 / 强度削减）（原文 :4014；本范围仅含 S40-1，原文 :4024 为空白行）
- 修订应用：G1 已套用于 S36-3、S37-8、S37-13、S38-8、S38-11、S39-11；G3（第三十八批重要修正，原文 :3896）已套用于 S38-1~S38-15，S38-11 的 `prev` 疑似项撤回；G2（S28-1 作废）在本范围无对应条目。

## 问题条目

### [致命] S36-1｜死代码（整文件约 500 行零调用者）+ 语义分叉
- 位置：`IrMemory.c:160-852`（对象池 `pool_init`~`KrtIrPoolPrintStats`、惰性分配 `lazy_create_op`~`KrtIrLazyPrintStats`、性能统计 `KrtIrProfilerInit`~`KrtIrCountersPrint`）；声明于 `IrMemory.h:33-110`
- 类别：死代码 / 语义分叉
- 触发条件：任何编译流程。全库（排除 `IrMemory.c/h` 自身）grep `KrtIRPoolManager|KrtLazyAllocManager|KrtIRProfiler|KrtIRMemoryStats|KrtIRPerformanceCounters` 命中 0 次；池/惰性/统计全部 API 零外部调用者。
- 后果：① 项目 IR 节点分配实际只有 `Ir.c` 的 `ir_create_inst`→`KrtIrArenaAlloc` 一条路径，`IrMemory.c` 另有一套完整的「定长对象池 + free_list + block」分配器从未启用；arena 线性分配 vs 池化复用两套语义并存，未来接入即与 `Ir.c` 生命周期假设冲突。② 该文件 60% 以上代码（含全部诊断统计输出）不可达，`KrtIrPoolPrintStats` 的统计表永远打印不出来。
- 最小修复：删除 `IrMemory.c:160-852` 及 `IrMemory.h:33-110` 中全部池/惰性/统计 API；若保留对象池，则必须让 `Ir.c` 改走 `KrtIrPoolAlloc*` 并删除 arena 直配路径，只留一条分配语义。

### [高] S36-2｜静默失败 + 资源与生命周期（`current_pool` 单调前移，旧池空间永久浪费）
- 位置：`IrMemory.c:87-101`（`:88` `while (size > pool->size - pool->used && pool->next)`；`:92-100` 新建池；`:101` `arena->current_pool = pool;`）
- 类别：静默失败 / 资源生命周期
- 触发条件：`current_pool` 剩余空间小于本次 `size` 时（`Ir.c:1436` 用 `KrtIrArenaCreate(0)` → `KRT_DEFAULT_POOL_SIZE = 4KB`，见 `IrMemory.h:7`，池很小，极易触发）。例如池 1 剩 100 字节时来一个 200 字节分配。
- 后果：池 1 的 100 字节永远不会被再用（无回绕、无复用、无统计、无告警）；4KB 默认池 + 37 处 `KrtIrArenaAlloc` 调用点 → 池数量随编译规模无界增长，内存放大；`KrtIrArenaReset`（`:125-135`）只清 `used`，不回收多余池。
- 最小修复：分配失败跳池后把 `current_pool` 重置回 `arena->pools` 重新扫描（或维护空闲池链）；至少对 `pool_count` 超阈值时告警。
- 备注：`:88` 的 `pool->size - pool->used` 是 `size_t` 减法，`used > size` 时下溢为巨大正数 → 条件恒假 → 静默跳过该池，当前不变量下无断言保护。

### [高] S36-3｜静默失败（`pool_grow` 三条失败路径全部静默 `return`，`KrtIrPoolAlloc` 跟着静默返回 NULL）
- 位置：`IrMemory.c:172-193`（`:178-182` 溢出/对齐/乘法失败 → `return;`；`:183-186` arena 分配失败 → `return;`）、`IrMemory.c:219-247`（`:246` `return NULL;`）
- 类别：静默失败 / 诊断缺失
- 触发条件：`object_size > SIZE_MAX - sizeof(KrtPoolNode)`、`KrtSizeAlign`/`KrtSizeMultiply` 失败、arena 耗尽。
- 后果：池分配失败 → `KrtIrPoolAlloc` 返回 NULL → 调用方（若接入）直接解引用 → 空指针崩溃，崩溃点远离根因；或静默少分配导致 IR 缺失。全链无任何诊断。
- 最小修复：`pool_grow` 失败时 `KRT_COMPILE_ERROR("object pool grow failed: type=%s reason=...")`；`KrtIrPoolAlloc` 在 `:246` 前同样报错。
- 备注：【修订 G1】分配失败为活路径且零诊断（`KrtSafeMalloc` 失败返回 NULL，非 exit），本条结论不变、严重度维持高。

### [高] S36-4｜未检查前置条件（`KrtIrPoolFree` 不做归属/对齐校验，可把任意指针挂入 free_list）
- 位置：`IrMemory.c:269-281`（`:277` `KrtPoolNode* node = (void*)((char*)obj - offsetof(KrtPoolNode, data));`）
- 类别：未检查前置条件 / 共享可变状态
- 触发条件：任何错误配对，如 `KrtIrPoolFreeInst(mgr, (KrtIRInst*)some_stack_obj)`、`KrtIrPoolFreeBlock` 与 `KrtIrPoolFreeInst` 混用、或对非本 pool 分配的指针对齐偏移。
- 后果：`obj` 未校验是否落在某个 block 内、是否按 `node_size` 对齐，直接 `(char*)obj - offsetof(...)` 得到假 node 写进 `free_list` → 后续 `KrtIrPoolAlloc` 返回该指针 → 类型混淆 / 堆破坏 / 静默数据损坏。
- 最小修复：`pool_grow` 时把每个 block 的 `[start,end)` 记入 `pool->blocks`（该字段当前恒为 NULL，见 S36-6），释放时校验 `obj` 落在某 block 内且 `(obj - start) % node_size == 0`。

### [高] S36-5｜契约破坏 + 死代码（`KrtIrPoolManagerDestroy` 名为销毁，实为只丢指针；`blocks` 字段全程 NULL）
- 位置：`IrMemory.c:208-217`（仅置 `free_list=NULL`、`blocks=NULL`）、`IrMemory.c:165`（`pool->blocks = NULL`）、`IrMemory.h:59`（`KrtPoolNode* blocks;`）
- 类别：契约破坏 / 死代码
- 触发条件：调用 `KrtIrPoolManagerDestroy` 后再读 `manager->arena`，或任何读 `pool->blocks` 的代码。
- 后果：① `manager->arena` 未被清空，destroy 后 manager 处于「半失效」状态，复用即操作已失效状态；② `blocks` 字段从 `pool_init` 起恒为 NULL，全库无任何写入点（grep 只有 `IrMemory.c:165/215` 两处赋值）→ 死字段，且正是 S36-4 缺失的归属校验数据结构。
- 最小修复：`KrtIrPoolManagerDestroy` 增加 `manager->arena = NULL;`；删除 `blocks` 字段，或在 `pool_grow` 中真正维护它。

### [中] S36-6｜静默失败 + 死代码（`KrtIrPoolDefrag`/`DefragAll` 是空操作）
- 位置：`IrMemory.c:332-355`（`:339-342` 计算 `free_count` 后 `if (free_count < 2) return;`，`:344` `(void)free_count;`）
- 类别：静默失败 / 死代码
- 触发条件：任何调用（当前全库零调用者，见 S36-1）。
- 后果：函数名与注释暗示「碎片整理」，实际计算完 `free_count` 后什么都不做，仅 `(void)free_count;` 消警。调用方若接入会误以为碎片已整理，而 `Frag%` 统计（`:371`）仍按未整理状态计算 → 诊断与实际不符。
- 最小修复：实现真正的整理（重建 block、合并空闲节点），或删除该 API 及 `IrMemory.h:88-89` 声明。

### [中] S36-7｜未检查前置条件 + 整数溢出（统计计数器为 `int`，百分比计算可溢出为负）
- 位置：`IrMemory.c:357-386`（`:365` `int total_allocated = pool->alloc_count;`、`:371` `return (free_count * 100) / total_allocated;`、`:380` `int total = pool->hit_count + pool->miss_count;`、`:385` `(pool->hit_count * 100) / total`）、`IrMemory.h:60-63`（四个计数器均 `int`）
- 类别：未检查前置条件 / 静默失败
- 触发条件：大规模编译下 `free_count > INT_MAX/100`（约 2147 万）时 `free_count * 100` 溢出为负 → `KrtIrPoolFragmentationRate` 返回负百分比；`hit_count + miss_count` 亦可溢出。
- 后果：统计信息错误/为负，误导调优；整数除法还使 <1% 的比率恒显示 0%。
- 最小修复：计数器改 `uint64_t`；计算改 `(uint64_t)free_count * 100 / total_allocated`。

### [中] S36-8｜未检查前置条件（`KrtIrArenaAlloc` 对 `arena->current_pool == NULL` 无守卫）
- 位置：`IrMemory.c:79-88`（`:80` 只查 `!arena || size == 0`；`:87` 取 `current_pool`；`:88` 直接 `pool->size`）
- 类别：未检查前置条件
- 触发条件：`current_pool` 为 NULL 时（`KrtIrArenaCreate` 失败时直接返回 NULL，不留下半初始化 arena，故当前不可达；但 `KrtIrArenaReset`（`:125-135`）在 `arena->pools == NULL` 时不会把 `current_pool` 置 NULL，反之若未来新增 `ArenaClose/Detach` 即触发）。
- 后果：`:88` 空指针解引用 → SIGSEGV。
- 最小修复：`if (!arena || !arena->current_pool || size == 0) return NULL;`

### [中] S36-9｜契约破坏（`KrtIrArenaAlloc(arena, 0)` 返回 NULL，与「分配失败」不可区分）
- 位置：`IrMemory.c:79-82`（`size == 0` → NULL）对照 `IrMemory.c:110-123`（`KrtIrArenaStrdup` 用 `len + 1` 规避）
- 类别：契约破坏 / 诊断缺失
- 触发条件：任何直接 `KrtIrArenaAlloc(arena, 0)` 的调用。
- 后果：调用方无法区分「请求 0 字节（合法）」与「真 OOM」，易把正常空分配误判为内存耗尽而触发错误路径。`KrtIrArenaStrdup` 的 `len+1` 属正确绕行，但契约未文档化。
- 最小修复：在 `IrMemory.h:26` 声明处注释「size==0 返回 NULL 属正常语义」，或改为返回非 NULL 哨兵。

### [中] S36-10｜契约破坏（`KrtLazyOp.target` 一个字段两种解释，无类型标记区分）
- 位置：`IrMemory.h:96-103`（`void* target;`）、`IrMemory.c:484-501`（`RecordAlloc` 存 `void**`）、`IrMemory.c:504-522`（`RecordCopy` 存 `void**`）、`IrMemory.c:525-543`（`RecordInit` 存 `void*`）、`IrMemory.c:545-588`（`lazy_execute_op` 按 `op->type` 分别当 `void**` / `void*` 解引用）
- 类别：契约破坏 / 未检查前置条件
- 触发条件：调用方对 `KRT_LAZY_OP_INIT` 传入 `void**`（或反之），或 `RecordInit` 的 `target` 为指向指针的指针。
- 后果：`op->target` 被双重解引用语义混用，传错即静默写错地址（`memset` 到错误位置）或段错误；契约完全靠调用方自觉，无断言、无诊断。
- 最小修复：拆成 `void* target` + `void** slot` 两个字段，或改用 union 并在 DEBUG 下断言类型匹配。

### [疑似] S36-X1｜`KrtIrLazyRecordInit` 的 `target` 类型与 Alloc/Copy 不一致
- 位置：`IrMemory.c:525-543`（`RecordInit` 存 `void*`）对照 `:484-501`/`:504-522`（Alloc/Copy 存 `void**`）；`IrMemory.h:96-103`（`KrtLazyOp.target` 声明为 `void*`）
- 类别：疑似 / 需验证
- 触发条件：是否存在某个调用点把 `&ptr` 传给 `RecordInit`（那样 `memset` 会清零指针本身而非指向的缓冲区）。
- 后果：若存在该用法则静默写错内存；当前全库零调用者，无法从代码推断意图。
- 最小修复：查设计文档或 git 历史确认该 API 预期用法后再定。
- 备注：**需验证**；本条与 S36-10 相关。

### [疑似] S36-X2｜`KrtIrArenaAlloc` 的 `total_allocated` 溢出检查与 reset 的统计口径
- 位置：`IrMemory.c:84`（`size > SIZE_MAX - arena->total_allocated`）、`IrMemory.c:125-135`（`KrtIrArenaReset`）
- 类别：疑似 / 需验证
- 触发条件：`arena->total_allocated` 是跨池累计，而 `KrtIrArenaReset` 会清零；是否存在「reset 后 `total_allocated` 归零但 `pool_count` 不清零」导致统计口径不一致的调用序列。
- 后果：统计口径不一致（潜在），当前 `KrtIrArenaReset` 全库零调用者故不可达。
- 最小修复：**需验证** `KrtIrArenaReset` 的调用点。

### [致命] S37-1｜配置漂移 + 语义分叉（三条后端对同一 IR 跑不同的优化管线）
- 位置：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c:1296`（`// KrtIrSsaOptimize(func);  //禁用SSA优化,除了Airs_td以外的人不要打开它,SSA不稳定`）、`Re.KrtC/src/Compiler/Backend/X86/X86Codegen.c:1019`（`KrtIrSsaOptimize(func);`）、`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.c:220`（`KrtIrSsaOptimize(func);`）
- 类别：配置漂移 / 语义分叉
- 触发条件：同一份 `.krt` 源码分别用 Kro / X86 / Vm 后端编译。
- 后果：X86 与 Vm 在代码生成前会执行常量传播、复制传播、死代码删除（`IrSsa.c:1497-1508`），Kro 完全不执行。同一语义的程序在三条后端上得到不同的指令序列与不同的运行时行为（叠加 S37-2/S37-3 的有缺陷传播，等于三条后端三种语义）。注释还明确邀请「除了 Airs_td 以外的人不要打开它」——开关取值取决于开发者身份而非构建配置，属未文档化、未测试的配置漂移。
- 最小修复：把 SSA 优化与否提升为显式构建选项（`-O` 级别或 `--enable-ssa`），三条后端读同一配置；在修复 S37-2/S37-3 之前应统一关闭并在注释中记录原因与 issue 链接。

### [高] S37-2｜阶段不对齐 + 静默失败（`ssa_eliminate_dead_code` 只压缩 `insts[]`，不动 `first_inst` 链，DCE 对 X86/Vm 完全无效）
- 位置：`IrSsa.c:1314-1358`（`:1354` `block->insts[new_count++] = inst;`、`:1357` `block->inst_count = new_count;`）；对照后端读取点 `VmCodegen.c:228`（`KrtIRInst* inst = block->first_inst;`）、`X86RegAlloc.c:17`、`KroCodegen.c:497`、`KroRegisterPacking.inc:210/412/464/517/526`
- 类别：阶段不对齐 / 契约破坏
- 触发条件：X86/Vm 后端调用 `KrtIrSsaOptimize` → `ssa_eliminate_dead_code`。
- 后果：`KrtIRBasicBlock` 同时维护 `insts[]` 数组与 `first_inst`/`last_inst` 链表两份指令表示（`Ir.h:289-300`）。DCE 只压实 `insts[]` 并减 `inst_count`，完全不修改链表。X86/Vm 走链表 → 被判定为「死」的指令仍全部发射，DCE 静默无效；而 `KroRegisterPacking.inc:267`（`block->insts[index]`）走数组 → 一旦 Kro 打开 S37-1 那行注释，`position`（`:265` 用 `block->inst_count` 累加）与 `insts[index]` 的对应关系被打破，读到已删除指令或越界。同一份 IR 两种遍历语义，且无一致性断言。
- 最小修复：DCE 删除指令时同步维护 `first_inst`/`last_inst` 链（或统一为单一表示，删除 `insts[]`）；在 `KrtIrSsaOptimize` 末尾加 `KRT_IR_ASSERT` 校验 `inst_count` 与链表长度一致。

### [高] S37-3｜静默失败 / 语义错误（DCE 保留清单漏 `KRT_IR_CALL_INDIRECT`，间接调用被当死代码删除）
- 位置：`IrSsa.c:1346-1349`（保留条件只列 `RETURN/CALL/SYSCALL/BRANCH/JUMP/STORE/ALLOC/LOAD`）；产生点 `Ir.c:1016-1044`（`KrtIrCallIndirect` 返回 TEMP 结果）；调用点 `IrGen.c:1648`
- 类别：静默失败 / 未检查前置条件
- 触发条件：函数指针调用的返回值未被使用（如把调用当语句执行），且编译走 X86 或 Vm 后端。
- 后果：`KRT_IR_CALL_INDIRECT` 有副作用（调用外部代码）但不在保留清单里，其结果 temp 无人引用 → 被 DCE 删除 → 调用凭空消失，程序静默跳过函数指针调用。Kro 后端因 S37-1 未开 SSA 而侥幸躲过。
- 最小修复：保留清单加入 `KRT_IR_CALL_INDIRECT`（以及任何未来新增的 call 类 opcode）；更稳妥的做法是给 `KrtIROpcode` 增加「有副作用」属性表，DCE 查表而非手写枚举清单。

### [高] S37-4｜死代码 + 阶段不对齐（SSA 构造入口零调用者，「SSA 优化」跑在非 SSA IR 上）
- 位置：`IrSsa.c:1048-1072`（`KrtIrSsaConstruct`）、`IrSsa.c:1291-1312`（`KrtIrSsaVerify`）；调用统计：`KrtIrSsaConstruct`/`KrtIrSsaVerify`/`KrtIrSsaBuilderCreate`/`KrtIrSsaInsertPhis`/`KrtIrComputeDominance`/`KrtIrSsaLowerPhis`/`KrtIrSsaReplaceVarUses`/`KrtIrSsaRemoveStoreInsts`/`KrtIrSsaCollectVars`/`KrtIrVarTableCreate`/`KrtIrVarNewVersion`/`KrtIrPhiCreate` 外部调用者均为 0
- 类别：死代码 / 阶段不对齐
- 触发条件：任何编译流程。
- 后果：① 项目里 IR 从未被转成 SSA（变量版本化、phi 插入、phi 下沉全部不执行），但 `KrtIrSsaOptimize` 的名字与注释宣称是「SSA 优化」，实际是在普通 IR 上跑 temp 级的常量/复制传播。② `KrtIrSsaVerify` 用「存在 STORE 即非 SSA」作判据（`:1305-1307`），从未被调用，因此 IR 是否满足 SSA 前置条件无人校验，`KrtIrSsaOptimize` 直接在未验证的 IR 上运行。
- 最小修复：要么接入 `KrtIrSsaConstruct` 并在 `KrtIrSsaOptimize` 前调用 `KrtIrSsaVerify`（失败即报错），要么把 `KrtIrSsaOptimize` 重命名为 `KrtIrLocalTempPropagate` 并删除全部死代码（约 700 行）。

### [高] S37-5｜未检查前置条件（`KrtIrComputePostorder` 缓冲区越界 + 返回值导致调用方越界读）
- 位置：`IrSsa.c:345-424`（`:352-353` 按 `max_blocks` 分配 `blocks`/`visited`；`:366-370` `while (block) { blocks[block_count++] = block; ... }` 无上限；`:414` `return result_top;`；`:408-410` 只写 `i < max_blocks`）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：调用方传入的 `max_blocks` 小于 `func` 实际基本块数（唯一调用方 `KrtIrComputeDominance:487` 传的是 `count_blocks(func)`，当前恰好相等；但 `KrtIrComputePostorder` 是 `IrSsa.h` 公开 API）。
- 后果：`blocks[block_count++]` 与 `visited[current]` 越界写（堆破坏）；同时函数返回真实总数 `result_top`，调用方若按返回值遍历 `order[0..result_top-1]` 会读到未初始化/越界内存。返回值语义（「实际块数」还是「写入 order 的块数」）未文档化。
- 最小修复：`while (block && block_count < max_blocks)`；返回值改为实际写入 `order` 的数量，或返回 -1 表示 `max_blocks` 不足。

### [高] S37-6｜未检查前置条件（`KrtIrSsaInsertPhisForVar` 定长栈数组无界 + `pred_idx == -1` 越界索引）
- 位置：`IrSsa.c:683`（`int worklist_queue[KRT_SSA_MAX_BLOCKS];` = 4096，`IrSsa.c:6`）、`IrSsa.c:695-700`（`worklist_queue[worklist_back++] = i;` 无上限）、`IrSsa.c:726-733`（`int pred_idx = -1; ... if (!has_phi[pred_idx] && !has_store[pred_idx])`）
- 类别：未检查前置条件
- 触发条件：① CFG 块数接近/超过 4096 时 worklist 溢出（栈缓冲区溢出）；② `target_block->preds[p]` 未被 `collect_blocks` 收进 `blocks[]`（前驱块从 entry 不可达，或 `collect_blocks` 因 `max_blocks` 截断）→ `pred_idx` 保持 -1 → `has_phi[-1]` / `has_store[-1]` 越界读并可能越界写 `worklist_queue[-1]`。
- 后果：栈溢出 / 越界写，静默内存破坏或崩溃。
- 最小修复：`worklist_queue` 改堆分配（`KrtIrArenaAlloc(arena, actual_count * sizeof(int))`）并加 `worklist_back < actual_count` 断言；`pred_idx < 0` 时 `continue`。

### [中] S37-7｜未检查前置条件（`KrtIrVarTableCreate` 不校验 `bucket_count`）
- 位置：`IrSsa.c:19-39`（`:28-30` `bucket_count * sizeof(...)`；`:36` `hash % table->bucket_count`）
- 类别：未检查前置条件
- 触发条件：`bucket_count <= 0`。`KrtIrSsaBuilderCreate:228` 传常量 256 故当前不可达，但 `IrSsa.h:65` 是公开 API。
- 后果：`bucket_count == 0` → `:36` 除零（SIGFPE）；`bucket_count < 0` → `bucket_count * sizeof(ptr)` 溢出为巨大 `size_t` → 分配失败返回 NULL → `:36` 仍除零。
- 最小修复：`if (!arena || bucket_count <= 0) return NULL;`

### [中] S37-8｜静默失败（`KrtIrVarNewVersion` strdup 失败后静默丢弃，版本号已递增）
- 位置：`IrSsa.c:81-105`（`:87` `int new_version = existing ? (existing->version + 1) : 0;`、`:95` `var->name = KrtIrArenaStrdup(...)` 未检查、`:103` `var_table_add`）
- 类别：静默失败 / 未检查前置条件
- 触发条件：arena 分配失败（`KrtIrArenaAlloc` 在对齐/溢出检查失败时返回 NULL，`IrMemory.c:84-86`，非 exit）。
- 后果：`var->name == NULL` → `var_table_add`（`:64-75`）因 `!var->name` 静默 `return`，但该 `KrtIRVarVersion` 已被 `var` 持有且 `new_version` 已消耗 → 后续同名变量取到旧版本或找不到，版本号出现空洞，SSA 重命名静默错乱。全链无诊断。
- 最小修复：`var->name` 为空时 `KRT_COMPILE_ERROR("var version name alloc failed")`。
- 备注：【修订 G1】分配失败为活路径且零诊断，本条结论不变。

### [中] S37-9｜静默失败（`KrtIrVarVersionedName` 256 字节栈缓冲截断，可致不同变量同名）
- 位置：`IrSsa.c:112-120`（`:116` `char buffer[256];`、`:117` `snprintf(buffer, sizeof(buffer), "%s_%d", name, version);`）
- 类别：静默失败
- 触发条件：`strlen(name) + 数字 + 2 > 255`（变量名超过约 240 字符）。
- 后果：`snprintf` 静默截断 → 两个不同变量可能得到相同的版本名（`foo_1` 与 `fooXXXXX_1` 截断后相同）→ SSA 重命名把两个变量合并，静默数据错乱。无诊断。
- 最小修复：用 `KrtIrArenaAlloc` 按 `strlen(name) + 32` 动态分配，或截断时报错。

### [中] S37-10｜契约破坏（`KrtIrVarFindVersion` 忽略 `block` 参数；`KrtIrSsaCollectVars` 的 `arena` 参数未使用）
- 位置：`IrSsa.c:107-110`（`(void)block; return var_table_find(table, name);`）、`IrSsa.c:276-278`（`(void)arena;`）
- 类别：契约破坏
- 触发条件：任何按「块内版本」语义调用 `KrtIrVarFindVersion` 的代码。
- 后果：函数签名宣称支持按块查找版本，实现只按名字查全局最新版本 → 调用方拿到跨块错误版本（静默）。`KrtIrSsaCollectVars` 要求 `arena` 却完全不用，属未文档化的契约冗余，且 `:316` `return list.names;` 把 `KRT_CALLOC` 的内存在 `IrSsa.c:751-753` 用 `KRT_FREE` 释放，与 arena 语义混用（当前配对正确，但两套分配器混用无注释）。
- 最小修复：实现块级查找（沿支配链回溯）或删除 `block` 参数；删除 `KrtIrSsaCollectVars` 的 `arena` 参数并注释内存所有权。

### [中] S37-11｜静默失败 + 注释脱节（`lookup_var` 未定义变量返回 IMM 0）
- 位置：`IrSsa.c:763-790`（`:785-788` `val.type = KRT_IR_VALUE_IMM; val.data.imm = 0;`）
- 类别：静默失败
- 触发条件：SSA 重命名时遇到 `var_table` 里不存在的变量名（未声明变量、或前一步重命名漏登记）。
- 后果：未定义变量被静默替换为常量 0，且 `val.value_type` 未设置（默认 0 = `TOKEN_EOF`）→ 与 `Ir.c:538-558` `KrtIrLoad` 把未定义符号吞成 IMM 0 的既有行为叠加，错误被层层掩盖，最终在代码生成期变成错误数值而非编译错误。
- 最小修复：`KRT_COMPILE_ERROR("SSA rename: undefined variable '%s'", name)`。

### [中] S37-12｜未检查前置条件 + 死代码（`ssa_merge_blocks` 合并后不摘链、不更新 `succ_capacity`，且调试打印错位）
- 位置：`IrSsa.c:1443-1495`（`:1460-1462` `block->succ_count = next_block->succ_count;` 直接写 `block->succs[i]` 但不校验 `block->succ_capacity`、不把 `next_block` 从 `func` 块链表摘除、不更新 `first_inst`/`last_inst`；`:1487-1494` `fprintf(stderr, "[LowerPhis] After: main block inst[%d] opcode=%d\n", ...)`）
- 类别：未检查前置条件 / 死代码
- 触发条件：仅 `KrtIrSsaFullOptimize:1510-1523` 调用，而 `KrtIrSsaFullOptimize` 外部零调用者 → 整段死代码。
- 后果：① 合并后 `next_block` 仍在 `func->entry_block` 链上，其 `preds` 仍指向 `block`，后续遍历会重复处理同一批指令；② `block->succs` 复用 `next_block->succs` 的计数但容量未同步，若 `next_block->succ_count > block->succ_capacity` 则越界写；③ `:1487` 的打印从 `KrtIrSsaLowerPhis`（`:1182-1290`）复制而来，与「合并块」无关，且无条件输出到 stderr（诊断噪音/注释脱节）。
- 最小修复：删除 `ssa_merge_blocks` 与 `KrtIrSsaFullOptimize`（无人调用），或实现完整的块摘链 + 容量检查 + 移除调试打印。

### [中] S37-13｜未检查前置条件（`KrtIrPhiAddOperand` 不校验 `phi->blocks` 非空）
- 位置：`IrSsa.c:157-163`（`:158` 只校验 `!phi || index < 0 || index >= phi->pred_count`，随后 `:160` `phi->blocks[index] = block;`）；对照 `IrSsa.c:138-148`（`KrtIrPhiCreate` 在 arena 分配失败时把 `phi->blocks` 置 NULL 但 `phi->pred_count` 仍 > 0）
- 类别：未检查前置条件
- 触发条件：`KrtIrPhiCreate` 的 `KrtIrArenaAlloc` 返回 NULL（`IrMemory.c:84-86` 的对齐/溢出失败路径）。
- 后果：`phi->blocks[index] = block` 空指针解引用 → SIGSEGV。虽然概率低，但 `KrtIrPhiCreate` 自己用 `if (phi->blocks)` 保护了 memset（`:141-144`），说明作者认为该指针可能为空，而 `KrtIrPhiAddOperand` 未同步该假设 → 契约不一致。
- 最小修复：`if (!phi || !phi->blocks || !phi->values || index < 0 || index >= phi->pred_count) return;`
- 备注：【修订 G1】`KrtIrArenaAlloc` 失败返回 NULL 为活路径，本条触发条件成立、严重度维持中。

### [中] S37-14｜契约破坏（`KrtIrBlockAddPhi` 未置 `phi->next = NULL`）
- 位置：`IrSsa.c:186-204`（`:197-202` 尾插时只改 `phi_list->tail->next = phi`，未清 `phi->next`）
- 类别：契约破坏
- 触发条件：复用一个已在别处链表中的 `KrtIRPhi`（例如从其他块迁移、或 `KrtIrPhiCreate` 后多次 `KrtIrBlockAddPhi` 到不同块）。
- 后果：`phi->next` 残留旧链表指针 → `KrtIrPhiFind`（`:166-184`）遍历 `phi_list->head` 时会越过尾节点读到别的块的 phi，比较 `var_name` 时可能命中错误 phi（静默错误），或走到野指针崩溃。
- 最小修复：`KrtIrBlockAddPhi` 中 `phi->next = NULL;`。

### [中] S37-15｜语义分叉 / 静默失败（`ssa_constant_propagation` 只认 `KRT_IR_VALUE_IMM`，不认 `KRT_IR_VALUE_INTEGER`/`IMM_F`）
- 位置：`IrSsa.c:1404-1441`（`:1412` `inst->operands[0].type == KRT_IR_VALUE_IMM`、`:1413` `double const_val = inst->operands[0].data.imm;`、`:1430-1431` 写回 `type = KRT_IR_VALUE_IMM; data.imm = const_val`）；整数常量产生于 `Ir.c:406-412`（`KrtIrInteger` → `KRT_IR_VALUE_INTEGER` + `KrtUInt128`），浮点常量产生于 `Ir.c:601-608`（`KrtIrImmF` → `KRT_IR_VALUE_IMM_F`）
- 类别：语义分叉 / 静默失败
- 触发条件：X86/Vm 后端对 `int32 x = 5;` 这类整数常量拷贝做传播。
- 后果：① 所有 `KRT_IR_VALUE_INTEGER`（`KrtUInt128`，支持 128 位）与 `KRT_IR_VALUE_IMM_F` 的常量拷贝完全不会被传播，而 `KRT_IR_VALUE_IMM`（double）会被传播 → 同一「常量传播」语义在三种立即数类型上行为不一致（阶段不对齐）。② 更危险的是 `KRT_IR_VALUE_IMM` 在 `Ir.c:1103-1113`/`1127-1139` 里被当作偏移量/尺寸的小整数使用（`offset_val.data.imm = (double)offset`），`ssa_constant_propagation` 会把这种 double 位型的「整数」当成常量向下游传播，把整数值以 double 载体替换 `KRT_IR_VALUE_INTEGER` 操作数 → 与 `KrtIrInteger` 的 `KrtUInt128` 归一化语义分叉，128 位整数会被截断/精度丢失。
- 最小修复：传播前按 `inst->result.value_type` 分派，只对真正的浮点常量传播 `IMM_F`、只对整数常量传播 `INTEGER`，并保留 `value_type`；禁止把 `KRT_IR_VALUE_IMM` 用于整数偏移（在 `KrtIrLoadPtr`/`KrtIrStorePtrSized` 改用 `KrtIrInteger`）。

### [中] S37-16｜静默失败 + 资源生命周期（`KrtIrVarTableDestroy` 是空操作，与 `KrtIrSsaBuilderDestroy` 的 `KRT_FREE` 混用两套分配器）
- 位置：`IrSsa.c:41-43`（`(void)table;`）、`IrSsa.c:234-251`（`KrtIrSsaBuilderDestroy` 中 `KRT_FREE(ssa_builder->current_var_stack); KRT_FREE(ssa_builder);`）
- 类别：静默失败 / 资源生命周期
- 触发条件：任何 `KrtIrSsaBuilderDestroy` 调用（当前零调用者）。
- 后果：`KrtIrVarTable` 及其 buckets 由 arena 分配（`:24-30`），`KrtIrSsaBuilder` 由 `KRT_CALLOC` 分配（`:217`），`current_var_stack` 由 `KRT_MALLOC`/类似分配。`KrtIrVarTableDestroy` 名为销毁实则空操作，实际依赖 arena 整体释放；但 `KrtIrSsaBuilderDestroy` 又对另外两个字段做 `KRT_FREE` —— 同一结构体内三种内存来源、两种销毁策略，无任何注释。若有人误对 `var_table` 调 `KRT_FREE` 即无效（指向 arena 内部），若误对 arena 内存二次 `KRT_FREE` 则堆破坏。
- 最小修复：统一为 arena 分配（`KrtIrSsaBuilder` 也走 arena），删除 `KRT_FREE`；或让 `KrtIrVarTableDestroy` 真正释放并文档化所有权。

### [疑似] S37-X1｜`ssa_constant_propagation` 不跨块传播的保守性
- 位置：`IrSsa.c:1404-1441`
- 类别：疑似 / 需验证
- 触发条件：某 temp 定义在块 A、使用在块 B（不被传播）——这是保守而非错误。
- 后果：需验证是否存在「定义块 A 的 temp 在块 B 被使用，且传播后 `value_type` 与 `data.imm` 载体不匹配」的实际 IR 用例。
- 最小修复：**需验证**：用 `target ir` 导出含跨块 temp 的 IR，对比 X86 与 Kro 后端输出。

### [疑似] S37-X2｜`worklist_queue` 溢出需 CFG 块数 > 4096
- 位置：`IrSsa.c:6`（`KRT_SSA_MAX_BLOCKS`）、`IrSsa.c:683`
- 类别：疑似 / 需验证
- 触发条件：`KRTIrSsaInsertPhisForVar` 的 `worklist_queue` 溢出需 CFG 块数 > 4096。
- 后果：需验证 `KRT_SSA_MAX_BLOCKS` 是否在其他地方被当作硬上限并提前拒绝。
- 最小修复：**需验证**：`grep -rn "KRT_SSA_MAX_BLOCKS"`。

### [疑似] S37-X3｜`KrtIrSsaCollectVars` 中 `operands[j]` 是否可能为 NULL
- 位置：`IrSsa.c:253-274`（`add_var_name`）、`Ir.c:343-348`（`ir_create_inst`）、`Ir.c:1364-1368`（`KrtIrVarTypeTruncate`，前批 S34-8）
- 类别：疑似 / 需验证
- 触发条件：`inst->operands[j]` 在 `operand_count > 0` 时是否可能为 NULL。
- 后果：`ir_create_inst:343-348` 保证 `operands` 非空，但 `KrtIrVarTypeTruncate` 可能把 `operand_count` 改小而不重分配，方向是安全的。
- 最小修复：**需验证**：是否存在把 `operand_count` 增大而不重分配 `operands` 的路径。

### [高（死代码内潜在）] S38-1｜`rename_block` 无访问标记递归 + 被 iterative 的栈溢出 fallback 调用 → 栈溢出
- 位置：`Re.KrtC/src/Compiler/Middle/Ir/IrSsa.c:792-864`（递归点 :861-863）；fallback 调用点 `:1036-1038`
- 类别：未检查前置条件 / 死代码（两份实现）
- 触发条件：`rename_block_iterative` 的 `stack_top` 达到 `KRT_RENAME_STACK_MAX`（:791 = 4096）时，:1038 调用 `rename_block`。任何 CFG 嵌套深度 > 4096 的函数（编译器生成的 IR 块数通常远超此数）即触发。
- 后果：`rename_block` 无 `visited` 数组，对含环 CFG（循环）无限递归 → SIGSEGV；即使无环，剩余深度也会再次溢出。
- 最小修复：删除 `rename_block`（:792-864），把 iterative 的 fallback 改为 `break` 或返回错误码；若要保留递归版，必须加 `visited` 参数。
- 备注：【修订 G3】原文判「致命」；`KrtIrSsaOptimize`（:1497-1508）只调用 `ssa_constant_propagation`/`ssa_copy_propagation`/`ssa_eliminate_dead_code`，从不调用 `KrtIrSsaConstruct`/`ReplaceVarUses`/`RemoveStoreInsts`/`LowerPhis`，本条位于零调用者死代码内，严重度降为高（死代码内潜在）。

### [高（死代码内潜在）] S38-2｜`rename_block_iterative` 的 `visited` 只标当前块，不标 phi 前驱回填 → phi 值静默丢失
- 位置：`IrSsa.c:876-885`（visited 数组）、`:1029-1041`（前驱回填）
- 类别：静默失败 / 契约破坏
- 触发条件：CFG 有环时，`visited[succ_idx]` 为 true 则 :1032-1034 `continue` 跳过该 succ 的 phi 前驱回填。
- 后果：回边前驱的 phi 操作数永远不被写入（保持 `KRT_IR_VALUE_VOID`），`KrtIrSsaLowerPhis:1206-1208` 因 `val->type == KRT_IR_VALUE_VOID` 直接 `continue` → 该前驱的 COPY 不生成 → phi 在该路径上无定义值。SSA 构造结果错误。
- 最小修复：phi 前驱回填必须在块首次访问时完成（标准 SSA 算法），或改为两趟：先全量回填 phi 值，再做重命名。
- 备注：【修订 G3】原文判「致命」；位于零调用者死代码内，严重度降为高（死代码内潜在）。

### [中（死代码内潜在）] S38-3｜`rename_block`/`rename_block_iterative` 只维护 `insts[]` 数组，不维护 `first_inst`/`last_inst` 链 → 后端走链表看不到重命名
- 位置：`IrSsa.c:819-852`（数组遍历）、`:929-960`（数组遍历）
- 类别：阶段不对齐 / 契约破坏
- 触发条件：所有 SSA 重命名路径。
- 后果：`KrtIRBasicBlock` 双表示（`insts[]` + 链表）。重命名只改数组元素，链表里的 `inst` 指针虽相同（指向同一 `KrtIRInst`），但 `inst->result.data.name` 被改成版本名后，`KrtIrSsaLowerPhis:1247` 读 `phi->inst->result` 时可能读到已被 `KrtIrSsaReplaceVarUses` 改回的基名。后端（`VmCodegen.c:228`、`X86RegAlloc.c:17`）走链表，若链表顺序与数组不一致则结果不确定。
- 最小修复：统一表示，或文档化「链表与数组必须同步」，并在 `KrtIrSsaConstruct` 末尾断言两者一致。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

### [中（死代码内潜在）] S38-4｜`rename_block` 的 phi 实现是伪造 `KrtIRInst`，从不加入块指令 → phi 定值无后端可见性
- 位置：`IrSsa.c:803-817`
- 类别：契约破坏 / 静默失败
- 触发条件：所有 phi 节点。
- 后果：`phi_inst` 是 arena 分配的 NOP 指令，`opcode = KRT_IR_NOP`，不加入 `block->insts[]` 也不加入链表。`KrtIrSsaLowerPhis:1247` 只读其 `result`，但 `KrtIrSsaReplaceVarUses:1124-1141` 会把 `result.data.name` 的版本后缀剥掉 → COPY 的 result 名与 operands 名相同 → 版本合并，SSA 破坏。
- 最小修复：phi 的 result 名不应被 `KrtIrSsaReplaceVarUses` 剥离（或该函数只处理 operands 不处理 result）。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

### [中（死代码内潜在）] S38-5｜`KrtIrSsaLowerPhis` 的调试 `fprintf` 残留在生产代码，且只对 `main` 生效 → 配置漂移
- 位置：`IrSsa.c:1187-1193`
- 类别：死代码 / 配置漂移 / 注释脱节
- 触发条件：编译 `main` 函数时。
- 后果：每次编译 `main` 都向 stderr 打印 `[LowerPhis] Before: ...`，污染诊断输出；非 main 函数不打印，行为分叉。
- 最小修复：删除该 `fprintf` 块。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

### [中（死代码内潜在）] S38-6｜`KrtIrSsaLowerPhis` 只改链表不保证数组一致（与 `KrtIrSsaRemoveStoreInsts` 同类）
- 位置：`IrSsa.c:1249-1285`
- 类别：阶段不对齐
- 触发条件：phi lowering 插入 COPY 时。
- 后果：:1249-1257 把 `move_inst` 插入 `pred->insts[]` 数组，:1258-1285 又把 `move_inst` 插入链表。但 `KrtIrSsaRemoveStoreInsts:1155-1180` 只压缩数组不动链表 → 若之前调用过 RemoveStoreInsts，数组与链表已经错位，此处插入后错位加剧。后端走链表，结果不确定。
- 最小修复：`KrtIrSsaRemoveStoreInsts` 同步维护链表，或废弃数组表示。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。本批「疑似但不确定」第 1 项（调用顺序）已由第三十八批重要修正解答：`KrtIrSsaOptimize` 从不调用本条涉及的任何函数。

### [中（死代码内潜在）] S38-7｜`KrtIrSsaReplaceVarUses` 用 `strchr(name, '_')` 找第一个下划线 → 基名含下划线时语义破坏
- 位置：`IrSsa.c:1083-1087`、`:1124-1128`
- 类别：静默失败 / 语义分叉
- 触发条件：变量名含下划线（如 `my_var`），SSA 重命名后变为 `my_var_3`。
- 后果：`strchr` 找到第一个 `_`（`my_var_3` 中的 `my` 后），`base_len = 2`，`strncpy` 得到 `"my"`，`new_val.data.name = "my"` → 变量名被截断为 `my`，与原变量 `my_var` 不符。所有含下划线的变量在 SSA 后名字错误，可能与其他变量冲突。
- 最小修复：版本名格式改为不可能出现在标识符中的分隔符（如 `\x01`），或从末尾解析版本号。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

### [中（死代码内潜在）] S38-8｜`KrtIrSsaConstruct` 不校验 `func->entry_block` 非 NULL → `actual_count` 可能为 0，静默返回
- 位置：`IrSsa.c:1048-1072`（:1065-1066 `KRT_CALLOC(actual_count, ...)`，:1071 调用 iterative）
- 类别：未检查前置条件 / 静默失败
- 触发条件：`func->entry_block == NULL`（如空函数体或构造失败）。
- 后果：`collect_blocks` 返回 0，`actual_count = 0`，`KRT_CALLOC(0, sizeof(bool))` 行为未定义；`rename_block_iterative` 里 `stack[0].block = NULL` → 空操作返回，SSA 构造静默失败。
- 最小修复：`KrtIrSsaConstruct` 入口断言 `func->entry_block != NULL`，否则返回错误码。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。【修订 G1】`KRT_CALLOC(0, ...)` 在 `size==0` 时返回 NULL 而非 exit，静默失败路径成立。本批「疑似但不确定」第 3 项（`collect_blocks` 对 `entry_block == NULL` 是否返回 0）与本条触发条件相关，仍需读 `IrSsa.c:230-260` 确认。

### [中（死代码内潜在）] S38-9｜`rename_block_iterative` 的 `visited` 数组用 `actual_count` 分配，但 `block_to_index` 用 `max_blocks + 1024` 分配 → 索引不一致
- 位置：`IrSsa.c:1060-1063`（`block_to_index` 分配 `max_blocks + 1024`）、`:876`（`visited` 分配 `actual_count`）
- 类别：未检查前置条件 / 契约破坏
- 触发条件：`collect_blocks` 返回的 `actual_count` 与 `count_blocks` 的 `max_blocks` 不一致（如 CFG 有不可达块）。
- 后果：`block_to_index` 的索引范围与 `visited` 数组大小不匹配，:1032 `visited[succ_idx]` 可能越界读/写。
- 最小修复：统一用 `actual_count` 分配所有数组，或在 `collect_blocks` 后断言 `actual_count <= max_blocks`。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

### [中（死代码内潜在）] S38-10｜`rename_block_iterative` 的 `stack` 定长 4096，溢出时 fallback 到无 visited 的递归（见 S38-1）
- 位置：`IrSsa.c:791`（`KRT_RENAME_STACK_MAX 4096`）、`:878-885`（定长栈）、`:1036-1038`（fallback）
- 类别：未检查前置条件
- 触发条件：CFG 深度 > 4096。
- 后果：见 S38-1（栈溢出 / 无限递归）。
- 最小修复：fallback 改为返回错误码，或把栈改为动态分配。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。

### [中（死代码内潜在）] S38-11｜`KrtIrSsaLowerPhis` 扩容 `pred->insts` 用 `KRT_REALLOC`，但 `move_inst` 用 `KrtIrArenaAlloc` → 混合分配器 + 失败静默
- 位置：`IrSsa.c:1219-1227`（KRT_REALLOC）、`:1229-1247`（KrtIrArenaAlloc）
- 类别：资源与生命周期 / 静默失败
- 触发条件：`KrtIrArenaAlloc` 返回 NULL（arena 耗尽）。
- 后果：:1234-1236 `if (!move_inst) continue;` / :1240-1242 `if (!move_inst->operands) continue;` 静默跳过该 phi 操作数 → phi 在该前驱无 COPY → 该路径 phi 无定义值。SSA 破坏。
- 最小修复：arena 分配失败应返回错误码（`KrtIrSsaLowerPhis` 当前返回 void，需改为 int）。
- 备注：【修订 G3】原文判「高」；位于零调用者死代码内，严重度降为中（死代码内潜在）。【修订 G1】arena 分配失败返回 NULL 为活路径且零诊断，`if (!move_inst) continue;` 属静默失败，结论成立。本批「疑似但不确定」第 2 项撤回：`KrtIRInst` 无 `prev` 字段（`Ir.h:281-289`），`prev` 疑似项不成立。

### [中（死代码内潜在）] S38-12｜`rename_block` 与 `rename_block_iterative` 的 phi/ALLOC/STORE/COPY 处理代码几乎逐行重复
- 位置：`IrSsa.c:803-836` vs `:897-960`
- 类别：语义分叉
- 触发条件：任何维护时。
- 后果：两份实现已出现差异（iterative 多了 COPY 处理 :922-928，recursive 没有）→ 结果不一致。
- 最小修复：删除 `rename_block`，只保留 iterative 一份。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

### [中（死代码内潜在）] S38-13｜`rename_block` 的 STORE 处理（:828-836）与通用操作数循环（:849-852）逻辑分散在三处
- 位置：`IrSsa.c:828-836`（STORE 特判）、`:849-852`（通用循环跳过 j==0）
- 类别：语义分叉
- 触发条件：STORE/ALLOC 指令。
- 后果：改写规则分散，易漏改。
- 最小修复：合并到唯一改写点。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

### [中（死代码内潜在）] S38-14｜`KrtIrSsaRemoveStoreInsts` 只压缩数组不动链表（同 S38-6）
- 位置：`IrSsa.c:1155-1180`
- 类别：阶段不对齐
- 触发条件：调用 RemoveStoreInsts 后。
- 后果：链表仍含 STORE，后端仍执行 STORE → SSA lowering 后 STORE 未被移除。
- 最小修复：同步维护链表。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

### [中（死代码内潜在）] S38-15｜`KrtIrSsaReplaceVarUses` 的 `is_version` 检查只验证后缀全为数字，不验证版本号范围
- 位置：`IrSsa.c:1090-1105`
- 类别：静默失败
- 触发条件：变量名形如 `foo_999999999999999999999`（超长数字）。
- 后果：`strncpy` 后 `base_name` 正确，但版本号溢出（如果后续有解析）。当前只改名不解析，影响有限。
- 最小修复：限制版本号长度。
- 备注：【修订 G3】原文判「中」；位于零调用者死代码内，严重度维持中（死代码内潜在）。

### [高] S39-1｜`fold_binary_op` 移位操作未检查移位量 → 未定义行为
- 位置：`IrOptimizer.c:129-130`
- 类别：未检查前置条件
- 触发条件：`KRT_IR_LSHIFT`/`KRT_IR_RSHIFT` 的 `right` 操作数 >= 64 或为负。
- 后果：`(int64_t)left << (int)right` 在 C 标准中是未定义行为（移位量 >= 类型宽度）。x86 上实际取模 64，结果与源码语义不符。
- 最小修复：`if (right < 0 || right >= 64) return 0;`（或按语言语义报错）。

### [高] S39-2｜`fold_binary_op` 把 `double` 强转 `int64_t` → 精度丢失
- 位置：`IrOptimizer.c:126-131`
- 类别：静默失败 / 阶段不对齐
- 触发条件：对 `KRT_IR_AND/OR/XOR/LSHIFT/RSHIFT` 的 double 操作数进行位运算。
- 后果：double 不能精确表示所有 int64（>2^53 丢失低位），位运算结果错误。
- 最小修复：整数运算应走 `KrtIrFoldInteger`（:184 路径），不应进入 double 折叠。

### [高] S39-3｜`fold_binary_op` 的除零/取模保护是死代码，且与 `fold_constant_binary` 矛盾
- 位置：`IrOptimizer.c:122-125`（`fold_binary_op` 返回 0）、`:168-171`（`fold_constant_binary` 返回 false）
- 类别：死代码 / 语义分叉
- 触发条件：任何除零/模零。
- 后果：`fold_binary_op` 的 `right != 0 ? ... : 0` 永不被执行（调用方已拦截）；若未来有新调用方直接调 `fold_binary_op`，会得到静默的 0 而非错误。
- 最小修复：删除 `fold_binary_op` 中的除零分支，统一由 `fold_constant_binary` 处理。

### [高] S39-4｜整数折叠把结果塞进 `KRT_IR_IMM` opcode → opcode 与 operand type 不匹配
- 位置：`IrOptimizer.c:184-190`
- 类别：阶段不对齐 / 契约破坏
- 触发条件：`KrtIrFoldInteger` 成功时。
- 后果：`inst->opcode = KRT_IR_IMM`（语义为 double 立即数），但 `inst->operands[0] = integer_result` 的 `type` 是 `KRT_IR_VALUE_INTEGER`（`KrtUInt128`）。后端按 `KRT_IR_IMM` 处理时读 `data.imm`（double 字段），而 `KrtUInt128` 在 union 中与 `double imm` 重叠 → 读取错误的位模式。
- 最小修复：整数折叠后 opcode 应为 `KRT_IR_INTEGER`（若存在）或保留原 opcode 只替换 operands。

### [高] S39-5｜`ir_optimize_constant_folding` 覆盖 `operands[0].data.imm` 但不更新 `value_type`
- 位置：`IrOptimizer.c:204`
- 类别：阶段不对齐
- 触发条件：double 常量折叠。
- 后果：`inst->operands[0].value_type` 保留原操作数的 token 类型（如 `TOKEN_INT`），但值已是 double 结果 → 后续阶段按 `value_type` 解释时类型不符。
- 最小修复：同步设置 `value_type`。

### [高] S39-6｜`ir_optimize_constant_propagation` 的 `constants.capacity = 256` 固定，超出静默丢弃
- 位置：`IrOptimizer.c:273`（`capacity = 256`）、`:281`（`if (!found && constants.count < constants.capacity)`）
- 类别：静默失败
- 触发条件：函数内不同 TEMP 常量数 > 256。
- 后果：超出部分不被传播，优化不完整且无任何诊断。
- 最小修复：动态扩容或报错。

### [高] S39-7｜`values_equal` 的 `KRT_IR_VALUE_VAR` 分支 `strcmp` 无 NULL 检查
- 位置：`IrOptimizer.c:50`
- 类别：未检查前置条件
- 触发条件：`a->data.name` 或 `b->data.name` 为 NULL。
- 后果：空指针解引用 → SIGSEGV。
- 最小修复：`return a->data.name && b->data.name && strcmp(...) == 0;`

### [高] S39-8｜`is_pure_operation` 不含浮点操作码 → 浮点表达式 CSE 不合并
- 位置：`IrOptimizer.c:60-82`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：浮点运算（FADD/FSUB/FMUL/FDIV/FLT/FGT/FEQ/FLE/FGE/FNE）。
- 后果：整数表达式被 CSE 合并，浮点表达式不合并 → 同层功能行为不一致。
- 最小修复：把浮点操作码加入 `is_pure_operation`。

### [高] S39-9｜`is_side_effect_free` 的 `default: return true` → 未知 opcode 被误判为无副作用
- 位置：`IrOptimizer.c:84-99`
- 类别：静默失败
- 触发条件：未来新增有副作用的 opcode，或当前已有但未列出的 opcode（如 `KRT_IR_CALL_INDIRECT` 已列出，但 `KRT_IR_ATOMIC_*` 等未列出）。
- 后果：DCE 可能删除有副作用的指令。
- 最小修复：`default: return false;`（保守）。

### [高] S39-10｜`is_constant_value` 只认 `KRT_IR_VALUE_IMM`，漏 `KRT_IR_VALUE_INTEGER`/`IMM_F`
- 位置：`IrOptimizer.c:37-39`
- 类别：阶段不对齐
- 触发条件：128 位整数常量或浮点立即数。
- 后果：这些常量不参与 double 折叠路径（虽然 :184 有整数路径，但 :198 的 double 路径漏掉 `IMM_F`）。
- 最小修复：补充 `KRT_IR_VALUE_IMM_F` 分支。

### [高] S39-11｜`ir_optimize_constant_propagation` 的 `KRT_CALLOC` 失败时跳过该函数（静默）
- 位置：`IrOptimizer.c:265-269`
- 类别：静默失败 / 未检查前置条件（OOM 时静默 NULL 传播，零诊断）
- 触发条件：OOM（`KRT_CALLOC` 返回 NULL）。
- 后果：该函数常量传播被静默跳过，无任何诊断；优化结果不完整且不可观测。
- 最小修复：移除该分支或改为断言/报错；至少补 `KRT_COMPILE_ERROR`。
- 备注：【修订 G1】原文判「中」并称「KRT_CALLOC 失败实际会 exit(1)，此分支不可达（死代码）」——该前提错误：`KrtSafeCalloc`/`KrtSafeMalloc` 失败返回 NULL（size==0 或 malloc 失败均返回 NULL），`KrtMemoryReportError` 是空操作，故此分支是活路径，改标「静默失败 / 未检查前置条件」，严重度由中提升为高。

### [中] S39-12｜`fold_constant_unary` 只处理 `KRT_IR_SUB`，不处理按位取反/逻辑非
- 位置：`IrOptimizer.c:155-163`
- 类别：死代码 / 语义分叉
- 触发条件：`KRT_IR_NOT` 等一元操作。
- 后果：一元常量折叠不完整。
- 最小修复：补充或删除该函数。

### [致命] S40-1｜DCE 的 `KRT_FREE(inst)` 与 arena 分配的 inst 冲突 → 堆破坏（启用 SSA 后触发）
- 位置：`IrOptimizer.c:336-340`（`KRT_FREE(inst->operands); KRT_FREE(inst);`）；对照 `Ir.c:334`（`ir_create_inst` 用 `KRT_CALLOC`）与 `IrSsa.c:1229`（`move_inst` 用 `KrtIrArenaAlloc`）
- 类别：资源与生命周期 / 契约破坏
- 触发条件：`KrtIrSsaLowerPhis` 把 arena 分配的 `move_inst` 加入 `pred->first_inst` 链表（`IrSsa.c:1268-1272`）后，`ir_optimize_dead_code_elimination` 遍历到它并 `KRT_FREE(move_inst)`。当前 SSA 未启用（见 S37-4 死代码事实），故不可达；一旦启用 SSA 即触发。
- 后果：对 arena 内存调用 `free()` → 堆元数据破坏 / SIGSEGV / 未定义行为。
- 最小修复：inst 结构体加 `bool is_arena` 字段，DCE 只释放 `is_arena == false` 的；或统一用 arena 分配 inst。
- 备注：原文把「SSA 未启用」归因于 S37-1（配置漂移）；按 S37-4，根因是 SSA 构造入口零调用者。严重度维持致命（一旦启用即触发）。
