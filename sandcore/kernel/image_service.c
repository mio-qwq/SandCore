/* =====================================================================
 * mio：M8三环图片代理。内核绝不链接stb/libwebp，坏压缩流即便触发
 * 解码器异常也只是普通用户任务；桌面仍能处理输入、报告错误。
 *
 * 请求阶段复制路径与身份，解码阶段由独立SCX私有堆承担，提交阶段
 * 才建立内核候选页。候选整幅复制成功后换成ready；读取失败、取消、
 * 请求者退出、服务退出与超时都在这里收拢生命周期。旧FSREAD、
 * FRAME32、48字MONITOR及task_t布局一项不变。
 * ===================================================================== */
#include "image_service.h"
#include "auth.h"
#include "task.h"
#include "paging.h"
#include "userspace.h"
#include "memory.h"
#include "timer.h"
#include "fs.h"
#include "wm.h"

typedef struct {
    u32 ticket,generation,worker_generation,started;
    int state,worker,claimed,error,owner,previous,next,queued;
    int worker_owner;
    u32 worker_owner_generation,worker_ticket;
    char path[64];
    image_surface_t image;
} image_job_t;
static image_job_t empty_jobs;
static image_job_t *jobs_at(int pid)
{void *p=task_data(pid,TASK_DATA_IMAGE);return p?p:&empty_jobs;}
#define IMAGE_JOB(pid) (*jobs_at(pid))
u32 image_service_task_bytes(void){return sizeof(image_job_t);}
static u32 serial;
static int active_worker;
static u32 active_generation;
static int pending_head=-1,pending_tail=-1;

static void pending_remove(image_job_t *job)
{
    if(!job->queued)return;
    if(job->previous>=0)IMAGE_JOB(job->previous).next=job->next;else pending_head=job->next;
    if(job->next>=0)IMAGE_JOB(job->next).previous=job->previous;else pending_tail=job->previous;
    job->previous=job->next=-1;job->queued=0;
}
static void worker_detach(image_job_t *job)
{
    if(job->worker>0 && task_owned(job->worker) && task_generation(job->worker)==job->worker_generation){
        image_job_t *worker=&IMAGE_JOB(job->worker);
        if(worker->worker_ticket==job->ticket){worker->worker_ticket=0;worker->worker_owner=-1;}
    }
}
static void changed(image_job_t *job)
{
    task_notify_generation(job->owner,job->generation,TASK_EVENT_IMAGE);
    if(!job->owner)wm_request_compose();
}

static image_job_t *owned(int owner,u32 ticket)
{
    if(owner<0 || !task_owned(owner) || !ticket)return 0;
    image_job_t *job=&IMAGE_JOB(owner);
    return job->state && job->ticket==ticket && job->generation==task_generation(owner)?job:0;
}
static image_job_t *working(int worker,u32 ticket)
{
    if(worker<=0 || !task_owned(worker))return 0;
    image_job_t *link=&IMAGE_JOB(worker);
    if(link->worker_ticket!=ticket)return 0;
    image_job_t *job=owned(link->worker_owner,ticket);
    return job && job->generation==link->worker_owner_generation && job->state==2
        && job->worker==worker && job->worker_generation==task_generation(worker)?job:0;
}
static void discard(image_job_t *job)
{
    /* 先撤销身份，再杀服务：task_stop会回到本模块，不能在递归清理
     * 时误把同一缓存释放两次，也不能把worker自己的新作业混进来。 */
    int worker=job->worker;u32 generation=job->worker_generation;
    pending_remove(job);worker_detach(job);job->state=0;job->worker=0;image_release(&job->image);
    if(worker>0 && task_generation(worker)==generation)task_stop(worker);
}
static void fail(image_job_t *job,int error)
{
    int worker=job->worker;u32 generation=job->worker_generation;
    pending_remove(job);worker_detach(job);job->state=4;job->error=error;job->worker=0;image_release(&job->image);
    changed(job);
    if(worker>0 && task_generation(worker)==generation)task_stop(worker);
}
int image_service_request(int owner,const char *path)
{
    if(owner<0 || !task_owned(owner) || !path || IMAGE_JOB(owner).state)return -1;
    /* 解码任务不能再启动嵌套解码。防止一个服务占完任务槽后，所有
     * 外层作业只能等待永远不能得到CPU/槽位的子服务。 */
    if(owner==active_worker && task_generation(owner)==active_generation)return -1;
    char normalized[64];u32 stat[2];
    /* mio：桌面owner0没有用户cwd，主题中的壁纸身份沿FS根相对合同。
     * userspace_resolve明确只接收1..NTASK-1；过去把0也传进去会
     * 使每张合法压缩壁纸在解码前固定报-2。内核内部请求用同一个
     * fs_normalize做完整根路径校验，普通三环仍结合本人cwd。
     * 这里只修内部owner0入口，不改IMAGEREQUEST的用户ABI，也不
     * 给予用户取得桌面缓存/票据的权限。 */
    if((owner==0?fs_normalize(path,normalized):userspace_resolve(owner,path,normalized,sizeof(normalized)))<0
        || fs_stat(normalized,stat) || stat[0]!=1 || !stat[1] || stat[1]>16u*1024*1024)return -2;
    if(owner>0 && !fs_access(owner,normalized,FS_ACCESS_READ))return -5;
    image_job_t *job=&IMAGE_JOB(owner);
    /* 票据不回绕重用，耗尽明确失败；无需为每次请求扫描所有活任务。
     * 待办链只含尚未启动的请求，按实际创建时间FIFO负责启动与超时。 */
    if(serial==0x7FFFFFFFu)return -4;serial++;
    int i=0;do{job->path[i]=normalized[i];}while(normalized[i++]);
    job->ticket=serial;job->generation=task_generation(owner);job->started=sc_ticks;
    job->worker=job->claimed=job->error=0;job->state=1;
    job->owner=owner;job->previous=pending_tail;job->next=-1;job->queued=1;
    if(pending_tail>=0)IMAGE_JOB(pending_tail).next=owner;else pending_head=owner;
    pending_tail=owner;task_kernel_wake();
    return (int)serial;
}
int image_service_result(int owner,u32 ticket,u32 *info,void *pixels,u32 capacity)
{
    image_job_t *job=owned(owner,ticket);if(!job || !info)return -1;
    if(job->state==4)return job->error;
    if(job->state!=3)return 1;
    u32 bytes=job->image.width*job->image.height*4;
    if(pixels && capacity<bytes)return -1;
    if(!pixels && capacity)return -1;
    /* 先验全缓冲再写快照/像素。失败不能把最后一页未映射的缓冲
     * 写成一幅看似成功、实际尾部损坏的照片。 */
    if(pixels && !paging_user_range(TASK(owner).pd,(u32)pixels,bytes))return -1;
    info[0]=1;info[1]=job->image.width;info[2]=job->image.height;info[3]=bytes;
    info[4]=2;info[5]=ticket;info[6]=info[7]=0;
    if(!pixels)return 0;
    task_render_hold(1);sti();
    int copied=paging_user_copy(TASK(owner).pd,(u32)pixels,job->image.pixels,bytes,1);
    cli();task_render_hold(0);
    if(copied)return -1;
    discard(job);return 0;
}
int image_service_claim(int worker,u32 ticket,char *path,u32 capacity)
{
    image_job_t *job=working(worker,ticket);
    if(!job || job->claimed || !path || !capacity || capacity>64)return -1;
    u32 n=0;while(job->path[n])n++;
    if(capacity<=n)return -1;
    for(u32 i=0;i<=n;i++)path[i]=job->path[i];
    job->claimed=1;return (int)n;
}
int image_service_submit(int worker,u32 ticket,u32 width,u32 height,u32 address,int status)
{
    image_job_t *job=working(worker,ticket);if(!job || !job->claimed)return -1;
    if(status){
        if(status<-6 || status>-2)return -1;
        job->state=4;job->error=status;changed(job);return 0;
    }
    if(!width || !height || width>1920 || height>1080)return -1;
    u32 bytes=width*height*4;
    if(!paging_user_range(TASK(worker).pd,address,bytes))return -1;
    image_surface_t candidate={0};candidate.width=width;candidate.height=height;
    candidate.pages=(bytes+4095)/4096;
    candidate.pixels=(u32 *)pframe_alloc_run(candidate.pages);
    if(!candidate.pixels){job->state=4;job->error=-5;changed(job);return -5;}
    task_render_hold(1);sti();
    int copied=paging_user_copy(TASK(worker).pd,address,candidate.pixels,bytes,0);
    cli();task_render_hold(0);
    if(copied){image_release(&candidate);return -1;}
    image_release(&job->image);job->image=candidate;job->state=3;
    changed(job);return 0;
}
int image_service_cancel(int owner,u32 ticket)
{
    image_job_t *job=owned(owner,ticket);if(!job)return -1;
    discard(job);return 0;
}
int image_service_take(u32 ticket,image_surface_t *out)
{
    image_job_t *job=owned(0,ticket);if(!job || !out)return -1;
    if(job->state==4){int error=job->error;discard(job);return error;}
    if(job->state!=3)return 1;
    image_release(out);*out=job->image;job->image=(image_surface_t){0};discard(job);return 0;
}
void image_service_owner_stopped(int owner)
{
    if(owner<=0 || !task_owned(owner))return;
    image_job_t *link=&IMAGE_JOB(owner);
    image_job_t *job=link->worker_ticket?working(owner,link->worker_ticket):0;
    if(job){worker_detach(job);job->worker=0;fail(job,-6);}
    if(link->state)discard(link);
}
void image_service_poll(void)
{
    /* 单服务串行启动，避免多张1080p图片同时占据全部任务与内存。
     * 窗口仍正常异步运行。完成像素允许暂存直到请求者读取，下一张
     * 不依赖上一张是否最小化；上一worker退出或异常脱离即可继续，
     * 异常诊断仍交给原暂停卡片保留，不能自动关闭来隐藏服务崩溃。
     * 30秒是真实PIT超时，不把循环次数或宿主秒当客体执行时间。 */
    if(active_worker){
        int worker=active_worker;
        image_job_t *job=task_owned(worker) && task_generation(worker)==active_generation
            ?working(worker,IMAGE_JOB(worker).worker_ticket):0;
        if(job && TASK(worker).state==3){
            /* 服务也遵守普通三环异常协议：仅报告解码失败，保留真实
             * APPLICATION PAUSED卡片，由用户关叉才杀暂停任务。将
             * worker从作业脱离，客户端自动取消错误票据时不能把诊断
             * 卡一并悄悄消掉；新请求可用另一空槽继续工作。 */
            worker_detach(job);job->state=4;job->error=-6;job->worker=0;changed(job);
        }
        else if(job && (TASK(worker).state!=1 || (u32)(sc_ticks-job->started)>=3000))fail(job,-6);
        if(!task_owned(worker) || task_generation(worker)!=active_generation || TASK(worker).state!=1)active_worker=0;
    }
    u32 budget=64;
    while(pending_head>=0 && budget--){
        image_job_t *job=&IMAGE_JOB(pending_head);
        if(job->generation!=task_generation(job->owner)){discard(job);continue;}
        if((u32)(sc_ticks-job->started)<3000)break;
        fail(job,-6);
    }
    if(pending_head>=0 && (u32)(sc_ticks-IMAGE_JOB(pending_head).started)>=3000){task_kernel_wake();return;}
    if(active_worker)return;
    if(pending_head>=0){
        int i=pending_head;
        /* 动态任务没有“预设空槽”前提；spawn依据真实内存申请。
         * 保留请求自身的30秒期限，不因历史编号全部使用而永远不启动。 */
        u32 stat[2];if(fs_stat("SYS/CORE/IMAGE.SCX",stat) || stat[0]!=1){fail(&IMAGE_JOB(i),-3);return;}
        char command[48]="SYS/CORE/IMAGE.SCX ",reverse[11];int n=0,at=19;
        u32 ticket=IMAGE_JOB(i).ticket;do{reverse[n++]=(char)('0'+ticket%10);ticket/=10;}while(ticket);
        while(n)command[at++]=reverse[--n];command[at]=0;
        /* 启动文件IO也可能超过一tick：保持任务0与共享装载中转区，
         * 只开硬件IRQ，不让另一EXEC抢占并覆盖scx_stage。 */
        task_render_hold(1);sti();int worker=auth_service_exec(command,i);cli();task_render_hold(0);
        if(worker<0){fail(&IMAGE_JOB(i),-3);return;}
        IMAGE_JOB(i).worker=active_worker=worker;
        IMAGE_JOB(i).worker_generation=active_generation=task_generation(worker);
        IMAGE_JOB(i).claimed=0;IMAGE_JOB(i).state=2;pending_remove(&IMAGE_JOB(i));
        IMAGE_JOB(worker).worker_owner=i;IMAGE_JOB(worker).worker_owner_generation=task_generation(i);
        IMAGE_JOB(worker).worker_ticket=IMAGE_JOB(i).ticket;return;
    }
}
