# SCCLIB 标准 C 运行库

2026-10-15，SandCore M10a2 — The Application Platform Era。

**本批32位/字节标准C profile已统一构建、系统内生成并实际运行。** [真实证据](../evidence/m10a2-stdlib/README.md)包括94导出/19导入的客体库、stdio六模式、正常/显式/init退出和递归拒绝、共享STD及空流等待、当前G1执行与精确回收。未声明完整ISO C、浮点/64位或整版M10a2完成；SCUI与应用重制继续。

设计依据为 [SCCLIB-DESIGN.md](SCCLIB-DESIGN.md)、[SCCC-STDLIB-PLAN.md](SCCC-STDLIB-PLAN.md)、[SC32-FILE.md](SC32-FILE.md) 与 [SC32API.md](SC32API.md)。下文以实际源的选择和边界为准，明确两项设计细化：标准流改为第一次使用时take；PIPE/TTY/SERIAL的普通stdio改为等待真实事件的阻塞行为。

## 1. 构成、SDK 与库版本

实现目录 `user/stdlib/`，唯一构建入口 `SCCLIB.C`，清单 `SCCLIB.json`。入口包含九个自写 `.c`，所有模块通过 `SCCLIBPRIVATE.H` 共用私有声明。不能另把九个c重复链接，也不能把SCCLIB.C包含进应用来静态复制正文。

| 源 | 职责 |
|---|---|
| `memory.c` | 16B子分配、空闲桶/邻接合并、realloc和arena回收 |
| `string.c` | 字节内存、完整基础字符串组、字节集合、Two-Way搜索、C locale比较/转换 |
| `convert.c` | strtol/strtoul、atoi/atol、abs/labs、进程随机序列与getenv |
| `ctype.c` | 自写生成的C locale字节分类表与大小写函数 |
| `errno.c` | 进程私有errno、Sc32状态映射、错误文字 |
| `stdio.c` | 动态FILE、标准流、六模式、缓冲/定位/回退/等待/收尾 |
| `format.c` | 共享variadic格式器、FILE/string/bounded string三个sink |
| `terminate.c` | 动态atexit、根prepare、正常exit与直接_Exit/abort/assert |
| `SCCLIB.C` | int(void) init、void(void) fini与单份模块聚合 |

SCCLIB为 **ABI1.0 / library_version1**，依赖SC32API.SCL ABI1/min_minor0/min_version1，以及R1 SCLDR.SCL ABI1/**min_minor1/min_version2**。旧九导出ROOT不满足这条依赖，不能通过恒成功注册桩让库加载。Root真实三接口为 `ScApplicationRegisterPrepare`、`ScApplicationPinCallback`、`ScApplicationExit`。

清单94个函数/实现辅助导出，19个函数导入、两条依赖。全部导入只来自公开SC32及协定R1；没有SCAPI.H、内联硬件桥、libc链接或第三方实现。导出init/fini名字由头中独立callback字段定位，不为应用公开管理运行库的私有生命周期。

普通应用使用小写 `<stdio.h>`、`<stdlib.h>` 等，标准头上盘 `/SYS/INC/`；库源与私有头上盘 `/SYS/SRC/STDLIB/`。Root负责相应安装/构建接线，本代理没有改Makefile。`SCCLIBPRIVATE.H`不作为应用公共SDK。第三方组件原私有同名头保留原用途，不当成SCCLIB标准头。

i386 cdecl参数槽4B、最低入口栈对齐4B、EAX整型/指针返回、callee保留EBX/ESI/EDI/EBP；沿 [SCL.md](SCL.md) 的ABI4。GCC引导使用SDK incoming-stack-boundary2/preferred4，不因自动CRT某次16B调用就假定所有普通调用16B。size_t=unsigned int、ptrdiff_t=int、wchar_t=int，均4B；long4B，char8位有符号，short16位。

## 2. 实际函数与标准范围

范围为“C89基础函数语义 + 选定C99的32位/字节/C locale功能”。不是完整ISO C、不是完整hosted实现；所有本表仍是**源已写、执行待验**。

| 组 | 实际函数实体/头 |
|---|---|
| 分配 | malloc/calloc/realloc/free |
| 内存 | memcpy/memmove/memset/memcmp/memchr |
| 字符串 | strlen/strcpy/strncpy/strcat/strncat/strcmp/strncmp/strchr/strrchr/strspn/strcspn/strpbrk/strstr/strtok/strerror/strcoll/strxfrm |
| 转换/算法/环境 | atoi/atol/strtol/strtoul/abs/labs/qsort/bsearch/rand/srand/getenv |
| ctype | isalnum/isalpha/isblank/iscntrl/isdigit/isgraph/islower/isprint/ispunct/isspace/isupper/isxdigit/tolower/toupper |
| FILE基础 | fopen/fclose/fflush/fread/fwrite/fseek/ftell/rewind/fgetpos/fsetpos |
| 字符/缓冲/错误 | fgetc/getc/getchar/fputc/putc/putchar/fgets/fputs/puts/ungetc/feof/ferror/clearerr/setbuf/setvbuf/perror |
| 格式输出 | printf/fprintf/sprintf/snprintf/vprintf/vfprintf/vsprintf/vsnprintf |
| 路径 | remove/rename，沿真实SandFS权限、活跃pin BUSY与既有rename目的策略 |
| 终止 | atexit/exit/_Exit/abort；assert宏调用真实异常结束辅助函数 |
| 基础头 | stddef/limits/stdint32子集/stdbool/stdarg/errno/assert/string/ctype/stdio/stdlib |
| 实现辅助 | __scc_errno_location / __scc_standard / __scc_assert_fail；给宏调用的真实库函数，不能在应用另复制状态 |

未发布函数没有虚假prototype/导出：浮点/64位转换、math/time/locale函数、格式输入scanf族、freopen/tmpfile/tmpnam、div/ldiv聚合返回、宽字符/信号/setjmp/system等按正式设计继续推进。gets历史边界仍记录，不默默删除后声称完整C89。C locale已经用于现字符分类与strcoll/strxfrm；它不代表locale.h或UTF-8多字节转换已完成。

stdbool.h必须有真正 `_Bool`；SCCC能力门 `__SCCC_BOOL__`，没有该能力时报错，不能typedef char替代。stdarg.h在SCCC选择unsigned char*游标，并以 `__SCCC_VA32__` 要求真实 `__builtin_va_start/arg/copy/end`；GCC i386使用其真实builtin va_list。offsetof使用真实 `__builtin_offsetof`，不以空指针取成员地址冒充ICE。stdint只给8/16/32/least/fast/pointer的真实类型和宏，没有int64_t、uint64_t或32位intmax_t伪代64位。

## 3. 分配器合同

普通arena64KiB，超过约32KiB的块使用页取整独立arena；32B块头/16B跨度/对齐前缀保证payload16B对齐。当前64桶是尺寸类别，**不是分配数量上限**。空闲bitmap跳过空桶，同桶first-fit；释放通过前/后跨度定位并合并，避免扫描全部活跃分配。最坏搜索会遍历桶内空闲块，不声称O(1)分配。

free(NULL)无作用；非起点/重复free仍调用方UB，已检测的损坏直接异常退出，不能继续把损坏链作为地址。realloc先原位缩小/合并后块，必要移动时先分配再复制请求尺寸前缀，失败旧块不变；普通缩小余块与相邻free合并。独立大arena缩小保留该arena容量至free，避免不支持的partial page-free。calloc乘积、header/round-up加法都先检查，calloc请求区清零，不把复用过的malloc块当零页。

选择malloc(0)/calloc任一0返回NULL且不改errno；realloc(p,0)释放并返回NULL，明确是此32位profile的选择。资源/尺寸失败返回NULL；实际Sc32错误映射记录在errno。Sc32单次/总heap容量64MiB，元数据也占空间，不能保证一个完整64MiB malloc payload。

全空普通arena保留至多一个warm arena，其余和大arena整块ScMemoryFreePages；无一malloc一页，无中间页面非法free。页释放成功后才移除控制链接，不访问已经解除映射的arena。正常SCCLIB fini最后释放所有arena；异常由kernel owner链回收，不跑损坏堆的用户回调。

## 4. 字符串、转换与算法

字节比较统一unsigned char。memmove选正确方向且不设置DF；memcpy/memset按8字节展开加尾字节，只访问明确count；无终止长度的扫描按字节，不跨对象/NUL宽读到保护页。当前采用纯C实现，没有声称REP/SIMD优化已经完成或性能已经达标。

strncpy短源补零至n、长源不保证终止；strncat至多n个源字节另写NUL。strchr/strrchr可以查终止NUL，strstr空needle返回text。集合函数与strtok建256位局部集合，strtok游标属本进程、会修改输入，未承诺线程安全。strstr为自写Two-Way临界分解/周期跳步，不为长needle每个位置重扫完整needle，不调用malloc。源码算法正确性仍须Root定向执行核验。

strtol/strtoul支持C空白、符号、base0/2..36；有合法下一位才消费0x。无数字endptr回原nptr，overflow仍扫完合法数字；先比较cutoff/cutlim再乘加。signed负幅度2147483648按LONG_MIN特例返回，不对INT_MIN取正；合法unsigned负号用unsigned模减，幅度溢出仍ERANGE/ULONG_MAX。成功不清errno；invalid base的EINVAL是明确诊断扩展。atoi/atol越界及abs最小值仍标准UB，不宣传饱和安全转换。

qsort为迭代heapsort，固定64B交换栈块，无递归/malloc、最坏O(n log n)比较、非稳定排序。bsearch半开区间，compar(key,element)方向正确，重复键可返回任一匹配。count*width先验，不用signed a-b要求比较器幅度，不将资源失败变成未排序的假成功。

ctype固定C locale表；EOF与unsigned char输入前提、0/非0返回、128..255不作Unicode分类。rand为进程私有LCG、默认seed1、同seed重现，RAND_MAX32767，无密码学用途承诺。getenv用现Sc32单值最大255B合同复用256B私有结果，失败NULL，后续getenv覆盖结果；不返回局部栈。环境名字/可查值范围沿SC32，不假造完整POSIX环境接口。

## 5. FILE、六模式与缓冲

FILE为opaque控制对象，每个普通fopen使用malloc记录和一个现代ScFile别名，沿动态链枚举，没有16槽FILE门槛。六模式flags为r1/w14/a22/r+3/w+15/a+23，支持b与+两种合法顺序；w成功时真截断，a每write真EOF，a+初始读位置0。创建六位rw23一次设置；字符文本/binary在首批都按字节/LF保持，无CRLF/Ctrl-Z/NUL填充。C11 x、t、ccs不开放。

控制分配在OPEN创建/截断之前，buffer延迟申请；malloc失败可回退无缓冲，不伪回滚已成功创建/截断。默认4KiB全缓冲，TTY输出行缓冲，stderr无缓冲；setvbuf支持三种模式及借用caller数组，失败保留旧配置，借用buffer不free。大块空缓冲read/write直接分成≤64KiB底层请求，不给整文件R3缓存或反复复制完整正文。

读预读只推进backend游标，FILE另保逻辑位置；消费缓冲才推进逻辑。write先接受到buffer，最终错误可在fflush/fclose出现。方向不是按buffer空判断：输出→输入须fflush或定位，输入→输出须定位，最后输入确遇EOF时例外；clearerr不抹掉“上次输入遇EOF”事实，成功ungetc会撤这个例外。超出保证用法给明确错误，不能静默重置位置。

### 5.1 阻塞与 EOF

Sc32 endpoint read/write仍保留原真实非阻塞raw=-6。SCCLIB的普通PIPE/TTY/SERIAL stdio在SC32_E_AGAIN时调用 **ScSystemWait(SC32_EVENT_STREAM,SC32_INFINITE)**，然后重新操作端点；kernel按真实数据/引用/状态变化把事件锁存到native descriptor owner/PID代数，不因stdout常可写每tick通知。过期事件只导致一次重新检查，没有轮询空stdin或把AGAIN当EOF。

每次等待后API完整重验地址、权限与句柄；已经完成的prefix先计入，只重试后缀。写pipeline关闭读端得到EPIPE；最后全局writer退出/close才read0。只有成功read0置EOF，失败置error、保留errno，wait失败也不是EOF。内部FS raw=-10由SC32 wrapper同步续进，不与端点AGAIN混同。

当前没有stdio nonblocking公开profile或不存在的ScFileWait声明；将来若增加nonblocking须明确模式/API及EAGAIN/ferror，不能把本阻塞合同偷偷换掉。Root已只读核对native event成员链，**真实PIPE/TTY输入等待仍待客体验证**。

EOF=-1与所有字节0..255区分；feof/ferror只查粘滞状态，clearerr清指示但不改游标/未写后缀。已有error不自动使后续成功IO返回失败，单次operation_error另记录。fread/fwrite返回完整元素数、乘积先验；任何尺寸0不改变FILE状态或caller数据。不完整末元素仍可已部分读写，不得盲目重放完整请求。

### 5.2 双层 flush、定位与回退

fflush先drain库buffer，按确定prefix撤成功字节，保留后缀；再ScFileFlush真实提交regular目录revision并保持打开。buffer空也执行backend flush，正文已提交后flush失败重试不再次append。fflush(NULL)处理适用输出流、继续其它流并保留第一错误；输入方向的fflush给EINVAL，不冒充POSIX“丢预读”。

fseek按逻辑位置处理CUR，提交输出后查询END；成功才清EOF/预读/pushback并建立方向屏障，不清既有error。EOF外位置由内核下一次write真实零洞。append ftell先drain以取实际新EOF，不拿OPEN时length推算。nonseekable PIPE/TTY/SERIAL/null返回ESPIPE，parentReplace有真实tell但无seek。

ftell为signed32 long，超LONG_MAX失败EOVERFLOW；fpos_t为8B私有position/valid cookie，可保存u32位置。set-position通过至多3个SET/CUR段处理Sc32 signed入参，不能把高位u32 cast负SET。定位失败重查真实cursor并撤陈旧buffer，不能作多步原子承诺。

ungetc保证内置一字节，不依赖malloc；EOF参数失败，成功清EOF但不清error，不写回文件。逻辑位置0的回退暂未知，tell可以失败而不下溢；读出pushback后恢复此前位置，成功定位丢弃它。额外未消费第二字节可以失败。fgets保留换行、成功补NUL，读前即EOF返回NULL且不改目标；n1选择空串不消费，read错误返回NULL。getc/putc实体保证实参一次求值。

## 6. 标准流、继承与退出

库init只建立三个静态控制对象，**第一次访问相应标准宏/函数才ScFileGetStandard一次**。这样RegisterPrepare最后分配Root终止栈失败时，不会已经夺取/关闭旧0/1/2；纯malloc应用也不触碰旧标准引用。获取失败或fclose后的该控制对象不再take，子进程仍持端点不能复活本PID关闭的stdout。

first take按已实现底座把真实引用交native alias，旧slot仅弱proxy，用于旧调用/default spawn继承而不计writer保活。fclose真减alias，最后本PID标准alias撤proxy，最后全局writer才EOF。未使用对应stdio的旧标准slot一直走原旧路径，随后由kernel进程清理。多个标准端点可能共享底层位置，库不借每次查询分配旧fd。

STD FILE/MEMORY按真实caps定位；PIPE/TTY/null无seek/tell。TTY仍是UTF-8文本端点，任意二进制/NUL可能失败。父PARENT_REPLACE stdout flush把全部buffer交端点并刷ATA，持久名字发布仍由父commit拥有；不能子库越权publish或close/reopen父事务。ordinary fopen则有真实现代保持打开的flush。

R1注册在SCCLIB init最后一步，只有init成功后Root提交provider pin与128KiB终止栈。atexit有32项BSS最低预留，更多走动态allocator，32不是上限；先取节点→Root PinCallback成功→挂链，busy/init期间失败回收候选，不能留下失效模块地址。Root只允许RUNNING/PREPARING的合法APP函数域或READY SCL X段。

正常main return/exit由Root汇合，先调用prepare逆序执行atexit；节点先脱链/归还再调用，新登记可进入未执行链，重复地址按登记次数执行。然后Root逆依赖fini，依赖者可继续用stdio，SCCLIB fini只flush/close、释放剩余记录/env/arena，不第二次atexit。stderr最后关供一次固定错误诊断；错误尽力关闭其它流，保持原exit码。

SCCLIB成功后其它SCL init调用exit可由R1终止runner撤离init事务，收尾已成功init前缀；SCCLIB自身未成功、prepare/fini递归exit或不允许的loader busy退出为异常1。exit仅调用RootExit，不在自己调用栈Destroy。_Exit/abort/assert失败走ScProcessExit，跳过atexit/fini/flush，kernel回收句柄/pin/候选页；abort尚无ISO SIGABRT handler行为，不能称完整signal。Root两栈保留显式exit前自动buffer所在用户栈直到flush结束。

## 7. 格式输出、错误与宏

八个输出函数共一个parse/render，语法整段预检先拒绝不支持格式；render用va_copy独立游标、结束va_end。格式支持d/i/u/o/x/X/c/s/p/n/%、五flags、十进制/* width/precision、h/l与选择C99 hh/z/t；p为固定0x加8位小写hex。INT_MIN用unsigned幅度；整数精度/零值/八进制#/sign/zero/left顺序有实际实现。

snprintf n0允许NULL，n>0至多n-1内容+NUL，返回完整逻辑长度不含NUL；逻辑计数与存储计数分开，%n只执行一次，截断不改其逻辑count。大padding的受限string sink直接计数/写实际可存区域，不循环数十亿个被丢弃字符。总数/width超过INT_MAX给EOVERFLOW而不回绕。

浮点f/e/g/a、ll/j/L、宽字符lc/ls未支持，语法预检返回负值/ENOSYS且不读取错误宽度参数、不假输出0。非法语法EINVAL。动态width/实际IO随后失败可以已有prefix，不承诺格式调用原子；sprintf容量是caller前提，无隐藏固定截断。s精度按字节，不能声称字符/Unicode精度；p/type/%n有效pointer依然caller责任。

errno宏返回本进程SCCLIB内int左值，正码按errno.h固定；成功不清。Sc32 negative/domain/raw可继续由SC32查询，与errno区分；seek能力失败ESPIPE、资源ENOMEM/ENOSPC、权限EACCES、I/O EIO、broken pipe EPIPE、范围EOVERFLOW/转换ERANGE、staleESTALE等。perror先保存errno再写文字，不让诊断自身失败替换原错误文本。

assert宏每次include按NDEBUG重新选，关闭时不求值、开启时一次；失败给真实表达式/file/line后异常结束，低内存诊断不分配堆。本头当前为C89诊断位置范围，未假填C99 __func__。辅助函数都真实导出。

## 8. 静态收齐与待验证出口

静态检查只核定义/导出、外部调用/导入、manifest九字段、公共头/路径、尾空白与实际无SCAPI/asm桥；不执行库算法。所有94导出有实体，19调用与19导入匹配；公共头连同SC32API/GUI/SCTYPES静态不同宏名331，加实现/predefined后仍须真实预处理计数，静态去重不等于容量PASS。

后续由Root统一：SDK小写安装→C1..C5真实工具链→R1新Root→GCC引导与客体SCCC生成SCCLIB→实际动态应用/SCLIB调用。只针对未解决风险验证16对齐/碎片合并/realloc失败、memmove/Two-Way/strto边界、格式计数、六模式/双flush/预读CUR/ungetc、真实PIPE/TTY等待/EOF和main/explicitexit回调/回收。复用未变化Foundation/旧格式/权限证据，不自行加全矩阵或复制大量盘。

GCC种子库可用同源i386 freestanding/no-builtin/no-pie/non-PIC参数，再由mkscl处理真实ET_REL及同一清单；客体入口为 `sccc /SYS/SRC/STDLIB/SCCLIB.C /SYS/LIB/SCCLIB.SCL --scl ...`，正式LIB写入需Root规定的外部SYSTEM发布链，不给普通/root三环新权限。本文未实际运行这些命令，Root应在已授权隔离验证目录生成，再按正式发布边界安装。

实际兼容表各项当前均为“源码/静态收齐，宿主与客体待验”。没有完整ISO、性能达标、Phase3或M10a2完成声明。文件故障/断电UNCERTAIN沿SandFS合同，CRC不等于日志或原子事务。

## 修订

2026-10-15：新增真正独立SCCLIB源码/94实体导出、19公开导入、11标准头及ABI1.0清单；实现子分配/字节算法/整数转换/缓冲FILE/格式与退出协定；标准alias改首次使用，stdio PIPE/TTY/SERIAL按真实事件阻塞。仅源码和静态审阅，无本批构建或客体验证。


修订：2026-10-15，统一stdlib-build-06及guest02/04/05实际SCCLIB/SCLIB/R1/C1..C5、九模式/当前G1/253项通用工作流与精确回收通过；保留三项验证失败、未变输入复用与证据边界，见evidence/m10a2-stdlib，完整目标继续。
