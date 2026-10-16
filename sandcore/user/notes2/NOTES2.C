#include "NOTES2.H"
/* 聚合仅组织本应用私有职责；系统库仍从真正独立的SCL动态装载。 */
#include "notes_ui.c"
#include "notes_io.c"
#include "notes_actions.c"

int main(void)
{
    ScStatus status=n_build();
    if(status>=0){
        char argument[1024];ScU32 bytes=0;
        status=ScProcessArguments(argument,sizeof(argument),&bytes);
        if(status>=0)status=n_load(bytes?argument:"/HOME/notes.txt",bytes?0:1);
        if(status<0){n_error(status,bytes?argument:"/HOME/notes.txt");status=0;}
    }
    while(status>=0 && !notes.quit){
        status=ScUiContextStep(notes.ui,notes.io?1:SC32_INFINITE);
        if(status<0 || status==SC_UI_END)break;
        if(notes.io){ScU32 before=notes.io->offset;n_io_step();
            if(notes.io && notes.io->offset!=before)n_status(0);}
    }
    n_io_release();
    ScUiDialogRelease(notes.dialog);ScUiDialogRelease(notes.notice);
    ScUiContextRelease(notes.ui);ScUiDocumentRelease(notes.document);
    for(ScU32 i=0;i<5;i++)ScUiMenuRelease(notes.menus[i]);
    if(status<0){fprintf(stderr,"Notes 2 error=%d\n",status);return 1;}return 0;
}
