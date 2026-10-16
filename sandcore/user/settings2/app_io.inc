/* 有限16KiB批次共享状态机。不对UI事件同步重读全文，不以路径字符串
* 充当身份；每次写用私有候选，只有真实DURABLE终态才清调用者草稿。 */
static ScU32 temporary_serial;
static ScU32 path_equal(const char *a,const char *b)
{
    while(*a && *b){unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='a' && x<='z')x-=32;
        if(y>='a' && y<='z')y-=32;
        if(x!=y)return 0;
    }return !*a && !*b;
}
static ScStatus normalize(const char *path,const char *base,char out[65])
{
    ScString *s=0;
    const char *z=0;
    ScStatus code=ScPathNormalize(path,base,0,&s);
    if(code==0)code=ScStringCStr(s,&z);
    if(code==0 && (!z[0] || z[0]!='/' || strlen(z)>64))code=SC32_E_RANGE;
    if(code==0){for(const unsigned char *p=(const unsigned char *)z;*p;p++)if(*p<33 || *p==':' || *p=='\\'){code=SC32_E_INVALID;
            break;
        }}
    if(code==0)strcpy(out,z);
    ScStringRelease(s);
    return code;
}
static void pin_store(IoPin *pin,const ScLeaseInfo *info)
{pin->lease=info->lease;
    pin->generation=info->generation;
    pin->sequence=info->operation_sequence;
    pin->file=info->file;
    pin->directory=info->directory;
}
static ScLeaseControl pin_control(const IoPin *pin)
{ScLeaseControl q;
    INIT(q);
    q.lease=pin->lease;
    q.generation=pin->generation;
    q.operation_sequence=pin->sequence;
    return q;
}
static ScStatus pin_query(IoPin *pin,ScLeaseInfo *info)
{ScLeaseControl q=pin_control(pin);
    INIT(*info);
    ScStatus s=ScFileLeaseQuery(&q,info);
    if(s==0)pin_store(pin,info);
    return s;
}
static ScStatus pin_open(const char *path,ScU32 kind,ScU32 lease,ScU32 revision,ScU32 metadata,IoPin *out)
{
    ScLeaseOpenDesc q;
    INIT(q);
    q.path=path;
    q.object_kind=kind;
    q.lease_kind=lease;
    q.expected_revision=revision;
    q.flags=metadata?SC32_LEASE_METADATA_ONLY:0;
    q.open_flags=metadata || kind==SC32_PATH_DIRECTORY?0:lease==SC32_LEASE_READ_STABLE?SC32_FILE_READ:SC32_FILE_WRITE;
    ScLeaseInfo info;
    INIT(info);
    ScStatus s=ScFileLeaseOpenPinned(&q,&info);
    if(s==0)pin_store(out,&info);
    return s;
}
static ScStatus pin_release(IoPin *pin,ScU32 cancel)
{
    if(!pin->lease)return 0;
    ScLeaseControl q=pin_control(pin);
    ScLeaseInfo info;
    INIT(info);
    ScStatus s=cancel?ScFileLeaseCancel(&q,&info):ScFileLeaseRelease(&q,&info);
    if(s==SC32_PENDING){pin_store(pin,&info);
        return s;
    }
    if(s==SC32_E_STALE || s==0){
        if(pin->file)ScFileClose(pin->file);
        if(pin->directory)ScDirectoryClose(pin->directory);
        memset(pin,0,sizeof(*pin));
        return 0;
    }
    return s;
}
static ScStatus io_prepare(IoWrite *job,const char *destination,ScU32 expected,ScU32 mode,ScU32 kind)
{
    if(job->phase)return SC32_E_BUSY;
    memset(job,0,sizeof(*job));
    job->mode=mode;
    job->is_directory=kind==SC32_PATH_DIRECTORY;
    job->expected_target=expected;
    ScStatus s=normalize(destination,"/",job->destination);
    if(s<0)return s;
    ScPathInfo target;
    INIT(target);
    s=ScPathQuery(job->destination,&target);
    if(expected){
        if(s<0)return s;
        if(target.generation!=expected || target.kind!=kind)return SC32_E_STALE;
        s=pin_open(job->destination,kind,kind==SC32_PATH_FILE?SC32_LEASE_WRITE_EXCLUSIVE:SC32_LEASE_NAMESPACE,expected,0,&job->target);
        if(s<0)return s;
    }else if(s!=SC32_E_NOT_FOUND)return s<0?s:SC32_E_EXISTS;
    char parent[65];
    strcpy(parent,job->destination);
    char *slash=strrchr(parent,'/');
    if(!slash)return SC32_E_INVALID;
    if(slash==parent)slash[1]=0;
    else *slash=0;
    ScProcessRef self;
    INIT(self);
    s=ScProcessSelf(&self);
    for(ScU32 tries=0;s==0 && tries<64;tries++){
        int n;
        if(!expected){strcpy(job->temporary,job->destination);
            n=(int)strlen(job->temporary);
        }
        else n=snprintf(job->temporary,sizeof(job->temporary),"%s%s.S2%X%X",parent,strcmp(parent,"/")?"/":"",self.pid,++temporary_serial);
        if(expected && (n<0 || (ScU32)n>=sizeof(job->temporary))){
            char home[65];
            ScU32 length;
            s=ScProcessEnvironment("HOME",home,sizeof(home),&length);
            if(s<0)break;
            n=snprintf(job->temporary,sizeof(job->temporary),"%s/.S2%X%X",home,self.pid,++temporary_serial);
        }
        if(n<0 || (ScU32)n>=sizeof(job->temporary)){s=SC32_E_RANGE;
            break;
        }
        ScLeaseOpenDesc q;
        INIT(q);
        q.path=job->temporary;
        q.object_kind=kind;
        q.lease_kind=kind==SC32_PATH_FILE?SC32_LEASE_WRITE_EXCLUSIVE:SC32_LEASE_NAMESPACE;
        q.open_flags=kind==SC32_PATH_FILE?SC32_FILE_WRITE:0;
        q.create_mode=3;
        ScLeaseInfo info;
        INIT(info);
        s=ScFileLeaseCreatePinned(&q,&info);
        if(s==0){pin_store(&job->candidate,&info);
            job->observation=info;
            break;
        }
        if(expected && (s==SC32_E_EXISTS || s==SC32_E_BUSY))s=0;
        else break;
    }
    if(s==0 && !job->candidate.lease)s=SC32_E_EXISTS;
    if(s<0){pin_release(&job->target,0);
        return s;
    }
    job->phase=1;
    return 0;
}
static ScStatus io_start_memory(IoWrite *job,const char *target,ScString *body,ScU32 expected,ScU32 mode,ScU32 hold)
{
    ScView v;
    INIT(v);
    ScStatus s=ScStringView(body,&v);
    if(s<0)return s;
    s=io_prepare(job,target,expected,mode,SC32_PATH_FILE);
    if(s<0)return s;
    s=ScStringRetain(body);
    if(s<0){job->error=s;
        job->phase=5;
        return s;
    }
    job->memory=body;
    job->bytes=v.data;
    job->length=v.bytes;
    job->hold=hold;
    return 0;
}
static ScStatus io_start_copy(IoWrite *job,const char *target,const char *source,ScU32 revision,ScU32 mode)
{
    ScStatus s=io_prepare(job,target,0,mode,SC32_PATH_FILE);
    if(s<0)return s;
    s=pin_open(source,SC32_PATH_FILE,SC32_LEASE_READ_STABLE,revision,0,&job->source);
    ScFileInfo info;
    INIT(info);
    if(s==0)s=ScFileQuery(job->source.file,&info);
    if(s<0){job->error=s;
        job->phase=5;
        return s;
    }
    job->length=info.length;
    return 0;
}
static ScStatus io_start_directory(IoWrite *job,const char *target,ScU32 mode)
{return io_prepare(job,target,0,mode,SC32_PATH_DIRECTORY);
}
static ScStatus io_fail(IoWrite *job,ScStatus s)
{job->error=s;
    job->phase=5;
    return SC32_PENDING;
}
static ScStatus io_step(IoWrite *job)
{
    if(!job->phase)return 0;
    if(job->phase==6)return job->error;
    if(job->cancel && job->phase<4)return io_fail(job,SC32_E_CANCELLED);
    ScStatus s=0;
    if(job->phase==1){
        if(job->offset<job->length){
            ScU32 count=job->length-job->offset;
            if(count>sizeof(job->buffer))count=sizeof(job->buffer);
            const void *body;
            ScIoResult r;
            INIT(r);
            if(!job->bytes){
                if(job->buffer_offset==job->buffer_used){
                    s=ScFileRead(job->source.file,job->buffer,count,&r);
                    if(s<0)return io_fail(job,s);
                    if(!r.transferred || r.transferred>count)return io_fail(job,SC32_E_IO);
                    job->buffer_used=r.transferred;
                    job->buffer_offset=0;
                }
                count=job->buffer_used-job->buffer_offset;
                body=job->buffer+job->buffer_offset;
            }
            else body=job->bytes+job->offset;
            INIT(r);
            s=ScFileWrite(job->candidate.file,body,count,&r);
            if(r.transferred>count)return io_fail(job,SC32_E_FAILED);
            job->offset+=r.transferred;
            if(!job->bytes)job->buffer_offset+=r.transferred;
            if(r.flags&SC32_FILE_STATE_UNCERTAIN)job->uncertain=1;
            if(s<0 || !r.transferred)return io_fail(job,s<0?s:SC32_E_IO);
            return SC32_PENDING;
        }
        job->phase=2;
        return SC32_PENDING;
    }
    if(job->phase==2){
        if(!job->is_directory){s=ScFileFlush(job->candidate.file);
            if(s<0)return io_fail(job,s);
        }
        s=pin_query(&job->candidate,&job->observation);
        if(s<0)return io_fail(job,s);
        job->phase=3;
        return SC32_PENDING;
    }
    if(job->phase==3){
        ScLeaseInfo target;
        INIT(target);
        if(job->target.lease){s=pin_query(&job->target,&target);
            if(s<0)return io_fail(job,s);
            if(target.revision!=job->expected_target)return io_fail(job,SC32_E_STALE);
        }
        ScLeasePublishDesc q;
        INIT(q);
        q.source_lease=job->candidate.lease;
        q.source_generation=job->candidate.generation;
        q.target_path=job->destination;
        q.expected_source_object=job->observation.object_id;
        q.expected_source_revision=job->observation.revision;
        q.expected_snapshot_epoch=job->observation.snapshot_epoch;
        if(job->target.lease){q.flags=SC32_LEASE_EXPECT_EXACT;
            q.target_lease=target.lease;
            q.target_generation=target.generation;
            q.expected_target_object=target.object_id;
            q.expected_target_revision=target.revision;
        }else {q.flags=SC32_LEASE_EXPECT_ABSENT|SC32_LEASE_SET_MODE;
            q.final_mode=job->mode;
        }
        ScLeaseInfo info;
        INIT(info);
        s=job->target.lease?ScFileLeasePublishPreserving(&q,&info):ScFileLeasePublish(&q,&info);
        if(s<0)return io_fail(job,s);
        job->observation=info;
        pin_store(&job->candidate,&info);
        job->phase=4;
        return SC32_PENDING;
    }
    if(job->phase==4){
        ScLeaseControl q=pin_control(&job->candidate);
        ScLeaseInfo info;
        INIT(info);
        s=job->cancel?ScFileLeaseCancel(&q,&info):ScFileLeaseAdvance(&q,&info);
        if(info.lease){job->observation=info;
            pin_store(&job->candidate,&info);
        }
        if(s==SC32_PENDING)return s;
        if(info.state==SC32_LEASE_UNCERTAIN_STATE || info.flags&SC32_LEASE_FLAG_UNCERTAIN){job->uncertain=1;
            return io_fail(job,SC32_E_IO);
        }
        if(s<0)return io_fail(job,s);
        if(info.state!=SC32_LEASE_COMMITTED || !(info.flags&SC32_LEASE_FLAG_DURABLE))return io_fail(job,SC32_E_FAILED);
        job->durable=1;
        job->phase=5;
        return SC32_PENDING;
    }
    if(job->phase==5){
        s=pin_release(&job->source,0);
        if(s==SC32_PENDING)return s;
        s=pin_release(&job->target,0);
        if(s==SC32_PENDING)return s;
        if(!job->hold || !job->durable){s=pin_release(&job->candidate,0);
            if(s==SC32_PENDING)return s;
        }
        job->phase=6;
        return job->error;
    }
    return SC32_E_INVALID;
}
static void io_dispose(IoWrite *job)
{
    IoPin *pins[]={&job->source,&job->target,&job->candidate};
    for(ScU32 i=0;i<3;i++)if(pin_release(pins[i],0)==SC32_PENDING){
        /* 调用者只在终态/退出丢弃job。关闭FD是后端已有安全点，会把
        * 尚余GC转为独立回收，不丢下release=1 tombstone或悬空句柄。 */
        if(pins[i]->file)ScFileClose(pins[i]->file);
        if(pins[i]->directory)ScDirectoryClose(pins[i]->directory);
    }
    ScStringRelease(job->memory);
    memset(job,0,sizeof(*job));
}
static ScStatus read_start(IoRead *job,const char *path,ScU32 limit)
{
    if(job->phase)return SC32_E_BUSY;
    memset(job,0,sizeof(*job));
    job->limit=limit;
    ScStatus s=normalize(path,"/",job->path);
    ScPathInfo meta;
    INIT(meta);
    if(s==0)s=ScPathQuery(job->path,&meta);
    if(s==0 && (meta.kind!=SC32_PATH_FILE || meta.bytes>limit))s=SC32_E_RANGE;
    if(s==0)s=pin_open(job->path,SC32_PATH_FILE,SC32_LEASE_READ_STABLE,meta.generation,0,&job->pin);
    if(s==0)s=ScStringCreate("",0,&job->text);
    if(s<0){pin_release(&job->pin,0);
        return s;
    }
    job->length=meta.bytes;
    job->revision=meta.generation;
    job->phase=1;
    return 0;
}
static ScStatus read_step(IoRead *job)
{
    if(job->phase!=1)return job->error;
    if(job->offset<job->length){
        char buffer[16384];
        ScU32 count=job->length-job->offset;
        if(count>sizeof(buffer))count=sizeof(buffer);
        ScIoResult r;
        INIT(r);
        ScStatus s=ScFileRead(job->pin.file,buffer,count,&r);
        if(s==0 && (!r.transferred || r.transferred>count))s=SC32_E_IO;
        if(s==0)s=ScStringAppend(job->text,buffer,r.transferred);
        if(s<0){job->error=s;
            job->phase=2;
            return s;
        }
        job->offset+=r.transferred;
        return SC32_PENDING;
    }
    ScView v;
    INIT(v);
    ScStatus s=ScStringView(job->text,&v);
    ScU32 bad;
    if(s==0)s=ScUtf8Validate(v.data,v.bytes,&bad);
    if(s==0){const unsigned char *bytes=v.data;
        for(ScU32 i=0;i<v.bytes;i++)if(bytes[i]<32 && bytes[i]!='\n' && bytes[i]!='\r' && bytes[i]!='\t'){s=SC32_E_INVALID;
            break;
        }}
    job->error=s;
    job->phase=2;
    return s;
}
static void read_dispose(IoRead *job)
{pin_release(&job->pin,0);
    ScStringRelease(job->text);
    memset(job,0,sizeof(*job));
}
static ScStatus read_small(const char *path,ScU32 limit,ScString **out,ScU32 *revision)
{
    IoRead job;
    memset(&job,0,sizeof(job));
    ScStatus s=read_start(&job,path,limit);
    /* 此入口仅用于<4096B消费者配置，最多一次真实正文读；大文档走
    * read_step主循环，不把整目录或全文工作藏在pointer事件中。 */
    while(s==0 || s==SC32_PENDING){s=read_step(&job);
        if(s!=SC32_PENDING)break;
    }
    if(s==0){*out=job.text;
        job.text=0;
        if(revision)*revision=job.revision;
    }
    read_dispose(&job);
    return s;
}
