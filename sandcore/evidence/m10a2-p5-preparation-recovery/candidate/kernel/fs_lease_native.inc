static entry_t *native_current(native_file_t *n)
{
    if(!n || n->invalidated)return 0;
    if(n->unpublished)return &n->draft;
    /* 其它名字remove/replace会压缩整卷数组。缓存只在当前name仍
     * 对应这个slot时有效；漂移只修这个对象，不扫描全部native池。 */
    if(n->index>=(u32)nfiles || !dirents[n->index].start_lba || !same(n->name,dirents[n->index].name)){
        if(native_index(n)<0)return 0;
    }
    return dirents+n->index;
}
int fs_lease_file_name(u32 object,char out[64])
{
    native_file_t *n=native_object(object);if(!n || n->invalidated)return -1;
    for(u32 i=0;i<64;i++)out[i]=n->name[i];
    return 0;
}
int fs_lease_file_private(u32 object)
{native_file_t *n=native_object(object);return n && n->unpublished;}
int fs_lease_close_committed(u32 id,u32 object)
{
    lease_record_t *r=lease_get(id);native_file_t *n=native_object(object);
    entry_t *entry=native_current(n);
    return r && n && r->state==SC32_LEASE_COMMITTED && !n->pending && !n->uncertain
        && entry && (r->flags&SC32_LEASE_FLAG_DURABLE) && entry->generation==r->revision;
}
static int lease_live(lease_record_t *r)
{return r && !r->release && !r->cancel && !(r->flags&SC32_LEASE_FLAG_REVOKED)
    && (r->state==SC32_LEASE_READY || r->state==SC32_LEASE_COMMITTED);}
static int lease_owner_valid(lease_record_t *r)
{return task_owned((int)r->owner) && task_generation((int)r->owner)==r->owner_generation
    && TASK(r->owner).state!=2;}
static int lease_parent_access(lease_record_t *r,int pid,int writing)
{
    for(u32 id=r->parent;id;){
        lease_record_t *parent=lease_get(id);
        if(!parent || !lease_owner_valid(parent) || parent->owner!=(u32)pid
           || (!lease_live(parent) && !(writing<0 && parent->state==SC32_LEASE_PENDING_STATE))
           || parent->metadata_only || (writing>0 && parent->read_only))return 0;
        if(!fs_access(pid,parent->name,FS_ACCESS_READ))return 0;
        id=parent->parent;
    }
    return 1;
}
static int lease_private_access(native_file_t *n,int pid,u32 access)
{
    lease_record_t *r=lease_get(n->lease_head);
    if(!r || r->owner!=(u32)pid || !lease_owner_valid(r)
       || (!lease_live(r) && !(access==0 && r->state==SC32_LEASE_PENDING_STATE)))return FS_N_STALE;
    if(!lease_parent_access(r,pid,access?(access&FS_NATIVE_WRITE)!=0:-1))return FS_N_ACCESS;
    char parent[64];copy(parent,n->name);int count=len(parent);
    while(count && parent[count-1]!='/')count--;
    if(count)parent[count-1]=0;else parent[0]=0;
    if(!fs_access(pid,parent,FS_ACCESS_READ))return FS_N_ACCESS;
    if(access&FS_NATIVE_WRITE){
        if(fs_faulted)return FS_N_IO;
        if(!lease_mutable(pid,n->name,0))return FS_N_ACCESS;
    }
    if(access && !permission(auth_subject_uid(pid),auth_subject_gid(pid),n->draft.uid,n->draft.gid,n->draft.mode,access&3u))return FS_N_ACCESS;
    return 0;
}
static int lease_native_guard(native_file_t *n,int pid,u32 writer,u32 access)
{
    if(!n || n->invalidated)return FS_N_STALE;
    if(!(access&FS_NATIVE_WRITE))return 0;
    u32 permit=0;
    for(u32 id=n->lease_head;id;){
        lease_record_t *r=lease_get(id);if(!r)return FS_N_STALE;
        if(r->kind!=SC32_LEASE_WRITE_EXCLUSIVE || r->handle!=writer || r->owner!=(u32)pid
           || !lease_owner_valid(r) || !lease_live(r))return FS_N_BUSY;
        if(!lease_parent_access(r,pid,1))return FS_N_ACCESS;
        if(r->garbage_count)return FS_N_AGAIN;
        permit=r->id;id=r->object_next;
    }
    if(lease_conflict(n->name,FS_LEASE_CONTENT,permit,0,0))return FS_N_BUSY;
    return 0;
}
static int lease_native_open_blocked(const char *name,u32 flags)
{return fs_lease_path_blocked(name,(flags&FS_NATIVE_WRITE)?FS_LEASE_CONTENT:FS_LEASE_READ_OPEN);}
static void lease_private_discard(native_file_t *n)
{
    lease_record_t *r=lease_get(n->lease_head);
    if(n->reserved_count && r){
        /* 新IO在garbage未清完时被guard挡住，所以一个真实pending
         * extent足够，不需要取消时再分配可能失败的GC节点。 */
        r->garbage_start=n->reserved_start;r->garbage_count=n->reserved_count;lease_work_add(r);
    }
    n->reserved_start=n->reserved_count=0;native_pending_clear(n);
}
static int lease_private_commit(native_file_t *n)
{
    lease_record_t *r=lease_get(n->lease_head);
    if(!r || !lease_live(r) || !lease_owner_valid(r))return FS_N_STALE;
    int error=lease_private_access(n,(int)n->owner,FS_NATIVE_WRITE);
    if(error)return error;
    if(ata_flush())return FS_N_IO;
    u32 old=(n->previous.size+511u)/512u,next=(n->candidate.size+511u)/512u;
    if(n->previous.start_lba!=n->candidate.start_lba){r->garbage_start=n->previous.start_lba;r->garbage_count=old;}
    else if(old>next){r->garbage_start=n->previous.start_lba+next;r->garbage_count=old-next;}
    n->draft=n->candidate;n->flushed_revision=0;n->reserved_start=n->reserved_count=0;
    n->complete=1;n->result=0;r->revision=n->draft.generation;
    if(r->garbage_count)lease_work_add(r);
    native_completed(n);return 0;
}
