#include "SCLIBPRIVATE.H"

static void sl_array_dispose(SlBase *base,int quiet)
{
    ScArray *array=(ScArray *)base;
    if(array->drop && (!quiet || array->trusted_drop)){
        for(ScU32 i=0;i<array->count;i++)array->drop(array->data+i*array->width,array->context);
    }
    free(array->data);
    array->data=0;
    array->count=array->capacity=0;
}
ScStatus ScArrayCreate(const ScArrayDesc *desc,ScArray **out)
{
    ScStatus status=sl_input(desc,sizeof(*desc));
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(!desc->item_bytes || desc->initial_capacity>0xFFFFFFFFu/desc->item_bytes || desc->reserved[0] || desc->reserved[1])return SC_UTIL_E_RANGE;
    ScArray *array=sl_object(SL_ARRAY,sizeof(*array),sl_array_dispose);
    if(!array)return SC_UTIL_E_MEMORY;
    array->width=desc->item_bytes;
    array->generation=1;
    if(desc->initial_capacity){array->data=malloc(desc->initial_capacity*desc->item_bytes);
        if(!array->data){ScArrayRelease(array);
            return SC_UTIL_E_MEMORY;}}
    status=sl_pin((const void *)desc->drop);
    if(status){ScArrayRelease(array);
        return status;}
    array->capacity=desc->initial_capacity;
    array->drop=desc->drop;
    array->context=desc->context;
    *out=array;
    return 0;
}
ScStatus ScArrayRetain(ScArray *array){return sl_retain(array,SL_ARRAY);}
void ScArrayRelease(ScArray *array){sl_release(array,SL_ARRAY);}
ScStatus ScArrayQuery(const ScArray *array,ScContainerInfo *out)
{
    ScStatus status=sl_valid(array,SL_ARRAY);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,array->count,array->capacity,array->width,array->generation,0);
    return 0;
}
ScStatus ScArrayReserve(ScArray *array,ScU32 capacity)
{
    ScStatus status=sl_valid(array,SL_ARRAY);
    if(status)return status;
    status=sl_mutation(&array->base,array->generation);
    if(status)return status;
    if(capacity<=array->capacity)return 0;
    if(capacity>0xFFFFFFFFu/array->width)return SC_UTIL_E_RANGE;
    unsigned char *fresh=realloc(array->data,capacity*array->width);
    if(!fresh)return SC_UTIL_E_MEMORY;
    array->data=fresh;
    array->capacity=capacity;
    array->generation++;
    return 0;
}
ScStatus ScArrayInsert(ScArray *array,ScU32 index,const void *items,ScU32 count)
{
    ScStatus status=sl_valid(array,SL_ARRAY);
    if(status)return status;
    status=sl_mutation(&array->base,array->generation);
    if(status)return status;
    if(index>array->count || (count && !items))return SC_UTIL_E_INVALID;
    if(!count)return 0;
    if(count>0xFFFFFFFFu-array->count || array->count+count>0xFFFFFFFFu/array->width)return SC_UTIL_E_RANGE;
    ScU32 bytes=count*array->width;
    void *saved=0;
    const void *source=items;
    if(sl_alias(items,bytes,array->data,array->count*array->width)){
        saved=malloc(bytes);
        if(!saved)return SC_UTIL_E_MEMORY;
        memcpy(saved,items,bytes);
        source=saved;
    }
    ScU32 wanted=array->count+count;
    if(wanted>array->capacity){
        ScU32 capacity=sl_capacity(array->capacity,wanted,array->width);
        unsigned char *fresh=realloc(array->data,capacity*array->width);
        if(!fresh){free(saved);
            return SC_UTIL_E_MEMORY;}
        array->data=fresh;
        array->capacity=capacity;
    }
    memmove(array->data+(index+count)*array->width,array->data+index*array->width,(array->count-index)*array->width);
    memcpy(array->data+index*array->width,source,bytes);
    free(saved);
    array->count=wanted;
    array->generation++;
    return 0;
}
ScStatus ScArrayErase(ScArray *array,ScU32 index,ScU32 count)
{
    ScStatus status=sl_enter(array,SL_ARRAY);
    if(status)return status;
    status=sl_mutation(&array->base,array->generation);
    if(!status && (index>array->count || count>array->count-index))status=SC_UTIL_E_RANGE;
    if(status || !count){sl_leave(array);
        return status;}
    array->base.mutation=1;
    if(array->drop){for(ScU32 i=0;i<count;i++)array->drop(array->data+(index+i)*array->width,array->context);}
    memmove(array->data+index*array->width,array->data+(index+count)*array->width,(array->count-index-count)*array->width);
    array->count-=count;
    array->generation++;
    array->base.mutation=0;
    sl_leave(array);
    return 0;
}
ScStatus ScArrayResize(ScArray *array,ScU32 count)
{
    ScStatus status=sl_valid(array,SL_ARRAY);
    if(status)return status;
    if(count<array->count)return ScArrayErase(array,count,array->count-count);
    status=sl_mutation(&array->base,array->generation);
    if(status || count==array->count)return status;
    if(count>0xFFFFFFFFu/array->width)return SC_UTIL_E_RANGE;
    if(count>array->capacity){
        ScU32 capacity=sl_capacity(array->capacity,count,array->width);
        unsigned char *fresh=realloc(array->data,capacity*array->width);
        if(!fresh)return SC_UTIL_E_MEMORY;
        array->data=fresh;
        array->capacity=capacity;
    }
    memset(array->data+array->count*array->width,0,(count-array->count)*array->width);
    array->count=count;
    array->generation++;
    return 0;
}
ScStatus ScArrayView(const ScArray *array,ScView *out)
{
    ScStatus status=sl_valid(array,SL_ARRAY);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_view(out,array->data,array->count*array->width,array->generation);
    return 0;
}
ScStatus ScArrayGet(const ScArray *array,ScU32 index,void *out)
{
    ScStatus status=sl_valid(array,SL_ARRAY);
    if(status)return status;
    if(!out || index>=array->count)return SC_UTIL_E_RANGE;
    memmove(out,array->data+index*array->width,array->width);
    return 0;
}
ScStatus ScArraySet(ScArray *array,ScU32 index,const void *item)
{
    ScStatus status=sl_enter(array,SL_ARRAY);
    if(status)return status;
    status=sl_mutation(&array->base,array->generation);
    if(!status && (!item || index>=array->count))status=SC_UTIL_E_RANGE;
    if(status){sl_leave(array);
        return status;}
    unsigned char *target=array->data+index*array->width;
    if(item==target){sl_leave(array);
        return 0;}
    if(!array->drop){memmove(target,item,array->width);
        array->generation++;
        sl_leave(array);
        return 0;}
    unsigned char *fresh=malloc(array->width),*old=array->drop?malloc(array->width):0;
    if(!fresh || (array->drop && !old)){free(fresh);
        free(old);
        sl_leave(array);
        return SC_UTIL_E_MEMORY;}
    memcpy(fresh,item,array->width);
    if(old)memcpy(old,target,array->width);
    memcpy(target,fresh,array->width);
    array->generation++;
    array->base.mutation=1;
    if(old)array->drop(old,array->context);
    array->base.mutation=0;
    free(fresh);
    free(old);
    sl_leave(array);
    return 0;
}
ScStatus ScArrayClear(ScArray *array)
{ScStatus status=sl_valid(array,SL_ARRAY);
    return status?status:ScArrayErase(array,0,array->count);}
