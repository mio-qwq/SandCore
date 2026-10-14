# SCCC 标准库前置增量实施计划

2026-10-15。**只读源审计与实施计划，C1..C5 尚未实现；本次没有修改编译器、构建规则或 SDK，没有编译或启动 VM。** 根统一 Foundation 构建与客体验证使用的源码保持稳定。

本文件细化 [SCCLIB-DESIGN.md §3](SCCLIB-DESIGN.md#3-当前-sccc-源码审计与必要增量)，不代替 SCCLIB 实现、不扩大或削减 [M10a2RULE.MD](../../M10a2RULE.MD) 的完整目标。优先批保持32位整数/指针、真实 stdarg、真正 `_Bool`、可靠标准头；浮点/64位/聚合ABI有独立后续条件。下文新增 helper/node 名称是计划名称，当前源码中没有这些实现。

## 1. 实际入口与来源快照

当前树不存在 `user/s3c_cli.c`。实际命令行入口是 [user/sccc.c](../user/sccc.c)，仅定义 `SCCC_CLI=1` 后包含 `s3c.c`；GUI直接编译 `s3c.c`。两者共用 lex/pp/parse/emit/scl，不能只修GUI或另写一个与实际SCCC不同的“标准库编译器”。

`s3c.c::main` 顺序为路径/清单处理、`preprocess(input)`、`compile_unit()`、`finish_image(output)`。`s3c_parse.inc` 在 `s3c_emit.inc` 前包含，后端/共同类型helper需要在s3c.c有前置声明；`s3c_scl.inc` 最后包含。新的builtin不应被普通隐式函数分支登记成外部符号。

本次 HEAD=`72783199e3d2e987d4124a89b45913a4290937a2`，正在整合的工作树源不能仅以这个提交代表。相较SCCLIB-DESIGN原快照，当前parse已有 `aggregate_identity` 与前向聚合的限定副本完成同步；计划沿用它，不重做或抹掉Root这项修复。

| 当前源 | SHA256 |
|---|---|
| `user/sccc.c` | `57b0b9bd0a607fc4b4c5d87b6c2af0c952b8675d6f282c02c1e3545aca013ae1` |
| `user/s3c.c` | `e59583f3a1bae8aed53d19e4384a5baba3cb36e4a9b9f56a9e018bb9f013e91b` |
| `user/s3c_parse.inc` | `5b7cde7a6f4af652e485e5aff8fe7df97a25ffa4ff60339c5d4107db262ba815` |
| `user/s3c_emit.inc` | `6b125aa0d42f32cce848193057f51d3663a5f69df3c3d84a43a348ff78470a2c` |
| `user/s3c_lex.inc` | `daac00f4c34383cbb47ec2dfce8fa3e959ef2813c66f647284ab2ab4b75becfc` |
| `user/s3c_pp.inc` | `9ebde1119c4cf4d67d681b79bb51cae79c1a7d79b3682c4a09d977ec83acb871` |
| `user/s3c_scl.inc` | `14b6066e184fc3c845f9402181cb1df309b62ffde091f98af298ac5145408a27` |
| `docs/SCCLIB-DESIGN.md` | `c2bd4cc4d79bae0616d12fda1b63124234fa265ae7856b49636ab141d31a388d` |

下面行号只定位这个快照，实施时以函数名/字段为准。来源摘要不是新代码PASS证据。

## 2. 保持的类型字段与 ABI4

| 现字段 | 现职责 | 首批约束 |
|---|---|---|
| `Type.kind,size,base,count,fields,flags` | 24B类型记录；函数base为返回型、count为named数、fields为Parameter链、flags目前是variadic0/1 | 新bool追加kind和基础类型编号，不移动既有C_VOID..C_UINT；不要为首批把整Type表扩大 |
| `Parameter.name,type,next` | 12B，声明时按源码顺序建链 | 原型转换沿这条链；va_start可定位最右named，不新增每函数4096槽临时数组 |
| `Symbol.kind,type,value,scope,...` | 参数S_PARAM的value为EBP偏移；全局value待最终布局 | va_start验证当前函数的真实S_PARAM，不用用户传入的任意整数当偏移 |
| `Node.kind,type,a,b,c,value,where,next` | 32B；N_ARG.a为表达式，b为倒链；N_CALL.a目标、b参数链、value为argc | C1复用N_CAST；C2新增少量kind并复用已有字段，不先给所有Node加成员 |
| `Tok.k,v,where` | 12B；T_NUM/T_UNUM可区分signed/unsigned；v指向保存数值/原拼写的pool记录 | 修#条件符号属性可以在局部求值状态传递，不必拓宽全部Tok |
| `aggregate_identity[TYPE_MAX]` | 当前16KiB，限定副本共享同一聚合身份 | 类型兼容沿用身份，不能因同字段布局/暂未完成而合并不同标签 |

`compile_unit()`（parse:768）给named参数 `EBP+8+4*i`；`emit_function()`固定EBP帧，local_bytes按4取整。`N_CALL`（emit:451）按倒链逐个push EAX，间接目标另存一槽，从 `ESP+4*argc` 读回目标，最后清 `4*argc + (indirect?4:0)`。EAX/ECX/EDX为临时寄存器，EBX/ESI/EDI/EBP由callee保留；普通入口最低4B对齐。

首批bool/int/long/pointer在参数栈上都仍占4B；bool在内存对象里是1B，不能因此把参数槽改成1B。va_start从EBP求地址，与函数局部栈、asm额外push、表达式中临时push和CRT16对齐无关。当前SCL/SCX/SKM容器、重定位宽度、自动64B CRT不需要改。

## 3. C1：原型参数转换

### 3.1 解析时转换，后端保持压栈规则

适配点是 `primary()` 的调用分支（parse:405..418）。取到函数类型后，建立named Parameter游标；每个实参仍按源码顺序只解析一次，在建N_ARG前决定目标型：

1. named实参使用声明参数型，做允许的赋值式转换和类型诊断。
2. variadic余参做整数提升、数组/函数衰减；首批没有float→double路径，因为两者尚未支持。
3. 当前隐式无prototype类型count=-1的旧路径保留其已发布行为；现代标准SDK路径应强制声明完整prototype，不能默默按返回int调用malloc。不能把空 `f()` 与 `f(void)` 的现混同称作完整C89语言支持。
4. 转换结果再挂原N_ARG倒链。N_CALL只消费这个已经有正确类型的表达式，不再第二次求实参，也不反转Parameter链去迎合压栈顺序。

新增 `argument_conversion(target,expr)` 与 `default_argument_conversion(expr)` 可以用N_CAST表达实际转换。已相同/无需机器转换的32位型避免插多余节点；只有窄化、bool规范化或必要类型衰减才建节点。常量窄化可走共同常量转换，但必须保持原signed/unsigned结果和来源诊断。

`promoted()`（parse:76）已有array→pointer、func→pointer、char/short→int；它返回类型，**没有自动改调用实参树**。要把结果应用于unnamed实参，同时增加BOOL→int。调用位置不要只把N_ARG.type改掉、却留下没有转换的a表达式，后端实际发射的是a。

### 3.2 类型允许关系

| 实参/目标 | 应有行为 |
|---|---|
| 整型→整型 | 根据目标有符号/宽度转换，bool先比较整值是否0 |
| 对象指针→void*，void*→对象指针 | C的合法对象指针转换，保留必要的目标限定；不得据4B大小顺便允许function*↔void* |
| T*→const T* | 允许增加直接所指对象的限定；const T*→T*需要诊断，不能无声丢const |
| T**→const T** | 不能将“任意递归层加const”都当合法；保持C多级指针的限制 |
| 函数/数组表达式 | 在实参值上下文衰减，函数签名须兼容；void*规则不替代函数指针规则 |
| 0/null pointer constant→指针 | 仅真正的空指针常量关系；运行时整数0变量不是自动合法指针实参 |
| void表达式或aggregate值 | 首批拒绝，不把结构地址push4当按值传递，不接受未完整对象 |

当前 `sclc_type_same()`（scl:285）是**声明兼容**：忽略参数最外层const/volatile，递归保留所指对象限定并用aggregate_identity。可以抽取共同类型比较，但不能直接用“完全同型”来拒绝合法的增加限定/void*赋值，也不能把声明比较的宽松count=-1规则用于现代调用检查。

`Type.flags`在普通类型表示Q_UNSIGNED/CONST/VOLATILE，在FUNC目前表示variadic；新增函数属性必须用明确掩码，现调用判断不能继续把“任意非零flags”都视为variadic。首批无需新增属性时保持字段不变。32位long目前与int同一基础类型，机器槽兼容不等于完整C类型identity/rank；完整语言覆盖另需独立区分，不用同宽性宣称不同指针类型都兼容。

`emit_expression::N_CALL`保持现direct F_REL/indirect保护槽与清栈。首批全部4B，Node.value仍可为argc；将来8B参数必须改为真实argument_bytes，不能在这里偷偷用“argc仍乘4”支持long long。

## 4. C3：真正 `_Bool` 的所有转换路径

这项应先于C1的bool参数完整开放，也先于公开stdbool.h。

| 适配点 | 具体工作 |
|---|---|
| `s3c.c` TY枚举；parse基础C_*及compile_unit | 追加TY_BOOL/C_BOOL，建size1/align1基础型；原0..6编号保持，枚举作为编译器内部描述不改变公开ABI |
| `declaration_spec()` / `type_ahead()` | 识别 `_Bool`、const/volatile资格，拒绝unsigned/short/long/int等非法组合，不把bool解释成用户变量或typedef char |
| `promoted()`、binary/unary/conditional | BOOL提升到signed int；指针/整数到bool比较整32位是否0，不能先截低8位 |
| `cast_value()`（emit:94） | BOOL发 `test eax,eax; setne al; movzx eax,al`，借现test_eax/bool_result(5)，8B基本序列；所有合法scalar来源结果0/1 |
| `load_value`、frame/global load | BOOL用零扩展字节；更新frame_scalar/global_scalar的kind白名单，保留原优化和volatile真实访问次数 |
| `store_value()` / `frame_store()` | 规范化后写byte，赋值表达式EAX也返回规范值；不能只让存储内存正确、外层表达式仍是256 |
| `N_INC/N_ASSIGN` 两条路径 | 原运算后调用共同目标转换：true++仍true；false--转true；后缀保留旧规范值；指针/字段/数组/volatile也一次读写 |
| `N_RETURN` / C1 named转换 | 返回与调用边界一致，bool内存1B、cdecl slot4、EAX为0/1 |
| `constant_value()` / `global_init()` | 见下面：目标型常量转换必须独立覆盖，不能仅更新运行时cast |

`global_init(type,offset,expr)`（emit:551）当前求constant_value后直接写目标size字节；`initialize()`（parse:568）给N_INIT保留目标型，但通常没有显式N_CAST。故 `_Bool b=256;` 即使运行时cast已修，旧global_init仍会把低字节0写入。结构/数组内的全局bool、static局部、const初始化也走此路径。

计划新增共同 `constant_conversion(type,value)`：N_CAST常量、global_init目标写入调用同一规则；布尔先value!=0再存。局部N_INIT（emit:896）最终走store_value，可复用运行时规则。合法静态地址到bool还需识别零地址常量/非零对象地址；不生成1B绝对重定位、不把所有地址表达式交u32常量算术后误拒绝。未支持的复杂表达式明确诊断。

`qualified()`当前复制类型，并在聚合完成后同步同身份副本；bool无需另建aggregate身份。bool unsigned加载特性必须与promoted→int区分，避免bool参加运算后误选无符号比较。首批不实现bit-field `_Bool`，现bit-field整体仍拒绝，清单保持明确。

## 5. C2：ABI4 变参 builtin

### 5.1 语法、类型与节点

保留实现命名空间的 `__builtin_va_start/arg/copy/end` 在primary的普通T_ID隐式函数处理前识别。SDK宏将标准名字映射到它们；`va_arg(ap,type-name)` 第二项按 `declaration_spec + abstract declarator` 读取，不能用普通 `expression(2)` 解析类型关键字。

首批va_list的共同布局为指向下一个4B参数槽的游标；SCCC头可用已支持的pointer typedef，四builtin负责语义。GCC引导头使用其真实builtin并在i386互调用时核对布局，不能以64位宿主va_list的布局当客体ABI。

新增四种节点即可复用现Node32：START的a为ap左值、value为已验证扫描起点偏移；ARG的a为ap左值、type为请求结果型；COPY的a为dest左值、b为source；END的a为ap左值。具体字段选择在实现时统一，不能修改N_ARG/N_CALL既有含义。

| builtin | 静态约束 |
|---|---|
| START | 当前function_symbol有效且函数variadic、有named；parmN是当前函数最右named的真实identifier/S_PARAM，符合首批支持的类型；扫描起点`EBP+8+4*named_count`，不按sizeof(parmN)加地址 |
| ARG | ap是可修改的合法va_list左值，type为首批int/unsigned/long/unsigned long/指针；va_arg可以在接收va_list的非variadic v函数中使用，不能错误限制为只能在variadic定义内 |
| COPY | dest是合法可写va_list左值，source类型匹配；两项各求值一次，复制后两个游标独立 |
| END | ap是合法va_list左值，各求值一次；可将其置0使状态明确，不生成库调用 |

char/short/_Bool请求不等同实际默认提升后的int，给诊断；void/array/function/aggregate/FP/64请求在首批拒绝。va_arg读不到动态实参的类型标签，实际数量与相符类型仍由调用方遵守；不能承诺运行期识别所有错配。va_start/copy/end配对按标准生命周期使用，不用没有控制流分析的编译器假称能证明所有路径正确。

新builtin不得产生S_FUNCTION符号、导入条目、F_REL或私有syscall。它们只访问本栈和ap对象；标准库函数本体仍作为SCCLIB独立SCL存在。

### 5.2 后端顺序与栈平衡

| 节点 | 可以沿现后端实现的步骤 |
|---|---|
| START | `address(ap)`一次，保存目的；LEA从EBP取得编译器决定的首unnamed地址；按pointer4写ap，无须移动ESP |
| ARG | `address(ap)`一次到EAX；EDX保ap对象地址，EAX载当前游标；ECX=EAX+4；先把ECX写ap，再从原EAX载4B结果；只用caller临时寄存器，无callee寄存器破坏 |
| COPY | 一次求dest地址，原栈暂存；一次取source游标，取回dest地址后store4；push/pop必须平衡 |
| END | 一次求ap地址，写0；类型为void，不读取下一个参数槽 |

用现 `address()/memory()/move_reg()/store_value()`，保持复杂ap如`*pointer_to_ap`的一次求值。ARG在`sizeof`中只建类型，不执行游标推进；全局常量初始化不能调用非ICE builtin。N_EXPR/N_COMMA/N_COND含这些节点时保留顺序/短路，没有凭名字调用一个返回0的普通函数。

直接/间接variadic函数都有相同named槽偏移。间接目标暂存槽属于caller私有保护区，位于全部实参之后，不算callee named或unnamed参数。callee局部帧和asm保存位于EBP以下，不改变参数读取。无须将全部调用改16B对齐，也不应为每次va_arg开辟BSS固定大数组。

## 6. C4：整数常量、#if 与 offsetof

### 6.1 当前失真点

`lexnext()`（lex:134）u32逐位乘加后才分类，超过UINT_MAX可已回绕；后缀循环吞任意连续U/L，`1LL`、重复U也未真实表达其C类型。目标没有long long，声明处虽拒绝，字面量处仍可能降为int。必须统一拒绝，不能把“该字面量值小”作为64位语法真实支持。

`pp_expression()`（pp:238..281）从T_UNUM拿值后没有保留unsigned；比较、除法与右移统一cast int。故 `#if 0xffffffffu > 0x7fffffff` 的结果会与正常C unsigned比较不一致。只改lexer的T_UNUM选择不足以使标准限值宏可用。

| 适配点 | 首批工作 |
|---|---|
| lexer digit loop | 在`value*base+digit`之前用除法阈值检查UINT_MAX；合法基数/至少一位/尾字符继续严格验证 |
| lexer suffix | 接受本阶段合法U、L、UL/LU及大小写组合；LL/ULL明确未支持；重复U、不合法混合与溢出诊断 |
| primary数字型 | 按已选择的32位类型范围分类；保留C89基础与选择C99之间的差异，不伪造无64位条件下完整C99十进制常量规则 |
| pp局部求值 | 返回/携带value与unsigned状态，按本阶段整数提升/常规转换选择比较、除法、右移；短路未求值分支仍不触发除0；无需给Tok32位之外加字段 |
| make_number/token_text/#/## | 合成数值与保留的原拼写一致，unsigned状态不在重拼接/重新词法化时丢失；新增token种类时这些spell分支同时更新 |
| 常量目标转换 | 与C3共用，区分普通unsigned回绕和必须诊断的扫描/容量溢出；不把已有signed UB重新包装成安全数学 |

32位long/int类型身份当前混同，完整rank和常量类型需另补；首批限值宏使用可表示构造，如LONG_MIN=`(-2147483647L-1L)`。这里修支持范围内的错误，不发布LLONG_MAX或UINT64_C假宏。

### 6.2 offsetof 的真实 ICE

在primary添加保留builtin，例如 `__builtin_offsetof(type-name,member-designator)`，直接使用Type.fields、Symbol.value（字段字节偏移）和array stride；最终为 `constant(offset,C_UINT)`。解析nested member/选择的常量array下标时先验证完整类型、成员存在与乘加范围，不计算或解引用空指针、不产生address fix。

公共宏从offsetof展开到它，结果可在数组界、枚举和static initializer中使用。现 `constant_value()`只认const/cast/unary/binary等有限节点，不能仅放 `((size_t)&((T*)0)->m)` 进头然后宣称ICE已经合格。bit-field/不完整型/无效成员继续诊断；不把该builtin当VLA、flexible array或任意GCC语法支持。

## 7. C5：SDK、诊断与自编译入口

| 位置 | 计划 |
|---|---|
| `preprocess()` 的预定义数组（pp:517） | 分别增加真实VA32/BOOL/OFFSETOF能力宏；名字随实现冻结，不能仅看到头存在就置1；保留HOSTED0，不伪造STDC_VERSION |
| `pp_include()`（pp:342） | 小写标准头真正上盘；核查局部同名私有头、SYS/INC根身份与cwd；沿一次解析，不引入新的rootless二次resolve |
| 标准宏 | stdarg type-name宏可走现参数展开；assert必须在NDEBUG变化后重新选择，不能整头guard锁死；argc8不是函数参数数上限 |
| `primary()` 的未知函数分支（parse:366附近） | 现代SDK/动态应用对未声明标准函数给明确诊断，避免malloc被隐式int截成错误返回；旧语言兼容策略显式设置，不悄改所有旧SCX源码承诺 |
| `compile_unit()` 函数开始处 + primary | 若选择C99 assert，产生真正只读`__func__` char数组；按函数名登记一次，sizeof保留数组长度，与SCL字串只读span共用；不作为全局pp文本替换 |
| `scl_declaration_type()/sclc_type_same()` | 保持原型、最外层参数限定规则与当前aggregate_identity；现代import必须完整prototype，builtin不进入该符号表 |
| SDK/GCC分支 | GCC用其真实builtin，SCCC用新增路径；不依赖SCCC缺失的statement-expression/typeof/r-m asm等扩展；不以host通过替客体 |

编译器实现自身保持现可自编译的C子集，类型图内新增TY_BOOL不要求实现代码自己开始写 `_Bool`；builtin实现用普通int/指针/现asm编码helper即可。不要让新编译器源码必须先include未发布stdarg.h/bool头，切断现客体G0→G1引导链。最终新SCCLIB确需由真正支持这些能力的G1/G2生成和执行。

`fatal()`仍在提交正文前停止，日志/map/源身份保护沿现规则。新增诊断不能先覆盖输入、回退运行旧输出或输出半个动态清单。已有SCAPI.H字节、prefix24/64、SCL无CRT、SKM入口与权限保持。

## 8. long long、FP、聚合与正常退出的范围

| 能力 | 优先批是否必需 | 真实后续条件 |
|---|---|---|
| C1标量prototype转换 | 必需 | 已有字段可实现，含function pointer与named/unnamed区别 |
| 真stdarg4槽 + va_copy | 必需 | C2执行证据后发布printf/v组及stdarg头 |
| 真_Bool | 必需 | C3所有存储/常量/调用路径闭环，不能char替代 |
| constexpr/offsetof/可靠限值头 | 必需 | C4修lexer与pp两端，真实ICE |
| long long / `%ll` / int64_t | 首批明确未支持；不是从完整目标删除 | 新8B类型/常量、双字存取/运算/转换/比较/移位、EDX:EAX返回、真实8B参数与清栈、va_arg8推进，signed边界与runtime helper；不能只改declaration_spec允许第二个long |
| float/double/long double及浮点格式 | 首批明确未支持 | 解析/类型/算术/提升/参数/返回与用户任务状态、math/strto/format一致，不能用int输出0代替 |
| aggregate按值/返回 | 非优先字节库阻塞项，但div/ldiv标准签名仍待此项 | 明确i386参数复制、hidden return策略及双工具链互操作，不能ABI4函数偷偷改变旧参数布局 |
| restrict、完整K&R/no-prototype、long独立rank、公开setjmp/signal | 完整ISO目标仍有缺项 | 单列语言/ABI合同与验证；首批标准函数范围不依赖伪声明 |
| `exit/atexit`正常终止 | SCCLIB优先生命周期必需；不属于单纯C1..C5 | Root R1 prepare hook + 真实continuation撤离SCCLIB帧，随后逆依赖fini；本计划不改冻结root，也不把私有退出点称公开setjmp已完成 |

现WIDE inlineasm只提供EDX:EAX乘积/ADC等指令，未给C表达式完整64位类型；ScWideCounter两个u32成员也不改变这个结论。优先阶段允许明确32位库profile，禁止发布能链接却永远失败的标准声明当完成功能。

## 9. 自编译容量与成本

### 9.1 静态可算的成本

当前明确CLI include图14个文件：sccc.c、s3c.c、五inc、COMPILECLI.inc、SCIO.H、SCAPI.H、SCLFMT.H/SCDYN.H、scl_format.c/scd_format.c。实际文件长度总 **237755B**；若各路径只装入一次，加每源NUL得到237769B，相对SOURCE_MAX262144仅余 **24375B**。这是读取现文件长度的静态预算，未运行预处理；路径别名/实际客体源安装布局可能使source_count/source_used不同，不能称实际自编译容量PASS。

| 固定表 | 当前上限/记录尺寸 | 多一成员的实际代价 |
|---|---|---|
| Type | 4096×24B | 每条加4B令编译器BSS增加16KiB |
| Symbol | 6144×32B | 每条加4B增加24KiB |
| Parameter | 4096×12B | 每条加4B增加16KiB |
| Node | 8192×32B，每函数清树 | 每条加4B增加32KiB；C1另插cast会消耗该函数节点余量 |
| Tok | 65536×12B，另有宏/行缓存 | 主token表加4B已增加256KiB，还会同时扩大共享Tok缓存，不能轻率新增token成员 |
| Macro | 384×56B，body8192 Tok | capability/标准宏增加实际槽；undef后macro_find只找active，再define可能新增槽，body也累积而不回收 |
| code/data/fix/map | 262000/131072B、12288 Fix、32768B map | builtin/诊断/自编译函数扩充分别消耗输出、字串、引用和符号行，不只有源码字节成本 |

编译器自身SCX load+BSS必须≤0x3d0000=3997696B，code+initialized DATA另受CODE_MAX限制，local frame≤65536B、用户栈131072B。当前静态固定工作区没有malloc替代；扩大表会直接进入客体BSS。不能以SCL映像可到64MiB为理由绕过编译器自身旧SCX私有区上限。

C3追加kind/基础型不扩大表实体；C1最多给需要转换的实参增加一节点（32B逻辑树占用），全相同的int/ptr调用不机械包cast。C2节点复用现字段、寄存器和小临时栈；C4pp局部状态可用value+unsigned两个普通标量/输出参数，避免升级所有Tok。新增boolcast约8B机器码的估算只是基本序列，不是完整生成程序大小测量。

源码新增约24KiB就可能碰现source arena；标准SDK会另消耗macro/source/file/type容量。当前“SC32头340个宏”是已有注释中的特定输入记录，不可机械推出新SCCLIB翻译单元还有44个可用宏。必须统计实际所有被包含的头、活跃/历次定义、node峰值以及源去重路径。若真的溢出，保留具体输入/计数，再调整那个表或拆合理源码组织；不一次放宽所有上限，也不删标准优先函数来省空间。

### 9.2 后续测量位置

`tools/check_s3c.py` 的旧host builder只展开四inc，没有完整当前SCL/GUI适配，不能未经审阅直接运行并当当前链路证据。`tests/m10a2/test_scd_format.py::build_sccc` 已读取五inc、替换CLI/GUI桥、调用actual main并记录token/source/code/data/BSS/fix/macro，可作为以后定向backend诊断的基础；它不执行生成的i386字节。现脚本源码读取不等于本次运行了它。

以后由Root选择一个实际翻译单元覆盖新增语言语义，收齐source_used、source_count、token/type/symbol/parameter/macro/body/pool、node峰值、fix、load/BSS/stack/map占用。GUI/CLI有不同bridge源容量，应按影响核查，不凭一个宿主空main的余量保证完整自编译或正式SCCLIB源能容纳。

## 10. 代码生成验证何时触发、何时结束

当前禁止提前构建/VM。本表是实施完且Root统一冻结新输入后的定向验证条件，不授权新增大矩阵。

| 实质变更 | 必要出口 | 可复用与结束条件 |
|---|---|---|
| C1实参转换/类型诊断 | 同一程序的direct/indirect、嵌套副作用、窄参数/void*/const增加/named-vs-unnamed；真实SCCC生成后执行 | 每实参一次、callee值/stack balance正确后结束；不重跑网络/字体或全任务矩阵 |
| C3cast/load/store/global_init | 常量/局部/全局/聚合字段、256/负数/非空低字节0指针、prefix/postfix、volatile及bool参数/返回 | 护栏和值一致，原int/char优化受影响的代表路径通过即可，不自扩所有类型排列 |
| C2builtin语法/后端 | 真variadic和函数指针调用遍历整数/指针；v函数、copy独立遍历、复杂ap一次求值、sizeof不推进；最右参数/未支持type诊断 | 无导入/无syscall、游标步长4、两遍值一致；宿主只核编码，实际客体执行才验收 |
| C4lexer/pp/offsetof | supported maxima/minima、溢出/非法LL失败保护、unsigned#条件、nested offsetof ICE三处使用 | 有效语义与错误不提交正文；未变化旧格式表校验直接复用 |
| C5SDK路径/宏/函数名 | 实际公共头混合翻译单元、非根cwd、宏重包含、函数实体地址、manifest导入与能力宏 | 头存在/host include不算完成；guest SDK实际读取/调用通过结束 |
| 任一编译器实现源码/表改变 | 现Foundation客体编译器G0真正编新G1，G1执行定向语义与生成优先SCCLIB；记录自身load+BSS/实际源计数 | 旧G1来源摘要不能覆盖新源；是否需要再编G2按具体自编译问题决定，不默认G2/G3/全部旧组件矩阵 |

旧compiled SCX/SKM继续运行的ABI与“新compiler对同源输出完全相同”分开记录。只增加新builtin且未使用时，代表旧程序可以要求原字节；修复原错误的参数/常量语义导致必要字节改变时，说明触发源与正确结果，不能用字节差异自动判兼容失败，也不能忽略本来应该不变的布局/入口。SCL/SCD表、data span和BSS回填仍由原共同validator核验；没有修改它们本体不额外扩格式suite。

## 修订

2026-10-15：定位实际sccc.c入口与当前聚合身份增量；细化C1参数树转换、C2 ABI4 builtin节点/发射、C3运行时与全局常量bool转换、C4 unsigned预处理/字面量/offsetof、C5SDK及自编译；列静态容量和按改动触发的定向证据。仅计划，编译器增量尚未实现或验证。
