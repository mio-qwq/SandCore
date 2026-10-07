# 凤凰字体与三环 Unicode 文字接口

> 2026-10-07新增待验坏TTF矩阵：M10FBAD.C与verify_m10_font_edges.py已接，九种独立副本为原文件、缺12/缺16/双缺、截断12、越界目录16、越界cmap16、坏loca16、负轮廓16。深层损坏重新计算被改表校验和，避免只在checksum处拒绝却误称结构边界已验。实际启动后核预期脸掩码、另一张完整脸、拒绝脸暂存/字形/epoch/页归零、旧ASCII/GLYPH16/FONT8精确缓冲护栏与真实回退像素；每副本完整回读原其它内容和身份。当前仅源码/静态检查，尚无这九类客体PASS，旧全映射30160记录证据不覆盖坏文件/缩放/OOM/Notes布局。

> **2026-10-07 M10a1：直接读取完整原12px/16px TTF。** 此授权覆盖下文M7“只从font16.txt收字”的历史策略；该表/原SCF/旧ASCII仅保留同源恢复和历史ABI字节。font-04两来源盘两脸各7540个实际映射字形与独立无hint FreeType逐像素一致，合计30160字形记录，公开缓冲边界检查通过，源盘不变。当前参考实际报告版本2.14.2、加载标志163850、共享库SHA de0b38b01924302a7fa7bfbaee47afcb9468ad2f8a22b4a8cca56ad54f64569c。font-03重启中断证据保留；缩放/坏资源/Notes/缓存与旧字形合同仍待验。7543是每份原文件的静态glyph数，不是全部Unicode覆盖数。下方早期未构建叙述保留为历史。

## M10a1 直接 TTF 合同（完整验收未收齐）

用户原包1.02的两份TTF原样保存third_party/vonwaon；逐文件摘要见M10-SOURCES.json，license.txt原声明与完整CC0法律文本/PROVENANCE一起归SYS/LICENSE/VONWAON。运行副本为SYS/FONT/PHOENIX12.TTF及PHOENIX16.TTF。没有生成另一份中文点阵子集，没有移植其它字体引擎；kernel/ttf.c直接解析原字节。

每脸先核SFNT表范围/对齐/重叠/表校验和、head/maxp/hhea/hmtx/loca/glyf/cmap、全部glyph边界与轮廓，再提交完整脸。字库文件≤8MiB，单glyph点/轮廓≤4096作为坏资源预算；它们不是用户任务数量门槛。支持Unicode cmap4与12、长短loca、hmtx尾部重复advance及各glyph左轴承。实际两份字体各7543个glyph，均为on-curve直线且无复合字形；此配置明确拒绝off-curve或复合轮廓，**完整支持这份字库不等于任意TrueType引擎**。字体指令只检查完整长度并跳过，不执行字节码；本字体原生网格像素一致性仍须独立参考实测。布局参照官方[OpenType glyf](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf)、[cmap](https://learn.microsoft.com/en-us/typography/opentype/spec/cmap)、[hmtx](https://learn.microsoft.com/en-us/typography/opentype/spec/hmtx)；这些规范不代替本轮运行证据。

栅格化采用26.6整数坐标、扫描线中心采样、排序交点与非零绕数，保留洞/重叠。真实基线来自hhea、前进/左轴承来自hmtx；原12px的完整行框会保留下降区而不强行裁成12行，原16px为16行。新版NUI/UI从新位图入口取完整脸统一基线；内核新画面用同源布局路径。Notes光标、上下移动、鼠标命中与水平裁剪用同一字宽，非ASCII不再无条件算两格。文件原UTF-8字节与缺字显示分开，仍不包含输入法。

每脸原字节和解析暂存归独立PF页，缓存为64组×4路，按脸/代数/glyph/请求像素键，满组按填入顺序替换；最多256项是缓存预算，不能截断字符覆盖。缓存懒申请真实PF页，失败用单结果暂存，查询不把未申请页计成已占用；新脸提交使旧缓存失效，旧字体页完整释放。三环私有256项副本轮转替换并按版本清空；隐藏GUI不生成帧，不能在后台借字体缓存持续栅格化。

旧FONTINFO四字/SCF字数与激活语义不改。旧GLYPH16已有ASCII/SCF字形、32B行布局、8/16正返回域和未收字0＋空框保留；原表以外可从完整TTF补齐。旧FONT8仍1024B，ASCII继续以原16px相邻两行合并。完整字库统计/缩放用新增调用，不改旧忽略寄存器。

| 新调用 | 寄存器 | 结果 |
|---|---|---|
| 0x248 FONTINFO2 | EBX脸12/16，ECX完整16字输出 | 0成功；无效脸/映射-1。头0..15：版本1、代数、脸、已激活、glyph数、已映射标量数、unitsPerEm、ascent、descent、lineGap、原字节数、脸/暂存真实页数、全局缓存真实页数、命中、未命中、水平长指标数 |
| 0x249 GLYPHBITMAP | EBX标量，ECX脸12/16，EDX请求像素1..64，ESI输出，EDI容量≥400字且≤4096 | 正返回真实整数前进宽；未映射0但给同脸.notdef，非法-1/资源-4均不写输出 |

位图固定400字：16字头＋96行×4个u32，每字bit31为左端。头0..12为版本1、代数、glyph ID、映射成功、请求像素、整数前进宽、画布宽/高、基线、画布相对笔尖左偏移、脸、左轴承26.6、前进26.6；13..15为0。画布≤128×96，未用行/列为0。新入口独立验证完整1600B输出，超出的容量尾不写。负指标按有符号i32解释，无公开内部地址。

实际Unicode映射数量、项目源码仍缺字符/具体位置、全映射glyph绘制、12/16原生及缩放、损坏/截断/重复表、OOM/缓存回收和旧probe逐行兼容须在全量实现后统一验证并统计。当前不得把原文件7543个glyph称为全部中文/全部Unicode，也不得把旧探针PASS当本轮证据。

2026-10-07：font-02首盘实际S3C编译/全部7540原生12px映射和400字
缓冲哨兵/未用行列检查通过，935040B真实转储完整取回。Pillow默认
hint模式只有四个重音字符各差2像素；原轮廓无指令，直接FreeType
2.13.2的同源无hint模式对全部7540字逐像素相同，启用hint又重现
同四字符差异。旧失败报告不改，独立结果freetype-unhinted.json及
freetype-hinted.json保留。当前正式对照明确FT_LOAD_NO_HINTING、
NO_AUTOHINT、NO_BITMAP与FT_RENDER_MODE_MONO，符合前述不执行
字体指令/不自动改轮廓的既有合同；不是为四个字符设例外。参考只
动态调用已安装WSL共享库，记录实际版本/库摘要，不移植进客体。
接口与默认hint行为依据[FreeType官方加载说明](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html#ft_load_xxx)。
两脸/两来源、缩放/缺字/坏资源/Notes/缓存等完整验收继续，不能凭
一张12px转储宣布字体完成。字体原字节/内核轮廓本轮没有修改。

修订：2026-10-07，记录实际12px全映射/边界转储和无hint独立逐像素
对照；保留默认hint差异证据，完整两脸/两盘及其它字体合同待验。

修订：2026-10-06，依据用户新合同接原TTF整数全轮廓/映射与指标、版本缓存、NUI/Notes和0x248/249；保持历史缓冲/字形，未构建未运行。

> 作者 mio。唯一汉字源为 kernel/font16.txt，必须遵守其完整头注释；
> 本文说明 M7 的编码、字形与绘制合同。直接构建与系统内 SCCC 构建的
> 两个探针均已通过 QEMU；用户可以按 TESTING.md 的联合指南查看效果。

## 1. 来源与构建链

所有在用字体来自项目根 vonwaon-bitmap.ttf.zip 内
VonwaonBitmap-16px.ttf（凤凰点阵体 1.02、CC0）。
汉字只从用户拥有的 kernel/font16.txt 构建，不擅自回到完整 TTF
补齐没收录的字，不混用 BIOS、Unifont 或旧手绘 ASCII。

汉字：font16.txt → mkfont.py → 内核同源兜底；
font16.txt → mkscf.py → SYS/FONT.SCF → 开机严格校验后激活。
SCF 保持 SCF1MIO\0、12B 头、每字 276B；最多 128 字，读取缓冲
65536B。校验完整魔数/总长度/UTF-8 标签/重复标签/每行 16 点及 NUL。
坏文件不会激活半张字表，使用同源编译兜底；磁盘成功激活后，未收字
只显示框，不从编译表补字。FONTINFO 可以区分这两种来源。

ASCII：import_font.py --ascii 从同一 16px 文件生成
assets/font/phoenix-ascii.json（保存 TTF SHA256）；mkfont8.py 用
Python 标准库生成 font8_data.h。普通 WSL 构建不需要 Pillow。
原生 ASCII 宽 8、高 16，统一基线保留 g/j 下降部；旧 8×8 接口把
原生相邻两行合并，保证 M6 Shell 和小应用的 10px 行距仍可使用。
8px/倍数放大均由这份凤凰数据生成，没有继续启用旧手绘字表。

补汉字唯一命令：python tools/import_font.py 汉字；随后
python tools/font_preview.py，目检，再 build.bat/QEMU 截图。
不得手工修改或重排 font16.txt。M7 片名九字由此工具补入，当前
36 字；原 27 字逐位保持不变。当前源哈希见 GFX.md。

## 2. 编码与 API

公共头文件仅为完全大写的 SCAPI.H。文字字符串是 UTF-8，而非
GBK，也不是旧式双字节汉字码。Unicode 码点例如“沙”为 U+6C99；
UTF-8 存储字节为 E6 B2 99。两者不能交换作为 GLYPH16 参数。

| 接口 | 用途 | 返回 |
|---|---|---|
| sc_font8(buf) | 128 字×8 行兼容 ASCII 副本，1024B | 1024/-1 |
| sc_glyph16(codepoint,rows) | 16 个 u16、32B、bit15 在左 | 已收字宽 8/16；缺字 0；非法 -1 |
| sc_text16(win,x,y,utf8,color) | 本人窗口透明绘制 UTF-8 原生文字 | 非换行字符数；非法地址/owner/坐标 -1，非法编码 -2 |
| sc_fontinfo(info) | 四个 u32：版本/汉字数/高度/磁盘激活 | 0/-1 |

SC_TEXT16 正文最多 4095B，末尾必须有 NUL；跨页检查先于读字节。
ASCII 前进 8px，汉字/缺字框 16px；LF 行距 18px，CR 回行首。
颜色引用 GFX.md 槽位。负坐标裁剪，输入 x/y 限制 -32768..32767。
严格接受合法 1..4 字节 Unicode 标量，拒绝过长形式、孤立后续字节、
截断序列、代理项和 U+10FFFF 以上数值。整串先校验，失败不绘制。
合法四字节字符若未收录，会绘制一个框，而非把四个字节当四个字。

```c
#include "SCAPI.H"
int main(void)
{
    int win=sc_open("Phoenix",306,164);
    if(win<0) return 1;
    sc_fill(win,0,0,304,150,PAL_UI_NIGHT);
    sc_text16(win,12,35,"沙核操作系统",PAL_UI_TEXT);
    sc_text16(win,12,58,"欢迎来到图形的世界",PAL_UI_GOLD+7);
    wait_escape();
    return 0;
}
```

源文件保存为 UTF-8，SCCC 原样保存字符串中的 UTF-8 字节。
未收录的字不会因为源文件能编码它就自动出现在字库里。
当前键盘输入仍是 ASCII，没有同时承诺中文输入法。

## 3. 游戏/短片的使用方式

FRAME 会覆盖整张客户区，因此先 sc_text16 再 FRAME 会把文字盖掉。
逐帧程序用 sc_glyph16 得到字形副本，在私有帧缓冲逐位点亮，再提交。
UI.inc 的 ui_text16 有 64 项本进程缓存，缺字也缓存为框；它不
返回内核地址、不读取任意 supervisor 内存，也不自行加载第二套字体。
Welcome to Graphics 的中文片名已切换到这个正式入口。

## 4. 验证与修订记录

运行 tools/verify_m7_base.py，font 探针读取真实 SYS/FONT.SCF，
逐字逐行对照 GLYPH16；另检查缺字、非法码点、地址、跨页输出、
owner、坐标及四种错误编码，再绘制中文并写 home/font.ok。
截图位于 build/m7-base/10-phoenix-unicode.png。
系统内 SCCC 再编译 probe.c 并运行 font 模式，验证同一调用桥。
原生截图 build/m7-graphics/01-native-unicode.png；对应 results.json 为 PASS。
verify_modules.py 在副本盘注入坏行数据，实测整张 SCF 不激活且同源编译兜底仍显示。

| 日期 | 变更 |
|---|---|
| 2026-10-02 | 统一凤凰 ASCII/汉字来源，新增 Unicode 字形/UTF-8 绘制/字体信息 ABI；实现与本轮上机验证同步推进 |
| 2026-10-02 | 两条真实三环调用路径、36 字逐行对照/UTF-8/缺字/地址/owner 与坏 SCF 同源兜底通过；六幕短片使用同一正式文字接口 |

修订：2026-10-07，登记九类独立坏TTF/缺文件真实启动与原子拒绝、旧字体缓冲回退矩阵源码；尚未实际运行。

font-edges01仅首original执行，TTF/中文/GLYPH16检查通过；新夹具误猜FONT8为760B且返回0而失败。旧FONT8实际一直1024B/返回1024，已以原代码核对并仅修新夹具；font-edges02完整九类双来源执行中，无终态PASS。

修订：2026-10-07，保留真实原文件测试失败，纠正夹具的FONT8旧ABI误用，不改内核已发布合同。

### 2026-10-07 font-edges02双来源九类真实终态

runner退出0，font-edges-matrix.json为DECLARED_CASES_PASS。两份来源
各original/missing12/missing16/both-missing/truncated12/bad-table16/
bad-cmap16/bad-loca16/bad-outline16，共18个真实启动副本全部完成。
每副本实际S3C编译探针，核预期脸激活、被拒脸epoch/glyph/映射/
字节/暂存页归零、另一原脸完整7540映射和真实中文位图、新16字
快照/400字输出护栏、旧GLYPH16的32B边界/ASCII像素及旧FONT8的
1024B/返回1024/两端护栏。坏深层表的checksum已重新计算，不能
以目录checksum早退冒充cmap/loca/负轮廓边界验证。所有其它原盘
正文/UID/GID/rw/代数逐项回读保持，18输入和两原来源摘要不变。

已实际查看第二来源both-missing/font-rejection-desktop.png：旧
ASCII文件名/图标/Start文字仍正常辨识。该图只是回退检查，不充
正常字体/最终壁纸/全部GUI验收。01错误的760B测试护栏失败保留，
只修新测试而不改旧1024B内核ABI。缩放/缓存OOM/重初始化/原SCX
及当前新主核全映射参考仍继续，完整字体目标尚未全部验收。

tcp-faults01在同一desktop02主核执行新的原始线缆三连接声明
矩阵，尚无终态；network12明确静态分片前置后待运行。单VM，
全部原失败/源盘保留，完整M10a1 Goal active，不创建Release。

修订：2026-10-07，补当前主核双来源18个坏/缺TTF启动及旧精确ABI回退实际通过、双缺字体桌面人工查看，保留未覆盖字体/网络/整机范围。
