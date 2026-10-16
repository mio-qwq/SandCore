static void sh_store_unlock(ScShellStore *s) {
    if(s->catalog&&s->catalog->commit_lock==s)s->catalog->commit_lock=0;
}
static void sh_store_dispose(ShObject *base) {
    ScShellStore *s=(ScShellStore *)base;
    sh_lease_drop(&s->candidate,s->info.state!=SC_SHELL_STORE_COMPLETE);
    if(s->candidate.state==SC32_LEASE_COMMITTED&&(s->candidate.flags&SC32_LEASE_FLAG_DURABLE)&&s->catalog)s->catalog->store_revision=s->candidate.revision;
    sh_lease_drop(&s->target,0);
    if(s->file)ScFileClose(s->file);
    if(s->target_file)ScFileClose(s->target_file);
    sh_store_unlock(s);
    ScShellCatalogRelease(s->catalog);
    ScBufferRelease(s->body);
    free(s->path);
    free(s->temporary);
}
static ScStatus sh_store_begin_buffer(ScShellCatalog *catalog,ScBuffer *body,const char *path,ScU32 expected,ScU32 payload_epoch,ScShellStore **out) {
    ScShellStore *s;
    ScStatus status;
    const unsigned char *p;
    ScU32 bytes;
    if(!out||(catalog&&!sh_valid(catalog,SH_CATALOG)))return SC_SHELL_E_INVALID;
    status=sh_buffer_data(body,&p,&bytes);
    if(status<0)return status;
    s=(ScShellStore *)sh_new(sizeof(*s),SH_STORE,sh_store_dispose);
    if(!s)return SC_SHELL_E_MEMORY;
    status=sh_absolute(path,&s->path);
    if(status<0)goto fail;
    status=ScBufferRetain(body);
    if(status<0)goto fail;
    s->body=body;
    if(catalog) {
        status=ScShellCatalogRetain(catalog);
        if(status<0)goto fail;
        s->catalog=catalog;
        s->expected_epoch=catalog->epoch;
    }
    s->attempt=1;
    s->info.size=sizeof(s->info);
    s->info.version=1;
    s->info.state=SC_SHELL_STORE_WRITING;
    s->info.expected_revision=expected;
    s->info.catalog_epoch=payload_epoch;
    s->info.total_bytes=bytes;
    s->info.flags=SC_SHELL_STORE_READY;
    s->candidate.size=sizeof(s->candidate);
    s->candidate.version=1;
    s->target.size=sizeof(s->target);
    s->target.version=1;
    *out=s;
    return 0;
    fail:sh_release(&s->base);
    return sh_cause(status,26,0);
}
ScStatus ScShellStoreBegin(ScShellCatalog *catalog,const char *path,ScU32 expected,ScShellStore **out) {
    ScBuffer *body=0;
    ScStatus s;
    if(!sh_valid(catalog,SH_CATALOG))return SC_SHELL_E_STALE;
    if(catalog->commit_lock)return SC_SHELL_E_BUSY;
    s=sh_catalog_account_buffer(catalog,&body);
    if(s>=0)s=sh_store_begin_buffer(catalog,body,path,expected,catalog->epoch,out);
    ScBufferRelease(body);
    return s;
}
static ScStatus sh_store_result(ScShellStore *s,ScStatus status) {
    if(status==SC32_PENDING) {
        s->info.state=SC_SHELL_STORE_PUBLISHING;
        s->info.stage=6;
        s->info.flags=SC_SHELL_STORE_WAITING|SC_SHELL_STORE_READY;
        return SC_SHELL_PENDING;
    }
    if(status<0) {
        status=sh_cause(status,27,0);
        s->error=sh_error;
        s->info.status=status;
        s->info.state=(s->candidate.flags&SC32_LEASE_FLAG_UNCERTAIN)||s->candidate.state==SC32_LEASE_UNCERTAIN_STATE?SC_SHELL_STORE_UNKNOWN:SC_SHELL_STORE_FAILED;
        if(s->info.state==SC_SHELL_STORE_UNKNOWN) {
            s->info.status=SC_SHELL_E_UNKNOWN;
            s->error.code=SC_SHELL_E_UNKNOWN;
        }
        s->info.flags&=SC_SHELL_STORE_DURABLE;
        sh_store_unlock(s);
        return s->info.status;
    }
    if(s->candidate.state!=SC32_LEASE_COMMITTED||!(s->candidate.flags&SC32_LEASE_FLAG_DURABLE))return sh_store_result(s,SC_SHELL_E_UNKNOWN);
    s->info.state=SC_SHELL_STORE_PUBLISHING;
    s->info.new_revision=s->candidate.revision;
    s->info.status=0;
    s->info.flags=SC_SHELL_STORE_DURABLE|SC_SHELL_STORE_READY;
    s->info.stage=7;
    if(s->catalog)s->catalog->store_revision=s->candidate.revision;
    return SC_SHELL_PENDING;
}
ScStatus ScShellStoreStep(ScShellStore *s,ScU32 timeout) {
    ScStatus status=0;
    ScU32 stage;
    ScLeaseOpenDesc open;
    const unsigned char *p;
    ScU32 bytes;
    if(!sh_valid(s,SH_STORE))return SC_SHELL_E_STALE;
    if(s->base.depth)return SC_SHELL_E_BUSY;
    if(s->info.state==SC_SHELL_STORE_COMPLETE)return 0;
    if(s->info.state>=SC_SHELL_STORE_FAILED) {
        sh_error=s->error;
        return s->info.status;
    }
    if(s->catalog&&s->info.stage<6&&s->catalog->epoch!=s->expected_epoch) {
        s->info.state=SC_SHELL_STORE_FAILED;
        s->info.status=SC_SHELL_E_STALE;
        s->info.flags=0;
        sh_fail(SC_SHELL_E_STALE,SC_SHELL_E_STALE,27,0);
        s->error=sh_error;
        return SC_SHELL_E_STALE;
    }
    sh_enter(&s->base);
    stage=s->info.stage;
    if(stage==0) {
        if(s->info.expected_revision) {
            memset(&open,0,sizeof(open));
            open.size=sizeof(open);
            open.version=1;
            open.path=s->path;
            open.object_kind=SC32_PATH_FILE;
            open.lease_kind=SC32_LEASE_WRITE_EXCLUSIVE;
            open.expected_revision=s->info.expected_revision;
            open.open_flags=SC32_FILE_WRITE;
            status=ScFileLeaseOpenPinned(&open,&s->target);
            if(status>=0)s->target_file=s->target.file;
        }
        if(status>=0)++s->info.stage;
    } else if(stage==1) {
        free(s->temporary);
        s->temporary=0;
        status=sh_temporary(s->path,s->attempt,&s->temporary);
        if(status==SC_SHELL_E_EXISTS)status=SC32_E_EXISTS;
        if(status>=0) {
            memset(&open,0,sizeof(open));
            open.size=sizeof(open);
            open.version=1;
            open.path=s->temporary;
            open.object_kind=SC32_PATH_FILE;
            open.lease_kind=SC32_LEASE_WRITE_EXCLUSIVE;
            open.open_flags=SC32_FILE_READ|SC32_FILE_WRITE;
            open.create_mode=3;
            status=ScFileLeaseCreatePinned(&open,&s->candidate);
        }
        if(status==SC32_E_EXISTS) {
            if(s->attempt==0xFFFFFFFFu)status=SC_SHELL_E_BUSY;
            else {
                ++s->attempt;
                status=0;
            }
        } else if(status>=0) {
            s->file=s->candidate.file;
            ++s->info.stage;
        }
    } else if(stage==2) {
        status=sh_buffer_data(s->body,&p,&bytes);
        if(status>=0&&s->info.bytes_written<bytes) {
            ScIoResult io;
            ScU32 n=bytes-s->info.bytes_written;
            if(n>16384)n=16384;
            memset(&io,0,sizeof(io));
            io.size=sizeof(io);
            io.version=1;
            status=ScFileWrite(s->file,p+s->info.bytes_written,n,&io);
            if(io.transferred>n)status=SC_SHELL_E_PROTOCOL;
            else s->info.bytes_written+=io.transferred;
            if(status>=0&&!io.transferred)status=SC_SHELL_E_IO;
        }
        if(status>=0&&s->info.bytes_written==s->info.total_bytes)++s->info.stage;
    } else if(stage==3) {
        status=ScFileFlush(s->file);
        if(status>=0)++s->info.stage;
    } else if(stage==4) {
        ScLeaseControl c;
        memset(&c,0,sizeof(c));
        c.size=sizeof(c);
        c.version=1;
        c.lease=s->candidate.lease;
        c.generation=s->candidate.generation;
        status=ScFileLeaseQuery(&c,&s->candidate);
        if(status>=0)++s->info.stage;
    } else if(stage==5) {
        ScLeasePublishDesc publish;
        if(s->catalog&&s->catalog->commit_lock&&s->catalog->commit_lock!=s)status=SC_SHELL_E_BUSY;
        else {
            if(s->catalog)s->catalog->commit_lock=s;
            memset(&publish,0,sizeof(publish));
            publish.size=sizeof(publish);
            publish.version=1;
            publish.source_lease=s->candidate.lease;
            publish.source_generation=s->candidate.generation;
            publish.target_path=s->path;
            publish.expected_source_object=s->candidate.object_id;
            publish.expected_source_revision=s->candidate.revision;
            publish.final_mode=3;
            publish.flags=SC32_LEASE_SET_MODE|(s->info.expected_revision?SC32_LEASE_EXPECT_EXACT:SC32_LEASE_EXPECT_ABSENT);
            if(s->info.expected_revision) {
                /* CAT/LNK 的条件替换只改变内容，保留原文件的权限和身份。 */
                publish.flags=SC32_LEASE_EXPECT_EXACT;
                publish.final_mode=0;
                publish.target_lease=s->target.lease;
                publish.target_generation=s->target.generation;
                publish.expected_target_object=s->target.object_id;
                publish.expected_target_revision=s->target.revision;
            }
            status=s->info.expected_revision?ScFileLeasePublishPreserving(&publish,&s->candidate):ScFileLeasePublish(&publish,&s->candidate);
        }
        status=sh_store_result(s,status);
        sh_leave(&s->base);
        return status;
    } else if(stage==6) {
        ScLeaseControl c;
        memset(&c,0,sizeof(c));
        c.size=sizeof(c);
        c.version=1;
        c.lease=s->candidate.lease;
        c.generation=s->candidate.generation;
        c.operation_sequence=s->candidate.operation_sequence;
        status=ScFileLeaseAdvance(&c,&s->candidate);
        status=sh_store_result(s,status);
        if(status==SC_SHELL_PENDING&&timeout&&!(s->info.flags&SC_SHELL_STORE_READY)) {
            ScStatus waited=sh_wait(0,0,timeout);
            if(waited<0)status=waited;
        }
        sh_leave(&s->base);
        return status;
    } else if(stage==7||stage==8) {
        ScLeaseInfo *lease=stage==7?&s->candidate:&s->target;
        ScU32 bit=stage==7?1:2;
        if(lease->lease) {
            ScLeaseControl c;
            memset(&c,0,sizeof(c));
            c.size=sizeof(c);
            c.version=1;
            c.lease=lease->lease;
            c.generation=lease->generation;
            c.operation_sequence=lease->operation_sequence;
            if(s->release_started&bit)status=ScFileLeaseAdvance(&c,lease);
            else {
                s->release_started|=bit;
                status=ScFileLeaseRelease(&c,lease);
            }
            if(status==SC32_E_STALE)status=0;
            if(!status) {
                lease->lease=0;
                ++s->info.stage;
            }else if(status==SC32_PENDING)status=0;
        } else ++s->info.stage;
    } else if(stage==9) {
        if(s->file) {
            status=ScFileClose(s->file);
            s->file=0;
            if(status==SC32_E_STALE)status=0;
        }
        if(status>=0)++s->info.stage;
    } else if(stage==10) {
        if(s->target_file) {
            status=ScFileClose(s->target_file);
            s->target_file=0;
            if(status==SC32_E_STALE)status=0;
        }
        if(status>=0) {
            s->info.state=SC_SHELL_STORE_COMPLETE;
            s->info.flags=SC_SHELL_STORE_DURABLE;
            sh_store_unlock(s);
            sh_leave(&s->base);
            return 0;
        }
    } else status=SC_SHELL_E_PROTOCOL;
    if(status<0) {
        status=sh_cause(status,27,0);
        s->error=sh_error;
        s->info.state=SC_SHELL_STORE_FAILED;
        s->info.status=status;
        s->info.flags&=SC_SHELL_STORE_DURABLE;
        sh_store_unlock(s);
    } else {
        s->info.flags=(s->info.flags&SC_SHELL_STORE_DURABLE)|SC_SHELL_STORE_READY;
        status=SC_SHELL_PENDING;
    }
    sh_leave(&s->base);
    return status;
}
ScStatus ScShellStoreQuery(const ScShellStore *s,ScShellStoreInfo *out) {
    if(!sh_valid(s,SH_STORE))return SC_SHELL_E_STALE;
    return sh_output(out,&s->info,sizeof(s->info));
}
void ScShellStoreClose(ScShellStore *s) {
    if(sh_valid(s,SH_STORE))sh_release(&s->base);
}
