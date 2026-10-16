#include "PALETTE2.H"
#include "tools_ui.inc"
#include "palette_view.inc"
int main(void)
{
    ScStatus s=pal_create();
    ScI32 result=0;
    if(s==0)s=ScUiContextRun(tool.ui,&result);
    if(s<0)fprintf(stderr,"Palette2 error=%d\n",s);
    tool_close();
    ScUiSelectionRelease(pal.selection);
    ScUiProviderRelease(pal.provider);
    for(ScU32 i=0;i<280;i++)ScUiIconRelease(pal.icons[i]);
    return s<0?-s:result;
}
