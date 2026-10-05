#include "../SCHASH.H"
#include "../SCTEXT.H"
static int digest(int fd,u8 result[32])
{cli_sha state;hash_init(&state);u8 bytes[4096];int n;while((n=cli_read(fd,bytes,sizeof(bytes)))>0)hash_update(&state,bytes,(u32)n);if(n<0)return -1;hash_final(&state,result);return 0;}
static int run(int fd,const char *name)
{u8 result[32];if(digest(fd,result)<0)return 1;for(int i=0;i<32;i++)text_hex(1,result[i],2);cli_text(1,"  ");cli_text(1,name);cli_text(1,"\n");return 0;}
int main(void)
{
    if(cli_parse()<0)return 2;if(!cli_argc || !equal(cli_argv[0],"-c"))return text_files(0,run);
    if(cli_argc>2)return 2;int fd=cli_input(1);if(fd<0)return 1;text_reader reader={fd,{0},0,0};text_line line={0,0,0,0};int n,result=0;u32 checked=0;
    while((n=text_line_read(&reader,&line))>0){if(line.size<66){result=1;continue;}u8 expected[32];int valid=1;
        for(int i=0;i<64;i++){char c=line.bytes[i];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0){valid=0;break;}if(i&1)expected[i/2]|=(u8)d;else expected[i/2]=(u8)(d<<4);}
        if(!valid || line.bytes[64]!=' '){result=1;continue;}u32 at=65;while(at<line.size && line.bytes[at]==' ')at++;if(at<line.size && line.bytes[at]=='*')at++;
        if(at==line.size || line.size-at>63){result=1;continue;}char name[64];for(u32 i=at;i<line.size;i++)name[i-at]=line.bytes[i];name[line.size-at]=0;
        int input=sc_stream_open(name,1,0);u8 actual[32];int okay=input>=0 && digest(input,actual)>=0;if(input>=0)sc_stream_close(input,0);
        if(okay)for(int i=0;i<32;i++)if(actual[i]!=expected[i])okay=0;cli_text(1,name);cli_text(1,okay?": OK\n":": FAILED\n");if(!okay)result=1;checked++;
    }text_line_free(&line);if(fd)sc_stream_close(fd,0);return result || n<0 || !checked;
}
