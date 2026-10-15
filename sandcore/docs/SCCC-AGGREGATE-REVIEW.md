# SCCC 聚合按值与返回 ABI 审查

2026-10-15。所属版本 **SandCore M10a2 — The Application Platform Era**。本文件是只读实现审查：没有修改 compiler、SDK、工具或夹具，没有构建、执行编译器或启动 QEMU。实现所有者为 `dynamic_scx_toolchain`，Root负责统一构建、交叉ABI及真实G1。下文区分初始源码、已落的新正文与待修项，不把静态判断称作运行PASS。

## 1. 结论与范围

新正文采用 **i386 System V 内存聚合返回**：caller提供结果区，hidden指针占首个4B槽；callee只弹这一个槽，普通参数仍由caller清理。按值参数是完整对象副本，不是把其地址当4B参数。8/12/16/32B struct与union使用同一机制，成员布局仍由原Type图确定。

当前独立结果槽、参数字节累计、间接目标寻址、return复制及named-last aggregate的va_start方向正确。审查发现并已由实现者补齐、再次只读核对的必要保护包括：固定CRT入口不允许新聚合返回/参数破坏其固定栈合同；含聚合的不兼容重声明不能覆盖已即时发射的调用；parenthesized数组hole替换在乘法前检查宽度。具体源出口见第8节；这仍不是运行结论。

旧标量cdecl、指针/整型结果、bool的AL返回处理、SCAPI.H、SCX/SKM/SCL/SCD容器和私有ROOT终止adapter不因此改ABI。新增聚合能力不代表long long、浮点、过对齐、bit-field、compound literal或完整ISO C已经支持。不能为省语法把公开struct按值入口改成私有pointer入口。

## 2. 真实源码与初始快照

已读共同入口 `s3c.c`、`s3c_parse.inc`、`s3c_std_parse.inc`、`s3c_emit.inc`、`s3c_std_emit.inc`，并核对 `s3c_scl.inc` 的声明比较/入口签名。CLI `sccc.c`包含同一 `s3c.c`；不存在另一套用于此ABI的标准库编译器。

| 初始源 | 字节 | 审查开始SHA256 |
|---|---:|---|
| `user/s3c.c` | 17497 | `0911e0ca08fbc538f874d19edb17b455bda983d71db259a1ba88acf0a63be8f8` |
| `user/s3c_parse.inc` | 38772 | `137075d91b5026f079ea7d41346539e5e699349560c66dd86a7413dfc7c439f7` |
| `user/s3c_std_parse.inc` | 12616 | `af38d856fbe8ba13a8474835e9fcc9ccbfeda429c419a58bdbfbdd4923974c8d` |
| `user/s3c_emit.inc` | 54903 | `5a3efec57e5f24f0be25d265bf8ace70e0e6bfb799d14ad7f5c4571e98d48b4a` |
| `user/s3c_std_emit.inc` | 3252 | `df80ae20d631e76a7002a22cd265728f07ab656423824c19ff525e90067fda76` |

初始实现明确拒绝aggregate参数/返回：parse `suffix()`仅接受完整标量参数和scalar返回，`compile_unit()`给S_PARAM `8+4*i`，`N_CALL`逐实参push EAX并清`argc*4`。aggregate表达式已有“EAX携带来源地址”的本地复制表示，但 `address()`仅接受symbol/deref/string；因此单独放开parser仍无法正确生成call、成员及return。

共同 `Type`24B、`Parameter`12B、`Node`32B、`Symbol`32B保持。聚合限定副本的 `aggregate_identity`沿原实现复用，不能因为不同标签同宽或同字段布局就合并身份。现SOURCE/Token/Pool/Code堆工作区及输出预算见 [SCCC-CAPACITY.md](SCCC-CAPACITY.md)，不是本审查新设的放宽额度。

## 3. ABI 栈合同

[GCC x86 attributes](https://gcc.gnu.org/onlinedocs/gcc/x86-Attributes.html#x86-Attributes)说明，默认非Microsoft x86-32内存聚合返回由callee弹hidden指针，普通cdecl参数由caller弹。Windows **宿主上的WSL ELF GCC**和Windows **目标的MinGW ABI**须区分；不能使用MinGW默认、`-freg-struct-return`或不同callee-pop属性编出同名库，随后声称与该System V合同相同。

通用入口仍最低4B对齐。宿主SCL按 [SCL.md](SCL.md)已有SDK参数接受4B入口并在需要时内部对齐；64B CRT的独立16B引导保持。此次不机械给每个SCCC call加16B垫片，不改变旧named参数偏移。

定义 `slot(T)`：已支持的整型/指针为4；complete struct/union为 `round_up(sizeof(T),4)`。数组和函数只经既有参数衰减，不作为裸按值对象；每个aggregate须正宽度、合法1/2/4B对齐并在当前64KiB参数块/帧限制内。必须先检查再取整/累计。

| callee EBP偏移 | scalar返回 | aggregate返回 |
|---|---|---|
| `+0` | saved EBP | saved EBP |
| `+4` | return PC | return PC |
| `+8` | 第一个普通参数 | hidden结果指针 |
| 普通参数起点 | `+8` | `+12` |
| 后续偏移 | 前一偏移加其slot宽 | 前一偏移加其slot宽 |
| 返回指令 | `ret` | `ret 4` |
| caller清理 | 普通参数块 | 普通参数块；不再弹hidden |

具体例子：`AggRect AggRectStep(u32 before,AggRect input,u32 after)`中hidden为+8，before+12，16B input+16，after+32，普通参数总24B。caller右到左铺after4/input16/before4，最后push hidden4。callee `ret 4`返回后，caller只加24恢复原ESP。

间接目标由caller先保存。push hidden之后，目标位置为 `ESP + argument_bytes + 4`；无聚合返回时仍为 `ESP + argument_bytes`。caller最后另加自己的目标保护4B。该槽不是callee的named/unnamed参数，不能纳入va_start。

callee return复制实际 `sizeof(T)`到`[EBP+8]`指向区并返回其地址。caller仍以LEA重取自己预定的结果槽，不依赖外部callee是否恰将EAX保留到清栈后。没有把8B aggregate当EDX:EAX整数返回。

## 4. 参数副本、结果槽与寄存器

当前新 `emit_expression::N_CALL`先完整累计每个N_ARG的slot宽，右到左每实参求值一次。aggregate实参先得到EAX来源，再reserve对应caller栈块并复制；仅清对象末端至slot末端的补槽字节。不能从源对象读取rounded-size之外的1..3B，不能用`push [source+last]`越过对象末端。

新 `copy_aggregate`以REP MOVSB复制实际字节，局部push/pop保存ESI/EDI和原目的，返回EAX目的，DF显式清。caller在helper额外push前已取得目的ESP地址，避免保存寄存器改变目的偏移。新按值参数、return、aggregate va_arg都用这条机器复制；没有引入libc导入或syscall，也没有改旧 `store_value`的标量/既有聚合赋值机器序列。

`N_CALL.c`与aggregate `N_VA_ARG.c`保存各自唯一的负EBP结果偏移。首次实际emit才分配，按slot宽附在所有parse-local之后。每个调用树节点拥有独立槽，嵌套 `step(step(x),y)`和同一全表达式的多个调用不会覆盖一个全局scratch。

`emit_function`原本已无条件发射 `sub esp,imm32`，新正文记录立即数位置并在整个body生成后回填最终local_bytes。即使函数返回scalar、原local_bytes为0，只要body有aggregate call也能取得真实帧。callee保存寄存器的push在最终frame下方；所有return跳到共同恢复/leave/ret路径。Node、Token及静态BSS未扩字段。

`sizeof make()`或`sizeof make().array`只保留类型，不执行call，不分配结果槽，不推进va_arg。全局sizeof也不因缺function frame而被拒；真正全局aggregate copy仍不是常量初始化。栈槽物理保留到函数返回只是实现存储策略，不承诺程序可以把临时结果地址逃逸到全表达式之后。

## 5. 成员 rvalue 与内部地址

当前 `address()`末支只为aggregate值内部调用emit，因而N_CALL/N_COND/N_COMMA/N_ASSIGN/aggregate N_VA_ARG可以给成员读取或复制提供地址；不会把scalar call自动变成可取址对象。

`primary()`的`.`将 `!lvalue(base)`记录为新N_DEREF.c，`lvalue()`对这个标记为false。嵌套 `.sub.member`沿标记传播；临时数组直接下标沿标记传播。`f().member`可读，但`&f().member`、给它赋值或自增明确拒绝。`->`先建立真实解引用，因此`f().pointer->member`仍可修改那个独立指向的对象，不能以外层返回值是rvalue为理由错误拒绝。

这对应 [WG14 N1570 §6.5.2.3及§6.2.4](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)：点成员的值类别来自其基表达式，指针成员是被指向对象；包含数组的返回临时只在相应全表达式内有效。编译器可保守诊断直接修改已识别临时数组的代码；这不是完整逃逸/别名分析，不能声称能发现所有经指针保存后的非法使用。

按值参数/返回/初始化/简单赋值比较同一nominal aggregate identity并保实际对象宽。aggregate算术、scalar接收aggregate、aggregate复合赋值及aggregate C cast不伪装成地址整数运算。显式void丢弃结果仍可执行其真实副作用。

## 6. va_start、va_arg 与已有stdarg

当前SDK `stdlib/stdarg.h`已经把四标准宏交给真实builtin，SCCC的va_list为unsigned-char指针游标，GCC使用其真实i386 builtin；不改头中的实现或用局部地址猜栈。

新 `standard_builtin::N_VA_START`仍核当前variadic函数、最右named、未被shadow的真实S_PARAM。起点是 `symbols[last].value + slot(last_type)`，而不是`+4`或`+sizeof`未取整值。hidden偏移已经在S_PARAM起点中包含，不能再加一次。

`AggPair AggVariadic(u32 count,AggPair input,...)`：hidden+8，count+12，input+16且占8B，首unnamed+24。没有hidden的同named参数函数，其首unnamed为+20。最后named分别8/12/16/32B时，只改slot宽度，不改游标表示。

新aggregate va_arg先求ap左值地址一次，读取原cursor，按slot宽推进并写回一次；随后恰复制sizeof(T)到该N_VA_ARG的独立EBP槽，EAX返回槽地址。下一次va_arg不能覆盖已取的aggregate值；scalar va_arg仍读4B，copy/end仍保持原一次求值/独立游标。char/short/bool的请求不等同实际默认提升后的int，继续拒绝；FP/64/array/function/不完整aggregate也不被伪装支持。

ellipsis aggregate实参保留对象类型并使用其连续槽副本，数量与实际请求类型仍由C调用者遵守。va_arg没有运行期类型标签，不能承诺自动识别错误类型或越过实际参数表。

## 7. 不兼容声明、入口与容量

`abi_function_aggregate`沿pointer/array找到函数类型，只在双方涉及aggregate时为旧SCX/SKM增加严格声明比较，标量旧路保持。新 `abi_declaration_type`复用 `sclc_type_same`，忽略参数最外层CV，仍比较函数结果/参数nominal身份/variadic标志。未原型调用的aggregate实参拒绝。SCL/SCD原有完整prototype导入检查继续。

必要原因是逐函数即时emit：此前的implicit-int调用已经生成普通ret/caller pop规则，后面只改Symbol.type无法把它变成带hidden的新调用。必须在最终输出前拒不兼容覆盖，不能靠运行时尝试猜callee类型。

固定24B SCX/SKM CRT与64B SCD CRT不增加hidden槽。SCD的 `sclc_callback(main)`已有signed int(void)验证；legacy `finish_image`须拒aggregate main/skm_main返回，并拒SKM那唯一普通参数为aggregate。原合法int/pointer入口不改字节。SCL init/fini同已有精确int(void)/void(void)合同，ROOT两个私有adapter原型不改。

参数块累加每步用剩余预算比较，单aggregate尺寸取整前检查正值/合法对齐/64KiB；callee named参数和caller实际参数都按同一helper。临时结果附加也逐步比较64KiB剩余frame。该静态局部上限不等同任意递归总栈安全证明；真实128KiB栈和原异常回收仍有效。

新机器码仍通过原Code/Fix/map容量检查，失败在finish提交前fatal，旧成功主体保持。没有为了聚合改大SCX load/BSS、伪造SCL版本、去掉源码或改系统库函数签名。

## 8. 具体审查反馈与闭合条件

| 项 | 源点/具体触发 | 状态与必要处理 |
|---|---|---|
| hidden清栈 | N_CALL、N_RETURN、emit_function | 当前新正文已按ret4/普通arg_bytes处理，静态路径匹配 |
| 独立结果槽 | aggregate_result_slot +最终prologue patch | 新正文已落；不复用单全局scratch，sizeof不allocate |
| rvalue成员/真实pointer | lvalue/primary/address | 新正文已落non-lvalue与arrow分离，旧scalar地址分支保持 |
| named aggregate va_start | standard_builtin、S_PARAM偏移 | 新正文已落slot(last)及hidden起点，Root夹具已纳入 |
| aggregate CRT返回 | finish_image | 新正文已拒；SCD/init/fini原严格签名继续 |
| SKM aggregate唯一参数 | `int skm_main(AggPair api)` | 初版仍能过argc1而CRT只传4B；当前源已在finish_image加唯一参数aggregate拒绝，静态闭合 |
| local冲突声明 | local_declaration重用全局extern/function符号 | 当前源在涉及aggregate函数/funcptr时先复验类型；完整function保留旧参数名字，因此同函数的va_start不被局部别名原型改掉，静态闭合 |
| 数组hole溢出 | `struct Bad {int head; int (tail[0x7fffffff]);};` | 初版replace_hole直接乘，旧size0 hole可接受巨大count；换int后有符号乘法越界，负字段可能留下貌似4B aggregate。当前源已在乘前复用array_of除法阈值，成员和ABI宽度用size<=0拒绝。该例未运行 |
| map地址域 | map_function | 新正文只在shared函数行使用RVA，SCX/SCD绝对值保持；当前shared BASE=0，不能据此另称新的运行证明 |

Root已冻结独立 `AGGABI.H/AGGCALLEE.C/AGGPROBE.C`：55个实际值/隔离/嵌套/member/callback/varargs/栈检查，9个callee exports、10个Probe imports。计划同一首轮实际G1后，以普通UID1000做G1Caller→GCCLib、GCCCaller→G1Lib两次交叉；回调在同两次中反向跨工具链，不扩第三个理论矩阵。

`AggStackSample()`两次返回同一callee局部地址的整数副本，只比较、不解引用。Root会核GCC及native实际机器码为固定栈偏移而没有AND ESP对齐掩蔽低位，随后才可称精确ESP余额。不能只看到重复调用未崩溃就断言清栈正确。

55项和两交叉完成、当前source/真实G1摘要及输出合同/owner回收闭合后可结束这次ABI定向验证。新system libraries与custom AGG fixture library生成/执行字段分列，先前未执行新SC32/SCUI的事实不被custom库已执行覆盖。本文件不运行夹具，不新增编译器自举矩阵，不宣布SCUI或整个M10a2完成。

## 修订

2026-10-15：完成当前parse/std_parse/emit/std_emit只读审查，核新sret/按值/独立结果槽/成员/变参正文；固定CRT参数、局部声明与hole乘法三个具体保护已由所有者修复并再次静态核对，新源及55项交叉验收仍由Root统一执行。
