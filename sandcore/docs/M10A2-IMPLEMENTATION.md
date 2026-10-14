# M10a2 实现与恢复入口

正式名称：**SandCore M10a2 — The Application Platform Era**。

2026-10-14 已按用户要求开工。基线 `main@8e1c27abfe1b0c14ddff6d23464a961eabfd8cc0`；完整目标见根 `M10a2RULE.MD` 和 `M10A2-DESIGN.md`。当前有未提交的实际源码，整个 M10a2 尚未完成。

## 当前状态

| 范围 | 源码/规范状态 | 真实验证状态 |
|---|---|---|
| Phase 0 总体架构 | 已记录旧加载器/SCCC/分页/安全基线、完整依赖及阶段出口，组件完整清单已写 | 源码审计，组件新运行验证待迁移阶段 |
| SCL v1 | 公共字节偏移头、freestanding C/Python 校验器、ELF32 宿主引导打包器及 dump/deps 已写 | 定向格式/宿主构建通过，真实 BASE/UPPER 各段/导入/重定位已运行 |
| SCLDR | 真正文件装载、迭代依赖/环检测、函数/数据导入、ABS/PC 回填、初始化/回滚、引用/地址固定寿命已写 | 宿主算法通过；Windows QEMU 两库真实调用、不同装载地址、显式卸载通过；Phase 1 仍未完整 |
| 模块分页 | 独立 VA 区、0x280..282 分配/保护/释放、syscall 输出 RW、授权调试特例、退出回收接线已写 | 定向 WSL 构建与单次双盘 QEMU 保护原子性/只读写回拒绝/显式和退出回收通过 |
| SCCC 动态编译/动态 SCX 启动 | 动态输出正在实现，计划沿用严格JSON显式导入/导出；自动启动导入尚待接通 | 新编译器代码未构建/未客体验证 |
| SC32API / SCCLIB / SCLIB / SCUI | 完整路线和引导依赖已确定 | 尚未建立完整四库 |
| Desktop/Shell/主要应用重制 | 完整范围保留，平台成熟后按次序推进 | 尚未实施重制/验收 |
| SDK/性能/统一验收 | 出口标准已明确 | 待平台及应用完成 |

## 文件归属与接续

主代理：`SCLDR.H`、`scldr.c`、`scl_platform.c`、总体文档/构建/guest 主控。内核切片已由前子任务交接：paging/task/process/image_service/ringdebug 及 MEM/SYSCALL，已停止并行编辑，主代理负责最终构建和回归。

用户同日规定 subagent 使用 **GPT-6.1 Sol + max**，当前格式工具、宿主测试、只读审查按此配置继续。子任务只写自己明确拥有的文件，模型切换不删除落盘成果；运行状态以工具实际记录为准，不假定会话中断后仍在后台工作。

SCL 宿主引导产物将明确标 `HOST_BOOTSTRAP`。宿主 runtime 测试只用回调模拟 init/fini，不运行 i386 库机器码。只有实际 Windows QEMU 运行结果才可登记三环调用和权限/回收通过。

## 下一步及结束条件

1. 收齐当前格式工具、两库样例与宿主定向测试，修复具体失败。
2. WSL 只构建当前 core/loader 和 SCL 引导 SCX/库，不通过 all/m10a1-artifacts 重编全部游戏。
3. Windows 双盘 QEMU 在独立验证盘执行真实两库函数/数据/字符串/不同地址回填，检查代码只读输入/输出拒绝、保护失败原子性、非法释放和页回收；达到这些条件就结束该轮。
4. 同批记录源摘要、日志、截图、真实范围与未完成项，然后继续 SCCC 动态输出及自动启动导入。首切片通过不代表 Phase 1 完整出口，更不代表 M10a2 验收。

原 M10a1、M9、M8a 与用户盘保持；没有本轮 Release 或最终验收申请。

## 2026-10-14 首个实际引导切片

仓库中的小型证据入口为 [`evidence/m10a2-bootstrap`](../evidence/m10a2-bootstrap/README.md)，保存下述实际构建、宿主/客体报告与原尺寸截图；数据盘仅留本机。

`build/m10a2-bootstrap/bootstrap-build-01.json` 记录 WSL ELF 构建退出 0、21.43 秒与逐项输入摘要。新 CORE 完整 SHA256 为 `30928d3dd5778bd9a66f7f2e2badc12c916179a1978f8e7e4a957e44e1e12f43`；载荷 383216B，BSS 332840B。BASE/UPPER 分别为 616/1428B，由宿主 freestanding GCC 与 mkscl 产生，明确不是 SCCC 客体编译产物。

`build/m10a2-runtime-host/report.json` 为宿主算法证据，使用真实 mmap/mprotect、UBSan 和模拟 init/fini；`executes_i386=false`。覆盖依赖/符号/版本、重定位、初始化/分配/保护失败回滚、DAG逆序清理、引用固定和两个上下文隔离。格式定向证据为 `build/m10a2-scl-fixtures/format-tests.log`，C/Python 的代表性拒绝码与不同基址回填一致。

`build/m10a2-scl-guest-01/scl.json` 为 **DECLARED_SCL_BOOTSTRAP_CASES_PASS**。只启动一次 Windows QEMU/TCG，软盘+256MiB IDE，使用一个独立会话盘：准备回调直接修改已校验的本会话副本，没有长期保留另一份准备盘。只替换该副本 CORE 并添加三个测试文件；原基盘及其余对象的正文/UID/GID/rw/代数保持。

实际执行结果：UPPER 调 BASE 函数、导入数据、内部指针/字符串与 ABS/PC32 正确；两上下文实际函数地址为 `0x50000000` 与 `0x50008000`，修改一份可写全局不影响另一份。只读库字符串可作为 STREAMWRITE 输入，GETARGS/MONITOR/STAT 写回均拒绝；跨缺页保护失败保持原可写权限；中间地址、旧 USERFREE 和重复释放拒绝。引用重复装载只初始化一次，返回地址固定后 Close 保留调用能力，未查询地址的 Close 真正卸载。

探针内显式卸载/Destroy 的 PF 页精确恢复。外部全局页账首轮正常运行为 6838→6844，保留这 6 页的首次暖态差额，不凭猜测归因为某项缓存；之后未 Destroy 的普通 EXIT 和真实 #PF 回调退出均为 6844→6844。#PF 模式实际返回 14，未将异常暂停或卡片关闭伪装为正常返回。这里仍未证明同时运行的多个进程或全部调试模块边界；后续按具体改动补齐。

原基盘 SHA256 `3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64` 在运行前后相同。原盘已有 SCX `ls` 真实读取新库目录通过。HMP 实际 Esc/F1 输入与原尺寸 1920×1080 桌面截图已保存并查看：`build/m10a2-scl-guest-01/desktop.png`；它证明新核心启动后的现有桌面，桌面重制尚未开始。

已修复静态审查发现的两个真实寿命问题：Close 按实际初始化链清理整个退役闭包；MODULEFREE 先撤销该地址块的断点/pending/异常入口，避免同进程虚址复用污染新库。后者当前有构建与静态核对，尚无完整 GUI Debug 动态模块流程证据。SCAPI.H仍与开工 SHA256 一致。

下一项继续 SCCC 动态输出/显式导入导出和动态 SCX 启动。完整四库、SCUI、Desktop/Shell 与组件重制/SDK/性能和统一验收的范围不变。

## 修订

2026-10-14：持久化真实开工源码、基线、文件归属、模型设置及未验证边界，便于会话中断后恢复。

2026-10-14：记录首次真实宿主构建、算法/格式证据和单次 Windows 双盘 SCL 引导验证；保留 6 页暖态差额和完整后续目标，不把局部通过称 Phase 1 或 M10a2 完成。
