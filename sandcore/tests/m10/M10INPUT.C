#include "SCIO.H"
/* 输入/绘制延迟探针走普通窗口、键盘与事件ABI。没有内核测试后门，
 * 隐藏时只等待可见性；串口标记在实际客户画布更新完成后才发出。
 * 标记并不证明显存已合成，宿主还须截真实画面核对工作区像素。 */
static u32 theme[32];
static char report_line[256];
static int paint(int window,int count,int key,int full)
{
    int geometry[5];if(sc_info(window,geometry)<0 || sc_theme(theme)<0)return -1;
    int width=geometry[2],height=geometry[3];
    if(full){
        if(sc_fill_rgb(window,0,0,width,height,theme[8+SC_THEME_PAPER])<0)return -1;
        sc_fill_rgb(window,24,24,4,32,theme[8+SC_THEME_ACCENT]);
        sc_text_rgb(window,40,30,"Input timing",theme[8+SC_THEME_TEXT]);
        sc_text_rgb(window,24,72,"Real keys. Native pixels. Active task load.",theme[8+SC_THEME_MUTED]);
        sc_fill_rgb(window,24,108,width-48,1,theme[8+SC_THEME_LINE]);
        sc_text_rgb(window,24,height-36,"Press letters; Esc exits.",theme[8+SC_THEME_MUTED]);
    }
    sc_fill_rgb(window,24,128,width-48,96,theme[8+SC_THEME_FACE_ALT]);
    sc_text_rgb(window,40,144,"KEY EVENTS",theme[8+SC_THEME_MUTED]);
    char line[64],digits[12];copy(line,"Count ",sizeof(line));decimal(digits,count);append(line,digits,sizeof(line));
    append(line,"    Key ",sizeof(line));decimal(digits,key);append(line,digits,sizeof(line));
    return sc_text_rgb(window,40,180,line,theme[8+SC_THEME_TEXT]);
}
static void value(const char *name,int number)
{char digits[12];decimal(digits,number);append(report_line,name,sizeof(report_line));append(report_line,digits,sizeof(report_line));}
static void report(void)
{
    /* 同一UART还有交互Shell的结束标记。数字逐段write会交错成两条
     * 坏记录；先拼好这条短消息，再用一次流调用提交完整行。 */
    append(report_line,"\n",sizeof(report_line));cli_text(1,report_line);
}
int main(void)
{
    int window=sc_open_rgb("Input timing",640,360);if(window<0)return 1;
    u32 visibility[8],last_epoch=0,last_theme=0;int count=0,last_key=0,ready=0;
    for(;;){
        if(sc_visibility(window,visibility)<0)return 2;
        if(!visibility[1]){sc_event_wait(SC_EVENT_VISIBILITY,0xFFFFFFFFu);continue;}
        if(sc_theme(theme)<0)return 3;
        int full=!ready || last_epoch!=visibility[2] || last_theme!=theme[2] || visibility[4];
        if(full){if(paint(window,count,last_key,1)<0)return 4;last_epoch=visibility[2];last_theme=theme[2];}
        if(!ready){
            int geometry[5];u32 self[8],display[8];if(sc_info(window,geometry)<0 || sc_process_self(self)<0 || sc_display(display)<0)return 5;
            copy(report_line,"M10INPUT_READY ",sizeof(report_line));value("window=",window);value(" x=",geometry[0]);value(" y=",geometry[1]);
            value(" w=",geometry[2]);value(" h=",geometry[3]);value(" pid=",(int)self[1]);value(" generation=",(int)self[2]);report();ready=1;
            copy(report_line,"M10INPUT_CLIENT ",sizeof(report_line));value("x=",geometry[0]+1);value(" y=",geometry[1]+(display[0]==320?13:24*(int)display[2]/100));
            value(" w=",geometry[2]);value(" h=",geometry[3]);report();
        }
        int key=sc_key();if(key==27)break;
        if(key>=0){
            last_key=key;count++;if(paint(window,count,key,0)<0)return 6;
            copy(report_line,"M10INPUT ",sizeof(report_line));value("count=",count);value(" key=",key);value(" tick=",sc_tick());report();
        }else sc_event_wait(SC_EVENT_INPUT|SC_EVENT_VISIBILITY,0xFFFFFFFFu);
    }
    cli_text(1,"M10INPUT_DONE\n");return 0;
}
