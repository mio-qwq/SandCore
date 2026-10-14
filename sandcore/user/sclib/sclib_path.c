#include "SCLIBPRIVATE.H"

typedef struct {ScU32 output,parent;} SlPathLevel;
static int sl_separator(unsigned char value,ScU32 flags)
{return value=='/' || ((flags&SC_PATH_BACKSLASH) && value=='\\');}
ScStatus ScPathNormalize(const char *path,const char *base,ScU32 flags,ScString **out)
{
    if(!out || !path || (flags&~7u))return SC_UTIL_E_INVALID;
    ScU32 length;
    ScStatus status=sl_string_bytes(path,&length);
    if(status)return status;
    status=ScUtf8Validate(path,length,0);
    if(status)return status;
    ScU32 base_bytes=0;
    if(base && !sl_separator((unsigned char)path[0],flags)){
        status=sl_string_bytes(base,&base_bytes);
        if(status)return status;
        status=ScUtf8Validate(base,base_bytes,0);
        if(status)return status;
    }
    ScU32 separator=base_bytes!=0;
    if(base_bytes>0xFFFFFFFDu-separator || length>0xFFFFFFFDu-separator-base_bytes)return SC_UTIL_E_RANGE;
    ScU32 count=length+base_bytes+separator;
    unsigned char *joined=malloc(count+2u),*fresh=malloc(count+2u);
    if(!joined || !fresh){free(joined);
        free(fresh);
        return SC_UTIL_E_MEMORY;}
    if(base_bytes){memcpy(joined,base,base_bytes);
        joined[base_bytes]='/';}
    memcpy(joined+base_bytes+(base_bytes!=0),path,length+1u);
    ScU32 rooted=sl_separator(joined[0],flags),at=0,used=rooted?1u:0u,levels=0,capacity=0;
    SlPathLevel *stack=0;
    if(rooted)fresh[0]='/';
    if((flags&SC_PATH_REQUIRE_ABSOLUTE) && !rooted)status=SC_UTIL_E_INVALID;
    while(at<count && !status){
        while(at<count && sl_separator(joined[at],flags))at++;
        ScU32 start=at;
        while(at<count && !sl_separator(joined[at],flags))at++;
        ScU32 bytes=at-start;
        if(!bytes || (bytes==1u && joined[start]=='.'))continue;
        ScU32 parent=bytes==2u && joined[start]=='.' && joined[start+1u]=='.';
        if(parent && levels && !stack[levels-1u].parent){used=stack[--levels].output;
            continue;}
        if(parent && rooted){if(flags&SC_PATH_REJECT_ABOVE_ROOT)status=SC_UTIL_E_RANGE;
            continue;}
        if(levels==capacity){
            ScU32 next=sl_capacity(capacity,levels+1u,sizeof(*stack));
            if(!next){status=SC_UTIL_E_RANGE;
                break;}
            SlPathLevel *grown=realloc(stack,next*sizeof(*stack));
            if(!grown){status=SC_UTIL_E_MEMORY;
                break;}
            stack=grown;
            capacity=next;
        }
        stack[levels].output=used;
        stack[levels].parent=parent;
        levels++;
        if(used && fresh[used-1u]!='/')fresh[used++]='/';
        memcpy(fresh+used,joined+start,bytes);
        used+=bytes;
    }
    if(!status){if(!used)fresh[used++]='.';
        fresh[used]=0;
        status=ScStringCreate(fresh,used,out);}
    free(stack);
    free(joined);
    free(fresh);
    return status;
}
ScStatus ScPathJoin(const char *directory,const char *leaf,ScU32 flags,ScString **out)
{if(!directory || !leaf)return SC_UTIL_E_INVALID;
    return ScPathNormalize(leaf,directory,flags,out);}
ScStatus ScPathSplit(const char *path,ScPathParts *out)
{
    ScStatus status=sl_output(out,sizeof(*out));
    if(status)return status;
    ScU32 bytes;
    status=sl_string_bytes(path,&bytes);
    if(status)return status;
    status=ScUtf8Validate(path,bytes,0);
    if(status)return status;
    ScU32 name=0;
    for(ScU32 i=0;i<bytes;i++){if(path[i]=='/')name=i+1u;}
    ScU32 extension=bytes;
    if(bytes-name>1u){for(ScU32 i=name+1u;i<bytes;i++){if(path[i]=='.')extension=i;}}
    if((bytes-name==1u && path[name]=='.') || (bytes-name==2u && path[name]=='.' && path[name+1u]=='.'))extension=bytes;
    ScPathParts value={32,1,bytes && path[0]=='/',name,name,bytes-name,extension,bytes-extension};
    memcpy(out,&value,sizeof(value));
    return 0;
}
ScStatus ScPathRelative(const char *base,const char *path,ScU32 flags,ScString **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScString *left,*right;
    ScStatus status=ScPathNormalize(base,0,flags|SC_PATH_REQUIRE_ABSOLUTE,&left);
    if(status)return status;
    status=ScPathNormalize(path,0,flags|SC_PATH_REQUIRE_ABSOLUTE,&right);
    if(status){ScStringRelease(left);
        return status;}
    ScU32 a=1,b=1,common_a=1,common_b=1;
    while(a<left->bytes && b<right->bytes){
        ScU32 end_a=a,end_b=b;
        while(end_a<left->bytes && left->data[end_a]!='/')end_a++;
        while(end_b<right->bytes && right->data[end_b]!='/')end_b++;
        if(end_a-a!=end_b-b || memcmp(left->data+a,right->data+b,end_a-a))break;
        common_a=end_a<left->bytes?end_a+1u:end_a;
        common_b=end_b<right->bytes?end_b+1u:end_b;
        a=common_a;
        b=common_b;
    }
    ScString *relative;
    status=ScStringCreate(0,0,&relative);
    if(!status){
        a=common_a;
        while(a<left->bytes && !status){status=ScStringAppend(relative,"../",3);
            while(a<left->bytes && left->data[a]!='/')a++;
            if(a<left->bytes)a++;}
        if(!status && common_b<right->bytes)status=ScStringAppend(relative,right->data+common_b,right->bytes-common_b);
        if(!status && relative->bytes && relative->data[relative->bytes-1u]=='/')status=ScStringErase(relative,relative->bytes-1u,1);
        if(!status && !relative->bytes)status=ScStringAppend(relative,".",1);
        if(status)ScStringRelease(relative);
        else *out=relative;
    }
    ScStringRelease(left);
    ScStringRelease(right);
    return status;
}
