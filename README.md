# SandCore / 沙核

自研32位x86操作系统：内核、SandFS、图形桌面、SCX用户程序、SKM零环扩展、
原生编译器和命令行开发环境。M9已于2026-10-05由用户验收通过并正式发布；
M8a成果及原M8未完成内容继续按原决定封存。

| 能力 | 当前范围 |
|---|---|
| 图形 | VGA及原生真彩、多分辨率、Aurora/Classic、独立窗口与客户区截图 |
| 音频 | QEMU AC97、开关机/六种提示、MP3/WAV/FLAC、封面与左下角迷你播放器 |
| 身份与文件 | 内核UID/GID/rw、固定进程身份、内核票据、SandFS v5/COW事务 |
| 最高管理 | 外部双串口SYSTEM；客体root不能自连或取得MOD编辑资格；COM1真实零环调试 |
| CLI开发 | 自写独立命令、nano、S3C/SCCC、SandAsm、SCDBG、一次性/常驻SKM |
| SSE2 | 整数编码、CPU检测、抢占/异常/复用状态隔离与回退 |
| CLI参考覆盖 | 用户确认M9本地140/171（81.9%），只承诺公开支持子集，完整310行/缺项保留 |

[验收与截图](sandcore/docs/M9-ACCEPTANCE.md)、[实现清单](sandcore/docs/M9-IMPLEMENTATION.md)、
[设计](sandcore/docs/M9-DESIGN.md)、[构建](sandcore/docs/BUILD.md)、
[第三方源码/许可](sandcore/docs/THIRD-PARTY.md)、[完整CLI对照](sandcore/docs/M9-CLI-MATRIX.tsv)。
项目源码在`sandcore/`，工作区规则在`AGENTS.md`，当前接力状态在`HANDOFF.md`。

根目录的`sanddata_editor/`子项目提供自写C宿主工具[sanddata_editor.exe](sanddata_editor/sanddata_editor.exe)，
像压缩包一样浏览SandFS镜像，支持文件/文件夹拖入拖出、删除、重命名与备份保存；
用法与源码入口见[编辑器说明](sanddata_editor/README.md)。

在 Windows 安装 WSL Ubuntu 工具链和 Python/Pillow 后，根目录运行 `build.bat`
构建 M9；历史版本运行 `build-version.bat M6a`、`M6`、`M7` 或 `M8a`。
五份源码均已在干净克隆中实际构建通过；M9另通过真实串口及客体编译运行检查。
依赖、输出及验证边界见[构建复现记录](sandcore/docs/REBUILD-VERIFICATION.md)。

本仓库保存完整工程源码、必要资源、文档、测试工具及许可证；`snapshots/`保留
M6/M6a/M7/M8a的历史源码，`releases/`仅保存M6/M7/M8a/M9各一份精简构建包。
每包包含一套既有启动盘/数据盘和启动工具，镜像字节来自原交付包，不重新构建旧版本。
`history/`还保存被历史build/试玩目录忽略的源码、文档和影片工程输入，按内容去重。
文件范围、摘要和复现条件见[备份说明](sandcore/docs/SOURCE-BACKUP.md)。
[GitHub Releases](https://github.com/mio-qwq/SandCore/releases)提供四个版本页面及构建包链接。
历史测试盘、录像、渲染输出、宿主下载工具和缓存留在本机。仓库保持私有。
网络留M10、GPU暂缓；M10完成时基础内核冻结为仅安全更新，功能通过CORE SKM发布。

修订：2026-10-05，M9验收候选；按用户最新范围保存完整工程代码与每版本一份构建产物。
修订：2026-10-05，补齐历史源码输入与字体，五版干净构建及M9客体运行通过；建立轻量Releases页面。
修订：2026-10-05，用户明确验收M9通过；增加根目录自写C/Win32 SandFS镜像编辑器。
修订：2026-10-06，镜像编辑器统一迁入sanddata_editor子目录并更新入口。
