# SandCore / 沙核

自研32位x86操作系统：内核、SandFS、图形桌面、SCX用户程序、SKM零环扩展、
原生编译器和命令行开发环境。当前为M9验收候选，Windows QEMU验证通过，
待用户验收；M8a成果及原M8未完成内容继续按原决定封存。

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

本仓库为用户授权的私有项目备份。源码、资源、文档、测试和许可直接进入Git；
运行镜像、固定M7/M8a包及历史数据放私有Release，说明和恢复脚本见[备份入口](backup/README.md)。
宿主下载工具/缓存不复制；原本机成果不删除。未经用户决定不开放仓库或改变开源授权。
网络留M10、GPU暂缓；M10完成时基础内核冻结为仅安全更新，功能通过CORE SKM发布。

修订：2026-10-05，M9验收候选与私有近完整备份入口。
