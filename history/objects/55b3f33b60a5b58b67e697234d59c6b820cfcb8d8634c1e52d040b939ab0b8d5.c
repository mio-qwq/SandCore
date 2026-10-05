/* =====================================================================
 * mio：SandFS v4，平铺完整路径、子目录投影与用户态核心保护。
 *
 * 【为什么仍不引入目录 inode】目录只是名字的斜杠分段，磁盘管理不用
 * 维护父子链表，也不会产生悬空父节点；列表从完整名字推导下一层。
 * start_lba=0/size=0 表示显式空目录，支持 mkdir 后暂时没有文件的情况。
 * 新盘 32 个目录扇区、192 项、72B 项；旧 1/8 扇区的 40B 项仍可读写。
 *
 * 【保护放在哪里】fs_user_mutable 是内核系统调用门的统一判定，不是
 * 磁盘权限位或 GUI 的只读标记。先规范化再忽略 ASCII 大小写比对
 * SYS/CORE；重命名/删除目录还要检查其全部后代，不能移动 SYS 绕过。
 * 内部 fs_write 供可信零环代码使用，不用假造“内核用户”获得写权限。
 *
 * 【诚实的失败语义】所有容量/冲突在改元数据之前检查；盘 IO 无日志，
 * 写入失败可能已经改变部分扇区。返回 -1，不宣称掉电事务或完整回滚。
 * ===================================================================== */
#include "io.h"
#include "ata.h"
#include "fs.h"
#define FS_MAX_FILES 192
#define FS_DIR_SECTS 32
#define FS_LEGACY_SECTS 16384u
#define FS_MAX_SECTS 131072u

typedef struct { char name[64]; u32 start_lba,size; } entry_t;
static entry_t dirents[FS_MAX_FILES];
static int nfiles;
static u32 fs_data_end,dir_sectors,entry_size,version,fs_disk_sectors;
static u8 meta[FS_DIR_SECTS*512];
static u8 sector[512];

static char fold(char c) { return c>='a' && c<='z' ? c-'a'+'A' : c; }
static int same(const char *a,const char *b)
{ while(*a && fold(*a)==fold(*b)) { a++; b++; } return fold(*a)==fold(*b); }
static int len(const char *s) { int n=0; while(s[n]) n++; return n; }
static void copy(char *d,const char *s) { while((*d++=*s++)) {} }
static int child(const char *name,const char *parent)
{
    int n=len(parent);
    if(!n) return name[0]!=0;
    for(int i=0;i<n;i++) if(fold(name[i])!=fold(parent[i]) || !name[i]) return 0;
    return name[n]=='/';
}
int fs_normalize(const char *path,char *out)
{
    int used=0,offset=0;
    /* 输入先逐段处理，输出始终最多 63B。不能把整条长路径截断再比对，
     * 否则两个不同名字或保护目录别名可能被错误合并。冒号/控制字符/
     * 反斜杠不在本系统路径语法内，显式拒绝，不引入第二套分隔规则。 */
    while(path[offset]) {
        while(path[offset]=='/') offset++;
        if(!path[offset]) break;
        int start=offset;
        while(path[offset] && path[offset]!='/') {
            if((u8)path[offset]<33 || path[offset]=='\\' || path[offset]==':') return -1;
            offset++;
        }
        int n=offset-start;
        if(n==1 && path[start]=='.') continue;
        if(n==2 && path[start]=='.' && path[start+1]=='.') {
            if(!used) return -1;
            while(used && out[used-1]!='/') used--;
            if(used) used--;
            continue;
        }
        if(used+n+(used!=0)>FS_NAME_MAX) return -1;
        if(used) out[used++]='/';
        for(int i=0;i<n;i++) out[used++]=path[start+i];
    }
    out[used]=0; return used;
}
static int find(const char *name)
{ for(int i=0;i<nfiles;i++) if(same(name,dirents[i].name)) return i; return -1; }
static int protected(const char *name)
{ return same(name,"SYS/CORE") || child(name,"SYS/CORE"); }
int fs_user_mutable(const char *path,int descendants)
{
    char name[64]; if(fs_normalize(path,name)<0 || !name[0] || protected(name)) return 0;
    if(descendants) for(int i=0;i<nfiles;i++)
        if(child(dirents[i].name,name) && protected(dirents[i].name)) return 0;
    return 1;
}
static int write_meta(void)
{
    if(ata_read_sectors(0,1,sector)) return -1;
    *(u32 *)(sector+12)=(u32)nfiles;
    if(ata_write_sectors(0,1,sector)) return -1;
    for(u32 i=0;i<dir_sectors*512;i++) meta[i]=0;
    u32 name_bytes=entry_size-8;
    for(int i=0;i<nfiles;i++) {
        u8 *e=meta+i*entry_size;
        int n=len(dirents[i].name);
        if(n>=(int)name_bytes) return -1;
        for(int k=0;k<n;k++) e[k]=(u8)dirents[i].name[k];
        *(u32 *)(e+name_bytes)=dirents[i].start_lba;
        *(u32 *)(e+name_bytes+4)=dirents[i].size;
    }
    return ata_write_sectors(1,dir_sectors,meta);
}
void fs_init(void)
{
    nfiles=0; dir_sectors=0;
    if(ata_read_sectors(0,1,sector)) return;
    const char *magic="SANDFSMIO";
    for(int i=0;i<9;i++) if(sector[i]!=(u8)magic[i]) return;
    u32 ds=*(u32 *)(sector+16),ver=*(u32 *)(sector+20),count=*(u32 *)(sector+12);
    if(!ds) ds=1;
    if(ver==4 && ds==32) entry_size=72;
    else if(ver<=3 && (ds==1 || ds==8)) entry_size=40;
    else return;
    if(count>FS_MAX_FILES || count>ds*512/entry_size) return;
    /* v4保留字段24登记可用扇区；0维持历史8MB。声明容量必须同时
     * 小于当前驱动与本版上限，不能信任超级块把小盘伪装成大盘。 */
    u32 declared=ver==4?*(u32 *)(sector+24):0;
    fs_disk_sectors=declared?declared:FS_LEGACY_SECTS;
    if(fs_disk_sectors<1+ds || fs_disk_sectors>FS_MAX_SECTS || fs_disk_sectors>ata_sector_count())return;
    if(ata_read_sectors(1,ds,meta)) return;
    fs_data_end=1+ds;
    for(u32 i=0;i<count;i++) {
        u8 *e=meta+i*entry_size; u32 namesize=entry_size-8;
        if(!e[0] || e[namesize-1]) return;
        for(u32 k=0;k<namesize;k++) dirents[i].name[k]=(char)e[k];
        dirents[i].name[namesize-1]=0;
        dirents[i].start_lba=*(u32 *)(e+namesize);
        dirents[i].size=*(u32 *)(e+namesize+4);
        u32 at=dirents[i].start_lba,size=dirents[i].size;
        if(!at && !size && ver==4) continue;
        if(size>fs_disk_sectors*512 || at<1+ds || at>fs_disk_sectors
           || (size+511)/512>fs_disk_sectors-at) return;
        if(at+(size+511)/512>fs_data_end) fs_data_end=at+(size+511)/512;
    }
    nfiles=(int)count; dir_sectors=ds; version=ver;
}
int fs_count(void) { return nfiles; }
const char *fs_name(int i) { return i>=0 && i<nfiles ? dirents[i].name : 0; }
u32 fs_size(int i) { return i>=0 && i<nfiles ? dirents[i].size : 0; }
int fs_stat(const char *path,u32 *info)
{
    char name[64]; if(fs_normalize(path,name)<0) return -1;
    if(!name[0]) { info[0]=2; info[1]=0; return 0; }
    int i=find(name);
    if(i>=0) { info[0]=dirents[i].start_lba?1:2; info[1]=dirents[i].size; return 0; }
    for(i=0;i<nfiles;i++) if(child(dirents[i].name,name)) { info[0]=2; info[1]=0; return 0; }
    return -1;
}
int fs_read(const char *path,void *buf,u32 capacity)
{
    char name[64]; if(fs_normalize(path,name)<0) return -1;
    int i=find(name); if(i<0 || !dirents[i].start_lba) return -1;
    u32 size=dirents[i].size; if(size>capacity) size=capacity;
    for(u32 s=0;s<(size+511)/512;s++) {
        if(ata_read_sectors(dirents[i].start_lba+s,1,sector)) return -1;
        for(u32 k=0;k<512 && s*512+k<size;k++) ((u8 *)buf)[s*512+k]=sector[k];
    }
    return (int)size;
}
/* 大图只读一行即可，避免用户态为24位BMP暂存6MB。减法裁剪长度
 * 使offset+capacity没有溢出机会，末扇区不写穿请求范围。 */
int fs_read_at(const char *path,void *buf,u32 capacity,u32 offset)
{
    char name[64];if(fs_normalize(path,name)<0)return -1;
    int i=find(name);if(i<0 || !dirents[i].start_lba || offset>dirents[i].size)return -1;
    u32 size=dirents[i].size-offset;if(size>capacity)size=capacity;
    u32 used=0;
    while(used<size) {
        u32 pos=offset+used,k=pos&511,n=512-k;if(n>size-used)n=size-used;
        if(ata_read_sectors(dirents[i].start_lba+pos/512,1,sector))return -1;
        for(u32 j=0;j<n;j++)((u8 *)buf)[used+j]=sector[k+j];
        used+=n;
    }
    return (int)used;
}
int fs_write(const char *path,const u8 *buf,u32 size)
{
    char name[64];
    if(fs_normalize(path,name)<=0 || !dir_sectors || size>fs_disk_sectors*512) return -1;
    if(len(name)>=(int)entry_size-8) return -1; /* 旧盘保持旧项布局与名字上限 */
    u32 info[2];
    if(!fs_stat(name,info) && info[0]==2) return -1;
    /* 任何已有普通文件不能同时充当父目录，否则列表与读取产生歧义。 */
    for(int j=0;j<nfiles;j++) if(dirents[j].start_lba && child(name,dirents[j].name)) return -1;
    int i=find(name),fresh=(i<0);
    if(fresh) {
        if(nfiles>=FS_MAX_FILES || (u32)nfiles>=dir_sectors*512/entry_size) return -1;
        i=nfiles;
    }
    u32 sectors=(size+511)/512,old=fresh?0:(dirents[i].size+511)/512;
    u32 at=sectors<=old && !fresh?dirents[i].start_lba:fs_data_end;
    if(at>fs_disk_sectors || sectors>fs_disk_sectors-at) return -1;
    for(u32 s=0;s<sectors;s++) {
        for(u32 k=0;k<512;k++) sector[k]=s*512+k<size?buf[s*512+k]:0;
        if(ata_write_sectors(at+s,1,sector)) return -1;
    }
    copy(dirents[i].name,name); dirents[i].start_lba=at; dirents[i].size=size;
    if(at+sectors>fs_data_end) fs_data_end=at+sectors;
    if(fresh) nfiles++;
    return write_meta()?-1:(int)size;
}
int fs_mkdir(const char *path)
{
    char name[64]; u32 info[2];
    if(version!=4 || fs_normalize(path,name)<=0 || !fs_stat(name,info)) return -1;
    if(nfiles>=FS_MAX_FILES || (u32)nfiles>=dir_sectors*512/entry_size) return -1;
    for(int i=0;i<nfiles;i++) if(dirents[i].start_lba && child(name,dirents[i].name)) return -1;
    copy(dirents[nfiles].name,name); dirents[nfiles].start_lba=0; dirents[nfiles].size=0;
    nfiles++; return write_meta();
}
int fs_remove(const char *path)
{
    char name[64]; if(fs_normalize(path,name)<=0) return -1;
    int i=find(name); if(i<0) return -1;
    for(int j=0;j<nfiles;j++) if(child(dirents[j].name,name)) return -1;
    for(int j=i;j<nfiles-1;j++) dirents[j]=dirents[j+1];
    nfiles--; return write_meta();
}
static entry_t renamed[FS_MAX_FILES];
int fs_rename(const char *oldpath,const char *newpath)
{
    char old[64],next[64]; u32 info[2];
    if(fs_normalize(oldpath,old)<=0 || fs_normalize(newpath,next)<=0 || fs_stat(old,info)) return -1;
    if(same(old,next)) return 0;
    if(child(next,old) || !fs_stat(next,info)) return -1;
    for(int i=0;i<nfiles;i++)
        if(dirents[i].start_lba && child(next,dirents[i].name)) return -1;
    int n=len(old),m=len(next);
    for(int i=0;i<nfiles;i++) {
        renamed[i]=dirents[i];
        if(same(dirents[i].name,old) || child(dirents[i].name,old)) {
            int tail=len(dirents[i].name+n);
            if(m+tail>FS_NAME_MAX || m+tail>=(int)entry_size-8) return -1;
            copy(renamed[i].name,next); copy(renamed[i].name+m,dirents[i].name+n);
        }
        for(int j=0;j<i;j++) if(same(renamed[i].name,renamed[j].name)) return -1;
    }
    for(int i=0;i<nfiles;i++) dirents[i]=renamed[i];
    return write_meta();
}
int fs_list_dir(const char *path,char *out,u32 capacity)
{
    char parent[64]; u32 info[2];
    if(!capacity || fs_normalize(path,parent)<0 || fs_stat(parent,info) || info[0]!=2) return -1;
    /* 用名字去重而不是目录索引；多个 home/game/ 下的条目只产生一个 game。
     * 每一行先完整构造再检查容量，绝不输出半个文件名或未终止字符串。 */
    static char names[FS_MAX_FILES][64]; int used_names=0; u32 used=0;
    int prefix=len(parent)+(parent[0]!=0);
    for(int i=0;i<nfiles;i++) if(child(dirents[i].name,parent)) {
        const char *p=dirents[i].name+prefix; char name[64]; int n=0;
        while(p[n] && p[n]!='/') { name[n]=p[n]; n++; } name[n]=0;
        int duplicate=0;
        for(int j=0;j<used_names;j++) if(same(names[j],name)) duplicate=1;
        if(duplicate) continue;
        copy(names[used_names++],name);
        int directory=p[n]=='/' || !dirents[i].start_lba;
        u32 size=directory?0:dirents[i].size; char digits[12]; int dn=0;
        do { digits[dn++]=(char)('0'+size%10); size/=10; } while(size);
        if(used+2+(u32)n+1+(u32)dn+1>=capacity) break;
        out[used++]=directory?'D':'F'; out[used++]=' ';
        for(int j=0;j<n;j++) out[used++]=name[j];
        out[used++]=' ';
        while(dn) out[used++]=digits[--dn];
        out[used++]='\n';
    }
    out[used]=0; return (int)used;
}
