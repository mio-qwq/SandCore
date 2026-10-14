#include "SCLIBPRIVATE.H"

typedef struct {char *section,*key;
    ScU32 section_bytes,key_bytes;
    ScValue value;} SlConfigItem;
struct ScConfig {SlBase base;
    ScHash *items;
    ScU32 generation;};
static void sl_config_item_drop(void *data,void *context)
{SlConfigItem *item=data;
    (void)context;
    free(item->section);
    free(item->key);
    sl_value_free(&item->value);}
static void sl_config_dispose(SlBase *base,int quiet)
{ScConfig *config=(ScConfig *)base;
    (void)quiet;
    if(config->items)ScHashRelease(config->items);
    config->items=0;}
ScStatus ScConfigCreate(ScConfig **out)
{
    if(!out)return SC_UTIL_E_INVALID;
    ScConfig *config=sl_object(SL_CONFIG,sizeof(*config),sl_config_dispose);
    if(!config)return SC_UTIL_E_MEMORY;
    ScHashDesc desc={32,1,sizeof(SlConfigItem),0,sl_config_item_drop,0,{0,0}};
    ScStatus status=ScHashCreate(&desc,&config->items);
    if(status){ScConfigRelease(config);
        return status;}
    config->items->trusted_drop=1;
    config->generation=1;
    *out=config;
    return 0;
}
ScStatus ScConfigRetain(ScConfig *config){return sl_retain(config,SL_CONFIG);}
void ScConfigRelease(ScConfig *config){sl_release(config,SL_CONFIG);}
static ScStatus sl_config_name(const char *name,int empty,ScU32 *bytes)
{
    ScStatus status=sl_string_bytes(name,bytes);
    if(status)return status;
    if(!empty && !*bytes)return SC_UTIL_E_SYNTAX;
    status=ScUtf8Validate(name,*bytes,0);
    if(status)return status;
    if(*bytes && (name[0]==' ' || name[0]=='\t' || name[*bytes-1u]==' ' || name[*bytes-1u]=='\t'))return SC_UTIL_E_SYNTAX;
    for(ScU32 i=0;i<*bytes;i++){
        unsigned char ch=(unsigned char)name[i];
        if(ch<32u || ch==127u || ch=='[' || ch==']' || ch=='=' || ch=='#' || ch==';' || ch=='"' || ch=='\\')return SC_UTIL_E_SYNTAX;
    }
    return 0;
}
static ScStatus sl_config_key(const char *section,const char *key,unsigned char **combined,ScU32 *bytes,ScU32 *section_bytes,ScU32 *key_bytes)
{
    ScStatus status=sl_config_name(section,1,section_bytes);
    if(!status)status=sl_config_name(key,0,key_bytes);
    if(status)return status;
    if(*section_bytes>0xFFFFFFFEu-*key_bytes)return SC_UTIL_E_RANGE;
    *bytes=*section_bytes+1u+*key_bytes;
    unsigned char *result=malloc(*bytes);
    if(!result)return SC_UTIL_E_MEMORY;
    if(*section_bytes)memcpy(result,section,*section_bytes);
    result[*section_bytes]=0;
    memcpy(result+*section_bytes+1u,key,*key_bytes);
    *combined=result;
    return 0;
}
static ScStatus sl_config_set(ScConfig *config,const char *section,const char *key,const ScValue *value,ScU32 flags)
{
    ScStatus status=sl_valid(config,SL_CONFIG);
    if(status)return status;
    status=sl_mutation(&config->base,config->generation);
    if(status)return status;
    unsigned char *combined;
    ScU32 bytes,sbytes,kbytes;
    status=sl_config_key(section,key,&combined,&bytes,&sbytes,&kbytes);
    if(status)return status;
    SlConfigItem item;
    memset(&item,0,sizeof(item));
    item.section_bytes=sbytes;
    item.key_bytes=kbytes;
    status=sl_value_clone(value,&item.value);
    if(!status){
        item.section=malloc(sbytes+1u);
        item.key=malloc(kbytes+1u);
        if(!item.section || !item.key)status=SC_UTIL_E_MEMORY;
        else{memcpy(item.section,section,sbytes+1u);
            memcpy(item.key,key,kbytes+1u);}
    }
    if(!status)status=ScHashSet(config->items,combined,bytes,&item,flags);
    free(combined);
    if(status){sl_config_item_drop(&item,0);
        return status;}
    config->generation++;
    return 0;
}
ScStatus ScConfigSet(ScConfig *config,const char *section,const char *key,const ScValue *value)
{return sl_config_set(config,section,key,value,0);}
ScStatus ScConfigGet(const ScConfig *config,const char *section,const char *key,ScValue *out)
{
    ScStatus status=sl_valid(config,SL_CONFIG);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    unsigned char *combined;
    ScU32 bytes,sbytes,kbytes;
    status=sl_config_key(section,key,&combined,&bytes,&sbytes,&kbytes);
    if(status)return status;
    SlHashNode *node=sl_hash_find(config->items,combined,bytes,ScHashBytes(combined,bytes));
    free(combined);
    if(!node)return SC_UTIL_E_NOT_FOUND;
    const SlConfigItem *item=(const SlConfigItem *)node->value;
    memcpy(out,&item->value,sizeof(*out));
    return 0;
}
ScStatus ScConfigRemove(ScConfig *config,const char *section,const char *key)
{
    ScStatus status=sl_valid(config,SL_CONFIG);
    if(status)return status;
    status=sl_mutation(&config->base,config->generation);
    if(status)return status;
    unsigned char *combined;
    ScU32 bytes,sbytes,kbytes;
    status=sl_config_key(section,key,&combined,&bytes,&sbytes,&kbytes);
    if(status)return status;
    status=ScHashRemove(config->items,combined,bytes);
    free(combined);
    if(!status)config->generation++;
    return status;
}
ScStatus ScConfigQuery(const ScConfig *config,ScContainerInfo *out)
{
    ScStatus status=sl_valid(config,SL_CONFIG);
    if(!status)status=sl_output(out,sizeof(*out));
    if(status)return status;
    sl_info(out,config->items->count,config->items->capacity/2u,sizeof(ScValue),config->generation,0);
    return 0;
}
static int sl_config_hex(unsigned char ch)
{if(!isxdigit(ch))return -1;
    return ch<='9'?(int)ch-'0':toupper(ch)-'A'+10;}
static ScStatus sl_config_hex4(const unsigned char *text,ScU32 bytes,ScU32 *at,ScU32 *scalar)
{
    if(*at>bytes || bytes-*at<4u)return SC_UTIL_E_SYNTAX;
    ScU32 value=0;
    for(ScU32 i=0;i<4u;i++){int digit=sl_config_hex(text[*at+i]);
        if(digit<0)return SC_UTIL_E_SYNTAX;
        value=(value<<4)|(ScU32)digit;}
    *at+=4u;
    *scalar=value;
    return 0;
}
static ScStatus sl_config_quote(const unsigned char *text,ScU32 bytes,ScString **out,ScU32 *consumed)
{
    if(!bytes || text[0]!='"')return SC_UTIL_E_SYNTAX;
    ScString *string;
    ScStatus status=ScStringCreate(0,0,&string);
    if(status)return status;
    ScU32 at=1,closed=0;
    while(at<bytes && !status){
        unsigned char byte=text[at++];
        if(byte=='"'){closed=1;
            break;}
        if(byte<32u || byte==127u){status=SC_UTIL_E_SYNTAX;
            break;}
        if(byte!='\\'){status=ScStringAppend(string,&byte,1);
            continue;}
        if(at==bytes){status=SC_UTIL_E_SYNTAX;
            break;}
        byte=text[at++];
        if(byte=='"' || byte=='\\')status=ScStringAppend(string,&byte,1);
        else if(byte=='n' || byte=='r' || byte=='t' || byte=='b' || byte=='f'){
            byte=byte=='n'?'\n':byte=='r'?'\r':byte=='t'?'\t':byte=='b'?'\b':'\f';
            status=ScStringAppend(string,&byte,1);
        }else if(byte=='u'){
            ScU32 scalar;
            status=sl_config_hex4(text,bytes,&at,&scalar);
            if(status)break;
            if(scalar>=0xD800u && scalar<=0xDBFFu){
                if(bytes-at<2u || text[at]!='\\' || text[at+1u]!='u'){status=SC_UTIL_E_SYNTAX;
                    break;}
                at+=2u;
                ScU32 low;
                status=sl_config_hex4(text,bytes,&at,&low);
                if(status)break;
                if(low<0xDC00u || low>0xDFFFu){status=SC_UTIL_E_UTF8;
                    break;}
                scalar=0x10000u+((scalar-0xD800u)<<10)+(low-0xDC00u);
            }
            unsigned char encoded[4];
            ScU32 count;
            status=ScUtf8Encode(scalar,encoded,sizeof(encoded),&count);
            if(!status)status=ScStringAppend(string,encoded,count);
        }else status=SC_UTIL_E_SYNTAX;
    }
    if(!status && !closed)status=SC_UTIL_E_SYNTAX;
    if(status){ScStringRelease(string);
        return status;}
    *consumed=at;
    *out=string;
    return 0;
}
static int sl_config_space(unsigned char byte){return byte==' ' || byte=='\t';}
static ScStatus sl_config_value(const unsigned char *text,ScU32 bytes,ScValue *out)
{
    ScValue value;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    ScString *string=0;
    ScU32 consumed=bytes;
    ScStatus status=0;
    if(bytes && text[0]=='"'){
        status=sl_config_quote(text,bytes,&string,&consumed);
        if(!status){value.type=SC_VALUE_UTF8;
            value.data=string->data;
            value.bytes=string->bytes;}
    }else if(bytes>=4u && !memcmp(text,"hex\"",4)){
        ScU32 end=4;
        while(end<bytes && text[end]!='"')end++;
        if(end==bytes || ((end-4u)&1u))return SC_UTIL_E_SYNTAX;
        ScU32 count=(end-4u)/2u;
        unsigned char *data=malloc(count+1u);
        if(!data)return SC_UTIL_E_MEMORY;
        for(ScU32 i=0;i<count;i++){
            int high=sl_config_hex(text[4u+i*2u]),low=sl_config_hex(text[5u+i*2u]);
            if(high<0 || low<0){free(data);
                return SC_UTIL_E_SYNTAX;}data[i]=(unsigned char)((high<<4)|low);
        }
        data[count]=0;
        value.type=SC_VALUE_BYTES;
        value.data=data;
        value.bytes=count;
        consumed=end+1u;
    }else{
        ScU32 end=0;
        while(end<bytes && text[end]!='#' && text[end]!=';')end++;
        consumed=end;
        while(end && sl_config_space(text[end-1u]))end--;
        char *token=malloc(end+1u);
        if(!token)return SC_UTIL_E_MEMORY;
        if(end)memcpy(token,text,end);
        token[end]=0;
        if(!strcmp(token,"null"))value.type=SC_VALUE_NULL;
        else if(!strcmp(token,"true") || !strcmp(token,"false")){value.type=SC_VALUE_BOOL;
            value.lo=token[0]=='t';}
        else if(end>=10u && !memcmp(token,"counter(",8) && token[end-1u]==')'){
            char *next;
            errno=0;
            unsigned long low=strtoul(token+8,&next,16);
            if(next==token+8 || *next!=',' || token[8]=='-' || token[8]=='+' || sl_config_space((unsigned char)token[8]) || errno==ERANGE)status=SC_UTIL_E_SYNTAX;
            else{
                char *start=next+1;
                errno=0;
                unsigned long high=strtoul(start,&next,16);
                if(next==start || next!=token+end-1u || *start=='-' || *start=='+' || sl_config_space((unsigned char)*start) || errno==ERANGE)status=SC_UTIL_E_SYNTAX;
                else{value.type=SC_VALUE_COUNTER;
                    value.lo=(ScU32)low;
                    value.hi=(ScU32)high;}
            }
        }else if(end && (isdigit((unsigned char)token[0]) || token[0]=='-' || token[0]=='+')){
            char *next;
            errno=0;
            if(token[end-1u]=='u'){
                token[end-1u]=0;
                unsigned long number=strtoul(token,&next,10);
                if(next==token || *next || token[0]=='-' || errno==ERANGE)status=SC_UTIL_E_RANGE;
                else{value.type=SC_VALUE_U32;
                    value.lo=(ScU32)number;}
            }else{
                long number=strtol(token,&next,10);
                if(next==token || *next || errno==ERANGE)status=SC_UTIL_E_RANGE;
                else{value.type=SC_VALUE_I32;
                    value.lo=(ScU32)number;}
            }
        }else{value.type=SC_VALUE_UTF8;
            value.data=token;
            value.bytes=end;
            token=0;}
        free(token);
    }
    if(!status){
        while(consumed<bytes && sl_config_space(text[consumed]))consumed++;
        if(consumed<bytes && text[consumed]!='#' && text[consumed]!=';')status=SC_UTIL_E_SYNTAX;
    }
    if(string){
        if(!status){ScValue cloned;
            status=sl_value_clone(&value,&cloned);
            if(!status)value=cloned;}
        ScStringRelease(string);
    }
    if(status){if(!string)sl_value_free(&value);
        return status;}
    memcpy(out,&value,sizeof(value));
    return 0;
}
ScStatus ScConfigParse(ScConfig *config,const void *utf8,ScU32 bytes,ScU32 flags,ScParseIssue *issue)
{
    ScStatus status=sl_valid(config,SL_CONFIG);
    if(status)return status;
    status=sl_mutation(&config->base,config->generation);
    if(status)return status;
    if(flags&~SC_CONFIG_REPLACE_DUPLICATES)return SC_UTIL_E_INVALID;
    if(issue){status=sl_output(issue,sizeof(*issue));
        if(status)return status;}
    ScU32 bad=0;
    status=ScUtf8Validate(utf8,bytes,&bad);
    if(!status && bytes){
        const void *nul=memchr(utf8,0,bytes);
        if(nul){bad=(ScU32)nul-(ScU32)utf8;
            status=SC_UTIL_E_SYNTAX;}
    }
    ScParseIssue result={32,1,bad,1,1,status,{0,0}};
    if(status){
        const unsigned char *prefix=utf8;
        for(ScU32 i=0;i<bad;i++){if(prefix[i]=='\n'){result.line++;
                result.column=1;}else result.column++;}
        if(issue)memcpy(issue,&result,sizeof(result));
        return status;
    }
    const unsigned char *text=utf8;
    ScConfig *candidate;
    status=ScConfigCreate(&candidate);
    if(status)return status;
    char *section=malloc(1);
    if(!section){ScConfigRelease(candidate);
        return SC_UTIL_E_MEMORY;}section[0]=0;
    ScU32 at=0,line=1;
    while(at<bytes && !status){
        ScU32 start=at;
        while(at<bytes && text[at]!='\n')at++;
        ScU32 end=at;
        if(end>start && text[end-1u]=='\r')end--;
        ScU32 first=start;
        while(first<end && sl_config_space(text[first]))first++;
        while(end>first && sl_config_space(text[end-1u]))end--;
        result.byte_offset=first;
        result.line=line;
        result.column=first-start+1u;
        if(first<end && text[first]!='#' && text[first]!=';'){
            if(text[first]=='['){
                ScU32 close=first+1u;
                while(close<end && text[close]!=']')close++;
                if(close==end)status=SC_UTIL_E_SYNTAX;
                else{
                    ScU32 tail=close+1u;
                    while(tail<end && sl_config_space(text[tail]))tail++;
                    if(tail<end && text[tail]!='#' && text[tail]!=';')status=SC_UTIL_E_SYNTAX;
                    ScU32 name=first+1u,last=close;
                    while(name<last && sl_config_space(text[name]))name++;
                    while(last>name && sl_config_space(text[last-1u]))last--;
                    char *fresh=malloc(last-name+1u);
                    if(!fresh)status=SC_UTIL_E_MEMORY;
                    else{
                        memcpy(fresh,text+name,last-name);
                        fresh[last-name]=0;
                        ScU32 length;
                        if(!status)status=sl_config_name(fresh,1,&length);
                        if(status)free(fresh);
                        else{free(section);
                            section=fresh;}
                    }
                }
            }else{
                ScU32 equal=first;
                while(equal<end && text[equal]!='=')equal++;
                if(equal==end)status=SC_UTIL_E_SYNTAX;
                else{
                    ScU32 key_end=equal;
                    while(key_end>first && sl_config_space(text[key_end-1u]))key_end--;
                    char *key=malloc(key_end-first+1u);
                    if(!key)status=SC_UTIL_E_MEMORY;
                    else{
                        memcpy(key,text+first,key_end-first);
                        key[key_end-first]=0;
                        ScU32 value_start=equal+1u;
                        while(value_start<end && sl_config_space(text[value_start]))value_start++;
                        ScValue value;
                        memset(&value,0,sizeof(value));
                        status=sl_config_value(text+value_start,end-value_start,&value);
                        if(!status)status=sl_config_set(candidate,section,key,&value,(flags&SC_CONFIG_REPLACE_DUPLICATES)?0u:SC_HASH_INSERT_ONLY);
                        sl_value_free(&value);
                        free(key);
                    }
                }
            }
        }
        if(at<bytes)at++;
        if(line==0xFFFFFFFFu && at<bytes)status=SC_UTIL_E_RANGE;
        else line++;
    }
    free(section);
    if(!status){ScHash *old=config->items;
        config->items=candidate->items;
        candidate->items=old;
        config->generation++;}
    result.code=status;
    if(!status)result.byte_offset=result.line=result.column=0;
    if(issue)memcpy(issue,&result,sizeof(result));
    ScConfigRelease(candidate);
    return status;
}
static int sl_config_compare(const void *a,const void *b)
{
    const SlConfigItem *left=*(const SlConfigItem *const *)a,*right=*(const SlConfigItem *const *)b;
    int section=strcmp(left->section,right->section);
    return section?section:strcmp(left->key,right->key);
}
static ScStatus sl_config_serialize_value(ScString *text,const ScValue *value)
{
    if(value->type==SC_VALUE_NULL)return ScStringAppend(text,"null",4);
    if(value->type==SC_VALUE_BOOL)return ScStringAppend(text,value->lo?"true":"false",value->lo?4u:5u);
    if(value->type==SC_VALUE_I32)return ScStringAppendFormat(text,"%d",(ScI32)value->lo);
    if(value->type==SC_VALUE_U32)return ScStringAppendFormat(text,"%uu",value->lo);
    if(value->type==SC_VALUE_COUNTER)return ScStringAppendFormat(text,"counter(%08x,%08x)",value->lo,value->hi);
    ScString *escaped;
    ScStatus status=value->type==SC_VALUE_BYTES?ScFormatHex(value->data,value->bytes,0,&escaped):ScFormatEscape(value->data,value->bytes,&escaped);
    if(status)return status;
    status=ScStringAppend(text,value->type==SC_VALUE_BYTES?"hex\"":"\"",value->type==SC_VALUE_BYTES?4u:1u);
    if(!status)status=ScStringAppend(text,escaped->data,escaped->bytes);
    if(!status)status=ScStringAppend(text,"\"",1);
    ScStringRelease(escaped);
    return status;
}
ScStatus ScConfigSerialize(const ScConfig *config,ScU32 flags,ScString **out)
{
    ScStatus status=sl_valid(config,SL_CONFIG);
    if(status || !out)return status?status:SC_UTIL_E_INVALID;
    if(flags&~SC_CONFIG_SORT_KEYS)return SC_UTIL_E_INVALID;
    ScU32 count=config->items->count;
    if(count>0xFFFFFFFFu/sizeof(SlConfigItem *))return SC_UTIL_E_RANGE;
    SlConfigItem **order=count?malloc(count*sizeof(*order)):0;
    if(count && !order)return SC_UTIL_E_MEMORY;
    ScU32 index=0;
    for(SlHashNode *p=config->items->first;p;p=p->next)order[index++]=(SlConfigItem *)p->value;
    if(flags&SC_CONFIG_SORT_KEYS)qsort(order,count,sizeof(*order),sl_config_compare);
    ScString *text;
    status=ScStringCreate(0,0,&text);
    if(status){free(order);
        return status;}
    const char *section="";
    for(ScU32 i=0;i<count && !status;i++){
        SlConfigItem *item=order[i];
        if(strcmp(section,item->section)){
            if(text->bytes)status=ScStringAppend(text,"\n",1);
            if(!status)status=ScStringAppendFormat(text,"[%s]\n",item->section);
            section=item->section;
        }
        if(!status)status=ScStringAppendFormat(text,"%s=",item->key);
        if(!status)status=sl_config_serialize_value(text,&item->value);
        if(!status)status=ScStringAppend(text,"\n",1);
    }
    free(order);
    if(status){ScStringRelease(text);
        return status;}*out=text;
    return 0;
}
