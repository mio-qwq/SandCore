#include "../SCIO.H"
int main(void){if(cli_parse()!=0)return 2;int fd=sc_stream_open("",8,0);if(fd<0)return 1;int r;while((r=sc_terminal_clear(fd))==-6)sc_yield();sc_stream_close(fd,0);return r<0;}
