#include "SCLIBPRIVATE.H"

typedef char sl_pointer_4[(sizeof(void *)==4 && sizeof(size_t)==4)?1:-1];
typedef char sl_view_20[(sizeof(ScView)==20)?1:-1];
typedef char sl_info_32[(sizeof(ScContainerInfo)==32)?1:-1];
static void *sl_object(ScU32 kind,ScU32 bytes,void (*dispose)(SlBase *,int))
{
    if(sl_stopping)return 0;
    SlBase *base=calloc(1,bytes);
    if(!base)return 0;
    base->kind=kind;
    base->references=1;
    base->dispose=dispose;
    base->next=sl_objects;
    if(sl_objects)sl_objects->previous=base;
    sl_objects=base;
    return base;
}
static ScStatus sl_valid(const void *object,ScU32 kind)
{
    if(!object)return SC_UTIL_E_INVALID;
    const SlBase *base=object;
    if(base->kind!=kind || base->closing || sl_stopping)return SC_UTIL_E_STALE;
    return base->mutation?SC_UTIL_E_BUSY:0;
}
static ScStatus sl_enter(void *object,ScU32 kind)
{
    ScStatus status=sl_valid(object,kind);
    if(status)return status;
    SlBase *base=object;
    if(base->depth==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    base->depth++;
    return 0;
}
static void sl_destroy(SlBase *base)
{
    base->closing=1;
    if(!base->cleared){base->cleared=1;
        base->dispose(base,0);}
    if(base->previous)base->previous->next=base->next;
    else sl_objects=base->next;
    if(base->next)base->next->previous=base->previous;
    base->kind=0;
    free(base);
}
static void sl_leave(void *object)
{
    SlBase *base=object;
    base->depth--;
    if(!base->references && !base->depth && !sl_finalizing)sl_destroy(base);
}
static ScStatus sl_retain(void *object,ScU32 kind)
{
    ScStatus status=sl_valid(object,kind);
    if(status)return status;
    SlBase *base=object;
    if(base->references==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    base->references++;
    return 0;
}
static void sl_release(void *object,ScU32 kind)
{
    if(!object)return;
    SlBase *base=object;
    if(base->kind!=kind || !base->references)return;
    if(!--base->references){base->closing=1;
        if(!base->depth && !sl_finalizing)sl_destroy(base);}
}
static ScStatus sl_input(const void *record,ScU32 bytes)
{
    if(!record)return SC_UTIL_E_INVALID;
    const ScU32 *header=record;
    return header[0]==bytes && header[1]==1?0:SC_UTIL_E_INVALID;
}
static ScStatus sl_output(const void *record,ScU32 bytes)
{
    if(!record)return SC_UTIL_E_INVALID;
    const ScU32 *header=record;
    return header[0]>=bytes && header[0]<=65536u && header[1]==1?0:SC_UTIL_E_INVALID;
}
static ScStatus sl_mutation(SlBase *base,ScU32 generation)
{
    if(base->closing || sl_stopping)return SC_UTIL_E_STALE;
    if(base->mutation)return SC_UTIL_E_BUSY;
    return generation==0xFFFFFFFFu?SC_UTIL_E_RANGE:0;
}
static ScStatus sl_identifier(ScU32 *out)
{
    if(sl_token==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    *out=++sl_token;
    return 0;
}
static ScStatus sl_pin(const void *function)
{return function?ScCallbackPin(function):0;}
static ScU32 sl_capacity(ScU32 current,ScU32 wanted,ScU32 width)
{
    if(!width || wanted>0xFFFFFFFFu/width)return 0;
    ScU32 limit=0xFFFFFFFFu/width,capacity=current?current:8;
    if(capacity>limit)capacity=limit;
    while(capacity<wanted){if(capacity>limit/2u){capacity=wanted;
            break;}capacity*=2u;}
    return capacity;
}
static int sl_alias(const void *pointer,ScU32 bytes,const void *base,ScU32 span)
{
    if(!bytes || !span)return 0;
    ScU32 a=(ScU32)pointer,b=(ScU32)base;
    return a<=b?b-a<bytes:a-b<span;
}
static void sl_counter_add(ScWideCounter *counter,ScU32 amount)
{ScU32 old=counter->lo;
    counter->lo+=amount;
    if(counter->lo<old)counter->hi++;}
static void sl_info(ScContainerInfo *out,ScU32 length,ScU32 capacity,ScU32 width,ScU32 generation,ScU32 flags)
{ScContainerInfo value={32,1,length,capacity,width,generation,flags,0};
    memcpy(out,&value,sizeof(value));}
static void sl_view(ScView *out,const void *data,ScU32 bytes,ScU32 generation)
{ScView value={20,1,data,bytes,generation};
    memcpy(out,&value,sizeof(value));}
static ScStatus sl_string_bytes(const char *text,ScU32 *bytes)
{
    if(!text)return SC_UTIL_E_INVALID;
    size_t count=strlen(text);
    if(count==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
    *bytes=count;
    return 0;
}
static ScStatus sl_value_clone(const ScValue *input,ScValue *out)
{
    ScStatus status=sl_input(input,sizeof(*input));
    if(status)return status;
    if(input->flags || input->type>SC_VALUE_BYTES)return SC_UTIL_E_INVALID;
    ScValue value=*input;
    value.data=0;
    if(input->type==SC_VALUE_UTF8 || input->type==SC_VALUE_BYTES){
        if(input->bytes && !input->data)return SC_UTIL_E_INVALID;
        if(input->bytes==0xFFFFFFFFu)return SC_UTIL_E_RANGE;
        if(input->type==SC_VALUE_UTF8){status=ScUtf8Validate(input->data,input->bytes,0);
            if(status)return status;}
        void *fresh=malloc(input->bytes+1u);
        if(!fresh)return SC_UTIL_E_MEMORY;
        if(input->bytes)memcpy(fresh,input->data,input->bytes);
        ((unsigned char *)fresh)[input->bytes]=0;
        value.data=fresh;
    }else if(input->data || input->bytes || (input->type!=SC_VALUE_COUNTER && input->hi)
        || (input->type==SC_VALUE_BOOL && input->lo>1u) || (input->type==SC_VALUE_NULL && input->lo))return SC_UTIL_E_INVALID;
    memcpy(out,&value,sizeof(value));
    return 0;
}
static void sl_value_free(ScValue *value)
{if(value->type==SC_VALUE_UTF8 || value->type==SC_VALUE_BYTES)free((void *)value->data);
    value->data=0;}
void ScLibFinalize(void)
{
    /* 两阶段静默回收先清内容，再清全部header。Stream/Model可引用
     * 更早创建的Buffer/Hash；若逐对象强制free会让其它owner的引用
     * 悬空。这里所有header保留到内容阶段结束，且不进入用户回调。 */
    sl_stopping=sl_finalizing=1;
    for(SlBase *base=sl_objects;base;base=base->next){
        base->closing=1;
        if(!base->cleared){base->cleared=1;
            base->dispose(base,1);}
    }
    while(sl_objects){SlBase *next=sl_objects->next;
        sl_objects->kind=0;
        free(sl_objects);
        sl_objects=next;}
    sl_finalizing=0;
}
