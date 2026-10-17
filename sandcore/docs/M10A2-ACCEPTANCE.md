# SandCore M10a2 交付范围

M10a2提供Ring3应用平台、统一桌面与主要应用，以及可在SandCore内编译、链接、运行和调试程序的SDK。运行与开发入口见[M10A2-RELEASE.md](M10A2-RELEASE.md)。

## 平台与实际检查

| 范围 | 已有实现与检查 |
|---|---|
| 动态运行时 | SCL装载、依赖/版本、符号、初始化回滚、退出生命周期与进程隔离 |
| SC32API | 窗口事件、文件对象、稳定读取、租约、条件替换与持久确认；旧ABI保留 |
| SCCLIB / SCLIB | 32位C运行库、标准流、字符串、容器、路径、配置与编码 |
| SCUI / SCGFX | 菜单、工具栏、树、表格、文本、文件对话框、焦点、模态、主题与缩放 |
| 桌面与Files | 图标持久化、多选、关联、跨应用剪贴板、拖放与持久文件作业 |
| Studio与SDK | 直接打开C、新建HelloWorld、SCAPI.H源码兼容、具体编译错误、SCCC/SCLD多源编译、动态插件与调试 |
| 其余应用 | Notes、Canvas、Lens、Settings、Monitor、Calculator、Terminal、Palette、Sound、Session及Installer |
| 文件系统 | 新记录和名字初始化、规范目录序列化；系统内编译/链接/运行后目录读回通过 |

主内核与恢复内核的SHA256均为`827a9e81e5e33a9f70e695ff579c50b7605a7d088fb90fc138657410e832ef68`。普通系统库和SCCC/SCLD沿用实际系统内生成的产物，SCSOUND的宿主codec来源单独记录。公开头、库版本、工具与固定源码通过SDK清单绑定。

Studio的实际检查包括打开C直接构建、旧SCAPI.H程序输出、默认HelloWorld构建运行、文件夹双击、滚动边界和具体语法错误。ResearchDesk覆盖多源应用、独立插件和调试流程。文件系统定向检查实际完成SCCC生成SCO、SCLD链接、程序输出及关闭后的目录严格读回。已有结果按实际受影响范围复用。

## 已知范围

SCCLIB是文档列出的32位C子集，尚不支持浮点、64位整数、scanf或完整locale/wide。SCL未承诺跨进程物理代码共享、NX或完整W^X。网络为IPv4/e1000与已列HTTP功能，未实现HTTPS/TLS、IPv6、浏览器、GPU或SMP。当前最高1920×1080，宿主比例不同会保留比例边框，未宣称全系统完整双语。

退出桌面、注销与本机空密码登录功能已实际检查；历史登录资源检查存在未通过项，不宣称所有路径资源零差。M8封存范围保持；完整M10与基础内核冻结未在M10a2宣称完成。

## 使用入口

解压运行包后启动run.bat。Studio中直接打开C文件或创建SCW工程，F7构建、Ctrl+F5运行、F5调试。ResearchDesk的命令见[SDK-RESEARCHDESK.md](SDK-RESEARCHDESK.md)，应用操作见[STUDIO2-USER-GUIDE.md](STUDIO2-USER-GUIDE.md)。

修订：2026-10-18，统一M10a2平台、应用、SDK与文件系统的实际交付范围。
