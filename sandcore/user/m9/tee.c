#include "../SCTEXT.H"
int main(void)
{
    if(cli_parse()<0 || cli_argc>12)return 2;int at=0,append_old=0;if(cli_argc && equal(cli_argv[0],"-a")){append_old=1;at++;}
    int files[12],opened=0,result=0;u32 capacity=0x1000000u;char value[256];u32 configured;
    if(sc_user(4,"REDIRECT_LIMIT",value,256)>=0 && !text_number(value,&configured))capacity=configured;
    for(int i=at;i<cli_argc;i++){
        int old=append_old?sc_stream_open(cli_argv[i],1,0):-1,fd=sc_stream_open(cli_argv[i],2,capacity);
        if(fd<0){if(old>=0)sc_stream_close(old,0);result=1;goto done;}
        files[opened++]=fd;if(old>=0){int r=cli_copy_fd(old,fd);sc_stream_close(old,0);if(r<0){result=1;goto done;}}
    }
    u8 data[4096];for(;;){int n=cli_read(0,data,4096);if(n<0){result=1;break;}if(!n)break;
        if(cli_write(1,data,(u32)n)<0){result=1;break;}for(int i=0;i<opened;i++)if(cli_write(files[i],data,(u32)n)<0){result=1;break;}if(result)break;}
done:for(int i=0;i<opened;i++){int r=sc_stream_close(files[i],!result);if(r<0){sc_stream_close(files[i],0);result=1;}}return result;
}
