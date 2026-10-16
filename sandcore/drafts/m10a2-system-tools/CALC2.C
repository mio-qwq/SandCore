#include "CALC2.H"
#include "tools_ui.inc"
#include "calc_engine.inc"
#include "calc_view.inc"
int main(void)
{
    ScStatus s=calc_create();
    ScI32 exit_code=0;
    if(s==0)s=ScUiContextRun(tool.ui,&exit_code);
    if(s<0)fprintf(stderr,"Calculator2 error=%d\n",s);
    tool_close();
    ScUiDocumentRelease(calc.document);
    ScUiSelectionRelease(calc.selection);
    ScUiProviderRelease(calc.provider);
    ScArrayRelease(calc.history);
    return s<0?-s:exit_code;
}
