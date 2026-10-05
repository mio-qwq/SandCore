#ifndef SANDCORE_ATA_H
#define SANDCORE_ATA_H

#include "io.h"

/* ATA PIO 读写驱动 —— 保护模式里访问 IDE 数据盘的唯一通道。
 * BIOS int 13h 在保护模式失效；本接口也不能访问 if=floppy 的引导盘。
 * 每次调用的 buf 必须容纳 count*512B，文件字节边界由 fs 层中转。
 * 等待阶段检查 BSY/DRQ/ERR/DF 与超时，写入结束检查缓存 flush；
 * 0=完成，-1=设备错误/超时，不能把单纯“不忙”当成数据已就绪。 */

void ata_init(void);                       /* 探测主盘 */
u32 ata_sector_count(void);                 /* IDENTIFY的LBA28容量，无有效盘为0 */
int  ata_read_sectors(u32 lba, u32 count, void *buf);   /* 0=成功 -1=失败 */
int  ata_write_sectors(u32 lba, u32 count, const void *buf);

#endif /* SANDCORE_ATA_H */
