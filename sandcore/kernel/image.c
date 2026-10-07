/* =====================================================================
 * mio：M8原始位图及SCX图标容器的严格读取。
 *
 * 【分开容器与像素】同一SCB2可独立上盘也可嵌在SCX正文之后；调用方
 * 明确给出区间，解析器不越过该区间寻找“碰巧能用”的头或忽略尾字节。
 * 【为何只做原始位图】压缩图片是用户输入且解码复杂，把它放三环才能
 * 使用私有堆和异常隔离；内核这里只承担可审计的小格式与资源生命周期。
 * ===================================================================== */
#include "image.h"
#include "fs.h"
#include "memory.h"
#include "display.h"
#include "palette.h"
#include "timer.h"

static u32 le32(const u8 *p)
{return (u32)p[0]|((u32)p[1]<<8)|((u32)p[2]<<16)|((u32)p[3]<<24);}
static int magic(const u8 *p,const char *m)
{for(int i=0;i<8;i++)if(p[i]!=(u8)m[i])return 0;return 1;}
int image_info(const char *path,u32 offset,u32 length,int icon,image_info_t *out)
{
    u32 stat[2];u8 h[32];
    if(!out || fs_stat(path,stat) || stat[0]!=1 || offset>stat[1]
       || length>stat[1]-offset || length<24)return -1;
    u32 count=length<32?length:32;
    if(fs_read_at(path,h,count,offset)!=(int)count)return -1;
    u32 w=le32(h+8),height=le32(h+12),format=le32(h+16),head;
    /* 限制后再乘：最大1920*1080*4，所有加法均远小于u32上限。
     * 图标的128上限也使懒缓存有明确的最坏页数，不跟照片一起膨胀。 */
    if(!w || !height || w>1920 || height>1080 || (icon && (w>128 || height>128)))return -1;
    u32 bytes=w*height;
    if(magic(h,"SCB1MIO") && format==1 && le32(h+20)==0x004F494Du)head=24;
    else if(length>=32 && magic(h,"SCB2MIO") && format==2 && !le32(h+20)
            && le32(h+24)==bytes*4 && le32(h+28)==0x004F494Du){head=32;bytes*=4;}
    else return -1;
    if(length!=head+bytes)return -1;
    *out=(image_info_t){w,height,format,bytes,offset+head};return 0;
}
void image_release(image_surface_t *surface)
{
    if(surface->pixels)pframe_free_run((u32)surface->pixels,surface->pages);
    *surface=(image_surface_t){0};
}
void image_reader_cancel(image_reader_t *reader)
{
    image_release(&reader->candidate);*reader=(image_reader_t){0};
}
int image_reader_begin(image_reader_t *reader,const char *path,u32 offset,u32 length,int icon)
{
    image_reader_cancel(reader);
    if(fs_normalize(path,reader->path)<=0 || fs_metadata(reader->path,reader->metadata)
       || reader->metadata[1]!=1 || offset>reader->metadata[2]
       || length>reader->metadata[2]-offset)return -1;
    reader->offset=offset;reader->length=length;reader->icon=icon;reader->phase=1;return 0;
}
int image_reader_step(image_reader_t *reader,u32 budget,image_surface_t *out)
{
    u32 metadata[8],started=sc_ticks;
    if(!reader->phase || !budget || !out)return -1;
    if(fs_metadata(reader->path,metadata))goto failed;
    for(u32 i=0;i<8;i++)if(metadata[i]!=reader->metadata[i])goto failed;
    if(reader->phase==1){
        if(image_info(reader->path,reader->offset,reader->length,reader->icon,&reader->info))goto failed;
        reader->candidate.width=reader->info.width;reader->candidate.height=reader->info.height;
        reader->candidate.pages=(reader->info.width*reader->info.height*4+4095)/4096;
        reader->candidate.pixels=(u32 *)pframe_alloc_run(reader->candidate.pages);
        if(!reader->candidate.pixels)goto failed;
        reader->phase=2;
    }
    /* 只在任务0服务保护内读一个有限块；共享FS扇区中转不与三环
     * 系统调用交叉。每扇后的PIT检查使慢PIO设备也能及时交还主循环，
     * 管理心跳、输入与网络定时器不再等整幅6MiB壁纸完成。 */
    u8 indexed[512];
    do{
        u32 count=512-((reader->info.body+reader->position)&511u);
        if(count>reader->info.bytes-reader->position)count=reader->info.bytes-reader->position;
        if(count>budget)count=budget;
        void *target=reader->info.format==2?(u8 *)reader->candidate.pixels+reader->position:indexed;
        if(fs_read_at(reader->path,target,count,reader->info.body+reader->position)!=(int)count)goto failed;
        if(reader->info.format==1)for(u32 i=0;i<count;i++){
            u8 color=indexed[i];if(color>PAL_UI_LINE)goto failed;
            reader->candidate.pixels[reader->position+i]=reader->icon && !color?0:0xFF000000u|display_rgb(color);
        }
        reader->position+=count;budget-=count;
        if(reader->flatten){
            /* 背景的透明度合成与读盘使用同一个候选生命周期和预算。
             * 只处理完整像素；半个ARGB留到下批，不读未初始化正文。
             * 固定face随主题重载取消，计算与旧wall_accept逐位相同。 */
            u32 complete=reader->info.format==2?reader->position/4:reader->position;
            for(u32 i=reader->prepared_pixels;i<complete;i++){
                u32 pixel=reader->candidate.pixels[i],alpha=pixel>>24,result=0;
                if(alpha==255)result=pixel&0xFFFFFFu;
                else if(!alpha)result=reader->background;
                else for(u32 shift=0;shift<24;shift+=8)
                    result|=(((((pixel>>shift)&255)*alpha+((reader->background>>shift)&255)*(255-alpha)+127)/255)<<shift);
                reader->candidate.pixels[i]=result;
            }
            reader->prepared_pixels=complete;
        }
    }while(reader->position<reader->info.bytes && budget && sc_ticks-started<2u);
    if(reader->position<reader->info.bytes)return 1;
    image_release(out);*out=reader->candidate;reader->candidate=(image_surface_t){0};
    reader->phase=0;return 0;
failed:
    image_reader_cancel(reader);return -1;
}
int image_load(const char *path,u32 offset,u32 length,int icon,image_surface_t *out)
{
    image_info_t info;
    if(image_info(path,offset,length,icon,&info))return -1;
    image_surface_t next={0};
    next.width=info.width;next.height=info.height;
    next.pages=(info.width*info.height*4+4095)/4096;
    next.pixels=(u32 *)pframe_alloc_run(next.pages);
    if(!next.pixels)return -1;
    if(info.format==2){
        if(fs_read_at(path,next.pixels,info.bytes,info.body)!=(int)info.bytes)goto failed;
    }else{
        u8 row[1920];
        for(u32 y=0;y<info.height;y++){
            if(fs_read_at(path,row,info.width,info.body+y*info.width)!=(int)info.width)goto failed;
            for(u32 x=0;x<info.width;x++){
                u8 c=row[x];if(c>PAL_UI_LINE)goto failed;
                next.pixels[y*info.width+x]=icon && !c?0:0xFF000000u|display_rgb(c);
            }
        }
    }
    /* 只有完整读完才替换，失败释放候选；调用者仍有旧surface可显示。
     * 不存在半幅新图已占用页、错误返回后无人负责释放的状态。 */
    image_release(out);*out=next;return 0;
failed:
    image_release(&next);return -1;
}
int image_scx_info(const char *path,u8 *h,u32 *offset,u32 *length)
{
    u32 stat[2];
    if(fs_stat(path,stat) || stat[0]!=1 || stat[1]<36 || fs_read_at(path,h,36,0)!=36)return -1;
    if(!magic(h,"SCX1MIO") || le32(h+32)!=0x004F494Du)return -2;
    u32 load=le32(h+12),bss=le32(h+16),stack=le32(h+20),flags=le32(h+24);
    if(!load || load>262108 || load>stat[1]-36 || bss>0x3D0000u || load>0x3D0000u-bss
       || le32(h+8)>=load || le32(h+28)!=0x400000 || stack<4096 || stack>131072 || (flags&~3u))return -3;
    *offset=*length=0;
    if(flags&2){
        image_info_t icon;
        u32 at=36+load,n=stat[1]-at;
        if(image_info(path,at,n,1,&icon) || icon.format!=2)return -3;
        *offset=at;*length=n;
    }else if(stat[1]!=36+load)return -3;
    return 0;
}
