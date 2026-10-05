#include "../SCIO.H"
int main(void)
{
    if(cli_parse()<0)return 2;
    if(!cli_argc)return cli_copy_fd(0,1)<0?1:0;
    for(int i=0;i<cli_argc;i++){
        int fd=cli_input(i);if(fd<0)return cli_error("cat",fd);
        int result=cli_copy_fd(fd,1);if(fd)sc_stream_close(fd,0);if(result<0)return cli_error("cat",result);
    }
    return 0;
}
