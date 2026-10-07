#ifndef SANDCORE_CORE_IMAGE_H
#define SANDCORE_CORE_IMAGE_H
#include "io.h"

/* M10a1磁盘主核合同；版本/类型不能与旧壁纸SKM1混淆。
 * 固定地址在三环映像4..8MiB与永久模块24..25MiB之外，所有任务共享
 * supervisor映射。8MiB是映像物理地址预算，不能拿来限制任务数量。 */
#define CORE_IMAGE_BASE 0x00800000u
#define CORE_IMAGE_LIMIT 0x01000000u
#define CORE_IMAGE_MAX 0x00400000u
#define CORE_IMAGE_HEADER 128u
#define CORE_IMAGE_VERSION 2u
#define CORE_IMAGE_MAIN 1u
#define CORE_BOOT_ADDRESS 0x00008500u
#define CORE_BOOT_MAGIC 0x31424353u
#define CORE_BOOT_HANDOFF 0x4B424353u
#define CORE_BOOT_RECOVERY 1u
#define CORE_BOOT_REQUESTED 2u
#define CORE_LOADER_VERSION 1u

typedef struct {
    u8 magic[8];
    u32 version,header_bytes,kind,flags,number,abi_min,abi_max,load_base;
    u32 entry_offset,image_bytes,bss_offset,bss_bytes,memory_bytes,relocations,required_cpu,file_bytes;
    u8 sha256[32];
    u32 reserved[5],header_crc;
} core_image_t;

typedef struct {
    u32 magic,version,size,flags,disk_sectors,fs_version,loader_version,reserved0;
    u32 load_base,image_bytes,bss_offset,bss_bytes,memory_bytes,entry,image_crc,volume_generation;
    u8 sha256[32];
    char path[64];
    u32 e820_address,e820_count,reserved[21],crc;
} core_boot_t;
typedef char core_image_keeps_128[(sizeof(core_image_t)==128)?1:-1];
typedef char core_boot_keeps_256[(sizeof(core_boot_t)==256)?1:-1];

/* 入口复制固定低地址快照，再由memory_init后的第一批预留保护主核。 */
void core_boot_accept(u32 handoff,u32 address);
void core_boot_reserve(void);
int core_boot_info(core_boot_t *out);
#endif
