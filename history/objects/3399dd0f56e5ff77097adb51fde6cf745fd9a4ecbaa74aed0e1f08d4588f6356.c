/* mio：Monitor只展示真实内核快照。内存图是每秒已用物理页的历史，
 * 不伪装CPU负载；空槽、暂停、僵尸都明确列出。刷新/冻结只影响本
 * 程序采样，不停止调度器。窗口改变重新计算图表与任务行的宽度。 */
#include "SCAPI.H"
#include "NUI.inc"
static u32 snapshot[48],display[8],history[120];
static int cursor,samples,last_sample,frozen;
static inline void sample(void)
{
    sc_monitor(snapshot);
    sc_display(display);
    if(!frozen&&sc_tick()-last_sample>=100){
        last_sample=sc_tick();
        history[cursor]=snapshot[3];
        cursor=(cursor+1)%120;
        if(samples<120)samples++;

    }
}
static inline void draw(void)
{

    ui_header("MONITOR / SYSTEM OBSERVATORY","Real memory pages, tasks and display / no synthetic CPU meter");
    ui_control(1,16,70,104,frozen?"Resume":"Freeze",frozen);
    ui_control(2,128,70,104,"Refresh",0);
    int chart_h=(UI_H-174)/2;
    if(chart_h<56)chart_h=56;
    ui_panel(16,116,UI_W-32,chart_h);
    ui_text(32,126,"PHYSICAL MEMORY / KiB",PAL_UI_CYAN+7);
    ui_number(UI_W-208,126,snapshot[3]/1024,PAL_UI_TEXT);
    ui_text(UI_W-144,126,"used",PAL_UI_MUTED);
    int graph_y=156,graph_h=chart_h-48,graph_w=UI_W-64;
    for(int i=1;i<samples;i++){
        int a=(cursor-samples+i-1+120)%120,b=(a+1)%120;
        int y0=graph_y+graph_h-(int)(history[a]/1024)*graph_h/(int)(snapshot[2]/1024?snapshot[2]/1024:1);
        int y1=graph_y+graph_h-(int)(history[b]/1024)*graph_h/(int)(snapshot[2]/1024?snapshot[2]/1024:1);
        ui_line(32+(i-1)*graph_w/119,y0,32+i*graph_w/119,y1,PAL_UI_CYAN+7);

    }
    int y=126+chart_h;
    ui_panel(16,y,UI_W-32,UI_H-y-46);
    ui_text(32,y+10,"TASKS / pid   state       name",PAL_UI_CYAN+7);
    const char *states[]={
        "empty","runnable","zombie","paused"
    };
    int rows=(UI_H-y-80)/22;
    if(rows>8)rows=8;
    for(int i=0;i<rows;i++){
        u32 *row=snapshot+8+i*5;
        ui_number(32,y+38+i*22,row[0],PAL_UI_MUTED);
        ui_text(88,y+38+i*22,row[1]<4?states[row[1]]:"unknown",row[1]==3?PAL_UI_GOLD+7:PAL_UI_TEXT);
        char name[13];
        for(int j=0;j<12;j++)name[j]=((char *)(row+2))[j];
        name[12]=0;
        ui_text(216,y+38+i*22,name,PAL_UI_MUTED);

    }
    char footer[100],s[12];
    copy(footer,display[3]?"LFB ":"VGA ",100);
    decimal(s,display[0]);
    append(footer,s,100);
    append(footer," x ",100);
    decimal(s,display[1]);
    append(footer,s,100);
    append(footer," / ",100);
    decimal(s,display[2]);
    append(footer,s,100);
    append(footer,"% UI / free KiB ",100);
    decimal(s,snapshot[4]/1024);
    append(footer,s,100);
    ui_footer(footer);
}
int main(void)
{
    if(ui_open("Monitor")<0)return 1;
    sample();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();
        sample();
        draw();
        ui_present();
        int k=sc_key();
        if(k==27)return 0;
        if(ui_action==1||k==' ')frozen=!frozen;
        if(ui_action==2||k==5){
            sc_monitor(snapshot);
            sc_display(display);

        }
    }
}
