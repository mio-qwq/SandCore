# SC32 Drop：真实目标与有期限ticket

所属版本：**SandCore M10a2 — The Application Platform Era**。公共头 `user/SC32DROP.H`，私有字布局 `user/dropapi/SC32DROPWIRE.H`，7项cdecl桥在 `user/dropapi/sc32_drop.c`。本批协议/桥/真实WM/kernelDrop/Broker helper、退出与session撤销已接线，**尚未构建或运行**。它没有用NOT_SUPPORTED正文冒充支持的入口，也不能以源码收齐称跨窗口拖放已运行。

## 1. 正式记录与selector

统一SYS300沿现私有gate，420 REGISTER、421 UNREGISTER、422 AT、423 VALIDATE、424 CONNECT、425 COMPLETE、426 CANCEL。Error.operation从e00起；不改旧SCAPI、已冻结Broker31函数或FONT/GUI调用。公开小整数用enum，避免CPP宏成本。固定输入精确size/version1、保留字段0，out规范宽度..65536/version1、整区RW预验后才effects，成功规范prefix/未知尾保持。formats表为完整RO输入，允许SCL只读页，不保存用户指针。

| 记录 | 精确字段顺序，每字段4B |
|---|---|
| TargetDesc48 | size/version/window/generation/endpoint/actions/formats指针/format_count/flags/reserved[3] |
| TargetRef16 | size/version/target_id/target_generation |
| Point32 | size/version/source_window/source_generation/screen_x_i32/screen_y_i32/actions/flags |
| ValidateRequest32 | size/version/ticket_lo/ticket_hi/source_window/source_generation/target_id/target_generation |
| Token16 | size/version/ticket_lo/ticket_hi |
| Info144 | size/version/target_id/target_generation/window/generation/endpoint/actions/client_x_i32/client_y_i32/geometry_epoch/visibility_epoch，然后Identity48，末ticket_lo/ticket_hi/expires_lo/expires_hi/source_window/source_generation/flags/reserved，再追加source_pid/source_pid_generation/reserved2[2] |

Identity48完全采用 [SC32-BROKER.md](SC32-BROKER.md) 的真实PID/gen、UID/GID、realm/session/subject与资格事实。整数是value；不能据endpoint/target/window值读foreign canvas/proc/title、替换身份或取得SYSTEM。

函数为 `ScDropRegister(desc,&TargetRef)`、`ScDropUnregister(window,generation)`、`ScDropAt(point,&Info)`、`ScDropValidate(ValidateRequest,&Info)`、`ScDropConnect(ValidateRequest,&channel)`、`ScDropComplete(Token,action)`、`ScDropCancel(Token)`。C普通标识符不能让typedef与函数同名，因此32B记录正式名为ScDropValidateRequest。Complete的私有request24为size/version/ticket_lo/ticket_hi/action/reserved；Cancel直接Token16。Connect输出私有BrokerToken16，经完整scalar out预验后才发布本人channel。

## 2. 注册与WM事实

Register必须真实本人window/gen、本人且listening endpoint、同登录session，actions为COPY1/MOVE2非空子集；formats1..64项、编号非0且无重复，内核完整复制。每个window只保留一个当前注册，注册替换必须原子且推进/替换target身份，撤旧票；不能把64项一次描述预算当全局target数量上限。flags/Point.flags当前只允许0。

At不是窗口列表猜测：source必须真实输入/capture资格，Root按当前active同session的真实最高输入窗确定point目标，检查可见、modal、遮挡及geometry/visibility epoch。透明Tooltip不挡输入；noactivate菜单实际挡在root窗上时，如果该菜单未注册，结果为NOT_FOUND，不能穿透找下面target。只能回已登记且有资格的目标、client坐标、真实owner身份与短寿命ticket，不泄露其它窗口内容。

Root提供可信local WM helper：本人window验证及真实身份/可见/input资格；实际点最高输入窗；几何/可见性代数。它们不给R3窗口/画布/proc地址，ABI在Root与WM统一接线时记录。当前桥不自行模拟这些事实。

## 3. 票、连接与撤销

ticket由内核绑定source PID/gen/window/gen/session和target PID/gen/window/gen/registration、geometry/visibility、允许actions及真正宽单调期限。Validate仅source或target可用，并给重新核实的Info；目标不能相信源在IPC正文里自报的身份。Info144尾部的source_pid/source_pid_generation来自票据真实源进程；目标逐帧与ScIpcPeer及ScIpcPacket.sender的PID/gen/session核对后才接受OFFER和正文。NONE为NOT_FOUND，旧代数/已撤或过期为STALE，越权ACCESS。

最终真实UP不自动撤票，允许UP后的Transfer/commit ACK；Root标记有期限handoff，Info.flags可给CAPTURED1/HANDED_OFF2事实。显式Complete/Cancel释放，不保留无限历史token。Unregister/替换、endpointclose、源/目标窗口隐藏或destroy、session注销、源/目标退出均撤相关票；所有链按真实owner/session组织，分批清理，不扫描全任务来猜别人的桌面。

At只给foreign target endpoint值，而原Broker只能按name Connect；不能让UI猜对方名称或把这个数值当本人拥有权。因此独立DropConnect在核ticket/source身份、同session及目标当前几何资格后，由Root可信Broker helper连接已验证endpoint，仍执行真实ACL、队列、refs和peer身份。返回是本人CONNECTING channel，按正常IpcAccept/Query/Ready等待实际连接，绝不跳过对端Accept或伪造一个channel。原31Broker协议保持，由Root给新增内核helper接线。

Complete当前仅source调用，action必须COPY或MOVE单值且在票允许集合内；它只消费该协议轮次。OS无法凭这个action参数证明应用磁盘已经提交，源SCSHELL必须先收到来自真实target身份、同票/nonce/动作且成功的持久commit ACK，再决定逐项删源。Send成功、连接成功、At成功或Complete成功都不等同文件MOVE完成；失败保源及撤临时资源。

## 4. Ring3 IPC envelope与Offer

ScDragEnvelope严格64B：size/version/type/nonce_lo/nonce_hi/source_window/source_generation/target_id/target_generation/ticket_lo/ticket_hi/format/allowed_actions/chosen_action/body_bytes/status_i32。阶段为OFFER1/ACCEPT2/TRANSFER3/COMPLETE4/CANCEL5/REJECT6，COPY1/MOVE2。body_bytes是本帧跟随正文尺寸，不能含用户VA或把nonce当权限；传输分块顺序/总长度由应用的版本化正文协议核对，真实帧顺序另读ScIpcPacket双字sequence。

Offer正文前16B ScDragOffer `{size=16,version=1,count,total_bytes}`，跟count个16B ScDragFormat `{format,offset,bytes,flags=0}`。offset相对整份body，table/body每个乘加、端点及不重叠先检查，所有格式ID唯一，源span保持到复制完成；正文不携对方指针。完整格式表由Ring3界定实际预算，Kernel只识别登记格式资格与有界IPC/授权Shared能力，不能把应用schema塞进Ring0。

源OFFER实际identity来自Packet.sender；target先Validate ticket与自己的真实registration/window，再回复ACCEPT；Transfer完整候选验证/真正commit后回COMPLETE。源再次核Packet.sender、ticket/context、nonce、动作及结果，才发布成功并Complete。拒绝/取消/超时/目标变化关闭channel并释放ticket；大内容可用有界帧或明确grant真实Shared，不借BSS字符串或foreign地址伪共享。临时snapshot读成本人数据后先Close再应用回调，避免callback中clipboard替换悬空。

实际UI `user/scui/ui_data.c` 由对应owner实现，core消息泵/输入步骤由SCUI owner接。必须按真实有序DOWN/MOVE/UP及输入epoch，不靠超时poll猜鼠标沿；菜单捕获与模态范围同用WM事实。当前协议不包含假事件/固定target数组或已执行验收声明。

## 修订记录

2026-10-15：冻结7函数与48/32/128记录、真实point ticket/Connect缺项、显式Complete/Cancel及64B Ring3 envelope；Root实际kernel/WM接线和UI workflow待完成，未构建/运行。

2026-10-15：同批尚未发布的Info追加真实源PID/gen至144B，闭合票据与实际IPC peer核对；128B前缀字段顺序保留。


## 2026-10-15 内核正文接线

真实正文位于 `kernel/sc32_drop.c` 与 `wm_drop.inc`，已组合到task/main/Make。每源窗口一个当前拖动轮次；Target、Ticket、Window索引及Owner/Session链均是动态稀疏池。一次formats64项是请求预算，不是全局target数量上限。注册可以在隐藏或尚未显示的本人登录桌面构造；At/Validate才检查实际可见/modal/输入/当前session，不能把后台初始化失败当安全隔离。

At需本人聚焦capture与当前已路由screen点，落后点为STALE；新轮次必须真实left down。相同source/target/registration/geometry/visibility/原点/允许actions复用ticket与一个连接，MOVE更新点与30秒宽单调期限。最后UP的At仍在capture期间冻结点，Info标HANDED_OFF；Validate不要求继续capture，有限期限内保持handoff。改变目标撤旧票，Complete/Cancel消费；Connect每轮仅一次，后续重复为BUSY，At不生成pending连接。回调ACK不是OS磁盘提交证明，MOVE依然须应用持久事务确认。

Ticket低字是真实池槽+1、高字是不会静默回绕复用的序号；序号耗尽明确RANGE。源/目标身份都重新核对真实PID/gen/UID/GID/realm/session/subject。几何代数之外还核client原点/尺寸，窗口MOVE不能保留旧点授权；visibility/session切换、隐藏或停任务立即使旧票不可用。源窗口变更主动丢本人票，目标销毁/替换一次active撤销后按真实关联票链分批收束；其它已失效元数据受30秒期限链回收，不在高频鼠标移动全扫任务。每轮Target GC/Session关闭/过期链分别最多32步。退出在auth/pd之前撤本人票和注册，保留其它source的真实拥有者链至收束。

登记formats完整拷贝与去重；具体OFFER格式/正文解释由Ring3 target依据自己的声明核对，内核不解析应用IPC envelope。WM只返回屏幕命中的真实最高窗口及可信值，菜单遮挡不穿透，Tooltip透明跳过；不返回foreign画布/过程/标题。仅query用户输出整区RW预验后才有副作用。以上**源码未构建/执行**，真正跨窗口拖动、对端sender核对、拒绝/取消和回收须新客体证据。

修订：2026-10-15，收齐真实内核票据/WM命中、30秒handoff、PID/peer144记录与动态清理链，旧SCAPI字节保持，待统一验证。

2026-10-15静态授权修复：捕获丢失不当作UP。handoff前核起始真实input epoch及仍有capture，失捕/失焦/INPUTSYNC立即撤票，重获capture不能复活旧票；真正UP的At或仍同输入资格下Validate才冻结handoff，之后合法ACK不要求继续capture。新增事实只在内核local WmDropFact，公开144B布局不变，未运行。

2026-10-16：旧ScDropComplete仍只消费传输轮次、不证明文件提交。显式SCUI v2在HOLD/READY前消费30秒输入票，后续只在已认证channel/nonce/owner上传真实job终态，不延长旧票或增加内核权限。源视图只在最终结果后复核实际FS，旧v1语义保持。详见SCUI-DROP-DEFERRED，待统一构建与客体验证。
