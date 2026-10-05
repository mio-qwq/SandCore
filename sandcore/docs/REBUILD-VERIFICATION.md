# 从源码快照重建：2026-10-05

用户要求GitHub备份既完整又能重建。此次在两个独立Git干净克隆中构建，
目录不含本机原M7/M8a大ZIP，也不借用原build中的对象文件或资源输出。
源码输入、精简基线、字体及第三方固定源码全部来自Git；编译工具由宿主安装。

## 入口与依赖

Windows仓库根目录运行`build.bat`默认M9；历史版本运行
`build-version.bat M6a`、`M6`、`M7`或`M8a`。
M9输出在`sandcore/build/m9-work/`，历史输出在`rebuild-output/<版本>/sandcore/build/`。
历史输出目录已经存在时拒绝覆盖，可传`--out rebuild-output/<新目录>`。
根M9 BAT和历史M6a BAT本身均实际执行通过；BAT保持纯ASCII。

依赖：WSL Ubuntu中的GCC、GNU ELF binutils、Make、NASM、Python3；资源生成需要Pillow，
可安装WSL `python3-pil`，或把带Pillow的Windows `python.exe`放入PATH。
本次实际工具为GCC 15.2.0、NASM 3.01、Make 4.4.1、WSL Python 3.14.4，
资源工具自动选择Windows Python 3.12/Pillow；运行验证用Windows QEMU 11.1.0。
这些宿主工具不混入系统组件源码或构建包。

## 实际结果

| 版本 | 构建输入核对 | 结果与证据 |
|---|---|---|
| M6a | 原始快照63文件逐项SHA256 | [构建通过](build-evidence/2026-10-05/M6a.json)、[日志](build-evidence/2026-10-05/M6a.log) |
| M6 | 原始快照92文件逐项SHA256 | [构建通过](build-evidence/2026-10-05/M6.json)、[日志](build-evidence/2026-10-05/M6.log) |
| M7 | 原始快照132文件逐项SHA256 | [构建通过](build-evidence/2026-10-05/M7.json)、[日志](build-evidence/2026-10-05/M7.log) |
| M8a | 原始快照580文件逐项SHA256；仓库内M7兼容基线 | [构建通过](build-evidence/2026-10-05/M8a.json)、[日志](build-evidence/2026-10-05/M8a.log) |
| M9 | 当前源码、225项第三方归档、仓库内M7/M8a基线及字体 | [构建/运行通过](build-evidence/2026-10-05/M9.json)、[日志](build-evidence/2026-10-05/M9.log) |

每版都实际生成启动盘与数据盘，尺寸/SHA256保存在JSON中；构建前后原快照摘要相同。
`.gitattributes`关闭Git自动换行转换，防止Windows克隆改变第三方固定源码和历史快照。
第一次克隆确实因自动换行改变stb声明摘要而停止；加上此规则后重新干净克隆构建通过。

M9额外验证：

- 新生成启动盘与既有build31验收启动盘完全相同。
- [17项真实串口检查](build-evidence/2026-10-05/M9-serial.json)通过：上传/下载正文、空文件、失败恢复、摘要、进度/速率/ETA、源盘不改。
- [系统内SCCC编译运行](build-evidence/2026-10-05/M9-native.json)通过：串口上传独立C程序，生成SCX，实际运行输出标记并返回预设23。
- [兼容静态核对](build-evidence/2026-10-05/M9-compat.json)通过：M7的40个公开函数/29宏、M8a的76函数/59宏、旧task布局和16份LEGACY原字节。
- Windows QEMU实际执行HMP `sendkey shift`及`screendump`，[1920×1080桌面](build-evidence/2026-10-05/M9-desktop.png)已查看；两张源盘均未修改。

![干净构建的M9桌面](build-evidence/2026-10-05/M9-desktop.png)

## 重建与原交付的区别

M6a/M6/M7直接运行原快照Make规则。M8a只在独立副本中替换旧M7字节读取脚本及其
原ZIP文件前置条件，C/ASM/头文件/资源生成代码保持原快照；原快照本身不变。
M9读取器从仓库内精简基线镜像取已发布的旧SCX/资源，并从对应源码快照取历史头文件，
分别验证精简包、原来源和每个读取项的摘要；不会假称精简ZIP具有原大ZIP的摘要。

历史Make构建验证的是宿主启动/自举工程。原发布盘还有当时在SCCC中原生编译的应用、
对应发布接线和用户数据，因此不宣称所有历史数据盘与原发布盘逐字节相同。
全部原生应用源码、include、原生编译/发布工具和文档随快照保存；原交付镜像每版另保留一份。
M9默认数据盘也不重复原验收盘内的临时夹具/会话文件，完整专项验收仍见[M9-ACCEPTANCE.md](M9-ACCEPTANCE.md)。
本次没有重新执行历史M6/M7/M8a的整套界面/性能验收，也没有重启已封存M8的开发或渲染。

修订：2026-10-05，五版干净源码构建通过；记录M9串口、客体编译执行、旧ABI及HMP截图证据与范围。
