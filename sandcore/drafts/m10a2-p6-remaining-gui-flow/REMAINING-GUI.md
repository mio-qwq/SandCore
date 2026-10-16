# 剩余原生GUI紧凑操作单

只读准备，未执行。使用当前同一VM及HMP输入/截图/owner账，不另开VM、不造资源夹具、不扩主题/DPI矩阵。D表示Root在当前轮已创建的可写目录；NOTE.TXT为先前Notes正常持久保存的文件。本页的DRAW.SCB与INST.TXT使用D内尚不存在的叶名。

共享FileDialog：上方目录框输入目录→Enter→等目录加载；下方名称框输入叶名→“确定”。不要把完整文件路径填上方后当作已打开。键盘写法为用户键名；沿当前driver发HMP，例如Ctrl+Shift+S是ctrl-shift-s，Ctrl+Space是ctrl-spc。点击以当前真实画面为准，缩放/模式变化后重新取坐标。

| 应用/入口 | 只做这1–2步 | 应看到的真实结果 | 正常退出 |
|---|---|---|---|
| **Canvas 2** /APPS/CANVAS2.SCX | ① Ctrl+N（File → New 512 x 320），按B选Brush，在画布中间拖一笔，Ctrl+Z再Ctrl+Y。② Ctrl+Shift+S（File → Save as SCB2...），保存D/DRAW.SCB，等操作结束。 | 第一笔撤去再恢复；右侧仍512×320，Undo/Redo记录对应变化。保存完成状态为Native image saved，标题不再有未保存星号；D中存在实际SCB2文件。 | Ctrl+Q（File → Exit），等保存/operation释放。若还有脏图，真实Unsaved image中按本轮意图Save或Discard，不杀进程。 |
| **Lens 2** /APPS/LENS2.SCX | ① Ctrl+O（File → Open image...）打开刚才D/DRAW.SCB。② F（View → Fit image）再1（View → 100% physical pixels）。 | 真正显示同一笔迹，右侧512×320；Fit与100%视口变化，原图尺寸不变。可直接把该画面作为Canvas保存后再开的证据。 | Ctrl+Q（File → Exit）；若有正在读取，等取消/资源释放完成。 |
| **Settings 2** /APPS/SETTINGS2.SCX | ① 默认“显示”页先记当前模式/百分比，点不同的“缩放 100%/150%/200%”之一，再点“预览”。② 点Revert。 | 当前行显示预览剩余秒数且UI真实变换；Revert后模式与百分比恢复原值，倒计时结束。不点Keep，不保存账户配置。 | Ctrl+W（文件 → 关闭）。必须先完成Revert；若本轮确有其它脏草稿，按真实关闭确认处理。 |
| **Monitor 2** /APPS/MONITOR2.SCX | ① 在默认进程表选本人普通进程，进程 → 属性（或表中Enter），核PID/代数后关属性框。② 查看 → CPU历史图，等约两次真实采样；Ctrl+Space冻结，再Ctrl+Space继续。 | 属性显示匹配的PID+generation；历史图来自实际PIT样本，冻结期间不新增点，继续后重建采样基点。未知字段保持不可用，100Hz不是FPS。不用“终止”。 | Ctrl+W（查看 → 关闭）。 |
| **Calculator 2** /APPS/CALC2.SCX | ① 点击表达式框，Ctrl+A，输入(12+3)*2，Enter（计算 → 计算）。② 点编辑 → 复制结果，回已有Notes草稿Ctrl+V。 | 十进制30、十六进制0x1E及对应历史；Notes得到30。复制结果用菜单：表达式框中的Ctrl+C属于文本编辑，会复制输入选区。 | Ctrl+W（计算 → 关闭）。Notes接收草稿按原Notes关闭流程处理。 |
| **Palette 2** /APPS/PALETTE2.SCX | ① 查看 → 原始DAC 256槽，选一行看HEX/RGB/登记状态。② 查看 → 主题24角色，选一角色，复制 → 复制HEX，再粘贴已有Notes草稿。 | 来源标签和表内容明确切换硬件256槽/主题24角色，色样及HEX一致；Notes得到所选角色的实际HEX。1–104说明不是硬件总槽数。不点“程序壁纸”。 | Ctrl+W（查看 → 关闭）。 |
| **Sound 2** /APPS/SOUND2.SCX | ① 文件 → 打开…（Ctrl+O），用当前盘已有可读音频；无长曲目时可用系统现有/SYS/SOUND/START.WAV。确认真实格式/声道/采样率/时间，播放后用“播放 → 播放 / 暂停”（空格）。② 将“跳转至秒”填0并点“跳转”；若所选长曲目有足够时长，也可点播放 → 前进 10 秒。 | 真实worker就绪、播放/暂停及位置回到起点；短系统音效不要求能跳到10秒。仅设备PCM/已有短audio capture有证据才记音频输出，不把none后端写为可听。 | 文件 → 退出，等待窗口关闭及worker QUIT/真实结束。若仍处准备中，先播放 → 取消准备（工具栏“取消”），等终态；不要用Terminate替代正常退出。 |
| **Session 2** /APPS/SESSION2.SCX | ① 最后执行：Sessions → Lock desktop（Ctrl+Shift+L），进入本人SID的真实LOGIN_PANEL。② 通过Username/Password专用控件，用Root已有普通凭据点Sign in；记录新普通SID及原SID仍存在。 | 密码走Secret输入；认证成功创建独立普通桌面，不能记成原SID解锁。原会话后台保留。密码不写本页、不进入普通type_ascii或物理输入日志。 | 经现有Sessions → Switch desktop回到原测试SID，再Account → Close（Ctrl+Q）关闭其Session2；新建普通SID沿当前harness正常注销账收束。若本轮同源LOGIN2认证/SID已有有效证据，直接复用：这里只补F5（Sessions → Refresh sessions）、选本人行→Properties...后Ctrl+Q，不再重复登录。 |
| **文件安装** /APPS/INSTALLER2.SCX | ① Ctrl+O（文件 → 添加源文件）选D/NOTE.TXT；“目标”框填D/INST.TXT，保rw=311、UID/GID=-2，父目录/已有目录改权限两框不勾。② 文件 → 只读预检，等“预检通过”，再文件 → 开始安装；结束后操作 → 详情。 | 只有一项；结果为“完成”，详情DURABLE=1，目标字节数/权限及文件正文与源相符，源NOTE.TXT仍在。仅“正文已持久/权限待完成”不是完整安装成功。目标存在则记录保留/拒绝，不改成覆盖测试。 | Ctrl+W（文件 → 关闭）。如仍工作中，Esc（操作 → 取消）后等真实终态，再关闭；已持久项保持。 |

优先顺序：Canvas→Lens共用同一份正常作品；Calc/Palette共用已有Notes草稿；Settings回退完成后再取后续坐标；Monitor与安装只处理本人数据；Sound收完worker；Session最后做，避免切走其它应用。无须为每个应用留一张截图，Canvas/Lens保存后再开、Monitor冻结或音频真实状态等有意义画面即可。

## 直接复用的证据与边界

- Root已确认HOST07和3目标宿主后端08通过；其它未改应用/库的真实原生生成沿build/m10a2-p6-guest-generation-run-03/p6-generation.json及Root当前refresh来源复用。这里不再重编、不把编译结果算GUI动作通过。
- 标准控件/字体/缩放/隐藏提交与退出基础账沿evidence/m10a2-p4-context-and-p5-generation/index.json的未变范围复用；条件文件事务与clipboard分别沿evidence/m10a2-p5-file-jobs/和m10a2-p5-file-clipboard/复用，不再新造权限/冲突矩阵。Canvas、Settings及安装GUI本轮操作结果仍需独立真实事实。
- Files↔desktop Drop、Notes保存关联再开、Studio的RDSTATS.SCW/RDESK.SCW构建和Debug流程沿Root当前GUI账单，已获得有效证据的动作不重做；本页不改那些路径或助手。
- 结束复用当前harness quiet/resources/ledger与正常注销流程。等待真实窗口/worker/票据终态，不以截图消失替代owner账，不另做异常矩阵。

修订记录：2026-10-16，依据当前主源的菜单/快捷键只读整理；本页无执行/构建/客体验证结果。
