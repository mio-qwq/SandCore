#ifndef SANDCORE_SC32PRIVATE_H
#define SANDCORE_SC32PRIVATE_H
#include "../SC32API.H"
#include "../runtime/SCLDR.H"
#include "SC32WIRE.H"

/* 公共含指针记录只用于32位目标。条件数组只做静态ABI核对，不运行。 */
typedef char sc32_types_are_32[(sizeof(ScU32)==4 && sizeof(ScI32)==4 && sizeof(void *)==4)?1:-1];
typedef char sc32_error_is_32[(sizeof(ScErrorInfo)==32)?1:-1];
typedef char sc32_page_is_64[(sizeof(ScProcessPage)==64)?1:-1];
typedef char sc32_job_is_48[(sizeof(ScJobInfo)==48)?1:-1];
typedef char sc32_stat_is_16[(sizeof(ScPathStatInfo)==16)?1:-1];
typedef char sc32_meta_is_48[(sizeof(ScPathInfo)==48)?1:-1];
typedef char sc32_file_open_is_32[(sizeof(ScFileOpenDesc)==32)?1:-1];
typedef char sc32_file_info_is_48[(sizeof(ScFileInfo)==48)?1:-1];
typedef char sc32_file_result_is_32[(sizeof(ScIoResult)==32)?1:-1];
typedef char sc32_directory_entry_is_96[(sizeof(ScDirectoryEntry)==96)?1:-1];
typedef char sc32_directory_page_is_32[(sizeof(ScDirectoryPage)==32)?1:-1];
typedef char sc32_identity_is_32[(sizeof(ScIdentityInfo)==32)?1:-1];
typedef char sc32_session_is_80[(sizeof(ScSessionInfo)==80)?1:-1];
typedef char sc32_session_page_is_32[(sizeof(ScSessionPage)==32)?1:-1];
typedef char sc32_theme_is_128[(sizeof(ScThemeInfo)==128)?1:-1];
typedef char sc32_display_is_64[(sizeof(ScDisplayInfo)==64)?1:-1];
typedef char sc32_library_desc_is_32[(sizeof(ScLibraryDesc)==32)?1:-1];
typedef char sc32_library_info_is_64[(sizeof(ScLibraryInfo)==64)?1:-1];

/* 全部片段由SC32API.C组合为一个真正的SCL；这些函数不导出。 */
static ScI32 sc32_call(ScI32 number,ScI32 a,ScI32 b,ScI32 c,ScI32 d,ScI32 e,ScI32 f);
static void sc32_zero(void *out,ScU32 bytes);
static void sc32_copy(void *out,const void *input,ScU32 bytes);
static ScStatus sc32_probe(const void *address,ScU32 bytes,ScU32 access);
static ScStatus sc32_out(void *out,ScU32 bytes);
static ScStatus sc32_record_out(void *out,ScU32 bytes);
static ScStatus sc32_record_in(const void *input,ScU32 bytes);
static ScStatus sc32_string(const char *text,ScU32 maximum,ScU32 *length);
static ScStatus sc32_fail(ScI32 code,ScU32 domain,ScI32 raw,ScU32 operation);
static ScStatus sc32_invalid(ScI32 code,ScU32 operation);
static ScStatus sc32_native(ScI32 raw,ScU32 operation);
static ScStatus sc32_legacy(ScI32 raw,ScU32 operation);
static ScStatus sc32_runtime_error(ScI32 raw,ScU32 operation);
static ScStatus sc32_absolute(const char *path,char out[65],ScU32 operation);
static ScStatus sc32_text_output(const char *text,ScU32 bytes,char *out,ScU32 capacity,ScU32 *length,ScU32 operation);
static ScStatus sc32_text_buffers(char *out,ScU32 capacity,ScU32 *length);
static int sc32_overlap(const void *a,ScU32 a_bytes,const void *b,ScU32 b_bytes);

#define SC32_OP_ERROR_QUERY 1u
#define SC32_OP_ERROR_NAME 2u
#define SC32_OP_ERROR_FORMAT 3u
#define SC32_OP_CAPS 0x100u
#define SC32_OP_MEMORY 0x101u
#define SC32_OP_MONOTONIC 0x102u
#define SC32_OP_CLOCK 0x103u
#define SC32_OP_CPU 0x104u
#define SC32_OP_STORAGE 0x105u
#define SC32_OP_WAIT 0x106u
#define SC32_OP_SELF 0x200u
#define SC32_OP_PROCESS_QUERY 0x201u
#define SC32_OP_PROCESS_PAGE 0x202u
#define SC32_OP_TERMINATE 0x203u
#define SC32_OP_SPAWN 0x204u
#define SC32_OP_JOB_QUERY 0x205u
#define SC32_OP_JOB_WAIT 0x206u
#define SC32_OP_JOB_CLOSE 0x207u
#define SC32_OP_JOB_CANCEL 0x208u
#define SC32_OP_YIELD 0x209u
#define SC32_OP_SLEEP 0x20au
#define SC32_OP_ARGUMENTS 0x20bu
#define SC32_OP_ENV 0x20cu
#define SC32_OP_ENV_SET 0x20du
#define SC32_OP_CWD 0x20eu
#define SC32_OP_CWD_SET 0x20fu
#define SC32_OP_RESOLVE 0x300u
#define SC32_OP_STAT 0x301u
#define SC32_OP_META 0x302u
#define SC32_OP_CREATE 0x303u
#define SC32_OP_REMOVE 0x304u
#define SC32_OP_RENAME 0x305u
#define SC32_OP_PERMISSIONS 0x306u
#define SC32_OP_PATH_READ 0x307u
#define SC32_OP_PATH_REPLACE 0x308u
#define SC32_OP_ALLOC 0x400u
#define SC32_OP_FREE 0x401u
#define SC32_OP_IDENTITY 0x500u
#define SC32_OP_SESSION_PAGE 0x501u
#define SC32_OP_SESSION_CONTROL 0x502u
#define SC32_OP_SESSION_PATH 0x503u
#define SC32_OP_THEME 0x504u
#define SC32_OP_THEME_APPLY 0x505u
#define SC32_OP_THEME_PATH 0x506u
#define SC32_OP_DISPLAY 0x507u
#define SC32_OP_DISPLAY_APPLY 0x508u
#define SC32_OP_LOAD_LIBRARY 0x600u
#define SC32_OP_SYMBOL 0x601u
#define SC32_OP_FREE_LIBRARY 0x602u
#define SC32_OP_LIBRARY_QUERY 0x603u
#define SC32_OP_FILE_BASE 0x700u
#endif
