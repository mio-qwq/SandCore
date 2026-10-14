# SCCLIB / C1..C5 / R1 定向探针

2026-10-15，SandCore M10a2 — The Application Platform Era。

**探针已统一构建、客体SCCC生成并实际执行。** [真实证据](../evidence/m10a2-stdlib/README.md)包括九模式、全native库/Guard、GCC仅AL有效Bool及当前G1编译执行。build01/02的-Werror失败保留；guest02旧Shell不支持2>&1、guest03helper重复Close的失败也保留。最终使用正式SHARERUN启动共享端点；完整M10a2继续。

## 1. 产物与安装

源码为 `tests/m10a2/STDLIBPROBE.C`、`STDLIBGUARD.C`，分别使用同名 `.json` 清单。应用只包含十一份小写标准头和正式 `SC32API.H`；没有SCAPI、内联系统调用、库正文或私有ROOT adapter。Guard为独立SCL，五个函数导出、五个SCCLIB函数导入和一个SC32函数导入，init/fini有真实正文。应用清单有59个函数导入，不将全部94个SCCLIB入口逐个排列测试。

统一包目录：`/HOME/M10STD/`。将 `STDLIBPROBE.SCX` 和 `STDLIBGUARD.SCL` 邻放，目录允许本次探针创建和更新文件。系统依赖为 `/SYS/LIB/SCCLIB.SCL` ABI1.0/version1、`SC32API.SCL` ABI1.0/version1。Guard另明确要求 `SCLDR.SCL` ABI1/min_minor1/min_version2；旧九导出ROOT不能代替R1。产物名都在现SandFS名字长度内。

GCC引导与客体SCCC都从同一份普通C源码生成。应用 `main(void)` 通过真实 `ScProcessArguments` 选择模式，不依赖尚不存在的argc/argv CRT。参数必须是表内一个完整单词，空参数默认core。Guard的GCC分支仅两个测试函数使用i386 naked机器正文，让EAX为0x100/0x101、有效AL为0/1；客体SCCC分支使用真正 `_Bool` 返回。检查外部GCC `_Bool` 高位风险时，必须保留GCC生成的Guard，不能用返回已经规范化的客体库声称覆盖该项。

## 2. 模式与判定协议

| 模式 | 成功退出码 | 必须同时核对的证据 |
|---|---:|---|
| `core` | 0 | 标准输出末尾 `STDLIB_CORE_PASS failures=0`，没有 `STDLIB FAIL` |
| `stdio` | 0 | 标准输出末尾 `STDLIB_STDIO_PASS failures=0`，`STDIO.DAT` 最终字节为 `1234` |
| `return` | 37 | `STDLIB_LIFECYCLE_ARMED return exit=37`，进程结束后 `RETURN.LOG` 精确匹配下节 |
| `exit` | 38 | `STDLIB_LIFECYCLE_ARMED exit exit=38`，进程结束后 `EXIT.LOG` 精确匹配下节 |
| `shared` | 0 | Shell共享重定向文件恰好两个字节 `BA`，`SHARED.RES` 精确匹配下节 |
| `init-exit` | 42 | Guard init正常终止，`INIT.LOG` 只有 `INIT_EXIT\n`，main未执行 |
| `recursive` | 1 | 回调中再次exit触发固定异常诊断，`RECURSE.LOG` 保持0字节，没有返回或fini |
| `pipe` | 0 | `STDLIB_INPUT_READY pipe` 后真实stdin取得 `Q\n`，末尾 `STDLIB_PIPE_PASS failures=0` |
| `tty` | 0 | `STDLIB_INPUT_READY tty` 后真实TTY取得 `Q\n`，末尾 `STDLIB_TTY_PASS failures=0` |

探针检查日志的单行格式为 `STDLIB PASS <name>` 或 `STDLIB FAIL <name>`。汇总只在检查无失败、输出调用成功且stdout实际flush成功时返回0；日志写入失败也给非零码。普通检查失败为91；参数读取失败90；exit意外返回92；shared准备失败93、共享检查失败94；输入READY输出失败95；未知模式96；init-exit错误到达main为97。

`ARMED` 只说明登记和未flush条件已经成立，**不是终止PASS**。必须等真实退出码和最终文件，不能仅grep该标记。shared没有标准检查日志，也不把stdout当结果通道。

主代理可以在同一次guest启动内顺序执行所需模式，复用当前开发盘及旧有效证据。只为实际风险运行这些组，不追加函数全排列、全盘副本或无关历史矩阵。PIPE/TTY供未解决的真实阻塞风险使用；能从已有未变化证据确认的部分直接复用。

## 3. core 覆盖的具体问题

- C1：合法pointer/NULL/CV条件表达式、const聚合数组成员decay、函数指针比较器和跨SCL窄形参转换。
- C2：真实va_start/copy/arg/end、拷贝游标独立、整数与pointer及默认提升；`va_arg(*va_cursor(&copy),int)` 的ap地址副作用只能发生一次。
- C3：256/65536到bool、volatile uintptr_t运行值0x100转pointer再转bool、全局/数组/聚合bool字节、bool返回与外部GCC仅AL有效返回。pointer不解引用；其低字节0而全地址非零，能识别先截字节的错误，又不触发已知对象地址恒真的警告。
- C4：offsetof作为数组长度ICE、嵌套数组成员偏移、unsigned字面量/比较/移位和八进制预处理条件。SCCC分支中 `(1 ? -1 : 0u) >> 1` 必须等于 `~0u >> 1`，验证条件表达式先合并unsigned类别；右侧适应预处理整数宽度，不错误假设GCC的预处理值只有32位。GCC仍执行通用unsigned字面量/比较/移位检查；该条件负分支专用项只由客体SCCC求值，避免GCC合理的sign-promotion诊断，不降低-Werror。条件错误直接编译失败，不转成运行时假PASS。
- 分配器：八种尺寸同时16B对齐且数据不串扰、复用、两个邻块合并、calloc乘积溢出及复用区清零、realloc尺寸失败保旧数据、缩小及增长保前缀。失败请求为确定尺寸溢出，不耗尽全机内存。地址复用/合并判定用于当前实现质量，不另承诺ISO C固定地址ABI。
- 字符串：两个方向的真实重叠memmove、unsigned字节比较、strncpy补零及围栏、周期和非周期Two-Way搜索、重复前缀的长尾匹配。
- 转换/算法：LONG_MIN、base0/2/36、无数字与0x前缀、unsigned溢出扫到正确endptr、负号模减、errno保留/ERANGE/EINVAL、含INT边界的qsort与bsearch、C locale字节和EOF。
- 格式：INT_MIN/UINT_MAX、flags、整数/字符串/pointer、动态width/precision、窄整数提升、snprintf围栏/截断逻辑长度/%n、NULL零容量，以及实际printf。assert实参也只求值一次。

这是32位/字节profile的定向证据，不覆盖未发布的浮点、64位、scanf、locale/time或完整ISO C。原SCAPI.H冻结字节不在探针中重声明或重写。

## 4. stdio 六模式与保持打开

一个文件 `/HOME/M10STD/STDIO.DAT` 按顺序经过 `w`、`rb`、`ab`、`r+b`、`wb+`、`a+b`，覆盖六个真实flags及b/+两种合法顺序。它验证w真写入并flush后从Sc32 path stat看到已提交长度、重复flush保持FILE打开、r预读后逻辑位置和CUR不取backend位置、ungetc一字节位置、fgetpos/fsetpos、EOF/clearerr/rewind、a实际新EOF、r+不截断且覆盖当前位置、w+截断及EOF后的合法写方向、a+初始读0而每写真追加。所有借用读缓冲在所属FILE关闭前仍存活。

末尾内容必须为 `1234`。PATH stat来自正式SC32，不从FILE私有控制对象猜长度；该组没有复制整个文件或为每个模式新建大盘。

## 5. 正常return与显式exit的顺序

两个模式各打开一个独立FILE并借用 `main` 自动数组作1024B全缓冲。先登记APP_A、Guard私有static回调、APP_B。APP_B执行时追加APP_PLUS，故真正prepare应产生APP_B、APP_PLUS、GUARD_CB、APP_A。Guard fini最后写GUARD_FINI；它依赖SCCLIB，所以所有事件之后才由SCCLIB fini drain/flush/close。应用没有提前flush/close这个FILE。

开始终止前，真实ScPathStat必须确认文件长度仍为0。main直接return37，或从较深的helper调用exit38。R1实际另栈收尾必须保留借用数组中的未写正文；只修改一个返回码或在SCCLIB栈上Destroy不能满足证据。

`RETURN.LOG` 期望53字节，所有行尾为LF：

```text
MAIN:return
APP_B
APP_PLUS
GUARD_CB
APP_A
GUARD_FINI
```

`EXIT.LOG` 期望51字节：

```text
MAIN:exit
APP_B
APP_PLUS
GUARD_CB
APP_A
GUARD_FINI
```

Guard回调不是导出函数，成功atexit证明ROOT接受真实module代码范围内的私有函数。APP_A/B/PLUS也是真实APP内部函数。不存在预写的末尾PASS；回调追加失败会留下 `REGISTER_FAILED`，任意缺行、重排、重复或字节损坏都失败。退出后的页/句柄回收由主控观察真实kernel计数，本探针不伪造“已回收”统计。

## 6. stdout/stderr共享位置回归

旧SandShell不支持2>&1。真正Shell先重定向helper，再由正式ScProcessSpawn传递同一端点：

```text
SHARERUN.SCX /HOME/M10STD/STDLIBPROBE.SCX > /HOME/M10STD/SHARED.OUT
```

stdout先缓冲A，stderr无缓冲写B，stdout再flush。共享底层游标实际成为2；ftell(stdout)与fgetpos(stdout)必须都报告2。`SHARED.OUT` 只能是 `BA`，无LF或其它诊断。`SHARED.RES` 必须精确为：

```text
STDLIB_SHARED_PASS position=2 fpos=2
```

这个模式对应已确认的stdio drain缺陷：不能只对APPEND更新logical_position，普通共享STD在每个确定prefix后也必须从真实result.position加尚未写后缀同步。结果文件与共享标准端点独立，不把“测试日志本身多写的字节”当正确游标。

## 7. init中exit与回调递归exit

`init-exit` 的判定在Guard init通过正式ScProcessArguments读取。此时SCCLIB依赖已经成功init/提交prepare，Guard当前init尚未返回0。Guard打开 `/HOME/M10STD/INIT.LOG`，以默认全缓冲写 `INIT_EXIT\n` 后exit42；不提前flush/close。Root必须撤离真实init事务，运行已成功初始化前缀的SCCLIB fini，使日志最终恰好10字节 `INIT_EXIT\n`。

Guard尚未成功init，不能调用它的fini；若误调用，Guard会往同一FILE追加 `GUARD_FINI\n`，精确字节检查将失败。main也不能执行，日志中不得有 `STDLIB_INIT_EXIT_MAIN_RAN`。返回42、精确10字节、无main标记与真实页回收必须同时满足；源码中的调用意图不能当成功证据。

`recursive` 打开 `/HOME/M10STD/RECURSE.LOG` 并借用main数组缓冲，写入尚未发布的 `MAIN_RECURSIVE\n`，把该FILE交Guard作fini哨兵。main登记一个atexit回调后打印并flush `STDLIB_RECURSIVE_ARMED`，本来的main返回码为43。回调再向缓冲FILE写事件，向stdout实际打印并flush `STDLIB_RECURSIVE_CALLBACK`，随即exit99。

R1在PREPARING拒绝第二次正常终止，必须实际输出固定诊断：

```text
SCL: invalid normal termination
```

最终退出码必须为1，不能是43或99；不得出现 `STDLIB_RECURSIVE_RETURNED`。此路径直接进入kernel异常回收，不重跑prepare/fini，也不flush这个用户FILE，因此 `RECURSE.LOG` 必须仍为0字节。若错误走fini，哨兵和待写正文会留下非空文件。主控另外核真实进程页/句柄归还。这两条各运行一次即可，不展开其它理论非法状态矩阵。

## 8. 实际输入等待

PIPE模式要求上游送 `Q\n` 并真实关闭writer；fgets取得该行后，下次fgetc才可遇EOF。为了覆盖AGAIN，应让reader在READY后先面对空但仍有writer的pipe，再由主控安排后续写入；立即已有正文的普通echo管道只能覆盖数据/EOF，不能声称证明AGAIN等待。

TTY模式在READY后由真实键盘/串口输入Q和换行，fgets须成功且不置EOF/error。它不再读第二行来伪造TTY EOF。READY输出先真实flush，主控可按该事实驱动输入。阻塞/唤醒证据来自实际事件与程序完成，不能把超时中断或有字符时的一次read当空流等待通过。

## 修订

2026-10-15：新增两份正式SDK定向探针、准确SCX/SCL清单及core/stdio/终止/共享位置/输入协议；补已成功SCCLIB之后的init-exit及prepare回调递归exit实际状态出口。源码与静态审阅已收齐，本批宿主构建、客体SCCC与QEMU证据待主代理统一执行。

2026-10-15：依据统一stdlib-build-01真实-Werror，仅修probe的预处理signed比较和已知对象地址bool表达式；保留unsigned条件类型及完整pointer到bool的风险判定，不降低警告等级。库/ROOT/编译器未改，修后构建与客体证据仍待主代理统一执行。

2026-10-15：统一stdlib-build-02只剩GCC对条件负分支转unsigned的实际诊断；将该类型合并检查限于__SCCC__分支，所有工具链继续执行通用unsigned检查。未关闭警告或改库，待统一build03与客体原生编译。


修订：2026-10-15，统一stdlib-build-06及guest02/04/05实际SCCLIB/SCLIB/R1/C1..C5、九模式/当前G1/253项通用工作流与精确回收通过；保留三项验证失败、未变输入复用与证据边界，见evidence/m10a2-stdlib，完整目标继续。
