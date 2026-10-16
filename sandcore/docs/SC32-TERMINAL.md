# SC32 Terminal 与 SCUI Terminal 公开合同 v1

2026-10-16。新增公开头 `SC32TERMINAL.H`、`SCUITERM.H`；SC32API统一1.4/v6，独立SCTERM.SCL为1.0/v1。内核不引入libc或浮点，不修改SCAPI.H。

## PTY与旧CLI

`ScProcessSpawnInteractive(command, desc, out)` 原子建立PTY并启动**一个**实际受管程序。默认Terminal2传 `/BIN/SH.SCX -i`，后续用户输入送这个shell，环境/变量/管道/重定向/前台票据均属于它。允许显式 `--command /absolute/PROGRAM.SCX arguments` 启动单个交互程序；既有首路径无空白、rootless63B边界保持。

`ScTerminalDesc` 32B：size/version、columns、rows、flags、reserved[3]。columns为1..512、rows为1..256，flags及保留字段0。`ScInteractiveInfo` 32B：size/version、job、master、flags、reserved[3]。master是双向现代ScFile，flags固定RAW=1、APPLICATION_ECHO=2、INTERRUPT_EVENTS=4。不提供canonical行编辑、自动echo、POSIX termios/pgrp/信号集；当前兼容模型由旧shell真实订阅控制事件并按真实job PID/代数取消前台。

master READ取得按实际写入顺序合并的stdout/stderr，WRITE送原始stdin；4096B输入/输出环分别有背压，遵守ScFile短读写、AGAIN、EOF、broken pipe。可以ScFileDuplicate，不占父旧16fd；最后master引用才挂断。slave在旧孩子标准0/1/2，另有方向0的控制引用；它不会成为隐藏writer保活EOF。即使标准流全部重定向，nano仍可用旧STREAM_TERMINAL取得同一个控制slave。连接能力不跨登录session传播，不伪装物理serial或SYSTEM管理口。

`ScTerminalQuery(master, out)` 返回64B：头、kind=8、columns、rows、geometry_revision、input_bytes、output_bytes、flags、interrupt_revision、slave_readers、slave_writers、reserved[4]。kind8/9分别为新PTY master/slave，旧4/5不改。`ScTerminalResize` 只改真实尺寸/几何代数并唤醒，绝不增加中断代数。普通前台nano通过旧terminal_info看到kind9及相同尺寸。

`ScTerminalControl(master, action)`：INTERRUPT=1、EOF=2、HANGUP=3。中断在slave有真实event订阅时递增中断代数并清空内核积压输入；无订阅时发送原始3字节。EOF有订阅时产生一次可消费EOF，无订阅时发原始4字节。HANGUP持久关闭；已排队输出仍可排空，slave输入随后EOF、输出broken pipe。父退出先撤master/native/控制引用，再沿实际受管jobs回收孩子和后代。GUI Ctrl-C控制走独立gate，越过大段待发粘贴，并撤自身尚未发送的输入。

旧 `terminal_clear(slave)` 以整个7B `ESC[2J ESC[H` 原子写到同一输出环；空间不足返回AGAIN，不改画面或偷写内核WM画布。所有用户记录先完整验输入/输出映射再分配或spawn；未知输出尾部保留，保留字段拒绝非零。selector为SYS300 20C..20F，ErrorInfo operation1600..1603。master/query/resize/control均须本人ScFile/native引用，真实PIDgen与同session重新核验。

## 共享终端模型与控件

`ScUiTerminalCreate(context, desc, &model, &node)` 创建真正SCUI自定义类节点。desc指定parent、初始网格、history_rows（0..32768）、input_budget_bytes（16..1048576）、font_role和CURSOR|WRAP标志；其它保留字段0。节点持有模型引用，调用者另持一份，退出须Release。直接NodeDestroy、父销毁或ContextRelease都会撤节点关联；已Retain的模型可读Query/Copy，不能把失效node/context用于绘制。Class只经公开SCUI注册与Painter工作，不复制私有框架对象。

Feed接受任意字节块并报告accepted前缀，包括错误时已处理的前缀；调用者不可重送。UTF8跨块暂存单个标量，坏序列显示替代符并累计invalid_utf8，Finish把EOF时未完成标量显示为一次替代符。中文宽度按同源凤凰字体真实measure换算成网格1/2列，组合标量附于原cell，continuation不重复复制。宽字遇最后单列且禁止wrap时显示替代符，不能越过cell数组。Resize先造完整两屏候选，失败保留原网格/历史/光标；因缩小需移出的顶部行进入历史，保住当前提示行。历史有明确行数上限，满后复用最旧行。

当前ANSI集合：C0 CR/LF/BS/TAB/BEL，ESC D/E/M/7/8/c；CSI A/B/C/D/E/F/G/H/f/d、J/K/X、@/P、L/M、S/T、r、s/u、m、h/l、n、c。支持页边距、origin/insert/wrap、cursor显示、应用箭头、47/1047/1049替代屏、2004括号粘贴和状态/光标回应。LF默认同时回到行首，保SandCore旧shell/nano帧语义。SGR支持16色、亮色、underline、inverse、bold亮色；映射既有SC32主题角色，不宣称精确xterm RGB、256色/真彩、字体粗体塑形、sixel或完整VT兼容。38/48扩展色参数作为整体安全消费，不能把内部数字误解释成reset。OSC只接受0/2 UTF8标题，不执行OSC52剪贴板或命令。

键盘Text按UTF8编码；方向/Home/End/Insert/Delete/PgUp/PgDn/F1..F12发ANSI/VT220序列，Ctrl组合发控制字节，Alt发ESC前缀。Ctrl+Shift+C/V使用公共剪贴板；其余桌面快捷键继续交SCUI命令表。鼠标选择、公共capture丢失取消拖动，滚轮/滚动条、Shift+PgUp/PgDn、Ctrl+Shift+Home/End浏览历史。Copy不重复宽字续格，Paste验证完整UTF8与唯一末尾NUL，并在开启2004时原子封装。

TakeInput仅交出模型生成的字节，传输和部分写重试由调用应用拥有。库无子进程/文件/PID控制权，因此可供其它传输复用。可见区域才绘制，glyph/宽度按字体、字号/缩放代数失效；没有cursor blink定时帧。隐藏Tab的0×0布局保留上次有效网格，后台字节继续解析，显示时重新布局；窗口真正缩小仍准确同步PTY尺寸。

## nano小尺寸补丁

入口原条件没有kind白名单，kind9直接走已有ANSI键分支。新增适配只处理kind9的小于16×5网格：暂停绘制/普通编辑键，保gap、Undo、revision与dirty；真实PTY尺寸不伪报。恢复尺寸后继续。Ctrl-X可明确退出，脏文档随后Y保存已命名文件/N丢弃/Esc取消；未命名文档要恢复尺寸后选路径。旧4/5的限制、私有码、串口能力和旧SCX原字节保持。

本规格随实现交Root整合；宿主模型与机器码生成不等于客体GUI或CLI已验收。
