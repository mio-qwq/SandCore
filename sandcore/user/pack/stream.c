#include "../SCPACK.H"
#include "bzip2/bzlib.h"
#include "lzma/C/LzmaDec.h"
typedef struct {int input;u8 bytes[4096];u32 at,size,total;int eof;} pack_reader;
static int refill(pack_reader *r)
{
    if(r->at<r->size)return 1;if(r->eof)return 0;int n=cli_read(r->input,r->bytes,4096);
    if(n<0)return -1;r->at=0;r->size=(u32)n;if(!n){r->eof=1;return 0;}
    if((u32)n>SC_PACK_LIMIT-r->total)return -1;r->total+=(u32)n;return 1;
}
static int exact(pack_reader *r,u8 *to,u32 n)
{while(n){if(refill(r)!=1)return -1;u32 take=r->size-r->at;if(take>n)take=n;sand_pack_copy(to,r->bytes+r->at,take);r->at+=take;to+=take;n-=take;}return 0;}
static int emit(int output,const u8 *bytes,u32 n,u32 *total,u32 limit)
{if(n>limit-*total)return -1;*total+=n;return output<0 || !n?0:(cli_write(output,bytes,n)<0?-1:0);}
static void *bz_allocate(void *opaque,int count,int bytes)
{(void)opaque;if(count<0 || bytes<0 || (bytes && (u32)count>0xFFFFFFFFu/(u32)bytes))return 0;return sand_pack_malloc((u32)count*(u32)bytes);}
static void bz_release(void *opaque,void *p){(void)opaque;sand_pack_free(p);}
int sand_pack_bzip(int input,int output,int decode,int level,int small,u32 limit)
{
    pack_reader r={.input=input};u8 out[4096];u32 total=0;int members=0;
    for(;;){bz_stream state;int result=-1;sand_pack_fill(&state,0,sizeof(state));state.bzalloc=bz_allocate;state.bzfree=bz_release;
        int got=refill(&r);if(got<0)return -1;if(!got && decode)return members?0:-1;
        int init=decode?BZ2_bzDecompressInit(&state,0,small):BZ2_bzCompressInit(&state,level,0,30);if(init!=BZ_OK)return -1;
        for(;;){int available=refill(&r);if(available<0)break;state.next_in=(char *)r.bytes+r.at;state.avail_in=r.size-r.at;
            state.next_out=(char *)out;state.avail_out=sizeof(out);u32 before=state.avail_in;
            int code=decode?BZ2_bzDecompress(&state):BZ2_bzCompress(&state,r.eof?BZ_FINISH:BZ_RUN);
            r.at+=before-state.avail_in;u32 n=sizeof(out)-state.avail_out;if(emit(output,out,n,&total,limit)<0)break;
            if(code==BZ_STREAM_END){result=0;break;}
            if(decode){if(code!=BZ_OK || (!n && before==state.avail_in))break;}
            else if(code!=BZ_RUN_OK && code!=BZ_FINISH_OK)break;
            sc_yield();
        }
        if(decode)BZ2_bzDecompressEnd(&state);else BZ2_bzCompressEnd(&state);
        if(result<0 || !decode)return result;if(++members==1024)return -1;
        /* 串联bzip2逐成员验证CRC，尾部垃圾与截断不能被忽略。
         * r.at保留核心未消费的字节，下个成员无需重读或整体缓存。 */
    }
}
static void *lz_allocate(ISzAllocPtr allocator,SizeT bytes)
{(void)allocator;return sand_pack_malloc(bytes);}
static void lz_release(ISzAllocPtr allocator,void *p)
{(void)allocator;sand_pack_free(p);}
int sand_pack_lzma(int input,int output,u32 limit)
{
    pack_reader r={.input=input};u8 header[13],out[4096];u32 total=0,size=0;int unknown=1,result=-1;
    if(exact(&r,header,13)<0)return -1;for(int i=5;i<13;i++)if(header[i]!=255)unknown=0;
    if(!unknown){for(int i=9;i<13;i++)if(header[i])return -1;for(int i=0;i<4;i++)size|=(u32)header[5+i]<<(8*i);if(size>limit)return -1;}
    CLzmaProps props;if(LzmaProps_Decode(&props,header,5)!=SZ_OK || props.dicSize>SC_PACK_LIMIT)return -1;
    const ISzAlloc allocator={lz_allocate,lz_release};CLzmaDec state;LzmaDec_Construct(&state);
    if(LzmaDec_Allocate(&state,header,5,&allocator)!=SZ_OK){LzmaDec_Free(&state,&allocator);return -1;}LzmaDec_Init(&state);
    for(;;){int available=refill(&r);if(available<0)break;SizeT in=r.size-r.at,n=sizeof(out),before=in;
        u32 remaining=(unknown?limit:size)-total;if(n>remaining)n=remaining;
        ELzmaStatus status;int code=LzmaDec_DecodeToBuf(&state,out,&n,r.bytes+r.at,&in,n==remaining?LZMA_FINISH_END:LZMA_FINISH_ANY,&status);
        r.at+=in;if(code!=SZ_OK || emit(output,out,n,&total,limit)<0)break;
        if(status==LZMA_STATUS_FINISHED_WITH_MARK || (!unknown && total==size && status==LZMA_STATUS_MAYBE_FINISHED_WITHOUT_MARK)){
            if((!unknown && total!=size) || refill(&r)!=0)break;result=0;break;}
        if((!n && !in && (r.eof || before)) || total==(unknown?limit:size))break;
        sc_yield();
    }
    LzmaDec_Free(&state,&allocator);return result;
}
