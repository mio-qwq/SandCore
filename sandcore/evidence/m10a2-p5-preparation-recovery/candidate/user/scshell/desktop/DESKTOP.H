#ifndef SANDCORE_DESKTOP_PRIVATE_H
#define SANDCORE_DESKTOP_PRIVATE_H
#include <SCSHELL.H>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 私有模型由桌面进程独占；路径与显示名分离，任何截断只发生在绘制。 */
typedef struct {
    char *identity,*label,*path;
    ScShellShortcut *shortcut;
    ScUiIcon *icon;
    ScI32 x,y,old_x,old_y;
    ScU32 selected,old_selected,kind,observed_revision,next_cell,next_icon,icon_failed,removed;
} DeskItem;
typedef struct {ScI32 x,y;ScU32 first,used;} DeskCell;
typedef struct {ScShellWindowInfo value;ScUiRect button;} DeskTask;
typedef struct {ScU32 command;ScShellApplication *app;ScU32 verb;} DeskCommand;
typedef struct DeskMenuWork DeskMenuWork;
struct DeskMenuWork {DeskMenuWork *next;ScShellMenu *menu;};
typedef struct {
    ScUiContext *ui;
    ScUiNode desktop,bar,canvas,task_canvas;
    ScUiClass desktop_class,task_class;
    ScShellCatalog *catalog;
    ScShellService *service;
    ScShellStore *store;
    ScIpcEndpoint endpoint;
    ScShellHostInfo host;
    ScIdentityInfo identity;
    ScUiMetrics metrics;
    ScUiMenu *menu;
    ScShellMenu *shell_menu;
    DeskMenuWork *menu_work;
    ScU32 popup,epoch,store_revision,layout_mode,grid_w,grid_h;
    ScU32 layout_epoch,save_epoch,bar_height;
    ScU32 layout_unsaved,layout_serial,store_serial;
    DeskItem *items;ScU32 count,capacity;
    ScDirectory refresh_directory;DeskItem *refresh_items;ScU32 refresh_count,refresh_capacity;
    ScDirectoryPage refresh_page;
    DeskCell *cells;ScU32 cell_capacity;
    ScHash *item_index;
    ScU32 loaded_icons,icons_pending,icon_errors;
    DeskTask *tasks,*task_candidate;ScU32 task_count,task_capacity,task_used,task_allocated;
    ScU32 task_cursor,task_epoch,task_scanning,task_first,bar_focus;
    DeskCommand *commands;ScU32 command_count,command_capacity;
    ScU32 focus,anchor,pressed,last_click,last_click_tick;
    ScU32 *drag_items,drag_count,drag_capacity;
    ScI32 down_x,down_y,pointer_x,pointer_y,scroll_x,scroll_y,extent_w,extent_h;
    ScU32 gesture,moved,selection_modifiers,visible,active,quit,refresh_pending;
    ScU32 save_pending,timer,last_second,task_dirty,task_tick,clock_tick,clock_hour,clock_minute,clock_dirty;
    ScUiRect rubber;
    ScShellFileJob *job;ScU32 job_done;ScShellDialog *progress,*properties;
    ScUiDialog *dialog;
    ScU32 edit_item,edit_revision,edit_new;char edit_path[68];
    ScUiDragData pending_drop;void *drop_bytes;
    ScU32 recovery_nonce;
    ScU32 external_drag;
    ScU32 drop_target;
    char account_name[33];
    char desktop_path[68],store_path[68],recovery_path[68],recovery_directory[68],drop_destination[68],clock[96];
} Desktop;

enum {
    DESK_NONE=0xffffffffu,DESK_DRAG=1,DESK_BOX=2,
    DESK_OPEN=0x7200,DESK_REFRESH=0x7201,DESK_GRID=0x7202,DESK_AUTO=0x7203,
    DESK_FREE=0x7204,DESK_SELECT_ALL=0x7205,DESK_SETTINGS=0x7206,
    DESK_LOGOUT=0x7207,DESK_CASCADE=0x7208,DESK_TILE=0x7209,
    DESK_TASK_FOCUS=0x7210,DESK_TASK_MINIMIZE=0x7211,
    DESK_TASK_MAXIMIZE=0x7212,DESK_TASK_RESTORE=0x7213,DESK_TASK_CLOSE=0x7214,
    DESK_DROP_COPY=0x7220,DESK_DROP_MOVE=0x7221,DESK_DROP_LINK=0x7222,
    DESK_LAYOUT_RELOAD=0x7230,
    DESK_NEW_SHORTCUT=0x7240,
    DESK_EXIT=0x7241,
    DESK_START_COMMAND=0x10000
};

static Desktop desk;
static ScUiText desk_text(const char *);
static ScStatus desk_error(ScStatus,const char *);
static ScStatus desk_refresh(void);
static ScStatus desk_layout_save(void);
static ScStatus desk_layout_apply(ScU32);
static ScStatus desk_open(ScU32);
static ScStatus desk_menu(ScU32,ScI32,ScI32);
static ScStatus desk_command(ScU32);
static ScStatus desk_tasks_step(void);
static ScStatus desk_tasks_action(ScU32,ScU32);
static ScStatus desk_job_event(const ScShellEvent *,void *);
static ScStatus desk_input(ScUiContext *,const ScUiEvent *,void *);
static ScStatus desk_paint(ScUiContext *,ScUiNode,ScUiPainter *,void *);
static ScStatus desk_bar_paint(ScUiContext *,ScUiNode,ScUiPainter *,void *);
static ScStatus desk_bar_input(ScUiContext *,const ScUiEvent *,void *);
static ScStatus desk_drop(ScUiContext *,const ScUiEvent *,void *);
static void desk_invalidate_item(ScU32);
static ScStatus desk_index(void);
static ScStatus desk_layout_conflict(void);
static ScStatus desk_begin_transfer(void);
static ScStatus desk_icons_step(void);
static void desk_icon_unlink(ScU32);
static void desk_layout_changed(void);
#endif
