# SandCore M10a2 下载与校验

[SandCore M10a2 — The Application Platform Era](https://github.com/mio-qwq/SandCore/releases/latest)。

运行包包含run.py、run.bat、sandcore.img、sanddata.img四文件；安装Windows QEMU和Python后运行run.bat。SDK单独提供公共头、9个系统库、原生编译和链接工具、示例、规范、源码及许可证。使用与支持范围见[M10A2-RELEASE.md](M10A2-RELEASE.md)。

| 附件 | 字节 | SHA256 |
|---|---:|---|
| [SandCore-M10a2.zip](https://github.com/mio-qwq/SandCore/releases/latest/download/SandCore-M10a2.zip) | 21642012 | `3abfa11b706c47158392e36e421e766bc2f9fb280e807341c2a76445d6799434` |
| [SandCore-M10a2-SDK.zip](https://github.com/mio-qwq/SandCore/releases/latest/download/SandCore-M10a2-SDK.zip) | 5259694 | `80c7f7ee97ba156c34232199922a801b0d39787cd08ac0073dd90ce9699570fd` |
| [SHA256SUMS](https://github.com/mio-qwq/SandCore/releases/latest/download/SHA256SUMS) | 174 | `904a48c998a359ace4b562f79f93aa52e4ab5f739bad2e0d6dc6b5608c0fe07c` |

主内核与恢复内核SHA256均为`827a9e81e5e33a9f70e695ff579c50b7605a7d088fb90fc138657410e832ef68`。运行包为干净初始数据盘；已有个人文件应另行保留，避免直接覆盖个人盘。

修订：2026-10-18，统一M10a2运行包、SDK下载和附件摘要。
