/* =====================================================================
 *  SandCore ATA PIO 读盘驱动 (kernel/ata.c)
 *  ---------------------------------------------------------------------
 *  引导器时代的 int 13h 在保护模式下失效, 读盘只能自己来:
 *  走主 IDE 通道 (0x1F0-0x1F7) 的 PIO 模式, 逐扇读回。
 *  M6 同时支持读写；写后 FLUSH CACHE，超时/ERR/DF 都报告失败。
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
static u32 disk_sectors;
u32 ata_sector_count(void){return disk_sectors;}

#ifndef BOOT_READ_ONLY
volatile ata_diagnostic_t ata_diagnostic;
static void trace_command(u32 command,u32 lba)
{
    ata_diagnostic.active_command=command;ata_diagnostic.active_lba=lba;
    if(command==CMD_READ)ata_diagnostic.reads++;
    else if(command==CMD_WRITE)ata_diagnostic.writes++;
    else if(command==CMD_FLUSH)ata_diagnostic.flushes++;
}
static int trace_failure(u32 need_data,u32 status,u32 tries,int timeout)
{
    ata_diagnostic.failures++;if(timeout)ata_diagnostic.timeouts++;
    ata_diagnostic.failed_command=ata_diagnostic.active_command;
    ata_diagnostic.failed_lba=ata_diagnostic.active_lba;
    ata_diagnostic.failed_need_data=need_data;ata_diagnostic.failed_status=status;
    ata_diagnostic.failed_error=inb(ATA_ERR);ata_diagnostic.failed_tries=tries;
    return -1;
}
#else
/* 最小启动器保持原指令与BSS；阶段诊断只归磁盘主核。 */
#define trace_command(command,lba) ((void)0)
#define trace_failure(need_data,status,tries,timeout) (-1)
#endif

static int wait_ready(int need_data)
{
    /* BSY 清零不等于读写数据已经就绪：DRQ 必须按命令阶段额外检查。
     * 原实现超时后只看 BSY，会把“无数据但不忙”的状态当作成功，
     * 上层于是可能保存坏字节却提示 saved。循环预算用整数计数，
     * 不依赖已被中断门暂停的 PIT；耗尽预算直接失败，不能继续 inw/outw。 */
    u8 st=0;
    for(u32 tries=0;tries<2000000;tries++) {
        st=inb(ATA_STAT);
        if(st==0 || st==0xFF) return trace_failure((u32)need_data,st,tries+1,0);
        if(st&STAT_BSY) continue;
        if(st&(STAT_ERR|0x20)) return trace_failure((u32)need_data,st,tries+1,0);
        if(!need_data || (st&STAT_DRQ)) return 0;
    }
    return trace_failure((u32)need_data,st,2000000,1);
}

void ata_init(void)
{
    have_disk=0;disk_sectors=0;
#ifndef BOOT_READ_ONLY
    for(u32 i=0;i<sizeof(ata_diagnostic)/sizeof(u32);i++)((volatile u32 *)&ata_diagnostic)[i]=0;
    ata_diagnostic.version=1;
#endif
    /* 选主盘, 发 IDENTIFY 探测; 无盘/超时则降级为无盘模式 */
    outb(ATA_DRIVE, 0xE0);            /* 主盘 + LBA 模式位 */
    io_wait();
    outb(ATA_CMD, 0xEC);              /* IDENTIFY DEVICE */
    u32 t = 200000;
    while (t--) {
        u8 st = inb(ATA_STAT);
        if (st == 0) { have_disk = 0; return; }   /* 无设备 */
        if (!(st & STAT_BSY) && (st & STAT_DRQ)) {
            /* IDENTIFY第60/61字给出LBA28扇区数（与QEMU core.c一致）。
             * 不能只扩大SandFS常量：8MB旧盘仍只有自己的真实容量，
             * 写越尾之前必须在驱动层拒绝，所有上层格式都受同一约束。 */
            for (u32 i=0;i<256;i++){
                u32 word=inw(ATA_DATA);
                if(i==60)disk_sectors=word;
                if(i==61)disk_sectors|=word<<16;
            }
            if(!disk_sectors || disk_sectors>0x0FFFFFFFu){disk_sectors=0;return;}
            have_disk = 1;
            return;
        }
    }
    have_disk = 0;
}

#ifndef BOOT_READ_ONLY
int ata_write_sectors(u32 lba, u32 count, const void *buf)
{
    if (!have_disk || count == 0 || lba>=disk_sectors || count>disk_sectors-lba)
        return -1;
    const u16 *p = (const u16 *)buf;

    for (u32 s = 0; s < count; s++) {
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        io_wait();
        outb(ATA_COUNT, 1);
        outb(ATA_LBA_LO, (u8)(lba & 0xFF));
        outb(ATA_LBA_MI, (u8)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
        trace_command(CMD_WRITE,lba);outb(ATA_CMD, CMD_WRITE);

        if(wait_ready(1)!=0) return -1;
        for (u32 i = 0; i < 256; i++)
            outw(ATA_DATA, p[i]);          /* 256 字 = 512 字节 */
        if(wait_ready(0)!=0) return -1;
        p += 256;
        lba++;
    }
    /* 一个调用的全部PIO数据阶段均完成后，只刷一次设备缓存。
     * 256MiB盘的一个目录bank有384扇区；逐扇FLUSH把一次元数据
     * 提交放大成384次持久化屏障，常驻服务因此长时间占住任务0。
     * 成功返回仍保证本调用全部字节落盘；任何数据/最终flush错误
     * 都返回-1，SandFS的备用bank与最后超级块提交顺序保持。 */
    trace_command(CMD_FLUSH,lba-1);outb(ATA_CMD,CMD_FLUSH);
    return wait_ready(0);
}

int ata_flush(void)
{if(!have_disk || wait_ready(0))return -1;trace_command(CMD_FLUSH,0);outb(ATA_CMD,CMD_FLUSH);return wait_ready(0);}
#endif

int ata_read_sectors(u32 lba, u32 count, void *buf)
{
    if (!have_disk || count == 0 || lba>=disk_sectors || count>disk_sectors-lba)
        return -1;
    u16 *p = (u16 *)buf;

    for (u32 s = 0; s < count; s++) {
        outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
        io_wait();
        outb(ATA_COUNT, 1);                        /* 一次一扇, 稳 */
        outb(ATA_LBA_LO, (u8)(lba & 0xFF));
        outb(ATA_LBA_MI, (u8)((lba >> 8) & 0xFF));
        outb(ATA_LBA_HI, (u8)((lba >> 16) & 0xFF));
        trace_command(CMD_READ,lba);outb(ATA_CMD, CMD_READ);

        if(wait_ready(1)!=0) return -1;

        for (u32 i = 0; i < 256; i++)
            p[i] = inw(ATA_DATA);                  /* 256 字 = 512 字节 */
        p += 256;
        lba++;
    }
    return 0;
}
