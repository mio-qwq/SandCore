# native04 → native05 定向刷新接线

本稿只准备新接线，未执行当前源码preflight、构建、客体编译或VM。Root提供未来成功HOST11的完整SHA与最终显式标签后执行。当前预期仅SCUI；这不是默认值，程序要求重复`--label`提供完整集合，并要求实际闭包变化与其精确相等。未知标签、重复标签、compiler/SCLD变化仍拒绝。

旧运行底座固定为final04 CORE/SDK与layout02：CORE.SKM为597840 B、SHA `37331d772966e8d02759297bf74b08be3025b3b8e36f1e76d75735dc468529b7`。前轮native04报告完整SHA为`1ac7e951434d5465a665844019a731609a08c35c4a22a5f42bb6c43fac2b5191`，真实outer04为`226d9d99ae250c247744430b0247f7c7f30cc315ca1687e2a8e6b604f7317175`。native04 producer仍为HOST09；final04运行CORE/SDK是HOST10；未来当前准备来源是HOST11。历史preflight里的HOST03、旧task_layout及layout02实际编译来源均保留，不改写历史。

`followup.py`复用冻结v1的pin/copy/get绑定和原runner，复用冻结v2的标签与当前公开头验证。新增范围只有native04/final04/layout02固定输入，以及跨两轮明确继承路径的副文件追溯。85份副文件的只读检查已通过：36份直接实际get，49份经显式继承链到原始get；记录的报告SHA、case、代数、PID、guest路径、字节和摘要逐项相符。没有往inner报告伪造get条目。新报告的公开头来源取真实prepared map，旧report保持原样。

观察器使用final04 SDK归档中的旧SCUI/Desktop源码，五个结构头与真实i386 layout02的TU/bin/JSON逐字节核验。final04 CORE二进制、94份实际`.d`、生成头和原构建凭证固定；完整旧编译闭包摘要与HOST10原记录核对。观察器实际解释的`PRIVATE_SOURCE_PINS`仍须原字节。其余旧内核实现由固定CORE ELF及原凭证确定，不把未来修复后的当前`image_service.c`错当作旧CORE正文，也不要求当前内核退回旧实现。未来HOST11仍由原`sdk.verified_build`完整核验当前源码/ABI/工具/输出，当前用户源码和所有formal NATIVE清单仍逐项严格比较。

运行八库、桌面/Login、G1 SCCC/SCLD、SCFILEWORK采用native04旧原生字节；SCSOUND仅取final04 SDK中已证明的唯一HOST codec例外，UAX取同一SDK。IMAGE取固定HOST10对应字节。新产物只在客体编译并取回到新bundle，不安装到本轮bootstrap。

Root结束GUI03、收齐修复并取得HOST11实际完整SHA后，在sandcore目录执行以下命令。先把`$host11Sha`设为Root已确认的64位SHA；若最终批准的实际标签多于SCUI，逐项追加`--label`，不要省略或使用自动猜测集合。两个05目录必须尚不存在。

```powershell
python -B drafts/m10a2-p6-followup-refresh-v4/driver.py --host-report build/m10a2-main-studio-01/p6-unified-host-11.json --host-sha256 $host11Sha --label SCUI --vm-out build/m10a2-p6-refresh-vm-05 --bundle-out build/m10a2-p6-target-refresh-05
```

该命令只读准备，不创建输出目录或VM。准备通过后，Root在同一命令追加`--execute`；未来真实客体工作目录为`/HOME/P6FOLLOWUP4`。若05已留有失败结果，保留失败目录，由Root选择同前缀的新编号，不能覆盖。执行沿原严格refresh流程，最终必须仍为8库/21 artifacts/27 SCO完整集合、strict reader通过、真实owner accounting及current/source守恒；不运行额外矩阵。

仅审阅旧底座可用：

```powershell
python -B drafts/m10a2-p6-followup-refresh-v4/driver.py --frozen-audit
```

本次只执行上述旧底座只读audit（exit 0）、两份新Python AST及旧bootstrap/观察器字节核对。没有读取当前GUI进程/输入，没有启动VM、构建或复制数据盘；主树、旧helper、旧draft与历史报告均未修改。`PINS.json`列固定输入，`READ-ONLY-CHECK.json`记录边界，`driver.patch`是冻结v3到新driver的回读差异，不应用到旧目录。

修订：2026-10-16，新v4准备完成；未来HOST11/current preparation及实际native05执行待Root。
