/* =====================================================================
 * mio：Notes，ring3 ASCII 记事本，整文件读写 SandFS。
 * 传入路径就打开该文本，未传路径默认 home/notes.txt；文件不存在则为空。
 * 当前编辑模型为末尾追加/退格，自动折行与尾部滚动。F2 显式保存，F3
 * 重新读取；Esc 关闭。这里不借内核 Shell 或 term 网格，所有状态在用户页。
 * 文本最多 4095 字节并保留终止符，保存时只写正文长度，不把 NUL 放入文件。
 * ===================================================================== */
#include "api.h"
static char text[4096],path[32],arguments[128];
static int win,size,changed;
static const char *status="ready";

/* FSREAD 返回正文长度，不保证给正文追加字符串终止符；这里预留一字节
 * 并自行 NUL 终止。打开不存在的路径进入空白新文件状态，实际创建只在
 * 用户明确按 F2 时发生。F3 复用此路径，主动舍弃未保存编辑并重读盘上版本。 */
static void load(void)
{
    int n=sc_read(path,text,sizeof(text)-1);
    size=n<0?0:n; text[size]=0; changed=0;
    status=n<0?"new file":"loaded";
}
static void draw(void)
{
    page(win,"NOTES / ASCII","F2 save  F3 reload  ESC close");
    sc_text(win,8,34,path,PAL_CON_TINT);
    /* 视觉行与文件换行是两件事：35 列自动折行只改变显示游标，不往
     * text 插入 LF；用户 Enter 输入的 LF 才是保存文件中的真实字节。
     * 第一遍只测量最后游标在哪一行，第二遍按同一折行规则画尾部七行。 */
    int row=0,col=0;
    /* 先算视觉行数，再只显示尾部七行；逻辑文件始终完整保留。
     * 不能把滚动实现成删除字符串开头，否则保存会丢掉已滚出的正文。 */
    for(int i=0;i<size;i++) {
        if(text[i]=='\n') { row++; col=0; }
        else { if(col==35) { row++; col=0; } col++; }
    }
    int first=row>6?row-6:0; row=0; col=0;
    for(int i=0;i<size;i++) {
        char c=text[i];
        if(c=='\n') { row++; col=0; continue; }
        if(col==35) { row++; col=0; }
        if(row>=first) {
            char glyph[2]={c>=32&&c<=126?c:'.',0};
            sc_text(win,8+col*8,48+(row-first)*10,glyph,PAL_TITLE);
        }
        col++;
    }
    if(col==35) { row++; col=0; }
    if(row-first<7) sc_fill(win,8+col*8,56+(row-first)*10,6,1,PAL_CON_TINT);
    sc_text(win,212,10,changed?"edited":status,changed?PAL_CON_WARN:PAL_CON_TINT);
}
void main(void)
{
    win=sc_open("Notes",306,164); if(win<0) return;
    /* 参数先拷贝到本任务 arguments，token 就地拆词后，path 再保存一份。
     * 后续绘制/保存不依赖临时分词位置，也不把用户指针交给另一个任务。 */
    sc_args(arguments,sizeof(arguments)); char *p=arguments; char *name=token(&p);
    copy(path,*name?name:"home/notes.txt",sizeof(path)); load(); draw();
    for(;;) {
        int key=sc_key(); if(key<0) continue;
        if(key==27) return;
        if(key==2) {
            /* 只有完整写入 size 字节才清除 changed；失败后仍保留内存
             * 正文供再次保存。保存不含 NUL，空正文 size=0 也可合法建文件。
             * 没有后台自动保存，因此 Esc/红叉不会意外覆盖用户盘上旧版本。 */
            int n=sc_write(path,text,size);
            if(n==size) { changed=0; status="saved"; } else status="failed";
        } else if(key==3) load();
        else if(key==8 && size) { text[--size]=0; changed=1; }
        else if((key==10 || (key>=32&&key<=126)) && size<(int)sizeof(text)-1) {
            text[size++]=(char)key; text[size]=0; changed=1;
        }
        draw();
    }
}
