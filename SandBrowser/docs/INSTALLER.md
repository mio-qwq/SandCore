# 安装器和载荷

作者：mio。实现位于 src/installer.c、tools/package.py。

ZIP 中仅 SETUP.SCX 与 BROWSER.PAK。解压到同一目录后，只运行 SETUP.SCX。私有运行库、应用和资源都在 PAK 内；安装器只依赖系统 SC32API/SCCLIB。

向导提供欢迎、组件/许可说明、目标路径与快捷方式、真实进度、结果与 Launch。原创地球经纬线、移动网络包与节点动画的主题文字为：

> welcome to the World Wide Web !

动画与进度独立：进度依据解压校验并实际写入的字节数，动画不触发假完成。

0.1.1 修复了 0.1.0 没有提交矩形导致窗口正文空白的问题。完整首帧先于载荷读取；每帧都执行 SC32 绘制事务，失败撤销 claim 并保留待重绘状态，错误码写入窗口标题和标准错误。隐藏/最小化时暂停上传，恢复后重画。

按钮仅由左键按下并在原按钮内松开触发，移出/失去捕获会取消；不可用按钮不会执行。Tab/Shift+Tab 切换焦点，Enter/Space 执行，Alt+B/N/C 返回/前进/取消。路径框提供光标、Home/End、左右移动、Delete/Backspace 和 Ctrl+A。错误页提供 Retry，路径和快捷方式选项保留。

## PAK v1

整数为 little-endian u32。头为 8 字节 SBPACK1\0、version=1、文件数、解压总字节数。每条为 name[32]、原长度、压缩长度、CRC32，紧跟 zlib-deflate 数据。文件名限大写 ASCII 数字字母及 _ . -，禁止路径和 ..；记录须唯一且完整覆盖输入。

先校验目录、长度、数量和总和，再按文件解压和 CRC 校验。CRC 检测损坏，不是发行签名。SHA-256 清单另在安装包和构建报告提供。

## 提交与恢复

目标为绝对路径，父目录已存在，长度 3–36 个 ASCII 字符，默认 $HOME/SBROWSE。写到旁边 .NEW 目录；更新时保留 FAVS.TXT、HISTORY.TXT、OPTIONS.TXT。备份优先使用 .OLD，已有旧备份则选择空闲的 .O01… .O99，不会覆盖旧备份；结果页显示实际备份位置。.NEW 已存在时保留现场并停止，不自动删除中断安装的文件。

每个文件写入/关闭后核对类型和实际长度，成功才累计进度。解压/写入失败或取消，只清理本次暂存文件，包含可能的部分写入。清理失败会要求检查 .NEW。提交失败尝试恢复旧目录，恢复失败要求从选定的备份路径恢复；不宣称断电级事务保证。旧版其他个人文件保留在备份中。

可选创建 /DESK/SBROWSE.LNK，第三行写入安装目录下 BROWSER.SCB 的绝对路径。图标是原创 32×32 ARGB SCB2 资源，包含经典窗口与地球。0.1.2 可识别旧安装器生成的同一命令、空图标的快捷方式并修复图标；其他已有同名快捷方式保留，无写权限会报告。不会改全局 PATH 或系统菜单。卸载管理界面尚未实现。

Help → About 显示作者 mio、版本和真实编译时间；关于、帮助和许可均可离线打开。完整许可从实际使用的源码与许可文本生成。

宿主回归检查调用实际窗口提交与安装器事件代码，覆盖提交矩形、失败撤销、尺寸变化、最小化/恢复、左键按钮、禁止动作、路径编辑、关闭取消与错误重试。运行方法：在 WSL 的本目录用 gcc 编译 tools/test_ui_contract.c 和 src/window.c（include 路径为 include、../sandcore/user、vendor/miniz-3.0.2、build/generated，启用 -ffunction-sections -fdata-sections -Wl,--gc-sections），输出到 build/ 后运行。该检查使用 SC32 边界替身，不等同于客体实测；实际显示、安装、动画、更新和 Launch 由用户在 SandCore 中运行。
