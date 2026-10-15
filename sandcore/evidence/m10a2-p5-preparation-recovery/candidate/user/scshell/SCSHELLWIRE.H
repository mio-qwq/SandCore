#ifndef SANDCORE_SCSHELLWIRE_H
#define SANDCORE_SCSHELLWIRE_H
#include <SC32BROKER.H>

/* i386 LE值协议，不能传指针/FILE/lease/foreigncontext。Header后是body；
 * body内offset均相对完整body起点。帧的真实身份只认Packet.sender/Peer。
 * 本草稿尚未接线或执行；worker与client共用该唯一合同，不复制一套。 */
enum {
    SHW_VERSION=1,SHW_FRAME_BYTES=65536,SHW_HEADER_BYTES=48,
    SHW_MORE=1,SHW_REPLY=2,SHW_EVENT=4,
    SHW_HELLO=0x200,SHW_HELLO_ACK=0x201,
    SHW_JOB_PLAN=0x210,SHW_JOB_COMMIT=0x211,SHW_JOB_ACCEPTED=0x212,
    SHW_JOB_PROGRESS=0x220,SHW_JOB_CONFLICT=0x221,SHW_JOB_ITEM=0x222,SHW_JOB_RESULT=0x223,
    SHW_JOB_RESOLVE=0x230,SHW_JOB_CANCEL=0x231,SHW_JOB_CANCELLED_ACK=0x232,SHW_JOB_QUERY=0x233,
    SHW_COPY=1,SHW_MOVE=2,SHW_DELETE=3,SHW_RENAME=4,SHW_MKDIR=5,
    SHW_RECURSIVE=1,SHW_PRESERVE_MODE=2,
    SHW_ITEM_EXPECT_SOURCE=1,SHW_ITEM_EXPECT_DESTINATION=2,
    SHW_FEATURE_COPY=1,SHW_FEATURE_MOVE=2,SHW_FEATURE_DELETE=4,
    SHW_FEATURE_RENAME=8,SHW_FEATURE_MKDIR=16,SHW_FEATURE_RECURSIVE=32,
    SHW_FEATURE_CONFLICT=64,SHW_FEATURE_JOURNAL=128,
    SHW_CONNECTING=1,SHW_PLANNING=2,SHW_RUNNING=3,SHW_CONFLICT=4,
    SHW_CANCELLING=5,SHW_FINISHED=6,SHW_CANCELLED=7,SHW_FAILED=8,SHW_UNKNOWN=9,
    SHW_STAGE_NONE=0,SHW_STAGE_CANDIDATE=1,SHW_STAGE_FLUSHED=2,
    SHW_STAGE_DESTINATION=3,SHW_STAGE_RENAME=4,SHW_STAGE_SOURCE_REMOVED=5,
    SHW_STAGE_DELETED=6,SHW_STAGE_DIRECTORY=7,
    SHW_RESULT_DONE=1,SHW_RESULT_SKIPPED=2,SHW_RESULT_SOURCE_RETAINED=3,
    SHW_RESULT_FAILED=4,SHW_RESULT_CANCELLED=5,SHW_RESULT_UNCERTAIN=6,
    SHW_DURABLE=1,SHW_SOURCE_RETAINED=2,SHW_OBSERVATION_LIVE=4,SHW_RECOVERY_WRITTEN=8,
    SHW_PROGRESS_PRIVATE_PREFIX=1,
    SHW_DECISION_REPLACE=1,SHW_DECISION_SKIP=2,SHW_DECISION_RENAME=3,
    SHW_DECISION_MERGE=4,SHW_DECISION_RETRY=5,SHW_DECISION_CANCEL=6
};

typedef struct {
    ScU32 size,version,opcode,flags,request_lo,request_hi,body_bytes;
    ScU32 offset,total_bytes,state_epoch;
    ScI32 status;
    ScU32 reserved;
} ScShellWireHeader;

typedef struct {
    ScU32 size,version,protocol,features,job_lo,job_hi,max_frame_bytes,flags;
} ScShellWireHello;

/* PLAN整体从64B Begin、count个48B Item及字符串组成。所有字符串
 * 唯一末NUL+4B零padding，绝对规范路径最多65B含NUL。recovery必须
 * 是caller可写且不在任一source/target受锁子树内的独立普通路径。 */
typedef struct {
    ScU32 size,version,operation,flags,job_lo,job_hi,count,items_offset;
    ScU32 destination_offset,destination_bytes,recovery_offset,recovery_bytes;
    ScU32 conflict_policy,final_mode,reserved[2];
} ScShellWireJobBegin;
typedef struct {
    ScU32 size,version,item_id,kind,source_offset,source_bytes;
    ScU32 destination_offset,destination_bytes,expected_source_revision;
    ScU32 expected_destination_revision,expected_destination_kind,flags;
} ScShellWireJobItem;

typedef struct {
    ScU32 size,version,job_lo,job_hi,item_id,flags,reserved[2];
} ScShellWireJobControl;
/* 只有当前conflict epoch和显式观察相等才可继续；RENAME时后跟
 * new_path_bytes的绝对新目标（含NUL/零padding），其它decision为0。 */
typedef struct {
    ScU32 size,version,job_lo,job_hi,item_id,conflict_epoch,decision;
    ScU32 expected_source_revision,expected_destination_revision,new_path_bytes;
} ScShellWireJobDecision;

typedef struct {
    ScU32 size,version,job_lo,job_hi,state,stage,current_item;
    ScU32 items_total,items_done,items_failed,items_skipped,total_known;
    ScU32 bytes_lo,bytes_hi,total_lo,total_hi,conflict_epoch;
    ScI32 status,cause;
    ScU32 domain;
    ScI32 raw;
    ScU32 flags,reserved[2];
} ScShellWireJobProgress;

/* object只在OBSERVATION_LIVE且真实owner-open pin仍持有时非0；不是
 * persistent birthID。PID/gen正文必须与真实Packet.sender相等，正文
 * 自报身份永远不授权。DROP Complete不是这个durable worker ledger。 */
typedef struct {
    ScU32 size,version,job_lo,job_hi,item_id,operation,state,stage;
    ScI32 status,cause;
    ScU32 domain;
    ScI32 raw;
    ScU32 bytes_lo,bytes_hi,source_object,source_revision,destination_object;
    ScU32 destination_revision,conflict_epoch,flags,worker_pid,worker_generation,reserved[2];
    char source[68],destination[68];
} ScShellWireJobAck;

typedef char shw_header_48[(sizeof(ScShellWireHeader)==48)?1:-1];
typedef char shw_hello_32[(sizeof(ScShellWireHello)==32)?1:-1];
typedef char shw_begin_64[(sizeof(ScShellWireJobBegin)==64)?1:-1];
typedef char shw_item_48[(sizeof(ScShellWireJobItem)==48)?1:-1];
typedef char shw_control_32[(sizeof(ScShellWireJobControl)==32)?1:-1];
typedef char shw_decision_40[(sizeof(ScShellWireJobDecision)==40)?1:-1];
typedef char shw_progress_96[(sizeof(ScShellWireJobProgress)==96)?1:-1];
typedef char shw_ack_232[(sizeof(ScShellWireJobAck)==232)?1:-1];

/* 服务与worker共享头，不共享角色。0x100族只接受由HostConnect确认的
 * 真host；worker 0x200族仍核ScJob实际child。 */
enum {
    SHW_SERVICE_HELLO=0x100,SHW_SERVICE_HELLO_ACK=0x101,
    SHW_CATALOG_QUERY=0x110,SHW_REGISTER=0x120,SHW_UNREGISTER=0x121,
    SHW_ASSOCIATION=0x122,SHW_LAYOUT=0x123,SHW_CATALOG_CHANGED=0x131,
    SHW_CATALOG_MAGIC=0x54414353,SHW_SHORTCUT_MAGIC=0x324C4353,
    SHW_SERVICE_FEATURE_CATALOG=1,SHW_SERVICE_FEATURE_ASSOCIATION=2,
    SHW_SERVICE_FEATURE_LAYOUT=4,SHW_SERVICE_FEATURE_ACCOUNT_STORE=8
};
typedef struct {
    ScU32 size,version,protocol,features,host_epoch,catalog_epoch,max_frame_bytes,flags;
} ScShellWireServiceHello;
typedef struct {ScU32 size,version,expected_epoch,flags;} ScShellWireQuery;
typedef struct {ScU32 size,version,epoch,token,revision,flags,reserved[2];} ScShellWireReply;
typedef struct {
    ScU32 size,version,layer,flags,id_offset,id_bytes,expected_revision,reserved;
} ScShellWireName;
/* 文件/QUERY快照统一64B头；CRC仅涵盖其后body。epoch是活动快照，
 * 不能把文件revision/host_epoch/内核generation当成它。 */
typedef struct {
    ScU32 magic,version,header_bytes,body_bytes,crc,epoch,next_token;
    ScU32 applications,associations,layout_items,layout_mode,grid_width,grid_height;
    ScU32 layout_revision,reserved[2];
} ScShellWireCatalog;
/* record_bytes涵盖本条固定头/verb数组/所有字符串，offset相对本条。
 * 注册请求必须token/revision/owner/uid/gid为0，服务覆盖真实author。
 * 本地Export/Import可携带这些值，但不产生任何OS权限。 */
typedef struct {
    ScU32 size,version,token,revision,layer,flags,owner_pid,owner_generation;
    ScI32 uid,gid;ScU32 profile,verb_count,verbs_offset;
    ScU32 id_offset,id_bytes,label_offset,label_bytes,executable_offset,executable_bytes;
    ScU32 arguments_offset,arguments_bytes,icon_offset,icon_bytes,category_offset,category_bytes;
    ScU32 record_bytes,reserved[2];
} ScShellWireApplication;
typedef struct {
    ScU32 size,version,flags,profile,verb_offset,verb_bytes,label_offset,label_bytes;
    ScU32 executable_offset,executable_bytes,arguments_offset,arguments_bytes;
    ScU32 types_offset,types_bytes,icon_offset,icon_bytes;
} ScShellWireVerb;
typedef struct {
    ScU32 size,version,record_bytes,layer,flags,revision;
    ScU32 type_offset,type_bytes,application_offset,application_bytes,verb_offset,verb_bytes;
} ScShellWireAssociation;
typedef struct {
    ScU32 size,version,record_bytes,item,flags;ScI32 x,y,width,height;
    ScU32 order,identity_offset,identity_bytes;
} ScShellWireLayoutItem;
typedef struct {
    ScU32 size,version,count,mode,grid_width,grid_height,revision,reserved;
} ScShellWireLayoutSet;
typedef char shw_service_hello32[(sizeof(ScShellWireServiceHello)==32)?1:-1];
typedef char shw_catalog64[(sizeof(ScShellWireCatalog)==64)?1:-1];
typedef char shw_app112[(sizeof(ScShellWireApplication)==112)?1:-1];
typedef char shw_verb64[(sizeof(ScShellWireVerb)==64)?1:-1];
typedef char shw_assoc48[(sizeof(ScShellWireAssociation)==48)?1:-1];
typedef char shw_layout48[(sizeof(ScShellWireLayoutItem)==48)?1:-1];
#endif
