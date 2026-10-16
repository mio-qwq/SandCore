# 文件Drop终态与来源视图

本目录已写齐隔离源码和局部补丁，尚未修改主树、构建或运行。当前GUI02/pins与所有旧草稿保持冻结；SCUI ABI1.4/library5与SCSHELL ABI1.2/library3已经Root确认，实际动态表仍待Root统一生成。

## 已确定的因果链

Files2的f2_drop先复制真实FILES正文并打开“放置文件”确认，返回HANDLED。SCUI ui_drop_peer_step在TRANSFER回调返回后立即排COMPLETE，下一轮按旧p->action发送传输ACK；发送端ScDropComplete消费票据并回调SC32_DRAG_COMPLETE。

此时用户尚未选动作，文件job也不存在。f2_drop_reply稍后把pending_drop副本的chosen_action改为MOVE，ScShellDropFiles正确启动SC_SHELL_MOVE，但SCUI保存的协商action及已发ACK仍是COPY。Desktop的发送回调收到COMPLETE只清external_drag，不刷新来源。Files发送回调也只清drag_token。

真实GUI02的移动已由文件系统与恢复日志证明：目标20B、源路径不存在，E-C.REC为DURABLE且stage=RENAME(4)。此终态与SOURCE_REMOVED(5)均表示来源路径已实际改变；不能只认stage5而遗漏同卷rename。130秒后仍有选中图标，而人工F5后剩原16图标，说明当前模型没有被真正终态失效。人工刷新只用于继续GUI，不把自动刷新FAIL改成PASS。

因此仅在旧传输COMPLETE置refresh_pending仍会过早，不能解决用户确认和磁盘操作之后的失效。只靠chosen_action=MOVE判断也不可靠，因为它现在不是最终用户动作。

## 必须保留的边界

- ScUiDragBegin旧flags0、旧version1 wire、旧立即ACK语义，以及ScUiDragData/ScUiDragDesc全部布局保留。旧立即提交控件不用新接口。
- 只有Files/Desktop的文件拖放显式选择新异步协议；新能力不授文件权限，实际文件job/lease、候选、EXACT/ABSENT、权限和持久ledger仍是原实现。
- 不新增内核selector、不改变旧ScDropComplete：它本来只消费传输轮次，不能证明文件已提交。
- 不加目录轮询、刷新timer、重复Drop或全目录鼠标移动查询。终态产生一次普通refresh_pending，现有分页重建自己完成。
- 失败/取消/退出取消未提交项并保源；已经持久提交的文件按原ledger保留，不承诺凭UI回调撤销已经完成的rename/remove。

## 拟定公开合同

新增独立大写SCUIDROP.H与SCSHELLDROP.H，使用已有SCUI/SC32公开类型。ScUiDragDesc已有flags字段通过新头具名opt-in选择Ring3 envelope v2，布局和flags0不变。新增ScShellDragBeginEx才设置该flag；旧ScShellDragBegin严格flags0。SCUI原170导出追加4项，SCSHELL原96追加3项；旧SCUI.H/SCSHELL.H原字节保持。

四个cdecl入口：

1. ScUiDropDefer(context,event,receipt_out)：只准真实完整TRANSFER回调，固定本peer/nonce/window/node代数和真实sender，返回本人context的16B版本化receipt（size/version/id/generation）。调用者只拥有这个值，不得到channel、内核票或private指针。
2. ScUiDropQuery(context,receipt,info_out)：152B版本1快照，包含状态/错误、原80B TRANSFER借用视图及56B结果；状态区分交接中、可确认、待发送、已发送、失败。Pending不消费receipt。
3. ScUiDropReply(context,receipt,result)：56B版本1结果，含实际action、status、具名flags、items_total/items_durable/source_changed/retained/failed/skipped/uncertain和reserved。成功COMMITTED必须所有项DONE+DURABLE且没有保源/跳过/失败/不明；source_changed只由真实DONE+DURABLE的RENAME或SOURCE_REMOVED项累计。取消无job时status=CANCELLED、action0、无成功flag。Reply严格校验大小/版本/保留字段/计数和动作允许集，重复同结果为幂等，改结果拒绝。
4. ScUiDropRelease(context,receipt)：显式放弃/释放；未完成则关闭channel让对端收到真实EOF，已发送只回收本人状态。退出/fini/节点退役/EOF清理同一所有权链，不回调已释放应用context。

ScUiDragData布局不扩：新source opt-in时，终态回调的data/bytes可携同一版本化result；旧即时COMPLETE仍保持bytes0。新增具名handoff phase只通知物理拖动已交接，不能作为成功。发送端只清鼠标/拖动临态，最终结果到来才失效来源视图。

## 为什么需要传输交接

kernel/sc32_drop.c的expiry_refresh固定30秒；ticket_live还重新核当前落点、几何/可见性，不能拿旧输入票无限等待用户确认或长文件job。现公开API没有Deferred/Reply出口。

v2在完整TRANSFER后按以下顺序：

1. receiver在回调调用Defer并复制所需FILES正文，暂不开modal/不开job；库发HOLD。
2. source核真实peer、票/nonce/format与动作集合，使用既有ScDropComplete仅消费传输票，释放cursor/capture；不发业务COMPLETE、不删源。库保留已授权Broker channel和该轮身份，发READY。
3. receiver收到真实READY后Query才变“可确认”，此后不再借已消费票授任何新权。应用此时打开确认框；可选择COPY或MOVE，必须在已固定的两端允许集合内。最终动作不由先前悬停默认COPY推断。
4. 接收者原文件job真正终态且逐项ledger读完后Reply。SCUI在同一认证channel/nonce上传v2最终结果；sender校验后只在这时发布业务终态，带实际MOVE及source_changed。
5. sender按source_changed失效本目录一次；部分已提交/不明也发布真实终态，必要时失效并复核，不宣称成功或再删一次源。成功COPY或确认前取消不凭空删除来源模型。

交接前仍受原短票/资格/超时约束。交接后窗口失焦或隐藏不等于取消文件事务；source/receiver真正节点退出、显式取消或Broker EOF则退出异步状态，应用取消未提交job。pending事务是动态值对象，不占唯一物理拖动槽；同context可再次发起下一次真实拖动，不用宏或固定数组扩大上限。

当前ui_drag_wait会对任何drag/peer强制1tick；新交接完成的receipt不走该忙等，只靠实际STREAM/现context等待唤醒。物理拖动及尚在传输的旧阶段保持当前节奏。隐藏时不生成画面，文件job和协议泵继续。

## 最小源码范围

SCUI：新公开头与exports/imports版本接线；ui_data的private Drop段局部hunk或其独立私有include、相应fini/节点销毁/等待和v2解析。旧v1算法不重写，v2正文有版本化边界。

SCSHELL：旧DragBegin不变；BeginEx显式选择新协议。DropFilesDeferred从真实receipt原正文创建并绑定job、转移receipt所有权，DropReply只从该绑定job读取真实逐项结果，不接受任意普通job的成功声明。旧DropFiles行为保持。

Files2：真实TRANSFER取得receipt；background只在READY开原选择框；receipt挂到对应F2Job，选择动作与job一致；逐项终态审完再Reply，cancel/error/close释放。sender终态带真实结果才置refresh_pending。

Desktop：对称接收；原单job增加有界逐项终态审阅，不能只看progress百分比。发送端handoff清external_drag，最终source_changed再refresh_pending。RENAME和SOURCE_REMOVED同认；原选择/布局由正常分页重建匹配存活对象。

同批更新公开Drop/UI/Shell文档、manifest和SDK头归档。需要实际重生成的闭包据Root最终整合确定，不能因先前计划三目标就保留已确认缺陷，也不扩到无关应用矩阵。

## 结束条件

当前原故障、manual F5与取消证据保留。Root整合后同一GUI流程做一次Desktop→Files确认MOVE，核磁盘真正结果、最终action和source_changed、无需F5图标消失；确认前取消复用已有源20B/SHA/代数和目录前后相同事实，受改协议部分只作最小定向确认。旧立即提交Drop按已有控件证据及受改边界复核，不把本草稿称运行通过。

修订记录：2026-10-16，真实Drop文件终态与来源视图合同及隔离源码收齐；未构建、未客体验证。
