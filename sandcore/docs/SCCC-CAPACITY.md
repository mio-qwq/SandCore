# SCCC 大翻译单元工作区

2026-10-15：本批源码已实现、静态核对，**尚未构建、尚未运行新 G1 或 SCUI 原生编译**。旧标准库客体证据仍对应其原输入，不覆盖本批容量改动。实现范围为 `user/s3c.c`、`s3c_pp.inc`、`s3c_emit.inc`；不改 C1..C5、SCL/SCD 格式、旧 SCAPI 或装载器。

## 1. 为什么改工作区

SCUI 完整正文、控制、编辑器和对话框继续增长，已经超过旧 SOURCE 262144B。把这些区直接改成更大的全局数组，会使编译器自身 G1 的 BSS 超过旧 SCX 的私有映像边界。此次把四个大区迁入本人 `sc_alloc` 高端私有堆，并为真实逼近边界的 include、宏和 map 表留裕量；不删库功能、不用宿主产物替代原生后端。

编译器是保留的自举工具，继续使用既有 SCAPI 私有桥。它不依赖 SCCLIB，不把页分配冒充通用 malloc，也不把这个例外扩给新应用或系统库。

## 2. 固定地址与容量

| 区域 | 旧预算 | 本批预算 | 保存位置 |
|---|---:|---:|---|
| Source | 262144B | 1048576B | 本人高端堆，一次取得 |
| Token | 65536 条、每条 12B | 262144 条、3145728B | 本人高端堆，一次取得 |
| Pool | 196608B | 2097152B | 本人高端堆，一次取得 |
| Code，SCX / 动态 SCX / SKM | 262000+36B | 262000+36B | 本人高端堆，一次取得 |
| Code，SCL | 与旧 Code 共用 | 1048576+36B | 本人高端堆，一次取得 |
| include 记录 | 32 条 | 64 条 | BSS；每条 Source 72B，另有 once 标记 |
| macro 记录 | 384 条 | 512 条 | BSS；每条 Macro 56B，另有 busy 标记 |
| map 文本 | 32768B | 131072B | BSS；全部 writer 仍用同一 `sizeof(map_text)` 检查 |
| Symbol / storage / sizes 按ID表 | 6144 项，合245760B | 16384 项，合655360B | BSS；新增409600B，见§7实际诊断 |

Source 预算是所有实际装入路径的正文加每源一个 NUL，包含未启用条件分支的原文件字节。Token 预算留一条 EOF，因此最终有效 token 最多 262143 条。Pool 的记录仍为 12B 原有头、正文和一个 NUL，offset、字面量原始拼写及整数值的布局不变。Code 仍从 `code+36` 开始写；36B 前缀只作为编译器共用暂存，不改变任何最终格式头。

`workspace_acquire()` 在参数、路径及副产物身份检查之后、首次 manifest intern 和预处理之前取得四块。每块的申请失败立即停止后续申请、释放已取得候选再 fatal。成功编译在 `finish_image()` 的正文、map、日志处理返回后才 `workspace_release()`，随后界面/CLI只使用自己的结果副本。取消和其他 fatal 仍调用 EXIT，由内核按本人页目录回收工作区及原有清单候选。

在 PP、parse 和 emit 期间不 realloc、不搬动、不提前释放。递归 `Lexer.source` 借用 Source；token、字符串和语法树中保存的池 offset 在整个翻译单元期间有效。四块均是固定基址的有界工作区，不是增长过程中可能让借用指针悬空的容器。

## 3. 真正保留的输出边界

工作区可以容纳更大输入，不表示每种容器可以容纳更大输出。

| 输出 | 继续实际检查的合同 |
|---|---|
| 旧 SCX1 | `CODE_MAX=262000` 保持；对齐后的 code+DATA 必须在该限制内；load+BSS ≤ 0x3d0000；36B 头、固定 0x400000、24B CRT、128KiB 栈不变 |
| 自动动态 SCX1 | 原 `finish_dynamic()` 的 CODE_MAX、code+DATA+SCD1 metadata 和 load+BSS 检查不变；64B CRT、旧容器头和固定地址不变 |
| SKM1 | code 工作区仍用旧限制；最终 load 和 BSS 分别 ≤ 131072B、reloc ≤ 8192、禁止 icon；32B 头和回填行为不变 |
| SCL1 | code 工作区 1MiB；初始化 DATA 工作区仍 131072B；最终文件 ≤ 16MiB、映像 ≤ 64MiB，段权限/排序/表与共同 validator 保持 |

`task_loader_stage()` 仍为 262144B，`image_scx_info()` 的 load 最大 262108B。SCCC 的 262000B 初始化载荷比加载器边界更严格，本批没有扩大它。加载器先校验容器，再只读载荷进 Stage；合法独立 SCB2 图标可以使总容器超过 262144B。因此不新增错误的“SCX 总文件必须小于 Stage”检查。图标仍不进入程序地址域、不改变 BSS 起点或重定位。

SCL 格式的 64MiB 映像上限也不是本编译器所有对象的生成上限：现全局 BSS 构造器仍逐项限制 0x3d0000，DATA 仍 128KiB。这些已有生成限制明确保留，不能借本批 Source 扩容宣称完整 64MiB 原生生成能力。

## 4. 页预算与自举映像

SCX/动态/SKM 四块合计请求 6553492B，按现 4096B 私有页分配器取整为 1600 页、6553600B。SCL 四块合计请求 7340068B，取整为 1793 页、7344128B。SCL code 的额外 36B 会占第 257 页，不能把它隐去称恰好 7MiB。以上只算四个工作区，清单表、最终容器、页表和编译界面还需自己的真实资源；OOM不保证某个输入一定编译成功。

旧四大 BSS 数组合计 1507220B。新四个指针和 `code_capacity` 增 20B；Source/once 增 2432B；Macro/busy 增 7680B；map 增 98304B。按 i386 既有记录尺寸，净减少 BSS **1398784B**。

历史已验 G1 的 load234065B/BSS3591864B、code217510B/DATA16553B 是 [标准库原生证据](../evidence/m10a2-stdlib/README.md) 的原输入记录。只替换上述工作区和表容量的静态推算 BSS 为 2193080B，低于 0x3d0000=3997696B；新诊断/生命周期函数还会增加 code/DATA。因此这个推算不能代替新 G1 的实际头、运行或页回收证据。GUI 自身有不同附加工作区，也须看其最终产物。

本批不增加每 token/Type/Node 的成员，避免记录尺寸同时放大所有宏、行和 AST 缓存。申请只在编译开头发生，逐 token/逐函数不新增分配。内核现 `sc_alloc` 会逐页清零，一次性 6.25MiB/约7MiB 申请的实际延迟尚未测量，不据静态预算宣称性能达标。

## 5. 本次文本快照及剩余限制

以下为 2026-10-15 04:37（UTC+8）对现物理文件的只读 include 图统计；CLI/GUI各排除另一界面分支，头搜索按当前 user、stdlib、sclib、runtime、textdata。没有运行预处理器或编译器。路径别名在真实 Source 表中仍可能形成另一记录，条件分支、宏 undef/redefine 和宏展开也会使实际计数不同。

| 主源 | 唯一物理文件 | 正文字节 | 每源一个 NUL 后 | 宏名并集含 11 预定义 |
|---|---:|---:|---:|---:|
| sccc.c / CLI | 16 | 263341 | 263357 | 269 |
| s3c.c / GUI | 18 | 297051 | 297069 | 268 |
| scui/SCUI.C | 32 | 522870 | 522902 | 302 |
| sc32/SC32API.C，含 Broker/Drop | 30 | 158052 | 158082 | 376 |

四图 SHA256（排序后的相对路径、NUL、原文件字节串）：

```
CLI   c797fd1c6585c0fe14c62cf8c900af1a4bc2c82afdf06bfdcbbda7e5db11e6a6
GUI   1a6185e19f79694cf7c571586e10d075a1c31cc57a4529d60f183d4e9f894110
SCUI  9c39db9e7cb1bf681f12926ef660dc6c995fc2fde2552b645b03e336ac5f4e67
SC32  866f07bdce4f60014fa7d0c2400957708bc0e2932206693227a9a8a53baddcaf
```

SCUI 部分仍由其 owner 继续写，以上不是冻结整库身份。较早 519523B 快照的未展开源 token 粗数约144625，只用于选择 262144 条预算，不称实际 `token_count`。同快照约495个函数定义，仅按 SYS/SRC/SCUI 来源行估算函数 map 已约26420B，156导出与导入/依赖再追加，故32KiB缺少正式输入裕量。扩为128KiB后仍保留溢出检查，不默默裁掉符号行。

其他未改的真实限制包括 IDENT4096、TYPE4096、PARAM4096、NODE8192（每函数复用）、FIX12288、DATA131072、宏正文8192 Tok、逻辑行1024 Tok、宏参数8×128 Tok、递归深度16、源路径63B；Symbol最新16384项由§7实际6144失败推进。宏名并集不是 actual macro 槽峰值：undef 后同名 redefine 可能另占槽，宏正文也累积不回收。剩余某表若被完整正式输入耗尽，应据其具体诊断处理，不能删 SCUI 功能或把源码拆成假导出占位。

## 6. 诊断与接续验证

Source、Pool、最终/临时 token 输出、include、macro、macro body、identifier、machine code 与普通 symbol map 的新容量诊断给出 area/used/need/limit；这些计数按 u32 十进制打印。Source `need` 是原文件正文长度，检查另外留一个 NUL且先比较剩余长度，避免 `info[1]+1` 在 UINT_MAX 回绕。Pool `need` 包括原12B头和NUL。每处均先检查再写，没有越界写后才报错的路径。

参数/路径拒绝仍发生在工作区取得之前。共享/动态模式原有输入与 output/log/map 身份保护、ROOTLESS一次解析、map失败不提交正文继续。容量 fatal 在 finish 前终止，旧成功正文不被容量失败替换；本批不重排旧 SCX/SKM 的日志/map写入合同，也不宣称新的磁盘原子事务。

本次只完成源、include/容量算术与生命周期静态审阅，以及尊重原 CRLF 的差异空白检查。由 Root 统一构建后，只针对这次改动取得：新 SCCC0 的实际映像、其真实生成并执行 G1、G1 对完整 SCUI 的 actual source/token/pool/各表/code/DATA/BSS/map结果和正常退出回收。SCL正文仍由原共同validator核验；旧代表程序只按受影响范围复用或定向比较。未发生新失败不扩旧大矩阵。

## 7. 完整SCUI实际触发Symbol容量（2026-10-15）

主代理HOST真实后端预检03已解析/生成AGGCALLEE与AGGPROBE返回0，随后完整SCUI在 `ui_edit.c:1531` 触及累计 `SYMBOL_MAX=6144`；尚未进入该新轮VM。这不是删库或另写薄实现的理由。本次仅把 `s3c.c` 的SYMBOL_MAX改为16384，Symbol与`scl_symbol_storage/scl_symbol_sizes`两个旁表同源扩展，其它TYPE/PARAM/FIX/IDENT等caps和全部ABI片段保持冻结。

按i386实际定义，Symbol为8个int共32B，两个旁表各4B；静态增量为 `(16384-6144)*(32+4+4)=409600B`，恰100页。新总表655360B，不把符号移到可能中途搬动的堆，也不提前复用仍被Type/AST/fix引用的编号。此编译器有限TU profile不等于无限符号，超16384仍明确失败；目标运行时对象/任务没有因此添加数量门槛。

本次只读主代理 `ui-build-09` 的真实SCCC0容器：load132501B、BSS2193323B、stack131072B；源码改宏后的BSS静态推算2602923B，沿当前load的组合2735424B，小于旧 `0x3d0000=3997696B`，余1262272B。这个3d边界为低映像留出既有栈/异常区，不将stack再次混进header BSS或暗扩大loader窗口。

此推算仅是此次Symbol增量的安全预算，不是新SCCC0/G1真实头、完整SCUI或页回收结果；新宏可能影响code/layout，仍须主代理统一构建/真实HOST state计量及实际客体自编译确认。新的host工具将于失败/成功都记录types/symbols/params/fixes实际峰值；下一真实诊断才决定其它容量变化，不预先把全部表扩大，不再用QEMU定位这些前端容量错误。

## 修订

2026-10-15：实现固定地址的本人 Source/Token/Pool/Code 堆工作区、成功/失败生命周期、实际 include/宏/map 裕量与无符号容量诊断；保留旧 SCX/SKM/SCD 及 SCL 格式边界。源码及静态预算已收齐，新构建/G1/SCUI原生执行尚未验证。

2026-10-15：依据HOST03完整SCUI在ui_edit1531触6144的实际失败，只扩Symbol及两旁表至16384，记录409600B增量与build09旧头静态预算；新完整SCUI/state/native结果待主代理统一验证，其它caps/ABI冻结。
