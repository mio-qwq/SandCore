/* 此片段在native_file_t定义后、旧FS函数前包含，只声明私有状态与
 * 前向合同。实际正文在fs_lease.inc，不污染v5的96B持久记录。 */
typedef struct lease_snapshot {
    struct lease_snapshot *next;
    u32 used;
    entry_t rows[40];
} lease_snapshot_t;
typedef char lease_snapshot_fits_page[(sizeof(lease_snapshot_t)<=4096)?1:-1];
typedef struct {
    u32 parent,child,sibling,head,subtree;
    u32 character;
} lease_name_node_t;
typedef struct {
    u32 generation,head;
} lease_owner_t;
typedef struct {
    u32 id,generation,owner,owner_generation,owner_previous,owner_next;
    u32 kind,state,flags,read_only,metadata_only,handle,handle_kind,object,revision,epoch;
    char name[64],target[64];
    u32 parent,child_head,child_previous,child_next;
    u32 node[2],name_previous[2],name_next[2],object_previous,object_next;
    u32 target_borrow_head,namespace_borrow_head,target_previous,target_next;
    u32 namespace_previous,namespace_next,target_lease,namespace_lease;
    lease_snapshot_t *snapshot_head,*snapshot_tail;
    lease_snapshot_t *snapshot_cache;
    u32 members,snapshot_valid,snapshot_cache_base;
    u32 operation,sequence,stage,cancel,release,detaching;
    int result,raw;
    u32 work_previous,work_next,queued,garbage_start,garbage_count,cleanup_initialized;
    u32 cleanup_start,cleanup_count,cleanup_pending_start,cleanup_pending_count;
    entry_t *candidate;
    u32 candidate_pages,candidate_freed,candidate_count,copy_cursor;
    u32 serialize_offset,bank_sector,bank_crc,bank,bank_generation,object_sequence;
    u32 expected_source_object,expected_source_revision,expected_target_object;
    u32 expected_target_revision,expected_epoch,final_mode,condition;
    entry_t draft,removed;
    u32 removed_valid,apply_cursor,committed;
    u8 super[512];
} lease_record_t;
static objpool_t lease_pool,lease_owners,lease_names;
static u32 lease_counter,lease_name_root,lease_work_head,lease_work_tail,lease_meta_owner;
static int lease_initialized;
enum { L_WAIT=1,L_COPY,L_SERIALIZE,L_BANK,L_SUPER,L_APPLY,L_DISPOSE };
static entry_t *native_current(native_file_t *n);
static lease_record_t *lease_get(u32 id);
static void lease_work_add(lease_record_t *r);
static int lease_native_guard(native_file_t *n,int pid,u32 writer,u32 access);
static int lease_private_access(native_file_t *n,int pid,u32 access);
static int lease_private_commit(native_file_t *n);
static void lease_private_discard(native_file_t *n);
static int lease_native_open_blocked(const char *name,u32 flags);
static void lease_volume_reset(void);
static void lease_mark_cancel(lease_record_t *r,int release);
static int lease_cleanup_step(lease_record_t *r);
static void lease_finish(lease_record_t *r,int status,int raw,int uncertain);
