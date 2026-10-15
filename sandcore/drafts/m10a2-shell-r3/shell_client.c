static void sh_request_dispose(ShObject *base) {
    ScShellRequest *r=(ScShellRequest *)base;
    ScBufferRelease(r->body);
    ScBufferRelease(r->result);
}
static void sh_client_disconnect(ScShell *s,ScStatus cause) {
    ScShellRequest *r=s->head;
    ScIpcChannel channel=s->channel;
    s->channel=0;
    s->info.state=SC_SHELL_DISCONNECTED;
    if(channel)ScIpcClose(channel);
    s->head=0;
    s->tail=0;
    s->info.pending_requests=0;
    s->info.queued_bytes=0;
    while(r) {
        ScShellRequest *next=r->next;
        r->next=0;
        r->owner=0;
        if(r->info.enqueued&&r->info.side_effect) {
            r->info.state=SC_SHELL_REQUEST_UNKNOWN;
            r->info.status=SC_SHELL_E_UNKNOWN;
        } else {
            r->info.state=SC_SHELL_REQUEST_FAILED;
            r->info.status=cause;
        }
        r->error=sh_error;
        r->error.size=sizeof(r->error);
        r->error.version=1;
        r->error.code=r->info.status;
        r->error.operation=r->info.opcode;
        r->error.epoch=s->info.epoch;
        sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_REPLY,s->info.epoch,r,0,r->info.status,0,0,0);
        sh_release(&r->base);
        r=next;
    }
    sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_DISCONNECTED,s->info.epoch,0,0,cause,0,0,0);
}
static void sh_client_dispose(ShObject *base) {
    ScShell *s=(ScShell *)base;
    ScShellRequest *r=s->head;
    if(s->channel)ScIpcClose(s->channel);
    while(r) {
        ScShellRequest *next=r->next;
        r->owner=0;
        r->next=0;
        r->info.state=r->info.enqueued&&r->info.side_effect?SC_SHELL_REQUEST_UNKNOWN:SC_SHELL_REQUEST_FAILED;
        r->info.status=r->info.state==SC_SHELL_REQUEST_UNKNOWN?SC_SHELL_E_UNKNOWN:SC_SHELL_E_CANCELLED;
        memset(&r->error,0,sizeof(r->error));
        r->error.size=sizeof(r->error);
        r->error.version=1;
        r->error.code=r->info.status;
        r->error.operation=r->info.opcode;
        sh_release(&r->base);
        r=next;
    }
    ScBufferRelease(s->hello);
    ScBufferRelease(s->receive);
    free(s->scratch);
    if(!sh_quiet&&s->desc.context_drop)s->desc.context_drop(s->desc.context,0);
}
ScStatus ScShellConnect(const ScShellConnectDesc *d,ScShell **out) {
    ScShell *s=0;
    ScShellConnectionInfo c;
    ScBrokerIdentity self,peer;
    ScShellWireServiceHello hello;
    ScStatus status=sh_record(d,sizeof(*d));
    if(status<0)return status;
    if(!out||d->flags||(d->budget_bytes&&d->budget_bytes<SHW_FRAME_BYTES))return SC_SHELL_E_INVALID;
    status=sh_self(&self);
    if(status<0)return status;
    memset(&c,0,sizeof(c));
    c.size=sizeof(c);
    c.version=1;
    status=ScShellHostConnect(d->expected_host_epoch,&c);
    if(status<0)return sh_cause(status,40,0);
    memset(&peer,0,sizeof(peer));
    peer.size=sizeof(peer);
    peer.version=1;
    status=ScIpcPeer(c.channel,&peer);
    if(status<0)goto fail;
    if(!sh_same_identity(&peer,&c.peer)||!sh_subject(&peer,&self)||peer.pid!=c.host.pid||peer.generation!=c.host.generation|| peer.session!=c.host.session||c.host.state!=SC32_SHELL_ACTIVE) {
        status=SC_SHELL_E_ACCESS;
        goto fail;
    }
    status=sh_pin(d->procedure,d->context_drop);
    if(status<0)goto fail;
    s=(ScShell *)sh_new(sizeof(*s),SH_CLIENT,sh_client_dispose);
    if(!s) {
        status=SC_SHELL_E_MEMORY;
        goto fail;
    }
    s->scratch=(unsigned char *)malloc(SHW_FRAME_BYTES);
    if(!s->scratch) {
        status=SC_SHELL_E_MEMORY;
        goto fail;
    }
    memset(&hello,0,sizeof(hello));
    hello.size=sizeof(hello);
    hello.version=1;
    hello.protocol=1;
    hello.features=SHW_SERVICE_FEATURE_CATALOG|SHW_SERVICE_FEATURE_ASSOCIATION|SHW_SERVICE_FEATURE_LAYOUT;
    hello.host_epoch=c.host.host_epoch;
    hello.max_frame_bytes=SHW_FRAME_BYTES;
    status=ScBufferCreate(sizeof(hello),&s->hello);
    if(status>=0)status=ScBufferAppend(s->hello,&hello,sizeof(hello));
    if(status<0)goto fail;
    s->desc=*d;
    if(!s->desc.budget_bytes)s->desc.budget_bytes=SH_DEFAULT_BUDGET;
    s->channel=c.channel;
    c.channel=0;
    s->info.size=sizeof(s->info);
    s->info.version=1;
    s->info.state=SC_SHELL_CONNECTING;
    s->info.host=c.host;
    s->info.peer=peer;
    *out=s;
    return 0;
    fail:if(c.channel)ScIpcClose(c.channel);
    if(s)sh_release(&s->base);
    return sh_cause(status,40,0);
}
ScStatus ScShellQuery(const ScShell *s,ScShellInfo *out) {
    if(!sh_valid(s,SH_CLIENT))return SC_SHELL_E_STALE;
    return sh_output(out,&s->info,sizeof(s->info));
}
void ScShellClose(ScShell *s) {
    if(!sh_valid(s,SH_CLIENT))return;
    /* Close可从通知内重入；当前Step持depth直到它不再访问对象。 */ sh_enter(&s->base);
    s->base.closing=1;
    if(s->info.state!=SC_SHELL_DISCONNECTED)sh_client_disconnect(s,SC_SHELL_E_CANCELLED);
    sh_release(&s->base);
    sh_leave(&s->base);
}
static ScStatus sh_request(ScShell *s,ScU32 opcode,ScBuffer *body,ScU32 epoch,ScU32 effect,ScShellRequest **out) {
    ScShellRequest *r;
    ScWideCounter next;
    const unsigned char *p;
    ScU32 n;
    ScStatus status;
    if(!sh_valid(s,SH_CLIENT)||s->info.state==SC_SHELL_DISCONNECTED)return SC_SHELL_E_STALE;
    if(!out)return SC_SHELL_E_INVALID;
    status=sh_buffer_data(body,&p,&n);
    if(status<0)return status;
    if(n>s->desc.budget_bytes||s->info.queued_bytes>s->desc.budget_bytes-n||s->info.pending_requests==0xFFFFFFFFu)return SC_SHELL_E_RANGE;
    next=s->sequence;
    status=sh_next_counter(&next);
    if(status<0)return status;
    r=(ScShellRequest *)sh_new(sizeof(*r),SH_REQUEST,sh_request_dispose);
    if(!r)return SC_SHELL_E_MEMORY;
    status=ScBufferRetain(body);
    if(status<0) {
        sh_release(&r->base);
        return sh_cause(status,41,0);
    }
    r->body=body;
    r->owner=s;
    r->info.size=sizeof(r->info);
    r->info.version=1;
    r->info.state=SC_SHELL_REQUEST_QUEUED;
    r->info.opcode=opcode;
    r->info.sequence=next;
    r->info.epoch=epoch;
    r->info.side_effect=effect;
    ++r->base.references;
    s->sequence=next;
    if(s->tail)s->tail->next=r;
    else s->head=r;
    s->tail=r;
    ++s->info.pending_requests;
    s->info.queued_bytes+=n;
    *out=r;
    return 0;
}
static ScStatus sh_client_reply(ScShell *s,const ScShellWireHeader *h,const unsigned char *body) {
    ScU32 complete;
    ScStatus status;
    ScShellRequest *r=s->head;
    if(h->opcode==SHW_CATALOG_CHANGED) {
        if(h->flags!=SHW_EVENT||h->request_lo||h->request_hi||h->total_bytes||h->status)return SC_SHELL_E_PROTOCOL;
        s->info.epoch=h->state_epoch;
        return sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_CHANGED,s->info.epoch,0,0,0,0,0,0);
    }
    if(s->info.state==SC_SHELL_CONNECTING) {
        ScShellWireServiceHello hello;
        if(h->opcode!=SHW_SERVICE_HELLO_ACK||h->flags!=SHW_REPLY||h->request_lo||h->request_hi||h->total_bytes!=sizeof(hello)||h->status)return SC_SHELL_E_PROTOCOL;
        memcpy(&hello,body,sizeof(hello));
        if(hello.size!=sizeof(hello)||hello.version!=1||hello.protocol!=1||hello.host_epoch!=s->info.host.host_epoch||hello.max_frame_bytes!=SHW_FRAME_BYTES||hello.flags)return SC_SHELL_E_PROTOCOL;
        s->info.state=SC_SHELL_CONNECTED;
        s->info.epoch=hello.catalog_epoch;
        ScBufferRelease(s->hello);
        s->hello=0;
        return sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_CONNECTED,s->info.epoch,0,0,0,0,0,0);
    }
    if(!r||r->info.state!=SC_SHELL_REQUEST_SENT||h->opcode!=r->info.opcode||!(h->flags&SHW_REPLY)||(h->flags&SHW_EVENT)|| h->request_lo!=r->info.sequence.lo||h->request_hi!=r->info.sequence.hi||h->status>0)return SC_SHELL_E_PROTOCOL;
    if(r->result&&(h->total_bytes!=r->rx_offset||h->state_epoch!=s->info.epoch||h->status!=r->info.status))return SC_SHELL_E_PROTOCOL;
    r->rx_offset=h->total_bytes;
    status=sh_collect(&r->result,h,body,s->desc.budget_bytes,&complete);
    if(status<0)return status;
    r->info.status=h->status;
    s->info.epoch=h->state_epoch;
    if(complete) {
        const unsigned char *p,*q;
        ScU32 n,queued;
        sh_buffer_data(r->result,&p,&n);
        sh_buffer_data(r->body,&q,&queued);
        if(h->status<0) {
            if(n!=sizeof(r->error))return SC_SHELL_E_PROTOCOL;
            memcpy(&r->error,p,sizeof(r->error));
            if(r->error.size!=sizeof(r->error)||r->error.version!=1||r->error.code!=h->status||r->error.reserved[0]||r->error.reserved[1]||r->error.reserved[2])return SC_SHELL_E_PROTOCOL;
            sh_error=r->error;
        }
        r->info.result_bytes=n;
        r->info.epoch=h->state_epoch;
        r->info.state=SC_SHELL_REQUEST_COMPLETE;
        r->owner=0;
        s->head=r->next;
        r->next=0;
        if(!s->head)s->tail=0;
        --s->info.pending_requests;
        s->info.queued_bytes-=queued;
        status=sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_REPLY,h->state_epoch,r,0,h->status,p,n,0);
        sh_release(&r->base);
        return status;
    }
    return 0;
}
ScStatus ScShellStep(ScShell *s,ScU32 timeout) {
    ScStatus status=0;
    ScU32 i,progress=0;
    if(!sh_valid(s,SH_CLIENT))return SC_SHELL_E_STALE;
    if(s->base.depth)return SC_SHELL_E_BUSY;
    if(s->info.state==SC_SHELL_DISCONNECTED)return SC_SHELL_E_STALE;
    sh_enter(&s->base);
    s->base.notification_error=0;
    for(i=0;i<SH_SERVICE_BATCH&&s->channel;++i) {
        ScShellWireHeader h;
        const unsigned char *body;
        if(s->info.state==SC_SHELL_CONNECTING&&!s->hello_sent) {
            ScU32 offset=0;
            memset(&h,0,sizeof(h));
            h.opcode=SHW_SERVICE_HELLO;
            status=sh_frame_send(s->channel,&h,s->hello,&offset,s->scratch);
            if(status<0)break;
            if(!status) {
                s->hello_sent=1;
                progress=1;
            }
        } else if(s->info.state==SC_SHELL_CONNECTED&&s->head&&s->head->info.state==SC_SHELL_REQUEST_QUEUED) {
            ScShellRequest *r=s->head;
            ScU32 before=r->offset;
            memset(&h,0,sizeof(h));
            h.opcode=r->info.opcode;
            h.request_lo=r->info.sequence.lo;
            h.request_hi=r->info.sequence.hi;
            h.state_epoch=r->info.epoch;
            status=sh_frame_send(s->channel,&h,r->body,&r->offset,s->scratch);
            if(status<0)break;
            if(before!=r->offset) {
                r->info.enqueued=1;
                progress=1;
            }
            if(!status) {
                r->info.state=SC_SHELL_REQUEST_SENT;
                r->info.enqueued=1;
            }
        }
        status=sh_frame_receive(s->channel,&s->info.peer,s->scratch,&h,&body);
        if(status==SC_SHELL_PENDING) {
            status=0;
            break;
        }
        if(status==SC_SHELL_END) {
            status=SC_SHELL_E_IO;
            break;
        }
        if(status<0)break;
        s->info.received_bytes+=h.body_bytes;
        progress=1;
        status=sh_client_reply(s,&h,body);
        if(status<0)break;
    }
    if(status<0&&s->channel)sh_client_disconnect(s,status);
    if(status>=0&&s->channel) {
        ScBuffer *outgoing=s->info.state==SC_SHELL_CONNECTING&&!s->hello_sent?s->hello: (s->head&&s->head->info.state==SC_SHELL_REQUEST_QUEUED?s->head->body:0);
        ScU32 offset=outgoing==s->hello?0:(s->head?s->head->offset:0);
        if(sh_io_ready(s->channel,outgoing,offset,0))status=SC_SHELL_READY;
    }
    if(status>=0&&status!=SC_SHELL_READY&&!progress&&timeout&&s->channel) {
        ScU32 flags=SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED,minimum=1;
        if((s->info.state==SC_SHELL_CONNECTING&&!s->hello_sent)||(s->head&&s->head->info.state==SC_SHELL_REQUEST_QUEUED))flags|=SC32_BROKER_READY_WRITE;
        if(flags&SC32_BROKER_READY_WRITE)minimum=sh_frame_bytes(s->info.state==SC_SHELL_CONNECTING?s->hello:s->head->body,s->info.state==SC_SHELL_CONNECTING?0:s->head->offset);
        status=sh_wait_min(s->channel,flags,minimum,timeout);
        if(status>=0)status=SC_SHELL_PENDING;
    }
    if(status>=0&&s->base.notification_error<0)status=s->base.notification_error;
    sh_leave(&s->base);
    return status;
}
ScStatus ScShellQueryCatalog(ScShell *s,ScU32 epoch,ScShellRequest **out) {
    ScShellWireQuery q;
    ScBuffer *b=0;
    ScStatus status;
    memset(&q,0,sizeof(q));
    q.size=sizeof(q);
    q.version=1;
    q.expected_epoch=epoch;
    status=ScBufferCreate(sizeof(q),&b);
    if(status>=0)status=ScBufferAppend(b,&q,sizeof(q));
    if(status>=0)status=sh_request(s,SHW_CATALOG_QUERY,b,epoch,0,out);
    ScBufferRelease(b);
    return sh_cause(status,41,0);
}
ScStatus ScShellRegisterApplication(ScShell *s,const ScShellApplicationDesc *d,ScU32 epoch,ScShellRequest **out) {
    ScShellApplication *a=0;
    ScBuffer *b=0;
    ScStatus status=sh_application_new(d,&a);
    if(status>=0&&d->layer==SC_SHELL_LAYER_SYSTEM)status=SC_SHELL_E_ACCESS;
    if(status>=0)status=sh_application_encode(a,&b);
    if(status>=0)status=sh_request(s,SHW_REGISTER,b,epoch,1,out);
    ScBufferRelease(b);
    ScShellApplicationRelease(a);
    return sh_cause(status,41,0);
}
ScStatus ScShellUnregisterApplication(ScShell *s,const char *id,ScU32 layer,ScU32 epoch,ScShellRequest **out) {
    ScShellWireName h;
    ScBuffer *b=0;
    ScStatus status;
    if(!sh_name(id)||layer<1||layer>2)return SC_SHELL_E_INVALID;
    memset(&h,0,sizeof(h));
    h.size=sizeof(h);
    h.version=1;
    h.layer=layer;
    status=ScBufferCreate(sizeof(h),&b);
    if(status>=0)status=ScBufferResize(b,sizeof(h));
    if(status>=0)status=sh_append_string(b,id,&h.id_offset,&h.id_bytes);
    if(status>=0)status=ScBufferWrite(b,0,&h,sizeof(h));
    if(status>=0)status=sh_request(s,SHW_UNREGISTER,b,epoch,1,out);
    ScBufferRelease(b);
    return sh_cause(status,41,0);
}
ScStatus ScShellSetAssociation(ScShell *s,const ScShellAssociationDesc *d,ScU32 epoch,ScShellRequest **out) {
    ShAssociation a;
    ScBuffer *b=0;
    ScStatus status=sh_record(d,sizeof(*d));
    if(status<0)return status;
    if(d->layer<1||d->layer>2||d->flags||!sh_name(d->application)||sh_text(d->verb,255,1)<0)return SC_SHELL_E_INVALID;
    memset(&a,0,sizeof(a));
    status=sh_type(d->type,&a.type);
    if(status<0)return status;
    a.application=(char *)d->application;
    a.verb=(char *)(d->verb?d->verb:"");
    a.layer=d->layer;
    a.revision=d->expected_revision;
    status=sh_association_encode(&a,&b);
    if(status>=0)status=sh_request(s,SHW_ASSOCIATION,b,epoch,1,out);
    free(a.type);
    ScBufferRelease(b);
    return sh_cause(status,41,0);
}
ScStatus ScShellApplyLayout(ScShell *s,const ScShellLayout *l,ScU32 epoch,ScShellRequest **out) {
    ScBuffer *b=0;
    ScStatus status;
    if(!sh_valid(l,SH_LAYOUT))return SC_SHELL_E_STALE;
    status=sh_layout_encode(l,&b);
    if(status>=0)status=sh_request(s,SHW_LAYOUT,b,epoch,1,out);
    ScBufferRelease(b);
    return status;
}
ScStatus ScShellRequestQuery(const ScShellRequest *r,ScShellRequestInfo *out) {
    if(!sh_valid(r,SH_REQUEST))return SC_SHELL_E_STALE;
    return sh_output(out,&r->info,sizeof(r->info));
}
ScStatus ScShellRequestError(const ScShellRequest *r,ScShellErrorInfo *out) {
    if(!sh_valid(r,SH_REQUEST))return SC_SHELL_E_STALE;
    if(r->info.state<SC_SHELL_REQUEST_COMPLETE)return SC_SHELL_PENDING;
    if(r->info.status>=0)return SC_SHELL_E_NOT_FOUND;
    return sh_output(out,&r->error,sizeof(r->error));
}
ScStatus ScShellRequestCopy(const ScShellRequest *r,ScBuffer **out) {
    if(!sh_valid(r,SH_REQUEST))return SC_SHELL_E_STALE;
    if(!out)return SC_SHELL_E_INVALID;
    if(r->info.state!=SC_SHELL_REQUEST_COMPLETE)return r->info.state>=SC_SHELL_REQUEST_UNKNOWN?r->info.status:SC_SHELL_PENDING;
    if(r->info.status<0)return r->info.status;
    return sh_buffer_copy(r->result,out);
}
ScStatus ScShellRequestCatalog(const ScShellRequest *r,ScShellCatalog **out) {
    const unsigned char *p;
    ScU32 n;
    ScStatus s;
    if(!sh_valid(r,SH_REQUEST))return SC_SHELL_E_STALE;
    if(!out||r->info.opcode!=SHW_CATALOG_QUERY)return SC_SHELL_E_INVALID;
    if(r->info.state!=SC_SHELL_REQUEST_COMPLETE)return r->info.state>=SC_SHELL_REQUEST_UNKNOWN?r->info.status:SC_SHELL_PENDING;
    if(r->info.status<0)return r->info.status;
    s=sh_buffer_data(r->result,&p,&n);
    if(s<0)return s;
    return sh_catalog_decode(p,n,out);
}
void ScShellRequestClose(ScShellRequest *r) {
    if(sh_valid(r,SH_REQUEST))sh_release(&r->base);
}
