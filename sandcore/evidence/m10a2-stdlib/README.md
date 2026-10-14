# SCCLIB / SCLIB / R1 与当前 SCCC 真实证据

2026-10-15，**SandCore M10a2 — The Application Platform Era**。完整目标保持active；本目录只证明本批标准C profile、通用库和受控终止出口。SCUI、Desktop/Shell、主要组件和最终SDK/性能验收继续。

WSL `stdlib-build-06`退出0、39.36秒，实际输入在构建期间未变。[stdlib.json](stdlib.json)对应guest04，[native.json](native.json)对应guest05。宿主GCC引导库与系统内SCCC库来源分列：四份客体生成字节保存后，在另一次Windows双盘QEMU安装并实际执行。来源见[native-libraries/provenance.json](native-libraries/provenance.json)，逐文件摘要见[archive.json](archive.json)。没有把库正文静态复制进probe。

| 客体生成库 | 字节 | 导出/导入 | SHA256 |
|---|---:|---:|---|
| SCLDR ABI1.1/v2 | 57040 | 12/0 | 658b163f910bbe77cb1acf7887d7a7e694342542ec73edd6bdb1c7fc85873305 |
| SC32API ABI1.1/v2 | 82014 | 86/6 | fc47e0ac7c713c11358cb157a2bae892897a00bcac90b972913136c8c8eba81a |
| SCCLIB ABI1.0/v1 | 66892 | 94/19 | 9d1d7e308e48ce666343c5b0c922dcba3cddef9685bfa94b4858afe193095ea9 |
| SCLIB ABI1.0/v1 | 146165 | 163/30 | 2aa76e9695e39c002fa65e9e8777f83f4645b802c906b2465cf5eb70a310fdd5 |

实际出口包括allocator对齐/复用/合并/失败保旧、变参/Bool/offsetof/unsigned、字符串/转换/排序/格式，stdio六模式/逻辑位置/缓冲/EOF/flush保持打开。main借用缓冲的return37与深层exit38保持精确日志；atexit LIFO、回调内追加、Guard私有static代码pin及逆序fini真正执行。Guard init中exit42只结束已成功初始化前缀；递归exit触发固定诊断/异常1，不运行fini。共享STD原字节BA及位置2、空但尚有writer的pipe等待后读Q/真实EOF、空管理TTY等待后读Q均通过。

当前SCCC0实际生成G1：234101B，load234065/BSS3591864，SHA256 `dadf20e7f94370f3621b2f0a2006f49127bab23cade89fc19f7bc35ff4dd57bf`。G1实际编译标准库probe，生成app再真实运行core，并跨GCC Guard验证仅AL有效的Bool返回。宿主[compiler-host.json](compiler-host.json)前端/诊断和生成容量不冒充i386执行。

UTILPROBE只用正式公开头和动态导入：容器/COW/UTF8/path/config、buffer/stream/binary/真实FILE、error/logger、事件重入、model、cache/lease和真实malloc容量失败，共253检查。HOST库运行0.672秒，native库0.750秒；22B文件逐字节匹配。流程内heap为77/71页、modules50/83页，均是运行时实际成本；正常退出后外层PF差0。94/163是实际函数范围，不代表完整ISO C、全函数理论矩阵、大规模GUI或整机性能。

原基盘始终SHA256 `3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64`。HOST六组从guest02复用，guest04明确REUSED_PASS并核库/应用/Guard/主核/源码未变；失败shared未复用。guest05全native九模式和253检查均实际运行，FILE/STD/window/queue池当前页/对象回到基线。执行上下文为外部SYSTEM会话中的Ring3，不据此宣称全部普通UID权限或GUI行为已验。

三次具体失败保留：[冷桌面](failures/cold-desktop.json)功能全过但PF多2025；[Shell语法](failures/unsupported-shell.json)在旧Shell拒绝2>&1时app未执行；[重复领票](failures/double-job-close.json)为新helper成功Wait后重复Close错误84。首项后用真实HMP字段确认desktop.pages1793→3818、scaled_background.pages0→2025，与PF6965→8990一致，再等待旧异步资源完成并严格核应用暖态页数。后两项只修验证helper：ScProcessSpawn把同一真实端点传给子stdout/stderr，Wait成功已领票；旧脚本/源快照保留，库/内核未为三项改变行为或降低判据。

复现时Windows控制QEMU，编译在WSL。所有输出必须使用新目录/新标签：

```text
wsl.exe -e python3 /mnt/c/Users/Administrator/Desktop/projectos/sandcore/tools/build_m10a2_stdlib.py --tag <新标签>
python sandcore/tools/verify_m10a2_stdlib.py --out <首轮新目录> --build-report sandcore/build/m10a2-stdlib-bootstrap/<新标签>.json --utility-source sandcore/tests/m10a2/UTILPROBE.C
python sandcore/tools/verify_m10a2_stdlib.py --out <第二新目录> --build-report sandcore/build/m10a2-stdlib-bootstrap/<新标签>.json --utility-source sandcore/tests/m10a2/UTILPROBE.C --native-libraries <首轮新目录>/native-libraries --skip-library-build --guest-guard
```

两张1920×1080截图均已实际查看，展示新库执行结束后的既有桌面，不是SCUI/新版Shell外观验收。

![客体库运行后的既有桌面](native-desktop.png)

## 修订记录

| 日期 | 变更与证据边界 |
|---|---|
| 2026-10-15 | 保存统一构建、G1实际执行、HOST与全native库/通用工作流、精确回收、三项验证失败及源盘保持；完整目标继续 |
