# M10a1实现收齐与统一验证入口

> **2026-10-07 用户验收通过：** 用户明确“验收通过，提交GitHub，用wsl的gh”。M10a1保持M10a开发版本，19个网络工具（含curl/nc -e）的实际支持与HTTP/TLS边界不变；停止新增验证。验收结果同批持久化后推送代码，不创建Release，不宣称M10已完成或基础内核冻结。

> **2026-10-07 开发验收包已提交：** M10a1约定实现与收尾完成，等待用户验收。动态任务/独立会话/隐藏停画、磁盘主核与用户签名扩展、原始凤凰TTF、256MiB盘与编辑器、e1000/IPv4和19个网络工具已接线。新增curl系统内编译及14项定向检查通过，包含最终镜像启动/联网/桌面截图；已有有效双来源证据按实际源码影响复用，停止新增测试。原18对应18/55，curl单列追加；HTTP可用，HTTPS/TLS尚未实现。入口见[M10a1验收说明](M10A1-ACCEPTANCE.md)，不创建Release。

以下旧阶段状态与失败记录是历史沿革，不能作为当前待办清单；最新结果以验收说明为准。

> **2026-10-07执行收敛：** 下表的实现入口已收齐，已有任务/会话/签名/字体/网络等实际结果按VERIFICATION记录复用。遵循AGENTS第十二节：不追求穷举理论边界，不因无关小改动重跑所有组件，不继续增加同类夹具；只处理具体故障和必要交付检查。历史“待验”描述须与最新结果对照，不能自动转为新的无限测试队列。

> **2026-10-07当前：** M10a1完整Goal继续，尚未整体验收。用户公钥/六份签名有效，迟交未导致返工或重签。新MAIN981ee90b…修隐藏故障桌面清理后的模态残留：同字节双盘HMP夹具22项通过；session12双盘各两轮65会话全部通过，冷PID历史页5→11/其余回收，暖PF与记录/旁表页无增长，12张完整工作区逐字节保留。preserved-recovery01两盘真实坏主核回退到原fa9f0d39…核，原ELF/载荷重包装一致、同用户签名初始化与常驻服务通过；非用新核替代恢复核。原盘/全部历史/权限/公开签名保留；新候选其它完整键鼠、多卡/多普通会话异常、真实GUI、设备/存储/字体边界等合同继续待验。当前无运行VM，无私钥访问/commit/push/Release。详细证据和范围见M10A1-VERIFICATION.md；以下旧接力记录保留为历史。

> **2026-10-07最新接力：** dhcp-fix02独立build03退出0，648份构建输入及实际产物冻结；MAIN正文381584B/BSS332840B、完整SHA35084bcd37178b5c68377574caa4ba6b73629dd71e7a6bd36d1cfb7d3f23ff7b，默认T2派生只替换一处表达式，上游原件/全部声明原字节保持。publish03两份256MiB盘退出0，各437份当前文件精确核对，225/167固定归档及实际派生/生成工具齐全，原恢复CORE保持。唯一runner long03/PID18032运行四模式×双来源；第一来源四模式全部18项通过，最大有限默认T1/T2、86400秒、显式无限及有限→无限自动续期/计数清零/6500tick越过旧到期时段保持/实际ping全部通过，源盘摘要保持；第二来源与完整双盘矩阵未收齐。旧short02双来源22项通过保留，不覆盖新MAIN；short03/core10/font06/network13脚本已匹配新CORE/inputs03准备，未启动。公钥/六份签名再次独立验证，无私钥访问或重新签署；完整Goal active，无commit/push/Release。

> **2026-10-07租约边界当前：** dhcp-lifecycle02退出0，两来源全部三策略共22声明检查通过，独立线缆/PIT/CPU/源盘不变审计已保存，实际1920×1080桌面已查看。long01夹具误用不存在的sc_sleep而原生链接失败，已用旧公开timed event wait修正；long02原生编译通过，在真实0xFFFFFFFE租约、省略T1/T2时读到t2=536870910而正确值3758096382，t1_timeout错误归零，完整FAIL已保存。现补固定BSD原件生成的一处无溢出默认T2派生与完整实际源码归档，dhcp-fix02正在准备独立构建，尚未验证。公钥和六份签名再次独立验证通过，无私钥访问；完整Goal active，无commit/push/Release。

> **2026-10-07 Shell真实回归通过：** shell-run-01退出0，两只读来源盘各31项、共62項stdin回归通过，源盘摘要保持。文件/管道EOF、无末尾LF、连接未结束时exit、跨行引号/循环/heredoc、CRLF/Tab、read及子进程保留后续输入、坏控制/NUL/未结束语法拒绝、旧脚本/-c和显式-i都有精确输出/退出码/错误/日志。此范围不冒充全部Shell或整机验收。publish-03双来源各429文件归档/当前SRC头逐字节核对通过，network-10 PID8632正在完整三profile/双来源/18原生工具重测，未收齐终态。新65会话冷暖资源页账探针已补，尚待执行；Goal active。

> **2026-10-07 network-09终态：** runner退出1。首来源isolated56项通过，新增地址复用/旧选项21项检查全部PASS；客体编译前15工具通过，nc源码第22行unknown identifier失败。只读输入确认SYS/INC/SCNET.H已更新，NETCLI相对包含的SYS/SRC/SCNET.H仍旧，故本次应用未继续，不能算全部网络通过。现补SRC副本并给网络SCX/源码增加双目录三份头的资源依赖，不改候选主核。publish-03新双盘退出0；network-10待当前独立Shell stdin回归结束后运行。shell-run-01 PID3964执行中，正常EOF/引号/循环/heredoc/read等已有分项，完整终态未收齐。全部原失败和原盘保留，Goal active。

> **2026-10-07最新终态：** sessions-06在net-reuse-fix-01新主核/inputs-02两个只读来源完成声明矩阵，runner退出0。两来源各11阶段/22项真实NUI检查通过，65个同UID独立登录会话和真实窗口全部就绪，分页查询含全部65个普通隐藏桌面，全部注销后的原会话ID被拒绝。隐藏窗口无新增客户帧、像素保持且后台推进；回到会话/恢复最小化窗口实际重画。每个输入源摘要不变；65回收后PF页数各少6页，缓存与完整资源总账待验，不能称全部页回收达标。普通密码/F12锁屏、鼠标/拖动/相对输入/故障卡、全部内置应用后台与隐藏生成、其它旁表和历史兼容仍待验。network-09 PID5420正在同一候选上完整三profile/双来源/18原生工具重测，并新增真实bind/listen地址复用与旧选项边界探针。完整Goal active；旧core10/font04证据不当作新主核验收。

> **2026-10-07当前验证状态：** 用户公钥和六份签名已经收到并验证，晚提供仅延后正向验签，不影响其它实现。冻结core10的core-08两来源12项声明profile、font-04全部30160映射记录通过；这些旧证据不作为新主核整体验收。network-08首来源三profile通过、第二来源隔离链路通过；第二来源应用在已完成47项后因nc连续监听返回-98而失败。现场PIT推进、UART/ATA无错误、坏释放记录全零，未复现此前tcp_free重复释放。当前已补socket选项5 REUSEADDR及nc TCP监听接线，保留TIME_WAIT；net-reuse-fix-01构建退出0，主核正文379536B/BSS327432B，正文SHA a33bf9f9cb18f7b82136cc87195ddb86541b195471ca8200bde7fd3601c4c4a5。完整第三方167份固定归档已核对，补最新声明后独立双盘publish-02退出0，两个盘各428份归档/候选文件逐字节核对通过。sessions-05真实NUI的11阶段/22项隔离、输入和隐藏停画检查通过，65任务创建但夹具窗口尺寸不合法而未就绪；已改128×96并要求真实65个ID，session-06 PID1276已在新net-reuse-inputs-02双盘单独重测，未收齐终态。完整Goal active，未commit/push/Release；当前新主核尚未获得整体验收，下方旧运行状态保留沿革。

> 2026-10-06本次实现收齐核对。只证明实现/资源/规则/规范具备入口，
> 不证明能构建、客体行为正确或性能达标；完整目标仍active。

依AGENTS第十一节第9条，开工批准已涵盖后续统一构建与验证。
此表覆盖M10A1-DESIGN全部产品范围；不把缺少用户签名、尚未运行
的用例或后续修复改成完成，也不把它们解释为又一轮阶段审批。

| 产品合同 | 实现与资源 | 正式接线/规范 | 进入统一阶段后必须证明 |
|---|---|---|---|
| 动态任务及全部八类旁表 | objpool.c、task_store.c、task.c及AUTH/LOGIN/USERSPACE/PROCESS/STREAMS/SIMD/IMAGE/FAULT | task_init读取各实际对象尺寸；MEM/CPU/SYSCALL | >32并发、真实耗尽/回滚、退出/异常/PID身份复用与PF账 |
| 动态关联资源 | streams.c、fs.c、session.c、wm.c、module2.c | 新分页/父子和拥有者链；M10-LIFECYCLE/SESSION/WM/FS/CORE | >64作业、管道/窗口/截图/事务超旧门槛、异常及跨拥有者回收 |
| 就绪/等待/及时服务 | task.c就绪链/延后epoch/定时堆、main.c服务和清理批次 | SCAPI 0x240/241/247；CPU/PERFORMANCE | 公平性、真实应用、输入长尾、相同模式基线对照 |
| 旧API与工具分页 | 旧8/32缓冲保留；ps/top/pidof/killall/Monitor使用新页 | SCAPI.H/SYSCALL/CPU/MONITOR | 历史SCX原字节、护栏/忽略寄存器、完整滚动/代数差分 |
| 独立登录桌面 | session.c、auth.c、wm.c、desktop.c、theme.c、userspace.c | 登录/LOGIN.SCX/sessionctl/会话面板；SESSION/THEME/WM | 同UID多会话、mio/root/SYSTEM显示/输入/注销/配置/截图与复用 |
| 内核决定隐藏可见性 | 0x244、历史绘制陷入保留、NUI/UI及各内置GUI/编译回调 | 非阻塞回调与后台音频/CLI；SESSION/PERFORMANCE | 实际零隐藏栅格/帧提交、后台进度、切回重画及历史增量 |
| 磁盘主核/只读loader | loader.c/entry/ld、core_entry/core_boot/core.ld、mkcore.py | m10.mk明确ELF32 MAIN与128扇区软盘；CORE/BOOT | 冷启动/格式/边界/坏核心/双坏/Shift恢复及实际主核来源 |
| 验签/有序常驻扩展 | module2.c、core_signature.c、固定Monocypher、mkext/core_signature工具 | 92B服务表保留旧前缀，WALL编号100待签；CORE/MODULE | 用户签名正向、编号排序/冲突/篡改/ABI/重定位/失败及常驻 |
| 用户独占私钥 | 构建只收32B公钥，工具只收公开签名；无键全部拒绝 | mkcore_public_key.py/M10_PUBLIC_KEY；CORE | 代理不接触私钥；正向输入待用户亲自签署，不伪造PASS |
| 完整原TTF | 原ZIP/12px/16px原字节、ttf.c/GLYPHS.inc、内核和Notes/NUI文字 | FONTINFO2/GLYPHBITMAP、font_coverage.py；FONT/NOTES | 完整映射/像素参考/中文编辑/字宽与旧接口、坏文件安全和缓存账 |
| 256MiB及旧盘保留 | ATA/FS动态容量/分配树/统计失效、mkfs_m10.py | 新副本/原件归档/大bank；FS/STORAGE/MEM | 实际256MiB、所有旧内容/权限/代数、满盘/事务/IO失败/旧容量 |
| 宿主镜像编辑器 | 动态条目/共享不可变正文/分块IO与插入OOM回滚 | build/m10a1独立EXE；编辑器README | 读改存回读、峰值内存/耗时/Windows原生交互，原EXE保留 |
| e1000/IPv4 | 自写e1000.c、固定lwIP2.2.1、私有freestanding port/network.c | main/IRQ/socket0x250..263；NETWORK/QEMU | DMA/链路/复位/分片、TCP真实故障/重传/乱序/拥塞/关闭/DHCP/DNS |
| socket权限/生命周期 | net_socket.c动态票据与拥有者代数、事件/就绪/退出清理 | SCNET.H/SCAPI.H、NETEXEC不继承UART授权；NETWORK | 用户指针/长度/无设备/普通身份/外来句柄/退出/真实OOM与PF |
| 18独立网络工具 | user/net的18个独立main及NETCLI.inc，含nc -e | 独立SCX/源码/手册；CLI-M10与55行M10-CLI-MATRIX | 18逐项客体行为/原生编译/管道/重定向，源码/构建/行为分别计数 |
| 许可与历史保留 | 固定源码/原文/摘要清单、原ZIP/旧SCX/SKM/发布盘 | SYS/LICENSE/NETSRC、audit_third_party.py；THIRD-PARTY | 构建归档原字节/逐文件许可、迁移原件、历史软件及无设备/VGA |

首次统一构建：WSL ELF `make -j4 m10a1`，输出仅
`build/m10a1-work`。主核和loader分开链接；迁移源只读已有M9
开发盘，缺少时只读精简发布ZIP。主核进入SYS/CORE/CORE.SKM；
原壁纸归历史/WALL待签，恢复目录独立。M10a1构建链的m9-artifacts
前缀用于复用成熟工具与固定资源，其BUILD已改为M10独立路径，
不写M9/M8历史产物，不重启影片/游戏资源开发。

统一验证不只运行已有两个脚本：M10LIFE/M10RES和双盘生命周期、
M10NET/网络对端只是部分已写入口；启动/签名/会话/完整字体/磁盘/
编辑器/历史软件/真实应用性能矩阵还要在同轮补齐执行并按结果修复。
验证脚本可随实际产物继续完善，不能把缺用例或旧PASS当覆盖；
任何新代码编译成功也不代表完整M10a1完成。

当前即将开始首次构建，无新编译/运行成功证据。用户公钥与签名
未到达，正向签名链待验；继续构建、其它运行和可审阅待签包。

## 修订

2026-10-06：逐合同核对实现/资源/规则/规范入口，编辑器补独立
输出保护旧EXE，转统一构建/实际验收；本表不声明最终完成。

修订：2026-10-07，记录network-08真实终态、TCP地址复用构建和65会话夹具修正；区分旧主核通过与新主核待验。

修订：2026-10-07，记录sessions-06双来源全部声明用例通过和65真实桌面范围；保留6页差额及完整其它合同待验。

修订：2026-10-07，记录network-09新增选项实际PASS子项与客体nc编译失败；补SRC/INC双处头文件的定向资源依赖。

修订：2026-10-07，登记Shell stdin双来源62项真实PASS及network-10重测，资源总账和其它完整合同继续。

2026-10-07待确认静态问题：wm.c的fault_remove撤销隐藏会话最后一张故障卡后，没有直接清其所属view->fault_modal；wm_close_owner_step只据当前active_view清fault_focus。源码路径可能使隐藏SYSTEM/其它桌面卡片清理后，切回仍有模态输入阻挡。此为静态疑点，尚无实际复现，不称已确认缺陷；下一组真实隐藏故障→kill/reap→切回/焦点/HMP输入需确认后修复和重测，不用taskstate03的PF通过代替交互验收。

2026-10-07：curl与必要收尾验证完成，19工具开发验收包提交；停止新增测试，等待用户验收。

2026-10-07：用户明确验收通过，授权通过WSL gh认证将M10a1提交GitHub；保留实际证据与开发版本口径。
