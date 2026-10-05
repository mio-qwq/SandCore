/* =====================================================================
 *  SandCore ATA PIO 读盘驱动 (kernel/ata.c)
 *  ---------------------------------------------------------------------
 *  引导器时代的 int 13h 在保护模式下失效, 读盘只能自己来:
 *  走主 IDE 通道 (0x1F0-0x1F7) 的 PIO 模式, 逐扇读回。
 *  v1 只实现读, 不写 (SandFS 只读, 写盘留给 M6)。
 * ===================================================================== */
#include "io.h"
#include "ata.h"

#define ATA_DATA   0x1F0
#define ATA_ERR    0x1F1
#define ATA_COUNT  0x1F2
#define ATA_LBA_LO 0x1F3
#define ATA_LBA_MI 0x1F4
#define ATA_LBA_HI 0x1F5
#define ATA_DRIVE  0x1F6
#define ATA_CMD    0x1F7
#define ATA_STAT   0x1F7

#define STAT_BSY 0x80
#define STAT_DRQ 0x08
#define STAT_ERR 0x01

#define CMD_READ  0x20
#define CMD_WRITE 0x30
#define CMD_FLUSH 0xE7

static int have_disk;

void ata_init(void)
{
    /* 选主盘, 发 IDENTIFY 探测; 无盘/超时则降级为无盘模式 */
    outb(ATA_DRIVE, 0xE0);            /* 主盘 + LBA 模式位 */
    io_wait();
    outb(ATA_CMD, 0xEC);              /* IDENTIFY DEVICE */
    u32 t = 200000;
    while (t--) {
        u8 st = inb(ATA_STAT);
        if (st == 0) { have_disk = 0; return; }   /* 无设备 */
        if (!(st & STAT_BSY) && (st & STAT_DRQ)) {
            have_disk = 1;
            for (u32 i = 0; i < 256; i++)  (void)inw(ATA_DATA);  /* 丢弃识别数据 */
            return;
        }
    }
    have_disk = 0;
}

int ata_write_sectors(u32 lba, u32 count, const void *buf)
{
    if (!have_disk || count == 0)
        return -1;
    const u16 *p = (const u16 *)buf;

    for (u32 s = 0; s < count; s++) {
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        io_wait();
        outb(ATA_COUNT, 1);
        outb(ATA_LBA_LO, (u8)(lba & 0xFF));
        outb(ATA_LBA_MI, (u8)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
        outb(ATA_CMD, CMD_WRITE);

        u32 t = 2000000;
        while (t--) {
            u8 st = inb(ATA_STAT);
            if (!(st & STAT_BSY) && (st & STAT_DRQ))
                break;
        }
        for (u32 i = 0; i < 256; i++)
            outw(ATA_DATA, p[i]);          /* 256 字 = 512 字节 */
        outb(ATA_CMD, CMD_FLUSH);          /* 刷缓存保证落盘 */
        t = 2000000;
        while (t--) {
            if (!(inb(ATA_STAT) & STAT_BSY))
                break;
        }
        p += 256;
        lba++;
    }
    return 0;
}

int ata_read_sectors(u32 lba, u32 count, void *buf)
{
    if (!have_disk || count == 0)
        return -1;
    u16 *p = (u16 *)buf;

    for (u32 s = 0; s < count; s++) {
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        io_wait();
        outb(ATA_COUNT, 1);                        /* 一次一扇, 稳 */
        outb(ATA_LBA_LO, (u8)(lba & 0xFF));
        outb(ATA_LBA_MI, (u8)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
        outb(ATA_CMD, CMD_READ);

        u32 t = 2000000;                           /* 等 BSY 清零 */
        while (t--) {
            u8 st = inb(ATA_STAT);
            if (!(st & STAT_BSY)) {
                if (st & STAT_ERR) return -1;
                if (st & STAT_DRQ) break;
            }
        }
        if (inb(ATA_STAT) & STAT_BSY)
            return -1;                             /* 超时 */

        for (u32 i = 0; i < 256; i++)
            p[i] = inw(ATA_DATA);                  /* 256 字 = 512 字节 */
        p += 256;
        lba++;
    }
    return 0;
}
