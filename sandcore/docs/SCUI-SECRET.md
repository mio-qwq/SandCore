# SCUI Secret v1

2026-10-16。SCUISECRET.H、SCUI.SCL ABI1.3/v4，新增标准控件kind27；旧SC_UI_PASSWORD只掩码的ABI不改变。Secret拥有专用用户页，没有UiDocument/piece/undo/String正文。普通CopyText/GetValue/SetValue/SetText拒绝明文通道，Accessible只报告密码属性与空value，所有通知只带长度/代数。

输入1..4096B UTF-8（Session使用128B），按码点导航/删除、选择、Ctrl+A、鼠标定位和键盘Tab/Enter，Ctrl+C/X/V/Z/Y不进入剪贴板或历史。所有正文移位在专页内完成，删除立即擦尾；四字节UTF-8临时区volatile清零。键盘/文本在普通监听器前消费，导航事件剥离原message/scalar；SCUI自己拥有的消息栈在分派后擦除。星号渲染和glyph缓存不含正文。

Consume同步借用const data/bytes，保证末尾NUL，只允许回调内使用。无论回调成功、失败、请求清除或销毁节点，返回前擦活动页与候选页。callback由运行时pin，调用持有node/context并拒重入。应用不得把借入正文另存文件、日志、命令行、剪贴板、String或持久缓存；同一进程内的恶意代码不在控件提供的隔离边界内。

绑定SignIn按钮后，失焦到该按钮时活动字段立即清空，正文转入独立sealed专页候选；只在按钮真实Command时消费。鼠标DOWN不提交，UP外/失捕获/其它焦点/隐藏/原生失焦/两秒期限撤候选；键盘Enter与按钮Space/Enter同路。未绑定按钮的失焦直接清空。小窗使用标准Scroll/EnsureVisible，Secret不私绘应用窗口。

撤销/超时设置INVALIDATED；迟到的按钮UP/Command不会把已撤候选当作新空密码提交。重新进入字段或输入新字符后恢复新提交资格；正常Consume完成即清除事务标记。

Clear、正常Consume、失焦、隐藏、禁用、销毁、quiet fini及可控失败都volatile擦专页后释放。强制终止不跑用户析构，已有分页保证再次映射给用户前清零；不宣称立即擦除所有物理RAM。按主代理明确范围，本轮没有敏感页gate、分页修改或全任务退出改造。

128B两区约8KiB，4096B两区约16KiB，O(字段字节数)编辑。光标DRAW_ONLY、sealed期限短后台timer，隐藏不持续绘制。未测实际输入长尾/CPU，不以宿主断言冒充guest性能/视觉验收。

修订：2026-10-16，新增共享Secret及提交事务合同。
