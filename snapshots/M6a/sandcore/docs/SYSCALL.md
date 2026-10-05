# SandCore 系统调用 API v0.2（M6a）

> **作者: mio** ｜ 沙核 OS 应用编程接口。用户程序 (ring3) 经软中断
> `int 0x7C` 调用内核服务（向量 0x7C 是沙核自己的选择，刻意区别于 Linux 的 0x80）。
> **本文是 ABI 权威**：寄存器约定以 `kernel/task.c` 的 `syscall_c_common()` 为准逐条核对过。

## 1. 调用约定

```
EAX = 功能号
参数寄存器（按各调用定义）: EBX / ECX / EDX / ESI / EDI / EBP
返回值放 EAX（成功多为 0 或正数/句柄；负数或 0xFFFFFFFF = 失败）
除 EAX 外寄存器不保证保留
```

- 系统调用门：IDT 向量 0x7C，attr = 0xEE（P=1, **DPL=3** —— 用户态可触发），32 位中断门
- 触发即陷入 ring0，CPU 自动切到当前任务内核栈（TSS.esp0，调度器每次切换时更新）
- 用户指针（字符串/缓冲）**必须落在本任务用户区 0x400000-0x7FFFFF**；
  内核经 `uptr()`（任务页目录首帧基址 + 偏移）翻译访问 —— 越区指针后果未定义（v1 信任模型，M7 收紧）
- 颜色参数一律是**调色板槽位号**（见 `kernel/palette.h` / docs/GFX.md §2），不是 RGB

## 2. 功能号全表（v0.2 现状）

### 进程（0x00-0x02）

| 号 | 名称 | 入参 | 返回 | 说明 |
|---|---|---|---|---|
| 0x00 | SYS_EXIT | — | 不返回 | 结束当前任务（置僵尸，等调度器让位） |
| 0x01 | SYS_PUTS | EBX=字符串(ASCIZ) | 0 | 向终端输出。**【已知问题】**当前写 term 字符网格而 WM v2 不再渲染它（内核 shell 已退役）——改造方案见 HANDOFF.md §3.6：改为向调用者窗口追加文本 |
| 0x02 | SYS_GETTICK | — | EAX=PIT tick | 开机心跳数（100Hz） |

### 窗口（0x10-0x15）★ M6a 新增

| 号 | 名称 | 入参 | 返回 | 说明 |
|---|---|---|---|---|
| 0x10 | SYS_WINOPEN | EBX=标题 ECX=宽 EDX=高 | EAX=窗口句柄(≥0)，负=失败 | 开窗口（自动居中；上限 6 个，画布上限 304×150） |
| 0x11 | SYS_WINCLOSE | EBX=句柄 | 0 | 关闭本人窗口（校验 owner） |
| 0x12 | SYS_TXT | EBX=句柄 ECX=x EDX=y ESI=串 EDI=颜色 | 0 | 在窗口客户区 (x,y) 画 8×8 文本。**【已知问题】**当前画进后台缓冲，会被合成时的画布 blit 覆盖——需改为写 `win->canvas`（HANDOFF.md §3.5） |
| 0x13 | SYS_FILL | EBX=句柄 ECX=x EDX=y ESI=宽 EDI=高 EBP=颜色 | 0 | 客户区实心矩形（同上，需改写画布） |
| 0x14 | SYS_GETKEY | — | EAX=ASCII 或 -1 | 取聚焦窗口按键（16 深度队列；无键返回 -1，**不阻塞**） |
| 0x15 | SYS_MOUSE | — | EAX = x \| y<<9 \| 按键<<18 | 鼠标状态打包（x,y 各 9 位，按键 bit0 左/bit1 右/bit2 中） |

### 文件系统（0x30-0x32）★ M6a 新增

| 号 | 名称 | 入参 | 返回 | 说明 |
|---|---|---|---|---|
| 0x30 | SYS_FSREAD | EBX=路径 ECX=缓冲 EDX=上限 | 字节数，-1=没有 | 读整文件进用户缓冲 |
| 0x31 | SYS_FSWRITE | EBX=路径 ECX=数据 EDX=长度 | 写入字节数，-1=失败 | 写/新建文件（原地放得下则重写，否则追加数据末端；目录表自动写回） |
| 0x32 | SYS_FSLIST | EBX=缓冲 EDX=上限 | 生成字节数 | 列目录：每行 `路径 大小\n`，整体 NUL 结尾 |

### 进程启动（0x40）★ M6a 新增

| 号 | 名称 | 入参 | 返回 | 说明 |
|---|---|---|---|---|
| 0x40 | SYS_EXEC | EBX=SCX 路径 | EAX=pid(≥0)；-1 没文件 / -2 魔数错 / -3 尺寸非法 / -4 内存不足 | 读 SCX → 建独立页目录 → 建 ring3 任务（**并发运行**，分页后同程序可同时跑多份） |

## 3. 规划中的功能号（落地时登记进 §2）

```
0x03  SYS_PUTDEC      打印十进制数
0x16  SYS_EVENT       统一事件队列（窗口暴露/关闭/重绘请求）
0x17  SYS_ICON        从 SCX 内嵌图标字段读图标（M6 规划：图标信息写进可执行文件）
0x18  SYS_WALLPAPER   设置壁纸（M6 桌面打磨）
0x19  SYS_WINMOVE     移动/改尺寸窗口（配合最小化，M6b）
0x0100+ 内存：申请/释放用户页
0x0500+ 汇编器/编译器服务（M6b/M7：SCCC 依赖）
```

> 【窗口 API 说明】（mio 交代）内核侧窗口接口（wm_open_user_window 等）是 ring0
> 内部函数；对用户程序的一切能力都经本文件的 0x1x 号段暴露，新增能力先在此登记规格。

## 4. 用户程序的样子（user/hello.asm 节选，平二进制，零依赖）

```nasm
[bits 32]
[org 0x400000]              ; 所有程序统一装载基址（分页后各任务映射到私有物理帧）
_start:
    mov eax, 0x10           ; SYS_WINOPEN
    mov ebx, wtitle
    mov ecx, 220            ; 宽
    mov edx, 110            ; 高
    int 0x7C
    mov [win], eax          ; 句柄存好, 后续调用都要带
    ...
    mov eax, 0x00           ; SYS_EXIT
    int 0x7C
```

构建链：`nasm -f bin app.asm -o app.bin` → `python tools\mkscx.py app.bin app.scx`
→ 放进 `build/fs/` 对应目录 → `make` 自动上盘 → 系统里点图标或 `run 路径`。

## 5. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-02 | 初版：调用约定、EXIT/PUTS/GETTICK、规划表、作者 mio |
| v0.2 | 2026-10-02 | M6a：窗口 0x10-0x15 / 文件 0x30-0x32 / EXEC 0x40 全表（寄存器与 task.c 分发器逐条核对）；标注 SYS_PUTS 与 SYS_TXT 的两个已知设计问题（改造方案见根目录 HANDOFF.md §3） |
