#include "SCLDR.H"

extern void ProbeWrite(const char *,unsigned int);
extern unsigned int ProbeFreePages(void);
extern int ProbeArgs(char *,int);
extern int ProbeReadonlyOutput(const char *);
extern int ProbeMappings(void);
extern void ProbeFaultExit(void);
static int failures;
static unsigned int length(const char *s)
{unsigned int n=0;while(s[n])++n;return n;}
static int equal(const char *a,const char *b)
{while(*a && *a==*b){++a;++b;}return *a==*b;}
static void text(const char *s)
{ProbeWrite(s,length(s));}
static void number(unsigned int value)
{
    char bytes[11];unsigned int n=0;
    do{bytes[n++]=(char)('0'+value%10);value/=10;}while(value);
    for(unsigned int i=0;i<n/2;++i){char c=bytes[i];bytes[i]=bytes[n-i-1];bytes[n-i-1]=c;}
    ProbeWrite(bytes,n);
}
static void check(const char *name,int okay)
{text(okay?"PASS ":"FAIL ");text(name);text("\n");if(!okay)++failures;}
static void *symbol(ScRuntime *r,ScModule m,const char *name,unsigned int kind)
{
    void *address=0;
    int error=ScRuntimeSymbol(r,m,name,kind,&address);
    check(name,!error && address!=0);
    return address;
}
static int stats(ScRuntime *r,ScRuntimeStats *out)
{out->size=sizeof(*out);return ScRuntimeQuery(r,out);}
int main(void)
{
    char args[64];ProbeArgs(args,sizeof(args));
    check("module-mapping-boundaries",ProbeMappings()==0);
    unsigned int before=ProbeFreePages();
    ScRuntimePlatform platform;
    check("platform",ScRuntimeDefaultPlatform(&platform,"/HOME/M10A2")==0);
    ScRuntime *r=ScRuntimeCreate(&platform);
    check("runtime-create",r!=0);if(!r)return 1;
    ScModule upper=0,base=0,duplicate=0;
    check("load-upper-with-base",ScRuntimeOpen(r,"UPPER.SCL",1,0,1,&upper)==0);
    if(!upper){ScRuntimeDestroy(r);return 1;}
    ScRuntimeStats state;
    check("two-real-modules",!stats(r,&state) && state.modules==2 && state.loads==2);
    check("reuse-upper",!ScRuntimeOpen(r,"UPPER.SCL",1,0,1,&duplicate) && duplicate==upper);
    check("close-one-reference",ScRuntimeClose(r,duplicate)==SC_SCL_RESIDENT);
    check("open-base",!ScRuntimeOpen(r,"BASE.SCL",1,0,1,&base));
    ScModule sentinel=0x11223344;
    check("reject-ABI-and-preserve-output",ScRuntimeOpen(r,"BASE.SCL",2,0,1,&sentinel)==SC_SCL_VERSION
          && sentinel==0x11223344);
    int (*compute)(int)=symbol(r,upper,"upper_compute",1);
    int (*pointer_call)(int)=symbol(r,upper,"upper_call_pointer",1);
    int *base_value=symbol(r,base,"base_value",2);
    int *base_init_count=symbol(r,base,"base_init_count",2);
    int *upper_init_count=symbol(r,upper,"upper_init_count",2);
    const char *message=symbol(r,upper,"upper_message",2);
    int **base_pointer=symbol(r,upper,"upper_base_pointer",2);
    if(failures){ScRuntimeDestroy(r);return 1;}
    check("cross-library-machine-code",compute(10)==144 && pointer_call(10)==22);
    check("external-data-relocation",*base_pointer==base_value && *base_value==7);
    check("init-once",*base_init_count==1 && *upper_init_count==1);
    check("library-rodata",equal(message,"SCL upper"));
    text("RODATA ");ProbeWrite(message,9);text("\n");
    check("readonly-copyout-rejected",ProbeReadonlyOutput(message)==0);
    *base_value=20;
    check("writable-private-data",compute(10)==170 && pointer_call(10)==35);
    void *missing=(void *)0x11223344;
    check("missing-symbol-preserves-output",ScRuntimeSymbol(r,upper,"does_not_exist",1,&missing)==SC_SCL_SYMBOL
          && missing==(void *)0x11223344);
    ScRuntime *other=ScRuntimeCreate(&platform);ScModule other_upper=0;
    check("independent-runtime-load",other && !ScRuntimeOpen(other,"UPPER.SCL",1,0,1,&other_upper));
    if(other_upper){
        int (*other_compute)(int)=symbol(other,other_upper,"upper_compute",1);
        check("different-actual-load-address",(void *)other_compute!=(void *)compute);
        check("independent-data-and-relocations",other_compute && other_compute(10)==144 && compute(10)==170);
        text("ADDRESSES ");number((unsigned int)compute);text(" ");number((unsigned int)other_compute);text("\n");
    }
    if(other)ScRuntimeDestroy(other);
    if(equal(args,"orphan")){
        text("ORPHAN modules-live-at-exit\n");
        return failures?1:0;
    }
    if(equal(args,"fault")){
        text("FAULT modules-live-before-page-fault\n");
        ProbeFaultExit();return 1;
    }
    check("pin-close-base",ScRuntimeClose(r,base)==SC_SCL_RESIDENT);
    check("pin-close-upper",ScRuntimeClose(r,upper)==SC_SCL_RESIDENT);
    check("closed-handle-rejected",ScRuntimeClose(r,upper)==SC_SCL_HANDLE);
    check("escaped-function-still-valid",compute(10)==170);
    check("destroy-runtime",ScRuntimeDestroy(r)==0);
    check("exact-explicit-page-recovery",ProbeFreePages()==before);
    /* 未查询符号时，Close 必须真正卸载而不是所有模块永久驻留。 */
    r=ScRuntimeCreate(&platform);upper=0;
    check("reload-unpinned",r && !ScRuntimeOpen(r,"UPPER.SCL",1,0,1,&upper));
    if(upper){check("actual-unload",ScRuntimeClose(r,upper)==0);
        check("module-count-zero",!stats(r,&state) && state.modules==0);}
    if(r)ScRuntimeDestroy(r);
    check("exact-unload-page-recovery",ProbeFreePages()==before);
    text(failures?"SCL_BOOTSTRAP_FAIL\n":"SCL_BOOTSTRAP_PASS\n");
    return failures?1:0;
}
