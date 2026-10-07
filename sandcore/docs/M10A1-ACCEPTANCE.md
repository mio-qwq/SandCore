# M10a1 开发验收

2026-10-07：约定功能实现及必要收尾检查完成，提交用户验收。
这是M10a的开发版本，不是GitHub Release，也不代表M10基础内核冻结。
按用户最新要求停止增加测试矩阵；以下区分本次curl检查、已有双来源证据和已知范围。

## 运行

本机直接双击项目根目录run-m10a1.bat；默认使用sandcore/build/m10a1中的最新启动盘和256MiB数据盘。
可独立解压build/SandCore-M10a1-acceptance.zip，双击包内run-m10a1.bat。
需要本机Python 3和Windows QEMU（C:\Program Files\qemu），包内不分发这些宿主工具。
默认有窗口、单vCPU、256MiB内存、e1000和QEMU user网络；先读取主核/字体并等首个桌面帧，串口才完成握手。

每次启动复制只读验收基盘到独立sessions目录。终端显示实际运行目录；密码、文件和设置修改都保存在该目录的sanddata.img。
继续上次会话时，向启动批处理传入 --data "旧会话目录\sanddata.img"。
关闭QEMU后再使用包内sanddata_editor.exe编辑该会话盘；项目原M9/M10开发盘不覆盖。

客体Shell首次联网可执行udhcpc -n -t 20，然后ip addr show、ping 10.0.2.2。
HTTP示例：curl -I http://example.com/ 或 curl -L -o /TMP/PAGE http://example.com/。
公网站点可能跳HTTPS；HTTPS会明确失败，实测网络对端是受控本机HTTP服务。
外部串口窗口为SYSTEM管理入口，与客体Shell不同，普通/root不获得外部硬件管理资格。

root默认没有已设置的可登录密码。外部串口输入passwd root；在New password与Again提示各输入:secret，
按宿主隐藏输入提示键入并确认自己的密码。看到Password updated后，客体按F12，在登录界面使用root和刚设置的密码。
root进入独立桌面；SYSTEM窗口默认隐藏在普通/root桌面之外。不要把未设置密码解释为空密码登录。

## 本轮功能

| 项目 | 实现与实际证据 |
|---|---|
| 取消固定多任务上限 | 任务、凭据、环境、调试、SIMD及关联作业/端点/管道/窗口/会话动态增长；131并发及真实内存耗尽后回滚/回收，旧分页ABI保留 |
| 调度与回收 | 就绪/等待队列、及时服务、退出/异常/PID代数隔离；同负载输入p95由约2.203秒降到15–32毫秒，属于该夹具而非整机FPS保证 |
| 独立会话桌面 | mio/root/SYSTEM窗口、焦点、输入、故障卡隔离；隐藏客户帧和合成为零，后台继续，切回重画；两轮65会话每来源及暖资源页账通过 |
| 磁盘主内核 | /SYS/CORE/CORE.SKM和最小loader；旧恢复核置/SYS/RECOVERY，真实坏主核回退已验证 |
| 签名扩展 | 用户Ed25519公钥/公开签名；编号顺序/冲突/篡改/ABI/重定位/初始化失败与常驻生命周期有证据，原壁纸独立编号100 |
| 原始TTF | 直接读取凤凰12px/16px TTF，整数/定点栅格、按需缓存；7540映射字符每脸全量像素对照，Notes中文显示/编辑/保存通过 |
| 256MiB盘 | 内核/打包器/Windows编辑器容量与目录接线；旧文件/UID/GID/权限/代数原样保留或归档，不覆盖原盘 |
| e1000与IPv4 | Ethernet/ARP/IP/ICMP/UDP/TCP、路由、分片、DHCP/DNS、socket及退出回收；Windows QEMU真实对端和短/长/无限租约证据 |
| 网络工具19个 | 原18：ip、ifconfig、ifup、ifdown、route、arp、arping、ping、traceroute、netstat、nslookup、hostname、dnsdomainname、ipcalc、udhcpc、nc（含-e）、wget、tftp；追加curl |
| 许可 | 固定源码在third_party，客体/SYS/LICENSE保留完整源码、版权/许可证/声明；只引入符合合同的宽松许可组件 |

## curl 收尾

user/net/curl.c为自写HTTP常用子集，与wget共用HTTP.inc，未移植libcurl或BusyBox，也不引入libc。
支持单URL的GET/HEAD/POST/PUT/自定义方法，-H/-A、-d/--data-binary/-T上传，-o/-D/-i、-L、-f、-s/-S/-v、超时和体积限制。
具体选项/退出码/预算见CLI-M10.md；不支持TLS、HTTP2/3、FTP、multipart、代理、自动解压及完整上游curl选项。

只启动一次新增curl验证VM。build/m10a1-curl-final-01/curl.json记录14项全部PASS：
联网、系统内S3C编译/help、原生与安装版GET、HEAD/完整响应头、POST二进制/请求头/输出文件精确字节、
PUT文件、表单POST、包含响应头的跳转、-f的404退出22、HTTPS明确退出1。
同VM启动最终镜像，HMP实际输入及1920×1080桌面截图留证；验证来源数据盘摘要未变。
无新增全组件回归或第二套curl矩阵。

## 已有证据复用

最新主核完整SHA256：981ee90ba4f399edbc348561e25ef45de2364f50091cad9dc0f088585a513f7b。
最新主核构建冻结的279份kernel/boot/third_party/assets输入与当前文件摘要全部一致。
此前网络/字体/任务验收候选到当前主核只有kernel/wm.c的故障卡清理变化；当前双来源会话/故障卡回归覆盖该变化。
curl改变三环HTTP工具与构建/源码接线，未改变内核、字库、驱动或签名扩展。
最终打包还同步只读网络手册；包内执行映像和资源与实测版本逐字节核对，文档更新不另开VM。

| 证据目录（build/） | 实测范围 |
|---|---|
| m10a1-network-13 | 双来源6个profile、308声明检查，原18工具逐项系统内编译/声明行为 |
| m10a1-dhcp-lifecycle-03、m10a1-dhcp-long-03 | 22项短租约及36项长/最大有限/无限租约与真实续期 |
| m10a1-core-11、m10a1-preserved-recovery-01 | 12个扩展/启动profile及原恢复核回退、用户签名与常驻服务 |
| m10a1-font-06、m10a1-font-edges-02、m10a1-notes-03 | 30160映射记录、坏/缺字体和旧文字ABI、中文编辑保存 |
| m10a1-taskstate-03、m10a1-lifecycle-01 | 环境/异常/调试/SIMD/代数、动态关联资源与实际资源耗尽 |
| m10a1-sessions-12、m10a1-fault-desktop-04、m10a1-fault-multicard-01 | 隐藏停画/回收/输入、跨用户隐藏故障卡及多卡关闭 |
| m10a1-network-resources-01、m10a1-tcp-faults-01、m10a1-shell-01 | socket拥有者/真实耗尽/退出、TCP重传乱序/关闭、Shell字节流 |
| m10a1-scheduling-before-03、m10a1-scheduling-after-03 | 相同131任务输入/画面前后对照 |
| ../sanddata_editor/build/m10a1-final-check | 256MiB原生读改存/回读/CF_HDROP，1532原对象不变；0.987秒/峰值518.6MiB |

验收包选入这些批次的JSON、日志、说明和代表截图；旧报告中的绝对路径是运行当时路径。
用户已授权清理重复测试盘和无用截图，不能把旧报告提及的每个原始文件说成仍然存在。
详细沿革与失败修复保留M10A1-VERIFICATION.md；本次未重新运行M9所有旧程序或新建整机吞吐/FPS矩阵。

## 验收边界

网络支持Windows QEMU e1000/IPv4；浏览器、IPv6、SMP、GPU和输入法不在本轮。
HTTP可用，HTTPS/TLS未实现。19是独立工具数量；原参考覆盖仍18/55，余37明确未实现，不声称完整BusyBox或curl选项兼容。
字库覆盖原TTF全部可映射字符，不等于全部Unicode；原字体没有U+00B7/U+2014，保留回退。
动态任务以真实内存等资源为界，耗尽明确失败，并非无限内存。
M8游戏/影片仍封存，M9历史成果保留。用户私钥未进入本包/仓库；只有公钥和公开签名。

## 用户验收步骤

1. 双击入口，确认桌面正常；Shell运行ip addr show、udhcpc -n -t 20和curl --help。
2. 用Notes打开/SYS/SRC/net/curl.c或已有中文源码，确认中文注释与滚动。
3. 设置root密码后按F12登录，确认进入独立桌面，mio窗口不出现在root桌面。
4. 使用HTTP对端试curl -I/-L/-o，或查看包内14项实测JSON；nc -e已包含原18验收证据。
5. 退出QEMU，用新版编辑器打开会话256MiB盘；修改后下次以--data继续该盘。

上述是交给用户检查的步骤，不是代理继续扩展测试的待办。

## 修订

2026-10-07：追加curl完成有限客体检查，同步最终入口、编辑器与证据复用，提交开发验收包等待用户验收。
