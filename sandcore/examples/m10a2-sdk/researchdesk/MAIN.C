#include "RDAPP.H"
RdApp rd;
int main(void)
{
    ScStatus status=RdUiCreate();
    if(status<0){
        fprintf(stderr,"ResearchDesk startup: %d\n",status);
        if(rd.ui)ScUiContextRelease(rd.ui);
        return 1;
    }
    static const char sample[]="# 标签<TAB>32位整数\n实验 A\t12\n实验 B\t-4\n对照组\t0\n实验 C\t32\n";
    RdDoc *data=0;
    status=RdDocumentCreate("",sample,sizeof(sample)-1,0,&data);
    if(status<0)RdError(status,"无法创建示例文档");
    else {
        static const char note[]="ResearchDesk\n\n数据与笔记使用同一个公开 Document/Edit。\n选择数据文档后执行“读取数据并统计”。\n修改源后旧统计失效；源行激活只定位同一修订。\n";
        RdDocumentCreate("",note,sizeof(note)-1,0,0);
    }
    if(data){
        rd.active=data;
        ScUiTabSelect(rd.ui,rd.tabs,data->page);
        RdParseStart();
    }
    char args[1024];
    ScU32 bytes=0;
    if(!ScProcessArguments(args,sizeof(args),&bytes) && bytes){
        status=RdIoStart(RD_IO_OPEN,args,0);
        if(status<0)RdError(status,"打开参数文件失败");
    }
    while(!rd.stopped){
        ScUiContextStep(rd.ui,0);
        int more=RdIoStep();
        more|=RdModelStep();
        if(!rd.io.kind && !rd.dialog && rd.dialog_action){
            ScU32 action=rd.dialog_action;
            RdDoc *doc=RdDocument(rd.dialog_doc);
            rd.dialog_action=rd.dialog_doc=0;
            if(action==RD_CLOSE && doc){
                rd.active=doc;
                RdRequest(RD_CLOSE);
            }
            else if(action==RD_EXIT)RdRequest(RD_EXIT);
        }
        if(rd.quitting && !rd.io.kind && !rd.dialog){
            ScUiNodeDestroy(rd.ui,rd.root);
            rd.stopped=1;
            break;
        }
        ScU32 timeout=more?1u:SC32_INFINITE;
        ScUiContextWaitTimeout(rd.ui,timeout,&timeout);
        ScU32 events=0;
        ScSystemWait(SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM|SC32_EVENT_JOB,timeout,&events);
    }
    RdModelDispose();
    for(RdDoc *doc=rd.docs;doc;){
        RdDoc *next=doc->next;
        ScUiDocumentRelease(doc->document);
        free(doc);
        doc=next;
    }
    ScUiSelectionRelease(rd.tree_selection);
    ScUiSelectionRelease(rd.table_selection);
    ScUiProviderRelease(rd.tree_provider);
    ScUiProviderRelease(rd.table_provider);
    for(ScU32 i=0;i<6;i++)ScUiMenuRelease(rd.menus[i]);
    ScUiContextRelease(rd.ui);
    return 0;
}
