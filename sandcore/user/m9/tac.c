#include "../SCTEXT.H"
static int run(int fd,const char *name)
{(void)name;u32 size;char *bytes=cli_slurp(fd,0x1000000u,&size);if(!bytes)return 1;u32 end=size;int result=0;
    while(end){u32 start=end;if(bytes[end-1]=='\n')start--;while(start && bytes[start-1]!='\n')start--;
        if(cli_write(1,bytes+start,end-start)<0){result=1;break;}end=start;}sc_free(bytes);return result;}
int main(void){if(cli_parse()<0)return 2;return text_files(0,run);}
