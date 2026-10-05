#include "../SCTEXT.H"
static int quantity(const char *p,u32 *out)
{char number[32];int n=length(p);if(!n || n>31)return -1;u32 multiplier=1;if(p[n-1]=='k'||p[n-1]=='K'){multiplier=1024;n--;}else if(p[n-1]=='M'){multiplier=1048576;n--;}
    for(int i=0;i<n;i++)number[i]=p[i];number[n]=0;u32 value;if(text_number(number,&value)<0 || value>0xFFFFFFFFu/multiplier)return -1;*out=value*multiplier;return 0;}
int main(void)
{
    if(cli_parse()<0)return 2;const char *input=0,*output=0;u32 block=512,count=0xFFFFFFFFu,skip=0;
    for(int i=0;i<cli_argc;i++){char *p=cli_argv[i];while(*p && *p!='=')p++;if(!*p)return 2;*p++=0;
        if(equal(cli_argv[i],"if"))input=p;else if(equal(cli_argv[i],"of"))output=p;else if(equal(cli_argv[i],"bs")){if(quantity(p,&block)<0 || !block || block>65536)return 2;}
        else if(equal(cli_argv[i],"count")){if(quantity(p,&count)<0)return 2;}else if(equal(cli_argv[i],"skip")){if(quantity(p,&skip)<0)return 2;}else return 2;}
    int fd=input?sc_stream_open(input,1,0):0;if(fd<0)return 1;u8 *bytes=sc_alloc(block);if(!bytes)return 1;
    int target=1,result=0;u32 written=0,blocks=0;u32 limit=0x1000000u;
    if(output){char capacity[256];u32 configured;if(sc_user(4,"REDIRECT_LIMIT",capacity,256)>=0 && !text_number(capacity,&configured))limit=configured;
        if(count!=0xFFFFFFFFu && count<=0xFFFFFFFFu/block && count*block<limit)limit=count*block;
        target=sc_stream_open(output,2,limit);if(target<0){result=1;goto done;}}
    for(u32 i=0;i<skip;i++){u32 used=0;while(used<block){int n=cli_read(fd,bytes+used,block-used);if(n<0){result=1;goto done;}if(!n)goto done;used+=(u32)n;}}
    while(blocks<count){u32 used=0;int eof=0;while(used<block){int n=cli_read(fd,bytes+used,block-used);if(n<0){result=1;goto done;}if(!n){eof=1;break;}used+=(u32)n;}
        if(!used)break;if(cli_write(target,bytes,used)<0 || written>0xFFFFFFFFu-used){result=1;break;}written+=used;blocks++;if(eof)break;}
done:if(target>2){int r=sc_stream_close(target,!result);if(r<0){sc_stream_close(target,0);result=1;}}
    if(fd)sc_stream_close(fd,0);sc_free(bytes);text_unsigned(2,written);cli_text(2," bytes copied\n");return result;
}
