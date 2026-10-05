/* mio：开机 SKM 加载器。执行前验证整文件、重定位表与装载区边界，
 * 只认约定目录与完整魔数，不能把 .SCX 或任意数据当零环代码运行。
 * 装载区由 PF 位图提前预留，所有任务映射均为 supervisor 恒等映射，
 * 三环既看不到模块内容，也不能通过自己的页表映射覆盖它。 */
#include "module.h"
#include "fs.h"
#include "gfx.h"
#include "wm.h"
#include "memory.h"
#include "auth.h"
#include "task.h"
#include "timer.h"
#include "process.h"

static u8 stage[131072+8192*4+32];
static u32 cursor;
static void (*scene_callback)(void);
static int ephemeral,registration_failed;
static u32 active_owner,scene_owner;
static struct {u32 address,pages,owner;} allocations[64];
static char resident_names[32][64];
static u32 resident_count;
static int same_name(const char *a,const char *b)
{
    while(*a && *b){char x=*a++,y=*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return 0;}
    return !*a && !*b;
}
u32 modules_loaded,modules_failed;   /* 无头验收读取真实装载结果 */
static void install_scene(void (*paint)(void))
{if(ephemeral)registration_failed=1;else {scene_callback=paint;scene_owner=active_owner;}}
static int screen_width(void){return GFX_W;}
static int screen_height(void){return GFX_H;}
static void *allocate_bytes(u32 bytes)
{
    if(!active_owner || !bytes || bytes>0x1000000u)return 0;
    for(u32 i=0;i<64;i++)if(!allocations[i].address){
        u32 pages=(bytes+4095)/4096,address=pframe_alloc_run(pages);if(!address)return 0;
        allocations[i].address=address;allocations[i].pages=pages;allocations[i].owner=active_owner;
        for(u32 j=0;j<pages*4096;j++)((u8 *)address)[j]=0;return (void *)address;
    }return 0;
}
static void release_bytes(void *address,u32 bytes)
{
    if(!bytes || bytes>0x1000000u)return;
    for(u32 i=0;i<64;i++)if(allocations[i].address==(u32)address && allocations[i].owner==active_owner
        && allocations[i].pages==(bytes+4095)/4096){
        pframe_free_run((u32)address,allocations[i].pages);allocations[i].address=0;return;
    }
}
static void reclaim(u32 owner)
{
    /* 一次性模块即使忘记释放辅助缓冲，也不能把页泄漏到下一次调试。
     * 仅追踪通过API申请的资源；最高权限零环程序仍不是沙箱。 */
    for(u32 i=0;i<64;i++)if(allocations[i].address && allocations[i].owner==owner){
        pframe_free_run(allocations[i].address,allocations[i].pages);allocations[i].address=0;
    }
}
static u32 ticks(void){return sc_ticks;}
static const module_api_t api={1,sizeof(module_api_t),gfx_pset,gfx_fill,gfx_circle,wm_wallpaper,install_scene,screen_width,screen_height,
    allocate_bytes,release_bytes,ticks,fs_read_at,fs_write};

static int load(const char *name,int resident)
{
    if(memory_managed_bytes()+0x200000u<MODULE_BASE+MODULE_BYTES) return -1;
    u32 info[2];
    if(fs_stat(name,info) || info[0]!=1 || info[1]>sizeof(stage)) return -1;
    int n=fs_read(name,stage,sizeof(stage));
    if(n<32 || (u32)n!=info[1]) return -1;
    const char *magic="SKM1MIO";
    for(int i=0;i<8;i++) if(stage[i]!=(u8)magic[i]) return -1;
    u32 entry=*(u32 *)(stage+8),size=*(u32 *)(stage+12),bss=*(u32 *)(stage+16);
    u32 reloc=*(u32 *)(stage+20),abi=*(u32 *)(stage+24);
    if(!size || size>131072 || bss>131072 || entry>=size || reloc>8192 || abi!=1
       || *(u32 *)(stage+28)!=0x004F494Du || (u32)n!=32+size+reloc*4) return -1;
    u32 total=(size+bss+4095)&~4095u;
    if(resident && (total>MODULE_BYTES-cursor || resident_count>=32)) return -1;
    u32 base=resident?MODULE_BASE+cursor:pframe_alloc_run(total/4096);
    if(!base)return -4;
    /* 先验证全部重定位再拷贝/执行，重复项会加基址两次，必须拒绝。
     * 生成端排序去重，读取端也不能把磁盘内容当作可信工具输出。 */
    u32 *fixes=(u32 *)(stage+32+size);
    for(u32 i=0;i<reloc;i++) {
        if(size<4 || fixes[i]>size-4 || (i && fixes[i]<fixes[i-1]+4)) goto bad;
        if(*(u32 *)(stage+32+fixes[i])>=size+bss) goto bad;
    }
    for(u32 i=0;i<total;i++) ((u8 *)base)[i]=i<size?stage[32+i]:0;
    for(u32 i=0;i<reloc;i++) *(u32 *)(base+fixes[i])+=base;
    /* 初始化失败撤回场景注册，先前已成功模块的回调不应被坏模块覆盖。
     * 入口本身有零环权限；真正运行时的异常由 CPL0 panic 诊断。 */
    void (*previous)(void)=scene_callback;u32 previous_owner=scene_owner;
    ephemeral=!resident;registration_failed=0;active_owner=resident?resident_count+1:33;
    /* 固定任务和CR3后开硬件中断，让外部COM1能暂停真实入口现场。
     * 模块初始化不调用调度；共享重定位暂存不能被第二个任务重入。 */
    task_render_hold(1);sti();
    int result=((int (*)(const module_api_t *))(base+entry))(&api);
    cli();task_render_hold(0);ephemeral=0;
    if(result || registration_failed){reclaim(active_owner);active_owner=0;scene_callback=previous;scene_owner=previous_owner;
        if(!resident)pframe_free_run(base,total/4096);return result?result:-5;}
    if(resident){
        cursor+=total;u32 i=0;while(name[i] && i<63){resident_names[resident_count][i]=name[i];i++;}
        resident_names[resident_count++][i]=0;
    }else {reclaim(active_owner);pframe_free_run(base,total/4096);}active_owner=0;
    return 0;
bad:
    if(!resident)pframe_free_run(base,total/4096);return -1;
}
static int prefix(const char *s,const char *p)
{
    while(*p) {
        char c=*s++; if(c>='a' && c<='z') c=c-'a'+'A';
        if(c!=*p++) return 0;
    }
    return 1;
}
void modules_init(void)
{
    cursor=resident_count=0; modules_loaded=modules_failed=0; scene_callback=0;ephemeral=0;active_owner=scene_owner=0;
    for(u32 i=0;i<64;i++)allocations[i].address=0;
    const char *dirs[2]={"SYS/CORE/","SYS/MOD/"};
    for(int d=0;d<2;d++) for(int i=0;i<fs_count();i++) {
        const char *name=fs_name(i); int n=0;
        while(name[n]) n++;
        if(n<4 || !prefix(name,dirs[d]) || !prefix(name+n-4,".SKM")) continue;
        if(load(name,1)) modules_failed++; else modules_loaded++;
    }
}
int modules_paint(void)
{
    if(!scene_callback) return 0;
    active_owner=scene_owner;scene_callback();active_owner=0;return 1;
}
int modules_execute(int pid,const char *path,int resident)
{
    if((resident!=0 && resident!=1) || !auth_can_mod(pid))return -5;
    char name[64];if(fs_normalize(path,name)<=0 || !fs_access(pid,name,FS_ACCESS_READ))return -1;
    /* 常驻不热重载。路径大小写别名统一比较；删除/改盘文件也不会
     * 释放运行地址，已安装回调归常驻区直到重启。 */
    if(resident)for(u32 i=0;i<resident_count;i++)if(same_name(name,resident_names[i]))return -6;
    return load(name,resident);
}
void modules_info(u32 out[8])
{for(u32 i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=resident_count;out[2]=cursor;out[3]=MODULE_BYTES;out[4]=modules_loaded;out[5]=modules_failed;}
int modules_list(int pid,char *out,u32 capacity)
{
    if(!auth_can_mod(pid))return -5;u32 needed=1;for(u32 i=0;i<resident_count;i++){u32 n=0;while(resident_names[i][n])n++;needed+=n+1;}
    if(capacity<needed)return -1;u32 at=0;for(u32 i=0;i<resident_count;i++){u32 n=0;while(resident_names[i][n])out[at++]=resident_names[i][n++];out[at++]='\n';}
    out[at]=0;return (int)at;
}
