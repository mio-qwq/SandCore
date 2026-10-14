#ifndef SANDCORE_SCDYN_H
#define SANDCORE_SCDYN_H

/* mio：动态应用仍使用旧 SCX1 的固定地址容器。此头只定义初始化载荷
 * 尾部的 SCD1 字节合同，不用宿主结构体填充解释磁盘元数据。 */
#define SCD_MAGIC "SCD1MIO"
#define SCD_FORMAT_VERSION 1u
#define SCD_HEADER_SIZE 64u
#define SCD_PROGRAM_BASE 0x00400000u
#define SCD_MAX_PROGRAM_IMAGE 0x003d0000u
#define SCD_CRT_SIZE 64u

#define SCD_H_VERSION 8u
#define SCD_H_HEADER_SIZE 12u
#define SCD_H_METADATA_SIZE 16u
#define SCD_H_PROGRAM_LOAD 20u
#define SCD_H_PROGRAM_BSS 24u
#define SCD_H_MAIN 28u
#define SCD_H_DEPS 32u
#define SCD_H_DEP_COUNT 36u
#define SCD_H_IMPORTS 40u
#define SCD_H_IMPORT_COUNT 44u
#define SCD_H_RELOCS 48u
#define SCD_H_RELOC_COUNT 52u
#define SCD_H_STRINGS 56u
#define SCD_H_STRINGS_SIZE 60u

#define SCD_DEPENDENCY_SIZE 20u
#define SCD_D_NAME 0u
#define SCD_D_ABI_MAJOR 4u
#define SCD_D_MIN_MINOR 8u
#define SCD_D_MIN_VERSION 12u
#define SCD_D_FLAGS 16u
#define SCD_IMPORT_SIZE 16u
#define SCD_I_NAME 0u
#define SCD_I_DEPENDENCY 4u
#define SCD_I_KIND 8u
#define SCD_I_SIZE 12u
#define SCD_SYMBOL_FUNCTION 1u
#define SCD_SYMBOL_DATA 2u
#define SCD_RELOC_SIZE 16u
#define SCD_R_PATCH 0u
#define SCD_R_TYPE 4u
#define SCD_R_IMPORT 8u
#define SCD_R_ADDEND 12u
#define SCD_RELOC_ABS32 1u
#define SCD_RELOC_PC32 2u

#define SCD_E_FORMAT (-1)
#define SCD_E_RANGE (-2)
#define SCD_E_SYMBOL (-3)
#define SCD_E_VERSION (-4)

/* SYS 0x283：EBX=本人RW的32B bootinfo，ECX=本人SCX尾部SCD地址。
 * 内核先校验元数据与实际load/BSS，返回0或负错误；普通应用不
 * 包含软中断桥，也不把这些字段当作自行请求内核权限的公开接口。 */
#define SCD_BOOT_SYSCALL 0x283u
#define SCD_CRT_DESCRIPTOR_OFFSET 6u
#define SCD_CRT_ARGUMENT_OFFSET 32u
#define SCD_BOOT_SIZE 32u
#define SCD_BOOT_VERSION 1u
#define SCD_B_SIZE 0u
#define SCD_B_VERSION 4u
#define SCD_B_ROOT_BASE 8u
#define SCD_B_ENTRY 12u
#define SCD_B_METADATA 16u
#define SCD_B_METADATA_SIZE 20u
#define SCD_B_ALLOCATION 24u
#define SCD_B_IMAGE 28u
typedef struct {
    unsigned int size,version,root_base,entry_va,metadata_va,metadata_size;
    unsigned int allocation_bytes,image_size;
} ScDynamicBootInfo;

unsigned int scd_read32(const unsigned char *bytes);
/* metadata_rva 是相对固定 SCX 基址的偏移；调用者另核对头中的
 * load/BSS 与已装载 SCX 相等。返回0或上述负错误，不执行机器码。 */
int scd_validate(const void *metadata,unsigned int metadata_size,unsigned int metadata_rva);

#endif
