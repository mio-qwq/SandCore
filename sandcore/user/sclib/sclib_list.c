#include "SCLIBPRIVATE.H"

static SlListNode *sl_list_find(const ScList *list,ScU32 token)
{
    if(!list->capacity)return 0;
    ScU32 code=ScHashBytes(&token,4);
    for(SlListNode *p=list->buckets[code&(list->capacity-1u)];p;p=p->bucket){if(p->token==token)return p;}
    return 0;
}
static void sl_list_collect(ScList *list,int quiet)
{
    if(list->walk)return;
    while(list->retired){
        SlListNode *p=list->retired;
        list->retired=p->retired;
        if(!quiet && list->drop)list->drop(p->data,list->context);
        free(p);
    }
}
static void sl_list_unlink(ScList *list,SlListNode *p)
{
    ScU32 code=ScHashBytes(&p->token,4);
    SlListNode **link=list->buckets+(code&(list->capacity-1u));
    while(*link!=p)link=&(*link)->bucket;
    *link=p->bucket;
    if(p->previous)p->previous->next=p->next;
    else list->first=p->next;
    if(p->next)p->next->previous=p->previous;
    else list->last=p->previous;
    p->alive=0;
    p->retired=list->retired;
    list->retired=p;
    list->count--;
}
static void sl_list_dispose(SlBase *base,int quiet)
{
    ScList *list=(ScList *)base;
    while(list->first)sl_list_unlink(list,list->first);
    list->walk=0;
    sl_list_collect(list,quiet);
    free(list->buckets);
    list->buckets=0;
    list->capacity=0;
}
ScStatus ScListCreate(const ScListDesc *desc,ScList **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(!desc->item_bytes || desc->item_bytes>0xFFFFFFFFu-sizeof(SlListNode) || desc->reserved[0] || desc->reserved[1] || desc->reserved[2])return SC_UTIL_E_RANGE;
    ScList *list=sl_object(SL_LIST,sizeof(*list),sl_list_dispose);
    if(!list)return SC_UTIL_E_MEMORY;
    status=sl_pin((const void *)desc->drop);
    if(status){ScListRelease(list);
        return status;}
    list->width=desc->item_bytes;
    list->generation=1;
    list->drop=desc->drop;
    list->context=desc->context;
    *out=list;
    return 0;
}
ScStatus ScListRetain(ScList *list){return sl_retain(list,SL_LIST);}
void ScListRelease(ScList *list){sl_release(list,SL_LIST);}
ScStatus ScListQuery(const ScList *list,ScContainerInfo *out)
{
    ScStatus status=sl_valid(list,SL_LIST);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,list->count,list->capacity/2u,list->width,list->generation,0);
    return 0;
}
ScStatus ScListInsert(ScList *list,ScU32 before,const void *item,ScU32 *token)
{
    ScStatus status=sl_valid(list,SL_LIST);
    if(status)return status;
    status=sl_mutation(&list->base,list->generation);
    if(status)return status;
    if(!item || !token)return SC_UTIL_E_INVALID;
    SlListNode *next=before?sl_list_find(list,before):0;
    if(before && !next)return SC_UTIL_E_STALE;
    if(list->count==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    ScU32 capacity=sl_hash_capacity(list->capacity,list->count+1u);
    if(!capacity)return SC_UTIL_E_RANGE;
    SlListNode **table=capacity==list->capacity?0:calloc(capacity,sizeof(*table));
    if(capacity!=list->capacity && !table)return SC_UTIL_E_MEMORY;
    SlListNode *fresh=malloc(sizeof(*fresh)-1u+list->width);
    if(!fresh){free(table);
        return SC_UTIL_E_MEMORY;}
    status=sl_identifier(&fresh->token);
    if(status){free(table);
        free(fresh);
        return status;}
    memcpy(fresh->data,item,list->width);
    fresh->alive=1;
    fresh->retired=0;
    if(table){
        for(SlListNode *p=list->first;p;p=p->next){ScU32 code=ScHashBytes(&p->token,4);
            p->bucket=table[code&(capacity-1u)];
            table[code&(capacity-1u)]=p;}
        free(list->buckets);
        list->buckets=table;
        list->capacity=capacity;
    }
    ScU32 code=ScHashBytes(&fresh->token,4);
    fresh->bucket=list->buckets[code&(list->capacity-1u)];
    list->buckets[code&(list->capacity-1u)]=fresh;
    fresh->next=next;
    fresh->previous=next?next->previous:list->last;
    if(fresh->previous)fresh->previous->next=fresh;
    else list->first=fresh;
    if(next)next->previous=fresh;
    else list->last=fresh;
    list->count++;
    list->generation++;
    *token=fresh->token;
    return 0;
}
ScStatus ScListRemove(ScList *list,ScU32 token)
{
    ScStatus status=sl_enter(list,SL_LIST);
    if(status)return status;
    status=sl_mutation(&list->base,list->generation);
    SlListNode *p=0;
    if(!status){p=sl_list_find(list,token);
        if(!p)status=SC_UTIL_E_STALE;}
    if(status){sl_leave(list);
        return status;}
    sl_list_unlink(list,p);
    list->generation++;
    list->base.mutation=1;
    sl_list_collect(list,0);
    list->base.mutation=0;
    sl_leave(list);
    return 0;
}
static void sl_list_entry(const ScList *list,const SlListNode *node,ScListEntry *out)
{ScListEntry value={24,1,node->token,node->data,list->width,list->generation};
    memcpy(out,&value,sizeof(value));}
ScStatus ScListGet(const ScList *list,ScU32 token,ScListEntry *out)
{
    ScStatus status=sl_valid(list,SL_LIST);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    SlListNode *p=sl_list_find(list,token);
    if(!p)return SC_UTIL_E_STALE;
    sl_list_entry(list,p,out);
    return 0;
}
ScStatus ScListIterate(const ScList *list,ScIterator *iterator)
{
    ScStatus status=sl_valid(list,SL_LIST);
    if(!status)status=sl_output(iterator,sizeof(*iterator));
    if(status)return status;
    ScIterator value={24,1,(void *)list,(ScU32)list->first,list->generation,sl_token};
    memcpy(iterator,&value,sizeof(value));
    return 0;
}
ScStatus ScListNext(ScIterator *iterator,ScListEntry *out)
{
    ScStatus status=sl_input(iterator,sizeof(*iterator));
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    ScList *list=iterator->owner;
    status=sl_valid(list,SL_LIST);
    if(status)return status;
    if(iterator->generation!=list->generation)return SC_UTIL_E_STALE;
    if(!iterator->position)return SC_UTIL_END;
    SlListNode *p=(SlListNode *)iterator->position;
    sl_list_entry(list,p,out);
    iterator->position=(ScU32)p->next;
    return 0;
}
ScStatus ScListVisit(ScList *list,ScListVisitProc procedure,void *context)
{
    ScStatus status=sl_enter(list,SL_LIST);
    if(status)return status;
    if(!procedure){sl_leave(list);
        return SC_UTIL_E_INVALID;}
    status=sl_pin((const void *)procedure);
    if(status){sl_leave(list);
        return status;}
    ScU32 limit=sl_token;
    list->walk++;
    /* next在回调之前保存。删当前/下一项都先退役，原next保持直到最
     * 外层visit结束；本次不追逐新插项，嵌套visit有自己的入口水位。 */
    for(SlListNode *p=list->first;p;){
        SlListNode *next=p->next;
        if(p->alive && p->token<=limit){ScListEntry entry;
            sl_list_entry(list,p,&entry);
            status=procedure(&entry,context);}
        if(status || list->base.closing)break;
        p=next;
    }
    list->walk--;
    list->base.mutation=1;
    sl_list_collect(list,0);
    list->base.mutation=0;
    if(!status && list->base.closing)status=SC_UTIL_E_CANCELLED;
    sl_leave(list);
    return status;
}
ScStatus ScListClear(ScList *list)
{
    ScStatus status=sl_enter(list,SL_LIST);
    if(status)return status;
    status=sl_mutation(&list->base,list->generation);
    if(status){sl_leave(list);
        return status;}
    if(list->count){while(list->first)sl_list_unlink(list,list->first);
        list->generation++;}
    list->base.mutation=1;
    sl_list_collect(list,0);
    list->base.mutation=0;
    sl_leave(list);
    return 0;
}
