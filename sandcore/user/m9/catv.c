#include "../SCTEXT.H"
static int ends,tabs,nonprinting=1;
static int run(int fd,const char *name)
{
    (void)name;u8 bytes[4096];int n;while((n=cli_read(fd,bytes,sizeof(bytes)))>0){char out[16384];u32 used=0;
        for(int i=0;i<n;i++){u8 c=bytes[i];if(c=='\n'){if(ends)out[used++]='$';out[used++]='\n';continue;}
            if((c=='\t' && !tabs) || !nonprinting){out[used++]=(char)c;continue;}
            if(c>=128){out[used++]='M';out[used++]='-';c-=128;}if(c<32){out[used++]='^';out[used++]=(char)(c+64);}else if(c==127){out[used++]='^';out[used++]='?';}else out[used++]=(char)c;}
        if(cli_write(1,out,used)<0)return 1;}return n<0;
}
int main(void)
{if(cli_parse()<0)return 2;int at=0;for(;at<cli_argc;at++){char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(p[0]!='-' || !p[1])break;
    for(int j=1;p[j];j++){if(p[j]=='e')ends=1;else if(p[j]=='t')tabs=1;else if(p[j]=='v')nonprinting=0;else return 2;}}return text_files(at,run);}
