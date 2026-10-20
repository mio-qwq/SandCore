# 固定信任快照

源文件来自 Chromium 官方 Gitiles 的 net/data/ssl/chrome_root_store：root_store.certs 与 root_store.textproto。构建只读取本目录已经固定的文件，不联网更新信任根。

快照策略版本 version_major=42。make_anchors.py 从 trust_anchors 的 100 个活动条目提取受支持的 RSA/EC 公钥，按传统 X.509 CA 信任模型使用；additional_certs 中的其他证书不作为新信任根。不会按单个域名添加例外。

0.1.2 曾错误地把包含 Chrome 专属 constraints 的 47 个活动 CA 整体排除，造成有效的 DigiCert G2 等链无法验证。0.1.3 修正了该生成策略。Chrome 的版本条件和 SCT 发布截止条件需要其 CT/版本策略引擎，不能解释为“这个根现在失效”。本浏览器尚未实现这些附加策略，ROOTS.JSON 对此明确标注；不宣称符合 Chrome Root Program 的完整约束。

策略 SHA-256、每张纳入根证书的 SHA-256 和数量写入 build/anchors.json，并随安装包保存为 ROOTS.JSON。快照文件本身的摘要见 SNAPSHOT.json。Chromium BSD 许可位于 ../licenses/CHROMIUM.TXT。

这不是 Chrome 的全部策略，尚无在线撤销、完整 distrust 时间策略或自动更新。TLS 仍使用证书签名、主机名和 UTC 有效期校验。没有不安全的继续按钮。
