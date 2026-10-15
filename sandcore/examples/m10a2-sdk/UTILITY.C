#include <SC32API.H>
#include <SCLIB.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
/* 每一行是一个stable model item。输入累积到完整UTF-8行才提交，
 * 失败删本次空候选；标准FILE负责缓冲，容器不复制stdio算法。 */

typedef struct
{
    ScModel *model;
    ScConfig *config;
    ScU32 count,bytes;
} Utility;

static ScStatus add_line(Utility *u,const void *bytes,ScU32 count)
{
    ScU32 bad=0,id=0;
    ScStatus s=ScUtf8Validate(bytes,count,&bad);
    if(s<0)
    {
        fprintf(stderr,"UTILITY invalid UTF-8 byte=%u\n",bad);
        return s;
    }
    if(u->count==0xffffffffu || count>0xffffffffu-u->bytes)return SC_UTIL_E_RANGE;
    s=ScModelInsert(u->model,0,0,&id);
    if(s==0)
    {
        ScValue value;
        memset(&value,0,sizeof(value));
        value.size=sizeof(value);
        value.version=1;
        value.type=SC_VALUE_UTF8;
        value.data=bytes;
        value.bytes=count;
        s=ScModelSet(u->model,id,"label",&value);
    }
    if(s<0)
    {
        if(id)ScModelRemove(u->model,id);
        return s;
    }
    u->count++;
    u->bytes+=count;
    return 0;
}

static ScStatus read_lines(Utility *u,FILE *file)
{
    ScBuffer *line=0;
    ScStatus s=ScBufferCreate(256,&line);
    char block[4096];
    while(s==0)
    {
        size_t count=fread(block,1,sizeof(block),file);
        for(ScU32 start=0,i=0;s==0 && i<(ScU32)count;i++)
        {
            if(block[i]=='\n')
            {
                s=ScBufferAppend(line,block+start,i-start);
                ScView view;
                memset(&view,0,sizeof(view));
                view.size=sizeof(view);
                view.version=1;
                if(s==0)s=ScBufferView(line,&view);
                if(s==0)
                {
                    ScU32 bytes=view.bytes;
                    if(bytes && ((const char *)view.data)[bytes-1]=='\r')bytes--;
                    s=add_line(u,view.data,bytes);
                }
                if(s==0)s=ScBufferClear(line);
                start=i+1;
            }
            if(i+1==(ScU32)count && start<(ScU32)count && s==0)s=ScBufferAppend(line,block+start,(ScU32)count-start);
        }
        if(count<sizeof(block))
        {
            if(ferror(file))s=SC_UTIL_E_IO;
            break;
        }
    }
    if(s==0)
    {
        ScView view;
        memset(&view,0,sizeof(view));
        view.size=sizeof(view);
        view.version=1;
        s=ScBufferView(line,&view);
        if(s==0 && view.bytes)s=add_line(u,view.data,view.bytes);
    }
    ScBufferRelease(line);
    return s;
}

static ScStatus visit(ScU32 id,const ScModelNodeInfo *info,void *context)
{
    Utility *u=(Utility *)context;
    ScValue value;
    char section[32];
    ScStatus s;
    if(!id)return 0;
    memset(&value,0,sizeof(value));
    value.size=sizeof(value);
    value.version=1;
    s=ScModelGet(u->model,id,"label",&value);
    if(s<0 || value.type!=SC_VALUE_UTF8)return s<0?s:SC_UTIL_E_INVALID;
    if(printf("item %u / revision %u: ",id,info->revision)<0)return SC_UTIL_E_IO;
    if(fwrite(value.data,1,value.bytes,stdout)!=value.bytes || fputc('\n',stdout)==EOF)return SC_UTIL_E_IO;
    snprintf(section,sizeof(section),"item-%u",id);
    return ScConfigSet(u->config,section,"label",&value);
}

int main(void)
{
    Utility u;
    ScStatus s;
    FILE *input=0,*output=0;
    ScString *serialized=0;
    ScView view;
    ScProcessRef self;
    const char *input_path=0,*output_path=0;
    memset(&u,0,sizeof(u));
    memset(&self,0,sizeof(self));
    self.size=sizeof(self);
    self.version=1;
    s=ScProcessSelf(&self);
    if(s==0)s=ScModelCreate(&u.model);
    if(s==0)s=ScConfigCreate(&u.config);
    /* getenv结果可被下次getenv覆盖；先消费输入路径，再查询输出路径。
     * 清errno区分本库的未设置与真实分配/环境查询失败。 */
    if(s==0)
    {
        errno=0;
        input_path=getenv("SDK_INPUT");
        if(!input_path && errno)s=errno==ENOMEM?SC_UTIL_E_MEMORY:SC_UTIL_E_IO;
    }
    if(s==0 && input_path && *input_path)
    {
        input=fopen(input_path,"rb");
        if(!input)s=SC_UTIL_E_IO;
        if(s==0)s=read_lines(&u,input);
        if(input && fclose(input)!=0)s=SC_UTIL_E_IO;
    }
    else if(s==0)
    {
        const char *items[3]=
        {
            "公用SDK","严格UTF-8","真实model与标准stdio"
        };
        for(ScU32 i=0;s==0 && i<3;i++)s=add_line(&u,items[i],(ScU32)strlen(items[i]));
    }
    if(s==0)s=ScModelVisit(u.model,0,visit,&u);
    if(s==0)s=ScConfigSerialize(u.config,SC_CONFIG_SORT_KEYS,&serialized);
    memset(&view,0,sizeof(view));
    view.size=sizeof(view);
    view.version=1;
    if(s==0)s=ScStringView(serialized,&view);
    if(s==0)
    {
        errno=0;
        output_path=getenv("SDK_OUTPUT");
        if(!output_path && errno)s=errno==ENOMEM?SC_UTIL_E_MEMORY:SC_UTIL_E_IO;
    }
    if(s==0 && output_path && *output_path)
    {
        output=fopen(output_path,"wb");
        if(!output)s=SC_UTIL_E_IO;
        if(s==0 && fwrite(view.data,1,view.bytes,output)!=view.bytes)s=SC_UTIL_E_IO;
        if(output && fclose(output)!=0)s=SC_UTIL_E_IO;
    }
    else if(s==0)
    {
        if(fputs("\nCanonical configuration:\n",stdout)==EOF || fwrite(view.data,1,view.bytes,stdout)!=view.bytes)s=SC_UTIL_E_IO;
    }
    if(s==0)
    {
        if(printf("UTILITY pid=%u generation=%u items=%u UTF8-bytes=%u config-bytes=%u\n",self.pid,self.generation,u.count,u.bytes,view.bytes)<0)s=SC_UTIL_E_IO;
        if(fflush(stdout)!=0 || ferror(stdout))s=SC_UTIL_E_IO;
    }
    if(s<0)fprintf(stderr,"UTILITY failed status=%d errno=%d; failed line candidate discarded\n",s,errno);
    ScStringRelease(serialized);
    ScConfigRelease(u.config);
    ScModelRelease(u.model);
    return s<0?1:0;
}
