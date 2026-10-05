/* =====================================================================
 * mio：Settings 是普通三环程序，提供表单和通用文本编辑器，源码仍可在Studio编辑。
 * 显卡切换、超时回退与写盘结果属于内核；这里仅呈现选项，不能用
 * 修改UI标签冒充模式成功。配置注册只写执行命令与图标，没有包安装
 * 或“复制成功即可信”的隐含权限。菜单重载失败保留内核旧有效快照。
 *
 * 显示预览始终显示真实当前模式和剩余秒数，支持keep/revert鼠标与
 * 键盘。初次打开只选择当前值，不自动切换或写盘，避免启动工具改变
 * 用户桌面。菜单增删/快捷方式创建先显式确认，再核对完整写入长度。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "IMAGECLIENT.inc"
static const int widths[]={
    320,640,800,1024,1280,1920
};
static const int heights[]={
    200,480,600,768,720,1080
};
static char status[96]="Display changes need confirmation",menu_text[8192];
static char labels[32][32],commands[32][128],icons[32][64];
static int tab,mode,scale_choice,count,selected,first,loaded;
static u32 display[8];
static inline void show_result(int r,const char *success)
{
    copy(status,r>=0?success:"Operation failed: ",sizeof(status));
    if(r<0){
        char s[12];
        decimal(s,r);
        append(status,s,sizeof(status));

    }
}
static inline int menus_read(void)
{

    int n=sc_read("SYS/MENU.CFG",menu_text,sizeof(menu_text)-1);
    count=0;
    loaded=0;
    if(n<10){
        copy(status,"MENU.CFG missing or invalid",sizeof(status));
        return 0;

    }
    menu_text[n]=0;
    char *p=menu_text;
    if(p[0]!='S'||p[1]!='M'||p[2]!='E'||p[3]!='N'||p[4]!='U'||p[5]!='1'||p[6]!='M'||p[7]!='I'||p[8]!='O')return 0;
    while(*p&&*p!='\n')p++;
    if(*p)p++;
    while(*p&&count<32){
        if(*p=='#'||*p=='\n'||*p=='\r'){
            while(*p&&*p!='\n')p++;
            if(*p)p++;
            continue;

        }
        char *fields[3];
        int valid=1;
        for(int f=0;f<3;f++){
            fields[f]=p;
            while(*p&&*p!='|'&&*p!='\n'&&*p!='\r')p++;
            if(f<2&&*p!='|'){
                valid=0;
                break;

            }
            if(*p)*p++=0;

        }
        if(!valid)return 0;
        if(length(fields[0])>31||length(fields[1])>127||length(fields[2])>63)return 0;
        copy(labels[count],fields[0],32);
        copy(commands[count],fields[1],128);
        copy(icons[count],fields[2],64);
        count++;
        while(*p=='\r'||*p=='\n')p++;

    }
    loaded=1;
    if(selected>=count)selected=count?count-1:0;
    if(first>=count)first=0;
    return 1;
}
static inline int menus_save(void)
{

    copy(menu_text,"SMENU1MIO\n",sizeof(menu_text));
    for(int i=0;i<count;i++){
        append(menu_text,labels[i],sizeof(menu_text));
        append(menu_text,"|",sizeof(menu_text));
        append(menu_text,commands[i],sizeof(menu_text));
        append(menu_text,"|",sizeof(menu_text));
        append(menu_text,icons[i],sizeof(menu_text));
        append(menu_text,"\n",sizeof(menu_text));

    }
    /* mio：菜单消费者现在提供独立纯检查。先验证临时正文，再写
     * 固定文件；过去“先写坏菜单、重载失败还留在盘上”的路径不能
     * 留给普通用户。候选按窗口handle区分，关闭窗口也不共享草稿。 */
    char candidate[64]="HOME/.SETTINGS-",number[12];
    decimal(number,ui_win);append(candidate,number,64);append(candidate,".CFG",64);
    int n=length(menu_text),r=sc_write(candidate,menu_text,n);
    if(r==n)r=sc_config_check(0,candidate);else if(r>=0)r=-3;
    sc_remove(candidate);
    if(r<0){show_result(r,"");return 0;}
    r=sc_write("SYS/MENU.CFG",menu_text,n);
    if(r!=n){
        show_result(r<0?r:-1,"");
        return 0;

    }
    r=sc_reload();
    show_result(r,"Menu saved and reloaded");
    return r==0;
}
static inline int field_valid(const char *s)
{
    for(int i=0;s[i];i++)if(s[i]=='|'||s[i]=='\n'||s[i]=='\r')return 0;
    return 1;
}
static inline void add_menu(void)
{

    if(!loaded||count>=32){
        copy(status,"Menu unavailable or full",sizeof(status));
        return;

    }
    char label[32]="My program",cmd[128]="apps/hello.scx",icon[64]="SYS/ICONS/APP.SCB";
    if(!ui_edit_text(label,32,"Menu label"))return;
    if(!ui_edit_text(cmd,128,"SCX command and arguments"))return;
    if(!ui_edit_text(icon,64,"SCF / SCB icon path"))return;
    if(!field_valid(label)||!field_valid(cmd)||!field_valid(icon)){
        copy(status,"Pipe and newline are forbidden",sizeof(status));
        return;

    }
    char path[64];
    copy(path,cmd,64);
    for(int i=0;path[i];i++)if(path[i]==' '){
        path[i]=0;
        break;

    }
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1){
        copy(status,"Executable path does not exist",sizeof(status));
        return;

    }
    copy(labels[count],label,32);
    copy(commands[count],cmd,128);
    copy(icons[count],icon,64);
    selected=count++;
    if(!menus_save())menus_read();
}
static inline void shortcut(void)
{

    if(!count||selected>=count)return;
    char path[64]="DESK/MYAPP.LNK",body[256];
    if(!ui_edit_path(path,64,"Save desktop shortcut"))return;
    copy(body,labels[selected],sizeof(body));
    append(body,"\n",sizeof(body));
    append(body,commands[selected],sizeof(body));
    append(body,"\n",sizeof(body));
    append(body,icons[selected],sizeof(body));
    append(body,"\n",sizeof(body));
    char candidate[64]="HOME/.SETTINGS-",number[12];
    decimal(number,ui_win);append(candidate,number,64);append(candidate,".CFG",64);
    int n=length(body),r=sc_write(candidate,body,n);
    if(r==n)r=sc_config_check(1,candidate);else if(r>=0)r=-3;
    sc_remove(candidate);
    if(r<0){show_result(r,"");return;}
    r=sc_write(path,body,n);
    show_result(r==n?sc_reload():-1,"Desktop shortcut created");
}
/* 私有片段使用上面的菜单读写/状态接口；不是第二份公共API头。 */
#include "SETTINGS.inc"
static int settings_scroll,settings_content,settings_body_top,settings_view_height;
static const char *tab_names[]={"Display","Desktop","Programs","User","Environment","Config files"};
static void settings_control(int id,int x,int y,int w,const char *text,int active)
{if(ui_compact)ui_small_control(id,x,y,w,text,active);else ui_control(id,x,y,w,text,active);}
static void settings_value(int x,int y,const char *value,int width)
{
    /* 字段只显示可见正文，真实草稿不截断；长值由模态框/通用编辑器
     * 完整编辑。尾部推进至UTF-8标量边界，不能显示半个汉字字节。 */
    char text[256];int cap=ui_clamp(width/8,2,256),n=length(value);
    if(n>=cap)n=cap-1;
    while(n&&((u8)value[n]&192)==128)n--;
    for(int i=0;i<n;i++)text[i]=value[i];
    text[n]=0;ui_text(x,y,text,PAL_UI_TEXT);
}
static int settings_tabs(void)
{
    int y=ui_compact?34:70,step=ui_compact?26:36;
    if(UI_H<260){
        /* 200%/小窗口不能让三排页签挤走整个工作区。保留鼠标逐页
         * 导航与Tab循环，每页仍可上下滚动，不隐藏实际设置能力。 */
        settings_control(16,16,y,56,"<",0);
        settings_control(17,UI_W-72,y,56,">",0);
        ui_text(80,y+3,tab_names[tab],PAL_UI_CYAN+7);
        return y+step+6;
    }
    int cols=UI_W>=760?6:UI_W>=480?3:2;
    int w=(UI_W-32-(cols-1)*8)/cols;
    for(int i=0;i<6;i++)settings_control(10+i,16+(i%cols)*(w+8),y+(i/cols)*step,w,tab_names[i],tab==i);
    return y+((6+cols-1)/cols)*step+8;
}
static void draw(void)
{
    ui_clip_clear();sc_display(display);
    ui_header("SETTINGS","M8 / make SandCore your workspace");
    settings_body_top=settings_tabs();
    settings_view_height=UI_H-settings_body_top-(ui_compact?54:68);
    if(settings_view_height<24)settings_view_height=24;
    ui_clip_set(16,settings_body_top,UI_W-32,settings_view_height);
    int y=settings_body_top-settings_scroll,step=ui_compact?26:36,w=UI_W-48;
    if(tab==0){
        ui_text(24,y,"RESOLUTION",PAL_UI_CYAN+7);y+=24;
        int cols=UI_W>=600?3:2,ww=(w-(cols-1)*8)/cols;
        const char *names[]={"VGA","640x480","800x600","1024x768","1280x720","1920x1080"};
        for(int i=0;i<6;i++)settings_control(20+i,24+(i%cols)*(ww+8),y+(i/cols)*step,ww,names[i],mode==i);
        y+=((6+cols-1)/cols)*step+16;
        ui_text(24,y,"UI SCALE",PAL_UI_CYAN+7);y+=24;
        const int scale_ids[]={30,31,32};const char *scale_labels[]={"100%","150%","200%"};
        int sx=24;
        for(int i=0;i<3;i++){if(sx+80>UI_W-24){sx=24;y+=step;}settings_control(scale_ids[i],sx,y,80,scale_labels[i],scale_choice==100+i*50);sx+=88;}
        y+=step+12;
        const int ids[]={40,41,42};const char *labels[]={"Preview","Keep","Revert"};y=ui_toolbar(ids,labels,3,y);
        ui_text(24,y,"Actual:",PAL_UI_MUTED);ui_number(88,y,display[0],PAL_UI_TEXT);
        ui_text(128,y,"x",PAL_UI_MUTED);ui_number(152,y,display[1],PAL_UI_TEXT);y+=24;
        ui_number(24,y,display[2],PAL_UI_TEXT);ui_text(64,y,"%",PAL_UI_MUTED);
        if(display[6]){ui_text(104,y,"Keep in",PAL_UI_GOLD+7);ui_number(176,y,display[6],PAL_UI_GOLD+7);}y+=28;
    }else if(tab==1){
        ui_text(24,y,"THEME",PAL_UI_CYAN+7);y+=24;
        const int ids[]={70,71,72,73,74,53,50,51,52};
        const char *labels[]={"White Aurora","Classic","Custom theme","Reload theme","Wallpaper file","Reload desktop","VGA dawn","VGA night","VGA warm"};
        y=ui_toolbar(ids,labels,9,y);
        ui_text(24,y,"Current wallpaper:",PAL_UI_MUTED);y+=24;
        char path[64];sc_theme_path(0,path,64);settings_value(24,y,path,w);y+=28;
        ui_text(24,y,"SCB BMP PNG JPEG WebP / VGA scenes",PAL_UI_MUTED);y+=28;
    }else if(tab==2){
        const int ids[]={60,66,61,62,63};const char *actions[]={"Add","Edit","Remove","Shortcut","Reload"};
        y=ui_toolbar(ids,actions,5,y);
        int rows=(settings_view_height-110)/28;if(rows<1)rows=1;
        if(selected<first)first=selected;
        if(selected>=first+rows)first=selected-rows+1;
        ui_panel(16,y,UI_W-32,rows*28+8);
        for(int r=0;r<rows&&first+r<count;r++){
            int i=first+r,yy=y+4+r*28;
            if(i==selected)ui_rect(20,yy,UI_W-40,28,PAL_UI_NIGHT+6);
            ui_selected_text(28,yy+6,labels[i],i==selected);
            if(UI_W>640)settings_value(300,yy+6,commands[i],UI_W-324);
            if((ui_pressed&1)&&ui_hit(20,yy,UI_W-40,28))selected=i;
        }
        y+=rows*28+16;
        const int pages[]={64,65};const char *page_labels[]={"Prev","Next"};y=ui_toolbar(pages,page_labels,2,y);
    }else if(tab==3){
        ui_text(24,y,"USERNAME",PAL_UI_CYAN+7);y+=24;
        ui_panel(24,y,w,32);settings_value(32,y+8,user_name,w-16);y+=40;
        settings_control(80,24,y,112,"Edit name",0);y+=step+16;
        ui_text(24,y,"HOME DIRECTORY",PAL_UI_CYAN+7);y+=24;
        ui_panel(24,y,w,32);settings_value(32,y+8,user_home,w-16);y+=40;
        settings_control(81,24,y,112,"Edit home",0);y+=step+12;
        const int ids[]={82,83};const char *labels[]={"Save user","Reload user"};y=ui_toolbar(ids,labels,2,y);
        ui_text(24,y,user_dirty?"Draft / press Save user":"Single user / no login or password",PAL_UI_MUTED);y+=28;
    }else if(tab==4){
        const int ids[]={90,91,92,93,94};const char *actions[]={"Add","Edit","Remove","Save env","Reload env"};
        y=ui_toolbar(ids,actions,5,y);
        int rows=(settings_view_height-130)/32;if(rows<1)rows=1;
        if(env_selected<env_first)env_first=env_selected;
        if(env_selected>=env_first+rows)env_first=env_selected-rows+1;
        ui_panel(16,y,UI_W-32,rows*32+8);
        for(int row=0;row<rows&&env_first+row<env_count;row++){
            int i=env_first+row,yy=y+4+row*32;
            if(i==env_selected)ui_rect(20,yy,UI_W-40,32,PAL_UI_NIGHT+6);
            ui_selected_text(28,yy+8,env_names[i],i==env_selected);
            if(UI_W>480)settings_value(UI_W/3,yy+8,env_values[i],UI_W*2/3-28);
            if((ui_pressed&1)&&ui_hit(20,yy,UI_W-40,32))env_selected=i;
        }
        y+=rows*32+16;
        const int pages[]={95,96};const char *page_labels[]={"Prev","Next"};y=ui_toolbar(pages,page_labels,2,y);
        ui_text(24,y,env_dirty?"Draft / Save env applies every key":"Default PATH /BIN:/APPS",PAL_UI_MUTED);y+=28;
    }else{
        const int ids[]={100,101,102,103,104,105,106,107};
        const char *labels[]={"USER.CFG","ENV.CFG","DISPLAY.CFG","THEME.CFG","MENU.CFG","WALL.CFG","Open file","New file"};
        y=ui_toolbar(ids,labels,8,y);
        ui_text(24,y,"Text editor / known configs validated",PAL_UI_MUTED);y+=24;
        ui_text(24,y,"Core code remains protected",PAL_UI_MUTED);y+=28;
    }
    settings_content=y-settings_body_top+settings_scroll;
    ui_clip_clear();
    int max_scroll=settings_content-settings_view_height;if(max_scroll<0)max_scroll=0;
    if(settings_scroll>max_scroll){settings_scroll=max_scroll;ui_followup=1;}
    int bar=UI_H-(ui_compact?50:64);
    settings_control(18,16,bar,64,"Up",0);settings_control(19,88,bar,72,"Down",0);
    ui_text(176,bar+3,settings_scroll?"Scrolled":"Page",PAL_UI_MUTED);
    ui_footer(status);
}
static void edit_menu(void)
{
    if(!loaded||!count)return;
    char label[32],command[128],icon[64];copy(label,labels[selected],32);copy(command,commands[selected],128);copy(icon,icons[selected],64);
    if(!ui_edit_text(label,32,"Menu label")||!ui_edit_text(command,128,"SCX command")||!ui_edit_value(icon,64,"Icon path / @theme / @auto",1))return;
    if(!field_valid(label)||!field_valid(command)||!field_valid(icon)){copy(status,"Invalid menu field",sizeof(status));return;}
    copy(labels[selected],label,32);copy(commands[selected],command,128);copy(icons[selected],icon,64);
    if(!menus_save())menus_read();
}
static void choose_wallpaper(void)
{
    char path[64];sc_theme_path(0,path,64);
    if(!ui_edit_path(path,64,"Choose wallpaper image"))return;
    u32 info[2];u8 header[32];
    if(sc_stat(path,info)||info[0]!=1||sc_read_at(path,header,32,0)<24){copy(status,"Cannot read wallpaper",sizeof(status));return;}
    int scb=header[0]=='S'&&header[1]=='C'&&header[2]=='B'&&(header[3]=='1'||header[3]=='2')
        &&header[4]=='M'&&header[5]=='I'&&header[6]=='O'&&!header[7];
    if(!scb){
        ScImageClient client={0};int result=image_client_begin(&client,path);
        if(result){show_result(result,"Wallpaper validated");return;}
        /* 验证阶段还未写配置。真正解完才保存路径；点击Cancel或Esc
         * 只取消本人的票据，旧主题/旧壁纸完全保留。这个模态页仍按
         * NUI重排，不能在1080/150%窗口里画固定320×200状态条。 */
        for(;;){
            result=image_client_step(&client);if(result!=1)break;
            if(ui_frame_due()){
                ui_pointer();ui_header("CHECKING WALLPAPER",path);
                ui_text(16,ui_compact?42:76,"Checking image",PAL_UI_MUTED);
                ui_small_control(1,16,UI_H-(ui_compact?52:68),96,"Cancel",0);
                ui_footer("Configuration remains unchanged");ui_present();
                if(ui_action==1 || sc_key_peek()==27){
                    if(sc_key_peek()==27)sc_key();image_client_cancel(&client);
                    copy(status,"Wallpaper cancelled",sizeof(status));return;
                }
            }
        }
        if(result){show_result(result,"Wallpaper validated");return;}
        sc_free(client.pixels);
    }else{
    u32 width=(u32)header[8]|((u32)header[9]<<8)|((u32)header[10]<<16)|((u32)header[11]<<24);
    u32 height=(u32)header[12]|((u32)header[13]<<8)|((u32)header[14]<<16)|((u32)header[15]<<24);
    u32 format=(u32)header[16]|((u32)header[17]<<8)|((u32)header[18]<<16)|((u32)header[19]<<24);
    if(!width||!height||width>1920||height>1080){copy(status,"Wallpaper dimensions invalid",sizeof(status));return;}
    u32 bytes=width*height;
    int valid=header[3]=='1'&&format==1&&info[1]==24+bytes
        &&header[20]=='M'&&header[21]=='I'&&header[22]=='O'&&!header[23];
    if(header[3]=='2'&&info[1]>=32){
        u32 body=(u32)header[24]|((u32)header[25]<<8)|((u32)header[26]<<16)|((u32)header[27]<<24);
        valid=format==2&&!(header[20]|header[21]|header[22]|header[23])&&body==bytes*4&&info[1]==32+body
            &&header[28]=='M'&&header[29]=='I'&&header[30]=='O'&&!header[31];
    }
    if(!valid){copy(status,"Wallpaper header/length invalid",sizeof(status));return;}
    }
    /* 只替换wallpaper字段，保留用户全部主题角色和名称。候选主题
     * 保存成功后才生效，重复字段会由同一内核解析器整体拒绝。 */
    int n=sc_read("SYS/THEME.CFG",cfg_text,CFG_MAX-1);
    if(n<0){copy(status,"Theme file unavailable",sizeof(status));return;}cfg_text[n]=0;
    char *p=cfg_text;copy(cfg_scratch,"",CFG_MAX);
    while(*p){
        char *line=p;while(*p&&*p!='\n')p++;if(*p)*p++=0;
        char key[128];int i=0;while(line[i]&&line[i]!='='&&i<127){key[i]=line[i];i++;}key[i]=0;
        if(!equal(cfg_trim(key),"wallpaper")){append(cfg_scratch,line,CFG_MAX);append(cfg_scratch,"\n",CFG_MAX);}
    }
    append(cfg_scratch,"wallpaper=",CFG_MAX);append(cfg_scratch,path,CFG_MAX);append(cfg_scratch,"\n",CFG_MAX);
    char candidate[64];cfg_candidate_path(candidate);n=length(cfg_scratch);int r=sc_write(candidate,cfg_scratch,n);
    if(r==n)r=sc_theme_load(candidate,1);else if(r>=0)r=-3;
    sc_remove(candidate);show_result(r,"Wallpaper saved and applied");
}
int main(void)
{
    if(ui_open("Settings")<0)return 1;
    sc_display(display);scale_choice=display[2];
    for(int i=0;i<6;i++)if(widths[i]==(int)display[0]&&heights[i]==(int)display[1])mode=i;
    menus_read();cfg_user_read();cfg_env_read();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();if(cfg_editing)cfg_editor_draw();else draw();ui_present();
        /* 同帧出现鼠标按钮和排队文字时，先完成按钮的焦点转移。
         * 若在这里先GETKEY，再进输入框，那个字就只被普通页面取走
         * 而没有编辑目标，用户快速“点击后键入”会丢首字。按钮帧
         * 保留键队首，模态框或下一普通帧再按原GETKEY语义消费；
         * 不用peek冒充取键，也不清空驱动/窗口队列。 */
        int a=ui_action,key=a?-1:sc_key();
        if(cfg_editing){cfg_editor_action(key,a);continue;}
        if(key==27){
            if(display[6])sc_display_apply(0,0,0,2);
            else if((!user_dirty&&!env_dirty)||ui_confirm("Unsaved settings","Discard drafts and close?"))return 0;
        }
        int next=tab;
        if(a>=10&&a<=15)next=a-10;
        if(a==16)next=(tab+5)%6;
        if(a==17||key==9)next=(tab+1)%6;
        if(next!=tab){tab=next;settings_scroll=0;}
        if(a==18||key==0x80)settings_scroll=ui_clamp(settings_scroll-96,0,8192);
        if(a==19||key==0x81)settings_scroll=ui_clamp(settings_scroll+96,0,ui_clamp(settings_content-settings_view_height,0,8192));
        if(a>=20&&a<26)mode=a-20;
        if(a>=30&&a<=32)scale_choice=100+(a-30)*50;
        if(a==40||(tab==0&&key=='p'))show_result(sc_display_apply(widths[mode],heights[mode],scale_choice,0),"Preview active: Keep or Revert");
        if(a==41||(tab==0&&key=='k'))show_result(sc_display_apply(0,0,0,1),"Display saved for next boot");
        if(a==42||(tab==0&&key=='r'))show_result(sc_display_apply(0,0,0,2),"Previous display restored");
        if(a>=50&&a<=52)show_result(sc_wallpaper(a-50),"Legacy VGA scene saved");
        if(a==70||a==71)show_result(sc_theme_load(a==70?"SYS/THEMES/AURORA.CFG":"SYS/THEMES/CLASSIC.CFG",1),"Theme saved and applied");
        if(a==72){char path[64]="SYS/THEMES/AURORA.CFG";if(ui_edit_path(path,64,"Open theme configuration"))show_result(sc_theme_load(path,1),"Custom theme saved and applied");}
        if(a==73)show_result(sc_theme_load(0,2),"Theme configuration reloaded");
        if(a==74)choose_wallpaper();
        if(a==53||a==63){show_result(sc_reload(),"Desktop reloaded");menus_read();}
        if(a==60)add_menu();
        if(a==66)edit_menu();
        if(a==61&&count&&ui_confirm("Remove menu entry?",labels[selected])){
            for(int i=selected;i+1<count;i++){copy(labels[i],labels[i+1],32);copy(commands[i],commands[i+1],128);copy(icons[i],icons[i+1],64);}
            count--;if(selected>=count)selected=count?count-1:0;if(!menus_save())menus_read();
        }
        if(a==62)shortcut();
        if(a==64&&selected)selected--;
        if(a==65&&selected+1<count)selected++;
        if(a==80){char value[32];copy(value,user_name,32);if(ui_edit_text(value,32,"Username / no authentication")){copy(user_name,value,32);user_dirty=1;}}
        if(a==81){char value[64];copy(value,user_home,64);if(ui_edit_text(value,64,"Absolute home directory")){copy(user_home,value,64);user_dirty=1;}}
        if(a==82){copy(cfg_scratch,"SUSER1MIO\nusername=",CFG_MAX);append(cfg_scratch,user_name,CFG_MAX);append(cfg_scratch,"\nhome=",CFG_MAX);append(cfg_scratch,user_home,CFG_MAX);append(cfg_scratch,"\n",CFG_MAX);if(cfg_staged_save(0,cfg_scratch,length(cfg_scratch)))user_dirty=0;}
        if(a==83&&(!user_dirty||ui_confirm("Reload user","Discard user draft?"))){show_result(sc_user_config(0,0,2),"User configuration reloaded");cfg_user_read();}
        if((a==90||a==91)&&env_loaded){
            int index=a==90?env_count:env_selected;
            if(index<16&&(a==90||index<env_count)){
                char name[32],value[256];copy(name,a==90?"NEW_VAR":env_names[index],32);copy(value,a==90?"":env_values[index],256);
                if(ui_edit_text(name,32,"Environment variable name")&&ui_edit_value(value,256,"Environment value / PATH is absolute",1)){
                    copy(env_names[index],name,32);copy(env_values[index],value,256);if(a==90)env_count++;env_selected=index;env_dirty=1;
                }
            }else copy(status,"Environment supports at most 16 entries",sizeof(status));
        }
        if(a==92&&env_count&&ui_confirm("Remove environment variable?",env_names[env_selected])){
            for(int i=env_selected;i+1<env_count;i++){copy(env_names[i],env_names[i+1],32);copy(env_values[i],env_values[i+1],256);}
            env_count--;if(env_selected>=env_count)env_selected=env_count?env_count-1:0;env_dirty=1;
        }
        if(a==93)cfg_env_save();
        if(a==94&&(!env_dirty||ui_confirm("Reload environment","Discard environment draft?"))){show_result(sc_user_config(1,0,2),"Environment reloaded");cfg_env_read();}
        if(a==95&&env_selected)env_selected--;
        if(a==96&&env_selected+1<env_count)env_selected++;
        if(a>=100&&a<=106){
            const char *paths[]={"SYS/USER.CFG","SYS/ENV.CFG","SYS/DISPLAY.CFG","SYS/THEME.CFG","SYS/MENU.CFG","SYS/WALL.CFG"};
            char path[64];copy(path,a<106?paths[a-100]:cfg_path,64);
            if(a<106||ui_edit_path(path,64,"Open configuration text"))cfg_open(path);
        }
        if(a==107){char path[64]="HOME/CONFIG.CFG";if(ui_edit_path(path,64,"New configuration text")){copy(cfg_path,path,64);cfg_text[0]=0;cfg_used=cfg_cursor=cfg_first=cfg_left=0;cfg_dirty=cfg_editing=1;}}
    }
}
