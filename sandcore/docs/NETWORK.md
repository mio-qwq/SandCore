# M10a1 IPv4 与 e1000 合同

> **2026-10-07 network-10终态：** 退出1。第一来源isolated56/applications93/no-nic4声明全部PASS，包括18实际原生编译、新nc-e与连续监听关闭；第二来源隔离链路在controlled-DHCP宿主40秒标记等待超时，完整FAIL。随后只读诊断实际BOUND且原命令退出0、PIT推进、无ATA/UART/坏释放错误，不能据此改写超时为PASS。网络定时/服务时延原因继续排查；同时sessions-07证实冷桌面原始壁纸加载阻断task0服务，已补分批加载源码，尚未构建/实测。公钥/签名提交时间与该失败无关。

> **2026-10-07 network-09终态：** runner退出1。首来源isolated56项通过，新增地址复用/旧选项21项检查全部PASS；客体编译前15工具通过，nc源码第22行unknown identifier失败。只读输入确认SYS/INC/SCNET.H已更新，NETCLI相对包含的SYS/SRC/SCNET.H仍旧，故本次应用未继续，不能算全部网络通过。现补SRC副本并给网络SCX/源码增加双目录三份头的资源依赖，不改候选主核。publish-03新双盘退出0；network-10待当前独立Shell stdin回归结束后运行。shell-run-01 PID3964执行中，正常EOF/引号/循环/heredoc/read等已有分项，完整终态未收齐。全部原失败和原盘保留，Goal active。

> **同日network-08终态：** 退出1。首来源isolated54/applications93/no-nic4声明全部PASS；第二来源isolated54、18工具原生编译/启动及nc-e cat/sh/权限/退出通过，随后重新监听未绑定。原错误文件只读查到nc:-98，CPU正常HLT/IF=1且时钟前进，坏释放计数全0、ATA失败/超时0。地址被TIME_WAIT占用，现新增REUSEADDR选项5、nc TCP监听使用，上游原TIME_WAIT保留。net-reuse-fix-01构建退出0，正文379536B/BSS327432B，正文SHA a33bf9f9cb18f7b82136cc87195ddb86541b195471ca8200bde7fd3601c4c4a5。新复用版本尚待双来源实测，旧完整矩阵仍失败。

> **同日TCP关闭修复：** net-free-diagnostic-01的network-07已退出1，坏释放记录指向tcp_free+72，256B类第2槽占用位已清空，确认再次释放TCP PCB。socket排队关闭时保留destroy通知，实际tcp_close调用前才解绑；失败重试恢复通知，成功后方可释放socket。net-close-fix-01构建/独立两盘迁移通过，正文SHA bf43cd35770d95a2b7bed31ede0c89c3d430ae88027e3e288986e26cf569184c。network-08 PID11648正在完整三profile/两来源/18原生重编译；首来源isolated54项声明PASS，短程序及连续关闭子项已实际推进，完整终态待收齐。core10旧通过不代表新主核验收。

> **2026-10-07当前证据：** core10的隔离网络声明子集已实际Windows QEMU通过，18工具已客体编译并运行帮助；nc-e cat、sh的7B纯正文/退出7和普通root权限子项通过。完整网络合同未验收。network-06单独applications诊断仍失败，实际截图为NET FREE SLOT断言，CPU停在panic/IF=0且时钟不前进，ATA失败/超时0，管理超时是后续症状。新增坏释放的私有只读布局/调用地址计数，独立net-free-diagnostic-01构建/两盘迁移通过；尚待该映像重现，不能把假设当作实测根因。保持所有旧证据和源码，下一批范围重新按主核摘要区分。

> 全部内容对应新增源码，尚未构建、客体执行或网络验收。源码接线不是吞吐、可靠性或兼容证据。

## 设备与协议

本轮设备为 Windows QEMU 的 `e1000` / Intel 82540EM，PCI `8086:100e`，接口名 `en0`。自写驱动依据 [Intel 8254x SDM](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf) 的寄存器与 legacy 描述符合同；未移植其它驱动或 QEMU GPL 实现。PCI/BAR 探测、监督态 PCD/PWT MMIO 映射、共享 IRQ、DMA、链路、异步复位与发送 watchdog 均有源码；不宣称支持其它真实网卡。

IPv4 主机栈为固定 lwIP 2.2.1，提交 `77dcd25a72509eb83f72b033d219b1d40cd8eb95`。以太网、ARP/静态邻居、IPv4/分片与重组、ICMP、UDP、TCP/监听/接受/重传/乱序/拥塞与窗口、DHCP 客户端及地址冲突检查、DNS A 查询/缓存/随机事务标识启用。单物理接口、默认 MTU 1500、静态地址/DNS/网关及最长前缀路由可配置。配置是本次启动的运行状态，尚不提供持久网络配置文件。

IPv6、转发/桥接、IGMP 多播订阅、PPP、TLS、上游应用服务器未启用；这些不是首批18工具的行为覆盖。IP 广播与 UDP 广播有独立 socket 选项。本机 IPv4 回环通过同一受控协议上下文处理，不借宿主 loopback 冒充客体收发。初始化需要实际网卡和已有强熵源；无设备/熵失败保留桌面并报告启动状态，不填入 QEMU 默认 IP 冒充租约成功。

## 服务、资源与生命周期

IRQ 只读取/清除设备原因、标记待处理并唤醒 task0；不分配协议对象、不调用 lwIP。raw API 在 IF=0 的系统调用或 task0 持有用户调度锁的串行网络服务中执行。服务期间可以接受内置 IRQ；用户任务在批次间重新调度。

每次服务最多处理32个 RX 描述符、64个 TX 完成、32个回环 IP 报文、8个到期定时回调、64次 ARP 消费者投递、64个延迟关闭/丢弃步骤及64个配置通知对象。各协议定时回调内部仍使用上游算法；“8个回调”不等于固定毫秒预算，完整规模的长尾测量和必要修复属于统一验收，当前没有性能达标结论。

DMA 环各128个描述符，各有128×2048B缓冲，共130个物理页。TX 留一描述符区分空/满，满环返回背压，不覆盖 DMA 正文；失败复位后可能仍被设备访问的页留在设备隔离区，到重启才结束其生命周期。成功复位复用原 DMA 页，不反复分配。驱动 MTU/帧范围14..1514B；超出或多描述符坏帧丢弃并记录。

驱动快照state为ABSENT0、RESETTING1、RUNNING2、FAILED3、STOPPED4、
START_PENDING5。启动发现/映射后关闭DMA并进入5；首次设备服务轮才
写CTRL复位并启动25tick截止时间，避免字体/欢迎页初始化耗时造成
尚未第一次观察设备就误报超时。复位位确实未清才判硬件超时；EEPROM
读取仍分阶段、有真实截止时间。公开信息记录FAILED/error=5等失败，
不能把协议栈ready当设备正常；实际复位/断链时延矩阵仍须验证。

协议小块分配器按真实 PF 页增长，32..2048B分类页和大块连续页都有实际计账，空分类页归还。PCB/节点使用动态分配，没有固定全局 socket/任务数量门槛。单连接 RX 128KiB/64消息、TCP窗口/发送正文32KiB、发送队列128项、乱序32KiB/64 pbuf；ARP缓存128、每邻居排队8、DNS缓存/并发表32、重组/回环64 pbuf 是明确的协议资源背压。它们不限制任务创建或 socket 对象总数。ICMP echo 的16位标识空间单独管理，65536个同时活动的标识是线缆格式边界。

周期定时节点在初始化时按实际 cyclic 表预留，回调前归专用保留链，普通 ARP/PCB OOM 不能抢走续约的最后节点；额外节点仍可按内存增长。维护节点归整个协议启动周期并计入统计。意外定时节点丢失报告协议失败并停止新收发；不能把失败状态当“空闲”或联网成功。

致命失败收尾撤DHCP/ARP待发项、每轮丢弃最多32个回环包、终止最多32个TCP PCB。分片与DNS每轮各执行一次终止清理，按固定配置的最长寿命/重试上界结束；DNS随机UDP查询端口随后由上游释放。这个过程不推进正常租约或伪报联网成功。新查询被拒绝，迟到回调仍核单调句柄；设备隔离页和协议周期保留页的启动生命周期另计。

Socket 对象地址稳定，31位正句柄单调发放、不在本次启动复用，绑定 PID+代数和拥有者链。主动关闭先撤回调/ARP订阅，再预算化释放接收包、子连接和 PCB；TCP close 的暂时内存失败重试，500tick仍失败则 abort。TIME_WAIT 由协议处理并回收。任务退出等待自己的网络对象实际撤销后才释放身份/代数，迟到 DNS 回调仅按单调句柄重新查对象，不持有已释放用户指针。

## 地址、就绪和错误

地址固定四个 u32：`1,ipv4,port,0`。`ipv4=A<<24|B<<16|C<<8|D`；端口为主机序0..65535。绑定地址只能ANY、本机地址或回环；端口0选择临时端口。低于1024端口要求root或外部授权SYSTEM。TCP/UDP协议参数0或本协议号；ICMP为echo用途，ARP socket仅协议0x0806。

调用非阻塞。`-11`需要事件让出再重试，连接首次成功启动返回`-115`。`TASK_EVENT_NETWORK/SC_EVENT_NETWORK=16`是提示；等待前检查状态，醒来重试真实操作。READ1、WRITE2、ERROR4、HUP8、ACCEPT16。READ可表示数据或EOF，TCP队列先读完再报告错误/EOF；WRITE不预留内存，下一次发送仍可能失败，用户库有再次让出路径。

常见错误：`-9`无效/非本人句柄、`-12`分配失败、`-13`权限、`-14`用户范围、`-19`无设备、`-22`参数、`-36`名称过长、`-90`报文过大、`-95`不支持、`-98`地址占用、`-99`地址不可用、`-100`网络不可用、`-101`无路由、`-103/104`中止/重置、`-107`未连接、`-108`关闭、`-110`超时、`-114`已开始、`-115`连接进行中、`-126`配置缺强熵。NETCONTROL无设备返回-19/缺熵-126；socket操作的未就绪状态返回-100。其它上游错误映射见network.c，不能用一个负数冒充数据长度。

TCP发送每次最多16KiB并可部分接受；复制成功后即使暂时无法输出也保留协议队列。UDP每报文≤65507B，ICMP正文≤65515B，用户缓冲单调用≤65536B。UDP/ICMP/ARP接收缓冲过小时消费并截断该报文，STATUS报告原消息长度/截断；TCP保留余下字节。零长度UDP是合法报文；零长度TCP读写不消费正文。

ICMP仅允许发echo request，内核填独立标识/校验和；可收对应echo reply及引用该echo的ICMP错误。ARP发送必须使用本机MAC、当前IPv4或0的地址冲突探测身份，只能ARP请求/响应，不提供任意以太网伪造或全帧抓包。每帧独立不可变快照，订阅游标只枚举ARP对象，后建对象不接收历史帧。

## 新系统调用

所有输出先检查完整用户范围；不改旧忽略寄存器、缓冲或功能号。

| 号 | SCAPI包装 | EBX / ECX / EDX / ESI | 返回 |
|---|---|---|---|
| 250 | sc_net_info | 输出384B | 0/负 |
| 251 | sc_net_control | 操作 / 输入128B | 0/负 |
| 252 | sc_socket | 类型1TCP/2UDP/3ICMP/4ARP / 协议 | 正句柄/负 |
| 253/254 | sc_socket_bind/connect | 句柄 / 地址16B | 0/负，TCP连接-115 |
| 255 | sc_socket_listen | 句柄 / backlog1..255 | 0/负 |
| 256 | sc_socket_accept | 句柄 / 可空地址输出16B | 子句柄/-11/负 |
| 257/258 | sc_socket_send/receive | 句柄 / 正文 / 字节 / 可空地址16B | 实际字节/负 |
| 259 | sc_socket_shutdown | 句柄 / how0读、1写、2双方 | 0/负 |
| 25A | sc_socket_close | 句柄 | 0/负，后台回收 |
| 25B | sc_socket_status | 句柄 / 输出64B | 0/负 |
| 25C | sc_socket_option | 句柄 / 选项 / 值 | 0/负 |
| 25D | sc_socket_page | 输出 / 字数32..1040 / 游标 | 本页行数/负 |
| 25E/25F | sc_route_page/arp_page | 输出 / 字数24..528 / 游标 | 本页行数/负 |
| 260 | sc_dns_start | NUL名称≤255B | 正票据/负 |
| 261 | sc_dns_status | 票据 / 输出32B | 0/负 |
| 262 | sc_dns_cancel | 票据 | 0/负 |
| 263 | sc_net_exec | 程序命令 / 三个i32描述符12B | 正作业票据/负 |

表内号均十六进制。除明确使用外，其余寄存器忽略。SCAPI的page包装参数顺序为`cursor,out,words`，内核寄存器顺序为表中顺序。独立便利库SCNET.H不提供BSD/libc兼容。

STATUS16字：`1,type,state,ready,error,rx_bytes,rx_messages,accept_count,local_ip,local_port,remote_ip,remote_port,last_message,truncated,icmp_identifier,config_epoch`。state=NEW1、BOUND2、CONNECTING3、CONNECTED4、LISTEN5、ERROR6、CLOSING7、DNS8。选项1TTL1..255、2UDP广播0/1、3TCP keepalive0/1、4TCP NODELAY0/1；新增选项5TCP/UDP REUSEADDR0/1，默认关闭，SCNET.H给出SC_NET_OPT_REUSEADDR。nc仅TCP监听在绑定前启用，使真实TIME_WAIT仍按旧四元组保留而新监听可复用原本地端口；不撤销TIME_WAIT或改变序号。选项1..4和调用寄存器/返回布局保持，UDP广播不复制给全部重用socket。

所有PAGE头16字清零后填：`1,epoch,count,cursor,total,row_words`，其余保留0；socket/ARP的total目前0，route是显式动态路由数。游标0开始/0结束。socket每次最多扫描256个对象，过滤后可得到空页及非零后续游标；不能因空页提前停止。每次最多64行，尾部按声明字数清零。跨页不冻结变化。

Socket行16字：`handle,pid,generation,type,state,local_ip,local_port,remote_ip,remote_port,rx_bytes,sent_bytes,received_bytes,error,dropped,ready,icmp_identifier`。普通账户仅同文件主体UID且代数有效的对象；root/外部SYSTEM可读全部，查询权限不授予操作他人句柄的能力。

Route行8字：`entry_id,destination,prefix,gateway,metric,1,0,0`，entry_id为内部索引+1，后续cursor同值；最长前缀命中同prefix替换，不用metric选择多路径。connected和接口默认网关来自INFO，工具另列。ARP行8字：`cache_index,ipv4,mac_low4,mac_high2,1,0,0,0`；游标为下一个缓存下标，列已解析项，不显示未知/未解析项或区分静态/动态标志。

DNS8字：`1,state,ipv4,error,epoch,0,0,handle`，state=1待解析/2成功/3失败；取消后票据失效，缓存归栈。内核接口为A查询，SCNET便利层把严格四段数字和localhost直接解析，配置domain不偷偷改调用者传入名字。

INFO96字：0版本1、1配置代数、2 flags(READY1/ADMIN2/LINK4/DHCP8/FAILED16)、3启动错误；4IPv4、5mask、6gateway、7/8DNS、9MTU、10/11MAC；12DHCPstate、13tries、14lease_seconds、15used_seconds；16socket数、17用户接收排队字节、18协议PF页、19峰值页、20实际申请字节、21入口分配/协议丢弃、22路由错误、23服务批次、24lwIP版本、25..31IP recv/drop/err、TCP recv/xmit、UDP recv、ICMP recv。32..47hostname64B、48..63domain64B。

64..95驱动32字：`1,state,epoch,link,pci_address,bar_base,bar_bytes,irq_line,irq_pin,mac_low,mac_high,rx_packets,rx_bytes,rx_dropped,rx_bad,rx_overflow,tx_packets,tx_bytes,tx_bad,tx_backpressure,tx_inflight,irq_count,resets,watchdogs,error,last_irq,dma_pages,rx_next,tx_next,tx_clean,ring_count,buffer_bytes`。驱动state=ABSENT0/RESETTING1/RUNNING2/FAILED3/STOPPED4。启动错误=1设备初始化/2强熵/3协议初始化资源/4 DHCP启动/5意外定时器丢失。协议页统计不含DMA/动态对象池页，不能当网络总页占用。

## 配置与远程程序权限

CONTROL输入32字，第0字版本1。操作1静态地址：1IP/2mask/3gateway/4DNS0/5DNS1/6MTU68..1500；2up、3down、4DHCP启动、5renew、6release、7reset无额外字段；8DNS取1/2；9hostname/10domain从第1字起NUL字符串≤63B；11静态ARP取1IP/2MAC低4/3MAC高2；12删除静态ARP取1IP；13清ARP；14/15增删路由取1目的网络/2prefix0..32/3gateway/4metric。hostname/domain当前接受ASCII字母数字横线，domain另接受点；hostname不可空、domain可清空。静态地址要求连续非零掩码/合法单播，网关同网段且非本机。修改需root/外部授权SYSTEM，普通联网不要求管理权限。

用户2026-10-06追加明确要求`nc -e`。NETEXEC只接收三个管道端点，分别读/写/写；返回原作业生命周期票据。普通/root取本人文件主体，受委托服务取其普通主体；外部SYSTEM明确调用时新孩子为root/AUTH_NORMAL，外部会话票据清零。无委托主体的普通SYSTEM服务不能借此变为root。子进程及后代不能取得AUTH_SERIAL、UART端点、MOD/CORE零环编辑资格。旧SPAWN2及串口最高权限语义不改。网络是数据管道，不能成为管理串口接收状态机。

## 验收与来源归档

原始lwIP完整固定文件/声明归`/SYS/LICENSE/LWIP`；私有适配与自写驱动/桥/socket归`/SYS/NETSRC`。来源、摘要、实际条款见[THIRD-PARTY](THIRD-PARTY.md)。18工具源码与选项及完整55行参考见[CLI-M10](CLI-M10.md)。

`tools/verify_m10_network.py`已写双来源盘入口，依赖原创`m10netpeer.py`与`m10netservices.py`。隔离socket以太网验证DHCP/DNS/ARP/ICMP/UDP及逆序分片；user后端验证TCP/UDP流、nc -e、HTTP正文/坏帧/回滚、TFTP重复/丢包/终块。原生编译18工具和M10NET公开ABI探针、131同时socket/缓冲边界、真实HMP输入/截图都有用例源码。每项另记退出码/字节/CPU/耗时，PCAP与对端摘要独立；脚本只输出已声明用例的状态，不输出整轮完成。TCP线缆丢包/乱序/窗口、真实耗尽与长尾、普通身份/跨任务句柄、TFTP大回绕、复位/恢复仍待补齐和实际执行。

首轮实际运行在network-01停止：M10NET客体编译通过，DHCP超时，双方
PCAP无帧。network-diagnostic-02完整公开快照确认state=FAILED3、
error=5、DMA页0、无TX/RX：冷启动提交复位到首次轮询之间超过25tick。
已修启动时序；network-03首盘20秒DHCP和隔离链路用例通过，完整
双盘/权限/故障矩阵仍待验，不能宣称网络完整通过。

network-02首DNS失败源于宿主对端event参数名与DNS详情name冲突，
原异常/抓包保留；改event_name后03实际DNS/缓存/负答/超时通过。
03首盘另有ARP/DAD、ICMP/65000B分片、TTL引用错误、131socket、
0/1/8193/60000B逆序分片UDP、接口下线/恢复/重取地址实证。
应用profile首盘18工具客体编译/启动及二进制TCP/UDP/nc -e回显通过，
随后wait跨Shell作业归属错误退出127；等待改为原交互Shell，后续
程序退出码/身份/HTTP/TFTP仍待重测。profile可筛选只用于定向定位，
JSON明确scope/complete_profiles；局部成功不能代替最终完整双盘矩阵。

修订：2026-10-06，登记首轮DHCP失败/公开设备快照，补START_PENDING
实际复位提交时序；失败截图/原始异常与预期关闭事件独立归档，重测待验。

统一阶段需用明确`-netdev ... -device e1000,netdev=...`的Windows QEMU、强熵输入与两个独立来源数据盘，保留原盘。NAT外联与受控隔离以太网对端分开：QEMU user网络的ICMP限制不能冒充客体失败或成功。实测DHCP/DNS、ARP/DAD、ICMP错误、分片/乱序/重传、环回绕、并发监听/接受/半关闭、断链/复位/无设备、OOM/退出/句柄复用与真实页回收；传输摘要、截断/坏包、nc -e子程序/权限/回收和全部工具行为逐项记录。网络负载下的输入/绘制长尾及真实CPU/吞吐必须同配置对照。

修订：2026-10-06，建立自写e1000/lwIP整数适配、动态socket、0x250..263、运行配置/权限与资源合同；18工具源码接线，nc -e获用户明确追加；全部未构建未运行。

修订：2026-10-06，补致命状态DNS/分片/TCP终止清理及双对端/双来源盘验收源码，nc -e短程序/破管保留尾部；无新运行证据。

修订：2026-10-06，登记build-05/network-03首盘隔离链路及18工具
原生编译证据，保留对端参数/原Shell作业归属失败；完整双盘仍待验。

修订：2026-10-07，记录network-09新增选项实际PASS子项与客体nc编译失败；补SRC/INC双处头文件的定向资源依赖。

修订：2026-10-07，记录network-10与sessions-07真实失败，补冷桌面原始资源分批读盘/候选原子交付源码及独立构建通过；新版客体、最终像素、页账与性能尚待验证。

network11退出1。最后60000B用例实际stdout只有发送失败，未进入
接收；前8193B通过时PIT5949，失败命令前PIT6012，过全局60秒
coarse tick。失败公开快照IP/mask/gateway均0，DHCP状态CHECKING8、
租约60；对端同期真实RELEASE/重新DISCOVER/REQUEST/ACK，捕获无
60000B报文。DMA130页/RUNNING2/link1、RX坏/溢出/丢弃及TX坏/
背压/看门狗/错误均0，ATA/UART/NET坏释放0。该失败是地址生命周期
前置条件失效，不能改称通过或说UDP重组本身失败。分片矩阵现在
先显式静态配置，再保留原0/1/8193/60000B各用例及后续DHCP恢复，
不延长任何原命令超时。短租约续期/重绑定/到期另列必验待验。


原创M10TCP.C/有限m10tcppeer.py及双盘verify_m10_tcp_faults.py已写，丢SYN/数据、窗口、乱序重复、半关闭/EOF和RST仅待验源码，完整拥塞/性能/资源仍须补齐。

修订：2026-10-07，记录network11短租约导致地址撤下的实际失败和分片显式静态前置，补有限TCP线缆故障待验源码；不延长原超时。

### 2026-10-07 tcp-faults01双来源独立线缆声明通过

runner退出0，tcp-faults-matrix.json为DECLARED_CASES_PASS，两来源
各9项完成：实际S3C编译两个探针、静态隔离接口、故障交换、独立
线缆核对、拒绝SYN、已建立RST、netstat查询和RST线缆证据。两原
数据盘摘要保持。仅固定三连接故障对端，不是另一个通用TCP栈。

两盘各实见第一次SYN被丢、第二次同ISN成功；第一次数据段被丢，
同序列/长度/摘要段再次发送，实际0窗口后恢复，零窗口期间另有
1个包，不能仅据此称完整persist时序已验。对端上行完整16384B
SHA6c946c0a9501d2a9fb9dc65bb931071233fb28a2820a8be06344e1482f5e6bfc。
客体按每个原字节核4097B下行，独立对端先乱序/重复再缺口补齐，
server ISN0xfffffc00跨32位序号边界；没有重复交付，真实FIN之后
EOF0，guest transmit half shutdown后send返回-108但接收继续。
独立线缆见最终FIN ACK，没有非预期guest reset。拒绝SYN和建立
后的真实RST都给公开-104，状态/64B护栏通过，关闭返回0。

原始PCAP、peer.json和独立tcp-summary.json、客体精确输出与
失败护栏来源分别保留。netstat项目只要求查询成功，不冒充零
socket或物理页回收。完整拥塞/长尾吞吐/persist、多连接压力、
跨所有者代数/权限/退出/OOM与真实页总账、驱动链路/复位/回绕
仍须继续；此声明通过不是整个网络/M10a1完成。

network12在同一desktop02主核重新执行完整三profile/双来源及
18工具原生编译；分片前已明确静态配置，不扩大原超时。此轮
终态尚未到，原network11租约到期/失败不改。所有旧FAIL保留。

修订：2026-10-07，补双来源原始TCP丢包/窗口恢复/乱序重复/半关闭与两类RST实际声明通过，保持完整拥塞/权限/资源/整机范围待验。

### 2026-10-07 socket权限与真实耗尽探针待验

新增tests/m10/M10NRES.C、tools/verify_m10_network_resources.py，
在现有desktop02主核/两个256MiB只读来源上通过公开ABI执行。
普通mio隐藏登录实际向隔离对端发送18B并逐字节收回；父子双方
不能操作另一PID私有socket，普通身份不能更改网络、CORE权限或
绑定低端口。孩子故意不close，真实EOF退出后等待内核回收。

独立同进程阶段动态创建UDP到真正-12资源失败，要求超过旧数量，
八次重试前后socket/PF数稳定，全部关闭后等待异步回收完成并
精确核对NETINFO协议页/活字节与MONITOR实际空闲物理页基线。
百万项数组只限夹具预算，若未耗尽就失败；不是内核总socket门槛。
逆序撤销匹配上游PCB头插链，避免耗尽探针人为制造平方级尾删，
此结果不会冒充任意关闭顺序性能。全部失败路径恢复stdio、释放
票据/管道/孩子，不让夹具自身泄漏被误判为内核失败。

资源脚本还核对独立peer.json/PCAP中的真实18B摘要、耗尽后真实
ping及零逻辑socket，并保存真实HMP截图。当前仅静态/AST及21
文件逐字节冻结通过，runner01已准备但尚未启动；输入源、CORE
和符号均使用已固定的desktop02/desktop-inputs02。新的测试源码
将在最终整包构建时归档；当前输入盘通过宿主上传此原始探针。
冻结build/m10a1-network-resources01-source-freeze/manifest.json。

network12唯一VM仍运行：第一来源三profile完成，第二来源进行中，
完整终态未收齐。短DHCP租约续期/重绑定/到期、驱动复位/DMA、
并发TCP/拥塞/吞吐和全部输入长尾继续，不将夹具或旧证据称完整
网络/M10a1通过。上游粗租约定时默认60秒，60秒租约T1/T2换算
可能重合；下一独立线缆实测应先证明此问题，再调整适配配置。

修订：2026-10-07，补socket跨身份/隐藏收发及真实OOM回滚/PF账待验源码与冻结，完整目标保持。

### 2026-10-07短DHCP租约生命周期待验

新增M10DHCP.C、LeasePeer和verify_m10_dhcp_lifecycle.py。对端独立
给出60秒租约/T1=20秒/T2=40秒，保存真实IP目的地址、ciaddr、
选项和PCAP；分别允许全部应答、丢弃单播续期、完全停止租约应答。
客体只用公开NETCTRL/NETINFO及真实PIT，不主动调用RENEW、不改
客体时钟；要求到期前自动续期，续期无应答后广播重绑定，到期时
撤地址/掩码/默认网关但保持接口up，服务恢复后自动重新获租。
宿主逐阶段切换真实线缆策略，恢复选择请求须晚于恢复事件，不能
用最初绑定记录冒充恢复；续期/重绑定不携带选址/服务器ID选项。
每种状态后再实际ping。AST和20文件原字节冻结通过，runner01
已准备，尚未启动；当前desktop02原适配未改，先实测再修复。
长/无限租约算术、驱动/全部网络性能仍独立待验。

冻结build/m10a1-dhcp-lifecycle01-source-freeze/manifest.json。
只使用公开候选、公钥和用户签名；没有私钥访问或重新签署。

修订：2026-10-07，补短租约自动续期/重绑定/到期撤址和恢复待验接线，不改默认粒度或现有证据。

### 2026-10-07 network12完整声明矩阵通过

runner退出0，network-matrix.json为DECLARED_NETWORK_CASES_PASS，
complete_profiles/native_required均true。两个不同只读来源各完成
隔离57、应用93、无网卡4项，合计308个声明检查全部PASS；六个
profile均核原输入摘要未变，两盘各18工具实际S3C编译/运行通过。
终态独立审计保存build/m10a1-network-12/declared-audit.json，
矩阵SHA3d784c7b72fe4d0bc1ba7ea7b053c4d41b253959ffe24dcde43e87d882175b78。

DHCP/DNS/ARP/ICMP/TTL引用错误、旧选项边界、131动态socket、
显式静态前置下0/1/8193/60000B逆序分片都有真实线缆和客体字节。
应用检查包含18工具原生闭环、二进制TCP/UDP、nc -e cat/sh/身份/
快速关闭及原Shell实际退出状态，HTTP定长/chunked/关连接/重定向
和错误保留旧目标，HTTPS明确不支持，TFTP重复/丢包/末ACK/多块/
空文件/上传及对端摘要。无网卡状态/计算工具/错误路径另4项。
分片静态前置只保证该声明场景的有效地址，不冒充短租约续期通过。

实际查看第二来源application-network-desktop.png的1920×1080
画面，图标/文字/原壁纸可辨识，SYSTEM管理窗口没有进入mio桌面。
该图不是隐藏全部GUI或原壁纸像素对照的代替品；12最终工作区像素
相等证据仍独立属于sessions10/desktop-pixels03。六张HMP图保留。

resources01/PID17616已在同一desktop02/两盘启动，两探针实际
编译及静态接口通过；完整权限/真实OOM页账终态尚未到。租约三
模式探针只冻结待验；驱动链路/复位/DMA/TFTP32MiB序号回绕、
吞吐/拥塞/输入长尾及全部其它M10a1合同继续。旧网络失败均保留，
此308声明通过不是完整网络或M10a1验收。无commit/push/Release。

修订：2026-10-07，登记当前主核network12双来源六profile/308声明终态与18客体编译，保留剩余完整合同。

### 2026-10-07 socket真实耗尽首来源实测，第二来源继续

resources01首来源八个声明检查全部通过，普通mio隐藏会话真实18B
UDP对端摘要、父子跨PID所有权、配置/低端口/CORE拒绝及未close
退出全部完成。权限六个原生PASS和耗尽四个原生PASS已逐项核数量，
原盘摘要保持，真实ping/零逻辑socket仍正常。不是全网络验收。

同一探针进程实际创建388125个未绑定UDP对象后返回-12；八次
再次创建都失败且socket/PF计数不变。完整关闭后实空闲物理页
54808→54808，协议页2→2，活协议字节基线相同的原生条件通过，
新句柄仍有效、旧句柄无效。只是内存耗尽测试，未绑定PCB没有加入
上游udp_pcbs活动链；逆序撤销不代表已绑定长链任意关闭性能。
夹具注释先前泛称创建头插链不精确，执行结束后会更正并保留
实际已运行源码摘要/冻结；不改已经执行的代码或原证据。

记录socket_exhaust_ticks=3621，宿主命令162.5秒，QEMU CPU
17.296875秒；三者分列，PIT采样/宿主时钟不同，不能据此计算
整机FPS或宣称吞吐/输入性能达标。首来源审计为
build/m10a1-network-resources-01/first-source-audit.json，当前
第二来源仍执行，完整双盘终态未收齐；没有新内核改动或Release。

修订：2026-10-07，补未绑定UDP真实388125对象耗尽/回滚/精确PF首来源数据，保留第二来源及全连接性能待验。

### 2026-10-07 resources01双来源真实耗尽与权限声明通过

runner退出0，network-resources-matrix.json为DECLARED_CASES_PASS。
两来源各八声明、权限六原生/耗尽四原生检查全部通过，合计16/20。
两输入摘要不变，fixture实跑SHA75dc2749122f44640ae1a454a3da904deffcc662fd7b81ab62b4d7552667add3，原始冻结01保留。
独立审计build/m10a1-network-resources-01/declared-audit.json，
矩阵SHA1e9ef40a639bca389b4f4239b0c45484cd6b1b31d560ab8adbd24355b9b8ec7f。

普通mio隐藏登录真实18B发送/逐字接收及独立对端摘要，两向跨PID
私有socket拒绝、普通配置/低端口/CORE拒绝、未close的正常EOF
退出回收、父子清理后逻辑socket/队列归零都有原生结果。此范围
没有用隐藏停止绘制替代网络后台工作，也没有SYSTEM身份绕过。

| 来源 | 真实创建后-12 | PF关闭前/后 | 协议页前/后 | PIT耗尽/回收tick | 宿主命令秒 | QEMU CPU秒 |
|---|---:|---|---|---:|---:|---:|
| 1 | 388125 | 54808 / 54808 | 2 / 2 | 3621 | 162.500 | 17.296875 |
| 2 | 388349 | 54838 / 54838 | 2 / 2 | 3683 | 178.422 | 18.031250 |

两次八次真实失败重试不改变socket/PF；关闭全部后协议活字节
原生比较亦回基线，新socket可创建、旧句柄仍-9，随后实际ping及
零逻辑socket通过。本轮只创建未绑定UDP，尚未加入udp_pcbs链，
不证明已绑定长链/全部TCP并发性能；已只修夹具原因注释，实跑
原字节和旧冻结没有覆盖。PIT/宿主/CPU独立，不能称整机FPS达标。

原desktop02主核/签名未改。dhcp-lifecycle01/PID15596已开始
三种真实短租约策略，尚未收齐终态；驱动复位/链路/DMA/大TFTP
回绕、完整吞吐/拥塞/输入长尾和其它全部合同继续。当前无commit/
push/Release，不把16个声明或network12的308项当完整M10a1通过。

修订：2026-10-07，补双来源388125/388349 socket真实耗尽/八失败回滚/精确PF及普通隐藏收发权限终态，保持完整网络与整机待验。

### 2026-10-07短租约实际失败与适配修复待验

原desktop02主核的dhcp-lifecycle01退出1。探针真实绑定60秒租约/
T1=20/T2=40后等待3500 PIT tick，peak_lease_seconds=0、exit1；
独立PCAP/LeasePeer只有两次DISCOVER/SELECT，没有自动RENEW，
对端没有错误。失败快照仍BOUND10/地址10.23.0.2/租约60、使用0，
驱动/收包坏帧/坏释放未报错。原FAIL、输入、完整冻结和现场保持。
不是放宽480秒宿主期限或改静态配置后把此用例标PASS。

适配已改DHCP_COARSE_TIMER_SECS=1，单IPv4接口每秒一回调，
DHCP_TIMEOUT_SIZE_T为32位，并使用上游支持的自定义换算宏，
保留全部32位有限秒数，0秒转最小1秒、原60秒重试阈值/退避保留。
NETINFO仍原96字布局，其租约使用字段保持秒含义。私有dhcp.c
原样包含上游翻译单元，只更名粗定时函数并在同一网络上下文清除
有限→无限租约时被bind跳过赋值遗留的旧t0/T1/T2。上游固定BSD
原文件不改，完整声明及实际适配源码继续进SYS/LICENSE/NETSRC。

这些修复尚未构建或运行，不能用旧308项/资源PF结果充新主核
验收。新独立dhcp-fix01构建/双盘迁移/原三策略重测正在准备；
长租约和无限租约的实测、其它完整网络与整机验收继续。主核改变
不改变用户已签扩展，不需要读取私钥或重新签原模块。

修订：2026-10-07，登记60秒短租约自动续期真实FAIL与1秒/32位/无限过渡适配待验，保留原超时和完整目标。


2026-10-07 DHCP候选实测接力：build01在单接口NETIF_FOREACH展开成if时continue编译失败，原日志保留；改为有条件嵌套后build02退出0，643份输入及实际产物冻结。CORE完整SHA64da4e2e9615b771a4187b0d89f79f13821e104ca61cf527f63f46a9df421b59，正文381584B/BSS332840B。第一迁移发现陈旧THIRDPARTY.MD，运行前拒绝，不作为已验盘；补M9归档及声明后publish02双盘退出0。inputs02各434文件逐字节一致、225/167固定源码完整、公钥嵌入与WALL100公开验签通过，旧恢复主核保持。生命周期runner02/PID8576已启动原续期/重绑定/到期三策略，未收齐终态；旧主核PASS不能充本轮验收，长/无限租约及完整Goal继续。

修订：2026-10-07，保存DHCP首次构建失败和新构建/归档通过，登记双盘原三策略实际重测中。


2026-10-07短/长租约实际终态：dhcp-lifecycle02双来源各11项、共22项声明通过，原自动续期/重绑定/到期撤址与服务恢复都用实际独立线缆确认，source_unchanged，runner退出0；matrix SHAa4ccd5bcb7c4db59590d659422ec811bc137a57aebe070b1eb5bd1bb31e068ee。renew原生等候1891/1909 PIT tick、rebind3971/3980、expire5951/5900，peak为20/19、39/39、59/59秒；宿主命令分别38.125/39.375、73.797/73.718、116.938/116.844秒，QEMU CPU7.547/7.453、2.125/2.422、3.375/3.266秒。计时分别记录，不称整机性能达标。真实1920×1080截图保留原桌面与隐藏管理窗口。

long01夹具调用不存在的sc_sleep，原生链接失败，源/日志保留；仅改既有sc_event_wait(0,ticks)后long02原生编译通过。真实服务器返回0xFFFFFFFE租约并省略T1/T2，冻结候选对应32位布局只读HMP发现t0=4294967294、t1_timeout=0、t2=536870910，正确默认T2应3758096382。失败原因是上游先32位乘7再除8，且错误T2触发T1归零；不改观察值或时钟来修用例。现从固定BSD原件生成一处t-ceil(t/8)等式派生，原件/声明原样保留，实际完整派生及生成工具进SYS/NETSRC/PORT；新主核待构建、重测，22项旧候选PASS不覆盖它。无限租约/旧计数过渡与完整网络/整机目标继续。

修订：2026-10-07，登记短租约双来源22项通过、长租约默认T2真实溢出及私有派生修复待验。


2026-10-07默认T2修复候选已构建：build03退出0，648份输入逐个与冻结匹配并捕获实际ELF/bin/sym/MAIN/派生；MAIN正文381584B/BSS332840B，完整SHA35084bcd37178b5c68377574caa4ba6b73629dd71e7a6bd36d1cfb7d3f23ff7b。派生76940B，SHA6b337eb1451877e06a6f288237b7d339f7cb347a9c21fcbc49f92187acb94007，去掉新增适配注释、反转唯一表达式后与固定原件全部字节一致。原件/完整版权/BSD条文不改。publish03双来源256MiB、1526/1488条目，各437份当前文件精确核对；固定归档225/167、完整派生和生成工具已实际在盘，公钥嵌入/WALL100公开验签通过，恢复主核原字节不变。

long03/PID18032正在四模式双盘实际重测。第一来源最大32位有限默认、86400秒显式、显式无限租约及有限→无限旧timer清零有PASS子项，65秒保持与第二来源尚未终态；不能改写原long02溢出FAIL。short03/core10/font06/network13匹配当前候选的脚本只准备未启动，旧主核矩阵不当作当前全量验收。完整Goal继续，无commit/push/Release。

修订：2026-10-07，登记默认T2修复独立构建与437文件双盘归档通过、长/无限边界实际重测中。


long03第一来源已收齐全部四模式18项DECLARED_CASES_PASS，有限→无限后的6500tick越过旧到期时段保持及真实wire/no额外renew/rebind/release、ping通过，source_unchanged；第二来源正在执行，完整矩阵尚未终态。此补充覆盖前段当时65秒仍等待的进度，未宣布全轮M10a1通过。

修订：2026-10-07，补长/无限租约第一来源18项实际通过，双盘终态仍待收齐。


long03双来源终态：runner退出0，四模式全部36声明检查通过；最大有限默认实际t0=4294967294、t1=2147483647、t2=3758096382，实际无限过渡t0/T1/T2/后续重试/lease_used全零，自动续期之后6500/6501真实PIT tick仍BOUND与原地址，独立线缆没有错误后续renew/rebind/release，实际ping通过。两来源无对端错误、source_unchanged，matrix SHA3ec6f6ac1958cd1ba59dae2eb8866f9d7d4fb4474c3e14a7a21b225490fbf3ba，独立declared-audit.json。过渡完整命令宿主145.422/146.984秒、QEMU CPU3.141/3.313秒，包含取得/续期/保持，不冒充吞吐或整机FPS。实际第二来源1920×1080桌面截图已查看，保留原壁纸/图标和隐藏管理窗口。

当前short03/PID15812在同MAIN/inputs03重跑原三策略；core10/font06/network13匹配新产物且只准备未启动，670份后续验收输入/公开签名源码冻结保留。长租约36項不等于完整网络或M10a1通过，不重写旧long02溢出FAIL，不动用户私钥/历史盘，完整Goal继续。

修订：2026-10-07，补双来源长/无限租约36项实测终态、PIT/宿主CPU分列及新候选短租约重测中。
