/* =====================================================================
 *  SandCore 图形子系统 v2 (kernel/gfx.c)
 *  ---------------------------------------------------------------------
 *  双缓冲实现: VGA仍有独立64000B索引后台；原生模式申请2025页
 *  真RGB后台。旧槽位绘图先查表，ARGB只在合成阶段整数混合。
 *  gfx_swap()最终整帧写设备；原生是PCI线性显存，VGA有界等回扫。
 *  不把高分辨率整帧成本视为零；合成期间IRQ继续收输入与计时。
 * ===================================================================== */
#include "io.h"
#include "gfx.h"
#include "memory.h"
#include "display.h"

#define VGA_FB ((volatile u8 *)0xA0000)

static u8 gfx_legacy_bb[320*200];
static u32 *native_bb;
static int native_active;
i32 gfx_width=320,gfx_height=200;
/* mio：只约束后台写入，读像素仍能取得完整底图。默认无视口，
 * 保留所有旧调用；局部场景设置后每一条直接行写路径也要裁，
 * 否则后台/PCI只更新矩形而背景偷偷整屏覆盖，会制造下一帧残影。 */
static int clip_active,clip_x0,clip_y0,clip_x1,clip_y1;
void gfx_clip_reset(void){clip_active=0;}
void gfx_clip(int x,int y,int w,int h)
{
    signed long long right=(signed long long)x+w,bottom=(signed long long)y+h;
    clip_active=1;clip_x0=clip_y0=clip_x1=clip_y1=0;
    if(w<=0 || h<=0 || right<=0 || bottom<=0 || x>=GFX_W || y>=GFX_H)return;
    clip_x0=x<0?0:x;clip_y0=y<0?0:y;
    clip_x1=right>GFX_W?GFX_W:(int)right;
    clip_y1=bottom>GFX_H?GFX_H:(int)bottom;
}
void gfx_clip_bounds(i32 *out)
{
    out[0]=clip_active?clip_x0:0;out[1]=clip_active?clip_y0:0;
    out[2]=(clip_active?clip_x1:GFX_W)-out[0];
    out[3]=(clip_active?clip_y1:GFX_H)-out[1];
}
static inline int write_point(int x,int y)
{
    return (u32)x<(u32)GFX_W && (u32)y<(u32)GFX_H
        && (!clip_active || (x>=clip_x0 && x<clip_x1 && y>=clip_y0 && y<clip_y1));
}
int gfx_resize(int width,int height)
{
    if(width<320 || height<200 || width>1920 || height>1080)return -1;
    if(width!=320 || height!=200) {
        /* 一次预留最高档后台区，后续预览/自动回退不再依赖堆碎片。
         * 原64000B缓冲仍独立存在；无扩展设备启动不会消耗这块 RAM。
         * 常驻显示页计入页分配统计，窗口页则随关闭单独释放。 */
        if(!native_bb)native_bb=(u32 *)pframe_alloc_run((1920u*1080u*4+4095)/4096);
        if(!native_bb)return -1;
        native_active=1;
    } else native_active=0;
    gfx_width=width;gfx_height=height;
    gfx_init();
    return 0;
}

void gfx_init(void)
{
    gfx_clip_reset(); /* 启动/换模式不能继承上一次场景的旧坐标视口 */
    for (u32 i = 0; i < (u32)(GFX_W * GFX_H); i++) {
        if(native_active) native_bb[i]=0;
        else gfx_legacy_bb[i]=0;
    }
}

void gfx_pset(i32 x, i32 y, u8 c)
{
    if (write_point(x,y))
        { if(native_active) native_bb[y*GFX_W+x]=display_rgb(c);
          else gfx_legacy_bb[y*GFX_W+x]=c; }
}

u8 gfx_get(i32 x, i32 y)
{
    if ((u32)x >= (u32)GFX_W || (u32)y >= (u32)GFX_H)
        return 0;
    return native_active?display_index(native_bb[y*GFX_W+x]):gfx_legacy_bb[y*GFX_W+x];
}

void gfx_rgb_pset(i32 x,i32 y,u32 rgb)
{
    if(!write_point(x,y)) return;
    if(native_active) native_bb[y*GFX_W+x]=rgb&0x00FFFFFFu;
    else gfx_legacy_bb[y*GFX_W+x]=display_index(rgb);
}
u32 gfx_rgb_get(i32 x,i32 y)
{
    if((u32)x>=(u32)GFX_W || (u32)y>=(u32)GFX_H) return 0;
    return native_active?native_bb[y*GFX_W+x]:display_rgb(gfx_legacy_bb[y*GFX_W+x]);
}
void gfx_argb_pset(i32 x,i32 y,u32 argb)
{
    if(!write_point(x,y))return; /* 先裁再混合，视口外不做透明乘除 */
    u32 alpha=argb>>24;
    if(!alpha) return;
    if(alpha==255) { gfx_rgb_pset(x,y,argb); return; }
    u32 old=gfx_rgb_get(x,y),inverse=255-alpha,result=0;
    /* 每通道分别计算，避免直接乘打包RGB导致进位串色。255分母与
     * 四舍五入保证端点正确；无需浮点/SIMD，也没有libc依赖。
     * 目的后台已是不透明RGB，图标的直通alpha只在这里混合一次。 */
    for(u32 shift=0;shift<24;shift+=8) {
        u32 value=(((argb>>shift)&255)*alpha+((old>>shift)&255)*inverse+127)/255;
        result|=value<<shift;
    }
    gfx_rgb_pset(x,y,result);
}
void gfx_rgb_fill(i32 x,i32 y,i32 w,i32 h,u32 rgb)
{
    if(w<=0 || h<=0 || x>=GFX_W || y>=GFX_H) return;
    /* 在裁剪前用64位整数求右/下边界，防止超大或负坐标加法溢出；
     * 最终循环永远不超过屏幕大小，主题配置不能变成无限绘制。 */
    signed long long right=(signed long long)x+w,bottom=(signed long long)y+h;
    if(right<=0 || bottom<=0) return;
    int x0=x<0?0:x,y0=y<0?0:y;
    int x1=right>GFX_W?GFX_W:(int)right,y1=bottom>GFX_H?GFX_H:(int)bottom;
    if(clip_active){
        if(x0<clip_x0)x0=clip_x0;
        if(y0<clip_y0)y0=clip_y0;
        if(x1>clip_x1)x1=clip_x1;
        if(y1>clip_y1)y1=clip_y1;
    }
    if(x0>=x1 || y0>=y1)return; /* REP计数绝不能由负交集转无符号 */
    if(native_active) {
        rgb&=0xFFFFFFu;
        for(int j=y0;j<y1;j++) {
            u32 *row=native_bb+j*GFX_W+x0;u32 count=(u32)(x1-x0);
            __asm__ __volatile__("cld; rep stosl":"+D"(row),"+c"(count):"a"(rgb):"memory");
        }
    }else {
        u8 slot=display_index(rgb);
        for(int j=y0;j<y1;j++)for(int i=x0;i<x1;i++)gfx_legacy_bb[j*GFX_W+i]=slot;
    }
}

void gfx_fill(i32 x, i32 y, i32 w, i32 h, u8 c)
{
    if(native_active){gfx_rgb_fill(x,y,w,h,display_rgb(c));return;}
    /* VGA保留原索引字节，重复DAC颜色也不能换成另一个槽；旧探针
     * 和SCB1按槽位解释画布。量化仅用于真实RGB输入的兼容边界。 */
    if(w<=0 || h<=0)return;
    signed long long right=(signed long long)x+w,bottom=(signed long long)y+h;
    int x0=x<0?0:x,y0=y<0?0:y;
    int x1=right>GFX_W?GFX_W:(int)right,y1=bottom>GFX_H?GFX_H:(int)bottom;
    if(clip_active){
        if(x0<clip_x0)x0=clip_x0;
        if(y0<clip_y0)y0=clip_y0;
        if(x1>clip_x1)x1=clip_x1;
        if(y1>clip_y1)y1=clip_y1;
    }
    if(x0>=x1 || y0>=y1)return;
    for(int r=y0;r<y1;r++)for(int col=x0;col<x1;col++)gfx_legacy_bb[r*GFX_W+col]=c;
}

void gfx_argb_blit(i32 x,i32 y,int w,int h,const u32 *pixels,int stride)
{
    if(w<=0 || h<=0 || stride<w)return;
    int left=x<0?-x:0,top=y<0?-y:0,right=w,bottom=h;
    if(x+right>GFX_W)right=GFX_W-x;
    if(y+bottom>GFX_H)bottom=GFX_H-y;
    if(clip_active){
        if(x+left<clip_x0)left=clip_x0-x;
        if(y+top<clip_y0)top=clip_y0-y;
        if(x+right>clip_x1)right=clip_x1-x;
        if(y+bottom>clip_y1)bottom=clip_y1-y;
    }
    if(left>=right || top>=bottom)return;
    if(!native_active) {
        for(int r=top;r<bottom;r++)for(int c=left;c<right;c++)gfx_argb_pset(x+c,y+r,pixels[r*stride+c]);
        return;
    }
    for(int r=top;r<bottom;r++) {
        const u32 *source=pixels+r*stride+left;u32 *out=native_bb+(y+r)*GFX_W+x+left;
        for(int c=0;c<right-left;c++) {
            u32 pixel=source[c],alpha=pixel>>24;
            if(alpha==255){out[c]=pixel&0xFFFFFFu;continue;}
            if(!alpha)continue;
            u32 old=out[c],inverse=255-alpha,result=0;
            for(u32 shift=0;shift<24;shift+=8)
                result|=(((((pixel>>shift)&255)*alpha+((old>>shift)&255)*inverse+127)/255)<<shift);
            out[c]=result;
        }
    }
}
void gfx_rgb_row_map(int y,const u32 *source,const u32 *indices,int count)
{
    if(y<0 || y>=GFX_H || count<0 || count>GFX_W)return;
    if(clip_active && (y<clip_y0 || y>=clip_y1))return;
    int first=clip_active?clip_x0:0,last=clip_active && count>clip_x1?clip_x1:count;
    if(native_active) {
        u32 *out=native_bb+y*GFX_W;
        for(int x=first;x<last;x++)out[x]=source[indices[x]]&0xFFFFFFu;
    }else for(int x=first;x<last;x++)gfx_rgb_pset(x,y,source[indices[x]]);
}
void gfx_opaque_blit(i32 x,i32 y,int w,int h,const u32 *pixels,int stride)
{
    if(w<=0 || h<=0 || stride<w)return;
    int left=x<0?-x:0,top=y<0?-y:0,right=w,bottom=h;
    if(x+right>GFX_W)right=GFX_W-x;
    if(y+bottom>GFX_H)bottom=GFX_H-y;
    if(clip_active){
        if(x+left<clip_x0)left=clip_x0-x;
        if(y+top<clip_y0)top=clip_y0-y;
        if(x+right>clip_x1)right=clip_x1-x;
        if(y+bottom>clip_y1)bottom=clip_y1-y;
    }
    if(left>=right || top>=bottom)return;
    for(int r=top;r<bottom;r++){
        const u32 *source=pixels+r*stride+left;
        if(native_active){
            u32 *out=native_bb+(y+r)*GFX_W+x+left;
            for(int c=0;c<right-left;c++)out[c]=source[c]&0xFFFFFFu;
        }else for(int c=left;c<right;c++)gfx_rgb_pset(x+c,y+r,pixels[r*stride+c]);
    }
}
void gfx_background_copy(const u32 *pixels,int width,int height)
{
    if(!pixels || width!=GFX_W || height!=GFX_H)return;
    if(clip_active){
        /* 预采样缓存仍是整屏；只拷视口行段。stride沿原屏宽，
         * 不能把矩形宽当源步长，透明窗口随后还依赖这份正确底图。 */
        for(int y=clip_y0;y<clip_y1;y++){
            const u32 *source=pixels+y*width+clip_x0;
            u32 count=(u32)(clip_x1-clip_x0);
            if(native_active){
                u32 *out=native_bb+y*width+clip_x0;
                __asm__ __volatile__("cld; rep movsl":"+D"(out),"+S"(source),"+c"(count)::"memory");
            }else for(u32 x=0;x<count;x++)gfx_legacy_bb[y*width+clip_x0+(int)x]=(u8)source[x];
        }
        return;
    }
    u32 count=(u32)width*height;
    if(native_active){
        u32 *out=native_bb;
        __asm__ __volatile__("cld; rep movsl":"+D"(out),"+S"(pixels),"+c"(count)::"memory");
    }else for(u32 i=0;i<count;i++)gfx_legacy_bb[i]=(u8)pixels[i];
}

/* Bresenham 直线: 全整数, 八向对称处理 */
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u8 c)
{
    i32 dx = x1 - x0, dy = y1 - y0;
    i32 sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    i32 err = dx - dy;
    for (;;) {
        gfx_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        i32 e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

/* 圆: 中点法逐列填充; fill=0 只描轮廓 */
void gfx_circle(i32 cx, i32 cy, i32 r, u8 c, i32 fill)
{
    if (r < 0)
        return;
    for (i32 y = -r; y <= r; y++) {
        /* 求 x^2 + y^2 = r^2 的 x (整数开方, 逐次逼近) */
        i32 x = r;
        while (x * x + y * y > r * r)
            x--;
        if (fill) {
            for (i32 i = cx - x; i <= cx + x; i++)
                gfx_pset(i, cy + y, c);
        } else {
            gfx_pset(cx + x, cy + y, c);
            gfx_pset(cx - x, cy + y, c);
        }
    }
}

void gfx_swap(void)
{
    if(native_active) display_present_rgb(native_bb,GFX_W,GFX_H);
    else display_present(gfx_legacy_bb,GFX_W,GFX_H);
}
void gfx_swap_rect(int x,int y,int w,int h)
{
    if(native_active)display_present_rgb_rect(native_bb,x,y,w,h,GFX_W);
    else gfx_swap();
}

/* ---------------- 文字渲染 ---------------- */
#include "font.h"
#include "font16.h"

/* SCF-8 单字符 (font.h 的直角表, 0x20..0x7E), 透明底 */
void gfx_char8(i32 x, i32 y, char ch, u8 color, i32 scale)
{
    const unsigned char *g = glyph_of(ch);
    for (int r = 0; r < 8; r++)
        for (int c = 0; c < 8; c++)
            if ((g[r] >> (7 - c)) & 1)
                for (int dy = 0; dy < scale; dy++)
                    for (int dx = 0; dx < scale; dx++)
                        gfx_pset(x + c * scale + dx, y + r * scale + dy, color);
}

void gfx_text8(i32 x, i32 y, const char *s, u8 color, i32 scale, i32 sp)
{
    for (; *s; s++, x += 8 * scale + sp)
        gfx_char8(x, y, *s, color, scale);
}

/* mio：UTF-8 的“字节数”和“字符宽”不能混用。严格拒绝过长形式、
 * 代理项、越界标量及截断序列；每读一个后续字节先确认非 NUL，
 * 避免对位于用户映射末端的终止字符串多读。非法时仅消费首字节。
 */
int gfx_utf8_next(const char **text,u32 *scalar)
{
    const u8 *p=(const u8 *)*text;
    u32 b=*p,value,minimum;
    int n;
    if(!b) return 0;
    *text=(const char *)p+1;
    if(b<128) { *scalar=b; return 1; }
    if(b>=0xC2 && b<=0xDF) { n=2; minimum=0x80; value=b&31; }
    else if(b>=0xE0 && b<=0xEF) { n=3; minimum=0x800; value=b&15; }
    else if(b>=0xF0 && b<=0xF4) { n=4; minimum=0x10000; value=b&7; }
    else { *scalar=0xFFFD; return -1; }
    for(int i=1;i<n;i++) {
        if(!p[i] || (p[i]&0xC0)!=0x80) { *scalar=0xFFFD; return -1; }
        value=(value<<6)|(p[i]&63);
    }
    if(value<minimum || value>0x10FFFF || (value>=0xD800 && value<=0xDFFF)) {
        *scalar=0xFFFD; return -1;
    }
    *text=(const char *)p+n; *scalar=value;
    return 1;
}
int gfx_utf8_valid(const char *text)
{
    u32 scalar;
    while(*text) if(gfx_utf8_next(&text,&scalar)<0) return 0;
    return 1;
}
int gfx_glyph16(u32 scalar,u16 *rows)
{
    if(scalar>=32 && scalar<=126) {
        for(int r=0;r<16;r++) rows[r]=sc_latin16[scalar-32][r];
        return 8;
    }
    const zh16_t *glyph=0;
    if(scalar>=0x800 && scalar<=0xFFFF && !(scalar>=0xD800 && scalar<=0xDFFF)) {
        char name[4];
        name[0]=(char)(0xE0|(scalar>>12));
        name[1]=(char)(0x80|((scalar>>6)&63));
        name[2]=(char)(0x80|(scalar&63)); name[3]=0;
        glyph=zh16_find(name);
    }
    for(int r=0;r<16;r++) {
        u16 bits=0;
        if(glyph) {
            for(int c=0;c<16;c++) if(glyph->r[r][c]=='#') bits|=(u16)(0x8000u>>c);
        } else bits=(r==0 || r==15)?0xFFFF:0x8001;
        rows[r]=bits;
    }
    return glyph?16:0;
}
void gfx_font_info(u32 *info)
{
    info[0]=1; info[1]=(u32)(zh16_active?zh16_active_n:ZH16_N);
    info[2]=16; info[3]=zh16_active?1:0;
}
void gfx_char16(i32 x,i32 y,const char *utf8,u8 fg,u8 bg)
{
    u32 scalar=0xFFFD; u16 rows[16];
    gfx_utf8_next(&utf8,&scalar);
    int width=gfx_glyph16(scalar,rows); if(!width) width=16;
    for(int r=0;r<16;r++) for(int c=0;c<width;c++) {
        if(rows[r]&(0x8000u>>c)) gfx_pset(x+c,y+r,fg);
        else if(bg) gfx_pset(x+c,y+r,bg);
    }
}
void gfx_text16(i32 x,i32 y,const char *text,u8 fg,u8 bg)
{
    i32 left=x; u32 scalar; u16 rows[16];
    while(*text) {
        gfx_utf8_next(&text,&scalar);
        if(scalar=='\n') { x=left; y+=18; continue; }
        if(scalar=='\r') { x=left; continue; }
        int width=gfx_glyph16(scalar,rows); if(!width) width=16;
        for(int r=0;r<16;r++) for(int c=0;c<width;c++) {
            if(rows[r]&(0x8000u>>c)) gfx_pset(x+c,y+r,fg);
            else if(bg) gfx_pset(x+c,y+r,bg);
        }
        x+=width+2;
    }
}

/* SCF 的记录布局仍为 12B 头 + 每字 276B，旧图标无需换格式。
 * 字体加载先完整检查，全部合法后才替换活动表，坏字体不留下半张
 * 表。行指针引用永久 fbuf，不能指向内核栈/临时读取缓冲。
 */
static zh16_t zh16_loaded[128];
int gfx_font_load(const void *buf,u32 size)
{
    const u8 *p=(const u8 *)buf;
    const char *magic="SCF1MIO";
    if(size<12) return -1;
    for(int i=0;i<8;i++) if(p[i]!=(u8)magic[i]) return -1;
    u32 count=(u32)p[8]|((u32)p[9]<<8)|((u32)p[10]<<16)|((u32)p[11]<<24);
    if(!count || count>128 || size!=12+count*276) return -1;
    for(u32 i=0;i<count;i++) {
        const u8 *record=p+12+i*276;
        const char *label=(const char *)record; u32 scalar;
        if(record[3] || record[0]<0xE0 || gfx_utf8_next(&label,&scalar)!=1
           || label!=(const char *)record+3) return -1;
        for(u32 j=0;j<i;j++) {
            const u8 *old=p+12+j*276;
            if(old[0]==record[0] && old[1]==record[1] && old[2]==record[2]) return -1;
        }
        for(int r=0;r<16;r++) {
            const u8 *row=record+4+r*17;
            if(row[16]) return -1;
            for(int c=0;c<16;c++) if(row[c]!='#' && row[c]!='.') return -1;
        }
    }
    for(u32 i=0;i<count;i++) {
        const u8 *record=p+12+i*276;
        zh16_loaded[i].zh=(const char *)record;
        for(int r=0;r<16;r++) zh16_loaded[i].r[r]=(const char *)record+4+r*17;
    }
    font16_use(zh16_loaded,(int)count);
    return (int)count;
}
