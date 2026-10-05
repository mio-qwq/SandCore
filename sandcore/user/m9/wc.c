#include "../SCTEXT.H"
static int mode;
static int count(int fd,const char *name)
{
    u32 lines=0,words=0,bytes=0;int inword=0;u8 data[4096];
    for(;;){int n=cli_read(fd,data,sizeof(data));if(n<0)return cli_error("wc",n);if(!n)break;
        if(bytes>0xFFFFFFFFu-(u32)n)return cli_error("wc: counter overflow",-1);bytes+=(u32)n;
        for(int i=0;i<n;i++){u8 c=data[i];if(c=='\n')lines++;int blank=c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\v'||c=='\f';if(!blank && !inword)words++;inword=!blank;}}
    if(!mode || (mode&1)){text_unsigned(1,lines);cli_text(1," ");}if(!mode || (mode&2)){text_unsigned(1,words);cli_text(1," ");}
    if(!mode || (mode&4)){text_unsigned(1,bytes);cli_text(1," ");}cli_text(1,name);cli_text(1,"\n");return 0;
}
int main(void){if(cli_parse()<0)return 2;int at=0;while(at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1]){
    for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];if(c=='l')mode|=1;else if(c=='w')mode|=2;else if(c=='c')mode|=4;else return 2;}at++;}return text_files(at,count);}
