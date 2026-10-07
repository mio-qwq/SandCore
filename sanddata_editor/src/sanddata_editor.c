/* 独立Windows宿主工具，界面和文件拖放均为C/Win32实现。 */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define COBJMACROS
#define _WIN32_WINNT 0x0601
#include "sanddata_editor.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <ole2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* 复用GFX.md已登记的Aurora命名角色，宿主工具不占客体DAC槽位。 */
#define PAPER RGB(251,252,254)
#define ICE RGB(230,243,249)
#define INK RGB(23,50,71)
#define MUTED RGB(95,120,140)
#define ACCENT RGB(20,127,159)
#define LINE RGB(215,230,237)
enum { CMD_OPEN=101,CMD_SAVE,CMD_SAVEAS,CMD_ADD,CMD_EXTRACT,CMD_MKDIR,CMD_DELETE,CMD_UP,CMD_RENAME,CMD_COPY };
typedef struct {char name[64];int directory,index;} View;
typedef struct {char name[64];HTREEITEM item;} TreeNode;
static HINSTANCE instance;static HWND window,list,tree,pathbar,status,buttons[8];
static HFONT font,title_font;static HBRUSH paper_brush,ice_brush;static SfsImage image;
static char current[64];static View *views;static uint32_t view_capacity;static int view_count,refreshing;
static TreeNode *nodes;static uint32_t node_capacity;static int node_count;static int dpi=96;
static wchar_t *input_value;static const wchar_t *input_title;static int input_ok;
static int scale(int n){return MulDiv(n,dpi,96);}
static void error_box(void){MessageBoxW(window,sfs_error,L"SandFS 镜像编辑器",MB_OK|MB_ICONERROR);}
static int same(const char *a,const char *b){for(;*a&&*b;a++,b++){int x=*a,y=*b;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}return *a==*b;}
static int selected(void){return ListView_GetSelectedCount(list);}
static int reserve_items(void **array,uint32_t *capacity,uint32_t need,size_t stride)
{
    if(need<=*capacity)return 1;uint32_t count=*capacity?*capacity:16;
    while(count<need){if(count>UINT32_MAX/2)return 0;count*=2;}
    if(count>SIZE_MAX/stride)return 0;void *grown=realloc(*array,count*stride);if(!grown)return 0;
    *array=grown;*capacity=count;return 1;
}
static void refresh_all(void);
static int save_image(int save_as);
static void import_drop(HDROP drop,int force_import);
static int prompt_unsaved(void){
    if(!image.dirty)return 1;
    int choice=MessageBoxW(window,L"镜像有未保存的修改。是否保存？",L"SandFS 镜像编辑器",MB_YESNOCANCEL|MB_ICONQUESTION);
    return choice==IDNO?1:choice==IDYES?save_image(0):0;
}
static void set_status(void){
    wchar_t text[512];uint32_t used=image.data_start;
    for(uint32_t i=0;i<image.count;i++)used+=(image.entries[i].size+511)/512;
    if(image.entries)swprintf(text,512,L"SandFS v%u   ·   %u / %u 项   ·   %.0f MiB · 可用 %.2f MiB%s    |    拖入添加 · 拖出提取 · Ctrl+S 保存",image.version,image.count,image.capacity,(double)image.sectors/2048,(double)(used<=image.sectors?image.sectors-used:0)/2048,image.dirty?L"   ·   有未保存修改":L"");
    else wcscpy(text,L"打开或拖入 sanddata.img 开始编辑；文件和文件夹均可拖入拖出。");
    SetWindowTextW(status,text);
    wchar_t caption[HOST_PATH];if(image.path)swprintf(caption,HOST_PATH,L"%ls%ls — SandFS 镜像编辑器",image.dirty?L"* ":L"",image.path);else wcscpy(caption,L"SandFS 镜像编辑器");SetWindowTextW(window,caption);
    for(int i=1;i<8;i++)EnableWindow(buttons[i],image.entries!=NULL);
}
static int icon_for(const wchar_t *name,int directory){
    SHFILEINFOW info;SHGetFileInfoW(name,directory?FILE_ATTRIBUTE_DIRECTORY:FILE_ATTRIBUTE_NORMAL,&info,sizeof(info),SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_USEFILEATTRIBUTES);return info.iIcon;
}
static int view_compare(const void *pa,const void *pb){const View *a=pa,*b=pb;if(a->directory!=b->directory)return b->directory-a->directory;return _stricmp(a->name,b->name);}
static void refresh_list(void){
    view_count=0;SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
    if(image.entries&&!reserve_items((void **)&views,&view_capacity,image.count+1,sizeof(*views))){
        SendMessageW(list,WM_SETREDRAW,TRUE,0);wcscpy(sfs_error,L"列表资源不足，镜像内容仍保留。");error_box();return;}
    if(image.entries)for(uint32_t i=0;i<image.count;i++){
        const SfsEntry *entry=image.entries+i;
        if(current[0]&&!sfs_child(entry->name,current))continue;
        const char *rel=entry->name+(current[0]?strlen(current)+1:0);if(!*rel)continue;
        const char *slash=strchr(rel,'/');size_t length=slash?(size_t)(slash-entry->name):strlen(entry->name);
        char full[64];memcpy(full,entry->name,length);full[length]=0;
        int at=-1;for(int j=0;j<view_count;j++)if(same(views[j].name,full)){at=j;break;}
        if(at<0){View *v=views+view_count++;strcpy(v->name,full);v->directory=slash!=NULL||entry->directory;v->index=slash?sfs_find(&image,full):(int)i;}
    }
    qsort(views,(size_t)view_count,sizeof(*views),view_compare);
    for(int i=0;i<view_count;i++){
        View *v=views+i;const char *leaf=strrchr(v->name,'/');leaf=leaf?leaf+1:v->name;wchar_t *name=sfs_wide(leaf);
        LVITEMW item={0};item.mask=LVIF_TEXT|LVIF_IMAGE|LVIF_PARAM;item.iItem=i;item.pszText=name;item.iImage=icon_for(name,v->directory);item.lParam=i;ListView_InsertItem(list,&item);free(name);
        wchar_t size[80]=L"",owner[80]=L"",rw[32]=L"";
        if(!v->directory&&v->index>=0){uint32_t bytes=image.entries[v->index].size;if(bytes>=1048576)swprintf(size,80,L"%.2f MiB",(double)bytes/1048576);else if(bytes>=1024)swprintf(size,80,L"%.1f KiB",(double)bytes/1024);else swprintf(size,80,L"%u B",bytes);}
        if(v->index>=0&&image.version==5){SfsEntry *e=image.entries+v->index;if(e->uid==-1)wcscpy(owner,L"SYSTEM");else swprintf(owner,80,L"UID %d",e->uid);swprintf(rw,32,L"%lc%lc %lc%lc %lc%lc",e->mode&1?L'r':L'-',e->mode&2?L'w':L'-',e->mode&4?L'r':L'-',e->mode&8?L'w':L'-',e->mode&16?L'r':L'-',e->mode&32?L'w':L'-');}
        ListView_SetItemText(list,i,1,v->directory?L"文件夹":L"文件");ListView_SetItemText(list,i,2,size);ListView_SetItemText(list,i,3,owner);ListView_SetItemText(list,i,4,rw);
    }
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,NULL,TRUE);
    wchar_t *where=sfs_wide(current);wchar_t path[160];swprintf(path,160,L"  /%ls",where?where:L"");free(where);SetWindowTextW(pathbar,path);set_status();
}
static int tree_find(const char *name){for(int i=0;i<node_count;i++)if(same(nodes[i].name,name))return i;return -1;}
static HTREEITEM tree_ensure(const char *name){
    int at=tree_find(name);if(at>=0)return nodes[at].item;
    char parent[64];strcpy(parent,name);char *slash=strrchr(parent,'/');HTREEITEM par=nodes[0].item;
    if(slash){*slash=0;par=tree_ensure(parent);if(!par)return NULL;}
    if(!reserve_items((void **)&nodes,&node_capacity,(uint32_t)node_count+1,sizeof(*nodes)))return NULL;
    const char *leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
    at=node_count;wchar_t *label=sfs_wide(leaf);if(!label)return NULL;
    TVINSERTSTRUCTW insert={0};insert.hParent=par;insert.hInsertAfter=TVI_SORT;insert.item.mask=TVIF_TEXT|TVIF_PARAM;insert.item.pszText=label;insert.item.lParam=at;
    HTREEITEM item=TreeView_InsertItem(tree,&insert);free(label);if(!item)return NULL;
    strcpy(nodes[at].name,name);nodes[at].item=item;node_count++;return item;
}
static void refresh_all(void){
    if(!reserve_items((void **)&nodes,&node_capacity,1,sizeof(*nodes))){wcscpy(sfs_error,L"目录树资源不足，镜像内容仍保留。");error_box();return;}
    refreshing=1;SendMessageW(tree,WM_SETREDRAW,FALSE,0);TreeView_DeleteAllItems(tree);node_count=1;nodes[0].name[0]=0;
    TVINSERTSTRUCTW root={0};root.hParent=TVI_ROOT;root.hInsertAfter=TVI_FIRST;root.item.mask=TVIF_TEXT|TVIF_PARAM;root.item.pszText=L"镜像根目录";root.item.lParam=0;nodes[0].item=TreeView_InsertItem(tree,&root);
    int complete=nodes[0].item!=NULL;
    if(complete&&image.entries)for(uint32_t i=0;i<image.count;i++){
        char path[64];strcpy(path,image.entries[i].name);
        if(!image.entries[i].directory){char *last=strrchr(path,'/');if(!last)continue;*last=0;}
        if(!tree_ensure(path)){complete=0;break;}
    }
    int at=tree_find(current);if(at<0){current[0]=0;at=0;}TreeView_SelectItem(tree,nodes[at].item);TreeView_EnsureVisible(tree,nodes[at].item);TreeView_Expand(tree,nodes[0].item,TVE_EXPAND);
    SendMessageW(tree,WM_SETREDRAW,TRUE,0);InvalidateRect(tree,NULL,TRUE);refreshing=0;refresh_list();
    if(!complete){wcscpy(sfs_error,L"目录树显示资源不足，部分目录暂未显示；镜像内容仍完整保留。");error_box();}
}
static void navigate(const char *name){strcpy(current,name);int at=tree_find(current);if(at>=0)TreeView_SelectItem(tree,nodes[at].item);refresh_list();}
static void go_up(void){char parent[64];strcpy(parent,current);char *slash=strrchr(parent,'/');if(slash)*slash=0;else parent[0]=0;navigate(parent);}
static void open_image(const wchar_t *path){if(!prompt_unsaved())return;if(!sfs_load(&image,path)){error_box();return;}current[0]=0;refresh_all();}
static void choose_open(void){
    wchar_t path[HOST_PATH]=L"";OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFilter=L"SandFS 镜像 (*.img)\0*.img\0所有文件\0*.*\0";o.lpstrFile=path;o.nMaxFile=HOST_PATH;o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_EXPLORER;o.lpstrTitle=L"打开 sanddata.img";if(GetOpenFileNameW(&o))open_image(path);
}
static int save_image(int save_as){
    if(!image.entries)return 0;wchar_t path[HOST_PATH];wcscpy(path,image.path);
    if(save_as){OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFilter=L"SandFS 镜像 (*.img)\0*.img\0";o.lpstrFile=path;o.nMaxFile=HOST_PATH;o.lpstrDefExt=L"img";o.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;if(!GetSaveFileNameW(&o))return 0;}
    if(!save_as&&!image.dirty)return 1;
    HCURSOR prior=SetCursor(LoadCursorW(NULL,IDC_WAIT));int ok=sfs_save(&image,path);SetCursor(prior);if(!ok){error_box();return 0;}refresh_all();SetWindowTextW(status,L"已保存。原镜像的备份位于同一目录，文件名含 .bak- 日期时间。");return 1;
}
static int join_name(char out[64],const char *dir,const wchar_t *host){
    const wchar_t *leaf=wcsrchr(host,L'\\');leaf=leaf?leaf+1:host;char *name=sfs_utf8(leaf);if(!name)return 0;
    if(strlen(dir)+strlen(name)+(dir[0]?1:0)>63){free(name);wcscpy(sfs_error,L"导入后路径超过 SandFS 上限。");return 0;}
    size_t n=strlen(dir);memcpy(out,dir,n);if(n)out[n++]='/';memcpy(out+n,name,strlen(name)+1);free(name);return 1;
}
static void import_paths(wchar_t **paths,int count){
    if(!image.entries||!count)return;SfsImage working={0};if(!sfs_clone(&working,&image)){error_box();return;}
    HCURSOR prior=SetCursor(LoadCursorW(NULL,IDC_WAIT));int ok=1;
    for(int i=0;i<count;i++){char dest[64];if(!join_name(dest,current,paths[i])||!sfs_add_path(&working,paths[i],dest)){ok=0;break;}}
    SetCursor(prior);if(!ok){sfs_free(&working);error_box();return;}sfs_free(&image);image=working;refresh_all();
}
static void import_drop(HDROP drop,int force_import){
    UINT count=DragQueryFileW(drop,0xffffffff,NULL,0);wchar_t **paths=calloc(count,sizeof(*paths));if(!paths){DragFinish(drop);return;}
    for(UINT i=0;i<count;i++){UINT n=DragQueryFileW(drop,i,NULL,0);paths[i]=calloc((size_t)n+1,sizeof(wchar_t));if(paths[i])DragQueryFileW(drop,i,paths[i],n+1);}
    DragFinish(drop);
    int complete=1;for(UINT i=0;i<count;i++)if(!paths[i])complete=0;
    if(complete&&count==1&&(!image.entries||!force_import)){const wchar_t *ext=wcsrchr(paths[0],L'.');if(ext&&!_wcsicmp(ext,L".img"))open_image(paths[0]);else if(image.entries)import_paths(paths,(int)count);}
    else if(complete&&image.entries)import_paths(paths,(int)count);
    else if(!image.entries)MessageBoxW(window,L"请先打开 sanddata.img，再拖入文件。",L"SandFS 镜像编辑器",MB_OK);
    for(UINT i=0;i<count;i++)free(paths[i]);free(paths);
}
static void choose_add(void){
    if(!image.entries)return;wchar_t *buffer=calloc(HOST_PATH,sizeof(wchar_t));if(!buffer)return;
    OPENFILENAMEW o={0};o.lStructSize=sizeof(o);o.hwndOwner=window;o.lpstrFilter=L"所有文件\0*.*\0";o.lpstrFile=buffer;o.nMaxFile=HOST_PATH;o.Flags=OFN_FILEMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_EXPLORER;o.lpstrTitle=L"添加到当前镜像目录（文件夹可直接拖入）";
    if(GetOpenFileNameW(&o)){wchar_t *next=buffer+wcslen(buffer)+1;
        if(!*next){wchar_t *one[]={buffer};import_paths(one,1);}else{
            int count=0;for(wchar_t *p=next;*p;p+=wcslen(p)+1)count++;
            wchar_t **paths=calloc((size_t)count,sizeof(*paths));int i=0;
            if(paths){for(wchar_t *p=next;*p;p+=wcslen(p)+1){size_t n=wcslen(buffer)+wcslen(p)+2;paths[i]=calloc(n,sizeof(wchar_t));if(paths[i])swprintf(paths[i],n,L"%ls\\%ls",buffer,p);i++;}
                int ok=1;for(i=0;i<count;i++)if(!paths[i])ok=0;if(ok)import_paths(paths,count);for(i=0;i<count;i++)free(paths[i]);free(paths);
            }
        }
    }free(buffer);
}
static wchar_t *choose_folder(void){
    IFileOpenDialog *dlg=NULL;IShellItem *item=NULL;PWSTR chosen=NULL;wchar_t *result=NULL;
    if(SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog,NULL,CLSCTX_INPROC_SERVER,&IID_IFileOpenDialog,(void **)&dlg))){
        IFileOpenDialog_SetOptions(dlg,FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST);IFileOpenDialog_SetTitle(dlg,L"选择导出文件夹");
        if(SUCCEEDED(IFileOpenDialog_Show(dlg,window))&&SUCCEEDED(IFileOpenDialog_GetResult(dlg,&item))){
            if(SUCCEEDED(IShellItem_GetDisplayName(item,SIGDN_FILESYSPATH,&chosen)))result=_wcsdup(chosen);CoTaskMemFree(chosen);IShellItem_Release(item);
        }IFileOpenDialog_Release(dlg);
    }return result;
}
static int extract_selected(const wchar_t *folder){
    int at=-1;while((at=ListView_GetNextItem(list,at,LVNI_SELECTED))>=0)if(!sfs_extract(&image,views[at].name,folder))return 0;return 1;
}
static void choose_extract(void){if(!selected())return;wchar_t *folder=choose_folder();if(!folder)return;if(!extract_selected(folder))error_box();else SetWindowTextW(status,L"已导出选中项目。镜像中的文件仍保留。");free(folder);}
static INT_PTR CALLBACK input_proc(HWND dlg,UINT msg,WPARAM wp,LPARAM lp){
    (void)lp;if(msg==WM_INITDIALOG){SetWindowTextW(dlg,input_title);SetDlgItemTextW(dlg,1001,input_value);SendDlgItemMessageW(dlg,1001,EM_SETLIMITTEXT,63,0);SendDlgItemMessageW(dlg,1001,EM_SETSEL,0,-1);SetFocus(GetDlgItem(dlg,1001));return FALSE;}
    if(msg==WM_COMMAND){if(LOWORD(wp)==IDOK){GetDlgItemTextW(dlg,1001,input_value,128);input_ok=1;EndDialog(dlg,IDOK);return TRUE;}if(LOWORD(wp)==IDCANCEL){EndDialog(dlg,IDCANCEL);return TRUE;}}return FALSE;
}
static void name_action(int rename){
    if(!image.entries||(!rename&&image.version<=3))return;
    int at=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(rename&&(at<0||selected()!=1))return;
    wchar_t value[128]=L"";if(rename){const char *leaf=strrchr(views[at].name,'/');wchar_t *v=sfs_wide(leaf?leaf+1:views[at].name);if(v){wcscpy(value,v);free(v);}}
    input_value=value;input_title=rename?L"重命名":L"新建文件夹";input_ok=0;DialogBoxParamW(instance,MAKEINTRESOURCEW(201),window,input_proc,0);if(!input_ok||!value[0])return;
    char *leaf=sfs_utf8(value);char dest[64];if(!leaf)return;
    if(strchr(leaf,'/')||strchr(leaf,'\\')||strlen(current)+strlen(leaf)+(current[0]?1:0)>63){free(leaf);wcscpy(sfs_error,L"请输入一个有效的文件名，不要包含路径。");error_box();return;}
    size_t prefix=strlen(current);memcpy(dest,current,prefix);if(prefix)dest[prefix++]='/';memcpy(dest+prefix,leaf,strlen(leaf)+1);free(leaf);
    SfsImage working={0};if(!sfs_clone(&working,&image)){error_box();return;}
    int ok=rename?sfs_rename(&working,views[at].name,dest):sfs_mkdir(&working,dest);
    if(!ok){sfs_free(&working);error_box();return;}sfs_free(&image);image=working;refresh_all();
}
static void delete_selected(void){
    if(!selected())return;if(MessageBoxW(window,L"从镜像中删除选中项目？文件夹的内容也会删除。\n修改在点击“保存”后写入镜像。",L"删除项目",MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
    int at=-1;while((at=ListView_GetNextItem(list,at,LVNI_SELECTED))>=0)sfs_remove(&image,views[at].name);refresh_all();
}

/* 拖出采用标准CF_HDROP，Explorer会从临时目录复制真实文件。拖放结束后不删除
 * 临时文件，因为Explorer可能异步读取；不会用移动效果删除镜像原文件。 */
typedef struct {IDataObject iface;LONG refs;HGLOBAL files;} DragData;
typedef struct {IDropSource iface;LONG refs;} DragSource;
typedef struct {IEnumFORMATETC iface;LONG refs;ULONG position;} FormatEnum;
static IEnumFORMATETCVtbl enum_vtable;
static HRESULT STDMETHODCALLTYPE enum_query(IEnumFORMATETC *self,REFIID iid,void **out){if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IEnumFORMATETC)){*out=self;IEnumFORMATETC_AddRef(self);return S_OK;}*out=NULL;return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE enum_add(IEnumFORMATETC *self){return (ULONG)InterlockedIncrement(&((FormatEnum *)self)->refs);}
static ULONG STDMETHODCALLTYPE enum_release(IEnumFORMATETC *self){LONG n=InterlockedDecrement(&((FormatEnum *)self)->refs);if(!n)free(self);return (ULONG)n;}
static HRESULT STDMETHODCALLTYPE enum_next(IEnumFORMATETC *self,ULONG count,FORMATETC *out,ULONG *fetched){FormatEnum *e=(FormatEnum *)self;if(!out||(!fetched&&count!=1))return E_POINTER;if(fetched)*fetched=0;if(!count)return S_OK;if(e->position)return S_FALSE;*out=(FORMATETC){CF_HDROP,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};e->position=1;if(fetched)*fetched=1;return count==1?S_OK:S_FALSE;}
static HRESULT STDMETHODCALLTYPE enum_skip(IEnumFORMATETC *self,ULONG count){FormatEnum *e=(FormatEnum *)self;ULONG left=1-e->position;e->position=1;return count<=left?S_OK:S_FALSE;}
static HRESULT STDMETHODCALLTYPE enum_reset(IEnumFORMATETC *self){((FormatEnum *)self)->position=0;return S_OK;}
static HRESULT STDMETHODCALLTYPE enum_clone(IEnumFORMATETC *self,IEnumFORMATETC **out){if(!out)return E_POINTER;FormatEnum *e=calloc(1,sizeof(*e));if(!e)return E_OUTOFMEMORY;e->iface.lpVtbl=&enum_vtable;e->refs=1;e->position=((FormatEnum *)self)->position;*out=&e->iface;return S_OK;}
static IEnumFORMATETCVtbl enum_vtable={enum_query,enum_add,enum_release,enum_next,enum_skip,enum_reset,enum_clone};
static HRESULT STDMETHODCALLTYPE data_query(IDataObject *self,REFIID iid,void **out){if(!out)return E_POINTER;if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IDataObject)){*out=self;IDataObject_AddRef(self);return S_OK;}*out=NULL;return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE data_add(IDataObject *self){return (ULONG)InterlockedIncrement(&((DragData *)self)->refs);}
static ULONG STDMETHODCALLTYPE data_release(IDataObject *self){DragData *d=(DragData *)self;LONG n=InterlockedDecrement(&d->refs);if(!n){GlobalFree(d->files);free(d);}return (ULONG)n;}
static HRESULT STDMETHODCALLTYPE data_can(IDataObject *self,FORMATETC *fmt){(void)self;if(!fmt)return E_POINTER;return fmt->cfFormat==CF_HDROP&&(fmt->tymed&TYMED_HGLOBAL)&&fmt->dwAspect==DVASPECT_CONTENT&&fmt->lindex==-1?S_OK:DV_E_FORMATETC;}
static HRESULT STDMETHODCALLTYPE data_get(IDataObject *self,FORMATETC *fmt,STGMEDIUM *medium){HRESULT hr=data_can(self,fmt);if(FAILED(hr))return hr;if(!medium)return E_POINTER;DragData *d=(DragData *)self;SIZE_T size=GlobalSize(d->files);HGLOBAL copy=GlobalAlloc(GMEM_MOVEABLE,size);if(!copy)return E_OUTOFMEMORY;void *a=GlobalLock(copy),*b=GlobalLock(d->files);if(!a||!b){if(a)GlobalUnlock(copy);if(b)GlobalUnlock(d->files);GlobalFree(copy);return E_OUTOFMEMORY;}memcpy(a,b,size);GlobalUnlock(copy);GlobalUnlock(d->files);medium->tymed=TYMED_HGLOBAL;medium->hGlobal=copy;medium->pUnkForRelease=NULL;return S_OK;}
static HRESULT STDMETHODCALLTYPE data_here(IDataObject *self,FORMATETC *f,STGMEDIUM *m){(void)self;(void)f;(void)m;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE data_canonical(IDataObject *self,FORMATETC *in,FORMATETC *out){(void)self;(void)in;if(out)out->ptd=NULL;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE data_set(IDataObject *self,FORMATETC *f,STGMEDIUM *m,BOOL release){(void)self;(void)f;(void)m;(void)release;return E_NOTIMPL;}
static HRESULT STDMETHODCALLTYPE data_enum(IDataObject *self,DWORD direction,IEnumFORMATETC **out){(void)self;if(direction!=DATADIR_GET)return E_NOTIMPL;if(!out)return E_POINTER;FormatEnum *e=calloc(1,sizeof(*e));if(!e)return E_OUTOFMEMORY;e->iface.lpVtbl=&enum_vtable;e->refs=1;*out=&e->iface;return S_OK;}
static HRESULT STDMETHODCALLTYPE data_advise(IDataObject *self,FORMATETC *f,DWORD flags,IAdviseSink *sink,DWORD *connection){(void)self;(void)f;(void)flags;(void)sink;(void)connection;return OLE_E_ADVISENOTSUPPORTED;}
static HRESULT STDMETHODCALLTYPE data_unadvise(IDataObject *self,DWORD connection){(void)self;(void)connection;return OLE_E_ADVISENOTSUPPORTED;}
static HRESULT STDMETHODCALLTYPE data_enumadvise(IDataObject *self,IEnumSTATDATA **out){(void)self;if(out)*out=NULL;return OLE_E_ADVISENOTSUPPORTED;}
static IDataObjectVtbl data_vtable={data_query,data_add,data_release,data_get,data_here,data_can,data_canonical,data_set,data_enum,data_advise,data_unadvise,data_enumadvise};
static HRESULT STDMETHODCALLTYPE source_query(IDropSource *self,REFIID iid,void **out){if(!out)return E_POINTER;if(IsEqualIID(iid,&IID_IUnknown)||IsEqualIID(iid,&IID_IDropSource)){*out=self;IDropSource_AddRef(self);return S_OK;}*out=NULL;return E_NOINTERFACE;}
static ULONG STDMETHODCALLTYPE source_add(IDropSource *self){return (ULONG)InterlockedIncrement(&((DragSource *)self)->refs);}
static ULONG STDMETHODCALLTYPE source_release(IDropSource *self){LONG n=InterlockedDecrement(&((DragSource *)self)->refs);if(!n)free(self);return (ULONG)n;}
static HRESULT STDMETHODCALLTYPE source_continue(IDropSource *self,BOOL escape,DWORD key){(void)self;if(escape)return DRAGDROP_S_CANCEL;if(!(key&MK_LBUTTON))return DRAGDROP_S_DROP;return S_OK;}
static HRESULT STDMETHODCALLTYPE source_feedback(IDropSource *self,DWORD effect){(void)self;(void)effect;return DRAGDROP_S_USEDEFAULTCURSORS;}
static IDropSourceVtbl source_vtable={source_query,source_add,source_release,source_continue,source_feedback};
static HGLOBAL prepare_drag(void){
    wchar_t temp[HOST_PATH],folder[HOST_PATH];DWORD n=GetTempPathW(HOST_PATH,temp);if(!n||n>HOST_PATH-100)return NULL;
    swprintf(folder,HOST_PATH,L"%lsSandFS-export-%lu-%llu",temp,GetCurrentProcessId(),(unsigned long long)GetTickCount64());
    if(!CreateDirectoryW(folder,NULL)){wcscpy(sfs_error,L"无法创建拖出暂存目录。");return NULL;}
    if(!extract_selected(folder))return NULL;
    SIZE_T chars=1;int at=-1;
    while((at=ListView_GetNextItem(list,at,LVNI_SELECTED))>=0){const char *leaf=strrchr(views[at].name,'/');wchar_t *w=sfs_wide(leaf?leaf+1:views[at].name);if(!w)return NULL;chars+=wcslen(folder)+wcslen(w)+2;free(w);}
    HGLOBAL handle=GlobalAlloc(GMEM_MOVEABLE|GMEM_ZEROINIT,sizeof(DROPFILES)+chars*sizeof(wchar_t));if(!handle)return NULL;
    DROPFILES *drop=GlobalLock(handle);if(!drop){GlobalFree(handle);return NULL;}drop->pFiles=sizeof(DROPFILES);drop->fWide=TRUE;wchar_t *next=(wchar_t *)((unsigned char *)drop+sizeof(DROPFILES));at=-1;
    while((at=ListView_GetNextItem(list,at,LVNI_SELECTED))>=0){const char *leaf=strrchr(views[at].name,'/');wchar_t *w=sfs_wide(leaf?leaf+1:views[at].name);swprintf(next,chars,L"%ls\\%ls",folder,w);SIZE_T used=wcslen(next)+1;next+=used;chars-=used;free(w);}
    GlobalUnlock(handle);return handle;
}
static void drag_selected(int clipboard){
    if(!selected())return;HGLOBAL files=prepare_drag();if(!files){error_box();return;}
    if(clipboard){if(OpenClipboard(window)){EmptyClipboard();if(!SetClipboardData(CF_HDROP,files))GlobalFree(files);CloseClipboard();}else GlobalFree(files);return;}
    DragData *data=calloc(1,sizeof(*data));DragSource *source=calloc(1,sizeof(*source));if(!data||!source){free(data);free(source);GlobalFree(files);return;}
    data->iface.lpVtbl=&data_vtable;data->refs=1;data->files=files;source->iface.lpVtbl=&source_vtable;source->refs=1;DWORD effect=0;
    DoDragDrop(&data->iface,&source->iface,DROPEFFECT_COPY,&effect);IDataObject_Release(&data->iface);IDropSource_Release(&source->iface);
}
static LRESULT CALLBACK list_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR ref){
    (void)id;(void)ref;if(msg==WM_DROPFILES){SendMessageW(window,WM_APP+1,wp,0);return 0;}return DefSubclassProc(hwnd,msg,wp,lp);
}
static void layout(void){
    RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom,margin=scale(20),left=scale(240),top=scale(132),bottom=scale(36);
    for(int i=0;i<8;i++)MoveWindow(buttons[i],margin+i*scale(106),scale(68),scale(98),scale(32),TRUE);
    MoveWindow(pathbar,margin,scale(108),w-margin*2,scale(22),TRUE);
    MoveWindow(tree,margin,top,left-scale(16),h-top-bottom-scale(12),TRUE);
    MoveWindow(list,margin+left,top,w-margin*2-left,h-top-bottom-scale(12),TRUE);
    MoveWindow(status,margin,h-bottom,w-margin*2,scale(24),TRUE);
    int lw=w-margin*2-left;ListView_SetColumnWidth(list,0,lw-scale(382)>scale(220)?lw-scale(382):scale(220));
}
static void command(int id){switch(id){case CMD_OPEN:choose_open();break;case CMD_SAVE:save_image(0);break;case CMD_SAVEAS:save_image(1);break;case CMD_ADD:choose_add();break;case CMD_EXTRACT:choose_extract();break;case CMD_MKDIR:name_action(0);break;case CMD_DELETE:delete_selected();break;case CMD_UP:go_up();break;case CMD_RENAME:name_action(1);break;case CMD_COPY:drag_selected(1);break;}}
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    switch(msg){
    case WM_CREATE:{window=hwnd;HDC dc=GetDC(hwnd);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(hwnd,dc);
        font=CreateFontW(-scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        title_font=CreateFontW(-scale(26),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        const wchar_t *labels[]={L"打开镜像",L"保存",L"另存为",L"添加文件",L"导出选中",L"新建文件夹",L"删除",L"上一级"};int ids[]={CMD_OPEN,CMD_SAVE,CMD_SAVEAS,CMD_ADD,CMD_EXTRACT,CMD_MKDIR,CMD_DELETE,CMD_UP};
        for(int i=0;i<8;i++)buttons[i]=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,hwnd,(HMENU)(INT_PTR)ids[i],instance,NULL);
        pathbar=CreateWindowW(L"STATIC",L"  /",WS_CHILD|WS_VISIBLE|SS_PATHELLIPSIS,0,0,0,0,hwnd,NULL,instance,NULL);
        tree=CreateWindowExW(WS_EX_CLIENTEDGE,WC_TREEVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TVS_HASBUTTONS|TVS_HASLINES|TVS_LINESATROOT|TVS_SHOWSELALWAYS,0,0,0,0,hwnd,(HMENU)301,instance,NULL);
        list=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LVS_REPORT|LVS_SHOWSELALWAYS,0,0,0,0,hwnd,(HMENU)302,instance,NULL);
        ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);ListView_SetBkColor(list,PAPER);ListView_SetTextBkColor(list,PAPER);ListView_SetTextColor(list,INK);TreeView_SetBkColor(tree,PAPER);TreeView_SetTextColor(tree,INK);
        SHFILEINFOW info;HIMAGELIST icons=(HIMAGELIST)SHGetFileInfoW(L"folder",FILE_ATTRIBUTE_DIRECTORY,&info,sizeof(info),SHGFI_SYSICONINDEX|SHGFI_SMALLICON|SHGFI_USEFILEATTRIBUTES);ListView_SetImageList(list,icons,LVSIL_SMALL);
        const wchar_t *cols[]={L"名称",L"类型",L"大小",L"所有者",L"权限"};int widths[]={360,76,96,100,110};for(int i=0;i<5;i++){LVCOLUMNW c={0};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_FMT;c.pszText=(wchar_t *)cols[i];c.cx=scale(widths[i]);c.fmt=i==2?LVCFMT_RIGHT:LVCFMT_LEFT;ListView_InsertColumn(list,i,&c);}
        status=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,0,0,0,0,hwnd,NULL,instance,NULL);
        for(int i=0;i<8;i++)SendMessageW(buttons[i],WM_SETFONT,(WPARAM)font,TRUE);HWND controls[]={tree,list,pathbar,status};for(int i=0;i<4;i++)SendMessageW(controls[i],WM_SETFONT,(WPARAM)font,TRUE);
        DragAcceptFiles(hwnd,TRUE);DragAcceptFiles(list,TRUE);SetWindowSubclass(list,list_proc,1,0);refresh_all();return 0;}
    case WM_SIZE:layout();InvalidateRect(hwnd,NULL,TRUE);return 0;
    case WM_GETMINMAXINFO:((MINMAXINFO *)lp)->ptMinTrackSize=(POINT){scale(940),scale(520)};return 0;
    case WM_COMMAND:command(LOWORD(wp));return 0;
    case WM_DROPFILES:import_drop((HDROP)wp,0);return 0;
    case WM_APP+1:import_drop((HDROP)wp,1);return 0;
    case WM_NOTIFY:{NMHDR *n=(NMHDR *)lp;
        if(n->hwndFrom==tree&&n->code==TVN_SELCHANGEDW&&!refreshing){NMTREEVIEWW *tv=(NMTREEVIEWW *)lp;int at=(int)tv->itemNew.lParam;if(at>=0&&at<node_count){strcpy(current,nodes[at].name);refresh_list();}}
        if(n->hwndFrom==list){
            if(n->code==NM_DBLCLK){int at=((NMITEMACTIVATE *)lp)->iItem;if(at>=0&&at<view_count&&views[at].directory)navigate(views[at].name);}
            if(n->code==LVN_BEGINDRAG)drag_selected(0);
            if(n->code==LVN_KEYDOWN){NMLVKEYDOWN *k=(NMLVKEYDOWN *)lp;if(k->wVKey==VK_DELETE)delete_selected();if(k->wVKey==VK_F2)name_action(1);if(k->wVKey==VK_BACK)go_up();}
            if(n->code==NM_RCLICK){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,CMD_EXTRACT,L"导出选中…");AppendMenuW(menu,MF_STRING,CMD_COPY,L"复制到资源管理器  Ctrl+C");AppendMenuW(menu,MF_STRING,CMD_RENAME,L"重命名  F2");AppendMenuW(menu,MF_SEPARATOR,0,NULL);AppendMenuW(menu,MF_STRING,CMD_DELETE,L"删除  Delete");POINT p;GetCursorPos(&p);TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,hwnd,NULL);DestroyMenu(menu);}
        }return 0;}
    case WM_CTLCOLORSTATIC:{HDC dc=(HDC)wp;SetTextColor(dc,(HWND)lp==status?MUTED:INK);SetBkMode(dc,TRANSPARENT);return (LRESULT)paper_brush;}
    case WM_ERASEBKGND:{RECT r;GetClientRect(hwnd,&r);FillRect((HDC)wp,&r,paper_brush);return 1;}
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT client;GetClientRect(hwnd,&client);RECT header={0,0,client.right,scale(58)};FillRect(dc,&header,ice_brush);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ACCENT);HFONT old=SelectObject(dc,title_font);RECT title={scale(22),scale(12),scale(220),scale(52)};DrawTextW(dc,L"SandFS",-1,&title,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,font);SetTextColor(dc,MUTED);RECT sub={scale(150),scale(18),client.right-scale(20),scale(48)};DrawTextW(dc,L"镜像编辑器   /   打开 · 拖入 · 拖出 · 保存",-1,&sub,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,old);EndPaint(hwnd,&ps);return 0;}
    case WM_CLOSE:if(prompt_unsaved())DestroyWindow(hwnd);return 0;
    case WM_DESTROY:sfs_free(&image);free(views);free(nodes);views=NULL;nodes=NULL;view_capacity=node_capacity=0;DeleteObject(font);DeleteObject(title_font);PostQuitMessage(0);return 0;
    }return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE previous,LPWSTR command_line,int show){
    (void)previous;(void)command_line;instance=inst;OleInitialize(NULL);SetProcessDPIAware();
    INITCOMMONCONTROLSEX cc={sizeof(cc),ICC_LISTVIEW_CLASSES|ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&cc);
    paper_brush=CreateSolidBrush(PAPER);ice_brush=CreateSolidBrush(ICE);
    WNDCLASSEXW cls={0};cls.cbSize=sizeof(cls);cls.lpfnWndProc=wndproc;cls.hInstance=inst;cls.hCursor=LoadCursorW(NULL,IDC_ARROW);cls.hIcon=LoadIconW(NULL,IDI_APPLICATION);cls.hIconSm=cls.hIcon;cls.hbrBackground=paper_brush;cls.lpszClassName=L"SandCoreImageEditor";RegisterClassExW(&cls);
    window=CreateWindowExW(0,cls.lpszClassName,L"SandFS 镜像编辑器",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1160,760,NULL,NULL,inst,NULL);
    ShowWindow(window,show);UpdateWindow(window);
    int argc;wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argv&&argc>1)open_image(argv[1]);LocalFree(argv);
    ACCEL keys[]={{FVIRTKEY|FCONTROL,'S',CMD_SAVE},{FVIRTKEY|FCONTROL,'O',CMD_OPEN},{FVIRTKEY|FCONTROL,'C',CMD_COPY},{FVIRTKEY,VK_BACK,CMD_UP}};
    HACCEL accelerators=CreateAcceleratorTableW(keys,4);MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(!TranslateAcceleratorW(window,accelerators,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    DestroyAcceleratorTable(accelerators);DeleteObject(paper_brush);DeleteObject(ice_brush);OleUninitialize();return 0;
}
