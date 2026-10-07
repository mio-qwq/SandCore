#ifndef SANDCORE_TASK_STORE_H
#define SANDCORE_TASK_STORE_H
#include "task.h"
#include "objpool.h"

/* 扩展字段属于内核私有对象；公开task_t前缀及陷入帧没有加字段。 */
typedef struct {
    task_t task;
    u32 generation,running,dispatch,last_yield;
    int exit_code,ready_prev,ready_next,queue,zombie_next,cleanup_next;
    u64 queue_epoch;
    u32 session,reservation_water,transactions,windows,captures;
    int session_previous,session_next;
    u32 events,wait_mask,wait_frame;
    u64 wait_deadline;
    int waiting,wait_heap;
    u32 deferred_draw;
    int visibility_aware;
    int cleanup_phase;
} task_record_t;

int task_store_init(const u32 sizes[TASK_DATA_COUNT]);
int task_store_reserve(void);
void task_store_release(int pid);
void task_store_cancel(int pid);
void task_store_commit(int pid);
task_record_t *task_record(int pid);
void task_store_stats(u32 out[8]);

#endif
