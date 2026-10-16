#ifndef SANDCORE_NOTES2_PRIVATE_H
#define SANDCORE_NOTES2_PRIVATE_H
#include <SCUI.H>
#include <SC32LEASE.H>
#include <SC32PATH.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    N_NEW=0x8100,N_OPEN,N_SAVE,N_SAVE_AS,N_RELOAD,N_EXIT,N_UNDO,N_REDO,
    N_CUT,N_COPY,N_PASTE,N_SELECT_ALL,N_FIND,N_FIND_NEXT,N_FIND_PREVIOUS,
    N_REPLACE,N_REPLACE_ALL,N_FIND_CLOSE,N_CANCEL_IO,N_ABOUT,
    N_CONFIRM_SAVE=0x8200,N_CONFIRM_DISCARD,N_CONFIRM_CANCEL,
    N_ACTION_NONE=0,N_ACTION_NEW=1,N_ACTION_OPEN=2,N_ACTION_RELOAD=3,N_ACTION_EXIT=4,
    N_DIALOG_NONE=0,N_DIALOG_OPEN=1,N_DIALOG_SAVE=2,N_DIALOG_CONFIRM=3,
    N_IO_LOAD=1,N_IO_SAVE=2,N_IO_WRITE=1,N_IO_FLUSH=2,N_IO_PUBLISH=3,
    N_IO_ADVANCE=4,N_IO_LOAD_READ=5,N_IO_FINISH=6,N_IO_CLEANUP=7,
    N_IO_BYTES=16384
};
typedef struct {ScLeaseInfo info;ScFile file;ScU32 valid,closing;} NotesLease;
typedef struct {
    ScU32 kind,phase,offset,total,revision,original_revision,cancel,settled,sequence,seed,candidate_trials;
    ScStatus status;ScErrorInfo error;
    char path[68],candidate[68];
    NotesLease source,target;
    ScBuffer *input;ScString *snapshot;
    ScUiDocument *candidate_document;
    unsigned char chunk[N_IO_BYTES];
} NotesIo;
typedef struct {
    ScUiContext *ui;ScUiNode root,menu,toolbar,status,edit,search,needle,replacement,caseless;
    ScUiDocument *document;
    ScUiMenu *menus[5];
    ScUiDialog *dialog,*notice;
    ScU32 dialog_kind,pending_action,quit,loaded_revision,io_serial;
    char path[68],message[160],title[160];
    ScU32 command_flags[N_ABOUT-N_NEW+1];
    NotesIo *io;
} Notes;
static Notes notes;

static void n_record(void *,ScU32);
static ScUiText n_text(const char *);
static void n_status(const char *);
static void n_error(ScStatus,const char *);
static ScStatus n_document_empty(void);
static ScStatus n_file_dialog(ScU32);
static ScStatus n_transition(ScU32);
static ScStatus n_request(ScU32);
static ScStatus n_save(const char *);
static ScStatus n_load(const char *,ScU32);
static void n_io_step(void);
static void n_io_release(void);
static ScStatus n_build(void);
static ScStatus n_event(ScUiContext *,const ScUiEvent *,void *);
#endif
