#include "../SCTEXT.H"
static u32 table[256];
static u32 update(u32 crc,u8 byte){return (crc<<8)^table[((crc>>24)^byte)&255];}
static int run(int fd,const char *name)
{u8 bytes[4096];u32 crc=0,size=0;int n;while((n=cli_read(fd,bytes,sizeof(bytes)))>0){if(size>0xFFFFFFFFu-(u32)n)return 1;size+=(u32)n;for(int i=0;i<n;i++)crc=update(crc,bytes[i]);}
    if(n<0)return 1;u32 count=size;while(count){crc=update(crc,(u8)count);count>>=8;}text_unsigned(1,~crc);cli_text(1," ");text_unsigned(1,size);if(!equal(name,"-")){cli_text(1," ");cli_text(1,name);}cli_text(1,"\n");return 0;}
int main(void){if(cli_parse()<0)return 2;for(u32 i=0;i<256;i++){u32 crc=i<<24;for(int b=0;b<8;b++)crc=(crc<<1)^((crc&0x80000000u)?0x04C11DB7u:0);table[i]=crc;}return text_files(0,run);}
