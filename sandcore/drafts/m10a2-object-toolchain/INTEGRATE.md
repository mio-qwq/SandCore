# 主代理整合说明

所有本任务写入限定于 drafts/m10a2-object-toolchain 与 build/m10a2-object-agent-*。没有改正式user/kernel/tools/Makefile，没有Git、QEMU或删除操作。

1. 核验 compiler-pins.json 的原四份编译器文件SHA，应用 compiler.patch。新增 s3c_sco.inc 与 SCOFMT.H 到 user；SCDBGFILE.H 是正式公共调试格式头，和 Debug2 统一安装。补丁保留当前 default-label 修正及真正聚合/sret ABI，未替换对应实现片段。
2. 将 scld.c 放正式 CLI 源，保持 runtime/SCLFMT.H/SCDYN.H/scl_format.c/scd_format.c 的相对包含路径。它们是现有共同验证器，不新造不兼容validator。普通SDK INC继续不暴露库/内核私有实现；开发者需要的公共格式头为SCOFMT.H/SCDBGFILE.H。
3. 新增 SCLD.SCX 的 HOST_BOOTSTRAP/原生构建规则。SCLD.json 明确 SC32API(ABI1,min_minor3,min_version5) 与 SCCLIB(ABI1,min_version1)22项真实imports。现G1能够按单源 `sccc /SYS/SRC/SCLD.C /HOME/SCLD.SCX --dynamic /SYS/SRC/SCLD.JSON` 生成它；可将新编译器先按现统一流程引导，保持实际来源明确。**不要用安装脚本或host编译器替代客体编译证据。**
4. Studio工程每个源分别运行 `SCCC source output.SCO --object`，本次全部成功后运行 `SCLD OUTPUT --dynamic/--scl manifest @objects.rsp`。响应文件只列对象；工程的源保存代数、对象配置、清单和最终产物绑定由Studio管理。链接器成功stdout `SCLD OK ...`，失败stderr `SCLD ERROR object:0: reason: symbol`，错误对象可导航工程。
5. SDK提供 examples/ 的两个TU与应用/库清单，并安装正式SCLD、公共头、已有系统SCL。真实原生SDK验收需由旧G1/新G1生成对象→SCLD链接→应用执行，SCL另需加载init/fini/符号和资源回收；最低一轮覆盖BSS地址实际4B对齐、两份static同名、缺失/重复保留正文。现有未变平台证据直接复用。

复现宿主定向检查（在sandcore）:

```sh
python3 drafts/m10a2-object-toolchain/tests.py
```

tests.py 从原有host_check只取已存在的 I/O adapter，实际前端/后端来自摘要固定的本源码；原生链接器宿主检查编译同一个 scld.c 只切换参数/路径入口。脚本固定只写本目录与 build/m10a2-object-agent-*，没有镜像、QEMU或发布。

8项检查记录 build/m10a2-object-agent-tests/report.json 与 commands.log。execute-scx.elf 是本次原生SCO链接结果的真实x86函数入口；execute-scl.elf 是本次多对象SCL在0x500000按既有格式模型回填后的真实库函数；execute-aggregate.elf 实际执行跨TU按值结构输入、hidden-sret与原输入不变。还明确检查SCO记录16B对齐后，metadata末的实际BSS地址保持16B。测试仅提供Linux退出桥并跳过SandCore CRT，不是SandCore装载器/stdio/客体验收。

实际后端容量记录见 report 最后一项。当前原生SCLD SCO→SCX约73KiB，单源SCCC→SCX另记。扩展编译器 load254319B、完整254355B、余7789B；原上限262108B没有改变。此数字是本次真实SCCC后端字节生成，**不是它已经在客体执行的G1或固定点结论**。

修订：2026-10-16，精确补丁、公共合同、原生CLI与宿主检查交接，整合与客体来源待主代理。
