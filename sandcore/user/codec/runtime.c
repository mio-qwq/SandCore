/* mio：三环解码的最小freestanding运行时。第三方库名字兼容malloc/
 * memcpy，但这里没有宿主CRT、进程、stdio或动态库，服务所用的页
 * 全经SCAPI分配。小块分配不一块一页，避免解码树数千个节点吃掉
 * 64MB地址域；arena按16B对齐，释放块合并/重用，单请求硬限48MB。
 * 64位除法用两个32位半字的逐位长除法，避免递归调用libgcc帮助函数。
 */
#include "CODEC.H"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CODEC_HEAP_LIMIT (48u*1024*1024)
#define CODEC_LIVE 0x4F494D43u
typedef struct codec_arena {u32 bytes;struct codec_arena *next;u32 zero[2];} codec_arena_t;
typedef struct codec_block {u32 magic,size;struct codec_block *next;u32 zero;} codec_block_t;
static codec_arena_t *arenas;
static codec_block_t *available;
static u32 reserved,active_ticket;
static int memory_failed;

void codec_ticket(u32 ticket){active_ticket=ticket;}
int codec_memory_failed(void){return memory_failed;}
void *memcpy(void *dst,const void *src,size_t bytes)
{u8 *d=dst;const u8 *s=src;for(size_t i=0;i<bytes;i++)d[i]=s[i];return dst;}
void *memmove(void *dst,const void *src,size_t bytes)
{
    u8 *d=dst;const u8 *s=src;
    if((uintptr_t)d<=(uintptr_t)s)for(size_t i=0;i<bytes;i++)d[i]=s[i];
    else while(bytes){bytes--;d[bytes]=s[bytes];}
    return dst;
}
void *memset(void *dst,int value,size_t bytes)
{u8 *d=dst;for(size_t i=0;i<bytes;i++)d[i]=(u8)value;return dst;}
int memcmp(const void *a,const void *b,size_t bytes)
{const u8 *x=a,*y=b;for(size_t i=0;i<bytes;i++)if(x[i]!=y[i])return (int)x[i]-y[i];return 0;}
size_t strlen(const char *text){size_t n=0;while(text[n])n++;return n;}
int abs(int value){return value<0?-value:value;}

/* mio：第三方通用调色板源码包含qsort调用，freestanding编译也必须
 * 有完整声明。这里实现原地堆排序，既不依赖libc，也不申请临时页，
 * 不因攻击者提供已有序/逆序数据而退化成快速排序的二次复杂度。
 * 字节交换支持任意元素宽度；所有下标先检查count*size不溢出，
 * 堆的父节点条件保证root*2+1仍在合法size_t范围。比较器由固定
 * 解码程序提供，不接受客体文件里的函数地址。 */
static void codec_sort_swap(u8 *a,u8 *b,size_t size)
{
    for(size_t i=0;i<size;i++){u8 value=a[i];a[i]=b[i];b[i]=value;}
}
static void codec_sort_down(u8 *base,size_t root,size_t count,size_t size,
                            int (*compare)(const void *,const void *))
{
    while(root<count/2){
        size_t child=root*2+1;
        if(child+1<count && compare(base+child*size,base+(child+1)*size)<0)child++;
        if(compare(base+root*size,base+child*size)>=0)break;
        codec_sort_swap(base+root*size,base+child*size,size);root=child;
    }
}
void qsort(void *base,size_t count,size_t size,
           int (*compare)(const void *,const void *))
{
    if(count<2 || !size)return;
    if(!base || !compare || count>(size_t)-1/size)abort();
    u8 *bytes=base;
    for(size_t parent=count/2;parent;){parent--;codec_sort_down(bytes,parent,count,size,compare);}
    while(count>1){count--;codec_sort_swap(bytes,bytes+count*size,size);codec_sort_down(bytes,0,count,size,compare);}
}

void *malloc(size_t bytes)
{
    if(!bytes)bytes=1;
    if(bytes>CODEC_HEAP_LIMIT-64){memory_failed=1;return 0;}
    bytes=(bytes+15)&~15u;
    codec_block_t **link=&available,*block;
    while(*link && (*link)->size<bytes)link=&(*link)->next;
    if(*link){block=*link;*link=block->next;}
    else{
        u32 size=(bytes+sizeof(codec_arena_t)+sizeof(codec_block_t)+4095)&~4095u;
        if(size<65536)size=65536;
        if(size>CODEC_HEAP_LIMIT-reserved){memory_failed=1;return 0;}
        codec_arena_t *arena=sc_alloc(size);if(!arena){memory_failed=1;return 0;}
        arena->bytes=size;arena->next=arenas;arenas=arena;reserved+=size;
        block=(codec_block_t *)(arena+1);block->size=size-sizeof(*arena)-sizeof(*block);
    }
    if(block->size>=bytes+sizeof(*block)+16){
        codec_block_t *tail=(codec_block_t *)((u8 *)(block+1)+bytes);
        tail->magic=0;tail->size=block->size-bytes-sizeof(*block);tail->zero=0;
        /* 分裂尾块也维持地址有序，不插到表头。free按相邻地址合并，
         * 无序尾块会让临时解码树释放后无法重新组成整幅图的大块。 */
        codec_block_t **tail_link=&available;
        while(*tail_link&&(uintptr_t)*tail_link<(uintptr_t)tail)tail_link=&(*tail_link)->next;
        tail->next=*tail_link;*tail_link=tail;block->size=bytes;
    }
    block->magic=CODEC_LIVE;block->next=0;block->zero=0;return block+1;
}
void free(void *ptr)
{
    if(!ptr)return;
    codec_block_t *block=(codec_block_t *)ptr-1;
    if(block->magic!=CODEC_LIVE)abort();
    block->magic=0;
    /* free表按虚址排序，只合并真正相邻的块，不跨arena头。单任务
     * 没有后台线程改表；同时兼顾大PNG/VP8L临时树释放后的复用。 */
    codec_block_t **link=&available,*before=0;
    while(*link && (uintptr_t)*link<(uintptr_t)block){before=*link;link=&(*link)->next;}
    block->next=*link;*link=block;
    if(block->next && (u8 *)(block+1)+block->size==(u8 *)block->next){
        block->size+=sizeof(*block)+block->next->size;block->next=block->next->next;
    }
    if(before && (u8 *)(before+1)+before->size==(u8 *)block){before->size+=sizeof(*block)+block->size;before->next=block->next;}
}
void *calloc(size_t count,size_t size)
{
    if(size && count>CODEC_HEAP_LIMIT/size){memory_failed=1;return 0;}
    size_t bytes=count*size;void *ptr=malloc(bytes);if(ptr)memset(ptr,0,bytes);return ptr;
}
void *realloc(void *ptr,size_t size)
{
    if(!ptr)return malloc(size);
    if(!size){free(ptr);return 0;}
    codec_block_t *block=(codec_block_t *)ptr-1;
    if(block->magic!=CODEC_LIVE)abort();
    if(size<=block->size)return ptr;
    void *next=malloc(size);if(!next)return 0;
    memcpy(next,ptr,block->size);free(ptr);return next;
}
void codec_release_all(void)
{
    /* 只在第三方所有指针都不再使用后调用。逐arena保存next再交回
     * 页，不能释放当前arena后才读取其链指针。 */
    available=0;while(arenas){codec_arena_t *next=arenas->next;sc_free(arenas);arenas=next;}reserved=0;memory_failed=0;
}
void abort(void)
{
    if(active_ticket)sc_image_submit(active_ticket,0,0,0,-4);
    sc_exit(4);for(;;)sc_yield();
}

typedef union {uint64_t value;struct {u32 lo,hi;} half;} codec_u64_t;
static uint64_t divide64(uint64_t a,uint64_t b,uint64_t *remainder)
{
    codec_u64_t n,d,q,r;n.value=a;d.value=b;q.half.lo=q.half.hi=r.half.lo=r.half.hi=0;
    if(!(d.half.lo|d.half.hi))abort();
    for(int bit=63;bit>=0;bit--){
        u32 carry=r.half.hi>>31;
        r.half.hi=(r.half.hi<<1)|(r.half.lo>>31);
        r.half.lo=(r.half.lo<<1)|((bit>=32?n.half.hi>>(bit-32):n.half.lo>>bit)&1);
        if(carry || r.half.hi>d.half.hi || (r.half.hi==d.half.hi && r.half.lo>=d.half.lo)){
            u32 borrow=r.half.lo<d.half.lo;r.half.lo-=d.half.lo;r.half.hi-=d.half.hi+borrow;
            if(bit>=32)q.half.hi|=1u<<(bit-32);else q.half.lo|=1u<<bit;
        }
    }
    if(remainder)*remainder=r.value;return q.value;
}
uint64_t __udivdi3(uint64_t a,uint64_t b){return divide64(a,b,0);}
uint64_t __umoddi3(uint64_t a,uint64_t b){uint64_t r;divide64(a,b,&r);return r;}
uint64_t __udivmoddi4(uint64_t a,uint64_t b,uint64_t *r){return divide64(a,b,r);}
/* 有符号帮助入口同样从无符号幅值做除法。INT64_MIN不在有符号域
 * 取负，余数保留被除数符号；不链接宿主libgcc或递归调用自己。 */
int64_t __divdi3(int64_t a,int64_t b)
{
    uint64_t n=a<0?0u-(uint64_t)a:(uint64_t)a,d=b<0?0u-(uint64_t)b:(uint64_t)b;
    uint64_t q=divide64(n,d,0);if((a<0)!=(b<0))q=0u-q;return (int64_t)q;
}
int64_t __moddi3(int64_t a,int64_t b)
{
    uint64_t n=a<0?0u-(uint64_t)a:(uint64_t)a,d=b<0?0u-(uint64_t)b:(uint64_t)b,r;
    divide64(n,d,&r);if(a<0)r=0u-r;return (int64_t)r;
}
