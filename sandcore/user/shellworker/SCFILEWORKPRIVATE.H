#ifndef SANDCORE_SCFILEWORKPRIVATE_H
#define SANDCORE_SCFILEWORKPRIVATE_H

#include "SC32API.H"
#include "SC32BROKER.H"
#include "SC32LEASE.H"
#include <SC32PATH.H>
#include "SCLIB.H"
/* 共享唯一协议头；不复制到worker目录，也不公开进SDK。 */
#include "../scshell/SCSHELLWIRE.H"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* worker的拥有模型，不是wire。跨进程只发送后续共用私有协议中的
 * 值与offset；这些指针始终只属于当前worker地址空间。 */
enum {
    SW_COPY=1,SW_MOVE=2,SW_DELETE=3,SW_RENAME=4,SW_MKDIR=5,
    SW_PLAN=1,SW_SOURCE=2,SW_TARGET=3,SW_RENAME_BEGIN=4,SW_RENAME_WAIT=5,
    SW_CANDIDATE=6,SW_READ=7,SW_WRITE=8,SW_FLUSH=9,SW_PUBLISH_BEGIN=10,
    SW_PUBLISH_WAIT=11,SW_COPY_RECORD=12,SW_REMOVE_BEGIN=13,SW_REMOVE_WAIT=14,
    SW_RESULT_RECORD=15,SW_CLEANUP=16,SW_DONE=17,SW_CONFLICT=18,
    SW_MODE_BEGIN=19,SW_MODE_WAIT=20,SW_FINALIZE=21,SW_RETRY_CLEANUP=22,
    SW_PUBLISHED=23,SW_RENAMED=24,SW_MODE_DONE=25,SW_DIRECTORY_READY=26,SW_REPLANNING=27,
    SW_OUT_COPIED=1,SW_OUT_MOVED=2,SW_OUT_REMOVED=3,SW_OUT_RENAMED=4,
    SW_OUT_CREATED=5,SW_OUT_SKIPPED=6,SW_OUT_CANCELLED=7,
    SW_OUT_FAILED=8,SW_OUT_UNKNOWN=9,SW_OUT_SOURCE_RETAINED=10,
    SW_RESOLVE_REPLACE=1,SW_RESOLVE_SKIP=2,SW_RESOLVE_RENAME=3,
    SW_RESOLVE_MERGE=4,SW_RESOLVE_RETRY=5,SW_RESOLVE_CANCEL=6,
    SW_RECURSIVE=1,SW_FORCE_COPY_MOVE=2,SW_MERGE_DIRECTORIES=4,
    SW_OVERWRITE=8,SW_DESTINATION_DIRECTORY=16,
    SW_PHASE_INPUT=1,SW_PHASE_PLAN=2,SW_PHASE_COPY=3,SW_PHASE_COMMIT=4,
    SW_PHASE_REMOVE=5,SW_PHASE_RECOVERY=6,SW_PHASE_FINISHED=7,
    SW_IO_BYTES=65536,SW_PLAN_QUANTUM=64
};

typedef struct SwRoot SwRoot;
typedef struct SwItem SwItem;
typedef struct SwLease SwLease;
typedef struct SwOutbound SwOutbound;
struct SwLease {
    ScLeaseInfo info;
    ScFile file;
    ScDirectory directory;
    ScU32 valid,closing,terminal;
};
struct SwRoot {
    SwRoot *next;
    ScU32 id,client_id,flags,kind,expected_revision,planned,completed,skip;
    ScU32 expected_destination_revision,expected_destination_kind,plan_failed;
    SwItem *root_item;
    ScU32 first,last;
    char source[68],destination[68];
    SwLease scope;
    SwLease destination_scope;
    ScU32 scan_cursor,member_count,remaining,root_moved;
};
struct SwItem {
    SwItem *next,*parent,*next_journal,*next_result;
    SwRoot *root;
    ScU32 id,client_id,kind,source_revision,source_mode,depth,phase,outcome;
    ScU32 source_object,destination_object,destination_revision,source_removed;
    ScU32 copied,published,directory_created,overwrite,merge,retry,conflict_epoch;
    ScU32 durable,recovery_written,reported,journaled,mode_done,finalizing,journal_pending;
    ScU32 attempt_phase,skip_children,covered,stage,children_total,children_finished,children_failed,candidate_trials;
    ScU32 conflict_source_revision,conflict_destination_revision;
    ScU32 decision_expected_destination,decision_check;
    ScU32 retry_root_release;
    ScU32 bytes,offset,buffer_bytes,buffer_used,confirmed_bytes,final_mode;
    ScStatus status;
    ScErrorInfo cause;
    char source[68],destination[68];
};
typedef struct {
    ScProcessRef self,parent;
    ScIdentityInfo identity;
    ScBrokerIdentity peer;
    ScIpcChannel channel;
    ScU32 operation,flags,state,cancelled,peer_closed,started,finished;
    ScU32 final_mode,plan_incomplete,items_done,items_failed,items_skipped,user_cancel;
    ScU32 root_count,hello,committed,protocol_error,final_queued,cleaned;
    ScU32 last_request_lo,last_request_hi,epoch;
    ScU32 candidate_sequence,candidate_started,candidate_seed,candidate_space;
    SwItem *retry_item;
    SwLease retry_lease;
    ScU32 retry_phase,retry_cursor,retry_root,retry_snapshot_borrowed;
    ScStatus retry_error;
    ScStatus failure;
    ScErrorInfo failure_cause;
    ScU32 item_counter,next_item_id,active_index,delete_index,planning_complete,executing_deletes;
    ScU32 conflict_counter,progress_sequence_lo,progress_sequence_hi;
    ScWideCounter request,confirmed_bytes,total_bytes;
    SwRoot *roots,*last_root,*planning_root,*running_root;
    SwItem *items,*last_item,*active;
    ScArray *order;
    SwItem **sorted;
    SwRoot **selected;
    ScHash *paths;
    SwLease source_lease,candidate_lease,target_lease;
    SwLease *source;
    unsigned char *io;
    ScLeaseEntry *rows;
    ScU32 chunk_prefix,error_after_prefix;
    ScStatus deferred_io_error;
    ScErrorInfo deferred_cause;
    char endpoint[64],recovery_path[68];
    SwLease recovery_lease;
    ScBuffer *journal;
    ScU32 recovery_sequence,record_count,recovery_ready,recovery_failed,recovery_phase;
    SwItem *journal_head,*journal_tail,*result_head,*result_tail;
    ScU32 input_complete,input_bytes,input_expected,output_blocked;
    ScBuffer *incoming;
    SwOutbound *output_head,*output_tail;
    ScU32 output_bytes;
} SwWorker;

static SwWorker sw;
static void sw_record(void *out,ScU32 bytes);
static ScStatus sw_path(const char *text,char out[68]);
static int sw_same(const char *a,const char *b);
static int sw_child(const char *path,const char *parent);
static ScStatus sw_join(const char *base,const char *tail,char out[68]);
static void sw_counter_add(ScWideCounter *counter,ScU32 value);
static void sw_error(SwItem *item,ScStatus status);
static ScStatus sw_lease_open(SwLease *lease,const char *path,ScU32 kind,
    ScU32 lease_kind,ScU32 revision,ScU32 flags,SwLease *parent,int create);
static ScStatus sw_namespace_open(SwLease *lease,const char *path,ScU32 revision,ScU32 read_only,SwLease *parent);
static ScStatus sw_lease_query(SwLease *lease);
static ScStatus sw_lease_advance(SwLease *lease);
static ScStatus sw_lease_release(SwLease *lease);
static ScStatus sw_lease_publish(SwLease *lease,SwLease *target,SwLease *parent,const char *path,ScU32 mode,int rename);
static ScStatus sw_lease_remove(SwLease *lease);
static ScStatus sw_lease_cancel(SwLease *lease);
static ScStatus sw_lease_mode(SwLease *lease,ScU32 mode);
static ScStatus sw_lease_metadata(SwLease *lease,const char *path,ScU32 kind,ScU32 revision,SwLease *parent);
static SwLease *sw_source_parent(SwItem *item);
static SwLease *sw_destination_parent(SwItem *item);
static int sw_can_read(ScI32 uid,ScI32 gid,ScU32 mode);
static ScStatus sw_plan_root(SwRoot *root);
static ScStatus sw_plan_add(SwRoot *root,const ScLeaseEntry *entry);
static ScStatus sw_plan_finish(void);
static ScStatus sw_item_step(SwItem *item);
static ScStatus sw_journal_create(void);
static ScStatus sw_journal_record(SwItem *item,ScU32 stage,int durable);
static ScStatus sw_journal_flush(void);
static ScStatus sw_emit_result(SwItem *item);
static ScStatus sw_emit_progress(SwItem *item);
static ScStatus sw_emit_conflict(SwItem *item);
static ScStatus sw_protocol_pump(void);
static ScStatus sw_protocol_flush(void);
static ScStatus sw_queue(ScU32 opcode,const void *data,ScU32 bytes,ScU32 flags,ScStatus status);
static void sw_ack(SwItem *item,ScShellWireJobAck *ack,int live);
static ScStatus sw_decide(const ScShellWireJobDecision *decision,const char *path);
static ScStatus sw_finish_item(SwItem *item,ScU32 outcome,ScStatus status);
static ScStatus sw_begin_conflict(SwItem *item,ScStatus status);
static ScStatus sw_deliver_results(void);
static ScStatus sw_retry_begin(SwItem *item);
static ScStatus sw_retry_step(void);
static void sw_fail(ScStatus status);
static void sw_cleanup(void);

#endif
