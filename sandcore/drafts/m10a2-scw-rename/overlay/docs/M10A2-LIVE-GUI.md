# M10a2 当前真实客体接力

2026-10-16。完整持续目标仍active。不是最终验收完成声明。先读本页，再读P6实施状态和根M10a2RULE.MD；不要用旧handoff覆盖当前工作区。

## 已完成的当前凭证

- `build/m10a2-p6-guest-generation-run-03/p6-generation.json`：DECLARED_P6_NATIVE_GENERATION_PASS；8普通库、21工具/应用记录（含工具种子）、27个真正SCO；最后整体owner账physical_delta=0。原报告SHA为f5538ba547f2ae38e00f35d35b57e2c0a55ef8282955aefbf221042122c57ddc。current_inputs_unchanged/source_disk_unchanged均true，changed为空。
- 同轮旧实际G1生成新SCCC1，再生成原生SCLD种子，真实编译/链接各库和应用；最终SCLD也由SCO生成。生成不等于安装或应用执行。主代理已查看该轮桌面截图。
- `build/m10a2-main-studio-01/p6-unified-host-05.json`：exit0、41产物、1320输入、source变化0；04/05只固定后续非编译工具变化，未变原生产物按实际源码闭包复用。SDK首次stage失败因为把未接线历史PAGINGSHARED-DRAFT当编译源；现核固定聚合入口完整include闭包及公共布局，未编译草稿仍完整归档并按HOST固定。
- `build/m10a2-p6-final-01/core/`：仅CORE/软盘构建，显式native桌面、LOGIN2及五库发行输入。CORE.SKM 597712B，SHA6f697fd6c09d38e409504ad987d3de945d47593f565678fde799ea4944989811。`drafts/m10a2-p6-native-core-receipt/`已核ELF/flat/CORE正文、程序摘要、六资源表、公钥与294非生成依赖；观察边界如其README所述。
- `build/m10a2-p6-final-01/sdk/`：九库native-preview严格stage成功，483文件/17364189B；proof为同目录codec-proof-02.json，HOST05。普通八库native，只有固定SCSOUND为授权HOST codec例外。尚无ResearchDesk客体编译/运行结论。
- `candidate-plan-01.json`及实际GUI data_prepare：867明确文件/metadata、13默认文本升级、19原件归档575907B；旧身份/权限及未列对象守恒回读。prepare追加metadata可选参数，旧调用默认不变。原M10a1盘仍只读；新GUI会话仅自己的单盘副本。

## 正在运行的GUI01

`drafts/m10a2-p6-acceptance/driver.py`持有QemuSession、GuiGuest、RefreshHarness及JSONL输入循环。启动命令已经执行，不要盲目再启动同目录：

```text
python -B sandcore/drafts/m10a2-p6-acceptance/driver.py --native-report sandcore/build/m10a2-p6-guest-generation-run-03/p6-generation.json --host-report sandcore/build/m10a2-main-studio-01/p6-unified-host-05.json --sdk-directory sandcore/build/m10a2-p6-final-01/sdk --core-build sandcore/build/m10a2-p6-final-01/core --source-image sandcore/build/m10a1/sanddata.img --out sandcore/build/m10a2-p6-gui-run-01 --execute --accel tcg
```

当前工具exec session=81493，QEMU PID=15152，普通UID/GID1000、SID4，原选中SID1。这是瞬时状态；恢复必须先核会话/进程确实仍在，不能假定平台中断后后台活着。`p6-gui.json`是每步真实JSON、完整stdout、物理输入、截图与身份记录；没有整轮GUI PASS。TTY有ANSI折行，精确结果以JSON文件为准。

启动AC97保留，但启动段wavcapture已精确停止，后续不录长静音。Sound阶段再用audio_start/audio_stop短取证，none后端不称可听。

**VM运行期间不要编辑main、当前driver/runtime/flow/final-flow helpers、HOST产物或SDK副本。** driver最终会核其pins；只读观察和不在pins内的新草稿可继续。不得写客体内存，不注入Broker命令，不用SCUIDEMO F12。输入接口说明见`drafts/m10a2-p6-gui-runtime/RUNBOOK.md`，动作ID唯一，失败不确定业务不重发。

## 已实际做过的GUI动作

1. 真正su mio登录得到新SID4；sessionctl show/bind确认普通active。两个SCSHELL进程均实际UID1000。原生桌面及系统/开发多级菜单截图已看。
2. 鼠标走向子菜单时曾跨过父菜单另一行而切到开发组；随后用Left/Up/Right/Down/Enter真正从开始菜单启动Files2，ps确认FILES2.SCX UID1000。不是用SYSTEM启动再标普通。
3. Files2 CtrlL输入/HOME，CtrlShiftN打开名称提示；真实输入P6GUI并点确定，外部只读ls -ld确认`drw---- 1000 1000 0 /HOME/P6GUI`。再CtrlL进入该空目录，截图已看。当前窄窗口按源码自适应隐藏sidebar，最大化后应再看目录树，不应误报不存在；缩放数值尚未独立采样，不由截图字体猜百分比。
4. 通过开始菜单System子组键盘选择Notes2；它真实读出旧/HOME/notes.txt的159B原文本。随后CtrlN新建文档并输入`P6 native GUI note.\n`（20B），不覆盖旧文本。
5. CtrlShiftS打开共享FileDialog；上方目录填/HOME/P6GUI后Enter，下方填NOTE.TXT并点确定，实际出现“操作未完成，请检查错误”，文件不存在。该步失败，不能称保存成功。Esc取消后20B文本仍完整留在未命名脏文档，截图`p6-native-notes-unsaved-retained.png`已看。
6. 最后已点击Start并发送`down,right,ret`，意图启动开发组首项Studio2；当前尚未读下一张截图确认，下一步先观察，不重放此动作。动作ID为start-for-studio、start-launch-studio。

文件关联、clipboard/drop、Studio工程/F5/F6/F7、Debug真实SCL、其它组件代表动作、两主题、资源最终账仍待。正式SDK使用SCW工程工作区：RDSTATS.SCW先构建独立SCL，再RDESK.SCW四TU。工作区入口为“工程 → 打开工作区…”，没有CtrlShiftO，F7本身已启动Debug2，不再CtrlF5重启。

SCW（SandCore Workspace）为工程工作区格式，入口、样例、筛选与文档使用 .SCW。已有同名游戏存档格式保持；本次不扩内容嗅探或泛关联。桌面图标按实际透明度/背景键色和轮廓复核，保原前景颜色与资源，不能靠粗暴色键挖掉内部内容。两项仍在独占草稿准备，不改本活跃VM的冻结源码。

## 两处已定位、尚未改当前冻结代码的问题

**Save As阻断：** `user/scui/ui_dialog.c:812`调用旧ScPathMetadata，却期待精确NOT_FOUND；内核旧fs_metadata对缺名传-1，sc32_legacy映射FAILED，因此Notes的n_save根本未触发。最小修：该次改ScPathQuery、显式引入SC32PATH.H、HOST与真正SCO导入清单换掉唯一ScPathMetadata；旧API/SCAPI语义及Notes状态机不改。审阅已冻结于`drafts/m10a2-save-dialog-review/`。

**名称提示布局：** SCSHELL sh_menu_name_prompt误将单Name框交ScUiPropertySheet，导致“设置 2”、大窗口和Apply。`drafts/m10a2-files-prompt-fix/`已交4现有路径局部补丁+新shell_name_prompt.inc，用320×170逻辑标准modal/Label/Textbox/Button，保原job及持久ACK；未合main、未编译/运行。不是改Files2或SCUI PropertySheet本身。

先正常收束当前VM（Notes测试文档未保存，关闭时走真实丢弃确认；其余应用正常关/领完，show1后logout4，再runtime finish核owner）。然后合两处修复，实际HOST/SCO重生SCUI及SCSHELL正式manifest。`drafts/m10a2-p6-target-refresh/`可按真实label闭包只refresh变化库；其它逐项REUSED回原run03证据，不重跑27。SCUI变化须重新绑定LOGIN的五库资源摘要/CORE；若桌面字节没变，不额外重生成它。当前helper只AST静态检查，未实际执行，接入前审阅。

## 保存与下一批

main已保存cfdcd16与5a53cc9。后续SDK闭包修复、metadata hook、SDK/默认文档及GUI/候选/CORE凭证草稿尚有待提交修改；.zcodeignore为用户未跟踪文件，继续不碰。WSL push上次退出1，远端更新未确认。冻结成果原件及失败日志保留，不删除build旧数据绕过此前自动审批拒绝。

优先继续当前Studio独立工作流，必要时记录新的真实阻断，然后正常收束/修两个共享库/仅针对实际改变回归。没有证据的功能继续标待验，不因本检查点结束完整目标。

修订：2026-10-16，保存完整原生生成、native CORE/SDK、活跃普通GUI与两个实际共享层缺陷及最小后续路径。
