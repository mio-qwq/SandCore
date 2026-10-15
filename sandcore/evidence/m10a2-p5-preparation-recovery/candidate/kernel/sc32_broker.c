#include "sc32_broker.h"
#include "sc32_core.h"
#include "sc32_gui.h"
#include "task.h"
#include "task_store.h"
#include "auth.h"
#include "session.h"
#include "objpool.h"
#include "paging.h"
#include "memory.h"
#include "fs.h"
#include "../user/sc32/SC32BROKERWIRE.H"
/* 所有入口在单CPU/IF=0运行。对象池的32项只是页内分配粒度；拥有者
* 链、AVL索引和候选/垃圾链都动态增长，不用固定句柄数组替代task上限。
* 大共享对象每步16页；退出只沿本人链，后台只沿真实待通知/待回收链。 */
typedef struct br_avl br_avl;
struct br_avl {br_avl *left,*right,*parent;
    u32 height;
};
typedef struct br_gc br_gc;
struct br_gc {br_gc *next;
    u32 kind,queued;
};
typedef struct br_blob br_blob;
typedef struct br_clip br_clip;
typedef struct br_format br_format;
typedef struct br_session br_session;
typedef struct br_endpoint br_endpoint;
typedef struct br_connection br_connection;
typedef struct br_message br_message;
typedef struct br_shared br_shared;
typedef struct br_handle br_handle;
typedef struct br_offer br_offer;
enum {BR_CANDIDATE=1,BR_SNAPSHOT,BR_ENDPOINT,BR_CHANNEL,BR_SHARED,BR_GRANT,BR_MAPPING,BR_OFFER};
enum {BR_GC_BLOB=1,BR_GC_CLIP,BR_GC_ENDPOINT,BR_GC_CHANNEL,BR_GC_SHARED};
typedef struct {
    u32 generation,head,ipc_head,mapping_head,stopping,watch_session,watch_epoch;
    int watch_previous,watch_next;
}
br_owner;
struct br_session {
    u32 id,epoch,closing,candidates;
    br_clip *current;
    int watch_head,notify_cursor;
    u32 notify_epoch,notify_queued;
    br_session *notify_next;
};
struct br_blob {br_gc gc;
    int index;
    u32 references,bytes,head,tail;
};
typedef struct {u32 next,used;
    u8 data[4088];
}
br_blob_page;
struct br_format {
    br_avl tree;
    int index;
    br_clip *owner;
    br_format *next;
    u32 format;
    br_blob *blob;
};
struct br_clip {
    br_gc gc;
    int index;
    u32 references,session,budget,used,bytes,count,epoch,writers;
    ScBrokerIdentity publisher;
    br_avl *root;
    br_format *first,*last;
};
struct br_endpoint {
    br_gc gc;
    br_avl tree;
    int index;
    u32 handle,namespace_id,scope,closing,listening,references;
    Sc32IpcCreate desc;
    char name[64];
    ScBrokerIdentity owner;
    br_connection *first,*last;
    u32 pending,pending_bytes,epoch;
};
struct br_offer {
    int index;
    u32 candidate,format,total,received,error;
    br_clip *clip;
    br_blob *blob;
    u32 stage,need,used,seen,count,padding,utf8_need,scalar,minimum;
    u8 saved[88];
};
struct br_connection {
    int index;
    u32 ports[2],references,accepted,failed;
    u32 budget,max_frame;
    br_endpoint *listener;
    br_connection *previous,*next;
    u32 sequence_lo[2],sequence_hi[2];
};
struct br_message {
    int index;
    br_message *next;
    ScIpcPacket packet;
    br_blob *blob;
    u32 charge;
    u8 small[128];
};
typedef struct br_frames br_frames;
struct br_frames {br_frames *next,*previous;
    u32 count,reserved;
    u32 frames[1020];
};
struct br_shared {
    br_gc gc;
    int index;
    u32 object,references,bytes,pages,ready_pages,state,error,mappings;
    ScBrokerIdentity creator;
    br_frames *first,*last;
};
struct br_handle {
    br_gc gc;
    u32 id,owner,generation,kind,previous,next,closed;
    union {
        br_offer *offer;
        br_clip *clip;
        br_endpoint *endpoint;
        struct {br_connection *connection;
            u32 side,used,count,epoch;
            br_message *first,*last;
            ScBrokerIdentity peer;
        }
        channel;
        struct {br_shared *object;
            u32 access,offset,bytes;
        }
        shared;
        struct {br_shared *object;
            u32 access,offset,bytes,flags;
            ScBrokerIdentity target;
        }
        grant;
        struct {br_shared *object;
            u32 pd,base,bytes,offset,access,bound,removed,state,error,epoch;
        }
        mapping;
    }
    body;
};

static objpool_t br_owners,br_sessions,br_handles,br_blobs,br_clips,br_formats,br_endpoints,br_connections,br_messages,br_shared_objects,br_offers;

static u32 br_started,br_serial;

static br_avl *br_endpoint_root;

static br_gc *br_garbage_first,*br_garbage_last;

static br_session *br_notify_first,*br_notify_last;
typedef char br_peer_size[(sizeof(ScBrokerIdentity)==48)?1:-1];
typedef char br_clip_info_size[(sizeof(ScClipboardInfo)==96)?1:-1];
typedef char br_endpoint_desc_size[(sizeof(ScIpcEndpointDesc)==64)?1:-1];
typedef char br_packet_size[(sizeof(ScIpcPacket)==80)?1:-1];
typedef char br_shared_info_size[(sizeof(ScSharedInfo)==64)?1:-1];
typedef char br_frame_page_size[(sizeof(br_frames)==4096)?1:-1];

static void br_zero(void *out,u32 bytes){u8 *p=out;
    for(u32 i=0;i<bytes;i++)p[i]=0;
}

static void br_copy(void *out,const void *in,u32 bytes){u8 *a=out;
    const u8 *b=in;
    for(u32 i=0;i<bytes;i++)a[i]=b[i];
}

static u32 br_height(br_avl *p){return p?p->height:0;
}

static void br_height_set(br_avl *p){u32 a=br_height(p->left),b=br_height(p->right);
    p->height=1u+(a>b?a:b);
}

static void br_tree_replace(br_avl **root,br_avl *old,br_avl *fresh)
{
    if(!old->parent)*root=fresh;
    else if(old->parent->left==old)old->parent->left=fresh;
    else old->parent->right=fresh;
    if(fresh)fresh->parent=old->parent;
}

static br_avl *br_rotate(br_avl **root,br_avl *p,int left)
{
    br_avl *q=left?p->right:p->left;
    br_tree_replace(root,p,q);
    if(left){p->right=q->left;
        if(p->right)p->right->parent=p;
        q->left=p;
    }
    else{p->left=q->right;
        if(p->left)p->left->parent=p;
        q->right=p;
    }
    p->parent=q;
    br_height_set(p);
    br_height_set(q);
    return q;
}

static void br_balance(br_avl **root,br_avl *p)
{
    while(p){br_height_set(p);
        i32 difference=(i32)br_height(p->left)-(i32)br_height(p->right);
        if(difference>1){if(br_height(p->left->right)>br_height(p->left->left))br_rotate(root,p->left,1);
            p=br_rotate(root,p,0);
        }
        else if(difference<-1){if(br_height(p->right->left)>br_height(p->right->right))br_rotate(root,p->right,0);
            p=br_rotate(root,p,1);
        }
        p=p->parent;
    }
}

static void br_tree_remove(br_avl **root,br_avl *p)
{
    br_avl *start=p->parent;
    if(!p->left)br_tree_replace(root,p,p->right);
    else if(!p->right)br_tree_replace(root,p,p->left);
    else{
        br_avl *next=p->right;
        while(next->left)next=next->left;
        if(next->parent!=p){start=next->parent;
            br_tree_replace(root,next,next->right);
            next->right=p->right;
            next->right->parent=next;
        }
        else start=next;
        br_tree_replace(root,p,next);
        next->left=p->left;
        next->left->parent=next;
        br_height_set(next);
    }
    p->left=p->right=p->parent=0;
    p->height=1;
    br_balance(root,start);
}

static void br_gc_add(br_gc *gc,u32 kind)
{
    if(gc->queued)return;
    gc->queued=1;
    gc->kind=kind;
    gc->next=0;
    if(br_garbage_last)br_garbage_last->next=gc;
    else br_garbage_first=gc;
    br_garbage_last=gc;
    task_kernel_wake();
}

static int br_identity(int pid,ScBrokerIdentity *out)
{
    u32 auth[8];
    if(pid<=0 || !task_owned(pid) || (TASK(pid).state!=1 && TASK(pid).state!=3) || auth_info(pid,auth))return SC32_E_STALE;
    session_t *session=session_get(session_task(pid));
    if(!session || session->closing || session->kind==SESSION_LOCKED || auth_subject_uid(pid)<AUTH_SYSTEM)return SC32_E_ACCESS;
    br_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->pid=(u32)pid;
    out->generation=task_generation(pid);
    out->uid=auth_uid(pid);
    out->gid=auth_gid(pid);
    out->realm=auth[3];
    out->session=session->id;
    out->subject_uid=auth_subject_uid(pid);
    out->subject_gid=auth_subject_gid(pid);
    if(auth_can_mod(pid))out->flags=SC32_BROKER_ID_HARDWARE_SYSTEM;
    return 0;
}

static int br_identity_same(const ScBrokerIdentity *a,const ScBrokerIdentity *b)
{
    return a->pid==b->pid && a->generation==b->generation && a->uid==b->uid && a->gid==b->gid
    && a->realm==b->realm && a->session==b->session && a->subject_uid==b->subject_uid && a->subject_gid==b->subject_gid;
}

static br_owner *br_owner_get(int pid,int create)
{
    br_owner *p=objpool_get(&br_owners,pid);
    if(p)return p->generation==task_generation(pid)?p:0;
    if(!create || objpool_claim(&br_owners,pid))return 0;
    p=objpool_get(&br_owners,pid);
    p->generation=task_generation(pid);
    p->watch_previous=p->watch_next=-1;
    return p;
}

static int br_handle_error(void){return br_serial==0x7FFFFFFFu?SC32_E_RANGE:SC32_E_MEMORY;
}

static void br_owner_trim(int pid)
{
    br_owner *p=objpool_get(&br_owners,pid);
    if(p && !p->head && !p->ipc_head && !p->mapping_head && !p->watch_session)objpool_release(&br_owners,pid);
}

static br_session *br_session_get(u32 id,int create)
{
    if(!id || id>0x7FFFFFFFu)return 0;
    br_session *p=objpool_get(&br_sessions,(int)id);
    if(p || !create)return p;
    if(objpool_claim(&br_sessions,(int)id))return 0;
    p=objpool_get(&br_sessions,(int)id);
    p->id=id;
    p->epoch=1;
    p->watch_head=p->notify_cursor=-1;
    return p;
}

static void br_session_trim(br_session *session)
{
    if(session && !session->current && !session->candidates && session->watch_head<0 && !session->notify_queued
    && (session->closing || session->epoch==1))objpool_release(&br_sessions,(int)session->id);
}

/* 同一拥有者三条动态资源链：IPC先撤/唤醒peer，其余能力随后，
 * 共享映射最后逐页拆。不能让新插head的64MiB mapping把已EOF的
 * 独立IPC peer挂在旧channel之前，等待1024轮后才收到关闭事实。
 * 这只是类型分类，不给任一种对象增加固定数量门槛。 */
static u32 *br_owner_head(br_owner *owner,u32 kind)
{
    if(kind==BR_MAPPING)return &owner->mapping_head;
    if(kind==BR_ENDPOINT || kind==BR_CHANNEL)return &owner->ipc_head;
    return &owner->head;
}

static br_handle *br_handle_new(int pid,u32 kind)
{
    if(br_serial==0x7FFFFFFFu)return 0;
    br_owner *owner=br_owner_get(pid,1);
    if(!owner)return 0;
    u32 id=br_serial+1u;
    if(objpool_claim(&br_handles,(int)id)){br_owner_trim(pid);
        return 0;
    }
    br_handle *p=objpool_get(&br_handles,(int)id);
    p->id=id;
    p->owner=(u32)pid;
    p->generation=owner->generation;
    p->kind=kind;
    u32 *head=br_owner_head(owner,kind);p->next=*head;
    if(p->next)((br_handle *)objpool_get(&br_handles,(int)p->next))->previous=id;
    *head=id;
    br_serial=id;
    return p;
}

static br_handle *br_handle_any(u32 id)
{return id && id<=0x7FFFFFFFu?objpool_get(&br_handles,(int)id):0;
}

static br_handle *br_handle_get(int pid,u32 id,u32 kind)
{
    br_handle *p=br_handle_any(id);
    return p && !p->closed && p->owner==(u32)pid && p->generation==task_generation(pid) && (!kind || p->kind==kind)?p:0;
}

static void br_handle_unlink(br_handle *p)
{
    br_owner *owner=objpool_get(&br_owners,(int)p->owner);
    if(p->previous)br_handle_any(p->previous)->next=p->next;
    else if(owner)*br_owner_head(owner,p->kind)=p->next;
    if(p->next)br_handle_any(p->next)->previous=p->previous;
    p->previous=p->next=0;
}

static void br_handle_drop(br_handle *p)
{int pid=(int)p->owner;
    br_handle_unlink(p);
    objpool_release(&br_handles,(int)p->id);
    br_owner_trim(pid);
}

static int br_publish(int pid,u32 output,const void *value,u32 bytes)
{return bytes && paging_user_copy(TASK(pid).pd,output,(void *)value,bytes,1)?SC32_E_INVALID:0;
}

static int br_span(int pid,u32 address,u32 bytes,int write)
{
    if(!bytes)return 0;
    return (write?paging_user_write_range(TASK(pid).pd,address,bytes):paging_user_range(TASK(pid).pd,address,bytes))?0:SC32_E_INVALID;
}

static int br_overlap(u32 a,u32 na,u32 b,u32 nb)
{return na && nb && (a<=b?b-a<na:a-b<nb);
}

static int br_output_overlap(int pid,u32 output,u32 data,u32 bytes)
{u32 declared;
    return paging_user_copy(TASK(pid).pd,output,&declared,4,0) || br_overlap(output,declared,data,bytes);
}

static int br_handle_input(int pid,u32 request,u32 output,u32 width,Sc32BrokerHandle *local)
{
    int error=sc32_core_input(pid,request,local,sizeof(*local));
    if(!error)error=width?sc32_core_output(pid,output,width):output?SC32_E_INVALID:0;
    return error;
}

static void br_token(Sc32BrokerToken *out,u32 handle,u32 value)
{out->size=sizeof(*out);
    out->version=1;
    out->handle=handle;
    out->value=value;
}

static void br_notify(br_handle *p)
{if(p && !p->closed)task_notify_generation((int)p->owner,p->generation,TASK_EVENT_STREAM);
}

static void br_blob_unref(br_blob *p)
{if(p && !--p->references)br_gc_add(&p->gc,BR_GC_BLOB);
}

static br_blob *br_blob_from_user(int pid,u32 address,u32 bytes)
{
    int index=objpool_alloc(&br_blobs);
    if(index<0)return 0;
    br_blob *p=objpool_get(&br_blobs,index);
    p->index=index;
    p->references=1;
    p->bytes=bytes;
    for(u32 done=0;done<bytes;){
        u32 physical=pframe_alloc();
        if(!physical){br_blob_unref(p);
            return 0;
        }
        br_blob_page *page=(br_blob_page *)physical;
        page->next=0;
        page->used=bytes-done;
        if(page->used>sizeof(page->data))page->used=sizeof(page->data);
        if(p->tail)((br_blob_page *)p->tail)->next=physical;
        else p->head=physical;
        p->tail=physical;
        if(paging_user_copy(TASK(pid).pd,address+done,page->data,page->used,0)){br_blob_unref(p);
            return 0;
        }
        done+=page->used;
    }
    return p;
}

static int br_blob_copy(br_blob *blob,u32 offset,void *out,u32 bytes)
{
    if(offset>blob->bytes || bytes>blob->bytes-offset)return SC32_E_RANGE;
    br_blob_page *page=(br_blob_page *)blob->head;
    u8 *target=out;
    while(page && offset>=page->used){offset-=page->used;
        page=(br_blob_page *)page->next;
    }
    for(u32 done=0;done<bytes;){
        if(!page)return SC32_E_FAILED;
        u32 count=page->used-offset;
        if(count>bytes-done)count=bytes-done;
        br_copy(target+done,page->data+offset,count);
        done+=count;
        offset=0;
        page=(br_blob_page *)page->next;
    }
    return 0;
}

static int br_blob_publish(int pid,br_blob *blob,u32 offset,u32 address,u32 bytes)
{
    br_blob_page *page=(br_blob_page *)blob->head;
    while(page && offset>=page->used){offset-=page->used;
        page=(br_blob_page *)page->next;
    }
    for(u32 done=0;done<bytes;){
        if(!page)return SC32_E_FAILED;
        u32 count=page->used-offset;
        if(count>bytes-done)count=bytes-done;
        if(paging_user_copy(TASK(pid).pd,address+done,page->data+offset,count,1))return SC32_E_INVALID;
        done+=count;
        offset=0;
        page=(br_blob_page *)page->next;
    }
    return 0;
}

static int br_utf8(const u8 *data,u32 bytes)
{
    for(u32 at=0;at<bytes;){
        u32 first=data[at++],scalar=first,count=0,minimum=0;
        if(!first)return SC32_E_INVALID;
        if(first<0x80u)continue;
        if(first>=0xC2u && first<=0xDFu){count=1;
            minimum=0x80u;
            scalar=first&31u;
        }
        else if(first>=0xE0u && first<=0xEFu){count=2;
            minimum=0x800u;
            scalar=first&15u;
        }
        else if(first>=0xF0u && first<=0xF4u){count=3;
            minimum=0x10000u;
            scalar=first&7u;
        }
        else return SC32_E_INVALID;
        if(count>bytes-at)return SC32_E_INVALID;
        while(count--){u32 c=data[at++];
            if((c&0xC0u)!=0x80u)return SC32_E_INVALID;
            scalar=(scalar<<6)|(c&63u);
        }
        if(scalar<minimum || scalar>0x10FFFFu || (scalar>=0xD800u && scalar<=0xDFFFu))return SC32_E_INVALID;
    }
    return 0;
}

static int br_clip_validate(br_blob *blob,u32 format)
{
    if(format==SC32_CLIPBOARD_TEXT){
        if(!blob->bytes)return SC32_E_INVALID;
        /* 用户任务内核栈只有4KiB，不能再在此留一页缓冲。顺序游标
        * 跨页读取UTF8状态，只有几个标量；复杂度为正文一次扫描。 */
        br_blob_page *page=(br_blob_page *)blob->head;
        u32 at=0,remaining=blob->bytes,need=0,scalar=0,minimum=0;
        while(remaining){
            if(!page)return SC32_E_FAILED;
            if(at==page->used){page=(br_blob_page *)page->next;
                at=0;
                continue;
            }
            u32 c=page->data[at++];
            remaining--;
            if(!remaining)return c || need?SC32_E_INVALID:0;
            if(!c)return SC32_E_INVALID;
            if(need){if((c&0xC0u)!=0x80u)return SC32_E_INVALID;
                scalar=(scalar<<6)|(c&63u);
                need--;
                if(!need && (scalar<minimum || scalar>0x10FFFFu || (scalar>=0xD800u && scalar<=0xDFFFu)))return SC32_E_INVALID;
            }else if(c<0x80u)continue;
            else if(c>=0xC2u && c<=0xDFu){need=1;
                scalar=c&31u;
                minimum=0x80u;
            }
            else if(c>=0xE0u && c<=0xEFu){need=2;
                scalar=c&15u;
                minimum=0x800u;
            }
            else if(c>=0xF0u && c<=0xF4u){need=3;
                scalar=c&7u;
                minimum=0x10000u;
            }
            else return SC32_E_INVALID;
        }
        return SC32_E_INVALID;
    }
    if(format==SC32_CLIPBOARD_FILES){
        ScClipboardFiles header;
        if(blob->bytes<sizeof(header) || br_blob_copy(blob,0,&header,sizeof(header)))return SC32_E_INVALID;
        if(header.size!=16 || header.version!=1 || !header.count || header.count>(blob->bytes-16u)/24u
        || (header.operation!=SC32_CLIPBOARD_COPY && header.operation!=SC32_CLIPBOARD_CUT))return SC32_E_INVALID;
        u32 at=16;
        for(u32 i=0;i<header.count;i++){
            ScClipboardFileRow row;
            if(br_blob_copy(blob,at,&row,sizeof(row)))return SC32_E_INVALID;
            if(row.path_bytes<2 || row.path_bytes>65 || row.record_bytes!=((20u+row.path_bytes+3u)&~3u)
            || row.record_bytes>blob->bytes-at || !row.revision || (row.flags&~SC32_CLIPBOARD_PIN_OBSERVATION)
            || ((row.flags&SC32_CLIPBOARD_PIN_OBSERVATION)?!row.object_id:row.object_id!=0))return SC32_E_INVALID;
            char path[65],normalized[64];
            if(br_blob_copy(blob,at+20,path,row.path_bytes))return SC32_E_INVALID;
            if(path[0]!='/' || path[row.path_bytes-1u] || br_utf8((u8 *)path,row.path_bytes-1u))return SC32_E_INVALID;
            int length=fs_normalize(path,normalized);
            if(length<0 || (u32)length+2u!=row.path_bytes)return SC32_E_INVALID;
            for(u32 j=0;j<(u32)length;j++)if(path[j+1u]!=normalized[j])return SC32_E_INVALID;
            for(u32 j=20u+row.path_bytes;j<row.record_bytes;j++){u8 padding;
                if(br_blob_copy(blob,at+j,&padding,1) || padding)return SC32_E_INVALID;
            }
            at+=row.record_bytes;
        }
        return at==blob->bytes?0:SC32_E_INVALID;
    }
    return format>=SC32_CLIPBOARD_CUSTOM_FIRST?0:SC32_E_INVALID;
}

static br_format *br_format_find(br_clip *clip,u32 format,br_avl ***slot,br_avl **parent)
{
    br_avl **link=&clip->root,*previous=0;
    while(*link){br_format *p=(br_format *)*link;
        if(p->format==format)return p;
        previous=*link;
        link=format<p->format?&(*link)->left:&(*link)->right;
    }
    if(slot)*slot=link;
    if(parent)*parent=previous;
    return 0;
}

static void br_clip_unref(br_clip *clip)
{if(clip && !--clip->references)br_gc_add(&clip->gc,BR_GC_CLIP);
}

static int br_clip_session(int pid,ScBrokerIdentity *identity,br_session **out,int create)
{
    int error=br_identity(pid,identity);
    if(error)return error;
    session_t *s=session_get(identity->session);
    if(s->kind==SESSION_SYSTEM && !auth_can_mod(pid))return SC32_E_ACCESS;
    br_session *p=br_session_get(identity->session,create);
    if(!p)return create?SC32_E_MEMORY:SC32_E_NOT_FOUND;
    if(p->closing)return SC32_E_ACCESS;
    *out=p;
    return 0;
}

static void br_clip_info(ScClipboardInfo *out,br_clip *clip,u32 handle,u32 epoch)
{
    br_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->handle=handle;
    out->epoch=epoch;
    if(clip){out->formats=clip->count;
        out->bytes=clip->bytes;
        out->budget_bytes=clip->budget;
        out->publisher=clip->publisher;
    }
    else out->flags=SC32_CLIPBOARD_EMPTY;
}

static void br_watch_unlink(int pid,br_owner *owner)
{
    if(!owner->watch_session)return;
    br_session *session=br_session_get(owner->watch_session,0);
    if(session){
        if(session->notify_cursor==pid)session->notify_cursor=owner->watch_next;
        if(owner->watch_previous>=0)((br_owner *)objpool_get(&br_owners,owner->watch_previous))->watch_next=owner->watch_next;
        else session->watch_head=owner->watch_next;
        if(owner->watch_next>=0)((br_owner *)objpool_get(&br_owners,owner->watch_next))->watch_previous=owner->watch_previous;
    }
    owner->watch_session=0;
    owner->watch_previous=owner->watch_next=-1;
    if(session && session->closing)br_session_trim(session);
}

static void br_clip_changed(br_session *session)
{
    if(!session->notify_queued && session->watch_head>=0){
        session->notify_queued=1;
        session->notify_epoch=session->epoch;
        session->notify_cursor=session->watch_head;
        session->notify_next=0;
        if(br_notify_last)br_notify_last->notify_next=session;
        else br_notify_first=session;
        br_notify_last=session;
    }
    task_kernel_wake();
}

static int br_clip_begin(int pid,u32 request,u32 output)
{
    ScClipboardDesc input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(Sc32BrokerToken));
    if(error)return error;
    if(input.flags || input.reserved[0] || input.reserved[1] || input.reserved[2] || input.reserved[3])return SC32_E_INVALID;
    u32 budget=input.budget_bytes?input.budget_bytes:0x100000u;
    if(budget<sizeof(br_format) || budget>0x04000000u)return SC32_E_RANGE;
    ScBrokerIdentity identity;
    br_session *session;
    error=br_clip_session(pid,&identity,&session,1);
    if(error)return error;
    if(session->candidates==0xFFFFFFFFu)return SC32_E_RANGE;
    int index=objpool_alloc(&br_clips);
    if(index<0){br_session_trim(session);
        return SC32_E_MEMORY;
    }
    br_clip *clip=objpool_get(&br_clips,index);
    clip->index=index;
    clip->references=1;
    clip->session=session->id;
    clip->budget=budget;
    clip->publisher=identity;
    br_handle *handle=br_handle_new(pid,BR_CANDIDATE);
    if(!handle){objpool_release(&br_clips,index);
        br_session_trim(session);
        return br_handle_error();
    }
    handle->body.clip=clip;
    session->candidates++;
    Sc32BrokerToken reply;
    br_token(&reply,handle->id,session->epoch);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_clip_offer(int pid,u32 request,u32 output)
{
    Sc32ClipboardOffer input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    if(output || (input.format!=SC32_CLIPBOARD_TEXT && input.format!=SC32_CLIPBOARD_FILES && input.format<SC32_CLIPBOARD_CUSTOM_FIRST))return SC32_E_INVALID;
    if(input.bytes>SC32_BROKER_IO_MAX)return SC32_E_RANGE;
    error=br_span(pid,input.data,input.bytes,0);
    if(error)return error;
    br_handle *handle=br_handle_get(pid,input.handle,BR_CANDIDATE);
    if(!handle)return SC32_E_STALE;
    ScBrokerIdentity identity;
    br_session *session;
    error=br_clip_session(pid,&identity,&session,0);
    if(error)return error;
    br_clip *clip=handle->body.clip;
    if(clip->session!=session->id)return SC32_E_ACCESS;
    br_avl **slot=0,*parent=0;
    br_format *old=br_format_find(clip,input.format,&slot,&parent);
    u32 existing=old?old->blob->bytes:0,extra=old?0u:sizeof(br_format);
    if(input.bytes>clip->budget-(clip->used-existing) || extra>clip->budget-(clip->used-existing)-input.bytes)return SC32_E_RANGE;
    br_blob *blob=br_blob_from_user(pid,input.data,input.bytes);
    if(!blob)return SC32_E_MEMORY;
    error=br_clip_validate(blob,input.format);
    if(error){br_blob_unref(blob);
        return error;
    }
    br_format *fresh=old;
    if(!fresh){int index=objpool_alloc(&br_formats);
        if(index<0){br_blob_unref(blob);
            return SC32_E_MEMORY;
        }
        fresh=objpool_get(&br_formats,index);
        fresh->index=index;
        fresh->owner=clip;
        fresh->format=input.format;
        fresh->tree.height=1;
        fresh->tree.parent=parent;
        *slot=&fresh->tree;
        br_balance(&clip->root,parent);
        if(clip->last)clip->last->next=fresh;
        else clip->first=fresh;
        clip->last=fresh;
        clip->count++;
    }
    br_blob *previous=fresh->blob;
    fresh->blob=blob;
    clip->used=clip->used-existing+input.bytes+extra;
    clip->bytes=clip->bytes-existing+input.bytes;
    br_blob_unref(previous);
    return 0;
}

static int br_clip_install(br_clip *clip,u32 format,br_blob *blob)
{
    br_avl **slot=0,*parent=0;
    br_format *entry=br_format_find(clip,format,&slot,&parent);
    u32 old=entry?entry->blob->bytes:0,extra=entry?0u:sizeof(br_format);
    if(blob->bytes>clip->budget-(clip->used-old) || extra>clip->budget-(clip->used-old)-blob->bytes)return SC32_E_RANGE;
    if(!entry){int index=objpool_alloc(&br_formats);
        if(index<0)return SC32_E_MEMORY;
        entry=objpool_get(&br_formats,index);
        entry->index=index;
        entry->owner=clip;
        entry->format=format;
        entry->tree.height=1;
        entry->tree.parent=parent;
        *slot=&entry->tree;
        br_balance(&clip->root,parent);
        if(clip->last)clip->last->next=entry;
        else clip->first=entry;
        clip->last=entry;
        clip->count++;
    }
    br_blob *previous=entry->blob;
    entry->blob=blob;
    clip->used=clip->used-old+blob->bytes+extra;
    clip->bytes=clip->bytes-old+blob->bytes;
    br_blob_unref(previous);
    return 0;
}

static int br_offer_feed(br_offer *offer,const u8 *data,u32 bytes)
{
    if(offer->format==SC32_CLIPBOARD_TEXT){
        for(u32 i=0;i<bytes;i++){u32 c=data[i];
            offer->received++;
            if(offer->received==offer->total){if(c || offer->utf8_need)return SC32_E_INVALID;
                continue;
            }
            if(!c)return SC32_E_INVALID;
            if(offer->utf8_need){if((c&0xC0u)!=0x80u)return SC32_E_INVALID;
                offer->scalar=(offer->scalar<<6)|(c&63u);
                offer->utf8_need--;
                if(!offer->utf8_need && (offer->scalar<offer->minimum || offer->scalar>0x10FFFFu || (offer->scalar>=0xD800u && offer->scalar<=0xDFFFu)))return SC32_E_INVALID;
            }else if(c<0x80u)continue;
            else if(c>=0xC2u && c<=0xDFu){offer->utf8_need=1;
                offer->scalar=c&31u;
                offer->minimum=0x80u;
            }
            else if(c>=0xE0u && c<=0xEFu){offer->utf8_need=2;
                offer->scalar=c&15u;
                offer->minimum=0x800u;
            }
            else if(c>=0xF0u && c<=0xF4u){offer->utf8_need=3;
                offer->scalar=c&7u;
                offer->minimum=0x10000u;
            }
            else return SC32_E_INVALID;
        }
        return 0;
    }
    if(offer->format!=SC32_CLIPBOARD_FILES){offer->received+=bytes;
        return 0;
    }
    for(u32 at=0;at<bytes;){
        if(offer->stage==4)return SC32_E_INVALID;
        u32 take=offer->need-offer->used;
        if(take>bytes-at)take=bytes-at;
        br_copy(offer->saved+offer->used,data+at,take);
        offer->used+=take;
        offer->received+=take;
        at+=take;
        if(offer->used!=offer->need)continue;
        if(offer->stage==0){ScClipboardFiles header;
            br_copy(&header,offer->saved,sizeof(header));
            if(header.size!=16 || header.version!=1 || !header.count || header.count>(offer->total-16u)/24u || (header.operation!=1 && header.operation!=2))return SC32_E_INVALID;
            offer->count=header.count;
            offer->stage=1;
            offer->need=20;
        }else if(offer->stage==1){ScClipboardFileRow row;
            br_copy(&row,offer->saved,sizeof(row));
            if(row.path_bytes<2 || row.path_bytes>65 || row.record_bytes!=((20u+row.path_bytes+3u)&~3u) || row.record_bytes>offer->total-(offer->received-20u)
            || !row.revision || (row.flags&~SC32_CLIPBOARD_PIN_OBSERVATION) || ((row.flags&SC32_CLIPBOARD_PIN_OBSERVATION)?!row.object_id:row.object_id!=0))return SC32_E_INVALID;
            offer->stage=2;
            offer->need=row.path_bytes;
            offer->padding=row.record_bytes-20u-row.path_bytes;
        }else if(offer->stage==2){char normalized[64];
            if(offer->saved[0]!='/' || offer->saved[offer->need-1u] || br_utf8(offer->saved,offer->need-1u))return SC32_E_INVALID;
            int length=fs_normalize((char *)offer->saved,normalized);
            if(length<0 || (u32)length+2u!=offer->need)return SC32_E_INVALID;
            for(u32 i=0;i<(u32)length;i++)if(offer->saved[i+1u]!=(u8)normalized[i])return SC32_E_INVALID;
            if(offer->padding){offer->stage=3;
                offer->need=offer->padding;
            }
            else{offer->seen++;
                offer->stage=offer->seen==offer->count?4u:1u;
                offer->need=20;
            }
        }else{
            for(u32 i=0;i<offer->need;i++)if(offer->saved[i])return SC32_E_INVALID;
            offer->seen++;
            offer->stage=offer->seen==offer->count?4u:1u;
            offer->need=20;
        }
        offer->used=0;
    }
    return 0;
}

static int br_offer_begin(int pid,u32 request,u32 output)
{
    Sc32ClipboardOfferBegin input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(Sc32BrokerToken));
    if(error)return error;
    if(input.reserved || (input.format!=1 && input.format!=2 && input.format<SC32_CLIPBOARD_CUSTOM_FIRST) || (input.format==1 && !input.total_bytes) || (input.format==2 && input.total_bytes<40))return SC32_E_INVALID;
    br_handle *candidate=br_handle_get(pid,input.handle,BR_CANDIDATE);
    if(!candidate)return SC32_E_STALE;
    ScBrokerIdentity identity;
    br_session *session;
    error=br_clip_session(pid,&identity,&session,0);
    if(error)return error;
    br_clip *clip=candidate->body.clip;
    if(clip->session!=session->id)return SC32_E_ACCESS;
    br_format *old=br_format_find(clip,input.format,0,0);
    u32 previous=old?old->blob->bytes:0,extra=old?0u:sizeof(br_format);
    if(input.total_bytes>clip->budget-(clip->used-previous) || extra>clip->budget-(clip->used-previous)-input.total_bytes)return SC32_E_RANGE;
    if(clip->references==0xFFFFFFFFu || clip->writers==0xFFFFFFFFu)return SC32_E_RANGE;
    int index=objpool_alloc(&br_offers);
    if(index<0)return SC32_E_MEMORY;
    br_offer *offer=objpool_get(&br_offers,index);
    br_blob *blob=br_blob_from_user(pid,0,0);
    if(!blob){objpool_release(&br_offers,index);
        return SC32_E_MEMORY;
    }
    br_handle *handle=br_handle_new(pid,BR_OFFER);
    if(!handle){br_blob_unref(blob);
        objpool_release(&br_offers,index);
        return br_handle_error();
    }
    offer->index=index;
    offer->candidate=candidate->id;
    offer->clip=clip;
    offer->blob=blob;
    offer->format=input.format;
    offer->total=input.total_bytes;
    offer->need=16;
    clip->references++;
    clip->writers++;
    handle->body.offer=offer;
    Sc32BrokerToken reply;
    br_token(&reply,handle->id,0);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_offer_write(int pid,u32 request,u32 output)
{
    Sc32ClipboardOfferWrite input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    if(output || input.bytes>SC32_BROKER_IO_MAX)return output?SC32_E_INVALID:SC32_E_RANGE;
    error=br_span(pid,input.data,input.bytes,0);
    if(error)return error;
    br_handle *handle=br_handle_get(pid,input.handle,BR_OFFER);
    if(!handle)return SC32_E_STALE;
    br_offer *offer=handle->body.offer;
    if(offer->error)return (int)offer->error;
    if(!br_handle_get(pid,offer->candidate,BR_CANDIDATE))return SC32_E_CANCELLED;
    if(input.offset!=offer->received || input.bytes>offer->total-offer->received)return SC32_E_RANGE;
    br_blob *blob=offer->blob;
    for(u32 done=0;done<input.bytes;){
        br_blob_page *page=(br_blob_page *)blob->tail;
        if(!page || page->used==sizeof(page->data)){
            u32 physical=pframe_alloc();
            if(!physical){offer->error=(u32)SC32_E_MEMORY;
                return SC32_E_MEMORY;
            }
            page=(br_blob_page *)physical;
            page->used=page->next=0;
            if(blob->tail)((br_blob_page *)blob->tail)->next=physical;
            else blob->head=physical;
            blob->tail=physical;
        }
        u32 count=sizeof(page->data)-page->used;
        if(count>input.bytes-done)count=input.bytes-done;
        u8 *fresh=page->data+page->used;
        if(paging_user_copy(TASK(pid).pd,input.data+done,fresh,count,0)){offer->error=(u32)SC32_E_INVALID;
            return SC32_E_INVALID;
        }
        page->used+=count;
        blob->bytes+=count;
        error=br_offer_feed(offer,fresh,count);
        if(error){offer->error=(u32)error;
            return error;
        }
        done+=count;
    }
    return 0;
}

static void br_offer_drop(br_handle *handle)
{
    br_offer *offer=handle->body.offer;
    offer->clip->writers--;
    br_clip_unref(offer->clip);
    br_blob_unref(offer->blob);
    objpool_release(&br_offers,offer->index);
    br_handle_drop(handle);
}

static int br_offer_finish(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,0,&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_OFFER);
    if(!handle)return SC32_E_STALE;
    br_offer *offer=handle->body.offer;
    if(offer->error)return (int)offer->error;
    br_handle *candidate=br_handle_get(pid,offer->candidate,BR_CANDIDATE);
    if(!candidate || candidate->body.clip!=offer->clip)return SC32_E_CANCELLED;
    ScBrokerIdentity identity;
    br_session *session;
    error=br_clip_session(pid,&identity,&session,0);
    if(error)return error;
    if(session->id!=offer->clip->session)return SC32_E_ACCESS;
    if(offer->received!=offer->total || (offer->format==SC32_CLIPBOARD_FILES && offer->stage!=4) || (offer->format==SC32_CLIPBOARD_TEXT && offer->utf8_need))return SC32_E_INVALID;
    error=br_clip_install(offer->clip,offer->format,offer->blob);
    if(error)return error;
    offer->blob=0;
    br_offer_drop(handle);
    return 0;
}

static int br_clip_commit(int pid,u32 request,u32 output,int clear)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScClipboardInfo),&input);
    if(error)return error;
    ScBrokerIdentity identity;
    br_session *session;
    error=br_clip_session(pid,&identity,&session,1);
    if(error)return error;
    if(input.value && input.value!=session->epoch)return SC32_E_STALE;
    if(session->epoch==0xFFFFFFFFu)return SC32_E_RANGE;
    br_handle *handle=clear?0:br_handle_get(pid,input.handle,BR_CANDIDATE);
    if(clear && input.handle)return SC32_E_INVALID;
    if(!clear && !handle)return SC32_E_STALE;
    br_clip *candidate=handle?handle->body.clip:0;
    if(candidate && candidate->writers)return SC32_E_BUSY;
    if(candidate && (!candidate->count || candidate->session!=session->id))return SC32_E_INVALID;
    br_clip *previous=session->current;
    session->epoch++;
    if(candidate){candidate->references++;
        candidate->epoch=session->epoch;
        candidate->publisher=identity;
    }
    session->current=candidate;
    if(handle){session->candidates--;
        br_clip_unref(candidate);
        br_handle_drop(handle);
    }
    br_clip_unref(previous);
    br_clip_changed(session);
    ScClipboardInfo reply;
    br_clip_info(&reply,candidate,0,session->epoch);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_clip_current(int pid,u32 request,u32 output,int open)
{
    int error=sc32_core_output(pid,output,sizeof(ScClipboardInfo));
    if(error)return error;
    if(request)return SC32_E_INVALID;
    ScBrokerIdentity identity;
    br_session *session=0;
    error=br_clip_session(pid,&identity,&session,0);
    if(error==SC32_E_NOT_FOUND){if(open)return error;
        ScClipboardInfo empty;
        br_clip_info(&empty,0,0,1);
        return br_publish(pid,output,&empty,sizeof(empty));
    }
    if(error)return error;
    br_handle *handle=0;
    if(open){if(!session->current)return SC32_E_NOT_FOUND;
        if(session->current->references==0xFFFFFFFFu)return SC32_E_RANGE;
        handle=br_handle_new(pid,BR_SNAPSHOT);
        if(!handle)return br_handle_error();
        handle->body.clip=session->current;
        session->current->references++;
    }
    ScClipboardInfo reply;
    br_clip_info(&reply,session->current,handle?handle->id:0,session->epoch);
    error=br_publish(pid,output,&reply,sizeof(reply));
    if(error)return error;
    br_owner *owner=br_owner_get(pid,0);
    if(owner && owner->watch_session==session->id)owner->watch_epoch=session->epoch;
    return 0;
}

static int br_clip_formats(int pid,u32 request,u32 output)
{
    Sc32ClipboardFormats input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScClipboardPage));
    if(error)return error;
    if(input.reserved[0] || input.reserved[1] || input.capacity>SC32_BROKER_PAGE_MAX)return SC32_E_INVALID;
    error=br_span(pid,input.rows,input.capacity*sizeof(ScClipboardFormatInfo),1);
    if(error || br_output_overlap(pid,output,input.rows,input.capacity*sizeof(ScClipboardFormatInfo)))return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_SNAPSHOT);
    if(!handle)return SC32_E_STALE;
    br_clip *clip=handle->body.clip;
    br_session *session=br_session_get(clip->session,0);
    if(!session || session->closing || session_task(pid)!=clip->session)return SC32_E_ACCESS;
    br_format *first=clip->first;
    if(input.after==SC32_BROKER_ENUM_END)first=0;
    else if(input.after){if(input.after>0x80000000u)return SC32_E_INVALID;
        br_format *last=objpool_get(&br_formats,(int)(input.after-1u));
        if(!last || last->owner!=clip)return SC32_E_STALE;
        first=last->next;
    }
    ScClipboardPage reply={32,1,input.handle,0,input.after,clip->epoch,0,0};
    while(first && reply.count<input.capacity){ScClipboardFormatInfo row={16,1,first->format,first->blob->bytes};
        error=br_publish(pid,input.rows+reply.count*sizeof(row),&row,sizeof(row));
        if(error)return error;
        reply.count++;
        reply.cursor=(u32)first->index+1u;
        first=first->next;
    }
    if(!first){reply.cursor=SC32_BROKER_ENUM_END;
        reply.flags=SC32_CLIPBOARD_FINISHED;
    }
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_clip_read(int pid,u32 request,u32 output)
{
    Sc32ClipboardRead input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScBrokerIoResult));
    if(error)return error;
    if(input.reserved)return SC32_E_INVALID;
    if(input.bytes>SC32_BROKER_IO_MAX)return SC32_E_RANGE;
    error=br_span(pid,input.data,input.bytes,1);
    if(error || br_output_overlap(pid,output,input.data,input.bytes))return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_SNAPSHOT);
    if(!handle)return SC32_E_STALE;
    br_clip *clip=handle->body.clip;
    br_session *session=br_session_get(clip->session,0);
    if(!session || session->closing || session_task(pid)!=clip->session)return SC32_E_ACCESS;
    br_format *format=br_format_find(clip,input.format,0,0);
    if(!format)return SC32_E_NOT_FOUND;
    if(input.offset>format->blob->bytes)return SC32_E_RANGE;
    u32 bytes=format->blob->bytes-input.offset;
    if(bytes>input.bytes)bytes=input.bytes;
    error=br_blob_publish(pid,format->blob,input.offset,input.data,bytes);
    if(error)return error;
    ScBrokerIoResult reply={32,1,input.handle,bytes,format->blob->bytes,input.offset,0,0};
    if(input.offset+bytes==format->blob->bytes)reply.flags=SC32_BROKER_IO_END;
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_clip_watch(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(Sc32BrokerToken),&input);
    if(error)return error;
    if(input.handle>1 || input.value)return SC32_E_INVALID;
    ScBrokerIdentity identity;
    br_session *session;
    error=br_clip_session(pid,&identity,&session,input.handle!=0);
    if(error==SC32_E_NOT_FOUND && !input.handle){Sc32BrokerToken empty;
        br_token(&empty,0,1);
        return br_publish(pid,output,&empty,sizeof(empty));
    }
    if(error)return error;
    br_owner *owner=br_owner_get(pid,input.handle!=0);
    if(input.handle && !owner){br_session_trim(session);
        return SC32_E_MEMORY;
    }
    if(owner){br_watch_unlink(pid,owner);
        if(input.handle){owner->watch_session=session->id;
            owner->watch_epoch=session->epoch;
            owner->watch_next=session->watch_head;
            if(owner->watch_next>=0)((br_owner *)objpool_get(&br_owners,owner->watch_next))->watch_previous=pid;
            session->watch_head=pid;
    }}
    Sc32BrokerToken reply;
    br_token(&reply,0,session->epoch);
    error=br_publish(pid,output,&reply,sizeof(reply));
    if(!input.handle){br_owner_trim(pid);br_session_trim(session);}
    return error;
}

static int br_close_handle(br_handle *handle);

static int br_mapping_remove(br_handle *handle);

static void br_shared_unref(br_shared *object);

static br_endpoint *br_endpoint_node(br_avl *node)
{return node?(br_endpoint *)((u8 *)node-__builtin_offsetof(br_endpoint,tree)):0;
}

static int br_name_compare(const char *a,const char *b)
{while(*a && *a==*b){a++;
        b++;
    }
    return (int)(u8)*a-(int)(u8)*b;
}

static int br_name(int pid,u32 address,char out[64])
{
    br_zero(out,64);
    for(u32 i=0;i<64;i++){
        if(paging_user_copy(TASK(pid).pd,address+i,out+i,1,0))return SC32_E_INVALID;
        u32 c=(u8)out[i];
        if(!c){if(!i || (i==1 && out[0]=='.') || (i==2 && out[0]=='.' && out[1]=='.'))return SC32_E_INVALID;
            return 0;
        }
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.'))return SC32_E_INVALID;
    }
    return SC32_E_RANGE;
}

static br_endpoint *br_endpoint_find(u32 namespace_id,u32 scope,const char *name,br_avl ***slot,br_avl **parent)
{
    br_avl **link=&br_endpoint_root,*previous=0;
    while(*link){br_endpoint *p=br_endpoint_node(*link);
        int compare=namespace_id<p->namespace_id?-1:namespace_id>p->namespace_id?1:scope<p->scope?-1:scope>p->scope?1:br_name_compare(name,p->name);
        if(!compare)return p;
        previous=*link;
        link=compare<0?&(*link)->left:&(*link)->right;
    }
    if(slot)*slot=link;
    if(parent)*parent=previous;
    return 0;
}

static int br_acl(br_endpoint *endpoint,const ScBrokerIdentity *peer)
{
    Sc32IpcCreate *acl=&endpoint->desc;
    u32 flags=acl->acl_flags;
    int public_normal=(flags&SC32_IPC_ACL_PUBLIC_NORMAL)!=0;
    if(public_normal){session_t *s=session_get(peer->session);
        if(!s || s->kind!=SESSION_NORMAL || peer->realm!=AUTH_NORMAL || peer->uid<0 || peer->subject_uid<0)return 0;
    }
    if(flags&SC32_IPC_ACL_SESSION){if(acl->allowed_session && peer->session!=acl->allowed_session)return 0;
    }
    else if(peer->session!=endpoint->owner.session)return 0;
    if(flags&SC32_IPC_ACL_UID){if(peer->subject_uid!=acl->allowed_uid)return 0;
    }
    else if(!public_normal && (peer->uid!=endpoint->owner.uid || peer->subject_uid!=endpoint->owner.subject_uid))return 0;
    if(flags&SC32_IPC_ACL_GID){if(peer->subject_gid!=acl->allowed_gid)return 0;
    }
    else if(!public_normal && (peer->gid!=endpoint->owner.gid || peer->subject_gid!=endpoint->owner.subject_gid))return 0;
    if(flags&SC32_IPC_ACL_REALM){if(peer->realm!=acl->allowed_realm)return 0;
    }
    else if(!public_normal && peer->realm!=endpoint->owner.realm)return 0;
    return 1;
}

static u32 br_pending_charge(void){return sizeof(br_connection)+2u*sizeof(br_handle);
}

static void br_pending_detach(br_connection *connection)
{
    br_endpoint *endpoint=connection->listener;
    if(!endpoint)return;
    if(connection->previous)connection->previous->next=connection->next;
    else endpoint->first=connection->next;
    if(connection->next)connection->next->previous=connection->previous;
    else endpoint->last=connection->previous;
    endpoint->pending--;
    endpoint->pending_bytes-=br_pending_charge();
    endpoint->references--;
    endpoint->epoch++;
    connection->listener=0;
    connection->previous=connection->next=0;
    br_notify(br_handle_any(endpoint->handle));
    if(endpoint->closing)br_gc_add(&endpoint->gc,BR_GC_ENDPOINT);
}

static int br_ipc_create(int pid,u32 request,u32 output)
{
    Sc32IpcCreate input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(Sc32BrokerToken));
    if(error)return error;
    if(input.namespace_id>SC32_IPC_NAMESPACE_SYSTEM || input.flags || input.reserved[0] || input.reserved[1] || (input.acl_flags&~31u))return SC32_E_INVALID;
    ScBrokerIdentity identity;
    error=br_identity(pid,&identity);
    if(error)return error;
    if(input.session && input.session!=identity.session)return SC32_E_ACCESS;
    if(input.namespace_id==SC32_IPC_NAMESPACE_SYSTEM && !auth_can_mod(pid))return SC32_E_ACCESS;
    if(input.acl_flags&SC32_IPC_ACL_PUBLIC_NORMAL){if(input.namespace_id!=SC32_IPC_NAMESPACE_SYSTEM || !auth_can_mod(pid))return SC32_E_ACCESS;
    }
    if((input.acl_flags&SC32_IPC_ACL_SESSION) && !input.allowed_session && !(input.acl_flags&SC32_IPC_ACL_PUBLIC_NORMAL))return SC32_E_INVALID;
    if(((input.acl_flags&SC32_IPC_ACL_UID) && input.allowed_uid<-1) || ((input.acl_flags&SC32_IPC_ACL_GID) && input.allowed_gid<-1)
    || ((input.acl_flags&SC32_IPC_ACL_REALM) && input.allowed_realm>AUTH_KERNEL))return SC32_E_INVALID;
    if(!input.queue_budget_bytes)input.queue_budget_bytes=0x40000u;
    if(!input.max_frame_bytes)input.max_frame_bytes=SC32_BROKER_IO_MAX;
    if(!input.pending_budget_bytes)input.pending_budget_bytes=0x10000u;
    if(input.queue_budget_bytes<sizeof(br_message) || input.queue_budget_bytes>0x04000000u || input.max_frame_bytes>SC32_BROKER_IO_MAX
    || input.pending_budget_bytes<br_pending_charge() || input.pending_budget_bytes>0x04000000u)return SC32_E_RANGE;
    char name[64];
    error=br_name(pid,input.name,name);
    if(error)return error;
    u32 scope=input.namespace_id==SC32_IPC_NAMESPACE_SYSTEM?0:identity.session;
    br_avl **slot=0,*parent=0;
    if(br_endpoint_find(input.namespace_id,scope,name,&slot,&parent))return SC32_E_EXISTS;
    int index=objpool_alloc(&br_endpoints);
    if(index<0)return SC32_E_MEMORY;
    br_endpoint *endpoint=objpool_get(&br_endpoints,index);
    br_handle *handle=br_handle_new(pid,BR_ENDPOINT);
    if(!handle){objpool_release(&br_endpoints,index);
        return br_handle_error();
    }
    endpoint->index=index;
    endpoint->handle=handle->id;
    endpoint->namespace_id=input.namespace_id;
    endpoint->scope=scope;
    endpoint->references=1;
    endpoint->desc=input;
    endpoint->desc.name=0;
    endpoint->owner=identity;
    endpoint->epoch=1;
    br_copy(endpoint->name,name,64);
    endpoint->tree.height=1;
    endpoint->tree.parent=parent;
    *slot=&endpoint->tree;
    br_balance(&br_endpoint_root,parent);
    handle->body.endpoint=endpoint;
    Sc32BrokerToken reply;
    br_token(&reply,handle->id,SC32_IPC_STATE_CREATED);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_ipc_listen(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,0,&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_ENDPOINT);
    if(!handle)return SC32_E_STALE;
    br_endpoint *endpoint=handle->body.endpoint;
    if(endpoint->closing)return SC32_E_CANCELLED;
    if(endpoint->namespace_id==SC32_IPC_NAMESPACE_SYSTEM && !auth_can_mod(pid))return SC32_E_ACCESS;
    if(!endpoint->listening){endpoint->listening=1;
        endpoint->epoch++;
    }
    return 0;
}

/* Drop只传内核已核实的endpoint；普通named Connect共用此事务正文，
 * 因而ACL、预算、peer、Accept和OOM回滚不会分裂出第二条信任路径。 */
static int br_connect_endpoint(int pid,br_endpoint *endpoint,const ScBrokerIdentity *identity,Sc32BrokerToken *reply)
{
    ScBrokerIdentity server;
    int error=br_identity((int)endpoint->owner.pid,&server);
    if(error || !br_identity_same(&server,&endpoint->owner))return SC32_E_STALE;
    if(!br_acl(endpoint,identity))return SC32_E_ACCESS;
    if(endpoint->closing)return SC32_E_BROKEN_PIPE;
    if(!endpoint->listening)return SC32_E_AGAIN;
    if(endpoint->references==0xFFFFFFFFu || endpoint->pending==0xFFFFFFFFu)return SC32_E_RANGE;
    if(br_pending_charge()>endpoint->desc.pending_budget_bytes-endpoint->pending_bytes)return SC32_E_AGAIN;
    int index=objpool_alloc(&br_connections);
    if(index<0)return SC32_E_MEMORY;
    br_connection *connection=objpool_get(&br_connections,index);
    br_handle *client=br_handle_new(pid,BR_CHANNEL);
    if(!client){objpool_release(&br_connections,index);
        return br_handle_error();
    }
    br_handle *accepted=br_handle_new((int)server.pid,BR_CHANNEL);
    if(!accepted){br_handle_drop(client);
        objpool_release(&br_connections,index);
        return br_handle_error();
    }
    connection->index=index;
    connection->ports[0]=client->id;
    connection->ports[1]=accepted->id;
    connection->references=2;
    connection->listener=endpoint;
    connection->budget=endpoint->desc.queue_budget_bytes;
    connection->max_frame=endpoint->desc.max_frame_bytes;
    client->body.channel.connection=accepted->body.channel.connection=connection;
    accepted->body.channel.side=1;
    client->body.channel.peer=server;
    accepted->body.channel.peer=*identity;
    client->body.channel.epoch=accepted->body.channel.epoch=1;
    /* 预算/真实双方拥有者/两句柄全部取得后才入pending队列；OOM没有
    * 半个对端，也不占旧16fd。Accept前channel只可查状态，不能Send。 */
    connection->previous=endpoint->last;
    if(endpoint->last)endpoint->last->next=connection;
    else endpoint->first=connection;
    endpoint->last=connection;
    endpoint->pending++;
    endpoint->pending_bytes+=br_pending_charge();
    endpoint->references++;
    endpoint->epoch++;
    br_notify(br_handle_any(endpoint->handle));
    br_token(reply,client->id,SC32_IPC_STATE_CONNECTING);
    return 0;
}

int sc32_broker_identity(int pid,ScBrokerIdentity *out)
{return br_started && out?br_identity(pid,out):SC32_E_NOT_SUPPORTED;}
int sc32_broker_available(void){return br_started;}
int sc32_broker_drop_endpoint(int owner,u32 id,u32 session,ScBrokerIdentity *out)
{
    if(!br_started || !out)return SC32_E_NOT_SUPPORTED;
    br_handle *handle=br_handle_get(owner,id,BR_ENDPOINT);
    if(!handle)return SC32_E_STALE;
    br_endpoint *endpoint=handle->body.endpoint;
    int error=br_identity(owner,out);if(error)return error;
    if(!br_identity_same(out,&endpoint->owner) || out->session!=session)return SC32_E_STALE;
    if(endpoint->closing)return SC32_E_BROKEN_PIPE;
    if(!endpoint->listening)return SC32_E_AGAIN;
    return 0;
}
int sc32_broker_drop_connect(int source,int owner,u32 id,Sc32BrokerToken *out)
{
    ScBrokerIdentity identity,server;
    int error=br_identity(source,&identity);if(error)return error;
    error=sc32_broker_drop_endpoint(owner,id,identity.session,&server);if(error)return error;
    br_handle *handle=br_handle_get(owner,id,BR_ENDPOINT);
    return br_connect_endpoint(source,handle->body.endpoint,&identity,out);
}

static int br_ipc_connect(int pid,u32 request,u32 output)
{
    Sc32IpcConnect input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(Sc32BrokerToken));
    if(error)return error;
    if(input.namespace_id>1 || input.flags || input.reserved[0] || input.reserved[1])return SC32_E_INVALID;
    ScBrokerIdentity identity;
    error=br_identity(pid,&identity);
    if(error)return error;
    char name[64];
    error=br_name(pid,input.name,name);
    if(error)return error;
    if(input.namespace_id==SC32_IPC_NAMESPACE_SYSTEM && input.session)return SC32_E_INVALID;
    u32 scope=input.namespace_id==SC32_IPC_NAMESPACE_SYSTEM?0:input.session?input.session:identity.session;
    br_endpoint *endpoint=br_endpoint_find(input.namespace_id,scope,name,0,0);
    if(!endpoint)return SC32_E_NOT_FOUND;
    Sc32BrokerToken reply;
    error=br_connect_endpoint(pid,endpoint,&identity,&reply);
    return error?error:br_publish(pid,output,&reply,sizeof(reply));
}
static int br_ipc_accept(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(Sc32IpcAccepted),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_ENDPOINT);
    if(!handle)return SC32_E_STALE;
    br_endpoint *endpoint=handle->body.endpoint;
    if(endpoint->closing)return SC32_E_BROKEN_PIPE;
    br_connection *connection=endpoint->first;
    if(!connection)return SC32_E_AGAIN;
    br_handle *accepted=br_handle_get(pid,connection->ports[1],BR_CHANNEL),*client=br_handle_any(connection->ports[0]);
    if(!accepted || !client || client->closed)return SC32_E_STALE;
    Sc32IpcAccepted reply={64,1,accepted->id,SC32_IPC_STATE_CONNECTED,{0}};
    reply.peer=accepted->body.channel.peer;
    error=br_publish(pid,output,&reply,sizeof(reply));
    if(error)return error;
    br_pending_detach(connection);
    connection->accepted=1;
    client->body.channel.epoch++;
    accepted->body.channel.epoch++;
    br_notify(client);
    br_notify(accepted);
    return 0;
}

static br_handle *br_peer(br_handle *handle)
{
    br_connection *connection=handle->body.channel.connection;
    return connection?br_handle_any(connection->ports[1u-handle->body.channel.side]):0;
}

static int br_channel_closed(br_handle *handle)
{br_handle *peer=br_peer(handle);
    return !peer || peer->closed || !task_owned((int)peer->owner) || task_generation((int)peer->owner)!=peer->generation || TASK(peer->owner).state==2 || handle->body.channel.connection->failed;
}

static void br_message_drop(br_message *message)
{br_blob_unref(message->blob);
    objpool_release(&br_messages,message->index);
}

static void br_message_pop(br_handle *handle)
{
    br_message *message=handle->body.channel.first;
    handle->body.channel.first=message->next;
    if(!handle->body.channel.first)handle->body.channel.last=0;
    handle->body.channel.used-=message->charge;
    handle->body.channel.count--;
    handle->body.channel.epoch++;
    br_message_drop(message);
    br_handle *peer=br_peer(handle);
    if(peer){peer->body.channel.epoch++;
        br_notify(peer);
    }
}

static u32 br_channel_budget(br_handle *handle)
{
    /* 连接脱离listener后仍保存双方同一预算；将参数直接留在port，
    * 不能靠已Close的endpoint地址继续查配置。 */
    return handle->body.channel.connection->budget;
}

static int br_ipc_send(int pid,u32 request,u32 output)
{
    Sc32IpcSend input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    if(output || !input.type)return SC32_E_INVALID;
    if(input.bytes>SC32_BROKER_IO_MAX)return SC32_E_RANGE;
    error=br_span(pid,input.data,input.bytes,0);
    if(error)return error;
    br_handle *handle=br_handle_get(pid,input.handle,BR_CHANNEL);
    if(!handle)return SC32_E_STALE;
    br_connection *connection=handle->body.channel.connection;
    if(br_channel_closed(handle))return SC32_E_BROKEN_PIPE;
    if(!connection->accepted)return SC32_E_AGAIN;
    br_handle *peer=br_peer(handle);
    u32 budget=br_channel_budget(peer),maximum=connection->max_frame;
    if(input.bytes>maximum)return SC32_E_RANGE;
    u32 charge=sizeof(br_message)+input.bytes;
    if(charge>budget-peer->body.channel.used)return SC32_E_AGAIN;
    u32 side=handle->body.channel.side;
    if(connection->sequence_lo[side]==0xFFFFFFFFu && connection->sequence_hi[side]==0xFFFFFFFFu)return SC32_E_RANGE;
    ScBrokerIdentity identity;
    error=br_identity(pid,&identity);
    if(error)return error;
    int index=objpool_alloc(&br_messages);
    if(index<0)return SC32_E_MEMORY;
    br_message *message=objpool_get(&br_messages,index);
    message->index=index;
    message->charge=charge;
    if(input.bytes<=sizeof(message->small)){if(input.bytes && paging_user_copy(TASK(pid).pd,input.data,message->small,input.bytes,0)){objpool_release(&br_messages,index);
            return SC32_E_INVALID;
    }}
    else{message->blob=br_blob_from_user(pid,input.data,input.bytes);
        if(!message->blob){objpool_release(&br_messages,index);
            return SC32_E_MEMORY;
    }}
    ScIpcPacket *packet=&message->packet;
    packet->size=sizeof(*packet);
    packet->version=1;
    packet->type=input.type;
    packet->bytes=input.bytes;
    packet->flags=SC32_IPC_FRAME_DATA;
    packet->sender=identity;
    if(!++connection->sequence_lo[side])connection->sequence_hi[side]++;
    packet->sequence_lo=connection->sequence_lo[side];
    packet->sequence_hi=connection->sequence_hi[side];
    if(peer->body.channel.last)peer->body.channel.last->next=message;
    else peer->body.channel.first=message;
    peer->body.channel.last=message;
    peer->body.channel.used+=charge;
    peer->body.channel.count++;
    peer->body.channel.epoch++;
    br_notify(peer);
    return 0;
}

static int br_ipc_packet(int pid,u32 request,u32 output,int consume)
{
    Sc32IpcReceive input;
    int error;
    if(consume)error=sc32_core_input(pid,request,&input,sizeof(input));
    else{Sc32BrokerHandle local;
        error=sc32_core_input(pid,request,&local,sizeof(local));
        if(error)return error;
        br_zero(&input,sizeof(input));
        input.handle=local.handle;
        if(local.value)error=SC32_E_INVALID;
    }
    if(!error)error=sc32_core_output(pid,output,sizeof(ScIpcPacket));
    if(error)return error;
    if(input.reserved || input.bytes>SC32_BROKER_IO_MAX)return SC32_E_RANGE;
    if(consume){error=br_span(pid,input.data,input.bytes,1);
        if(error || br_output_overlap(pid,output,input.data,input.bytes))return SC32_E_INVALID;
    }
    br_handle *handle=br_handle_get(pid,input.handle,BR_CHANNEL);
    if(!handle)return SC32_E_STALE;
    br_message *message=handle->body.channel.first;
    if(!message){if(!br_channel_closed(handle))return SC32_E_AGAIN;
        ScIpcPacket eof;
        br_zero(&eof,sizeof(eof));
        eof.size=sizeof(eof);
        eof.version=1;
        eof.flags=SC32_IPC_FRAME_EOF;
        eof.sender=handle->body.channel.peer;
        return br_publish(pid,output,&eof,sizeof(eof));
    }
    if(consume && input.bytes<message->packet.bytes)return SC32_E_RANGE;
    if(consume){error=message->blob?br_blob_publish(pid,message->blob,0,input.data,message->packet.bytes):br_publish(pid,input.data,message->small,message->packet.bytes);
        if(error)return error;
    }
    error=br_publish(pid,output,&message->packet,sizeof(message->packet));
    if(error)return error;
    if(consume)br_message_pop(handle);
    return 0;
}

static int br_ipc_peer_query(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScBrokerIdentity),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_CHANNEL);
    if(!handle)return SC32_E_STALE;
    return br_publish(pid,output,&handle->body.channel.peer,sizeof(ScBrokerIdentity));
}

static void br_ipc_info(br_handle *handle,ScIpcInfo *out)
{
    br_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->handle=handle->id;
    out->kind=handle->kind==BR_ENDPOINT?1u:2u;
    if(handle->kind==BR_ENDPOINT){br_endpoint *endpoint=handle->body.endpoint;
        out->state=endpoint->listening?SC32_IPC_STATE_LISTENING:SC32_IPC_STATE_CREATED;
        out->epoch=endpoint->epoch;
        out->queued_bytes=endpoint->pending_bytes;
        out->queue_budget_bytes=endpoint->desc.pending_budget_bytes;
        out->pending_connections=endpoint->pending;
        out->max_frame_bytes=endpoint->desc.max_frame_bytes;
    }else{
        br_connection *connection=handle->body.channel.connection;
        out->state=connection->failed?SC32_IPC_STATE_CLOSED:connection->accepted?SC32_IPC_STATE_CONNECTED:SC32_IPC_STATE_CONNECTING;
        out->epoch=handle->body.channel.epoch;
        out->queued_bytes=handle->body.channel.used;
        out->queued_frames=handle->body.channel.count;
        out->queue_budget_bytes=br_channel_budget(handle);
        out->max_frame_bytes=connection->max_frame;
        out->peer_closed=br_channel_closed(handle);
    }
}

static int br_ipc_query(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScIpcInfo),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,0);
    if(!handle || (handle->kind!=BR_ENDPOINT && handle->kind!=BR_CHANNEL))return SC32_E_STALE;
    ScIpcInfo reply;
    br_ipc_info(handle,&reply);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static void br_shared_unref(br_shared *object)
{if(object && !--object->references)br_gc_add(&object->gc,BR_GC_SHARED);
}

static void br_shared_info(br_handle *handle,ScSharedInfo *out)
{
    br_shared *object=handle->body.shared.object;
    br_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->handle=handle->id;
    out->object_id=object->object;
    out->bytes=handle->body.shared.bytes;
    out->offset=handle->body.shared.offset;
    out->access=handle->body.shared.access;
    out->state=object->state;
    out->allocation_bytes=object->bytes;
    out->ready_pages=object->ready_pages;
    out->references=object->references;
    out->mappings=object->mappings;
    out->issuer_pid=object->creator.pid;
    out->issuer_generation=object->creator.generation;
}

static int br_shared_step(br_shared *object)
{
    if(object->state==SC32_SHARED_READY)return 0;
    if(object->state==SC32_SHARED_FAILED)return (int)object->error;
    for(u32 step=0;step<SC32_B_STEP_PAGES && object->ready_pages<object->pages;step++){
        if(!object->last || object->last->count==1020){
            u32 physical=pframe_alloc();
            if(!physical){object->state=SC32_SHARED_FAILED;
                object->error=(u32)SC32_E_MEMORY;
                return SC32_E_MEMORY;
            }
            br_frames *frames=(br_frames *)physical;
            br_zero(frames,sizeof(*frames));
            frames->previous=object->last;
            if(object->last)object->last->next=frames;
            else object->first=frames;
            object->last=frames;
        }
        u32 physical=pframe_alloc();
        if(!physical){object->state=SC32_SHARED_FAILED;
            object->error=(u32)SC32_E_MEMORY;
            return SC32_E_MEMORY;
        }
        /* 每页在任何用户PTE可见前全零；候选失败只有broker拥有PF，
        * 统一GC撤metadata和帧，不能让失败Create暴露旧任务残留字节。 */
        br_zero((void *)physical,PAGE_SIZE);
        object->last->frames[object->last->count++]=physical;
        object->ready_pages++;
    }
    if(object->ready_pages==object->pages){object->state=SC32_SHARED_READY;
        return 0;
    }
    return SC32_PENDING;
}

static int br_shared_create(int pid,u32 request,u32 output)
{
    ScSharedDesc input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScSharedInfo));
    if(error)return error;
    if(!input.bytes || input.bytes>SC32_B_SHARED_WINDOW_TOP-SC32_B_SHARED_WINDOW_BASE)return SC32_E_RANGE;
    if((input.access!=1 && input.access!=3) || input.flags || input.reserved[0] || input.reserved[1] || input.reserved[2])return SC32_E_INVALID;
    ScBrokerIdentity identity;
    error=br_identity(pid,&identity);
    if(error)return error;
    int index=objpool_alloc(&br_shared_objects);
    if(index<0)return SC32_E_MEMORY;
    br_shared *object=objpool_get(&br_shared_objects,index);
    br_handle *handle=br_handle_new(pid,BR_SHARED);
    if(!handle){objpool_release(&br_shared_objects,index);
        return br_handle_error();
    }
    object->index=index;
    object->object=handle->id;
    object->references=1;
    object->pages=(input.bytes+4095u)/4096u;
    object->bytes=object->pages*4096u;
    object->creator=identity;
    object->state=SC32_SHARED_PREPARING;
    handle->body.shared.object=object;
    handle->body.shared.access=input.access;
    handle->body.shared.bytes=object->bytes;
    ScSharedInfo reply;
    br_shared_info(handle,&reply);
    error=br_publish(pid,output,&reply,sizeof(reply));
    return error?error:SC32_PENDING;
}

static int br_shared_continue(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScSharedInfo),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_SHARED);
    if(!handle)return SC32_E_STALE;
    error=br_shared_step(handle->body.shared.object);
    ScSharedInfo reply;
    br_shared_info(handle,&reply);
    int publish_error=br_publish(pid,output,&reply,sizeof(reply));
    return publish_error?publish_error:error;
}

static int br_shared_query(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScSharedInfo),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_SHARED);
    if(!handle)return SC32_E_STALE;
    ScSharedInfo reply;
    br_shared_info(handle,&reply);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_shared_grant(int pid,u32 request,u32 output)
{
    Sc32SharedGrant input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScSharedGrantInfo));
    if(error)return error;
    if(input.reserved[0] || input.reserved[1] || input.reserved[2] || (input.flags&~SC32_SHARED_GRANT_CROSS_SESSION)
    || (input.access!=1 && input.access!=3) || !input.bytes || ((input.bytes|input.offset)&4095u))return SC32_E_INVALID;
    br_handle *source=br_handle_get(pid,input.handle,BR_SHARED);
    if(!source)return SC32_E_STALE;
    br_shared *object=source->body.shared.object;
    if(object->state!=SC32_SHARED_READY)return SC32_E_BUSY;
    if((input.access&source->body.shared.access)!=input.access || input.offset>source->body.shared.bytes || input.bytes>source->body.shared.bytes-input.offset)return SC32_E_ACCESS;
    if(!input.target_pid || input.target_pid>0x7FFFFFFFu || !input.target_generation)return SC32_E_INVALID;
    ScBrokerIdentity issuer,target;
    error=br_identity(pid,&issuer);
    if(!error)error=br_identity((int)input.target_pid,&target);
    if(error)return error;
    if(target.generation!=input.target_generation)return SC32_E_STALE;
    if(target.session!=issuer.session && !(input.flags&SC32_SHARED_GRANT_CROSS_SESSION))return SC32_E_ACCESS;
    if(object->references==0xFFFFFFFFu)return SC32_E_RANGE;
    br_handle *grant=br_handle_new(pid,BR_GRANT);
    if(!grant)return br_handle_error();
    object->references++;
    grant->body.grant.object=object;
    grant->body.grant.access=input.access;
    grant->body.grant.offset=source->body.shared.offset+input.offset;
    grant->body.grant.bytes=input.bytes;
    grant->body.grant.flags=input.flags;
    grant->body.grant.target=target;
    ScSharedGrantInfo reply={32,1,grant->id,target.pid,target.generation,input.access,grant->body.grant.offset,input.bytes};
    return br_publish(pid,output,&reply,sizeof(reply));
}

static int br_shared_open(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScSharedInfo),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *grant=br_handle_any(input.handle);
    if(!grant || grant->closed || grant->kind!=BR_GRANT || !task_owned((int)grant->owner) || TASK(grant->owner).state==2 || task_generation((int)grant->owner)!=grant->generation)return SC32_E_STALE;
    ScBrokerIdentity identity;
    error=br_identity(pid,&identity);
    if(error)return error;
    if(!br_identity_same(&identity,&grant->body.grant.target))return SC32_E_ACCESS;
    br_shared *object=grant->body.grant.object;
    if(object->references==0xFFFFFFFFu)return SC32_E_RANGE;
    br_handle *handle=br_handle_new(pid,BR_SHARED);
    if(!handle)return br_handle_error();
    object->references++;
    handle->body.shared.object=object;
    handle->body.shared.access=grant->body.grant.access;
    handle->body.shared.offset=grant->body.grant.offset;
    handle->body.shared.bytes=grant->body.grant.bytes;
    ScSharedInfo reply;
    br_shared_info(handle,&reply);
    return br_publish(pid,output,&reply,sizeof(reply));
}

static void br_mapping_info(br_handle *handle,ScSharedMappingInfo *out)
{
    br_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->handle=handle->id;
    out->object_id=handle->body.mapping.object->object;
    out->address=handle->body.mapping.state==SC32_SHARED_READY?handle->body.mapping.base:0;
    out->bytes=handle->body.mapping.bytes;
    out->offset=handle->body.mapping.offset;
    out->access=handle->body.mapping.access;
    out->state=handle->body.mapping.state;
    out->epoch=handle->body.mapping.epoch;
}

static int br_shared_map(int pid,u32 request,u32 output)
{
    Sc32SharedMap input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(!error)error=sc32_core_output(pid,output,sizeof(ScSharedMappingInfo));
    if(error)return error;
    if(input.reserved[0] || input.reserved[1] || (input.access!=1 && input.access!=3) || !input.bytes || ((input.bytes|input.offset)&4095u))return SC32_E_INVALID;
    br_handle *source=br_handle_get(pid,input.handle,BR_SHARED);
    if(!source)return SC32_E_STALE;
    br_shared *object=source->body.shared.object;
    if(object->state!=SC32_SHARED_READY)return SC32_E_BUSY;
    if((input.access&source->body.shared.access)!=input.access || input.offset>source->body.shared.bytes || input.bytes>source->body.shared.bytes-input.offset)return SC32_E_ACCESS;
    if(object->references==0xFFFFFFFFu || object->mappings==0xFFFFFFFFu)return SC32_E_RANGE;
    br_handle *mapping=br_handle_new(pid,BR_MAPPING);
    if(!mapping)return br_handle_error();
    u32 base=paging_shared_reserve(TASK(pid).pd,input.bytes);
    if(!base){br_handle_drop(mapping);
        return SC32_E_MEMORY;
    }
    object->references++;
    object->mappings++;
    mapping->body.mapping.object=object;
    mapping->body.mapping.pd=TASK(pid).pd;
    mapping->body.mapping.base=base;
    mapping->body.mapping.bytes=input.bytes;
    mapping->body.mapping.offset=source->body.shared.offset+input.offset;
    mapping->body.mapping.access=input.access;
    mapping->body.mapping.state=SC32_SHARED_PREPARING;
    mapping->body.mapping.epoch=1;
    ScSharedMappingInfo reply;
    br_mapping_info(mapping,&reply);
    error=br_publish(pid,output,&reply,sizeof(reply));
    return error?error:SC32_PENDING;
}

static int br_shared_frames(br_shared *object,u32 first,u32 count,u32 out[SC32_B_STEP_PAGES])
{
    if(count>SC32_B_STEP_PAGES || first>object->ready_pages || count>object->ready_pages-first)return SC32_E_FAILED;
    br_frames *block=object->first;
    while(block && first>=block->count){first-=block->count;
        block=block->next;
    }
    for(u32 i=0;i<count;i++){
        if(!block)return SC32_E_FAILED;
        if(first==block->count){block=block->next;
            first=0;
            if(!block)return SC32_E_FAILED;
        }
        out[i]=block->frames[first++];
    }
    return 0;
}

static int br_mapping_continue(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScSharedMappingInfo),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,BR_MAPPING);
    if(!handle)return SC32_E_STALE;
    if(handle->body.mapping.removed)return SC32_E_BUSY;
    if(handle->body.mapping.state==SC32_SHARED_FAILED)error=(int)handle->body.mapping.error;
    else if(handle->body.mapping.state!=SC32_SHARED_READY){
        u32 count=handle->body.mapping.bytes/PAGE_SIZE-handle->body.mapping.bound;
        if(count>SC32_B_STEP_PAGES)count=SC32_B_STEP_PAGES;
        u32 frames[SC32_B_STEP_PAGES];
        error=br_shared_frames(handle->body.mapping.object,handle->body.mapping.offset/PAGE_SIZE+handle->body.mapping.bound,count,frames);
        if(!error && paging_shared_bind(handle->body.mapping.pd,handle->body.mapping.base,handle->body.mapping.bound,frames,count,handle->body.mapping.access))error=SC32_E_FAILED;
        if(error){handle->body.mapping.state=SC32_SHARED_FAILED;
            handle->body.mapping.error=(u32)error;
        }
        else{handle->body.mapping.bound+=count;
            if(handle->body.mapping.bound==handle->body.mapping.bytes/PAGE_SIZE)handle->body.mapping.state=SC32_SHARED_READY;
            else error=SC32_PENDING;
        }
    }
    ScSharedMappingInfo reply;
    br_mapping_info(handle,&reply);
    int publish_error=br_publish(pid,output,&reply,sizeof(reply));
    return publish_error?publish_error:error;
}

static int br_mapping_remove(br_handle *handle)
{
    u32 total=handle->body.mapping.bytes/PAGE_SIZE,count=total-handle->body.mapping.removed;
    if(count>SC32_B_STEP_PAGES)count=SC32_B_STEP_PAGES;
    if(count && paging_shared_unbind(handle->body.mapping.pd,handle->body.mapping.base,handle->body.mapping.removed,count))return SC32_E_FAILED;
    handle->body.mapping.removed+=count;
    if(handle->body.mapping.removed!=total)return SC32_PENDING;
    if(paging_shared_release(handle->body.mapping.pd,handle->body.mapping.base,handle->body.mapping.bytes))return SC32_E_FAILED;
    br_shared *object=handle->body.mapping.object;
    object->mappings--;
    br_shared_unref(object);
    br_handle_drop(handle);
    return 0;
}

static int br_close_handle(br_handle *handle)
{
    if(handle->kind==BR_MAPPING)return br_mapping_remove(handle);
    if(handle->kind==BR_OFFER){br_offer_drop(handle);
        return 0;
    }
    if(handle->kind==BR_CANDIDATE || handle->kind==BR_SNAPSHOT){
        if(handle->kind==BR_CANDIDATE){br_session *session=br_session_get(handle->body.clip->session,0);
            if(session){session->candidates--;
                br_session_trim(session);
        }}
        br_clip_unref(handle->body.clip);
    }
    else if(handle->kind==BR_SHARED)br_shared_unref(handle->body.shared.object);
    else if(handle->kind==BR_GRANT)br_shared_unref(handle->body.grant.object);
    else if(handle->kind==BR_ENDPOINT){
        br_endpoint *endpoint=handle->body.endpoint;
        endpoint->closing=1;
        endpoint->listening=0;
        endpoint->handle=0;
        endpoint->references--;
        br_tree_remove(&br_endpoint_root,&endpoint->tree);
        br_gc_add(&endpoint->gc,BR_GC_ENDPOINT);
    }else if(handle->kind==BR_CHANNEL){
        handle->closed=1;
        br_handle_unlink(handle);
        br_connection *connection=handle->body.channel.connection;
        br_handle *peer=br_peer(handle);
        if(peer){peer->body.channel.epoch++;
            br_notify(peer);
        }
        if(!connection->accepted){connection->failed=1;
            br_pending_detach(connection);
            if(handle->body.channel.side==0 && peer && !peer->closed)(void)br_close_handle(peer);
        }
        br_gc_add(&handle->gc,BR_GC_CHANNEL);
        br_owner_trim((int)handle->owner);
        return 0;
    }else return SC32_E_STALE;
    br_handle_drop(handle);
    return 0;
}

#include "broker_shell.inc"

static int br_close(int pid,u32 request,u32 output,u32 kind)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,0,&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    br_handle *handle=br_handle_get(pid,input.handle,kind);
    if(!handle || (!kind && handle->kind!=BR_ENDPOINT && handle->kind!=BR_CHANNEL))return SC32_E_STALE;
    return br_close_handle(handle);
}

static int br_ready_value(int pid,u32 id,ScBrokerReadyInfo *out)
{
    br_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->handle=id;
    if(!id){
        br_owner *owner=br_owner_get(pid,0);
        if(!owner || !owner->watch_session)return 0;
        br_session *session=br_session_get(owner->watch_session,0);
        if(!session || session->closing || session_task(pid)!=session->id)return SC32_E_ACCESS;
        out->epoch=session->epoch;
        if(out->epoch!=owner->watch_epoch)out->flags=SC32_BROKER_READY_READ;
        return 0;
    }
    br_handle *handle=br_handle_get(pid,id,0);
    if(!handle)return SC32_E_STALE;
    if(handle->kind==BR_ENDPOINT){br_endpoint *endpoint=handle->body.endpoint;
        out->epoch=endpoint->epoch;
        out->read_bytes=endpoint->pending;
        if(endpoint->first)out->flags=SC32_BROKER_READY_ACCEPT;
        return 0;
    }
    if(handle->kind!=BR_CHANNEL)return SC32_E_INVALID;
    out->epoch=handle->body.channel.epoch;
    br_message *message=handle->body.channel.first;
    if(message){out->flags|=SC32_BROKER_READY_READ;
        out->read_bytes=message->packet.bytes;
    }
    if(br_channel_closed(handle)){out->flags|=SC32_BROKER_READY_READ|SC32_BROKER_READY_CLOSED;
        return 0;
    }
    if(handle->body.channel.connection->accepted){br_handle *peer=br_peer(handle);
        u32 budget=br_channel_budget(peer),available=budget-peer->body.channel.used;
        if(available>=sizeof(br_message)){out->flags|=SC32_BROKER_READY_WRITE;
            out->write_bytes=available-sizeof(br_message);
            if(out->write_bytes>handle->body.channel.connection->max_frame)out->write_bytes=handle->body.channel.connection->max_frame;
    }}
    return 0;
}

static int br_ready(int pid,u32 request,u32 output)
{
    Sc32BrokerHandle input;
    int error=br_handle_input(pid,request,output,sizeof(ScBrokerReadyInfo),&input);
    if(error)return error;
    if(input.value)return SC32_E_INVALID;
    ScBrokerReadyInfo reply;
    error=br_ready_value(pid,input.handle,&reply);
    return error?error:br_publish(pid,output,&reply,sizeof(reply));
}

int sc32_broker_wait_check(int pid,u32 request,u32 output,u32 *timeout,u32 *mask,u32 *events)
{
    if(!br_started)return SC32_E_NOT_SUPPORTED;
    if(output || !timeout || !mask || !events)return SC32_E_INVALID;
    ScBrokerWaitDesc input;
    int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    if(input.reserved || (input.ready_flags&~15u) || (input.event_mask&~255u) || input.minimum_write_bytes>SC32_BROKER_IO_MAX
    || (input.timeout>0x7FFFFFFFu && input.timeout!=SC32_INFINITE) || (!input.ready_flags && !input.event_mask))return SC32_E_INVALID;
    ScBrokerReadyInfo ready;
    error=br_ready_value(pid,input.handle,&ready);
    if(error)return error;
    u32 flags=ready.flags&input.ready_flags;
    if((flags&SC32_BROKER_READY_WRITE) && ready.write_bytes<input.minimum_write_bytes)flags&=~SC32_BROKER_READY_WRITE;
    *events=flags?TASK_EVENT_STREAM:0;
    /* 混合等待读取实际GUI队列，即使早先的INPUT提示已被消费，也
    * 不会带着仍待分发的消息无限睡眠。仅作peek，不夺走GUI消息。 */
    if((input.event_mask&TASK_EVENT_INPUT) && sc32_gui_wait_result(pid,0)==SC32_OK)*events|=TASK_EVENT_INPUT;
    *events|=task_record(pid)->events&input.event_mask;
    *timeout=input.timeout;
    *mask=input.event_mask|(input.ready_flags?TASK_EVENT_STREAM:0);
    if(*events)return 0;
    if(!input.timeout){*events=TASK_EVENT_TIMEOUT;
        return 0;
    }
    return SC32_PENDING;
}

int sc32_broker_init(void)
{
    if(br_started)return 0;
    if(objpool_init(&br_owners,sizeof(br_owner),0) || objpool_init(&br_sessions,sizeof(br_session),0) || objpool_init(&br_handles,sizeof(br_handle),0)
    || objpool_init(&br_blobs,sizeof(br_blob),0) || objpool_init(&br_clips,sizeof(br_clip),0) || objpool_init(&br_formats,sizeof(br_format),0)
    || objpool_init(&br_endpoints,sizeof(br_endpoint),0) || objpool_init(&br_connections,sizeof(br_connection),0) || objpool_init(&br_messages,sizeof(br_message),0)
    || objpool_init(&br_shared_objects,sizeof(br_shared),0) || objpool_init(&br_offers,sizeof(br_offer),0))return SC32_E_MEMORY;
    br_started=1;
    return 0;
}

int sc32_broker_call(int pid,u32 operation,u32 request,u32 output)
{
    if(!br_started)return SC32_E_NOT_SUPPORTED;
    switch(operation){
        case SC32_B_CLIP_BEGIN:return br_clip_begin(pid,request,output);
        case SC32_B_CLIP_OFFER:return br_clip_offer(pid,request,output);
        case SC32_B_CLIP_COMMIT:return br_clip_commit(pid,request,output,0);
        case SC32_B_CLIP_ABORT:return br_close(pid,request,output,BR_CANDIDATE);
        case SC32_B_CLIP_OPEN:return br_clip_current(pid,request,output,1);
        case SC32_B_CLIP_FORMATS:return br_clip_formats(pid,request,output);
        case SC32_B_CLIP_READ:return br_clip_read(pid,request,output);
        case SC32_B_CLIP_CLOSE:return br_close(pid,request,output,BR_SNAPSHOT);
        case SC32_B_CLIP_CLEAR:return br_clip_commit(pid,request,output,1);
        case SC32_B_CLIP_QUERY:return br_clip_current(pid,request,output,0);
        case SC32_B_CLIP_WATCH:return br_clip_watch(pid,request,output);
        case SC32_B_CLIP_OFFER_BEGIN:return br_offer_begin(pid,request,output);
        case SC32_B_CLIP_OFFER_WRITE:return br_offer_write(pid,request,output);
        case SC32_B_CLIP_OFFER_FINISH:return br_offer_finish(pid,request,output);
        case SC32_B_CLIP_OFFER_ABORT:return br_close(pid,request,output,BR_OFFER);
        case SC32_B_IPC_CREATE:return br_ipc_create(pid,request,output);
        case SC32_B_IPC_LISTEN:return br_ipc_listen(pid,request,output);
        case SC32_B_IPC_CONNECT:return br_ipc_connect(pid,request,output);
        case SC32_B_IPC_ACCEPT:return br_ipc_accept(pid,request,output);
        case SC32_B_IPC_SEND:return br_ipc_send(pid,request,output);
        case SC32_B_IPC_PEEK:return br_ipc_packet(pid,request,output,0);
        case SC32_B_IPC_RECEIVE:return br_ipc_packet(pid,request,output,1);
        case SC32_B_IPC_PEER:return br_ipc_peer_query(pid,request,output);
        case SC32_B_IPC_QUERY:return br_ipc_query(pid,request,output);
        case SC32_B_IPC_CLOSE:return br_close(pid,request,output,0);
        case SC32_B_READY:return br_ready(pid,request,output);
        case SC32_B_SHARED_CREATE:return br_shared_create(pid,request,output);
        case SC32_B_SHARED_STEP:return br_shared_continue(pid,request,output);
        case SC32_B_SHARED_GRANT:return br_shared_grant(pid,request,output);
        case SC32_B_SHARED_OPEN:return br_shared_open(pid,request,output);
        case SC32_B_SHARED_REVOKE:return br_close(pid,request,output,BR_GRANT);
        case SC32_B_SHARED_MAP:return br_shared_map(pid,request,output);
        case SC32_B_SHARED_MAP_STEP:return br_mapping_continue(pid,request,output);
        case SC32_B_SHARED_UNMAP:return br_close(pid,request,output,BR_MAPPING);
        case SC32_B_SHARED_QUERY:return br_shared_query(pid,request,output);
        case SC32_B_SHARED_CLOSE:return br_close(pid,request,output,BR_SHARED);
        default:return SC32_E_NOT_SUPPORTED;
    }
}

int sc32_broker_stop_step(int pid)
{
    br_owner *owner=objpool_get(&br_owners,pid);
    if(!owner)return 1;
    /* PID复用前必须完成本代清理；错代不能借旧pd继续拆新任务的PTE。 */
    if(owner->generation!=task_generation(pid))return 0;
    owner->stopping=1;
    br_watch_unlink(pid,owner);
    for(u32 budget=0;budget<32;budget++){
        owner=objpool_get(&br_owners,pid);
        if(!owner)return 1;
        u32 head=owner->ipc_head?owner->ipc_head:owner->head?owner->head:owner->mapping_head;
        if(!head){br_owner_trim(pid);return 1;}
        br_handle *handle=br_handle_any(head);
        if(!handle)return 0;
        int mapped=handle->kind==BR_MAPPING;
        int error=br_close_handle(handle);
        if(error || mapped)return !objpool_get(&br_owners,pid);
    }
    return 0;
}

void sc32_broker_session_discard(u32 id)
{
    br_session *session=br_session_get(id,0);
    if(!session)return;
    session->closing=1;
    br_clip_unref(session->current);
    session->current=0;
    br_clip_changed(session);
    br_session_trim(session);
}

static int br_gc_step(br_gc *gc,u32 *page_budget)
{
    if(gc->kind==BR_GC_BLOB){br_blob *blob=(br_blob *)gc;
        while(blob->head && *page_budget){br_blob_page *page=(br_blob_page *)blob->head;
            blob->head=page->next;
            pframe_free((u32)page);
            (*page_budget)--;
        }
        if(blob->head)return 0;
        objpool_release(&br_blobs,blob->index);
        return 1;
    }
    if(gc->kind==BR_GC_CLIP){br_clip *clip=(br_clip *)gc;
        if(clip->first){br_format *format=clip->first;
            clip->first=format->next;
            br_blob_unref(format->blob);
            objpool_release(&br_formats,format->index);
            return 0;
        }
        objpool_release(&br_clips,clip->index);
        return 1;
    }
    if(gc->kind==BR_GC_ENDPOINT){br_endpoint *endpoint=(br_endpoint *)gc;
        if(endpoint->first){br_handle *server=br_handle_any(endpoint->first->ports[1]);
            if(server && !server->closed)(void)br_close_handle(server);
            else br_pending_detach(endpoint->first);
            return 0;
        }
        if(endpoint->references)return 0;
        objpool_release(&br_endpoints,endpoint->index);
        return 1;
    }
    if(gc->kind==BR_GC_CHANNEL){br_handle *handle=(br_handle *)gc;
        if(handle->body.channel.first){br_message_pop(handle);
            return 0;
        }
        br_connection *connection=handle->body.channel.connection;
        connection->ports[handle->body.channel.side]=0;
        if(!--connection->references)objpool_release(&br_connections,connection->index);
        objpool_release(&br_handles,(int)handle->id);
        return 1;
    }
    if(gc->kind==BR_GC_SHARED){br_shared *object=(br_shared *)gc;
        while(object->first && *page_budget){br_frames *block=object->first;
            if(block->count){pframe_free(block->frames[--block->count]);
                (*page_budget)--;
            }
            else{object->first=block->next;
                if(object->first)object->first->previous=0;
                pframe_free((u32)block);
                (*page_budget)--;
        }}
        if(object->first)return 0;
        objpool_release(&br_shared_objects,object->index);
        return 1;
    }
    return 1;
}

void sc32_broker_poll(void)
{
    u32 pages=SC32_B_STEP_PAGES;
    for(u32 budget=0;budget<32 && br_garbage_first;budget++){
        br_gc *gc=br_garbage_first,*next=gc->next;
        if(br_gc_step(gc,&pages)){br_garbage_first=next;
            if(!next)br_garbage_last=0;
        }
        else{br_garbage_first=gc->next;
            gc->next=0;
            if(br_garbage_first){br_garbage_last->next=gc;
                br_garbage_last=gc;
            }else br_garbage_first=br_garbage_last=gc;
        }
        if(!pages)break;
    }
    for(u32 budget=0;budget<32 && br_notify_first;budget++){
        br_session *session=br_notify_first;
        if(session->notify_cursor>=0){int pid=session->notify_cursor;
            br_owner *owner=objpool_get(&br_owners,pid);
            session->notify_cursor=owner?owner->watch_next:-1;
            if(owner && owner->watch_session==session->id)task_notify_generation(pid,owner->generation,TASK_EVENT_STREAM);
            continue;
        }
        if(!session->closing && session->notify_epoch!=session->epoch){session->notify_epoch=session->epoch;
            session->notify_cursor=session->watch_head;
            continue;
        }
        br_notify_first=session->notify_next;
        if(!br_notify_first)br_notify_last=0;
        session->notify_queued=0;
        session->notify_next=0;
        if(session->closing)br_session_trim(session);
    }
    if(br_garbage_first || br_notify_first)task_kernel_wake();
}

int sc32_broker_pending(void){return br_garbage_first || br_notify_first;
}
