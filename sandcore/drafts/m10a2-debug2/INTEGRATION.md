# 主代理整合说明

本目录的源码与补丁只供主代理审阅/整合，本代理没有修改正式 kernel/user/tools/Makefile、没有运行 Git 或 QEMU。`prepare_integration.py` 仅读主树，写本目录补丁和 `build/m10a2-debug2-agent-check/integration/` 检查副本；它不会应用补丁。

## 文件与版本

- `public/SC32DEBUG.H` → `user/SC32DEBUG.H`，纯公开 typed 调试合同。
- `user/debugapi/` → 同路径，私有 wire 与 SC32API 库入口；不安装 wire 到 INC。
- `kernel/sc32_debug.{c,h}`、`sc32_debug_modules.inc` → 同路径，动态断点/正式来源核验登记/分页查询。
- `user/runtime/scl_debug_registry.c` → 同路径，ROOT 自己的私有桥。
- `user/debug2/*` → 同路径，真正 SCUI 原生应用；`DEBUG2.json` 从实际 GCC object 的 undefined 符号逐项匹配公开库导出生成。
- `docs/SC32-DEBUG.md` → 同路径，公开 ABI/错误/运行期登记/GUI 合同。
- `user/SCDBGFILE.H` 的唯一源来自另一个对象工具链草稿 `drafts/m10a2-object-toolchain/SCDBGFILE.H`；补丁包含其完全相同字节，不另设格式。

`integration.patch` 包含14处已有文件的明确接线及上述新文件。它提升 SC32API 到 ABI1.4 / v6、ROOT 到 ABI1.2 / v3；SC32API 的 ROOT 依赖相应 min1.2/v3。对象工具链代理的 SpawnCapture 同样约定 SC32API1.4/v6，整合时把其导出一起加入现有 manifest，不能用本补丁覆盖它的新增入口。

私有 selector 为 0x900..0x91F，不占其它代理的 FILE 0x20B、HOST 0x800 或既有公共号。旧 SCAPI.H / old debugger / SCDBG / scdbg 没有被改写或替代。补丁在 process.c 中只接新钩子及私有 owner/旧断点辅助函数，旧 DEBUG 寄存器语义、旧八槽及原 TF/INT3代码保留；新 typed 断点有自己的动态拥有链。

SDK 接线加入 SC32DEBUG.H、SCDBGFILE.H 和 SC32-DEBUG.md，源码来源检查含 debugapi。ROOT 的实际来源闭包还显式固定新的私有 wire / 公开布局；这不是把 wire 安装进公共 INC。正式构建记录和镜像 staging 还需主代理把新源/头/文档纳入实际输入清单。

## 可信加载器接线

1. `task_exec_scx_parts` 在实际加载事务中调用 `sc32_debug_main_image`，固定完整文件 SHA256、精确 load/BSS 和真实来源；失败停止本次子任务。
2. `scl_boot_map` 在 root_copy 之后、首条 ROOT 用户指令之前直接调用 `sc32_debug_bootstrap_image`；失败回滚登记与映射。
3. ROOT 的 Adopt 只借该精确已登记映像，不把已经被 loader 使用过的 BSS 再要求为零。此差异在静态生命周期审阅中发现并修正。
4. 其它默认 SCL 在 protect/reloc 后、init 前经私有桥按64KiB步骤登记，PENDING之间真实yield；成功init提交READY，实际释放前撤销。SCLDR不导入SC32API。
5. MODULEPROTECT 更新映射/模块epoch；MODULEFREE/异常/退出撤销；清理阶段4通过既有 owner链每步回收一个新对象。无固定总量数组，分页直接跟随稳定拥有链，total来自真实增删计数。

不要只复制 Debug2.SCX 就称新调试器已接通；它需要同批新 CORE、ROOT 和 SC32API。SCUI、SCCLIB、SCLIB本次没有修改行为，可以复用来源仍匹配的既有正式库。

## 构建与启动

本目录的宿主可复现命令：

```
wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore python3 drafts/m10a2-debug2/check_host.py
```

该命令不启动客体。它构建当前草稿及精确 adapter 副本、按正式 mkscl/mkdscx 生成真实 HOST_BOOTSTRAP 产物，并跑解码器 sanitizer 检查。生成头只读复用现有 `build/font*_data.h` 和当前 P5 已生成的 `shell_host_issue.h`，不复制整盘、不触及用户私钥。

正式合入后定向目标是 `m10a2-debug2-artifacts`。内核加入 `sc32_debug.o`；ROOT、SC32API、新公开头与实际库manifest同批构建。SDK/镜像中安装 `/APPS/DEBUG2.SCX`，Studio的入口合同为：

```
/APPS/DEBUG2.SCX --launch /absolute/TARGET.SCX [target arguments]
```

Debug2自己以typed Launch创建真实 paused owned process。SCL通过真实SCX宿主加载。命令首路径遵循既有streams无空白路径合同；其余目标参数保留原串。GUI默认目标stdin/out/err为空设备；现有交互CLI调试继续保留独立scdbg，typed Launch本身支持已有描述符。

## 必要收尾范围

只验证本批新增风险，不重跑整个M10a1/M8矩阵：

1. 用真实目标验证首指令暂停、F8真实TF、F5/F4、至少9个同时存在的软件断点、命中重插/删除当前IP断点、退出/重启与作业回收；核目标引用代数及错误stop代数拒绝。
2. 用真实SCL目标检查ROOT和依赖的实际映射/ABI/SHA/导出；加载/释放改变epoch、坏来源/坏重定位不登记、sidecar SHA和地址域/段越界拒绝。注册中的候选ID不能被Symbols/Segments猜测查询。
3. 一次GUI工作流覆盖不可读内存、地址/分支/栈导航、Modules/Symbols/Breakpoints、module属性、拖Splitter/浮动停靠/小窗口滚动、Classic/Aurora与隐藏停帧。记录真实截图；不能把宿主打包当GUI验收。
4. 因 process异常/owner链加钩子，最小复查一个旧scdbg TF/INT3流程与原字节hash；其它未改库的证据按源/依赖匹配复用。
5. 记录本批实际装载/暂停刷新/内存回收成本。模块cache按module_epoch失效，分页无全链重复扫描，但这些结构不能代替QEMU性能数据。

完成后由主代理同步 README、SYSCALL/SC32API/DEBUGGER/SDK、对应组件状态和修订记录；未做部分保持待验，不宣布整个M10a2完成。
