static ScStatus n_path(const char *input,char out[68])
{
    ScU32 bytes=0;ScStatus status=ScPathResolve(input,out,68,&bytes);
    return status<0?status:bytes>64?SC32_E_RANGE:0;
}
static ScStatus n_revision(ScU32 *out)
{
    ScUiDocumentInfo info;n_record(&info,sizeof(info));
    ScStatus status=ScUiDocumentQuery(notes.document,&info);if(status>=0)*out=info.revision;return status;
}
static void n_lease_control(NotesLease *lease,ScLeaseControl *control)
{
    n_record(control,sizeof(*control));control->lease=lease->info.lease;
    control->generation=lease->info.generation;control->operation_sequence=lease->info.operation_sequence;
}
static ScStatus n_lease_open(NotesLease *lease,const char *path,ScU32 revision,ScU32 create,ScU32 metadata)
{
    ScLeaseOpenDesc desc;ScLeaseInfo info;n_record(&desc,sizeof(desc));n_record(&info,sizeof(info));
    desc.path=path;desc.object_kind=SC32_PATH_FILE;
    desc.lease_kind=create?SC32_LEASE_WRITE_EXCLUSIVE:SC32_LEASE_READ_STABLE;
    desc.expected_revision=revision;desc.create_mode=create?3:0;
    desc.flags=metadata?SC32_LEASE_METADATA_ONLY:0;
    desc.open_flags=metadata?0:create?SC32_FILE_READ|SC32_FILE_WRITE:SC32_FILE_READ;
    ScStatus status=create?ScFileLeaseCreatePinned(&desc,&info):ScFileLeaseOpenPinned(&desc,&info);
    if(status>=0){lease->info=info;lease->file=info.file;lease->valid=1;}
    return status;
}
static ScStatus n_lease_close(NotesLease *lease)
{
    if(!lease->valid)return 0;
    if(lease->file){ScStatus status=ScFileClose(lease->file);
        if(status==SC32_E_BUSY || status==SC32_E_AGAIN)return SC32_PENDING;
        lease->file=0;
    }
    ScLeaseControl control;ScLeaseInfo info;n_lease_control(lease,&control);n_record(&info,sizeof(info));
    ScStatus status=ScFileLeaseRelease(&control,&info);
    if(status==0 || status==SC32_E_STALE){memset(lease,0,sizeof(*lease));return 0;}
    if(info.lease==lease->info.lease)lease->info=info;
    return status;
}
static void n_io_fail(ScStatus status)
{
    NotesIo *io=notes.io;
    if(!io->status){io->status=status;n_record(&io->error,sizeof(io->error));ScErrorQuery(&io->error);}
    io->phase=N_IO_CLEANUP;
}
static void n_io_release(void)
{
    NotesIo *io=notes.io;if(!io)return;
    /* 正常UI只在作业结束后关闭；异常退出的剩余owner引用由内核按预算
     * 撤销，不能为fini写一个无界等待循环阻塞整机或谎称已回滚。 */
    n_lease_close(&io->source);n_lease_close(&io->target);
    ScBufferRelease(io->input);ScStringRelease(io->snapshot);
    ScUiDocumentRelease(io->candidate_document);free(io);notes.io=0;
}
static NotesIo *n_io_new(ScU32 kind,const char *path)
{
    if(notes.io)return 0;
    NotesIo *io=calloc(1,sizeof(*io));if(!io)return 0;
    io->kind=kind;strcpy(io->path,path);notes.io=io;return io;
}
static ScStatus n_load(const char *path,ScU32 optional)
{
    if(notes.io)return SC32_E_BUSY;
    char absolute[68];ScStatus status=n_path(path,absolute);if(status<0)return status;
    ScPathInfo metadata;n_record(&metadata,sizeof(metadata));status=ScPathQuery(absolute,&metadata);
    if(status==SC32_E_NOT_FOUND && optional){strcpy(notes.path,absolute);notes.loaded_revision=0;n_status("新文档，尚未保存");return 0;}
    if(status<0)return status;if(metadata.kind!=SC32_PATH_FILE)return SC32_E_INVALID;
    NotesIo *io=n_io_new(N_IO_LOAD,absolute);if(!io)return SC32_E_MEMORY;
    status=n_revision(&io->original_revision);
    if(status>=0)status=n_lease_open(&io->source,absolute,metadata.generation,0,0);
    if(status>=0)status=ScBufferCreate(0,&io->input);
    if(status<0){n_io_release();return status;}
    io->revision=metadata.generation;io->total=metadata.bytes;io->phase=N_IO_LOAD_READ;
    n_status("正在读取候选；原文保持可编辑");return 0;
}
static ScStatus n_save(const char *path)
{
    if(notes.io)return SC32_E_BUSY;
    char absolute[68];ScStatus status=n_path(path,absolute);if(status<0)return status;
    ScPathInfo metadata;n_record(&metadata,sizeof(metadata));status=ScPathQuery(absolute,&metadata);
    if(status<0 && status!=SC32_E_NOT_FOUND)return status;
    ScU32 existing=status==0;
    if(existing && metadata.kind!=SC32_PATH_FILE)return SC32_E_INVALID;
    if(!strcmp(absolute,notes.path) && notes.loaded_revision
       && (!existing || metadata.generation!=notes.loaded_revision))return SC32_E_STALE;
    NotesIo *io=n_io_new(N_IO_SAVE,absolute);if(!io)return SC32_E_MEMORY;
    ScUiDocumentInfo document;n_record(&document,sizeof(document));
    status=ScUiDocumentQuery(notes.document,&document);
    if(status>=0){io->original_revision=document.revision;io->total=document.bytes;
        status=ScUiDocumentCopy(notes.document,0,document.bytes,&io->snapshot);}
    if(status>=0 && existing)status=n_lease_open(&io->target,absolute,metadata.generation,0,1);
    if(status<0){n_io_release();return status;}
    io->phase=N_IO_WRITE;io->sequence=++notes.io_serial;
    ScProcessRef self;n_record(&self,sizeof(self));status=ScProcessSelf(&self);
    if(status<0){n_io_release();return status;}
    io->seed=self.pid*2654435761u+self.generation*40503u+io->sequence;
    n_status("正在写入私有候选；原文件尚未改变");return 0;
}
static ScStatus n_candidate(NotesIo *io)
{
    static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char parent[68];strcpy(parent,io->path);ScU32 bytes=(ScU32)strlen(parent);
    while(bytes && parent[bytes-1]!='/')bytes--;
    if(!bytes || bytes>=64)return SC32_E_RANGE;
    parent[bytes]=0;ScU32 width=64u-bytes;if(width>7)width=7;
    ScU32 space=1;
    for(ScU32 i=0;i<width;i++){if(space>0xffffffffu/36u){space=0;break;}space*=36u;}
    if(space && io->candidate_trials==space)return SC32_E_NO_SPACE;
    if(io->candidate_trials==0xffffffffu)return SC32_E_RANGE;
    ScU32 value;
    if(space){value=io->seed%space;
        value=io->candidate_trials>=space-value?io->candidate_trials-(space-value):value+io->candidate_trials;
    }else value=io->seed+io->candidate_trials;
    io->candidate_trials++;
    strcpy(io->candidate,parent);
    for(ScU32 i=0;i<width;i++){io->candidate[bytes+width-1-i]=digits[value%36u];value/=36u;}
    io->candidate[bytes+width]=0;
    /* 每次仅尝试一个尚未受理的名字，EXISTS只推进候选序号；不重放
     * 已接受的Write/Publish，不在UI回调中无界搜索或覆盖别人的文件。 */
    if(!strcmp(io->candidate,io->path))return SC32_PENDING;
    ScStatus status=n_lease_open(&io->source,io->candidate,0,1,0);
    return status==SC32_E_EXISTS?SC32_PENDING:status;
}
static ScStatus n_publish(NotesIo *io)
{
    ScLeaseControl query;ScLeaseInfo current;n_lease_control(&io->source,&query);n_record(&current,sizeof(current));
    ScStatus status=ScFileLeaseQuery(&query,&current);if(status<0)return status;io->source.info=current;
    ScLeasePublishDesc desc;n_record(&desc,sizeof(desc));n_record(&current,sizeof(current));
    desc.source_lease=io->source.info.lease;desc.source_generation=io->source.info.generation;
    desc.expected_source_object=io->source.info.object_id;desc.expected_source_revision=io->source.info.revision;
    desc.target_path=io->path;desc.flags=SC32_LEASE_EXPECT_ABSENT|SC32_LEASE_SET_MODE;desc.final_mode=23;
    if(io->target.valid){
        desc.flags=SC32_LEASE_EXPECT_EXACT|SC32_LEASE_SET_MODE;
        desc.target_lease=io->target.info.lease;desc.target_generation=io->target.info.generation;
        desc.expected_target_object=io->target.info.object_id;desc.expected_target_revision=io->target.info.revision;
        ScPathInfo prior;n_record(&prior,sizeof(prior));status=ScPathQuery(io->path,&prior);if(status<0)return status;
        desc.final_mode=prior.rw;
    }
    status=ScFileLeasePublish(&desc,&current);
    if(current.lease==io->source.info.lease)io->source.info=current;
    return status;
}
static void n_io_complete(void)
{
    NotesIo *io=notes.io;ScStatus status=io->status;ScU32 action=notes.pending_action;
    ScU32 revision=0;ScStatus queried=n_revision(&revision);
    if(status>=0 && queried<0)status=queried;
    if(status>=0 && io->kind==N_IO_LOAD){
        if(revision!=io->original_revision)status=SC32_E_STALE;
        else{status=ScUiEditBind(notes.ui,notes.edit,io->candidate_document);
            if(status>=0){ScUiDocumentRelease(notes.document);notes.document=io->candidate_document;
                io->candidate_document=0;strcpy(notes.path,io->path);notes.loaded_revision=io->revision;
                ScUiDocumentMarkSaved(notes.document);n_status("已打开，UTF-8 原始换行保持");}}
    }else if(status>=0){
        strcpy(notes.path,io->path);notes.loaded_revision=io->revision;
        if(revision==io->original_revision){ScUiDocumentMarkSaved(notes.document);n_status("已持久保存");}
        else{notes.pending_action=action=0;n_status("快照已保存；此后新增的编辑仍未保存");}
    }
    if(status<0){notes.pending_action=action=0;
        if(status==SC32_E_CANCELLED)n_status("操作已取消，内存作品保留");else n_error(status,io->path);}
    n_io_release();
    if(status>=0 && action){notes.pending_action=0;n_transition(action);}
    n_status(0);
}
static void n_io_step(void)
{
    NotesIo *io=notes.io;if(!io)return;ScStatus status=0;
    if(io->cancel && io->phase!=N_IO_CLEANUP){
        notes.pending_action=0;
        if(io->kind==N_IO_LOAD || !io->source.valid)n_io_fail(SC32_E_CANCELLED);
        else{ScLeaseControl control;ScLeaseInfo current;n_lease_control(&io->source,&control);n_record(&current,sizeof(current));
            status=ScFileLeaseCancel(&control,&current);io->cancel=0;
            if(current.lease==io->source.info.lease)io->source.info=current;
            if(current.state==SC32_LEASE_COMMITTED && (current.flags&SC32_LEASE_FLAG_DURABLE)){
                io->revision=current.revision;io->settled=1;io->phase=N_IO_CLEANUP;
            }else if(status==SC32_PENDING)io->phase=N_IO_ADVANCE;
            else n_io_fail(status<0?status:SC32_E_CANCELLED);
        }
        return;
    }
    if(io->phase==N_IO_LOAD_READ){
        ScU32 bytes=io->total-io->offset;if(bytes>N_IO_BYTES)bytes=N_IO_BYTES;
        if(bytes){ScIoResult result;n_record(&result,sizeof(result));status=ScFileRead(io->source.file,io->chunk,bytes,&result);
            if(status>=0 && result.transferred!=bytes)status=SC32_E_IO;
            if(status>=0)status=ScBufferAppend(io->input,io->chunk,bytes);
            if(status<0){n_io_fail(status);return;}io->offset+=bytes;return;}
        ScView view;n_record(&view,sizeof(view));status=ScBufferView(io->input,&view);
        if(status>=0)status=ScUtf8Validate(view.data,view.bytes,0);
        ScUiDocumentDesc desc;n_record(&desc,sizeof(desc));desc.flags=SC_UI_DOCUMENT_PRESERVE_EOL;
        desc.undo_budget_bytes=1048576;desc.text.size=sizeof(desc.text);desc.text.version=1;
        desc.text.data=view.data;desc.text.bytes=view.bytes;
        if(status>=0)status=ScUiDocumentCreate(&desc,&io->candidate_document);
        if(status<0)n_io_fail(status);else io->phase=N_IO_CLEANUP;
        return;
    }
    if(io->phase==N_IO_WRITE){
        if(!io->source.valid){status=n_candidate(io);if(status<0)n_io_fail(status);return;}
        if(io->offset==io->total){io->phase=N_IO_FLUSH;return;}
        ScView text;n_record(&text,sizeof(text));status=ScStringView(io->snapshot,&text);if(status<0){n_io_fail(status);return;}
        ScU32 bytes=io->total-io->offset;if(bytes>N_IO_BYTES)bytes=N_IO_BYTES;
        ScIoResult result;n_record(&result,sizeof(result));status=ScFileWrite(io->source.file,(const unsigned char *)text.data+io->offset,bytes,&result);
        if(result.transferred<=bytes)io->offset+=result.transferred;
        if(status<0 || !result.transferred){n_io_fail(status<0?status:SC32_E_IO);return;}
        return;
    }
    if(io->phase==N_IO_FLUSH){status=ScFileFlush(io->source.file);
        if(status<0)n_io_fail(status);else io->phase=N_IO_PUBLISH;return;}
    if(io->phase==N_IO_PUBLISH){status=n_publish(io);
        if(status==SC32_PENDING)io->phase=N_IO_ADVANCE;else n_io_fail(status<0?status:SC32_E_FAILED);return;}
    if(io->phase==N_IO_ADVANCE){
        ScLeaseControl control;ScLeaseInfo current;n_lease_control(&io->source,&control);n_record(&current,sizeof(current));
        status=ScFileLeaseAdvance(&control,&current);
        if(current.lease==io->source.info.lease)io->source.info=current;
        if(status==SC32_PENDING)return;
        if(status<0 || current.state!=SC32_LEASE_COMMITTED || !(current.flags&SC32_LEASE_FLAG_DURABLE)
           || current.flags&SC32_LEASE_FLAG_UNCERTAIN){n_io_fail(status<0?status:SC32_E_CANCELLED);return;}
        io->revision=current.revision;io->settled=1;io->phase=N_IO_CLEANUP;return;
    }
    if(io->phase==N_IO_CLEANUP){
        status=n_lease_close(&io->source);if(status==SC32_PENDING)return;
        if(status<0 && !io->status)io->status=status;
        status=n_lease_close(&io->target);if(status==SC32_PENDING)return;
        if(status<0 && !io->status)io->status=status;
        n_io_complete();
    }
}
