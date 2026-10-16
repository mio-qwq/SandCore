# SCO v1 与原生 SCLD

2026-10-16，M10a2 Studio2/SDK 独立工具链实现。此目录属于待主代理整合的草稿，不改变正式 user/kernel/tools/Makefile，不代表客体或整个 M10a2 验收完成。

原后端把每个函数及时变成真实 x86；TU 结束时保留 Symbol、Fix 及初始化来源 span。SCLD 只需这些保留信息，不需要重新解析 C。对象模式复用 shared 的只读来源区间、声明兼容与静态绑定，但不读取库清单，不添加 SCX 入口，不解决其它 TU 的名字。独立 CLI 链接器负责最后布局、跨对象解析、诊断、动态容器与调试副文件，避免把完整链接器塞入只余 19506B 的原编译器。

## 独立编译与命令

```text
sccc A.C A.SCO --object
sccc B.C B.SCO --object
scld PROJECT.SCX --dynamic PROJECT.JSON A.SCO B.SCO
scld LIBRARY.SCL --scl LIBRARY.JSON A.SCO B.SCO
scld PROJECT.SCX --dynamic PROJECT.JSON @OBJECTS.RSP
```

响应文件最大 64KiB，包含由空格、Tab、CR、LF 分隔的对象路径，可用双引号包裹路径。没有转义、注释、嵌套 @ 或隐式搜索。路径相对调用进程 cwd；Studio 应保存经公开 PathResolve 得到的绝对路径。响应文件不包含 C 源码。对象数、全局表、重定位、输出缓冲按实际数量动态申请；参数文本目前受 ScProcessArguments 的 4096B 调用缓冲约束，长对象列表使用响应文件。

每次只编译一个 TU。普通头文件声明可以重复出现，各 TU 的 static 函数、文件静态对象、块静态对象和字串保持独立身份。没有生成包含所有 .c 的聚合文件，没有调用 GCC/ld/Python 代替客体链接。

最终 manifest 采用既有严格 JSON：SCX 的四字段 application 清单及 SCL 的九字段清单。未知/重复键、错误整数、重复依赖/导入/导出、同名多依赖 import、坏库名、无关 import、未声明实际外部引用均拒绝。main 必须为外部定义 signed int(void)；SCL init 为 int(void)、fini 为 void(void)。只显式导出清单项目。SCL ABI 主/次/发布版本及依赖最低版本保持现有含义。

## SCO1MIO 磁盘合同

所有数值为小端 u32，addend 是 32 位补码。只能逐字节读取；不保存指针，不使用宿主 sizeof(struct)。文件最大 16MiB，各对象逻辑内存累计不超过 64MiB。各表规范连续，乘加先做减法范围检查。补丁必须原为四字节零；消费者对补丁排序后拒绝任何重叠。

96B 头：

| 偏移 | 字段 |
|---:|---|
| 0 | 8B `SCO1MIO` 加 NUL |
| 8 / 12 / 16 / 20 | version=1 / header=96 / machine=3 / flags=0 |
| 24 | 精确文件长度 |
| 28 / 32 | section offset / count=4 |
| 36 / 40 | symbol offset / count |
| 44 / 48 | relocation offset / count |
| 52 / 56 | source-file offset / count |
| 60 / 64 | source-line offset / count |
| 68 / 72 | strings offset / bytes |
| 76..95 | 五个保留零字 |

顺序为头 → sections → symbols → relocations → source files → source lines → strings → 零补齐 → 节正文。字符串区非空，末字节为 NUL，引用必须指向字符串起点；符号采用 SCL 的 ASCII identifier/127B 规则，源路径是可显示的非控制字节串。不同对象不合并字符串，源路径不授予文件访问权限。

四个 24B section 记录固定按 TEXT=1、RODATA=2、DATA=3、BSS=4 排列，空类也保留记录。六字依次是 kind、file_offset、file_bytes、memory_bytes、alignment、access。access 分别为 RX=5、R=1、RW=3、RW=3；只接受 1..4096 的二次幂对齐。初始化节 file_bytes 必须恰等 memory_bytes，BSS 必须 file_bytes=file_offset=0。空节 file_offset=0。非空 payload 按节顺序、不重叠、4B文件对齐，之间只可零补齐，最后正文恰达文件末。SCCC 当前语言实际自然对齐仅 1/2/4，writer 的节对齐为 4；reader/linker 已保留 16..4096 的合法对象对齐。

32B symbol 记录：name_offset、binding、kind、section、value、size、ABI_shape、flags。binding 为 local=0/global=1；kind 为 function=1/data=2；section=0 表示 undefined，value 必须 0 且绑定必须 global。定义的 value 是所属节内偏移，完整 size 必须在节内；函数只在 TEXT，数据不能在 TEXT。没有 COMMON/weak/absolute/TLS 等隐含含义。

flags 的 bit0 表示 signed int(void) 可入口/初始化，bit1 表示 void(void) 可终结，两者互斥，bit2..7 保留零，bit8..20 保存自然 alignment 数值，其余必须零。函数 alignment=1；数据来自实际 C 类型自然对齐。定义 value 必须满足自然对齐，section alignment 不小于它。未定义数据保存所需最小大小，不完整数组保存一个完整元素大小；解析时实际定义必须足够大且足够对齐。

ABI_shape 是函数 cdecl 形状的 FNV1a32：递归保存返回/按值参数类型、字节宽度、signed/unsigned、参数数量/变参、数组长度和按值结构的成员偏移/形状；指针只记录4B指针类别，避免沿递归结构展开。它发现实质传参/hidden-sret布局冲突，**不是完整C类型相等证明，也不是安全摘要**。数据 ABI_shape=0，通过 kind/minimum_size/alignment 检查。不同 TU 必须仍使用同一个正式公共 C 头；合法原生聚合 ABI 沿现 SCCC i386 memory-return/callee-pop hidden pointer，不改变已有标量或 sret 发射。

24B relocation 记录：patch_section、patch_offset、type、target_kind、target、addend。ABS32=1，PC32=2；target_kind symbol=0 时 target 为本对象 symbol 下标，section=1 时 target 为本对象 1..4 节号。patch 必须完整落在初始化节内，不能在 BSS。S 表示最终定义或节基址，A 为补码加数，P 为实际补丁地址：ABS32=(S+A) mod2^32；PC32=(S+A-P) mod2^32。writer 从旧 F_REL 的内部 addend 恰减4一次；数据/字串指针的基础节内偏移进入 A，保留一过尾和负加数，不能把相邻来源区间猜成另一个对象。

16B source-file：path_string、source_bytes、source_FNV1a32、reserved0。哈希覆盖本次实际读入、不含附加 NUL 的原文，原生编译器本来禁止原文内 NUL。FNV 只提示编辑过的源码，不提供安全或装载身份。

16B source-line：section=TEXT、section_offset、source-file index、line_number。行号从1开始、offset 递增且小于真实 TEXT 正文。每次真正发射语句前保存当前 code_used，函数入口也登记；同一位置取最后实际语句行，相同连续行不重复登记。它是实际代码位置的语句行表，未承诺变量位置/表达式单步/DWARF。

## 最终链接与对齐

全局符号采用动态开放寻址哈希表。先收齐定义和声明，任何重复外部定义明确列出两个对象；local 不进入全局表。未定义引用先解析其它对象的实际外部定义，再严格匹配 manifest 动态导入；缺失符号、函数形状、数据大小/对齐、导出kind或静态逃逸均在输出前拒绝。

四类节按类型和对象输入顺序布局，每个对象节单独对齐。SCL 各类别从页边界开始、memory 页补齐；既有 read-only text/rodata、独立可写 data/BSS 及 init/fini 合同继续。所有内部/外部 ABS32/PC32 交既有 SCL 运行时处理。SCLD 完成输出后调用未改动的共同 scl_validate。

动态 SCX 保留原36B头、0x400000固定基址、64B私有CRT、128KiB栈、262108B初始化载荷上限和0x3d0000私有映像预算。内部引用直接回填，实际外部引用进入 SCD 表；没有把依赖库或装载器代码静态复制进应用。全部初始化节和 metadata 布局完成后，把 strings 末尾补零使 **最终 load 对齐到 BSS 组所需最大alignment**，再将 BSS 的所有节/符号/内部重定位目标统一移动。共同 scd_validate 保持不变。

单源 `--dynamic` 的最小补丁仅向 SCD 字符串末尾补0..3B，保证当前 SCCC 的4B自然BSS对齐；原reader已经允许此尾零，未更改版本/字段/旧历史文件。SCO 链接器单独落实最高4096B的记录对齐。不能把单源补4B宣称所有显式/SIMD对齐已实现；当前SCCC语法没有新增 aligned 属性。旧 `--scx`/SKM 路径没有布局变化。

## SCDB1MIO 调试副文件

公开常量为 SCDBGFILE.H，与 Debug2 采用同一合同。默认输出为 **完整输出路径追加 `.sdbg`**，例如 PROJECT.SCX.sdbg、PROJECT.SCL.sdbg，避免同目录同stem应用与库互相覆盖；文本图为追加 `.map`。

96B头：magic `SCDB1MIO` 8B；8 version=1；12 header=96；16 machine=3；20 address_kind，SCX=1绝对VA/SCL=2 RVA；24 完整image文件长度；28 object_count；32..63 完整image SHA256；64/68 symbols offset/count；72/76 files offset/count；80/84 lines offset/count；88/92 strings offset/bytes。表规范连续且字符串恰达末尾。

symbol24=name_string、address、size、kind、binding、object_index。包含全部真实定义，包括私有函数/数据；(object_index,name) 保留私有身份，不能仅按同名文本查找。source-file20=object_index、path_string、原文bytes、FNV1a32、reserved0。line16=address、全局source-file下标、line、object_index。行地址随最终节布局计算，SCL为RVA、SCX为固定VA，包含BSS的符号地址使用最终对齐后的load。

SHA256 在 SCLD 内部用自写整数实现，绑定完整最终 SCX/SCL 字节，与宿主 hashlib 对照。副文件不代替运行期模块身份、映射权限或进程访问授权；Debug2 从内核提供的真实模块SHA/段快照校验image身份，再逐项检查symbol/line是否落在正确段中。可变磁盘上的现文件不能冒充已装载身份。

## I/O、生命周期与结束条件

原生 SCLD 仅通过公开 SC32API 与 SCCLIB 使用用户态文件、内存、输出流和参数；不包含 SCAPI.H，不手写 syscall，不取得 SKM/ROOT 权限。标准库/内核 ABI 无改动。原生 C 源可由现 SCCC 单源 `--dynamic SCLD.json` 引导，随后它自己链接后续独立 SCO；没有必须依赖新的原生链接器才能先生成链接器的环。

语义、格式、容量、别名、跨对象解析与共同容器验证全部完成前不打开任何输出。源对象、manifest及响应文件不得与正文/map/sdbg别名。先写debug/map，最后正文；**当前SandFS/SCCLIB没有跨三个文件原子事务**，磁盘I/O失败可留下候选副文件，SHA不匹配则Debug2必须拒绝。坏输入保留既有正文；不能把此规则扩大为磁盘故障下的原子替换保证。

对象/工作表在进程期持有稳定指针；先固定 Object 数组再建立 Symbol.owner，避免动态增长留下悬空owner。成功路径释放三个大候选输出，普通退出/失败由SCCLIB与既有进程生命周期回收其余所属内存；不维护跨调用全局缓存。没有重复生成磁盘镜像或测试盘。

本轮直接检查结束于：分别编译真实TU、原生C链接器生成合法SCX/SCL、源/符号/重定位/保护/对齐正确、失败保留、真实后端可生成独立SCLD与编译器且旧容量够用。SandCore Windows QEMU 的实际CLI/stdio、对象编译、运行/退出回收、Studio集成与旧程序回归由主代理在整合后做最小有效客体验证，本目录不伪写这些结果。

修订：2026-10-16，SCO/SCDB二进制合同与实际C实现收齐；真实后端分别编译/链接及Linux i386函数直接执行通过，客体工具运行与整合验收待主代理。

## SCCC 源身份数量（2026-10-16）

源元数据取消固定64项门槛，SCO v1的source-file/source-line字段与版本不变。
SCCC内部where保留语句1/文件索引15/行号16位，最多32768源身份，写入第32769项前
明确拒绝；SCDB的全局文件下标仍由真实对象源表重新定位。路径和内容不作去重，
同名私有符号、不同路径身份和实际源行保持。动态拥有/OOM/来源证据见
[SCCC源容量](SCCC-SOURCE-CAPACITY.md)；本次仅增加宿主后端证据，客体待主代理。

修订：2026-10-16，补充动态SCCC源表与现有15bit位置编码边界，未修改SCO/SCDB磁盘格式。
