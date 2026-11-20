# 第五十八批：KroCodegen.c:861-1622

## 覆盖范围
- 文件与行段：`Re.KrtC/src/Compiler/Backend/Kro/KroCodegen.c` 第 861-1622 行（`generate_instruction` 尾部、`generate_block`、`calculate_function_stack_size`、`generate_function`、`generate_data_section`、`KrtKrtGenerate`）。
- 为核对符号定义，另按需读取同文件相邻少量上下文（110-189、220-310、355-404、449-511、550-620、630-740），并 grep 了 `KroWriter.c`、`Ir.c`、`Ir.h`、`KroCodegen.h`、`KroRegisterPacking.inc`、`KroTailCalls.inc`、`Compiler.c` 中的特定符号行。未通读其他源文件。
- 前提 G1 已应用：`KRT_MALLOC/CALLOC/REALLOC/STRDUP` 失败返回 NULL，`KrtMemoryReportError` 为空操作，因此所有分配失败分支均按「活路径 + 零诊断」判定。

## 问题条目

### [致命] 58-1｜`output_file` 参数完全未使用，写盘走的是 `output_filename`
- 位置：`KroCodegen.c:1470`（签名）、`1478`（仅存 `ctx.output_file`）、实际写盘 `1506`、`1615`；对照 `KroCodegen.h:87` 注释 “Emit a KRO object for module into output_file; output_filename names diagnostics.”
- 类别：6 契约破坏；13 注释与实现脱节；5 死代码（`ctx.output_file` 为死字段）
- 触发条件：所有 `KRT_TARGET_KRO_OBJ` / `KRT_TARGET_EXE_PLATFORM` 编译（`Compiler.c:243`、`247` 传入 `compiler->output_file` 与 `compiler->output_filename`）。
- 后果：调用方打开的 `FILE*` 从未被写入；后端用 `output_filename` 再次 `fopen(...,"wb")`（`KroWriter.c:481`）。同一文件被两个句柄以 `"wb"` 打开（前者先截断、后者再截断写入）；若调用方传入的是管道 / 内存流 / `/dev/stdout` / 与文件名不同的目标，输出会丢失或落点错误；`output_filename == NULL` 时静默写当前目录下的 `output.kro`（`1487`）。
- 最小修复：为 `kro_write_file` 增加 `FILE*` 重载并真正写 `output_file`；或删除 `output_file` 参数，同步修正 `KroCodegen.h:87` 注释与 `Compiler.c` 调用约定。
- 备注：无。

### [致命] 58-2｜`kro_write_file` 失败被空 `if` 吞掉，且函数返回 void
- 位置：`KroCodegen.c:1615-1616`（`if (!kro_write_file(...)) { }`），另 `1506` 直接忽略返回值；返回类型 `void` 见 `1470`。
- 类别：1 静默失败；2 诊断缺失；9 错误处理不一致
- 触发条件：磁盘满、配额、权限不足、目标路径不可写（`kro_write_file` 内 `fopen`/`fwrite` 失败，`KroWriter.c:481-567`）。
- 后果：目标文件残缺或根本不存在，`KrtKrtGenerate` 仍正常返回，`Compiler.c:256` 继续 `return true`，编译被上报为成功；调用方无法区分成功与失败。
- 最小修复：`KrtKrtGenerate` 返回 `bool`，写盘失败时 `KrtError` 并向 `Compiler.c` 传播；至少删掉空 `{}` 并记录诊断。
- 备注：无。

### [致命] 58-3｜`string_const_sym_indices[]` 存在未初始化元素
- 位置：`KroCodegen.c:1418`（`KRT_MALLOC`，非 CALLOC）、`1426-1428`（`if (!str) { continue; }`）、`1439-1440`（仅非空字符串才赋值）；消费点 `685-688`。
- 类别：8 未检查前置条件（未初始化内存）；10 资源与生命周期
- 触发条件：`module->string_constants[i] == NULL`，即 `KrtIrStringConst` 中 `KRT_STRDUP` 失败（G1：返回 NULL，`Ir.c:1074`），或模块中本身存在空槽。
- 后果：`continue` 跳过赋值，`string_const_sym_indices[i]` 保持堆上垃圾值；随后 `emit_load_value_to_reg`（`685-688`）读出该垃圾 `sym_idx`，只要 `>= 0` 就调用 `emit_load_string_addr_to_reg` → `kro_add_reloc`（`240`）把非法符号索引写入 ABS64 重定位；`kro_add_reloc`（`KroWriter.c:397-428`）不校验 `sym_idx`，最终产出损坏的目标文件。同时该字符串未写 rodata、未建符号，`str_const_N` 命名与实际符号错位。
- 最小修复：改用 `KRT_CALLOC`；或在 `continue` 前显式写 `-1`，并让引用处对 `-1` 走诊断/回退路径。
- 备注：与 58-4 的 OOM 路径叠加。

### [严重] 58-4｜`generate_data_section` 分配失败静默 `return`，下游零代码发射
- 位置：`KroCodegen.c:1418-1421`；下游 `685-691`。
- 类别：1 静默失败（G1）
- 触发条件：`KRT_MALLOC(module->string_const_count * sizeof(int32_t))` 返回 NULL。
- 后果：`ctx.string_const_count` 保持 0、指针保持 NULL（`1422` 未执行），函数无诊断返回；`KrtKrtGenerate`（`1495`）忽略该返回继续生成全部函数。此后每个 `KRT_IR_VALUE_STRING_CONST` 在 `685-691` 因 `sym_idx < 0` 命中 `if (sym_idx >= 0)` 的否分支，**一条指令都不发射**，目标寄存器残留上一条指令的旧值 → 字符串地址静默错误。
- 最小修复：失败时 `KrtError` 并使整个代码生成失败（向上返回状态），不要在半初始化状态下继续。
- 备注：`685-691` 的静默跳过是 OOM 零诊断的直接放大器。

### [严重] 58-5｜字符串长度检查失败直接 `return`，跳过全部剩余字符串与全部 globals
- 位置：`KroCodegen.c:1434`（`if (len >= UINT32_MAX) { KrtError(...); return; }`）。
- 类别：1 静默失败；4 阶段不对齐
- 触发条件：某个字符串常量长度 ≥ `UINT32_MAX`（或后续 `kro_write_rodata` 无法容纳）。
- 后果：`generate_data_section` 提前返回，剩余字符串常量与**所有全局变量**的 rodata/data 写入（`1444-1467`）全部丢失；调用方无感知，继续生成引用这些符号的函数，目标文件出现未定义符号 / 错误重定位。
- 最小修复：`generate_data_section` 返回 `bool`，失败时中止整次 `KrtKrtGenerate`，不得只丢局部数据。
- 备注：有 `KrtError` 但无传播通道，诊断与中止语义脱节。

### [严重] 58-6｜syscall 实参过多被静默截断
- 位置：`KroCodegen.c:1224-1226`（`if (arg_count > g_syscall_arg_reg_count) { arg_count = g_syscall_arg_reg_count; }`）。
- 类别：1 静默失败；2 诊断缺失
- 触发条件：`KRT_IR_SYSCALL` 的实参数 > `g_syscall_arg_reg_count`（Linux 6、Windows 4，见 `50-57`）。
- 后果：多余实参被直接丢弃，syscall 以错误参数执行（例如 `write` 丢失 buf/count、`mmap` 丢失 flag），且无任何诊断；生成器继续产出“看起来正常”的目标文件。
- 最小修复：超限时报错并中止/跳过该指令，同时保证寄存器装载与实际参数一致。
- 备注：无。

### [严重] 58-7｜`KRT_IR_ADDRESS_OF` 对 ARG 索引无边界检查
- 位置：`KroCodegen.c:1151-1152`（`offset = ctx->arg_stack_offsets[storage->data.index];`）。
- 类别：8 未检查前置条件（越界）
- 触发条件：`ADDRESS_OF` 的操作数是索引 `< 0` 或 `>= KRT_KRO_MAX_ARGS(128)` 的 `KRT_IR_VALUE_ARG`。
- 后果：越界读 `arg_stack_offsets` 之后的相邻字段（`arg_address_taken`、`arg_packed`，见 `KroCodegen.h:67-69`），`offset` 变成垃圾值；随后 `lea rax,[rbp-offset]`（`1162-1165`）生成错误地址，且错误地址会被当作指针使用。
- 最小修复：读取前检查 `storage->data.index >= 0 && storage->data.index < ctx->current_param_count`，否则回退到符号路径并报错。
- 备注：疑似，需验证 IR 能否产生越界 ARG 索引（对照 `660`、`666` 与 `KroRegisterPacking.inc:536-537` 均做了 `>= 0 && < current_param_count/< KRT_KRO_MAX_ARGS` 检查，此处为遗漏点）。

### [严重] 58-8｜`ADDRESS_OF` 符号未找到时把 `-1` 当符号索引写重定位
- 位置：`KroCodegen.c:1158-1160`（`int symbol = kro_find_symbol(...); emit_load_string_addr_to_reg(ctx, symbol, KRT_REG_RAX);`），经 `227-241` 调用 `kro_add_reloc`。
- 类别：8 未检查前置条件；6 契约破坏
- 触发条件：`VAR` 既不在局部变量表（`find_local_var` 返回 NULL）也不在符号表中——例如未声明/未导出符号，或 `KRT_IR_ALLOC`（`1195-1197`）在局部变量数达到 `KRT_KRO_MAX_LOCAL_VARS` 后 `find_or_alloc_local_var` 返回 NULL 的路径。
- 后果：`kro_find_symbol` 返回 `-1`（`KroWriter.c:357-372`），`emit_load_string_addr_to_reg` 将其转为 `uint32_t` 的 `0xFFFFFFFF` 写入 ABS64 重定位；`kro_add_reloc`（`KroWriter.c:397-428`）不校验符号索引，目标文件含非法重定位，加载/链接阶段才暴露或越界访问。
- 最小修复：`symbol < 0` 时 `KrtError` 并跳过该地址计算（或写 0），不得把 `-1` 传入重定位。
- 备注：疑似，需验证是否存在“未声明 VAR 取地址”或“局部变量表溢出”的合法/可达路径。

### [中] 58-9｜`KrtStorePtr` / `KrtLoadPtr` 操作数不足时静默回退为普通函数调用
- 位置：`KroCodegen.c:923`（`&& inst->operand_count >= 4`）、`933`（`&& inst->operand_count >= 3`），回退路径 `944-1045`。
- 类别：3 语义分叉；9 错误处理不一致；2 诊断缺失
- 触发条件：前端为这两个内建生成实参不足的调用（如 `KrtStorePtr` 仅 2 个实参，`operand_count == 3`）。
- 后果：条件不成立即落入通用调用流程，把 `"KrtStorePtr"` / `"KrtLoadPtr"` 当作外部函数符号调用（`1032-1040`）；若运行时不存在该函数，则生成未定义符号引用，且原本的内存存储/加载语义完全丢失，无任何诊断。
- 最小修复：内建操作数数量不匹配时 `KrtError` 并中止，不要静默改走通用调用。
- 备注：疑似，需验证前端/IR 优化是否可能生成实参不足的内建调用。

### [中] 58-10｜入口参数为空与 writer 创建失败均静默 `return`
- 位置：`KroCodegen.c:1471-1473`（`if (!output_file || !module) return;`）、`1491-1493`（`if (!ctx.writer) return;`）。
- 类别：1 静默失败；2 诊断缺失
- 触发条件：调用方传入 NULL 的 `output_file`/`module`，或 `kro_writer_create()` 失败。
- 后果：直接返回，不写文件、不 `KrtError`；`Compiler.c:256` 仍返回成功，问题被延迟到加载/运行阶段才暴露。
- 最小修复：统一改为返回 `bool` 并在失败时 `KrtError`。
- 备注：与 58-2 构成同一条“失败不传播”链。

### [中] 58-11｜`generate_function` 失败路径静默返回，不留下任何产物
- 位置：`KroCodegen.c:1559-1565`（`if (!generate_function(...)) { release_temporary_storage(...); KRT_FREE(...); kro_writer_destroy(...); return; }`）。
- 类别：1 静默失败；2 诊断缺失
- 触发条件：`generate_function` 返回 false（如 `1291-1294` 的临时存储分配失败；该分支自身有 `KrtError`，但调用点无额外处理）。
- 后果：目标文件完全不写，且此前已写入的 data/rodata 全部随 `kro_writer_destroy` 丢弃；调用方仍判定编译成功。错误处理与 58-2 的“写盘失败”不一致（一处有诊断、一处无）。
- 最小修复：失败时 `KrtError` 并向 `Compiler.c` 返回失败状态。
- 备注：无。

### [中] 58-12｜`output_filename` 超长被静默截断
- 位置：`KroCodegen.c:1480-1485`（`if (len >= sizeof(ctx.output_filename)) { len = sizeof(...) - 1; }`）。
- 类别：11 配置漂移；1 静默失败
- 触发条件：调用方传入长度 ≥ 256 的 `output_filename`。
- 后果：文件名被静默截断，输出写到另一个路径，用户指定的目标丢失且无告警。
- 最小修复：超长时报错并失败，或动态分配并完整保存路径。
- 备注：无。

### [中] 58-13｜全局符号添加失败 / 重名被静默接受
- 位置：`KroCodegen.c:1466`（`kro_add_symbol(...)` 返回值被丢弃），同类 `1587`（`_start` 符号）。
- 类别：1 静默失败；9 错误处理不一致
- 触发条件：`kro_add_symbol` 因容量/内存失败返回 `-1`（`KroWriter.c:297-302`），或 global 名称与函数/其他 global/ `_start` 重名。
- 后果：符号表出现 `-1` 或重复符号，后续重定位按名字取符号时行为不确定，链接期才报错；生成阶段零诊断。
- 最小修复：检查返回值；对重名做检测并报错。
- 备注：无。

### [中] 58-14｜`RETURN` 无操作数时静默返回 0
- 位置：`KroCodegen.c:1054-1056`（`} else if (!identity) { emit_load_imm64(ctx, 0); }`）。
- 类别：2 诊断缺失；3 语义分叉
- 触发条件：`KRT_IR_RETURN` 的 `operand_count == 0` 且未被 `emit_tail_identity_return` 命中（如优化/SSA 改写后遗留的终止指令）。
- 后果：对非 void 返回类型的函数静默返回整数 0，不报诊断；调用方得到错误返回值。
- 最小修复：校验 `operand_count` 与 `func->return_type` 的一致性，缺失时 `KrtError`。
- 备注：疑似，需验证 RETURN 是否可能零操作数（`KrtIrReturn` 总是添加 1 个操作数，`Ir.c:882-894`；但优化/SSA 路径未逐一核对）。

### [中] 58-15｜`default` 分支静默忽略未实现的 IR opcode
- 位置：`KroCodegen.c:1243-1244`（`default: break;`）。
- 类别：5 死代码；2 诊断缺失
- 触发条件：IR 中出现本 switch 未覆盖的 opcode（例如 `Ir.h` 中存在但此处未处理的指令）。
- 后果：该指令不发射任何代码，程序静默少做一步，且无告警；与 `1243` 同函数的其它分支（都带前置检查）形成错误处理不一致。
- 最小修复：对未实现 opcode `KrtError` 并中止。
- 备注：疑似，需对照 `Ir.h:226-275` 的 opcode 列表核对是否全部覆盖。

### [低] 58-16｜`ADDRESS_OF` / `STACKALLOC` / `ALLOC` 未检查 `operand_count`
- 位置：`KroCodegen.c:1149`（`KrtIRValue* storage = &inst->operands[0];`）、`1170` 与 `1175`（`operands[0]`/`operands[1]`）、`1190`（`operands[0]`）。
- 类别：8 未检查前置条件（越界）
- 触发条件：对应指令 `operand_count` 小于代码读取的下标。
- 后果：越界读 `inst->operands` 数组，得到垃圾值后继续生成代码。
- 最小修复：进入各分支前检查 `operand_count`（与 `1077`、`1098`、`1118` 等分支的写法保持一致）。
- 备注：疑似，需验证 IR 构造器是否保证操作数个数（`KrtIrAddressOf`/`KrtIrStackAlloc` 在 `Ir.c:993-1014` 固定添加操作数）。

### [低] 58-17｜`KRT_IR_PHI` 在栈帧大小固定后仍可能增长帧
- 位置：`KroCodegen.c:1213`（`alloc_temp_slot(ctx, inst->result.data.index);`），`alloc_temp_slot` 定义 `558-568`（会 `ctx->current_stack_offset += 16`），而 `stack_size` 已在 `1284-1288` / `1332` 计算并用于 prologue。
- 类别：4 阶段不对齐；8 未检查前置条件
- 触发条件：PHI 结果 temp 在代码生成阶段仍处于 `stack_offset == 0 && reg_low < 0`（例如被优化阶段标记 `no_storage`，`KroRegisterPacking.inc:289/481`、`KroTailCalls.inc:122/157`）。
- 后果：`alloc_temp_slot` 在 `stack_size` 固定之后追加 16 字节栈空间，超出已分配帧，破坏栈布局（与调用者参数区/红区重叠）。
- 最小修复：保证所有 temp 在 `plan_function_storage` 阶段完成分配；`alloc_temp_slot` 需要新增槽位时视为内部错误并中止。
- 备注：疑似，需验证 PHI temp 是否可能被标记 `no_storage`（`record_function_temporaries` 对 PHI 只设 `skip_register`，`KroRegisterPacking.inc:215-217`）。

### [低] 58-18｜`if (!global)` 为恒不成立的死检查
- 位置：`KroCodegen.c:1446`（`KrtIRGlobal* global = &module->globals[i]; if (!global || !global->name)`）。
- 类别：5 死代码
- 触发条件：恒不成立——`global` 是对 `module->globals[i]` 取地址，永远非 NULL。
- 后果：给出“已做空指针防护”的错觉，真正未检查的是 `module->globals` 本身（`1444` 的循环上界来自 `module->global_count`，数组可能为 NULL 或更小）。
- 最小修复：删除 `!global`，改为检查 `module->globals` 非空且 `i < global_capacity`。
- 备注：无。

### [低] 58-19｜直接调用时 `function_name == NULL` 静默 `break`
- 位置：`KroCodegen.c:918-921`（`const char* func_name = ...; if (!func_name) { break; }`）。
- 类别：2 诊断缺失；1 静默失败
- 触发条件：`KRT_IR_VALUE_FUNCTION` 值的 `data.function_name` 为 NULL（函数值构造失败或符号名为空）。
- 后果：整条调用指令被静默丢弃，不发射调用、不报错，程序行为缺失。
- 最小修复：`KrtError` 并中止，或回退到 `kro_add_undefined_symbol` 路径。
- 备注：无。
