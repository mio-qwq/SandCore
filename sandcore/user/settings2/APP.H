#ifndef SANDCORE_SETUP_APP_PRIVATE_H
#define SANDCORE_SETUP_APP_PRIVATE_H
#include <SC32API.H>
#include <SC32PATH.H>
#include <SC32LEASE.H>
#include <SC32WALL.H>
#include <SCUI.H>
#include <SCSHELL.H>
#include <SCLIB.H>
#include "SC32CONFIG.H"
#include "SC32REPLACE.H"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define INIT(x) do {memset(&(x),0,sizeof(x));(x).size=sizeof(x);(x).version=1;} while(0)
typedef ScStatus (*AppCommand)(ScU32);
typedef ScStatus (*AppEvent)(ScUiContext *,const ScUiEvent *,void *);
typedef struct {ScUiNode node;ScU32 minimum_width;} AppBand;
typedef struct {
    ScUiContext *ui;ScUiNode root,menu,body,status;
    ScUiMenu *menus;AppCommand command;AppEvent event;
    ScU32 quit,visible,relayout;char message[320];AppBand *bands;ScU32 band_count,band_capacity;
} App;
typedef struct {const char *label;ScU32 command,key,modifiers;} AppMenuEntry;
typedef struct {ScFileLease lease;ScU32 generation,sequence;ScFile file;ScDirectory directory;} IoPin;
typedef struct {
    IoPin source,target,candidate;
    ScString *memory;const char *bytes;
    char destination[65],temporary[65];
    ScU32 length,offset,phase,mode,is_directory,hold,cancel,uncertain,durable,expected_target,buffer_used,buffer_offset;
    ScStatus error;ScLeaseInfo observation;unsigned char buffer[16384];
} IoWrite;
typedef struct {
    IoPin pin;ScString *text;ScU32 phase,limit,length,offset,revision;
    ScStatus error;char path[65];
} IoRead;
static App app;
static ScUiText text(const char *z);
static void status(const char *z);
static ScStatus error(ScStatus code,const char *operation);
static ScStatus layout(ScUiNode node,ScU32 mode,ScI32 width,ScI32 height,ScU32 flex);
static ScStatus widget(ScUiNode parent,ScU32 kind,const char *label,ScU32 command,ScI32 width,ScI32 height,ScU32 flex,ScUiNode *out);
static ScStatus listen(ScUiNode node,ScU32 phases,AppEvent procedure,void *context);
static ScStatus menu_part(const char *title,const AppMenuEntry *entries,ScU32 count);
static ScStatus app_create(const char *title,AppCommand command,AppEvent event);
static void app_close(void);
static ScStatus node_text(ScUiNode node,char *out,ScU32 capacity);
static void set_text(ScUiNode node,const char *z);
static ScStatus message(const char *title,const char *body);
static ScStatus normalize(const char *path,const char *base,char out[65]);
static ScU32 path_equal(const char *a,const char *b);
static ScStatus pin_open(const char *path,ScU32 kind,ScU32 lease,ScU32 revision,ScU32 metadata,IoPin *out);
static ScStatus pin_release(IoPin *pin,ScU32 cancel);
static ScStatus pin_query(IoPin *pin,ScLeaseInfo *info);
static ScStatus io_start_memory(IoWrite *job,const char *target,ScString *body,ScU32 expected,ScU32 mode,ScU32 hold);
static ScStatus io_start_copy(IoWrite *job,const char *target,const char *source,ScU32 revision,ScU32 mode);
static ScStatus io_start_directory(IoWrite *job,const char *target,ScU32 mode);
static ScStatus io_step(IoWrite *job);
static void io_dispose(IoWrite *job);
static ScStatus read_start(IoRead *job,const char *path,ScU32 limit);
static ScStatus read_step(IoRead *job);
static void read_dispose(IoRead *job);
static ScStatus read_small(const char *path,ScU32 limit,ScString **out,ScU32 *revision);
static ScStatus config_string(ScConfig *cfg,const char *key,const char *value);
static ScStatus config_get(ScConfig *cfg,const char *key,char *out,ScU32 capacity);
static ScStatus property(const char *title,ScConfig *initial,const ScUiFieldDesc *fields,ScU32 count,AppEvent apply,void *context);
static ScStatus responsive_band(ScUiNode parent,ScU32 minimum_width,ScUiNode *out);
static ScStatus responsive_update(void);
static ScStatus app_step(void);
static ScUiFieldDesc field(ScU32 id,const char *label,const char *key,ScU32 kind,ScI32 minimum,ScI32 maximum);
#endif
