# 历史源码快照

M6a、M6、M7、M8a来自既有源码/交付档案。`releases/MANIFEST.json`逐文件记录
原始尺寸与SHA256，快照保留原字节，`.gitattributes`禁止Git自动换行转换。

在仓库根目录运行`build-version.bat M6a`、`M6`、`M7`或`M8a`。
输出在`rebuild-output/<版本>/sandcore/build/`；已有输出不覆盖，
可用`--out rebuild-output/<新名称>`选择新目录。WSL命令等价于
`python3 sandcore/tools/rebuild_version.py M8a --jobs 4`。

M8a原构建规则硬绑定本机原M7 ZIP。重建工具只在独立工作副本中替换旧字节读取脚本
及这条文件前置条件，改从仓库内已校验基线读取；快照的C/ASM/头文件/资源生成代码不改。
宿主Make产物是启动和自举工程，不等于历史SCCC原生发布盘的逐字节重现。
原生编译/发布工具及全部应用源码随快照保留，原交付运行盘另在`releases/`。
M6a是早期源码基线，没有冒称独立正式发布包。

五版实际重建证据见[REBUILD-VERIFICATION.md](../sandcore/docs/REBUILD-VERIFICATION.md)。

修订：2026-10-05，历史快照摘要核对及独立重建入口验证通过。
