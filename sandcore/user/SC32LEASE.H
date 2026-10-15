#ifndef SANDCORE_SC32LEASE_H
#define SANDCORE_SC32LEASE_H

#include "SC32API.H"

/* 拟正式公共头：本目录尚未接入发行SDK。身份从可信陷入取得，应用
 * 只传本人句柄；lease代数和文件revision分别核验，不能互相替代。 */
typedef ScU32 ScFileLease;
enum {
    SC32_LEASE_MIN_ABI_MINOR=3, SC32_LEASE_MIN_LIBRARY_VERSION=4,
    SC32_CAP_LEASES_HI=1,
    SC32_LEASE_READ_STABLE=1, SC32_LEASE_WRITE_EXCLUSIVE=2,
    SC32_LEASE_NAMESPACE=3
};
enum {
    SC32_LEASE_READY=1, SC32_LEASE_PENDING_STATE=2,
    SC32_LEASE_COMMITTED=3, SC32_LEASE_CANCELLED_STATE=4,
    SC32_LEASE_FAILED_STATE=5, SC32_LEASE_UNCERTAIN_STATE=6,
    SC32_LEASE_RELEASED=7
};
enum {
    SC32_LEASE_READ_ONLY_NAMESPACE=1, SC32_LEASE_METADATA_ONLY=2,
    SC32_LEASE_EXPECT_ABSENT=1, SC32_LEASE_EXPECT_EXACT=2,
    SC32_LEASE_SET_MODE=4,
    SC32_LEASE_FLAG_CANDIDATE=1, SC32_LEASE_FLAG_DIRECTORY=2,
    SC32_LEASE_FLAG_DURABLE=4, SC32_LEASE_FLAG_CANCEL_REQUESTED=8,
    SC32_LEASE_FLAG_REVOKED=16, SC32_LEASE_FLAG_SNAPSHOT=32,
    SC32_LEASE_FLAG_READ_ONLY=64, SC32_LEASE_FLAG_UNCERTAIN=128,
    SC32_LEASE_FLAG_METADATA_ONLY=256,
    SC32_LEASE_ROW_REVISION_ONLY=1, SC32_LEASE_PAGE_FINISHED=1
};
enum {
    SC32_LEASE_OP_NONE=0, SC32_LEASE_OP_PUBLISH=1,
    SC32_LEASE_OP_RENAME=2, SC32_LEASE_OP_REMOVE=3,
    SC32_LEASE_OP_CANCEL=4, SC32_LEASE_OP_RELEASE=5, SC32_LEASE_OP_SET_MODE=6
};

/* 64B。kind=READ/WRITE时file有效，directory为0；NAMESPACE反之。
 * parent namespace必须是本人有效租约，普通同PID句柄不自动继承。
 * file要求exact object/revision；namespace要求exact revision/epoch。
 * namespace锁根要求R，具体修改再核fileW或直接parentR/W；默认
 * token不是权限提升，READ_ONLY token绝不授权修改。 */
typedef struct {
    ScU32 size,version,kind;
    ScFile file;
    ScDirectory directory;
    ScFileLease namespace_lease;
    ScU32 namespace_generation,expected_object,expected_revision;
    ScU32 expected_epoch,flags,reserved[5];
} ScLeaseAcquireDesc;

/* 64B。OpenPinned绑定现有名；CreatePinned要求不存在并保留名字，
 * 生成不可见、mode3的私有候选。正式名字只在条件Publish成功时可见。
 * OpenPinned必须给非零exact expected_revision，不把新pin伪装成
 * 持久身份。flags=METADATA_ONLY仅接受existing，FILE+READ_STABLE或
 * DIR+NAMESPACE且open_flags0；只授META身份pin，Query/Close可用，
 * IO/seek/flush/DirNext/Snapshot拒绝，也不能作子项namespace grant。
 * 目录epoch0且SNAPSHOT清表示未枚举，members0不能当作空目录。
 * 无整文件R3缓存；候选使用真实extent及现代FILE游标/IO。 */
typedef struct {
    ScU32 size,version;
    const char *path;
    ScU32 object_kind,lease_kind;
    ScFileLease namespace_lease;
    ScU32 namespace_generation,expected_revision,open_flags,create_mode;
    ScU32 flags,reserved[5];
} ScLeaseOpenDesc;

/* 160B。path为绝对显示路径，最多65B含NUL；余padding为0。object_id
 * 只表示当前活FS pin观察；目录以revision/snapshot_epoch及租约固定
 * 成员核验，object_id=0，不发明持久inode/birth-ID/mtime。 */
typedef struct {
    ScU32 size,version;
    ScFileLease lease;
    ScU32 generation,kind,state;
    ScFile file;
    ScDirectory directory;
    ScU32 object_id,revision,snapshot_epoch,flags,operation;
    ScStatus result;
    ScI32 raw;
    ScU32 owner_pid,owner_pid_generation,member_count,progress,total;
    ScU32 operation_sequence,reserved[2];
    char path[68];
} ScLeaseInfo;

/* 96B。target namespace可为0（后端为这个operation保留完整目标名）。
 * EXACT覆盖必须带本人target lease及exact观察；ABSENT全部target观察
 * 为0。Publish只接受私有候选，Rename只接受已发布对象；SET_MODE仅
 * Publish可用，将最终mode与名字一同提交，不公开半成品。 */
typedef struct {
    ScU32 size,version;
    ScFileLease source_lease;
    ScU32 source_generation;
    ScFileLease namespace_lease;
    ScU32 namespace_generation;
    ScFileLease target_lease;
    ScU32 target_generation;
    const char *target_path;
    ScU32 expected_source_object,expected_source_revision;
    ScU32 expected_target_object,expected_target_revision;
    ScU32 expected_snapshot_epoch,final_mode,flags,reserved[8];
} ScLeasePublishDesc;

/* 48B。目录删除只接受真正空目录，不把递归删除谎称一个原子提交。 */
typedef struct {
    ScU32 size,version;
    ScFileLease lease;
    ScU32 generation,expected_object,expected_revision,expected_snapshot_epoch;
    ScU32 flags,reserved[4];
} ScLeaseRemoveDesc;
/* 48B。只改published对象rw，不改UID/GID。exact身份与当前真正
 * owner/auth_can_manage权限均必需，不能用META pin提升chmod权限。 */
typedef struct {
    ScU32 size,version;
    ScFileLease lease;
    ScU32 generation,expected_object,expected_revision,expected_snapshot_epoch;
    ScU32 final_mode,flags,reserved[3];
} ScLeaseModeDesc;
/* 所有control flags=0。Cancel对仍活同序号的终态可重复；Release在
 * 安全点最终回收ID记录，之后Query/Cancel/Release均STALE。成功后
 * caller清本地ref，不保留无限tombstone来伪造历史查询能力。 */
typedef struct {
    ScU32 size,version;
    ScFileLease lease;
    ScU32 generation,operation_sequence,flags,reserved[2];
} ScLeaseControl;

/* 固定成员快照的名字包含完整绝对路径。只捕获当时META可见项，
 * 每页还复验锁根R与各项当前META，祖先无R的深层名字不发布。
 * row不需要caller预置头；原revision是取得时事实，不授正文读取权。 */
typedef struct {
    ScU32 size,version,kind,length,object_id,revision;
    ScI32 uid,gid;
    ScU32 rw,flags,index,snapshot_epoch,reserved[3];
    char path[68];
} ScLeaseEntry;
typedef struct {
    ScU32 size,version,lease,generation,count,cursor,snapshot_epoch,flags;
} ScLeasePage;

ScStatus ScFileLeaseAcquire(const ScLeaseAcquireDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseOpenPinned(const ScLeaseOpenDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseCreatePinned(const ScLeaseOpenDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseQuery(const ScLeaseControl *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseSnapshot(const ScLeaseControl *desc,ScU32 cursor,
                           ScLeaseEntry *rows,ScU32 capacity,ScLeasePage *out);
ScStatus ScFileLeasePublish(const ScLeasePublishDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseRename(const ScLeasePublishDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseRemove(const ScLeaseRemoveDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseSetMode(const ScLeaseModeDesc *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseRelease(const ScLeaseControl *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseCancel(const ScLeaseControl *desc,ScLeaseInfo *out);
ScStatus ScFileLeaseAdvance(const ScLeaseControl *desc,ScLeaseInfo *out);

#endif
