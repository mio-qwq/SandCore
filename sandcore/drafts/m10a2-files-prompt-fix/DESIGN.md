# Files 2 名称提示的局部修复草稿

本组只准备源码补丁，main、现有 GUI helper、冻结草稿不变；不构建、不启动 VM。依据为真实截图 build/m10a2-p6-gui-run-01/p6-native-files-mkdir-dialog.png 与当前源文件。

Files 2 的 SC_SHELL_COMMAND_NEW_DIRECTORY / RENAME 交给 ScShellMenuExecute。SCSHELL shell_menu.c 的 sh_menu_name_prompt 用 ScUiPropertySheet 创建一个文本字段，且 field.page=1。SCUI 正常创建 560×390 逻辑尺寸、多页签、确定/取消/应用的属性表；页名由“设置 %u”和 page+1 得到“设置 2”。因此本组修正 SCSHELL 的调用方式，不修改 SCUI 公共接口、布局或 PropertySheet。

替换为公开 ScUiWindowCreate(CLOSE|MODAL) 创建的标准模态名称提示，由公开 Label / Textbox / Button 组成。公开 SCUI 尚无通用 ScUiDialogCreate；不伪造 ScUiDialog、不读取私有结构。根窗口320×170逻辑单位，按该 SCUI context 的公开 Metrics 缩放，居中于真实 owner 并限制到显示器范围。200%时客户区640×340像素，100/150%分别320×170、480×255；标准控件和配色仍由 SCUI 绘制。仅名称栏、输入框、紧凑的规则/错误提示与右下动作按钮，不设页签或应用按钮。

标题/主要按钮为“新建文件夹/新建”和“重命名/重命名”；次按钮为“取消”。初始文件夹名为“新建文件夹”（旧英文默认含空格，而当前文件系统拒绝空格）。重命名仍取真实 source basename，冲突改名仍取 conflict destination basename。文本框默认获得焦点并全选；Tab/Shift+Tab和默认按钮复用 SCUI，Esc/关闭只取消。名称仍最多63个UTF8字节，完整路径仍由同一 ScPathJoin/原文件服务验证，不宣称支持空格或改变文件权限。

事件回调只保存待确认/取消意图；现有 ScShellMenuStep 消费一次，不做输入回调内的磁盘事务，也不引入计时器或忙轮询。非法输入保留窗口、原输入和错误提示。校验完毕后，必须先销名称窗口再进入原 managed job / conflict resolve：内核同一 owner 只能有一个 modal child，不能让原进度框在名称窗仍占 modal slot 时创建。提交启动若失败且没有存活 job，重新显示同一名称与错误，不暗报成功。

ScShellMenuRelease 对新 root 先 Disconnect 再 Destroy，安静 fini 保持原有 SCUI quiet 清理约定；owner销毁时的 DESTROY 清理节点值。所有字段都在 SCSHELL 私有结构，不改公开ABI/导出。现有 Properties、删除确认、文件任务/恢复记录/DURABLE ACK逻辑保留。

Root整合后的最小检查：仅重生成 SCSHELL native库/manifest并用当前同一GUI会话，Ctrl+Shift+N检查中文紧凑提示；输入无空格目录名、Enter并核真正持久结果；F2改名后Esc不修改，再F2确认并核目录列表；尝试空格名称保持提示并能改正；原Properties仍显示完整内容。新草稿本身没有 HOST/native/guest PASS。

修订记录：2026-10-16，定位真实 PropertySheet 误用并先写本局部设计，待Root整合和客体操作。
