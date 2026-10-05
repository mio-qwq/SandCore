/* =====================================================================
 * mio：SC STUDIO，三环内部 IDE。编辑缓冲属于应用，光标可在任意行移动；
 * 保存/编译/运行/调试经普通文件与进程 API 完成，内核不内置编译器。
 * F5 启动 SCCC 或 SandAsm，等待 STATUS 确认结束，再读取本次诊断。
 * 编译失败不运行旧产物，即使磁盘上仍留着上次成功的同名 SCX。
 *
 * UTF-8 注释按字符边界移动/删除，已收汉字用系统原生字形，缺字显示框，原字节仍保留；
 * 键盘可输入 ASCII C/汇编。源码最多 65535B，超过上限拒绝打开，
 * 不能截掉尾部再保存。路径对话框也是三环软件图形，不靠内核控件。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#define EDIT_MAX 65536
static char source[EDIT_MAX],open_scratch[EDIT_MAX],filename[64]="HOME/HELLO.C",output[64]="HOME/HELLO.SCX";
static char diagnostic_text[4096],status[40]="F1 open / F3 new";
static int used,cursor,first_line,left_column,dirty,compiled,compiler_pid=-1,diagnostic_mode;
static int editor_y,editor_rows,editor_columns,edit_menu,menu_x,menu_y;
static int utf8_next(int at)
{
    if(at>=used) return used;
    at++;
    while(at<used&&((u8)source[at]&192)==128) at++;
    return at;
}
static int utf8_previous(int at)
{
    if(!at) return 0;
    at--;
    while(at&&((u8)source[at]&192)==128) at--;
    return at;
}
static int line_start(int at) { while(at&&source[at-1]!='\n') at--;
    return at; }
static int line_end(int at) { while(at<used&&source[at]!='\n') at++;
    return at; }
static int column(int at)
{ int n=0;
    for(int i=line_start(at);i<at;i=utf8_next(i)) n+=(u8)source[i]>=128?2:1;
    return n; }
static int line_number(int at)
{ int n=0;
    for(int i=0;i<at;i++) if(source[i]=='\n') n++;
    return n; }
static int line_at(int n)
{ int at=0;
    while(n&&at<used) {if(source[at++]=='\n') n--;}return at; }
static int move_column(int at,int col)
{ while(col>0&&at<used&&source[at]!='\n') {col-=(u8)source[at]>=128?2:1; at=utf8_next(at);}
    return at; }
static int is_asm(void)
{
    int n=length(filename);
    if(n<4) return 0;
    char *p=filename+n-4;
    return p[0]=='.'&&(p[1]=='a'||p[1]=='A')&&(p[2]=='s'||p[2]=='S')&&(p[3]=='m'||p[3]=='M');
}
static void output_path(void)
{
    copy(output,filename,64);
    int n=length(output),end=n;
    while(end&&output[end-1]!='/'&&output[end-1]!='.') end--;
    if(end&&output[end-1]=='.') output[end-1]=0;
    if(length(output)>55) copy(output,"HOME/BUILD",64);
    append(output,".SCX",64);
}
static void set_status(const char *text) { copy(status,text,sizeof(status)); }
static int open_file(const char *path)
{
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1) {set_status("File not found");
        return 0;}
    if(info[1]>=EDIT_MAX) {set_status("Source exceeds 65535 bytes");
        return 0;}
    /* 整体读取与文本检查都在备用缓冲完成；失败时当前未保存源码
     * 一字节也不动。STAT 后文件可能被其他应用修改，因此还必须
     * 核对实际读取长度，不能把部分内容当成完整的新编辑文档。 */
    int n=sc_read(path,open_scratch,(int)info[1]);
    if(n!=(int)info[1]) {set_status("Read failed; source preserved"); return 0;}
    for(int i=0;i<n;i++) if(!open_scratch[i]) {set_status("Binary file rejected"); return 0;}
    for(int i=0;i<n;i++) source[i]=open_scratch[i];
    source[n]=0;
    used=n;
    cursor=first_line=left_column=dirty=compiled=diagnostic_mode=0;
    copy(filename,path,64);
    output_path();
    set_status("Source opened");
    return 1;
}
static int save_file(void)
{
    int n=sc_write(filename,source,used);
    if(n!=used) {set_status(n==-5?"Core file protected by kernel":"Save failed");
        return 0;}
    dirty=0;
    set_status("Source saved");
    return 1;
}
static void insert(int value)
{
    int n=value==9?4:1;
    if(used+n>=EDIT_MAX) {set_status("Source capacity reached");
        return;}
    for(int i=used;i>=cursor;i--) source[i+n]=source[i];
    for(int i=0;i<n;i++) source[cursor+i]=(char)(value==9?' ':value);
    used+=n;
    cursor+=n;
    dirty=1;
    compiled=0;
}
static void backspace(void)
{
    if(!cursor) return;
    int from=utf8_previous(cursor),n=cursor-from;
    for(int i=cursor;i<=used;i++) source[i-n]=source[i];
    used-=n;
    cursor=from;
    dirty=1;
    compiled=0;
}
static void draw(void)
{
    ui_header("SC STUDIO",filename);
    const int ids[]={1,2,3,4,5,6,7};const char *actions[]={"Open","Save","New","Source","Build","Run","Debug"};
    editor_y=ui_toolbar(ids,actions,7,ui_compact?38:70);
    editor_rows=(UI_H-editor_y-46)/20;if(editor_rows<1)editor_rows=1;
    editor_columns=(UI_W-84)/8;if(editor_columns<8)editor_columns=8;
    ui_panel(16,editor_y,UI_W-32,editor_rows*20+8);
    int line=line_number(cursor),col=column(cursor);
    if(line<first_line) first_line=line;
    if(line>=first_line+editor_rows) first_line=line-editor_rows+1;
    if(col<left_column) left_column=col;
    if(col>=left_column+editor_columns-1) left_column=col-editor_columns+2;
    if(diagnostic_mode) {
        char *p=diagnostic_text;
        for(int row=0;row<editor_rows&&*p;row++) {
            char text[129];
            int n=0;
            while(*p&&*p!='\n') {if(n<128&&n<editor_columns) text[n++]=*p;
                p++;}if(*p) p++;
            text[n]=0;
            ui_text(24,editor_y+6+row*20,text,row==0?PAL_UI_GOLD+7:PAL_UI_MUTED);
        }
    } else {
        for(int row=0;row<editor_rows;row++) {
            int at=line_at(first_line+row);
            if(at>=used&&first_line+row>line_number(used)) break;
            int y=editor_y+5+row*20;
            if(first_line+row==line) ui_rect(67,y-1,UI_W-91,20,PAL_UI_NIGHT+5);
            ui_number(24,y,first_line+row+1,PAL_UI_MUTED);
            at=move_column(at,left_column);
            char text[513]; int n=0,cells=0;
            while(at<used&&source[at]!='\n'&&cells<editor_columns) {
                int next=utf8_next(at),width=(u8)source[at]>=128?2:1;
                if(cells+width>editor_columns) break;
                for(int j=at;j<next&&n<512;j++) text[n++]=source[j]=='\r'?' ':source[j];
                cells+=width; at=next;
            }
            text[n]=0;
            ui_text16(69,y,text,PAL_UI_TEXT);
        }
        ui_rect(69+(col-left_column)*8,editor_y+4+(line-first_line)*20,1,18,PAL_UI_CYAN+7);
    }
    ui_footer(status);
    ui_text(UI_W-88,ui_compact?6:14,dirty?"EDIT":"SAVED",dirty?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    if(edit_menu){ui_panel(menu_x,menu_y,160,220);for(int i=0;i<7;i++)ui_control(i+1,menu_x+8,menu_y+8+i*30,144,actions[i],0);}
}
static void build(void)
{
    if(!save_file()) return;
    output_path();
    if(length(filename)+length(output)+14>127) copy(output,"HOME/BUILD.SCX",64);
    char command[128],log[64];
    copy(log,output,64);
    append(log,".log",64);
    /* 删除旧诊断而非旧成功文件；新编译的退出码决定可运行标记。
     * 删除可能失败时仍读取退出码，不把旧日志中的 OK 当本次成功。 */
    sc_remove(log);
    copy(command,is_asm()?"bin/asm.scx ":"bin/s3c.scx ",128);
    append(command,filename,128);
    append(command," ",128);
    append(command,output,128);
    if(is_asm()) append(command," --ide",128);
    compiler_pid=sc_exec(command);
    compiled=0;
    if(compiler_pid<0) {set_status("Cannot start compiler");
        return;}
    set_status("Compiler running...");
}
static void check_compiler(void)
{
    if(compiler_pid<0) return;
    int state=sc_status(compiler_pid);
    if(state==0x40000000||state==0x40000001) return;
    compiler_pid=-1;
    compiled=state==0;
    char log[64];
    copy(log,output,64);
    append(log,".log",64);
    int n=sc_read(log,diagnostic_text,sizeof(diagnostic_text)-1);
    if(n<0) copy(diagnostic_text,compiled?"Assembler OK / generated SCX":"Compiler failed / check source",sizeof(diagnostic_text));
    else diagnostic_text[n]=0;
    diagnostic_mode=1;
    set_status(compiled?"Build OK: F6 run F7 debug":"Build failed: F4 source");
}
static void execute(int debug)
{
    if(!compiled||dirty) {set_status("Build current source first (F5)");
        return;}
    char command[128];
    copy(command,debug?"apps/debugger.scx ":"",128);
    append(command,output,128);
    if(sc_exec(command)<0) set_status("Cannot start output");
    else set_status(debug?"Debugger started":"Program started");
}
static int confirm_discard(void)
{
    if(!dirty) return 1;
    draw();
    return ui_confirm("Unsaved source","Discard edits and continue?");
}
int main(void)
{
    if(ui_open("SC STUDIO / mio")<0) return 1;
    char args[128];
    sc_args(args,sizeof(args));
    char *p=args,*path=token(&p);
    if(*path) open_file(path);
    else {const char *demo="#include \"SCAPI.H\"\n\nint main(void)\n{\n    int w=sc_open(\"Hello / mio\",306,164);\n    sc_text(w,8,32,\"Made inside SandCore\",PAL_TITLE);\n    wait_escape();\n    return 0;\n}\n";
        copy(source,demo,sizeof(source));
        used=length(source);
        dirty=1;}
    draw();
    ui_present();
    for(;;) {
        check_compiler();
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        int key=sc_key();
        if(ui_action>=1&&ui_action<=7){key=ui_action;edit_menu=0;}
        /* 命中原生文字行后用现有UTF-8边界函数定位，汉字占两列。
         * 不把屏幕像素直接当字节偏移，否则中文注释会被点击拆坏。 */
        if(!edit_menu&&!diagnostic_mode&&(ui_pressed&1)&&ui_hit(69,editor_y+4,UI_W-91,editor_rows*20)){
            int row=(ui_y-editor_y-4)/20,cell=(ui_x-69)/8+left_column;
            cursor=move_column(line_at(first_line+row),cell);}
        if(ui_pressed&2){edit_menu=!edit_menu;menu_x=ui_clamp(ui_x,0,UI_W-160);menu_y=ui_clamp(ui_y,0,UI_H-250);}
        if(key<0) {if(ui_frame_due()) {draw();
                ui_present();}else sc_yield();
            continue;}
        if(key==27) {if(edit_menu)edit_menu=0;else if(confirm_discard()) return 0;}
        else if(key==1) {if(confirm_discard()) {char path[64];
                copy(path,filename,64);
                draw();
                if(ui_edit_path(path,64,"Open source path")) open_file(path);}}
        else if(key==2) save_file();
        else if(key==3) {if(confirm_discard()) {char path[64]="HOME/NEW.C";
                draw();
                if(ui_edit_path(path,64,"New source path")) {copy(filename,path,64);
                    source[0]=0;
                    used=cursor=first_line=left_column=0;
                    dirty=1;
                    compiled=0;}}}
        else if(key==4) diagnostic_mode=0;
        else if(key==5) {if(compiler_pid<0) build();}
        else if(key==6) execute(0);
        else if(key==7) execute(1);
        else if(key==0x88) cursor=0;
        else if(key==0x89) cursor=used;
        else if(!diagnostic_mode) {
            if(key==0x82) cursor=utf8_previous(cursor);
            else if(key==0x83) cursor=utf8_next(cursor);
            else if(key==0x80) {int start=line_start(cursor);
                if(start) cursor=move_column(line_start(start-1),column(cursor));}
            else if(key==0x81) {int end=line_end(cursor);
                if(end<used) cursor=move_column(end+1,column(cursor));}
            else if(key==8) backspace();
            else if(key==10||key==9||(key>=32&&key<127)) insert(key);
        }
        draw();
        ui_present();
    }
}
