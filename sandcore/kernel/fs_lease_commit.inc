static void lease_finish(lease_record_t *r,int status,int raw,int uncertain)
{
    if(lease_meta_owner==r->id)lease_meta_owner=0;
    lease_unborrow(r);lease_unlink(r,1);r->stage=r->candidate?L_DISPOSE:0;
    r->result=status;r->raw=raw;
    if(uncertain){
        /* 不知道超级块是否已经切bank时，两份可能被引用的extent都
         * 留在位图。运行卷只读，不能把释放候选冒充磁盘rollback。 */
        fs_faulted=1;r->state=SC32_LEASE_UNCERTAIN_STATE;r->flags|=SC32_LEASE_FLAG_UNCERTAIN;
    }else r->state=status==SC32_E_CANCELLED?SC32_LEASE_CANCELLED_STATE:SC32_LEASE_FAILED_STATE;
    if(r->candidate || r->garbage_count || r->release)lease_work_add(r);else lease_work_remove(r);
    task_notify_generation((int)r->owner,r->owner_generation,TASK_EVENT_STREAM);
}
static int lease_copy_batch(lease_record_t *r)
{
    u32 budget=128;
    while(r->copy_cursor<(u32)nfiles && budget--){
        entry_t e=dirents[r->copy_cursor++];
        int source=same(e.name,r->name);
        int destination=(r->operation==SC32_LEASE_OP_PUBLISH || r->operation==SC32_LEASE_OP_RENAME) && same(e.name,r->target);
        if((r->operation==SC32_LEASE_OP_REMOVE && source) || destination){
            r->removed=e;r->removed_valid=1;continue;
        }
        if(r->operation==SC32_LEASE_OP_RENAME && (source || child(e.name,r->name))){
            int old=len(r->name),next=len(r->target),tail=len(e.name+old);
            if(next+tail>FS_NAME_MAX)return SC32_E_RANGE;
            char suffix[64];copy(suffix,e.name+old);lease_zero(e.name,sizeof(e.name));
            copy(e.name,r->target);copy(e.name+next,suffix);
            e.generation=new_generation();if(!e.generation)return SC32_E_RANGE;
        }
        if(r->operation==SC32_LEASE_OP_SET_MODE && source){
            e.mode=r->final_mode;e.generation=new_generation();if(!e.generation)return SC32_E_RANGE;
        }
        if(r->candidate_count>=dir_capacity)return SC32_E_NO_SPACE;
        r->candidate[r->candidate_count++]=e;
    }
    if(r->copy_cursor<(u32)nfiles)return SC32_PENDING;
    if(r->operation==SC32_LEASE_OP_PUBLISH){
        if(r->candidate_count>=dir_capacity)return SC32_E_NO_SPACE;
        native_file_t *n=native_object(r->object);entry_t *current=native_current(n);
        if(n && !current)return SC32_E_STALE;
        entry_t e=n?*current:r->draft;
        lease_zero(e.name,sizeof(e.name));copy(e.name,r->target);
        if(r->condition&SC32_LEASE_PRESERVE_TARGET_METADATA){
            if(!r->removed_valid || r->removed.generation!=r->expected_target_revision)return SC32_E_STALE;
            e.uid=r->removed.uid;e.gid=r->removed.gid;e.mode=r->removed.mode;e.flags=r->removed.flags;
        }
        if(r->condition&SC32_LEASE_SET_MODE)e.mode=r->final_mode;
        e.generation=new_generation();if(!e.generation)return SC32_E_RANGE;
        r->candidate[r->candidate_count++]=e;
    }
    r->object_sequence=object_generation;r->stage=L_SERIALIZE;return SC32_PENDING;
}
static void lease_serialize_batch(lease_record_t *r)
{
    u32 start=r->serialize_offset,bytes=dir_sectors*512u-start;
    if(bytes>16u*512u)bytes=16u*512u;
    u32 end=start+bytes;for(u32 i=start;i<end;i++)meta[i]=0;
    for(u32 index=start/96u;index<r->candidate_count && index*96u<end;index++){
        u8 encoded[96];lease_zero(encoded,sizeof(encoded));native_serialize(encoded,r->candidate+index);
        u32 at=index*96u,first=at<start?start-at:0,last=96;
        if(at+last>end)last=end-at;
        for(u32 i=first;i<last;i++)meta[at+i]=encoded[i];
    }
    r->serialize_offset=end;
    if(end==dir_sectors*512u){
        *(u32 *)(r->super+12)=r->candidate_count;*(u32 *)(r->super+48)=r->bank;
        *(u32 *)(r->super+52)=r->bank_generation;*(u32 *)(r->super+56)=r->object_sequence;
        r->stage=L_BANK;
    }
}
static void lease_remove_child_link(u32 parent,u32 child_id)
{
    lease_name_node_t *p=lease_node(parent);u32 *link=&p->child;
    while(*link && *link!=child_id)link=&lease_node(*link)->sibling;
    if(*link)*link=lease_node(child_id)->sibling;
}
static int lease_move_budget(lease_record_t *r)
{
    if(r->handle_kind!=2 || !r->node[0] || !r->node[1])return 0;
    u32 branch=lease_child(r->node[0],'/'),count=branch?lease_node(branch)->subtree:0;
    for(u32 id=r->node[1];id;id=lease_node(id)->parent)
        if(lease_node(id)->subtree>0xffffffffu-count-1u)return SC32_E_RANGE;
    return 0;
}
static void lease_move_index(lease_record_t *r)
{
    u32 old=r->node[0],next=r->node[1];
    /* 原地Publish只撤slot1的提交期预留，保持slot0及子树索引原样。
     * 不拆装同一叶子，避免把无名称变化的提交误作目录分支搬迁。 */
    if(old==next){lease_unlink(r,1);return;}
    if(r->handle_kind==2){
        u32 branch=lease_child(old,'/');
        if(branch){
            u32 count=lease_node(branch)->subtree;
            lease_remove_child_link(old,branch);
            for(u32 id=old;id;id=lease_node(id)->parent)lease_node(id)->subtree-=count;
            lease_name_node_t *n=lease_node(branch),*p=lease_node(next);
            n->parent=next;n->sibling=p->child;p->child=branch;
            for(u32 id=next;id;id=lease_node(id)->parent)lease_node(id)->subtree+=count;
        }
    }
    lease_unlink(r,0);
    /* next已由slot1预留，最后提交后这里不再分配，也不允许OOM。 */
    lease_name_node_t *n=lease_node(next);u32 token=r->id<<1;
    r->node[0]=next;r->name_next[0]=n->head;
    if(n->head)lease_token(n->head)->name_previous[n->head&1u]=token;
    n->head=token;
    for(u32 id=next;id;id=lease_node(id)->parent)lease_node(id)->subtree++;
    lease_unlink(r,1);
}
static void lease_rebind_tree(lease_record_t *r,const char *old_name)
{
    u32 prefix=(u32)len(old_name),new_length=(u32)len(r->target);
    for(lease_record_t *item=r;item;item=lease_tree_next(r,item)){
        if(!item->node[0])continue;
        char suffix[64];copy(suffix,item->name+prefix);lease_zero(item->name,64);
        copy(item->name,r->target);copy(item->name+new_length,suffix);
        item->snapshot_valid=0;
        native_file_t *n=native_object(item->object);
        if(n){
            lease_zero(n->name,64);copy(n->name,item->name);
            if(n->unpublished){lease_zero(n->draft.name,64);copy(n->draft.name,item->name);}
            else native_index(n);
        }
        if(item->handle)sc32_file_lease_rebind((int)item->owner,item->handle,item->name);
        int index=find(item->name);if(index>=0)item->revision=dirents[index].generation;
        item->epoch=item->metadata_only?0:native_directory_sequence;
    }
}
static void lease_retire_replaced(lease_record_t *target)
{
    if(!target)return;
    native_file_t *n=native_object(target->object);if(n)n->invalidated=1;
    lease_unlink(target,0);target->snapshot_valid=0;target->flags|=SC32_LEASE_FLAG_REVOKED;
    target->state=SC32_LEASE_CANCELLED_STATE;target->result=SC32_E_STALE;target->raw=FS_N_STALE;
    /* 目标只有此lease+唯一description，preflight已拒foreign/dup pin。
     * 先撤handle绑定，再降强pin，不让旧target FD认到同名新source。 */
    u32 handle=target->handle;target->handle=0;
    if(handle)sc32_file_lease_unbind((int)target->owner,handle,target->id,1);
    lease_object_unlink(target);lease_work_add(target);
}
static void lease_commit_memory(lease_record_t *r)
{
    char old_name[64];copy(old_name,r->name);lease_record_t *target=lease_get(r->target_lease);
    lease_record_t *destination_parent=lease_get(r->namespace_lease);
    if(!destination_parent && target)destination_parent=lease_get(target->parent);
    /* 单次指针交换保证lookup永远只见一个完整目录，不分批覆盖活
     * dirents而把半棵rename暴露给其它进程。旧数组改归本job的GC。 */
    entry_t *old=dirents;dirents=r->candidate;r->candidate=old;
    r->candidate_pages=dir_pages;r->candidate_freed=0;nfiles=(int)r->candidate_count;
    active_bank=r->bank;metadata_generation=r->bank_generation;
    published_object_generation=r->object_sequence;native_directory_sequence++;
    lease_retire_replaced(target);
    if(r->operation==SC32_LEASE_OP_SET_MODE){
        /* 名字、uid/gid与extent完全不动；只更新这个身份的revision。
         * 目录旧snapshot属性已失效，不能继续发布旧mode当当前授权。 */
        int index=find(r->name);if(index>=0)r->revision=dirents[index].generation;
        r->snapshot_valid=0;
    }else if(r->operation==SC32_LEASE_OP_REMOVE){
        native_file_t *n=native_object(r->object);if(n)n->invalidated=1;
        u32 handle=r->handle;r->handle=0;r->snapshot_valid=0;lease_unlink(r,0);
        if(handle)sc32_file_lease_unbind((int)r->owner,handle,r->id,1);
        lease_object_unlink(r);r->flags|=SC32_LEASE_FLAG_REVOKED;
    }else{
        lease_move_index(r);
        native_file_t *n=native_object(r->object);
        if(n)n->unpublished=0;
        r->flags&=~SC32_LEASE_FLAG_CANDIDATE;lease_rebind_tree(r,old_name);
        /* 移到另一namespace后必须转移显式父grant；不能继续借旧父
         * 权限，也不能让旧目录Close取消已经移出的目标保护。 */
        lease_record_t *old_parent=lease_get(r->parent);
        if(destination_parent || (old_parent && !child(r->name,old_parent->name))){
            lease_parent_unlink(r);lease_parent_link(r,destination_parent);
        }
    }
    if(r->removed_valid && r->removed.start_lba){
        r->cleanup_pending_start=r->removed.start_lba;r->cleanup_pending_count=(r->removed.size+511u)/512u;
    }
    r->committed=1;r->state=SC32_LEASE_COMMITTED;r->flags|=SC32_LEASE_FLAG_DURABLE;
    r->result=r->raw=0;r->stage=L_DISPOSE;lease_meta_owner=0;lease_unborrow(r);highwater();
    task_notify_generation((int)r->owner,r->owner_generation,TASK_EVENT_STREAM);lease_work_add(r);
}
static void lease_metadata_step(lease_record_t *r)
{
    if(r->cancel || r->release || !lease_owner_valid(r)){
        lease_finish(r,SC32_E_CANCELLED,FS_N_STALE,0);return;
    }
    if(r->stage==L_WAIT && (native_meta_owner || lease_meta_owner))return;
    /* volume锁内命名/权限记录不能变化；每批仍复验真实owner及源/目标
     * 当前权限，不重复枚举整棵tree及每个native description。 */
    int status=r->stage==L_WAIT?lease_operation_condition(r):lease_source_condition(r,0);
    if(!status && r->stage!=L_WAIT && r->operation!=SC32_LEASE_OP_REMOVE
       && r->operation!=SC32_LEASE_OP_SET_MODE)status=lease_destination_condition(r);
    if(status){lease_finish(r,status,status,0);return;}
    if(r->stage==L_WAIT){
        if(native_directory_sequence==0xffffffffu){lease_finish(r,SC32_E_RANGE,FS_N_RANGE,0);return;}
        if(lease_move_budget(r)){lease_finish(r,SC32_E_RANGE,FS_N_RANGE,0);return;}
        r->candidate=(entry_t *)pframe_alloc_run(dir_pages);
        if(!r->candidate){lease_finish(r,SC32_E_MEMORY,FS_N_MEMORY,0);return;}
        r->candidate_pages=dir_pages;r->candidate_freed=0;
        if(ata_read_sectors(0,1,r->super)){lease_finish(r,SC32_E_IO,FS_N_IO,0);return;}
        r->bank=active_bank^1u;r->bank_generation=metadata_generation+1u;
        if(!r->bank_generation)r->bank_generation=1;
        lease_meta_owner=r->id;r->stage=L_COPY;return;
    }
    if(lease_meta_owner!=r->id){lease_finish(r,SC32_E_FAILED,FS_N_BUSY,0);return;}
    if(r->stage==L_COPY){status=lease_copy_batch(r);if(status<0)lease_finish(r,status,status,0);return;}
    if(r->stage==L_SERIALIZE){lease_serialize_batch(r);return;}
    if(r->stage==L_BANK){
        u32 count=dir_sectors-r->bank_sector;if(count>16)count=16;
        if(ata_write_sectors(1+r->bank*dir_sectors+r->bank_sector,count,meta+r->bank_sector*512u)){
            lease_finish(r,SC32_E_IO,FS_N_IO,0);return;
        }
        r->bank_crc=crc32_update(r->bank_crc,meta+r->bank_sector*512u,count*512u);r->bank_sector+=count;
        if(r->bank_sector==dir_sectors)r->stage=L_SUPER;
        return;
    }
    if(r->stage==L_SUPER){
        /* 每个batch和这里都走原owner条件。最后PIO不能跨调度点：
         * 成功后同临界段交换完整内存目录，Cancel不插到两者之间。 */
        *(u32 *)(r->super+44)=r->bank_crc;*(u32 *)(r->super+508)=crc32_update(0,r->super,508);
        if(ata_write_sectors(0,1,r->super) || ata_flush()){
            lease_finish(r,SC32_E_IO,FS_N_IO,1);return;
        }
        lease_commit_memory(r);
    }
}
