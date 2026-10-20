# 固定信任快照

源文件来自 Chromium 官方 Gitiles 的 net/data/ssl/chrome_root_store：root_store.certs 与 root_store.textproto。构建只读取本目录已经固定的文件，不联网更新信任根。

策略版本 version_major=42。make_anchors.py 从 100 个活动条目中选择 53 个不附加无法表达的 constraints 且算法受支持的根。其他 47 个排除，不能以“忽略额外约束”方式扩大信任。

策略 SHA-256、每张纳入根证书的 SHA-256 和数量写入 build/anchors.json，并随安装包保存为 ROOTS.JSON。快照文件本身的摘要见 SNAPSHOT.json。Chromium BSD 许可位于 ../licenses/CHROMIUM.TXT。

这不是 Chrome 的全部策略，尚无在线撤销、完整 distrust 时间策略或自动更新。TLS 仍使用证书签名、主机名和 UTC 有效期校验。没有不安全的继续按钮。
