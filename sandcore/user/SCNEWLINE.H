#ifndef SCNEWLINE_H
#define SCNEWLINE_H
#include "SCIO.H"
typedef struct {int fd,count_only;u8 bytes[4096];u32 used,total;} newline_output;
static int newline_flush(newline_output *out)
{if(!out->count_only && cli_write(out->fd,out->bytes,out->used)<0)return -1;out->used=0;return 0;}
static int newline_byte(newline_output *out,u8 c)
{
    if(out->total==0xFFFFFFFFu)return -1;out->total++;if(out->count_only)return 0;
    if(out->used==sizeof(out->bytes) && newline_flush(out)<0)return -1;out->bytes[out->used++]=c;return 0;
}
static int newline_convert(int input,int output,int dos,int count_only,u32 *bytes)
{
    newline_output out={output,count_only,{0},0,0};u8 data[4096];int cr=0;
    for(;;){int n=cli_read(input,data,sizeof(data));if(n<0)return -1;if(!n)break;for(int i=0;i<n;i++){u8 c=data[i];
        if(cr){if(c=='\n'){if((dos && newline_byte(&out,'\r')<0) || newline_byte(&out,'\n')<0)return -1;cr=0;continue;}
            if(newline_byte(&out,'\r')<0)return -1;cr=0;}
        if(c=='\r'){cr=1;continue;}if(c=='\n' && dos && newline_byte(&out,'\r')<0)return -1;if(newline_byte(&out,c)<0)return -1;}}
    if(cr && newline_byte(&out,'\r')<0)return -1;if(newline_flush(&out)<0)return -1;*bytes=out.total;return 0;
}
static int newline_file(const char *input,const char *output,int dos)
{
    int fd=equal(input,"-")?0:sc_stream_open(input,1,0);if(fd<0)return cli_error("newline: input",fd);u32 size=0;
    if(!output || equal(output,"-")){int result=newline_convert(fd,1,dos,0,&size);if(fd)sc_stream_close(fd,0);return result<0?cli_error("newline: stream",result):0;}
    if(!fd)return cli_error("newline: named output needs seekable input",-1);
    /* 命名文件先用同一个有代数检查的流统计精确长度，再seek回写入
     * COW事务。避免unix2dos为每个输入盲目保留两倍磁盘空间；中途
     * 修改输入/权限会失败，原输出与元数据在完整commit前均保留。 */
    if(newline_convert(fd,0,dos,1,&size)<0 || sc_stream_seek(fd,0)<0){sc_stream_close(fd,0);return cli_error("newline: count/read",-1);}
    int target=sc_stream_open(output,2,size);if(target<0){sc_stream_close(fd,0);return cli_error("newline: output",target);}
    u32 actual;int result=newline_convert(fd,target,dos,0,&actual);sc_stream_close(fd,0);
    if(result<0 || actual!=size){sc_stream_close(target,0);return cli_error("newline: changed/write",-1);}
    result=sc_stream_close(target,1);if(result<0){sc_stream_close(target,0);return cli_error("newline: commit",result);}return 0;
}
static int newline_main(int dos)
{
    if(cli_parse()<0)return 2;int at=0,named=0;for(;at<cli_argc;at++){const char *option=cli_argv[at];if(equal(option,"--")){at++;break;}if(option[0]!='-' || !option[1])break;
        if(equal(option,"-u"))dos=0;else if(equal(option,"-d"))dos=1;else if(equal(option,"-n"))named=1;else return 2;}
    if(named){if(cli_argc-at!=2)return 2;return newline_file(cli_argv[at],cli_argv[at+1],dos);}
    if(at==cli_argc)return newline_file("-",0,dos);int result=0;for(;at<cli_argc;at++)if(newline_file(cli_argv[at],equal(cli_argv[at],"-")?0:cli_argv[at],dos))result=1;return result;
}
#endif
