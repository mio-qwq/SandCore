#include "objpool.h"
#include "memory.h"

/* 8+9+9+5=31。索引节点装入一页，叶块含32个16字节对齐的对象。
 * 位图逐层记录“已满/仍有活对象”，申请与空池枚举均不扫历史PID。
 * 对象本体从不搬家；扩容只加独立节点/叶块，因此嵌套spawn之后
 * 调用方握住的对象指针仍有效。页面数独立计账，空块归还可审计。 */
struct objpool_node {
    void *child[512];
    u32 full[16],live[16],children;
};
typedef char objpool_node_fits_page[(sizeof(objpool_node_t)<=4096)?1:-1];
typedef struct {
    u32 used,pages,history;
    u32 reserved[13]; /* 正文从64字节边界开始，满足FXSAVE的16字节要求。 */
} objpool_block_t;
typedef char objpool_body_alignment[(sizeof(objpool_block_t)==64)?1:-1];

static void zero(void *address,u32 bytes)
{u8 *p=address;for(u32 i=0;i<bytes;i++)p[i]=0;}
static void bit(u32 *map,u32 index,int set)
{if(set)map[index>>5]|=1u<<(index&31);else map[index>>5]&=~(1u<<(index&31));}
static int any(const u32 *map,u32 words)
{for(u32 i=0;i<words;i++)if(map[i])return 1;return 0;}
static int all(const u32 *map)
{for(u32 i=0;i<16;i++)if(map[i]!=0xFFFFFFFFu)return 0;return 1;}
static int find(const u32 *map,u32 words,u32 start,int invert)
{
    if(start>=words*32)return -1;
    for(u32 word=start>>5;word<words;word++){
        u32 value=invert?~map[word]:map[word];
        if(word==(start>>5))value&=0xFFFFFFFFu<<(start&31);
        if(value)return (int)(word*32+(u32)__builtin_ctz(value));
    }
    return -1;
}
static void *pages_new(objpool_t *pool,u32 pages)
{
    u32 address=pframe_alloc_run(pages);if(!address)return 0;
    zero((void *)address,pages*4096u);pool->pages+=pages;
    if(pool->pages>pool->peak_pages)pool->peak_pages=pool->pages;
    return (void *)address;
}
static void pages_drop(objpool_t *pool,void *address,u32 pages)
{pframe_free_run((u32)address,pages);pool->pages-=pages;}
int objpool_init(objpool_t *pool,u32 bytes,u32 flags)
{
    /* 检查的是32位地址/乘法域，不设人为的对象数门槛。 */
    if(!pool || !bytes || bytes>0x07FFFF00u || (flags&~OBJPOOL_KEEP_IDLE))return -1;
    zero(pool,sizeof(*pool));pool->stride=(bytes+15u)&~15u;pool->flags=flags;return 0;
}
static objpool_block_t *block(const objpool_t *pool,int id)
{
    if(!pool || id<0)return 0;
    u32 key=(u32)id;objpool_node_t *a=pool->roots[key>>23];
    if(!a)return 0;objpool_node_t *b=a->child[(key>>14)&511];
    return b?b->child[(key>>5)&511]:0;
}
void *objpool_get(const objpool_t *pool,int id)
{
    objpool_block_t *p=block(pool,id);
    if(!p || !(p->used&(1u<<((u32)id&31))))return 0;
    return (u8 *)p+64+((u32)id&31)*pool->stride;
}
void *objpool_history(const objpool_t *pool,int id)
{
    if(!pool || id<0)return 0;
    if(!(pool->flags&OBJPOOL_KEEP_IDLE))return objpool_get(pool,id);
    objpool_block_t *p=block(pool,id);
    return p && ((p->used|p->history)&(1u<<((u32)id&31)))?(u8 *)p+64+((u32)id&31)*pool->stride:0;
}
void objpool_remember(objpool_t *pool,int id)
{
    objpool_block_t *p=block(pool,id);
    if(p && (pool->flags&OBJPOOL_KEEP_IDLE))p->history|=p->used&(1u<<((u32)id&31));
}
int objpool_claim(objpool_t *pool,int id)
{
    if(!pool || !pool->stride || id<0)return -1;
    u32 key=(u32)id,r=key>>23,m=(key>>14)&511,l=(key>>5)&511,s=key&31;
    objpool_node_t *a=pool->roots[r],*b=a?a->child[m]:0;
    objpool_block_t *p=b?b->child[l]:0;
    if(p && (p->used&(1u<<s)))return -1;
    int new_a=0,new_b=0;
    if(!a){a=pages_new(pool,1);if(!a)return -1;pool->roots[r]=a;new_a=1;}
    if(!b){b=pages_new(pool,1);if(!b)goto failed;a->child[m]=b;a->children++;new_b=1;}
    if(!p){
        u32 pages=(64u+32u*pool->stride+4095u)/4096u;
        p=pages_new(pool,pages);if(!p)goto failed;p->pages=pages;b->child[l]=p;b->children++;
    }
    /* 兼容历史只能在调用方提交创建之后覆盖；普通对象禁止读上任载荷。 */
    if(!(pool->flags&OBJPOOL_KEEP_IDLE))zero((u8 *)p+64+s*pool->stride,pool->stride);
    p->used|=1u<<s;pool->objects++;bit(b->live,l,1);bit(a->live,m,1);bit(pool->live,r,1);
    bit(b->full,l,p->used==0xFFFFFFFFu);bit(a->full,m,all(b->full));bit(pool->full,r,all(a->full));
    if(key+1>pool->high_water)pool->high_water=key+1;return 0;
failed:
    if(new_b){a->child[m]=0;a->children--;pages_drop(pool,b,1);}
    if(new_a){pool->roots[r]=0;pages_drop(pool,a,1);}
    return -1;
}
int objpool_alloc(objpool_t *pool)
{
    if(!pool || !pool->stride)return -1;
    int r=find(pool->full,8,0,1);if(r<0)return -1;
    objpool_node_t *a=pool->roots[r];int m=a?find(a->full,16,0,1):0;
    objpool_node_t *b=a?a->child[m]:0;int l=b?find(b->full,16,0,1):0;
    objpool_block_t *p=b?b->child[l]:0;
    u32 s=p?(u32)__builtin_ctz(~p->used):0;
    int id=(int)(((u32)r<<23)|((u32)m<<14)|((u32)l<<5)|s);
    return objpool_claim(pool,id)?-1:id;
}
void objpool_release(objpool_t *pool,int id)
{
    objpool_block_t *p=block(pool,id);if(!p)return;
    u32 key=(u32)id,r=key>>23,m=(key>>14)&511,l=(key>>5)&511,s=key&31;
    if(!(p->used&(1u<<s)))return;
    objpool_node_t *a=pool->roots[r],*b=a->child[m];
    p->used&=~(1u<<s);pool->objects--;bit(b->full,l,0);bit(a->full,m,0);bit(pool->full,r,0);
    if(p->used)return;
    bit(b->live,l,0);if(!any(b->live,16))bit(a->live,m,0);
    if(!any(a->live,16))bit(pool->live,r,0);
    /* 首次增长后创建失败的空块没有可查询历史，必须真正归还页面。
     * 已提交的PID历史才缓存，不能用KEEP_IDLE掩盖失败回滚泄漏。 */
    if((pool->flags&OBJPOOL_KEEP_IDLE) && p->history)return;
    b->child[l]=0;b->children--;pages_drop(pool,p,p->pages);
    if(b->children)return;
    a->child[m]=0;a->children--;pages_drop(pool,b,1);
    if(a->children)return;
    pool->roots[r]=0;pages_drop(pool,a,1);
}
int objpool_next(const objpool_t *pool,int after)
{
    if(!pool || after==0x7FFFFFFF)return -1;
    u32 start=after<0?0:(u32)after+1;
    for(int r=find(pool->live,8,start>>23,0);r>=0;r=find(pool->live,8,(u32)r+1,0)){
        objpool_node_t *a=pool->roots[r];u32 m0=(u32)r==(start>>23)?(start>>14)&511:0;
        for(int m=find(a->live,16,m0,0);m>=0;m=find(a->live,16,(u32)m+1,0)){
            objpool_node_t *b=a->child[m];u32 prefix=((u32)r<<23)|((u32)m<<14);
            u32 l0=prefix==(start&0x7FFFC000u)?(start>>5)&511:0;
            for(int l=find(b->live,16,l0,0);l>=0;l=find(b->live,16,(u32)l+1,0)){
                objpool_block_t *p=b->child[l];u32 base=prefix|((u32)l<<5);
                u32 used=p->used;if(base==(start&~31u))used&=0xFFFFFFFFu<<(start&31);
                if(used)return (int)(base+(u32)__builtin_ctz(used));
            }
        }
    }
    return -1;
}
void objpool_destroy(objpool_t *pool)
{
    if(!pool)return;
    for(u32 r=0;r<256;r++){
        objpool_node_t *a=pool->roots[r];if(!a)continue;
        for(u32 m=0;m<512;m++){
            objpool_node_t *b=a->child[m];if(!b)continue;
            for(u32 l=0;l<512;l++){objpool_block_t *p=b->child[l];if(p)pages_drop(pool,p,p->pages);}
            pages_drop(pool,b,1);
        }
        pages_drop(pool,a,1);
    }
    zero(pool,sizeof(*pool));
}
