#include "../SCLOG.H"
int main(void)
{if(cli_parse()!=0)return 2;char path[64];if(local_log_path(path)<0)return 1;int fd=sc_stream_open(path,1,0);if(fd<0)return cli_error("logread",fd);int result=cli_copy_fd(fd,1);sc_stream_close(fd,0);return result<0?1:0;}
