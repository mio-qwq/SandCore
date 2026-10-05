/* =====================================================================
 * mio：Monitor使用公开的只读快照，不以内存或刷新率猜CPU占用。
 *
 * CPU历史来自两次100Hz客体PIT采样差分；内核含图形，不能重复
 * 求和。每任务代数防止复用pid时旧累计计数减出巨大负载。冻结只
 * 停止本程序采样，恢复先换基点，不把冻结期间算成“一秒”。
 * 图形布局按当前客户区/缩放重算；紧凑窗口用Next/方向键看全八槽，
 * Volumes从真实ATA/FS状态取数，不编造MBR/GPT分区。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
static u32 snapshot[48],display[8],storage[SC_STORAGE_WORDS];
static u32 cpu[SC_CPU_WORDS],before[SC_CPU_WORDS],history[120],memory_history[120];
static int task_percent[8],cursor,samples,last_sample,frozen,view,first_task;
static int cpu_valid,cpu_busy,cpu_idle,cpu_kernel,cpu_user,cpu_graphics;

static void baseline(void)
{
    sc_cpu(before);last_sample=sc_tick();
}
static void sample(void)
{
    sc_display(display);
    if(frozen)return;
    sc_monitor(snapshot);sc_storage(storage);
    if((u32)sc_tick()-(u32)last_sample<100)return;
    sc_cpu(cpu);last_sample=sc_tick();
    u32 total=cpu[3]-before[3];cpu_valid=total!=0;
    if(total){
        cpu_idle=(int)((cpu[4]-before[4])*100/total);
        cpu_kernel=(int)((cpu[5]-before[5])*100/total);
        cpu_user=(int)((cpu[6]-before[6])*100/total);
        cpu_graphics=(int)((cpu[7]-before[7])*100/total);cpu_busy=100-cpu_idle;
        for(int i=0;i<8;i++){
            u32 *a=cpu+16+i*6,*b=before+16+i*6;
            u32 ticks=a[2]==b[2]?a[3]-b[3]:a[3];
            task_percent[i]=(int)(ticks*100/total);
            if(task_percent[i]>100)task_percent[i]=100;
        }
        history[cursor]=(u32)cpu_busy;memory_history[cursor]=snapshot[3]/1024;
        cursor=(cursor+1)%120;if(samples<120)samples++;
    }
    for(int i=0;i<SC_CPU_WORDS;i++)before[i]=cpu[i];
}
static void metric(int x,int y,const char *label,int value)
{
    char text[64],n[12];copy(text,label,64);
    if(cpu_valid){decimal(n,value);append(text,n,64);append(text,"%",64);}
    else append(text,"--",64);
    ui_text(x,y,text,PAL_UI_MUTED);
}
static void chart(int x,int y,int w,int h,const char *title,const u32 *values,u32 limit,int color)
{
    ui_panel(x,y,w,h);ui_text(x+12,y+10,title,PAL_UI_TEXT);
    int gx=x+12,gy=y+(ui_compact?30:40),gh=h-(ui_compact?42:54),gw=w-24;
    if(gh<8)gh=8;if(!limit)limit=1;
    ui_line(gx,gy+gh,gx+gw,gy+gh,PAL_UI_LINE);
    for(int i=1;i<samples;i++){
        int a=(cursor-samples+i-1+120)%120,b=(a+1)%120;
        int y0=gy+gh-(int)(values[a]*(u32)gh/limit);
        int y1=gy+gh-(int)(values[b]*(u32)gh/limit);
        ui_line(gx+(i-1)*gw/119,y0,gx+i*gw/119,y1,color);
    }
}
static void overview(void)
{
    int y=ui_compact?82:114,h=ui_compact?72:(UI_H-240)/2;
    if(h<100 && !ui_compact)h=100;
    int half=(UI_W-44)/2;
    chart(16,y,half,h,"CPU / busy %",history,100,PAL_UI_CYAN+7);
    chart(28+half,y,half,h,"RAM / KiB",memory_history,snapshot[2]/1024,PAL_UI_GOLD+7);
    y+=h+8;metric(20,y,"Idle ",cpu_idle);metric(UI_W/4,y,"Kernel ",cpu_kernel);
    metric(UI_W/2,y,"User ",cpu_user);metric(UI_W*3/4,y,"Gfx ",cpu_graphics);
    y+=24;ui_panel(16,y,UI_W-32,UI_H-y-(ui_compact?28:38));
    ui_text(28,y+8,"PID   STATE       NAME",PAL_UI_TEXT);
    ui_text(UI_W-100,y+8,"CPU %",PAL_UI_TEXT);
    int rows=(UI_H-y-68)/22;if(rows<1)rows=1;if(rows>8)rows=8;
    if(first_task>8-rows)first_task=8-rows;
    const char *states[]={"empty","runnable","zombie","paused"};
    for(int n=0;n<rows;n++){
        int i=first_task+n;u32 *row=snapshot+8+i*5;int ty=y+34+n*22;
        ui_number(28,ty,row[0],PAL_UI_MUTED);
        ui_text(76,ty,row[1]<4?states[row[1]]:"unknown",row[1]==3?PAL_UI_GOLD+7:PAL_UI_MUTED);
        char name[13];for(int j=0;j<12;j++)name[j]=((char *)(row+2))[j];name[12]=0;
        ui_text(184,ty,i?name:"kernel",PAL_UI_TEXT);
        if(cpu_valid && row[1])ui_number(UI_W-90,ty,task_percent[i],PAL_UI_CYAN+7);
    }
}
static void storage_row(int y,const char *label,u32 value,const char *unit)
{
    char number[12],text[64];decimal(number,(int)value);copy(text,number,64);append(text,unit,64);
    ui_text(32,y,label,PAL_UI_MUTED);ui_text(UI_W/2,y,text,PAL_UI_TEXT);
}
static void volumes(void)
{
    int y=ui_compact?82:114,step=ui_compact?20:30;
    ui_panel(16,y,UI_W-32,UI_H-y-(ui_compact?28:38));
    ui_text(32,y+12,"IDE0 / whole-device SandFS",PAL_UI_CYAN+7);y+=step+16;
    if(!storage[6]){ui_text(32,y,"No valid volume",PAL_UI_ALERT);return;}
    storage_row(y,"ATA capacity",storage[3]/2," KiB");y+=step;
    storage_row(y,"Volume capacity",storage[8]/2," KiB");y+=step;
    storage_row(y,"Append free",storage[11]/2," KiB");y+=step;
    storage_row(y,"Live file data",storage[15]/2," KiB");y+=step;
    storage_row(y,"Historical holes",storage[17]/2," KiB");y+=step;
    storage_row(y,"Directory used",storage[13]," entries");y+=step;
    storage_row(y,"Directory capacity",storage[12]," entries");y+=step;
    if(y+20<UI_H-34)ui_text(32,y,"No MBR/GPT table / deleted holes are not reclaimed",PAL_UI_MUTED);
}
static void draw(void)
{
    ui_header("MONITOR / SYSTEM OBSERVATORY","100Hz guest CPU samples / memory / disks and volumes");
    int y=ui_compact?42:70;
    ui_control(1,16,y,96,"Overview",view==0);ui_control(2,120,y,96,"Volumes",view==1);
    ui_control(3,224,y,96,frozen?"Resume":"Freeze",frozen);
    ui_control(4,328,y,88,"Refresh",0);
    if(UI_W>530)ui_control(5,424,y,96,"Next tasks",0);
    if(view)volumes();else overview();
    ui_footer(frozen?"Frozen view / scheduler continues":"PIT state samples / Gfx is inside Kernel / not host CPU");
}
int main(void)
{
    if(ui_open("Monitor")<0)return 1;
    sc_monitor(snapshot);sc_storage(storage);baseline();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();sample();draw();ui_present();int k=sc_key();
        if(k==27)return 0;
        if(ui_action==1)view=0;if(ui_action==2)view=1;
        if(ui_action==3 || k==' '){frozen=!frozen;if(!frozen)baseline();}
        if(ui_action==4 || k==5){sc_monitor(snapshot);sc_storage(storage);baseline();}
        if(ui_action==5 || k==0x81){first_task++;if(first_task>=8)first_task=0;}
        if(k==0x80 && first_task>0)first_task--;
    }
}
