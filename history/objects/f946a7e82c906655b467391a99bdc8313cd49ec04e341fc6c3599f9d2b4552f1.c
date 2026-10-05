/* =====================================================================
 * mio：Settings 是普通三环程序，配置文字可在 Studio 同样编辑。
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
static const int widths[]={
    320,640,800,1024,1280,1920
};
static const int heights[]={
    200,480,600,768,720,1080
};
static const char *mode_names[]={
    "VGA 320 x 200","640 x 480","800 x 600","1024 x 768","1280 x 720","1920 x 1080"
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
    int n=length(menu_text),r=sc_write("SYS/MENU.CFG",menu_text,n);
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
    int n=length(body),r=sc_write(path,body,n);
    show_result(r==n?sc_reload():-1,"Desktop shortcut created");
}
static inline void draw(void)
{

    sc_display(display);
    ui_header("SETTINGS","M8 / make SandCore your workspace");
    int tabs_y=ui_compact?34:70;
    if(ui_compact){
        ui_small_control(10,8,tabs_y,88,"Display",tab==0);
        ui_small_control(11,104,tabs_y,88,"Desktop",tab==1);
        ui_small_control(12,200,tabs_y,88,"Programs",tab==2);

    }
    else{
        ui_control(10,16,tabs_y,96,"Display",tab==0);
        ui_control(11,120,tabs_y,96,"Desktop",tab==1);
        ui_control(12,224,tabs_y,104,"Programs",tab==2);

    }
    int y=ui_compact?60:116;
    if(tab==0){

        ui_panel(16,y,UI_W-32,UI_H-y-30);
        if(!ui_compact)ui_text(32,y+16,"RESOLUTION",PAL_UI_CYAN+7);
        int cols=ui_compact?3:2,ww=(UI_W-64-(cols-1)*8)/cols;
        const char *short_names[]={
            "VGA","640x480","800x600","1024x768","1280x720","1920x1080"
        };
        for(int i=0;i<6;i++){
            int xx=24+(i%cols)*(ww+8),yy=y+(ui_compact?4:44)+(i/cols)*(ui_compact?24:36);
            if(ui_compact)ui_small_control(20+i,xx,yy,ww,short_names[i],mode==i);
            else ui_control(20+i,xx,yy,ww,mode_names[i],mode==i);

        }
        int yy=y+(ui_compact?52:164);
        if(ui_compact){
            ui_small_control(30,24,yy,ww,"100%",scale_choice==100);
            ui_small_control(31,32+ww,yy,ww,"150%",scale_choice==150);
            ui_small_control(32,40+2*ww,yy,ww,"200%",scale_choice==200);
            ui_small_control(40,24,yy+24,ww,"Preview",0);
            ui_small_control(41,32+ww,yy+24,ww,"Keep",display[6]!=0);
            ui_small_control(42,40+2*ww,yy+24,ww,"Revert",0);

        }
        else{
            ui_text(32,yy,"UI SCALE",PAL_UI_CYAN+7);
            ui_control(30,32,yy+24,84,"100%",scale_choice==100);
            ui_control(31,124,yy+24,84,"150%",scale_choice==150);
            ui_control(32,216,yy+24,84,"200%",scale_choice==200);
            ui_control(40,32,yy+66,96,"Preview",0);
            ui_control(41,136,yy+66,80,"Keep",display[6]!=0);
            ui_control(42,224,yy+66,96,"Revert",0);

        }
        if(!ui_compact&&yy+116<UI_H-34){
            ui_text(32,yy+112,"Actual:",PAL_UI_MUTED);
            ui_number(104,yy+112,display[0],PAL_UI_TEXT);
            ui_text(144,yy+112,"x",PAL_UI_MUTED);
            ui_number(168,yy+112,display[1],PAL_UI_TEXT);
            ui_number(240,yy+112,display[2],PAL_UI_TEXT);
            ui_text(280,yy+112,"%",PAL_UI_MUTED);

        }
        if(display[6]){
            ui_text(UI_W-152,ui_compact?6:14,"Keep in",PAL_UI_GOLD+7);
            ui_number(UI_W-80,ui_compact?6:14,display[6],PAL_UI_GOLD+7);

        }

    }
    else if(tab==1){
        ui_panel(16,y,UI_W-32,UI_H-y-46);
        ui_text(32,y+16,"WALLPAPER",PAL_UI_CYAN+7);
        ui_control(50,32,y+48,160,"Dawn dunes",0);
        ui_control(51,32,y+88,160,"Night dunes",0);
        ui_control(52,32,y+128,160,"Warm dunes",0);
        ui_text(216,y+48,"Files and icons stay on disk.",PAL_UI_MUTED);
        ui_text(216,y+80,"Right-click desktop for actions.",PAL_UI_MUTED);
        ui_control(53,32,y+180,176,"Reload desktop",0);

    }
    else{

        ui_control(60,16,y,80,"Add",0);
        ui_control(61,104,y,88,"Remove",0);
        ui_control(62,200,y,112,"Shortcut",0);
        ui_control(63,320,y,88,"Reload",0);
        int rows=(UI_H-y-126)/28;
        if(rows<1)rows=1;
        if(selected<first)first=selected;
        if(selected>=first+rows)first=selected-rows+1;
        ui_panel(16,y+44,UI_W-32,rows*28+8);
        for(int r=0;r<rows&&first+r<count;r++){
            int i=first+r,yy=y+48+r*28;
            if(i==selected)ui_rect(20,yy,UI_W-40,28,PAL_UI_NIGHT+6);
            ui_text(32,yy+6,labels[i],PAL_UI_TEXT);
            if(UI_W>720){
                char s[48];
                copy(s,commands[i],48);
                ui_text(288,yy+6,s,PAL_UI_MUTED);

            }
            if((ui_pressed&1)&&ui_hit(20,yy,UI_W-40,28))selected=i;

        }
        ui_control(64,16,UI_H-70,80,"Prev",0);
        ui_control(65,104,UI_H-70,80,"Next",0);

    }
    ui_footer(status);
}
int main(void)
{

    if(ui_open("Settings")<0)return 1;
    sc_display(display);
    scale_choice=display[2];
    for(int i=0;i<6;i++)if(widths[i]==(int)display[0]&&heights[i]==(int)display[1])mode=i;
    menus_read();
    for(;;){
        if(!ui_frame_due())continue;
        ui_pointer();
        draw();
        ui_present();
        int key=sc_key(),a=ui_action;
        if(key==27){
            if(display[6])sc_display_apply(0,0,0,2);
            else return 0;

        }
        if(a>=10&&a<=12)tab=a-10;
        if(a>=20&&a<26)mode=a-20;
        if(a>=30&&a<=32)scale_choice=100+(a-30)*50;
        if(a==40||key=='p')show_result(sc_display_apply(widths[mode],heights[mode],scale_choice,0),"Preview active: Keep or Revert");
        if(a==41||key=='k')show_result(sc_display_apply(0,0,0,1),"Display saved for next boot");
        if(a==42||key=='r')show_result(sc_display_apply(0,0,0,2),"Previous display restored");
        if(a>=50&&a<=52)show_result(sc_wallpaper(a-50),"Wallpaper saved");
        if(a==53||a==63){
            show_result(sc_reload(),"Desktop reloaded");
            menus_read();

        }
        if(a==60)add_menu();
        if(a==61&&count&&ui_confirm("Remove menu entry?",labels[selected])){
            for(int i=selected;i+1<count;i++){

                copy(labels[i],labels[i+1],32);
                copy(commands[i],commands[i+1],128);
                copy(icons[i],icons[i+1],64);

            }
            count--;
            if(selected>=count)selected=count?count-1:0;
            if(!menus_save())menus_read();

        }
        if(a==62)shortcut();
        if(a==64&&selected)selected--;
        if(a==65&&selected+1<count)selected++;

    }
}
