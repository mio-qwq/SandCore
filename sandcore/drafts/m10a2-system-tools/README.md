# 三个原生系统工具重制入口

此目录含 Calculator 2、Palette 2、Monitor 2 的实际 Ring3源码、精确动态清单和定向构建片段。设计见 `DESIGN.md`，当前证据见 `STATUS.md`。旧 calc.c / palette.c / monitor.c、正式镜像、main、Git及已经冻结的Files2目录均未修改。

三个入口分别为 `CALC2.C`、`PALETTE2.C`、`MONITOR2.C`。对应大写私有头和模块化 `.inc` 负责应用数据；共用 `TOOLS.H/tools_ui.inc` 只构造/调用公共SCUI控件和命令，没有复制控件算法。`calc_engine.inc`是独立32位表达式引擎；`monitor_model.inc`是分页与有预算的索引模型。所有应用经真实SCL导入SC32API、SCCLIB、SCLIB、SCUI、SCSHELL。

## 工作流

- **Calculator 2**：优先级/括号/一元正负、十进制及0x十六进制、+ - * / %、INT_MIN/除零/溢出守卫；标准Document/TextBox选区与撤销、动态历史表、恢复表达式、ANS、十进制/带符号十六进制跨应用复制。输入4096B/嵌套64层，拒绝时保留输入；不声称浮点或科学计算。窄窗口收起历史与次要栏，键盘在标准ScrollView中仍可访问。
- **Palette 2**：实际256槽快照、GFX登记分组、原0保留/1–104旧可达/105–255未分配口径、选择SpinBox、可排序/调整列宽/过滤的TableView、HEX/RGB复制和实际色样；24个主题角色另列。小窗口保留表格及当前HEX/RGB，侧边大色样仅在足够空间显示。未读取的颜色显示不可用，失败保留旧快照并标记。三种程序壁纸保留真实持久化及主题图片优先提示。
- **Monitor 2**：动态ProcessPage双缓冲、真实PID+代数、异步分页/排序/过滤、选择属性/确认终止/复制；PIT差分CPU、全局内存与本Monitor映射页、真实卷字段、120点独立有效位CPU/内存历史；冻结/继续/手动刷新。隐藏约5秒后台采样并停止图表生成，显式冻结停止定时采样；恢复重建基点。小窗口仍可切到完整CPU/内存历史页面，失败不画伪零。

Palette的旧1/2/3热键仅在表格有焦点时保存壁纸；全局使用Alt+1/2/3，输入槽号不会误改背景。Monitor表格Space保留冻结功能，全局Ctrl+Space；过滤框的空格照常输入。所有用户操作菜单/键盘/按钮共用真实命令，Classic/Aurora共享行为与命名色角色。

## 构建与整合

```powershell
wsl.exe sh -lc 'cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore && python3 drafts/m10a2-system-tools/check_host.py'
```

只写 `build/m10a2-system-tools-agent-host/`，生成i386对象/动态SCX、壁纸组合语法源、日志/报告和纯数学ASan/UBSan程序；不启动QEMU、不生成数据盘、不改内核或主库。也可显式追加：

```sh
make -f Makefile -f drafts/m10a2-system-tools/system-tools.mk \
  BUILD=build/m10a2-system-tools-agent-integration m10a2-system-tools-artifacts
```

Calculator/Monitor依赖SC32API ABI1.3/v5及SCUI ABI1.1/v2；Palette的两个壁纸函数待Root并入统一SC32API **ABI1.4/v6**。接线见 `wallapi/INTEGRATION.md`，独立公开头 `SC32WALL.H`、私有库桥/只读core query/实际wm snapshot片段与固定来源摘要已经提供，没有用占位实现凑运行成功。

Root安装时先保留旧SCX原字节，再将新容器分别接为 `/APPS/CALC.SCX`、`/APPS/PALETTE.SCX`、`/APPS/MONITOR.SCX`；复用现有字体/Unicode/图标及库资源。wallpaper query selector0x006；Set复用旧0x18，不改旧SCAPI。新版待真实键鼠、双盘/权限、主题/缩放、隐藏与回收的定向客体验收。

Monitor不把PIT Hz当FPS，不把Gfx与Kernel重复相加，不把本Monitor页数称目标RSS；各分页和全局查询不是原子快照。现代ScSystemStorage未公开的旧ATA历史洞字段不伪造，设备/空闲等以真实扇区计数显示。普通root不因此获得硬件SYSTEM权限。
