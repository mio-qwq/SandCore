# SC32 核心 typed/query 与宽单调时钟

所属版本：**SandCore M10a2 — The Application Platform Era**，Phase 2首个
内核协调切片。对应 `kernel/sc32_core.c/h`、`kernel/timer.c/h`，共享合同为
`user/sc32/SC32WIRE.H`、`SC32API.H`、`SCTYPES.H`。本批主代理已统一构建，
公开typed-query/规范前缀/RO输入与RW输出/真实页计数及单调等待已实际运行，见
[SC32证据](../evidence/m10a2-sc32/README.md)，未覆盖项不能据此标PASS。
这不代表完整SC32API、四大库、GUI消息或整个M10a2完成。

## 1. 私有入口与安全前置条件

新增 `int 0x7C` 的独立功能号0x300：EBX=selector，ECX=request本人虚址
或0，EDX=output本人虚址或0，其余参数寄存器忽略。内核分发取真实当前
PID，调用 `sc32_core_call(pid,selector,request,output)`；用户不能在请求中
指定缓冲所属PID。普通应用只使用公开cdecl函数，不复制此私有寄存器桥。
旧SCAPI.H、功能号、忽略寄存器、缓冲和已发布行为保持冻结。

入口及共用helper要求单CPU的IF=0临界区、当前活用户任务及其真实私有页
目录；误用IF=1返回BUSY。所有输入先验完整本人P/US范围并复制到可信
局部记录；只读SCL中的输入合法。查询输出先读8B size/version，再要求
size处于该v1规范尺寸..65536、version=1，检查整个声明范围P/US/RW。
非法、只读、缺页、尺寸或版本错误不会改写输出。

成功只写v1规范前缀，返回头的size为规范尺寸；大缓冲未知尾部保持原
字节，不能顺手清零或扩写。保留字段在可信局部记录中清零再发布，不泄漏
内核栈。实际发布再次经paging_user_copy写方向核验，不能依赖CR0.WP未
启用的零环写权。没有跨进程用户数据地址、页目录、内核栈、物理帧等指针
被输出；Memory中的heap/module基址只是已公开的本人虚拟窗口常量。

## 2. 首五个selector

| selector | 输入 | 输出 | 行为 |
|---:|---|---|---|
| 0x01 BUFFERPROBE | 精确20B：size=20,version=1,address,bytes,access | 必须0 | 按本次本人范围检查R=1/W=2，access只允许1/2/3；不创建能力票据 |
| 0x02 CAPABILITIES | 必须0 | ScCapabilities，64B | 真实版本/已实现服务位、路径/参数容量、页粒度、PIT频率和两个用户窗口尺寸 |
| 0x03 PROCESSQUERY | 精确16B ScProcessRef | ScProcessInfo，96B | 同一临界区核对PID/代数/真实身份/会话，再交付允许的公开状态 |
| 0x04 MEMORY | 必须0 | ScMemoryInfo，64B | PF实际统计及本任务真实私有US映射/页表计数 |
| 0x05 MONOTONIC | 必须0 | ScMonotonicClock，32B | ISR维护的宽tick原子快照，当前逻辑频率与零保留字段 |

BUFFERPROBE的bytes=0没有任何访问，允许address=0，但access仍须合法。
非零读检查P/US，写检查另查PDE/PTE RW；失败为INVALID。Probe不是可
缓存授权，每次真正读写/创建服务仍须重新检查实际使用范围。未知selector
返回NOT_SUPPORTED，不读取其未知布局，不假装完成尚未实现的服务。

CAPABILITIES v1的16字依次为size/version/abi_major/abi_minor/
kernel_contract/syscall_gate/capabilities_lo/capabilities_hi/max_path_bytes/
max_arguments_bytes/page_size/pit_hz/heap_bytes/module_bytes/0/0。
目前ABI1.0、kernel_contract1、gate0x300、路径65B（含根斜杠与NUL）、
参数1024B、页4096B，图形堆和模块窗口各64MiB。这是单对象/地址域
合同，不替代取消固定任务、窗口或句柄数量上限的要求。

当前置位CORE_QUERY、PROCESS、PRIVATE_PAGES、PATH_IO、JOBS_STREAMS、
SESSIONS、THEMES、DISPLAY、LIBRARIES、MONOTONIC、UTC_CLOCK；这些
表示真实存在的内核服务合同，不保证特定文件/设备当前就绪。高字为0。
DIRECTORIES/UPDATE_FILES待独立FILE切片真正接通；WINDOW_MESSAGES、
CLIPBOARD、IPC、SHARED_MEMORY为0，不凭旧FRAME或顺序替换流推断
新的完整语义。后续新增能力须由实际接线和相应合同决定，同批更新此表。

## 3. 带代数的进程查询

ScProcessRef的四字为size=16/version=1/pid/generation。pid=0且generation=0
请求本人；其它引用必须有非零代数且PID在原31位编号域中。输入来自本人
只读范围也可。先完整预检96B及调用者声明的额外输出范围，再读取引用，
不先做操作后才盲写结果，也不通过旧STATUS查询值拼出一个新身份。

对象必须仍由task_store持有，代数精确相等，auth_info能核验当前凭据。
对象已回收、代数陈旧或退出后凭据不可核验，返回STALE；原STATUS/JOB
的退出结果保留仍走旧合同，新接口不伪造已经丢失的退出身份。

权限为：本人可查询；真实外部串口授权SYSTEM可查询；普通root只管理
普通目标；普通相同UID还必须是同一非零实际登录会话。其它SYSTEM服务
不能因为UID都为-1便读取另一SYSTEM或内核任务状态，普通root也不因
管理普通账户而获得外部串口最高资格。拒绝时输出不改变。

成功96B的24字/字节字段以公共头为准：size/version/pid/generation/state，
真实uid/gid，realm/session/flags，exit_code，waiting/wait_mask/
pending_events/running_samples/dispatches，16B name，四字零保留。
flags为DETAILS|REALM_VALID，realm来自真实凭据；name复制旧12B名称并
保留后4B零。仅查询活/暂停对象，exit_code为0，故障暂停的暂存诊断码不
冒充已完成EXIT。state沿原1/3编码；等待是独立字段，不以state=1宣称
它正在CPU上执行。样本/派发数沿既有u32 PIT统计，不当作墙钟或FPS。

## 4. 真实内存统计

ScMemoryInfo v1的16字为size/version/page_size/managed_bytes/used_bytes/
free_bytes/heap_base/heap_bytes/module_base/module_bytes/process_heap_pages/
process_module_pages/process_total_pages/page_table_pages/0/0。

managed/used/free来自PF位图分配器的当前实际计数，包含原有保留与所有
分配。heap_base为0x40000000、module_base为0x50000000，二者长度各
64MiB；它们是本人虚拟地址域，不是已分配物理内存或可写内核别名。

逐本任务现存P/US私有页表统计：heap/module分别计相应窗口的US映射
正文页；total包含本人SCX代码/BSS、普通/异常栈、图形堆和模块所有US
映射正文，不包含页表或内核栈。page_table_pages为本人页目录1页加所有
现存US私有页表。RO代码/数据同样计入正文，RW只影响访问，不改变归属。

计数不分配新页，不扫其它任务或PID历史，不把旧MONITOR的全局PF余额
当作进程内存。当前最多33张私有PT，每张1024项，成本按本人现存页表
规模；这不是任务/模块总数限制。内核栈、任务/凭据/SIMD旁表、记录池和
共享缓存已经在全局used中，不重复伪造成process_total。实际查询耗时及
客体页账由统一定向验证记录，源码复杂度不冒充实测性能。

## 5. 宽单调时钟

旧volatile u32 sc_ticks继续每个IRQ0加一并自然回绕，GETTICK与旧等待
语义不改。timer_tick在同一次IF=0中执行低字递增，并在真实回绕时增加
私有高字。高字从启动BSS的0开始，不由应用偶尔查询或RTC时间猜回绕，
也不因再次配置PIT而重置旧计数。

timer_monotonic(out[2])短CLI同时读取lo/hi，保留调用前IF；typed gate原
IF=0保持关闭。ScMonotonicClock为32B：size=32/version=1/ticks.lo/
ticks.hi/hz/0/0/0。正常启动PIT配置100Hz，hz报告当前配置的逻辑频率，
不宣称物理振荡器没有误差、1ms精度、FPS或宿主墙钟。这里没有消息
timer对象、回调或RTC日历混入单调值，也不从IRQ进入用户代码。

## 6. 共用helper与接线

sc32_core_input(pid,address,local,bytes)要求固定请求的size精确等于服务端
bytes、version=1，整区读取后才复制；bytes须来自可信布局宽度。
sc32_core_output(pid,address,bytes)只预检版本和完整声明RW范围，不发布
结果。其它同gate服务调用它们后仍须按操作事务安排发布，成功只写已知
前缀，实际IO部分完成另按本服务的正式计数合同处理，不能泛化为参数失败
也可以部分写入。两个helper均只接受真实当前活用户任务与IF=0。

本切片不改task.c/h、task_store、WM/FS或用户API头。主代理统一接0x300
分发和sc32_core.o构建规则，并按独立FILE/GUI等selector范围调各自
服务，不重解释旧功能号。必要定向出口是新库实际动态调用、只读输入与
完整输出拒绝/护栏、PID代数及身份/会话拒绝、真实内存账和宽tick进位；
已有未受影响证据可复用，不生成重复测试盘或扩展大矩阵。

## 修订

2026-10-14：实现0x300首五typed/query selector、共用安全输入/输出前检、
真实进程身份/会话与页账、IRQ0宽单调进位；公共SCAPI.H冻结保持。本批
仅源码与静态检查，构建/客体执行由主代理统一进行，未宣称Phase 2完成。

2026-10-15：主代理统一构建、实际客体生成的库/公开应用与三窗口P0运行通过；首Shell背景表一页单列，暖PF及GUI池精确恢复，证据见evidence/m10a2-sc32；完整平台继续。
