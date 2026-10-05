#include "../SCTEXT.H"
static u32 table[256];
static int run(int fd,const char *name)
{u8 bytes[4096];u32 crc=0xFFFFFFFFu;int n;while((n=cli_read(fd,bytes,sizeof(bytes)))>0)for(int i=0;i<n;i++)crc=(crc>>8)^table[(crc^bytes[i])&255];
    if(n<0)return 1;text_hex(1,~crc,8);cli_text(1," ");cli_text(1,name);cli_text(1,"\n");return 0;}
int main(void){if(cli_parse()<0)return 2;for(u32 i=0;i<256;i++){u32 crc=i;for(int b=0;b<8;b++)crc=(crc>>1)^((crc&1)?0xEDB88320u:0);table[i]=crc;}return text_files(0,run);}
