# 第八十三批：SelfHost/Compiler/Backend/Vm/Interpreter.krt（全量）

## 覆盖范围
- 目标文件：`/sdcard/KairoteLang/SelfHost/Compiler/Backend/Vm/Interpreter.krt`，共 1357 行，分 3 段（1-460、461-920、921-1357）用 read 全量读完。
- 内容覆盖：EBC v1/v2-v6 序列化与反序列化（`KrtVmWrite/Read`、`KrtVmEmit*`、`KrtVmReadV2`）、校验（`KrtVmValidateVersion`）、运行时（region/allocation/frame/handler）、执行主循环（op 0-76）、宽整数（`KrtVmWide*`）、legacy v1 解释器、runtime 类型校验。
- 对照：`Re.KrtC/src/Compiler/Backend/Vm/VmCodegen.c` 只实现 legacy EBC v1（`OP_CONSTANT/OP_SET_LOCAL/...`），不覆盖本机 EBC v2-v6 的 op 语义，故 v2-v6 条目无法与该 C 文件逐条对照；凡依赖 Kro/x86 行为推断的均标「疑似」。
- 结论：共 24 条问题（致命 1、高 2、中 8、低 13）。

## 问题条目

### [致命] 83-1｜整数溢出绕过 op 50/51/53/54 的 locals 边界校验，造成负偏移越界读写
- 位置：`Interpreter.krt:91-92`（校验）、`Interpreter.krt:806`（op 52 写）、`Interpreter.krt:810`（op 53/54 读）
- 类别：未检查前置条件 / 整数溢出 / 内存安全
- 触发条件：字节码中 op 50 的 `extra`（或 op 51/53/54 的 `argument`）取接近 `INT64_MAX` 的值（如 `9223372036854775802`），使 `extra + 6` 回绕为负，`extra + 6 > locals` 不成立，校验被绕过。
- 后果：`handler.slot` 被截断为 int32 负值；op 52 按 `(slot+1)*8`、`(slot+2)*8` 向 `frame.locals` 负偏移写 type/value，op 53/54 从负偏移读；`KrtVmWrite/KrtVmRead` 本身无区域检查，形成堆外读写，可崩溃或破坏相邻帧/池内存。
- 最小修复：校验改为 `extra < 0 || extra > locals - 6`（并保证 `locals >= 6`），`argument` 同理；执行端再对 slot 做非负与上界断言。
- 备注：若干脆让 `extra + 6` 触发有符号溢出陷阱，则变成对恶意字节的拒绝服务；两种语义都需显式处理。

### [高] 83-2｜op 33（post.increment）接受 width=16，但宽值 scratch 未分配且实现按指针算术
- 位置：`Interpreter.krt:81`（校验允许）、`Interpreter.krt:732`（执行）、`Interpreter.krt:926-931`（layout）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：op 33 的 `extra=16`（`KrtVmWidth(16)` 为真，校验接受）。
- 后果：v6 下 `KrtVmLoad(width=16)` 调用 `KrtVmWideBox`，而 `KrtVmWideScratchBytes` 对 op 33 返回 0，`layout.offsets[index] = -1`，`KrtVmWideBox` 因 `wide_cursor < 0` 置 `state.failed`，合法指令必报 E_VM；即使在非 inline 的外部 v2-5 字节码下，`value + argument` 也是把 box 指针当整数加，结果错误。
- 最小修复：校验层禁止 op 33 使用 width=16（宽值自增应只走 op 67），或为 op 33 width=16 正确实现宽值加并分配 16 字节 scratch。
- 备注：当前 Ir 未发射 op 33 width=16（宽值走 op 67），故为潜在/外部字节码触发；但校验器与执行器对同一编码的接受性不一致本身是硬缺陷。

### [高] 83-3｜op 26 在操作数栈下溢时仍执行 syscall
- 位置：`Interpreter.krt:721-725`、`Interpreter.krt:492-497`
- 类别：未检查前置条件 / 错误处理不一致
- 触发条件：op 26 的 `extra > 0` 而当前帧可用栈值不足；`KrtVmPop` 置 `state.failed` 并返回 0。
- 后果：`arguments` 数组部分或全部为零，仍调用 `KrtVmSystemCall` 执行 mmap/munmap/open/read/write 等系统调用，产生真实副作用后才在循环顶部因 `state.failed` 退出。
- 最小修复：在填充 `arguments` 前检查 `state.depth - frame.stack_base >= extra`，不足则置 `state.failed` 且不调用 syscall。
- 备注：`KrtKroNativeStackDepths` 静态分析通常能拦截，但运行时无兜底，且静态分析一旦与运行时栈基口径不同即被穿透。

### [中] 83-4｜KrtVmAllocate 失败静默返回 0，未置 state.failed
- 位置：`Interpreter.krt:509-524`；调用点 `Interpreter.krt:719`（op 24）、`Interpreter.krt:729`（op 30）、`Interpreter.krt:833-836`（op 66）
- 类别：静默失败 / 诊断缺失
- 触发条件：`count < 0`、`stride < 0`、乘法/加法溢出或 `syscall(9)` 返回负。
- 后果：op 24/30/66 把 0 压栈后继续执行，直到后续内存访问才报 E_VM，报错位置与根因偏离；负数 count 本应是运行期错误却被当作空指针。
- 最小修复：失败时置 `state.failed = true` 并返回 0，或在压栈前检查返回值。
- 备注：需与 Kro 后端对分配失败的处理对齐（疑似不一致）。

### [中] 83-5｜栈/宽布局分配失败静默返回 1 且泄漏 wide_layout
- 位置：`Interpreter.krt:663-667`
- 类别：资源与生命周期 / 诊断缺失
- 触发条件：`KrtDriverAlloc(2097152)` 失败，或 `KrtVmMakeWideLayout` 因单函数 scratch 超过 64MiB 返回 null。
- 后果：两条路径都只 `return 1`，无 `KrtDriverError`；`state.stack == 0` 分支直接 `delete state`，若 wide_layout 已创建则泄漏。
- 最小修复：两处失败均调用 `KrtDriverError("E_VM", ...)`；`stack == 0` 分支先释放 `state.wide_layout`。
- 备注：`KrtDriverAlloc` 自身失败不诊断，调用方必须补。

### [中] 83-6｜globals 分配失败静默返回 1
- 位置：`Interpreter.krt:673-676`
- 类别：诊断缺失 / 静默失败
- 触发条件：`KrtDriverAlloc((int64)module.global_count * 8) == 0`。
- 后果：直接 `KrtVmDispose(state); return 1;`，无任何 E_VM 诊断，调用方无法区分是模块错误还是内存不足。
- 最小修复：失败前调用 `KrtDriverError("E_VM", ...)`。
- 备注：与 83-5 同类但位置独立。

### [中] 83-7｜用 syscall munmap 释放 KrtDriverAlloc（bump pool）内存，页对齐会拆掉共享池
- 位置：`Interpreter.krt:618-619`（stack/globals）、`Interpreter.krt:914`（runtime_pointer）；分配点 `Interpreter.krt:660`、`Interpreter.krt:674`、`Interpreter.krt:1345`
- 类别：资源与生命周期 / 契约破坏
- 触发条件：VM 执行结束或失败清理时。
- 后果：`KrtDriverAlloc` 以 1MiB 起、页对齐的 mmap 作为 bump pool（`PlatformAbstraction.krt:23-30`）；`munmap(state.globals, global_count*8)` 或 `munmap(runtime_pointer, runtime_length)` 会按页拆掉同一池中相邻的编译器对象，后续分配/使用可能崩溃。
- 最小修复：VM 自有内存统一走 `KrtVmAllocate`（直接 `syscall(9)`）或提供配套释放；不要对 pool 指针直接 munmap。
- 备注：疑似，取决于驱动在 VM 运行后是否继续使用该池；2MiB 的 stack 通常独占映射，风险较低，globals/runtime_pointer 风险高。

### [中] 83-8｜反序列化后的 module 未设置 runtime_bytes/data_storage，module 不自洽
- 位置：`Interpreter.krt:190-221`（ReadV2）、`Interpreter.krt:1307`（KrtVmRuntimeValidate）、`Interpreter.krt:1322`（Emit）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：`KrtVmReadV2` 从产物加载带 runtime 的模块。
- 后果：`module.runtime_bytes` 保持 null，`module.data_storage` 也未赋值；`KrtVmRuntimeValidate(module)` 会对已加载模块返回 false，任何依赖这两个字段的后续阶段（再发射、跨后端交换）拿到空数据。
- 最小修复：ReadV2 成功后令 `runtime_bytes` 指向 `runtime_pointer`（或复制），`data_storage` 指向 `module.data`。
- 备注：当前执行路径不读 `runtime_bytes`，故未立即暴露，属契约层面的阶段不对齐。

### [低] 83-9｜反序列化失败路径泄漏 module 数组
- 位置：`Interpreter.krt:200-221`、`Interpreter.krt:1333-1346`
- 类别：资源与生命周期
- 触发条件：ReadV2/RuntimeRead 分配数组后，因范围校验或 `KrtDriverAlloc` 失败返回 false。
- 后果：`ops/args/extra/starts/local_counts/parameter_counts/...` 与 `runtime_offsets/runtime_relocations` 未释放。
- 最小修复：失败返回前 `delete` 已分配数组与 relocation 链表。
- 备注：进程生命周期短，影响低。

### [低] 83-10｜KrtVmRun 全路径不回收 artifact.data
- 位置：`Interpreter.krt:902-916`
- 类别：资源与生命周期
- 触发条件：每次 `KrtVmRun`（v1 与 v2-6 路径）。
- 后果：整个字节码缓冲（最大 64MiB）来自 `KrtDriverAlloc` bump pool，运行后不回收，重复运行会持续增长。
- 最小修复：执行结束后释放 artifact.data（注意 v2 的 `module.data` 指向其内部，需先执行完）。
- 备注：若 `krt run` 为一次性进程则影响有限。

### [低] 83-11｜KrtVmReadArtifact 失败路径泄漏 data
- 位置：`Interpreter.krt:887-896`
- 类别：资源与生命周期
- 触发条件：读中断（`count <= 0`）或尾读非 0。
- 后果：已分配的 `data` 未释放即返回 false。
- 最小修复：失败前释放 `data`。
- 备注：—

### [低] 83-12｜op 69 WideRef 为死代码且 scratch 计算漏项
- 位置：`Interpreter.krt:100`（v6 拒绝）、`Interpreter.krt:158`（emit 提升到 6）、`Interpreter.krt:844-848`（执行）、`Interpreter.krt:926-931`（layout）
- 类别：死代码 / 阶段不对齐
- 触发条件：Ir 中 `KrtNativeOp.WideRef` 无任何发射点；emit 遇到 op 69 会先提升到版本 6，再被校验拒绝。
- 后果：VM/Kro 保留不可达分支；若外部 v2-5 字节码含 op 69 且 `pointer == 0`，执行期 `KrtVmWideBox` 需 16 字节 scratch，而 `KrtVmWideScratchBytes` 未为其预留（仅靠非 inline 走 heap 才侥幸工作），属布局约定缺口。
- 最小修复：删除 op 69 相关分支，或在校验层明确拒绝并同步移除 Kro 侧死分支。
- 备注：—

### [中] 83-13｜op 60-68 缺 version>=6 前置校验，宽操作可在非 inline ABI 下执行
- 位置：`Interpreter.krt:93-102`、`Interpreter.krt:154-160`
- 类别：阶段不对齐 / 契约破坏
- 触发条件：外部/手工 version 2-5 字节码含 op 60-63、66-68。
- 后果：校验接受这些宽操作，执行时 `wide_inline=false`，宽值改走堆 box，与 v6 的「持久 inline 存储」ABI 不同；同一字节码在 v5 与 v6 下内存布局/语义契约不一致。
- 最小修复：对 op 60-69 统一增加 `version >= 6` 校验（op 69 维持现有更严规则）。
- 备注：emit 总会把含这些 op 的模块提升到 6，故仅外部字节码触发。

### [低] 83-14｜op 61/62 的 extra（无符号标志）未校验
- 位置：`Interpreter.krt:820-823`、`Interpreter.krt:94-95`
- 类别：未检查前置条件 / 语义分叉
- 触发条件：`extra` 为任意非 0（如 2、-1）。
- 后果：VM 一律按 unsigned 解释；若 Kro 侧按 `extra == 1` 判定，则同一字节码两后端结果不同。
- 最小修复：校验 `extra ∈ {0,1}`。
- 备注：疑似，需与 Kro 的 `arity != 0` 判定点对齐。

### [中] 83-15｜宽整数移位掩码与 Kro/x86 可能不同
- 位置：`Interpreter.krt:976-977`
- 类别：语义分叉
- 触发条件：移位量在 64..127 之间。
- 后果：VM 用 `((int64)b & 127)`；x86 64 位移位指令硬件把计数掩码为 63，Kro 若直接发射 `shl/shr/shrd` 序列，计数 64..127 的结果与 VM 不同。
- 最小修复：确认 Kro 的 128 位移位实现（是否显式掩码 127），统一两端语义。
- 备注：疑似，需反汇编 Kro 输出或查 `KrtKroNativeWideBinary` 的移位序列确认。

### [中] 83-16｜宽整数除法/取模未处理 INT128_MIN / -1 溢出
- 位置：`Interpreter.krt:978-982`
- 类别：未检查前置条件 / 语义分叉
- 触发条件：`a = INT128_MIN`、`b = -1`（操作码 47 或 37）。
- 后果：VM 走软件除法，结果回绕或未定义；Kro/x86 `idiv` 会触发 #DE 异常，两后端行为不同。
- 最小修复：除法前检查 `a == INT128_MIN && b == -1`，按语言语义报错或回绕。
- 备注：疑似；b == 0 已检查，仅溢出未检查。

### [低] 83-17｜负 double → uint64 转换语义未定义/可能分叉
- 位置：`Interpreter.krt:329-338`（`KrtVmFloatConvert` 的 `target == -8` 分支）
- 类别：语义分叉
- 触发条件：op 38 `extra == -8` 且源值为负或超出 uint64 范围。
- 后果：Krt 软件转换与 x86 `cvttsd2si` 的 INT_MIN 饱和行为可能不同，VM 与 Kro 结果分叉。
- 最小修复：明确并统一越界/负数转换语义。
- 备注：疑似。

### [低] 83-18｜op 21 Address 的 extra 步长与 pointer+offset 溢出未校验
- 位置：`Interpreter.krt:713-716`
- 类别：未检查前置条件
- 触发条件：`extra` 任意（含 0、负、非 2 次幂），或 `pointer + offset` 溢出。
- 后果：地址回绕；与 Kro 的地址运算语义可能不同，且校验层对 op 21 无任何 argument/extra 约束。
- 最小修复：校验 `extra` 非负（最好为 2 次幂），并对加法做溢出检查。
- 备注：疑似。

### [低] 83-19｜KrtVmEmitV1 未检查 entry 的聚合返回隐藏参数
- 位置：`Interpreter.krt:113-116`
- 类别：契约破坏
- 触发条件：entry 的 `parameter_counts == 0` 但 `aggregate_return_sizes > 0`。
- 后果：v1 产物按 0 参数入口序列化，而模块的 `KrtVmPhysicalParameters(entry) == 1`；v1 运行期与模块契约不一致。
- 最小修复：把入口检查改为 `KrtVmPhysicalParameters(module, module.entry) == 0`，并显式拒绝 `aggregate_return_sizes[entry] > 0`。
- 备注：—

### [低] 83-20｜校验函数内调用 KrtDriverError，诊断重复且顺序依赖
- 位置：`Interpreter.krt:67`、`Interpreter.krt:76`、`Interpreter.krt:81`、`Interpreter.krt:140`
- 类别：错误处理不一致
- 触发条件：`KrtVmValidateVersion` 返回 false 的任意路径。
- 后果：函数内部先报一条具体错误，调用方再报 "invalid or unsupported VM module"，诊断重复且第一条细节可能被覆盖；纯校验函数带副作用，不利于复用与测试。
- 最小修复：校验函数只返回 bool，统一由调用方按上下文诊断。
- 备注：—

### [低] 83-21｜KrtVmEnter 的 locals_size == 0 分支不可达
- 位置：`Interpreter.krt:586`
- 类别：死代码
- 触发条件：`locals_size = scalar_bytes + aggregate_size + wide_bytes + 30`，最小为 30。
- 后果：`if (frame.locals_size == 0) { frame.locals_size = 8; }` 永不成立，误导读者以为存在零大小帧。
- 最小修复：删除该分支或改为断言。
- 备注：—

### [低] 83-22｜KrtVmNumberText 内部分配未释放
- 位置：`Interpreter.krt:254`、`Interpreter.krt:282`（`digits`/`text`；`KrtDriverNumber` 返回的 `significant`/`exponent_text` 亦为池内分配）
- 类别：资源与生命周期
- 触发条件：legacy v1 打印路径每次调用。
- 后果：中间缓冲与结果串不回收，bump pool 持续增长。
- 最小修复：返回前释放中间缓冲（若池支持回收）。
- 备注：仅 v1 路径，进程短生命周期。

### [低] 83-23｜op 52 异常展开未校验 handler.frame 仍是当前帧祖先
- 位置：`Interpreter.krt:802-807`
- 类别：未检查前置条件
- 触发条件：handler 链被破坏（如未来引入跨帧注册或 op 51 误配）。
- 后果：`while (state.frame != handler.frame)` 可能走到 `state.frame == null`，随后 `state.frame.pc` 空指针解引用。
- 最小修复：循环内加 `if (state.frame == null) { state.failed = true; break; }`。
- 备注：当前 handler/frame 不变量下不可达，属纵深防御。

### [低] 83-24｜代码/数据段 region 标记为可写
- 位置：`Interpreter.krt:668`（对比 `Interpreter.krt:669-672` 的 runtime immutable）
- 类别：契约破坏
- 触发条件：字节码用 op 23 向 op 25 取得的 data 地址写入。
- 后果：常量/数据段可自修改，与 runtime 段显式 `immutable` 的约定不一致；Kro 侧通常区分 .rodata/.data。
- 最小修复：按段区分可写性，至少把只读常量区标记 immutable。
- 备注：疑似设计意图，需与后端 ABI 确认。
