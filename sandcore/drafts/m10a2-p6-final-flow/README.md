# 一次 native 安装与最终 GUI 会话准备

只读准备，未构建、staging、启动VM、复制盘、写主树或用Git。先执行的CORE/SDK精确命令见 CORE-SDK.md；旧GUI-FLOW未改。run03的部分成功不作完整来源，Candidate构造只接受最终DECLARED_P6_NATIVE_GENERATION_PASS和输入/原盘守恒。

## 最少顺序

1. Root收齐实际native报告；非编译工具变动以真实静态增量HOST/archive报告04固定，不因此重生成未变库。
2. 新F/core中只构建CORE/loader；三项issuer显式指向同N的native LOGIN2、desktop和五库。H目录及原HOST报告不变。
3. Windows Python生成SCSOUND固定例外proof和native-preview SDK小文件；八库、compiler、最终SCLD均用真实N provenance。
4. 构造本目录Candidate，只读检查payload/CORE实际载荷、当前源与元数据/小原件归档计划。
5. GUI使用现有QemuSession.data_prepare：启动器只复制原盘一次，回调将统一extra/metadata交现有prepare，一次write_image完成。本步之前不先mkfs一张256MiB盘。
6. 同一普通会话执行既有GUI-FLOW，用当前CORE符号和已有owner/pending账。验收后的本VM sanddata.img是后续发行的明确候选来源，不急于另复制大盘；原盘、旧证据保持。

## Candidate实际安装表

| 目标 | 精确来源 |
|---|---|
| SYS/LIB九库 | SDK stage内八个actual-native SCL，逐字节再次等于N.native_libraries；SCSOUND只等于H的唯一固定host codec输出 |
| SYS/BIN/SCCC.SCX | N.native_artifacts/SCCC1.SCX的本轮真实生成/执行工具；沿SDK provenance核验 |
| SYS/BIN/SCLD.SCX | N的最终SCO/native-SCLD产物SCLD.SCX，非SCLD0 seed |
| APPS/十四组件 | FILES2、STUDIO2、DEBUG2、SETTINGS2、NOTES2、CANVAS2、LENS2、MONITOR2、CALC2、TERMINAL2、PALETTE2、SOUND2、SESSION2、INSTALLER2，全部取N |
| SYS/CORE/LOGIN2.SCX | N的LOGIN2；不能把SESSION2改名当同一发行身份 |
| SYS/SHELL/SCSHELL.SCX | N真实desktop，CORE发行摘要绑定同字节 |
| SYS/SHELL/SCFILEWORK.SCX | N真实文件worker |
| APPS/SOUNDWORK.SCX | N真实音频worker；此路径与sound_jobs.c和正式make一致 |
| BIN/SH.SCX、BIN/NANO.SCX、SYS/CORE/IMAGE.SCX | H明确outputs里的既有运行程序，不把它们冒称本轮native应用 |
| INC/SDK/UI/FONT/LICENSE | SDK stage的全部SYS payload；所选RESOURCE补充只从N已pin bootstrap资源读取 |
| SYS/SRC调试源 | 从真实SCO sources的guest路径和SHA恢复对应user原文；不改SCDB路径、不用另一份同名源冒充 |
| .map/.sdbg | 八native库、真实native应用/最终SCLD的实际副文件，核N所记SHA/长度；旧容器SCCC只带实际map，不造sdbg |
| SYS/CORE/CORE.SKM | F/core真实ELF/flat包装的当前CORE |

不整拷H/fs或整个build，不将HOST bootstrap库/LOGIN/desktop混回native payload。编辑性MENU/USER/ENV/DISPLAY/THEME/WALL、HOME/DESK/USERS、SYS/THEMES资源不会被RESOURCE通配复制覆盖。MENU/LNK只走下述精确默认转换。

CORE审计不只信issuer头：复用inspect_elf/check_main，核CORE载荷与core.bin、ELF界限一致；按真实符号读取login_issued_sha256、shell_host_issued_sha256及六条login_issued_resources，逐条对比本次将装入的native程序/五库/UAX字节。

## metadata可选参数的精确合同

Root将在generation结束后给 verify_m10a2_scl.prepare增加可选metadata参数，旧调用默认行为不变。新参数形如：

    metadata = {
      'SYS/MENU.CFG': (1000, 1000, 23),
      'BIN/SH.SCX': (原uid, 原gid, 原mode),
      'APPS/FILES2.SCX': (0, 0, 23),
      'SYS/LIB/SCUI.SCL': (-1, -1, 23)
    }

键是ASCII fold后的rootless完整路径；值为uid/gid/mode三元组。只有extra明确列项才能覆盖，prepare依此建Record并原样回读核验。旧受控文件替换保其原uid/gid/mode；新SYS文件默认-1/-1/23，新APPS/BIN沿mkfs_m9.defaults为0/0/23；其它新路径同样沿defaults。已有目录不当普通文件覆盖。系统保护/角色权限均不由此准备器放宽。

原prepare对所有SYS addition强写-1、其它强写1000；不能直接拿来替换用户拥有的SYS/MENU或root拥有的BIN/SH。本helper遇未接metadata的旧prepare会明确拒绝，不降级。metadata只规定内容候选的身份，不冒充旧generation；最终所有新代数仍由一次prepare统一单调分配。

## 同一extra里的精确默认升级与原件归档

完整payload先投影到当前盘的只读records，实际新目标存在后调用正式upgrade_records。只有真实发布原MENU/LNK全文+uid/gid/mode匹配才改变；自定义项/参数/四游戏/缺目标保持。完整原基盘且十四新应用齐备时为MENU+12个应用LNK共13项；实际条数以计划报告为准，不硬写PASS。

与正式mkfs_m10一致，归档使用 **SYS/M10BASE/XXXXXXXX.OLD + INDEX.JSON**。不是LEGACY/FILES：

- 本次确有不同字节的旧CORE、SH、nano、IMAGE、默认小文本及明确覆盖的旧源码正文先归档，元数据保持原uid/gid/mode；不复制整盘或所有build。
- 既有.OLD名字永不覆写。相同SHA且同uid/gid/mode的历史原件可复用；否则找未占用的8位十六进制名字，包含本轮已保留名字，避免碰撞。
- INDEX使用正式七字段path/archive/sha256/uid/gid/mode/generation，generation是原对象事实。旧INDEX全文也先归档/复用，再生成只记录本次映射的新INDEX（包含旧INDEX映射），因此旧索引及其前代链不丢失；历史archive不改动。
- 归档原件、INDEX和升级文本全部加入同一extra/metadata，仍只执行一次prepare/write_image。报告给出新归档数/字节和复用映射；默认转换中的planning_generation_not_installed明确不当最终安装代数。

## Root调用方式

先仅只读打印计划（Windows PowerShell，P6HostReport须为实际04路径）：

    python -B drafts/m10a2-p6-final-flow/prepare_candidate.py --native-report build/m10a2-p6-guest-generation-run-03/p6-generation.json --host-report ROOT填写实际04报告路径 --sdk-directory build/m10a2-p6-final-01/sdk --core-build build/m10a2-p6-final-01/core --source-image build/m10a1/sanddata.img

该CLI没有execute选项，不写盘。实际安装只由Root现有GUI driver传入回调：

    from pathlib import Path
    from prepare_candidate import Candidate
    candidate = Candidate(native_report, host_report, sdk_directory, core_build, source_image)
    with QemuSession(candidate.core/'sandcore.img', candidate.source, gui_out,
                     accel='tcg', headless=True, audio=True, memory=256,
                     data_prepare=candidate.install) as vm:
        # Root现有会话初始化、普通SID、当前CORE符号、HMP与owner账在这里复用。
        # 执行已冻结GUI-FLOW，不调用旧P4/P5 main的专用通过条件。
        ...

Candidate.install拒绝原盘本身、硬链接和非build内会话sanddata.img，且先核副本等于固定原始SHA。它只调用一次prepare(...runtime_fixtures=False, metadata=...)，随后只读核计划payload/metadata回读及原盘SHA守恒。返回值明确为安装完成、guest执行尚待；旧prepare固定HOST来源字样另列legacy_helper_origin_labels，不冒称native/GUI验收。

当前旧 verify_m10a2_ui_gui.py main绑定P4五库/Demo报告，verify_m10a2_shell_runtime.py main要求P5_NATIVE_GENERATION_PASS；不能直接塞P6报告或删条件让其PASS。复用其中QemuSession/GuiGuest/RefreshHarness/ScuiInput等已有机制及GUI-FLOW动作即可。Sound代表动作需真实音频，示例audio=True；隐藏/后台/seek/cancel沿正常worker生命周期，不另起逐组件VM。

图像/音频/网络/字体未变部分复用已有证据，不因总CORE摘要变化扩矩阵。最终证据分别记录N真实生成、九库安装、当前native-bound CORE、普通用户实际GUI、原盘守恒和owner收束；安装/静态计划本身都不是M10a2完成。

修订：2026-10-16，只读整合精确CORE/SDK命令与单副本data_prepare候选；未执行。
