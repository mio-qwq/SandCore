#include "scl_boot.h"
#include "task.h"
#include "task_store.h"
#include "paging.h"
#include "fs.h"
#include "../user/runtime/SCLFMT.H"
#include "../user/runtime/SCDYN.H"

#define SCL_BOOT_PATH "/SYS/LIB/SCLDR.SCL"
#define SCL_BOOT_NAME "SCLDR.SCL"
#define SCL_BOOT_ENTRY "ScApplicationStart"
#define SCL_BOOT_ABI 1u
#define SCL_BOOT_NOT_FOUND (-5)
#define SCL_BOOT_MEMORY (-6)
#define SCL_BOOT_BUSY (-10)
#define SCL_BOOT_IO (-11)

typedef char scl_boot_keeps_shared_contract[(SCL_BOOT_INFO_SIZE==SCD_BOOT_SIZE
    && SCL_BOOT_INFO_VERSION==SCD_BOOT_VERSION && sizeof(ScDynamicBootInfo)==32
    && SYS_SCLBOOT==SCD_BOOT_SYSCALL)?1:-1];

/* 与三环共享完整格式校验器，不能给系统库另开一个较宽松的磁盘格式。
 * 校验结束后才读取任何表项；所有名字和入口也来自这个已验证快照。 */
static u32 word(const u8 *bytes,u32 offset)
{return scl_read32(bytes+offset);}

static int compare(const char *a,const char *b)
{
    while(*a && *a==*b){a++;b++;}
    return (u8)*a<(u8)*b?-1:*a!=*b;
}

static int root_entry(const u8 *file,u32 *entry)
{
    const char *strings=(const char *)file+word(file,SCL_H_STRINGS);
    if(compare(strings+word(file,SCL_H_NAME),SCL_BOOT_NAME))return SCL_E_SYMBOL;
    if(word(file,SCL_H_ABI_MAJOR)!=SCL_BOOT_ABI)return SCL_E_VERSION;
    /* 根必须独立引导；内核不能为了装载它递归读应用依赖，也不能
     * 执行任何用户init/fini机器码。完整链接算法留在根的三环入口。 */
    if(word(file,SCL_H_IMPORT_COUNT) || word(file,SCL_H_DEP_COUNT)
        || word(file,SCL_H_INIT)!=SCL_NO_ENTRY || word(file,SCL_H_FINI)!=SCL_NO_ENTRY)
        return SCL_E_FORMAT;
    u32 first=0,count=word(file,SCL_H_EXPORT_COUNT),exports=word(file,SCL_H_EXPORTS);
    while(count){
        u32 half=count/2,index=first+half;
        const u8 *item=file+exports+index*SCL_EXPORT_SIZE;
        int order=compare(strings+word(item,SCL_E_NAME),SCL_BOOT_ENTRY);
        if(order<0){first=index+1;count-=half+1;}
        else if(order>0)count=half;
        else{
            if(word(item,SCL_E_KIND)!=SCL_SYMBOL_FUNCTION)return SCL_E_SYMBOL;
            *entry=word(item,SCL_E_RVA);return 0;
        }
    }
    return SCL_E_SYMBOL;
}

void scl_boot_reset(int pid)
{
    if(!task_owned(pid))return;
    task_record_t *record=task_record(pid);
    record->scl_root_base=record->scl_root_entry=record->scl_root_metadata=0;
    record->scl_root_metadata_size=record->scl_root_bytes=record->scl_root_image_size=0;
}

static void root_info(const task_record_t *record,u32 out[8])
{
    out[0]=SCL_BOOT_INFO_SIZE;out[1]=SCL_BOOT_INFO_VERSION;
    out[2]=record->scl_root_base;out[3]=record->scl_root_entry;
    out[4]=record->scl_root_metadata;out[5]=record->scl_root_metadata_size;
    out[6]=record->scl_root_bytes;out[7]=record->scl_root_image_size;
}

static int descriptor_check(const task_record_t *record,u32 address,u32 output)
{
    if(!address)return 0;
    u32 pd=record->task.pd;
    /* CRT明确传入自身SCX载荷内的描述符。先验完整头，再取正文
     * 长度并验完整元数据；不能先读恶意指针里的长度或跨到图形堆。
     * 当前陷入沿用本人CR3且IF=0，验证后这份用户快照不会被抢占改写。 */
    if(address<USER_BASE || address>=USER_BASE+SCD_MAX_PROGRAM_IMAGE
        || !paging_user_range(pd,address,SCD_HEADER_SIZE))return SCD_E_RANGE;
    const u8 *metadata=(const u8 *)address;
    u32 size=scd_read32(metadata+SCD_H_METADATA_SIZE);
    if(size<SCD_HEADER_SIZE || size>USER_BASE+SCD_MAX_PROGRAM_IMAGE-address
        || !paging_user_range(pd,address,size))return SCD_E_RANGE;
    int error=scd_validate(metadata,size,address-USER_BASE);if(error)return error;
    /* 成功输出将被CRT立即传回根入口；若它覆盖描述符，内核会在
     * 返回成功的同时毁掉自己的已验输入。两个区间都已整区验过，
     * 端点相加不回绕，只拒绝重叠，不禁止一般只读描述符输入。 */
    if(output<address+size && output+SCL_BOOT_INFO_SIZE>address)return SCD_E_RANGE;
    u32 load=scd_read32(metadata+SCD_H_PROGRAM_LOAD),bss=scd_read32(metadata+SCD_H_PROGRAM_BSS);
    if(load!=record->scx_load_size || bss!=record->scx_bss_size
        || !paging_user_range(pd,USER_BASE,load+bss))return SCD_E_RANGE;
    return 0;
}

static int root_copy(u32 pd,u32 base,const u8 *file,u32 metadata_size,u32 allocation_bytes)
{
    for(u32 i=0;i<word(file,SCL_H_SEGMENT_COUNT);i++){
        const u8 *item=file+word(file,SCL_H_SEGMENTS)+i*SCL_SEGMENT_SIZE;
        u32 bytes=word(item,SCL_S_FILE_SIZE);
        if(bytes && paging_user_copy(pd,base+word(item,SCL_S_RVA),
            (void *)(file+word(item,SCL_S_FILE_OFFSET)),bytes,1))return SCL_BOOT_IO;
    }
    for(u32 i=0;i<word(file,SCL_H_RELOC_COUNT);i++){
        const u8 *item=file+word(file,SCL_H_RELOCS)+i*SCL_RELOC_SIZE;
        u32 patch=word(item,SCL_R_PATCH);
        /* 无导入的根只存在内部目标。用u32运算精确保留SCL的模2^32
         * 加数语义，不能把有符号相加溢出交给C未定义行为。PC补丁
         * 地址是字节地址P；ELF的-4已在addend中，不再额外减一次。 */
        u32 value=base+word(item,SCL_R_TARGET)+word(item,SCL_R_ADDEND);
        if(word(item,SCL_R_TYPE)==SCL_RELOC_PC32)value-=base+patch;
        u8 bytes[4];for(u32 k=0;k<4;k++)bytes[k]=(u8)(value>>(k*8));
        if(paging_user_copy(pd,base+patch,bytes,4,1))return SCL_BOOT_IO;
    }
    if(paging_user_copy(pd,base+word(file,SCL_H_IMAGE_SIZE),(void *)file,metadata_size,1))
        return SCL_BOOT_IO;
    /* 一块分配同时拥有映像和页对齐元数据。完整块先RO，只有段表
     * 明确标W的数据/BSS再开放RW；映像间隙和元数据尾填充仍RO。
     * 分配器已整页清零，不再逐字节重复清BSS或分配文件私有副本。 */
    if(paging_module_protect(pd,base,allocation_bytes,SCL_SEG_R))return SCL_BOOT_IO;
    for(u32 i=0;i<word(file,SCL_H_SEGMENT_COUNT);i++){
        const u8 *item=file+word(file,SCL_H_SEGMENTS)+i*SCL_SEGMENT_SIZE;
        if((word(item,SCL_S_FLAGS)&SCL_SEG_W) && paging_module_protect(pd,
            base+word(item,SCL_S_RVA),word(item,SCL_S_MEMORY_SIZE),SCL_SEG_R|SCL_SEG_W))
            return SCL_BOOT_IO;
    }
    return 0;
}

int scl_boot_map(u32 user_output,u32 user_descriptor)
{
    int pid=task_pid();u32 flags;
    __asm__ __volatile__("pushfl;popl %0":"=r"(flags));
    if(flags&0x200u)return SCL_BOOT_BUSY;
    if(pid<=0 || !task_owned(pid) || TASK(pid).state!=1
        || !paging_user_write_range(TASK(pid).pd,user_output,SCL_BOOT_INFO_SIZE))return SCL_E_RANGE;
    u32 pd=TASK(pid).pd;task_record_t *record=task_record(pid);u32 info[8];
    int error=descriptor_check(record,user_descriptor,user_output);if(error)return error;
    if(record->scl_root_base){
        /* 同一进程重复引导复用原映像及磁盘快照。MODULEFREE完成后
         * 同步撤缓存；这里仍核对真实块，不能向调用者交付陈旧虚址。 */
        if(paging_module_size(pd,record->scl_root_base)!=record->scl_root_bytes
            || !paging_user_range(pd,record->scl_root_base,record->scl_root_bytes))return SCL_BOOT_IO;
        root_info(record,info);
        return paging_user_copy(pd,user_output,info,sizeof(info),1)?SCL_BOOT_IO:0;
    }
    /* 凭据已属于调用者，不使用启动期间的SYSTEM launch特例。
     * 固定系统路径不经过CWD/PATH或用户包目录，也没有同名回退。 */
    u32 stat[2];
    if(!fs_access(pid,SCL_BOOT_PATH,FS_ACCESS_READ) || fs_stat(SCL_BOOT_PATH,stat)
        || stat[0]!=1)return SCL_BOOT_NOT_FOUND;
    if(stat[1]<SCL_HEADER_SIZE || stat[1]>TASK_LOADER_STAGE_BYTES)return SCL_E_RANGE;
    u8 *file=task_loader_stage(stat[1]);if(!file)return SCL_BOOT_MEMORY;
    if(fs_read(SCL_BOOT_PATH,file,stat[1])!=(int)stat[1])return SCL_BOOT_IO;
    error=scl_validate(file,stat[1]);if(error)return error;
    u32 entry=0;error=root_entry(file,&entry);if(error)return error;
    u32 image_size=word(file,SCL_H_IMAGE_SIZE);
    u32 metadata_size=word(file,SCL_H_STRINGS)+word(file,SCL_H_STRINGS_SIZE);
    u32 metadata_pages=(metadata_size+PAGE_SIZE-1u)&~(PAGE_SIZE-1u);
    if(metadata_pages>USER_MODULE_TOP-USER_MODULE_BASE-image_size)return SCL_E_RANGE;
    u32 allocation_bytes=image_size+metadata_pages;
    u32 base=paging_module_alloc(pd,allocation_bytes);if(!base)return SCL_BOOT_MEMORY;
    error=root_copy(pd,base,file,metadata_size,allocation_bytes);
    if(!error){
        info[0]=SCL_BOOT_INFO_SIZE;info[1]=SCL_BOOT_INFO_VERSION;info[2]=base;info[3]=base+entry;
        info[4]=base+image_size;info[5]=metadata_size;info[6]=allocation_bytes;info[7]=image_size;
        if(paging_user_copy(pd,user_output,info,sizeof(info),1))error=SCL_BOOT_IO;
    }
    if(error){(void)paging_module_free(pd,base);return error;}
    /* 输出、复制与页保护都成功才提交本人缓存。任何前置失败都不
     * 改用户输出/已有模块；内核从不调用返回的ScApplicationStart。 */
    record->scl_root_base=base;record->scl_root_entry=base+entry;
    record->scl_root_metadata=base+image_size;record->scl_root_metadata_size=metadata_size;
    record->scl_root_bytes=allocation_bytes;record->scl_root_image_size=image_size;
    return 0;
}
