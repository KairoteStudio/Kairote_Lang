# 第七十四批：SelfHost/Compiler/Backend/Kro/KroBackend.krt（1-1735）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Backend/Kro/KroBackend.krt` 第 1-1735 行（分段读取，未整文件通读）。
- 交叉核对仅限 grep 特定符号：`SelfHost/Compiler/Middle/Ir/Ir.krt` 的 `enum KrtNativeOp`（1554-1568 行）、本文件内 `strtab_size`/`string_bytes`/`*_reloc_count` 的赋值点。未在 `/sdcard/KairoteLang/Re.KrtC` 下检索到 `KroCodegen/Kro*.inc`（grep 无命中），故涉及 C 版对照的条目一律标注「疑似」。
- 本批覆盖：浮点编码（1-117）、全局符号/ABI 清单（119-352）、异常帧（354-379）、整数规范化（381-396）、KrtKroObject 与文本写出（398-836）、IR 快速发射器（838-937）、原生栈 IR 后端（939-1557）、写出辅助与字符串运行时（1559-1631）、聚合/宽浮点位转换（1633-1735）。

## 问题条目

### [高] 74-1｜Compare 未知操作码静默回退为 setne
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1314`-`1319`
- 类别：未检查前置条件；语义分叉；诊断缺失
- 触发条件：`KrtNativeOp.Compare` 且 `module.args[index]` 不属于 `{61,101,60,62,102,103}`。`int32 cc = 149` 为默认值，六个 if/else 均不命中时既不报错也不返回 false。
- 后果：发射 `0x0f 0x95`（setne）作为比较结果，生成静默错误的机器码；有符号/无符号分支都受影响。
- 最小修复：default 分支改为 `return false`，或先校验 `args[index]` 白名单再查表。
- 备注：同文件 `KrtKroNativeFloatCompare`（77-78 行）对未知 condition 明确 `return false`，同一后端内两套比较的处理口径不一致。

### [高] 74-2｜KrtKroGlobalSymbols 写 slot_symbols/data 前不校验下标与尺寸
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:256`-`267`
- 类别：未检查前置条件（数组下标越界）
- 触发条件：`entry.declaration.native_global-1 >= module.global_count`（258 行）、`entry.guard_slot >= module.global_count`（261、267 行）、`slot*8 + entry.storage_size > module.global_count*8`（265-266 行）。这些字段均来自 AST/模块元数据，本函数未做任何边界校验即直接索引写入。
- 后果：`slot_symbols[...]` 与 `data[offset+part]` 越界写，破坏相邻堆对象或符号表；越界写不触发任何诊断。
- 最小修复：在进入符号循环前对每个 entry 统一校验 `native_global-1`、`guard_slot`、`offset+storage_size` 落在 `[0, global_count*8)`。
- 备注：同函数 287 行对重定位 slot 做了 `slot<0||slot>=module.global_count` 校验，说明作者意识到该不变量，但 258/261/265 的写入点漏检，前后不一致。

### [高] 74-3｜KrtKroWriteSymbols 用 total_reloc_count 读取只按 text 分配的缓冲
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:790`-`793`
- 类别：契约破坏；内存越界读；资源与生命周期
- 触发条件：`output.total_reloc_count = text_reloc_count + rodata_reloc_count + data_reloc_count`（292 行），而 `text_relocation_bytes` 在 282-284 行只按 `(old_relocs + global_reloc_count)*16+1` 分配；只要 `rodata_reloc_count` 或 `data_reloc_count` 非零即越界。
- 后果：`KrtKroWriteBytes(relocations, total_reloc_count*16, ...)` 越过数组尾部读取，把堆上无关字节写入目标文件（重定位表错乱 + 潜在信息泄露）。
- 最小修复：按节分别写入（text/rodata/data 各自计数与缓冲），或让 `total_reloc_count` 只统计确实连续存放于 `text_relocation_bytes` 的条目。
- 备注：疑似（取决于是否存在 rodata/data 重定位非零的组合路径）；本切片内 292 行的相加公式与 793 行的单缓冲读取在字面上即不自洽。

### [中] 74-4｜KrtKroGlobalSymbols 失败路径留下半构建的 output
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:238`-`244`（先替换 `symbol_bytes`/`string_bytes`/`data_bytes`），失败点 `249`、`287`、`289`
- 类别：错误处理不一致；资源与生命周期
- 触发条件：`initializer_index` 非法（249）、重定位 slot 无对应符号（287）、`KrtKroLibraryWord`/重定位写入失败（288-289）。
- 后果：return false 时 `output` 已被改为新数组尺寸与部分符号内容，旧数组已 `delete`；调用方若复用或仍执行写盘，会得到结构不合法的对象，且中途分配未回收。
- 最小修复：先在临时变量中完成全部校验与构建，全部成功后再一次性提交到 `output`；或在每个失败分支回滚并释放新数组。
- 备注：与 74-15（module 状态污染）同类但位置与对象不同，独立记录。

### [中] 74-5｜KrtKroAbiFinishNamed 追加符号名时无边界检查
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:347`-`348`
- 类别：未检查前置条件（数组下标）；契约破坏
- 触发条件：`name + strlen(marker) + 1 > output.string_bytes.length`。`string_bytes` 在 `KrtKroGlobalSymbols`（238 行）只按全局名总长 `names+1` 分配，未给 ABI 清单名预留；函数内也无扩容。
- 后果：`output.string_bytes[name]=text[index]` 越界写；同时本函数自身不更新 `output.strtab_size`（依赖 2037 行调用方补写），函数级不变量不完整。
- 最小修复：写入前校验剩余容量并按需扩容，同时在本函数内同步 `strtab_size`（或明确注释由调用方负责）。
- 备注：疑似——未允许读取 1735 行以后的完整调用链，无法确认调用方是否另行扩容；`marker` 以 `string` 传入后强转 `byte*` 遍历（347 行）本身也依赖未声明的字符串内存布局。

### [中] 74-6｜KrtKroObjectIsValid 过弱，空函数产物通过校验却无法写出
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:470`-`474`；空函数路径 `858`-`866`
- 类别：契约破坏；静默失败；阶段不对齐
- 触发条件：`ir_function.instruction_count == 0` 时 `symbol_count=0`、`strtab_size=6`（857-865 行），`KrtKroObjectIsValid` 判定通过（只查 `symbol_count>=0`、`strtab_size>=0`），但 `KrtKroWriteSymbols`（796-797 行）要求 symbol_count 为 1 或 2 且 strtab_size 为 6/13，必然返回 false。
- 后果：同一对象在「有效性」与「可序列化」两个阶段得到相反结论，错误在写盘阶段才出现且无诊断，属于阶段间契约漂移。
- 最小修复：`KrtKroObjectIsValid` 增加与写出器一致的组合约束，或让写盘器接受 0 符号对象。
- 备注：`IsValid` 还不校验 `data_size>0 && data_bytes==null`、`symbol_count>0 && symbol_bytes==null` 等组合。

### [中] 74-7｜KrtKroNativeGlobalsStart 是忽略 output/pc 的空实现
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:143`-`145`，调用点 `1240`
- 类别：死代码；阶段不对齐；注释与实现脱节
- 触发条件：每次编译入口函数（`function_index == module.entry`）都会在序言中调用它并传播其返回值。
- 后果：函数体只做 `slots` 范围校验，不发出任何字节、不推进 pc；若命名/调用语义期望在此登记 globals 区（.bss 或全局初始化起点），该动作缺失；若确实无需动作，则 1240 行的 `KrtKroNativeCode(...,16724293,3)` 与本次调用构成的「伪步骤」会误导维护者。
- 最小修复：对照 C 版确认此处是否应发射内容；无内容则删除调用并简化 1240 行，有内容则补齐实现与注释。
- 备注：疑似——本仓库未 grep 到 `KroCodegen/Kro*.inc`，无法完成交叉确认。

### [中] 74-8｜异常槽位计算 `arity + 6` / `record + 6` 存在 int32 溢出
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1502`、`1509`、`1514`
- 类别：未检查前置条件（整数溢出）
- 触发条件：`arity`（即 `module.extra[index]`）或 `record`（即 `module.args[index]`）接近 `INT32_MAX`，`arity + 6` 回绕为负数，`< 0` 与 `> local_counts` 两道检查同时失效。
- 后果：`KrtKroNativeTryEnter/TryLeave` 收到巨大 slot，`-8*(slot+6)` 位移回绕，生成访问错误帧地址的代码；异常记录布局被破坏。
- 最小修复：比较前转为 int64，或先判断 `arity > module.local_counts[function_index]` 再算 `arity + 6`。
- 备注：正常前端不会产生该值，但后端对 IR 元数据零容忍要求下应显式拒绝。

### [中] 74-9｜Data 操作码未拒绝负的 size（extra）
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1421`
- 类别：未检查前置条件；诊断缺失
- 触发条件：`module.extra[index] < 0` 且 `module.args[index] + module.extra[index] <= module.data_length`（`args>=0` 已单独检查）。
- 后果：负长度被静默接受，`patch_kinds=3` 的重定位仍按 `data_base + args` 解析，尺寸信息被丢弃，生成的数据引用语义不明。
- 最小修复：条件中加入 `module.extra[index] < 0` 直接 `return false`。

### [中] 74-10｜KrtKroNativeStackDepths 先把 int64 extra 截断成 int32 再判范围
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1140`
- 类别：未检查前置条件（整数溢出/窄化）
- 触发条件：`module.extra[index]` 超出 int32 表示范围（例如负大数或 > 2^31-1）。
- 后果：`(int32)module.extra[index]` 回绕后参与 `need`/`delta` 计算（1149-1156 行），栈深度分析结果错误；可能把非法 IR 判为合法，或在后续发射中才以无关原因失败。
- 最小修复：在用范围内先以 int64 校验 `extra`，再窄化；或对其绝对值设置统一上限。

### [中] 74-11｜文本缓冲容量估算使用未校验的 extra，可为负
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1193`
- 类别：未检查前置条件；静默失败
- 触发条件：`module.ops[i]==19` 且 `module.extra[i]` 为负，`capacity += extra*20 + 64` 反而缩小容量。
- 后果：`words=(capacity+3)/4` 偏小，发射中后期 `KrtKroEmitTextByte` 因越界返回 false，错误点在大量代码之后才暴露，定位困难。
- 最小修复：估算循环中对 `extra` 做非负校验（或按 0 处理并单独报错）。
- 备注：`KrtKroEmitTextByte`（454 行）有边界保护，不会造成越界写，故定为中而非高。

### [中] 74-12｜宽指令暂存指针挂在 output 对象上并在逐函数间复用
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1260`（`output.native_wide_base/cursor/limit`）
- 类别：共享可变状态
- 触发条件：一次构建中连续编译多个函数（1225-1524 行循环），每个函数在 1260 行重置 cursor/limit；期间的 `return false` 路径会留下半更新状态。
- 后果：发射正确性依赖该隐式全局游标的生命周期；任何提前 return 后再重入，或并行编译同一 output，都会读到脏状态。注释（438-442 行）声明其为「compiler state, never object-file metadata」，但没有隔离措施。
- 最小修复：把 base/cursor/limit 收进发射上下文局部结构，随函数进出栈重置。
- 备注：疑似（需结合 1735 行后宽指令发射函数确认 cursor 的全部读者）。

### [中] 74-13｜op==30 把负栈深度钳为 0，掩盖上游分析错误
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1408`-`1409`
- 类别：静默失败；诊断缺失
- 触发条件：`stack_depths[index] < 1`（例如该块未被 `KrtKroNativeFlowEdge` 到达而保持 -1，或上游 need/delta 计算有误）。
- 后果：`live_values` 被静默改为 0，后续搬移操作数指令按错误深度生成；真正的分析错误永不报告。
- 最小修复：`live_values < 0` 时直接 `return false` 并指明指令序号。

### [中] 74-14｜同一 KrtKroObject 在动态文本与遗留 text_word 两条写出路径间状态串味
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:618`-`621`（动态路径优先），与 `487`-`496`、`842`-`876` 的遗留字段写入
- 类别：共享可变状态；契约破坏
- 触发条件：同一个 `KrtKroObject` 先经 `KrtKroBeginText`（445-450 行）写过动态文本，再交给 `KrtKroEmitConstantHelperCall`/`KrtKroEmitIrFunction` 等只写 `text_word0..7` 的发射器。
- 后果：`KrtKroWriteText` 因 `text_words != null` 走动态分支，完全忽略刚写入的遗留指令字，输出与请求不符且无任何报错。
- 最小修复：遗留发射器入口清空 `text_words`/`text_word_count`，或 `KrtKroObjectIsValid` 增加互斥断言。

### [中] 74-15｜KrtKroEmitStackModule 失败后污染 module 的发布态
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1207`-`1208`、`1215`-`1216`（先发布 `function_offsets/function_sizes/external_reloc_*`），失败点 `1232`、`1237`、`1242`、`1374` 等
- 类别：共享可变状态；错误处理不一致；资源与生命周期
- 触发条件：任一轮发射在发布 offsets 后返回 false（如 patch 容量不足、KrtKroNativeStackDepths 失败、操作码非法）。
- 后果：`module` 持有指向旧数据的偏移数组与半填充的 `external_reloc_offsets`；若调用方重试或对同一 module 再编译，会得到错误的重定位目标。
- 最小修复：全部成功后再把 offsets 写回 module，或用局部数组构建完再交换。

### [低] 74-16｜后端混用裸数字操作码与枚举常量
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1335`（`op == 75`）、`1457`（`op == 31`）、`1501`（`op == 50`）、`1333`-`1516` 全段
- 类别：配置漂移；注释与实现脱节
- 触发条件：`Ir.krt:1554-1568` 的 `KrtNativeOp` 枚举顺序或取值调整时，本文件大量字面量（30/31/32/33/35-43/44-46/50-54/60-69/73-76）不会随之变化。
- 后果：枚举重排后此处静默失配（把一种操作当另一种发射），且编译期无任何提示。
- 最小修复：全部改用 `(int32)KrtNativeOp.Xxx`；至少对宽操作 60-69 与守卫 75/76 建常量表。

### [低] 74-17｜FrameLoad/FrameStore 自身不限制 slot，位移可按 int32 回绕
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:966`、`978`（`-8 * (slot + 1)`）
- 类别：未检查前置条件（整数溢出）
- 触发条件：以接近 `INT32_MAX/8` 的 slot 调用这两个通用 helper（`KrtKroNativeIndirectCall` 的 `scratch` 来自 `module.local_counts`，当前仅被 frame 上限间接约束）。
- 后果：displacement 回绕为正数，生成 `mov reg,[rbp+正偏移]`，读写调用者帧之外的内存。
- 最小修复：helper 入口校验 `slot >= 0 && slot <= 268435455`，或改用 int64 计算后判范围。
- 备注：在 `KrtKroEmitStackModule` 主路径中 slot 受 1257 行 `frame_bytes <= 1048576` 间接限制，故为低。

### [低] 74-18｜Allocate 把 stride==0 当作按字节分配，语义未文档化
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1077`-`1083`（仅 `stride > 1` 时相乘），调用示例 `1622`（传 0）
- 类别：契约破坏；语义分叉
- 触发条件：任何 `KrtKroNativeAllocate(stride=0)` 调用（如字符串拼接）。
- 后果：0 与 1 行为完全相同；若 C 版把 0 视为非法或另有含义，即构成跨实现分叉；当前注释（1067-1069 行）只说明 payload/头部布局，未说明 0 的语义。
- 最小修复：明确 0/1 等价并写入注释，或拒绝 0。
- 备注：疑似——缺少 C 版对照，无法确认是否有意设计。
