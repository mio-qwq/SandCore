/* 动态名字树只保存有租约的路径。正文IO先走对象链；旧path操作按
 * 最多63B祖先查找，不在每个字节或每个owner退出扫描全卷。 */
static void lease_zero(void *p,u32 bytes)
{for(u32 i=0;i<bytes;i++)((u8 *)p)[i]=0;}
static lease_record_t *lease_get(u32 id)
{
    if(!id || id>0x7fffffffu || !lease_initialized)return 0;
    lease_record_t *r=objpool_get(&lease_pool,(int)id);
    return r && r->id==id?r:0;
}
static lease_record_t *lease_owned(int pid,u32 id,u32 generation)
{
    lease_record_t *r=lease_get(id);
    return task_owned(pid) && r && r->owner==(u32)pid
        && r->owner_generation==task_generation(pid) && r->generation==generation?r:0;
}
static lease_name_node_t *lease_node(u32 id)
{return id?objpool_get(&lease_names,(int)(id-1u)):0;}
static int lease_init(void)
{
    if(lease_initialized)return 0;
    if(objpool_init(&lease_pool,sizeof(lease_record_t),0)
       || objpool_init(&lease_owners,sizeof(lease_owner_t),0)
       || objpool_init(&lease_names,sizeof(lease_name_node_t),0))return SC32_E_MEMORY;
    lease_initialized=1;return 0;
}
static u32 lease_child(u32 parent,u32 character)
{
    lease_name_node_t *p=lease_node(parent);
    for(u32 id=p?p->child:0;id;id=lease_node(id)->sibling)
        if(lease_node(id)->character==character)return id;
    return 0;
}
static void lease_prune(u32 id)
{
    while(id){
        lease_name_node_t *n=lease_node(id);
        if(!n || n->head || n->child || n->subtree)return;
        u32 parent=n->parent;
        if(parent){
            lease_name_node_t *p=lease_node(parent);u32 *link=&p->child;
            while(*link && *link!=id)link=&lease_node(*link)->sibling;
            if(*link)*link=n->sibling;
        }else lease_name_root=0;
        objpool_release(&lease_names,(int)(id-1u));id=parent;
    }
}
static u32 lease_path_node(const char *name,int create)
{
    if(!lease_name_root){
        if(!create)return 0;
        int slot=objpool_alloc(&lease_names);if(slot<0)return 0;
        lease_name_root=(u32)slot+1;
    }
    u32 id=lease_name_root;
    for(u32 i=0;name[i];i++){
        u32 character=(u8)fold(name[i]),next=lease_child(id,character);
        if(!next){
            if(!create)return 0;
            int slot=objpool_alloc(&lease_names);
            if(slot<0){lease_prune(id);return 0;}
            next=(u32)slot+1;lease_name_node_t *n=lease_node(next),*p=lease_node(id);
            n->parent=id;n->character=character;n->sibling=p->child;p->child=next;
        }
        id=next;
    }
    return id;
}
static lease_record_t *lease_token(u32 token)
{return lease_get(token>>1);}
static int lease_link(lease_record_t *r,u32 slot,const char *name)
{
    u32 id=lease_path_node(name,1);if(!id)return SC32_E_MEMORY;
    for(u32 p=id;p;p=lease_node(p)->parent)
        if(lease_node(p)->subtree==0xffffffffu){lease_prune(id);return SC32_E_RANGE;}
    lease_name_node_t *n=lease_node(id);u32 token=(r->id<<1)|slot;
    r->node[slot]=id;r->name_next[slot]=n->head;
    if(n->head)lease_token(n->head)->name_previous[n->head&1u]=token;
    n->head=token;
    for(u32 p=id;p;p=lease_node(p)->parent)lease_node(p)->subtree++;
    return 0;
}
static void lease_unlink(lease_record_t *r,u32 slot)
{
    u32 id=r->node[slot];if(!id)return;
    u32 previous=r->name_previous[slot],next=r->name_next[slot];
    if(previous)lease_token(previous)->name_next[previous&1u]=next;
    else lease_node(id)->head=next;
    if(next)lease_token(next)->name_previous[next&1u]=previous;
    for(u32 p=id;p;p=lease_node(p)->parent)lease_node(p)->subtree--;
    r->node[slot]=r->name_previous[slot]=r->name_next[slot]=0;lease_prune(id);
}
static int lease_ancestor(u32 ancestor,u32 descendant)
{
    for(u32 depth=0;descendant && depth<64;depth++){
        if(ancestor==descendant)return 1;
        lease_record_t *r=lease_get(descendant);
        if(!r)return 0;
        descendant=r->parent;
    }
    return 0;
}
static int lease_allowed(lease_record_t *r,u32 a,u32 b,u32 ns)
{
    if(r->id==a || r->id==b || r->id==ns)return 1;
    /* 显式child绑定继承祖先NS；整树operation也覆盖自己已授权child。
     * 与caller PID相同本身从不构成这个许可。 */
    return (a && (lease_ancestor(r->id,a) || lease_ancestor(a,r->id)))
        || (b && lease_ancestor(r->id,b)) || (ns && lease_ancestor(r->id,ns));
}
static int lease_node_blocked(u32 node,const char *name,u32 operation,u32 a,u32 b,u32 ns,int descendants)
{
    lease_name_node_t *n=lease_node(node);
    for(u32 token=n?n->head:0;token;){
        lease_record_t *r=lease_token(token);u32 slot=token&1u;
        u32 next=r->name_next[slot];const char *held=slot?r->target:r->name;
        int covers=descendants || same(name,held)
            || ((slot?r->handle_kind==2:r->kind==SC32_LEASE_NAMESPACE) && child(name,held));
        if(covers && !lease_allowed(r,a,b,ns)){
            /* kind不会随Rename/Remove/SetMode改变；只放过真正空闲的读。
             * slot1是提交目标保留，PENDING/清理也不能被新读者插入。 */
            if(operation==FS_LEASE_STABLE_OPEN && !slot && r->kind==SC32_LEASE_READ_STABLE
               && r->handle_kind==1 && (r->state==SC32_LEASE_READY || r->state==SC32_LEASE_COMMITTED)
               && !r->cancel && !r->release && !r->queued && !r->stage
               && !(r->flags&(SC32_LEASE_FLAG_CANDIDATE|SC32_LEASE_FLAG_REVOKED|SC32_LEASE_FLAG_UNCERTAIN))){token=next;continue;}
            if(operation==FS_LEASE_READ_OPEN && !slot
               && (r->kind!=SC32_LEASE_NAMESPACE || r->read_only)){token=next;continue;}
            return 1;
        }
        token=next;
    }
    return 0;
}
static int lease_conflict(const char *name,u32 operation,u32 a,u32 b,u32 ns)
{
    if(!lease_name_root)return 0;
    u32 id=lease_name_root;
    if(lease_node_blocked(id,name,operation,a,b,ns,0))return 1;
    for(u32 i=0;name[i];i++){
        id=lease_child(id,(u8)fold(name[i]));if(!id)return 0;
        if(lease_node_blocked(id,name,operation,a,b,ns,0))return 1;
    }
    if(operation!=FS_LEASE_TREE)return 0;
    /* 深度由磁盘63B名字约束，不由任务/lease数量约束；只遍历被改
     * 子树的实际index节点，无关路径不进入循环。 */
    u32 branch=name[0]?lease_child(id,'/'):id;
    u32 stack[64],depth=0,current=branch?lease_node(branch)->child:0;
    while(current || depth){
        if(!current){current=stack[--depth];continue;}
        lease_name_node_t *n=lease_node(current);
        if(lease_node_blocked(current,name,operation,a,b,ns,1))return 1;
        if(n->child){if(n->sibling)stack[depth++]=n->sibling;current=n->child;}
        else current=n->sibling;
    }
    return 0;
}
int fs_lease_path_blocked(const char *name,u32 operation)
{return lease_initialized && lease_conflict(name,operation,0,0,0);}
int fs_lease_volume_locked(void){return lease_meta_owner!=0;}
int fs_lease_available(void){return version==5 && dir_sectors && !fs_faulted;}
static void lease_work_remove(lease_record_t *r)
{
    if(!r->queued)return;
    if(r->work_previous)lease_get(r->work_previous)->work_next=r->work_next;
    else lease_work_head=r->work_next;
    if(r->work_next)lease_get(r->work_next)->work_previous=r->work_previous;
    else lease_work_tail=r->work_previous;
    r->work_previous=r->work_next=r->queued=0;
}
static void lease_work_add(lease_record_t *r)
{
    if(r->queued)return;
    r->work_previous=lease_work_tail;r->work_next=0;r->queued=1;
    if(lease_work_tail)lease_get(lease_work_tail)->work_next=r->id;else lease_work_head=r->id;
    lease_work_tail=r->id;task_kernel_wake();
}
static void lease_parent_link(lease_record_t *r,lease_record_t *parent)
{
    if(!parent)return;
    r->parent=parent->id;r->child_next=parent->child_head;parent->child_head=r->id;
    if(r->child_next)lease_get(r->child_next)->child_previous=r->id;
}
static void lease_parent_unlink(lease_record_t *r)
{
    lease_record_t *parent=lease_get(r->parent);
    if(r->child_previous)lease_get(r->child_previous)->child_next=r->child_next;
    else if(parent)parent->child_head=r->child_next;
    if(r->child_next)lease_get(r->child_next)->child_previous=r->child_previous;
    r->parent=r->child_previous=r->child_next=0;
}
static lease_record_t *lease_new(int pid,u32 kind,const char *name,int *error)
{
    *error=lease_init();if(*error)return 0;
    if(!task_owned(pid) || pid<=0 || TASK(pid).state==2){*error=SC32_E_STALE;return 0;}
    if(lease_counter==0x7fffffffu){*error=SC32_E_RANGE;return 0;}
    lease_owner_t *owner=objpool_get(&lease_owners,pid);
    if(owner && owner->generation!=task_generation(pid)){*error=SC32_E_STALE;return 0;}
    if(!owner){
        if(objpool_claim(&lease_owners,pid)){*error=SC32_E_MEMORY;return 0;}
        owner=objpool_get(&lease_owners,pid);owner->generation=task_generation(pid);
    }
    u32 id=lease_counter+1;
    if(objpool_claim(&lease_pool,(int)id)){
        if(!owner->head)objpool_release(&lease_owners,pid);
        *error=SC32_E_MEMORY;return 0;
    }
    /* claim初始全零；先经稳定pool地址填写，才允许lease_get取得。 */
    lease_record_t *r=objpool_get(&lease_pool,(int)id);r->id=r->generation=++lease_counter;
    r->owner=(u32)pid;r->owner_generation=task_generation(pid);r->kind=kind;r->state=SC32_LEASE_READY;
    copy(r->name,name);r->owner_next=owner->head;
    if(r->owner_next)lease_get(r->owner_next)->owner_previous=r->id;
    owner->head=r->id;return r;
}
static void lease_free(lease_record_t *r)
{
    lease_work_remove(r);lease_parent_unlink(r);
    lease_owner_t *owner=objpool_get(&lease_owners,(int)r->owner);
    if(r->owner_previous)lease_get(r->owner_previous)->owner_next=r->owner_next;
    else if(owner)owner->head=r->owner_next;
    if(r->owner_next)lease_get(r->owner_next)->owner_previous=r->owner_previous;
    u32 pid=r->owner;objpool_release(&lease_pool,(int)r->id);
    if(owner && !owner->head)objpool_release(&lease_owners,(int)pid);
}
static int lease_mutable(int pid,const char *name,int tree)
{
    if(version!=5 || !name[0] || protected(name)
       || (module_path(name) && !auth_can_mod(pid)))return 0;
    if(tree)for(int i=0;i<nfiles;i++)if(child(dirents[i].name,name)
        && (protected(dirents[i].name) || (module_path(dirents[i].name) && !auth_can_mod(pid))))return 0;
    return 1;
}
static int lease_parent_read(int pid,const char *name)
{
    char parent[64];copy(parent,name);int count=len(parent);
    while(count && parent[count-1]!='/')count--;
    if(count)parent[count-1]=0;else parent[0]=0;
    return fs_access(pid,parent,FS_ACCESS_READ);
}
static int lease_parent_check(int pid,u32 id,u32 generation,const char *name,int writing,lease_record_t **out)
{
    *out=0;
    if(!id)return generation?SC32_E_INVALID:0;
    lease_record_t *r=lease_owned(pid,id,generation);
    if(!r)return SC32_E_STALE;
    if(r->kind!=SC32_LEASE_NAMESPACE || (r->state!=SC32_LEASE_READY && r->state!=SC32_LEASE_COMMITTED) || r->release || r->cancel
       || !child(name,r->name))return SC32_E_INVALID;
    if(r->flags&SC32_LEASE_FLAG_CANDIDATE)return SC32_E_INVALID;
    if(r->metadata_only || (writing && r->read_only))return SC32_E_ACCESS;
    /* namespace是范围排他锁。根R用于遍历；每次真正操作另核fileW
     * 或直接parentR/W，不用根W误禁移动只读目录。 */
    if(!fs_access(pid,r->name,FS_ACCESS_READ))return SC32_E_ACCESS;
    *out=r;return 0;
}
static void lease_object_link(lease_record_t *r,native_file_t *n)
{
    r->object=n->id;n->refs++;r->object_next=n->lease_head;n->lease_head=r->id;
    if(r->object_next)lease_get(r->object_next)->object_previous=r->id;
}
static void lease_object_unlink(lease_record_t *r)
{
    native_file_t *n=native_object(r->object);if(!n){r->object=0;return;}
    if(r->object_previous)lease_get(r->object_previous)->object_next=r->object_next;
    else n->lease_head=r->object_next;
    if(r->object_next)lease_get(r->object_next)->object_previous=r->object_previous;
    u32 object=r->object;r->object=r->object_previous=r->object_next=0;
    fs_native_close(object,0);
}
static void lease_information(lease_record_t *r,ScLeaseInfo *out)
{
    lease_zero(out,sizeof(*out));out->size=sizeof(*out);out->version=1;
    out->lease=r->id;out->generation=r->generation;out->kind=r->kind;out->state=r->state;
    if(r->handle_kind==1)out->file=r->handle;else out->directory=r->handle;
    out->object_id=r->object;out->revision=r->revision;out->snapshot_epoch=r->epoch;
    out->flags=r->flags|(r->read_only?SC32_LEASE_FLAG_READ_ONLY:0)
        |(r->metadata_only?SC32_LEASE_FLAG_METADATA_ONLY:0)
        |(r->snapshot_valid?SC32_LEASE_FLAG_SNAPSHOT:0)
        |(r->cancel?SC32_LEASE_FLAG_CANCEL_REQUESTED:0);
    out->operation=r->operation;out->operation_sequence=r->sequence;
    out->result=r->result;out->raw=r->raw;out->owner_pid=r->owner;out->owner_pid_generation=r->owner_generation;
    out->member_count=r->members;
    if(r->stage==L_COPY){out->progress=r->copy_cursor;out->total=(u32)nfiles;}
    else if(r->stage==L_SERIALIZE){out->progress=r->serialize_offset;out->total=dir_sectors*512u;}
    else if(r->stage>=L_BANK && r->stage<=L_SUPER){out->progress=r->bank_sector;out->total=dir_sectors;}
    else if(r->cleanup_count || r->cleanup_pending_count || r->garbage_count){
        out->progress=0;out->total=r->cleanup_count+r->cleanup_pending_count+r->garbage_count;
    }
    native_file_t *n=native_object(r->object);
    if(n && !n->invalidated){
        entry_t *entry=native_current(n);
        if(entry)out->revision=entry->generation;else out->flags|=SC32_LEASE_FLAG_REVOKED;
        if(n->uncertain)out->flags|=SC32_LEASE_FLAG_UNCERTAIN;
    }else if(r->handle_kind==2 && r->handle && r->node[0]
             && !(r->flags&(SC32_LEASE_FLAG_CANDIDATE|SC32_LEASE_FLAG_REVOKED))){
        int index=find(r->name);if(index>=0)out->revision=dirents[index].generation;
    }
    out->path[0]='/';for(u32 i=0;r->name[i];i++)out->path[i+1]=r->name[i];
}
