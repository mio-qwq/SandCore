#ifndef SANDCORE_DEBUG2_APP_H
#define SANDCORE_DEBUG2_APP_H
#include "SCUI.H"
#include "SC32DEBUG.H"
#include "D2DECODE.H"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

enum {D2_CODE,D2_REGISTERS,D2_MEMORY,D2_STACK,D2_BREAKPOINTS,D2_MODULES,D2_SYMBOLS,D2_PANES};
enum {D2_LAUNCH=100,D2_BROWSE,D2_RESTART,D2_STOP,D2_QUIT,D2_CONTINUE=200,D2_PAUSE,D2_STEP,
    D2_GOTO_CODE=300,D2_GOTO_MEMORY,D2_BREAK_ADD,D2_BREAK_REMOVE,D2_FOLLOW_IP,D2_PREVIOUS,D2_NEXT,D2_FOLLOW_BRANCH,
    D2_FLOAT=400,D2_DOCK,D2_RESET_LAYOUT,D2_SAVE_LAYOUT,D2_LOAD_LAYOUT,
    D2_ACTIVATE_CODE=500,D2_ACTIVATE_STACK,D2_ACTIVATE_MODULE,D2_ACTIVATE_SYMBOL,D2_ABOUT,D2_MODULE_DETAILS};
typedef struct {
    ScU32 id,address,aux,generation,flags;
    char text[5][160];
} D2Row;
typedef struct {ScU32 address,module,line,object;char path[128];} D2SourceLine;
typedef struct D2App D2App;
typedef struct {
    D2App *app;ScU32 kind,revision,table,container,listener;
    ScUiProvider *provider;ScUiSelection *selection;
} D2Pane;
typedef struct {
    ScDebugSnapshot snapshot;
    ScArray *rows[D2_PANES],*modules,*segments,*lines;
    ScU32 stage,cursor,epoch,module_index,segment_cursor,symbol_cursor;
    ScU32 code_address,memory_address,break_cursor,sidecar_module,sidecar_stage,cached_modules;
    ScFile sidecar_file;unsigned char *sidecar_bytes;ScU32 sidecar_size,sidecar_read,sidecar_cursor;
    char sidecar_status[160];
} D2Frame;
struct D2App {
    ScUiContext *ui;ScUiNode root,menubar,toolbar,commandbox,addressbox,status,workspace,top,left,right,bottom,dock,tabs,float_root;
    D2Pane panes[D2_PANES];ScUiMenu *menu,*debug_menu,*view_menu,*file_menu,*tools;
    ScUiDialog *dialog;ScConfig *saved_layout;
    ScDebugLaunchInfo launch;ScDebugSnapshot current;
    D2Frame *frame,*candidate;
    ScU32 timer,listener,active,closing,visible,revision,code_address,memory_address,follow_ip,closing_ticks,restart_pending,timer_period,timer_root,dialog_kind;
    char command[1088],message[240],layout_path[65];
};
static void D2Record(void *p,ScU32 size){memset(p,0,size);((ScU32 *)p)[0]=size;((ScU32 *)p)[1]=1;}
static ScUiText D2Text(const char *s){ScUiText t={sizeof(t),1,s,(ScU32)strlen(s)};return t;}
static ScU32 D2Count(ScArray *a){ScContainerInfo i;D2Record(&i,sizeof(i));return a && ScArrayQuery(a,&i)==0?i.length:0;}
static const void *D2Data(ScArray *a){ScView v;D2Record(&v,sizeof(v));return a && ScArrayView(a,&v)==0?v.data:0;}
static ScStatus D2Array(ScU32 stride,ScArray **out){ScArrayDesc d;D2Record(&d,sizeof(d));d.item_bytes=stride;return ScArrayCreate(&d,out);}
static ScStatus D2Append(ScArray *a,const void *r){return ScArrayInsert(a,D2Count(a),r,1);}
ScStatus D2RowAppend(ScArray *,D2Row *);
void D2Error(D2App *,const char *,ScStatus);
void D2Status(D2App *,const char *);
void D2FrameDestroy(D2Frame *);
ScStatus D2CaptureStart(D2App *);
ScStatus D2CaptureStep(D2App *);
ScStatus D2BuildUi(D2App *);
void D2Publish(D2App *,D2Frame *);
void D2Clear(D2App *);
ScStatus D2Launch(D2App *,const char *);
ScStatus D2Control(D2App *,ScU32);
void D2Poll(D2App *);
void D2Schedule(D2App *);
void D2Close(D2App *);
ScStatus D2Navigate(D2App *,ScU32);
ScStatus D2SidecarStep(D2App *,D2Frame *);
int D2AddressInModule(const D2Frame *,const ScDebugModule *,ScU32,ScU32,ScU32);
void D2Annotate(D2Frame *);
#endif
