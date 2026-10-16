#include "RDAPP.H"
static ScU32 serial;
void RdRecord(void *p,ScU32 n){
    memset(p,0,n);
    ((ScU32 *)p)[0]=n;
    ((ScU32 *)p)[1]=1;
}
ScUiText RdText(const char *s){
    ScUiText t={
        sizeof(t),1,s,(ScU32)strlen(s)
    };
    return t;
}
RdDoc *RdDocument(ScU32 id){
    for(RdDoc *d=rd.docs;d;d=d->next)if(d->id==id)return d;
    return 0;
}
ScStatus RdNotify(ScUiProvider *provider,ScU32 kind)
{
    ScU32 *counter=provider==rd.tree_provider?&rd.tree_revision:&rd.table_revision;
    if(*counter==UINT_MAX)return SC_UI_E_RANGE;
    ScUiDataChange change;
    RdRecord(&change,sizeof(change));
    change.kind=kind;
    change.revision=++*counter;
    return ScUiProviderNotify(provider,&change);
}
static void rows_free(RdRow *rows,ScU32 count)
{
    for(ScU32 i=0;i<count;i++)ScStringRelease(rows[i].label);
    free(rows);
}
void RdParseCancel(void)
{
    rows_free(rd.parse.rows,rd.parse.count);
    ScStringRelease(rd.parse.source);
    memset(&rd.parse,0,sizeof(rd.parse));
    rd.computing=0;
}
void RdChanged(RdDoc *d)
{
    ScUiDocumentInfo info;
    RdRecord(&info,sizeof(info));
    if(ScUiDocumentQuery(d->document,&info)<0)return;
    d->revision=info.revision;
    if(rd.source_doc==d->id && rd.source_revision!=info.revision){
        rd.stats_valid=0;
        rd.computing=0;
        ScUiInvalidate(rd.ui,rd.chart,0);
    }
    RdRefresh();
}
ScStatus RdParseStart(void)
{
    if(!rd.active)return SC_UI_E_NOT_FOUND;
    RdParseCancel();
    ScUiDocumentInfo info;
    RdRecord(&info,sizeof(info));
    ScStatus status=ScUiDocumentQuery(rd.active->document,&info);
    if(status<0)return status;
    ScString *source=0;
    status=ScUiDocumentCopy(rd.active->document,0,info.bytes,&source);
    if(status<0)return status;
    RdParse *p=&rd.parse;
    p->source=source;
    p->doc=rd.active->id;
    p->revision=info.revision;
    p->line=1;
    RdRecord(&p->view,sizeof(p->view));
    status=ScStringView(source,&p->view);
    if(status<0){
        RdParseCancel();
        return status;
    }
    p->phase=1;
    RdStatus("正在分批读取数据，旧结果保留");
    return 0;
}
static ScStatus integer(const char *s,ScU32 bytes,ScI32 *out)
{
    int negative=0;
    ScU32 at=0,value=0;
    if(at<bytes && (s[at]=='-' || s[at]=='+'))negative=s[at++]=='-';
    if(at==bytes)return SC_UTIL_E_SYNTAX;
    ScU32 limit=negative?0x80000000u:0x7fffffffu;
    for(;at<bytes;at++){
        if(s[at]<'0'||s[at]>'9')return SC_UTIL_E_SYNTAX;
        ScU32 digit=(ScU32)(s[at]-'0');
        if(value>(limit-digit)/10)return SC_UTIL_E_RANGE;
        value=value*10+digit;
    }
    *out=negative?(ScI32)(0u-value):(ScI32)value;
    return 0;
}
static ScStatus parse_line(RdParse *p,ScU32 first,ScU32 last)
{
    const char *source=(const char *)p->view.data;
    if(last>first && source[last-1]=='\r')last--;
    if(last==first || source[first]=='#')return 0;
    ScU32 tab=first;
    while(tab<last && source[tab]!='\t')tab++;
    if(tab==first || tab==last)return SC_UTIL_E_SYNTAX;
    if(tab-first>65536)return SC_UTIL_E_RANGE;
    ScI32 value;
    ScStatus status=integer(source+tab+1,last-tab-1,&value);
    if(status<0)return status;
    ScString *label=0;
    status=ScStringCreate(source+first,tab-first,&label);
    if(status<0)return status;
    if(p->count==p->capacity){
        ScU32 capacity=p->capacity?p->capacity*2:32;
        if(capacity<p->capacity || capacity>UINT_MAX/sizeof(RdRow)){
            ScStringRelease(label);
            return SC_UTIL_E_RANGE;
        }
        RdRow *rows=(RdRow *)realloc(p->rows,capacity*sizeof(RdRow));
        if(!rows){
            ScStringRelease(label);
            return SC_UTIL_E_MEMORY;
        }
        p->rows=rows;
        p->capacity=capacity;
    }
    if(serial==UINT_MAX){
        ScStringRelease(label);
        return SC_UTIL_E_RANGE;
    }
    RdRow row;
    memset(&row,0,sizeof(row));
    row.id=++serial;
    row.line=p->line;
    row.offset=first;
    row.value=value;
    row.label=label;
    p->rows[p->count++]=row;
    return 0;
}
static void reply_init(ScUiDataReply *r)
{
    ScU32 revision=r->revision;
    RdRecord(r,sizeof(*r));
    RdRecord(&r->value,sizeof(r->value));
    r->revision=revision;
    r->state=SC_UI_VISIBLE|SC_UI_ENABLED;
}
ScStatus RdTreeData(void *unused,const ScUiDataRequest *q,ScUiDataReply *r)
{
    (void)unused;
    reply_init(r);
    if(q->operation==SC_UI_DATA_COUNT || q->operation==SC_UI_DATA_CHILD_COUNT){
        if(!q->parent)r->total=1;
        else if(q->parent==1)for(RdDoc *d=rd.docs;d;d=d->next)r->total++;
        return 0;
    }
    if(q->operation==SC_UI_DATA_CHILD_AT){
        if(!q->parent){
            if(q->index)return SC_UI_E_NOT_FOUND;
            r->item=1;
            return 0;
        }
        if(q->parent!=1)return SC_UI_E_NOT_FOUND;
        ScU32 index=0;
        for(RdDoc *d=rd.docs;d;d=d->next)if(index++==q->index){
            r->item=d->id+1;
            r->parent=1;
            return 0;
        }
        return SC_UI_E_NOT_FOUND;
    }
    if(q->operation==SC_UI_DATA_INDEX_OF){
        if(q->item==1)return 0;
        ScU32 index=0;
        for(RdDoc *d=rd.docs;d;d=d->next){
            if(q->item==d->id+1){
                r->total=index;
                r->parent=1;
                return 0;
            }
            index++;
        }
        return SC_UI_E_NOT_FOUND;
    }
    r->item=q->item;
    r->value.type=SC_VALUE_UTF8;
    if(q->item==1){
        r->state|=SC_UI_EXPANDED;
        r->value.data="ResearchDesk";
        r->value.bytes=12;
        return 0;
    }
    RdDoc *d=q->item>1?RdDocument(q->item-1):0;
    if(!d)return SC_UI_E_NOT_FOUND;
    r->parent=1;
    r->value.data=d->label;
    r->value.bytes=(ScU32)strlen(d->label);
    return 0;
}
static int compare_rows(const void *a,const void *b)
{
    const RdRow *left=rd.rows+*(const ScU32 *)a,*right=rd.rows+*(const ScU32 *)b;
    int result=0;
    if(rd.sort_field==0){
        const char *x=0,*y=0;
        ScStringCStr(left->label,&x);
        ScStringCStr(right->label,&y);
        result=strcmp(x,y);
    }
    else if(rd.sort_field==1)result=left->value<right->value?-1:left->value>right->value;
    else result=left->line<right->line?-1:left->line>right->line;
    if(!result)result=left->id<right->id?-1:left->id>right->id;
    return rd.sort_reverse?(result<0?1:result>0?-1:0):result;
}
ScStatus RdTableData(void *unused,const ScUiDataRequest *q,ScUiDataReply *r)
{
    (void)unused;
    reply_init(r);
    if(q->operation==SC_UI_DATA_SORT){
        if(q->field>2)return SC_UI_E_INVALID;
        ScU32 *order=rd.count?(ScU32 *)malloc(rd.count*4):0,*positions=rd.count?(ScU32 *)malloc(rd.count*4):0;
        if(rd.count && (!order || !positions)){
            free(order);
            free(positions);
            return SC_UI_E_MEMORY;
        }
        for(ScU32 i=0;i<rd.count;i++)order[i]=i;
        rd.sort_field=q->field;
        rd.sort_reverse=q->flags&1;
        qsort(order,rd.count,4,compare_rows);
        for(ScU32 i=0;i<rd.count;i++)positions[order[i]]=i;
        free(rd.order);
        free(rd.positions);
        rd.order=order;
        rd.positions=positions;
        ScStatus status=RdNotify(rd.table_provider,SC_UI_DATA_SORTED);
        r->revision=rd.table_revision;
        return status;
    }
    if(q->operation==SC_UI_DATA_COUNT || q->operation==SC_UI_DATA_CHILD_COUNT){
        r->total=q->parent?0:rd.count;
        return 0;
    }
    if(q->operation==SC_UI_DATA_CHILD_AT){
        if(q->parent || q->index>=rd.count)return SC_UI_E_NOT_FOUND;
        r->item=rd.rows[rd.order?rd.order[q->index]:q->index].id;
        return 0;
    }
    if(!rd.count || q->item<rd.rows[0].id)return SC_UI_E_NOT_FOUND;
    ScU32 index=q->item-rd.rows[0].id;
    if(index>=rd.count || rd.rows[index].id!=q->item)return SC_UI_E_NOT_FOUND;
    RdRow *row=rd.rows+index;
    if(q->operation==SC_UI_DATA_INDEX_OF){
        r->total=rd.positions?rd.positions[index]:index;
        return 0;
    }
    r->item=row->id;
    if(q->field==0){
        ScView view;
        RdRecord(&view,sizeof(view));
        ScStatus status=ScStringView(row->label,&view);
        if(status<0)return status;
        r->value.type=SC_VALUE_UTF8;
        r->value.data=view.data;
        r->value.bytes=view.bytes;
    }
    else {
        r->value.type=q->field==1?SC_VALUE_I32:SC_VALUE_U32;
        r->value.lo=q->field==1?(ScU32)row->value:row->line;
    }
    return 0;
}
ScStatus RdPluginLoad(void)
{
    if(rd.computing)return SC_UI_E_BUSY;
    ScLibraryDesc desc;
    RdRecord(&desc,sizeof(desc));
    desc.name="RDSTATS.SCL";
    desc.abi_major=1;
    desc.min_version=1;
    ScLibrary library=0;
    ScStatus status=ScLoadLibrary(&desc,&library);
    if(status<0)return status;
    void *begin=0,*push=0,*finish=0;
    status=ScGetSymbol(library,"RdStatsBegin",SC32_SYMBOL_FUNCTION,1,&begin);
    if(!status)status=ScGetSymbol(library,"RdStatsPush",SC32_SYMBOL_FUNCTION,1,&push);
    if(!status)status=ScGetSymbol(library,"RdStatsFinish",SC32_SYMBOL_FUNCTION,1,&finish);
    if(status<0){
        ScFreeLibrary(library);
        return status;
    }
    if(rd.plugin)ScFreeLibrary(rd.plugin);
    rd.plugin=library;
    rd.begin=(RdStatsBeginProc)begin;
    rd.push=(RdStatsPushProc)push;
    rd.finish=(RdStatsFinishProc)finish;
    RdStatus("插件已连接。更新库文件后请重新打开程序。");
    return 0;
}
ScStatus RdPluginRelease(void)
{
    if(rd.computing)return SC_UI_E_BUSY;
    if(!rd.plugin)return 0;
    ScStatus status=ScFreeLibrary(rd.plugin);
    if(!status){
        rd.plugin=0;
        rd.begin=0;
        rd.push=0;
        rd.finish=0;
        RdStatus("已释放插件引用；已返回的函数地址依运行时规则固定至进程结束。");
    }
    return status;
}
int RdModelStep(void)
{
    RdParse *p=&rd.parse;
    if(p->phase){
        RdDoc *doc=RdDocument(p->doc);
        ScUiDocumentInfo info;
        RdRecord(&info,sizeof(info));
        if(!doc || ScUiDocumentQuery(doc->document,&info)<0 || info.revision!=p->revision){
            RdParseCancel();
            RdStatus("源已修改，本次分析取消；旧结果仍可查看。");
            return 0;
        }
        const char *source=(const char *)p->view.data;
        for(ScU32 budget=64;budget && p->at<p->view.bytes;budget--){
            ScU32 first=p->at,last=first;
            while(last<p->view.bytes && source[last]!='\n')last++;
            ScStatus status=parse_line(p,first,last);
            if(status<0){
                char message[160];
                snprintf(message,sizeof(message),"第%u行不是 标签<TAB>32位整数；当前文档和旧表保留",p->line);
                RdError(status,message);
                RdParseCancel();
                return 0;
            }
            p->at=last<p->view.bytes?last+1:last;
            p->line++;
        }
        if(p->at<p->view.bytes)return 1;
        RdRow *old=rd.rows;
        ScU32 count=rd.count;
        rd.rows=p->rows;
        rd.count=p->count;
        p->rows=0;
        p->count=0;
        free(rd.order);
        free(rd.positions);
        rd.order=rd.positions=0;
        rd.source_doc=p->doc;
        rd.source_revision=p->revision;
        rd.data_revision++;
        rd.stats_valid=0;
        RdParseCancel();
        ScUiSelectionClear(rd.table_selection);
        RdNotify(rd.table_provider,SC_UI_DATA_RESET);
        rows_free(old,count);
        if(!rd.plugin){
            ScStatus status=RdPluginLoad();
            if(status<0){
                RdError(status,"数据已载入，统计插件未连接");
                ScUiInvalidate(rd.ui,rd.chart,0);
                return 0;
            }
        }
        RdRecord(&rd.stats,sizeof(rd.stats));
        if(rd.begin(&rd.stats)<0){
            RdError(SC_UI_E_FAILED,"插件拒绝统计合同");
            return 0;
        }
        rd.stat_at=0;
        rd.stat_revision=rd.data_revision;
        rd.computing=1;
    }
    if(rd.computing){
        ScI32 values[64];
        ScU32 count=0;
        while(count<64 && rd.stat_at<rd.count)values[count++]=rd.rows[rd.stat_at++].value;
        if(rd.push(&rd.stats,values,count)<0){
            rd.computing=0;
            RdError(SC_UI_E_FAILED,"统计插件失败，文档未改变");
            return 0;
        }
        if(rd.stat_at<rd.count)return 1;
        RdStatsResult result;
        RdRecord(&result,sizeof(result));
        ScStatus status=rd.finish(&rd.stats,&result);
        rd.computing=0;
        if(status<0 || result.size!=sizeof(result) || result.version!=1 || result.count!=rd.count
        || result.positive>rd.count || result.negative>rd.count-result.positive || result.zero!=rd.count-result.positive-result.negative || !memchr(result.sum,0,sizeof(result.sum))){
            RdError(status<0?status:SC_UI_E_INVALID,"统计结果无效");
            return 0;
        }
        rd.result=result;
        rd.stats_valid=rd.stat_revision==rd.data_revision;
        printf("RDESK_ANALYSIS doc=%u revision=%u rows=%u sum=%s mean=%d\n",rd.source_doc,rd.source_revision,rd.result.count,rd.result.sum,rd.result.mean);
        ScUiInvalidate(rd.ui,rd.chart,0);
        RdRefresh();
    }
    return rd.parse.phase || rd.computing;
}
void RdModelDispose(void)
{
    RdParseCancel();
    rows_free(rd.rows,rd.count);
    free(rd.order);
    free(rd.positions);
    rd.rows=0;
    rd.count=0;
    RdPluginRelease();
}
