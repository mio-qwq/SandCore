# M10a2 GUI06 与四文件试玩交接

> 2026-10-17状态纠正：实际Files取消后已看到Target exited(313)，不是正常取消；最终截图与原报告存于evidence/m10a2-p6-gui06-partial。随后恢复时，进程表无QEMU20472、exec36349句柄缺失，不能继续该VM，也没有最后owner回收/注销证据。下文“仍活/冻结/取消待观察”只保留为当时检查点。默认启动器的TCG/SDL/无monitor选择已修正，现为WHPX优先、GTK和图形monitor，详情与冷启动证据见[M10A2-DEFAULT-ENTRY.md](M10A2-DEFAULT-ENTRY.md)。用户两盘没有重写。

2026-10-17，完整目标仍active。当前真实运行HOST12/native06/final06；exec36349、Windows QEMU20472、普通SID4，原SID1。主源、tools、当前SDK/CORE与活helper继续冻结。最终owner账尚未请求。

## 当前输入位置

实际绑定 `gui6-rebind-after-installer-process-observation`，不是initial-ordinary-bind。Debug2已正常拥有并结束Installer，Target exited(0)。同一个Debug2又真实拥有启动Files2，入口暂停1，实际原生Files映像SHA `58a83aa86019fdd81b0ff212fd5c6dd319251ee5ae2bfcd044d17a7b8960a03d`。

Files实际map的f2_close入口0041BE65已通过真实Add BP按钮安装软件断点；之后以打开Debug菜单刷新命令状态、Esc、F5继续，Files正常显示。当前路径/HOME，选中本轮安装的DATA.RDT。已真实拖到空桌面，看到四按钮确认，点击取消；随后点击Debug任务栏，最后图 `p6-gui6-files-cancel-stop-or-live.png`：Files仍可见，Debug窗口未显示，不能据此宣称退出或取消已完整通过。下一步先恢复Debug任务栏窗口，查看真实f2_close断点/退出状态；不要重做安装或重复拖放。

最后截图SHA `6f89f6c77e36e846d2558abfb5b910d3cb5a3723af0a59acad7899e1bc173dda2`。get不清绑定，run会清绑定；后续物理输入仍须引用已亲自查看的真实截图。

## 实际通过

- Settings程序表显示真实系统目录；环境表显示PATH。添加属性框持续OPEN，实际输入M10GUI=verified、确定后成为第二行。普通用户保存沿原权限返回SC32_E_ACCESS(-103)，新持有的错误框可见，Enter关闭。重读环境恢复原PATH一行，Settings正常X关闭；未将权限拒绝改成保存PASS。
- 安装器通过系统菜单尝试启动后无前台，ps未见实例；具体原因尚未判定。后续由Debug2实际启动、继续后窗口存在。首次点击任务栏后曾隐藏，再恢复即显示；实际暂停2在BrokerWait(EIP500110EE/EBX40B)。UiContext错误头读到-313/cause/raw -109(AGAIN)、operation5(PAINT)、node1，发生过隐藏/绘制交错，不能直接判为持续绘制故障。
- 安装器最大化后实际添加 `/SYS/SDK/EXAMPLES/RDESK/DATA.RDT`；只读预检73B通过、未写盘。开始安装后/HOME/DATA.RDT完成；详情显示源revision974、目标revision1508、DURABLE1，正文与UID/GID/rw核实、来源保留。实际get取回源和目标，各73B，SHA均 `e01e31d9117841935f2a0cadbf022efccd4ec7ec30982e6807c1c32363e74ff4`。
- Installer正常X触发已安装的installer_close断点，暂停3/0040DC4E、EAX0；F5继续后真实Target exited(0)。不是强制终止。

## 已明确或待定位的问题

1. SCUI快捷键查找先跳过缓存DISABLED，而执行阶段才做动态CommandQuery，导致状态已变为可用的F5/F9无法触发；打开Debug菜单重新查询后同一命令可用。工具栏也显示旧禁用状态。应修共同查找/激活与必要的应用命令状态更新，保持真正禁用命令不执行，不引入每帧全量轮询。
2. Installer默认840宽/556客户高在150%下将列表和部分按钮挤出。最大化业务已通过，默认布局仍需压紧设置区和常用操作、保留全部菜单功能与窄窗可达性。
3. Settings/Installer错误信息当前把多行body放进单行status，底部被截；应保留多行消息框、状态栏单行。
4. Settings保存权限拒绝后/HOME出现41B `.C31`，与该次临时候选长度相符；清理路径需核实。尚未取正文或判定具体失败原因，不将其删掉掩盖问题。
5. GUI04 Files取消后进程消失的历史事实保留；GUI06正在用真实owned调试确认原因，当前未作PASS。

## 用户请求的可运行四文件副本

用户明确要求复制当前可运行最小副本到 `C:\Users\Administrator\Desktop\test`，目录只含run.py、run.bat和两盘。已从只读原M10a1盘重新装配同HOST12/native06/final06精确payload，未复制活VM磁盘，全部计划正文/UID/GID/rw与其余原对象正文/身份/generation逐项回读核实。原盘SHA保持。

仅交付四项：run.py2983B、ASCII/CRLF run.bat237B、sandcore.img1474560B、sanddata.img268435456B。启动盘SHA `47921cc2f9f0a23ea404cbd041a216931844023eedbcdbb4c824e9a55a8cfb3f`，数据盘SHA `b08f0503261dac9860105a57c580e420abd7c63b245c3098773a21dff732c3c9`。入口独立于仓库，Windows QEMU/TCG、单CPU、256MiB、SDL窗口、AC97/DirectSound和e1000用户网络；fw_cfg每次新32B熵仅写系统临时目录并回收，试玩目录不增加文件。

真实执行 `cmd.exe /d /c run.bat --check` 返回0，目录仍严格四项。未替用户启动新VM，客体运行证据明确复用相同产物的当前GUI06。首次装配在移动两盘后因BAT尚未生成而中断，finish.py只补BAT并只读核完整成品，没有再复制整盘；原记录保留。完整回执 `build/m10a2-minimal-playable-01/delivery.json`，源在drafts/m10a2-minimal-playable。

修订：2026-10-17，记录Settings与普通用户安装实测、Debug命令缓存问题、Files取消当前调试位置，以及用户要求的独立四文件试玩副本。

修订：2026-10-17，补Files真实exit313及VM中断边界，撤销当前仍活的解释；补默认入口DPI/WHPX修正的独立证据链接。
