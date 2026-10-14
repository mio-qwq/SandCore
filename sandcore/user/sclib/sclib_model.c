#include "SCLIBPRIVATE.H"

typedef struct SlModelNode SlModelNode;
struct SlModelNode {SlModelNode *parent,*first,*last,*previous,*next,*all,*all_previous;
    ScU32 id,revision;
    ScHash *properties;};
struct ScModel {SlBase base;
    SlModelNode root,*nodes;
    ScHash *index;
    ScEventHub *events;
    ScU32 count,generation;
    ScStatus notification;};
static void sl_model_value_drop(void *data,void *context){(void)context;
    sl_value_free(data);}
static ScStatus sl_model_properties(ScHash **out)
{
    ScHashDesc desc={32,1,sizeof(ScValue),0,sl_model_value_drop,0,{0,0}};
    ScStatus status=ScHashCreate(&desc,out);
    if(!status)(*out)->trusted_drop=1;
    return status;
}
static SlModelNode *sl_model_find(const ScModel *model,ScU32 id)
{
    if(!id)return (SlModelNode *)&model->root;
    SlHashNode *entry=sl_hash_find(model->index,&id,4,ScHashBytes(&id,4));
    return entry?*(SlModelNode **)entry->value:0;
}
static void sl_model_unlink(SlModelNode *node)
{
    if(node->previous)node->previous->next=node->next;
    else node->parent->first=node->next;
    if(node->next)node->next->previous=node->previous;
    else node->parent->last=node->previous;
    node->parent=node->previous=node->next=0;
}
static void sl_model_link(SlModelNode *node,SlModelNode *parent,SlModelNode *before)
{
    node->parent=parent;
    node->next=before;
    node->previous=before?before->previous:parent->last;
    if(node->previous)node->previous->next=node;
    else parent->first=node;
    if(before)before->previous=node;
    else parent->last=node;
}
static void sl_model_dispose(SlBase *base,int quiet)
{
    ScModel *model=(ScModel *)base;
    (void)quiet;
    while(model->nodes){SlModelNode *node=model->nodes;
        model->nodes=node->all;
        ScHashRelease(node->properties);
        free(node);}
    if(model->root.properties)ScHashRelease(model->root.properties);
    if(model->index)ScHashRelease(model->index);
    if(model->events)ScEventHubRelease(model->events);
    model->root.properties=0;
    model->index=0;
    model->events=0;
    model->count=0;
}
static void sl_model_notify(ScModel *model,ScU32 kind,ScU32 node,ScU32 parent,const char *property)
{
    ScModelChange change={32,1,kind,node,parent,model->generation,property,0};
    ScEvent event={48,1,kind,0,(ScU32)model,0,0,&change,sizeof(change),{0,0,0}};
    ScEventResult result={32,1,0,0,0,{0,0,0}};
    ScStatus status=ScEventEmit(model->events,&event,0,&result);
    /* 状态已经提交。观察失败不能倒带树并伪称候选失败；单独公开
     * notification状态，由调用方决定报错/停止后续动作。 */
    model->notification=result.first_error?result.first_error:status<0?status:0;
}
ScStatus ScModelCreate(ScModel **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScModel *model=sl_object(SL_MODEL,sizeof(*model),sl_model_dispose);
    if(!model)return SC_UTIL_E_MEMORY;
    ScHashDesc desc={32,1,sizeof(SlModelNode *),0,0,0,{0,0}};
    ScStatus status=ScHashCreate(&desc,&model->index);
    if(!status)status=sl_model_properties(&model->root.properties);
    if(!status)status=ScEventHubCreate(&model->events);
    if(status){ScModelRelease(model);
        return status;}
    model->generation=model->root.revision=1;
    *out=model;
    return 0;
}
ScStatus ScModelRetain(ScModel *model){return sl_retain(model,SL_MODEL);}
void ScModelRelease(ScModel *model){sl_release(model,SL_MODEL);}
ScStatus ScModelInsert(ScModel *model,ScU32 parent,ScU32 before,ScU32 *node)
{
    ScStatus status=sl_enter(model,SL_MODEL);
    if(status)return status;
    status=sl_mutation(&model->base,model->generation);
    if(!status && !node)status=SC_UTIL_E_INVALID;
    SlModelNode *owner=status?0:sl_model_find(model,parent),*next=before?sl_model_find(model,before):0;
    if(!status && (!owner || (before && (!next || next->parent!=owner))))status=SC_UTIL_E_STALE;
    if(status){sl_leave(model);
        return status;}
    SlModelNode *fresh=calloc(1,sizeof(*fresh));
    if(!fresh){sl_leave(model);
        return SC_UTIL_E_MEMORY;}
    status=sl_model_properties(&fresh->properties);
    if(!status)status=sl_identifier(&fresh->id);
    if(!status)status=ScHashSet(model->index,&fresh->id,4,&fresh,SC_HASH_INSERT_ONLY);
    if(status){if(fresh->properties)ScHashRelease(fresh->properties);
        free(fresh);
        sl_leave(model);
        return status;}
    fresh->revision=1;
    fresh->all=model->nodes;
    if(model->nodes)model->nodes->all_previous=fresh;
    model->nodes=fresh;
    sl_model_link(fresh,owner,next);
    model->count++;
    model->generation++;
    *node=fresh->id;
    sl_model_notify(model,SC_MODEL_INSERT,fresh->id,parent,0);
    sl_leave(model);
    return 0;
}
ScStatus ScModelRemove(ScModel *model,ScU32 id)
{
    ScStatus status=sl_enter(model,SL_MODEL);
    if(status)return status;
    status=sl_mutation(&model->base,model->generation);
    if(!status && !id)status=SC_UTIL_E_INVALID;
    SlModelNode *root=status?0:sl_model_find(model,id);
    if(!status && !root)status=SC_UTIL_E_STALE;
    if(status){sl_leave(model);
        return status;}
    ScU32 parent=root->parent->id;
    SlModelNode *leaf=root;
    while(leaf->first)leaf=leaf->first;
    /* 删除先预验index可完成整个子树的代数增量。关闭后观察只收到
     * 稳定ID，不拿已释放node指针回调，也不从每个叶重新扫根。 */
    ScU32 removing=0;
    SlModelNode *p=root;
    for(;;){removing++;
        if(p->first){p=p->first;
            continue;}while(p!=root && !p->next)p=p->parent;
        if(p==root)break;
        p=p->next;}
    if(removing>0xFFFFFFFFu-model->index->generation){sl_leave(model);
        return SC_UTIL_E_RANGE;}
    for(;;){
        SlModelNode *up=leaf->parent;
        int last=leaf==root;
        sl_model_unlink(leaf);
        ScHashRemove(model->index,&leaf->id,4);
        if(leaf->all_previous)leaf->all_previous->all=leaf->all;
        else model->nodes=leaf->all;
        if(leaf->all)leaf->all->all_previous=leaf->all_previous;
        ScHashRelease(leaf->properties);
        free(leaf);
        model->count--;
        if(last)break;
        leaf=up;
        while(leaf->first)leaf=leaf->first;
    }
    model->generation++;
    sl_model_notify(model,SC_MODEL_REMOVE,id,parent,0);
    sl_leave(model);
    return 0;
}
ScStatus ScModelMove(ScModel *model,ScU32 id,ScU32 parent,ScU32 before)
{
    ScStatus status=sl_enter(model,SL_MODEL);
    if(status)return status;
    status=sl_mutation(&model->base,model->generation);
    if(!status && !id)status=SC_UTIL_E_INVALID;
    SlModelNode *node=status?0:sl_model_find(model,id),*owner=status?0:sl_model_find(model,parent),*next=before?sl_model_find(model,before):0;
    if(!status && (!node || !owner || (before && (!next || next->parent!=owner))))status=SC_UTIL_E_STALE;
    if(!status && next==node){sl_leave(model);
        return 0;}
    if(!status){for(SlModelNode *p=owner;p;p=p->parent){if(p==node){status=SC_UTIL_E_INVALID;
                break;}}}
    if(!status && node->revision==0xFFFFFFFFu)status=SC_UTIL_E_RANGE;
    if(status){sl_leave(model);
        return status;}
    sl_model_unlink(node);
    sl_model_link(node,owner,next);
    node->revision++;
    model->generation++;
    sl_model_notify(model,SC_MODEL_MOVE,id,parent,0);
    sl_leave(model);
    return 0;
}
static void sl_model_info(const ScModel *model,const SlModelNode *node,ScModelNodeInfo *out)
{
    ScModelNodeInfo value={32,1,node->id,node->parent?node->parent->id:0,node->first?node->first->id:0,node->next?node->next->id:0,model->generation,node->properties->count};
    memcpy(out,&value,sizeof(value));
}
ScStatus ScModelNodeQuery(const ScModel *model,ScU32 id,ScModelNodeInfo *out)
{
    ScStatus status=sl_valid(model,SL_MODEL);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    SlModelNode *node=sl_model_find(model,id);
    if(!node)return SC_UTIL_E_STALE;
    sl_model_info(model,node,out);
    return 0;
}
static ScStatus sl_model_key(const char *property,ScU32 *bytes)
{ScStatus status=sl_string_bytes(property,bytes);
    if(status)return status;
    if(!*bytes)return SC_UTIL_E_INVALID;
    return ScUtf8Validate(property,*bytes,0);}
static ScStatus sl_model_property(ScModel *model,ScU32 id,const char *property,const ScValue *input,int remove)
{
    ScStatus status=sl_enter(model,SL_MODEL);
    if(status)return status;
    status=sl_mutation(&model->base,model->generation);
    SlModelNode *node=status?0:sl_model_find(model,id);
    if(!status && !node)status=SC_UTIL_E_STALE;
    if(!status && node->revision==0xFFFFFFFFu)status=SC_UTIL_E_RANGE;
    ScU32 bytes=0;
    if(!status)status=sl_model_key(property,&bytes);
    if(status){sl_leave(model);
        return status;}
    char *name=malloc(bytes+1u);
    if(!name){sl_leave(model);
        return SC_UTIL_E_MEMORY;}memcpy(name,property,bytes+1u);
    ScValue value;
    memset(&value,0,sizeof(value));
    if(!remove)status=sl_value_clone(input,&value);
    if(!status)status=remove?ScHashRemove(node->properties,name,bytes):ScHashSet(node->properties,name,bytes,&value,0);
    if(status){sl_value_free(&value);
        free(name);
        sl_leave(model);
        return status;}
    node->revision++;
    model->generation++;
    ScU32 parent=node->parent?node->parent->id:0;
    sl_model_notify(model,SC_MODEL_VALUE,id,parent,name);
    free(name);
    sl_leave(model);
    return 0;
}
ScStatus ScModelSet(ScModel *model,ScU32 node,const char *property,const ScValue *value){return sl_model_property(model,node,property,value,0);}
ScStatus ScModelUnset(ScModel *model,ScU32 node,const char *property){return sl_model_property(model,node,property,0,1);}
ScStatus ScModelGet(const ScModel *model,ScU32 id,const char *property,ScValue *out)
{
    ScStatus status=sl_valid(model,SL_MODEL);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    SlModelNode *node=sl_model_find(model,id);
    if(!node)return SC_UTIL_E_STALE;
    ScU32 bytes;
    status=sl_model_key(property,&bytes);
    if(status)return status;
    SlHashNode *entry=sl_hash_find(node->properties,property,bytes,ScHashBytes(property,bytes));
    if(!entry)return SC_UTIL_E_NOT_FOUND;
    memcpy(out,entry->value,sizeof(*out));
    return 0;
}
ScStatus ScModelVisit(ScModel *model,ScU32 root_id,ScModelVisitProc procedure,void *context)
{
    ScStatus status=sl_enter(model,SL_MODEL);
    if(status)return status;
    if(!procedure){sl_leave(model);
        return SC_UTIL_E_INVALID;}
    SlModelNode *root=sl_model_find(model,root_id);
    if(!root){sl_leave(model);
        return SC_UTIL_E_STALE;}
    ScArrayDesc desc={32,1,4,0,0,0,{0,0}};
    ScArray *snapshot;
    status=ScArrayCreate(&desc,&snapshot);
    if(status){sl_leave(model);
        return status;}
    SlModelNode *p=root;
    for(;;){
        status=ScArrayInsert(snapshot,snapshot->count,&p->id,1);
        if(status)break;
        if(p->first){p=p->first;
            continue;}
        while(p!=root && !p->next)p=p->parent;
        if(p==root)break;
        p=p->next;
    }
    if(!status)status=sl_pin((const void *)procedure);
    for(ScU32 i=0;i<snapshot->count && !status;i++){
        ScU32 id=((ScU32 *)snapshot->data)[i];
        SlModelNode *node=sl_model_find(model,id);
        if(!node)continue;
        ScModelNodeInfo info;
        sl_model_info(model,node,&info);
        status=procedure(id,&info,context);
        if(!status && model->base.closing)status=SC_UTIL_E_CANCELLED;
    }
    ScArrayRelease(snapshot);
    sl_leave(model);
    return status;
}
ScStatus ScModelConnect(ScModel *model,const ScSubscriptionDesc *desc,ScU32 *token)
{ScStatus status=sl_valid(model,SL_MODEL);
    return status?status:ScEventConnect(model->events,desc,token);}
ScStatus ScModelDisconnect(ScModel *model,ScU32 token)
{ScStatus status=sl_valid(model,SL_MODEL);
    return status?status:ScEventDisconnect(model->events,token);}
ScStatus ScModelQuery(const ScModel *model,ScContainerInfo *out)
{
    ScStatus status=sl_valid(model,SL_MODEL);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,model->count,model->index->capacity/2u,sizeof(ScValue),model->generation,model->notification?1u:0u);
    return 0;
}
ScStatus ScModelNotificationStatus(const ScModel *model,ScStatus *out)
{ScStatus status=sl_valid(model,SL_MODEL);
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    *out=model->notification;
    return 0;}
