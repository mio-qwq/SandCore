# SCW 工作区接线交接

正式合同和可进入SDK的文档为 SCW-WORKSPACE.md；边界见 DESIGN.md。本组只准备固定来源局部补丁，main、GUI/helper、已生成SDK、游戏和原始历史证据均未修改，没有构建或VM运行。

Root整合顺序：

1. 对照 PINS.json，逐项核13个修改路径的前像；按 scw-workspace.patch 局部hunk整合。overlay仅供审读，不整份覆盖其它并行更改。
2. 对照 RENAMES.json，把两份原始小工作区文本精确移动到其 target；当前main中target必须不存在，正文SHA/字节数与source相同。overlay提供两份target原字节，共1069字节，不复制SDK或镜像。
3. 同批加入 docs/SCW-WORKSPACE.md。正式文档使用自然的SCW工作区说明；LIVE-GUI只调整当前待操作的示例入口，CURRENT-GOAL添加当前合同，不重写真实旧执行记录。
4. Root统一进行必要的Studio2实际SCO/原生生成与清单、来源报告增量及SDK新stage。此目录不手工修改NATIVE.JSON，不借同名旧产物宣称已验证。工具/只归档源码的变化按实际影响记静态增量，未变库不需重新编译。

主要源码范围为 Studio2 的四个私有源：STUDIO2.C接收Shell单路径的成对外引号并识别SCW；studio_io.c统一工作区后缀、无后缀保存补全及最终ABSENT保护；studio_actions.c用公开FileDialog的filter/help并解释保存错误；studio_ui.c使用工作区菜单文案。工程schema、成员数组、构建目标、工具命令ID与持久保存逻辑不扩展。共享SCUI/SCSHELL/桌面关联表均不改，与名称提示补丁无重叠路径。

公开FileDialog没有默认文件名字段，所以不读取其私有布局。默认扩展名在Studio保存入口完成。自动补全若目标已存在，io_write_start在拿当前目标身份前明确拒绝；若此前不存在，后续发布仍用原ABSENT条件，另一任务在保存途中创建同名文件也不能被覆盖。显式完整SCW名沿原覆盖确认、EXACT与Preserving。

现有关联合同按后缀选择，游戏存档也使用SCW，因此本组不注册泛SCW关联或增加内容嗅探。显式Open with仍可选Studio；其正常schema校验拒绝非工作区正文。所有游戏代码、存档、旧关联与自定义条目保留。

SDK接线由 tools/m10a2_sdk_final.py 的 install_researchdesk只收 .scw，并通过configure归档SCW-WORKSPACE；stage_m10a2_sdk.py本身委派这两个入口，没有另一个工程后缀表。build_m10a2_p6.py的源码输入后缀相应纳入 .scw。BUILD.SH只补工作区说明，全部实际编译/链接命令逐字不变。

全局只读rg的活跃引用、文件名与游戏碰撞归类见 SCAN.json。冻结草稿、build/SDK副本和历史报告保留；它们不被本组作为正式新SDK入口。所有活跃正式文档补丁及新合同均没有命名沿革叙事。

目前证据只覆盖Python AST、JSON、精确前像不变、两份工作区内容完全相同、脚本非注释命令完全相同和活跃引用覆盖。C代码未编译，文件对话框/保存/真实Shell启动未执行；Root整合后按当前GUI批次确认打开两份SCW、普通用户构建和一次保存即可，不新建矩阵。

修订记录：2026-10-16，SCW工作区入口、默认扩展、SDK文本与精确来源交接；SOURCE_ONLY。
