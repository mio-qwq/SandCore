static void sh_service_client_free(ScShellService *s,ShClient *c) {
    if(c->store&&(c->store->info.flags&SC_SHELL_STORE_READY)&&s->ready_stores)--s->ready_stores;
    if(c->send) {
        const unsigned char *p;
        ScU32 bytes;
        if(sh_buffer_data(c->send,&p,&bytes)>=0&&bytes>=c->offset&&s->info.queued_bytes>=bytes-c->offset)s->info.queued_bytes-=bytes-c->offset;
    }
    if(c->channel)ScIpcClose(c->channel);
    ScBufferRelease(c->receive);
    ScBufferRelease(c->send);
    ScBufferRelease(c->transaction_reply);
    ScShellStoreClose(c->store);
    ScShellCatalogRelease(c->candidate);
    free(c);
}
static void sh_service_dispose(ShObject *base) {
    ScShellService *s=(ScShellService *)base;
    ShClient *c=s->clients;
    while(c) {
        ShClient *next=c->next;
        sh_service_client_free(s,c);
        c=next;
    }
    ScShellCatalogRelease(s->catalog);
    free(s->store);
    free(s->scratch);
    if(!sh_quiet&&s->desc.context_drop)s->desc.context_drop(s->desc.context,0);
}
static ScU32 sh_service_transient(const ScShellCatalog *c) {
    ScU32 i,n=0;
    for(i=0;i<c->app_count;++i)if(c->apps[i]->info.layer==SC_SHELL_LAYER_SESSION)++n;
    for(i=0;i<c->association_count;++i)if(c->associations[i]->layer==SC_SHELL_LAYER_SESSION)++n;
    return n;
}
ScStatus ScShellServiceCreate(const ScShellServiceDesc *d,ScShellService **out) {
    ScShellService *s=0;
    ScShellHostInfo host;
    ScBrokerIdentity self;
    ScIpcInfo endpoint;
    ScStatus status=sh_record(d,sizeof(*d));
    if(status<0)return status;
    if(!out||!sh_valid(d->catalog,SH_CATALOG)||!d->endpoint||!d->host_epoch||d->reserved[0]||d->reserved[1]|| (d->frame_budget_bytes&&d->frame_budget_bytes<SHW_FRAME_BYTES))return SC_SHELL_E_INVALID;
    status=sh_self(&self);
    if(status<0)return status;
    memset(&host,0,sizeof(host));
    host.size=sizeof(host);
    host.version=1;
    status=ScShellHostQuery(&host);
    if(status<0)return sh_cause(status,50,0);
    if(host.pid!=self.pid||host.generation!=self.generation||host.session!=self.session||host.host_epoch!=d->host_epoch|| (host.state!=SC32_SHELL_STARTING&&host.state!=SC32_SHELL_PREPARED&&host.state!=SC32_SHELL_ACTIVE)|| (host.endpoint&&host.endpoint!=d->endpoint))return SC_SHELL_E_ACCESS;
    memset(&endpoint,0,sizeof(endpoint));
    endpoint.size=sizeof(endpoint);
    endpoint.version=1;
    status=ScIpcQuery(d->endpoint,&endpoint);
    if(status<0)return sh_cause(status,50,0);
    if(endpoint.kind!=SC32_IPC_KIND_ENDPOINT||endpoint.state!=SC32_IPC_STATE_LISTENING||endpoint.max_frame_bytes<SHW_FRAME_BYTES)return SC_SHELL_E_INVALID;
    status=sh_pin(d->procedure,d->context_drop);
    if(status<0)return status;
    s=(ScShellService *)sh_new(sizeof(*s),SH_SERVICE,sh_service_dispose);
    if(!s)return SC_SHELL_E_MEMORY;
    s->scratch=(unsigned char *)malloc(SHW_FRAME_BYTES);
    if(!s->scratch) {
        status=SC_SHELL_E_MEMORY;
        goto fail;
    }
    if(d->account_store&&*d->account_store) {
        status=sh_absolute(d->account_store,&s->store);
        if(status<0)goto fail;
    }
    status=ScShellCatalogRetain(d->catalog);
    if(status<0)goto fail;
    s->catalog=d->catalog;
    s->self=self;
    s->host=host;
    s->desc=*d;
    if(!s->desc.frame_budget_bytes)s->desc.frame_budget_bytes=SH_DEFAULT_BUDGET;
    s->info.size=sizeof(s->info);
    s->info.version=1;
    s->info.epoch=s->catalog->epoch;
    s->info.transient_entries=sh_service_transient(s->catalog);
    *out=s;
    return 0;
    fail:sh_release(&s->base);
    return sh_cause(status,50,0);
}
static ScStatus sh_service_reply(ScShellService *s,ShClient *c,const ScShellWireHeader *request,ScStatus status,ScBuffer *body) {
    ScStatus x;
    const unsigned char *p;
    ScU32 bytes;
    if(c->send)return SC_SHELL_E_PROTOCOL;
    memset(&c->output,0,sizeof(c->output));
    c->output.opcode=request->opcode;
    c->output.flags=SHW_REPLY;
    c->output.request_lo=request->request_lo;
    c->output.request_hi=request->request_hi;
    c->output.state_epoch=s->catalog->epoch;
    c->output.status=status;
    if(status<0) {
        ScShellErrorInfo error;
        if(sh_error.code!=status)sh_fail(status,status,request->opcode,0);
        error=sh_error;
        error.epoch=s->catalog->epoch;
        x=ScBufferCreate(sizeof(error),&c->send);
        if(x>=0)x=ScBufferAppend(c->send,&error,sizeof(error));
        if(x<0)return sh_cause(x,51,0);
    } else if(body) {
        x=ScBufferRetain(body);
        if(x<0)return sh_cause(x,51,0);
        c->send=body;
    } else {
        x=ScBufferCreate(0,&c->send);
        if(x<0)return sh_cause(x,51,0);
    }
    c->offset=0;
    x=sh_buffer_data(c->send,&p,&bytes);
    if(x<0)return x;
    if(bytes>0xFFFFFFFFu-s->info.queued_bytes)return SC_SHELL_E_RANGE;
    s->info.queued_bytes+=bytes;
    return 0;
}
static ScStatus sh_service_commit(ScShellService *s,ScShellCatalog *candidate,ScU32 account) {
    if(account)return s->store?SC_SHELL_PENDING:SC_SHELL_E_ACCESS;
    if(s->catalog->commit_lock)return SC_SHELL_E_BUSY;
    sh_catalog_swap(s->catalog,candidate);
    s->info.epoch=s->catalog->epoch;
    s->info.transient_entries=sh_service_transient(s->catalog);
    return 0;
}
static ScStatus sh_service_save_begin(ScShellService *s,ShClient *c,const ScShellWireHeader *h,ScShellCatalog *candidate,ScU32 token,ScU32 revision) {
    ScBuffer *body=0,*reply=0;
    ScShellStore *store=0;
    ScShellWireReply result;
    ScStatus status;
    memset(&result,0,sizeof(result));
    result.size=sizeof(result);
    result.version=1;
    result.epoch=candidate->epoch;
    result.token=token;
    result.revision=revision;
    status=ScBufferCreate(sizeof(result),&reply);
    if(status>=0)status=ScBufferAppend(reply,&result,sizeof(result));
    if(status>=0)status=sh_catalog_account_buffer(candidate,&body);
    if(status>=0)status=sh_store_begin_buffer(s->catalog,body,s->store,s->catalog->store_revision,candidate->epoch,&store);
    if(status>=0)status=ScShellCatalogRetain(candidate);
    if(status<0) {
        ScShellStoreClose(store);
        ScBufferRelease(body);
        ScBufferRelease(reply);
        return sh_cause(status,52,0);
    }
    c->store=store;
    c->candidate=candidate;
    c->transaction=*h;
    c->transaction_reply=reply;
    ++s->ready_stores;
    ScBufferRelease(body);
    return 0;
}
static ScStatus sh_service_save_step(ScShellService *s,ShClient *c,ScU32 *progress) {
    ScShellStore *store=c->store;
    ScStatus status;
    ScU32 was_ready=store->info.flags&SC_SHELL_STORE_READY;
    status=ScShellStoreStep(store,0);
    if(was_ready&&!(store->info.flags&SC_SHELL_STORE_READY)) {
        if(s->ready_stores)--s->ready_stores;
    } else if(!was_ready&&(store->info.flags&SC_SHELL_STORE_READY))++s->ready_stores;
    if(status==SC_SHELL_PENDING) {
        *progress=1;
        return 0;
    }
    if(status>=0||(store->info.flags&SC_SHELL_STORE_DURABLE)) {
        c->candidate->store_revision=store->info.new_revision;
        sh_catalog_swap(s->catalog,c->candidate);
        s->info.epoch=s->catalog->epoch;
        s->info.transient_entries=sh_service_transient(s->catalog);
    } else sh_error=store->error;
    {
        ScStatus queued=sh_service_reply(s,c,&c->transaction,status,status>=0?c->transaction_reply:0);
        ScBufferRelease(c->transaction_reply);
        c->transaction_reply=0;
        ScShellCatalogRelease(c->candidate);
        c->candidate=0;
        ScShellStoreClose(store);
        c->store=0;
        if(queued<0)return queued;
    }
    if(status>=0)sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_CHANGED,s->catalog->epoch,0,0,0,&c->peer,sizeof(c->peer),0);
    *progress=1;
    return 0;
}
static ScStatus sh_service_request(ScShellService *s,ShClient *c,const ScShellWireHeader *h,const ScBuffer *input) {
    const unsigned char *p;
    ScU32 bytes,token=0,revision=0;
    ScStatus status;
    ScBuffer *reply=0;
    ScShellCatalog *candidate=0;
    status=sh_buffer_data(input,&p,&bytes);
    if(status<0)return status;
    if(!c->hello) {
        ScShellWireServiceHello hello,answer;
        ScShellWireHeader response=*h;
        if(h->opcode!=SHW_SERVICE_HELLO||h->request_lo||h->request_hi||h->flags||h->status||h->state_epoch||bytes!=sizeof(hello))return SC_SHELL_E_PROTOCOL;
        memcpy(&hello,p,sizeof(hello));
        if(hello.size!=sizeof(hello)||hello.version!=1||hello.protocol!=1||hello.host_epoch!=s->host.host_epoch||hello.catalog_epoch||hello.max_frame_bytes!=SHW_FRAME_BYTES||hello.flags)return SC_SHELL_E_PROTOCOL;
        memset(&answer,0,sizeof(answer));
        answer.size=sizeof(answer);
        answer.version=1;
        answer.protocol=1;
        answer.features=SHW_SERVICE_FEATURE_CATALOG|SHW_SERVICE_FEATURE_ASSOCIATION|SHW_SERVICE_FEATURE_LAYOUT;
        if(s->store)answer.features|=SHW_SERVICE_FEATURE_ACCOUNT_STORE;
        answer.host_epoch=s->host.host_epoch;
        answer.catalog_epoch=s->catalog->epoch;
        answer.max_frame_bytes=SHW_FRAME_BYTES;
        status=ScBufferCreate(sizeof(answer),&reply);
        if(status>=0)status=ScBufferAppend(reply,&answer,sizeof(answer));
        response.opcode=SHW_SERVICE_HELLO_ACK;
        if(status>=0)status=sh_service_reply(s,c,&response,0,reply);
        ScBufferRelease(reply);
        if(status>=0) {
            c->hello=1;
            c->changed_epoch=s->catalog->epoch;
        }
        return status;
    }
    if(h->flags||h->status||(!h->request_lo&&!h->request_hi))return SC_SHELL_E_PROTOCOL;
    if(h->opcode==SHW_CATALOG_QUERY) {
        ScShellWireQuery query;
        if(bytes!=sizeof(query)) {
            status=SC_SHELL_E_PROTOCOL;
            goto respond;
        }
        memcpy(&query,p,sizeof(query));
        if(query.size!=sizeof(query)||query.version!=1||query.flags||query.expected_epoch!=h->state_epoch) {
            status=SC_SHELL_E_PROTOCOL;
            goto respond;
        }
        if(query.expected_epoch&&query.expected_epoch!=s->catalog->epoch) {
            status=SC_SHELL_E_STALE;
            goto respond;
        }
        status=ScShellCatalogExport(s->catalog,&reply);
        goto respond;
    }
    if(h->state_epoch!=s->catalog->epoch) {
        status=SC_SHELL_E_STALE;
        goto respond;
    }
    status=sh_catalog_clone(s->catalog,&candidate);
    if(status<0)goto respond;
    if(h->opcode==SHW_REGISTER) {
        ScShellApplication *a=0;
        status=sh_application_decode(p,bytes,&a);
        if(status>=0) {
            if(a->info.layer<1||a->info.layer>2||a->info.token||a->info.revision||a->info.owner_pid||a->info.owner_generation||a->info.uid||a->info.gid)status=SC_SHELL_E_PROTOCOL;
            else {
                ScShellApplication *old=0;
                ScStatus found=ScShellCatalogFind(candidate,a->desc.id,&old);
                if(found>=0&&a->info.layer==SC_SHELL_LAYER_SESSION&&old->info.layer==SC_SHELL_LAYER_SESSION&&old->info.owner_pid&& (old->info.owner_pid!=c->peer.pid||old->info.owner_generation!=c->peer.generation))status=SC_SHELL_E_ACCESS;
                ScShellApplicationRelease(old);
            }
            if(status>=0)status=ScShellCatalogRegister(candidate,&a->desc,candidate->epoch,&token);
            if(status>=0) {
                ScU32 i;
                for(i=0;i<candidate->app_count;++i)if(candidate->apps[i]->info.token==token) {
                    ScShellApplication *created=candidate->apps[i];
                    created->info.uid=c->peer.uid;
                    created->info.gid=c->peer.gid;
                    revision=created->info.revision;
                    if(a->info.layer==SC_SHELL_LAYER_SESSION) {
                        created->info.owner_pid=c->peer.pid;
                        created->info.owner_generation=c->peer.generation;
                    }
                    break;
                }
                status=sh_service_commit(s,candidate,a->info.layer==SC_SHELL_LAYER_ACCOUNT);
            }
        }
        ScShellApplicationRelease(a);
    } else if(h->opcode==SHW_UNREGISTER) {
        ScShellWireName name;
        const char *id=0;
        ScU32 cursor=sizeof(name),i;
        if(bytes<sizeof(name))status=SC_SHELL_E_PROTOCOL;
        else {
            memcpy(&name,p,sizeof(name));
            if(name.size!=sizeof(name)||name.version!=1||name.layer<1||name.layer>2||name.flags||name.expected_revision||name.reserved)status=SC_SHELL_E_PROTOCOL;
            else {
                status=sh_decode_string(p,bytes,&cursor,name.id_offset,name.id_bytes,255,&id);
                if(status>=0&&cursor!=bytes)status=SC_SHELL_E_PROTOCOL;
            }
            if(status>=0) {
                for(i=0;i<candidate->app_count;++i)if(candidate->apps[i]->info.layer==name.layer&&!strcmp(candidate->apps[i]->desc.id,id))break;
                if(i==candidate->app_count)status=SC_SHELL_E_NOT_FOUND;
                else if(name.layer==SC_SHELL_LAYER_SESSION&&(candidate->apps[i]->info.owner_pid!=c->peer.pid||candidate->apps[i]->info.owner_generation!=c->peer.generation))status=SC_SHELL_E_ACCESS;
            }
            if(status>=0)status=ScShellCatalogUnregister(candidate,id,name.layer,candidate->epoch);
            if(status>=0)status=sh_service_commit(s,candidate,name.layer==SC_SHELL_LAYER_ACCOUNT);
        }
    } else if(h->opcode==SHW_ASSOCIATION) {
        ShAssociation *a=0;
        ScShellAssociationDesc d;
        status=sh_association_decode(p,bytes,&a);
        if(status>=0) {
            if(a->layer<1||a->layer>2)status=SC_SHELL_E_ACCESS;
            else {
                ScU32 i;
                for(i=0;i<candidate->association_count;++i) {
                    ShAssociation *old=candidate->associations[i];
                    if(old->layer==SC_SHELL_LAYER_SESSION&&a->layer==SC_SHELL_LAYER_SESSION&&!strcmp(old->type,a->type)&&!strcmp(old->verb,a->verb)&& (old->owner_pid!=c->peer.pid||old->owner_generation!=c->peer.generation)) {
                        status=SC_SHELL_E_ACCESS;
                        break;
                    }
                }
                memset(&d,0,sizeof(d));
                d.size=sizeof(d);
                d.version=1;
                d.layer=a->layer;
                d.type=a->type;
                d.application=a->application;
                d.verb=a->verb;
                d.expected_revision=a->revision;
                if(status>=0)status=ScShellCatalogSetAssociation(candidate,&d,candidate->epoch);
                if(status>=0&&a->layer==SC_SHELL_LAYER_SESSION)for(i=0;i<candidate->association_count;++i) {
                    ShAssociation *created=candidate->associations[i];
                    if(created->layer==a->layer&&!strcmp(created->type,a->type)&&!strcmp(created->verb,a->verb)) {
                        created->owner_pid=c->peer.pid;
                        created->owner_generation=c->peer.generation;
                        break;
                    }
                }
                if(status>=0)status=sh_service_commit(s,candidate,a->layer==SC_SHELL_LAYER_ACCOUNT);
            }
        }
        sh_association_release(a);
    } else if(h->opcode==SHW_LAYOUT) {
        ScShellLayout *layout=0;
        status=sh_layout_decode(p,bytes,&layout);
        if(status>=0)status=ScShellCatalogApplyLayout(candidate,layout,candidate->epoch);
        if(status>=0)status=sh_service_commit(s,candidate,1);
        ScShellLayoutRelease(layout);
    } else status=SC_SHELL_E_PROTOCOL;
    if(status==SC_SHELL_PENDING) {
        status=sh_service_save_begin(s,c,h,candidate,token,revision);
        ScShellCatalogRelease(candidate);
        return status;
    }
    if(status>=0) {
        ScShellWireReply r;
        memset(&r,0,sizeof(r));
        r.size=sizeof(r);
        r.version=1;
        r.epoch=s->catalog->epoch;
        r.token=token;
        r.revision=revision;
        status=ScBufferCreate(sizeof(r),&reply);
        if(status>=0)status=ScBufferAppend(reply,&r,sizeof(r));
        /* commit之后回复OOM仍不重放；当前连接关闭会让client看到UNKNOWN。 */ if(status<0) {
            ScShellCatalogRelease(candidate);
            ScBufferRelease(reply);
            return sh_cause(status,52,0);
        }
        sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_CHANGED,s->catalog->epoch,0,0,0,&c->peer,sizeof(c->peer),0);
    }
    respond:ScShellCatalogRelease(candidate);
    if(status<0&&status>SC_SHELL_E_INVALID)status=sh_cause(status,52,0);
    {
        ScStatus sent=sh_service_reply(s,c,h,status,reply);
        ScBufferRelease(reply);
        return sent;
    }
}
static ScStatus sh_service_accept(ScShellService *s) {
    ScIpcChannel channel=0;
    ScBrokerIdentity peer,actual;
    ShClient *c;
    ScStatus status;
    memset(&peer,0,sizeof(peer));
    peer.size=sizeof(peer);
    peer.version=1;
    status=ScIpcAccept(s->desc.endpoint,&channel,&peer);
    if(status==SC32_E_AGAIN)return SC_SHELL_PENDING;
    if(status<0)return sh_cause(status,53,0);
    memset(&actual,0,sizeof(actual));
    actual.size=sizeof(actual);
    actual.version=1;
    status=ScIpcPeer(channel,&actual);
    if(status<0||!sh_same_identity(&peer,&actual)||!sh_subject(&peer,&s->self)) {
        ScIpcClose(channel);
        return status<0?sh_cause(status,53,0):0;
    }
    c=(ShClient *)calloc(1,sizeof(*c));
    if(!c) {
        ScIpcClose(channel);
        return SC_SHELL_E_MEMORY;
    }
    c->channel=channel;
    c->peer=peer;
    c->previous=s->tail;
    if(s->tail)s->tail->next=c;
    else s->clients=c;
    s->tail=c;
    if(!s->cursor)s->cursor=c;
    ++s->info.clients;
    return 0;
}
static ScStatus sh_service_io(ScShellService *s,ShClient *c,ScU32 *progress) {
    ScStatus status;
    ScShellWireHeader h;
    const unsigned char *body;
    ScU32 complete;
    if(c->store) {
        status=sh_service_save_step(s,c,progress);
        if(status<0||c->store)return status;
    }
    if(c->send) {
        ScU32 before=c->offset;
        status=sh_frame_send(c->channel,&c->output,c->send,&c->offset,s->scratch);
        if(status<0)return status;
        if(c->offset!=before) {
            *progress=1;
            s->info.queued_bytes-=c->offset-before;
        }
        if(!status) {
            ScBufferRelease(c->send);
            c->send=0;
            c->offset=0;
        }else return 0;
    }
    if(c->hello&&c->changed_epoch!=s->catalog->epoch) {
        ScShellWireHeader changed;
        ScU32 offset=0;
        memset(&changed,0,sizeof(changed));
        changed.opcode=SHW_CATALOG_CHANGED;
        changed.flags=SHW_EVENT;
        changed.state_epoch=s->catalog->epoch;
        status=sh_frame_send(c->channel,&changed,0,&offset,s->scratch);
        if(status<0)return status;
        if(!status) {
            c->changed_epoch=s->catalog->epoch;
            *progress=1;
        }else return 0;
    }
    status=sh_frame_receive(c->channel,&c->peer,s->scratch,&h,&body);
    if(status==SC_SHELL_PENDING)return 0;
    if(status==SC_SHELL_END)return SC_SHELL_END;
    if(status<0)return status;
    if(c->receive&&(h.opcode!=c->input.opcode||h.request_lo!=c->input.request_lo||h.request_hi!=c->input.request_hi||h.total_bytes!=c->input.total_bytes||h.state_epoch!=c->input.state_epoch||h.status!=c->input.status||(h.flags&~SHW_MORE)!=(c->input.flags&~SHW_MORE)))return SC_SHELL_E_PROTOCOL;
    if(!c->receive) {
        ScWideCounter sequence;
        sequence.lo=h.request_lo;
        sequence.hi=h.request_hi;
        if(c->hello&&!sh_counter_before(&c->sequence,&sequence))return SC_SHELL_E_PROTOCOL;
        c->input=h;
    }
    status=sh_collect(&c->receive,&h,body,s->desc.frame_budget_bytes,&complete);
    if(status<0)return status;
    ++s->info.received_frames;
    *progress=1;
    if(complete) {
        ScBuffer *input=c->receive;
        c->receive=0;
        c->sequence.lo=h.request_lo;
        c->sequence.hi=h.request_hi;
        h.flags&=~SHW_MORE;
        h.offset=0;
        h.body_bytes=h.total_bytes;
        status=sh_service_request(s,c,&h,input);
        ScBufferRelease(input);
        return status;
    }
    return 0;
}
static void sh_service_sweep(ScShellService *s) {
    ScMonotonicClock clock;
    ScU32 checked=0;
    memset(&clock,0,sizeof(clock));
    clock.size=sizeof(clock);
    clock.version=1;
    if(!s->info.transient_entries||s->catalog->commit_lock)return;
    if(ScSystemMonotonic(&clock)<0||sh_counter_before(&clock.ticks,&s->next_sweep))return;
    sh_deadline(&s->next_sweep,&clock.ticks,100);
    while(checked<32&&s->catalog->app_count) {
        ScShellApplication *a;
        ScProcessRef ref;
        ScProcessInfo info;
        ScStatus status;
        if(s->sweep_app>=s->catalog->app_count)s->sweep_app=0;
        a=s->catalog->apps[s->sweep_app];
        ++checked;
        if(a->info.layer!=SC_SHELL_LAYER_SESSION) {
            ++s->sweep_app;
            continue;
        }
        memset(&ref,0,sizeof(ref));
        ref.size=sizeof(ref);
        ref.version=1;
        ref.pid=a->info.owner_pid;
        ref.generation=a->info.owner_generation;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        status=ScProcessQuery(&ref,&info);
        if(status==SC32_E_STALE||(status>=0&&info.state==SC32_PROCESS_EXITING)) {
            /* 实际PID/gen退出才撤SID项；一个channel EOF不等于owner退出。 */ if(ScShellCatalogUnregister(s->catalog,a->desc.id,SC_SHELL_LAYER_SESSION,s->catalog->epoch)<0)++s->sweep_app;
            else {
                s->info.epoch=s->catalog->epoch;
                if(s->info.transient_entries)--s->info.transient_entries;
            }
        } else ++s->sweep_app;
        if(checked>=s->catalog->app_count)break;
    }
    checked=0;
    while(checked<32&&s->catalog->association_count) {
        ShAssociation *a;
        ScProcessRef ref;
        ScProcessInfo info;
        ScStatus status;
        if(s->sweep_association>=s->catalog->association_count)s->sweep_association=0;
        a=s->catalog->associations[s->sweep_association];
        ++checked;
        if(a->layer!=SC_SHELL_LAYER_SESSION) {
            ++s->sweep_association;
            continue;
        }
        memset(&ref,0,sizeof(ref));
        ref.size=sizeof(ref);
        ref.version=1;
        ref.pid=a->owner_pid;
        ref.generation=a->owner_generation;
        memset(&info,0,sizeof(info));
        info.size=sizeof(info);
        info.version=1;
        status=ScProcessQuery(&ref,&info);
        if((status==SC32_E_STALE||(status>=0&&info.state==SC32_PROCESS_EXITING))&&s->catalog->epoch!=0xFFFFFFFFu) {
            memmove(s->catalog->associations+s->sweep_association,s->catalog->associations+s->sweep_association+1,(s->catalog->association_count-s->sweep_association-1)*sizeof(*s->catalog->associations));
            --s->catalog->association_count;
            ++s->catalog->epoch;
            sh_association_release(a);
            if(s->info.transient_entries)--s->info.transient_entries;
        } else ++s->sweep_association;
        if(checked>=s->catalog->association_count)break;
    }
}
ScStatus ScShellServiceStep(ScShellService *s,ScU32 timeout) {
    ScStatus status=0;
    ScU32 i,progress=0,visits;
    ShClient *current;
    if(!sh_valid(s,SH_SERVICE))return SC_SHELL_E_STALE;
    if(s->base.depth)return SC_SHELL_E_BUSY;
    sh_enter(&s->base);
    s->base.notification_error=0;
    {
        ScShellHostInfo current_host;
        memset(&current_host,0,sizeof(current_host));
        current_host.size=sizeof(current_host);
        current_host.version=1;
        status=ScShellHostQuery(&current_host);
        if(status<0)goto done;
        if(current_host.host_epoch!=s->host.host_epoch||current_host.pid!=s->self.pid||current_host.generation!=s->self.generation||current_host.session!=s->self.session||current_host.state==SC32_SHELL_STOPPING) {
            status=SC_SHELL_E_STALE;
            goto done;
        }
        if(current_host.state!=SC32_SHELL_PREPARED&&current_host.state!=SC32_SHELL_ACTIVE) {
            status=SC_SHELL_PENDING;
            goto done;
        }
    }
    for(i=0;i<SH_SERVICE_BATCH;++i) {
        status=sh_service_accept(s);
        if(status==SC_SHELL_PENDING) {
            status=0;
            break;
        }
        if(status<0)goto done;
        progress=1;
    }
    {
        ScU32 before=s->catalog->epoch;
        sh_service_sweep(s);
        if(before!=s->catalog->epoch) {
            progress=1;
            sh_event(&s->base,s->desc.procedure,s->desc.context,SC_SHELL_EVENT_CHANGED,s->catalog->epoch,0,0,0,0,0,0);
        }
    }
    if(!s->scan_remaining)s->scan_remaining=s->info.clients;
    current=s->cursor?s->cursor:s->clients;
    visits=s->scan_remaining;
    if(visits>s->info.clients)visits=s->info.clients;
    if(visits>SH_SERVICE_BATCH)visits=SH_SERVICE_BATCH;
    for(i=0;i<visits&&current&&!s->base.closing;++i) {
        ShClient *c=current;
        current=c->next?c->next:s->clients;
        status=sh_service_io(s,c,&progress);
        if(status<0||status==SC_SHELL_END) {
            if(c->previous)c->previous->next=c->next;
            else s->clients=c->next;
            if(c->next)c->next->previous=c->previous;
            else s->tail=c->previous;
            if(current==c)current=s->clients;
            --s->info.clients;
            sh_service_client_free(s,c);
            status=0;
        }
    }
    s->cursor=current;
    if(s->scan_remaining>=visits)s->scan_remaining-=visits;
    else s->scan_remaining=0;
    if(progress)s->scan_remaining=s->info.clients;
    s->info.flags=s->scan_remaining||s->ready_stores?SC_SHELL_SERVICE_READY:0;
    if(!progress&&timeout&&!s->base.closing&&!s->info.flags) {
        ScU32 ticks=timeout;
        /* 只有真实临时owner存在才有业务清理期限；空catalog不100Hz忙轮询。 */ if(s->info.transient_entries&&(ticks==SC32_INFINITE||ticks>100))ticks=100;
        status=sh_wait(0,0,ticks);
        if(status>=0)status=SC_SHELL_PENDING;
    }
    done:s->info.epoch=s->catalog->epoch;
    if(status>=0&&s->base.notification_error<0)status=s->base.notification_error;
    sh_leave(&s->base);
    return status;
}
ScStatus ScShellServiceQuery(const ScShellService *s,ScShellServiceInfo *out) {
    if(!sh_valid(s,SH_SERVICE))return SC_SHELL_E_STALE;
    return sh_output(out,&s->info,sizeof(s->info));
}
void ScShellServiceClose(ScShellService *s) {
    if(!sh_valid(s,SH_SERVICE))return;
    s->base.closing=1;
    sh_release(&s->base);
}
