#include "../SCCRONTAB.inc"
static cron_table table;
static int publish(const char *source,const char *target)
{
    u32 generation;if(cron_read(source,&table,&generation)<0)return cli_error("crontab: invalid table",-1);
    char absolute[64];u32 meta[8];if(cli_path(source,absolute)<0 || sc_fsmeta(absolute,meta)<0 || meta[7]!=generation)return 1;
    int in=sc_stream_open(source,1,0),out=-1,r=-1;if(in<0)return 1;
    char destination[64];u32 old[2];if(cli_path(target,destination)<0)goto done;int exists=sc_stat(destination,old);
    if(exists==-1 && sc_create_ex(target,1,3)<0)goto done;if(exists<0 && exists!=-1)goto done;
    out=sc_stream_open(target,2,meta[2]);if(out<0)goto done;r=cli_copy_fd(in,out);
    if(r>=0){u32 current[8];if(sc_fsmeta(absolute,current)<0 || current[7]!=generation)r=-1;}
done:
    if(out>=3){int end=sc_stream_close(out,r>=0);if(end<0){sc_stream_close(out,0);r=-1;}}sc_stream_close(in,0);return r<0?1:0;
}
int main(void)
{
    if(cli_parse()!=1)return 2;const char *argument=cli_argv[0];char path[64];if(cron_path(0,path)<0)return 1;
    if(equal(argument,"-l")){int fd=sc_stream_open(path,1,0);if(fd<0)return cli_error("crontab",fd);int result=cli_copy_fd(fd,1);sc_stream_close(fd,0);return result<0?1:0;}
    if(equal(argument,"-r")){char absolute[64];if(cli_path(path,absolute)<0)return 1;int result=sc_remove(absolute);return result<0?cli_error("crontab",result):0;}
    if(equal(argument,"-e")){u32 own[8];if(sc_process_self(own)<0)return 1;char temporary[64]="/TMP/CRON.",number[12];decimal(number,(int)own[1]);append(temporary,number,64);append(temporary,".",64);decimal(number,(int)own[2]);append(temporary,number,64);
        if(sc_create_ex(temporary,1,3)<0)return 1;int result=1,fd=sc_stream_open(path,1,0);
        if(fd>=3){int out=sc_stream_open(temporary,2,32767);int r=out>=3?cli_copy_fd(fd,out):-1;sc_stream_close(fd,0);if(out>=3){int end=sc_stream_close(out,r>=0);if(end<0){sc_stream_close(out,0);r=-1;}}if(r<0)goto done;}
        else {char absolute[64];u32 info[2];if(cli_path(path,absolute)<0 || sc_stat(absolute,info)!=-1)goto done;}
        char command[1024];int used=0,fds[3]={0,1,2};if(launch_argument(command,&used,"nano",1)<0 || launch_argument(command,&used,temporary,0)<0)goto done;
        int job=sc_spawn2(command,fds,0);if(job<0 || launch_wait((u32)job))goto done;result=publish(temporary,path);
done:
        {char absolute[64];if(cli_path(temporary,absolute)>=0)sc_remove(absolute);}return result;
    }
    if(argument[0]=='-')return 2;return publish(argument,path);
}
