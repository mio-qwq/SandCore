static void lease_mark_cancel(lease_record_t *r,int release)
{
    if(release && r->release<(u32)release)r->release=(u32)release;
    if(!release && r->state==SC32_LEASE_COMMITTED)return;
    if(!r->cancel){
        r->cancel=1;r->snapshot_valid=0;
        if(!r->operation)r->operation=release?SC32_LEASE_OP_RELEASE:SC32_LEASE_OP_CANCEL;
        if(r->stage>=L_WAIT && r->stage<=L_SUPER)lease_finish(r,SC32_E_CANCELLED,FS_N_STALE,0);
        else if(r->state!=SC32_LEASE_COMMITTED && r->state!=SC32_LEASE_UNCERTAIN_STATE){
            r->state=SC32_LEASE_CANCELLED_STATE;
            r->result=r->operation==SC32_LEASE_OP_RELEASE || r->operation==SC32_LEASE_OP_CANCEL?0:SC32_E_CANCELLED;
            r->raw=0;
        }
        native_file_t *n=native_object(r->object);
        entry_t *entry=native_current(n);
        if(entry)r->revision=entry->generation;
        if(r->handle_kind==2 && r->handle && r->node[0]
           && !(r->flags&(SC32_LEASE_FLAG_CANDIDATE|SC32_LEASE_FLAG_REVOKED))){
            int index=find(r->name);if(index>=0)r->revision=dirents[index].generation;
        }
        if(n && n->uncertain)r->flags|=SC32_LEASE_FLAG_UNCERTAIN;
        if(n && n->pending && n->writer==r->handle){
            if(native_meta_owner==n->id)native_meta_owner=native_meta_sector=0;
            if(n->reserved_count){r->garbage_start=n->reserved_start;r->garbage_count=n->reserved_count;}
            n->reserved_start=n->reserved_count=0;native_pending_clear(n);
        }
        if(!(r->flags&SC32_LEASE_FLAG_CANDIDATE))lease_unlink(r,0);
    }
    lease_work_add(r);
}
static void lease_extent_batch(u32 *start,u32 *count,u32 *budget,int quarantine)
{
    if(!*count)return;
    if(quarantine){*count=0;return;}
    u32 take=*count;if(take>*budget)take=*budget;
    if(take){allocated(*start,take,0);*start+=take;*count-=take;*budget-=take;}
}
static int lease_candidate_pages_step(lease_record_t *r)
{
    if(!r->candidate)return 1;
    u32 count=r->candidate_pages-r->candidate_freed;if(count>16)count=16;
    pframe_free_run((u32)r->candidate+r->candidate_freed*4096u,count);r->candidate_freed+=count;
    if(r->candidate_freed!=r->candidate_pages)return 0;
    r->candidate=0;r->candidate_pages=r->candidate_freed=0;r->stage=0;return 1;
}
static int lease_cleanup_step(lease_record_t *r)
{
    if(r->cancel){
        /* 只从本人dependency/child链取消一个真实对象。父记录在全部
         * child和borrower脱链前仍活，不在每个退出上扫描所有owner。 */
        u32 borrower=r->target_borrow_head?r->target_borrow_head:r->namespace_borrow_head;
        if(borrower){lease_record_t *other=lease_get(borrower);lease_mark_cancel(other,0);return 0;}
        if(r->child_head){
            lease_record_t *leaf=lease_get(r->child_head);
            while(leaf->child_head)leaf=lease_get(leaf->child_head);
            lease_mark_cancel(leaf,2);
            if(lease_cleanup_step(leaf)){lease_work_remove(leaf);lease_free(leaf);}
            return 0;
        }
        if(!r->cleanup_initialized){
            r->cleanup_initialized=1;
            native_file_t *n=native_object(r->object);
            if(n && n->unpublished){r->cleanup_start=n->draft.start_lba;r->cleanup_count=(n->draft.size+511u)/512u;}
        }
    }
    u32 budget=128;int quarantine=(r->flags&SC32_LEASE_FLAG_UNCERTAIN)!=0;
    /* 位图回收也有预算，避免64MiB候选取消时在IF=0走无界循环。
     * 不确定提交只撤RAM/句柄，可能被磁盘bank引用的extent保留。 */
    lease_extent_batch(&r->garbage_start,&r->garbage_count,&budget,quarantine || fs_faulted);
    lease_extent_batch(&r->cleanup_pending_start,&r->cleanup_pending_count,&budget,quarantine);
    lease_extent_batch(&r->cleanup_start,&r->cleanup_count,&budget,quarantine || fs_faulted);
    int pages_done=lease_candidate_pages_step(r);
    if(r->garbage_count || r->cleanup_pending_count || r->cleanup_count || !pages_done)return 0;
    if(!r->cancel){
        r->stage=0;
        if(r->queued)task_notify_generation((int)r->owner,r->owner_generation,TASK_EVENT_STREAM);
        lease_work_remove(r);return 1;
    }
    if(r->snapshot_head){
        lease_snapshot_t *next=r->snapshot_head->next;
        pframe_free_run((u32)r->snapshot_head,1);r->snapshot_head=next;
        r->snapshot_cache=0;r->snapshot_cache_base=0;
        if(next)return 0;
        r->snapshot_tail=0;r->members=0;
    }
    native_file_t *n=native_object(r->object);
    int candidate=(r->flags&SC32_LEASE_FLAG_CANDIDATE)!=0;
    if(n && n->unpublished){n->draft.size=0;n->draft.start_lba=fs_data_start;}
    u32 handle=r->handle;r->handle=0;
    if(handle)sc32_file_lease_unbind((int)r->owner,handle,r->id,candidate);
    lease_object_unlink(r);lease_unlink(r,0);lease_unlink(r,1);
    r->flags|=SC32_LEASE_FLAG_REVOKED;r->snapshot_valid=0;
    if(r->release)r->state=SC32_LEASE_RELEASED;
    if(r->queued)task_notify_generation((int)r->owner,r->owner_generation,TASK_EVENT_STREAM);
    lease_work_remove(r);return 1;
}
int fs_lease_detach_step(int pid,u32 handle,u32 id)
{
    lease_record_t *r=lease_get(id);
    if(!r)return 1;
    if(r->owner!=(u32)pid || r->owner_generation!=task_generation(pid) || r->handle!=handle)return 0;
    lease_mark_cancel(r,2);r->detaching=1;
    /* 这里是关闭安全点：活metadata已经确定完成或已丢候选bank，
     * 后台只用lease自有FS强pin/extent/内核数组，不再借FD或pd。
     * 因而先解除FILE绑定并允许原FD下降，GC保留最后强pin至结束。 */
    sc32_file_lease_unbind(pid,handle,id,0);r->handle=0;
    if(!(r->flags&SC32_LEASE_FLAG_CANDIDATE))lease_object_unlink(r);
    return 1;
}
static void lease_advance_owned(lease_record_t *r)
{
    if(!lease_owner_valid(r))lease_mark_cancel(r,2);
    if(r->stage>=L_WAIT && r->stage<=L_SUPER)lease_metadata_step(r);
    else if(r->queued){
        int done=lease_cleanup_step(r);
        if(done && r->release==2)lease_free(r);
    }
}
int fs_lease_control(int pid,u32 operation,const Sc32LeaseControlRequest *q,ScLeaseInfo *out)
{
    if(q->flags || q->reserved[0] || q->reserved[1])return SC32_E_INVALID;
    lease_record_t *r=lease_owned(pid,q->lease,q->generation);if(!r)return SC32_E_STALE;
    if(q->operation_sequence!=r->sequence && !(operation==SC32_L_QUERY && !q->operation_sequence))return SC32_E_STALE;
    if(operation==SC32_L_QUERY){lease_information(r,out);return 0;}
    if(operation==SC32_L_RELEASE){
        lease_mark_cancel(r,1);
        if(lease_cleanup_step(r)){
            lease_information(r,out);lease_free(r);return 0;
        }
        lease_information(r,out);return SC32_PENDING;
    }
    if(operation==SC32_L_CANCEL){
        if(r->release)return SC32_E_BUSY;
        if(r->state==SC32_LEASE_COMMITTED){lease_information(r,out);return 0;}
        lease_mark_cancel(r,0);int done=lease_cleanup_step(r);lease_information(r,out);return done?0:SC32_PENDING;
    }
    if(operation!=SC32_L_ADVANCE)return SC32_E_INVALID;
    lease_advance_owned(r);r=lease_owned(pid,q->lease,q->generation);
    if(!r)return SC32_E_STALE;
    if(r->release && r->state==SC32_LEASE_RELEASED){lease_information(r,out);lease_free(r);return 0;}
    lease_information(r,out);
    if(r->state==SC32_LEASE_PENDING_STATE || r->queued)return SC32_PENDING;
    return r->cancel?0:r->result;
}
static entry_t *lease_snapshot_entry(lease_record_t *r,u32 index)
{
    if(!r->snapshot_cache || index<r->snapshot_cache_base){r->snapshot_cache=r->snapshot_head;r->snapshot_cache_base=0;}
    while(r->snapshot_cache && index-r->snapshot_cache_base>=r->snapshot_cache->used){
        r->snapshot_cache_base+=r->snapshot_cache->used;r->snapshot_cache=r->snapshot_cache->next;
    }
    return r->snapshot_cache?&r->snapshot_cache->rows[index-r->snapshot_cache_base]:0;
}
static int lease_snapshot_permission(lease_record_t *r,const entry_t *e)
{
    if((r->flags&SC32_LEASE_FLAG_CANDIDATE) && same(e->name,r->name))
        return lease_parent_read((int)r->owner,r->name)
            && permission(auth_subject_uid((int)r->owner),auth_subject_gid((int)r->owner),e->uid,e->gid,e->mode,FS_ACCESS_READ);
    return fs_access((int)r->owner,e->name,FS_ACCESS_META);
}
static int lease_snapshot_root_access(lease_record_t *r)
{
    if(r->metadata_only)return 0;
    if(r->flags&SC32_LEASE_FLAG_CANDIDATE)
        return lease_parent_read((int)r->owner,r->name)
            && permission(auth_subject_uid((int)r->owner),auth_subject_gid((int)r->owner),
                r->draft.uid,r->draft.gid,r->draft.mode,FS_ACCESS_READ);
    return fs_access((int)r->owner,r->name,FS_ACCESS_READ);
}
int fs_lease_snapshot(int pid,const Sc32LeaseSnapshotRequest *q,ScLeasePage *out)
{
    if(q->flags || q->reserved[0] || q->reserved[1] || q->capacity>SC32_LEASE_BATCH_MAX)return SC32_E_INVALID;
    lease_record_t *r=lease_owned(pid,q->lease,q->generation);if(!r)return SC32_E_STALE;
    if(r->kind!=SC32_LEASE_NAMESPACE)return SC32_E_INVALID;
    if(!lease_snapshot_root_access(r))return SC32_E_ACCESS;
    if(!r->snapshot_valid || !lease_live(r))return SC32_E_STALE;
    if(q->cursor>r->members)return SC32_E_RANGE;
    u32 count=r->members-q->cursor;if(count>q->capacity)count=q->capacity;
    /* 在发布任一row之前预验这一页所有当前权限，避免第N行拒绝时
     * 已把前N-1行写进用户out；内容只来自固定成员snapshot。 */
    for(u32 i=0;i<count;i++){
        entry_t *entry=lease_snapshot_entry(r,q->cursor+i);
        if(!entry || !lease_snapshot_permission(r,entry))return SC32_E_ACCESS;
    }
    lease_zero(out,sizeof(*out));out->size=sizeof(*out);out->version=1;
    out->lease=r->id;out->generation=r->generation;out->count=count;out->cursor=q->cursor+count;out->snapshot_epoch=r->epoch;
    if(out->cursor==r->members)out->flags=SC32_LEASE_PAGE_FINISHED;
    return 0;
}
int fs_lease_snapshot_row(int pid,u32 id,u32 generation,u32 index,ScLeaseEntry *out)
{
    lease_record_t *r=lease_owned(pid,id,generation);
    if(r && !lease_snapshot_root_access(r))return SC32_E_ACCESS;
    if(!r || !r->snapshot_valid || index>=r->members)return SC32_E_STALE;
    entry_t *e=lease_snapshot_entry(r,index);if(!e || !lease_snapshot_permission(r,e))return SC32_E_ACCESS;
    lease_zero(out,sizeof(*out));out->size=sizeof(*out);out->version=1;
    out->kind=e->start_lba?1u:2u;out->length=e->size;out->revision=e->generation;
    out->uid=e->uid;out->gid=e->gid;out->rw=e->mode;out->flags=SC32_LEASE_ROW_REVISION_ONLY;
    out->index=index;out->snapshot_epoch=r->epoch;out->path[0]='/';
    for(u32 i=0;e->name[i];i++)out->path[i+1]=e->name[i];
    return 0;
}
void fs_lease_poll(void)
{
    if(!lease_initialized)return;
    u32 id=lease_meta_owner?lease_meta_owner:lease_work_head;
    lease_record_t *r=lease_get(id);if(!r)return;
    lease_advance_owned(r);r=lease_get(id);
    if(r && r->queued && lease_meta_owner!=id){lease_work_remove(r);lease_work_add(r);}
    if(lease_work_head)task_kernel_wake();
}
int fs_lease_stop_step(int pid)
{
    if(!lease_initialized)return 1;
    lease_owner_t *o=objpool_get(&lease_owners,pid);if(!o)return 1;
    if(o->generation!=task_generation(pid))return 0;
    /* 一轮只推进至多32个已经安全完成的record；遇长extent GC只给
     * 128sector预算便让出。状态2逻辑撤权已生效，不能暂停无关ready。 */
    for(u32 budget=32;budget && o->head;budget--){
        lease_record_t *r=lease_get(o->head);if(!r)return 0;
        lease_mark_cancel(r,2);
        if(!lease_cleanup_step(r))return 0;
        lease_free(r);o=objpool_get(&lease_owners,pid);if(!o)return 1;
    }
    return !o->head;
}
static void lease_volume_reset(void)
{
    /* 仅fs_init的启动/挂载拆卷路径；正常exit不用全pool遍历。它发生
     * 在native_pool与dirents释放之前，私有candidate RAM逐份还页。 */
    if(!lease_initialized)return;
    for(int id=objpool_next(&lease_pool,-1);id>=0;id=objpool_next(&lease_pool,id)){
        lease_record_t *r=objpool_get(&lease_pool,id);lease_snapshot_drop(r);
        if(r->candidate && r->candidate_pages>r->candidate_freed)
            pframe_free_run((u32)r->candidate+r->candidate_freed*4096u,r->candidate_pages-r->candidate_freed);
    }
    objpool_destroy(&lease_pool);objpool_destroy(&lease_owners);objpool_destroy(&lease_names);
    lease_name_root=lease_work_head=lease_work_tail=lease_meta_owner=0;lease_initialized=0;
}
