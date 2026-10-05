/* =====================================================================
 * mio：Files，ring3 文件管理器。
 * SandFS 暂为路径前缀目录，所以导航只过滤 FSLIST 的名字，不伪装成真正
 * inode 目录树。每次 F5 重读磁盘元数据，能看到汇编器/记事本新建的文件。
 * j/k 移动选择，Enter 执行 SCX 或用 Notes 打开文本；不触碰内核对象。
 * 文件名数组与列表缓冲分开：解析列表时就地切行，选中路径仍独立有效。
 * ===================================================================== */
#include "api.h"
/* 容量对应 SandFS v3 的 96 项与 31B 路径合同。最大一行含名字/空格/
 * 十进制大小/换行，小于 48B；8192B 足够一次装下全目录与结尾 NUL。
 * paths 独立复制路径，不能保存 listing 内指针：下一次 refresh 会覆写它。 */
static char listing[8192],paths[96][32],sizes[96][12],command[128];
static int count,selection,directory,win;
static const char *prefixes[]={"","sys/","bin/","apps/","home/","desk/"};

static int begins(const char *s,const char *prefix)
{ while(*prefix) if(*s++!=*prefix++) return 0; return 1; }
/* FSLIST 只提供平铺文本“完整路径 大小\n”。此处就地切成两段后按前缀
 * 过滤，不向内核申请目录对象；空前缀自然匹配全部文件。当前目录序号
 * 只由 0..5 按键设置，所以 prefixes 索引始终在定义范围内。
 * 刷新后选择可能越过新目录末尾，必须收回到最后一项；空目录保持 0，
 * open_selected 则先检查 count，不能凭这个 0 去读不存在的路径。 */
static void refresh(void)
{
    int n=sc_list(listing,sizeof(listing)); count=0;
    if(n<0) { listing[0]=0; return; }
    char *p=listing;
    while(*p && count<96) {
        char *name=p;
        while(*p && *p!=' ') p++;
        if(!*p) break;
        *p++=0; char *bytes=p;
        while(*p && *p!='\n') p++;
        if(*p) *p++=0;
        if(begins(name,prefixes[directory])) {
            copy(paths[count],name,32); copy(sizes[count],bytes,12); count++;
        }
    }
    if(selection>=count) selection=count?count-1:0;
}
/* 每页七行，页起点按 selection/7 对齐；上/下跨过第七行时自然翻页。
 * 先 page 清整画布再重绘，短名称覆盖长名称、空目录覆盖旧目录时都不会
 * 遗留残字。执行路径不做裁剪，展示省略仅是排版策略。 */
static void draw(void)
{
    page(win,"FILES / SandFS","j/k select  ENTER open  F5 refresh");
    sc_text(win,8,34,"0 all 1 sys 2 bin 3 apps 4 home",PAL_CON_TINT);
    int first=(selection/7)*7;
    for(int row=0;row<7 && first+row<count;row++) {
        int idx=first+row, y=50+row*10;
        if(idx==selection) sc_fill(win,6,y-1,290,10,PAL_TASKBAR);
        /* 显示名允许省略尾部，但执行仍使用完整 paths；右侧独立列显示
         * 字节数，路径最长 31B 时也不会与大小相互覆盖。 */
        char label[25]; copy(label,paths[idx],sizeof(label));
        sc_text(win,8,y,label,idx==selection?PAL_CON_TINT:PAL_TITLE);
        sc_text(win,216,y,sizes[idx],PAL_WIN_TITLE);
    }
    if(!count) sc_text(win,8,54,"(empty)",PAL_WIN_TITLE);
}
/* EXEC 接受“程序路径 参数”而非文件关联对象。SCX 可直接运行，文本
 * 拼成 apps/note.scx + 完整路径。128B command 足以容纳 14B 前缀与
 * 31B 文件路径；copy/append 仍带显式容量，避免以后扩展路径时溢出。
 * 启动异步完成，Files 本身保留；新窗口由 WM 取得焦点，关闭后可继续浏览。 */
static void open_selected(void)
{
    if(!count) return;
    const char *name=paths[selection]; int n=length(name);
    if(n>=4 && equal(name+n-4,".scx")) copy(command,name,sizeof(command));
    else {
        /* 只把文本文件交给编辑器，二进制资源不能当成可编辑的字符流。
         * 这样 Enter 打开字体/图标时不会把二进制 NUL 截断后误保存。 */
        if(n<4 || (!equal(name+n-4,".txt")&&!equal(name+n-4,".asm")&&!equal(name+n-4,".cfg"))) {
            sc_fill(win,0,124,304,26,PAL_CON_BG);
            sc_text(win,8,128,"binary resource / use SCX app",PAL_CON_WARN); return;
        }
        copy(command,"apps/note.scx ",sizeof(command)); append(command,name,sizeof(command));
    }
    if(sc_exec(command)<0) {
        sc_fill(win,0,124,304,26,PAL_CON_BG);
        sc_text(win,8,128,"open failed",PAL_CON_WARN);
    }
}
void main(void)
{
    win=sc_open("Files",306,164); if(win<0) return;
    refresh(); draw();
    for(;;) {
        int key=sc_key(); if(key<0) continue;
        if(key==27) return;
        if(key=='j'||key==0x81) { if(selection+1<count) selection++; }
        else if(key=='k'||key==0x80) { if(selection) selection--; }
        else if(key>='0'&&key<='5') { directory=key-'0'; selection=0; refresh(); }
        else if(key==5) refresh();
        else if(key==10) { open_selected(); continue; }
        draw();
    }
}
