# SandAsm 原生汇编器规范 v0.2

> 作者 mio。实现 `user/assembler.c`，程序 `bin/asm.scx`，运行于 ring3。
> 宿主 GCC 只用于构建汇编器本身；汇编源文件、生成机器码和写 SCX 均在沙核内完成，不调用 NASM 或宿主脚本。

## 1. 从源文件到程序

在 SandShell 输入：

```text
asm home/demo.asm apps/demo.scx
```

汇编器开自己的结果窗口，读取第一参数的源文件，成功后写入第二参数的 SCX；按 Esc 关闭结果页，再输入：

```text
run apps/demo.scx
```

生成程序创建窗口并显示 `hello from SandAsm / SCX1MIO / mio`；其示例循环取键，关闭用窗口红叉。
源文件由 Notes 编辑：`run apps/note.scx home/demo.asm`，F2 保存；也可另选路径创建源文件。

## 2. 支持的语法（明确的 NASM 子集）

| 语法 | 机器码/含义 |
|---|---|
| `[bits 32]` | 验证 32 位模式 |
| `[org 0x400000]` | 验证用户装载基址 |
| `label:` | 标签，可独立成行或与指令同一行 |
| `mov reg, immediate_or_label` | B8+reg，32 位立即数；标签=base+RVA |
| `xor dst_reg, src_reg` | 31 /r，两个 32 位寄存器 |
| `int number` | CD imm8，含沙核系统调用 0x7C |
| `div reg` | F7 /6；除零会按系统异常政策红屏 |
| `jmp label` | E9 rel32；固定近跳转，不做短跳转优化 |
| `ret` / `nop` | C3 / 90 |
| `db "text", 10, 0` | 字符串与字节数据；支持单双引号 |
| `; comment` | 行尾/整行注释，字符串内的分号仍为数据 |

寄存器：eax/ecx/edx/ebx/esp/ebp/esi/edi；数字：十进制、0x 十六进制、负立即数（补码）。
名字与指令大小写敏感。标签字母/数字/下划线组成，≤31B，最多 64 个。
不支持内存寻址、mov reg/reg、cmp/jcc/call、宏、include、section、重定位或字符串转义。遇到未支持语法必须报错，不静默当成别的指令。

## 3. 两遍工作流与边界

第一遍计算每条编码长度与标签 RVA；前向标签暂用零占位，只影响值，不改变长度。
第二遍查完整符号表，编码绝对立即数或 `目标-(下一指令虚址)` 的相对位移。
源缓冲不原地破坏，两遍都逐行复制到暂存行，LF/CRLF 均可；错误显示 1 基行号与原因。
源文件少于 8192B、单行最多 255B、输出机器码最多 8192B；未知标签、重复标签、数值溢出、字节越界、缺逗号、未闭合引号均失败。
失败不调用 FSWRITE，因此不会创建新输出或覆盖已有输出；成功写头长 36 的 SCX1MIO，署名 MIO，栈建议 8192B。
`_start` 存在时作为 entry RVA，否则 entry=0。仅在载荷末尾定义入口会拒绝写盘；最终入口须落在载荷里，内核加载器再次校验。
超长名字的错误路径同样保证暂存名字 NUL 终止，后续寄存器匹配不会越过 32B 缓冲。

## 4. 验证证据

`tools/verify_apps.py` 经 HMP 输入 asm 命令，读取实际数据盘验证新 SCX 头和长度，再通过 Shell 运行生成程序并截图。
另用 Notes 创建非法指令、32 字符名字、末尾入口三个源文件，分别报错，数据盘均不存在新输出。
截图：`build/m6/04-native-assembler.png`、`05-generated-program.png`、`07b-assembler-error.png`、`07c-assembler-long-symbol.png`、`07d-assembler-invalid-entry.png`。
这是“用沙核生成沙核程序”的第一步；汇编器自身仍由宿主 C 工具链构建，不宣称已完成汇编器自举或 M7 C 自编译。

## 5. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-02 | ring3 两遍汇编、语法/边界/SCX 头、系统内生成与执行以及错误路径截图 |
| v0.2 | 2026-10-02 | 超长符号暂存串终止与载荷末尾入口写盘前拒绝；Notes→Shell 实际错误路径验证，补足两遍/编码/错误传播中文注释 |

### M7 IDE 调用补充

`bin/asm.scx 输入 输出 --ide` 用于图形 IDE：写 输出.log，并以实际
返回码结束（0 成功，2 参数，3 读取，4 汇编，5 写盘）；不等待 Esc。
CLI 不带 --ide 保留结果窗口。main 已明确返回 int，不以未定义的
void main 寄存器值作为 STATUS 结果。

2026-10-02：IDE 异步汇编模式/日志/真实返回码同步，M6 CLI 结果查看保留。


## M8 状态窗口适配（第一阶段，未运行）

正式assembler.c使用SCAPI.H/COMPILEUI.inc原生主题/缩放窗口。
两遍汇编规则、8192B源码/载荷、64符号与SCX头保持；--ide、行号错误、
不输出半成品、系统内读写/生成运行保留。当前行在安全边界更新实际
两遍阶段/进度并处理取消；很小窗口给Enlarge，鼠标Cancel/Close与Esc。
这次UI和输入接线没有运行证据，旧M6/M7语言成功不代表新版窗口通过。

2026-10-03：源码接共享原生编译窗口，保留汇编语言/SCX合同，待第二阶段。
