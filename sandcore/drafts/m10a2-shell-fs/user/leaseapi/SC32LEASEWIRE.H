#ifndef SANDCORE_SC32LEASEWIRE_H
#define SANDCORE_SC32LEASEWIRE_H

#include "../SC32LEASE.H"

/* 仅系统库/内核包含；本隔离目录保持正式user/leaseapi相对布局。
 * gate仍为SYS0x300。
 * 0x700是selector而不是旧syscall或SC32PRIVATE的error-operation。 */
enum {
    SC32_L_ACQUIRE=0x700, SC32_L_OPEN=0x701, SC32_L_CREATE=0x702,
    SC32_L_QUERY=0x703, SC32_L_SNAPSHOT=0x704, SC32_L_PUBLISH=0x705,
    SC32_L_RENAME=0x706, SC32_L_REMOVE=0x707, SC32_L_RELEASE=0x708,
    SC32_L_CANCEL=0x709, SC32_L_ADVANCE=0x70a, SC32_L_SET_MODE=0x70b,
    SC32_LEASE_BATCH_MAX=64, SC32_LEASE_WIRE_VERSION=1
};
typedef struct {
    ScU32 size,version,kind,file,directory,namespace_lease,namespace_generation;
    ScU32 expected_object,expected_revision,expected_epoch,flags,reserved[5];
} Sc32LeaseAcquireRequest;
typedef struct {
    ScU32 size,version,path,object_kind,lease_kind,namespace_lease;
    ScU32 namespace_generation,expected_revision,open_flags,create_mode;
    ScU32 flags,reserved[5];
} Sc32LeaseOpenRequest;
typedef struct {
    ScU32 size,version,source_lease,source_generation,namespace_lease,namespace_generation;
    ScU32 target_lease,target_generation,target_path,expected_source_object,expected_source_revision;
    ScU32 expected_target_object,expected_target_revision,expected_snapshot_epoch,final_mode,flags,reserved[8];
} Sc32LeasePublishRequest;
typedef struct {
    ScU32 size,version,lease,generation,expected_object,expected_revision,expected_snapshot_epoch;
    ScU32 flags,reserved[4];
} Sc32LeaseRemoveRequest;
typedef struct {
    ScU32 size,version,lease,generation,expected_object,expected_revision,expected_snapshot_epoch;
    ScU32 final_mode,flags,reserved[3];
} Sc32LeaseModeRequest;
typedef struct {
    ScU32 size,version,lease,generation,operation_sequence,flags,reserved[2];
} Sc32LeaseControlRequest;
typedef struct {
    ScU32 size,version,lease,generation,rows,capacity,cursor,flags,reserved[2];
} Sc32LeaseSnapshotRequest;

#endif
