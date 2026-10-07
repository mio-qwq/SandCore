/* 直接读取原TTF，不预制另一份中文点阵子集。两份凤凰字体的实际
 * 轮廓均为on-curve直线、没有复合字形；这个字体配置明确拒绝其它
 * 轮廓类型，不能把“支持此完整字库”宣传为任意TrueType引擎。
 * 原生像素以中心采样/非零绕数填充，保留洞和重叠；不执行字体
 * 字节码、不开FPU、不依赖libc。所有边界/轮廓先验证，成功才提交。
 * 暂存点/交点与原字节归脸的PF页，缓存归全局版本，退出用户不释放
 * 系统字体。无缓存页时仍可用私有固定结果暂存，不隐去缺字/失败。 */
#include "ttf.h"
#include "memory.h"
#include "fs.h"
#include "crypto.h"

#define FONT_LIMIT (8u*1024u*1024u)
#define SHAPE_LIMIT 4096u
#define CACHE_SETS 64u
#define CACHE_WAYS 4u
typedef struct {u32 offset,bytes;} table_t;
typedef struct {i32 x,y;} point_t;
typedef struct {i32 x,winding;} crossing_t;
typedef struct {
    u8 *data;u32 bytes,pages,workspace,workspace_pages;
    table_t head,maxp,hhea,hmtx,loca,glyf,cmap;
    u32 glyphs,metrics,units,points_cap,contours_cap,location_format;
    u32 cmap_offset,cmap_bytes,cmap_format,cmap_count,mapped,id,epoch;
    i32 ascent,descent,line_gap;
    point_t *points;u16 *ends;u8 *flags;crossing_t *crossings;
} face_t;
typedef struct {
    u32 epoch,face,glyph,pixels,stamp;
    i32 advance,width,height,baseline,left,bearing_fixed,advance_fixed;
    u32 rows[TTF_BITMAP_ROWS][4];
} glyph_t;
static face_t faces[2];
static glyph_t *cache;
static glyph_t temporary;
static u32 cache_pages,clock,epoch_counter,hits,misses;
static u16 be16(const u8 *p){return (u16)((u16)p[0]<<8|p[1]);}
static u32 be32(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static i32 signed16(const u8 *p){return (i32)(signed short)be16(p);}
static int range(u32 offset,u32 bytes,u32 total){return offset<=total && bytes<=total-offset;}
static u32 align4(u32 n){return (n+3u)&~3u;}
static face_t *get_face(u32 id){return id==12?&faces[0]:id==16?&faces[1]:0;}
static void release(face_t *f)
{
    if(f->data)pframe_free_run((u32)f->data,f->pages);
    if(f->workspace)pframe_free_run(f->workspace,f->workspace_pages);
    crypto_zero(f,sizeof(*f));
}
static u32 location(const face_t *f,u32 glyph)
{
    const u8 *p=f->data+f->loca.offset;
    return f->location_format?be32(p+glyph*4):2u*be16(p+glyph*2);
}
static table_t *table(face_t *f,u32 tag)
{
    switch(tag){
    case 0x68656164:return &f->head;case 0x6D617870:return &f->maxp;
    case 0x68686561:return &f->hhea;case 0x686D7478:return &f->hmtx;
    case 0x6C6F6361:return &f->loca;case 0x676C7966:return &f->glyf;
    case 0x636D6170:return &f->cmap;default:return 0;}
}
static int table_directory(face_t *f)
{
    const u8 *p=f->data;if(f->bytes<12 || be32(p)!=0x00010000u)return -1;
    u32 count=be16(p+4);if(!count || count>64 || !range(12,count*16,f->bytes))return -1;
    for(u32 i=0;i<count;i++){
        const u8 *r=p+12+i*16;u32 tag=be32(r),offset=be32(r+8),bytes=be32(r+12);
        if((offset&3u) || offset<12+count*16 || !range(offset,bytes,f->bytes))return -1;
        for(u32 j=0;j<i;j++){
            const u8 *old=p+12+j*16;u32 start=be32(old+8),size=be32(old+12);
            if(tag==be32(old) || (bytes && size && offset<start+size && start<offset+bytes))return -1;
        }
        u32 sum=0;
        for(u32 at=0;at<bytes;at+=4){u32 word=0;
            for(u32 n=0;n<4;n++)word=(word<<8)|(at+n<bytes?p[offset+at+n]:0);
            if(tag==0x68656164u && at==8)word=0;sum+=word;
        }
        if(sum!=be32(r+4))return -1;
        table_t *dst=table(f,tag);if(dst)*dst=(table_t){offset,bytes};
    }
    if(f->head.bytes<54 || f->maxp.bytes<32 || f->hhea.bytes<36 || !f->hmtx.bytes
        || !f->loca.bytes || !f->glyf.bytes || f->cmap.bytes<4)return -1;
    const u8 *head=p+f->head.offset,*maxp=p+f->maxp.offset,*hhea=p+f->hhea.offset;
    if(be32(head)!=0x00010000u || be32(head+12)!=0x5F0F3CF5u || be32(maxp)!=0x00010000u
        || be32(hhea)!=0x00010000u || signed16(head+52))return -1;
    f->units=be16(head+18);f->location_format=be16(head+50);f->glyphs=be16(maxp+4);
    f->points_cap=be16(maxp+6);f->contours_cap=be16(maxp+8);f->metrics=be16(hhea+34);
    f->ascent=signed16(hhea+4);f->descent=signed16(hhea+6);f->line_gap=signed16(hhea+8);
    if(f->units<16 || f->units>16384 || f->location_format>1 || !f->glyphs || !f->metrics
        || f->metrics>f->glyphs || !f->points_cap || !f->contours_cap || f->points_cap>SHAPE_LIMIT
        || f->contours_cap>SHAPE_LIMIT || f->ascent<=0 || f->descent>0
        || f->ascent-f->descent>f->units*3u/2u || f->line_gap<0
        || f->loca.bytes<(f->glyphs+1)*(f->location_format?4u:2u)
        || f->hmtx.bytes<f->metrics*4+(f->glyphs-f->metrics)*2)return -1;
    u32 previous=0;
    for(u32 i=0;i<=f->glyphs;i++){u32 at=location(f,i);if(at<previous || at>f->glyf.bytes)return -1;previous=at;}
    u32 point_bytes=f->points_cap*sizeof(point_t),end_bytes=align4(f->contours_cap*2);
    u32 flag_bytes=align4(f->points_cap),cross_bytes=f->points_cap*sizeof(crossing_t);
    f->workspace_pages=(point_bytes+end_bytes+flag_bytes+cross_bytes+4095)/4096;
    f->workspace=pframe_alloc_run(f->workspace_pages);if(!f->workspace)return -4;
    f->points=(point_t *)f->workspace;f->ends=(u16 *)(f->workspace+point_bytes);
    f->flags=(u8 *)f->ends+end_bytes;f->crossings=(crossing_t *)(f->flags+flag_bytes);return 0;
}
static int decode(face_t *f,u32 glyph,u32 *contours,u32 *points)
{
    u32 lo=location(f,glyph),hi=location(f,glyph+1);*contours=*points=0;
    if(lo==hi)return 0;if(hi-lo<10)return -1;
    const u8 *p=f->data+f->glyf.offset+lo;u32 bytes=hi-lo;
    i32 count=signed16(p);if(count<0 || (u32)count>f->contours_cap)return -1;
    i32 xmin=signed16(p+2),ymin=signed16(p+4),xmax=signed16(p+6),ymax=signed16(p+8);
    if(xmin>xmax || ymin>ymax)return -1;
    u32 at=10;if(!range(at,(u32)count*2,bytes))return -1;
    for(i32 i=0;i<count;i++){u32 n=be16(p+at);at+=2;
        if((i && n<=f->ends[i-1]) || n>=f->points_cap)return -1;f->ends[i]=(u16)n;}
    /* 零轮廓允许只保留10B头；其它指令完整核边界后跳过，永不解释。 */
    if(!count && at==bytes)return 0;
    if(!range(at,2,bytes))return -1;u32 instructions=be16(p+at);at+=2;
    if(!range(at,instructions,bytes))return -1;at+=instructions;
    u32 n=count?(u32)f->ends[count-1]+1:0;
    for(u32 i=0;i<n;){if(at>=bytes)return -1;u8 flag=p[at++];u32 repeats=0;
        if(flag&8){if(at>=bytes)return -1;repeats=p[at++];}
        if((flag&128u) || !(flag&1u) || repeats>=n-i)return -1;
        do{f->flags[i++]=flag;}while(repeats--);
    }
    i32 value=0;
    for(u32 i=0;i<n;i++){
        u8 flag=f->flags[i];i32 delta=0;
        if(flag&2){if(at>=bytes)return -1;delta=p[at++];if(!(flag&16))delta=-delta;}
        else if(!(flag&16)){if(!range(at,2,bytes))return -1;delta=signed16(p+at);at+=2;}
        value+=delta;if(value<xmin || value>xmax)return -1;f->points[i].x=value;
    }
    value=0;
    for(u32 i=0;i<n;i++){
        u8 flag=f->flags[i];i32 delta=0;
        if(flag&4){if(at>=bytes)return -1;delta=p[at++];if(!(flag&32))delta=-delta;}
        else if(!(flag&32)){if(!range(at,2,bytes))return -1;delta=signed16(p+at);at+=2;}
        value+=delta;if(value<ymin || value>ymax)return -1;f->points[i].y=value;
    }
    *contours=(u32)count;*points=n;return 0;
}
static u32 mapping4(const face_t *f,u32 scalar,u32 segment)
{
    const u8 *p=f->data+f->cmap_offset;u32 n=f->cmap_count;
    u32 start=be16(p+16+n*2+segment*2),end=be16(p+14+segment*2);
    if(scalar<start || scalar>end)return 0;
    u32 delta=be16(p+16+n*4+segment*2),ro=16+n*6+segment*2,offset=be16(p+ro);
    if(!offset)return (scalar+delta)&65535u;
    u32 at=ro+offset+(scalar-start)*2;if(!range(at,2,f->cmap_bytes))return 0;
    u32 glyph=be16(p+at);return glyph?(glyph+delta)&65535u:0;
}
static u32 mapping(const face_t *f,u32 scalar)
{
    if(scalar>0x10FFFFu || (scalar>=0xD800 && scalar<=0xDFFF))return 0;
    const u8 *p=f->data+f->cmap_offset;u32 lo=0,hi=f->cmap_count;
    if(f->cmap_format==4){
        if(scalar>65535)return 0;
        while(lo<hi){u32 mid=lo+(hi-lo)/2;if(be16(p+14+mid*2)<scalar)lo=mid+1;else hi=mid;}
        return lo<f->cmap_count?mapping4(f,scalar,lo):0;
    }
    while(lo<hi){u32 mid=lo+(hi-lo)/2;if(be32(p+16+mid*12+4)<scalar)lo=mid+1;else hi=mid;}
    if(lo>=f->cmap_count)return 0;const u8 *g=p+16+lo*12;u32 start=be32(g);
    return scalar<start?0:be32(g+8)+scalar-start;
}
static int cmap(face_t *f)
{
    const u8 *p=f->data+f->cmap.offset;u32 count=be16(p+2),score=0;
    if(be16(p) || !count || !range(4,count*8,f->cmap.bytes))return -1;
    for(u32 i=0;i<count;i++){
        const u8 *r=p+4+i*8;u32 platform=be16(r),encoding=be16(r+2),offset=be32(r+4);
        if(!range(offset,2,f->cmap.bytes))return -1;
        if(platform!=0 && !(platform==3 && (encoding==1 || encoding==10)))continue;
        u32 format=be16(p+offset),rank=format==12?2:format==4?1:0;
        if(rank>score){score=rank;f->cmap_offset=f->cmap.offset+offset;f->cmap_format=format;}
    }
    if(!score)return -1;p=f->data+f->cmap_offset;
    u32 available=f->cmap.offset+f->cmap.bytes-f->cmap_offset;
    if(f->cmap_format==4){
        if(available<16)return -1;f->cmap_bytes=be16(p+2);u32 twice=be16(p+6);f->cmap_count=twice/2;
        if(!twice || (twice&1) || f->cmap_bytes>available || f->cmap_bytes<16+f->cmap_count*8
            || be16(p+14+twice))return -1;
        u32 previous=0;
        for(u32 i=0;i<f->cmap_count;i++){
            u32 start=be16(p+16+twice+i*2),end=be16(p+14+i*2),ro=16+f->cmap_count*6+i*2,offset=be16(p+ro);
            if(start>end || (i && start<=previous) || (offset&1u)
                || (offset && (ro+offset<16+f->cmap_count*8 || !range(ro+offset,2,f->cmap_bytes)
                    || !range(ro+offset+(end-start)*2,2,f->cmap_bytes))))return -1;
            previous=end;
            for(u32 c=start;c<=end;c++){u32 glyph=mapping4(f,c,i);if(glyph>=f->glyphs)return -1;
                if(glyph && !(c>=0xD800 && c<=0xDFFF))f->mapped++;}
        }
        if(previous!=65535)return -1;
    }else{
        if(available<16 || be16(p+2))return -1;f->cmap_bytes=be32(p+4);f->cmap_count=be32(p+12);
        if(f->cmap_bytes<16 || f->cmap_bytes>available || f->cmap_count>(f->cmap_bytes-16)/12)return -1;
        u32 previous=0;
        for(u32 i=0;i<f->cmap_count;i++){
            const u8 *g=p+16+i*12;u32 start=be32(g),end=be32(g+4),glyph=be32(g+8);
            if(start>end || end>0x10FFFFu || (i && start<=previous) || glyph>=f->glyphs
                || end-start>=f->glyphs-glyph)return -1;
            previous=end;u32 n=end-start+1;if(!glyph)n--;
            if(start<=0xDFFF && end>=0xD800){u32 a=start<0xD800?0xD800:start,b=end>0xDFFF?0xDFFF:end;
                u32 skipped=b-a+1;if(!glyph && start>=a && start<=b)skipped--;n-=skipped;}
            f->mapped+=n;
        }
    }
    return 0;
}
static int load(face_t *f,const char *path,u32 id)
{
    u32 info[2];if(fs_stat(path,info) || info[0]!=1 || info[1]<12 || info[1]>FONT_LIMIT)return -1;
    f->bytes=info[1];f->pages=(f->bytes+4095)/4096;f->data=(u8 *)pframe_alloc_run(f->pages);
    if(!f->data)return -4;f->id=id;
    if(fs_read(path,f->data,f->bytes)!=(int)f->bytes || table_directory(f) || cmap(f))return -1;
    for(u32 i=0;i<f->glyphs;i++){u32 contours,points;if(decode(f,i,&contours,&points))return -1;}
    return 0;
}
int ttf_init(void)
{
    /* 两张脸分别原子替换；一张文件坏不激活半张脸，不损另一张完整脸。
     * 暂无三环重载入口，内核重复调用也只在全部审阅完成后提交版本。 */
    int mask=0;
    for(u32 i=0;i<2;i++){
        face_t next;crypto_zero(&next,sizeof(next));u32 id=i?16:12;
        if(!load(&next,i?"SYS/FONT/PHOENIX16.TTF":"SYS/FONT/PHOENIX12.TTF",id)
            && epoch_counter<0x7FFFFFFFu){
            next.epoch=++epoch_counter;face_t previous=faces[i];faces[i]=next;release(&previous);mask|=1<<i;
        }else release(&next);
    }
    if(mask && cache)crypto_zero(cache,cache_pages*4096);
    return mask;
}
static i32 floor_div(i32 value,i32 divisor)
{i32 result=value/divisor;if(value<0 && value%divisor)result--;return result;}
static i32 ceil_div(i32 value,i32 divisor){return -floor_div(-value,divisor);}
static i32 multiply_divide(i32 a,i32 b,i32 divisor)
{
    int negative=(a<0)^(b<0)^(divisor<0);u32 x=a<0?0u-(u32)a:(u32)a;
    u32 y=b<0?0u-(u32)b:(u32)b,d=divisor<0?0u-(u32)divisor:(u32)divisor;
    u64 numerator=(u64)x*y;u32 result;
    if(!(numerator>>32))result=(u32)numerator/d;
    else{result=0;for(int bit=31;bit>=0;bit--){u64 test=(u64)d<<bit;
            if(numerator>=test){numerator-=test;result|=1u<<bit;}}}
    return negative?-(i32)result:(i32)result;
}
static void sift(crossing_t *p,u32 start,u32 count)
{
    for(u32 root=start;root<count/2;){u32 child=root*2+1;
        if(child+1<count && p[child].x<p[child+1].x)child++;
        if(p[root].x>=p[child].x)return;crossing_t value=p[root];p[root]=p[child];p[child]=value;root=child;}
}
static void sort(crossing_t *p,u32 count)
{
    for(u32 n=count/2;n;n--)sift(p,n-1,count);
    for(u32 n=count;n>1;n--){crossing_t value=p[0];p[0]=p[n-1];p[n-1]=value;sift(p,0,n-1);}
}
static int raster(face_t *f,u32 id,u32 pixels,glyph_t *g)
{
    crypto_zero(g,sizeof(*g));u32 contours,points;
    if(decode(f,id,&contours,&points))return -1;
    const u8 *metrics=f->data+f->hmtx.offset+(id<f->metrics?id:f->metrics-1)*4;
    const u8 *bearing=f->data+f->hmtx.offset+(id<f->metrics?id*4+2:f->metrics*4+(id-f->metrics)*2);
    i32 lsb=signed16(bearing),xmin=0;u32 lo=location(f,id),hi=location(f,id+1);
    if(lo!=hi)xmin=signed16(f->data+f->glyf.offset+lo+2);
    /* hmtx的左轴承才是笔尖到轮廓左边的距离，不能假定每份TTF均
     * 把xMin直接设为lsb。保持phantom原点所表达的设计定位。 */
    for(u32 i=0;i<points;i++)f->points[i].x+=lsb-xmin;
    g->advance=(i32)((be16(metrics)*pixels+f->units/2)/f->units);
    g->bearing_fixed=multiply_divide(lsb,(i32)pixels*64,(i32)f->units);
    g->advance_fixed=multiply_divide(be16(metrics),(i32)pixels*64,(i32)f->units);
    g->baseline=ceil_div(f->ascent*(i32)pixels,(i32)f->units);
    g->height=g->baseline+ceil_div(-f->descent*(i32)pixels,(i32)f->units);
    i32 left=0,right=g->advance;
    for(u32 i=0;i<points;i++){
        i32 a=floor_div(f->points[i].x*(i32)pixels,(i32)f->units);
        i32 b=ceil_div(f->points[i].x*(i32)pixels,(i32)f->units);
        if(a<left)left=a;if(b>right)right=b;
    }
    g->left=left;g->width=right-left;
    if(g->width>128 || g->height>(i32)TTF_BITMAP_ROWS || g->height<1)return -1;
    for(u32 i=0;i<points;i++){
        f->points[i].x=multiply_divide(f->points[i].x,(i32)pixels*64,(i32)f->units)-left*64;
        f->points[i].y=g->baseline*64-multiply_divide(f->points[i].y,(i32)pixels*64,(i32)f->units);
    }
    for(i32 row=0;row<g->height;row++){
        i32 y=row*64+32;u32 first=0,count=0;
        for(u32 contour=0;contour<contours;contour++){
            u32 last=f->ends[contour];
            for(u32 at=first;at<=last;at++){
                point_t a=f->points[at],b=f->points[at==last?first:at+1];
                if(a.y==b.y || y<(a.y<b.y?a.y:b.y) || y>=(a.y>b.y?a.y:b.y))continue;
                i32 x=a.x+multiply_divide(y-a.y,b.x-a.x,b.y-a.y);
                f->crossings[count++]=(crossing_t){x,b.y>a.y?1:-1};
            }
            first=last+1;
        }
        sort(f->crossings,count);i32 winding=0,start=0;
        for(u32 at=0;at<count;){i32 x=f->crossings[at].x,change=0;
            do{change+=f->crossings[at++].winding;}while(at<count && f->crossings[at].x==x);
            if(winding){i32 begin=ceil_div(start-32,64),end=ceil_div(x-32,64);
                if(begin<0)begin=0;if(end>g->width)end=g->width;
                for(i32 col=begin;col<end;col++)g->rows[row][col/32]|=1u<<(31-(col&31));}
            winding+=change;start=x;
        }
        if(winding)return -1;
    }
    g->epoch=f->epoch;g->face=f->id;g->glyph=id;g->pixels=pixels;return 0;
}
static glyph_t *glyph(face_t *f,u32 id,u32 pixels)
{
    if(!cache){u32 pages=(sizeof(glyph_t)*CACHE_SETS*CACHE_WAYS+4095)/4096;
        cache=(glyph_t *)pframe_alloc_run(pages);if(cache){cache_pages=pages;crypto_zero(cache,cache_pages*4096);}}
    glyph_t *result=&temporary;
    if(cache){u32 set=((id*2654435761u)^(pixels*97u)^f->id)&(CACHE_SETS-1);result=cache+set*CACHE_WAYS;
        for(u32 i=0;i<CACHE_WAYS;i++){
            glyph_t *g=cache+set*CACHE_WAYS+i;
            if(g->epoch==f->epoch && g->glyph==id && g->pixels==pixels && g->face==f->id){hits++;return g;}
            if(!g->epoch || (result->epoch && g->stamp<result->stamp))result=g;
        }
    }
    misses++;if(raster(f,id,pixels,result))return 0;result->stamp=++clock;
    /* 时钟只排序缓存替换，回绕时重置stamp而不修改字形/版本身份。 */
    if(!clock && cache){for(u32 i=0;i<CACHE_SETS*CACHE_WAYS;i++)cache[i].stamp=0;clock=1;result->stamp=1;}
    return result;
}
int ttf_bitmap(u32 scalar,u32 face,u32 pixels,u32 out[TTF_BITMAP_WORDS])
{
    face_t *f=get_face(face);if(!f || !f->epoch || pixels<1 || pixels>64 || scalar>0x10FFFF
        || (scalar>=0xD800 && scalar<=0xDFFF))return -1;
    u32 id=mapping(f,scalar);glyph_t *g=glyph(f,id,pixels);if(!g)return -4;
    for(u32 i=0;i<TTF_BITMAP_WORDS;i++)out[i]=0;
    out[0]=1;out[1]=f->epoch;out[2]=id;out[3]=id!=0;out[4]=pixels;out[5]=(u32)g->advance;
    out[6]=(u32)g->width;out[7]=(u32)g->height;out[8]=(u32)g->baseline;out[9]=(u32)g->left;out[10]=face;
    out[11]=(u32)g->bearing_fixed;out[12]=(u32)g->advance_fixed;
    for(u32 row=0;row<TTF_BITMAP_ROWS;row++)for(u32 word=0;word<4;word++)out[16+row*4+word]=g->rows[row][word];
    return id?g->advance:0;
}
int ttf_glyph16(u32 scalar,u16 out[16])
{
    face_t *f=&faces[1];if(!f->epoch)return -2;u32 id=mapping(f,scalar);glyph_t *g=glyph(f,id,16);if(!g)return -2;
    for(u32 row=0;row<16;row++){u16 bits=0;
        if(row<(u32)g->height)for(i32 col=0;col<16;col++){i32 x=col-g->left;
            if(x>=0 && x<g->width && (g->rows[row][x/32]&(1u<<(31-(x&31)))))bits|=(u16)(0x8000u>>col);}
        out[row]=bits;
    }
    /* 旧GLYPH16的正返回域始终8/16；真实advance/左轴承由新位图输出。
     * 不把较大的字宽送给旧16位行消费者造成越界移位。 */
    return id?(g->advance<=8?8:16):0;
}
int ttf_font8(u32 scalar,u8 out[8])
{
    u16 rows[16];int width=ttf_glyph16(scalar,rows);if(width<0)return width;
    for(u32 row=0;row<8;row++)out[row]=(u8)((rows[row*2]|rows[row*2+1])>>8);return width;
}
int ttf_info(u32 face,u32 out[16])
{
    face_t *f=get_face(face);if(!f)return -1;for(u32 i=0;i<16;i++)out[i]=0;
    out[0]=1;out[1]=f->epoch;out[2]=face;out[3]=f->epoch!=0;out[4]=f->glyphs;out[5]=f->mapped;
    out[6]=f->units;out[7]=(u32)f->ascent;out[8]=(u32)f->descent;out[9]=(u32)f->line_gap;
    out[10]=f->bytes;out[11]=f->pages+f->workspace_pages;out[12]=cache_pages;out[13]=hits;out[14]=misses;out[15]=f->metrics;
    return 0;
}
u32 ttf_mapped(void){return faces[1].epoch?faces[1].mapped:0;}
u32 ttf_epoch(void){return faces[1].epoch;}
