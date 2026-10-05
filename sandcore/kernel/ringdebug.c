#include "ringdebug.h"
#include "serial.h"
#include "memory.h"
#include "paging.h"
#include "task.h"
/* 真实外部COM1调试器。暂停时IF=0且完全不调用调度/窗口/文件系统，
 * 用UART紧急轮询收发，所以被调试的Shell/GUI故障不影响控制通道。
 * 该入口没有三环系统调用；UART外部RX本身是最高权限的物理边界。 */
typedef struct {u32 address;u8 original;int used;} breakpoint_t;
static breakpoint_t breaks[16];
static u32 *stopped;
static int ready,armed,halt_requested,stepping,pending,continue_after_step,fatal;
static char command[600];
static u32 used;
static void output(const char *text)
{while(*text){if(serial_emergency_put(SERIAL_DEBUG,(u8)*text++))return;}}
static void hex(u32 value)
{char out[9];const char *digits="0123456789abcdef";for(int i=0;i<8;i++)out[i]=digits[(value>>(28-i*4))&15];out[8]=0;output(out);}
static int equal(const char *a,const char *b){while(*a && *a==*b){a++;b++;}return *a==*b;}
static char *token(char **p){while(**p==' ')(*p)++;char *begin=*p;while(**p && **p!=' ')(*p)++;if(**p)*(*p)++=0;return begin;}
static int integer(const char *text,u32 *out)
{
    if(text[0]=='0' && text[1]=='x')text+=2;if(!*text)return -1;u32 value=0;
    while(*text){int c=*text++,d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(d<0 || value>(0xFFFFFFFFu-(u32)d)/16)return -1;value=value*16+(u32)d;}
    *out=value;return 0;
}
static int ram(u32 address,u32 length)
{
    if(!length || address<0x10000u || length>256 || length>0xFFFFFFFFu-address)return 0;
    u32 end=address+length;
    if((address<USER_TOP && end>USER_BASE) || (address<USER_HEAP_TOP && end>USER_HEAP_BASE))return 0;
    for(u32 i=0;i<e820_count();i++){
        e820_entry_t *e=e820_entry(i);if(e->type_lo!=1 || e->base_hi || e->len_hi)continue;
        if(address>=e->base_lo && address-e->base_lo<=e->len_lo && length<=e->len_lo-(address-e->base_lo))return 1;
    }
    return 0;
}
static void registers(void)
{
    if(!stopped){output("ERR running\r\n");return;}
    static const char *names[17]={"gs","fs","es","ds","edi","esi","ebp","saved-esp","ebx","edx","ecx","eax","vector","error","eip","cs","eflags"};
    for(u32 i=0;i<17;i++){output(names[i]);output("=");hex(stopped[i]);output(i%4==3?"\r\n":" ");}
    output("esp=");hex((stopped[15]&3)==3?stopped[17]:(u32)(stopped+17));
    output(" pid=");hex((u32)task_pid());output(" cr3=");u32 pd;__asm__ __volatile__("mov %%cr3,%0":"=r"(pd));hex(pd);output("\r\n");
}
static int execute(void)
{
    char *p=command,*op=token(&p),*a=token(&p),*b=token(&p);u32 address=0,count=0;
    if(equal(op,"hello") || equal(op,"help")){armed=1;output("SCDBG1 hello halt regs mem <hex> <hexlen> write <hex> <hexbytes> break <hex> clear <hex> step cont\r\n");return 0;}
    if(equal(op,"halt")){armed=1;halt_requested=1;output(stopped?"OK stopped\r\n":"OK requested\r\n");return 0;}
    if(equal(op,"regs")){registers();return 0;}
    if(equal(op,"step") || equal(op,"cont")){
        if(!stopped || fatal){output(fatal?"ERR fatal exception cannot resume\r\n":"ERR running\r\n");return 0;}
        stepping=equal(op,"step");continue_after_step=!stepping;
        if(pending>=0 || stepping)stopped[16]|=0x100u;else stopped[16]&=~0x100u;
        output("OK resume\r\n");return 1;
    }
    if(integer(a,&address)){output("ERR address\r\n");return 0;}
    if(equal(op,"mem")){
        if(!stopped || integer(b,&count) || !ram(address,count)){output("ERR stopped RAM range required\r\n");return 0;}
        output("DATA ");for(u32 i=0;i<count;i++){u8 v=((u8 *)address)[i];char bytes[3];const char *d="0123456789abcdef";
            bytes[0]=d[v>>4];bytes[1]=d[v&15];bytes[2]=0;output(bytes);}output("\r\n");return 0;
    }
    if(equal(op,"write")){
        u8 bytes[256];u32 n=0;while(b[n*2]){char pair[3]={b[n*2],b[n*2+1],0};u32 v;
            if(n==256 || !pair[1] || integer(pair,&v)){output("ERR bytes\r\n");return 0;}bytes[n++]=(u8)v;}
        if(!stopped || !ram(address,n)){output("ERR stopped RAM range required\r\n");return 0;}
        for(int i=0;i<16;i++)if(breaks[i].used && breaks[i].address>=address && breaks[i].address-address<n){output("ERR overlaps breakpoint\r\n");return 0;}
        for(u32 i=0;i<n;i++)((u8 *)address)[i]=bytes[i];output("OK\r\n");return 0;
    }
    if(equal(op,"break")){
        armed=1;
        if(!ram(address,1) || (stopped && address==stopped[14])){output("ERR breakpoint address\r\n");return 0;}
        for(int i=0;i<16;i++)if(breaks[i].used && breaks[i].address==address){output("OK exists\r\n");return 0;}
        for(int i=0;i<16;i++)if(!breaks[i].used){
            if(*(u8 *)address==0xCC){output("ERR original INT3\r\n");return 0;}
            breaks[i]=(breakpoint_t){address,*(u8 *)address,1};*(u8 *)address=0xCC;output("OK\r\n");return 0;}
        output("ERR breakpoint capacity\r\n");return 0;
    }
    if(equal(op,"clear")){
        for(int i=0;i<16;i++)if(breaks[i].used && breaks[i].address==address){
            *(u8 *)address=breaks[i].original;breaks[i].used=0;if(pending==i)pending=-1;output("OK\r\n");return 0;}
        output("ERR no breakpoint\r\n");return 0;
    }
    output("ERR command\r\n");return 0;
}
static int consume(u8 c)
{
    if(c=='\r')return 0;
    if(c=='\n'){command[used]=0;used=0;return execute();}
    if(c==8 || c==127){if(used)used--;return 0;}
    if(c<32 || c>126)return 0;
    if(used==sizeof(command)-1){used=0;output("ERR line capacity\r\n");return 0;}
    command[used++]=(char)c;return 0;
}
void ringdebug_init(void)
{ready=1;armed=halt_requested=stepping=continue_after_step=fatal=0;pending=-1;stopped=0;used=0;}
void ringdebug_poll(void)
{
    if(!ready)return;u8 byte;
    for(u32 budget=1024;budget && serial_read(SERIAL_DEBUG,&byte,1);budget--)consume(byte);
    if(halt_requested){halt_requested=0;__asm__ __volatile__("int3":::"memory");}
}
void ringdebug_irq(u32 *frame)
{
    if(!ready || stopped)return;u8 byte;
    for(u32 budget=1024;budget && serial_read(SERIAL_DEBUG,&byte,1);budget--)consume(byte);
    if(halt_requested){halt_requested=2;(void)ringdebug_exception(36,frame);}
}
int ringdebug_exception(u32 vector,u32 *frame)
{
    if(!ready)return 0;
    int owned=0;
    if(vector==36 && halt_requested==2){halt_requested=0;owned=1;}
    else if(vector==3 && (frame[15]&3)==0){
        owned=1;for(int i=0;i<16;i++)if(breaks[i].used && breaks[i].address==frame[14]-1){
            frame[14]--;*(u8 *)breaks[i].address=breaks[i].original;pending=i;break;}
    }else if(vector==1 && (stepping || pending>=0)){
        owned=1;
        if(pending>=0){if(breaks[pending].used)*(u8 *)breaks[pending].address=0xCC;pending=-1;}
        frame[16]&=~0x100u;
        if(continue_after_step && !stepping){continue_after_step=0;return 1;}
    }else if((frame[15]&3)==0 && armed){owned=1;fatal=1;}
    if(!owned)return 0;
    stopped=frame;output("STOP vector=");hex(vector);output(" eip=");hex(frame[14]);output("\r\n");registers();
    serial_stats_t info;
    if(serial_snapshot(SERIAL_DEBUG,&info) || !info.present){stopped=0;return fatal?0:1;}
    for(;;){int byte=serial_emergency_get(SERIAL_DEBUG);if(byte>=0 && consume((u8)byte))break;
        /* IF=0不能HLT等一个不会送达的UART IRQ；PAUSE只是空闲提示，
         * 这里的有意轮询仅存在于被外部调试机暂停的状态。 */
        __asm__ __volatile__("pause");}
    stopped=0;stepping=stepping!=0;return 1;
}
