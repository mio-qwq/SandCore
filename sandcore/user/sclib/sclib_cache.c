#include "SCLIBPRIVATE.H"

typedef struct SlCacheEntry SlCacheEntry;
struct SlCacheEntry {
    SlCacheEntry *bucket,*previous,*next;
    unsigned char *key;
    ScU32 key_bytes,hash,cost,leases,valid;
    void *resource;
    ScCacheStamp stamp;
};
typedef struct {ScU32 id;
    SlCacheEntry *entry;} SlCacheLease;
struct ScResourceCache {
    SlBase base;
    SlCacheEntry **buckets,*first,*last;
    ScHash *lease_index;
    ScU32 capacity,count,budget,used,generation;
    ScCacheLoadProc load;
    ScCacheDropProc drop;
    void *context;
    ScWideCounter hits,misses,evictions;
};
static SlCacheEntry *sl_cache_find(const ScResourceCache *cache,const void *key,ScU32 bytes,ScU32 code)
{
    if(!cache->capacity)return 0;
    for(SlCacheEntry *p=cache->buckets[code&(cache->capacity-1u)];p;p=p->bucket){
        if(p->valid && p->hash==code && p->key_bytes==bytes && (!bytes || !memcmp(p->key,key,bytes)))return p;
    }
    return 0;
}
static void sl_cache_lru_remove(ScResourceCache *cache,SlCacheEntry *entry)
{
    if(entry->previous)entry->previous->next=entry->next;
    else cache->first=entry->next;
    if(entry->next)entry->next->previous=entry->previous;
    else cache->last=entry->previous;
}
static void sl_cache_lru_first(ScResourceCache *cache,SlCacheEntry *entry)
{
    entry->previous=0;
    entry->next=cache->first;
    if(cache->first)cache->first->previous=entry;
    else cache->last=entry;
    cache->first=entry;
}
static void sl_cache_drop(ScResourceCache *cache,SlCacheEntry *entry,int quiet)
{
    SlCacheEntry **link=cache->buckets+(entry->hash&(cache->capacity-1u));
    while(*link!=entry)link=&(*link)->bucket;
    *link=entry->bucket;
    sl_cache_lru_remove(cache,entry);
    cache->used-=entry->cost;
    cache->count--;
    /* 从所有可查链撤掉再进drop。正常drop中再次清同cache不能二次
     * 触及这个resource；晚fini不调用可能已清context的用户函数。 */
    if(!quiet)cache->drop(entry->resource,cache->context);
    free(entry->key);
    free(entry);
}
static void sl_cache_dispose(SlBase *base,int quiet)
{
    ScResourceCache *cache=(ScResourceCache *)base;
    while(cache->first)sl_cache_drop(cache,cache->first,quiet);
    if(cache->lease_index)ScHashRelease(cache->lease_index);
    free(cache->buckets);
    cache->buckets=0;
    cache->lease_index=0;
    cache->capacity=0;
}
ScStatus ScCacheCreate(const ScCacheDesc *desc,ScResourceCache **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(!desc->load || !desc->drop || !desc->budget_bytes || desc->reserved[0] || desc->reserved[1])return SC_UTIL_E_INVALID;
    ScResourceCache *cache=sl_object(SL_CACHE,sizeof(*cache),sl_cache_dispose);
    if(!cache)return SC_UTIL_E_MEMORY;
    ScHashDesc index={32,1,sizeof(SlCacheLease),0,0,0,{0,0}};
    status=ScHashCreate(&index,&cache->lease_index);
    if(!status)status=sl_pin((const void *)desc->load);
    if(!status)status=sl_pin((const void *)desc->drop);
    if(status){ScCacheRelease(cache);
        return status;}
    cache->budget=desc->budget_bytes;
    cache->load=desc->load;
    cache->drop=desc->drop;
    cache->context=desc->context;
    cache->generation=1;
    *out=cache;
    return 0;
}
ScStatus ScCacheRetain(ScResourceCache *cache){return sl_retain(cache,SL_CACHE);}
void ScCacheRelease(ScResourceCache *cache){sl_release(cache,SL_CACHE);}
static ScStatus sl_cache_lease(ScResourceCache *cache,SlCacheEntry *entry,ScU32 *token)
{
    if(entry->leases==0xFFFFFFFFu || cache->base.depth==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    SlCacheLease lease;
    ScStatus status=sl_identifier(&lease.id);
    if(status)return status;
    lease.entry=entry;
    status=ScHashSet(cache->lease_index,&lease.id,4,&lease,SC_HASH_INSERT_ONLY);
    if(status)return status;
    /* 每个lease占一个内部pin；用户Release(cache)只停止新Acquire，
     * 还活的借用resource不会随header一起释放。最后ReleaseLease才收。 */
    entry->leases++;
    cache->base.depth++;
    *token=lease.id;
    return 0;
}
ScStatus ScCacheAcquire(ScResourceCache *cache,const void *key,ScU32 key_bytes,const ScCacheStamp *stamp,void **resource,ScU32 *lease)
{
    ScStatus status=sl_enter(cache,SL_CACHE);
    if(status)return status;
    status=sl_mutation(&cache->base,cache->generation);
    if(!status)status=sl_input(stamp,sizeof(*stamp));
    if(!status && (!resource || !lease || (void *)resource==(void *)lease || (key_bytes && !key) || key_bytes>0xFFFFFFFFu-sizeof(SlCacheEntry)-1u))status=SC_UTIL_E_INVALID;
    if(status){sl_leave(cache);
        return status;}
    ScU32 code=ScHashBytes(key,key_bytes);
    SlCacheEntry *existing=sl_cache_find(cache,key,key_bytes,code);
    if(existing && !memcmp(&existing->stamp,stamp,sizeof(*stamp))){
        ScU32 token;
        status=sl_cache_lease(cache,existing,&token);
        if(!status){sl_cache_lru_remove(cache,existing);
            sl_cache_lru_first(cache,existing);
            sl_counter_add(&cache->hits,1);
            *resource=existing->resource;
            *lease=token;}
        sl_leave(cache);
        return status;
    }
    ScU32 capacity=sl_hash_capacity(cache->capacity,cache->count+1u);
    if(!capacity){sl_leave(cache);
        return SC_UTIL_E_RANGE;}
    SlCacheEntry *fresh=calloc(1,sizeof(*fresh));
    SlCacheEntry **table=capacity==cache->capacity?0:calloc(capacity,sizeof(*table));
    if(!fresh || (capacity!=cache->capacity && !table)){free(fresh);
        free(table);
        sl_leave(cache);
        return SC_UTIL_E_MEMORY;}
    fresh->key=malloc(key_bytes+1u);
    if(!fresh->key){free(fresh);
        free(table);
        sl_leave(cache);
        return SC_UTIL_E_MEMORY;}
    if(key_bytes)memcpy(fresh->key,key,key_bytes);
    fresh->key[key_bytes]=0;
    fresh->key_bytes=key_bytes;
    fresh->hash=code;
    fresh->stamp=*stamp;
    ScView view={20,1,fresh->key,key_bytes,0};
    ScU32 cost=0;
    cache->base.mutation=1;
    status=cache->load(&view,&fresh->stamp,cache->context,&fresh->resource,&cost);
    int loaded=status==0;
    if(!status && cache->base.closing)status=SC_UTIL_E_CANCELLED;
    if(!status && cost>0xFFFFFFFFu-sizeof(*fresh)-key_bytes-1u)status=SC_UTIL_E_RANGE;
    if(!status){fresh->cost=cost+sizeof(*fresh)+key_bytes+1u;
        if(fresh->cost>cache->budget)status=SC_UTIL_E_RANGE;}
    ScU32 needed=0;
    if(!status){
        needed=fresh->cost>cache->budget-cache->used?fresh->cost-(cache->budget-cache->used):0u;
        ScU32 available=0;
        for(SlCacheEntry *p=cache->last;p && available<needed;p=p->previous){if(!p->leases)available+=p->cost;}
        if(available<needed)status=SC_UTIL_E_BUSY;
    }
    ScU32 token=0;
    if(!status)status=sl_cache_lease(cache,fresh,&token);
    if(status){
        if(loaded || fresh->resource)cache->drop(fresh->resource,cache->context);
        cache->base.mutation=0;
        free(fresh->key);
        free(fresh);
        free(table);
        sl_leave(cache);
        return status;
    }
    if(table){
        for(SlCacheEntry *p=cache->first;p;p=p->next){p->bucket=table[p->hash&(capacity-1u)];
            table[p->hash&(capacity-1u)]=p;}
        free(cache->buckets);
        cache->buckets=table;
        cache->capacity=capacity;
    }
    if(existing){
        existing->valid=0;
        if(!existing->leases){
            ScU32 charge=existing->cost;
            sl_cache_drop(cache,existing,0);
            needed=charge>=needed?0u:needed-charge;
        }
    }
    while(needed){
        SlCacheEntry *victim=cache->last;
        while(victim && victim->leases)victim=victim->previous;
        ScU32 charge=victim->cost;
        sl_cache_drop(cache,victim,0);
        sl_counter_add(&cache->evictions,1);
        needed=charge>=needed?0u:needed-charge;
    }
    /* 只有仍有lease的旧stamp留下来；Invalidate/ReleaseLease都及时
     * 清无lease项，无需每次miss全扫缓存，更不能在drop后重读可能
     * 正好位于旧resource里的caller key。候选已拥有独立key。 */
    fresh->valid=1;
    fresh->bucket=cache->buckets[code&(cache->capacity-1u)];
    cache->buckets[code&(cache->capacity-1u)]=fresh;
    sl_cache_lru_first(cache,fresh);
    cache->used+=fresh->cost;
    cache->count++;
    cache->generation++;
    sl_counter_add(&cache->misses,1);
    cache->base.mutation=0;
    *resource=fresh->resource;
    *lease=token;
    sl_leave(cache);
    return 0;
}
ScStatus ScCacheReleaseLease(ScResourceCache *cache,ScU32 token)
{
    if(!cache || cache->base.kind!=SL_CACHE || sl_stopping)return SC_UTIL_E_STALE;
    if(cache->base.mutation)return SC_UTIL_E_BUSY;
    if(cache->base.depth==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    cache->base.depth++;
    SlHashNode *found=sl_hash_find(cache->lease_index,&token,4,ScHashBytes(&token,4));
    if(!found){sl_leave(cache);
        return SC_UTIL_E_STALE;}
    SlCacheEntry *entry=((SlCacheLease *)found->value)->entry;
    ScStatus status=ScHashRemove(cache->lease_index,&token,4);
    if(status){sl_leave(cache);
        return status;}
    entry->leases--;
    cache->base.depth--;
    cache->base.mutation=1;
    if(!entry->leases && (!entry->valid || cache->base.closing))sl_cache_drop(cache,entry,0);
    cache->base.mutation=0;
    sl_leave(cache);
    return 0;
}
ScStatus ScCacheInvalidate(ScResourceCache *cache,const void *key,ScU32 key_bytes)
{
    ScStatus status=sl_enter(cache,SL_CACHE);
    if(status)return status;
    status=sl_mutation(&cache->base,cache->generation);
    if(!status && key_bytes && !key)status=SC_UTIL_E_INVALID;
    SlCacheEntry *entry=status?0:sl_cache_find(cache,key,key_bytes,ScHashBytes(key,key_bytes));
    if(!status && !entry)status=SC_UTIL_E_NOT_FOUND;
    if(status){sl_leave(cache);
        return status;}
    entry->valid=0;
    cache->generation++;
    cache->base.mutation=1;
    if(!entry->leases)sl_cache_drop(cache,entry,0);
    cache->base.mutation=0;
    sl_leave(cache);
    return 0;
}
ScStatus ScCacheClear(ScResourceCache *cache)
{
    ScStatus status=sl_enter(cache,SL_CACHE);
    if(status)return status;
    status=sl_mutation(&cache->base,cache->generation);
    if(status){sl_leave(cache);
        return status;}
    if(cache->count){
        cache->generation++;
        cache->base.mutation=1;
        for(SlCacheEntry *p=cache->first;p;){SlCacheEntry *next=p->next;
            p->valid=0;
            if(!p->leases)sl_cache_drop(cache,p,0);
            p=next;}
        cache->base.mutation=0;
    }
    sl_leave(cache);
    return 0;
}
ScStatus ScCacheSetBudget(ScResourceCache *cache,ScU32 bytes)
{
    ScStatus status=sl_enter(cache,SL_CACHE);
    if(status)return status;
    status=sl_mutation(&cache->base,cache->generation);
    if(!status && !bytes)status=SC_UTIL_E_INVALID;
    if(status){sl_leave(cache);
        return status;}
    ScU32 needed=cache->used>bytes?cache->used-bytes:0u,available=0;
    for(SlCacheEntry *p=cache->last;p && available<needed;p=p->previous){if(!p->leases)available+=p->cost;}
    if(available<needed){sl_leave(cache);
        return SC_UTIL_E_BUSY;}
    cache->budget=bytes;
    cache->generation++;
    cache->base.mutation=1;
    while(cache->used>bytes){SlCacheEntry *entry=cache->last;
        while(entry->leases)entry=entry->previous;
        sl_cache_drop(cache,entry,0);
        sl_counter_add(&cache->evictions,1);}
    cache->base.mutation=0;
    sl_leave(cache);
    return 0;
}
ScStatus ScCacheQuery(const ScResourceCache *cache,ScCacheInfo *out)
{
    ScStatus status=sl_valid(cache,SL_CACHE);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    ScCacheInfo value;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.budget_bytes=cache->budget;
    value.used_bytes=cache->used;
    value.entries=cache->count;
    value.leases=cache->lease_index->count;
    value.revision=cache->generation;
    value.hits=cache->hits;
    value.misses=cache->misses;
    value.evictions=cache->evictions;
    memcpy(out,&value,sizeof(value));
    return 0;
}
