# 桌面 alpha 修复的实际截图像素凭证

GUI02 的 Files 图标在本次范围内通过。原不透明前景逐 RGB 保持，半透明边缘与原 straight ARGB、真实壁纸的合成结果逐 RGB 完全相等。证据是主代理已有的普通 UID1000 / SID4 桌面截图；本检查没有启动、暂停、恢复或输入 VM，没有构建或修改主源、旧产物及图像资产。

| 同一采样集合 | GUI01 | GUI02 |
| --- | ---: | ---: |
| 不透明前景等于原 RGB | 1640 / 1640 | 1640 / 1640 |
| 半透明点等于正确壁纸合成 | 0 / 693 | 693 / 693 |
| 半透明点等于旧错误黑底合成 | 693 / 693 | 0 / 693 |
| 全透明点等于独立壁纸模型 | 2522 / 2522 | 2522 / 2522 |

采样严格复用上一轮 `inspect_pixels.py` 的 Files 坐标 `(96,48)`、原始 128×128 → 72×72 nearest 几何。两图均排除快捷方式角标 `(96,107,13,13)`，以及鼠标框 `(160,100,16,20)` 与图标的交集；分别排除 169、160 点，共保留 4855 点。没有搜索其它尺寸、采样器或图标组合。

背景来自实际盘中 `SYS/WALL/AURORA.SCB`，全部源像素 alpha=255，按真实 `desktop_wallpaper` 的整数 cover 规则把 1672×941 裁为 `(0,0,1672,940)` 后映射到 1920×1080。先用 2522 个透明点独立确认背景模型，再用每通道 `(source*alpha + background*(255-alpha) + 127)//255` 判断边缘，不从边缘截图拟合背景。

例如屏幕 `(105,66)` 的源 ARGB 为 `01000000`，壁纸 RGB 为 `(192,227,251)`，正确结果为 `(191,226,250)`；GUI01 为 `(0,0,0)`，GUI02 精确为正确结果。屏幕 `(107,67)` 的源 `13bba15d` 正确结果为 `(192,222,239)`，GUI01 为 `(14,12,7)`，GUI02 精确为正确结果。

原件来源分别只读 `build/m10a1/sanddata.img`、GUI01/GUI02 的 `sanddata.img`。上一轮八个 Aurora 图标及壁纸逐字节相同；未复制磁盘。Files SHA256 为 `2a6a31f0732886fadaf457b1afba18cb8e6beb4ab37282cd86e6a55772286ea1`；壁纸 SHA256 为 `4fa69fd47a82b8752fb425bf3ae5613d0eeb8e7e0aca799c21f600ad835e1922`。八个资源的完整摘要见 JSON。

截图固定输入：

- GUI01：`build/m10a2-p6-gui-run-01/p6-native-desktop-initial.png`，SHA256 `3d6ebce153def126d38457b44eb6df58149d64d313a871745da22922fb8e3584`。
- GUI02：`build/m10a2-p6-gui-run-02/p6-new-native-desktop.png`，SHA256 `9a57a7504873b058462558c4fe17fb41740cdfc3f9f8eeb30bfee1e0cad9455e`。

实际运行来源分层记录：`p6-gui.json` 的 `baseline.coherent_snapshot.debug_modules[6]` 为已加载 SCUI，733256 文件字节，SHA256 `7059348b04925a4ba5f29702abd0c419a570b05008ad854908f70dae66d640b7`，与新原生生成报告一致。该 coherent baseline 的 SCSHELL owner 是 PID1 / generation1 / session1；普通截图的 UID1000 / SID4 来自同报告的 `initial-ordinary-bind` 与 `desktop-start` 动作关联，不能把 baseline 冒称 SID4 的模块查询。报告仍由主代理追加，JSON 只固定本次读取摘要和使用字段，不声称整个运行期报告或数据盘不变。

检查命令：`python -B sandcore/drafts/m10a2-desktop-alpha-runtime/inspect_runtime.py`，实际退出 0。`PIXEL-EVIDENCE.json` SHA256：`91d50731116aa2030e5a9ca0adfac6e45e8c8a102fff8e5e4dffcf454fdf1d22`。

范围仅是该实际截图、同一图标几何的 alpha 修复。未据此接受其它缩放画质、整个桌面性能或完整 M10a2 GUI；没有重画图标、抠白或修改阴影/前景。检查脚本、JSON、本文即本目录冻结交付。
