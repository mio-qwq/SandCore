#include "SCCLIBPRIVATE.H"
static void __scc_swap(unsigned char *left,unsigned char *right,size_t width)
{
    unsigned char temporary[64];
    while(width){
        size_t count=width<sizeof(temporary)?width:sizeof(temporary);
        memcpy(temporary,left,count);
        memcpy(left,right,count);
        memcpy(right,temporary,count);
        left+=count;
        right+=count;
        width-=count;
    }
}
static void __scc_sift(unsigned char *base,size_t root,size_t count,size_t width,int (*compare)(const void *,const void *))
{
    /* 子下标只在root<count/2时形成，乘二不会u32溢出；无递归/分配。 */
    while(root<count/2u){
        size_t child=root*2u+1u;
        if(child>=count)break;
        if(child+1u<count && compare(base+child*width,base+(child+1u)*width)<0)child++;
        if(compare(base+root*width,base+child*width)>=0)break;
        __scc_swap(base+root*width,base+child*width,width);
        root=child;
    }
}
void qsort(void *base,size_t count,size_t width,int (*compare)(const void *,const void *))
{
    if(count<2 || !width)return;
    if(!base || !compare){
        errno=EINVAL;
        return;
    }
    if(count>SIZE_MAX/width){
        errno=EOVERFLOW;
        return;
    }
    unsigned char *bytes=base;
    for(size_t root=count/2u;root;root--)__scc_sift(bytes,root-1u,count,width,compare);
    for(size_t remaining=count;remaining>1u;remaining--){
        __scc_swap(bytes,bytes+(remaining-1u)*width,width);
        __scc_sift(bytes,0,remaining-1u,width,compare);
    }
}
void *bsearch(const void *key,const void *base,size_t count,size_t width,int (*compare)(const void *,const void *))
{
    if(!count)return NULL;
    if(!width || !key || !base || !compare){
        errno=EINVAL;
        return NULL;
    }
    if(count>SIZE_MAX/width){
        errno=EOVERFLOW;
        return NULL;
    }
    const unsigned char *bytes=base;
    size_t begin=0,end=count;
    while(begin<end){
        size_t middle=begin+(end-begin)/2u;
        const void *element=bytes+middle*width;
        int result=compare(key,element);
        if(!result)return (void *)element;
        if(result<0)end=middle;
        else begin=middle+1u;
    }
    return NULL;
}
