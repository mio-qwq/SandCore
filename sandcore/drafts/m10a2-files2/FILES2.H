#ifndef SANDCORE_FILES2_PRIVATE_H
#define SANDCORE_FILES2_PRIVATE_H

#include <SC32API.H>
#include <SC32PATH.H>
#include <SC32LEASE.H>
#include <SC32BROKER.H>
#include <SC32HOST.H>
#include <SCSHELL.H>
#include <SCUI.H>
#include <SCLIB.H>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* 这是应用私有模型，绝不作为文件系统持久ID或内核对象布局。 */
enum {
    F2_PATH=68,F2_QUERY=256,F2_BATCH=24,F2_NONE=0xffffffffu,
    F2_BACK=0x8100,F2_FORWARD,F2_UP,F2_REFRESH,F2_GO,F2_FILTER,F2_SEARCH,
    F2_CANCEL_SEARCH,F2_BROWSE,F2_OPEN_FILE,F2_EDIT,F2_DEBUG,F2_OPEN_WITH,
    F2_NEW_FILE,F2_BOOKMARK_ADD,F2_BOOKMARK_REMOVE,F2_BOOKMARK_MENU,
    F2_SELECT_ALL,F2_SELECT_NONE,F2_CLOSE,F2_ABOUT,F2_ADDRESS,
    F2_DROP_COPY,F2_DROP_MOVE,F2_EXIT_CANCEL,F2_EXIT_WAIT,
    F2_BOOKMARK_FIRST=0x10000,F2_HISTORY_FIRST=0x20000,
    F2_NAV_NEW=1,F2_NAV_REFRESH=2,F2_NAV_HISTORY=3,F2_NAV_SEARCH=4,
    F2_WRITE_BOOKMARKS=1,F2_WRITE_EMPTY=2,
    F2_PROMPT_NEW=1,F2_PROMPT_RENAME_CONFLICT=2
};

typedef struct {
    ScU32 id,kind,bytes,revision,rw;
    ScI32 uid,gid;
    char path[F2_PATH],owner[32],access[16],type[32];
} F2Entry;

typedef struct {
    F2Entry *rows;
    ScU32 count,capacity,visible,*order,*rank,search,skipped,directory_revision;
    ScHash *paths,*ids;
    char path[F2_PATH],query[F2_QUERY];
} F2Snapshot;

typedef struct {char path[F2_PATH];} F2Path;

typedef struct {
    F2Snapshot *snapshot;
    ScDirectory directory;
    F2Path *queue;
    ScU32 count,capacity,cursor,active,visited,nav_mode,history_index;
    char enumerating[F2_PATH];
} F2Scan;

typedef struct {
    F2Snapshot *snapshot;
    ScU32 active,publish,phase,field,descending,position,count;
    ScU32 *order,*scratch,*rank,width,left,middle,last,a,b,out,merging;
    ScU32 nav_mode,history_index;
    char filter[F2_QUERY];
} F2Order;

typedef struct {
    ScU32 id,parent,sibling,revision,loaded,queued,error;
    ScU32 *children,count;
    char path[F2_PATH];
} F2TreeNode;

typedef struct {char path[F2_PATH];ScU32 revision;} F2TreeChild;
typedef struct {
    ScDirectory directory;
    ScU32 node,count,capacity;
    F2TreeChild *children;
} F2TreeScan;

typedef struct {char path[F2_PATH];} F2Bookmark;
typedef struct F2MenuWork F2MenuWork;
struct F2MenuWork {F2MenuWork *next;ScShellMenu *menu;ScStatus last_error;};

typedef struct F2Job F2Job;
struct F2Job {
    F2Job *next;
    ScShellFileJob *handle;
    ScShellDialog *progress;
    ScUiDialog *conflict;
    ScShellFileResult observation;
    ScU32 final,read_index,reviewed,durable,retained,uncertain,failed,skipped;
    ScU32 last_state,last_item,conflict_epoch;
    ScU32 decision;
    ScStatus last_error;
};

typedef struct {
    ScU32 active,purpose,stage,offset,total,expected_revision,serial,cancel,committed,attempts;
    ScStatus result;
    ScFile file,target_file;
    ScLeaseInfo candidate,target;
    ScString *body;
    char path[F2_PATH],temporary[F2_PATH];
} F2Writer;

typedef struct {
    ScUiContext *ui;
    ScUiNode root,menubar,toolbar,address,filter,search,body,sidebar,tree,splitter,
        table,status,location,scan_progress;
    ScUiProvider *table_provider,*tree_provider;
    ScUiSelection *selection,*tree_selection;
    ScUiMenu *menus,*places,*history_menu;
    ScUiDialog *dialog;
    ScShellDialog *properties;
    ScShell *shell;
    ScShellCatalog *catalog;
    ScShellRequest *catalog_request;
    ScShellMenu *shell_menu;
    F2MenuWork *menu_work;
    F2Job *jobs,*prompt_job;
    F2Snapshot *current;
    F2Scan scan;
    F2Order order;
    F2TreeNode *nodes;
    F2TreeScan tree_scan;
    ScU32 node_count,node_capacity,*tree_queue,tree_count,tree_capacity,tree_cursor;
    ScHash *tree_index,*icons;
    F2Path *history;
    ScU32 history_count,history_capacity,history_index;
    F2Bookmark *bookmarks;
    ScU32 bookmark_count,bookmark_capacity,bookmark_revision,bookmark_serial,saved_serial;
    F2Writer writer;
    ScIdentityInfo identity;
    ScUiMetrics metrics;
    ScStatus menu_error;
    ScU32 next_item,table_revision,tree_revision,sort_field,sort_descending,publishing,
        visible,quit,exit_pending,refresh_pending,filter_pending,bookmark_save_pending,
        popup,drag_token,gesture,last_click,last_tick,drop_item,prompt_kind,catalog_dirty,
        recovery_serial,ui_error,drop_revision,reconnect_timer,reconnect_pending;
    ScI32 press_x,press_y;
    void *drop_bytes;
    ScUiDragData pending_drop;
    char drop_destination[F2_PATH],recovery_directory[F2_PATH],bookmark_path[F2_PATH],
        recovery_path[F2_PATH],filter_text[F2_QUERY],pending_file[F2_PATH],message[320];
} F2App;

static F2App f2;
static ScStatus f2_error(ScStatus,const char *);
static void f2_status(const char *);
static ScStatus f2_command(ScU32);
static ScStatus f2_navigate(const char *,ScU32,ScU32,const char *);
static void f2_order_cancel(void);
static ScStatus f2_order_begin(F2Snapshot *,ScU32,ScU32,ScU32,ScU32,ScU32);
static ScStatus f2_table_notify(ScU32);
static ScStatus f2_menu_prepare(ScU32,ScI32,ScI32,ScU32);
static ScStatus f2_shell_event(const ScShellEvent *,void *);
static ScStatus f2_open_items(const ScShellFileItem *,ScU32,ScU32);
static ScStatus f2_writer_begin(const char *,ScString *,ScU32,ScU32);
static ScStatus f2_places_refresh(void);
static ScStatus f2_history_refresh(void);
static ScStatus f2_tree_invalidate(void);
static ScStatus f2_dialog_complete(ScUiContext *,const ScUiEvent *,void *);
static ScUiNode f2_dialog_owner(void);
static ScStatus f2_prompt(ScU32,F2Job *);
static ScStatus f2_job_adopt(ScShellFileJob *);
static void f2_gesture_cancel(ScU32);

#endif
