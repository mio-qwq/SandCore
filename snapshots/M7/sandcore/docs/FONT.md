# 凤凰字体与三环 Unicode 文字接口

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
