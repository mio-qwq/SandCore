# SC32 字体与字形副本

所属版本：SandCore M10a2 — The Application Platform Era。

2026-10-15：公共头、私有字体服务和Ring3正文、SC32API.SCL导出/版本、可信gate与构建规则已收齐，**尚未构建或运行本批**。Phase3已验证字节保留；新源码不能因为旧0x248/249已验就宣称新接口通过。

正式入口为 `SC32FONT.H` 中 `ScFontQuery`、`ScFontGetGlyph`。应用只使用cdecl函数及值记录，系统库持有寄存器桥；SCUI只缓存本进程字形副本，不复制字体解析器。字体来源继续是用户原凤凰12/16 TTF；适用字体配置、许可证、原字节和旧调用见[FONT.md](FONT.md)。

`ScFontInfo`为96B：size/version、face/generation/active/source_kind、glyph_count/mapped_count/units_per_em、有符号ascent/descent/line_gap、data_bytes/face_pages/cache_pages/cache_hits/cache_misses/horizontal_metrics/native_pixels/flags、四个零保留字。两脸编号12/16；native_pixels只陈述脸原生设计尺寸。指标仍是font units，行框按请求尺寸用整数计算。缓存计数是现有真实32位计数，不能假称宽计数。Query未激活脸成功报告active=0/source_kind=NONE，不能虚构可用字库。

`ScFontGlyphDesc`为32B：size/version/face/scalar/pixels/expected_generation/flags/reserved。pixels为1..64；拒绝代理项、超Unicode范围、非法脸和未定义flags。expected_generation为0取当前脸，非0与当前epoch不符返回STALE；未激活脸为NOT_FOUND。

`ScFontGlyph`为1600B：64B头含size/version/face/generation/glyph_id/mapped/pixels/advance/width/height/baseline/left/bearing_26_6/advance_26_6/flags/reserved，后接96行×4字。bit31为左端，宽至多128、高至多96，未用行列为0。advance、baseline、left及26.6值按有符号32位解释。合法缺字也成功：mapped=0，提供同脸`.notdef`及真实指标；不能把旧返回0误作一个宽度为0的空字形。

输入尺寸精确、version=1；输出允许规范尺寸至65536B、版本1。先检查完整输入RO与声明输出RW，错误不写out；成功只写规范前缀，未知尾部保持。字形生成在可信候选完成后发布。服务共用一个1600B旧/新候选并只转换64B头，避免在每任务4KiB内核栈同时留两张位图。字体缓存、轮廓暂存和解析来源依然归内核，公开记录没有内部指针。

私有0x300 gate的selector为0x600/0x601；旧0x248/249、400字头布局与冻结SCAPI.H完全不变。能力位SC32_CAP_FONT_GLYPHS只有实际接线后才置。150%/200%应请求18/24或24/32px并重栅格，不能放大整张GUI替代原生尺寸。

当前无恢复脸新合同：TTF未激活不借旧FONT8伪装12px脸。若SCUI需要同源恢复，后续必须单列source_kind、真实16px限制与epoch并实际接线；不替换字库、不伪造原始指标。复杂脚本整形、kerning、任意TTF、hint解释器及输入法不因新接口产生。

## 修订记录

| 日期 | 变更与证据边界 |
|---|---|
| 2026-10-15 | 实现两项typed字体正文与正式公共布局，保持旧ABI；尚未接线、构建或执行 |


2026-10-15后续接线：两个桥已组合SC32API.SCL ABI1.2/v3，kernel/sc32_font.o进CORE，SYS300选择600/601实际分发，Font能力位按本构建服务登记。新增ScDisplayPalette共用旧0x27真实256槽副本，先验256..16384项的完整输出RW，成功只改前256。SCUI/SCB1/SCF使用该副本而非另一张裸色表。本批接线源码仍未构建/运行，原旧字体结果只证明原服务，新的布局/护栏/缺字代数将由PLATFORMPROBE真实检查。

修订：2026-10-15，typed字体/真实DAC调色板已收齐gate/Make/独立SCL导出，未构建运行。
