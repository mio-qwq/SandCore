static ScStatus t_copy_range(const ScUiTerminal *m,ScU32 first,ScU32 first_column,ScU32 last,ScU32 last_column,ScString **out)
{
    ScString *text=0;
    ScStatus status=ScStringCreate("",0,&text);
    if(status<0)return status;
    for(ScU32 y=first;y<=last && !status;y++){
        TermRow *r=t_row_at(m,y);
        ScU32 begin=y==first?first_column:0,end=y==last?last_column:r->used;
        if(end>r->used)end=r->used;
        if(begin>end)begin=end;
        if(begin && begin<r->columns && (r->cells[begin].flags&SC_UI_TERM_CONTINUATION))begin--;
        for(ScU32 x=begin;x<end && !status;x++){
            TermCell *c=r->cells+x;
            if(c->flags&SC_UI_TERM_CONTINUATION)continue;
            if(c->marks){
                ScView view;
                t_record(&view,sizeof(view));
                status=ScStringView(c->marks,&view);
                if(!status)status=ScStringAppend(text,view.data,view.bytes);
            }
            else {
                char bytes[4];
                ScU32 count;
                status=ScUtf8Encode(c->scalar?c->scalar:' ',bytes,4,&count);
                if(!status)status=ScStringAppend(text,bytes,count);
            }
        }
        if(!status && y!=last && !r->wrapped)status=ScStringAppend(text,"\n",1);
    }
    if(status<0)ScStringRelease(text);
    else *out=text;
    return status;
}
ScStatus ScUiTerminalSelect(ScUiTerminal *m,const ScUiTerminalSelection *selection)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!selection || selection->size!=sizeof(*selection) || selection->version!=1 || selection->reserved[0] || selection->reserved[1])return SC_UI_E_INVALID;
    if(!selection->anchor_row && !selection->active_row){
        t_selection_clear(m);
        t_damage(m,0,(ScI32)m->screen->rows-1);
        t_flush_damage(m);
        return 0;
    }
    ScU32 first,last;
    if(!t_row_index(m,selection->anchor_row,&first) || !t_row_index(m,selection->active_row,&last))return SC_UI_E_STALE;
    if(selection->anchor_column>t_row_at(m,first)->columns || selection->active_column>t_row_at(m,last)->columns)return SC_UI_E_RANGE;
    m->selection=*selection;
    t_damage(m,0,(ScI32)m->screen->rows-1);
    t_flush_damage(m);
    return 0;
}
static int t_selection_range(const ScUiTerminal *m,ScU32 *first,ScU32 *a,ScU32 *last,ScU32 *b)
{
    if(!m->selection.anchor_row || !t_row_index(m,m->selection.anchor_row,first) || !t_row_index(m,m->selection.active_row,last))return 0;
    *a=m->selection.anchor_column;
    *b=m->selection.active_column;
    if(*first>*last || (*first==*last && *a>*b)){
        ScU32 swap=*first;
        *first=*last;
        *last=swap;
        swap=*a;
        *a=*b;
        *b=swap;
    }
    return *first!=*last || *a!=*b;
}
ScStatus ScUiTerminalCopySelection(const ScUiTerminal *m,ScString **out)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!out)return SC_UI_E_INVALID;
    ScU32 first,a,last,b;
    if(!t_selection_range(m,&first,&a,&last,&b))return SC_UI_E_NOT_FOUND;
    return t_copy_range(m,first,a,last,b,out);
}
ScStatus ScUiTerminalCopyTranscript(const ScUiTerminal *m,ScString **out)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    if(!out)return SC_UI_E_INVALID;
    ScU32 last=t_total(m)-1;
    while(last && !t_row_at(m,last)->used)last--;
    return t_copy_range(m,0,0,last,t_row_at(m,last)->used,out);
}
ScStatus ScUiTerminalCopy(ScUiTerminal *m)
{
    ScString *text=0;
    ScStatus status=ScUiTerminalCopySelection(m,&text);
    if(status<0)return status;
    ScView view;
    t_record(&view,sizeof(view));
    status=ScStringView(text,&view);
    const char *body=0;
    if(!status)status=ScStringCStr(text,&body);
    ScClipboardInfo info;
    t_record(&info,sizeof(info));
    if(!status)status=ScClipboardQuery(&info);
    ScClipboardDesc desc;
    t_record(&desc,sizeof(desc));
    desc.budget_bytes=view.bytes+4097;
    ScClipboardCandidate candidate=0;
    ScU32 epoch=info.epoch;
    if(!status)status=ScClipboardBegin(&desc,&candidate);
    if(!status)status=ScClipboardOffer(candidate,SC32_CLIPBOARD_TEXT,body,view.bytes+1);
    if(!status)status=ScClipboardCommit(candidate,epoch,&info);
    if(status<0 && candidate)ScClipboardAbort(candidate);
    ScStringRelease(text);
    return status;
}
ScStatus ScUiTerminalPaste(ScUiTerminal *m)
{
    if(!t_valid(m))return SC_UI_E_STALE;
    ScClipboardInfo info;
    t_record(&info,sizeof(info));
    ScStatus status=ScClipboardOpen(&info);
    if(status<0)return status;
    ScU32 bytes=0,cursor=0;
    unsigned char *body=0;
    for(;;){
        ScClipboardFormatInfo formats[16];
        ScClipboardPage page;
        t_record(&page,sizeof(page));
        status=ScClipboardFormats(info.handle,cursor,formats,16,&page);
        if(status<0)break;
        for(ScU32 i=0;i<page.count;i++)if(formats[i].format==SC32_CLIPBOARD_TEXT)bytes=formats[i].bytes;
        if(bytes || (page.flags&SC32_CLIPBOARD_FINISHED))break;
        if(page.cursor==cursor){
            status=SC_UI_E_IO;
            break;
        }
        cursor=page.cursor;
    }
    if(!status && (!bytes || bytes>m->input_budget+1))status=bytes?SC_UI_E_RANGE:SC_UI_E_NOT_FOUND;
    if(!status){
        body=(unsigned char *)malloc(bytes);
        if(!body)status=SC_UI_E_MEMORY;
    }
    for(ScU32 at=0;!status && at<bytes;){
        ScBrokerIoResult io;
        t_record(&io,sizeof(io));
        ScU32 take=bytes-at;
        if(take>65536)take=65536;
        status=ScClipboardRead(info.handle,SC32_CLIPBOARD_TEXT,at,body+at,take,&io);
        if(!status && (!io.transferred || io.transferred>take || io.total_bytes!=bytes))status=SC_UI_E_IO;
        if(!status)at+=io.transferred;
    }
    if(!status && body[bytes-1])status=SC_UI_E_UTF8;
    if(!status)status=ScUiTerminalSend(m,body,bytes-1,1);
    ScStatus closed=ScClipboardClose(info.handle);
    free(body);
    return status?status:closed;
}
