#include "SCPACK.H"
#ifndef PACK_DECODE
#define PACK_DECODE 0
#endif
#ifndef PACK_STDOUT
#define PACK_STDOUT 0
#endif
#ifndef PACK_LZMA
#define PACK_LZMA 0
#endif
/* 命令行、命名与事务由项目实现；许可核心只处理压缩字节。
 * 输出文件在完整CRC/结束标记检查后提交，失败保留源/已有目标。 */
static int pack_one(const char *name,int decode,int standard,int keep,int force,int test,int level,int small)
{
    int input=equal(name,"-")?0:sc_stream_open(name,1,0);if(input<0)return cli_error(name,input);
    int output=test?-1:1,result=-1;char target[64];
    if(input && !standard && !test){int n=length(name);copy(target,name,64);const char *suffix=PACK_LZMA?".lzma":".bz2";int tail=length(suffix);
        if(decode){if(n<=tail || !equal(target+n-tail,suffix))goto done;target[n-tail]=0;}
        else {if(n+tail>=64)goto done;append(target,suffix,64);}
        char resolved[64];u32 info[2];if(cli_path(target,resolved)<0 || (!force && sc_stat(resolved,info)>=0))goto done;
        output=sc_stream_open(target,2,SC_PACK_LIMIT);if(output<0)goto done;
    }
    result=PACK_LZMA?sand_pack_lzma(input,output,SC_PACK_LIMIT):sand_pack_bzip(input,output,decode,level,small,SC_PACK_LIMIT);
done:
    if(input)sc_stream_close(input,0);if(output>2){int close=sc_stream_close(output,result==0);if(close<0){sc_stream_close(output,0);result=-1;}
        if(!result && !keep){char resolved[64];if(cli_path(name,resolved)<0 || sc_remove(resolved)<0)result=-1;}}
    return result<0?cli_error(PACK_LZMA?"unlzma":"bzip2",result):0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int decode=PACK_DECODE,standard=PACK_STDOUT,keep=0,force=0,test=0,small=0,level=9,at=0;
    for(;at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1];at++){
        if(equal(cli_argv[at],"--")){at++;break;}for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];
            if(c=='d')decode=1;else if(c=='c')standard=1;else if(c=='k')keep=1;else if(c=='f')force=1;else if(c=='t'){test=decode=1;}
            else if(c=='s')small=1;else if(c>='1' && c<='9' && !PACK_LZMA)level=c-'0';else return 2;}}
    if(at==cli_argc)return pack_one("-",decode,1,1,force,test,level,small);
    int result=0;for(int i=at;i<cli_argc;i++)if(pack_one(cli_argv[i],decode,standard,keep,force,test,level,small))result=1;return result;
}
