#include "SCUIPRIVATE.H"
#include "../SC32PALETTE.H"

/* 模型适配只拥有UI的订阅和临时借用；Model本体仍由SCLIB管理。 */
typedef struct {
    ScUiProvider *provider;
    ScResourceCache *icons;
    ScU32 icon_lease;
    ScUiContext *icon_context;
    ScU32 sort_field,descending,sort_parent;
    char fallback[32];
    ScHash *children;
}
UiModelAdapter;
typedef struct {ScU32 parent,revision,count;
    ScArray *anchors;
}
UiModelChildren;
typedef struct {ScUiContext *context;
    ScUiNode node;
    ScU32 generation;
}
UiDataTarget;
typedef struct {ScU32 id,ordinal;
    UiValue key;
}
UiSortItem;
static ScU32 ui_sort_descending,ui_sort_kind;

static ScU32 ui_data_u32(const unsigned char *p)
{return (ScU32)p[0]|((ScU32)p[1]<<8)|((ScU32)p[2]<<16)|((ScU32)p[3]<<24);
}
static int ui_data_reserved(const ScU32 *p,ScU32 count)
{for(ScU32 i=0;i<count;i++)if(p[i])return 0;
    return 1;
}
static ScStatus ui_data_status(ScStatus s)
{return s<0?ui_cause(0,s,UI_OP_DATA,0):s;
}
static ScStatus ui_model_icon_load(const ScView *key,const ScCacheStamp *stamp,void *context,void **resource,ScU32 *cost)
{
    UiModelAdapter *adapter=(UiModelAdapter *)context;
    char path[64];
    ScUiIcon *icon=0;
    ScStatus s;
    (void)stamp;
    if(key->bytes==0 || key->bytes>=sizeof(path) || memchr(key->data,0,key->bytes))return SC_UI_E_INVALID;
    memcpy(path,key->data,key->bytes);
    path[key->bytes]=0;
    s=ScUiIconLoad(adapter->icon_context,path,&icon);
    if(s<0)return s;
    *resource=icon;
    *cost=icon->bytes+sizeof(*icon);
    return 0;
}
static void ui_model_icon_drop(void *resource,void *context)
{(void)context;
    ScUiIconRelease((ScUiIcon *)resource);
}

static void ui_icon_dispose(UiObject *object,int quiet)
{ScUiIcon *icon=(ScUiIcon *)object;
    (void)quiet;
    free(icon->pixels);
    icon->pixels=0;
    icon->bytes=0;
}
ScStatus ScUiIconCreateArgb(ScU32 width,ScU32 height,ScU32 stride,const ScU32 *pixels,ScUiIcon **out)
{
    ScU32 row,bytes,last;
    ScUiIcon *icon;
    void *candidate=0;
    ScStatus s;
    if(!out || !pixels || !width || !height)return SC_UI_E_INVALID;
    if(!ui_multiply(width,4,&row) || stride<row || !ui_multiply(row,height,&bytes)
        || !ui_multiply(stride,height-1u,&last) || !ui_add(last,row,&last))return SC_UI_E_RANGE;
    s=ui_object_new(sizeof(*icon),UI_ICON,ui_icon_dispose,&candidate);
    if(s<0)return s;
    icon=(ScUiIcon *)candidate;
    icon->pixels=(ScU32 *)malloc(bytes);
    if(!icon->pixels){ui_object_release(&icon->object);
        return SC_UI_E_MEMORY;
    }
    for(ScU32 y=0;y<height;y++)memcpy((unsigned char *)icon->pixels+y*row,(const unsigned char *)pixels+y*stride,row);
    icon->width=width;
    icon->height=height;
    icon->format=2;
    icon->bytes=bytes;
    icon->revision=1;
    *out=icon;
    return SC_UI_OK;
}
ScStatus ScUiIconRetain(ScUiIcon *icon){return ui_object_retain((UiObject *)icon,UI_ICON);
}
void ScUiIconRelease(ScUiIcon *icon){ui_object_release((UiObject *)icon);
}
ScStatus ScUiIconQuery(const ScUiIcon *icon,ScUiIconInfo *out)
{
    ScUiIconInfo value;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!icon || icon->object.kind!=UI_ICON || icon->object.closing)return SC_UI_E_STALE;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.width=icon->width;
    value.height=icon->height;
    value.format=icon->format;
    value.flags=icon->flags;
    value.revision=icon->revision;
    value.bytes=icon->bytes;
    memcpy(out,&value,sizeof(value));
    return SC_UI_OK;
}
ScStatus ScUiIconLoad(ScUiContext *context,const char *path,ScUiIcon **out)
{
    ScFileOpenDesc open;
    ScFile file=0;
    ScFileInfo info;
    ScIoResult io;
    ScStatus s;
    unsigned char *body=0;
    ScU32 *pixels=0,width=0,height=0,offset=0,format=0,count,bytes,palette[256];
    if(!path || !out || !context || context->object.kind!=UI_CONTEXT || context->object.closing)return SC_UI_E_INVALID;
    memset(&open,0,sizeof(open));
    open.size=sizeof(open);
    open.version=1;
    open.path=path;
    open.flags=SC32_FILE_READ;
    s=ScFileOpen(&open,&file);
    if(s<0)return ui_cause(context,s,UI_OP_DATA,0);
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScFileQuery(file,&info);
    /* 图标沿既有资源合同最大128方；不是文档、窗口或控件数量限制。 */
    if(s==0 && (info.length<24 || info.length>65568u))s=SC_UI_E_RANGE;
    if(s==0){body=(unsigned char *)malloc(info.length);
        if(!body)s=SC_UI_E_MEMORY;
    }
    for(ScU32 done=0;s==0 && done<info.length;){
        ScU32 take=info.length-done;
        if(take>65536u)take=65536u;
        memset(&io,0,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScFileRead(file,body+done,take,&io);
        if(s==0 && (!io.transferred || io.transferred>take))s=SC_UI_E_IO;
        if(s==0)done+=io.transferred;
    }
    {ScStatus closed=ScFileClose(file);
        if(s==0)s=closed;
    }
    if(s<0){free(body);
        return ui_cause(context,s,UI_OP_DATA,0);
    }
    if(info.length==288u && memcmp(body,"SCF1MIO\0",8)==0 && ui_data_u32(body+8)==1){
        ScU32 label=0;
        while(label<4 && body[12+label])label++;
        if(!label || label==4 || ScUtf8Validate(body+12,label,0)<0)s=SC_UI_E_INVALID;
        for(ScU32 i=label;i<4;i++)if(body[12+i])s=SC_UI_E_INVALID;
        for(ScU32 y=0;y<16;y++){
            for(ScU32 x=0;x<16;x++)if(body[16+y*17+x]!='#' && body[16+y*17+x]!='.')s=SC_UI_E_INVALID;
            if(body[16+y*17+16])s=SC_UI_E_INVALID;
        }
        width=height=16;
        format=3;
    }
    else{
        width=ui_data_u32(body+8);
        height=ui_data_u32(body+12);
        format=ui_data_u32(body+16);
        if(!width || !height || width>128 || height>128)s=SC_UI_E_RANGE;
        else if(memcmp(body,"SCB1MIO\0",8)==0 && format==1 && ui_data_u32(body+20)==0x004f494du){
            offset=24;
            if(info.length!=offset+width*height)s=SC_UI_E_INVALID;
        }
        else if(info.length>=32 && memcmp(body,"SCB2MIO\0",8)==0 && format==2 && !ui_data_u32(body+20)
            && ui_data_u32(body+24)==width*height*4u && ui_data_u32(body+28)==0x004f494du){
            offset=32;
            if(info.length!=offset+width*height*4u)s=SC_UI_E_INVALID;
        }
        else s=SC_UI_E_INVALID;
    }
    if(s==0 && format==1)s=ScDisplayPalette(palette,256);
    count=bytes=0;
    if(s==0 && (!ui_multiply(width,height,&count) || !ui_multiply(count,4,&bytes)))s=SC_UI_E_RANGE;
    if(s==0){pixels=(ScU32 *)malloc(bytes);
        if(!pixels)s=SC_UI_E_MEMORY;
    }
    if(s==0){
        for(ScU32 i=0;i<count;i++){
            if(format==2)pixels[i]=ui_data_u32(body+offset+i*4u);
            else if(format==1){ScU32 slot=body[offset+i];
                pixels[i]=slot?0xff000000u|palette[slot]:0;
            }
            else pixels[i]=body[16+(i/16u)*17u+i%16u]=='#'?0xff000000u|ui_color(context,SC32_COLOR_ACCENT):0;
        }
        s=ScUiIconCreateArgb(width,height,width*4u,pixels,out);
    }
    free(pixels);
    free(body);
    return s<0?ui_cause(context,s,UI_OP_DATA,0):s;
}

static void ui_model_children_drop(void *item,void *context)
{(void)context;
    UiModelChildren *p=*(UiModelChildren **)item;
    if(p){ScArrayRelease(p->anchors);
        free(p);
    }
}
static ScStatus ui_provider_model_children(ScUiProvider *provider,ScU32 parent,UiModelChildren **out)
{
    UiModelAdapter *adapter=(UiModelAdapter *)provider->description.context;
    ScHashEntry entry;
    ScModelNodeInfo node;
    ScStatus s;
    memset(&entry,0,sizeof(entry));
    entry.size=sizeof(entry);
    entry.version=1;
    if(ScHashGet(adapter->children,&parent,4,&entry)==0){UiModelChildren *cached=*(UiModelChildren *const *)entry.value;
        if(cached->revision==provider->revision){*out=cached;
            return 0;
        }
    }
    UiModelChildren *candidate=(UiModelChildren *)calloc(1,sizeof(*candidate));
    if(!candidate)return SC_UI_E_MEMORY;
    candidate->parent=parent;
    candidate->revision=provider->revision;
    s=ui_array_new(4,&candidate->anchors);
    memset(&node,0,sizeof(node));
    node.size=sizeof(node);
    node.version=1;
    if(s==0)s=ScModelNodeQuery(provider->model,parent,&node);
    ScU32 id=node.first_child;
    while(s==0 && id){
        if((candidate->count&63u)==0)s=ScArrayInsert(candidate->anchors,candidate->count/64,&id,1);
        if(s==0)s=ScModelNodeQuery(provider->model,id,&node);
        if(s<0)break;
        if(candidate->count==UINT_MAX){s=SC_UI_E_RANGE;
            break;
        }
        candidate->count++;
        id=node.next_sibling;
        if((candidate->count&4095u)==0)ScProcessYield();
    }
    if(s==0 && candidate->revision!=provider->revision)s=SC_UI_E_STALE;
    if(s==0)s=ScHashSet(adapter->children,&parent,4,&candidate,0);
    if(s<0){ui_model_children_drop(&candidate,0);
        return ui_data_status(s);
    }
    *out=candidate;
    return 0;
}
static ScStatus ui_provider_model_child(ScUiProvider *provider,ScU32 parent,ScU32 index,ScModelNodeInfo *out)
{
    UiModelChildren *children;
    ScStatus s=ui_provider_model_children(provider,parent,&children);
    ScU32 id=0;
    if(s<0)return s;
    if(index>=children->count)return SC_UI_END;
    s=ScArrayGet(children->anchors,index/64,&id);
    memset(out,0,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    for(ScU32 step=0;s==0 && step<=index%64;step++){s=ScModelNodeQuery(provider->model,id,out);
        id=out->next_sibling;
    }
    return ui_data_status(s);
}
static ScStatus ui_provider_model_count(ScUiProvider *provider,ScU32 parent,ScU32 *count)
{
    UiModelChildren *children;
    ScStatus s=ui_provider_model_children(provider,parent,&children);
    if(s==0)*count=children->count;
    return s;
}
static void ui_provider_property(ScU32 field,char name[32])
{if(!field)strcpy(name,"label");
    else snprintf(name,32,"field%u",field);
}
static int ui_provider_key_compare(const void *left,const void *right)
{
    const UiSortItem *a=(const UiSortItem *)left,*b=(const UiSortItem *)right;
    const ScValue *x=&a->key.value,*y=&b->key.value;
    int answer=0;
    if(ui_sort_kind==SC_UI_SORT_SIGNED){ScI32 u=(ScI32)x->lo,v=(ScI32)y->lo;
        answer=(u>v)-(u<v);
    }
    else if(ui_sort_kind==SC_UI_SORT_UNSIGNED)answer=(x->lo>y->lo)-(x->lo<y->lo);
    else if(ui_sort_kind==SC_UI_SORT_COUNTER)answer=x->hi==y->hi?(x->lo>y->lo)-(x->lo<y->lo):(x->hi>y->hi?1:-1);
    else if(x->type==y->type){
        if(x->type==SC_VALUE_I32){ScI32 u=(ScI32)x->lo,v=(ScI32)y->lo;
            answer=(u>v)-(u<v);
        }
        else if(x->type==SC_VALUE_COUNTER){answer=x->hi==y->hi?(x->lo>y->lo)-(x->lo<y->lo):(x->hi>y->hi?1:-1);
        }
        else if(x->type==SC_VALUE_UTF8 || x->type==SC_VALUE_BYTES){
            ScU32 n=x->bytes<y->bytes?x->bytes:y->bytes;
            answer=memcmp(x->data,y->data,n);
            if(!answer)answer=(x->bytes>y->bytes)-(x->bytes<y->bytes);
        }
        else answer=(x->lo>y->lo)-(x->lo<y->lo);
    }
    else answer=(x->type>y->type)-(x->type<y->type);
    if(answer && ui_sort_descending)answer=answer>0?-1:1;
    return answer?answer:(a->ordinal>b->ordinal)-(a->ordinal<b->ordinal);
}
static ScStatus ui_provider_model_sort(ScUiProvider *provider,const ScUiDataRequest *request)
{
    UiModelAdapter *adapter=(UiModelAdapter *)provider->description.context;
    ScU32 count=0,bytes=0,revision=provider->revision;
    UiSortItem *items=0;
    ScArray *order=0;
    ScStatus s;
    if(revision==UINT_MAX)return SC_UI_E_RANGE;
    s=ui_provider_model_count(provider,request->parent,&count);
    if(s<0)return s;
    if(!ui_multiply(count,sizeof(*items),&bytes))return SC_UI_E_RANGE;
    if(bytes){items=(UiSortItem *)calloc(1,bytes);
        if(!items)return SC_UI_E_MEMORY;
    }
    char property[32];
    ui_provider_property(request->field,property);
    ScModelNodeInfo root;
    memset(&root,0,sizeof(root));
    root.size=sizeof(root);
    root.version=1;
    s=ScModelNodeQuery(provider->model,request->parent,&root);
    ScU32 id=root.first_child;
    for(ScU32 i=0;s==0 && i<count;i++){
        ScValue value;
        ScModelNodeInfo node;
        memset(&value,0,sizeof(value));
        value.size=sizeof(value);
        value.version=1;
        memset(&node,0,sizeof(node));
        node.size=sizeof(node);
        node.version=1;
        if(!id){s=SC_UI_E_STALE;
            break;
        }
        items[i].id=id;
        items[i].ordinal=i;
        s=ScModelGet(provider->model,id,property,&value);
        if(s==SC_UTIL_E_NOT_FOUND){memset(&value,0,sizeof(value));
            value.size=sizeof(value);
            value.version=1;
            s=0;
        }
        if(s==0)s=ui_value_copy(&value,&items[i].key);
        if(s==0)s=ScModelNodeQuery(provider->model,id,&node);
        id=node.next_sibling;
        if((i&63u)==63u)ScProcessYield();
    }
    if(s==0){ui_sort_descending=request->flags&1u;
        ui_sort_kind=request->flags>>8;
        if(ui_sort_kind)for(ScU32 i=0;i<count;i++)if(items[i].key.value.type==SC_VALUE_UTF8 || items[i].key.value.type==SC_VALUE_BYTES){s=SC_UI_E_INVALID;
            break;
        }
        if(s==0)qsort(items,count,sizeof(*items),ui_provider_key_compare);
    }
    if(s==0)s=ui_array_new(sizeof(ScU32),&order);
    if(s==0)s=ScArrayReserve(order,count);
    for(ScU32 i=0;s==0 && i<count;i++)s=ScArrayInsert(order,i,&items[i].id,1);
    if(s==0 && (provider->revision!=revision || provider->object.closing))s=SC_UI_E_STALE;
    if(s==0){ScArray *old=provider->order;
        provider->order=order;
        order=0;
        ScArrayRelease(old);
        adapter->sort_field=request->field;
        adapter->sort_parent=request->parent;
        adapter->descending=request->flags&1u;
        ScUiDataChange change;
        memset(&change,0,sizeof(change));
        change.size=sizeof(change);
        change.version=1;
        change.kind=SC_UI_DATA_SORTED;
        if(provider->revision==UINT_MAX)s=SC_UI_E_RANGE;
        else{change.revision=provider->revision+1;
            s=ScUiProviderNotify(provider,&change);
        }
    }
    for(ScU32 i=0;i<count;i++)ui_value_clear(&items[i].key);
    free(items);
    ScArrayRelease(order);
    return ui_data_status(s);
}
static ScStatus ui_model_data(void *context,const ScUiDataRequest *request,ScUiDataReply *reply)
{
    UiModelAdapter *adapter=(UiModelAdapter *)context;
    ScUiProvider *provider=adapter->provider;
    ScModelNodeInfo node;
    ScStatus s=0;
    ScValue value;
    char property[32];
    memset(&node,0,sizeof(node));
    node.size=sizeof(node);
    node.version=1;
    if(adapter->icon_lease){ScCacheReleaseLease(adapter->icons,adapter->icon_lease);
        adapter->icon_lease=0;
    }
    reply->revision=provider->revision;
    if(request->operation==SC_UI_DATA_COUNT || request->operation==SC_UI_DATA_CHILD_COUNT)
    return ui_provider_model_count(provider,request->parent,&reply->total);
    if(request->operation==SC_UI_DATA_SORT){s=ui_provider_model_sort(provider,request);
        reply->revision=provider->revision;
        return s;
    }
    if(request->operation==SC_UI_DATA_FIND_PREFIX){
        if(reply->value.type!=SC_VALUE_UTF8 || request->flags&~1u)return SC_UI_E_INVALID;
        ScU32 count=0;
        s=ui_provider_model_count(provider,request->parent,&count);
        if(s<0)return s;
        for(ScU32 step=0;step<count;step++){
            ScU32 index=request->index>=count?step:(request->index+step)%count,id;
            if(provider->order && request->parent==adapter->sort_parent)s=ScArrayGet(provider->order,index,&id);
            else{s=ui_provider_model_child(provider,request->parent,index,&node);
                id=node.id;
            }
            if(s<0)return ui_data_status(s);
            ScValue label;
            memset(&label,0,sizeof(label));
            label.size=sizeof(label);
            label.version=1;
            s=ScModelGet(provider->model,id,"label",&label);
            int equal=s==0 && label.type==SC_VALUE_UTF8 && label.bytes>=reply->value.bytes;
            for(ScU32 i=0;equal && i<reply->value.bytes;i++){unsigned char a=((const unsigned char *)label.data)[i],b=((const unsigned char *)reply->value.data)[i];
                if(request->flags&1u){if(a>='A' && a<='Z')a+=(unsigned char)('a'-'A');
                    if(b>='A' && b<='Z')b+=(unsigned char)('a'-'A');
                }
                if(a!=b)equal=0;
            }
            if(equal){reply->item=id;
                reply->parent=request->parent;
                reply->total=index;
                reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
                return 0;
            }
            if((step&255u)==255u)ScProcessYield();
        }
        return SC_UI_E_NOT_FOUND;
    }
    if(request->operation==SC_UI_DATA_CHILD_AT){
        ScU32 id=0;
        if(provider->order && request->parent==adapter->sort_parent){
            s=ScArrayGet(provider->order,request->index,&id);
            if(s<0)return SC_UI_END;
            s=ScModelNodeQuery(provider->model,id,&node);
        }
        else s=ui_provider_model_child(provider,request->parent,request->index,&node);
    }
    else s=ScModelNodeQuery(provider->model,request->item,&node);
    if(s!=0)return ui_data_status(s);
    reply->item=node.id;
    reply->parent=node.parent;
    reply->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    if(ScModelGet(provider->model,node.id,"enabled",&value)==0 && value.type==SC_VALUE_BOOL && !value.lo)reply->state&=~SC_UI_ENABLED;
    if(request->operation==SC_UI_DATA_INDEX_OF){
        if(request->parent && request->parent!=node.parent)return SC_UI_E_STALE;
        ScU32 index=0;
        ScModelNodeInfo child;
        for(;;index++){
            if(provider->order && node.parent==adapter->sort_parent){ScU32 id=0;
                s=ScArrayGet(provider->order,index,&id);
                if(s<0)return SC_UI_E_STALE;
                if(id==node.id)break;
            }
            else{s=ui_provider_model_child(provider,node.parent,index,&child);
                if(s!=0)return SC_UI_E_STALE;
                if(child.id==node.id)break;
            }
        }
        reply->total=index;
        return SC_UI_OK;
    }
    ui_provider_property(request->operation==SC_UI_DATA_CELL || request->operation==SC_UI_DATA_COMMIT_EDIT?request->field:0,property);
    if(request->operation==SC_UI_DATA_COMMIT_EDIT){
        s=ScModelSet(provider->model,node.id,property,&reply->value);
        if(s<0)return ui_data_status(s);
        reply->revision=provider->revision;
        reply->value.size=sizeof(reply->value);
        reply->value.version=1;
        return ui_data_status(ScModelGet(provider->model,node.id,property,&reply->value));
    }
    reply->value.size=sizeof(reply->value);
    reply->value.version=1;
    s=ScModelGet(provider->model,node.id,property,&reply->value);
    if(s==SC_UTIL_E_NOT_FOUND){
        memset(&reply->value,0,sizeof(reply->value));
        reply->value.size=sizeof(reply->value);
        reply->value.version=1;
        if(!request->field){int bytes=snprintf(adapter->fallback,sizeof(adapter->fallback),"Item %u",node.id);
            reply->value.type=SC_VALUE_UTF8;
            reply->value.data=adapter->fallback;
            reply->value.bytes=(ScU32)bytes;
        }
        s=0;
    }
    if(s==0 && provider->bindings && adapter->icons){
        ScValue image;
        memset(&image,0,sizeof(image));
        image.size=sizeof(image);
        image.version=1;
        if(ScModelGet(provider->model,node.id,"icon",&image)==0 && image.type==SC_VALUE_UTF8 && image.bytes){
            ScCacheStamp stamp;
            memset(&stamp,0,sizeof(stamp));
            stamp.size=sizeof(stamp);
            stamp.version=1;
            stamp.namespace_id=2;
            stamp.object_id=node.id;
            stamp.object_generation=node.revision;
            stamp.user_generation=provider->revision;
            adapter->icon_context=provider->bindings->context;
            stamp.theme_epoch=adapter->icon_context->theme.generation;
            stamp.display_epoch=adapter->icon_context->display.generation;
            void *icon=0;
            ScStatus loaded=ScCacheAcquire(adapter->icons,image.data,image.bytes,&stamp,&icon,&adapter->icon_lease);
            if(loaded==0)reply->icon=(ScUiIcon *)icon;
            else if(loaded!=SC32_E_NOT_FOUND && loaded!=SC_UI_E_NOT_FOUND)return ui_data_status(loaded);
        }
    }
    return ui_data_status(s);
}

static void ui_provider_dispose(UiObject *object,int quiet)
{
    ScUiProvider *provider=(ScUiProvider *)object;
    if(provider->model){
        UiModelAdapter *adapter=(UiModelAdapter *)provider->description.context;
        if(provider->model_subscription)ScModelDisconnect(provider->model,provider->model_subscription);
        /* late quiet不得触外部Model的用户drop；SCLIB自己的quiet fini回收它。 */
        if(!quiet)ScModelRelease(provider->model);
        if(adapter){if(adapter->icon_lease)ScCacheReleaseLease(adapter->icons,adapter->icon_lease);
            ScCacheRelease(adapter->icons);
            ScHashRelease(adapter->children);
            free(adapter);
        }
    }
    else if(!quiet && provider->description.context_drop)
    provider->description.context_drop(provider->description.context,0);
    provider->model=0;
    provider->model_subscription=0;
    ScArrayRelease(provider->order);
    provider->order=0;
}
ScStatus ScUiProviderCreate(const ScUiProviderDesc *description,ScUiProvider **out)
{
    ScUiProvider *provider;
    void *candidate=0;
    ScStatus s=ui_input(description,sizeof(*description));
    if(s<0 || !out)return s<0?s:SC_UI_E_INVALID;
    if(!description->procedure || (description->flags&~15u) || !ui_data_reserved(description->reserved,2))return SC_UI_E_INVALID;
    s=ui_object_new(sizeof(*provider),UI_PROVIDER,ui_provider_dispose,&candidate);
    if(s<0)return s;
    provider=(ScUiProvider *)candidate;
    s=ui_pin((const void *)description->procedure);
    if(s==0)s=ui_pin((const void *)description->context_drop);
    if(s<0){ui_object_release(&provider->object);
        return ui_data_status(s);
    }
    provider->description=*description;
    provider->revision=1;
    *out=provider;
    return SC_UI_OK;
}
ScStatus ScUiProviderRetain(ScUiProvider *provider){return ui_object_retain((UiObject *)provider,UI_PROVIDER);
}
void ScUiProviderRelease(ScUiProvider *provider){ui_object_release((UiObject *)provider);
}
static ScStatus ui_model_changed(const ScEvent *event,void *context)
{
    UiModelAdapter *adapter=(UiModelAdapter *)context;
    ScUiProvider *provider=adapter->provider;
    if(ui_quiet || !event || event->bytes<sizeof(ScModelChange))return 0;
    const ScModelChange *source=(const ScModelChange *)event->data;
    ScUiDataChange change;
    memset(&change,0,sizeof(change));
    change.size=sizeof(change);
    change.version=1;
    change.revision=provider->revision==UINT_MAX?UINT_MAX:provider->revision+1;
    change.kind=source->kind==SC_MODEL_MOVE?SC_UI_DATA_MOVE:source->kind==SC_MODEL_VALUE?SC_UI_DATA_UPDATE:source->kind;
    change.parent=source->parent;
    change.item=source->node;
    change.count=1;
    return ScUiProviderNotify(provider,&change);
}
ScStatus ScUiProviderFromModel(ScModel *model,ScUiProvider **out)
{
    ScUiProvider *provider;
    UiModelAdapter *adapter;
    ScUiProviderDesc description;
    ScContainerInfo info;
    ScSubscriptionDesc subscription;
    ScStatus s;
    if(!model || !out)return SC_UI_E_INVALID;
    adapter=(UiModelAdapter *)calloc(1,sizeof(*adapter));
    if(!adapter)return SC_UI_E_MEMORY;
    memset(&description,0,sizeof(description));
    description.size=sizeof(description);
    description.version=1;
    description.flags=SC_UI_PROVIDER_SORT|SC_UI_PROVIDER_EDIT|SC_UI_PROVIDER_TREE;
    description.procedure=ui_model_data;
    description.context=adapter;
    s=ScUiProviderCreate(&description,&provider);
    if(s<0){free(adapter);
        return s;
    }
    s=ScModelRetain(model);
    if(s<0){ScUiProviderRelease(provider);
        free(adapter);
        return ui_data_status(s);
    }
    provider->model=model;
    adapter->provider=provider;
    ScHashDesc hash;
    memset(&hash,0,sizeof(hash));
    hash.size=sizeof(hash);
    hash.version=1;
    hash.value_bytes=sizeof(UiModelChildren *);
    hash.drop=ui_model_children_drop;
    s=ScHashCreate(&hash,&adapter->children);
    ScCacheDesc cache;
    memset(&cache,0,sizeof(cache));
    cache.size=sizeof(cache);
    cache.version=1;
    cache.budget_bytes=524288u;
    cache.load=ui_model_icon_load;
    cache.drop=ui_model_icon_drop;
    cache.context=adapter;
    if(s==0)s=ScCacheCreate(&cache,&adapter->icons);
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    if(s==0)s=ScModelQuery(model,&info);
    if(s==0)provider->revision=info.generation;
    memset(&subscription,0,sizeof(subscription));
    subscription.size=sizeof(subscription);
    subscription.version=1;
    subscription.procedure=ui_model_changed;
    subscription.context=adapter;
    if(s==0)s=ScModelConnect(model,&subscription,&provider->model_subscription);
    if(s<0){ScUiProviderRelease(provider);
        return ui_data_status(s);
    }
    *out=provider;
    return SC_UI_OK;
}

static ScStatus ui_provider_call(ScUiProvider *provider,const ScUiDataRequest *request,ScUiDataReply *out)
{
    ScUiDataReply reply;
    UiValue input;
    ScValue original_input;
    ScStatus s=ui_input(request,sizeof(*request));
    ScU32 revision;
    if(s<0)return s;
    s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(request->operation<SC_UI_DATA_COUNT || request->operation>SC_UI_DATA_FIND_PREFIX
        || !ui_data_reserved(request->reserved,2)
        || request->flags&~(request->operation==SC_UI_DATA_SORT?0x301u:request->operation==SC_UI_DATA_FIND_PREFIX?1u:0u)
        || (request->operation==SC_UI_DATA_SORT && (request->flags>>8)>SC_UI_SORT_COUNTER))return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)provider,UI_PROVIDER);
    if(s<0)return s;
    revision=provider->revision;
    if(request->revision && request->revision!=revision){ui_object_leave(&provider->object);
        return SC_UI_E_STALE;
    }
    memset(&reply,0,sizeof(reply));
    reply.size=sizeof(reply);
    reply.version=1;
    reply.revision=revision;
    reply.value.size=sizeof(reply.value);
    reply.value.version=1;
    memset(&input,0,sizeof(input));
    /* 唯一in/out特例：COMMIT_EDIT先克隆caller的候选，普通query不读out正文。 */
    if(request->operation==SC_UI_DATA_COMMIT_EDIT || request->operation==SC_UI_DATA_FIND_PREFIX){
        if(request->operation==SC_UI_DATA_COMMIT_EDIT && !(provider->description.flags&SC_UI_PROVIDER_EDIT))s=SC_UI_E_NOT_SUPPORTED;
        else{original_input=out->value;
            s=ui_value_copy(&original_input,&input);
            if(s==0)reply.value=input.value;
        }
    }
    if(s==0){provider->querying++;
        s=provider->description.procedure(provider->description.context,request,&reply);
        provider->querying--;
    }
    if(s==0 || s==SC_UI_PENDING){
        if(provider->object.closing || (provider->revision!=revision && request->operation!=SC_UI_DATA_COMMIT_EDIT && request->operation!=SC_UI_DATA_SORT))s=SC_UI_E_STALE;
        else if(reply.size!=sizeof(reply) || reply.version!=1 || !reply.revision || reply.revision!=provider->revision
            || reply.value.size!=sizeof(ScValue) || reply.value.version!=1 || reply.value.flags
            || reply.value.type>SC_VALUE_BYTES)s=SC_UI_E_INVALID;
        else if(reply.value.type==SC_VALUE_UTF8 && ScUtf8Validate(reply.value.data,reply.value.bytes,0)<0)s=SC_UI_E_UTF8;
        else if(reply.value.type!=SC_VALUE_UTF8 && reply.value.type!=SC_VALUE_BYTES && (reply.value.data || reply.value.bytes))s=SC_UI_E_INVALID;
        else if((reply.value.type==SC_VALUE_BYTES && reply.value.bytes && !reply.value.data)
            || (reply.value.type!=SC_VALUE_COUNTER && reply.value.hi)
            || (reply.value.type==SC_VALUE_BOOL && reply.value.lo>1)
            || (reply.value.type==SC_VALUE_NULL && reply.value.lo))s=SC_UI_E_INVALID;
        else{
            /* 返回借用若仍指候选clone，则映回caller仍拥有的原始候选，
             * 不能发布一个马上随ui_value_clear解除的data指针。 */
            if((request->operation==SC_UI_DATA_COMMIT_EDIT || request->operation==SC_UI_DATA_FIND_PREFIX) && input.value.data && reply.value.data){
                ScU32 start=(ScU32)input.value.data,where=(ScU32)reply.value.data;
                if(where>=start && where-start<=input.value.bytes && reply.value.bytes<=input.value.bytes-(where-start))
                reply.value.data=(const unsigned char *)original_input.data+(where-start);
            }
            memcpy(out,&reply,sizeof(reply));
        }
    }
    ui_value_clear(&input);
    ui_object_leave(&provider->object);
    return ui_data_status(s);
}
static ScStatus ui_provider_count(ScUiProvider *provider,ScU32 parent,ScU32 *count,ScU32 *revision)
{
    ScUiDataRequest request;
    ScUiDataReply reply;
    ScStatus s;
    if(!count || !revision)return SC_UI_E_INVALID;
    memset(&request,0,sizeof(request));
    request.size=sizeof(request);
    request.version=1;
    request.operation=parent?SC_UI_DATA_CHILD_COUNT:SC_UI_DATA_COUNT;
    request.parent=parent;
    memset(&reply,0,sizeof(reply));
    reply.size=sizeof(reply);
    reply.version=1;
    s=ui_provider_call(provider,&request,&reply);
    if(s==0){*count=reply.total;
        *revision=reply.revision;
    }
    return s;
}
static ScStatus ui_provider_item(ScUiProvider *provider,ScU32 parent,ScU32 index,ScU32 *item,ScU32 *revision,ScU32 *state)
{
    ScUiDataRequest request;
    ScUiDataReply reply;
    ScStatus s;
    if(!item || !state || !revision)return SC_UI_E_INVALID;
    memset(&request,0,sizeof(request));
    request.size=sizeof(request);
    request.version=1;
    request.operation=SC_UI_DATA_CHILD_AT;
    request.parent=parent;
    request.index=index;
    memset(&reply,0,sizeof(reply));
    reply.size=sizeof(reply);
    reply.version=1;
    s=ui_provider_call(provider,&request,&reply);
    if(s==0){if(!reply.item)return SC_UI_E_INVALID;
        *item=reply.item;
        *state=reply.state;
        *revision=reply.revision;
    }
    return s;
}
static ScStatus ui_provider_cell(ScUiProvider *provider,ScU32 item,ScU32 field,ScValue *out)
{
    ScUiDataRequest request;
    ScUiDataReply reply;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    memset(&request,0,sizeof(request));
    request.size=sizeof(request);
    request.version=1;
    request.operation=SC_UI_DATA_CELL;
    request.item=item;
    request.field=field;
    memset(&reply,0,sizeof(reply));
    reply.size=sizeof(reply);
    reply.version=1;
    s=ui_provider_call(provider,&request,&reply);
    if(s==0)memcpy(out,&reply.value,sizeof(*out));
    return s;
}

static void ui_provider_detach(UiBinding *binding)
{
    if(!binding)return;
    ScUiProvider *provider=binding->provider;
    if(binding->previous)binding->previous->next=binding->next;
    else provider->bindings=binding->next;
    if(binding->next)binding->next->previous=binding->previous;
    free(binding);
}
static void ui_provider_changed(ScUiProvider *provider,const ScUiDataChange *change)
{
    ScU32 count=0,index=0,bytes=0;
    UiBinding *binding;
    UiDataTarget *targets=0;
    for(binding=provider->bindings;binding;binding=binding->next)count++;
    if(!ui_multiply(count,sizeof(*targets),&bytes))return;
    if(bytes)targets=(UiDataTarget *)malloc(bytes);
    if(bytes && !targets){
        /* 已提交model不能谎称回滚。此分支不进用户callback，只标损伤/重试。 */
        for(binding=provider->bindings;binding;binding=binding->next){UiNode *n=ui_node(binding->context,binding->node);
            if(n && n->view){n->view->revision=0;
                binding->context->notification_error=SC_UI_E_MEMORY;
                ui_node_damage(n);
            }
        }
        return;
    }
    for(binding=provider->bindings;binding;binding=binding->next){
        if(ScUiContextRetain(binding->context)<0)continue;
        UiNode *n=ui_node(binding->context,binding->node);
        if(!n){ScUiContextRelease(binding->context);
            continue;
        }
        UiDataTarget target={binding->context,binding->node,n->generation};
        targets[index++]=target;
    }
    for(ScU32 i=0;i<index;i++){
        UiNode *n=ui_node(targets[i].context,targets[i].node);
        if(n && n->generation==targets[i].generation && n->view && n->view->provider==provider){
            ui_node_hold(n);
            ui_view_changed(n,change);
            if(change->kind==SC_UI_DATA_RESET || change->kind==SC_UI_DATA_REMOVE)ui_selection_prune(n->view->selection);
            if(!ui_quiet)ui_event(n,SC_UI_EVENT_MODEL_CHANGED,change,sizeof(*change));
            ui_node_leave(n);
        }
        ScUiContextRelease(targets[i].context);
    }
    free(targets);
}
static ScStatus ui_model_children_advance(const ScHashEntry *entry,void *context)
{UiModelChildren *children=*(UiModelChildren *const *)entry->value;
    children->revision=*(const ScU32 *)context;
    return 0;
}
ScStatus ScUiProviderNotify(ScUiProvider *provider,const ScUiDataChange *change)
{
    ScStatus s=ui_input(change,sizeof(*change));
    if(s<0)return s;
    if(!change->revision || change->kind<SC_UI_DATA_INSERT || change->kind>SC_UI_DATA_MOVE || change->flags || !ui_data_reserved(change->reserved,2))return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)provider,UI_PROVIDER);
    if(s<0)return s;
    if(change->revision<provider->revision)s=SC_UI_E_STALE;
    else{ScU32 previous=provider->revision;
        provider->revision=change->revision;
        if(provider->model && (change->kind==SC_UI_DATA_UPDATE || change->kind==SC_UI_DATA_SORTED)){
            UiModelAdapter *adapter=(UiModelAdapter *)provider->description.context;
            ScHashVisit(adapter->children,ui_model_children_advance,&provider->revision);
        }
        if(change->kind!=SC_UI_DATA_RESET && change->kind!=SC_UI_DATA_REMOVE){
            for(UiObject *o=ui_objects;o;o=o->next)if(o->kind==UI_SELECTION && !o->closing){ScUiSelection *selection=(ScUiSelection *)o;
                if(selection->provider==provider && selection->all_revision==previous)selection->all_revision=provider->revision;
            }
        }
        if(provider->order && change->kind!=SC_UI_DATA_SORTED){ScArrayRelease(provider->order);
            provider->order=0;
        }
        ui_provider_changed(provider,change);
    }
    ui_object_leave(&provider->object);
    return s;
}

static void ui_selection_dispose(UiObject *object,int quiet)
{
    ScUiSelection *selection=(ScUiSelection *)object;
    (void)quiet;
    ScHashRelease(selection->items);
    selection->items=0;
    ScArrayRelease(selection->universe);
    selection->universe=0;
    ScUiProviderRelease(selection->provider);
    selection->provider=0;
}
ScStatus ScUiSelectionCreate(ScUiProvider *provider,ScU32 flags,ScUiSelection **out)
{
    ScUiSelection *selection;
    void *candidate=0;
    ScStatus s;
    if(!out || (flags&~3u))return SC_UI_E_INVALID;
    s=ScUiProviderRetain(provider);
    if(s<0)return s;
    s=ui_object_new(sizeof(*selection),UI_SELECTION,ui_selection_dispose,&candidate);
    if(s<0){ScUiProviderRelease(provider);
        return s;
    }
    selection=(ScUiSelection *)candidate;
    selection->provider=provider;
    selection->flags=flags;
    selection->revision=1;
    s=ui_hash_new(1,&selection->items);
    if(s<0){ScUiSelectionRelease(selection);
        return ui_data_status(s);
    }
    *out=selection;
    return SC_UI_OK;
}
ScStatus ScUiSelectionRetain(ScUiSelection *selection){return ui_object_retain((UiObject *)selection,UI_SELECTION);
}
void ScUiSelectionRelease(ScUiSelection *selection){ui_object_release((UiObject *)selection);
}
static ScU32 ui_selection_entries(const ScUiSelection *selection)
{
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    return ScHashQuery(selection->items,&info)==0?info.length:0;
}
static int ui_selection_has(ScUiSelection *selection,ScU32 item)
{
    ScHashEntry entry;
    int present;
    if(!selection || !item || selection->object.closing)return 0;
    if(selection->all_revision && selection->all_revision!=selection->provider->revision)return 0;
    memset(&entry,0,sizeof(entry));
    entry.size=sizeof(entry);
    entry.version=1;
    present=ScHashGet(selection->items,&item,sizeof(item),&entry)==0;
    if(!selection->all_revision)return present;
    ScView view;
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    if(ScArrayView(selection->universe,&view)<0)return 0;
    const ScU32 *ids=(const ScU32 *)view.data;
    ScU32 lo=0,hi=view.bytes/4;
    while(lo<hi){ScU32 mid=lo+(hi-lo)/2;
        if(ids[mid]<item)lo=mid+1;
        else if(ids[mid]>item)hi=mid;
        else return !present;
    }
    return 0;
}
static void ui_selection_info(const ScUiSelection *selection,ScUiSelectionInfo *value)
{
    ScU32 entries=ui_selection_entries(selection);
    memset(value,0,sizeof(*value));
    value->size=sizeof(*value);
    value->version=1;
    value->count=selection->all_revision?(entries<=selection->all_count?selection->all_count-entries:0):entries;
    value->current=selection->current;
    value->anchor=selection->anchor;
    value->revision=selection->revision;
    value->flags=selection->flags;
}
ScStatus ScUiSelectionQuery(const ScUiSelection *selection,ScUiSelectionInfo *out)
{
    ScUiSelectionInfo value;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!selection || selection->object.kind!=UI_SELECTION || selection->object.closing)return SC_UI_E_STALE;
    if(selection->all_revision && selection->all_revision!=selection->provider->revision){ui_selection_prune((ScUiSelection *)selection);
        if(selection->all_revision!=selection->provider->revision)return SC_UI_E_BUSY;
    }
    ui_selection_info(selection,&value);
    memcpy(out,&value,sizeof(value));
    return 0;
}
static ScStatus ui_selection_item(ScUiSelection *selection,ScU32 item,ScUiDataReply *reply)
{
    ScUiDataRequest request;
    memset(&request,0,sizeof(request));
    request.size=sizeof(request);
    request.version=1;
    request.operation=SC_UI_DATA_ITEM;
    request.item=item;
    memset(reply,0,sizeof(*reply));
    reply->size=sizeof(*reply);
    reply->version=1;
    return ui_provider_call(selection->provider,&request,reply);
}
static ScStatus ui_selection_hash_copy(ScHash *source,ScHash **out)
{
    ScHash *copy=0;
    ScIterator iterator;
    ScHashEntry entry;
    unsigned char byte=1;
    ScStatus s=ui_hash_new(1,&copy);
    if(s<0)return ui_data_status(s);
    memset(&iterator,0,sizeof(iterator));
    iterator.size=sizeof(iterator);
    iterator.version=1;
    s=ScHashIterate(source,&iterator);
    while(s==0){
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        s=ScHashNext(&iterator,&entry);
        if(s==SC_UTIL_END){s=0;
            break;
        }
        if(s<0)break;
        s=ScHashSet(copy,entry.key,entry.key_bytes,&byte,0);
    }
    if(s<0){ScHashRelease(copy);
        return ui_data_status(s);
    }
    *out=copy;
    return 0;
}
static ScStatus ui_selection_notify(ScUiSelection *selection,ScU32 type,const ScUiSelectionInfo *value)
{
    ScUiProvider *provider=selection->provider;
    UiBinding *binding;
    UiDataTarget *targets=0;
    ScU32 count=0,used=0,bytes=0;
    ScStatus answer=0;
    for(binding=provider->bindings;binding;binding=binding->next)count++;
    if(!ui_multiply(count,sizeof(*targets),&bytes))return SC_UI_E_RANGE;
    if(bytes){targets=(UiDataTarget *)malloc(bytes);
        if(!targets)return SC_UI_E_MEMORY;
    }
    for(binding=provider->bindings;binding;binding=binding->next){
        UiNode *n=ui_node(binding->context,binding->node);
        if(!n || !n->view || n->view->selection!=selection || ScUiContextRetain(binding->context)<0)continue;
        UiDataTarget target={binding->context,n->id,n->generation};
        targets[used++]=target;
    }
    for(ScU32 i=0;i<used;i++){
        UiNode *n=ui_node(targets[i].context,targets[i].node);
        if(n && n->generation==targets[i].generation && n->view && n->view->selection==selection){
            ui_node_hold(n);
            ScUiEvent event;
            ui_event_record(&event,n,type);
            event.data=value;
            event.bytes=sizeof(*value);
            event.flags|=SC_UI_EVENT_PROGRAMMATIC;
            if(type==SC_UI_EVENT_SELECTION_CHANGING)event.flags|=SC_UI_EVENT_CANCELABLE;
            ScStatus s=ui_emit(n->owner,n,&event);
            if(type==SC_UI_EVENT_SELECTION_CHANGED){ui_node_damage(n);
                if(s<0)n->owner->notification_error=s;
            }
            else if(s==SC_UI_VETO || s<0)answer=s==SC_UI_VETO?SC_UI_E_CANCELLED:s;
            ui_node_leave(n);
        }
        ScUiContextRelease(targets[i].context);
        if(answer){for(ScU32 j=i+1;j<used;j++)ScUiContextRelease(targets[j].context);
            break;
        }
    }
    free(targets);
    return answer;
}
static ScStatus ui_selection_commit(ScUiSelection *selection,ScHash *candidate,ScU32 all_revision,ScU32 all_count,
    ScU32 current,ScU32 anchor,ScArray *universe)
{
    ScUiSelectionInfo info;
    ScStatus s;
    ScU32 revision=selection->revision;
    if(revision==UINT_MAX)return SC_UI_E_RANGE;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    info.current=current;
    info.anchor=anchor;
    info.revision=revision+1;
    info.flags=selection->flags;
    ScContainerInfo count;
    memset(&count,0,sizeof(count));
    count.size=sizeof(count);
    count.version=1;
    s=ScHashQuery(candidate,&count);
    if(s<0)return ui_data_status(s);
    info.count=all_revision?all_count-count.length:count.length;
    if(!(selection->flags&SC_UI_SELECTION_ALLOW_EMPTY) && !info.count){
        ScU32 total=0,ignored=0;
        s=ui_provider_count(selection->provider,0,&total,&ignored);
        if(s!=0)return s;
        if(total)return SC_UI_E_CANCELLED;
    }
    s=ui_selection_notify(selection,SC_UI_EVENT_SELECTION_CHANGING,&info);
    if(s<0)return s;
    if(selection->object.closing || selection->revision!=revision)return SC_UI_E_STALE;
    ScHash *old=selection->items;
    selection->items=candidate;
    if(universe || !all_revision){ScArray *previous=selection->universe;
        selection->universe=universe;
        ScArrayRelease(previous);
    }
    selection->all_revision=all_revision;
    selection->all_count=all_count;
    selection->current=current;
    selection->anchor=anchor;
    selection->revision=revision+1;
    ScHashRelease(old);
    ui_selection_notify(selection,SC_UI_EVENT_SELECTION_CHANGED,&info);
    return 0;
}
static ScStatus ui_selection_ids(ScUiSelection *selection,const ScU32 *ids,ScU32 count,ScU32 action)
{
    ScStatus s;
    ScHash *candidate=0;
    ScU32 revision,all_revision,all_count,selection_revision;
    unsigned char present=1;
    if(action>SC_UI_TOGGLE || (!ids && count) || (!(selection->flags&SC_UI_SELECTION_MULTI) && count>1))return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)selection,UI_SELECTION);
    if(s<0)return s;
    selection_revision=selection->revision;
    revision=selection->provider->revision;
    all_revision=selection->all_revision;
    all_count=selection->all_count;
    if(all_revision && all_revision!=revision){ui_selection_prune(selection);
        all_revision=selection->all_revision;
        if(all_revision!=revision){ui_object_leave(&selection->object);
            return SC_UI_E_BUSY;
        }
    }
    if(action==SC_UI_REPLACE){s=ui_hash_new(1,&candidate);
        all_revision=all_count=0;
    }
    else s=ui_selection_hash_copy(selection->items,&candidate);
    if(s==0 && all_revision && (action==SC_UI_ADD || action==SC_UI_TOGGLE)){
        ScView universe;
        memset(&universe,0,sizeof(universe));
        universe.size=sizeof(universe);
        universe.version=1;
        s=ScArrayView(selection->universe,&universe);
        const ScU32 *items=(const ScU32 *)universe.data;
        int outside=0;
        for(ScU32 i=0;s==0 && i<count;i++){ScU32 lo=0,hi=universe.bytes/4;
            while(lo<hi){ScU32 mid=lo+(hi-lo)/2;
                if(items[mid]<ids[i])lo=mid+1;
                else hi=mid;
            }
            if(lo==universe.bytes/4 || items[lo]!=ids[i])outside=1;
        }
        if(s==0 && outside){ScArray *selected=0;
            ScHash *normal=0;
            s=ScUiSelectionCopy(selection,&selected);
            if(s==0)s=ui_hash_new(1,&normal);
            ScContainerInfo info;
            memset(&info,0,sizeof(info));
            info.size=sizeof(info);
            info.version=1;
            if(s==0)s=ScArrayQuery(selected,&info);
            for(ScU32 i=0;s==0 && i<info.length;i++){ScU32 item;
                s=ScArrayGet(selected,i,&item);
                if(s==0)s=ScHashSet(normal,&item,4,&present,0);
            }
            ScArrayRelease(selected);
            if(s==0){ScHashRelease(candidate);
                candidate=normal;
                normal=0;
                all_revision=all_count=0;
            }
            ScHashRelease(normal);
        }
    }
    for(ScU32 i=0;s==0 && i<count;i++){
        ScUiDataReply item;
        s=ui_selection_item(selection,ids[i],&item);
        if(s!=0)break;
        if(!ids[i] || !(item.state&SC_UI_ENABLED)){s=SC_UI_E_INVALID;
            break;
        }
        ScHashEntry entry;
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        int in=ScHashGet(candidate,&ids[i],4,&entry)==0;
        int selected=all_revision?!in:in;
        int want=action==SC_UI_REMOVE?0:action==SC_UI_TOGGLE?!selected:1;
        int store=all_revision?!want:want;
        if(store)s=ScHashSet(candidate,&ids[i],4,&present,0);
        else if(in)s=ScHashRemove(candidate,&ids[i],4);
    }
    if(s==0 && selection->provider->revision!=revision)s=SC_UI_E_STALE;
    if(s==0 && selection->revision!=selection_revision)s=SC_UI_E_STALE;
    if(s==0){ScU32 current=count?ids[count-1]:0,anchor=action==SC_UI_REPLACE?(count?ids[0]:0):selection->anchor;
        s=ui_selection_commit(selection,candidate,all_revision,all_count,current,anchor,0);
        if(s==0)candidate=0;
    }
    ScHashRelease(candidate);
    ui_object_leave(&selection->object);
    return ui_data_status(s);
}
ScStatus ScUiSelectionSet(ScUiSelection *selection,ScU32 item,ScU32 action)
{
    if(!selection)return SC_UI_E_INVALID;
    if(!(selection->flags&SC_UI_SELECTION_MULTI) && action==SC_UI_ADD)action=SC_UI_REPLACE;
    return ui_selection_ids(selection,&item,1,action);
}
ScStatus ScUiSelectionClear(ScUiSelection *selection){return ui_selection_ids(selection,0,0,SC_UI_REPLACE);
}
ScStatus ScUiSelectionRange(ScUiSelection *selection,ScU32 first,ScU32 last,ScU32 action)
{
    ScStatus s=ui_object_enter((UiObject *)selection,UI_SELECTION);
    if(s<0)return s;
    ScU32 selection_revision=selection->revision;
    ScUiDataRequest request;
    ScUiDataReply a,b;
    ScU32 *ids=0,bytes=0;
    memset(&request,0,sizeof(request));
    request.size=sizeof(request);
    request.version=1;
    request.operation=SC_UI_DATA_INDEX_OF;
    memset(&a,0,sizeof(a));
    a.size=sizeof(a);
    a.version=1;
    memset(&b,0,sizeof(b));
    b.size=sizeof(b);
    b.version=1;
    request.item=first;
    s=ui_provider_call(selection->provider,&request,&a);
    if(s==0){request.item=last;
        s=ui_provider_call(selection->provider,&request,&b);
    }
    if(s==0 && a.parent!=b.parent)s=SC_UI_E_INVALID;
    ScU32 begin=a.total<b.total?a.total:b.total,end=a.total>b.total?a.total:b.total,count=0;
    if(s==0 && !ui_add(end-begin,1,&count))s=SC_UI_E_RANGE;
    if(s==0 && action==SC_UI_REPLACE && (selection->flags&SC_UI_SELECTION_MULTI)
        && !(selection->provider->description.flags&SC_UI_PROVIDER_TREE) && !a.parent && begin==0){
        ScU32 total=0,revision=0;
        s=ui_provider_count(selection->provider,0,&total,&revision);
        if(s==0 && count==total){ScHash *empty=0;
            ScArray *universe=0;
            s=ui_hash_new(1,&empty);
            if(s==0)s=ui_array_new(4,&universe);
            for(ScU32 i=0;s==0 && i<total;i++){ScU32 item,generation,state;
                s=ui_provider_item(selection->provider,0,i,&item,&generation,&state);
                if(s==0)s=ScArrayInsert(universe,i,&item,1);
                if((i&4095u)==4095u)ScProcessYield();
            }
            if(s==0){ScView view;
                memset(&view,0,sizeof(view));
                view.size=sizeof(view);
                view.version=1;
                s=ScArrayView(universe,&view);
                if(s==0){ScU32 *items=(ScU32 *)view.data;
                    /* 无递归heap-sort，不借有符号减法比较完整U32 stable IDs。 */
                    for(ScU32 i=total/2;i;i--){ScU32 at=i-1,value=items[at];
                        for(;;){ScU32 child=at*2+1;
                            if(child>=total)break;
                            if(child+1<total && items[child+1]>items[child])child++;
                            if(items[child]<=value)break;
                            items[at]=items[child];
                            at=child;
                        }
                        items[at]=value;
                    }
                    for(ScU32 end=total;end>1;end--){ScU32 top=items[0];
                        items[0]=items[end-1];
                        items[end-1]=top;
                        ScU32 at=0,value=items[0];
                        for(;;){ScU32 child=at*2+1;
                            if(child>=end-1)break;
                            if(child+1<end-1 && items[child+1]>items[child])child++;
                            if(items[child]<=value)break;
                            items[at]=items[child];
                            at=child;
                        }
                        items[at]=value;
                    }
                    for(ScU32 i=1;i<total;i++)if(items[i]==items[i-1]){s=SC_UI_E_INVALID;
                        break;
                    }
                }
            }
            if(s==0 && selection->provider->revision!=revision)s=SC_UI_E_STALE;
            /* provider枚举可合法重入清空选择，候选不能覆盖该显式修改。 */
            if(s==0 && selection->revision!=selection_revision)s=SC_UI_E_STALE;
            if(s==0){s=ui_selection_commit(selection,empty,revision,total,last,first,universe);
                if(s==0){empty=0;
                    universe=0;
                }
            }
            ScHashRelease(empty);
            ScArrayRelease(universe);
            ui_object_leave(&selection->object);
            return ui_data_status(s);
        }
    }
    if(s==0){if(!ui_multiply(count,4,&bytes))s=SC_UI_E_RANGE;
        else{ids=(ScU32 *)malloc(bytes);
            if(!ids)s=SC_UI_E_MEMORY;
        }
    }
    for(ScU32 i=0;s==0 && i<count;i++){ScU32 revision,state;
        s=ui_provider_item(selection->provider,a.parent,begin+i,&ids[i],&revision,&state);
    }
    if(s==0 && selection->revision!=selection_revision)s=SC_UI_E_STALE;
    if(s==0)s=ui_selection_ids(selection,ids,count,action);
    free(ids);
    ui_object_leave(&selection->object);
    return ui_data_status(s);
}
ScStatus ScUiSelectionCopy(const ScUiSelection *source,ScArray **out)
{
    ScUiSelection *selection=(ScUiSelection *)source;
    ScArray *copy=0;
    ScStatus s;
    if(!out)return SC_UI_E_INVALID;
    s=ui_object_enter((UiObject *)selection,UI_SELECTION);
    if(s<0)return s;
    ScU32 revision=selection->provider->revision;
    if(selection->all_revision && selection->all_revision!=revision){ui_selection_prune(selection);
        if(selection->all_revision!=revision)s=SC_UI_E_BUSY;
    }
    if(s==0)s=ui_array_new(sizeof(ScU32),&copy);
    if(s==0 && selection->all_revision){
        ScU32 total=0,ignored;
        s=ui_provider_count(selection->provider,0,&total,&ignored);
        for(ScU32 i=0;s==0 && i<total;i++){
            ScU32 item,state,generation;
            s=ui_provider_item(selection->provider,0,i,&item,&generation,&state);
            if(s==0 && ui_selection_has(selection,item)){ScContainerInfo info;
                memset(&info,0,sizeof(info));
                info.size=sizeof(info);
                info.version=1;
                s=ScArrayQuery(copy,&info);
                if(s==0)s=ScArrayInsert(copy,info.length,&item,1);
            }
        }
    }
    else if(s==0){
        ScIterator iterator;
        memset(&iterator,0,sizeof(iterator));
        iterator.size=sizeof(iterator);
        iterator.version=1;
        s=ScHashIterate(selection->items,&iterator);
        ScU32 used=0;
        while(s==0){ScHashEntry entry;
            memset(&entry,0,sizeof(entry));
            entry.size=sizeof(entry);
            entry.version=1;
            s=ScHashNext(&iterator,&entry);
            if(s==SC_UTIL_END){s=0;
                break;
            }
            if(s<0)break;
            if(entry.key_bytes!=4){s=SC_UI_E_INVALID;
                break;
            }
            s=ScArrayInsert(copy,used++,entry.key,1);
        }
    }
    if(s==0 && selection->provider->revision!=revision)s=SC_UI_E_STALE;
    if(s==0){*out=copy;
        copy=0;
    }
    ScArrayRelease(copy);
    ui_object_leave(&selection->object);
    return ui_data_status(s);
}
static void ui_selection_prune(ScUiSelection *selection)
{
    if(!selection || selection->object.closing)return;
    if(selection->all_revision){
        if(selection->all_revision==selection->provider->revision)return;
        ScView view;
        memset(&view,0,sizeof(view));
        view.size=sizeof(view);
        view.version=1;
        if(ScArrayView(selection->universe,&view)<0)return;
        ScHash *candidate=0;
        ScStatus s=ui_selection_hash_copy(selection->items,&candidate);
        if(s<0)return;
        const ScU32 *items=(const ScU32 *)view.data;
        unsigned char absent=1;
        ScU32 current=selection->current,anchor=selection->anchor;
        for(ScU32 i=0;s==0 && i<view.bytes/4;i++){
            ScUiDataReply reply;
            s=ui_selection_item(selection,items[i],&reply);
            if(s==SC_UI_E_STALE || s==SC_UI_E_NOT_FOUND){s=ScHashSet(candidate,items+i,4,&absent,0);
                if(current==items[i])current=0;
                if(anchor==items[i])anchor=0;
            }
            if((i&4095u)==4095u)ScProcessYield();
        }
        if(s==0){ScHash *old=selection->items;
            selection->items=candidate;
            candidate=old;
            selection->all_revision=selection->provider->revision;
            selection->current=current;
            selection->anchor=anchor;
        }
        ScHashRelease(candidate);
        return;
    }
    ScArray *ids=0;
    if(ScUiSelectionCopy(selection,&ids)<0)return;
    ScContainerInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    if(ScArrayQuery(ids,&info)==0)for(ScU32 i=0;i<info.length;i++){
        ScU32 item=0;
        ScUiDataReply reply;
        if(ScArrayGet(ids,i,&item)<0)break;
        if(ui_selection_item(selection,item,&reply)==SC_UI_E_STALE || !reply.item){
            if(!selection->all_revision)ScHashRemove(selection->items,&item,4);
            if(selection->current==item)selection->current=0;
            if(selection->anchor==item)selection->anchor=0;
        }
    }
    ScArrayRelease(ids);
}

static ScWideCounter ui_scroll_limit(ScWideCounter content,ScU32 viewport)
{
    if(!content.hi && content.lo<=viewport){ScWideCounter zero={0,0};
        return zero;
    }
    ScU32 before=content.lo;
    content.lo-=viewport;
    if(before<viewport)content.hi--;
    return content;
}
ScStatus ScUiScrollQuery(ScUiContext *context,ScUiNode id,ScUiScrollInfo *out)
{
    UiNode *node=ui_node(context,id);
    ScUiScrollInfo value;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!node)return SC_UI_E_STALE;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.x=node->scroll_x;
    value.y=node->scroll_y;
    value.content_width=node->content_width;
    value.content_height=node->content_height;
    ScUiRect viewport;s=ui_scroll_viewport(node,&viewport);if(s<0)return s;
    value.viewport_width=viewport.width>0?(ScU32)viewport.width:0;
    value.viewport_height=viewport.height>0?(ScU32)viewport.height:0;
    value.line_step_x=node->scroll_step_x;
    value.line_step_y=node->scroll_step_y;
    memcpy(out,&value,sizeof(value));
    return SC_UI_OK;
}
ScStatus ScUiScrollTo(ScUiContext *context,ScUiNode id,const ScWideCounter *x,const ScWideCounter *y)
{
    UiNode *node=ui_node(context,id);
    ScWideCounter next_x,next_y,max_x,max_y;
    if(!node)return SC_UI_E_STALE;
    if(!x || !y)return SC_UI_E_INVALID;
    ScUiRect viewport;ScStatus checked=ui_scroll_viewport(node,&viewport);if(checked<0)return checked;
    max_x=ui_scroll_limit(node->content_width,viewport.width>0?(ScU32)viewport.width:0);
    max_y=ui_scroll_limit(node->content_height,viewport.height>0?(ScU32)viewport.height:0);
    next_x=ui_wide_compare(x,&max_x)>0?max_x:*x;
    next_y=ui_wide_compare(y,&max_y)>0?max_y:*y;
    if(ui_wide_compare(&next_x,&node->scroll_x)==0 && ui_wide_compare(&next_y,&node->scroll_y)==0)return SC_UI_OK;
    if(node->value_revision==UINT_MAX || context->revision==UINT_MAX)return SC_UI_E_RANGE;
    ui_node_hold(node);
    node->scroll_x=next_x;
    node->scroll_y=next_y;
    node->value_revision++;
    context->revision++;
    ui_node_layout_dirty(node);
    ui_node_damage(node);
    ScStatus notice=ui_event(node,SC_UI_EVENT_VALUE_CHANGED,0,0);
    if(notice<0)context->notification_error=notice;
    ui_node_leave(node);
    return SC_UI_OK;
}
static ScStatus ui_access_text(UiNode *node,ScU32 part,ScString **out)
{
    ScString *source=0;
    ScStatus s;
    if(part==SC_UI_ACCESSIBLE_NAME)source=node->accessible_name?node->accessible_name:node->text;
    else if(part==SC_UI_ACCESSIBLE_HELP)source=node->tooltip;
    else if(part==SC_UI_ACCESSIBLE_VALUE){
        if(node->flags&SC_UI_PASSWORD)return ui_data_status(ScStringCreate("",0,out));
        if(node->kind==SC_UI_EDIT || node->kind==SC_UI_TEXTBOX)return ScUiNodeCopyText(node->owner,node->id,out);
        if(node->value.value.type==SC_VALUE_UTF8)return ui_data_status(ScStringCreate(node->value.value.data,node->value.value.bytes,out));
        char number[64];
        int bytes=0;
        ScValue *value=&node->value.value;
        if(value->type==SC_VALUE_I32)bytes=snprintf(number,sizeof(number),"%d",(ScI32)value->lo);
        else if(value->type==SC_VALUE_U32 || value->type==SC_VALUE_BOOL)bytes=snprintf(number,sizeof(number),"%u",value->lo);
        else if(value->type==SC_VALUE_COUNTER)bytes=snprintf(number,sizeof(number),"0x%08x%08x",value->hi,value->lo);
        else if(node->kind==SC_UI_CHECKBOX || node->kind==SC_UI_RADIO){
            const char *state=(node->state&SC_UI_MIXED)?"mixed":(node->state&SC_UI_CHECKED)?"checked":"unchecked";
            return ui_data_status(ScStringCreate(state,(ScU32)strlen(state),out));
        }
        else source=node->text;
        if(bytes)return ui_data_status(ScStringCreate(number,(ScU32)bytes,out));
    }
    else return SC_UI_E_INVALID;
    if(!source)return ui_data_status(ScStringCreate("",0,out));
    s=ScStringRetain(source);
    if(s==0)*out=source;
    return ui_data_status(s);
}
ScStatus ScUiAccessibleText(ScUiContext *context,ScUiNode id,ScU32 part,ScString **out)
{
    UiNode *node=ui_node(context,id);
    if(!node)return SC_UI_E_STALE;
    if(!out)return SC_UI_E_INVALID;
    return ui_access_text(node,part,out);
}
ScStatus ScUiAccessibleQuery(ScUiContext *context,ScUiNode id,ScUiAccessibleInfo *out)
{
    UiNode *node=ui_node(context,id),*root;
    ScUiAccessibleInfo value;
    ScString *name=0,*text=0;
    ScStatus s=ui_output(out,sizeof(*out));
    if(s<0)return s;
    if(!node)return SC_UI_E_STALE;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    value.node=id;
    value.parent=node->parent?node->parent->id:0;
    value.role=node->kind==SC_UI_SECRET?SC_UI_TEXTBOX:node->kind;
    value.state=node->effective;
    value.revision=node->value_revision;
    value.rectangle=node->rectangle;
    root=ui_root(context,id);
    if(root && root->window){
        ScWindowInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScWindowQuery(root->window->window,&info);
        if(s<0)return ui_cause(context,s,UI_OP_NODE,id);
        ScI32 left=(info.width-(ScI32)info.client_width)/2;
        ScI32 top=info.height-(ScI32)info.client_height-left;
        if((info.x>0 && value.rectangle.x>INT_MAX-info.x) || (info.x<0 && value.rectangle.x<INT_MIN-info.x)
            || (info.y>0 && value.rectangle.y>INT_MAX-info.y) || (info.y<0 && value.rectangle.y<INT_MIN-info.y))return SC_UI_E_RANGE;
        value.rectangle.x+=info.x;
        value.rectangle.y+=info.y;
        if(value.rectangle.x>INT_MAX-left || value.rectangle.y>INT_MAX-top)return SC_UI_E_RANGE;
        value.rectangle.x+=left;
        value.rectangle.y+=top;
    }
    if((node->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)){
        if(node->flags&SC_UI_FOCUSABLE)value.actions|=1u<<(SC_UI_ACCESS_FOCUS-1u);
        if(node->kind==SC_UI_BUTTON || node->kind==SC_UI_LABEL)value.actions|=1u<<(SC_UI_ACCESS_INVOKE-1u);
        if(node->kind==SC_UI_CHECKBOX || node->kind==SC_UI_RADIO)value.actions|=1u<<(SC_UI_ACCESS_TOGGLE-1u);
        if(node->view)value.actions|=(1u<<(SC_UI_ACCESS_SELECT-1u))|(1u<<(SC_UI_ACCESS_EXPAND-1u));
        if(node->kind==SC_UI_TAB)value.actions|=1u<<(SC_UI_ACCESS_SELECT-1u);
        if(node->content_width.hi || node->content_height.hi || node->content_width.lo>(ScU32)node->rectangle.width
            || node->content_height.lo>(ScU32)node->rectangle.height)value.actions|=1u<<(SC_UI_ACCESS_SCROLL-1u);
        if(!(node->state&SC_UI_READONLY))value.actions|=1u<<(SC_UI_ACCESS_SET_VALUE-1u);
    }
    s=ui_access_text(node,SC_UI_ACCESSIBLE_NAME,&name);
    if(s==0)s=ui_access_text(node,SC_UI_ACCESSIBLE_VALUE,&text);
    if(s==0){ScContainerInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScStringQuery(name,&info);
        if(s==0)value.name_bytes=info.length;
        if(s==0)s=ScStringQuery(text,&info);
        if(s==0)value.value_bytes=info.length;
    }
    if(s==0)memcpy(out,&value,sizeof(value));
    ScStringRelease(name);
    ScStringRelease(text);
    return ui_data_status(s);
}
ScStatus ScUiAccessibleSetName(ScUiContext *context,ScUiNode id,const ScUiText *text)
{
    UiNode *node=ui_node(context,id);
    ScString *next=0;
    ScStatus s;
    if(!node)return SC_UI_E_STALE;
    s=ui_text_copy(text,&next);
    if(s<0)return ui_cause(context,s,UI_OP_NODE,id);
    if(node->value_revision==UINT_MAX){ScStringRelease(next);
        return SC_UI_E_RANGE;
    }
    ScString *old=node->accessible_name;
    node->accessible_name=next;
    node->value_revision++;
    ScStringRelease(old);
    return 0;
}
ScStatus ScUiAccessibleAction(ScUiContext *context,ScUiNode id,ScU32 action,const ScValue *value)
{
    UiNode *node=ui_node(context,id);
    if(!node)return SC_UI_E_STALE;
    if((node->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))!=(SC_UI_VISIBLE|SC_UI_ENABLED))return SC_UI_E_CANCELLED;
    if(action==SC_UI_ACCESS_FOCUS)return ScUiFocus(context,id);
    if(action==SC_UI_ACCESS_INVOKE){if(node->kind==SC_UI_LABEL && node->label_target)return ScUiFocus(context,node->label_target);
        if(node->kind!=SC_UI_BUTTON)return SC_UI_E_INVALID;
        return ScUiCommand(context,id,node->command);
    }
    if(node->state&SC_UI_READONLY)return SC_UI_E_CANCELLED;
    if(action==SC_UI_ACCESS_TOGGLE){
        if(node->kind==SC_UI_RADIO)return ScUiRadioSelect(context,node->parent?node->parent->id:0,id);
        if(node->kind!=SC_UI_CHECKBOX)return SC_UI_E_INVALID;
        return ScUiCheckSet(context,id,(node->state&SC_UI_CHECKED)?0:1);
    }
    ScStatus s=ui_input(value,sizeof(*value));
    if(s<0)return s;
    if(action==SC_UI_ACCESS_SET_VALUE){
        if(value->type==SC_VALUE_UTF8){ScUiText text={sizeof(text),1,(const char *)value->data,value->bytes};
            return ScUiNodeSetText(context,id,&text);
        }
        return ScUiNodeSetValue(context,id,value);
    }
    if(action==SC_UI_ACCESS_SELECT){if(node->kind==SC_UI_TAB && value->type==SC_VALUE_U32)return ScUiTabSelect(context,id,value->lo);
        if(!node->view || !node->view->selection || value->type!=SC_VALUE_U32)return SC_UI_E_INVALID;
        return ScUiSelectionSet(node->view->selection,value->lo,SC_UI_REPLACE);
    }
    if(action==SC_UI_ACCESS_EXPAND){if(!node->view || !node->view->selection || value->type!=SC_VALUE_BOOL)return SC_UI_E_INVALID;
        return ScUiTreeExpand(context,id,node->view->selection->current,value->lo);
    }
    if(action==SC_UI_ACCESS_SCROLL){if(value->type!=SC_VALUE_COUNTER && value->type!=SC_VALUE_U32)return SC_UI_E_INVALID;
        ScWideCounter offset={value->lo,value->hi};
        return ScUiScrollTo(context,id,&node->scroll_x,&offset);
    }
    return SC_UI_E_INVALID;
}

ScStatus ScUiDockMove(ScUiContext *context,ScUiNode panel_id,ScUiNode host_id,ScU32 dock)
{
    UiNode *panel=ui_node(context,panel_id),*host=ui_node(context,host_id);
    ScStatus s;
    if(!panel || !host)return SC_UI_E_STALE;
    if(panel->window || panel==host || dock<SC_UI_DOCK_TOP || dock>SC_UI_DOCK_FILL)return SC_UI_E_INVALID;
    for(UiNode *walk=host;walk;walk=walk->parent)if(walk==panel)return SC_UI_E_INVALID;
    if(context->revision>=UINT_MAX-2u)return SC_UI_E_RANGE;
    s=ScUiNodeMove(context,panel_id,host_id,0);
    if(s<0)return s;
    panel->layout.dock=dock;
    ui_node_layout_dirty(panel);
    ui_node_damage(panel);
    return 0;
}
ScStatus ScUiDockFloat(ScUiContext *context,ScUiNode panel,const ScUiWindowDesc *description,ScUiNode *out)
{
    UiNode *node=ui_node(context,panel);
    ScUiNode root=0;
    ScStatus s;
    if(!node)return SC_UI_E_STALE;
    if(!out || node->window)return SC_UI_E_INVALID;
    if(context->revision>=UINT_MAX-3u)return SC_UI_E_RANGE;
    s=ScUiWindowCreate(context,description,&root);
    if(s<0)return s;
    s=ScUiDockMove(context,panel,root,SC_UI_DOCK_FILL);
    if(s<0){ScUiNodeDestroy(context,root);
        return s;
    }
    *out=root;
    return 0;
}

typedef struct {ScU32 node,kind,parent,reserved;
    ScUiLayoutDesc layout;
}
UiSavedLayout;
ScStatus ScUiLayoutSave(ScUiContext *context,ScUiNode root_id,ScConfig *config)
{
    UiNode *root=ui_node(context,root_id),*node;
    ScU32 count=0,body=0,bytes=0;
    unsigned char *blob;
    ScStatus s;
    if(!root || !root->window)return SC_UI_E_STALE;
    if(!config)return SC_UI_E_INVALID;
    for(node=context->all_nodes;node;node=node->all_next)if(node->root==root_id && !(node->private_flags&UI_NODE_RETIRED))count++;
    if(!ui_multiply(count,sizeof(UiSavedLayout),&body) || !ui_add(body,24,&bytes))return SC_UI_E_RANGE;
    blob=(unsigned char *)calloc(1,bytes);
    if(!blob)return SC_UI_E_MEMORY;
    ScU32 header[6]={0x59414c55u,1,bytes,count,root_id,context->revision};
    memcpy(blob,header,sizeof(header));
    UiSavedLayout *rows=(UiSavedLayout *)(blob+24);
    ScU32 at=0;
    for(node=context->all_nodes;node;node=node->all_next)if(node->root==root_id && !(node->private_flags&UI_NODE_RETIRED)){
        rows[at].node=node->id;
        rows[at].kind=node->kind;
        rows[at].parent=node->parent?node->parent->id:0;
        rows[at++].layout=node->layout;
    }
    /* 一个拥有的BYTES值一次Set，不以多次ConfigSet留下半份布局。 */
    ScValue value={sizeof(value),1,SC_VALUE_BYTES,0,0,0,blob,bytes};
    s=ScConfigSet(config,"SCUI","layout",&value);
    free(blob);
    return ui_cause(context,s,UI_OP_LAYOUT,root_id);
}
ScStatus ScUiLayoutRestore(ScUiContext *context,ScUiNode root_id,const ScConfig *config)
{
    UiNode *root=ui_node(context,root_id);
    ScValue value;
    ScStatus s;
    UiSavedLayout *rows=0;
    ScU32 bytes,count,revision;
    ScHash *indices=0;
    ScU32 *parents=0;
    unsigned char *colors=0;
    if(!root || !root->window)return SC_UI_E_STALE;
    if(!config)return SC_UI_E_INVALID;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    s=ScConfigGet(config,"SCUI","layout",&value);
    if(s<0)return ui_cause(context,s,UI_OP_LAYOUT,root_id);
    if(value.type!=SC_VALUE_BYTES || value.bytes<24 || !value.data)return SC_UI_E_INVALID;
    const unsigned char *blob=(const unsigned char *)value.data;
    count=ui_data_u32(blob+12);
    revision=ui_data_u32(blob+20);
    if(ui_data_u32(blob)!=0x59414c55u || ui_data_u32(blob+4)!=1 || ui_data_u32(blob+8)!=value.bytes
        || ui_data_u32(blob+16)!=root_id || !revision || !ui_multiply(count,sizeof(*rows),&bytes)
        || bytes!=value.bytes-24)return SC_UI_E_INVALID;
    if(context->revision>=UINT_MAX-2u)return SC_UI_E_RANGE;
    if(!count)return SC_UI_E_INVALID;
    if(bytes){rows=(UiSavedLayout *)malloc(bytes);
        parents=(ScU32 *)malloc(count*4);
        colors=(unsigned char *)calloc(1,count);
        if(!rows || !parents || !colors){free(rows);
            free(parents);
            free(colors);
            return SC_UI_E_MEMORY;
        }
        memcpy(rows,blob+24,bytes);
    }
    s=ui_hash_new(4,&indices);
    for(ScU32 i=0;i<count;i++){
        if(s<0)break;
        UiNode *node=ui_node(context,rows[i].node);
        ScUiLayoutDesc *layout=&rows[i].layout;
        if(!node || node->kind!=rows[i].kind || rows[i].reserved || node->root!=root_id || (node->window && node!=root)){s=SC_UI_E_STALE;
            break;
        }
        s=ui_layout_valid(context,layout);
        if(s<0)break;
        s=ScHashSet(indices,&rows[i].node,4,&i,SC_HASH_INSERT_ONLY);
        if(s==SC_UTIL_E_EXISTS)s=SC_UI_E_INVALID;
    }
    for(ScU32 i=0;s==0 && i<count;i++){
        if(rows[i].node==root_id){if(rows[i].parent){s=SC_UI_E_INVALID;
                break;
            }
            parents[i]=UINT_MAX;
            continue;
        }
        ScHashEntry entry;
        memset(&entry,0,sizeof(entry));
        entry.size=sizeof(entry);
        entry.version=1;
        s=ScHashGet(indices,&rows[i].parent,4,&entry);
        if(s<0){s=SC_UI_E_INVALID;
            break;
        }
        parents[i]=*(const ScU32 *)entry.value;
    }
    for(ScU32 i=0;s==0 && i<count;i++)if(!colors[i]){
        ScU32 at=i;
        while(at!=UINT_MAX && !colors[at]){colors[at]=1;
            at=parents[at];
        }
        if(at!=UINT_MAX && colors[at]==1){s=SC_UI_E_INVALID;
            break;
        }
        at=i;
        while(at!=UINT_MAX && colors[at]==1){colors[at]=2;
            at=parents[at];
        }
    }
    if(s==0){
        /* 发布没有分配点或应用callback；布局计算仍由core候选阶段完成。 */
        context->batch++;
        for(ScU32 i=0;i<count;i++){
            UiNode *node=ui_node(context,rows[i].node);
            node->layout=rows[i].layout;
            if(node->id!=root_id && (!node->parent || node->parent->id!=rows[i].parent)){
                UiNode *parent=ui_node(context,rows[i].parent);
                ui_detach_node(node);
                ui_append_node(parent,node,0);
            }
            ui_node_layout_dirty(node);
        }
        context->revision++;
        context->batch--;
        ui_node_layout_dirty(root);
    }
    ScHashRelease(indices);
    free(colors);
    free(parents);
    free(rows);
    return ui_cause(context,s,UI_OP_LAYOUT,root_id);
}

/* Drop只保存本人UI对象；foreign身份、命中、票及连接必须来自正式OS broker。 */
typedef struct UiDropRoot UiDropRoot;
typedef struct UiDropNode UiDropNode;
typedef struct UiDropPeer UiDropPeer;
typedef struct UiDrag UiDrag;
enum {UI_DROP_ROOT_OBJECT=8,UI_DROP_NODE_OBJECT=9,UI_DROP_PEER_OBJECT=10,UI_DRAG_OBJECT=11};
struct UiDropNode {UiObject object;
    UiDropNode *next;
    UiDropRoot *root;
    ScUiNode node;
    ScU32 generation,active;
    ScUiDropTargetDesc description;
    ScU32 *formats;
};
struct UiDropRoot {
    UiObject object;
    UiDropRoot *next;
    ScUiContext *context;
    ScUiNode root;
    ScWindow window;
    ScU32 generation;
    ScIpcEndpoint endpoint;
    ScDropTargetRef reference;
    UiDropNode *nodes;
    UiDropPeer *peers;
    ScU32 owner_live,format_count,actions,formats[64];
};
struct UiDropPeer {
    UiObject object;
    UiDropPeer *next;
    UiDropRoot *root;
    ScIpcChannel channel;
    ScBrokerIdentity sender;
    ScDragEnvelope envelope;
    ScDropInfo information;
    ScUiNode node;
    ScU32 node_generation,format,action,total,received,phase;
    unsigned char *body;
    ScWideCounter deadline;
    ScU32 listed;
    UiDropPeer *receipt_next;
    ScU32 receipt_id,receipt_state,deferred,allowed_actions;ScI32 receipt_status;
    ScUiDropResult receipt_result;
};
struct UiDrag {
    UiObject object;
    UiDrag *next;
    ScUiContext *context;
    ScUiNode source,preview;
    ScU32 token,root,generation,epoch,actions,nonce,phase,up;
    ScWindow window;
    unsigned char *offer;
    ScU32 bytes,format,offset,sent,action;
    ScUiIcon *icon;
    ScIpcChannel channel;
    ScDropInfo target;
    ScDragEnvelope last;
    ScWideCounter deadline;
    ScU32 cancelled,captured;
    ScU32 deferred_opt,deferred_stage,deferred_actions,deferred_notified;ScStatus deferred_failure;
};
static UiDropRoot *ui_drop_roots;
static UiDrag *ui_drags;
static ScU32 ui_drag_nonce=1;
#include "scui_drop_deferred.inc"

static UiDropRoot *ui_drop_root_find(ScUiContext *c,ScU32 root)
{for(UiDropRoot *r=ui_drop_roots;r;r=r->next)if(r->context==c && r->root==root && !r->object.closing)return r;
    return 0;
}
static UiDropNode *ui_drop_node_find(UiDropRoot *r,ScU32 node)
{for(UiDropNode *n=r?r->nodes:0;n;n=n->next)if(n->node==node && n->active && !n->object.closing)return n;
    return 0;
}
static int ui_drop_identity(const ScBrokerIdentity *a,const ScBrokerIdentity *b)
{return a->pid==b->pid && a->generation==b->generation && a->session==b->session && a->realm==b->realm;
}
static ScDropValidateRequest ui_drop_validation(const ScDragEnvelope *e)
{ScDropValidateRequest v={sizeof(v),1,e->ticket_lo,e->ticket_hi,e->source_window,e->source_generation,e->target_id,e->target_generation};
    return v;
}
static ScDropToken ui_drop_token(const ScDropInfo *info)
{ScDropToken token={sizeof(token),1,info->ticket_lo,info->ticket_hi};
    return token;
}
static int ui_drop_envelope(const ScDragEnvelope *e,ScU32 bytes)
{return bytes>=sizeof(*e) && e->size==sizeof(*e) && ((e->version==1 && e->type>=SC32_DRAG_OFFER && e->type<=SC32_DRAG_REJECT) || (e->version==2 && e->type>=SC32_DRAG_OFFER && e->type<=UI_D2_FINAL)) && e->body_bytes==bytes-sizeof(*e) && (e->nonce_lo || e->nonce_hi);
}
static ScStatus ui_drag_offer_check(const void *body,ScU32 available,int whole)
{
    if(!body || available<sizeof(ScDragOffer))return SC_UI_E_INVALID;
    const ScDragOffer *offer=(const ScDragOffer *)body;
    ScU32 table;
    if(offer->size!=sizeof(*offer) || offer->version!=1 || !offer->count || offer->count>SC32_DROP_FORMAT_LIMIT
        || !ui_multiply(offer->count,sizeof(ScDragFormat),&table) || !ui_add(table,sizeof(*offer),&table)
        || table>available || offer->total_bytes<table || offer->total_bytes>16777216u || (whole && offer->total_bytes!=available)
        || (!whole && available!=table))return SC_UI_E_INVALID;
    const ScDragFormat *formats=(const ScDragFormat *)(offer+1);
    for(ScU32 i=0;i<offer->count;i++){
        if(!formats[i].format || formats[i].flags || formats[i].offset<table || formats[i].offset>offer->total_bytes
            || formats[i].bytes>offer->total_bytes-formats[i].offset)return SC_UI_E_INVALID;
        for(ScU32 j=0;j<i;j++)if(formats[i].format==formats[j].format
            || (formats[i].bytes && formats[j].bytes && formats[i].offset<formats[j].offset+formats[j].bytes && formats[j].offset<formats[i].offset+formats[i].bytes))return SC_UI_E_INVALID;
    }
    return 0;
}
static ScStatus ui_drop_send(ScIpcChannel channel,const ScDragEnvelope *envelope,const void *body,ScU32 bytes)
{
    if(bytes>65536u-sizeof(*envelope))return SC_UI_E_RANGE;
    unsigned char *frame=(unsigned char *)malloc(sizeof(*envelope)+bytes);
    if(!frame)return SC_UI_E_MEMORY;
    ScDragEnvelope e=*envelope;
    e.size=sizeof(e);
    e.version=envelope->version==2?2:1;
    e.body_bytes=bytes;
    memcpy(frame,&e,sizeof(e));
    if(bytes)memcpy(frame+sizeof(e),body,bytes);
    ScStatus s=ScIpcSend(channel,SC32_IPC_FRAME_DATA,frame,sizeof(e)+bytes);
    free(frame);
    return s;
}
static void ui_drop_peer_dispose(UiObject *object,int quiet)
{
    UiDropPeer *p=(UiDropPeer *)object;
    (void)quiet;
    UiDropPeer **slot=&p->root->peers;
    while(*slot && *slot!=p)slot=&(*slot)->next;
    if(*slot)*slot=p->next;
    ScIpcClose(p->channel);
    free(p->body);
    p->body=0;
    ui_object_release(&p->root->object);
}
static void ui_drop_peer_close(UiDropPeer *p)
{if(p->listed){ui_d2_fail(p,p->receipt_status<0?p->receipt_status:SC_UI_E_CANCELLED);p->listed=0;
        ui_object_release(&p->object);
    }
}
static void ui_drop_root_dispose(UiObject *object,int quiet)
{
    UiDropRoot *r=(UiDropRoot *)object;
    (void)quiet;
    UiDropRoot **slot=&ui_drop_roots;
    while(*slot && *slot!=r)slot=&(*slot)->next;
    if(*slot)*slot=r->next;
    if(r->reference.target_id)ScDropUnregister(r->window,r->generation);
    if(r->endpoint)ScIpcClose(r->endpoint);
    r->endpoint=0;
    while(r->peers){UiDropPeer *p=r->peers;
        r->peers=p->next;
        p->next=0;
        ui_drop_peer_close(p);
    }
}
static void ui_drop_node_dispose(UiObject *object,int quiet)
{
    UiDropNode *n=(UiDropNode *)object;
    UiDropRoot *r=n->root;
    UiDropNode **slot=&r->nodes;
    while(*slot && *slot!=n)slot=&(*slot)->next;
    if(*slot)*slot=n->next;
    if(!quiet && n->description.context_drop)n->description.context_drop(n->description.context,0);
    free(n->formats);
    n->formats=0;
    if(!r->nodes && r->owner_live){r->owner_live=0;
        if(r->reference.target_id)ScDropUnregister(r->window,r->generation);
        if(r->endpoint)ScIpcClose(r->endpoint);
        r->endpoint=0;
        while(r->peers){UiDropPeer *p=r->peers;
            r->peers=p->next;
            p->next=0;
            ui_drop_peer_close(p);
        }
        ui_object_release(&r->object);
    }
    ui_object_release(&r->object);
}
static ScStatus ui_drop_root_register(UiDropRoot *r,UiDropNode *candidate,UiDropNode *replace)
{
    ScU32 formats[64],count=0,actions=0;
    UiDropNode *n=r->nodes;
    for(ScU32 pass=0;pass<2;pass++){
        while(n){UiNode *node=ui_node(r->context,n->node),*window=ui_node(r->context,r->root);
            if(n!=replace && (n->active || n==candidate) && !n->object.closing && node && window && window->window->visible
                && (node->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED)){
                actions|=n->description.actions;
                for(ScU32 i=0;i<n->description.format_count;i++){
                    ScU32 format=n->formats[i],j;
                    for(j=0;j<count && formats[j]!=format;j++){}
                    if(j==count){if(count==64)return ui_cause(r->context,SC_UI_E_RANGE,UI_OP_DATA,r->root);
                        formats[count++]=format;
                    }
                }
            }
            n=n->next;
        }
        n=pass==0?candidate:0;
    }
    if(!candidate && !replace && count==r->format_count && actions==r->actions && !memcmp(formats,r->formats,count*4))return 0;
    if(!count){if(r->reference.target_id)ScDropUnregister(r->window,r->generation);
        r->reference.target_id=0;
        r->format_count=r->actions=0;
        return 0;
    }
    ScDropTargetDesc desc;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.window=r->window;
    desc.generation=r->generation;
    desc.endpoint=r->endpoint;
    desc.actions=actions;
    desc.formats=formats;
    desc.format_count=count;
    ScDropTargetRef ref={sizeof(ref),1,0,0};
    ScStatus s=ScDropRegister(&desc,&ref);
    if(s==0){r->reference=ref;
        r->format_count=count;
        r->actions=actions;
        memcpy(r->formats,formats,count*4);
    }
    return ui_cause(r->context,s,UI_OP_DATA,r->root);
}
ScStatus ScUiDropTargetRegister(ScUiContext *context,ScUiNode id,const ScUiDropTargetDesc *desc)
{
    UiNode *node=ui_node(context,id),*root=ui_root(context,id);
    ScStatus s=ui_input(desc,sizeof(*desc));
    UiDropRoot *r;
    UiDropNode *candidate;
    void *memory=0;
    int fresh=0;
    if(s<0)return s;
    if(!node || !root || !root->window)return SC_UI_E_STALE;
    if(!desc->procedure || !desc->actions || desc->actions&~3u || desc->flags || !desc->formats || !desc->format_count || desc->format_count>64 || !ui_data_reserved(desc->reserved,3))return SC_UI_E_INVALID;
    for(ScU32 i=0;i<desc->format_count;i++){if(!desc->formats[i])return SC_UI_E_INVALID;
        for(ScU32 j=0;j<i;j++)if(desc->formats[i]==desc->formats[j])return SC_UI_E_INVALID;
    }
    r=ui_drop_root_find(context,root->id);
    if(!r){s=ui_object_new(sizeof(*r),UI_DROP_ROOT_OBJECT,ui_drop_root_dispose,&memory);
        if(s<0)return s;
        r=(UiDropRoot *)memory;
        r->context=context;
        r->root=root->id;
        r->window=root->window->window;
        r->generation=root->window->information.generation;
        r->owner_live=1;
        fresh=1;
        ScIpcEndpointDesc endpoint;
        char name[64];
        snprintf(name,sizeof(name),"SCUI.DROP.%u.%u",r->window,ui_drag_nonce++);
        memset(&endpoint,0,sizeof(endpoint));
        endpoint.size=sizeof(endpoint);
        endpoint.version=1;
        endpoint.name=name;
        endpoint.queue_budget_bytes=262144;
        endpoint.max_frame_bytes=65536;
        endpoint.pending_budget_bytes=65536;
        /* 默认ACL绑定本人SID/subject UID/GID/realm；显式SESSION要求另给
         * allowed_session，不能把未填写的0当授权，也不放开PUBLIC跨域。 */
        endpoint.acl_flags=0;
        s=ScIpcCreate(&endpoint,&r->endpoint);
        if(s==0)s=ScIpcListen(r->endpoint);
        if(s<0){
            /* 清理会更新SC32 LastError，先保存真实失败的cause/domain/raw。 */
            s=ui_cause(context,s,UI_OP_DATA,id);
            ui_object_release(&r->object);
            return s;
        }
        r->next=ui_drop_roots;
        ui_drop_roots=r;
    }
    s=ui_object_new(sizeof(*candidate),UI_DROP_NODE_OBJECT,ui_drop_node_dispose,&memory);
    if(s<0){s=ui_cause(context,s,UI_OP_DATA,id);
        if(fresh)ui_object_release(&r->object);
        return s;
    }
    candidate=(UiDropNode *)memory;
    candidate->root=r;
    ui_object_retain(&r->object,UI_DROP_ROOT_OBJECT);
    candidate->node=id;
    candidate->generation=node->generation;
    candidate->formats=(ScU32 *)malloc(desc->format_count*4);
    if(!candidate->formats){s=SC_UI_E_MEMORY;
        goto failed;
    }
    memcpy(candidate->formats,desc->formats,desc->format_count*4);
    candidate->description=*desc;
    candidate->description.formats=candidate->formats;
    candidate->description.context_drop=0;
    s=ui_pin((const void *)desc->procedure);
    if(s==0)s=ui_pin((const void *)desc->context_drop);
    if(s<0)goto failed;
    UiDropNode *old=ui_drop_node_find(r,id);
    s=ui_drop_root_register(r,candidate,old);
    /* register已映射原底层错误，二次映射UI码会抹去原cause。 */
    if(s<0)goto failed_mapped;
    candidate->description.context_drop=desc->context_drop;
    candidate->active=1;
    candidate->next=r->nodes;
    r->nodes=candidate;
    if(old){old->active=0;
        ui_object_release(&old->object);
    }
    return 0;
    failed:s=ui_cause(context,s,UI_OP_DATA,id);
    failed_mapped:ui_object_release(&candidate->object);
    return s;
}
ScStatus ScUiDropTargetRemove(ScUiContext *context,ScUiNode id)
{
    UiNode *node=ui_node(context,id);
    if(!node)return SC_UI_E_STALE;
    UiDropRoot *r=ui_drop_root_find(context,node->root);
    UiDropNode *n=ui_drop_node_find(r,id);
    if(!n)return SC_UI_E_NOT_FOUND;
    ScStatus s=ui_drop_root_register(r,0,n);
    if(s<0)return s;
    n->active=0;
    ui_object_release(&n->object);
    return 0;
}
static ScStatus ui_drop_callback(UiDropNode *target,ScU32 type,ScUiDragData *data,ScI32 x,ScI32 y)
{
    UiNode *node=ui_node(target->root->context,target->node);
    if(!target->active || !node || node->generation!=target->generation)return SC_UI_E_STALE;
    if((node->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))!=(SC_UI_VISIBLE|SC_UI_ENABLED))return SC_UI_E_CANCELLED;
    ScStatus s=ui_object_enter(&target->object,UI_DROP_NODE_OBJECT);
    if(s<0)return s;
    ui_node_hold(node);
    ScUiEvent event;
    ui_event_record(&event,node,type);
    event.flags=SC_UI_EVENT_PROGRAMMATIC|SC_UI_EVENT_CANCELABLE;
    event.x=x-node->rectangle.x;
    event.y=y-node->rectangle.y;
    event.data=data;
    event.bytes=sizeof(*data);
    const ScUiEvent *previous_drop_event=ui_drop_current_event;ui_drop_current_event=&event;
    s=target->description.procedure(node->owner,&event,target->description.context);
    ui_drop_current_event=previous_drop_event;
    if(s==SC_UI_VETO)s=SC_UI_E_CANCELLED;
    else if(s>0)s=0;
    if(s==0 && (node->private_flags&UI_NODE_RETIRED || target->object.closing || !target->active))s=SC_UI_E_STALE;
    ui_object_leave(&target->object);
    ui_node_leave(node);
    return s;
}
static UiDropNode *ui_drop_hit(UiDropRoot *root,ScI32 x,ScI32 y)
{
    ScUiNode id=0;
    if(ScUiHitTest(root->context,root->root,x,y,&id)<0)return 0;
    UiNode *node=ui_node(root->context,id);
    while(node){UiDropNode *target=ui_drop_node_find(root,node->id);
        if(target && (node->effective&(SC_UI_VISIBLE|SC_UI_ENABLED))==(SC_UI_VISIBLE|SC_UI_ENABLED))return target;
        node=node->parent;
    }
    return 0;
}
static ScStatus ui_drop_receive(ScIpcChannel channel,unsigned char **out,ScIpcPacket *packet)
{
    memset(packet,0,sizeof(*packet));
    packet->size=sizeof(*packet);
    packet->version=1;
    ScStatus s=ScIpcPeek(channel,packet);
    if(s<0)return s;
    if(packet->type!=SC32_IPC_FRAME_DATA || packet->bytes<sizeof(ScDragEnvelope) || packet->bytes>65536)return SC_UI_E_INVALID;
    unsigned char *frame=(unsigned char *)malloc(packet->bytes);
    if(!frame)return SC_UI_E_MEMORY;
    s=ScIpcReceive(channel,frame,packet->bytes,packet);
    if(s<0){free(frame);
        return s;
    }
    if(!ui_drop_envelope((const ScDragEnvelope *)frame,packet->bytes)){free(frame);
        return SC_UI_E_INVALID;
    }
    *out=frame;
    return 0;
}

static int ui_drop_same_round(const ScDragEnvelope *a,const ScDragEnvelope *b)
{
    return a->nonce_lo==b->nonce_lo && a->nonce_hi==b->nonce_hi && a->source_window==b->source_window && a->source_generation==b->source_generation
    && a->target_id==b->target_id && a->target_generation==b->target_generation && a->ticket_lo==b->ticket_lo && a->ticket_hi==b->ticket_hi;
}
static ScStatus ui_drop_peer_step(UiDropPeer *p)
{
    if(p->deferred)return ui_d2_peer_step(p);
    ScStatus s;
    ScWideCounter now;
    ui_now(&now);
    if(p->envelope.ticket_lo || p->envelope.ticket_hi){ScDropValidateRequest request=ui_drop_validation(&p->envelope);
        ScDropInfo current;
        memset(&current,0,sizeof(current));
        current.size=sizeof(current);
        current.version=1;
        s=ScDropValidate(&request,&current);
        if(s<0)return s;
        p->deadline.lo=current.expires_lo;
        p->deadline.hi=current.expires_hi;
    }
    if(ui_wide_compare(&now,&p->deadline)>=0)return SC_UI_E_CANCELLED;
    if(p->phase==SC32_DRAG_ACCEPT || p->phase==SC32_DRAG_COMPLETE || p->phase==SC32_DRAG_REJECT){
        ScDragEnvelope reply=p->envelope;
        reply.type=p->phase;
        reply.format=p->format;
        reply.chosen_action=p->action;
        s=ui_drop_send(p->channel,&reply,0,0);
        if(s==0){if(p->phase==SC32_DRAG_ACCEPT)p->phase=SC32_DRAG_TRANSFER;
            else return SC_UI_END;
        }
        return s;
    }
    unsigned char *frame=0;
    ScIpcPacket packet;
    s=ui_drop_receive(p->channel,&frame,&packet);
    if(s<0)return s;
    ScDragEnvelope *e=(ScDragEnvelope *)frame;
    if(!ui_drop_identity(&packet.sender,&p->sender)){free(frame);
        return SC_UI_E_INVALID;
    }
    if(p->phase==0 || (e->type==SC32_DRAG_OFFER && !p->received)){
        if(e->type!=SC32_DRAG_OFFER || ui_drag_offer_check(frame+sizeof(*e),e->body_bytes,0)<0){free(frame);
            return SC_UI_E_INVALID;
        }
        ScDropValidateRequest request=ui_drop_validation(e);
        memset(&p->information,0,sizeof(p->information));
        p->information.size=sizeof(p->information);
        p->information.version=1;
        s=ScDropValidate(&request,&p->information);
        if(s==0 && (p->information.window!=p->root->window || p->information.generation!=p->root->generation || p->information.target_id!=p->root->reference.target_id
            || p->information.target_generation!=p->root->reference.target_generation))s=SC_UI_E_STALE;
        if(s==0 && (p->sender.pid!=p->information.source_pid || p->sender.generation!=p->information.source_pid_generation
            || p->sender.session!=p->information.identity.session))s=SC_UI_E_INVALID;
        UiDropNode *target=s==0?ui_drop_hit(p->root,p->information.client_x,p->information.client_y):0;
        if(s==0 && !target)s=SC_UI_E_NOT_FOUND;
        ScU32 target_node=target?target->node:0,target_generation=target?target->generation:0;
        if(target)ui_object_retain(&target->object,UI_DROP_NODE_OBJECT);
        const ScDragOffer *offer=(const ScDragOffer *)(frame+sizeof(*e));
        const ScDragFormat *formats=(const ScDragFormat *)(offer+1);
        const ScDragFormat *chosen=0;
        if(s==0)for(ScU32 i=0;!chosen && i<target->description.format_count;i++)for(ScU32 j=0;j<offer->count;j++)if(target->formats[i]==formats[j].format){chosen=formats+j;
            break;
        }
        if(s==0 && !chosen)s=SC_UI_E_NOT_SUPPORTED;
        UiDropNode *old=ui_drop_node_find(p->root,p->node);
        int same=old && target==old;
        if(old && !same){ScUiDragData leave={sizeof(leave),1,SC32_DRAG_CANCEL,p->format,p->information.actions,p->action,0,0,p->sender};
            ui_drop_callback(old,SC_UI_EVENT_DRAG_LEAVE,&leave,p->information.client_x,p->information.client_y);
        }
        if(s==0){free(p->body);
            p->body=0;
            p->received=0;
            p->total=chosen->bytes;
            p->format=chosen->format;
            p->body=(unsigned char *)malloc(p->total?p->total:1);
            if(!p->body)s=SC_UI_E_MEMORY;
        }
        p->envelope=*e;
        p->phase=SC32_DRAG_REJECT;
        if(s==0){ScU32 actions=e->allowed_actions&p->information.actions&target->description.actions;
            ScUiDragData data={sizeof(data),1,SC32_DRAG_OFFER,p->format,actions,(actions&SC_UI_DRAG_COPY)?SC_UI_DRAG_COPY:SC_UI_DRAG_MOVE,e->body_bytes,offer,p->sender};
            if(!actions)s=SC_UI_E_CANCELLED;
            else s=ui_drop_callback(target,same?SC_UI_EVENT_DRAG_OVER:SC_UI_EVENT_DRAG_ENTER,&data,p->information.client_x,p->information.client_y);
            if(s==0 && (data.chosen_action==SC_UI_DRAG_COPY || data.chosen_action==SC_UI_DRAG_MOVE) && (data.chosen_action&actions)){
                p->action=data.chosen_action;p->allowed_actions=actions;
                p->node=target_node;
                p->node_generation=target_generation;
                p->phase=SC32_DRAG_ACCEPT;
            }
            else if(s==0)s=SC_UI_E_INVALID;
        }
        p->envelope.status=s;
        p->deadline.lo=p->information.expires_lo;
        p->deadline.hi=p->information.expires_hi;
        if(!p->deadline.lo && !p->deadline.hi){p->deadline=now;
            ui_wide_add(&p->deadline,500);
        }
        if(target)ui_object_release(&target->object);
        free(frame);
        return 0;
    }
    if(!ui_drop_same_round(e,&p->envelope) || e->format!=p->format || e->chosen_action!=p->action || e->version!=p->envelope.version){free(frame);
        return SC_UI_E_INVALID;
    }
    if(e->type==SC32_DRAG_CANCEL){free(frame);
        return SC_UI_E_CANCELLED;
    }
    if(e->type!=SC32_DRAG_TRANSFER || e->body_bytes>p->total-p->received || (!e->body_bytes && p->total!=p->received)){free(frame);
        return SC_UI_E_INVALID;
    }
    ScDropValidateRequest request=ui_drop_validation(e);
    ScDropInfo current;
    memset(&current,0,sizeof(current));
    current.size=sizeof(current);
    current.version=1;
    s=ScDropValidate(&request,&current);
    UiDropNode *target=s==0?ui_drop_node_find(p->root,p->node):0;
    if(s==0 && (!target || target->generation!=p->node_generation))s=SC_UI_E_STALE;
    if(s==0){if(e->body_bytes)memcpy(p->body+p->received,frame+sizeof(*e),e->body_bytes);
        p->received+=e->body_bytes;
        if(p->received==p->total){
            ScUiDragData data={sizeof(data),1,SC32_DRAG_TRANSFER,p->format,p->envelope.version==2?p->allowed_actions:p->information.actions,p->action,p->total,p->body,p->sender};
            UiDropPeer *previous_peer=ui_drop_current_peer;ui_drop_current_peer=p;
            s=ui_drop_callback(target,SC_UI_EVENT_DROP,&data,current.client_x,current.client_y);
            ui_drop_current_peer=previous_peer;
            if(s==0 && data.chosen_action!=p->action)s=SC_UI_E_INVALID;
            p->envelope.status=s;
            if(s<0&&p->deferred)ui_d2_fail(p,s);
            p->phase=s<0?SC32_DRAG_REJECT:p->deferred?SC32_DRAG_TRANSFER:SC32_DRAG_COMPLETE;
        }
    }
    free(frame);
    return s;
}
static void ui_drag_disconnect(UiDrag *d,int cancel)
{
    if(d->channel && cancel && d->deferred_stage && !d->deferred_notified){
        ScDragEnvelope reply=d->last;reply.type=SC32_DRAG_CANCEL;reply.format=d->format;reply.status=SC_UI_E_CANCELLED;
        reply.chosen_action=0;ui_drop_send(d->channel,&reply,0,0);
    }
    if(d->channel){ScIpcClose(d->channel);
        d->channel=0;
    }
    if(d->target.ticket_lo || d->target.ticket_hi){ScDropToken token=ui_drop_token(&d->target);
        if(cancel)ScDropCancel(&token);
    }
    memset(&d->target,0,sizeof(d->target));
    d->phase=0;
    d->sent=0;
}
static void ui_drag_dispose(UiObject *object,int quiet)
{
    UiDrag *d=(UiDrag *)object;
    (void)quiet;
    UiDrag **slot=&ui_drags;
    while(*slot && *slot!=d)slot=&(*slot)->next;
    if(*slot)*slot=d->next;
    if(d->deferred_opt&&!d->deferred_notified&&!quiet&&!ui_quiet){
        ScUiDropResult result;ui_d2_result(&result,d->deferred_failure<0?d->deferred_failure:SC_UI_E_CANCELLED);
        result.action=d->action;if(d->deferred_stage || d->phase==SC32_DRAG_COMPLETE)result.uncertain=1;
        d->deferred_notified=1;ui_d2_notice(d,SC32_DRAG_CANCEL,&result);
    }
    ui_drag_disconnect(d,1);
    UiNode *source=ui_node(d->context,d->source);
    if(source && d->captured){ScUiCapture(d->context,d->source,0);
        UiNode *root=ui_root(d->context,d->source);
        if(root)ScPointerSetCursor(root->window->window,SC32_CURSOR_ARROW);
    }
    if(d->preview && ui_node(d->context,d->preview))ScUiNodeDestroy(d->context,d->preview);
    ScUiIconRelease(d->icon);
    free(d->offer);
    d->offer=0;
}
static void ui_drag_close(UiDrag *d)
{if(!d->cancelled){d->cancelled=1;
        ui_object_release(&d->object);
    }
}
ScStatus ScUiDragBegin(ScUiContext *context,const ScUiDragDesc *desc,ScU32 *token)
{
    ScStatus s=ui_input(desc,sizeof(*desc));
    UiNode *source,*root;
    UiDrag *d;
    void *candidate=0;
    ScInputSnapshot input;
    if(s<0 || !token)return s<0?s:SC_UI_E_INVALID;
    if(!desc->actions || desc->actions&~3u || desc->flags&~SC_UI_DRAG_DEFERRED || !ui_data_reserved(desc->reserved,4))return SC_UI_E_INVALID;
    source=ui_node(context,desc->source);
    root=ui_root(context,desc->source);
    if(!source || !root || !root->window)return SC_UI_E_STALE;
    for(UiDrag *drag=ui_drags;drag;drag=drag->next)if(drag->context==context&&!drag->deferred_stage)return SC_UI_E_BUSY;
    if(ui_drag_nonce==UINT_MAX)return SC_UI_E_RANGE;
    s=ui_drag_offer_check(desc->offer,desc->offer_bytes,1);
    if(s<0)return s;
    memset(&input,0,sizeof(input));
    input.size=sizeof(input);
    input.version=1;
    s=ScInputQuery(root->window->window,&input);
    if(s<0)return ui_cause(context,s,UI_OP_DATA,source->id);
    if(!(input.buttons&1) || !input.focused || !input.visible)return SC_UI_E_CANCELLED;
    s=ui_object_new(sizeof(*d),UI_DRAG_OBJECT,ui_drag_dispose,&candidate);
    if(s<0)return s;
    d=(UiDrag *)candidate;
    d->context=context;
    d->source=source->id;
    d->root=source->root;
    d->window=root->window->window;
    d->generation=root->window->information.generation;
    d->epoch=input.input_epoch;
    d->actions=desc->actions;d->deferred_opt=(desc->flags&SC_UI_DRAG_DEFERRED)!=0;
    d->token=d->nonce=ui_drag_nonce++;
    d->offer=(unsigned char *)malloc(desc->offer_bytes);
    if(!d->offer){s=SC_UI_E_MEMORY;
        goto failed;
    }
    memcpy(d->offer,desc->offer,desc->offer_bytes);
    d->bytes=desc->offer_bytes;
    if(desc->preview){s=ScUiIconRetain(desc->preview);
        if(s<0)goto failed;
        d->icon=desc->preview;
    }
    s=ScUiCapture(context,source->id,1);
    if(s<0)goto failed;
    d->captured=1;
    if(d->icon){UiNode *preview=0;
        ScUiRect anchor={input.x-source->rectangle.x,input.y-source->rectangle.y,1,1};
        ScU32 size=(ScU32)ui_scale(context,32);
        s=ui_popup_root(source,SC32_POPUP_TOOLTIP,&anchor,size,size,SC32_POPUP_NO_ACTIVATE|SC32_POPUP_HIT_TRANSPARENT,&preview);
        if(s==0){d->preview=preview->id;
            ScUiWidgetDesc widget;
            ScUiNode image;
            memset(&widget,0,sizeof(widget));
            widget.size=sizeof(widget);
            widget.version=1;
            widget.kind=SC_UI_LABEL;
            widget.parent=preview->id;
            widget.text.size=sizeof(widget.text);
            widget.text.version=1;
            s=ScUiWidgetCreate(context,&widget,&image);
            if(s==0)s=ScUiNodeSetIcon(context,image,d->icon);
            if(s==0)s=ScUiLayout(context,preview->id);
        }
        if(s<0)goto failed;
    }
    d->next=ui_drags;
    ui_drags=d;
    ui_now(&d->deadline);
    ui_wide_add(&d->deadline,3000);
    ScPointerSetCursor(d->window,SC32_CURSOR_DRAG_COPY);
    *token=d->token;
    return 0;
    failed:ui_object_release(&d->object);
    return ui_cause(context,s,UI_OP_DATA,source->id);
}
ScStatus ScUiDragCancel(ScUiContext *context,ScU32 token)
{
    for(UiDrag *d=ui_drags;d;d=d->next)if(d->context==context && d->token==token){ui_drag_close(d);
        return 0;
    }
    return SC_UI_E_NOT_FOUND;
}
static ScStatus ui_drag_point(UiDrag *d,ScI32 x,ScI32 y)
{
    UiNode *root=ui_root(d->context,d->source);
    if(!root || !root->window)return SC_UI_E_STALE;
    ScWindowInfo info;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    ScStatus s=ScWindowQuery(d->window,&info);
    if(s<0)return s;
    ScI32 left=(info.width-(ScI32)info.client_width)/2,top=info.height-(ScI32)info.client_height-left;
    ScDropPoint point={sizeof(point),1,d->window,d->generation,info.x+left+x,info.y+top+y,d->actions,0};
    ScDropInfo target;
    memset(&target,0,sizeof(target));
    target.size=sizeof(target);
    target.version=1;
    s=ScDropAt(&point,&target);
    if(s==SC32_E_STALE){ScInputSnapshot snapshot;
        memset(&snapshot,0,sizeof(snapshot));
        snapshot.size=sizeof(snapshot);
        snapshot.version=1;
        if(ScInputQuery(d->window,&snapshot)==0){point.screen_x=info.x+left+snapshot.x;
            point.screen_y=info.y+top+snapshot.y;
            s=ScDropAt(&point,&target);
        }
    }
    if(d->preview){UiNode *preview=ui_node(d->context,d->preview);
        if(preview && preview->window){ScWindowInfo placed;
            memset(&placed,0,sizeof(placed));
            placed.size=sizeof(placed);
            placed.version=1;
            ScWindowPlace(preview->window->window,point.screen_x+ui_scale(d->context,12),point.screen_y+ui_scale(d->context,12),preview->window->information.width,preview->window->information.height,&placed);
        }
    }
    if(s==SC32_E_NOT_FOUND || s==SC32_E_STALE){ui_drag_disconnect(d,1);
        ScPointerSetCursor(d->window,SC32_CURSOR_FORBIDDEN);
        return 0;
    }
    if(s<0)return s;
    if(d->channel && target.target_id==d->target.target_id && target.target_generation==d->target.target_generation){
        if(target.ticket_lo==d->target.ticket_lo && target.ticket_hi==d->target.ticket_hi){
            if(ui_drag_nonce==UINT_MAX)return SC_UI_E_RANGE;
            d->target=target;
            d->last.nonce_lo=d->nonce=ui_drag_nonce++;
            d->phase=SC32_DRAG_OFFER;
            d->sent=0;
            d->deadline.lo=target.expires_lo;
            d->deadline.hi=target.expires_hi;
            return 0;
        }
    }
    ui_drag_disconnect(d,1);
    d->target=target;
    ScDragEnvelope offer={sizeof(ScDragEnvelope),1,SC32_DRAG_OFFER,d->nonce,0,d->window,d->generation,target.target_id,target.target_generation,target.ticket_lo,target.ticket_hi,0,d->actions,0,0,0};
    offer.version=d->deferred_opt?2:1;
    d->last=offer;
    ScDropValidateRequest request=ui_drop_validation(&d->last);
    s=ScDropConnect(&request,&d->channel);
    if(s<0){ui_drag_disconnect(d,1);
        return s;
    }
    d->phase=SC32_DRAG_OFFER;
    d->deadline.lo=target.expires_lo;
    d->deadline.hi=target.expires_hi;
    return 0;
}
static ScStatus ui_drag_source_step(UiDrag *d)
{
    if(d->deferred_stage)return ui_d2_source_step(d);
    ScWideCounter now;
    ScStatus s=ui_now(&now);
    /* 取时失败不能把未定义值或假零时钟用于判断票据是否过期。 */
    if(s<0)return s;
    if(ui_wide_compare(&now,&d->deadline)>=0)return SC_UI_E_CANCELLED;
    if(!d->channel)return d->up?SC_UI_E_CANCELLED:0;
    ScIpcInfo connection;
    memset(&connection,0,sizeof(connection));
    connection.size=sizeof(connection);
    connection.version=1;
    s=ScIpcQuery(d->channel,&connection);
    if(s<0)return s;
    if(connection.state!=SC32_IPC_STATE_CONNECTED)return SC32_E_AGAIN;
    if(d->phase==SC32_DRAG_OFFER){
        const ScDragOffer *offer=(const ScDragOffer *)d->offer;
        ScU32 table=sizeof(*offer)+offer->count*sizeof(ScDragFormat);
        s=ui_drop_send(d->channel,&d->last,d->offer,table);
        if(s==0)d->phase=SC32_DRAG_ACCEPT;
        return s;
    }
    if(d->phase==SC32_DRAG_TRANSFER){
        if(!d->up)return 0;
        ScU32 take=d->bytes-d->offset-d->sent;
        if(take>65536u-sizeof(ScDragEnvelope))take=65536u-sizeof(ScDragEnvelope);
        ScDragEnvelope e=d->last;
        e.type=SC32_DRAG_TRANSFER;
        e.format=d->format;
        e.chosen_action=d->action;
        s=ui_drop_send(d->channel,&e,d->offer+d->offset+d->sent,take);
        if(s==0){d->sent+=take;
            const ScDragOffer *offer=(const ScDragOffer *)d->offer;
            const ScDragFormat *formats=(const ScDragFormat *)(offer+1);
            for(ScU32 i=0;i<offer->count;i++)if(formats[i].format==d->format && d->sent==formats[i].bytes)d->phase=SC32_DRAG_COMPLETE;
        }
        return s;
    }
    unsigned char *frame=0;
    ScIpcPacket packet;
    s=ui_drop_receive(d->channel,&frame,&packet);
    if(s<0)return s;
    ScDragEnvelope *e=(ScDragEnvelope *)frame;
    if(!ui_drop_identity(&packet.sender,&d->target.identity) || e->body_bytes){free(frame);
        return SC_UI_E_CANCELLED;
    }
    if(e->nonce_lo<d->last.nonce_lo && e->nonce_hi==d->last.nonce_hi && e->ticket_lo==d->last.ticket_lo && e->ticket_hi==d->last.ticket_hi){free(frame);
        return 0;
    }
    if(!ui_drop_same_round(e,&d->last) || e->version!=d->last.version || e->status<0){free(frame);
        return SC_UI_E_CANCELLED;
    }
    if(d->phase==SC32_DRAG_ACCEPT && e->type==SC32_DRAG_ACCEPT){
        if((e->chosen_action!=SC_UI_DRAG_COPY && e->chosen_action!=SC_UI_DRAG_MOVE) || !(e->chosen_action&d->actions&d->target.actions)){free(frame);
            return SC_UI_E_INVALID;
        }
        const ScDragOffer *offer=(const ScDragOffer *)d->offer;
        const ScDragFormat *formats=(const ScDragFormat *)(offer+1);
        int found=0;
        for(ScU32 i=0;i<offer->count;i++)if(formats[i].format==e->format){d->format=e->format;
            d->offset=formats[i].offset;
            d->bytes=formats[i].offset+formats[i].bytes;
            found=1;
            break;
        }
        if(!found){free(frame);
            return SC_UI_E_INVALID;
        }
        d->action=e->chosen_action;
        d->phase=SC32_DRAG_TRANSFER;
        ScPointerSetCursor(d->window,d->action==SC_UI_DRAG_COPY?SC32_CURSOR_DRAG_COPY:SC32_CURSOR_DRAG_MOVE);
    }
    else if(d->deferred_opt && d->phase==SC32_DRAG_COMPLETE && e->version==2 && e->type==UI_D2_HOLD){
        ScU32 allowed=d->actions&d->target.actions;
        if(e->format!=d->format || !e->allowed_actions || (e->allowed_actions&~allowed) || e->chosen_action!=d->action || !(e->allowed_actions&d->action))s=SC_UI_E_INVALID;
        else {ScDropToken ticket=ui_drop_token(&d->target);s=ScDropComplete(&ticket,d->action);
            if(s==0){d->deferred_stage=1;d->deferred_actions=e->allowed_actions;
                d->target.ticket_lo=d->target.ticket_hi=0;
                ScStatus notice=ui_d2_notice(d,SC_UI_DRAG_HANDOFF,0);if(notice<0)d->context->notification_error=notice;
                ui_d2_detach_pointer(d);
            }
        }
    }
    else if(d->phase==SC32_DRAG_COMPLETE && e->type==SC32_DRAG_COMPLETE && e->format==d->format && e->chosen_action==d->action){
        ScDropToken ticket=ui_drop_token(&d->target);
        s=ScDropComplete(&ticket,d->action);
        if(s==0){d->deferred_notified=1;UiNode *source=ui_node(d->context,d->source);
            if(source){ScUiDragData data={sizeof(data),1,SC32_DRAG_COMPLETE,d->format,d->actions,d->action,0,0,packet.sender};
                ScStatus notice=ui_event(source,SC_UI_EVENT_DROP,&data,sizeof(data));
                if(notice<0)d->context->notification_error=notice;
            }
            s=SC_UI_END;
        }
    }
    else s=SC_UI_E_INVALID;
    free(frame);
    return s;
}

static ScStatus ui_drag_pump(ScUiContext *context)
{
    ScStatus enter=ui_object_enter((UiObject *)context,UI_CONTEXT);
    if(enter<0)return enter;
    ScU32 budget=8;
    UiDropRoot *root=ui_drop_roots;
    if(root)ui_object_retain(&root->object,UI_DROP_ROOT_OBJECT);
    while(root){UiDropRoot *next=root->next;
        if(next)ui_object_retain(&next->object,UI_DROP_ROOT_OBJECT);
        if(root->context==context && root->owner_live && root->endpoint){
            if(budget){ScIpcChannel channel=0;
                ScBrokerIdentity identity;
                memset(&identity,0,sizeof(identity));
                identity.size=sizeof(identity);
                identity.version=1;
                ScStatus s=ScIpcAccept(root->endpoint,&channel,&identity);
                if(s==0){UiDropPeer *peer;
                    void *candidate=0;
                    s=ui_object_new(sizeof(*peer),UI_DROP_PEER_OBJECT,ui_drop_peer_dispose,&candidate);
                    if(s==0){peer=(UiDropPeer *)candidate;
                        peer->root=root;
                        ui_object_retain(&root->object,UI_DROP_ROOT_OBJECT);
                        peer->channel=channel;
                        peer->sender=identity;
                        peer->listed=1;
                        ui_now(&peer->deadline);
                        ui_wide_add(&peer->deadline,500);
                        peer->next=root->peers;
                        root->peers=peer;
                    }
                    else ScIpcClose(channel);
                    budget--;
                }
                else if(s!=SC32_E_AGAIN && s!=SC32_E_NOT_FOUND)context->notification_error=ui_cause(context,s,UI_OP_DATA,root->root);
            }
            UiDropPeer *peer=root->peers;
            if(peer)ui_object_retain(&peer->object,UI_DROP_PEER_OBJECT);
            while(peer){UiDropPeer *following=peer->next;
                if(following)ui_object_retain(&following->object,UI_DROP_PEER_OBJECT);
                if(budget && root->owner_live && peer->listed && ui_d2_peer_work(peer)){ScStatus s=ui_drop_peer_step(peer);
                    budget--;
                    if(s==SC_UI_END || (s<0 && s!=SC32_E_AGAIN)){if(s<0)ui_d2_fail(peer,s);ui_drop_peer_close(peer);
                        if(s<0 && s!=SC32_E_BROKEN_PIPE)context->notification_error=ui_cause(context,s,UI_OP_DATA,peer->node);
                    }
                }
                ui_object_release(&peer->object);
                peer=following;
            }
        }
        ui_object_release(&root->object);
        root=next;
    }
    UiDrag *drag=ui_drags;
    if(drag)ui_object_retain(&drag->object,UI_DRAG_OBJECT);
    while(drag){UiDrag *next=drag->next;
        if(next)ui_object_retain(&next->object,UI_DRAG_OBJECT);
        if(drag->context==context && budget && ui_d2_source_work(drag)){ScStatus s=drag->cancelled?SC_UI_E_CANCELLED:ui_drag_source_step(drag);
            budget--;
            if(s==SC_UI_END || (s<0 && s!=SC32_E_AGAIN)){if(s<0)drag->deferred_failure=s;ui_drag_close(drag);
                if(s<0)context->notification_error=ui_cause(context,s,UI_OP_DATA,drag->source);
            }
        }
        ui_object_release(&drag->object);
        drag=next;
    }
    ui_object_leave(&context->object);
    return 0;
}
static ScStatus ui_drag_event(UiNode *root,const ScUiEvent *event)
{
    UiDrag *d;
    for(d=ui_drags;d;d=d->next)if(d->context==root->owner && d->root==root->root&&!d->deferred_stage)break;
    if(!d)return 0;
    if(event->type==SC_UI_EVENT_CAPTURE_LOST || event->type==SC_UI_EVENT_INPUT_SYNC || event->type==SC_UI_EVENT_FOCUS_OUT
        || (event->type==SC_UI_EVENT_KEY_DOWN && event->key==27)){ScUiDragCancel(root->owner,d->token);
        return SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_MOVE){if(d->up)return SC_UI_HANDLED;
        ScStatus s=ui_drag_point(d,event->x,event->y);
        if(s<0)ScUiDragCancel(root->owner,d->token);
        return s<0?ui_cause(root->owner,s,UI_OP_DATA,root->id):SC_UI_HANDLED;
    }
    if(event->type==SC_UI_EVENT_POINTER_UP && event->message && (event->message->data[3]&1u)){
        ScStatus s=ui_drag_point(d,event->x,event->y);
        d->up=1;
        if(s<0 || !d->channel){ScUiDragCancel(root->owner,d->token);
            return s<0?ui_cause(root->owner,s,UI_OP_DATA,root->id):SC_UI_HANDLED;
        }
        return SC_UI_HANDLED;
    }
    return 0;
}
static ScU32 ui_drag_wait(ScUiContext *context,ScU32 timeout)
{
    for(UiDrag *d=ui_drags;d;d=d->next)if(d->context==context){
        if(!d->deferred_stage){if(timeout>1)timeout=1;}
        else {if(ui_d2_source_work(d))return 0;if(d->deferred_stage==1)timeout=ui_d2_timeout(&d->deadline,timeout);}
    }
    for(UiDropRoot *r=ui_drop_roots;r;r=r->next)if(r->context==context)for(UiDropPeer *p=r->peers;p;p=p->next)if(p->listed){
        if(!p->deferred){if(timeout>1)timeout=1;}
        else {if(ui_d2_peer_work(p))return 0;if(p->deferred<3)timeout=ui_d2_timeout(&p->deadline,timeout);}
    }
    return timeout;
}
static void ui_drag_state(UiNode *node)
{
    UiDropRoot *root=ui_drop_root_find(node->owner,node->root);
    if(!root || !root->nodes)return;
    ScStatus s=ui_drop_root_register(root,0,0);
    if(s<0)node->owner->notification_error=s;
}
static void ui_drag_node_destroy(UiNode *node,int quiet)
{
    (void)quiet;
    UiDrag *d=ui_drags;
    while(d){UiDrag *next=d->next;
        if(d->context==node->owner && (d->source==node->id || d->root==node->id))ui_drag_close(d);
        d=next;
    }
    UiDropRoot *root=ui_drop_root_find(node->owner,node->root);
    UiDropNode *target=ui_drop_node_find(root,node->id);
    if(target){ui_object_retain(&root->object,UI_DROP_ROOT_OBJECT);
        if(root->reference.target_id)ScDropUnregister(root->window,root->generation);
        root->reference.target_id=0;
        root->format_count=root->actions=0;
        target->active=0;
        ui_object_release(&target->object);
        if(root->nodes)ui_drop_root_register(root,0,0);
        ui_object_release(&root->object);
    }
}
static void ui_drag_finish(void)
{
    while(ui_drop_receipts){UiDropPeer *p=ui_drop_receipts;ScUiContext *owner=p->root->context;ui_drop_receipts=p->receipt_next;p->receipt_next=0;p->receipt_id=0;ui_object_release(&p->object);ScUiContextRelease(owner);}
    while(ui_drags){UiDrag *d=ui_drags;
        ui_drags=d->next;
        d->next=0;
        ui_drag_close(d);
    }
    while(ui_drop_roots){UiDropRoot *r=ui_drop_roots;
        ui_object_retain(&r->object,UI_DROP_ROOT_OBJECT);
        ui_drop_roots=r->next;
        r->next=0;
        while(r->nodes){UiDropNode *n=r->nodes;
            r->nodes=n->next;
            n->next=0;
            ui_object_release(&n->object);
        }
        ui_object_release(&r->object);
    }
}
