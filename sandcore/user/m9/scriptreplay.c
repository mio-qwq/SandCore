#include "../SCTEXT.H"
static int delay_value(const char **cursor,u32 *ticks)
{const char *p=*cursor;u32 seconds=0,fraction=0;int digits=0;if(*p<'0'||*p>'9')return -1;
    while(*p>='0'&&*p<='9'){seconds=seconds*10+(u32)(*p++-'0');if(seconds>86400)return -1;}if(*p=='.'){p++;while(*p>='0'&&*p<='9'){if(digits<2)fraction=fraction*10+(u32)(*p-'0');digits++;p++;}if(!digits)return -1;}
    if(digits==1)fraction*=10;if(*p!=' ' && *p!='\t')return -1;while(*p==' '||*p=='\t')p++;*cursor=p;*ticks=seconds*100+fraction;return 0;}
int main(void)
{
    if(cli_parse()<2 || cli_argc>3)return 2;u32 divisor=1;if(cli_argc==3 && (text_number(cli_argv[2],&divisor)<0 || !divisor || divisor>10000))return 2;
    int timing=sc_stream_open(cli_argv[0],1,0),data=sc_stream_open(cli_argv[1],1,0),result=1;if(timing<0 || data<0)goto done;
    text_reader reader={timing,{0},0,0};text_line line={0};int n;u8 bytes[4096];
    while((n=text_line_read(&reader,&line))>0){if(line.size>=256)goto release;char text[256];for(u32 i=0;i<line.size;i++){if(!line.bytes[i])goto release;text[i]=line.bytes[i];}text[line.size]=0;
        const char *p=text;u32 ticks,count;if(delay_value(&p,&ticks)<0 || text_number(p,&count)<0 || count>67108864)goto release;u32 start=(u32)sc_tick();ticks/=divisor;
        while((u32)sc_tick()-start<ticks)sc_yield();while(count){u32 take=count>4096?4096:count;int got=cli_read(data,bytes,take);if(got<=0 || cli_write(1,bytes,(u32)got)<0)goto release;count-=(u32)got;}}
    if(n<0)goto release;{u8 extra;if(cli_read(data,&extra,1)!=0)goto release;}result=0;
release:
    text_line_free(&line);
done:
    if(timing>=0)sc_stream_close(timing,0);if(data>=0)sc_stream_close(data,0);if(result)cli_text(2,"scriptreplay: timing/data/output failure\n");return result;
}
