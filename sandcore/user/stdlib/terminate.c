#include "SCCLIBPRIVATE.H"
typedef struct __scc_exit_node __scc_exit_node;
struct __scc_exit_node {
    __scc_exit_node *next;
    void (*callback)(void);
};
static __scc_exit_node *__scc_exit_callbacks;
static __scc_exit_node __scc_exit_reserve[32];
static __scc_exit_node *__scc_exit_spare;
static int __scc_preparing;
static void __scc_callbacks_init(void)
{
    __scc_exit_callbacks=NULL;
    __scc_exit_spare=NULL;
    __scc_preparing=0;
    /* 32项是标准最低预留，不是注册上限；更多节点仍走动态allocator。 */
    for(unsigned int i=0;i<32;i++){
        __scc_exit_reserve[i].next=__scc_exit_spare;
        __scc_exit_spare=__scc_exit_reserve+i;
    }
}
static __scc_exit_node *__scc_callback_allocate(void)
{
    if(!__scc_exit_spare)return malloc(sizeof(__scc_exit_node));
    __scc_exit_node *node=__scc_exit_spare;
    __scc_exit_spare=node->next;
    return node;
}
static void __scc_callback_release(__scc_exit_node *node)
{
    uintptr_t address=(uintptr_t)node,begin=(uintptr_t)__scc_exit_reserve;
    if(address>=begin && address-begin<sizeof(__scc_exit_reserve)){
        node->next=__scc_exit_spare;
        __scc_exit_spare=node;
    }
    else free(node);
}
int atexit(void (*callback)(void))
{
    if(!callback){
        errno=EINVAL;
        return -1;
    }
    __scc_exit_node *node=__scc_callback_allocate();
    if(!node)return -1;
    /* 先取资源再固定真实代码寿命；loader busy/init失败不留下逃逸函数指针。 */
    int status=ScApplicationPinCallback(callback);
    if(status){
        __scc_callback_release(node);
        errno=status==-6?ENOMEM:status==-10?EBUSY:EINVAL;
        return -1;
    }
    node->callback=callback;
    node->next=__scc_exit_callbacks;
    __scc_exit_callbacks=node;
    return 0;
}
static void __scc_prepare(void)
{
    if(__scc_preparing)return;
    __scc_preparing=1;
    while(__scc_exit_callbacks){
        __scc_exit_node *node=__scc_exit_callbacks;
        __scc_exit_callbacks=node->next;
        void (*callback)(void)=node->callback;
        __scc_callback_release(node);
        /* 先脱链后调用；回调新登记可进入链头，重复地址按次数执行。 */
        callback();
    }
}
static void __scc_callbacks_finish(void)
{
    /* fini只释放残余记录，不能在依赖者已结束后第二次执行atexit。 */
    while(__scc_exit_callbacks){
        __scc_exit_node *node=__scc_exit_callbacks;
        __scc_exit_callbacks=node->next;
        __scc_callback_release(node);
    }
}
void _Exit(int status)
{
    ScProcessExit(status);
    for(;;)ScProcessYield();
}
void exit(int status)
{
    /* R1撤离当前库帧再走prepare/fini；旧ROOT由最低版本依赖明确拒绝。 */
    ScApplicationExit(status);
    _Exit(EXIT_FAILURE);
}
void abort(void)
{
    __scc_early_write("SCCLIB: abnormal termination\n");
    _Exit(EXIT_FAILURE);
}
void __scc_assert_fail(const char *expression,const char *file,int line)
{
    char digits[16];
    unsigned int count=0,value=(unsigned int)line;
    do{
        digits[count++]=(char)('0'+value%10u);
        value/=10u;
    }
    while(value);
    __scc_early_write("Assertion failed: ");
    __scc_early_write(expression);
    __scc_early_write(" (");
    __scc_early_write(file);
    __scc_early_write(":");
    while(count){
        char byte[2];
        byte[0]=digits[--count];
        byte[1]=0;
        __scc_early_write(byte);
    }
    __scc_early_write(")\n");
    _Exit(EXIT_FAILURE);
}
