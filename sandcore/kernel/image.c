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
