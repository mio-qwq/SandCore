#include "RDAPP.H"
static ScU32 serial;
static void control(const RdPin *pin,ScLeaseControl *c)
{
    RdRecord(c,sizeof(*c));
    c->lease=pin->value.lease;
    c->generation=pin->value.generation;
    c->operation_sequence=pin->value.operation_sequence;
}
static ScStatus pin_existing(RdPin *pin,const char *path,ScU32 revision,int metadata)
{
    ScLeaseOpenDesc d;
    RdRecord(&d,sizeof(d));
    d.path=path;
    d.object_kind=SC32_PATH_FILE;
    d.lease_kind=SC32_LEASE_READ_STABLE;
    d.expected_revision=revision;
    d.flags=metadata?SC32_LEASE_METADATA_ONLY:0;
    d.open_flags=metadata?0:SC32_FILE_READ;
    RdRecord(&pin->value,sizeof(pin->value));
    ScStatus status=ScFileLeaseOpenPinned(&d,&pin->value);
    if(status>=0)pin->live=1;
    return status;
}
static int release_pin(RdPin *pin)
{
    if(!pin->live)return 0;
    if(pin->value.file){
        ScStatus status=ScFileClose(pin->value.file);
        if(status==SC32_E_AGAIN || status==SC32_E_BUSY)return 1;
        if(status<0 && status!=SC32_E_STALE)RdError(status,"关闭文件句柄失败");
        pin->value.file=0;
    }
    ScLeaseControl c;
    control(pin,&c);
    ScLeaseInfo value;
    RdRecord(&value,sizeof(value));
    ScStatus status=ScFileLeaseRelease(&c,&value);
    if(status==SC32_PENDING){
        pin->value=value;
        return 1;
    }
    if(status<0 && status!=SC32_E_STALE){
        RdError(status,"释放文件租约失败");
        return 1;
    }
    pin->live=0;
    return 0;
}
static ScStatus canonical(const char *path,char out[68])
{
    ScU32 bytes=0;
    ScStatus status=ScPathResolve(path,out,68,&bytes);
    if(status<0)return status;
    if(!bytes || bytes>64 || out[0]!='/')return SC32_E_RANGE;
    for(ScU32 i=0;i<bytes;i++)if((unsigned char)out[i]<=32 || out[i]==';' || out[i]=='\\' || out[i]==':')return SC32_E_INVALID;
    return ScUtf8Validate(out,bytes,0)<0?SC32_E_INVALID:0;
}
static void complete(ScStatus status)
{
    rd.io.status=status;
    rd.io.phase=RD_IO_CLEANUP;
    if(status<0)rd.dialog_action=rd.dialog_doc=0;
    if(status<0)RdError(status,"文件操作未提交，文档和撤销仍保留");
}
ScStatus RdIoStart(ScU32 kind,const char *path,RdDoc *doc)
{
    if(rd.io.kind)return SC32_E_BUSY;
    if(kind!=RD_IO_OPEN && kind!=RD_IO_SAVE)return SC32_E_INVALID;
    RdIo candidate;
    memset(&candidate,0,sizeof(candidate));
    candidate.kind=kind;
    candidate.phase=1;
    ScStatus status=canonical(path,candidate.path);
    if(status<0)return status;
    if(kind==RD_IO_SAVE){
        if(!doc)return SC_UI_E_NOT_FOUND;
        ScUiDocumentInfo info;
        RdRecord(&info,sizeof(info));
        status=ScUiDocumentQuery(doc->document,&info);
        if(status<0)return status;
        candidate.doc=doc->id;
        candidate.revision=info.revision;
        status=ScUiDocumentCopy(doc->document,0,info.bytes,&candidate.snapshot);
        if(status<0)return status;
        RdRecord(&candidate.view,sizeof(candidate.view));
        status=ScStringView(candidate.snapshot,&candidate.view);
        if(status<0){
            ScStringRelease(candidate.snapshot);
            return status;
        }
    }
    else {
        status=ScBufferCreate(4096,&candidate.bytes);
        if(status<0)return status;
    }
    rd.io=candidate;
    RdStatus(kind==RD_IO_OPEN?"正在固定并读取完整候选":"正在准备条件保存");
    return 0;
}
void RdIoCancel(void){
    if(rd.io.kind)rd.io.cancel=1;
}
int RdIoStep(void)
{
    RdIo *io=&rd.io;
    if(!io->kind)return 0;
    ScStatus status=0;
    if(io->phase==RD_IO_CLEANUP){
        if(release_pin(&io->target) || release_pin(&io->source))return 1;
        ScStringRelease(io->snapshot);
        ScBufferRelease(io->bytes);
        memset(io,0,sizeof(*io));
        RdRefresh();
        return 0;
    }
    if(io->cancel && io->phase<5){
        complete(SC32_E_CANCELLED);
        return 1;
    }
    if(io->phase==1){
        ScPathInfo info;
        RdRecord(&info,sizeof(info));
        status=ScPathQuery(io->path,&info);
        if(io->kind==RD_IO_OPEN){
            if(status<0){
                complete(status);
                return 1;
            }
            if(info.kind!=SC32_PATH_FILE){
                complete(SC32_E_INVALID);
                return 1;
            }
            status=pin_existing(&io->source,io->path,info.generation,0);
            if(status<0){
                complete(status);
                return 1;
            }
            io->phase=2;
            return 1;
        }
        RdDoc *doc=RdDocument(io->doc);
        if(!doc){
            complete(SC32_E_STALE);
            return 1;
        }
        if(!status){
            if(info.kind!=SC32_PATH_FILE || (!strcmp(doc->path,io->path) && doc->disk_revision && doc->disk_revision!=info.generation)){
                complete(SC32_E_STALE);
                return 1;
            }
            status=pin_existing(&io->target,io->path,info.generation,1);
            if(status<0){
                complete(status);
                return 1;
            }
        }
        else if(status!=SC32_E_NOT_FOUND){
            complete(status);
            return 1;
        }
        ScProcessRef self;
        RdRecord(&self,sizeof(self));
        status=ScProcessSelf(&self);
        if(status<0){
            complete(status);
            return 1;
        }
        ScU32 parent=(ScU32)strlen(io->path);
        while(parent && io->path[parent-1]!='/')parent--;
        if(parent>34 || serial==UINT_MAX){
            complete(SC32_E_RANGE);
            return 1;
        }
        memcpy(io->candidate,io->path,parent);
        snprintf(io->candidate+parent,sizeof(io->candidate)-parent,"RD%08X%08X.TMP",self.pid,++serial);
        ScLeaseOpenDesc create;
        RdRecord(&create,sizeof(create));
        create.path=io->candidate;
        create.object_kind=SC32_PATH_FILE;
        create.lease_kind=SC32_LEASE_WRITE_EXCLUSIVE;
        create.open_flags=SC32_FILE_WRITE;
        create.create_mode=3;
        RdRecord(&io->source.value,sizeof(io->source.value));
        status=ScFileLeaseCreatePinned(&create,&io->source.value);
        if(status<0){
            complete(status);
            return 1;
        }
        io->source.live=1;
        io->phase=3;
        return 1;
    }
    if(io->phase==2){
        unsigned char body[4096];
        ScIoResult result;
        RdRecord(&result,sizeof(result));
        status=ScFileRead(io->source.value.file,body,sizeof(body),&result);
        if(result.transferred>sizeof(body)){
            complete(SC32_E_IO);
            return 1;
        }
        if(result.transferred){
            ScContainerInfo info;
            RdRecord(&info,sizeof(info));
            ScBufferQuery(io->bytes,&info);
            if(result.transferred>RD_MAX_BYTES-info.length){
                complete(SC32_E_RANGE);
                return 1;
            }
            ScStatus appended=ScBufferAppend(io->bytes,body,result.transferred);
            if(appended<0){
                complete(appended);
                return 1;
            }
        }
        if(status==SC32_E_AGAIN)return 1;
        if(status<0){
            complete(status);
            return 1;
        }
        if(result.transferred)return 1;
        ScView view;
        RdRecord(&view,sizeof(view));
        status=ScBufferView(io->bytes,&view);
        if(!status && (memchr(view.data,0,view.bytes) || ScUtf8Validate(view.data,view.bytes,0)<0))status=SC_UI_E_UTF8;
        if(!status)status=RdDocumentCreate(io->path,view.data,view.bytes,io->source.value.revision,0);
        if(!status)RdStatus("文件已完整打开");
        complete(status);
        return 1;
    }
    if(io->phase==3){
        if(io->offset<io->view.bytes){
            ScU32 take=io->view.bytes-io->offset;
            if(take>4096)take=4096;
            ScIoResult result;
            RdRecord(&result,sizeof(result));
            status=ScFileWrite(io->source.value.file,(const unsigned char *)io->view.data+io->offset,take,&result);
            if(result.transferred>take){
                complete(SC32_E_IO);
                return 1;
            }
            io->offset+=result.transferred;
            if(status<0 && status!=SC32_E_AGAIN){
                complete(status);
                return 1;
            }
            return 1;
        }
        status=ScFileFlush(io->source.value.file);
        if(status==SC32_E_AGAIN || status==SC32_PENDING)return 1;
        if(status<0){
            complete(status);
            return 1;
        }
        io->phase=4;
        return 1;
    }
    if(io->phase==4){
        ScLeaseControl c;
        control(&io->source,&c);
        ScLeaseInfo source;
        RdRecord(&source,sizeof(source));
        status=ScFileLeaseQuery(&c,&source);
        if(status<0){
            complete(status);
            return 1;
        }
        io->source.value=source;
        ScLeasePublishDesc publish;
        RdRecord(&publish,sizeof(publish));
        publish.source_lease=source.lease;
        publish.source_generation=source.generation;
        publish.expected_source_object=source.object_id;
        publish.expected_source_revision=source.revision;
        publish.target_path=io->path;
        publish.flags=SC32_LEASE_EXPECT_ABSENT|SC32_LEASE_SET_MODE;
        publish.final_mode=3;
        if(io->target.live){
            publish.flags=SC32_LEASE_EXPECT_EXACT;
            publish.final_mode=0;
            publish.target_lease=io->target.value.lease;
            publish.target_generation=io->target.value.generation;
            publish.expected_target_object=io->target.value.object_id;
            publish.expected_target_revision=io->target.value.revision;
        }
        ScLeaseInfo result;
        RdRecord(&result,sizeof(result));
        status=io->target.live?ScFileLeasePublishPreserving(&publish,&result):ScFileLeasePublish(&publish,&result);
        if(result.lease==source.lease)io->source.value=result;
        if(status!=SC32_PENDING){
            complete(status<0?status:SC32_E_FAILED);
            return 1;
        }
        io->phase=5;
        return 1;
    }
    if(io->phase==5){
        ScLeaseControl c;
        control(&io->source,&c);
        ScLeaseInfo result;
        RdRecord(&result,sizeof(result));
        if(io->cancel){
            status=ScFileLeaseCancel(&c,&result);
            if(result.lease==io->source.value.lease)io->source.value=result;
            io->cancel=0;
            control(&io->source,&c);
        }
        RdRecord(&result,sizeof(result));
        status=ScFileLeaseAdvance(&c,&result);
        if(result.lease==io->source.value.lease)io->source.value=result;
        if(status==SC32_PENDING)return 1;
        if(status<0 || result.state!=SC32_LEASE_COMMITTED || !(result.flags&SC32_LEASE_FLAG_DURABLE) || (result.flags&SC32_LEASE_FLAG_UNCERTAIN)){
            complete(status<0?status:SC32_E_IO);
            return 1;
        }
        io->committed=1;
        RdDoc *doc=RdDocument(io->doc);
        if(doc){
            strcpy(doc->path,io->path);
            doc->disk_revision=result.revision;
            ScUiDocumentInfo info;
            RdRecord(&info,sizeof(info));
            if(!ScUiDocumentQuery(doc->document,&info) && info.revision==io->revision)ScUiDocumentMarkSaved(doc->document);
        }
        RdStatus("保存已持久提交；保存期间的新修改仍保留为未保存");
        complete(0);
        return 1;
    }
    complete(SC32_E_FAILED);
    return 1;
}
