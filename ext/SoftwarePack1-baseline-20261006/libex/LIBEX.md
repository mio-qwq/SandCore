# libex —— ext 扩展生态共享库规范

> 版本 1.0（2026-10-06，SandCore_ExtraSoftware_Pack_1 同批冻结）。
> 本目录是**跨软件包复用层**：SoftwarePack2 及后续扩展包直接复用，
> 复用时整体拷贝、不改语义；发现缺陷在本文件登记后修复，两个包
> 同步升版本。所有代码为原创实现，不引入第三方源码。

## 1. 目录与构建

| 文件 | 职责 |
|---|---|
| `SCAPI.H` | 平台唯一公共头（拷贝自主项目，构建期引用，**不修改**） |
| `NUI.inc` / `SCMEM.inc` / `SCMEM.H` | 平台原生界面片段与批处理内存原语（拷贝自主项目，**不修改**） |
| `exutil.c/.h` | 表达式引擎 / CSV / 配置 / UTF-8 / 整数数学 / 格式化 |
| `exaudio.c/.h` | 音频声道包装 + 程序化音效合成 |
| `exui_ext.inc` | NUI 之上的追加控件（复选框/进度条/列表行/图标预览/测宽） |
| `start.asm` | 用户态入口桥（`_start` -> `main` -> SYS_EXIT） |
| `linker.ld` | SCX 链接脚本（基址 0x400000，BSS NOLOAD） |

构建约定：
- freestanding：`-m32 -ffreestanding -nostdlib -fno-builtin`，无 libc/libm/libgcc。
- **禁 64 位除法/取模与 double<->i64 转换**：i386 上会生成
  `__udivdi3/__divdi3/__fixdfdi` 等 libgcc 辅助符号，`-nostdlib`
  下链接直接失败。需要大数用"分段 u32"实现（参考 exutil 的
  `ex_cfg_build`、sheet 的 `sh_fmt` 百万段拆分）。
- Windows MinGW：GCC 会从 `main` 发出 `call __main`（CRT 桩），
  start.asm 的 WIN_COFF 分支提供空桩；`-fno-leading-underscore`
  对 MinGW 有效（C 符号无前缀）。
- WSL/Linux：ELF + `-m elf_i386`，objcopy 无需 -j。
- 目标机为完整 32 位 x86：**允许浮点**（内核保存/恢复 FPU 上下文），
  允许全部整数指令；不使用 SSE 特权态特性。

## 2. exutil 合同

### 2.1 表达式引擎 `ex_eval`

```c
int ex_eval(const char *text, ex_var_fn vars, ex_call_fn calls,
            int *out, int *err_pos);
```

- 32 位整数语义：加减乘按补码回绕（程序员语义，是特性）；除/余
  的除零与 `INT_MIN/-1`、移位计数越界**置错**（x86 会抛 #DE）。
- 运算符：`?: || && | ^ & == != < <= > >= << >> + - * / %` 一元
  `! ~ - +`；字面量 `123` / `0x1F` / `0b1010` / `0o17` / `'c'`。
- `|| && ?:` 短路：未选分支**连错误都不产生**（`x!=0 && 100/x`
  保护写法可用）。
- 变量回调返回 0 = 未知变量 = 错误；未知函数同理。绝不静默当 0。
- 函数实参至多 8 个；超限参数照常消费后报错（保证错误定位准确）。
- 失败返回 -1，`err_pos` 为第一处错误的字节偏移（可用于画定位符）。

### 2.2 其余 API

- `ex_parse_int`：可选符号 + `0x/0b/0o` 前缀；溢出/尾随垃圾拒绝。
- `ex_dec/ex_udec/ex_hex/ex_bin`：先在局部缓冲生成完整数字再按
  容量复制，不输出半截。
- `ex_utf8_next`：合法 1..4 字节标量；坏序列前进 1 字节给
  0xFFFD——调用方按返回步长移动永不死循环。
- `ex_cfg_get/ex_cfg_build`：`key=value`、`#` 注释；build 容量不足
  整体失败（调用方可选择不写坏文件）。
- `ex_csv_split/ex_csv_join`：RFC4180 子集（引号包裹、`""` 转义）；
  split 后字段共享行缓冲且行内容可能已折叠（引号剥离），使用期内
  不得释放行。超限字段并入最后一个（不丢内容也不假装没超限）。
- `ex_dir_parse`：解析 SC_DIR 的 `D/F 名字 大小\n` 行协议；任何
  一行不合法整体失败。
- `ex_sqrt_u32`：逐位试商，无溢出中间量；`ex_gcd_u32/ex_lcm_u32`
  先除后乘防回绕；`ex_rand_*` xorshift32（全零种子强制搅动）。

## 3. exaudio 合同

```c
int  exaudio_init(void);            /* 0=有声卡；-1=无声卡（全部空转） */
u32  exaudio_stream(const s16 *samples, u32 frames);
void exaudio_pump(void);            /* 游戏循环每帧续喂尾巴 */
int  exaudio_tone(int freq,int ms,int wave,int volume); /* 0方波 1噪声 2三角 */
void exaudio_shutdown(void);
int  exaudio_sysfx(int kind);       /* 内核六种系统音效 0..5 */
```

- 内核合同：48000Hz S16 双声道，普通 UID 最多两路 voice；submit
  非阻塞返回实际接收帧数。
- 尾巴模型：一次没喂完的部分记在 pending，`pump` 续喂——后到的
  音效不会插队截断前者。**samples 在播完前必须保持有效**：
  `exaudio_tone` 在堆上生成并登记回收槽（FIFO 16 个），满足该约定。
- 无声卡时所有入口安全空转，调用方无需特判。
- 包络：10ms 线性起音 + 每 ms ×27/32 指数衰减 + 2ms 收尾，全整数。

## 4. exui_ext 合同（NUI 附加控件）

前置：先 `#include "NUI.inc"` 再 include 本片段（同一编译单元）。
所有控件写 NUI 的 `ui_action/ui_hover_rects`，与原生控件同一契约：

- `ex_check(id,x,y,label,&value)`：复选框，点击翻转 `*value`。
- `ex_progress(x,y,w,h,per_mille)`：进度条（0..1000）。
- `ex_row(id,x,y,w,h)`：列表行，hover 高亮，左键返回 1。
- `ex_icon_draw(data,size,x,y,scale)` / `ex_icon_header` /
  `ex_icon_write`：SCB2 严格校验（魔数/署名/格式/精确长度/≤128）、
  最近邻放大预览、落盘。**校验失败宁可空一块不画越界**。
- `ex_text_w` / `ex_text_cut`：凤凰字体测宽（ASCII 8/其余 16）与
  按像素预算的安全截断（不切半个 UTF-8 标量）。

## 5. 修订记录

| 日期 | 变更 |
|---|---|
| 2026-10-06 | 随 SandCore_ExtraSoftware_Pack_1 建立本规范；记录表达式引擎/音频/附加控件与构建链（含 libgcc、__main 桩两个平台坑）合同 |
