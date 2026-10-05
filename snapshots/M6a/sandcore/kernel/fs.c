/* =====================================================================
 *  SandFS 只读文件系统 (kernel/fs.c)
 *  ---------------------------------------------------------------------
 *  【版图】LBA 65 超级块 → LBA 66 目录表 (每项 40B: 名31+NUL+start_lba+size)
 *          → LBA 67 起文件数据顺序排列。为 30 个文件和资源服务的最小设计。
 *  【v1 取舍】只读; 单层平铺无目录树; 无权限无日志。
 * ===================================================================== */
#include "io.h"
#include "ata.h"
#include "fs.h"

#define FS_LBA 0
#define FS_MAGIC "SANDFS"
#define FS_MAX_FILES 12

static struct {
    char     name[FS_NAME_MAX + 1];
    u32      start_lba;
    u32      size;
} dirents[FS_MAX_FILES];

static int nfiles;
static u32 fs_data_end;               /* 数据区末端 LBA (写文件从这里追加) */

static void write_meta(void);  /* 超级块+目录表写回 */

void fs_init(void)
{
    static u8 sec[512];
    nfiles = 0;

    if (ata_read_sectors(FS_LBA, 1, sec) != 0)
        return;
    /* 魔数 SANDFSMIO (作者尾缀版), 9 字节 */
    static const u8 magic[9] = {'S','A','N','D','F','S','M','I','O'};
    for (int i = 0; i < 9; i++)
        if (sec[i] != magic[i])
            return;                               /* 没有文件系统 */

    nfiles = sec[12] | (sec[13] << 8);
    if (nfiles > FS_MAX_FILES)
        nfiles = FS_MAX_FILES;

    static u8 dir[512];
    if (ata_read_sectors(FS_LBA + 1, 1, dir) != 0) {
        nfiles = 0;
        return;
    }
    fs_data_end = FS_LBA + 2;
    for (int i = 0; i < nfiles; i++) {
        u8 *e = dir + i * 40;
        int c = 0;
        while (c < FS_NAME_MAX && e[c]) {
            dirents[i].name[c] = (char)e[c];
            c++;
        }
        dirents[i].name[c] = 0;
        dirents[i].start_lba = e[32] | (e[33] << 8) | (e[34] << 16) | (e[35] << 24);
        dirents[i].size      = e[36] | (e[37] << 8) | (e[38] << 16) | (e[39] << 24);
        u32 sects = (dirents[i].size + 511) / 512;
        if (dirents[i].start_lba + sects > fs_data_end)
            fs_data_end = dirents[i].start_lba + sects;
    }
}

/* 写文件 (SandFS v2): 已存在且放得下 → 原地重写; 否则追加到数据末端。
 * 新文件则登记目录项。目录表/计数同步写回磁盘。 */
int fs_write(const char *name, const u8 *buf, u32 size)
{
    for (int i = 0; i < nfiles; i++) {
        const char *a = name, *b = dirents[i].name;
        int same = 1;
        while (*a || *b)
            if (*a++ != *b++) { same = 0; break; }
        if (!same)
            continue;
        u32 old_sects = (dirents[i].size + 511) / 512;
        u32 new_sects = (size + 511) / 512;
        u32 at = (new_sects <= old_sects) ? dirents[i].start_lba : fs_data_end;
        if (ata_write_sectors(at, new_sects, buf) != 0)
            return -1;
        dirents[i].start_lba = at;
        dirents[i].size = size;
        if (at + new_sects > fs_data_end)
            fs_data_end = at + new_sects;
        write_meta();
        return (int)size;
    }
    if (nfiles >= FS_MAX_FILES)
        return -1;
    u32 at = fs_data_end;
    if (ata_write_sectors(at, (size + 511) / 512, buf) != 0)
        return -1;
    int c = 0;
    while (name[c] && c < FS_NAME_MAX) { dirents[nfiles].name[c] = name[c]; c++; }
    dirents[nfiles].name[c] = 0;
    dirents[nfiles].start_lba = at;
    dirents[nfiles].size = size;
    fs_data_end = at + (size + 511) / 512;
    nfiles++;
    write_meta();
    return (int)size;
}

int fs_count(void)              { return nfiles; }
const char *fs_name(int i)      { return (i >= 0 && i < nfiles) ? dirents[i].name : 0; }
u32  fs_size(int i)             { return (i >= 0 && i < nfiles) ? dirents[i].size : 0; }

static void write_meta(void)
{
    static u8 sec[512];
    if (ata_read_sectors(FS_LBA, 1, sec) != 0)
        return;
    sec[12] = (u8)(nfiles & 0xFF);
    sec[13] = (u8)((nfiles >> 8) & 0xFF);
    ata_write_sectors(FS_LBA, 1, sec);

    static u8 dir[512];
    for (int i = 0; i < FS_MAX_FILES; i++)
        for (int k = 0; k < 40; k++)
            dir[i * 40 + k] = 0;
    for (int i = 0; i < nfiles; i++) {
        int c = 0;
        while (dirents[i].name[c]) { dir[i * 40 + c] = (u8)dirents[i].name[c]; c++; }
        u32 lb = dirents[i].start_lba, sz = dirents[i].size;
        dir[i * 40 + 32] = (u8)lb;           dir[i * 40 + 33] = (u8)(lb >> 8);
        dir[i * 40 + 34] = (u8)(lb >> 16);   dir[i * 40 + 35] = (u8)(lb >> 24);
        dir[i * 40 + 36] = (u8)sz;           dir[i * 40 + 37] = (u8)(sz >> 8);
        dir[i * 40 + 38] = (u8)(sz >> 16);   dir[i * 40 + 39] = (u8)(sz >> 24);
    }
    ata_write_sectors(FS_LBA + 1, 1, dir);
}

int fs_read(const char *name, void *buf, u32 bufsize)
{
    for (int i = 0; i < nfiles; i++) {
        const char *a = name, *b = dirents[i].name;
        int same = 1;
        while (*a || *b)
            if (*a++ != *b++) { same = 0; break; }
        if (!same)
            continue;

        u32 size = dirents[i].size;
        if (size > bufsize)
            size = bufsize;                       /* 目标缓冲太小便截断 */
        u32 sectors = (size + 511) / 512;
        if (sectors == 0)
            return 0;
        if (ata_read_sectors(dirents[i].start_lba, sectors, buf) != 0)
            return -1;
        return (int)size;
    }
    return -1;
}
