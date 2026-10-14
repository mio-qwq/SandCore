#include "SCLIBPRIVATE.H"

ScU32 ScHashBytes(const void *bytes,ScU32 count)
{
    const unsigned char *p=bytes;
    ScU32 hash=2166136261u;
    for(ScU32 i=0;i<count;i++)hash=(hash^p[i])*16777619u;
    return hash;
}
static SlHashNode *sl_hash_find(const ScHash *hash,const void *key,ScU32 bytes,ScU32 code)
{
    if(!hash->capacity)return 0;
    for(SlHashNode *p=hash->buckets[code&(hash->capacity-1u)];p;p=p->bucket){
        if(p->hash==code && p->key_bytes==bytes && (!bytes || !memcmp(p->key,key,bytes)))return p;
    }
    return 0;
}
static void sl_hash_collect(ScHash *hash,int quiet)
{
    if(hash->walk)return;
    while(hash->retired){
        SlHashNode *p=hash->retired;
        hash->retired=p->retired;
        if(hash->drop && (!quiet || hash->trusted_drop))hash->drop(p->value,hash->context);
        free(p->key);
        free(p->value);
        free(p);
    }
}
static void sl_hash_unlink(ScHash *hash,SlHashNode *p)
{
    SlHashNode **link=hash->buckets+(p->hash&(hash->capacity-1u));
    while(*link!=p)link=&(*link)->bucket;
    *link=p->bucket;
    if(p->previous)p->previous->next=p->next;
    else hash->first=p->next;
    if(p->next)p->next->previous=p->previous;
    else hash->last=p->previous;
    p->alive=0;
    p->retired=hash->retired;
    hash->retired=p;
    hash->count--;
}
static void sl_hash_dispose(SlBase *base,int quiet)
{
    ScHash *hash=(ScHash *)base;
    while(hash->first)sl_hash_unlink(hash,hash->first);
    hash->walk=0;
    sl_hash_collect(hash,quiet);
    free(hash->buckets);
    hash->buckets=0;
    hash->capacity=0;
}
static ScU32 sl_hash_capacity(ScU32 current,ScU32 entries)
{
    if(entries>0x10000000u)return 0;
    ScU32 wanted=entries?entries*2u:8u,capacity=current?current:8u;
    while(capacity<wanted){if(capacity>0x10000000u)return 0;
        capacity*=2u;}
    return capacity;
}
static SlHashNode **sl_hash_table(ScHash *hash,ScU32 capacity)
{
    SlHashNode **fresh=calloc(capacity,sizeof(*fresh));
    if(!fresh)return 0;
    /* cached hash免去扩容时反复扫描大key，所有entry都只重链一次。 */
    for(SlHashNode *p=hash->first;p;p=p->next){p->bucket=fresh[p->hash&(capacity-1u)];
        fresh[p->hash&(capacity-1u)]=p;}
    return fresh;
}
ScStatus ScHashCreate(const ScHashDesc *desc,ScHash **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(!desc->value_bytes || desc->reserved[0] || desc->reserved[1])return SC_UTIL_E_INVALID;
    ScU32 capacity=sl_hash_capacity(0,desc->initial_capacity);
    if(!capacity)return SC_UTIL_E_RANGE;
    ScHash *hash=sl_object(SL_HASH,sizeof(*hash),sl_hash_dispose);
    if(!hash)return SC_UTIL_E_MEMORY;
    hash->buckets=calloc(capacity,sizeof(*hash->buckets));
    if(!hash->buckets){ScHashRelease(hash);
        return SC_UTIL_E_MEMORY;}
    hash->capacity=capacity;
    hash->width=desc->value_bytes;
    hash->generation=1;
    status=sl_pin((const void *)desc->drop);
    if(status){ScHashRelease(hash);
        return status;}
    hash->drop=desc->drop;
    hash->context=desc->context;
    *out=hash;
    return 0;
}
ScStatus ScHashRetain(ScHash *hash){return sl_retain(hash,SL_HASH);}
void ScHashRelease(ScHash *hash){sl_release(hash,SL_HASH);}
ScStatus ScHashQuery(const ScHash *hash,ScContainerInfo *out)
{
    ScStatus status=sl_valid(hash,SL_HASH);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,hash->count,hash->capacity/2u,hash->width,hash->generation,0);
    return 0;
}
ScStatus ScHashReserve(ScHash *hash,ScU32 entries)
{
    ScStatus status=sl_valid(hash,SL_HASH);
    if(status)return status;
    status=sl_mutation(&hash->base,hash->generation);
    if(status)return status;
    ScU32 capacity=sl_hash_capacity(hash->capacity,entries);
    if(!capacity)return SC_UTIL_E_RANGE;
    if(capacity==hash->capacity)return 0;
    SlHashNode **fresh=sl_hash_table(hash,capacity);
    if(!fresh)return SC_UTIL_E_MEMORY;
    free(hash->buckets);
    hash->buckets=fresh;
    hash->capacity=capacity;
    hash->generation++;
    return 0;
}
ScStatus ScHashSet(ScHash *hash,const void *key,ScU32 key_bytes,const void *value,ScU32 flags)
{
    ScStatus status=sl_enter(hash,SL_HASH);
    if(status)return status;
    status=sl_mutation(&hash->base,hash->generation);
    if(!status && ((!key && key_bytes) || !value || key_bytes==0xFFFFFFFFu || (flags&~3u) || flags==3u))status=SC_UTIL_E_INVALID;
    if(status){sl_leave(hash);
        return status;}
    ScU32 code=ScHashBytes(key,key_bytes);
    SlHashNode *existing=sl_hash_find(hash,key,key_bytes,code);
    if((existing && (flags&SC_HASH_INSERT_ONLY)) || (!existing && (flags&SC_HASH_REPLACE_ONLY))){sl_leave(hash);
        return existing?SC_UTIL_E_EXISTS:SC_UTIL_E_NOT_FOUND;}
    if(existing){
        if(existing->depth){sl_leave(hash);
            return SC_UTIL_E_BUSY;}
        if(value==existing->value){sl_leave(hash);
            return 0;}
        if(!hash->drop){memmove(existing->value,value,hash->width);
            hash->generation++;
            sl_leave(hash);
            return 0;}
        unsigned char *fresh=malloc(hash->width);
        if(!fresh){sl_leave(hash);
            return SC_UTIL_E_MEMORY;}
        memcpy(fresh,value,hash->width);
        unsigned char *old=existing->value;
        existing->value=fresh;
        hash->generation++;
        hash->base.mutation=1;
        if(hash->drop)hash->drop(old,hash->context);
        hash->base.mutation=0;
        free(old);
        sl_leave(hash);
        return 0;
    }
    ScU32 capacity=sl_hash_capacity(hash->capacity,hash->count+1u);
    if(!capacity){sl_leave(hash);
        return SC_UTIL_E_RANGE;}
    SlHashNode *fresh=calloc(1,sizeof(*fresh));
    if(!fresh){sl_leave(hash);
        return SC_UTIL_E_MEMORY;}
    fresh->key=malloc(key_bytes+1u);
    fresh->value=malloc(hash->width);
    if(!fresh->key || !fresh->value){free(fresh->key);
        free(fresh->value);
        free(fresh);
        sl_leave(hash);
        return SC_UTIL_E_MEMORY;}
    if(key_bytes)memcpy(fresh->key,key,key_bytes);
    fresh->key[key_bytes]=0;
    memcpy(fresh->value,value,hash->width);
    fresh->hash=code;
    fresh->key_bytes=key_bytes;
    fresh->alive=1;
    SlHashNode **table=capacity==hash->capacity?0:calloc(capacity,sizeof(*table));
    if(capacity!=hash->capacity && !table){free(fresh->key);
        free(fresh->value);
        free(fresh);
        sl_leave(hash);
        return SC_UTIL_E_MEMORY;}
    status=sl_identifier(&fresh->token);
    if(status){free(table);
        free(fresh->key);
        free(fresh->value);
        free(fresh);
        sl_leave(hash);
        return status;}
    /* 所有候选和token已齐，重链后不再有失败点。失败不会改旧桶链。 */
    if(table){
        for(SlHashNode *p=hash->first;p;p=p->next){p->bucket=table[p->hash&(capacity-1u)];
            table[p->hash&(capacity-1u)]=p;}
        free(hash->buckets);
        hash->buckets=table;
        hash->capacity=capacity;
    }
    fresh->bucket=hash->buckets[code&(hash->capacity-1u)];
    hash->buckets[code&(hash->capacity-1u)]=fresh;
    fresh->previous=hash->last;
    if(hash->last)hash->last->next=fresh;
    else hash->first=fresh;
    hash->last=fresh;
    hash->count++;
    hash->generation++;
    sl_leave(hash);
    return 0;
}
static void sl_hash_entry(const ScHash *hash,const SlHashNode *node,ScHashEntry *out)
{ScHashEntry value={32,1,node->token,node->key,node->key_bytes,node->value,hash->width,hash->generation};
    memcpy(out,&value,sizeof(value));}
ScStatus ScHashGet(const ScHash *hash,const void *key,ScU32 key_bytes,ScHashEntry *out)
{
    ScStatus status=sl_valid(hash,SL_HASH);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    if(key_bytes && !key)return SC_UTIL_E_INVALID;
    SlHashNode *node=sl_hash_find(hash,key,key_bytes,ScHashBytes(key,key_bytes));
    if(!node)return SC_UTIL_E_NOT_FOUND;
    sl_hash_entry(hash,node,out);
    return 0;
}
ScStatus ScHashRemove(ScHash *hash,const void *key,ScU32 key_bytes)
{
    ScStatus status=sl_enter(hash,SL_HASH);
    if(status)return status;
    status=sl_mutation(&hash->base,hash->generation);
    if(!status && key_bytes && !key)status=SC_UTIL_E_INVALID;
    SlHashNode *p=status?0:sl_hash_find(hash,key,key_bytes,ScHashBytes(key,key_bytes));
    if(!status && !p)status=SC_UTIL_E_NOT_FOUND;
    if(status){sl_leave(hash);
        return status;}
    sl_hash_unlink(hash,p);
    hash->generation++;
    hash->base.mutation=1;
    sl_hash_collect(hash,0);
    hash->base.mutation=0;
    sl_leave(hash);
    return 0;
}
ScStatus ScHashIterate(const ScHash *hash,ScIterator *iterator)
{
    ScStatus status=sl_valid(hash,SL_HASH);
    if(!status)status=sl_output(iterator,sizeof(*iterator));
    if(status)return status;
    ScIterator value={24,1,(void *)hash,(ScU32)hash->first,hash->generation,sl_token};
    memcpy(iterator,&value,sizeof(value));
    return 0;
}
ScStatus ScHashNext(ScIterator *iterator,ScHashEntry *out)
{
    ScStatus status=sl_input(iterator,sizeof(*iterator));
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    ScHash *hash=iterator->owner;
    status=sl_valid(hash,SL_HASH);
    if(status)return status;
    if(iterator->generation!=hash->generation)return SC_UTIL_E_STALE;
    if(!iterator->position)return SC_UTIL_END;
    SlHashNode *node=(SlHashNode *)iterator->position;
    sl_hash_entry(hash,node,out);
    iterator->position=(ScU32)node->next;
    return 0;
}
ScStatus ScHashVisit(ScHash *hash,ScHashVisitProc procedure,void *context)
{
    ScStatus status=sl_enter(hash,SL_HASH);
    if(status)return status;
    if(!procedure){sl_leave(hash);
        return SC_UTIL_E_INVALID;}
    status=sl_pin((const void *)procedure);
    if(status){sl_leave(hash);
        return status;}
    ScU32 limit=sl_token;
    hash->walk++;
    for(SlHashNode *p=hash->first;p;){
        SlHashNode *next=p->next;
        if(p->alive && p->token<=limit){
            ScHashEntry entry;
            sl_hash_entry(hash,p,&entry);
            p->depth++;
            status=procedure(&entry,context);
            p->depth--;
        }
        if(status || hash->base.closing)break;
        p=next;
    }
    hash->walk--;
    hash->base.mutation=1;
    sl_hash_collect(hash,0);
    hash->base.mutation=0;
    if(!status && hash->base.closing)status=SC_UTIL_E_CANCELLED;
    sl_leave(hash);
    return status;
}
ScStatus ScHashClear(ScHash *hash)
{
    ScStatus status=sl_enter(hash,SL_HASH);
    if(status)return status;
    status=sl_mutation(&hash->base,hash->generation);
    if(status){sl_leave(hash);
        return status;}
    if(hash->count){while(hash->first)sl_hash_unlink(hash,hash->first);
        hash->generation++;}
    hash->base.mutation=1;
    sl_hash_collect(hash,0);
    hash->base.mutation=0;
    sl_leave(hash);
    return 0;
}
