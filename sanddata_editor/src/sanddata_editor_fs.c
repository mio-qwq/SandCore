/* 宿主离线编辑器：保存采用完整回读与同目录替换，避免半写目录毁掉原盘。 */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include "sanddata_editor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>

wchar_t sfs_error[1024];
/* 整盘快照和新导入正文引用计数。拖入的整批撤销只复制目录，不复制
 * 256MiB快照或所有正文；工作副本修改的对象另分配，旧对象始终不改。 */
struct SfsBuffer {uint32_t refs;unsigned char *data;size_t size;};
static SfsBuffer *buffer_new(unsigned char *data,size_t size)
{SfsBuffer *b=malloc(sizeof(*b));if(b){b->refs=1;b->data=data;b->size=size;}return b;}
static void buffer_release(SfsBuffer *b)
{if(b && !--b->refs){free(b->data);free(b);}}
static int fail(const wchar_t *format,...) {
    va_list args;va_start(args,format);vswprintf(sfs_error,1024,format,args);va_end(args);return 0;
}
static int winfail(const wchar_t *what) {
    wchar_t text[512]=L"";DWORD code=GetLastError();
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,NULL,code,0,text,512,NULL);
    return fail(L"%ls：%ls",what,text);
}
static uint32_t get32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);p[2]=(unsigned char)(n>>16);p[3]=(unsigned char)(n>>24);}
static uint32_t crc32(const unsigned char *p,size_t n) {
    static uint32_t table[256];static int ready;uint32_t crc=~0u;
    if(!ready){for(unsigned i=0;i<256;i++){uint32_t c=i;for(int k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320u:0);table[i]=c;}ready=1;}
    for(size_t i=0;i<n;i++)crc=(crc>>8)^table[(crc^p[i])&255];return ~crc;
}
static int fold(int c){return c>='a'&&c<='z'?c-'a'+'A':c;}
static int same(const char *a,const char *b){while(*a&&*b){if(fold((unsigned char)*a++)!=fold((unsigned char)*b++))return 0;}return *a==*b;}
int sfs_child(const char *name,const char *parent) {
    size_t n=strlen(parent);if(!n)return name[0]!=0;
    for(size_t i=0;i<n;i++)if(!name[i]||fold((unsigned char)name[i])!=fold((unsigned char)parent[i]))return 0;
    return name[n]=='/';
}
wchar_t *sfs_wide(const char *s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0);if(!n)return NULL;
    wchar_t *out=malloc((size_t)n*sizeof(wchar_t));if(out)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,out,n);return out;
}
char *sfs_utf8(const wchar_t *s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,NULL,0,NULL,NULL);if(!n)return NULL;
    char *out=malloc((size_t)n);if(out)WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s,-1,out,n,NULL,NULL);return out;
}
int sfs_valid_name(const char *s,uint32_t limit) {
    size_t len=strlen(s);if(!len||len>limit||s[0]=='/'||s[len-1]=='/')return 0;
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s,-1,NULL,0))return 0;
    const char *part=s;
    for(const char *p=s;;p++){
        if(*p&&((unsigned char)*p<=32||*p=='\\'||*p==':'))return 0;
        if(!*p||*p=='/'){
            size_t n=(size_t)(p-part);if(!n||(n==1&&part[0]=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return 0;
            if(!*p)break;part=p+1;
        }
    }return 1;
}
int sfs_find(const SfsImage *fs,const char *name){for(uint32_t i=0;i<fs->count;i++)if(same(fs->entries[i].name,name))return (int)i;return -1;}
void sfs_free(SfsImage *fs){if(fs->entries)for(uint32_t i=0;i<fs->count;i++)buffer_release(fs->entries[i].content);free(fs->entries);
    if(fs->snapshot)buffer_release(fs->snapshot);else free(fs->original);free(fs->path);memset(fs,0,sizeof(*fs));}
static int parse(SfsImage *fs,unsigned char *raw,size_t length) {
    memset(fs,0,sizeof(*fs));fs->original=raw;fs->image_size=length;
    fs->snapshot=buffer_new(raw,length);if(!fs->snapshot)return fail(L"内存不足。");
    if(length<512||length%512||memcmp(raw,"SANDFSMIO",9))return fail(L"不是有效的 SandFS 镜像。请选择 sanddata.img。");
    fs->count=get32(raw+12);fs->dir_sectors=get32(raw+16);fs->version=get32(raw+20);
    if(!fs->dir_sectors)fs->dir_sectors=1;
    uint32_t stride,names;
    if(fs->version==5&&(fs->dir_sectors==192||fs->dir_sectors==384||fs->dir_sectors==768||fs->dir_sectors==1536)&&get32(raw+28)==96){
        if(crc32(raw,508)!=get32(raw+508))return fail(L"超级块校验失败，拒绝编辑损坏的镜像。");
        stride=96;names=64;fs->capacity=fs->dir_sectors*512/96;
        fs->bank=get32(raw+48);fs->commit_generation=get32(raw+52);fs->object_generation=get32(raw+56);
        if(fs->bank>1||!fs->commit_generation||(int32_t)get32(raw+32)<-1||(int32_t)get32(raw+36)<-1||(get32(raw+40)&~63u))return fail(L"超级块身份或目录代数非法。");
    }else if(fs->version==4&&(fs->dir_sectors==32||fs->dir_sectors==80)){stride=72;names=64;fs->capacity=fs->dir_sectors==32?192:512;}
    else if(fs->version<=3&&(fs->dir_sectors==1||fs->dir_sectors==8)){stride=40;names=32;fs->capacity=fs->dir_sectors*512/40;}
    else return fail(L"不支持此 SandFS 目录格式。");
    if(fs->count>fs->capacity)return fail(L"目录项目数超过容量。");
    fs->data_start=1+fs->dir_sectors*(fs->version==5?2:1);
    fs->sectors=fs->version>=4?get32(raw+24):16384;if(!fs->sectors)fs->sectors=16384;
    if(fs->sectors<fs->data_start||fs->sectors>524288||fs->sectors>length/512)return fail(L"镜像容量声明非法。");
    size_t offset=(size_t)(1+fs->bank*fs->dir_sectors)*512;
    if(offset+(size_t)fs->dir_sectors*512>length)return fail(L"目录超出镜像。");
    if(fs->version==5&&crc32(raw+offset,(size_t)fs->dir_sectors*512)!=get32(raw+44))return fail(L"活动目录校验失败，拒绝编辑损坏的镜像。");
    fs->entries=calloc(fs->capacity,sizeof(*fs->entries));if(!fs->entries)return fail(L"内存不足。");
    unsigned char *used=calloc(fs->sectors,1);if(!used)return fail(L"内存不足。");
    for(uint32_t i=0;i<fs->count;i++){
        const unsigned char *e=raw+offset+(size_t)i*stride;SfsEntry *r=fs->entries+i;
        if(!e[0]||e[names-1]||!memchr(e,0,names)){free(used);return fail(L"目录文件名非法。");}
        memcpy(r->name,e,names);if(!sfs_valid_name(r->name,names-1)){free(used);return fail(L"目录路径不是有效规范路径。");}
        for(uint32_t j=0;j<i;j++)if(same(r->name,fs->entries[j].name)){free(used);return fail(L"镜像含重复路径。");}
        uint32_t at=get32(e+names);r->size=get32(e+names+4);r->directory=fs->version>=4&&!at&&!r->size;
        r->mode=23;r->generation=1;
        if(fs->version==5){r->uid=(int32_t)get32(e+72);r->gid=(int32_t)get32(e+76);r->mode=get32(e+80);r->generation=get32(e+88);
            if(r->uid<-1||r->gid<-1||(r->mode&~63u)||get32(e+84)!=(uint32_t)r->directory||!r->generation||get32(e+92)){free(used);return fail(L"文件身份、权限、类型或代数非法。");}
            if(r->generation>fs->object_generation)fs->object_generation=r->generation;
        }
        if(r->directory)continue;
        uint32_t blocks=(r->size+511)/512;
        if(r->size>fs->sectors*512||at<fs->data_start||at>fs->sectors||blocks>fs->sectors-at){free(used);return fail(L"文件数据超出磁盘容量。");}
        for(uint32_t b=at;b<at+blocks;b++){if(used[b]){free(used);return fail(L"文件数据互相重叠。");}used[b]=1;}
        if(r->size)r->data=raw+(size_t)at*512;
    }
    free(used);
    if(fs->version==5)for(uint32_t i=0;i<fs->count;i++){
        char parent[64];strcpy(parent,fs->entries[i].name);
        for(char *p=parent;*p;p++)if(*p=='/'){*p=0;int at=sfs_find(fs,parent);*p='/';if(at<0||!fs->entries[at].directory)return fail(L"镜像缺失显式父目录。");}
    }
    return 1;
}
static unsigned char *read_file(const wchar_t *path,size_t max,size_t *size) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file==INVALID_HANDLE_VALUE){winfail(L"无法打开文件（请先关闭使用此镜像的 QEMU）");return NULL;}
    LARGE_INTEGER length;if(!GetFileSizeEx(file,&length)||length.QuadPart<0||(uint64_t)length.QuadPart>max){CloseHandle(file);fail(L"文件超过可用容量。");return NULL;}
    *size=(size_t)length.QuadPart;unsigned char *p=malloc(*size?*size:1);DWORD got=0;
    if(!p){CloseHandle(file);fail(L"内存不足。");return NULL;}
    if(!ReadFile(file,p,(DWORD)*size,&got,NULL)||got!=*size){free(p);CloseHandle(file);winfail(L"读取失败");return NULL;}CloseHandle(file);return p;
}
int sfs_load(SfsImage *fs,const wchar_t *path) {
    wchar_t absolute[HOST_PATH];DWORD n=GetFullPathNameW(path,HOST_PATH,absolute,NULL);if(!n||n>=HOST_PATH)return fail(L"镜像路径无效或过长。");
    size_t size;unsigned char *raw=read_file(absolute,256u*1024*1024,&size);if(!raw)return 0;
    SfsImage fresh;if(!parse(&fresh,raw,size)){sfs_free(&fresh);return 0;}
    fresh.path=_wcsdup(absolute);if(!fresh.path){sfs_free(&fresh);return fail(L"内存不足。");}sfs_free(fs);*fs=fresh;return 1;
}
int sfs_clone(SfsImage *dest,const SfsImage *src) {
    *dest=*src;dest->entries=NULL;dest->original=NULL;dest->path=NULL;dest->snapshot=NULL;
    dest->entries=calloc(src->capacity,sizeof(*dest->entries));dest->path=src->path?_wcsdup(src->path):NULL;
    if(!dest->entries||(src->path&&!dest->path)){sfs_free(dest);return fail(L"内存不足。");}
    dest->original=src->original;dest->snapshot=src->snapshot;if(dest->snapshot)dest->snapshot->refs++;
    for(uint32_t i=0;i<src->count;i++){dest->entries[i]=src->entries[i];if(dest->entries[i].content)dest->entries[i].content->refs++;}
    return 1;
}
static uint32_t next_generation(SfsImage *fs){if(fs->version!=5)return 1;if(fs->object_generation==UINT32_MAX){fail(L"对象代数已经耗尽。");return 0;}return ++fs->object_generation;}
static void defaults(SfsEntry *e) {
    e->uid=e->gid=0;e->mode=23;
    if(same(e->name,"HOME")||sfs_child(e->name,"HOME")||same(e->name,"DESK")||sfs_child(e->name,"DESK")||sfs_child(e->name,"SYS/THEMES")||same(e->name,"SYS/DISPLAY.CFG")||same(e->name,"SYS/THEME.CFG")||same(e->name,"SYS/WALL.CFG")||same(e->name,"SYS/MENU.CFG"))e->uid=e->gid=1000;
    if(same(e->name,"HOME/ROOT")||sfs_child(e->name,"HOME/ROOT")){e->uid=e->gid=0;e->mode=3;}
    if(same(e->name,"SYS/CORE")||sfs_child(e->name,"SYS/CORE")||same(e->name,"SYS/MOD")||sfs_child(e->name,"SYS/MOD")||same(e->name,"SYS/AUTH")||sfs_child(e->name,"SYS/AUTH")){e->uid=e->gid=-1;if(same(e->name,"SYS/AUTH")||sfs_child(e->name,"SYS/AUTH"))e->mode=3;}
    if(same(e->name,"TMP"))e->mode=63;
}
static int room(const SfsImage *fs) {
    uint32_t blocks=fs->data_start;for(uint32_t i=0;i<fs->count;i++)if(!fs->entries[i].directory){uint32_t n=(fs->entries[i].size+511)/512;if(n>fs->sectors-blocks)return fail(L"镜像空间不足，修改没有保存。");blocks+=n;}return 1;
}
int sfs_mkdir(SfsImage *fs,const char *name) {
    if(!sfs_valid_name(name,fs->version<=3?31:63))return fail(L"SandFS 路径最多 %u 个 UTF-8 字节，不能包含空格、反斜杠或冒号。",fs->version<=3?31u:63u);
    int prior=sfs_find(fs,name);if(prior>=0)return fs->entries[prior].directory?1:fail(L"同名位置已经是文件。");
    if(fs->version<=3)return 1; /* 旧卷通过文件路径推导目录，不能写入零LBA目录项。 */
    char parent[64];strcpy(parent,name);char *slash=strrchr(parent,'/');if(slash){*slash=0;if(!sfs_mkdir(fs,parent))return 0;}
    if(fs->count>=fs->capacity)return fail(L"目录项目已满。");uint32_t gen=next_generation(fs);if(!gen)return 0;
    SfsEntry *e=fs->entries+fs->count++;memset(e,0,sizeof(*e));strcpy(e->name,name);e->directory=1;e->generation=gen;defaults(e);fs->dirty=1;return 1;
}
int sfs_add_path(SfsImage *fs,const wchar_t *host,const char *dest) {
    DWORD attr=GetFileAttributesW(host);if(attr==INVALID_FILE_ATTRIBUTES)return winfail(L"无法读取拖入的文件");
    if(attr&FILE_ATTRIBUTE_REPARSE_POINT)return fail(L"请拖入实际文件或文件夹，暂不导入链接。");
    if(!sfs_valid_name(dest,fs->version<=3?31:63))return fail(L"SandFS 路径过长或包含空格等非法字符，请重命名后导入。");
    if(attr&FILE_ATTRIBUTE_DIRECTORY){
        if(!sfs_mkdir(fs,dest))return 0;
        size_t n=wcslen(host);wchar_t *pattern=malloc((n+3)*sizeof(wchar_t));if(!pattern)return fail(L"内存不足。");swprintf(pattern,n+3,L"%ls\\*",host);
        WIN32_FIND_DATAW fd;HANDLE search=FindFirstFileW(pattern,&fd);free(pattern);
        if(search==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND?1:winfail(L"无法读取文件夹");
        int ok=1;
        do{if(!wcscmp(fd.cFileName,L".")||!wcscmp(fd.cFileName,L".."))continue;
            char *leaf=sfs_utf8(fd.cFileName);char child[64];wchar_t *path=malloc((n+wcslen(fd.cFileName)+2)*sizeof(wchar_t));
            if(!leaf||!path||strlen(dest)+strlen(leaf)+1>63){free(leaf);free(path);ok=fail(L"导入后的路径超过 SandFS 上限。");break;}
            size_t prefix=strlen(dest);memcpy(child,dest,prefix);child[prefix++]='/';memcpy(child+prefix,leaf,strlen(leaf)+1);swprintf(path,n+wcslen(fd.cFileName)+2,L"%ls\\%ls",host,fd.cFileName);free(leaf);
            ok=sfs_add_path(fs,path,child);free(path);if(!ok)break;
        }while(FindNextFileW(search,&fd));
        if(ok&&GetLastError()!=ERROR_NO_MORE_FILES)ok=winfail(L"文件夹读取中断");FindClose(search);return ok;
    }
    char parent[64];strcpy(parent,dest);char *slash=strrchr(parent,'/');if(slash){*slash=0;if(!sfs_mkdir(fs,parent))return 0;}
    int at=sfs_find(fs,dest);if(at>=0&&fs->entries[at].directory)return fail(L"不能用文件覆盖文件夹。");
    if(at<0&&fs->count>=fs->capacity)return fail(L"目录项目已满。");
    size_t size;unsigned char *data=read_file(host,(size_t)fs->sectors*512,&size);if(!data)return 0;
    if(at>=0&&fs->entries[at].size==size&&(!size||!memcmp(fs->entries[at].data,data,size))){free(data);return 1;}
    uint32_t gen=next_generation(fs);if(!gen){free(data);return 0;}
    SfsBuffer *content=buffer_new(data,size);if(!content){free(data);return fail(L"内存不足。");}
    SfsEntry *e;if(at<0){e=fs->entries+fs->count++;memset(e,0,sizeof(*e));strcpy(e->name,dest);defaults(e);}else{e=fs->entries+at;buffer_release(e->content);}
    e->content=content;e->data=data;e->size=(uint32_t)size;e->generation=gen;fs->dirty=1;return room(fs);
}
int sfs_remove(SfsImage *fs,const char *name) {
    uint32_t out=0;int removed=0;
    for(uint32_t i=0;i<fs->count;i++){
        if(same(fs->entries[i].name,name)||sfs_child(fs->entries[i].name,name)){buffer_release(fs->entries[i].content);removed=1;}
        else fs->entries[out++]=fs->entries[i];
    }fs->count=out;if(removed)fs->dirty=1;return 1;
}
int sfs_rename(SfsImage *fs,const char *from,const char *to) {
    if(!sfs_valid_name(to,fs->version<=3?31:63))return fail(L"新名称超过路径上限或含非法字符。");
    if(!same(from,to)&&sfs_find(fs,to)>=0)return fail(L"新名称已经存在。");
    size_t prefix=strlen(from);
    for(uint32_t i=0;i<fs->count;i++)if(same(fs->entries[i].name,from)||sfs_child(fs->entries[i].name,from)){
        char name[128];snprintf(name,sizeof(name),"%s%s",to,fs->entries[i].name+prefix);
        if(!sfs_valid_name(name,fs->version<=3?31:63))return fail(L"子文件的新路径超过上限。");
        for(uint32_t j=0;j<fs->count;j++)if(!same(fs->entries[j].name,from)&&!sfs_child(fs->entries[j].name,from)&&same(name,fs->entries[j].name))return fail(L"重命名与现有文件冲突。");
    }
    for(uint32_t i=0;i<fs->count;i++)if(same(fs->entries[i].name,from)||sfs_child(fs->entries[i].name,from)){
        char name[64];snprintf(name,sizeof(name),"%s%s",to,fs->entries[i].name+prefix);uint32_t gen=next_generation(fs);if(!gen)return 0;strcpy(fs->entries[i].name,name);fs->entries[i].generation=gen;
    }fs->dirty=1;return 1;
}
static int host_dirs(wchar_t *path) {
    for(wchar_t *p=path+3;*p;p++)if(*p=='\\'||*p=='/'){wchar_t saved=*p;*p=0;
        DWORD attr=GetFileAttributesW(path);if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT)){*p=saved;return fail(L"导出目录中含链接，拒绝穿越。");}
        if(!CreateDirectoryW(path,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS){*p=saved;return winfail(L"无法创建导出目录");}*p=saved;
    }return 1;
}
static int host_name_valid(const wchar_t *text) {
    for(const wchar_t *part=text;*part;){
        const wchar_t *end=wcschr(part,L'\\');if(!end)end=part+wcslen(part);
        if(end==part||end[-1]==L'.'||end[-1]==L' ')return 0;
        const wchar_t *dot=wmemchr(part,L'.',(size_t)(end-part));size_t n=(size_t)((dot?dot:end)-part);
        if(n==3&&(!_wcsnicmp(part,L"CON",3)||!_wcsnicmp(part,L"PRN",3)||!_wcsnicmp(part,L"AUX",3)||!_wcsnicmp(part,L"NUL",3)))return 0;
        if(n==4&&(!_wcsnicmp(part,L"COM",3)||!_wcsnicmp(part,L"LPT",3))&&wcschr(L"123456789¹²³",part[3]))return 0;
        part=*end?end+1:end;
    }return 1;
}
int sfs_extract(const SfsImage *fs,const char *name,const wchar_t *folder) {
    const char *leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;size_t skip=strlen(name)-strlen(leaf);int any=0;
    for(uint32_t i=0;i<fs->count;i++)if(same(fs->entries[i].name,name)||sfs_child(fs->entries[i].name,name)){
        const SfsEntry *e=fs->entries+i;wchar_t *rel=sfs_wide(e->name+skip);if(!rel)return fail(L"文件名编码错误。");
        /* Windows路径本身有额外的字符限制；不能让文件名被解释成路径通配符。 */
        for(wchar_t *p=rel;*p;p++){if(wcschr(L"<>:\"|?*",*p)){free(rel);return fail(L"文件名包含 Windows 不能导出的字符。");}if(*p=='/')*p='\\';}
        if(!host_name_valid(rel)){free(rel);return fail(L"文件名使用 Windows 保留名称或以点结尾，请先在镜像中重命名。");}
        size_t n=wcslen(folder)+wcslen(rel)+2;wchar_t *out=malloc(n*sizeof(wchar_t));if(!out){free(rel);return fail(L"内存不足。");}swprintf(out,n,L"%ls\\%ls",folder,rel);free(rel);
        int ok=host_dirs(out);if(ok&&e->directory){if(!CreateDirectoryW(out,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)ok=winfail(L"无法创建文件夹");}
        if(ok&&!e->directory){HANDLE f=CreateFileW(out,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);DWORD done=0;
            if(f==INVALID_HANDLE_VALUE)ok=winfail(L"无法导出（不会覆盖已有文件）");else{if(!WriteFile(f,e->data,e->size,&done,NULL)||done!=e->size)ok=winfail(L"文件导出失败");CloseHandle(f);}
        }free(out);if(!ok)return 0;any=1;
    }return any?1:fail(L"此目录没有可导出的项目。");
}
static unsigned char *serialize(const SfsImage *fs) {
    if(!room(fs))return NULL;
    unsigned char *out=calloc(fs->image_size,1);if(!out){fail(L"内存不足。");return NULL;}memcpy(out,fs->original,512);
    uint32_t stride=fs->version==5?96:fs->version==4?72:40,names=fs->version<=3?32:64;
    uint32_t bank=fs->version==5?1-fs->bank:0,cursor=fs->data_start;
    unsigned char *meta=out+(size_t)(1+bank*fs->dir_sectors)*512;
    for(uint32_t i=0;i<fs->count;i++){
        const SfsEntry *r=fs->entries+i;unsigned char *e=meta+(size_t)i*stride;memcpy(e,r->name,strlen(r->name));
        if(!r->directory){put32(e+names,cursor);put32(e+names+4,r->size);if(r->size)memcpy(out+(size_t)cursor*512,r->data,r->size);cursor+=(r->size+511)/512;}
        if(fs->version==5){put32(e+72,(uint32_t)r->uid);put32(e+76,(uint32_t)r->gid);put32(e+80,r->mode);put32(e+84,(uint32_t)r->directory);put32(e+88,r->generation);}
    }
    put32(out+12,fs->count);
    if(fs->version==5){memcpy(out+(size_t)(1+(1-bank)*fs->dir_sectors)*512,meta,(size_t)fs->dir_sectors*512);
        put32(out+44,crc32(meta,(size_t)fs->dir_sectors*512));put32(out+48,bank);
        put32(out+52,fs->commit_generation==UINT32_MAX?1:fs->commit_generation+1);put32(out+56,fs->object_generation);put32(out+508,crc32(out,508));
    }return out;
}
static int file_equal(HANDLE file,const unsigned char *expected,size_t size)
{
    LARGE_INTEGER length;if(!GetFileSizeEx(file,&length)||(uint64_t)length.QuadPart!=size)return 0;
    unsigned char *block=malloc(1048576);if(!block)return 0;int ok=1;
    for(size_t at=0;at<size;){DWORD count=(DWORD)(size-at>1048576?1048576:size-at),got=0;
        if(!ReadFile(file,block,count,&got,NULL)||got!=count||memcmp(block,expected+at,count)){ok=0;break;}at+=count;}
    free(block);return ok;
}
int sfs_save(SfsImage *fs,const wchar_t *target) {
    unsigned char *out=serialize(fs);if(!out)return 0;
    /* 回读同一份序列化结果；父目录、重叠范围和CRC不合格就不碰原镜像。 */
    SfsImage check;if(!parse(&check,out,fs->image_size)){sfs_free(&check);return 0;}
    for(uint32_t i=0;i<fs->count;i++){SfsEntry *a=fs->entries+i,*b=check.entries+i;
        if(strcmp(a->name,b->name)||a->size!=b->size||a->directory!=b->directory||(a->size&&memcmp(a->data,b->data,a->size))||(fs->version==5&&(a->uid!=b->uid||a->gid!=b->gid||a->mode!=b->mode||a->generation!=b->generation))){sfs_free(&check);return fail(L"保存前内容回读失败。");}
    }
    check.path=_wcsdup(target);if(!check.path){sfs_free(&check);return fail(L"内存不足。");}
    HANDLE guard=INVALID_HANDLE_VALUE;DWORD attr=GetFileAttributesW(target);
    if(attr!=INVALID_FILE_ATTRIBUTES){
        guard=CreateFileW(target,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if(guard==INVALID_HANDLE_VALUE){sfs_free(&check);return winfail(L"镜像正在使用，无法保存");}
        if(fs->path&&!_wcsicmp(fs->path,target)){
            if(!file_equal(guard,fs->original,fs->image_size)){CloseHandle(guard);sfs_free(&check);return fail(L"镜像已被其他程序修改。请另存为新文件，避免覆盖外部修改。");}
        }
    }
    size_t cap=wcslen(target)+96;wchar_t *temp=malloc(cap*sizeof(wchar_t)),*backup=malloc(cap*sizeof(wchar_t));
    if(!temp||!backup){free(temp);free(backup);if(guard!=INVALID_HANDLE_VALUE)CloseHandle(guard);sfs_free(&check);return fail(L"内存不足。");}
    SYSTEMTIME time;GetLocalTime(&time);HANDLE file=INVALID_HANDLE_VALUE;
    for(unsigned k=0;k<100;k++){swprintf(temp,cap,L"%ls.tmp-%lu-%u",target,GetCurrentProcessId(),k);file=CreateFileW(temp,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);if(file!=INVALID_HANDLE_VALUE)break;if(GetLastError()!=ERROR_FILE_EXISTS)break;}
    int ok=1;DWORD done=0;
    if(file==INVALID_HANDLE_VALUE)ok=winfail(L"无法创建保存副本");
    else{if(!WriteFile(file,out,(DWORD)fs->image_size,&done,NULL)||done!=fs->image_size||!FlushFileBuffers(file))ok=winfail(L"写入保存副本失败");CloseHandle(file);}
    if(ok){HANDLE reread=CreateFileW(temp,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if(reread==INVALID_HANDLE_VALUE||!file_equal(reread,out,fs->image_size))ok=fail(L"磁盘保存副本回读失败，原镜像未替换。");
        if(reread!=INVALID_HANDLE_VALUE)CloseHandle(reread);}
    if(ok&&guard!=INVALID_HANDLE_VALUE){
        for(unsigned k=0;k<100;k++){swprintf(backup,cap,L"%ls.bak-%04u%02u%02u-%02u%02u%02u-%u",target,time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,k);if(GetFileAttributesW(backup)==INVALID_FILE_ATTRIBUTES)break;}
        if(!ReplaceFileW(target,temp,backup,REPLACEFILE_IGNORE_MERGE_ERRORS,NULL,NULL))ok=winfail(L"无法替换镜像，保存副本和备份保留在原目录");
    }else if(ok&&!MoveFileExW(temp,target,MOVEFILE_WRITE_THROUGH))ok=winfail(L"无法写入目标文件");
    if(guard!=INVALID_HANDLE_VALUE)CloseHandle(guard);
    if(!ok){/* 失败保留已经生成的完整副本，便于恢复，不悄悄删除证据。 */sfs_free(&check);}else{sfs_free(fs);*fs=check;fs->dirty=0;}
    free(temp);free(backup);return ok;
}
