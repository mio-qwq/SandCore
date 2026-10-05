/* =====================================================================
 *  SandCore 图形子系统 v2 (kernel/gfx.c)
 *  ---------------------------------------------------------------------
 *  双缓冲实现: 后台缓冲是内核 .bss 里的一块 64000B 内存,
 *  所有图元往那儿画; gfx_swap() 等垂直回扫后一次性整块拷进显存。
 *  整块拷贝用 u32 搬运 (16000 次), 一帧开销可忽略。
 * ===================================================================== */
#include "io.h"
#include "gfx.h"
#include "memory.h"
#include "display.h"

#define VGA_FB ((volatile u8 *)0xA0000)

static u8 gfx_legacy_bb[320*200];
static u8 *gfx_bb=gfx_legacy_bb,*native_bb;
i32 gfx_width=320,gfx_height=200;
int gfx_resize(int width,int height)
{
    if(width<320 || height<200 || width>1920 || height>1080)return -1;
    if(width!=320 || height!=200) {
        /* 一次预留最高档后台区，后续预览/自动回退不再依赖堆碎片。
         * 原64000B缓冲仍独立存在；无扩展设备启动不会消耗这块 RAM。
         * 常驻显示页计入页分配统计，窗口页则随关闭单独释放。 */
        if(!native_bb)native_bb=(u8 *)pframe_alloc_run((1920u*1080u+4095)/4096);
        if(!native_bb)return -1;
        gfx_bb=native_bb;
    } else gfx_bb=gfx_legacy_bb;
    gfx_width=width;gfx_height=height;
    for(int i=0;i<width*height;i++)gfx_bb[i]=0;
    return 0;
}

void gfx_init(void)
{
    for (u32 i = 0; i < GFX_W * GFX_H; i++)
        gfx_bb[i] = 0;
}

void gfx_pset(i32 x, i32 y, u8 c)
{
    if ((u32)x < GFX_W && (u32)y < GFX_H)
        gfx_bb[y * GFX_W + x] = c;
}

u8 gfx_get(i32 x, i32 y)
{
    if ((u32)x >= GFX_W || (u32)y >= GFX_H)
        return 0;
    return gfx_bb[y * GFX_W + x];
}

void gfx_fill(i32 x, i32 y, i32 w, i32 h, u8 c)
{
    for (i32 j = y; j < y + h; j++)
        for (i32 i = x; i < x + w; i++)
            gfx_pset(i, j, c);
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
    display_present(gfx_bb,GFX_W,GFX_H);
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
