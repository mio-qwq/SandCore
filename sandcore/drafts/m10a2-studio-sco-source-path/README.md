# Studio 消费 SCCC 的根内源名

GUI02 已经完成真实 RDSTATS 对象编译，截图可见 `SCCC OK SCO / Compilation complete`，随后 Studio 在 source-file 校验返回 `-108`，尚未进入最终链接。失败 SCO 已由正常 cleanup 删除；本草稿没有恢复它、重放 F5 或把其它对象说成这个失败对象。

真正的 producer 路径链可以从主源直接对上：

- `s3c.c:354` 明确旧 RESOLVE 返回根内、没有前导 `/` 的规范名，`--object` 共用此解析点。
- `sccc_include.inc` 从绝对工程 include 目录去掉一个根 `/`；默认标准头从 `SYS/INC/` 查找。
- `s3c_pp.inc::load_source` 保存实际找到的名字；`s3c_sco.inc:143` 把 `sources[i].path` 原样写入 16B source-file 行引用的字符串。
- `ScPathResolve` 的公开合同则把没有 `/` 的输入当 cwd 相对路径。Studio 原来直接调用 `path(name)`，会给这个已属根坐标的源名再叠一次 cwd，导致普通工程成员或系统头都可能被判成工程外来源。

实际留存的 `build/m10a2-p6-target-refresh-03/objects/STUDIO2.SCO` 有 32 个 source-file，全部非空、无根 `/`、不超过 63B；已直接按二进制表读取并记录 SHA/名字，不仅查看 map 文本。此事实佐证当前正式 SCCC 的坐标合同。SCO 通用磁盘规范里的 source path 仍是展示元数据，不授予文件访问权；本修复没有改格式、版本或给任意第三方路径加新授权。

`studio-sco-source-root.patch` 只改 `user/studio2/studio_build.c` 的源名输入适配：先在 `name[68]` 放 `/`，再把 SCO 字符串读进剩余 67B，仍沿原 `path` 做正规化与 UTF-8/非法字节检查。这样 cwd 不再介入。已有绝对名仍由相同根正规化处理；`..` 等 alias 仍先正规化再验证，不能用 `SYS/INC/../...` 绕过限定。

后面的完整工程成员比较、`/SYS/INC/` 识别、系统依赖真实 `ScPathQuery` revision、输入 pin/发布前复核和 alias 检查保持逐字节原文。没有按 basename 比较，没有跳过 source-file，项目外非系统头仍必须显式加入工程。顺便仅把已经失效的“最多64份source”注释改为按实际表枚举；没有新增数量门槛。

固定原像、结果、依赖、真实 SCO 表与截图摘要见 `PINS.json`。`prepare.py` 只向本目录生成 overlay/补丁并核对 GUI02 原像；主树、旧草稿、SCCC、SCO 公开头与 HOST/NATIVE manifest 都不修改。没有新动态导入；实际 refresh 标签仅 STUDIO2。Root 重生后应让正常 RDSTATS F5 通过依赖校验并进入真正 native SCLD 链接，再依据当前产物发布资格判断结果。

本次完成的是源码定位、真实旧 SCO 只读解析、文本补丁及 Python 语法/原像检查。未构建、未执行修后 Studio、未启动 VM，不宣称链接或 GUI 构建已通过。Notes 参数组继续保持冻结。
