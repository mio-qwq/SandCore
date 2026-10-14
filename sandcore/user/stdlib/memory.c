#include "SCCLIBPRIVATE.H"
typedef struct __scc_arena __scc_arena;
typedef struct __scc_block __scc_block;
struct __scc_arena {
    __scc_arena *next,*previous;
    size_t bytes,live;
    unsigned int dedicated,reserved[3];
};
struct __scc_block {
    size_t span,previous_span,requested;
    __scc_arena *arena;
    __scc_block *next_free,*previous_free;
    unsigned int magic,allocated;
};
/* 32B块头与16B跨度使payload自然16对齐；arena控制也不递归malloc。 */
#define SCC_HEAP_MAGIC 0x53434342u
#define SCC_HEAP_SMALL 65536u
#define SCC_HEAP_LARGE 32768u
#define SCC_HEAP_BINS 64
static __scc_arena *__scc_arenas,*__scc_warm_arena;
static __scc_block *__scc_bins[SCC_HEAP_BINS];
static unsigned int __scc_bin_bits[2];
static size_t __scc_arena_prefix(void) {
    return (sizeof(__scc_arena)+15u)&~15u;
}
static int __scc_bin(size_t span)
{
    if(span<=512u)return (int)((span-1u)/16u);
    unsigned int bound=1024u;
    int bin=32;
    while(span>=bound && bin<62){
        bound<<=1;
        bin++;
        if(!bound)break;
    }
    return bin;
}
static void __scc_free_remove(__scc_block *block)
{
    int bin=__scc_bin(block->span);
    if(block->previous_free)block->previous_free->next_free=block->next_free;
    else __scc_bins[bin]=block->next_free;
    if(block->next_free)block->next_free->previous_free=block->previous_free;
    if(!__scc_bins[bin])__scc_bin_bits[bin/32]&=~(1u<<(bin%32));
    block->next_free=block->previous_free=NULL;
}
static void __scc_free_add(__scc_block *block)
{
    int bin=__scc_bin(block->span);
    block->previous_free=NULL;
    block->next_free=__scc_bins[bin];
    if(block->next_free)block->next_free->previous_free=block;
    __scc_bins[bin]=block;
    __scc_bin_bits[bin/32]|=1u<<(bin%32);
}
static __scc_block *__scc_block_after(__scc_block *block)
{
    unsigned char *end=(unsigned char *)block+block->span;
    if(end==(unsigned char *)block->arena+block->arena->bytes)return NULL;
    return (__scc_block *)end;
}
static void __scc_block_set(__scc_block *block,__scc_arena *arena,size_t span,size_t previous)
{
    block->span=span;
    block->previous_span=previous;
    block->requested=0;
    block->arena=arena;
    block->next_free=block->previous_free=NULL;
    block->magic=SCC_HEAP_MAGIC;
    block->allocated=0;
}
static __scc_block *__scc_new_arena(size_t needed)
{
    size_t prefix=__scc_arena_prefix();
    if(needed>SIZE_MAX-prefix-4095u){
        errno=ENOMEM;
        return NULL;
    }
    unsigned int dedicated=needed>=SCC_HEAP_LARGE;
    size_t bytes=dedicated?(needed+prefix+4095u)&~4095u:SCC_HEAP_SMALL;
    if(bytes>0x04000000u){errno=ENOMEM;return NULL;}
    void *memory=NULL;
    ScStatus status=ScMemoryAllocatePages(bytes,&memory);
    if(status){
        if(status==SC32_E_MEMORY || status==SC32_E_RANGE || status==SC32_E_NO_SPACE)errno=ENOMEM;
        else __scc_from_status(status);
        return NULL;
    }
    __scc_arena *arena=memory;
    arena->bytes=bytes;
    arena->live=0;
    arena->dedicated=dedicated;
    arena->previous=NULL;
    arena->next=__scc_arenas;
    if(arena->next)arena->next->previous=arena;
    __scc_arenas=arena;
    __scc_block *block=(__scc_block *)((unsigned char *)arena+prefix);
    __scc_block_set(block,arena,bytes-prefix,0);
    return block;
}
static void __scc_split(__scc_block *block,size_t wanted)
{
    if(block->arena->dedicated || block->span-wanted<sizeof(__scc_block)+16u)return;
    size_t remaining=block->span-wanted;
    block->span=wanted;
    __scc_block *rest=(__scc_block *)((unsigned char *)block+wanted);
    __scc_block_set(rest,block->arena,remaining,wanted);
    __scc_block *after=__scc_block_after(rest);
    if(after && !after->allocated){
        __scc_free_remove(after);
        rest->span+=after->span;
        after=__scc_block_after(rest);
    }
    if(after)after->previous_span=rest->span;
    __scc_free_add(rest);
}
static __scc_block *__scc_checked(void *memory)
{
    /* 非起点/重复free是调用方UB；损坏状态不能继续作为链地址写入。 */
    __scc_block *block=(__scc_block *)((unsigned char *)memory-sizeof(__scc_block));
    if(((uintptr_t)memory&15u) || block->magic!=SCC_HEAP_MAGIC || !block->allocated
    || block->span<sizeof(__scc_block)+16u || (block->span&15u)
    || !block->arena || block->span>block->arena->bytes
    || (unsigned char *)block<(unsigned char *)block->arena+__scc_arena_prefix()
    || (unsigned char *)block+block->span>(unsigned char *)block->arena+block->arena->bytes){
        __scc_early_write("SCCLIB: damaged allocation\n");
        abort();
    }
    return block;
}
void *malloc(size_t bytes)
{
    if(!bytes)return NULL;
    if(bytes>SIZE_MAX-sizeof(__scc_block)-15u){
        errno=ENOMEM;
        return NULL;
    }
    size_t wanted=(bytes+sizeof(__scc_block)+15u)&~15u;
    __scc_block *block=NULL;
    for(int bin=__scc_bin(wanted);bin<SCC_HEAP_BINS;bin++){
        if(!(__scc_bin_bits[bin/32]&(1u<<(bin%32))))continue;
        for(block=__scc_bins[bin];block && block->span<wanted;block=block->next_free){
        }
        if(block){
            __scc_free_remove(block);
            break;
        }
    }
    if(!block){
        block=__scc_new_arena(wanted);
        if(!block)return NULL;
    }
    if(__scc_warm_arena==block->arena)__scc_warm_arena=NULL;
    __scc_split(block,wanted);
    block->allocated=1;
    block->requested=bytes;
    block->arena->live++;
    return (unsigned char *)block+sizeof(__scc_block);
}
static int __scc_arena_release(__scc_arena *arena)
{
    __scc_arena *previous=arena->previous,*next=arena->next;
    ScStatus status=ScMemoryFreePages(arena);
    if(status){
        __scc_from_status(status);
        return -1;
    }
    /* 页面已不存在，以下仅写相邻arena和库BSS，不再读arena本身。 */
    if(previous)previous->next=next;
    else __scc_arenas=next;
    if(next)next->previous=previous;
    if(__scc_warm_arena==arena)__scc_warm_arena=NULL;
    return 0;
}
void free(void *memory)
{
    if(!memory)return;
    __scc_block *block=__scc_checked(memory);
    __scc_arena *arena=block->arena;
    block->allocated=0;
    block->requested=0;
    arena->live--;
    __scc_block *after=__scc_block_after(block);
    if(after && !after->allocated){
        __scc_free_remove(after);
        block->span+=after->span;
    }
    if(block->previous_span){
        __scc_block *before=(__scc_block *)((unsigned char *)block-block->previous_span);
        if(!before->allocated){
            __scc_free_remove(before);
            before->span+=block->span;
            block=before;
        }
    }
    after=__scc_block_after(block);
    if(after)after->previous_span=block->span;
    if(!arena->live && (arena->dedicated || __scc_warm_arena)){
        if(!__scc_arena_release(arena))return;
    }
    if(!arena->live && !arena->dedicated && !__scc_warm_arena)__scc_warm_arena=arena;
    __scc_free_add(block);
}
void *calloc(size_t count,size_t bytes)
{
    if(!count || !bytes)return NULL;
    if(count>SIZE_MAX/bytes){
        errno=ENOMEM;
        return NULL;
    }
    size_t total=count*bytes;
    void *memory=malloc(total);
    if(memory)memset(memory,0,total);
    return memory;
}
void *realloc(void *memory,size_t bytes)
{
    if(!memory)return malloc(bytes);
    if(!bytes){
        free(memory);
        return NULL;
    }
    if(bytes>SIZE_MAX-sizeof(__scc_block)-15u){
        errno=ENOMEM;
        return NULL;
    }
    __scc_block *block=__scc_checked(memory);
    size_t old=block->requested;
    size_t wanted=(bytes+sizeof(__scc_block)+15u)&~15u;
    if(wanted<=block->span){
        __scc_split(block,wanted);
        block->requested=bytes;
        return memory;
    }
    __scc_block *after=__scc_block_after(block);
    if(after && !after->allocated && after->span>=wanted-block->span){
        __scc_free_remove(after);
        block->span+=after->span;
        after=__scc_block_after(block);
        if(after)after->previous_span=block->span;
        __scc_split(block,wanted);
        block->requested=bytes;
        return memory;
    }
    void *replacement=malloc(bytes);
    if(!replacement)return NULL;
    memcpy(replacement,memory,old<bytes?old:bytes);
    free(memory);
    return replacement;
}
static void __scc_heap_finish(void)
{
    /* 正常fini先关闭FILE/回调，再按arena整块归还；异常由kernel owner链。 */
    __scc_warm_arena=NULL;
    for(int bin=0;bin<SCC_HEAP_BINS;bin++)__scc_bins[bin]=NULL;
    __scc_bin_bits[0]=__scc_bin_bits[1]=0;
    while(__scc_arenas){
        if(__scc_arena_release(__scc_arenas))break;
    }
}
