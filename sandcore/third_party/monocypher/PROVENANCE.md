# Monocypher 4.0.3 固定验签来源

本项目采用 **BSD-2-Clause**，同时原样保留上游双许可全文、文件头、作者表和说明。
官方仓库：https://github.com/LoupVaillant/Monocypher
固定提交：`ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f`（4.0.3）。
官方源码归档：`https://codeload.github.com/LoupVaillant/Monocypher/zip/ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f`。
归档SHA256：`3f2a78032686ae8f085bd7892a890e932cf4f6ce42328214ce7aaf14d58b6c18`。

选用七份未修改原件：LICENCE.md、AUTHORS.md、README.md、src/monocypher.c/h、
src/optional/monocypher-ed25519.c/h。逐文件摘要见../M10-SOURCES.json。
本地包装只调用公开Ed25519验签函数和曲线验签依赖；SHA-512属于同一快照。
上游Edwards/X25519包含SUPERCOP ref10来源片段，AUTHORS.md保留对应作者；
上游许可说明将测试外部实现记为公有领域。未编译/链接测试外部库，未复制其实现
进客体目录。完整官方归档仅作为宿主来源证据保留在third_party/imports。

内核适配在kernel/core_signature.c与构建规则：freestanding整数、关闭浮点/SIMD、
函数分段与链接回收；不会链接签署、私钥生成、其它密码算法或标准C运行库。
公钥由用户提供的32字节原始公开文件固定；缺键关闭CORE扩展信任。
包装限制规范点编码，并拒绝可接受公开退化签名的低阶点；内核拒绝坏长度/格式，
再由上游完成Ed25519方程检查。尚未构建/运行，不以下载摘要作为密码学验收。

全部选用原件及本说明安装至`/SYS/LICENSE/MONOCYPHER/`，不只附一份许可证。
OpenSSL仅为独立宿主验签工具，不进入系统组件依赖或发布盘。

修订：2026-10-06，固定来源、BSD许可选择、内含来源、完整归档与适配接线；未验证。
