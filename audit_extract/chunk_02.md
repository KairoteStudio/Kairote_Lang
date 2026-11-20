# Chunk 02 抽取

## 覆盖范围
- `## 本批发现（BackendPe.c）`（原文 :754）
- `## BackendElf.c（全 2077 行，已逐段读完）`（原文 :825）
- `## 补发：ArkLink/include/ArkLink/ 头文件契约层发现（第四轮已读未输出）`（原文 :977）
- `# 第五批：Re.KrtC/src/Main.c、Build/BuildSystem.c、Shared/BytecodeGenerator.c`（原文 :1037）
- `# 第六批：PlatformAbstraction.c、CompilerError.c、Path.c、SmartPtr.c、ArkLinkIntegration.c、KroWriter.c`（原文 :1184）

## 问题条目

### [致命] BackendPe-1｜section_maps 索引无范围检查
- 位置：`ArkLink/src/Backend/BackendPe.c:246,279,311`
- 类别：未检查前置条件
- 触发条件：`reloc->section_index >= input->section_count`（`temp_maps` 仅 `input->section_count` 项，见 :685）
- 后果：越界读，重定位表 RVA 错乱或崩溃
- 最小修复：`if (reloc->section_index >= input->section_count) continue/return ERR;`
- 备注：—

### [致命] BackendPe-2｜符号 section_index 越界读
- 位置：`ArkLink/src/Backend/BackendPe.c:1087`
- 类别：未检查前置条件
- 触发条件：`reloc->symbol->section_index` 越界（`section_maps` 共 `total_sections` 项；如 LoaderElf 把 `SHN_ABS` 映射为 0、LoaderKro 把 `.bss` 映射为 0 之外的情况）
- 后果：`output->section_maps[...].rva` 越界读
- 最小修复：加 `section_index < output->section_count` 检查
- 备注：—

### [致命] BackendPe-3｜file_offset 无范围检查且可溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:1090`
- 类别：未检查前置条件
- 触发条件：`output->section_maps[reloc->section_index].file_offset` 无范围检查；`+ reloc->offset` 可溢出
- 后果：越界指针，随后 :1122 写越界
- 最小修复：校验 `section_index` 与 `file_offset + offset <= output->size`
- 备注：—

### [致命] BackendPe-4｜reloc_file_offset 整数下溢导致堆越界写
- 位置：`ArkLink/src/Backend/BackendPe.c:1122`
- 类别：整数下溢 / 未检查前置条件
- 触发条件：`reloc_file_offset`（uint32_t）> `output->size`（size_t），`output->size - reloc_file_offset` 下溢为巨大值，`ark_reloc_apply_pe_base` 的 `size >= 4/8` 检查通过
- 后果：堆越界写（损坏重定位 offset 触发）
- 最小修复：`if (reloc_file_offset >= output->size) continue/return ERR;`
- 备注：—

### [致命] BackendPe-5｜导出序号下溢 / 越界写
- 位置：`ArkLink/src/Backend/BackendPe.c:629`
- 类别：未检查前置条件 / 整数下溢
- 触发条件：`eat + (exp->ordinal - input->export_ordinal_base) * sizeof(address)`，`ordinal` 与 `base` 均 uint32_t；`ordinal < base` 下溢为巨大 size_t，或 `ordinal - base >= export_count`
- 后果：堆越界写
- 最小修复：`uint64_t idx = (uint64_t)exp->ordinal - base; if (idx >= count) return ERR;`
- 备注：—

### [致命] BackendPe-6｜reloc_count 乘法溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:230`
- 类别：整数溢出
- 触发条件：`reloc_count > SIZE_MAX/4`，`malloc(input->reloc_count * sizeof(uint32_t))` 乘法未检查
- 后果：分配不足，:257 `page_rvas[page_count++]` 堆溢出
- 最小修复：`if (reloc_count > SIZE_MAX/sizeof(uint32_t)) return NULL;`
- 备注：—

### [致命] BackendPe-7｜block_size/total_size 溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:285-289,320`
- 类别：整数溢出
- 触发条件：`block_size = 8 + ((count + 1) & ~1) * 2` 与 `total_size += block_size` 未检查溢出；`calloc(1, total_size)` 后 :320 按 `count` 写 entry（单页重定位数量极大）
- 后果：堆溢出写
- 最小修复：每步检查 `count > (SIZE_MAX-8)/2`、`total_size > SIZE_MAX - block_size`
- 备注：—

### [致命] BackendPe-8｜import_count 乘法溢出 + NULL module
- 位置：`ArkLink/src/Backend/BackendPe.c:364`
- 类别：整数溢出 + 未检查前置条件
- 触发条件：`calloc(input->import_count, sizeof(char*))` 乘法未检查；:369-380 把可能为 NULL 的 `input->imports[i].module` 直接 `strcmp`/`modules[module_count++]`
- 后果：崩溃或堆溢出
- 最小修复：检查乘法溢出；跳过 NULL module 并诊断
- 备注：—

### [致命] BackendPe-9｜strlen(NULL) 崩溃
- 位置：`ArkLink/src/Backend/BackendPe.c:469-470`
- 类别：未检查前置条件
- 触发条件：`input->imports[import_idx].symbol` 为 NULL（`BackendCommon.c:574` 明确允许 symbol 为 NULL 跳过，PE 路径未同步该契约）
- 后果：`strlen(sym_name)` 解引用 NULL 崩溃
- 最小修复：`if (!sym_name) { free(sym_indices); goto fail; }` 或跳过
- 备注：—

### [致命] BackendPe-10｜export 相关乘法溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:561,565,570-572,588,596`
- 类别：整数溢出
- 触发条件：`malloc(input->export_count * sizeof(ArkExportEntry))`、`memcpy(..., export_count * sizeof(...))`、`count * sizeof(uint32_t)`（三处）、`malloc(count*sizeof(size_t))`（两处）均未检查乘法溢出（超大 export_count）
- 后果：分配不足 → 堆溢出
- 最小修复：统一 `if (count > SIZE_MAX/sizeof(T)) goto fail;`
- 备注：—

### [致命] BackendPe-11｜section_count 乘法溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:685,789`
- 类别：整数溢出
- 触发条件：`calloc(input->section_count, sizeof(ArkSectionRvaMap))`、`calloc(total_sections, sizeof(ArkSectionRvaMap))` 未检查
- 后果：分配不足 → 后续写溢出
- 最小修复：检查 `count > SIZE_MAX/sizeof(...)`
- 备注：—

### [致命] BackendPe-12｜头尺寸计算溢出/截断
- 位置：`ArkLink/src/Backend/BackendPe.c:743-748`
- 类别：整数溢出 / 阶段不对齐
- 触发条件：`total_sections * sizeof(PE_SECTION_HEADER)` 赋给 uint32_t `section_table_size`，`total_header_size = dos_header_size + pe_header_size + section_table_size` 可溢出/截断
- 后果：`headers_size` 计算错误，PE 头越界或截断
- 最小修复：用 uint64_t 计算并检查 `<= UINT32_MAX`
- 备注：—

### [致命] BackendPe-13｜RVA 计算溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:726-728,819-821`
- 类别：整数溢出
- 触发条件：`edata_rva = idata_rva + (uint32_t)((import_data_size + section_alignment - 1) & ~(section_alignment - 1))`；`import_data_size + 0xFFF` 可溢出，`idata_rva + ...` 可溢出；`reloc_rva` 同理
- 后果：段 RVA 回绕，数据目录指向错误位置
- 最小修复：用 uint64_t 计算并检查
- 备注：—

### [致命] BackendPe-14｜SizeOfCode 等累加溢出
- 位置：`ArkLink/src/Backend/BackendPe.c:879-886,890-896`
- 类别：整数溢出
- 触发条件：`(input->sections[i].size + section_alignment - 1)` 中 size 为 size_t 可溢出；`size_of_code += aligned_size` 累加溢出
- 后果：PE 头 SizeOfCode/SizeOfInitializedData 错误
- 最小修复：统一使用 `ark_backend_align_up`（已处理 clamp）而非手写表达式
- 备注：—

### [致命] BackendPe-15｜section 文件偏移/RVA 回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:967,1006,1026`
- 类别：整数溢出 / 阶段不对齐
- 触发条件：`raw_size = (uint32_t)((sec->size + file_alignment - 1) & ~(file_alignment - 1))` 可溢出并截断；`current_rva += (virtual_size + section_alignment - 1) & ~(section_alignment - 1)` 中 `virtual_size` 为 uint32_t，`+0xFFF` 可溢出；`current_file_offset += raw_size` 累加溢出
- 后果：section 文件偏移/RVA 回绕，`memcpy` 越界
- 最小修复：全部改用 uint64_t 与 `ark_backend_align_up`，并检查 `<= UINT32_MAX`
- 备注：—

### [致命] BackendPe-16｜两套头尺寸计算分叉
- 位置：`ArkLink/src/Backend/BackendPe.c:922-923`
- 类别：语义分叉 / 阶段不对齐
- 触发条件：`headers_size_calc = 0x80 + 4 + sizeof(PE_COFF_HEADER) + sizeof(PE_OPTIONAL_HEADER_64) + total_sections * 40` 与 :750 `headers_size = ark_backend_align_up_32(total_header_size, file_alignment)` 是两套计算；:747 `section_table_size` 是 uint32_t 截断值，:922 用 `total_sections * 40`（size_t 不截断）
- 后果：section 数使 `total_sections*40 > UINT32_MAX` 时两者不一致，`SizeOfHeaders` 与实际头布局不符
- 最小修复：抽出 `ark_pe_calc_header_size()`，两处共用
- 备注：—

### [致命] BackendPe-17｜current_rva 初值与 layout 口径不一致
- 位置：`ArkLink/src/Backend/BackendPe.c:959-960`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：`current_rva = section_alignment`（固定 0x1000），而 `ark_layout_create` 首段 VA 为 `image_base + ark_backend_align_up(headers_size, section_alignment)`；`headers_size > 0x1000`（section 数 > ~(0x1000-0x200)/40 ≈ 56）时 `align_up = 0x2000`，`temp_maps[0].rva = 0x2000` 但 `current_rva = 0x1000`
- 后果：`output->section_maps[].rva` 与 section 头 `VirtualAddress` 及后续重定位写位置全部错位，镜像损坏
- 最小修复：`current_rva` 初始化为 `ark_backend_align_up(headers_size, section_alignment)`，并与 layout 对齐口径统一
- 备注：—

### [致命] BackendPe-18｜section size 截断为 uint32_t
- 位置：`ArkLink/src/Backend/BackendPe.c:962-1008`
- 类别：整数溢出 / 截断
- 触发条件：`virtual_size = (uint32_t)sec->size`、`raw_size = (uint32_t)...`、`virtual_address = current_rva` 均把 size_t 截断为 uint32_t，而 `total_file_size` 用 64 位 `sec->file_size` 累加（section size > 4GB）
- 后果：`memcpy(output->data + raw_offset, sec->data, sec->size)` 越界（raw_offset 按截断值计算，size 按原始值）
- 最小修复：入口拒绝 `sec->size > UINT32_MAX`
- 备注：—

### [高] BackendPe-19｜strcmp 的 module 名可能为 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:373,422,434`
- 类别：未检查前置条件
- 触发条件：`strcmp(modules[j], mod)`、`strcmp(input->imports[i].module, module_name)` 中 `mod`/`module_name` 可能为 NULL（:369-380 未过滤 NULL module）
- 后果：崩溃
- 最小修复：统一过滤 NULL module
- 备注：—

### [高] BackendPe-20｜重定位 symbol 名 strcmp 未判 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:1078`
- 类别：未检查前置条件
- 触发条件：`strcmp(input->imports[j].symbol, reloc->symbol->name)` 两者均可能为 NULL
- 后果：崩溃
- 最小修复：跳过 NULL 并诊断
- 备注：—

### [高] BackendPe-21｜导入符号匹配失败静默用第一个 IAT 槽
- 位置：`ArkLink/src/Backend/BackendPe.c:1083`
- 类别：静默失败
- 触发条件：导入符号匹配失败
- 后果：`import_idx` 保持 0，静默使用第一个导入的 IAT 槽地址 → 符号地址错乱
- 最小修复：匹配失败时诊断并跳过/报错
- 备注：—

### [高] BackendPe-22｜未知重定位类型静默丢弃
- 位置：`ArkLink/src/Backend/BackendPe.c:1108-1109`
- 类别：静默失败 / 诊断缺失
- 触发条件：未知 `reloc->type`，`default: continue`
- 后果：重定位静默丢弃，无诊断
- 最小修复：记录诊断并统计失败数
- 备注：—

### [高] BackendPe-23｜段 data 为 NULL 静默留零
- 位置：`ArkLink/src/Backend/BackendPe.c:1002-1004`
- 类别：静默失败
- 触发条件：`sec->data == NULL && sec->size > 0`
- 后果：段内容丢失但链接成功
- 最小修复：返回 `ARK_LINK_ERR_INVALID_ARGUMENT` 或诊断
- 备注：—

### [高] BackendPe-24｜入口段无效静默用 0x1000
- 位置：`ArkLink/src/Backend/BackendPe.c:809-815`
- 类别：静默失败
- 触发条件：`entry_section` 无效
- 后果：静默用 `entry_point_rva = 0x1000`（首段起点）→ 入口点错误
- 最小修复：无效时返回错误
- 备注：—

### [高] BackendPe-25｜段名超 8 字符静默截断
- 位置：`ArkLink/src/Backend/BackendPe.c:205-207`
- 类别：静默失败
- 触发条件：section 名超 8 字符
- 后果：同名段冲突
- 最小修复：诊断或改用 `/42` 格式（COFF 长名）
- 备注：—

### [高] BackendPe-26｜导出序号回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:619`
- 类别：整数下溢 / 静默失败
- 触发条件：`(uint16_t)(exp->ordinal - input->export_ordinal_base)`，`ordinal < base` 时回绕为大正数写入 `eot`
- 后果：导出序号错乱
- 最小修复：检查 `ordinal >= base`
- 备注：—

### [高] BackendPe-27｜64 位 VA/size 多处截断
- 位置：`ArkLink/src/Backend/BackendPe.c:694-695,711,751,852,965-967,1014,1033,1053`
- 类别：整数截断（阶段不对齐）
- 触发条件：镜像 > 4GB，多处把 64 位 VA/size 截断为 uint32_t（`temp_maps[].rva`、`idata_rva`、`image_size`、`NumberOfSections`、`virtual_size/raw_size`、各段头 size）
- 后果：字段回绕，加载器误读
- 最小修复：入口检查 `<= UINT32_MAX`
- 备注：—

### [高] BackendPe-28｜PE 可选头字段硬编码，忽略 input
- 位置：`ArkLink/src/Backend/BackendPe.c:925,928-931`
- 类别：配置漂移
- 触发条件：`Subsystem`、`SizeOfStackReserve/Commit`、`SizeOfHeapReserve/Commit` 全硬编码，忽略 `input->subsystem`、`input->stack_size`（`Backend.h:170-171` 已声明）
- 后果：CLI/session 设置无效
- 最小修复：读 `input` 字段，缺省再回退默认
- 备注：与 Header-7 同源

### [高] BackendPe-29｜对齐值硬编码
- 位置：`ArkLink/src/Backend/BackendPe.c:674-675,909`
- 类别：配置漂移 / 魔法数字
- 触发条件：`section_alignment=0x1000`、`file_alignment=0x200` 硬编码，忽略 `input` 可能携带的对齐；`opt->BaseOfCode = section_alignment` 亦硬编码
- 后果：配置无效
- 最小修复：从 `input`/layout 读取并文档化
- 备注：—

### [高] BackendPe-30｜SIZE_MAX 错误哨兵与 ark_buffer_append 契约冲突
- 位置：`ArkLink/src/Backend/BackendPe.c:222`
- 类别：契约破坏
- 触发条件：`*out_size = SIZE_MAX` 作为初始错误哨兵，但 `Backend.h:34` 规定 `ark_buffer_append` 失败返回 `SIZE_MAX`；调用方 :700 用 `reloc_data_size != 0` 判断
- 后果：语义混淆
- 最小修复：用独立 `int* ok` 或初始化 0
- 备注：—

### [高] BackendPe-31｜import_builder_get_rva 失败值写入 IAT
- 位置：`ArkLink/src/Backend/BackendPe.c:484-488`
- 类别：静默失败
- 触发条件：`import_builder_get_rva` 失败返回 `UINT32_MAX`
- 后果：被直接写入 ILT/IAT 条目 → 导入地址错误
- 最小修复：检查返回值
- 备注：—

### [高] BackendPe-32｜module_count==0 时 IAT RVA 静默为 0
- 位置：`ArkLink/src/Backend/BackendPe.c:501-504`
- 类别：静默失败
- 触发条件：`module_count == 0`（所有 import 的 module 均为 NULL）
- 后果：`descriptor` 从全零缓冲读出，`*out_iat_rva = 0` 静默 → IAT 数据目录指向 0
- 最小修复：`module_count == 0` 时返回错误
- 备注：—

### [高] BackendPe-33｜export name 未判 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:543-547,566,593,616`
- 类别：未检查前置条件
- 触发条件：`compare_export_entries` 与 `strlen(sorted_exports[i].name)` 未判 `name == NULL`
- 后果：崩溃
- 最小修复：入口校验 export name 非 NULL
- 备注：—

### [中] BackendPe-34｜sorted_exports 初始化时机（疑似）
- 位置：`ArkLink/src/Backend/BackendPe.c:659-662`
- 类别：资源与生命周期（疑似）
- 触发条件：`sorted_exports` 在 :561 赋值前未初始化；若 `malloc` 失败 `goto fail` 执行 `free(sorted_exports)`——此时值为 NULL（赋值已发生），安全；但若编译器/优化导致未赋值路径则未定义
- 后果：潜在未定义行为
- 最小修复：显式 `= NULL`
- 备注：疑似（原严重度：中）

### [高] BackendPe-35｜错误码误报为 MEMORY
- 位置：`ArkLink/src/Backend/BackendPe.c:670,801,870`
- 类别：错误处理不一致
- 触发条件：成功路径返回 `ARK_LINK_OK`，失败统一 `ARK_LINK_ERR_MEMORY`，但 :860 `opt_offset + sizeof > total_file_size` 属格式错误而非内存不足
- 后果：错误码误报
- 最小修复：引入 `ARK_LINK_ERR_FORMAT`/`ERR_LAYOUT`
- 备注：—

### [中] BackendPe-36｜段 kind 硬编码枚举
- 位置：`ArkLink/src/Backend/BackendPe.c:982-993`
- 类别：魔法数字 / 语义分叉
- 触发条件：`sec->kind == 1/2/3` 硬编码枚举值（虽与 `ArkSectionKind` 当前值一致），与 `BackendCommon.c:198-215` 的 `classify_section_kind` 是两套映射
- 后果：映射漂移风险
- 最小修复：复用 `classify_section_kind` 或改用枚举常量
- 备注：原编号 37

### [中] BackendPe-37｜reloc_rva 偏移溢出回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:246,279,311`
- 类别：整数截断
- 触发条件：`section_maps[...].rva + reloc->offset`（uint32_t + size_t）赋给 uint32_t `reloc_rva`，偏移溢出时回绕
- 后果：RVA 错误
- 最小修复：用 uint64_t 并检查
- 备注：原编号 38

### [中] BackendPe-38｜file_offset + offset 截断
- 位置：`ArkLink/src/Backend/BackendPe.c:1090`
- 类别：整数截断
- 触发条件：`file_offset + reloc->offset` 赋给 uint32_t
- 后果：偏移错误
- 最小修复：uint64_t
- 备注：原编号 39

### [中] BackendPe-39｜导出 address 加法回绕
- 位置：`ArkLink/src/Backend/BackendPe.c:624-628`
- 类别：整数溢出
- 触发条件：`section_maps[exp->section_index].rva + exp->offset`（uint32_t + size_t）赋给 uint32_t `address`，可回绕
- 后果：地址错误
- 最小修复：uint64_t 并检查
- 备注：原编号 40

### [中] BackendPe-40｜export_count 截断
- 位置：`ArkLink/src/Backend/BackendPe.c:642-643`
- 类别：整数截断
- 触发条件：`(uint32_t)input->export_count`
- 后果：数量回绕
- 最小修复：检查 `<= UINT32_MAX`
- 备注：原编号 41

### [中] BackendPe-41｜align_up 返回值截断
- 位置：`ArkLink/src/Backend/BackendPe.c:831-841`
- 类别：整数截断
- 触发条件：`ark_backend_align_up(...)` 返回 uint64_t 直接 cast uint32_t
- 后果：对齐值错误
- 最小修复：检查
- 备注：原编号 42

### [中] BackendPe-42｜IAT 目录 Size 口径可能少算（疑似）
- 位置：`ArkLink/src/Backend/BackendPe.c:949-951`
- 类别：语义分叉
- 触发条件：`actual_iat_entries = import_iat_total_entries - 1` 假定最后一项是终止符，但 `total_entries = import_count + module_count` 中每个 module 的终止符占一项，实际有效项数应为 `import_count`；减 1 的口径与 IAT 布局不一致
- 后果：数据目录 Size 可能少算（需验证 Windows 加载器口径）
- 最小修复：验证后统一口径
- 备注：疑似（原编号 43，原严重度：中）

### [中] BackendPe-43｜多 module 时全局 IAT RVA 只取第一个（疑似）
- 位置：`ArkLink/src/Backend/BackendPe.c:501`
- 类别：语义分叉
- 触发条件：`memcpy(&descriptor, builder->data + idt_offset, ...)` 取第一个 module 的 IAT RVA 作为全局 IAT RVA；多 module 时其余 module 的 IAT 位置未被数据目录引用（依赖 IAT 连续布局）
- 后果：多 module 下 IAT 引用可能不完整（需验证）
- 最小修复：验证后修正
- 备注：疑似（原编号 44，原严重度：中）

### [中] BackendPe-44｜布局错误返回 MEMORY 且 free 未判 NULL
- 位置：`ArkLink/src/Backend/BackendPe.c:860-872`
- 类别：错误处理不一致
- 触发条件：失败路径返回 `ARK_LINK_ERR_MEMORY`，但 `opt_offset + sizeof(...) > total_file_size` 是布局错误；此处 `free(export_data)` 未判 NULL（`free(NULL)` 安全）
- 后果：错误码误报
- 最小修复：引入布局错误码
- 备注：原编号 45

### [致命] ELF-1｜imports 的 symbol/module 为 NULL 时 strcmp 崩溃
- 位置：`ArkLink/src/Backend/BackendElf.c:533,536`
- 类别：未检查前置条件（NULL 解引用）
- 触发条件：`imports[i].symbol == NULL` 或 `imports[i].module == NULL`；可达路径：`Resolver.c:500-501` 的 `strdup` 返回值未检查（OOM 时字段为 NULL），或任何直接构造 `ArkBackendInput` 的调用方传 NULL
- 后果：`strcmp(seen_names[k], NULL)` → SIGSEGV，链接器崩溃，无诊断
- 最小修复：:532 循环内加 `sym &&` / `mod &&`，与 :540/544 的判空一致；或在 `elf_prepare` 入口拒绝 NULL
- 备注：—

### [致命] ELF-2｜static_loads 上限 48 静默丢段
- 位置：`ArkLink/src/Backend/BackendElf.c:907-909`
- 类别：静默失败 + 死上限
- 触发条件：静态链接（`!has_dynamic && !has_tls`）且 section 数 > 48
- 后果：`static_loads[48]` 写满后直接 `break`，后续 section 的 PT_LOAD 被静默丢弃，镜像缺段 → 加载崩溃/数据错误，无任何诊断
- 最小修复：改为动态数组，或超限返回 `ARK_LINK_ERR_FORMAT` 并打印段索引
- 备注：—

### [致命] ELF-3｜动态段 PLT 标签覆盖既有表项
- 位置：`ArkLink/src/Backend/BackendElf.c:1502-1514`
- 类别：阶段不对齐（动态段标签覆盖既有表项）
- 触发条件：`has_dynamic && func_import_count > 0 && rela_plt_count > 0`（任何带函数导入的动态链接）
- 后果：`dyntab_count`（:1272）未为 JMPREL/PLTRELSZ/PLTGOT/PLTREL 预留槽位，:1503-1514 把 4 个标签写到 `null_idx-5 .. null_idx-2`，覆盖 `DT_FINI_ARRAY`/`DT_FINI_ARRAYSZ`/`DT_RELA`/`DT_RELASZ` → `.rela.dyn` 地址/大小丢失、fini_array 丢失；ELF 能加载但运行结果错误
- 最小修复：`dyntab_count` 增加 4，把 4 个 PLT 标签放进 DT_NULL 前的独立槽位
- 备注：—

### [致命] ELF-4｜e_phnum 少算 2
- 位置：`ArkLink/src/Backend/BackendElf.c:1608-1609`
- 类别：契约破坏
- 触发条件：`has_dynamic`（任何动态链接）
- 后果：实际写入 2×PT_LOAD + PT_INTERP + PT_DYNAMIC + (PT_TLS) + (PT_GNU_RELRO) + PT_GNU_STACK，但 `e_phnum` 只算 `phdr_load_count + tls + relro + stack`，漏掉 `has_dynamic ? 2 : 0`；加载器只读前 5 个 phdr，PT_GNU_RELRO/PT_GNU_STACK 被忽略 → RELRO 失效、栈属性回退默认
- 最小修复：补 `+ (has_dynamic ? 2 : 0)`，并断言 `phdr_write_idx == e_phnum`
- 备注：—

### [致命] ELF-5｜DT_HASH nchain ≠ dynsym 项数
- 位置：`ArkLink/src/Backend/BackendElf.c:1726-1731`
- 类别：阶段不对齐
- 触发条件：`has_dynamic && export_sym_count > 1`
- 后果：规划期 :763 `hash_nchain = 1 + import_count + export_func_count + 1`，写入期 :1726 漏掉导出函数；dynsym 共 `1 + import_count + export_sym_count` 项，nchain 少 `export_sym_count - 1`。ELF 要求 nchain == dynsym 项数，不符时动态链接器越界读 chain 或无法解析导出符号 → 运行时符号查找失败
- 最小修复：写入期沿用规划期公式，并把导出函数纳入 chain
- 备注：—

### [致命] ELF-6｜import_count 乘法溢出 → 堆溢出
- 位置：`ArkLink/src/Backend/BackendElf.c:523`
- 类别：整数溢出 → 堆溢出
- 触发条件：`import_count > SIZE_MAX/2`，`import_count * 2 + 2` 溢出
- 后果：分配极小缓冲 → :542/:546 `seen_names[seen_count++]` 越界写
- 最小修复：calloc 前检查 `import_count <= (SIZE_MAX - 2) / 2`
- 备注：—

### [致命] ELF-7｜elf_shnum 溢出 / 截断
- 位置：`ArkLink/src/Backend/BackendElf.c:552,1573-1574`
- 类别：整数溢出 + 截断
- 触发条件：`ns + rela_sections` 极大
- 后果：`elf_shnum` 溢出 → :689 `calloc(elf_shnum, ...)` 分配不足 → 越界；或 `> 65535` 时 `e_shnum`/`e_shstrndx` 截断 → 段头表错乱
- 最小修复：计算前检查不溢出且 `<= 65535`，否则 `ARK_LINK_ERR_FORMAT`
- 备注：—

### [致命] ELF-8｜段 data NULL / 重定位失败静默跳过
- 位置：`ArkLink/src/Backend/BackendElf.c:676-678,684-685`
- 类别：静默失败 / 诊断缺失
- 触发条件：section `size > 0` 但 `data == NULL`；或重定位应用失败
- 后果：`if (!target->data) continue;` 静默跳过该段全部重定位；`ark_reloc_process_all` 返回 void（`BackendCommon.c:452`），内部 `default: continue`（:496）吞掉未知类型 → 生成错误镜像，无诊断
- 最小修复：`data == NULL && size > 0` 返回 `ARK_LINK_ERR_FORMAT`；`ark_reloc_process_all` 改为返回错误码
- 备注：—

### [致命] ELF-9｜shstrtab 14 个 sb_add 只检查 3 个
- 位置：`ArkLink/src/Backend/BackendElf.c:611-615`
- 类别：静默失败
- 触发条件：shstrtab 追加失败（OOM）
- 后果：:600-609 的 `.interp/.dynsym/.dynstr/.dynamic/.hash/.init_array/.fini_array/.rela.dyn/.plt/.got.plt` 返回 `UINT32_MAX` 时未检查，`sec_name_off` 记成 0xFFFFFFFF → `sh_name` 非法 → 段名解析失败
- 最小修复：抽 `assign_name(idx, s)` 统一检查
- 备注：—

### [致命] ELF-10｜layout 查询返回 NULL 时地址全为 0
- 位置：`ArkLink/src/Backend/BackendElf.c:410-434`
- 类别：静默失败
- 触发条件：`ark_layout_get_section` 返回 NULL
- 后果：`elf_section_vaddr/offset/vsize`、`elf_meta_vaddr/offset/size` 一律返回 0，地址计算与 memcpy 目标全错，生成损坏镜像
- 最小修复：返回错误码或至少 `assert(index < ns)`
- 备注：—

### [高] ELF-11｜多处乘法溢出未检查
- 位置：`ArkLink/src/Backend/BackendElf.c:949,728,747,763-764,775,1125,1467,1510,1702,1710`
- 类别：未检查前置条件（乘法溢出）
- 触发条件：极大 `total_syms` / `relocs_per_sec[i]` / `import_count` / `rela_plt_count`
- 后果：`calloc(n, sizeof(T))` / `memcpy(..., n*sizeof(T))` 前无 `n > SIZE_MAX/sizeof(T)` 检查 → 堆溢出或镜像截断
- 最小修复：统一安全乘法辅助函数；入口对 `total_syms`、`import_count` 设上限
- 备注：—

### [高] ELF-12｜st_shndx 截断
- 位置：`ArkLink/src/Backend/BackendElf.c:957,975,1022,1064-1065`
- 类别：整数截断
- 触发条件：section 数 > 65534，`st_shndx` 为 uint16_t，`(uint16_t)(1 + i)` 回绕
- 后果：符号绑定到错误段
- 最小修复：入口断言 `ns <= 65534`
- 备注：—

### [高] ELF-13｜同一导出符号三种取值口径
- 位置：`ArkLink/src/Backend/BackendElf.c:977,1060,1243`
- 类别：语义分叉
- 触发条件：任何导出符号。`.symtab` 第一阶段用 `section_vaddr + exports[i].offset`（:977），第二阶段同名符号被覆写为 `sec_vaddr + exports[i].value`（:1060），`.dynsym` 又用 `exports[i].offset`（:1243）。`ArkExportEntry` 同时有 `offset`(uint32_t) 和 `value`(uint64_t)；`Resolver.c:554-555` 目前两者同值所以暂时一致，但 `BackendPe.c:625/627` 也是 in-range 用 `.offset`、out-of-range 用 `.value`
- 后果：任一调用方只填一个字段，三处口径就不一致 → 符号地址错误
- 最小修复：统一为单一字段，或入口断言 `offset == value`
- 备注：—

### [高] ELF-14｜未知重定位类型静默当成 64 位绝对重定位
- 位置：`ArkLink/src/Backend/BackendElf.c:1155-1164`
- 类别：静默失败 / 语义分叉（重定位类型映射）
- 触发条件：`r->type` 不在 1..5
- 后果：`default: r_type = R_X86_64_64;` 把未知类型静默当成 64 位绝对重定位；`case 2` 与 `case 5` 都映射 `R_X86_64_32`，语义重复
- 最小修复：`default` 返回 `ARK_LINK_ERR_FORMAT` 并报告 `r->type`；合并 case 2/5
- 备注：—

### [高] ELF-15｜非动态链接下导入重定位静默丢弃
- 位置：`ArkLink/src/Backend/BackendElf.c:1163-1168`
- 类别：静默失败
- 触发条件：`r->symbol->section_index == 0 && import_module != NULL` 且 `!has_dynamic`
- 后果：`continue` 静默丢弃重定位；非动态链接下导入符号本就无法解析，应显式拒绝
- 最小修复：`!has_dynamic` 时返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

### [高] ELF-16｜符号索引线性扫描 + 映射提前释放
- 位置：`ArkLink/src/Backend/BackendElf.c:1137-1145`
- 类别：语义分叉 / 性能
- 触发条件：任何重定位。符号索引靠 `strcmp` 线性扫描 `.symtab` + `strstr`，而 `elf_build_symbols` 已用 `NameIndexMap` 建好映射却在 :1028 提前 `nim_free`。同一语义两处实现
- 后果：性能差、实现分叉
- 最小修复：保留 `sym_map2` 到 `elf_build_relocations`，直接查表
- 备注：—

### [高] ELF-17｜rela_plt_count 死条件 + 静默改值
- 位置：`ArkLink/src/Backend/BackendElf.c:1500-1501,1505-1507`
- 类别：死代码 / 静默降级
- 触发条件：`rela_plt_count > UINT32_MAX / sizeof(Elf64_Rela)`。:1505 的 `rela_plt_count == 0` 被外层 `> 0` 排除，是死条件；:1507 把 `rela_plt_count` 静默改成 `func_import_count`
- 后果：DT_PLTRELSZ 错误
- 最小修复：删死条件；溢出时返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

### [高] ELF-18｜绑定模式不可配置（死分支）
- 位置：`ArkLink/src/Backend/BackendElf.c:1516-1520`
- 类别：死代码 / 配置漂移
- 触发条件：任何动态链接。`int use_lazy_binding = 1;` 局部常量，`if (!use_lazy_binding)` 永不执行，`DT_BIND_NOW` 永不写入，`else {}` 空
- 后果：绑定模式不可配置
- 最小修复：从 `ArkBackendInput` 读标志，或删除死分支并注释固定 lazy
- 备注：—

### [高] ELF-19｜入口段无效时 e_entry 回退镜像起点
- 位置：`ArkLink/src/Backend/BackendElf.c:1558-1562`
- 类别：静默失败
- 触发条件：`entry_section >= ns`
- 后果：`e_entry = image_base`（镜像起点），入口错误 → 崩溃，无诊断
- 最小修复：返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

### [高] ELF-20｜ArkSectionRvaMap 字段 4GB 截断
- 位置：`ArkLink/src/Backend/BackendElf.c:2039-2042`
- 类别：整数截断
- 触发条件：镜像 > 4GB。`ArkSectionRvaMap.rva/size/file_offset` 均为 uint32_t
- 后果：截断 → 调用方拿到错误 RVA
- 最小修复：入口断言镜像 <= 4GB，或字段加宽
- 备注：—

### [高] ELF-21｜kind_to_sh_type 忽略 kind 参数语义
- 位置：`ArkLink/src/Backend/BackendElf.c:223-225,1846`
- 类别：死参数 / 语义分叉
- 触发条件：任何非 BSS section。`kind_to_sh_type(void)` 无参数、恒返回 `SHT_PROGBITS`，与 `kind_to_canonical`/`kind_to_sh_flags` 的 kind 相关语义分叉；函数签名暗示应按 kind 区分
- 后果：section 类型语义分叉
- 最小修复：改为 `kind_to_sh_type(ArkSectionKind)` 或删除该函数直接写常量
- 备注：—

### [高] ELF-22｜DT_ 宏重复定义
- 位置：`ArkLink/src/Backend/BackendElf.c:90-100,137-165`
- 类别：重复宏定义
- 触发条件：编译期。`DT_NULL/DT_NEEDED/DT_STRTAB/DT_SYMTAB/DT_RELA/DT_RELASZ/DT_RELAENT/DT_STRSZ/DT_SYMENT/DT_DEBUG` 定义两次
- 后果：若值不一致会静默采用后者
- 最小修复：删第一组或加 `#ifndef`
- 备注：—

### [高] ELF-23｜sb_add 返回值未检查 / 段名截断
- 位置：`ArkLink/src/Backend/BackendElf.c:617-620,261-270`
- 类别：未检查返回值 / 截断
- 触发条件：`sb_add(&state->strtab, "")` 失败；段名超过 63/79 字节
- 后果：返回值未检查；`snprintf(buf, 64/80, ...)` 静默截断 → 不同 section 得到相同名 → 段名冲突
- 最小修复：检查返回值；截断时报错
- 备注：—

### [中] ELF-24｜PLT/GOT 尺寸不匹配时静默改小 func_import_count
- 位置：`ArkLink/src/Backend/BackendElf.c:1766,1811`
- 类别：静默降级
- 触发条件：`elf_meta_size(plt/gotplt)` 小于预期
- 后果：`func_import_count` 被静默改小 → PLT/GOT 与 dynsym/dyntab 不一致
- 最小修复：规划期与写入期统一公式，或返回错误
- 备注：—

### [中] ELF-25｜未知 section kind 静默跳过
- 位置：`ArkLink/src/Backend/BackendElf.c:884`
- 类别：静默失败
- 触发条件：未知 section kind
- 后果：`continue` 跳过，不生成 PT_LOAD，无诊断
- 最小修复：返回 `ARK_LINK_ERR_FORMAT`
- 备注：—

### [中] ELF-26｜ns==0 时 section_count 未显式赋值
- 位置：`ArkLink/src/Backend/BackendElf.c:2050-2052`
- 类别：契约破坏
- 触发条件：`ns == 0` 时 `section_maps` 不分配、`section_count` 保持 0，但 `output->data` 仍发布
- 后果：输出契约不一致
- 最小修复：显式 `output->section_count = state->ns`
- 备注：—

### [疑似] ELF-疑似-1｜段合并条件可能反向扩展（需验证）
- 位置：`ArkLink/src/Backend/BackendElf.c:893-905`
- 类别：语义分叉（疑似）
- 触发条件：段合并条件 `(c->p_offset + c->p_filesz) >= off || fend <= (c->p_offset + c->p_filesz)`；若 section 的 `file_offset` 非单调，会把前一段向后扩展
- 后果：段范围错误（需验证 `ark_layout_create` 是否保证按 `file_offset` 排序输出）
- 最小修复：验证布局排序保证
- 备注：原文未编号的「疑似」项

### [疑似] ELF-疑似-2｜DT_HASH 单桶 chain（需验证）
- 位置：`ArkLink/src/Backend/BackendElf.c:1726-1731`
- 类别：语义分叉 / 性能（疑似）
- 触发条件：hash chain 值 `(i+1) % hash_nchain`；chain[nchain-1]=0 作为终止符合法，但单桶 O(n) 查找
- 后果：需确认目标动态链接器是否接受单桶 DT_HASH
- 最小修复：验证加载器兼容性
- 备注：原文未编号的「疑似」项

### [高] HDR-1｜符号值 uint32_t 静默截断
- 位置：`ArkLink/include/ArkLink/Resolver.h:28`、`Loader.h:81`
- 类别：语义分叉 / 契约破坏
- 触发条件：`ArkResolverSymbol.value` 是 uint32_t，而 `ArkSymbolDesc.value` 是 uint64_t；`Resolver.c:279` 的 `sym.value = (uint32_t)sym_desc->value;` 静默截断，无诊断、无范围断言（任何 64 位符号值 ≥ 2^32）
- 后果：链接期符号值错误，PE/ELF 重定位写入错误地址，程序崩溃或静默数据损坏
- 最小修复：`ArkResolverSymbol.value` 改 uint64_t；若必须保留 32 位，则截断前显式检查 `> UINT32_MAX` 并返回 `ARK_RES_ERROR_*`
- 备注：—

### [高] HDR-2｜section 指针与 section_index 双字段无不变量
- 位置：`ArkLink/include/ArkLink/Resolver.h:26-31`
- 类别：契约破坏（冗余双字段）
- 触发条件：任何手动构造或修改符号的路径。`ArkResolverSymbol` 同时有 `section`（指针）和 `section_index`（uint32_t），两者无不变量约束、无断言一致
- 后果：某路径只更新其一时产生悬垂/错引 → 节定位不一致 → 重定位写错节
- 最小修复：只保留 `section` 指针（`section_index` 反查或删除），或加 `ARK_ASSERT(section == NULL || section_index == section->index)`
- 备注：—

### [高] HDR-3｜offset+width 32 位回绕绕过边界检查
- 位置：`ArkLink/include/ArkLink/Backend.h:105`
- 类别：未检查前置条件
- 触发条件：`ArkRelocProcessor.offset` 为 uint32_t，而 `BackendCommon.c:386/393/400` 计算 `offset+2/4/8` 做边界检查，32 位回绕可绕过（`offset=0xFFFFFFFE` 时 `offset+2==0`）
- 后果：越界写入重定位目标缓冲区
- 最小修复：用 uint64_t 计算 `offset + width`，或先检查 `offset > UINT32_MAX - width` 再拒绝
- 备注：—

### [高] HDR-4｜ark_reloc_process_all 返回 void 吞掉未知类型
- 位置：`ArkLink/src/Backend/BackendCommon.c:452`
- 类别：静默失败 / 诊断缺失
- 触发条件：遇到 `ARK_RELOC_*` 未实现的动作。返回 void，内部 `default: continue`（:496）吞掉未知重定位类型，不报类型值、不报偏移、不返回错误码
- 后果：重定位静默丢失 → 生成可执行文件行为错误，无任何线索
- 最小修复：返回 `ArkLinkResult`，未知类型返回 `ARK_LINK_ERROR_UNSUPPORTED_RELOC` 并附 type/offset
- 备注：—

### [高] HDR-5｜ark_reloc_apply_elf default 静默忽略
- 位置：`ArkLink/src/Backend/BackendCommon.c:385`
- 类别：静默失败
- 触发条件：未知 action
- 后果：`default: return;` 静默忽略，不写值、不报错
- 最小修复：同 HDR-4
- 备注：—

### [高] HDR-6｜符号值解析链阶段耦合
- 位置：`ArkLink/src/Backend/BackendCommon.c:346-348`
- 类别：阶段不对齐 / 语义分叉
- 触发条件：重定位符号值取值链 `symbol_address_resolved ? symbol_address : (symbol_rva ? symbol->value : 0)`；`symbol_address`（64 位）与 `symbol_rva`（32 位）分属不同阶段（链接布局 vs 对象解析），优先级隐式耦合在 `elf_plan_layout` 是否设置 `symbol_address_resolved` 上（任何路径未设 `symbol_address_resolved` 但设了 `symbol_rva`）
- 后果：ELF 与 PE 后端可能取到不同符号值口径
- 最小修复：把符号值解析集中到唯一函数（如 `ark_reloc_resolve_symbol_value`），两端共用
- 备注：—

### [中] HDR-7｜PE 后端忽略 input 配置字段
- 位置：`ArkLink/include/ArkLink/Backend.h:170-172`、`ArkLink/src/Backend/BackendPe.c:925/928-931`
- 类别：配置漂移
- 触发条件：`ArkBackendInput` 声明 `stack_size`/`subsystem`/`output_type`，但 PE 后端硬编码值，配置字段被无视
- 后果：用户配置无效，行为不可预测
- 最小修复：PE 后端读取这些字段，缺失时用默认并报 warning
- 备注：与 BackendPe-28 同源

### [中] HDR-8｜internal 别名判定基于 size 而非 capacity
- 位置：`ArkLink/src/Backend/BackendCommon.c:112-126`
- 类别：语义分叉
- 触发条件：`internal` 别名判定基于 `size`，但别名冲突实际取决于 `capacity`（写入可能越界到 capacity）
- 后果：判为 internal 的段可能重叠
- 最小修复：判定覆盖 `size..capacity`
- 备注：—

### [致命] B5-1｜build_project_independent 完全忽略 output_path
- 位置：`Re.KrtC/src/Main.c:909-914`
- 类别：配置漂移 / 静默失败
- 触发条件：`krt build project.krt -o /path/to/foo`（或 `build_project` 传入任何 `-o`）；参数被 `__attribute__((unused))` 显式丢弃，函数体内从未引用
- 后果：用户指定的输出路径被静默忽略，产物固定落到 `<root>/bin/<platform>/<output_name>`；`-o` 形同虚设，且无任何警告
- 最小修复：把 `output_path` 传给 `ParallelCompilerLinkResults`；为空时再走默认 `bin/<platform>/<name>` 逻辑
- 备注：—

### [致命] B5-2｜目标文件缺失只报错不中止
- 位置：`Re.KrtC/src/Main.c:856-860`
- 类别：静默失败 / 契约破坏
- 触发条件：`obj_idx != success_count`（某 .kro 未生成或 `KrtPlatformPathExists` 失败）
- 后果：只 `KrtError(...)` 后不 return，继续执行 `link_result = obj_idx == success_count ? 0 : -1;`，把「文件缺失」降级成「链接失败」，最终只报 `Linking failed`（:900）；原始「哪个文件缺失」信息被覆盖
- 最小修复：`obj_idx != success_count` 时立即释放资源并 `return 1`
- 备注：—

### [致命] B5-3｜WriteByte 的 KRT_REALLOC 失败未检查
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:120-121`
- 类别：未检查前置条件 / 静默失败
- 触发条件：`realloc` 返回 NULL（OOM）
- 后果：`chunk->code`/`chunk->lines` 被赋 NULL，旧指针丢失（内存泄漏），随后 `chunk->code[chunk->count] = byte` 解引用 NULL → 崩溃。`AddConstant`（:138）同
- 最小修复：`void* p = realloc(...); if (!p) { /* 报告并终止/返回错误 */ } chunk->code = p;`
- 备注：G1 前提修正后结论不变（此处 `KRT_REALLOC` 为 `realloc` 宏，失败返回 NULL）

### [致命] B5-4｜chunk->count+1 / capacity*2 整数溢出
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:124-126`
- 类别：未检查前置条件
- 触发条件：`count` 接近 `INT_MAX`
- 后果：`capacity < count+1` 为假 → 越界写入
- 最小修复：改用 `size_t` 或检查 `count > INT_MAX - 1`
- 备注：—

### [高] B5-5｜get_kro_filename 盲目替换末 4 字符且不校验扩展名
- 位置：`Re.KrtC/src/Main.c:551-558`
- 类别：未检查前置条件 / 静默失败
- 触发条件：传入 `foo`（len=3）→ 不替换，输出仍 `foo`；传入 `a.bcd` → 变 `a.kro`；传入长度接近 `kro_file_size` 的路径 → `strcpy(kro_file+len-4, ".kro")` 写 5 字节可能越界
- 后果：路径错乱或缓冲区溢出；`link_kro_executable`（:269）用它生成 import 的 .kro 名，错误名会导致链接缺文件
- 最小修复：校验 `len >= 4 && strcmp(krt_file+len-4, ".krt")==0`，否则报错；并保证 `kro_file_size >= len+1`
- 备注：—

### [高] B5-6｜import_kro_capacity 翻倍无溢出检查
- 位置：`Re.KrtC/src/Main.c:279`
- 类别：未检查前置条件
- 触发条件：import 数量使容量翻倍超过 `INT_MAX`
- 后果：容量变负 → `realloc` 尺寸异常/失败，`imports_ok=0`，依赖被静默丢弃，链接出残缺可执行文件
- 最小修复：翻倍前检查 `import_kro_capacity > INT_MAX/2`
- 备注：—

### [高] B5-7｜链接失败只报「Linking failed」
- 位置：`Re.KrtC/src/Main.c:300-306,900`
- 类别：诊断缺失
- 触发条件：任何链接失败
- 后果：丢弃 `KrtArkLinkLinkObjects` 返回的具体错误码/原因，用户无法定位
- 最小修复：打印返回码及 ArkLink 上下文中记录的错误串
- 备注：—

### [高] B5-8｜错误信息与实际不符
- 位置：`Re.KrtC/src/Main.c:1050-1055`
- 类别：诊断缺失（诊断与实际错误不符）
- 触发条件：`compile_result != 0` 但 `failed_files == 0`
- 后果：报「0 个文件编译失败」，与实际（并行编译整体失败）矛盾
- 最小修复：区分 `compile_result` 与 `failed_files`，分别报告
- 备注：—

### [高] B5-9｜中间路径缓冲区静默截断
- 位置：`Re.KrtC/src/Main.c:990-995`
- 类别：静默失败
- 触发条件：`obj_dir` 或 `file_name_no_ext` 长度 ≥ 500
- 后果：中间 .kro 路径被截断，写到错误位置，且无诊断
- 最小修复：截断后检查 `strlen(src) >= sizeof(dst)` 并报错
- 备注：—

### [高] B5-10｜两处 snprintf 未检查返回值
- 位置：`Re.KrtC/src/Main.c:1135-1140`
- 类别：未检查前置条件
- 触发条件：`object_dir` 过长
- 后果：`aggregate_path`/`aggregate_kro` 被截断，后续 `fopen` 失败仅报「无法创建合并项目源码」，不报路径被截断
- 最小修复：检查 `snprintf` 返回值 ≥ sizeof 时报错
- 备注：—

### [高] B5-11｜WriteShort 写死行号 0
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:130-131`
- 类别：注释与实现脱节 / 语义分叉
- 触发条件：所有 short 常量写入
- 后果：字节码行号信息丢失，调试/栈回溯定位到错误行
- 最小修复：`WriteShort` 接收 `line` 参数并透传
- 备注：—

### [高] B5-12｜SerializeToFile 写失败仍返回 true
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:159-203`
- 类别：静默失败
- 触发条件：磁盘满、`fwrite` 短写、`fclose` 失败
- 后果：函数返回 `true`，调用方认为序列化成功；文件内容残缺
- 最小修复：检查每个 `fwrite` 返回值与 `fclose`，失败返回 `false` 并报错
- 备注：—

### [高] B5-13｜字符串常量长度截断为 uint16_t
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:191`
- 类别：未检查前置条件
- 触发条件：字符串常量长度 > 65535
- 后果：长度字段被截断，反序列化读越界/错乱
- 最小修复：检查 `strlen > UINT16_MAX` 并报错
- 备注：—

### [高] B5-14｜VAL_OBJ 分支不写任何数据
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:196-198`
- 类别：契约破坏 / 静默失败
- 触发条件：常量池含 `VAL_OBJ`
- 后果：文件只写 `type` 字节，反序列化时按类型读后续字节会错位；且无诊断
- 最小修复：序列化时对 `VAL_OBJ` 报错或实现其编码
- 备注：—

### [高] B5-15｜KrtBytecodeGeneratorDestroy 不检查 NULL
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:102-104`
- 类别：未检查前置条件
- 触发条件：传入 NULL
- 后果：`free(NULL)` 本身安全，但与 `KrtBuildContextDestroy`（:21 有 NULL 检查）风格不一致；若将来改为非 free 释放会崩
- 最小修复：统一 `if (!generator) return;`
- 备注：—

### [高] B5-16｜KrtBuildContextCreate 的 NULL 检查为活路径而非死代码（G1 修订）
- 位置：`Re.KrtC/src/Build/BuildSystem.c:7-10`
- 类别：静默失败 / 未检查前置条件（OOM 时 NULL 传播，零诊断）
- 触发条件：`KRT_MALLOC` 失败（KrtSafeMalloc 失败返回 NULL，不 exit）
- 后果：`if (!ctx) return NULL;` 是活路径，静默向上传播 NULL；`KrtMemoryReportError` 为空操作 → 零诊断。调用方 `Main.c:473` 按「可能返回 NULL」处理，与旧「失败即终止」假设形成双重标准
- 最小修复：保留 NULL 检查并补充 OOM 诊断；或统一分配器语义与文档
- 备注：【修订 G1】原文判为「死代码」基于「KRT_MALLOC 失败即 exit(1)」的错误前提；实际 `Core/Memory/Allocator.c:159-211` 失败返回 NULL、`KrtCommon.h:77-81` 宏无守卫。严重度按后果（静默失败 / 零诊断）重定为高

### [高] B5-17｜链接失败丢弃 ArkLink 错误码
- 位置：`Re.KrtC/src/Build/BuildSystem.c:62-67`
- 类别：诊断缺失
- 触发条件：`KrtArkLinkLink` 返回非 0
- 后果：只写 `"ArkLink link failed"`，具体原因丢失
- 最小修复：把 ArkLink 结果码/错误串写入 `ctx->error_message`
- 备注：—

### [中] B5-18｜未知项目类型静默返回 "console"
- 位置：`Re.KrtC/src/Main.c:48-60`
- 类别：静默失败
- 触发条件：`new <未知类型> <名>` 且未提供项目名
- 后果：用户笔误类型被吞掉，项目名变成 "console"
- 最小修复：未知类型报错退出
- 备注：—

### [中] B5-19｜get_kro_filename 前向声明处未注释契约
- 位置：`Re.KrtC/src/Main.c:246`（与 :551 之间）
- 类别：契约破坏（轻微）
- 触发条件：调用方不知 `kro_file_size` 下界要求
- 后果：易误用
- 最小修复：声明处补 `@param kro_file_size` 下界要求
- 备注：—

### [中] B5-20｜模板创建失败时部分产物未清理
- 位置：`Re.KrtC/src/Main.c:1376`
- 类别：资源与生命周期
- 触发条件：`KrtProjCreateTemplate` 中途失败
- 后果：残留空目录/文件，无回滚
- 最小修复：失败时删除已创建项
- 备注：—

### [中] B5-21｜KRT_REALLOC 宏无括号保护
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:88`
- 类别：未检查前置条件
- 触发条件：传入带副作用或复杂表达式
- 后果：宏展开多次求值
- 最小修复：加括号 `(ptr)`/`(size)`，或改 `static inline`
- 备注：—

### [中] B5-22｜容量检查逻辑重复实现
- 位置：`Re.KrtC/src/Shared/BytecodeGenerator.c:117`
- 类别：语义分叉
- 触发条件：`chunk->capacity < chunk->count + 1` 与 `AddConstant` 的 `capacity < count + 1` 重复实现
- 后果：逻辑分叉风险
- 最小修复：抽成唯一 `chunk_reserve` 函数
- 备注：—

### [致命] B6-1｜BSS 重定位从不写入文件且 total_reloc_count 漏算 bss
- 位置：`Re.KrtC/src/Shared/KroWriter.c:537-557`
- 类别：静默失败 / 契约破坏
- 触发条件：任何写入 `KRO_SEC_BSS` 重定位的代码路径（`kro_add_reloc` 的 `case KRO_SEC_BSS`）
- 后果：`writer->bss_relocs` 被填充但 `kro_write_file` 只序列化 text/rodata/data 三组；`header.total_reloc_count`（:485）也不含 bss。BSS 符号地址在链接期全部丢失 → 链接产物行为错误或崩溃
- 最小修复：加 `header.bss_reloc_count` 并写入 `writer->bss_relocs`；`total_reloc_count` 计入 bss
- 备注：—

### [致命] B6-2｜写文件失败路径静默且 fclose 未检查
- 位置：`Re.KrtC/src/Shared/KroWriter.c:476-567`
- 类别：静默失败
- 触发条件：磁盘满/IO 错误。`fwrite` 失败时 `fclose` 可能仍缓冲未刷数据；:566 成功路径的 `fclose` 返回值被忽略
- 后果：调用方（`KrtCompilePipelineExecute`）拿到 `true` 但文件残缺，后续链接基于坏 .kro
- 最小修复：检查 `fclose` 返回值；失败时删除半成品文件
- 备注：—

### [致命] B6-3｜kro_add_reloc 丢弃 add_reloc_to_list 的失败
- 位置：`Re.KrtC/src/Shared/KroWriter.c:346`
- 类别：静默失败 / 错误处理不一致
- 触发条件：`realloc` 失败（OOM）
- 后果：重定位被静默丢弃，函数返回 void，无任何诊断
- 最小修复：`kro_add_reloc` 改返回 `bool`，调用方检查并中止
- 备注：—

### [致命] B6-4｜容量翻倍无溢出检查
- 位置：`Re.KrtC/src/Shared/KroWriter.c:236,289,361,154`
- 类别：未检查前置条件
- 触发条件：`add_string`（:236 `while (new_capacity < new_size) new_capacity *= 2;`）、`kro_add_symbol`/`kro_add_import_symbol`/`add_reloc_to_list`（`* 2`）、`ensure_capacity`
- 后果：`uint32_t` 回绕 → `realloc` 小缓冲 → `memcpy` 堆溢出；`add_string` 的 while 可能死循环
- 最小修复：每次翻倍前检查 `> UINT32_MAX/2`，改为 `new_capacity = required`
- 备注：—

### [致命] B6-5｜count * element_size 乘法溢出
- 位置：`Re.KrtC/src/Shared/SmartPtr.c:59,97`
- 类别：未检查前置条件
- 触发条件：`KrtMakeSharedArray(type, count, elem)` 且 `count*elem` 溢出 `size_t`
- 后果：分配极小缓冲，数组写入越界 → 堆损坏
- 最小修复：`if (count != 0 && element_size > SIZE_MAX / count) return NULL;`
- 备注：—

### [致命] B6-6｜KrtArkLinkAddObjectFile 返回值被忽略
- 位置：`Re.KrtC/src/Shared/ArkLinkIntegration.c:286`
- 类别：静默失败 / 契约破坏
- 触发条件：`KrtArkLinkLinkObjects` 中添加任一 obj 失败（session_add_input 失败、strdup 失败、realloc 失败）
- 后果：链接继续进行，缺文件 → 链接失败或生成缺符号的可执行文件；无诊断
- 最小修复：检查返回值，失败立即销毁 ctx 并返回 -1
- 备注：—

### [致命] B6-7｜add_input 失败时已 strdup 的槽位泄漏
- 位置：`Re.KrtC/src/Shared/ArkLinkIntegration.c:123-127`
- 类别：资源与生命周期
- 触发条件：`arklink_session_add_input` 返回非 OK
- 后果：`ctx->obj_files[obj_count]` 已分配但 `obj_count` 未增；`KrtArkLinkContextDestroy` 只释放 `i < obj_count` → 泄漏
- 最小修复：失败时 `KRT_FREE(ctx->obj_files[obj_count])` 并置 NULL
- 备注：—
