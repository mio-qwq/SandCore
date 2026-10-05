# 各版本一次构建产物

每个ZIP只有一套原启动盘/数据盘和运行工具；四包共19.913MiB。

| 版本 | 文件 | MiB |
|---|---|---:|
| M6 | [SandCore-M6-build.zip](SandCore-M6-build.zip) | 0.040 |
| M7 | [SandCore-M7-build.zip](SandCore-M7-build.zip) | 0.168 |
| M8a | [SandCore-M8a-build.zip](SandCore-M8a-build.zip) | 8.481 |
| M9 | [SandCore-M9-build.zip](SandCore-M9-build.zip) | 11.224 |

解压后运行包内BAT，需要Windows QEMU；M9另需Python 3.12。
M9仍待用户验收；M8a是用户批准的部分发布，原M8未完成目标继续封存。

[全部摘要/来源](MANIFEST.json)、[ZIP校验表](SHA256SUMS.txt)、
[完整备份范围及复现条件](../sandcore/docs/SOURCE-BACKUP.md)。

源码在仓库`sandcore/`；历史源码在`snapshots/`。
全部盘字节来自既有交付，不重编旧版本；重新打开精简ZIP校验全部CRC和SHA256通过。
