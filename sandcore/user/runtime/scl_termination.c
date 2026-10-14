#include "SCLJUMPPRIVATE.H"

enum {APP_NEW,APP_LOADING,APP_RUNNING,APP_UNWINDING,APP_PREPARING,APP_FINALIZING,APP_FINISHED,APP_FAILED};
typedef struct {
    ScRootJumpI386 jump;
    ScRuntime *runtime;
    ScRuntimePlatform platform;
    const unsigned char *descriptor;
    ScModule *handles;
    void **addresses;
    ScLoaded *prepare_owner,*interrupted_init;
    void (*prepare)(void);
    void *termination_stack;
    ScU32 phase,started,main_linked,prepare_called,finishing_on_stack,destroy_allowed;
    ScU32 exit_from_init;
    int load_error;
} ScApplicationState;
static ScApplicationState application;
static ScRuntime *application_runtime;
ScRuntime *ScApplicationRuntime(void){return application_runtime;}

static void app_termination_fail(void)
{
    /* 非法递归/损坏控制点不再触碰用户堆或stdio链。低层EXIT保留
     * 异常边界，固定小诊断不会将INT_MIN取负或递归分配错误对象。 */
    static const char text[]="SCL: invalid normal termination\n";
    ScRuntimePlatformCall(0x212,2,(int)text,sizeof(text)-1,0,0,0);
    ScRuntimePlatformCall(0,1,0,0,0,0,0);
    for(;;)ScRuntimePlatformCall(7,0,0,0,0,0,0);
}
static int app_runtime_change_allowed(ScRuntime *r)
{
    return r!=application_runtime || application.phase==APP_LOADING || application.phase==APP_RUNNING;
}
static int app_runtime_destroy_allowed(ScRuntime *r)
{
    return r!=application_runtime || (application.phase==APP_FINALIZING && application.destroy_allowed);
}
static int app_module_code(ScLoaded *m,ScPointer address)
{
    ScPointer base=(ScPointer)m->image;
    if(address<base || address-base>=word(m->metadata,SCL_H_IMAGE_SIZE))return 0;
    ScU32 relative=(ScU32)(address-base);
    for(ScU32 i=0;i<word(m->metadata,SCL_H_SEGMENT_COUNT);i++){
        const unsigned char *segment=record(m,SCL_H_SEGMENTS,SCL_SEGMENT_SIZE,i);
        ScU32 start=word(segment,SCL_S_RVA),bytes=word(segment,SCL_S_MEMORY_SIZE);
        if((word(segment,SCL_S_FLAGS)&SCL_SEG_X) && relative>=start && relative-start<bytes)return 1;
    }
    return 0;
}
int ScApplicationRegisterPrepare(void (*prepare_function)(void))
{
    ScRuntime *r=application_runtime;
    if(!r || r!=application.runtime)return SC_SCL_HANDLE;
    if(!prepare_function)return SC_SCL_FORMAT;
    if((application.phase!=APP_LOADING && application.phase!=APP_RUNNING) || !application.jump.armed
        || !r->busy || r->invoking_kind!=1 || !r->invoking
        || compare(module_name(r->invoking),"SCCLIB.SCL"))return SC_SCL_BUSY;
    ScLoaded *m=r->invoking;
    if(application.prepare_owner || m->application_prepare || m->application_stack_candidate)return SC_SCL_BUSY;
    if(!app_module_code(m,(ScPointer)prepare_function))return SC_SCL_RANGE;
    void *candidate=r->platform.allocate(r->platform.context,SC_ROOT_TERMINATION_BYTES);
    if(!candidate)return SC_SCL_MEMORY;
    if((ScPointer)candidate>0xffffffffu-SC_ROOT_TERMINATION_BYTES){
        r->platform.release(r->platform.context,candidate);return SC_SCL_RANGE;
    }
    /* 当前init尚未成功，不能让调用方得到驻留副作用。候选挂在
     * 已完整创建的模块上，失败rollback能找回它，不依赖已弃C帧。 */
    m->application_prepare=prepare_function;m->application_stack_candidate=candidate;
    return 0;
}
static void app_module_initialized(ScRuntime *r,ScLoaded *m)
{
    if(!m->application_prepare)return;
    if(r!=application_runtime || application.prepare_owner || !m->application_stack_candidate)app_termination_fail();
    application.prepare_owner=m;application.prepare=m->application_prepare;
    application.termination_stack=m->application_stack_candidate;m->application_stack_candidate=0;
    application.jump.termination_top=(ScU32)(ScPointer)application.termination_stack+SC_ROOT_TERMINATION_BYTES;
    m->pinned=1;
}
static void app_module_releasing(ScRuntime *r,ScLoaded *m)
{
    if(m->application_stack_candidate){
        r->platform.release(r->platform.context,m->application_stack_candidate);m->application_stack_candidate=0;
    }
    if(r==application_runtime && application.prepare_owner==m){
        application.prepare_owner=0;application.prepare=0;
        /* fini可在128KiB终止栈上运行；provider不拥有此栈。正常
         * 收束留到恢复原ROOT栈再归还，事务rollback才在原栈直接撤。 */
        if(!application.finishing_on_stack && application.termination_stack){
            r->platform.release(r->platform.context,application.termination_stack);
            application.termination_stack=0;application.jump.termination_top=0;
        }
    }
    m->application_prepare=0;
}
int ScApplicationPinCallback(void (*callback)(void))
{
    ScRuntime *r=application_runtime;
    if(!r || r!=application.runtime)return SC_SCL_HANDLE;
    if(!callback)return SC_SCL_FORMAT;
    if((application.phase!=APP_RUNNING && application.phase!=APP_PREPARING) || r->busy)return SC_SCL_BUSY;
    ScPointer address=(ScPointer)callback,metadata=(ScPointer)application.descriptor;
    /* SCD没有单独text表，主正文域排除CRT/metadata/BSS。MODULE
     * 则使用完整已验证的X段；static函数与导出函数同样能被固定。 */
    if(application.main_linked && address>=SCD_PROGRAM_BASE+SCD_CRT_SIZE && address<metadata)return 0;
    for(ScLoaded *m=r->modules;m;m=m->next){
        if(m->ready && app_module_code(m,address)){m->pinned=1;return 0;}
    }
    return SC_SCL_RANGE;
}
void ScApplicationExit(int status)
{
    ScRuntime *r=application_runtime;
    if(!r || r!=application.runtime || !application.jump.armed || !application.prepare_owner
        || !application.prepare_owner->ready || !application.jump.termination_top
        || (application.phase!=APP_LOADING && application.phase!=APP_RUNNING))app_termination_fail();
    application.exit_from_init=0;application.interrupted_init=0;
    if(r->busy){
        /* 只有真实init调用点的图结点已完整创建。其它busy可能在
         * 回滚/fini/平台服务中，不能凭用户要求强行解锁和Destroy。 */
        if(r->invoking_kind!=1 || !r->invoking || r->invoking->ready || r->invoking->initialized)app_termination_fail();
        application.exit_from_init=1;application.interrupted_init=r->invoking;
    }
    application.jump.status=(ScU32)status;application.phase=APP_UNWINDING;
    __scl_root_unwind(&application.jump);
    app_termination_fail();
}

#ifndef __SCCC__
/* 两个实体只有basic asm，不允许C局部变量或生成的prologue换栈。
 * SCCC相同私有call由专用emit生成，不能用当前inline asm猜EBP。 */
__attribute__((naked,noinline))
int __scl_root_run(ScRootJumpI386 *jump __attribute__((unused)),
                   int (*entry)(void) __attribute__((unused)),
                   int (*finish)(void *) __attribute__((unused)),void *context __attribute__((unused)))
{
    __asm__ __volatile__(
        "push %ebp; push %ebx; push %esi; push %edi; "
        "mov 20(%esp),%edx; mov %esp,0(%edx); mov %ebp,4(%edx); "
        "mov %ebx,8(%edx); mov %esi,12(%edx); mov %edi,16(%edx); "
        "mov 28(%esp),%eax; mov %eax,28(%edx); "
        "mov 32(%esp),%eax; mov %eax,32(%edx); "
        "call .L_scl_root_pc; .L_scl_root_pc: pop %ecx; "
        "add $(.L_scl_root_landing-.L_scl_root_pc),%ecx; mov %ecx,20(%edx); "
        "movl $1,44(%edx); mov %edx,%esi; mov 24(%esp),%eax; "
        "and $-16,%esp; cld; call *%eax; "
        "mov %esi,%edx; mov %eax,36(%edx); movl $0,40(%edx); movl $0,44(%edx); "
        ".L_scl_root_landing: mov 24(%esi),%ecx; test %ecx,%ecx; "
        "jnz .L_scl_root_stack; mov 0(%esi),%ecx; "
        ".L_scl_root_stack: mov %ecx,%esp; and $-16,%esp; sub $12,%esp; "
        "push 32(%esi); cld; call *28(%esi); "
        "mov 0(%esi),%esp; pop %edi; pop %esi; pop %ebx; pop %ebp; cld; ret");
}
__attribute__((naked,noinline,noreturn))
void __scl_root_unwind(ScRootJumpI386 *jump __attribute__((unused)))
{
    __asm__ __volatile__(
        "mov 4(%esp),%edx; cmpl $1,44(%edx); jne .L_scl_root_invalid; "
        "cmpl $0,24(%edx); je .L_scl_root_invalid; "
        "movl $0,44(%edx); movl $1,40(%edx); "
        "mov 4(%edx),%ebp; mov 8(%edx),%ebx; mov %edx,%esi; mov 16(%edx),%edi; "
        "cld; jmp *20(%edx); .L_scl_root_invalid: ud2; jmp .L_scl_root_invalid");
}
#endif
