#include "../SCIO.H"
int main(void)
{if(cli_parse()!=0)return 2;u8 bytes[4096];u32 current=0;int n;while((n=cli_read(0,bytes,sizeof(bytes)))>0){if(cli_write(1,bytes,(u32)n)<0)return 1;current+=(u32)n;
    if(current>=1048576){if(cli_text(2,".")<0)return 1;current-=1048576;}}if(cli_text(2,"\n")<0)return 1;return n<0;}
