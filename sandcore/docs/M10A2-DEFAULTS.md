# 默认入口的精确升级

唯一发布基线为 build/m10a1/sanddata.img，SHA256 3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64。已使用只读文件和 ACCESS_READ mmap 验证；只提取 493B MENU 和 16 个三行 LNK，以及 UID/GID/mode/摘要，没有复制盘。该盘没有 CAT。

1. Desktop system seed 正常改为十四个新 SCX，采用 MODERN profile；每项先查新文件。明确 NOT_FOUND 时查已知旧入口并保旧 profile，全部不存在则跳过，不生成坏链接。旧文件、源码和 LEGACY 参数入口保留，不做全局路径重定向。
2. 候选打包在发布树程序已经合并后处理 records。仅相同路径、原始全文字节、UID/GID/mode 都与基线一致时才转换。MENU 保原标签、图标、行序和换行，只改无参数已知命令；Legacy tools 和四个游戏保留。LNK 转为既有 SCLNK2 的 executable/MODERN 描述，保标签、图标和空参数。新文件缺失时原条目原字节保留。改变的原文本交给打包器现有小文件归档，原盘不写。
3. 启动载入 CAT 后，仅完整字段符合原 /SYS/MENU.CFG 特定行生成的 Imported ACCOUNT 项才做内存投影，包括 ID 来源 hash/行号、层、flags、全部字符串、唯一 open verb 和 flags/types/profile；有任何自定义则不动。投影保持 ID/token/顺序/布局，其他记录不替换。只提交包含改变项的 catalog patch，携带原 layout 内容，沿公开 Import 做 epoch 条件提交。SYSTEM CAT 继续被原 Load 拒绝，不增加此前不存在的兼容格式。
4. 首次导入 MENU 仅在全文为原基线或本转换器合法逐行升级结果且 UID/GID/mode 原样时投影；持 READ_STABLE lease 覆盖比对和实际 ImportLegacyMenu，避免检查后换源。自定义菜单不做投影。保存过的 CAT 独立按完整 Imported 描述比对，不猜当前菜单内容。
5. 投影默认只影响内存。沿现有正常 Store 的 exact revision/Preserving/持久 ACK 保存，不手工改 CAT 字节，不强制重排或保存布局；明确日志 durable=0，后续正常成功 Store 才是持久事实。

新 system 关联为目录→Files2，代码→Studio2，文本→Notes2，图片→Lens2，实际支持的 mp3/wav/flac→Sound2；旧显式账户/会话关联优先级保留。Installer 仅文件安装 GUI，不加入包管理合同。

只做一个小离线 records 转换检查和必要源码编译检查，不启动客体或扩矩阵。Root 整合真实候选打包、生成精确动态 manifest，并统一验证菜单/关联/Files↔Desktop Drop。

修订：2026-10-16，按真实发布字节及现有 ACCOUNT-only Store/Load 合同收敛。

最终候选安装沿单次prepare/write_image接明确metadata覆盖：旧对象保原UID/GID/mode，新SYS文件使用SYSTEM身份，新APPS/BIN使用既有默认；覆盖只限显式载荷，未列出对象逐项守恒。确有变化的旧运行程序及默认小文本沿SYS/M10BASE/XXXXXXXX.OLD和INDEX.JSON归档，保原身份；已有同字节/身份历史可复用，旧INDEX先归档，原盘只读。实际安装回读须同时核正文和元数据，未执行规划不能称已迁移。

修订：2026-10-16，最终单会话候选明确元数据及被替换原件归档，避免旧测试准备器默认值覆盖用户身份。
