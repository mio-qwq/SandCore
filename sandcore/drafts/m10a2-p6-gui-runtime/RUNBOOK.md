# 一VM正式GUI观察与输入接线

本目录只有可复用薄helper和方案，没有main改动、构建、写盘或VM执行。
当前generation VM及其脚本/输入/产物均不修改。总流程沿
`drafts/m10a2-p6-gui-flow/GUI-FLOW.md`；该文件和flow_input.py是本批交互合同。
本目录不提供坐标表，不构造外国ScUiContext，不注入Broker command，不按
SCUIDEMO的main/second角色解析正式应用，也不用F12“刷新”产品。

## 外层仍负责VM和真正普通会话

Root沿已有QemuSession(data_prepare只改本会话副本)、GuiGuest/RefreshHarness
安装经过来源校验的正式native候选/SDK和重新发行的SCSHELL/LOGIN2/CORE。
八个普通库及codec例外按已有真实来源分列；不从本目录生成替身或probe。
GUI VM设audio=True保留AC97。headless仍是none后端，不声称宿主可听。
QemuSession会自动wavcapture，boot后先audio_stop精确结束当前self-owned
index，保留短启动WAV，不删它也不关设备，避免长GUI流程一直录静音。
Sound阶段才audio_start新叶名，实际短动作后立即audio_stop。

warm之后、创建测试SID之前，保存一次：

```python
before, observations = harness.quiet()
baseline = (before, observations, harness.resources())
```

Root使用已经批准的`printf`空密码和`id; exit | su mio`真实创建普通SID，比较
sessionctl list中新UID1000/type=user行，再sessionctl show新SID。不能拿Shell
后台包装进程PID当应用PID，也不能用SYSTEM启动产品后写UID1000标签。
后续应用从该实际普通桌面Start菜单打开。密码不经下面type接口；该接口
会记录真实物理键，仅允许明确非秘密ASCII。

把当前草稿和gui-flow目录加入sys.path，接现成对象：

```python
from scui_input import ScuiInput
from flow_input import FlowInput
from verify_m10a2_ui_gui import sessions
from runtime import GuiRuntime, serve
inputs = ScuiInput(vm, guest, symbols, report['physical_input'].append)
flow = FlowInput(inputs, harness)
runtime = GuiRuntime(flow, harness, session_parser=sessions,
                     baseline=baseline, cold_font=existing_qualified_cold_font)
serve(runtime)  # JSONL stdin持续交互；外层context仍拥有VM，finish不关VM。
```

这里只使用既有ScuiInput.key/move/click/button；FlowInput.drag是真的
DOWN、越过阈值、MOVE、UP。未调用ScuiInput.snapshot/wait_logged（它们发
Demo专用F12），没有新测试按钮。普通SID身份由真实sessionctl list绑定；
CLI执行后会清除“当前已选中”绑定，下一批物理输入前再bind，避免把旧SID
资格带到切回的桌面。test_session另留作最后注销与资源账核对。

## JSONL接口

每条有唯一id。同id重复只返回旧结果、`replayed=false`，不重放业务输入；
中途异常记UNVERIFIED_OR_INPUT_UNCERTAIN。尤其MOVE/COPY/DROP超时不能
自动再发。每步真实效果需后续证据，不因输入已发送就判GUI PASS。

| op | 字段 | 行为 |
|---|---|---|
| run | command、可选expected/timeout | 现成harness.run/Guest.command；明确记外部SYSTEM CLI，不能当GUI copy/build动作 |
| bind | session | 真sessionctl list确认该UID1000普通SID当前active |
| screen | label | 现成screen；返回真实PNG路径/SHA/尺寸，拒覆盖旧证据 |
| keys | keys数组 | 真实HMP键名；如ctrl-l、ctrl-shift-s、f5；不接受F12 |
| type | text、purpose、secret=false | FlowInput非秘密ASCII；purpose为path/filename/document/symbol/terminal_command |
| click | point=[实际x,y]、screenshot={path,sha256}、可选button | 点来自本VM当前真实截图；不猜点，支持left/right |
| drag | start/end实际点、screenshot={path,sha256} | 一次真实PS/2跨窗拖动，业务不重发 |
| get | path、label | 从实际绝对guest路径读回新文件，返回字节/SHA/代数；不写客体 |
| audio_stop | 无 | audio_hmp stopcapture当前self-owned index；info capture确认只移除此index，finalized并清vm.audio_capture_id |
| audio_start | label | audio_hmp wavcapture sand-audio；确认唯一新index和本VM输出路径，不覆盖原WAV |
| finish | 无 | 确认测试SID已注销，复用一次quiet/ledger，结束控制循环而不关VM |

keys/type/click/drag必须先bind。坐标请求必须带本VM实际已取得PNG的path/SHA，
点在该真实原生尺寸内。Root从当前截图或同源只读模型选择实际点；helper
不声称“有一个坐标”就验证了控件身份。窗口移动/缩放/菜单变化后先screen，
不能复用过时点。没有实际截图的click/drag不执行。

以下只示意不含坐标的真实请求；SID由上一条真实list给出，不能抄固定编号：

```json
{"id":"files-address-focus","op":"keys","keys":["ctrl-l","ctrl-a"]}
{"id":"files-address-text","op":"type","text":"/HOME/P6GUI","purpose":"path","secret":false}
{"id":"files-address-enter","op":"keys","keys":["ret"]}
{"id":"files-current-picture","op":"screen","label":"p6-files-current"}
```

cli和harness原有打印重定向stderr，stdout只有JSON回复。异常并不自动重试，
完整运行证据仍在外层harness/console/management-rx、physical_input和截图中。

## 产品动作与必须收集的事实

1. 用Files实际菜单创建D=/HOME/P6GUI和CLIP/RETURN/DROPBACK，名字已有用户
   内容则Root选择新的本人目录。Notes真实编辑区输入`P6 native GUI note`，
   Ctrl+Shift+S；文件对话框先上方目录Enter等待，再下方NOTE.TXT/确定。
   观察COMMITTED+DURABLE、dirty清除后关闭；Files中Enter打开TXT默认关联。
   记实际触发窗口/普通SID、重新打开的Notes正文及读取的原文件字节和权限。
2. Files里右键看到真正Open with/Properties菜单，取消，再Ctrl+C→CLIP内
   Ctrl+V，Ctrl+X→RETURN内Ctrl+V。记录CUT发布后源仍在、durable MOVE后
   源消失，真实目标字节/revision和uid/gid/rw。显式Open with只验证显式路由，
   不能替代前一项Enter默认关联。
3. Files的NOTE.TXT拖到实际桌面空白，在真实Drop对话框选复制。反向从桌面
   NOTE.TXT图标拖到Files的DROPBACK空白，选移动；再做一次Drop后取消。
   desktop_path/recovery_path从实际观察获得，不能假定/Desktop或预设日志名。
   `evidence.journal`按真正SCSFJ1/CRC/nonce/worker连续帧解析任意本次长度，
   不沿用旧131079B/三frame夹具。transfer只在DONE+DURABLE与对应真实文件
   字节/revision一致时返回一致；DROP Complete或图标出现单独不算持久提交。
   CRC日志不是密码学授权；实际worker身份若未外部观察应明确标出。
4. Studio点工程→打开工程（无默认键，Ctrl+O仅文档），先RDSTATS.SCP再
   RDESK.SCP，均在/SYS/SDK/EXAMPLES/RDESK。分别F5一次：独立SCL有1个TU，
   RDESK为MAIN.C/MODEL.C/VIEW.C/IO.C四TU和一次SCLD --dynamic @response。
   保存真实输出正文/诊断及当前project epoch/build generation/发布三份
   durable结果，不能以已有SCX判断当前成功。evidence.studio_build只校
   真实命令和观察到的发布资格，缺字段保持UNVERIFIED。
5. 当前构建发布完成才F6。看实际RDESK树、双文档、四行/图，rows4/sum40/
   mean10和实际同目录RDSTATS.SCL；GUI事实先留截图。正常关闭后等待Studio
   捕获 stdout的RDESK_ANALYSIS及job结束/两端EOF/close，不能发F12刷缓冲。
6. F7一次即可由Studio启动Debug2 --launch；不再Ctrl+F5另建目标。入口暂停
   可能尚未加载全部SCL，不能要求此时RDSTATS已经出现。由实际SHA绑定的
   Symbols取得main（歧义用Module!Symbol），Address→F9→F5至断点→F8。
   寄存器末行显示target PID、process generation和binding，STOP/status显示
   暂停代数。记录同一target、新stop_generation、真实reason=single step与
   各pane同一发布代数；evidence.debug_step不把自跳同EIP误判成未执行。
7. 为证明真正SCL执行，选择已实际加载的SCUI等模块，在“Verified runtime
   module properties”取module/load-generation、base、ABI、源revision和完整
   SHA。用evidence.scl_module对照本VM那份真正native SCL。再从实际Symbols
   选择随后确实会调用的导出（据当前程序源/真实状态决定），设第二断点并
   继续到该SCL，观察EIP在真实可执行段及新暂停。RDSTATS若尚未加载，只能
   先让程序走到它的真实加载/调用后再观察，不能用app.map或路径扫描伪造
   runtime模块。模块属性/Symbols可见证据足够时不另造probe。

正式应用没有通用跨进程SCUI树枚举。必要的私有事实只能由Root用当前同源
布局/map做既有外部只读观察，或从真实产品UI读取；这里不构建新布局工具，
不生成硬编码偏移。缺snapshot/ACK/模块/发布资格则保留UNVERIFIED，不补零。

少量截图：Notes关联再开、Drop接收/终态、Studio四TU输出与RDESK图表、Debug
main断点/单步和真正SCL模块。图像不拼接；隐藏/关闭/领完任务后，Root正常
恢复原桌面并sessionctl logout测试SID，finish才核一次owner/resources账。
原盘保持、当前输入保持和完整GUI验收状态仍由外层Root统一记录。

Sound只用原盘现有资源，不造音频夹具：/TMP/LONG.WAV足以代表10秒跳转，
/SYS/TEST/TONE.MP3、/TMP/ALBUM.MP3、/TMP/ALBUM.FLAC、/TMP/STEREO.FLAC
已有Root只读清单（uid0/gid0/mode23）。按原目标取WAV/MP3/FLAC真实播放、
一次代表seek/正常后台和短PCM捕获，不扩编码矩阵。PCM字节/帧/SHA只证明
实际混音输出，seek/音频内容/GUI状态另核，none后端不等于宿主扬声器可听。
scserial不改。

修订：2026-10-16，只写正式GUI的持续JSON输入helper/证据解析/接线方案；未构建、未启动VM、无GUI PASS。
