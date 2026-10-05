/* mio：受保护路径SYS/CORE/IMAGE.SCX的普通三环工作进程。
 * 这个文件不是零环SKM模块；磁盘写保护只保证服务身份代码可信，
 * 运行时仍受私有页表/异常隔离约束。内核派出的票据才可claim，
 * 用户手动运行同一个SCX不会获知别人的路径或替别人提交像素。
 * 一次服务只处理一张图，全部arena最终回收，不持有隐藏跨请求缓存。
 */
#include "CODEC.H"
#include <stdlib.h>
int main(void)
{
    char args[128],path[64];sc_args(args,sizeof(args));
    u32 ticket=0;int n=0;
    while(args[n]){
        if(args[n]<'0' || args[n]>'9' || ticket>214748364u)return 1;
        u32 digit=(u32)(args[n]-'0');if(ticket==214748364u && digit>7)return 1;
        ticket=ticket*10+digit;n++;
    }
    if(!n || !ticket || sc_image_claim(ticket,path,sizeof(path))<0)return 1;
    codec_ticket(ticket);
    u32 stat[2],*pixels=0;u8 *source=0;int width=0,height=0,status=-2;
    if(sc_stat(path,stat) || stat[0]!=1 || !stat[1] || stat[1]>16u*1024*1024)goto finished;
    source=malloc(stat[1]);if(!source){status=-5;goto finished;}
    /* 64KB分段给桌面与其它窗口让出。完整字节数必须与快照一致；
     * 文件中途被改短不会让残留私有内存当成压缩尾部。 */
    for(u32 at=0;at<stat[1];){
        u32 count=stat[1]-at;if(count>65536)count=65536;
        if(sc_read_at(path,source+at,count,at)!=(int)count)goto finished;
        at+=count;sc_yield();
    }
    status=codec_decode(source,stat[1],&pixels,&width,&height);
finished:
    int submitted=sc_image_submit(ticket,(u32)width,(u32)height,pixels,status);
    codec_ticket(0);codec_release_all();
    return submitted<0?1:0;
}
