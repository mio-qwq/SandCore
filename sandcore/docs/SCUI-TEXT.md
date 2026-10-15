# SCUI Unicode15.1属性资源

2026-10-15：固定官方原数据和完整Unicode V3许可证已保存，生成器与资源格式已写；**SCUI规则正文、客体加载/编辑和分段符合性尚未运行**。属性资源不是分段算法，不把标量Previous改名声称完整grapheme/word行为。

数据来源为官方15.1.0的GraphemeBreakProperty、WordBreakProperty、emoji-data与DerivedCoreProperties原文件，保存于 `third_party/unicode15_1`。ReadMe、完整LICENSE和PROVENANCE.json逐项保留URL/字节/SHA；不复制第三方实现。生成器 `tools/mkui_textdata.py`核固定摘要、完整范围/重叠，再提取GCB/WB/Extended_Pictographic/InCB。原数据及完整声明将在本轮统一接线到 `/SYS/LICENSE/UNICODE15_1/`，运行资源在 `/SYS/UI/UAX15_1.SCU`。

规则依据固定[UAX29 revision43](https://www.unicode.org/reports/tr29/tr29-43.html)，包括GB9c Indic conjunct、emoji ZWJ和RI配对；word采用该版默认规则，语言词典、BiDi、字体整形和IME并不由这张属性表产生。许可采用[Unicode V3](https://www.unicode.org/license.txt)，完整版权/许可/免责声明/禁止无授权背书声明原样保存，不仅列作者名。

格式为小端、64B头加12B连续闭区间记录。头0..7为ASCII `SCUT15_1`；偏移8/12为header_bytes64/format1，16/20/24为Unicode15/1/0，28为完整file_bytes，32为record_count，36为stride12，40为payload IEEE CRC32，44为max_scalar0x10FFFF，48为允许flag mask0x00311F0F，52/56/60为零。区间为start/end/flags三个u32，必须完整连续覆盖0..10FFFF、升序无重叠，邻同flags合并；UTF8输入另拒绝代理项，并非表中存在范围就允许坏UTF8。

flags低4位GCB：Other0、CR1、LF2、Control3、Extend4、ZWJ5、RI6、Prepend7、SpacingMark8、L9/V10/T11/LV12/LVT13。位8..12的WB：Other0、CR1、LF2、Newline3、Extend4、ZWJ5、RI6、Format7、Katakana8、Hebrew_Letter9、ALetter10、Single_Quote11、Double_Quote12、MidNumLet13、MidLetter14、MidNum15、Numeric16、ExtendNumLet17、WSegSpace18。bit16为Extended_Pictographic；位20..21 InCB：None0、Consonant1、Linker2、Extend3。私有常量头为 `user/textdata/SCUTEXTDATA.H`，采用enum避免CPP宏爆炸。

加载先验证头、精确文件长度、计数乘法、保留/flags/enum/range/连续覆盖和CRC，候选完整才发布。资源不可读/格式坏须报告实际错误，不静默猜所有字符为Other。SCUI缓存只读表并二分查询；表归库，context引用与quiet fini解绑定/最后释放必须明确，不为每次caret重新读盘或在巨型C数组占SCCC正文/固定BSS。生成的二进制小于64KiB只是本批实际资源预算，不限制Unicode覆盖。

## 修订记录

| 日期 | 变更与证据边界 |
|---|---|
| 2026-10-15 | 保存Unicode15.1原属性/许可，定义SCUT15_1资源和生成器；算法与客体符合性待SCUI统一验证 |


## 官方边界oracle与公开SDK查询

自写分段算法通过正式 `ScUiDocumentBoundary(document,byte_offset,kind,direction,&out)` 查询；字素/词为1/2，方向为严格前一个或后一个边界。此入口可供编辑导航和Studio补全使用，测试不包含SCUI源码。

固定官方Unicode15.1 `GraphemeBreakTest.txt` 1187行及 `WordBreakTest.txt` 1826行在 `tests/m10a2/unicode15_1`，原件摘要/来源/完整Unicode V3许可及客体归档独立登记。自写 `mkui_texttests.py` 仅按原始÷/×标记提取预期UTF8字节边界，生成36152B/58936B的两个SCT值文件；预期不来自本项目算法。完整3013原始用例各走正式DocumentCreate、正反向Boundary、Release，精确比较每步偏移、错误和最终文件长度。`SCUITEXT.SCX` 不投造键鼠事实，也不据文档分段通过称整个编辑绘画通过。

生成器已转换固定oracle，客体应用正文和统一目标已接线。**尚未构建/执行3013用例**；前述资源生成不是算法PASS。

修订：2026-10-15，登记独立官方3013边界oracle、完整许可归档、严格正反向SDK查询探针与未运行状态。
