#include "sc32_core.h"
#include "task.h"
#include "task_store.h"
#include "paging.h"
#include "auth.h"
#include "session.h"
#include "memory.h"
#include "timer.h"
#include "mouse.h"
#include "sc32_broker.h"
#include "sc32_drop.h"
#include "fs_lease.h"
#include "../user/SC32HOST.H"
#include "../user/sc32/SC32WIRE.H"

#define SC32_PTE_PRESENT 0x001u
#define SC32_PTE_USER 0x004u

/* 共享公开记录只作为已复制的可信局部值使用；用户磁盘/指针不强转
 * 内核结构。编译期尺寸断言防止无意改布局后静默扩写旧版本输出。 */
typedef char sc32_gate_keeps_wire[(SC32_CORE_SYSCALL==SC32_WIRE_GATE)?1:-1];
typedef char sc32_probe_keeps_wire[(sizeof(Sc32ProbeRequest)==20)?1:-1];
typedef char sc32_capabilities_keeps_wire[(sizeof(ScCapabilities)==64)?1:-1];
typedef char sc32_process_ref_keeps_wire[(sizeof(ScProcessRef)==16)?1:-1];
typedef char sc32_process_info_keeps_wire[(sizeof(ScProcessInfo)==96)?1:-1];
typedef char sc32_memory_keeps_wire[(sizeof(ScMemoryInfo)==64)?1:-1];
typedef char sc32_monotonic_keeps_wire[(sizeof(ScMonotonicClock)==32)?1:-1];

static void clear(void *memory,u32 bytes)
{u8 *p=memory;for(u32 i=0;i<bytes;i++)p[i]=0;}

static int caller_valid(int pid)
{
    u32 flags;__asm__ __volatile__("pushfl;popl %0":"=r"(flags));
    if(flags&0x200u)return SC32_E_BUSY;
    /* pid来自可信分发器，不允许请求记录或其它零环误用把某个任意
     * pid当成当前用户缓冲的拥有者。IF=0保持页表、身份与代数同代。 */
    if(pid<=0 || pid!=task_pid() || !task_owned(pid) || TASK(pid).state!=1
        || !TASK(pid).pd || TASK(pid).pd==paging_kernel_pd())return SC32_E_INVALID;
    return SC32_OK;
}

int sc32_core_input(int pid,u32 address,void *local,u32 bytes)
{
    int error=caller_valid(pid);if(error)return error;
    if(!local || bytes<8)return SC32_E_INVALID;
    u32 header[2];
    if(paging_user_copy(TASK(pid).pd,address,header,sizeof(header),0))return SC32_E_INVALID;
    if(header[0]!=bytes)return SC32_E_INVALID;
    if(header[1]!=SC32_WIRE_VERSION)return SC32_E_VERSION;
    /* 先确认完整范围再复制，不能读了半个request才发现后页缺失。
     * 输入只查P/US，合法只读SCL里的描述符不需要被迫改成RW。 */
    return paging_user_copy(TASK(pid).pd,address,local,bytes,0)?SC32_E_INVALID:SC32_OK;
}

int sc32_core_output(int pid,u32 address,u32 bytes)
{
    int error=caller_valid(pid);if(error)return error;
    if(bytes<8 || bytes>SC32_WIRE_MAX_RECORD)return SC32_E_INVALID;
    u32 header[2];
    if(paging_user_copy(TASK(pid).pd,address,header,sizeof(header),0))return SC32_E_INVALID;
    if(header[0]<bytes || header[0]>SC32_WIRE_MAX_RECORD)return SC32_E_INVALID;
    if(header[1]!=SC32_WIRE_VERSION)return SC32_E_VERSION;
    /* 未知尾部虽然不写，仍属于调用者声明的完整输出范围。不能只验
     * 已知前缀后接受一个含只读页/缺页的“较大缓冲”，也不清零尾部。 */
    return paging_user_write_range(TASK(pid).pd,address,header[0])?SC32_OK:SC32_E_INVALID;
}

static int publish(int pid,u32 output,void *value,u32 bytes)
{
    /* 具体操作已先验整个声明范围；发布只复制该版本定义的前缀。
     * 再次由分页复制核对RW，不能用CR0.WP未开启的零环权限盲写。 */
    return paging_user_copy(TASK(pid).pd,output,value,bytes,1)?SC32_E_INVALID:SC32_OK;
}

static int probe(int pid,u32 request,u32 output)
{
    if(output)return SC32_E_INVALID;
    Sc32ProbeRequest input;int error=sc32_core_input(pid,request,&input,sizeof(input));
    if(error)return error;
    if(!input.access || (input.access&~(SC32_WIRE_READ|SC32_WIRE_WRITE)))return SC32_E_INVALID;
    /* 零字节不产生任何访问，NULL也合法；这不授予随后非零访问。
     * 每个实际读写服务仍须重新检查范围，Probe不是可缓存的能力票据。 */
    if(!input.bytes)return SC32_OK;
    int valid=(input.access&SC32_WIRE_WRITE)
        ?paging_user_write_range(TASK(pid).pd,input.address,input.bytes)
        :paging_user_range(TASK(pid).pd,input.address,input.bytes);
    return valid?SC32_OK:SC32_E_INVALID;
}

static int capabilities(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScCapabilities));if(error)return error;
    if(request)return SC32_E_INVALID;
    ScCapabilities result;clear(&result,sizeof(result));
    result.size=sizeof(result);result.version=SC32_RECORD_VERSION;
    result.abi_major=SC32_ABI_MAJOR;result.abi_minor=SC32_ABI_MINOR;
    result.kernel_contract=SC32_WIRE_VERSION;result.syscall_gate=SC32_WIRE_GATE;
    /* 位表示已存在的内核服务合同，不从老FRAME或顺序替换流推断
     * 消息、更新文件、剪贴板、IPC或共享映射已经具备完整新语义。
     * 此构建已链接真实FILE/消息gate与owner清理。控件、剪贴板、
     * IPC和共享映射没有因为旧接口存在就提前置能力位。 */
    result.capabilities_lo=SC32_CAP_CORE_QUERY|SC32_CAP_PROCESS|SC32_CAP_PRIVATE_PAGES
        |SC32_CAP_PATH_IO|SC32_CAP_JOBS_STREAMS|SC32_CAP_SESSIONS|SC32_CAP_THEMES
        |SC32_CAP_DISPLAY|SC32_CAP_LIBRARIES|SC32_CAP_MONOTONIC|SC32_CAP_UTC_CLOCK
        |SC32_CAP_DIRECTORIES|SC32_CAP_UPDATE_FILES|SC32_CAP_WINDOW_MESSAGES|SC32_CAP_FONT_GLYPHS
        |SC32_CAP_POPUPS|SC32_CAP_POINTER_CURSOR;
    if(sc32_broker_available())result.capabilities_lo|=SC32_CAP_CLIPBOARD|SC32_CAP_IPC|SC32_CAP_SHARED_MEMORY;
    if(sc32_drop_available())result.capabilities_lo|=SC32_CAP_DROP_TARGETS;
    if(fs_lease_available())result.capabilities_hi|=SC32_CAP_LEASES_HI;
    /* 能力位表示已接入的角色服务合同，不表示当前用户桌面已经ACTIVE。 */
    result.capabilities_hi|=SC32_CAP_HOST_HI;
    result.max_path_bytes=SC32_PATH_CAPACITY;result.max_arguments_bytes=1024;
    if(mouse_wheel_available())result.capabilities_lo|=SC32_CAP_POINTER_WHEEL;
    result.page_size=PAGE_SIZE;result.pit_hz=timer_frequency_hz();
    result.heap_bytes=USER_HEAP_TOP-USER_HEAP_BASE;
    result.module_bytes=USER_MODULE_TOP-USER_MODULE_BASE;
    return publish(pid,output,&result,sizeof(result));
}

static int process_allowed(int caller,int target,int uid)
{
    if(caller==target || auth_can_mod(caller))return 1;
    int caller_uid=auth_uid(caller);
    /* SYSTEM资格包含真实外部串口会话，不是两个UID都为-1便互读。
     * 普通root只管理普通目标；普通同UID还必须是同一真实会话，
     * 避免借相同数字窥探另一次登录的等待/事件/名称等详细状态。 */
    if(uid<0 || caller_uid<0)return 0;
    if(auth_can_manage(caller))return 1;
    u32 session=session_task(caller);
    return uid==caller_uid && session && session==session_task(target);
}

static int process_query(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScProcessInfo));if(error)return error;
    ScProcessRef input;error=sc32_core_input(pid,request,&input,sizeof(input));if(error)return error;
    int target;
    if(!input.pid && !input.generation){target=pid;input.generation=task_generation(pid);}
    else{
        if(input.pid>0x7FFFFFFFu || !input.generation)return SC32_E_INVALID;
        target=(int)input.pid;
    }
    if(!task_owned(target) || task_generation(target)!=input.generation)return SC32_E_STALE;
    u32 identity[8];
    /* 退出后凭据按既有清理链撤销，历史PID记录不足以重新证明身份。
     * 新查询只承诺真正活对象；不把旧STATUS返回码和新代数拼起来。
     * 普通STATUS/JOB的历史退出行为继续由原接口提供。 */
    if(auth_info(target,identity))return SC32_E_STALE;
    if(!process_allowed(pid,target,(int)identity[1]))return SC32_E_ACCESS;
    task_record_t *record=task_record(target);
    ScProcessInfo result;clear(&result,sizeof(result));
    result.size=sizeof(result);result.version=SC32_RECORD_VERSION;
    result.pid=(u32)target;result.generation=identity[5];result.state=(u32)record->task.state;
    result.uid=(i32)identity[1];result.gid=(i32)identity[2];result.realm=identity[3];
    result.session=record->session;result.flags=SC32_PROCESS_DETAILS|SC32_PROCESS_REALM_VALID;
    result.waiting=record->waiting!=0;result.wait_mask=record->wait_mask;result.pending_events=record->events;
    result.running_samples=record->running;result.dispatches=record->dispatch;
    for(u32 i=0;i<sizeof(record->task.name);i++)result.name[i]=record->task.name[i];
    /* 活/暂停对象没有已完成退出码。故障暂停的暂存诊断值不冒充EXIT。
     * name剩余4B和所有保留字段由可信局部记录清零，不泄漏内核栈。 */
    return publish(pid,output,&result,sizeof(result));
}

static void private_page_counts(u32 pd,ScMemoryInfo *result)
{
    u32 *directory=(u32 *)pd;result->page_table_pages=1;
    /* 只走本人现存US私有页表，不扫其它任务或PID历史高水位。
     * 当前三个窗口最多33张PT；一次查询按已映射PT逐页计数，不
     * 分配额外资源，也不把全局PF余额当作本进程页数。页目录及
     * PT另列，内核栈/旁表/历史记录池不重复计进用户映射正文。 */
    for(u32 index=0;index<1024;index++){
        u32 entry=directory[index],required=SC32_PTE_PRESENT|SC32_PTE_USER;
        if((entry&required)!=required)continue;
        result->page_table_pages++;
        u32 *table=(u32 *)(entry&~4095u);
        for(u32 page=0;page<1024;page++){
            if((table[page]&required)!=required)continue;
            u32 address=(index<<22)|(page<<12);result->process_total_pages++;
            if(address>=USER_HEAP_BASE && address<USER_HEAP_TOP)result->process_heap_pages++;
            if(address>=USER_MODULE_BASE && address<USER_MODULE_TOP)result->process_module_pages++;
        }
    }
}

static int memory_query(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScMemoryInfo));if(error)return error;
    if(request)return SC32_E_INVALID;
    ScMemoryInfo result;clear(&result,sizeof(result));
    result.size=sizeof(result);result.version=SC32_RECORD_VERSION;result.page_size=PAGE_SIZE;
    result.managed_bytes=memory_managed_bytes();result.used_bytes=memory_used_bytes();
    result.free_bytes=memory_free_bytes();
    result.heap_base=USER_HEAP_BASE;result.heap_bytes=USER_HEAP_TOP-USER_HEAP_BASE;
    result.module_base=USER_MODULE_BASE;result.module_bytes=USER_MODULE_TOP-USER_MODULE_BASE;
    private_page_counts(TASK(pid).pd,&result);
    return publish(pid,output,&result,sizeof(result));
}

static int monotonic_query(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScMonotonicClock));if(error)return error;
    if(request)return SC32_E_INVALID;
    ScMonotonicClock result;clear(&result,sizeof(result));
    result.size=sizeof(result);result.version=SC32_RECORD_VERSION;
    u32 ticks[2];timer_monotonic(ticks);result.ticks.lo=ticks[0];result.ticks.hi=ticks[1];
    result.hz=timer_frequency_hz();
    return publish(pid,output,&result,sizeof(result));
}

int sc32_core_call(int pid,u32 operation,u32 request,u32 output)
{
    int error=caller_valid(pid);if(error)return error;
    switch(operation){
    case SC32_WIRE_PROBE:return probe(pid,request,output);
    case SC32_WIRE_CAPABILITIES:return capabilities(pid,request,output);
    case SC32_WIRE_PROCESS:return process_query(pid,request,output);
    case SC32_WIRE_MEMORY:return memory_query(pid,request,output);
    case SC32_WIRE_MONOTONIC:return monotonic_query(pid,request,output);
    default:return SC32_E_NOT_SUPPORTED;
    }
}
