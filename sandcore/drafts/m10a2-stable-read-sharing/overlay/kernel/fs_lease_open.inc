static void lease_snapshot_drop(lease_record_t *r)
{
    while(r->snapshot_head){lease_snapshot_t *next=r->snapshot_head->next;
        pframe_free_run((u32)r->snapshot_head,1);r->snapshot_head=next;}
    r->snapshot_tail=0;r->members=r->snapshot_valid=0;
}
static int lease_snapshot_add(lease_record_t *r,const entry_t *entry)
{
    if(!r->snapshot_tail || r->snapshot_tail->used==40){
        lease_snapshot_t *page=(lease_snapshot_t *)pframe_alloc_run(1);
        if(!page)return SC32_E_MEMORY;
        lease_zero(page,4096);
        if(r->snapshot_tail)r->snapshot_tail->next=page;else r->snapshot_head=page;
        r->snapshot_tail=page;
    }
    r->snapshot_tail->rows[r->snapshot_tail->used++]=*entry;r->members++;return 0;
}
static int lease_snapshot_capture(lease_record_t *r)
{
    if(r->metadata_only)return SC32_E_ACCESS;
    if(!(r->flags&SC32_LEASE_FLAG_CANDIDATE) && !fs_access((int)r->owner,r->name,FS_ACCESS_READ))return SC32_E_ACCESS;
    if(r->flags&SC32_LEASE_FLAG_CANDIDATE){
        int status=lease_snapshot_add(r,&r->draft);if(status)return status;
    }else{
        if(!r->name[0]){
            entry_t root;lease_zero(&root,sizeof(root));root.uid=root_uid;root.gid=root_gid;
            root.mode=root_mode;root.flags=root.generation=1;
            int status=lease_snapshot_add(r,&root);if(status)return status;
        }
        /* IF=0且名字锁已取得：一次扫描固定需要的实际成员。没有PIO，
         * 之后分页不再依赖可因无关删除而压缩的dirents下标。 */
        for(int i=0;i<nfiles;i++)if(same(dirents[i].name,r->name) || child(dirents[i].name,r->name)){
            /* META只检查可遍历祖先，不要求条目自身正文R。不可读目录
             * 本身可见，其内部名字由祖先R检查滤掉；一个不可读文件
             * 不应使整份计划失败，更不能借旧快照泄露隐藏后代。 */
            if(!fs_access((int)r->owner,dirents[i].name,FS_ACCESS_META))continue;
            int status=lease_snapshot_add(r,dirents+i);if(status)return status;
        }
    }
    r->snapshot_valid=1;r->epoch=native_directory_sequence;return 0;
}
static void lease_new_rollback(lease_record_t *r)
{
    lease_snapshot_drop(r);lease_unlink(r,1);lease_unlink(r,0);
    lease_object_unlink(r);lease_free(r);
}
static int lease_exact_entry(const char *name,u32 expected,u32 *kind,u32 *revision)
{
    if(!name[0]){*kind=2;*revision=1;return expected==1?0:SC32_E_STALE;}
    int index=find(name);if(index<0)return SC32_E_NOT_FOUND;
    entry_t *e=dirents+index;*kind=e->start_lba?1u:2u;*revision=e->generation;
    return expected==e->generation?0:SC32_E_STALE;
}
int fs_lease_acquire(int pid,const Sc32LeaseAcquireRequest *q,ScLeaseInfo *out)
{
    if(!dir_sectors || fs_faulted)return SC32_E_IO;
    if(version!=5)return SC32_E_NOT_SUPPORTED;
    if(q->kind<1 || q->kind>3)return SC32_E_INVALID;
    for(u32 i=0;i<5;i++)if(q->reserved[i])return SC32_E_INVALID;
    int directory=q->kind==SC32_LEASE_NAMESPACE;
    if(!q->expected_revision || (directory?(q->file || !q->directory || q->expected_object || (q->flags&~1u)):
        (!q->file || q->directory || !q->expected_object || q->expected_epoch || q->flags)))return SC32_E_INVALID;
    fs_lease_binding_t b;int status=sc32_file_lease_binding(pid,directory?q->directory:q->file,directory?2u:1u,&b);
    if(status)return status;
    if(b.metadata_only)return SC32_E_ACCESS;
    if(b.lease)return SC32_E_BUSY;
    int writing=q->kind==SC32_LEASE_WRITE_EXCLUSIVE || (directory && !(q->flags&1u));
    lease_record_t *parent=0;status=lease_parent_check(pid,q->namespace_lease,q->namespace_generation,b.path,writing,&parent);
    if(status)return status;
    if(lease_meta_owner || native_meta_owner)return SC32_E_BUSY;
    u32 conflict_operation=directory?FS_LEASE_TREE:q->kind==SC32_LEASE_READ_STABLE?FS_LEASE_STABLE_OPEN:FS_LEASE_CONTENT;
    if(lease_conflict(b.path,conflict_operation,0,0,parent?parent->id:0)
       || transaction_busy(b.path))return SC32_E_BUSY;
    u32 kind,revision;status=lease_exact_entry(b.path,q->expected_revision,&kind,&revision);if(status)return status;
    if(kind!=(directory?2u:1u))return SC32_E_INVALID;
    if(writing && (!lease_mutable(pid,b.path,directory) || (!directory && !fs_access(pid,b.path,FS_ACCESS_WRITE))))return SC32_E_ACCESS;
    if(!fs_access(pid,b.path,directory?FS_ACCESS_READ:(writing?FS_ACCESS_WRITE:FS_ACCESS_READ)))return SC32_E_ACCESS;
    native_file_t *n=0;
    if(directory){
        if(q->expected_epoch!=b.epoch || q->expected_epoch!=native_directory_sequence)return SC32_E_STALE;
        if(native_busy(b.path))return SC32_E_BUSY;
    }else{
        n=native_object(b.object);
        if(!n || n->unpublished || n->invalidated || b.object!=q->expected_object)return SC32_E_STALE;
        if(n->pending || n->refs==0xffffffffu)return SC32_E_BUSY;
        if(!(b.flags&(writing?FS_NATIVE_WRITE:FS_NATIVE_READ)))return SC32_E_ACCESS;
        if(writing && (b.aliases!=1 || n->refs!=1))return SC32_E_BUSY;
    }
    lease_record_t *r=lease_new(pid,q->kind,b.path,&status);if(!r)return status;
    r->handle=b.handle;r->handle_kind=directory?2u:1u;r->revision=revision;r->read_only=directory && (q->flags&1u);
    if(directory)r->flags|=SC32_LEASE_FLAG_DIRECTORY;
    lease_parent_link(r,parent);status=lease_link(r,0,r->name);
    if(!status && directory)status=lease_snapshot_capture(r);
    if(status){lease_new_rollback(r);return status;}
    if(n)lease_object_link(r,n);
    status=sc32_file_lease_attach(pid,b.handle,n?n->id:0,r->id,0);
    if(status){lease_new_rollback(r);return status;}
    lease_information(r,out);return 0;
}
static native_file_t *lease_open_object(const char *name,int fresh,int *error)
{
    native_file_t *n=0;
    if(!fresh)for(int i=objpool_next(&native_pool,-1);i>=0;i=objpool_next(&native_pool,i)){
        native_file_t *candidate=objpool_get(&native_pool,i);
        if(!same(candidate->name,name))continue;
        if(candidate->unpublished || candidate->invalidated || candidate->pending){*error=SC32_E_BUSY;return 0;}
        n=candidate;break;
    }
    if(n){
        if(n->refs>=0xfffffffeu){*error=SC32_E_RANGE;return 0;}
        n->refs++;return n;
    }
    if(native_counter==0x7fffffffu){*error=SC32_E_RANGE;return 0;}
    u32 id=native_counter+1;
    if(objpool_claim(&native_pool,(int)id)){*error=SC32_E_MEMORY;return 0;}
    n=objpool_get(&native_pool,(int)id);n->id=++native_counter;n->refs=1;n->ready=1;copy(n->name,name);
    if(!fresh)n->index=(u32)find(name);
    return n;
}
int fs_lease_open(int pid,const Sc32LeaseOpenRequest *q,const char *name,int create,ScLeaseInfo *out)
{
    for(u32 i=0;i<5;i++)if(q->reserved[i])return SC32_E_INVALID;
    if((q->flags&~SC32_LEASE_METADATA_ONLY) || (q->object_kind!=1 && q->object_kind!=2) || !name[0])return SC32_E_INVALID;
    int metadata_only=(q->flags&SC32_LEASE_METADATA_ONLY)!=0;
    int directory=q->object_kind==2,writing=q->lease_kind==SC32_LEASE_WRITE_EXCLUSIVE || directory;
    if(directory){
        if(q->lease_kind!=SC32_LEASE_NAMESPACE || q->open_flags)return SC32_E_INVALID;
    }else if(metadata_only){
        if(q->lease_kind!=SC32_LEASE_READ_STABLE || q->open_flags)return SC32_E_INVALID;
    }else{
        if(q->lease_kind!=SC32_LEASE_READ_STABLE && q->lease_kind!=SC32_LEASE_WRITE_EXCLUSIVE)return SC32_E_INVALID;
        if(!(q->open_flags&(writing?FS_NATIVE_WRITE:FS_NATIVE_READ)) || (q->open_flags&~19u)
           || (!writing && q->open_flags!=FS_NATIVE_READ) || ((q->open_flags&16u) && !writing))return SC32_E_INVALID;
    }
    if(create){
        if(metadata_only || q->expected_revision || q->create_mode!=3 || (!directory && !writing))return SC32_E_INVALID;
    }else if(!q->expected_revision || q->create_mode)return SC32_E_INVALID;
    if(!dir_sectors || fs_faulted)return SC32_E_IO;
    if(version!=5)return SC32_E_NOT_SUPPORTED;
    if(lease_meta_owner || native_meta_owner)return SC32_E_BUSY;
    lease_record_t *parent=0;int status=lease_parent_check(pid,q->namespace_lease,q->namespace_generation,name,writing,&parent);
    if(status)return status;
    u32 conflict_operation=directory?FS_LEASE_TREE:q->lease_kind==SC32_LEASE_READ_STABLE?FS_LEASE_STABLE_OPEN:FS_LEASE_CONTENT;
    if(lease_conflict(name,conflict_operation,0,0,parent?parent->id:0)
       || transaction_busy(name))return SC32_E_BUSY;
    int index=find(name);
    if(create && index>=0)return SC32_E_EXISTS;
    if(!create && index<0)return SC32_E_NOT_FOUND;
    if(!create && (dirents[index].generation!=q->expected_revision || (dirents[index].start_lba?1u:2u)!=q->object_kind))return SC32_E_STALE;
    if(writing && !lease_mutable(pid,name,directory))return SC32_E_ACCESS;
    if(create){if(!fs_access_parent(pid,name))return SC32_E_ACCESS;}
    else if(!fs_access(pid,name,metadata_only?FS_ACCESS_META:directory?FS_ACCESS_READ:q->open_flags&3u))return SC32_E_ACCESS;
    char parent_name[64];copy(parent_name,name);int count=len(parent_name);
    while(count && parent_name[count-1]!='/')count--;
    if(count)parent_name[count-1]=0;else parent_name[0]=0;
    u32 info[2];if(fs_stat(parent_name,info) || info[0]!=2)return SC32_E_INVALID;
    if(directory && native_busy(name))return SC32_E_BUSY;
    /* WRITE需要唯一实际description，不能先增ref再用同owner偷越别名。 */
    if(writing && !directory && native_busy(name))return SC32_E_BUSY;
    lease_record_t *r=lease_new(pid,q->lease_kind,name,&status);if(!r)return status;
    r->handle_kind=q->object_kind;r->metadata_only=metadata_only;
    if(directory)r->flags|=SC32_LEASE_FLAG_DIRECTORY;
    lease_parent_link(r,parent);status=lease_link(r,0,name);
    if(status){lease_new_rollback(r);return status;}
    status=sc32_file_lease_reserve(pid,q->object_kind,name,q->open_flags,&r->handle);
    if(status){lease_new_rollback(r);return status;}
    native_file_t *n=0;
    if(!directory){
        n=lease_open_object(name,create,&status);
        if(!n){sc32_file_lease_abort_reservation(pid,r->handle);r->handle=0;lease_new_rollback(r);return status;}
    }
    if(create){
        u32 generation=new_generation();
        if(!generation){status=SC32_E_RANGE;goto failed;}
        r->flags|=SC32_LEASE_FLAG_CANDIDATE;r->revision=generation;
        lease_zero(&r->draft,sizeof(r->draft));copy(r->draft.name,name);
        r->draft.uid=module_path(name)?AUTH_SYSTEM:auth_subject_uid(pid);
        r->draft.gid=module_path(name)?AUTH_SYSTEM:auth_subject_gid(pid);r->draft.mode=3;r->draft.generation=generation;
        r->draft.flags=directory?1u:0u;r->draft.start_lba=directory?0:fs_data_start;
        if(n){n->unpublished=1;n->draft=r->draft;}
    }else r->revision=dirents[index].generation;
    if(n)lease_object_link(r,n);
    status=sc32_file_lease_attach(pid,r->handle,n?n->id:0,r->id,metadata_only);
    if(status)goto failed;
    /* 元数据pin从未遍历子树。epoch0与SNAPSHOT清不能被解释为空目录；
     * 条件remove仍扫描真实成员，rename仍核全部保护后代与外来pin。 */
    if(directory && !metadata_only){status=lease_snapshot_capture(r);if(status)goto failed_attached;}
    lease_information(r,out);return 0;
failed_attached:
    sc32_file_lease_unbind(pid,r->handle,r->id,1);r->handle=0;
    lease_new_rollback(r);return status;
failed:
    if(n)fs_native_close(n->id,0);
    sc32_file_lease_abort_reservation(pid,r->handle);r->handle=0;
    lease_new_rollback(r);return status;
}
