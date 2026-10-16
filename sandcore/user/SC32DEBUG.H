#ifndef SANDCORE_SC32DEBUG_H
#define SANDCORE_SC32DEBUG_H
#include "SC32API.H"

/* 此头只有稳定三环类型/声明。所有地址均为目标32位用户虚址；
 * 进程、绑定、暂停、模块四种代数不能互相替代，也不是内核指针。 */
#define SC32_DEBUG_VERSION 1u
#define SC32_DEBUG_CAP_HI 8u
#define SC32_DEBUG_RUNNING 1u
#define SC32_DEBUG_EXITED 2u
#define SC32_DEBUG_PAUSED 3u
#define SC32_DEBUG_STOP_ENTRY 1u
#define SC32_DEBUG_STOP_PAUSE 2u
#define SC32_DEBUG_STOP_STEP 3u
#define SC32_DEBUG_STOP_BREAKPOINT 4u
#define SC32_DEBUG_STOP_EXCEPTION 5u
#define SC32_DEBUG_REGISTERS_VALID 1u
#define SC32_DEBUG_READ_ORIGINAL_CODE 1u
#define SC32_DEBUG_CONTINUE 1u
#define SC32_DEBUG_PAUSE 2u
#define SC32_DEBUG_STEP 3u
#define SC32_DEBUG_TERMINATE 4u
#define SC32_DEBUG_READ_MAX 4096u
#define SC32_DEBUG_PAGE_MAX 64u
#define SC32_DEBUG_MAIN 1u
#define SC32_DEBUG_SCL 2u
#define SC32_DEBUG_VERIFIED_SOURCE 1u
#define SC32_DEBUG_MODULE_INITIALIZING 2u
#define SC32_DEBUG_MODULE_READY 4u
#define SC32_DEBUG_READABLE 1u
#define SC32_DEBUG_WRITABLE 2u
#define SC32_DEBUG_EXECUTABLE 4u
#define SC32_DEBUG_ACCESS_MIXED 8u
#define SC32_DEBUG_BP_ENABLED 1u
#define SC32_DEBUG_BP_PENDING 2u
#define SC32_DEBUG_BP_INVALID 4u
#define SC32_DEBUG_SYMBOL_EXPORTED 1u
#define SC32_DEBUG_SYMBOL_FUNCTION 1u
#define SC32_DEBUG_SYMBOL_DATA 2u

typedef struct { ScU32 size,version,pid,generation,binding,reserved; } ScDebugRef;
typedef struct {
    ScU32 size,version;
    const char *command;
    ScI32 streams[3];
    ScU32 flags,reserved;
} ScDebugLaunchDesc;
typedef struct {
    ScU32 size,version; ScDebugRef target; ScJob job; ScU32 reserved[3];
} ScDebugLaunchInfo;
typedef struct {
    ScU32 size,version,eax,ebx,ecx,edx,esi,edi,ebp,esp,eip,eflags;
    ScU32 cs,ss,ds,es,fs,gs,vector,error,cr2,reserved[3];
} ScDebugRegisters;
typedef struct {
    ScU32 size,version; ScDebugRef target;
    ScU32 state,stop_generation,reason,module_epoch,breakpoint_epoch,flags;
    ScI32 exit_code; ScU32 reserved;
    ScDebugRegisters registers;
} ScDebugSnapshot;
typedef struct {
    ScU32 size,version,stop_generation,address,requested,readable,flags,reserved;
} ScDebugReadInfo;
typedef struct {
    ScU32 size,version,id,generation,address,flags,hits,stop_generation;
} ScDebugBreakpoint;
typedef struct {
    ScU32 size,version,id,generation,kind,base,image_bytes,allocation_bytes;
    ScU32 abi_major,abi_minor,library_version,flags,segment_count,symbol_count;
    ScU32 source_bytes,source_revision;
    unsigned char sha256[32];
    char name[40],source[SC32_PATH_CAPACITY];
} ScDebugModule;
typedef struct {
    ScU32 size,version,module,module_generation,index,address,bytes,access;
} ScDebugSegment;
typedef struct {
    ScU32 size,version,module,module_generation,index,address,bytes,kind,flags;
    char name[128];
    ScU32 reserved;
} ScDebugSymbol;
typedef struct {
    ScU32 size,version,stop_generation,epoch,cursor,count,total,flags;
} ScDebugPage;
#define SC32_DEBUG_PAGE_FINISHED 1u

/* 所有输出先填size/version；错误保持输出，较大记录的未知尾部不写。
 * Launch创建的目标由该调试器独占，关闭调试器时内核终止并回收目标。
 * 不接受猜PID附加；已有目标引用只有真实拥有者可以使用。 */
ScStatus ScDebugLaunch(const ScDebugLaunchDesc *,ScDebugLaunchInfo *out);
ScStatus ScDebugQuery(const ScDebugRef *,ScDebugSnapshot *out);
ScStatus ScDebugControl(const ScDebugRef *,ScU32 stop_generation,ScU32 action,ScDebugSnapshot *out);
/* data/readable各bytes字节，readable[i]为0/1；不可读字节data为0且不作
 * 有效数据。所有片段与result属于同一暂停代数，运行状态拒绝读取。 */
ScStatus ScDebugRead(const ScDebugRef *,ScU32 stop_generation,ScU32 address,ScU32 bytes,
    ScU32 flags,void *data,unsigned char *readable,ScDebugReadInfo *result);
ScStatus ScDebugBreakpointAdd(const ScDebugRef *,ScU32 stop_generation,ScU32 address,ScDebugBreakpoint *out);
ScStatus ScDebugBreakpointRemove(const ScDebugRef *,ScU32 stop_generation,ScU32 id,ScU32 generation);
ScStatus ScDebugBreakpoints(const ScDebugRef *,ScU32 stop_generation,ScU32 after,ScU32 expected_epoch,
    ScDebugBreakpoint *rows,ScU32 capacity,ScDebugPage *out);
ScStatus ScDebugModules(const ScDebugRef *,ScU32 stop_generation,ScU32 after,ScU32 expected_epoch,
    ScDebugModule *rows,ScU32 capacity,ScDebugPage *out);
ScStatus ScDebugSegments(const ScDebugRef *,ScU32 stop_generation,ScU32 module,ScU32 module_generation,
    ScU32 after,ScDebugSegment *rows,ScU32 capacity,ScDebugPage *out);
ScStatus ScDebugSymbols(const ScDebugRef *,ScU32 stop_generation,ScU32 module,ScU32 module_generation,
    ScU32 after,ScDebugSymbol *rows,ScU32 capacity,ScDebugPage *out);

#endif
