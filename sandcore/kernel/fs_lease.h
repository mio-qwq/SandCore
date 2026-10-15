#ifndef SANDCORE_FS_LEASE_H
#define SANDCORE_FS_LEASE_H

#include "io.h"
#include "../user/SC32LEASE.H"
#include "../user/leaseapi/SC32LEASEWIRE.H"

/* 目标位置kernel/fs_lease.h；本草稿通过integration.patch接线。
 * 内部binding没有用户指针，kind沿FILE=1/DIRECTORY=2。 */
typedef struct {
    u32 handle,kind,object,flags,aliases,epoch,lease,metadata_only;
    char path[64];
} fs_lease_binding_t;

int fs_lease_acquire(int pid,const Sc32LeaseAcquireRequest *q,ScLeaseInfo *out);
int fs_lease_open(int pid,const Sc32LeaseOpenRequest *q,const char *name,int create,ScLeaseInfo *out);
int fs_lease_begin(int pid,const Sc32LeasePublishRequest *q,const char *target,int rename,ScLeaseInfo *out);
int fs_lease_remove(int pid,const Sc32LeaseRemoveRequest *q,ScLeaseInfo *out);
int fs_lease_set_mode(int pid,const Sc32LeaseModeRequest *q,ScLeaseInfo *out);
int fs_lease_control(int pid,u32 operation,const Sc32LeaseControlRequest *q,ScLeaseInfo *out);
int fs_lease_snapshot(int pid,const Sc32LeaseSnapshotRequest *q,ScLeasePage *out);
int fs_lease_snapshot_row(int pid,u32 lease,u32 generation,u32 index,ScLeaseEntry *out);
int fs_lease_detach_step(int pid,u32 file,u32 lease);
int fs_lease_stop_step(int pid);
void fs_lease_poll(void);

/* 旧path入口没有permit，不能因同PID而越过租约。CONTENT只核正文/
 * exact名及namespace祖先，TREE另核被改名/删除的全部受锁后代。
 * READ_OPEN用于新pin，不限制不改变对象身份的已有普通读取。 */
#define FS_LEASE_CONTENT 1u
#define FS_LEASE_TREE 2u
#define FS_LEASE_READ_OPEN 3u
int fs_lease_path_blocked(const char *name,u32 operation);
int fs_lease_volume_locked(void);
int fs_lease_available(void);
int fs_lease_file_name(u32 object,char out[64]);
int fs_lease_file_private(u32 object);
int fs_lease_close_committed(u32 lease,u32 object);

/* 唯一可接触FILE私有record的片段。其它服务只用可信值，不跨poll
 * 保存descriptor指针；关闭候选与撤lease顺序由后端统一控制。 */
int sc32_file_lease_binding(int pid,u32 handle,u32 kind,fs_lease_binding_t *out);
int sc32_file_lease_reserve(int pid,u32 kind,const char *name,u32 flags,u32 *handle);
int sc32_file_lease_attach(int pid,u32 handle,u32 object,u32 lease,int metadata_only);
void sc32_file_lease_abort_reservation(int pid,u32 handle);
void sc32_file_lease_unbind(int pid,u32 handle,u32 lease,int close_candidate);
void sc32_file_lease_rebind(int pid,u32 handle,const char *name);

#endif
