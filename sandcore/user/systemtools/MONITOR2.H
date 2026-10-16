#ifndef SANDCORE_MONITOR2_PRIVATE_H
#define SANDCORE_MONITOR2_PRIVATE_H
#include "TOOLS.H"
enum {MON_FREEZE=0x6500,MON_PROCESSES,MON_SYSTEM,MON_STORAGE,MON_CPU_HISTORY,MON_MEMORY_HISTORY,MON_DETAILS,MON_TERMINATE,MON_RESET_HISTORY};
typedef struct {
    ScProcessInfo process;
    ScU32 item,total,percent,valid;
    char name[32],state[24],cpu[16],owner[32],session[24],waiting[48];
} MonitorRow;
typedef struct {
    MonitorRow *rows;
    ScU32 count,capacity,visible;
    ScHash *identities,*items;
    ScU32 *indices,index_capacity,*order,*rank;
} MonitorSnapshot;
typedef struct {ScU32 tick,busy,used,total,cpu_valid,memory_valid;
} MonitorPoint;
typedef struct {char label[80],value[128];
} MonitorMetric;
typedef struct {
    ScU32 active,publish,phase,at,count,width,left,a,b,middle,last,out,merging,field,descending;
    MonitorSnapshot *snapshot;
    ScU32 *order,*scratch,*rank;
} MonitorOrder;
typedef struct {
    ScUiNode table,filter,summary,chart_bar,cpu_chart,memory_chart,filter_bar,freeze_button;
    ScUiProvider *provider;
    ScUiSelection *selection;
    ScUiClass chart_class;
    ScUiDialog *confirm;
    ScProcessRef target,self;
    ScU32 confirm_active;
    MonitorSnapshot current,staging;
    MonitorOrder order;
    ScU32 *work_indices,work_capacity;
    ScProcessPage page;
    ScU32 scan,baseline,task_valid,task_baseline,sample_pending,reorder_pending,
    frozen,timer,next_item,revision,field,descending,mode,samples,cursor,history_count,refresh_once,layout_stamp;
    ScCpuInfo cpu,before;
    ScMemoryInfo memory;
    ScStorageInfo storage;
    ScU32 cpu_valid,memory_valid,storage_valid,cpu_baseline,pending_busy,pending_idle,pending_kernel,pending_user,pending_graphics;
    MonitorPoint history[120];
    MonitorMetric system_metrics[24],volume_metrics[20];
    ScU32 system_count,volume_count;
    char filter_text[256],status[256];
} MonitorApp;
static MonitorApp mon;
static ScStatus mon_columns(void);
static void mon_summary(void);
static ScStatus mon_notify(ScU32 kind);
static ScStatus mon_index_begin(MonitorSnapshot *,ScU32,ScU32,ScU32);
#endif
