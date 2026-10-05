#include "../SCTEXT.H"
static int all;static u32 number=1;
static int run(int fd,const char *name)
{(void)name;text_reader r={fd,{0},0,0};text_line l={0,0,0,0};int n,result=0;
    while((n=text_line_read(&r,&l))>0){if(l.size || all){text_unsigned(1,number++);cli_text(1,"\t");}else cli_text(1,"\t");if(text_line_write(1,&l)<0){result=1;break;}}
    text_line_free(&l);return result || n<0;}
int main(void){if(cli_parse()<0)return 2;int at=0;if(cli_argc>=2 && equal(cli_argv[0],"-b")){if(equal(cli_argv[1],"a"))all=1;else if(!equal(cli_argv[1],"t"))return 2;at=2;}return text_files(at,run);}
