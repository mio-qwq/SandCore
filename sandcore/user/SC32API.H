#ifndef SANDCORE_SC32API_H
#define SANDCORE_SC32API_H

#include "SCTYPES.H"

/* 正式cdecl API。只声明本基础切片的真实入口，寄存器桥留在库内部。
 * 记录调用前填size/version；成功返回规范size，未定义尾部保持不变。 */
#define SC32_ABI_MAJOR 1u
#define SC32_ABI_MINOR 2u
#define SC32_LIBRARY_VERSION 3u
#define SC32_RECORD_VERSION 1u
#define SC32_PATH_CAPACITY 65u
#define SC32_INFINITE 0xFFFFFFFFu
#define SC32_ENUM_BEGIN 0xFFFFFFFFu
#define SC32_PATH_IO_MAX 65536u
#define SC32_SPAWN_NO_STREAM_INHERIT 1u
#define SC32_EVENT_INPUT 1u
#define SC32_EVENT_VISIBILITY 2u
#define SC32_EVENT_STREAM 4u
#define SC32_EVENT_JOB 8u
#define SC32_EVENT_NETWORK 16u
#define SC32_EVENT_AUDIO 32u
#define SC32_EVENT_IMAGE 64u
#define SC32_EVENT_AUTH 128u
#define SC32_EVENT_TIMEOUT 0x40000000u
#define SC32_OK 0
#define SC32_PENDING 1
#define SC32_TIMEOUT 2
#define SC32_RESIDENT 3
#define SC32_E_INVALID (-101)
#define SC32_E_VERSION (-102)
#define SC32_E_ACCESS (-103)
#define SC32_E_NOT_FOUND (-104)
#define SC32_E_EXISTS (-105)
#define SC32_E_MEMORY (-106)
#define SC32_E_NO_SPACE (-107)
#define SC32_E_STALE (-108)
#define SC32_E_AGAIN (-109)
#define SC32_E_BROKEN_PIPE (-110)
#define SC32_E_IO (-111)
#define SC32_E_NOT_SUPPORTED (-112)
#define SC32_E_BUSY (-113)
#define SC32_E_RANGE (-114)
#define SC32_E_CANCELLED (-115)
#define SC32_E_FAILED (-116)
#define SC32_DOMAIN_LIBRARY 1u
#define SC32_DOMAIN_KERNEL 2u
#define SC32_DOMAIN_LEGACY 3u
#define SC32_DOMAIN_SCLDR 4u

#define SC32_CAP_CORE_QUERY 1u
#define SC32_CAP_PROCESS 2u
#define SC32_CAP_PRIVATE_PAGES 4u
#define SC32_CAP_PATH_IO 8u
#define SC32_CAP_DIRECTORIES 16u
#define SC32_CAP_JOBS_STREAMS 32u
#define SC32_CAP_SESSIONS 64u
#define SC32_CAP_THEMES 128u
#define SC32_CAP_DISPLAY 256u
#define SC32_CAP_LIBRARIES 512u
#define SC32_CAP_MONOTONIC 1024u
#define SC32_CAP_UTC_CLOCK 2048u
#define SC32_CAP_WINDOW_MESSAGES 4096u
#define SC32_CAP_UPDATE_FILES 8192u
#define SC32_CAP_CLIPBOARD 16384u
#define SC32_CAP_IPC 32768u
#define SC32_CAP_SHARED_MEMORY 65536u
#define SC32_CAP_CALLBACK_LIFETIME 131072u

typedef struct {
    ScU32 size,version;
    ScI32 code;
    ScU32 domain;
    ScI32 raw;
    ScU32 operation,reserved[2];
} ScErrorInfo;
typedef struct {
    ScU32 size,version,abi_major,abi_minor,kernel_contract,syscall_gate;
    ScU32 capabilities_lo,capabilities_hi,max_path_bytes,max_arguments_bytes;
    ScU32 page_size,pit_hz,heap_bytes,module_bytes,reserved[2];
} ScCapabilities;
typedef struct { ScU32 size,version,pid,generation; } ScProcessRef;
#define SC32_PROCESS_EMPTY 0u
#define SC32_PROCESS_RUNNING 1u
#define SC32_PROCESS_EXITING 2u
#define SC32_PROCESS_PAUSED 3u
#define SC32_PROCESS_DETAILS 1u
#define SC32_PROCESS_REALM_VALID 2u
#define SC32_REALM_UNKNOWN 0xFFFFFFFFu
typedef struct {
    ScU32 size,version,pid,generation,state;
    ScI32 uid,gid;
    ScU32 realm,session,flags;
    ScI32 exit_code;
    ScU32 waiting,wait_mask,pending_events,running_samples,dispatches;
    char name[16];
    ScU32 reserved[4];
} ScProcessInfo;
typedef struct {
    ScU32 size,version,cursor,count,tick,total_samples,idle_samples;
    ScU32 kernel_samples,user_samples,graphics_samples,hz,examined,reserved[4];
} ScProcessPage;
typedef ScU32 ScJob;
#define SC32_JOB_FINISHED 0u
#define SC32_JOB_RUNNING 1u
#define SC32_JOB_PAUSED 3u
typedef struct {
    ScU32 size,version,state;
    ScI32 exit_code;
    ScU32 child_pid,child_generation,original_pid,owned,reserved[4];
} ScJobInfo;
typedef struct {
    ScU32 size,version,page_size,managed_bytes,used_bytes,free_bytes;
    ScU32 heap_base,heap_bytes,module_base,module_bytes;
    ScU32 process_heap_pages,process_module_pages,process_total_pages;
    ScU32 page_table_pages,reserved[2];
} ScMemoryInfo;
typedef struct {
    ScU32 size,version;
    ScWideCounter ticks;
    ScU32 hz,reserved[3];
} ScMonotonicClock;
typedef struct {
    ScU32 size,version,year,month,day,hour,minute,second;
    ScI32 utc_offset_seconds;
    ScU32 reserved[3];
} ScUtcClock;
typedef struct {
    ScU32 size,version,hz,tick,total,idle,kernel,user,graphics,method;
    ScU32 reserved[6];
} ScCpuInfo;
typedef struct {
    ScU32 size,version,mounted,format,disk_sectors,data_start,data_end;
    ScU32 directory_capacity,record_count,live_sectors,reserved_sectors;
    ScU32 free_sectors,largest_free_run,object_sequence,active_bank;
    ScU32 metadata_generation,faulted,reserved[3];
} ScStorageInfo;

#define SC32_PATH_FILE 1u
#define SC32_PATH_DIRECTORY 2u
typedef struct { ScU32 size,version,kind,bytes; } ScPathStatInfo;
typedef struct {
    ScU32 size,version,kind,bytes;
    ScI32 uid,gid;
    ScU32 rw,flags,generation,reserved[3];
} ScPathInfo;
typedef ScU32 ScFile;
typedef ScU32 ScDirectory;
#define SC32_FILE_READ 1u
#define SC32_FILE_WRITE 2u
#define SC32_FILE_CREATE 4u
#define SC32_FILE_TRUNCATE 8u
#define SC32_FILE_APPEND 16u
#define SC32_SEEK_SET 0u
#define SC32_SEEK_CUR 1u
#define SC32_SEEK_END 2u
typedef struct {
    ScU32 size,version;
    const char *path;
    ScU32 flags,create_mode,reserved[3];
} ScFileOpenDesc;
typedef struct {
    ScU32 size,version,handle,position,length,object_id,revision,flags;
    ScU32 endpoint_kind,endpoint_capabilities,reserved[2];
} ScFileInfo;
typedef struct {
    ScU32 size,version,transferred,position,length,object_id,revision,flags;
} ScIoResult;
typedef struct {
    ScU32 size,version,kind,length,generation;
    ScI32 uid,gid;
    ScU32 rw;
    char name[64];
} ScDirectoryEntry;
typedef struct {
    ScU32 size,version,handle,count,cursor,generation,flags,reserved;
} ScDirectoryPage;
#define SC32_DIRECTORY_FINISHED 1u
#define SC32_FILE_STATE_DIRTY 0x00010000u
#define SC32_FILE_STATE_FAULTED 0x00020000u
#define SC32_FILE_STATE_UNCERTAIN 0x00040000u
#define SC32_FILE_STATE_GROWING 0x00080000u
#define SC32_FILE_STATE_STANDARD 0x00100000u
/* 只有STANDARD对象解释endpoint_*；普通现代文件按文件ABI支持定位/刷新。 */
#define SC32_ENDPOINT_FILE 1u
#define SC32_ENDPOINT_PARENT_REPLACE 2u
#define SC32_ENDPOINT_PIPE 3u
#define SC32_ENDPOINT_SERIAL 4u
#define SC32_ENDPOINT_TTY 5u
#define SC32_ENDPOINT_NULL 6u
#define SC32_ENDPOINT_MEMORY 7u
#define SC32_ENDPOINT_SEEK 1u
#define SC32_ENDPOINT_TELL 2u
#define SC32_ENDPOINT_FLUSH 4u
#define SC32_ENDPOINT_PARENT_PUBLISH 8u

#define SC32_IDENTITY_MANAGE 1u
#define SC32_IDENTITY_MODULES 2u
typedef struct {
    ScU32 size,version;
    ScI32 uid,gid;
    ScU32 realm,flags,task_generation,session;
} ScIdentityInfo;
typedef struct {
    ScU32 size,version,id;
    ScI32 uid,gid;
    ScU32 kind,visible,closing,members,generation;
    char name[32];
    ScU32 reserved[2];
} ScSessionInfo;
typedef struct {
    ScU32 size,version,cursor,count,current_session,visible_session;
    ScU32 reserved[2];
} ScSessionPage;
#define SC32_SESSION_SHOW 0u
#define SC32_SESSION_LOGOUT 1u
#define SC32_SESSION_LOGIN_PANEL 2u
typedef struct {
    ScU32 size,version,style,generation,title_height,taskbar_height,radius,border;
    ScU32 colors[24];
} ScThemeInfo;
#define SC32_COLOR_FACE 0u
#define SC32_COLOR_FACE_ALT 1u
#define SC32_COLOR_PAPER 2u
#define SC32_COLOR_TEXT 3u
#define SC32_COLOR_MUTED 4u
#define SC32_COLOR_LINE 5u
#define SC32_COLOR_ACCENT 6u
#define SC32_COLOR_ACCENT_DEEP 7u
#define SC32_COLOR_SELECT 8u
#define SC32_COLOR_SELECT_TEXT 9u
#define SC32_COLOR_SHADOW 10u
#define SC32_COLOR_ALERT 11u
#define SC32_COLOR_GOLD 12u
#define SC32_COLOR_GREEN 13u
#define SC32_COLOR_VIOLET 14u
#define SC32_COLOR_ROSE 15u
#define SC32_COLOR_HIGHLIGHT 16u
#define SC32_COLOR_DARK_EDGE 17u
#define SC32_COLOR_TITLE_ACTIVE 18u
#define SC32_COLOR_TITLE_INACTIVE 19u
#define SC32_COLOR_TITLE_TEXT 20u
#define SC32_COLOR_TITLE_MUTED 21u
#define SC32_COLOR_DESKTOP_TEXT 22u
#define SC32_COLOR_DESKTOP_SHADOW 23u
#define SC32_STYLE_AURORA 0u
#define SC32_STYLE_CLASSIC 1u
#define SC32_THEME_TEMPORARY 0u
#define SC32_THEME_SAVE 1u
#define SC32_THEME_RELOAD 2u
#define SC32_THEME_WALLPAPER 0u
#define SC32_THEME_ICON_ROOT 1u
#define SC32_THEME_NAME 2u
typedef struct {
    ScU32 size,version,width,height,scale_percent,backend,generation;
    ScU32 supported_modes,preview_seconds,max_width,reserved[6];
} ScDisplayInfo;
#define SC32_DISPLAY_PREVIEW 0u
#define SC32_DISPLAY_CONFIRM 1u
#define SC32_DISPLAY_CANCEL 2u

typedef ScU32 ScLibrary;
#define SC32_SYMBOL_FUNCTION 1u
#define SC32_SYMBOL_DATA 2u
typedef struct {
    ScU32 size,version;
    const char *name;
    ScU32 abi_major,min_minor,min_version,reserved[2];
} ScLibraryDesc;
typedef struct {
    ScU32 size,version,modules,image_bytes,metadata_bytes,pinned_modules;
    ScU32 loads,reuses,relocations,reserved[7];
} ScLibraryInfo;

ScStatus ScErrorQuery(ScErrorInfo *out);
ScStatus ScErrorName(ScI32 code,char *out,ScU32 capacity,ScU32 *required);
ScStatus ScErrorFormat(const ScErrorInfo *error,char *out,ScU32 capacity,ScU32 *required);
ScStatus ScSystemCapabilities(ScCapabilities *out);
ScStatus ScSystemMemory(ScMemoryInfo *out);
ScStatus ScSystemMonotonic(ScMonotonicClock *out);
ScStatus ScSystemClock(ScUtcClock *out);
ScStatus ScSystemCpu(ScCpuInfo *out);
ScStatus ScSystemStorage(ScStorageInfo *out);
ScStatus ScSystemWait(ScU32 mask,ScU32 timeout_ticks,ScU32 *events);
ScStatus ScProcessSelf(ScProcessRef *out);
ScStatus ScProcessQuery(const ScProcessRef *process,ScProcessInfo *out);
ScStatus ScProcessEnumerate(ScProcessPage *page,ScProcessInfo *rows,ScU32 capacity);
ScStatus ScProcessTerminate(const ScProcessRef *process);
ScStatus ScProcessSpawn(const char *command,const ScI32 streams[3],ScU32 flags,ScJob *out);
void ScProcessExit(ScI32 code);
ScStatus ScProcessYield(void);
ScStatus ScProcessSleep(ScU32 ticks);
ScStatus ScProcessArguments(char *out,ScU32 capacity,ScU32 *length);
ScStatus ScProcessEnvironment(const char *name,char *out,ScU32 capacity,ScU32 *length);
ScStatus ScProcessSetEnvironment(const char *name,const char *value,ScU32 remove);
ScStatus ScProcessWorkingDirectory(char *out,ScU32 capacity,ScU32 *length);
ScStatus ScProcessSetWorkingDirectory(const char *path);
ScStatus ScJobQuery(ScJob job,ScJobInfo *out);
ScStatus ScJobWait(ScJob job,ScU32 timeout_ticks,ScJobInfo *out);
ScStatus ScJobClose(ScJob job);
ScStatus ScJobCancel(ScJob job);
ScStatus ScPathResolve(const char *path,char *out,ScU32 capacity,ScU32 *length);
ScStatus ScPathStat(const char *path,ScPathStatInfo *out);
ScStatus ScPathMetadata(const char *path,ScPathInfo *out);
ScStatus ScPathCreate(const char *path,ScU32 kind,ScU32 rw);
ScStatus ScPathRemove(const char *path);
ScStatus ScPathRename(const char *before,const char *after);
ScStatus ScPathPermissions(const char *path,ScI32 uid,ScI32 gid,ScU32 rw);
ScStatus ScPathRead(const char *path,ScU32 offset,void *out,ScU32 capacity,ScU32 *transferred);
ScStatus ScPathReplace(const char *path,const void *data,ScU32 bytes,ScU32 *transferred);
ScStatus ScMemoryAllocatePages(ScU32 bytes,void **out);
ScStatus ScMemoryFreePages(void *base);
ScStatus ScIdentityQuery(ScIdentityInfo *out);
ScStatus ScSessionEnumerate(ScSessionPage *page,ScSessionInfo *rows,ScU32 capacity);
ScStatus ScSessionControl(ScU32 session,ScU32 action);
ScStatus ScSessionPath(const char *leaf,char *out,ScU32 capacity,ScU32 *length);
ScStatus ScThemeQuery(ScThemeInfo *out);
ScStatus ScThemeApply(const char *path,ScU32 action);
ScStatus ScThemePath(ScU32 kind,char *out,ScU32 capacity,ScU32 *length);
ScStatus ScDisplayQuery(ScDisplayInfo *out);
ScStatus ScDisplayApply(ScU32 width,ScU32 height,ScU32 scale,ScU32 action);
ScStatus ScLoadLibrary(const ScLibraryDesc *desc,ScLibrary *out);
ScStatus ScGetSymbol(ScLibrary library,const char *name,ScU32 kind,ScU32 minimum_size,void **out);
ScStatus ScFreeLibrary(ScLibrary library);
ScStatus ScLibraryQuery(ScLibraryInfo *out);
/* 持久回调先固定所属代码映像到进程终止；只接受本人实际函数域。 */
ScStatus ScCallbackPin(const void *function_address);

/* 普通文件对象由独立FILE切片提供，声明与实际桥/内核同批收齐。
 * 这不是旧顺序替换流的别名；更新模式与保持打开的flush见规范。 */
ScStatus ScFileOpen(const ScFileOpenDesc *desc,ScFile *out);
ScStatus ScFileRead(ScFile file,void *out,ScU32 bytes,ScIoResult *result);
ScStatus ScFileWrite(ScFile file,const void *data,ScU32 bytes,ScIoResult *result);
ScStatus ScFileSeek(ScFile file,ScI32 offset,ScU32 whence,ScFileInfo *out);
ScStatus ScFileTell(ScFile file,ScU32 *position);
ScStatus ScFileFlush(ScFile file);
ScStatus ScFileClose(ScFile file);
ScStatus ScFileQuery(ScFile file,ScFileInfo *out);
ScStatus ScFileTruncate(ScFile file,ScU32 bytes);
ScStatus ScFileDuplicate(ScFile file,ScFile *out);
ScStatus ScFileGetStandard(ScU32 index,ScFile *out);
ScStatus ScDirectoryOpen(const char *path,ScDirectory *out);
ScStatus ScDirectoryNext(ScDirectory directory,ScDirectoryEntry *rows,ScU32 capacity,ScDirectoryPage *page);
ScStatus ScDirectoryClose(ScDirectory directory);

#include "SC32GUI.H"
#include "SC32FONT.H"
#include "SC32PALETTE.H"
#include "SC32POPUP.H"

#endif
