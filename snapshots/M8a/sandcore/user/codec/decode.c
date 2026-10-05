/* =====================================================================
 * mio：真实内存解码。PNG/JPEG采用固定版本stb_image，WebP采用固定
 * libwebp；版本/许可/源哈希见third_party/SOURCES.json。第三方文件
 * 不改，SandCore适配层独立放这里，便于升级与追踪来源。
 *
 * 先检查格式签名/容器长度/尺寸，再让库分配。PNG另检每个chunk的
 * CRC与IEND，避免库的宽容读取把截断/损坏文件画成“加载成功”。
 * 输出统一直通ARGB32：PNG透明度原样保留、JPEG/BMP强制alpha255。
 * 不靠宿主预转换、不在内核解压、不用扩展名代替内容识别。
 * ===================================================================== */
#include "CODEC.H"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
/* mio：一个解码worker一个普通任务，没有宿主TLS/线程指针。若保留
 * stb默认__thread，库会经GS访问不存在的TLS块，链接成功仍会在
 * 三环真实解码时故障；其错误/翻转选项改为本任务BSS即可。 */
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 1920
#define STBI_MALLOC(n) malloc(n)
#define STBI_REALLOC(p,n) realloc(p,n)
#define STBI_FREE(p) free(p)
#define STBI_ASSERT(c) ((c)?(void)0:abort())
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "src/webp/decode.h"

static u32 little32(const u8 *p){return (u32)p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;}
static u32 big32(const u8 *p){return (u32)p[3]|(u32)p[2]<<8|(u32)p[1]<<16|(u32)p[0]<<24;}
static int size_ok(int w,int h){return w>0 && h>0 && w<=1920 && h<=1080;}
static u32 crc32(const u8 *p,u32 bytes)
{
    u32 crc=0xFFFFFFFFu;
    for(u32 i=0;i<bytes;i++){
        crc^=p[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xEDB88320u&(0u-(crc&1)));
    }
    return ~crc;
}
static int png_container(const u8 *source,u32 bytes)
{
    if(bytes<45 || memcmp(source,"\211PNG\r\n\032\n",8))return 0;
    u32 at=8;int first=1;
    while(at<=bytes-12){
        u32 n=big32(source+at);if(n>bytes-at-12)return 0;
        if(first && (n!=13 || memcmp(source+at+4,"IHDR",4)))return 0;
        if(crc32(source+at+4,n+4)!=big32(source+at+8+n))return 0;
        if(!memcmp(source+at+4,"IEND",4))return !n && at+12==bytes;
        at+=n+12;first=0;
    }
    return 0;
}
static int bitmap(const u8 *s,u32 bytes,u32 **out,int *w,int *h)
{
    if(bytes<54 || s[0]!='B' || s[1]!='M')return -4;
    u32 dib=little32(s+14),at=little32(s+10),width=little32(s+18);
    int height=(int)little32(s+22),top=height<0,bpp=(int)s[28]|(int)s[29]<<8;
    if(height==(int)0x80000000u || !width || width>1920)return -4;
    if(height<0)height=-height;
    if(!size_ok((int)width,height) || dib<40 || dib>bytes-14 || at<14+dib || at>bytes
        || s[26]!=1 || s[27] || (bpp!=24 && bpp!=32) || little32(s+30))return -4;
    u32 stride=(width*(u32)(bpp/8)+3)&~3u;
    if(stride*(u32)height>bytes-at)return -4;
    u32 *pixels=malloc(width*(u32)height*4);if(!pixels)return -5;
    for(int y=0;y<height;y++){
        const u8 *row=s+at+(u32)(top?y:height-1-y)*stride;
        for(u32 x=0;x<width;x++){
            const u8 *p=row+x*(u32)(bpp/8);
            pixels[(u32)y*width+x]=0xFF000000u|(u32)p[2]<<16|(u32)p[1]<<8|p[0];
        }
        if(!(y&31))sc_yield();
    }
    *w=(int)width;*h=height;*out=pixels;return 0;
}
int codec_decode(const u8 *source,u32 bytes,u32 **pixels,int *width,int *height)
{
    if(!source || !pixels || !width || !height || bytes<4 || bytes>16u*1024*1024)return -4;
    if(source[0]=='B' && source[1]=='M')return bitmap(source,bytes,pixels,width,height);
    if(bytes>=12 && !memcmp(source,"RIFF",4) && !memcmp(source+8,"WEBP",4)){
        /* 静态WebP含有损VP8/无损VP8L与alpha。动画容器不能只显示
         * 第一帧却谎称动画支持；本版明确报不支持，保留客户端旧图。 */
        if(little32(source+4)!=bytes-8)return -4;
        WebPBitstreamFeatures features;
        if(WebPGetFeatures(source,bytes,&features)!=VP8_STATUS_OK || features.has_animation
            || !size_ok(features.width,features.height))return -4;
        u32 length=(u32)features.width*features.height*4;
        u8 *out=malloc(length);if(!out)return -5;
        if(!WebPDecodeBGRAInto(source,bytes,out,length,features.width*4)){free(out);return codec_memory_failed()?-5:-4;}
        *pixels=(u32 *)out;*width=features.width;*height=features.height;return 0;
    }
    int png=bytes>=8 && !memcmp(source,"\211PNG\r\n\032\n",8);
    int jpeg=source[0]==255 && source[1]==216;
    if(!png && !jpeg)return -4;
    if(png && !png_container(source,bytes))return -4;
    /* JPEG要求实际EOI，不能让损坏的熵编码流靠库补零成为有效照片。
     * 末尾附加私有数据当前不接受；不扫描嵌入缩略图的EOI来冒充尾部。 */
    if(jpeg && (source[bytes-2]!=255 || source[bytes-1]!=217))return -4;
    int w,h,channels;
    if(!stbi_info_from_memory(source,(int)bytes,&w,&h,&channels) || !size_ok(w,h))return -4;
    u8 *rgba=stbi_load_from_memory(source,(int)bytes,&w,&h,&channels,4);
    if(!rgba)return codec_memory_failed()?-5:-4;
    if(!size_ok(w,h)){stbi_image_free(rgba);return -4;}
    /* 原库输出RGBA，x86的u32 ARGB实际内存是BGRA。原地交换R/B，
     * 不再分配第二幅1080p图；PNG alpha不预乘，Lens使用统一合成。 */
    for(u32 i=0;i<(u32)w*h;i++){
        u8 r=rgba[i*4];rgba[i*4]=rgba[i*4+2];rgba[i*4+2]=r;
        if(jpeg)rgba[i*4+3]=255;
    }
    *pixels=(u32 *)rgba;*width=w;*height=h;return 0;
}
