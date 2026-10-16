#ifndef SANDCORE_TERMINAL2_H
#define SANDCORE_TERMINAL2_H
#include "SCUITERM.H"
#include "SC32TERMINAL.H"
#include "SCUITABS.H"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
enum {
    T_NEW=1000,T_CLOSE,T_EXIT,T_COPY,T_PASTE,T_ALL,T_CLEAR,T_HISTORY,T_RESET,T_DENSE,T_CODE,T_EOF,T_INTERRUPT,T_ABOUT,T_CONFIRM=1100,T_CANCEL
};
typedef struct TerminalSession {
    struct TerminalSession *next;
    ScU32 id,page,node,closing,cancel_sent,read_eof,finished,columns,rows;
    ScUiTerminal *terminal;
    ScJob job;
    ScFile master;
    ScI32 exit_code;
    unsigned char pending[4096];
    ScU32 pending_at,pending_bytes;
    char label[192];
}
TerminalSession;
typedef struct {
    ScUiContext *ui;
    ScUiNode root,menu,toolbar,tabs,status;
    ScUiMenu *menus[5];
    ScUiDialog *dialog;
    TerminalSession *sessions,*active,*cursor;
    ScU32 next_id,session_count,quitting,stopped,pending_action,pending_session;
    char command[1088],message[256];
}
TerminalApplication;
static TerminalApplication terminal_app;
static void a_record(void *p,ScU32 n){
    memset(p,0,n);
    ((ScU32 *)p)[0]=n;
    ((ScU32 *)p)[1]=1;
}
static ScUiText a_text(const char *s){
    ScUiText t={
        sizeof(t),1,s,(ScU32)strlen(s)
    };
    return t;
}
static void a_status(const char *);
static void a_error(ScStatus,const char *);
static ScStatus a_session_new(void);
static void a_session_close(TerminalSession *);
static void a_request(ScU32);
static ScStatus a_event(ScUiContext *,const ScUiEvent *,void *);
#endif
