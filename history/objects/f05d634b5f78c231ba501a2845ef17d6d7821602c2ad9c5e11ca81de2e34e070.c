/* =====================================================================
 * mio：M8真彩色/高端图形堆的可运行接口示例与自动验收探针。
 * 它只调用公开SCAPI.H：像素来自本任务私有堆，内核不向应用借显存。
 * 渐变故意包含超过256种RGB，以区分“真正RGB”与槽位转换的32bpp。
 * 所有诊断结果存在本任务只读观测变量；宿主验证只读它们，不伪造状态。
 * ===================================================================== */
#include "SCAPI.H"
volatile u32 rgb_probe[24];
static void check(int condition,int number)
{
    if(!condition) { rgb_probe[1]++; rgb_probe[2]=(u32)number; }
}
int main(void)
{
    char args[128];u32 before[48],after[48],palette[256];
    sc_args(args,128);sc_palette(palette);
    int win=sc_open_rgb("TRUECOLOR",900,620);
    if(win<0)return 90;
    int info[5];sc_info(win,info);
    rgb_probe[3]=(u32)win;rgb_probe[4]=info[2];rgb_probe[5]=info[3];
    sc_monitor(before);
    rgb_probe[6]=before[3];
    if(equal(args,"oom")) {
        /* 32MB测试机的剩余物理页不足32MB；验证途中分配失败也完整
         * 归还像素和页表。只看返回值不够，还要比较分配器实际计数。 */
        void *huge=sc_alloc(32u*1024*1024);
        rgb_probe[7]=(u32)huge;
        check(huge==0,1);if(huge)sc_free(huge);
        sc_monitor(after);rgb_probe[8]=after[3];check(before[3]==after[3],2);
        sc_fill_rgb(win,0,0,info[2],info[3],SC_RGB_PAPER);
        sc_text_rgb(win,24,24,rgb_probe[1]?"OOM CHECK FAILED":"OOM rollback PASS",SC_RGB_INK);
        rgb_probe[0]=2;
        while(sc_key()!=27)sc_yield();
        return (int)rgb_probe[1];
    }
    /* 宿主第一轮送62字符参数，确认驱动队列到窗口队列的背压没有
     * 截断真实Shell命令。参数通过EXEC/GETARGS，而非诊断写入。 */
    if(args[0])check(equal(args,"abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"),19);
    /* 9MB块跨三张4MB页表，另一个两页块用于首地址/中间页检查。
     * 内核逐页清零；检查开头、跨表边界、最后一字，随后真的读写它们。 */
    u32 *large=sc_alloc(9u*1024*1024),*small=sc_alloc(8192);
    check(large!=0 && small!=0,3);
    if(!large || !small)return 91;
    rgb_probe[7]=(u32)large;
    check(large[0]==0 && large[1048576]==0 && large[2359295]==0,4);
    large[0]=123;large[1048576]=456;large[2359295]=789;
    check(large[0]==123 && large[1048576]==456 && large[2359295]==789,5);
    check(sc_free((u8 *)small+4096)==-1,6);
    check(sc_free((void *)0x400000)==-1,7);
    check(sc_free(small)==0 && sc_free(small)==-1,8);
    check(sc_free(large)==0,9);
    check(sc_alloc(0)==0 && sc_alloc(0xFFFFFFFFu)==0,10);
    sc_monitor(after);rgb_probe[8]=after[3];check(before[3]==after[3],11);
    u32 bytes=(u32)info[2]*info[3]*4;
    u32 *pixels=sc_alloc(bytes);
    check(pixels!=0,12);if(!pixels)return 92;
    rgb_probe[9]=(u32)pixels;rgb_probe[10]=bytes;
    check(sc_palette(pixels)==0,13); /* 输出到高端用户页的通用缓冲校验 */
    check(sc_frame(win,pixels,(int)(bytes/4))==-1,14);
    check(sc_frame32(win,pixels,bytes-4)==-1,15);
    check(sc_frame32(win+1000,pixels,bytes)==-1,16);
    check(sc_frame32(win-1,pixels,bytes)==-1,20); /* Shell仍存活，不能画父窗口 */
    check(sc_frame32(win,(u32 *)0x200000,bytes)==-1,17);
    for(int y=0;y<info[3];y++)for(int x=0;x<info[2];x++) {
        u32 color=SC_RGB_PAPER,alpha=255;
        if(y>=96 && y<info[3]-44) {
            /* 真彩色诊断图不是另一套UI配色：两通道各取0..255，
             * 从已登记的青色/暖色材质端点组合，产生完整连续取样。 */
            u32 r=(u32)x*255/(info[2]-1),g=(u32)(y-96)*255/(info[3]-140);
            u32 b=palette[PAL_UI_CYAN+7]&255;
            color=(r<<16)|(g<<8)|b;
            if(x>=info[2]-96)alpha=128;
        }
        pixels[y*info[2]+x]=(alpha<<24)|color;
    }
    check(sc_frame32(win,pixels,bytes)==0,18);
    sc_text_rgb(win,24,24,"Welcome to true color",SC_RGB_INK);
    sc_text_rgb(win,24,54,rgb_probe[1]?"Heap / owner CHECK FAILED":"Private heap / owner checks PASS",SC_RGB_ACCENT);
    sc_text_rgb(win,24,info[3]-28,"Esc: exit and reclaim all pages",SC_RGB_INK);
    rgb_probe[0]=1;
    /* 特意不手工free像素块：关闭任务必须回收所有遗漏的堆页/页表，
     * 自动测试同时比较关闭前后的内存计数，不能只检查窗口消失。 */
    while(sc_key()!=27)sc_yield();
    return (int)rgb_probe[1];
}
