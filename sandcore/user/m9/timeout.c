#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<2)return 2;int seconds;if(cli_integer(cli_argv[0],&seconds)<0 || seconds<0 || seconds>21474836)return 2;
    char command[1024];copy(command,cli_argv[1],1024);for(int i=2;i<cli_argc;i++){if(length(command)+length(cli_argv[i])*2+4>=1024)return 2;append(command," \"",1024);
        for(char *p=cli_argv[i];*p;p++){char s[3]={0,0,0};if(*p=='"'||*p=='\\'){s[0]='\\';s[1]=*p;}else s[0]=*p;append(command,s,1024);}append(command,"\"",1024);}
    int fds[3]={0,1,2},job=sc_spawn2(command,fds,0);if(job<0)return 1;u32 info[8],begin=(u32)sc_tick();int result=0,timed_out=0;
    while((result=sc_wait2((u32)job,info))>0){if(seconds && !timed_out && (u32)((u32)sc_tick()-begin)>=(u32)seconds*100){if(info[3])sc_kill2((int)info[3]);timed_out=1;}sc_yield();}
    return result<0?1:timed_out?124:(int)info[2];
}
