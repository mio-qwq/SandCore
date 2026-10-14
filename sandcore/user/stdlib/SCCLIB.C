/* 同一份普通C源码供GCC引导与客体SCCC生成独立SCL，没有main/SCAPI/asm桥。 */
#include "SCCLIBPRIVATE.H"
#include "errno.c"
#include "memory.c"
#include "string.c"
#include "ctype.c"
#include "convert.c"
#include "algorithm.c"
#include "stdio.c"
#include "format.c"
#include "terminate.c"
static int __scc_initialized;
int SccLibraryInit(void)
{
    if(__scc_initialized)return -1;
    __scc_errno_value=0;
    __scc_callbacks_init();
    if(__scc_stdio_init()){
        __scc_stdio_finish();
        __scc_heap_finish();
        return -1;
    }
    /* 最后一步注册；根仅在init返回0后commit provider pin与终止栈。 */
    int status=ScApplicationRegisterPrepare(__scc_prepare);
    if(status){
        __scc_stdio_finish();
        __scc_heap_finish();
        return status;
    }
    __scc_initialized=1;
    return 0;
}
void SccLibraryFini(void)
{
    if(!__scc_initialized)return;
    __scc_stdio_finish();
    __scc_callbacks_finish();
    __scc_environment_finish();
    __scc_heap_finish();
    __scc_initialized=0;
}
