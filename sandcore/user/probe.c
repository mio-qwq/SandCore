/* mio：M7 底座行为探针。每种结果由真实 CPU/内核路径产生，不调用
 * 内核“假装异常”的测试入口。输出文件供无头验收读取，窗口供截图。
 * 此探针最终由 SCCC 再编译，用于核对宿主与原生编译器行为一致性。 */
#include "SCAPI.H"
static u8 pixels[304*150];
static volatile int recovered;
static void exception(u32 *context)
{
    /* UD2 恰好两字节，修正 EIP 后恢复第一现场。能继续写文件证明
     * 不仅回调被调用，而且 EXCRETURN 的用户栈/寄存器/选择子均正确。 */
    recovered=1; context[14]+=2; sc_resume(context);
}
static int debugger(void)
{
    u32 first[22],hit[22],step[22];
    int pid=sc_dbgexec("apps/probe.scx target");
    if(pid<0 || sc_debug(pid,0,0,first,88)) return 1;
    if(sc_debug(pid,4,first[14],0,0) || sc_debug(pid,2,0,0,0)) return 2;
    while(sc_status(pid)==0x40000000) sc_yield();
    if(sc_debug(pid,0,0,hit,88) || hit[12]!=3 || hit[14]!=first[14]) return 3;
    if(sc_debug(pid,1,0,0,0)) return 4;
    while(sc_status(pid)==0x40000000) sc_yield();
    if(sc_debug(pid,0,0,step,88) || step[12]!=1 || step[14]==hit[14]) return 5;
    u8 bytes[16];
    if(sc_debug(pid,3,step[14],bytes,16)) return 6;
    if(sc_debug(pid,5,first[14],0,0) || sc_debug(pid,2,0,0,0)) return 7;
    int until=sc_tick()+10;
    while(sc_tick()<until) sc_yield();
    sc_debug(pid,6,0,0,0);
    sc_write("home/debug.ok","PASS",4);
    return 0;
}
static int filesystem(void)
{
    /* 直接调用 API，绕过文件管理器 UI 的按钮状态。别名/祖先移动均
     * 必须被内核入口拒绝，这才证明保护不是一个可以绕开的 GUI 提示。 */
    char b[512]; u32 info[2];
    if(sc_write("/sys/./CORE/../core/core.skm","X",1)!=-5) return 1;
    if(sc_remove("SYS/CORE/CORE.SKM")!=-5) return 2;
    if(sc_rename("sys","home/system")!=-5) return 3;
    if(sc_mkdir("SYS/CORE/new")!=-5) return 4;
    if(sc_read("/SYS/CORE/CORE.SKM",b,8)!=8 || b[0]!='S' || b[1]!='K' || b[2]!='M') return 5;
    if(sc_mkdir("home/game") || sc_mkdir("home/game/world")) return 6;
    if(sc_write("home/game/world/chunk.dat","MIO",3)!=3) return 7;
    if(sc_stat("home/game/world",info) || info[0]!=2) return 8;
    if(sc_dir("home/game",b,sizeof(b))<=0 || !equal(b,"D world 0\n")) return 9;
    if(sc_rename("home/game","home/play")) return 10;
    if(sc_read("/HOME//play/./world/../world/chunk.dat",b,3)!=3 || b[0]!='M') return 11;
    if(sc_remove("home/play")!=-1) return 12;
    if(sc_remove("home/play/world/chunk.dat") || sc_remove("home/play/world") || sc_remove("home/play")) return 13;
    if(sc_stat("home/play",info)!=-1) return 14;
    if(sc_write("home/result.txt","OK",2)!=2) return 15;
    if(sc_rename("home/result.txt","SYS/CORE/result.txt")!=-5) return 16;
    if(sc_rename("home/result.txt","home/result2.txt") || sc_remove("home/result2.txt")) return 17;
    sc_write("home/fs.ok","PASS",4); return 0;
}
static int fontcheck(int win)
{
    /* 文件字形逐位对照正式 API，证明不是另一套相似字体替代。
     * 原生 Unicode 码点从 SCF 的 UTF-8 标签解码；程序不假定某个
     * 硬编码字库序号。所有输出缓冲与字符串均是真实 ring3 地址。 */
    static u8 scf[40000];
    u32 info[4]; u16 rows[16];
    int n=sc_read("SYS/FONT.SCF",scf,sizeof(scf));
    if(n<12 || sc_fontinfo(info) || info[0]!=1 || info[2]!=16 || info[3]!=1) return 1;
    u32 count=*(u32 *)(scf+8);
    if(info[1]!=count || 12+count*276!=(u32)n) return 2;
    for(u32 i=0;i<count;i++) {
        u8 *entry=scf+12+i*276;
        u32 scalar=((u32)(entry[0]&15)<<12)|((u32)(entry[1]&63)<<6)|(entry[2]&63);
        if(sc_glyph16(scalar,rows)!=16) return 3;
        for(int r=0;r<16;r++) {
            u16 expected=0;
            for(int c=0;c<16;c++) if(entry[4+r*17+c]=='#') expected|=(u16)(0x8000u>>c);
            if(rows[r]!=expected) return 4;
        }
    }
    if(sc_glyph16('A',rows)!=8 || sc_glyph16(0x1F600,rows)!=0
       || rows[0]!=0xFFFF || rows[1]!=0x8001 || rows[15]!=0xFFFF) return 5;
    if(sc_glyph16(0xD800,rows)!=-1 || sc_glyph16(0x110000,rows)!=-1
       || sc_glyph16(0x6C99,(u16 *)0x100000)!=-1
       || sc_glyph16(0x6C99,(u16 *)0x7DEFF0)!=-1
       || sc_fontinfo((u32 *)0x100000)!=-1) return 6;
    if(sc_text16(win,12,60,"A\xC0\xAF",PAL_UI_TEXT)!=-2
       || sc_text16(win,12,60,"\xED\xA0\x80",PAL_UI_TEXT)!=-2
       || sc_text16(win,12,60,"\xF4\x90\x80\x80",PAL_UI_TEXT)!=-2
       || sc_text16(win,12,60,"\xE6\xB2",PAL_UI_TEXT)!=-2) return 7;
    if(sc_text16(win,0,0,(const char *)0x100000,PAL_UI_TEXT)!=-1
       || sc_text16(win+100,0,0,"沙",PAL_UI_TEXT)!=-1
       || sc_text16(win,0x7FFFFFFF,0,"沙",PAL_UI_TEXT)!=-1) return 8;
    sc_fill(win,0,0,304,150,PAL_UI_NIGHT);
    sc_text(win,12,12,"PHOENIX / UTF-8 / RING 3",PAL_UI_CYAN+7);
    if(sc_text16(win,12,35,"沙核操作系统",PAL_UI_TEXT)!=6) return 9;
    if(sc_text16(win,12,58,"欢迎来到图形的世界",PAL_UI_GOLD+7)!=9) return 10;
    if(sc_text16(win,12,83,"ASCII + \xF0\x9F\x98\x80",PAL_UI_MUTED)!=9) return 11;
    sc_text(win,12,120,"SOURCE GLYPHS + BOUNDS VERIFIED",PAL_UI_MUTED);
    sc_write("home/font.ok","PASS",4);
    return 0;
}
int main(void)
{
    char args[128]; sc_args(args,128);
    int win=sc_open("M7 Probe",306,164);
    if(win<0) return 1;
    if(equal(args,"font")) {
        int result=fontcheck(win);
        if(result) {char b[12]; decimal(b,result); sc_puts("FONT ERROR "); sc_puts(b);}
        wait_escape(); return result;
    }
    if(equal(args,"fs")) {
        int result=filesystem(); char b[12]; decimal(b,result);
        sc_puts("FILESYSTEM RESULT "); sc_puts(b); sc_puts("\n");
        wait_escape(); return result;
    }
    if(equal(args,"debug")) {
        int result=debugger(); char b[12]; decimal(b,result);
        sc_puts("DEBUG RESULT "); sc_puts(b); sc_puts("\n");
        wait_escape(); return result;
    }
    if(equal(args,"handler")) {
        sc_handler(6,exception);
        __asm__ __volatile__("ud2");
        if(recovered) { sc_write("home/handled.ok","PASS",4); sc_puts("HANDLER RESUMED\n"); }
        wait_escape(); return !recovered;
    }
    if(equal(args,"ud")) __asm__ __volatile__("ud2");
    if(equal(args,"page")) *(volatile int *)0x100000=7;
    if(equal(args,"div")) __asm__ __volatile__("xor %%ecx,%%ecx; div %%ecx":::"eax","edx","ecx","cc");
    if(equal(args,"target")) { sc_puts("DEBUG TARGET RUNNING\n"); wait_escape(); return 0; }
    int previous=-1;
    for(;;) {
        int tick=sc_tick()/3;
        if(sc_key()==27) return 0;
        if(tick!=previous) {
            previous=tick;
            for(int y=0;y<150;y++) for(int x=0;x<304;x++)
                pixels[y*304+x]=((x+tick)%80<40)?PAL_CON_TINT:PAL_WIN_TITLE;
            if(sc_frame(win,pixels,sizeof(pixels))) return 2;
        }
        sc_yield();
    }
}
