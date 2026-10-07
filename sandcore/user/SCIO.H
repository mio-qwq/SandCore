#ifndef SCIO_H
#define SCIO_H
#include "SCAPI.H"
/* 自写CLI的小型字节流工具箱，属于SandCore专用接口，不公开或冒充
 * stdio/stdlib/string运行库。每个工具有独立main与载荷，无argv分派。 */
#define CLI_ARGS 32
static char cli_argument_text[1024];
static char *cli_argv[CLI_ARGS];
static int cli_argc;
static void cli_wait_io(int fd,int writing)
{
    u32 ready[8];
    if(sc_stream_ready(fd,ready)<0)return;
    if(ready[writing?3:2])return;
    /* 检查与等待间的变化保留为sticky事件；共享管道/TTY按实际订阅链
     * 唤醒。返回后仍重试IO，另一个读者先消费不会把提示当成正文。 */
    sc_event_wait(SC_EVENT_STREAM|SC_EVENT_INPUT,0xFFFFFFFFu);
}
static int cli_write(int fd,const void *bytes,u32 n)
{
    u32 at=0;while(at<n){u32 chunk=n-at;if(chunk>65536)chunk=65536;int result=sc_stream_write(fd,(const u8 *)bytes+at,chunk);
        if(result==-6){cli_wait_io(fd,1);continue;}if(result<=0)return result?result:-1;at+=(u32)result;}return (int)at;
}
static int cli_text(int fd,const char *text){return cli_write(fd,text,(u32)length(text));}
static void cli_number(int fd,int value){char text[12];decimal(text,value);cli_text(fd,text);}
static int cli_error(const char *what,int code)
{cli_text(2,what);cli_text(2,": ");cli_number(2,code);cli_text(2,"\n");return 1;}
static int cli_read(int fd,void *bytes,u32 n)
{if(n>65536)n=65536;for(;;){int result=sc_stream_read(fd,bytes,n);if(result!=-6)return result;cli_wait_io(fd,0);}}
static int cli_parse(void)
{
    if(sc_arguments2(cli_argument_text,sizeof(cli_argument_text))<0)return -1;
    char *from=cli_argument_text,*to=cli_argument_text;cli_argc=0;
    while(*from){
        while(*from==' ' || *from=='\t')from++;if(!*from)break;
        if(cli_argc==CLI_ARGS)return -1;cli_argv[cli_argc++]=to;char quote=0;
        while(*from){
            char c=*from++;
            if(quote){if(c==quote){quote=0;continue;}if(c=='\\' && quote=='"' && *from)c=*from++;}
            else {if(c=='\'' || c=='"'){quote=c;continue;}if(c==' ' || c=='\t')break;if(c=='\\'){if(!*from)return -1;c=*from++;}}
            *to++=c;
        }
        if(quote)return -1;*to++=0;
    }
    return cli_argc;
}
static int cli_integer(const char *text,int *out)
{
    int minus=0;if(*text=='-' || *text=='+'){minus=*text=='-';text++;}if(!*text)return -1;
    u32 value=0,limit=minus?0x80000000u:0x7FFFFFFFu;
    while(*text){if(*text<'0'||*text>'9')return -1;u32 d=(u32)(*text++-'0');if(value>(limit-d)/10)return -1;value=value*10+d;}
    *out=minus?(int)(0u-value):(int)value;return 0;
}
static int cli_path(const char *path,char out[64]){return sc_resolve(path,out,64);}
static int cli_input(int argument)
{return argument>=cli_argc || equal(cli_argv[argument],"-")?0:sc_stream_open(cli_argv[argument],1,0);}
static int cli_copy_fd(int input,int output)
{
    u8 bytes[4096];for(;;){int count=cli_read(input,bytes,sizeof(bytes));if(count<=0)return count;
        int result=cli_write(output,bytes,(u32)count);if(result<0)return result;}
}
static int cli_copy_file(const char *input,const char *output)
{
    char path[64];u32 info[2];if(cli_path(input,path)<0 || sc_stat(path,info)<0 || info[0]!=1)return -1;
    int source=sc_stream_open(input,1,0);if(source<0)return source;
    int target=sc_stream_open(output,2,info[1]);if(target<0){sc_stream_close(source,0);return target;}
    int result=cli_copy_fd(source,target);sc_stream_close(source,0);
    if(result>=0)result=sc_stream_close(target,1);else sc_stream_close(target,0);return result;
}
static char *cli_slurp(int fd,u32 capacity,u32 *out)
{
    if(capacity==0xFFFFFFFFu)return 0;u32 reserved=capacity<4096?capacity:4096,n=0;char *bytes=sc_alloc(reserved+1);if(!bytes)return 0;
    /* capacity是明确失败的输入上限，不是小文件必须占满的预留量。
     * 先读一块再按倍增扩容，多个小归档成员不会各占16MiB；总复制
     * 线性有界。恰好达到上限仍探测EOF，不能把多出的正文截掉。 */
    for(;;){u8 chunk[4096];u32 take=capacity-n;if(take>4096)take=4096;if(!take){u8 extra;if(cli_read(fd,&extra,1)!=0){sc_free(bytes);return 0;}break;}
        int count=cli_read(fd,chunk,take);if(count<0){sc_free(bytes);return 0;}if(!count)break;u32 needed=n+(u32)count;
        if(needed>reserved){u32 next=reserved?reserved:1;while(next<needed){if(next>capacity/2){next=capacity;break;}next*=2;}
            char *fresh=sc_alloc(next+1);if(!fresh){sc_free(bytes);return 0;}for(u32 i=0;i<n;i++)fresh[i]=bytes[i];sc_free(bytes);bytes=fresh;reserved=next;}
        for(int i=0;i<count;i++)bytes[n++]=(char)chunk[i];}
    bytes[n]=0;*out=n;return bytes;
}
#endif
