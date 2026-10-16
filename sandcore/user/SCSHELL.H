#ifndef SANDCORE_SCSHELL_H
#define SANDCORE_SCSHELL_H
#include <SC32HOST.H>
#include <SC32PATH.H>
#include <SC32LEASE.H>
#include <SCUI.H>

/* 公开cdecl合同。指针只借本人地址空间，跨进程由库编码成值/offset。
 * 当前为隔离源码；SC32 ABI1.3/v4接线和实际运行由统一构建验收确认。 */
enum {
    SC_SHELL_ABI_MAJOR=1,SC_SHELL_ABI_MINOR=1,SC_SHELL_VERSION=2,
    SC_SHELL_OK=0,SC_SHELL_END=1,SC_SHELL_PENDING=2,SC_SHELL_READY=3,
    SC_SHELL_E_INVALID=-401,SC_SHELL_E_VERSION=-402,SC_SHELL_E_RANGE=-403,
    SC_SHELL_E_MEMORY=-404,SC_SHELL_E_UTF8=-405,SC_SHELL_E_STALE=-406,
    SC_SHELL_E_NOT_FOUND=-407,SC_SHELL_E_EXISTS=-408,SC_SHELL_E_BUSY=-409,
    SC_SHELL_E_ACCESS=-410,SC_SHELL_E_IO=-411,SC_SHELL_E_CANCELLED=-412,
    SC_SHELL_E_PROTOCOL=-413,SC_SHELL_E_UNKNOWN=-414,SC_SHELL_E_NOT_SUPPORTED=-415,
    SC_SHELL_LAYER_SYSTEM=0,SC_SHELL_LAYER_ACCOUNT=1,SC_SHELL_LAYER_SESSION=2,
    SC_SHELL_PROFILE_MODERN=1,SC_SHELL_PROFILE_LEGACY=2,
    SC_SHELL_APP_HIDDEN=1,SC_SHELL_APP_DISABLED=2,SC_SHELL_APP_TRANSIENT=4,
    SC_SHELL_VERB_DEFAULT=1,SC_SHELL_VERB_NO_DOCUMENT=2,
    SC_SHELL_TYPE_DIRECTORY=1,SC_SHELL_TYPE_EXECUTABLE=2,SC_SHELL_TYPE_EXTENSION=3,
    SC_SHELL_CONNECTING=1,SC_SHELL_CONNECTED=2,SC_SHELL_DISCONNECTED=3,
    SC_SHELL_REQUEST_QUEUED=1,SC_SHELL_REQUEST_SENT=2,SC_SHELL_REQUEST_COMPLETE=3,
    SC_SHELL_REQUEST_UNKNOWN=4,SC_SHELL_REQUEST_FAILED=5,
    SC_SHELL_EVENT_CONNECTED=1,SC_SHELL_EVENT_CHANGED=2,SC_SHELL_EVENT_REPLY=3,
    SC_SHELL_EVENT_DISCONNECTED=4,SC_SHELL_EVENT_JOB=5,SC_SHELL_EVENT_CONFLICT=6,
    SC_SHELL_EVENT_ITEM=7,SC_SHELL_EVENT_COMMAND=8,
    SC_SHELL_PAGE_FINISHED=1,SC_SHELL_IMPORT_REPLACE=1,
    SC_SHELL_LAYOUT_SELECTED=1,SC_SHELL_LAYOUT_PINNED=2,
    SC_SHELL_LAYOUT_FREE=0,SC_SHELL_LAYOUT_GRID=1,SC_SHELL_LAYOUT_AUTO=2,
    SC_SHELL_COPY=1,SC_SHELL_MOVE=2,SC_SHELL_DELETE=3,SC_SHELL_RENAME=4,SC_SHELL_MKDIR=5,
    SC_SHELL_RECURSIVE=1,SC_SHELL_PRESERVE_MODE=2,
    SC_SHELL_ITEM_EXPECT_SOURCE=1,SC_SHELL_ITEM_EXPECT_DESTINATION=2,
    SC_SHELL_JOB_CONNECTING=1,SC_SHELL_JOB_PLANNING=2,SC_SHELL_JOB_RUNNING=3,
    SC_SHELL_JOB_CONFLICT=4,SC_SHELL_JOB_CANCELLING=5,SC_SHELL_JOB_FINISHED=6,
    SC_SHELL_JOB_CANCELLED=7,SC_SHELL_JOB_FAILED=8,SC_SHELL_JOB_UNKNOWN=9,
    SC_SHELL_STAGE_NONE=0,SC_SHELL_STAGE_CANDIDATE=1,SC_SHELL_STAGE_FLUSHED=2,
    SC_SHELL_STAGE_DESTINATION=3,SC_SHELL_STAGE_RENAME=4,SC_SHELL_STAGE_SOURCE_REMOVED=5,
    SC_SHELL_STAGE_DELETED=6,SC_SHELL_STAGE_DIRECTORY=7,
    SC_SHELL_RESULT_DONE=1,SC_SHELL_RESULT_SKIPPED=2,SC_SHELL_RESULT_SOURCE_RETAINED=3,
    SC_SHELL_RESULT_FAILED=4,SC_SHELL_RESULT_CANCELLED=5,SC_SHELL_RESULT_UNCERTAIN=6,
    SC_SHELL_DURABLE=1,SC_SHELL_SOURCE_RETAINED=2,SC_SHELL_OBSERVATION_LIVE=4,
    SC_SHELL_RECOVERY_WRITTEN=8,
    SC_SHELL_PROGRESS_PRIVATE_PREFIX=1,
    SC_SHELL_REPLACE=1,SC_SHELL_SKIP=2,SC_SHELL_RENAME_TARGET=3,
    SC_SHELL_MERGE=4,SC_SHELL_RETRY=5,SC_SHELL_CANCEL=6,
    SC_SHELL_COMMAND_OPEN=0x7100,SC_SHELL_COMMAND_OPEN_WITH=0x7101,
    SC_SHELL_COMMAND_COPY=0x7102,SC_SHELL_COMMAND_CUT=0x7103,
    SC_SHELL_COMMAND_PASTE=0x7104,SC_SHELL_COMMAND_DELETE=0x7105,
    SC_SHELL_COMMAND_RENAME=0x7106,SC_SHELL_COMMAND_PROPERTIES=0x7107,
    SC_SHELL_COMMAND_NEW_DIRECTORY=0x7108,
    SC_SHELL_SHORTCUT_LEGACY=1,SC_SHELL_SHORTCUT_APPLICATION=2,SC_SHELL_SHORTCUT_EXECUTABLE=3,SC_SHELL_SHORTCUT_PATH=4,
    SC_SHELL_MENU_START=1,SC_SHELL_MENU_SELECTION=2,SC_SHELL_MENU_BACKGROUND=3
};
enum {
    SC_SHELL_STORE_WRITING=1,SC_SHELL_STORE_PUBLISHING=2,SC_SHELL_STORE_COMPLETE=3,
    SC_SHELL_STORE_FAILED=4,SC_SHELL_STORE_CANCELLED=5,SC_SHELL_STORE_UNKNOWN=6,
    SC_SHELL_STORE_DURABLE=1,SC_SHELL_STORE_READY=2,SC_SHELL_STORE_WAITING=4,
    SC_SHELL_SERVICE_READY=1
};
typedef struct ScShell ScShell;
typedef struct ScShellRequest ScShellRequest;
typedef struct ScShellCatalog ScShellCatalog;
typedef struct ScShellApplication ScShellApplication;
typedef struct ScShellLayout ScShellLayout;
typedef struct ScShellShortcut ScShellShortcut;
typedef struct ScShellService ScShellService;
typedef struct ScShellFileJob ScShellFileJob;
typedef struct ScShellMenu ScShellMenu;
typedef struct ScShellDialog ScShellDialog;
typedef struct ScShellStore ScShellStore;

typedef struct {
    ScU32 size,version;ScI32 code,cause;ScU32 domain;ScI32 raw;
    ScU32 operation,item,epoch,reserved[3];
} ScShellErrorInfo;
typedef struct {
    ScU32 size,version,type,epoch;ScShellRequest *request;ScShellFileJob *job;
    ScU32 command,item;ScStatus status;const void *data;ScU32 bytes,reserved;
} ScShellEvent;
typedef ScStatus (*ScShellEventProc)(const ScShellEvent *,void *context);
typedef struct {
    ScU32 size,version,expected_host_epoch,flags;ScShellEventProc procedure;
    void *context;ScDropProc context_drop;ScU32 budget_bytes;
} ScShellConnectDesc;
typedef struct {
    ScU32 size,version,state,epoch,pending_requests,queued_bytes,received_bytes,flags;
    ScShellHostInfo host;ScBrokerIdentity peer;
} ScShellInfo;
typedef struct {
    ScU32 size,version,state,opcode;ScWideCounter sequence;ScStatus status;
    ScU32 epoch,result_bytes,side_effect,enqueued,reserved;
} ScShellRequestInfo;

typedef struct {
    ScU32 size,version;const char *verb,*label;ScU32 flags,profile;
    const char *executable,*arguments,*types,*icon;ScU32 reserved[2];
} ScShellVerbDesc;
typedef struct {
    ScU32 size,version,layer,flags;const char *id,*label,*executable,*arguments,*icon,*category;
    ScU32 profile;const ScShellVerbDesc *verbs;ScU32 verb_count,reserved[3];
} ScShellApplicationDesc;
typedef struct {
    ScU32 size,version,token,revision,layer,flags,owner_pid,owner_generation;
    ScI32 uid,gid;ScU32 epoch,verb_count,string_bytes,reserved[3];
} ScShellApplicationInfo;
typedef struct {
    ScU32 size,version,layer,flags;const char *type,*application,*verb;
    ScU32 expected_revision,reserved[4];
} ScShellAssociationDesc;
typedef struct {
    ScU32 size,version,revision,layer,flags,application_token,verb_index,epoch;
} ScShellAssociationInfo;
typedef struct {
    ScU32 size,version,epoch,applications,associations,layout_items,bytes,flags;
} ScShellCatalogInfo;
typedef struct {ScU32 size,version,cursor,count,epoch,examined,flags,reserved;} ScShellPage;
typedef struct {
    ScU32 size,version,item,flags;const char *identity;ScI32 x,y,width,height;
    ScU32 order,reserved[2];
} ScShellLayoutItem;
typedef struct {
    ScU32 size,version,revision,count,mode,grid_width,grid_height,reserved;
} ScShellLayoutInfo;
typedef struct {
    ScU32 size,version,kind,flags;const char *label,*application,*target,*arguments,*icon;
    ScU32 profile,reserved[2];
} ScShellShortcutDesc;

/* 本地catalog是拥有快照，不是权限票。Service会在真实peer基础上覆盖
 * SID层owner，拒绝外来SYSTEM层；显式种子导入由host自己的代码完成。 */
ScStatus ScShellErrorQuery(ScShellErrorInfo *out);
ScStatus ScShellCatalogCreate(ScShellCatalog **out);
ScStatus ScShellCatalogRetain(ScShellCatalog *);
void ScShellCatalogRelease(ScShellCatalog *);
ScStatus ScShellCatalogQuery(const ScShellCatalog *,ScShellCatalogInfo *out);
ScStatus ScShellCatalogStoreRevision(const ScShellCatalog *,ScU32 *out);
ScStatus ScShellCatalogRegister(ScShellCatalog *,const ScShellApplicationDesc *,ScU32 expected_epoch,ScU32 *token);
ScStatus ScShellCatalogUnregister(ScShellCatalog *,const char *id,ScU32 layer,ScU32 expected_epoch);
ScStatus ScShellCatalogApplications(const ScShellCatalog *,ScU32 after,ScU32 expected_epoch,ScShellApplicationInfo *rows,ScU32 capacity,ScShellPage *out);
ScStatus ScShellCatalogApplication(const ScShellCatalog *,ScU32 token,ScShellApplication **out);
ScStatus ScShellCatalogFind(const ScShellCatalog *,const char *id,ScShellApplication **out);
ScStatus ScShellApplicationRetain(ScShellApplication *);
void ScShellApplicationRelease(ScShellApplication *);
ScStatus ScShellApplicationQuery(const ScShellApplication *,ScShellApplicationInfo *out);
ScStatus ScShellApplicationDescribe(const ScShellApplication *,ScShellApplicationDesc *out);
ScStatus ScShellCatalogSetAssociation(ScShellCatalog *,const ScShellAssociationDesc *,ScU32 expected_epoch);
ScStatus ScShellCatalogAssociation(const ScShellCatalog *,const char *type,const char *verb,ScShellAssociationInfo *out);
ScStatus ScShellCatalogResolve(const ScShellCatalog *,const char *path,const char *verb,ScShellApplication **out,ScU32 *verb_index);
ScStatus ScShellCatalogExport(const ScShellCatalog *,ScBuffer **out);
ScStatus ScShellCatalogImport(ScShellCatalog *,const void *bytes,ScU32 count,ScU32 expected_epoch,ScU32 flags);
ScStatus ScShellCatalogLoad(ScShellCatalog *,const char *path,ScU32 expected_epoch);
ScStatus ScShellCatalogImportLegacyMenu(ScShellCatalog *,const char *path,ScU32 expected_epoch);
ScStatus ScShellCatalogSave(ScShellCatalog *,const char *path,ScU32 expected_revision,ScU32 *new_revision);
typedef struct {
    ScU32 size,version,state;ScStatus status;
    ScU32 expected_revision,new_revision,catalog_epoch,bytes_written,total_bytes,stage,flags,reserved;
} ScShellStoreInfo;
ScStatus ScShellStoreBegin(ScShellCatalog *,const char *path,ScU32 expected_revision,ScShellStore **out);
ScStatus ScShellStoreStep(ScShellStore *,ScU32 timeout_ticks);
ScStatus ScShellStoreQuery(const ScShellStore *,ScShellStoreInfo *out);
void ScShellStoreClose(ScShellStore *);
ScStatus ScShellLayoutCreate(const ScShellLayoutItem *,ScU32 count,ScU32 mode,ScU32 grid_width,ScU32 grid_height,ScShellLayout **out);
ScStatus ScShellLayoutRetain(ScShellLayout *);
void ScShellLayoutRelease(ScShellLayout *);
ScStatus ScShellLayoutQuery(const ScShellLayout *,ScShellLayoutInfo *out);
ScStatus ScShellLayoutGet(const ScShellLayout *,ScU32 index,ScShellLayoutItem *out);
ScStatus ScShellLayoutArrange(const ScShellLayout *,const ScUiRect *work,ScU32 mode,ScShellLayout **out);
ScStatus ScShellCatalogLayout(const ScShellCatalog *,ScShellLayout **out);
ScStatus ScShellCatalogApplyLayout(ScShellCatalog *,ScShellLayout *,ScU32 expected_epoch);

typedef struct {
    ScU32 size,version;ScShellCatalog *catalog;ScIpcEndpoint endpoint;
    ScU32 host_epoch,frame_budget_bytes;const char *account_store;
    ScShellEventProc procedure;void *context;ScDropProc context_drop;ScU32 reserved[2];
} ScShellServiceDesc;
typedef struct {
    ScU32 size,version,clients,epoch,queued_bytes,received_frames,transient_entries,flags;
} ScShellServiceInfo;
ScStatus ScShellServiceCreate(const ScShellServiceDesc *,ScShellService **out);
ScStatus ScShellServiceStep(ScShellService *,ScU32 timeout_ticks);
ScStatus ScShellServiceQuery(const ScShellService *,ScShellServiceInfo *out);
void ScShellServiceClose(ScShellService *);
ScStatus ScShellConnect(const ScShellConnectDesc *,ScShell **out);
ScStatus ScShellQuery(const ScShell *,ScShellInfo *out);
ScStatus ScShellStep(ScShell *,ScU32 timeout_ticks);
void ScShellClose(ScShell *);
ScStatus ScShellQueryCatalog(ScShell *,ScU32 expected_epoch,ScShellRequest **out);
ScStatus ScShellRegisterApplication(ScShell *,const ScShellApplicationDesc *,ScU32 expected_epoch,ScShellRequest **out);
ScStatus ScShellUnregisterApplication(ScShell *,const char *id,ScU32 layer,ScU32 expected_epoch,ScShellRequest **out);
ScStatus ScShellSetAssociation(ScShell *,const ScShellAssociationDesc *,ScU32 expected_epoch,ScShellRequest **out);
ScStatus ScShellApplyLayout(ScShell *,const ScShellLayout *,ScU32 expected_epoch,ScShellRequest **out);
ScStatus ScShellRequestQuery(const ScShellRequest *,ScShellRequestInfo *out);
ScStatus ScShellRequestError(const ScShellRequest *,ScShellErrorInfo *out);
ScStatus ScShellRequestCopy(const ScShellRequest *,ScBuffer **out);
ScStatus ScShellRequestCatalog(const ScShellRequest *,ScShellCatalog **out);
void ScShellRequestClose(ScShellRequest *);

ScStatus ScShellLaunch(const ScShellApplication *,ScU32 verb_index,const char *document,ScLaunchInfo *out);
ScStatus ScShellOpen(const ScShellCatalog *,const char *path,const char *verb,ScLaunchInfo *out);
ScStatus ScShellShortcutParse(const void *bytes,ScU32 count,const char *base,ScShellShortcut **out);
ScStatus ScShellShortcutCreate(const ScShellShortcutDesc *,ScShellShortcut **out);
ScStatus ScShellShortcutRead(const char *path,ScShellShortcut **out);
ScStatus ScShellShortcutDescribe(const ScShellShortcut *,ScShellShortcutDesc *out);
ScStatus ScShellShortcutSerialize(const ScShellShortcut *,ScBuffer **out);
ScStatus ScShellShortcutWrite(const ScShellShortcut *,const char *path,ScU32 expected_revision,ScU32 *new_revision);
ScStatus ScShellShortcutLaunch(const ScShellShortcut *,const ScShellCatalog *,ScLaunchInfo *out);
void ScShellShortcutRelease(ScShellShortcut *);
ScStatus ScShellIconLoad(ScUiContext *,const char *spec,const char *executable,ScUiIcon **out);
ScStatus ScShellIconCacheBudget(ScU32 bytes);
ScStatus ScShellIconCacheQuery(ScCacheInfo *out);

typedef struct {
    ScU32 size,version,item,kind;const char *source,*destination;
    ScU32 expected_source_revision,expected_destination_revision,expected_destination_kind,flags,reserved[2];
} ScShellFileItem;
typedef struct {
    ScU32 size,version,operation,flags;const ScShellFileItem *items;ScU32 count;
    const char *destination,*recovery_path;ScU32 final_mode,conflict_policy;
    ScShellEventProc procedure;void *context;ScDropProc context_drop;ScU32 reserved[3];
} ScShellFileJobDesc;
typedef struct {
    ScU32 size,version;ScWideCounter nonce;ScU32 state,stage,current_item;
    ScU32 items_total,items_done,items_failed,items_skipped,total_known;
    ScWideCounter bytes,total_bytes;ScU32 conflict_epoch;ScStatus status,cause;
    ScU32 domain;ScI32 raw;ScU32 flags,reserved[2];
} ScShellFileProgress;
typedef struct {
    ScU32 size,version;ScWideCounter nonce;ScU32 item,operation,state,stage;
    ScStatus status,cause;ScU32 domain;ScI32 raw;ScWideCounter bytes;
    ScU32 source_object,source_revision,destination_object,destination_revision;
    ScU32 conflict_epoch,flags,worker_pid,worker_generation,reserved[2];
    char source[68],destination[68];
} ScShellFileResult;
typedef struct {
    ScU32 size,version,item,conflict_epoch,decision;
    ScU32 expected_source_revision,expected_destination_revision;const char *new_path;
} ScShellConflictDecision;
ScStatus ScShellFileOperationBegin(const ScShellFileJobDesc *,ScShellFileJob **out);
ScStatus ScShellFileOperationStep(ScShellFileJob *,ScU32 timeout_ticks);
ScStatus ScShellFileOperationQuery(const ScShellFileJob *,ScShellFileProgress *out);
ScStatus ScShellFileOperationResult(const ScShellFileJob *,ScU32 index,ScShellFileResult *out);
ScStatus ScShellFileOperationConflict(const ScShellFileJob *,ScShellFileResult *out);
ScStatus ScShellFileOperationResolveConflict(ScShellFileJob *,const ScShellConflictDecision *);
ScStatus ScShellFileOperationCancel(ScShellFileJob *);
void ScShellFileOperationClose(ScShellFileJob *);
ScStatus ScShellClipboardText(const ScUiText *,ScU32 expected_epoch,ScClipboardInfo *out);
ScStatus ScShellClipboardReadText(ScString **out,ScU32 *epoch);
ScStatus ScShellClipboardCopyFiles(const ScShellFileItem *,ScU32 count,ScU32 expected_epoch,ScClipboardInfo *out);
ScStatus ScShellClipboardCutFiles(const ScShellFileItem *,ScU32 count,ScU32 expected_epoch,ScClipboardInfo *out);
ScStatus ScShellClipboardPaste(const char *destination,const char *recovery_path,ScShellEventProc,void *context,ScShellFileJob **out);
ScStatus ScShellClipboardReconcile(ScShellFileJob *,ScClipboardInfo *out);

typedef struct {
    ScU32 size,version,kind,command,flags;const ScShellFileItem *items;ScU32 count;
    const char *directory;ScShellCatalog *catalog;ScUiContext *ui;ScUiNode owner;
    ScShellEventProc procedure;void *context;ScDropProc context_drop;
    const char *recovery_directory;ScU32 reserved;
} ScShellMenuDesc;
typedef ScStatus (*ScShellMenuProc)(const ScShellMenuDesc *,ScUiMenu *,void *context);
typedef struct {
    ScU32 size,version,scope,flags;ScShellMenuProc build;ScShellEventProc execute;
    void *context;ScDropProc context_drop;ScU32 reserved[4];
} ScShellMenuProviderDesc;
ScStatus ScShellMenuRegisterProvider(const ScShellMenuProviderDesc *,ScU32 *token);
ScStatus ScShellMenuUnregisterProvider(ScU32 token);
ScStatus ScShellMenuBuild(const ScShellMenuDesc *,ScShellMenu **out);
ScStatus ScShellMenuNative(ScShellMenu *,ScUiMenu **out);
ScStatus ScShellMenuExecute(ScShellMenu *,ScU32 command);
ScStatus ScShellMenuStep(ScShellMenu *,ScU32 timeout_ticks);
void ScShellMenuRelease(ScShellMenu *);
ScStatus ScShellFileDialog(ScUiContext *,const ScUiFileDialogDesc *,ScUiDialog **out);
ScStatus ScShellConfirm(ScUiContext *,ScUiNode owner,const ScUiText *title,const ScUiText *message,ScUiEventProc,void *,ScUiDialog **out);
ScStatus ScShellErrorDialog(ScUiContext *,ScUiNode owner,const ScShellErrorInfo *,const char *path,ScUiDialog **out);
ScStatus ScShellProperties(ScUiContext *,ScUiNode owner,const char *path,ScShellDialog **out);
ScStatus ScShellProgress(ScUiContext *,ScUiNode owner,ScShellFileJob *,ScShellDialog **out);
ScStatus ScShellDialogStep(ScShellDialog *);
void ScShellDialogClose(ScShellDialog *);
ScStatus ScShellDragBegin(ScUiContext *,ScUiNode source,const ScShellFileItem *,ScU32 count,ScU32 actions,ScU32 *token);
ScStatus ScShellDropFiles(const ScUiDragData *,const char *destination,const char *recovery_path,ScShellFileJob **out);

#endif
