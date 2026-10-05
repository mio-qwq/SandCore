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
#include "UI.inc"
static char listing[16384],names[192][64],sizes[192][12],cwd[64],status[40];
static int kinds[192],count,selection;
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
    while(*p&&count<192) {
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
    ui_panel(8,43,60,103);
    ui_panel(75,43,235,103);
    const char *tabs[]={"ROOT","HOME","SYS","APPS","BIN"};
    for(int i=0;i<5;i++) {ui_number(12,52+i*17,i+1,PAL_UI_CYAN+5);
        ui_text(22,52+i*17,tabs[i],PAL_UI_MUTED);}
    int first=selection/8*8;
    for(int row=0;row<8&&first+row<count;row++) {
        int i=first+row,y=49+row*11;
        if(i==selection) {ui_rect(80,y-1,225,11,PAL_UI_NIGHT+6);
            ui_rect(80,y-1,2,11,PAL_UI_CYAN+7);}
        if(kinds[i]==2) {ui_rect(87,y+1,9,6,PAL_UI_GOLD+5);
            ui_rect(87,y-1,4,2,PAL_UI_GOLD+7);}
        else {ui_rect(88,y,7,8,PAL_UI_MUTED);
            ui_span(90,y+2,3,PAL_UI_PANEL);}
        char label[21];
        copy(label,names[i],sizeof(label));
        ui_text(103,y,label,i==selection?PAL_UI_TEXT:PAL_UI_MUTED);
        if(kinds[i]!=2) ui_text(267,y,sizes[i],PAL_UI_NIGHT+7);
    }
    if(!count) ui_text(89,58,"Empty directory",PAL_UI_MUTED);
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
        ui_panel(12,48,294,65);
        ui_text(22,58,"Delete selected path?",PAL_UI_GOLD+7);
        char short_name[33];
        copy(short_name,path,sizeof(short_name));
        ui_text(22,78,short_name,PAL_UI_TEXT);
        ui_text(22,99,"Y confirm  ESC cancel",PAL_UI_MUTED);
        ui_present();
        for(;;) {int key=sc_key();
            if(key==27) break;
            if(key=='y'||key=='Y') {result(sc_remove(path),"Path deleted");
                break;}sc_yield();}
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
        int key=sc_key();
        if(key<0) {sc_yield();
            continue;}if(key==27) return 0;
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
