static ScStatus sw_pending_result(SwItem *item)
{
    if(!item->reported){
        if(sw.result_tail)sw.result_tail->next_result=item;
        else sw.result_head=item;
        sw.result_tail=item;
        item->next_result=0;
        item->reported=1;
    }
    return 0;
}
static ScStatus sw_deliver_results(void)
{
    SwItem *item=sw.result_head;
    if(!item)return 0;
    if(item->journal_pending && !sw.recovery_failed)return 0;
    ScStatus status=sw_emit_result(item);
    if(status)return status;
    sw.result_head=item->next_result;
    if(!sw.result_head)sw.result_tail=0;
    item->next_result=0;
    item->reported=2;
    return 0;
}
static int sw_success(const SwItem *item)
{
    return item->status==0 && (item->outcome==SW_OUT_COPIED || item->outcome==SW_OUT_MOVED
        || item->outcome==SW_OUT_REMOVED || item->outcome==SW_OUT_RENAMED || item->outcome==SW_OUT_CREATED);
}
static ScStatus sw_close_active(void)
{
    SwLease *list[3]={&sw.target_lease,&sw.candidate_lease,&sw.source_lease};
    for(ScU32 i=0;i<3;i++)if(list[i]->valid){
        ScStatus status=sw_lease_release(list[i]);
        if(status==SC32_PENDING)return status;
        if(status)return status;
    }
    sw.source=0;
    return 0;
}
static ScStatus sw_finish_item(SwItem *item,ScU32 outcome,ScStatus status)
{
    if(item->phase!=SW_CLEANUP){
        item->outcome=outcome;
        if(item->kind==2 && outcome!=SW_OUT_COPIED && outcome!=SW_OUT_MOVED && outcome!=SW_OUT_RENAMED && outcome!=SW_OUT_CREATED)item->skip_children=1;
        if(status)sw_error(item,status);
        else{item->status=0;
            sw_record(&item->cause,sizeof(item->cause));
        }
        item->phase=SW_CLEANUP;
    }
    ScStatus closed=sw_close_active();
    if(closed==SC32_PENDING)return closed;
    if(closed && !item->status)sw_error(item,closed);
    if(sw.recovery_ready && !item->journaled && !sw.recovery_failed){
        ScStatus recorded=sw_journal_record(item,item->stage,sw.operation!=SW_COPY);
        if(recorded){sw_error(item,recorded);
            if(item->published && sw.operation==SW_MOVE && !item->source_removed)item->outcome=SW_OUT_SOURCE_RETAINED;
            else if(item->outcome!=SW_OUT_UNKNOWN)item->outcome=SW_OUT_FAILED;
            sw.recovery_failed=1;
        }
        item->journaled=1;
    }
    item->phase=SW_DONE;
    sw.items_done++;
    if(item->outcome==SW_OUT_SKIPPED)sw.items_skipped++;
    if(item->status<0 || item->outcome==SW_OUT_FAILED || item->outcome==SW_OUT_UNKNOWN || item->outcome==SW_OUT_SOURCE_RETAINED)sw.items_failed++;
    if(item->status<0 && !sw.failure_cause.code)sw.failure_cause=item->cause;
    if(item->parent){item->parent->children_finished++;
        if(!sw_success(item))item->parent->children_failed++;
    }
    sw_pending_result(item);
    return 0;
}
static ScStatus sw_begin_conflict(SwItem *item,ScStatus status)
{
    if(sw.conflict_counter==0xffffffffu)return SC32_E_RANGE;
    item->attempt_phase=item->phase;
    sw_error(item,status);
    ScPathInfo info;
    sw_record(&info,sizeof(info));
    item->conflict_source_revision=0;
    item->conflict_destination_revision=0;
    if(item->source[0] && !ScPathQuery(item->source,&info))item->conflict_source_revision=info.generation;
    sw_record(&info,sizeof(info));
    if(item->destination[0] && !ScPathQuery(item->destination,&info))item->conflict_destination_revision=info.generation;
    item->conflict_epoch=++sw.conflict_counter;
    item->phase=SW_CONFLICT;
    sw.state=SHW_CONFLICT;
    /* 先确保持久结果已写，再发新的询问；已有ITEM不因冲突丢失。 */
    if(sw.record_count){ScStatus flushed=sw_journal_flush();
        if(flushed)return flushed;
    }
    return sw_emit_conflict(item);
}
static ScStatus sw_candidate_path(char out[68])
{
    static const char digits[]="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char parent[68];
    memcpy(parent,sw.recovery_path,68);
    ScU32 n=(ScU32)strlen(parent);
    while(n && parent[n-1]!='/')n--;
    if(!n || n>=64)return SC32_E_RANGE;
    parent[n]=0;
    ScU32 width=64u-n;
    if(width>7)width=7;
    if(!sw.candidate_started){sw.candidate_seed=ScHashBytes(&sw.request,sizeof(sw.request))^sw.self.pid^sw.self.generation;
        sw.candidate_started=1;
    }
    ScU32 limit=1;
    for(ScU32 i=0;i<width;i++){if(limit>0xffffffffu/36u){limit=0;
            break;
        }limit*=36u;
    }
    sw.candidate_space=limit;
    if(sw.candidate_sequence==0xffffffffu)return SC32_E_RANGE;
    ScU32 value=sw.candidate_seed+sw.candidate_sequence++;
    if(limit)value%=limit;
    memset(out,0,68);
    memcpy(out,parent,n);
    for(ScU32 i=0;i<width;i++){out[n+width-1-i]=digits[value%36u];
        value/=36u;
    }
    return 0;
}
static ScStatus sw_create_candidate(SwItem *item)
{
    char path[68];
    ScStatus status=sw_candidate_path(path);
    if(status)return status;
    if(sw.candidate_space && item->candidate_trials>=sw.candidate_space)return SC32_E_NO_SPACE;
    item->candidate_trials++;
    if(sw_same(path,sw.recovery_path) || sw_find_item(path))return SC32_PENDING;
    status=sw_lease_open(&sw.candidate_lease,path,item->kind,
        item->kind==2?SC32_LEASE_NAMESPACE:SC32_LEASE_WRITE_EXCLUSIVE,0,
        item->kind==1?SC32_FILE_READ|SC32_FILE_WRITE:0,0,1);
    /* EXISTS在接受前无副作用，下一step换另一个真正未占用名字；
     * 不是重放已受理的Write/Publish。名字长度仍服从真实v5限制。 */
    if(status==SC32_E_EXISTS)return SC32_PENDING;
    return status;
}
static ScStatus sw_source_open(SwItem *item)
{
    if(sw.operation==SW_MKDIR){sw.source=0;
        return 0;
    }
    if(item==item->root->root_item && item->kind==2 && item->root->scope.valid){sw.source=&item->root->scope;
    }
    else if(item->kind==2 && sw.operation==SW_COPY){sw.source=0;
    }
    else{
        ScStatus status;
        if(sw.operation==SW_COPY)status=sw_lease_open(&sw.source_lease,item->source,1,SC32_LEASE_READ_STABLE,
            item->source_revision,SC32_FILE_READ,sw_source_parent(item),0);
        else status=sw_lease_metadata(&sw.source_lease,item->source,item->kind,item->source_revision,sw_source_parent(item));
        if(status)return status;
        sw.source=&sw.source_lease;
    }
    if(sw.source){
        ScStatus status=sw_lease_query(sw.source);
        if(status)return status;
        if(sw.source->info.revision!=item->source_revision)return SC32_E_STALE;
        item->source_object=sw.source->info.object_id;
    }
    if(item->kind==1){
        ScFileInfo info;
        sw_record(&info,sizeof(info));
        ScStatus status=ScFileQuery(sw.source_lease.file,&info);
        if(status)return status;
        if(info.revision!=item->source_revision)return SC32_E_STALE;
        if(info.length!=item->bytes){sw_counter_subtract(&sw.total_bytes,item->bytes);
            item->bytes=info.length;
            sw_counter_add(&sw.total_bytes,item->bytes);
        }
        ScPathInfo metadata;
        sw_record(&metadata,sizeof(metadata));
        status=ScPathQuery(item->source,&metadata);
        if(status)return status;
        if(metadata.kind!=1 || metadata.generation!=item->source_revision)return SC32_E_STALE;
        item->source_mode=metadata.rw;
        if(sw.flags&SHW_PRESERVE_MODE)item->final_mode=metadata.rw;
    }
    return 0;
}
static ScStatus sw_target(SwItem *item)
{
    ScPathInfo info;
    sw_record(&info,sizeof(info));
    ScStatus status=ScPathQuery(item->destination,&info);
    if(status!=0 && status!=SC32_E_NOT_FOUND)return status;
    int exists=!status;
    /* caller明确观察缺名，不能让默认REPLACE把后来出现的对象覆盖。
     * 自己已经持久发布的root可沿原恢复路径收尾，不把它当外来目标。 */
    if(item==item->root->root_item && !item->published && (item->root->flags&SHW_ITEM_EXPECT_ABSENT) && exists)return SC32_E_STALE;
    if(item->decision_check && (exists?info.generation:0)!=item->decision_expected_destination)return SC32_E_STALE;
    if(item==item->root->root_item && !item->retry && item->root->expected_destination_revision
       && (!exists || info.generation!=item->root->expected_destination_revision || info.kind!=item->root->expected_destination_kind))return SC32_E_STALE;
    if(exists){
        item->destination_revision=info.generation;
        if(item->merge && item->kind==2 && info.kind==2){
            if(item==item->root->root_item && !item->root->destination_scope.valid){
                status=sw_namespace_open(&item->root->destination_scope,item->destination,info.generation,0,0);
                if(status)return status;
            }
            item->phase=SW_DIRECTORY_READY;
            return 0;
        }
        if(!item->overwrite || info.kind!=item->kind)return SC32_E_EXISTS;
        status=sw_lease_metadata(&sw.target_lease,item->destination,info.kind,info.generation,sw_destination_parent(item));
        if(status)return status;
        item->destination_object=sw.target_lease.info.object_id;
    }else item->destination_revision=item->destination_object=0;
    if(sw.candidate_lease.valid && !item->published && item->stage==SHW_STAGE_FLUSHED)item->phase=SW_PUBLISH_BEGIN;
    else if(sw.operation==SW_MOVE || sw.operation==SW_RENAME)item->phase=SW_RENAME_BEGIN;
    else item->phase=SW_CANDIDATE;
    return 0;
}
static ScStatus sw_wait_operation(SwItem *item,SwLease *lease)
{
    if(sw.cancelled && !lease->terminal && lease->info.state==SC32_LEASE_PENDING_STATE){
        ScStatus status=sw_lease_cancel(lease);
        if(status!=0 && status!=SC32_PENDING)return status;
        return SC32_PENDING;
    }
    ScStatus status=sw_lease_advance(lease);
    if(status==SC32_PENDING)return status;
    if((lease->info.flags&SC32_LEASE_FLAG_UNCERTAIN) || lease->info.state==SC32_LEASE_UNCERTAIN_STATE){
        item->outcome=SW_OUT_UNKNOWN;
        item->durable=0;
        sw_fail(SC32_E_IO);
        return SC32_E_IO;
    }
    if(status)return status;
    if(lease->info.state!=SC32_LEASE_COMMITTED || !(lease->info.flags&SC32_LEASE_FLAG_DURABLE))return SC32_E_CANCELLED;
    return 0;
}
static void sw_cover_renamed(SwItem *item)
{
    /* 真实一次目录rename覆盖整棵受锁子树，只发根的durable事实；
     * 不为未执行的子计划伪造新revision或逐字节复制证据。 */
    for(ScU32 i=item->root->first;i<item->root->last;i++){
        SwItem *child=sw.sorted[i];
        if(child==item || !sw_child(child->source,item->source) || child->phase==SW_DONE)continue;
        child->covered=1;
        child->phase=SW_DONE;
        child->outcome=SW_OUT_MOVED;
        child->reported=2;
        child->source_removed=1;
        sw.items_done++;
        if(child->kind==1)sw_counter_add(&sw.confirmed_bytes,child->bytes);
    }
}
static ScStatus sw_io_failure(SwItem *item,ScStatus status,ScU32 flags)
{
    if(flags&(SC32_FILE_STATE_FAULTED|SC32_FILE_STATE_UNCERTAIN)){
        sw_fail(status?status:SC32_E_IO);
        return sw_finish_item(item,SW_OUT_UNKNOWN,status?status:SC32_E_IO);
    }
    return sw_begin_conflict(item,status?status:SC32_E_IO);
}
static ScStatus sw_copy_read(SwItem *item)
{
    if(item->offset==item->bytes){item->phase=SW_FLUSH;
        return 0;
    }
    ScU32 bytes=item->bytes-item->offset;
    if(bytes>SW_IO_BYTES)bytes=SW_IO_BYTES;
    ScIoResult result;
    sw_record(&result,sizeof(result));
    result.transferred=0xffffffffu;
    ScStatus status=ScFileRead(sw.source_lease.file,sw.io,bytes,&result);
    ScU32 accepted=result.transferred<=bytes?result.transferred:0;
    if(result.flags&(SC32_FILE_STATE_FAULTED|SC32_FILE_STATE_UNCERTAIN))return sw_io_failure(item,status?status:SC32_E_IO,result.flags);
    if(!accepted)return sw_io_failure(item,status?status:SC32_E_STALE,result.flags);
    item->buffer_bytes=accepted;
    item->buffer_used=0;
    sw.error_after_prefix=status!=0;
    sw.deferred_io_error=status;
    sw.deferred_cause=item->cause;
    if(status){sw_error(item,status);
        sw.deferred_cause=item->cause;
    }
    item->phase=SW_WRITE;
    return 0;
}
static ScStatus sw_copy_write(SwItem *item)
{
    ScU32 bytes=item->buffer_bytes-item->buffer_used;
    ScIoResult result;
    sw_record(&result,sizeof(result));
    result.transferred=0xffffffffu;
    ScStatus status=ScFileWrite(sw.candidate_lease.file,sw.io+item->buffer_used,bytes,&result);
    ScU32 accepted=result.transferred<=bytes?result.transferred:0;
    item->buffer_used+=accepted;
    item->offset+=accepted;
    if(status || !accepted)return sw_io_failure(item,status?status:SC32_E_IO,result.flags);
    if(item->buffer_used==item->buffer_bytes){
        if(sw.error_after_prefix){item->cause=sw.deferred_cause;
            item->status=sw.deferred_io_error;
            return sw_begin_conflict(item,sw.deferred_io_error);
        }
        item->phase=SW_READ;
    }
    return sw_emit_progress(item);
}
static ScStatus sw_directory_finalize(SwItem *item)
{
    if(item->children_finished!=item->children_total || item->children_failed){
        item->skip_children=1;
        return sw_finish_item(item,sw.operation==SW_MOVE?SW_OUT_SOURCE_RETAINED:SW_OUT_FAILED,SC32_E_FAILED);
    }
    if(sw.operation==SW_DELETE){
        ScStatus status=sw_source_open(item);
        if(status)return sw_begin_conflict(item,status);
        item->phase=SW_REMOVE_BEGIN;
        return 0;
    }
    int set_mode=item->directory_created || ((sw.flags&SHW_PRESERVE_MODE) && item->merge);
    if(set_mode && !item->mode_done){
        if(item==item->root->root_item && item->root->destination_scope.valid){
            ScStatus status=sw_lease_query(&item->root->destination_scope);
            if(status)return sw_begin_conflict(item,status);
            if(item->root->destination_scope.info.revision!=item->destination_revision)return sw_begin_conflict(item,SC32_E_STALE);
            sw.source=&item->root->destination_scope;
        }
        else{
            ScStatus status=sw_lease_metadata(&sw.candidate_lease,item->destination,2,item->destination_revision,sw_destination_parent(item));
            if(status)return sw_begin_conflict(item,status);
            sw.source=&sw.candidate_lease;
        }
        item->phase=SW_MODE_BEGIN;
        return 0;
    }
    if(sw.operation==SW_MOVE){
        ScStatus status=sw_source_open(item);
        if(status)return sw_begin_conflict(item,status);
        item->published=item->durable=1;
        item->stage=SHW_STAGE_DIRECTORY;
        item->phase=SW_COPY_RECORD;
        return 0;
    }
    if(item->merge){item->published=item->durable=1;
        item->stage=SHW_STAGE_DIRECTORY;
    }
    return sw_finish_item(item,sw.operation==SW_MKDIR?SW_OUT_CREATED:SW_OUT_COPIED,0);
}
static ScStatus sw_item_step(SwItem *item)
{
    if(item->phase==SW_DONE)return 0;
    if(item->phase==SW_CLEANUP)return sw_finish_item(item,item->outcome,item->status);
    if(item->phase==SW_CONFLICT)return sw.cancelled?sw_finish_item(item,item->published && sw.operation==SW_MOVE?SW_OUT_SOURCE_RETAINED:SW_OUT_CANCELLED,SC32_E_CANCELLED):0;
    if(sw.cancelled && item->phase!=SW_RENAME_WAIT && item->phase!=SW_PUBLISH_WAIT && item->phase!=SW_REMOVE_WAIT && item->phase!=SW_MODE_WAIT){
        if(item->source_removed && item->durable)return sw_finish_item(item,sw.operation==SW_DELETE?SW_OUT_REMOVED:sw.operation==SW_RENAME?SW_OUT_RENAMED:SW_OUT_MOVED,0);
        if(item->published && item->kind==1 && sw.operation==SW_COPY)return sw_finish_item(item,SW_OUT_COPIED,0);
        if(item->published && sw.operation==SW_MOVE && !item->source_removed)return sw_finish_item(item,SW_OUT_SOURCE_RETAINED,SC32_E_CANCELLED);
        return sw_finish_item(item,SW_OUT_CANCELLED,SC32_E_CANCELLED);
    }
    if(item->phase==SW_FINALIZE)return item->finalizing?sw_directory_finalize(item):0;
    if(item->phase==SW_DIRECTORY_READY){
        ScStatus status=sw_lease_release(&sw.source_lease);
        if(status==SC32_PENDING)return 0;
        if(status)return status;
        sw.source=0;
        item->phase=SW_FINALIZE;
        return 0;
    }
    if(item->phase==SW_RETRY_CLEANUP){
        if(item->retry_root_release&1u){
            ScStatus status=sw_lease_release(&item->root->scope);
            if(status==SC32_PENDING)return 0;
            if(status)return status;
            status=sw_lease_metadata(&item->root->scope,item->source,2,item->source_revision,0);
            if(status)return sw_begin_conflict(item,status);
            item->retry_root_release&=~1u;
            sw.plan_incomplete=1;
        }
        if(item->retry_root_release&2u){
            ScStatus status=sw_lease_release(&item->root->destination_scope);
            if(status==SC32_PENDING)return 0;
            if(status)return status;
            status=sw_lease_metadata(&item->root->destination_scope,item->destination,2,item->destination_revision,0);
            if(status)return sw_begin_conflict(item,status);
            item->retry_root_release&=~2u;
        }
        ScStatus status=sw_lease_release(&sw.target_lease);
        if(status==SC32_PENDING)return 0;
        if(status)return status;
        if(item->attempt_phase!=SW_PUBLISH_BEGIN){
            status=sw_lease_release(&sw.source_lease);
            if(status==SC32_PENDING)return 0;
            if(status)return status;
            if(!item->published || (sw.candidate_lease.info.flags&SC32_LEASE_FLAG_METADATA_ONLY)){
                status=sw_lease_release(&sw.candidate_lease);
                if(status==SC32_PENDING)return 0;
                if(status)return status;
            }
            sw.source=0;
        }
        item->phase=item->attempt_phase==SW_PUBLISH_BEGIN?SW_TARGET:item->attempt_phase;
        return 0;
    }
    if(item->phase==SW_SOURCE){
        if(item->root->skip || (item->parent && item->parent->skip_children))return sw_finish_item(item,SW_OUT_SKIPPED,0);
        if(item->root->plan_failed || item->status<0)return sw_begin_conflict(item,item->status?item->status:SC32_E_ACCESS);
        if(item->kind==2 && sw.operation==SW_DELETE){item->phase=SW_FINALIZE;
            return 0;
        }
        ScStatus status=sw_source_open(item);
        if(status)return sw_begin_conflict(item,status);
        if(item->kind==2 && sw.operation==SW_COPY){
            ScPathInfo info;
            sw_record(&info,sizeof(info));
            status=ScPathQuery(item->source,&info);
            if(status || !sw_can_read(info.uid,info.gid,info.rw)){item->skip_children=1;
                return sw_begin_conflict(item,status?status:SC32_E_ACCESS);
            }
            if(info.kind!=2 || info.generation!=item->source_revision)return sw_begin_conflict(item,SC32_E_STALE);
            item->source_mode=info.rw;
            if(sw.flags&SHW_PRESERVE_MODE)item->final_mode=info.rw;
        }
        if(sw.operation==SW_DELETE)item->phase=SW_REMOVE_BEGIN;
        else if(sw.operation==SW_RENAME && sw_same(item->source,item->destination))return sw_finish_item(item,SW_OUT_RENAMED,0);
        else item->phase=SW_TARGET;
        return 0;
    }
    if(item->phase==SW_TARGET){ScStatus status=sw_target(item);
        return status?sw_begin_conflict(item,status):0;
    }
    if(item->phase==SW_RENAME_BEGIN){
        ScStatus status=sw_lease_publish(sw.source,&sw.target_lease,sw_destination_parent(item),item->destination,0,1);
        if(status==SC32_PENDING){item->phase=SW_RENAME_WAIT;
            return 0;
        }
        if(status==SC32_E_NOT_SUPPORTED && sw.operation==SW_MOVE && item->kind==1){
            /* 仅后端明确未受理的跨卷能力缺失才走真实copy+remove。
             * IO/超时/未知结果从不通过另一条路径重放同一副作用。 */
            item->phase=SW_CANDIDATE;
            return 0;
        }
        return sw_begin_conflict(item,status?status:SC32_E_FAILED);
    }
    if(item->phase==SW_RENAME_WAIT){
        ScStatus status=sw_wait_operation(item,sw.source);
        if(status==SC32_PENDING)return 0;
        if(status)return item->outcome==SW_OUT_UNKNOWN?sw_finish_item(item,SW_OUT_UNKNOWN,status):
            status==SC32_E_CANCELLED?sw_finish_item(item,SW_OUT_CANCELLED,status):sw_begin_conflict(item,status);
        item->published=item->source_removed=item->durable=1;
        item->stage=SHW_STAGE_RENAME;
        item->destination_revision=sw.source->info.revision;
        item->destination_object=sw.source->info.object_id;
        item->confirmed_bytes=item->bytes;
        sw_counter_add(&sw.confirmed_bytes,item->bytes);
        if(item->kind==2)sw_cover_renamed(item);
        item->phase=SW_RENAMED;
        return 0;
    }
    if(item->phase==SW_RENAMED){
        ScStatus recorded=sw_journal_record(item,SHW_STAGE_RENAME,1);
        if(recorded)return sw_finish_item(item,SW_OUT_FAILED,recorded);
        item->journaled=1;
        return sw_finish_item(item,sw.operation==SW_MOVE?SW_OUT_MOVED:SW_OUT_RENAMED,0);
    }
    if(item->phase==SW_CANDIDATE){
        if(sw.operation==SW_MOVE && item->kind==1 && !(sw.source_lease.info.flags&SC32_LEASE_FLAG_CANDIDATE)
           && (sw.source_lease.info.flags&SC32_LEASE_FLAG_METADATA_ONLY)){
            ScStatus released=sw_lease_release(&sw.source_lease);
            if(released==SC32_PENDING)return 0;
            if(released)return sw_begin_conflict(item,released);
            ScStatus opened=sw_lease_open(&sw.source_lease,item->source,1,SC32_LEASE_READ_STABLE,item->source_revision,SC32_FILE_READ,sw_source_parent(item),0);
            if(opened)return sw_begin_conflict(item,opened);
            sw.source=&sw.source_lease;
        }
        ScStatus status=sw_create_candidate(item);
        if(status==SC32_PENDING)return 0;
        if(status)return sw_begin_conflict(item,status);
        item->stage=SHW_STAGE_CANDIDATE;
        item->phase=item->kind==2?SW_PUBLISH_BEGIN:SW_READ;
        return 0;
    }
    if(item->phase==SW_READ)return sw_copy_read(item);
    if(item->phase==SW_WRITE)return sw_copy_write(item);
    if(item->phase==SW_FLUSH){
        ScStatus status=ScFileFlush(sw.candidate_lease.file);
        if(status)return sw_begin_conflict(item,status);
        status=sw_lease_query(&sw.candidate_lease);
        if(status)return sw_begin_conflict(item,status);
        item->stage=SHW_STAGE_FLUSHED;
        item->phase=SW_PUBLISH_BEGIN;
        return 0;
    }
    if(item->phase==SW_PUBLISH_BEGIN){
        ScStatus status=sw_lease_publish(&sw.candidate_lease,&sw.target_lease,sw_destination_parent(item),item->destination,item->kind==2?3:item->final_mode,0);
        if(status==SC32_PENDING){item->phase=SW_PUBLISH_WAIT;
            return 0;
        }
        return sw_begin_conflict(item,status?status:SC32_E_FAILED);
    }
    if(item->phase==SW_PUBLISH_WAIT){
        ScStatus status=sw_wait_operation(item,&sw.candidate_lease);
        if(status==SC32_PENDING)return 0;
        if(status)return item->outcome==SW_OUT_UNKNOWN?sw_finish_item(item,SW_OUT_UNKNOWN,status):
            status==SC32_E_CANCELLED?sw_finish_item(item,SW_OUT_CANCELLED,status):sw_begin_conflict(item,status);
        item->published=item->durable=1;
        item->destination_revision=sw.candidate_lease.info.revision;
        item->destination_object=sw.candidate_lease.info.object_id;
        item->stage=item->kind==2?SHW_STAGE_DIRECTORY:SHW_STAGE_DESTINATION;
        item->confirmed_bytes=item->bytes;
        sw_counter_add(&sw.confirmed_bytes,item->bytes);
        item->phase=SW_PUBLISHED;
        return 0;
    }
    if(item->phase==SW_PUBLISHED){
        if(item->kind==2){
            item->directory_created=1;
            ScStatus released=sw_lease_release(&sw.target_lease);
            if(released==SC32_PENDING)return 0;
            if(released)return sw_begin_conflict(item,released);
            if(item==item->root->root_item){item->root->destination_scope=sw.candidate_lease;
                memset(&sw.candidate_lease,0,sizeof(sw.candidate_lease));
            }
            else{ScStatus closed=sw_lease_release(&sw.candidate_lease);
                if(closed==SC32_PENDING)return 0;
                if(closed)return sw_begin_conflict(item,closed);
            }
            ScStatus recorded=sw_journal_record(item,SHW_STAGE_DIRECTORY,1);
            if(recorded)return sw_finish_item(item,SW_OUT_FAILED,recorded);
            item->phase=SW_DIRECTORY_READY;
            sw.source=0;
            return 0;
        }
        item->phase=SW_COPY_RECORD;
        return 0;
    }
    if(item->phase==SW_COPY_RECORD){
        ScStatus recorded=sw_journal_record(item,item->stage,sw.operation==SW_MOVE);
        if(recorded)return sw_finish_item(item,
            sw.operation==SW_MOVE?SW_OUT_SOURCE_RETAINED:SW_OUT_FAILED,recorded);
        if(sw.operation==SW_MOVE){item->phase=SW_REMOVE_BEGIN;
            return 0;
        }
        item->journaled=1;
        return sw_finish_item(item,SW_OUT_COPIED,0);
    }
    if(item->phase==SW_MODE_BEGIN){
        ScStatus status=sw_lease_mode(sw.source,item->final_mode);
        if(status==SC32_PENDING){item->phase=SW_MODE_WAIT;
            return 0;
        }
        return sw_begin_conflict(item,status?status:SC32_E_FAILED);
    }
    if(item->phase==SW_MODE_WAIT){
        ScStatus status=sw_wait_operation(item,sw.source);
        if(status==SC32_PENDING)return 0;
        if(status)return item->outcome==SW_OUT_UNKNOWN?sw_finish_item(item,SW_OUT_UNKNOWN,status):
            status==SC32_E_CANCELLED?sw_finish_item(item,SW_OUT_CANCELLED,status):sw_begin_conflict(item,status);
        item->mode_done=item->durable=1;
        item->destination_revision=sw.source->info.revision;
        item->published=1;
        item->stage=SHW_STAGE_DIRECTORY;
        item->phase=SW_MODE_DONE;
        return 0;
    }
    if(item->phase==SW_MODE_DONE){
        if(sw.source==&sw.candidate_lease){ScStatus closed=sw_lease_release(&sw.candidate_lease);
            if(closed==SC32_PENDING)return 0;
            if(closed)return closed;
        }
        sw.source=0;
        item->phase=SW_FINALIZE;
        return 0;
    }
    if(item->phase==SW_REMOVE_BEGIN){
        if(!sw.source){ScStatus opened=sw_source_open(item);
            if(opened)return sw_begin_conflict(item,opened);
        }
        ScStatus recorded=sw_journal_record(item,SW_JOURNAL_REMOVE_INTENT,1);
        if(recorded)return sw_finish_item(item,item->published?SW_OUT_SOURCE_RETAINED:SW_OUT_FAILED,recorded);
        ScStatus status=sw_lease_remove(sw.source);
        if(status==SC32_PENDING){item->phase=SW_REMOVE_WAIT;
            return 0;
        }
        return sw_begin_conflict(item,status?status:SC32_E_FAILED);
    }
    if(item->phase==SW_REMOVE_WAIT){
        ScStatus status=sw_wait_operation(item,sw.source);
        if(status==SC32_PENDING)return 0;
        if(status)return item->outcome==SW_OUT_UNKNOWN?sw_finish_item(item,SW_OUT_UNKNOWN,status):
            status==SC32_E_CANCELLED?sw_finish_item(item,item->published?SW_OUT_SOURCE_RETAINED:SW_OUT_CANCELLED,status):sw_begin_conflict(item,status);
        item->source_removed=item->durable=1;
        item->stage=sw.operation==SW_DELETE?SHW_STAGE_DELETED:SHW_STAGE_SOURCE_REMOVED;
        item->confirmed_bytes=item->bytes;
        if(sw.operation==SW_DELETE)sw_counter_add(&sw.confirmed_bytes,item->bytes);
        return sw_finish_item(item,sw.operation==SW_DELETE?SW_OUT_REMOVED:SW_OUT_MOVED,0);
    }
    return SC32_E_INVALID;
}
static ScStatus sw_new_destination(SwItem *item,const char *path)
{
    if(!path[0] || sw_same(path,"/") || sw_same(sw.recovery_path,path) || sw_child(sw.recovery_path,path))return SC32_E_INVALID;
    for(SwRoot *root=sw.roots;root;root=root->next)
        if(root->source[0] && (sw_same(path,root->source) || sw_child(path,root->source) || sw_child(root->source,path)))return SC32_E_INVALID;
    ScU32 old=(ScU32)strlen(item->destination),next=(ScU32)strlen(path);
    if(item->kind==2)for(ScU32 i=item->root->first;i<item->root->last;i++){
        SwItem *child=sw.sorted[i];
        if(child->phase!=SW_DONE && sw_child(child->source,item->source)
           && next+(ScU32)strlen(child->destination)-old>64)return SC32_E_RANGE;
    }
    if(item->kind==2)for(ScU32 i=item->root->first;i<item->root->last;i++){
        SwItem *child=sw.sorted[i];
        if(child->phase==SW_DONE || !sw_child(child->source,item->source))continue;
        char suffix[68];
        memcpy(suffix,child->destination+old,strlen(child->destination+old)+1);
        memset(child->destination,0,68);
        memcpy(child->destination,path,next);
        memcpy(child->destination+next,suffix,strlen(suffix)+1);
    }
    memcpy(item->destination,path,68);
    if(item==item->root->root_item)memcpy(item->root->destination,path,68);
    return 0;
}
static ScStatus sw_decide(const ScShellWireJobDecision *decision,const char *path)
{
    SwItem *item=sw.active;
    if(!item || item->phase!=SW_CONFLICT || item->id!=decision->item_id || item->conflict_epoch!=decision->conflict_epoch
       || item->conflict_source_revision!=decision->expected_source_revision
       || item->conflict_destination_revision!=decision->expected_destination_revision)return SC32_E_STALE;
    if(decision->decision==SHW_DECISION_CANCEL){sw.cancelled=sw.user_cancel=1;
        sw.state=SHW_CANCELLING;
    }
    else if(decision->decision==SHW_DECISION_SKIP){
        item->skip_children=item->kind==2;
        ScStatus status=sw_finish_item(item,item->published && sw.operation==SW_MOVE?SW_OUT_SOURCE_RETAINED:SW_OUT_SKIPPED,
            item->published && sw.operation==SW_MOVE?SC32_E_CANCELLED:0);
        if(status!=0 && status!=SC32_PENDING)return status;
        sw.state=SHW_RUNNING;
    }else{
        if(item->root->plan_failed && item->kind==1){
            ScPathInfo actual;
            sw_record(&actual,sizeof(actual));
            ScStatus checked=ScPathQuery(item->source,&actual);
            if(checked)return sw_begin_conflict(item,checked);
            if(actual.kind!=item->root->kind || actual.generation!=item->conflict_source_revision)return sw_begin_conflict(item,SC32_E_STALE);
            item->root->plan_failed=0;
        }
        if(item->published && decision->decision!=SHW_DECISION_RETRY)return SC32_E_STALE;
        if(decision->decision==SHW_DECISION_MERGE){
            if(item->kind!=2)return SC32_E_INVALID;
            ScPathInfo info;
            sw_record(&info,sizeof(info));
            ScStatus checked=ScPathQuery(item->destination,&info);
            if(checked || info.kind!=2 || info.generation!=item->conflict_destination_revision)return SC32_E_STALE;
            item->merge=1;
            item->overwrite=0;
        }else if(decision->decision==SHW_DECISION_REPLACE){
            if(!item->conflict_destination_revision || sw.operation==SW_DELETE)return SC32_E_INVALID;
            item->overwrite=1;
            item->merge=0;
        }else if(decision->decision==SHW_DECISION_RENAME){
            if(sw.operation==SW_DELETE)return SC32_E_INVALID;
            ScStatus checked=sw_new_destination(item,path);
            if(checked)return checked;
            item->overwrite=item->merge=0;
        }
        if(item->conflict_source_revision)item->source_revision=item->conflict_source_revision;
        item->destination_revision=item->conflict_destination_revision;
        item->decision_expected_destination=decision->decision==SHW_DECISION_RENAME?0:item->conflict_destination_revision;
        item->decision_check=1;
        item->retry++;
        if(!item->retry)return SC32_E_RANGE;
        int expand=item->kind==2 && !item->published && !item->children_finished
            && (item->root->plan_failed || item->skip_children
                || (decision->decision==SHW_DECISION_MERGE && (item->root->scope.info.flags&SC32_LEASE_FLAG_METADATA_ONLY))
                || (sw.operation==SW_DELETE && (sw.flags&SHW_RECURSIVE)
                    && (item->root->scope.info.flags&SC32_LEASE_FLAG_METADATA_ONLY)));
        if(expand){
            if(decision->decision!=SHW_DECISION_RETRY && decision->decision!=SHW_DECISION_MERGE)return SC32_E_STALE;
            ScStatus checked=sw_retry_begin(item);
            if(checked)return checked;
            item->phase=SW_REPLANNING;
            ScShellWireJobProgress reply;
            sw_progress(&reply,item);
            return sw_queue(SHW_JOB_PROGRESS,&reply,sizeof(reply),SHW_REPLY,0);
        }
        if(sw.source && sw.source!=&sw.source_lease && (sw.source->info.state==SC32_LEASE_FAILED_STATE
           || sw.source->info.state==SC32_LEASE_CANCELLED_STATE)){
            if(sw.source==&item->root->destination_scope && item->finalizing)item->retry_root_release|=2;
            else if(sw.source==&item->root->scope)item->retry_root_release|=1;
        }
        if(item->published){
            item->attempt_phase=item->finalizing?SW_FINALIZE:SW_REMOVE_BEGIN;
            if(item->source_removed)return SC32_E_STALE;
        }else if(sw.candidate_lease.valid && item->stage==SHW_STAGE_FLUSHED
            && (sw.candidate_lease.info.state==SC32_LEASE_READY || sw.candidate_lease.info.state==SC32_LEASE_COMMITTED))item->attempt_phase=SW_PUBLISH_BEGIN;
        else{item->attempt_phase=SW_SOURCE;
            item->offset=item->buffer_bytes=item->buffer_used=0;
            item->stage=SHW_STAGE_NONE;
        }
        item->status=0;
        sw_record(&item->cause,sizeof(item->cause));
        item->phase=SW_RETRY_CLEANUP;
        sw.state=SHW_RUNNING;
    }
    ScShellWireJobProgress reply;
    sw_progress(&reply,item);
    return sw_queue(SHW_JOB_PROGRESS,&reply,sizeof(reply),SHW_REPLY,0);
}
