# M10a2.2 发行内容记录

本补丁只装配已构建、已完成定向验证的文件系统修复内核，不新增构建或QEMU运行。功能源码改动仅为`kernel/fs.c`，位于已签名提交`60a36d00cfe8e09eebc427cf7dcdaf9f4b6d98e4`；原验证见[目录修复](../m10a2-directory-repair/README.md)。

`delivery.json`及`SHA256SUMS`记录运行包、SDK、修复内核和数据盘摘要。运行盘从M10a2.1干净发行附件装配，严格四文件；只替换主核、恢复主核及3份SDK文档，逐对象核对2483条其它记录保持。SDK保留全部二进制原字节，文档、MANIFEST、PROVENANCE、HASHES及内部SHA256SUMS同步。用户个人test盘没有访问或打包。

原M10a2、M10a2.1的Release和附件不改；本次独立发行见[M10a2.2](https://github.com/mio-qwq/SandCore/releases/tag/M10a2.2)。详细使用与边界见[补丁说明](../../docs/M10A2.2-RELEASE.md)，附件摘要见[发布记录](../../docs/M10A2-PUBLISHED.md)。签名提交与实际上传digest/size的完整结果保存在本地build/m10a2-directory-release-01，发布不会改写旧验证结果。

修订：2026-10-17，记录独立M10a2.2打包来源及未重复测试边界。
