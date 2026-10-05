#include "../SCIO.H"
int main(void){if(cli_parse()<1)return 2;for(int i=0;i<cli_argc;i++){char path[64];u32 info[2];if(cli_path(cli_argv[i],path)<0)return 2;
    if(sc_stat(path,info)<0){int fd=sc_stream_open(cli_argv[i],2,0);if(fd<0)return cli_error("touch",fd);
        int r=sc_stream_close(fd,1);if(r<0)return cli_error("touch",r);}}
    return 0; /* SandFS无时间戳；已存在文件保持正文，规范明确此差异。 */
}
