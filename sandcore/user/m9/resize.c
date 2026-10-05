#include "../SCTEXT.H"
int main(void)
{if(cli_parse()!=0)return 2;int fd=sc_stream_open("",8,0);if(fd<0)return 1;u32 info[8];int r=sc_terminal_info2(fd,info);sc_stream_close(fd,0);if(r<0 || !info[2] || !info[3])return 1;
    cli_text(1,"COLUMNS=");text_unsigned(1,info[2]);cli_text(1,"; LINES=");text_unsigned(1,info[3]);return cli_text(1,"; export COLUMNS LINES;\n")<0;}
