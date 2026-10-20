/* Copyright (c) 2026 mio. TLS 强熵与证书校验时钟；不回退到计时器种子。 */
#include "sb.h"

unsigned sb_random_sources(void) {
    unsigned a = 0, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    unsigned maximum = a, sources = 0;
    if (maximum >= 1) {
        a = 1;
        c = 0;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
        if (c & (1u << 30))
            sources |= SB_RNG_RDRAND;
    }
    if (maximum >= 7) {
        a = 7;
        c = 0;
        __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
        if (b & (1u << 18))
            sources |= SB_RNG_RDSEED;
    }
    return sources;
}
static int hardware_word(unsigned sources, uint32_t *out) {
    unsigned char ok;
    uint32_t value;
    if (sources & SB_RNG_RDSEED)
        for (unsigned attempt = 0; attempt < 64; attempt++) {
            __asm__ volatile(".byte 0x0f,0xc7,0xf8; setc %1"
                             : "=a"(value), "=qm"(ok) :: "cc");
            if (ok) {
                *out = value;
                return 0;
            }
            __asm__ volatile("rep; nop");
        }
    if (sources & SB_RNG_RDRAND)
        for (unsigned attempt = 0; attempt < 32; attempt++) {
            __asm__ volatile(".byte 0x0f,0xc7,0xf0; setc %1"
                             : "=a"(value), "=qm"(ok) :: "cc");
            if (ok) {
                *out = value;
                return 0;
            }
            __asm__ volatile("rep; nop");
        }
    return -1;
}
int sb_random(void *p, unsigned n) {
    if (!n)
        return 0;
    if (!p)
        return -1;
    unsigned total = n, sources = sb_random_sources();
    memset(p, 0, n);
    unsigned char *out = p;
    while (n) {
        uint32_t value;
        if (hardware_word(sources, &value)) {
            memset(p, 0, total);
            return -1;
        }
        unsigned take = n < 4 ? n : 4;
        memcpy(out, &value, take);
        out += take;
        n -= take;
    }
    return 0;
}
int sb_utc(uint32_t *days, uint32_t *seconds) {
    if (!days || !seconds)
        return -1;
    ScUtcClock c;
    sb_record(&c, sizeof(c));
    if (ScSystemClock(&c) != SC32_OK || c.year < 2020 || c.year > 2099)
        return -1;
    unsigned y = c.year, m = c.month;
    const unsigned month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const unsigned before_month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    unsigned leap = y % 4 == 0 && (y % 100 || y % 400 == 0);
    if (m < 1 || m > 12 || !c.day ||
        c.day > month_days[m - 1] + (m == 2 && leap) ||
        c.hour > 23 || c.minute > 59 || c.second > 59)
        return -1;
    unsigned before = y - 1;
    *days = 365 * y + before / 4 - before / 100 + before / 400 + 1 +
            before_month[m - 1] + c.day - 1 + (m > 2 && leap);
    *seconds = c.hour * 3600 + c.minute * 60 + c.second;
    return 0;
}
