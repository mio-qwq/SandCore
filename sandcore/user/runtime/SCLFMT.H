#ifndef SANDCORE_SCLFMT_H
#define SANDCORE_SCLFMT_H

/* mio：磁盘格式只公布字节偏移，不让宿主结构体填充或未对齐读取成为 ABI。
 * 本头与验证器保持 freestanding，SCLDR 可以在标准库装载之前使用它。 */
#define SCL_MAGIC "SCL1MIO"
#define SCL_HEADER_SIZE 128u
#define SCL_FORMAT_VERSION 1u
#define SCL_MACHINE_I386 3u
#define SCL_PAGE_SIZE 4096u
#define SCL_MAX_FILE_SIZE 16777216u
#define SCL_MAX_IMAGE_SIZE 67108864u
#define SCL_NO_ENTRY 0xffffffffu

#define SCL_H_HEADER_SIZE 8u
#define SCL_H_VERSION 12u
#define SCL_H_MACHINE 16u
#define SCL_H_FLAGS 20u
#define SCL_H_FILE_SIZE 24u
#define SCL_H_IMAGE_SIZE 28u
#define SCL_H_ABI_MAJOR 32u
#define SCL_H_ABI_MINOR 36u
#define SCL_H_LIBRARY_VERSION 40u
#define SCL_H_NAME 44u
#define SCL_H_SEGMENTS 48u
#define SCL_H_SEGMENT_COUNT 52u
#define SCL_H_EXPORTS 56u
#define SCL_H_EXPORT_COUNT 60u
#define SCL_H_IMPORTS 64u
#define SCL_H_IMPORT_COUNT 68u
#define SCL_H_RELOCS 72u
#define SCL_H_RELOC_COUNT 76u
#define SCL_H_DEPS 80u
#define SCL_H_DEP_COUNT 84u
#define SCL_H_STRINGS 88u
#define SCL_H_STRINGS_SIZE 92u
#define SCL_H_INIT 96u
#define SCL_H_FINI 100u
#define SCL_H_RESERVED 104u

#define SCL_SEGMENT_SIZE 24u
#define SCL_S_RVA 0u
#define SCL_S_MEMORY_SIZE 4u
#define SCL_S_FILE_OFFSET 8u
#define SCL_S_FILE_SIZE 12u
#define SCL_S_FLAGS 16u
#define SCL_S_ALIGNMENT 20u
#define SCL_SEG_R 1u
#define SCL_SEG_W 2u
#define SCL_SEG_X 4u

#define SCL_EXPORT_SIZE 20u
#define SCL_E_NAME 0u
#define SCL_E_RVA 4u
#define SCL_E_SIZE 8u
#define SCL_E_KIND 12u
#define SCL_E_RESERVED 16u
#define SCL_SYMBOL_FUNCTION 1u
#define SCL_SYMBOL_DATA 2u

#define SCL_IMPORT_SIZE 16u
#define SCL_I_NAME 0u
#define SCL_I_DEPENDENCY 4u
#define SCL_I_KIND 8u
#define SCL_I_SIZE 12u

#define SCL_RELOC_SIZE 20u
#define SCL_R_PATCH 0u
#define SCL_R_TYPE 4u
#define SCL_R_TARGET_KIND 8u
#define SCL_R_TARGET 12u
#define SCL_R_ADDEND 16u
#define SCL_RELOC_ABS32 1u
#define SCL_RELOC_PC32 2u
#define SCL_TARGET_INTERNAL 0u
#define SCL_TARGET_IMPORT 1u

#define SCL_DEPENDENCY_SIZE 20u
#define SCL_D_NAME 0u
#define SCL_D_ABI_MAJOR 4u
#define SCL_D_MIN_MINOR 8u
#define SCL_D_MIN_VERSION 12u
#define SCL_D_FLAGS 16u

#define SCL_E_FORMAT (-1)
#define SCL_E_RANGE (-2)
#define SCL_E_SYMBOL (-3)
#define SCL_E_VERSION (-4)

unsigned int scl_read32(const unsigned char *bytes);
int scl_validate(const void *bytes, unsigned int size);
/* 完整文件先验过并已映射后，可单独核验恰好到字符串末尾的元数据。
 * 此入口验证全部表/名字/段/导出/重定位边界，不读取缺失的磁盘payload。 */
int scl_validate_metadata(const void *bytes, unsigned int size);

#endif
