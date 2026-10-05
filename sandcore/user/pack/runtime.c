#include "../SCPACK.H"
static u32 pack_live;
void *sand_pack_malloc(u32 bytes)
{
    /* 同时在用的字典/概率表/块排序内存有总量上限，不允许恶意
     * 文件用多个独立分配绕过单次限制；任务退出由内核兜底回收。 */
    if(!bytes)bytes=1;if(bytes>25165824u-pack_live || bytes>0xFFFFFFFFu-16)return 0;
    u32 *p=sc_alloc(bytes+16);if(!p)return 0;p[0]=bytes;p[1]=0x5041434Bu;pack_live+=bytes;return p+4;
}
void sand_pack_free(void *address)
{if(address){u32 *p=(u32 *)address-4;if(p[1]!=0x5041434Bu)return;pack_live-=p[0];p[1]=0;sc_free(p);}}
void *sand_pack_copy(void *to,const void *from,u32 bytes)
{u8 *d=to;const u8 *s=from;for(u32 i=0;i<bytes;i++)d[i]=s[i];return to;}
void *sand_pack_fill(void *to,int value,u32 bytes)
{u8 *d=to;for(u32 i=0;i<bytes;i++)d[i]=(u8)value;return to;}
void bz_internal_error(int code)
{cli_text(2,"bzip2: internal core assertion ");cli_number(2,code);cli_text(2,"\n");sc_call(0,3,0,0,0,0,0);for(;;)sc_yield();}
