#include "../SCTEXT.H"
static u32 stop=8;
static int run(int fd,const char *name)
{(void)name;u8 bytes[4096];u32 column=0;int n;while((n=cli_read(fd,bytes,4096))>0)for(int i=0;i<n;i++){
    u8 c=bytes[i];if(c=='\t'){u32 spaces=stop-column%stop;for(u32 j=0;j<spaces;j++)if(cli_text(1," ")<0)return 1;column+=spaces;}
    else {if(cli_write(1,&c,1)<0)return 1;if(c=='\n'||c=='\r')column=0;else if(c=='\b'){if(column)column--;}else column++;}}return n<0;}
int main(void){if(cli_parse()<0)return 2;int at=0;if(cli_argc>=2 && equal(cli_argv[0],"-t")){if(text_number(cli_argv[1],&stop)<0 || !stop || stop>256)return 2;at=2;}return text_files(at,run);}
