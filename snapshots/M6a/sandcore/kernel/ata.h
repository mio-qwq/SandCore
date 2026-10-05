#ifndef SANDCORE_ATA_H
#define SANDCORE_ATA_H

#include "io.h"

/* ATA PIO 读盘驱动 (M5) —— 保护模式里读磁盘的唯一通道
 * (BIOS int 13h 在保护模式失效, boot 契约里写明了) */

void ata_init(void);                       /* 探测主盘 */
int  ata_read_sectors(u32 lba, u32 count, void *buf);   /* 0=成功 -1=失败 */
int  ata_write_sectors(u32 lba, u32 count, const void *buf);

#endif /* SANDCORE_ATA_H */
