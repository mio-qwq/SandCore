/* Copyright (c) 2026 mio. 全部像素写入经画布裁剪；系统 TTF 按实际字号栅格化。 */
#include "sb.h"
#include <math.h>
typedef struct {
    uint32_t scalar, tick;
    int pixels;
    ScFontGlyph glyph;
} CachedGlyph;
static CachedGlyph *cache;
static uint32_t glyph_tick;
void sb_init_fonts(void) {
    if (!cache)
        cache = calloc(256, sizeof(*cache));
}
static const ScFontGlyph *get_glyph(uint32_t scalar, int pixels) {
    if (!cache)
        sb_init_fonts();
    if (!cache)
        return 0;
    pixels = pixels < 6 ? 6 : pixels > 64 ? 64 : pixels;
    unsigned slot = 0;
    uint32_t oldest = ~0u;
    for (unsigned i = 0; i < 256; i++) {
        if (cache[i].scalar == scalar && cache[i].pixels == pixels) {
            cache[i].tick = ++glyph_tick;
            return &cache[i].glyph;
        }
        if (cache[i].tick < oldest) {
            oldest = cache[i].tick;
            slot = i;
        }
    }
    CachedGlyph *g = cache + slot;
    ScFontGlyphDesc d;
    sb_record(&d, sizeof(d));
    d.face = 16;
    d.scalar = scalar;
    d.pixels = pixels;
    sb_record(&g->glyph, sizeof(g->glyph));
    if (ScFontGetGlyph(&d, &g->glyph) < 0)
        return 0;
    g->scalar = scalar;
    g->pixels = pixels;
    g->tick = ++glyph_tick;
    return &g->glyph;
}
static uint32_t next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = *s++, n = 0, v = c;
    if (c >= 0xc2 && c < 0xe0) {
        n = 1;
        v = c & 31;
    } else if (c >= 0xe0 && c < 0xf0) {
        n = 2;
        v = c & 15;
    } else if (c >= 0xf0 && c < 0xf5) {
        n = 3;
        v = c & 7;
    } else if (c >= 128) {
        *p = (const char *)s;
        return 0xfffd;
    }
    for (unsigned i = 0; i < n; i++) {
        if (!*s || (*s & 0xc0) != 0x80) {
            *p = (const char *)s;
            return 0xfffd;
        }
        v = (v << 6) | (*s++ & 63);
    }
    *p = (const char *)s;
    return v;
}
void sb_fill(SbCanvas *c, int x, int y, int w, int h, uint32_t rgb) {
    int x0 = x < c->x0 ? c->x0 : x, y0 = y < c->y0 ? c->y0 : y, x1 = x + w > c->x1 ? c->x1 : x + w,
        y1 = y + h > c->y1 ? c->y1 : y + h;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > c->width)
        x1 = c->width;
    if (y1 > c->height)
        y1 = c->height;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = c->pixels + j * c->stride;
        for (int i = x0; i < x1; i++)
            row[i] = 0xff000000u | rgb;
    }
}
void sb_line(SbCanvas *c, int x, int y, int x2, int y2, uint32_t rgb) {
    int dx = abs(x2 - x), sx = x < x2 ? 1 : -1, dy = -abs(y2 - y), sy = y < y2 ? 1 : -1,
        e = dx + dy;
    for (;;) {
        sb_fill(c, x, y, 1, 1, rgb);
        if (x == x2 && y == y2)
            break;
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y += sy;
        }
    }
}
void sb_bevel(SbCanvas *c, int x, int y, int w, int h, int down) {
    uint32_t a = down ? 0x808080 : 0xffffff, b = down ? 0xffffff : 0x404040;
    sb_fill(c, x, y, w, 1, a);
    sb_fill(c, x, y, 1, h, a);
    sb_fill(c, x, y + h - 1, w, 1, b);
    sb_fill(c, x + w - 1, y, 1, h, b);
    sb_fill(c, x + 1, y + 1, w - 2, 1, down ? 0x404040 : 0xdfdfdf);
    sb_fill(c, x + 1, y + 1, 1, h - 2, down ? 0x404040 : 0xdfdfdf);
    sb_fill(c, x + 1, y + h - 2, w - 2, 1, down ? 0xdfdfdf : 0x808080);
    sb_fill(c, x + w - 2, y + 1, 1, h - 2, down ? 0xdfdfdf : 0x808080);
}
int sb_text_width(const char *p, SbFont f) {
    int w = 0;
    while (*p) {
        uint32_t cp = next(&p);
        const ScFontGlyph *g = get_glyph(cp, f.pixels);
        w += g ? g->advance : f.pixels / 2;
    }
    return w;
}
void sb_text(SbCanvas *c, int x, int y, const char *p, SbFont f, uint32_t rgb) {
    int start = x;
    while (*p) {
        uint32_t cp = next(&p);
        if (cp == '\n') {
            x = start;
            y += f.pixels + 3;
            continue;
        }
        const ScFontGlyph *g = get_glyph(cp, f.pixels);
        if (!g) {
            x += f.pixels / 2;
            continue;
        }
        int gx = x + g->left, gy = y + f.pixels - f.pixels / 5 - g->baseline;
        for (unsigned r = 0; r < g->height && r < 96; r++)
            for (unsigned k = 0; k < g->width && k < 128; k++)
                if (g->rows[r][k / 32] & (0x80000000u >> (k % 32))) {
                    int slant = f.italic ? (int)(g->height - r) / 5 : 0;
                    sb_fill(c, gx + k + slant, gy + r, 1 + !!f.bold, 1, rgb);
                }
        if (f.underline)
            sb_fill(c, x, y + f.pixels + 1, g->advance, 1, rgb);
        x += g->advance;
    }
}
void sb_blit(SbCanvas *c, int x, int y, int w, int h, const uint32_t *src, int sw, int sh,
             int stride) {
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0)
        return;
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < c->y0 || dy >= c->y1 || dy < 0 || dy >= c->height)
            continue;
        for (int i = 0; i < w; i++) {
            int dx = x + i;
            if (dx < c->x0 || dx >= c->x1 || dx < 0 || dx >= c->width)
                continue;
            uint32_t s = src[(j * sh / h) * stride + i * sw / w], a = s >> 24;
            if (a == 255)
                c->pixels[dy * c->stride + dx] = s;
            else if (a) {
                uint32_t d = c->pixels[dy * c->stride + dx], r = 0xff000000u;
                for (unsigned k = 0; k < 3; k++) {
                    unsigned shift = k * 8, v = (((s >> shift) & 255) * a +
                                                 ((d >> shift) & 255) * (255 - a) + 127) /
                                                255;
                    r |= v << shift;
                }
                c->pixels[dy * c->stride + dx] = r;
            }
        }
    }
}
void sb_circle(SbCanvas *c, int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                sb_fill(c, cx + x, cy + y, 1, 1, color);
}
