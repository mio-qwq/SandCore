# 异步文件拖放

SCUI ABI1.4/library5追加SCUIDROP.H的四个cdecl入口；原170入口、SCUI.H、ScUiDragData的80B布局及旧flags0行为保持。SCSHELL ABI1.2/library3追加SCSHELLDROP.H的三个入口，原96入口及SCSHELL.H保持。版本常量在新头，正式库版本以manifest和实际SCL动态表为准。

## 同步与异步

原flags0使用envelope v1。成功返回的TRANSFER回调按原合同表示同步接收已经完成，SCUI立即回复原传输COMPLETE；它不推测调用者另建的后台job。旧源和立即接收者继续沿原行为。

只有显式SC_UI_DRAG_DEFERRED的source才协商v2；普通ScShellDragBegin仍严格使用flags0，新Files/Desktop使用ScShellDragBeginEx和SC_SHELL_DRAG_DEFERRED。同一新版SCUI中的新source与不调用Defer的立即接收者可正常完成，但bytes0的旧式COMPLETE只是原同步声明，不带文件DURABLE或SOURCE_CHANGED。严格异步接收者遇到v1不自行降级，Defer返回NOT_SUPPORTED并在启动job前拒绝，来源保留。只识别v1的历史SCUI库不能解释v2，明确拒绝，不静默降级或冒充持久完成。

## 公共值对象

| 记录 | SC32字节 | 合同 |
|---|---|---|
| ScUiDropReceipt | 16 | size/version/id/generation；本人context单调不复用句柄，拥有引用，必须Release。 |
| ScUiDropResult | 56 | size/version/action/status/flags/total/durable/source_changed/retained/failed/skipped/uncertain/reserved[2]。 |
| ScUiDropReceiptInfo | 152 | size/version/state/status、80B原TRANSFER借用视图、56B结果。TRANSFER是已认证的本轮副本，data只借到Release/context退出，不能改写。 |

Defer只接受当前真实完整TRANSFER回调中的原event，复制event或回调外调用失败；取得receipt不等于READY。Query成功返回有效快照，包括FAILED。Reply相同结果幂等，发送完成才返回0，期间PENDING；不同结果拒绝。Release未完成引用关闭对应channel，使真实对端收到取消/EOF；SENT引用只回收状态。receipt保留context，quiet fini不执行应用完成回调。

COMMITTED要求status非负、total非零、全部DONE+DURABLE且无失败/跳过/保源/不明。MOVE还需真实来源改变，COPY不得携来源改变。SOURCE_CHANGED必须与source_changed计数一致，计数不能超过DONE+DURABLE数。reserved必须0。SCUI验证形状、动作和真实peer轮次，不把普通R3业务声明升级成内核磁盘证明。

SCSHELL默认文件路径更严格：ScShellDropFilesDeferred仅从该receipt的真实原FILES副本创建job，绑定同一context/receipt，成功接管并清caller句柄。ScShellDropReply只接受这种绑定job，不接受任意普通job与应用填写的成功位。每步最多审16个真实结果，READY表示还有审阅工作、PENDING表示等待，END表示最终消息已发送并释放receipt。DONE+DURABLE、status非负且stage为原子RENAME(4)或SOURCE_REMOVED(5)才累计来源改变；目的已提交但来源保留不冒充完整移动成功。

## v2轮次

64B ScDragEnvelope保持字段布局，version2新增HOLD7、READY8、FINAL9；v1仍只解释1..6。HOLD/READY无正文，FINAL正文严格为56B结果，envelope状态0表示协议帧有效，业务错误放result.status。新分支始终复核真实ScIpcPacket.sender、同channel、nonce、source/target窗口代数、原票号、format及允许动作集合，不读取foreign指针。

1. OFFER/ACCEPT/TRANSFER仍用真实WM票与普通Broker。完整正文回调调用Defer，暂不打开模态或开始文件job。
2. 接收端发送HOLD，固定两端及目标节点允许动作的交集。源端核实真实peer/票/轮次后调用旧ScDropComplete，仅消费传输票、释放cursor/capture，并发送READY。此处只发HANDOFF，不发业务COMPLETE。v2源HANDOFF及带结果的终态中，ScUiEvent.command为该次Begin返回token，不能把晚到结果误用于下一次物理拖动。
3. 接收端真正收到READY才可显示“复制到目标/移动到目标/取消”并启动对应job。最终动作可以不同于悬停默认动作，仍必须属于已认证交集。原30秒票不持过用户确认；交接后不再借旧鼠标落点授文件权限。
4. 真实job终态且逐项ledger审完才Reply。FINAL携实际动作、持久数和来源改变数。源端核完整帧、真实身份与该轮次后才发布带结果的COMPLETE，默认动作COPY不再遮住用户实际MOVE。
5. 来源视图据来源改变失效一次，再由原分页目录查询决定存活项与选中项。通知不授删源许可，源端不再删第二次。纯R3回报不能直接从UI移除文件；实际FS查询仍是视图事实。

HOLD到READY在原短交接期限内完成。READY后允许用户等待确认与长job，不因失焦或隐藏取消已确认事务；不生成隐藏画面。pending receipt是动态对象，不占唯一物理拖动槽。等待只依赖真实STREAM/既有context期限，待用户或job时不强制1tick，不轮询目录。

## 取消、错误与退出

确认前取消不创建job，回复取消并保留来源。源退出/节点销毁/显式取消或channel EOF使receipt失败；绑定job按原cancel路径取消未提交工作。对端发送后立即退出时先领取已排队DATA，再认EOF，不能吞掉真实FINAL。

已经持久提交的目标和已完成的原子rename/remove不靠界面回调假装回滚。部分成功逐项保留，负终态仍携已经证实的source_changed，来源视图复核；不明/异常终止不能记成功，必要时一次失效后重读实际FS，不自动重发MOVE。接收者与源关闭均清对应channel/receipt，已持久ledger继续保留。

桌面的“创建快捷方式”仍只写普通LNK，不改变来源；每项ScShellShortcutWrite返回真实修订才计提交。它以COPY动作报告无SOURCE_CHANGED的业务结果，失败部分按实际已写项保留。

## 当前证据

GUI02原COPY/MOVE的20B正文、CRC/DURABLE恢复日志证明文件操作成立；原source_view_stale与130秒后人工F5清除ghost证明自动失效失败。原取消保源与16项目录不变证据保留。本次是隔离源码修复，未构建、未客体运行；不能用原文件操作通过替代新异步协议、旧即时分支与自动来源视图的定向验证。

修订记录：2026-10-16，显式延迟完成、真实job绑定、短票交接及来源视图失效合同；等待Root统一整合与定向验收。

## 2026-10-17 输入排队修复（源码待验）

GUI11真实350ms按住后出现确认并取消保源、Files正常exit0，但快拖原失败保留。其UI error=-313 cause=-103 operation=7 node=12静态映射为搜索框tooltip PopupCreate，不是DATA操作10；原日志不改写。

SCUI当前library_version6、接口仍ABI1.4和原174入口，最低SC32API1.5/v7。最终UP使用独立ScDropAtRelease和原消息epoch/双字sequence，只认WM真实UP凭证。MOVE已排队而当前物理按钮已松开时只等待对应UP，不用新当前位置猜落点或把ACCESS当文件拒权。ScUiDragBegin可在仍持capture且同代的真实排队MOVE上准备私有源对象，所有跨窗权限仍由最终内核凭证授予；不存在凭程序消息直接投递DROP。旧flags0即时完成与显式v2异步业务合同均保持。

自动tooltip在到期时重查本人输入焦点/可见性/按钮/capture和真实控件命中，查询与创建之间的失焦只取消该自动提示；Menu/Combo及其它权限错误照常报告。避免旧hover在非聚焦窗申请popup触发ERROR，不把权限限制放宽到内核。

修订：2026-10-17，收齐真实UP接线和tooltip输入资格修复，原GUI11与快拖缺陷证据保留，待统一构建及定向验证。
