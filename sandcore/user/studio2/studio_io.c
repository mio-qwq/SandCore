static void rec(void *p,U n){
    memset(p,0,n);
    ((U *)p)[0]=n;
    ((U *)p)[1]=1;
}
static ScUiText text(const char *s){
    ScUiText t={
        sizeof(t),1,s,(U)strlen(s)
    };
    return t;
}
static StDoc *document(U id){
    for(StDoc *d=st.documents;d;d=d->next)if(d->id==id)return d;
    return 0;
}
static StDoc *document_path(const char *p){
    for(StDoc *d=st.documents;d;d=d->next)if(same_name(d->path,p))return d;
    return 0;
}
static ScStatus path(const char *input,char out[68])
{
    U n=0;
    ScStatus s=ScPathResolve(input,out,68,&n);
    if(s<0)return s;
    if(!n||n>64||out[0]!='/')return SC32_E_RANGE;
    for(U i=0;i<n;i++)if((unsigned char)out[i]<=32u||out[i]==';'||out[i]=='\\'||out[i]==':')return SC32_E_INVALID;
    return ScUtf8Validate(out,n,0)<0?SC32_E_INVALID:0;
}
static U workspace_suffix(const char *name)
{
    const char *base=strrchr(name,'/');
    const char *suffix=strrchr(base?base+1:name,'.');
    return suffix&&same_name(suffix,".SCW");
}
static ScStatus workspace_save_name(const char *name,char out[68],U *implicit)
{
    ScStatus s=path(name,out);
    if(s<0)return s;
    const char *base=strrchr(out,'/');
    const char *suffix=strrchr(base?base+1:out,'.');
    *implicit=0;
    if(suffix)return workspace_suffix(out)?0:SC32_E_INVALID;
    U n=(U)strlen(out);
    if(n>60u)return SC32_E_RANGE;
    memcpy(out+n,".SCW",5);
    *implicit=1;
    return 0;
}
static void pin_control(StPin *p,ScLeaseControl *c){
    rec(c,sizeof(*c));
    c->lease=p->info.lease;
    c->generation=p->info.generation;
    c->operation_sequence=p->info.operation_sequence;
}
static ScStatus pin_open(StPin *p,const char *name,U revision,U metadata)
{
    ScLeaseOpenDesc d;
    rec(&d,sizeof(d));
    rec(&p->info,sizeof(p->info));
    d.path=name;
    d.object_kind=SC32_PATH_FILE;
    d.lease_kind=SC32_LEASE_READ_STABLE;
    d.expected_revision=revision;
    d.flags=metadata?SC32_LEASE_METADATA_ONLY:0;
    d.open_flags=metadata?0:SC32_FILE_READ;
    ScStatus s=ScFileLeaseOpenPinned(&d,&p->info);
    if(s>=0)p->live=1;
    return s;
}
static ScStatus pin_close(StPin *p)
{
    if(!p->live)return 0;
    if(p->info.file){
        ScStatus s=ScFileClose(p->info.file);
        if(s==SC32_PENDING||s==SC32_E_BUSY||s==SC32_E_AGAIN)return SC32_PENDING;
        if(s<0&&s!=SC32_E_STALE)return s;
        p->info.file=0;
    }
    if(p->info.directory){
        ScStatus s=ScDirectoryClose(p->info.directory);
        if(s==SC32_PENDING||s==SC32_E_BUSY||s==SC32_E_AGAIN)return SC32_PENDING;
        if(s<0&&s!=SC32_E_STALE)return s;
        p->info.directory=0;
    }
    /* Acquire失败时也持有待关目录，但没有lease；Close成功才可清空。
     * 正常目录的Release仅解除绑定，不会替应用关闭既有目录句柄。 */
    if(!p->info.lease){
        memset(p,0,sizeof(*p));
        return 0;
    }
    ScLeaseControl c;
    ScLeaseInfo info;
    pin_control(p,&c);
    rec(&info,sizeof(info));
    ScStatus s=ScFileLeaseRelease(&c,&info);
    if(info.lease==p->info.lease)p->info=info;
    if(s==0||s==SC32_E_STALE){
        memset(p,0,sizeof(*p));
        return 0;
    }
    return s;
}
static void io_fail(ScStatus s){
    if(st.io&&!st.io->status)st.io->status=s;
    st.io->phase=S_CLEAN;
}
static ScStatus io_open(const char *name,U kind,U owner)
{
    if(st.io)return SC32_E_BUSY;
    char absolute[68];
    ScStatus s=path(name,absolute);
    if(s<0)return s;
    if(kind==S_IO_PROJECT_OPEN&&!workspace_suffix(absolute))return SC32_E_INVALID;
    if(kind==S_IO_OPEN){
        StDoc *old=document_path(absolute);
        if(old){
            st.active=old;
            ScUiTabSelect(st.ui,st.tabs,old->page);
            return 0;
        }
    }
    ScPathInfo info;
    rec(&info,sizeof(info));
    s=ScPathQuery(absolute,&info);
    if(s<0)return s;
    if(info.kind!=SC32_PATH_FILE||info.bytes>1048576u)return SC32_E_RANGE;
    StIo *io=calloc(1,sizeof(*io));
    if(!io)return SC32_E_MEMORY;
    io->phase=S_READ;
    io->kind=kind;
    io->owner=owner;
    io->revision=info.generation;
    io->total=info.bytes;
    strcpy(io->path,absolute);
    s=pin_open(&io->source,absolute,info.generation,0);
    if(s>=0)s=ScBufferCreate(info.bytes,&io->buffer);
    if(s<0){
        pin_close(&io->source);
        ScBufferRelease(io->buffer);
        free(io);
        return s;
    }
    st.io=io;
    status("读取候选文件…");
    return 0;
}
static ScStatus io_write_start(ScString *snapshot,const char *name,U expected,U kind,U owner,U revision,U absent)
{
    if(st.io)return SC32_E_BUSY;
    char absolute[68];
    ScStatus s=path(name,absolute);
    if(s<0)return s;
    ScPathInfo info;
    rec(&info,sizeof(info));
    s=ScPathQuery(absolute,&info);
    if(s<0&&s!=SC32_E_NOT_FOUND)return s;
    U exists=s==0;
    /* 自动扩展名没有对最终目标取得覆盖确认；这里只允许ABSENT，发布时继续原ABSENT条件。 */
    if(absent&&exists)return SC32_E_EXISTS;
    if(exists&&info.kind!=SC32_PATH_FILE)return SC32_E_INVALID;
    if(expected&&(!exists||info.generation!=expected))return SC32_E_STALE;
    StIo *io=calloc(1,sizeof(*io));
    if(!io)return SC32_E_MEMORY;
    io->phase=S_WRITE;
    io->kind=kind;
    io->owner=owner;
    io->epoch=st.epoch;
    io->revision=revision;
    io->mode=exists?info.rw:23u;
    strcpy(io->path,absolute);
    ScView view;
    rec(&view,sizeof(view));
    s=ScStringView(snapshot,&view);
    if(s>=0){
        io->total=view.bytes;
        s=ScStringRetain(snapshot);
        if(s>=0)io->snapshot=snapshot;
    }
    if(s>=0&&exists)s=pin_open(&io->target,absolute,info.generation,1);
    ScProcessRef self;
    rec(&self,sizeof(self));
    if(s>=0)s=ScProcessSelf(&self);
    if(s>=0)io->seed=self.pid*40503u+self.generation*2654435761u+st.epoch;
    if(s<0){
        pin_close(&io->target);
        ScStringRelease(io->snapshot);
        free(io);
        return s;
    }
    st.io=io;
    status("写入私有候选，原文件保持…");
    return 0;
}
static ScStatus io_save(StDoc *d,const char *name)
{
    StDoc *alias=document_path(name);
    if(alias&&alias!=d)return SC32_E_EXISTS;
    ScUiDocumentInfo info;
    rec(&info,sizeof(info));
    ScStatus s=ScUiDocumentQuery(d->text,&info);
    ScString *snapshot=0;
    if(s>=0)s=ScUiDocumentCopy(d->text,0,info.bytes,&snapshot);
    if(s>=0)s=io_write_start(snapshot,name,!strcmp(name,d->path)?d->disk_revision:0,S_IO_SAVE,d->id,info.revision,0);
    ScStringRelease(snapshot);
    return s;
}
static ScStatus io_project_save(const char *name)
{
    char absolute[68];
    U implicit=0;
    ScStatus s=workspace_save_name(name,absolute,&implicit);
    if(s<0)return s;
    ScString *snapshot=0;
    s=project_encode(&snapshot);
    if(s>=0)s=io_write_start(snapshot,absolute,!strcmp(absolute,st.project_path)?st.project_disk_revision:0,S_IO_PROJECT_SAVE,0,st.project_revision,implicit);
    ScStringRelease(snapshot);
    return s;
}
static ScStatus io_candidate(StIo *io)
{
    U n=(U)strlen(io->path);
    while(n&&io->path[n-1]!='/')n--;
    if(!n||n>57u)return SC32_E_RANGE;
    memcpy(io->candidate,io->path,n);
    snprintf(io->candidate+n,68-n,"S%06X",io->seed++&0xffffffu);
    ScLeaseOpenDesc d;
    rec(&d,sizeof(d));
    d.path=io->candidate;
    d.object_kind=SC32_PATH_FILE;
    d.lease_kind=SC32_LEASE_WRITE_EXCLUSIVE;
    d.open_flags=SC32_FILE_READ|SC32_FILE_WRITE;
    d.create_mode=3;
    rec(&io->source.info,sizeof(io->source.info));
    ScStatus s=ScFileLeaseCreatePinned(&d,&io->source.info);
    if(s>=0)io->source.live=1;
    return s==SC32_E_EXISTS?SC32_PENDING:s;
}
static void io_complete(void)
{
    StIo *io=st.io;
    ScStatus s=io->status;
    if(s>=0 && (io->kind==S_IO_OPEN||io->kind==S_IO_PROJECT_OPEN)){
        ScView v;
        rec(&v,sizeof(v));
        s=ScBufferView(io->buffer,&v);
        if(s>=0 && memchr(v.data,0,v.bytes))s=SC32_E_INVALID;
        if(s>=0)s=ScUtf8Validate(v.data,v.bytes,0);
        if(s>=0&&io->kind==S_IO_OPEN){
            StDoc *d=0;
            s=doc_new(io->path,v.data,v.bytes,io->revision,&d);
            if(s>=0&&st.jump_line){
                st.jump_doc=d->id;
                navigate(0);
            }
        }
        else if(s>=0)s=project_load(v.data,v.bytes,io->revision,io->path);
    }
    else if(s>=0&&io->kind==S_IO_SAVE){
        StDoc *d=document(io->owner);
        if(!d)s=SC32_E_STALE;
        else{
            if(strcmp(d->path,io->path)){
                for(U i=0;i<st.member_count;i++)if(!strcmp(st.members[i].path,d->path))strcpy(st.members[i].path,io->path);
                st.project_dirty=1;
                st.project_revision++;
                changed();
                project_notify();
            }
            strcpy(d->path,io->path);
            d->disk_revision=io->epoch;
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            if(s>=0)s=ScUiDocumentQuery(d->text,&q);
            if(s>=0&&q.revision==io->revision)ScUiDocumentMarkSaved(d->text);
        }
    }
    else if(s>=0&&io->kind==S_IO_PROJECT_SAVE){
        strcpy(st.project_path,io->path);
        st.project_disk_revision=io->epoch;
        if(st.project_revision==io->revision){
            st.project_saved=st.epoch;
            st.project_dirty=0;
        }
        project_notify();
    }
    U owner=io->owner,kind=io->kind;
    ScBufferRelease(io->buffer);
    ScStringRelease(io->snapshot);
    free(io);
    st.io=0;
    if(s<0){
        st.save_all=st.build_requested=0;
        if(st.build.phase){
            st.build.error=s;
            st.build.cancel=1;
        }
        error(s,"文件操作未完成，内存作品保留");
    }
    else{
        status("文件操作完成");
        if(st.pending_action&&owner){
            U action=st.pending_action;
            st.pending_action=0;
            StDoc *d=document(owner);
            ScUiDocumentInfo q;
            rec(&q,sizeof(q));
            if(d&&ScUiDocumentQuery(d->text,&q)>=0&&!(q.flags&SC_UI_DOCUMENT_DIRTY))perform(action);
        }
        else if(st.pending_action&&kind==S_IO_PROJECT_SAVE&&!st.project_dirty){
            U action=st.pending_action;
            st.pending_action=0;
            perform(action);
        }
    }
    refresh();
}
static void io_step(void)
{
    StIo *io=st.io;
    if(!io)return;
    ScStatus s=0;
    if(io->cancel==1u && io->phase!=S_CLEAN){
        if(io->source.live&&io->kind!=S_IO_OPEN&&io->kind!=S_IO_PROJECT_OPEN){
            ScLeaseControl c;
            ScLeaseInfo q;
            pin_control(&io->source,&c);
            rec(&q,sizeof(q));
            s=ScFileLeaseCancel(&c,&q);
            if(q.lease==io->source.info.lease)io->source.info=q;
            if(q.state==SC32_LEASE_COMMITTED&&(q.flags&SC32_LEASE_FLAG_DURABLE)&&!(q.flags&SC32_LEASE_FLAG_UNCERTAIN)){
                io->epoch=q.revision;
                io->cancel=0;
                io->phase=S_CLEAN;
                st.pending_action=0;
                return;
            }
            if(s==SC32_PENDING){
                io->cancel=2;
                io->phase=S_ADVANCE;
                return;
            }
        }
        io_fail(SC32_E_CANCELLED);
        return;
    }
    if(io->phase==S_READ){
        U n=io->total-io->offset;
        if(n>16384)n=16384;
        if(n){
            unsigned char chunk[16384];
            ScIoResult r;
            rec(&r,sizeof(r));
            s=ScFileRead(io->source.info.file,chunk,n,&r);
            if(s>=0&&r.transferred!=n)s=SC32_E_IO;
            if(s>=0)s=ScBufferAppend(io->buffer,chunk,n);
            if(s<0)io_fail(s);
            else io->offset+=n;
            return;
        }
        io->phase=S_CLEAN;
        return;
    }
    if(io->phase==S_WRITE){
        if(!io->source.live){
            s=io_candidate(io);
            if(s<0)io_fail(s);
            return;
        }
        if(io->offset==io->total){
            io->phase=S_FLUSH;
            return;
        }
        ScView v;
        rec(&v,sizeof(v));
        s=ScStringView(io->snapshot,&v);
        U n=io->total-io->offset;
        if(n>16384)n=16384;
        ScIoResult r;
        rec(&r,sizeof(r));
        if(s>=0)s=ScFileWrite(io->source.info.file,(const unsigned char *)v.data+io->offset,n,&r);
        if(r.transferred<=n)io->offset+=r.transferred;
        if(s<0||!r.transferred)io_fail(s<0?s:SC32_E_IO);
        return;
    }
    if(io->phase==S_FLUSH){
        s=ScFileFlush(io->source.info.file);
        if(s<0)io_fail(s);
        else io->phase=S_PUBLISH_IO;
        return;
    }
    if(io->phase==S_PUBLISH_IO){
        ScLeaseControl c;
        ScLeaseInfo q;
        pin_control(&io->source,&c);
        rec(&q,sizeof(q));
        s=ScFileLeaseQuery(&c,&q);
        if(s<0){
            io_fail(s);
            return;
        }
        io->source.info=q;
        ScLeasePublishDesc d;
        rec(&d,sizeof(d));
        d.source_lease=q.lease;
        d.source_generation=q.generation;
        d.expected_source_object=q.object_id;
        d.expected_source_revision=q.revision;
        d.target_path=io->path;
        d.final_mode=io->mode;
        d.flags=SC32_LEASE_EXPECT_ABSENT|SC32_LEASE_SET_MODE;
        if(io->target.live){
            d.flags=SC32_LEASE_EXPECT_EXACT;d.final_mode=0;
            d.target_lease=io->target.info.lease;
            d.target_generation=io->target.info.generation;
            d.expected_target_object=io->target.info.object_id;
            d.expected_target_revision=io->target.info.revision;
        }
        rec(&q,sizeof(q));
        /* exact旧目标继承自身metadata；缺名文件仍按新文件模式发布。
         * 不能借候选的caller所有者改掉共享工程/源文件的原身份。 */
        s=io->target.live?ScFileLeasePublishPreserving(&d,&q):ScFileLeasePublish(&d,&q);
        if(q.lease==io->source.info.lease)io->source.info=q;
        if(s==SC32_PENDING)io->phase=S_ADVANCE;
        else io_fail(s<0?s:SC32_E_FAILED);
        return;
    }
    if(io->phase==S_ADVANCE){
        ScLeaseControl c;
        ScLeaseInfo q;
        pin_control(&io->source,&c);
        rec(&q,sizeof(q));
        s=ScFileLeaseAdvance(&c,&q);
        if(q.lease==io->source.info.lease)io->source.info=q;
        if(s==SC32_PENDING)return;
        if(s<0||q.state!=SC32_LEASE_COMMITTED||!(q.flags&SC32_LEASE_FLAG_DURABLE)||(q.flags&SC32_LEASE_FLAG_UNCERTAIN))io_fail(s<0?s:SC32_E_IO);
        else{
            io->epoch=q.revision;
            io->phase=S_CLEAN;
        }
        return;
    }
    if(io->phase==S_CLEAN){
        s=pin_close(&io->source);
        if(s==SC32_PENDING)return;
        if(s<0&&!io->status)io->status=s;
        s=pin_close(&io->target);
        if(s==SC32_PENDING)return;
        if(s<0&&!io->status)io->status=s;
        io_complete();
    }
}
