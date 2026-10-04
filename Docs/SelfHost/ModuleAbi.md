# 原生模块 ABI

SelfHost 的模块接口由编译器写入 KRO。消费模块读取 KRO 内的声明和开放泛型模板；不会打开生产模块的原始源码。Python 测试只运行工具并检查产物。

## 独立编译

```sh
SelfHost/krtc Provider.krt -c -o Provider.kro
SelfHost/krtc Consumer.krt Provider.kro -c -o Consumer.kro
SelfHost/krtc Provider.kro Consumer.kro -o program
```

Consumer 可以直接 `using` Provider 导出的命名空间，使用公开类型、字段、构造函数、方法和泛型。普通公开函数体导出为 `extern` 声明。开放泛型保留模板体及其私有依赖；消费者为此前未出现的闭实例生成代码。

类型成员清单与实际生成代码的函数清单分别保存。移除外部声明和开放模板后，公开成员、重载、构造函数及虚方法清单仍完整，契约与运行时派发都读取该成员清单。

普通函数体的导出范围由原生 Lexer 定位配对的结束括号；注释和字符串中的括号不参与匹配，文件末尾注释保留，编辑不会跨越拼接后的源文件边界。无法生成接口时报告 `E_MODULE_METADATA`。

## 版本与契约

|记录|内容|
|---|---|
|`_KRT_ABI2_MANIFEST`|原有 scalar/struct/callback 函数契约，保留原 ABI2 编码|
|`_KRT_ABI3_MANIFEST`|命名对象 ABI 与弱实例函数契约|
|`_KRT_ABI3_TYPES`|独立的类型布局、方法、继承、接口、枚举与可变全局变量契约|
|`_KRT_MODULE3_METADATA`|声明接口、文件来源与开放模板|

函数契约记录包括符号索引、定义标志、完整名义身份和完整 ABI 字节。定义标志为 `0`（引用）、`1`（强定义）或 ABI3 的 `5`（弱定义）。类型记录使用无符号索引 `0xffffffff` 和标志 `2`，允许一致的重复声明。

直接 wide 数值参数、返回值和 `ref wide` 使用完整载荷及调用者分配的返回缓冲区，边界契约为 ABI3 并带 `KRTWIDE3` 物理约定。含此边界的 typed callback 形状版本为 `3`。普通 struct 中的 wide 字段及 `fn(Packet)->Packet` 的原 ABI2 编码保持兼容；旧的 wide 指针返回约定不能与新接口静默混用。

Driver 在发布可执行文件前验证契约。带 KRO 引用的 `-c` 也验证全部布局和函数签名，允许尚未定义的外部函数。裸 ArkLink 同样检查 ABI2/3 契约。布局冲突报告 `E_ABI_LAYOUT`；坏记录和未知版本报告 `E_ABI_MANIFEST`、`E_ABI_VERSION`。失败不会替换已有输出。

独立类型契约按完整声明名匹配，并在契约中记录实际种类。即使没有共同函数，同名 class、struct、interface 或 enum 的不一致声明也会被拒绝。强全局变量、guard 和初始化入口的重复定义或缺失定义报告 `E_ABI_SYMBOL`。

公开开放泛型方法也有独立签名契约。类型参数按位置编号，形参名不参与身份，数组、指针、nullable、ref、函数类型及命名类型构造器保留完整结构。约束使用按参数位置排序的集合；重命名类型参数、调整 `where` 顺序或等价的命名空间别名不会改变契约。局部实例化出来的闭方法不扩大声明清单。

## 模块可见性与名称查找

`internal` 的访问范围由声明所属的原始模块决定。同一次原生多源编译中的文件共享该范围；独立 KRO 保留各自的模块来源。泛型实例化保留模板的声明来源，因此模板可以调用其所属模块的内部辅助函数。消费代码继续按自身的模块来源检查访问权限。

另一个模块的内部实现与消费模块的同名公共 `extern` 原型分别保留，原型合并不会把它们替换为同一个声明。公共原型按其公开符号寻找定义；缺少公开实现时报告 `E_LINK`。KRO 中保留的内部或私有模板依赖继续受访问检查约束。

文件私有声明在其原始文件中优先。同模块的公共与内部声明处于相同的查找范围，重载选择继续比较实参类型；查找本模块的声明后再考虑其他模块的公开声明。类成员的 private/protected 访问独立检查，同类可访问的重载按实参匹配，访问修饰符不改变整数宽度等匹配结果。

## 类型身份与运行时对象

身份包含完整名义名称、类型种类和闭泛型参数。私有与内部类型加入原始文件来源。生成器类型加入原始工厂的完整签名和 sequence/cursor 角色。编译单元中的声明序号不参与身份。

对象头保存运行时描述符指针。描述符、类型测试、接口/虚方法查找和异常匹配使用完整身份字节。哈希仅用于索引；碰撞后仍比较完整字节。稳定符号使用完整身份的十六进制编码。

`_KRTD3$`、`_KRTK3$`、`_KRTI3$`、`_KRTM3$` 分别标识描述符、身份字节、接口表和方法表。所有内部指针使用稳定符号重定位。ArkLink 对重复弱记录逐字节检查，并比较每个重定位的位置、类型、附加值和完整目标符号。

装箱保留具体值的描述符、大小和完整载荷。结构体接口方法的接收地址调整为载荷起点；参数和返回值继续使用通用 struct ABI。消费模块可以调用其源码中未声明的具体实现。

## 静态状态

原生全局变量和静态字段使用稳定 `.data` 符号 `_KRTG3$` 及 RIP 相对重定位。导入的普通声明引用生产模块的存储；闭泛型静态字段使用按完整类型参数区分的弱存储。存储范围以符号记录的 offset、size 和类型对齐要求为准。

显式 `extern static` 声明通过稳定初始化入口 `_KRTL3$` 调用定义模块的初始化器。读取和写入字段前均保证初始化；调用保留活动表达式、参数寄存器、闭包环境和栈对齐。guard 地址本身不触发初始化调用。

初始化使用共享状态、线程所有者和一次初始化 guard。递归读取同线程正在初始化的存储不会再次运行初始化器；其他线程等待初始化完成。异常路径重置状态并重新抛出原异常。模块本地完成 guard 提供已初始化的快速路径。VM 保留槽地址及对应的一次初始化语义。

只包含全局或静态标量声明的原生库模块，在全部初始化式均可安全求常量时直接写入 `.data`。字段 guard `_KRTJ3$` 为完成态；wide 载荷按 16 字节对齐。初始化别名 `_KRTL3$` 共享一个 3 字节的 `xor eax,eax; ret` 函数，对象没有 `main`、启动入口或运行期赋值。后续读取不会覆盖用户写入的数据。

合法初始化式若无法安全求常量，整个模块保留原来的有序动态初始化，包括对其他全局值的读取、函数调用和异常重试。求常量失败不会把初始化值替换为零。

常量转换按目标整数精度规范载荷。65–127 位的 wide 精度保留低 64 位，并把高 64 位截断到目标剩余位数；有符号值再按目标符号位延伸，无符号值的其余高位为零。128 位值保留完整载荷。源语言整数类型名采用偶数精度；回归使用 `uint66`、`uint72`、`int72`、`int126` 和 `uint128` 检查实际数据字节、运行期转换及读写结果。

## 验证

`Test/SelfHost/test_module_abi.py` 对独立生产模块、服务模块和消费模块分别执行 O0–O3 编译，交换链接顺序并执行结果。覆盖未知 class/struct 实现、继承与接口、wide/fixed struct、闭包 callback、类型化异常、KRO 独立元数据、重复声明、静态/闭泛型状态及拒绝时的旧产物保护。

`Test/Quality/test_elf_failure.c` 验证大于 4 GiB 的完整 ABS64 地址与 PC32 位移，避免描述符指针在 ELF 链接时丢失高位。

`fixtures/module-abi-wide` 保存旧编译器产生的 wide ABI2 对象十六进制文件及来源哈希。门禁在四种优化等级、两种链接顺序下验证旧 value/ref/callback 约定被拒绝，Driver 和裸 ArkLink 都保留已有输出。

### 当前验收记录

2026-10-04，final14 完成整体验收，汇总记录为 `build/selfhost-types/final14/completion.json`（`complete: true`）。本轮包含 112 个 SelfHost 源文件；各项验收的源码、工具和输入前后哈希保持不变。

|验收|实际结果|记录|
|---|---|---|
|完整 SelfHost 套件|688 项通过，5098.47 秒，无跳过|`build/selfhost-types/final14/full-suite/report.json`|
|标准库契约|59 项在 O0–O3 全部通过|`build/selfhost-types/final14/standard-library/report.json`|
|fresh Stage 2 原生验收|448 项通过，644.382 秒|`build/selfhost-types/final14/fresh-bootstrap/generations/report.json`|
|fresh Stage 3 原生验收|448 项通过，636.020 秒|同上|

Stage 2、3、4 的 KRO 逐字节一致，SHA-256 为 `70f64291157e24073d1a34837f7387fbe7e25be3ae2716a7e8d310bc4b7173ab`。完整套件和两代验收合计运行 60 个压力进程，各执行 25000 轮；测量区间内的 RSS 和 VmSize 增量均为零。具体测量值保存在汇总记录中。

旧编译器 ae82 与最终 Stage 2 dd7c5 的 ABI2 双向混编记录为 `build/selfhost-module-abi/mixed-abi2-final14/report.json`。O0–O3、两种编译方向和两种链接顺序共 16 次实际执行全部成功；8 对完整 ABI2 manifest 各为 927 字节，逐字节相同。样本覆盖普通 struct、含 wide 字段的 struct 及 struct callback，没有直接 wide 参数或返回边界。报告保存完整工具和源码 SHA、准确命令与产物；输入前后保持一致。

默认 Stage 2 和 ArkLink 已安装并切换，安装记录为 `build/selfhost-types/final14/default-installation/report.json`。空 PATH、未设置 `SELFHOST_COMPILER` 的默认入口程序验证结构体、装箱及 foreach，输出 `63` 并正常退出；记录为 `build/selfhost-types/default-entry-smoke14/executed.json`。这些结果证明所记录的功能和契约，不表示与 Re.KrtC 全部功能等价。

阶段 14 的局部验收已通过 Access、ModuleAbi 和 Project 三个模块，以及标准库数据对象的 O0–O3 用例。本地记录 `build/selfhost-module-abi/candidate14-scoped/` 保存工具 SHA、准确命令、输入前后 SHA 和日志；该局部结果与 final14 整体验收分别记录。

历史 9f 记录验证了当时的跨模块行为及 ABI2 双向兼容。阶段 10 的整合验收暴露了内部原型与数据对象回归；阶段 11 的局部报告保留了模板内部辅助函数查找和项目符号假设的失败证据。这些历史工具、输入和结果保留原状，当前结果使用阶段 14 的独立记录。验证范围以实际执行的用例及产物报告为准。
