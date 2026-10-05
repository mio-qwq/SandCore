#include "../SCTEXT.H"
int main(void)
{
    if(cli_parse()<1)return 2;char command[1024];copy(command,cli_argv[0],1024);for(int i=1;i<cli_argc;i++){if(length(command)+length(cli_argv[i])*2+4>=1024)return 2;append(command," \"",1024);
        for(char *p=cli_argv[i];*p;p++){char s[3]={0,0,0};if(*p=='"'||*p=='\\'){s[0]='\\';s[1]=*p;}else s[0]=*p;append(command,s,1024);}append(command,"\"",1024);}
    int fds[3]={0,1,2};u32 begin=(u32)sc_tick();int job=sc_spawn2(command,fds,0);if(job<0)return 1;u32 info[8];int result;while((result=sc_wait2((u32)job,info))>0)sc_yield();
    u32 elapsed=(u32)sc_tick()-begin;cli_text(2,"real ");text_unsigned(2,elapsed/100);cli_text(2,".");if(elapsed%100<10)cli_text(2,"0");text_unsigned(2,elapsed%100);cli_text(2,"s (guest PIT)\n");return result<0?1:(int)info[2];
}
