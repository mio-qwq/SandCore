/* mio：测试完整缓冲而非只测被写子区。旧M7和新G2
 * 真正生成并执行相同源码，能力宏只决定实现路径。 */
#include "SCAPI.H"
#include "NUI.inc"
static int lengths[22]={0,1,2,3,4,5,7,8,15,16,31,32,63,64,65,127,128,255,256,257,511,512},words[8]={0,1,2,3,7,31,64,127};
static int rects[72][5]={{-3,-2,9,7,0},
{0,0,37,29,0},
{37,29,0,0,0},
{9,7,-5,2,0},
{-99,-99,2,2,1},
{0,0,37,29,1},
{9,7,-5,2,1},
{28,21,20,20,1},
{-15,5,23,1,0},
{33,10,30,13,1},
{23,21,2,34,0},
{-26,10,23,0,1},
{11,11,2,34,0},
{38,34,6,12,1},
{47,3,-3,28,0},
{14,20,10,0,1},
{36,39,43,-2,0},
{-15,-13,2,23,1},
{38,34,16,34,0},
{-26,-5,55,2,1},
{46,22,19,38,0},
{-1,-20,5,8,1},
{21,2,0,9,0},
{2,38,53,16,1},
{-22,-2,39,9,0},
{11,9,21,-3,1},
{-30,27,51,14,0},
{27,33,29,42,1},
{13,25,50,-3,0},
{38,-6,11,-7,1},
{25,26,46,38,0},
{3,19,-3,6,1},
{1,37,47,27,0},
{-6,3,26,28,1},
{48,-19,58,-2,0},
{-19,-4,52,11,1},
{-7,36,6,8,0},
{-15,36,51,-6,1},
{-15,-21,18,15,0},
{3,-8,35,42,1},
{-13,0,47,21,0},
{-20,-11,45,33,1},
{-18,-13,3,42,0},
{-19,-17,44,18,1},
{-15,-24,0,25,0},
{25,-21,57,-7,1},
{33,39,31,38,0},
{-6,-23,38,22,1},
{19,36,-1,18,0},
{-28,23,46,10,1},
{29,-10,57,22,0},
{-4,-22,12,15,1},
{45,35,53,-4,0},
{41,17,28,31,1},
{5,39,8,3,0},
{11,-13,-6,36,1},
{17,38,-8,9,0},
{30,12,40,8,1},
{28,16,37,-1,0},
{41,-10,9,8,1},
{34,2,24,-2,0},
{23,-9,-3,26,1},
{11,-20,24,3,0},
{31,3,33,-7,1},
{-13,38,47,4,0},
{24,-15,55,-8,1},
{43,-14,51,19,0},
{13,2,38,14,1},
{6,38,6,44,0},
{-5,10,-1,6,1},
{30,13,4,26,0},
{11,-22,55,-2,1}};
static u8 source[640],target[640],output[1521536];
static u32 rect_buffer[37*29+32];
static volatile int mem_probe[8];
static int used=32,errors;
static void reset(void){for(int i=0;i<640;i++){source[i]=(u8)(i*37+11);target[i]=(u8)(i*13+7);}}
static void append_buffer(const void *data,int count){
    const u8 *p=(const u8 *)data;for(int i=0;i<count;i++)output[used++]=p[i];
}
static void put(int at,u32 value){for(int i=0;i<4;i++)output[at+i]=(u8)(value>>(8*i));}
int main(void){
    int win=sc_open_rgb("SCMEM byte guards / mio",310,170);if(win<0)return 1;
    if(sc_mem_copy(0,0,0)||sc_mem_move(0,0,0)||sc_mem_fill(0,0,0)||sc_mem_fill32(0,0,0))errors++;
    for(int k=0;k<22;k++)for(int a=0;a<4;a++)for(int b=0;b<4;b++){
        reset();if(sc_mem_copy(target+32+b,source+32+a,(u32)lengths[k])!=target+32+b)errors++;
        append_buffer(target,640);
    }
    for(int k=0;k<22;k++)for(int a=0;a<4;a++){
        reset();if(sc_mem_fill(target+32+a,421,(u32)lengths[k])!=target+32+a)errors++;
        append_buffer(target,640);
    }
    for(int k=0;k<8;k++)for(int a=0;a<4;a++){
        reset();if(sc_mem_fill32((u32 *)(target+32+a),0x12345678u,(u32)words[k])!=(u32 *)(target+32+a))errors++;
        append_buffer(target,640);
    }
    for(int k=0;k<22;k++)for(int a=0;a<8;a++)for(int b=0;b<8;b++){
        reset();if(sc_mem_move(target+32+b,target+32+a,(u32)lengths[k])!=target+32+b)errors++;
        append_buffer(target,640);
    }
    // 裁剪参考包括负尺寸/屏外/全宽/150%逻辑裁剪；保存前后16项护栏。
    ui_width=37;ui_height=29;ui_scale=150;ui_pixels=rect_buffer+16;
    for(int k=0;k<72;k++){
        for(int i=0;i<37*29+32;i++)rect_buffer[i]=(i<16||i>=16+37*29)?0x534D494Fu:0xFF123456u;
        ui_clip_clear();if(rects[k][4])ui_clip_set(2,3,13,9);
        ui_physical_rgb(rects[k][0],rects[k][1],rects[k][2],rects[k][3],0x12ABCDEFu);
        append_buffer(rect_buffer,sizeof(rect_buffer));
    }
    char *magic="SMEM1MIO";for(int i=0;i<8;i++)output[i]=(u8)magic[i];
    put(8,1);put(12,1880);put(16,72);put(20,(u32)(used-32));put(24,(u32)errors);
#ifdef __SCCC_REP__
    put(28,1);
#else
    put(28,0);
#endif
    for(int i=0;i<16;i++){put(used,0x534D494Fu);used+=4;}
    char path[64];sc_args(path,sizeof(path));mem_probe[1]=sc_write(path,output,(u32)used);
    mem_probe[2]=errors;mem_probe[0]=1;
    for(;;){if(sc_key()==27)return 0;sc_yield();}
}
