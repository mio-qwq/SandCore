# SCCC i386 聚合按值 ABI

2026-10-15，**SandCore M10a2 — The Application Platform Era**。本批为 SCUI 完整源码实际拒绝项而增加真正 struct/union 按值参数及 hidden-sret 返回，并修正 SCL 函数 map 的地址域。实现位于 `user/s3c_parse.inc`、`s3c_std_parse.inc`、`s3c_emit.inc`、`s3c_std_emit.inc`；`s3c.c`、公开SDK头、SCL/SCD/SCX格式、SCAPI.H及库/内核源未改。**SOURCE_ONLY：没有执行本批构建、宿主后端或QEMU；真实交叉验证由主代理统一完成。**

## 1. 范围与采用的调用约定

SCUI 的 `ui_scroll_limit(ScWideCounter content, ScU32 viewport)` 已在实际HOST前端预检被旧“aggregate by-value parameters are unsupported”拒绝；旧parser也禁止struct/union返回。不能把完整库改为薄壳或删掉计数器/Rect/Value语义来绕过这个缺口。

本批采用 **i386 System V ELF、memory aggregate return、callee-pop hidden pointer**。GCC官方说明默认x86-32 ABI由callee清hidden pointer，Windows x86-32默认不同；实际宿主库继续由WSL的ELF GCC构建，沿现SCL的4B incoming stack与内部16B重对齐参数，不把MinGW/MS ABI混用。[GCC x86函数属性](https://gcc.gnu.org/onlinedocs/gcc/x86-Function-Attributes.html)

完整struct/union按值形参、返回、直接与间接call、嵌套结果、成员读取、聚合赋值/初始化/return、conditional/comma、变参聚合均采用同一表示。数组/函数形参仍衰减为指针；普通scalar参数/返回、Bool AL零扩展、ROOT双栈adapter、原SCX/SKM入口及旧cdecl只处理其原路径。

不引入浮点、64位整型、SSE按值类型、bit-field、packed/over-aligned类型、`-freg-struct-return`、fastcall/MS ABI或compound literal。完整类型仅允许现语言真实布局的1/2/4B对齐。类型不完整、聚合身份不同、非法scalar/aggregate转换均明确诊断。普通指针取值/转换仍沿C1..C5，不将aggregate来源地址伪装成整数参数。

聚合成员/按值宽度必须为正数。括号声明的array hole在换入真实元素后重新执行count/元素宽度/0x3d0000除法预验，之后才相乘；不能让此前size0的hole绕过array_of检查、产生负溢出宽度再作为一个假小对象进入ABI。

## 2. 参数布局与复制

对聚合T，`slot(T)=round_up(sizeof(T),4)`；scalar槽仍4B。形参offset从EBP+8开始；聚合返回函数另有hidden pointer，普通形参从EBP+12开始。后续offset逐项累加实际slot，参数数量/原型的C视图不包含hidden pointer。

例如：

```c
struct Pair { unsigned lo, hi; };
struct Pair step(unsigned before, struct Pair value, unsigned after);
```

其callee帧：

| 偏移 | 内容 |
|---:|---|
| EBP+0 | 原EBP |
| EBP+4 | return address |
| EBP+8 | caller提供的结果对象地址 |
| EBP+12 | before，4B |
| EBP+16 | value，完整8B副本 |
| EBP+24 | after，4B |

参数树仍按源码倒链从右向左求值。聚合实参先取得本次值的真实来源地址，再为该参数减ESP预留slot，将**恰sizeof(T)字节**复制进去；caller原对象不会被callee修改。slot尾的0..3B padding由caller写零，不从来源对象末尾越界读取。内部结构padding属于sizeof(T)，仍按对象表示复制，不承诺把未初始化成员或内部padding变为零。

新的 `copy_aggregate` 发射真实 `CLD; REP MOVSB`，保存并恢复ESI/EDI，保存原目的地址并以EAX返回它；不调用libc。目的ESP在helper自身push之前取得，helper结束完全恢复ESP。旧 `store_value` 的聚合复制保持原发射路径；按值复制不会为大对象生成每字节一条MOV而无界膨胀机器码。

同一参数块最多64KiB；每种聚合须完整且不超过这个预算。参数总宽在发射call之前检查，定义时也检查形参offset总量。此预算与原64KiB局部frame规则协调，不代表任意调用深度都能放入128KiB用户栈；栈规模/实际递归深度仍由程序和现装载预算限制。

## 3. 聚合返回与栈平衡

caller为每个实际求值的聚合call准备结果slot。完成所有显式实参后，最后压hidden结果地址；callee的 `[EBP+8]` 因而固定为它。直接call仍用原F_REL；间接函数指针先压栈保存，hidden压入之后以 `[ESP+explicit_argument_bytes+4]` 取得目标。

callee的 `return aggregate_expression` 先求出来源值地址，将sizeof(result)复制到hidden地址，EAX保留目的地址，然后进入共同epilogue。epilogue为 `leave; ret 4`；有inline asm时原callee-saved保存/恢复仍先完成。落到函数尾部而没有合法return属于C未定义结果，不捏造聚合值；ABI仍返回hidden地址并RET4。

RET4只清hidden pointer。caller随后清 `explicit_argument_bytes + (indirect ? 4 : 0)`，**不再次清hidden4B**，最后用LEA重取本人的结果slot，不依赖外部callee的EAX作为唯一借用来源。

设显式参数块为A、间接目标保存为I（0或4），忽略helper已平衡的内部push：

| 步骤 | 相对call开始前ESP的变化 |
|---|---:|
| 保存indirect目标、放显式参数、压hidden | `-(I+A+4)` |
| CALL→callee RET4完成 | `-(I+A)` |
| caller ADD ESP(A+I) | 0 |

hidden指针不混进普通参数count，也不计入va_start右端偏移。scalar函数仍C3返回，scalar caller仍清其全部普通参数；没有aggregate参与的原call字节逻辑保持。

## 4. 结果临时对象与成员语义

共同Node仍32B，Type/Parameter记录不增宽。`N_CALL.c`、`N_VA_ARG.c` 在各自节点中保存聚合结果slot的负EBPoffset；首次**实际emit**才分配，各节点独立，不用单一全函数scratch。嵌套 `outer(inner(...), another(...))` 的结果不会被后一call覆盖，循环中的同一call节点可重复使用它自己的槽。

函数prologue一直预留原SUB ESP imm32位置，完成整个函数发射后回填最终local_bytes，包含结果slot；callee-saved push位于最终frame以下。已有局部变量的offset不移动，ESP临时参数/赋值push也不移动EBP槽。槽失败沿原64KiB frame诊断及fatal退出，不产生半个最终输出。

延迟分配是必要的：`sizeof(f())`不得调用f或为未求值表达式占结果slot，尤其全局sizeof不要求存在函数帧。真正global initializer的call仍不是常量，不因这个ABI放宽。

聚合值在后端以对象地址表示，内部member地址生成可接受call、conditional、comma、assignment及va_arg结果。新的 `N_DEREF.c` 标记临时聚合成员为非lvalue并沿嵌套`.`和该临时array的`[]`传播：`make().field`可读，`&make().field`和修改该temporary成员拒绝。`make().pointer->field`解引用的是实际指向对象，正常lvalue/权限不被来源rvalue污染。

聚合simple assignment、初始化与return要求同一struct/union身份，CV副本沿原aggregate_identity认同；复合赋值、aggregate→scalar、scalar→aggregate、聚合cast拒绝。聚合conditional仍用现共同类型判定，不以首四字节模拟整个值。

## 5. 变参与声明兼容

`va_start(ap,last)` 使用 `last实际S_PARAM offset + slot(last.type)`。因此末named参数是8/12/16/32B对象或有hidden sret时，cursor都位于全部named参数之后，不再错误地固定+4。ap自身仍是SDK真实4B游标，表达式求址一次。

匿名聚合参数不做integer promotion，复制按同一slot规则；`va_arg(ap,T)`仅接受既有promoted32bit integer/pointer或完整struct/union。cursor先推进slot(T)，聚合取值再复制到该N_VA_ARG独立frame slot，以免之后游标/实参对象变化使先前结果变成别的值；float/64/窄未提升类型继续拒绝。va_copy/end的原语义和ROOT adapter不改。

普通SCX是逐函数即时生成：若先前已按scalar ABI发射call，后来同名声明不能改为hidden-sret或聚合形参。所有模式的aggregate相关函数/函数指针重声明须兼容，aggregate原型须先于任何call；没有完整原型的aggregate实参call拒绝。局部extern同样复验，不替换已有定义参数名字而破坏va_start。SCL/SCD原本严格原型规则继续。显式错误函数指针cast不提供另一种ABI实现；合法跨库调用以正式完整头与library ABI为准。

旧SCX main和SKM skm_main的CRT没有hidden pointer，必须明确拒绝新引入的aggregate返回入口；SKM唯一API参数也不能为aggregate，因为旧CRT只传4B指针。自动动态SCX的签名继续由原signed int(void)规则验证。init/fini、ROOT私有callback仍保持原指定签名。

## 6. SCL map 地址域修正

此前 `compile_unit`写 `SCLMAP1MIO`，但 `emit_function/emit_standard_adapters`调用 `map_function(BASE+code_used)`，函数行实际仍为旧0x400000绝对列；`finish_scl`的EXPORT行却是RVA，IMPORT/REQUIRE为表index。这个混合不能用于真正target module base换算。

本批在共同 `map_function` 对shared_output减BASE一次，使普通函数/私有ROOT adapter函数行与SCL EXPORT同为真实RVA。SCDMAP/普通SCX/SKM保持原绝对VA；不更改机器码符号值、重定位或容器布局。IMPORT/REQUIRE仍是index，消费者须按记录kind解析，不能把其数值当地址。

现map仅有函数定义源行及辅助typed行，没有语句级行表或image摘要绑定。这个修正不等于完成Debug2模块/源码调试；旧历史map原字节保留，完整typed Debug/模块注册和debug sidecar属于另一个获准阶段。

## 7. 生命周期、静态容量与待验出口

未新增compiler heap区、Node成员、Type成员或静态大数组；新临时对象属于生成程序的现函数frame。Compiler自身Source/Token/Pool/Code的取得/释放规则不变，scalar旧Code限制、SCX36B/CRT、load/BSS/stack与SCL/SCD共同validator仍各自检查。

SOURCE_ONLY期间只审阅类型/栈/机器字节发射/失败路径，不预写PASS。主代理的真实AGGABI fixture将核8/12/16/32B结构体/union、输入隔离、GCC↔SCCC两方向直接和callback、nested-return/member/conditional、重复调用后scalar、真实ESP以及末named聚合va_start，并检查真实WSL GCC callee的RET4。冻结fixture预期终marker为`AGGABI PASS checks=55 failures=0`，这是待执行的断言，不是已取得结果。完整SCUI再由同一新后端实际生成，随后Windows客体执行/页账；生成、HOST前端通过与真实运行分别记录。

不扩旧大矩阵，不重新把未受影响Phase1/标准库/网络证据归零。任何具体interop或完整SCUI诊断由owner修复后做对应最小回归；达到本批出口不表示整个M10a2或Studio2/Debug2完成。Studio/Debug独立只读设计暂为本实际compiler缺口让路，未以文档替代实现。

修订：2026-10-15，实现真实i386 by-value/sret、稳定结果slot、聚合varargs与诊断/CRT保护，修shared函数map RVA；四片段和本规范收齐后统一验证，本批尚无新运行结论。
