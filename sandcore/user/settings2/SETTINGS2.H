#ifndef SANDCORE_SETTINGS2_PRIVATE_H
#define SANDCORE_SETTINGS2_PRIVATE_H
#include "APP.H"
#include <SC32IMAGE.H>
enum {
    S2_PREVIEW=0x5200,S2_KEEP,S2_REVERT,S2_RESOLUTION=0x5210,S2_SCALE=0x5220,
    S2_AURORA=0x5230,S2_CLASSIC,S2_THEME_BROWSE,S2_THEME_APPLY,S2_THEME_RELOAD,
    S2_WALL_DUNES=0x5240,S2_WALL_NIGHT,S2_WALL_SAND,S2_WALL_IMAGE,S2_WALL_CLEAR,
    S2_PROGRAM_ADD=0x5250,S2_PROGRAM_EDIT,S2_PROGRAM_REMOVE,S2_PROGRAM_SAVE,S2_PROGRAM_RELOAD,S2_SHORTCUT,
    S2_USER_SAVE=0x5260,S2_USER_RELOAD,S2_LOGIN,
    S2_ENV_ADD=0x5270,S2_ENV_EDIT,S2_ENV_REMOVE,S2_ENV_SAVE,S2_ENV_RELOAD,
    S2_OPEN=0x5280,S2_SAVE,S2_SAVE_AS,S2_RELOAD,S2_NEW,S2_UNDO,S2_REDO,S2_CUT,S2_COPY,S2_PASTE,S2_ALL,S2_FIND,S2_REPLACE,
    S2_OPEN_USER=0x5290,S2_OPEN_ENV,S2_OPEN_MENU,S2_OPEN_THEME,S2_OPEN_DISPLAY,S2_OPEN_WALL,
    S2_CANCEL=0x52a0,S2_CLOSE,S2_ABOUT,S2_DISCARD_OPEN,S2_DISCARD_EXIT,
    S2_BROWSE_DOCUMENT=1,S2_BROWSE_SAVE=2,S2_BROWSE_THEME=3,S2_BROWSE_IMAGE=4,S2_BROWSE_SHORTCUT=5,
    S2_SAVE_DOCUMENT=1,S2_SAVE_USER=2,S2_SAVE_ENV=3,S2_SAVE_THEME=4,
    S2_VALIDATION_NONE=99,S2_VALIDATION_USER=100,S2_VALIDATION_ENV=101
};
typedef struct {char key[32],value[256];} EnvironmentRow;
typedef struct {
    ScShellApplication *original;ScString *id,*label,*executable,*arguments,*icon,*category;
    ScU32 layer,flags,profile,dirty,removed;
} ProgramRow;
typedef struct {
    IoWrite write;IoPin temporary;ScString *body;ScU32 phase,kind,validator,expected,document_revision,cleanup,cancel;
    ScStatus outcome;ScU32 durable,active,uncertain;char target[65],temporary_path[65];
} SettingsSave;
typedef struct {
    ScUiNode tabs,pages[6],display_note,user_name,user_home,environment,programs,editor,document_path,theme_path,wall_note;
    ScUiProvider *env_provider,*program_provider;ScUiSelection *env_selection,*program_selection;
    ScUiDocument *document;ScU32 document_revision,doc_saved_revision,user_revision,env_revision,user_dirty,env_dirty,user_loaded,env_loaded;
    char paths[6][65],path[65],pending_path[65],theme[65];
    EnvironmentRow env[16];ScU32 env_count,env_model_revision;
    ProgramRow *rows;ScU32 row_count,row_capacity,program_model_revision,program_cursor,program_loading;
    ScShell *shell;ScShellRequest *request;ScShellCatalog *catalog;ScU32 catalog_epoch,request_kind,save_cursor;
    ScU32 width,height,scale,preview,owns_preview,timer,browse_kind,prompt_kind,loading,closing;
    ScUiDialog *dialog;IoRead reader;SettingsSave save;
    ScImageRequest image;IoPin image_pin;char image_path[65];ScU32 image_wait,image_more;
    ScUiMenu *context_menu;
} Settings;
static Settings settings;
static ScStatus settings_command(ScU32 command);
static ScStatus settings_event(ScUiContext *,const ScUiEvent *,void *);
static ScStatus settings_create(void);
static ScStatus settings_step(void);
static void settings_close(void);
static ScStatus settings_save_start(ScU32 kind,const char *path,ScString *body,ScU32 expected,ScU32 document_revision);
static ScStatus settings_browse(ScU32 kind);
static ScStatus settings_document_open(const char *path,ScU32 discard);
static ScStatus settings_load_user(void);
static ScStatus settings_load_env(void);
static void settings_model_changed(ScUiProvider *provider,ScU32 *revision);
static ScStatus program_request_refresh(void);
static ScStatus program_step(void);
static ScStatus program_command(ScU32 command);
static ScStatus settings_wallpaper(const char *path);
static ScStatus settings_image_step(void);
#endif
