# SCUI 平台第一轮检查点

2026-10-15，**SandCore M10a2 — The Application Platform Era**。完整目标仍未完成。本目录保存guest04第一轮已结束的原始报告、实际原生产物及必要日志；GUI guest01由Root独立进行，**本检查点没有GUI验收结论，也没有把新原生SC32/SCUI已生成写成已安装或执行**。后续第二轮报告/真实截图另行补入，不能倒改第一轮来源。

[ui-platform.json](ui-platform.json)原文件完整保存，状态为`DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS`。该报告SHA256为`dbad5d190a92bdb98f7518881412f5a977640644ecdd71d8debe868fa27dc672`；对应[ui-build-11.json](build/ui-build-11.json)退出0、28.573秒、`source_files_modified_during_build=[]`，报告摘要`fa45bfa50914afdee3ad843d44506391426a9fb6daa976bb8242ea02f989f7fb`。来源逐文件长度/摘要及原路径见[archive.json](archive.json)，不修改原报告中的历史绝对路径。

## 哪些已经实际运行

- Windows双盘QEMU，普通`su mio`真实登录，应用UID1000/realm0。HOST PLATFORMPROBE与G1 PLATFORMPROBE各87项，父/孩子不同PID和代数、同真实普通SID；真实clipboard/IPC/shared PTE/font/palette和基础能力通过。
- HOST及实际G1的SCUITEXT各比较官方Unicode15.1的3013条fixture、12948个双向边界，均退出0。**这些caller实际使用本轮SYS/LIB的五份HOST GCC引导库**，不据此声称新生成的SCUI系统库已执行。
- SCCC0在客体编译当前完整源得到G1；这个G1实际执行，生成两系统库、三应用和两AGG夹具。没有将宿主frontend产物复制为客体输出。
- 聚合ABI两个真实普通执行方向为G1Caller→GCCLib与GCCCaller→G1Lib，分别55 checks/failures0，callback反向跨工具链；栈观测分别`007ffd30→007ffd30`和`007ffdb4→007ffdb4`、delta0。原stdout为[case-024](stdout/case-024.stdout.bin)、[case-026](stdout/case-026.stdout.bin)。custom原生AGGCALLEE已执行，和新**system** library未执行两个字段分列：`generated_library_execution=true`、`generated_system_libraries_execution=false`。

实际owner/PF账在完整报告中。首次HOST平台9007→9008的1PF由只读计数/分配位证明属于全局空finite-wait首页缓存，未作为未知泄漏豁免；其余文本、两AGG方向、G1平台与G1文本各9008→9008，unexplained_delta0。Font冷成本、KEEP_IDLE历史池和真实owner/GC分别记录；这些数值不代表SCUI GUI资源已测。

## 实际G1与输出字节

| 产物 | 字节 | 导出/导入 | 第一轮状态 | SHA256 |
|---|---:|---|---|---|
| [SCCC1.SCX](native-artifacts/SCCC1.SCX) | 242574 | 编译器 | 生成并实际执行 | `50d152928d2eba50b46890d77ebe3295067bb372785a0db57708e26f752413a5` |
| [SC32API.SCL](native-libraries/SC32API.SCL) | 103863 | 130/6 | 生成，未安装/执行 | `a2935f7653070578a1f15562dd93bd099a0d114c6d09cea93ac08deeef271c7c` |
| [SCUI.SCL](native-libraries/SCUI.SCL) | 656592 | 158/137 | 生成，未安装/执行 | `62f2ba92f72ae2e7a9ebac33655300b25e76241f350f630e3ac309d5d3d0db33` |
| [PLATFORMPROBE.SCX](native-artifacts/PLATFORMPROBE.SCX) | 63265 | 0/52 | 生成并普通执行，HOST系统库 | `217bba7cab07ea46b27aac7abd73d78f27af8955b8d017484fd4eb25055a31e4` |
| [SCUITEXT.SCX](native-artifacts/SCUITEXT.SCX) | 5242 | 0/13 | 生成并普通执行，HOST系统库 | `8487372def93b0183894586fcfc6278566f395f65b5ca971f4fe25a04adfa637` |
| [SCUIDEMO.SCX](native-artifacts/SCUIDEMO.SCX) | 76096 | 0/133 | 只生成，GUI本轮未执行 | `18aece7952c5de804e9dd2ffbd0c5b0a2440f1bb5d788336a075240f5f5f59a3` |
| [AGGCALLEE.SCL](aggregate-abi/AGGCALLEE.SCL) | 1947 | 9/0 | 生成并交叉执行 | `ad6fb1b88362750f2e9ae019112b3442c08a42016785c4defd25c69b9b92943e` |
| [AGGPROBE.SCX](aggregate-abi/AGGPROBE.SCX) | 6066 | 0/10 | 生成并交叉执行 | `71f53350b90f0d7492c5e9b728b852098c3c909462733e95eec07ab895e7d1fe` |

G1旧SCX容器load242538/BSS2602680/stack131072，仍在load262108与私有映像0x3d0000限制内。两份系统库的来源原文在[native-libraries/provenance.json](native-libraries/provenance.json)，G1及三应用在[native-artifacts/provenance.json](native-artifacts/provenance.json)。两份小GCC AGG counterpart在`aggregate-abi/host/`，其摘要由build11原证明绑定；这里不将它们标为G1产物。

## HOST预检与保留的诊断

[host05 report](host/05/report.json)是`ACTUAL_SCCC_HOST_ADAPTER_UI_SYNTAX`，exit0、13.654秒，`executes_i386=false`、`executes_generated_compiler=false`、`guest_compilation_or_execution=false`。它使用真实当前前后端的I/O适配器预检6个输入，不执行生成的i386；SCUI当时types2806/symbols7659/params3007/fixes4491。它绑定[build10](build/ui-build-10.json)，不是guest04的build11。SCUI/三个主要app当时输出摘要与本轮实际G1输出相同，但AGGPROBE后来改为固定栈余额观测：host05的5948B不能冒称等于guest04的6066B。原HOST日志完整放在`host/05/`，没有复制host VFS或适配器源码树。

三次实际客体失败保留完整原报告和原stdout，未裁剪成PASS：

| 诊断 | 原文件 | 实际失败边界 |
|---|---|---|
| guest01 | [完整报告](diagnostics/guest-01/ui-platform.json)、[stdout](diagnostics/guest-01/failure.stdout.bin) | HOST平台行为通过后，原页账未解释finite-wait首页，差1PF；后续按真实四字段/分配位修正归属，失败原证据保留 |
| guest02 | [完整报告](diagnostics/guest-02/ui-platform.json)、[stdout](diagnostics/guest-02/failure.stdout.bin) | 实际G1编SCUI在ui_data.c:815遇compound literal语法；不能只凭GCC构建称原生通过 |
| guest03 | [完整报告](diagnostics/guest-03/ui-platform.json)、[stdout](diagnostics/guest-03/failure.stdout.bin) | 第一个AGG方向55项中exact-caller-stack-balance失败，inner退出1；guest04实际两方向delta0另列，不覆盖失败记录 |

此外`host/diagnostics-01/`保聚合参数未支持的真实frontend诊断，`host/diagnostics-03/`保ui_edit symbol容量诊断，`host/diagnostics-04/`保HOST适配器观察锚点失败；后一项是宿主观察工具问题，不写成客体编译器或系统故障。相应原report/log都保留。

## 保存边界与下一步

本检查点的74份原文件总5426861B（约5.18MiB），另有这份README和摘要索引。完整成功JSON只存一份；总console/debug/编译log/map及case stdout保留，未复制2MiB原始管理协议流、session QMP大副本、完整VFS、源码树、任何磁盘、P3重复库，也不清理或删除原产物。

原M10a1数据盘SHA256保持`3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64`。本批report记录source_disk_unchanged/current_inputs_unchanged为true，changed_input_output_paths为空；复制每个文件前后再次核原文件及副本SHA。当前源码/接口继续冻结，保存证据不触发重编或新VM。

ROOT/SCCLIB/SCLIB/UTIL/R1的既有实际来源与验收复用[P3证据](../m10a2-stdlib/README.md)、[原生库provenance](../m10a2-stdlib/native-libraries/provenance.json)、[native执行report](../m10a2-stdlib/native.json)，不在这里重复复制。旧M8、M10a1和其它未受影响证据保留。

第二轮须真正安装本轮native SC32/SCUI和P3 native ROOT/STD/SCLIB五库，执行普通独立桌面的SCUIDEMO及主题/DPI/输入/文档/对话框/损伤/隐藏恢复/回收；Root会补真实新report及PNG。首轮desktop图仅是首帧/桌面上下文：[原PNG](ui-platform-first-round-desktop.png)，不是SCUI演示截图。当前`GUI_behavior_verified=false`、`full_M10a2=false`，完整控件/桌面重制/SDK/性能与剩余应用目标不因该检查点缩减。

## 修订

2026-10-15：保存guest04完整第一轮及实际G1字节、两个聚合交叉、host05与三次真实失败的小型检查点；来源/生成/执行及未安装新系统库边界分列，GUI结果另补。
