# M10a2 SCUI default/publication 证据草稿

本目录保留真实生成与执行来源，当前 **GUI 尚未通过**，不代表完整 M10a2。
所有复制文件逐份核对 SHA256；`archive-m10a2-ui-gui-guest-10.json` 将原路径、归档路径、大小和摘要关联起来，相同正文只复制一份。不包含磁盘副本。

- `reports/first05.json`：原系统内 SCCC0→G1、六项 default 机器分支、双向 55 项 ABI 与既有平台/文本执行；本轮生成的新三库没有在该 VM 安装执行。
- `native/`：上述真实 G1、SC32API、SCCLIB、四个动态 App 与 SWDEFAULT 原字节。`origin-first05/SCUI.SCL` 是 publication 修复前的旧库，`publication02/SCUI.SCL` 是只由原 G1 重新编译的修复库，两者分别保留。
- `reports/publication02.json`：仅重新编译 SCUI 的真实 VM；不重做自举/ABI/平台/文本，也不把生成称为执行。
- `reports/native07-GUI-FAIL.json`：新原生平台与 DEFAULTAPI 实际执行及 owner 回收通过；GUI 整轮仍失败。
- `reports/text09-GUI-FAIL.json`：publication 库的官方 3013 条文本语料及 owner 回收真实通过；GUI 的 Popup 诊断键等待失败，原报告未修改。
- `reports/m10a2-ui-gui-guest-10.json`：真实普通 SID4、PID4/gen2；Combo 三项展开、选择和关闭已发生，随后表格 Ctrl+A 的公开选择数仍为 1，整轮失败。对应原始 console、输入记录及截图保留。

`compile/`、`stdout/`、`sessions/` 和 `provenance/` 均为原文件正文；原样的历史路径通过索引追溯，不改写原报告。`host/` 是归档时的宿主工具快照，不能冒充每份旧报告都使用了这些新版工具。两处选择实现修复仍等待统一 build17 和定向实际验证，尚无成功声明。
