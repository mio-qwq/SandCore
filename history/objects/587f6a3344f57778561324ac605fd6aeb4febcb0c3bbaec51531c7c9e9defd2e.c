/* =====================================================================
 * mio：Monitor的M8独立草稿；正式user/monitor.c仍保持旧版本。
 * 合同先见docs/MONITOR.md、CPU.md、STORAGE.md。图形、分页、
 * 历史均在三环私有页，所有数据来自原有只读快照，不改变ABI。
 *
 * 充足空间保留CPU/RAM历史和任务总览，紧凑窗口优先真实数据行，
 * 细分/历史另有页面；不能用固定坐标挤出高缩放客户区。Freeze只
 * 停采样，Resume重建基点。Gfx属于Kernel，不重复相加；无样本/
 * 读取失败明确显示，不把内存页数或重画速度当CPU占用。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
static u32 snapshot[48],storage[SC_STORAGE_WORDS],cpu[SC_CPU_WORDS],before[SC_CPU_WORDS];
static u32 history[120],memory_history[120],memory_history_valid[120];
static int task_percent[8],cursor,samples,last_sample,frozen,view,first_task,first_volume,first_metric;
static int cpu_valid,baseline_valid,monitor_valid,storage_valid;
static int cpu_busy,cpu_idle,cpu_kernel,cpu_user,cpu_graphics;
static int monitor_y,monitor_bottom,task_y,visible_tasks,volume_rows,metric_rows,small_window;
static int monitor_menu,menu_x,menu_y,menu_columns,menu_step,menu_button,menu_width,menu_height;
static volatile int monitor_operations;

static int percent(u32 value,u32 total)
{
    if(!total)return 0;
    if(value>=total)return 100;
    /* value*100可能先溢出而最终比例仍合法。累加100次并取模得到
     * 同一向下取整结果。rest<total；只有rest<total-value才相加，
     * 因此加法也不会溢出。不依赖浮点/64位运行库，成本有界。
     * 复用槽取新代计数，不能减上一代；超本窗口的值夹100。 */
    int result=0;u32 rest=0;
    for(int i=0;i<100;i++){
        if(rest>=total-value){rest-=total-value;result++;}
        else rest+=value;
    }
    return result;
}
static void baseline(void)
{
    u32 next[SC_CPU_WORDS];
    baseline_valid=!sc_cpu(next)&&next[0]==1&&next[8]==8;
    if(baseline_valid)for(int i=0;i<SC_CPU_WORDS;i++)before[i]=next[i];
    last_sample=sc_tick();cpu_valid=0;
}
static void snapshots(void)
{
    u32 next[48],disk[SC_STORAGE_WORDS];
    monitor_valid=!sc_monitor(next)&&next[0]==1&&next[5]==8;
    storage_valid=!sc_storage(disk)&&disk[0]==1;
    /* 每类完整成功才提交；失败保留原副本和历史但明确标不可用。
     * 三次调用不构成原子全系统快照，不能声称名字与CPU同周期。 */
    if(monitor_valid)for(int i=0;i<48;i++)snapshot[i]=next[i];
    if(storage_valid)for(int i=0;i<SC_STORAGE_WORDS;i++)storage[i]=disk[i];
}
static void sample(void)
{
    if(frozen||(u32)sc_tick()-(u32)last_sample<100)return;
    snapshots();
    if(sc_cpu(cpu)||cpu[0]!=1||cpu[8]!=8){baseline_valid=cpu_valid=0;last_sample=sc_tick();return;}
    last_sample=sc_tick();
    if(!baseline_valid){for(int i=0;i<SC_CPU_WORDS;i++)before[i]=cpu[i];baseline_valid=1;cpu_valid=0;return;}
    u32 total=cpu[3]-before[3];cpu_valid=total!=0;
    if(total){
        cpu_idle=percent(cpu[4]-before[4],total);cpu_kernel=percent(cpu[5]-before[5],total);
        cpu_user=percent(cpu[6]-before[6],total);cpu_graphics=percent(cpu[7]-before[7],total);cpu_busy=100-cpu_idle;
        for(int i=0;i<8;i++){
            u32 *a=cpu+16+i*6,*b=before+16+i*6;
            task_percent[i]=percent(a[2]==b[2]?a[3]-b[3]:a[3],total);
        }
        history[cursor]=(u32)cpu_busy;memory_history_valid[cursor]=(u32)monitor_valid;
        if(monitor_valid)memory_history[cursor]=snapshot[3]/1024;
        cursor=(cursor+1)%120;if(samples<120)samples++;
    }
    for(int i=0;i<SC_CPU_WORDS;i++)before[i]=cpu[i];
}
static void metric(int x,int y,const char *label,int value)
{
    char text[64],number[12];copy(text,label,sizeof(text));
    if(cpu_valid){decimal(number,value);append(text,number,sizeof(text));append(text,"%",sizeof(text));}
    else append(text,"--",sizeof(text));
    ui_text(x,y,text,PAL_UI_MUTED);
}
static void chart(int x,int y,int w,int h,const char *title,const u32 *values,u32 limit,int ram,int color)
{
    ui_panel(x,y,w,h);ui_clip_set(x+4,y+4,w-8,h-8);ui_text(x+8,y+6,title,PAL_UI_TEXT);
    int gx=x+8,gy=y+24,gh=h-32,gw=w-16;
    if(gh<8||gw<8){ui_clip_clear();return;}
    if(!limit)limit=1;
    ui_line(gx,gy+gh,gx+gw-1,gy+gh,PAL_UI_LINE);
    for(int i=1;i<samples;i++){
        int a=(cursor-samples+i-1+120)%120,b=(a+1)%120;
        /* RAM失败的点不连成虚假0值曲线；时间位置不挪动，只跳
         * 对应线段。比例先化成0..100再映射高度，大内存计数不会
         * 与高窗口相乘溢出。图线仍完整裁剪在自身面板内。 */
        if(ram&&(!memory_history_valid[a]||!memory_history_valid[b]))continue;
        int y0=gy+gh-percent(values[a],limit)*gh/100,y1=gy+gh-percent(values[b],limit)*gh/100;
        ui_line(gx+(i-1)*(gw-1)/119,y0,gx+i*(gw-1)/119,y1,color);
    }
    ui_clip_clear();
}
static void arrows(int x,int y,int rows)
{
    int h=rows*22,button=h<32?8:16;
    for(int r=0;r<4;r++){
        ui_span(x+4-r,y+2+r,1+r*2,PAL_UI_MUTED);
        ui_span(x+1+r,y+h-button+2+r,7-r*2,PAL_UI_MUTED);
    }
    if((ui_pressed&1)&&ui_hit(x,y,12,button))ui_action=20;
    if((ui_pressed&1)&&ui_hit(x,y+h-button,12,button))ui_action=21;
}
static void task_name(char *out,const char *input)
{
    int n=0;
    /* 名称只有12B且不保证NUL，旧布局可能截在UTF-8标量中间。
     * 只复制完整标量到视觉副本，不读第13B，不回写真实快照。
     * 未收字交凤凰缺字框，禁止为方便而另装一套字体。 */
    while(n<12&&input[n]){
        int bytes=(u8)input[n]<128?1:(u8)input[n]>=0xC2&&(u8)input[n]<=0xF4?((u8)input[n]<0xE0?2:(u8)input[n]<0xF0?3:4):0;
        if(!bytes||n+bytes>12)break;
        int valid=1;for(int j=1;j<bytes;j++)if(((u8)input[n+j]&192)!=128)valid=0;
        /* 续字节形状还不足以证明合法标量：排除过长编码、代理
         * 区和大于U+10FFFF的四字节形式，避免把截坏名称交给
         * 字形解码器后误画成另一字符。原12B快照始终不变。 */
        if(bytes>=3&&((u8)input[n]==0xE0&&(u8)input[n+1]<0xA0))valid=0;
        if(bytes>=3&&((u8)input[n]==0xED&&(u8)input[n+1]>=0xA0))valid=0;
        if(bytes==4&&((u8)input[n]==0xF0&&(u8)input[n+1]<0x90))valid=0;
        if(bytes==4&&((u8)input[n]==0xF4&&(u8)input[n+1]>0x8F))valid=0;
        if(!valid)break;
        for(int j=0;j<bytes;j++)out[n+j]=input[n+j];n+=bytes;
    }
    out[n]=0;
}
static void overview(void)
{
    int y=monitor_y,available=monitor_bottom-y;
    if(available>=300){
        int half=(UI_W-44)/2,h=(available-140)/2;if(h>180)h=180;
        if(UI_W>=500){chart(16,y,half,h,"CPU / busy %",history,100,0,PAL_UI_CYAN+7);
            chart(28+half,y,half,h,"RAM / KiB",memory_history,snapshot[2]/1024,1,PAL_UI_GOLD+7);}
        else chart(16,y,UI_W-32,h,"CPU / busy %",history,100,0,PAL_UI_CYAN+7);
        y+=h+8;ui_clip_set(16,y,UI_W-32,40);
        metric(24,y,"Idle ",cpu_idle);metric(UI_W/2,y,"Kernel ",cpu_kernel);
        metric(24,y+20,"User ",cpu_user);metric(UI_W/2,y+20,"Gfx ",cpu_graphics);ui_clip_clear();y+=44;
    }
    int heading=ui_compact?18:28;
    /* 任务行起点与箭头共用同一实际布局值。大窗口上方有历史
     * 图，不能把工具条后的monitor_y直接当任务列表；这个私有
     * 值也让只读验收在同一帧定位，绝不成为外部写入口。 */
    task_y=y+heading;
    visible_tasks=(monitor_bottom-y-heading-2)/22;if(visible_tasks>8)visible_tasks=8;
    ui_panel(16,y,UI_W-32,monitor_bottom-y);
    ui_clip_set(24,y+2,UI_W-48,heading);ui_text(24,y+2,"TASKS / CPU %",PAL_UI_TEXT);ui_clip_clear();
    if(!monitor_valid){ui_clip_set(24,y+heading,UI_W-48,monitor_bottom-y-heading);
        ui_text(24,y+heading,"Task snapshot unavailable",PAL_UI_ALERT);ui_clip_clear();return;}
    if(visible_tasks<1)return;
    first_task=ui_clamp(first_task,0,8-visible_tasks);
    const char *states[]={"empty","run","zombie","paused"};
    ui_clip_set(24,task_y,UI_W-60,visible_tasks*22);
    for(int n=0;n<visible_tasks;n++){
        int i=first_task+n,ty=task_y+n*22;u32 *row=snapshot+8+i*5;
        ui_number(24,ty,row[0],PAL_UI_MUTED);ui_text(64,ty,row[1]<4?states[row[1]]:"unknown",row[1]==3?PAL_UI_GOLD+7:PAL_UI_MUTED);
        char name[13],label[13];task_name(name,(char *)(row+2));
        ui_copy_utf8(label,i?name:"kernel",ui_clamp((UI_W-230)/8,2,13));ui_text(136,ty,label,PAL_UI_TEXT);
        if(cpu_valid&&row[1])ui_number(UI_W-76,ty,task_percent[i],PAL_UI_CYAN+7);else ui_text(UI_W-76,ty,"--",PAL_UI_MUTED);
    }
    ui_clip_clear();arrows(UI_W-36,task_y,visible_tasks);
}
static void value_row(int y,const char *label,u32 value,const char *unit)
{
    char number[12],text[48];decimal(number,(int)value);copy(text,number,sizeof(text));append(text,unit,sizeof(text));
    ui_clip_set(24,y,UI_W/2-32,20);ui_text(24,y,label,PAL_UI_MUTED);ui_clip_clear();
    ui_clip_set(UI_W/2,y,UI_W/2-40,20);ui_text(UI_W/2,y,text,PAL_UI_TEXT);ui_clip_clear();
}
static void volumes(void)
{
    int heading=ui_compact?18:28;volume_rows=(monitor_bottom-monitor_y-heading-2)/22;
    if(volume_rows>8)volume_rows=8;ui_panel(16,monitor_y,UI_W-32,monitor_bottom-monitor_y);
    ui_clip_set(24,monitor_y+2,UI_W-48,heading);ui_text(24,monitor_y+2,"IDE0 / SandFS / no MBR",PAL_UI_CYAN+7);ui_clip_clear();
    if(!storage_valid||!storage[6]){ui_clip_set(24,monitor_y+heading,UI_W-48,monitor_bottom-monitor_y-heading);
        ui_text(24,monitor_y+heading,storage_valid?"No valid volume":"Storage snapshot unavailable",PAL_UI_ALERT);ui_clip_clear();return;}
    if(volume_rows<1)return;
    first_volume=ui_clamp(first_volume,0,8-volume_rows);
    const char *labels[]={"ATA capacity","Volume capacity","Append free","Live file data","Historical holes","Directory used","Dir capacity","Format version"};
    const char *short_labels[]={"ATA size","Volume size","Append free","Live data","Old holes","Dir used","Dir limit","FS version"};
    const int indices[]={3,8,11,15,17,13,12,7};
    /* 单行数据优先完整数字。窄窗口使用同义短标签，不把一项
     * 折成两行后覆盖下一条；字段次序/来源与宽视图完全相同。 */
    for(int n=0;n<volume_rows;n++){int i=first_volume+n;value_row(monitor_y+heading+n*22,UI_W<400?short_labels[i]:labels[i],i<5?storage[indices[i]]/2:storage[indices[i]],i<5?" KiB":i<7?" entries":"");}
    arrows(UI_W-36,monitor_y+heading,volume_rows);
}
static void details(void)
{
    int heading=ui_compact?18:28;metric_rows=(monitor_bottom-monitor_y-heading-2)/22;
    if(metric_rows>8)metric_rows=8;first_metric=ui_clamp(first_metric,0,8-metric_rows);
    ui_panel(16,monitor_y,UI_W-32,monitor_bottom-monitor_y);
    ui_clip_set(24,monitor_y+2,UI_W-48,heading);ui_text(24,monitor_y+2,"Gfx is inside Kernel",PAL_UI_CYAN+7);ui_clip_clear();
    const char *labels[]={"Busy","Idle","Kernel","User","Gfx subset","RAM used","RAM free","RAM total"};
    const int values[]={cpu_busy,cpu_idle,cpu_kernel,cpu_user,cpu_graphics,0,0,0};
    for(int n=0;n<metric_rows;n++){
        int i=first_metric+n,y=monitor_y+heading+n*22;
        if(i<5&&cpu_valid)value_row(y,labels[i],(u32)values[i]," %");
        else if(i>=5&&monitor_valid)value_row(y,labels[i],snapshot[i==5?3:i==6?4:2]/1024," KiB");
        else{ui_text(24,y,labels[i],PAL_UI_MUTED);ui_text(UI_W/2,y,"--",PAL_UI_MUTED);}
    }
    if(metric_rows)arrows(UI_W-36,monitor_y+heading,metric_rows);
}
static int toolbar(void)
{
    const char *labels[]={"Overview","Volumes",frozen?"Resume":"Freeze","Refresh","Next tasks"};
    const int widths[]={96,96,96,88,96};int x=16,y=ui_compact?38:70,step=ui_compact?26:36;
    for(int i=0;i<5;i++){
        /* 宽窗口保留旧五按钮位置；窄窗口按文案排两行，高缩放
         * 仍留真实数据行。命中与绘制共用宽高，不能只缩文字。 */
        int w=UI_W>=536?widths[i]:length(labels[i])*8+20;
        if(x+w>UI_W-16){x=16;y+=step;}
        int active=i==0?view==0:i==1?view==1:i==2?frozen:0;
        if(ui_compact)ui_small_control(i+1,x,y,w,labels[i],active);else ui_control(i+1,x,y,w,labels[i],active);
        x+=w+8;
    }
    return y+step+8;
}
static void menu_geometry(void)
{
    menu_step=ui_compact?26:36;menu_columns=7*menu_step+16>UI_H-16?2:1;
    menu_button=menu_columns==2?118:164;
    menu_width=menu_columns*(menu_button+8)+8;menu_height=((7+menu_columns-1)/menu_columns)*menu_step+16;
    menu_x=ui_clamp(menu_x,8,UI_W-menu_width-8);menu_y=ui_clamp(menu_y,8,UI_H-menu_height-8);
}
static void draw(void)
{
    visible_tasks=volume_rows=metric_rows=0;small_window=UI_W<276||UI_H<172;
    if(small_window){
        monitor_menu=0;ui_background(SC_THEME_FACE_ALT);int h=UI_H<22?UI_H:22;
        ui_button_box(4,(UI_H-h)/2,UI_W-8,h,UI_W>=104?"Enlarge":"+",0);
        if((ui_pressed&1)&&ui_hit(4,(UI_H-h)/2,UI_W-8,h))ui_action=98;return;
    }
    int pressed=ui_pressed;if(monitor_menu)ui_pressed=0;
    const char *titles[]={"Tasks / charts","ATA and SandFS","CPU / memory details","CPU history","RAM history"};
    ui_header("MONITOR",titles[view]);monitor_y=toolbar();monitor_bottom=UI_H-(ui_compact?22:30)-6;
    if(view==0)overview();else if(view==1)volumes();else if(view==2)details();
    else chart(16,monitor_y,UI_W-32,monitor_bottom-monitor_y,view==3?"CPU / busy %":"RAM / KiB",view==3?history:memory_history,view==3?100:snapshot[2]/1024,view==4,view==3?PAL_UI_CYAN+7:PAL_UI_GOLD+7);
    char footer[80];
    if(frozen)copy(footer,"Frozen / scheduler continues",sizeof(footer));
    else{copy(footer,"Busy ",sizeof(footer));char number[12];
        if(cpu_valid){decimal(number,cpu_busy);append(footer,number,sizeof(footer));append(footer,"%",sizeof(footer));}
        else append(footer,"--",sizeof(footer));append(footer," / Right menu / Left-Right pages",sizeof(footer));}
    ui_clip_set(0,UI_H-(ui_compact?22:30),UI_W,ui_compact?22:30);ui_footer(footer);ui_clip_clear();ui_pressed=pressed;
    if(monitor_menu){
        const char *labels[]={"Overview","Volumes","CPU details","CPU history","RAM history",frozen?"Resume":"Freeze","Refresh"};
        menu_geometry();ui_panel(menu_x,menu_y,menu_width,menu_height);
        for(int i=0;i<7;i++){int x=menu_x+8+(i%menu_columns)*(menu_button+8),y=menu_y+8+(i/menu_columns)*menu_step;
            if(ui_compact)ui_small_control(40+i,x,y,menu_button,labels[i],0);else ui_control(40+i,x,y,menu_button,labels[i],0);}
    }
}
static void scroll(int down)
{
    int step=down?1:-1;
    if(view==0)first_task=ui_clamp(first_task+step,0,8-visible_tasks);
    else if(view==1)first_volume=ui_clamp(first_volume+step,0,8-volume_rows);
    else if(view==2)first_metric=ui_clamp(first_metric+step,0,8-metric_rows);
    else view=view==3?4:3;
}
int main(void)
{
    if(ui_open("Monitor / mio")<0)return 1;
    snapshots();baseline();ui_pointer();draw();ui_present();
    for(;;){
        sample();if(!ui_frame_due())continue;
        ui_pointer();int was_menu=monitor_menu;draw();ui_present();
        int action=ui_action,key=(action||(was_menu&&(ui_pressed&1)))?-1:sc_key();
        if(action==98){sc_window(ui_win,1);continue;}
        if(was_menu&&(ui_pressed&1)&&!action){monitor_menu=0;ui_followup=1;continue;}
        int chosen=action>=40&&action<=46;
        if(chosen){int item=action-40;monitor_menu=0;if(item<5){view=item;action=0;}else action=item==5?3:4;}
        if(action==1)view=0;if(action==2)view=1;
        if(action==3||key==' '){frozen=!frozen;if(!frozen){snapshots();baseline();}}
        if(action==4||key==5){snapshots();baseline();}
        if(action==5){view=0;int rows=visible_tasks>0?visible_tasks:1;
            /* Next循环真实可滚动的起点。不能先增到不可见起点
             * 再被draw夹回尾页，否则两行以上的尾页永远不回首。 */
            first_task++;if(first_task>8-rows)first_task=0;}
        if(action==20||key==0x80)scroll(0);if(action==21||key==0x81)scroll(1);
        if(key==0x82)view=(view+4)%5;if(key==0x83)view=(view+1)%5;
        if(!small_window&&(ui_pressed&2)){monitor_menu=!monitor_menu;menu_x=ui_x;menu_y=ui_y;}
        if(key==27){if(monitor_menu)monitor_menu=0;else return 0;}
        if(chosen||action||key>=0||(ui_pressed&2)){monitor_operations++;ui_followup=1;draw();ui_present();}
    }
}
