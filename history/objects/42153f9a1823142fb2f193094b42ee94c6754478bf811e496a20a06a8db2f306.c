/* =====================================================================
 * mio：M8 Files。SandFS按完整路径平铺，FSDIR投影为当前目录的直接
 * 子项；显示名称可以省略，执行、改名、删除始终使用独立真实名字。
 *
 * 布局的边界也是输入的边界：工具条先换行，分页区先预留，最后才
 * 算列表行数。空间不足不强行塞一行盖住按钮；最小窗口给真实放大
 * 入口。浮层开启时暂时屏蔽背景的按下沿，关闭浮层的点击不能同时
 * 执行底下文件。所有保护依然由内核检查，界面只准确解释失败。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
static char listing[65536],names[512][64],sizes[512][12],cwd[64],status[64];
static int kinds[512],count,selection;
static int file_context,context_x,context_y,context_columns,context_width,context_height;
static int context_step,context_button_width,last_click=-100,clicked=-1;
static int body_y,list_left,list_y,row_height,navigation_y,visible_rows,small_window;
/* mio：验收只读这个私有序号，区分模态已关闭与文件操作真正返回；
 * 不能把对话框消失当作写盘/读取完成。它不是成功码或公共ABI。 */
static volatile int file_operations;
static int upper(int c){return c>='a'&&c<='z'?c-'a'+'A':c;}
static int extension(const char *path,const char *ext)
{
    int n=length(path),m=length(ext);
    if(n<m)return 0;
    for(int i=0;i<m;i++)if(upper(path[n-m+i])!=upper(ext[i]))return 0;
    return 1;
}
/* mio：append会按容量截断，适合文案但不适合文件身份。先验证完整
 * 拼接长度，失败完全不改目标，避免63B边界执行另一个同前缀文件。 */
static int path_join(char *out,const char *directory,const char *name)
{
    int a=length(directory),b=length(name);
    if(a+b+(a?1:0)>=64){copy(status,"Path exceeds 63 bytes",sizeof(status));return 0;}
    copy(out,directory,64);
    if(a)append(out,"/",64);
    append(out,name,64);
    return 1;
}
static void result(int value,const char *success)
{
    if(value>=0)copy(status,success,sizeof(status));
    else if(value==-5)copy(status,"Core path protected by kernel",sizeof(status));
    else{char n[12];decimal(n,value);copy(status,"Operation failed: ",sizeof(status));append(status,n,sizeof(status));}
}
static void refresh(void)
{
    count=0;clicked=-1;last_click=-100;
    int n=sc_dir(cwd,listing,sizeof(listing));
    if(n<0){result(n,"");selection=0;return;}
    char *p=listing;
    while(*p&&count<512){
        int kind=*p++=='D'?2:1;
        if(*p!=' ')break;
        p++;char *name=p;
        while(*p&&*p!=' ')p++;
        if(!*p)break;
        *p++=0;char *size=p;
        while(*p&&*p!='\n')p++;
        if(*p)*p++=0;
        /* 内核合同保证名称上限；仍不把坏行截断成有效文件身份。 */
        if(length(name)>=64||length(size)>=12){copy(status,"Invalid directory entry",sizeof(status));break;}
        copy(names[count],name,64);copy(sizes[count],size,12);kinds[count++]=kind;
    }
    if(selection>=count)selection=count?count-1:0;
}
static void context_geometry(void)
{
    context_step=ui_compact?26:36;
    context_columns=7*context_step+16>UI_H-38?2:1;
    context_button_width=context_columns==2?118:164;
    context_width=context_columns*(context_button_width+8)+8;
    context_height=((7+context_columns-1)/context_columns)*context_step+16;
    /* 菜单可遮住列表，不能画到客户区外或让页脚盖掉最后的Delete。
     * 极小窗口在draw前已切到放大入口，因此这里至少有276×168。 */
    context_x=ui_clamp(context_x,8,UI_W-context_width-8);
    context_y=ui_clamp(context_y,8,UI_H-context_height-8);
}
static void draw(void)
{
    small_window=UI_W<276||UI_H<168;
    if(small_window){
        file_context=0;visible_rows=0;
        ui_background(SC_THEME_FACE_ALT);
        int h=UI_H<22?UI_H:22;
        ui_button_box(4,(UI_H-h)/2,UI_W-8,h,UI_W>=104?"Enlarge":"+",0);
        if((ui_pressed&1)&&ui_hit(4,(UI_H-h)/2,UI_W-8,h))ui_action=98;
        return;
    }
    const int ids[]={1,2,3,4,5,6,7};
    const char *actions[]={"Open","Edit","Debug","Up","New dir","Rename","Delete"};
    /* mio：ui_toolbar内部也有命中处理。只屏蔽列表而不屏蔽工具条，
     * 会让关闭菜单的一次点击先执行背景Delete，因此背景共用同一个
     * 零按下沿，绘完后才为浮层恢复本帧真实事件；不修改内核队列。 */
    int pressed=ui_pressed;
    if(file_context)ui_pressed=0;
    ui_header("FILES",cwd[0]?cwd:"ROOT / SandFS v4");
    body_y=ui_toolbar(ids,actions,7,ui_compact?38:70);
    row_height=ui_compact?18:28;
    /* 紧凑页脚22单位高，分页22高并留2单位间隔；默认640/200%
     * 的175高客户区还可容纳一条18高文件行，不能再多扣2行空间。 */
    navigation_y=UI_H-(ui_compact?46:68);
    int sidebar=UI_W>=600&&UI_H>=340?144:0;
    list_left=16+sidebar;list_y=body_y+4;
    visible_rows=(navigation_y-8-list_y)/row_height;
    if(visible_rows<0)visible_rows=0;
    int panel_h=visible_rows*row_height+8;
    if(sidebar){
        ui_panel(16,body_y,128,navigation_y-body_y-8);
        const char *tabs[]={"ROOT","HOME","SYS","APPS","BIN"};
        for(int i=0;i<5;i++)ui_control(100+i,24,body_y+12+i*36,112,tabs[i],0);
    }
    if(visible_rows)ui_panel(list_left,body_y,UI_W-list_left-16,panel_h);
    int first=visible_rows?selection/visible_rows*visible_rows:0;
    for(int row=0;row<visible_rows&&first+row<count;row++){
        int i=first+row,y=list_y+row*row_height,icon_y=y+(row_height-14)/2;
        if(i==selection){ui_rect(list_left+4,y,UI_W-list_left-24,row_height,PAL_UI_NIGHT+6);ui_rect(list_left+4,y,2,row_height,PAL_UI_CYAN+7);}
        /* 文件夹的凸出页签和文档的折角让轮廓可辨，不画纯方形色块。
         * 色彩来自主题角色映射的既有槽位，不向用户中文字库补符号。 */
        if(kinds[i]==2){ui_rect(list_left+14,icon_y+4,14,10,PAL_UI_GOLD+5);ui_rect(list_left+14,icon_y+1,6,3,PAL_UI_GOLD+7);}
        else{ui_rect(list_left+16,icon_y,10,14,PAL_UI_MUTED);ui_rect(list_left+23,icon_y,3,3,PAL_UI_PANEL);ui_span(list_left+18,icon_y+5,6,PAL_UI_PANEL);}
        int show_size=kinds[i]!=2&&UI_W-list_left>330;
        int label_end=show_size?UI_W-120:UI_W-24;
        char label[64];ui_copy_utf8(label,names[i],ui_clamp((label_end-list_left-40)/8+1,2,64));
        if(i==selection)ui_selected_text(list_left+40,y+(row_height-16)/2,label,1);
        else ui_text(list_left+40,y+(row_height-16)/2,label,PAL_UI_MUTED);
        if(show_size){
            if(i==selection)ui_selected_text(UI_W-104,y+(row_height-16)/2,sizes[i],1);
            else ui_text(UI_W-104,y+(row_height-16)/2,sizes[i],PAL_UI_MUTED);
        }
        if(!file_context&&ui_hit(list_left+4,y,UI_W-list_left-24,row_height)){
            if(ui_pressed&2){selection=i;file_context=1;context_x=ui_x;context_y=ui_y;clicked=-1;}
            if(ui_pressed&1){selection=i;if(clicked==i&&sc_tick()-last_click<35)ui_action=1;clicked=i;last_click=sc_tick();}
        }
    }
    if(!count&&visible_rows)ui_text(list_left+24,list_y,"Empty directory",PAL_UI_MUTED);
    if(!visible_rows)ui_text(16,list_y,"Enlarge to show files",PAL_UI_MUTED);
    ui_small_control(8,16,navigation_y,72,"Prev",0);ui_small_control(9,96,navigation_y,72,"Next",0);
    if(!sidebar)ui_small_control(10,176,navigation_y,72,"Path",0);
    ui_footer(status[0]?status:"ENTER open N mkdir F2 rename X delete");
    ui_pressed=pressed;
    if(file_context){
        context_geometry();ui_panel(context_x,context_y,context_width,context_height);
        for(int i=0;i<7;i++){
            int x=context_x+8+(i%context_columns)*(context_button_width+8),y=context_y+8+(i/context_columns)*context_step;
            if(ui_compact)ui_small_control(i+1,x,y,context_button_width,actions[i],0);
            else ui_control(i+1,x,y,context_button_width,actions[i],0);
        }
    }
}
static int selected_path(char *out)
{
    if(count)return path_join(out,cwd,names[selection]);
    copy(out,cwd,64);return 1;
}
static void open_selected(int edit,int debug)
{
    if(!count)return;
    char path[64],command[128];
    if(!selected_path(path))return;
    if(kinds[selection]==2){copy(cwd,path,64);selection=0;refresh();return;}
    if(extension(path,".SCX")&&!edit){
        if(debug){copy(command,"APPS/DEBUGGER.SCX ",128);append(command,path,128);}else copy(command,path,128);
    }else if(extension(path,".SCB")||extension(path,".BMP")){
        copy(command,"APPS/LENS.SCX ",128);append(command,path,128);
    }else{
        /* 只分派已实现的格式，未知二进制不能送编辑器再误保存损坏。
         * 压缩图片在真实解码器验收之后才登记，不凭扩展名假称支持。 */
        if(!extension(path,".C")&&!extension(path,".H")&&!extension(path,".INC")&&!extension(path,".ASM")
            &&!extension(path,".TXT")&&!extension(path,".CFG")&&!extension(path,".LOG")){
            copy(status,"Binary file; use an application",sizeof(status));return;
        }
        copy(command,"APPS/IDE.SCX ",128);append(command,path,128);
    }
    result(sc_exec(command),"Application started");file_operations++;
}
static void parent(void)
{
    int n=length(cwd);
    while(n&&cwd[n-1]!='/')n--;
    if(n)n--;
    cwd[n]=0;selection=0;refresh();
}
static void mutation(int operation)
{
    char path[64],next[128];int issued=0;
    if(!selected_path(path)){file_operations++;return;}
    if(operation==0){
        copy(next,cwd,sizeof(next));
        /* 当前目录后附斜杠也须完整容纳；输入框允许用户换到任意路径。 */
        if(next[0])append(next,"/",sizeof(next));
        if(ui_edit_path(next,sizeof(next),"Create directory")){
            if(length(next)>=64){copy(status,"Path exceeds 63 bytes",sizeof(status));file_operations++;return;}
            result(sc_mkdir(next),"Directory created");issued=1;
        }
    }else if(operation==1&&count){
        copy(next,path,sizeof(next));
        if(ui_edit_path(next,sizeof(next),"Rename / move path")){
            if(length(next)>=64){copy(status,"Path exceeds 63 bytes",sizeof(status));file_operations++;return;}
            result(sc_rename(path,next),"Path renamed");issued=1;
        }
    }else if(operation==2&&count){
        if(ui_confirm("Delete selected path?",path)){result(sc_remove(path),"Path deleted");issued=1;}
    }
    /* 取消不刷新、不重排选择；发出调用后才刷新真实磁盘，失败文案
     * 保留。操作序号在刷新完成后增长，验收才能读到稳定列表。 */
    if(issued)refresh();else copy(status,"Cancelled",sizeof(status));
    file_operations++;
}
static void change_path(void)
{
    /* 先允许输入超过文件系统上限，再完整拒绝；不能因输入框只能
     * 容纳63B而把用户输入的64B悄悄变成另一个合法的63B目标。 */
    char next[128];copy(next,cwd,sizeof(next));
    if(ui_edit_path(next,sizeof(next),"Directory path")){
        if(length(next)>=64){copy(status,"Path exceeds 63 bytes",sizeof(status));file_operations++;return;}
        u32 info[2];int value=sc_stat(next,info);
        /* 空字符串是FSDIR的根入口，STAT根也由内核统一解释。 */
        if(!value&&info[0]==2){copy(cwd,next,64);selection=0;refresh();copy(status,"Directory opened",sizeof(status));}
        else result(value?value:-1,"");
    }else copy(status,"Cancelled",sizeof(status));
    file_operations++;
}
int main(void)
{
    if(ui_open("FILES / mio")<0)return 1;
    sc_args(cwd,sizeof(cwd));u32 info[2];
    if(cwd[0]&&(sc_stat(cwd,info)||info[0]!=2))cwd[0]=0;
    refresh();draw();ui_present();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();draw();ui_present();
        /* 点击的模态去向优先。保留排队首字给路径框，菜单外点击也
         * 只关闭浮层，不顺便消费一个将来可能用于输入的字符。 */
        int a=ui_action,key=(a||(file_context&&(ui_pressed&1)))?-1:sc_key();
        if(a==98){sc_window(ui_win,1);continue;}
        if(a>=1&&a<=7){const int keys[]={10,4,'d',8,'n',2,'x'};key=keys[a-1];file_context=0;}
        if(a>=100&&a<=104)key='1'+a-100;
        if(a==8){selection-=visible_rows?visible_rows:1;if(selection<0)selection=0;clicked=-1;}
        if(a==9){selection+=visible_rows?visible_rows:1;if(selection>=count)selection=count?count-1:0;clicked=-1;}
        if(a==10)change_path();
        if(file_context&&(ui_pressed&1)&&!a){file_context=0;continue;}
        if(key<0)continue;
        if(key==27){if(file_context){file_context=0;continue;}return 0;}
        file_context=0;status[0]=0;
        if(key=='j'||key==0x81){if(selection+1<count)selection++;}
        else if(key=='k'||key==0x80){if(selection)selection--;}
        else if(key==10)open_selected(0,0);
        else if(key==4)open_selected(1,0);
        else if(key=='d'||key=='D')open_selected(0,1);
        else if(key==8)parent();
        else if(key==5)refresh();
        else if(key=='n'||key=='N')mutation(0);
        else if(key==2)mutation(1);
        else if(key=='x'||key=='X')mutation(2);
        else if(key>='1'&&key<='5'){
            const char *places[]={"","HOME","SYS","APPS","BIN"};copy(cwd,places[key-'1'],64);selection=0;refresh();
        }
        draw();ui_present();
    }
}
