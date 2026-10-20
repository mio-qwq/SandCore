/* Copyright (c) 2026 mio. Host checks of physical clipping and zoom font requests. */
#include "sb.h"
#include <assert.h>

static unsigned requested[65];
void sb_record(void *p, unsigned n) {
    memset(p, 0, n);
    ((uint32_t *)p)[0] = n;
    ((uint32_t *)p)[1] = 1;
}
ScStatus ScFontGetGlyph(const ScFontGlyphDesc *d, ScFontGlyph *g) {
    assert(d->pixels <= 64 && d->pixels >= 1);
    requested[d->pixels]++;
    g->advance = d->pixels / 2;
    g->baseline = d->pixels - d->pixels / 5;
    g->width = g->height = 1;
    g->rows[0][0] = 0x80000000u;
    return SC32_OK;
}
int main(void) {
    uint32_t storage[16 * 12 + 2];
    for (unsigned i = 0; i < sizeof(storage) / sizeof(*storage); i++)
        storage[i] = 0x12345678;
    SbCanvas c = {storage + 1, 16, 12, 16, 1, 1, 9, 7, 150};
    sb_fill(&c, -2, -2, 20, 20, 0xabcdef);
    for (int y = 0; y < 12; y++)
        for (int x = 0; x < 16; x++)
            assert(c.pixels[y * 16 + x] ==
                   (x >= 1 && x < 13 && y >= 1 && y < 10 ? 0xffabcdef : 0x12345678));
    assert(storage[0] == 0x12345678 && storage[16 * 12 + 1] == 0x12345678);
    memset(c.pixels, 0, 16 * 12 * 4);
    c.x0 = c.y0 = 0; c.x1 = 11; c.y1 = 8;
    const uint32_t image[4] = {0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffffff};
    sb_blit(&c, 2, 2, 4, 4, image, 2, 2, 2);
    assert(c.pixels[3 * 16 + 3] == image[0]);
    assert(c.pixels[3 * 16 + 8] == image[1]);
    assert(c.pixels[8 * 16 + 3] == image[2]);
    assert(c.pixels[8 * 16 + 8] == image[3]);
    assert(!c.pixels[2 * 16 + 3] && !c.pixels[3 * 16 + 9]);
    memset(c.pixels, 0, 16 * 12 * 4);
    sb_text(&c, 1, 1, "AB", (SbFont){10, 0, 0, 0}, 0x112233);
    assert(requested[10] && requested[15]); /* New raster size; original CSS advance. */
    assert(c.pixels[1 * 16 + 1] == 0xff112233);
    assert(c.pixels[1 * 16 + 9] == 0xff112233);
    assert(storage[0] == 0x12345678 && storage[16 * 12 + 1] == 0x12345678);
    memset(c.pixels, 0, 16 * 12 * 4);
    c.scale_percent = 50; c.x1 = 32; c.y1 = 24;
    sb_fill(&c, 2, 2, 4, 4, 0x010203);
    assert(c.pixels[1 * 16 + 1] == 0xff010203);
    assert(c.pixels[2 * 16 + 2] == 0xff010203);
    assert(!c.pixels[3 * 16 + 3]);
    puts("Zoom clipping, image mapping and font re-rasterization checks passed.");
    return 0;
}
