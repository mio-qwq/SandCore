/* 目标包含点为sc32_file.c末尾；只在这里触碰真正的本人descriptor。
 * 强FS pin归lease，不增加旧16fd，不在退出时枚举所有FILE。 */
int sc32_file_lease_binding(int pid,u32 id,u32 kind,fs_lease_binding_t *out)
{
    file_handle_t *h=handle(pid,id,kind);
    if(!h)return SC32_E_STALE;
    if(h->closing || h->completed_kind)return SC32_E_BUSY;
    clear(out,sizeof(*out));out->handle=h->id;out->kind=h->kind;
    out->lease=h->lease;out->epoch=h->epoch;out->metadata_only=h->lease_metadata_only;
    for(u32 i=0;i<64;i++)out->path[i]=h->path[i];
    if(kind==FILE_HANDLE){
        file_description_t *d=description(h);
        if(!d || !d->ready)return SC32_E_BUSY;
        if(d->standard>=0 || !d->object)return SC32_E_INVALID;
        out->object=d->object;out->flags=d->flags;out->aliases=d->refs;
        fs_native_info_t info;int raw=fs_native_info(pid,d->object,0,&info);
        if(raw)return native_status(raw);
        /* Rename之后FS对象名是可信当前名字，原h.path可能是初始值。 */
        if(fs_lease_file_name(d->object,out->path))return SC32_E_STALE;
    }
    return 0;
}
int sc32_file_lease_reserve(int pid,u32 kind,const char *name,u32 flags,u32 *result)
{
    if(kind!=FILE_HANDLE && kind!=DIRECTORY_HANDLE)return SC32_E_INVALID;
    int status=initialize();if(status)return status;
    file_handle_t *h=new_handle(pid,kind);if(!h)return SC32_E_MEMORY;
    for(u32 i=0;i<64;i++)h->path[i]=name[i];
    if(kind==FILE_HANDLE){
        int index=objpool_alloc(&descriptions);
        if(index<0){drop(h);return SC32_E_MEMORY;}
        file_description_t *d=objpool_get(&descriptions,index);
        d->standard=-1;d->flags=flags;d->create_mode=3;
        status=attach_description(h,(u32)index+1);
        if(status){objpool_release(&descriptions,index);drop(h);return status;}
    }else h->epoch=fs_native_directory_epoch();
    *result=h->id;return 0;
}
int sc32_file_lease_attach(int pid,u32 id,u32 object,u32 lease,int metadata_only)
{
    file_handle_t *h=handle(pid,id,FILE_HANDLE);
    if(!h)h=handle(pid,id,DIRECTORY_HANDLE);
    if(!h)return SC32_E_STALE;
    if(h->lease)return SC32_E_BUSY;
    if(h->kind==FILE_HANDLE){
        file_description_t *d=description(h);
        if(!d || d->standard>=0 || (d->object && d->object!=object))return SC32_E_INVALID;
        d->object=object;d->ready=1;
    }else if(object)return SC32_E_INVALID;
    h->lease=lease;h->lease_metadata_only=metadata_only!=0;return 0;
}
void sc32_file_lease_abort_reservation(int pid,u32 id)
{
    file_handle_t *h=handle(pid,id,FILE_HANDLE);
    if(!h)h=handle(pid,id,DIRECTORY_HANDLE);
    if(h && !h->lease)drop(h);
}
void sc32_file_lease_unbind(int pid,u32 id,u32 lease,int close_candidate)
{
    file_handle_t *h=handle(pid,id,FILE_HANDLE);
    if(!h)h=handle(pid,id,DIRECTORY_HANDLE);
    if(!h || h->lease!=lease)return;
    h->lease=0;
    if(close_candidate)drop(h);
}
void sc32_file_lease_rebind(int pid,u32 id,const char *name)
{
    file_handle_t *h=handle(pid,id,FILE_HANDLE);
    if(!h)h=handle(pid,id,DIRECTORY_HANDLE);
    if(!h)return;
    for(u32 i=0;i<64;i++)h->path[i]=name[i];
    if(h->kind==DIRECTORY_HANDLE){h->cursor=0;h->epoch=fs_native_directory_epoch();}
}
