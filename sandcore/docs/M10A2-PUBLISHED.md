# M10a2 实际发布记录

2026-10-17。SandCore M10a2 — The Application Platform Era已发布。以下来自实际GitHub返回结果；发布前的staging/交付报告保持其原始时态。

- [GitHub Release](https://github.com/mio-qwq/SandCore/releases/tag/M10a2)，tag M10a2，draft=false，prerelease=false。
- [发行源提交](https://github.com/mio-qwq/SandCore/commit/53090db84670d4b875c1ceb164c77ada52cc025a)：`53090db84670d4b875c1ceb164c77ada52cc025a`。最终完整树快照的GitHub签名验证verified=true、reason=valid，GraphQL签名VALID。主体使用正常Git通过代理提交/推送，传输提交自身未签名的事实记录在commit.json；最终快照签名覆盖完整树与父链。此前开发历史完整保留，无强推。
- 完整交付合同与GUI13原资源FAIL/+8页边界见[M10A2-ACCEPTANCE.md](M10A2-ACCEPTANCE.md)，未改称零差PASS。
- SDK是同一native11/HOST21/final12的499项载荷、8普通原生库和唯一SCSOUND宿主codec例外；运行包严格四文件，用户test盘不在附件中。

| 附件 | 字节 | SHA256 |
|---|---:|---|
| [SandCore-M10a2-SDK.zip](https://github.com/mio-qwq/SandCore/releases/download/M10a2/SandCore-M10a2-SDK.zip) | 5211109 | `9170d28760466af1fbf6e287867b1ab9f7073674969af201302cc3953aa4a8eb` |
| [SandCore-M10a2.zip](https://github.com/mio-qwq/SandCore/releases/download/M10a2/SandCore-M10a2.zip) | 21471099 | `66a9311cf68ba3bf5ca9a1649893163807294064198f70dd228459152df4acec` |
| [SHA256SUMS](https://github.com/mio-qwq/SandCore/releases/download/M10a2/SHA256SUMS) | 174 | `77a1fd59a0fd03841034946dc3925c8a28c9cbb5943d5b33ffc7a8193800cbcd` |

GitHub上传后的资产digest/size已与本机逐项相等；原始记录见[evidence/m10a2-delivery-01](../evidence/m10a2-delivery-01/README.md)中的commit.json、publish.json、delivery.json及SHA256SUMS。未因发布新增编译/QEMU矩阵。完整M10及基础内核冻结未在本版宣称完成。

修订：2026-10-17，落地实际签名与Release结果。
