#ifndef SANDCORE_SCUI_H
#define SANDCORE_SCUI_H
#include "SC32API.H"
#include "SC32BROKER.H"
#include "SC32DROP.H"
#include "SCLIB.H"

/* 控件树与回调只在本人Ring3拥有；完整正文由SCUI.SCL动态装载。 */

enum {
    SC_UI_ABI_MAJOR=1,SC_UI_ABI_MINOR=0,SC_UI_VERSION=1,
    SC_UI_OK=0,SC_UI_HANDLED=1,SC_UI_VETO=2,SC_UI_END=3,SC_UI_PENDING=4,
    SC_UI_E_INVALID=-301,SC_UI_E_VERSION=-302,SC_UI_E_RANGE=-303,
    SC_UI_E_MEMORY=-304,SC_UI_E_STALE=-305,SC_UI_E_NOT_FOUND=-306,
    SC_UI_E_EXISTS=-307,SC_UI_E_BUSY=-308,SC_UI_E_UTF8=-309,
    SC_UI_E_IO=-310,SC_UI_E_CANCELLED=-311,SC_UI_E_NOT_SUPPORTED=-312,SC_UI_E_FAILED=-313,
    SC_UI_WINDOW=1,SC_UI_PANEL=2,SC_UI_BUTTON=3,SC_UI_LABEL=4,
    SC_UI_CHECKBOX=5,SC_UI_RADIO=6,SC_UI_GROUPBOX=7,SC_UI_TEXTBOX=8,
    SC_UI_EDIT=9,SC_UI_COMBOBOX=10,SC_UI_SPINBOX=11,SC_UI_TAB=12,
    SC_UI_TREE=13,SC_UI_LIST=14,SC_UI_MENUBAR=15,SC_UI_TOOLBAR=16,
    SC_UI_STATUS=17,SC_UI_PROGRESS=18,SC_UI_TOOLTIP=19,SC_UI_SCROLL=20,
    SC_UI_SPLITTER=21,SC_UI_DOCK=22,SC_UI_GRID=23,SC_UI_TABLE=24,
    SC_UI_PROPERTY_PAGE=25,SC_UI_CLASS_FIRST=65536,
    SC_UI_VISIBLE=1,SC_UI_ENABLED=2,SC_UI_FOCUSED=4,SC_UI_HOVER=8,
    SC_UI_PRESSED=16,SC_UI_CHECKED=32,SC_UI_MIXED=64,SC_UI_SELECTED=128,
    SC_UI_READONLY=256,SC_UI_INVALID=512,SC_UI_EXPANDED=1024,SC_UI_BUSY=2048,
    SC_UI_FOCUSABLE=1,SC_UI_TABSTOP=2,SC_UI_EDITOR_TAB=4,
    SC_UI_LABEL_SELECTABLE=8,SC_UI_PASSWORD=16,
    SC_UI_THREE_STATE=32,SC_UI_WRAP=64,
    SC_UI_STOP_LAST_ROOT=1,SC_UI_SCALE_DISPLAY=0,SC_UI_SCALE_CUSTOM=1,
    SC_UI_TIMER_DRAW_ONLY=1,SC_UI_TIMER_BACKGROUND=2,
    SC_UI_LISTEN_ONCE=1,
    SC_UI_FONT_BODY=0,SC_UI_FONT_DENSE=1,SC_UI_FONT_HEADING=2,SC_UI_FONT_MENU=3,SC_UI_FONT_CODE=4,
    SC_UI_PROGRESS_DETERMINATE=0,SC_UI_PROGRESS_INDETERMINATE=1,
    SC_UI_DRAG_COPY=1,SC_UI_DRAG_MOVE=2,
    SC_UI_DOCK_TOP=1,SC_UI_DOCK_BOTTOM=2,SC_UI_DOCK_LEFT=3,SC_UI_DOCK_RIGHT=4,SC_UI_DOCK_FILL=5,
    SC_UI_DOCUMENT_READONLY=1,SC_UI_DOCUMENT_DIRTY=2,SC_UI_DOCUMENT_PRESERVE_EOL=4,
    SC_UI_REPLACE_NO_UNDO=1,SC_UI_REPLACE_NEW_UNDO=2,
    SC_UI_FIND_BACKWARD=1,SC_UI_FIND_ASCII_CASELESS=2,SC_UI_FIND_WHOLE_WORD=4,SC_UI_FIND_WRAP=8,
    SC_UI_BOUNDARY_GRAPHEME=1,SC_UI_BOUNDARY_WORD=2,
    SC_UI_STYLE_UNDERLINE=1,SC_UI_STYLE_STRIKE=2,
    SC_UI_FILE_OPEN=1,SC_UI_FILE_SAVE=2,SC_UI_FILE_DIRECTORY=4,SC_UI_FILE_MULTI=8,SC_UI_FILE_OVERWRITE=16,
    SC_UI_DIALOG_CREATING=1,SC_UI_DIALOG_OPEN=2,SC_UI_DIALOG_VALIDATING=3,
    SC_UI_DIALOG_DONE=4,SC_UI_DIALOG_CANCELLED=5,
    SC_UI_DIALOG_OK=1,SC_UI_DIALOG_CANCEL=2,SC_UI_DIALOG_APPLY=3,
    SC_UI_FIELD_READONLY=1,SC_UI_FIELD_REQUIRED=2,
    SC_UI_ACCESSIBLE_NAME=1,SC_UI_ACCESSIBLE_VALUE=2,SC_UI_ACCESSIBLE_HELP=3,
    SC_UI_ACCESS_FOCUS=1,SC_UI_ACCESS_INVOKE=2,SC_UI_ACCESS_TOGGLE=3,SC_UI_ACCESS_SELECT=4,
    SC_UI_ACCESS_EXPAND=5,SC_UI_ACCESS_SCROLL=6,SC_UI_ACCESS_SET_VALUE=7,
    SC_UI_COLUMN_SORTABLE=1,SC_UI_COLUMN_EDITABLE=2,SC_UI_COLUMN_RIGHT=4,SC_UI_COLUMN_HIDDEN=8,
    SC_UI_FORMAT_TEXT=0,SC_UI_FORMAT_I32=1,SC_UI_FORMAT_U32=2,SC_UI_FORMAT_BYTES=3,
    SC_UI_FORMAT_HEX=4,SC_UI_FORMAT_COUNTER=5,
    SC_UI_SORT_TEXT=0,SC_UI_SORT_SIGNED=1,SC_UI_SORT_UNSIGNED=2,SC_UI_SORT_COUNTER=3,
    SC_UI_ABSOLUTE=0,SC_UI_HBOX=1,SC_UI_VBOX=2,SC_UI_DOCK_LAYOUT=3,
    SC_UI_GRID_LAYOUT=4,SC_UI_FLOW=5,SC_UI_OVERLAY=6,SC_UI_SCROLL_LAYOUT=7,
    SC_UI_AUTO=-1,SC_UI_START=0,SC_UI_CENTER=1,SC_UI_END_ALIGN=2,SC_UI_STRETCH=3,
    SC_UI_CAPTURE_PHASE=1,SC_UI_TARGET_PHASE=2,SC_UI_BUBBLE_PHASE=4,
    SC_UI_EVENT_DEVICE=1,SC_UI_EVENT_PROGRAMMATIC=2,SC_UI_EVENT_CANCELABLE=4,
    SC_UI_MENU_SEPARATOR=1,SC_UI_MENU_DISABLED=2,SC_UI_MENU_CHECKED=4,
    SC_UI_MENU_RADIO=8,SC_UI_MENU_DEFAULT=16,
    SC_UI_PROVIDER_SORT=1,SC_UI_PROVIDER_EDIT=2,SC_UI_PROVIDER_TREE=4,SC_UI_PROVIDER_ASYNC=8,
    SC_UI_DATA_COUNT=1,SC_UI_DATA_CHILD_COUNT=2,SC_UI_DATA_CHILD_AT=3,
    SC_UI_DATA_ITEM=4,SC_UI_DATA_CELL=5,SC_UI_DATA_INDEX_OF=6,
    SC_UI_DATA_SORT=7,SC_UI_DATA_COMMIT_EDIT=8,SC_UI_DATA_FIND_PREFIX=9,
    SC_UI_DATA_INSERT=1,SC_UI_DATA_REMOVE=2,SC_UI_DATA_RESET=3,SC_UI_DATA_UPDATE=4,SC_UI_DATA_SORTED=5,SC_UI_DATA_MOVE=6,
    SC_UI_SELECTION_MULTI=1,SC_UI_SELECTION_ALLOW_EMPTY=2,
    SC_UI_REPLACE=0,SC_UI_ADD=1,SC_UI_REMOVE=2,SC_UI_TOGGLE=3,
    SC_UI_EVENT_CREATE=1,SC_UI_EVENT_DESTROY=2,SC_UI_EVENT_FOCUS_IN=3,SC_UI_EVENT_FOCUS_OUT=4,
    SC_UI_EVENT_KEY_DOWN=5,SC_UI_EVENT_KEY_UP=6,SC_UI_EVENT_TEXT=7,
    SC_UI_EVENT_POINTER_ENTER=8,SC_UI_EVENT_POINTER_LEAVE=9,SC_UI_EVENT_POINTER_MOVE=10,
    SC_UI_EVENT_POINTER_DOWN=11,SC_UI_EVENT_POINTER_UP=12,SC_UI_EVENT_WHEEL=13,
    SC_UI_EVENT_CAPTURE_LOST=14,SC_UI_EVENT_COMMAND=15,
    SC_UI_EVENT_VALUE_CHANGING=16,SC_UI_EVENT_VALUE_CHANGED=17,
    SC_UI_EVENT_SELECTION_CHANGING=18,SC_UI_EVENT_SELECTION_CHANGED=19,
    SC_UI_EVENT_OPENING=20,SC_UI_EVENT_CLOSING=21,SC_UI_EVENT_CLOSE_REQUEST=22,
    SC_UI_EVENT_LAYOUT=23,SC_UI_EVENT_PAINT=24,SC_UI_EVENT_TIMER=25,
    SC_UI_EVENT_MODEL_CHANGED=26,SC_UI_EVENT_ERROR=27,SC_UI_EVENT_COMMAND_QUERY=28,
    SC_UI_EVENT_DIALOG_COMPLETE=29,SC_UI_EVENT_VALIDATE=30,SC_UI_EVENT_APPLY=31,
    SC_UI_EVENT_DRAG_ENTER=32,SC_UI_EVENT_DRAG_OVER=33,SC_UI_EVENT_DRAG_LEAVE=34,
    SC_UI_EVENT_DROP=35,SC_UI_EVENT_INPUT_SYNC=36,SC_UI_EVENT_VISIBILITY=37,SC_UI_EVENT_USER=32768
};

typedef struct ScUiContext ScUiContext;
typedef struct ScUiPainter ScUiPainter;
typedef struct ScUiMenu ScUiMenu;
typedef struct ScUiProvider ScUiProvider;
typedef struct ScUiSelection ScUiSelection;
typedef struct ScUiDocument ScUiDocument;
typedef struct ScUiDialog ScUiDialog;
typedef struct ScUiIcon ScUiIcon;
typedef ScU32 ScUiNode;
typedef ScU32 ScUiClass;

typedef struct {ScI32 x,y,width,height;} ScUiRect;
typedef struct {ScI32 width,height;} ScUiSize;
typedef struct {ScU32 size,version;const char *data;ScU32 bytes;} ScUiText;
typedef struct {ScU32 size,version,key,modifiers;} ScUiShortcut;

typedef struct {
    ScU32 size,version,type,phase,flags,node,current,root,command,tick,input_epoch;
    ScI32 x,y;ScU32 key,scalar,modifiers,buttons;
    const ScMessage *message;const void *data;ScU32 bytes;
    ScWideCounter sequence;ScU32 reserved[2];
} ScUiEvent;
typedef ScStatus (*ScUiEventProc)(ScUiContext *,const ScUiEvent *,void *);

typedef struct {
    ScU32 size,version,flags,cache_budget_bytes,undo_budget_bytes,scheduling_quantum;
    ScU32 default_font_face,default_font_pixels,scale_mode,scale_percent;
    ScUiEventProc error_procedure;void *context;ScU32 reserved[4];
} ScUiContextDesc;
typedef struct {
    ScU32 size,version;ScUiText title;ScI32 x,y;
    ScU32 client_width,client_height,style,owner;
    void *context;ScDropProc context_drop;ScU32 reserved[2];
} ScUiWindowDesc;
typedef struct {
    ScU32 size,version,kind,parent,command,flags;ScUiText text;
    void *context;ScDropProc context_drop;ScU32 reserved[4];
} ScUiWidgetDesc;
typedef struct {
    ScU32 size,version,node,generation,kind,parent,first_child,next_sibling;
    ScU32 root,native_window,state,flags;ScUiRect rectangle,clip;
    ScU32 value_revision,layout_revision,font_role,reserved;
} ScUiNodeInfo;
typedef struct {
    ScU32 size,version,mode,flags;
    ScI32 left,top,width,height,min_width,min_height,max_width,max_height;
    ScU32 flex_weight,dock;ScI32 margin[4],padding[4];
    ScU32 gap,columns,column,row,column_span,row_span,align_h,align_v,reserved[2];
} ScUiLayoutDesc;
typedef struct {
    ScU32 size,version,type,phases;ScI32 priority;
    ScUiEventProc procedure;void *context;ScDropProc context_drop;
    ScU32 flags,reserved[3];
} ScUiSubscription;
typedef struct {
    ScU32 size,version,nodes,roots,timers,listeners,layout_revision;ScI32 notification_error;
    ScU32 surface_bytes,cache_bytes,dirty_roots,visible_roots;
    ScWideCounter received_messages,paint_count,submitted_bytes;ScU32 reserved[2];
} ScUiContextInfo;
typedef struct {
    ScU32 size,version;ScI32 code,cause_code;ScU32 domain;ScI32 raw;
    ScU32 operation,node,revision,reserved[3];
} ScUiErrorInfo;
typedef struct {
    ScU32 size,version,scale_percent,font_face,font_pixels,font_epoch,theme_epoch,display_epoch;
    ScU32 unit,row_height,menu_height,toolbar_height,status_height,gap,splitter,border;
} ScUiMetrics;
typedef struct {
    ScU32 size,version,node,flags;ScWideCounter deadline;
    ScU32 period_ticks,reserved;
} ScUiTimerDesc;
typedef struct {
    ScU32 size,version,token,period_ticks;ScWideCounter missed;ScU32 flags,reserved;
} ScUiTimerEvent;
typedef struct {
    ScU32 size,version,width,height,lines,baseline,generation,missing_glyphs;
} ScUiTextMetrics;
typedef struct {
    ScU32 size,version,width,height,format,flags,revision,bytes;
} ScUiIconInfo;

typedef ScStatus (*ScUiMeasureProc)(ScUiContext *,ScUiNode,const ScUiSize *,ScUiSize *,void *);
typedef ScStatus (*ScUiArrangeProc)(ScUiContext *,ScUiNode,const ScUiRect *,void *);
typedef ScStatus (*ScUiPaintProc)(ScUiContext *,ScUiNode,ScUiPainter *,void *);
typedef struct {
    ScU32 size,version,flags;const char *name;
    ScUiMeasureProc measure;ScUiArrangeProc arrange;ScUiPaintProc paint;
    ScUiEventProc event;void *context;ScDropProc context_drop;ScU32 reserved[6];
} ScUiClassDesc;


ScStatus ScUiContextCreate(const ScUiContextDesc *,ScUiContext **out);
ScStatus ScUiContextRetain(ScUiContext *);
void ScUiContextRelease(ScUiContext *);
ScStatus ScUiContextQuery(const ScUiContext *,ScUiContextInfo *out);
/* after=0取首个活原生root；非零只接受本context仍活root，END/错误不写out。 */
ScStatus ScUiContextRootNext(ScUiContext *,ScUiNode after,ScUiNode *out);
ScStatus ScUiErrorQuery(const ScUiContext *,ScUiErrorInfo *out);
ScStatus ScUiContextStop(ScUiContext *,ScI32 exit_code);
ScStatus ScUiContextRun(ScUiContext *,ScI32 *exit_code);
ScStatus ScUiContextStep(ScUiContext *,ScU32 timeout_ticks);
ScStatus ScUiContextDispatch(ScUiContext *,const ScMessage *);
ScStatus ScUiWindowCreate(ScUiContext *,const ScUiWindowDesc *,ScUiNode *out);
ScStatus ScUiWindowNative(ScUiContext *,ScUiNode,ScWindowInfo *out);
ScStatus ScUiWindowShow(ScUiContext *,ScUiNode,ScU32 action);
ScStatus ScUiWindowSetTitle(ScUiContext *,ScUiNode,const ScUiText *);
ScStatus ScUiWidgetCreate(ScUiContext *,const ScUiWidgetDesc *,ScUiNode *out);
ScStatus ScUiNodeDestroy(ScUiContext *,ScUiNode);
ScStatus ScUiNodeQuery(const ScUiContext *,ScUiNode,ScUiNodeInfo *out);
ScStatus ScUiNodeMove(ScUiContext *,ScUiNode,ScUiNode parent,ScUiNode before);
ScStatus ScUiNodeSetState(ScUiContext *,ScUiNode,ScU32 mask,ScU32 value);
ScStatus ScUiNodeSetText(ScUiContext *,ScUiNode,const ScUiText *);
ScStatus ScUiNodeCopyText(ScUiContext *,ScUiNode,ScString **out);
ScStatus ScUiNodeSetValue(ScUiContext *,ScUiNode,const ScValue *);
ScStatus ScUiNodeGetValue(ScUiContext *,ScUiNode,ScValue *out);
ScStatus ScUiNodeSetIcon(ScUiContext *,ScUiNode,ScUiIcon *);
ScStatus ScUiNodeSetFont(ScUiContext *,ScUiNode,ScU32 font_role);
ScStatus ScUiNodeSetLayout(ScUiContext *,ScUiNode,const ScUiLayoutDesc *);
ScStatus ScUiNodeGetLayout(ScUiContext *,ScUiNode,ScUiLayoutDesc *out);
ScStatus ScUiLayout(ScUiContext *,ScUiNode root);
ScStatus ScUiBatchBegin(ScUiContext *);
ScStatus ScUiBatchEnd(ScUiContext *);
ScStatus ScUiInvalidate(ScUiContext *,ScUiNode,const ScUiRect *local);
ScStatus ScUiEnsureVisible(ScUiContext *,ScUiNode,const ScUiRect *local);
ScStatus ScUiFocus(ScUiContext *,ScUiNode);
ScStatus ScUiFocusQuery(const ScUiContext *,ScUiNode root,ScUiNode *out);
ScStatus ScUiFocusNext(ScUiContext *,ScUiNode root,ScI32 direction);
ScStatus ScUiCapture(ScUiContext *,ScUiNode,ScU32 enabled);
ScStatus ScUiHitTest(const ScUiContext *,ScUiNode root,ScI32 x,ScI32 y,ScUiNode *out);
ScStatus ScUiListen(ScUiContext *,ScUiNode,const ScUiSubscription *,ScU32 *token);
ScStatus ScUiDisconnect(ScUiContext *,ScU32 token);
ScStatus ScUiCommand(ScUiContext *,ScUiNode source,ScU32 command);
ScStatus ScUiTimerSet(ScUiContext *,const ScUiTimerDesc *,ScU32 *token);
ScStatus ScUiTimerChange(ScUiContext *,ScU32 token,const ScUiTimerDesc *);
ScStatus ScUiTimerCancel(ScUiContext *,ScU32 token);
ScStatus ScUiTimerQuery(ScUiContext *,ScU32 token,ScUiTimerDesc *out);
ScStatus ScUiMetricsQuery(ScUiContext *,ScUiNode root,ScUiMetrics *out);
ScStatus ScUiMeasureText(ScUiContext *,ScU32 font_role,const ScUiText *,ScI32 width,ScU32 flags,ScUiTextMetrics *out);
ScStatus ScUiClassRegister(ScUiContext *,const ScUiClassDesc *,ScUiClass *out);
ScStatus ScUiClassUnregister(ScUiContext *,ScUiClass);
ScStatus ScUiPainterClip(ScUiPainter *,const ScUiRect *);
ScStatus ScUiPainterRestore(ScUiPainter *);
ScStatus ScUiPainterFill(ScUiPainter *,const ScUiRect *,ScU32 color_role);
ScStatus ScUiPainterLine(ScUiPainter *,ScI32 x1,ScI32 y1,ScI32 x2,ScI32 y2,ScU32 color_role);
ScStatus ScUiPainterText(ScUiPainter *,const ScUiRect *,const ScUiText *,ScU32 font_role,ScU32 flags);
ScStatus ScUiPainterIcon(ScUiPainter *,const ScUiRect *,ScUiIcon *,ScU32 state);
ScStatus ScUiIconCreateArgb(ScU32 width,ScU32 height,ScU32 stride,const ScU32 *pixels,ScUiIcon **out);
ScStatus ScUiIconLoad(ScUiContext *,const char *path,ScUiIcon **out);
ScStatus ScUiIconRetain(ScUiIcon *);
void ScUiIconRelease(ScUiIcon *);
ScStatus ScUiIconQuery(const ScUiIcon *,ScUiIconInfo *out);


typedef struct {
    ScU32 size,version,operation,revision,parent,item,index,column,field,flags,reserved[2];
} ScUiDataRequest;
typedef struct {
    ScU32 size,version,revision,total,item,parent,state;ScUiIcon *icon;ScValue value;
} ScUiDataReply;
typedef ScStatus (*ScUiDataProc)(void *context,const ScUiDataRequest *,ScUiDataReply *);
typedef struct {
    ScU32 size,version,flags;ScUiDataProc procedure;void *context;
    ScDropProc context_drop;ScU32 reserved[2];
} ScUiProviderDesc;
typedef struct {
    ScU32 size,version,revision,kind,parent,first,count,item,field,flags,reserved[2];
} ScUiDataChange;
typedef struct {
    ScU32 size,version,column,flags;ScUiText title;
    ScI32 width,min_width,max_width;ScU32 field,format,sort_kind,reserved[2];
} ScUiColumnDesc;
typedef struct {
    ScU32 size,version,count,current,anchor,revision,flags,reserved;
} ScUiSelectionInfo;
typedef struct {
    ScU32 size,version;ScWideCounter x,y,content_width,content_height;
    ScU32 viewport_width,viewport_height,line_step_x,line_step_y,flags,reserved;
} ScUiScrollInfo;
typedef struct {
    ScU32 size,version,part,flags;ScI32 width;ScUiText text;
    ScUiIcon *icon;ScU32 reserved[2];
} ScUiStatusPart;

ScStatus ScUiProviderCreate(const ScUiProviderDesc *,ScUiProvider **out);
ScStatus ScUiProviderFromModel(ScModel *,ScUiProvider **out);
ScStatus ScUiProviderRetain(ScUiProvider *);
void ScUiProviderRelease(ScUiProvider *);
ScStatus ScUiProviderNotify(ScUiProvider *,const ScUiDataChange *);
ScStatus ScUiViewBind(ScUiContext *,ScUiNode,ScUiProvider *,ScUiSelection *);
ScStatus ScUiViewUnbind(ScUiContext *,ScUiNode);
ScStatus ScUiViewRefresh(ScUiContext *,ScUiNode,ScU32 revision);
ScStatus ScUiSelectionCreate(ScUiProvider *,ScU32 flags,ScUiSelection **out);
ScStatus ScUiSelectionRetain(ScUiSelection *);
void ScUiSelectionRelease(ScUiSelection *);
ScStatus ScUiSelectionQuery(const ScUiSelection *,ScUiSelectionInfo *out);
ScStatus ScUiSelectionSet(ScUiSelection *,ScU32 item,ScU32 action);
ScStatus ScUiSelectionRange(ScUiSelection *,ScU32 first,ScU32 last,ScU32 action);
ScStatus ScUiSelectionClear(ScUiSelection *);
ScStatus ScUiSelectionCopy(const ScUiSelection *,ScArray **item_ids);
ScStatus ScUiTableSetColumns(ScUiContext *,ScUiNode,const ScUiColumnDesc *,ScU32 count);
ScStatus ScUiTableSetColumnWidth(ScUiContext *,ScUiNode,ScU32 column,ScI32 width);
ScStatus ScUiTableSort(ScUiContext *,ScUiNode,ScU32 column,ScU32 descending);
ScStatus ScUiTableBeginEdit(ScUiContext *,ScUiNode,ScU32 item,ScU32 column);
ScStatus ScUiTableEndEdit(ScUiContext *,ScUiNode,ScU32 commit);
ScStatus ScUiTreeExpand(ScUiContext *,ScUiNode,ScU32 item,ScU32 expanded);
ScStatus ScUiViewEnsureItem(ScUiContext *,ScUiNode,ScU32 item);
ScStatus ScUiViewHitTest(ScUiContext *,ScUiNode,ScI32 x,ScI32 y,ScU32 *item,ScU32 *column);
ScStatus ScUiScrollQuery(ScUiContext *,ScUiNode,ScUiScrollInfo *out);
ScStatus ScUiScrollTo(ScUiContext *,ScUiNode,const ScWideCounter *x,const ScWideCounter *y);
ScStatus ScUiSplitterSet(ScUiContext *,ScUiNode,ScI32 position);
ScStatus ScUiDockMove(ScUiContext *,ScUiNode panel,ScUiNode host,ScU32 dock);
ScStatus ScUiDockFloat(ScUiContext *,ScUiNode panel,const ScUiWindowDesc *,ScUiNode *root);
ScStatus ScUiLayoutSave(ScUiContext *,ScUiNode root,ScConfig *);
ScStatus ScUiLayoutRestore(ScUiContext *,ScUiNode root,const ScConfig *);
ScStatus ScUiButtonSetDefault(ScUiContext *,ScUiNode root,ScUiNode button);
ScStatus ScUiLabelAssociate(ScUiContext *,ScUiNode label,ScUiNode target);
ScStatus ScUiCheckSet(ScUiContext *,ScUiNode,ScU32 checked);
ScStatus ScUiRadioSelect(ScUiContext *,ScUiNode group,ScUiNode radio);
ScStatus ScUiSpinRange(ScUiContext *,ScUiNode,ScI32 minimum,ScI32 maximum,ScI32 step);
ScStatus ScUiTabAdd(ScUiContext *,ScUiNode tabs,ScUiNode page,const ScUiText *title);
ScStatus ScUiTabSelect(ScUiContext *,ScUiNode tabs,ScUiNode page);
ScStatus ScUiComboOpen(ScUiContext *,ScUiNode,ScU32 open);
ScStatus ScUiProgressSet(ScUiContext *,ScUiNode,ScU32 value,ScU32 total,ScU32 mode);
ScStatus ScUiStatusSetParts(ScUiContext *,ScUiNode,const ScUiStatusPart *,ScU32 count);
ScStatus ScUiTooltipSet(ScUiContext *,ScUiNode,const ScUiText *);


typedef struct {
    ScU32 size,version,item,command,flags;ScUiText label;
    ScUiIcon *icon;ScUiMenu *submenu;ScUiShortcut shortcut;ScUiText help;
    void *context;ScDropProc context_drop;ScU32 reserved[3];
} ScUiMenuItem;
typedef struct {
    ScU32 size,version,command,flags;ScUiText label;ScUiShortcut shortcut;
    ScUiIcon *icon;ScU32 reserved[3];
} ScUiCommandDesc;
ScStatus ScUiMenuCreate(ScUiMenu **out);
ScStatus ScUiMenuRetain(ScUiMenu *);
void ScUiMenuRelease(ScUiMenu *);
ScStatus ScUiMenuQuery(const ScUiMenu *,ScContainerInfo *out);
ScStatus ScUiMenuGet(const ScUiMenu *,ScU32 item,ScUiMenuItem *out);
ScStatus ScUiMenuReplace(ScUiMenu *,const ScUiMenuItem *,ScU32 count);
ScStatus ScUiMenuInsert(ScUiMenu *,ScU32 before,const ScUiMenuItem *,ScU32 *item);
ScStatus ScUiMenuRemove(ScUiMenu *,ScU32 item);
ScStatus ScUiMenuSetState(ScUiMenu *,ScU32 item,ScU32 mask,ScU32 state);
ScStatus ScUiMenuSetText(ScUiMenu *,ScU32 item,const ScUiText *);
ScStatus ScUiMenuBind(ScUiContext *,ScUiNode menubar,ScUiMenu *);
ScStatus ScUiMenuOpen(ScUiContext *,ScUiNode root,ScUiMenu *,ScI32 x,ScI32 y,ScU32 flags,ScU32 *popup);
ScStatus ScUiMenuClose(ScUiContext *,ScU32 popup);
ScStatus ScUiToolbarBind(ScUiContext *,ScUiNode toolbar,ScUiMenu *);
ScStatus ScUiCommandSet(ScUiContext *,const ScUiCommandDesc *);
ScStatus ScUiCommandQuery(ScUiContext *,ScU32 command,ScUiCommandDesc *out);
ScStatus ScUiCommandRemove(ScUiContext *,ScU32 command);


typedef struct {
    ScU32 size,version,flags,undo_budget_bytes;ScUiText text;ScU32 reserved[4];
} ScUiDocumentDesc;
typedef struct {
    ScU32 size,version,bytes,lines,revision,saved_revision,undo_bytes,redo_bytes;
    ScU32 flags,can_undo,can_redo,reserved;
} ScUiDocumentInfo;
typedef struct {
    ScU32 size,version,anchor,caret,affinity,revision,flags,reserved;
} ScUiTextSelection;
typedef struct {
    ScU32 size,version,start,end,foreground_role,background_role,flags,revision;
} ScUiStyleSpan;
typedef struct {
    ScU32 size,version,flags,start,end;ScUiText needle;ScU32 reserved[3];
} ScUiFindDesc;
ScStatus ScUiDocumentCreate(const ScUiDocumentDesc *,ScUiDocument **out);
ScStatus ScUiDocumentRetain(ScUiDocument *);
void ScUiDocumentRelease(ScUiDocument *);
ScStatus ScUiDocumentQuery(const ScUiDocument *,ScUiDocumentInfo *out);
ScStatus ScUiDocumentCopy(const ScUiDocument *,ScU32 start,ScU32 bytes,ScString **out);
ScStatus ScUiDocumentReplace(ScUiDocument *,ScU32 start,ScU32 removed,const ScUiText *,ScU32 flags);
ScStatus ScUiDocumentUndo(ScUiDocument *);
ScStatus ScUiDocumentRedo(ScUiDocument *);
ScStatus ScUiDocumentBeginUndo(ScUiDocument *);
ScStatus ScUiDocumentEndUndo(ScUiDocument *);
ScStatus ScUiDocumentMarkSaved(ScUiDocument *);
ScStatus ScUiDocumentFind(const ScUiDocument *,const ScUiFindDesc *,ScUiTextSelection *out);
ScStatus ScUiDocumentBoundary(ScUiDocument *,ScU32 byte_offset,ScU32 kind,ScI32 direction,ScU32 *out);
ScStatus ScUiDocumentReplaceAll(ScUiDocument *,const ScUiFindDesc *,const ScUiText *,ScU32 *changed);
ScStatus ScUiEditBind(ScUiContext *,ScUiNode,ScUiDocument *);
ScStatus ScUiEditGetSelection(ScUiContext *,ScUiNode,ScUiTextSelection *out);
ScStatus ScUiEditSetSelection(ScUiContext *,ScUiNode,const ScUiTextSelection *);
ScStatus ScUiEditHitTest(ScUiContext *,ScUiNode,ScI32 x,ScI32 y,ScU32 *byte_offset,ScU32 *affinity);
ScStatus ScUiEditSetSpans(ScUiContext *,ScUiNode,const ScUiStyleSpan *,ScU32 count);
ScStatus ScUiEditCopy(ScUiContext *,ScUiNode);
ScStatus ScUiEditCut(ScUiContext *,ScUiNode);
ScStatus ScUiEditPaste(ScUiContext *,ScUiNode);
ScStatus ScUiEditSelectAll(ScUiContext *,ScUiNode);


typedef struct {
    ScU32 size,version,owner,flags;ScUiText title,message;
    const ScUiMenuItem *buttons;ScU32 button_count,default_command,cancel_command;
    ScUiEventProc completion;void *context;ScDropProc context_drop;ScU32 reserved;
} ScUiMessageBoxDesc;
typedef struct {
    ScU32 size,version,owner,flags;ScUiText title;
    const char *directory;const ScUiMenuItem *filters;ScU32 filter_count,selected_filter;
    ScUiEventProc completion;void *context;ScDropProc context_drop;ScU32 reserved;
} ScUiFileDialogDesc;
typedef struct {
    ScU32 size,version,owner,flags;ScUiText title;ScConfig *initial;
    ScUiEventProc validate,apply,completion;void *context;ScDropProc context_drop;ScU32 reserved[2];
} ScUiPropertySheetDesc;
typedef struct {
    ScU32 size,version,flags,field,kind,page;ScUiText label;
    const char *section;const char *key;ScI32 minimum,maximum;
    ScU32 reserved[2];
} ScUiFieldDesc;
typedef struct {
    ScU32 size,version,stage,root,command,selected_count,flags;ScI32 error;
} ScUiDialogInfo;
typedef struct {
    ScU32 size,version,node,parent,role,actions,state,flags;ScUiRect rectangle;
    ScU32 name_bytes,value_bytes,revision,reserved;
} ScUiAccessibleInfo;
ScStatus ScUiMessageBox(ScUiContext *,const ScUiMessageBoxDesc *,ScUiDialog **out);
ScStatus ScUiFileDialog(ScUiContext *,const ScUiFileDialogDesc *,ScUiDialog **out);
ScStatus ScUiPropertySheet(ScUiContext *,const ScUiPropertySheetDesc *,const ScUiFieldDesc *,ScU32 count,ScUiDialog **out);
ScStatus ScUiDialogRetain(ScUiDialog *);
void ScUiDialogRelease(ScUiDialog *);
ScStatus ScUiDialogQuery(const ScUiDialog *,ScUiDialogInfo *out);
ScStatus ScUiDialogClose(ScUiDialog *,ScU32 command);
ScStatus ScUiDialogSelectedPath(ScUiDialog *,ScU32 index,ScString **out);
ScStatus ScUiDialogCopyValues(ScUiDialog *,ScConfig **out);
ScStatus ScUiAccessibleQuery(ScUiContext *,ScUiNode,ScUiAccessibleInfo *out);
ScStatus ScUiAccessibleText(ScUiContext *,ScUiNode,ScU32 part,ScString **out);
ScStatus ScUiAccessibleSetName(ScUiContext *,ScUiNode,const ScUiText *);
ScStatus ScUiAccessibleAction(ScUiContext *,ScUiNode,ScU32 action,const ScValue *);


typedef struct {
    ScU32 size,version,actions,flags;const ScU32 *formats;ScU32 format_count;
    ScUiEventProc procedure;void *context;ScDropProc context_drop;ScU32 reserved[3];
} ScUiDropTargetDesc;
typedef struct {
    ScU32 size,version,source,actions;const void *offer;ScU32 offer_bytes;
    ScUiIcon *preview;ScU32 flags,reserved[4];
} ScUiDragDesc;
/* chosen_action只在ENTER/OVER/DROP协议回调内为in/out；真实身份是内核副本。 */
typedef struct {
    ScU32 size,version,phase,format,actions,chosen_action,bytes;const void *data;
    ScBrokerIdentity sender;
} ScUiDragData;
ScStatus ScUiDropTargetRegister(ScUiContext *,ScUiNode,const ScUiDropTargetDesc *);
ScStatus ScUiDropTargetRemove(ScUiContext *,ScUiNode);
ScStatus ScUiDragBegin(ScUiContext *,const ScUiDragDesc *,ScU32 *token);
ScStatus ScUiDragCancel(ScUiContext *,ScU32 token);

#endif
