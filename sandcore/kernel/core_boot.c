#include "core_image.h"
#include "crc.h"
#include "memory.h"
#include "interrupts.h"

static core_boot_t snapshot;
static int accepted;
static void handoff_failure(void) __attribute__((noreturn));
static void handoff_failure(void)
{
    /* 显存/IDT/调度尚未初始化，不能用依赖正常桌面的panic_screen。
     * loader已配置COM1；这里只有有界原始诊断与停机，不触碰私有页。 */
    const char *message="CORE HANDOFF INVALID\r\n";
    while(*message){u32 budget=65536;while(budget && !(inb(0x3FD)&0x20))budget--;
        if(!budget)break;outb(0x3F8,(u8)*message++);}
    for(;;){cli();halt();}
}
void core_boot_accept(u32 handoff,u32 address)
{
    if(handoff!=CORE_BOOT_HANDOFF || address!=CORE_BOOT_ADDRESS)handoff_failure();
    const core_boot_t *source=(const core_boot_t *)address;
    if(source->magic!=CORE_BOOT_MAGIC || source->version!=1 || source->size!=sizeof(*source)
        || source->load_base!=CORE_IMAGE_BASE || source->memory_bytes>CORE_IMAGE_LIMIT-CORE_IMAGE_BASE
        || !source->image_bytes || source->image_bytes>CORE_IMAGE_MAX
        || source->bss_offset<source->image_bytes || source->bss_offset>source->memory_bytes
        || source->bss_bytes!=source->memory_bytes-source->bss_offset
        || source->entry<source->load_base || source->entry-source->load_base>=source->image_bytes
        || source->e820_address!=0x7E00u || !source->e820_count || source->e820_count>32
        || source->e820_count!=*(volatile u32 *)0x7E00 || source->bss_offset&4095u
        || source->reserved0 || source->flags&~3u || source->path[63]
        || source->crc!=crc32_update(0,source,sizeof(*source)-4))handoff_failure();
    for(u32 i=0;i<21;i++)if(source->reserved[i])handoff_failure();
    snapshot=*source;accepted=1;
}
void core_boot_reserve(void)
{
    /* 位图已经初始化，任何堆/页表/设备申请之前先标主核所有实际页。
     * 旧单体入口不会调用accept，原低2MiB保留方式继续有效。 */
    if(accepted)pf_reserve(snapshot.load_base,snapshot.memory_bytes);
}
int core_boot_info(core_boot_t *out)
{if(!accepted || !out)return -1;*out=snapshot;return 0;}
