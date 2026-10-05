/* =====================================================================
 * mio：主题协议验收程序。配置通过真实SandFS写入，再调用公开主题
 * API加载；从不修改内核变量。失败必须保留整份有效快照，成功须有
 * 同名角色、合法路径与变更代数。inspect模式只查询，供真实重启检查。
 * 图形窗口及数据留给无头工具只读观测，Esc走正常EXIT资源回收。
 * ===================================================================== */
#include "SCAPI.H"
volatile u32 theme_probe[40];
static u32 snapshot[32],latest[32];
static void check(int condition,int number)
{ if(!condition){theme_probe[1]++;theme_probe[2]=(u32)number;} }
static void same_snapshot(int number)
{
    sc_theme(latest);
    for(int i=0;i<32;i++)check(snapshot[i]==latest[i],number);
}
static void bad(const char *text,int number)
{
    int n=length(text);sc_theme(snapshot);
    check(sc_write("HOME/TBAD.CFG",text,n)==n,number);
    check(sc_theme_load("HOME/TBAD.CFG",0)==-1,number);
    same_snapshot(number);
}
static void show(int stage)
{
    sc_theme(latest);
    for(int i=0;i<32;i++)theme_probe[8+i]=latest[i];
    int win=sc_open_rgb("THEME",600,320),info[5];
    check(win>0,40);if(win<0)return;
    sc_info(win,info);
    sc_fill_rgb(win,0,0,info[2],info[3],latest[8+SC_THEME_FACE]);
    sc_text_rgb(win,24,24,"M8 / named theme configuration",latest[8+SC_THEME_TEXT]);
    sc_text_rgb(win,24,62,theme_probe[1]?"Theme checks FAILED":"Theme checks PASS",latest[8+SC_THEME_ACCENT]);
    sc_text_rgb(win,24,100,latest[1]?"Classic / real bevel controls":"Aurora / white surfaces",latest[8+SC_THEME_TEXT]);
    sc_text_rgb(win,24,150,"Esc: close and reclaim window",latest[8+SC_THEME_TEXT]);
    theme_probe[0]=(u32)stage;
    while(sc_key()!=27)sc_yield();
}
int main(void)
{
    char args[64],name[64];sc_args(args,64);
    if(equal(args,"inspect")){show(2);return (int)theme_probe[1];}
    check(sc_theme((u32 *)0x200000)==-1,1);
    check(sc_theme_path(2,(char *)0x200000,64)==-1,2);
    check(sc_theme_path(3,name,64)==-1,3);
    check(sc_theme_path(2,name,0)==-1 && sc_theme_path(2,name,65)==-1,4);
    sc_theme(snapshot);name[0]='!';
    check(sc_theme_path(2,name,1)==-1 && name[0]=='!',5);
    check(sc_theme_load("HOME/NOT-FOUND.CFG",0)==-2,6);same_snapshot(6);
    check(sc_theme_load((char *)0x200000,0)==-1,7);same_snapshot(7);
    bad("OTHER1MIO\nstyle=classic\n",8);
    bad("STHEME1MIO\nstyle=classic\nstyle=aurora\n",9);
    bad("STHEME1MIO\nunknown=123\n",10);
    bad("STHEME1MIO\nstyle=other\n",11);
    bad("STHEME1MIO\ntext=FF\n",12);
    bad("STHEME1MIO\ntext=1234567\n",13);
    bad("STHEME1MIO\ntext=xyz123\n",14);
    bad("STHEME1MIO\nradius=21\n",15);
    bad("STHEME1MIO\nradius=-1\n",16);
    bad("STHEME1MIO\nname=abcdefghijklmnopqrstuvwxyz012345\n",17);
    bad("STHEME1MIO\nwallpaper=../../outside\n",18);
    /* 嵌入NUL和非法UTF-8直接以字节写入，避免字符串长度函数隐藏
     * 故障尾部。主题解析不能接受“首个NUL前合法”的一半配置。 */
    char corrupt[64];copy(corrupt,"STHEME1MIO\nname=valid\n",64);
    int n=length(corrupt);corrupt[n++]=0;corrupt[n++]='!';
    sc_write("HOME/TBAD.CFG",corrupt,n);sc_theme(snapshot);
    check(sc_theme_load("HOME/TBAD.CFG",0)==-1,19);same_snapshot(19);
    copy(corrupt,"STHEME1MIO\nname=",64);n=length(corrupt);corrupt[n++]=(char)0xFF;corrupt[n++]='\n';
    sc_write("HOME/TBAD.CFG",corrupt,n);
    check(sc_theme_load("HOME/TBAD.CFG",0)==-1,20);same_snapshot(20);
    const char *custom="STHEME1MIO\n# style deliberately last\ntext=#173247\nname=Classic Proof\niconroot=SYS//ICONS/./CLASSIC\nwallpaper=HOME/../HOME/TEST.SCB\nradius=20\nstyle=classic\n";
    n=length(custom);sc_write("HOME/TGOOD.CFG",custom,n);
    check(sc_theme_load("HOME/TGOOD.CFG",0)==0,21);sc_theme(latest);
    check(latest[0]==1 && latest[1]==1 && latest[5]==0 && latest[8+SC_THEME_TEXT]==SC_RGB_INK,22);
    check(latest[2]!=snapshot[2],23);
    check(sc_theme_path(2,name,64)==13 && equal(name,"Classic Proof"),24);
    check(sc_theme_path(1,name,64)>0 && equal(name,"SYS/ICONS/CLASSIC"),25);
    check(sc_theme_path(0,name,64)>0 && equal(name,"HOME/TEST.SCB"),26);
    check(sc_theme_load("SYS/THEMES/AURORA.CFG",0)==0,27);sc_theme(latest);
    check(latest[1]==0 && latest[8+SC_THEME_PAPER]==SC_RGB_PAPER,28);
    check(sc_theme_load("SYS/THEMES/CLASSIC.CFG",1)==0,29);
    check(sc_theme_load((char *)0xFFFFFFFFu,2)==0,30); /* 固定路径重读不使用EBX */
    sc_theme(latest);check(latest[1]==1,31);
    show(1);return (int)theme_probe[1];
}
