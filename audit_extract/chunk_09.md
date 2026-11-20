# Chunk 09 抽取

## 覆盖范围
- 第五十七批：Core/Memory — Allocator.c(475) + Arena.c(250) + SmartPtr.c(316)（「高」「中」部分，原文第 5929–5999 行）

## 全局修订
- G1（前提修正）：`KRT_MALLOC`/`KRT_CALLOC`/`KRT_REALLOC`/`KRT_STRDUP` 失败**不 exit**，而是返回 NULL；`KrtMemoryReportError` 为空操作。所有分配失败路径都是活路径且零诊断（详见 chunk_08 的修订段）。

## 问题条目

### [高] S57-3｜死代码（整模块零调用者）
- 位置：`Core/Memory/SmartPtr.c` 全 316 行 + `SmartPtr.h`
- 类别：死代码
- 触发条件：任何调用（当前零调用者）
- 后果：约 316 行死代码，内含 S57-4~S57-8 多个未修复缺陷，一旦启用即炸
- 最小修复：删除整个模块，或补测试后启用

### [高] S57-4｜契约破坏（cleanup 属性签名不匹配，潜伏致命）
- 位置：`SmartPtr.h:62` vs `SmartPtr.c:221`
- 类别：契约破坏
- 触发条件：任何 `SmartPtr* p __attribute__((cleanup(KrtPtrDestroy)))` 变量离开作用域
- 后果：GCC cleanup 传 `SmartPtr**`，而函数签名是 `SmartPtr*` → 函数内 `ptr->type` 读到栈垃圾 → 可能以垃圾 type 调用 `KrtSharedRelease` → 对垃圾地址 `atomic_fetch_sub` → SEGV；`KRT_FREE(ptr)` 释放栈地址（静默 no-op，泄漏）
- 最小修复：改 `void KrtPtrDestroy(SmartPtr** ptr)`，或加 wrapper
- 备注：当前零调用者 → 定级高（潜伏）

### [高] S57-5｜未检查前置条件（整数溢出）
- 位置：`SmartPtr.c:54-64`、`SmartPtr.c:93-98`
- 类别：未检查前置条件
- 触发条件：`count * element_size` 溢出（如 `count=SIZE_MAX/4, element_size=8`）
- 后果：`KrtMakeShared(type_name, count*element_size)` 得到过小分配，调用方按 `count*element_size` 写入 → 堆缓冲区溢出
- 最小修复：用 `KrtSizeMultiply`（`Core/Utils/Size.h:18-24`）检查，溢出返回 NULL

### [高] S57-6｜语义分叉 / 契约破坏
- 位置：`SmartPtr.c:186-188` + `SmartPtr.h:60`
- 类别：语义分叉 / 契约破坏
- 触发条件：对 `PTR_WEAK` 指针调用 `KRT_PTR_VALID`/`KrtPtrGet`
- 后果：`KrtPtrIsNull` 含 `ptr->ptr == NULL`，而 weak 指针的 `ptr` 恒为 NULL（`SmartPtr.c:252`）→ 任何有效 weak 指针都被判 invalid；`KrtPtrGet`（:144-149）对活着的 weak 返回 NULL，而 `KrtWeakLock` 能返回有效 shared → 同一语义两种结果
- 最小修复：`KrtPtrIsNull` 改为 `!ptr || !KrtPtrIsValid(ptr)`，或 `KrtPtrGet` 对 `PTR_WEAK` 走 `KrtWeakLock`

### [高] S57-7｜静默失败 / 契约破坏
- 位置：`SmartPtr.c:229-240`
- 类别：静默失败 / 契约破坏
- 触发条件：传入非 `KRT_MALLOC` 分配的 `new_ptr`；或 `new_ptr == NULL`
- 后果：`ptr_cleanup_internal` 后直接 `ptr->ptr = new_ptr; ptr->type = PTR_UNIQUE`，不检查来源；之后 `KrtPtrDestroy` → `KRT_FREE(new_ptr)` → 未跟踪 → 静默 no-op 泄漏（叠加 S57-2）；`size` 形参被 `__attribute__((unused))` 丢弃
- 最小修复：文档化「`new_ptr` 必须来自 `KRT_MALLOC`」，或改签名为 `KrtPtrReset(SmartPtr*, SmartPtr*)`

### [高] S57-8｜死代码（Arena 大半）
- 位置：`Arena.c:17-32`（`KrtArenaCreate` 互斥版）、`:114-124`（`KrtArenaCalloc`）、`:126-136`（`KrtArenaStrdup`）、`:171-189`（`KrtArenaReset`）、`:191-219`（`KrtArenaStats`）、`:221-231`（`KrtArenaGetUsage`）、`:233-250`（`KrtArenaGetBlockCount`）
- 类别：死代码
- 触发条件：任何调用（当前外部零调用者）
- 后果：约半个模块死代码
- 最小修复：删除（仅 `KrtArenaAlloc`/`KrtArenaCreateLocal`/`KrtArenaDestroy`/`KrtArenaRealloc` 有调用）

### [高] S57-9｜死代码（11 个声明无定义）
- 位置：`Allocator.h:68,69,78,81,82,93,94,95,123,124,125`
- 类别：死代码 / 契约破坏
- 触发条件：任何调用
- 后果：`KrtMemoryProtect`/`KrtMemoryUnprotect`/`KrtPtrIsFreed`/`KrtPtrIsDangling`/`KrtPtrTrackFree`/`KrtMemoryValidateHeap`/`KrtMemoryCheckIntegrity`/`KrtMemoryScanCorruption`/`KrtMemorySafetyEnablePoison`/`KrtMemorySafetyEnableCanary`/`KrtMemorySafetySetStrictMode` 全项目 0 定义 0 调用；声明暗示存在内存保护/完整性校验/严格模式，实际不存在，调用即链接错误
- 最小修复：删除声明，或实现

### [高] S57-10｜死代码 + 诊断不可达
- 位置：`Allocator.c:333-345`（`KrtPtrCheckDoubleFree`）
- 类别：死代码 / 诊断不可达
- 触发条件：任何调用（当前零调用者）
- 后果：`KrtSafeFree`（:276-310）在释放时已 `remove_memory_block` 并释放 `MemoryBlock`，第二次 `KrtPtrGetBlock` 必返回 NULL → `block && block->is_freed` 恒假 → 双重释放永远检测不出来；`is_freed` 字段（:205 写入）是死字段
- 最小修复：删除该 API 与 `is_freed`，或在 `remove_memory_block` 时 poison 标记

### [高] S57-11｜死代码
- 位置：`Allocator.c:437-458`（`KrtMemoryDumpBlocks`，遍历链表但 `current = current->next` 无任何输出）、`:474-475`（`KrtMemoryDumpStats` 空函数体）、`:460-472`（`KrtMemoryGetTotalUsage`/`KrtMemoryGetBlockCount` 零调用者）
- 类别：死代码
- 最小修复：删除

### [高] S57-12｜诊断逻辑反了（恒不触发）
- 位置：`Allocator.c:295-297`
- 类别：诊断不可达
- 触发条件：任何 `KrtSafeFree`
- 后果：`KrtMemoryIsPoisoned(ptr, block->user_size)` 在 free 中于 poison 之前调用，且要求全部用户字节都是 `0xDD` 才返回 true → 对活块几乎不可能成立 → "USE_AFTER_FREE" 诊断是死代码
- 最小修复：改为 free 前记录 hash，二次 free 时比对；或删除该诊断

### [高] S57-13｜静默失败
- 位置：`Allocator.c:283-287`（未跟踪指针静默 no-op → 泄漏）、`:239-244`（`KrtSafeRealloc` 未跟踪返回 NULL 并泄漏 `old_ptr`）
- 类别：静默失败
- 最小修复：`KrtMemoryReportError` 改为 `KrtError_LOC`（真实诊断）并 `KRT_COMPILE_ERROR`

### [高] S57-14｜静默失败（宏）
- 位置：`Allocator.h:107`（`KRT_ARRAY_GET` 越界返回 NULL 无诊断）、`:109-117`（`KRT_ARRAY_SET` 越界调用空操作 `KrtMemoryReportError`）
- 类别：静默失败
- 后果：越界写入被静默丢弃
- 最小修复：越界时 `KRT_COMPILE_ERROR`

### [高] S57-15｜契约破坏 / 静默失败
- 位置：`Arena.c:77-112` + `Tokenizer.c:428,436`
- 类别：契约破坏 / 静默失败
- 触发条件：arena 分配失败（含 `KrtArenaAlloc` 对 `size==0` 返回 NULL）
- 后果：`Tokenizer.c:428`（及 :436）在 arena 分配失败时 `return lexer_create_token(TOKEN_EOF, ...)` → 把 OOM 伪装成文件结束，词法阶段静默截断输入，后续阶段得到残缺 token 流
- 最小修复：失败时 `KRT_COMPILE_ERROR` 报位置与原因；`KrtArenaAlloc` 对 `size == 0` 的行为需明确文档化（当前返回 NULL，调用方 `ParserBase.h:10`、`TypeExpansion.inc:16` 用 `size ? size : 1` 绕过，属未文档化前置条件）

### [中] S57-16｜契约破坏（未文档化的 `old_size` 前置条件）
- 位置：`Arena.c:138-169`
- 类别：契约破坏
- 触发条件：调用方传入错误的 `old_size`（唯一调用点 `Tokenizer.c:434` 传 `capacity/2`，当前正确）
- 后果：`old_aligned` 与真实分配不符时，快速路径 `ptr == block->data + block->used - old_aligned` 误判 → 走慢路径 → 旧 arena 空间永久泄漏；且 `memcpy(result, ptr, old_size)` 可能多拷贝（`old_size` 大于真实分配）→ 越界读
- 最小修复：把 `old_size` 存进分配头，或文档化并加 `assert`
- 备注：已核查快速路径条件本身正确（`Arena.h:21` 对齐保证、`KRT_ARENA_ALIGNMENT` 与 `KrtSizeAlign` 一致）

### [中] S57-17｜死代码 / 诊断不可达
- 位置：`SmartPtr.c:299-308`（`total_allocations - total_deallocations` 无符号下溢）、`:137-142`（`KrtSharedCount`）、`:242-261`（`KrtMakeWeak`）等零调用者
- 类别：死代码
- 最小修复：删模块（S57-3）

### [中] S57-18｜死代码（宏别名级联）
- 位置：`KrtCommon.h:199-209`
- 类别：死代码
- 触发条件：任何 `KRT_MALLOC_LEAK` 系列调用
- 后果：`KRT_LEAK_DETECTION_ENABLED` 在 `LeakDetector.h:10` 与 `KrtCommon.h:168` 重复定义为 0（S45-2 已报，不重复计数）→ `KRT_MALLOC_LEAK` 系列成为 `KRT_MALLOC` 纯别名 → `LeakDetector.c:157-193` 四个 `*LeakDetected` 函数不可达
- 最小修复：随 S45-2 一并处理
