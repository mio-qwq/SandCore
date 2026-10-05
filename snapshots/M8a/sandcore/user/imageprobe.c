/* mio：普通三环探针验证真实SCX入口对内置图标的拒绝边界。
 * 不从宿主调用内核函数；每个损坏容器先写文件，再通过EXEC执行入口。
 * 编码合法的图标应仅作为容器资源，不进入应用映像。真正正常启动、
 * Lens透明合成与旧程序运行由宿主的真实输入矩阵另外验证。 */
#include "SCAPI.H"
#include "IMAGE.inc"
static u8 original[4096],container[4096];
static int original_size,failures;
static volatile u32 image_probe[16];
static void check(int yes,int number)
{if(!yes){failures++;image_probe[2]=(u32)number;}}
static int prepare(void)
{
    for(int i=0;i<original_size;i++)container[i]=original[i];
    image_put32(container+24,2);
    u8 *h=container+original_size;
    const char *magic="SCB2MIO";for(int i=0;i<8;i++)h[i]=(u8)magic[i];
    image_put32(h+8,2);image_put32(h+12,1);image_put32(h+16,2);
    image_put32(h+20,0);image_put32(h+24,8);image_put32(h+28,0x004F494D);
    image_put32(h+32,0xFF000000u|SC_RGB_INK);
    image_put32(h+36,0x80000000u|SC_RGB_ACCENT);
    return original_size+40;
}
static void rejected(int bytes,int number)
{
    check(sc_write("HOME/BADICON.SCX",container,bytes)==bytes,100+number);
    check(sc_exec("HOME/BADICON.SCX")==-3,number);
}
int main(void)
{
    int window=sc_open_rgb("Image protocol",480,140);if(window<0)return 1;
    char args[32];sc_args(args,sizeof(args));
    if(equal(args,"classic")){
        check(sc_theme_load("SYS/THEMES/CLASSIC.CFG",0)==0,18);
        image_probe[1]=(u32)failures;image_probe[0]=2;
        sc_fill_rgb(window,0,0,480,140,SC_RGB_WHITE);
        sc_text_rgb(window,16,22,"SCX identity / Classic",SC_RGB_INK);
        while(sc_key()!=27)sc_yield();
        return failures;
    }
    original_size=sc_read("apps/hello.scx",original,sizeof(original));
    check(original_size>36 && original_size<3000,1);
    if(original_size<=36 || original_size>=3000)return 1;
    int n=prepare();image_put32(container+24,4);rejected(n,2);
    n=prepare();image_put32(container+24,0);rejected(n,3);
    n=prepare();rejected(original_size,4);
    n=prepare();container[original_size+3]='1';rejected(n,5);
    n=prepare();image_put32(container+original_size+8,0);rejected(n,6);
    n=prepare();image_put32(container+original_size+8,129);rejected(n,7);
    n=prepare();image_put32(container+original_size+16,1);rejected(n,8);
    n=prepare();image_put32(container+original_size+20,1);rejected(n,9);
    n=prepare();image_put32(container+original_size+24,7);rejected(n,10);
    n=prepare();image_put32(container+original_size+28,0);rejected(n,11);
    n=prepare();rejected(n-1,12);
    n=prepare();container[n]=0;rejected(n+1,13);
    n=prepare();check(sc_write("HOME/ICONHELLO.SCX",container,n)==n,14);
    const char *embedded="Embedded\nHOME/ICONHELLO.SCX\n\n";
    const char *fixed="Fixed\nHOME/ICONHELLO.SCX\nSYS/ICONS/AURORA/LENS.SCB\n";
    const char *themed="Themed\nHOME/ICONHELLO.SCX\n@LENS\n";
    check(sc_write("DESK/I-EMBED.LNK",embedded,length(embedded))==length(embedded),19);
    check(sc_write("DESK/I-FIXED.LNK",fixed,length(fixed))==length(fixed),20);
    check(sc_write("DESK/I-THEME.LNK",themed,length(themed))==length(themed),21);
    check(sc_reload()==0,22);
    /* TIDAL资源位于旧8MB边界之后：STAT+READAT应能读取真实SCB2头，
     * 不是只在宿主扩文件、内核继续以旧容量截断。 */
    u32 stat[2];u8 h[32];
    check(sc_stat("SYS/WALL/TIDAL.SCB",stat)==0 && stat[0]==1,15);
    check(sc_read_at("SYS/WALL/TIDAL.SCB",h,32,0)==32 && image_scb2(h),16);
    check(sc_stat("LEGACY/BIN/SHELL.SCX",stat)==0 && stat[0]==1,17);
    image_probe[1]=(u32)failures;image_probe[0]=1;
    sc_fill_rgb(window,0,0,480,140,SC_RGB_WHITE);
    sc_text_rgb(window,16,22,failures?"SCB2 / SCX icon FAILED":"SCB2 / SCX icon boundaries PASS",SC_RGB_INK);
    sc_text_rgb(window,16,54,"64MB data / LEGACY checked",SC_RGB_ACCENT);
    while(sc_key()!=27)sc_yield();
    return failures;
}
