#include "../SCLAUNCH.H"
#include "../SCTEXT.H"
static void timing(u32 ticks,u32 bytes)
{text_unsigned(2,ticks/100);cli_text(2,".");if(ticks%100<10)cli_text(2,"0");text_unsigned(2,ticks%100);cli_text(2," ");text_unsigned(2,bytes);cli_text(2,"\n");}
int main(void)
{
    if(cli_parse()<0)return 2;int at=0,append_mode=0,timed=0;const char *command=0,*file="typescript";
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"-a"))append_mode=1;else if(equal(p,"-t"))timed=1;
        else if(equal(p,"-c")){if(++at==cli_argc)return 2;command=cli_argv[at];}else if(equal(p,"--")){at++;break;}else if(*p=='-')return 2;else break;}
    if(cli_argc-at>1)return 2;if(at<cli_argc)file=cli_argv[at];u32 limit=16777216;char configured[256];if(sc_user(4,"SCRIPT_LIMIT",configured,256)>=0 && (text_number(configured,&limit)<0 || !limit || limit>67108864))return 2;
    int terminal=sc_stream_open("",8,0);if(terminal<0)return cli_error("script: terminal",terminal);int input=-1,out=-1,pipe[2]={-1,-1},result=1,job=0;u32 status[8]={0};
    if(append_mode){input=sc_stream_open(file,1,0);if(input<0){char path[64];u32 info[2];if(cli_path(file,path)<0 || sc_stat(path,info)!=-1)goto done;}}
    out=sc_stream_open(file,2,limit);if(out<0)goto done;if(input>=0){if(sc_stream_seek(input,0)<0 || cli_copy_fd(input,out)<0)goto done;sc_stream_close(input,0);input=-1;}
    if(sc_stream_pipe(pipe)<0)goto done;int fds[3]={terminal,pipe[1],pipe[1]};char launch[1024];int used=0;
    if(launch_argument(launch,&used,"sh",1)<0)goto done;if(command){if(launch_argument(launch,&used,"-c",0)<0 || launch_argument(launch,&used,command,0)<0)goto done;}
    else if(launch_argument(launch,&used,"--serial",0)<0)goto done;job=sc_spawn2(launch,fds,0);if(job<0){job=0;goto done;}sc_stream_close(pipe[1],0);pipe[1]=-1;
    int event=sc_stream_event(0,1);u32 previous=(u32)sc_tick();u8 bytes[4096];
    for(;;){int n=sc_stream_read(pipe[0],bytes,sizeof(bytes));if(n>0){if(cli_write(out,bytes,(u32)n)<0 || cli_write(terminal,bytes,(u32)n)<0)goto done;
            u32 now=(u32)sc_tick();if(timed)timing(now-previous,(u32)n);previous=now;}
        else if(n<0 && n!=-6)goto done;
        if(job){int r=sc_wait2((u32)job,status);if(r<0)goto done;if(!r)job=0;
            else if(event>=0 && sc_stream_event(0,0)!=event){if(status[3])sc_kill_generation((int)status[3],status[4]);event=sc_stream_event(0,0);}}
        if(!job && !n)break;sc_yield();}
    if(sc_stream_close(out,1)<0)goto done;out=-1;result=(int)status[2];
done:
    if(job){u32 state[8];int r=sc_wait2((u32)job,state);if(r>0 && state[3]){sc_kill_generation((int)state[3],state[4]);(void)sc_wait2((u32)job,state);}}
    if(out>=0)sc_stream_close(out,0);if(input>=0)sc_stream_close(input,0);for(int i=0;i<2;i++)if(pipe[i]>=0)sc_stream_close(pipe[i],0);sc_stream_close(terminal,0);
    if(result)cli_text(2,"script: session/input/output failed\n");return result;
}
