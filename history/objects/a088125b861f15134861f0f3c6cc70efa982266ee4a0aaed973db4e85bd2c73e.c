/* mio：普通三环内存压力探针，不修改内核或其它任务。
 * 每次从公开MONITOR读取真实空闲字节，保留4MB加页表余量，
 * 让Lens已有UI继续工作，但8MB候选不能分配成功。单个高端
 * 堆最多64MB，所以实例可能先触及虚址上限；另一实例继续。
 * 指针留在数组，不主动free；退出由已有生命周期严格回收。 */
#include "SCAPI.H"
volatile u32 reserve_state[8];
static void *held[32];
int main(void)
{
    u32 memory[48];int info[5];
    int win=sc_open_rgb("Memory reserve",160,90);
    if(win<0)return 1;
    sc_info(win,info);sc_fill_rgb(win,0,0,info[2],info[3],SC_RGB_PAPER);
    sc_text_rgb(win,8,8,"Memory reserve",SC_RGB_INK);
    sc_monitor(memory);reserve_state[2]=memory[4];reserve_state[0]=1;
    for(int i=0;i<32;i++){
        sc_monitor(memory);
        if(memory[4]<=4u*1024*1024+65536)break;
        u32 bytes=memory[4]-4u*1024*1024-65536;
        if(bytes>8u*1024*1024)bytes=8u*1024*1024;
        held[i]=sc_alloc(bytes);
        if(!held[i])break;
        reserve_state[1]+=bytes;reserve_state[4]++;
        sc_yield();
    }
    sc_monitor(memory);reserve_state[3]=memory[4];reserve_state[0]=2;
    while(sc_key()!=27)sc_yield();
    return 0;
}
