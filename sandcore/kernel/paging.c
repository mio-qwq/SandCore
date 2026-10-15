/* =====================================================================
 *  SandCore 分页子系统 (kernel/paging.c)
 *  ---------------------------------------------------------------------
 *  【M8模型】两级页表 (4KB 页)。内核页目录恒等映射完整 4GB 地址域
 *  (内核代码/栈/堆/显存/BSS 全在其中, 分页开启瞬间运行环境不变)。
 *  每个用户任务一份页目录: 拷贝内核共享表项；PT#1覆盖旧映像/栈
 *  0x400000-0x7FFFFF，高端0x40000000-0x43FFFFFF为懒分配图形堆。
 *  M10a2另在0x50000000..0x53FFFFFF装载私有SCL映像；三区均使用
 *  任务私有页表，中间物理RAM别名保持supervisor —— 这样:
 *    * 所有程序都用 org 0x400000 链接, 装载时映射到各自物理帧;
 *    * 换任务 = 换 CR3 (私有区域随之切换);
 *    * 装载时经物理恒等映射写帧，系统调用则沿用当前 CR3 访问用户虚址。
 *  恒等映射表项不带 US：ring3 只能访问自己映像与已分配堆页，不能借恒等别名
 *  读写内核或别的应用。内核仍能经物理地址访问任意用户帧。
 * ===================================================================== */
#include "io.h"
#include "paging.h"
#include "memory.h"

#define PT_PER_DIR 1024
#define PTE_P  0x001
#define PTE_RW 0x002
#define PTE_US 0x004
#define PTE_BLOCK_FIRST 0x200u
#define PTE_BLOCK_LAST  0x400u
#define PTE_SHARED 0x800u
/* x86 的9..11位供操作系统使用。块首/块尾记录在私有PTE中，而不是
 * 用户可改写的堆头；因此用户不能伪造长度去释放邻块或内核物理帧。 */

static u32 kernel_pd[PT_PER_DIR] __attribute__((aligned(4096)));
static int paging_on;

static u32 pt_alloc(void)         /* 从物理分配器拿一页做页表 (恒等可达) */
{
    u32 p = pframe_alloc();
    if (!p) return 0;
    u32 *t = (u32 *)p;
    for (u32 i = 0; i < 1024; i++)
        t[i] = 0;
    return p;
}

void paging_init(void)
{
    /* 1024 个目录项 × 1024 个 4KB 页 = 4GB，恒等映射同时覆盖 MMIO。
     * 映射存在不代表 RAM 存在；物理分配器仍只按 E820 可用项分配帧。
     * 1024 张共享页表占 3MB-7MB，main.c 已预留 3MB-8MB。 */
    u32 pt_region = 0x300000u;
    for (u32 i = 0; i < PT_PER_DIR; i++) {
        u32 pt = pt_region + i * PAGE_SIZE;
        kernel_pd[i] = pt | PTE_P | PTE_RW;
        u32 *t = (u32 *)pt;
        for (u32 e = 0; e < 1024; e++)
            t[e] = (i * 0x400000u + e * PAGE_SIZE) | PTE_P | PTE_RW;
    }
    paging_switch((u32)kernel_pd);

    u32 cr0;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;                    /* CR0.PG */
    __asm__ __volatile__("mov %0, %%cr0" :: "r"(cr0) : "memory");
    paging_on = 1;
}

u32 paging_kernel_pd(void)
{
    return (u32)kernel_pd;
}

void paging_switch(u32 pd)
{
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(pd) : "memory");
}

int paging_map_device(u32 base,u32 bytes)
{
    if(!paging_on || !bytes || (base&4095u) || (bytes&4095u) || bytes>0xFFFFFFFFu-base)return -1;
    u32 end=base+bytes;
    if(base<0x10000000u || (base<USER_HEAP_TOP && end>USER_HEAP_BASE)
        || (base<USER_MODULE_TOP && end>USER_MODULE_BASE)
        || (base<USER_SHARED_TOP && end>USER_SHARED_BASE))return -1;
    for(u32 i=0;i<e820_count();i++){
        e820_entry_t *entry=e820_entry(i);if(entry->type_lo!=E820_TYPE_USABLE || entry->type_hi)continue;
        u64 lo=((u64)entry->base_hi<<32)|entry->base_lo;
        u64 length=((u64)entry->len_hi<<32)|entry->len_lo,hi=lo+length;
        if(hi<lo || (lo<(u64)end && hi>(u64)base))return -1;
    }
    for(u32 address=base;address<end;address+=4096u){
        u32 *pt=(u32 *)(kernel_pd[address>>22]&~4095u);
        pt[(address>>12)&1023u]=address|PTE_P|PTE_RW|0x018u;
        __asm__ __volatile__("invlpg (%0)"::"r"(address):"memory");
    }
    return 0;
}

u32 paging_new_task_dir(void)
{
    u32 pd = pt_alloc();                   /* 页目录 (新页, 全零) */
    if (!pd) return 0;
    u32 user_pt = pt_alloc();              /* 私有用户区页表 */
    if (!user_pt) { pframe_free(pd); return 0; }
    u32 *d = (u32 *)pd;
    for (u32 i = 0; i < PT_PER_DIR; i++)
        d[i] = kernel_pd[i];               /* 共享项照抄 (含恒等映射) */
    d[USER_BASE / 0x400000u] = user_pt | PTE_P | PTE_RW | PTE_US;
    /* 高端堆是尚未映射的私有领地。保留低端物理恒等别名十分重要：
     * 内核的原生后台/窗口页可能落在8MB之后，扩大低端用户区会让它们
     * 在调用者CR3中变成另一段用户页，内核拷帧就会写错物理内存。 */
    for(u32 i=USER_HEAP_BASE>>22;i<USER_HEAP_TOP>>22;i++) d[i]=0;
    for(u32 i=USER_MODULE_BASE>>22;i<USER_MODULE_TOP>>22;i++) d[i]=0;
    for(u32 i=USER_SHARED_BASE>>22;i<USER_SHARED_TOP>>22;i++) d[i]=0;
    return pd;
}

void paging_map_user(u32 pd, u32 vaddr, u32 paddr)
{
    u32 *d = (u32 *)pd;
    u32 pt = d[USER_BASE / 0x400000u] & ~0xFFFu;   /* 私有用户页表 */
    u32 idx = (vaddr - USER_BASE) / PAGE_SIZE;
    ((u32 *)pt)[idx] = paddr | PTE_P | PTE_RW | PTE_US;
}

static int user_range(u32 pd,u32 ptr,u32 size,int write)
{
    /* 先用“剩余区间长度”比较 size，不能先求 ptr+size：恶意长度会使
     * 32 位加法回绕，从而把越界缓冲误判为合法。size=0 无需读任何页。
     * 后续首末页都算包含端点，确认所有跨越的 PTE 同时带 P 与 US，
     * 写入另查目录项和页表项的RW；只读数据仍能作为系统调用输入。
     * syscall 在中断门内 IF=0，同任务不会在校验后突然卸掉这些页。 */
    u32 top;
    if(ptr>=USER_BASE && ptr<USER_TOP) top=USER_TOP;
    else if(ptr>=USER_HEAP_BASE && ptr<USER_HEAP_TOP) top=USER_HEAP_TOP;
    else if(ptr>=USER_MODULE_BASE && ptr<USER_MODULE_TOP) top=USER_MODULE_TOP;
    else if(ptr>=USER_SHARED_BASE && ptr<USER_SHARED_TOP) top=USER_SHARED_TOP;
    else return 0;
    if(size>top-ptr || !pd || pd==(u32)kernel_pd) return 0;
    if (!size) return 1;
    u32 required=PTE_P|PTE_US|(write?PTE_RW:0);
    u32 *d = (u32 *)pd;
    for(u32 va=ptr&~0xFFFu;va<ptr+size;va+=PAGE_SIZE) {
        if((d[va>>22]&required)!=required) return 0;
        u32 *pt=(u32 *)(d[va>>22]&~0xFFFu);
        if((pt[(va>>12)&1023]&required)!=required) return 0;
    }
    return 1;
}
int paging_user_range(u32 pd,u32 ptr,u32 size)
{return user_range(pd,ptr,size,0);}
int paging_user_write_range(u32 pd,u32 ptr,u32 size)
{
    /* 旧SCX与图形堆仍是RW。新增只读SCL页后，内核不能仅凭US校验
     * 就替用户写入只读代码；CR0.WP未全局开启，物理别名也始终可写。
     * 输入仍走读校验，允许库的只读字符串/表格作为合法syscall参数。 */
    return user_range(pd,ptr,size,1);
}

void paging_free_task_dir(u32 pd)
{
    /* 共享内核 PT 的所有者是整个系统，绝不能跟着某个进程退出而释放。
     * 只遍历带US的私有表项：PTE记录映像、栈、图形堆和模块的物理帧，
     * 因而即使装载到一半失败，也能用同一个函数完整回滚已分配部分。
     * 调用方必须保证 pd 不再是当前执行地址空间；内核目录额外拒绝释放。 */
    if (!pd || pd == (u32)kernel_pd) return;
    /* 只有带US的私有目录项能释放。共享supervisor页表永远不会被
     * 某个应用的退出牵连；高端堆的懒分配页表也在同一条回收链中。 */
    u32 *d=(u32 *)pd;
    for(u32 index=0;index<1024;index++) {
        if((d[index]&(PTE_P|PTE_US))!=(PTE_P|PTE_US)) continue;
        u32 *pt=(u32 *)(d[index]&~0xFFFu);
        /* Broker必须先撤映射。防御性跳过Shared标签，不能把仍由
         * 其它recipient使用的PF按私有页doublefree；PT本身仍归本人。 */
        for(u32 i=0;i<1024;i++)if((pt[i]&PTE_P) && !(pt[i]&PTE_SHARED))pframe_free(pt[i]&~0xFFFu);
        pframe_free((u32)pt);
    }
    pframe_free(pd);
}

int paging_user_copy(u32 pd,u32 address,void *buffer,u32 size,int write)
{
    if(!user_range(pd,address,size,write)) return -1;
    u8 *bytes=buffer;
    /* 按页分块，而不是每字节重新走页表。调试读保留“整区先检查”
     * 的失败原子性，同时也能合法检查跨4MB页表边界的图形堆。 */
    for(u32 done=0;done<size;) {
        u32 va=address+done;
        u32 *pt=(u32 *)(((u32 *)pd)[va>>22]&~0xFFFu);
        u8 *physical=(u8 *)((pt[(va>>12)&1023]&~0xFFFu)+(va&0xFFFu));
        u32 count=PAGE_SIZE-(va&0xFFFu);
        if(count>size-done) count=size-done;
        for(u32 i=0;i<count;i++) {
            if(write) physical[i]=bytes[done+i]; else bytes[done+i]=physical[i];
        }
        done+=count;
    }
    return 0;
}
int paging_debug_patch(u32 pd,u32 address,u8 value)
{
    if(!paging_user_range(pd,address,1))return -1;
    u32 *pt=(u32 *)(((u32 *)pd)[address>>22]&~0xFFFu);
    if(pt[(address>>12)&1023]&PTE_SHARED)return -1;
    u8 *physical=(u8 *)((pt[(address>>12)&1023]&~0xFFFu)+(address&0xFFFu));
    *physical=value;return 0;
}

static u32 *private_pte(u32 pd,u32 address,int create)
{
    u32 *d=(u32 *)pd,index=address>>22;
    if(!(d[index]&PTE_P)) {
        if(!create) return 0;
        u32 pt=pt_alloc();
        if(!pt) return 0;
        d[index]=pt|PTE_P|PTE_RW|PTE_US;
    }
    /* 只允许访问本任务的私有表，不能把恒等映射共享表改成用户页。 */
    if(!(d[index]&PTE_US))return 0;
    return (u32 *)(d[index]&~0xFFFu)+((address>>12)&1023);
}
static void private_flush(u32 pd)
{
    u32 active;
    __asm__ __volatile__("mov %%cr3,%0":"=r"(active));
    if(active==pd) paging_switch(pd);
}
static void private_trim(u32 pd,u32 first,u32 top)
{
    /* 不保留空页表。否则反复申请/释放跨多个4MB区段后，即使像素页
     * 全部归还，物理统计仍会缓慢增长，退出前的资源核对也无法收敛。 */
    u32 *d=(u32 *)pd;
    for(u32 index=first>>22;index<top>>22;index++) {
        if((d[index]&(PTE_P|PTE_US))!=(PTE_P|PTE_US)) continue;
        u32 *pt=(u32 *)(d[index]&~0xFFFu); int used=0;
        /* 非present的Shared占位也是活reservation。只看P会把另一
         * 尚在分批bind的mapping页表提前释放，导致候选虚址相互覆盖。 */
        for(u32 i=0;i<1024;i++)if(pt[i]&(PTE_P|PTE_SHARED)){used=1;break;}
        if(!used) { d[index]=0; pframe_free((u32)pt); }
    }
    private_flush(pd);
}
static u32 private_alloc(u32 pd,u32 bytes,u32 first,u32 top)
{
    if(!pd || pd==(u32)kernel_pd || !bytes || bytes>top-first) return 0;
    u32 pages=(bytes+PAGE_SIZE-1)/PAGE_SIZE,run=0,base=0;
    /* 只要求虚拟地址连续，物理页可以碎片化；图像解码不应因某个
     * 相邻任务占了一个物理帧就无法申请本来足够大的像素缓冲。 */
    for(u32 va=first;va<top;va+=PAGE_SIZE) {
        u32 *pte=private_pte(pd,va,0);
        if(pte && (*pte&PTE_P)) run=0;
        else if(++run==pages) { base=va-(pages-1)*PAGE_SIZE; break; }
    }
    if(!base) return 0;
    u32 done=0;
    for(;done<pages;done++) {
        u32 *pte=private_pte(pd,base+done*PAGE_SIZE,1);
        if(!pte) break;
        u32 physical=pframe_alloc();
        if(!physical) break;
        for(u32 i=0;i<PAGE_SIZE/4;i++) ((u32 *)physical)[i]=0;
        *pte=physical|PTE_P|PTE_RW|PTE_US;
    }
    if(done!=pages) {
        /* 尚未对外公布块首，因此失败只能按已成功页数内部回滚。
         * 所有像素页、包括失败时新建的空页表，都必须归还。 */
        for(u32 i=0;i<done;i++) {
            u32 *pte=private_pte(pd,base+i*PAGE_SIZE,0);
            pframe_free(*pte&~0xFFFu); *pte=0;
        }
        private_trim(pd,first,top); return 0;
    }
    *private_pte(pd,base,0)|=PTE_BLOCK_FIRST;
    *private_pte(pd,base+(pages-1)*PAGE_SIZE,0)|=PTE_BLOCK_LAST;
    private_flush(pd); return base;
}
static int private_free(u32 pd,u32 base,u32 first_address,u32 top)
{
    if(!pd || pd==(u32)kernel_pd || (base&0xFFFu)
       || base<first_address || base>=top) return -1;
    u32 *first=private_pte(pd,base,0);
    if(!first || (*first&(PTE_P|PTE_BLOCK_FIRST))!=(PTE_P|PTE_BLOCK_FIRST)) return -1;
    u32 end=base;
    /* 先确认完整块尾，才开始释放；即使内部元数据不一致也不释放
     * 半块。另一块的首标志会阻断扫描，绝不串进下一个合法分配。 */
    for(;end<top;end+=PAGE_SIZE) {
        u32 *pte=private_pte(pd,end,0);
        if(!pte || !(*pte&PTE_P) || (end!=base && (*pte&PTE_BLOCK_FIRST))) return -1;
        if(*pte&PTE_BLOCK_LAST) break;
    }
    if(end==top) return -1;
    for(u32 va=base;va<=end;va+=PAGE_SIZE) {
        u32 *pte=private_pte(pd,va,0); pframe_free(*pte&~0xFFFu); *pte=0;
    }
    private_trim(pd,first_address,top); return 0;
}
#include "paging_shared.inc"

u32 paging_user_alloc(u32 pd,u32 bytes)
{return private_alloc(pd,bytes,USER_HEAP_BASE,USER_HEAP_TOP);}
int paging_user_free(u32 pd,u32 base)
{return private_free(pd,base,USER_HEAP_BASE,USER_HEAP_TOP);}
u32 paging_module_alloc(u32 pd,u32 bytes)
{return private_alloc(pd,bytes,USER_MODULE_BASE,USER_MODULE_TOP);}
int paging_module_free(u32 pd,u32 base)
{return private_free(pd,base,USER_MODULE_BASE,USER_MODULE_TOP);}
u32 paging_module_size(u32 pd,u32 base)
{
    if(!pd || pd==(u32)kernel_pd || (base&4095u)
        || base<USER_MODULE_BASE || base>=USER_MODULE_TOP)return 0;
    u32 *first=private_pte(pd,base,0);
    if(!first || (*first&(PTE_P|PTE_BLOCK_FIRST))!=(PTE_P|PTE_BLOCK_FIRST))return 0;
    for(u32 va=base;va<USER_MODULE_TOP;va+=PAGE_SIZE){
        u32 *pte=private_pte(pd,va,0);
        if(!pte || !(*pte&PTE_P) || (va!=base && (*pte&PTE_BLOCK_FIRST)))return 0;
        if(*pte&PTE_BLOCK_LAST)return va-base+PAGE_SIZE;
    }
    return 0;
}
int paging_module_protect(u32 pd,u32 base,u32 bytes,u32 access)
{
    /* 单CPU陷入期间IF=0：先检查整段，任何坏页都不能留下半次保护。
     * 只改RW位，内核块首/尾和物理帧归属原样保留；不按用户头猜长度。
     * 该接口限模块窗口。代码与数据必须由装载器分别页对齐，不能用
     * 一页混合区的“只读代码”宣传掩盖同页数据要求可写的事实。 */
    if((access!=1 && access!=3) || !bytes || (base&4095u) || (bytes&4095u)
        || base<USER_MODULE_BASE || base>=USER_MODULE_TOP || bytes>USER_MODULE_TOP-base
        || !paging_user_range(pd,base,bytes))return -1;
    for(u32 va=base;va<base+bytes;va+=PAGE_SIZE){
        u32 *pte=private_pte(pd,va,0);
        *pte=(*pte&~PTE_RW)|(access==3?PTE_RW:0);
    }
    private_flush(pd);return 0;
}
