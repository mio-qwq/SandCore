#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "SC32API.H"

/* 回调故意不导出，验证pin依据真实代码范围而不是导出表；fini仍能
 * 使用依赖的SCCLIB。事件FILE由应用持有，最后由SCCLIB统一关闭。 */
static FILE *guard_events;
static int guard_initialized,guard_registered;

static void guard_callback(void)
{
    if(guard_events)fputs("GUARD_CB\n",guard_events);
}

int StdlibGuardInit(void)
{
    if(guard_initialized)return -1;
    guard_initialized=1;
    guard_registered=0;
    guard_events=NULL;
    char mode[64];
    ScU32 length=0;
    if(ScProcessArguments(mode,sizeof(mode),&length))return -1;
    if(strcmp(mode,"init-exit")==0){
        /* SCCLIB依赖已成功init，当前Guard尚未返回0。固定日志缓存
         * 留给成功前缀的SCCLIB fini；误调用Guard fini会追加坏标记。 */
        guard_events=fopen("/HOME/M10STD/INIT.LOG","w");
        if(!guard_events || fputs("INIT_EXIT\n",guard_events)==EOF)return -1;
        exit(42);
        return -1;
    }
    return 0;
}

void StdlibGuardFini(void)
{
    if(guard_events)fputs("GUARD_FINI\n",guard_events);
    guard_events=NULL;
    guard_initialized=0;
}

int StdlibGuardSetStream(FILE *stream)
{
    if(!guard_initialized || !stream || guard_events)return -1;
    guard_events=stream;
    return 0;
}

int StdlibGuardRegister(void)
{
    if(!guard_initialized || !guard_events || guard_registered)return -1;
    /* atexit先取节点再经ROOT固定本库私有函数；成功才提交本地标志。 */
    if(atexit(guard_callback))return -1;
    guard_registered=1;
    return 0;
}

int StdlibGuardNarrow(unsigned char a,signed char b,unsigned short c,
                      short d,bool flag,const char *text,void (*callback)(int))
{
    if(!callback)return 0;
    callback(7);
    return a==0x23 && b==-1 && c==0x2345 && d==-1 && flag==1 &&
           text && text[0]=='C' && text[1]=='V' && !text[2];
}

#ifdef __GNUC__
/* i386 ABI只保证AL。测试库特意留下非零高位，捕捉调用者把整个
 * EAX当_Bool的错误；这是测试机器正文，不属于系统库桥接。 */
__attribute__((naked)) bool StdlibGuardFalse(void)
{
    __asm__("movl $0x100, %eax\n\tret");
}
__attribute__((naked)) bool StdlibGuardTrue(void)
{
    __asm__("movl $0x101, %eax\n\tret");
}
#else
/* 客体自产库没有内联汇编；保留真实_Bool类型与相同公共签名。 */
bool StdlibGuardFalse(void)
{
    return false;
}
bool StdlibGuardTrue(void)
{
    return true;
}
#endif
