#ifndef RESEARCHDESK_APP_H
#define RESEARCHDESK_APP_H
#include "SCUI.H"
#include "SCUITABS.H"
#include "SC32PATH.H"
#include "SC32LEASE.H"
#include "SC32REPLACE.H"
#include "RDSTATS.H"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
enum {
    RD_NEW=1000,RD_OPEN,RD_SAVE,RD_SAVE_AS,RD_CLOSE,RD_EXIT,RD_UNDO,RD_REDO,RD_CUT,RD_COPY,RD_PASTE,RD_ALL,
    RD_PARSE,RD_PLUGIN,RD_RELEASE,RD_CANCEL,RD_FIND,RD_FIND_NEXT,RD_FONT_SMALL,RD_FONT_NORMAL,RD_ABOUT,RD_DISCARD=1200,RD_KEEP,RD_DO_SAVE
};
enum {
    RD_IO_OPEN=1,RD_IO_SAVE=2,RD_IO_CLEANUP=99,RD_MAX_BYTES=4194304
};
typedef struct RdDoc {
    struct RdDoc *next;
    ScU32 id,page,edit,revision,disk_revision;
    ScUiDocument *document;
    char path[68],label[96];
}
RdDoc;
typedef struct {
    ScU32 id,line,offset;
    ScI32 value;
    ScString *label;
}
RdRow;
typedef struct {
    ScU32 phase,doc,revision,at,line,bad_line,next_id,count,capacity;
    ScString *source;
    ScView view;
    RdRow *rows;
    ScStatus result;
}
RdParse;
typedef struct {
    ScU32 live;
    ScLeaseInfo value;
}
RdPin;
typedef struct {
    ScU32 kind,phase,doc,revision,offset,cancel,committed;
    ScStatus status;
    char path[68],candidate[68];
    ScString *snapshot;
    ScView view;
    ScBuffer *bytes;
    RdPin source,target;
}
RdIo;
typedef struct {
    ScUiContext *ui;
    ScUiNode root,menu,toolbar,tree,tabs,table,status,chart,find_box;
    ScUiNode left_split,body_split,content,right;
    ScUiMenu *menus[6];
    ScUiDialog *dialog;
    ScUiProvider *tree_provider,*table_provider;
    ScUiSelection *tree_selection,*table_selection;
    RdDoc *docs,*active;
    ScU32 next_doc,next_row,data_revision,source_doc,source_revision;
    RdRow *rows;
    ScU32 count,sort_field,sort_reverse,*order,*positions,tree_revision,table_revision;
    RdParse parse;
    RdIo io;
    ScLibrary plugin;
    RdStatsBeginProc begin;
    RdStatsPushProc push;
    RdStatsFinishProc finish;
    RdStatsState stats;
    RdStatsResult result;
    ScU32 computing,stat_at,stat_revision,stats_valid;
    ScU32 quitting,stopped,dialog_action,dialog_doc,dialog_kind,chart_class;
    char message[256];
}
RdApp;
extern RdApp rd;
void RdRecord(void *,ScU32);
ScUiText RdText(const char *);
RdDoc *RdDocument(ScU32);
ScStatus RdDocumentCreate(const char *,const void *,ScU32,ScU32,RdDoc **);
ScStatus RdDocumentClose(RdDoc *);
void RdChanged(RdDoc *);
ScStatus RdParseStart(void);
int RdModelStep(void);
void RdParseCancel(void);
ScStatus RdPluginLoad(void);
ScStatus RdPluginRelease(void);
void RdModelDispose(void);
ScStatus RdUiCreate(void);
void RdStatus(const char *);
void RdError(ScStatus,const char *);
void RdRefresh(void);
ScStatus RdNotify(ScUiProvider *,ScU32);
ScStatus RdTreeData(void *,const ScUiDataRequest *,ScUiDataReply *);
ScStatus RdTableData(void *,const ScUiDataRequest *,ScUiDataReply *);
ScStatus RdIoStart(ScU32,const char *,RdDoc *);
int RdIoStep(void);
void RdIoCancel(void);
void RdRequest(ScU32);
ScStatus RdAction(ScUiContext *,const ScUiEvent *,void *);
ScStatus RdFind(void);
#endif
