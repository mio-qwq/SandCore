/* =====================================================================
 * mio：M8 Notes，普通三环文本编辑器；公开行为先见docs/NOTES.md。
 *
 * 原来的F2保存/F3重载/Esc与缺失初始文件的空文档继续保留。
 * 文档/备用缓冲/行索引/ARGB帧都在本任务用户页，内核不包含编辑
 * 业务。65535B正文与终止符分开；失败不把内存作品或路径截断。
 * 任意位置编辑与可见行索引沿已实测Studio模型，未收字符仅显示
 * 凤凰缺字框，保存原字节。不加入私有字体、颜色或系统调用号。
 *
 * 本文件已接入M8第一阶段源码；用户批准第二阶段后再进行
 * 真正历史G2生成/运行与完整矩阵，当前不宣称原生验收通过。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#define EDIT_MAX 65536
static char source[EDIT_MAX],open_scratch[EDIT_MAX],filename[64]="HOME/notes.txt";
static char status[96]="F2 save / F3 reload";
static int used,cursor,first_line,left_column,dirty,follow_cursor=1;
static int editor_y,editor_rows,editor_columns,editor_bottom,editor_right,small_window,scroll_drag;
static int edit_menu,menu_x,menu_y,menu_columns,menu_width,menu_height,menu_step,menu_button;
/* 行号与文本字节不可混用。最坏65535个LF仍有65536个起点，
 * 包括末尾空行；打开一次建表，插入/删除只平移后续偏移。
 * 大文档每帧只遍历可见行，内核BSS及已发布ABI都不扩张。 */
static u32 line_starts[EDIT_MAX];
static int line_count=1;
static volatile int notes_operations;
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
static void changed(void){follow_cursor=1;}
static void set_status(const char *text){copy(status,text,sizeof(status));}
static int open_file(const char *path)
{
    if(length(path)>=64){set_status("Path exceeds 63 bytes");return 0;}
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1){set_status("File not found; text preserved");return 0;}
    if(info[1]>=EDIT_MAX){set_status("Text exceeds 65535 bytes");return 0;}
    /* 先在独立备用区完成全部验证，再提交正文和身份。用户正在
     * 编辑的source不能直接作为FSREAD目标：短读、二进制与
     * 错误路径都不能把一份尚未保存的作品变成半份新文件。 */
    int n=sc_read(path,open_scratch,(int)info[1]);
    if(n!=(int)info[1]){set_status("Read failed; text preserved");return 0;}
    for(int i=0;i<n;i++)if(!open_scratch[i]){set_status("Binary file rejected");return 0;}
    for(int i=0;i<n;i++)source[i]=open_scratch[i];
    source[n]=0;used=n;index_document();
    cursor=first_line=left_column=dirty=0;follow_cursor=1;
    copy(filename,path,sizeof(filename));set_status("Text loaded");return 1;
}
static int save_file(void)
{
    /* 空文档同样是合法文件。只写正文、不写终止符，只有实际
     * 返回完整字节数才清脏；内核的SYS/CORE保护必须如实提示。
     * 保存失败不清空文本，也不改变当前文件身份或光标位置。 */
    int n=sc_write(filename,source,used);
    if(n!=used){set_status(n==-5?"Core file protected by kernel":"Save failed; text preserved");return 0;}
    dirty=0;set_status("Text saved");return 1;
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
    menu_columns=5*menu_step+16>UI_H-16?2:1;
    menu_button=menu_columns==2?118:164;
    menu_width=menu_columns*(menu_button+8)+8;
    menu_height=((5+menu_columns-1)/menu_columns)*menu_step+16;
    menu_x=ui_clamp(menu_x,8,UI_W-menu_width-8);
    menu_y=ui_clamp(menu_y,8,UI_H-menu_height-8);
}
static int scroll_first(void){return first_line;}
static int scroll_count(void){return line_count;}
static void scroll_to(int n)
{
    int maximum=line_count-editor_rows;if(maximum<0)maximum=0;
    first_line=ui_clamp(n,0,maximum);follow_cursor=0;
    /* 手动浏览不改变插入位置，下一帧不能立刻跟随旧光标把
     * 视口拉回。真正键入/方向/鼠标定位才再次开启跟随。 */
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
    const int ids[]={1,2,3,4,5};
    const char *actions[]={"Open","Save","Reload","New","Save as"};
    int pressed=ui_pressed;if(edit_menu)ui_pressed=0;
    ui_header("NOTES",filename);editor_y=ui_toolbar(ids,actions,5,ui_compact?38:70);
    editor_bottom=UI_H-(ui_compact?22:30)-6;
    editor_rows=(editor_bottom-editor_y-8)/20;if(editor_rows<0)editor_rows=0;
    editor_right=UI_W-40;editor_columns=(editor_right-69)/8;
    if(editor_columns<1)editor_columns=1;
    int line=line_number(cursor),col=column(cursor);
    if(editor_rows&&follow_cursor){
        if(line<first_line)first_line=line;
        if(line>=first_line+editor_rows)first_line=line-editor_rows+1;
        if(col<left_column)left_column=col;
        if(col>=left_column+editor_columns)left_column=col-editor_columns+1;
        follow_cursor=0;
    }
    if(editor_rows){
        ui_panel(16,editor_y,UI_W-32,editor_rows*20+8);
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
        scrollbar();
    }else ui_text(16,editor_y+2,"Enlarge to edit text",PAL_UI_MUTED);
    ui_footer(status);
    if(UI_W>=420)ui_text(UI_W-88,ui_compact?6:14,dirty?"EDIT":"SAVED",dirty?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    ui_pressed=pressed;
    if(edit_menu){
        menu_geometry();ui_panel(menu_x,menu_y,menu_width,menu_height);
        for(int i=0;i<5;i++){
            int x=menu_x+8+(i%menu_columns)*(menu_button+8),y=menu_y+8+(i/menu_columns)*menu_step;
            if(ui_compact)ui_small_control(i+1,x,y,menu_button,actions[i],0);
            else ui_control(i+1,x,y,menu_button,actions[i],0);
        }
    }
}
static int confirm_discard(void)
{if(!dirty)return 1;draw();return ui_confirm("Unsaved text","Discard edits and continue?");}
static void path_dialog(int create)
{
    if(!confirm_discard()){notes_operations++;return;}
    char candidate[128];copy(candidate,create?"HOME/NEW.TXT":filename,sizeof(candidate));draw();
    if(!ui_edit_path(candidate,sizeof(candidate),create?"New text path":"Open text path"))set_status("Cancelled");
    else if(length(candidate)>=64)set_status("Path exceeds 63 bytes");
    else if(create){
        copy(filename,candidate,sizeof(filename));source[0]=0;
        used=cursor=first_line=left_column=0;index_document();dirty=1;follow_cursor=1;
        set_status("New text / F2 creates file");
    }else open_file(candidate);
    notes_operations++;
}
static void save_as(void)
{
    /* Save as不丢文档，因而不先询问舍弃。候选身份与活动身份
     * 分开；完整写成功才改名，失败或取消始终留在原作品上。
     * 128B候选让超长可以明确拒绝，不截断到63B误写其它文件。 */
    char candidate[128];copy(candidate,filename,sizeof(candidate));draw();
    if(!ui_edit_path(candidate,sizeof(candidate),"Save text as"))set_status("Cancelled");
    else if(length(candidate)>=64)set_status("Path exceeds 63 bytes");
    else{
        int n=sc_write(candidate,source,used);
        if(n!=used)set_status(n==-5?"Core file protected by kernel":"Save as failed; text preserved");
        else{copy(filename,candidate,sizeof(filename));dirty=0;set_status("Text saved as new path");}
    }
    notes_operations++;
}
int main(void)
{
    if(ui_open("Notes / mio")<0)return 1;
    char args[128];sc_args(args,sizeof(args));char *p=args,*path=token(&p);
    const char *initial=*path?path:filename;
    /* 初始缺失路径沿M6的空文档模型，开窗不会自动写盘。
     * 但目录/二进制/超限读取失败不是“新文件”；即使启动失败
     * 也保留明确错误文案，不能用空白界面假称已读入原文件。 */
    if(length(initial)>=64)set_status("Path exceeds 63 bytes");
    else{
        u32 info[2];int missing=sc_stat(initial,info);
        if(missing){copy(filename,initial,sizeof(filename));index_document();set_status("New file / F2 creates file");}
        else open_file(initial);
    }
    ui_pointer();draw();ui_present();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();int was_menu=edit_menu;draw();ui_present();
        /* 动作先确定去向，路径框才消费真实键队首。浮层外部
         * 点只撤菜单，不能同时保存、重载或移动底下的光标。 */
        int action=ui_action,key=(action||(was_menu&&(ui_pressed&1)))?-1:sc_key();
        if(action==98){sc_window(ui_win,1);continue;}
        if(was_menu&&(ui_pressed&1)&&!action){edit_menu=scroll_drag=0;ui_followup=1;continue;}
        if(action>=1&&action<=5){key=action;edit_menu=0;}
        if(action==20)scroll_to(first_line-1);
        if(action==21)scroll_to(first_line+1);
        if(!was_menu&&!action&&(ui_pressed&1)&&editor_rows&&ui_hit(69,editor_y+4,editor_right-69,editor_rows*20)){
            int row=(ui_y-editor_y-4)/20,cell=(ui_x-69)/8+left_column;
            int line=first_line+row;if(line>=line_count)line=line_count-1;
            cursor=move_column(line_at(line),cell);follow_cursor=1;
        }
        if(!small_window&&(ui_pressed&2)){edit_menu=!edit_menu;menu_x=ui_x;menu_y=ui_y;scroll_drag=0;}
        if(key==27){if(edit_menu)edit_menu=0;else if(confirm_discard())return 0;}
        else if(key==1)path_dialog(0);
        else if(key==2){save_file();notes_operations++;}
        else if(key==3){open_file(filename);notes_operations++;}
        else if(key==4)path_dialog(1);
        else if(key==5)save_as();
        else if(key==0x88){cursor=0;follow_cursor=1;}
        else if(key==0x89){cursor=used;follow_cursor=1;}
        else if(key==0x82){cursor=utf8_previous(cursor);follow_cursor=1;}
        else if(key==0x83){cursor=utf8_next(cursor);follow_cursor=1;}
        else if(key==0x80){int line=line_number(cursor);if(line)cursor=move_column(line_at(line-1),column(cursor));follow_cursor=1;}
        else if(key==0x81){int line=line_number(cursor);if(line+1<line_count)cursor=move_column(line_at(line+1),column(cursor));follow_cursor=1;}
        else if(key==8)backspace();
        else if(key==10||key==9||(key>=32&&key<127))insert(key);
        draw();ui_present();
    }
}
