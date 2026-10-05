#include "../SCTEXT.H"
static u32 limit=10;static int by_bytes;
static int run(int fd,const char *name)
{
    (void)name;if(by_bytes){u8 data[4096];u32 left=limit;while(left){int n=cli_read(fd,data,left>4096?4096:left);if(n<0)return 1;if(!n)break;if(cli_write(1,data,(u32)n)<0)return 1;left-=(u32)n;}return 0;}
    text_reader r={fd,{0},0,0};text_line l={0,0,0,0};int result=0;
    for(u32 i=0;i<limit;i++){int n=text_line_read(&r,&l);if(n<=0){result=n<0;break;}if(text_line_write(1,&l)<0){result=1;break;}}text_line_free(&l);return result;
}
int main(void){if(cli_parse()<0)return 2;int at=0;if(cli_argc>=2 && (equal(cli_argv[0],"-n")||equal(cli_argv[0],"-c"))){by_bytes=cli_argv[0][1]=='c';if(text_number(cli_argv[1],&limit)<0)return 2;at=2;}return text_files(at,run);}
