#ifndef SANDDATA_EDITOR_H
#define SANDDATA_EDITOR_H
#include <windows.h>
#include <stdint.h>
#define SFS_PATH 64
#define HOST_PATH 32768
typedef struct SfsBuffer SfsBuffer;
typedef struct {
    char name[SFS_PATH]; unsigned char *data; uint32_t size;
    int32_t uid,gid; uint32_t mode,generation; int directory;
    SfsBuffer *content; /* 新导入正文共享；盘内正文借不可变snapshot，不复制。 */
} SfsEntry;
typedef struct {
    SfsEntry *entries; uint32_t count,capacity,version,dir_sectors,data_start,sectors;
    uint32_t bank,commit_generation,object_generation;
    unsigned char *original; size_t image_size; wchar_t *path; int dirty;
    SfsBuffer *snapshot;
} SfsImage;
extern wchar_t sfs_error[1024];
int sfs_load(SfsImage *image,const wchar_t *path);
void sfs_free(SfsImage *image);
int sfs_clone(SfsImage *dest,const SfsImage *source);
int sfs_find(const SfsImage *image,const char *name);
int sfs_child(const char *name,const char *parent);
int sfs_valid_name(const char *name,uint32_t max_bytes);
int sfs_add_path(SfsImage *image,const wchar_t *host,const char *destination);
int sfs_mkdir(SfsImage *image,const char *name);
int sfs_remove(SfsImage *image,const char *name);
int sfs_rename(SfsImage *image,const char *from,const char *to);
int sfs_extract(const SfsImage *image,const char *name,const wchar_t *folder);
int sfs_save(SfsImage *image,const wchar_t *path);
wchar_t *sfs_wide(const char *text);
char *sfs_utf8(const wchar_t *text);
#endif
