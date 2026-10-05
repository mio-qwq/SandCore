#include "../SCTEXT.H"
static int system_v;
static int sum_run(int fd,const char *name)
{
    u8 bytes[4096];u32 sum=0,total=0;for(;;){int n=cli_read(fd,bytes,sizeof(bytes));if(n<0)return cli_error("sum: read",n);if(!n)break;
        if(total>0xFFFFFFFFu-(u32)n)return cli_error("sum: size overflow",-1);total+=(u32)n;
        for(int i=0;i<n;i++)if(system_v)sum+=bytes[i];else sum=(((sum>>1)|((sum&1)<<15))+bytes[i])&65535;
        if(system_v)sum=(sum&65535)+(sum>>16);}
    if(system_v){sum=(sum&65535)+(sum>>16);sum=(sum&65535)+(sum>>16);text_unsigned(1,sum);cli_text(1," ");text_unsigned(1,total/512+(total%512!=0));}
    else {char digits[6];for(int i=4;i>=0;i--){digits[i]=(char)('0'+sum%10);sum/=10;}digits[5]=0;cli_text(1,digits);cli_text(1," ");text_unsigned(1,total/1024+(total%1024!=0));}
    if(!equal(name,"-")){cli_text(1," ");cli_text(1,name);}return cli_text(1,"\n")<0?1:0;
}
int main(void)
{if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc;at++){if(equal(cli_argv[at],"--")){at++;break;}
    if(equal(cli_argv[at],"-s"))system_v=1;else if(equal(cli_argv[at],"-r"))system_v=0;else if(cli_argv[at][0]=='-' && cli_argv[at][1])return 2;else break;}return text_files(at,sum_run);}
