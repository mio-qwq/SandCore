#include "STUDIO2.H"
#include "studio_io.c"
#include "studio_project.c"
#include "studio_syntax.c"
#include "studio_build.c"
#include "studio_ui.c"
#include "studio_actions.c"
int main(void)
{
    ScStatus s=project_defaults();
    st.project_dirty=0;
    if(s>=0)s=ui_build();
    if(s<0){
        fprintf(stderr,"Studio2 startup: %d\n",s);
        return 1;
    }
    char args[1024];
    U n=0;
    s=ScProcessArguments(args,sizeof(args),&n);
    if(s>=0 && n){
        while(n && (args[n-1]==' '||args[n-1]=='\t'))args[--n]=0;
        const char *ext=strrchr(args,'.');
        io_open(args,ext && (!strcmp(ext,".SCP")||!strcmp(ext,".scp"))?S_IO_PROJECT_OPEN:S_IO_OPEN,0);
    }
    while(!st.stopping){
        io_step();
        build_step();
        child_step(&st.running,0);
        ui_tick();
        if(st.quit_requested && !st.io && !st.build.phase && !st.running.capture.job && !st.running.artifact.live && !st.dialog)perform(S_ACTION_EXIT);
        if(st.active && st.active->syntax_revision!=st.active->revision)syntax(st.active);
        ScU32 wait=(!st.io&&!st.build.phase&&!st.running.capture.job&&!st.running.artifact.live)?SC32_INFINITE:1u;
        ScUiContextStep(st.ui,wait);
    }
    /* 正常退出只在IO/child/发布终态领取之后到达，不靠fini无界等待。 */
    for(StDoc *d=st.documents;d;){
        StDoc *next=d->next;
        syntax_free(d);
        ScUiDocumentRelease(d->text);
        free(d);
        d=next;
    }
    ScUiDocumentRelease(st.output_document);
    ScUiSelectionRelease(st.tree_selection);
    ScUiSelectionRelease(st.diag_selection);
    ScUiProviderRelease(st.tree_provider);
    ScUiProviderRelease(st.diag_provider);
    for(U i=0;i<7;i++)ScUiMenuRelease(st.menus[i]);
    ScConfigRelease(st.config);
    free(st.members);
    free(st.errors);
    free(st.build.inputs);
    free(st.build.dependencies);
    ScUiContextRelease(st.ui);
    return 0;
}
