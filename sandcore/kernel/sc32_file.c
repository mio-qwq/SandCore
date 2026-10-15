#include "sc32_file.h"
#include "sc32_core.h"
#include "fs.h"
#include "fs_lease.h"
#include "streams.h"
#include "task.h"
#include "paging.h"
#include "userspace.h"
#include "objpool.h"
#include "../user/sc32/SC32FILEWIRE.H"
#include "../user/pathapi/SC32PATHWIRE.H"

enum { FILE_HANDLE=1,DIRECTORY_HANDLE=2 };
typedef struct {
    u32 id,owner,generation,previous,next,kind,description,member_previous,member_next;
    u32 native_reference,closing,cursor,epoch,lease,lease_metadata_only;
    u32 completed_kind,completed_position,completed_length;
    Sc32FileReply completed_reply;
    char path[64];
} file_handle_t;
typedef struct {u32 refs,head,object,position,flags,create_mode,ready;int standard;} file_description_t;
typedef struct {u32 generation,head,pending_open,standard[3];} file_owner_t;
static objpool_t handles,descriptions,owners;
static u32 handle_counter;
static int initialized;
typedef char file_reply_is_48[(sizeof(Sc32FileReply)==48)?1:-1];
typedef char file_request_is_32[(sizeof(Sc32FileOpenRequest)==32)?1:-1];
typedef char directory_reply_is_32[(sizeof(Sc32DirectoryReply)==32)?1:-1];
typedef char directory_row_is_96[(sizeof(ScDirectoryEntry)==96)?1:-1];

static void clear(void *p,u32 bytes){for(u32 i=0;i<bytes;i++)((u8 *)p)[i]=0;}
static int same(const char *a,const char *b)
{
    while(*a && *b){int x=(u8)*a++,y=(u8)*b++;if(x>='a' && x<='z')x-=32;if(y>='a' && y<='z')y-=32;
        if(x!=y)return 0;}return *a==*b;
}
static int initialize(void)
{
    if(initialized)return 0;
    if(objpool_init(&handles,sizeof(file_handle_t),0) || objpool_init(&descriptions,sizeof(file_description_t),0)
       || objpool_init(&owners,sizeof(file_owner_t),0))return SC32_E_MEMORY;
    initialized=1;return 0;
}
static file_handle_t *handle(int pid,u32 id,u32 kind)
{
    if(!id || id>0x7fffffffu)return 0;
    file_handle_t *h=objpool_get(&handles,(int)id);
    return h && h->id==id && h->owner==(u32)pid && h->generation==task_generation(pid) && h->kind==kind?h:0;
}
static file_description_t *description(file_handle_t *h)
{return h && h->description?objpool_get(&descriptions,(int)(h->description-1u)):0;}
static void drop(file_handle_t *h)
{
    file_owner_t *o=objpool_get(&owners,(int)h->owner);file_description_t *d=description(h);
    if(h->previous)((file_handle_t *)objpool_get(&handles,(int)h->previous))->next=h->next;
    else if(o)o->head=h->next;
    if(h->next)((file_handle_t *)objpool_get(&handles,(int)h->next))->previous=h->previous;
    if(o && o->pending_open==h->id)o->pending_open=0;
    if(d){
        if(h->member_previous)((file_handle_t *)objpool_get(&handles,(int)h->member_previous))->member_next=h->member_next;
        else d->head=h->member_next;
        if(h->member_next)((file_handle_t *)objpool_get(&handles,(int)h->member_next))->member_previous=h->member_previous;
        if(h->native_reference)streams_native_close((int)h->owner,h->native_reference);
        if(d->object)fs_native_cancel(d->object,h->id);
        if(!--d->refs){
            if(d->object)fs_native_close(d->object,h->id);
            if(o && d->standard>=0 && o->standard[d->standard]==h->description)o->standard[d->standard]=0;
            objpool_release(&descriptions,(int)(h->description-1u));
        }
    }
    u32 owner=h->owner;objpool_release(&handles,(int)h->id);
    if(o && !o->head && !o->pending_open)objpool_release(&owners,(int)owner);
}
int sc32_file_stop_step(int pid)
{
    if(!initialized)return 1;
    file_owner_t *o=objpool_get(&owners,pid);if(!o)return 1;
    for(u32 budget=32;budget && o->head;budget--){
        file_handle_t *h=objpool_get(&handles,(int)o->head);if(!h){o->head=0;break;}
        /* 退出不写用户缓冲、不运行用户fini、不做无界磁盘flush。已
         * 接受正文保留，未发布growth候选撤销；明示durability在Flush。 */
        if(h->lease && !fs_lease_detach_step(pid,h->id,h->lease))return 0;
        drop(h);o=objpool_get(&owners,pid);if(!o)return 1;
    }
    if(!o->head){objpool_release(&owners,pid);return 1;}return 0;
}
void sc32_file_stop(int pid){while(!sc32_file_stop_step(pid)){};}
static file_owner_t *owner(int pid,int create)
{
    file_owner_t *o=objpool_get(&owners,pid);
    if(o && o->generation!=task_generation(pid)){sc32_file_stop(pid);o=0;}
    if(!o && create){if(objpool_claim(&owners,pid))return 0;o=objpool_get(&owners,pid);o->generation=task_generation(pid);}
    return o;
}
static file_handle_t *new_handle(int pid,u32 kind)
{
    if(handle_counter==0x7fffffffu)return 0;
    file_owner_t *o=owner(pid,1);if(!o)return 0;
    u32 id=handle_counter+1;if(objpool_claim(&handles,(int)id)){
        if(!o->head)objpool_release(&owners,pid);
        return 0;}
    file_handle_t *h=objpool_get(&handles,(int)id);h->id=++handle_counter;h->kind=kind;
    h->owner=(u32)pid;h->generation=task_generation(pid);h->next=o->head;
    if(h->next)((file_handle_t *)objpool_get(&handles,(int)h->next))->previous=h->id;
    o->head=h->id;return h;
}
static int attach_description(file_handle_t *h,u32 id)
{
    file_description_t *d=objpool_get(&descriptions,(int)(id-1u));if(!d || d->refs==0xffffffffu)return SC32_E_RANGE;
    h->description=id;h->member_next=d->head;d->head=h->id;d->refs++;
    if(h->member_next)((file_handle_t *)objpool_get(&handles,(int)h->member_next))->member_previous=h->id;
    return 0;
}
static int native_status(int raw)
{
    switch(raw){case 0:return 0;case FS_N_INVALID:return SC32_E_INVALID;case FS_N_NOT_FOUND:return SC32_E_NOT_FOUND;
    case FS_N_ACCESS:return SC32_E_ACCESS;case FS_N_MEMORY:return SC32_E_MEMORY;case FS_N_NO_SPACE:return SC32_E_NO_SPACE;
    case FS_N_STALE:return SC32_E_STALE;case FS_N_BUSY:return SC32_E_BUSY;case FS_N_RANGE:return SC32_E_RANGE;
    case FS_N_IO:return SC32_E_IO;case FS_N_AGAIN:return SC32_E_AGAIN;case FS_N_NOT_SUPPORTED:return SC32_E_NOT_SUPPORTED;}
    return SC32_E_FAILED;
}
static int stream_status(int raw)
{
    if(raw>=0)return 0;
    switch(raw){case -4:return SC32_E_MEMORY;case -5:return SC32_E_ACCESS;case -6:return SC32_E_AGAIN;
    case -7:return SC32_E_BROKEN_PIPE;case -8:return SC32_E_STALE;case -12:return SC32_E_NOT_SUPPORTED;
    case -2:return SC32_E_INVALID;case -1:return SC32_E_IO;}return SC32_E_FAILED;
}
static int publish(int pid,u32 address,const void *data,u32 bytes)
{return paging_user_copy(TASK(pid).pd,address,(void *)data,bytes,1)?SC32_E_INVALID:0;}
static int path(int pid,u32 address,char out[64])
{
    char input[128];if(!address)return SC32_E_INVALID;
    u32 i=0;for(;i<sizeof(input);i++){
        if(address>0xffffffffu-i || !paging_user_range(TASK(pid).pd,address+i,1)
           || paging_user_copy(TASK(pid).pd,address+i,input+i,1,0))return SC32_E_INVALID;
        if(!input[i])break;
    }
    if(i==sizeof(input) || !paging_user_range(TASK(pid).pd,address,i+1))return SC32_E_INVALID;
    /* userspace_resolve的rootless结果直接进fs根接口，不再次结合cwd。 */
    clear(out,64);return userspace_resolve(pid,input,out,64)<0?SC32_E_RANGE:0;
}
static int overlap(u32 a,u32 bytes,u32 b,u32 other)
{return bytes && other && a<=0xffffffffu-bytes && b<=0xffffffffu-other && a<b+other && b<a+bytes;}
static int reserved(const u32 *words,u32 first)
{for(u32 i=first;i<8;i++)if(words[i])return 0;return 1;}
#include "sc32_path_query.inc"

static void file_reply(int pid,file_handle_t *h,file_description_t *d,Sc32FileReply *r,int raw,u32 transferred)
{
    clear(r,sizeof(*r));r->size=sizeof(*r);r->version=1;r->handle=h?h->id:0;r->raw=raw;r->transferred=transferred;
    if(!d)return;
    r->position=d->position;r->flags=d->flags;r->object=d->object;
    if(d->standard>=0){
        r->flags|=SC32_FILE_STANDARD;u32 info[8];
        if(!streams_native_query(pid,h->native_reference,info)){
            r->position=info[2];r->length=info[3];r->revision=info[4];r->endpoint_kind=info[1];r->endpoint_capabilities=info[6];
        }
    }else if(d->object){
        fs_native_info_t info;if(!fs_native_info(pid,d->object,0,&info)){
            r->length=info.length;r->object=info.object;r->revision=info.revision;r->flags|=info.flags<<16;
        }else if(!d->ready)r->flags|=SC32_FILE_GROWING;
    }
}
int sc32_file_complete(int pid,u32 generation,u32 writer,u32 kind,u32 position,u32 length,u32 object,int raw,u32 transferred)
{
    if(!initialized || !task_owned(pid) || task_generation(pid)!=generation)return 0;
    file_handle_t *h=handle(pid,writer,FILE_HANDLE);file_description_t *d=description(h);
    if(!d || d->object!=object || h->completed_kind)return 0;
    if(kind==FS_NATIVE_WRITE_OPERATION && !raw)d->position=position+transferred;
    h->completed_kind=kind;h->completed_position=position;h->completed_length=length;
    file_reply(pid,h,d,&h->completed_reply,raw,transferred);return 1;
}
static int completed(file_handle_t *h,u32 kind,u32 value,Sc32FileReply *reply)
{
    if(!h->completed_kind)return 0;
    if(h->completed_kind!=kind || (kind!=FS_NATIVE_FLUSH_OPERATION && h->completed_length!=value))return -1;
    *reply=h->completed_reply;h->completed_kind=0;return 1;
}
static int open_file(int pid,const u32 *q,u32 output)
{
    if(!reserved(q,5) || !(q[3]&3u) || (q[3]&~31u) || q[4]>63u
       || ((q[3]&28u) && !(q[3]&FS_NATIVE_WRITE)))return SC32_E_INVALID;
    char name[64];int status=path(pid,q[2],name);if(status)return status;
    file_owner_t *o=owner(pid,0);file_handle_t *h=0;file_description_t *d=0;
    if(o && o->pending_open){
        h=handle(pid,o->pending_open,FILE_HANDLE);d=description(h);
        if(!h || !d || !same(name,h->path) || d->flags!=q[3] || d->create_mode!=q[4])return SC32_E_BUSY;
    }else{
        h=new_handle(pid,FILE_HANDLE);if(!h)return SC32_E_MEMORY;
        int index=objpool_alloc(&descriptions);if(index<0){drop(h);return SC32_E_MEMORY;}
        d=objpool_get(&descriptions,index);d->standard=-1;d->flags=q[3];d->create_mode=q[4];
        attach_description(h,(u32)index+1);for(u32 i=0;i<64;i++)h->path[i]=name[i];
        o=owner(pid,0);o->pending_open=h->id;
    }
    Sc32FileReply r;int cached=completed(h,FS_NATIVE_OPEN_OPERATION,q[4],&r);if(cached<0)return SC32_E_BUSY;
    int raw=cached?r.raw:fs_native_open(pid,h->id,name,d->flags,d->create_mode,&d->object);
    if(!cached)cached=completed(h,FS_NATIVE_OPEN_OPERATION,q[4],&r);
    if(cached>0)raw=r.raw;else file_reply(pid,h,d,&r,raw,0);status=native_status(raw);
    if(!status){d->ready=1;o->pending_open=0;}
    else if(raw!=FS_N_AGAIN){drop(h);r.handle=0;}
    if(!status || raw==FS_N_AGAIN || raw==FS_N_IO){int copied=publish(pid,output,&r,sizeof(r));if(copied)return copied;}
    return status;
}
static int duplicate_file(int pid,file_handle_t *original,u32 output)
{
    if(original->lease)return SC32_E_BUSY;
    if(original->lease_metadata_only)return SC32_E_ACCESS;
    file_description_t *d=description(original);if(!d || !d->ready || original->closing)return SC32_E_BUSY;
    if(d->refs==0xffffffffu)return SC32_E_RANGE;
    file_handle_t *h=new_handle(pid,FILE_HANDLE);if(!h)return SC32_E_MEMORY;
    int raw=0;
    if(d->standard>=0){raw=streams_native_duplicate(pid,original->native_reference,&h->native_reference);
        if(raw){drop(h);return stream_status(raw);}}
    attach_description(h,original->description);Sc32FileReply r;file_reply(pid,h,d,&r,0,0);return publish(pid,output,&r,sizeof(r));
}
static int standard_file(int pid,u32 index,u32 output)
{
    if(index>2)return SC32_E_INVALID;
    file_owner_t *o=owner(pid,0);file_description_t *d=o && o->standard[index]?objpool_get(&descriptions,(int)(o->standard[index]-1u)):0;
    if(d && d->head){file_handle_t *existing=handle(pid,d->head,FILE_HANDLE);if(existing)return duplicate_file(pid,existing,output);}
    file_handle_t *h=new_handle(pid,FILE_HANDLE);if(!h)return SC32_E_MEMORY;
    int slot=objpool_alloc(&descriptions);if(slot<0){drop(h);return SC32_E_MEMORY;}
    d=objpool_get(&descriptions,slot);d->standard=(int)index;d->ready=1;
    attach_description(h,(u32)slot+1);
    int raw=streams_native_take(pid,(int)index,&h->native_reference);
    if(raw){drop(h);return raw==-1?SC32_E_NOT_FOUND:stream_status(raw);}
    u32 info[8];clear(info,sizeof(info));streams_native_query(pid,h->native_reference,info);d->flags=info[5]&3u;
    o=owner(pid,0);o->standard[index]=(u32)slot+1;
    Sc32FileReply r;file_reply(pid,h,d,&r,0,0);return publish(pid,output,&r,sizeof(r));
}
static int directory(int pid,u32 operation,const u32 *q,u32 output)
{
    Sc32DirectoryReply r;clear(&r,sizeof(r));r.size=sizeof(r);r.version=1;
    if(operation==SC32_D_OPEN){
        if(q[3] || q[4] || !reserved(q,5))return SC32_E_INVALID;
        char name[64];int status=path(pid,q[2],name);if(status)return status;
        if(fs_lease_path_blocked(name,FS_LEASE_READ_OPEN))return SC32_E_BUSY;
        if(!fs_access(pid,name,FS_ACCESS_READ))return SC32_E_ACCESS;
        u32 info[2];if(fs_stat(name,info))return SC32_E_NOT_FOUND;if(info[0]!=2)return SC32_E_INVALID;
        file_handle_t *h=new_handle(pid,DIRECTORY_HANDLE);if(!h)return SC32_E_MEMORY;
        for(u32 i=0;i<64;i++)h->path[i]=name[i];
        h->epoch=fs_native_directory_epoch();
        r.handle=h->id;r.epoch=h->epoch;return publish(pid,output,&r,sizeof(r));
    }
    file_handle_t *h=handle(pid,q[2],DIRECTORY_HANDLE);if(!h)return SC32_E_STALE;
    if(operation==SC32_D_CLOSE){
        if(q[3] || !reserved(q,4))return SC32_E_INVALID;
        if(h->lease && !fs_lease_detach_step(pid,h->id,h->lease))return SC32_E_BUSY;
        drop(h);return publish(pid,output,&r,sizeof(r));
    }
    if(h->lease_metadata_only)return SC32_E_ACCESS;
    if(!reserved(q,5) || q[4]>SC32_DIRECTORY_BATCH_MAX)return SC32_E_RANGE;
    u32 bytes=q[4]*(u32)sizeof(ScDirectoryEntry);
    if(bytes && (!paging_user_write_range(TASK(pid).pd,q[3],bytes) || overlap(q[3],bytes,output,sizeof(r))))return SC32_E_INVALID;
    if(h->epoch!=fs_native_directory_epoch())return SC32_E_STALE;
    if(!fs_access(pid,h->path,FS_ACCESS_READ))return SC32_E_ACCESS;
    u32 cursor=h->cursor;r.handle=h->id;r.epoch=h->epoch;
    while(r.count<q[4] && cursor!=0xffffffffu){
        fs_native_row_t row;u32 next;int raw=fs_native_directory_read(pid,h->path,h->epoch,cursor,&row,&next);
        if(raw<0)return native_status(raw);
        cursor=next;if(raw==1)break;
        ScDirectoryEntry item;clear(&item,sizeof(item));item.size=sizeof(item);item.version=1;
        item.kind=row.kind;item.length=row.length;item.generation=row.generation;item.uid=row.uid;item.gid=row.gid;item.rw=row.mode;
        for(u32 i=0;i<64;i++)item.name[i]=row.name[i];
        int copied=publish(pid,q[3]+r.count*(u32)sizeof(item),&item,sizeof(item));if(copied)return copied;r.count++;
    }
    h->cursor=cursor;r.cursor=cursor;r.flags=cursor==0xffffffffu?SC32_DIRECTORY_END:0;
    return publish(pid,output,&r,sizeof(r));
}
int sc32_file_call(int pid,u32 operation,u32 request,u32 output)
{
    if(operation==SC32_P_QUERY)return path_query(pid,request,output);
    if(operation!=SC32_F_OPEN && operation!=SC32_F_READ && operation!=SC32_F_WRITE && operation!=SC32_F_SEEK
       && operation!=SC32_F_TELL && operation!=SC32_F_FLUSH && operation!=SC32_F_CLOSE && operation!=SC32_F_QUERY
       && operation!=SC32_F_TRUNCATE && operation!=SC32_F_DUPLICATE && operation!=SC32_F_STANDARD
       && operation!=SC32_D_OPEN && operation!=SC32_D_NEXT && operation!=SC32_D_CLOSE)return SC32_E_NOT_SUPPORTED;
    u32 q[8];int status=sc32_core_input(pid,request,q,sizeof(q));if(status)return status;
    u32 out_bytes=operation>=SC32_D_OPEN?sizeof(Sc32DirectoryReply):sizeof(Sc32FileReply);
    status=sc32_core_output(pid,output,out_bytes);if(status)return status;
    status=initialize();if(status)return status;
    if(operation>=SC32_D_OPEN)return directory(pid,operation,q,output);
    if(operation==SC32_F_OPEN)return open_file(pid,q,output);
    if(operation==SC32_F_STANDARD){if(q[2] || !reserved(q,4))return SC32_E_INVALID;return standard_file(pid,q[3],output);}
    file_handle_t *h=handle(pid,q[2],FILE_HANDLE);if(!h)return SC32_E_STALE;
    file_description_t *d=description(h);if(!d || !d->ready)return SC32_E_BUSY;
    if(h->lease_metadata_only && operation!=SC32_F_QUERY && operation!=SC32_F_CLOSE)return SC32_E_ACCESS;
    Sc32FileReply r;int raw=0;u32 transferred=0;
    if(operation==SC32_F_READ || operation==SC32_F_WRITE){
        if(!reserved(q,5) || q[4]>SC32_FILE_IO_MAX)return SC32_E_RANGE;
        if(h->closing)return SC32_E_BUSY;
        if(!(d->flags&(operation==SC32_F_READ?FS_NATIVE_READ:FS_NATIVE_WRITE)))return SC32_E_ACCESS;
        if(q[4] && (!(operation==SC32_F_READ?paging_user_write_range(TASK(pid).pd,q[3],q[4]):paging_user_range(TASK(pid).pd,q[3],q[4]))
           || overlap(q[3],q[4],output,sizeof(r))))return SC32_E_INVALID;
        if(h->completed_kind){
            int cached=operation==SC32_F_WRITE?completed(h,FS_NATIVE_WRITE_OPERATION,q[4],&r):-1;
            if(cached<0)return SC32_E_BUSY;
            int copied=publish(pid,output,&r,sizeof(r));return copied?copied:native_status(r.raw);
        }
        if(d->standard>=0){
            raw=operation==SC32_F_READ?streams_native_read(pid,h->native_reference,(void *)q[3],q[4])
                :streams_native_write(pid,h->native_reference,(const void *)q[3],q[4]);
            if(raw>=0)transferred=(u32)raw;
            status=stream_status(raw);
        }else if(operation==SC32_F_READ){
            raw=fs_native_read(pid,d->object,(void *)q[3],q[4],d->position,&transferred);status=native_status(raw);d->position+=transferred;
        }else{
            u32 position=d->position;
            raw=fs_native_write(pid,d->object,h->id,(const void *)q[3],q[4],d->position,(d->flags&FS_NATIVE_APPEND)!=0,&transferred,&position);
            status=native_status(raw);if(transferred)d->position=position+transferred;
        }
        if(operation==SC32_F_WRITE && h->completed_kind){
            int cached=completed(h,FS_NATIVE_WRITE_OPERATION,q[4],&r);if(cached<0)return SC32_E_BUSY;
            int copied=publish(pid,output,&r,sizeof(r));return copied?copied:native_status(r.raw);
        }
        file_reply(pid,h,d,&r,raw>=0 && d->standard>=0?0:raw,transferred);
        {int copied=publish(pid,output,&r,sizeof(r));if(copied)return copied;}
        return status;
    }
    if(operation==SC32_F_SEEK){
        if(!reserved(q,5) || q[3]>SC32_SEEK_END)return SC32_E_INVALID;
        if(h->closing)return SC32_E_BUSY;
        if(d->standard>=0){u32 info[8];raw=streams_native_query(pid,h->native_reference,info);
            if(!raw && !(info[6]&1u))raw=-12;
            if(raw){file_reply(pid,h,d,&r,raw,0);int copied=publish(pid,output,&r,sizeof(r));return copied?copied:stream_status(raw);}}
        file_reply(pid,h,d,&r,0,0);u32 base=q[3]==SC32_SEEK_SET?0:q[3]==SC32_SEEK_CUR?r.position:r.length;
        ScI32 offset=(ScI32)q[4];u32 position;
        if(offset<0){u32 subtract=0u-(u32)offset;if(subtract>base)return SC32_E_RANGE;position=base-subtract;}
        else{if((u32)offset>0xffffffffu-base)return SC32_E_RANGE;position=base+(u32)offset;}
        if(d->standard>=0){raw=streams_native_seek(pid,h->native_reference,position);status=stream_status(raw);}
        else{fs_native_info_t info;raw=fs_native_info(pid,d->object,d->flags&3u,&info);status=native_status(raw);
            if(!status){fs_native_cancel(d->object,h->id);h->completed_kind=0;d->position=position;}}
        if(status){file_reply(pid,h,d,&r,raw,0);int copied=publish(pid,output,&r,sizeof(r));return copied?copied:status;}
        file_reply(pid,h,d,&r,0,0);return publish(pid,output,&r,sizeof(r));
    }
    if(!reserved(q,4) || (operation!=SC32_F_TRUNCATE && q[3]))return SC32_E_INVALID;
    if(operation==SC32_F_DUPLICATE)return duplicate_file(pid,h,output);
    if(operation==SC32_F_CLOSE && h->lease_metadata_only){
        if(h->lease && !fs_lease_detach_step(pid,h->id,h->lease))return SC32_E_BUSY;
        file_reply(pid,h,d,&r,0,0);drop(h);r.handle=0;return publish(pid,output,&r,sizeof(r));
    }
    if(operation==SC32_F_CLOSE && h->lease){
        int complete=fs_lease_file_private(d->object) || fs_lease_close_committed(h->lease,d->object);
        if(!fs_lease_detach_step(pid,h->id,h->lease))return SC32_E_BUSY;
        if(complete){file_reply(pid,h,d,&r,0,0);drop(h);r.handle=0;return publish(pid,output,&r,sizeof(r));}
    }
    if(operation==SC32_F_QUERY || operation==SC32_F_TELL){
        if(d->standard>=0){u32 info[8];raw=streams_native_query(pid,h->native_reference,info);
            if(!raw && operation==SC32_F_TELL && !(info[6]&2u))raw=-12;
            status=stream_status(raw);}
        else{fs_native_info_t info;raw=fs_native_info(pid,d->object,0,&info);status=native_status(raw);}
        file_reply(pid,h,d,&r,raw,0);{int copied=publish(pid,output,&r,sizeof(r));return copied?copied:status;}
    }
    if(h->closing && operation!=SC32_F_CLOSE)return SC32_E_BUSY;
    u32 completion_kind=operation==SC32_F_TRUNCATE?FS_NATIVE_TRUNCATE_OPERATION:FS_NATIVE_FLUSH_OPERATION;
    if(operation==SC32_F_CLOSE && !h->closing && h->completed_kind && h->completed_kind!=FS_NATIVE_FLUSH_OPERATION)h->completed_kind=0;
    int cached=completed(h,completion_kind,q[3],&r);
    if(cached<0)return SC32_E_BUSY;
    if(cached>0){
        status=native_status(r.raw);if(operation==SC32_F_CLOSE){drop(h);r.handle=0;}
        int copied=publish(pid,output,&r,sizeof(r));return copied?copied:status;
    }
    if(operation==SC32_F_TRUNCATE){
        if(d->standard>=0)return SC32_E_NOT_SUPPORTED;
        if(!(d->flags&FS_NATIVE_WRITE))return SC32_E_ACCESS;
        raw=fs_native_truncate(pid,d->object,h->id,q[3]);status=native_status(raw);
    }else{
        if(operation==SC32_F_CLOSE && !h->closing){
            h->closing=1;if(d->object)fs_native_cancel(d->object,h->id);
        }
        if(d->standard>=0){raw=streams_native_flush(pid,h->native_reference);status=stream_status(raw);}
        else if(d->flags&FS_NATIVE_WRITE){raw=fs_native_flush(pid,d->object,h->id);status=native_status(raw);}
        else{fs_native_info_t info;raw=fs_native_info(pid,d->object,FS_NATIVE_READ,&info);status=native_status(raw);}
    }
    cached=completed(h,completion_kind,q[3],&r);
    if(cached>0){raw=r.raw;status=native_status(raw);}else file_reply(pid,h,d,&r,raw,0);
    if(operation==SC32_F_CLOSE && raw!=FS_N_AGAIN){drop(h);r.handle=0;}
    if(!status || status==SC32_E_AGAIN || status==SC32_E_IO || operation==SC32_F_CLOSE){
        int copied=publish(pid,output,&r,sizeof(r));if(copied)return copied;
    }
    return status;
}

#include "sc32_file_lease.inc"
