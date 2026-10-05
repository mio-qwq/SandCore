/* mio：编译器共用的三环状态窗口，不参与词法、汇编或机器码生成。
 * phase是实际阶段，done/total仅在存在确定分母时画进度，不能把耗时
 * 猜成百分比。轮询在解析安全边界发生，取消先于输出文件提交。
 * 所有状态属于当前进程，Studio只依赖退出码/日志，不读取这些字段。 */
#include "NUI.inc"
static char build_view_source[128],build_view_target[128],build_view_message[512];
static int build_view_active,build_view_failed,build_view_done,build_view_total;
static int build_view_tick,build_view_cancel;
static void build_view_draw(void)
{
    ui_header("Build tools","Native compiler / source to machine code");
    if(UI_W<260||UI_H<172){
        ui_text(16,36,"Enlarge for build details",PAL_UI_MUTED);
        ui_small_control(98,16,60,112,"Enlarge",0);return;
    }
    int y=ui_compact?34:78,w=UI_W-32;
    ui_clip_set(16,y,w,40);
    ui_text(16,y,build_view_source,PAL_UI_TEXT);
    ui_text(16,y+20,build_view_target,PAL_UI_MUTED);ui_clip_clear();
    y+=ui_compact?44:62;
    /* 诊断按可见字节分行，只裁绘制副本；日志仍保留完整文件名/行号。
     * 超出高度由真实日志和Studio读取，不能写进按钮或客户区末页。 */
    int bottom=UI_H-(ui_compact?52:74),at=0;
    ui_clip_set(16,y,w,bottom-y);
    while(build_view_message[at]&&y+16<=bottom){
        char row[192];int n=0,cells=0;
        while(build_view_message[at]&&build_view_message[at]!='\n'&&cells<(w/8)-1&&n<184){
            unsigned char first=(unsigned char)build_view_message[at];
            int bytes=first<128?1:first<224?2:first<240?3:4;
            for(int k=0;k<bytes&&build_view_message[at];k++)row[n++]=build_view_message[at++];
            cells+=bytes==1?1:2;
        }
        if(build_view_message[at]=='\n')at++;
        row[n]=0;ui_text(16,y,row,build_view_failed?PAL_UI_ALERT:PAL_UI_TEXT);y+=20;
    }
    ui_clip_clear();
    int bar=UI_H-(ui_compact?48:66);
    ui_rect(16,bar,w,4,PAL_UI_LINE);
    if(build_view_total>0){
        /* total是最多65536个标记或8192字节，乘窗口宽不溢出i32。 */
        int done=ui_clamp(build_view_done,0,build_view_total);
        ui_rect(16,bar,w*done/build_view_total,4,PAL_UI_CYAN+7);
    }
    ui_small_control(1,16,bar+9,112,build_view_active?"Cancel":"Close",0);
    if(UI_W>420)ui_text(144,bar+12,build_view_active?"Esc cancels safely":"Esc closes window",PAL_UI_MUTED);
}
static void build_view_begin(const char *title,const char *source,const char *target)
{
    copy(build_view_source,source,sizeof(build_view_source));
    copy(build_view_target,target,sizeof(build_view_target));
    build_view_active=1;build_view_tick=-100;
    ui_open(title);
}
static int build_view_poll(const char *phase,int done,int total,int force)
{
    copy(build_view_message,phase,sizeof(build_view_message));
    build_view_done=done;build_view_total=total;
    int tick=sc_tick();
    if(!force&&tick-build_view_tick<10)return build_view_cancel;
    build_view_tick=tick;
    if(ui_win<0||!ui_pixels)return build_view_cancel;
    ui_pointer();build_view_draw();ui_present();
    int key=ui_action?-1:sc_key();
    if(ui_action==98){sc_window(ui_win,1);ui_followup=1;}
    else if(ui_action==1||key==27)build_view_cancel=1;
    return build_view_cancel;
}
static void build_view_result(const char *message,int failed)
{
    build_view_active=0;build_view_failed=failed;
    build_view_poll(message,failed?0:1,1,1);
}
static void build_view_wait(void)
{
    if(ui_win<0||!ui_pixels)return;
    build_view_cancel=0;ui_followup=1;
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();build_view_draw();ui_present();
        int key=ui_action?-1:sc_key();
        if(ui_action==98){sc_window(ui_win,1);ui_followup=1;}
        else if(ui_action==1||key==27)return;
    }
}
