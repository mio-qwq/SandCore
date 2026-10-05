/* =====================================================================
 *  SandFS 可写文件系统 (kernel/fs.c)，作者 mio
 *  ---------------------------------------------------------------------
 *  【v3 版图】IDE LBA0 超级块 → LBA1..8 目录表 → LBA9 起数据。
 *  每目录项仍为 40B，文件名允许 sys/bin/apps/desk/home 路径前缀。
 *  超级块 +16 保存目录扇区数；旧盘为 0 时按 v1/v2 的单扇区读取。
 *  扩容只改目录区长度，SCF/SCX 文件内容与每项布局都不变。
 *  【取舍】伪目录、整文件读写、追加式扩展；没有权限、回收压缩或日志。
 *  IO 失败返回 -1；掉电原子性不作承诺，详见 docs/FS.md。
 * ===================================================================== */
#include "io.h"
#include "ata.h"
#include "fs.h"

#define FS_LBA 0
#define FS_MAX_FILES 96
#define FS_DIR_SECTS 8
#define FS_DISK_SECTS 16384u          /* 构建的数据盘固定 8MB，拒绝写越界 */

static struct {
    char     name[FS_NAME_MAX + 1];
    u32      start_lba;
    u32      size;
} dirents[FS_MAX_FILES];

static int nfiles;
static u32 fs_data_end;               /* 数据区末端 LBA (写文件从这里追加) */
static u32 dir_sectors;              /* 兼容读取旧盘时保留其实际目录长度 */

static int write_meta(void);  /* 超级块+目录表写回；任何阶段失败都返回 -1 */

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

    dir_sectors = *(u32 *)(sec+16);
    if (!dir_sectors) dir_sectors = 1;
    if (dir_sectors != 1 && dir_sectors != FS_DIR_SECTS) return;
    nfiles = (int)*(u32 *)(sec+12);
    if (nfiles < 0 || nfiles > (int)(dir_sectors*512/40) || nfiles > FS_MAX_FILES) {
        nfiles = 0; return;
    }

    static u8 dir[FS_DIR_SECTS*512];
    if (ata_read_sectors(FS_LBA + 1, dir_sectors, dir) != 0) {
        nfiles = 0;
        return;
    }
    fs_data_end = FS_LBA + 1 + dir_sectors;
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
        if (dirents[i].start_lba < 1+dir_sectors || dirents[i].size > FS_DISK_SECTS*512
         || dirents[i].start_lba > FS_DISK_SECTS || sects > FS_DISK_SECTS-dirents[i].start_lba) {
            nfiles = 0; return;
        }
        if (dirents[i].start_lba + sects > fs_data_end)
            fs_data_end = dirents[i].start_lba + sects;
    }
}

/* 写文件 (SandFS v2): 已存在且放得下 → 原地重写; 否则追加到数据末端。
 * 新文件则登记目录项。目录表/计数同步写回磁盘。 */
int fs_write(const char *name, const u8 *buf, u32 size)
{
    int length = 0;
    while (name[length] && length <= FS_NAME_MAX) length++;
    if (!length || length > FS_NAME_MAX || size > FS_DISK_SECTS*512 || !dir_sectors) return -1;
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
        if (new_sects > FS_DISK_SECTS-at) return -1;
        static u8 sector[512];
        for (u32 s = 0; s < new_sects; s++) {
            for (u32 k = 0; k < 512; k++) sector[k] = s*512+k < size ? buf[s*512+k] : 0;
            if (ata_write_sectors(at+s, 1, sector) != 0) return -1;
        }
        dirents[i].start_lba = at;
        dirents[i].size = size;
        if (at + new_sects > fs_data_end)
            fs_data_end = at + new_sects;
        if(write_meta()!=0) return -1;
        return (int)size;
    }
    if (nfiles >= FS_MAX_FILES || nfiles >= (int)(dir_sectors*512/40))
        return -1;
    u32 at = fs_data_end;
    if ((size+511)/512 > FS_DISK_SECTS-at) return -1;
    static u8 sector[512];
    for (u32 s = 0; s < (size+511)/512; s++) {
        for (u32 k = 0; k < 512; k++) sector[k] = s*512+k < size ? buf[s*512+k] : 0;
        if (ata_write_sectors(at+s, 1, sector) != 0) return -1;
    }
    int c = 0;
    while (name[c] && c < FS_NAME_MAX) { dirents[nfiles].name[c] = name[c]; c++; }
    dirents[nfiles].name[c] = 0;
    dirents[nfiles].start_lba = at;
    dirents[nfiles].size = size;
    fs_data_end = at + (size + 511) / 512;
    nfiles++;
    if(write_meta()!=0) return -1;
    return (int)size;
}

int fs_count(void)              { return nfiles; }
const char *fs_name(int i)      { return (i >= 0 && i < nfiles) ? dirents[i].name : 0; }
u32  fs_size(int i)             { return (i >= 0 && i < nfiles) ? dirents[i].size : 0; }

static int write_meta(void)
{
    /* 元数据的 IO 错误也必须传回应用，否则 Notes 会在目录尚未落盘时
     * 显示 saved。这个小文件系统没有日志：失败可能已经写入部分扇区，
     * 返回 -1 是诚实报告失败，并不宣称事务回滚或掉电原子性。 */
    static u8 sec[512];
    if (ata_read_sectors(FS_LBA, 1, sec) != 0)
        return -1;
    *(u32 *)(sec+12) = (u32)nfiles;
    if(ata_write_sectors(FS_LBA, 1, sec)!=0) return -1;

    static u8 dir[FS_DIR_SECTS*512];
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
    return ata_write_sectors(FS_LBA + 1, dir_sectors, dir);
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
        /* 盘 IO 以整扇区工作，末扇区必须经过中转，不能越过调用者上限。 */
        static u8 sector[512];
        for (u32 s = 0; s < sectors; s++) {
            if (ata_read_sectors(dirents[i].start_lba+s, 1, sector) != 0) return -1;
            for (u32 k = 0; k < 512 && s*512+k < size; k++) ((u8 *)buf)[s*512+k] = sector[k];
        }
        return (int)size;
    }
    return -1;
}
