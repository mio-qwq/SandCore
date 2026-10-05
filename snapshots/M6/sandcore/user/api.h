#ifndef SANDCORE_USER_API_H
#define SANDCORE_USER_API_H

/* =====================================================================
 * mio：M6 用户态组件的最小公共接口，所有能力只经 int 0x7C 获得。
 * 这不是 M7 的 scapi.h：目前借宿主 ELF 工具链编译 C 应用，系统内编译器
 * 与单头文件内联汇编接口仍留给 M7。此处先把 cdecl 桥与应用职责分开。
 * 调色板槽位直接复用权威头文件，禁止应用自行发明 RGB 或裸槽位编号。
 * 所有工具函数自己实现；没有 malloc、stdio、浮点或任何 libc 依赖。
 * ===================================================================== */
#include "../kernel/palette.h"
typedef unsigned char u8;
typedef unsigned int u32;
/* 普通 C 采用 cdecl，参数先在栈上；start.asm 将 nr/a..f 搬到
 * EAX/EBX/ECX/EDX/ESI/EDI/EBP，再 int 0x7C。内核 ABI 不承诺保存
 * 通用寄存器，桥必须替 C 保存 EBX/ESI/EDI/EBP，否则优化后的调用者
 * 局部状态会被系统调用破坏。这里 int 与指针都按 32 位 x86 解释。
 * 具体功能号/寄存器/错误值的权威文档为 docs/SYSCALL.md。 */
int sc_call(int nr,int a,int b,int c,int d,int e,int f);

/* w/h 是外框尺寸（含 1px 边框与 12px 标题栏），内核限制到
 * 48×32..306×164。成功句柄稳定，不是 z 序数组下标；失败为负。
 * 新窗口归当前 pid 并聚焦；退出 main 后入口桥会 EXIT 回收本人窗口。 */
static inline int sc_open(const char *title,int w,int h)
{ return sc_call(0x10,(int)title,w,h,0,0,0); }
/* GETKEY 不阻塞，且只有本人拥有聚焦窗口才拿得到队列里的键；因此
 * 必须用 int 保存返回值，char 会把 0x80..0x83 方向键误当负数。
 * -1 表示没有键；F2/F3/F5=2/3/5，LF=10，BS=8，Esc=27。
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
/* EXEC 异步启动，字符串可为“路径 参数”。路径≤31B、参数≤127B；
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
/* 绘图全部使用客户区坐标与 palette.h 槽位。内核按 owner 找独占
 * canvas 并裁剪，合成时才上屏，应用不触碰显存或合成后台缓冲。
 * 不存在/非本人句柄为静默无操作；TXT 字串仍会先检查用户映射。
 * PUTS 则向本人最近创建窗口按游标输出，适合 CLI，不适合精确排版。 */
static inline void sc_fill(int win,int x,int y,int w,int h,int color)
{ sc_call(0x13,win,x,y,w,h,color); }
static inline void sc_text(int win,int x,int y,const char *s,int color)
{ sc_call(0x12,win,x,y,(int)s,color,0); }
static inline void sc_puts(const char *s) { sc_call(1,(int)s,0,0,0,0,0); }

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
