# M10a2 当前真实客体接力

2026-10-16。完整持续目标仍 active。先读本页、P6实施状态及根 M10a2RULE.MD；生成、安装和运行分别记载。

## 原生刷新04已完成，当前构建稳定读修复

主源已保存main@0384676；此后仅稳定读三内核文件和规范增量待保存。HOST09真实成功；v3确认实际变化恰好12标签，完整源码/正式NATIVE/HOST摘要及85项旧副文件来源保持。`drafts/m10a2-p6-followup-refresh-v3/driver.py` 已退出0，exec37714/QEMU9016关闭。outer `build/m10a2-p6-refresh-vm-04/refresh-vm.json` 为EXPLICIT_LABEL_REFRESH_GENERATED，SHA226d9d99ae250c247744430b0247f7c7f30cc315ca1687e2a8e6b604f7317175；inner `build/m10a2-p6-target-refresh-04/p6-generation.json` 为DECLARED_P6_NATIVE_GENERATION_PASS，SHA1ac7e951434d5465a665844019a731609a08c35c4a22a5f42bb6c43fac2b5191。12项全部真实编译/链接/映像/map/SCDB取回通过，27对象/8普通库/21记录完整，最终physical/unexplained0、unexpected=[]，源及原盘不变。

该刷新运行底座是旧final02 CORE/SDK及原native SCCC1/SCLD，新库未在刷新VM安装执行。新SCUI752812B/SHA159963852994f46afc7deb71cede4b8b56ae39be35b65c602db678be1837bdf6；新SCSHELL320068B/SHA723b572f533d6af9e37fd5da23484671c36371b2ad906b827d4e95a2a6187fc5。新i386布局已实际编译为 `build/m10a2-p6-gui-coherent-layout-02/layout-proof.json`，SHA6b828bbab032c4bb8ceb00ee6a94f7b55cdf0671ec18d2cb807d73240260e80a，desk_size1332/context_size672/node_size456/root_size184。旧95 proof不改。final03只执行了新layout，没有生成CORE/SDK。

同文件重播/seek的确定源码矛盾为旧READ_STABLE仍持有时新稳定读被CONTENT冲突拒绝。`drafts/m10a2-stable-read-sharing`已按原像合入三内核文件和SC32-FILE规范：仅新稳定读Acquire/Open兼容健康idle的slot0 READ_STABLE，其它write/namespace/目标预留/取消/提交中记录仍阻挡；旧权限撤回语义不改。凭证 `drafts/m10a2-gui02-integration/stable-read-integration.json`。当前HOST10构建exec11106，源码继续冻结；并非再次生成12用户态目标。final04新接线分开记录native04的原生成HOST09与新运行CORE/HOST10，五个结构头若逐字节相同则复用实际新layout02，不改旧proof字段。普通GUI03尚未启动，不能称修后行为已通过。

## GUI02 已正常收束

`build/m10a2-p6-gui-run-02/p6-gui.json` 为 `P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING`。exec22125/QEMU2364均已退出。所有应用、Shell文件worker和音频worker正常关闭，恢复原SID1，注销普通UID/GID1000、realm0的测试SID4；不重放本轮动作。

最终一致owner账：PF13922→13922，physical_delta、attributed、unexplained均0，unexpected=[]，全部资源池及历史/字体/桌面/wait页差均0。baseline和最终均在同一次短暂停中读取完整owner/GC/资源，随后确认恢复。没有豁免GUI01的205页。GUI01原FAIL保持。

全部冻结输入及原盘保持：current_inputs_unchanged/source_disk_unchanged=true，changed_input_output_paths=[]。本轮会话盘SHA为608e4855413db026b527a18ecaa517553c379c9dd0e27028cf070b60546eaa27。它包含本轮普通用户创建的测试文件和恢复日志；原M10a1盘仍独立保留。

## 实际成功与边界

- 桌面图标：黑色半透明边缘已修复。`drafts/m10a2-desktop-alpha-runtime` 核1640不透明点RGB保持、693半透明点精确匹配真实壁纸合成、2522透明点精确透出背景；8个原图标和壁纸字节未改。
- Files2：目录树和七列详细表、普通用户目录、COPY及CUT/MOVE已实际操作。NOTE.TXT正文20B，copy-journal-audit.json和move-journal-audit.json逐帧核REC头、序号、CRC及DONE/DURABLE。COPY为stage3，MOVE为stage4；目标正文相同，移动源确实消失。窄窗口隐藏目录树是自适应布局。
- 跨窗文件操作：Files→Desktop COPY，再Desktop→Files MOVE，实际正文、目录和两份REC均一致；第三次确认前取消，源SHA/修订1564保持，/DESK前后原16项不变。drop-journal-audit.json明确 `ACTUAL_CROSS_WINDOW_COPY_MOVE_DURABLE_SOURCE_VIEW_STALE`，不能称整个Drop闭环通过。
- Notes2：旧159B notes.txt保留；新文档20B `P6 native GUI note.\n` 保存到 `/HOME/P6GUI/NOTE.TXT`，界面“已持久保存”、标题无星。只读取回SHA7def5086c7959c3579cf642bea6e2473349576f26c2c9dbba874db8ec921ec69。
- Canvas2：默认512×320ARGB图像真实绘画，CtrlZ去除笔画、CtrlY恢复；Save As保存 `/HOME/P6GUI/DRAW.SCB`，界面Native image saved、修订2/保存2、clean，文件655392B。
- Lens2：Files选DRAW.SCB按Enter走默认关联，打开同一笔画；Fit后按1显示原生100%/512×320。`p6-lens-native-pixels.png`已实际查看。
- Calculator2：实际 `(12+3)*2=30`，十六进制0x1E，最大化显示历史。复制到空Notes失败，两次粘贴均0B，不能称跨应用文本剪贴板通过。
- Palette2：真实DAC256槽和主题24角色、选择ACCENT角色6时HEX#147F9F/RGB20,127,159和色样同步；最大化settled画面完整适配。未把已知失败的文本复制重复算通过。
- Monitor2：真实进程表含PID/代数/权限过滤；冻结、CPU历史图、继续已有实际画面。FILES2为PID3/代数14；属性菜单/Enter未显示对话框，已定位立即Release错误，待修复。
- Terminal2：普通mio/HOME终端真实创建RDESK/RDWORK/RDPLUGIN目录并exit后关闭；GUI01普通id/管道未变证据复用。
- Sound2：实际文件对话框选择 `/TMP/LONG.WAV` 后播放，48000Hz/双声道/16bit、进度0:04/0:12、PCM峰值显示。短QEMU录音5.675秒，272408帧，544816个样本中542546非零，peak10080，SHAf69157e9046dd6e7a83f84e9befd1ff7aa10046590d38b357167b8deda38fcbf。录音及时停止；none后端不称宿主可听。迷你布局实际切换。EOF后重播长期显示准备，尚未定位完整根因；最终正常关闭且worker退出。

## 本轮发现且必须收束的缺陷

1. 紧凑名称框输入后裸Enter无动作，按钮成功：Files同context OPEN快捷键先于默认按钮。局部CAPTURE补丁只作用名称TextBox，保留原job/ACK。
2. Notes默认关联接收原始带引号参数却直接当路径，返回-103：严格单路径解析补丁保持其它关联入口。
3. Studio2实际打开RDSTATS.SCW、树列JSON/C/H；F5真实SCCC输出OK SCO后依赖资格检查误将SCO根内名当cwd相对名，返回-108。补明确根斜杠后仍沿原namespace/修订/alias检查。尚无本轮链接/运行/Debug成功结论。
4. Settings2首列ID为0被SCUI拒绝；已实际观察启动进程而无窗口。Installer同构造缺陷由源码确认，未无意义再启动。修复列身份从1起、provider按field取值。
5. Calculator两个键帽字形缺失：仅改为已有字形“退格”和ASCII减号，数学逻辑不变。
6. Shell TEXT1 writer未发送终止NUL，reader又拒合法NUL，与现有Broker/SCUI合同冲突。仅两个函数修复owned正文、唯一终止NUL、预算及错误清理，旧公开ABI不变。
7. Desktop源MOVE后图标130秒以上未消失，人工F5才清除。旧传输ACK早于用户选择和文件job终态；新独立SCUIDROP.H/SCSHELLDROP.H增加显式HOLD/READY和真实绑定job终态，源端只失效后重读FS，不重复删源。旧flags0立即ACK及旧公共头字节保留。新实现仍待整合、编译和实际验证。
8. Systemtools的MessageBox及错误框创建后立即Release，OPEN最后引用触发取消；应用必须持有到完成/退出。Sound的CtrlO/R/M注册大写而现代键符是小写；分别作窄修复。重播的候选失败状态需如实回显，不能凭准备文案推断worker死锁。

## 当前源码与下一步

GUI结束后Root已按精确前后SHA合入局部修复、Drop/TEXT1及共享对话框/Sound修复，凭证在 `drafts/m10a2-gui02-integration`。新HOST09已实际exit0、41产物、source变化0，完整报告SHA879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4。SCO09十应用PASS；两库因新export多写size被严格SCLD拒绝，删去该非法字段后SCO10两库PASS，12份正式NATIVE已按真实输出提升。HOST08原FAIL与所有日志保留，不改写旧报告。上述是宿主证据，尚无新字节的系统内生成和GUI结论。

Drop原交付及独立cancel/retired gate、EOF flags与receiver退役work门已合；TEXT1单独按两个顶部函数与独立lower hunk组合，没有覆盖同文件另一组修复。GCC歧义缩进行已分开，Files/Desktop补真实ScUiDialogClose导入，未放宽严格检查。下一步为12明确标签原生刷新，继而按新native字节签发CORE/SDK并做定向GUI。新SCUI/桌面私有布局需真实重新生成i386凭证，不能替换旧proof摘要。

原生刷新后续入口 `drafts/m10a2-p6-followup-refresh-v3/driver.py`，只对HOST09命名接线更新，v2及历史保持：每个真实变化label显式重复--label，必须与闭包变化集合完全相等。12项为SCUI/SCSHELL/FILES2/DESKTOP/NOTES2/STUDIO2/SETTINGS2/INSTALLER2/CALC2/PALETTE2/MONITOR2/SOUND2。85项副文件witness保持，当前待生成源和旧运行底座源分开固定。不要重写历史报告或伪造source alias。完整目标还包括Studio的SCL/四TU构建→运行→Debug2、设置预览回退、文件安装和完整SDK开发体验。

## 既有实际构建来源

HOST07报告SHA22b85409627ff02b46bc47093a4f2c9482e0229efb23b403d469892997a0e39c，exit0/41产物/源变化0。完整原生run03报告SHAf5538ba547f2ae38e00f35d35b57e2c0a55ef8282955aefbf221042122c57ddc：8普通库、21记录、27SCO、实际旧G1→新SCCC1→原生SCLD及最终SCO工具链，物理页差0。

target-refresh03报告SHAfbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f：SCUI/SCSHELL/STUDIO2实际新生成，其它逐项复用，严格reader完成，PF14036→14036。SCUI7059348b04925a4ba5f29702abd0c419a570b05008ad854908f70dae66d640b7，SCSHELL617c8bb8733d5f629f8a073d82e25ba29e8f1936530559fbcae002fee703738e，STUDIO2 2f6032fefc271c2a37c0667c8a47a70ffe81e1716442c5be3309e9f0912fb1ee。

final02实际CORE SHAaed45e2f613e556cc4687582c5b79a39a55393c394ed0be9b57c58035dbd4102，SDK485项/17389865B，MANIFEST82a839cf68c32c7cae18427a7a949e74e80005359f312e76040a29b9072cf439。凭证 `drafts/m10a2-p6-final-02-receipt`；三个实际步骤退出0，1325限定源前后不变。普通八库真实native，SCSOUND仅明确授权HOST codec例外。GUI02实际执行的正是这一发行组合。

## 保存纪律

SCW（SandCore Workspace）是工程工作区，Studio入口、过滤和SDK示例统一 `.SCW`；不加入改名历史。桌面仍为Ring3。

SCAPI.H冻结SHA81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1；原盘SHA3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64。M8封存、用户文件、旧SCX、正式产物及失败证据保留，.zcodeignore不动。main已存cce7e6e，之后修复待保存；远端尚未确认。只按具体风险验证，不重复无关矩阵、不扩磁盘副本。

修订：2026-10-16，GUI02正常关闭、一致零差资源账、真实应用结果和必要修复接力。
