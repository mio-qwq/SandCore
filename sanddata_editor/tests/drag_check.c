#define wWinMain editor_unused_entry
#include "../src/sanddata_editor.c"
#undef wWinMain
int wmain(int argc,wchar_t **argv){if(argc!=2)return 2;instance=GetModuleHandleW(NULL);OleInitialize(NULL);
INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_LISTVIEW_CLASSES|ICC_TREEVIEW_CLASSES};InitCommonControlsEx(&controls);
paper_brush=CreateSolidBrush(PAPER);ice_brush=CreateSolidBrush(ICE);
WNDCLASSW c={0};c.lpfnWndProc=wndproc;c.hInstance=instance;c.lpszClassName=L"EditorDragCheck";RegisterClassW(&c);
window=CreateWindowW(c.lpszClassName,L"check",WS_OVERLAPPEDWINDOW,0,0,1000,700,NULL,NULL,instance,NULL);
open_image(argv[1]);if(!image.entries)return 3;navigate("TMP");int found=-1;
for(int i=0;i<view_count;i++)if(same(views[i].name,"TMP/EDITORTEST"))found=i;if(found<0)return 4;
ListView_SetItemState(list,found,LVIS_SELECTED,LVIS_SELECTED);HGLOBAL files=prepare_drag();if(!files)return 5;
DragData *data=calloc(1,sizeof(*data));data->iface.lpVtbl=&data_vtable;data->refs=1;data->files=files;
FORMATETC fmt={CF_HDROP,NULL,DVASPECT_CONTENT,-1,TYMED_HGLOBAL};STGMEDIUM medium={0};
if(FAILED(IDataObject_GetData(&data->iface,&fmt,&medium)))return 6;
if(DragQueryFileW(medium.hGlobal,0xffffffff,NULL,0)!=1)return 7;
wchar_t path[HOST_PATH];DragQueryFileW(medium.hGlobal,0,path,HOST_PATH);size_t n=wcslen(path);wcscat(path,L"\\renamed.bin");
HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);unsigned char bytes[768];DWORD got;
if(f==INVALID_HANDLE_VALUE||!ReadFile(f,bytes,768,&got,NULL)||got!=768)return 8;CloseHandle(f);for(int i=0;i<768;i++)if(bytes[i]!=(unsigned char)i)return 9;
path[n]=0;wcscat(path,L"\\empty");f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);LARGE_INTEGER length;if(f==INVALID_HANDLE_VALUE||!GetFileSizeEx(f,&length)||length.QuadPart)return 10;CloseHandle(f);
IEnumFORMATETC *enumerator=NULL;if(FAILED(IDataObject_EnumFormatEtc(&data->iface,DATADIR_GET,&enumerator)))return 11;FORMATETC advertised;ULONG gotfmt;if(IEnumFORMATETC_Next(enumerator,1,&advertised,&gotfmt)!=S_OK||gotfmt!=1||advertised.cfFormat!=CF_HDROP)return 12;IEnumFORMATETC_Release(enumerator);ReleaseStgMedium(&medium);IDataObject_Release(&data->iface);DestroyWindow(window);OleUninitialize();puts("PASS actual GUI list selection, CF_HDROP COM data/enumeration and exported binary/empty file");return 0;}
