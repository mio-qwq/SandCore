# 桌面图标黑边：straight ARGB 修复准备

2026-10-16。冻结交接，仅写本目录；未改 main/当前GUI/helpers/SDK、原资源或历史字节，未启动VM。补丁只修 SCUI 两个三环合成点及一个私有声明，不改 nearest/bilinear、布局、图标尺寸、阴影、资源加载、公共ABI或内核绘制。

## 已确认的实际原因

桌面 `desktop_view.inc::desk_paint` 先调用 ScUiPainterClear，将此三环透明surface清为ARGB0，壁纸由既有合成器在下层提供。`ui_paint.c::ScUiPainterIcon` 原半透明分支却始终从 `0xff000000` 开始，只用目标RGB混色，不考虑目标alpha。目标为透明0时，它把原始柔边/投影乘alpha当成黑底图，再强制变为不透明；后续WM已无剩余alpha可透出壁纸。`ui_argb.c` 的 OVER 有相同问题。

现有截图拟合实际 Files 72×72框(x96,y48)到原128px资源，1640个不透明样本 **1640个原RGB逐位相同**；693个无快捷方式覆盖的半透明样本精确等于错误的黑底不透明结果，另外6个位置位于光标覆盖的同一x160竖列，原值与观察值完整保留在 PIXEL-EVIDENCE.json，不把它们算作图标结果。

两个直接可核样本：源(16,32)为 `01000000`，截图(105,66)变为纯黑；源(19,33)为 `13BBA15D`，截图(107,67)变为RGB(14,12,7)，恰是原色乘19/255后强制A255。正确的透明目标合成应原字节保留这两个源ARGB，让原始alpha交给下层壁纸。

这不是白色键色抠底。两条实际加载路径完整保留SCB2的straight RGB/A，只有历史SCB1图标的索引0按原合同透明；没有白色RGB阈值。GUI盘所读8份SCB与原m10a1资源逐字节相等。NOTE原件含631个不透明纯白像素，本截图nearest抽到177个，177个仍为纯白；没有把白色内页挖空的证据。不能通过删黑/抠白/阈值化alpha或重绘图标修这个合成错误。

## 补丁与整数合同

`straight-alpha.patch` 的固定原像和结果分别在 pins.json/result-pins.json。三目标是 ui_paint.c、ui_argb.c、SCUIPRIVATE.H；**不修改SCUI.C，不与 m10a2-file-dialog-display 的三个目标重叠，可按任意顺序分别严格应用。** 两组修改整合后只需统一生成一次受影响SCUI及其实际副文件，更新对应SDK/LOGIN发行资源摘要与CORE；公共应用正文未变的生成来源可复用。

两处调用点都明确保留快路：srcA0跳过、srcA255直接复制、destA0原ARGB复制；destA255逐位沿用原通道公式，不经过新helper。COPY仍原样复制全部像素。共享私有 helper 只处理 1≤srcA,destA≤254 的慢路，避免SCCC不inline给普通opaque路径增加函数调用。

慢路令 `D=srcA*255+destA*(255-srcA)`，输出A为 `(D+127)/255`，每个straight通道为 `(srcC*srcA*255+destC*destA*(255-srcA)+D/2)/D`。D最多65024，通道累积与舍入小于2^24；全用ScU32，无浮点、64位除法、分配、读盘或新绘制timer。两个端点均半透明才做这次规范化，原前景颜色与alpha不在加载时破坏。

Root在当前VM结束后，可使用已有严格工具将本补丁应用主树，选全新receipt路径：

```text
python -B sandcore/tools/apply_pinned_m10a2_patch.py --patch sandcore/drafts/m10a2-desktop-icon-alpha/straight-alpha.patch --pins sandcore/drafts/m10a2-desktop-icon-alpha/pins.json --pin-root sandcore --receipt sandcore/build/ROOT-NEW-STRAIGHT-ALPHA-APPLICATION.json
```

本目录未执行此应用命令。原像变化须人工审阅，不模糊重定基。现有SCB/历史文件不重生成、不转换；原采样和所有尺寸保持。

## 实际检查边界

PIXEL-EVIDENCE.json及inspect_pixels.py只读取既有截图/SCB，未生成或编辑图像。arithmetic-report.json为 **宿主i386整数算术** PASS：144个alpha边界/颜色组合、1024个opaque旧公式逐位对照、4个已知回归值，生产helper直接取自同一补丁字符串；参考用宿主64位有理整数公式。没有编译SCUI/SCL/CORE、调用系统或运行客体。初次检查仅fixture的单行if排版触发Werror，其失败报告另保留；生产补丁未因此改变。

Root要求本批只修合成，故菜单缩小和桌面缩放仍保持原nearest采样。Aurora统一nearest可能丢掉很小细节，属独立次级采样问题；本补丁没有借机扩大视觉/缓存/性能合同。不宣称修后截图、客体编译或实际性能通过，Root后续做一次必要原尺寸透明边缘/opaque菜单对照即可。
