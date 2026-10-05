/* =====================================================================
 * mio：M7 FILES。SandFS 仍保存平铺的完整路径，但 FSDIR 由内核生成
 * 直接子项；界面不能再简单过滤所有同前缀文件，否则会把孙目录内容
 * 误当当前目录。目录/文件操作全部调用 API，SYS/CORE 的保护来自
 * 内核，不依赖这个界面是否灰掉按钮；失败值明确显示，不能假报成功。
 *
 * 列表/名称都是私有副本。刷新改写 listing 时，选中路径不悬挂于旧
 * 行缓冲。完整路径始终最多 63B，视觉省略只影响展示，不影响执行。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
static char listing[65536],names[512][64],sizes[512][12],cwd[64],status[40];
static int kinds[512],count,selection;
static int file_context,context_x,context_y,last_click=-100,clicked=-1,body_y,visible_rows;
static void path_join(char *out,const char *directory,const char *name)
{
    copy(out,directory,64);
    if(out[0]) append(out,"/",64);
    append(out,name,64);
}
static int upper(int c) { return c>='a'&&c<='z'?c-'a'+'A':c; }
static int extension(const char *path,const char *ext)
{
    int n=length(path),m=length(ext);
    if(n<m) return 0;
    for(int i=0;i<m;i++) if(upper(path[n-m+i])!=upper(ext[i])) return 0;
    return 1;
}
static void result(int value,const char *success)
{
    if(value>=0) copy(status,success,sizeof(status));
    else if(value==-5) copy(status,"Core path protected by kernel",sizeof(status));
    else { copy(status,"Operation failed: ",sizeof(status));
        char n[12];
        decimal(n,value);
        append(status,n,sizeof(status)); }
}
static void refresh(void)
{
    count=0;
    int n=sc_dir(cwd,listing,sizeof(listing));
    if(n<0) { result(n,"");
        return; }
    char *p=listing;
    while(*p&&count<512) {
        int kind=*p++=='D'?2:1;
        if(*p!=' ') break;
        p++;
        char *name=p;
        while(*p&&*p!=' ') p++;
        if(!*p) break;
        *p++=0;
        char *size=p;
        while(*p&&*p!='\n') p++;
        if(*p) *p++=0;
        copy(names[count],name,64);
        copy(sizes[count],size,12);
        kinds[count++]=kind;
    }
    if(selection>=count) selection=count?count-1:0;
}
static void draw(void)
{
    ui_header("FILES",cwd[0]?cwd:"ROOT / SandFS v4");
    const int ids[]={1,2,3,4,5,6,7};
    const char *actions[]={"Open","Edit","Debug","Up","New dir","Rename","Delete"};
    body_y=ui_toolbar(ids,actions,7,ui_compact?38:70);
    int sidebar=UI_W>=600?144:0,left=16+sidebar;
    visible_rows=(UI_H-body_y-46)/28;if(visible_rows<1)visible_rows=1;
    if(sidebar)ui_panel(16,body_y,128,visible_rows*28+8);
    ui_panel(left,body_y,UI_W-left-16,visible_rows*28+8);
    const char *tabs[]={"ROOT","HOME","SYS","APPS","BIN"};
    if(sidebar)for(int i=0;i<5;i++)ui_control(100+i,24,body_y+12+i*36,112,tabs[i],0);
    int first=selection/visible_rows*visible_rows;
    for(int row=0;row<visible_rows&&first+row<count;row++) {
        int i=first+row,y=body_y+4+row*28;
        if(i==selection){ui_rect(left+4,y,UI_W-left-24,28,PAL_UI_NIGHT+6);ui_rect(left+4,y,2,28,PAL_UI_CYAN+7);}
        if(kinds[i]==2){ui_rect(left+14,y+9,14,10,PAL_UI_GOLD+5);ui_rect(left+14,y+6,6,3,PAL_UI_GOLD+7);}
        else{ui_rect(left+16,y+6,10,14,PAL_UI_MUTED);ui_span(left+18,y+10,6,PAL_UI_PANEL);}
        char label[64];copy(label,names[i],ui_clamp((UI_W-left-144)/8,8,64));
        if(i==selection)ui_selected_text(left+40,y+6,label,1);
        else ui_text(left+40,y+6,label,PAL_UI_MUTED);
        if(kinds[i]!=2&&UI_W-left>330){
            if(i==selection)ui_selected_text(UI_W-104,y+6,sizes[i],1);
            else ui_text(UI_W-104,y+6,sizes[i],PAL_UI_MUTED);
        }
        if(!file_context&&ui_hit(left+4,y,UI_W-left-24,28)){
            if(ui_pressed&2){selection=i;file_context=1;context_x=ui_clamp(ui_x,0,UI_W-180);context_y=ui_clamp(ui_y,0,UI_H-230);}
            if(ui_pressed&1){selection=i;if(clicked==i&&sc_tick()-last_click<35)ui_action=1;clicked=i;last_click=sc_tick();}}
    }
    if(!count)ui_text(left+24,body_y+20,"Empty directory",PAL_UI_MUTED);
    ui_small_control(8,16,UI_H-68,72,"Prev",0);ui_small_control(9,96,UI_H-68,72,"Next",0);
    if(!sidebar)ui_small_control(10,176,UI_H-68,72,"Path",0);
    if(file_context){ui_panel(context_x,context_y,180,224);for(int i=0;i<7;i++)ui_control(i+1,context_x+8,context_y+8+i*30,164,actions[i],0);}
    ui_footer(status[0]?status:"ENTER open N mkdir F2 rename X delete");
}
static void selected_path(char *out)
{ if(count) path_join(out,cwd,names[selection]);
    else copy(out,cwd,64); }
static void open_selected(int edit,int debug)
{
    if(!count) return;
    char path[64],command[128];
    selected_path(path);
    if(kinds[selection]==2) {copy(cwd,path,64);
        selection=0;
        refresh();
        return;}
    if(extension(path,".SCX")&&!edit) {
        if(debug) {copy(command,"apps/debugger.scx ",128);
            append(command,path,128);}else copy(command,path,128);
    } else if(extension(path,".SCB")||extension(path,".BMP")) {
        copy(command,"apps/lens.scx ",128);append(command,path,128);
    } else {
        if(!extension(path,".C")&&!extension(path,".H")&&!extension(path,".INC")&&!extension(path,".ASM")
            &&!extension(path,".TXT")&&!extension(path,".CFG")&&!extension(path,".LOG")) {
            copy(status,"Binary file; use an application",sizeof(status));
            return;
        }
        copy(command,"apps/ide.scx ",128);
        append(command,path,128);
    }
    result(sc_exec(command),"Application started");
}
static void parent(void)
{
    int n=length(cwd);
    while(n&&cwd[n-1]!='/') n--;
    if(n) n--;
    cwd[n]=0;
    selection=0;
    refresh();
}
static void mutation(int operation)
{
    char path[64],next[64];
    selected_path(path);
    draw();
    if(operation==0) {
        copy(next,cwd,64);
        if(next[0]) append(next,"/",64);
        if(ui_edit_path(next,64,"Create directory")) result(sc_mkdir(next),"Directory created");
    } else if(operation==1&&count) {
        copy(next,path,64);
        if(ui_edit_path(next,64,"Rename / move path")) result(sc_rename(path,next),"Path renamed");
    } else if(operation==2&&count) {
        if(ui_confirm("Delete selected path?",path))result(sc_remove(path),"Path deleted");
    }
    refresh();
}
int main(void)
{
    if(ui_open("FILES / mio")<0) return 1;
    sc_args(cwd,sizeof(cwd));
    u32 info[2];
    if(cwd[0]&&(sc_stat(cwd,info)||info[0]!=2)) cwd[0]=0;
    refresh();
    draw();
    ui_present();
    for(;;) {
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        int key=sc_key();
        int a=ui_action;
        if(a>=1&&a<=7){const int keys[]={10,4,'d',8,'n',2,'x'};key=keys[a-1];file_context=0;}
        if(a>=100&&a<=104)key='1'+a-100;
        if(a==8){selection-=visible_rows;if(selection<0)selection=0;}
        if(a==9){selection+=visible_rows;if(selection>=count)selection=count?count-1:0;}
        if(a==10){char p[64];copy(p,cwd,64);if(ui_edit_path(p,64,"Directory path")){u32 info[2];if(!sc_stat(p,info)&&info[0]==2){copy(cwd,p,64);selection=0;refresh();}}}
        if(file_context&&(ui_pressed&1)&&!a)file_context=0;
        if(key<0)continue;if(key==27){if(file_context){file_context=0;continue;}return 0;}
        status[0]=0;
        if(key=='j'||key==0x81) {if(selection+1<count) selection++;}
        else if(key=='k'||key==0x80) {if(selection) selection--;}
        else if(key==10) open_selected(0,0);
        else if(key==4) open_selected(1,0);
        else if(key=='d'||key=='D') open_selected(0,1);
        else if(key==8) parent();
        else if(key==5) refresh();
        else if(key=='n'||key=='N') mutation(0);
        else if(key==2) mutation(1);
        else if(key=='x'||key=='X') mutation(2);
        else if(key>='1'&&key<='5') {const char *places[]={"","HOME","SYS","APPS","BIN"};
            copy(cwd,places[key-'1'],64);
            selection=0;
            refresh();}
        draw();
        ui_present();
    }
}
