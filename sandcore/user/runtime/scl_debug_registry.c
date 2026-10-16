#include "SCLPRIVATE.H"
#include "../debugapi/SC32DEBUGWIRE.H"

/* ROOT自己的私有桥，启动时SC32API尚未装载，所以不能调用
 * ScDebug*，也不能把登记回调塞进旧ScRuntimePlatform的ABI尾部。 */
static int scl_debug_path(ScRuntime *r,ScLoaded *m,char out[64])
{
    if(r->platform.read_file!=read_file || r->platform.map_image!=map_image
    || r->platform.protect_image!=protect_image)return 0;
    const char *name=module_name(m),*root=r->platform.context;
    if(!root || system_library(name))root="/SYS/LIB";
    ScU32 n=0;
    while(*root){if(n==62)return -1;
        out[n++]=*root++;
    }
    if(!n || out[n-1]!='/')out[n++]='/';
    while(*name){if(n==63)return -1;
        out[n++]=*name++;
    }out[n]=0;
    return 1;
}
static int scl_debug_register(ScRuntime *r,ScLoaded *m)
{
    char path[64];
    int applicable=scl_debug_path(r,m,path);
    if(applicable<=0)return applicable<0?SC_SCL_RANGE:0;
    ScU32 bytes=0;
    while(path[bytes])bytes++;
    Sc32DebugRegistration input={sizeof(input),1,(ScU32)(ScPointer)m->image,
        (ScU32)(ScPointer)m->metadata,m->metadata_size,(ScU32)(ScPointer)path,bytes+1,0,0,0,{0,0}};
    Sc32DebugRegistrationResult result={sizeof(result),1,0,0};
    int status;
    do{
        status=ScRuntimePlatformCall(0x300,SC32_D2_REGISTER,(int)&input,(int)&result,0,0,0);
        if(status==SC32_PENDING)ScRuntimePlatformCall(7,0,0,0,0,0,0);
    }while(status==SC32_PENDING);
    if(status<0)return status==SC32_E_MEMORY?SC_SCL_MEMORY:SC_SCL_IO;
    m->debug_module=result.module;
    m->debug_generation=result.generation;
    return 0;
}
static void scl_debug_change(ScLoaded *m,ScU32 operation)
{
    if(!m->debug_module)return;
    Sc32DebugRegistration input={sizeof(input),1,(ScU32)(ScPointer)m->image,0,0,0,0,0,
        m->debug_module,m->debug_generation,{0,0}};
    /* 用户正常终止与异常回收都在内核复核真实映射寿命。注销失败不
     * 强制信任旧登记，随后MODULEFREE/任务清理仍按真实范围撤销。 */
    (void)ScRuntimePlatformCall(0x300,(int)operation,(int)&input,0,0,0,0);
    if(operation==SC32_D2_UNREGISTER)m->debug_module=m->debug_generation=0;
}
static void scl_debug_ready(ScLoaded *m){scl_debug_change(m,SC32_D2_READY);
}
static void scl_debug_unregister(ScLoaded *m){scl_debug_change(m,SC32_D2_UNREGISTER);
}
