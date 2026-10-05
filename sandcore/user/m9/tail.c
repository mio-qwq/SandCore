#include "../SCTEXT.H"
static u32 limit=10;static int by_bytes;
static int run(int fd,const char *name)
{
    (void)name;if(!limit){u8 discard[4096];int n;while((n=cli_read(fd,discard,4096))>0){}return n<0;}
    if(by_bytes){if(limit>0x1000000u)return cli_error("tail: byte limit",-2);u8 *ring=sc_alloc(limit);if(!ring)return 1;u32 at=0,used=0;u8 data[4096];int n;
        while((n=cli_read(fd,data,4096))>0)for(int i=0;i<n;i++){ring[at++]=data[i];if(at==limit)at=0;if(used<limit)used++;}
        int result=n<0;u32 start=used==limit?at:0,first=limit-start;if(first>used)first=used;
        if(!result && (cli_write(1,ring+start,first)<0 || cli_write(1,ring,used-first)<0))result=1;sc_free(ring);return result;}
    if(limit>4096)return cli_error("tail: line limit",-2);text_line *ring=sc_alloc(limit*sizeof(text_line));if(!ring)return 1;
    for(u32 i=0;i<limit;i++)ring[i]=(text_line){0,0,0,0};text_reader r={fd,{0},0,0};text_line line={0,0,0,0};u32 at=0,used=0;int n;
    while((n=text_line_read(&r,&line))>0){text_line swap=ring[at];ring[at]=line;line=swap;at=(at+1)%limit;if(used<limit)used++;}
    int result=n<0;u32 start=used==limit?at:0;for(u32 i=0;i<used && !result;i++)if(text_line_write(1,&ring[(start+i)%limit])<0)result=1;
    text_line_free(&line);for(u32 i=0;i<limit;i++)text_line_free(&ring[i]);sc_free(ring);return result;
}
int main(void){if(cli_parse()<0)return 2;int at=0;if(cli_argc>=2 && (equal(cli_argv[0],"-n")||equal(cli_argv[0],"-c"))){by_bytes=cli_argv[0][1]=='c';if(text_number(cli_argv[1],&limit)<0)return 2;at=2;}return text_files(at,run);}
