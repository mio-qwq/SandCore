#include "port.h"
#include "../io.h"
#include "../memory.h"
#include "../timer.h"
#include "../task.h"
#include "../random.h"
#include "../crypto.h"
#include "../interrupts.h"

#define NET_PAGE_MAGIC 0x4E504731u
#define NET_BLOCK_MAGIC 0x4E424C31u
#define NET_PAGE_HEADER 128u
#define NET_LARGE_CLASS 7u
typedef struct net_page {
    u32 magic,kind,capacity,available,pages,bytes;
    struct net_page *next,*prev;
    u32 occupied[4];
} net_page_t;
typedef struct {u32 bytes,magic;} net_block_t;
static net_page_t *partial[7];
static const u32 classes[7]={32,64,128,256,512,1024,2048};
static u32 owned_pages,peak_pages,live_bytes,context_depth;
unsigned int sc_net_free_fault[12];
static hmac256_t random_key,isn_key;
static u32 random_counter,random_cursor=32,isn_counter;
static u8 random_block[32];
static int random_initialized;
static int lost_timer;

void *sc_net_memcpy(void *to,const void *from,size_t bytes)
{u8 *d=to;const u8 *s=from;for(size_t i=0;i<bytes;i++)d[i]=s[i];return to;}
void *sc_net_memmove(void *to,const void *from,size_t bytes)
{
    u8 *d=to;const u8 *s=from;
    if((u32)d<(u32)s){for(size_t i=0;i<bytes;i++)d[i]=s[i];}
    else if(d!=s){while(bytes){bytes--;d[bytes]=s[bytes];}}
    return to;
}
void *sc_net_memset(void *to,int value,size_t bytes)
{u8 *d=to;for(size_t i=0;i<bytes;i++)d[i]=(u8)value;return to;}
int sc_net_memcmp(const void *a,const void *b,size_t bytes)
{const u8 *x=a,*y=b;for(size_t i=0;i<bytes;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;}
size_t sc_net_strlen(const char *value){size_t i=0;while(value[i])i++;return i;}
int sc_net_strcmp(const char *a,const char *b)
{while(*a && (u8)*a==(u8)*b){a++;b++;}return (int)(u8)*a-(int)(u8)*b;}
int sc_net_strncmp(const char *a,const char *b,size_t bytes)
{for(size_t i=0;i<bytes;i++){int d=(int)(u8)a[i]-(int)(u8)b[i];if(d || !a[i])return d;}return 0;}
char *sc_net_strchr(const char *value,int byte)
{do{if((u8)*value==(u8)byte)return (char *)value;}while(*value++);return 0;}
char *sc_net_strstr(const char *value,const char *find)
{size_t n=sc_net_strlen(find);if(!n)return (char *)value;for(;*value;value++)if(!sc_net_strncmp(value,find,n))return (char *)value;return 0;}
int sc_net_atoi(const char *value)
{
    while(*value==' ' || *value=='\t')value++;int negative=0;
    if(*value=='+' || *value=='-')negative=*value++=='-';u32 n=0;
    while(*value>='0' && *value<='9'){
        u32 v=(u32)(*value++-'0');if(n>(0x7FFFFFFFu-v)/10u)return 0;n=n*10+v;
    }
    return negative?-(int)n:(int)n;
}
void sc_net_assert(const char *why)
{
    /* 上游sys_timeout_abs在OOM分支断言后马上return，不会解引用空
     * 节点。将这一种可恢复资源失败记为协议栈故障，服务入口停止
     * 新流量并通知/回收socket，不能因一页耗尽让整个桌面panic。 */
    if(!sc_net_strncmp(why,"sys_timeout: timeout != NULL",27)){lost_timer=1;task_kernel_wake();return;}
    panic(why,0x4E4554u);
}
int sc_net_timer_failed(void){return lost_timer;}
void sc_net_context(int entered)
{if(entered)context_depth++;else{if(!context_depth)sc_net_assert("NET CONTEXT UNDERFLOW");context_depth--;}}
void sc_net_core_assert(void){if(!context_depth)sc_net_assert("NET RAW API CONTEXT");}
static void unlink_partial(net_page_t *page)
{
    if(page->prev)page->prev->next=page->next;else partial[page->kind]=page->next;
    if(page->next)page->next->prev=page->prev;page->next=page->prev=0;
}
static void link_partial(net_page_t *page)
{
    page->prev=0;page->next=partial[page->kind];
    if(page->next)page->next->prev=page;partial[page->kind]=page;
}
void *sc_net_alloc(size_t bytes)
{
    sc_net_core_assert();if(!bytes)bytes=1;
    if(bytes>0x7FFFFFFFu-NET_PAGE_HEADER)return 0;
    u32 kind=0;while(kind<7 && bytes+sizeof(net_block_t)>classes[kind])kind++;
    if(kind==7){
        u32 pages=((u32)bytes+NET_PAGE_HEADER+4095u)/4096u;
        u32 base=pframe_alloc_run(pages);if(!base)return 0;
        net_page_t *page=(net_page_t *)base;sc_net_memset(page,0,NET_PAGE_HEADER);
        page->magic=NET_PAGE_MAGIC;page->kind=NET_LARGE_CLASS;page->pages=pages;page->bytes=bytes;
        owned_pages+=pages;live_bytes+=bytes;
        if(owned_pages>peak_pages)peak_pages=owned_pages;return (void *)(base+NET_PAGE_HEADER);
    }
    /* 小PCB/队列节点使用页内位图：不把每个几十字节对象膨胀成一页，
     * 部分空闲页链使分配/释放不扫描全部socket。空页立即归还PF，
     * 各尺寸类只是布局粒度，页数与对象数没有固定池容量。 */
    net_page_t *page=partial[kind];
    if(!page){
        u32 base=pframe_alloc();if(!base)return 0;page=(net_page_t *)base;
        sc_net_memset(page,0,NET_PAGE_HEADER);page->magic=NET_PAGE_MAGIC;page->kind=kind;
        page->capacity=(4096-NET_PAGE_HEADER)/classes[kind];page->available=page->capacity;page->pages=1;
        link_partial(page);owned_pages++;if(owned_pages>peak_pages)peak_pages=owned_pages;
    }
    u32 index=0;
    for(u32 w=0;w<4;w++){
        u32 freebits=~page->occupied[w];if(freebits){index=w*32+(u32)__builtin_ctz(freebits);break;}
    }
    if(index>=page->capacity)sc_net_assert("NET SLAB BITMAP");
    page->occupied[index/32]|=1u<<(index%32);page->available--;
    if(!page->available)unlink_partial(page);
    net_block_t *block=(net_block_t *)((u32)page+NET_PAGE_HEADER+index*classes[kind]);
    block->bytes=bytes;block->magic=NET_BLOCK_MAGIC;live_bytes+=bytes;return block+1;
}
void *sc_net_calloc(size_t count,size_t bytes)
{
    if(bytes && count>0x7FFFFFFFu/bytes)return 0;size_t total=count*bytes;
    void *p=sc_net_alloc(total);if(p)sc_net_memset(p,0,total);return p;
}
void sc_net_free(void *pointer)
{
    if(!pointer)return;sc_net_core_assert();net_page_t *page=(net_page_t *)((u32)pointer&~4095u);
    if(page->magic!=NET_PAGE_MAGIC)sc_net_assert("NET FREE OWNER");
    if(page->kind==NET_LARGE_CLASS){
        if((u32)pointer!=(u32)page+NET_PAGE_HEADER)sc_net_assert("NET FREE LARGE BASE");
        u32 pages=page->pages;live_bytes-=page->bytes;owned_pages-=pages;page->magic=0;
        pframe_free_run((u32)page,pages);return;
    }
    if(page->kind>=7 || (u32)pointer<(u32)page+NET_PAGE_HEADER+sizeof(net_block_t))sc_net_assert("NET FREE SMALL BASE");
    u32 offset=(u32)pointer-(u32)page-NET_PAGE_HEADER-sizeof(net_block_t),size=classes[page->kind];
    u32 index=offset/size;
    if(offset%size || index>=page->capacity || !(page->occupied[index/32]&(1u<<(index%32)))){
        /* 只在真实错误分支留下自洽现场；不吞断言、不改变正常释放。
         * 调用地址与占用位图区分非基址释放、重复释放及页布局损坏，
         * 无需在失联后读取协议正文或随机种子。 */
        sc_net_free_fault[0]=1;sc_net_free_fault[1]=(u32)pointer;sc_net_free_fault[2]=(u32)page;
        sc_net_free_fault[3]=page->kind;sc_net_free_fault[4]=offset;sc_net_free_fault[5]=size;
        sc_net_free_fault[6]=index;sc_net_free_fault[7]=page->capacity;
        sc_net_free_fault[8]=index<128?page->occupied[index/32]:0;
        sc_net_free_fault[9]=page->available;sc_net_free_fault[10]=(u32)__builtin_return_address(0);
        sc_net_free_fault[11]=context_depth;sc_net_assert("NET FREE SLOT");
    }
    net_block_t *block=(net_block_t *)pointer-1;if(block->magic!=NET_BLOCK_MAGIC)sc_net_assert("NET FREE HEADER");
    live_bytes-=block->bytes;block->magic=0;page->occupied[index/32]&=~(1u<<(index%32));
    if(!page->available)link_partial(page);page->available++;
    if(page->available==page->capacity){unlink_partial(page);page->magic=0;owned_pages--;pframe_free((u32)page);}
}
u32 sc_net_pages(void){return owned_pages;}
u32 sc_net_peak_pages(void){return peak_pages;}
u32 sc_net_used(void){return live_bytes;}
u32 sys_now(void){return sc_ticks*10u;}
int sc_net_random_init(void)
{
    u8 seed[64];if(!random_ready() || random_bytes(seed,sizeof(seed))<0)return -1;
    hmac256_init(&random_key,seed,32);hmac256_init(&isn_key,seed+32,32);
    crypto_zero(seed,sizeof(seed));random_initialized=1;return 0;
}
u32 sc_net_rand(void)
{
    sc_net_core_assert();if(!random_initialized)sc_net_assert("NET STRONG SEED MISSING");
    if(random_cursor>=32){
        /* 强熵只需一次；HMAC计数流避免每个端口选择都轮询宿主或RDRAND。
         * 私有计数器回绕时要求重新取强熵，不能重复旧流。ISN用独立键。 */
        if(!++random_counter){if(sc_net_random_init()<0)sc_net_assert("NET RESEED FAILED");random_counter=1;}
        hmac256(&random_key,&random_counter,sizeof(random_counter),random_block);random_cursor=0;
    }
    u32 value;sc_net_memcpy(&value,random_block+random_cursor,4);random_cursor+=4;return value;
}
u32 sc_net_isn(u32 local,u32 remote,u16 local_port,u16 remote_port)
{
    sc_net_core_assert();if(!random_initialized)sc_net_assert("NET ISN SEED MISSING");
    u32 tuple[3]={local,remote,((u32)local_port<<16)|remote_port};u8 digest[32];
    hmac256(&isn_key,tuple,sizeof(tuple),digest);u32 hash;sc_net_memcpy(&hash,digest,4);crypto_zero(digest,sizeof(digest));
    /* 同四元组的M随时钟和连接次数前进，端点哈希让另一端点不能从
     * 已观察连接推断新连接ISN；32位回绕遵循TCP序号算术。 */
    return hash+sc_ticks*2500u+(++isn_counter)*65537u;
}
