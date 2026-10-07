# 原始凤凰点阵字体

来源：用户提供项目根vonwaon-bitmap.ttf.zip，Vonwaon Bitmap Font 1.02，Haoyu Qiu，原创建日期2022-05-12。官方入口[Timothy Qiu](https://timothyqiu.itch.io/vonwaon-bitmap)。选择原声明的CC0-1.0，保留license.txt完整原文；CC0-1.0.txt来自Creative Commons官方完整法律文本https://creativecommons.org/publicdomain/zero/1.0/legalcode.txt。

原包SHA256：cb16d8f99a28fa6b06225466043abde23d521d58c9c5e4e5a1f42aab240792a0。两份TTF与license.txt原样解出，不改字形、字符映射或内部声明；逐文件摘要见third_party/M10-SOURCES.json。TTF本身是用户交付的完整轮廓源，未另取得上游字体编辑工程，不把本记录称为存在未收到的工程源。

静态格式检查：两脸均7543个glyph，Unicode cmap4，全部轮廓为on-curve直线，没有复合字形；12px最大132点/23轮廓，16px最大176点/29轮廓。每脸1个字形有字体指令，内核仅检查长度并跳过，不运行字节码。原生点阵网格的实际像素一致性与完整覆盖统计仍待统一客体/独立参考验证。

构建复制原12/16 TTF到SYS/FONT/PHOENIX12.TTF及PHOENIX16.TTF，原文件/许可/本来源/完整CC0原文另归SYS/LICENSE/VONWAON。加载器由SandCore自写整数代码实现，不移植FreeType/stb_truetype或任意其它字体引擎。数据与解析实现分开，不能把直接读取原字体说成预生成中文子集。

2026-10-06：仅固定原资源与源码接线，未构建未运行。
