# SCCC 原生 SCL 输出切片

版本：v0.2，2026-10-14。所属版本：**SandCore M10a2 — The Application Platform Era**。

SCCC 使用自己的词法、预处理、类型、语法和 x86 后端生成机器码，由 `s3c_scl.inc` 直接封装 [SCL v1](SCL.md)。不调用宿主 ELF 打包器，不复制依赖库正文，不生成库的 main/EXIT 入口。实现、输入与宿主检查已收齐，2026-10-14 Windows 客体 SCCC0/G1 编译及真实两库调用通过，证据见第7节；完整 Phase 1 和 M10a2 仍未完成。

## 1. 入口与兼容

```
sccc source.c output.SCL --scl manifest.json
s3c source.c output.SCL --scl manifest.json
```

CLI 和保留的图形外壳均解析四参数。独立 shared_output 标志选择新路径，不根据扩展名猜模式。普通 SCX、可选图标和 `--skm` 仍用旧入口、布局、地址回填和 ABI。编译器自身可继续使用冻结 SCAPI.H，未修改其内容。

新模式对输入/输出/清单各调用一次 RESOLVE，得到从根目录起算、**不含开头斜杠**的规范名，保持63B容量。这个旧ABI输出不能再次作为cwd相对路径解析；CLI与GUI共用一次解析支路。按 SandFS 的 ASCII 大小写比较身份，并忽略可选的开头根斜杠。正文、`.log`、`.map` 均不得与源或清单重名；日志是输入别名时直接诊断返回，不调用会写日志的 fatal。长输出路径的 SCL 日志回退明确使用 `/HOME/S3C.log/.map`，也参与身份检查。旧输出模式的路径行为保持。

继承现有整数、指针、数组、结构体和 cdecl 子集，不承诺新增 ISO C 覆盖。浮点/64 位整数等边界仍在。历史词法 LL/整数字面量边界不在本次改动中，后续标准库阶段须独立审计。

## 2. 与宿主工具相同的严格 JSON

九个顶层字段全部必需，缺回调用 null：

```json
{
  "name": "EXAMPLE.SCL",
  "abi_major": 1,
  "abi_minor": 0,
  "library_version": 1,
  "init": null,
  "fini": null,
  "exports": [{"name": "example", "kind": "function"}],
  "imports": [{"name": "base_value", "kind": "data", "dependency": "BASE.SCL", "size": 4}],
  "dependencies": [{"name": "BASE.SCL", "abi_major": 1, "min_minor": 0, "min_version": 1}]
}
```

清单最大 1 MiB。未知/重复键、缺项、重复符号/依赖、自依赖、非法名称/kind/NUL 和越界整数均拒绝。名字解码后只能为合法 ASCII，Unicode 转义只能产生 ASCII。整数为无符号十进制 u32，拒绝负号、浮点、指数、前导零或 true/false；主 ABI 和最低符号尺寸非零。输出 basename 必须与 name 一致。

导出仅取清单明确列出的实际外部链接定义，不自动导出所有全局，static 不得逃逸。导入必须已由 C 声明、尚未定义且被真实代码/初始化指针引用；未引用的公共头 extern 不制造导入，未使用的清单 import 拒绝。归属严格限定到指定依赖。

完整数据对象的 minimum_size 不少于其 C 宽度；不完整 extern 数组要求完整元素，minimum_size 不少于一个元素。清单不改变 C sizeof。不完整数组定义后的兼容 extern 重声明保留已确定尺寸；完整函数原型也保留。形参最外层 const/volatile 不属于函数类型，指针所指限定仍比较；类型嵌套检查超过 32 层明确失败。static 原型后的普通定义/extern 继承内部链接，外部链接改 static 诊断冲突。函数导入必须有显式完整原型。

## 3. 段和回填

旧 SCX/SKM 模式的数据顺序不动。SCL 模式为字串和初始化对象登记来源范围、对齐、权限、最终 RVA；来源区间保持递增且不重叠，回填通过二分映射。

| 内容 | 描述位 | 最终处理 |
|---|---|---|
| 函数 | RX | RVA 0 开始，省去 SCX 24B main/EXIT 桥 |
| 字串与 const 对象 | R | 依对象对齐重排；无初值 const 物化为只读零数据 |
| 普通初值对象 | RW | 包括内外部初始化指针 |
| 普通 BSS | RW | 独立零正文段 |

空类不生成空段，实际段均页对齐。指向 const 的可写指针对象仍在 RW。实际映射和只读保护由 SCLDR/内核完成，不宣称 NX。

F_REL 转内部/导入 PC32，加数为旧 addend 减 4，运行时使用 `S+A-P`。F_ABS 转内部/导入 ABS32。F_DATA 在新模式分别保留字串起点与额外加数，避免一过尾指针误归相邻来源区间。负数据补丁 offset 先恢复为 `-offset-1`，再映射到重排后的对象 RVA。

导出/依赖按名字排序，导入按依赖下标/名字排序，重定位按补丁排序并拒绝重叠。元数据和排序工作区按实际数量申请，完整检查乘加、文件/映像上限和补丁覆盖。init 仅接受外部定义的有符号 `int(void)`，fini 仅接受 `void(void)`，均无参数/可变参数；名称可以不导出。

## 4. 提交与 RVA 图

`.SCL` 保存全部标准表，并先由共同 scl_validate 验证。共同验证源码只进入编译器，不进入生成的库。`.map` 用 `SCLMAP1MIO` 标明 RVA，保留函数/源行及 EXPORT、IMPORT、REQUIRE 辅助行；导出地址是 RVA，IMPORT/REQUIRE 的地址列是表下标。正式 ABI/尺寸和全部动态表以 SCL 为准，可用 scldump 检查；完整动态源级调试接线另列。

语法、清单、类型、静态链接、回填、容量、公共验证或 map 写入失败，均不打开已有 SCL 正文。所有会 fatal 的 map 提交放在最终正文提交之前。正文与 map **不是跨文件原子事务**：map 已写而正文磁盘 I/O 失败时，map 可对应未提交候选；正文 I/O 遵守现有 SandFS 整文件写语义并报告失败。副产物不能覆盖输入。

正常结束释放清单、排序、区间、导入及候选映像工作区；fatal 退出由既有进程生命周期回收所属页。不使用新内核测试入口。

## 5. 容量与明确来源

最新CLI的12份自编译输入正文加各文件NUL共217334B，超过旧 SOURCE_MAX=196608，源码区因此调整到262144。新增函数的真实符号图超过旧16KiB，map_text调整到32768。其余 TOKEN_MAX=65536、CODE_MAX=262000、DATA_MAX=131072、SCX 私有映像0x3d0000和128KiB栈保持检查。

首次冻结的宿主真实SCCC后端自身结果为load=190421B、BSS=3535004B、tokens=48855、code=178428B、data=11993B、fixes=3905，source=217212B，保留为路径修复前历史。最新真实客体生成并执行的G1为load=190453B/BSS=3535004B，见第7节；没有G1/G2/G3编译器完整字节固定点结论。

宿主诊断在 `build/m10a2-sccc-host/`。host_check.py 只替换 I/O，实际前后端来自源码。12 个定向项通过：两库生成与共同验证/四种回填/RO/static、旧 SCX/SKM 代表输入与 b00b8ce 旧源全字节一致、map 故障与四类坏清单保留正文、数组/形参 CV 声明边界、不完整数组导入、当前自身源码生成。最后 main 路径保护变更不影响宿主 harness 前缀及前后端/验证器/夹具，报告 reuse_basis 明确复用前 11 项，只刷新自身源码输出；路径保护另有只读审查，实际客体分支另验。

| 宿主真实 SCCC 后端产物 | 长度 | 重定位 | SHA256 |
|---|---:|---:|---|
| BASE.SCL | 732B | 6 | `bafa9fd7cce5e7bccc4f5fbbe58f699bf082d639a1f1b07a59a1b5422ea2edfa` |
| UPPER.SCL | 1660B | 22 | `502498d29fdd9ae0271d0c4c95e1293fb9b848ed6481f756821a679c527e6f9d` |

## 6. 客体交接输入

`NATIVEBASE.C/NATIVEBASE.json`、`NATIVEUPPER.C/NATIVEUPPER.json` 在 tests/m10a2。只用当前 SCCC 子集，没有 SCAPI/syscall/libc/宿主属性。BASE 的 static base_private=2 参与计算但不得导出。独立包目录可使用：

```
/HOME/M10A2/SCCC0.SCX /SYS/TEST/NATIVEBASE.C /HOME/M10A2/NATIVE/BASE.SCL --scl /SYS/TEST/NATIVEBASE.json
/HOME/M10A2/SCCC0.SCX /SYS/TEST/NATIVEUPPER.C /HOME/M10A2/NATIVE/UPPER.SCL --scl /SYS/TEST/NATIVEUPPER.json
```

还须安装新 s3c_scl.inc、runtime/SCLFMT.H、runtime/scl_format.c 才能在客体生成新版编译器；不能将顶层复制规则当作已安装子目录。

预期保持：BASE value=7/state=11/init_count=1，base_add(10)=17；UPPER value=5/state=12/bss[3]=10/init_count=1，upper_compute(10)=144、upper_call_pointer(10)=22，只读消息为 SCL upper。UPPER fini 调 BASE 后 bss[3]=8/state=-23，BASE 随后 state=-11。

主开发接续同一 Windows VM 执行 SCCC0 → 两库 → 实际调用，以及客体生成 G1 → 两库字节对照/调用，配合代表旧 SCX/SKM、坏清单、路径别名和 map 失败保留主体。当前宿主结果不替代这个客体闭环或全组件验收。

## 7. Windows客体实测与路径修复

证据入口：[`evidence/m10a2-native`](../evidence/m10a2-native/README.md)。GCC启动编译器SCCC0来自native-build-02，load108337B/BSS3535343B，属于HOST_BOOTSTRAP。Windows双盘native-guest-02从只读M10a1基盘建立一个独立会话盘；直接安装本轮源码/验证器/JSON和SCCC0，没有宿主代编生成两库。

SCCC0在客体生成BASE732B/UPPER1660B并由SCLPROBE实际调用；随后SCCC0在客体编译完整CLI源码树得到SCCC1.SCX，190489B（36B头+190453B正文）、BSS3535004B、栈131072B、SHA256 `3830fca861b3c3a7dc3852f981b65d1ecd5fcdd2d3778f393ca1e60afc18432e`。这个G1实际执行并再次编两库，其BASE/UPPER与SCCC0输出全字节相同，并再次真实装载调用通过。此处的一致是**两份库输出**，不是GCC与SCCC编译器映像相同。

同VM仅做相关回归：原盘旧SCCC与新版的代表SCX/SKM输出全字节一致；map路径为真实目录时编译失败且旧正文不变；未知清单字段失败保留旧正文；日志路径与输入别名直接拒绝，源字节保留；未Destroy的原生库退出PF暖基点8869→8869。原基盘与本轮当前输入SHA均不变，真实HMP输入/1920×1080桌面截图已查看。执行身份来自外部SYSTEM Shell，尚无完整普通身份/GUI动态开发验收。

首轮native-guest-01曾失败：共享模式重复RESOLVE，把/TMP输入加上cwd=/SYS，找不到清单。报告/截图/错误代码与编译器保留；修复后仅重建编译器并重跑该闭环。原host_check替换main，只证明真实前后端，未覆盖这一接口语义；rootless_check保留真实CLI/GUI main，以旧ABI的ROOTLESS适配补四项必要回归：各参数解析一次、cwd SYS不叠加、63B容量、/HOME日志fallback别名保护。

动态SCX启动导入、正式SC32API/四库/SDK、自编译固定点和完整平台验收仍是后续工作，不以此切片关闭Phase1。

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-14 | shared_output/严格JSON/只读重排/动态表/RVA图收齐；修声明与副产物边界，记录实测容量和定向宿主证据，客体闭环另列 |
| v0.2 | 2026-10-14 | 修复重复RESOLVE及日志根斜杠身份；SCCC0/G1真实两库输出/调用与定向旧格式/失败保留通过，保存首轮失败和原始代码 |
