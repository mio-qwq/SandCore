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
#include "../user/configapi/SC32REPLACEWIRE.H"
#include "memory.h"
#include "auth.h"
#include "task.h"
#include "task_store.h"
#include "objpool.h"
#include "crc.h"
#include "sc32_file.h"
#include "fs_lease.h"
#define FS_LEGACY_SECTS 16384u
#define FS_MAX_SECTS 524288u
#define ALLOCATION_LEAF_SECTORS 512u

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
/* 每512扇区一个叶摘要，父节点合并前缀/后缀/最大连续空闲与总空闲。
 * 256MiB卷的树只需32KiB；找空闲区间O(log N)+最多512位，统计读根。
 * COW保留/提交/撤销都经allocated更新，不缓存尚未提交的目录内容。 */
typedef struct {u32 prefix,suffix,best,free;} space_node_t;
static space_node_t *space_tree;
static u32 space_pages,space_leaves,live_sectors,live_bytes;
static int live_dirty;
static void fs_space_rebuild(void);
static void live_refresh(void)
{
    if(!live_dirty)return;live_sectors=live_bytes=0;
    for(int i=0;i<nfiles;i++)if(dirents[i].start_lba){live_sectors+=(dirents[i].size+511)/512;live_bytes+=dirents[i].size;}
    live_dirty=0;
}
static u32 object_generation;
typedef struct {
    u32 token,start,capacity,used,original,failed;
    int owner,uid,gid;
    u32 owner_generation,previous,next;
    char name[64];
} transaction_t;
static objpool_t transaction_pool;
static u32 transaction_counter;
/* 活跃modern open使用独立身份，磁盘generation仍是内容/权限版本。
 * pin阻止旧replace/remove/rename接管同名对象；chmod不撤身份，逐次
 * 权限复验。候选只有内核extent/进度，绝不持有用户buffer跨陷入。 */
typedef struct {
    char name[64];entry_t candidate,previous,draft;
    u32 id,refs,ready,index,pending,writer,owner,owner_generation;
    u32 position,length,target,copy_sector,zero_offset,reserved_start,reserved_count;
    u32 relocated,complete,accepted,uncertain;
    u32 unpublished,invalidated,lease_head,flushed_revision;
    int result;
} native_file_t;
static objpool_t native_pool;
static u32 native_counter,native_meta_owner,native_meta_sector,published_object_generation;
static u32 native_meta_bank,native_meta_generation,native_meta_count,native_meta_sequence,native_meta_crc;
static u32 native_directory_sequence=1;
static u8 native_super[512];
enum { N_OPEN=1,N_WRITE,N_TRUNCATE,N_FLUSH };
#include "fs_lease_private.inc"
static int same(const char *a,const char *b);
static int child(const char *name,const char *parent);
static int native_busy(const char *name)
{
    for(int i=objpool_next(&native_pool,-1);i>=0;i=objpool_next(&native_pool,i)){
        native_file_t *n=objpool_get(&native_pool,i);
        if(n->refs && (same(name,n->name) || child(n->name,name)))return 1;
    }
    return 0;
}
static u32 new_generation(void)
{
    /* 持久化的单调序号防止删除后同名重建被旧文件句柄误认；耗尽即
     * 拒绝修改，不能在32位回绕时把旧对象再次当成同一个文件。 */
    if(object_generation==0xFFFFFFFFu || native_directory_sequence==0xffffffffu)return 0;
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
static int transaction_busy(const char *name)
{
    for(int i=objpool_next(&transaction_pool,-1);i>=0;i=objpool_next(&transaction_pool,i)){
        transaction_t *t=objpool_get(&transaction_pool,i);
        if(same(name,t->name) || child(t->name,name))return 1;
    }
    return 0;
}
static int busy(const char *name)
{return transaction_busy(name) || native_busy(name);}
static int protected(const char *name)
{ return same(name,"SYS/CORE") || child(name,"SYS/CORE") || same(name,"SYS/AUTH") || child(name,"SYS/AUTH")
    || same(name,"SYS/RECOVERY") || child(name,"SYS/RECOVERY")
    /* 系统SCL是用户态代码，但启动来源必须稳定。root绕过普通rw
     * 不等于可替换系统库；宿主发行包与可信零环更新沿原内部接口。 */
    || same(name,"SYS/LIB") || child(name,"SYS/LIB")
    || same(name,"SYS/SHELL") || child(name,"SYS/SHELL")
    || same(name,"SYS/UI/UAX15_1.SCU"); }
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
    if(fs_faulted || native_meta_owner || fs_lease_volume_locked() || native_directory_sequence==0xffffffffu)return -1;
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
        active_bank=next;metadata_generation=generation;published_object_generation=object_generation;native_directory_sequence++;return 0;
    }
    *(u32 *)(sector+12)=(u32)nfiles;
    if(ata_write_sectors(0,1,sector))return -1;
    int result=ata_write_sectors(1,dir_sectors,meta);if(!result)native_directory_sequence++;return result;
}
static void release_directory(void)
{
    lease_volume_reset();
    if(dirents)pframe_free_run((u32)dirents,dir_pages);
    if(meta)pframe_free_run((u32)meta,meta_pages);
    if(allocation)pframe_free_run((u32)allocation,allocation_pages);
    if(space_tree)pframe_free_run((u32)space_tree,space_pages);
    space_tree=0;space_pages=space_leaves=live_sectors=live_bytes=0;live_dirty=1;
    allocation=0;allocation_pages=fs_data_start=active_bank=metadata_generation=fs_faulted=0;
    object_generation=0;
    objpool_destroy(&native_pool);native_meta_owner=native_meta_sector=published_object_generation=0;
    objpool_destroy(&transaction_pool);
    for(int pid=task_next(-1);pid>=0;pid=task_next(pid))task_record(pid)->transactions=0;
    dirents=0;meta=0;dir_pages=meta_pages=0;
    nfiles=0;dir_sectors=version=fs_disk_sectors=dir_capacity=fs_data_end=0;
}
void fs_init(void)
{
    release_directory();
    if(objpool_init(&transaction_pool,sizeof(transaction_t),0) || objpool_init(&native_pool,sizeof(native_file_t),0))return;
    if(ata_read_sectors(0,1,sector)) return;
    const char *magic="SANDFSMIO";
    for(int i=0;i<9;i++) if(sector[i]!=(u8)magic[i]) return;
    u32 ds=*(u32 *)(sector+16),ver=*(u32 *)(sector+20),count=*(u32 *)(sector+12);
    if(!ds) ds=1;
    if(ver==5 && (ds==192 || ds==384 || ds==768 || ds==1536) && *(u32 *)(sector+28)==96
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
    if(ver==5){
        space_leaves=1;while(space_leaves<(sectors+511)/512)space_leaves<<=1;
        space_pages=(space_leaves*2*sizeof(space_node_t)+4095)/4096;
        space_tree=(space_node_t *)pframe_alloc_run(space_pages);if(!space_tree)goto failed;
        fs_space_rebuild();
    }
    published_object_generation=object_generation;live_dirty=1;return;
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
    live_refresh();out[15]=live_sectors;out[16]=live_bytes;
    /* 现有卷没有数据洞回收；不能用总减活跃字节冒充还能写的空间。
     * 防止损坏重叠记录让只读统计无符号下溢；此值不是分区号。 */
    u32 used=fs_data_end-fs_data_start;
    out[17]=used>=out[15]?used-out[15]:0;out[18]=version==5?2:1;
}

void fs_storage_info2(u32 out[16])
{
    for(u32 i=0;i<16;i++)out[i]=0;out[0]=1;out[1]=dir_sectors!=0;if(!dir_sectors)return;
    out[2]=version;out[3]=fs_disk_sectors;out[4]=fs_data_start;out[5]=fs_data_end;out[6]=dir_capacity;out[7]=(u32)nfiles;
    live_refresh();
    out[8]=live_sectors;
    if(version==5 && space_tree){
        out[10]=space_tree[1].free;out[11]=space_tree[1].best;
        u32 occupied=fs_disk_sectors-fs_data_start-out[10];out[9]=occupied>=out[8]?occupied-out[8]:0;
    }else out[10]=out[11]=fs_disk_sectors-fs_data_end;
    out[12]=object_generation;out[13]=active_bank;out[14]=metadata_generation;out[15]=(u32)fs_faulted;
}
static void space_leaf(u32 index)
{
    space_node_t *node=space_tree+space_leaves+index;node->prefix=node->suffix=node->best=node->free=0;
    u32 run=0;int initial=1;
    for(u32 i=0;i<512;i++){
        u32 at=index*512+i;int available=at>=fs_data_start && at<fs_disk_sectors && !(allocation[at>>3]&(1u<<(at&7)));
        if(available){node->free++;run++;if(initial)node->prefix++;if(run>node->best)node->best=run;}
        else {initial=0;run=0;}
    }node->suffix=run;
}
static void space_parent(u32 index,u32 span)
{
    space_node_t *node=space_tree+index,*left=space_tree+index*2,*right=left+1;u32 half=span/2;
    node->free=left->free+right->free;
    node->prefix=left->prefix==half?half+right->prefix:left->prefix;
    node->suffix=right->suffix==half?half+left->suffix:right->suffix;
    node->best=left->best>right->best?left->best:right->best;
    if(left->suffix+right->prefix>node->best)node->best=left->suffix+right->prefix;
}
static void fs_space_rebuild(void)
{
    for(u32 i=0;i<space_leaves;i++)space_leaf(i);
    for(u32 start=space_leaves/2,span=1024;start;start/=2,span*=2)
        for(u32 i=start;i<start*2;i++)space_parent(i,span);
}
static void allocated(u32 at,u32 count,int used)
{
    if(!allocation || !count)return;
    for(u32 i=0;i<count;i++){u32 n=at+i;u8 bit=(u8)(1u<<(n&7));if(used)allocation[n>>3]|=bit;else allocation[n>>3]&=(u8)~bit;}
    if(space_tree){u32 first=at/512,last=(at+count-1)/512;
        for(u32 i=first;i<=last;i++)space_leaf(i);
        first+=space_leaves;last+=space_leaves;
        for(u32 span=1024;first>1;span*=2){first/=2;last/=2;for(u32 i=first;i<=last;i++)space_parent(i,span);}
    }
    live_dirty=1;
}
static u32 allocate(u32 count)
{
    if(!count)return fs_data_start;
    if(!space_tree || space_tree[1].best<count)return 0;
    u32 index=1,start=0,span=space_leaves*512,selected=0;
    while(index<space_leaves){space_node_t *left=space_tree+index*2,*right=left+1;
        if(left->best>=count){index*=2;span/=2;continue;}
        if(left->suffix+right->prefix>=count){selected=start+span/2-left->suffix;break;}
        start+=span/2;span/=2;index=index*2+1;
    }
    if(!selected){u32 run=0;for(u32 i=0;i<span;i++){u32 at=start+i;
        if(at<fs_data_start || at>=fs_disk_sectors || allocation[at>>3]&(1u<<(at&7)))run=0;else run++;
        if(run==count){selected=at+1-count;break;}}}
    if(!selected)return 0;allocated(selected,count,1);return selected;
}
static void highwater(void)
{
    live_dirty=1;
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
    if(fs_normalize(path,name)<=0 || !dir_sectors || fs_faulted || size>fs_disk_sectors*512)return -1;
    if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(name,FS_LEASE_CONTENT) || busy(name))return -6;
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
        /* 目录事务会换入复用的物理页，空余槽不保证为零。新建对象必须
         * 初始化完整记录，不能把文件名NUL后的旧代码/数据留给另一写回路径。 */
        for(u32 k=0;k<sizeof(entry_t);k++)((u8 *)&dirents[i])[k]=0;
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
    for(u32 k=0;k<sizeof(dirents[i].name);k++)dirents[i].name[k]=0;
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
    if(native_meta_owner || fs_lease_volume_locked())return -6;
    char name[64]; u32 info[2];
    if(version<4 || fs_normalize(path,name)<=0 || !fs_stat(name,info)) return -1;
    if(fs_lease_path_blocked(name,FS_LEASE_TREE) || busy(name))return -6;
    if((u32)nfiles>=dir_capacity)return -1;
    for(int i=0;i<nfiles;i++) if(dirents[i].start_lba && child(name,dirents[i].name)) return -1;
    for(u32 k=0;k<sizeof(entry_t);k++)((u8 *)&dirents[nfiles])[k]=0;
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
    int i=find(name);if(i<0)return -1;if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(name,FS_LEASE_TREE) || busy(name))return -6;
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
    if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(old,FS_LEASE_TREE)
       || fs_lease_path_blocked(next,FS_LEASE_TREE) || busy(old) || busy(next))return -6;
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
            for(u32 k=0;k<sizeof(renamed[i].name);k++)renamed[i].name[k]=0;
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
    if(native_meta_owner || fs_lease_volume_locked())return -6;
    char name[64];u32 info[2];if(version!=5 || !dir_sectors || fs_faulted || (kind!=1 && kind!=2) || mode>63 || fs_normalize(path,name)<=0)return -1;
    if(!fs_user_mutable(name,0) || !fs_access_parent(owner,name))return -5;
    if(!fs_stat(name,info))return -8;if(fs_lease_path_blocked(name,FS_LEASE_TREE) || busy(name))return -6;if((u32)nfiles>=dir_capacity)return -4;
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
{if(auth_login_resource_access(pid,path,access))return 1;
 return fs_access_identity(path,auth_subject_uid(pid),auth_subject_gid(pid),access);}
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
    if(native_meta_owner || fs_lease_volume_locked())return -6;
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
    if(!token || token>0x7FFFFFFFu || !task_owned(owner))return 0;
    transaction_t *t=objpool_get(&transaction_pool,(int)token);
    return t && t->token==token && t->owner==owner && t->owner_generation==task_generation(owner)?t:0;
}
int fs_stream_snapshot(int owner,u32 token,u32 out[4])
{
    transaction_t *t=stream(owner,token);if(!t)return -8;
    out[0]=t->used;out[1]=t->capacity;out[2]=t->original;out[3]=t->failed|fs_faulted;return 0;
}
static void transaction_release(transaction_t *t)
{
    /* 临时扇区归属独立于PID编号。先从真实拥有者的链上撤销，
     * 退出只处理本人的事务，不在每个子进程结束时扫描全部写入者。 */
    if(task_owned(t->owner) && task_generation(t->owner)==t->owner_generation){
        if(t->previous)((transaction_t *)objpool_get(&transaction_pool,(int)t->previous))->next=t->next;
        else task_record(t->owner)->transactions=t->next;
        if(t->next)((transaction_t *)objpool_get(&transaction_pool,(int)t->next))->previous=t->previous;
    }
    objpool_release(&transaction_pool,(int)t->token);
}
int fs_stream_begin(int owner,const char *path,u32 capacity)
{
    char name[64];u32 info[2];
    if(!task_owned(owner) || version!=5 || fs_faulted || capacity>fs_disk_sectors*512
        || fs_normalize(path,name)<=0 || protected(name))return -1;
    if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(name,FS_LEASE_CONTENT) || busy(name))return -6;
    if(owner && (!fs_user_mutable(name,0) || !fs_access(owner,name,FS_ACCESS_WRITE)))return -5;
    if(!fs_stat(name,info) && info[0]!=1)return -1;
    /* 内部串口路径也必须有真实父目录，不能接收一个悬空后代使卷下次
     * 无法挂载。0不是三环能传入的owner，公开入口总取task_pid。 */
    char parent[64];copy(parent,name);int n=len(parent);
    while(n && parent[n-1]!='/')n--;if(n)parent[n-1]=0;else parent[0]=0;
    if(fs_stat(parent,info) || info[0]!=2)return -1;
    int index=find(name);if(index<0 && (u32)nfiles>=dir_capacity)return -1;
    if(transaction_counter==0x7FFFFFFFu)return -6;
    int token=(int)(transaction_counter+1);
    if(objpool_claim(&transaction_pool,token))return -4;
    transaction_t *t=objpool_get(&transaction_pool,token);
    u32 at=allocate((capacity+511)/512);if(!at){objpool_release(&transaction_pool,token);return -4;}
    t->token=++transaction_counter;t->start=at;t->capacity=capacity;t->used=t->failed=0;
    t->original=index>=0?dirents[index].generation:0;t->owner=owner;
    t->uid=owner?auth_subject_uid(owner):AUTH_SYSTEM;
    t->gid=owner?auth_subject_gid(owner):AUTH_SYSTEM;copy(t->name,name);
    t->owner_generation=task_generation(owner);t->next=task_record(owner)->transactions;
    if(t->next)((transaction_t *)objpool_get(&transaction_pool,(int)t->next))->previous=t->token;
    task_record(owner)->transactions=t->token;
    return (int)t->token;
}
int fs_stream_write(int owner,u32 token,const void *data,u32 length)
{
    transaction_t *t=stream(owner,token);if(!t)return -1;
    if(fs_lease_path_blocked(t->name,FS_LEASE_CONTENT))return -6;
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
    allocated(t->start,(t->capacity+511)/512,0);transaction_release(t);return 0;
}
void fs_stream_stop(int owner)
{
    if(!task_owned(owner))return;
    while(task_record(owner)->transactions){
        u32 token=task_record(owner)->transactions;
        if(fs_stream_abort(owner,token)){task_record(owner)->transactions=0;break;}
    }
}
int fs_stream_stop_step(int owner)
{
    if(!task_owned(owner) || !task_record(owner)->transactions)return 1;
    u32 token=task_record(owner)->transactions;
    if(fs_stream_abort(owner,token)){task_record(owner)->transactions=0;return 1;}
    return !task_record(owner)->transactions;
}
int fs_stream_commit(int owner,u32 token)
{
    transaction_t *t=stream(owner,token);if(!t || t->failed || fs_faulted)return -1;
    if(native_meta_owner || fs_lease_volume_locked() || fs_lease_path_blocked(t->name,FS_LEASE_CONTENT) || native_busy(t->name))return -6;
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
    allocated(t->start+live,reserved-live,0);int result=(int)t->used;transaction_release(t);highwater();return result;
}

/* 以下为独立modern文件底座。旧replace仍是全文件COW，旧fd仍按
 * generation失效；新接口不会把它们改造成另一种隐式事务。 */
static native_file_t *native_object(u32 id)
{
    if(!id || id>0x7fffffffu)return 0;
    native_file_t *n=objpool_get(&native_pool,(int)id);
    return n && n->id==id && n->refs?n:0;
}
static int native_index(native_file_t *n)
{
    if(n->invalidated)return FS_N_STALE;
    if(n->unpublished)return 0;
    int i=find(n->name);
    if(i<0 || !dirents[i].start_lba)return FS_N_STALE;
    /* 其它文件删除会压缩目录，不能永久缓存旧数组下标当对象身份。 */
    n->index=(u32)i;return i;
}
static int native_access(int pid,native_file_t *n,u32 access)
{
    if(!task_owned(pid) || !n || !n->ready || n->invalidated)return FS_N_STALE;
    if(n->unpublished)return lease_private_access(n,pid,access);
    if(native_index(n)<0)return FS_N_STALE;
    if(access&FS_NATIVE_WRITE){
        if(version!=5)return FS_N_NOT_SUPPORTED;
        if(fs_faulted)return FS_N_IO;
        if(!fs_user_mutable(n->name,0))return FS_N_ACCESS;
    }
    if(!fs_access(pid,n->name,access?access:FS_ACCESS_META))return FS_N_ACCESS;
    return 0;
}
int fs_native_info(int pid,u32 object,u32 access,fs_native_info_t *out)
{
    native_file_t *n=native_object(object);int error=native_access(pid,n,access);
    if(error)return error;
    entry_t *e=native_current(n);
    out->length=e->size;out->object=n->id;out->revision=e->generation;
    out->flags=(n->unpublished || e->generation>published_object_generation?FS_NATIVE_DIRTY:0)
        |(fs_faulted?FS_NATIVE_FAULTED:0)|(n->uncertain?FS_NATIVE_UNCERTAIN:0)
        |(n->pending && !n->complete?FS_NATIVE_GROWING:0);
    out->uid=e->uid;out->gid=e->gid;out->mode=e->mode;out->reserved=0;return 0;
}
static void native_pending_clear(native_file_t *n)
{
    n->pending=n->writer=n->owner=n->owner_generation=0;n->complete=0;n->result=0;
    n->position=n->length=n->target=n->copy_sector=n->zero_offset=0;
    n->reserved_start=n->reserved_count=n->relocated=n->accepted=0;
}
void fs_native_cancel(u32 object,u32 writer)
{
    native_file_t *n=native_object(object);if(!n || !n->pending || n->writer!=writer)return;
    if(n->unpublished){lease_private_discard(n);return;}
    if(native_meta_owner==n->id)native_meta_owner=native_meta_sector=0;
    /* 未发布候选只在内存位图保留。元数据已成功提交时reserved已清零，
     * 不能把关闭/退出伪装成回滚一个已经对其他句柄可见的写入。 */
    if(n->reserved_count)allocated(n->reserved_start,n->reserved_count,0);
    native_pending_clear(n);
}
void fs_native_close(u32 object,u32 writer)
{
    native_file_t *n=native_object(object);if(!n)return;
    fs_native_cancel(object,writer);
    if(--n->refs)return;
    if(native_meta_owner==n->id)native_meta_owner=native_meta_sector=0;
    if(n->reserved_count)allocated(n->reserved_start,n->reserved_count,0);
    objpool_release(&native_pool,(int)object);
}
static void native_completed(native_file_t *n)
{
    /* 元数据完成与用户领取分开：结果留在动态writer句柄，当前file
     * pending键立即可释放。否则并发append会等一个已经完成的ACK，
     * 或原调用会重放而重复append。这里绝不复制到用户out。 */
    if(sc32_file_complete((int)n->owner,n->owner_generation,n->writer,n->pending,
        n->position,n->length,n->id,n->result,n->result?0:n->accepted))native_pending_clear(n);
}
static void native_failed(native_file_t *n,int error,int uncertain)
{
    if(native_meta_owner==n->id)native_meta_owner=native_meta_sector=0;
    if(uncertain){n->uncertain=1;fs_faulted=1;}
    if(n->unpublished && n->reserved_count){lease_record_t *r=lease_get(n->lease_head);
        if(r){r->garbage_start=n->reserved_start;r->garbage_count=n->reserved_count;lease_work_add(r);}}
    else if(n->reserved_count)allocated(n->reserved_start,n->reserved_count,0);
    n->reserved_start=n->reserved_count=0;n->result=error;n->complete=1;
}
static void native_serialize(u8 *p,const entry_t *e)
{
    /* 磁盘名字是最长63B的NUL字符串，其余字节是规范零填充；内存记录
     * 可能来自旧接口或复用槽，不能按64B裸复制并给脏填充计算合法CRC。
     * 只处理这条记录的64B，不为每次事务清空整块大容量目录工作数组。 */
    u32 j=0;for(;j<63u && e->name[j];j++)p[j]=(u8)e->name[j];
    for(;j<64u;j++)p[j]=0;
    *(u32 *)(p+64)=e->start_lba;*(u32 *)(p+68)=e->size;
    *(i32 *)(p+72)=e->uid;*(i32 *)(p+76)=e->gid;
    *(u32 *)(p+80)=e->mode;*(u32 *)(p+84)=e->flags;*(u32 *)(p+88)=e->generation;
    *(u32 *)(p+92)=0;
}
static int native_publish_start(native_file_t *n)
{
    if(n->unpublished)return lease_private_commit(n);
    if(fs_lease_volume_locked())return FS_N_AGAIN;
    if(native_meta_owner)return native_meta_owner==n->id?FS_N_AGAIN:FS_N_BUSY;
    if(fs_faulted)return FS_N_IO;
    if(native_directory_sequence==0xffffffffu)return FS_N_RANGE;
    if(ata_read_sectors(0,1,native_super))return FS_N_IO;
    native_meta_count=(u32)nfiles+(!n->ready?1u:0u);
    if(native_meta_count>dir_capacity)return FS_N_NO_SPACE;
    for(u32 i=0;i<dir_sectors*512u;i++)meta[i]=0;
    for(u32 i=0;i<native_meta_count;i++)native_serialize(meta+i*96u,i==n->index?&n->candidate:dirents+i);
    native_meta_bank=active_bank^1u;native_meta_generation=metadata_generation+1u;
    if(!native_meta_generation)native_meta_generation=1;
    native_meta_sequence=object_generation;
    *(u32 *)(native_super+12)=native_meta_count;
    *(u32 *)(native_super+48)=native_meta_bank;*(u32 *)(native_super+52)=native_meta_generation;
    *(u32 *)(native_super+56)=native_meta_sequence;*(u32 *)(native_super+508)=crc32_update(0,native_super,508);
    /* bank快照只占已有内核meta缓冲。短锁冻结所有目录修改，正文
     * 读取与WM仍能运行；每次最多16扇区，绝不在一个gate写完整大bank。 */
    native_meta_owner=n->id;native_meta_sector=native_meta_crc=0;task_kernel_wake();return FS_N_AGAIN;
}
static int native_publish_step(native_file_t *n)
{
    if(n->complete)return n->result;
    if(native_meta_owner!=n->id)return FS_N_BUSY;
    int pid=(int)n->owner;
    if(lease_native_guard(n,pid,n->writer,FS_NATIVE_WRITE)){
        native_failed(n,FS_N_BUSY,0);native_completed(n);return FS_N_BUSY;
    }
    if(!task_owned(pid) || task_generation(pid)!=n->owner_generation || protected(n->name)
       || (module_path(n->name) && !auth_can_mod(pid))
       || (n->ready?!fs_access(pid,n->name,FS_ACCESS_WRITE):!fs_access_parent(pid,n->name))){
        native_failed(n,FS_N_ACCESS,0);native_completed(n);return FS_N_ACCESS;
    }
    if(native_meta_sector<dir_sectors){
        u32 count=dir_sectors-native_meta_sector;if(count>16)count=16;
        if(ata_write_sectors(1+native_meta_bank*dir_sectors+native_meta_sector,count,meta+native_meta_sector*512u)){
            native_failed(n,FS_N_IO,0);native_completed(n);return FS_N_IO;
        }
        /* CRC也按同一8KiB批量续进，不在一次陷入遍历整个最大目录。
         * 现CRC为查表算法，已写batch的数据在meta锁期间不变。 */
        native_meta_crc=crc32_update(native_meta_crc,meta+native_meta_sector*512u,count*512u);
        native_meta_sector+=count;task_kernel_wake();return FS_N_AGAIN;
    }
    /* 超级块错误是结果不明，保留卷故障状态；不说恢复旧内存等于
     * 磁盘回滚。启动时仍按旧v5 CRC和active-bank合同拒绝坏卷。 */
    *(u32 *)(native_super+44)=native_meta_crc;*(u32 *)(native_super+508)=crc32_update(0,native_super,508);
    if(ata_write_sectors(0,1,native_super)){native_failed(n,FS_N_IO,1);native_completed(n);return FS_N_IO;}
    if(!n->ready)nfiles++;
    dirents[n->index]=n->candidate;n->ready=1;
    u32 old=(n->previous.size+511u)/512u,next=(n->candidate.size+511u)/512u;
    if(n->previous.start_lba && n->previous.start_lba!=n->candidate.start_lba)allocated(n->previous.start_lba,old,0);
    else if(old>next)allocated(n->previous.start_lba+next,old-next,0);
    n->reserved_start=n->reserved_count=0;n->complete=1;n->result=0;
    active_bank=native_meta_bank;metadata_generation=native_meta_generation;
    published_object_generation=native_meta_sequence;native_meta_owner=native_meta_sector=0;native_directory_sequence++;highwater();
    task_notify_generation((int)n->owner,n->owner_generation,TASK_EVENT_STREAM);native_completed(n);return 0;
}
void fs_native_poll(void)
{
    native_file_t *n=native_object(native_meta_owner);if(!n)return;
    if(!task_owned((int)n->owner) || task_generation((int)n->owner)!=n->owner_generation || TASK(n->owner).state==2){
        fs_native_cancel(n->id,n->writer);return;
    }
    /* 用户可暂时停止续调，但不能因此无限期霸占全卷元数据锁。
     * 此阶段所有正文已在内核/磁盘，poll不借用户指针或切用户CR3。 */
    native_publish_step(n);
}
static int native_pending_match(native_file_t *n,int pid,u32 writer,u32 kind,u32 position,u32 length)
{
    return n->pending==kind && n->writer==writer && n->owner==(u32)pid
        && n->owner_generation==task_generation(pid) && n->position==position && n->length==length;
}
static void native_pending_start(native_file_t *n,int pid,u32 writer,u32 kind,u32 position,u32 length)
{
    n->pending=kind;n->writer=writer;n->owner=(u32)pid;n->owner_generation=task_generation(pid);
    n->position=position;n->length=length;n->complete=n->accepted=n->relocated=0;n->result=0;
    n->copy_sector=0;n->reserved_start=n->reserved_count=0;
    n->previous=n->candidate=*native_current(n);n->zero_offset=n->previous.size;
}
int fs_native_open(int pid,u32 writer,const char *name,u32 flags,u32 mode,u32 *object)
{
    if(!dir_sectors)return FS_N_IO;
    if(!task_owned(pid) || !writer || !name || !name[0] || (flags&~31u) || !(flags&3u) || mode>63u
       || ((flags&(FS_NATIVE_CREATE|FS_NATIVE_TRUNCATE|FS_NATIVE_APPEND)) && !(flags&FS_NATIVE_WRITE)))return FS_N_INVALID;
    if(fs_lease_volume_locked())return FS_N_AGAIN;
    if(lease_native_open_blocked(name,flags))return FS_N_BUSY;
    if(flags&FS_NATIVE_WRITE){if(version!=5)return FS_N_NOT_SUPPORTED;if(fs_faulted)return FS_N_IO;
        if(!fs_user_mutable(name,0))return FS_N_ACCESS;}
    native_file_t *n=*object?native_object(*object):0;
    if(n){
        if(!native_pending_match(n,pid,writer,N_OPEN,flags,mode))return FS_N_BUSY;
        int result=n->complete?n->result:native_publish_step(n);
        if(result!=FS_N_AGAIN && result!=FS_N_BUSY)native_pending_clear(n);
        return result;
    }
    if(transaction_busy(name))return FS_N_BUSY;
    int index=find(name),fresh=index<0;
    if(fresh && !(flags&FS_NATIVE_CREATE))return FS_N_NOT_FOUND;
    if(!fresh && !dirents[index].start_lba)return FS_N_INVALID;
    if(!fs_access(pid,name,FS_ACCESS_META))return FS_N_ACCESS;
    if(!fresh && !fs_access(pid,name,flags&3u))return FS_N_ACCESS;
    if(fresh && !fs_access_parent(pid,name))return FS_N_ACCESS;
    if(fresh && (u32)nfiles>=dir_capacity)return FS_N_NO_SPACE;
    if((fresh || (flags&FS_NATIVE_TRUNCATE)) && native_meta_owner)return FS_N_AGAIN;
    for(int i=objpool_next(&native_pool,-1);i>=0;i=objpool_next(&native_pool,i)){
        native_file_t *candidate=objpool_get(&native_pool,i);
        if(!same(name,candidate->name))continue;
        if(candidate->unpublished || candidate->invalidated)return FS_N_BUSY;
        if(!candidate->ready || candidate->pending)return FS_N_AGAIN;
        n=candidate;break;
    }
    if(n){if(n->refs==0xffffffffu)return FS_N_RANGE;n->refs++;}
    else{
        if(native_counter==0x7fffffffu)return FS_N_RANGE;
        u32 id=native_counter+1;if(objpool_claim(&native_pool,(int)id))return FS_N_MEMORY;
        n=objpool_get(&native_pool,(int)id);n->id=++native_counter;n->refs=1;copy(n->name,name);
    }
    n->index=fresh?(u32)nfiles:(u32)index;n->ready=!fresh;*object=n->id;
    if(!fresh && !(flags&FS_NATIVE_TRUNCATE))return 0;
    if(fresh){
        for(u32 i=0;i<sizeof(n->previous);i++)((u8 *)&n->previous)[i]=0;
        n->candidate=n->previous;copy(n->candidate.name,name);n->candidate.start_lba=fs_data_start;
        n->candidate.uid=module_path(name)?AUTH_SYSTEM:auth_subject_uid(pid);
        n->candidate.gid=module_path(name)?AUTH_SYSTEM:auth_subject_gid(pid);n->candidate.mode=mode;
        n->pending=N_OPEN;n->writer=writer;n->owner=(u32)pid;n->owner_generation=task_generation(pid);
        n->position=flags;n->length=mode;
    }else{
        native_pending_start(n,pid,writer,N_OPEN,flags,mode);
        n->candidate.size=0;n->candidate.start_lba=fs_data_start;
    }
    n->candidate.generation=new_generation();if(!n->candidate.generation){native_failed(n,FS_N_RANGE,0);return FS_N_RANGE;}
    int result=native_publish_start(n);
    if(result!=FS_N_AGAIN)native_failed(n,result,0);
    return result;
}
int fs_native_read(int pid,u32 object,void *out,u32 bytes,u32 offset,u32 *done)
{
    if(bytes>65536u){*done=0;return FS_N_RANGE;}
    *done=0;native_file_t *n=native_object(object);int error=native_access(pid,n,FS_NATIVE_READ);if(error)return error;
    entry_t *e=native_current(n);
    if(offset>=e->size || !bytes)return 0;
    u32 count=e->size-offset;if(count>bytes)count=bytes;
    while(*done<count){
        u32 pos=offset+*done,within=pos&511u,take=512u-within;if(take>count-*done)take=count-*done;
        if(ata_read_sectors(e->start_lba+pos/512u,1,sector))return FS_N_IO;
        for(u32 j=0;j<take;j++)((u8 *)out)[*done+j]=sector[within+j];*done+=take;
    }
    return 0;
}
static int native_growth_start(native_file_t *n,u32 target,int overwrite_old)
{
    u32 old=(n->previous.size+511u)/512u,next=(target+511u)/512u;n->target=target;
    if(next<=old && !overwrite_old){n->candidate.start_lba=n->previous.start_lba;return 0;}
    int adjacent=!overwrite_old;
    u32 tail=n->previous.start_lba+old,extra=next-old;
    if(tail>fs_disk_sectors || extra>fs_disk_sectors-tail)adjacent=0;
    /* 整叶使用现空闲摘要，只有至多两个边叶逐bit检查；不为大扩展
     * 在IF=0逐个扫描几十万扇区。空间只保留，不预分配整份文件RAM。 */
    for(u32 at=tail,left=extra;adjacent && left;){
        u32 count=512u-(at&511u);if(count>left)count=left;
        if(count==512u){if(space_tree[space_leaves+at/512u].free!=512u)adjacent=0;}
        else for(u32 i=0;adjacent && i<count;i++)if(allocation[(at+i)>>3]&(1u<<((at+i)&7)))adjacent=0;
        at+=count;left-=count;
    }
    if(adjacent){
        allocated(tail,extra,1);n->reserved_start=tail;n->reserved_count=extra;
        n->candidate.start_lba=n->previous.start_lba;n->copy_sector=old;
    }else{
        u32 at=allocate(next);if(!at)return FS_N_NO_SPACE;
        n->candidate.start_lba=at;n->reserved_start=at;n->reserved_count=next;n->relocated=1;
    }
    return 0;
}
static int native_growth_step(native_file_t *n)
{
    u32 budget=128,old=(n->previous.size+511u)/512u;
    /* 只因真实扩展才迁移extent，单步最多64KiB。已复制旧正文不被
     * 其它writer修改（pending键锁），无关文件与WM不需要等待这次复制。 */
    while(n->relocated && n->copy_sector<old && budget){
        if(ata_read_sectors(n->previous.start_lba+n->copy_sector,1,sector)
           || ata_write_sectors(n->candidate.start_lba+n->copy_sector,1,sector)){native_failed(n,FS_N_IO,0);return FS_N_IO;}
        n->copy_sector++;budget--;
    }
    while(n->zero_offset<n->target && budget){
        u32 pos=n->zero_offset,within=pos&511u,count=512u-within;
        if(count>n->target-pos)count=n->target-pos;
        if(within){if(ata_read_sectors(n->candidate.start_lba+pos/512u,1,sector)){native_failed(n,FS_N_IO,0);return FS_N_IO;}}
        else for(u32 j=0;j<512u;j++)sector[j]=0;
        for(u32 j=0;j<count;j++)sector[within+j]=0;
        if(ata_write_sectors(n->candidate.start_lba+pos/512u,1,sector)){
            native_failed(n,FS_N_IO,!n->relocated && pos/512u<old);return FS_N_IO;
        }
        n->zero_offset+=count;budget--;
    }
    return (n->relocated && n->copy_sector<old) || n->zero_offset<n->target?FS_N_AGAIN:0;
}
static int native_bytes_write(native_file_t *n,const void *data,u32 bytes,u32 offset,u32 *done,int candidate)
{
    *done=0;u32 at=candidate?n->candidate.start_lba:native_current(n)->start_lba;
    while(*done<bytes){
        u32 pos=offset+*done,within=pos&511u,count=512u-within;if(count>bytes-*done)count=bytes-*done;
        if(within || count<512u){if(ata_read_sectors(at+pos/512u,1,sector))return FS_N_IO;}
        for(u32 j=0;j<count;j++)sector[within+j]=((const u8 *)data)[*done+j];
        if(ata_write_sectors(at+pos/512u,1,sector)){
            /* 已完成prefix可计数，失败扇区不能推测是否部分落盘。原位
             * 正文有不确定影响时冻结卷，明确UNCERTAIN，不伪造rollback。 */
            if(!candidate || (!n->relocated && pos/512u<(n->previous.size+511u)/512u)){
                n->uncertain=1;fs_faulted=1;
            }
            return FS_N_IO;
        }
        *done+=count;
    }
    return 0;
}
int fs_native_write(int pid,u32 object,u32 writer,const void *data,u32 bytes,u32 offset,int append,u32 *done,u32 *position)
{
    *done=0;native_file_t *n=native_object(object);int error=native_access(pid,n,FS_NATIVE_WRITE);
    if(error){if(n && n->writer==writer && !n->complete)fs_native_cancel(object,writer);return error;}
    if(fs_lease_volume_locked())return FS_N_AGAIN;
    error=lease_native_guard(n,pid,writer,FS_NATIVE_WRITE);if(error)return error;
    entry_t *e=native_current(n);u32 pos=append?(n->pending?n->position:e->size):offset;*position=pos;
    if(!bytes)return 0;if(bytes>65536u || pos>fs_disk_sectors*512u || bytes>fs_disk_sectors*512u-pos)return FS_N_RANGE;
    if(n->pending){
        if(!native_pending_match(n,pid,writer,N_WRITE,pos,bytes))return n->writer!=writer?FS_N_AGAIN:FS_N_BUSY;
        if(n->complete){error=n->result;*done=error?0:n->accepted;native_pending_clear(n);return error;}
        if(native_meta_owner==n->id){
            error=native_publish_step(n);if(error==FS_N_AGAIN)return error;
            *done=error?0:n->accepted;native_pending_clear(n);return error;
        }
        if(native_meta_owner)return FS_N_AGAIN;
    }else{
        if(native_meta_owner)return FS_N_AGAIN;
        if(pos+bytes<=e->size){
            /* 为后续Flush的目录epoch还留一次提交空间，不能接受正文
             * 后才发现版本域已无法发布。两种计数均不回绕复用。 */
            if(native_directory_sequence>=0xfffffffeu)return FS_N_RANGE;
            u32 generation=new_generation();if(!generation)return FS_N_RANGE;
            error=native_bytes_write(n,data,bytes,pos,done,0);
            if(*done || n->uncertain){e->generation=generation;live_dirty=1;n->flushed_revision=0;
                if(!n->unpublished)native_directory_sequence++;}
            return error;
        }
        native_pending_start(n,pid,writer,N_WRITE,pos,bytes);
        error=native_growth_start(n,pos+bytes,pos<e->size);
        if(error){native_failed(n,error,0);native_pending_clear(n);return error;}
    }
    error=native_growth_step(n);if(error){if(error!=FS_N_AGAIN)native_pending_clear(n);return error;}
    u32 written=0;error=native_bytes_write(n,data,bytes,pos,&written,1);
    if(error){native_failed(n,error,n->uncertain);native_pending_clear(n);return error;}
    /* 候选正文已完成，之后只保留内核状态，不留data虚址。发布期间
     * caller可被调度，返回AGAIN代表owned操作未完成而非虚假写成功。 */
    n->accepted=written;n->candidate.size=n->target;
    n->candidate.uid=e->uid;n->candidate.gid=e->gid;n->candidate.mode=e->mode;
    n->candidate.generation=new_generation();if(!n->candidate.generation){native_failed(n,FS_N_RANGE,0);return FS_N_RANGE;}
    error=native_publish_start(n);if(error!=FS_N_AGAIN){native_failed(n,error,0);native_pending_clear(n);}return error;
}
int fs_native_truncate(int pid,u32 object,u32 writer,u32 length)
{
    native_file_t *n=native_object(object);int error=native_access(pid,n,FS_NATIVE_WRITE);
    if(error){if(n && n->writer==writer && !n->complete)fs_native_cancel(object,writer);return error;}
    if(fs_lease_volume_locked())return FS_N_AGAIN;
    error=lease_native_guard(n,pid,writer,FS_NATIVE_WRITE);if(error)return error;
    if(length>fs_disk_sectors*512u)return FS_N_RANGE;
    if(n->pending){
        if(!native_pending_match(n,pid,writer,N_TRUNCATE,0,length))return n->writer!=writer?FS_N_AGAIN:FS_N_BUSY;
        if(n->complete){error=n->result;native_pending_clear(n);return error;}
        if(native_meta_owner==n->id){error=native_publish_step(n);if(error!=FS_N_AGAIN)native_pending_clear(n);return error;}
        if(native_meta_owner)return FS_N_AGAIN;
    }else{
        if(native_meta_owner)return FS_N_AGAIN;
        if(length==native_current(n)->size)return 0;
        native_pending_start(n,pid,writer,N_TRUNCATE,0,length);
        if(length>n->previous.size){error=native_growth_start(n,length,0);if(error){native_failed(n,error,0);native_pending_clear(n);return error;}}
        else n->target=length;
    }
    if(length>n->previous.size){error=native_growth_step(n);if(error){if(error!=FS_N_AGAIN)native_pending_clear(n);return error;}}
    entry_t *e=native_current(n);n->candidate.size=length;if(!length)n->candidate.start_lba=fs_data_start;
    n->candidate.uid=e->uid;n->candidate.gid=e->gid;n->candidate.mode=e->mode;
    n->candidate.generation=new_generation();if(!n->candidate.generation){native_failed(n,FS_N_RANGE,0);return FS_N_RANGE;}
    error=native_publish_start(n);if(error!=FS_N_AGAIN){native_failed(n,error,0);native_pending_clear(n);}return error;
}
int fs_native_flush(int pid,u32 object,u32 writer)
{
    native_file_t *n=native_object(object);int error=native_access(pid,n,FS_NATIVE_WRITE);
    if(error){if(n && n->writer==writer && !n->complete)fs_native_cancel(object,writer);return error;}
    if(fs_lease_volume_locked())return FS_N_AGAIN;
    error=lease_native_guard(n,pid,writer,FS_NATIVE_WRITE);if(error)return error;
    if(n->unpublished){
        if(n->pending)return FS_N_BUSY;
        if(ata_flush())return FS_N_IO;
        n->flushed_revision=n->draft.generation;return 0;
    }
    if(n->pending){
        if(!native_pending_match(n,pid,writer,N_FLUSH,0,0))return n->writer!=writer?FS_N_AGAIN:FS_N_BUSY;
        if(n->complete){error=n->result;native_pending_clear(n);return error;}
        error=native_publish_step(n);if(error!=FS_N_AGAIN)native_pending_clear(n);return error;
    }
    if(native_meta_owner)return FS_N_AGAIN;
    if(dirents[n->index].generation<=published_object_generation)return ata_flush()?FS_N_IO:0;
    native_pending_start(n,pid,writer,N_FLUSH,0,0);
    error=native_publish_start(n);if(error!=FS_N_AGAIN){native_failed(n,error,0);native_pending_clear(n);}return error;
}
u32 fs_native_directory_epoch(void){return native_directory_sequence;}
int fs_native_directory_read(int pid,const char *name,u32 epoch,u32 cursor,fs_native_row_t *out,u32 *next)
{
    if(epoch!=fs_native_directory_epoch())return FS_N_STALE;
    if(!fs_access(pid,name,FS_ACCESS_READ))return FS_N_ACCESS;
    u32 info[2];if(fs_stat(name,info) || info[0]!=2)return FS_N_NOT_FOUND;
    if(cursor>=(u32)nfiles){*next=0xffffffffu;return 1;}
    int prefix=len(name)+(name[0]!=0);
    for(u32 i=cursor;i<(u32)nfiles;i++){
        if(!child(dirents[i].name,name))continue;
        const char *p=dirents[i].name+prefix;u32 count=0;while(p[count] && p[count]!='/')count++;
        if(version==5 && p[count])continue; /* v5有显式父目录，不回看去重。 */
        if(version<5){
            int repeated=0;
            for(u32 j=0;j<i && !repeated;j++)if(child(dirents[j].name,name)){
                const char *q=dirents[j].name+prefix;u32 k=0;
                while(k<count && fold(q[k])==fold(p[k]))k++;
                if(k==count && (!q[k] || q[k]=='/'))repeated=1;
            }
            if(repeated)continue;
        }
        for(u32 k=0;k<64u;k++)out->name[k]=k<count?p[k]:0;
        out->kind=p[count] || !dirents[i].start_lba?2u:1u;out->length=out->kind==2?0:dirents[i].size;
        out->generation=dirents[i].generation;out->uid=dirents[i].uid;out->gid=dirents[i].gid;out->mode=dirents[i].mode;
        *next=i+1u;return 0;
    }
    *next=0xffffffffu;return 1;
}

#include "fs_lease.inc"

#include "fs_path_query.inc"
