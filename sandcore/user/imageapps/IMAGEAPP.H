#ifndef SANDCORE_IMAGEAPP_H
#define SANDCORE_IMAGEAPP_H
#include <SCUI.H>
#include <SCUICANVAS.H>
#include <SC32PATH.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
enum {I_NEW=100,I_OPEN,I_SAVE,I_SAVE_AS,I_EXPORT,I_CLOSE,I_UNDO,I_REDO,I_CUT,I_COPY,I_PASTE,I_DELETE,I_ALL,I_CLEAR,
    I_FIT,I_NATIVE,I_ZOOM_IN,I_ZOOM_OUT,I_CENTER,I_CW,I_CCW,I_FLIP_X,I_FLIP_Y,I_BRUSH_PROPERTIES,I_IMAGE_PROPERTIES,
    I_TOOL_FIRST=200,I_COLOR_FIRST=220,I_RADIUS_DOWN=240,I_RADIUS_UP,I_CANCEL,I_FLOAT,I_DOCK,I_ABOUT,
    I_DISCARD=270,I_SAVE_CHANGES,I_KEEP};
enum {I_DIALOG_OPEN=1,I_DIALOG_SAVE=2,I_DIALOG_EXPORT=3,I_DIALOG_DISCARD=4,I_DIALOG_INFO=5,I_DIALOG_BRUSH=6,
    I_INTENT_NONE=0,I_INTENT_REPLACE=1,I_INTENT_CLOSE=2};
typedef struct ImageCommand ImageCommand;
struct ImageCommand {ImageCommand *next;
    ScU32 command;
};
typedef struct {
    ScUiContext *ui;
    ScUiNode root,menubar,toolbar,body,tools,tools_split,view_parent,right_split,dock,float_root,status,properties,history;
    ScGfxCanvas *view;
    ScGfxSurface *surface,*candidate;
    ScGfxOperation *operation;
    ScUiMenu *menu,*file_menu,*edit_menu,*view_menu,*image_menu,*help_menu,*tool_menu,*drawing_menu;
    ScUiProvider *history_provider;
    ScUiSelection *history_selection;
    ScUiDialog *dialog;
    ScU32 dialog_kind,dialog_done,dialog_command,intent,after_save,closing,closed,compact,cut_pending,operation_kind,operation_complete,release_wait;
    ScU32 native_path,file_revision,candidate_native,candidate_revision,provider_revision,source_format,last_history_revision,last_undo,last_redo;
    ScGfxSurface *history_surface;
    ScU32 palette[256],colors[8],color_index;
    ScGfxBrush brush;
    ScGfxRect cut_rect;
    char path[65],candidate_path[65],message[256],history_text[80];
    ScGfxOperationInfo completed;
    ImageCommand *commands,*last_command;
    ScStatus failure;
} ImageApp;
static void image_record(void *p,ScU32 n){memset(p,0,n);
    ((ScU32 *)p)[0]=n;
    ((ScU32 *)p)[1]=1;
}
static ScUiText image_text(const char *s){ScUiText t={sizeof(t),1,s,(ScU32)strlen(s)};
    return t;
}
static void image_status(ImageApp *,const char *);
static void image_error(ImageApp *,const char *,ScStatus);
static ScStatus image_execute(ImageApp *,ScU32);
static ScStatus image_file_dialog(ImageApp *,ScU32);
static ScStatus image_properties(ImageApp *,ScU32);
static void image_refresh(ImageApp *);
static void image_consider_intent(ImageApp *);
static ScStatus image_event(ScUiContext *,const ScUiEvent *,void *);
#endif
