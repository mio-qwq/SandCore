static void sw_record(void *out,ScU32 bytes)
{
    memset(out,0,bytes);
    ((ScU32 *)out)[0]=bytes;
    ((ScU32 *)out)[1]=1;
}
static void sw_counter_add(ScWideCounter *counter,ScU32 value)
{
    ScU32 before=counter->lo;
    counter->lo+=value;
    if(counter->lo<before)counter->hi++;
}
static void sw_counter_subtract(ScWideCounter *counter,ScU32 value)
{
    if(counter->lo<value)counter->hi--;
    counter->lo-=value;
}
static int sw_same(const char *a,const char *b)
{
    while(*a && *b){
        unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='a' && x<='z')x=(unsigned char)(x-32);
        if(y>='a' && y<='z')y=(unsigned char)(y-32);
        if(x!=y)return 0;
    }
    return *a==*b;
}
static int sw_child(const char *path,const char *parent)
{
    if(!parent[0])return 0;
    ScU32 bytes=(ScU32)strlen(parent);
    if(bytes==1 && parent[0]=='/')return path[0]=='/' && path[1]!=0;
    if(strlen(path)<=bytes)return 0;
    char prefix[68];
    memcpy(prefix,path,bytes);
    prefix[bytes]=0;
    return sw_same(prefix,parent) && path[bytes]=='/';
}
static ScStatus sw_path(const char *text,char out[68])
{
    ScU32 bytes=(ScU32)strlen(text),bad=0;
    if(!bytes || bytes>64 || text[0]!='/')return SC32_E_RANGE;
    if(ScUtf8Validate(text,bytes,&bad))return SC_UTIL_E_UTF8;
    ScString *path=0;
    const char *canonical=0;
    ScStatus status=ScPathNormalize(text,"/",SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&path);
    if(status)return status;
    status=ScStringCStr(path,&canonical);
    if(!status && strcmp(canonical,text))status=SC32_E_INVALID;
    if(!status){
        for(ScU32 i=1;i<bytes;i++)if((unsigned char)text[i]<33 || text[i]==':' || text[i]=='\\')status=SC32_E_INVALID;
    }
    if(!status){memset(out,0,68);
        memcpy(out,text,bytes+1);
    }
    ScStringRelease(path);
    /* 输入必须已是caller解析的绝对规范值；这里只做词法验证，不
     * 调ScPathResolve再叠worker的CWD，更不截断成另一个磁盘身份。 */
    return status;
}
static ScStatus sw_join(const char *base,const char *tail,char out[68])
{
    ScString *path=0;
    const char *text=0;
    ScStatus status=ScPathJoin(base,tail,SC_PATH_REQUIRE_ABSOLUTE|SC_PATH_REJECT_ABOVE_ROOT,&path);
    if(status)return status;
    status=ScStringCStr(path,&text);
    if(!status)status=sw_path(text,out);
    ScStringRelease(path);
    return status;
}
static void sw_error(SwItem *item,ScStatus status)
{
    item->status=status;
    sw_record(&item->cause,sizeof(item->cause));
    if(status<=SC32_E_INVALID && status>=SC32_E_FAILED)ScErrorQuery(&item->cause);
    if(item->cause.code!=status){sw_record(&item->cause,sizeof(item->cause));
        item->cause.code=status;
        item->cause.domain=SC32_DOMAIN_LIBRARY;
        item->cause.raw=status;
    }
}
static int sw_lease_valid(const ScLeaseInfo *info)
{
    return info->size==sizeof(*info) && info->version==1 && info->lease && info->generation
        && info->owner_pid==sw.self.pid && info->owner_pid_generation==sw.self.generation;
}
static ScStatus sw_lease_accept(SwLease *lease,ScStatus status,ScLeaseInfo *info)
{
    if(sw_lease_valid(info)){
        lease->info=*info;
        lease->valid=1;
        if(info->file)lease->file=info->file;
        if(info->directory)lease->directory=info->directory;
    }
    if((status==0 || status==SC32_PENDING) && !lease->valid)return SC32_E_FAILED;
    return status;
}
static void sw_control(const SwLease *lease,ScLeaseControl *control)
{
    sw_record(control,sizeof(*control));
    control->lease=lease->info.lease;
    control->generation=lease->info.generation;
    control->operation_sequence=lease->info.operation_sequence;
}
static ScStatus sw_lease_open(SwLease *lease,const char *path,ScU32 kind,
    ScU32 lease_kind,ScU32 revision,ScU32 flags,SwLease *parent,int create)
{
    ScLeaseOpenDesc desc;
    ScLeaseInfo info;
    sw_record(&desc,sizeof(desc));
    sw_record(&info,sizeof(info));
    desc.path=path;
    desc.object_kind=kind;
    desc.lease_kind=lease_kind;
    desc.expected_revision=revision;
    desc.open_flags=flags;
    desc.create_mode=create?3:0;
    if(parent && parent->valid){desc.namespace_lease=parent->info.lease;
        desc.namespace_generation=parent->info.generation;
    }
    ScStatus status=create?ScFileLeaseCreatePinned(&desc,&info):ScFileLeaseOpenPinned(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_namespace_open(SwLease *lease,const char *path,ScU32 revision,ScU32 read_only,SwLease *parent)
{
    if(parent && parent->valid){
        if(!read_only)return sw_lease_open(lease,path,2,SC32_LEASE_NAMESPACE,revision,0,parent,0);
        if(!(parent->info.flags&SC32_LEASE_FLAG_READ_ONLY))return SC32_E_INVALID;
    }
    ScDirectory directory=0;
    ScDirectoryPage page;
    ScLeaseAcquireDesc desc;
    ScLeaseInfo info;
    ScStatus status=ScDirectoryOpen(path,&directory);
    if(status)return status;
    sw_record(&page,sizeof(page));
    status=ScDirectoryNext(directory,0,0,&page);
    if(status){ScDirectoryClose(directory);
        return status;
    }
    sw_record(&desc,sizeof(desc));
    sw_record(&info,sizeof(info));
    desc.kind=SC32_LEASE_NAMESPACE;
    desc.directory=directory;
    desc.expected_revision=revision;
    desc.expected_epoch=page.generation;
    desc.flags=read_only?SC32_LEASE_READ_ONLY_NAMESPACE:0;
    if(parent && parent->valid){desc.namespace_lease=parent->info.lease;
        desc.namespace_generation=parent->info.generation;
    }
    status=ScFileLeaseAcquire(&desc,&info);
    if(status){ScDirectoryClose(directory);
        return status;
    }
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_query(SwLease *lease)
{
    if(!lease->valid)return SC32_E_STALE;
    ScLeaseControl desc;
    ScLeaseInfo info;
    sw_control(lease,&desc);
    sw_record(&info,sizeof(info));
    ScStatus status=ScFileLeaseQuery(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_advance(SwLease *lease)
{
    if(!lease->valid)return SC32_E_STALE;
    ScLeaseControl desc;
    ScLeaseInfo info;
    sw_control(lease,&desc);
    sw_record(&info,sizeof(info));
    ScStatus status=ScFileLeaseAdvance(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_release(SwLease *lease)
{
    if(!lease->valid)return 0;
    if(!lease->closing){
        /* 先走真实Close安全点，保留attached lease的已提交判断，避免
         * 最终mode只读时Release后再做多余写flush。Close失败但已drop
         * 的普通IO结果不重放；BUSY/AGAIN才保留尚未关闭的原ID。 */
        ScStatus closed=lease->file?ScFileClose(lease->file):lease->directory?ScDirectoryClose(lease->directory):0;
        if(closed==SC32_E_BUSY || closed==SC32_E_AGAIN)return SC32_PENDING;
        lease->file=0;
        lease->directory=0;
        lease->closing=1;
    }
    ScLeaseControl desc;
    ScLeaseInfo info;
    sw_control(lease,&desc);
    sw_record(&info,sizeof(info));
    ScStatus status=ScFileLeaseRelease(&desc,&info);
    if(status==0 || status==SC32_E_STALE){
        memset(lease,0,sizeof(*lease));
        return 0;
    }
    sw_lease_accept(lease,status,&info);
    lease->closing=1;
    return status;
}
static ScStatus sw_lease_publish(SwLease *lease,SwLease *target,SwLease *parent,const char *path,ScU32 mode,int rename)
{
    ScLeasePublishDesc desc;
    ScLeaseInfo info;
    sw_record(&desc,sizeof(desc));
    sw_record(&info,sizeof(info));
    desc.source_lease=lease->info.lease;
    desc.source_generation=lease->info.generation;
    desc.expected_source_object=lease->info.object_id;
    desc.expected_source_revision=lease->info.revision;
    desc.expected_snapshot_epoch=lease->info.kind==SC32_LEASE_NAMESPACE?lease->info.snapshot_epoch:0;
    desc.target_path=path;
    desc.flags=SC32_LEASE_EXPECT_ABSENT;
    if(!rename){desc.flags|=SC32_LEASE_SET_MODE;
        desc.final_mode=mode;
    }
    if(parent && parent->valid){desc.namespace_lease=parent->info.lease;
        desc.namespace_generation=parent->info.generation;
    }
    if(target && target->valid){
        desc.flags=(desc.flags&~SC32_LEASE_EXPECT_ABSENT)|SC32_LEASE_EXPECT_EXACT;
        desc.target_lease=target->info.lease;
        desc.target_generation=target->info.generation;
        desc.expected_target_object=target->info.object_id;
        desc.expected_target_revision=target->info.revision;
    }
    ScStatus status=rename?ScFileLeaseRename(&desc,&info):ScFileLeasePublish(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_remove(SwLease *lease)
{
    ScLeaseRemoveDesc desc;
    ScLeaseInfo info;
    sw_record(&desc,sizeof(desc));
    sw_record(&info,sizeof(info));
    desc.lease=lease->info.lease;
    desc.generation=lease->info.generation;
    desc.expected_object=lease->info.object_id;
    desc.expected_revision=lease->info.revision;
    desc.expected_snapshot_epoch=lease->info.kind==SC32_LEASE_NAMESPACE?lease->info.snapshot_epoch:0;
    ScStatus status=ScFileLeaseRemove(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_cancel(SwLease *lease)
{
    ScLeaseControl desc;
    ScLeaseInfo info;
    sw_control(lease,&desc);
    sw_record(&info,sizeof(info));
    ScStatus status=ScFileLeaseCancel(&desc,&info);
    lease->terminal=1;
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_mode(SwLease *lease,ScU32 mode)
{
    ScLeaseModeDesc desc;
    ScLeaseInfo info;
    sw_record(&desc,sizeof(desc));
    sw_record(&info,sizeof(info));
    desc.lease=lease->info.lease;
    desc.generation=lease->info.generation;
    desc.expected_object=lease->info.object_id;
    desc.expected_revision=lease->info.revision;
    desc.expected_snapshot_epoch=lease->info.kind==SC32_LEASE_NAMESPACE?lease->info.snapshot_epoch:0;
    desc.final_mode=mode;
    ScStatus status=ScFileLeaseSetMode(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static ScStatus sw_lease_metadata(SwLease *lease,const char *path,ScU32 kind,ScU32 revision,SwLease *parent)
{
    ScLeaseOpenDesc desc;
    ScLeaseInfo info;
    sw_record(&desc,sizeof(desc));
    sw_record(&info,sizeof(info));
    desc.path=path;
    desc.object_kind=kind;
    desc.lease_kind=kind==2?SC32_LEASE_NAMESPACE:SC32_LEASE_READ_STABLE;
    desc.expected_revision=revision;
    desc.flags=SC32_LEASE_METADATA_ONLY;
    if(parent && parent->valid){desc.namespace_lease=parent->info.lease;
        desc.namespace_generation=parent->info.generation;
    }
    ScStatus status=ScFileLeaseOpenPinned(&desc,&info);
    return sw_lease_accept(lease,status,&info);
}
static SwLease *sw_source_parent(SwItem *item)
{return item->root->scope.valid && sw_child(item->source,item->root->source)?&item->root->scope:0;
}
static SwLease *sw_destination_parent(SwItem *item)
{return item->root->destination_scope.valid && sw_child(item->destination,item->root->destination)?&item->root->destination_scope:0;
}
static int sw_can_read(ScI32 uid,ScI32 gid,ScU32 mode)
{
    if(sw.identity.uid==0)return 1;
    ScU32 shift=sw.identity.uid==uid?0:sw.identity.gid==gid?2:4;
    return ((mode>>shift)&1u)!=0;
}
