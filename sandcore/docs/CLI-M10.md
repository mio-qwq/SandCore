# M10a1 独立网络工具

> **2026-10-07最新范围：** 用户追加curl；独立工具总数19，原18仍对应55项参考表的18/55，curl单列追加不计入原分母。原18两盘原生编译/声明行为及六profile308项已通过，按最新验证收敛规则复用；curl宿主构建、客体S3C编译及14项定向检查已通过（build/m10a1-curl-final-01/curl.json）；HTTP请求/上传/跳转/保存与错误退出均有实测。HTTP仍是首批范围，HTTPS/TLS明确未实现。下方早期批次保留历史。

> **2026-10-07 network-09终态：** runner退出1。首来源isolated56项通过，新增地址复用/旧选项21项检查全部PASS；客体编译前15工具通过，nc源码第22行unknown identifier失败。只读输入确认SYS/INC/SCNET.H已更新，NETCLI相对包含的SYS/SRC/SCNET.H仍旧，故本次应用未继续，不能算全部网络通过。现补SRC副本并给网络SCX/源码增加双目录三份头的资源依赖，不改候选主核。publish-03新双盘退出0；network-10待当前独立Shell stdin回归结束后运行。shell-run-01 PID3964执行中，正常EOF/引号/循环/heredoc/read等已有分项，完整终态未收齐。全部原失败和原盘保留，Goal active。

> **同日network-08终态：** 退出1。修复TCP销毁通知时机后，第一来源三profile全部声明PASS：isolated54、applications93（18原生工具、nc-e短程序/5次连续关闭、UDP Shell、HTTP及TFTP精确字节/坏响应/重传）、no-nic4。第二来源isolated54及18原生编译/启动、nc-e cat/sh/权限程序字节与原Shell退出通过；下一次监听失败。离线只读原错误文件为`nc: -98`，现场PIT继续、坏释放计数全0、ATA失败超时0。不是TCP重复释放重现，完整矩阵仍FAIL。新增地址重用选项并为nc TCP监听启用，保留TIME_WAIT；net-reuse-fix-01构建通过，下一轮待验。

> **2026-10-07 network-05终态：** 已退出1。18工具再次客体编译/帮助启动通过，nc-e sh准确输出7B `M10-SH\n`并把退出7交还原交互Shell，普通root网络子程序权限检查通过。随后等待权限探针原作业的管理INPUT/PING ACK超时；HTTP/TFTP/UDP-e及第二来源仍未收齐，完整网络CLI不标PASS。已补只读失败CPU/队列/ATA记录，保持协议正常时限。

> **2026-10-07最新：** network-04首盘隔离链路声明用例通过；18工具再次实际S3C编译/帮助启动通过，TCP/UDP字节回显及nc-e cat的16402B/原Shell等待退出通过。nc-e sh额外输出58B欢迎语/提示符/回显，整轮仍失败，不能把前述子集当完整网络CLI通过。已修Shell非终端stdin路径、定向构建并生成独立双盘候选，network-05完整重测中。HTTP/TFTP、其它nc-e程序/退出、第二来源与完整权限/故障仍待收齐，以下旧批次状态保留沿革。

> 当前：18/18独立main源码、WSL链接/SCX打包18/18；对应18/55原网络参考项。network-03首盘18工具实际S3C编译及启动均通过，隔离联网子集与nc -e二进制回显已有证据；完整双盘行为/权限/故障尚未收齐，不将help启动计为完整命令验收。

原[M9完整310行矩阵](M9-CLI-MATRIX.tsv)及其55项网络参考保留不改。本轮另建[M10网络55行表](M10-CLI-MATRIX.tsv)，首批18登记宿主构建和首盘原生编译；逐行为证据分基础子集与只有help启动，完整行为仍待验，其余37未纳入M10a1。18/55为32.7%的命令名源码/构建口径，不能称BusyBox完整选项兼容。ip子命令、别名及同功能参数不重复计数。

每个`user/net/*.c`有独立main、链接产物与`/BIN/名称.SCX`。共用`NETCLI.inc`和SandCore专用`SCNET.H/SCIO.H/SCAPI.H`，未复制BusyBox代码。源码归`/SYS/SRC/net/`，头文件走既有`/SYS/SRC`与`/SYS/INC`，手册归`/SYS/MAN/NETCLI.MD`和`NETWORK.MD`。SCCC源级开发闭环必须在统一阶段实际编译，不能用宿主编译结果替代。

所有工具`--help`/首参数`-h`返回0，参数错误2，操作失败通常1，成功0；ping/arping/traceroute按结果返回0或1，nc -e正常返回程序退出码0..255。输出/错误走Shell字节流，可管道/重定向；没有额外GUI。接口唯一名`en0`。时间参数整数秒，PIT为100Hz，显示RTT分辨率10ms；不是微秒测量。

| 独立工具 | 首批公开行为与选项 | 主要验收点 |
|---|---|---|
| ip | link/addr/route/neigh show；link set en0 up/down；addr add IP/PREFIX dev en0；route add/del CIDR/default via IP metric N dev en0；neigh add/replace/del/flush | 对应真实配置，不按子命令计四项 |
| ifconfig | 无参/en0查询；en0 up/down；en0 IP netmask MASK gw IP mtu N | 地址/掩码/MTU/链路/统计，非法值与权限 |
| ifup | [en0] | 启用实际接口/复位，不等待虚构租约 |
| ifdown | [en0] | 停止收发及DHCP，后台对象错误明确 |
| route | -n/show；add/del default/CIDR/-host IP，gw/metric/dev | connected/接口默认/显式静态路由，最长前缀 |
| arp | -n/-a；-s IP MAC；-d IP；-f清表 | 已解析缓存；-d仅静态项，未冒充动态删除 |
| arping | -I en0、-c次数、-w秒、-D地址冲突探测、数字IPv4 | 本机真实MAC/IPv4或DAD零地址；无重复写/身份伪造 |
| ping | -c次数(默认4，0持续)、-W等待秒、-i间隔秒、-s正文0..65499B、HOST | echo/错误/超时/大分片，成功至少一回复 |
| traceroute | -m1..255、-w秒、-q1..255、HOST；ICMP echo模式 | 逐TTL/多探针/引用匹配/错误/终点，未实现UDP模式 |
| netstat | -a/-n/-l/-t/-u可组合 | 动态分页；普通同主体UID，管理者全部，隐藏他人操作句柄 |
| nslookup | -t秒 NAME | 配置DNS的A查询/缓存/失败；暂不支持指定临时SERVER/反查/AAAA |
| hostname | 查询、-s、设置NAME | 运行hostname，权限与长度；不承诺FQDN反查 |
| dnsdomainname | 查询、设置DOMAIN、空引用清空 | 运行DNS域；不代表自动查询search suffix |
| ipcalc | IP/PREFIX 或 IP MASK | /0、/31、/32、非连续掩码；纯计算无需设备 |
| udhcpc | -i en0、-t等待秒、-n获取/-r续约/-R释放 | 内核维护租约；无脚本钩子或独立守护程序 |
| nc | TCP/UDP客户端、-l -p端口单连接/单UDP对端、-s本地IPv4、-w空闲秒(0无限)、-q输入EOF后秒、-v、TCP -z、**-e PROGRAM** | 双向背压、半关闭、二进制、对端/程序退出、权限与拥有者回收 |
| wget | HTTP GET、最多5次重定向、-O文件/`-`、-T超时秒、--max-size字节(默认16MiB)、-q进度静默、-S头诊断 | Content-Length/chunked/EOF正文、坏/截断/超过预算保留旧文件；HTTPS明确未实现 |
| tftp | -g下载/-p上传、-r远端/-l本地或`-`、-t重传秒、-R重试数、--max-size字节(默认16MiB)、HOST [PORT] | octet/512B、TID锁定、重复/丢包/ACK/空终块/16位回绕/最后ACK重发、错误回滚 |

全部网络控制要求root或外部授权SYSTEM；普通用户可以TCP/UDP、受限echo/ARP探测和DNS。低于1024端口另检查权限。说明是当前代码的支持范围；实际故障返回值以[NETWORK](NETWORK.md)及后续证据为准，不把列表当PASS。

## nc -e 的程序和管道

用户明确要求`nc -e`，现有源码支持客户端和监听路径，如`nc -l -p 12345 -e sh`或`nc HOST PORT -e "cat -"`。程序及其参数作为一个Shell引用字段传入，PATH按调用者环境寻找。每次nc只服务一个连接/UDP对端，不自动后台重开监听。程序stdin接网络输入，stdout和stderr合流发送；两个方向分别保留4KiB，非阻塞推进，满管道时仍能处理反向数据。没有伪TTY；需TTY的全屏程序不是本次-e的行为承诺。

TCP对端发送EOF时关闭子程序stdin，继续发送其最后输出再半关闭。程序输出EOF、空闲/退出时限、网络错误及父任务退出均有回收路径；作业PID须核代数，不能杀复用对象。socket和两个管道任何创建/接线失败都撤销，子程序归nc父作业链。

子程序先关闭stdin时，破管仅停止网络输入方向，仍发送stdout/stderr的尾部；程序输出全部EOF并发完后允许结束，不强迫对端主动关闭才能收尾。新增验收源码包含-e cat二进制、-e sh退出7、短echo、公开ABI身份/非UART管道检查。这些是待执行用例，尚无-e行为PASS。

管道EOF和作业退出结果发布分别处理：须取得真实完成作业的退出码再结束，不能把STREAM先清理造成的EOF当成exit(0)。

网络程序采用新增NETEXEC身份/描述符合同：普通/root保持文件主体；外部SYSTEM明确-e时新孩子为root/AUTH_NORMAL，无UART/外部会话票据，不获得MOD/CORE最高管理。普通SYSTEM服务只有合法普通受委托主体时可启动，不伪造root。旧SPAWN2的权限继承不变。此边界来自已批准外部串口最高权限合同，属于实现约束，未另加用户审批流程。

## 文件传输

wget拒绝HTTPS、压缩Content-Encoding、未知Transfer-Encoding及冲突长度，发送Accept-Encoding identity。响应头总量≤16KiB、单行≤2047B；URL≤1023B、DNS名≤255B，首批ASCII/百分号路径。HTTP重定向只沿HTTP，非HTTP报明确失败。已知长度文件按真实长度预留；未知长度/chunked按公开max-size预留磁盘事务，读取只有4KiB缓冲，成功提交时归还多余预留。用户可把上限增至256MiB，真实空闲不足明确失败；不是把整个预算占进RAM。输出`-`遵守字节流背压，已输出字节不具备文件回滚语义。

TFTP按[RFC 1350](https://www.rfc-editor.org/info/rfc1350/)自写，未复制实现。仅octet，不协商blksize/tsize/netascii；默认UDP69可指定端口。重传期限到达才重发上一请求/DATA/ACK，重复ACK不触发DATA放大；重复DATA只重新ACK，不重复写。最后短DATA先成功提交本地文件再ACK，随后最多两个重传间隔dally。输入整512倍数补零长度终块，16位块号回绕实际兼容待验。不提供tftpd，不把客户端名算两项。下载未知总长使用max-size事务；超时/错误/满盘放弃，保留旧目标。

## 统计与未验证范围

当前18份源码已WSL编译、链接、SCX打包并安装两份256MiB迁移盘；network-03首盘18工具客体SCCC编译/启动通过，基础联网子集已有证据，完整行为未收齐。统一阶段继续逐工具测参数/普通与管理身份、真实受控对端、Shell二进制管道/重定向、断链/超时/OOM/退出回收，并在双来源盘实际原生编译。表中PARTIAL-DISK1-PASS表示已测基础子集，HELP-DISK1-PASS仅表示help；两者均不是完整命令验收。

修订：2026-10-06，18项独立网络源码和安装/开发规则收齐初版，保留55行/三种统计；用户追加nc -e，协议与权限同步，全部未构建未运行。
修订：2026-10-06，统一build-04通过，18项宿主SCX构建计数更新为18/18，客体行为仍0/18；两份只读来源迁移盘已生成。

修订：2026-10-06，按network-03实际记录首盘18客体编译/启动、基础
网络与nc -e二进制回显，55行表分help/行为子集；完整双盘仍待验。

修订：2026-10-07，记录network-09新增选项实际PASS子项与客体nc编译失败；补SRC/INC双处头文件的定向资源依赖。

## 追加curl

自写独立 /BIN/CURL.SCX，复用wget的HTTP.inc字节解码器，不复制上游curl实现。常用命令：

    curl http://HOST/path
    curl -I http://HOST/path
    curl -fsSL -o /TMP/body http://HOST/path
    curl -X POST -H "Content-Type: application/json" -d '{"hello":1}' http://HOST/path
    curl --data-binary @/TMP/data -o /TMP/reply http://HOST/path
    curl -T /TMP/data http://HOST/path

支持单个HTTP URL，GET/HEAD/默认POST/默认PUT及-X自定义方法，重复-H、-A、-i、-I、-o、-D、-L、-f、-s/-S、-v、-4，短布尔参数可合并。支持对应长名及--data/--data-binary/--data-raw/--upload-file/--url。一次命令只允许一种正文输入，不支持重复-d拼接；-d @文件剥除CR/LF/NUL，二进制模式保留全部字节，文件按4KiB流式上传，stdin按真实长度增长读入。默认响应/需读入stdin预算16MiB，可用--max-size调至256MiB。

-m/--max-time为网络请求总期限，--connect-timeout限定TCP连接；均为整数秒，0表示不限。默认分别30秒/15秒。默认不跟随重定向，-L默认最多5次，--max-redirs接受0..20；普通POST的301/302/303转GET，307/308重放原正文，离开原主机/端口不转交Authorization/Proxy-Authorization/Cookie。响应头每次16KiB，包含所有跳转头的缓存总量64KiB，仅需要-i/HEAD时增长分配。文件正文失败回滚，已输出的stdout字节不能撤回。工具维护Content-Length/Transfer-Encoding/Connection/Expect，不接受覆盖这些请求头。

HTTP错误默认仍输出正文；-f在状态>=400时不输出正文并返回22。超时28、连接失败7、DNS失败6、输出失败23、输入失败26、过多跳转47、超过正文预算63。参数错误2、坏URL3、HTTPS/其它协议不支持1。协议/接收其它错误56。没有TLS/HTTPS、HTTP2/3、FTP、multipart、Cookie库、代理、自动压缩、重复正文或多URL功能，不称完整上游curl兼容。选项语义参考[上游官方手册](https://curl.se/docs/manpage.html)，实际支持以此表和客体结果为准。

修订：2026-10-07，用户追加自写curl，原18证据复用；公开HTTP子集、总期限、上传/头/文件与退出码合同，客体定向验证待完成。

2026-10-07：curl追加HTTP子集完成系统内编译及14项有限验收，独立工具19/19；原18/55和TLS未实现口径保持。
