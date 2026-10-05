/* mio：配置纯检查的实际三环探针。候选是正常FSWRITE创建，不注入
 * 内核结构。每次检查前后对照主题/显示代数与持久文件，确保“检查”
 * 不是临时加载的另一名字；保留原RELOAD返回码与核心保护。 */
#include "SCAPI.H"
static volatile u32 config_probe[8];
static u32 before_theme[32],after_theme[32],before_display[8],after_display[8];
static char saved_menu[8192],saved_user[4096],scratch[8192];
static void check(int condition,int location)
{if(!condition){config_probe[1]++;config_probe[2]=location;}}
static void candidate(int kind,const char *text,int expected,int location)
{
    int n=length(text);check(sc_write("HOME/CFG-PROBE.TXT",text,n)==n,location);
    check(sc_config_check(kind,"HOME/CFG-PROBE.TXT")==expected,location+1);
}
int main(void)
{
    sc_theme(before_theme);sc_display(before_display);
    int menu_n=sc_read("SYS/MENU.CFG",saved_menu,sizeof(saved_menu));
    int user_n=sc_read("SYS/USER.CFG",saved_user,sizeof(saved_user));
    check(menu_n>0&&user_n>0,10);
    candidate(0,"SMENU1MIO\nName|apps/hello.scx|@auto\n",0,20);
    candidate(0,"SMENU1MIO\nName|apps/hello.scx\n",-1,22);
    candidate(0,"SMENU1MIO\n|apps/hello.scx|\n",-1,24);
    candidate(1,"Name\napps/hello.scx\n@auto\n",0,30);
    candidate(1,"Name\napps/hello.scx\n@auto\nextra\n",-1,32);
    candidate(1,"\napps/hello.scx\n@auto\n",-1,34);
    check(sc_config_check(2,"SYS/THEMES/AURORA.CFG")==0,40);
    candidate(2,"STHEME1MIO\nstyle=classic\nname=Test\n",0,42);
    candidate(2,"STHEME1MIO\nstyle=classic\nstyle=aurora\n",-1,44);
    candidate(2,"STHEME1MIO\ntext=GGHHII\n",-1,46);
    candidate(3,"SCFG1MIO\nwidth=1920\nheight=1080\nscale=200\n",0,50);
    candidate(3,"SCFG1MIO\r\n# note\r\nwidth=640\r\nheight=480\r\nscale=150\r\n",0,52);
    candidate(3,"SCFG1MIO\nwidth=640\nheight=480\nscale=150\nwidth=800\n",-1,54);
    candidate(3,"SCFG1MIO\nwidth=639\nheight=480\nscale=100\n",-1,56);
    candidate(3,"SCFG1MIO\nwidth=640\nheight=480\nscale=100\nother=1\n",-1,58);
    candidate(4,"2\r\n",0,60);candidate(4,"3",-1,62);candidate(4,"1hidden",-1,64);
    check(sc_config_check(5,"HOME/CFG-PROBE.TXT")==-1,70);
    check(sc_config_check(0,(const char *)0x10000)==-1,71);
    check(sc_config_check(0,"HOME/NOT-EXIST.CFG")==-2,72);
    char hidden[]={ 'S','M','E','N','U','1','M','I','O','\n',0,'x' };
    check(sc_write("HOME/CFG-PROBE.TXT",hidden,sizeof(hidden))==sizeof(hidden),73);
    check(sc_config_check(0,"HOME/CFG-PROBE.TXT")==-1,74);
    sc_theme(after_theme);sc_display(after_display);
    for(int i=0;i<32;i++)check(before_theme[i]==after_theme[i],80+i);
    for(int i=0;i<8;i++)check(before_display[i]==after_display[i],120+i);
    int n=sc_read("SYS/MENU.CFG",scratch,sizeof(scratch));check(n==menu_n,130);
    for(int i=0;i<n;i++)check(scratch[i]==saved_menu[i],131);
    n=sc_read("SYS/USER.CFG",scratch,sizeof(scratch));check(n==user_n,132);
    for(int i=0;i<n;i++)check(scratch[i]==saved_user[i],133);
    check(sc_write("SYS/CORE/CORE.SKM","bad",3)==-5,134);
    sc_remove("HOME/CFG-PROBE.TXT");config_probe[0]=1;
    int window=sc_open("Config probe",306,150);
    sc_text(window,8,36,config_probe[1]?"CONFIG CHECK FAILED":"PURE CONFIG CHECK PASS",PAL_TITLE);
    while(sc_key()!=27)sc_yield();return config_probe[1]?1:0;
}
