#include "../SCLAUNCH.H"
int main(void)
{
    if(cli_parse()<1)return 2;int at=0,no_header=0,limit=-1;u32 seconds=2;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(equal(p,"-t"))no_header=1;
        else if(equal(p,"-n")){int n;if(++at==cli_argc || cli_integer(cli_argv[at],&n)<0 || n<1 || n>86400)return 2;seconds=(u32)n;}
        else if(equal(p,"--count")){if(++at==cli_argc || cli_integer(cli_argv[at],&limit)<0 || limit<1)return 2;}else if(*p=='-')return 2;else break;}
    if(at==cli_argc)return 2;u32 info[8];int tty=-1,last=0,iteration=0;if(sc_terminal_info2(1,info)>=0 && (info[1]==4 || info[1]==5))tty=sc_stream_open("",8,0);
    if(tty<0 && limit<0){cli_text(2,"watch: redirected use requires --count\n");return 2;}
    for(;;){u32 start=(u32)sc_tick();if(tty>=0){int r;while((r=sc_terminal_clear(tty))==-6)sc_yield();if(r<0){last=1;break;}}
        if(!no_header){cli_text(1,"Every ");cli_number(1,(int)seconds);cli_text(1,"s: ");cli_text(1,cli_argv[at]);cli_text(1,"\n\n");}
        int fds[3]={0,1,2},job=launch_words(at,fds);if(job<0){last=1;break;}last=launch_wait((u32)job);iteration++;if(limit>=0 && iteration==limit)break;
        while((u32)sc_tick()-start<seconds*100u)sc_yield();}
    if(tty>=0)sc_stream_close(tty,0);return last;
}
