# SCL v1 二进制与宿主工具规范

版本：v1.0，2026-10-14。所属版本：**SandCore M10a2 — The Application Platform Era**。

本规范定义真正装入 Ring 3 地址空间的 `.SCL` 库文件。SCL 与 SKM 的权限和装载路径独立，不获得 Ring 0 权限，不替代已有 SCX、SCAPI.H 或历史程序 ABI。

当前文件头、表布局与验证合同由 `user/runtime/SCLFMT.H`、`scl_format.c` 和 `tools/scl_format.py` 实现。`mkscl.py` 是 **HOST_BOOTSTRAP**：输入机器码来自宿主 ELF 工具链；它不是客体 SCCC 的动态库输出，也不证明系统内编译已实现。SCCC、自动依赖 SCX 和正式 SDK 的后续接线须分别记录证据。

## 1. 数值、名称与规范化布局

所有字段均为无符号 32 位小端整数，唯独重定位 `addend` 按有符号 32 位二进制补码解释。禁止把磁盘字节强转为宿主结构体。全部偏移先做减法边界验证，表计数先以剩余长度除以记录宽度验证。

文件最大 16 MiB；映像最大 64 MiB；页为 4096 字节。文件长度必须与头中 `file_size` 完全一致。映像长度非零且页对齐；段间保留区由运行时清零。

库名必须匹配 `[A-Z0-9_]{1,31}\.SCL`，不含路径。符号名必须匹配 `[A-Za-z_][A-Za-z0-9_]{0,126}`，最多 127 字节。名称以 NUL 结束。表中的名称偏移相对于字符串区；引用必须指向字符串开头，即偏移为 0 或前一字节为 NUL。字符串区本身非空且最后一字节为 NUL；不采用 ELF 的字符串后缀复用规则。

文件按以下次序连续排列，空表也必须填写当前游标：

```
128B 文件头
段表
导出表
导入表
重定位表
依赖表
字符串区
段正文与零填充
```

前五张表之间、依赖表与字符串区之间没有空洞。段按 RVA 递增，内存范围不重叠。非空段正文在文件中按同一顺序递增、不重叠；正文前的填充及正文后的文件尾填充必须全零。宿主构建器将正文起点补齐到 4 字节，磁盘验证合同不以此额外限制已有合法正文起点。

导出按符号名严格字典序排列，依赖按库名严格字典序排列；二者不得重复。导入按 `(dependency_index, name)` 严格排序，组合键不得重复。重定位按 `patch_rva` 排序，每项覆盖 4 字节，覆盖范围不得重叠。

## 2. 128 字节文件头

| 字节偏移 | 字段 | v1 约束 |
|---:|---|---|
| 0 | magic，8B | `53 43 4c 31 4d 49 4f 00`，即 `SCL1MIO` 加 NUL |
| 8 | header_size | 128 |
| 12 | format_version | 1 |
| 16 | machine | 3，i386 |
| 20 | flags | 0 |
| 24 | file_size | 精确文件长度 |
| 28 | image_size | 非零、4096 对齐、至多 64 MiB |
| 32 | abi_major | 非零；依赖必须完全相等 |
| 36 | abi_minor | 依赖要求的最低次版本 |
| 40 | library_version | 独立发布版本整数 |
| 44 | name_offset | 相对字符串区的库名偏移 |
| 48 / 52 | segments / segment_count | 段表文件偏移 / 项数；至少一项 |
| 56 / 60 | exports / export_count | 导出表文件偏移 / 项数 |
| 64 / 68 | imports / import_count | 导入表文件偏移 / 项数 |
| 72 / 76 | relocs / reloc_count | 重定位表文件偏移 / 项数 |
| 80 / 84 | dependencies / dependency_count | 依赖表文件偏移 / 项数 |
| 88 / 92 | strings / strings_size | 字符串区文件偏移 / 字节数 |
| 96 | init_rva | 初始化函数 RVA；无回调为 `0xffffffff` |
| 100 | fini_rva | 结束函数 RVA；无回调为 `0xffffffff` |
| 104 | reserved，24B | 六个 u32 全零 |

RVA 为相对装载基址的字节偏移，RVA 0 是合法地址。不能把 0 作为“无入口”的标记。两个回调若存在，必须位于具有 X 描述位的段中。

ABI 主版本必须精确匹配，次版本及独立发布版本分别须大于或等于依赖要求；不以文件名或时间戳推断兼容性。

## 3. 段表，每项 24 字节

| 项内偏移 | 字段 | 约束 |
|---:|---|---|
| 0 | rva | 4096 对齐 |
| 4 | memory_size | 非零、4096 对齐；范围在映像中 |
| 8 | file_offset | 正文文件偏移；正文长度 0 时必须为 0 |
| 12 | file_size | 不超过 memory_size |
| 16 | access | R=1、W=2、X=4；必须含 R；不得含未知位或同时含 W/X |
| 20 | alignment | 4096 |

运行时先把完整映像置零，再复制每段的 `file_size` 字节。尾部未存储的内存保持零；`.bss` 通常为零正文的 RW 段。所有符号、回调与重定位覆盖区必须落在一段内，不能跨段边界。

描述位禁止 W+X，同时当前非 PAE x86 分页只能实际保证代码页只读，**没有 NX 执行禁止**。R/RW 描述位并不让数据页在硬件上不可执行；不能把本合同宣传为完整硬件 W^X。

## 4. 符号与依赖表

导出项 20B：

| 偏移 | 字段 |
|---:|---|
| 0 | name_offset |
| 4 | rva |
| 8 | size，非零，整个符号在单段内 |
| 12 | kind：function=1，data=2 |
| 16 | reserved，0 |

函数导出必须位于 X 段，数据导出必须位于非 X 段。名字是稳定的查询键；同一库不能导出两个相同名字。

导入项 16B：

| 偏移 | 字段 |
|---:|---|
| 0 | name_offset |
| 4 | dependency_index，依赖表下标 |
| 8 | kind：function=1，data=2 |
| 12 | minimum_size，非零 |

导入只在指定依赖库中解析，不遍历全进程全局符号空间。目标导出必须同名、同 kind，且导出尺寸不少于导入最低尺寸。数据导入以对象可访问宽度声明；函数最低尺寸不是函数类型校验，v1 的签名兼容仍由库 ABI 与公开头文件负责。

依赖项 20B：

| 偏移 | 字段 |
|---:|---|
| 0 | library_name_offset |
| 4 | abi_major，非零 |
| 8 | minimum_abi_minor |
| 12 | minimum_library_version |
| 16 | flags，0 |

可以有仅用于初始化顺序而未被符号导入引用的依赖。缺库、版本不兼容、缺符号或依赖环必须明确失败，不能静默改用同名候选库。

## 5. 重定位表，每项 20 字节

| 偏移 | 字段 |
|---:|---|
| 0 | patch_rva，完整 4B 在单段内 |
| 4 | type：ABS32=1，PC32=2 |
| 8 | target_kind：内部 RVA=0，导入表下标=1 |
| 12 | target：对应内部 RVA 或 import_index |
| 16 | addend，有符号 32 位 |

令 `B` 为装载基址，`P=B+patch_rva`，内部目标 `S=B+target`，外部目标 `S=resolved_import[target]`，`A=addend`：

```
ABS32：写入 (S + A) mod 2^32
PC32 ：写入 (S + A - P) mod 2^32
```

内部 `target` 本身须落在一个有效段中，导入下标须存在。PC32 中 P 是补丁字节地址；不能擅自再减 4。i386 `call` 的 ELF REL 加数通常已经为 -4，这由输入记录原样保留。

不支持 GOT、PLT、TLS、弱符号覆盖、按序数导出、延迟导入或 ELF 组合重定位。运行时先解析所有导入并处理重定位，再建立代码只读保护，最后调用初始化函数。

## 6. 调用、装载与生命周期

v1 引导路径使用 i386 cdecl：参数从栈传递，调用方清栈，EBX/ESI/EDI/EBP 由被调用方保留，整型/指针返回值在 EAX。指针与 int 均为 32 位。库不能链接 SCX `_start` 或调用 EXIT 终结所在应用；调用返回应用原调用栈。

初始化入口为 `int init(void)`，返回 0 成功；结束入口为 `void fini(void)`。依赖先初始化，使用它的库随后初始化；正常关闭按相反依赖方向结束。初始化函数须自行清理由它产生且在失败时需要撤销的外部副作用；未成功返回的初始化不承诺调用其 fini。回调运行时禁止重入同一 SCLDR，重入返回 BUSY。

`SCLDR.H` 定义 `ScRuntimeCreate/Open/Close/Symbol/Query/Destroy`。当前实例、可写数据及 BSS 均属于一个进程；当前不提供跨进程物理代码页共享。模块列表与依赖工作栈不使用固定模块数或固定递归深度；实际空间不足返回 MEMORY/RANGE 并回滚。

重复打开同名兼容库只增加显式引用，不重复初始化。每条依赖边另计引用。`ScRuntimeSymbol` 一旦把地址返回给调用方，就把该模块固定至运行时结束：任意函数指针可能已被保存，运行时不能猜测所有调用栈并强制卸载。此时 `Close` 释放显式引用并返回 RESIDENT；没有显式引用、依赖边和地址固定的模块才能安全立即释放。

`Destroy` 只在应用 main 已返回且没有任何库栈/回调仍在执行时调用，执行逆序 fini 并回收全部模块。进程异常退出由内核回收私有映射及其他任务资源，不承诺运行可能已损坏的用户态 fini。

当前默认平台将系统名称 `SC32API/SCCLIB/SCLIB/SCUI/SCSHELL/SCGFX/SCRES/SCLDR.SCL` 固定解析到 `/SYS/LIB/`；其余库从调用方显式绝对包目录读取，目录为空时同样使用系统目录。没有 CWD/PATH 隐式搜索。目录与文件权限仍由现有内核检查；第三方 SCL 不因属于库文件而被视为可信。受保护系统名称列表的版本扩展须同批更新公开规范。

基础错误码：FORMAT=-1、RANGE=-2、SYMBOL=-3、VERSION=-4；SCLDR 另有 NOT_FOUND=-5、MEMORY=-6、CYCLE=-7、INIT=-8、HANDLE=-9、BUSY=-10、IO=-11。成功为 0，释放引用但仍驻留为 1。失败不向调用方交付半装载模块。

## 7. HOST_BOOTSTRAP 打包器

```
python3 tools/mkscl.py input.o output/NAME.SCL --manifest manifest.json
```

输出文件 basename 必须与 manifest 的 name 一致。先完整验证并构建，再同目录临时写入和替换；无效输入不能截断已有输出。工具不会隐式链接 `start.asm`、CRT、libc 或任何依赖库代码。

清单是 UTF-8 JSON，未知字段、重复键、重复符号和不正确的整数类型均拒绝。以下顶层字段全部必需，缺回调用 `null` 明确表示：

```json
{
  "name": "EXAMPLE.SCL",
  "abi_major": 1,
  "abi_minor": 0,
  "library_version": 1,
  "init": null,
  "fini": null,
  "exports": [{"name": "example", "kind": "function"}],
  "imports": [{"name": "base_value", "dependency": "BASE.SCL", "kind": "data", "size": 4}],
  "dependencies": [{"name": "BASE.SCL", "abi_major": 1, "min_minor": 0, "min_version": 1}]
}
```

exports 只允许 `name/kind`，尺寸与位置来自 ELF 已定义全局、默认可见、非零尺寸的 FUNC/OBJECT 符号。imports 必须覆盖 ELF 的全部未定义全局符号且无多余声明；归属、类型与最低尺寸不能从名字猜测。init/fini 必须为已定义的默认可见全局函数，可以不列入 exports。依赖不允许重名或显式自依赖。

输入子集：

- 单份 ELF32、小端、i386、ET_REL；OSABI 为 System V 或 Linux，版本 0。无程序头、入口或机器标志；不支持扩展节编号。
- 唯一 SYMTAB；16B 符号项、8B REL 项。多源文件先用宿主 `ld -m elf_i386 -r` 合成一份对象；这仍是宿主来源。
- 装载节只取 PROGBITS/NOBITS，对齐不超过 4096；拒绝可写可执行节。MERGE/STRINGS 提示允许保留，但不执行字符串合并。
- 按原节顺序在 `.text` RX、`.rodata` R、`.data` RW、`.bss` RW 四组内排布，保持每节原有对齐，按组页对齐；NOBITS 只接受可写、非执行 BSS。
- 重定位只接受装载节的 R_386_32 / R_386_PC32。从 REL 补丁抽出原始有符号加数，记录到 SCL 表并将正文补丁四字节清零；运行期再按实际地址完成修补。
- 拒绝 RELA、TLS、IFUNC、COMMON、弱/unique 符号、COMDAT/GROUP、压缩节、动态 ELF 节、隐式构造数组、`.ctors/.dtors`、`.eh_frame` 与异常运行时。绝对符号不能作为导出或内部重定位目标。使用 `-fno-common` 与关闭 PIE/PIC、栈保护、异常展开的编译旗标。
- 非装载调试节不进入映像，其 REL 索引与补丁范围仍检查；该工具不提供完整源级调试符号包。全局名 `_start/sc_call` 拒绝，避免误把旧 SCX 的启动/EXIT 桥接入库。

接口 `mkscl.pack_elf(blob, manifest) -> bytes` 可用于构建工具。底层 `scl_format.build_scl` 输入 `segments` 的 `data` 为原字节、`mem_size` 为页对齐长度；外部重定位的 target 是调用方传入 imports 原列表下标，构建器排序时自动重映射。`validate(blob)` 返回完整已验证表，`relocated_image` 是只生成字节、不执行 i386 指令的宿主对照模型。

ELF 文件头、节、符号和 REL 解释依据 [System V gABI](https://www.sco.com/developers/gabi/latest/ch4.eheader.html)、[符号表](https://www.sco.com/developers/gabi/latest/ch4.symtab.html)与[重定位规范](https://www.sco.com/developers/gabi/latest/ch4.reloc.html)；i386 运算定义见 [Intel386 ABI Supplement](https://www.sco.com/developers/devspecs/abi386-4.pdf)。这些是格式参考，未复制第三方实现代码。

## 8. 检查工具

```
python3 tools/scldump.py NAME.SCL
python3 tools/scldump.py NAME.SCL --json
python3 tools/scldump.py NAME.SCL --deps directory
```

先完整验证，再显示所有段、导出、导入、重定位、依赖和字符串项，以及真实 SHA256、回调位置和版本。`--json` 输出机器可读数据；失败退出 1，JSON 错误写 stderr。

`--deps` 只从明确目录递归解析依赖，检查声明名称、ABI 主版本、最低次/发布版本、每项导入的符号 kind/尺寸及依赖环。显式工作栈避免深链调用栈溢出；每个库只读取一次。输出的初始化次序是静态分析结果，没有运行任何库代码，不代表初始化成功或客体验收。

## 9. 两库真实输入与定向宿主证据

固定输入在 `tests/m10a2/libbase.c/libbase.json`、`libupper.c/libupper.json`。两个库都没有 main、SCAPI.H、syscall 桥或依赖 libc。

初始化后 BASE：`base_value=7`、`base_state=11`、`base_init_count=1`；`base_add(10)=17`，只读 `base_message="SCL base"`。UPPER 导入 BASE 函数及数据，拥有独立 BSS、只读字符串、外部函数/数据指针、内部数据/只读指针以及单独成节的内部函数。初始化后 `upper_state=12`、`upper_bss[3]=10`、`upper_value=5`、`upper_init_count=1`；`upper_compute(10)=144`，`upper_call_pointer(10)=22`。

UPPER fini 仍调用 BASE：将 `upper_bss[3]=8`、`upper_state=-23`、fini_count 加一；随后 BASE fini 将 base_state 设为 -11、fini_count 加一。重复装载不能重新执行 init。BSS、初始计数及数据隔离须由运行时执行证据另行确认。

从 sandcore 目录运行（Windows 通过 WSL 执行编译，以下命令在 Linux shell 内）：

```sh
mkdir -p build/m10a2-scl-fixtures
gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -msoft-float -mno-sse -mno-mmx -c tests/m10a2/libbase.c -o build/m10a2-scl-fixtures/libbase.o
gcc -m32 -std=c11 -O2 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -msoft-float -mno-sse -mno-mmx -c tests/m10a2/libupper.c -o build/m10a2-scl-fixtures/libupper.o
python3 tools/mkscl.py build/m10a2-scl-fixtures/libbase.o build/m10a2-scl-fixtures/BASE.SCL --manifest tests/m10a2/libbase.json
python3 tools/mkscl.py build/m10a2-scl-fixtures/libupper.o build/m10a2-scl-fixtures/UPPER.SCL --manifest tests/m10a2/libupper.json
python3 tests/m10a2/test_scl_format.py
```

2026-10-14 本次实际 WSL 输出：

| 产物 | 长度 | 表项 | SHA256 |
|---|---:|---|---|
| BASE.SCL | 616B | 4 段、6 导出、0 导入、5 重定位 | `3c6477ae17b53ac71fe383527e9fd20c39bfaba4fbf82214a435b143e4a5498c` |
| UPPER.SCL | 1428B | 4 段、12 导出、2 导入、22 重定位 | `b67efb1af7bb6fba8e7cd738f0260132221c15c2a14569f710e8fc7d46840f88` |

`readelf -r -s` 确认 UPPER 有内部/外部 ABS32 与 PC32 四种组合。7 项定向宿主测试通过，日志 `build/m10a2-scl-fixtures/format-tests.log`：真实对象与 freestanding C/Python 验证、不同基址指针与零 BSS、16 类代表性非法字段/范围的共同拒绝码、ELF/清单子集拒绝、导入排序索引重映射、递归依赖失败诊断、JSON CLI。

这些结果不执行 i386 库函数，不代表 QEMU 内装载/回调/生命周期、只读页保护、进程隔离或客体 SCCC 构建已通过，更不代表 Phase 1 或整个 M10a2 完成。实际客体矩阵、截图和恢复接力状态由主开发验收记录管理。

同日主开发另行完成的 Windows QEMU 首切片见 `build/m10a2-scl-guest-01/scl.json`：实际 BASE/UPPER 函数与数据、重复装载初始化一次、两个运行时的不同装载地址与独立数据、只读输出写回拒绝、显式卸载/Destroy 页回收、未 Destroy 退出回收、异常退出回收及旧 SCX `ls` 均通过，并保存 1920×1080 桌面截图。该报告明确 `guest_sccc_compiled=false`、范围仅 `M10A2_SCL_BOOTSTRAP_ONLY`。本次核对 `scl_format.c/SCLFMT.H/scl_format.py` 当前摘要同时匹配宿主运行时报告与此客体构建冻结输入；`mkscl.py` 当前摘要也匹配客体构建冻结输入。规范与检查工具的后续文字/展示变更不能冒充新的客体构建。

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-10-14 | 固定 SCL v1 字节布局与规范化规则；新增 ELF HOST_BOOTSTRAP、表/依赖检查工具、两库真实输入及一轮宿主格式一致证据，另记主开发客体首切片与冻结输入一致，完整 Phase 1 尚未结束 |
| v1.0 | 2026-10-14 | 磁盘ABI不变；增加完整表边界的metadata-prefix校验用于借用启动根，完整文件payload/零填充校验保留，自动SCX及原生SCL/G1客体接线见SCX-DYNAMIC与M10A2-IMPLEMENTATION |
