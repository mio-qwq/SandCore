#include "SCSUM.H"
#if defined(SC_HASH_SHA512)
#include "SCSHA512.H"
#define sum_state hash512_state
#define sum_init hash512_init
#define sum_update hash512_update
#define sum_final hash512_final
#define SUM_BYTES 64
#elif defined(SC_HASH_MD5) || defined(SC_HASH_SHA1)
#include "SCMDHASH.H"
#define sum_state legacy_hash
#define sum_init legacy_init
#define sum_update legacy_update
#define sum_final legacy_final
#if defined(SC_HASH_MD5)
#define SUM_BYTES 16
#else
#define SUM_BYTES 20
#endif
#else
#include "SCHASH.H"
#define sum_state cli_sha
#define sum_init hash_init
#define sum_update hash_update
#define sum_final hash_final
#define SUM_BYTES 32
#endif
static int sum_quiet,sum_status;
static int sum_digest(int fd,u8 *out)
{sum_state state;sum_init(&state);u8 bytes[4096];int n;while((n=cli_read(fd,bytes,sizeof(bytes)))>0){sum_update(&state,bytes,(u32)n);sc_yield();}if(n<0)return -1;sum_final(&state,out);return 0;}
static int sum_run(int fd,const char *name)
{u8 out[SUM_BYTES];if(sum_digest(fd,out)<0)return 1;for(int i=0;name[i];i++)if(name[i]=='\n' || name[i]=='\r' || name[i]=='\\'){cli_text(2,"checksum: filename cannot be represented\n");return 1;}
    char line[SUM_BYTES*2+68];const char *hex="0123456789abcdef";int n=0;for(int i=0;i<SUM_BYTES;i++){line[n++]=hex[out[i]>>4];line[n++]=hex[out[i]&15];}
    line[n++]=' ';line[n++]=' ';for(int i=0;name[i];i++){if(n==(int)sizeof(line)-2)return 1;line[n++]=name[i];}line[n++]='\n';return cli_write(1,line,(u32)n)<0;}
static int sum_hex(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int checksum_main(void)
{
    if(cli_parse()<0)return 2;int at=0,check=0;
    for(;at<cli_argc;at++){const char *p=cli_argv[at];if(equal(p,"--")){at++;break;}if(equal(p,"-c"))check=1;else if(equal(p,"--quiet"))sum_quiet=1;
        else if(equal(p,"--status"))sum_status=1;else if(equal(p,"-b") || equal(p,"-t")){}else if(p[0]=='-' && p[1])return 2;else break;}
    if(!check){if(sum_quiet || sum_status)return 2;return text_files(at,sum_run);}if(cli_argc-at>1)return 2;
    int fd=cli_input(at);if(fd<0)return 1;text_reader reader={fd,{0},0,0};text_line line={0};int n,result=0;u32 checked=0;
    while((n=text_line_read(&reader,&line))>0){u32 h=SUM_BYTES*2;if(line.size<h+3 || line.bytes[h]!=' ' || (line.bytes[h+1]!=' ' && line.bytes[h+1]!='*')){result=1;continue;}
        u8 expected[SUM_BYTES],actual[SUM_BYTES];int valid=1;for(u32 i=0;i<h;i++){int d=sum_hex(line.bytes[i]);if(d<0){valid=0;break;}if(i&1)expected[i/2]|=(u8)d;else expected[i/2]=(u8)(d<<4);}
        u32 bytes=line.size-h-2;if(!valid || bytes>63){result=1;continue;}char name[64];for(u32 i=0;i<bytes;i++){name[i]=line.bytes[h+2+i];if((u8)name[i]<32)valid=0;}name[bytes]=0;
        if(!valid){result=1;continue;}int input=equal(name,"-")?-1:sc_stream_open(name,1,0);int okay=input>=0 && sum_digest(input,actual)>=0;if(input>=0)sc_stream_close(input,0);
        if(okay)for(int i=0;i<SUM_BYTES;i++)if(actual[i]!=expected[i])okay=0;if(!sum_status && (!sum_quiet || !okay)){cli_text(1,name);cli_text(1,okay?": OK\n":": FAILED\n");}
        if(!okay)result=1;checked++;}text_line_free(&line);if(fd)sc_stream_close(fd,0);return result || n<0 || !checked;
}
