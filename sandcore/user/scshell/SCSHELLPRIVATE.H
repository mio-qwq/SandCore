#ifndef SANDCORE_SCSHELLPRIVATE_H
#define SANDCORE_SCSHELLPRIVATE_H
#include "SCSHELL.H"
#include "SCSHELLWIRE.H"
#include "SC32REPLACE.H"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

enum {SH_APP=1,SH_CATALOG=2,SH_LAYOUT=3,SH_SHORTCUT=4,SH_CLIENT=5,
      SH_REQUEST=6,SH_SERVICE=7,SH_JOB=8,SH_MENU=9,SH_DIALOG=10,SH_STORE=11,
      SH_DEFAULT_BUDGET=1048576,SH_SERVICE_BATCH=64,SH_JOB_BATCH=16};
typedef struct ShObject ShObject;
struct ShObject {
    ScU32 magic,kind,references,depth,closing;
    ScStatus notification_error;
    void (*dispose)(ShObject *);
    ShObject *previous,*next;
};
typedef struct {
    ScU32 references,revision,layer,flags,owner_pid,owner_generation;
    char *type,*application,*verb;
} ShAssociation;
struct ScShellApplication {
    ShObject base;ScShellApplicationInfo info;ScShellApplicationDesc desc;
    char *strings[6];ScShellVerbDesc *verbs;ScHash *verb_index;ScU32 default_verb;
};
struct ScShellLayout {ShObject base;ScShellLayoutInfo info;ScShellLayoutItem *items;};
struct ScShellCatalog {
    ShObject base;ScU32 epoch,next_token,store_revision;
    ScShellApplication **apps;ScU32 app_count;
    ShAssociation **associations;ScU32 association_count;ScShellLayout *layout;
    ScShellStore *commit_lock;
};
struct ScShellShortcut {ShObject base;ScShellShortcutDesc desc;char *strings[5];ScBuffer *legacy;};
struct ScShellRequest {
    ShObject base;ScShellRequestInfo info;ScShellErrorInfo error;ScBuffer *body,*result;
    ScU32 offset,rx_offset;ScShellRequest *next;ScShell *owner;
};
struct ScShell {
    ShObject base;ScShellInfo info;ScShellConnectDesc desc;ScWideCounter sequence;
    ScIpcChannel channel;ScShellRequest *head,*tail;ScBuffer *hello,*receive;
    ScU32 hello_sent;unsigned char *scratch;
};
typedef struct ShClient ShClient;
struct ShClient {
    ShClient *previous,*next;ScIpcChannel channel;ScBrokerIdentity peer;ScU32 hello;
    ScWideCounter sequence;ScShellWireHeader input,output;
    ScBuffer *receive,*send;ScU32 offset,changed_epoch;
    ScShellStore *store;ScShellCatalog *candidate;ScShellWireHeader transaction;ScBuffer *transaction_reply;
};
struct ScShellService {
    ShObject base;ScShellServiceDesc desc;ScShellServiceInfo info;
    ScShellCatalog *catalog;char *store;ScShellHostInfo host;ScBrokerIdentity self;
    ShClient *clients,*tail,*cursor;unsigned char *scratch;ScWideCounter next_sweep;
    ScU32 sweep_app,sweep_association,scan_remaining,ready_stores;
};
struct ScShellFileJob {
    ShObject base;ScShellFileJobDesc desc;ScShellFileProgress progress;
    ScShellFileResult conflict;ScArray *results;ScBuffer *plan,*outgoing,*receive,*clip;
    ScShellWireHeader output,input;ScWideCounter sequence;ScBrokerIdentity self,peer;
    ScIpcEndpoint listener;ScIpcChannel channel;ScJob managed;
    ScU32 child_pid,child_generation,stage,offset,conflict_valid,clip_epoch;
    ScU32 clip_operation,closed,cancel_requested;unsigned char *scratch;
};
typedef struct ShProvider ShProvider;
struct ShProvider {
    ShProvider *next;ScU32 token,references,removed;ScShellMenuProviderDesc desc;
};
struct ScShellMenu {
    ShObject base;ScShellMenuDesc desc;ScShellFileItem *items;char *directory;
    ScUiMenu *menu;ShProvider **providers;ScU32 provider_count;
    ScArray *commands;ScShellFileJob *job;ScShellDialog *dialog;ScUiDialog *prompt;
    char *recovery;ScU32 pending_command,prompt_kind,catalog_epoch;
    ScUiDialog *conflict_prompt;ScShellFileResult conflict_snapshot;
};
struct ScShellDialog {
    ShObject base;ScUiContext *ui;ScUiDialog *dialog;ScUiNode root,label,progress,cancel;
    ScU32 listener;ScShellFileJob *job;char *path;ScPathInfo metadata;ScConfig *values;
    ScUiNode mode,apply;ScLeaseInfo lease;ScU32 pending,requested_mode,close_requested;
    ScU32 kind,command,have_progress;ScShellFileProgress last_progress;
    ScUiNode details;ScUiDocument *document;ScU32 log_index;
};
struct ScShellStore {
    ShObject base;ScShellStoreInfo info;ScShellCatalog *catalog;ScBuffer *body;
    ScU32 expected_epoch,attempt,release_started;char *path,*temporary;ScLeaseInfo candidate,target;
    ScFile file,target_file;ScShellErrorInfo error;
};
static ShObject *sh_objects;
static ShProvider *sh_providers;
static ScU32 sh_provider_next=1,sh_quiet,sh_finalizing;
static ScWideCounter sh_job_next={1,0};
static ScShellErrorInfo sh_error={48,1,0,0,0,0,0,0,0,{0,0,0}};
static ScStatus sh_fail(ScStatus code,ScStatus cause,ScU32 operation,ScU32 item);
static ScStatus sh_cause(ScStatus cause,ScU32 operation,ScU32 item);
static ScStatus sh_record(const void *record,ScU32 bytes);
static ScStatus sh_output(void *out,const void *value,ScU32 bytes);
static ScStatus sh_text(const char *text,ScU32 maximum,ScU32 empty);
static ScStatus sh_copy_text(const char *text,ScU32 maximum,ScU32 empty,char **out);
static ScStatus sh_absolute(const char *path,char **out);
static void *sh_new(ScU32 bytes,ScU32 kind,void (*dispose)(ShObject *));
static int sh_valid(const void *object,ScU32 kind);
static ScStatus sh_retain(ShObject *object,ScU32 kind);
static void sh_release(ShObject *object);
static void sh_enter(ShObject *object);
static void sh_leave(ShObject *object);
static int sh_same_identity(const ScBrokerIdentity *,const ScBrokerIdentity *);
static ScStatus sh_self(ScBrokerIdentity *);
static ScStatus sh_buffer_copy(const ScBuffer *,ScBuffer **out);
static ScStatus sh_buffer_data(const ScBuffer *,const unsigned char **data,ScU32 *bytes);
static ScStatus sh_append_string(ScBuffer *,const char *,ScU32 *offset,ScU32 *bytes);
static ScStatus sh_string_at(const unsigned char *,ScU32 total,ScU32 floor,ScU32 offset,ScU32 bytes,ScU32 maximum,const char **out);
static ScStatus sh_next_counter(ScWideCounter *);
static ScU32 sh_crc(const void *,ScU32);
static ScStatus sh_read_file(const char *,ScBuffer **out);
static ScStatus sh_store_file(const char *,const ScBuffer *,ScU32 expected,ScU32 *revision);
static ScStatus sh_catalog_account_buffer(const ScShellCatalog *,ScBuffer **out);
static ScStatus sh_store_begin_buffer(ScShellCatalog *,ScBuffer *,const char *,ScU32 expected,ScU32 payload_epoch,ScShellStore **out);
static ScStatus sh_lease_finish(ScStatus,ScLeaseInfo *);
static ScStatus sh_wait(ScU32,ScU32,ScU32);
static ScStatus sh_wait_min(ScU32,ScU32,ScU32,ScU32);
static ScU32 sh_frame_bytes(const ScBuffer *,ScU32);
static int sh_io_ready(ScU32,const ScBuffer *,ScU32,ScU32 listener);
static ScStatus sh_application_encode(const ScShellApplication *,ScBuffer **out);
static ScStatus sh_application_decode(const void *,ScU32,ScShellApplication **out);
static ScStatus sh_application_new(const ScShellApplicationDesc *,ScShellApplication **out);
static ScStatus sh_catalog_clone(const ScShellCatalog *,ScShellCatalog **out);
static void sh_catalog_swap(ScShellCatalog *,ScShellCatalog *);
static ScStatus sh_layout_encode(const ScShellLayout *,ScBuffer **out);
static ScStatus sh_layout_decode(const void *,ScU32,ScShellLayout **out);
static ScStatus sh_association_encode(const ShAssociation *,ScBuffer **out);
static ScStatus sh_association_decode(const void *,ScU32,ShAssociation **out);
static void sh_association_release(ShAssociation *);
static ScStatus sh_frame_send(ScIpcChannel,const ScShellWireHeader *,const ScBuffer *,ScU32 *offset,unsigned char *scratch);
static ScStatus sh_frame_receive(ScIpcChannel,const ScBrokerIdentity *,unsigned char *scratch,ScShellWireHeader *,const unsigned char **body);
static ScStatus sh_collect(ScBuffer **,const ScShellWireHeader *,const unsigned char *,ScU32 budget,ScU32 *complete);
static ScStatus sh_request(ScShell *,ScU32 opcode,ScBuffer *,ScU32 epoch,ScU32 effect,ScShellRequest **out);
static ScStatus sh_file_items(const ScShellFileItem *,ScU32 count,ScShellFileItem **out);
static void sh_file_items_free(ScShellFileItem *,ScU32 count);
static ScStatus sh_clip_files_encode(const ScShellFileItem *,ScU32 count,ScU32 operation,ScBuffer **out);
static ScStatus sh_clip_files_decode(const void *,ScU32,ScShellFileItem **out,ScU32 *count,ScU32 *operation);
static ScStatus sh_clip_read(ScU32 format,ScBuffer **out,ScU32 *epoch);
static ScStatus sh_job_queue(ScShellFileJob *,ScU32 opcode,ScBuffer *body);
static ScStatus sh_job_hold(ScShellFileJob *);
static void sh_menu_provider_release(ShProvider *);
static void sh_ui_text(const char *,ScUiText *);
static ScStatus sh_widget(ScUiContext *,ScUiNode,ScU32 kind,const char *text,ScU32 command,ScI32 height,ScUiNode *out);
static ScStatus sh_dialog_label(ScShellDialog *,const char *);
static ScStatus sh_dialog_window(ScUiContext *,ScUiNode,const char *,ScU32,ScShellDialog **);
static ScStatus sh_menu_confirm_delete(ScShellMenu *);
static ScStatus sh_legacy_fields(const char *,char **,char **);
#endif
