/* mio：旧索引画布的真实三环等价探针。只用已发布接口生成固定图案，
 * 验证器比较实际后台/PCI客户区，不用注入画布或伪造程序状态。
 * 原画布在放大/恢复时不变，包含全部256槽和跨行非周期边界。
 * index_probe是本程序的只读诊断旁表，永远不是公开SCAPI输出布局。 */
#include "SCAPI.H"
static u8 pixels[64000];
static volatile int index_probe[8];
int main(void)
{
    int window=sc_open("Indexed / mio",320,184),info[5];
    if(window<0||sc_info(window,info))return 1;
    int width=info[2],height=info[3];
    if(width*height>64000)return 2;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++)
        pixels[y*width+x]=(u8)((x*37+y*53+(x*y)%251)&255);
    index_probe[1]=width;index_probe[2]=height;
    index_probe[3]=sc_frame(window,pixels,(u32)(width*height));
    index_probe[0]=1;
    for(;;){
        int key=sc_key();
        if(key==27)return 0;
        if(key=='1'||key=='2'){
            index_probe[4]=sc_window(window,key=='1'?1:2);
            index_probe[3]=sc_frame(window,pixels,(u32)(width*height));
            index_probe[0]++;
        }
        sc_yield();
    }
}
