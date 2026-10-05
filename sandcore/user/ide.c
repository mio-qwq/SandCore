/* =====================================================================
 * mio：SC STUDIO，M8原生图形编辑/编译草稿。合同见docs/STUDIO.md。
 *
 * 编辑文档、行索引和绘制缓冲全部是三环私有数据。内核只提供已有
 * 文件/窗口/进程接口，仍由真正SCCC/SandAsm生成SCX。保留F1至F9、
 * UTF-8光标和历史65535B上限，不通过改变旧ABI来换取新版界面。
 *
 * 现已接入M8第一阶段正式源码；用户批准第二阶段后统一构建和
 * 原生联测。此前独立草稿的证据不自动覆盖当前整合版本。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#define EDIT_MAX 65536
static char source[EDIT_MAX],open_scratch[EDIT_MAX];
static char filename[64]="HOME/HELLO.C",output[64]="HOME/HELLO.SCX";
static char diagnostic_text[4096],status[64]="F1 open / F3 new";
static int used,cursor,first_line,left_column,dirty,compiled,compiler_pid=-1,diagnostic_mode;
static int editor_y,editor_rows,editor_columns,editor_bottom,editor_right,small_window;
static int edit_menu,menu_x,menu_y,menu_columns,menu_width,menu_height,menu_step,menu_button;
static int follow_cursor=1,diagnostic_first,diagnostic_lines=1,scroll_drag;
/* 65535个换行最多产生65536行，末尾空行也要有起点。索引放用户
 * BSS，不扩大内核。每帧从文首逐行寻找起点会把大文件绘制变成
 * 多次重复扫描；这里打开时一次建立，编辑时只平移后续行偏移。 */
static u32 line_starts[EDIT_MAX];
static int line_count=1;
static u32 document_revision=1,build_revision,compiler_generation;
static char build_output[64];
static volatile int studio_operations;
static int utf8_next(int at)
{
    if(at>=used)return used;
    at++;
    while(at<used&&((u8)source[at]&192)==128)at++;
    return at;
}
static int utf8_previous(int at)
{
    if(!at)return 0;
    at--;
    while(at&&((u8)source[at]&192)==128)at--;
    return at;
}
static void index_document(void)
{
    line_count=1;line_starts[0]=0;
    for(int i=0;i<used;i++)if(source[i]=='\n')line_starts[line_count++]=i+1;
}
static int line_number(int at)
{
    /* 找到不大于光标的最后一个起点。光标恰在LF后属于下一行，
     * 光标恰在末尾LF之后则属于合法的最后空行，不能向上挪。 */
    int lo=0,hi=line_count;
    while(lo+1<hi){int mid=(lo+hi)/2;if(line_starts[mid]<=(u32)at)lo=mid;else hi=mid;}
    return lo;
}
static int line_at(int n)
{return n>=0&&n<line_count?(int)line_starts[n]:used;}
static int line_start(int at){return line_at(line_number(at));}
static int line_end(int at)
{int n=line_number(at);return n+1<line_count?line_at(n+1)-1:used;}
static int column(int at)
{
    int n=0;
    for(int i=line_start(at);i<at;i=utf8_next(i))n+=(u8)source[i]>=128?2:1;
    return n;
}
static int move_column(int at,int col)
{
    /* 点击宽字第二格也落在整个编码之后；始终不返回续字节位置。 */
    while(col>0&&at<used&&source[at]!='\n'){col-=(u8)source[at]>=128?2:1;at=utf8_next(at);}
    return at;
}
static void changed(void)
{
    document_revision++;if(!document_revision)document_revision=1;
    compiled=0;follow_cursor=1;
}
static void set_status(const char *text){copy(status,text,sizeof(status));}
static int is_asm(void)
{
    int n=length(filename);if(n<4)return 0;
    char *p=filename+n-4;
    return p[0]=='.'&&(p[1]=='a'||p[1]=='A')&&(p[2]=='s'||p[2]=='S')&&(p[3]=='m'||p[3]=='M');
}
static void output_path(void)
{
    copy(output,filename,64);int end=length(output);
    while(end&&output[end-1]!='/'&&output[end-1]!='.')end--;
    if(end&&output[end-1]=='.')output[end-1]=0;
    /* 最长55B词干再加.SCX/.log或.map仍在63B身份内，不能依靠
     * append截断来制造同前缀输出。长身份继续用历史HOME/BUILD。 */
    if(length(output)>55)copy(output,"HOME/BUILD",64);
    append(output,".SCX",64);
}
static int open_file(const char *path)
{
    if(length(path)>=64){set_status("Path exceeds 63 bytes");return 0;}
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1){set_status("File not found");return 0;}
    if(info[1]>=EDIT_MAX){set_status("Source exceeds 65535 bytes");return 0;}
    /* STAT不等于READ已经成功：先读完整备用缓冲并排除嵌入NUL，
     * 最后一起提交身份/源码/光标/索引，错误不毁掉未保存编辑。 */
    int n=sc_read(path,open_scratch,(int)info[1]);
    if(n!=(int)info[1]){set_status("Read failed; source preserved");return 0;}
    for(int i=0;i<n;i++)if(!open_scratch[i]){set_status("Binary file rejected");return 0;}
    for(int i=0;i<n;i++)source[i]=open_scratch[i];
    source[n]=0;used=n;index_document();
    cursor=first_line=left_column=dirty=diagnostic_mode=0;changed();
    copy(filename,path,64);output_path();set_status("Source opened");return 1;
}
static int save_file(void)
{
    int n=sc_write(filename,source,used);
    if(n!=used){set_status(n==-5?"Core file protected by kernel":"Save failed");return 0;}
    dirty=0;set_status("Source saved");return 1;
}
static void insert(int value)
{
    int n=value==9?4:1;
    if(used+n>=EDIT_MAX){set_status("Source capacity reached");return;}
    int line=line_number(cursor);
    for(int i=used;i>=cursor;i--)source[i+n]=source[i];
    for(int i=0;i<n;i++)source[cursor+i]=(char)(value==9?' ':value);
    if(value==10){
        for(int i=line_count;i>line+1;i--)line_starts[i]=line_starts[i-1]+n;
        line_starts[line+1]=cursor+n;line_count++;
    }else for(int i=line+1;i<line_count;i++)line_starts[i]+=n;
    used+=n;cursor+=n;dirty=1;changed();
}
static void backspace(void)
{
    if(!cursor)return;
    int from=utf8_previous(cursor),n=cursor-from,line=line_number(cursor);
    /* 删除LF合并两行，移除的恰是当前行起点；删除整汉字只改
     * 后续偏移。先读被删字节再搬缓冲，不能拿搬完后的字节判断。 */
    if(source[from]=='\n'){
        for(int i=line;i+1<line_count;i++)line_starts[i]=line_starts[i+1]-n;
        line_count--;
    }else for(int i=line+1;i<line_count;i++)line_starts[i]-=n;
    for(int i=cursor;i<=used;i++)source[i-n]=source[i];
    used-=n;cursor=from;dirty=1;changed();
}
static void menu_geometry(void)
{
    menu_step=ui_compact?26:36;
    menu_columns=7*menu_step+16>UI_H-16?2:1;
    menu_button=menu_columns==2?118:164;
    menu_width=menu_columns*(menu_button+8)+8;
    menu_height=((7+menu_columns-1)/menu_columns)*menu_step+16;
    menu_x=ui_clamp(menu_x,8,UI_W-menu_width-8);
    menu_y=ui_clamp(menu_y,8,UI_H-menu_height-8);
}
static int scroll_first(void){return diagnostic_mode?diagnostic_first:first_line;}
static int scroll_count(void){return diagnostic_mode?diagnostic_lines:line_count;}
static void scroll_to(int n)
{
    int maximum=scroll_count()-editor_rows;if(maximum<0)maximum=0;
    n=ui_clamp(n,0,maximum);
    if(diagnostic_mode)diagnostic_first=n;else{first_line=n;follow_cursor=0;}
}
static void scrollbar(void)
{
    int x=editor_right+4,y=editor_y+4,h=editor_rows*20;
    ui_rect_rgb(x,y,16,h,ui_role(SC_THEME_FACE_ALT));
    /* 箭头是现有角色的几何线条，不借新字形破坏用户字库。高度
     * 只有一行时仍有两个8px按钮；轨道不足便不绘负高的滑块。 */
    int button=h<32?8:16;
    for(int r=0;r<4;r++){
        ui_span(x+4-r,y+2+r,1+r*2,PAL_UI_MUTED);
        ui_span(x+1+r,y+h-button+2+r,7-r*2,PAL_UI_MUTED);
    }
    if((ui_pressed&1)&&ui_hit(x,y,16,button))ui_action=20;
    if((ui_pressed&1)&&ui_hit(x,y+h-button,16,button))ui_action=21;
    int track=h-2*button,total=scroll_count(),visible=editor_rows;
    if(track>0){
        int maximum=total-visible;if(maximum<0)maximum=0;
        int thumb=total>visible?track*visible/total:track;
        if(thumb<8)thumb=track<8?track:8;
        int offset=maximum?(track-thumb)*scroll_first()/maximum:0;
        ui_rect_rgb(x+2,y+button+offset,12,thumb,ui_role(SC_THEME_LINE));
        if((ui_pressed&1)&&ui_hit(x,y+button,16,track))scroll_drag=1;
        if(scroll_drag&&ui_focus&&(ui_buttons&1)){
            int position=ui_clamp(ui_y-y-button-thumb/2,0,track-thumb);
            scroll_to(track>thumb?position*maximum/(track-thumb):0);
        }
    }
    if(!(ui_buttons&1)||!ui_focus)scroll_drag=0;
}
static void draw(void)
{
    small_window=UI_W<276||UI_H<160;editor_rows=0;
    if(small_window){
        edit_menu=scroll_drag=0;ui_background(SC_THEME_FACE_ALT);
        int h=UI_H<22?UI_H:22;
        ui_button_box(4,(UI_H-h)/2,UI_W-8,h,UI_W>=104?"Enlarge":"+",0);
        if((ui_pressed&1)&&ui_hit(4,(UI_H-h)/2,UI_W-8,h))ui_action=98;
        return;
    }
    const int ids[]={1,2,3,4,5,6,7};
    const char *actions[]={"Open","Save","New","Source","Build","Run","Debug"};
    int pressed=ui_pressed;if(edit_menu)ui_pressed=0;
    ui_header("SC STUDIO",filename);
    editor_y=ui_toolbar(ids,actions,7,ui_compact?38:70);
    editor_bottom=UI_H-(ui_compact?22:30)-6;
    editor_rows=(editor_bottom-editor_y-8)/20;
    if(editor_rows<0)editor_rows=0;
    editor_right=UI_W-40;editor_columns=(editor_right-69)/8;
    if(editor_columns<1)editor_columns=1;
    int line=line_number(cursor),col=column(cursor);
    if(editor_rows&&follow_cursor&&!diagnostic_mode){
        if(line<first_line)first_line=line;
        if(line>=first_line+editor_rows)first_line=line-editor_rows+1;
        if(col<left_column)left_column=col;
        if(col>=left_column+editor_columns)left_column=col-editor_columns+1;
        follow_cursor=0;
    }
    if(editor_rows){
        ui_panel(16,editor_y,UI_W-32,editor_rows*20+8);
        if(diagnostic_mode){
            char *p=diagnostic_text;int skipped=0;
            while(*p&&skipped<diagnostic_first)if(*p++=='\n')skipped++;
            for(int row=0;row<editor_rows&&*p;row++){
                char text[513];int n=0,clipped=0;
                /* 日志原字节仍存完整4095B，仅当前显示行按完整UTF-8
                 * 裁剪。先复制完整行前缀，再让NUI统一画规定字形。 */
                while(*p&&*p!='\n'){
                    if(!clipped){
                        int bytes=1;
                        if((u8)*p>=0xC2&&(u8)*p<=0xF4)bytes=(u8)*p<0xE0?2:(u8)*p<0xF0?3:4;
                        /* 保存前检查整标量与整行：512B前缀不足时
                         * 丢弃的是显示尾部，不修改原诊断或拆宽字。 */
                        int complete=n+bytes<=512;
                        for(int j=1;j<bytes;j++)if(!p[j]||p[j]=='\n'||((u8)p[j]&192)!=128){complete=0;break;}
                        if(complete){for(int j=0;j<bytes;j++)text[n++]=p[j];p+=bytes;continue;}
                        clipped=1;
                    }
                    p++;
                }
                if(*p)p++;
                text[n]=0;
                ui_clip_set(24,editor_y+4,editor_right-24,editor_rows*20);
                ui_text16(24,editor_y+5+row*20,text,PAL_UI_MUTED);ui_clip_clear();
            }
        }else{
            for(int row=0;row<editor_rows&&first_line+row<line_count;row++){
                int at=line_at(first_line+row),y=editor_y+5+row*20;
                int selected=first_line+row==line;
                if(selected)ui_rect_rgb(67,y-1,editor_right-67,20,ui_role(SC_THEME_SELECT));
                ui_number(24,y,first_line+row+1,PAL_UI_MUTED);
                /* 横向视口可能从宽字第二格开始。跳过整个编码后保留
                 * 一格空白，不能把后面的字向左压一格导致光标错位。 */
                int skipped=0;
                while(at<used&&source[at]!='\n'&&skipped<left_column){skipped+=(u8)source[at]>=128?2:1;at=utf8_next(at);}
                int padding=skipped-left_column;if(padding<0)padding=0;
                char text[513];int n=0,cells=padding;
                while(at<used&&source[at]!='\n'&&cells<editor_columns){
                    int next=utf8_next(at),width=(u8)source[at]>=128?2:1;
                    if(cells+width>editor_columns||n+next-at>512)break;
                    for(int j=at;j<next;j++)text[n++]=source[j]=='\r'?' ':source[j];
                    cells+=width;at=next;
                }
                text[n]=0;ui_selected_text(69+padding*8,y,text,selected);
            }
            if(line>=first_line&&line<first_line+editor_rows&&col>=left_column&&col<left_column+editor_columns)
                ui_rect_rgb(69+(col-left_column)*8,editor_y+4+(line-first_line)*20,1,18,ui_role(SC_THEME_ACCENT));
        }
        scrollbar();
    }else ui_text(16,editor_y+2,"Enlarge to edit source",PAL_UI_MUTED);
    ui_footer(status);
    if(UI_W>=420)ui_text(UI_W-88,ui_compact?6:14,dirty?"EDIT":"SAVED",dirty?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    ui_pressed=pressed;
    if(edit_menu){
        menu_geometry();ui_panel(menu_x,menu_y,menu_width,menu_height);
        for(int i=0;i<7;i++){
            int x=menu_x+8+(i%menu_columns)*(menu_button+8),y=menu_y+8+(i/menu_columns)*menu_step;
            if(ui_compact)ui_small_control(i+1,x,y,menu_button,actions[i],0);
            else ui_control(i+1,x,y,menu_button,actions[i],0);
        }
    }
}
static void build(void)
{
    if(!save_file()){studio_operations++;return;}
    output_path();int assembly=is_asm();
    if(length(filename)+length(output)+12+(assembly?6:0)>127)copy(output,"HOME/BUILD.SCX",64);
    char command[128],log[64];copy(log,output,64);append(log,".log",64);
    /* 删的是旧日志，不是旧成功SCX；能否运行仅由本次真实退出码和
     * 文档版本决定。不能用残留的OK文字认定本次编译成功。 */
    sc_remove(log);copy(command,assembly?"bin/asm.scx ":"bin/s3c.scx ",128);
    append(command,filename,128);append(command," ",128);append(command,output,128);
    if(assembly)append(command," --ide",128);
    u32 cpu[SC_CPU_WORDS];
    if(sc_cpu(cpu)){compiled=0;set_status("Cannot sample compiler identity");studio_operations++;return;}
    compiler_pid=sc_exec(command);compiled=0;
    if(compiler_pid<0){set_status("Cannot start compiler");studio_operations++;return;}
    /* 在EXEC之前取各槽代数，返回PID的下一代即本次编译器身份。
     * 若先EXEC后采样，极短编译器可能已退出并被别的程序复用。
     * 代数回绕跳0与kernel/task.c一致，旧STATUS布局完全不改。 */
    compiler_generation=cpu[16+compiler_pid*6+2]+1;
    if(!compiler_generation)compiler_generation=1;
    build_revision=document_revision;copy(build_output,output,64);
    set_status("Compiler running...");studio_operations++;
}
static void check_compiler(void)
{
    if(compiler_pid<0)return;
    u32 cpu[SC_CPU_WORDS];
    if(sc_cpu(cpu)||cpu[16+compiler_pid*6+2]!=compiler_generation){
        compiler_pid=-1;compiled=0;set_status("Compiler identity lost / build again");studio_operations++;return;
    }
    int state=sc_status(compiler_pid);
    /* CPUINFO和STATUS是两个既有调用，之间可能轮转并复用PID。
     * 退出码读取后再次核对代数，防止把另一任务的0误当成功。
     * 它退出后也可再复用，但本次码已在两次同代数中稳定取得。 */
    if(sc_cpu(cpu)||cpu[16+compiler_pid*6+2]!=compiler_generation){
        compiler_pid=-1;compiled=0;set_status("Compiler identity lost / build again");studio_operations++;return;
    }
    if(state==0x40000000||state==0x40000001)return;
    compiler_pid=-1;compiled=state==0&&build_revision==document_revision;
    char log[64];copy(log,build_output,64);append(log,".log",64);
    int n=sc_read(log,diagnostic_text,sizeof(diagnostic_text)-1);
    if(n<0)copy(diagnostic_text,state==0?"Compiler OK / generated SCX":"Compiler failed / check source",sizeof(diagnostic_text));
    else diagnostic_text[n]=0;
    diagnostic_lines=1;for(int i=0;diagnostic_text[i];i++)if(diagnostic_text[i]=='\n')diagnostic_lines++;
    diagnostic_first=0;
    /* dirty可能在编译中再次保存后归零，因此不能作为源码版本。
     * 变化的文档继续留在编辑界面，旧编译结束不抢走当前编辑光标。 */
    if(build_revision!=document_revision)set_status("Source changed / build again");
    else{diagnostic_mode=1;set_status(compiled?"Build OK: F6 run F7 debug":"Build failed: F4 source");}
    ui_followup=1;studio_operations++;
}
static void execute(int debug)
{
    if(!compiled||dirty){set_status("Build current source first (F5)");studio_operations++;return;}
    char command[128];copy(command,debug?"apps/debugger.scx ":"",128);append(command,output,128);
    if(sc_exec(command)<0)set_status("Cannot start output");else set_status(debug?"Debugger started":"Program started");
    studio_operations++;
}
static int confirm_discard(void)
{if(!dirty)return 1;draw();return ui_confirm("Unsaved source","Discard edits and continue?");}
static void path_dialog(int create)
{
    if(!confirm_discard()){studio_operations++;return;}
    char candidate[128];copy(candidate,create?"HOME/NEW.C":filename,sizeof(candidate));draw();
    if(!ui_edit_path(candidate,sizeof(candidate),create?"New source path":"Open source path"))set_status("Cancelled");
    else if(length(candidate)>=64)set_status("Path exceeds 63 bytes");
    else if(create){
        copy(filename,candidate,64);source[0]=0;used=cursor=first_line=left_column=diagnostic_mode=0;
        index_document();dirty=1;changed();output_path();set_status("New source");
    }else open_file(candidate);
    studio_operations++;
}
int main(void)
{
    if(ui_open("SC STUDIO / mio")<0)return 1;
    char args[128];sc_args(args,sizeof(args));char *p=args,*path=token(&p);
    if(*path)open_file(path);
    else{
        const char *demo="#include \"SCAPI.H\"\n\nint main(void)\n{\n    int w=sc_open(\"Hello / mio\",306,164);\n    sc_text(w,8,32,\"Made inside SandCore\",PAL_TITLE);\n    wait_escape();\n    return 0;\n}\n";
        copy(source,demo,sizeof(source));used=length(source);dirty=1;index_document();
    }
    ui_pointer();draw();ui_present();
    for(;;){
        check_compiler();if(!ui_frame_due())continue;
        ui_pointer();int was_menu=edit_menu;draw();ui_present();
        /* 先处理点击的去向，模态才有机会消费首个排队字符。菜单外
         * 点击只撤浮层，不能同次保存、启动编译或移动底下的光标。 */
        int action=ui_action,key=(action||(was_menu&&(ui_pressed&1)))?-1:sc_key();
        if(action==98){sc_window(ui_win,1);continue;}
        if(was_menu&&(ui_pressed&1)&&!action){edit_menu=0;scroll_drag=0;ui_followup=1;continue;}
        if(action>=1&&action<=7){key=action;edit_menu=0;}
        if(action==20)scroll_to(scroll_first()-1);
        if(action==21)scroll_to(scroll_first()+1);
        if(!was_menu&&!action&&!diagnostic_mode&&(ui_pressed&1)&&editor_rows&&ui_hit(69,editor_y+4,editor_right-69,editor_rows*20)){
            int row=(ui_y-editor_y-4)/20,cell=(ui_x-69)/8+left_column;
            int line=first_line+row;if(line>=line_count)line=line_count-1;
            cursor=move_column(line_at(line),cell);follow_cursor=1;
        }
        if(!small_window&&(ui_pressed&2)){edit_menu=!edit_menu;menu_x=ui_x;menu_y=ui_y;scroll_drag=0;}
        if(key==27){if(edit_menu)edit_menu=0;else if(confirm_discard())return 0;}
        else if(key==1)path_dialog(0);
        else if(key==2){save_file();studio_operations++;}
        else if(key==3)path_dialog(1);
        else if(key==4){diagnostic_mode=0;follow_cursor=1;}
        else if(key==5){if(compiler_pid<0)build();else set_status("Compiler still running");}
        else if(key==6)execute(0);
        else if(key==7)execute(1);
        else if(key==0x88){cursor=0;follow_cursor=1;}
        else if(key==0x89){cursor=used;follow_cursor=1;}
        else if(!diagnostic_mode&&key>=0){
            if(key==0x82){cursor=utf8_previous(cursor);follow_cursor=1;}
            else if(key==0x83){cursor=utf8_next(cursor);follow_cursor=1;}
            else if(key==0x80){int line=line_number(cursor);if(line)cursor=move_column(line_at(line-1),column(cursor));follow_cursor=1;}
            else if(key==0x81){int line=line_number(cursor);if(line+1<line_count)cursor=move_column(line_at(line+1),column(cursor));follow_cursor=1;}
            else if(key==8)backspace();
            else if(key==10||key==9||(key>=32&&key<127))insert(key);
        }else if(diagnostic_mode&&key==0x80)scroll_to(diagnostic_first-1);
        else if(diagnostic_mode&&key==0x81)scroll_to(diagnostic_first+1);
        draw();ui_present();
    }
}
