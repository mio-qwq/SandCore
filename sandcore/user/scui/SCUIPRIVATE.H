#ifndef SANDCORE_SCUIPRIVATE_H
#define SANDCORE_SCUIPRIVATE_H
#include "../SCUI.H"
#include "../SC32FONT.H"
#include "../SC32BROKER.H"
#include "../SC32POPUP.H"
#include "../SC32DROP.H"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>

/* 聚合TU的共同合同；所有私有函数只供库正文，应用没有控件地址或桥。 */
enum {
    UI_CONTEXT=1,UI_MENU=2,UI_PROVIDER=3,UI_SELECTION=4,UI_DOCUMENT=5,
    UI_DIALOG=6,UI_ICON=7,UI_KIND_POPUP=26,
    UI_NODE_RETIRED=1,UI_NODE_DESTROYING=2,UI_NODE_LAYOUT_DIRTY=4,
    UI_ROOT_CREATING=8,UI_ROOT_LAYOUT_DEFERRED=16,
    UI_NODE_CONTEXT_COMMITTED=32,UI_NODE_CREATE_DELIVERED=64,UI_NODE_CLEARED=128,UI_NODE_DESTROY_SENT=256,UI_NODE_VIEWPORT_CHILD=512,
    UI_OP_CREATE=1,UI_OP_NODE=2,UI_OP_LAYOUT=3,UI_OP_EVENT=4,UI_OP_PAINT=5,
    UI_OP_FONT=6,UI_OP_MENU=7,UI_OP_PROVIDER=8,UI_OP_TIMER=9,UI_OP_DATA=10,
    UI_LAYOUT_CLIP=1,UI_TEXT_WRAP=1,UI_TEXT_ELLIPSIS=2,UI_TEXT_CENTER=4,
    UI_TEXT_RIGHT=8,UI_TEXT_UNDERLINE=16,UI_TEXT_STRIKE=32,
    UI_MENU_EVENT=1,UI_VIEW_EVENT=2,UI_CONTROL_EVENT=3
};
typedef struct UiObject UiObject;
typedef struct UiNode UiNode;
typedef struct UiRoot UiRoot;
typedef struct UiView UiView;
typedef struct UiListener UiListener;
typedef struct UiTimer UiTimer;
typedef struct UiClassDef UiClassDef;
typedef struct UiMenuEntry UiMenuEntry;
typedef struct UiPopup UiPopup;
typedef struct UiBinding UiBinding;
typedef struct UiColumn UiColumn;
typedef struct UiVisibleItem UiVisibleItem;
typedef struct {ScValue value;ScString *text;ScBuffer *bytes;} UiValue;
struct UiObject {
    ScU32 kind,references,depth,closing,disposed;
    UiObject *previous,*next;
    void (*dispose)(UiObject *,int quiet);
};
struct ScUiIcon {
    UiObject object;ScU32 width,height,format,flags,revision,bytes;ScU32 *pixels;
};
struct UiClassDef {
    ScU32 id,live_nodes,depth,retired;ScString *name;ScUiClassDesc description;
};
struct UiListener {
    UiListener *previous,*next;ScU32 token,node,retired,depth;
    ScUiSubscription description;
};
struct UiTimer {
    ScU32 token,index,paused,firing,retired,revision;ScUiTimerDesc description;
    UiNode *owner;UiTimer *node_previous,*node_next;
};
struct UiRoot {
    ScWindow window;ScWindowInfo information;ScU32 owner,focus,capture,hover,default_button;
    ScU32 *pixels;ScU32 bytes,width,height,geometry,visible,notified_visible,paint_pending,paint_failures,cursor;
    ScWideCounter retry;ScArray *damage;ScUiNode id;ScU32 popup,popup_flags;
};
struct UiVisibleItem {ScU32 id,parent,index,depth,state;};
struct UiColumn {ScUiColumnDesc description;ScString *title;ScI32 x,width;ScWideCounter position;};
struct UiView {
    ScUiProvider *provider;ScUiSelection *selection;UiBinding *binding;
    ScArray *columns,*column_order,*visible;ScWideCounter columns_width;ScHash *expanded,*cells;ScU32 revision,total,tree,depth,retired,sort_pending,cell_stamp;
    ScU32 hot_item,hot_column,resize_column,sort_column,descending,editor_item,editor_column,editor_type;
    ScString *search;ScU32 search_tick,cell_limit,cell_count;
    ScI32 resize_origin,resize_width;ScUiNode editor;
};
struct UiNode {
    UiNode *all_previous,*all_next,*parent,*first_child,*last_child,*previous,*next;
    UiNode *retired_next,*retire_parent,*destroy_next;ScU32 retire_children;
    UiNode *root_previous,*root_next;
    ScUiContext *owner;ScU32 id,generation,kind,state,flags,effective,private_flags,depth,menu_key;
    ScU32 root,command,value_revision,layout_revision,font_role;
    ScUiRect rectangle,clip,child_clip,viewport_rectangle;ScUiSize preferred;ScUiLayoutDesc layout;
    ScString *text,*accessible_name,*tooltip;UiValue value;ScUiIcon *icon;ScUiMenu *menu;ScU32 menu_hot,menu_pressed;
    void *context;ScDropProc context_drop;UiClassDef *custom;
    UiRoot *window;UiView *view;void *control;UiTimer *timers;
    ScWideCounter scroll_x,scroll_y,content_width,content_height;
    ScU32 scroll_step_x,scroll_step_y;ScUiNode label_target;
};
struct UiBinding {
    UiBinding *previous,*next;ScUiProvider *provider;ScUiContext *context;ScUiNode node;
};
struct ScUiProvider {
    UiObject object;ScUiProviderDesc description;ScModel *model;ScU32 model_subscription,revision;
    UiBinding *bindings;ScArray *order;ScU32 querying,flags;
};
struct ScUiSelection {
    UiObject object;ScUiProvider *provider;ScHash *items;ScArray *universe;
    ScU32 flags,current,anchor,revision,all_revision,all_count;
};
struct UiMenuEntry {
    ScUiMenuItem description;ScString *label,*help,*caption;ScU32 id,retired,depth,mnemonic;
};
struct ScUiMenu {
    UiObject object;ScArray *items;ScU32 revision,next_id,walk_depth;
};
struct UiPopup {
    UiPopup *previous,*next;ScUiContext *context;ScUiMenu *menu;ScUiNode owner,node,source;ScU32 source_item;
    ScU32 token,hot,parent_popup,child_popup,retired,first_row,range_first,hover_timer;ScI32 caption_width,shortcut_width,row_height;
};
struct ScUiContext {
    UiObject object;ScUiContextDesc description;ScUiErrorInfo error;
    ScHash *nodes,*classes,*commands,*timer_index;ScArray *timers;
    UiNode *all_nodes,*retired,*roots;UiListener *listeners;UiPopup *popups;const ScUiEvent *current_event;
    ScResourceCache *glyphs;ScThemeInfo theme;ScDisplayInfo display;ScFontInfo fonts[2];
    ScU32 next_node,next_class,next_token,revision,batch,walking,dispatching,stopped,quiet,hz,layout_depth,command_query_depth,listener_collecting;
    ScI32 exit_code,notification_error;
    ScU32 live_nodes,live_roots,layout_pending;
    ScWideCounter received_messages,paint_count,submitted_bytes;
};
struct ScUiPainter {
    ScUiContext *context;UiNode *node;UiRoot *root;ScUiRect clip;
    ScI32 origin_x,origin_y;ScArray *clips;ScStatus error;ScU32 text_role;
};
typedef struct {ScU32 node,revision;ScUiRect rectangle,clip,child_clip;ScUiSize preferred;} UiPlacement;
typedef struct {ScU32 face,pixels,scalar,generation;} UiGlyphKey;
typedef struct {ScU32 parent,first,count,depth,start;} UiTreeSegment;
typedef struct {ScU32 item,field,revision,format;} UiCellKey;
typedef struct {ScString *text;ScU32 stamp;} UiCell;

static int ui_stopping,ui_quiet,ui_finalizing;
static UiObject *ui_objects;
static ScStatus ui_input(const void *,ScU32);
static ScStatus ui_output(void *,ScU32);
static ScStatus ui_object_new(ScU32,ScU32,void (*)(UiObject *,int),void **);
static ScStatus ui_object_retain(UiObject *,ScU32);
static void ui_object_release(UiObject *);
static ScStatus ui_object_enter(UiObject *,ScU32);
static void ui_object_leave(UiObject *);
static ScStatus ui_fail(ScUiContext *,ScStatus,ScStatus,ScU32,ScU32,ScU32);
static ScStatus ui_cause(ScUiContext *,ScStatus,ScU32,ScU32);
static ScStatus ui_pin(const void *);
static int ui_add(ScU32,ScU32,ScU32 *);
static int ui_multiply(ScU32,ScU32,ScU32 *);
static int ui_signed_add(ScI32,ScI32,ScI32 *);
static ScU32 ui_fraction(ScU32,ScU32,ScU32);
static void ui_wide_add(ScWideCounter *,ScU32);
static int ui_wide_compare(const ScWideCounter *,const ScWideCounter *);
static ScU32 ui_wide_difference(const ScWideCounter *,const ScWideCounter *);
static ScStatus ui_now(ScWideCounter *);
static ScStatus ui_text_copy(const ScUiText *,ScString **);
static void ui_text_view(ScString *,ScUiText *);
static ScStatus ui_value_copy(const ScValue *,UiValue *);
static void ui_value_clear(UiValue *);
static UiNode *ui_node(ScUiContext *,ScUiNode);
static UiNode *ui_root(ScUiContext *,ScUiNode);
static ScStatus ui_node_new(ScUiContext *,ScU32,UiNode *,UiNode **);
static void ui_node_hold(UiNode *);
static void ui_node_leave(UiNode *);
static void ui_node_retire(UiNode *);
static void ui_nodes_collect(ScUiContext *);
static ScStatus ui_node_text(UiNode *,const ScUiText *);
static void ui_node_damage(UiNode *);
static void ui_node_layout_dirty(UiNode *);
static ScStatus ui_tree_effective(UiNode *);
static void ui_input_subtree(UiNode *);
static ScStatus ui_emit(ScUiContext *,UiNode *,ScUiEvent *);
static void ui_listeners_remove_node(ScUiContext *,ScU32);
static ScStatus ui_event(UiNode *,ScU32,const void *,ScU32);
static void ui_event_record(ScUiEvent *,UiNode *,ScU32);
static ScStatus ui_native_proc(ScWindow,const ScMessage *,void *);
static ScStatus ui_timer_pump(ScUiContext *);
static ScU32 ui_timer_wait(ScUiContext *,ScU32);
static void ui_timers_remove_node(UiNode *);
static void ui_timers_visibility(ScUiContext *,ScU32,int);
static ScStatus ui_layout_root(UiNode *);
static ScI32 ui_rect_right(const ScUiRect *);
static ScI32 ui_rect_bottom(const ScUiRect *);
static void ui_rect_intersection(const ScUiRect *,const ScUiRect *,ScUiRect *);
static int ui_rect_contains(const ScUiRect *,ScI32,ScI32);
static int ui_rect_nonempty(const ScUiRect *);
static ScStatus ui_damage(UiNode *,const ScUiRect *);
static ScStatus ui_paint_root(UiNode *);
static ScStatus ui_root_surface(UiNode *);
static ScStatus ui_theme_refresh(ScUiContext *);
static ScU32 ui_color(ScUiContext *,ScU32);
static ScI32 ui_scale(ScUiContext *,ScI32);
static ScStatus ui_glyph(ScUiContext *,ScU32,ScU32,const ScFontGlyph **,ScU32 *);
static void ui_glyph_release(ScUiContext *,ScU32);
static ScStatus ui_text_measure(ScUiContext *,ScU32,const ScUiText *,ScI32,ScU32,ScUiTextMetrics *);
static void ui_fill(ScUiPainter *,const ScUiRect *,ScU32);
static void ui_border(ScUiPainter *,const ScUiRect *,int);
static ScStatus ui_paint_text(ScUiPainter *,const ScUiRect *,const ScUiText *,ScU32,ScU32);
static ScStatus ui_menu_paint(UiNode *,ScUiPainter *);
static ScStatus ui_menu_event(UiNode *,const ScUiEvent *);
static ScStatus ui_menu_measure(UiNode *,ScUiSize *);
static ScStatus ui_menu_key(UiNode *,ScU32,ScU32);
static int ui_menu_pointer(UiNode *,ScU32,ScI32,ScI32,UiNode **);
static ScStatus ui_popup_root(UiNode *,ScU32,const ScUiRect *,ScU32,ScU32,ScU32,UiNode **);
static void ui_popup_node_destroy(UiNode *);
static void ui_menu_close_owner(ScUiContext *,ScU32);
static UiNode *ui_popup_target(UiNode *,ScI32 *,ScI32 *,int keyboard);
static ScStatus ui_shortcut(ScUiContext *,ScU32,ScU32,ScU32 *);
static ScStatus ui_command_state(ScUiContext *,ScU32,ScUiCommandDesc *);
static ScStatus ui_view_paint(UiNode *,ScUiPainter *);
static ScStatus ui_view_event(UiNode *,const ScUiEvent *);
static void ui_view_destroy(UiNode *);
static ScStatus ui_view_prepare(UiNode *);
static void ui_view_changed(UiNode *,const ScUiDataChange *);
static ScStatus ui_provider_call(ScUiProvider *,const ScUiDataRequest *,ScUiDataReply *);
static ScStatus ui_provider_count(ScUiProvider *,ScU32,ScU32 *,ScU32 *);
static ScStatus ui_provider_item(ScUiProvider *,ScU32,ScU32,ScU32 *,ScU32 *,ScU32 *);
static ScStatus ui_provider_cell(ScUiProvider *,ScU32,ScU32,ScValue *);
static void ui_provider_detach(UiBinding *);
static void ui_provider_changed(ScUiProvider *,const ScUiDataChange *);
static int ui_selection_has(ScUiSelection *,ScU32);
static void ui_selection_prune(ScUiSelection *);
static ScStatus ui_selection_ids(ScUiSelection *,const ScU32 *,ScU32,ScU32);

/* 四职责源由控件代理实现；core只持有control不解释其内部布局。 */
static ScStatus ui_controls_init(UiNode *);
static void ui_controls_destroy(UiNode *,int quiet);
static ScStatus ui_controls_measure(UiNode *,const ScUiSize *,ScUiSize *);
static ScStatus ui_controls_paint(UiNode *,ScUiPainter *);
static ScStatus ui_controls_event(UiNode *,const ScUiEvent *);
static void ui_controls_layout(UiNode *);
static ScStatus ui_controls_set_text(UiNode *,const ScUiText *);
static ScStatus ui_controls_copy_text(UiNode *,ScString **);
static ScStatus ui_controls_set_value(UiNode *,const ScValue *);
static int ui_controls_contains(UiNode *,ScI32,ScI32);
static ScStatus ui_controls_popup_event(UiNode *,const ScUiEvent *);
static void ui_dialog_node_destroy(UiNode *,int quiet);
static ScStatus ui_drag_pump(ScUiContext *);
static ScStatus ui_drag_event(UiNode *,const ScUiEvent *);
static void ui_drag_node_destroy(UiNode *,int quiet);
static void ui_drag_finish(void);
static ScU32 ui_drag_wait(ScUiContext *,ScU32);
static void ui_drag_state(UiNode *);
static ScStatus ui_unicode_prepare(void);
static void ui_unicode_finish(void);
static ScU32 ui_unicode_flags(ScU32);
static ScStatus ui_grapheme_next(const void *,ScU32,ScU32,ScU32 *);
static ScStatus ui_grapheme_previous(const void *,ScU32,ScU32,ScU32 *);
static ScStatus ui_word_next(const void *,ScU32,ScU32,ScU32 *);
static ScStatus ui_word_previous(const void *,ScU32,ScU32,ScU32 *);

#endif
