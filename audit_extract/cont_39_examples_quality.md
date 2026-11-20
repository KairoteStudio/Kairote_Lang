# 第九十六批：examples/http-server + Test/Quality

## 覆盖范围

- `examples/http-server/Main.krt`（3 行，全文读完）
- `examples/http-server/Server.krt`（258 行，全文读完）
- `Test/Quality/` 下全部 `.c/.h/.inc/.krt`：`test_backend_allocation.c`(179)、`test_elf_failure.c`(219)、`test_frontend_allocation.c`(100)、`test_ir.c`(260)、`test_linker.c`(121)、`test_memory.c`(157)、`test_memory_benchmark.c`(40)，合计 1076 行，逐个全量读完。
- 目录内 `.py`（RunTests.py/Audit.py/Benchmark.py/CheckStandards.py/test_native.py/test_standards.py）不在授权读取范围，仅对 `RunTests.py` 做过两次定点 grep 以核对调用参数，未通读。
- 全批无「致命」级；最高为「高」。共 42 条。

## 问题条目

### [中] 96-1｜示例工程零测试覆盖，Run 的端口契约与退出码无人验证
- 位置：`examples/http-server/Main.krt:2`
- 类别：覆盖缺口
- 触发条件：任何人修改 `HttpServer.Run` 的 `PORT=` 输出格式、就绪时序或返回码后直接合入。
- 后果：示例的行为契约（先 bind/listen 再打印 `PORT=`、连接上限用尽返回 0）没有任何断言保护，编译通过即等于验收。
- 最小修复：新增示例测试，以 `Run(0, 1, &handler)` 驱动一次连接，捕获 stdout 解析 `PORT=`，断言端口可连通、退出码为 0；或对 `Run` 做返回码表驱动用例。
- 备注：本条只针对该示例目录无任何测试的事实，不涉及 Main.krt 代码本身错误。

### [高] 96-2｜Run 丢弃 HandleClient 的返回值，连接级失败全部静默，退出码仍为 0
- 位置：`examples/http-server/Server.krt:247`（失败源头 `Server.krt:170`、`Server.krt:172`）
- 类别：静默通过
- 触发条件：任一连接上 `SendResponse`/`Respond` 发送失败（EPIPE、EAGAIN 超时）、或 handler 抛错走 `catch` 分支。
- 后果：`HandleClient` 的 bool 结果被丢弃，`Run` 既不计数也不上报，正常结束时仍 `return 0`；调用方（含 Main.krt:2）拿到 0 会认为服务健康，与实际错误不符。
- 最小修复：记录失败数（如 `int32 failed++`），`Run` 结束按失败数返回非 0，或至少输出一行错误诊断。
- 备注：`Server.krt:250` 只检查 `configured/closed`，同样未看 HandleClient 结果。

### [中] 96-3｜SendResponse 未校验 handler 返回的 body，空/default body 直接进入字符串取长
- 位置：`examples/http-server/Server.krt:82`
- 类别：契约破坏/不安全用法
- 触发条件：自定义 handler 返回 `status` 合法（200–599）但未赋 `body`（如 `default(HttpResponse)` 后只改 status），或 body 为 null。
- 后果：`Sys.InternalStringLength(response.body)` 作用于空引用，行为未定义（崩溃或长度错），后续 `InternalStringPtr` 与 Content-Length 全部失真；状态码合法性检查（79-81）帮不上，因为它只挡非法 status。
- 最小修复：在取长前判空（null 视为空串或统一回落 `Respond(client,500)`），并把「status 与 body 必须成对」写进 handler 契约注释（122-123 行已有借用电注释，需补所有权/非空约束）。
- 备注：`DefaultHandler`（115-120）自身总赋 body，所以只有自定义 handler 会踩。

### [中] 96-4｜accept 任一错误即关闭 listener 退出，瞬时资源耗尽被当成永久故障
- 位置：`examples/http-server/Server.krt:239-244`
- 类别：可用性/错误处理
- 触发条件：`accept` 返回 `EMFILE`/`ENFILE`/`ENOBUFS` 等瞬时错误（fd 被瞬时打满）。
- 后果：服务端直接关闭监听套接字并 `return 7`，整个进程退出；本可稍后重试的瞬时故障变成永久拒绝服务，且无诊断。
- 最小修复：对可恢复错误 `continue`（可加退避与计数上限），仅对不可恢复错误关闭退出；至少在退出前打印 errno。
- 备注：`client == -4`（EINTR）已在 240 行 continue，说明作者已区分过一类可重试错误。

### [中] 96-5｜handler 在 accept 线程同步执行且无隔离，单个阻塞 handler 瘫痪整个服务
- 位置：`examples/http-server/Server.krt:169`（分发点 `Server.krt:247`）
- 类别：示例中的不安全用法/契约破坏
- 触发条件：使用者按 `Run(port, limit, &handler)` 传入执行阻塞 I/O 或死循环的闭包。
- 后果：单线程事件循环被独占，后续 accept 与已排队连接全部停摆，`connection_limit` 形同虚设；示例没有任何「handler 必须快速返回」的说明或超时机制。
- 最小修复：在 `Run` 的文档注释中明确 handler 不得阻塞，或提供每连接超时/看门狗；示例层面至少补一条契约警告。
- 备注：作为教学示例，这个契约会直接被模仿到生产代码。

### [低] 96-6｜SendBytes/recvfrom 对 EINTR 无上限重试
- 位置：`examples/http-server/Server.krt:39`、`examples/http-server/Server.krt:132`
- 类别：竞态与非确定性
- 触发条件：进程安装了不带 `SA_RESTART` 的信号处理且信号持续到达。
- 后果：`count == -4` 时 `continue` 形成无退避忙等，`written`/`length` 不前进，连接与服务器都被拖住。
- 最小修复：加重试计数上限并在超限时按失败返回，或改用 `MSG_NOSIGNAL` 之外的自动重启语义。
- 备注：两处都是同一模式，属示例代码的可重试性缺陷。

### [低] 96-7｜退出码 4–9 无任何诊断文本，调用方无法区分失败原因
- 位置：`examples/http-server/Server.krt:217`、`:223`、`:229`、`:243`、`:252`、`:255`
- 类别：诊断与实际错误不符
- 触发条件：socket/setopt/bind/listen/getsockname/accept/close 任一失败。
- 后果：只有结构布局错误（198、210）会打印消息，其余失败仅以 4..9 的数字静默返回；Main.krt 原样透传，排障者看不到 errno 或阶段信息。
- 最小修复：每个返回点打印对应阶段与 errno（或统一错误表），保持退出码不变。
- 备注：与 96-4 同源，但本条关注可诊断性。

### [低] 96-8｜硬编码 Linux x86-64 系统调用号与 loopback 地址，示例不可移植
- 位置：`examples/http-server/Server.krt:24-32`、`:201-203`
- 类别：可移植性
- 触发条件：在非 x86-64 Linux（macOS/ARM/Windows/WSL 差异）上编译运行该示例。
- 后果：`SYS_socket=41` 等常量与 `int64` 直接 syscall 约定在其他架构/平台含义不同，轻则绑定失败，重则按错误 ABI 解释参数；尽管 191-212 做了结构体自检，但系统调用号本身没有平台开关。
- 最小修复：用条件编译把 syscall 号/结构限定在 `#if defined(__linux__) && defined(__x86_64__)`，否则给出明确的不支持提示。
- 备注：4 行注释已声明「Linux x86-64」，属已知取舍，故记低。

### [中] 96-9｜test_backend_allocation.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_backend_allocation.c:171-178`
- 类别：测试断言缺失/静默通过
- 触发条件：以 `-DNDEBUG` 构建该测试（CI 换优化档位时常见）。
- 后果：`assert` 全部被消去后，三种致命分配、PE 全部分配点失败注入与寄存器压力检查都不再产生任何失败信号，`main` 仍 `return 0`，测试变绿。
- 最小修复：改用自带 `CHECK(cond)` 宏（失败即 `fprintf+return 1`），或在构建脚本固定 `-UNDEBUG` 并加断言计数。
- 备注：文件内 `printf` 只报数量，不参与判定（168 行）。

### [中] 96-10｜三种致命分配共用同一条消息前缀断言，无法区分错误来源
- 位置：`Test/Quality/test_backend_allocation.c:121`（用例分支 105-111）
- 类别：断言过弱/诊断不符
- 触发条件：`backend_allocate`、`backend_allocate_zeroed`、`backend_reallocate` 中任一路径打印了错误的上下文文本，只要仍含 `[KRT_COMPILE_ERROR] Backend`。
- 后果：三种 kind 的断言完全同构，诊断文案错位（例如 calloc 失败写成 malloc）不会被发现；`kind` 参数实际上没有区分能力。
- 最小修复：按 kind 断言各自的消息片段（或断言消息中出现分配器名/大小参数），并断言退出码与消息一一对应。
- 备注：子进程 `_exit(99)` 的兜底（112）只能证明「失败了」，不能证明「失败对了」。

### [中] 96-11｜未初始化栈变量地址被当作 ArkLinkContext* 传入
- 位置：`Test/Quality/test_backend_allocation.c:136`、`:154`
- 类别：不安全用法
- 触发条件：`ark_backend_pe_link` 的实现开始读取 `context` 的任何字段（当前可能未读，故暂时不崩）。
- 后果：`context_placeholder` 是未初始化的 `char`，其地址被强转为 `ArkLinkContext*` 后一旦被解引用就是读取垃圾/越界，测试随机崩溃或读到脏状态，且崩溃点在被测代码而非测试。
- 最小修复：提供真正的 `ArkLinkContext` 实例，或在接口允许时直接传 `NULL`。
- 备注：与 `test_elf_failure.c:104/141` 传 `NULL` 的写法相比，这里明显是为了绕过空指针检查。

### [中] 96-12｜allocation_count 为 0 时失败注入被整体跳过且无任何告警
- 位置：`Test/Quality/test_backend_allocation.c:138-166`
- 类别：静默通过
- 触发条件：PE 链接路径改为复用调用方缓冲、不再产生任何 `calloc/realloc`（例如引入 arena 或静态缓冲）。
- 后果：`allocation_count = allocation_index` 得 0，循环 `index <= 0` 只执行成功路径一次，所有失败分支不再被遍历；测试继续打印 `PE allocation failures checked: 0` 并以 0 退出，覆盖悄悄归零。
- 最小修复：在 `allocation_count == 0` 时显式断言「PE 路径至少有一次分配」，或直接断言 `allocation_count > 0`。
- 备注：与 96-15 是同一类「计数驱动循环」的静默降级，但发生在另一个文件。

### [中] 96-13｜寄存器分配器的分配失败路径从未注入，故障测试只覆盖后端分配
- 位置：`Test/Quality/test_backend_allocation.c:74-92`
- 类别：覆盖缺口
- 触发条件：`regalloc_allocate_with_capacity` 内部堆分配失败时的清理/返回逻辑发生回归。
- 后果：`check_register_pressure` 全程 `failure_index=0`（永不失败），只验证成功着色结果；该文件虽已把 `KRT_MALLOC/KRT_CALLOC/KRT_REALLOC` 接管为可注入版本，却没有对 `X86RegAlloc.c` 做任何失败注入，X86 分配器的 OOM 分支零覆盖。
- 最小修复：仿照 `check_pe_allocation_failures`，记录成功路径的分配次数并对每个分配点注入一次失败，断言返回错误码且 `live_allocations==0`。
- 备注：`check_register_pressure` 只断言 `num_spilled` 与着色不冲突（82-88），失败路径完全未涉及。

### [低] 96-14｜check_fatal_allocation 的管道读取与描述符处理脆弱
- 位置：`Test/Quality/test_backend_allocation.c:100-102`、`:115-117`
- 类别：竞态与边界
- 触发条件：(a) `pipe()` 恰好返回 `diagnostics[1]==2`（运行前已关闭 stderr）时 `dup2(2,2)` 后 `close(2)` 会把刚建好的stderr 重新关掉；(b) 子进程消息长于 255 字节或被信号打断。
- 后果：(a) 子进程写进已关闭 fd，父进程 `read` 得 0，断言以误导性方式失败；(b) 单次 `read` 只取前 255 字节，`strstr` 可能漏判。
- 最小修复：`dup2` 后仅在 `diagnostics[1] != STDERR_FILENO` 时 close；父进程循环读到 EOF 并处理 `EINTR`。
- 备注：常规终端下 `diagnostics[1]==4`，(a) 不易触发，故记低。

### [高] 96-15｜test_elf_failure 目录模式静默跳过全部分配失败注入，并打印误导性的 checked: 0
- 位置：`Test/Quality/test_elf_failure.c:107`（输出 `:121`）
- 类别：静默通过 + 诊断与实际错误不符
- 触发条件：`argc > 1`，即按 `test_elf_failure <dir>` 运行（该模式的用途是落 ELF 快照）。
- 后果：`count = directory ? 0 : allocation_index` 把失败注入循环上限置 0，`for (index=0; index<=count; ...)` 只跑一次成功路径，170 余个分配点失败分支全部不执行；同时 `printf("ELF %s allocation failures checked: %zu")` 照常输出，读者会误以为失败路径已被覆盖。
- 最小修复：目录模式与失败注入解耦——先无目录跑完整失败注入，再（可选）带目录只落快照；或至少在 `count==0` 时打印「失败注入已跳过」并计入结果。
- 备注：据 `Test/Quality/RunTests.py:52` 可见常规调用只给 `test_linker` 追加参数，故默认 CI 路径不受影响；但任何手工/快照流程都会静默丢掉这层覆盖。

### [高] 96-16｜test_elf_failure 直接按 file_offset 读取产物，无边界校验，测试自身可越界
- 位置：`Test/Quality/test_elf_failure.c:143-144`
- 类别：整数/边界
- 触发条件：被测链接器返回错误的 `section_maps[i].file_offset`（大于 `output.size-8`）或 `output.size` 异常。
- 后果：`memcpy(&absolute, output.data + output.section_maps[1].file_offset, 8)` 直接越界读堆，可能崩溃或读到无关数据；由于该函数没有先断言 `output.size` 与 `file_offset` 的关系，错误实现可以在「越界读恰好命中预期值」时假通过。
- 最小修复：读取前断言 `output.section_maps[1].file_offset + 8 <= output.size`、`...[0].file_offset + 4 <= output.size`，再 memcpy。
- 备注：同文件 141 行只断言了 `result == ARK_LINK_OK`，未校验产物尺寸。

### [中] 96-17｜test_elf_failure.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_elf_failure.c:154-218`
- 类别：测试断言缺失/静默通过
- 触发条件：以 `-DNDEBUG` 构建。
- 后果：7 类场景的成功/失败断言、ABS64/PC32 地址校验（147-150）、快照 fwrite 结果全部消失，`main` 仍返回 0；ELF 后端回归不再被拦截。
- 最小修复：改用失败即返回非 0 的检查宏，或固定 `-UNDEBUG`。
- 备注：`snapshot()` 中的 `assert(fwrite(...)==...)`（88-95）被消去后连写盘短写都不会被发现。

### [中] 96-18｜test_elf_failure 只覆盖 ARK_OUTPUT_EXECUTABLE，且重定位类型被硬编码为 1
- 位置：`Test/Quality/test_elf_failure.c:138-139`、`:180-181`、`:212`
- 类别：覆盖缺口
- 触发条件：共享库/可重定位输出路径或 `ARK_RELOC_*` 其他类型出现回归。
- 后果：`output_type` 恒为 `ARK_OUTPUT_EXECUTABLE`，reloc `type` 一律写 1，导入重定位与节重定位未按类型矩阵展开；非 executable 产物与其余重定位类型的编码错误无测试拦截。
- 最小修复：为 `output_type` 与各 `ARK_RELOC_*` 建参数化用例；至少覆盖共享对象与 PC32/ABS64 两类交叉。
- 备注：`check_full_width_relocation_addresses` 虽验了 ABS64+PC32 各一条，但只是单组固定输入。

### [低] 96-19｜固定 4096 槽分配跟踪表，耗尽即 abort，测试不可扩展
- 位置：`Test/Quality/test_elf_failure.c:6`、`:19`
- 类别：边界/资源
- 触发条件：用例规模扩大（更多 section/import/export 或实现改为每次分配多块）使跟踪表超过 4096。
- 后果：`track_allocation` 直接 `assert(0 && "allocation tracker exhausted")`，测试以崩溃收场且信息不含实际分配数，无法区分「实现泄漏」与「表太小」。
- 最小修复：改用动态数组/哈希表记录指针，或至少把容量做成与输入规模相关的常量并在耗尽时打印计数。
- 备注：当前 40 导入+40 导出+6 节用例离上限尚远。

### [低] 96-20｜快照文件写入调用方任意路径且不清理
- 位置：`Test/Quality/test_elf_failure.c:83-96`、`:156`
- 类别：资源未清理/可移植性
- 触发条件：传入已存在的目录或指向既有文件的路径运行。
- 后果：`fopen(path,"wb")` 直接截断覆盖同名 `<name>.elf`/`<name>.maps`，且测试结束不删除生成的 7×2 个文件，污染工作区；路径完全依赖外部传参，无临时目录约定。
- 最小修复：快照写入 `mkdtemp` 创建的临时目录并在结束时递归删除；或至少 `unlink` 本次生成的文件。
- 备注：默认无参运行时 `directory=NULL`（156 行），不触发写盘。

### [低] 96-21｜ARK_ELF_TEST_SOURCE 默认相对包含路径绑定仓库布局
- 位置：`Test/Quality/test_elf_failure.c:75-78`
- 类别：可移植性
- 触发条件：从其它检出布局/子目录构建，或上层构建系统未把仓库根加入 include 路径。
- 后果：`#include "../../ArkLink/src/Backend/BackendElf.c"` 直接编译失败，错误信息指向 include 路径而非配置问题。
- 最小修复：由构建系统通过 `-DARK_ELF_TEST_SOURCE=...` 传入绝对/受控路径（该宏已有覆盖机制，默认值不必回退到相对路径）。
- 备注：同目录 test_backend_allocation.c:61,67-68 也硬编码 `../../Re.KrtC/...`、`../../ArkLink/...`，属同批构建耦合。

### [中] 96-22｜__wrap_KrtSafeStrdup/KrtSafeRealloc 的失败分支零覆盖
- 位置：`Test/Quality/test_frontend_allocation.c:33-38`
- 类别：覆盖缺口
- 触发条件：`KrtSafeStrdup`/`KrtSafeRealloc` 的空指针或原地扩张失败处理发生回归。
- 后果：这两个包装器只是为了让链接期 `--wrap` 完整而定义，`semantic_analyzer_create` 与 `KrtAstClone` 路径并不调用它们（`calls` 只统计实际触发的分配），因此它们的失败返回路径从未被执行；`fail_heap` 对四个包装器一视同仁并不能证明四者都被覆盖。
- 最小修复：增加真正走 strdup/realloc 的前端用例（如带源码拷贝的分析器构造），或显式断言 `strdup` 分配点数量 > 0。
- 备注：`check_ast_cloning` 只用 `KrtArenaCreateLocal`（71 行）与 arena 分配，进一步缩小了被覆盖的分配器集合。

### [中] 96-23｜test_frontend_allocation.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_frontend_allocation.c:93-99`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：分析器构造与 AST 克隆的全部失败注入断言失效，`assert(!KrtMemoryGetBlockCount())`（97）也不再校验内存平衡，main 仍返回 0。
- 最小修复：同 96-9：自带 CHECK 宏或固定 `-UNDEBUG`。
- 备注：该文件是 Test/Quality 中唯一的前端分配失败覆盖，失效后前端 OOM 路径完全裸奔。

### [低] 96-24｜只断言「返回 NULL + 块数平衡」，不验证失败诊断与错误状态传播
- 位置：`Test/Quality/test_frontend_allocation.c:47-57`、`:81-90`
- 类别：断言过弱
- 触发条件：`semantic_analyzer_create` 失败时打印了错误信息却仍返回非 NULL 脏对象，或反之返回 NULL 但丢失错误上下文。
- 后果：`assert(!analyzer)` 只覆盖返回值，`KrtMemoryGetBlockCount()==baseline` 只覆盖内存平衡；错误信息缺失、错误码未设置、`project_namespace_collection_failed` 之类状态未更新都不会被发现。
- 最小修复：对失败用例断言诊断输出/内部错误标志，并对成功用例断言无错误残留。
- 备注：与 96-10 同属「失败已发生 ≠ 失败处理正确」。

### [高] 96-25｜test_ir.c 在 count==0 时读 block->insts[-1]，测试自身越界
- 位置：`Test/Quality/test_ir.c:249`
- 类别：整数/边界
- 触发条件：某函数 `entry_block` 在 DCE/内联后指令数为 0（例如全是可删 NOP 且返回被提升/删除）。
- 后果：`block->last_inst == block->insts[count - 1]` 中 `count-1` 为 `size_t` 下溢到 `SIZE_MAX`，对 `insts` 数组做越界读，轻则读到垃圾导致断言随机失败，重则段错误把崩溃归因于被测优化器；同文件 25 行对同一不变量写了 `count ? ... : NULL` 的保护，说明该边界已知，此处遗漏。
- 最小修复：改为 `assert(count ? block->last_inst == block->insts[count-1] : block->last_inst == NULL)`，与 25 行一致。
- 备注：当前 50 个 Function 均含 Add+Return，暂时不会触发，属潜伏缺陷。

### [中] 96-26｜test_ir.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_ir.c:210-259`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：CSE/死链消除、多重定义、无效指针 builder、内联幂等性与 `assert_instruction_index` 全部失效，main 仍返回 0；IR 优化器回归不再被拦截。
- 最小修复：同 96-9。
- 备注：`check_invalid_pointer_builders` 中对 `KrtIrCallIndirect` 负数参数等边界（200-203）也一并失去校验。

### [中] 96-27｜IROptimizer stats 除 cse_count 外全无断言，优化器统计正确性零覆盖
- 位置：`Test/Quality/test_ir.c:65-67`
- 类别：断言过弱/覆盖缺口
- 触发条件：内联、DCE、CSE 的计数字段（如内联成功数、删除指令数）实现出错或未累加。
- 后果：只断言 `stats.cse_count > 0`，其余字段从未读取；`IROptimizer stats = {0}` 之外的返回结构对调用者不可靠，且 `ir_optimize_dead_code_elimination` 的 stats（228-229）完全没接收。
- 最小修复：断言各 stats 字段与手工计数的指令增量一致，并覆盖 DCE/inlining 的 stats。
- 备注：96-28 提到的无效指针路径也没有 stats 校验。

### [中] 96-28｜用直接改写 last_inst->result 伪造非 SSA 输入，绕过公开 API 构造非法 IR
- 位置：`Test/Quality/test_ir.c:144`、`:160`
- 类别：测试方法/覆盖缺口
- 触发条件：真实使用者无法（或不应）构造这种「同一 temp 被两条指令定义」的 IR 时。
- 后果：测试通过手写结构体字段制造不变量违背，验证的是「优化器容忍脏输入」而非「合法非 SSA 输入（如外部 IR/手写字节码）的处理」；一旦失败，断言位置离真正入口很远，且该构造无法复用到其他用例。
- 最小修复：提供公开的测试辅助构造器（或从文本 IR/字节码解码）生成同构输入，并保留对 builder 拒绝重复定义的正向断言。
- 备注：若这是刻意的鲁棒性测试，应再加一条「builder 不允许产生同类结构」的对照断言。

### [高] 96-29｜test_linker.c 对 iat/RVA 未做上下界校验即做指针运算与 memcpy，测试自身可越界
- 位置：`Test/Quality/test_linker.c:49`、`:52-53`、`:55`、`:58-59`、`:64-65`
- 类别：整数/边界
- 触发条件：`generate_import_table` 返回 `iat < base`（减法回绕成巨值）或 `ImportLookupTableRVA/NameRVA` 超出 `size`。
- 后果：`cursor = iat - base` 与 `data + directory.ImportLookupTableRVA - base` 直接成为巨大偏移，随后的 `memcpy`/`strcmp` 越界访问；唯一前置断言（48）只查 `size>200000`、`entries` 与 `iat%8`，挡不住这类错误，测试可能在越界读命中预期字节时假通过。
- 最小修复：在解引用前断言 `iat >= base`、`ImportLookupTableRVA >= base`、`NameRVA >= base` 且各 `+ 所需宽度 <= size`。
- 备注：该测试的定位是「直接验证序列化输出」（文件头注释），恰恰最需要边界护栏。

### [中] 96-30｜test_linker.c 向调用方任意路径写文件，可覆盖既有文件且从不清理
- 位置：`Test/Quality/test_linker.c:98-110`
- 类别：资源未清理/不安全用法
- 触发条件：`argv[1]` 指向已存在文件或不可写目录。
- 后果：`kro_write_file` 以写方式创建/截断目标文件，测试结束既不 `unlink` 也不校验文件长度，残留 `Output.kro`（RunTests.py:52 传入临时目录）；若路径来自用户输入还存在覆盖任意文件的风险面。
- 最小修复：写入 `mkdtemp` 临时目录，结束删除；或至少 `remove(filename)` 并校验文件大小。
- 备注：`assert(argc==2)`（115）保证一定有路径，但没有约束路径性质。

### [中] 96-31｜test_linker.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_linker.c:114-120`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：align_up 饱和边界（10-12）、buffer 复用/溢出（15-22）、1200 项导入表逐项比对（48-70）、KRO 对齐/回读（82-109）全部失效，main 仍返回 0。
- 最小修复：同 96-9。
- 备注：`check_kro` 中文件回读的 254 次 `fgetc` 断言（106-108）被消去后，写盘内容错也无人知。

### [中] 96-32｜导入表的 is_function 维度零断言，lookup>=base 反而把 ordinal 编码锁死
- 位置：`Test/Quality/test_linker.c:41`、`:64`
- 类别：覆盖缺口/断言过弱
- 触发条件：实现对 `is_function==0` 的数据导入改用 ordinal 编码（置高位）或 hint/name 标志错误。
- 后果：`imports[i].is_function = i % 2` 造出两种导入，但断言只走「lookup 是指向 hint/name 的 RVA」一条路；ordinal 条目会让 `lookup >= base` 直接失败（高位为 1），于是测试要么强迫实现忽略 is_function，要么报错，无论哪种都说明该维度未被验证。
- 最小修复：按 `is_function` 分支断言：函数导入查 hint/name 字符串与标志位，数据导入查 ordinal/名称策略，并覆盖 ordinal 路径。
- 备注：测试注释声称验证「serialized output directly」，但实际只验证了 name 表一种表示。

### [低] 96-33｜KRO 数据段回读内容未校验，只校验了代码段与头部字段
- 位置：`Test/Quality/test_linker.c:84-91`、`:104-109`
- 类别：覆盖缺口
- 触发条件：`kro_write_data`/`kro_write_data_aligned` 写入位置或内容错位（如写到代码区、偏移计算错误但偏移返回值正确）。
- 后果：`kro_get_data_offset()==33` 只证明偏移量数值，回读只检查了代码区的两个 0xcc 与填充 0x90；数据区那 1 字节从未被读回，数据段写坏仍全绿。
- 最小修复：按 `header`/data 偏移回读数据区字节并断言等于写入值，同时校验文件总长度。
- 备注：`kro_write_data_aligned(...,3)==0` 的失败分支（90）倒是被覆盖了。

### [中] 96-34｜test_memory.c 全部 oracle 都是 assert，main 恒返回 0
- 位置：`Test/Quality/test_memory.c:148-156`
- 类别：测试断言缺失/静默通过
- 触发条件：`-DNDEBUG` 构建。
- 后果：分配器溢出拒绝（27-28）、arena 分配/复位（45-68）、IR 池复用清零（98-118）、8 线程共享 arena 计数（134-145）与最终内存平衡（153）全部失效，main 仍返回 0。
- 最小修复：同 96-9。
- 备注：该文件是分配器安全性的主回归，失效影响面最大。

### [中] 96-35｜分配器边界覆盖不足：不测 0/1 字节、页阈值与次极端大值
- 位置：`Test/Quality/test_memory.c:18`、`:27-28`、`:31`、`:59-60`、`:68`
- 类别：整数/边界、覆盖缺口
- 触发条件：实现在 0 字节分配、1 字节分配、mmap/块阈值（如 128KB）附近或 `SIZE_MAX-1` 等次极值上行为错误。
- 后果：现有用例只覆盖 `sizeof(max_align_t)..255`、`SIZE_MAX` 与 `SIZE_MAX/2+1`；`size=0` 的返回约定、跨块阈值的重分配、`SIZE_MAX-1` 的加法溢出判定均无测试，属关键边界无覆盖。
- 最小修复：补齐 0/1 字节、128KB±1、`SIZE_MAX-1`、对齐溢出（`size` 使 `size+align-1` 回绕）用例。
- 备注：`KrtBufferCheck` 只测了 `SIZE_MAX-3` 与非法宽度组合（31-32）。

### [低] 96-36｜共享 arena 的 24000 精确断言兼作竞态探测，失败无竞争细节
- 位置：`Test/Quality/test_memory.c:144`
- 类别：竞态与非确定性
- 触发条件：arena 计数器非原子或线程调度使中间态被读到。
- 后果：断言只给出 `arena->allocation_count == 24000` 一个等式，失败时无法区分「竞态丢更新」「重复计数」还是「合法语义变更」；该用例既是功能测试又是竞态探测，职责混杂导致偶发失败难定位。
- 最小修复：把并发正确性交给 ThreadSanitizer/单独压力用例，功能用例只断言每个线程成功返回与最终对齐；或对计数失败打印每线程本地计数。
- 备注：`allocate_thread`（122-132）本身写得干净，无泄漏。

### [低] 96-37｜IR 池只测同型 LIFO 复用，未测扩容/耗尽与跨类型混用
- 位置：`Test/Quality/test_memory.c:98-118`
- 类别：覆盖缺口
- 触发条件：池在当前 arena 块耗尽后的扩容/失败路径，或同一池被不同类型对象复用。
- 后果：用例始终在 200 个对象内反复 alloc/free，`KrtIrPoolAlloc` 的扩容分支与失败返回值从未触发；池耗尽时的行为（返回 NULL 还是向 arena 要新块）无断言。
- 最小修复：构造超过初始池容量的分配序列触发扩容，并断言失败/成功返回与对象清零。
- 备注：`manager.pools[type].object_size` 为 0 的退化类型也未验证（105 行 memset 长度取自该字段）。

### [中] 96-38｜test_memory_benchmark 无阈值无判定，性能回归永远静默
- 位置：`Test/Quality/test_memory_benchmark.c:38-39`
- 类别：静默通过
- 触发条件：分配器/arena 实现性能回退（甚至退化到不可用）。
- 后果：程序只打印毫秒数并 `return 0`，没有任何基线比较、超时或断言；作为 Quality 套件的一部分，它对性能回归完全不设防，且 `assert` 也可被 `NDEBUG` 消除。
- 最小修复：与基线样本比较并设相对阈值（超阈返回非 0），或至少由 Benchmark.py 记录历史曲线并对突增报警。
- 备注：该文件的价值定位若是人工观测，应在 ReadMe 明确「不作为门禁」，避免被当成通过性测试。

### [中] 96-39｜基准两种模式不做内存平衡校验，也不调用 Cleanup
- 位置：`Test/Quality/test_memory_benchmark.c:16-37`
- 类别：资源泄漏/静默通过
- 触发条件：free 模式漏 free、或 arena 模式 destroy 不彻底。
- 后果：两种模式结束都不检查 `KrtMemoryGetBlockCount()/GetTotalUsage()`，也不调用 `KrtMemorySafetyCleanup()`（对比 test_memory.c:153-154 的做法）；即使每次运行泄漏 3 万个块，基准依然打印时间并以 0 退出，泄漏被进程退出掩盖。
- 最小修复：在两分支末尾断言块数/总用量归零并调用 Cleanup，把结果打印出来。
- 备注：`pointers[30000]`（17）本身在栈上，不存在泄漏，但正因如此更应校验堆侧。

### [低] 96-40｜基准的 argc 契约与两分支计时口径不一致
- 位置：`Test/Quality/test_memory_benchmark.c:14`、`:16-37`
- 类别：可移植性/诊断
- 触发条件：不带参数运行，或把两分支耗时横向比较。
- 后果：无参数时 `assert(argc==2)` 直接 abort，无用法提示；free 模式只测 malloc/free，arena 模式额外含 3×100000 次 64 字节 `memset`（32 行），两者计时口径不同，直接对比会得出错误结论。
- 最小修复：缺参时打印用法并返回非 0；两分支统一是否含 memset，或分别输出「纯分配」与「含写入」两个指标。
- 备注：ReadMe 已说明两版对比用途，但代码本身没有防误读保护。

### [低] 96-41｜test_ir.c 的 20 次整组重跑放大成本且失败定位信息不足
- 位置：`Test/Quality/test_ir.c:213-256`
- 类别：测试工程/覆盖效率
- 触发条件：套件运行时间敏感或某次迭代出现偶发失败。
- 后果：每次迭代重建 50 函数模块并完整跑一遍 `check_cse_and_dead_chains`/`check_unused_traps_and_calls`/`check_multiple_definitions`（253-255），任一断言失败只指向具体 assert 行，无法知道是第几次迭代（无迭代编号输出）；NDEBUG 下这些成本仍然支付。
- 最小修复：失败信息带上 `pass` 编号（或把重复次数做成参数），并保持至少一次全量。
- 备注：不影响正确性，属可维护性问题。

### [低] 96-42｜test_backend_allocation.c 的 printf 计数不参与判定，输出可被误读为覆盖率
- 位置：`Test/Quality/test_backend_allocation.c:168`
- 类别：诊断与实际错误不符
- 触发条件：`allocation_count` 因实现改变而变小（见 96-12），或失败注入实际未触发。
- 后果：`PE allocation failures checked: %zu` 只是打印，不与断言挂钩，也不校验 `allocation_count>0`；读者会把数字当成覆盖证明，与 96-15 的 `checked: 0` 属同一类「计数即结论」的误导性诊断。
- 最小修复：把计数纳入判定（计数为 0 直接失败），或改名并注明「本次实际执行的注入点数」。
- 备注：同文件 59 行 `Analyzer allocation failures checked` 同样是纯打印。
