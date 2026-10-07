#include "task_store.h"
typedef char task_prefix_keeps_abi[(sizeof(task_t)==168)?1:-1];

static objpool_t records,metadata[TASK_DATA_COUNT];
static task_record_t empty_record;

task_record_t *task_record(int pid)
{
    task_record_t *p=objpool_history(&records,pid);
    /* 旧快照仍要输出最初8/32行。缺失编号只读零记录，任何写入口
     * 必须先检查task_owned；不能把它误当任务0或可复用的真实槽。 */
    return p?p:&empty_record;
}
task_t *task_at(int pid){return &task_record(pid)->task;}
u32 task_slot_count(void){return records.high_water;}
int task_owned(int pid){return objpool_get(&records,pid)!=0;}
int task_next(int after){return objpool_next(&records,after);}
void *task_data(int pid,int kind)
{return kind>=0 && kind<TASK_DATA_COUNT?objpool_get(&metadata[kind],pid):0;}

static int claim_data(int pid)
{
    int done=0;
    for(;done<TASK_DATA_COUNT;done++)if(objpool_claim(&metadata[done],pid))break;
    if(done==TASK_DATA_COUNT)return 0;
    while(done)objpool_release(&metadata[--done],pid);return -1;
}
int task_store_init(const u32 sizes[TASK_DATA_COUNT])
{
    if(objpool_init(&records,sizeof(task_record_t),OBJPOOL_KEEP_IDLE))return -1;
    for(int i=0;i<TASK_DATA_COUNT;i++)if(objpool_init(&metadata[i],sizes[i],0))goto failed;
    if(objpool_claim(&records,0) || claim_data(0))goto failed;
    objpool_remember(&records,0);
    return 0;
failed:
    for(int i=0;i<TASK_DATA_COUNT;i++)objpool_destroy(&metadata[i]);
    objpool_destroy(&records);return -1;
}
int task_store_reserve(void)
{
    u32 water=records.high_water;int pid=objpool_alloc(&records);if(pid<0)return -1;
    if(claim_data(pid)){objpool_release(&records,pid);records.high_water=water;return -1;}
    task_record(pid)->reservation_water=water;
    return pid;
}
void task_store_commit(int pid){objpool_remember(&records,pid);}
void task_store_cancel(int pid)
{
    if(pid<=0 || !task_owned(pid))return;
    u32 water=task_record(pid)->reservation_water;task_store_release(pid);records.high_water=water;
}
void task_store_release(int pid)
{
    if(pid<=0 || !task_owned(pid))return;
    for(int i=0;i<TASK_DATA_COUNT;i++)objpool_release(&metadata[i],pid);
    objpool_release(&records,pid);
}
void task_store_stats(u32 out[8])
{
    /* 第5字是各池峰值之和的上界，不冒充同一时刻整机物理页峰值。
     * 公开PROCESSPAGE只使用当前页数，实测高峰由验收采样独立记录。 */
    u32 pages=records.pages,peak=records.peak_pages;
    for(int i=0;i<TASK_DATA_COUNT;i++){pages+=metadata[i].pages;peak+=metadata[i].peak_pages;}
    out[0]=1;out[1]=records.objects;out[2]=records.high_water;
    out[3]=records.pages;out[4]=pages;out[5]=peak;out[6]=sizeof(task_record_t);out[7]=0;
}
