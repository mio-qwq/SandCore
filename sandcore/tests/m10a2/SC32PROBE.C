#include "SC32API.H"

/* 正式API使用者：没有SCAPI、装载器源码、软中断或寄存器布局。 */
static ScFile output;
static ScIoResult io;
static int same(const char *a,const char *b)
{while(*a && *a==*b){a++;b++;}return *a==*b;}
static int say(const char *text)
{
    ScU32 count=0;while(text[count])count++;
    io.size=sizeof(io);io.version=1;
    return ScFileWrite(output,text,count,&io)==0 && io.transferred==count;
}
static int failure(const char *what)
{say("SC32 FAIL ");say(what);say("\n");return 1;}
static void init_info(void *value,ScU32 size)
{ScU32 *words=value;words[0]=size;words[1]=1;}

int main(void)
{
    if(ScFileGetStandard(1,&output))return 2;
    ScCapabilities caps;init_info(&caps,sizeof(caps));
    if(ScSystemCapabilities(&caps) || caps.abi_major!=1 || caps.page_size!=4096 ||
       !(caps.capabilities_lo&SC32_CAP_UPDATE_FILES))return failure("capabilities");
    struct {ScCapabilities value;ScU32 fence[4];} wide;
    init_info(&wide,sizeof(wide));for(int i=0;i<4;i++)wide.fence[i]=0x1234abcd;
    if(ScSystemCapabilities(&wide.value) || wide.value.size!=sizeof(ScCapabilities))return failure("version prefix");
    for(int i=0;i<4;i++)if(wide.fence[i]!=0x1234abcd)return failure("unknown tail");

    ScProcessRef self;init_info(&self,sizeof(self));
    ScProcessInfo process;init_info(&process,sizeof(process));
    if(ScProcessSelf(&self) || !self.pid || !self.generation ||
       ScProcessQuery(&self,&process) || process.pid!=self.pid || process.generation!=self.generation)
        return failure("process identity");
    ScProcessRef stale=self;stale.generation^=0x80000000u;
    init_info(&process,sizeof(process));
    if(ScProcessQuery(&stale,&process)!=SC32_E_STALE)return failure("stale generation");

    ScLibraryDesc load={sizeof(ScLibraryDesc),1,"SC32GUARD.SCL",1,0,1,{0,0}};
    ScLibrary library;
    if(ScLoadLibrary(&load,&library))return failure("load plugin");
    void *readonly_cap,*readonly_ref,*readonly_input,*function;
    if(ScGetSymbol(library,"readonly_capabilities",SC32_SYMBOL_DATA,sizeof(ScCapabilities),&readonly_cap) ||
       ScGetSymbol(library,"readonly_self",SC32_SYMBOL_DATA,sizeof(ScProcessRef),&readonly_ref) ||
       ScGetSymbol(library,"readonly_bytes",SC32_SYMBOL_DATA,8,&readonly_input) ||
       ScGetSymbol(library,"check_cdecl",SC32_SYMBOL_FUNCTION,1,&function))return failure("plugin symbols");
    if(((int (*)(int,int,int,int,int))function)(3,5,7,11,13)!=39)return failure("cross compiler cdecl");
    if(ScSystemCapabilities(readonly_cap)!=SC32_E_INVALID)return failure("readonly output");
    ScErrorInfo error;init_info(&error,sizeof(error));
    if(ScErrorQuery(&error) || error.code!=SC32_E_INVALID)return failure("stable error");
    init_info(&process,sizeof(process));
    if(ScProcessQuery(readonly_ref,&process) || process.pid!=self.pid)return failure("readonly input");
    if(ScFreeLibrary(library)!=SC32_RESIDENT || !same(readonly_input,"ROINPUT"))return failure("pinned symbol");

    ScMemoryInfo before,after;init_info(&before,sizeof(before));init_info(&after,sizeof(after));
    void *memory;
    if(ScSystemMemory(&before) || ScMemoryAllocatePages(8192,&memory) || ScSystemMemory(&after) ||
       after.process_heap_pages!=before.process_heap_pages+2)return failure("private page counts");
    if(ScMemoryFreePages(memory))return failure("free pages");
    init_info(&after,sizeof(after));
    if(ScSystemMemory(&after) || after.process_heap_pages!=before.process_heap_pages)return failure("page recovery");
    ScMonotonicClock start,end;init_info(&start,sizeof(start));init_info(&end,sizeof(end));
    if(ScSystemMonotonic(&start) || ScProcessSleep(2) || ScSystemMonotonic(&end) ||
       (end.ticks.hi==start.ticks.hi && end.ticks.lo-start.ticks.lo<2))return failure("monotonic wait");

    /* 每个验证进程有不同PID目录，普通与SYSTEM实例不互相改测例文件。 */
    char folder[65]="/HOME/M10P/P",path[65];char digits[10];ScU32 n=0,value=self.pid;
    do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    ScU32 used=0;while(folder[used])used++;
    while(n)folder[used++]=digits[--n];
    folder[used++]='G';value=self.generation;
    do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    while(n)folder[used++]=digits[--n];
    folder[used]=0;
    if(ScPathCreate(folder,SC32_PATH_DIRECTORY,63))return failure("create own directory");
    for(ScU32 i=0;i<used;i++)path[i]=folder[i];
    const char *leaf="/DATA.BIN";ScU32 k=0;while(leaf[k]){path[used+k]=leaf[k];k++;}path[used+k]=0;
    ScFileOpenDesc open={sizeof(ScFileOpenDesc),1,path,
        SC32_FILE_READ|SC32_FILE_WRITE|SC32_FILE_CREATE|SC32_FILE_TRUNCATE,63,{0,0,0}};
    ScFile file;ScFileInfo info;init_info(&info,sizeof(info));
    if(ScFileOpen(&open,&file))return failure("open update file");
    init_info(&io,sizeof(io));
    if(ScFileWrite(file,"ABCD",4,&io) || io.transferred!=4 || ScFileFlush(file))return failure("first flush");
    if(ScFileSeek(file,2,SC32_SEEK_SET,&info))return failure("seek read");
    char bytes[16];init_info(&io,sizeof(io));
    if(ScFileRead(file,bytes,2,&io) || io.transferred!=2 || bytes[0]!='C' || bytes[1]!='D')return failure("update read");
    init_info(&info,sizeof(info));
    if(ScFileSeek(file,131075,SC32_SEEK_SET,&info))return failure("seek beyond EOF");
    init_info(&io,sizeof(io));
    if(ScFileWrite(file,readonly_input,7,&io) || io.transferred!=7 || ScFileFlush(file))return failure("zero hole readonly write");
    init_info(&info,sizeof(info));
    if(ScFileSeek(file,131069,SC32_SEEK_SET,&info))return failure("read hole position");
    init_info(&io,sizeof(io));
    if(ScFileRead(file,bytes,13,&io) || io.transferred!=13)return failure("read zero hole");
    for(int i=0;i<6;i++)if(bytes[i])return failure("hole content");
    for(int i=0;i<7;i++)if(bytes[6+i]!=((const char *)readonly_input)[i])return failure("readonly content");
    if(ScFileClose(file))return failure("close update file");
    ScDirectory directory;ScDirectoryEntry entries[4];ScDirectoryPage page;init_info(&page,sizeof(page));
    if(ScDirectoryOpen(folder,&directory) || ScDirectoryNext(directory,entries,4,&page) ||
       page.count!=1 || !same(entries[0].name,"DATA.BIN") || ScDirectoryClose(directory))return failure("directory page");
    ScJob job;ScJobInfo job_info;init_info(&job_info,sizeof(job_info));
    if(ScProcessSpawn("echo SC32_CHILD_OK",0,0,&job) || ScJobWait(job,SC32_INFINITE,&job_info) ||
       job_info.exit_code!=0)return failure("spawn after standard take");
    if(!say("SC32_FOUNDATION_PASS\n"))return 3;
    return 0;
}
