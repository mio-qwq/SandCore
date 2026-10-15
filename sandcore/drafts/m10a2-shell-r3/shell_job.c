static void sh_file_items_free(ScShellFileItem *items,ScU32 count) {
    ScU32 i;
    if(!items)return;
    for(i=0;i<count;++i) {
        free((void *)items[i].source);
        free((void *)items[i].destination);
    }
    free(items);
}
static ScStatus sh_file_items(const ScShellFileItem *items,ScU32 count,ScShellFileItem **out) {
    ScShellFileItem *copy;
    ScU32 i,marker=1;
    ScStatus s;
    ScHash *ids=0;
    ScHashDesc hd;
    if(!items||!count||!out||count>0xFFFFFFFFu/sizeof(*copy))return SC_SHELL_E_INVALID;
    copy=(ScShellFileItem *)calloc(count,sizeof(*copy));
    if(!copy)return SC_SHELL_E_MEMORY;
    memset(&hd,0,sizeof(hd));
    hd.size=sizeof(hd);
    hd.version=1;
    hd.value_bytes=4;
    s=ScHashCreate(&hd,&ids);
    if(s<0)goto fail;
    for(i=0;i<count;++i) {
        s=sh_record(items+i,sizeof(*items));
        if(s<0)goto fail;
        if(!items[i].item||(items[i].kind!=SC32_PATH_FILE&&items[i].kind!=SC32_PATH_DIRECTORY)||items[i].flags&~3u||items[i].reserved[0]||items[i].reserved[1]|| ((items[i].flags&SC_SHELL_ITEM_EXPECT_SOURCE)?!items[i].expected_source_revision:items[i].expected_source_revision!=0)|| ((items[i].flags&SC_SHELL_ITEM_EXPECT_DESTINATION)?(!items[i].expected_destination_revision||(items[i].expected_destination_kind!=1&&items[i].expected_destination_kind!=2)):(items[i].expected_destination_revision||items[i].expected_destination_kind))) {
            s=SC_SHELL_E_INVALID;
            goto fail;
        }
        s=ScHashSet(ids,&items[i].item,4,&marker,SC_HASH_INSERT_ONLY);
        if(s<0)goto fail;
        copy[i]=items[i];
        copy[i].source=0;
        copy[i].destination=0;
        if(items[i].source&&*items[i].source) {
            s=sh_absolute(items[i].source,(char **)&copy[i].source);
            if(s<0)goto fail;
        }
        if(items[i].destination&&*items[i].destination) {
            s=sh_absolute(items[i].destination,(char **)&copy[i].destination);
            if(s<0)goto fail;
        }
    }
    ScHashRelease(ids);
    *out=copy;
    return 0;
    fail:ScHashRelease(ids);
    sh_file_items_free(copy,count);
    return sh_cause(s,70,0);
}
static ScStatus sh_job_plan(const ScShellFileJobDesc *d,const ScWideCounter *nonce,ScBuffer **out) {
    ScShellWireJobBegin begin;
    ScShellWireJobItem *rows=0;
    ScShellFileItem *items=0;
    ScBuffer *plan=0;
    char *destination=0,*recovery=0;
    ScU32 i,n;
    const unsigned char *p;
    ScStatus s=sh_record(d,sizeof(*d));
    if(s<0)return s;
    if(d->operation<1||d->operation>5||d->flags&~3u||d->final_mode>63||d->conflict_policy|| d->reserved[0]||d->reserved[1]||d->reserved[2]||d->count>(0xFFFFFFFFu-sizeof(begin))/sizeof(*rows))return SC_SHELL_E_INVALID;
    if((d->flags&SC_SHELL_PRESERVE_MODE)&&d->operation!=SC_SHELL_COPY&&d->operation!=SC_SHELL_MOVE)return SC_SHELL_E_INVALID;
    if(d->destination&&*d->destination&&d->operation!=SC_SHELL_COPY&&d->operation!=SC_SHELL_MOVE)return SC_SHELL_E_INVALID;
    s=sh_file_items(d->items,d->count,&items);
    if(s<0)return s;
    if(d->destination&&*d->destination) {
        s=sh_absolute(d->destination,&destination);
        if(s<0)goto done;
    }
    s=sh_absolute(d->recovery_path,&recovery);
    if(s<0)goto done;
    for(i=0;i<d->count;++i) {
        if((d->operation==SC_SHELL_MKDIR&&(items[i].source||items[i].kind!=SC32_PATH_DIRECTORY))|| (d->operation==SC_SHELL_DELETE&&items[i].destination)||(d->operation!=SC_SHELL_MKDIR&&!items[i].source)|| ((d->operation==SC_SHELL_RENAME||d->operation==SC_SHELL_MKDIR)&&!items[i].destination)|| ((d->operation==SC_SHELL_COPY||d->operation==SC_SHELL_MOVE)&&!items[i].destination&&!destination)) {
            s=SC_SHELL_E_INVALID;
            goto done;
        }
    }
    rows=(ScShellWireJobItem *)calloc(d->count,sizeof(*rows));
    if(!rows) {
        s=SC_SHELL_E_MEMORY;
        goto done;
    }
    memset(&begin,0,sizeof(begin));
    begin.size=sizeof(begin);
    begin.version=1;
    begin.operation=d->operation;
    begin.flags=d->flags;
    begin.job_lo=nonce->lo;
    begin.job_hi=nonce->hi;
    begin.count=d->count;
    begin.items_offset=sizeof(begin);
    begin.final_mode=d->final_mode;
    n=sizeof(begin)+d->count*sizeof(*rows);
    s=ScBufferCreate(n,&plan);
    if(s>=0)s=ScBufferResize(plan,n);
    if(s<0)goto done;
    if(destination) {
        s=sh_append_string(plan,destination,&begin.destination_offset,&begin.destination_bytes);
        if(s<0)goto done;
    }
    s=sh_append_string(plan,recovery,&begin.recovery_offset,&begin.recovery_bytes);
    if(s<0)goto done;
    for(i=0;i<d->count;++i) {
        ScShellWireJobItem *r=rows+i;
        r->size=sizeof(*r);
        r->version=1;
        r->item_id=items[i].item;
        r->kind=items[i].kind;
        r->expected_source_revision=items[i].expected_source_revision;
        r->expected_destination_revision=items[i].expected_destination_revision;
        r->expected_destination_kind=items[i].expected_destination_kind;
        r->flags=items[i].flags;
        if(items[i].source) {
            s=sh_append_string(plan,items[i].source,&r->source_offset,&r->source_bytes);
            if(s<0)goto done;
        }
        if(items[i].destination) {
            s=sh_append_string(plan,items[i].destination,&r->destination_offset,&r->destination_bytes);
            if(s<0)goto done;
        }
    }
    s=ScBufferWrite(plan,0,&begin,sizeof(begin));
    if(s>=0)s=ScBufferWrite(plan,sizeof(begin),rows,d->count*sizeof(*rows));
    if(s>=0)s=sh_buffer_data(plan,&p,&n);
    if(s<0)goto done;
    *out=plan;
    plan=0;
    done:free(rows);
    sh_file_items_free(items,d->count);
    free(destination);
    free(recovery);
    ScBufferRelease(plan);
    return sh_cause(s,70,0);
}
static void sh_job_dispose(ShObject *base) {
    ScShellFileJob *job=(ScShellFileJob *)base;
    if(job->channel)ScIpcClose(job->channel);
    if(job->listener)ScIpcClose(job->listener);
    if(job->managed) {
        ScJobInfo info;
        ScStatus s;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScJobQuery(job->managed,&info);
        if(s>=0&&info.state!=SC32_JOB_FINISHED) {
            /* 显式Close放弃剩余操作，不将强制停止伪装成持久ACK。内核
             * lease临界提交按真实owner停止安全点完成，再释放受管记录。 */
            ScJobCancel(job->managed);
            ScJobWait(job->managed,SC32_INFINITE,&info);
        }
        ScJobClose(job->managed);
    }
    ScArrayRelease(job->results);
    ScBufferRelease(job->plan);
    ScBufferRelease(job->outgoing);
    ScBufferRelease(job->receive);
    ScBufferRelease(job->clip);
    free(job->scratch);
    if(!sh_quiet&&job->desc.context_drop)job->desc.context_drop(job->desc.context,0);
}
static ScStatus sh_job_queue(ScShellFileJob *job,ScU32 opcode,ScBuffer *body) {
    ScWideCounter next=job->sequence;
    ScStatus s;
    if(job->outgoing)return SC_SHELL_E_BUSY;
    s=sh_next_counter(&next);
    if(s<0)return s;
    s=ScBufferRetain(body);
    if(s<0)return sh_cause(s,71,0);
    job->sequence=next;
    job->outgoing=body;
    job->offset=0;
    memset(&job->output,0,sizeof(job->output));
    job->output.opcode=opcode;
    job->output.request_lo=next.lo;
    job->output.request_hi=next.hi;
    return 0;
}
static ScStatus sh_job_control(ScShellFileJob *job,ScU32 opcode) {
    ScShellWireJobControl c;
    ScBuffer *b=0;
    ScStatus s;
    memset(&c,0,sizeof(c));
    c.size=sizeof(c);
    c.version=1;
    c.job_lo=job->progress.nonce.lo;
    c.job_hi=job->progress.nonce.hi;
    s=ScBufferCreate(sizeof(c),&b);
    if(s>=0)s=ScBufferAppend(b,&c,sizeof(c));
    if(s>=0)s=sh_job_queue(job,opcode,b);
    ScBufferRelease(b);
    return s;
}
ScStatus ScShellFileOperationBegin(const ScShellFileJobDesc *d,ScShellFileJob **out) {
    ScShellFileJob *job=0;
    ScBuffer *plan=0;
    ScIpcEndpointDesc ep;
    ScArrayDesc array;
    ScJobInfo info;
    ScBrokerIdentity self;
    ScWideCounter nonce;
    char name[64],command[128];
    ScStatus s=sh_record(d,sizeof(*d));
    if(s<0)return s;
    if(!out)return SC_SHELL_E_INVALID;
    s=sh_self(&self);
    if(s<0)return s;
    nonce=sh_job_next;
    s=sh_next_counter(&sh_job_next);
    if(s<0)return s;
    s=sh_job_plan(d,&nonce,&plan);
    if(s<0)return s;
    s=sh_pin(d->procedure,d->context_drop);
    if(s<0)goto fail;
    s=ScCallbackPin((const void *)sh_job_dispose);
    if(s<0)goto fail;
    job=(ScShellFileJob *)sh_new(sizeof(*job),SH_JOB,sh_job_dispose);
    if(!job) {
        s=SC_SHELL_E_MEMORY;
        goto fail;
    }
    job->plan=plan;
    plan=0;
    job->scratch=(unsigned char *)malloc(SHW_FRAME_BYTES);
    if(!job->scratch) {
        s=SC_SHELL_E_MEMORY;
        goto fail;
    }
    memset(&array,0,sizeof(array));
    array.size=sizeof(array);
    array.version=1;
    array.item_bytes=sizeof(ScShellFileResult);
    s=ScArrayCreate(&array,&job->results);
    if(s<0)goto fail;
    job->progress.size=sizeof(job->progress);
    job->progress.version=1;
    job->progress.nonce=nonce;
    job->progress.state=SC_SHELL_JOB_CONNECTING;
    job->progress.items_total=d->count;
    job->self=self;
    if(snprintf(name,sizeof(name),"SCSJ.%u.%u.%u.%u.%u",self.pid,self.generation,self.session,nonce.lo,nonce.hi)>=(int)sizeof(name)) {
        s=SC_SHELL_E_RANGE;
        goto fail;
    }
    memset(&ep,0,sizeof(ep));
    ep.size=sizeof(ep);
    ep.version=1;
    ep.name=name;
    ep.session=self.session;
    ep.queue_budget_bytes=262144;
    ep.max_frame_bytes=SHW_FRAME_BYTES;
    ep.pending_budget_bytes=262144;
    ep.acl_flags=SC32_IPC_ACL_SESSION|SC32_IPC_ACL_UID|SC32_IPC_ACL_GID|SC32_IPC_ACL_REALM;
    ep.allowed_uid=self.uid;
    ep.allowed_gid=self.gid;
    ep.allowed_realm=SC32_BROKER_REALM_NORMAL;
    ep.allowed_session=self.session;
    s=ScIpcCreate(&ep,&job->listener);
    if(s>=0)s=ScIpcListen(job->listener);
    if(s<0)goto fail;
    if(snprintf(command,sizeof(command),"/SYS/SHELL/SCFILEWORK.SCX %s",name)>=(int)sizeof(command)) {
        s=SC_SHELL_E_RANGE;
        goto fail;
    }
    s=ScProcessSpawn(command,0,SC32_SPAWN_NO_STREAM_INHERIT,&job->managed);
    if(s<0)goto fail;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScJobQuery(job->managed,&info);
    if(s<0)goto fail;
    if(!info.child_pid||!info.child_generation) {
        s=SC_SHELL_E_STALE;
        goto fail;
    }
    job->child_pid=info.child_pid;
    job->child_generation=info.child_generation;
    job->desc=*d;
    job->desc.items=0;
    job->desc.destination=0;
    job->desc.recovery_path=0;
    *out=job;
    return 0;
    fail:ScBufferRelease(plan);
    if(job)sh_release(&job->base);
    return sh_cause(s,70,0);
}
static ScStatus sh_job_accept(ScShellFileJob *job) {
    ScIpcChannel channel=0;
    ScBrokerIdentity peer,actual;
    ScShellWireHello hello;
    ScStatus s;
    memset(&peer,0,sizeof(peer));
    peer.size=sizeof(peer);
    peer.version=1;
    s=ScIpcAccept(job->listener,&channel,&peer);
    if(s==SC32_E_AGAIN)return SC_SHELL_PENDING;
    if(s<0)return sh_cause(s,72,0);
    memset(&actual,0,sizeof(actual));
    actual.size=sizeof(actual);
    actual.version=1;
    s=ScIpcPeer(channel,&actual);
    if(s<0||!sh_same_identity(&peer,&actual)||!sh_subject(&peer,&job->self)||peer.pid!=job->child_pid||peer.generation!=job->child_generation) {
        ScIpcClose(channel);
        return SC_SHELL_PENDING;
    }
    job->channel=channel;
    job->peer=peer;
    ScIpcClose(job->listener);
    job->listener=0;
    memset(&hello,0,sizeof(hello));
    hello.size=sizeof(hello);
    hello.version=1;
    hello.protocol=1;
    hello.features=255;
    hello.job_lo=job->progress.nonce.lo;
    hello.job_hi=job->progress.nonce.hi;
    hello.max_frame_bytes=SHW_FRAME_BYTES;
    s=ScBufferCreate(sizeof(hello),&job->outgoing);
    if(s>=0)s=ScBufferAppend(job->outgoing,&hello,sizeof(hello));
    if(s<0)return sh_cause(s,72,0);
    memset(&job->output,0,sizeof(job->output));
    job->output.opcode=SHW_HELLO;
    job->offset=0;
    job->stage=1;
    return 0;
}
static ScStatus sh_job_flush(ScShellFileJob *job,ScU32 *progress) {
    ScStatus s;
    ScU32 before=job->offset,opcode=job->output.opcode;
    if(!job->outgoing)return 0;
    s=sh_frame_send(job->channel,&job->output,job->outgoing,&job->offset,job->scratch);
    if(job->offset!=before)*progress=1;
    if(s)return s;
    ScBufferRelease(job->outgoing);
    job->outgoing=0;
    job->offset=0;
    if(opcode==SHW_JOB_PLAN) {
        job->stage=3;
        s=sh_job_control(job,SHW_JOB_COMMIT);
        if(s<0)return s;
    } else if(opcode==SHW_JOB_COMMIT)job->stage=4;
    return 0;
}
static int sh_ack_path(const char *path) {
    const char *end=(const char *)memchr(path,0,68);
    ScU32 i,n;
    if(!end)return 0;
    n=(ScU32)(end-path);
    if(n>64||(n&&path[0]!='/')||ScUtf8Validate(path,n,0)<0)return 0;
    for(i=n+1;i<68;++i)if(path[i])return 0;
    return 1;
}
static ScStatus sh_job_frame(ScShellFileJob *job,const ScShellWireHeader *h,const unsigned char *data) {
    ScStatus s;
    ScWideCounter sequence;
    sequence.lo=h->request_lo;
    sequence.hi=h->request_hi;
    if(h->flags&SHW_MORE||h->offset||h->total_bytes!=h->body_bytes||h->status>0||sh_counter_before(&job->sequence,&sequence))return SC_SHELL_E_PROTOCOL;
    if(job->stage==1&&h->opcode==SHW_HELLO_ACK) {
        ScShellWireHello hello;
        if(h->flags!=SHW_REPLY||h->request_lo||h->request_hi||h->status||h->body_bytes!=sizeof(hello))return SC_SHELL_E_PROTOCOL;
        memcpy(&hello,data,sizeof(hello));
        if(hello.size!=sizeof(hello)||hello.version!=1||hello.protocol!=1||hello.job_lo!=job->progress.nonce.lo||hello.job_hi!=job->progress.nonce.hi||hello.max_frame_bytes!=SHW_FRAME_BYTES||hello.flags)return SC_SHELL_E_PROTOCOL;
        job->stage=2;
        job->progress.state=SC_SHELL_JOB_PLANNING;
        s=sh_job_queue(job,SHW_JOB_PLAN,job->plan);
        return s;
    }
    if(job->stage<2||(!h->request_lo&&!h->request_hi))return SC_SHELL_E_PROTOCOL;
    if(h->state_epoch<job->input.state_epoch)return SC_SHELL_E_PROTOCOL;
    job->input=*h;
    if(h->opcode==SHW_JOB_PROGRESS||h->opcode==SHW_JOB_RESULT||h->opcode==SHW_JOB_ACCEPTED||h->opcode==SHW_JOB_CANCELLED_ACK) {
        ScShellWireJobProgress progress;
        if(h->body_bytes!=sizeof(progress)||!(h->flags&(SHW_EVENT|SHW_REPLY)))return SC_SHELL_E_PROTOCOL;
        memcpy(&progress,data,sizeof(progress));
        if(progress.size!=sizeof(progress)||progress.version!=1||progress.job_lo!=job->progress.nonce.lo||progress.job_hi!=job->progress.nonce.hi|| progress.state<1||progress.state>9||progress.stage>7||progress.reserved[0]||progress.reserved[1]||progress.total_known>1)return SC_SHELL_E_PROTOCOL;
        memcpy(&job->progress,&progress,sizeof(progress));
        job->progress.size=sizeof(job->progress);
        if(h->opcode==SHW_JOB_RESULT||h->opcode==SHW_JOB_CANCELLED_ACK) {
            if(progress.state<SC_SHELL_JOB_FINISHED)return SC_SHELL_E_PROTOCOL;
            job->closed=1;
            job->conflict_valid=0;
        }
        return sh_event(&job->base,job->desc.procedure,job->desc.context,SC_SHELL_EVENT_JOB,h->state_epoch,0,job,progress.status,&job->progress,sizeof(job->progress),progress.current_item);
    }
    if(h->opcode==SHW_JOB_ITEM||h->opcode==SHW_JOB_CONFLICT) {
        ScShellFileResult result;
        ScContainerInfo info;
        if(h->flags!=SHW_EVENT||h->body_bytes!=sizeof(result))return SC_SHELL_E_PROTOCOL;
        memcpy(&result,data,sizeof(result));
        if(result.size!=sizeof(result)||result.version!=1||!sh_counter_equal(&result.nonce,&job->progress.nonce)||!result.item|| result.worker_pid!=job->peer.pid||result.worker_generation!=job->peer.generation||result.operation<1||result.operation>5||result.stage>7|| result.reserved[0]||result.reserved[1]||!sh_ack_path(result.source)||!sh_ack_path(result.destination)|| (!(result.flags&SC_SHELL_OBSERVATION_LIVE)&&(result.source_object||result.destination_object))|| (result.state==SC_SHELL_RESULT_UNCERTAIN&&(result.flags&SC_SHELL_DURABLE)))return SC_SHELL_E_PROTOCOL;
        if(h->opcode==SHW_JOB_CONFLICT) {
            if(!result.conflict_epoch)return SC_SHELL_E_PROTOCOL;
            job->conflict=result;
            job->conflict_valid=1;
            job->progress.state=SC_SHELL_JOB_CONFLICT;
            job->progress.conflict_epoch=result.conflict_epoch;
            return sh_event(&job->base,job->desc.procedure,job->desc.context,SC_SHELL_EVENT_CONFLICT,h->state_epoch,0,job,result.status,&job->conflict,sizeof(job->conflict),result.item);
        }
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScArrayQuery(job->results,&info);
        if(s<0)return sh_cause(s,73,result.item);
        s=ScArrayInsert(job->results,info.length,&result,1);
        if(s<0)return sh_cause(s,73,result.item);
        return sh_event(&job->base,job->desc.procedure,job->desc.context,SC_SHELL_EVENT_ITEM,h->state_epoch,0,job,result.status,&result,sizeof(result),result.item);
    }
    if(h->status<0&&(h->flags&SHW_REPLY)) {
        job->progress.status=h->status;
        job->progress.state=SC_SHELL_JOB_FAILED;
        job->closed=1;
        return sh_event(&job->base,job->desc.procedure,job->desc.context,SC_SHELL_EVENT_JOB,h->state_epoch,0,job,h->status,data,h->body_bytes,0);
    }
    return SC_SHELL_E_PROTOCOL;
}
ScStatus ScShellFileOperationStep(ScShellFileJob *job,ScU32 timeout) {
    ScStatus s=0;
    ScU32 i,progress=0;
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    if(job->base.depth)return SC_SHELL_E_BUSY;
    if(job->closed)return 0;
    sh_enter(&job->base);
    job->base.notification_error=0;
    for(i=0;i<SH_JOB_BATCH&&!job->base.closing&&!job->closed;++i) {
        ScShellWireHeader h;
        const unsigned char *body;
        if(!job->channel) {
            s=sh_job_accept(job);
            if(s==SC_SHELL_PENDING) {
                s=0;
                break;
            }
            if(s<0)break;
            progress=1;
        }
        s=sh_job_flush(job,&progress);
        if(s<0)break;
        if(job->cancel_requested&&!job->outgoing&&job->stage>=4) {
            s=sh_job_control(job,SHW_JOB_CANCEL);
            if(s<0)break;
            job->cancel_requested=0;
        }
        s=sh_frame_receive(job->channel,&job->peer,job->scratch,&h,&body);
        if(s==SC_SHELL_PENDING) {
            s=0;
            break;
        }
        if(s==SC_SHELL_END) {
            s=job->closed?0:SC_SHELL_E_UNKNOWN;
            break;
        }
        if(s<0)break;
        progress=1;
        s=sh_job_frame(job,&h,body);
        if(s<0)break;
    }
    if(s>=0&&!job->channel&&!job->closed) {
        ScJobInfo info;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        s=ScJobQuery(job->managed,&info);
        if(s>=0&&info.state==SC32_JOB_FINISHED)s=SC_SHELL_E_UNKNOWN;
    }
    if(s<0&&!job->closed&&!job->base.closing) {
        job->progress.status=s;
        job->progress.state=SC_SHELL_JOB_UNKNOWN;
        job->closed=1;
        sh_event(&job->base,job->desc.procedure,job->desc.context,SC_SHELL_EVENT_JOB,job->input.state_epoch,0,job,s,&job->progress,sizeof(job->progress),0);
    }
    if(s>=0&&!job->closed&&!job->base.closing&&(sh_io_ready(job->channel?job->channel:job->listener,job->outgoing,job->offset,!job->channel)|| (job->cancel_requested&&!job->outgoing&&job->stage>=4)))s=SC_SHELL_READY;
    if(s>=0&&s!=SC_SHELL_READY&&!progress&&timeout&&!job->closed&&!job->base.closing) {
        ScU32 handle=job->channel?job->channel:job->listener;
        ScU32 flags=job->channel?SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED:SC32_BROKER_READY_ACCEPT|SC32_BROKER_READY_CLOSED;
        if(job->outgoing)flags|=SC32_BROKER_READY_WRITE;
        s=sh_wait_min(handle,flags,job->outgoing?sh_frame_bytes(job->outgoing,job->offset):1,timeout);
        if(s>=0)s=SC_SHELL_PENDING;
    }
    if(s>=0&&s!=SC_SHELL_READY&&!job->closed)s=SC_SHELL_PENDING;
    if(s>=0&&job->base.notification_error<0)s=job->base.notification_error;
    sh_leave(&job->base);
    return s;
}
ScStatus ScShellFileOperationQuery(const ScShellFileJob *job,ScShellFileProgress *out) {
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    return sh_output(out,&job->progress,sizeof(job->progress));
}
ScStatus ScShellFileOperationResult(const ScShellFileJob *job,ScU32 index,ScShellFileResult *out) {
    ScShellFileResult result;
    ScContainerInfo info;
    ScStatus s=sh_record(out,sizeof(*out));
    if(s<0)return s;
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    memset(&info,0,sizeof(info));
    info.size=sizeof(info);
    info.version=1;
    s=ScArrayQuery(job->results,&info);
    if(s<0)return sh_cause(s,74,0);
    if(index>=info.length)return SC_SHELL_END;
    s=ScArrayGet(job->results,index,&result);
    if(s<0)return sh_cause(s,74,0);
    return sh_output(out,&result,sizeof(result));
}
ScStatus ScShellFileOperationConflict(const ScShellFileJob *job,ScShellFileResult *out) {
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    if(!job->conflict_valid)return SC_SHELL_E_NOT_FOUND;
    return sh_output(out,&job->conflict,sizeof(job->conflict));
}
ScStatus ScShellFileOperationResolveConflict(ScShellFileJob *job,const ScShellConflictDecision *d) {
    ScShellWireJobDecision w;
    ScBuffer *b=0;
    char *path=0;
    ScStatus s=sh_record(d,sizeof(*d));
    if(s<0)return s;
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    if(!job->conflict_valid||job->closed)return SC_SHELL_E_STALE;
    if(d->item!=job->conflict.item||d->conflict_epoch!=job->conflict.conflict_epoch||d->expected_source_revision!=job->conflict.source_revision|| d->expected_destination_revision!=job->conflict.destination_revision)return SC_SHELL_E_STALE;
    if(d->decision<1||d->decision>6)return SC_SHELL_E_INVALID;
    if(job->outgoing)return SC_SHELL_E_BUSY;
    memset(&w,0,sizeof(w));
    w.size=sizeof(w);
    w.version=1;
    w.job_lo=job->progress.nonce.lo;
    w.job_hi=job->progress.nonce.hi;
    w.item_id=d->item;
    w.conflict_epoch=d->conflict_epoch;
    w.decision=d->decision;
    w.expected_source_revision=d->expected_source_revision;
    w.expected_destination_revision=d->expected_destination_revision;
    if(d->decision==SC_SHELL_RENAME_TARGET) {
        s=sh_absolute(d->new_path,&path);
        if(s<0)goto done;
        w.new_path_bytes=(ScU32)strlen(path)+1;
    } else if(d->new_path&&*d->new_path)return SC_SHELL_E_INVALID;
    s=ScBufferCreate(sizeof(w)+68,&b);
    if(s>=0)s=ScBufferAppend(b,&w,sizeof(w));
    if(s>=0&&path) {
        ScU32 off,n;
        s=sh_append_string(b,path,&off,&n);
    }
    if(s>=0)s=sh_job_queue(job,SHW_JOB_RESOLVE,b);
    if(s>=0) {
        job->conflict_valid=0;
        job->progress.state=SC_SHELL_JOB_RUNNING;
    }
    done:free(path);
    ScBufferRelease(b);
    return sh_cause(s,75,0);
}
ScStatus ScShellFileOperationCancel(ScShellFileJob *job) {
    if(!sh_valid(job,SH_JOB))return SC_SHELL_E_STALE;
    if(job->closed)return 0;
    job->cancel_requested=1;
    job->progress.state=SC_SHELL_JOB_CANCELLING;
    return 0;
}
static ScStatus sh_job_hold(ScShellFileJob *job) {
    return sh_retain((ShObject *)job,SH_JOB);
}
void ScShellFileOperationClose(ScShellFileJob *job) {
    if(!sh_valid(job,SH_JOB))return;
    sh_release(&job->base);
}
