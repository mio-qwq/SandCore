# SCCLIB 标准运行库设计与兼容范围

2026-10-15，**SandCore M10a2 — The Application Platform Era**。

本文件是 Phase 3 的正式设计输入，状态为**设计与现有源码审计收齐，标准库尚未实现、构建或客体验证**。它不把已实现的 SC32 文件接口称作 `stdio`，也不把 Phase 1 动态装载证据扩大到 SCCLIB。优先目标完整保留本地开发合同 §4.1：分配器、字符串、缓冲文件、格式输出、转换与算法、基础标准头，以及时间、区域与数学的后续支持。

## 1. 标准目标、边界与实施顺序

首个可发布范围是“**C89 基础函数语义 + 明确选择的 C99 功能，32 位整数、字节流、C locale**”。这个名称描述函数兼容清单，不宣称 SCCC 是完整 C89/C99 编译器或 SCCLIB 是完整 hosted ISO C 实现。C89 本身已有浮点、聚合返回、信号等要求；不能因为没有 `long long` 就称为完整 C89。C99 的 `snprintf`、`va_copy`、`stdint.h`、`stdbool.h` 是用户优先目标中的增量，不因选用 C89 基础而删除。

| 顺序 | 要交付的实际行为 | 必须先解决的条件 |
|---|---|---|
| P3-A 基础 | 四个分配函数、完整字节字符串组、整数转换、ctype、errno、assert、排序查找、限值与尺寸头 | 公开头与实际导出一致；整数常量和 `offsetof` 不能静默产生错误值 |
| P3-B 文件与格式 | 六模式缓冲 FILE、标准流、定位、EOF/error、单字节回退、整数/指针格式输出及 `v*` 组、`snprintf` | SC32 文件底座实际通过；真实变参/参数转换；根正常退出接线；不能仅声明函数 |
| P3-B 基础 C99 增量 | 真正 `_Bool`、`bool/true/false`、`va_copy`、实际宽度的固定整数类型与宏 | 新编译器能力先生成、执行，再发布相应头；完整 C99 仍未承诺 |
| P3-C 标准覆盖推进 | 格式输入、聚合结果、环境/随机、C locale、时间、setjmp/signal，随后浮点/64 位与数学 | 各行能力具备后单独更新清单，不用失败桩代替实现；不是把用户优先项推迟到此阶段 |

P3-A/P3-B 是同一批优先库的内部实施次序；`printf/fprintf/snprintf`、`stdarg.h` 和 `stdbool.h` 必须在优先批闭环。P3-C 保留完整规范目标与尚缺条件，不能把本表当作削减 M10a2 的批准。每个条目分别记“设计、源码、宿主生成、客体 SCCC 生成、客体执行、已发布版本”；只有真实完成的功能进入导出表和能力宏。

当前不支持浮点类型、64 位 C 整数、复数、宽字符 I/O、TLS、C11 原子/线程。`ScWideCounter {lo,hi}` 是版本化 SC32 数据记录，不是 `uint64_t`；使用两个 32 位成员不能宣称支持 `%ll`、C 整数提升或 64 位算术。后续标准扩展也不会把内核变成 libc 或浮点执行者。

## 2. 动态库、SDK 与 ABI

正式产物为 `/SYS/LIB/SCCLIB.SCL`，公共标准头保留小写 ISO 名称，安装在 `/SYS/INC/`。用户只包含标准头，普通调用在运行期导入 SCCLIB；没有应用私有 `.inc` 正文、静态复制分配器、手写 syscall 或包含 SCAPI.H 的要求。依赖方向仍是 `SCLDR → SC32API → SCCLIB → SCLIB → SCUI`，SC32API 的私有桥保持 freestanding，不能反过来调用 malloc/printf。

SCCLIB 的库代码及初始化数据可以作为同一文件产物复用；当前每进程有私有可写状态和唯一运行时模块身份。堆、FILE 链、errno、`strtok` 游标、随机状态和退出函数链都属于本进程的一份 SCCLIB，不能放入跨进程静态内核旁表。没有已实现的线程/TLS 合同，所以首批不宣称多线程可重入；跨进程隔离是必须验证的实际行为。

### 2.1 ABI4 与宿主引导

i386 cdecl 的参数四字节槽、右到左入栈、caller 清栈、EAX 返回整数/指针、EBX/ESI/EDI/EBP 被调用方保留，沿用 [SCL.md §6](SCL.md#6-调用装载与生命周期)。通用入口最低栈对齐为 **4B**；自动 SCX CRT 的 16B 引导调用不代表后续每个 C 调用都 16B 对齐。分配器返回 16B 对齐与函数入口 ABI4 是两件事。

宿主 GCC 引导同一库源沿正式 SDK 的 `-mincoming-stack-boundary=2 -mpreferred-stack-boundary=4`，接受 ABI4 并在需要时内部对齐；不能用暂时没有 SSE 掩盖错误的栈假设。SCCC 编译出的调用与回调也遵守 ABI4。首批不改旧 SCX/SKM 或旧 SCAPI 寄存器约定，不随意引入 hidden aggregate return 参数。

`size_t = unsigned int`、`ptrdiff_t = int`，均为 **32 位/4 字节**类型；`long/unsigned long` 也是 32 位。字节为 8 位，plain `char` 有符号，short 为 16 位。标准头可与 `SCTYPES.H` 同时包含，但标准头不公开 `ScFile`、wire、PID 或内核布局。共用一个内部尺寸类型头与独立 include guard，不能靠重复 typedef 的偶然接受掩盖不兼容声明。

`FILE` 为不完整结构的 typedef，应用不得依赖内部成员。`stdin/stdout/stderr` 宏通过库内访问函数取得三个稳定控制对象；访问已经关闭的标准流不会再次 `ScFileGetStandard`、复活本 PID 的弱 proxy 或借子进程引用保活。`errno` 宏展开为库访问函数返回的可修改 `int` 左值；不在每个应用生成独立 errno 副本，也不强迫应用使用可写数据导入。辅助访问函数属于实现保留命名空间，必须有真实 manifest 导出。

### 2.2 头与导入的共同交付

库 manifest 仍为严格九字段 SCL，应用仍为独立四字段 dynamic manifest。依赖条目声明实际最低版本，所有外部调用和外部数据引用都有匹配的导入；未使用的标准声明不强行产生导入。标准函数的实体必须可取地址，例如 `(memcpy)`、`&strlen`、比较器函数指针；宏优化不能只保留内联分支而删除实体。

SDK 增加真正的小写标准头安装规则，不能只沿用当前 `user/*.H` 的大写 glob。现编译器 `pp_include` 能解析引号和尖括号，但都先查源目录，再查 `SYS/INC/`；正式标准头查找必须审查非根 cwd 下的根目录身份、现有私有同名头与显式 SDK 选择，保持一次解析，不将 rootless 路径再次作为 cwd 相对名。旧源码路径行为不在本设计中悄悄改变。

`user/audio|codec|compress|pack/include/` 已有第三方组件专用同名头，它们不是公共 SCCLIB 头：有的只有私有回调适配，有的依赖 GCC `long long`。保留这些来源/许可和旧构建隔离，不合并入 `/SYS/INC` 后冒充 SCCC 标准支持，也不复制第三方库实现。

能力只用项目专用、版本化宏说明，如 SCCLIB profile 与已交付的浮点/64 位功能；具体名字随真实 SDK 冻结。SCCC 现有 `__STDC_HOSTED__=0` 保持诚实；不能为了让上游头启用分支而伪造 `__STDC_VERSION__=199901L` 或完整 hosted 环境。

## 3. 当前 SCCC 源码审计与必要增量

本次按工作树实际源读取，HEAD 为 `72783199e3d2e987d4124a89b45913a4290937a2`，后续 SC32 源增量尚未由该 HEAD 表示。以下是静态事实，不是新增编译执行证据。

| 实际入口 | 当前行为 | 对标准库的影响 |
|---|---|---|
| `s3c.c:73`、`s3c_parse.inc:759` | 类型仅 VOID/CHAR/SHORT/INT/PTR/ARRAY/FUNC/STRUCT/UNION | 没有 TY_BOOL、浮点或 64 位类型；long 与 int 使用同一基础类型 |
| `s3c_parse.inc:186`、`:310` | 解析 int/char/short/long；long long 与 float/double 拒绝；type-ahead 不含 `_Bool`/restrict | `typedef char bool` 不是真 bool；C99 restrict 语义不能假称已实现 |
| `s3c_parse.inc:235`、`:395` | 能记录 `...` 和 named count，调用只核参数数目 | “能写省略号”不等于具备 stdarg；缺 prototype 参数转换和调用参数类型拒绝 |
| `s3c_parse.inc:799`、`s3c_emit.inc:451` | named 参数偏移 `EBP+8+4*i`；每个实参求值后 push EAX | 没有不同参数宽度布局；aggregate 实参不能默默压地址冒充按值参数 |
| `s3c_parse.inc:73`、`s3c_emit.inc:94` | 运算中 char/short 提升为 int；cast 只处理一/二字节符号扩展 | 需把提升放到 unnamed 调用参数；布尔赋值/初始化/返回必须规范到 0/1 |
| 全部 parser/emit/pp 搜索 | 没有 `va_start/va_arg/va_end/va_copy` 或对应 builtin | 当前不可发布会读取标准变参的 printf/vprintf 实现 |
| `s3c_lex.inc:134` | u32 累乘无溢出预验，连续 U/L 均吞掉，不区分 LL | `4294967296`、LL 等可能绕回/降宽；必须诊断，不能拿字面量假实现 int64 |
| `s3c_emit.inc:473` | 常量求值不接受一般 address/member 表达式作为整数常量 | 常见空指针成员地址宏不足以保证标准 `offsetof` 是整数常量表达式 |
| `s3c_parse.inc:257/:271` | 结构/联合按值参数与返回拒绝 | `div/ldiv` 的标准结构返回被阻塞；不能改成输出参数仍叫同名标准函数 |
| `s3c_emit.inc:604/:840` | asm 仅固定 a/b/c/d/S/D 约束与明确指令白名单，含 asm 的函数保存 EBX/ESI/EDI | 不支持完整 GCC `r/m`、statement expression；不能用脆弱读 EBP 宏代替变参机制 |
| `s3c_pp.inc:114/:118/:517` | 有 `__LINE__`、`__FILE__`；没有 `__func__` 或 STDC_VERSION | C89 assert 的源位置能取得；选择 C99 assert 前需真实函数名支持 |
| `s3c.c:26..47` | 当前 MACRO_MAX=384、SOURCE_MAX=262144、FILE_MAX=32；其余仍有明确容量 | 新 SDK 源/宏必须按真实输入计数；本设计不擅自扩容量或回用旧320数字 |

### 3.1 优先批的编译器工作

| 编号 | 必须实现的具体行为 | 最小实际验收出口 |
|---|---|---|
| C1 调用转换 | 有 prototype 的固定实参按目标类型转换；unnamed 实参执行整数提升、数组/函数衰减；未支持的 aggregate/FP/64 实参显式拒绝，保留一次求值和现栈顺序 | 普通、函数指针、嵌套调用与库边界传 signed/unsigned char/short，布尔及指针；不能仅核数量 |
| C2 stdarg | 解析四个 builtin，`va_arg(ap,type)` 接受真正 type-name；va_start 只在 variadic 定义内，对正确最右 named 参数建立扫描点；ap 左值只求值一次；copy 独立状态、end 生命周期 | SCCC 生成的 variadic 函数真正遍历正负整数、无符号高位、指针；`v*` 与 va_copy 两遍遍历；错误类型诊断 |
| C3 布尔 | 新 TY_BOOL/真实 `_Bool`，size/align1；常量、存储、cast、参数、返回、自增/复合赋值把整型或指针非零转为1，提升后为 int | `_Bool b=256`、`b=-2`、低字节为0的非空指针仍为1，初始化/数组/结构字段/函数边界一致 |
| C4 常量/offsetof | 扫描乘加溢出；合法后缀和类型选择；现未支持 LL 明确拒绝；真实 offsetof builtin 用已建成员布局产生 ICE，禁止 bit-field/不完整类型 | UINT32_MAX/LONG_MIN 宏、数组界、枚举/静态初始化中的 offsetof，越界字面量保留旧输出 |
| C5 SDK/诊断 | 真正能力宏、guard 与有限递归；调用未声明标准函数不可静默错用隐式 int；C99 `__func__` 支持后才宣告对应 assert 范围 | 实际标准头与 SC32API/SCUI 混合翻译单元，在系统内读取同一 SDK 并生成运行 |
| R1 正常终止 | 根受控退出点与 prepare hook，使 return main 和 exit 汇合；不能在 SCCLIB 调用栈内 Destroy | 返回 main/显式 exit/atexit 回调的顺序与仅执行一次；异常跳过用户回调，详见第10节 |

ABI4 的首批 `va_list` 选择指向下一个参数槽的库/编译器共同游标；参数开始由编译器的 named 参数表计算，不用 `sizeof(last)` 推算。int/unsigned int/long/unsigned long/指针每次推进4。请求 char/short/_Bool 的 va_arg 不是取窄字节的合法替代，实际应按提升后的 int 获取；void/array/function/未支持的 aggregate、FP、64 位 type-name诊断。

`va_start/va_arg` 必须由标准宏进入真实 builtin；`va_end` 与 `va_copy` 同样明确规定状态和一次求值。GCC 引导头使用其真实 builtin，SCCC 使用新增 builtin；共同 i386 布局须实测相互调用。不能把宏中的“把指针加四”当作未经类型检查的完整 ISO stdarg。变参宏本身属于 C99 预处理，和函数变参是两个独立能力。布尔转换与变参生命周期的核对依据为 [WG14 N1570 §6.3.1.2、§7.16](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)，该 C11 草案只用于这些共同规则的核查，不据此宣称 C11 支持。

后续完整语言标准还需区分 int/long 类型身份与 rank、无 prototype 和 K&R 定义、restrict、聚合 ABI、宽整数/FP、setjmp 的特殊控制流等。它们单列，不作为已有优先函数的虚假声明，也不让不相关语言扩展阻塞已可实现的整型基础。

审计源 SHA256（仅来源定位）：

| 文件 | SHA256 |
|---|---|
| `user/s3c.c` | `a184ea2fba298cb78d856de0f51e934749d06c7c7673c6e4ffd4ef070ff01962` |
| `user/s3c_parse.inc` | `b4a109769db27d3421690fd2eec04483f79afb5b3ac6f5b24f2870d4f7c8f933` |
| `user/s3c_emit.inc` | `6b125aa0d42f32cce848193057f51d3663a5f69df3c3d84a43a348ff78470a2c` |
| `user/s3c_lex.inc` | `daac00f4c34383cbb47ec2dfce8fa3e959ef2813c66f647284ab2ab4b75becfc` |
| `user/s3c_pp.inc` | `9ebde1119c4cf4d67d681b79bb51cae79c1a7d79b3682c4a09d977ec83acb871` |
| `user/SC32API.H` | `77888a139be50a53a481c3c6d1cb87d7fff1c18da7d0611d27314021d5455b4f` |
| `user/runtime/scl_application.c` | `714361b36497bf64e6bc8e2a471daf6b2961f6dd8936acd62459838a752b12df` |

## 4. 分配器

### 4.1 语义与尺寸预验

| 函数 | 必须实现的行为 |
|---|---|
| `malloc(n)` | 返回至少 n 字节、16B 对齐、与其它活跃分配不重叠的对象，或 NULL；内容不承诺为0；失败设置项目 ENOMEM。选定 `malloc(0)=NULL`，不视作分配失败、不改 errno |
| `calloc(count,size)` | 在任何加法/乘法/页申请前检查 `size!=0 && count>SIZE_MAX/size`；实际请求全部清为零位，不假设所有语言指针/浮点的零表示都等同于这些位。任一参数0返回NULL且不改 errno；溢出/资源不足返回NULL、ENOMEM |
| `realloc(p,n)` | p为NULL等同malloc；n>0失败时旧对象、内容和归属不变。成功保存旧/新请求尺寸较小者，不复制未请求的块尾部。选定 n=0 时释放 p 并返回NULL；这属于本阶段明确的零尺寸选择，不推广为其它标准版本的普遍规则 |
| `free(p)` | NULL无作用；真实分配起点才可释放一次。内部指针、另一分配器地址、重复释放属调用方 UB，不能因为内核拒绝非法页就声称支持。调试可诊断损坏，不允许继续用损坏链修改任意内存 |

所有 header 加法、16B round-up、页 round-up、arena 大小与 `count*size` 都先检查再运算。内核目前单次页申请1..64MiB、进程 heap VA 总容量64MiB，不把这说成 malloc 可以保证一个完整64MiB payload；元数据、其它 arena 与已占页都会消耗真实空间。整数计算仅使用有依据的 unsigned 回绕规则，不能靠 signed overflow 判断越界。

### 4.2 arena、空闲块与回收

页只作为后端。初始普通 arena 取64KiB，细分为多个块；32KiB以上请求可直接用页取整的独立 arena，避免大对象割裂小块池。配置尺寸是起点，实际性能验收前不宣称最优。64KiB arena 可容纳许多小分配，禁止一个 malloc 对应一次 ScMemoryAllocatePages。

每个 arena 的控制信息在其固定前缀，不能递归调用 malloc 为 malloc 分配元数据。块头32B、块跨度16的倍数，payload起点16B对齐；保存实际请求长度、跨度、前一块跨度、所属arena、状态和空闲链。最小可分裂余块48B。块的边界和 arena 末端先验证，绝不把应用数据当未经检查的 next 指针。

空闲块按小尺寸精细桶与大尺寸对数桶挂链，并用非空 bitmap 跳过空桶。同桶按实际跨度找满足请求的块，分裂后余块入相应桶；不宣称这个 first-fit 的最坏时间是 O(1)，最坏会扫描桶内空闲块。释放通过相邻块跨度做 O(1) 邻接定位，将前/后空闲块撤链后合并，再入桶，避免遍历所有活跃分配。复杂度、桶数、搜索长度、碎片分布由典型小对象和大小交错负载测量记录。

realloc 优先原位缩小并分裂，或消费相邻空闲块扩张；不能改写前一对象的活跃payload。需要移动时先成功申请新块，复制所需前缀，再撤旧块，失败不先free。calloc 只清请求区；普通已复用块不因早先页清零而误判为calloc合格。

arena 完全空闲时回收其整块 Sc32 页申请，保留至多一个普通 warm arena 供复用；独立大 arena 立即归还。不能对 arena 中间页面调用不支持的 partial free。页归还成功后才撤本库控制记录；失败保留可追踪状态。正常库 fini 先清stdio/回调资源，最后释放 arena；异常进程退出由内核 owner 链回收所有页，不能执行损坏堆上的free链。

## 5. 字节与字符串函数

所有比较按 `unsigned char` 值，不能受 plain char 的符号影响。没有“自动验证每个用户指针”的标准库承诺：有效对象、容量、可写性由调用方满足；标准规定的重叠 UB 不会因库名字变成安全复制扩展。零长度请求实现不访问数据，但不把 NULL 变成可移植的合法 ISO 指针参数。

| 函数 | 首批语义与容易误写的边界 |
|---|---|
| `memcpy` | 复制恰好n字节并返回dest；重叠仍UB；已知n的完整块才可用REP |
| `memmove` | 语义如临时副本；真正支持前后重叠与同地址；i386地址比较用于私有实现，不能用跨对象的C有序指针比较假装可移植 |
| `memset` | c转unsigned char重复n次，返回dest |
| `memcmp` | 比较前n字节，第一个不同字节决定正/负；返回大小不是标准必须的差值幅度 |
| `memchr` | c转unsigned char，搜前n字节，允许命中0；无命中NULL |
| `strlen` | 计算终止NUL之前字节数；UTF-8字节数不是字符数 |
| `strcpy` | 包括终止NUL，返回dest；不能悄悄截断 |
| `strncpy` | 最多读取n字节，短源补零至n；长源没有必然终止NUL；n不是dest总容量 |
| `strcat` | 从dest终止处追加完整源及NUL；调用方保证总容量 |
| `strncat` | 至多追加n个源字节，另写终止NUL；n不含dest已有长度，不是strncpy式补零 |
| `strcmp` | 比较至首个不同字节或共同NUL |
| `strncmp` | 至多n字节，遇到NUL停止；n=0返回0 |
| `strchr` | c转char的目标字节，终止NUL也是可查目标；返回首处或NULL |
| `strrchr` | 同上返回最后一处；不能漏掉查找NUL |
| `strspn` | 返回开头连续属于accept字节集合的长度 |
| `strcspn` | 返回开头连续不属于reject字节集合的长度 |
| `strpbrk` | 首个属于accept集合的非NUL字节，或NULL |
| `strstr` | 首个完整needle；空needle返回haystack；不修改输入 |
| `strtok` | 第一次给字符串，随后NULL续取；每次可换分隔集合；跳过连续分隔、不产生空token，修改分隔为NUL；隐藏游标只属本进程，不称线程安全 |
| `strerror` | 依据本库正errno码给稳定只读文字；未知码有明确文字，不把Sc32负码直接作为C errno |
| `strcoll` | C locale中与strcmp排序一致；非C locale尚未支持时不得声称区域排序 |
| `strxfrm` | C locale变换保持排序，返回所需长度；足够容量才产生完整转换及NUL，长度不足不能谎报成功字符串；n=0允许标准规定的长度查询 |

已知长度的复制/填充采用现真实 REP 子集与纯C回退；始终保持DF=0，memmove逆向用不改变DF的明确倒序块/字节循环。无边界长度的 strlen/strcmp/strchr 不越过实际对象/NUL读宽字，也不越过末页碰保护页。优化必须保留这个边界。

`strspn/strcspn/strpbrk/strtok` 用256位栈字节集合，建表O(m)、扫描O(n)，不逐字符反复遍历整分隔字符串。`strstr` 选自写 Two-Way 字节匹配，needle长度/因子只算一次，线性最坏比较规模、常量辅助空间；不因malloc失败退成无界平方热点，不引入第三方实现。短needle可走明确线性小分支。首次实现先核正确性，再按真实SCCC输出及页面边界测量；设计不是性能PASS。

## 6. 转换、ctype 与通用算法

### 6.1 整数转换

`strtol/strtoul` 共用扫描器，采用 C locale 的空白、可选正负号、base0或2..36以及A..Z/a..z数字映射。只在后面确有合法十六进制数字时消费0x前缀；例如base0的`09`停在9，`0x`可消费前面的0而停在x，不吞掉没有数字的整段前缀。无数字时返回0且endptr指回**原nptr**，不是跳过空白/符号后的位置；有数字则endptr指第一个未转换字节，NULL endptr合法。

扫描前计算 `cutoff=limit/base` 与 `cutlim=limit%base`，先比较后乘加；溢出后继续扫完合法数字以产生正确endptr。strtol正上限LONG_MAX、负幅度上限2147483648u，LONG_MIN使用可表示的构造，不能先对INT_MIN取正；strtoul上限ULONG_MAX，合法负幅度转换后用unsigned模减，`-1`为ULONG_MAX。幅度本身越界返回相应极值并置ERANGE，不能靠环绕后的值“恰好在范围内”判断成功。转换成功不清errno。invalid base选择返回0、endptr=nptr、EINVAL作为明确项目扩展诊断，调用方不可把它视为ISO的统一errno要求。

| 函数 | 范围 |
|---|---|
| `atoi` | 十进制int转换；可表示输入遵守标准；超int范围属标准UB，项目可诊断ERANGE但不宣传可移植饱和转换 |
| `atol` | 同上，32位long；复用扫描但不改变标准对越界的前提 |
| `strtol` | 完整32位signed long扫描、errno/endptr/基数边界 |
| `strtoul` | 完整32位unsigned long，处理合法负号与真实幅度溢出 |
| `abs/labs` | 可表示绝对值才成立；INT_MIN/LONG_MIN仍标准UB，不能宣称返回原值是正确绝对值 |
| `div/ldiv` | 后续真实标准struct返回，商向零、余数配合；除0和不可表示商仍UB；聚合ABI到位前不导出 |
| `atof/strtod`，C99 `strtof/strtold/strtoll/strtoull/atoll/llabs/lldiv` | 等待真实FP/64位/相关ABI，不给int版本同名代替 |

### 6.2 ctype

首批为 C locale 的256字节表与真实函数实体。输入必须是EOF或unsigned char可表示值；`isalpha((char)0xFF)`在char有符号时不满足前提，库不假装能从负数恢复原编码。EOF映射不改变，分类结果只要求0或非0。128..255在首批不作为Unicode字符分类；UTF-8字符分类归后续真正区域/宽字符或SCLIB扩展，不偷偷改变ctype含义。

| 名称 | C locale判定 |
|---|---|
| `isalnum` | 字母或十进制数字 |
| `isalpha` | A..Z或a..z |
| `iscntrl` | 0..31和127 |
| `isdigit` | 0..9 |
| `isgraph` | 33..126 |
| `islower` | a..z |
| `isprint` | 32..126 |
| `ispunct` | graph中排除alnum |
| `isspace` | 空格、\t、\n、\r、\f、\v |
| `isupper` | A..Z |
| `isxdigit` | 0..9、a..f、A..F |
| `tolower/toupper` | 只映射相应ASCII大小写，其余合法输入原值返回 |
| C99 `isblank` | 选择增加空格/水平制表判定，有实体与宏一致 |

### 6.3 qsort / bsearch

`qsort(base,nmemb,size,compar)` 的 comparator 接收两个真实元素地址、返回负/0/正；实现只比较符号，不要求比较器用可能溢出的 `a-b`。顺序一致是调用方前提。重复元素允许任意相对次序，qsort不承诺稳定。首批选择自写迭代heapsort，最坏O(n log n)比较、不递归、不malloc，固定小栈块交换任意size的元素；不会因辅助分配失败而静默留下未排序数组。

`bsearch(key,base,nmemb,size,compar)` 传key与候选元素，不颠倒方向；数组按相同comparator顺序预排序；找不到NULL，重复键返回其中任意匹配元素。半开区间、中点 `lo+(hi-lo)/2`，不把u32差值转成负signed索引。两者在 `nmemb*size`、元素偏移形成前检查溢出；nmemb0不访问数组。size0或无有效数组不承诺有意义的排序；溢出属于调用方对象前提被破坏，项目诊断并停止，不能返回错误地址。比较器中断/长跳转不构成排序事务或自动回滚保证。

## 7. 缓冲 FILE

底座精确沿用 [SC32-FILE.md](SC32-FILE.md) 与 [SC32API.md](SC32API.md)。普通文件是真正可更新的现代句柄，不能用旧顺序 END_REPLACE 代替；内核处理随机字节、增extent、零洞、身份/权限与有界续进，用户库只保留有限缓冲。

### 7.1 六模式和对象寿命

| fopen模式 | Sc32 flags | 成功时的行为 |
|---|---:|---|
| `r/rb` | 1 | 现存可读普通文件，位置0，不创建 |
| `w/wb` | 14 | 新建或OPEN成功前已真正截断，位置0 |
| `a/ab` | 22 | 新建或打开，每次底层write取实际EOF；选定初始位置0 |
| `r+/r+b/rb+` | 3 | 同对象读写，位置0 |
| `w+/w+b/wb+` | 15 | 同对象读写，成功OPEN已截断 |
| `a+/a+b/ab+` | 23 | 初始读取位置0；写强制当前EOF，写后位置新EOF |

只接受这些完整拼写，不把`t/ccs/x`当已支持扩展；C11 exclusive `x` 等待独立create-exclusive底座。regular text和binary在首批都保持字节、LF不转CRLF，不设DOS Ctrl-Z EOF、不追加隐藏NUL；C text的便携定位限制仍单独记录，不能把这个实现选择宣传为所有平台的通用规则。创建权限用现六位rw的23（owner/group/other读、owner写），由kernel在创建时一次设置，不先公开后chmod；已有文件权限不覆盖。

先分配控制对象/所需路径状态，再进行会创建/截断的OPEN，避免因可预知的控制分配失败先破坏文件。缓冲可延迟分配；分配失败回退无缓冲而保持正确语义，不能OPEN已成功后无说明撤销文件创建/截断。若OPEN后还有真实不可恢复初始化错误，关闭已取句柄并明确既有文件副作用，不能声称整体原子回滚。

每个FILE拥有一个ScFile别名和动态控制记录；全库沿活跃FILE链管理，没有16槽替代门槛。普通fopen相互独立游标。标准对象在库init只取得一次0/1/2；真实引用/弱proxy及default spawn仍由已冻结streams合同负责。fclose销毁本别名，最后全局pipe writer才EOF。混用旧stream或直接ScFile接口时，调用方先提交输出并同步输入位置，不能假定另一个接口会看见本FILE尚未提交的缓冲或替它失效预读。当前没有POSIX `fdopen/fileno/dup`声明；未来增加时须规定共享描述与各FILE读写缓冲同步，不能沿用两个私有缓冲而宣称彼此实时一致。

### 7.2 缓冲与性能

普通文件默认4KiB全缓冲，stderr无缓冲，TTY stdout行缓冲，非交互stdout全缓冲。库init不为所有未来文件预分配页；缓冲来自malloc块，FILE也不能整文件R3缓存。setvbuf支持 `_IOFBF/_IOLBF/_IONBF`，只在首次IO前设置；应用buffer按给定size借用，不能free，库buffer由库释放；`setbuf(buf)`使用BUFSIZ、NULL无缓冲。未知模式、零缓冲容量等明确失败，失败不拆掉旧配置。

字符IO先在缓冲内处理；已知大块允许绕过空缓冲，拆为最多65536B的ScFileRead/Write。TTY输出沿真实端点限制处理确定prefix，不把NUL/任意二进制伪装为文本可写设备。真正pipe背压raw=-6映射EAGAIN，不能误作内部FS_N_AGAIN=-10重放；私有FS续进已由SC32 wrapper同请求让出，SCCLIB不保存kernel请求中的用户指针跨异步操作。

维护有待写数据的行缓冲流链，只在真正向交互端点请求新输入之前drain这些流，使无换行提示可见；缓冲内的fgetc不扫描全FILE链、不调用flush-all。频繁行输出只在换行、满缓冲、需要交互输入、显式flush和正常终止时提交。效率验证区分库复制/格式成本、门次数、内核PIO与元数据发布，不能只以宿主CPU百分比称高效。

### 7.3 两层 flush 与失败

fflush 的第一层是 drain：把待写字节经ScFileWrite交给端点；第二层是ScFileFlush：regular文件真实持久发布当前目录revision并保持打开。即使库缓冲为空，但上次已提交的字节仍待后端发布，也必须做第二层。没有close/reopen，不重置游标，不更换object身份。

drain根据确定transferred删除成功前缀，保留未完成后缀和error标记；下次flush只重试后缀，不能重复append前缀。全部正文已提交而backend FLUSH失败时，缓冲可为空但persist_pending保持，重试只刷新，不重放正文。卷UNCERTAIN/FAULTED来自真实底座，不把“transferred没有计入”解释成磁盘必然没改变。

STD PIPE/TTY/null的fflush提交到其真实端点，按实际FLUSH能力完成，不假造磁盘durability。父管理PARENT_REPLACE的fflush交完整库buffer并刷新ATA，**持久名字发布仍由父事务commit拥有**；子库不能越权publish或替父close/reopen。底层说明不足以支持seek/tell时，C接口返回失败并设ESPIPE，不能把库累计字节数作为成功ftell。

fflush用于输出流或最近不是输入的update流；输入方向的fflush不是标准的“丢弃预读”方法。首批对这类超出保证的用法给EOF/EINVAL诊断，输入同步应使用定位函数。`fflush(NULL)`沿活跃链处理适用的输出/更新流，跳过纯输入/最近输入流；一次失败仍尝试其它流，返回EOF并保留首个错误，成功返回0。清error标志不会删除待写后缀或解除卷fault。这里的标准方向规则核对 [WG14 N1570 §7.21.5.2、§7.21.5.3](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)，不引入POSIX输入flush语义。

### 7.4 逻辑位置、预读与方向变化

FILE同时保存底层已知游标、read-buffer读/末指针、write-buffer长度、方向、pushback和位置有效性。普通输入逻辑位置为底层位置减未消费预读，再计pushback；普通待写逻辑位置为底层位置加已接受的待写字节。所有加减先验范围。不能在fgetc预读后直接ScFileTell返回内核游标。

append待写数据的最终EOF会受其它writer影响，ftell需要先提交本FILE待写正文，再取真实写后游标；不根据OPEN时旧length推算。seek/read不取消append标志。普通待写FILE的ftell可由已知逻辑位置返回，无需每次提交。不可定位标准端点先检查能力后失败，不因尝试ftell额外清空buffer。

fseek先提交必要输出并完成flush，按逻辑位置计算SEEK_CUR，SEEK_END使用提交后的实际length；输入预读不能使CUR偏移两次。只有定位成功才丢弃预读与pushback、清EOF、建立方向切换屏障；fseek不清error。允许EOF外位置，后续写的零缺口交现代FS；不能在seek时缓存/清整段洞。

Sc32 SET入参是signed32，当前位置是u32。内部set-position helper必须先计算目标、预验范围；目标大于LONG_MAX时可用有界SET/CUR段建立位置，不能把u32目标直接cast成负SET。ftell的返回是signed long：不能表示的位置返回-1/EOVERFLOW。fpos_t可用私有完整记录保存u32位置及必要文本状态，fgetpos/fsetpos走同一逻辑位置helper；不能把公开的long限制悄悄套到可保存的fpos_t。多步实际定位中若底层失败，重查真实cursor并设置错误，不声称游标仍在原位。

更新流输出后输入，需要调用fflush或定位函数；输入后输出需要定位，最后一次输入遇EOF时例外。方向状态不能只看缓冲是否为空；成功ungetc清EOF后也不再满足EOF例外。对缺少屏障的超出标准保证用法首批明确给失败诊断，不悄悄自动复位位置。合法EOF例外由内部同步确保实际写位置正确。

### 7.5 EOF、error、ungetc 与元素计数

EOF=-1，与所有unsigned char值0..255区分。底层成功read0才建立EOF；read错误建立error，EAGAIN是错误而不是EOF。feof/ferror仅查粘滞指示，不探盘；clearerr清两个指示但不移动位置、不撤未提交字节。底层已报EOF后，除clearerr、成功定位或ungetc等规定动作，读取维持EOF，不擅自因为别的writer扩文件清掉它。

保证至少一字节ungetc，不依赖malloc成功；使用控制对象内槽，字符先转unsigned char，EOF入参失败且不改状态。成功清EOF、保留error，不写回文件。pushback待消费时不再次推进底层游标；定位丢弃slot。当前位置0或文本流的回退位置暂不可确定时显式标位置无效，ftell可失败，消费完或成功定位恢复原可知位置，不能u32减1回绕。额外第二次未消费ungetc可失败且保留已有槽。

fread/fwrite返回**完整元素数**，不是字节数；size或nmemb0返回0且不改变buffer、EOF/error和游标。乘积溢出在任何IO前给项目EOVERFLOW诊断。IO失败可以已经读取/写入不完整末元素，返回`accepted_bytes/size`；调用方不得把失败的同整块write盲目重放。缓冲fwrite成功只代表库接受了字节，最终错误可能在fflush/fclose出现，必须保留到可查询的error/errno。

### 7.6 逐函数交付清单

| 名称 | 首批必须满足的语义 |
|---|---|
| `fopen` | 六模式、b两种update位置、准确创建/截断/append与失败回收 |
| `fclose` | 尽力drain、真实flush、关闭本alias；错误返回EOF，控制对象/自有buffer也释放；底层close最终错误也不让caller复用已销毁handle |
| `fflush` | 两层、保持打开/位置、NULL批量、partial不重放 |
| `fread/fwrite` | 零请求不改状态、checked乘积、完整元素返回、partial与缓冲延迟错误 |
| `fseek/ftell` | 逻辑位置、CUR预读、END/append、真实非seekable失败、long范围 |
| `rewind` | 等价回到起点并清error指示，无返回值；失败不能假装定位成功，可由errno检查 |
| `fgetpos/fsetpos` | 私有fpos_t完整位置/状态，成功setpos清EOF并撤pushback，不把错误掩成0 |
| `fgetc/getc/getchar` | unsigned char提升到int或EOF；getc首批用函数，保证实参一次求值 |
| `fputc/putc/putchar` | c转unsigned char；成功返回写入字符，失败EOF；不错误返回未转换原c |
| `fgets` | 最多n-1、保留读到的换行、成功补NUL；开始即EOF返回NULL且不改目标；read错误返回NULL。n<=0明确诊断；n=1选择写空串返回s且不消费 |
| `fputs` | 不写源NUL、不追加换行，成功非负、失败EOF |
| `puts` | 写字符串再换行；不是fputs别名 |
| `ungetc` | 至少一字节、EOF失败、清EOF/不清error、逻辑位置与seek撤槽 |
| `feof/ferror/clearerr` | 真粘滞状态，无探盘/flush副作用 |
| `setbuf/setvbuf` | 首次IO前、借用/自有buffer寿命、三缓冲模式、失败保留配置 |
| `perror` | 先保存errno，非空前缀后`: `，错误文字与换行到stderr；自身IO错误不使原诊断变成另一个errno字符串 |
| `printf/fprintf/sprintf/snprintf`、`vprintf/vfprintf/vsprintf/vsnprintf` | 同一真实variadic格式器与不同sink，详见第8节；snprintf属于选择的C99增量 |
| `remove/rename` | 后续按ScPath接口实际权限/文件或目录/活跃pin BUSY语义；不是复制后删除的伪原子rename |
| `freopen` | 后续真正关闭原association再建立新association，标准对象地址不变；filename=NULL策略单独定义；不能先当作fopen别名 |
| `tmpfile/tmpnam` | 后续需要内核原子排他创建与临时文件寿命；先查不存在再create有竞争，当前不宣称成立 |
| `scanf/fscanf/sscanf`及选定C99 `vscanf/vfscanf/vsscanf` | 后续格式输入；未实现不声明或导出，不用整数输出格式器假代解析器 |
| `gets` | C89历史不安全入口保留在兼容清单待定、默认SDK不发布，C11已移除；完整C89声明必须把这个差异列出，不能无声省略后称完整C89 |

fclose返回后FILE不可再使用，包括失败返回；SCCLIB先保存第一处错误，再尽力关闭释放，后续close不能覆盖主要诊断。文件目录扩展与后台提交的确定prefix/UNCERTAIN沿底座，不另承诺断电原子性。正常退出fclose所有FILE，异常退出不运行用户态缓冲，因此不能把“内核页回收正常”当缓冲已落盘证明。

## 8. 格式输出与 stdarg

首批八个输出函数共享一个解析器。variadic入口使用真实va_start/end；v入口使用传入va_list，内部需要多遍时va_copy后逐份va_end，不依赖“当前ap恰巧是普通指针”的拷贝猜测。sink分为FILE、无界sprintf与有界snprintf；无界sprintf的容量由调用方保证，不能偷偷改变为固定截断。

| 部分 | 首批明确支持 |
|---|---|
| 转换 | `d i u o x X c s p n %`；其中n按对应指针类型写已产生字符数，%不读取实参 |
| flags | `- + 空格 # 0`及其标准优先关系；不引入POSIX `'`分组 |
| width/precision | 十进制与`*`，动态实参为int；负width左对齐，负precision视作未指定；数字和累积字符数溢出先诊断，不signed回绕 |
| 长度 | 无修饰与`h/l`；选择C99 `hh/z/t`，按i386真实提升/类型；`j/ll/L`及宽字符`lc/ls`等待真实对应类型 |
| 整数精度 | 精度0和值0不产生数字；`#`的八进制例外保留；x/X非零才有0x/0X；有整数precision时0 flag不充当精度 |
| 字符/字符串 | c取提升后的int再转unsigned char；s精度按字节限制，不把UTF-8字节截断承诺为字符截断；NULL字符串不满足标准有效指针前提，调试诊断不是标准`(null)`保证 |
| 指针 | p接void*，实现定义输出为固定`0x`加8个小写十六进制数，NULL为`0x00000000`；调用方仍须给相符variadic类型 |
| n | int*/short*/long*/signed char*/size_t对应signed类型指针/ptrdiff_t*；写逻辑已产生数，不写被snprintf截断后的长度；不任意禁用后仍称整数标准范围完整 |

格式器用unsigned取负幅度处理INT_MIN/LONG_MIN，不调用abs。%h/%hh从提升的int取得后正确缩窄，%u取unsigned，%l按long/unsigned long；signed/unsigned、指针类型与实际实参必须相符，不靠“都是4B”宣称所有类型错配合法。

完整格式语法预检后才产生输出；含`f F e E g G a A`、`ll/j/L`、未支持宽字符或非法语法时返回负值，置项目ENOSYS/EINVAL，不读取错误宽度实参、不输出假0、不把该调用列为标准成功范围。此检查是明确失败扩展，**不等于这些标准格式已经实现**。动态width/precision取参、输出计数或FILE实际IO在随后单遍执行中失败可能已有prefix，不能作格式调用原子承诺；n写入、副作用与失败边界逐项记录。

snprintf的n0允许s=NULL，完全不写；n>0至多写n-1个内容字节并补NUL，正常返回完整逻辑长度而非截断长度，不含NUL。错误路径若可写buffer则保留有界终止；计数超过INT_MAX返回负值/EOVERFLOW，不无符号绕回正成功。不能格式化两遍并把%n执行两次；同一遍增量count与sink容量分离。其返回/容量核对 [WG14 N1570 §7.21.6.5](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)，同时明确它是本项目选择的C99函数。

不凭“库可由GCC编译”开放浮点/64位：官方应用SCCC必须真实生成相同类型、算术、参数布局与返回，才能扩大这些格式。后续扩展先确定8B参数与浮点返回/long double政策，再改编译器、SDK、manifest版本与兼容清单，保留ABI4现函数的32位行为。

## 9. errno、诊断与标准头

errno为正int值，初始0，成功调用不无条件清零；只在有规定或项目扩展诊断的失败路径设置。SC32负status、domain/raw仍可由SC32接口查询，不塞进errno而让C代码猜旧ABI。首批需要EDOM/ERANGE/EILSEQ及文件/资源扩展；具体数字作为新SCCLIB ABI在实现冻结时一次分配，不能复制第三方组件私有头的数字当公共承诺。

| Sc32结果/使用场景 | SCCLIB errno语义 |
|---|---|
| INVALID / VERSION | EINVAL / ENOSYS，按输入错误或不兼容实际能力区分 |
| ACCESS / NOT_FOUND / EXISTS | EACCES / ENOENT / EEXIST |
| MEMORY / NO_SPACE | ENOMEM / ENOSPC |
| STALE / AGAIN | ESTALE / EAGAIN；内部raw=-10续进由SC32同步wrapper解决，不作为普通FILE短失败 |
| BROKEN_PIPE / IO | EPIPE / EIO；partial保留真实已经接受的prefix |
| NOT_SUPPORTED | seek/tell用ESPIPE，其它未实现能力用ENOSYS，不假报成功 |
| BUSY / RANGE / CANCELLED / FAILED | EBUSY / EOVERFLOW或ERANGE按语义 / ECANCELED / EIO |
| 格式或ctype编码问题 | EILSEQ只用于真实编码错误，不能作为任意syntax错误万能码 |

assert宏在NDEBUG下完全不求值，启用时表达式只求一次；失败输出表达式、源文件与行号，随后异常终止，不能仅打印后继续。C99函数名在编译器具备__func__前不假填空串说完整。assert/perror/abort内的诊断路径不分配大块堆，保留一段固定小栈空间供低内存失败使用。

### 9.1 标准头逐项范围

| 标准头 | 计划交付与边界 |
|---|---|
| `stddef.h` | NULL、size_t、ptrdiff_t、wchar_t=int32基础类型及真正offsetof；wchar_t typedef不等于已支持宽字面量/函数；C11 max_align_t不在本阶段，不能把16对齐说成该类型已支持 |
| `limits.h` | CHAR_BIT8、SCHAR[-128,127]、UCHAR255、plain CHAR有符号、SHORT16、INT/LONG32及对应unsigned maxima；LONG_MIN用`(-2147483647L-1L)`；MB_LEN_MAX按实际locale转换后设置，不能从TTY UTF-8推定 |
| `stdint.h` | int8/16/32_t、uint8/16/32_t与least同宽类型；fast8/16/32为int/unsigned int，intptr_t/uintptr_t为int/unsigned int，配真实限值及常量宏；先没有64类型；C99要求的least64/fast64/intmax_t等缺口明确，因此该头首批是选定32位子集，不能错误用32位intmax_t冒充完整C99 |
| `stdbool.h` | C3到位后bool→_Bool、true1、false0及标准宏；不提供char替身 |
| `stdarg.h` | C2到位后va_list与四宏；C89三宏加选择C99 va_copy；声明或普通函数桩不足以交付 |
| `errno.h` | 可修改int左值宏、EDOM/ERANGE/EILSEQ和明确项目扩展；每进程私有、无TLS承诺 |
| `assert.h` | NDEBUG策略在每次包含时生效，不能普通整头guard锁死首次选择；表达式一次求值、真实abort |
| `string.h` | 第5节完整基础组；不会把strdup/strlcpy等非ISO扩展塞入标准保证范围 |
| `ctype.h` | 第6节C locale实体与可选宏、EOF/unsigned char前提；isblank为明确C99增量 |
| `stdio.h` | FILE/fpos_t/EOF/三标准流/SEEK三常量/BUFSIZ/缓冲模式与已完成函数；未实现格式输入/临时文件不发布虚假prototype |
| `stdlib.h` | malloc四项、整数转换、abs/labs、qsort/bsearch；exit/atexit/abort须第10节接线；其余逐项推进。聚合、FP与64位函数尚不导出 |
| `locale.h` | 后续先真实C locale：setlocale(NULL)查询、"C"设置、""选择环境可支持值；未知名返回NULL并保持旧状态；localeconv完整lconv字段、CHAR_MAX哨兵和只读共享结果寿命，不只给一个字符串函数 |
| `time.h` | 第11节clock/time/difftime/mktime/asctime/ctime/gmtime/localtime/strftime与结构、限值；FP返回阻塞difftime，CPU采样不能冒充clock |
| `math.h` | 完整分阶段浮点函数/错误/舍入计划，真实FP类型/ABI前不发布double函数或整型同名替代 |
| `float.h` | 跟真实float/double/long double格式与算术模式一起发布；不能仅列IEEE常量却没有类型 |
| `setjmp.h` | 后续真实保存callee状态/返回两次、longjmp和自动变量规则；私有根退出上下文不等于公开setjmp已完成 |
| `signal.h` | 后续真实ISO信号/handler/raise/abort关联；SC32异常回调不是直接同义替代 |
| `iso646.h` | C95/C99选择项，可用标准替代运算符宏；只有真实头安装后计入 |
| `inttypes.h` | 后续与intmax_t/64位/stdint及相应格式器同步；不能给PRI64却无64参数支持 |
| `wchar.h/wctype.h` | 后续真正多字节状态、宽字符转换/分类/IO；现UTF-8显示/字体不是这些函数的实现 |
| `complex.h/tgmath.h/fenv.h` | 后续复数类型、type-generic机制及浮点环境合同；不靠空头通过include当完成 |
| C11新增 `stdalign.h/stdatomic.h/stdnoreturn.h/threads.h/uchar.h` | 本阶段不宣称支持；N1570用于核对共同规则不扩大目标到C11 |

标准头中的类型/宏可以先于实体实现，但对应调用不得给“能链接的失败桩”。每次发布以真实导出白名单生成/检查manifest，确保公开声明、SDK文件、依赖和实际功能四者一致。头宏不吞浮点参数、不绕过compiler诊断，也不能为降低宏数把标准名称改成SandCore私有名字。

## 10. 正常终止、main 返回与异常

现 `scl_application.c` 在main返回后直接ScRuntimeDestroy，SC32 ScProcessExit直接进入内核资源回收；两者尚不能等同完整stdlib exit。必须在Phase3接线受控根终止阶段，根的新增API与实现由主代理统一处理，本文不改冻结ROOT。

选择的生命周期是：依赖全部init成功后，根保存私有退出continuation；运行main；正常return或显式exit都进入同一个根终止路径；先调用SCCLIB注册的prepare函数逆序执行atexit，此时依赖库仍活跃；随后根逆依赖fini，依赖者fini仍可使用SCCLIB；SCCLIB自己的fini最后drain/flush/close所有FILE、移除临时文件并回收allocator，然后SC32API fini，最后返回CRT旧EXIT。

显式exit不能在SCCLIB自己的栈上调用ScRuntimeDestroy并卸载自己。必须以根保存的真实continuation撤离应用/库帧，回到ScApplicationStart的根栈；需要专用编译器/后端支持并让GCC引导采用同一自写i386适配，不能用当前asm白名单的EBP猜测或从C函数随意改ESP后return。根保存状态包含callee-saved寄存器、ESP/EBP和恢复PC，属于本进程私有root记录，无kernel指针。这个必要增量不是公开setjmp的完成功能声明。

prepare注册须能在SCCLIB init期间记录真实函数和所属模块寿命；只挂钩不能重入装卸模块。无SCCLIB依赖的应用继续现正常main路径。init失败的外部副作用仍由该init撤销，未成功init不承诺fini。exit重入/在fini再次exit是超出保证的用法，防止重复回调/无限递归并以明确异常路径结束，不假作第二次正常终止。

| 函数/事件 | 明确语义 |
|---|---|
| `atexit(func)` | 动态LIFO，每次成功登记都调用一次，重复地址也按登记次数；至少32项无需把32设为全局上限，更多受真实资源限制；失败非零，不能覆盖旧链 |
| `exit(status)` | 不返回，走根正常终止路径；EXIT_SUCCESS0/EXIT_FAILURE1，其它32位状态沿已有任务退出码约定 |
| `return main` | 对已装载SCCLIB应用与exit(main结果)等价的回调/文件收尾，不直接跳过atexit |
| `abort()` | 不返回、异常结束；首批信号未实现前明确是异常终止子集，不称完整SIGABRT可捕获行为；不保证flush或atexit/fini，固定小诊断可尽力写stderr |
| C99 `_Exit(status)` | 不执行atexit，选择不flush/fini，调用SC32进程退出并由内核回收；只有实体接通才声明 |
| kernel terminate/真实异常 | 不执行潜在损坏的用户回调，不保证用户buffer落盘；内核真实句柄、pin、候选extent、页沿本人链清理 |

atexit运行时的新登记选择允许成功并插入尚未执行链头；逐节点先脱链后调用，防重复执行。回调在所有相关模块仍驻留时执行；正常fini不能提前释放其函数地址。回调中longjmp跳离退出函数不在保证范围。flush错误必须尽力关闭其它流并记录，不把main非零码改成“成功”，也不凭页已回收宣称IO成功。stdlib终止顺序与最少登记能力核对 [WG14 N1570 §7.22.4](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)，SCL fini插入位置是本项目为依赖寿命选择的工程合同。

## 11. 环境、区域、时间与数学的后续计划

| 功能 | 实际实现条件与语义目标 |
|---|---|
| `getenv` | 用ScProcessEnvironment取得本进程值；NULL表示未找到；返回的库缓冲寿命/后续getenv覆盖政策公开，不返回指向临时栈的字符串；动态增长先预验实际长度 |
| `system` | 用真实Shell命令解释、wait/退出状态，`system(NULL)`只查询是否有command processor；须有正式SCSHELL及状态编码，不能简单ScProcessSpawn直跑冒充全部system语义 |
| `rand/srand` | 独立进程状态、默认seed1、相同seed重现、RAND_MAX至少32767；自写算法记录质量/非密码用途，stdlib其它函数不暗中消耗该序列 |
| `mblen/mbtowc/wctomb/mbstowcs/wcstombs` | 先C locale真实单字节转换与reset/query，再UTF-8与state；不得从TTY能显示中文推定已具备多字节C locale |
| `locale` | 至少正确C locale、完整lconv；其它locale有数据/实现才列支持，失败设置保持旧locale，ctype/strcoll/数字格式一起切换失效 |
| `clock` | C CPU时间定义需要可信每进程CPU累计时间；当前running_samples/100Hz统计仅为采样，不能用系统monotonic ticks直接充当CPU clock。缺精确基础时返回(clock_t)-1并列未支持，交付真实clock前补计时底座 |
| `time` | ScSystemClock为UTC2000..2099；首个32位signed time_t若选Unix秒只能表示至2038，越界明确失败(-1)，不能截断2099。宽时间升级与64位ABI一并审查 |
| `gmtime/localtime/mktime` | 完整年月日/闰年/周日号/年日号、规范化越界tm成员、可表示范围和DST政策；先UTC本地时区固定0/DST0并公开，再真正时区规则。mktime还改写规范化tm，不只计算一个整数 |
| `asctime/ctime/strftime` | 真标准文本/格式/缓冲长度结果及本库静态结果寿命；strftime容量不足返回0且不伪报完成，选定格式逐项列出 |
| `difftime` | 返回double，真实FP编译器/ABI前不能用long或ScWideCounter替代签名 |
| `math` | 在float/double/long double类型、常量解析、转换、算术、参数提升与返回、任务状态支持验收后，分三角/双曲/指数对数/幂根/取整余数等实际函数组推进，记录精度、特殊值、错误与舍入，不填零函数 |
| FP/64格式和解析 | 与math/类型同批更新printf/scanf/strto/stdint/inttypes，完整共同ABI，不能宿主GCC特供而官方SCCC永远无法调用 |

这里允许分阶段交付“未支持的可检测标准结果”，例如clock不可取得时的-1；不允许只写声明/恒失败函数就把某条能力统计为已实现。完整SCCLIB目标仍由M10a2实际兼容清单与用户合同管理。

## 12. 实施、定向证据与结束条件

本次只有文档与只读源审计，不新增标准库源、标准头、manifest或构建规则；没有启动编译、客体SCCC、QEMU或Phase3。主代理在SC32当前出口实际验证后，按C1..C5/R1、基础、buffer/格式、生命周期次序实施；编译器/ROOT必要增量由相应owner接手。

后续验收按具体风险组织少量有目的的程序，复用未变化的动态装载/旧格式证据，不自扩全理论排列组合：

| 待证实的问题 | 最小证据与结束条件 |
|---|---|
| 变参/布尔/头真实可用 | 宿主与客体实际SCCC生成同SDK程序并执行；窄值提升、指针、copy、bool256/非空指针、非法LL诊断及实体函数指针 |
| allocator确为子分配 | 小块/交错释放合并、16对齐、in-place/moved realloc、失败旧块保持、calloc溢出；记录页增长/全空arena释放和两进程私有数据，不仅mock页函数 |
| 文件语义 | 同程序六模式、预读CUR、ungetc/EOF、合法更新屏障、append/partial、两次flush后继续、fclose/std EOF/default spawn；目录功能由SC32底座证据复用 |
| 格式/字符串/算法 | INT_MIN/UINT_MAX、width/precision/#/n、snprintf零/截断计数、重叠memmove与页边界、strto endptr/overflow、重复键与comparator方向 |
| 正常退出与回收 | main return/explicitexit执行一次atexit且在fini之前、fini可用stdio；真实异常跳过user回调，保留kernel回收账；输出内容真正读盘核对 |
| 效率 | 相同输入测字符缓冲与块IO的门次数、复制字节/页占用/碎片搜索、PIT与典型延迟；大洞/extent预算及WM推进使用已有FS定向证据，不宣称无测量性能达标 |

源码、头、库导出与依赖版本同步后，由真实动态应用在系统内编译、运行、读取结果，保存日志/必要截图、产物摘要和 source provenance。历史SC32/Phase1证据只覆盖未受影响基础；设计清单不进入PASS计数。完成这一优先批也仍需按M10a2RULE推进SCLIB/SCUI/桌面与其余完整目标。

## 13. 来源与修订

项目合同与实际源：根M10a2RULE.MD §4.1/§9、[SC32API.md](SC32API.md)、[SC32-FILE.md](SC32-FILE.md)、[SCL.md](SCL.md)、[SCCC-SCL.md](SCCC-SCL.md)、[C.md](C.md)、`SC32API.H/SCTYPES.H`、SCCC lexer/parser/pp/emit以及`scl_application.c/SCLDR.H`。本次未以旧私有同名头或第三方C实现充当标准实现依据。

标准核对仅用WG14官方 [N1570（2011-04-12 C11委员会草案）](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf) 的共同语言/库规则，重点为布尔、stdarg、stdio、分配/转换与终止。N1570不是C89/C99原版、不代表本项目选择C11或最终ISO出版文本；具体分阶段范围以本文件各表为准，标准章节编号以N1570为定位。本次没有完成官方 [N1256（C99含修订草案）](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf) 正文的逐条版本对照，后续扩为完整标准时需补该对照。

本次没有复制标准正文或引入任何第三方实现，没有新增运行时许可依赖。算法自写、宿主开发工具与既有第三方组件归档分开记录；若今后选用外部代码，仍先按THIRD-PARTY和用户宽松许可合同审查/固定来源/完整归档，不能由此文档默认授权引入其它许可。

2026-10-15：新增SCCLIB正式设计；逐项列优先标准函数/头、ABI4与现SCCC缺项、16B子分配器、缓冲六模式/双层flush/逻辑位置、格式边界、正常终止与后续标准支持；仅静态设计，未实现或验证Phase3。
