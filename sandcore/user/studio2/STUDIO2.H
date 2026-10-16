#ifndef STUDIO2_PRIVATE_H
#define STUDIO2_PRIVATE_H
#include <SCUI.H>
#include <SCUITABS.H>
#include <SCSHELL.H>
#include <SCSHELLABSENT.H>
#include <SC32CAPTURE.H>
#include <SC32REPLACE.H>
#include <SCOFMT.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int U;
typedef struct StDoc StDoc;
typedef struct {
    ScLeaseInfo info;
    ScU32 live;
}
StPin;
typedef struct {
    U start,end,role;
}
StToken;
typedef struct {
    char *text;
    U bytes,state_in,state_out;
    StToken *tokens;
    U count,capacity;
}
StLine;
struct StDoc {
    StDoc *next;
    U id,disk_revision,revision,syntax_revision;
    char path[68],label[96];
    ScUiDocument *text;
    ScUiNode page,edit;
    StLine *lines;
    U line_count;
};
typedef struct {
    U id,source;
    char path[68];
}
StMember;
typedef struct {
    U id,generation,line;
    char path[68],message[768];
}
StDiagnostic;
typedef struct {
    U revision;
    char path[68];
}
StDependency;
typedef struct {
    U generation,doc_id,doc_revision;
    StMember member;
    StPin pin;
    char object[68];
}
StInput;
typedef struct {
    ScCaptureInfo capture;
    StPin artifact;
    U ended,eof[2],cancel;
    ScI32 exit_code;
    char pending[2][4096];
    U used[2];
}
StChild;
typedef struct {
    U phase,kind,owner,epoch,revision,total,offset,cancel,seed;
    ScStatus status;
    char path[68],candidate[68];
    StPin source,target;
    ScBuffer *buffer;
    ScString *snapshot;
    U mode;
}
StIo;
typedef struct {
    U phase,generation,epoch,next,input_count,linking,cancel,publishing;
    ScStatus error;
    StInput *inputs;
    StDependency *dependencies;
    U dependency_count,dependency_capacity;
    StPin manifest,system_headers;
    U manifest_revision,manifest_doc,manifest_document_revision,verify_count,verify_next,verify_files,verify_strings,verify_bytes;
    StChild child;
    char directory[68],candidate[68],response[68];
    ScShellFileJob *filejob;
    ScShellDialog *progress;
    ScShellFileItem items[3];
    char src[3][68],dst[3][68];
}
StBuild;
typedef struct {
    ScUiContext *ui;
    ScUiNode root,menu,toolbar,status,body,upper,tree_panel,tree,tree_split,tabs,lower_split,out_panel,out_tabs,output_page,diagnostic_page,output,diagnostics,search,needle,replacement,caseless;
    ScUiNode float_output,float_tree;
    ScUiMenu *menus[7];
    ScUiProvider *tree_provider,*diag_provider;
    ScUiSelection *tree_selection,*diag_selection;
    StDoc *documents,*active;
    U document_serial,member_serial,epoch,project_saved,project_dirty,project_revision,project_disk_revision,tree_revision,diag_revision,diag_serial;
    StMember *members;
    U member_count,member_capacity;
    StDiagnostic *errors;
    U error_count,error_capacity;
    ScConfig *config;
    char project_path[68],compiler[68],linker[68],manifest_path[68],output_path[68],debug_host[68],publish_dir[68],workspace[68],includes[256],arguments[512];
    U target_scl;
    ScUiDocument *output_document;
    U output_bytes;
    char message[256],title[192];
    U command_flags[64],busy_refresh,quit_requested,stopping,success,success_epoch,success_generation,output_revision,output_revisions[3];
    StIo *io;
    StBuild build;
    StChild running;
    ScUiDialog *dialog,*notice;
    U dialog_kind,dialog_owner,pending_action,pending_doc,jump_line,jump_doc,click_id,click_tick;
    U save_cursor,save_all,build_requested,config_save_requested,compact;
    ScStatus config_error;
}
Studio;
static Studio st;
enum {
    S_NEW=0x8600,S_OPEN,S_SAVE,S_SAVE_AS,S_SAVE_ALL,S_CLOSE,S_PROJECT_NEW,S_PROJECT_OPEN,S_PROJECT_SAVE,S_ADD,S_REMOVE,S_CONFIG,
    S_UNDO,S_REDO,S_CUT,S_COPY,S_PASTE,S_ALL,S_FIND,S_NEXT,S_PREVIOUS,S_REPLACE,S_REPLACE_ALL,S_HIDE_FIND,
    S_BUILD,S_CANCEL,S_RUN,S_DEBUG,S_PUBLISH,S_FLOAT_OUTPUT,S_DOCK_OUTPUT,S_FLOAT_TREE,S_DOCK_TREE,S_LAYOUT_SAVE,S_LAYOUT_RESET,S_SDK,S_ABOUT,S_EXIT,S_COUNT,
    S_DIALOG_OPEN=1,S_DIALOG_NEW,S_DIALOG_SAVE,S_DIALOG_PROJECT_OPEN,S_DIALOG_PROJECT_SAVE,S_DIALOG_ADD,S_DIALOG_PUBLISH,S_DIALOG_CONFIG,S_DIALOG_CONFIRM,
    S_CONFIRM_SAVE=0x8700,S_CONFIRM_DISCARD,S_CONFIRM_CANCEL,
    S_IO_OPEN=1,S_IO_SAVE,S_IO_PROJECT_OPEN,S_IO_PROJECT_SAVE,
    S_READ=1,S_WRITE,S_FLUSH,S_PUBLISH_IO,S_ADVANCE,S_CLEAN,
    S_BUILD_SAVE=1,S_BUILD_PIN,S_BUILD_COMPILE,S_BUILD_LINK,S_BUILD_COPY,S_BUILD_SETTLE,S_BUILD_CLEAN,
    S_ACTION_CLOSE=1,S_ACTION_EXIT,S_ACTION_PROJECT_OPEN,S_ACTION_PROJECT_NEW
};
static void rec(void *,U);
static ScUiText text(const char *);
static void status(const char *);
static void error(ScStatus,const char *);
static ScStatus event(ScUiContext *,const ScUiEvent *,void *);
static ScStatus dialog_done(ScUiContext *,const ScUiEvent *,void *);
static ScStatus widget(ScUiNode,U,const char *,U,ScUiNode *);
static ScStatus layout(ScUiNode,U,int,int,U,U);
static ScStatus doc_new(const char *,const void *,U,U,StDoc **);
static ScStatus file_dialog(U);
static void refresh(void);
static void changed(void);
static void syntax(StDoc *);
static void syntax_free(StDoc *);
static ScStatus io_open(const char *,U,U);
static ScStatus io_save(StDoc *,const char *);
static ScStatus io_project_save(const char *);
static void io_step(void);
static ScStatus pin_open(StPin *,const char *,U,U);
static ScStatus pin_close(StPin *);
static ScStatus path(const char *,char [68]);
static int same_name(const char *,const char *);
static ScStatus project_load(const void *,U,U,const char *);
static ScStatus project_defaults(void);
static ScStatus project_add(const char *);
static void project_notify(void);
static ScStatus config_dialog(void);
static void config_set(ScConfig *,const char *,const char *,const char *);
static ScStatus config_read(ScConfig *);
static ScStatus project_encode(ScString **);
static ScStatus build_begin(void);
static void build_step(void);
static void child_step(StChild *,U);
static ScStatus spawn(StChild *,const char *,U);
static void output_append(U,const void *,U);
static ScStatus run(U);
static ScStatus publish_begin(const char *,U);
static void navigate(U);
static ScStatus request(U);
static void perform(U);
static void command(U);
static void ui_tick(void);
#endif
