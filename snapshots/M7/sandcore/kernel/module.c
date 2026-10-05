/* mio：开机 SKM 加载器。执行前验证整文件、重定位表与装载区边界，
 * 只认约定目录与完整魔数，不能把 .SCX 或任意数据当零环代码运行。
 * 装载区由 PF 位图提前预留，所有任务映射均为 supervisor 恒等映射，
 * 三环既看不到模块内容，也不能通过自己的页表映射覆盖它。 */
#include "module.h"
#include "fs.h"
#include "gfx.h"
#include "wm.h"
#include "memory.h"

static u8 stage[131072+8192*4+32];
static u32 cursor;
static void (*scene_callback)(void);
u32 modules_loaded,modules_failed;   /* 无头验收读取真实装载结果 */
static void install_scene(void (*paint)(void)) { scene_callback=paint; }
static const module_api_t api={1,sizeof(module_api_t),gfx_pset,gfx_fill,gfx_circle,wm_wallpaper,install_scene};

static int load(const char *name)
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
    if(total>MODULE_BYTES-cursor) return -1;
    u32 base=MODULE_BASE+cursor;
    /* 先验证全部重定位再拷贝/执行，重复项会加基址两次，必须拒绝。
     * 生成端排序去重，读取端也不能把磁盘内容当作可信工具输出。 */
    u32 *fixes=(u32 *)(stage+32+size);
    for(u32 i=0;i<reloc;i++) {
        if(size<4 || fixes[i]>size-4 || (i && fixes[i]<fixes[i-1]+4)) return -1;
        if(*(u32 *)(stage+32+fixes[i])>=size+bss) return -1;
    }
    for(u32 i=0;i<total;i++) ((u8 *)base)[i]=i<size?stage[32+i]:0;
    for(u32 i=0;i<reloc;i++) *(u32 *)(base+fixes[i])+=base;
    /* 初始化失败撤回场景注册，先前已成功模块的回调不应被坏模块覆盖。
     * 入口本身有零环权限；真正运行时的异常由 CPL0 panic 诊断。 */
    void (*previous)(void)=scene_callback;
    int result=((int (*)(const module_api_t *))(base+entry))(&api);
    if(result) { scene_callback=previous; return -1; }
    cursor+=total; return 0;
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
    cursor=0; modules_loaded=modules_failed=0; scene_callback=0;
    const char *dirs[2]={"SYS/CORE/","SYS/MOD/"};
    for(int d=0;d<2;d++) for(int i=0;i<fs_count();i++) {
        const char *name=fs_name(i); int n=0;
        while(name[n]) n++;
        if(n<4 || !prefix(name,dirs[d]) || !prefix(name+n-4,".SKM")) continue;
        if(load(name)) modules_failed++; else modules_loaded++;
    }
}
int modules_paint(void)
{
    if(!scene_callback) return 0;
    scene_callback(); return 1;
}
