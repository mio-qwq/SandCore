/* M10a1：磁盘主核之外的常驻扩展。CORE只自动执行签名SKM2；MOD保留
 * 原SKM1与外部SYSTEM管理合同。对象、辅助页、注册项按真实资源增长，
 * 不拿旧32个模块/64次分配/1MiB区域再成为多任务的间接门槛。
 * 签名是授权，零环不是沙箱；错误入口仍进入CPL0诊断，不能热卸载。 */
#include "module.h"
#include "core_signature.h"
#include "crypto.h"
#include "crc.h"
#include "fs.h"
#include "gfx.h"
#include "wm.h"
#include "memory.h"
#include "auth.h"
#include "task.h"
#include "timer.h"
#include "serial.h"
#include "simd.h"
#include "objpool.h"

typedef struct {
    char name[64];u32 base,pages,bytes,number,state,allocation_head,callback_head;
} resident_t;
typedef struct {u32 address,pages,owner,previous,next;} allocation_t;
typedef struct {
    u32 token,owner,kind,line,previous,next,owner_previous,owner_next,period,due,pending;
    void *function,*context;
} callback_t;
typedef struct {char name[64];u32 number,generation,conflict;u8 digest[32];} candidate_t;
typedef struct {u8 *data;u32 bytes,pages,image,bss,entry,relocations,offset,number;} image_t;
static objpool_t residents,allocations,callbacks;
static u32 active_owner,scene_owner,resident_count,resident_bytes,callback_counter,irq_depth;
static int ephemeral,registration_failed,busy,service_cursor=-1;
static u32 irq_heads[16],service_head,last_service_tick;
static void (*scene_callback)(void);
u32 modules_loaded,modules_failed;

static int same(const char *a,const char *b)
{while(*a && *b){char x=*a++,y=*b++;if(x>='a' && x<='z')x-=32;if(y>='a' && y<='z')y-=32;if(x!=y)return 0;}return !*a && !*b;}
static int prefix(const char *name,const char *head)
{while(*head){char c=*name++;if(c>='a' && c<='z')c-=32;if(c!=*head++)return 0;}return 1;}
static int extension_name(const char *name)
{
    if(!prefix(name,"SYS/CORE/") || same(name,"SYS/CORE/CORE.SKM"))return 0;
    u32 n=9;while(name[n]){if(name[n]=='/')return 0;n++;}
    return n>=13 && same(name+n-4,".SKM");
}
static resident_t *resident(u32 owner)
{return owner?objpool_get(&residents,(int)(owner-1)):0;}
static void log_result(const char *path,const char *state,int error)
{
    char line[116];u32 n=0;const char *prefix_text="CORE EXT ";while(*prefix_text)line[n++]=*prefix_text++;
    while(*path && n<80)line[n++]=*path++;line[n++]=' ';while(*state && n<100)line[n++]=*state++;
    if(error){line[n++]=' ';u32 magnitude=error<0?0u-(u32)error:(u32)error;
        if(error<0)line[n++]='-';char digits[12];u32 used=0;
        do{digits[used++]=(char)('0'+magnitude%10);magnitude/=10;}while(magnitude);
        while(used && n<113)line[n++]=digits[--used];}
    line[n++]='\r';line[n++]='\n';u32 sent=serial_write(SERIAL_DEBUG,(const u8 *)line,n);
    while(sent<n){if(serial_emergency_put(SERIAL_DEBUG,(u8)line[sent])<0)break;sent++;}
}
static void *allocate_bytes(u32 bytes)
{
    resident_t *r=resident(active_owner);if(!r || irq_depth || !bytes || bytes>CORE_EXTENSION_MEMORY_MAX)return 0;
    int id=objpool_alloc(&allocations);if(id<0)return 0;
    allocation_t *a=objpool_get(&allocations,id);u32 pages=(bytes+4095)/4096,address=pframe_alloc_run(pages);
    if(!address){objpool_release(&allocations,id);return 0;}
    a->address=address;a->pages=pages;a->owner=active_owner;a->next=r->allocation_head;
    if(a->next)((allocation_t *)objpool_get(&allocations,(int)a->next-1))->previous=(u32)id+1;
    r->allocation_head=(u32)id+1;crypto_zero((void *)address,pages*4096);return (void *)address;
}
static void allocation_release(u32 link)
{
    allocation_t *a=objpool_get(&allocations,(int)link-1);resident_t *r=resident(a->owner);
    if(a->previous)((allocation_t *)objpool_get(&allocations,(int)a->previous-1))->next=a->next;
    else if(r)r->allocation_head=a->next;
    if(a->next)((allocation_t *)objpool_get(&allocations,(int)a->next-1))->previous=a->previous;
    pframe_free_run(a->address,a->pages);objpool_release(&allocations,(int)link-1);
}
static void release_bytes(void *address,u32 bytes)
{
    resident_t *r=resident(active_owner);if(!r || irq_depth || !bytes || bytes>CORE_EXTENSION_MEMORY_MAX)return;
    for(u32 link=r->allocation_head;link;){allocation_t *a=objpool_get(&allocations,(int)link-1);
        if(a->address==(u32)address && a->pages==(bytes+4095)/4096){allocation_release(link);return;}link=a->next;}
}
static int function_owned(resident_t *r,void *function)
{return r && (u32)function>=r->base && (u32)function-r->base<r->bytes;}
static int callback_register(u32 kind,u32 line,void *function,void *context,u32 period)
{
    resident_t *r=resident(active_owner);
    if(ephemeral || !function_owned(r,function) || irq_depth || (kind==1 && line>=16)
        || period>0x7FFFFFFFu || callback_counter==0x7FFFFFFFu){registration_failed=1;return -5;}
    u32 token=callback_counter+1;int id=(int)(token-1);
    if(objpool_claim(&callbacks,id)){registration_failed=1;return -4;}
    callback_counter=token;callback_t *c=objpool_get(&callbacks,id);
    c->token=token;c->owner=active_owner;c->kind=kind;c->line=line;c->function=function;c->context=context;
    c->period=period;c->due=sc_ticks+period;c->pending=kind==2 && !period;
    u32 *head=kind==1?irq_heads+line:&service_head;c->next=*head;
    if(c->next)((callback_t *)objpool_get(&callbacks,(int)c->next-1))->previous=token;*head=token;
    c->owner_next=r->callback_head;
    if(c->owner_next)((callback_t *)objpool_get(&callbacks,(int)c->owner_next-1))->owner_previous=token;
    r->callback_head=token;return (int)token;
}
static int irq_register(u32 line,void (*function)(u32,void *),void *context)
{return callback_register(1,line,(void *)function,context,0);}
static int service_register(void (*function)(void *),void *context,u32 period)
{return callback_register(2,0,(void *)function,context,period);}
static int service_wake(int token)
{
    callback_t *c=token>0?objpool_get(&callbacks,token-1):0;
    if(!c || c->token!=(u32)token || c->owner!=active_owner || c->kind!=2)return -5;
    c->pending=1;task_kernel_wake();return 0;
}
static void callback_release(u32 token)
{
    callback_t *c=objpool_get(&callbacks,(int)token-1);resident_t *r=resident(c->owner);
    u32 *head=c->kind==1?irq_heads+c->line:&service_head;
    if(c->previous)((callback_t *)objpool_get(&callbacks,(int)c->previous-1))->next=c->next;else *head=c->next;
    if(c->next)((callback_t *)objpool_get(&callbacks,(int)c->next-1))->previous=c->previous;
    if(c->owner_previous)((callback_t *)objpool_get(&callbacks,(int)c->owner_previous-1))->owner_next=c->owner_next;
    else if(r)r->callback_head=c->owner_next;
    if(c->owner_next)((callback_t *)objpool_get(&callbacks,(int)c->owner_next-1))->owner_previous=c->owner_previous;
    if(service_cursor==(int)token)service_cursor=(int)c->previous;
    objpool_release(&callbacks,(int)token-1);
}
static void reclaim(u32 owner)
{
    resident_t *r=resident(owner);if(!r)return;
    while(r->callback_head)callback_release(r->callback_head);
    while(r->allocation_head)allocation_release(r->allocation_head);
}
static void install_scene(void (*paint)(void))
{
    if(ephemeral || !function_owned(resident(active_owner),(void *)paint))registration_failed=1;
    else {scene_callback=paint;scene_owner=active_owner;}
}
static int screen_width(void){return GFX_W;}
static int screen_height(void){return GFX_H;}
static u32 ticks(void){return sc_ticks;}
static const module_api_t api={1,sizeof(module_api_t),gfx_pset,gfx_fill,gfx_circle,wm_wallpaper,install_scene,
    screen_width,screen_height,allocate_bytes,release_bytes,ticks,fs_read_at,fs_write,
    irq_register,service_register,service_wake,inb,outb,inw,outw,inl,outl};

static void image_release(image_t *s)
{if(s->data)pframe_free_run((u32)s->data,s->pages);crypto_zero(s,sizeof(*s));}
static int relocations_valid(const image_t *s)
{
    const u8 *payload=s->data+s->offset;const u32 *fixes=(const u32 *)(payload+s->image);
    for(u32 i=0;i<s->relocations;i++){
        if(s->image<4 || fixes[i]>s->image-4 || (i && fixes[i]<fixes[i-1]+4))return 0;
        u32 value=*(const u32 *)(payload+fixes[i]);if(value>=s->image+s->bss)return 0;
    }return 1;
}
static int image_read(const char *name,int signed_core,image_t *s)
{
    crypto_zero(s,sizeof(*s));u32 info[2];if(fs_stat(name,info) || info[0]!=1)return -1;
    u32 limit=signed_core?CORE_IMAGE_HEADER+CORE_IMAGE_MAX*2+CORE_SIGNATURE_BYTES:32+131072+8192*4;
    if(info[1]<(signed_core?CORE_IMAGE_HEADER+CORE_SIGNATURE_BYTES:32) || info[1]>limit)return -1;
    if(signed_core && !core_signature_available())return -7;
    s->bytes=info[1];s->pages=(s->bytes+4095)/4096;s->data=(u8 *)pframe_alloc_run(s->pages);if(!s->data)return -4;
    int result=-1;if(fs_read(name,s->data,s->bytes)!=(int)s->bytes)goto failed;
    if(signed_core){
        core_image_t *h=(core_image_t *)s->data;const char *magic="SKM2MIO";
        for(u32 i=0;i<8;i++)if(h->magic[i]!=(u8)magic[i])goto failed;
        if(h->version!=2 || h->header_bytes!=CORE_IMAGE_HEADER || h->kind!=CORE_IMAGE_EXTENSION || h->flags
            || !h->number || h->number>0x7FFFFFFFu || h->abi_min!=1 || h->abi_max!=1 || h->load_base
            || !h->image_bytes || h->image_bytes>CORE_IMAGE_MAX || h->entry_offset>=h->image_bytes
            || h->bss_offset!=h->image_bytes || h->bss_bytes>CORE_EXTENSION_MEMORY_MAX-h->image_bytes
            || h->memory_bytes!=h->image_bytes+h->bss_bytes || h->relocations>h->image_bytes/4
            || h->required_cpu&~CORE_EXTENSION_CPU_SSE2 || (h->required_cpu && !simd_sse2())
            || h->file_bytes!=s->bytes || s->bytes!=CORE_IMAGE_HEADER+h->image_bytes+h->relocations*4+CORE_SIGNATURE_BYTES
            || h->header_crc!=crc32_update(0,h,124))goto failed;
        for(u32 i=0;i<5;i++)if(h->reserved[i])goto failed;
        u8 digest[32];sha256(s->data+CORE_IMAGE_HEADER,h->image_bytes+h->relocations*4,digest);
        if(!crypto_equal(digest,h->sha256,32))goto failed;
        if(core_signature_check(s->data,s->bytes)){result=-8;goto failed;}
        s->image=h->image_bytes;s->bss=h->bss_bytes;s->entry=h->entry_offset;s->relocations=h->relocations;
        s->offset=CORE_IMAGE_HEADER;s->number=h->number;
    }else {
        const char *magic="SKM1MIO";for(u32 i=0;i<8;i++)if(s->data[i]!=(u8)magic[i])goto failed;
        u32 *h=(u32 *)(s->data+8);s->entry=h[0];s->image=h[1];s->bss=h[2];s->relocations=h[3];s->offset=32;
        if(!s->image || s->image>131072 || s->bss>131072 || s->entry>=s->image || s->relocations>8192
            || h[4]!=1 || h[5]!=0x004F494Du || s->bytes!=32+s->image+s->relocations*4)goto failed;
    }
    if(!relocations_valid(s))goto failed;return 0;
failed:
    image_release(s);return result;
}
static int load(const char *name,int keep,int signed_core,const candidate_t *expected)
{
    if(busy || irq_depth)return -6;busy=1;image_t image;int result=image_read(name,signed_core,&image);
    if(result){busy=0;return result;}
    if(expected){u8 digest[32];u32 meta[8];sha256(image.data,image.bytes,digest);
        if(image.number!=expected->number || fs_metadata(name,meta) || meta[7]!=expected->generation
            || !crypto_equal(digest,expected->digest,32)){image_release(&image);busy=0;return -9;}}
    int id=objpool_alloc(&residents);if(id<0){image_release(&image);busy=0;return -4;}
    resident_t *r=objpool_get(&residents,id);u32 pages=(image.image+image.bss+4095)/4096;
    r->base=pframe_alloc_run(pages);if(!r->base){objpool_release(&residents,id);image_release(&image);busy=0;return -4;}
    r->pages=pages;r->bytes=image.image;r->number=image.number;r->state=1;
    u32 n=0;while(name[n] && n<63){r->name[n]=name[n];n++;}r->name[n]=0;
    u8 *target=(u8 *)r->base;for(u32 i=0;i<pages*4096;i++)target[i]=i<image.image?image.data[image.offset+i]:0;
    const u32 *fixes=(const u32 *)(image.data+image.offset+image.image);
    for(u32 i=0;i<image.relocations;i++)*(u32 *)(target+fixes[i])+=r->base;
    u32 entry=r->base+image.entry;image_release(&image);
    void (*previous)(void)=scene_callback;u32 previous_owner=scene_owner;
    ephemeral=!keep;registration_failed=0;active_owner=(u32)id+1;
    task_render_hold(1);sti();result=((int (*)(const module_api_t *))entry)(&api);
    cli();task_render_hold(0);ephemeral=0;
    if(result || registration_failed){result=result?result:-5;reclaim(active_owner);scene_callback=previous;scene_owner=previous_owner;
        pframe_free_run(r->base,r->pages);objpool_release(&residents,id);
    }else if(keep){
        r->state=2;resident_count++;resident_bytes+=r->pages*4096;
        /* 初始化成功才放行IRQ；失败注册不会遗留可触发的悬空入口。
         * 共享PIC线不覆盖内置处理器，扩展负责读取/清除自身设备状态。 */
        for(u32 link=r->callback_head;link;){callback_t *c=objpool_get(&callbacks,(int)link-1);
            if(c->kind==1){u16 port=c->line>=8?0xA1:0x21;u8 bit=(u8)(1u<<(c->line&7));outb(port,inb(port)&(u8)~bit);
                if(c->line>=8)outb(0x21,inb(0x21)&(u8)~4u);}link=c->owner_next;}
    }else {reclaim(active_owner);pframe_free_run(r->base,r->pages);objpool_release(&residents,id);}
    active_owner=0;busy=0;return result;
}
static void sift(candidate_t *array,u32 start,u32 count)
{
    for(u32 root=start;root<count/2;){u32 child=root*2+1;
        if(child+1<count && array[child].number<array[child+1].number)child++;
        if(array[root].number>=array[child].number)return;candidate_t saved=array[root];array[root]=array[child];array[child]=saved;root=child;}
}
static void sort(candidate_t *array,u32 count)
{
    for(u32 n=count/2;n;n--)sift(array,n-1,count);
    for(u32 n=count;n>1;n--){candidate_t saved=array[0];array[0]=array[n-1];array[n-1]=saved;sift(array,0,n-1);}
}
void modules_init(void)
{
    objpool_init(&residents,sizeof(resident_t),0);objpool_init(&allocations,sizeof(allocation_t),0);
    objpool_init(&callbacks,sizeof(callback_t),0);scene_callback=0;active_owner=scene_owner=0;
    resident_count=resident_bytes=modules_loaded=modules_failed=0;last_service_tick=0xFFFFFFFFu;
    int count=fs_count();u32 pages=((u32)count*sizeof(candidate_t)+4095)/4096;
    candidate_t *array=count?(candidate_t *)pframe_alloc_run(pages):0;u32 accepted=0;
    /* 先验证整批再识别重复编号，不能先运行第一份、看到后面的冲突再
     * 补拒绝。每次只保留一份待验载荷，永久候选只存路径/代数/摘要。 */
    for(int i=0;i<count;i++){
        const char *name=fs_name(i);if(!extension_name(name))continue;image_t image;
        int error=array?image_read(name,1,&image):-4;
        if(error){modules_failed++;log_result(name,error==-7?"NO_USER_KEY":error==-8?"BAD_SIGNATURE":"REJECT",error);continue;}
        candidate_t *c=array+accepted;crypto_zero(c,sizeof(*c));u32 n=0;while(name[n] && n<63){c->name[n]=name[n];n++;}
        u32 meta[8];if(fs_metadata(name,meta)){image_release(&image);modules_failed++;log_result(name,"CHANGED",-9);continue;}
        c->generation=meta[7];c->number=image.number;sha256(image.data,image.bytes,c->digest);image_release(&image);accepted++;
    }
    sort(array,accepted);
    for(u32 i=0;i<accepted;){u32 end=i+1;while(end<accepted && array[end].number==array[i].number)end++;
        if(end-i>1)for(u32 n=i;n<end;n++)array[n].conflict=1;i=end;}
    for(u32 i=0;i<accepted;i++){
        candidate_t *c=array+i;int error=c->conflict?-10:load(c->name,1,1,c);
        if(error)modules_failed++;else modules_loaded++;
        log_result(c->name,c->conflict?"DUPLICATE_NUMBER":error?"INIT_REJECT":"STARTED",error);
    }
    if(array)pframe_free_run((u32)array,pages);
    /* SYS/MOD的旧SKM1只在受保护管理目录按历史合同加载，不用这个
     * 兼容分支接受CORE目录无签名映像。恢复目录从来不进入扫描。 */
    for(int i=0;i<fs_count();i++){
        const char *name=fs_name(i);u32 n=0;while(name[n])n++;
        if(n<4 || !prefix(name,"SYS/MOD/") || !same(name+n-4,".SKM"))continue;
        int error=load(name,1,0,0);if(error)modules_failed++;else modules_loaded++;log_result(name,error?"MOD_REJECT":"MOD_STARTED",error);
    }
}
int modules_paint(void)
{if(!scene_callback)return 0;u32 previous=active_owner;active_owner=scene_owner;scene_callback();active_owner=previous;return 1;}
void modules_irq(u32 line)
{
    if(line>=16)return;irq_depth++;u32 previous=active_owner;
    for(u32 link=irq_heads[line];link;){callback_t *c=objpool_get(&callbacks,(int)link-1);resident_t *r=resident(c->owner);
        if(r && r->state==2){active_owner=c->owner;((void (*)(u32,void *))c->function)(line,c->context);}link=c->next;}
    active_owner=previous;irq_depth--;
}
void modules_poll(void)
{
    if(busy || irq_depth || last_service_tick==sc_ticks)return;last_service_tick=sc_ticks;
    u32 link=service_cursor>0?((callback_t *)objpool_get(&callbacks,service_cursor-1))->next:service_head;
    if(!link)link=service_head;u32 first=link,budget=64;
    while(link && budget--){callback_t *c=objpool_get(&callbacks,(int)link-1);resident_t *r=resident(c->owner);
        if(r && r->state==2 && (c->pending || (c->period && (i32)(sc_ticks-c->due)>=0))){
            c->pending=0;u32 previous=active_owner;active_owner=c->owner;
            task_render_hold(1);sti();((void (*)(void *))c->function)(c->context);cli();task_render_hold(0);active_owner=previous;
            /* 同步IO等可信回调可能跨越本周期；从返回后再等待完整
             * period，避免每轮立即追赶而饿死管理/输入。回调期间
             * 新投递的pending保留，period=0仍只按真实事件唤醒。 */
            if(c->period)c->due=sc_ticks+c->period;
        }
        service_cursor=(int)link;link=c->next?c->next:service_head;if(link==first)break;
    }
}
int modules_execute(int pid,const char *path,int keep)
{
    if((keep!=0 && keep!=1) || !auth_can_mod(pid))return -5;
    char name[64];if(fs_normalize(path,name)<=0 || !fs_access(pid,name,FS_ACCESS_READ))return -1;
    /* 自动CORE只开机一次。手工MODULEEXEC也不能借路径把主核/SKM2
     * 当SKM1运行；新签名扩展不提供热初始化/热重载旁路。 */
    if(prefix(name,"SYS/CORE/") || prefix(name,"SYS/RECOVERY/"))return -5;
    if(keep)for(int i=objpool_next(&residents,-1);i>=0;i=objpool_next(&residents,i))
        if(same(name,((resident_t *)objpool_get(&residents,i))->name))return -6;
    return load(name,keep,0,0);
}
void modules_info(u32 out[8])
{crypto_zero(out,32);out[0]=1;out[1]=resident_count;out[2]=resident_bytes;out[3]=memory_managed_bytes();out[4]=modules_loaded;out[5]=modules_failed;}
int modules_list(int pid,char *out,u32 capacity)
{
    if(!auth_can_mod(pid))return -5;u32 needed=1;
    for(int id=objpool_next(&residents,-1);id>=0;id=objpool_next(&residents,id)){resident_t *r=objpool_get(&residents,id);u32 n=0;while(r->name[n])n++;
        if(n+1>0xFFFFFFFFu-needed)return -1;needed+=n+1;}
    if(capacity<needed)return -1;u32 at=0;
    for(int id=objpool_next(&residents,-1);id>=0;id=objpool_next(&residents,id)){resident_t *r=objpool_get(&residents,id);u32 n=0;while(r->name[n])out[at++]=r->name[n++];out[at++]='\n';}
    out[at]=0;return (int)at;
}
