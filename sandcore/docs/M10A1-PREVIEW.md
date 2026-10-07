# SandCore M10a1 Preview

发布类型：**GitHub 预发布（Pre-release）**；标签：`M10a1`；用户验收与发布授权日期：2026-10-07。后续主开发分支统一为 `main`。

M10a1 是 M10a 的开发版本。本次按用户最新决定发布已有开发验收包，不代表完整 M10 完成或基础内核冻结。原运行实现与有效验收证据复用，本次只做发布文件完整性与分支整合检查。

## 下载与启动

[M10a1 Preview 发布页面](https://github.com/mio-qwq/SandCore/releases/tag/M10a1)

下载附件 **SandCore-M10a1-acceptance.zip**，完整解压后双击 `run-m10a1.bat`。需要 Windows Python 3 和 Windows QEMU，默认 QEMU 路径为 `C:\Program Files\qemu`。包内提供启动/数据盘、运行工具、匹配符号、新版 SandFS 镜像编辑器、源码、文档和既有代表截图/结构化证据。

默认使用 e1000、QEMU user 网络、256MiB 内存和独立会话盘；终端显示实际运行目录。继续已有会话时传入 `--data "旧会话sanddata.img的绝对路径"`。退出使用该盘的 QEMU 后再编辑数据盘。

## 本版变化

- 动态任务与关联资源，取消固定全局多任务数量上限；按实际资源失败、回滚和回收。
- 各登录会话独立桌面与焦点，SYSTEM 管理窗口默认隐藏；隐藏 GUI 停止生成和合成画面，后台工作继续。
- 主内核位于 `/SYS/CORE/CORE.SKM`，最小 loader 与恢复核分离；扩展验签后按内部唯一编号初始化，支持常驻服务。
- 直接加载完整原始凤凰 12px/16px TTF，Notes 中文源码显示与编辑；开发数据盘扩至 256MiB，新版 Windows 镜像编辑器同步支持。
- Windows QEMU e1000 与 IPv4 主机网络栈，19 个独立网络工具，含 `nc -e` 和自写 HTTP `curl`。
- 修复首份验收包默认启动器遗漏 `stop` 事件导致的退出问题，当前附件包含该修复。

完整使用、root 登录方法、实测范围和已有证据见 [M10A1-ACCEPTANCE.md](M10A1-ACCEPTANCE.md)。正式签名私钥仅由用户保管，仓库与运行包接收公开结果。

## 包来源与摘要

附件就是用户指定的既有 `sandcore/build/SandCore-M10a1-acceptance.zip`，保持原字节。

| 项目 | 值 |
|---|---|
| 文件大小 | 61,462,700 字节，约 58.6MiB |
| ZIP SHA256 | `5a9d489726839aee4cbaf95b63857dbe635051b7dad6625fa92c6864282b2f2e` |
| 包内记录的源码提交 | `d403b37848b1f9901c0f3aa3bd478db26f74a405` |
| 主核 SHA256 | `981ee90ba4f399edbc348561e25ef45de2364f50091cad9dc0f088585a513f7b` |
| 数据盘 SHA256 | `3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64` |

包内 `manifest.json` 是打包时的状态快照，仍含 `user_acceptance=PENDING` 和 `release_created=false`；用户之后已经验收并授权本次预发布，最终状态以本发布说明和仓库验收记录为准。包内原源码快照早于本次总 README、ext 迁入和分支整理。

发布前核对启动器修复、Python 入口、关键启动/数据盘、主核符号和编辑器的长度与 SHA256；它们与包内清单一致。清单列出1780个文件，另有打包器生成的BAT和清单本身，共1782个成员；BAT按生成规则核对。这是交付完整性检查，不是重新运行系统验收。

## 已知范围

- 网络目标为 Windows QEMU e1000，仅 IPv4。HTTPS/TLS、IPv6、浏览器、其它真实网卡、GPU 驱动、SMP 和输入法未纳入本版。
- `curl`/`wget` 的 HTTP 子集和 CLI 选项以 [CLI-M10.md](CLI-M10.md) 为准；原18工具对应55项参考，curl单列，不宣称完整BusyBox或上游curl兼容。
- 原 M8 未完成的游戏性能、影片与播放器目标继续按原决定封存。
- `ext/SoftwarePack1` 作为独立用户态软件包纳入最新源码，保留原M9用户验收和宿主补充检查记录；本附件不追加该包的M10a1运行验收声明。
- 本次预发布不会改变仓库可见性；源码和第三方许可范围见项目总README与[THIRD-PARTY.md](THIRD-PARTY.md)。

修订：2026-10-07，依据用户最新授权登记M10a1 Preview、原包摘要与单main分支决定；复用已验收运行结果，不新增客体矩阵。
