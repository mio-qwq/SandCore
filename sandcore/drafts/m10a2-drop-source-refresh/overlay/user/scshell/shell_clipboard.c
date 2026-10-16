static ScStatus sh_clip_offer(ScU32 format,const void *data,ScU32 bytes,ScU32 expected,ScClipboardInfo *out) {
    ScClipboardDesc desc;
    ScClipboardCandidate candidate=0;
    ScStatus s;
    if(sh_record(out,sizeof(*out))<0)return SC_SHELL_E_INVALID;
    memset(&desc,0,sizeof(desc));
    desc.size=sizeof(desc);
    desc.version=1;
    desc.budget_bytes=bytes<4096?4096:bytes;
    s=ScClipboardBegin(&desc,&candidate);
    if(s>=0)s=ScClipboardOffer(candidate,format,data,bytes);
    if(s>=0) {
        s=ScClipboardCommit(candidate,expected,out);
        if(s>=0)candidate=0;
    }
    if(candidate)ScClipboardAbort(candidate);
    return sh_cause(s,80,0);
}
ScStatus ScShellClipboardText(const ScUiText *text,ScU32 epoch,ScClipboardInfo *out) {
    ScStatus s=sh_record(text,sizeof(*text));
    if(s<0)return s;
    if(text->bytes&&(!text->data||memchr(text->data,0,text->bytes)))return SC_SHELL_E_INVALID;
    s=ScUtf8Validate(text->data,text->bytes,0);
    if(s<0)return SC_SHELL_E_UTF8;
    return sh_clip_offer(SC32_CLIPBOARD_TEXT,text->data,text->bytes,epoch,out);
}
static ScStatus sh_clip_read(ScU32 format,ScBuffer **out,ScU32 *epoch) {
    ScClipboardInfo info;
    ScClipboardFormatInfo rows[16];
    ScClipboardPage page;
    ScBuffer *buffer=0;
    ScU32 after=0,total=0,found=0,offset=0;
    ScStatus s;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScClipboardOpen(&info);
    if(s<0)return sh_cause(s,81,0);
    for(;;) {
        ScU32 i;
        memset(&page,0,sizeof(page));
        page.size=sizeof(page);
        page.version=1;
        s=ScClipboardFormats(info.handle,after,rows,16,&page);
        if(s<0)goto done;
        for(i=0;i<page.count;++i)if(rows[i].format==format) {
            total=rows[i].bytes;
            found=1;
            break;
        }
        if(found||page.flags&SC32_CLIPBOARD_FINISHED)break;
        if(page.cursor==after) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        after=page.cursor;
    }
    if(!found) {
        s=SC_SHELL_E_NOT_FOUND;
        goto done;
    }
    s=ScBufferCreate(total,&buffer);
    if(s>=0)s=ScBufferResize(buffer,total);
    if(s<0)goto done;
    while(offset<total) {
        ScBrokerIoResult io;
        const unsigned char *p;
        ScU32 n,take=total-offset;
        if(take>65536)take=65536;
        s=sh_buffer_data(buffer,&p,&n);
        if(s<0)goto done;
        memset(&io,0,sizeof(io));
        io.size=sizeof(io);
        io.version=1;
        s=ScClipboardRead(info.handle,format,offset,(void *)(p+offset),take,&io);
        if(s<0)goto done;
        if(!io.transferred||io.transferred>take||io.total_bytes!=total) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        offset+=io.transferred;
    }
    *out=buffer;
    buffer=0;
    if(epoch)*epoch=info.epoch;
    done:ScClipboardClose(info.handle);
    ScBufferRelease(buffer);
    return sh_cause(s,81,0);
}
ScStatus ScShellClipboardReadText(ScString **out,ScU32 *epoch) {
    ScBuffer *b=0;
    ScString *str=0;
    const unsigned char *p;
    ScU32 n,e;
    ScStatus s;
    if(!out||!epoch)return SC_SHELL_E_INVALID;
    s=sh_clip_read(SC32_CLIPBOARD_TEXT,&b,&e);
    if(s>=0)s=sh_buffer_data(b,&p,&n);
    if(s>=0&&(memchr(p,0,n)||ScUtf8Validate(p,n,0)<0))s=SC_SHELL_E_UTF8;
    if(s>=0)s=ScStringCreate(p,n,&str);
    ScBufferRelease(b);
    if(s<0)return sh_cause(s,81,0);
    *out=str;
    *epoch=e;
    return 0;
}
static ScStatus sh_clip_files_encode(const ScShellFileItem *items,ScU32 count,ScU32 operation,ScBuffer **out) {
    ScShellFileItem *owned=0;
    ScBuffer *b=0;
    ScClipboardFiles header;
    ScStatus s;
    ScU32 i;
    s=sh_file_items(items,count,&owned);
    if(s<0)return s;
    memset(&header,0,sizeof(header));
    header.size=sizeof(header);
    header.version=1;
    header.count=count;
    header.operation=operation;
    s=ScBufferCreate(sizeof(header),&b);
    if(s>=0)s=ScBufferAppend(b,&header,sizeof(header));
    if(s<0)goto done;
    for(i=0;i<count;++i) {
        ScPathInfo info;
        ScClipboardFileRow row;
        ScU32 length,z=0;
        if(!owned[i].source) {
            s=SC_SHELL_E_INVALID;
            goto done;
        }
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScPathQuery(owned[i].source,&info);
        if(s<0)goto done;
        if(info.kind!=owned[i].kind|| ((owned[i].flags&SC_SHELL_ITEM_EXPECT_SOURCE)&&info.generation!=owned[i].expected_source_revision)) {
            s=SC_SHELL_E_STALE;
            goto done;
        }
        length=(ScU32)strlen(owned[i].source)+1;
        memset(&row,0,sizeof(row));
        row.record_bytes=sizeof(row)+((length+3)&~3u);
        row.path_bytes=length;
        row.revision=info.generation;
        /* pin观察不能跨进程持久化；这里只记录当前目录revision。 */ s=ScBufferAppend(b,&row,sizeof(row));
        if(s>=0)s=ScBufferAppend(b,owned[i].source,length);
        if(s>=0&&((4-(length&3))&3))s=ScBufferAppend(b,&z,(4-(length&3))&3);
        if(s<0)goto done;
    }
    *out=b;
    b=0;
    done:sh_file_items_free(owned,count);
    ScBufferRelease(b);
    return sh_cause(s,82,0);
}
static ScStatus sh_clip_files_decode(const void *bytes,ScU32 count,ScShellFileItem **out,ScU32 *item_count,ScU32 *operation) {
    const unsigned char *p=(const unsigned char *)bytes;
    ScClipboardFiles header;
    ScShellFileItem *items=0;
    ScU32 pos,i;
    ScStatus s;
    if(!p||count<sizeof(header))return SC_SHELL_E_PROTOCOL;
    memcpy(&header,p,sizeof(header));
    if(header.size!=sizeof(header)||header.version!=1||!header.count||header.count>(count-sizeof(header))/(sizeof(ScClipboardFileRow)+4)|| header.count>0xFFFFFFFFu/sizeof(*items)||(header.operation!=SC32_CLIPBOARD_COPY&&header.operation!=SC32_CLIPBOARD_CUT))return SC_SHELL_E_PROTOCOL;
    items=(ScShellFileItem *)calloc(header.count,sizeof(*items));
    if(!items)return SC_SHELL_E_MEMORY;
    pos=sizeof(header);
    for(i=0;i<header.count;++i) {
        ScClipboardFileRow row;
        ScPathInfo info;
        const char *path;
        ScU32 cursor=sizeof(row);
        if(pos>count||count-pos<sizeof(row)) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        memcpy(&row,p+pos,sizeof(row));
        if(row.record_bytes>count-pos||row.record_bytes<sizeof(row)||!row.revision||row.flags&~SC32_CLIPBOARD_PIN_OBSERVATION|| ((row.flags&SC32_CLIPBOARD_PIN_OBSERVATION)?!row.object_id:row.object_id!=0)) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        s=sh_decode_string(p+pos,row.record_bytes,&cursor,sizeof(row),row.path_bytes,64,&path);
        if(s<0)goto fail;
        if(cursor!=row.record_bytes||path[0]!='/') {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        items[i].size=sizeof(*items);
        items[i].version=1;
        items[i].item=i+1;
        items[i].flags=SC_SHELL_ITEM_EXPECT_SOURCE;
        items[i].expected_source_revision=row.revision;
        s=sh_absolute(path,(char **)&items[i].source);
        if(s<0)goto fail;
        if(strcmp(items[i].source,path)) {
            s=SC_SHELL_E_PROTOCOL;
            goto fail;
        }
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScPathQuery(path,&info);
        if(s<0)goto fail;
        items[i].kind=info.kind;
        /* 较旧revision交给worker产生真实冲突，不把同名新对象默认为旧源。 */ pos+=row.record_bytes;
    }
    if(pos!=count) {
        s=SC_SHELL_E_PROTOCOL;
        goto fail;
    }
    *out=items;
    *item_count=header.count;
    *operation=header.operation;
    return 0;
    fail:sh_file_items_free(items,header.count);
    return sh_cause(s,83,i+1);
}
static ScStatus sh_clip_publish_files(const ScShellFileItem *items,ScU32 count,ScU32 operation,ScU32 epoch,ScClipboardInfo *out) {
    ScBuffer *b=0;
    const unsigned char *p;
    ScU32 n;
    ScStatus s=sh_clip_files_encode(items,count,operation,&b);
    if(s>=0)s=sh_buffer_data(b,&p,&n);
    if(s>=0)s=sh_clip_offer(SC32_CLIPBOARD_FILES,p,n,epoch,out);
    ScBufferRelease(b);
    return s;
}
ScStatus ScShellClipboardCopyFiles(const ScShellFileItem *items,ScU32 count,ScU32 epoch,ScClipboardInfo *out) {
    return sh_clip_publish_files(items,count,SC32_CLIPBOARD_COPY,epoch,out);
}
ScStatus ScShellClipboardCutFiles(const ScShellFileItem *items,ScU32 count,ScU32 epoch,ScClipboardInfo *out) {
    return sh_clip_publish_files(items,count,SC32_CLIPBOARD_CUT,epoch,out);
}
ScStatus ScShellClipboardPaste(const char *destination,const char *recovery,ScShellEventProc procedure,void *context,ScShellFileJob **out) {
    ScBuffer *b=0;
    const unsigned char *p;
    ScU32 bytes=0,epoch=0,count=0,operation=0;
    ScShellFileItem *items=0;
    ScShellFileJobDesc d;
    ScShellFileJob *job=0;
    ScStatus s;
    if(!out)return SC_SHELL_E_INVALID;
    s=sh_clip_read(SC32_CLIPBOARD_FILES,&b,&epoch);
    if(s>=0)s=sh_buffer_data(b,&p,&bytes);
    if(s>=0)s=sh_clip_files_decode(p,bytes,&items,&count,&operation);
    if(s<0)goto done;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.operation=operation==SC32_CLIPBOARD_CUT?SC_SHELL_MOVE:SC_SHELL_COPY;
    d.flags=SC_SHELL_RECURSIVE;
    d.items=items;
    d.count=count;
    d.destination=destination;
    d.recovery_path=recovery;
    d.final_mode=3;
    d.procedure=procedure;
    d.context=context;
    s=ScShellFileOperationBegin(&d,&job);
    if(s<0)goto done;
    job->clip=b;
    b=0;
    job->clip_epoch=epoch;
    job->clip_operation=operation;
    *out=job;
    done:sh_file_items_free(items,count);
    ScBufferRelease(b);
    return s;
}
static int sh_cut_complete(const ScShellFileJob *job,const char *path) {
    ScContainerInfo info;
    ScU32 i;
    ScStatus s;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScArrayQuery(job->results,&info);
    if(s<0)return 0;
    for(i=0;i<info.length;++i) {
        ScShellFileResult r;
        if(ScArrayGet(job->results,i,&r)<0)return 0;
        if(!strcmp(r.source,path)&&(r.flags&SC_SHELL_DURABLE)&&r.state==SC_SHELL_RESULT_DONE&&r.status>=0&& (r.stage==SC_SHELL_STAGE_RENAME||r.stage==SC_SHELL_STAGE_SOURCE_REMOVED))return 1;
    }
    return 0;
}
ScStatus ScShellClipboardReconcile(ScShellFileJob *job,ScClipboardInfo *out) {
    const unsigned char *p;
    ScU32 bytes,pos,count=0,i;
    ScClipboardFiles h;
    ScBuffer *remaining=0;
    ScStatus s;
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    if(sh_record(out,sizeof(*out))<0)return SC_SHELL_E_INVALID;
    if(!job->clip||job->clip_operation!=SC32_CLIPBOARD_CUT)return SC_SHELL_E_INVALID;
    if(!job->closed)return SC_SHELL_PENDING;
    s=sh_buffer_data(job->clip,&p,&bytes);
    if(s<0)return s;
    if(bytes<sizeof(h))return SC_SHELL_E_PROTOCOL;
    memcpy(&h,p,sizeof(h));
    pos=sizeof(h);
    s=ScBufferCreate(bytes,&remaining);
    if(s>=0)s=ScBufferResize(remaining,sizeof(h));
    if(s<0)goto done;
    for(i=0;i<h.count;++i) {
        ScClipboardFileRow row;
        if(pos>bytes||bytes-pos<sizeof(row)) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        memcpy(&row,p+pos,sizeof(row));
        if(row.record_bytes>bytes-pos||row.path_bytes>row.record_bytes-sizeof(row)) {
            s=SC_SHELL_E_PROTOCOL;
            goto done;
        }
        if(!sh_cut_complete(job,(const char *)(p+pos+sizeof(row)))) {
            s=ScBufferAppend(remaining,p+pos,row.record_bytes);
            if(s<0)goto done;
            ++count;
        }
        pos+=row.record_bytes;
    }
    if(count==h.count) {
        s=ScClipboardQuery(out);
        if(s>=0&&out->epoch!=job->clip_epoch)s=SC_SHELL_E_STALE;
    } else if(!count)s=ScClipboardClear(job->clip_epoch,out);
    else {
        const unsigned char *data;
        ScU32 n;
        h.count=count;
        s=ScBufferWrite(remaining,0,&h,sizeof(h));
        if(s>=0)s=sh_buffer_data(remaining,&data,&n);
        if(s>=0)s=sh_clip_offer(SC32_CLIPBOARD_FILES,data,n,job->clip_epoch,out);
    }
    if(s>=0)job->clip_operation=0;
    done:ScBufferRelease(remaining);
    return sh_cause(s,84,0);
}
static ScStatus sh_drag_begin(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 flags,ScU32 *token) {
    ScBuffer *files=0,*offer=0;
    ScDragOffer header;
    ScDragFormat row;
    ScUiDragDesc d;
    const unsigned char *p;
    ScU32 bytes,n;
    ScStatus s;
    if(!actions||actions&~3u||flags&~SC_SHELL_DRAG_DEFERRED||!token)return SC_SHELL_E_INVALID;
    s=sh_clip_files_encode(items,count,SC32_CLIPBOARD_COPY,&files);
    if(s>=0)s=sh_buffer_data(files,&p,&bytes);
    if(s<0)goto done;
    if(bytes>0xFFFFFFFFu-sizeof(header)-sizeof(row)) {
        s=SC_SHELL_E_RANGE;
        goto done;
    }
    header.size=sizeof(header);
    header.version=1;
    header.count=1;
    header.total_bytes=sizeof(header)+sizeof(row)+bytes;
    row.format=SC32_CLIPBOARD_FILES;
    row.offset=sizeof(header)+sizeof(row);
    row.bytes=bytes;
    row.flags=0;
    s=ScBufferCreate(header.total_bytes,&offer);
    if(s>=0)s=ScBufferAppend(offer,&header,sizeof(header));
    if(s>=0)s=ScBufferAppend(offer,&row,sizeof(row));
    if(s>=0)s=ScBufferAppend(offer,p,bytes);
    if(s>=0)s=sh_buffer_data(offer,&p,&n);
    /* 分配/追加失败不能继续读取尚未返回的offer长度。 */
    if(s<0)goto done;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.source=source;
    d.actions=actions;
    d.offer=p;
    d.offer_bytes=n;d.flags=flags&SC_SHELL_DRAG_DEFERRED?SC_UI_DRAG_DEFERRED:0;
    if(s>=0)s=ScUiDragBegin(ui,&d,token);
    done:ScBufferRelease(files);
    ScBufferRelease(offer);
    return sh_cause(s,85,0);
}
ScStatus ScShellDragBegin(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 *token)
{return sh_drag_begin(ui,source,items,count,actions,0,token);}
ScStatus ScShellDragBeginEx(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 flags,ScU32 *token)
{return sh_drag_begin(ui,source,items,count,actions,flags,token);}
ScStatus ScShellDropFiles(const ScUiDragData *data,const char *destination,const char *recovery,ScShellFileJob **out) {
    ScBrokerIdentity self;
    ScShellFileItem *items=0;
    ScShellFileJobDesc d;
    ScU32 count=0,operation=0;
    ScStatus s=sh_record(data,sizeof(*data));
    if(s<0)return s;
    if(!out||data->phase!=SC32_DRAG_TRANSFER||data->format!=SC32_CLIPBOARD_FILES|| (data->chosen_action!=SC32_DROP_COPY&&data->chosen_action!=SC32_DROP_MOVE)||!(data->actions&data->chosen_action))return SC_SHELL_E_INVALID;
    s=sh_self(&self);
    if(s<0)return s;
    if(!sh_subject(&data->sender,&self))return SC_SHELL_E_ACCESS;
    s=sh_clip_files_decode(data->data,data->bytes,&items,&count,&operation);
    if(s<0)return s;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.operation=data->chosen_action==SC32_DROP_MOVE?SC_SHELL_MOVE:SC_SHELL_COPY;
    d.flags=SC_SHELL_RECURSIVE;
    d.items=items;
    d.count=count;
    d.destination=destination;
    d.recovery_path=recovery;
    d.final_mode=3;
    s=ScShellFileOperationBegin(&d,out);
    sh_file_items_free(items,count);
    return s;
}

#include "shell_drop_deferred.inc"
