#include "SCDEFLATE.H"
#ifndef GZIP_DECODE
#define GZIP_DECODE 0
#endif
#ifndef GZIP_STDOUT
#define GZIP_STDOUT 0
#endif
/* RFC1952包装由我们实现；miniz只接收原始DEFLATE。坏CRC、截断、
 * 保留标志、坏FHCRC都失败，串联成员逐个验证，文件事务最后提交。 */
static int gzip_decode(int input,int output,u32 limit)
{
    sand_reader r;sand_reader_init(&r,input,0,0);u32 all=0;int members=0;
    for(;;){int first=sand_reader_byte(&r);if(first==-2)return members?0:-1;if(first<0)return -1;
        u8 header[10];header[0]=(u8)first;if(sand_reader_exact(&r,header+1,9)<0 || first!=31 || header[1]!=139 || header[2]!=8 || (header[3]&224))return -1;
        u32 hc=sand_crc(0xFFFFFFFFu,header,10);int flags=header[3];
        if(flags&4){u8 length_bytes[2];if(sand_reader_exact(&r,length_bytes,2)<0)return -1;hc=sand_crc(hc,length_bytes,2);
            int count=length_bytes[0]+((int)length_bytes[1]<<8);while(count--){int c=sand_reader_byte(&r);if(c<0)return -1;u8 b=(u8)c;hc=sand_crc(hc,&b,1);}}
        for(int bit=8;bit<=16;bit<<=1)if(flags&bit){int c,count=0;do{c=sand_reader_byte(&r);if(c<0 || ++count>65536)return -1;u8 b=(u8)c;hc=sand_crc(hc,&b,1);}while(c);}
        if(flags&2){u8 check[2];if(sand_reader_exact(&r,check,2)<0 || ((hc^0xFFFFFFFFu)&65535)!=((u32)check[0]|(u32)check[1]<<8))return -1;}
        u32 checksum,bytes;if(sand_inflate(&r,output,limit-all,&checksum,&bytes)<0)return -1;u8 tail[8];
        if(sand_reader_exact(&r,tail,8)<0 || sand_little(tail)!=checksum || sand_little(tail+4)!=bytes)return -1;
        all+=bytes;members++;if(members==1024)return -2;
    }
}
static int gzip_one(const char *name,int decode,int standard,int keep,int force,int test,int level)
{
    int input=equal(name,"-")?0:sc_stream_open(name,1,0);if(input<0)return cli_error(name,input);
    char target[64];int output=test?-1:1;u32 limit=sand_output_limit();
    if(!standard && !test && input){int n=length(name);if(n>60){sc_stream_close(input,0);return 1;}copy(target,name,64);
        if(decode){if(n>3 && equal(target+n-3,".gz"))target[n-3]=0;else if(n>4 && equal(target+n-4,".tgz")){target[n-4]=0;append(target,".tar",64);}else {sc_stream_close(input,0);return cli_error("gunzip: expected .gz/.tgz",-1);}}
        else append(target,".gz",64);
        char resolved[64],source_path[64];u32 old[2],source_info[2];
        if(cli_path(target,resolved)<0 || cli_path(name,source_path)<0 || sc_stat(source_path,source_info)<0 || source_info[0]!=1){sc_stream_close(input,0);return 1;}
        if(!force && sc_stat(resolved,old)>=0){sc_stream_close(input,0);return cli_error("gzip: output exists",-1);}
        u32 capacity=decode?limit:source_info[1]+source_info[1]/8+4096;if(!decode && capacity<source_info[1]){sc_stream_close(input,0);return 1;}
        output=sc_stream_open(target,2,capacity);if(output<0){sc_stream_close(input,0);return cli_error(target,output);}
    }
    int result;if(decode)result=gzip_decode(input,output,limit);
    else {u8 header[10]={31,139,8,0,0,0,0,0,0,255};u32 crc,size,compressed;
        result=cli_write(output,header,10)<0?-1:sand_deflate(input,output,level,&crc,&size,&compressed);
        if(!result){u8 tail[8];sand_putlittle(tail,crc);sand_putlittle(tail+4,size);if(cli_write(output,tail,8)<0)result=-1;}}
    if(input)sc_stream_close(input,0);if(output>2){int r=sc_stream_close(output,!result);if(r<0){sc_stream_close(output,0);result=r;}
        if(!result && !keep){char path[64];if(cli_path(name,path)<0 || sc_remove(path)<0)result=-1;}}
    return result<0?cli_error("gzip",result):0;
}
int main(void)
{
    if(cli_parse()<0)return 2;int decode=GZIP_DECODE,standard=GZIP_STDOUT,keep=0,force=0,test=0,level=6,at=0;
    for(;at<cli_argc && cli_argv[at][0]=='-' && cli_argv[at][1];at++){
        if(equal(cli_argv[at],"--")){at++;break;}for(int i=1;cli_argv[at][i];i++){char c=cli_argv[at][i];
            if(c=='d')decode=1;else if(c=='c')standard=1;else if(c=='k')keep=1;else if(c=='f')force=1;else if(c=='t'){test=decode=1;}
            else if(c>='1'&&c<='9')level=c-'0';else return 2;}}
    if(at==cli_argc)return gzip_one("-",decode,1,1,force,test,level);
    int result=0;for(int i=at;i<cli_argc;i++)if(gzip_one(cli_argv[i],decode,standard,keep,force,test,level))result=1;return result;
}
