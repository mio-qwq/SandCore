# 动态 SCX 启动与 SCD1 规范

版本：v1.0，2026-10-14。所属版本：**SandCore M10a2 — The Application Platform Era**。

动态应用继续使用 [FS.md](FS.md) 的原始 **SCX1MIO 36B 头、固定 0x400000 基址和旧退出语义**。本规范新增的是应用初始化载荷末尾的 SCD1 描述符，以及工具自动生成的私有 64B CRT；不改变旧 SCX、图标或 SKM 格式。真正的依赖装载、导入解析、初始化、main 调用和正常清理由 Ring3 `/SYS/LIB/SCLDR.SCL` 执行，应用不静态复制依赖库或装载器正文。

## 1. 独立应用清单与入口

```
sccc source.c NAME.SCX --dynamic app.json
s3c source.c NAME.SCX --dynamic app.json
python3 tools/mkdscx.py app.o NAME.SCX --manifest app.json
```

应用模式只接受以下四个顶层字段，全部必需；不能用 `--scl` 的九字段库清单替代，也不根据 `.SCX` 扩展名隐式选择动态模式：

```json
{
  "kind": "application",
  "name": "NAME.SCX",
  "dependencies": [{"name": "BASE.SCL", "abi_major": 1, "min_minor": 0, "min_version": 1}],
  "imports": [{"name": "base_add", "dependency": "BASE.SCL", "kind": "function", "size": 1}]
}
```

应用名称匹配 `[A-Z0-9_]{1,31}\.SCX`，输出 basename 必须相等。依赖与导入子项的名称、字段、kind、版本、尺寸、唯一性及 JSON 严格规则与 [SCL.md](SCL.md)、[SCCC-SCL.md](SCCC-SCL.md) 相同。清单最大 1MiB；未知/重复键、负数、浮点、指数、布尔整数、越界整数和 NUL 均拒绝。可以声明只参与初始化顺序的依赖；manifest import 必须是实际初始化正文或代码引用的未定义外部符号，未使用的头文件原型不制造导入。

原生 SCCC 要求外部定义的有符号 `int main(void)`，无参数/可变参数，且函数有实际机器码。应用仅以普通 C 函数/数据声明调用公共库接口，无需 `SCAPI.H`、内联软中断或 `ScRuntimeOpen`。ELF 不记录 C 返回类型/参数类型，因此宿主工具只能验证定义的默认可见全局 FUNC `main`，源码作者和编译器须保证该 C 合同；不能把 ELF 符号检查当作函数类型检查。

源、输出、清单各只调用一次旧 RESOLVE。返回的根内无前导斜杠名字不再次按 cwd 解析。输出、`.log`、`.map` 不得与源或清单形成 ASCII 大小写/根斜杠别名；长日志路径的 `/HOME/S3C` 回退也检查。语义、容量、回填、公共格式验证或 map 写入失败不打开已有正文；map 与正文仍不是跨文件原子事务。

## 2. 初始化载荷与地址

```
RVA 0       自动生成的64B CRT
RVA 64      本程序函数正文
             原生SCCC的旧初始化DATA顺序
metadata_rva SCD1头 → dependencies → imports → relocs → strings
load_size    零初始化BSS
```

SCD1 必须恰好占据 SCX 的初始化载荷尾部，`metadata_rva + metadata_size == load_size`。`program_load_bytes/program_bss_bytes` 必须与实际 SCX 相等。代码、初始化数据、元数据与 BSS 总量不超过 `0x3d0000`；旧加载器的初始化载荷上限 262108B、4KiB..128KiB 栈合同保持。

原生 SCCC 不重排应用 DATA，不将程序的内部引用登记成运行期导入。内部绝对地址先按最终 `load_size` 回填：BSS 从包含全部 SCD 的初始化载荷末开始，不能沿用插入元数据之前的地址。内部 PC32 仍直接连接本程序函数。宿主链接器保留 ELF 节的对齐，以字符串尾部 NUL 填充使最终 BSS 基址满足所需对齐；填充计入 `strings_size`，没有表外空洞。

## 3. 64B SCD1 头

全部整数为小端 u32，重定位 addend 按有符号 32 位补码解释。磁盘字节逐字节读取，禁止强转宿主结构体。

| 偏移 | 字段 | 约束 |
|---:|---|---|
| 0 | magic，8B | `SCD1MIO` 加 NUL |
| 8 | format_version | 1 |
| 12 | header_size | 64 |
| 16 | metadata_size | 精确描述符字节数 |
| 20 | program_load_bytes | 完整初始化载荷含 SCD |
| 24 | program_bss_bytes | SCX BSS 字节数 |
| 28 | main_rva | `64 <= main_rva < metadata_rva` |
| 32 / 36 | dependencies / dependency_count | 描述符内偏移/项数 |
| 40 / 44 | imports / import_count | 描述符内偏移/项数 |
| 48 / 52 | relocs / reloc_count | 描述符内偏移/项数 |
| 56 / 60 | strings / strings_size | 描述符内偏移/字节数 |

规范化连续布局为 **64B头 → 20B依赖表 → 16B导入表 → 16B重定位表 → 字符串区**。空表也填写当前游标。每次乘加都先以剩余范围/记录宽度核对；字符串区非空、精确占据元数据末尾且最后一字节为 NUL。名称偏移相对字符串区，必须为 NUL 字符串起点，不能复用字符串后缀。

依赖记录：`name_offset, abi_major, min_minor, min_version, flags`；主 ABI 非零、flags=0，名称按字典序严格排序。导入记录：`name_offset, dependency_index, kind, minimum_size`；kind 函数=1/数据=2，最低尺寸非零，依赖下标有效，按 `(dependency_index,name)` 严格排序。库名和符号名的字节规则完全采用 SCL v1；应用名不作为库依赖身份。

## 4. 动态重定位

每条记录16B：`patch_rva, type, import_index, addend_s32`。仅记录真正的外部导入，type 为 ABS32=1 或 PC32=2；import_index 指向最终规范化导入表。

补丁覆盖完整4B，必须满足 `64 <= patch_rva <= metadata_rva - 4`；不能覆盖正在执行的 CRT 或 SCD 本身。记录按补丁递增，覆盖范围不得重叠。生成器将外部补丁四字节清零，真实加数保存在表内。

令 `S` 为指定依赖中解析到的导出地址，`A` 为有符号加数，`P=0x400000+patch_rva`：

```
ABS32 = (S + A) mod 2^32
PC32  = (S + A - P) mod 2^32
```

i386 call 的 REL 加数通常包含 -4。SCCC 的旧 F_REL 保存的是另一种内部约定，因此转动态项时减4一次；宿主 ELF REL 的加数原样提取，运行时不得再次减4。导入必须同名、同kind、导出尺寸不少于最低尺寸，ABI 主版本相等且次/发布版本不低于要求；初始化前完成全部解析和回填。

## 5. 自动 CRT 与根引导

CRT 在栈上保留32B bootinfo，将 EBX 指向该本人可写缓冲、ECX 指向 SCD，然后私有调用 `EAX=0x283, int 0x7c`。这是编译器生成的引导桥，不是要求应用作者处理的公开 syscall API。内核只从固定受保护系统路径取得无依赖 SCLDR 根映像、校验/映射/只读保护并返回；它不执行库函数或代替 Ring3 解析应用依赖。完整系统调用与权限合同见 [SYSCALL.md](SYSCALL.md)。

bootinfo 为8个 u32，依次为：`32, 1, root_base, entry_va, metadata_va, metadata_size, allocation_bytes, image_size`。所有地址均为本人用户虚址，不返回内核指针。启动成功后 CRT 以 cdecl 调用根导出的 `ScApplicationStart(descriptor, const ScDynamicBootInfo *)`；后者在三环加载依赖、调用 main、逆序清理。返回码交原 SCX EXIT；根引导失败直接 EXIT 1。

正式私有 CRT 固定64B，descriptor VA 两个立即数字段位于 RVA6（ECX）和 RVA32（cdecl参数），失败分支位于RVA49。初始ESP是`USER_TOP-16`，保持16B对齐；CRT先留32B信息，在保存信息指针后另留8B垫片，压两参数时CALL前ESP仍16B对齐，返回一次加48B完整恢复初始栈。失败分支只恢复最初32B。这个布局供工具检查，不要求内核扫描机器码；SYS0x283直接校验ECX给出的描述符及本人实际映像。普通源码引用、main入口与动态补丁都不得落入CRT。

## 6. 两条真实工具链

原生路径由 SCCC 自己的词法、预处理、类型、语法和 x86 后端生成机器码，再直接生成 SCD/SCX，没有调用宿主链接器。共同 `scd_validate` 仅进入编译器自身和运行时，不静态进入生成的应用。

宿主 `mkdscx.py` 是 **HOST_BOOTSTRAP**：输入为 mkscl 严格子集中的单份 ELF32 i386 ET_REL，可以先用 `ld -m elf_i386 -r` 合并多对象。它真实解析 R_386_32/PC32，链接本程序内部定义并生成同一私有 CRT；实际 undefined 根据严格应用清单留为运行期导入。它不读取依赖 SCL 文件、不链接 libc、旧 start.asm 或 SCLDR 正文。ELF 弱符号、COMMON、GOT/PLT/TLS、异常运行时和其它不支持的节/重定位明确拒绝，子集详见 [SCL.md](SCL.md)§7。

```
gcc -x c -m32 -O2 -ffreestanding -fno-builtin -fno-pie -fno-pic -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -msoft-float -mno-sse -mno-mmx -c tests/m10a2/DYNAPP.C -o build/DYNAPP.o
python3 tools/mkdscx.py build/DYNAPP.o build/DYNAPP.SCX --manifest tests/m10a2/DYNAPP.json --json
python3 tests/m10a2/test_scd_format.py
```

宿主正文在完整构建/验证后用同目录临时文件替换，坏 ELF/清单不截断已有输出。`--json` 显示真实导入、依赖、重定位与尺寸；`scd_format.validate_scx` 可独立检查封装，`relocated_image` 仅产生地址对照字节，不执行 i386 指令。

`tests/m10a2/DYNAPP.C/JSON` 是无 SCAPI、无显式装载器的交接输入，预期 main 返回0：检查两库调用、函数/数据/字符串导入、初始化一次、内部/外部指针、元数据后BSS地址及真实读写。宿主格式/原生后端结果与客体编译/运行分开记录；没有实际客体启动证据不能称自动启动已验收，更不能以本切片关闭整个 M10a2。

## 7. 本轮定向宿主证据

`build/m10a2-dynamic-host/combined-report-04-crt.json` 明确 `executes_i386=false, guest_sccc=false`，CRT栈对齐修复前记录保留在 `combined-report.json`。真实原生后端通过CLI/GUI的实际main与ROOTLESS适配生成同一DYNAPP，正文包括8条外部动态重定位；C/Python共同校验、导入排序重映射、内部BSS/RO和外部ABS/PC地址对照、代表性非法表/main/CRT/metadata范围拒绝通过。map写入失败、坏清单、日志回退输入别名均保留正文或源。代表旧SCX/SKM与 `d43bdd10ee859a72e120eed6eebec6a4f95d0cef` 源输出全字节相同，九字段BASE/UPPER仍由真实SCCC后端生成并通过共同SCL校验。

| 实际宿主产物 | 长度 | SHA256 |
|---|---:|---|
| SCCC真实后端DYNAPP（对齐修复后） | 1416B | `2e70b1d0944949b6cb3a5c501c37135e98570aa3140fe4a2151d447aad2cf580` |
| 自写REL链接器DYNAPP（对齐修复后） | 912B | `456fe194bc797909b20b4b737f1f390bd8e9b5f1da46f4361fc47e5a60a4ed0c` |
| 对齐修复前自身源码生成SCX（历史容量证据） | 207894B | `28e5455d7bb992b75ec27eb13e3ab0cc8f3171efd420cdf172c9981ad4ee10cb` |

对齐修复前的自身输入实测 `source=235897B, tokens=52820, code=194554B, data=13302B, load=207858B, BSS=3538848B, fixes=4210, macros=262`。旧256项宏表真实耗尽后调整320，只补跑这一容量检查；其余已通过夹具不新增宏，前后端/格式/REL代码未变，报告保留原输入摘要并明确复用依据。首次GCC因大写`.C`误判C++的失败也保留，正确命令显式 `-x c`。

日志为 `build/m10a2-dynamic-host-tests.log`（首次输入语言失败）、`-tests-02.log`（前五项通过/自身宏容量失败）、`-tests-03-self.log`（容量修复后自身通过）。旧失败不改称PASS，宿主生成的机器码未在这些检查中执行；实际客体SCCC和动态应用启动/退出页回收仍由主开发统一验收。

随后发现旧CRT压两参数后CALL前ESP为8模16，违反GCC的cdecl栈对齐要求。修复保持64B与旧SCX不变，仅插8B垫片并在成功返回后恢复48B；依据实际 `paging.h USER_TOP=0x800000` 和 `task.c task_spawn(...,USER_TOP-16)`核对初始0x7ffff0为16B对齐。只补跑 `test_01_real_host_and_native_dynamic`，HOST/CLI/GUI重新生成、共同格式验证和CRT两立即数/失败分支/调用前对齐/返回栈恢复通过，日志 `build/m10a2-dynamic-host-tests-04-crt.log`。其它未受影响的格式/回填/失败保留/旧格式证据继续复用；修复前的DYNAPP字节保存在两个 `DYNAPP-*-before-crt-fix.SCX` 小文件，没有将旧摘要冒充新CRT。

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-10-14 | 固定SCD1尾部格式、独立应用JSON、ECX描述符/32B根引导、64B自动CRT、原生与宿主真实链接路径；客体执行由主开发统一留证 |
| v1.1 | 2026-10-14 | 64B CRT增8B参数垫片保证CALL前16B对齐、成功返回恢复48B、参数立即数移至32；只定向刷新生成/共同检查，记录实际初始栈与新摘要 |
