/* =====================================================================
 * mio：SandFS v5，完整路径、显式父目录、UID/GID/rw与COW事务。
 *
 * 【为什么仍不引入目录 inode】目录只是名字的斜杠分段，磁盘管理不用
 * 维护父子链表，也不会产生悬空父节点；列表从完整名字推导下一层。
 * start_lba=0/size=0 表示显式空目录，支持 mkdir 后暂时没有文件的情况。
 * 新盘双384扇区目录bank、2048个96B项；历史格式按原位置读取，
 * 三环先迁移副本再修改，不在开机时覆盖旧盘数据。
 *
 * 【保护放在哪里】fs_user_mutable 是内核系统调用门的统一判定，不是
 * GUI的只读标记。磁盘rw与保护gate一起检查，root只绕过普通rw。
 * 重命名/删除目录还要检查全部保护后代，不能移动SYS绕过。
 * 内部 fs_write 供可信零环代码使用，不用假造“内核用户”获得写权限。
 *
 * 【诚实的失败语义】所有容量/冲突在改元数据之前检查；盘 IO 无日志，
 * 新数据及备用元数据先写，超级块最后提交；结果不明转只读。
 * 返回-1，不宣称扇区撕裂/掉电事务或仅恢复内存就完成磁盘回滚。
 * ===================================================================== */
#include "io.h"
#include "ata.h"
#include "fs.h"
#include "memory.h"
#include "auth.h"
#include "task.h"
#include "crc.h"
#define FS_MAX_FILES 2048
#define FS_LEGACY_SECTS 16384u
#define FS_MAX_SECTS 131072u

typedef struct { char name[64]; u32 start_lba,size;i32 uid,gid;u32 mode,flags,generation,reserved; } entry_t;
/* mio：目录常驻页在验证超级块后按实际容量分配，不能把512项加
 * 三份临时表硬塞进2MB以内的引导BSS。失败整组释放、呈现无效卷。
 * rename短期另分配，list直接回看去重；日常读取不反复分配目录。 */
static entry_t *dirents;
static int nfiles;
static u32 fs_data_end,dir_sectors,entry_size,version,fs_disk_sectors,dir_capacity;
static u32 dir_pages,meta_pages;
static u8 *meta;
static u8 sector[512];
static i32 root_uid,root_gid;
static u32 root_mode;
static u32 fs_data_start,metadata_generation,active_bank,allocation_pages,fs_faulted;
static u8 *allocation;
static u32 object_generation;
#define FS_TRANSACTIONS 8
typedef struct {
    u32 token,start,capacity,used,original,failed;
    int owner,uid,gid;
    char name[64];
} transaction_t;
static transaction_t transactions[FS_TRANSACTIONS];
static u32 transaction_counter;
static u32 new_generation(void)
{
    /* 持久化的单调序号防止删除后同名重建被旧文件句柄误认；耗尽即
     * 拒绝修改，不能在32位回绕时把旧对象再次当成同一个文件。 */
    if(object_generation==0xFFFFFFFFu)return 0;
    return ++object_generation;
}

static u32 name_bytes(void){return version>=4?64u:32u;}

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
static int busy(const char *name)
{
    for(u32 i=0;i<FS_TRANSACTIONS;i++)if(transactions[i].token
        && (same(name,transactions[i].name) || child(transactions[i].name,name)))return 1;
    return 0;
}
static int protected(const char *name)
{ return same(name,"SYS/CORE") || child(name,"SYS/CORE") || same(name,"SYS/AUTH") || child(name,"SYS/AUTH"); }
static int module_path(const char *name)
{return same(name,"SYS/MOD") || child(name,"SYS/MOD");}
int fs_user_mutable(const char *path,int descendants)
{
    char name[64]; if(fs_normalize(path,name)<0 || !name[0] || protected(name)) return 0;
    /* 旧卷没有可持久化的身份元数据，M9只提供兼容读取；先迁移副本。
     * MOD授权另核对外部会话，root和普通SYSTEM服务都不能绕过。 */
    if(version<5 || (module_path(name) && !auth_can_mod(task_pid())))return 0;
    if(descendants) for(int i=0;i<nfiles;i++)
        if(child(dirents[i].name,name) && (protected(dirents[i].name)
            || (module_path(dirents[i].name) && !auth_can_mod(task_pid()))))return 0;
    return 1;
}
static int write_meta(void)
{
    if(fs_faulted)return -1;
    if(ata_read_sectors(0,1,sector)) return -1;
    for(u32 i=0;i<dir_sectors*512;i++) meta[i]=0;
    u32 names=name_bytes();
    for(int i=0;i<nfiles;i++) {
        u8 *e=meta+i*entry_size;
        int n=len(dirents[i].name);
        if(n>=(int)names) return -1;
        for(int k=0;k<n;k++) e[k]=(u8)dirents[i].name[k];
        *(u32 *)(e+names)=dirents[i].start_lba;
        *(u32 *)(e+names+4)=dirents[i].size;
        if(version==5){
            *(i32 *)(e+72)=dirents[i].uid;*(i32 *)(e+76)=dirents[i].gid;
            *(u32 *)(e+80)=dirents[i].mode;*(u32 *)(e+84)=dirents[i].flags;
            *(u32 *)(e+88)=dirents[i].generation;
        }
    }
    if(version==5){
        u32 next=active_bank^1u;
        if(ata_write_sectors(1+next*dir_sectors,dir_sectors,meta))return -1;
        *(u32 *)(sector+12)=(u32)nfiles;
        *(u32 *)(sector+44)=crc32_update(0,meta,dir_sectors*512);
        *(u32 *)(sector+48)=next;
        u32 generation=metadata_generation+1;if(!generation)generation=1;
        *(u32 *)(sector+52)=generation;*(u32 *)(sector+56)=object_generation;
        *(u32 *)(sector+508)=crc32_update(0,sector,508);
        /* 先新数据/备用目录，最后切换超级块。提交IO报错可能结果不明，
         * 停止后续写入，保留原数据；不以“恢复内存”谎称磁盘已回滚。 */
        if(ata_write_sectors(0,1,sector)){fs_faulted=1;return -1;}
        active_bank=next;metadata_generation=generation;return 0;
    }
    *(u32 *)(sector+12)=(u32)nfiles;
    if(ata_write_sectors(0,1,sector))return -1;
    return ata_write_sectors(1,dir_sectors,meta);
}
static void release_directory(void)
{
    if(dirents)pframe_free_run((u32)dirents,dir_pages);
    if(meta)pframe_free_run((u32)meta,meta_pages);
    if(allocation)pframe_free_run((u32)allocation,allocation_pages);
    allocation=0;allocation_pages=fs_data_start=active_bank=metadata_generation=fs_faulted=0;
    object_generation=0;
    for(u32 i=0;i<FS_TRANSACTIONS;i++)transactions[i].token=0;
    dirents=0;meta=0;dir_pages=meta_pages=0;
    nfiles=0;dir_sectors=version=fs_disk_sectors=dir_capacity=fs_data_end=0;
}
void fs_init(void)
{
    release_directory();
    if(ata_read_sectors(0,1,sector)) return;
    const char *magic="SANDFSMIO";
    for(int i=0;i<9;i++) if(sector[i]!=(u8)magic[i]) return;
    u32 ds=*(u32 *)(sector+16),ver=*(u32 *)(sector+20),count=*(u32 *)(sector+12);
    if(!ds) ds=1;
    if(ver==5 && (ds==192 || ds==384) && *(u32 *)(sector+28)==96
        && *(u32 *)(sector+508)==crc32_update(0,sector,508))entry_size=96;
    else if(ver==4 && (ds==32 || ds==80)) entry_size=72;
    else if(ver<=3 && (ds==1 || ds==8)) entry_size=40;
    else return;
    u32 capacity=ver==5?ds*512/96:ver==4?(ds==32?192:512):ds*512/entry_size;
    if(count>capacity) return;
    /* v4保留字段24登记可用扇区；0维持历史8MB。声明容量必须同时
     * 小于当前驱动与本版上限，不能信任超级块把小盘伪装成大盘。 */
    u32 declared=ver>=4?*(u32 *)(sector+24):0;
    u32 sectors=declared?declared:FS_LEGACY_SECTS;
    fs_data_start=1+ds*(ver==5?2u:1u);
    if(sectors<fs_data_start || sectors>FS_MAX_SECTS || sectors>ata_sector_count())return;
    dir_pages=(capacity*sizeof(entry_t)+4095)/4096;meta_pages=(ds*512+4095)/4096;
    dirents=(entry_t *)pframe_alloc_run(dir_pages);
    meta=(u8 *)pframe_alloc_run(meta_pages);
    if(!dirents || !meta)goto failed;
    for(u32 i=0;i<dir_pages*4096;i++)((u8 *)dirents)[i]=0;
    u32 expected_crc=0;
    if(ver==5){
        active_bank=*(u32 *)(sector+48);metadata_generation=*(u32 *)(sector+52);expected_crc=*(u32 *)(sector+44);
        object_generation=*(u32 *)(sector+56);
        if(active_bank>1 || !metadata_generation)goto failed;
    }
    if(ata_read_sectors(1+active_bank*ds,ds,meta))goto failed;
    if(ver==5 && crc32_update(0,meta,ds*512)!=expected_crc)goto failed;
    fs_disk_sectors=sectors;
    version=ver;root_uid=root_gid=0;root_mode=23;
    if(ver==5){
        root_uid=*(i32 *)(sector+32);root_gid=*(i32 *)(sector+36);root_mode=*(u32 *)(sector+40);
        if(root_uid<-1 || root_gid<-1 || (root_mode&~63u))goto failed;
    }
    fs_data_end=fs_data_start;
    for(u32 i=0;i<count;i++) {
        u8 *e=meta+i*entry_size; u32 namesize=name_bytes();
        if(!e[0] || e[namesize-1])goto failed;
        for(u32 k=0;k<namesize;k++) dirents[i].name[k]=(char)e[k];
        dirents[i].name[namesize-1]=0;
        dirents[i].start_lba=*(u32 *)(e+namesize);
        dirents[i].size=*(u32 *)(e+namesize+4);
        dirents[i].uid=dirents[i].gid=0;dirents[i].mode=23;dirents[i].generation=1;
        if(ver==5){
            dirents[i].uid=*(i32 *)(e+72);dirents[i].gid=*(i32 *)(e+76);
            dirents[i].mode=*(u32 *)(e+80);dirents[i].flags=*(u32 *)(e+84);
            dirents[i].generation=*(u32 *)(e+88);
            if(dirents[i].uid<-1 || dirents[i].gid<-1 || (dirents[i].mode&~63u)
                || (dirents[i].flags&~1u) || !dirents[i].generation || *(u32 *)(e+92))goto failed;
            if(dirents[i].generation>object_generation)object_generation=dirents[i].generation;
        }
        char normalized[64];if(fs_normalize(dirents[i].name,normalized)<=0 || !same(normalized,dirents[i].name))goto failed;
        for(u32 j=0;j<i;j++)if(same(dirents[i].name,dirents[j].name))goto failed;
        if(module_path(dirents[i].name) || protected(dirents[i].name))dirents[i].uid=dirents[i].gid=AUTH_SYSTEM;
        u32 at=dirents[i].start_lba,size=dirents[i].size;
        if(!at && !size && ver>=4){if(ver==5 && dirents[i].flags!=1)goto failed;continue;}
        if(ver==5 && dirents[i].flags)goto failed;
        if(size>fs_disk_sectors*512 || at<fs_data_start || at>fs_disk_sectors
           || (size+511)/512>fs_disk_sectors-at)goto failed;
        if(at+(size+511)/512>fs_data_end) fs_data_end=at+(size+511)/512;
    }
    nfiles=(int)count;dir_sectors=ds;version=ver;dir_capacity=capacity;
    if(ver==5){
        allocation_pages=((sectors+7)/8+4095)/4096;
        allocation=(u8 *)pframe_alloc_run(allocation_pages);if(!allocation)goto failed;
        for(u32 n=0;n<allocation_pages*4096;n++)allocation[n]=0;
        for(u32 n=0;n<fs_data_start;n++)allocation[n>>3]|=(u8)(1u<<(n&7));
        for(u32 i=0;i<count;i++){
            entry_t *e=&dirents[i];
            for(u32 n=0;n<(e->size+511)/512;n++){
                u32 at=e->start_lba+n;
                if(allocation[at>>3]&(1u<<(at&7)))goto failed;
                allocation[at>>3]|=(u8)(1u<<(at&7));
            }
            char parent[64];u32 n=0;
            while(e->name[n]){
                if(e->name[n]=='/'){
                    parent[n]=0;int index=find(parent);
                    if(index<0 || dirents[index].start_lba)goto failed;
                }
                parent[n]=e->name[n];n++;
            }
        }
    }
    return;
failed:
    release_directory();
}
void fs_storage_info(u32 *out)
{
    for(int i=0;i<32;i++)out[i]=0;
    out[0]=1;out[1]=ata_sector_count()!=0;out[2]=512;out[3]=ata_sector_count();
    out[4]=2880;out[5]=1;
    if(!dir_sectors)return;
    out[6]=1;out[7]=version;out[8]=fs_disk_sectors;out[9]=fs_data_start;
    out[10]=fs_data_end;out[11]=fs_disk_sectors-fs_data_end;
    out[12]=dir_capacity;out[13]=(u32)nfiles;out[14]=dir_sectors;
    for(int i=0;i<nfiles;i++)if(dirents[i].start_lba){
        out[15]+=(dirents[i].size+511)/512;out[16]+=dirents[i].size;
    }
    /* 现有卷没有数据洞回收；不能用总减活跃字节冒充还能写的空间。
     * 防止损坏重叠记录让只读统计无符号下溢；此值不是分区号。 */
    u32 used=fs_data_end-fs_data_start;
    out[17]=used>=out[15]?used-out[15]:0;out[18]=version==5?2:1;
}

void fs_storage_info2(u32 out[16])
{
    for(u32 i=0;i<16;i++)out[i]=0;out[0]=1;out[1]=dir_sectors!=0;if(!dir_sectors)return;
    out[2]=version;out[3]=fs_disk_sectors;out[4]=fs_data_start;out[5]=fs_data_end;out[6]=dir_capacity;out[7]=(u32)nfiles;
    for(int i=0;i<nfiles;i++)if(dirents[i].start_lba)out[8]+=(dirents[i].size+511)/512;
    if(version==5 && allocation){
        u32 run=0,occupied=0;for(u32 i=fs_data_start;i<fs_disk_sectors;i++){
            if(allocation[i>>3]&(1u<<(i&7))){occupied++;run=0;}
            else {out[10]++;run++;if(run>out[11])out[11]=run;}
        }out[9]=occupied>=out[8]?occupied-out[8]:0;
    }else out[10]=out[11]=fs_disk_sectors-fs_data_end;
    out[12]=object_generation;out[13]=active_bank;out[14]=metadata_generation;out[15]=(u32)fs_faulted;
}
static void allocated(u32 at,u32 count,int used)
{if(allocation)for(u32 i=0;i<count;i++){u32 n=at+i;u8 bit=(u8)(1u<<(n&7));if(used)allocation[n>>3]|=bit;else allocation[n>>3]&=(u8)~bit;}}
static u32 allocate(u32 count)
{
    if(!count)return fs_data_start;
    u32 run=0;
    for(u32 at=fs_data_start;at<fs_disk_sectors;at++){
        if(allocation[at>>3]&(1u<<(at&7)))run=0;else run++;
        if(run==count){u32 start=at+1-count;allocated(start,count,1);return start;}
    }
    return 0;
}
static void highwater(void)
{
    fs_data_end=fs_data_start;
    for(int i=0;i<nfiles;i++)if(dirents[i].start_lba){
        u32 end=dirents[i].start_lba+(dirents[i].size+511)/512;if(end>fs_data_end)fs_data_end=end;
    }
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
    if(fs_normalize(path,name)<=0 || !dir_sectors || fs_faulted || size>fs_disk_sectors*512 || busy(name)) return -1;
    u32 generation=version==5?new_generation():1;if(!generation)return -1;
    if(len(name)>=(int)name_bytes()) return -1; /* 旧盘保持旧名字上限。 */
    u32 info[2];
    if(!fs_stat(name,info) && info[0]==2) return -1;
    /* 任何已有普通文件不能同时充当父目录，否则列表与读取产生歧义。 */
    for(int j=0;j<nfiles;j++) if(dirents[j].start_lba && child(name,dirents[j].name)) return -1;
    int i=find(name),fresh=(i<0);
    if(fresh) {
        if((u32)nfiles>=dir_capacity)return -1;
        i=nfiles;
        dirents[i].uid=auth_subject_uid(task_pid());dirents[i].gid=auth_subject_gid(task_pid());
        if(dirents[i].uid<-1)dirents[i].uid=dirents[i].gid=AUTH_SYSTEM;
        dirents[i].mode=23;dirents[i].flags=dirents[i].reserved=0;dirents[i].generation=0;
        if(module_path(name) || protected(name))dirents[i].uid=dirents[i].gid=AUTH_SYSTEM;
    }
    entry_t previous=dirents[i];
    u32 sectors=(size+511)/512,old=fresh?0:(dirents[i].size+511)/512;
    u32 at=version==5?allocate(sectors):sectors<=old && !fresh?dirents[i].start_lba:fs_data_end;
    if(!at)return -1;
    if(at>fs_disk_sectors || sectors>fs_disk_sectors-at) return -1;
    for(u32 s=0;s<sectors;s++) {
        for(u32 k=0;k<512;k++) sector[k]=s*512+k<size?buf[s*512+k]:0;
        if(ata_write_sectors(at+s,1,sector)){if(version==5)allocated(at,sectors,0);return -1;}
    }
    copy(dirents[i].name,name); dirents[i].start_lba=at; dirents[i].size=size;
    dirents[i].generation=generation;
    if(at+sectors>fs_data_end) fs_data_end=at+sectors;
    if(fresh) nfiles++;
    if(write_meta()){
        if(fresh)nfiles--;dirents[i]=previous;
        if(version==5){allocated(at,sectors,0);highwater();}
        return -1;
    }
    if(version==5){if(!fresh)allocated(previous.start_lba,old,0);highwater();}
    return (int)size;
}
int fs_mkdir(const char *path)
{
    char name[64]; u32 info[2];
    if(version<4 || fs_normalize(path,name)<=0 || !fs_stat(name,info)) return -1;
    if((u32)nfiles>=dir_capacity)return -1;
    for(int i=0;i<nfiles;i++) if(dirents[i].start_lba && child(name,dirents[i].name)) return -1;
    copy(dirents[nfiles].name,name); dirents[nfiles].start_lba=0; dirents[nfiles].size=0;
    dirents[nfiles].uid=auth_subject_uid(task_pid());dirents[nfiles].gid=auth_subject_gid(task_pid());
    if(dirents[nfiles].uid<-1)dirents[nfiles].uid=dirents[nfiles].gid=AUTH_SYSTEM;
    if(module_path(name) || protected(name))dirents[nfiles].uid=dirents[nfiles].gid=AUTH_SYSTEM;
    u32 generation=version==5?new_generation():1;if(!generation)return -1;
    dirents[nfiles].mode=23;dirents[nfiles].flags=1;dirents[nfiles].generation=generation;dirents[nfiles].reserved=0;
    nfiles++;if(write_meta()){nfiles--;return -1;}return 0;
}
int fs_remove(const char *path)
{
    char name[64]; if(fs_normalize(path,name)<=0) return -1;
    int i=find(name); if(i<0 || busy(name)) return -1;
    for(int j=0;j<nfiles;j++) if(child(dirents[j].name,name)) return -1;
    entry_t removed=dirents[i];
    for(int j=i;j<nfiles-1;j++) dirents[j]=dirents[j+1];
    nfiles--;
    if(write_meta()){
        for(int j=nfiles;j>i;j--)dirents[j]=dirents[j-1];dirents[i]=removed;nfiles++;return -1;
    }
    if(version==5){allocated(removed.start_lba,(removed.size+511)/512,0);highwater();}return 0;
}
int fs_rename(const char *oldpath,const char *newpath)
{
    char old[64],next[64]; u32 info[2];
    if(fs_normalize(oldpath,old)<=0 || fs_normalize(newpath,next)<=0 || fs_stat(old,info)) return -1;
    if(busy(old) || busy(next))return -6;
    if(same(old,next)) return 0;
    if(child(next,old) || !fs_stat(next,info)) return -1;
    for(int i=0;i<nfiles;i++)
        if(dirents[i].start_lba && child(next,dirents[i].name)) return -1;
    int n=len(old),m=len(next);
    u32 pages=((u32)nfiles*sizeof(entry_t)+4095)/4096;
    entry_t *renamed=(entry_t *)pframe_alloc_run(pages);
    if(!renamed)return -1;
    for(int i=0;i<nfiles;i++) {
        renamed[i]=dirents[i];
        if(same(dirents[i].name,old) || child(dirents[i].name,old)) {
            int tail=len(dirents[i].name+n);
            if(m+tail>FS_NAME_MAX || m+tail>=(int)name_bytes())goto failed;
            copy(renamed[i].name,next); copy(renamed[i].name+m,dirents[i].name+n);
            renamed[i].generation=version==5?new_generation():1;
            if(!renamed[i].generation)goto failed;
        }
        for(int j=0;j<i;j++)if(same(renamed[i].name,renamed[j].name))goto failed;
    }
    entry_t *original=dirents;dirents=renamed;
    int result=write_meta();dirents=original;
    if(!result)for(int i=0;i<nfiles;i++)dirents[i]=renamed[i];
    pframe_free_run((u32)renamed,pages);return result;
failed:
    pframe_free_run((u32)renamed,pages);return -1;
}
int fs_list_dir(const char *path,char *out,u32 capacity)
{
    char parent[64]; u32 info[2];
    if(!capacity || fs_normalize(path,parent)<0 || fs_stat(parent,info) || info[0]!=2) return -1;
    /* 用名字去重而不是目录索引；多个 home/game/ 下的条目只产生一个 game。
     * 每一行先完整构造再检查容量，绝不输出半个文件名或未终止字符串。 */
    u32 used=0;
    int prefix=len(parent)+(parent[0]!=0);
    for(int i=0;i<nfiles;i++) if(child(dirents[i].name,parent)) {
        const char *p=dirents[i].name+prefix; char name[64]; int n=0;
        while(p[n] && p[n]!='/') { name[n]=p[n]; n++; } name[n]=0;
        int duplicate=0;
        /* 回看已经遍历的目录记录，不再保留32KB名字副本。最多512
         * 项，比较只走直接子项这一段；目录名与大小写语义不变。 */
        for(int j=0;j<i && !duplicate;j++)if(child(dirents[j].name,parent)) {
            const char *prior=dirents[j].name+prefix;int k=0;
            while(k<n && fold(prior[k])==fold(name[k]))k++;
            if(k==n && (!prior[k] || prior[k]=='/'))duplicate=1;
        }
        if(duplicate) continue;
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
int fs_create_ex(int owner,const char *path,u32 kind,u32 mode)
{
    char name[64];u32 info[2];if(version!=5 || !dir_sectors || fs_faulted || (kind!=1 && kind!=2) || mode>63 || fs_normalize(path,name)<=0)return -1;
    if(!fs_user_mutable(name,0) || !fs_access_parent(owner,name))return -5;
    if(!fs_stat(name,info))return -8;if(busy(name))return -6;if((u32)nfiles>=dir_capacity)return -4;
    u32 generation=new_generation();if(!generation)return -4;u32 at=kind==1?allocate(0):0;if(kind==1 && !at)return -4;
    /* 临时文件必须首次发布就是仅本人rw，不能先采用默认权限，
     * 再靠另一个chmod陷入修正，中间给其它任务留下读取窗口。 */
    entry_t *e=dirents+nfiles;for(u32 i=0;i<sizeof(*e);i++)((u8 *)e)[i]=0;copy(e->name,name);e->start_lba=at;e->generation=generation;
    e->uid=module_path(name)?AUTH_SYSTEM:auth_subject_uid(owner);e->gid=module_path(name)?AUTH_SYSTEM:auth_subject_gid(owner);e->mode=mode;e->flags=kind==2?1:0;
    nfiles++;if(write_meta()){nfiles--;return -1;}return 0;
}

static int permission(int uid,int gid,int owner,int group,u32 mode,u32 access)
{
    if(uid<-1)return 0;
    if(uid==AUTH_ROOT || uid==AUTH_SYSTEM)return 1;
    u32 shift=uid==owner?0:gid==group?2:4;
    return (((mode>>shift)&3u)&access)==access;
}
static int directory_permission(const char *path,int uid,int gid,u32 access)
{
    if(!path[0])return permission(uid,gid,root_uid,root_gid,root_mode,access);
    u32 info[2];if(fs_stat(path,info) || info[0]!=2)return 0;
    int index=find(path);
    if(index<0)return version<5 && permission(uid,gid,0,0,23,access);
    return permission(uid,gid,dirents[index].uid,dirents[index].gid,dirents[index].mode,access);
}
int fs_access_identity(const char *path,int uid,int gid,u32 access)
{
    char name[64],ancestor[64];if(!dir_sectors || fs_normalize(path,name)<0 || (access&~7u))return 0;
    if((access&FS_ACCESS_WRITE) && fs_faulted)return 0;
    if(!directory_permission("",uid,gid,FS_ACCESS_READ))return 0;
    u32 n=0,last=0;
    while(name[n]){
        if(name[n]=='/'){
            ancestor[n]=0;
            if(!directory_permission(ancestor,uid,gid,FS_ACCESS_READ))return 0;
            last=n+1;
        }
        ancestor[n]=name[n];n++;
    }
    if(!name[0])return directory_permission("",uid,gid,access&3);
    int index=find(name);
    if(index<0){
        /* 已经逐级检查目录r。查询缺失名字应让STAT返回不存在，
         * 不能误报权限拒绝，导致编辑器无法区分新文件与不可访问文件。
         * 这里只放行元数据查询，正文读/写仍按各自规则检查。 */
        if(access==FS_ACCESS_META)return 1;
        /* 新建检查直接父目录w，既有文件覆盖另外检查文件w。 */
        if(access!=FS_ACCESS_WRITE)return version<5 && (access==FS_ACCESS_READ || access==FS_ACCESS_META)
            && directory_permission(name,uid,gid,FS_ACCESS_READ);
        if(version<5)return 0;
        if(last)ancestor[last-1]=0;else ancestor[0]=0;
        return directory_permission(ancestor,uid,gid,FS_ACCESS_WRITE);
    }
    if((access&FS_ACCESS_WRITE) && version<5)return 0;
    if(access==FS_ACCESS_META)return 1;
    return permission(uid,gid,dirents[index].uid,dirents[index].gid,dirents[index].mode,access&3);
}
int fs_access(int pid,const char *path,u32 access)
{return fs_access_identity(path,auth_subject_uid(pid),auth_subject_gid(pid),access);}
int fs_access_parent(int pid,const char *path)
{
    char name[64];int n=fs_normalize(path,name);if(n<=0 || version<5)return 0;
    while(n && name[n-1]!='/')n--;if(n)name[n-1]=0;else name[0]=0;
    return fs_access(pid,name,FS_ACCESS_READ|FS_ACCESS_WRITE);
}
int fs_metadata(const char *path,u32 out[8])
{
    char name[64];u32 info[2];if(fs_normalize(path,name)<0 || fs_stat(name,info))return -1;
    for(u32 i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=info[0];out[2]=info[1];
    if(!name[0]){out[3]=(u32)root_uid;out[4]=(u32)root_gid;out[5]=root_mode;out[7]=1;return 0;}
    int index=find(name);if(index<0){out[5]=23;out[7]=1;return 0;}
    entry_t *e=&dirents[index];out[3]=(u32)e->uid;out[4]=(u32)e->gid;out[5]=e->mode;
    out[6]=e->flags;out[7]=e->generation;return 0;
}
int fs_permissions(int pid,const char *path,int uid,int gid,u32 mode)
{
    char name[64];if(version<5 || fs_normalize(path,name)<=0 || (mode&~63u))return -1;
    if(!fs_user_mutable(name,0) || !fs_access(pid,name,FS_ACCESS_META))return -5;
    int index=find(name);if(index<0)return -1;
    entry_t old=dirents[index];int caller=auth_uid(pid);
    if(caller!=old.uid && !auth_can_manage(pid))return -5;
    int changing_group=gid!=-2 && gid!=old.gid;
    if(uid==-2)uid=old.uid;if(gid==-2)gid=old.gid;
    if(uid<-1 || gid<-1 || (!auth_can_manage(pid) && (uid!=old.uid || (changing_group && gid!=auth_gid(pid)))))return -5;
    if(module_path(name) && (uid!=AUTH_SYSTEM || gid!=AUTH_SYSTEM))return -5;
    if((uid==AUTH_SYSTEM || gid==AUTH_SYSTEM) && !auth_can_mod(pid))return -5;
    dirents[index].uid=uid;dirents[index].gid=gid;dirents[index].mode=mode;
    dirents[index].generation=new_generation();if(!dirents[index].generation){dirents[index]=old;return -1;}
    if(write_meta()){dirents[index]=old;return -1;}return 0;
}

static transaction_t *stream(int owner,u32 token)
{
    if(!token)return 0;
    for(u32 i=0;i<FS_TRANSACTIONS;i++)if(transactions[i].token==token
        && transactions[i].owner==owner)return &transactions[i];
    return 0;
}
int fs_stream_begin(int owner,const char *path,u32 capacity)
{
    char name[64];u32 info[2];
    if(version!=5 || fs_faulted || capacity>fs_disk_sectors*512
        || fs_normalize(path,name)<=0 || protected(name) || busy(name))return -1;
    if(owner && (!fs_user_mutable(name,0) || !fs_access(owner,name,FS_ACCESS_WRITE)))return -5;
    if(!fs_stat(name,info) && info[0]!=1)return -1;
    /* 内部串口路径也必须有真实父目录，不能接收一个悬空后代使卷下次
     * 无法挂载。0不是三环能传入的owner，公开入口总取task_pid。 */
    char parent[64];copy(parent,name);int n=len(parent);
    while(n && parent[n-1]!='/')n--;if(n)parent[n-1]=0;else parent[0]=0;
    if(fs_stat(parent,info) || info[0]!=2)return -1;
    int index=find(name);if(index<0 && (u32)nfiles>=dir_capacity)return -1;
    transaction_t *t=0;for(u32 i=0;i<FS_TRANSACTIONS;i++)if(!transactions[i].token){t=&transactions[i];break;}
    if(!t || transaction_counter==0x7FFFFFFFu)return -6;
    u32 at=allocate((capacity+511)/512);if(!at)return -4;
    t->token=++transaction_counter;t->start=at;t->capacity=capacity;t->used=t->failed=0;
    t->original=index>=0?dirents[index].generation:0;t->owner=owner;
    t->uid=owner?auth_subject_uid(owner):AUTH_SYSTEM;
    t->gid=owner?auth_subject_gid(owner):AUTH_SYSTEM;copy(t->name,name);
    return (int)t->token;
}
int fs_stream_write(int owner,u32 token,const void *data,u32 length)
{
    transaction_t *t=stream(owner,token);if(!t)return -1;
    if(t->failed || fs_faulted || length>t->capacity-t->used){t->failed=1;return -1;}
    if(owner && (!fs_user_mutable(t->name,0) || !fs_access(owner,t->name,FS_ACCESS_WRITE)))return -5;
    const u8 *input=(const u8 *)data;u32 offset=0;
    while(offset<length){
        u32 position=t->used+offset,within=position&511,amount=512-within;
        if(amount>length-offset)amount=length-offset;
        if(within){if(ata_read_sectors(t->start+position/512,1,sector)){t->failed=1;return -1;}}
        else for(u32 k=0;k<512;k++)sector[k]=0;
        for(u32 k=0;k<amount;k++)sector[within+k]=input[offset+k];
        if(ata_write_sectors(t->start+position/512,1,sector)){t->failed=1;return -1;}
        offset+=amount;
    }
    t->used+=length;return (int)length;
}
int fs_stream_abort(int owner,u32 token)
{
    transaction_t *t=stream(owner,token);if(!t)return -1;
    allocated(t->start,(t->capacity+511)/512,0);t->token=0;return 0;
}
void fs_stream_stop(int owner)
{for(u32 i=0;i<FS_TRANSACTIONS;i++)if(transactions[i].token && transactions[i].owner==owner)fs_stream_abort(owner,transactions[i].token);}
int fs_stream_commit(int owner,u32 token)
{
    transaction_t *t=stream(owner,token);if(!t || t->failed || fs_faulted)return -1;
    if(owner && (!fs_user_mutable(t->name,0) || !fs_access(owner,t->name,FS_ACCESS_WRITE)))return -5;
    int index=find(t->name),fresh=index<0;
    if((!fresh && dirents[index].generation!=t->original) || (fresh && t->original))return -6;
    if(fresh && (u32)nfiles>=dir_capacity)return -4;
    u32 generation=new_generation();if(!generation)return -4;
    if(fresh)index=nfiles;
    entry_t previous=dirents[index];
    if(fresh){
        for(u32 k=0;k<sizeof(entry_t);k++)((u8 *)&dirents[index])[k]=0;
        dirents[index].uid=module_path(t->name)?AUTH_SYSTEM:t->uid;
        dirents[index].gid=module_path(t->name)?AUTH_SYSTEM:t->gid;
        dirents[index].mode=23;
    }
    copy(dirents[index].name,t->name);dirents[index].start_lba=t->start;
    dirents[index].size=t->used;dirents[index].generation=generation;
    if(fresh)nfiles++;
    if(write_meta()){if(fresh)nfiles--;dirents[index]=previous;return -1;}
    if(!fresh)allocated(previous.start_lba,(previous.size+511)/512,0);
    u32 live=(t->used+511)/512,reserved=(t->capacity+511)/512;
    allocated(t->start+live,reserved-live,0);int result=(int)t->used;t->token=0;highwater();return result;
}
