/* M10a1最小启动器。只有只读ATA/SandFS定位、SHA/格式边界、交接和诊断。
 * 不链接调度、完整FS、权限、桌面、音频、网络与SCAPI，也不写数据盘。
 * 正式磁盘核心丢失/损坏时尝试独立SYS/RECOVERY/CORE.SKM，始终重启更新。
 * CRC/SHA仅检测损坏，不是扩展Ed25519认证，不能把二者混作信任依据。 */
#include "../kernel/io.h"
#include "../kernel/ata.h"
#include "../kernel/crc.h"
#include "../kernel/crypto.h"
#include "../kernel/core_image.h"
#include "../kernel/palette.h"
#include "font8_data.h"

typedef struct {u16 low,selector;u8 zero,attributes;u16 high;} gate_t;
static gate_t idt[32];
static struct __attribute__((packed)){u16 limit;u32 base;} idtr;
static u8 sector[512],super[512],cache[512];
static u32 fs_version,entry_bytes,directory_lba,directory_sectors,directory_count,data_start,volume_sectors;
static u32 volume_generation,cache_lba;
static int cache_valid;
static u32 file_cache_lba;static int file_cache_valid;
static core_image_t header;
static u32 screen_progress;
extern void loader_fault(void);
extern void loader_jump(u32 entry) __attribute__((noreturn));
void loader_fail(u32 error) __attribute__((noreturn));

static void serial_init(void)
{
    outb(0x3F9,0);outb(0x3FB,0x80);outb(0x3F8,1);outb(0x3F9,0);
    outb(0x3FB,3);outb(0x3FA,0xC7);outb(0x3FC,3);
}
static void serial_text(const char *text)
{
    while(*text){u32 budget=65536;while(budget && !(inb(0x3FD)&0x20))budget--;
        if(!budget)return;outb(0x3F8,(u8)*text++);}
}
static void serial_number(u32 value)
{char number[12];u32 n=0;do{number[n++]=(char)('0'+value%10);value/=10;}while(value);while(n){char c[2]={number[--n],0};serial_text(c);}}
static void palette_slot(u8 slot,u8 r,u8 g,u8 b)
{outb(0x3C8,slot);outb(0x3C9,r>>2);outb(0x3C9,g>>2);outb(0x3C9,b>>2);}
static void rectangle(int x,int y,int w,int h,u8 color)
{volatile u8 *v=(volatile u8 *)0xA0000;for(int r=0;r<h;r++)for(int c=0;c<w;c++)v[(y+r)*320+x+c]=color;}
static void text(int x,int y,const char *message,u8 color)
{
    volatile u8 *v=(volatile u8 *)0xA0000;
    while(*message && x<=312){u8 ch=(u8)*message++;if(ch<32 || ch>126)ch='?';
        for(int r=0;r<8;r++)for(int c=0;c<8;c++)if(sc_font[ch-32][r]&(0x80u>>c))v[(y+r)*320+x+c]=color;
        x+=8;}
}
static void screen(const char *message,int failed)
{
    /* 沿GFX既有深靛/青/米白槽位，阴影、面板和单行状态保留清晰层次。
     * RGB端点与main.c的权威槽初始化一致，未增加任何应用裸色号。 */
    palette_slot(PAL_CON_BG,16,18,30);palette_slot(PAL_UI_PANEL,24,32,48);
    palette_slot(PAL_UI_LINE,53,73,91);palette_slot(PAL_CON_TINT,110,200,190);
    palette_slot(PAL_TITLE,235,232,220);palette_slot(PAL_UI_MUTED,110,137,153);
    palette_slot(PAL_CON_WARN,235,180,80);palette_slot(PAL_SHADOW,25,16,20);
    rectangle(0,0,320,200,PAL_CON_BG);rectangle(26,55,272,104,PAL_SHADOW);
    rectangle(22,51,272,104,PAL_UI_PANEL);rectangle(22,51,3,104,failed?PAL_CON_WARN:PAL_CON_TINT);
    text(38,69,"SANDCORE",PAL_TITLE);text(38,94,message,failed?PAL_CON_WARN:PAL_UI_MUTED);
    rectangle(38,116,238,3,PAL_UI_LINE);rectangle(38,116,(int)screen_progress,3,PAL_CON_TINT);
    text(38,136,failed?"Recovery media required":"Shift: recovery core",PAL_UI_MUTED);
}
void loader_fail(u32 error)
{
    serial_text("CORE LOADER FAIL ");serial_number(error);serial_text("\r\n");screen("Core cannot be loaded",1);
    for(;;){cli();halt();}
}
static void early_idt(void)
{
    for(u32 i=0;i<32;i++){u32 address=(u32)loader_fault;idt[i]=(gate_t){(u16)address,8,0,0x8E,(u16)(address>>16)};}
    idtr.limit=sizeof(idt)-1;idtr.base=(u32)idt;__asm__ __volatile__("lidt %0"::"m"(idtr):"memory");
    outb(0x21,255);outb(0xA1,255); /* loader无IRQ服务，不靠未设置IDT开中断。 */
}
static u32 word(const u8 *p)
{return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static int equal(const char *a,const char *b)
{
    for(u32 n=0;n<64;n++){u8 x=(u8)a[n],y=(u8)b[n];if(x>='a' && x<='z')x-=32;if(y>='a' && y<='z')y-=32;
        if(x!=y)return 0;if(!x)return 1;}return 0;
}
static int directory_open(void)
{
    if(ata_read_sectors(0,1,super))return -1;
    const char *magic="SANDFSMIO";for(u32 i=0;i<9;i++)if(super[i]!=(u8)magic[i])return -1;
    fs_version=word(super+20);directory_sectors=word(super+16);directory_count=word(super+12);
    if(!directory_sectors)directory_sectors=1;
    u32 bank=0;
    if(fs_version==5){
        if((directory_sectors!=192 && directory_sectors!=384 && directory_sectors!=768 && directory_sectors!=1536)
            || word(super+28)!=96 || word(super+508)!=crc32_update(0,super,508))return -1;
        bank=word(super+48);volume_generation=word(super+52);if(bank>1 || !volume_generation)return -1;entry_bytes=96;
    }else if(fs_version==4 && (directory_sectors==32 || directory_sectors==80))entry_bytes=72;
    else if(fs_version<=3 && (directory_sectors==1 || directory_sectors==8))entry_bytes=40;
    else return -1;
    if(directory_count>directory_sectors*512/entry_bytes)return -1;
    volume_sectors=fs_version>=4?word(super+24):0;if(!volume_sectors)volume_sectors=16384;
    data_start=1+directory_sectors*(fs_version==5?2u:1u);directory_lba=1+bank*directory_sectors;
    if(volume_sectors<data_start || volume_sectors>524288u || volume_sectors>ata_sector_count())return -1;
    /* CRC流式算整张活动目录。只读两扇缓存，不为字体/第三方目录预留大表。
     * bank不在读错时偷偷切换，超级块是唯一提交点，旧bank可能已过期。 */
    if(fs_version==5){u32 crc=0;
        for(u32 i=0;i<directory_sectors;i++){if(ata_read_sectors(directory_lba+i,1,sector))return -1;crc=crc32_update(crc,sector,512);}
        if(crc!=word(super+44))return -1;
    }
    cache_valid=0;return 0;
}
static int directory_entry(u32 index,u8 out[96])
{
    u32 at=index*entry_bytes;
    for(u32 i=0;i<entry_bytes;i++){
        u32 lba=directory_lba+(at+i)/512;
        if(!cache_valid || cache_lba!=lba){if(ata_read_sectors(lba,1,cache))return -1;cache_lba=lba;cache_valid=1;}
        out[i]=cache[(at+i)&511u];
    }
    return 0;
}
static int locate(const char *path,u32 *start,u32 *bytes)
{
    u8 entry[96];u32 namesize=fs_version>=4?64:32,found=0;
    for(u32 i=0;i<directory_count;i++){
        if(directory_entry(i,entry))return -1;
        if(!entry[0] || entry[namesize-1])return -1;
        if(!equal((const char *)entry,path))continue;
        if(found++)return -1; /* 同名主核全部拒绝，不凭目录先后选择。 */
        u32 lba=word(entry+namesize),size=word(entry+namesize+4);
        if(!size || size>CORE_IMAGE_HEADER+CORE_IMAGE_MAX || lba<data_start || lba>=volume_sectors
            || (size+511)/512>volume_sectors-lba)return -1;
        if(fs_version==5 && (word(entry+72)!=0xFFFFFFFFu || word(entry+76)!=0xFFFFFFFFu
            || word(entry+80)&~63u || word(entry+84) || !word(entry+88) || word(entry+92)))return -1;
        *start=lba;*bytes=size;
    }
    if(found!=1)return -1;
    /* 主核的保护不能只靠路径名：若另一普通文件指向同一扇区，三环写入
     * 那个别名就能改核心。只与选中的范围比较，保持O(N)两遍读取，
     * 不把完整FS的全目录对象/两两重叠验证塞进最小loader。 */
    u32 end=*start+(*bytes+511)/512;
    for(u32 i=0;i<directory_count;i++){
        if(directory_entry(i,entry))return -1;
        if(equal((const char *)entry,path))continue;
        u32 lba=word(entry+namesize),size=word(entry+namesize+4);
        if(!size)continue;
        if(lba<data_start || lba>=volume_sectors || size>volume_sectors*512u
            || (size+511)/512>volume_sectors-lba)return -1;
        if(lba<end && lba+(size+511)/512>*start)return -1;
    }
    return 0;
}
static int file_read(u32 start,u32 size,u32 offset,void *out,u32 bytes)
{
    if(offset>size || bytes>size-offset)return -1;
    u8 *target=out;
    while(bytes){u32 within=offset&511u,count=512-within;if(count>bytes)count=bytes;
        u32 lba=start+offset/512;
        if(!file_cache_valid || file_cache_lba!=lba){if(ata_read_sectors(lba,1,sector))return -1;file_cache_lba=lba;file_cache_valid=1;}
        for(u32 i=0;i<count;i++)target[i]=sector[within+i];offset+=count;target+=count;bytes-=count;
    }
    return 0;
}
static int available(u32 base,u32 bytes)
{
    const volatile u32 *map=(const volatile u32 *)0x7E04;u32 count=*(volatile u32 *)0x7E00;
    if(!count || count>32 || !bytes || bytes>CORE_IMAGE_LIMIT-base)return 0;
    u32 end=base+bytes;int found=0;
    for(u32 i=0;i<count;i++){
        const volatile u32 *e=map+i*6;
        if(e[1])continue;
        u64 hi=(u64)e[0]+((u64)e[3]<<32)+e[2];
        if(e[4]==1 && !e[5]){if(e[0]<=base && hi>=end)found=1;}
        else if((u64)e[0]<end && hi>base)return 0;
    }
    return found;
}
static int load_core(const char *path,u32 flags)
{
    u32 start=0,size=0;if(locate(path,&start,&size) || file_read(start,size,0,&header,sizeof(header)))return -1;
    const char *magic="SKM2MIO";for(u32 i=0;i<8;i++)if(header.magic[i]!=(u8)magic[i])return -2;
    if(header.version!=CORE_IMAGE_VERSION || header.header_bytes!=CORE_IMAGE_HEADER || header.kind!=CORE_IMAGE_MAIN
        || header.flags || header.number || header.abi_min!=1 || header.abi_max!=1 || header.required_cpu
        || header.relocations || header.load_base!=CORE_IMAGE_BASE || !header.image_bytes || header.image_bytes>CORE_IMAGE_MAX
        || header.entry_offset>=header.image_bytes || header.bss_offset<header.image_bytes || header.bss_offset&4095u
        || header.bss_offset>CORE_IMAGE_LIMIT-CORE_IMAGE_BASE || header.bss_bytes>CORE_IMAGE_LIMIT-CORE_IMAGE_BASE-header.bss_offset
        || header.memory_bytes!=header.bss_offset+header.bss_bytes || header.file_bytes!=size
        || size!=CORE_IMAGE_HEADER+header.image_bytes || header.header_crc!=crc32_update(0,&header,124))return -2;
    for(u32 i=0;i<5;i++)if(header.reserved[i])return -2;
    if(!available(header.load_base,header.memory_bytes))return -3;
    u8 *target=(u8 *)header.load_base;sha256_t hash;sha256_init(&hash);u32 crc=0;
    for(u32 offset=0;offset<header.image_bytes;){
        u32 n=header.image_bytes-offset;if(n>512)n=512;
        if(file_read(start,size,CORE_IMAGE_HEADER+offset,target+offset,n))return -4;
        sha256_update(&hash,target+offset,n);crc=crc32_update(crc,target+offset,n);offset+=n;
        u32 progress=offset*238u/header.image_bytes; /* 4MiB×238仍在u32内，不引入64位除法运行库。 */
        if(progress!=screen_progress){screen_progress=progress;rectangle(38,116,(int)progress,3,PAL_CON_TINT);}
    }
    u8 digest[32];sha256_final(&hash,digest);if(!crypto_equal(digest,header.sha256,32))return -5;
    for(u32 i=header.image_bytes;i<header.memory_bytes;i++)target[i]=0;
    core_boot_t *info=(core_boot_t *)CORE_BOOT_ADDRESS;crypto_zero(info,sizeof(*info));
    info->magic=CORE_BOOT_MAGIC;info->version=1;info->size=sizeof(*info);info->flags=flags;
    info->disk_sectors=ata_sector_count();info->fs_version=fs_version;info->loader_version=CORE_LOADER_VERSION;
    info->load_base=header.load_base;info->image_bytes=header.image_bytes;info->bss_offset=header.bss_offset;
    info->bss_bytes=header.bss_bytes;info->memory_bytes=header.memory_bytes;info->entry=header.load_base+header.entry_offset;
    info->image_crc=crc;info->volume_generation=volume_generation;
    for(u32 i=0;i<32;i++)info->sha256[i]=digest[i];for(u32 i=0;path[i] && i<63;i++)info->path[i]=path[i];
    info->e820_address=0x7E00;info->e820_count=*(volatile u32 *)0x7E00;info->crc=crc32_update(0,info,252);
    serial_text("CORE START ");serial_text(path);serial_text("\r\n");loader_jump(info->entry);
}
void loader_main(void)
{
    early_idt();serial_init();screen("Reading disk core",0);serial_text("SandCore loader v1\r\n");
    ata_init();if(!ata_sector_count())loader_fail(1);
    if(directory_open())loader_fail(2);
    int forced=(*(volatile u8 *)0x417&3u)!=0;
    int error=forced?-1:load_core("SYS/CORE/CORE.SKM",0);
    serial_text("PRIMARY REJECT ");serial_number((u32)(0-error));serial_text("\r\n");
    screen_progress=0;screen("Loading recovery core",0);
    error=load_core("SYS/RECOVERY/CORE.SKM",CORE_BOOT_RECOVERY|(forced?CORE_BOOT_REQUESTED:0));
    serial_text("RECOVERY REJECT ");serial_number((u32)(0-error));serial_text("\r\n");loader_fail(3);
}
