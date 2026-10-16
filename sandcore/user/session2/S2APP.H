#ifndef SANDCORE_S2APP_H
#define SANDCORE_S2APP_H
#include <SCUI.H>
#include <SCUISECRET.H>
#include <SC32AUTH.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
enum {S_SIGNIN=100,S_REQUEST_SIGNIN,S_CANCEL,S_CLEAR,S_CLOSE,S_REFRESH,S_SWITCH,S_LOGOUT,S_LOCK,S_PROPERTIES,S_ABOUT,S_LOGIN_PAGE,S_SESSIONS_PAGE};
enum {S_DIALOG_INFO=1,S_DIALOG_LOGOUT=2};
typedef struct S2Command S2Command;
struct S2Command {S2Command *next;
    ScU32 id;
};
typedef struct {
    ScUiContext *ui;
    ScUiNode root,tabs,login_page,sessions_page,username,password,signin,cancel,progress,status,identity,table,details,splitter;
    ScUiMenu *menu,*account_menu,*session_menu,*help_menu;
    ScUiProvider *provider;
    ScUiSelection *selection;
    ScUiDialog *dialog;
    ScIdentityInfo self;
    ScAuthTicket ticket;
    ScAuthLoginInfo auth;
    ScWideCounter auth_deadline;
    ScU32 locked,closed,visible,auth_poll,auth_activate,auth_timer,dialog_kind,dialog_done,dialog_command,revision,compact;
    ScU32 auth_ui_mode,auth_ui_done,auth_ui_total,auth_ui_flags;
    ScArray *rows,*candidate;
    ScU32 count,candidate_count,cursor,refreshing,selection_restore;
    ScSessionInfo control;
    ScU32 control_action,control_pending;
    S2Command *commands,*last_command;
    char message[240],cell[160];
    ScStatus failure;
} S2App;
static void s2_record(void *p,ScU32 n){memset(p,0,n);
    ((ScU32 *)p)[0]=n;
    ((ScU32 *)p)[1]=1;
}
static ScUiText s2_text(const char *s){ScUiText t={sizeof(t),1,s,(ScU32)strlen(s)};
    return t;
}
static const ScSessionInfo *s2_rows(const ScArray *);
static const ScSessionInfo *s2_find(const ScArray *,ScU32 count,ScU32 id);
static void s2_status(S2App *,const char *);
static void s2_error(S2App *,const char *,ScStatus);
static ScStatus s2_refresh(S2App *);
static void s2_refresh_step(S2App *);
static ScStatus s2_execute(S2App *,ScU32);
static ScStatus s2_event(ScUiContext *,const ScUiEvent *,void *);
static ScStatus s2_submit(ScUiContext *,ScUiNode,const char *,ScU32,void *);
static void s2_auth_poll(S2App *);
static void s2_auth_cancel(S2App *);
static void s2_update_auth_ui(S2App *);
static void s2_properties_update(S2App *);
#endif
