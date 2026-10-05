# M9自写整数awk（源码，未验证）

实现为SCAWK.H/inc与独立user/m9/awk.c，不引入其它awk源码或标准C运行库。
当前未构建/执行。它是明确有界的整数子集，不能凭命令名宣布完整POSIX awk
或BusyBox行为覆盖；完整行为用例与客体验证仍待统一进行。

已写pattern/action、BEGIN/END、范围pattern、默认print、$0/$n/NF/NR/FNR/
FILENAME、FS/OFS/ORS、-F/-v/多个-f、文件参数赋值、ARGC/ARGV、print/printf、
字符串拼接、32位算术/关系/短路/条件/赋值/复合赋值/前后自增、自写NFA regex、
if/else、while、for、for-in、next/break/continue/exit、关联数组/成员检测/delete。
函数含length/int/index/substr/tolower/toupper/match/split/sub/gsub/sprintf/system。
system经同UID子Shell，不能用awk改变父进程身份或打开UART。

数字为32位整数，加减乘模2^32，除零报错；不支持浮点/指数数字/数学函数。
数字字符串识别为完整十进制整数，非数字算术取0；C字节字符串/ASCII大小写，
没有Unicode标量下标。未知函数、用户函数定义、getline、do/return/nextfile、
print文件/管道重定向及regex反向引用/重复区间明确失败；Shell外部重定向可用。
printf/sprintf支持字符串/字符/整数、宽度/零填充/左对齐及字符串精度，整数
precision/浮点格式明确拒绝。子集不支持多维下标、RS变更或ARGV重排来改输入
循环；写ARGC/ARGV、RS、SUBSEP、IGNORECASE、OFMT/CONVFMT明确报错，
不会接受赋值却继续使用旧值。NUL输入/字符串明确拒绝，不截短后伪装完整。

容量：脚本32767B、2047个AST节点、128条rule、96个变量、256个字段、单记录
16KiB、单格式输出/字符串64KiB、临时表达式256KiB、持久字符串/键总计4MiB，
关联数组全局512项/1024个散列槽、键1023B、regex128个固定槽+8个动态缓存/
模式2047B。固定源码模式在运行前编译并保持至退出，动态模式独立轮换；解析
嵌套32、运行嵌套64、循环嵌套32；每记录与BEGIN/END阶段最多一百万步，
每1024步让出调度。超限返回错误，不静默删字段或以截断输出充数。

算法与生命周期：脚本解析一次，字段只在新记录/赋值$0时拆分；普通字段赋值
标脏，读取$0才重建。仅字段改变时压紧字段正文，表达式临时区在语句边界复用，
变量/数组的持久正文有单独所有权；结束释放全部正文、NFA及缓存。
index用线性前缀表，关联数组使用有界开放散列及墓碑，正则复用项目NFA。
词法字符串复用BSS缓冲，正文立即复制到literal池，避免递归解析反复占32KiB栈。
这些为源码设计，尚无性能测量，不能称客体运行效率已经达标。

后续统一客体验证例（尚未执行）：

```sh
printf 'a 3\nb 7\n' | awk '{ sum += $2; print NR, $1 } END { print sum }'
awk 'BEGIN { for(i=1;i<=4;i++) a[i]=i*i; for(i in a) print i,a[i] }'
printf 'abc\n' | awk '{ gsub(/b/,"&X"); print $0; $2="tail"; print NF,$0 }'
awk 'BEGIN { x=0; print (x && 1/0); printf("%04d %s\n",12,"ok") }'
```

修订：2026-10-05，自写解释器与完整容量/语义边界，源码未运行验收。

-F/-v及参数赋值按字符串转义解释，支持常用控制、八进制/十六进制转义，
NUL/未完成转义明确拒绝。split的/regex/保持正则语义，即便只有一个字符；
普通字符串FS的单字符分隔则按字面处理。词法quoted string中的反斜杠换行
是续行，不能把其误算入正文。运行前编译并固定源码regex，动态槽独立轮换。

修订：2026-10-05，同步词法栈、正则缓存、CLI转义与split语义；未验证。

2026-10-05真实QEMU首测：记录累加/字段/NR用例通过，数组用例在
`print (2 in a),a[2]`报statement separator。解析器已修括号外逗号
及单项括号后的表达式尾部；后续重测还未通过，不改称完整awk验收。
