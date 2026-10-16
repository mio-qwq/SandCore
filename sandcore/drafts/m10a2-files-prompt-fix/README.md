# 名称提示交接

本目录只准备隔离的局部源码补丁，未修改main，未运行构建、VM或输入操作。真实失败画面来自 Root 已运行的 build/m10a2-p6-gui-run-01/p6-native-files-mkdir-dialog.png；本组没有修复后截图或 native PASS。

根因和设计见 DESIGN.md。Files 2 的 Ctrl+Shift+N 真实调用经过 files2_actions.inc 的 f2_command → ScShellMenuExecute → shell_menu.c 的 sh_menu_name_prompt → ScUiPropertySheet。field.page=1，在 SCUI ui_dialog.c 的页签默认规则中成为“设置 2”；PropertySheet 固定560×390逻辑尺寸及确定/取消/应用三按钮。因此本补丁只纠正共享SCSHELL调用，不改SCUI、不绕到Files2私有控件。

files-name-prompt.patch 是完整交接；PINS.json 是4个现有文件的真实字节前像/后像SHA和1个新片段的SHA。overlay只供逐行审读，Root应按精确前像及局部hunk整合，不能覆盖其它后续改动。make_patch.py只读主树并输出本目录；前像变化即拒绝。它没有应用补丁的模式。

补丁范围：

- user/scshell/SCSHELLPRIVATE.H：把私有名称提示改为标准modal root与节点/意图值，不改公开结构。
- user/scshell/shell_menu.c：引用新片段；从Textbox复制真实当前文本；保原名称/路径校验、RENAME/MKDIR及冲突epoch/revision；在进入原任务前释放modal slot；非法输入保留提示；释放顺序断开listener后销root。
- 新 user/scshell/shell_name_prompt.inc：320×170逻辑模态窗口，中文名称/按钮/提示；标准Label/Textbox/Button，owner居中，SCUI Metrics缩放，Tab/Enter/Esc/关闭及焦点全选；不自绘控件、不用裸色、不加timer。
- user/scshell/SCSHELL.json：移除本库已不再使用的 ScUiPropertySheet/ScUiDialogCopyValues 两项导入，加入真实 ScUiMetricsQuery/ScUiEditSelectAll 两项；96导出、165导入，库ABI/版本/依赖不变。NATIVE.JSON必须由Root按新实际SCO重生成，不手工放宽unused验证。
- docs/SCSHELL-DESIGN.md：追加同批合同及明确未验状态；原Properties元数据/权限控件保持原样。

本次只做读取、补丁生成、Python AST与JSON结构静态检查，未编译C。PINS中所有前像在生成后再次读取比对；SCUI.H、ui_dialog.c、shell_dialog.c、Files2源和截图作为只读定位依据写入 SOURCE-PINS.json。修复后所有键盘、尺寸、owner关闭和磁盘行为仍由Root当前native GUI批次确认。

建议Root最小回归同一次GUI会话：新建提示的中文/无tab/无Apply和200%布局；全选输入合法无空格名→Enter→原任务真正DURABLE ACK与目录刷新；F2→Esc不改名，再F2确认改名；空格/空名保留输入且可修正；原Properties仍有完整页。不要为本补丁新造单独VM或组件矩阵。

修订记录：2026-10-16，基于真实native截图定位共享Shell的PropertySheet误用，隔离修复源码交齐；未构建、未客体验证。
