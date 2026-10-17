# SandCore M10a2 — The Application Platform Era

M10a2建立可复用的Ring3应用平台：SCL动态链接、四大系统库、统一SCUI桌面和主要应用，以及可在系统内编译多源程序的SDK。

## 下载与启动

[下载 SandCore M10a2](https://github.com/mio-qwq/SandCore/releases/latest)。

| 附件 | 内容 |
|---|---|
| SandCore-M10a2.zip | run.py、run.bat、sandcore.img、sanddata.img；桌面、应用与内置SDK |
| SandCore-M10a2-SDK.zip | 公共头、9个库、原生编译和链接工具、示例、规范、源码及许可证 |
| SHA256SUMS | 运行包和SDK的SHA256摘要 |

安装Windows QEMU和Python后，解压运行包并双击run.bat。QEMU默认路径为C:/Program Files/qemu，参数见run.py --help。启动器优先WHPX，可回退TCG；SDL原生相对鼠标、物理DPI和最近邻显示保持。客体最高1920×1080，宿主比例不同时全屏会保留比例边框。

默认mio未设置密码时，可在本机登录界面留空登录；设置过密码后使用实际密码。退出桌面返回登录并保留会话，注销关闭当前会话。已有个人数据盘应先备份，避免用干净发行盘覆盖个人文件。

## 应用与开发平台

- SCL动态装载支持版本、依赖、符号、初始化、退出回收与失败回滚。SCAPI.H旧接口及原字节保留，新能力通过SC32API和系统库公开。
- Ring3桌面提供图标、多选、持久布局、开始菜单、任务栏、文件关联、跨应用剪贴板与拖放。文件作业提供权限检查、持久记录和条件提交。
- Files、Studio、Debug、Notes、Canvas、Lens、Settings、Monitor、Calculator、Terminal、Palette、Sound、Session和Installer使用统一界面框架。
- Studio可直接打开C文件构建，也可新建带默认程序的SCW工程；F7构建、Ctrl+F5运行、F5调试。输出跟随末行，错误显示文件、行号和原因。文件对话框支持双击/Enter进入文件夹。
- SDK提供SCCC→SCO→SCLD多源编译流程、独立动态插件、标准C与通用库、GUI/图形/终端/音频能力，以及ResearchDesk等示例。
- SandFS保持目录名字规范补零，主内核与恢复内核使用相同文件系统实现；磁盘格式及公开API/ABI保持兼容。

Studio操作见[使用说明](STUDIO2-USER-GUIDE.md)，SDK见[M10A2-SDK.md](M10A2-SDK.md)，多源示例见[SDK-RESEARCHDESK.md](SDK-RESEARCHDESK.md)。

## 支持范围

8个普通系统库及SCCC/SCLD来自系统内编译，SCSOUND使用单独记录的宿主codec实现。SCCLIB支持文档列出的32位C子集，未提供完整ISO C、浮点、64位整数、scanf或完整locale/wide支持。SCL按进程装载，不宣称跨进程物理代码共享或完整W^X。

网络支持Windows QEMU e1000、IPv4和文档列出的HTTP工具，未实现HTTPS/TLS、IPv6或浏览器。GPU、SMP和全系统完整双语不在本版支持范围。登录功能已验证，相关历史资源检查存在未通过项，不宣称全部路径资源零差。M8封存范围保持；完整M10及基础内核冻结未在本版宣称完成。

附件摘要见[M10A2-PUBLISHED.md](M10A2-PUBLISHED.md)。

修订：2026-10-18，统一M10a2发行内容、启动方式与支持范围。
