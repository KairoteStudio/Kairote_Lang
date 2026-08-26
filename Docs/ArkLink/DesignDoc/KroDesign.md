# KRO (Kairote Object) 文件格式规范

> **版本 2**(规范修订)。v1 的文件/内存段序冲突、`.bss relocs` 悖论、`.rdata` 对齐缺失、
> `entry_point` 链接后语义未定义四项设计错误在本版修正。变更清单见文末「版本历史」。

## 设计哲学

- **最小充分性**：仅保留链接器工作的最小信息集（段、符号、重定位）
- **零历史包袱**：去除 DOS stub、Rich header、COFF 复杂存储类等 30 年技术债
- **专注自托管**：专为 Kairote 编译器设计，不追求通用 COFF 兼容性
- **C 友好**：`#pragma pack(push, 1)` 紧凑布局，无对齐填充，支持 `mmap` 零拷贝解析
- **段序一致**：文件内段顺序与内存段顺序严格一致（.text → .rdata → .data → .bss），
  使权限相同的区域在文件与内存中连续，mmap 直接映射即可生效页保护

---

## 文件布局（线性，无填充）

**规范（v2）**：数据节按 `.text → .rdata → .data` 排列，与内存布局同序；
`.bss` 不占文件空间；重定位仅按**引用方**节组织（不存在 `.bss relocs`，见「重定位」）。

```
Offset 0x00:  KROHeader        (64 bytes)
Offset 0x40:  .text data       (text_size bytes)
Offset 0x40 + text_size:       .rdata data (rodata_size bytes)
Offset 0x40 + text + rodata:   .data data (data_size bytes)
Offset 0x40 + text + rodata + data:  Symbol Table (sym_count × 32 bytes)
Offset ...:    .text  relocs    (text_reloc_count  × 16 bytes)
Offset ...:    .rdata relocs    (rodata_reloc_count × 16 bytes)
Offset ...:    .data  relocs    (data_reloc_count  × 16 bytes)
Offset ...:    String Table    (strtab_size bytes)
```

**偏移量计算公式**（链接器使用）：
```
text_data    = 0x40
rodata_data  = text_data    + text_size
data_data    = rodata_data  + rodata_size
sym_table    = data_data    + data_size
text_relocs  = sym_table    + sym_count × 32
rodata_relocs= text_relocs  + text_reloc_count × 16
data_relocs  = rodata_relocs+ rodata_reloc_count × 16
strtab       = data_relocs  + data_reloc_count × 16
```

---

## 核心数据结构

所有结构体使用 `#pragma pack(push, 1)`，字段紧密排列，无对齐填充。

### 1. 文件头（64 字节）

```c
typedef struct {
    uint32_t magic;              // 0x004F524B ('KRO\0')
    uint32_t version;            // 格式版本（当前为 2）
    uint32_t flags;              // bit0: PIC, bit1: 包含 LTO 字节码
    uint32_t entry_point;        // 见下方「入口点语义」

    uint32_t text_size;          // .text 数据字节数
    uint32_t rodata_size;        // .rdata 数据字节数
    uint32_t data_size;          // .data 数据字节数
    uint32_t bss_size;           // BSS 内存字节数（文件中不占空间）

    uint32_t text_reloc_count;   // .text  重定位条目数（引用方）
    uint32_t rodata_reloc_count; // .rdata 重定位条目数（引用方）
    uint32_t data_reloc_count;   // .data  重定位条目数（引用方）
    uint32_t bss_align;          // BSS 起始地址对齐（字节，通常为 8）

    uint32_t sym_count;          // 符号表条目数
    uint32_t strtab_size;        // 字符串表字节数
    uint32_t total_reloc_count;  // 重定位总数（= text + rodata + data）
    uint32_t reserved;           // 保留（必须为 0）
} KROHeader;
// 16 × 4 = 64 bytes ✓
```

**字段验证**：
- `magic` 必须为 `0x004F524B`
- `total_reloc_count` 必须等于 `text_reloc_count + rodata_reloc_count + data_reloc_count`
- `version` 当前必须为 2
- `bss_align` 必须为 0 或 2 的幂；为 0 时按 8 处理

### 入口点语义（v2 明确）

| 阶段 | `entry_point` 含义 |
|------|--------------------|
| 编译器产出（未链接） | 入口代码在**本文件 .text 内的字节偏移**；无入口时为 0 |
| 链接器输入 | 同上；链接器收集各输入的该偏移与符号表共同决定最终入口 |
| 链接器输出（可执行） | 改写为**最终镜像中的绝对虚拟地址**，直接装入 ELF `e_entry` |

链接器解析规则：取所有输入中最大的非零 `entry_point` 所在对象为主对象；
若全为 0，则在合并符号表中查找 `main` 并以其地址作为入口；两者皆缺 → 报错
"No entry point"。**禁止**把相对偏移原样写入 `e_entry`。

### 2. 符号表（32 字节/条目）

```c
typedef struct {
    uint32_t name_offset;        // 符号名在字符串表中的偏移
    uint32_t value;              // 段内偏移或绝对值
    uint32_t size;               // 符号大小（字节）
    uint32_t section;            // 段索引（KRO_SEC_*）
    uint32_t binding;            // KRO_BIND_LOCAL/GLOBAL/WEAK
    uint32_t type;               // KRO_SYM_NOTYPE/FUNC/OBJECT
    uint32_t flags;              // 符号标志
    uint32_t reserved;           // 保留
} KROSymbol;
// 8 × 4 = 32 bytes ✓
```

**段索引**：
| 值 | 含义 |
|----|------|
| 0  | 未定义/外部符号 |
| 1  | KRO_SEC_TEXT |
| 2  | KRO_SEC_RODATA |
| 3  | KRO_SEC_DATA |
| 4  | KRO_SEC_BSS |

### 3. 重定位条目（16 字节/条目）

```c
typedef struct {
    uint32_t offset;             // 需修补的位置（引用方节内的偏移）
    uint32_t sym_idx;            // 引用符号的索引（可为任意节的符号，含 BSS）
    uint32_t type;               // 重定位类型（KRO_RELOC_*）
    int32_t  addend;             // 加数（有符号）
} KROReloc;
// 4 × 4 = 16 bytes ✓
```

**重定位类型**：
| 编码 | 名称 | 说明 | 用途 |
|------|------|------|------|
| 1 | KRO_RELOC_ABS64 | 64 位绝对地址 | 数据段中的全局指针 |
| 2 | KRO_RELOC_ADDR32 | 32 位绝对地址 | 数据段中的 32 位指针 |
| 3 | KRO_RELOC_PC32 | RIP 相对 32 位有符号 | `call/jmp` 指令 |

---

## 内存布局

### 段顺序（虚拟地址空间）

与文件布局**严格同序**——这是 mmap 零拷贝成立的前提：

```
低地址
  .text   (RX)   - 代码
  .rdata  (RO)   - 只读数据（字符串字面量等）
  .data   (RW)   - 可读写数据
  .bss    (RW)   - 未初始化数据（零填充，紧随 .data 之后）
高地址
```

三段权限单调递变 RX → RO → RW，无权限交叠区，操作系统可将同权限页合并映射；
链接器产出的 ELF PT_LOAD 边界必须落在页边界上并与上述顺序一致。

### 对齐要求
- `.text`  起始地址 16 字节对齐（指令对齐）
- `.rdata` 起始地址 8 字节对齐（v2 补充；字符串/指针对象最低要求）
- `.data`  起始地址 8 字节对齐
- `.bss`   起始地址 `bss_align` 对齐（默认 8 字节）
- 链接器负责在段间插入 padding 以满足对齐；页级对齐由后端按目标平台页大小再向上取整

### 文件偏移 vs 虚拟地址
```
文件偏移：紧凑排列，无填充（顺序即上文文件布局）
虚拟地址：链接器根据对齐要求计算，可能包含 padding；但**相对顺序不变**

转换公式：
  file_offset(section) = 见上文偏移计算公式
  vaddr(section)       = align_up(prev_loaded_end, section_alignment)
约束（mmap 友好）：vaddr ≡ file_offset (mod page_size)
```

---

## BSS 段特殊处理

BSS 在文件中不占空间（`file_size = 0`），加载/链接时由零页填充。

**为什么没有 `.bss relocs`（v2 移除）**：BSS 加载后内容全为 0，不含任何指令或
已初始化指针，因此**BSS 内部不存在需要修补的位置**。其他节引用 BSS 符号时，
重定位记录登记在**引用方**（`.text`/`.rdata`/`.data`）的重定位表中，
`sym_idx` 指向 BSS 符号即可。v1 的 `bss_reloc_count` 是语义悖论，已删除。

**链接器处理**：
1. 读取 `bss_size` 与 `bss_align`
2. 在 `.data` 之后分配 `bss_size` 字节的零填充区域（并入 RW 段 memsz）
3. BSS 符号的 `value` 是相对于 BSS 段起始的偏移
4. 引用方重定位照常解析到 BSS 符号的最终虚拟地址

**注意**：BSS 没有 `file_offset`（文件中无数据），位置由 `.data` 结束地址对齐后推断。

---

## 字符串表

- 所有符号名以 C 字符串（`\0` 结尾）连续存储
- 第一个字节为 `\0`（空字符串，索引 0 表示"无名"）
- `name_offset = 0` 表示符号无名
- 长符号（>31 字符）完整存储在字符串表中
- 短符号也存储在字符串表中（简化实现，避免内联/外联两种路径）

---

## 重定位应用

链接器按以下顺序应用重定位（**引用方**顺序；v2 不再有 `.bss relocs`）：

1. 解析所有符号，确定最终虚拟地址
2. 按节顺序处理重定位：
   - `.text`  relocs → 修补代码段
   - `.rdata` relocs → 修补只读数据段
   - `.data`  relocs → 修补数据段
3. 对于每个重定位条目：
   - `patch_addr = vaddr(referencing_section) + offset`
   - `sym_addr = vaddr(symbol_section) + symbol_value`（symbol_section 可为 BSS）
   - 根据 `type` 计算最终值并写入 `patch_addr`

**重定位公式**：
```
KRO_RELOC_ABS64:  *(uint64_t*)patch_addr = sym_addr + addend
KRO_RELOC_ADDR32: *(uint32_t*)patch_addr = (uint32_t)(sym_addr + addend)
KRO_RELOC_PC32:   *(uint32_t*)patch_addr = (uint32_t)(sym_addr - patch_addr + addend - 4)
```

---

## 校验规则

链接器解析时应验证：
1. `magic == 0x004F524B`
2. `version == 2`（v1 已废弃；如需读取旧文件应走独立转换工具）
3. `total_reloc_count == text_reloc_count + rodata_reloc_count + data_reloc_count`
4. 所有偏移量不超出文件大小
5. 所有 `sym_idx < sym_count`
6. 所有 `name_offset < strtab_size`
7. `bss_align` 为 0 或 2 的幂

---

## 与 COFF 对比

| 维度 | COFF/PE | KRO |
|------|---------|-----|
| 文件头 | 多层嵌套（DOS头+PE头+COFF头+可选头≈500字节） | 单一 64 字节固定头 |
| 段处理 | 可变数量，复杂属性解析 | 固定 4 段，直接索引 |
| 段序 | 文件/内存各自规则 | 文件=内存，权限单调 RX→RO→RW |
| 符号解析 | 字符串表+长度前缀+辅助符号记录 | 固定 32 字节条目 |
| 重定位归属 | 按目标节混杂 | 仅按引用方节组织，无 BSS relocs |
| 内存占用 | 需多次 malloc 和字符串拷贝 | mmap 零拷贝，Arena 批量释放 |
| 链接速度 | 通用链接器需处理所有历史特性 | ArkLink 专注 Kairote 产物，O(N) 符号解析 |
| 工具链依赖 | 需要完整 MSVC/MinGW 工具链 | ArkLink 单文件可执行（<500KB） |

---

## 实现一致性要求（v2 新增）

两端实现必须与本规范同步：

| 组件 | 职责 |
|------|------|
| KrtC `Tools/KroWriter.c` | 按 `.text → .rdata → .data` 顺序写文件；`version=2`；`bss_align=8`；不产生 bss relocs |
| ArkLink `Loader/LoaderKro.c` | 按 v2 偏移公式解析；校验 `version==2`；不再读取 bss relocs |
| ArkLink `Backend/*` | 输出 ELF 的 PT_LOAD 边界按 RX/RO/RW 权限分组、页对齐、与 KRO 段序一致 |

---

## 版本历史

| 版本 | 变更 |
|------|------|
| 1 | 初版。存在四项设计缺陷：文件段序(.text→.data→.rdata)与内存段序冲突破坏 mmap 零拷贝与页保护；保留无语义的 `.bss relocs`；遗漏 `.rdata` 对齐；`entry_point` 链接后语义未定义 |
| 2 | 文件段序改为 .text→.rdata→.data 与内存一致；删除 `bss_reloc_count`，字段复用为 `bss_align`；补 `.rdata` 8 字节对齐；明确 `entry_point` 三阶段语义（未链接偏移 / 主对象选取 / 输出绝对 VA）；新增实现一致性要求 |

---

**底线**：KRO 是链接器输入格式，而非可执行格式。它牺牲通用性换取极致的解析速度和实现简洁性，配合 ArkLink 实现从 Kairote 源码到可执行文件的闭环。
