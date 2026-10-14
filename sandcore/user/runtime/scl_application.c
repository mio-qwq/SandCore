#include "SCLDR.H"
#include "SCLFMT.H"
#include "SCDYN.H"
#include "SCLPRIVATE.H"

#ifdef __UINTPTR_TYPE__
typedef __UINTPTR_TYPE__ ScAppPointer;
#else
typedef unsigned int ScAppPointer;
#endif
static ScRuntime *application_runtime;
ScRuntime *ScApplicationRuntime(void) { return application_runtime; }

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
int ScApplicationStart(const void *descriptor,const ScDynamicBootInfo *info)
{
    if(application_runtime || !app_boot_valid(info))return 1;
    ScAppPointer pointer=(ScAppPointer)descriptor;
    if(pointer<SCD_PROGRAM_BASE || pointer-SCD_PROGRAM_BASE>SCD_MAX_PROGRAM_IMAGE-SCD_HEADER_SIZE)
        return 1;
    const unsigned char *meta=descriptor;
    unsigned int bytes=app_word(meta,SCD_H_METADATA_SIZE);
    int error=scd_validate(meta,bytes,(unsigned int)pointer-SCD_PROGRAM_BASE);
    if(error){app_error("invalid program imports",error);return 1;}
    /* 旧USERQUERY的exe快照有根斜杠。目录只在此调用栈内借给平台，
     * main及所有fini结束前该帧仍存在；路径实际长度在read_file相加校验。 */
    char directory[65];const char *package=0;
    int count=ScRuntimePlatformCall(0x70,3,0,(int)directory,sizeof(directory),0,0);
    if(count>0 && count<(int)sizeof(directory)){
        int slash=-1;for(int i=0;i<count;i++)if(directory[i]=='/')slash=i;
        if(slash>=0){directory[slash?slash:1]=0;package=directory;}
    }
    ScRuntimePlatform platform;
    error=ScRuntimeDefaultPlatform(&platform,package);
    if(error){app_error("invalid library directory",error);return 1;}
    ScRuntime *runtime=ScRuntimeCreate(&platform);
    if(!runtime){app_error("runtime allocation",SC_SCL_MEMORY);return 1;}
    error=ScRuntimeAdoptBootstrap(runtime,(void *)info->root_base,
        (const void *)info->metadata_va,info->metadata_size);
    /* 依赖init/fini也必须看见本次应用的唯一运行时。先提交上下文再
     * 装依赖，防止init重入另起一棵图；busy仍禁止回调递归装卸。 */
    if(!error)application_runtime=runtime;
    unsigned int deps=app_word(meta,SCD_H_DEP_COUNT),imports=app_word(meta,SCD_H_IMPORT_COUNT);
    ScModule *handles=0;void **addresses=0;
    if(!error && deps){handles=platform.allocate(platform.context,deps*sizeof(ScModule));
        if(!handles)error=SC_SCL_MEMORY;}
    if(!error && imports){addresses=platform.allocate(platform.context,imports*sizeof(void *));
        if(!addresses)error=SC_SCL_MEMORY;}
    if(!error){
        const unsigned char *strings=meta+app_word(meta,SCD_H_STRINGS);
        for(unsigned int i=0;i<deps;i++){
            const unsigned char *d=meta+app_word(meta,SCD_H_DEPS)+i*20;
            error=ScRuntimeOpen(runtime,(const char *)strings+app_word(d,0),app_word(d,4),
                app_word(d,8),app_word(d,12),handles+i);
            if(error)break;
        }
        /* 在任何应用补丁之前先验证全部导入。缺符号/尺寸失败不会给
         * 一个已半回填的main机会；该进程随后EXIT回收借用根与所有页。 */
        for(unsigned int i=0;!error && i<imports;i++){
            const unsigned char *d=meta+app_word(meta,SCD_H_IMPORTS)+i*16;
            error=ScRuntimeResolve(runtime,handles[app_word(d,4)],(const char *)strings+app_word(d,0),
                app_word(d,8),app_word(d,12),addresses+i);
        }
        for(unsigned int i=0;!error && i<app_word(meta,SCD_H_RELOC_COUNT);i++){
            const unsigned char *d=meta+app_word(meta,SCD_H_RELOCS)+i*16;
            unsigned int patch=app_word(d,0),value=(unsigned int)(ScAppPointer)addresses[app_word(d,8)]+app_word(d,12);
            if(app_word(d,4)==2)value-=SCD_PROGRAM_BASE+patch;
            app_store((unsigned char *)(SCD_PROGRAM_BASE+patch),value);
        }
    }
    int result=1;
    if(error)app_error("dependency or symbol",error);
    else{
        result=((int (*)(void))(SCD_PROGRAM_BASE+app_word(meta,SCD_H_MAIN)))();
    }
    ScRuntimeDestroy(runtime);application_runtime=0;
    if(addresses)platform.release(platform.context,addresses);
    if(handles)platform.release(platform.context,handles);
    return result;
}
