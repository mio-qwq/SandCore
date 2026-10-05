#include "../SCDEFLATE.H"
#include "config.h"
#include "vendor/miniz.h"
static u32 crc_table[256];static int crc_ready;
u32 sand_crc(u32 crc,const void *data,u32 count)
{
    if(!crc_ready){for(u32 i=0;i<256;i++){u32 n=i;for(int j=0;j<8;j++)n=(n>>1)^((0u-(n&1))&0xEDB88320u);crc_table[i]=n;}crc_ready=1;}
    const u8 *p=data;for(u32 i=0;i<count;i++)crc=(crc>>8)^crc_table[(crc^p[i])&255];return crc;
}
mz_ulong mz_adler32(mz_ulong value,const unsigned char *p,size_t bytes)
{if(!p)return 1;u32 a=value&65535,b=value>>16;while(bytes){u32 n=bytes>5552?5552:(u32)bytes;bytes-=n;while(n--){a+=*p++;b+=a;}a%=65521;b%=65521;}return a|(b<<16);}
mz_ulong mz_crc32(mz_ulong value,const unsigned char *p,size_t bytes)
{return p?sand_crc((u32)value^0xFFFFFFFFu,p,(u32)bytes)^0xFFFFFFFFu:0;}
void sand_reader_init(sand_reader *r,int fd,u32 bytes,int bounded)
{r->fd=fd;r->at=r->used=r->eof=r->error=0;r->bounded=bounded;r->remaining=bytes;}
int sand_reader_fill(sand_reader *r)
{
    if(r->at<r->used)return r->used-r->at;if(r->error)return -1;if(r->eof)return 0;
    u32 take=4096;if(r->bounded && r->remaining<take)take=r->remaining;
    if(!take){r->eof=1;return 0;}int n=cli_read(r->fd,r->bytes,take);
    if(n<0){r->error=1;return n;}r->at=0;r->used=n;
    if(!n){if(r->bounded && r->remaining){r->error=1;return -1;}r->eof=1;}else if(r->bounded)r->remaining-=(u32)n;
    return n;
}
int sand_reader_byte(sand_reader *r){int n=sand_reader_fill(r);return n>0?r->bytes[r->at++]:n==0?-2:-1;}
int sand_reader_exact(sand_reader *r,void *out,u32 bytes)
{u8 *p=out;while(bytes){int n=sand_reader_fill(r);if(n<=0)return -1;u32 take=(u32)n;if(take>bytes)take=bytes;for(u32 i=0;i<take;i++)p[i]=r->bytes[r->at+i];r->at+=(int)take;p+=take;bytes-=take;}return 0;}
typedef struct {int fd,failed;u32 written;} deflate_output;
static mz_bool emit_deflate(const void *data,int bytes,void *context)
{deflate_output *out=context;if(bytes<0 || out->written>0xFFFFFFFFu-(u32)bytes || cli_write(out->fd,data,(u32)bytes)<0){out->failed=1;return 0;}out->written+=(u32)bytes;return 1;}
int sand_deflate(int input,int output,int level,u32 *crc,u32 *size,u32 *compressed)
{
    tdefl_compressor *state=sc_alloc(sizeof(tdefl_compressor));if(!state)return -4;
    deflate_output out={output,0,0};u32 checksum=0xFFFFFFFFu,total=0;int result=0;
    u32 flags=tdefl_create_comp_flags_from_zip_params(level,-15,0);
    if(tdefl_init(state,emit_deflate,&out,(int)flags)!=TDEFL_STATUS_OKAY){sc_free(state);return -1;}
    u8 buffer[4096];for(;;){int n=cli_read(input,buffer,4096);if(n<0 || total>0xFFFFFFFFu-(u32)(n>0?n:0)){result=-1;break;}
        if(n){checksum=sand_crc(checksum,buffer,(u32)n);total+=(u32)n;}
        tdefl_status status=tdefl_compress_buffer(state,buffer,(u32)n,n?TDEFL_NO_FLUSH:TDEFL_FINISH);
        if(status<0 || out.failed){result=-1;break;}if(!n){if(status!=TDEFL_STATUS_DONE)result=-1;break;}sc_yield();
    }
    sc_free(state);*crc=checksum^0xFFFFFFFFu;*size=total;*compressed=out.written;return result;
}
int sand_inflate(sand_reader *r,int output,u32 limit,u32 *crc,u32 *bytes)
{
    tinfl_decompressor *state=sc_alloc(sizeof(tinfl_decompressor));u8 *dictionary=sc_alloc(32768);
    if(!state || !dictionary){if(state)sc_free(state);if(dictionary)sc_free(dictionary);return -4;}
    tinfl_init(state);u32 total=0,checksum=0xFFFFFFFFu,at=0;int result=-1;
    for(;;){int available=sand_reader_fill(r);if(available<0)break;size_t in=(size_t)available,out=32768-at;
        u32 flags=(!r->eof && (!r->bounded || r->remaining))?TINFL_FLAG_HAS_MORE_INPUT:0;
        tinfl_status status=tinfl_decompress(state,r->bytes+r->at,&in,dictionary,dictionary+at,&out,flags);
        r->at+=(int)in;if((u32)out>limit-total)break;
        if(out){checksum=sand_crc(checksum,dictionary+at,(u32)out);if(output>=0 && cli_write(output,dictionary+at,(u32)out)<0)break;total+=(u32)out;at=(at+(u32)out)&32767;}
        if(status==TINFL_STATUS_DONE){result=0;break;}if(status<0 || (!in && !out))break;sc_yield();
    }
    sc_free(state);sc_free(dictionary);*crc=checksum^0xFFFFFFFFu;*bytes=total;return result;
}
