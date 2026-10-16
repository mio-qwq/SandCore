# M10a2 当前真实客体接力

2026-10-16。完整持续目标仍 active。先读本页、P6 实施状态及根 M10a2RULE.MD。本文区分生成、安装和实际运行，不是最终完成声明。

## 当前 GUI02 正在运行

最新实际进展：Notes/Studio/Terminal已正常关闭，仅Files2在 `/HOME/P6GUI/CLIP/RETURN/DROPBACK`，Canvas正从桌面启动。普通SID4仍active，VM/exec仍2364/22125。最后动作 `canvas-desktop-open`、`canvas-first-view`，后续先看真实画面，不重放启动。

Files实际完成文件COPY和CUT/MOVE：20B源/目标正文精确相等，E-6.REC/E-9.REC的头/连续三frame/CRC均正确，分别stage3/stage4、DURABLE=1；MOVE源目录只有RETURN、原NOTE不再存在。审计为VM目录的copy-journal-audit.json/move-journal-audit.json。真实Files→Desktop Copy到 `/DESK/NOTE.TXT`，再Desktop→Files Move到 `/HOME/P6GUI/CLIP/RETURN/DROPBACK/NOTE.TXT`；两份REC取回并按同样合同核验。第三次Files→Desktop拖放选取消，源20B/SHA/代数1564未变，/DESK前后均原16项。

跨窗操作本身持久成功，但Desktop在源移动删除后仍显示NOTE图标130秒以上；人工点击桌面空白+F5后才消失，图片p6-desktop-after-move-refresh.png与p6-desktop-manual-refresh.png保留前后。drop-journal-audit.json明确source_view_stale，不能称整个Drop闭环PASS。`drafts/m10a2-drop-source-refresh`正审阅传输ACK早于文件job持久终态、实际MOVE选择同步和发送端刷新出口，尚未合main；不能用过早一次refresh或目录轮询掩盖。

三处确定局部修复都已冻结但尚未合入：m10a2-name-prompt-enter（仅SCSHELL）、m10a2-app-path-arguments（仅NOTES2）、m10a2-studio-sco-source-path（仅STUDIO2，SCO根内名补明确根/后沿原严格资格检查）。后续接线m10a2-p6-followup-refresh已冻结，只读85副文件witness及当前布局/CORE闭包通过；目前该wrapper精确要求三个label，若Drop根因需要其它label，须先按实际设计调整明确范围，不能硬维持三个目标而不修。HOST08尚未构建。

操作注意：真实控件焦点有语义。地址TextBox内CtrlV属于文本粘贴；进入目录要等真实加载/新建job结束，随后点表格空白再CtrlV才是文件粘贴。此前过早切目录的一次操作落到旧目录、出现Result unknown，已正常关闭，未把它算成功；随后正确目录+表格焦点的单次COPY取得真实DURABLE ACK。业务动作不因超时盲重放。

exec session **22125**，Windows QEMU PID **2364**，原 SID1、普通 UID/GID1000/realm0 的测试 SID4。入口为 `drafts/m10a2-p6-gui-coherent/driver.py`，输入接口仍是 JSONL；报告为 `build/m10a2-p6-gui-run-02/p6-gui.json`，RUNNING。恢复必须先核进程，不能重启此输出目录或重放动作 ID。

本次输入使用 target-refresh-03、HOST07、final-02/core、final-02/sdk、原 M10a1 数据盘、`build/m10a2-p6-gui-coherent-layout-01/layout-proof.json`。实际新 i386 layout proof SHA 为95d1108d29e095223dca4f912e4ed3af57821d3ccbc0c7fdac3b699280ad79bd。CORE SHA为aed45e2f613e556cc4687582c5b79a39a55393c394ed0be9b57c58035dbd4102，SDK485项/17389865B。发行与构建凭证见 `drafts/m10a2-p6-final-02-receipt`，三个实际步骤exit0、1325限定源前后不变。主源及本次helpers/SDK/CORE均冻结，不在VM运行时修改。

基点已取得真实一致 PF13922/13922、host ready、等待和 GC 收束，完整暂停1.657秒并确认恢复；原可信host PID1/代数1 实际登记的新SCUI完整摘要为7059348b04925a4ba5f29702abd0c419a570b05008ad854908f70dae66d640b7。最终账尚未执行；不把GUI01的205页免除。

Root已实际查看并完成：

- 新桌面图标黑边消失。`p6-new-native-desktop.png` 已查看；像素凭证 `drafts/m10a2-desktop-alpha-runtime` 核1640不透明点原RGB保持、693半透明点全部精确匹配真实壁纸合成、2522透明点精确透出背景，原8图标及壁纸未变。baseline SID1与实际截图SID4的来源分开记录。
- Files2真正启动，CtrlL进入/HOME，CtrlShiftN紧凑新建名称框，输入P6GUI后点击新建成功，目录本人1000:1000并可进入。文本框裸Enter没有提交，按钮路径成功；修复草稿在 `drafts/m10a2-name-prompt-enter`，尚未合入。具体原因是同context Files OPEN快捷键优先于默认按钮，局部CAPTURE修复只作用于本名称框。
- Notes2读取旧159B notes.txt；CtrlN新文档输入20B，标准Save As保存 `/HOME/P6GUI/NOTE.TXT`。`p6-notes-durable-saved.png`实际显示“已持久保存”、标题无星号。只读取回 `notes-saved.txt` 精确为 `P6 native GUI note.\n`，SHA7def50866c7959c3579cf642bea6e2473349576f26c2c9dbba874db8ec921ec69。旧文本未改。
- Notes正常关闭后，Files刷新显示NOTE.TXT/20B/1000:1000；选中Enter走真实默认关联，但Notes对带外双引号路径返回-103。截图 `p6-notes-associated-status.png`保留失败。不是保存失败。最小启动解析草稿 `drafts/m10a2-app-path-arguments` 只修改Notes，另外四个关联入口已有相应处理；尚未合入。
- Terminal2从系统菜单启动，普通提示符mio/HOME。实际 `mkdir -p /HOME/RDESK /HOME/RDWORK /HOME/RDPLUGIN` 无错误并回提示符，三个目录也在共享对话框中真实列出。Shell已exit、Terminal正常点X关闭。
- Studio2从开发菜单启动。实际工程菜单“打开工作区…”/标题“.SCW”，SDK目录筛选仅列RDESK.SCW和RDSTATS.SCW。已打开RDSTATS.SCW，真实树含JSON/C/H；F5执行实际SCCC --object并输出SCCC OK SCO。随后依赖检查误报工程外头文件(-108)，未执行链接/发布。`p6-studio-scw-build-progress.png`保留实际编译命令和失败。正在针对SCO根相对路径与ScPathResolve当前目录语义准备局部修复；不重复F5。临时对象已被正常cleanup清除，Root只读get返回-1记录保留。

较早时点曾开Files2、空文档Notes2、Studio2及其错误提示，现均已按上方最新进展收束。继续其它代表GUI或收束本VM；最后切回SID1、注销SID4，再finish核一致owner账。新补丁只在新draft，等本VM结束后再合入、只刷新SCSHELL/NOTES2/STUDIO2；SCUI和CORE无须无故重编。main最新提交6bd6c93；当前新增凭证待保存，远端上次未确认。

## 当前实现与构建

- SCW（SandCore Workspace）是工程工作区，Studio 入口、打开/保存筛选和 SDK 示例统一使用 `.SCW`。先构建 `RDSTATS.SCW` 的独立 SCL，再构建 `RDESK.SCW` 的四 TU 应用。F5 构建、F6 运行、F7 调试。
- 共享 Save As 查询改用 ScPathQuery，保留精确 NOT_FOUND；FileDialog 类型列显示文件/文件夹。Notes 保存状态机及旧 API 语义保持。
- SCUI 图标和 ARGB 合成采用正确的整数 straight-alpha source-over；原图标资源、尺寸、采样与 opaque 快路保持。原问题是半透明像素被强制合成为不透明黑边，不能用删除白色像素解决。
- SCSHELL 单字段名称提示改为紧凑标准模态窗口，保留任务进度、冲突处理和持久 ACK。完整属性页继续使用原共享 PropertySheet。
- Studio 的只读 include 命名空间使用 DirectoryOpen/Next 和 FileLeaseAcquire；失败/取消清理保留目录句柄直到真正关闭。没有向 OpenPinned 传入不支持的组合标志。
- `build/m10a2-main-studio-01/p6-unified-host-07.json`：统一 HOST exit 0、41 产物、构建中源变化 0。06 的缺导入失败保留，已补 ScUiFocus/ScUiLayout，没有降低警告门槛。
- `build/m10a2-p6-sco-08/report.json`：实际 SCCC 宿主 IO 适配后端对 SCUI、SCSHELL、STUDIO2 三目标通过，正式 NATIVE 清单已经按实际 SCO 导入提升。此项不是客体编译或 GUI 通过。

## 当前原生刷新

`drafts/m10a2-p6-refresh-runner/driver.py` 已退出0，`build/m10a2-p6-refresh-vm-03` 状态 THREE_TARGET_REFRESH_GENERATED。生成目录 `build/m10a2-p6-target-refresh-03` 的报告为 DECLARED_P6_NATIVE_GENERATION_PASS，SHA256 `fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f`；完整严格读取审计通过，源码/原盘不变。原 VM 已关闭。只刷新 SCUI、SCSHELL、STUDIO2，其他产物逐项核源码闭包和副文件，显式 REUSED 到完整 run03。

刷新底座是旧的八个真实原生库、原生 SCCC/SCLD、桌面及旧 native-bound CORE；新生成库不会在本轮悄悄安装执行。当前待生成源与旧运行底座的观察源分别固定。旧布局头从固定 SDK 源归档核完整摘要，保持 owner helper 的严格检查。开始编译前的一致快照已核原 SID 的唯一可信桌面 PID/代数、MAIN 全文件摘要、图标/刷新/存储结束、SCUI 无未提交画面、等待状态及 GC 空链；PF 与资源账在同一次短暂停读取，随后确认恢复运行。

refresh-vm-01/02 都在新编译前失败：分别缺 core_binding 字段、误把待生成的新私有头当成旧运行底座源。原失败日志及原盘守恒结果保留。第三轮已完整通过：SCUI、SCSHELL、STUDIO2 六次编译/链接成功，原生映像/map/SCDB取回并严格核验，整体 PF 14036→14036，physical_delta/attributed/unexplained 均0、unexpected=[]。

完整原生来源为 `build/m10a2-p6-guest-generation-run-03/p6-generation.json`，SHA256 `f5538ba547f2ae38e00f35d35b57e2c0a55ef8282955aefbf221042122c57ddc`：8 普通库、21 工具/应用记录、27 SCO，整体 physical_delta=0，源码/原盘不变。工具链确实经过旧 G1 生成新 SCCC1、原生 SCLD 种子及最终 SCO/SCLD 生成。生成本身不代表应用已运行。

## GUI01 已结束，原失败保留

`build/m10a2-p6-gui-run-01/p6-gui.json` 状态 FAIL_OR_INTERRUPTED；exec 81493/QEMU 15152 已关闭，不复用动作 ID 或输出目录。所有应用已正常关闭、测试 SID4 已注销、原 SID1 已恢复。current_inputs_unchanged/source_disk_unchanged 均 true。

实际普通 UID/GID1000、realm0、SID4 通过菜单/真实 HMP PS2 完成：

1. 原生桌面和多级开始菜单；真正启动 Files2，创建本人 `/HOME/P6GUI` 并进入。只读元数据确认目录 UID/GID1000。窄窗口隐藏侧栏属于自适应布局，尚未独立采样缩放数值。
2. Notes2 读取旧 notes.txt；CtrlN 新建并输入 20B 正文 `P6 native GUI note.\n`。Save As 被共享查询错误阻断，取消后正文保留。关闭时走实际未保存确认并丢弃本次测试文档，未覆盖旧文档。
3. Studio2 通过共享文件对话框打开插件工作区，真实显示清单/C/H 文件树与编辑区；F5 因命名空间 pin 标志无效而返回 -101，未称构建成功。
4. Terminal2 实际同一普通 Shell 的 id 为 uid=1000 gid=1000 realm=0；`echo native-pipe | cat` 实际返回 native-pipe。普通 mkdir 建立 RDESK/RDWORK/RDPLUGIN 输出目录，Shell exit 后正常关闭终端。

原最终账 PF 13713→13918，+205 页未归属；FS native 对象减少 1、页减少 6，基点 wait_pages 与 wait heap 跨时点观察不一致。不能把差额称桌面缓存、符号碰撞或免除。新 GUI 必须在 baseline 和最终时点都使用一致快照，按活 owner/模块页及实际资源判断，不能回写旧 PASS。

## 下一次普通 GUI

刷新 PASS 后，正在构建新 `build/m10a2-p6-final-02/core`，按同一新 SCUI 的 LOGIN 五库资源发行摘要绑定；新的 SDK native-preview 使用 HOST07、完整来源链、八个普通原生库及唯一授权 HOST codec SCSOUND。旧 final-01 的 CORE/SDK 保持为历史固定来源。

新一次单 VM 的正常用户流程优先验证实际修复：桌面图标、紧凑新建名称框、Notes 保存和默认关联、Files 剪贴板/跨窗 Drop、Studio SCW 的 SCL/四 TU 构建→运行→Debug2。其余组件只做代表动作，复用未变 ABI/安全/平台/文本/桌面证据，不扩测试矩阵。AC97 保持，Sound 用短 wavcapture，结束立即关闭录音；none 后端不称宿主可听。

输入必须是产品真实菜单/控件/键鼠，不写客体内存、不注入 Broker command、不发送 Demo F12。管理 CLI 和只读取回仅作旁证，不算 GUI 操作。每个业务输入唯一 ID；不确定 MOVE/COPY 不盲重放。实际截图经 Root 查看后才记视觉结果。

## 保存纪律

main 已保存 cfdcd16、5a53cc9、f6cd28d；本批修复和真实刷新凭证正保存下一可恢复提交。WSL push 上次失败，远端未确认；用户 `.zcodeignore` 不动。原 M10a1 盘保持 SHA256 `3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64`，SCAPI.H 保持冻结 SHA256 `81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1`。M8 封存、用户文件、正式产物和失败证据保留。完整目标不在本检查点结束。

修订：2026-10-16，记录 SCW、共享层修复、HOST07/SCO08、GUI01 真实边界及三目标原生刷新接力。
