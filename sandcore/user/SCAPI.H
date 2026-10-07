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
/* 默认白色极光的命名角色，先在GFX.md登记；动态主题接口将允许
 * 查询配置中的同名角色。这里是示例/失败回退的固定默认值。 */
#define SC_RGB_PAPER 0x00FBFCFEu
#define SC_RGB_WHITE 0x00FFFFFFu
#define SC_RGB_INK   0x00173247u
#define SC_RGB_ACCENT 0x00147F9Fu
/* M8主题快照：角色顺序与THEME.md/kernel/theme.h严格一致。颜色是
 * RGB而非DAC槽位；每次代数改变取新快照，旧游戏材质不受主题影响。 */
#define SC_THEME_FACE 0
#define SC_THEME_FACE_ALT 1
#define SC_THEME_PAPER 2
#define SC_THEME_TEXT 3
#define SC_THEME_MUTED 4
#define SC_THEME_LINE 5
#define SC_THEME_ACCENT 6
#define SC_THEME_ACCENT_DEEP 7
#define SC_THEME_SELECT 8
#define SC_THEME_SELECT_TEXT 9
#define SC_THEME_SHADOW 10
#define SC_THEME_ALERT 11
#define SC_THEME_GOLD 12
#define SC_THEME_GREEN 13
#define SC_THEME_VIOLET 14
#define SC_THEME_ROSE 15
#define SC_THEME_HIGHLIGHT 16
#define SC_THEME_DARK_EDGE 17
#define SC_THEME_TITLE_ACTIVE 18
#define SC_THEME_TITLE_INACTIVE 19
#define SC_THEME_TITLE_TEXT 20
#define SC_THEME_TITLE_MUTED 21
#define SC_THEME_DESKTOP_TEXT 22
#define SC_THEME_DESKTOP_SHADOW 23
/* ABI只增不减：这些固定输出大小单独命名，旧48字MONITOR/窗口/
 * 调试现场不扩容。编译旧头文件的应用传旧容量仍必须安全运行。 */
#define SC_CPU_WORDS 64
#define SC_STORAGE_WORDS 32
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
/* M8只观察队首，不消费；事件驱动工具用它决定是否重绘，真正处理
 * 仍调用sc_key。使用新号0x77，旧GETKEY仍忽略全部入参并消费，
 * 不能把旧汇编残留的EBX值重新解释为选项。聚焦/owner检查一致。 */
static inline int sc_key_peek(void){return sc_call(0x77,0,0,0,0,0,0);}
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
/* M8单用户配置查询，不借内核指针。kind=0用户名/1家目录/2当前
 * 目录/3执行路径/4环境变量(name必填)。成功返回完整正文长度；容量
 * 不够=-1，环境变量不存在=-2。cwd/exe含根斜杠，路径解析结果不含。 */
static inline int sc_user(int kind,const char *name,char *out,int capacity)
{return sc_call(0x70,kind,(int)name,(int)out,capacity,0,0);}
/* kind=0 USER.CFG/1 ENV.CFG；action=0仅验证候选/1保存后提交/
 * 2重读固定配置。-1语法/参数、-2读、-3写，错误保持有效快照。 */
static inline int sc_user_config(int kind,const char *path,int action)
{return sc_call(0x71,kind,(int)path,action,0,0,0);}
/* mio：配置纯检查，绝不提交/切模式/增主题代数/写盘。kind=0菜单/
 * 1快捷方式/2主题/3显示/4旧壁纸模式；0成功、-1语法/参数、-2读取。
 * 使用实际消费者解析器，不开放内核指针；USER/ENV用原0x71动作0。
 * 支持模式不代表本机设备可用，显示仍走15秒预览。详见CONFIG.md。 */
static inline int sc_config_check(int kind,const char *path)
{return sc_call(0x7B,kind,(int)path,0,0,0,0);}
static inline int sc_chdir(const char *path){return sc_call(0x72,(int)path,0,0,0,0,0);}
/* 将任务cwd与相对路径结合，返回不带根斜杠的SandFS路径，根为
 * 空串；不检查存在。最多64B含NUL，失败不截断输出。 */
static inline int sc_resolve(const char *path,char *out,int capacity)
{return sc_call(0x73,(int)path,(int)out,capacity,0,0,0);}
/* 创建真实附着CLI任务：只有本人的已启用终端可授权输出。返回正
 * 票据，不是pid；-2未找到，-3坏SCX，-4内存，-6上次作业未结束。
 * JOB只查询本人的最近票据，运行/暂停值沿用STATUS高位合同；父
 * 终端关闭取消CLI。原sc_exec异步GUI与原PUTS本人窗口语义仍保留。 */
static inline int sc_cli(const char *command,int terminal)
{return sc_call(0x74,(int)command,terminal,0,0,0,0);}
static inline int sc_job(int ticket){return sc_call(0x75,ticket,0,0,0,0,0);}
/* 本人ARGB窗口：0启用流式终端/1清空/2重新排版。内核按当前
 * 宽度、主题与缩放绘凤凰字形；历史按需4页、关闭自动释放。 */
static inline int sc_terminal(int win,int command)
{return sc_call(0x76,win,command,0,0,0,0);}

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
/* 固定32个u32：协议/样式/代数/标题高/任务栏高/圆角/边框/保留，
 * 然后24个命名RGB。所有字段一次复制，不能把128B传成32B容量。 */
static inline int sc_theme(u32 *info) { return sc_call(0x2C,(int)info,0,0,0,0,0); }
/* 0临时加载，1验证并持久化，2重新读SYS/THEME.CFG（path忽略）。
 * 0成功；-1参数/语法，-2读取，-3写入。详情见THEME.md。 */
static inline int sc_theme_load(const char *path,int action)
{ return sc_call(0x2D,(int)path,action,0,0,0,0); }
/* kind 0壁纸/1图标根/2名称。完整输出及末尾NUL必须放得下，容量
 * 1..64；不足失败不截断，返回正文长度。只得到副本，不借内核指针。 */
static inline int sc_theme_path(int kind,char *out,int capacity)
{ return sc_call(0x2E,kind,(int)out,capacity,0,0,0); }
/* M8: info八个u32为物理宽/高、UI百分比、后端(0 VGA/1 LFB)、模式
 * 代数、支持位图、预览秒、最大宽。apply=0预览15秒/1保存/2回退；
 * 后两者忽略w/h/s，程序退出也不影响内核计时回退。返回0才算成功。 */
static inline int sc_display(u32 *info){return sc_call(0x20,(int)info,0,0,0,0,0);}
static inline int sc_display_apply(int w,int h,int scale,int action)
{return sc_call(0x21,w,h,scale,action,0,0);}
/* open2外框用物理像素，INFO给当前客户区。FRAME严格匹配最新cw*ch；
 * 调整尺寸后必须重查。pointer六个i32是客户区x/y、按键、按下沿、
 * 释放沿、聚焦；坐标允许负数/超边界，沿读后清零，只有owner可读。
 * 旧M7仍用原WINOPEN/MOUSE，内核转换整数放大后的坐标。 */
static inline int sc_open2(const char *title,int w,int h){return sc_call(0x23,(int)title,w,h,0,0,0);}
static inline int sc_pointer(int win,int *out){return sc_call(0x22,win,(int)out,0,0,0,0);}
/* 观察快照保留按下/释放沿，避免“为了判断是否重绘”先吞掉点击。
 * 普通sc_pointer消费沿的旧合同不变，两者都只能看本人窗口。 */
static inline int sc_pointer_peek(int win,int *out){return sc_call(0x78,win,(int)out,0,0,0,0);}
static inline int sc_window(int win,int command){return sc_call(0x25,win,command,0,0,0,0);}
static inline int sc_reload(void){return sc_call(0x24,0,0,0,0,0,0);}
/* monitor输出48个u32，前8为版本/tick/管理内存/已用/空闲/任务槽/
 * 文件项/可运行数，后8行各20B(pid/state/name[12])。只读统计，不
 * 提供进程写权限。palette输出256个0x00RRGGBB，供BMP量化用。 */
static inline int sc_monitor(u32 *out){return sc_call(0x26,(int)out,0,0,0,0,0);}
/* mio：CPUINFO为64个u32的完整只读副本。头16字：版本/频率/tick/
 * 总样本/空闲/内核/用户/图形(内核子集)/槽数/方法(1=PIT采样)，
 * 其余保留0。后8行各6字：pid/state/本槽代数/运行样本/调度进入
 * 次数/保留。差分样本算占比，同代数才可减每任务计数；不能把已
 * 退出任务的pid复用误算成CPU突增。u32计数回绕按无符号减法。
 * IF=0时PIT不能采样，不是逐指令/宿主QEMU进程计时。详见CPU.md。 */
static inline int sc_cpu(u32 *out){return sc_call(0x79,(int)out,0,0,0,0,0);}
/* STORAGEINFO独立32字，详见STORAGE.md：真实ATA容量及有效SandFS
 * 卷、追加空闲/活跃数据/目录容量。引导软盘大小为双盘启动合同，
 * 不是保护模式FDC枚举结果。当前整盘卷，无MBR/GPT分区表。
 * 两个新快照都先验证全部输出映射，失败-1且不写任何部分。 */
static inline int sc_storage(u32 *out){return sc_call(0x7A,(int)out,0,0,0,0,0);}
static inline int sc_palette(u32 *out){return sc_call(0x27,(int)out,0,0,0,0,0);}
/* M8真彩色：open_rgb客户区为直通ARGB32，FRAME32的长度以字节计。
 * 0xAARRGGBB在x86内存排列为B/G/R/A，alpha=0透明、255不透明。
 * 场景合成之后向32bpp显存输出RGB；旧FRAME仍只能用于索引画布。
 * fill_rgb/text_rgb接收0x00RRGGBB并写不透明像素，文字仍取凤凰。
 * 禁止应用把用户地址当成显存地址；所有操作仍由owner句柄隔离。 */
static inline int sc_open_rgb(const char *title,int w,int h){return sc_call(0x28,(int)title,w,h,0,0,0);}
static inline int sc_frame32(int win,const u32 *pixels,u32 bytes){return sc_call(0x29,win,(int)pixels,(int)bytes,0,0,0);}
static inline int sc_fill_rgb(int win,int x,int y,int w,int h,u32 rgb){return sc_call(0x2A,win,x,y,w,h,(int)rgb);}
static inline int sc_text_rgb(int win,int x,int y,const char *text,u32 rgb){return sc_call(0x2B,win,x,y,(int)text,(int)rgb,0);}
/* 私有图形堆位于0x40000000..0x43FFFFFF；按4096B向上取整、全清零。
 * alloc失败返回NULL，不能按“int为负”判断：这里的合法地址是高端。
 * free只接受块首，成功0、非法/重复/中间页-1；退出自动回收剩余页。
 * allocator没有realloc或字节小块语义，解码器在三环自己管理小块。 */
static inline void *sc_alloc(u32 bytes){return (void *)sc_call(0x100,(int)bytes,0,0,0,0,0);}
static inline int sc_free(void *base){return sc_call(0x101,(int)base,0,0,0,0,0);}
/* 分段读取不补NUL，EOF=0，失败=-1；可读超过应用地址空间的大文件。 */
static inline int sc_read_at(const char *path,void *out,int n,u32 offset)
{return sc_call(0x38,(int)path,(int)out,n,(int)offset,0,0);}
/* mio：M8新增异步图片API；旧接口/缓冲一律保留。请求复制本人的
 * 路径，返回正31位票据。每任务一个作业，独立三环服务解PNG/JPEG/
 * WebP/BMP，错误压缩数据不会在内核解释。最大1920×1080、输入16MB。
 * result输出恰好8个u32：1,w,h,w*h*4,2,ticket,0,0；format=2为
 * 直通ARGB32，alpha未预乘。0就绪、1等待、负数失败。pixels=NULL/
 * bytes=0只查信息；完整复制像素成功才消耗票据，失败仍可重试。
 * -1参数/权限/缓冲，-2文件，-3服务，-4格式/坏数据，-5内存，
 * -6服务崩溃/30秒超时。失败作业由cancel回收；本人退出自动回收。
 * claim/submit仅内核实际派出的worker身份可用，手动EXEC同文件或
 * 伪造票据/复用PID都不能接管。普通应用只需request/result/cancel。
 * 自定义SCB继续使用原始位图库，无须压缩服务；BMP也可独立读取。
 */
static inline int sc_image_request(const char *path){return sc_call(0x7C,(int)path,0,0,0,0,0);}
static inline int sc_image_result(u32 ticket,u32 *info,void *pixels,u32 bytes)
{return sc_call(0x7D,(int)ticket,(int)info,(int)pixels,(int)bytes,0,0);}
static inline int sc_image_claim(u32 ticket,char *path,u32 capacity)
{return sc_call(0x7E,(int)ticket,(int)path,(int)capacity,0,0,0);}
static inline int sc_image_submit(u32 ticket,u32 w,u32 h,const void *pixels,int status)
{return sc_call(0x7F,(int)ticket,(int)w,(int)h,(int)pixels,status,0);}
static inline int sc_image_cancel(u32 ticket){return sc_call(0x80,(int)ticket,0,0,0,0,0);}
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
/* mio：M8游戏相对鼠标，追加0x81/82，不改变旧MOUSE/POINTER。
 * 仅本人当前聚焦窗口可捕获；1请求、0释放；成功0，非法/失焦-1。
 * ESC/故障/失焦/最小化/关闭自动释放，应用不能困住宿主桌面。
 * snapshot为8xi32：版本1、dx、dy、buttons、pressed、released、
 * captured、focused。dx/dy为原PS/2屏幕方向计数，与分辨率/缩放
 * 无关，读取消费位移和新接口的按键沿；旧POINTER沿独立保留。
 * 缓冲非法整个调用失败，不消耗事件；未捕获返回零位移/按键。
 * 此处捕获的是客体输入；QEMU宿主光标约束仍由QEMU窗口负责。 */
static inline int sc_mouse_capture(int win,int enabled)
{return sc_call(0x81,win,enabled,0,0,0,0);}
static inline int sc_mouse_relative(int win,int *snapshot)
{return sc_call(0x82,win,(int)snapshot,0,0,0,0);}
/* M9：独立32B身份/登录/权限快照，不扩旧STAT/CPU/MONITOR缓冲。
 * 登录只是取得内核票据，当前和已存在进程不改UID；新Shell通过
 * sc_auth_exec创建。SYSTEM无普通登录入口，票据值不是系统令牌。 */
static inline int sc_auth_info(u32 *out){return sc_call(0x200,(int)out,0,0,0,0,0);}
static inline int sc_auth_login(const char *name,const char *password)
{return sc_call(0x201,(int)name,(int)password,0,0,0,0);}
static inline int sc_auth_status(u32 ticket,u32 *out)
{return sc_call(0x202,(int)ticket,(int)out,0,0,0,0);}
static inline int sc_auth_exec(u32 ticket,const char *command,int activate)
{return sc_call(0x203,(int)ticket,(int)command,activate,0,0,0);}
static inline int sc_auth_users(char *out,u32 capacity)
{return sc_call(0x204,(int)out,(int)capacity,0,0,0,0);}
static inline int sc_auth_account(int action,const char *name,const char *home,int uid,int gid)
{return sc_call(0x205,action,(int)name,(int)home,uid,gid,0);}
static inline int sc_auth_cancel(u32 ticket){return sc_call(0x206,(int)ticket,0,0,0,0,0);}
static inline int sc_auth_password(const char *name,const char *password,u32 proof)
{return sc_call(0x207,(int)name,(int)password,(int)proof,0,0,0);}
static inline int sc_fsmeta(const char *path,u32 *out)
{return sc_call(0x208,(int)path,(int)out,0,0,0,0);}
/* uid/gid=-2表示保留，mode是u/g/o各两位的rw六位，非Unix的rwx八进制。 */
static inline int sc_permissions(const char *path,int uid,int gid,u32 mode)
{return sc_call(0x209,(int)path,uid,gid,(int)mode,0,0);}
/* M9字节流均非阻塞：-6暂时无数据/空间，0为EOF；close(fd,1)发布
 * 输出事务，close(fd,0)仅关闭（最后一个引用关闭时放弃未提交正文）。 */
static inline int sc_stream_open(const char *path,u32 mode,u32 capacity){return sc_call(0x210,(int)path,mode,capacity,0,0,0);}
static inline int sc_stream_read(int fd,void *data,u32 bytes){return sc_call(0x211,fd,(int)data,bytes,0,0,0);}
static inline int sc_stream_write(int fd,const void *data,u32 bytes){return sc_call(0x212,fd,(int)data,bytes,0,0,0);}
static inline int sc_stream_close(int fd,int commit){return sc_call(0x213,fd,commit,0,0,0,0);}
/* 等待前景作业时登记终端控制，父进程只查Ctrl-C事件，不抢读孩子
 * 的输入；action=1登记/0查询/2撤登记。退出自动撤回，Ctrl-D为EOF。 */
static inline int sc_stream_event(int fd,u32 action){return sc_call(0x226,fd,action,0,0,0,0);}
static inline int sc_storage2(u32 *out){return sc_call(0x227,(int)out,0,0,0,0,0);}
static inline int sc_dbgspawn2(const char *command,const int *fds){return sc_call(0x228,(int)command,(int)fds,0,0,0,0);}
/* 不可变内存输入快照，最多32KiB；可作stdin，同UID子进程共享读游标，
 * 最后引用关闭/退出回收。不返回内核指针，也不开放UART伪造入口。 */
static inline int sc_stream_memory(const void *data,u32 bytes){return sc_call(0x229,(int)data,bytes,0,0,0,0);}
/* 正常身份创建SandShell作业，代码快照绑定子任务fd3；标准输入输出
 * 仍取fds[3]。args是Shell脚本名和位置参数文本，不改变父任务身份。 */
static inline int sc_spawn_buffer(const void *script,u32 bytes,const int *fds,const char *args){return sc_call(0x22A,(int)script,bytes,(int)fds,(int)args,0,0);}
static inline int sc_terminal_info2(int fd,u32 *out){return sc_call(0x22B,fd,(int)out,0,0,0,0);}
static inline int sc_terminal_clear(int fd){return sc_call(0x22C,fd,0,0,0,0,0);}
/* 独立32B本人快照：1,pid,generation,uid,gid,realm,0,0。 */
static inline int sc_process_self(u32 *out){return sc_call(0x22D,(int)out,0,0,0,0,0);}
/* PID及代数原子匹配后按KILL2权限结束；陈旧目标=-8，不改旧调用。 */
static inline int sc_kill_generation(int pid,u32 generation){return sc_call(0x22E,pid,generation,0,0,0,0);}
static inline int sc_clock2(u32 *out){return sc_call(0x22F,(int)out,0,0,0,0,0);}
static inline int sc_cpu2(u32 *out){return sc_call(0x237,(int)out,0,0,0,0,0);}
static inline int sc_storage_flush(void){return sc_call(0x238,0,0,0,0,0,0);}
static inline int sc_create_ex(const char *path,u32 kind,u32 mode){return sc_call(0x239,(int)path,kind,mode,0,0,0);}
static inline int sc_job_info2(u32 job,u32 *out){return sc_call(0x23A,job,(int)out,0,0,0,0);}
static inline int sc_module_list(char *out,u32 bytes){return sc_call(0x23B,(int)out,bytes,0,0,0,0);}
static inline int sc_stream_dup(int from,int to){return sc_call(0x214,from,to,0,0,0,0);}
static inline int sc_stream_pipe(int *fds){return sc_call(0x215,(int)fds,0,0,0,0,0);}
static inline int sc_stream_seek(int fd,u32 offset){return sc_call(0x216,fd,offset,0,0,0,0);}
static inline int sc_tty(int window){return sc_call(0x217,window,0,0,0,0,0);}
static inline int sc_spawn2(const char *command,const int *fds,u32 flags){return sc_call(0x218,(int)command,(int)fds,flags,0,0,0);}
static inline int sc_wait2(u32 job,u32 *out){return sc_call(0x219,job,(int)out,0,0,0,0);}
static inline int sc_kill2(int pid){return sc_call(0x21A,pid,0,0,0,0,0);}
static inline int sc_processes2(u32 *out,u32 words){return sc_call(0x21B,(int)out,words,0,0,0,0);}
/* M10a1分页：16字头+每行16字，32..1040字；after=0xFFFFFFFF开始。
 * 头[3]本页行数，[4]后续游标或0xFFFFFFFF结束。一次调用身份/代数/
 * CPU样本来自同一快照；跨页进程可能退出或新增，调用者不缓存旧指针。
 * 老PROCESSINFO2仍固定520字、CPUINFO2固定200字，不扩写旧缓冲。 */
#define SC_PROCESS_PAGE_WORDS 528
static inline int sc_process_page(u32 *out,u32 words,u32 after){return sc_call(0x240,(int)out,words,after,0,0,0);}
#define SC_EVENT_INPUT 1u
#define SC_EVENT_VISIBILITY 2u
#define SC_EVENT_STREAM 4u
#define SC_EVENT_JOB 8u
#define SC_EVENT_NETWORK 16u
#define SC_EVENT_AUDIO 32u
#define SC_EVENT_IMAGE 64u
#define SC_EVENT_AUTH 128u
#define SC_EVENT_TIMEOUT 0x40000000u
/* 事件合并保留至消费；timeout为PIT tick，FFFFFFFF无限等待。
 * 睡眠仍是存活任务，历史state字段不借用新的数字改变旧ABI。 */
static inline int sc_event_wait(u32 mask,u32 timeout){return sc_call(0x241,mask,timeout,0,0,0,0);}
/* 会话不是UID；仅自己的普通登录会话可切换/注销。SYSTEM桌面需要
 * 外部串口管理凭据。visible不因失焦变0，epoch用于切回后完整重画。 */
static inline int sc_session_page(u32 *out,u32 words,u32 after){return sc_call(0x242,(int)out,words,after,0,0,0);}
static inline int sc_session_control(u32 id,u32 action){return sc_call(0x243,id,action,0,0,0,0);}
static inline int sc_visibility(int handle,u32 out[8]){return sc_call(0x244,handle,(int)out,0,0,0,0);}
static inline int sc_session_path(const char *leaf,char *out,u32 capacity){return sc_call(0x246,(int)leaf,(int)out,capacity,0,0,0);}
/* 窗口分页沿截图权限；空页仍可能有后续游标，不能以count=0判断结束。 */
static inline int sc_window_page(u32 *out,u32 words,u32 after){return sc_call(0x245,(int)out,words,after,0,0,0);}
/* 8字快照：版本/流类型/可读/可写/错误/EOF/可读字节/控制事件序号。
 * 查询后等待STREAM，再重查本人fd；事件是状态变化提示，不能代替IO返回。 */
static inline int sc_stream_ready(int fd,u32 out[8]){return sc_call(0x247,fd,(int)out,0,0,0,0);}
/* 原TTF脸编号12/16，完整16字信息；旧FONTINFO四字合同不变。 */
static inline int sc_fontinfo2(u32 face,u32 out[16]){return sc_call(0x248,face,(int)out,0,0,0,0);}
#define SC_FONT_BITMAP_WORDS 400
/* 请求像素1..64，输出400字：16字指标头＋96行×4字(每字bit31在左)。
 * 返回真实前进宽，缺字0且仍输出同脸.notdef；负数不写输出。
 * 头字段/旧字形兼容合同见FONT.md，words是完整缓冲容量。 */
static inline int sc_glyph_bitmap(u32 scalar,u32 face,u32 pixels,u32 *out,u32 words)
{return sc_call(0x249,scalar,face,pixels,(int)out,words,0);}
static inline int sc_arguments2(char *out,u32 capacity){return sc_call(0x21C,(int)out,capacity,0,0,0,0);}
static inline int sc_simd_info(u32 *out){return sc_call(0x21D,(int)out,0,0,0,0,0);}
static inline int sc_module_exec(const char *path,int resident){return sc_call(0x21E,(int)path,resident,0,0,0,0);}
static inline int sc_module_info(u32 *out){return sc_call(0x21F,(int)out,0,0,0,0,0);}
static inline int sc_capture_open(int handle,u32 *out){return sc_call(0x220,handle,(int)out,0,0,0,0);}
static inline int sc_capture_read(u32 token,u32 offset,void *out,u32 bytes){return sc_call(0x221,(int)token,offset,(int)out,bytes,0,0);}
static inline int sc_capture_close(u32 token){return sc_call(0x222,(int)token,0,0,0,0,0);}
static inline int sc_capture_list(char *out,u32 capacity){return sc_call(0x223,(int)out,capacity,0,0,0,0);}
static inline int sc_env_set(const char *name,const char *value,int remove){return sc_call(0x224,(int)name,(int)value,remove,0,0,0);}
static inline int sc_env_list(char *out,u32 capacity){return sc_call(0x225,(int)out,capacity,0,0,0,0);}
static inline int sc_audio_info(u32 *out){return sc_call(0x230,(int)out,0,0,0,0,0);}
static inline int sc_audio_open(u32 rate,u32 channels){return sc_call(0x231,rate,channels,0,0,0,0);}
static inline int sc_audio_submit(u32 token,const void *samples,u32 frames){return sc_call(0x232,(int)token,(int)samples,frames,0,0,0);}
static inline int sc_audio_control(u32 token,u32 command,u32 value){return sc_call(0x233,(int)token,command,value,0,0,0);}
static inline int sc_audio_status(u32 token,u32 *out){return sc_call(0x234,(int)token,(int)out,0,0,0,0);}
static inline int sc_audio_effect(int kind){return sc_call(0x235,kind,0,0,0,0,0);}
static inline int sc_power2(int restart){return sc_call(0x236,restart,0,0,0,0,0);}
/* 独立新增调用，不借旧WINDOWCTRL忽略的寄存器修改公开语义。 */
static inline int sc_window_place(int handle,int x,int y,int width,int height)
{return sc_call(0x23C,handle,x,y,width,height,0);}
/* M10a1追加网络入口，不重定义旧功能号/忽略寄存器/旧缓冲。
 * 地址为4xu32:1,主机序IPv4数,主机序端口,0；每次数据调用≤65536B。
 * -11是暂时不可用，-115连接进行中，错误为负；没有内核阻塞socket。
 * 各对象固定归创建PID及代数，不能靠同UID或猜句柄共享。 */
#define SC_NET_INFO_WORDS 96u
#define SC_NET_CONTROL_WORDS 32u
#define SC_NET_TCP 1u
#define SC_NET_UDP 2u
#define SC_NET_ICMP 3u
#define SC_NET_ARP 4u
#define SC_NET_READ 1u
#define SC_NET_WRITE 2u
#define SC_NET_ERROR 4u
#define SC_NET_HUP 8u
#define SC_NET_ACCEPT 16u
static inline int sc_net_info(u32 out[96]){return sc_call(0x250,(int)out,0,0,0,0,0);}
static inline int sc_net_control(u32 operation,const u32 in[32]){return sc_call(0x251,operation,(int)in,0,0,0,0);}
static inline int sc_socket(u32 type,u32 protocol){return sc_call(0x252,type,protocol,0,0,0,0);}
static inline int sc_socket_bind(u32 handle,const u32 address[4]){return sc_call(0x253,handle,(int)address,0,0,0,0);}
static inline int sc_socket_connect(u32 handle,const u32 address[4]){return sc_call(0x254,handle,(int)address,0,0,0,0);}
static inline int sc_socket_listen(u32 handle,u32 backlog){return sc_call(0x255,handle,backlog,0,0,0,0);}
static inline int sc_socket_accept(u32 handle,u32 address[4]){return sc_call(0x256,handle,(int)address,0,0,0,0);}
static inline int sc_socket_send(u32 handle,const void *data,u32 bytes,const u32 *address){return sc_call(0x257,handle,(int)data,bytes,(int)address,0,0);}
static inline int sc_socket_receive(u32 handle,void *data,u32 bytes,u32 *address){return sc_call(0x258,handle,(int)data,bytes,(int)address,0,0);}
static inline int sc_socket_shutdown(u32 handle,u32 how){return sc_call(0x259,handle,how,0,0,0,0);}
static inline int sc_socket_close(u32 handle){return sc_call(0x25A,handle,0,0,0,0,0);}
static inline int sc_socket_status(u32 handle,u32 out[16]){return sc_call(0x25B,handle,(int)out,0,0,0,0);}
static inline int sc_socket_option(u32 handle,u32 option,u32 value){return sc_call(0x25C,handle,option,value,0,0,0);}
static inline int sc_socket_page(u32 cursor,u32 *out,u32 words){return sc_call(0x25D,(int)out,words,cursor,0,0,0);}
static inline int sc_route_page(u32 cursor,u32 *out,u32 words){return sc_call(0x25E,(int)out,words,cursor,0,0,0);}
static inline int sc_arp_page(u32 cursor,u32 *out,u32 words){return sc_call(0x25F,(int)out,words,cursor,0,0,0);}
static inline int sc_dns_start(const char *name){return sc_call(0x260,(int)name,0,0,0,0,0);}
static inline int sc_dns_status(u32 ticket,u32 out[8]){return sc_call(0x261,ticket,(int)out,0,0,0,0);}
static inline int sc_dns_cancel(u32 ticket){return sc_call(0x262,ticket,0,0,0,0,0);}
/* 新网络执行入口：三个描述符必须是stdin读管道、stdout/stderr写
 * 管道。返回可wait的作业票据。普通身份不提升；外部SYSTEM明确
 * 调用时子进程为root，绝不继承UART端点或串口零环管理票据。
 * 老SPAWN2的flags/权限/继承语义保持。 */
static inline int sc_net_exec(const char *command,const int descriptors[3])
{return sc_call(0x263,(int)command,(int)descriptors,0,0,0,0);}
#endif
