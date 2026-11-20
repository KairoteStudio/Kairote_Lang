# 第七十五批：SelfHost/Compiler/Backend/Kro/KroBackend.krt（1736-3470）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Backend/Kro/KroBackend.krt`，仅读取 1736-3470 行（全文件共 3469 行）。
- 覆盖函数：`KrtKroNativeWideFloat8Bits` 尾部、`KrtKroLibraryWord/Symbol/Name`、`KrtKroLibraryNormalize/Value`、`KrtKroLibraryStaticData`、`KrtKroEmitLibrarySections`、`KrtKroEmitLibraryData`、`KrtKroLibraryPublicSignature/PublicDefinition/NeedsAbi`、`KrtKroLibraryFunctions`、`KrtModuleMetadataClose/Edits/File/Build`、`KrtKroModuleMetadata`、`KrtNativeAllocateRegisters`、`KrtKroNativeRegisterFrame/PreserveRegisters/AllocatedLoad/AllocatedStore`、`KrtKroNativeTemporaryOp/Flush/Push/Pop/Cast/Instruction`、`KrtKroRuntimeAddress/AddSymbol/Name/Map/Find/Descriptors`、`KrtSsaAllocate`、`KrtKroSsaLoad/Store/Edge/Read/ScalarOp/Scalar`、`KrtKroEmitSsaModule`、`KrtKroEmitNativeModule`、`KrtKroWideScratchBytes`、`KrtKroNativeWideBox/Load/Store/Convert/Operands/Binary/Compare/Unary/Post/Float/Ref`。
- 交叉核对（仅按符号 grep/定点读取，未通读其他文件）：`SelfHost/Compiler/Middle/Ir/Ir.krt` 的 `KrtNativeOp` 枚举、`KrtSsaValidate`、`KrtSsaBuild`、`KrtSsaInputs`；`SelfHost/Compiler/Frontend/Semantic/SemanticRuntime.krt` 的 `KrtNativeRuntimeRead`；`SelfHost/Compiler/Pipeline/CompilerPipeline.krt` 的调用点；同文件 1405-1419（非 SSA 路径）用于对照。
- Re.KrtC 的 Kro 后端为 IR 驱动的 `KroCodegen.c/.h + Kro*.inc`，与本 Krt 自举实现结构不同，无法逐函数对照；相关分叉均标「疑似」。

## 问题条目

### [高] 75-1｜SSA 路径 StackAllocate 的 `live_values` 恒为 0（死代码 + 语义分叉）
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3030-3031`（`int32 live_values = 0; if (live_values < 0) { live_values = 0; }`），使用点 `:3038`、`:3041`
- 类别：死代码 / 语义分叉 / 注释与实现脱节
- 触发条件：SSA 基本块内出现 op 30（StackAllocate），且该分配嵌在更大表达式中（其下仍有活跃表达式操作数）。
- 后果：注释宣称「Shift live expression operands below the allocation」，但 `live_values` 恒为 0，`if (live_values < 0)` 永远为假；`rep movsq`（rcx=0）为空操作，活跃操作数不会被搬到新 rsp 之上，随后 `mov rax,[rsp+live_values*8]` 取到未初始化单元，表达式栈序错位，可能导致栈损坏/错误结果。同文件非 SSA 路径 `:1408` 使用 `stack_depths[index]-1`，确认是实现分叉。
- 最小修复：在 SSA 构建/发射期保留每个 value 的表达式栈深度，用 `depth-1` 计算 `live_values`；或在 StackAllocate 前强制 flush 表达式栈。
- 备注：与非 SSA 路径 1405-1409 对照确认。

### [高] 75-2｜`KrtKroRuntimeDescriptors` 经无边界检查的 `KrtNativeRuntimeRead` 读运行时 blob
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2520`（`KrtNativeRuntimeRead(module.runtime_bytes,record.offset+56)`）
- 类别：未检查前置条件 / 资源与生命周期
- 触发条件：runtime 类型记录的 `offset` 接近 blob 末尾，或 runtime blob 损坏/被伪造。
- 后果：`KrtNativeRuntimeRead`（`SemanticRuntime.krt:980-984`）直接 `bytes[offset+index]` 读 8 字节且无任何边界检查，越界读可能导致崩溃或把相邻内存当作 `interface_count` 使用，进而放大为后续越界。
- 最小修复：调用前验证 `record.offset >= 0 && record.offset + 64 <= module.runtime_length`；并让 `KrtNativeRuntimeRead` 自带边界检查。
- 备注：辅助函数位于其他文件，已按符号定点核对。

### [中] 75-3｜`method.target` 未校验即索引 `module.function_offsets`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2535-2537`
- 类别：未检查前置条件
- 触发条件：runtime method 的 `target.value` 为负数以外的越界函数索引。
- 后果：`module.function_offsets[target]` / `module.function_sizes[target]` 越界读，生成错误符号 offset/size，产出损坏对象或未定义行为。
- 最小修复：增加 `if (target >= module.function_count) { return false; }`（`target < 0` 已处理）。
- 备注：疑似（C 版无对应实现可对照）。

### [中] 75-4｜`relocation.target` 未校验即索引 `descriptor_symbols`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2560`
- 类别：未检查前置条件
- 触发条件：`runtime_relocation.kind == 1` 且 `target` 越界（负数或 `>= runtime_type_count`）。
- 后果：`descriptor_symbols[...]` 越界读，重定位被写成错误符号号。
- 最小修复：索引前检查 `relocation.target >= 0 && relocation.target < module.runtime_type_count`。
- 备注：疑似。

### [中] 75-5｜`record.index` 未校验即写入 `descriptor_symbols`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2517`（`descriptor_symbols[record.index] = symbol;`）
- 类别：未检查前置条件
- 触发条件：runtime 记录的 `index` 越界。
- 后果：越界写，破坏堆上数组。
- 最小修复：写入前检查 `record.index >= 0 && record.index < module.runtime_type_count`。
- 备注：疑似。

### [中] 75-6｜复制 rodata 重定位时源数组写成 `text_relocation_bytes`
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2556`（`relocations[text_count*16+index] = output.text_relocation_bytes[previous_text*16+index];`）
- 类别：语义分叉 / 契约破坏
- 触发条件：`output.rodata_reloc_count > 0`（复用同一 output 对象，或先执行过带 rodata 重定位的发射）。
- 后果：从 `text_relocation_bytes + previous_text*16` 读取并当作数据重定位追加，源与长度错位，产出损坏的重定位表。
- 最小修复：改用 `output.rodata_relocation_bytes`（或实际存放 rodata 重定位的数组），并核对字段命名。
- 备注：疑似；当前管线中 `rodata_reloc_count` 似恒为 0，属潜在缺陷。

### [中] 75-7｜库数据「局部名消歧」后缀未被符号表引用
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1942-1954`
- 类别：契约破坏 / 注释与实现脱节
- 触发条件：同一 data 模块内有两个同名 access-hidden 声明，或局部名与导出名相同。
- 后果：`KrtKroLibrarySymbol` 在 `:1942` 记录的 name 偏移指向未加后缀的原名；`:1949-1954` 追加的 `$local<index>` 副本没有任何符号引用，符号表仍出现重名，消费者/链接器无法区分，可能解析到错误符号。与函数路径 `:2004`「先改 `link_name` 再记录」的做法不一致。
- 最小修复：在追加后缀后再记录符号名偏移（或把 `name` 回退到后缀起点后调用 `KrtKroLibrarySymbol`）。
- 备注：疑似（C 版无对应实现）。

### [中] 75-8｜`KrtKroLibraryFunctions` 字符串表容量无运行时校验
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1992`（`new byte[count*8192+64]`），写入点 `:2009-2010`、`:2028-2029`、`:2033`、`:2036`
- 类别：未检查前置条件 / 资源与生命周期
- 触发条件：函数/外部符号名、ABI manifest 或 marker 的总长度超过预留容量。
- 后果：Krt 数组无边界检查，静默越界写，堆损坏且无诊断。
- 最小修复：写入前检查 `name + length + 1 <= capacity`，否则 `return false`。
- 备注：疑似；`KrtKroAbiFinishNamed`（`:336-348`）同样直接写 `output.string_bytes[name]`。

### [中] 75-9｜`KrtKroLibraryValue` 常量移位未校验移位量
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1851-1852`（`low = left << right;` / `left >> right`）
- 类别：未检查前置条件（移位）
- 触发条件：库静态数据中的常量表达式出现 `<<`/`>>`，且右操作数 `>= 64` 或为负。
- 后果：编译器自身执行未定义/实现定义移位，常量折叠结果错误；自举时错误常量会传播到生成代码，且无任何诊断。同函数对除零、`INT64_MIN / -1` 已有保护，仅移位缺失。
- 最小修复：`if (right < 0 || right >= 64) { return false; }`，或按语言语义折叠。
- 备注：疑似（C 版不可对照）。

### [中] 75-10｜`KrtKroSsaScalar` 对 op 2/3/100 缺少局部槽位范围检查
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2752-2754`
- 类别：未检查前置条件 / 错误处理不一致
- 触发条件：优化开启（`KrtDriverOptimization > 0`）时，SSA 中 op 2/3/100 的 `args` 越界。`KrtSsaValidate` 不校验 op 2/3/100 的槽位，而 generic 路径 `:2919`、`:2922` 有检查。
- 后果：直接以 `rbp-8*(slot+1)` 读写帧外内存，静默错误或崩溃；与 generic 路径行为不一致。
- 最小修复：与 `:2919/:2922` 一致，检查 `args >= 0 && args < module.local_counts[function_index]`。
- 备注：疑似（正常 SSA 构建保证范围，但缺少纵深防御）。

### [中] 75-11｜`KrtKroLibraryStaticData` 失败路径留下部分可变状态
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1884-1899`
- 类别：错误处理不一致 / 共享可变状态
- 触发条件：guard 槽位断言失败（`:1897` `module.initializer_guard_slot != module.global_count-2`）或在提交后失败。
- 后果：第二遍循环已写 `entry.static_data/static_low/static_high`，随后 return false；调用方只看到 false。管线 `CompilerPipeline.krt:515` 在返回 false 且 `function_count != 0` 时继续走 SSA，会用到被部分修改的 module/ast（static_data 标志、被摘链的 guard、被改小的 global_count）。
- 最小修复：先完成全部校验再提交修改；失败时回滚已写标志与计数。
- 备注：调用点已定点核对。

### [低] 75-12｜SSA 发射中 `op == KrtNativeOp.Nop` 分支不可达
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2915`（`else if (op == (int32)KrtNativeOp.Nop) { }`）
- 类别：死代码
- 触发条件：恒不触发（`:2900` 已 `if (op == 0 || op == 101) { index+1; continue; }`，而 `Nop = 0`）。
- 后果：无功能影响，增加维护误导。
- 最小修复：删除该分支，或移除 `op==0` 的前置跳过。
- 备注：`Nop=0` 见 `Ir.krt:1555`。

### [低] 75-13｜SSA 发射中 op 27（Drop）分支不可达
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3054-3055`
- 类别：死代码
- 触发条件：恒不触发（`KrtSsaBuild` 在 `Ir.krt:3369` 把 op 27 一律降级为 Nop）。
- 后果：无功能影响；若未来 SSA 不再降级 Drop，需重新确认该分支语义。
- 最小修复：删除该分支或注明依赖 SSA 降级。
- 备注：与 75-12 同类但位置/依据不同，独立列出。

### [低] 75-14｜SSA 发射中 op 32（Dup）分支不可达且编码与临时路径不一致
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3083-3084`（`255,52,36` = `call qword [rsp]`）
- 类别：死代码 / 疑似语义分叉
- 触发条件：恒不触发（`KrtSsaBuild` 在 `Ir.krt:3370-3372` 把 op 32 降级为 Nop 并复制栈值）。
- 后果：当前无影响；若该分支被启用，`call [rsp]` 与临时路径 `:2426` 的 `mov rax,[rsp]`（0x24048b48）语义不符。
- 最小修复：删除该分支，或改为与 `:2426` 一致的 `mov rax,[rsp]`。
- 备注：疑似；因不可达暂列低。

### [低] 75-15｜`KrtKroEmitSsaModule` 末尾重置 `symbol_count/strtab_size` 可能丢弃前序符号
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3195`（`output.symbol_count = 1; output.strtab_size = 6;`）
- 类别：契约破坏 / 共享可变状态
- 触发条件：同一 output 对象在本函数调用前已含符号（复用输出缓冲）。
- 后果：既有符号与字符串表计数被清零，若前序符号是必要的（如 ABI/manifest 符号），会丢失。
- 最小修复：明确输出对象所有权；重置前保存/迁移既有符号，或不重置。
- 备注：疑似；当前管线每次新建 output。

### [中] 75-16｜`external_used` / `external_reloc_count` 未在发射开始时重置
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2817`（取 `module.external_used`）、`:3181`（`external_used[external]=true; module.external_reloc_count++`）；入口 `:2777` 只重置 `runtime_code_count`
- 类别：共享可变状态 / 资源与生命周期
- 触发条件：同一 module 被发射两次，或输出/模块被复用。
- 后果：`external_used` 残留 true 使 `KrtKroLibraryFunctions` 多发射未引用外部符号；`external_reloc_count` 累积可能超过 `patch_capacity` 而失败，或产出重复重定位。
- 最小修复：发射开始时 `module.external_reloc_count = 0` 并清空 `external_used`，或每次使用独立数组。
- 备注：疑似；正常管线单次发射。

### [低] 75-17｜释放 `allocation` 成员但未释放对象自身
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3196`（`delete allocation.registers; delete allocation.spills; delete allocation.frame_counts; delete allocation;` 中 `delete allocation;` 缺失）
- 类别：资源与生命周期
- 触发条件：每次成功发射。
- 后果：`KrtSsaAllocation` 对象本身泄漏（若 Krt 无 GC）。
- 最小修复：补 `delete allocation;`。
- 备注：低。

### [中] 75-18｜`external_symbols[relocation_targets[index]]` 未校验索引
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2045`
- 类别：未检查前置条件
- 触发条件：`module.external_reloc_targets` 含越界或负值（损坏状态或复用 module 后累积）。
- 后果：`external_symbols` 越界读，写出错误符号号。
- 最小修复：索引前检查 `>= 0 && < module.external_count`。
- 备注：正常由 SSA 发射填充时安全；疑似。

### [低] 75-19｜StackAllocate 分配大小对齐疑似错误（`and rsp` 而非 `and rax`）
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3032-3034`（`add rax,15`；`and rsp,-16`；`sub rsp,rax`）
- 类别：未检查前置条件 / 语义分叉（疑似）
- 触发条件：StackAllocate 的 size 不是「16 的倍数减 15」时。
- 后果：`rsp` 先对齐，再减去 `size+15`，结果 `rsp` 通常不再 16 字节对齐，后续 `rep movsq`/`push` 及依赖 rsp 对齐的操作可能出错或性能受损。若意图是向上取整分配大小，正确写法应为 `add rax,15; and rax,-16`。
- 最小修复：改为 `and rax,-16`（或在 `sub` 后重新对齐并同步调整帧）。
- 备注：疑似；非 SSA 路径 `:1410-1415` 写法相同，可能为有意 ABI。

### [中] 75-20｜参数上限契约不一致：被调 `parameter_counts > 128` 拒绝，调用 `arity <= 129` 放行
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2832`（`parameter_counts[function_index] > 128` 返回 false）与 `:2966`（`arity > 129` 才拒绝）
- 类别：契约破坏 / 阶段不对齐
- 触发条件：模块含 129 个源参数的函数，或调用 arity=129 的函数。
- 后果：调用点可生成，但被调函数发射阶段直接失败，编译中断；若为外部声明则生成无法解析的调用。128 源参数 + 1 隐藏聚合指针 = 129 物理参数，两处上限未统一表达。
- 最小修复：统一「源参数 128 + 隐藏指针 1」的上限语义，或区分 source/physical 计数。
- 备注：疑似。

### [中] 75-21｜op 25（Data）未校验 `ssa.extra` 非负
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:3043`（`ssa.args[index] < 0 || ssa.args[index] + ssa.extra[index] > module.data_length`）
- 类别：未检查前置条件
- 触发条件：SSA 中 op 25 的 `extra` 为负（损坏 SSA；`KrtSsaValidate` 不校验 op 25）。
- 后果：检查不触发，生成 `data_base + args` 的数据指针，长度语义丢失，可能越界。
- 最小修复：增加 `ssa.extra[index] < 0` 检查。
- 备注：疑似（正常构建 extra >= 0）。

### [中] 75-22｜`output.data_reloc_count` 未更新
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2566-2567`（只设置 `text_reloc_count/total_reloc_count/relocation_count`）
- 类别：契约破坏
- 触发条件：消费者读取 `output.data_reloc_count`。
- 后果：数据段重定位计数保持陈旧值，对象契约不完整。
- 最小修复：设置 `output.data_reloc_count = data_count;`。
- 备注：疑似；取决于对象校验器是否读取该字段。

### [中] 75-23｜`output.rodata_reloc_count` 未重置/初始化，直接被 `KrtKroRuntimeDescriptors` 读取
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2544`（`int32 previous_data = output.rodata_reloc_count;`），而 `KrtKroLibraryFunctions` 在 `:2038-2039` 只设置 text 相关字段
- 类别：共享可变状态 / 契约破坏
- 触发条件：output 对象复用，或 `rodata_reloc_count` 未初始化。
- 后果：`previous_data` 非 0，触发 `:2556` 的错误复制路径。
- 最小修复：在 `KrtKroLibraryFunctions`/`KrtKroEmitSsaModule` 开始时重置 `output.rodata_reloc_count`（及 `data_reloc_count`）。
- 备注：与 75-6 相关但位置不同，独立列出。

### [低] 75-24｜`KrtKroEmitLibraryData` 将 `name_length` 由 int64 截断为 int32 累加
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:1923`（`names = names + (int32)declaration.name_length + 25;`）
- 类别：未检查前置条件 / 整数溢出
- 触发条件：声明名长度超过 `2^31-1`。
- 后果：容量估算被截断/溢出，字符串表分配不足，后续写入越界。
- 最小修复：先检查 `declaration.name_length` 范围，并用 int64 累加后再验证总容量。
- 备注：低（正常名字远小于该上限）。

### [低] 75-25｜`relocation.kind` 未校验，未知取值被当作函数重定位
- 位置：`SelfHost/Compiler/Backend/Kro/KroBackend.krt:2559-2560`（`kind == 0 ? ... : (kind == 1 ? ... : KrtKroRuntimeFind(functions, relocation.offset))`）
- 类别：未检查前置条件
- 触发条件：runtime 重定位的 `kind` 为 0/1/2 之外的值。
- 后果：被静默当作 kind 2（函数重定位），查表失败则整体失败，或错误解析。
- 最小修复：显式处理 0/1/2，其余 `return false`。
- 备注：低；疑似。
