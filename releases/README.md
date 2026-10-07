# 各版本一次构建产物

M10a1 按用户2026-10-07最新决定作为 [GitHub Preview 预发布](https://github.com/mio-qwq/SandCore/releases/tag/M10a1)提供，附件为原 `SandCore-M10a1-acceptance.zip`。包不在本目录重复保存，发布来源见[M10A1-PREVIEW.md](../sandcore/docs/M10A1-PREVIEW.md)，附件摘要见[SandCore-M10a1-SHA256SUMS.txt](SandCore-M10a1-SHA256SUMS.txt)。

每个ZIP只有一套原启动盘/数据盘和运行工具；四包共19.913MiB。

| 版本 | 文件 | MiB |
|---|---|---:|
| M6 | [SandCore-M6-build.zip](SandCore-M6-build.zip) | 0.040 |
| M7 | [SandCore-M7-build.zip](SandCore-M7-build.zip) | 0.168 |
| M8a | [SandCore-M8a-build.zip](SandCore-M8a-build.zip) | 8.481 |
| M9 | [SandCore-M9-build.zip](SandCore-M9-build.zip) | 11.224 |

解压后运行包内BAT，需要Windows QEMU；M9另需Python 3.12。
M9已于2026-10-05由用户验收通过；M8a是用户批准的部分发布，原M8未完成目标继续封存。

[全部摘要/来源](MANIFEST.json)、[ZIP校验表](SHA256SUMS.txt)、
[完整备份范围及复现条件](../sandcore/docs/SOURCE-BACKUP.md)。

源码在仓库`sandcore/`；历史源码在`snapshots/`。
全部盘字节来自既有交付，不重编旧版本；重新打开精简ZIP校验全部CRC和SHA256通过。

修订：2026-10-05，用户明确验收M9通过；原验收包字节/摘要保持，正式状态见M9-RELEASE.md。

修订：2026-10-07，新增M10a1 Preview外部附件与摘要入口，历史四份精简包保持。
