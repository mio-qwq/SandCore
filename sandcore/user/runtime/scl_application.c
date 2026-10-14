#include "SCLDR.H"
#include "SCLFMT.H"
#include "SCDYN.H"
#include "SCLPRIVATE.H"

#ifdef __UINTPTR_TYPE__
typedef __UINTPTR_TYPE__ ScAppPointer;
#else
typedef unsigned int ScAppPointer;
#endif
static void app_error(const char *what,int error)
{
    char text[96];unsigned int n=0;
    const char *prefix="SCL: ";while(*prefix)text[n++]=*prefix++;
    while(*what && n<76)text[n++]=*what++;
    text[n++]=' ';text[n++]='(';if(error<0){text[n++]='-';error=-error;}
    char digits[10];unsigned int count=0,value=(unsigned int)error;
    do{digits[count++]=(char)('0'+value%10);value/=10;}while(value);
    while(count)text[n++]=digits[--count];
    text[n++]=')';text[n++]='\n';
    ScRuntimePlatformCall(0x212,2,(int)text,(int)n,0,0,0);
}
static unsigned int app_word(const unsigned char *p,unsigned int offset)
{return scd_read32(p+offset);}
static void app_store(unsigned char *p,unsigned int value)
{for(unsigned int i=0;i<4;i++)p[i]=(unsigned char)(value>>(i*8));}
static int app_boot_valid(const ScDynamicBootInfo *info)
{
    if(!info || info->size!=32 || info->version!=1 ||
       info->root_base<0x50000000u || info->root_base>=0x54000000u ||
       !info->image_size || (info->image_size&4095u) ||
       info->allocation_bytes<info->image_size ||
       info->allocation_bytes>0x54000000u-info->root_base ||
       info->metadata_va!=info->root_base+info->image_size ||
       info->metadata_size<128 || info->metadata_size>info->allocation_bytes-info->image_size ||
       info->entry_va<info->root_base || info->entry_va-info->root_base>=info->image_size)return 0;
    return 1;
}
static int app_entry(void)
{
    const unsigned char *meta=application.descriptor;
    ScRuntime *runtime=application.runtime;
    unsigned int deps=app_word(meta,SCD_H_DEP_COUNT),imports=app_word(meta,SCD_H_IMPORT_COUNT);
    int error=0;
    if(deps){application.handles=application.platform.allocate(application.platform.context,deps*sizeof(ScModule));
        if(!application.handles)error=SC_SCL_MEMORY;}
    if(!error && imports){application.addresses=application.platform.allocate(application.platform.context,imports*sizeof(void *));
        if(!application.addresses)error=SC_SCL_MEMORY;}
    if(!error){
        const unsigned char *strings=meta+app_word(meta,SCD_H_STRINGS);
        for(unsigned int i=0;i<deps;i++){
            const unsigned char *d=meta+app_word(meta,SCD_H_DEPS)+i*20;
            error=ScRuntimeOpen(runtime,(const char *)strings+app_word(d,0),app_word(d,4),
                app_word(d,8),app_word(d,12),application.handles+i);
            if(error)break;
        }
        /* 在任何应用补丁之前先验证全部导入。缺符号/尺寸失败不会给
         * 一个已半回填的main机会；该进程随后EXIT回收借用根与所有页。 */
        for(unsigned int i=0;!error && i<imports;i++){
            const unsigned char *d=meta+app_word(meta,SCD_H_IMPORTS)+i*16;
            error=ScRuntimeResolve(runtime,application.handles[app_word(d,4)],(const char *)strings+app_word(d,0),
                app_word(d,8),app_word(d,12),application.addresses+i);
        }
        for(unsigned int i=0;!error && i<app_word(meta,SCD_H_RELOC_COUNT);i++){
            const unsigned char *d=meta+app_word(meta,SCD_H_RELOCS)+i*16;
            unsigned int patch=app_word(d,0),value=(unsigned int)(ScAppPointer)application.addresses[app_word(d,8)]+app_word(d,12);
            if(app_word(d,4)==2)value-=SCD_PROGRAM_BASE+patch;
            app_store((unsigned char *)(SCD_PROGRAM_BASE+patch),value);
        }
    }
    if(error){application.load_error=error;app_error("dependency or symbol",error);return 1;}
    application.main_linked=1;application.phase=APP_RUNNING;
    return ((int (*)(void))(SCD_PROGRAM_BASE+app_word(meta,SCD_H_MAIN)))();
}
static int app_finish(void *context)
{
    if(context!=&application || application.jump.armed || !application.runtime)app_termination_fail();
    ScRuntime *runtime=application.runtime;
    int explicit_exit=application.jump.reason==1;
    if(application.phase!=APP_LOADING && application.phase!=APP_RUNNING && application.phase!=APP_UNWINDING)app_termination_fail();
    application.finishing_on_stack=application.jump.termination_top!=0;
    if(runtime->busy){
        /* 逃逸只撤掉确实已弃的INIT事务帧；所有模块存储已完整。
         * 终止整个进程不再恢复Open checkpoint，不发中断init的fini，
         * initialized前缀与映像保留到prepare/fini全部完成。 */
        if(!explicit_exit || !application.exit_from_init || runtime->invoking_kind!=1
            || runtime->invoking!=application.interrupted_init)app_termination_fail();
        runtime->busy=0;runtime->invoking=0;runtime->invoking_kind=0;
    }
    if(!application.load_error && (explicit_exit || application.main_linked)){
        application.phase=APP_PREPARING;
        if(application.prepare && !application.prepare_called){
            application.prepare_called=1;application.prepare();
        }
    }
    application.phase=APP_FINALIZING;application.destroy_allowed=1;
    int error=ScRuntimeDestroy(runtime);application.destroy_allowed=0;
    if(error)app_termination_fail();
    application.runtime=0;application_runtime=0;
    if(application.addresses){application.platform.release(application.platform.context,application.addresses);application.addresses=0;}
    if(application.handles){application.platform.release(application.platform.context,application.handles);application.handles=0;}
    application.phase=APP_FINISHED;
    return (int)application.jump.status;
}
int ScApplicationStart(const void *descriptor,const ScDynamicBootInfo *info)
{
    if(application.started || application_runtime || !app_boot_valid(info))return 1;
    /* latch早于任何可回调工作。完成/失败后不再允许同ROOT实例第二
     * 次main，不能在已回填或已清理的图上重新借用同一continuation。 */
    application.started=1;application.phase=APP_LOADING;
    /* 只读本人已缓存bootinfo，不重复读取依赖或SCD。独立ROOT副本
     * 即使伪造一份自洽info，也不能把自己的entry冒充内核固定根。 */
    ScDynamicBootInfo verified;
    if(ScRuntimePlatformCall(SCD_BOOT_SYSCALL,(int)&verified,0,0,0,0,0)
        || verified.entry_va!=(ScU32)(ScAppPointer)ScApplicationStart){application.phase=APP_FAILED;return 1;}
    if(info->size!=verified.size || info->version!=verified.version || info->root_base!=verified.root_base
        || info->entry_va!=verified.entry_va || info->metadata_va!=verified.metadata_va
        || info->metadata_size!=verified.metadata_size || info->allocation_bytes!=verified.allocation_bytes
        || info->image_size!=verified.image_size){application.phase=APP_FAILED;return 1;}
    ScAppPointer pointer=(ScAppPointer)descriptor;
    if(pointer<SCD_PROGRAM_BASE || pointer-SCD_PROGRAM_BASE>SCD_MAX_PROGRAM_IMAGE-SCD_HEADER_SIZE){
        application.phase=APP_FAILED;return 1;
    }
    const unsigned char *meta=descriptor;
    unsigned int bytes=app_word(meta,SCD_H_METADATA_SIZE);
    int error=scd_validate(meta,bytes,(unsigned int)pointer-SCD_PROGRAM_BASE);
    if(error){app_error("invalid program imports",error);application.phase=APP_FAILED;return 1;}
    /* 原ROOT帧在整个run/finisher期间存活。package指向的目录不会
     * 因切终止栈失效，也不借main或SCCLIB的临时局部存储。 */
    char directory[65];const char *package=0;
    int count=ScRuntimePlatformCall(0x70,3,0,(int)directory,sizeof(directory),0,0);
    if(count>0 && count<(int)sizeof(directory)){
        int slash=-1;for(int i=0;i<count;i++)if(directory[i]=='/')slash=i;
        if(slash>=0){directory[slash?slash:1]=0;package=directory;}
    }
    error=ScRuntimeDefaultPlatform(&application.platform,package);
    if(error){app_error("invalid library directory",error);application.phase=APP_FAILED;return 1;}
    ScRuntime *runtime=ScRuntimeCreate(&application.platform);
    if(!runtime){app_error("runtime allocation",SC_SCL_MEMORY);application.phase=APP_FAILED;return 1;}
    error=ScRuntimeAdoptBootstrap(runtime,(void *)info->root_base,(const void *)info->metadata_va,info->metadata_size);
    if(error){app_error("invalid bootstrap runtime",error);(void)ScRuntimeDestroy(runtime);application.phase=APP_FAILED;return 1;}
    application.runtime=runtime;application_runtime=runtime;application.descriptor=meta;
    int result=__scl_root_run(&application.jump,app_entry,app_finish,&application);
    /* finisher不在被释放栈上返回。现在已经恢复原ROOT caller，库
     * 回调和FILE借用缓冲都结束，最后才归还128KiB独立终止页。 */
    if(application.termination_stack){
        application.platform.release(application.platform.context,application.termination_stack);
        application.termination_stack=0;application.jump.termination_top=0;
    }
    return result;
}
