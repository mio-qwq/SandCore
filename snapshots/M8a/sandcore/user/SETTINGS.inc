/* =====================================================================
 * mio：Settings私有配置工作区，和SCAPI.H公共合同分开。
 *
 * 【编辑不是保存】用户/环境表单先修改本进程草稿；只有Save才写候选
 * 文件并调用内核验证/提交。坏值、取消、打开失败不能改变有效配置。
 * 环境保留全部16项未知键，不把PATH之外的用户字段丢掉。
 *
 * 【通用编辑为何另有备用缓冲】先STAT、完整读、UTF-8/控制字节检查
 * 后才换文档。拒绝过长/二进制/读失败时，原来的未保存内容仍在。
 * 光标按Unicode标量移动，鼠标按8/16px字面定位，不能以像素作字节。
 * 文件上限8191B明确拒绝，不静默截尾再覆盖用户文件。
 *
 * 【核心保护】这不是权限编辑器；SYS/CORE仍由内核拒绝三环写入。
 * 候选路径含稳定窗口handle，避免两个Settings实例互相覆盖候选。
 * SandFS暂无掉电日志：成功前核对实际写入长度，不宣称掉电原子性。
 * ===================================================================== */
#define CFG_MAX 8192
static char cfg_text[CFG_MAX],cfg_scratch[CFG_MAX],cfg_path[64]="SYS/USER.CFG";
static int cfg_used,cfg_cursor,cfg_first,cfg_left,cfg_dirty,cfg_editing;
static int cfg_editor_y,cfg_rows,cfg_columns,cfg_menu,cfg_menu_x,cfg_menu_y;
static char user_name[32],user_home[64];
static char env_names[16][32],env_values[16][256];
static int env_count,env_selected,env_first,env_loaded,user_dirty,env_dirty;

static int cfg_equal(const char *a,const char *b)
{
    while(*a&&*b){int x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}
    return !*a&&!*b;
}
static int cfg_prefix(const char *a,const char *b)
{
    while(*b){int x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}
    return 1;
}
static int cfg_text_valid(const char *p,int n)
{
    for(int i=0;i<n;){
        u32 c=(u8)p[i++];int extra=0;u32 low=0;
        if(c<128){if(!c||(c<32&&c!='\n'&&c!='\r'&&c!='\t')||c==127)return 0;continue;}
        if(c>=0xC2&&c<=0xDF){extra=1;low=0x80;c&=31;}
        else if(c>=0xE0&&c<=0xEF){extra=2;low=0x800;c&=15;}
        else if(c>=0xF0&&c<=0xF4){extra=3;low=0x10000;c&=7;}
        else return 0;
        while(extra--){if(i>=n||((u8)p[i]&192)!=128)return 0;c=(c<<6)|((u8)p[i++]&63);}
        if(c<low||c>0x10FFFF||(c>=0xD800&&c<=0xDFFF))return 0;
    }
    return 1;
}
static void cfg_candidate_path(char *path)
{char number[12];copy(path,"HOME/.SETTINGS-",64);decimal(number,ui_win);append(path,number,64);append(path,".CFG",64);}
static int cfg_staged_save(int kind,const char *body,int n)
{
    char candidate[64];cfg_candidate_path(candidate);
    int r=sc_write(candidate,body,n);
    if(r==n)r=sc_user_config(kind,candidate,1);else if(r>=0)r=-3;
    /* 失败同样回收候选，避免每次错误占一个目录槽。提交保存的是原
     * 文本，读取临时文件之后内核才替换有效快照，绝不先写固定配置。 */
    int cleanup=sc_remove(candidate);
    if(!r&&cleanup<0){copy(status,"Saved; temporary file cleanup failed",sizeof(status));return 1;}
    show_result(r,"Saved and applied");return r==0;
}
static void cfg_user_read(void)
{
    sc_user(0,0,user_name,sizeof(user_name));sc_user(1,0,user_home,sizeof(user_home));user_dirty=0;
}
static char *cfg_trim(char *p)
{while(*p==' '||*p=='\t')p++;int n=length(p);while(n&&(p[n-1]=='\r'||p[n-1]==' '||p[n-1]=='\t'))p[--n]=0;return p;}
static void cfg_env_read(void)
{
    u32 info[2];env_loaded=0;
    if(sc_stat("SYS/ENV.CFG",info)||info[0]!=1||info[1]>=4096){copy(status,"ENV.CFG unavailable; use Config files",sizeof(status));return;}
    int n=sc_read("SYS/ENV.CFG",cfg_scratch,(int)info[1]);
    if(n!=(int)info[1]||!cfg_text_valid(cfg_scratch,n))return;
    cfg_scratch[n]=0;char *p=cfg_scratch;int row=0,count=0;
    while(*p){
        char *start=p;while(*p&&*p!='\n')p++;if(*p)*p++=0;
        char *name=cfg_trim(start);
        if(!row++){if(!equal(name,"SENV1MIO"))return;continue;}
        if(!*name||*name=='#')continue;
        char *value=name;while(*value&&*value!='=')value++;if(!*value||count==16)return;
        *value++=0;name=cfg_trim(name);value=cfg_trim(value);
        if(length(name)>31||length(value)>255)return;
        copy(env_names[count],name,32);copy(env_values[count],value,256);count++;
    }
    env_count=count;env_selected=env_first=env_dirty=0;env_loaded=1;
}
static void cfg_env_save(void)
{
    if(!env_loaded)return;
    copy(cfg_scratch,"SENV1MIO\n",CFG_MAX);
    for(int i=0;i<env_count;i++){
        append(cfg_scratch,env_names[i],CFG_MAX);append(cfg_scratch,"=",CFG_MAX);
        append(cfg_scratch,env_values[i],CFG_MAX);append(cfg_scratch,"\n",CFG_MAX);
    }
    if(cfg_staged_save(1,cfg_scratch,length(cfg_scratch)))env_dirty=0;
}
static int cfg_next(int at)
{if(at>=cfg_used)return cfg_used;at++;while(at<cfg_used&&((u8)cfg_text[at]&192)==128)at++;return at;}
static int cfg_previous(int at)
{if(!at)return 0;at--;while(at&&((u8)cfg_text[at]&192)==128)at--;return at;}
static int cfg_line_start(int at){while(at&&cfg_text[at-1]!='\n')at--;return at;}
static int cfg_line_end(int at){while(at<cfg_used&&cfg_text[at]!='\n')at++;return at;}
static int cfg_cells(int at)
{return cfg_text[at]=='\t'?4:cfg_text[at]=='\r'?0:(u8)cfg_text[at]>=128?2:1;}
static int cfg_column(int at)
{int n=0;for(int i=cfg_line_start(at);i<at;i=cfg_next(i))n+=cfg_cells(i);return n;}
static int cfg_line(int at)
{int n=0;for(int i=0;i<at;i++)if(cfg_text[i]=='\n')n++;return n;}
static int cfg_at_line(int row)
{int at=0;while(row&&at<cfg_used)if(cfg_text[at++]=='\n')row--;return at;}
static int cfg_at_column(int at,int col)
{while(col>0&&at<cfg_used&&cfg_text[at]!='\n'){int w=cfg_cells(at);if(w>col)break;col-=w;at=cfg_next(at);}return at;}
static int cfg_open(const char *path)
{
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1){copy(status,"File not found; edits preserved",sizeof(status));return 0;}
    if(info[1]>=CFG_MAX){copy(status,"File exceeds 8191 bytes; edits preserved",sizeof(status));return 0;}
    int n=sc_read(path,cfg_scratch,(int)info[1]);
    if(n!=(int)info[1]||!cfg_text_valid(cfg_scratch,n)){copy(status,"Read/text check failed; edits preserved",sizeof(status));return 0;}
    for(int i=0;i<n;i++)cfg_text[i]=cfg_scratch[i];
    cfg_text[n]=0;
    copy(cfg_path,path,64);cfg_used=n;cfg_cursor=cfg_first=cfg_left=cfg_dirty=0;cfg_editing=1;
    copy(status,"F2 save / F8 start / F9 end / Esc back",sizeof(status));return 1;
}
static int cfg_save(void)
{
    /* 已知配置必须用与消费者相同的校验入口。用户名/环境先候选
     * 提交；主题先纯检查；菜单/快捷方式先CONFIGCHECK。未知文本
     * 仍能编辑，但不借此绕过核心写保护。显示只存下次启动，当前
     * 模式继续由Display页的15秒预览流程管理。 */
    int r=0,n=cfg_used,kind=-1,known_user=-1;u32 info[2];
    if(cfg_equal(cfg_path,"SYS/USER.CFG"))known_user=0;
    if(cfg_equal(cfg_path,"SYS/ENV.CFG"))known_user=1;
    if(known_user>=0){
        if(!cfg_staged_save(known_user,cfg_text,n))return 0;
        cfg_dirty=0;
        /* 提交USER只重读USER；另一页还没保存的ENV草稿必须保留。
         * 同理提交ENV不丢USER草稿，页签切换不是隐式“放弃修改”。 */
        if(known_user==0)cfg_user_read();else cfg_env_read();
        return 1;
    }
    if(cfg_equal(cfg_path,"SYS/MENU.CFG"))kind=0;
    int size=length(cfg_path);
    if(cfg_prefix(cfg_path,"DESK/")&&size>=4&&cfg_equal(cfg_path+size-4,".LNK"))kind=1;
    if(cfg_equal(cfg_path,"SYS/THEME.CFG")||cfg_prefix(cfg_path,"SYS/THEMES/"))kind=2;
    if(cfg_equal(cfg_path,"SYS/DISPLAY.CFG"))kind=3;
    if(cfg_equal(cfg_path,"SYS/WALL.CFG"))kind=4;
    char candidate[64];
    if(kind>=0){
        cfg_candidate_path(candidate);r=sc_write(candidate,cfg_text,n);
        if(r==n)r=sc_config_check(kind,candidate);else if(r>=0)r=-3;
        if(!r&&kind==2&&cfg_equal(cfg_path,"SYS/THEME.CFG"))r=sc_theme_load(candidate,1);
        sc_remove(candidate);
        if(r<0){show_result(r,"");return 0;}
    }
    if(!(kind==2&&cfg_equal(cfg_path,"SYS/THEME.CFG"))){
        r=sc_write(cfg_path,cfg_text,n);
        if(r!=n){show_result(r<0?r:-3,"");return 0;}
    }
    if(kind==0||kind==1){r=sc_reload();menus_read();}
    if(kind==4)r=sc_wallpaper(cfg_text[0]-'0');
    if(r<0){show_result(r,"");return 0;}
    cfg_dirty=0;copy(status,kind==3?"Saved; display applies next boot":"Configuration saved",sizeof(status));
    /* STAT重新读只用来确认文件仍存在，不把写失败后的旧文件存在
     * 当成写成功。实际成功条件始终是上面的完整写入返回长度。 */
    sc_stat(cfg_path,info);return 1;
}
static void cfg_insert(int key)
{
    if(cfg_used+1>=CFG_MAX){copy(status,"8191-byte limit reached",sizeof(status));return;}
    for(int i=cfg_used;i>=cfg_cursor;i--)cfg_text[i+1]=cfg_text[i];
    cfg_text[cfg_cursor++]=(char)key;cfg_used++;cfg_dirty=1;
}
static void cfg_backspace(void)
{
    if(!cfg_cursor)return;
    int at=cfg_previous(cfg_cursor),removed=cfg_cursor-at;
    for(int i=cfg_cursor;i<=cfg_used;i++)cfg_text[i-removed]=cfg_text[i];
    cfg_cursor=at;cfg_used-=removed;cfg_dirty=1;
}
static void cfg_editor_draw(void)
{
    ui_header("CONFIG FILES",cfg_path);
    const int ids[]={130,131,132,133,134,135};
    const char *names[]={"Save","Reload","Back","Page up","Page down","Save as"};
    int y=ui_toolbar(ids,names,UI_H<260?3:6,ui_compact?34:70);
    cfg_editor_y=y+4;cfg_rows=(UI_H-cfg_editor_y-42)/20;if(cfg_rows<1)cfg_rows=1;
    cfg_columns=(UI_W-72)/8;if(cfg_columns<4)cfg_columns=4;
    int line=cfg_line(cfg_cursor),col=cfg_column(cfg_cursor);
    if(line<cfg_first)cfg_first=line;
    if(line>=cfg_first+cfg_rows)cfg_first=line-cfg_rows+1;
    if(col<cfg_left)cfg_left=col;
    if(col>=cfg_left+cfg_columns)cfg_left=col-cfg_columns+1;
    ui_panel(16,cfg_editor_y,UI_W-32,cfg_rows*20+8);
    ui_clip_set(20,cfg_editor_y+4,UI_W-40,cfg_rows*20);
    for(int row=0;row<cfg_rows;row++){
        int at=cfg_at_line(cfg_first+row),yy=cfg_editor_y+4+row*20;
        if(at>=cfg_used&&row&&(!cfg_used||cfg_text[cfg_used-1]!='\n'))break;
        if(cfg_first+row==line)ui_rect(52,yy,UI_W-72,20,PAL_UI_NIGHT+5);
        ui_number(22,yy,cfg_first+row+1,PAL_UI_MUTED);
        at=cfg_at_column(at,cfg_left);int x=56;
        while(at<cfg_used&&cfg_text[at]!='\n'&&x<UI_W-24){
            int next=cfg_next(at),w=cfg_cells(at);char glyph[5];
            if(cfg_text[at]!='\r'&&cfg_text[at]!='\t'){
                int n=0;for(int i=at;i<next;i++)glyph[n++]=cfg_text[i];glyph[n]=0;ui_text(x,yy,glyph,PAL_UI_TEXT);
            }
            x+=w*8;at=next;
        }
    }
    ui_rect(56+(col-cfg_left)*8,cfg_editor_y+4+(line-cfg_first)*20,1,18,PAL_UI_CYAN+7);
    ui_clip_clear();ui_footer(status);
    ui_text(UI_W-88,ui_compact?6:14,cfg_dirty?"EDIT":"SAVED",cfg_dirty?PAL_UI_GOLD+7:PAL_UI_CYAN+7);
    if(cfg_menu){ui_panel(cfg_menu_x,cfg_menu_y,144,108);ui_control(130,cfg_menu_x+6,cfg_menu_y+6,132,"Save",0);ui_control(131,cfg_menu_x+6,cfg_menu_y+40,132,"Reload",0);ui_control(132,cfg_menu_x+6,cfg_menu_y+74,132,"Back",0);}
}
static void cfg_editor_action(int key,int action)
{
    if((ui_pressed&1)&&ui_hit(56,cfg_editor_y+4,UI_W-80,cfg_rows*20)&&!cfg_menu)
        cfg_cursor=cfg_at_column(cfg_at_line(cfg_first+(ui_y-cfg_editor_y-4)/20),cfg_left+(ui_x-56)/8);
    if(ui_pressed&2){cfg_menu=!cfg_menu;cfg_menu_x=ui_clamp(ui_x,0,UI_W-144);cfg_menu_y=ui_clamp(ui_y,0,UI_H-138);}
    if(action>=130&&action<=135)cfg_menu=0;
    if(key==2||action==130)cfg_save();
    else if(key==27||action==132){
        if(key==27&&cfg_menu)cfg_menu=0;
        else if(!cfg_dirty||ui_confirm("Unsaved configuration","Discard edits and go back?"))cfg_editing=0;
    }else if(action==131){if(!cfg_dirty||ui_confirm("Reload from disk","Discard current edits?"))cfg_open(cfg_path);}
    else if(action==135||key==3){char path[64];copy(path,cfg_path,64);if(ui_edit_path(path,64,"Save configuration as")){copy(cfg_path,path,64);cfg_dirty=1;cfg_save();}}
    else if(action==133)cfg_cursor=cfg_at_line(ui_clamp(cfg_line(cfg_cursor)-cfg_rows,0,8191));
    else if(action==134)cfg_cursor=cfg_at_line(cfg_line(cfg_cursor)+cfg_rows);
    else if(key==0x88)cfg_cursor=0;
    else if(key==0x89)cfg_cursor=cfg_used;
    else if(key==0x82)cfg_cursor=cfg_previous(cfg_cursor);
    else if(key==0x83)cfg_cursor=cfg_next(cfg_cursor);
    else if(key==0x80){int start=cfg_line_start(cfg_cursor);if(start)cfg_cursor=cfg_at_column(cfg_line_start(start-1),cfg_column(cfg_cursor));}
    else if(key==0x81){int end=cfg_line_end(cfg_cursor);if(end<cfg_used)cfg_cursor=cfg_at_column(end+1,cfg_column(cfg_cursor));}
    else if(key==8)cfg_backspace();
    else if(key==10||key==9||(key>=32&&key<127))cfg_insert(key);
}
