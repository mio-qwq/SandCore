# SC32TWIN 双窗口公开API验收应用

所属版本：**SandCore M10a2 — The Application Platform Era**。源为 `tests/m10a2/SC32TWIN.C`，动态应用清单为 `SC32TWIN.json`。应用只包含正式 `SC32API.H`，31项导入都由真实调用引用；没有SCAPI、私有gate、inline asm、装载器源码或SC32库实现副本。

当前为**主代理HOST与客体SCCC编译、Windows实际输入运行通过**。实际原生应用25927B，全部coverage位0..26为0x07ffffff，正常退出0、本人heap10→10、暖PF6878→6878、GUI owner及窗口池归零；初次Shell背景表+1另行实测，完整证据见 [m10a2-sc32](../evidence/m10a2-sc32/README.md)。它只验证P0协调与native DLL的实际调用，不代表SCUI控件树、四库或完整M10a2已完成。系统源、公共头和Foundation测例均不因这个应用改写。

## 1. 身份、启动与画面

启动后创建两个独立根窗A/B，再创建以A为owner的模态C。窗口过程与context在Ring3，CLOSE和DESTROY走正式API。B/C的CLOSE调用默认过程，仅销指定窗口；A第一次CLOSE显式RejectClose，标题变为 `Twin A | Close rejected`，A仍存活。

若从SYSTEM管理链路启动，先明确显示它自己的会话桌面，报告 **SYSTEM Ring3**。不能把隐藏的SYSTEM窗口或显式SYSTEM会话当成普通身份/普通桌面证据。首份日志里的 `pid/pid_generation` 与三份WindowInfo的owner、session可核实际归属。普通桌面LNK启动同一个SCX时仍使用其真实登录身份，系统库位于受保护的SYS/LIB，不借SCL获得Ring0权限。

画面使用24角色主题快照，全部RGB来自FACE_ALT/PAPER/LINE/ACCENT/VIOLET/GOLD/GREEN/MUTED；ARGB常量只表示不透明alpha/通道掩码，没有新增裸RGB。标题由原生WM显示，不引入自写字体。客户区有轻渐变顶栏、焦点/活动轨、两个留白面板和底部三按钮/修饰状态；A左面板是捕获区域。状态色：主题accent正常、gold捕获、green有效状态，C是简洁金色模态面板。

客户surface按实际尺寸申请私有页，第一次PAINT或geometry改变才分配，候选失败保留旧指针，所有候选均有context归属。默认两窗各360×220×4B，C至多240×120×4B，按4KiB页取整合计185页/740KiB（不含内核画布/库metadata）；没有每app预留8MiB。允许真实resize至设备客户尺寸，不暗降分辨率。隐藏不调用绘图；模型仍可随另一窗的真实KEY/TEXT推进。

PAINT使用Begin的真实damage/geometry/paint token。已有surface只计算损伤矩形；新surface完整建立画面。一次Submit一个合法矩形，实际成功才End；隐藏AGAIN或异步geometry STALE保留重画需要，不假确认。纯鼠标运动只在面板标记实际位置改变时失效旧/新标记并集，夹在面板同一点的越界移动不持续整帧重画。计数是成功接受的矩形字节，不声称它们都是改变了颜色的像素。

## 2. 初始几何与坐标取得

应用从Display/Theme取得当前屏幕和标题/任务栏尺寸，计算期望值。下面 `/` 是非负整数除法：

```text
workH = display.height - theme.taskbar_height
W = clamp((display.width - 52) / 2 - 2, 94, 360)
H = clamp(workH - theme.title_height - 81, 80, 220)
X = max(0, (display.width - (2 * (W + 2) + 20)) / 2)
Y = max(0, (workH - (H + theme.title_height + 1)) / 2)
A desired = (X, Y), client W×H
B desired = (X + W + 22, Y), client W×H
CW = clamp(W - 32, 94, 240)
CH = clamp(H - 56, 74, 120)
C desired = (X + (W - CW) / 2, Y + theme.title_height + 20), client CW×CH
```

真实WM裁入工作区的结果以首次status0日志里的WindowInfo为准。客户原点由规范尺寸直接算，不猜固定标题高度：

```text
clientX = info.x + 1
clientY = info.y + info.height - info.client_height - 1
```

A捕获区为 `12 <= client_x < client_width/2-6`、`42 <= client_y < client_height-38`；默认用客户点 `(24,64)`。必须先关闭C解除A的模态阻挡。Close按钮位于标题最右侧，主代理按当前原生主题/Info取得实际按钮坐标。F6会改变B宽度，后续屏幕坐标应重新取F9快照，不能一直沿用初始位置。

有STD1时READY成功日志之后真实FileWrite/Flush输出 `SC32_TWIN_READY\n`。没有STD的桌面LNK入口仍以 `/HOME/M10P/GUI.LOG` 的初始status0为就绪；READY不是PAINT全部完成或完整GUI PASS。F6还要求B已有实际surface，harness可从首批PAINT消息及画面确认。

## 3. 操作与终态

真实键盘KEYDOWN/KEYUP/TEXT、按钮每条沿、FOCUS、MOVE/SIZE、CAPTURELOST、VISIBILITY和PAINT均按收到的64B消息记录。功能键只响应非repeat的KEYDOWN，重复make仍留证，不造成重复文件写入。

| 操作 | 条件和行为 |
|---|---|
| 初始点A客户并输入 | C尚存时A被模态阻挡，点击只激活C；键实际路由C，不能伪造A得焦 |
| 关闭C | 默认Destroy C；A和B继续，owner/modal资格由真实内核解除 |
| A左面板左键按下 | ScPointerCapture(1)；跨B移动仍收到A客户坐标，可为负/越界；真实左键UP后ScPointerCapture(0) |
| A第一次Close | RejectClose并改标题；下一次Close可默认销A，进程策略仍属应用 |
| F5 | B仍活时切HIDE/SHOW；真实VISIBILITY另留证，隐藏期间其它窗KEY/TEXT让B模型推进但不画 |
| F6 | B仍活、真实可见且已有surface时，取旧Paint claim再Place改变客户宽±32，断言旧End和旧geometry Submit都真实STALE；不符合条件无动作、不置假coverage |
| F9 | 任意活焦点窗写status1快照，继续等待真实消息 |
| F10 | 写status2结束请求并done；不在callback调用KernelExit。main关闭C/B/A、回收surface、核本人heap，再覆盖带cleanup_done的最终日志并正常return |
| 所有窗已关闭 | main自行决定结束，标AUTO_END；不是库关闭一个窗就杀进程 |

固定512条仅为验收日志容量；第513条明确error/OVERFLOW并结束，既不覆盖旧事件也不冒称OS队列上限。真实QUEUELOSS单独保存window0原消息、置LOSS/error并收束，不从快照补造缺失KEYUP/按钮UP。应用没有通用线程过程或控件树。

结束成功至少要求 `status==2`、`flags&2`、`error_count==0`、`alive_mask==0`、`heap_after==heap_before` 及真正进程退出码0。F10前一份status2尚无cleanup_done，不能提前当回收成功。仍要按实际coverage和事件顺序判本轮操作，不能只按F10就称所有输入测试通过。可用STD最后输出 `SC32_TWIN_EXIT\n` 或 `SC32_TWIN_FAIL\n`；日志写/flush失败与非零退出必须报告，旧日志不能冒充本次成功。

三环heap恢复只核本人surface/SC32过程metadata，没有把其它进程、内核画布、队列缓存或全局PF波动混进该比较；进程EXIT后的模块/栈/内核队列与全局页回收由主代理外部采样。正常main/库fini和异常owner兜底仍按系统合同执行。

## 4. 二进制日志 v1

路径固定 `/HOME/M10P/GUI.LOG`，创建mode63；只重写本测试记录。采用小端32位值，无宿主指针、没有压缩，C端有header128/message64/info96编译期尺寸断言。

```text
offset   0 .. 127   TwinLogHeader 128B
offset 128 .. 415   ScWindowInfo[3]，A/B/C，各96B
offset 416 .. 511   event_counts[24]，各DWORD
offset 512 .. end   ScMessage[event_count]，各64B
file_bytes = 512 + 64 * event_count
```

头前8字节是ASCII `SC32TWIN`（无NUL）。以下DWORD编号是**从偏移8开始**，DWORD n在 `8+4*n`，不是整个文件的DWORD编号：

| n | 字段 | 语义 |
|---:|---|---|
| 0 | version | 1 |
| 1 | header_bytes | 128 |
| 2 | status | 0 READY、1 F9快照、2结束、3失败 |
| 3 | file_bytes | 精确长度 |
| 4 / 5 | pid / pid_generation | 真实本人ScProcessSelf |
| 6 | snapshot_count | 包括READY/F9/F10请求/收束最终的成功尝试序号；写失败不会称durable |
| 7 / 8 / 9 | event_count / event_capacity / event_size | N / 512 / 64 |
| 10 / 11 | info_size / info_count | 96 / 3 |
| 12 / 13 / 14 | window_a / b / c | 本次窗口编号；失败候选以alive_mask区别 |
| 15 | alive_mask | 位0/1/2分别A/B/C当前三环生命周期存活 |
| 16 | coverage | 下表真实观察/实际操作位 |
| 17 | paints | 成功End次数，不是时钟FPS |
| 18 | submit_calls | 实际成功Submit次数 |
| 19 / 20 | submitted_lo / hi | 成功接受矩形像素字节64位累计 |
| 21 | error_count | API/断言/日志容量/queue loss/回收失败数 |
| 22 | first_code | signed32 ScStatus |
| 23 | first_domain | ScErrorInfo domain，或明确本地断言域 |
| 24 | first_raw | signed32真实raw/本地断言值 |
| 25 | first_operation | 真实ScErrorInfo.operation；LOCAL_ERROR时为90000000 OR本测试action |
| 26 / 27 | heap_before / after | 本人process_heap_pages，基线在创建窗之前；快照未收束时after仍基线 |
| 28 | flags | 下表位值 |
| 29 | last_tick | 写快照时宽monotonic的低字，不替换每条原事件tick |

flags数值：1=F10请求；2=cleanup_done；4=日志OVERFLOW；8=真实QUEUELOSS；16=首错误是本地断言诊断；32=所有窗关闭后AUTO_END；64=初始READY日志已开始。本地action只为定位应用，不冒充SC32私有selector；action 1日志容量、2Query、3Invalidate、4Free、5Allocate、6Begin/尺寸、7Submit、8End、9Write、10快照、11Open、12Flush、13Close、14STD、15显隐、16Place/旧claim、17旧End断言、18旧Submit断言、19运动损伤、20Theme、21Close veto/title、22capture、23Create、24Self、25基础查询、26目录、27Get、28loss、29Dispatch、30Destroy、31heap收束、32STD Close。

WindowInfo是最后有效96B副本；DESTROY后保留它以解释原客户坐标，alive_mask清对应位。未创建的条目保持零，不伪造内核对象。header字段不包含内核地址。每次快照额外查询本人的ScInputSnapshot，断言三个窗至多一个focused；输入快照只用于核当前事实，不填造事件日志。

event_counts[1..20]按正式MSG_ID统计**实际记录**，0用于其它ID，21..23保留零。events是窗口回调收到的原64B值，CREATE和显式同步DESTROY来自三环真实生命周期事实，带APPLICATION/sequence0；它们不能说成IRQ输入。内核queued DESTROY辅助副本若已退役由库吞掉，不重复进入已释放context。QUEUELOSS例外由main保存其真正window0值；不记录经过广播改目标的副本。原始tick、flags、epoch及六字data完全保持。

## 5. coverage位

下表bit从0起，标志值为 `1u << bit`。它是观察集，不把测试应用固定缓冲当系统能力，也不是只看某个总PASS标签。

| bit | 含义 |
|---:|---|
| 0 | 初始C.MODAL且A没有INPUT资格 |
| 1 / 2 | A / B收到真实FOCUS gain |
| 3 / 24 | 收到KEYDOWN / KEYUP |
| 4 | 收到TEXT |
| 5 / 6 / 7 | 左 / 右 / 中按钮DOWN，各沿保留在日志 |
| 8 | A左客户区实际capture成功 |
| 9 | A捕获期间真实左UP后实际release成功 |
| 10 | capture期间MOUSEMOVE客户坐标越A边界 |
| 11 | A首CLOSE实际RejectClose成功 |
| 12 / 13 | B / C实际CLOSE请求 |
| 14 / 15 | B真实VISIBILITY hidden / visible（15含初次激活，恢复顺序需读事件） |
| 16 | B隐藏期间模型由其它真实KEY/TEXT推进 |
| 17 / 18 | 收到SIZE / MOVE |
| 19 / 20 | F6旧geometry Submit / 旧Paint End真实返回STALE |
| 21 | 实际成功Paint End |
| 22 | main进入正常结束/回收路径（API错误仍可能status3） |
| 23 | 无活窗且本人heap恢复成功 |
| 25 | 收到CAPTURELOST |
| 26 | 收到FOCUS loss |

主代理当前一轮按实际需求点模态A/输入C、关闭C、A真实Shift+A通断、A capture跨B真实释放、首Close A拒绝、F5隐藏/其它窗输入/恢复、F6几何失效、关闭B后A继续、F9/F10及退出页回收即可。不为每个位自动展开全组合或重跑无改动Foundation/网络/字体/M8。右/中按钮另按本轮确有实际输入证据报告，不因未做就把日志status2改称全覆盖。

## 修订

2026-10-15：收齐纯公开SC32API双根窗/owner模态验收应用、主题角色surface/局部提交、真实消息日志、关闭拒绝/capture/显隐/几何失效和正常页回收出口；静态阶段，宿主/客体编译及Windows输入由主代理统一验证。

2026-10-15：主代理统一构建、实际客体生成的库/公开应用与三窗口P0运行通过；首Shell背景表一页单列，暖PF及GUI池精确恢复，证据见evidence/m10a2-sc32；完整平台继续。
