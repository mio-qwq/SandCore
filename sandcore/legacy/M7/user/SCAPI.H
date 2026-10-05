#ifndef SANDCORE_SCAPI_H
#define SANDCORE_SCAPI_H

/* mio：SCAPI.H 是唯一公共应用头文件，文件名必须完全大写。
 * 系统调用桥直接内联 int 0x7C；不要求另链 libc 或用户态 API 库。
 * 调色板定义与 kernel/palette.h 一致，槽位的权威分配见 docs/GFX.md。
 * 私有缓冲归应用，内核按页校验且所有窗口调用都检查 owner。
 */
#ifndef SANDCORE_PALETTE_H
#define SANDCORE_PALETTE_H

/* =====================================================================
 *  SandCore 调色板槽位分配 —— 唯一权威定义
 *  ---------------------------------------------------------------------
 *  与 docs/GFX.md 第 2 节的分配表一一对应。
 *  【纪律】新颜色必须: 先在表里认领槽位 -> 这里加 define -> 才能在代码用。
 *  渐变段 (1-15, 16-31) 只保证"段内索引越大越深/越暖"的相对语义,
 *  具体档位含义由绘制代码解释。
 * ===================================================================== */

#define PAL_SKY      1    /* 1..15  天空垂直渐变: 深靛 -> 暖橙 */
#define PAL_SAND_L   16   /* 16..23 后排沙丘: 亮 -> 暗 */
#define PAL_SAND_D   24   /* 24..31 前排沙丘: 暗 -> 更暗 */
#define PAL_SUN_CORE 32   /* 日核 (金) */
#define PAL_SUN_EDGE 33   /* 日缘 (橙) */
#define PAL_STAR     34   /* 星 (冷白) */
#define PAL_TITLE    35   /* 标题/正文 (米白) */
#define PAL_SHADOW   36   /* 标题投影 (近黑) */
#define PAL_CON_BG   37   /* 控制台底色 (深靛灰) */
#define PAL_CON_TINT 38   /* 控制台强调 (青) */
#define PAL_CON_WARN 39   /* 控制台警示 (琥珀) */
#define PAL_PANIC    40   /* 异常红屏 */
#define PAL_WIN_TITLE 41  /* 窗口标题栏 (未聚焦灰) */
#define PAL_TASKBAR  42   /* 任务栏底色 (深灰蓝) */

/* M7：每个八槽渐变越往后越亮，同材质面的法向光照选择档位。
 * 任何应用只提交索引，DAC RGB 在内核集中初始化，避免各窗口改色互扰。 */
#define PAL_UI_NIGHT 43
#define PAL_UI_CYAN  51
#define PAL_UI_GOLD  59
#define PAL_ROCK     67
#define PAL_GRASS    75
#define PAL_EARTH    83
#define PAL_WATER    91
#define PAL_UI_INK   99
#define PAL_UI_TEXT  100
#define PAL_UI_MUTED 101
#define PAL_UI_ALERT 102
#define PAL_UI_PANEL 103
#define PAL_UI_LINE  104

#endif /* SANDCORE_PALETTE_H */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
/* 普通 C 采用 cdecl，参数先在栈上；下面的内联汇编把 nr/a..f 搬到
 * EAX/EBX/ECX/EDX/ESI/EDI/EBP，再 int 0x7C。内核 ABI 不承诺保存
 * 通用寄存器，桥必须替 C 保存 EBX/ESI/EDI/EBP，否则优化后的调用者
 * 局部状态会被系统调用破坏。这里 int 与指针都按 32 位 x86 解释。
 * 具体功能号/寄存器/错误值的权威文档为 docs/SYSCALL.md。 */
static inline int sc_call(int nr,int a,int b,int c,int d,int e,int f)
{
    /* 七个参数先排成数组，ECX 临时保存数组指针；最后才覆盖 ECX 本身。
     * EBP 可被宿主编译器用作帧指针，不能声明成普通固定输出寄存器。
     * 这里显式保存/恢复四个 cdecl 保留寄存器，保证优化和无优化均可用。
     * memory/cc 告诉编译器陷入可能读写缓冲并改变标志，不能跨调用缓存。
     */
    int args[7]={nr,a,b,c,d,e,f}; int result; int *p=args;
    __asm__ __volatile__(
        "push %%ebp; push %%ebx; push %%esi; push %%edi; "
        "mov 16(%%ecx),%%esi; mov 20(%%ecx),%%edi; mov 24(%%ecx),%%ebp; "
        "mov 4(%%ecx),%%ebx; mov 12(%%ecx),%%edx; mov (%%ecx),%%eax; "
        "mov 8(%%ecx),%%ecx; int $0x7c; "
        "pop %%edi; pop %%esi; pop %%ebx; pop %%ebp"
        : "=a"(result), "+c"(p) : : "edx","cc","memory");
    return result;
}

/* w/h 是外框尺寸（含 1px 边框与 12px 标题栏），内核限制到
 * 48×32..320×184。成功句柄稳定，不是 z 序数组下标；失败为负。
 * 新窗口归当前 pid 并聚焦；退出 main 后入口桥会 EXIT 回收本人窗口。 */
static inline int sc_open(const char *title,int w,int h)
{ return sc_call(0x10,(int)title,w,h,0,0,0); }
/* GETKEY 不阻塞，且只有本人拥有聚焦窗口才拿得到队列里的键；因此
 * 必须用 int 保存返回值，char 会把 0x80..0x83 方向键误当负数。
 * -1 表示没有键；F1..F7=1..7；F8/F9/F10=0x88/0x89/0x8A；Backspace=8、Tab=9、Enter=10，不能混用。 Esc=27。
 * 忙轮询会继续被 PIT 抢占，不需在 ring3 执行非法的 hlt 指令。 */
static inline int sc_key(void) { return sc_call(0x14,0,0,0,0,0,0); }
static inline int sc_tick(void) { return sc_call(2,0,0,0,0,0,0); }
/* MOUSE 打包 x[0..8]、y[9..17]、按钮[18..20]；只提供全局快照，
 * 必须用 WININFO 的 focused 检查响应资格。info 至少有五个 i32：
 * 外框 x/y、客户区宽/高、焦点。客户区坐标=屏幕坐标-(x+1,y+13)。 */
static inline int sc_mouse(void) { return sc_call(0x15,0,0,0,0,0,0); }
static inline int sc_info(int win,int *info) { return sc_call(0x19,win,(int)info,0,0,0,0); }
/* 模式 0/1/2 对应沙丘/夜色/暖沙；0=保存成功，-1=失败。此调用由
 * 内核统一保存 sys/wall.cfg，应用不直接改内核壁纸变量或 VGA 状态。 */
static inline int sc_wallpaper(int mode) { return sc_call(0x18,mode,0,0,0,0,0); }
/* EXEC 异步启动，字符串可为“路径 参数”。路径≤63B、参数≤127B；
 * 内核复制到新任务，调用返回后本地 command 可以再次使用。返回 pid
 * 不是文件句柄，也没有等待子程序结束的含义。GETARGS 读的是本任务
 * 副本，最多 max-1 字节并 NUL 终止；返回正文复制长度或 -1。 */
static inline int sc_exec(const char *path) { return sc_call(0x40,(int)path,0,0,0,0,0); }
static inline int sc_args(char *buf,int max) { return sc_call(5,(int)buf,0,max,0,0,0); }
/* READ/WRITE 以字节计，文件无打开/关闭对象；READ 可按 max 截断，
 * 不自动补字符串 NUL，文本调用者应留一字节自行终止。WRITE 只写 n，
 * 返回值须与 n 相等才算完整成功。LIST 与之不同，会输出完整行
 * “路径 大小\n”并保留末尾 NUL，容量不足时停止，不输出半个路径。 */
static inline int sc_read(const char *name,void *buf,int max)
{ return sc_call(0x30,(int)name,(int)buf,max,0,0,0); }
static inline int sc_write(const char *name,const void *buf,int n)
{ return sc_call(0x31,(int)name,(int)buf,n,0,0,0); }
static inline int sc_list(char *buf,int max)
{ return sc_call(0x32,(int)buf,0,max,0,0,0); }
/* M7：目录列表输出 D/F 名字 大小 LF，只含直接子项；斜杠推导目录。
 * STAT 输出两个 u32（1 文件/2 目录、字节数）。核心保护失败返回 -5，
 * 即使应用手工调用 syscall，规范化路径后仍不能改 SYS/CORE 或其祖先。
 */
static inline int sc_dir(const char *path,char *buf,int max)
{ return sc_call(0x33,(int)path,(int)buf,max,0,0,0); }
static inline int sc_mkdir(const char *path) { return sc_call(0x34,(int)path,0,0,0,0,0); }
static inline int sc_remove(const char *path) { return sc_call(0x35,(int)path,0,0,0,0,0); }
static inline int sc_rename(const char *oldname,const char *newname)
{ return sc_call(0x36,(int)oldname,(int)newname,0,0,0,0); }
static inline int sc_stat(const char *path,u32 *info) { return sc_call(0x37,(int)path,(int)info,0,0,0,0); }
/* 绘图全部使用客户区坐标与 palette.h 槽位。内核按 owner 找独占
 * canvas 并裁剪，合成时才上屏，应用不触碰显存或合成后台缓冲。
 * 不存在/非本人句柄为静默无操作；TXT 字串仍会先检查用户映射。
 * PUTS 则向本人最近创建窗口按游标输出，适合 CLI，不适合精确排版。 */
static inline void sc_fill(int win,int x,int y,int w,int h,int color)
{ sc_call(0x13,win,x,y,w,h,color); }
static inline void sc_text(int win,int x,int y,const char *s,int color)
{ sc_call(0x12,win,x,y,(int)s,color,0); }
static inline void sc_puts(const char *s) { sc_call(1,(int)s,0,0,0,0,0); }

/* M7：状态高位表示仍在运行/暂停，普通整数是已经退出的返回码。
 * 显式让出不阻塞线程，游戏用 tick 控制帧率时可避免空转抢占其他应用。
 * FRAME 的 n 必须等于客户区 cw*ch，整帧提交后才进入合成，避免撕裂。
 */
static inline int sc_status(int pid) { return sc_call(6,pid,0,0,0,0,0); }
static inline void sc_yield(void) { sc_call(7,0,0,0,0,0,0); }
static inline void sc_exit(int code) { sc_call(0,code,0,0,0,0,0); }
static inline int sc_frame(int win,const void *pixels,int n) { return sc_call(0x16,win,(int)pixels,n,0,0,0); }
static inline int sc_down(int key) { return sc_call(0x17,key,0,0,0,0,0); }
static inline int sc_font8(void *buffer) { return sc_call(0x1A,(int)buffer,0,0,0,0,0); }
/* 原生凤凰字形：Unicode 标量而非 UTF-8 的三个字节拼成的整数。
 * rows 至少 32B（16 个 u16，bit15 为左边）。已收字返回宽 8/16，
 * 未收字返回 0 且给出 16px 占位框；-1 是非法码点或输出映射。
 * 自定义游戏把 rows 画进自己的帧缓冲后再 FRAME，不会被后续整帧
 * 覆盖。普通窗口直接用 sc_text16，省去私有字形缓存/位图循环。
 */
static inline int sc_glyph16(u32 scalar,u16 *rows)
{ return sc_call(0x1B,(int)scalar,(int)rows,0,0,0,0); }
/* UTF-8、透明底、ASCII 宽 8/汉字宽 16，LF 行距 18、CR 回行首。
 * 返回实际非换行字符数；-1 无权/非法地址/坐标，-2 非法 UTF-8。
 * 出错整串不绘制；缺字框属于正常绘制。最大正文 4095 字节。
 */
static inline int sc_text16(int win,int x,int y,const char *utf8,int color)
{ return sc_call(0x1C,win,x,y,(int)utf8,color,0); }
/* info 为四个 u32：接口版本 1、汉字条目数、原生高度 16、磁盘
 * 激活标记（1 磁盘 SCF，0 同源编译兜底）。不承诺任意汉字都收录。
 */
static inline int sc_fontinfo(u32 *info) { return sc_call(0x1D,(int)info,0,0,0,0,0); }
/* context 为 19 个双字；EIP=14、EFLAGS=16、ESP=17、向量=12。
 * 返回接口只接受内核交给本次回调的现场地址；不能恢复内核选择子/IOPL。
 */
static inline int sc_handler(int vector,void (*entry)(u32 *)) { return sc_call(9,vector,(int)entry,0,0,0,0); }
static inline int sc_resume(u32 *context) { return sc_call(8,(int)context,0,0,0,0,0); }
/* 调试启动在第一条用户指令之前暂停；只有创建它的调用者可操作。
 * cmd：0 现场(88B)、1 单步、2 继续、3 内存读(<=256B)、4 断点、
 * 5 删除断点、6 杀掉目标、7 暂停。断点是 CPU 真正执行的 INT3，不是 UI 标记。
 */
static inline int sc_dbgexec(const char *cmd) { return sc_call(0x50,(int)cmd,0,0,0,0,0); }
static inline int sc_debug(int pid,int command,u32 address,void *buffer,int n)
{ return sc_call(0x51,pid,command,(int)address,(int)buffer,n,0); }

/* 缓冲区容量由调用者显式提供，复制/拼接始终留出 NUL，避免把文件路径
 * 或 EXEC 参数写进相邻的应用状态。length 只对本程序已终止的字符串用。 */
static inline int length(const char *s) { int n=0; while(s[n]) n++; return n; }
static inline int equal(const char *a,const char *b)
{ while(*a && *a==*b) { a++; b++; } return *a==*b; }
static inline void copy(char *d,const char *s,int max)
{ int i=0; while(i+1<max && s[i]) { d[i]=s[i]; i++; } if(max) d[i]=0; }
static inline void append(char *d,const char *s,int max)
{ int n=length(d); if(n<max) copy(d+n,s,max-n); }
static inline char *token(char **p)
{
    /* 参数串归本应用所有，允许就地把空格改 NUL；返回指针一直有效到
     * 下一次 GETARGS 覆盖缓冲。内核不解析具体应用参数，分词留在 ring3。 */
    while(**p==' ') (*p)++;
    char *start=*p;
    while(**p && **p!=' ') (*p)++;
    if(**p) *(*p)++=0;
    return start;
}
static inline void decimal(char *out,int value)
{
    /* 用无符号幅值处理 INT_MIN，避免对最小负数取负的有符号溢出。 */
    u32 v=value<0 ? 0u-(u32)value : (u32)value;
    char rev[11]; int n=0,i=0;
    do { rev[n++]=(char)('0'+v%10); v/=10; } while(v);
    if(value<0) out[i++]='-';
    while(n) out[i++]=rev[--n];
    out[i]=0;
}

/* 所有小应用使用相同的边距、强调线和页脚。画布背景先填满，正文
 * 从 y=34 起，页脚保留 y=124；避免每个程序临时拼出不同的界面语言。 */
static inline void page(int win,const char *name,const char *footer)
{
    sc_fill(win,0,0,304,150,PAL_CON_BG);
    sc_fill(win,8,8,3,14,PAL_CON_TINT);
    sc_text(win,18,10,name,PAL_TITLE);
    sc_fill(win,8,28,270,1,PAL_WIN_TITLE);
    sc_text(win,8,128,footer,PAL_CON_TINT);
}
static inline void wait_escape(void)
{
    /* GETKEY 非阻塞；未聚焦时返回 -1，后台应用仍会被 PIT 正常抢占。
     * Esc 才正常返回 main；用户点击红叉则由内核 owner 路径直接结束任务。 */
    while(sc_key()!=27) { }
}
#endif
