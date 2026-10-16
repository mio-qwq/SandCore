# P6 客体原生生成 driver

实现已落盘；本任务没有启动 QEMU。入口 `driver.py` 默认只预检，只有主代理
随后显式给 `--execute` 才会通过现成 QemuSession 启动 Windows QEMU。
固定 /SYS/SRC 调试源闭包和 /SYS/INC 公共头路径可供最终 Debug2 打开。
最终SDK的正式源码位于SYS/SDK/SRC/user；Root同批保留这份少量SYS/SRC
调试映射，不在driver里改路径或重写SCDB。没有更改主树、Git、原盘或旧草稿。

更新后的只读宿主检查已完成27个CASE、8个普通库、322个源文件和47个当前
SDK公共头，证据在build/m10a2-p6-guest-generation-agent-check-02/report.json。
最长准备源码路径44B；预检还会读取实际HOST SCO的原始include路径，检查
换/SYS/SRC前缀后的真实路径也不超过63B。复用i386布局提取器读得task_record320B、generation168、
name28、name容量12、state20、pool.stride1088。已有真实宿主SCO/SCLD/SCDB
产物也通过新parser的逐源字节/FNV、符号/行/对象和实际布局复核。这些结果
不是新客体生成、运行或完整M10a2验收。最终HOST inputs/pins尚未提供。

## 主代理需要提供的唯一配置

`schema=1` 的 JSON，字段严格为下表。全部 `pin` 都是
`{"path":"仓库相对路径","sha256":"64位小写完整SHA","bytes":实际长度}`。
不会自动猜最新目录、接受空SHA或用HOST产物冒充native。

| 字段 | 内容 |
|---|---|
| schema | 1 |
| build | 最终统一HOST构建目录的仓库相对路径 |
| build_report | pin；成功报告，含exit_code=0、输入摘要和outputs(path/bytes/sha256) |
| source_sha256 | 最终build的实际输入摘要字典，至少覆盖完整源闭包、布局五头、kernel/objpool.c、task.c、desktop.c、ttf.c；每项必须在build报告inputs或inputs_sha256中一致 |
| p6_report | 一个pin或pin数组；现成check_m10a2_p6_objects.py的报告 |
| data | 原用户v5盘的pin，只读来源 |
| bootstrap_files | 显式guest映射数组，见下段 |
| sdk_plugins | 现有SDK插件spec的pin数组；从其public_headers读取额外公共头，可为空 |
| task_layout | 当前同源i386 layout.json的pin，生成方法见后文 |

build报告至少需要同一构建的 sandcore.img、core.sym、core.elf、core.bin、
fs/SYS/CORE/CORE.SKM 真实输出记录。CORE payload必须等于core.bin；ELF与
CORE的入口、image、BSS边界按已有inspect_elf/check_main核对。
同build的login_program_issue.h、login_resource_issue.h、shell_host_issue.h
必须在该成功报告各有唯一记录；还直接比对实际CORE载荷和core.sym中的
login/desktop摘要及六项资源表。它们必须精确绑定将安装的HOST LOGIN2、
五库、UAX和HOST桌面字节，不只相信另一个同名头文件。

bootstrap_files每项为：

```json
{"guest":"/SYS/LIB/SC32API.SCL","artifact":{"path":"build/FINAL/SC32API.SCL","sha256":"ROOT填写实际摘要","bytes":0},"origin":"HOST_BOOTSTRAP"}
```

上面的值仅示意结构，不能通过预检。origin仅接受HOST_BOOTSTRAP、
HOST_CODEC_EXCEPTION、RESOURCE。所有HOST代码必须列在最终build的真实
outputs中；SRC/INC不允许通过该数组覆盖。需要HOST库SCLDR、SC32API、
SCCLIB、SCLIB、SCUI、SCSHELL、SCGFX、SCTERM和SCSOUND，后者必须标为
HOST_CODEC_EXCEPTION；需要精确`/SYS/CORE/LOGIN2.SCX`和
`/SYS/SHELL/SCSHELL.SCX`。字体、UAX、UI资源、许可等也应显式映射，未列
对象仍由现成prepare逐项核原文字节/UID/GID/mode/generation不变。

P6报告允许按实际未变源码复用多个已保存成功case；不要求因另一个旧case
失败而重跑已有效的输出。每项必须是当前完整原文闭包、compile/link返回0、
status=PASS，且当前正式`CASE.NATIVE.JSON` SHA与原记录相等。case整体必须
覆盖当前CASES全部27项，包括SOUNDWORK、SESSION2、LOGIN2、SCCLIB和SCLIB；有同源冲突
则拒绝。清单的身份/版本/导出和原源清单相同，仅采用真实SCO得到的imports
及依赖。guest再次校验实际SCO imports，避免只信HOST列表。

SCCLIB/SCLIB旧native依赖的SC32API.H与当前1.4/v6头不相同，不能伪写旧SHA
以通过严格来源。Root已批准同VM各做一次SCCC→SCO→SCLD，八个普通库
全部有当前接口的新真实来源。原P3标准库功能/性能证据保留，不新增或重跑
功能矩阵。VM运行库仍是上面显式发行的HOST bootstrap。

## 准备和运行

先用WSL生成真实i386布局，只执行现有宿主gcc/objcopy读取头布局：

```powershell
wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore python3 drafts/m10a2-p6-guest-generation/layout.py --out build/m10a2-p6-guest-generation-agent-layout-FINAL
```

主代理固定上述config后，用Windows Python只预检：

```powershell
python drafts/m10a2-p6-guest-generation/driver.py --config build/PINNED-CONFIG.json --out build/m10a2-p6-guest-generation-preflight-FINAL
```

本任务不执行下条命令。主代理显式启动时必须给另一个全新输出目录：

```powershell
python drafts/m10a2-p6-guest-generation/driver.py --config build/PINNED-CONFIG.json --out build/m10a2-p6-guest-generation-run-FINAL --execute --accel tcg
```

QemuSession复制原盘一次，prepare只在这个会话副本内重建必要目录，原盘
不挂入QEMU。现成prepare内部使用原子替换的临时重建文件；最终仅保留
同一会话数据盘，不按组件复制256MiB盘。先warm现有Shell/桌面及两个持久
Shell变量，再整体计一次quiet/owner前后账。不会在每个小文件后重做资源矩阵。

每条producer通过现成Guest.dialog和真实Shell `$!`/read屏障取得PID；只读
暂停观察本批实际task_record代数/工具名，快速结束时可读已提交KEEP_IDLE
历史，拒绝同PID/代数重复，然后ACK并wait。没有写内核状态、放宽Broker或
模拟命令结果。PID观察和串口/窗口/ledger实际行为仍待这次Windows运行。

旧G1固定来自ui-platform-guest-05原报告，完整SHA
`dd7ba9d45fbd1abbafa3f5468dc2d9ee5822625aca052df2e6655945aa302138`，
原报告完整SHA、真实producer/stdout与SCX边界都会核实。其生成的新
`TOOLS/SCCC1.SCX`是当前SDK compiler槽名，明确记录chain_depth_after_HOST=2，
不会把它当成旧G1原字节。该新工具单源生成BOOT/SCLD.SCX，再用这一个
native seed链接全部独立SCO；最终SCLD也单独重链在OUT/SCLD.SCX。

结果在p6-generation.json、native-libraries/、native-artifacts/、objects/。
每个实际命令保存stdout、退出值、QEMU PID、进程代数、工具SHA和源闭包；
现成file方法回收完整文件及文件代数。每个SCO有log/map，每个最终链接
产物有map和append .sdbg，逐项比较正式SCL/SCD合同及真正SCO的段/符号/
源行身份、image SHA和地址域。bootstrap编译器/seed沿旧容器只取现有
log/map，不伪称已生成该路径尚不支持的SCDB。

新库、新桌面/应用、最终SCLD只取回，不在本VM安装/执行；实际执行的生成
工具在generated_tools_executed中单独记录。原生LOGIN2/五库的issuer与CORE
须下一运行VM重生。状态只可为DECLARED_P6_NATIVE_GENERATION_PASS或失败，
始终full_M10a2=false；程序不会主动发布、装SDK或改主树。

修订：2026-10-16，P6单VM原生生成driver实现及三项只读宿主检查；最终pins/客体生成/运行尚待。
