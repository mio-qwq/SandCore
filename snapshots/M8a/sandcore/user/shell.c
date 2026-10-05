/* =====================================================================
 * mio：M8 SandShell。此程序仍是普通三环应用，不把命令解释器放内核。
 *
 * 用户名/家目录/PATH来自有效配置；cwd属于本任务。除cd/clear/exit
 * 和兼容异步run外，所有普通命令通过PATH执行SCX，输出由内核授权
 * 汇入终端。等待用作业票据，不能把回收后复用的pid当上一个命令。
 * 逻辑历史由终端保存，缩放/改宽/主题重排时不丢内容、不放大旧像素。
 * ===================================================================== */
#include "SCAPI.H"

static int window,line_used,history_used,history_index;
static char line[256],history[8][256];
static int waiting,last_status;
static char username[32],cwd[65];

static void prompt(void)
{
    sc_user(0,0,username,32);sc_user(2,0,cwd,65);
    char text[112];copy(text,username,112);append(text,"@",112);append(text,cwd,112);append(text,"> ",112);
    sc_puts(text);line_used=0;line[0]=0;history_index=history_used;
}
static void diagnostic(const char *message,int code)
{
    char text[128],number[12];decimal(number,code);
    copy(text,message,128);append(text," (",128);append(text,number,128);append(text,")\n",128);sc_puts(text);
}
static void replace_line(const char *value)
{
    /* 退格次数依据逻辑字符数，不依据上一帧像素列；窄窗口折行后
     * 历史重排会自动回到正确位置。输入目前为键盘ASCII，UTF-8配置
     * 名称可正常显示，文件文本不受ASCII输入范围限制。 */
    while(line_used>0){sc_puts("\b");line_used--;}
    copy(line,value,256);line_used=length(line);sc_puts(line);
}
static void save_history(void)
{
    if(!line_used)return;
    if(history_used && equal(history[history_used-1],line))return;
    if(history_used==8) {
        for(int i=0;i<7;i++)copy(history[i],history[i+1],256);
        history_used=7;
    }
    copy(history[history_used++],line,256);
}
static int execute(void)
{
    save_history();sc_puts("\n");
    char parsed[256];copy(parsed,line,256);char *p=parsed;char *name=token(&p);
    if(!*name)return 0;
    if(equal(name,"exit"))return 1;
    if(equal(name,"clear")){sc_terminal(window,1);return 0;}
    if(equal(name,"cd")) {
        char home[64];
        if(!*p){sc_user(1,0,home,64);p=home;}
        int result=sc_chdir(p);if(result<0)diagnostic("cd: directory unavailable",result);
        return 0;
    }
    if(equal(name,"run")) {
        /* 原M6/M7“run HOME/...”命令仍按根路径异步启动GUI；CLI
         * 普通命令才使用cwd/PATH。不猜测文件是否GUI，也不改旧EXEC。 */
        int result=sc_exec(p);
        if(result<0)diagnostic("run: launch failed",result);else sc_puts("started\n");
        return 0;
    }
    int ticket=sc_cli(line,window);
    if(ticket<1) {
        diagnostic(ticket==-2?"command not found":ticket==-3?"invalid SCX":ticket==-6?"job still attached":"launch failed",ticket);
        last_status=ticket;return 0;
    }
    waiting=ticket;return 0;
}
int main(void)
{
    u32 display[8];sc_display(display);
    int width=display[0]>900?900:(int)display[0]-24;
    int height=display[1]>600?560:(int)display[1]-64;
    if(display[3]==0){width=306;height=172;}
    window=sc_open_rgb("SandShell",width,height);
    if(window<0 || sc_terminal(window,0)<0)return 1;
    sc_puts("SANDCORE  /  USER SPACE\nShell by mio\nType help.  Up / Down recall commands.\n\n");prompt();
    for(;;) {
        if(waiting) {
            int result=sc_job(waiting);
            if(result!=0x40000000 && result!=0x40000001) {
                last_status=result;waiting=0;
                if(result)diagnostic("command exited",result);
                prompt();
            }
            /* CLI只读取参数，不偷取父终端键盘队列。等待期间Shell
             * 不消费按键，提前敲下一条命令可在退出后继续顺序处理。 */
            sc_yield();continue;
        }
        int key=sc_key();
        if(key==27)return 0;
        if(key==10 || key==13) {
            line[line_used]=0;if(execute())return 0;
            if(!waiting)prompt();
        }else if(key==8 && line_used) {
            line[--line_used]=0;sc_puts("\b");
        }else if(key==0x80 && history_index>0) {
            history_index--;replace_line(history[history_index]);
        }else if(key==0x81 && history_index<history_used) {
            history_index++;replace_line(history_index==history_used?"":history[history_index]);
        }else if(key>=32 && key<=126) {
            if(line_used<255) {
                char one[2];one[0]=(char)key;one[1]=0;
                line[line_used++]=(char)key;line[line_used]=0;sc_puts(one);
            }
        }
        if(key<0)sc_yield(); /* 已到达的输入成批消费，再把CPU让回桌面 */
    }
}
