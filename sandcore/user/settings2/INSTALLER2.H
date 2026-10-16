#ifndef SANDCORE_INSTALLER2_PRIVATE_H
#define SANDCORE_INSTALLER2_PRIVATE_H
#include "APP.H"
enum {
    I2_ADD=0x5300,I2_DIRECTORY,I2_TARGET,I2_REMOVE,I2_PREFLIGHT,I2_START,I2_CANCEL,I2_RETRY,I2_DETAILS,I2_COPY,I2_REPORT,I2_CLOSE,I2_ABOUT,
    I2_QUEUED=0,I2_CHECKED=1,I2_COPYING=2,I2_CONTENT=3,I2_DONE=4,I2_REUSED=5,I2_FAILED=6,I2_CANCELLED=7,I2_UNCERTAIN=8,
    I2_BROWSE_SOURCE=1,I2_BROWSE_TARGET=2,I2_BROWSE_REPORT=3
};
typedef struct {
    char source[65],destination[65],detail[160];ScU32 kind,length,source_revision,result_revision,state,durable;
    ScStatus error;
} InstallItem;
typedef struct {
    ScUiNode target,mode,uid,gid,parents,repermission,table,progress;
    ScUiProvider *provider;ScUiSelection *selection;ScHash *identities;
    InstallItem *items;ScU32 count,capacity,revision,phase,cursor,checking,run_after_check,cancel,browse,file_count;
    ScU32 final_mode,create_parents,change_existing,child_directory,parent_cursor,reporting;
    ScI32 final_uid,final_gid;char destination[65],parent_path[65];
    IoWrite writer,report;ScUiDialog *dialog;
} Installer;
static Installer installer;
static ScStatus installer_command(ScU32 command);
static ScStatus installer_step(void);
static ScStatus installer_create(void);
static void installer_close(void);
#endif
