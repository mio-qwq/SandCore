#ifndef SANDCORE_SC32FILEWIRE_H
#define SANDCORE_SC32FILEWIRE_H

/* 系统库与内核的私有文件协议；普通应用只使用SC32API.H的cdecl入口。
 * 记录宽度固定，指针字段是本人32位虚址，不接收用户自报PID或代数。 */
#include "../SC32API.H"
#define SC32_FILE_WIRE_VERSION 1u
#define SC32_FILE_IO_MAX 65536u
#define SC32_DIRECTORY_BATCH_MAX 64u
#define SC32_F_OPEN 0x200u
#define SC32_F_READ 0x201u
#define SC32_F_WRITE 0x202u
#define SC32_F_SEEK 0x203u
#define SC32_F_TELL 0x204u
#define SC32_F_FLUSH 0x205u
#define SC32_F_CLOSE 0x206u
#define SC32_F_QUERY 0x207u
#define SC32_F_TRUNCATE 0x208u
#define SC32_F_DUPLICATE 0x209u
#define SC32_F_STANDARD 0x20au
#define SC32_D_OPEN 0x220u
#define SC32_D_NEXT 0x221u
#define SC32_D_CLOSE 0x222u
#define SC32_FILE_DIRTY 0x00010000u
#define SC32_FILE_FAULTED 0x00020000u
#define SC32_FILE_UNCERTAIN 0x00040000u
#define SC32_FILE_GROWING 0x00080000u
#define SC32_FILE_STANDARD 0x00100000u
#define SC32_DIRECTORY_END 1u
#define SC32_ENDPOINT_SEEK 1u
#define SC32_ENDPOINT_TELL 2u
#define SC32_ENDPOINT_FLUSH 4u
#define SC32_ENDPOINT_PARENT_PUBLISH 8u
#define SC32_ENDPOINT_FILE 1u
#define SC32_ENDPOINT_PARENT_REPLACE 2u
#define SC32_ENDPOINT_PIPE 3u
#define SC32_ENDPOINT_SERIAL 4u
#define SC32_ENDPOINT_TERMINAL 5u
#define SC32_ENDPOINT_NULL 6u
#define SC32_ENDPOINT_MEMORY 7u

/* OPEN严格32B，与ScFileOpenDesc的字布局相同。目录OPEN也使用此
 * 记录，但flags/create_mode/保留全零；路径只按cwd解析一次。 */
typedef struct {
    ScU32 size,version,path,flags,create_mode,reserved[3];
} Sc32FileOpenRequest;
typedef struct {
    ScU32 size,version,handle,buffer,bytes,reserved[3];
} Sc32FileIoRequest;
typedef struct {
    ScU32 size,version,handle,whence;
    ScI32 offset;
    ScU32 reserved[3];
} Sc32FileSeekRequest;
/* TELL/FLUSH/CLOSE/QUERY/DUP的value=0；TRUNCATE的value为新长度；
 * STANDARD的handle=0、value=0/1/2。余四字均零。 */
typedef struct {
    ScU32 size,version,handle,value,reserved[4];
} Sc32FileHandleRequest;
/* FILE所有输出48B。读取/写入实际IO错误仍返回这个已预验结果，
 * transferred只计成功完成的前缀；其它参数/版本/映射错误不改out。
 * object是独立打开对象身份，revision是当前磁盘目录版本，两者不混用。 */
typedef struct {
    ScU32 size,version,handle,transferred,position,length,object,revision,flags;
    ScI32 raw;
    ScU32 endpoint_kind,endpoint_capabilities;
} Sc32FileReply;
typedef struct {
    ScU32 size,version,handle,rows,capacity,reserved[3];
} Sc32DirectoryNextRequest;
/* DIRECTORY输出32B，rows每项规范96B ScDirectoryEntry，不要求调用者
 * 逐项预置头。整个capacity区间先验RW，成功只写实际count条。
 * 内容或权限版本变化返回STALE，不消费游标；重新OPEN取新视图。 */
typedef struct {
    ScU32 size,version,handle,count,cursor,epoch,flags,reserved;
} Sc32DirectoryReply;

#endif
