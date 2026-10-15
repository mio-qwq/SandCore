# 会话寿命启动与可信桌面角色隔离实现

所属 SandCore M10a2 Phase 5。这里是真实待集成源码，尚未构建、未运行；当前 Phase 4 的代码、五个动态库和客体验收输入保持不变。启动正文、角色owner/会话动态池、固定可信启动、endpoint绑定/发现与gate/R3桥已写；WM层级、首帧、工作区、枚举/动作正文由独立代理继续写，尚未收齐，不能构建或称底座完成。

公开 `SC32HOST.H` 的 `ScProcessLaunch` 使用32B `ScLaunchDesc`、64B `ScLaunchInfo`，记录版本1、i386 cdecl。私有32B `Sc32HostLaunch` 走现 `SYS_SC32=0x300` 中新 selector `0x800`。角色族实际分配 `0x801..0x80a`，其余到 `0x81f` 保留，与文件租约 `0x700..0x71f` 分开；未实现 selector 返回 NOT_SUPPORTED，不能置能力位。

输入路径与参数分开，路径接受至多127B原输入，resolve一次后为至多63B规范路径；实际装载器支持路径内空格。参数至多1023B，末尾唯一 NUL，flags及保留字段为0。完整输出范围先验RW，然后才装载。内核仅使用复制后的局部文本，不保存用户指针。成功返回真实 PID/gen/SID/UID/GID/realm，不创建作业或可等待 token，不继承三条标准流引用。环境和CWD由现 userspace_spawn 快照继承，凭据由现 auth_spawn 继承。

此入口仅允许真实普通会话里的普通主体，包括普通 root；禁止普通SYSTEM服务、外部串口SYSTEM、锁屏和关闭中的会话使用这项桌面启动合同。不会从UID、路径或任意SID数字授予桌面角色。子进程可在发起者退出后继续，仍是本SID成员，logout按真实session owner链停止它；显式terminate遵守既有权限。

装载正文抽取为 `task_exec_scx_parts`；旧 `task_exec_scx` 保留空格切路径与127B参数上限，然后调用同一正文。262144B暂存、SCX容器/载荷/BSS/栈/动态描述符校验、失败页回滚全部保留。168B旧 task_t.args 仍只给127B前缀，ARGUMENTS2由既有streams保存完整参数；旧调用未增加忽略寄存器或flags语义。

`make_patch.py` 只从当前真实源生成接线 diff 和输入摘要，不覆盖工作源、不构建。补丁还需与租约及完整角色实现合并，统一升级SC32版本/manifest、保护SYS/SHELL、文档和打包闭包后才进入Phase5构建。当前manifest不列本出口，当前capability不置新位。

## 可信角色与生命周期

每个真实普通SID有一个动态记录，以SID索引，另以PID/gen索引owner；不是固定数量数组。内核 `auth_session_shell_exec` 只能由任务0调用，只执行受保护固定 `SYS/SHELL/SCSHELL.SCX`，使用该SID普通UID/GID和normal realm，绝无SYSTEM委托身份。成功创建后、子进程尚未调度前内核绑定真实PID/gen。文件名、root身份、普通IPC名称、普通窗口都没有任命作用。

`HostQuery`只给本SID事实；`HostPrepare`仅绑定已获角色的本人两个不同SC32窗口、本人已listen端点及实际taskbar像素高度；`HostActivate`必须经WM证明首帧后才切换旧桌面业务。`HostConnect`不按名称查找，直接核绑定endpoint当前owner/SID/PIDgen/ACL，再取得真实channel与peer身份。它仍是异步CONNECTING，须由真实host Accept后按Broker状态继续；查询返回或创建channel不能当成服务已经接受请求。

角色记录epoch随死亡递增，旧epoch操作STALE，到最大值后停止自动复用，不能回绕使旧请求恢复有效。注销时先closing，禁止新发现/动作；任务停止立即撤角色及WM专层，owner实际资源随后沿原分步清理链释放。后台重启只沿真实待启动链，按deadline排序；一次poll只启动一个，最多四次连续失败/短命启动，之后显示旧桌面回退并停下自动重启。同一次活跃服务存活60秒后再死亡会恢复重试预算；用户可以在本SID服务离线时显式Restart。没有死循环重启或全会话每tick扫描。

## 窗口与记录宽度

| 记录 | 字节 | 约束 |
|---|---:|---|
| ScShellHostInfo | 96 | SID/世代、host epoch、PID/gen、endpoint、两surface和工作区值 |
| ScShellHostDesc | 48 | 本人两窗及endpoint，精确host epoch，flags/reserved0 |
| ScShellConnectionInfo | 160 | 实际channel + 96B host事实 + 48B真实peer |
| ScShellWindowInfo | 320 | 96B窗口事实、128B标题、68B完整exe字段；无canvas/procedure |
| ScShellWindowPage | 32 | cursor/count/epoch/examined/finished，空过滤页不假称结束 |
| ScShellWindowAction | 64 | window/gen与PID/gen都核对；动作不等于任务终止 |

窗口枚举每次1..64行，只在内核栈放一个320B row，整个调用者声明的row输出先验RW。窗口总量动态，游标0为起点；WM一次next限制检查预算，无法产出row但未结束时仍返回推进的cursor。expected epoch不符须重新取快照。行成功复制后才加count。WM变化只唤醒本SID已绑定host，不向它借出第三方指针。

SC32自身旧WindowControl仍只本人窗口。新的host动作范围仅同SID、真实PID/gen和仍活窗口，尊重modal/disabled/样式、应用CLOSE veto及旧窗口原关闭语义。具体WM正文仍在写，不能用目前声明冒充已生效。

必要定向验收：普通登录启动旧/动态SCX，实测发起者退出后子窗口继续；参数内空格和执行路径内空格；真实凭据/环境/CWD；旧SPAWN2仍受管且wait/EOF不退化；恶意输出只读页不产生子进程；logout停止子进程并回收；SYSTEM/锁屏被拒绝。这里没有假造PASS。

正常退出新增809 ScShellHostDeactivate(epoch,out)：本人PID/gen和精确epoch可幂等撤角色，清endpoint并进入STOPPING，仍保PID/gen到真实EXIT，拒绝新Connect/Prepare/动作，Restart仍BUSY。正常service先调用它再销Context/窗口/return，避免销role surface把运行中的ROOT fini/stdio提前task_stop。意外surface丢失或显示OOM仍detach→stop→有限重启。805 Workspace保持正高度实际工作区语义，不用height0暗藏清理。

SC32 Window PREPARING选择8192，保现Popup1024/NoActivate2048/HitTransparent4096。准备两surface保持visible0，只在同geometry尚未匹配完整Submit+PaintEnd时给该位；UI只从真实PAINT后Query事实允许有限首帧，不开输入/可见timer，首帧后资格撤回。公开error-operation与selector分开：host1000..1009、leasef00..f0a，避免碰现GUI800、Shared d00及Drop e00。

可信发行接线待正式服务正文收齐后完成：固定受保护路径是必要边界，但不能把旧卷上普通用户预先创建的同名SYS/SHELL映像自动当发行映像授角色。最终构建须把实际发行SCSHELL.SCX摘要固定到可信CORE/受信任更新登记，并在固定路径启动前核真实字节；缺失/不符保旧桌面且明确失败。不得用全零摘要通配或隐藏开发信任路径。当前没有正式SCSHELL.SCX，也没有把这项未接线写成PASS；这不涉及生成或读取用户正式私钥。

可信发行摘要正文现已写（隔离未构建）：tools/mk_shell_issue.py必须消费实际依赖SCSHELL.SCL、无嵌入icon且<=262144B的SCSHELL.SCX，生成CORE内非零SHA与精确bytes；没有默认放行header。auth固定事务标志仅任务0可置，task loader在已有SCX单一暂存读payload后、任何pd/spawn前核同一内存映像SHA，随后映射该相同字节；普通同名EXEC不授role。不会先hash再重读另一个磁盘版本，不使用用户私钥。最终服务正文/Make目标仍需收齐，这不是完成或实测证明。

桌面背景不复制旧整幅壁纸：Phase5新增ScUiPainterClear清当前clip的ARGB为透明（没有RGB颜色内容）；ScUiPainterTextColor只临时选择既有24个命名主题色角色，旧Text仍原样；ScUiPainterBounds返回当前damage的本地clip，让自绘控件只查询相交空间cell而不每次重测全部可见字形；ScUiContextWaitTimeout返回真实timer/drag下一期限与caller最大等待的交集，让GUI/IPC混合泵不猜固定高频poll。对应SCUI ABI1.1/v2、162公开入口，公开SC_UI_TEXT_*名称与原实际flags值一致。自写桌面class先清透明，再画图标/选择/文字，内核既有壁纸缓存仍在底层合成，仅留一份背景。普通窗口默认face与taskbar主题不变；声明/正文/manifest只在本隔离patch，现P4的158入口/当前库未改。需实际整批性能/画面验证，不称透明层已运行。

新80a ScShellHostWindowAt使用32B ScShellHostPoint与96B ScWindowInfo，error-operation100a。只真实ACTIVE host对当前可见本人SID普通窗口查询实际hit/z序；含desktop/taskbar与普通popup，用于把图标布局手势转换为真正跨窗FILES Drag。SYSTEM/外SID/隐藏SID被拒绝，输出完整预验，失败不写out。此查询无Drop票/副作用、不授文件权，不能代替ScDropAt/Validate的sender与票核验；避免用creation枚举顺序猜覆盖关系或每鼠标包铸临时票。

2026-10-15后续：窗口分页的64检查预算现在覆盖整次调用，而非对每一输出行分别64；private wm_shell_window_next收到remaining budget，行总量仍动态、空过滤页仍推进cursor。绑定host的真实窗口变化沿TASK_EVENT_VISIBILITY唤醒，桌面只在初次/变化/丢失重同步取分页，不每tick全量枚举。lease最终freeze2的error-operation是f00..f0b，SetMode70b；前面f0a终点为历史v1记录。

最终原生产物与可信摘要必须是同一字节：Make候选提供显式SHELL_HOST_IMAGE输入，bootstrap可取本目录HOST SCSHELL.SCX；实际G1生成正式SCSHELL.SCX后，先核其来源与完整依赖，再用该原生文件重生成发行header并重建必要CORE。最终数据盘安装同一原生文件，不能把HOST摘要沿用到不同机器码而谎称能可信启动。生成header与选择输入的SHA须进入Phase5构建报告；切换输入时显式刷新header，不能只依赖来自不同目录的mtime。尚未执行这条整合/原生/最终CORE流程。
