static int lease_binding_check(lease_record_t *r,int unique)
{
    if(!lease_owner_valid(r) || !r->handle)return SC32_E_STALE;
    fs_lease_binding_t b;int status=sc32_file_lease_binding((int)r->owner,r->handle,r->handle_kind,&b);
    if(status)return status;
    if(b.lease!=r->id || b.object!=r->object || !same(b.path,r->name))return SC32_E_STALE;
    if(r->object){
        native_file_t *n=native_object(r->object);
        if(!n || n->invalidated || n->pending)return SC32_E_BUSY;
        if(unique && (n->refs!=2 || b.aliases!=1 || n->lease_head!=r->id || r->object_next))return SC32_E_BUSY;
    }
    return 0;
}
static int lease_tree_pins(lease_record_t *r)
{
    for(int i=objpool_next(&native_pool,-1);i>=0;i=objpool_next(&native_pool,i)){
        native_file_t *n=objpool_get(&native_pool,i);
        if(!same(n->name,r->name) && !child(n->name,r->name))continue;
        lease_record_t *held=lease_get(n->lease_head);
        if(!held || !lease_ancestor(r->id,held->id) || n->refs!=2 || held->object_next
           || n->pending || lease_binding_check(held,1))return SC32_E_BUSY;
    }
    return 0;
}
static lease_record_t *lease_tree_next(lease_record_t *root,lease_record_t *current)
{
    if(current->child_head)return lease_get(current->child_head);
    while(current && current!=root){
        if(current->child_next)return lease_get(current->child_next);
        current=lease_get(current->parent);
    }
    return 0;
}
static int lease_tree_ready(lease_record_t *r,const char *target)
{
    u32 old_length=(u32)len(r->name),new_length=(u32)len(target);
    for(lease_record_t *child_record=lease_tree_next(r,r);child_record;child_record=lease_tree_next(r,child_record)){
        if(child_record->state==SC32_LEASE_PENDING_STATE || child_record->release || child_record->target_borrow_head
           || child_record->namespace_borrow_head || child_record->garbage_count)return SC32_E_BUSY;
        if(child_record->node[0] && new_length+(u32)len(child_record->name)-old_length>FS_NAME_MAX)return SC32_E_RANGE;
    }
    return lease_tree_pins(r);
}
static int lease_source_condition(lease_record_t *r,int full_tree)
{
    int pid=(int)r->owner;
    if(!lease_owner_valid(r) || r->read_only || !lease_parent_access(r,pid,1))return SC32_E_ACCESS;
    int status=lease_binding_check(r,1);if(status)return status;
    if(r->operation==SC32_LEASE_OP_SET_MODE){
        /* chmod的授权沿旧fs_permissions：META+真正owner/管理者，不
         * 偷加fileW/parentW。pin只固定身份，不能替代这次授权检查。 */
        if((r->flags&SC32_LEASE_FLAG_CANDIDATE) || !lease_mutable(pid,r->name,0)
           || !fs_access(pid,r->name,FS_ACCESS_META))return SC32_E_ACCESS;
        int index=find(r->name);if(index<0)return SC32_E_STALE;
        entry_t *entry=dirents+index;
        if((entry->start_lba?1u:2u)!=r->handle_kind || r->expected_source_object!=r->object
           || entry->generation!=r->expected_source_revision
           || (r->handle_kind==2 && r->expected_epoch!=r->epoch))return SC32_E_STALE;
        if(auth_uid(pid)!=entry->uid && !auth_can_manage(pid))return SC32_E_ACCESS;
        if((entry->uid==AUTH_SYSTEM || entry->gid==AUTH_SYSTEM) && !auth_can_mod(pid))return SC32_E_ACCESS;
        return 0;
    }
    if(!lease_mutable(pid,r->name,full_tree && r->handle_kind==2))return SC32_E_ACCESS;
    if(r->flags&SC32_LEASE_FLAG_CANDIDATE){
        if(!lease_parent_read(pid,r->name))return SC32_E_ACCESS;
    }else if(!fs_access_parent(pid,r->name))return SC32_E_ACCESS;
    if(r->expected_source_object!=r->object)return SC32_E_STALE;
    if(r->handle_kind==2){
        if(r->expected_epoch!=r->epoch)return SC32_E_STALE;
        if(r->flags&SC32_LEASE_FLAG_CANDIDATE){
            if(!permission(auth_subject_uid(pid),auth_subject_gid(pid),r->draft.uid,r->draft.gid,r->draft.mode,FS_ACCESS_READ))return SC32_E_ACCESS;
            if(r->expected_source_revision!=r->draft.generation)return SC32_E_STALE;
        }else{
            if(!fs_access(pid,r->name,FS_ACCESS_META))return SC32_E_ACCESS;
            int index=find(r->name);
            if(index<0 || dirents[index].start_lba || dirents[index].generation!=r->expected_source_revision)return SC32_E_STALE;
        }
    }else{
        native_file_t *n=native_object(r->object);entry_t *entry=native_current(n);
        if(!entry)return SC32_E_STALE;
        if(!n->unpublished && !fs_access(pid,r->name,FS_ACCESS_META))return SC32_E_ACCESS;
        if(n->unpublished && !permission(auth_subject_uid(pid),auth_subject_gid(pid),entry->uid,entry->gid,entry->mode,FS_ACCESS_WRITE))return SC32_E_ACCESS;
        if(entry->generation!=r->expected_source_revision)return SC32_E_STALE;
        if(n->unpublished && n->flushed_revision!=entry->generation)return SC32_E_BUSY;
        if(n->uncertain)return SC32_E_IO;
    }
    return 0;
}
static int lease_destination_condition(lease_record_t *r)
{
    int pid=(int)r->owner;lease_record_t *ns=lease_get(r->namespace_lease);
    if(ns){
        if(!lease_live(ns) || !lease_owner_valid(ns) || ns->read_only || ns->metadata_only || ns->kind!=SC32_LEASE_NAMESPACE
           || !child(r->target,ns->name) || !fs_access(pid,ns->name,FS_ACCESS_READ))return SC32_E_ACCESS;
    }
    if(!lease_mutable(pid,r->target,0) || !fs_access_parent(pid,r->target))return SC32_E_ACCESS;
    char parent[64];copy(parent,r->target);int count=len(parent);
    while(count && parent[count-1]!='/')count--;
    if(count)parent[count-1]=0;else parent[0]=0;
    u32 stat[2];if(fs_stat(parent,stat) || stat[0]!=2)return SC32_E_INVALID;
    if(lease_conflict(r->target,FS_LEASE_TREE,r->id,r->target_lease,r->namespace_lease)
       || transaction_busy(r->target))return SC32_E_BUSY;
    int index=find(r->target);
    if(r->condition&SC32_LEASE_EXPECT_ABSENT)return index<0?0:SC32_E_EXISTS;
    lease_record_t *target=lease_get(r->target_lease);
    if(!target || target->owner!=r->owner || !lease_owner_valid(target) || !lease_live(target)
       || target->read_only || !same(target->name,r->target) || index<0
       || target->object!=r->expected_target_object || dirents[index].generation!=r->expected_target_revision)return SC32_E_STALE;
    int status=lease_binding_check(target,1);if(status)return status;
    if(!lease_parent_access(target,pid,1) || !fs_access(pid,target->name,FS_ACCESS_META))return SC32_E_ACCESS;
    if(target->handle_kind!=r->handle_kind)return SC32_E_INVALID;
    if(target->handle_kind==2){
        if(!fs_access(pid,target->name,FS_ACCESS_META))return SC32_E_ACCESS;
        if(target->child_head)return SC32_E_BUSY;
        for(int i=0;i<nfiles;i++)if(child(dirents[i].name,target->name))return SC32_E_BUSY;
    }
    return 0;
}
static int lease_operation_condition(lease_record_t *r)
{
    if(fs_faulted || version!=5)return SC32_E_IO;
    int status=lease_source_condition(r,1);if(status)return status;
    if(transaction_busy(r->name) || lease_conflict(r->name,FS_LEASE_TREE,r->id,r->target_lease,r->namespace_lease))return SC32_E_BUSY;
    if(r->operation==SC32_LEASE_OP_SET_MODE)return 0;
    if(r->operation==SC32_LEASE_OP_REMOVE){
        if(r->handle_kind==2)for(int i=0;i<nfiles;i++)if(child(dirents[i].name,r->name))return SC32_E_BUSY;
        return 0;
    }
    if(same(r->name,r->target) || child(r->target,r->name))return SC32_E_INVALID;
    status=lease_destination_condition(r);if(status)return status;
    if(r->handle_kind==2)return lease_tree_ready(r,r->target);
    return 0;
}
static void lease_borrow(lease_record_t *r,lease_record_t *target,lease_record_t *ns)
{
    if(target){
        r->target_lease=target->id;r->target_next=target->target_borrow_head;target->target_borrow_head=r->id;
        if(r->target_next)lease_get(r->target_next)->target_previous=r->id;
    }
    if(ns){
        r->namespace_lease=ns->id;r->namespace_next=ns->namespace_borrow_head;ns->namespace_borrow_head=r->id;
        if(r->namespace_next)lease_get(r->namespace_next)->namespace_previous=r->id;
    }
}
static void lease_unborrow(lease_record_t *r)
{
    lease_record_t *target=lease_get(r->target_lease),*ns=lease_get(r->namespace_lease);
    if(r->target_previous)lease_get(r->target_previous)->target_next=r->target_next;
    else if(target)target->target_borrow_head=r->target_next;
    if(r->target_next)lease_get(r->target_next)->target_previous=r->target_previous;
    if(r->namespace_previous)lease_get(r->namespace_previous)->namespace_next=r->namespace_next;
    else if(ns)ns->namespace_borrow_head=r->namespace_next;
    if(r->namespace_next)lease_get(r->namespace_next)->namespace_previous=r->namespace_previous;
    r->target_lease=r->namespace_lease=0;r->target_previous=r->target_next=r->namespace_previous=r->namespace_next=0;
}
static int lease_accept(lease_record_t *r,ScLeaseInfo *out)
{
    if(r->sequence==0xffffffffu)return SC32_E_RANGE;
    r->sequence++;r->state=SC32_LEASE_PENDING_STATE;r->stage=L_WAIT;r->result=SC32_PENDING;r->raw=0;
    r->committed=r->removed_valid=r->copy_cursor=r->candidate_count=r->serialize_offset=r->bank_sector=r->bank_crc=0;
    r->flags&=~SC32_LEASE_FLAG_DURABLE;lease_work_add(r);lease_information(r,out);return SC32_PENDING;
}
int fs_lease_begin(int pid,const Sc32LeasePublishRequest *q,const char *target_name,int rename,ScLeaseInfo *out)
{
    for(u32 i=0;i<8;i++)if(q->reserved[i])return SC32_E_INVALID;
    if(!target_name[0] || !q->expected_source_revision || ((q->flags&3u)!=1 && (q->flags&3u)!=2) || (q->flags&~7u)
       || (rename && ((q->flags&SC32_LEASE_SET_MODE) || q->final_mode))
       || (!(q->flags&SC32_LEASE_SET_MODE) && q->final_mode) || q->final_mode>63)return SC32_E_INVALID;
    lease_record_t *r=lease_owned(pid,q->source_lease,q->source_generation);
    if(!r)return SC32_E_STALE;
    if(!lease_live(r) || r->target_borrow_head || r->namespace_borrow_head || r->queued || r->candidate)return SC32_E_BUSY;
    if(r->handle_kind==1 && q->expected_snapshot_epoch)return SC32_E_INVALID;
    if(r->handle_kind==2 && q->expected_source_object)return SC32_E_INVALID;
    if(rename?(r->flags&SC32_LEASE_FLAG_CANDIDATE):!(r->flags&SC32_LEASE_FLAG_CANDIDATE))return SC32_E_INVALID;
    lease_record_t *ns=0,*target=0;
    int status=lease_parent_check(pid,q->namespace_lease,q->namespace_generation,target_name,1,&ns);if(status)return status;
    if(q->flags&SC32_LEASE_EXPECT_ABSENT){
        if(q->target_lease || q->target_generation || q->expected_target_object || q->expected_target_revision)return SC32_E_INVALID;
    }else{
        target=lease_owned(pid,q->target_lease,q->target_generation);
        if(!target)return SC32_E_STALE;
        if(target==r || !q->expected_target_revision || (target->flags&SC32_LEASE_FLAG_CANDIDATE)
           || target->target_borrow_head || target->namespace_borrow_head)return SC32_E_INVALID;
        if(target->handle_kind==2 && q->expected_target_object)return SC32_E_INVALID;
    }
    if(r==ns || target==ns || (ns && lease_ancestor(r->id,ns->id)))return SC32_E_INVALID;
    u32 previous_operation=r->operation;
    r->operation=rename?SC32_LEASE_OP_RENAME:SC32_LEASE_OP_PUBLISH;
    r->expected_source_object=q->expected_source_object;r->expected_source_revision=q->expected_source_revision;
    r->expected_target_object=q->expected_target_object;r->expected_target_revision=q->expected_target_revision;
    r->expected_epoch=q->expected_snapshot_epoch;r->final_mode=q->final_mode;r->condition=q->flags;copy(r->target,target_name);
    /* 先只记录ID作preflight，真正borrow/list/名字保留在全部校验之后。 */
    r->target_lease=target?target->id:0;r->namespace_lease=ns?ns->id:0;
    status=lease_operation_condition(r);
    r->target_lease=r->namespace_lease=0;
    if(status){r->operation=previous_operation;return status;}
    if(r->sequence==0xffffffffu){r->operation=previous_operation;return SC32_E_RANGE;}
    status=lease_link(r,1,r->target);if(status){r->operation=previous_operation;return status;}
    lease_borrow(r,target,ns);return lease_accept(r,out);
}
int fs_lease_remove(int pid,const Sc32LeaseRemoveRequest *q,ScLeaseInfo *out)
{
    for(u32 i=0;i<4;i++)if(q->reserved[i])return SC32_E_INVALID;
    if(q->flags || !q->expected_revision)return SC32_E_INVALID;
    lease_record_t *r=lease_owned(pid,q->lease,q->generation);if(!r)return SC32_E_STALE;
    if(r->handle_kind==1 && q->expected_snapshot_epoch)return SC32_E_INVALID;
    if(r->handle_kind==2 && q->expected_object)return SC32_E_INVALID;
    if(!lease_live(r) || r->target_borrow_head || r->namespace_borrow_head || r->child_head || r->queued || r->candidate)return SC32_E_BUSY;
    if(r->flags&SC32_LEASE_FLAG_CANDIDATE)return SC32_E_INVALID;
    if(r->sequence==0xffffffffu)return SC32_E_RANGE;
    u32 previous_operation=r->operation;
    r->operation=SC32_LEASE_OP_REMOVE;r->expected_source_object=q->expected_object;
    r->expected_source_revision=q->expected_revision;r->expected_epoch=q->expected_snapshot_epoch;
    int status=lease_operation_condition(r);if(status){r->operation=previous_operation;return status;}
    return lease_accept(r,out);
}
int fs_lease_set_mode(int pid,const Sc32LeaseModeRequest *q,ScLeaseInfo *out)
{
    for(u32 i=0;i<3;i++)if(q->reserved[i])return SC32_E_INVALID;
    if(q->flags || !q->expected_revision || q->final_mode>63)return SC32_E_INVALID;
    lease_record_t *r=lease_owned(pid,q->lease,q->generation);if(!r)return SC32_E_STALE;
    if((r->handle_kind==1 && q->expected_snapshot_epoch) || (r->handle_kind==2 && q->expected_object))return SC32_E_INVALID;
    if(!lease_live(r) || r->target_borrow_head || r->namespace_borrow_head || r->queued || r->candidate)return SC32_E_BUSY;
    if(r->flags&SC32_LEASE_FLAG_CANDIDATE)return SC32_E_INVALID;
    if(r->sequence==0xffffffffu)return SC32_E_RANGE;
    u32 previous=r->operation;
    r->operation=SC32_LEASE_OP_SET_MODE;r->expected_source_object=q->expected_object;
    r->expected_source_revision=q->expected_revision;r->expected_epoch=q->expected_snapshot_epoch;
    r->final_mode=q->final_mode;r->condition=0;
    int status=lease_operation_condition(r);if(status){r->operation=previous;return status;}
    return lease_accept(r,out);
}
