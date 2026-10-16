# systemtools 通知引用与 Sound 快捷键

Monitor 的真实属性路径是 `mon_details → tool_message → ScUiMessageBox`；当前公开 SCSHELL 没有 `ScShellNotify`。程序查询成功后，原 helper 创建 OPEN 的 MessageBox，再立即 Release 唯一 caller 引用。SCUI `ui_dialog.c:311` 明确在最后引用且 OPEN 时主动取消，之后销毁 dialog/root。因此它可以返回 0，却在第一个属性框客户帧之前自行关闭；这条确定的生命周期错误不需要用 owner 或表列解释。

`tool_error` 同样错误：`ScShellErrorDialog` 只是格式化错误后创建一个 SCUI MessageBox，不持有额外引用，也没有传入完成回调；原 helper 仍立即 Release。窄修复不改共享库的所有权语义：

- ToolWindow 私有状态增加一个 notice 引用。
- 普通通知创建成功后持有引用，完成回调先清指针再 Release；已有未关闭通知返回 BUSY，不能覆盖原指针。
- tool_close 先取走并清空 notice，再最后 Release；SCUI 会按原语义取消仍 OPEN 的框，回调不会二次释放。
- 错误通知使用同一个应用持有的 SCUI 路径，仍保留操作名、code/cause/domain/raw 及 stderr 原业务错误。没有复制 SCSHELL 私有映射表或增添库接口。

因不再调用 `ScShellErrorDialog`，4 份 HOST 模板只移除这个导入，没有新增导入。NATIVE manifest 不手改，交 Root 根据真实 SCO 重生。

真实 native 生成报告的引用闭包确认 CALC2、PALETTE2、MONITOR2、SOUND2 都包含同一 `systemtools/TOOLS.H + tools_ui.inc`；它们是同一批精确改动的实际刷新标签。没有修改 Settings/Installer、已有 Monitor 终止确认框（它本来持有独立 confirm 引用）或 SCUI 库。Palette 最大化后的 settled 图已经正确，本稿没有任何 resize 补丁。

Sound 的 3 个快捷键另列 `sound_shortcuts.patch`：`'O'/'R'/'M' → 'o'/'r'/'m'`。键盘现代事件 `symbol=map_base[code]`，wm 和 SCUI 原样传递，`ui_shortcut` 精确比较 key，不做大小写归一。原大写注册值不会匹配 Ctrl+o/r/m；CtrlO 的实际无动作已经由 Root 留证。文案仍显示 Ctrl+O/Ctrl+R/Ctrl+M；按钮及其它命令均不变。

补丁为 `lifetime.patch`（共享私有头/实现与4份HOST模板）及 `sound_shortcuts.patch`（Sound菜单3字母），全部 before/result SHA 在 `PINS.json`。`prepare.py` 只生成本目录文本并核对 GUI02/真实 native 闭包；没有主树、旧草稿、构建或 VM 修改。

本次仅源码审阅、真实闭包/原像与文本检查，没有修后客体 PASS。Root 后续最小必要回归是 Monitor 已选同一进程的属性框真正留存并可关闭，再触发一次正常消息/错误确认引用能回收；Sound 用已有文件与 Ctrl+O/迷你等实际操作按当前流程继续，不扩矩阵。
