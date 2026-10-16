# 名称输入框 Enter 的局部修复

此组只准备SCSHELL单文件、两个局部hunk；main、旧草稿、当前GUI02及其helpers/pins均不改。不编译、不启动VM，没有修复后客体验证结果。

根因是同一SCUI context的快捷键优先于root默认按钮，不是ScUiButtonSetDefault没有保存按钮：

1. Files2 files2_view.inc:38–43经ScUiCommandSet登记快捷键；第80行把裸Enter绑定OPEN（0x7100）。登记表属于context，不按当前模态root过滤。
2. 名称TextBox的ui_edit_event对Enter KEY_DOWN返回0；现有sh_menu_name_event也没有处理Enter。TextBox会过滤后续TEXT中的CR/LF，不是名字被换行污染。
3. ui_dispatch.c:621先用ui_shortcut查context登记表，命中后执行OPEN。第622行的default_button只在else分支，因此不会再回退到名称窗默认按钮。
4. OPEN从prompt_name派发，路径只沿独立prompt树；该树只接OK/CANCEL，没有OPEN处理者。ScUiCommand仍把非负分发结果变为OK，所以出现无动作，而非明确错误。

Root已报告GUI02在真实Enter后名称窗保持、之后点击“新建”继续。本轮p6-gui.json中files-mkdir-name为5B物理键入，files-mkdir-confirm为ret，二者时间相隔1.125秒；日志本身标记effect待证，不把它当成客体事件追踪。上述静态优先级链与实际表现一致，不能归因于缺少额外等待，也没有声称直接记录到了0x7100客体事件。

修复只给该prompt根listener增加CAPTURE phase，并在CAPTURE分支严格要求：root为本prompt、node为prompt_name、KEY_DOWN、无Shift/Ctrl/Alt、键为Enter或Esc。命中后锁存原有OK/CANCEL意图并返回HANDLED；取消仍优先，不在回调内开始文件事务。其它capture一律返回未处理，原TARGET/BUBBLE的按钮、关闭、文字修改及其它控件行为保持。CAPS/NUM不影响裸Enter。

ScUiButtonSetDefault继续负责按钮外观与其它原有回退。Tab、编辑快捷键、按钮Space/Enter、文件名校验、冲突epoch/revision、MenuStep一次消费、先销modal再建进度、原DURABLE ACK均不改。不新增公开API/导入，不修改共享SCUI或全局快捷键。

交付为name-enter.patch、PINS.json及result/shell_name_prompt.inc。PINS固定精确before/result SHA，SOURCE-PINS固定只读因果链。prepare.py只读主树并输出本目录；前像不同则拒绝，不能用旧整文件覆盖后续更改。

Root整合后仅刷新实际受影响SCSHELL。最小回归在当前统一GUI批次：Ctrl+Shift+N→输入一个新叶名→真实Enter→核原文件任务持久结果；再开名称窗→Esc取消。按钮点击已有有效证据可复用，不再扩大控件/组合矩阵。

修订记录：2026-10-16，修正名称Textbox对局部Enter/Esc的事件归属；源码草稿，未构建、未客体验证。
