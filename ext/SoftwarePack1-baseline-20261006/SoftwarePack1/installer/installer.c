/* =====================================================================
 * installer.c —— SandCore_ExtraSoftware_Pack_1 总安装器（本包唯一
 * 交付物：一个 SCX，应用与图标全部内嵌）。
 *
 * 职责
 *   - 向导页：欢迎 -> 组件勾选 -> 选项（安装目录/快捷方式/默认
 *     配置/程序菜单项）-> 执行进度 -> 完成；检测到已安装应用时
 *     首页即提供"卸载"入口。
 *   - 安装步骤（每应用）：CRC32 复核内嵌 SCX -> 写 <目录>/NAME.SCX
 *     -> 图标落 SYS/ICONS/NAME.SCB -> 配置缺失时写默认值（更新
 *     绝不覆盖用户配置）-> DESK/NAME.LNK 三行快捷方式 -> 可选
 *     程序菜单项（候选文件 + sc_config_check 纯校验后才替换
 *     SYS/MENU.CFG）。全部完成后 sc_reload() 刷新桌面。
 *   - 卸载：删除 SCX/快捷方式/图标，可选删配置；菜单项按命令
 *     字段过滤剔除（同样先校验后替换）。
 *
 * 文件名合同（docs/CONFIG.md、IMAGE.md、FS.md）：快捷方式三行
 * 31/127/63B；菜单 SMENU1MIO 8191B/32 条/每行三字段；SCX 载荷与
 * SCB2 图标结构构建期已校验，运行期只复核 CRC 与写入回执。
 * ===================================================================== */
#include "SCAPI.H"
#include "NUI.inc"
#include "exui_ext.inc"
#include "payload.h"
#include "exutil.h"

#define INS_PACK "SandCore_ExtraSoftware_Pack_1"
#define INS_VER  "1.0"
#define INS_MAX  8

#define PG_WELCOME 0
#define PG_COMPS   1
#define PG_OPTIONS 2
#define PG_RUN     3
#define PG_DONE    4
#define PG_REMOVE  5
#define PG_REMOVED 6

#define ID_NEXT    1
#define ID_BACK    2
#define ID_QUIT    3
#define ID_DOIT    4
#define ID_BROWSE  5
#define ID_REMOVE  6
#define ID_UNINST  7
#define ID_COMP0   10
#define ID_SHORTCUT 20
#define ID_CONFIG  21
#define ID_MENU    22
#define ID_DELCFG  23

static int ins_page;
static int ins_sel[INS_MAX];
static int ins_shortcut=1,ins_config=1,ins_menu,ins_delcfg;
static char ins_dir[64]="/APPS";
static char ins_hint[64];          /* 选项页错误提示 */
static int ins_progress;           /* 0..1000 */
static char ins_log[INS_MAX][48];  /* 执行结果逐行 */
static int ins_done,ins_fail;
static int ins_installed_mask;     /* 启动时检测到的已装应用 */

/* ---------------- 小工具 ---------------- */
static u32 ins_crc32(const u8 *data,u32 n)
{
    u32 crc=0xFFFFFFFFu;
    for(u32 i=0;i<n;i++){
        crc^=data[i];
        for(int k=0;k<8;k++)
            crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1)));
    }
    return ~crc;
}
/* 目录规范化：去根斜杠与尾斜杠（SandFS 路径无根斜杠） */
static void ins_norm_dir(char *d)
{
    int n=length(d);
    while(n>0&&(d[n-1]=='/'||d[n-1]=='\\'))d[--n]=0;
    if(d[0]=='/'){for(int i=0;i<n;i++)d[i]=d[i+1];n--;}
    if(n&&(d[0]=='A')&&0){}
}
static void ins_join(char *out,int cap,const char *dir,const char *name)
{
    copy(out,dir,cap);
    if(out[0])append(out,"/",cap);
    append(out,name,cap);
}
static int ins_write_verify(const char *path,const u8 *data,u32 n)
{
    int w=sc_write(path,data,(int)n);
    if(w!=(int)n)return -1;
    u32 info[2];
    if(sc_stat(path,info)||info[0]!=1||info[1]!=n)return -2;
    return 0;
}
static int ins_detect_installed(int i)
{
    char path[64];
    ins_join(path,sizeof(path),ins_dir,EXAPP[i].name);
    append(path,".SCX",64);
    u32 info[2];
    return sc_stat(path,info)==0&&info[0]==1;
}
static void ins_detect_all(void)
{
    ins_installed_mask=0;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++)
        if(ins_detect_installed(i))ins_installed_mask|=1<<i;
}

/* ---------------- 安装核心 ---------------- */
/* 菜单项：候选 -> CONFIGCHECK -> 替换。任何一步失败都跳过并留提示，
 * 绝不把没通过校验的菜单写进 SYS。 */
static void ins_menu_entry(int i,int add)
{
    char path[64];
    ins_join(path,sizeof(path),ins_dir,EXAPP[i].name);
    append(path,".SCX",64);
    u8 buf[8192];
    int n=sc_read("SYS/MENU.CFG",buf,sizeof(buf)-1);
    if(n<0)return;                              /* 无菜单文件：跳过 */
    buf[n]=0;
    char keep[8192];
    int kn=0;
    char *p=(char *)buf;
    int changed=0;
    while(*p){
        char *line=p;
        while(*p&&*p!='\n')p++;
        int len=(int)(p-line);
        if(*p)p++;
        int mine=0;
        for(int k=0;k+4<len;k++)                /* 命令字段含本应用 SCX 名 */
            if(line[k]==EXAPP[i].name[0]){
                int match=1;
                int j=k,m=0;
                while(m<length(EXAPP[i].name)&&j<len){
                    char a=line[j],b=EXAPP[i].name[m];
                    if(ex_upper(a)!=ex_upper(b)){match=0;break;}
                    j++;m++;
                }
                if(match&&m==length(EXAPP[i].name)){mine=1;break;}
            }
        if(add||!mine){
            if(kn+len+1>=(int)sizeof(keep))return;
            for(int k=0;k<len;k++)keep[kn++]=line[k];
            keep[kn++]='\n';
        }else changed=1;
    }
    if(add){
        /* 追加一条：名字 命令 图标 */
        char row[192];
        copy(row,EXAPP[i].label,sizeof(row));
        append(row," /",sizeof(row));
        append(row,path,sizeof(row));
        append(row," /SYS/ICONS/",sizeof(row));
        append(row,EXAPP[i].name,sizeof(row));
        append(row,".SCB",sizeof(row));
        append(row,"\n",sizeof(row));
        if(kn+length(row)+1>=(int)sizeof(keep))return;   /* 菜单满 */
        for(const char *q=row;*q;q++)keep[kn++]=*q;
        changed=1;
    }
    if(!changed)return;
    keep[kn]=0;
    /* 候选文件 -> 纯校验 -> 通过才替换 */
    char cand[64]="HOME/PACK1-MENU.NEW";
    if(sc_write(cand,keep,kn)!=kn)return;
    if(sc_config_check(0,cand)!=0){sc_remove(cand);return;}
    if(sc_write("SYS/MENU.CFG",keep,kn)!=kn){sc_remove(cand);return;}
    sc_remove(cand);
}
static void ins_install_app(int i)
{
    char path[64],icon[64];
    const exapp_entry *a=&EXAPP[i];
    /* 1) 内嵌载荷完整性 */
    if(ins_crc32(a->scx,a->scx_size)!=a->scx_crc){
        copy(ins_log[i],"FAIL bad payload CRC",48);
        ins_fail++;
        return;
    }
    /* 2) 程序本体 */
    ins_join(path,sizeof(path),ins_dir,a->name);
    append(path,".SCX",64);
    int r=ins_write_verify(path,a->scx,a->scx_size);
    if(r){copy(ins_log[i],"FAIL write scx",48);ins_fail++;return;}
    /* 3) 图标 */
    copy(icon,"SYS/ICONS/",sizeof(icon));
    append(icon,a->name,64);
    append(icon,".SCB",64);
    if(ins_write_verify(icon,a->icon,a->icon_size)){
        copy(ins_log[i],"FAIL write icon",48);ins_fail++;return;
    }
    /* 4) 默认配置（只缺省时写，更新不覆盖用户配置） */
    char cfg[64]="HOME/";
    append(cfg,a->cfg_name,64);
    u32 info[2];
    int have=sc_stat(cfg,info)==0&&info[0]==1;
    if(!have&&ins_config){
        if(sc_write(cfg,(const u8 *)a->cfg,length(a->cfg))!=
           (int)length(a->cfg)){
            copy(ins_log[i],"FAIL write config",48);ins_fail++;return;
        }
    }
    /* 5) 桌面快捷方式：标签/命令/图标 三行 */
    if(ins_shortcut){
        char lnk[192];
        copy(lnk,a->label,sizeof(lnk));
        append(lnk,"\n",sizeof(lnk));
        append(lnk,path,sizeof(lnk));
        append(lnk,"\n",sizeof(lnk));
        append(lnk,icon,sizeof(lnk));
        append(lnk,"\n",sizeof(lnk));
        char lp[64]="DESK/";
        append(lp,a->name,64);
        append(lp,".LNK",64);
        if(sc_write(lp,lnk,length(lnk))!=(int)length(lnk)){
            copy(ins_log[i],"FAIL write shortcut",48);ins_fail++;return;
        }
    }
    /* 6) 程序菜单项（可选；校验不过自动跳过） */
    if(ins_menu)ins_menu_entry(i,1);
    copy(ins_log[i],"OK",48);
    {
        char s[48];
        ex_udec(s,sizeof(s),a->scx_size);
        append(ins_log[i]," ",48);
        append(ins_log[i],s,48);
        append(ins_log[i],"B",48);
    }
    ins_done++;
}
static void ins_remove_app(int i)
{
    const exapp_entry *a=&EXAPP[i];
    char path[64];
    ins_join(path,sizeof(path),ins_dir,a->name);
    append(path,".SCX",64);
    sc_remove(path);
    copy(path,"DESK/",sizeof(path));
    append(path,a->name,64);
    append(path,".LNK",64);
    sc_remove(path);
    copy(path,"SYS/ICONS/",sizeof(path));
    append(path,a->name,64);
    append(path,".SCB",64);
    sc_remove(path);
    if(ins_delcfg){
        copy(path,"HOME/",sizeof(path));
        append(path,a->cfg_name,64);
        sc_remove(path);
    }
    ins_menu_entry(i,0);
    copy(ins_log[i],"removed",48);
    ins_done++;
}

/* ---------------- 页面绘制 ---------------- */
static void ins_header_page(const char *title,const char *sub)
{ui_header(title,sub);}

static void ins_draw_welcome(void)
{
    ins_header_page(INS_PACK,"installer / version " INS_VER);
    int any=0;
    for(int i=0;i<EXAPP_COUNT;i++)any|=(ins_installed_mask>>i)&1;
    int y=ui_compact?34:80;
    /* 应用图标横排展示：包的第一眼 */
    int icon_n=EXAPP_COUNT;
    int spacing=64;
    int x0=UI_W/2-(icon_n*spacing)/2+16;
    for(int i=0;i<icon_n;i++)
        ex_icon_draw(EXAPP[i].icon,EXAPP[i].icon_size,x0+i*spacing,y,2);
    y+=64;
    {
        char line[96];
        copy(line,"This wizard installs ",sizeof(line));
        char n[8];ex_dec(n,sizeof(n),EXAPP_COUNT);
        append(line,n,sizeof(line));
        append(line," user applications:",sizeof(line));
        ui_text(24,y+=22,line,PAL_UI_TEXT);
    }
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        char line[96];
        copy(line,"  ",sizeof(line));
        append(line,EXAPP[i].label,sizeof(line));
        append(line," - ",sizeof(line));
        append(line,EXAPP[i].desc,sizeof(line));
        ui_text(24,y+=20,line,PAL_UI_TEXT);
    }
    if(any){
        y+=24;
        ui_text(24,y,"Some components are already installed.",PAL_UI_GOLD+7);
        ui_control(ID_UNINST,24,y+24,140,"Uninstall...",0);
        ui_control(ID_NEXT,UI_W-160,y+24,120,"Next",1);
    }else ui_control(ID_NEXT,UI_W-160,UI_H-64,120,"Next",1);
    ui_control(ID_QUIT,24,UI_H-64,100,"Quit",0);
    ui_footer("SandCore extension software by the ext project");
}
static void ins_draw_comps(void)
{
    ins_header_page(INS_PACK,"select components");
    int y=ui_compact?34:76;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        int row_h=56;
        if(ex_row(ID_COMP0+i,16,y,UI_W-32,row_h))ins_sel[i]^=1;
        ex_check(ID_COMP0+INS_MAX+i,24,y+4,"",&ins_sel[i]);
        ex_icon_draw(EXAPP[i].icon,EXAPP[i].icon_size,64,y+4,2);
        char line[96];
        copy(line,EXAPP[i].label,sizeof(line));
        ui_text(120,y+4,line,PAL_UI_TEXT);
        copy(line,EXAPP[i].desc,sizeof(line));
        ui_text(120,y+22,line,PAL_UI_MUTED);
        char size[16];
        ex_udec(size,sizeof(size),EXAPP[i].scx_size);
        copy(line,size,sizeof(line));
        append(line," B",sizeof(line));
        if(ins_detect_installed(i))append(line,"  installed",sizeof(line));
        ui_text(120,y+38,line,PAL_UI_MUTED);
        y+=row_h+6;
    }
    ui_control(ID_BACK,24,UI_H-64,100,"Back",0);
    ui_control(ID_NEXT,UI_W-160,UI_H-64,120,"Next",1);
    ui_footer("Click a row to toggle the component");
}
static void ins_draw_options(void)
{
    ins_header_page(INS_PACK,"install options");
    int y=ui_compact?34:80;
    ui_text(24,y,"Install directory:",PAL_UI_TEXT);
    ui_rect(24,y+20,UI_W-200,28,PAL_UI_INK);
    ui_text(32,y+27,ins_dir,PAL_UI_TEXT);
    ui_control(ID_BROWSE,UI_W-168,y+20,140,"Browse...",0);
    y+=64;
    ex_check(ID_SHORTCUT,24,y,"Create desktop shortcuts",&ins_shortcut);
    ex_check(ID_CONFIG,24,y+26,"Write default config files (keep existing)",
             &ins_config);
    ex_check(ID_MENU,24,y+52,"Add programs-menu entries (validated)",&ins_menu);
    if(ins_hint[0])ui_text(24,y+84,ins_hint,PAL_UI_ALERT);
    ui_control(ID_BACK,24,UI_H-64,100,"Back",0);
    ui_control(ID_DOIT,UI_W-160,UI_H-64,120,"Install",1);
    ui_footer("Default /APPS; SandFS paths have no leading slash");
}
static void ins_draw_run(void)
{
    ins_header_page(INS_PACK,"working...");
    int y=ui_compact?34:80;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        char line[64];
        copy(line,EXAPP[i].label,sizeof(line));
        ui_text(24,y,line,PAL_UI_TEXT);
        if(ins_log[i][0])ui_text(160,y,ins_log[i],
            ins_fail&&ins_log[i][0]=='F'?PAL_UI_ALERT:PAL_UI_MUTED);
        y+=20;
    }
    ex_progress(24,y+10,UI_W-48,18,ins_progress);
    ui_footer("Installing, please wait");
}
static void ins_draw_done(void)
{
    ins_header_page(INS_PACK,ins_fail?"finished with errors":"installed");
    int y=ui_compact?34:80;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        if(!ins_log[i][0])continue;
        char line[96];
        copy(line,EXAPP[i].label,sizeof(line));
        append(line,"  ",sizeof(line));
        append(line,ins_log[i],sizeof(line));
        ui_text(24,y,line,
            ins_log[i][0]=='F'?PAL_UI_ALERT:PAL_UI_TEXT);
        y+=20;
    }
    y+=20;
    char path[64];
    copy(path,"Programs are in ",sizeof(path));
    append(path,ins_dir,sizeof(path));
    ui_text(24,y,path,PAL_UI_MUTED);
    ui_text(24,y+20,"Desktop refresh issued (sc_reload).",PAL_UI_MUTED);
    ui_control(ID_QUIT,UI_W-160,UI_H-64,120,"Finish",1);
    ui_footer("Enjoy the extensions!");
}
static void ins_draw_remove(void)
{
    ins_header_page(INS_PACK,"uninstall components");
    int y=ui_compact?34:76;
    int any=0;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        if(!((ins_installed_mask>>i)&1))continue;
        any=1;
        if(ex_row(ID_COMP0+i,16,y,UI_W-32,24))ins_sel[i]^=1;
        ex_check(ID_COMP0+INS_MAX+i,24,y+2,"",&ins_sel[i]);
        ui_text(64,y+4,EXAPP[i].label,PAL_UI_TEXT);
        y+=28;
    }
    if(!any)ui_text(24,y,"Nothing installed in the target directory.",
                    PAL_UI_MUTED);
    y+=40;
    ex_check(ID_DELCFG,24,y,"Also remove configuration files",&ins_delcfg);
    ui_control(ID_BACK,24,UI_H-64,100,"Back",0);
    if(any)ui_control(ID_REMOVE,UI_W-200,UI_H-64,160,"Remove selected",0);
    ui_footer("Shortcuts, icons and menu entries are cleaned up too");
}
static void ins_draw_removed(void)
{
    ins_header_page(INS_PACK,"uninstalled");
    int y=ui_compact?34:80;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        if(!ins_log[i][0])continue;
        char line[96];
        copy(line,EXAPP[i].label,sizeof(line));
        append(line,"  ",sizeof(line));
        append(line,ins_log[i],sizeof(line));
        ui_text(24,y,line,PAL_UI_TEXT);
        y+=20;
    }
    ui_control(ID_QUIT,UI_W-160,UI_H-64,120,"Finish",1);
    ui_footer("Desktop refresh issued");
}

/* ---------------- 动作 ---------------- */
static int ins_selected_any(void)
{
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++)if(ins_sel[i])return 1;
    return 0;
}
static void ins_run_install(void)
{
    ins_page=PG_RUN;
    ins_progress=0;
    ins_done=ins_fail=0;
    for(int i=0;i<INS_MAX;i++)ins_log[i][0]=0;
    int total=0;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++)if(ins_sel[i])total++;
    int at=0;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++){
        if(!ins_sel[i])continue;
        ins_install_app(i);
        at++;
        ins_progress=at*1000/(total?total:1);
        ui_followup=1;
        sc_reload();                            /* 每步后桌面可用 */
    }
    ins_page=PG_DONE;
}
static void ins_run_remove(void)
{
    ins_page=PG_RUN;
    ins_done=ins_fail=0;
    for(int i=0;i<INS_MAX;i++)ins_log[i][0]=0;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++)
        if(ins_sel[i]&&((ins_installed_mask>>i)&1))ins_remove_app(i);
    sc_reload();
    ins_detect_all();
    ins_page=PG_REMOVED;
}
static void ins_browse_dir(void)
{
    /* 目录选择复用文件对话框：选中任意文件后取其目录。
     * 这是 ui_edit_path 语义下的最稳方案，不引新接口。 */
    char path[64];
    copy(path,ins_dir,sizeof(path));
    if(path[0])append(path,"/x",sizeof(path));
    else copy(path,"HOME/x",sizeof(path));
    if(!ui_edit_path(path,sizeof(path),"Pick any file inside target dir"))
        return;
    int n=length(path);
    while(n&&path[n-1]!='/')n--;
    if(n)path[n-1]=0;
    if(!path[0])copy(path,"HOME",64);
    ins_norm_dir(path);
    if(length(path)<64){
        copy(ins_dir,path,sizeof(ins_dir));
        ins_hint[0]=0;
    }else copy(ins_hint,"Directory path too long",sizeof(ins_hint));
    ui_followup=1;
}

static void ins_action(int id)
{
    switch(id){
    case ID_QUIT:ui_followup=1;break;           /* main 里决定退出 */
    case ID_NEXT:
        ins_page=ins_page==PG_WELCOME?PG_COMPS:PG_OPTIONS;
        break;
    case ID_BACK:ins_page=ins_page==PG_OPTIONS?PG_COMPS:PG_WELCOME;break;
    case ID_BROWSE:ins_browse_dir();break;
    case ID_DOIT:
        if(!ins_selected_any()){
            copy(ins_hint,"Select at least one component first",
                 sizeof(ins_hint));
            break;
        }
        ins_run_install();
        break;
    case ID_UNINST:ins_page=PG_REMOVE;break;
    case ID_REMOVE:
        if(!ins_selected_any())break;
        if(ui_confirm("Uninstall selected components?",
                      "Shortcuts and icons are removed too."))
            ins_run_remove();
        break;
    default:
        if(id>=ID_COMP0&&id<ID_COMP0+INS_MAX)ins_sel[id-ID_COMP0]^=1;
        break;
    }
    ui_followup=1;
}

/* ---------------- 主循环 ---------------- */
int main(void)
{
    if(ui_open(INS_PACK)<0)return 1;
    for(int i=0;i<EXAPP_COUNT&&i<INS_MAX;i++)ins_sel[i]=1;
    ins_detect_all();
    ui_followup=1;
    for(;;){
        if(!ui_frame_due_interval(100))continue;
        ui_pointer();
        switch(ins_page){
        case PG_WELCOME:ins_draw_welcome();break;
        case PG_COMPS:ins_draw_comps();break;
        case PG_OPTIONS:ins_draw_options();break;
        case PG_RUN:ins_draw_run();break;
        case PG_DONE:ins_draw_done();break;
        case PG_REMOVE:ins_draw_remove();break;
        case PG_REMOVED:ins_draw_removed();break;
        }
        ui_present();
        int key=ui_action?-1:sc_key();
        int quit=ui_action==ID_QUIT;
        if(ui_action&&ui_action!=ID_QUIT)ins_action(ui_action);
        else if(ui_action==ID_QUIT)quit=1;
        if(key==27){
            if(ui_confirm("Exit installer?","Nothing is installed yet."))
                quit=1;
            ui_followup=1;
        }
        if(quit)return 0;
    }
}
