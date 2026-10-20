/* Copyright (c) 2026 mio. 基于系统分配/文件函数的窄 C 运行时补充。 */
#include "sb.h"
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_NO_UNALIGNED
#define STB_SPRINTF_DECORATE(n) sbsp_##n
#include "stb_sprintf.h"

FILE *stdin, *stdout, *stderr;
extern FILE *__scc_standard(unsigned);
extern void (*__sb_ctors_start[])(void), (*__sb_ctors_end[])(void);
static int sb_errno;
char __libc_single_threaded = 1;
void *__dso_handle;
int *__errno_location(void) {
    return &sb_errno;
}
int vsnprintf(char *s, size_t n, const char *f, va_list a) {
    return sbsp_vsnprintf(s, n, f, a);
}
int snprintf(char *s, size_t n, const char *f, ...) {
    va_list a;
    va_start(a, f);
    int r = vsnprintf(s, n, f, a);
    va_end(a);
    return r;
}
int vsprintf(char *s, const char *f, va_list a) {
    return sbsp_vsprintf(s, f, a);
}
int sprintf(char *s, const char *f, ...) {
    va_list a;
    va_start(a, f);
    int r = vsprintf(s, f, a);
    va_end(a);
    return r;
}
typedef struct ExitHook {
    void (*f)(void *);
    void *arg;
    struct ExitHook *next;
} ExitHook;
static ExitHook *hooks;
int __cxa_atexit(void (*f)(void *), void *arg, void *dso) {
    (void)dso;
    ExitHook *h = malloc(sizeof(*h));
    if (!h)
        return -1;
    h->f = f;
    h->arg = arg;
    h->next = hooks;
    hooks = h;
    return 0;
}
void __cxa_finalize(void *dso) {
    (void)dso;
    while (hooks) {
        ExitHook *h = hooks;
        hooks = h->next;
        h->f(h->arg);
        free(h);
    }
}
int SbRuntimeInit(void) {
    stdin = __scc_standard(0);
    stdout = __scc_standard(1);
    stderr = __scc_standard(2);
#ifndef SB_NO_CTORS
    for (void (**p)(void) = __sb_ctors_start; p < __sb_ctors_end; p++)
        if (*p)
            (*p)();
#endif
    return 0;
}
void SbRuntimeFini(void) {
    __cxa_finalize(0);
}
void __stack_chk_fail(void) {
    sb_log("SandBrowser: runtime stack check failed.\n");
    ScProcessExit(127);
    for (;;) {
    }
}
void __assert_fail(const char *a, const char *file, unsigned line, const char *function) {
    sb_log("SandBrowser assertion: %s at %s:%u (%s)\n", a, file, line, function);
    ScProcessExit(126);
    for (;;) {
    }
}
int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return x - y;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncasecmp(const char *a, const char *b, size_t n) {
    while (n--) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
    return 0;
}
char *strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (p)
        memcpy(p, s, n);
    return p;
}
char *strndup(const char *s, size_t n) {
    size_t k = 0;
    while (k < n && s[k])
        k++;
    char *p = malloc(k + 1);
    if (p) {
        memcpy(p, s, k);
        p[k] = 0;
    }
    return p;
}
size_t strnlen(const char *s, size_t n) {
    size_t k = 0;
    while (k < n && s[k])
        k++;
    return k;
}
void *mempcpy(void *d, const void *s, size_t n) {
    memcpy(d, s, n);
    return (char *)d + n;
}
void bzero(void *p, size_t n) {
    memset(p, 0, n);
}
/* IEEE 数字转换由 litehtml/musl 的实现提供；此处不放数学近似桩。 */
long __isoc23_strtol(const char *s, char **p, int b) {
    return strtol(s, p, b);
}
unsigned long __isoc23_strtoul(const char *s, char **p, int b) {
    return strtoul(s, p, b);
}
size_t wcslen(const wchar_t *s) {
    const wchar_t *p = s;
    while (*p)
        p++;
    return p - s;
}
wchar_t *wmemcpy(wchar_t *d, const wchar_t *s, size_t n) {
    memcpy(d, s, n * sizeof(wchar_t));
    return d;
}
/* x87 环境恢复保证 nearbyint 不新增 FE_INEXACT。 */
double nearbyint(double x) {
    unsigned char env[28];
    double y;
    __asm__ volatile("fnstenv %0; fldl %2; frndint; fstpl %1; fldenv %0"
                     : "=m"(env), "=m"(y)
                     : "m"(x)
                     : "st", "memory");
    return y;
}
float nearbyintf(float x) {
    unsigned char env[28];
    float y;
    __asm__ volatile("fnstenv %0; flds %2; frndint; fstps %1; fldenv %0"
                     : "=m"(env), "=m"(y)
                     : "m"(x)
                     : "st", "memory");
    return y;
}
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    uint32_t days, sec;
    if (sb_utc(&days, &sec))
        return -1;
    tv->tv_sec = (days - 719528u) * 86400u + sec;
    tv->tv_usec = 0;
    return 0;
}
time_t time(time_t *out) {
    struct timeval tv;
    if (gettimeofday(&tv, 0))
        return (time_t)-1;
    if (out)
        *out = tv.tv_sec;
    return tv.tv_sec;
}
double sb_date_now(void) {
    uint32_t d, s;
    if (sb_utc(&d, &s))
        return 0.0 / 0.0;
    return ((double)d - 719528.0) * 86400000.0 + (double)s * 1000.0;
}
int sb_date_tz(double d) {
    (void)d;
    return 0;
}
void sb_fatal(void *context, const char *message) {
    (void)context;
    sb_log("SandBrowser JS fatal: %s\n", message ? message : "unknown");
    ScProcessExit(125);
    for (;;) {
    }
}
