# 备份与恢复

Git保存完整项目源码/资源/文档/测试/许可，Release `backup-2026-10-05`保存：

- `SandCore-M9-acceptance-2026-10-05.zip`：可直接解压试玩的M9验收包。
- `SandCore-M7-2026-10-02.zip`、`SandCore-M8a-2026-10-04.zip`：构建复现依赖的固定原字节。
- `SandCore-history-2026-10-05.scbk.zip`：历史工作区按1MiB内容块去重，独立zlib压缩。
- `SHA256SUMS.txt`及清单：附件大小、原始内容摘要、排除项、ZIP历史成员与恢复范围。

下载同一Release的所有附件到一个目录，Python 3标准库即可校验与恢复：

```sh
gh release download backup-2026-10-05 --repo mio-qwq/SandCore --dir downloaded
python backup/restore_history.py --archive downloaded/SandCore-history-2026-10-05.scbk.zip --assets downloaded --verify-only
python backup/restore_history.py --archive downloaded/SandCore-history-2026-10-05.scbk.zip --assets downloaded --out restored-projectos
```

恢复目标必须是不存在或空的独立目录，不覆盖当前工作区。普通文件和固定附件
逐字节核对SHA256；历史重复ZIP按成员内容备份，恢复到`ARCHIVE-CONTENTS/`，
保留原ZIP摘要、顺序和元信息，不承诺重新压缩后容器字节相同。
原始固定M7/M8a包保持原ZIP，不受该规则影响。

只排除宿主工具下载/缓存、Python缓存、Git元数据、本次输出本身，以及备份期间
确实持续变化而不能取得稳定副本的文件。每个排除项在清单列明。
全部本机原文件保留；Git+全部Release附件共同构成备份，单独clone不含大镜像。
过往未完成/失败证据保留原状态；M9验收候选不等于用户已验收或M10冻结完成。
