# 第八十六批：SelfHost/Compiler/Driver/Project.krt（全量）

## 覆盖范围
- 目标文件：`SelfHost/Compiler/Driver/Project.krt`，全量 1–997 行，分 4 段读完（1–250、251–500、501–750、751–997）。
- 覆盖模块：项目发现与加载（`KrtProjectFind`/`KrtProjectLoad`/`KrtProjectResolveLibraries`）、原生 glob（`KrtProjectGlobWalk`/`KrtProjectPatternMatch`/`KrtProjectSpecs`）、成功构建缓存（`KrtProjectCache`/`KrtProjectFileHash`/`KrtProjectReadManifest`/`KrtProjectWriteManifest`/`KrtProjectRestore`/`KrtProjectBuildKey`/`KrtProjectBuild`/`KrtProjectClean`）、`project new/info`、项目配置语言（`KrtProjectParse`/`Configure`/`Json`/`Legacy`/`Expression`/`Quoted`/`Expand`/`Set`）。
- 对照方式：只对 `Re.KrtC/src/Compiler/Driver/ProjectKrt.c`、`Project.c` 及 SelfHost 的 `PlatformAbstraction.krt`、`Driver.krt`、`Build/BuildSystem.krt` 做定点 grep（符号级），未整文件通读其他源文件。C 版 `Project.c`/`ProjectKrt.c` 无 manifest/缓存实现，涉及缓存与旧式格式的可比性结论均标「疑似」。

## 问题条目

### [高] 86-1｜KrtProjectFileHash 把「读取失败/分配失败」折叠为哈希 0，污染缓存键与恢复校验
- 位置：`SelfHost/Compiler/Driver/Project.krt:48-60`（关键 51、56），使用点 `338-340`、`355`、`358`、`360`、`362`、`460`、`468`、`473`、`482`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：文件存在但 `open/read` 失败、或 `KrtDriverAlloc(8192)` 返回 0；另外 55 行对 `-4`(EINTR) 无限 `continue`，无重试上限。
- 后果：函数统一返回 0，与「文件不存在」不可区分。`KrtProjectBuildKey` 在 338-340 对 `.kro` 对象、编译器、链接器直接调用它且不检查文件是否可读；`460/468/473/482` 把 0 写入 manifest 的 `hash`/`artifact_hash`。恢复时 `358/362` 先 `FileExists` 再比对哈希，若缓存文件随后不可读则 `FileHash` 又返回 0，与存储的 0 相等，校验通过并把损坏/截断的缓存对象当有效产物发布；也可能与旧 key 相撞走 `up-to-date`。
- 最小修复：改为 `(uint64 hash, bool ok)`；失败时 `BuildKey` 强制失效缓存并报 `E_OUTPUT`，`Restore`/`Clean` 把哈希失败视为「不匹配」而非 0；EINTR 重试加次数上限。
- 备注：51 行已检查分配失败，但静默返回 0 仍是失败吞掉；`KrtDriverAlloc` 在别处（如 `Driver.krt:527`）按可失败处理。

### [高] 86-2｜KrtProjectRestore 静默覆盖已存在的输出与对象文件
- 位置：`SelfHost/Compiler/Driver/Project.krt:360-364`
- 类别：6 契约破坏 / 1 静默失败
- 触发条件：`options.output` 或某个 `object.output` 已存在且哈希与 manifest 记录不同（用户手改、其他工具生成、上一次构建被中断后残留）。
- 后果：不提示、不备份，直接用缓存内容覆盖；若该路径是用户文件则数据丢失。`360` 与 `362` 两处同样行为。
- 最小修复：仅在目标不存在时发布；或覆盖前打印 `restored:`/保留 `.bak`，并让用户可用 `--rebuild` 显式选择。
- 备注：与 `481` 的构建期发布语义一致，但恢复路径没有任何确认或日志区分「新建」与「覆盖」。

### [高] 86-3｜compile_together 模式下单文件对象编译失败被 defer_failures 吞掉，构建仍返回 0
- 位置：`SelfHost/Compiler/Driver/Project.krt:442`（`task.defer_failures = project.compile_together`）、`456-464`（463 打印 `object deferred`）、`447`
- 类别：1 静默失败 / 6 契约破坏
- 触发条件：`CompileTogether(true)` 项目中某个源文件无法单独编译（依赖合并上下文），且错误码被 `Driver.krt:762 KrtDriverDeferredCode` 认定为可延迟。
- 后果：`KrtDriverParallelCompile` 对这类任务返回成功，`458` 的 `task.status == 0` 跳过该对象，manifest 不写对应记录，但 `484` 仍打印 `built:` 并返回 0。缓存与 `Clean` 都看不到这些缺失对象，构建产物「成功」但不完整。
- 最小修复：区分「允许 defer 且合并产物已覆盖」与「必须成功」；至少把被 defer 的单元写入 manifest 的 pending 状态，或在不使用 `check` 的构建中把缺失对象计入失败。
- 备注：463 的文案 `requires the combined project context` 与 `compile_together` 已提供合并上下文相矛盾，疑似设计意图但未在注释中说明。

### [高] 86-4｜发布顺序导致失败时输出已替换、缓存清单未更新，且无回滚
- 位置：`SelfHost/Compiler/Driver/Project.krt:481-483`（同类：`459`）
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`481` 的第二次发布（`cache/artifact`）失败，或 `483` 的 `KrtProjectWriteManifest` 失败（磁盘满、权限、I/O 错误）。
- 后果：用户可见的 `output` 已被新产物替换，但 `.krtcache/manifest.krtcache` 仍是旧 key/旧 artifact；临时目录被删除且无回滚。下一次构建可能用旧 manifest 走 `up-to-date`，或 manifest 与实际产物不一致。`459` 同样是先发布 `task.output` 再发布缓存，失败时对象输出已更新而缓存未更新。
- 最小修复：先写 `manifest.pending` 并最后原子发布，或失败时恢复/删除已发布的 `output` 与对象输出。
- 备注：`483` 有 `E_OUTPUT` 报错，但状态已不一致；`459` 的失败由 `KrtProjectPublish` 报错。

### [中] 86-5｜glob 目录嵌套超限的错误被二次误报为「source pattern matched no files」
- 位置：`SelfHost/Compiler/Driver/Project.krt:99`（报错并 `ok=false`）、`133`（`return null`）、`243-249`
- 类别：2 诊断缺失/误导
- 触发条件：模式递归深度超过 128。
- 后果：`KrtProjectGlobWalk` 已报 `E_INPUT source pattern directory nesting limit exceeded`，但 `KrtProjectGlobPaths` 用 `null` 同时表达「出错」和「无匹配」；`243-249` 再拆 parts 重试并追加第二条 `E_INPUT source pattern matched no files`，把真实原因掩盖，用户看到两条矛盾诊断。
- 最小修复：`KrtProjectGlobPaths` 返回 `(paths, ok)` 或错误码，出错时直接向上返回，不再进入「无匹配」分支。
- 备注：`133` 的 `if (glob.ok) return glob.paths; return null;` 是根因。

### [中] 86-6｜`**` 分支跳过所有隐藏文件/目录，与 C 版及非 magic 分支语义不一致
- 位置：`SelfHost/Compiler/Driver/Project.krt:107-111`
- 类别：3 语义分叉
- 触发条件：模式含 `**`，目录树中存在 `.foo.krt` 或 `.dir/x.krt`。
- 后果：SelfHost 直接跳过全部以 `.` 开头的条目；C 版 `ProjectKrt.c:317` 的 `krt_glob_append` 只跳过 `"."` 和 `".."`。同一项目在两版驱动下收集到的源文件集合不同。另：非 magic 分支 `121` 走精确路径，反而能命中隐藏文件，文件内部也不一致。
- 最小修复：只跳过 `"."`/`".."`，或把隐藏规则写入注释并同步 C 版。
- 备注：疑似；C 版对 `**` 的递归实现不同，无法逐字对齐，但「跳过全部点文件」在 C 版无对应逻辑。

### [中] 86-7｜KrtProjectPatternMatch 的前导点规则使 `*`/`?`/括号模式一律失配点文件
- 位置：`SelfHost/Compiler/Driver/Project.krt:77`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：待匹配名字首字节为 `.` 且模式首字节不是 `.`（如 `*.krt` 对 `.hidden.krt`、`[a-z]*.krt` 对 `.a.krt`）。
- 后果：直接 `return false`，即使模式本身可以匹配点字符；用户不会得到任何「点文件被排除」的提示。C 版 `krt_glob_match`（`ProjectKrt.c:203` 起）未见任何 `'.'` 特判（对 `== 46`/`'\.'` 的 grep 无命中）。
- 最小修复：若为对齐 `fnmatch(FNM_PERIOD)` 则同步 C 版与文档；否则删除该特判。
- 备注：疑似；该规则与 `107-111` 的隐藏跳过叠加后行为更难预测。

### [中] 86-8｜对象输出与最终输出同名时静默返回 1，无诊断
- 位置：`SelfHost/Compiler/Driver/Project.krt:437`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`KrtProjectObjectOutput(project, source)` 与 `options.output` 相等（例如把 `--output` 指向 `obj/xxx.kro`）。
- 后果：`KrtProjectOutputSafe` 那半条件会报 `E_OUTPUT`，但 `KrtDriverStringEqual(object_output, output)` 这半条件成立时直接 `KrtDriverRemoveTree(temporary); return 1;`，没有任何错误信息；用户只看到退出码。
- 最小修复：在该分支补 `KrtDriverError("E_OUTPUT","object output collides with final output")`。
- 备注：同一 if 的两个分支诊断行为不一致。

### [中] 86-9｜retained_output 缺失时静默失败
- 位置：`SelfHost/Compiler/Driver/Project.krt:467`
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：`compile_together` 构建后 `temporary/retained.kro` 不存在（编译器未产出该目标、目标类型不保留 KRO）。
- 后果：`!KrtDriverFileExists(options.retained_output)` 成立即删临时目录并 `return 1`，无 `KrtDriverError`；同条件另外两种失败（发布失败）由 `KrtDriverPublish` 报错，唯独文件缺失是静默的，排查困难。
- 最小修复：该分支补 `E_OUTPUT`，说明缺少 retained 对象及对应目标类型。
- 备注：`BuildSystem.krt:105` 是 retained 的实际产出点，可对照确认何种目标不产出。

### [中] 86-10｜Clean 在 manifest 无效时仍删除缓存目录，输出与对象残留
- 位置：`SelfHost/Compiler/Driver/Project.krt:486-498`（关键 `488` 的 `if (manifest.valid)`、`496`）
- 类别：1 静默失败 / 2 诊断缺失 / 10 资源与生命周期
- 触发条件：manifest 损坏/截断、`count>16384`、尾部多余字节，或 `manifest.project != project.path`（项目被移动、复制、路径写法变化）。
- 后果：`bin/...` 与 `obj/...` 产物全部保留，`.krtcache` 被 `RemoveTree` 删除，命令仍打印 `cleaned:` 并返回 0；用户以为项目已清理干净。
- 最小修复：manifest 无效时至少打印「无法确认上次产物」并保留缓存，或无条件删除已知 output/obj 后再删缓存。
- 备注：`225-227` 的注释只承诺「configuration 变化/依赖删除不阻塞清理」，不覆盖项目路径变化。

### [中] 86-11｜KrtProjectClean 忽略 KrtDriverRemoveFile 的失败
- 位置：`SelfHost/Compiler/Driver/Project.krt:490`、`493`
- 类别：1 静默失败 / 9 错误处理不一致
- 触发条件：产物只读、权限不足、文件被占用，`unlink` 返回非 0 且非 `-2`。
- 后果：删除失败被忽略，随后 `496-497` 仍打印 `cleaned:` 并返回 0；对比 `496` 的 `KrtDriverRemoveTree` 有返回值检查，两处策略不一致。
- 最小修复：检查返回值，失败时 `KrtDriverError("E_OUTPUT", ...)` 并返回 1。
- 备注：`PlatformAbstraction.krt:366` 明确返回 bool。

### [中] 86-12｜KrtProjectReadManifest 对损坏 manifest 完全静默
- 位置：`SelfHost/Compiler/Driver/Project.krt:291-302`（关键 `293`、`297`、`301`）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：magic 不匹配、`file.length < 48`、`count > 16384`、字符串越界、`reader.position != reader.length`、`project` 不匹配。
- 后果：一律返回 `valid=false`，构建路径静默重建（尚可接受），但 `Clean` 会走 86-10 的「删缓存留产物」路径；用户全程看不到「缓存已损坏」。
- 最小修复：区分「文件不存在」与「解析失败」，后者打印告警并计入诊断。
- 备注：构建静默重建本身合理，问题在不可观测与 clean 的连带后果。

### [中] 86-13｜KrtProjectExpand 对未闭合 `${` 静默保留字面量
- 位置：`SelfHost/Compiler/Driver/Project.krt:719-721`
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：字符串中出现 `${name`（扫描到串尾仍无 `}`）。
- 后果：`end == length` 时 `index++; continue;`，`${name` 原样进入配置值，后续 `Name`/`Output` 变成带 `${` 的路径，错误在很晚才以「文件不存在」等形式暴露，且无任何配置期诊断。
- 最小修复：该分支报 `E_PROJECT unterminated variable expansion` 并失败。
- 备注：疑似；C 版无可比符号，按诊断缺失记。

### [中] 86-14｜Legacy 格式探测只认 5 个键，其它旧式配置被送去 Configure 解析器
- 位置：`SelfHost/Compiler/Driver/Project.krt:971-985`（关键 `980`）、`993`
- 类别：3 语义分叉 / 2 诊断缺失
- 触发条件：旧式 `key: value` 配置中出现 `optimization`、`target`、`includes`、`libraries`、`compiletogether`、`version`、`description` 等未列入 `980` 的键。
- 后果：`KrtProjectLegacyFormat` 返回 false，`KrtProjectParse` 走 `KrtProjectConfigure`，lexer 把 `optimization: 2` 解析成标识符后报 `unsupported configuration statement`（`875`），旧配置整体不可用。
- 最小修复：把 `980` 的键集合与 `KrtProjectOption`（`812-823`）对齐，或改为「存在合法的 `key:` 行即判定 legacy」。
- 备注：疑似；C 版配置模型不同，无同类探测逻辑可比。

### [中] 86-15｜KrtProjectNew 部分写入失败不回滚，留下不可重试的半成品目录
- 位置：`SelfHost/Compiler/Driver/Project.krt:541-542`
- 类别：9 错误处理不一致 / 10 资源与生命周期
- 触发条件：`project.krt`、`.gitignore` 或 companion 源写入失败（权限、磁盘满）。
- 后果：`src/main.krt` 已写入但命令返回 1；由于 `514` 用「目录非空即拒绝」，用户直接重跑 `new` 会报 `destination is not empty`，必须先手工清理。
- 最小修复：任一步失败时删除本次已创建的目录/文件，或先写临时目录再整体发布。
- 备注：`541` 的三连写用 `||` 短路，第一处失败后两者都不执行，但已写内容保留。

### [中] 86-16｜KrtProjectBuildKey 不检查配置文件二次读取的失败
- 位置：`SelfHost/Compiler/Driver/Project.krt:335`
- 类别：1 静默失败 / 8 未检查前置条件
- 触发条件：`KrtProjectLoad` 成功后 `project.path` 被删除或变为不可读，`KrtDriverReadFile` 失败（`configuration.ok == false`）。
- 后果：`configuration.data/length` 为空仍被 `KrtProjectHashBytes` 哈希，key 静默变小；若与旧 key 相撞则直接 `up-to-date`，用旧产物掩盖配置已丢失的事实。
- 最小修复：检查 `configuration.ok`，失败时强制 `rebuild` 或报 `E_PROJECT`。
- 备注：与 86-1 同源（读取失败被吞），但位置与后果独立。

### [中] 86-17｜Sources glob 会把上一轮构建的 `obj/` 产物当作输入
- 位置：`SelfHost/Compiler/Driver/Project.krt:251`（收集点）、`125`（glob 收集文件）、`376`（对象输出固定落在 `obj/`）
- 类别：3 语义分叉 / 7 共享可变状态
- 触发条件：`p.Sources("**")`、`**/*`、`obj/**` 等会遍历 `obj/` 的模式。
- 后果：`obj/unit-N.kro` 被 `KrtProjectGlobWalk` 当作文件收集，`KrtProjectAdd` 按 `.kro` 加入 `project.objects`；build key 由此包含自身产物，形成自引用，输入集合随构建次数增长，链接输入也变化。
- 最小修复：`KrtProjectLoad` 收集时排除 `project.directory/obj` 与 `.krtcache`（以及 `project.output` 所在目录）。
- 备注：`KrtProjectNew` 生成的默认 `src/**/*.krt` 不受影响，仅用户宽模式触发。

### [中] 86-18｜glob 对不存在/不可读目录无诊断，统一表现为「无匹配」
- 位置：`SelfHost/Compiler/Driver/Project.krt:105`、`120`（`KrtDriverListEntries` 返回值直接使用）
- 类别：1 静默失败 / 2 诊断缺失
- 触发条件：目录不存在、`opendir` 权限不足；`PlatformAbstraction.krt:297` 的 `KrtDriverListEntries` 实现内无 `KrtDriverError`（该文件 grep 无错误调用）。
- 后果：返回空/null，`KrtProjectGlobWalk` 正常返回，最终报 `source pattern matched no files`，真实原因（不可读）被掩盖。
- 最小修复：区分「空目录」与「列举失败」，后者报 `E_INPUT`。
- 备注：疑似；未通读 `KrtDriverListEntries` 实现，仅确认其所在文件无错误上报。

### [低] 86-19｜build key 未包含 show_ir，show-ir 构建会改写缓存清单
- 位置：`SelfHost/Compiler/Driver/Project.krt:326-342`（无 show_ir）、`419`（restore 条件排除 show_ir）
- 类别：3 语义分叉 / 11 配置漂移
- 触发条件：同一项目先 `krt build --show-ir` 再普通 `krt build`（或相反），其余选项不变。
- 后果：`--show-ir` 跳过 restore 并执行完整构建、写 manifest，但 key 与常规构建相同；随后常规构建会以该 manifest 直接 `up-to-date`。若 show-ir 路径对产物有副作用，常规构建不会再校验。
- 最小修复：把 `options.show_ir` 纳入 key，或明确 show-ir 构建不得写 manifest。
- 备注：正常情况产物相同，故为低。

### [低] 86-20｜`if (!clean)` 与 `source == null` 为不可达死代码
- 位置：`SelfHost/Compiler/Driver/Project.krt:239`（`clean` 已在 `227` 提前返回）、`408`（`255` 已保证 `project.sources != null`）
- 类别：5 死代码
- 触发条件：任何调用路径。
- 后果：`239` 的条件恒真；`408` 的 `KrtDriverCompileBundle(options,null)` 分支不可达。若未来 `KrtProjectLoad` 放开 sources 检查，`408` 会以 null bundle 调用编译，行为未定义。
- 最小修复：删除 `if (!clean)` 外壳；把 `408` 改为断言或删除。
- 备注：不影响当前行为，属可维护性/防御性缺口。

### [低] 86-21｜project-info 输出字段不全，无法审计实际构建配置
- 位置：`SelfHost/Compiler/Driver/Project.krt:551-561`
- 类别：2 诊断缺失 / 12 测试不足（契约不可验证）
- 触发条件：执行 `krt project-info`。
- 后果：输出仅含 path/name/type/output/configuration/sources/includes/objects，缺少 `optimization`、`target`、`compile_together`、`libraries`、`patterns`、`version`、`description`；其中 `libraries`/`patterns` 是声明态输入，恰恰是排查「为什么选了这些源」所需的字段。
- 最小修复：补齐上述字段（`patterns`/`libraries` 可复用 `KrtProjectInfoPaths`）。
- 备注：疑似契约不完整；不影响构建。

### [低] 86-22｜KrtProjectSet 对空参数列表静默成功
- 位置：`SelfHost/Compiler/Driver/Project.krt:831-837`
- 类别：1 静默失败
- 触发条件：`p.Sources()`、`p.Includes()`、`p.Libraries()`（无实参）。
- 后果：`values == null` 时 while 被跳过，直接 `return true`，无任何诊断；若发生在 `Sources`，错误会推迟到 `240` 的 `no Sources defined`，掩盖真实写法问题。
- 最小修复：`values == null` 时报 `E_PROJECT option requires at least one value`。
- 备注：疑似；对比 `option == 11` 对 null 有显式报错，策略不一致。

### [低] 86-23｜项目类型错误消息与实际接受集合不一致
- 位置：`SelfHost/Compiler/Driver/Project.krt:234`、`513`（接受集合见 `202-204`）
- 类别：2 诊断缺失 / 13 注释与实现脱节
- 触发条件：`p.Type("foo")` 或 `krt new foo dir`。
- 后果：提示只写 `choose console, system, or library`，但 `KrtProjectKind` 实际接受 `console/exe/system/web/library/lib/dll`，用户无法从诊断得知可用值。
- 最小修复：消息改为完整集合，或改为通用文案「unsupported project type」。
- 备注：两处消息完全相同，需同步。

### [低] 86-24｜KrtProjectOptions 未复制 quiet/command/name，check 的逐文件单元行为与顶层不一致
- 位置：`SelfHost/Compiler/Driver/Project.krt:343-352`（缺 `quiet`/`command`/`name`）、`410-412`
- 类别：3 语义分叉 / 9 错误处理不一致
- 触发条件：`CompileTogether(false)` 的 `krt check`（`410` 构造 unit 时未设 `quiet`）。
- 后果：`unit.quiet` 取新对象的默认值而非 `original.quiet`，顶层 `--quiet` 对逐文件检查失效，输出噪声与顶层策略不一致；`command`/`name` 丢失，当前 `BuildSystem.krt` 未使用它们，属潜在契约缺口。
- 最小修复：在 `KrtProjectOptions` 中补拷 `quiet`/`command`/`name`（以及其它新增字段），或在 `410` 显式设置。
- 备注：疑似；`BuildSystem.krt:34/113` 确实读取 `options.quiet`。

### [低] 86-25｜var 分支出错后仍继续 Assign，可能掩盖首个错误
- 位置：`SelfHost/Compiler/Driver/Project.krt:866-868`
- 类别：9 错误处理不一致 / 2 诊断缺失
- 触发条件：`var 123 = ...`（变量名不是标识符）等导致 `866` 的 `KrtProjectFail`。
- 后果：Fail 后无 return，`KrtProjectToken`/`KrtProjectNext`/`KrtProjectTake` 在 `errors != 0` 下空转，`KrtProjectExpression` 返回 null，仍执行 `KrtProjectAssign(parser,name,null)`，把垃圾名字写入变量表；后续若再引用该名字，首错会被「unknown variable」类二次错误掩盖。
- 最小修复：`KrtProjectFail` 后立即 `return`（并补 `parser.depth--`）。
- 备注：影响仅限错误路径，故为低。

### [低] 86-26｜构建临时目录只以 PID 命名，存在复用与残留风险
- 位置：`SelfHost/Compiler/Driver/Project.krt:423`（`syscall(39,...)` 为 getpid）
- 类别：7 共享可变状态 / 10 资源与生命周期
- 触发条件：同进程内并发/重入构建，或系统 PID 复用后碰上上次崩溃残留的 `.krtcache/.build-<pid>`。
- 后果：两次构建共用同一临时目录，`worker-*.log`/`unit-*.kro` 可能互相覆盖；`MakeDirectories` 对已存在目录大概率成功，残留文件会被当成本轮产物。
- 最小修复：加入构建序号/随机后缀，并在构建开始时清理陈旧的 `.build-*`。
- 备注：单进程 CLI 顺序执行下触发概率低。
